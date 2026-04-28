# UnrealCSharp 源码分析

> **分析对象**: UnrealCSharp（Reference 快照）
> **源码路径**: `Reference/UnrealCSharp/`
> **对比基准**: `Plugins/Angelscript/`
> **分析日期**: 2026-04-08

UnrealCSharp 是基于 Mono/.NET 运行时的 UE C# 脚本插件。本轮不追求把 D1-D11 全部写成浅层目录摘抄，而是聚焦 D1 / D2 / D3 / D6 / D8 / D11 六个证据最完整的维度，直接回答架构、绑定、Blueprint 互操作、代码生成、性能路径和部署打包这六个核心问题。

与当前 Angelscript 相比，UnrealCSharp 的突出特征不是“能跑 C#”本身，而是把 `UHT/编辑器生成 -> C# 工程生成 -> 编译/Publish -> Runtime 装载 -> UFunction override 替换 -> Packaging Staging` 串成一条闭环流水线。Angelscript 在 `StaticJIT / PrecompiledData / Bind DB / 绑定覆盖率统计` 上更偏运行时与发布优化，而 UnrealCSharp 在 IDE 与工程组织上的完成度更高。

## 插件架构总览

```
[UnrealCSharp] Plugin Architecture
├─ UnrealCSharp                     // Runtime bridge，负责 UObject/UFunction 到 C# 的桥接
│  ├─ Environment                   // 域与 registry 聚合
│  ├─ Registry                      // 函数/对象/容器/委托注册
│  └─ Reflection                    // UFunction trampoline
├─ UnrealCSharpCore                 // Mono/.NET domain、动态类、配置与路径
│  ├─ Domain
│  ├─ Dynamic
│  └─ Common
├─ UnrealCSharpEditor               // 编辑器菜单、代码生成入口、打包设置
├─ ScriptCodeGenerator              // UHT 驱动的 C# 声明生成
├─ Compiler                         // 编译 / Publish 管线
├─ CrossVersion                     // UE 版本差异屏蔽层
└─ SourceCodeGenerator              // Program target，辅助生成工具
```

`Reference/UnrealCSharp/UnrealCSharp.uplugin:18-60` 显示它把 Runtime、Editor、Generator、Compiler、Program 明确拆开；`Plugins/Angelscript/Angelscript.uplugin:18-48` 则保持 `Runtime + Editor + Test` 三模块结构。后文可以看到，这种模块划分差异会直接影响绑定生成方式、IDE 支撑能力和部署链路。

## [维度 D1] 插件架构与模块划分

### 实现概述

UnrealCSharp 的模块划分是明显的“流水线拆层”。`UnrealCSharp` Runtime 模块只保留桥接层，`UnrealCSharpCore` 持有 Mono domain、动态类和公共路径能力，`UnrealCSharpEditor` 负责编辑器按钮与生成流程调度，`ScriptCodeGenerator` / `Compiler` 则把声明生成和编译发布独立成 Editor 模块。`Reference/UnrealCSharp/Source/UnrealCSharpEditor/UnrealCSharpEditor.Build.cs:58-60` 直接依赖 `ScriptCodeGenerator` 和 `Compiler`，说明这些工具链不是散落在 Runtime 里的 helper，而是被当成一等模块对待。

Angelscript 则是“Runtime 中心化”结构。`Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs:20-22` 直接把 `ThirdParty/angelscript/source` 加入 include path，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp:13-18` 在 `StartupModule()` 内直接进入 `InitializeAngelscript()`。这意味着 VM 初始化、绑定注册、预编译数据装载和调试主路径都收敛在 `AngelscriptRuntime`。

```
[D1] Module Dependency Comparison

UnrealCSharpEditor
├─ ScriptCodeGenerator              // C# 声明生成
├─ Compiler                         // 编译 / Publish
└─ UnrealCSharpCore                 // 共享配置与路径

UnrealCSharp
├─ UnrealCSharpCore                 // Mono domain / dynamic class / settings
└─ CrossVersion                     // UE 版本兼容层

UnrealCSharpCore
└─ Mono                            // 外部运行时模块依赖

AngelscriptEditor
└─ AngelscriptRuntime               // 复用 runtime 核心

AngelscriptTest
└─ AngelscriptRuntime               // 测试直接贴 runtime

AngelscriptRuntime
└─ ThirdParty/angelscript/source    // 插件内嵌 VM 源码
```

### 关键源码引用

关键源码 [1] `Reference/UnrealCSharp/UnrealCSharp.uplugin:18-60`

```jsonc
// ============================================================================
// 文件: Reference/UnrealCSharp/UnrealCSharp.uplugin
// 位置: 模块清单
// 说明: Runtime / Editor / Program 被明确拆成 7 个模块
// ============================================================================
"Modules": [
    {
        "Name": "UnrealCSharp",
        "Type": "Runtime"
    },
    {
        "Name": "UnrealCSharpEditor",
        "Type": "Editor"
    },
    {
        "Name": "ScriptCodeGenerator",
        "Type": "Editor" // ★ 代码生成是独立模块，不塞进 Runtime
    },
    {
        "Name": "Compiler",
        "Type": "Editor" // ★ 编译/Publish 也是独立模块
    },
    {
        "Name": "UnrealCSharpCore",
        "Type": "Runtime" // ★ Mono domain / 动态类 / 配置中枢
    },
    {
        "Name": "CrossVersion",
        "Type": "Runtime"
    },
    {
        "Name": "SourceCodeGenerator",
        "Type": "Program"
    }
]
```

关键源码 [2] `Reference/UnrealCSharp/Source/UnrealCSharpCore/UnrealCSharpCore.build.cs:38-128`

```csharp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/UnrealCSharpCore.build.cs
// 函数: UnrealCSharpCore(ReadOnlyTargetRules Target)
// 位置: 运行时核心依赖声明
// ============================================================================
PublicDependencyModuleNames.AddRange(
    new string[]
    {
        "Core",
        "Projects",
        "Mono" // ★ 运行时通过 Mono 模块接入 CLR，而不是在当前插件目录内直接编译第三方源码
    }
);

if (Target.bBuildEditor)
{
    PublicDependencyModuleNames.AddRange(
        new string[]
        {
            "DirectoryWatcher"
        }
    );

    PrivateDependencyModuleNames.AddRange(
        new string[]
        {
            "BlueprintGraph",
            "UnrealEd"
        }
    );
}

PrivateDependencyModuleNames.AddRange(
    new string[]
    {
        "CoreUObject",
        "Engine",
        "Slate",
        "SlateCore",
        "Json",
        "CrossVersion",
        "Projects"
    }
);

GeneratorModules(); // ★ 在 Build 阶段继续派生生成模块配置
...
PublicDefinitions.Add($"WITH_BINDING={(GetBoolValue("bEnableExport", false) ? "1" : "0")}"); // ★ 绑定导出开关由 ini 驱动
```

关键源码 [3] `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp:54-133`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp
// 函数: FCSharpEnvironment::Initialize
// 位置: Runtime 启动时聚合所有 registry 与 domain
// ============================================================================
Domain = new FDomain({
    "",
    FUnrealCSharpFunctionLibrary::GetFullAssemblyPublishPath() // ★ 直接按 Publish 产物路径启动 CLR
});

DynamicRegistry = new FDynamicRegistry();
CSharpBind = new FCSharpBind();
ClassRegistry = new FClassRegistry();
ReferenceRegistry = new FReferenceRegistry();
ObjectRegistry = new FObjectRegistry();
StructRegistry = new FStructRegistry();
ContainerRegistry = new FContainerRegistry();
DelegateRegistry = new FDelegateRegistry();
MultiRegistry = new FMultiRegistry();
StringRegistry = new FStringRegistry();
BindingRegistry = new FBindingRegistry();
// ★ 这里不是单一 VM 单例，而是把对象、结构体、容器、委托、binding address 拆成多个 registry

FUnrealCSharpModuleDelegates::OnCSharpEnvironmentInitialize.Broadcast();
```

关键源码 [4] `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs:20-88`

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs
// 函数: AngelscriptRuntime(ReadOnlyTargetRules Target)
// 位置: Runtime 直接接入内嵌 AngelScript VM 源码
// ============================================================================
var AngelscriptThirdPartyPath = Path.Combine(ModuleDirectory, "ThirdParty", "angelscript");
PublicIncludePaths.Add(Path.Combine(AngelscriptThirdPartyPath, "source")); // ★ 直接暴露插件内嵌源码目录
PublicIncludePaths.Add(AngelscriptThirdPartyPath);

PublicDependencyModuleNames.AddRange(new string[]
{
    "ApplicationCore",
    "Core",
    "CoreUObject",
    "Engine",
    "EngineSettings",
    "DeveloperSettings",
    "Json",
    "JsonUtilities",
    "GameplayTags",
    "StructUtils",
});

PrivateDependencyModuleNames.AddRange(new string[]
{
    "AIModule",
    "NavigationSystem",
    "NetCore",
    "Landscape",
    "Networking",
    "Sockets",
    "InputCore",
    "SlateCore",
    "Slate",
    "UMG",
    "TraceLog",
    "AssetRegistry",
    "Projects",
    "PhysicsCore",
    "CoreOnline",
    "EnhancedInput",
    "GameplayAbilities",
    "GameplayTasks",
}); // ★ 绑定层和运行时层集中在同一个 Runtime 模块里
```

### 设计取舍

- UnrealCSharp 以模块解耦换来清晰的 toolchain 边界：生成、编译、运行、跨版本适配分别可独立演进，但启动链更长、配置点更多。
- Angelscript 把 VM、绑定、预编译与调试主路径集中在 Runtime，构建和排查单点更直接，但 Runtime 模块更重，工具链职责更难继续外拆。
- 关于第三方运行时集成方式，这里要明确区分“推断”和“源码直证”：`UnrealCSharpCore.build.cs` 直证其依赖 `Mono` 模块；结合 `Reference/UnrealCSharp/` 目录下未见同级 `ThirdParty/Mono` 源码目录，可以推断它采用外部模块接入，而不是像 Angelscript 一样把 VM 源码嵌在插件 Runtime 目录里。

### 与 Angelscript 对比

| 观察点 | UnrealCSharp | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 模块粒度 | 7 个模块，含 `ScriptCodeGenerator` / `Compiler` / `Program` | 3 个模块，主能力集中在 `AngelscriptRuntime` | 实现方式不同 |
| 第三方运行时接入 | `UnrealCSharpCore` 依赖 `Mono` 模块，源码树中未见同级内嵌 VM | `AngelscriptRuntime` 直接 include `ThirdParty/angelscript/source` | 实现方式不同 |
| Runtime 组织 | `FCSharpEnvironment` 聚合 domain + 多 registry | `StartupModule()` 直接初始化 AngelScript 引擎与 bind/预编译链 | 实现方式不同 |
| 工具链边界 | 生成、编译、运行边界清晰 | 运行时集中度高 | 实现质量差异，UnrealCSharp 的职责边界更清楚 |

### 值得吸收

- 高优先级：把 Angelscript 的 UHT 代码生成、Runtime 绑定、预编译/发布诊断再拆清一层边界，至少做到“生成模块产物”和“Runtime 消费模块产物”是显式 contract。
- 中优先级：为 `ThirdParty/angelscript` 再加一层封装 module 或 façade，减少 Runtime 对第三方源码布局的直接感知。

## [维度 D2] 反射绑定机制

### 实现概述

UnrealCSharp 的反射绑定分成两段。第一段是编辑器期生成：`FGeneratorCore::BeginGenerator()` 读取 `SupportedModule`、`SupportedAssetPath`、`SupportedAssetClass` 和 `OverrideFunctionsMap`，随后 `FClassGenerator` / `FStructGenerator` / `FEnumGenerator` / `FBindingClassGenerator` 生成 C# 声明与绑定类。第二段是运行时 patch：`FCSharpBind` 枚举 `UClass` 上的 `UFunction` 和 interface 函数，匹配托管侧 `OverrideAttribute` 方法，并把命中的函数改造成 `UCSharpFunction::execCallCSharp` trampoline，最终通过 `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FCSharpFunctionDescriptor.cpp:17-100` 中的 `FCSharpFunctionDescriptor::CallCSharp()` 把调用转回 Mono。

Angelscript 当前并不是“纯手写绑定”。`AngelscriptFunctionTableExporter` 和 `AngelscriptFunctionTableCodeGenerator` 会在 UHT 阶段扫描 `BlueprintCallable / BlueprintPure`，生成 `AS_FunctionTable_*.cpp`、`AS_FunctionTable_Summary.json`、`AS_FunctionTable_ModuleSummary.csv` 和 skipped-entry CSV；但这些产物最终还是要落到 `FAngelscriptBinds::RegisterBinds / CallBinds` 这套显式注册机制上。也就是说，当前 Angelscript 已经把 BlueprintCallable 暴露从“全手写”推进到“UHT 自动补表 + runtime 显式注册”混合模式。

```
[D2] Reflection Binding Flow

[UnrealCSharp]
├─ [1] FGeneratorCore::BeginGenerator            // 读取模块过滤与 override map
├─ [2] FClassGenerator / FBindingClassGenerator  // 生成 C# 类型与 binding class
├─ [3] FCSharpBind scans UFunction + methods     // 匹配 OverrideAttribute
└─ [4] execCallCSharp trampoline                 // NativeFunc 跳回 Mono

[Angelscript]
├─ [1] UHT exporter scans BlueprintCallable/Pure
├─ [2] Generate AS_FunctionTable_*.cpp
├─ [3] Emit Summary / ModuleSummary / Skipped CSV
└─ [4] FAngelscriptBinds::CallBinds              // 仍由 runtime 显式注册生效
```

### 关键源码引用

关键源码 [1] `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:940-978`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp
// 函数: FGeneratorCore::BeginGenerator
// 位置: C# 声明生成前的输入配置整理
// ============================================================================
if (const auto UnrealCSharpEditorSetting = FUnrealCSharpFunctionLibrary::GetMutableDefaultSafe<
    UUnrealCSharpEditorSetting>())
{
    bIsSkipGenerateEngineModules = UnrealCSharpEditorSetting->IsSkipGenerateEngineModules();
    bIsGenerateAllModules = UnrealCSharpEditorSetting->IsGenerateAllModules();

    for (const auto& Module : UnrealCSharpEditorSetting->GetSupportedModule())
    {
        SupportedModule.Add(FString::Printf(TEXT("%s.%s"), *NAMESPACE_ROOT, *Module));
    }

    for (const auto& [Path] : UnrealCSharpEditorSetting->GetSupportedAssetPath())
    {
        SupportedAssetPath.Add(*FString::Printf(TEXT("%s/"), *Path));
    }
}

OverrideFunctionsMap = FUnrealCSharpFunctionLibrary::LoadFileToArray(FString::Printf(TEXT(
    "%s/%s.json"
),
    *FUnrealCSharpFunctionLibrary::GetCodeAnalysisPath(),
    *OVERRIDE_FUNCTION
)); // ★ override 能力不是临时反射扫一遍，而是把分析结果先沉淀成生成输入
```

关键源码 [2] `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FCSharpBind.cpp:179-304`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FCSharpBind.cpp
// 函数: FCSharpBind::Bind
// 位置: 运行时把托管 override 与 UFunction 对齐
// ============================================================================
for (TFieldIterator<UFunction> It(FoundClass, EFieldIteratorFlags::ExcludeSuper,
                                  EFieldIteratorFlags::ExcludeDeprecated,
                                  EFieldIteratorFlags::ExcludeInterfaces); It; ++It)
{
    if (const auto Function = *It)
    {
        Functions.Add(FUnrealCSharpFunctionLibrary::Encode(Function), Function);
    }
}

for (const auto& Interface : FoundClass->Interfaces)
{
    for (TFieldIterator<UFunction> It(Interface.Class, EFieldIteratorFlags::ExcludeSuper,
                                      EFieldIteratorFlags::ExcludeDeprecated,
                                      EFieldIteratorFlags::ExcludeInterfaces); It; ++It)
    {
        if (const auto Function = FoundClass->FindFunctionByName(It->GetFName()))
        {
            Functions.Add(FUnrealCSharpFunctionLibrary::Encode(Function), Function);
        }
    }
}

for (TFieldIteratorExt<UFunction> It(FoundClass, EFieldIteratorFlags::IncludeSuper,
                                     EFieldIteratorFlags::ExcludeDeprecated,
                                     EFieldIteratorFlags::IncludeInterfaces); It; ++It)
{
    if (auto Function = *It)
    {
        if (Function->HasAnyFunctionFlags(FUNC_BlueprintEvent) &&
            !Function->HasAnyFunctionFlags(FUNC_Final))
        {
            Functions.Emplace(FUnrealCSharpFunctionLibrary::Encode(...), Function);
            // ★ 这里只收集可 override 的 BlueprintEvent / interface 入口
        }
    }
}

for (const auto& [FunctionName, Function] : Functions)
{
    for (const auto& [MethodName, Method] : Methods)
    {
        if (Method != nullptr && Method->IsOverride())
        {
            ...
            if (MethodParamCount == FunctionParamCount)
            {
                Bind(NewClassDescriptor, FoundClass, MethodName, Function); // ★ 命中后把托管方法接到 UFunction
            }
        }
    }
}
```

关键源码 [3] `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/CSharpFunction.cpp:5-11`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/CSharpFunction.cpp
// 函数: UCSharpFunction::execCallCSharp
// 位置: UFunction NativeFunc 的最终跳板
// ============================================================================
DEFINE_FUNCTION(UCSharpFunction::execCallCSharp)
{
    if (const auto FunctionDescriptor = FCSharpEnvironment::GetEnvironment().GetOrAddFunctionDescriptor<
        FCSharpFunctionDescriptor>(GetTypeHash(Stack.CurrentNativeFunction)))
    {
        FunctionDescriptor->CallCSharp(Context, Stack, RESULT_PARAM); // ★ 真正把 UE 调用栈转回托管环境
    }
}
```

关键源码 [4] `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:21-63`

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs
// 函数: Export(IUhtExportFactory factory)
// 位置: UHT 导出器入口
// ============================================================================
[UhtExporter(
    Name = "AngelscriptFunctionTable",
    Description = "Exports Angelscript function table data",
    Options = UhtExporterOptions.Default | UhtExporterOptions.CompileOutput,
    CppFilters = ["AS_FunctionTable_*.cpp"],
    ModuleName = "AngelscriptRuntime")]
private static void Export(IUhtExportFactory factory)
{
    int generatedFileCount = AngelscriptFunctionTableCodeGenerator.Generate(factory);

    foreach (UhtModule module in factory.Session.Modules)
    {
        CountBlueprintCallableFunctions(...); // ★ 统计 BlueprintCallable/Pure 暴露面
    }

    WriteSkippedEntriesCsv(factory, skippedEntries);
    WriteSkippedReasonSummaryCsv(factory, skippedEntries); // ★ 直接输出 skipped 原因，为绑定缺口诊断服务
}
```

关键源码 [5] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:151-214`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp
// 函数: FAngelscriptBinds::RegisterBinds / CallBinds
// 位置: 绑定最终生效入口
// ============================================================================
void FAngelscriptBinds::RegisterBinds(FName BindName, int32 BindOrder, TFunction<void()> Function)
{
    GetBindArray().Add({BindName.IsNone() ? MakeUnnamedBindName() : BindName, BindOrder, MoveTemp(Function)});
}

void FAngelscriptBinds::CallBinds(const TSet<FName>& DisabledBindNames)
{
    for (const FBindFunction& Bind : GetSortedBindArray())
    {
        if (DisabledBindNames.Contains(Bind.BindName))
        {
            UE_LOG(Angelscript, Log, TEXT("Skipping bind '%s'"), *Bind.BindName.ToString());
            continue;
        }

        Bind.Function(); // ★ 无论来源是手写 bind 还是 UHT 生成 bind，最终都汇入这里
    }
}
```

### 设计取舍

- UnrealCSharp 的优势是“声明生成”和“运行时 override patch”打通了：生成期决定可见面，运行时决定谁接管 `UFunction`。代价是启动时仍要做一次反射匹配和 descriptor 建立。
- Angelscript 的优势不是完全自动，而是把自动化瞄准最痛的 `BlueprintCallable` 缺口，同时保留 `RegisterBinds()` 的显式控制力。这样更稳，但自动化覆盖面还没有扩展到完整的脚本侧类型系统和 override 生命周期。
- 这一维度最关键的结论是：当前 Angelscript 已经不是“纯手写绑定”；真正的差距在于 UnrealCSharp 的自动化更接近“声明系统 + runtime 接管”的闭环，而 Angelscript 的自动化更接近“函数表补洞 + 覆盖率诊断”。

### 与 Angelscript 对比

| 观察点 | UnrealCSharp | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 生成输入 | `SupportedModule` / `SupportedAssetPath` / `OverrideFunctionsMap` 驱动生成 | UHT exporter 扫描 `BlueprintCallable/Pure` | 实现方式不同 |
| 类型声明自动化 | `FClassGenerator` / `FStructGenerator` / `FEnumGenerator` / `FBindingClassGenerator` 组成完整声明链 | 已定位自动化主要集中在函数表与 skipped/summary 产物 | 实现质量差异，UnrealCSharp 更完整 |
| 最终注册生效方式 | `FCSharpBind` 扫描并接管 `UFunction` | `FAngelscriptBinds::CallBinds()` 统一显式注册 | 实现方式不同 |
| 绑定缺口诊断 | 本轮已定位到 override map 输入，但未见同等级 per-module coverage 报表 | `Summary.json + ModuleSummary.csv + Skipped CSV` 明确落盘 | 实现质量差异，Angelscript 诊断更细 |

### 值得吸收

- 高优先级：把 Angelscript UHT 产物从“函数表片段”升级为“runtime 可直接消费的绑定 metadata”，减少 runtime 再做重复推断。
- 高优先级：保留 `RegisterBinds()` 显式控制的前提下，引入更完整的声明生成闭环，尤其是 UClass/UStruct/UEnum 级别的脚本侧声明信息。
- 中优先级：继续强化当前已存在的 coverage 诊断优势，让生成报表直接指向 runtime 缺口和 reflective fallback 命中情况。

## [维度 D3] Blueprint 交互

### 实现概述

UnrealCSharp 的 Blueprint 互操作分两层。第一层是 override：托管方法通过 `OverrideAttribute` 暴露给 runtime，`Reference/UnrealCSharp/Script/UE/CoreUObject/Utils.cs:340-374` 会在反射扫描时把 `OverrideAttribute` 和 `UClassAttribute` 一起收集出来，随后 `FCSharpBind` 依函数名和参数个数把托管 override 绑回 `UFunction`，最终由 `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/CSharpFunction.cpp:5-11` 和 `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FCSharpFunctionDescriptor.cpp:17-100` 接管调用栈。第二层是动态 Blueprint binding：`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Domain/InternalCall/FRegisterEnhancedInputComponent.cpp:42-206` 能在运行时 `NewObject<UFunction>`、`AddFunctionToFunctionMap()`、追加到 `UBlueprintGeneratedClass::DynamicBindingObjects`，把 EnhancedInput 这样的 Blueprint 动态绑定也纳入 C#。

Angelscript 的 Blueprint 交互更偏“显式 specifier + 编译期校验 + 必要时 reflective fallback”。`BlueprintOverride` 在预处理阶段就会把函数标记成 `BlueprintEvent + BlueprintOverride` 并追加 `_Implementation` 后缀；`AngelscriptClassGenerator.cpp:443-520` 会在 editor 下检查父类中是否真的存在可覆写的 BlueprintEvent/BlueprintOverride，否则直接编译报错。若 BlueprintCallable 函数没有直连 bind，`BlueprintCallableReflectiveFallback.cpp` 会在满足参数限制时走 `ProcessEvent()` 反射调用。另一个特点是它常通过 `Plugins/Angelscript/Source/AngelscriptRuntime/BaseClasses/ScriptWorldSubsystem.h:7-129` 这类 Blueprintable 基类，把生命周期事件先声明成 `BP_*` 事件，再由脚本去实现。

```
[D3] Blueprint Interop Comparison

[UnrealCSharp]
C# method [Override]
├─ Utils collects attribute metadata
├─ FCSharpBind matches UFunction
├─ NativeFunc -> execCallCSharp
└─ FCSharpFunctionDescriptor marshals params back to Mono

EnhancedInput path
├─ InternalCall BindActionImplementation
├─ NewObject<UFunction>(RF_Transient)
├─ AddFunctionToFunctionMap
└─ UBlueprintGeneratedClass::DynamicBindingObjects

[Angelscript]
UFUNCTION(BlueprintOverride)
├─ Preprocessor marks BlueprintEvent + _Implementation
├─ ClassGenerator validates parent function legality
├─ direct bind or reflective fallback
└─ TargetObject->ProcessEvent(...)
```

### 关键源码引用

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Domain/InternalCall/FRegisterEnhancedInputComponent.cpp:42-206`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Domain/InternalCall/FRegisterEnhancedInputComponent.cpp
// 位置: EnhancedInput 的 Blueprint 动态绑定桥接
// ============================================================================
static MonoObject* GetDynamicBindingObjectImplementation(const FGarbageCollectionHandle InThisClass,
                                                         const FGarbageCollectionHandle InBindingClass)
{
    const auto ThisClass = FCSharpEnvironment::GetEnvironment().GetObject<UBlueprintGeneratedClass>(InThisClass);
    const auto BindingClass = FCSharpEnvironment::GetEnvironment().GetObject<UClass>(InBindingClass);

    if (ThisClass != nullptr && BindingClass != nullptr)
    {
        auto DynamicBindingObject = UBlueprintGeneratedClass::GetDynamicBindingObject(ThisClass, BindingClass);

        if (DynamicBindingObject == nullptr)
        {
            DynamicBindingObject = NewObject<UDynamicBlueprintBinding>(GetTransientPackage(), BindingClass);
            ThisClass->DynamicBindingObjects.Add(DynamicBindingObject); // ★ 直接把动态 binding object 注入到 BPGC
        }

        return FCSharpEnvironment::GetEnvironment().Bind(DynamicBindingObject);
    }
    return nullptr;
}

static void BindFunction(UClass* InClass, const FName* InFunctionName,
                         const TFunction<void(UFunction* InFunction)>& InProperty)
{
    if (InClass->FindFunctionByName(*InFunctionName))
    {
        return;
    }

    const auto Function = NewObject<UFunction>(InClass, *InFunctionName, EObjectFlags::RF_Transient);
    Function->FunctionFlags = FUNC_BlueprintEvent; // ★ 运行时临时合成 BlueprintEvent
    InProperty(Function);
    Function->Bind();
    Function->StaticLink(true);
    InClass->AddFunctionToFunctionMap(Function, *InFunctionName);

    FCSharpEnvironment::GetEnvironment().GetBind()->Bind(..., InClass, Function); // ★ 刚生成的 UFunction 立刻接入 C# bind
}
```

关键源码 [2] `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:1654-1680`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp
// 位置: BlueprintOverride specifier 预处理
// ============================================================================
else if (Spec.Name == PP_NAME_BlueprintOverride)
{
    if (FunctionDesc->bIsStatic)
    {
        MacroError(..., TEXT("Global UFUNCTION() may not be BlueprintOverride."));
        continue;
    }

    if (FunctionDesc->bBlueprintEvent)
    {
        MacroError(..., TEXT("cannot be both BlueprintEvent and BlueprintOverride."));
        continue;
    }

    if (!bHadCallable)
        FunctionDesc->bBlueprintCallable = false;

    FunctionDesc->bBlueprintEvent = true;
    FunctionDesc->bBlueprintOverride = true;
    FunctionDesc->ScriptFunctionName += TEXT("_Implementation"); // ★ 在编译期就改写脚本函数名，避免和原始事件冲突
}
```

关键源码 [3] `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:443-520`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 位置: BlueprintOverride 合法性校验
// ============================================================================
for (auto& Elem : FunctionMap)
{
    auto FunctionDesc = ClassData.NewClass->GetMethod(Elem.Key);
    if (FunctionDesc.IsValid() && FunctionDesc->bBlueprintOverride)
        continue;

    auto* UnrealParentFunction = GetBlueprintEventByScriptName(CodeSuperClass, Elem.Key);
    ...

    if (bHaveParentFunction && (FunctionDesc.IsValid() || bParentIsEvent))
    {
        if (bParentIsEvent)
        {
            FAngelscriptEngine::Get().ScriptCompileError(...,
                TEXT("override requires the BlueprintOverride function specifier."));
            // ★ 父类是 BlueprintEvent，但子类没显式写 BlueprintOverride，直接编译失败
        }
        else if (bParentIsCpp)
        {
            FAngelscriptEngine::Get().ScriptCompileError(...,
                TEXT("but is not a BlueprintEvent and cannot be overridden."));
        }
    }
}
```

关键源码 [4] `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp:290-420`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp
// 位置: BlueprintCallable 的 reflective fallback
// ============================================================================
bool InvokeReflectiveUFunctionFromGenericCall(
    asIScriptGeneric* InGeneric,
    UObject* TargetObject,
    UFunction* Function,
    bool bInjectMixinObject)
{
    uint8* ParameterBuffer = static_cast<uint8*>(FMemory_Alloca(Function->ParmsSize));
    InitializeParameterBuffer(Function, ParameterBuffer);

    for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
    {
        ...
        Property->CopySingleValue(Destination, SourceAddress); // ★ 逐参把脚本侧数据拷进 UE 参数缓冲
    }

    TargetObject->ProcessEvent(Function, ParameterBuffer); // ★ 真正通过 UE 反射层调用 Blueprint/UFunction

    if (FProperty* ReturnProperty = Function->GetReturnProperty())
    {
        ReturnProperty->CopySingleValue(ReturnDestination,
            ReturnProperty->ContainerPtrToValuePtr<void>(ParameterBuffer));
    }

    DestroyParameterBuffer(Function, ParameterBuffer);
    return true;
}
```

### 设计取舍

- UnrealCSharp 的强项是“运行时可合成 `UFunction` + 立刻绑回 C#”，因此像 EnhancedInput 这种 Blueprint 动态绑定并不一定需要预先写死在 UHT 产物里。代价是 runtime 逻辑更复杂，合成函数、注册、GC handle 绑定都要在运行时维护。
- Angelscript 的强项是把 override 约束前移到编译期：`BlueprintOverride` 不只是语法糖，而是会在预处理器和类生成器里双重校验，能更早发现错误。代价是扩展新型 Blueprint 动态 binding 时，需要更多显式脚本 specifier 或额外 bind 逻辑。
- Reflective fallback 让 Angelscript 能在缺少直连 bind 时继续可用，但 `BlueprintCallableReflectiveFallback.cpp:277-280, 387-389` 明确限制了参数数量和签名有效性，说明这是一条兜底路径而不是默认高性能主路径。

### 与 Angelscript 对比

| 观察点 | UnrealCSharp | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 覆写声明方式 | `[Override]` attribute 由 runtime 反射收集并匹配 `UFunction` | `BlueprintOverride` 在预处理期改写并在类生成期校验 | 实现方式不同 |
| 覆写合法性检查时机 | 主要在 bind/descriptor 建立时对齐 | 编译期就能发现错误 override | 实现质量差异，Angelscript 更早暴露错误 |
| Blueprint 动态绑定 | 已定位到通用 `NewObject<UFunction>` + `DynamicBindingObjects` 路径 | 本轮未定位到等价的通用运行时 `UFunction` 合成入口 | 没有实现（就通用动态函数合成能力而言） |
| 缺口兜底 | 直接桥接到 C# descriptor | reflective fallback 走 `ProcessEvent()`，带参数上限 | 实现方式不同 |

### 值得吸收

- 高优先级：保持 Angelscript 现有编译期 override 校验优势的同时，补一条更通用的运行时 Blueprint dynamic binding 能力，减少对预声明基类事件的依赖。
- 中优先级：为 reflective fallback 增加命中统计与热点诊断，避免长期把高频路径留在 `ProcessEvent()` 兜底层。

## [维度 D6] 代码生成与 IDE 支持

### 实现概述

UnrealCSharp 的 IDE 支持不是单点工具，而是三段链路。第一段，`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:237-310` 的 `Generator()` 把 `FSolutionGenerator -> FCodeAnalysis -> FDynamicGenerator -> Class/Struct/Enum/Binding generators -> Compiler` 串起来；第二段，`Reference/UnrealCSharp/Script/SourceGenerator/UnrealTypeSourceGenerator.cs:135-208` 在编译期为 `UStruct` 等类型补出 `.gen.cs`、`StaticStruct()`、相等运算符和 `GarbageCollectionHandle` 等样板；第三段，Fody/Mono.Cecil `Reference/UnrealCSharp/Script/Weavers/UnrealTypeWeaver.cs:1075-1118` 在 IL 层进一步注入运行时实现入口。配合 `Reference/UnrealCSharp/Template/Game.props:3-28`，项目模板会自动引用 `SourceGenerator` 和 `Weavers`，并在 `AfterBuild` 后直接 `Publish` 与复制 DLL。

Angelscript 当前已定位到的自动化，更像“绑定覆盖率工程化”而不是“IDE 工程化”。`AngelscriptFunctionTableCodeGenerator.cs:142-265` 会写出 `AS_FunctionTable_Summary.json`、`AS_FunctionTable_ModuleSummary.csv`、`AS_FunctionTable_Entries.csv`，并按 module 输出 direct/stub rate；`AngelscriptGeneratedFunctionTableTests.cpp:505-527` 还会验证 summary 中的 `stubRate`、`directBindRate` 与真实生成注册数一致。也就是说，Angelscript 在“知道自己还缺哪些 bind”这件事上已经做得很工程化，但本轮尚未在源码中定位到与 UnrealCSharp 对等的脚本工程生成、声明文件生成和 IDE 跳转链。

```
[D6] Code Generation Pipeline

[UnrealCSharp]
Editor Generator
├─ FSolutionGenerator               // 生成工程与输出路径
├─ FCodeAnalysis                    // 分析 override / metadata
├─ FClass/Struct/EnumGenerator      // 生成 C# 声明
├─ Roslyn SourceGenerator           // 编译期补 .gen.cs
├─ Fody / Weaver                    // IL 注入运行时实现
└─ Compiler + Publish               // 输出 DLL 到 Content/<PublishDirectory>

[Angelscript]
UHT Tool
├─ Scan BlueprintCallable/Pure
├─ Generate AS_FunctionTable_*.cpp
├─ Emit Summary.json / ModuleSummary.csv
└─ Tests verify direct/stub rates
```

### 关键源码引用

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:237-310`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp
// 函数: FUnrealCSharpEditorModule::Generator
// 位置: 编辑器侧总代码生成入口
// ============================================================================
FUnrealCSharpCoreModuleDelegates::OnBeginGenerator.Broadcast();

FSolutionGenerator::Generator();          // ★ 先生成 solution / csproj 模板
FCodeAnalysis::CodeAnalysis();            // ★ 再做代码分析
FDynamicGenerator::CodeAnalysisGenerator();

FGeneratorCore::BeginGenerator();
FClassGenerator::Generator();
FStructGenerator::Generator();
FEnumGenerator::Generator();
FAssetGenerator::Generator();
FBindingClassGenerator::Generator();
FBindingEnumGenerator::Generator();
FGeneratorCore::EndGenerator();

CollectGarbage(RF_NoFlags, true);
FCSharpCompiler::Get().ImmediatelyCompile(); // ★ 生成完成后立刻编译
FUnrealCSharpCoreModuleDelegates::OnEndGenerator.Broadcast();
```

关键源码 [2] `Reference/UnrealCSharp/Script/SourceGenerator/UnrealTypeSourceGenerator.cs:135-208`

```csharp
// ============================================================================
// 文件: Reference/UnrealCSharp/Script/SourceGenerator/UnrealTypeSourceGenerator.cs
// 位置: Roslyn SourceGenerator 为 UStruct 自动补全样板
// ============================================================================
if (type.Value.DynamicType == EDynamicType.UStruct)
{
    var interfaceBody = type.Value.HasBase || type.Value.HasGarbageCollectionHandle
        ? ": IStaticStruct"
        : ": IStaticStruct, IGarbageCollectionHandle";

    source +=
        $"\nnamespace {type.Value.NameSpace}\n" +
        $"{{\n\t{type.Value.Modifiers} class {type.Value.Name}{interfaceBody}\n" +
        "\t{\n";

    if (type.Value.HasStaticStruct == false)
    {
        source +=
            $"\t\tpublic static UScriptStruct StaticStruct()\n" +
            "\t\t{\n" +
            $"\t\t\treturn StaticStructSingleton ??= UStructImplementation.UStruct_StaticStructImplementation(\"{fullPath}\");\n" +
            "\t\t}\n";
        // ★ 自动生成 StaticStruct()，让脚本侧获得接近原生 UE 类型体验
    }

    if (type.Value.HasGarbageCollectionHandle == false && type.Value.HasBase == false)
    {
        source += "\t\tpublic nint GarbageCollectionHandle { get; set; }\n";
    }

    source += "\t}\n";
    Context.AddSource(type.Value.NameSpace + "." + type.Value.Name + ".gen.cs", source); // ★ 编译期直接生成 .gen.cs
}
```

关键源码 [3] `Reference/UnrealCSharp/Template/Game.props:3-28`

```xml
<!-- =========================================================================
文件: Reference/UnrealCSharp/Template/Game.props
位置: C# 工程模板
说明: SourceGenerator / Weaver / Publish 被写进统一模板
=========================================================================== -->
<ItemGroup>
    <PackageReference Include="Fody" Version="6.9.3">
        <PrivateAssets>all</PrivateAssets>
    </PackageReference>
    <WeaverFiles Include="$(ProjectDir)..\Weavers\bin\$(Configuration)\netstandard2.0\Weavers.dll"
                 WeaverClassNames="UnrealTypeWeaver" />
</ItemGroup>
<ItemGroup>
    <ProjectReference Include="..\SourceGenerator\SourceGenerator.csproj"
                      OutputItemType="Analyzer"
                      ReferenceOutputAssembly="false"/>
    <ProjectReference Include="..\Weavers\Weavers.csproj"
                      ReferenceOutputAssembly="false" />
</ItemGroup>
<Target Name="AfterBuildPublish" AfterTargets="Build">
    <MSBuild Projects="$(ProjectPath)" Targets="Publish" Properties="Configuration=$(Configuration)" />
    <!-- ★ Build 完立即 Publish -->
</Target>
<Target Name="CopyDllsAfterPublish" AfterTargets="Publish">
    <Copy SourceFiles="@(PublishFiles)" DestinationFolder="$(ScriptOutputPath)" />
    <!-- ★ Publish 结果直接复制到脚本输出目录 -->
</Target>
```

关键源码 [4] `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:142-265`

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs
// 位置: 绑定覆盖率产物生成
// ============================================================================
private static void WriteGenerationSummary(IUhtExportFactory factory,
    List<AngelscriptModuleGenerationSummary> moduleSummaries,
    List<AngelscriptGeneratedFunctionCsvEntry> csvEntries,
    int generatedFileCount)
{
    int totalGeneratedEntries = moduleSummaries.Sum(static summary => summary.TotalEntries);
    int totalDirectBindEntries = moduleSummaries.Sum(static summary => summary.DirectBindEntries);
    int totalStubEntries = moduleSummaries.Sum(static summary => summary.StubEntries);

    string summaryPath = factory.MakePath("AS_FunctionTable_Summary", ".json");
    string summaryJson = JsonSerializer.Serialize(
        new
        {
            totalGeneratedEntries,
            totalDirectBindEntries,
            totalStubEntries,
            directBindRate,
            stubRate,
            modules = moduleSummaries.Select(summary => new
            {
                moduleName = summary.ModuleName,
                directBindEntries = summary.DirectBindEntries,
                stubEntries = summary.StubEntries,
                shardCount = summary.ShardCount,
            }),
        },
        new JsonSerializerOptions { WriteIndented = true });

    File.WriteAllText(summaryPath, summaryJson, Encoding.UTF8); // ★ 直接产出 machine-readable summary
    WriteModuleSummaryCsv(factory, moduleSummaries);
    WriteEntryCsv(factory, csvEntries);                         // ★ 同时落 CSV，方便 diff 与 CI
}
```

关键源码 [5] `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp:505-527`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp
// 位置: 自动生成函数表报表的测试校验
// ============================================================================
if (!TestTrue(TEXT("Generated function table summary test should expose stubRate"),
    SummaryObject->TryGetNumberField(TEXT("stubRate"), StubRate)))
{
    return false;
}

const int32 CountedRegistrations = CountGeneratedBindingRegistrations(GeneratedDirectory);
if (!TestTrue(TEXT("Generated function table summary test should count generated registration lines from UHT output"),
    CountedRegistrations > 0))
{
    return false;
}

TestEqual(TEXT("Generated function table summary test should match the generated binding registration count"),
    TotalGeneratedEntries, CountedRegistrations);
TestTrue(TEXT("Generated function table summary test should keep directBindRate and stubRate normalized"),
    FMath::Abs((DirectBindRate + StubRate) - 1.0) < 1.e-9);
// ★ 这说明 Angelscript 虽然没有对等的 IDE 工程链，但它对“生成结果是否可信”有明确自动测试
```

### 设计取舍

- UnrealCSharp 把 IDE 体验当成插件的一部分来构建，而不是额外脚本。工程模板、SourceGenerator、Weaver、Publish 目标都沉淀成源码，因此 IDE 友好度是结构性产物。
- Angelscript 当前更强调“绑定生成质量可观测”，而不是“脚本工程可直接被现成 IDE 深度理解”。这并不代表它不能开发，而是说明投入优先级目前更偏向绑定覆盖率与运行时能力。
- 本轮要谨慎区分“没有定位到”和“绝对没有实现”：我在 `Plugins/Angelscript/` 范围内全局检索了 `IntelliSense|completion|Language Server|solution generator|declaration file` 等关键词，未定位到与 UnrealCSharp 对等的 IDE 工程链源码，因此这里只能下“本轮未定位到等价实现”的结论，而不是泛化成绝对没有。

### 与 Angelscript 对比

| 观察点 | UnrealCSharp | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 生成总入口 | `Generator()` 串起 solution、分析、生成、编译 | UHT tool 主要负责函数表生成与统计 | 实现质量差异，UnrealCSharp 更完整 |
| 编译期声明补全 | Roslyn SourceGenerator 输出 `.gen.cs` | 本轮未定位到等价声明生成链 | 没有实现（就同等级声明生成链而言） |
| 编译后注入 | Fody / Weaver 继续做 IL 级补强 | 本轮未定位到等价后处理链 | 没有实现（就同等级后处理链而言） |
| 生成质量诊断 | 本轮已见生成与分析输入，但未见同等级 per-module bind coverage 报表 | `Summary.json + CSV + tests` 已形成闭环 | 实现质量差异，Angelscript 诊断更强 |

### 值得吸收

- 高优先级：为 Angelscript 增加“可被 IDE 直接消费”的声明产物，不必复制 C# 工程体系，但至少应提供稳定的脚本 API 索引或 stub 文件生成。
- 中优先级：保留当前 `Summary.json / CSV / tests` 的优势，并把这些诊断结果继续接到编辑器 UI 或命令行报错里。

## [维度 D8] 性能与优化

### 实现概述

UnrealCSharp 的性能策略核心在于“把 managed/native 边界固定成若干可预测的桥接形态”。`FMonoDomain::Initialize()` 在 iOS 上显式切到 `MONO_AOT_MODE_INTERP`，其余平台默认 `MONO_AOT_MODE_NONE`；`FFunctionImplementation.cs:7-102` 提供了多组固定形态的 `InternalCall` 入口，参数缓冲和返回缓冲由 native 侧统一管理，调用查找依赖 `InFunctionHash`；`FBindingRegistry.cpp:19-68` 则用 `FGarbageCollectionHandle` 维护 native address 与 `MonoObject*` 的双向映射，避免每次都重新包装绑定对象。

Angelscript 的优化重心更偏原生 VM。`AngelscriptEngine.cpp:1433-1599` 在生成预编译数据时会同时启用 `FAngelscriptStaticJIT`，运行时可选择直接加载 `PrecompiledScript*.Cache`；`AngelscriptStateDump.cpp:1038-1099` 能把 `PrecompiledData` 和 `StaticJITState` 导出成 CSV；`FAngelscriptPooledContextBase::Init()` 则明确从线程本地池复用 context，避免频繁创建执行上下文。换言之，UnrealCSharp 的优化重点是托管边界桥接成本，Angelscript 的优化重点是脚本 VM 自身执行与装载成本。

```
[D8] Performance Path Comparison

[UnrealCSharp]
MonoDomain::Initialize
├─ iOS -> MONO_AOT_MODE_INTERP
├─ others -> MONO_AOT_MODE_NONE
├─ InternalCall function families
└─ BindingRegistry via GC handles

[Angelscript]
Engine init
├─ Bind DB load
├─ PrecompiledData load/save
├─ StaticJIT optional generation
└─ Thread-local context pool reuse
```

### 关键源码引用

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp:46-133`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp
// 函数: FMonoDomain::Initialize
// 位置: Mono JIT / AOT 策略选择
// ============================================================================
if (Domain == nullptr)
{
#if PLATFORM_IOS
    mono_jit_set_aot_mode(MONO_AOT_MODE_INTERP); // ★ iOS 明确走 AOT + interp
    mono_aot_register_module(static_cast<void**>(mono_aot_module_System_Private_CoreLib_info));
    ...
#else
    mono_jit_set_aot_mode(MONO_AOT_MODE_NONE);   // ★ 其它平台默认允许 JIT
#endif

    if (const auto UnrealCSharpSetting = FUnrealCSharpFunctionLibrary::GetMutableDefaultSafe<
        UUnrealCSharpSetting>())
    {
        if (UnrealCSharpSetting->IsEnableDebug())
        {
            mono_jit_parse_options(...);         // ★ 调试配置直接注入 runtime
        }
    }

    mono_debug_init(MONO_DEBUG_FORMAT_MONO);
    Domain = mono_jit_init("UnrealCSharp");
}
```

关键源码 [2] `Reference/UnrealCSharp/Script/UE/Library/FFunctionImplementation.cs:7-102`

```csharp
// ============================================================================
// 文件: Reference/UnrealCSharp/Script/UE/Library/FFunctionImplementation.cs
// 位置: 托管到 native 的固定桥接入口族
// ============================================================================
[MethodImpl(MethodImplOptions.InternalCall)]
public static extern void FFunction_GenericCall0Implementation(nint InMonoObject, uint InFunctionHash);

[MethodImpl(MethodImplOptions.InternalCall)]
public static extern void FFunction_PrimitiveCall3Implementation(nint InMonoObject, uint InFunctionHash,
    byte* InBuffer, byte* ReturnBuffer);

[MethodImpl(MethodImplOptions.InternalCall)]
public static extern void FFunction_CompoundCall7Implementation(nint InMonoObject, uint InFunctionHash,
    byte* InBuffer, byte* OutBuffer, byte* ReturnBuffer);

[MethodImpl(MethodImplOptions.InternalCall)]
public static extern void FFunction_GenericCall24Implementation(nint InMonoObject, uint InFunctionHash);
// ★ 调用族不是“一函数一个 P/Invoke”，而是按入参/出参/返回值形态复用固定桥接桩
```

关键源码 [3] `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FBindingRegistry.cpp:19-68`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FBindingRegistry.cpp
// 位置: native binding address 与托管对象的映射
// ============================================================================
for (auto& [Key, Value] : GarbageCollectionHandle2BindingAddress.Get())
{
    FGarbageCollectionHandle::Free<true>(Key);

    if (Value.bNeedFree)
    {
        delete Value.AddressWrapper;
    }
}

MonoObject* FBindingRegistry::GetObject(const FBindingValueMapping::FAddressType InAddress)
{
    const auto FoundGarbageCollectionHandle = BindingAddress2GarbageCollectionHandle.Find(InAddress);
    return FoundGarbageCollectionHandle != nullptr
        ? static_cast<MonoObject*>(*FoundGarbageCollectionHandle)
        : nullptr;
}
// ★ 通过双向映射缓存包装对象，减少反复构造 binding wrapper 的成本
```

关键源码 [4] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1433-1599`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 预编译数据与 StaticJIT 主路径
// ============================================================================
if (bGeneratePrecompiledData)
{
    PrecompiledData = new FAngelscriptPrecompiledData(Engine);
    StaticJIT = new FAngelscriptStaticJIT();
    StaticJIT->PrecompiledData = PrecompiledData;
    Engine->SetJITCompiler(StaticJIT); // ★ 生成预编译数据时同时接入 StaticJIT
}

if (bUsePrecompiledData)
{
    FString Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript.Cache");
    if (IFileManager::Get().FileExists(*Filename))
    {
        PrecompiledData = new FAngelscriptPrecompiledData(Engine);
        PrecompiledData->Load(Filename); // ★ 运行时直接装载缓存脚本
        ...
        if (!bScriptDevelopmentMode)
            PrecompiledData->bMinimizeMemoryUsage = true;
    }
}

if (bGeneratePrecompiledData)
{
    FString Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript.Cache");
    PrecompiledData->InitFromActiveScript();
    PrecompiledData->Save(Filename);    // ★ 生成模式下直接落盘缓存
}
```

关键源码 [5] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1795-1815`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: FAngelscriptPooledContextBase::Init
// 位置: 执行上下文池
// ============================================================================
asCContext* ActiveContext = tld->activeContext;
if (ActiveContext != nullptr)
{
    auto State = ActiveContext->m_status;
    if (State == asEXECUTION_ACTIVE
        && (DesiredScriptEngine == nullptr || ActiveContext->GetEngine() == DesiredScriptEngine))
    {
        Context = ActiveContext;
        Context->PushState();
        bWasNested = true;
        return;
    }
}

// Take a context from the thread-local pool if we have one
auto& LocalPool = GAngelscriptContextPool;
// ★ 显式复用线程本地 context，避免高频脚本执行时反复分配
```

关键源码 [6] `Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp:783-803`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp
// 位置: VM GC 统计导出
// ============================================================================
asUINT GCCurrentSize = 0;
asUINT GCTotalDestroyed = 0;
asUINT GCTotalDetected = 0;
asUINT GCNewObjects = 0;
asUINT GCTotalNewDestroyed = 0;
ScriptEngine->GetGCStatistics(&GCCurrentSize, &GCTotalDestroyed, &GCTotalDetected, &GCNewObjects, &GCTotalNewDestroyed);

AddStat(TEXT("GCCurrentSize"), LexToString(GCCurrentSize));
AddStat(TEXT("GCTotalDestroyed"), LexToString(GCTotalDestroyed));
AddStat(TEXT("GCTotalDetected"), LexToString(GCTotalDetected));
AddStat(TEXT("GCNewObjects"), LexToString(GCNewObjects));
AddStat(TEXT("GCTotalNewDestroyed"), LexToString(GCTotalNewDestroyed));
// ★ Angelscript 直接把 VM 级 GC 指标暴露给 dump 系统
```

### 设计取舍

- UnrealCSharp 的优化更像“桥接层优化”：函数哈希、固定形态 `InternalCall`、绑定对象缓存、平台差异化 AOT/JIT 选择，目标是尽量压低 managed/native 边界的可变成本。
- Angelscript 的优化更像“VM 主路径优化”：预编译缓存、StaticJIT、context pooling、GC state dump 都围绕“让脚本 VM 本身跑得快、可观测、可部署”展开。
- 本轮没有任何 benchmark 数据，因此这里不能下“谁更快”的绝对结论。源码能证实的是：两者优化重心不同，UnrealCSharp 优化的是跨语言边界，Angelscript 优化的是脚本 VM 生命周期。

### 与 Angelscript 对比

| 观察点 | UnrealCSharp | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| JIT/AOT 路径 | iOS `MONO_AOT_MODE_INTERP`，其它平台 `MONO_AOT_MODE_NONE` | `PrecompiledData + StaticJIT` 可生成与装载缓存 | 实现方式不同 |
| 调用桥接 | 固定形态 `InternalCall` + function hash | VM 内部调用，可选 reflective fallback / JIT 代码 | 实现方式不同 |
| 对象生命周期管理 | `FGarbageCollectionHandle` 映射 native address 与 `MonoObject*` | VM 内部 GC，并可导出 GC 统计 | 实现方式不同 |
| 性能诊断 | 本轮已见 debug/runtime 配置，但未见同等级性能 dump 产物 | `StateDump` 可导出 `GC` / `PrecompiledData` / `StaticJITState` | 实现质量差异，Angelscript 诊断更强 |

### 值得吸收

- 高优先级：把 Angelscript 现有的 `StateDump` 诊断优势继续前推到“绑定 fallback 命中率 / 热路径脚本调用”层面。
- 中优先级：为 Angelscript 的高频桥接场景总结更固定的调用桩形态，减少泛型反射路径落在热点上。

## [维度 D11] 部署与打包

### 实现概述

UnrealCSharp 的部署思路是“程序集就是内容资产的一部分”。`FUnrealCSharpFunctionLibrary::GetFullPublishDirectory()` 把发布目录固定到 `Content/<PublishDirectory>`；`UnrealCSharpEditor.cpp:209-234` 会自动把该目录加入 `DirectoriesToAlwaysStageAsUFS`；`Template/Game.props:16-28` 在 `AfterBuild` 后直接执行 `Publish`，再把产出的 DLL 拷到脚本输出目录。这意味着它从项目模板开始就假设最终交付物是 `Content` 下的程序集文件。

Angelscript 的部署思路是“脚本根目录 + 缓存文件”。`GetScriptRootDirectory()` 返回工程脚本根，运行时会从这里读写 `Binds.Cache`、`PrecompiledScript*.Cache`；同时它还能发现启用插件的 `Script` 根目录，把插件脚本也并进搜索路径。若开启 fully precompiled data，则 `AngelscriptEngine.cpp:2046-2056` 会明确警告本次运行禁用 hot reload。与 UnrealCSharp 不同，本轮全局检索 `Plugins/Angelscript/` 未发现 `DirectoriesToAlwaysStageAsUFS` / `ProjectPackagingSettings` 相关源码，说明它的打包策略更多依赖脚本根目录与缓存装载逻辑，而不是编辑器主动改 Packaging Settings。

```
[D11] Deployment Comparison

[UnrealCSharp]
Build
├─ Publish assemblies
├─ Copy *.dll -> Content/<PublishDirectory>
└─ Add PublishDirectory to UFS staging

[Angelscript]
Cook / Runtime
├─ Discover Script roots
├─ Load Binds.Cache
├─ Load or save PrecompiledScript*.Cache
└─ Fully precompiled => hot reload disabled
```

### 关键源码引用

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp:995-1049`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp
// 位置: 程序集发布路径
// ============================================================================
FString FUnrealCSharpFunctionLibrary::GetFullPublishDirectory()
{
    return FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / GetPublishDirectory());
    // ★ PublishDirectory 固定挂在 ProjectContentDir 下
}

TArray<FString> FUnrealCSharpFunctionLibrary::GetFullAssemblyPublishPath()
{
    return TArrayBuilder<FString>().
           Add(GetFullUEPublishPath()).
           Add(GetFullGamePublishPath()).
           Append(GetFullCustomProjectsPublishPath()).
           Build(); // ★ Runtime 启动时就按这些 DLL 路径加载
}

TArray<FString> FUnrealCSharpFunctionLibrary::GetAssemblyPath()
{
    return TArrayBuilder<FString>().
           Add(FPaths::ProjectContentDir() / GetPublishDirectory()).
           Add(FMonoFunctionLibrary::GetNetDirectory()).
           Build();
}
```

关键源码 [2] `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:209-234`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp
// 函数: FUnrealCSharpEditorModule::UpdatePackagingSettings
// 位置: 自动更新打包 Staging 目录
// ============================================================================
const auto PublishDirectory = FUnrealCSharpFunctionLibrary::GetPublishDirectory();

if (const auto ProjectPackagingSettings = FUnrealCSharpFunctionLibrary::GetMutableDefaultSafe<
    UProjectPackagingSettings>())
{
    for (const auto& [Path] : ProjectPackagingSettings->DirectoriesToAlwaysStageAsUFS)
    {
        if (Path == PublishDirectory)
        {
            bIsExisted = true;
            break;
        }
    }

    if (!bIsExisted)
    {
        ProjectPackagingSettings->DirectoriesToAlwaysStageAsUFS.Add({PublishDirectory});
        ProjectPackagingSettings->TryUpdateDefaultConfigFile(); // ★ 编辑器主动把程序集目录加入 UFS Staging
    }
}
```

关键源码 [3] `Reference/UnrealCSharp/Template/Game.props:16-28`

```xml
<!-- =========================================================================
文件: Reference/UnrealCSharp/Template/Game.props
位置: Build 完成后的 Publish / Copy 策略
=========================================================================== -->
<Target Name="AfterBuildPublish" AfterTargets="Build">
    <MSBuild Projects="$(ProjectPath)" Targets="Publish" Properties="Configuration=$(Configuration)" />
</Target>
<Target Name="CopyDllsAfterPublish" AfterTargets="Publish">
    <ItemGroup>
        <PublishFiles Include="$(PublishDir)*.dll" />
    </ItemGroup>
    <Copy SourceFiles="@(PublishFiles)" DestinationFolder="$(ScriptOutputPath)" />
    <!-- ★ 发布与复制动作内建在项目模板里，不要求用户手工搬运 DLL -->
</Target>
```

关键源码 [4] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:558-566`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 插件脚本根发现
// ============================================================================
Dependencies.GetEnabledPluginScriptRoots = []()
{
    TArray<FString> ScriptRoots;
    for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetEnabledPluginsWithContent())
    {
        ScriptRoots.Add(Plugin->GetBaseDir() / TEXT("Script")); // ★ 启用插件的 Script 根目录也会进入搜索路径
    }
    return ScriptRoots;
};
```

关键源码 [5] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1431-1588`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: Bind DB 与 PrecompiledScript 缓存的读写
// ============================================================================
AllRootPaths = DiscoverScriptRoots(/*bOnlyProjectRoot =*/ true);

FAngelscriptBindDatabase::Get().Load(GetScriptRootDirectory() / TEXT("Binds.Cache"), bGeneratePrecompiledData);
// ★ Bind metadata 以缓存文件形式驻留在脚本根目录

if (bUsePrecompiledData)
{
    FString Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript.Cache");
    if (IFileManager::Get().FileExists(*Filename))
    {
        PrecompiledData = new FAngelscriptPrecompiledData(Engine);
        PrecompiledData->Load(Filename); // ★ 发布态可直接装载预编译脚本缓存
    }
}

if (bGeneratePrecompiledData)
{
    FString Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript.Cache");
    PrecompiledData->InitFromActiveScript();
    PrecompiledData->Save(Filename);     // ★ 生成模式把缓存写回脚本根目录
}
```

关键源码 [6] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2046-2056`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: fully precompiled data 对热重载的影响
// ============================================================================
if (PrecompiledData != nullptr && bUsePrecompiledData && !bScriptDevelopmentMode)
{
    bUsedPrecompiledDataForPreprocessor = true;
    ModulesToCompile = PrecompiledData->GetModulesToCompile();

#if AS_CAN_HOTRELOAD
    UE_LOG(Angelscript, Warning, TEXT("Using fully precompiled scripts. Hot reloading is disabled for this run."));
    UE_LOG(Angelscript, Warning, TEXT("Delete PrecompiledScript.Cache or run with -as-development-mode flag to enable hot reload."));
    // ★ fully precompiled 不是“更快且保留全部编辑体验”，而是主动牺牲 hot reload
#endif
}
```

### 设计取舍

- UnrealCSharp 把程序集当作内容目录中的可 Stage 文件处理，因此部署链与 UE Packaging Settings 紧密耦合，优点是交付路径清晰，缺点是更依赖工程模板与编辑器配置。
- Angelscript 把脚本与缓存都挂到脚本根目录，优点是部署模型更贴近脚本系统本身，缺点是 staging 行为没有像 UnrealCSharp 那样由编辑器自动宣告给 Packaging Settings。
- 这一维度不能简单写成“DLL 打包比脚本缓存更先进”或反过来。两者只是面向不同运行时模型：UnrealCSharp 发布的是程序集，Angelscript 发布的是源脚本与预编译缓存。

### 与 Angelscript 对比

| 观察点 | UnrealCSharp | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 交付物形态 | `Content/<PublishDirectory>` 下的 DLL | `Script` 根目录下的脚本、`Binds.Cache`、`PrecompiledScript*.Cache` | 实现方式不同 |
| 打包配置接入 | 编辑器自动写入 `DirectoriesToAlwaysStageAsUFS` | 本轮未定位到等价自动 staging hook | 没有实现（就自动 UFS staging hook 而言） |
| 构建后处理 | `AfterBuild -> Publish -> CopyDllsAfterPublish` 内建在模板 | 运行时读写脚本缓存文件 | 实现方式不同 |
| 发布态与热重载关系 | 本轮已见程序集 publish 路径，未见同等级“全预编译即禁热更”开关 | fully precompiled 明确禁用 hot reload | 实现方式不同 |

### 值得吸收

- 高优先级：为 Angelscript 增加更显式的 packaging/staging 宣告入口，至少让预编译缓存、bind 数据和脚本根目录的打包行为在源码层面可追踪、可配置。
- 中优先级：借鉴 UnrealCSharp 的“项目模板内建发布动作”思路，把 Angelscript 的预编译/缓存生成流程纳入更稳定的 cook/build 钩子。

## 小结

- UnrealCSharp 的核心优势是“全链路闭环”：模块拆分、代码生成、工程模板、编译发布、运行时 patch 和打包 staging 都有明确源码落点，不是单点功能堆积。
- Angelscript 在 D2/D6 上并非“没有自动化”，而是自动化重心不同：它已经把 `BlueprintCallable` 函数表生成、覆盖率摘要、skipped 原因和测试校验做得相当扎实，但尚未扩展成与 UnrealCSharp 对等的 IDE/声明闭环。
- Blueprint 交互是两者差异最大的维度之一。UnrealCSharp 更强调运行时合成和动态接管，Angelscript 更强调编译期约束与必要时的 reflective fallback。
- 性能与部署两条线都体现出运行时模型差异：UnrealCSharp 优化 managed/native 边界并发布程序集，Angelscript 优化脚本 VM 生命周期并发布脚本缓存。

## 与 Angelscript 差异速查

| 维度 | UnrealCSharp 观察结论 | Angelscript 现状 | 差距判断 | 吸收优先级 |
| --- | --- | --- | --- | --- |
| D1 架构 | 多模块流水线拆层，`Generator/Compiler/Core` 边界清晰 | Runtime 中心化，ThirdParty VM 直接嵌入 | 实现质量差异 | 高 |
| D2 绑定 | 声明生成 + runtime `UFunction` 接管形成闭环 | UHT 自动函数表 + 显式 bind 注册并存 | 实现方式不同 | 高 |
| D3 Blueprint | override 与动态 `UFunction` 合成都能走 runtime 桥接 | 编译期 specifier 校验强，动态通用 binding 能力较少 | 部分没有实现 + 部分方式不同 | 高 |
| D6 IDE | `SolutionGenerator + SourceGenerator + Weaver + Publish` 三段式 | 绑定覆盖率诊断强，但本轮未定位到等价 IDE 工程链 | 部分没有实现 | 高 |
| D8 性能 | Mono AOT/JIT + function-hash `InternalCall` + GC handle 映射 | `PrecompiledData + StaticJIT + context pool + GC dump` | 实现方式不同 | 中 |
| D11 部署 | DLL 发布到 `Content/<PublishDirectory>` 并自动 UFS staging | `Script` 根目录 + `Binds.Cache` / `PrecompiledScript*.Cache` | 实现方式不同；自动 staging hook 缺失 | 中 |

---

## 深化分析 (2026-04-08 18:32:38)

本轮不扩散到更多维度，而是继续把旧文档中最值得深挖的三条链补到“可落源码时序”的粒度：`D1` 追 Runtime 真正何时进入可用状态，`D3` 追脚本父类变化后 Blueprint 子类如何被修复，`D6` 追单文件编辑体验如何和全量生成互不踩踏。

## [维度 D1] 启动编排的真实时序

上一轮只看到了模块拆分；这一轮继续往下追，能看到 UnrealCSharp 在 Editor 中并不是“模块加载即初始化 CLR”。真正的激活链是 `FEditorListener::OnPreBeginPIE()` 等待编译线程收敛，再转给 `FEngineListener::SetActive(true)`，之后才由 `FUnrealCSharpCoreModule -> FUnrealCSharpModule -> FCSharpEnvironment` 串起 domain 和各类 registry。换句话说，`Core` 模块更像 activation gate（激活门闩），不是直接塞满初始化逻辑的宿主。

相对地，Angelscript 在 `FAngelscriptRuntimeModule::StartupModule()` 内就直接走 `InitializeAngelscript()`，Editor 模式额外挂一个 fallback ticker。两者不是“有没有初始化”的差别，而是“初始化何时发生、是否先等编辑器态收敛”的差别。

```
[D1-Deep] Runtime Activation Timeline

[UnrealCSharp / Editor]
FEditorListener::OnPreBeginPIE
├─ wait until FCSharpCompiler::IsCompiling == false   // PIE 前先等 C# 编译完成
└─ FEngineListener::SetActive(true)
   └─ FUnrealCSharpCoreModule::SetActive(true)
      └─ FUnrealCSharpModule::OnUnrealCSharpCoreModuleActive
         └─ FCSharpEnvironment::Initialize
            ├─ new FDomain(assembly publish paths)
            ├─ new FCSharpBind / registries
            └─ OnCSharpEnvironmentInitialize
               └─ BindClassDefaultObject(all loaded UClass)

Async-loaded UObject/CDO
└─ FCSharpEnvironment::OnAsyncLoadingFlushUpdate       // 等对象脱离 AsyncLoading 后再绑定

[Angelscript / Editor]
FAngelscriptRuntimeModule::StartupModule
├─ InitializeAngelscript()
│  └─ OwnedPrimaryEngine->Initialize()
└─ AddTicker(TickFallbackPrimaryEngine)                // 编辑器期间兜底 tick
```

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:144-161` 与 `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Listener/FEngineListener.cpp:62-78`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp
// 函数: FEditorListener::OnPreBeginPIE
// 位置: Editor 进入 PIE 前先等待 C# 编译线程结束
// ============================================================================
while (FCSharpCompiler::Get().IsCompiling())
{
    FThreadHeartBeat::Get().HeartBeat();
    FPlatformProcess::SleepNoStats(0.0001f);
    FTSTicker::GetCoreTicker().Tick(FApp::GetDeltaTime());
    FThreadManager::Get().Tick();
    FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
}

FEngineListener::OnPreBeginPIE(bIsSimulating); // ★ 只有编译收敛后才允许真正激活 Runtime
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Listener/FEngineListener.cpp
// 函数: FEngineListener::SetActive
// 位置: Core 模块激活门闩
// ============================================================================
if (InbIsActive)
{
    if (const auto UnrealCSharpSetting = FUnrealCSharpFunctionLibrary::GetMutableDefaultSafe<
        UUnrealCSharpSetting>())
    {
        if (UnrealCSharpSetting->IsEnableImmediatelyActive())
        {
            FUnrealCSharpCoreModule::Get().SetActive(true); // ★ 真正广播 Core active
        }
    }
}
else
{
    FUnrealCSharpCoreModule::Get().SetActive(false);
}
```

关键源码 [2] `Reference/UnrealCSharp/Source/UnrealCSharp/Private/UnrealCSharp.cpp:12-45` 与 `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp:54-133, 295-387`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/UnrealCSharp.cpp
// 函数: FUnrealCSharpModule::OnUnrealCSharpCoreModuleActive
// 位置: Core active 后 Runtime 才继续广播自己的 active
// ============================================================================
void FUnrealCSharpModule::OnUnrealCSharpCoreModuleActive()
{
#if !WITH_EDITOR
    FDynamicGenerator::Generator(); // ★ 非 Editor 先补一轮 dynamic 生成
#endif

    FUnrealCSharpModuleDelegates::OnUnrealCSharpModuleActive.Broadcast();
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp
// 函数: FCSharpEnvironment::Initialize / OnAsyncLoadingFlushUpdate
// 位置: C# 环境真正落地，并把异步对象的绑定延后到安全时机
// ============================================================================
Domain = new FDomain({
    "",
    FUnrealCSharpFunctionLibrary::GetFullAssemblyPublishPath()
});

DynamicRegistry = new FDynamicRegistry();
CSharpBind = new FCSharpBind();
ClassRegistry = new FClassRegistry();
ReferenceRegistry = new FReferenceRegistry();
ObjectRegistry = new FObjectRegistry();
StructRegistry = new FStructRegistry();
ContainerRegistry = new FContainerRegistry();
DelegateRegistry = new FDelegateRegistry();
MultiRegistry = new FMultiRegistry();
StringRegistry = new FStringRegistry();
BindingRegistry = new FBindingRegistry();

FUnrealCSharpModuleDelegates::OnCSharpEnvironmentInitialize.Broadcast(); // ★ 这里才开始全量 CDO 绑定
...
for (const auto& PendingBindObject : PendingBindObjects)
{
    if (PendingBindObject->HasAnyFlags(EObjectFlags::RF_ClassDefaultObject))
    {
        FCSharpBind::BindClassDefaultObject(PendingBindObject); // ★ CDO 单独走 class-default-object 路径
    }
    else
    {
        Bind<true>(PendingBindObject);
    }
}
```

关键源码 [3] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp:13-24, 138-165, 186-199`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp
// 函数: FAngelscriptRuntimeModule::StartupModule / InitializeAngelscript / TickFallbackPrimaryEngine
// 位置: Angelscript 在 Editor/Commandlet 启动期直接拉起 primary engine
// ============================================================================
if (GIsEditor || IsRunningCommandlet())
{
    InitializeAngelscript(); // ★ StartupModule 内直接初始化
}

if (GIsEditor)
{
    FallbackTickHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateRaw(this, &FAngelscriptRuntimeModule::TickFallbackPrimaryEngine));
}
...
OwnedPrimaryEngine = MakeUnique<FAngelscriptEngine>();
FAngelscriptEngineContextStack::Push(OwnedPrimaryEngine.Get());
OwnedPrimaryEngine->Initialize(); // ★ 主 engine 在启动期就进入可用态
```

### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| Editor 激活时机 | UnrealCSharp 先等 `FCSharpCompiler` 收敛，再通过 `SetActive(true)` 触发 Runtime；Angelscript 在 `StartupModule()` 里直接初始化 primary engine | 实现方式不同 |
| 半初始化对象处理 | UnrealCSharp 把 async-loaded UObject/CDO 延后到 `OnAsyncLoadingFlushUpdate()` 再绑定；本轮未在 Angelscript 启动链上看到同层级“延后绑定”门闩 | 实现方式不同 |
| Editor 常驻行为 | Angelscript 明确保留 `TickFallbackPrimaryEngine()`；UnrealCSharp 更像“按需激活、按需绑定”的保守时序 | 实现方式不同 |

这部分不能写成“UnrealCSharp 更先进”或“Angelscript 更简单”。源码能证实的是：UnrealCSharp 选择了更保守的 staged activation（分段激活），以降低 PIE/异步装载期间的半初始化对象进入桥接层的概率；Angelscript 选择了更直接的 eager engine（立即初始化主引擎），以换取编辑器启动后立刻可用的脚本环境。

## [维度 D3] Blueprint 子类修复策略

上一轮已经定位到 UnrealCSharp 会把命中的 `UFunction` 改写为 `UCSharpFunction::execCallCSharp`。这一轮继续向后追，新增看到它不是只改函数入口，还会在 dynamic class 重建后立刻修复所有 Blueprint 子类：替换实例、切换 `ParentClass`、刷新节点、修正仍指向旧类的 pin，并立即 `CompileBlueprint()`。这说明它把“脚本父类变化影响 Blueprint 子类”当成生成链的一部分处理。

Angelscript 在这件事上并不是“完全没有处理”。`AngelscriptClassGenerator.cpp` 同样会给旧类打 `CLASS_NewerVersionExists`，并把 `ReplacedClass->NewerVersion` 指向新类；但与 UnrealCSharp 不同的是，受影响 Blueprint 的识别与解释被拆到了 `BlueprintImpactScanner` 里，按 `ScriptParentClass / NodeDependency / PinType / DelegateSignature / VariableType / ReferencedAsset` 六类原因做显式分析。也就是说，Angelscript 目前更偏“先找出谁会受影响，再决定怎么处理”，而 UnrealCSharp 更偏“生成期直接把子蓝图修好”。

```
[D3-Deep] Blueprint Child Repair After Script/Class Change

[UnrealCSharp]
DynamicClassGenerator
├─ create NewClass / NewBPGC
├─ if OldClass exists -> ReInstance(OldClass, NewClass)
│  ├─ ReplaceInstancesOfClass
│  ├─ collect child UBlueprintGeneratedClass
│  ├─ Blueprint->ParentClass = InNewClass
│  ├─ RefreshAllNodes()
│  ├─ patch pins still pointing to InOldClass
│  └─ CompileBlueprint(SkipGC | SkipSave)
└─ DynamicBlueprintExtension
   └─ CompilerContext->bIsFullCompile = false

[Angelscript]
Class reload
├─ ReplacedClass->ClassFlags |= CLASS_NewerVersionExists
├─ ReplacedClass->NewerVersion = NewClass
└─ BlueprintImpact::AnalyzeLoadedBlueprint
   ├─ parent class chain
   ├─ node external dependencies
   ├─ pin / variable types
   ├─ delegate signatures
   └─ referenced assets
```

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicClassGenerator.cpp:430-520`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicClassGenerator.cpp
// 函数: FDynamicClassGenerator::ReInstance
// 位置: dynamic class 更新后把 Blueprint 子类一并修复
// ============================================================================
InOldClass->ClassFlags |= CLASS_NewerVersionExists;
...
FBlueprintCompileReinstancer::ReplaceInstancesOfClass(InOldClass, InNewClass, ReplaceInstancesOfClassParameters);
...
Blueprint->ParentClass = InNewClass; // ★ 子蓝图父类被直接切到新类

if (FDynamicGenerator::IsFullGenerator())
{
    FBlueprintEditorUtils::RefreshAllNodes(Blueprint);
    ...
    if (Pin->PinType.PinSubCategoryObject == InOldClass)
    {
        Pin->PinType.PinSubCategoryObject = InNewClass; // ★ 把仍指向旧类的 pin 改到新类
    }
}
else
{
    FBlueprintEditorUtils::RefreshAllNodes(Blueprint);
}

FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
FKismetEditorUtilities::CompileBlueprint(Blueprint, BlueprintCompileOptions); // ★ 修复后立即重编译
```

关键源码 [2] `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/DynamicBlueprintExtension.cpp:7-10`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/DynamicBlueprintExtension.cpp
// 函数: UDynamicBlueprintExtension::HandleGenerateFunctionGraphs
// 位置: 动态蓝图扩展主动关闭 full compile
// ============================================================================
void UDynamicBlueprintExtension::HandleGenerateFunctionGraphs(FKismetCompilerContext* CompilerContext)
{
    CompilerContext->bIsFullCompile = false; // ★ 避免每次动态修复都落到完整 Blueprint 编译路径
}
```

关键源码 [3] `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2573-2579, 3695-3700`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 函数: FAngelscriptClassGenerator::CreateFullReloadClass
// 位置: Angelscript 也维护类版本链，但热更数据留给后续流程消费
// ============================================================================
UASClass* ReplacedClass = FindObject<UASClass>(FAngelscriptEngine::GetPackage(), *UnrealName);
if (ReplacedClass)
{
    ReplacedClass->Rename(*OldClassName, nullptr, REN_DontCreateRedirectors);
    ReplacedClass->ClassFlags |= CLASS_NewerVersionExists; // ★ 旧类被显式标记为过期版本
}
...
if (ReplacedClass != nullptr)
{
    bReinstancingAny = true;
    ReplacedClass->NewerVersion = NewClass; // ★ 新旧版本关系被保留下来
}
```

关键源码 [4] `Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp:150-245, 278-309`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp
// 函数: AnalyzeLoadedBlueprint / ScanBlueprintAssets
// 位置: 把“哪些 Blueprint 会被脚本变更影响”拆成显式扫描能力
// ============================================================================
if (UClass* ParentClass = Blueprint.ParentClass)
{
    for (UClass* ImpactedClass : Symbols.Classes)
    {
        if (ImpactedClass != nullptr && ParentClass->IsChildOf(ImpactedClass))
        {
            AddUniqueReason(OutReasons, EBlueprintImpactReason::ScriptParentClass);
            break;
        }
    }
}

if (Node->HasExternalDependencies(&Dependencies))
{
    ...
    AddUniqueReason(OutReasons, EBlueprintImpactReason::NodeDependency);
}

if (MatchesPinType(Pin->PinType, Symbols))
{
    AddUniqueReason(OutReasons, EBlueprintImpactReason::PinType);
}

if (Symbols.Delegates.Contains(SignatureFunction))
{
    AddUniqueReason(OutReasons, EBlueprintImpactReason::DelegateSignature);
}
...
if (AnalyzeLoadedBlueprint(*Blueprint, Result.Symbols, Match.Reasons))
{
    Result.Matches.Add(Match); // ★ 输出的是“受影响蓝图集合 + 影响原因”
}
```

### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 脚本父类变更后的 Blueprint 修复 | UnrealCSharp 在 `ReInstance()` 内直接替换实例、刷新节点、修 pin、重编译子蓝图 | 实现方式不同 |
| 受影响 Blueprint 识别 | Angelscript 明确提供 `BlueprintImpactScanner`，并给出 `ScriptParentClass / NodeDependency / PinType / DelegateSignature / VariableType / ReferencedAsset` 原因 | 不是没有实现，而是修复/诊断阶段分离 |
| 版本链管理 | 两边都有 `CLASS_NewerVersionExists` / 新旧类关系；UnrealCSharp 把后续 Blueprint 修复内联，Angelscript 把受影响面分析外置 | 实现方式不同 |

这条差距不能写成“Angelscript 还不会处理 Blueprint 子类”。更准确的说法是：UnrealCSharp 的自动修复链更短，脚本父类一变更就立即触发 Blueprint 子类 reinstance；Angelscript 的优势在于把影响原因显式化、可扫描、可测试，属于诊断能力更强但自动修复链更分离的方案。

## [维度 D6] 增量编辑体验的闭环与冲突规避

旧文档已经覆盖了 UnrealCSharp 的全量 `Generator()` 流水线；本轮新增的是“增量路径”。它并不是简单复用全量生成，而是单独建了一条 `DirectoryWatcher(.cs) -> 焦点恢复时触发 Compile(FileChanges) -> OnCompile 单文件 CodeAnalysis -> 更新 dynamic file map` 的 loop。与此同时，`FCSharpCompilerRunnable` 还会在 `OnBeginGenerator/OnEndGenerator` 把排队任务和 `FileChanges` 清空，避免全量生成与增量监听相互踩踏。

Angelscript 当前的增量开发体验更强调“输入队列的正确性”。`QueueScriptFileChanges()` 不只处理单个 `.as` 文件增删，还明确覆盖文件夹删除时枚举已加载脚本、文件夹新增时扫描内部 `.as`、以及 rename window（删旧加新）这几类边界情况；更重要的是，它们都有自动化测试。也就是说，UnrealCSharp 的强项在“类到文件”的 IDE 映射与单文件分析，Angelscript 的强项在 watcher correctness（监视器正确性）和回归保护。

```
[D6-Deep] Incremental Edit Loop

[UnrealCSharp]
Editor boot
├─ OnPostEngineInit -> CodeAnalysis + DynamicCodeAnalysisGenerator
├─ DirectoryWatcher watches changed directories
│  └─ collect *.cs changes, ignore Proxy/obj
├─ App activation regained
│  └─ FCSharpCompiler::Compile(FileChanges)
│     ├─ dotnet build game csproj
│     ├─ OnCompile.Broadcast(FileChanges)
│     ├─ FCodeAnalysis::Analysis(file)
│     └─ refresh dynamic override map
├─ Blueprint toolbar
│  ├─ OverrideFile.json / Dynamic map -> class→file lookup
│  ├─ Open File
│  └─ Code Analysis(single file)
└─ Generator begin/end
   └─ clear queued tasks to avoid collision with full generation

[Angelscript]
DirectoryWatcher(.as)
├─ script add/remove -> queue exact file pair
├─ folder remove -> enumerate loaded scripts under folder
├─ folder add -> scan contained *.as
└─ rename window -> retain one remove + one add
   └─ all covered by automation tests
```

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:22-64, 228-248, 306-371`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp
// 函数: FEditorListener::FEditorListener / OnCompile / OnApplicationActivationStateChanged / OnDirectoryChanged
// 位置: UnrealCSharp 的增量编辑闭环
// ============================================================================
OnCompileDelegateHandle = FUnrealCSharpCoreModuleDelegates::OnCompile.AddRaw(
    this, &FEditorListener::OnCompile);
...
DirectoryWatcherModule.Get()->RegisterDirectoryChangedCallback_Handle(
    Directory,
    IDirectoryWatcher::FDirectoryChanged::CreateRaw(this, &FEditorListener::OnDirectoryChanged),
    OnDirectoryChangedDelegateHandle,
    IDirectoryWatcher::WatchOptions::IncludeDirectoryChanges);
...
if (!InFileChangeData.IsEmpty())
{
    for (const auto& File : FileChange)
    {
        if (IFileManager::Get().FileExists(*File))
        {
            FCodeAnalysis::Analysis(File); // ★ 编译后只分析变更文件，不重扫整个工程
        }
    }

    FDynamicGenerator::SetCodeAnalysisDynamicFilesMap();
}
...
if (!bIsPIEPlaying && !bIsGenerating)
{
    FCSharpCompiler::Get().Compile(FileChanges); // ★ 焦点回到编辑器时再触发增量编译
    FileChanges.Reset();
}
...
if (FPaths::GetExtension(FileChange.Filename) == TEXT("cs"))
{
    // ★ 过滤 Proxy/obj，避免生成产物反向触发编译风暴
    if (!bIsIgnored)
    {
        FileChanges.Add(FileChange);
    }
}
```

关键源码 [2] `Reference/UnrealCSharp/Source/Compiler/Private/FCSharpCompilerRunnable.cpp:166-193, 248-299, 328-343`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/Compiler/Private/FCSharpCompilerRunnable.cpp
// 函数: FCSharpCompilerRunnable::Compile / OnBeginGenerator / OnEndGenerator
// 位置: 增量编译线程与全量生成互斥
// ============================================================================
if (UnrealCSharpEditorSetting->EnableCompiled())
{
    bIsCompiling = true;

    Compile(); // ★ 先执行 dotnet build

    const auto Task = FFunctionGraphTask::CreateAndDispatchWhenReady(
        [InFunction, this]()
        {
            if (!GExitPurge)
            {
                FUnrealCSharpCoreModuleDelegates::OnCompile.Broadcast(FileChanges);
                InFunction(); // ★ 编译完成后才把 FileChanges 交给后续生成/分析
            }
        },
        ...
        ENamedThreads::GameThread);
}
...
const auto CompileParam = FString::Printf(TEXT(
    "build \"%s\" --nologo -c %s"
), ...);
FUnrealCSharpFunctionLibrary::SyncProcess(CompileTool, CompileParam, OnComplete);
...
void FCSharpCompilerRunnable::OnBeginGenerator()
{
    bIsGenerating = true;
    Tasks.Empty();
    FileChanges.Empty(); // ★ 全量生成开始时清空增量队列
}

void FCSharpCompilerRunnable::OnEndGenerator()
{
    bIsGenerating = false;
    Tasks.Empty();
    FileChanges.Empty(); // ★ 全量生成结束后从干净状态重新积累变更
}
```

关键源码 [3] `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:34-80, 170-248` 与 `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FCodeAnalysis.cpp:12-36, 54-89`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp
// 函数: Initialize / BuildAction / SetCodeAnalysisOverrideFilesMap
// 位置: Blueprint 编辑器里的 class→file 定位与单文件分析入口
// ============================================================================
SetCodeAnalysisOverrideFilesMap();
FDynamicGenerator::SetCodeAnalysisDynamicFilesMap(); // ★ 启动时就把 class→override file map 建好
...
if (IFileManager::Get().FileExists(*OverrideFile))
{
    FCodeAnalysis::Analysis(OverrideFile); // ★ 直接针对当前 Blueprint 对应的 C# 文件做分析
}
...
CodeAnalysisOverrideFilesMap = FUnrealCSharpFunctionLibrary::LoadFileToString(FString::Printf(TEXT(
    "%s/%s.json"
), *FUnrealCSharpFunctionLibrary::GetCodeAnalysisPath(), *OVERRIDE_FILE));
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FCodeAnalysis.cpp
// 函数: FCodeAnalysis::Analysis
// 位置: 单文件分析会调用独立 CodeAnalysis 程序，而不是重跑全量生成
// ============================================================================
const auto AnalysisParam = FString::Printf(TEXT(
    "true \"%s\" \"%s\""
),
    *FPaths::ConvertRelativePathToFull(FUnrealCSharpFunctionLibrary::GetCodeAnalysisPath()),
    *InFile
);

FUnrealCSharpFunctionLibrary::SyncProcess(Program, AnalysisParam, [](const int32, const FString&)
{
});
```

关键源码 [4] `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp:43-89` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp:75-222`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp
// 函数: QueueScriptFileChanges
// 位置: 脚本增量变更队列的边界条件处理
// ============================================================================
if (AbsolutePath.EndsWith(TEXT(".as")))
{
    if (Change.Action == FFileChangeData::EFileChangeAction::FCA_Removed)
    {
        Engine.FileDeletionsDetectedForReload.AddUnique({ AbsolutePath, RelativePath });
    }
    else
    {
        Engine.FileChangesDetectedForReload.AddUnique({ AbsolutePath, RelativePath });
    }
    continue;
}

if (Change.Action == FFileChangeData::EFileChangeAction::FCA_Removed)
{
    for (const FAngelscriptEngine::FFilenamePair& LoadedScript : EnumerateLoadedScripts(AbsolutePath / TEXT("")))
    {
        Engine.FileDeletionsDetectedForReload.AddUnique(LoadedScript); // ★ 文件夹删除时回溯已加载脚本
    }
}
else if (Change.Action == FFileChangeData::EFileChangeAction::FCA_Added && FileManager.DirectoryExists(*AbsolutePath))
{
    FAngelscriptEngine::FindScriptFiles(FileManager, RelativePath, AbsolutePath, TEXT("*.as"), ContainedScriptFiles, false, false);
    // ★ 文件夹新增时主动扫描内部 .as 文件
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp
// 位置: watcher 队列行为有自动化回归保护
// ============================================================================
TestEqual(TEXT("DirectoryWatcher.Queue.ScriptAddAndRemove should queue one added script"), Engine->FileChangesDetectedForReload.Num(), 1);
TestEqual(TEXT("DirectoryWatcher.Queue.ScriptAddAndRemove should queue one removed script"), Engine->FileDeletionsDetectedForReload.Num(), 1);
...
TestEqual(TEXT("DirectoryWatcher.Queue.FolderAddScansContainedScripts should queue the two script files in the new folder"), Engine->FileChangesDetectedForReload.Num(), 2);
...
TestEqual(TEXT("DirectoryWatcher.Queue.FolderRemoveUsesLoadedScriptEnumerator should queue two removed scripts from the enumerator"), Engine->FileDeletionsDetectedForReload.Num(), 2);
...
TestEqual(TEXT("DirectoryWatcher.Queue.RenameWindowTracksRemoveAndAdd should queue one removed script"), Engine->FileDeletionsDetectedForReload.Num(), 1);
TestEqual(TEXT("DirectoryWatcher.Queue.RenameWindowTracksRemoveAndAdd should queue one added script"), Engine->FileChangesDetectedForReload.Num(), 1);
```

### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 增量入口 | UnrealCSharp 存在独立的 `DirectoryWatcher(.cs) -> Compile(FileChanges) -> FCodeAnalysis::Analysis(file)` 路径，不只是全量 `Generator()` | 不是没有实现 |
| IDE 映射 | UnrealCSharp 已建立 `Blueprint class -> override file` 的显式映射，并在 Blueprint 工具栏提供单文件打开/分析动作 | 实现方式不同 |
| watcher 正确性证据 | Angelscript 的脚本队列对 add/remove/folder/rename 都有自动化测试；本轮未定位到 UnrealCSharp 对等的 watcher 回归测试 | 实现质量差异 |
| 冲突规避 | UnrealCSharp 在 `OnBeginGenerator/OnEndGenerator` 明确清空增量队列，避免全量生成和增量编译交叉污染；Angelscript 本轮更突出的是输入队列正确性而非生成互斥 | 实现方式不同 |

这部分最值得 Angelscript 吸收的不是“照搬 C# 工程生成”，而是两点更窄的能力：一是把 `class -> source file` 的 IDE 映射做成显式可复用数据，而不是只有路径队列；二是在已有 watcher 测试优势之外，再补“全量重生成 / 增量监听并发时不会互相污染状态”的源码证据与测试覆盖。

---

## 深化分析 (2026-04-08 18:42:43)

### [维度 D3] Blueprint 事件调用边界：callee patch vs caller wrapper

前文已经覆盖了 override 合法性和 Blueprint 子类修复；这一轮继续下钻“事件真正是怎么被调用的”。源码显示，UnrealCSharp 选择改写被调 `UFunction` 本身：先把动态函数标成 `FUNC_Event / FUNC_BlueprintEvent`，再在 `FCSharpBind::Bind()` 里把命中的 `UFunction::NativeFunc` 改成 `UCSharpFunction::execCallCSharp`，最后由 `FCSharpFunctionDescriptor::CallCSharp()` 手动拆 `FFrame`、组 `MonoArray`、回填返回值和 `out` 参数。

Angelscript 走的是另一条边界。`FAngelscriptPreprocessor::GenerateBlueprintEventWrapper()` 会在脚本侧生成最终 wrapper，把参数逐个送进 `__Evt_PushArgument*`，再通过 `__Evt_Execute(this, Name)` 或 `BindMethodDirect(... CallEventWithSignature ...)` 回到 `ProcessEvent()` / 反射调用。也就是说，UnrealCSharp 更像 patch callee，Angelscript 更像生成 caller wrapper 并复用 UE 事件派发。

这里需要顺手校正术语：从源码看，UnrealCSharp 在 Blueprint 事件这条链上主要使用的是 Mono `InternalCall + Runtime_Invoke_Array`，而不是狭义的外部 DLL `P/Invoke`。但从“托管边界桥接”这个层面，它仍然属于 managed/native bridge。

```
[D3-Deep] Blueprint Event Dispatch Shape

[UnrealCSharp]
C# attributes
├─ FDynamicGeneratorCore::SetFlags            // 先把函数标成 FUNC_Event/FUNC_BlueprintEvent
├─ FCSharpBind::Bind                          // 命中 override 后直接改写 NativeFunc
│  ├─ patch OriginalFunction
│  └─ or duplicate NewFunction in derived class
└─ FCSharpFunctionDescriptor::CallCSharp      // 从 FFrame 解包后调用 Runtime_Invoke_Array

[Angelscript]
Script UFUNCTION specifiers
├─ Preprocessor wrapper generation            // 生成 caller wrapper，并追加 _Implementation
├─ __Evt_PushArgument*                        // 逐参数压入 FScriptCall
├─ __Evt_Execute / BindMethodDirect           // 通过直接注册入口触发
└─ UObject::ProcessEvent                      // 保持 UE 事件派发语义
```

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGeneratorCore.cpp:515-558`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGeneratorCore.cpp
// 函数: FDynamicGeneratorCore::SetFlags
// 位置: 把 C# attribute 落成 Blueprint 事件标记
// ============================================================================
if (InReflection->HasAttribute(FReflectionRegistry::Get().GetBlueprintNativeEventAttributeClass()))
{
    if (InFunction->FunctionFlags & FUNC_Net)
    {
        UE_LOG(LogUnrealCSharp, Error, TEXT("BlueprintNativeEvent functions cannot be replicated!"));
    }
    ...
    InFunction->FunctionFlags |= FUNC_Event;
    InFunction->FunctionFlags |= FUNC_BlueprintEvent; // ★ 托管 attribute 先转成 UE 事件标志
}

if (InReflection->HasAttribute(FReflectionRegistry::Get().GetBlueprintImplementableEventAttributeClass()))
{
    if (InFunction->FunctionFlags & FUNC_Net)
    {
        UE_LOG(LogUnrealCSharp, Error, TEXT("BlueprintImplementableEvent functions cannot be replicated!"));
    }
    ...
    InFunction->FunctionFlags |= FUNC_Event;
    InFunction->FunctionFlags |= FUNC_BlueprintEvent;
    InFunction->FunctionFlags &= ~FUNC_Native;        // ★ ImplementableEvent 明确去掉 native 标记
}
```

关键源码 [2] `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FCSharpBind.cpp:364-386` 与 `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FCSharpFunctionDescriptor.cpp:82-160`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FCSharpBind.cpp
// 函数: FCSharpBind::Bind
// 位置: override 命中后改写 UFunction 的执行入口
// ============================================================================
FCSharpEnvironment::GetEnvironment().AddFunctionHash<FUnrealFunctionDescriptor>(
    OverrideFunctionHash, InClassDescriptor, OverrideFunction);

OriginalFunction->SetNativeFunc(UCSharpFunction::execCallCSharp);
OriginalFunction->FunctionFlags |= FUNC_Native; // ★ 不是包一层 wrapper，而是直接接管被调函数
...
NewFunction = DuplicateFunction(OriginalFunction, InClass, FunctionName);
...
FCSharpEnvironment::GetEnvironment().AddFunctionHash<FCSharpFunctionDescriptor>(
    FunctionHash, InClassDescriptor, NewFunction, FCSharpFunctionRegister(NewFunction, OriginalFunction));

NewFunction->SetNativeFunc(UCSharpFunction::execCallCSharp);
NewFunction->FunctionFlags |= FUNC_Native;      // ★ 派生类新增函数也直接指向 C# trampoline
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FCSharpFunctionDescriptor.cpp
// 函数: FCSharpFunctionDescriptor::CallCSharp
// 位置: execCallCSharp 之后的 FFrame -> MonoArray 封送
// ============================================================================
const auto CSharpParams = FReflectionRegistry::Get().GetObjectClass()->NewArray(PropertyDescriptors.Num());
...
PropertyDescriptors[Index]->Get<std::false_type>(PropertyAddress, &Object);
FDomain::Array_Set(CSharpParams, Index, static_cast<MonoObject*>(Object)); // ★ 逐参数写入 MonoArray

const auto FoundMonoObject = FunctionRegister.GetOriginalFunctionFlags() & FUNC_Static
                                 ? nullptr
                                 : FCSharpEnvironment::GetEnvironment().GetObject(InContext);

if (const auto ReturnValue = Method->Runtime_Invoke_Array(FoundMonoObject, CSharpParams);
    ReturnValue != nullptr && ReturnPropertyDescriptor != nullptr)
{
    ...
    ReturnPropertyDescriptor->Set(
        FGarbageCollectionHandle::MonoObject2GarbageCollectionHandle(
            ReturnPropertyDescriptor->GetClass(), ReturnValue),
        RESULT_PARAM); // ★ 非 primitive 返回值再转回 handle
}

for (const auto& Index : OutPropertyIndexes)
{
    ...
    OutPropertyDescriptor->Set(
        FGarbageCollectionHandle::MonoObject2GarbageCollectionHandle(
            OutPropertyDescriptor->GetClass(),
            FDomain::Array_Get<MonoObject*>(CSharpParams, Index)),
        OutParams->PropAddr); // ★ out/ref 参数也要回填到 UE 栈
}
```

关键源码 [3] `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:1499-1529,1959-2023`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp
// 函数: 处理 BlueprintEvent / GenerateBlueprintEventWrapper
// 位置: 在脚本侧生成事件 wrapper，而不是改写被调 UFunction
// ============================================================================
else if (Spec.Name == PP_NAME_BlueprintEvent)
{
    ...
    FunctionDesc->bBlueprintEvent = true;
    FunctionDesc->bCanOverrideEvent = true;

    if (!bAlreadyHasWrapper)
    {
        GenerateBlueprintEventWrapper(File, Chunk, Macro, FunctionDesc); // ★ 预处理阶段直接生成 wrapper
        FunctionDesc->ScriptFunctionName += TEXT("_Implementation");
    }
}
...
Code += FString::Printf(TEXT(" %s __ReturnValue%s; __Evt_PushArgumentRef%s(__ReturnValue);"),
    *ReturnType, *GetReturnInit(ReturnType), *GetPushArgumentSuffix(ReturnType));
...
Code += FString::Printf(TEXT(" __Evt_Execute(this, %s);"), *GenerateStaticName(File, FunctionDesc->FunctionName));
// ★ 生成出来的脚本函数最终回到 __Evt_Execute，而不是让 UFunction::NativeFunc 改指向脚本 VM
```

关键源码 [4] `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp:270-282,468-470,520-530,629-632`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp
// 位置: caller wrapper 的最终执行点与直接注册入口
// ============================================================================
SCRIPTCALL_INLINE void ExecuteEvent(UObject* Object, FName EventName)
{
    UFunction* Function = Object->FindFunctionChecked(EventName);
    ...
    Object->ProcessEvent(Function, &ArgumentBuffer[0]); // ★ 最终仍走 UE 自己的事件派发
}
...
FAngelscriptBinds::BindGlobalFunction("void __Evt_Execute(const UObject Object, const FName& Name)", [](UObject* Object, const FName& Name)
{
    CurrentCall().ExecuteEvent(Object, Name);          // ★ wrapper 收集好的参数在这里统一发射
});
...
void CallEventWithSignature(asIScriptGeneric* InGeneric)
{
    ...
    InvokeReflectiveUFunctionFromGenericCall(Generic, static_cast<UObject*>(Generic->GetObject()), Sig->UnrealFunction);
}
...
int32 FunctionId = FAngelscriptBinds::BindMethodDirect(InType->GetAngelscriptTypeName(),
    Signature.Declaration, asFUNCTION(CallEventWithSignature), asCALL_GENERIC, ...);
// ★ Angelscript 的 Blueprint 事件入口被直接注册进脚本 VM，而不是 patch UE 原函数入口
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 事件调用边界 | UnrealCSharp 直接改写 `UFunction::NativeFunc` 到 `execCallCSharp`，属于 callee patch | 实现方式不同 |
| 参数封送位置 | UnrealCSharp 在 `FCSharpFunctionDescriptor::CallCSharp()` 内手动走 `FFrame -> MonoArray -> 返回值/out 回填` | 实现方式不同 |
| 调用方形态 | Angelscript 在预处理阶段生成 wrapper，运行时通过 `__Evt_Execute` / `BindMethodDirect` 保持 `ProcessEvent` 语义 | 实现方式不同 |
| Blueprint 事件约束落点 | UnrealCSharp 在 dynamic function flag 设置时拒绝 `Net/Private` 非法组合；Angelscript 在 preprocessor 期拒绝 `BlueprintEvent`/`BlueprintOverride` 冲突 | 实现方式不同 |

这条差距不能写成“Angelscript 没有 Blueprint override”或“UnrealCSharp 只有反射兜底”。更准确的说法是：UnrealCSharp 把 override 的核心成本压到被调函数 patch 和一次栈封送；Angelscript 把成本压到 caller wrapper 生成和直接注册表上，尽量保留 UE 自身 `ProcessEvent()` 语义。

### [维度 D8] GC 与对象所有权：handle 注入 vs schema 发射

上一轮 D8 主要看的是 `JIT/AOT + StaticJIT + context pool`。这一轮补的是更底层的对象生命周期模型。UnrealCSharp 不只是“有 `FGarbageCollectionHandle` 映射”这么简单；它还用 `UnrealTypeWeaver` 在 IL 层给类型补 `GarbageCollectionHandle` 属性，再由 `FClassReflection::NewGCHandle/NewWeakRefGCHandle()` 在构造包装对象时把 Mono GCHandle 回写进这个属性。这样一来，managed wrapper 自己就带着稳定的 native identity token。

Angelscript 则把 GC 信息前移到类型描述器。`FAngelscriptType::EmitReferenceInfo()` / `NeverRequiresGC()` 把“这个类型是否需要 GC、怎样遍历子成员”作为 type-level contract 固化下来；`Bind_UStruct.cpp`、`Bind_TArray.cpp`、`Bind_TSet.cpp` 会直接往 `UE::GC::FSchemaBuilder` 发射 `Reference` / `ReferenceArray` / `StructArray` / `StructSet`。换句话说，UnrealCSharp 依赖句柄槽与 handle table，Angelscript 依赖 UE GC schema 对脚本暴露布局的显式理解。

这也修正了前文 D8 的一个潜台词：UnrealCSharp 并不是没有性能诊断。`FMonoProfiler::Register()` 允许在 `-trace=CSharp` 时把 Mono 方法 enter/leave 直接写进 UE trace；只是它的诊断形态偏 runtime trace，而不是像 Angelscript `StateDump` 那样偏离线导出。

```
[D8-Deep] Ownership and GC Model

[UnrealCSharp]
Managed type build
├─ UnrealTypeWeaver injects GarbageCollectionHandle   // 每个包装类型有稳定 handle 槽
├─ FClassReflection::NewGCHandle                      // 创建 Mono handle 并回填属性
├─ FCSharpFunctionDescriptor                          // 参数/返回值在 handle 与 MonoObject 间转换
└─ FMonoProfiler (-trace=CSharp)                      // 托管方法 enter/leave 写入 UE trace

[Angelscript]
Type descriptor
├─ HasReferences / NeverRequiresGC                    // 类型级声明 GC 需求
├─ EmitReferenceInfo on UObject/UStruct               // 直接发射 UE GC schema
├─ EmitReferenceInfo on TArray/TSet/TMap              // 容器按 inner layout 生成 schema
└─ VM state dump                                      // 运行时统计可导出
```

关键源码 [1] `Reference/UnrealCSharp/Script/Weavers/UnrealTypeWeaver.cs:183-236`

```csharp
// ============================================================================
// 文件: Reference/UnrealCSharp/Script/Weavers/UnrealTypeWeaver.cs
// 函数: ProcessStructGarbageCollectionHandle
// 位置: 在 IL 层给类型补 GarbageCollectionHandle 属性
// ============================================================================
if (Type.Properties.Any(Property => Property.Name == "GarbageCollectionHandle"))
{
    return;
}

var garbageCollectionHandle = new PropertyDefinition("GarbageCollectionHandle", PropertyAttributes.None,
    ModuleDefinition.TypeSystem.IntPtr);

var garbageCollectionHandleBackingField = new FieldDefinition("<GarbageCollectionHandle>k__BackingField",
    FieldAttributes.Private, ModuleDefinition.TypeSystem.IntPtr);

Type.Fields.Add(garbageCollectionHandleBackingField);
...
instructions.Add(Instruction.Create(OpCodes.Ldfld, garbageCollectionHandleBackingField));
...
instructions.Add(Instruction.Create(OpCodes.Stfld, garbageCollectionHandleBackingField));
// ★ 句柄槽不是运行时字典里“额外记一下”，而是直接进入包装类型布局
```

关键源码 [2] `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Reflection/FClassReflection.cpp:596-621` 与 `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoProfiler.cpp:7-77`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Reflection/FClassReflection.cpp
// 函数: FClassReflection::NewGCHandle / NewWeakRefGCHandle
// 位置: 创建 Mono handle 后回写到包装对象
// ============================================================================
auto GarbageCollectionHandle = FMonoDomain::GCHandle_New_V2(InMonoObject, bPinned);
...
if (const auto FoundProperty = GetProperty(PROPERTY_GARBAGE_COLLECTION_HANDLE))
{
    FoundProperty->SetValue(InMonoObject, InParams, nullptr); // ★ handle 立刻写回对象属性
}
...
auto GarbageCollectionHandle = FMonoDomain::GCHandle_New_WeakRef_V2(InMonoObject, bTrackResurrection);
...
FoundProperty->SetValue(InMonoObject, InParams, nullptr);     // ★ 弱引用句柄同样回写
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoProfiler.cpp
// 函数: FMonoProfiler::Register / Method_Enter / Method_Leave
// 位置: UnrealCSharp 的运行时 trace 诊断入口
// ============================================================================
if (Channels.ToLower().Contains(TEXT("CSharp")))
{
    if (ProfilerHandle = mono_profiler_create(nullptr); ProfilerHandle != nullptr)
    {
        mono_profiler_set_method_enter_callback(ProfilerHandle, Method_Enter);
        mono_profiler_set_method_leave_callback(ProfilerHandle, Method_Leave);
        mono_profiler_set_method_exception_leave_callback(ProfilerHandle, Method_Exception_Leave);
        mono_profiler_set_method_tail_call_callback(ProfilerHandle, Method_Tail_Call);
        mono_profiler_set_call_instrumentation_filter_callback(ProfilerHandle, Call_Instrumentation_Filter);
    }
}
...
const auto EventName = FString::Printf(TEXT("[C#] %s.%s.%s"), *MethodNamespace, *ClassName, *MethodName);
FCpuProfilerTrace::OutputBeginDynamicEvent(*EventName);
...
FCpuProfilerTrace::OutputEndEvent(); // ★ 每个托管方法都能进入 UE trace，而不是只看汇总 dump
```

关键源码 [3] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:170-182,448-451` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UStruct.cpp:149-167`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h
// 位置: GC 需求被建模为 type-level contract
// ============================================================================
struct FGCReferenceParams
{
    TArray<FName, TInlineAllocator<2>> Names;
    class UASClass* Class = nullptr;
    SIZE_T AtOffset;
    UE::GC::FSchemaBuilder* Schema;
    UE::GC::FPropertyStack* DebugPath;
};

virtual void EmitReferenceInfo(const FAngelscriptTypeUsage& Usage, FGCReferenceParams& Params) const {}
virtual bool NeverRequiresGC(const FAngelscriptTypeUsage& Usage) const { return false; }
...
FORCEINLINE void EmitReferenceInfo(FAngelscriptType::FGCReferenceParams& Params) const { Type->EmitReferenceInfo(*this, Params); }
FORCEINLINE bool NeverRequiresGC() const { return Type.IsValid() && Type->NeverRequiresGC(*this); }
// ★ 这里没有与 Mono handle 对等的对象字段，GC 信息由类型系统统一决定
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UStruct.cpp
// 函数: EmitReferenceInfo
// 位置: struct 类型直接向 UE GC schema 发射成员引用信息
// ============================================================================
if (UsedStruct->StructFlags & STRUCT_AddStructReferencedObjects)
{
    UE::GC::StructAROFn StructARO = UsedStruct->GetCppStructOps()->AddStructReferencedObjects();
    Params.Schema->Add(UE::GC::DeclareMember(Params.Names.Top(), Params.AtOffset, UE::GC::EMemberType::MemberARO, StructARO));
}

TArray<const FStructProperty*> EncounteredStructProps;
for (FProperty* Property = UsedStruct->PropertyLink; Property; Property = Property->PropertyLinkNext)
{
    Property->EmitReferenceInfo(*Params.Schema, Params.AtOffset, EncounteredStructProps, *Params.DebugPath);
}
// ★ struct 内部怎么被 GC 遍历，不靠对象句柄表，而靠 schema 和 property walker
```

关键源码 [4] `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:185-190` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp:66-84`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp
// 位置: UObject 指针引用直接作为 UE GC Reference 成员发射
// ============================================================================
bool HasReferences(const FAngelscriptTypeUsage& Usage) const override { return true; }
bool IsObjectPointer() const override { return true; }
void EmitReferenceInfo(const FAngelscriptTypeUsage& Usage, FGCReferenceParams& Params) const override
{
    Params.Schema->Add(UE::GC::DeclareMember(Params.Names.Top(), Params.AtOffset, UE::GC::EMemberType::Reference));
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp
// 函数: FAngelscriptArrayType::EmitReferenceInfo
// 位置: 容器 GC 不是特殊 case，直接用 inner schema 递归展开
// ============================================================================
int32 ElementSize = Usage.SubTypes[0].Type->GetValueSize(Usage.SubTypes[0]);
UE::GC::FSchemaBuilder InnerSchema(ElementSize);

if (Usage.SubTypes[0].Type->IsObjectPointer())
{
    Params.Schema->Add(UE::GC::DeclareMember(Params.Names.Top(), Params.AtOffset, UE::GC::EMemberType::ReferenceArray, InnerSchema.Build()));
}
else
{
    FGCReferenceParams InnerParams = Params;
    InnerParams.Schema = &InnerSchema;
    InnerParams.AtOffset = 0;
    Usage.SubTypes[0].EmitReferenceInfo(InnerParams);

    Params.Schema->Add(UE::GC::DeclareMember(Params.Names.Top(), Params.AtOffset, UE::GC::EMemberType::StructArray, InnerSchema.Build()));
}
// ★ 容器元素是对象还是 struct，都会在 schema 层被明确区分
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 对象身份令牌 | UnrealCSharp 用 Weaver 注入 `GarbageCollectionHandle` 属性，并在创建 `MonoGCHandle` 时回写 | 实现方式不同 |
| GC 图谱表达方式 | Angelscript 把 `EmitReferenceInfo/NeverRequiresGC` 固化到类型系统，再由 `Bind_UStruct/TArray/...` 递归生成 UE GC schema | 实现方式不同 |
| 容器/struct GC 可见性 | 本轮在 Angelscript 看到 `Reference / ReferenceArray / StructArray / StructSet` 的显式发射；本轮未定位到 UnrealCSharp 对等的 UE GC schema 发射入口 | 实现方式不同 |
| 性能诊断形态 | UnrealCSharp 已有 `-trace=CSharp` 的 Mono method-level trace；Angelscript 已有 VM dump / GC stats 导出 | 实现方式不同 |

这条差距不能简单落成“谁的 GC 更先进”。源码能证实的是：UnrealCSharp 追求的是跨 managed/native 边界的稳定 identity 和 method trace；Angelscript 追求的是让 UE GC 直接理解脚本暴露出来的 native 布局。两者解决的是同一问题的不同切面。

### [维度 D11] 部署根目录与发布态裁剪：staged assembly set vs project-root cache

上一轮 D11 已经说明了“DLL 发布到 `Content/<PublishDirectory>`”与“脚本/缓存落在 `Script` 根目录”这层差异；这一轮继续下钻“这些根目录在运行时到底怎么被消费”。UnrealCSharp 的关键点是：编辑器启动时就执行 `UpdatePackagingSettings()`，运行时 domain 又拿 `GetFullAssemblyPublishPath()` 得到 `UE/Game/CustomProjects` 三类程序集清单；`UAssemblyLoader::Load()` 则只按 `ProjectContentDir()/PublishDirectory` 和 `.NET runtime dir` 两类路径找 DLL，再把字节喂给 `mono_assembly_load_from_full()`。这意味着它的发布单元是显式 assembly set，而不是自由散落的脚本文件。

Angelscript 的关键点更微妙。初始化时先 `DiscoverScriptRoots(/*bOnlyProjectRoot =*/ true)`，所以 `AllRootPaths[0]` 永远是项目自己的 `Script` 根；`Binds.Cache`、`PrecompiledScript*.Cache` 的读写也都锚定这个根。只有在本次运行没有直接使用 fully precompiled data 时，`InitialCompile()` 才会扩展到 `MakeAllScriptRoots()`，把插件 `Script` 根也纳入扫描。也就是说，Angelscript 不是没有多根脚本发现，而是发布态锚点明显更 project-root-centric（项目根中心化）。

另一个之前没写清的点是发布态裁剪。`Bind_BlueprintType.cpp:900-920` 会在绑定暴露前检查 `PKG_EditorOnly | PKG_UncookedOnly`，并读取 `UAngelscriptSettings::AdditionalEditorOnlyScriptPackageNames`。这说明 Angelscript 的部署逻辑不只体现在“把什么文件带进包”，还体现在“哪些 script package 在非编辑器运行时根本不应暴露”。

```
[D11-Deep] Deployment Root Selection

[UnrealCSharp]
Editor startup
├─ UpdatePackagingSettings()                  // 自动把 PublishDirectory 加入 UFS
├─ GetFullAssemblyPublishPath()               // UE/Game/CustomProjects DLL 清单
├─ UAssemblyLoader::Load(name)                // 扫 Content/<PublishDirectory> 与 net runtime dir
└─ FMonoDomain::LoadAssembly(bytes)           // 从 staged DLL 字节装入 Mono

[Angelscript]
Engine bootstrap
├─ DiscoverScriptRoots(true)                  // 先只保留 project Script root
├─ GetScriptRootDirectory() -> root[0]        // Binds.Cache / PrecompiledScript*.Cache 都锚定这里
├─ if not fully precompiled
│  └─ MakeAllScriptRoots()                    // 这时才扩展到 plugin Script roots
└─ Bind_BlueprintType::IsEditorOnlyClass      // 发布态过滤 editor-only script package
```

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:102,209-234`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp
// 位置: 编辑器启动时就声明打包 staging 行为
// ============================================================================
UpdatePackagingSettings(); // ★ 不是等用户手工点 Packaging Settings，而是模块启动就修正配置
...
const auto PublishDirectory = FUnrealCSharpFunctionLibrary::GetPublishDirectory();
...
for (const auto& [Path] : ProjectPackagingSettings->DirectoriesToAlwaysStageAsUFS)
{
    if (Path == PublishDirectory)
    {
        bIsExisted = true;
        break;
    }
}

if (!bIsExisted)
{
    ProjectPackagingSettings->DirectoriesToAlwaysStageAsUFS.Add({PublishDirectory});
    ProjectPackagingSettings->TryUpdateDefaultConfigFile(); // ★ 程序集目录自动成为 UFS staging 输入
}
```

关键源码 [2] `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp:1020-1049`、`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/AssemblyLoader.cpp:6-24`、`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp:500-508,560-567`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp
// 位置: 发布态程序集清单与加载搜索路径
// ============================================================================
TArray<FString> FUnrealCSharpFunctionLibrary::GetFullCustomProjectsPublishPath()
{
    ...
    FullCustomProjectsPublishPath.Add(GetFullPublishDirectory() / Name + DLL_SUFFIX);
}

TArray<FString> FUnrealCSharpFunctionLibrary::GetFullAssemblyPublishPath()
{
    return TArrayBuilder<FString>().
           Add(GetFullUEPublishPath()).
           Add(GetFullGamePublishPath()).
           Append(GetFullCustomProjectsPublishPath()).
           Build(); // ★ 运行时明确知道要装哪些 assembly
}

TArray<FString> FUnrealCSharpFunctionLibrary::GetAssemblyPath()
{
    return TArrayBuilder<FString>().
           Add(FPaths::ProjectContentDir() / GetPublishDirectory()).
           Add(FMonoFunctionLibrary::GetNetDirectory()).
           Build(); // ★ 搜索路径只有 staged content + net runtime dir
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/AssemblyLoader.cpp
// 位置: 按名称从 staging 目录加载程序集字节
// ============================================================================
auto AssemblyPaths = FUnrealCSharpFunctionLibrary::GetAssemblyPath();

for (const auto& AssemblyPath : AssemblyPaths)
{
    if (const auto File = FPaths::Combine(AssemblyPath, InAssemblyName) + DLL_SUFFIX;
        IFileManager::Get().FileExists(*File))
    {
        TArray<uint8> Data;
        FFileHelper::LoadFileToArray(Data, *File);
        return Data;
    }
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp
// 位置: 域初始化后立刻按程序集清单装载 DLL
// ============================================================================
if (const auto AssemblyLoader = FUnrealCSharpFunctionLibrary::GetAssemblyLoader())
{
    if (const auto Data = AssemblyLoader->Load(AssemblyName); !Data.IsEmpty())
    {
        MonoAssembly* Assembly = nullptr;
        LoadAssembly(AssemblyName, Data, nullptr, &Assembly); // ★ 用字节流而不是硬编码文件句柄装入 Mono
        return Assembly;
    }
}
...
void FMonoDomain::InitializeAssembly(const TArray<FString>& InAssemblies)
{
#if WITH_EDITOR
    InitializeAssemblyLoadContext();
#endif
    LoadAssembly(InAssemblies);
}
```

关键源码 [3] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:781-794,1326-1363,1430-1470,1517-1535,1582-1587,2061-2069`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: project-root-centric 的脚本根与缓存装载策略
// ============================================================================
FString FAngelscriptEngine::GetScriptRootDirectory()
{
    ...
    // The first root in the list of roots is the game project root.
    return AllRootPaths.IsEmpty() ? TEXT("") : CurrentEngine->AllRootPaths[0]; // ★ root[0] 明确是项目 Script 根
}
...
TArray<FString> FAngelscriptEngine::DiscoverScriptRoots(bool bOnlyProjectRoot) const
{
    FString RootPath = Dependencies.ConvertRelativePathToFull(Dependencies.GetProjectDir() / TEXT("Script"));
    ...
    if (!bOnlyProjectRoot)
    {
        for (const FString& PluginScriptRoot : Dependencies.GetEnabledPluginScriptRoots())
        {
            ...
            DiscoveredRootPaths.Add(ScriptPath);
        }
    }

    DiscoveredRootPaths.Insert(RootPath, 0); // ★ 项目根永远排第一个
    return DiscoveredRootPaths;
}
...
AllRootPaths = DiscoverScriptRoots(/*bOnlyProjectRoot =*/ true); // ★ 初始化先只拿项目根
...
FAngelscriptBindDatabase::Get().Load(GetScriptRootDirectory() / TEXT("Binds.Cache"), bGeneratePrecompiledData);
...
Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript_Development.Cache");
...
if (!IFileManager::Get().FileExists(*Filename))
    Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript.Cache");
...
if (bGeneratePrecompiledData)
{
    FString Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript.Cache");
    PrecompiledData->InitFromActiveScript();
    PrecompiledData->Save(Filename); // ★ 预编译缓存也回写到项目根
}
...
// Make sure we scan all plugins for script roots as well, now that we know we need them.
AllRootPaths = MakeAllScriptRoots(); // ★ 只有不直接走 fully precompiled path 时才扩展到 plugin roots
```

关键源码 [4] `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:900-920` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h:197-201`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp
// 函数: IsEditorOnlyClass
// 位置: 发布态前的 script package 暴露裁剪
// ============================================================================
if (Class->GetOutermost()->HasAnyPackageFlags(PKG_EditorOnly | PKG_UncookedOnly))
{
    bIsEditor = true; // ★ 来自 editor-only / uncooked 包的类直接视为 editor-only
}

if (!bIsEditor && FAngelscriptEngine::Get().ConfigSettings->AdditionalEditorOnlyScriptPackageNames.Contains(Class->GetOutermost()->GetFName()))
{
    bIsEditor = true; // ★ 游戏还可以额外指定 editor-only script package
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h
// 位置: editor-only script package 的配置源
// ============================================================================
/**
 * Script package names (/Script/ModuleName) that should be considered editor-only for the purposes of checking for incorrect usage.
 */
UPROPERTY(Config)
TArray<FName> AdditionalEditorOnlyScriptPackageNames;
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 发布单元 | UnrealCSharp 运行时按 `UE/Game/CustomProjects` 组成显式 assembly set 装载 DLL | 实现方式不同 |
| 根目录锚点 | Angelscript 的 `Binds.Cache` 与 `PrecompiledScript*.Cache` 都固定落在 `AllRootPaths[0]` 的项目 `Script` 根 | 实现方式不同 |
| 插件脚本根参与时机 | Angelscript 只有在未直接走 fully precompiled path 时，才扩展到 plugin `Script` roots 做扫描 | 实现方式不同 |
| 发布态裁剪 | Angelscript 明确用 `PKG_EditorOnly / PKG_UncookedOnly + AdditionalEditorOnlyScriptPackageNames` 做 script package 暴露过滤 | 不是没有实现，而是实现点落在暴露裁剪而非 Packaging Settings |

这条差距也不能写成“Angelscript 没有打包能力”。更准确的说法是：UnrealCSharp 把部署 contract 固化成 staged assembly set；Angelscript 把部署 contract 固化成“项目根缓存 + 条件性插件根扫描 + editor-only 包过滤”。两者都能支撑发布，只是边界落点完全不同。

---

## 深化分析 (2026-04-08 18:54:32)

本轮不再重复前两轮已经写透的启动时序、Blueprint 子类修复和增量监听，而是继续补三条之前还没落到源码 contract 的链路：`D2` 追 override 元数据究竟由谁生成、何时消费，`D6` 追 IDE 支持是“导航能力”还是“编译期约束”，`D1` 追真正拥有整条生成/编译流水线的模块是谁。

### [维度 D2] Override 元数据源头：script-first intent map vs native-first exposure map

前文已经说明 UnrealCSharp 会在 runtime patch `UFunction`。这一轮继续上溯元数据源头，可以看到它并不是在运行时临时扫一遍托管方法，而是先由 `CodeAnalysis.cs` 用 Roslyn 读取用户 `.cs` 文件，把 `[Override]` 类和 `[Override]` 方法写进 `OverrideFunction.json` / `OverrideFile.json`；之后 `FGeneratorCore` 直接把这份 JSON 当成生成输入，`FClassGenerator` 根据命中的 encode name 改写生成出来的方法名，最后 `FCSharpBind` 再据此复制 native `UFunction` 并回填 override hash。也就是说，UnrealCSharp 的 override contract 起点在“脚本作者意图”，不是 UHT。

Angelscript 则是另一条方向相反的链。`AngelscriptFunctionTableCodeGenerator` 从 UHT session 和 `AngelscriptRuntime.Build.cs` 出发，先决定哪些 native module 可以暴露，再按 `BlueprintCallable` 规则为每个 module 生成 `AS_FunctionTable_*.cpp` 分片，同时输出 summary / module CSV / entry CSV。它关心的第一问题不是“脚本作者准备 override 什么”，而是“当前 native 暴露面有哪些、哪些还是 stub”。这也是为什么 Angelscript 在 D2 上诊断报表更强，而 UnrealCSharp 在脚本到绑定的闭环更短。

```
[D2-Deep] Metadata Source Of Truth

[UnrealCSharp]
User .cs files
├─ CodeAnalysis.cs parses syntax tree
│  ├─ class [Override] -> OverrideFile.json
│  └─ method [Override] -> OverrideFunction.json
├─ FGeneratorCore loads OverrideFunctionsMap
├─ FClassGenerator renames generated method to override alias
└─ FCSharpBind duplicates UFunction + patches hash/native func

[Angelscript]
UHT session + AngelscriptRuntime.Build.cs
├─ LoadSupportedModules()                   // 先按 Build.cs 依赖图确定可暴露模块
├─ CollectEntries(BlueprintCallable/Pure)   // 从 native 暴露面收集 entry
├─ BuildShard() -> AS_FunctionTable_*.cpp   // 生成 C++ 注册分片
├─ WriteSummary / ModuleSummary / EntryCsv  // 生成覆盖率与 stub 诊断
└─ FAngelscriptBinds::CallBinds()           // runtime 只消费生成产物
```

关键源码 [1] `Reference/UnrealCSharp/Script/CodeAnalysis/CodeAnalysis.cs:55-83, 184-252, 375-383`

```csharp
// ============================================================================
// 文件: Reference/UnrealCSharp/Script/CodeAnalysis/CodeAnalysis.cs
// 函数: CodeAnalysis(...) / AnalysisOverride / WriteAll
// 位置: override 元数据的真正产出源
// ============================================================================
if (File.Exists(Path.Combine(_outputPathName, OverrideFunctionFileName)))
{
    _overrideFunction = JsonSerializer.Deserialize<Dictionary<string, List<string>>>(
        File.ReadAllText(Path.Combine(_outputPathName, OverrideFunctionFileName)));
}
...
if (File.Exists(Path.Combine(_outputPathName, OverrideFileFileName)))
{
    _overrideFile = JsonSerializer.Deserialize<Dictionary<string, string>>(
        File.ReadAllText(Path.Combine(_outputPathName, OverrideFileFileName)));
}
...
var Pair = _overrideFile.FirstOrDefault(pair => pair.Value == _inputFileName);
if (Pair.Key != null)
{
    _overrideFunction.Remove(Pair.Key);
    _overrideFile.Remove(Pair.Key);
    // ★ 单文件重分析时先清掉旧 class→file / class→override 列表，避免脏映射残留
}
...
if (Attribute.ToString().Equals("Override"))
{
    IsOverride = true;
}
...
_overrideFile[$"{NamespaceDeclaration.Name}.{ClassDeclaration.Identifier}"] = inFile;
// ★ 先记录“哪个 override class 来自哪个文件”
...
Functions.Add(MethodDeclaration.Identifier.ToString());
// ★ 再记录该 class 内具体哪些方法声明了 [Override]
...
File.WriteAllText(Path.Combine(_outputPathName, OverrideFunctionFileName),
    JsonSerializer.Serialize(_overrideFunction, new JsonSerializerOptions { WriteIndented = true }));
File.WriteAllText(Path.Combine(_outputPathName, OverrideFileFileName),
    JsonSerializer.Serialize(_overrideFile, new JsonSerializerOptions { WriteIndented = true }));
// ★ 这两份 JSON 是后续生成器和 Blueprint 工具栏都会消费的 contract
```

关键源码 [2] `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:647-656, 972-977` 与 `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp:284-297, 336-344`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp
// 函数: FGeneratorCore::GetOverrideFunctions / BeginGenerator
// 位置: 生成阶段直接消费 override JSON
// ============================================================================
const auto FoundFunctions = OverrideFunctionsMap.Find(FString::Printf(TEXT(
    "%s.%s"
), *InNameSpace, *InClass));

return FoundFunctions != nullptr ? *FoundFunctions : TArray<FString>{};
...
OverrideFunctionsMap = FUnrealCSharpFunctionLibrary::LoadFileToArray(FString::Printf(TEXT(
    "%s/%s.json"
), *FUnrealCSharpFunctionLibrary::GetCodeAnalysisPath(), *OVERRIDE_FUNCTION));
// ★ 生成器并不自己推断 override，而是直接读取 Roslyn 产物
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp
// 位置: 生成 C# 声明时根据 override 元数据改写方法名
// ============================================================================
if (OverrideFunctions.Contains(EncodeFunctionName))
{
    if (FUnrealCSharpFunctionLibrary::EnableCallOverrideFunction())
    {
        Functions.Emplace(true,
                          FUnrealCSharpFunctionLibrary::GetOverrideFunctionName(FunctionName),
                          FUnrealCSharpFunctionLibrary::GetOverrideFunctionName(EncodeFunctionName),
                          *FunctionIterator);
        // ★ 命中的函数在声明生成期就切到 override alias，而不是留到 runtime 再猜
    }
}
else
{
    Functions.Emplace(false, FunctionName, EncodeFunctionName, *FunctionIterator);
}
```

关键源码 [3] `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FCSharpBind.cpp:331-366`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FCSharpBind.cpp
// 函数: FCSharpBind::Bind
// 位置: runtime 继续消费 override alias，把 hash 与 UFunction 对齐
// ============================================================================
const auto& NewFunctionName = FUnrealCSharpFunctionLibrary::GetOverrideFunctionName(FunctionName);
const auto OverrideFunction = DuplicateFunction(OriginalFunction, InClass, *NewFunctionName);
...
if (const auto FoundField = FoundClass->GetField(FString::Printf(TEXT(
    "__%s"
), *OverrideMethodName)))
{
    FoundField->SetValue(FoundClass, &OverrideFunctionHash);
    // ★ 生成出的 __OverrideMethod 字段会被回填成 override function hash
}
...
OriginalFunction->SetNativeFunc(UCSharpFunction::execCallCSharp);
OriginalFunction->FunctionFlags |= FUNC_Native;
// ★ 最终 runtime 只需按照已经生成好的 alias/hash contract patch native func
```

关键源码 [4] `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:51-77, 81-139, 334-385, 449-515`

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs
// 函数: Generate / LoadSupportedModules / CollectEntries / ShouldGenerate
// 位置: Angelscript 的 D2 元数据起点在 native 暴露面而不是脚本 override 声明
// ============================================================================
AngelscriptSupportedModules supportedModules = LoadSupportedModules(factory);
...
if (!supportedModules.All.Contains(module.ShortName))
{
    continue;
}
// ★ 先按 Build.cs 解析出来的依赖图裁剪模块
...
string outputPath = factory.MakePath($"AS_FunctionTable_{module.ShortName}_{shardIndex:D3}", ".cpp");
factory.CommitOutput(outputPath, BuildShard(module.ShortName, editorOnly, includes, entries, startIndex, entryCount, shardIndex, shardCount));
// ★ 生成产物是 C++ 注册分片，不是脚本类上的 override map
...
WriteGenerationSummary(factory, moduleSummaries, csvEntries, generatedFileCount);
WriteCoverageDiagnostics(moduleSummaries);
// ★ 生成器自带 coverage / stub 诊断
...
if (!AngelscriptFunctionTableExporter.IsBlueprintCallable(function))
{
    return false;
}
...
return !function.FunctionExportFlags.ToString().Contains("CustomThunk", StringComparison.Ordinal);
// ★ 过滤逻辑聚焦“native 函数是否可自动导出”，不是“脚本作者是否打算覆写它”
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 元数据源头 | UnrealCSharp 的 override 元数据先由 Roslyn 扫脚本文件再落盘为 JSON | 实现方式不同 |
| 生成器输入 | UnrealCSharp 的 `FGeneratorCore` 直接吃 `OverrideFunction.json`；Angelscript 的 UHT 工具直接吃 UHT session 与 `Build.cs` | 实现方式不同 |
| 诊断重心 | UnrealCSharp 更强调 `class -> file -> override method` 闭环；Angelscript 更强调 module / entry / stub 覆盖率报表 | 实现质量差异，各自侧重点不同 |
| runtime 负担 | UnrealCSharp 在 runtime 继续消费 alias/hash contract patch `UFunction`；Angelscript runtime 主要执行已生成的 bind 分片 | 实现方式不同 |

这条差距不能写成“Angelscript 还是手写绑定”或“UnrealCSharp 完全自动”。更准确的说法是：UnrealCSharp 的自动化从脚本作者意图出发，Angelscript 的自动化从 native 暴露面出发；前者更像 intent-driven binding，后者更像 coverage-driven binding。

### [维度 D6] IDE contract：generated solution + analyzer diagnostics vs editor navigation

上一轮 D6 已经覆盖了增量监听和 Blueprint 工具栏。这一轮继续下钻 IDE 支撑的“硬约束层”，会发现 UnrealCSharp 的 IDE 支持不仅是打开文件，而是把 `SourceGenerator`、`Weaver`、`Publish` 和 `TargetFramework` 一起固化进生成出来的 C# solution。`Game.props` 直接把 `SourceGenerator.csproj` 以 `Analyzer` 方式挂进工程，再在 `AfterBuildPublish` 后自动 `Publish` 并把 DLL 复制到 `Content/<PublishDirectory>`。与此同时，`UnrealTypeSourceGenerator` 在编译期报错：动态类/结构不是 `partial`、`UClass` 没有基类、文件名与类型名不一致、类型重复，都会在 IDE 编译诊断里直接爆出来。

Angelscript 则不是没有 IDE 支持，而是层级不同。当前源码能直证的是：它在 Editor 里注册 `FSourceCodeNavigation` handler，把 `UASClass/UASFunction/UASStruct` 定位到脚本文件和行号，再直接 `code --goto` 打开 VS Code；工具菜单里还提供 “Open Angelscript workspace (VS Code)”。这意味着 Angelscript 的 IDE 支持重心是 editor-time navigation，而 UnrealCSharp 的重心是 generated-project contract 和 compile-time diagnostics。

```
[D6-Deep] IDE Support Layering

[UnrealCSharp]
FSolutionGenerator
├─ generate Script.sln / UE.csproj / Game.csproj
├─ inject Shared.props / Game.props / TargetFramework
├─ attach SourceGenerator as Analyzer
├─ attach Weaver + AfterBuildPublish
└─ publish DLLs to Content/<PublishDirectory>

Compile in IDE
├─ UnrealTypeSourceGenerator
│  ├─ partial required
│  ├─ UClass base required
│  ├─ file name must match type
│  └─ type must be unique
└─ diagnostics appear before runtime load

[Angelscript]
Editor module
├─ RegisterAngelscriptSourceNavigation
├─ NavigateToClass/Function/Property/Struct
│  └─ code --goto file:line
└─ Tools menu opens project Script workspace in VS Code
```

关键源码 [1] `Reference/UnrealCSharp/Template/Game.props:1-28` 与 `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FSolutionGenerator.cpp:9-25, 32-57, 77-125, 166-242`

```xml
<!-- =========================================================================
文件: Reference/UnrealCSharp/Template/Game.props
位置: 生成后的 Game 工程会直接继承这些 IDE/发布规则
=========================================================================== -->
<ProjectReference Include="..\SourceGenerator\SourceGenerator.csproj" OutputItemType="Analyzer" ReferenceOutputAssembly="false"/>
<ProjectReference Include="..\Weavers\Weavers.csproj" ReferenceOutputAssembly="false" />
<!-- ★ SourceGenerator 不是普通依赖，而是 Analyzer，说明诊断直接进入编译器 -->

<Target Name="AfterBuildPublish" AfterTargets="Build">
    <MSBuild Projects="$(ProjectPath)" Targets="Publish" Properties="Configuration=$(Configuration)" />
</Target>

<Target Name="CopyDllsAfterPublish" AfterTargets="Publish">
    <Copy SourceFiles="@(PublishFiles)" DestinationFolder="$(ScriptOutputPath)" />
</Target>
<!-- ★ IDE Build 之后自动进入 Publish，再把 DLL 复制到脚本发布目录 -->
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FSolutionGenerator.cpp
// 函数: FSolutionGenerator::Generator / ReplaceImport / ReplaceProjectReference / ReplaceOutputPath
// 位置: 解决方案模板不是静态样板，而是按当前工程动态改写
// ============================================================================
CopyTemplate(FUnrealCSharpFunctionLibrary::GetCodeAnalysisProjectPath(), ...);
CopyTemplate(FPaths::Combine(FUnrealCSharpFunctionLibrary::GetSourceGeneratorPath(), SOURCE_GENERATOR_NAME + PROJECT_SUFFIX), ...);
CopyTemplate(FPaths::Combine(FUnrealCSharpFunctionLibrary::GetWeaversPath(), WEAVERS_NAME + PROJECT_SUFFIX), ...);
CopyTemplate(FUnrealCSharpFunctionLibrary::GetUEProjectPath(), TemplatePath / DEFAULT_UE_NAME + PROJECT_SUFFIX, ...);
CopyTemplate(FUnrealCSharpFunctionLibrary::GetGameProjectPath(), TemplatePath / DEFAULT_GAME_NAME + PROJECT_SUFFIX, ...);
CopyTemplate(FPaths::Combine(FUnrealCSharpFunctionLibrary::GetFullScriptDirectory(),
                FUnrealCSharpFunctionLibrary::GetScriptDirectory() + SOLUTION_SUFFIX),
             TemplatePath / SOLUTION_NAME + SOLUTION_SUFFIX, ...);
// ★ 会同时生成 CodeAnalysis / SourceGenerator / Weavers / UE / Game / Solution 六类工程产物
...
OutResult = OutResult.Replace(TEXT("<Import Project=\"\" Condition=\"Exists('')\" />"),
    *FString::Printf(TEXT("<Import Project=\"%s%s\" Condition=\"Exists('%s%s')\" />"), ...));
...
OutResult = OutResult.Replace(TEXT("<ProjectReference Include=\"\" />"),
    *FString::Printf(TEXT("<ProjectReference Include=\"..\\%s\\%s%s\" />"), ...));
...
OutResult = OutResult.Replace(TEXT("<ScriptOutputPath></ScriptOutputPath>"),
    *FString::Printf(TEXT("<ScriptOutputPath>..\\..\\Content\\%s</ScriptOutputPath>"),
                     *FUnrealCSharpFunctionLibrary::GetPublishDirectory()));
// ★ 工程引用、输出目录、目标框架都在生成期被项目化，而不是靠用户手改 csproj
```

关键源码 [2] `Reference/UnrealCSharp/Script/SourceGenerator/UnrealTypeSourceGenerator.cs:13-46, 482-486, 503-526, 679-718`

```csharp
// ============================================================================
// 文件: Reference/UnrealCSharp/Script/SourceGenerator/UnrealTypeSourceGenerator.cs
// 位置: IDE 编译期直接执行的 Unreal 类型规则校验
// ============================================================================
public static readonly DiagnosticDescriptor ErrorDynamicClassNotAPartialClass = new DiagnosticDescriptor(
    "UC_ERROR_01", "UClass or UStruct must be a partial class", ...);
public static readonly DiagnosticDescriptor ErrorFileNameNotMatch = new DiagnosticDescriptor(
    "UC_ERROR_02", "The file name and class name do not match", ...);
public static readonly DiagnosticDescriptor ErrorTypeNameNotMatch = new DiagnosticDescriptor(
    "UC_ERROR_03", "The name of dynamic class is error", ...);
public static readonly DiagnosticDescriptor ErrorUClassHasNoBaseClass = new DiagnosticDescriptor(
    "UC_ERROR_04", "UClass must have a base class", ...);
public static readonly DiagnosticDescriptor ErrorTypeMustBeUnique = new DiagnosticDescriptor(
    "UC_ERROR_05", "Type must be unique", ...);
// ★ 这些规则直接挂到 Roslyn 诊断系统里
...
Errors.Add(Diagnostic.Create(UnrealTypeSourceGenerator.ErrorUClassHasNoBaseClass, ..., $"{name} must have a base class"));
...
if (Syntax.Modifiers.ToArray().Any(Modifier => Modifier.Text == "partial") == false)
{
    Errors.Add(Diagnostic.Create(UnrealTypeSourceGenerator.ErrorDynamicClassNotAPartialClass, ...));
    return;
}
...
if (Path.GetFileName(filePath) != currentFileName)
{
    Errors.Add(Diagnostic.Create(UnrealTypeSourceGenerator.ErrorFileNameNotMatch, ..., currentFileName));
}
...
if (!Types.Add(Name))
{
    Errors.Add(Diagnostic.Create(UnrealTypeSourceGenerator.ErrorTypeMustBeUnique, ..., $"{Name} must be unique"));
}
// ★ 这些错误都发生在 IDE build 阶段，运行时还没开始装载 CLR
```

关键源码 [3] `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp:15-43, 95-138` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:351-416, 696-733`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp
// 位置: Angelscript 的 IDE 支持主轴是 editor 导航
// ============================================================================
virtual bool NavigateToClass(const UClass* InClass) override
{
    ...
    OpenModule(Module, ClassDesc->LineNumber);
    return true;
}
...
virtual bool NavigateToFunction(const UFunction* InFunction) override
{
    ...
    OpenFile(Path, ASFunc->GetSourceLineNumber());
    return true;
}
...
if (LineNo != -1)
    FPlatformMisc::OsExecute(nullptr, TEXT("code"), *FString::Printf(TEXT("--goto \"%s:%d\""), *Path, LineNo));
// ★ 最终动作是让 VS Code 直接跳到脚本文件和行号
...
FSourceCodeNavigation::AddNavigationHandler(new FAngelscriptSourceCodeNavigation);
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 函数: StartupModule / RegisterToolsMenuEntries
// 位置: Editor 提供 workspace 入口与导航注册，但没有生成 host-language project/analyzer
// ============================================================================
RegisterAngelscriptSourceNavigation();
...
UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FAngelscriptEditorModule::RegisterToolsMenuEntries));
...
Section.AddMenuEntry(
    "ASOpenCode",
    NSLOCTEXT("Angelscript", "OpenCode.Label", "Open Angelscript workspace (VS Code)"),
    NSLOCTEXT("Angelscript", "OpenCode.ToolTip", "Opens Visual Studio Code in this project's Angelscript workspace"),
    FSourceCodeNavigation::GetOpenSourceCodeIDEIcon(),
    Action);
// ★ 提供的是“打开脚本工作区”入口，而不是额外生成一个类型安全的宿主语言 solution
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| IDE 支撑层级 | UnrealCSharp 通过生成 `sln/csproj + Analyzer + Weaver + Publish` 固化 IDE contract | 实现方式不同 |
| 错误暴露时机 | UnrealCSharp 的 `partial/base/file-name/unique` 规则在 Roslyn 编译期报错 | 实现质量差异 |
| 跳转能力 | Angelscript 已实现 class/function/property/struct 到脚本文件行号的 `code --goto` 导航 | 不是没有实现 |
| 工作区入口 | Angelscript 在 Tools 菜单里显式提供 VS Code workspace 入口，但源码中未见 UnrealCSharp 这种生成型 host-language solution contract | 实现方式不同 |

这里不能简单写成“UnrealCSharp IDE 支持更好”。源码能证实的是：UnrealCSharp 的 IDE 支持偏 compile-time correctness（编译期正确性），Angelscript 的 IDE 支持偏 editor-time navigation（编辑器内导航）。二者解决的是不同层级的问题。

### [维度 D1] 工具链所有权：editor-owned pipeline vs editor services around runtime

前两轮 D1 已经把 Runtime 激活时序看清了。本轮继续追“整条生成/编译链到底挂在哪个模块手里”。UnrealCSharp 的答案很明确：`UnrealCSharpEditor` 自己就是 toolchain orchestrator（工具链编排者）。`StartupModule()` 直接注册 `CodeAnalysis`、`SolutionGenerator`、`Compile`、`Generator` 四个 console command；`Generator()` 又把 `Solution -> CodeAnalysis -> DynamicCodeAnalysisGenerator -> Class/Struct/Enum/Asset/Binding 生成 -> GC -> 立即编译` 固化成单条 12 步流水线。`FCSharpCompilerRunnable` 再通过 `OnBeginGenerator/OnEndGenerator` 委托清空任务与文件变更队列，说明编译线程只是这条 Editor 流水线的从属执行器。

Angelscript 则更像“Editor 提供服务，Runtime 持有引擎”。`AngelscriptEditorModule::StartupModule()` 做的是 source navigation、state dump extension、directory watcher、content browser data source、settings、工具菜单和若干 debug/create blueprint delegate；真正持有 primary engine 生命周期的是 `AngelscriptRuntimeModule`。这不是好坏之分，而是 ownership boundary（所有权边界）不同。

```
[D1-Deep] Toolchain Ownership Boundary

[UnrealCSharp]
UnrealCSharpEditor::StartupModule
├─ register CodeAnalysis / SolutionGenerator / Compile / Generator commands
├─ register toolbars + packaging/data source
└─ Generator()
   ├─ OnBeginGenerator
   ├─ FSolutionGenerator
   ├─ FCodeAnalysis + DynamicCodeAnalysisGenerator
   ├─ Class / Struct / Enum / Asset / Binding generators
   ├─ CollectGarbage
   ├─ FCSharpCompiler::ImmediatelyCompile
   └─ OnEndGenerator
      └─ FCSharpCompilerRunnable clears tasks/file changes

[Angelscript]
AngelscriptEditor::StartupModule
├─ class reload helper / source navigation
├─ directory watcher / content browser / settings
├─ tools menu / debug helpers / create blueprint helpers
└─ runtime-related actions delegated outward

AngelscriptRuntime::StartupModule
└─ InitializeAngelscript + fallback ticker + owned primary engine
```

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:33-125, 237-309`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp
// 函数: FUnrealCSharpEditorModule::StartupModule / Generator
// 位置: UnrealCSharpEditor 自己拥有整条工具链入口
// ============================================================================
CodeAnalysisConsoleCommand = MakeUnique<FAutoConsoleCommand>(
    TEXT("UnrealCSharp.Editor.CodeAnalysis"), TEXT(""),
    FConsoleCommandDelegate::CreateLambda([](){ FCodeAnalysis::CodeAnalysis(); }));

SolutionGeneratorConsoleCommand = MakeUnique<FAutoConsoleCommand>(
    TEXT("UnrealCSharp.Editor.SolutionGenerator"), TEXT(""),
    FConsoleCommandDelegate::CreateLambda([](){ FSolutionGenerator::Generator(); }));

CompileConsoleCommand = MakeUnique<FAutoConsoleCommand>(
    TEXT("UnrealCSharp.Editor.Compile"), TEXT(""),
    FConsoleCommandDelegate::CreateLambda([](){ FCSharpCompiler::Get().Compile([](){}); }));

GeneratorConsoleCommand = MakeUnique<FAutoConsoleCommand>(
    TEXT("UnrealCSharp.Editor.Generator"), TEXT(""),
    FConsoleCommandDelegate::CreateLambda([](){ Generator(); }));
// ★ 生成、分析、编译、solution 都由 Editor 模块直接暴露入口
...
FUnrealCSharpCoreModuleDelegates::OnBeginGenerator.Broadcast();
FSolutionGenerator::Generator();
FCodeAnalysis::CodeAnalysis();
FDynamicGenerator::CodeAnalysisGenerator();
FGeneratorCore::BeginGenerator();
FClassGenerator::Generator();
FStructGenerator::Generator();
FEnumGenerator::Generator();
FAssetGenerator::Generator();
FBindingClassGenerator::Generator();
FBindingEnumGenerator::Generator();
FGeneratorCore::EndGenerator();
CollectGarbage(RF_NoFlags, true);
FCSharpCompiler::Get().ImmediatelyCompile();
FUnrealCSharpCoreModuleDelegates::OnEndGenerator.Broadcast();
// ★ Editor 直接串起“生成 -> 编译”整条链，而不是只提供菜单按钮
```

关键源码 [2] `Reference/UnrealCSharp/Source/Compiler/Private/FCSharpCompilerRunnable.cpp:22-27, 166-193, 328-343`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/Compiler/Private/FCSharpCompilerRunnable.cpp
// 函数: FCSharpCompilerRunnable::FCSharpCompilerRunnable / Compile / OnBeginGenerator / OnEndGenerator
// 位置: 编译线程对 Editor 生成周期显式让路
// ============================================================================
OnBeginGeneratorDelegateHandle = FUnrealCSharpCoreModuleDelegates::OnBeginGenerator.AddRaw(
    this, &FCSharpCompilerRunnable::OnBeginGenerator);
OnEndGeneratorDelegateHandle = FUnrealCSharpCoreModuleDelegates::OnEndGenerator.AddRaw(
    this, &FCSharpCompilerRunnable::OnEndGenerator);
// ★ CompilerRunnable 明确订阅生成周期
...
FUnrealCSharpCoreModuleDelegates::OnCompile.Broadcast(FileChanges);
InFunction();
// ★ 编译完成后才回到 GameThread 执行动态生成/分析的后半段
...
void FCSharpCompilerRunnable::OnBeginGenerator()
{
    bIsGenerating = true;
    Tasks.Empty();
    FileChanges.Empty();
}

void FCSharpCompilerRunnable::OnEndGenerator()
{
    bIsGenerating = false;
    Tasks.Empty();
    FileChanges.Empty();
}
// ★ 只要进入全量生成，编译队列就被清空，说明所有权在 Generator 而不是后台编译线程
```

关键源码 [3] `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:351-416` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp:13-39, 138-166`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 函数: FAngelscriptEditorModule::StartupModule
// 位置: Editor 侧更像服务聚合层
// ============================================================================
FClassReloadHelper::Init();
RegisterAngelscriptSourceNavigation();
...
UScriptEditorMenuExtension::InitializeExtensions();
AngelscriptEditor::Private::RegisterStateDumpExtension(StateDumpExtensionHandle);
...
for (const auto& RootPath : AllRootPaths)
{
    DirectoryWatcher->RegisterDirectoryChangedCallback_Handle(
        *RootPath,
        IDirectoryWatcher::FDirectoryChanged::CreateStatic(&OnScriptFileChanges),
        WatchHandle,
        IDirectoryWatcher::IncludeDirectoryChanges);
}
...
FAngelscriptRuntimeModule::GetDebugListAssets().AddLambda(...);
FAngelscriptRuntimeModule::GetEditorCreateBlueprint().AddLambda(...);
UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FAngelscriptEditorModule::RegisterToolsMenuEntries));
// ★ 这里注册的是导航、监听、UI、debug helper，不是一条“生成 -> 编译”总流水线
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp
// 函数: StartupModule / InitializeAngelscript / ShutdownModule
// 位置: primary engine 生命周期由 Runtime 模块持有
// ============================================================================
if (GIsEditor || IsRunningCommandlet())
{
    InitializeAngelscript();
}

if (GIsEditor)
{
    FallbackTickHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateRaw(this, &FAngelscriptRuntimeModule::TickFallbackPrimaryEngine));
}
...
OwnedPrimaryEngine = MakeUnique<FAngelscriptEngine>();
FAngelscriptEngineContextStack::Push(OwnedPrimaryEngine.Get());
OwnedPrimaryEngine->Initialize();
// ★ engine 初始化、context stack 与 fallback tick 都属于 Runtime 模块
...
if (OwnedPrimaryEngine.IsValid())
{
    FAngelscriptEngineContextStack::Pop(OwnedPrimaryEngine.Get());
    OwnedPrimaryEngine.Reset();
}
// ★ shutdown 也由 Runtime 收口
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 工具链入口归属 | UnrealCSharp 的 `UnrealCSharpEditor` 直接拥有 `SolutionGenerator/CodeAnalysis/Generator/Compile` 四类入口 | 实现方式不同 |
| 生成与编译的主从关系 | UnrealCSharp 的 compiler runnable 通过 begin/end delegate 给生成流程让路 | 实现方式不同 |
| Editor 模块职责 | Angelscript Editor 更像导航、监听、UI、debug helper 聚合层 | 实现方式不同 |
| Runtime 生命周期归属 | Angelscript 的 primary engine 初始化、tick、shutdown 明确落在 `AngelscriptRuntimeModule` | 实现方式不同 |

这条差距也不能写成“Angelscript 架构更松散”或“UnrealCSharp 更模块化”。更准确的结论是：UnrealCSharp 把工具链编排权上收到了 Editor 模块，Angelscript 把 Editor 做成 runtime 周边服务层。前者有利于把 `solution/codegen/compile/publish` 串成单条流水线，后者有利于让 runtime engine 生命周期保持单一所有者。

---

## 深化分析 (2026-04-08 19:05:38)

### [维度 D2] 类型桥接内核：structural property graph vs declaration-first signature

前几轮已经说明 UnrealCSharp 的 override 绑定不是纯手写。本轮继续往下挖，会发现它的真正核心不是 `Bind_*.cpp` 级别的注册数量，而是 `FTypeBridge` 这层“结构化类型桥”。`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:12-165` 先把托管 `FClassReflection` 归类到 `EPropertyTypeExtent`；随后 `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Bridge/FTypeBridge.inl:7-360` 递归构造 `FMapProperty / FSetProperty / FArrayProperty / FEnumProperty / FObjectProperty`。更关键的是，`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:562-611` 还把 `FMapProperty / FSetProperty / FOptionalProperty` 反向恢复为 C# 泛型类型。这说明 UnrealCSharp 的桥接 contract 不是“先生成一串声明，再想办法解释它”，而是“脚本类型和 UE property graph 之间存在一个可往返的结构映射”。

Angelscript 的做法则更偏 declaration-first。`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs:43-100` 在 UHT 阶段先把参数和返回值序列化为 C++ 类型字符串；运行时 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h:178-323` 再把 `UFunction` 属性映射到 `FAngelscriptTypeUsage`，最后产出 script declaration 文本。即使在 bind 缺口场景，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp:100-163,302-360` 也仍然是“先用 declaration 找到 script function，再用 `ProcessEvent()` 走 `UFunction` 属性表完成拷参”。换句话说，Angelscript 当然也有类型系统，但它对脚本 VM 暴露的主 contract 仍是 declaration/signature，而不是像 UnrealCSharp 那样把 `FProperty` 图当成一等桥接对象。

这一点会直接影响“泛型容器、复杂枚举、软引用、interface、optional”这类类型的扩展方式。UnrealCSharp 只要把新类型挂进 `FTypeBridge`，容器 helper、属性读写、函数 descriptor、动态类生成都会自动复用；Angelscript 则通常需要同时维护 `FAngelscriptTypeUsage`、签名生成、绑定注册与必要时的 reflective fallback。

```
[D2-Deep] Type Bridge Core

[UnrealCSharp]
Managed reflection type
├─ FTypeBridge::GetPropertyType()             // 先判定应落哪种 UE property
├─ FTypeBridge::Factory()                     // 递归构造 FProperty 图
│  ├─ Object / Interface / Enum
│  ├─ Array / Set / Map
│  └─ Soft / Weak / Lazy references
├─ FPropertyDescriptor::Factory()             // 再包装成 get/set/marshal descriptor
└─ FCSharpBind / TPropertyValue reuse graph   // 绑定、容器、属性访问共用同一桥接核

[Angelscript]
UHT + UFunction
├─ AngelscriptFunctionSignatureBuilder        // 先生成参数/返回值字符串
├─ FAngelscriptFunctionSignature              // 再生成 script declaration
├─ BindMethodDirect / BindGlobalFunctionDirect
└─ Reflective fallback -> ProcessEvent        // 运行时沿 UFunction property list 拷参
```

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:12-165,562-611`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp
// 函数: FTypeBridge::GetPropertyType / FTypeBridge::GetClass
// 位置: 12-165, 562-611，托管类型 <-> UE property graph 的双向归类入口
// ============================================================================
EPropertyTypeExtent FTypeBridge::GetPropertyType(const FClassReflection* InClass)
{
    const auto TypeDefinition = InClass->GetTypeDefinition();

    if (TypeDefinition->IsAssignableFrom(FReflectionRegistry::Get().GetUClassClass()))
    {
        return EPropertyTypeExtent::ClassReference;
    }

    if (TypeDefinition->IsAssignableFrom(FReflectionRegistry::Get().GetTScriptInterfaceClass()))
    {
        return EPropertyTypeExtent::Interface;
    }

    if (TypeDefinition->IsAssignableFrom(FReflectionRegistry::Get().GetEnumClass()))
    {
        return EPropertyTypeExtent::Enum;
    }

    if (TypeDefinition->IsAssignableFrom(FReflectionRegistry::Get().GetTMapClass()))
    {
        return EPropertyTypeExtent::Map;      // ★ 泛型 Map 先被归类成结构类型
    }

    if (TypeDefinition->IsAssignableFrom(FReflectionRegistry::Get().GetTSetClass()))
    {
        return EPropertyTypeExtent::Set;
    }

    if (TypeDefinition->IsAssignableFrom(FReflectionRegistry::Get().GetTArrayClass()))
    {
        return EPropertyTypeExtent::Array;
    }

    return EPropertyTypeExtent::None;
}

FClassReflection* FTypeBridge::GetClass(const FMapProperty* InProperty)
{
    if (InProperty != nullptr)
    {
        const auto FoundGenericClass = FReflectionRegistry::Get().GetTMapClass();
        const auto FoundKeyClass = GetClass(InProperty->KeyProp);
        const auto FoundValueClass = GetClass(InProperty->ValueProp);
        const auto ReflectionTypeArray = FReflectionRegistry::Get().GetObjectClass()->NewArray(2);

        FMonoDomain::Array_Set(ReflectionTypeArray, 0, FoundKeyClass->GetReflectionType());
        FMonoDomain::Array_Set(ReflectionTypeArray, 1, FoundValueClass->GetReflectionType());

        return MakeGenericTypeInstance(FoundGenericClass, ReflectionTypeArray);
        // ★ 不只会“从 C# 生 FProperty”，还能“从 FProperty 还原 C# 泛型”
    }

    return nullptr;
}

FClassReflection* FTypeBridge::GetClass(const FSetProperty* InProperty)
{
    if (InProperty != nullptr)
    {
        const auto FoundGenericClass = FReflectionRegistry::Get().GetTSetClass();
        const auto FoundClass = GetClass(InProperty->ElementProp);
        return MakeGenericTypeInstance(FoundGenericClass, FoundClass);
    }

    return nullptr;
}
```

关键源码 [2] `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Bridge/FTypeBridge.inl:7-113,145-291`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Bridge/FTypeBridge.inl
// 函数: FTypeBridge::Factory / ManagedFactory
// 位置: 7-113, 145-291，递归构造 UE property graph
// ============================================================================
template <auto IsSoftReference>
FProperty* FTypeBridge::Factory(FClassReflection* InClass, const FFieldVariant& InOwner,
                                const FName& InName, const EObjectFlags InObjectFlags)
{
    switch (const auto PropertyType = GetPropertyType(InClass); PropertyType)
    {
    case EPropertyTypeExtent::Int: return new FIntProperty(InOwner, InName, InObjectFlags);
    case EPropertyTypeExtent::Name: return new FNameProperty(InOwner, InName, InObjectFlags);
    case EPropertyTypeExtent::Struct:
    case EPropertyTypeExtent::Enum:
    case EPropertyTypeExtent::Map:
    case EPropertyTypeExtent::Set:
    case EPropertyTypeExtent::Array:
        {
            return ManagedFactory<IsSoftReference>(PropertyType, InClass, InOwner, InName, InObjectFlags);
            // ★ 复杂类型统一走 ManagedFactory，避免每个调用点自己拼 property graph
        }
    default:
        return nullptr;
    }
}

template <auto IsSoftReference>
FProperty* FTypeBridge::ManagedFactory(EPropertyTypeExtent InPropertyType, FClassReflection* InClass,
                                       const FFieldVariant& InOwner, const FName& InName,
                                       const EObjectFlags InObjectFlags)
{
    switch (InPropertyType)
    {
    case EPropertyTypeExtent::Map:
        {
            const auto MapProperty = new FMapProperty(InOwner, InName, InObjectFlags);
            MapProperty->KeyProp = Factory<IsSoftReference>(InClass->GetGenericArgument(),
                                                            MapProperty, "", EObjectFlags::RF_Transient);
            MapProperty->ValueProp = Factory<IsSoftReference>(InClass->GetGenericArgument(1),
                                                              MapProperty, "", EObjectFlags::RF_Transient);
            return MapProperty;
        }

    case EPropertyTypeExtent::Set:
        {
            const auto SetProperty = new FSetProperty(InOwner, InName, InObjectFlags);
            SetProperty->ElementProp = Factory<IsSoftReference>(InClass->GetGenericArgument(),
                                                                SetProperty, "", EObjectFlags::RF_Transient);
            return SetProperty;
        }

    case EPropertyTypeExtent::Array:
        {
            const auto ArrayProperty = new FArrayProperty(InOwner, InName, InObjectFlags);
            ArrayProperty->Inner = Factory<IsSoftReference>(InClass->GetGenericArgument(),
                                                            ArrayProperty, "", EObjectFlags::RF_Transient);
            return ArrayProperty;
        }

    case EPropertyTypeExtent::Enum:
        {
            const auto EnumProperty = new FEnumProperty(InOwner, InName, InObjectFlags);
            const auto UnderlyingProperty = Factory(InClass->GetUnderlyingType(),
                                                    EnumProperty, "", EObjectFlags::RF_NoFlags);
            EnumProperty->SetEnum(LoadObject<UEnum>(nullptr, *InClass->GetPathName()));
            EnumProperty->AddCppProperty(UnderlyingProperty);
            return EnumProperty;
        }

    default:
        return nullptr;
    }
}
```

关键源码 [3] `Reference/UnrealCSharp/Source/UnrealCSharp/Public/Registry/FCSharpBind.inl:88-117`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Public/Registry/FCSharpBind.inl
// 函数: FCSharpBind::BindImplementation
// 位置: 88-117，容器 helper 直接复用 FTypeBridge
// ============================================================================
const auto Property = FTypeBridge::Factory<>(InPropertyClassReflection, nullptr, "", EObjectFlags::RF_Transient);
Property->SetPropertyFlags(CPF_HasGetValueTypeHash);
const auto ContainerHelper = new T(Property, nullptr, true, true);
FCSharpEnvironment::GetEnvironment().AddContainerReference(ContainerHelper, InClassReflection, InMonoObject);
// ★ Array/Set 的 helper 不是硬编码类型表，而是用 FTypeBridge 临时构造 element property

const auto KeyProperty = FTypeBridge::Factory<>(InKeyClassReflection, nullptr, "", EObjectFlags::RF_Transient);
const auto ValueProperty = FTypeBridge::Factory<>(InValueClassReflection, nullptr, "", EObjectFlags::RF_Transient);
KeyProperty->SetPropertyFlags(CPF_HasGetValueTypeHash);
ValueProperty->SetPropertyFlags(CPF_HasGetValueTypeHash);
const auto ContainerHelper = new T(KeyProperty, ValueProperty, nullptr, true, true);
// ★ Map 也是同一套桥接核，只是递归深一层
```

关键源码 [4] `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs:43-100` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h:178-323`

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs
// 函数: AngelscriptFunctionSignatureBuilder::TryBuild
// 位置: 43-100，UHT 阶段先把函数形状序列化成字符串签名
// ============================================================================
public static bool TryBuild(UhtClass classObj, UhtFunction function,
    out AngelscriptFunctionSignature? signature, out string? failureReason)
{
    ...
    List<string> parameterTypes = new();
    foreach (UhtType parameterType in function.ParameterProperties.Span)
    {
        ...
        parameterTypes.Add(BuildParameterType(property));   // ★ 参数先落成文本
    }

    string returnType = function.ReturnProperty is UhtProperty returnProperty
        ? BuildReturnType(returnProperty)
        : "void";

    signature = new AngelscriptFunctionSignature(
        classObj.SourceName,
        function.SourceName,
        returnType,
        parameterTypes,
        HasFunctionFlag(function, "Static"),
        HasFunctionFlag(function, "Const"),
        true);
    return true;
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h
// 函数: FAngelscriptFunctionSignature::InitFromFunction
// 位置: 178-323，运行时把 UFunction 属性映射为 script declaration
// ============================================================================
for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
{
    FProperty* Property = *It;
    FAngelscriptTypeUsage Type = FAngelscriptTypeUsage::FromProperty(Property);

    if (!Type.IsValid())
    {
        bAllTypesValid = false;
        break;
    }

    if (Property->PropertyFlags & CPF_ReturnParm)
    {
        ReturnType = Type;
    }
    else
    {
        ArgumentTypes.Add(Type);
        ArgumentNames.Add(Property->GetName());
    }
}

Declaration = FAngelscriptType::BuildFunctionDeclaration(
    ReturnType, ScriptName, ArgumentTypes, ArgumentNames, ArgumentDefaults,
    (Function->HasAnyFunctionFlags(FUNC_Const) && !bStaticInScript) || bForceConst);
// ★ 最终交给 VM 的核心 contract 是 declaration 字符串
```

关键源码 [5] `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp:100-163,302-360`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp
// 函数: BindReflectiveFunction / InvokeReflectiveUFunctionFromGenericCall
// 位置: 100-163, 302-360，bind 缺口场景回退到 declaration + ProcessEvent
// ============================================================================
const int32 FunctionId = FAngelscriptBinds::BindMethodDirect(
    InType->GetAngelscriptTypeName(),
    Signature.Declaration,
    asFUNCTION(CallBlueprintCallableReflectiveFallback),
    asCALL_GENERIC,
    ASAutoCaller::FunctionCaller::Make(),
    ReflectiveSignature);
Signature.ModifyScriptFunction(FunctionId);
// ★ fallback 仍先依赖 declaration 绑定一个 script callable

uint8* ParameterBuffer = static_cast<uint8*>(FMemory_Alloca(Function->ParmsSize));
InitializeParameterBuffer(Function, ParameterBuffer);

for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
{
    FProperty* Property = *It;
    ...
    void* Destination = Property->ContainerPtrToValuePtr<void>(ParameterBuffer);
    void* SourceAddress = ResolveScriptArgumentAddress(Property, ScriptArgumentAddress);
    Property->CopySingleValue(Destination, SourceAddress);
}

TargetObject->ProcessEvent(Function, ParameterBuffer);
// ★ 真正执行时还是回到 UFunction property list 和 ProcessEvent
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 类型系统核心 contract | UnrealCSharp 的核心 contract 是可往返的 `FProperty` 结构图，而不是单向声明文本 | 实现方式不同 |
| 泛型容器桥接 | UnrealCSharp 的 `Map/Set/Array` 由 `FTypeBridge` 递归构造并被容器 helper 复用 | 实现质量差异 |
| 签名入口 | Angelscript 的主 contract 是 `UHT signature string + runtime declaration` | 实现方式不同 |
| bind 缺口恢复 | Angelscript 已有 reflective fallback，不是“绑定不到就完全失败” | 不是没有实现 |

这里最值得吸收的不是“把 Angelscript 全部改成 C# 那套桥”，而是把“类型桥接 contract”从当前较分散的 `TypeUsage/decl/db/fallback` 再收拢一层。只要桥接 contract 不够集中，后续每加一种复杂类型，签名生成、运行时 marshalling、fallback 路径就都要同步动。

### [维度 D8] 执行路径优化：collectible CLR + pooled marshaling vs bytecode-to-C++ static JIT

前面的 D8 主要讨论了 GC 与所有权。本轮继续下钻执行路径，会发现 UnrealCSharp 的优化重点不是“尽量让 Mono 跑得像 C++”，而是把“可卸载运行时 + 低反射开销 marshalling + 可观测性”三件事一起做。`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp:46-123` 明确区分 iOS 与非 iOS：iOS 走 `MONO_AOT_MODE_INTERP`，非 iOS 走 `MONO_AOT_MODE_NONE`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp:579-748` 又在 Editor 里构造 `AssemblyLoadContext(isCollectible=true)`，用 `LoadFromStream` 装载 DLL，并用 `GCHandle` 持住程序集对象后在卸载阶段统一释放。也就是说，UnrealCSharp 的第一层优化目标其实是“热迭代不锁文件、域能回收、平台模式可切换”。

第二层优化落在桥接调用本身。`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FFunctionDescriptor.cpp:18-60` 预先把 `FProperty` 拆成 `PropertyDescriptors / OutPropertyIndexes / ReferencePropertyIndexes`；`Reference/UnrealCSharp/Source/UnrealCSharp/Public/Reflection/Function/FFunctionParamBufferAllocator.h:57-71` 与 `.../Private/Reflection/Function/FFunctionParamBufferAllocator.cpp:20-83` 再按 `ParmsSize` 选择 pool/persistent/empty allocator；最后 `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FCSharpFunctionDescriptor.cpp:17-180` 只在非 native 栈场景申请参数缓存，随后直接把参数装成 `MonoObject[]` 调 `Runtime_Invoke_Array()`。这说明 UnrealCSharp 优化的是“每次跨边界时不要再重新做完整反射分析”。

Angelscript 的策略则更激进，直接把运行时成本往构建期推。`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp:3204-3380` 会线性扫描 bytecode、做 pre-pass 计算 label 和 stack offset，然后生成 C++ 函数体；`...:3415-3468` 再根据 virtual override 情况把函数标成 `final` 并尝试 devirtualize；`...:2683-2785` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UStruct.cpp:623-642` 还会判断某个脚本值类型是否“potentially different”，只有结构稳定、且能证明是 POD-like 时才允许在模板容器里走硬编码 native form。换句话说，Angelscript 不是在 callsite 上做轻量 marshalling，而是在编译阶段把一大段解释器语义提前折叠掉。

```
[D8-Deep] Execution Optimization Strategy

[UnrealCSharp]
Mono domain init
├─ iOS -> MONO_AOT_MODE_INTERP                // AOT runtime + interpreter execute
├─ Other -> MONO_AOT_MODE_NONE               // 常规 JIT/IL 路径
├─ optional debugger / CSharp trace profiler
└─ collectible AssemblyLoadContext           // 编辑器期可卸载程序集

Interop call path
├─ FFunctionDescriptor precomputes descriptors
├─ BufferAllocator reuses param memory
└─ Runtime_Invoke_Array managed call         // 每次调用只做必要 boxing/unboxing

[Angelscript]
StaticJIT pipeline
├─ Read bytecode
├─ PrePass compute labels + stack offsets
├─ Generate native C++ body
├─ mark final / devirtualize
└─ execute compiled native path

Type fastpath gate
└─ only stable POD-like script structs use hardcoded native form
```

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp:46-123,579-748`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp
// 函数: FMonoDomain::Initialize / InitializeAssemblyLoadContext / LoadAssembly / UnloadAssembly
// 位置: 46-123, 579-748，平台模式选择 + 可卸载程序集装载
// ============================================================================
void FMonoDomain::Initialize(const FMonoDomainInitializeParams& InParams)
{
    RegisterMonoTrace();
    RegisterAssemblyPreloadHook();

    if (Domain == nullptr)
    {
#if PLATFORM_IOS
        mono_jit_set_aot_mode(MONO_AOT_MODE_INTERP);
        mono_aot_register_module(static_cast<void**>(mono_aot_module_System_Private_CoreLib_info));
        // ★ iOS 明确走 AOT + interpreter 路径
#else
        mono_jit_set_aot_mode(MONO_AOT_MODE_NONE);
        // ★ 非 iOS 允许常规 JIT/IL 路径
#endif

        mono_debug_init(MONO_DEBUG_FORMAT_MONO);
        Domain = mono_jit_init("UnrealCSharp");
        mono_domain_set(Domain, false);
        RegisterProfiler();
    }

    InitializeAssembly(InParams.Assemblies);
}

const auto AssemblyLoadContextObject = Object_Init(AssemblyLoadContextClass, 2, Params);
AssemblyLoadContextGCHandle = GCHandle_New_V2(AssemblyLoadContextObject, false);
// ★ Editor 里显式创建 collectible AssemblyLoadContext

const auto Result = Runtime_Invoke(AlcLoadFromStreamMethod, AssemblyLoadContextObject, Params);
auto GCHandle = GCHandle_New_V2(Result, true);
AssemblyGCHandles.Add(GCHandle);
// ★ 程序集通过 stream 装入，再用 GCHandle 持有，减少文件锁与域切换残留

for (const auto GCHandle : AssemblyGCHandles)
{
    GCHandle_Free_V2(GCHandle);
}
mono_image_close(Image);
// ★ 卸载阶段释放程序集对象和 image
```

关键源码 [2] `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoProfiler.cpp:7-77`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoProfiler.cpp
// 函数: FMonoProfiler::Register / Method_Enter / Method_Leave
// 位置: 7-77，C# 调用栈直接接入 UE Trace
// ============================================================================
if (FString Channels; FParse::Value(FCommandLine::Get(), TEXT("-trace="), Channels, false))
{
    if (Channels.ToLower().Contains(TEXT("CSharp")))
    {
        if (ProfilerHandle = mono_profiler_create(nullptr); ProfilerHandle != nullptr)
        {
            mono_profiler_set_method_enter_callback(ProfilerHandle, Method_Enter);
            mono_profiler_set_method_leave_callback(ProfilerHandle, Method_Leave);
            mono_profiler_set_method_exception_leave_callback(ProfilerHandle, Method_Exception_Leave);
            mono_profiler_set_call_instrumentation_filter_callback(ProfilerHandle, Call_Instrumentation_Filter);
        }
    }
}

const auto EventName = FString::Printf(TEXT("[C#] %s.%s.%s"),
                                       *MethodNamespace, *ClassName, *MethodName);
FCpuProfilerTrace::OutputBeginDynamicEvent(*EventName);
// ★ 这不是普通日志，而是把 Mono method enter/leave 直接投到 CPU trace
```

关键源码 [3] `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FFunctionDescriptor.cpp:18-60`、`Reference/UnrealCSharp/Source/UnrealCSharp/Public/Reflection/Function/FFunctionParamBufferAllocator.h:57-71`、`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FCSharpFunctionDescriptor.cpp:17-180`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FFunctionDescriptor.cpp
// 函数: FFunctionDescriptor::Initialize
// 位置: 18-60，调用前先把 property/out/ref 索引缓存好
// ============================================================================
for (TFieldIterator<FProperty> It(Function.Get()); It && (It->PropertyFlags & CPF_Parm); ++It)
{
    auto PropertyDescriptor = FPropertyDescriptor::Factory(Property);

    if (Property->HasAnyPropertyFlags(CPF_ReturnParm))
    {
        ReturnPropertyDescriptor = PropertyDescriptor;
        continue;
    }

    const auto Index = PropertyDescriptors.Add(PropertyDescriptor);
    if (Property->HasAnyPropertyFlags(CPF_OutParm) && !Property->HasAnyPropertyFlags(CPF_ConstParm))
    {
        if (IsNativeFunction || Property->HasAnyPropertyFlags(CPF_ReferenceParm))
        {
            ReferencePropertyIndexes.Emplace(Index);   // ★ 后续调用时不再重扫 property 语义
        }

        OutPropertyIndexes.Emplace(Index);
    }
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Public/Reflection/Function/FFunctionParamBufferAllocator.h
// 函数: FFunctionParamBufferAllocatorFactory::Factory
// 位置: 57-71，按 ParmsSize 选择 buffer allocator
// ============================================================================
template <typename BufferAllocatorType, typename EmptyBufferAllocatorType = FFunctionParamEmptyBufferAllocator>
static TSharedRef<FFunctionParamBufferAllocator> Factory(const TWeakObjectPtr<UFunction>& InFunction)
{
    if (InFunction->ParmsSize > 0)
    {
        return MakeShared<BufferAllocatorType>(InFunction);
    }

    static EmptyBufferAllocatorType EmptyBufferAllocator;
    return MakeShared<EmptyBufferAllocatorType>(EmptyBufferAllocator);
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FCSharpFunctionDescriptor.cpp
// 函数: FCSharpFunctionDescriptor::CallCSharp
// 位置: 17-180，仅在必要时申请参数缓存并直接调用托管方法
// ============================================================================
if (InStack.Node != InStack.CurrentNativeFunction)
{
    Params = BufferAllocator.IsValid() ? BufferAllocator->Malloc() : nullptr;
    ...
}

const auto CSharpParams = FReflectionRegistry::Get().GetObjectClass()->NewArray(PropertyDescriptors.Num());
for (auto Index = 0; Index < PropertyDescriptors.Num(); ++Index)
{
    ...
    PropertyDescriptors[Index]->Get<std::false_type>(PropertyAddress, &Object);
    FDomain::Array_Set(CSharpParams, Index, static_cast<MonoObject*>(Object));
}

if (const auto ReturnValue = Method->Runtime_Invoke_Array(FoundMonoObject, CSharpParams);
    ReturnValue != nullptr && ReturnPropertyDescriptor != nullptr)
{
    ...
}

BufferAllocator->Free(Params);
// ★ 这里的优化点不是“完全无 marshalling”，而是把 descriptor 分析前移，把 buffer 生命周期压到最短
```

关键源码 [4] `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp:3204-3380,3415-3468`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp
// 函数: FAngelscriptStaticJIT::GenerateCppCode / DevirtualizeFunction / AnalyzeScriptFunction
// 位置: 3204-3380, 3415-3468，bytecode 预分析 + native codegen + devirtualize
// ============================================================================
Context.BC = ScriptFunction->GetByteCode(&BytecodeLength);
Context.BC_FunctionStart = Context.BC;
Context.BC_End = Context.BC + BytecodeLength;

while (Context.BC < Context.BC_End)
{
    asEBCInstr Instr = Context.GetInstr();
    FAngelscriptBytecode& Bytecode = FAngelscriptBytecode::GetBytecode(Instr);
    FStaticJITContext::FInstruction& NewInstr = Context.Instructions.Emplace_GetRef();
    NewInstr.BC = Context.BC;
    NewInstr.Bytecode = &Bytecode;
    Context.AdvanceBC();
}

for (int32 i = 0, Count = Context.Instructions.Num(); i < Count; ++i)
{
    Context.CurrentInstructionIndex = i;
    Context.BC = Context.Instructions[i].BC;
    Context.Instructions[i].Bytecode->PrePass(Context);
    // ★ 先做 pre-pass，后面才写真正的 C++ 代码
}

Context.GenerateNewFunction(ScriptFunction);
...
bool bImplemented = Bytecode.Implement(Context);
check(bImplemented);
// ★ 真正把 bytecode 逐条翻译成 native function body

if (bAllowDevirtualize && !FunctionsWithVirtualOverrides.Contains(VirtualFunction))
{
    return VirtualFunction;                    // ★ 能证明没被 override，就允许直接去虚调用
}

if (!FunctionsWithVirtualOverrides.Contains(ScriptFunction))
{
    ScriptFunction->traits.SetTrait(asTRAIT_FINAL, true);
    // ★ 先分析 override，再把函数标 final，为后续 runtime path 减枝
}
```

关键源码 [5] `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp:2683-2785` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UStruct.cpp:623-642`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp
// 函数: FAngelscriptStaticJIT::IsTypePotentiallyDifferent
// 位置: 2683-2785，决定哪些 script/native 类型可以安全硬编码
// ============================================================================
if ((Flags & asOBJ_TEMPLATE) != 0)
{
    if ((ObjectType->templateBaseType->flags & asOBJ_TEMPLATE_SUBTYPE_DETERMINES_SIZE) == 0)
        return false;
    else
        return true;
}

if ((Flags & asOBJ_SCRIPT_OBJECT) == 0)
{
    ...
    if (!GuaranteedNotDifferentStructs.Contains(UnrealStruct))
    {
        bPotentialDifference = true;
    }
}
else
{
    for (int32 i = 0, Count = ObjectType->properties.GetLength(); i < Count; ++i)
    {
        auto& DataType = ObjectType->properties[i]->type;
        if (DataType.IsObject() && !DataType.IsReference() && !DataType.IsReferenceType())
        {
            if (IsTypePotentiallyDifferent((asCObjectType*)DataType.GetTypeInfo()))
            {
                bPotentialDifference = true;
                break;
            }
        }
    }
}
// ★ 只有能证明尺寸/布局稳定的类型，StaticJIT 才敢继续走更激进的 native fastpath
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UStruct.cpp
// 函数: FUStructType::GetCppForm
// 位置: 623-642，POD-like script struct 才允许转 native form
// ============================================================================
if (ObjectType != nullptr && (ObjectType->flags & asOBJ_POD) != 0
    && FAngelscriptEngine::Get().StaticJIT != nullptr
    && !FAngelscriptEngine::Get().StaticJIT->IsTypePotentiallyDifferent(ObjectType)
    && ObjectType->GetFirstMethod("opAssign") == nullptr)
{
    int Size = GetValueSize(Usage);
    if (Size == 0)
        OutCppForm.CppType = FString::Printf(TEXT("TScriptPODEmptyStruct<%d>"), GetValueAlignment(Usage));
    else
        OutCppForm.CppType = FString::Printf(TEXT("TScriptPODStruct<%d,%d>"), GetValueSize(Usage), GetValueAlignment(Usage));
    return true;
}
// ★ 不是所有 script struct 都 native 化，只有稳定 POD 才放行
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| JIT/AOT 路径 | UnrealCSharp 明确区分 `MONO_AOT_MODE_INTERP` 与 `MONO_AOT_MODE_NONE`，Angelscript 则把脚本 bytecode 直接 lowering 成 C++ | 实现方式不同 |
| 调用优化位置 | UnrealCSharp 优化 callsite 的 buffer/descriptor/profiler，Angelscript 优化 compile-time devirtualize 与 native codegen | 实现方式不同 |
| 可观测性 | UnrealCSharp 在 `-trace=CSharp` 下把 Mono method enter/leave 直接投到 UE Trace | 实现质量差异 |
| 类型 fastpath 安全阀 | Angelscript 不是无脑 native 化，而是先做 `IsTypePotentiallyDifferent` 判断 | 不是没有实现 |

这里不能简单写成“StaticJIT 比 Mono 快”或“Mono 更灵活”。源码能证明的更准确结论是：UnrealCSharp 优化的是“跨边界成本 + 可卸载 + 可观测”，Angelscript 优化的是“把解释期成本尽量前移到代码生成与原生编译”。两者押注的性能瓶颈完全不同。

### [维度 D11] 交付物归属：managed payload staging vs generated native code folding

前面的 D11 已经说明 UnrealCSharp 有 staging 概念。本轮把整条交付链串起来后，可以看到它的一个关键设计：`dotnet build` 在这个插件里其实等价于 “build + publish + copy runtime payload”。`Reference/UnrealCSharp/Template/Game.props:16-27` 在 `Build` 后自动触发 `Publish`，并把 DLL 复制到 `ScriptOutputPath`；`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FSolutionGenerator.cpp:196-229` 又把 `ScriptOutputPath` 固定改写为 `..\..\Content\<PublishDirectory>`，且 cook 时不再注入 `WITH_EDITOR` 宏；`Reference/UnrealCSharp/Source/Compiler/Private/FCSharpCompilerRunnable.cpp:235-257` 则在 cook commandlet 下切到 runtime configuration；最后 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:102-124,209-235` 自动把 publish 目录写入 `DirectoriesToAlwaysStageAsUFS`，并在 cook commandlet 等 `AssetRegistry` 文件加载完成后再次跑 `Generator()`。这是一条完整、显式、源码内可追踪的 managed payload 交付链。

Angelscript 的交付物所有权则明显不同。`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp:3499-3608` 把静态 JIT 产物写到项目根目录 `AS_JITTED_CODE/`，产物本质是 C++ 源文件和共享头；同时 `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs:1-88` 只声明 include path 与模块依赖，没有额外 runtime payload 配置。这说明它的核心交付物不是“需要 staged 的并行脚本目录”，而是“最终应折叠回 native module / binary 的代码生成产物”。这里需要明确标注为推断：源码直证是“生成 C++ 到 `AS_JITTED_CODE` + Build.cs 未声明额外 runtime payload”；基于这两点可以推断 Angelscript 的部署主要复用 UE 原生模块打包链，而不是另带一个像 C# DLL 目录那样的运行时内容根。

```
[D11-Deep] Deployment Artifact Ownership

[UnrealCSharp]
dotnet build
└─ Game.props AfterBuildPublish
   ├─ Publish managed assemblies
   ├─ Copy *.dll -> Content/<PublishDirectory>
   ├─ UpdatePackagingSettings -> StageAsUFS
   └─ cook commandlet reruns Generator

[Angelscript]
StaticJIT pipeline
├─ Generate C++ -> <Root>/AS_JITTED_CODE
├─ fold output back into native build
└─ package via normal UE binary/content flow
```

关键源码 [1] `Reference/UnrealCSharp/Template/Game.props:16-27`

```xml
<!-- =========================================================================
文件: Reference/UnrealCSharp/Template/Game.props
位置: 16-27，build 之后自动 publish 并复制 DLL
=========================================================================== -->
<Target Name="AfterBuildPublish" AfterTargets="Build">
    <MSBuild Projects="$(ProjectPath)" Targets="Publish" Properties="Configuration=$(Configuration)" />
    <!-- ★ 这里意味着 compile worker 只发起 dotnet build，也会被项目模板接成 publish -->
</Target>

<Target Name="CopyDllsAfterPublish" AfterTargets="Publish">
    <ItemGroup>
        <PublishFiles Include="$(PublishDir)*.dll" />
    </ItemGroup>
    <MakeDir Directories="$(ScriptOutputPath)" Condition="!Exists('$(ScriptOutputPath)')" />
    <Copy SourceFiles="@(PublishFiles)" DestinationFolder="$(ScriptOutputPath)" />
    <!-- ★ 最终交付物是 DLL 目录，而不是仅仅一份中间编译缓存 -->
</Target>
```

关键源码 [2] `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FSolutionGenerator.cpp:196-229` 与 `Reference/UnrealCSharp/Source/Compiler/Private/FCSharpCompilerRunnable.cpp:235-257`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FSolutionGenerator.cpp
// 函数: FSolutionGenerator::ReplaceDefineConstants / ReplaceOutputPath / ReplaceTargetFramework
// 位置: 196-229，cook 态剥离 WITH_EDITOR，并把 publish 根固定到 Content 目录
// ============================================================================
if (!IsRunningCookCommandlet())
{
    DefineConstants += TEXT("WITH_EDITOR");
}
// ★ cook 时不把 editor-only 宏带进脚本工程

OutResult = OutResult.Replace(TEXT("<ScriptOutputPath></ScriptOutputPath>"),
    *FString::Printf(TEXT("<ScriptOutputPath>..\\..\\Content\\%s</ScriptOutputPath>"),
                     *FUnrealCSharpFunctionLibrary::GetPublishDirectory()));
// ★ 发布目录明确落在 Content/<PublishDirectory>

OutResult = OutResult.Replace(TEXT("<TargetFramework></TargetFramework>"),
    *FString::Printf(TEXT("<TargetFramework>net%d.%d</TargetFramework>"),
                     FUnrealCSharpFunctionLibrary::GetDotnetVersion(),
                     DOTNET_MINOR_VERSION));
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/Compiler/Private/FCSharpCompilerRunnable.cpp
// 函数: Compile
// 位置: 235-257，cook commandlet 下切换 runtime configuration
// ============================================================================
auto GetSolutionConfiguration = []()
{
    if (const auto UnrealCSharpEditorSetting = FUnrealCSharpFunctionLibrary::GetMutableDefaultSafe<
        UUnrealCSharpEditorSetting>())
    {
        return IsRunningCookCommandlet()
                   ? UnrealCSharpEditorSetting->GetRuntimeConfiguration()
                   : UnrealCSharpEditorSetting->GetEditorConfiguration();
    }

    return ESolutionConfiguration::Debug;
};

const auto CompileParam = FString::Printf(TEXT("build \"%s\" --nologo -c %s"),
    *FUnrealCSharpFunctionLibrary::GetGameProjectPath(),
    GetSolutionConfiguration() == ESolutionConfiguration::Debug ? TEXT("Debug") : TEXT("Release"));
// ★ 看似只有 build，但结合 Game.props，实际已经把 publish 也编进去了
```

关键源码 [3] `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:102-124,209-235`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp
// 函数: StartupModule / UpdatePackagingSettings
// 位置: 102-124, 209-235，自动把 publish 目录加入 UFS staging，并在 cook 时重跑生成
// ============================================================================
UpdatePackagingSettings();

if (IsRunningCookCommandlet())
{
    if (const auto AssetRegistryModule = FModuleManager::LoadModulePtr<FAssetRegistryModule>(TEXT("AssetRegistry")))
    {
        AssetRegistryModule->Get().OnFilesLoaded().AddLambda([]()
        {
            Generator();
        });
    }
}
// ★ cook 不是只做复制，而是等资产可见后再次生成脚本工程产物

for (const auto& [Path] : ProjectPackagingSettings->DirectoriesToAlwaysStageAsUFS)
{
    if (Path == PublishDirectory)
    {
        bIsExisted = true;
        break;
    }
}

if (!bIsExisted)
{
    ProjectPackagingSettings->DirectoriesToAlwaysStageAsUFS.Add({PublishDirectory});
    ProjectPackagingSettings->TryUpdateDefaultConfigFile();
}
// ★ managed payload 被显式纳入 UE 打包配置
```

关键源码 [4] `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp:3499-3608`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp
// 函数: FAngelscriptStaticJIT::WriteOutputCode
// 位置: 3499-3608，StaticJIT 交付物是项目根目录下的 C++ 代码
// ============================================================================
FString GenDir = FPaths::RootDir() / TEXT("AS_JITTED_CODE");
auto& FileManager = IFileManager::Get();
FileManager.MakeDirectory(*GenDir, true);
// ★ 产物根在项目根目录，不是 Content/UFS 目录

for (auto& HeaderElem : SharedHeaders)
{
    FString FullFilename = GenDir / Header.Filename;
    ...
    FFileHelper::SaveStringToFile(FullContent, *FullFilename);
}

for (auto ModuleElem : JITFiles)
{
    FJITFile& File = *ModuleElem.Value;
    FString FullFilename = GenDir / File.Filename;
    FString FullContent = STANDARD_HEADER;
    ...
    FullContent += STANDARD_INCLUDES;
    ...
}
// ★ 输出物是 shared header + module cpp，天然属于 native build 输入
```

关键源码 [5] `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs:1-88`

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs
// 函数: AngelscriptRuntime(ReadOnlyTargetRules Target)
// 位置: 1-88，模块规则只声明 include/dependency，不声明额外 runtime payload
// ============================================================================
var AngelscriptThirdPartyPath = Path.Combine(ModuleDirectory, "ThirdParty", "angelscript");
PublicIncludePaths.Add(Path.Combine(AngelscriptThirdPartyPath, "source"));
PublicIncludePaths.Add(AngelscriptThirdPartyPath);

PublicDependencyModuleNames.AddRange(new string[]
{
    "ApplicationCore",
    "Core",
    "CoreUObject",
    "Engine",
    "EngineSettings",
    "DeveloperSettings",
    "Json",
    "JsonUtilities",
    "GameplayTags",
    "StructUtils",
});

PrivateDependencyModuleNames.AddRange(new string[]
{
    "AIModule",
    "NavigationSystem",
    "NetCore",
    "Landscape",
    "Networking",
    "Sockets",
    "InputCore",
    "SlateCore",
    "Slate",
    "UMG",
    "TraceLog",
    "AssetRegistry",
    "Projects",
    "PhysicsCore",
    "CoreOnline",
    "EnhancedInput",
    "GameplayAbilities",
    "GameplayTasks",
});
// ★ 这里能直证模块依赖很多，但未见与“额外脚本目录 staging”对等的 payload 规则
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 交付物形态 | UnrealCSharp 交付的是 `Content/<PublishDirectory>` 下的 managed DLL 集 | 实现方式不同 |
| 打包接入点 | UnrealCSharp 源码里存在 `build -> publish -> copy -> StageAsUFS -> cook regenerate` 的完整闭环 | 实现质量差异 |
| StaticJIT 产物归属 | Angelscript 的 StaticJIT 产物是 `AS_JITTED_CODE/` 下的 C++ 输入文件 | 实现方式不同 |
| 额外 runtime payload 目录 | 从 `AngelscriptRuntime.Build.cs` 可直证其未声明与 C# DLL 目录对等的 payload 规则，因此更可能复用 native module 打包链 | 实现方式不同 |

这一维最重要的结论不是“谁更容易发版”，而是“交付物所有权在哪一层”。UnrealCSharp 必须把 managed payload 当成一等 runtime 内容来 stage；Angelscript 则尽量把脚本发布问题折叠回原生编译链。只要所有权不同，热更新、平台适配、签名/加密策略、崩溃定位方式就都会跟着不同。

---

## 深化分析 (2026-04-08 19:23:06)

本轮不再补前文已经展开过的生成链、Blueprint wrapper 或 staged assembly set，而是补三条之前还没有单独拆开的源码 contract：`D1` 看第三方运行时接入缝到底落在什么层，`D2` 看 override patch 的回滚所有权到底归谁，`D6` 看源码定位信息到底挂在离线映射里还是挂在运行时生成对象上。

### [维度 D1] 第三方运行时接入缝：configurable AssemblyLoader vs embedded VM source

前文 D1 已经说明 UnrealCSharp 通过 `Mono` 模块接入 CLR。本轮继续往下看“接入缝”本身，可以看到它不是简单把 Mono 当黑盒链接进去，而是把“程序集如何被找到”单独抽成了一个可配置的 UE `UClass`。`Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpSetting.h:140-141` 把 `AssemblyLoader` 暴露成 `TSubclassOf<UAssemblyLoader>`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp:755-757` 又在 domain 启动时显式安装 `mono_install_assembly_preload_hook`；真正读 DLL 字节的逻辑则落在 `UAssemblyLoader::Load()`。这意味着 UnrealCSharp 把“Mono runtime 嵌进 UE”这件事分成了两个层面：Mono 本体由 `Mono` 模块承载，程序集发现/解包由插件自己的 loader seam 决定。

Angelscript 的接入缝位置则完全不同。`Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs:20-22` 直接把 `ThirdParty/angelscript/source` 暴露进 Runtime include path；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:364-376` 里脚本引擎对象也由 `FAngelscriptEngine` 自己 `ShutDownAndRelease()`。也就是说，Angelscript 的 VM 接入点在“模块直接内嵌第三方源码 + engine 自己持有 VM 生命周期”，而不是像 UnrealCSharp 那样再额外抽一个“按名称取 assembly bytes”层。

```
[D1-Deep] Runtime Integration Seam

[UnrealCSharp]
Build.cs
├─ depend on Mono module
├─ settings expose TSubclassOf<UAssemblyLoader>
└─ FMonoDomain::RegisterAssemblyPreloadHook
   └─ AssemblyPreloadHook(name)
      ├─ AssemblyLoader->Load(name)
      └─ LoadAssembly(bytes)

[Angelscript]
AngelscriptRuntime.Build.cs
├─ include ThirdParty/angelscript/source
└─ FAngelscriptEngine
   ├─ own ScriptEngine create/release
   └─ BindScriptTypes around embedded VM
```

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpSetting.h:140-141` 与 `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Setting/UnrealCSharpSetting.cpp:87-93`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpSetting.h
// 位置: 140-141，程序集加载器是可配置的 UClass
// ============================================================================
UPROPERTY(Config, EditAnywhere, Category = Domain)
TSubclassOf<UAssemblyLoader> AssemblyLoader;
// ★ 第三方 runtime 的“找 DLL”策略没有写死在 FMonoDomain 里，而是给了配置扩展点
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Setting/UnrealCSharpSetting.cpp
// 函数: UUnrealCSharpSetting::GetAssemblyLoader
// 位置: 87-93，取到的是 loader default object
// ============================================================================
UAssemblyLoader* UUnrealCSharpSetting::GetAssemblyLoader() const
{
	return Cast<UAssemblyLoader>((AssemblyLoader->IsValidLowLevelFast()
		                              ? AssemblyLoader.Get()
		                              : UAssemblyLoader::StaticClass())
		->GetDefaultObject());
}
// ★ domain 侧只依赖抽象 loader，不关心最终是普通文件、解密包还是别的来源
```

关键源码 [2] `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp:490-508,755-757` 与 `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/AssemblyLoader.cpp:6-23`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp
// 函数: FMonoDomain::AssemblyPreloadHook / RegisterAssemblyPreloadHook
// 位置: 490-508, 755-757，Mono 预加载回调转交给插件自己的 loader seam
// ============================================================================
MonoAssembly* FMonoDomain::AssemblyPreloadHook(MonoAssemblyName* InAssemblyName, char** OutAssemblyPath,
                                               void* InUserData)
{
	auto AssemblyName = FString(mono_assembly_name_get_name(InAssemblyName));
	...
	if (const auto AssemblyLoader = FUnrealCSharpFunctionLibrary::GetAssemblyLoader())
	{
		if (const auto Data = AssemblyLoader->Load(AssemblyName); !Data.IsEmpty())
		{
			MonoAssembly* Assembly = nullptr;
			LoadAssembly(AssemblyName, Data, nullptr, &Assembly);
			return Assembly;
		}
	}

	return nullptr;
}

void FMonoDomain::RegisterAssemblyPreloadHook()
{
	mono_install_assembly_preload_hook(AssemblyPreloadHook, nullptr);
}
// ★ Mono 缺失依赖时，不直接走默认文件探测，而是先回到 UnrealCSharp 自己的加载缝
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/AssemblyLoader.cpp
// 函数: UAssemblyLoader::Load
// 位置: 6-23，默认 loader 只是按搜索路径取 DLL 字节
// ============================================================================
TArray<uint8> UAssemblyLoader::Load(const FString& InAssemblyName)
{
	auto AssemblyPaths = FUnrealCSharpFunctionLibrary::GetAssemblyPath();

	for (const auto& AssemblyPath : AssemblyPaths)
	{
		if (const auto File = FPaths::Combine(AssemblyPath, InAssemblyName) + DLL_SUFFIX;
			IFileManager::Get().FileExists(*File))
		{
			TArray<uint8> Data;
			FFileHelper::LoadFileToArray(Data, *File);
			return Data;
		}
	}

	return {};
}
// ★ 默认实现很薄，因此“加密/压缩/远端拉取”这类策略理论上都能通过换 loader 实现
```

关键源码 [3] `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs:20-22` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:364-376`

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs
// 位置: 20-22，VM 源码直接编进 Runtime 模块
// ============================================================================
var AngelscriptThirdPartyPath = Path.Combine(ModuleDirectory, "ThirdParty", "angelscript");
PublicIncludePaths.Add(Path.Combine(AngelscriptThirdPartyPath, "source"));
PublicIncludePaths.Add(AngelscriptThirdPartyPath);
// ★ 这里没有“按名字回调取 VM 依赖字节”的中间层，ThirdParty 源码就是 Runtime 的一部分
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 364-376，VM 生命周期由 engine 自己持有与释放
// ============================================================================
if (SharedState->ScriptEngine != nullptr)
{
	ReleaseContextsForScriptEngine(GAngelscriptContextPool.FreeContexts, SharedState->ScriptEngine);
	SharedState->ScriptEngine->ShutDownAndRelease();
	SharedState->ScriptEngine = nullptr;
}

SharedState->TypeDatabase.Reset();
SharedState->BindState.Reset();
SharedState->ToStringList.Reset();
SharedState->BindDatabase.Reset();
// ★ VM 对象、bind state、bind DB 都是 engine 自己托管的资源，不经过额外 loader abstraction
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 第三方 runtime 接入缝 | UnrealCSharp 把“找 assembly 字节”抽成 `TSubclassOf<UAssemblyLoader> + preload hook` | 实现方式不同 |
| VM 源码集成位置 | Angelscript 直接把 `ThirdParty/angelscript/source` 暴露给 Runtime 模块 | 实现方式不同 |
| 生命周期所有权 | UnrealCSharp 的 domain 消费 bytes，Angelscript 的 engine 直接 create/release VM | 实现方式不同 |
| 可替换性推断 | 依据 `UAssemblyLoader` 设计，可推断 UnrealCSharp 更容易插入自定义 assembly 解包策略；Angelscript 若替换 VM，改动点会落到 Runtime/Build 层 | 实现质量差异 |

这里最后一行是带来源的推断，不是空泛判断。支撑它的源码事实只有两个：UnrealCSharp 存在显式 loader seam，Angelscript 当前 reference 版本把 VM 源码直接编进 Runtime。只要接入缝位置不同，后续做加密、热替换、沙箱或平台裁剪时的改动半径就会不同。

### [维度 D2] Override patch 生命周期：RAII rollback token vs engine-scope bind replay

前文已经写过 UnrealCSharp 如何把 `UFunction` 改成 `execCallCSharp`。本轮补的是“谁负责把这次 patch 撤回”。答案不是 GC，也不是某个全局 reload helper，而是一个跟单个函数绑定的 `FCSharpFunctionRegister`。`FCSharpBind::BindImplementation()` 在命中 override 时把原始 `UFunction`、复制出来的函数、原始 flags 和原始 native func 一起打包进 `FCSharpFunctionRegister`，再塞进 `FCSharpFunctionDescriptor`；`FClassRegistry::Deinitialize()` 删除 descriptor 时，这个 register 的析构函数会恢复 `FunctionFlags`、恢复 `NativeFunc`、从 `FunctionMap` 删除临时函数并清 root/garbage。也就是说，UnrealCSharp 的 patch 生命周期边界是“descriptor 存活期”，不是“整个 runtime 存活期”。

Angelscript 这一层更粗。`FAngelscriptBinds::RegisterBinds()` 只是把 bind lambda 追加进静态数组，`BindScriptTypes()` 再统一 `CallBinds()`；清理时能直接看到的是 `BindState.Reset()`、`BindDatabase.Reset()` 和 `DiscardModule()` 这种 engine/module 级别边界，而不是某个单独 `UFunction` 对应一个回滚 token。这里不应该写成“Angelscript 没有回滚”，更准确的说法是：它的回滚边界在模块或引擎级，而 UnrealCSharp 的回滚边界已经细化到单个被 patch 的 `UFunction`。

```
[D2-Deep] Override Lifecycle Boundary

[UnrealCSharp]
BindImplementation(UFunction)
├─ create FCSharpFunctionRegister
├─ store into FCSharpFunctionDescriptor
├─ cache descriptor in FClassRegistry
└─ registry deinit / remove descriptor
   └─ ~FCSharpFunctionRegister
      ├─ restore flags + NativeFunc
      ├─ RemoveFunctionFromFunctionMap
      └─ RemoveFromRoot or MarkAsGarbage

[Angelscript]
RegisterBinds(lambda)
├─ append to static BindArray
├─ Engine::BindScriptTypes -> CallBinds()
└─ engine shutdown / module discard
   ├─ BindState.Reset()
   ├─ BindDatabase.Reset()
   └─ recompile or rebind on next init
```

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FCSharpBind.cpp:329-340,364-386`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FCSharpBind.cpp
// 函数: FCSharpBind::BindImplementation
// 位置: 329-340, 364-386，patch 时同时创建回滚 token
// ============================================================================
const auto OverrideFunction = DuplicateFunction(OriginalFunction, InClass, *NewFunctionName);

FCSharpEnvironment::GetEnvironment().AddFunctionHash<FCSharpFunctionDescriptor>(
	FunctionHash, InClassDescriptor, OriginalFunction,
	FCSharpFunctionRegister(OriginalFunction, OverrideFunction,
	                        OriginalFunction->FunctionFlags, OriginalFunction->GetNativeFunc()));

OriginalFunction->SetNativeFunc(UCSharpFunction::execCallCSharp);
OriginalFunction->FunctionFlags |= FUNC_Native;
...
NewFunction = DuplicateFunction(OriginalFunction, InClass, FunctionName);

FCSharpEnvironment::GetEnvironment().AddFunctionHash<FCSharpFunctionDescriptor>(
	FunctionHash, InClassDescriptor, NewFunction, FCSharpFunctionRegister(NewFunction, OriginalFunction));

NewFunction->SetNativeFunc(UCSharpFunction::execCallCSharp);
NewFunction->FunctionFlags |= FUNC_Native;
// ★ patch 与 rollback 元数据在同一个调用点被同时建立，不是事后补一份清理表
```

关键源码 [2] `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FCSharpFunctionRegister.cpp:29-67`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FCSharpFunctionRegister.cpp
// 函数: FCSharpFunctionRegister::~FCSharpFunctionRegister
// 位置: 29-67，析构时恢复原始函数并移除临时函数
// ============================================================================
FCSharpFunctionRegister::~FCSharpFunctionRegister()
{
	const auto InOriginalFunction = OriginalFunction.Get(true);
	const auto InCallCSharpFunction = Function.Get(true);

	if (InOriginalFunction != nullptr && InCallCSharpFunction != nullptr)
	{
		UFunction* FunctionRemove;

		if (InOriginalFunction->GetOuter() == InCallCSharpFunction->GetOuter())
		{
			InCallCSharpFunction->FunctionFlags = OriginalFunctionFlags;
			InCallCSharpFunction->SetNativeFunc(OriginalNativeFuncPtr);
			FunctionRemove = InOriginalFunction;
		}
		else
		{
			FunctionRemove = InCallCSharpFunction;
		}

		if (const auto Class = Cast<UClass>(FunctionRemove->GetOuter()))
		{
			Class->RemoveFunctionFromFunctionMap(FunctionRemove);
		}

		if (FunctionRemove->IsRooted())
		{
			FunctionRemove->RemoveFromRoot();
		}
		else
		{
			FunctionRemove->MarkAsGarbage();
		}
	}
}
// ★ 这里能直证 patch 不是“一写到底”，而是有明确 restore path
```

关键源码 [3] `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FClassRegistry.cpp:67-74` 与 `Reference/UnrealCSharp/Source/UnrealCSharp/Public/Reflection/Function/FCSharpFunctionDescriptor.h:24`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FClassRegistry.cpp
// 函数: FClassRegistry::Deinitialize
// 位置: 67-74，descriptor 的销毁就是 rollback 触发点
// ============================================================================
for (auto& [Key, Value] : FunctionDescriptorMap)
{
	delete Value;
	Value = nullptr;
}

FunctionDescriptorMap.Empty();
// ★ descriptor 被 delete 后，其内部成员 FCSharpFunctionRegister 也会跟着析构
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Public/Reflection/Function/FCSharpFunctionDescriptor.h
// 位置: 24，register token 是 descriptor 的成员
// ============================================================================
FCSharpFunctionRegister FunctionRegister;
// ★ 生命周期不是散落在全局 map，而是和 function descriptor 强绑定
```

关键源码 [4] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:151-158,186-214`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:364-376,1026-1056,1915-1922`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp
// 位置: 151-158, 186-214，bind 生命周期以静态数组 + replay 为中心
// ============================================================================
void FAngelscriptBinds::RegisterBinds(FName BindName, int32 BindOrder, TFunction<void()> Function)
{
	GetBindArray().Add({BindName.IsNone() ? MakeUnnamedBindName() : BindName, BindOrder, MoveTemp(Function)});
}

void FAngelscriptBinds::ResetBindState()
{
	GetBindState() = FAngelscriptBindState();
}

void FAngelscriptBinds::CallBinds(const TSet<FName>& DisabledBindNames)
{
	for (const FBindFunction& Bind : GetSortedBindArray())
	{
		...
		Bind.Function();
	}
}
// ★ 这里看到的是“注册一批 bind，再统一 replay”，没有与单个 UFunction 对应的 rollback token
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 364-376, 1026-1056, 1915-1922，清理边界落在 engine/module 级
// ============================================================================
if (SharedState->ScriptEngine != nullptr)
{
	ReleaseContextsForScriptEngine(GAngelscriptContextPool.FreeContexts, SharedState->ScriptEngine);
	SharedState->ScriptEngine->ShutDownAndRelease();
	SharedState->ScriptEngine = nullptr;
}

SharedState->TypeDatabase.Reset();
SharedState->BindState.Reset();
SharedState->BindDatabase.Reset();
...
bool FAngelscriptEngine::DiscardModule(const TCHAR* ModuleName)
{
	...
	int r = Engine->DiscardModule(AnsiName.Get());
	...
	if (UASClass* ScriptClass = Cast<UASClass>(Class->Class))
	{
		ScriptClass->ScriptTypePtr = nullptr;
	}
}
...
void FAngelscriptEngine::BindScriptTypes()
{
	FAngelscriptBinds::CallBinds(CollectDisabledBindNames());
}
// ★ 热重载/销毁时看到的是 module discard 与 shared state reset，不是 per-function restore
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| patch 回滚粒度 | UnrealCSharp 把 rollback token 绑到 `FCSharpFunctionDescriptor`，边界细到单个 `UFunction` | 实现质量差异 |
| rollback 触发点 | UnrealCSharp 在 registry delete descriptor 时恢复 flags/native func 并删掉临时函数 | 不是没有实现 |
| bind 生命周期主轴 | Angelscript 以 `RegisterBinds -> CallBinds -> Reset/Discard` 的 engine/module 级 replay 为主 | 实现方式不同 |
| 热重载清理边界 | Angelscript 的源码直证边界在 `BindState.Reset()`、`BindDatabase.Reset()` 与 `DiscardModule()` | 实现方式不同 |

这一维最重要的差异不是“谁能热重载”，而是“清理谁负责、清到多细”。UnrealCSharp 让每个被 patch 的 `UFunction` 自带回滚凭据；Angelscript 则把绑定与脚本类更新都收敛到更粗粒度的 engine/module 生命周期里。

### [维度 D6] 源码定位元数据归属：offline file map vs generated object metadata

上一轮 D6 已经比较过 IDE contract 与 source navigation 入口。本轮只追一个更底层的问题：`file:line` 元数据到底存在什么地方。UnrealCSharp 这边，`BlueprintToolBar` 和 `FDynamicGenerator` 都不是从 `UClass/UFunction` 本体上取源码位置，而是先加载 `OverrideFile.json` / `DynamicFile.json`，再用 `Namespace.Class` 去查文件路径，必要时再把文件反查回 `FClassReflection`。也就是说，源码位置是离线分析产物的一部分，运行时生成对象本身并不携带对等的 source-location contract。

Angelscript 则相反。`UASClass::GetSourceFilePath()` 直接从所属 module 的 `Code[0].AbsoluteFilename` 取路径，`UASFunction::GetSourceLineNumber()` 直接从 `scriptData->declaredAt` 取行号；`FAngelscriptSourceCodeNavigation` 只要拿到 `UASClass/UASFunction` 就能 `code --goto`。更重要的是，这条 contract 还有自动化测试，`AngelscriptSourceNavigationTests.cpp` 会断言生成出来的 `UASFunction` 保留了源文件路径和行号。

```
[D6-Deep] Source Location Ownership

[UnrealCSharp]
Roslyn CodeAnalysis
├─ OverrideFile.json
├─ DynamicFile.json
├─ Toolbar: class -> file
│  └─ Open File / Code Analysis
└─ DynamicGenerator: file -> class
   └─ reverse lookup through JSON map

[Angelscript]
Generated UASClass / UASFunction
├─ keep module pointer / ScriptFunction
├─ GetSourceFilePath()
├─ GetSourceLineNumber()
└─ SourceCodeNavigation
   └─ code --goto file:line
```

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:206-248` 与 `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGenerator.cpp:95-118,191-203`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp
// 位置: 206-248，Blueprint 工具栏通过 JSON sidecar 找到 override 文件
// ============================================================================
CodeAnalysisOverrideFilesMap = FUnrealCSharpFunctionLibrary::LoadFileToString(FString::Printf(TEXT(
	"%s/%s.json"
),
	*FUnrealCSharpFunctionLibrary::GetCodeAnalysisPath(),
	*OVERRIDE_FILE
));
...
const auto Class = FString::Printf(TEXT(
	"%s.%s"
),
	*FUnrealCSharpFunctionLibrary::GetClassNameSpace(Blueprint->GeneratedClass),
	*FUnrealCSharpFunctionLibrary::GetFullClass(Blueprint->GeneratedClass));

if (const auto FoundOverrideFile = CodeAnalysisOverrideFilesMap.Find(Class))
{
	return *FoundOverrideFile;
}

if (const auto FoundOverrideFile = DynamicOverrideFilesMap.Find(Class))
{
	return *FoundOverrideFile;
}
// ★ “类 -> 文件”关系来自离线 JSON 映射，不是 BlueprintGeneratedClass/UFunction 自带字段
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGenerator.cpp
// 位置: 95-118, 191-203，增量路径同样依赖 JSON sidecar 做 file <-> class 反查
// ============================================================================
void FDynamicGenerator::SetCodeAnalysisDynamicFilesMap()
{
	CodeAnalysisDynamicFilesMap = FUnrealCSharpFunctionLibrary::LoadFileToString(FString::Printf(TEXT(
		"%s/%s.json"
	),
		*FUnrealCSharpFunctionLibrary::GetCodeAnalysisPath(),
		*DYNAMIC_FILE
	));
}

FString FDynamicGenerator::GetDynamicFile(const FString& InName)
{
	const auto FoundDynamicFile = CodeAnalysisDynamicFilesMap.Find(InName);
	return FoundDynamicFile != nullptr ? *FoundDynamicFile : FString{};
}

EDynamicType FDynamicGenerator::GetDynamicType(const FString& InFile, FClassReflection*& OutClass)
{
	for (auto const& [Name, File] : CodeAnalysisDynamicFilesMap)
	{
		if (FPaths::IsSamePath(File, InFile))
		{
			if (auto Index = 0; Name.FindLastChar(TEXT('.'), Index))
			{
				OutClass = FReflectionRegistry::Get().GetClass(
					Name.Left(Index), Name.Right(Name.Len() - Index - 1));
				return FDynamicGeneratorCore::GetDynamicType(Name);
			}
		}
	}
}
// ★ file -> class 的反向解析同样走 sidecar，而不是从生成 UClass 上拿 SourceFilePath
```

关键源码 [2] `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h:117-125,192-193` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:1497-1558`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h
// 位置: 117-125, 192-193，generated function/class 自带 source-location API
// ============================================================================
UCLASS()
class ANGELSCRIPTRUNTIME_API UASFunction : public UFunction
{
	GENERATED_BODY()
public:
	class asIScriptFunction* ScriptFunction = nullptr;
	int32 GeneratedSourceLineNumber = -1;
	...
	FString GetSourceFilePath() const;
	int GetSourceLineNumber() const;
};
// ★ source-location contract 直接挂在生成出来的 UFunction 类型上
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp
// 函数: UASClass::GetSourceFilePath / UASFunction::GetSourceFilePath / GetSourceLineNumber
// 位置: 1497-1558，路径和行号直接来自脚本 module / scriptData
// ============================================================================
FString UASClass::GetSourceFilePath() const
{
	...
	return Module->Code[0].AbsoluteFilename;
}

FString UASFunction::GetSourceFilePath() const
{
	...
	return Module->Code[0].AbsoluteFilename;
}

int UASFunction::GetSourceLineNumber() const
{
	...
	return (scriptData->declaredAt & 0xFFFFF) + 1;
}
// ★ class/function 不需要额外 JSON sidecar，就能回答“我来自哪个文件第几行”
```

关键源码 [3] `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp:15-44,95-138` 与 `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSourceNavigationTests.cpp:69-79`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp
// 位置: 15-44, 95-138，导航层直接消费 generated object 上的位置信息
// ============================================================================
virtual bool NavigateToFunction(const UFunction* InFunction) override
{
	auto* ASFunc = Cast<const UASFunction>(InFunction);
	if (ASFunc == nullptr)
		return false;

	FString Path = ASFunc->GetSourceFilePath();
	if (Path.Len() == 0)
		return false;

	OpenFile(Path, ASFunc->GetSourceLineNumber());
	return true;
}

void RegisterAngelscriptSourceNavigation()
{
	FSourceCodeNavigation::AddNavigationHandler(new FAngelscriptSourceCodeNavigation);
}
// ★ 导航 handler 只消费对象元数据，不需要先去查一个 class→file JSON
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSourceNavigationTests.cpp
// 位置: 69-79，source-location contract 有自动化测试兜底
// ============================================================================
UASFunction* RuntimeASFunction = Cast<UASFunction>(RuntimeFunction);
...
TestEqual(TEXT("Generated function should preserve source file path"), RuntimeASFunction->GetSourceFilePath(), ScriptPath);
TestEqual(TEXT("Generated function should preserve source line number"), RuntimeASFunction->GetSourceLineNumber(), 6);
TestTrue(TEXT("Source navigation should recognize generated script class"), FSourceCodeNavigation::CanNavigateToClass(RuntimeClass));
TestTrue(TEXT("Source navigation should recognize generated script function"), FSourceCodeNavigation::CanNavigateToFunction(RuntimeFunction));
// ★ 这里不是“能打开编辑器”层面的 UI 测试，而是直接验证 generated UASFunction 保留了 path/line contract
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 源码定位元数据归属 | UnrealCSharp 的 `class <-> file` 关系落在 `OverrideFile.json` / `DynamicFile.json` sidecar | 实现方式不同 |
| 反查方向 | UnrealCSharp 同时实现了 `class -> file` 与 `file -> class`，但都依赖离线分析映射 | 实现方式不同 |
| 运行时对象自描述能力 | Angelscript 的 `UASClass/UASFunction` 可直接回答 `GetSourceFilePath/GetSourceLineNumber` | 实现质量差异 |
| 自动化验证 | Angelscript 对 source navigation contract 有 editor automation test；本轮未在 `Reference/UnrealCSharp` 中定位到对等测试 | 实现质量差异 |

这一维不能简单归纳成“谁导航更方便”。源码能直证的更深差别是：UnrealCSharp 把源码位置当成 analyzer sidecar；Angelscript 把源码位置当成 generated runtime object 的一部分。前者更适合把 IDE 行为和 Roslyn 结果对齐，后者更适合让 UE 里的任意系统在拿到 `UASFunction/UASClass` 后直接做定位、调试或错误回报。

---

## 深化分析 (2026-04-08 19:38:09)

本轮不再重复前文已经展开过的 reinstance、source navigation 或 staged payload，而是补三条更靠“边界条件”的 contract：`D3` 看 Blueprint 混合继承链在“创建前”就被怎样限制或验证，`D8` 看 GC 清理到底在哪个线程/层级落地，`D11` 看发布态如何确认脚本产物与当前二进制仍然匹配。

### [维度 D3] 混合继承链准入点：editor-time admission gate vs compiler-enforced BlueprintOverride

前文 D3 已经写过运行时 dispatch 和 Blueprint 子类 reinstance。本轮只补“这条继承链在进入运行时之前，谁先拦、拦在哪里”。UnrealCSharp 的答案是“编辑器入口先拦一层，真正的 override 绑定再在 runtime 里按反射结果去配对”。`SDynamicNewClassDialog` 明确禁止“dynamic blueprint class 选择 dynamic class 作为父类”；`UnrealCSharpBlueprintToolBar` 则把 Blueprint override 入口做成一个编辑器按钮，按 `AActor / UActorComponent / UUserWidget / UObject` 四种模板生成 `.cs` 文件，再把 `Namespace.Class -> 文件路径` 塞进 `DynamicOverrideFilesMap`。直到运行时 `FCSharpBind::Bind()`，才会把 `FUNC_BlueprintEvent` 的 `UFunction` 和 managed 侧 `Method->IsOverride()` 的方法按编码名与参数个数做最后匹配。

Angelscript 的准入点更靠前。`BlueprintOverride` 不是编辑器模板动作，而是 preprocessor 识别的函数说明符。`AngelscriptPreprocessor.cpp` 直接禁止 global/static `BlueprintOverride`，也禁止它与 `BlueprintEvent`/网络 specifier 混用；`AngelscriptClassGenerator.cpp` 再继续验证“父类里是否真有这个事件、是不是 BlueprintEvent、签名是否一致、editor-only 约束是否一致”。更关键的是，这个 contract 有自动化测试直证：`AngelscriptLearningScriptClassToBlueprintTraceTests.cpp` 会先编译带 `UFUNCTION(BlueprintOverride)` 的脚本父类，再创建 Blueprint 子类、编译它、spawn 实例，并最终通过 `BeginPlay` 验证脚本 override 真的沿着 `ScriptClass -> BlueprintChild` 继承链生效。

换句话说，UnrealCSharp 把 Blueprint 混合继承的“入口便利性”放在 Editor 工具栏和模板脚手架上；Angelscript 把“入口合法性”放在编译前端，并且允许 Blueprint 直接继承生成出来的脚本类。

```
[D3-Deep] Mixed Inheritance Admission Point

[UnrealCSharp]
Blueprint authoring
├─ SDynamicNewClassDialog                     // 先限制父类选择
│  └─ forbid dynamic BP -> dynamic class
├─ Blueprint toolbar                          // 再提供 Override Blueprint 菜单
│  ├─ choose Actor/Component/Widget/Object template
│  └─ emit .cs file + class->file map
└─ FCSharpBind runtime match
   ├─ scan FUNC_BlueprintEvent
   └─ match Method->IsOverride by name + param count

[Angelscript]
Script compile front-end
├─ Preprocessor parses BlueprintOverride      // 先做 specifier 合法性检查
├─ ClassGenerator validates super event       // 再验父类存在性/签名/editor-only
└─ Automation test
   ├─ compile script parent class
   ├─ create Blueprint child
   └─ ProcessEvent/BeginPlay proves chain works
```

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/NewClass/SDynamicNewClassDialog.cpp:894-900` 与 `Reference/UnrealCSharp/Script/UE/CoreUObject/OverrideAttribute.cs:5-8`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/NewClass/SDynamicNewClassDialog.cpp
// 位置: 894-900，创建阶段先禁止 dynamic blueprint 直接继承 dynamic class
// ============================================================================
if (NewClassTypeIndex == 1 && FDynamicClassGenerator::IsDynamicClass(ParentClass))
{
    bLastInputValidityCheckSuccessful = false;

    LastInputValidityErrorText = LOCTEXT("CreateDynamicBpClassFromDynamicClass",
                                         "The dynamic blueprint class cannot select dynamic class as parent class");

    return;
}
// ★ 这里拦截发生在“新建类对话框”，不是编译或运行时
```

```csharp
// ============================================================================
// 文件: Reference/UnrealCSharp/Script/UE/CoreUObject/OverrideAttribute.cs
// 位置: 5-8，override 标记本身是通用 Attribute，并不内建 Blueprint 语义
// ============================================================================
[AttributeUsage(AttributeTargets.Class | AttributeTargets.Method)]
public class OverrideAttribute : Attribute
{
}
// ★ 这只是“我是 override”的标记，Blueprint 合法性不在这里约束
```

关键源码 [2] `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:85-154,176-187` 与 `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FCSharpBind.cpp:234-301`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp
// 位置: 85-154, 176-187，Blueprint override 入口是编辑器菜单 + 模板脚手架
// ============================================================================
CommandList->MapAction(
    FUnrealCSharpEditorCommands::Get().OverrideBlueprint,
    FExecuteAction::CreateLambda([this]
    {
        ...
        static TArray<UClass*> TemplateClasses =
        {
            AActor::StaticClass(),
            UActorComponent::StaticClass(),
            UUserWidget::StaticClass(),
            UObject::StaticClass()
        };
        ...
        FFileHelper::LoadFileToString(Content, *Template);
        ...
        if (const auto FileName = GetFileName();
            FUnrealCSharpFunctionLibrary::SaveStringToFile(FileName, Content))
        {
            ...
            DynamicOverrideFilesMap.Add(Class, FileName);
        }
    }), FCanExecuteAction());
...
if (HasOverrideFile())
{
    MenuBuilder.AddMenuEntry(Commands.OpenFile, NAME_None, LOCTEXT("OpenFile", "Open File"));
    MenuBuilder.AddMenuEntry(Commands.CodeAnalysis, NAME_None, LOCTEXT("CodeAnalysis", "Code Analysis"));
}
else
{
    MenuBuilder.AddMenuEntry(Commands.OverrideBlueprint, NAME_None, LOCTEXT("OverrideBlueprint", "Override Blueprint"));
}
// ★ 先生成/打开 override 文件，再由后续编译和 runtime bind 接手
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FCSharpBind.cpp
// 位置: 234-301，真正决定“绑不绑得上”发生在 runtime 反射配对
// ============================================================================
for (TFieldIteratorExt<UFunction> It(FoundClass, EFieldIteratorFlags::IncludeSuper,
                                     EFieldIteratorFlags::ExcludeDeprecated,
                                     EFieldIteratorFlags::IncludeInterfaces); It; ++It)
{
    if (auto Function = *It)
    {
        if (Function->HasAnyFunctionFlags(FUNC_BlueprintEvent) &&
            !Function->HasAnyFunctionFlags(FUNC_Final))
        {
            Functions.Emplace(FUnrealCSharpFunctionLibrary::Encode(...), Function);
        }
    }
}

for (const auto& [Name, Method] : Class->GetMethods())
{
    if (Method != nullptr && Method->IsOverride())
    {
        Methods.Add(Name.Key, Method);
    }
}

for (const auto& [FunctionName, Function] : Functions)
{
    for (const auto& [MethodName, Method] : Methods)
    {
        if (FunctionName == MethodName)
        {
            ...
            if (MethodParamCount == FunctionParamCount)
            {
                Bind(NewClassDescriptor, FoundClass, MethodName, Function);
            }
        }
    }
}
// ★ Editor 只是在生成入口帮你起步，最终 override 仍要通过 runtime 名称/参数匹配
```

关键源码 [3] `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:1654-1680` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:732-828`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp
// 位置: 1654-1680，BlueprintOverride 在预处理期就是一等 specifier
// ============================================================================
else if (Spec.Name == PP_NAME_BlueprintOverride)
{
    if (FunctionDesc->bIsStatic)
    {
        MacroError(File, Macro, FString::Printf(TEXT("Global UFUNCTION() %s may not be BlueprintOverride."), *FunctionDesc->FunctionName));
        ...
    }

    if (FunctionDesc->bBlueprintEvent)
    {
        MacroError(File, Macro, FString::Printf(TEXT("UFUNCTION() %s cannot be both BlueprintEvent and BlueprintOverride."), *FunctionDesc->FunctionName));
        ...
    }

    FunctionDesc->bBlueprintEvent = true;
    FunctionDesc->bBlueprintOverride = true;
    FunctionDesc->ScriptFunctionName += TEXT("_Implementation");
}
// ★ 这里先把语法/语义不合法的 override 拦掉，再进入 class generator
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 位置: 732-828，继续验证父类事件存在性、签名和 editor-only 约束
// ============================================================================
if (FunctionDesc->bBlueprintOverride)
{
    ...
    if (!SuperFunctionDesc.IsValid())
    {
        FAngelscriptEngine::Get().ScriptCompileError(...,
            TEXT("BlueprintOverride method %s in class %s does not exist in superclass %s."));
        ClassData.ReloadReq = EReloadRequirement::Error;
    }
    else if (!SuperFunctionDesc->bBlueprintEvent && !SuperFunctionDesc->bBlueprintOverride)
    {
        FAngelscriptEngine::Get().ScriptCompileError(...,
            TEXT("BlueprintOverride method %s in class %s is not marked BlueprintEvent in superclass %s."));
        ClassData.ReloadReq = EReloadRequirement::Error;
    }
    else if (!SuperFunctionDesc->SignatureMatches(FunctionDesc))
    {
        FAngelscriptEngine::Get().ScriptCompileError(...,
            TEXT("BlueprintOverride method %s in class %s does not match signature of event declared in superclass %s."));
        ClassData.ReloadReq = EReloadRequirement::Error;
    }
    else if (SuperFunctionDesc->Meta.Contains(NAME_Meta_EditorOnly) && !FunctionDesc->Meta.Contains(NAME_Meta_EditorOnly))
    {
        FAngelscriptEngine::Get().ScriptCompileError(...,
            TEXT("BlueprintOverride method %s in class %s overrides an editor-only parent function, but is not in editor-only code."));
        ClassData.ReloadReq = EReloadRequirement::Error;
    }
}
// ★ 不是等运行时再“试试看能不能绑上”，而是先把 override contract 编译成硬错误
```

关键源码 [4] `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningScriptClassToBlueprintTraceTests.cpp:137-180,216-225`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningScriptClassToBlueprintTraceTests.cpp
// 位置: 137-180, 216-225，自动化测试直证 Blueprint 可以继承生成的脚本类
// ============================================================================
UFUNCTION(BlueprintOverride)
void BeginPlay()
{
    BeginPlayCount += 1;
}
...
Trace.AddStep(TEXT("CompileScriptClass"), TEXT("Compiled the script parent class with BlueprintOverride so Unreal reflection can generate a UClass that Blueprint can inherit from"));
...
Blueprint.BlueprintAsset = CreateTransientLearningBlueprintChild(*this, ScriptClass, TEXT("LearningScriptClassToBlueprintChild"));
...
Trace.AddStep(TEXT("CreateBlueprintChild"), TEXT("Created a transient Blueprint asset that inherits from the generated script class"));
...
BeginPlayActor(*Actor);
Trace.AddStep(TEXT("InvokeBeginPlay"), TEXT("Invoked BeginPlay on the spawned actor to trigger the script-defined BlueprintOverride"));
...
Trace.AddStep(TEXT("ReadScriptPropertyDefaults"), bReadBeginPlayCount && bReadActorLabel ? TEXT("Read reflected properties from the Blueprint actor instance to show that script defaults propagated through the Blueprint inheritance chain") : TEXT("Failed to read one or more reflected properties from the Blueprint actor"));
// ★ 这里不是“注释说支持”，而是把 ScriptParent -> BlueprintChild -> Actor instance 整条链跑了一遍
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 准入点位置 | UnrealCSharp 的 override 入口先落在 Editor 菜单与模板脚手架，最终运行时再配对；Angelscript 先在 preprocessor/class generator 把 contract 验死 | 实现方式不同 |
| 混合继承链限制 | UnrealCSharp 明确禁止 `dynamic blueprint class -> dynamic class` 这种父类选择 | 不是没有实现，而是入口被刻意收窄 |
| Blueprint 继承脚本类 | Angelscript 有自动化测试直证 Blueprint 可直接继承生成的脚本类并触发 `BlueprintOverride` | 实现质量差异 |
| 错误暴露时机 | UnrealCSharp 更晚，更多依赖 runtime bind 命中；Angelscript 更早，在编译期就给出精确错误 | 实现质量差异 |

这里最关键的差异不是“谁更灵活”，而是“失败什么时候暴露”。UnrealCSharp 把混合继承和 override 的很多成本放在 editor tooling 与 runtime bind；Angelscript 把成本前移到脚本编译前端，因此 Blueprint 链一旦能通过编译，后续行为边界通常更稳定。

### [维度 D8] 清理落点：finalizer-to-GameThread hop vs schema-owned GC walk

前文 D8 已经写过 `handle 注入 vs schema 发射`。本轮只补“真正释放对象时，清理动作在哪一层执行”。UnrealCSharp 这里最有信息量的不是 `GarbageCollectionHandle` 本身，而是它的 teardown 路径。`UnrealTypeWeaver::ProcessStructRegister()` 会给 struct 注入 `Finalize()`，在析构路径里调用 `UStruct_UnRegisterImplementation`；native 侧 `FRegisterStruct::UnRegisterImplementation()` 和 `FRegisterWeakObjectPtr::UnRegisterImplementation()` 又统一把删除动作包进 `AsyncTask(ENamedThreads::GameThread, ...)`；最终 `FStructRegistry::RemoveReference()` / `FObjectRegistry::RemoveReference()` 里才真正 `Free<false>`、解除引用关系并在需要时释放 struct 内存。源码没有直接写“finalizer 当前一定在非 game thread”，但既然注销路径显式强切回 game thread，可以推断设计者并不假定回调现场线程安全。

Angelscript 的释放路径更像“编译期声明、GC 期执行”。`FAngelscriptType` 把 `EmitReferenceInfo` 和 `NeverRequiresGC` 作为 type-level contract；`Bind_UStruct.cpp` 直接把 struct 的引用关系编进 `UE::GC::FSchemaBuilder`，而 `Bind_WorldCollision.cpp` 这类 POD/trivial 类型甚至可以直接声明 `NeverRequiresGC = true`。这意味着 Angelscript 不需要为每个脚本 wrapper 再走一遍 “managed finalizer -> native unregister -> registry free” 的回流路径；很多类型在 UE GC 看来要么是可直接遍历的 schema，要么压根不进入 GC 关注面。

```
[D8-Deep] Teardown Execution Venue

[UnrealCSharp]
Managed wrapper lifetime
├─ IGarbageCollectionHandle / injected Finalize()   // wrapper 自己持有 handle 与析构入口
├─ UStruct_UnRegister / WeakObjectPtr.UnRegister     // 托管侧触发 native 注销
├─ AsyncTask(GameThread)                             // 真正释放前显式切回 game thread
└─ Registry::RemoveReference
   ├─ Free GCHandle
   ├─ remove owner/reference relation
   └─ free struct memory if needed

[Angelscript]
Type contract lifetime
├─ NeverRequiresGC()                                // POD/trivial 类型直接跳过 GC 关注
├─ EmitReferenceInfo(...)                           // 需要 GC 的类型把 schema 一次性发给 UE
└─ UE GC walk
   ├─ follows emitted schema
   └─ no per-wrapper unregister callback required
```

关键源码 [1] `Reference/UnrealCSharp/Script/UE/CoreUObject/IGarbageCollectionHandle.cs:1-6` 与 `Reference/UnrealCSharp/Script/Weavers/UnrealTypeWeaver.cs:112-177`

```csharp
// ============================================================================
// 文件: Reference/UnrealCSharp/Script/UE/CoreUObject/IGarbageCollectionHandle.cs
// 位置: 1-6，所有参与这套模型的包装类型都共享 handle contract
// ============================================================================
public interface IGarbageCollectionHandle
{
    public nint GarbageCollectionHandle { get; set; }
}
// ★ wrapper 自己携带 native identity，也因此能在析构期主动发起 UnRegister
```

```csharp
// ============================================================================
// 文件: Reference/UnrealCSharp/Script/Weavers/UnrealTypeWeaver.cs
// 函数: ProcessStructRegister
// 位置: 112-177，weaver 在 IL 层给 struct 注入 Finalize -> UnRegister
// ============================================================================
ilProcessor.InsertAfter(constructor.Body.Instructions[3],
    Instruction.Create(OpCodes.Call, ModuleDefinition.ImportReference(_structRegisterImplementation)));
...
var destructor = new MethodDefinition("Finalize",
    MethodAttributes.HideBySig | MethodAttributes.Family | MethodAttributes.Virtual,
    ModuleDefinition.TypeSystem.Void);
...
destructor.Body.Instructions.Add(Instruction.Create(OpCodes.Ldarg_0));
destructor.Body.Instructions.Add(Instruction.Create(OpCodes.Call,
    Type.Methods.FirstOrDefault(Method => Method.Name == "get_GarbageCollectionHandle")));
destructor.Body.Instructions.Add(Instruction.Create(OpCodes.Call,
    ModuleDefinition.ImportReference(_structUnRegisterImplementation)));
...
Type.Methods.Add(destructor);
// ★ 这说明 teardown 入口不是 UE GC schema，而是 wrapper 的 Finalize 路径
```

关键源码 [2] `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Domain/InternalCall/FRegisterStruct.cpp:48-53` 与 `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Domain/InternalCall/FRegisterWeakObjectPtr.cpp:37-43`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Domain/InternalCall/FRegisterStruct.cpp
// 位置: 48-53，struct 注销动作不在当前线程直接执行，而是切回 game thread
// ============================================================================
static void UnRegisterImplementation(const FGarbageCollectionHandle InGarbageCollectionHandle)
{
    AsyncTask(ENamedThreads::GameThread, [InGarbageCollectionHandle]
    {
        (void)FCSharpEnvironment::GetEnvironment().RemoveStructReference(InGarbageCollectionHandle);
    });
}
// ★ 这里能直证“真正的 native 清理必须回到 game thread”
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Domain/InternalCall/FRegisterWeakObjectPtr.cpp
// 位置: 37-43，弱引用容器也采用相同的回切策略
// ============================================================================
static void UnRegisterImplementation(const FGarbageCollectionHandle InGarbageCollectionHandle)
{
    AsyncTask(ENamedThreads::GameThread, [InGarbageCollectionHandle]
    {
        (void)FCSharpEnvironment::GetEnvironment().RemoveMultiReference<TWeakObjectPtr<UObject>>(
            InGarbageCollectionHandle);
    });
}
// ★ 这不是 struct 特例，而是跨多种 wrapper 的统一清理模式
```

关键源码 [3] `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FStructRegistry.cpp:108-141` 与 `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FObjectRegistry.cpp:98-116`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FStructRegistry.cpp
// 位置: 108-141，game thread 上真正完成 handle 释放与内存销毁
// ============================================================================
if (*FoundGarbageCollectionHandle == InGarbageCollectionHandle)
{
    FGarbageCollectionHandle::Free<false>(*FoundGarbageCollectionHandle);
    (void)FCSharpEnvironment::GetEnvironment().RemoveReference(InGarbageCollectionHandle);
    StructAddress2GarbageCollectionHandle.Remove({FoundValue->Value.Get(), FoundValue->Address});
}

if (FoundValue->bNeedFree)
{
    if (FoundValue->Value.IsValid())
    {
        if (!(FoundValue->Value->StructFlags & (STRUCT_IsPlainOldData | STRUCT_NoDestructor)))
        {
            FoundValue->Value->DestroyStruct(FoundValue->Address);
        }

        FMemory::Free(FoundValue->Address);
    }
}

GarbageCollectionHandle2StructAddress.Remove(InGarbageCollectionHandle);
// ★ 直到 registry remove 阶段，native 资源才真正解除与释放
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FObjectRegistry.cpp
// 位置: 98-116，UObject wrapper 清理同样走 handle free + reference unlink
// ============================================================================
if (const auto FoundValue = GarbageCollectionHandle2Object.Find(InGarbageCollectionHandle))
{
    if (const auto FoundGarbageCollectionHandle = Object2GarbageCollectionHandleMap.Find(*FoundValue))
    {
        if (*FoundGarbageCollectionHandle == InGarbageCollectionHandle)
        {
            FGarbageCollectionHandle::Free<false>(*FoundGarbageCollectionHandle);
            (void)FCSharpEnvironment::GetEnvironment().RemoveReference(*FoundGarbageCollectionHandle);
            Object2GarbageCollectionHandleMap.Remove(*FoundValue);
        }
    }

    GarbageCollectionHandle2Object.Remove(InGarbageCollectionHandle);
    return true;
}
// ★ UObject wrapper 没有显式 free 内存，但依然依赖 registry 做最后的 handle 解绑
```

关键源码 [4] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:166-182`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UStruct.cpp:149-167` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_WorldCollision.cpp:28-42`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h
// 位置: 166-182，GC 清理契约首先被建模在类型层
// ============================================================================
virtual bool HasReferences(const FAngelscriptTypeUsage& Usage) const { return false; }

struct FGCReferenceParams
{
    TArray<FName, TInlineAllocator<2>> Names;
    class UASClass* Class = nullptr;
    SIZE_T AtOffset;
    UE::GC::FSchemaBuilder* Schema;
    UE::GC::FPropertyStack* DebugPath;
};

virtual void EmitReferenceInfo(const FAngelscriptTypeUsage& Usage, FGCReferenceParams& Params) const {}
virtual bool NeverRequiresGC(const FAngelscriptTypeUsage& Usage) const { return false; }
// ★ 这里决定的是“GC 要不要看我、怎么看我”，不是“析构时我要不要回调一遍 native unregister”
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UStruct.cpp
// 位置: 149-167，需要 GC 的 struct 直接把引用信息发给 UE schema
// ============================================================================
void EmitReferenceInfo(const FAngelscriptTypeUsage& Usage, FGCReferenceParams& Params) const override
{
    UScriptStruct* UsedStruct = GetStruct(Usage);
    check(UsedStruct);

    if (!HasReferences(Usage))
        return;

    if (UsedStruct->StructFlags & STRUCT_AddStructReferencedObjects)
    {
        UE::GC::StructAROFn StructARO = UsedStruct->GetCppStructOps()->AddStructReferencedObjects();
        Params.Schema->Add(UE::GC::DeclareMember(Params.Names.Top(), Params.AtOffset, UE::GC::EMemberType::MemberARO, StructARO));
    }

    for (FProperty* Property = UsedStruct->PropertyLink; Property; Property = Property->PropertyLinkNext)
    {
        Property->EmitReferenceInfo(*Params.Schema, Params.AtOffset, EncounteredStructProps, *Params.DebugPath);
    }
}
// ★ GC 遍历信息一次性写进 schema，后续由 UE GC 按 schema 走
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_WorldCollision.cpp
// 位置: 28-42，trivial 类型甚至可以直接宣告自己永不需要 GC
// ============================================================================
struct FTraceHandleType : TAngelscriptCppType<FTraceHandle>
{
    ...
    bool NeverRequiresGC(const FAngelscriptTypeUsage& Usage) const override { return true; }
};
// ★ 这类类型连 schema 发射都不需要，更不存在 per-wrapper unregister
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 清理触发源 | UnrealCSharp 由 wrapper `Finalize()/UnRegister` 主动回流到 native；Angelscript 由类型 schema 交给 UE GC 统一处理 | 实现方式不同 |
| 线程边界 | UnrealCSharp 在多个 `UnRegisterImplementation` 上显式 `AsyncTask(GameThread)` | 实现质量差异 |
| 清理成本形态 | UnrealCSharp 释放时要经过 registry unlink + handle free；Angelscript 可在 `NeverRequiresGC` 上直接短路 | 实现方式不同 |
| 设计重心 | UnrealCSharp 优先保证跨 managed/native 线程安全；Angelscript 优先让 GC 图谱在编译期就固定 | 实现方式不同 |

这一维不能简单写成“谁 GC 更快”。源码更直接支持的结论是：UnrealCSharp 把 teardown 设计成一次跨语言、跨线程的回流过程；Angelscript 则尽量让 GC 在进入运行期之前就知道怎么走、甚至知道哪些类型根本不用走。

### [维度 D11] 交付兼容性封印：filename/path trust vs BuildIdentifier + DataGuid seal

前文 D11 已经讲过根目录与 staging。本轮只看“发布出来的脚本产物，运行时如何确认它仍然和当前可执行文件匹配”。UnrealCSharp 的运行时装载 contract 相对朴素：`GetFullAssemblyPublishPath()` 先把发布集合收敛成 `UE/Game/CustomProjects` 的 DLL 路径；`GetAssemblyPath()` 再把搜索范围限定在 `ProjectContentDir()/PublishDirectory` 和 `.NET runtime dir`；`UAssemblyLoader::Load()` 按程序集名在这些目录里找第一个同名 DLL，找到后直接读字节；`FMonoDomain::AssemblyPreloadHook()` 则把这些字节喂给 `mono_image_open_from_data_with_name()` / `mono_assembly_load_from_full()`。在本轮定位到的这些代码路径里，能直接看到的是“名称 + 目录”约束，本轮没有定位到与 `BuildIdentifier` 或 `DataGuid` 对等的运行时兼容性校验。

Angelscript 在发布态则明显更“封印化”。`AngelscriptEngine.cpp` 先根据 `RuntimeConfig` 决定何时才允许 `bUsePrecompiledData`；随后会选择配置专属的 `PrecompiledScript_<Config>.Cache`，并用 `FAngelscriptPrecompiledData::IsValidForCurrentBuild()` 检查 `BuildIdentifier` 是否与当前构建一致。如果预编译 cache 通过了这一步，还要继续比对 `FStaticJITCompiledInfo::PrecompiledDataGuid` 和 `PrecompiledData->DataGuid`；一旦不一致，就明确禁用已编入二进制的 transpiled C++。更进一步，如果本次运行采用 fully precompiled scripts，热重载会被显式关闭并打印恢复方法。也就是说，Angelscript 的发布态不仅有“带什么文件”，还有“这份文件是否还能和这份二进制一起工作”的显式 sealing contract。

```
[D11-Deep] Deployment Compatibility Seal

[UnrealCSharp]
Published assemblies
├─ UE.dll / Game.dll / Custom.dll list
├─ search roots
│  ├─ Content/<PublishDirectory>
│  └─ net runtime dir
├─ UAssemblyLoader::Load(name)                 // first matching filename wins
└─ Mono load from raw bytes
   └─ runtime trust is primarily name + path

[Angelscript]
Precompiled shipping payload
├─ RuntimeConfig decides bUsePrecompiledData
├─ choose PrecompiledScript_<Config>.Cache
├─ validate BuildIdentifier
├─ validate StaticJIT PrecompiledDataGuid
└─ if fully precompiled
   ├─ disable hot reload
   └─ log how to re-enter development mode
```

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp:1035-1049` 与 `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/AssemblyLoader.cpp:6-23`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp
// 位置: 1035-1049，程序集集合与搜索根由名称和目录固定出来
// ============================================================================
TArray<FString> FUnrealCSharpFunctionLibrary::GetFullAssemblyPublishPath()
{
    return TArrayBuilder<FString>().
           Add(GetFullUEPublishPath()).
           Add(GetFullGamePublishPath()).
           Append(GetFullCustomProjectsPublishPath()).
           Build();
}

TArray<FString> FUnrealCSharpFunctionLibrary::GetAssemblyPath()
{
    return TArrayBuilder<FString>().
           Add(FPaths::ProjectContentDir() / GetPublishDirectory()).
           Add(FMonoFunctionLibrary::GetNetDirectory()).
           Build();
}
// ★ 运行时契约先把目标收敛成“哪些 DLL 名称 + 去哪些目录找”
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/AssemblyLoader.cpp
// 位置: 6-23，loader 按名字在搜索根里取第一个匹配 DLL
// ============================================================================
TArray<uint8> UAssemblyLoader::Load(const FString& InAssemblyName)
{
    auto AssemblyPaths = FUnrealCSharpFunctionLibrary::GetAssemblyPath();

    for (const auto& AssemblyPath : AssemblyPaths)
    {
        if (const auto File = FPaths::Combine(AssemblyPath, InAssemblyName) + DLL_SUFFIX;
            IFileManager::Get().FileExists(*File))
        {
            TArray<uint8> Data;
            FFileHelper::LoadFileToArray(Data, *File);
            return Data;
        }
    }

    return {};
}
// ★ 这里能直证 loader 的 trust boundary 是 filename/path；未见额外 build-id 或 guid 检查
```

关键源码 [2] `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp:490-531,755-757`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp
// 位置: 490-531, 755-757，程序集按 preload hook + raw bytes 装进 Mono
// ============================================================================
void FMonoDomain::RegisterAssemblyPreloadHook()
{
    mono_install_assembly_preload_hook(AssemblyPreloadHook, nullptr);
}

MonoAssembly* FMonoDomain::AssemblyPreloadHook(MonoAssemblyName* InAssemblyName, char** OutAssemblyPath,
                                               void* InUserData)
{
    ...
    if (const auto AssemblyLoader = FUnrealCSharpFunctionLibrary::GetAssemblyLoader())
    {
        if (const auto Data = AssemblyLoader->Load(AssemblyName); !Data.IsEmpty())
        {
            MonoAssembly* Assembly = nullptr;
            LoadAssembly(AssemblyName, Data, nullptr, &Assembly);
            return Assembly;
        }
    }

    return nullptr;
}

const auto Image = mono_image_open_from_data_with_name((char*)InData.GetData(), InData.Num(),
                                                       true, &ImageOpenStatus,
                                                       false, TCHAR_TO_UTF8(*InAssemblyName));
...
const auto Assembly = mono_assembly_load_from_full(Image, TCHAR_TO_UTF8(*InAssemblyName), &ImageOpenStatus, false);
// ★ Mono 侧这里接收的是“这份字节是什么程序集”，而不是“它是否与当前 build/sealed cache 匹配”
```

关键源码 [3] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1425-1428,1513-1556,2046-2056` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp:2642-2645`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 1425-1428, 1513-1556, 2046-2056，发布态使用预编译数据前先过多层兼容性检查
// ============================================================================
bGeneratePrecompiledData = RuntimeConfig.bGeneratePrecompiledData;
bScriptDevelopmentMode = RuntimeConfig.bIsEditor || RuntimeConfig.bDevelopmentMode;
bUsePrecompiledData = !bGeneratePrecompiledData && !RuntimeConfig.bIgnorePrecompiledData
    && !RuntimeConfig.bRunningCommandlet && !WITH_EDITOR && !bScriptDevelopmentMode;
...
if (IFileManager::Get().FileExists(*Filename))
{
    PrecompiledData = new FAngelscriptPrecompiledData(Engine);
    PrecompiledData->Load(Filename);

    if (!PrecompiledData->IsValidForCurrentBuild())
    {
        delete PrecompiledData;
        PrecompiledData = nullptr;
        UE_LOG(Angelscript, Warning, TEXT("Loaded angelscript precompiled data was for a different build configuration. Discarding all precompiled data."));
    }
    else
    {
        const FStaticJITCompiledInfo* CompiledInfo = FStaticJITCompiledInfo::Get();
        if (CompiledInfo != nullptr && CompiledInfo->PrecompiledDataGuid != PrecompiledData->DataGuid)
        {
            UE_LOG(Angelscript, Warning, TEXT("Loaded angelscript precompiled data does not match the transpiled C++ in the game binary. Transpiled code will not be used!"));
            FJITDatabase::Get().Clear();
        }
    }
}
...
if (PrecompiledData != nullptr && bUsePrecompiledData && !bScriptDevelopmentMode)
{
    bUsedPrecompiledDataForPreprocessor = true;
    ModulesToCompile = PrecompiledData->GetModulesToCompile();
    UE_LOG(Angelscript, Warning, TEXT("Using fully precompiled scripts. Hot reloading is disabled for this run."));
    UE_LOG(Angelscript, Warning, TEXT("Delete PrecompiledScript.Cache or run with -as-development-mode flag to enable hot reload."));
}
// ★ 先校验，再决定是否信任 cache 与二进制，再决定是否允许热重载
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp
// 函数: FAngelscriptPrecompiledData::IsValidForCurrentBuild
// 位置: 2642-2645，build 兼容性封印是显式函数，而不是隐含约定
// ============================================================================
bool FAngelscriptPrecompiledData::IsValidForCurrentBuild()
{
    return BuildIdentifier == GetCurrentBuildIdentifier() && BuildIdentifier != -1;
}
// ★ cache 是否还能用，不靠“文件名看起来像对的”，而靠 build identifier 显式判断
```

关键源码 [4] `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h:74-80`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h
// 位置: 74-80，编入二进制的 StaticJIT 还携带 precompiled-data guid
// ============================================================================
struct ANGELSCRIPTRUNTIME_API FStaticJITCompiledInfo
{
    FGuid PrecompiledDataGuid;

    FStaticJITCompiledInfo(FGuid Guid);
    static const FStaticJITCompiledInfo* Get();
};
// ★ 这为“二进制里的 transpiled C++ 是否仍和 cache 同一批产物”提供了第二道封印
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 运行时信任边界 | UnrealCSharp 的 loader 以 `assembly name + search path` 为主，本轮未定位到对等的 build-id/guid 校验 | 实现方式不同 |
| 预编译产物校验 | Angelscript 显式检查 `BuildIdentifier`，并继续比对 `PrecompiledDataGuid` | 实现质量差异 |
| 失配后的退化策略 | Angelscript 会丢弃不兼容 cache、禁用不匹配的 transpiled C++，并明确记录日志 | 实现质量差异 |
| 发布态热重载策略 | Angelscript 在 fully precompiled run 下直接关闭热重载并给出恢复方式；UnrealCSharp 本轮定位到的是 publish/load 路径，没有对等显式封印日志 | 实现方式不同 |

这一维最值得记住的不是“谁更安全”，而是“运行时究竟信任什么”。UnrealCSharp 的装载边界更像“把对的 DLL 放到对的目录”；Angelscript 的装载边界更像“即便文件在，也必须证明它和当前 build 与当前静态 JIT 是同一批产物”。

---

## 深化分析 (2026-04-08 19:50:35)

本节不新增对比结论，只对上一节 `2026-04-08 19:38:09` 中少数源码引用的行号边界做校准。原因是我在复核实际源码时确认：个别片段末尾包含了 `return;` 或闭合 `}`，而原行号范围少记了 1-2 行。以下勘误只修正证据定位，不改变前述分析判断。

| 维度 | 上一节引用 | 校准后引用 | 校准原因 |
| --- | --- | --- | --- |
| D3 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/NewClass/SDynamicNewClassDialog.cpp:894-900` | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/NewClass/SDynamicNewClassDialog.cpp:894-902` | 片段实际包含 `return;` 与闭合 `}` |
| D8 | `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Domain/InternalCall/FRegisterStruct.cpp:48-53` | `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Domain/InternalCall/FRegisterStruct.cpp:48-54` | 片段实际包含函数闭合 `}` |
| D8 | `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Domain/InternalCall/FRegisterWeakObjectPtr.cpp:37-43` | `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Domain/InternalCall/FRegisterWeakObjectPtr.cpp:37-44` | 片段实际包含函数闭合 `}` |
| D8 | `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FObjectRegistry.cpp:98-116` | `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FObjectRegistry.cpp:98-117` | 片段实际包含 `return true;` 后的闭合 `}` |
| D8 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UStruct.cpp:149-167` | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UStruct.cpp:149-168` | 片段实际包含函数闭合 `}` |
| D11 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp:1035-1049` | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp:1035-1050` | 片段实际包含 `GetAssemblyPath()` 的闭合 `}` |
| D11 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/AssemblyLoader.cpp:6-23` | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/AssemblyLoader.cpp:6-24` | 片段实际包含函数闭合 `}` |
| D11 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp:490-531,755-757` | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp:490-531,755-758` | `RegisterAssemblyPreloadHook()` 片段实际包含函数闭合 `}` |

---

## 深化分析 (2026-04-08 20:00:57)

本轮不再扩宽维度，只补三条前文尚未写透的结构性 contract：`Build.cs` 如何变成生成输入、UnrealCSharp 的“自动绑定”底下到底有多少手工描述层，以及 IDE 工程图谱究竟由谁持有。

### [维度 D1] Build 图谱传递：serialized manifest vs on-demand Build.cs parse

前文 D1 已经说明 UnrealCSharp 的模块拆分更细。本轮继续下钻会发现，它连模块图谱本身都不是“运行时临时扫目录”得到的，而是在 `UnrealCSharpCore.build.cs` 里被主动序列化成 `Intermediate/UnrealCSharp_Modules.json`。后续的 editor setting 与 generator 都从这份 manifest 读数据。换句话说，UnrealCSharp 把 Build graph 当成了**跨阶段共享数据**。

Angelscript 的做法不同。`AngelscriptFunctionTableCodeGenerator::LoadSupportedModules()` 会在 UHT 导出阶段先定位 `AngelscriptRuntime.Build.cs`，再用正则逐行提取 `DependencyModuleNames.AddRange` 里的字符串字面量，同时区分 `if (Target.bBuildEditor)` 内外块。也就是说，Angelscript 同样把 Build.cs 当生成输入，但它选择的是**按需解析源码文本**，而不是先落盘成稳定 manifest。

```
[D1-Deep] Build Graph Handoff

[UnrealCSharp]
UnrealCSharpCore.build.cs
├─ scan Project/Engine modules and plugins
├─ write Intermediate/UnrealCSharp_Modules.json
├─ FUnrealCSharpFunctionLibrary::Get*ModuleList()
├─ UUnrealCSharpEditorSetting::GetModuleList()
└─ FGeneratorCore::BeginGenerator
   └─ SupportedModule filter

[Angelscript]
AngelscriptRuntime.Build.cs
└─ AngelscriptFunctionTableCodeGenerator::LoadSupportedModules()
   ├─ resolve Build.cs path from UHT session
   ├─ regex parse quoted module names
   └─ filter factory.Session.Modules
```

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharpCore/UnrealCSharpCore.build.cs:131-209`

```csharp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/UnrealCSharpCore.build.cs
// 函数: GeneratorModules
// 位置: Build 阶段把模块/插件图谱固化到 Intermediate json
// ============================================================================
private void GeneratorModules()
{
    var Intermediate = Path.Combine(ProjectPath, "Intermediate");
    var JsonFullFilename = Path.Combine(Intermediate, "UnrealCSharp_Modules.json");

    var ProjectPlugins = new Dictionary<string, string>();
    GetModules(Path.GetFullPath(Path.Combine(ProjectPath, "Plugins/")), ProjectPlugins);
    GetPlugins(Path.GetFullPath(Path.Combine(ProjectPath, "Plugins/")), ProjectPlugins);

    var EngineModules = new Dictionary<string, string>();
    GetModules(Path.GetFullPath(Path.Combine(EngineDirectory, "Source/Developer/")), EngineModules);
    GetModules(Path.GetFullPath(Path.Combine(EngineDirectory, "Source/Editor/")), EngineModules);
    GetModules(Path.GetFullPath(Path.Combine(EngineDirectory, "Source/Programs/")), EngineModules);
    GetModules(Path.GetFullPath(Path.Combine(EngineDirectory, "Source/Runtime/")), EngineModules);

    var EnginePlugins = new Dictionary<string, string>();
    GetModules(Path.GetFullPath(Path.Combine(EngineDirectory, "Plugins/")), EnginePlugins);
    GetPlugins(Path.GetFullPath(Path.Combine(EngineDirectory, "Plugins/")), EnginePlugins);

    using var Writer = new JsonWriter(JsonFullFilename);
    Writer.WriteObjectStart();

    Writer.WriteObjectStart("ProjectModules");
    foreach (var Item in Target.ExtraModuleNames)
    {
        Writer.WriteValue(Item, Path.Join(Target.ProjectFile.Directory.FullName, "Source", Item));
    }
    Writer.WriteObjectEnd();

    Writer.WriteObjectStart("ProjectPlugins");
    foreach (var Item in ProjectPlugins)
    {
        Writer.WriteValue(Item.Key, Item.Value);
    }
    Writer.WriteObjectEnd();

    Writer.WriteObjectStart("EngineModules");
    foreach (var Item in EngineModules)
    {
        Writer.WriteValue(Item.Key, Item.Value);
    }
    Writer.WriteObjectEnd();

    Writer.WriteObjectStart("EnginePlugins");
    foreach (var Item in EnginePlugins)
    {
        Writer.WriteValue(Item.Key, Item.Value);
    }
    Writer.WriteObjectEnd();
    // ★ Build.cs 在这里已经不是“声明依赖”，而是生成后续工具链共享的模块 manifest
}
```

关键源码 [2] `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp:1259-1330` 与 `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Setting/UnrealCSharpEditorSetting.cpp:299-317`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp
// 函数: GetEngineModuleList / GetProjectModuleList
// 位置: Runtime / Editor 统一从 manifest 读模块名单
// ============================================================================
const TArray<FString>& FUnrealCSharpFunctionLibrary::GetEngineModuleList()
{
    static TArray<FString> EngineModuleList;

    if (EngineModuleList.IsEmpty())
    {
        static auto FilePath = FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("UnrealCSharp_Modules.json"));

        if (FString JsonStr; FFileHelper::LoadFileToString(JsonStr, *FilePath))
        {
            ...
            if (const TSharedPtr<FJsonObject>* OutObject; JsonObj->TryGetObjectField(TEXT("EngineModules"), OutObject))
            {
                for (const auto& [Key, PLACEHOLDER] : OutObject->Get()->Values)
                {
                    EngineModuleList.AddUnique(Key);
                }
            }

            if (const TSharedPtr<FJsonObject>* OutObject; JsonObj->TryGetObjectField(TEXT("EnginePlugins"), OutObject))
            {
                for (const auto& [Key, PLACEHOLDER] : OutObject->Get()->Values)
                {
                    EngineModuleList.AddUnique(Key);
                }
            }
        }
    }

    return EngineModuleList;
}

const TArray<FString>& FUnrealCSharpFunctionLibrary::GetProjectModuleList()
{
    static TArray<FString> ProjectModuleList;
    ...
    // ★ editor setting、generator、路径判定都复用同一份 manifest，而不是各扫各的目录
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Setting/UnrealCSharpEditorSetting.cpp
// 函数: UUnrealCSharpEditorSetting::GetModuleList
// 位置: 编辑器下拉框直接消费 manifest
// ============================================================================
TArray<FString> UUnrealCSharpEditorSetting::GetModuleList()
{
    TArray<FString> ModuleArray;

    const auto& ProjectModules = FUnrealCSharpFunctionLibrary::GetProjectModuleList();
    const auto& EngineModules = FUnrealCSharpFunctionLibrary::GetEngineModuleList();

    for (const auto& ProjectModule : ProjectModules)
    {
        ModuleArray.AddUnique(ProjectModule);
    }

    for (const auto& EngineModule : EngineModules)
    {
        ModuleArray.AddUnique(EngineModule);
    }

    return ModuleArray;
    // ★ UI 看到的模块列表，本质上是 Build.cs 预先序列化出来的结果
}
```

关键源码 [3] `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:334-385`

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs
// 函数: LoadSupportedModules
// 位置: UHT 导出阶段按需解析 AngelscriptRuntime.Build.cs
// ============================================================================
private static AngelscriptSupportedModules LoadSupportedModules(IUhtExportFactory factory)
{
    string buildCsPath = ResolveRuntimeBuildCsPath(factory);
    factory.AddExternalDependency(buildCsPath);

    HashSet<string> allModules = new(StringComparer.OrdinalIgnoreCase)
    {
        "AngelscriptRuntime",
    };
    HashSet<string> editorOnlyModules = new(StringComparer.OrdinalIgnoreCase);

    bool inDependencyBlock = false;
    bool inEditorBlock = false;
    foreach (string rawLine in File.ReadAllLines(buildCsPath))
    {
        string line = rawLine.Trim();
        if (line.StartsWith("if (Target.bBuildEditor)", StringComparison.Ordinal))
        {
            inEditorBlock = true;
        }

        if (line.Contains("DependencyModuleNames.AddRange", StringComparison.Ordinal))
        {
            inDependencyBlock = true;
        }

        if (inDependencyBlock)
        {
            foreach (Match match in QuotedStringPattern.Matches(line))
            {
                string moduleName = match.Groups[1].Value;
                allModules.Add(moduleName);
                if (inEditorBlock)
                {
                    editorOnlyModules.Add(moduleName);
                }
            }
        }
    }

    return new AngelscriptSupportedModules(allModules, editorOnlyModules);
    // ★ 这里没有单独 manifest，生成器直接回读 Build.cs 文本
}
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| Build 图谱的存在形态 | UnrealCSharp 把模块图谱序列化到 `Intermediate/UnrealCSharp_Modules.json` | 实现方式不同 |
| Build 图谱的消费方 | UnrealCSharp 的 editor setting 与 generator 共享同一份 manifest；Angelscript 的 UHT 工具每次回读 `Build.cs` 文本 | 实现方式不同 |
| 对 Build.cs 语法的耦合 | Angelscript 的模块裁剪依赖正则匹配 `AddRange` 语法；UnrealCSharp 把耦合集中在一次 JSON 生成 | 实现质量差异 |
| 是否属于“没有实现” | 不是。两边都把 Build.cs 变成生成输入，只是 handoff 形态不同 | 实现方式不同 |

这一维最值得吸收的点不是“模块多不多”，而是 Build 图谱有没有被显式建模成稳定数据。UnrealCSharp 的做法更像“先标准化，再复用”；Angelscript 当前更像“需要时再解析”。

### [维度 D2] 自动绑定的底座：descriptor-driven InternalCall stubs vs direct bind DSL

前文 D2 主要看 UHT / override / trampoline。本轮补的是更底层的一层：UnrealCSharp 的“自动绑定”并不等于“完全不手写桥接”。对 `UObject`、`FVector` 这类桥接语义较重的类型，作者仍然要先在 `FRegister*.cpp` 里用 `TBindingClassBuilder` 写出函数、属性和 operator 描述；随后 `FBindingClassGenerator` 再把这些描述生成为 C# `[MethodImpl(InternalCall)]` extern stub；运行时 `FMonoDomain::RegisterBinding()` 把描述里的实现名批量喂给 `mono_add_internal_call()`。

这条链说明一个关键事实：UnrealCSharp 的“自动化”主要发生在**managed stub 生成**与**批量注册**层，而不是桥接语义自动发现层。与之相比，Angelscript 的 `Bind_UObject.cpp` 这类文件会直接在 native 侧把 `ExistingClass("UObject").Method(...)` 注册进脚本 API。两边都保留了手工编写的桥接语义层；差别在于 UnrealCSharp 多了一层“由 descriptor 生成 managed surface”的中间产物。

```
[D2-Deep] Manual Bridge to Script Surface

[UnrealCSharp custom bridge]
FRegisterObject / FRegisterVector
├─ TBindingClassBuilder / FClassBuilder
├─ FBinding descriptor registry
├─ FBindingClassGenerator
│  └─ generate C# InternalCall extern stubs
└─ FMonoDomain::RegisterBinding
   └─ mono_add_internal_call

[Angelscript custom bridge]
Bind_UObject.cpp / Bind_FVector.cpp
└─ FAngelscriptBinds::ExistingClass(...).Method(...)
   └─ direct native DSL registration
```

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Binding/Class/FClassBuilder.cpp:6-15,59-79` 与 `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Domain/InternalCall/FRegisterObject.cpp:137-151`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Binding/Class/FClassBuilder.cpp
// 函数: FClassBuilder::FClassBuilder / FClassBuilder::Function
// 位置: 手工 bridge 描述首先被登记到 FBinding registry
// ============================================================================
FClassBuilder::FClassBuilder(const TFunction<FString()>& InClassFunction,
                             const FString& InImplementationNameSpace,
                             const TFunction<bool()>& InIsProjectClassFunction,
                             const bool InIsReflectionClass,
                             const TOptional<TFunction<FTypeInfo*()>>& InTypeInfoFunction):
    ClassRegister(FBinding::Get().Register(InClassFunction,
                                           InImplementationNameSpace,
                                           InIsProjectClassFunction,
                                           InIsReflectionClass,
                                           InTypeInfoFunction))
{
}

FClassBuilder& FClassBuilder::Function(const FString& InName,
                                       const FString& InImplementationName,
                                       const void* InMethod)
{
    const auto FunctionImplementationName = GetFunctionImplementationName(InName, InImplementationName);

    Functions.Add(InName);
    Function(FunctionImplementationName, TFunctionPointer<decltype(InMethod)>(InMethod));
    return *this;
    // ★ 这里同时记录“脚本可见名字”和“native 实现入口名”
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Domain/InternalCall/FRegisterObject.cpp
// 函数: FRegisterObject::FRegisterObject
// 位置: UObject bridge 语义仍然由手写 builder 描述
// ============================================================================
FRegisterObject()
{
    TBindingClassBuilder<UObject>(NAMESPACE_LIBRARY)
        .Function("Identical", IdenticalImplementation)
        .Function("StaticClass", StaticClassImplementation)
        .Function("GetClass", GetClassImplementation)
        .Function("GetName", GetNameImplementation)
        .Function("GetWorld", BINDING_FUNCTION(&UObject::GetWorld))
        .Function("IsValid", IsValidImplementation)
        .Function("IsA", IsAImplementation)
        .Function("AddToRoot", AddToRootImplementation)
        .Function("RemoveFromRoot", RemoveFromRootImplementation)
        .Function("IsRooted", IsRootedImplementation)
        .Function("AddReference", AddReferenceImplementation)
        .Function("RemoveReference", RemoveReferenceImplementation);
    // ★ “自动绑定”之前，先要有人把 UObject bridge 的语义逐项写出来
}
```

关键源码 [2] `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FBindingClassGenerator.cpp:743-833` 与 `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp:783-792`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FBindingClassGenerator.cpp
// 函数: FBindingClassGenerator::GeneratorImplementation
// 位置: 把 descriptor 生成成 C# InternalCall extern stub
// ============================================================================
void FBindingClassGenerator::GeneratorImplementation(const FBindingClass* InClass)
{
    ...
    if (InClass->GetSubscript().IsSet())
    {
        auto FunctionDeclaration = FString::Printf(TEXT(
            "\t\t[MethodImpl(MethodImplOptions.InternalCall)]\n"
            "\t\tpublic static extern void %s(nint InObject, byte* %s, byte* %s);\n"
        ),
                                                   *BINDING_COMBINE_FUNCTION_IMPLEMENTATION(
                                                       ClassContent,
                                                       InClass->GetSubscript().GetGetImplementationName()),
                                                   IN_BUFFER_TEXT,
                                                   RETURN_BUFFER_TEXT
        );
        ...
    }

    for (const auto& Property : InClass->GetProperties())
    {
        ...
        if (bRead)
        {
            GetFunctionContent = FString::Printf(TEXT(
                "\t\t[MethodImpl(MethodImplOptions.InternalCall)]\n"
                "\t\tpublic static extern void %s(nint InObject, byte* %s);\n"
            ),
                                                 *BINDING_COMBINE_FUNCTION_IMPLEMENTATION(
                                                     ClassContent, (BINDING_PROPERTY_GET + PropertyName)),
                                                 RETURN_BUFFER_TEXT
            );
        }
    }
    // ★ managed 侧看到的 extern API 是生成出来的，不是手写 C#
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp
// 函数: FMonoDomain::RegisterBinding
// 位置: 运行时把 descriptor 里的实现名批量注册给 Mono
// ============================================================================
void FMonoDomain::RegisterBinding()
{
    for (const auto& Class : FBinding::Get().Register().GetClasses())
    {
        for (const auto& Method : Class->GetMethods())
        {
            mono_add_internal_call(TCHAR_TO_ANSI(*Method.GetMethod()), Method.GetFunction());
        }
    }
    // ★ 到这里才真正把前面手写的 bridge 语义接到 CLR
}
```

关键源码 [3] `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp:37-59`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp
// 位置: UObject API 直接用 native DSL 注册给脚本侧
// ============================================================================
AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_UObject_Base((int32)FAngelscriptBinds::EOrder::Late-1, []
{
    auto UObject_ = FAngelscriptBinds::ExistingClass("UObject");
    UObject_.Method("void AddToRoot()", METHOD_TRIVIAL(UObject, AddToRoot));
    UObject_.Method("void RemoveFromRoot()", METHOD_TRIVIAL(UObject, RemoveFromRoot));
    UObject_.Method("bool GetIsRooted() const", METHOD_TRIVIAL(UObject, IsRooted));
    UObject_.Method("bool IsTransient() const", [](UObject* Object) { return Object->HasAnyFlags(RF_Transient); });
    UObject_.Method("bool IsEditorOnly() const", METHOD_TRIVIAL(UObject, IsEditorOnly));
    UObject_.Method("bool Modify(bool bAlwaysMarkDirty = true)", METHOD(UObject, Modify));
    ...
    UObject_.Method("UClass GetClass() const", METHOD_TRIVIAL(UObject, GetClass));
    UObject_.Method("UObject GetOuter() const", METHOD_TRIVIAL(UObject, GetOuter));
});
// ★ 这里没有额外的 managed stub 生成阶段，bind DSL 直接决定最终暴露面
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 自动化发生在哪一层 | UnrealCSharp 对 custom bridge 的自动化主要在 `descriptor -> C# InternalCall stub` 与批量注册层 | 实现方式不同 |
| 桥接语义来源 | UnrealCSharp 的 `FRegister*.cpp` 与 Angelscript 的 `Bind_*.cpp` 都是人工编写 | 不是“有/无”的差别 |
| 最终注册形态 | UnrealCSharp 走 `mono_add_internal_call`；Angelscript 直接在 native DSL 里注册脚本 API | 实现方式不同 |
| 可维护性代价 | UnrealCSharp 多一层生成 stub，managed surface 更统一；Angelscript 少一层中转，native 暴露路径更直接 | 实现质量差异 |

这一维要避免误判成“UnrealCSharp 全自动、Angelscript 全手写”。从源码看，更准确的说法是：两边都保留了人工维护的桥接语义层，只是 UnrealCSharp 在这层之上再生成一层 C# stub。

### [维度 D6] IDE 拓扑所有权：generated solution graph vs file/line navigation hooks

前文 D6 已经覆盖 Analyzer、Weaver 和跳转元数据。本轮继续往上收束，会发现 UnrealCSharp 的 IDE 支撑不只是“能补全”或“能跳转”，而是插件自己拥有整个 C# solution 的拓扑。`Template/Script.sln` 预留了 `UE / Game / SourceGenerator / Weavers / CustomProjects` 的位置；`FSolutionGenerator` 再把 `UE.csproj`、`Game.csproj`、`CustomProjects` 与对应的 `ProjectConfigurationPlatforms` 一次性补进去，`FCustomProject::GUID()` 负责给自定义工程生成稳定 GUID。

当前 Angelscript 的 IDE 集成则更偏“源码导航 hook”。`FAngelscriptSourceCodeNavigation` 能把 `UClass / UFunction / FProperty` 映射回脚本文件，再直接执行 `code --goto "path:line"`；`UASFunction::GetSourceLineNumber()` 从脚本字节码元数据里反推行号。也就是说，当前 Angelscript 已经实现**文件/行级导航**，但在本轮扫描到的插件源码里，没有定位到对等的 `sln/csproj` 生成与维护链。

```
[D6-Deep] IDE Topology Ownership

[UnrealCSharp]
Template/Script.sln
├─ UE project
├─ Game project
├─ SourceGenerator
├─ Weavers
└─ CustomProjects[*]                 // GUID-stable nodes
   └─ Game.props
      ├─ Analyzer reference
      ├─ Weaver reference
      └─ AfterBuildPublish -> DLL copy

[Angelscript]
Script metadata
├─ source file path
├─ source line number
└─ AngelscriptSourceCodeNavigation
   └─ code --goto path:line
```

关键源码 [1] `Reference/UnrealCSharp/Template/Script.sln:6-20,25-42` 与 `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpSetting.h:28-50`

```text
Microsoft Visual Studio Solution File, Format Version 12.00
Project("{9A19103F-16F7-4668-BE54-9A1E7A4F7556}") = "", "", "{7AF881DC-664B-4AF7-BCB6-8FA8CC5E8780}"
EndProject
Project("{9A19103F-16F7-4668-BE54-9A1E7A4F7556}") = "", "", "{A2B210E9-51AE-490B-8B87-F8492CB2A417}"
    ProjectSection(ProjectDependencies) = postProject
        {DB42848F-4C21-4581-99BA-C92CB37D4024} = {DB42848F-4C21-4581-99BA-C92CB37D4024}
    EndProjectSection
EndProject
Project("{2150E333-8FDC-42A3-9474-1A3956D46DE8}") = "Utils", "Utils", "{B447E954-3688-418A-9A1C-CD5FC12EC316}"
EndProject
Project("{9A19103F-16F7-4668-BE54-9A1E7A4F7556}") = "SourceGenerator", "SourceGenerator\\SourceGenerator.csproj", "{095D641F-E823-4EE2-89A8-EBC636049F98}"
EndProject
Project("{9A19103F-16F7-4668-BE54-9A1E7A4F7556}") = "Weavers", "Weavers\\Weavers.csproj", "{DB42848F-4C21-4581-99BA-C92CB37D4024}"
EndProject
ProjectPlaceholder
...
    {SolutionConfigurationPlatformsPlaceholder}.Release|Any CPU.Build.0 = Release|Any CPU
// ★ 模板里已经预留 UE/Game/工具项目/自定义项目占位，不是临时拼一个单工程
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpSetting.h
// 函数: FCustomProject::GUID
// 位置: 自定义 C# 工程的 solution 节点 GUID 由插件配置生成
// ============================================================================
USTRUCT()
struct FCustomProject
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere)
    FString Name;

    FString GUID() const
    {
        const auto Hex = FString::Printf(TEXT("%X"), GetTypeHash(Name));

        return FString::Printf(TEXT(
            "%s-%s-%s-%s-%s%s"
        ),
                               *Hex,
                               *Hex.Mid(0, 4),
                               *Hex.Mid(4, 4),
                               *Hex.Mid(0, 4),
                               *Hex.Mid(4, 4),
                               *Hex
        );
    }
    // ★ 自定义工程不是匿名附属目录，而是有稳定 GUID 的 solution node
};
```

关键源码 [2] `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FSolutionGenerator.cpp:276-362` 与 `Reference/UnrealCSharp/Template/Game.props:12-28`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FSolutionGenerator.cpp
// 函数: ReplaceProject / ReplaceProjectPlaceholder / ReplaceSolutionConfigurationPlatformsPlaceholder
// 位置: 真正把 template 变成完整 solution graph
// ============================================================================
void FSolutionGenerator::ReplaceProject(FString& OutResult)
{
    OutResult = OutResult.Replace(
        TEXT("Project(\"{9A19103F-16F7-4668-BE54-9A1E7A4F7556}\") = \"\", \"\", \"{7AF881DC-664B-4AF7-BCB6-8FA8CC5E8780}\""),
        *FString::Printf(TEXT(
            "Project(\"{9A19103F-16F7-4668-BE54-9A1E7A4F7556}\") = \"%s\", \"%s\\%s%s\", \"{7AF881DC-664B-4AF7-BCB6-8FA8CC5E8780}\""
        ),
                         *FUnrealCSharpFunctionLibrary::GetUEName(),
                         *FUnrealCSharpFunctionLibrary::GetUEName(),
                         *FUnrealCSharpFunctionLibrary::GetUEName(),
                         *PROJECT_SUFFIX
        ));
    ...
}

void FSolutionGenerator::ReplaceProjectPlaceholder(FString& OutResult)
{
    if (const auto UnrealCSharpSetting = FUnrealCSharpFunctionLibrary::GetMutableDefaultSafe<UUnrealCSharpSetting>())
    {
        for (const auto& CustomProject : UnrealCSharpSetting->GetCustomProjects())
        {
            Projects += FString::Printf(TEXT(
                "Project(\"{%s}\") = \"%s\", \"%s\\%s%s\", \"{%s}\"\n"
                "EndProject\n"
            ),
                                        *CSHARP_GUID,
                                        *CustomProject.Name,
                                        *CustomProject.Name,
                                        *CustomProject.Name,
                                        *PROJECT_SUFFIX,
                                        *CustomProject.GUID());
        }
    }
}

void FSolutionGenerator::ReplaceSolutionConfigurationPlatformsPlaceholder(FString& OutResult)
{
    ...
    // ★ 不只生成 project node，还给每个 custom project 补 Debug/Release 配置矩阵
}
```

```xml
<!-- =========================================================================
     文件: Reference/UnrealCSharp/Template/Game.props
     位置: solution graph 中 Game 项目的能力装配
     说明: Analyzer / Weaver / Publish 都被纳入项目定义
     ========================================================================= -->
<ItemGroup>
    <ProjectReference Include="..\SourceGenerator\SourceGenerator.csproj" OutputItemType="Analyzer" ReferenceOutputAssembly="false"/>
    <ProjectReference Include="..\Weavers\Weavers.csproj" ReferenceOutputAssembly="false" />
</ItemGroup>
<Target Name="AfterBuildPublish" AfterTargets="Build">
    <MSBuild Projects="$(ProjectPath)" Targets="Publish" Properties="Configuration=$(Configuration)" />
</Target>
<Target Name="CopyDllsAfterPublish" AfterTargets="Publish">
    <ItemGroup>
        <PublishFiles Include="$(PublishDir)*.dll" />
    </ItemGroup>
    <Copy SourceFiles="@(PublishFiles)" DestinationFolder="$(ScriptOutputPath)" />
</Target>
<!-- ★ IDE 工程节点本身就内置 Analyzer / Weaver / Publish 语义 -->
```

关键源码 [3] `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp:15-44,95-115` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:1548-1559`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp
// 位置: 编辑器 IDE 集成的主入口是 file/line 导航
// ============================================================================
virtual bool NavigateToFunction(const UFunction* InFunction) override
{
    auto* ASFunc = Cast<const UASFunction>(InFunction);
    if (ASFunc == nullptr)
        return false;

    FString Path = ASFunc->GetSourceFilePath();
    if (Path.Len() == 0)
        return false;

    OpenFile(Path, ASFunc->GetSourceLineNumber());
    return true;
}

void OpenFile(const FString& Path, int LineNo = -1)
{
    if (LineNo != -1)
        FPlatformMisc::OsExecute(nullptr, TEXT("code"), *FString::Printf(TEXT("--goto \"%s:%d\""), *Path, LineNo));
    else
        FPlatformMisc::OsExecute(nullptr, TEXT("code"), *FString::Printf(TEXT("\"%s\""), *Path));
}
// ★ 当前插件的 IDE 接入重点是“把已有脚本文件打开到某一行”
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp
// 函数: UASFunction::GetSourceLineNumber
// 位置: 行号来自脚本函数元数据，而不是外部 solution graph
// ============================================================================
int UASFunction::GetSourceLineNumber() const
{
    if (ScriptFunction == nullptr)
        return -1;

    auto* RealFunc = ((asCScriptFunction*)ScriptFunction);
    auto* scriptData = RealFunc->scriptData;
    if (scriptData == nullptr)
        return -1;

    return (scriptData->declaredAt & 0xFFFFF) + 1;
    // ★ IDE 跳转依赖脚本编译产物中的 file/line 元数据
}
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| IDE 拓扑所有权 | UnrealCSharp 插件自身生成并维护 `sln/csproj/props` 图谱 | 实现方式不同 |
| 自定义工程接入 | UnrealCSharp 为 `CustomProjects` 生成稳定 GUID 与配置矩阵 | 实现质量差异 |
| Angelscript 当前 IDE 主路径 | 当前插件源码直证的是 file/line 导航 hook，而不是外部 solution graph 生成 | “方案图谱”没有实现；“源码导航”已实现 |
| IDE contract 的粒度 | UnrealCSharp 把 Analyzer / Weaver / Publish 写进项目图谱；Angelscript 把导航能力写进编辑器 handler | 实现方式不同 |

这一维最关键的差异不是“能不能跳转”，而是**谁拥有 IDE 的结构定义权**。UnrealCSharp 把 IDE 看成插件交付物的一部分；当前 Angelscript 更像“把现有脚本资产映射回编辑器”。

---

## 深化分析 (2026-04-08 20:11:20)

### [维度 D1] 版本兼容层归属：dedicated CrossVersion module vs inline feature guards

前文 D1 已经比较过模块数量和第三方运行时接入。本轮继续下钻“版本漂移由谁吸收”。UnrealCSharp 的做法是把版本差异单独上升为 `CrossVersion` 模块：`CrossVersion.build.cs` 自己依赖 `Mono`，并导出 `UEVersion.h`、`DotnetVersion.h`、`VSVersion.h` 三类宏头；`UnrealCSharp` Runtime 和 `UnrealCSharpCore` 都显式依赖这个模块。这样业务模块拿到的不是零散 `#if ENGINE_MAJOR_VERSION`，而是已经抽象过的兼容开关。

Angelscript 在本轮扫描到的代码里则更偏“功能文件就地处理版本差异”。`Bind_FMath.cpp` 直接按 `ENGINE_MAJOR_VERSION >= 5` 决定是否暴露 `double SmoothStep`；`AngelscriptEngine.cpp` 也直接用 `#if WITH_EDITOR && ENGINE_MAJOR_VERSION >= 5` 决定是否安装 `asSetResolveObjectPtrFunction()`。这说明两边都在处理 UE 版本变化，但 UnrealCSharp 把它沉淀成独立兼容层，Angelscript 目前更多让业务实现各自承担。

```
[D1-Deep] Version Compatibility Ownership

[UnrealCSharp]
CrossVersion module
├─ UEVersion.h                       // UE API 漂移宏
├─ DotnetVersion.h                  // .NET 版本宏
├─ VSVersion.h                      // MSVC 版本宏
└─ consumed by
   ├─ UnrealCSharp Runtime
   ├─ UnrealCSharpCore
   ├─ Compiler
   └─ ScriptCodeGenerator

[Angelscript]
Feature implementation files
├─ Bind_FMath.cpp                   // 绑定文件内联版本判断
├─ AngelscriptEngine.cpp            // runtime 文件内联版本判断
└─ other runtime/editor files
   └─ direct #if ENGINE_MAJOR_VERSION ...
```

关键源码 [1] `Reference/UnrealCSharp/Source/CrossVersion/CrossVersion.build.cs:5-31`、`Reference/UnrealCSharp/Source/CrossVersion/Public/UEVersion.h:3-40`、`Reference/UnrealCSharp/Source/CrossVersion/Public/DotnetVersion.h:3-13`、`Reference/UnrealCSharp/Source/CrossVersion/Public/VSVersion.h:3-10`

```csharp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/CrossVersion/CrossVersion.build.cs
// 函数: CrossVersion(ReadOnlyTargetRules Target)
// 位置: 独立版本兼容模块的依赖定义
// ============================================================================
public class CrossVersion : ModuleRules
{
    public CrossVersion(ReadOnlyTargetRules Target) : base(Target)
    {
        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "Mono" // ★ 兼容层自己就知道 CLR 版本语义，不只是 UE 宏包装
            }
        );
    }
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/CrossVersion/Public/UEVersion.h
// 位置: UE 版本差异统一入口
// ============================================================================
#include "Misc/EngineVersionComparison.h"

#define UE_VERSION_START(MajorVersion, MinorVersion, PatchVersion) \
    UE_GREATER_SORT(ENGINE_MAJOR_VERSION, MajorVersion, UE_GREATER_SORT(ENGINE_MINOR_VERSION, MinorVersion, UE_GREATER_SORT(ENGINE_PATCH_VERSION, PatchVersion, true)))

#define UE_T_BASE_STRUCTURE_F_INT_POINT !UE_VERSION_START(5, 1, 0)
#define UE_U_CLASS_ADD_REFERENCED_OBJECTS !UE_VERSION_START(5, 1, 0)
#define UE_F_SOFT_OBJECT_PATH_SET_PATH_F_NAME !UE_VERSION_START(5, 1, 0)
// ★ 业务代码消费的是“语义宏”，而不是重复写具体版本号

// ============================================================================
// 文件: Reference/UnrealCSharp/Source/CrossVersion/Public/DotnetVersion.h
// 位置: .NET 版本兼容宏
// ============================================================================
#define DOTNET_VERSION_START(MajorVersion, MinorVersion, PatchVersion) \
    DOTNET_GREATER_SORT(DOTNET_MAJOR_VERSION, MajorVersion, DOTNET_GREATER_SORT(DOTNET_MINOR_VERSION, MinorVersion, DOTNET_GREATER_SORT(DOTNET_PATCH_VERSION, PatchVersion, true)))

#define DOTNET8 DOTNET_VERSION_START(8, 0, 0)
#define DOTNET9 DOTNET_VERSION_START(9, 0, 0)
#define DOTNET10 DOTNET_VERSION_START(10, 0, 0)
// ★ 兼容面覆盖到 CLR 本身

// ============================================================================
// 文件: Reference/UnrealCSharp/Source/CrossVersion/Public/VSVersion.h
// 位置: 编译器版本兼容宏
// ============================================================================
#ifdef _MSC_VER
#define VS2022 _MSC_VER >= 1930 && _MSC_VER < 1950
#define VS2026 _MSC_VER >= 1950
#else
#define VS2022 0
#define VS2026 0
#endif
// ★ 连 MSVC 差异也进入统一兼容层
```

关键源码 [2] `Reference/UnrealCSharp/Source/UnrealCSharp/UnrealCSharp.Build.cs:25-33`、`Reference/UnrealCSharp/Source/UnrealCSharpCore/UnrealCSharpCore.build.cs:14-20,38-45,68-77`

```csharp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/UnrealCSharp.Build.cs
// 函数: UnrealCSharp(ReadOnlyTargetRules Target)
// 位置: Runtime 显式依赖 CrossVersion
// ============================================================================
PublicDependencyModuleNames.AddRange(
    new string[]
    {
        "Core",
        "Engine",
        "CrossVersion",
        "UnrealCSharpCore"
        // ★ Runtime 不直接自己维护版本宏，而是吃兼容层
    }
);

// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/UnrealCSharpCore.build.cs
// 函数: UnrealCSharpCore(ReadOnlyTargetRules Target)
// 位置: Core 同时依赖 Mono 与 CrossVersion
// ============================================================================
#if UE_5_6_OR_LATER
    CppCompileWarningSettings.UndefinedIdentifierWarningLevel = WarningLevel.Off;
#elif UE_5_5_OR_LATER
    UndefinedIdentifierWarningLevel = WarningLevel.Off;
#else
    bEnableUndefinedIdentifierWarnings = false;
#endif
// ★ Build 级版本差异也集中处理

PublicDependencyModuleNames.AddRange(
    new string[]
    {
        "Core",
        "Projects",
        "Mono"
    }
);

PrivateDependencyModuleNames.AddRange(
    new string[]
    {
        "CoreUObject",
        "Engine",
        "Slate",
        "SlateCore",
        "Json",
        "CrossVersion",
        "Projects"
        // ★ Core 本身也把兼容层当成一等依赖
    }
);
```

关键源码 [3] `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FMath.cpp:70-80`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:669-674,1310-1314`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FMath.cpp
// 位置: 绑定文件直接携带引擎版本判断
// ============================================================================
#if ENGINE_MAJOR_VERSION >= 5
    FAngelscriptBinds::BindGlobalFunction(
        "float64 SmoothStep(float64 A, float64 B, float64 X) no_discard",
        FUNCPR_TRIVIAL(double, FMath::SmoothStep, (double, double, double)));
#endif
// ★ 版本差异直接落在功能绑定文件中

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: runtime 主文件也直接处理版本差异
// ============================================================================
#if WITH_EDITOR && ENGINE_MAJOR_VERSION >= 5
void AngelscriptResolveObjectPtr(void** PointerToObjectPtr)
{
    (void)((FObjectPtr*)PointerToObjectPtr)->Get();
}
#endif

...

#if WITH_EDITOR && ENGINE_MAJOR_VERSION >= 5
    asSetResolveObjectPtrFunction(&AngelscriptResolveObjectPtr);
#endif
// ★ 与绑定层同样，兼容逻辑内联在业务实现附近
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 版本兼容层归属 | UnrealCSharp 把 UE / .NET / MSVC 差异集中进 `CrossVersion` 模块 | 实现方式不同 |
| 业务代码暴露面 | UnrealCSharp 业务模块消费语义宏；Angelscript 业务文件直接写版本条件 | 实现方式不同 |
| 兼容范围 | UnrealCSharp 的统一兼容层不仅覆盖 UE，还覆盖 CLR 与编译器 | 实现质量差异 |
| Angelscript 现状 | 本轮已直证 Angelscript 在多个 runtime/bind 文件中就地处理版本差异 | 不是“没有实现”，而是“兼容责任分散” |

这一维更准确的结论不是“谁更兼容”，而是**版本漂移由谁承担**。UnrealCSharp 让 `CrossVersion` 模块承担这份成本；Angelscript 当前则让具体功能文件各自吸收。

### [维度 D6] 代码生成产物归属：IDE-visible `.gen.cs` + IL weaving vs compiler-only `GeneratedCode`

前文 D6 已经分析过 solution 图谱和 Analyzer 诊断。本轮继续追问“自动生成的代码最终落在哪里”。UnrealCSharp 的答案是两段式：第一段，Roslyn `UnrealTypeSourceGenerator` 直接 `Context.AddSource("*.gen.cs", ...)`，把 `.gen.cs` 当作编译输入的一部分；第二段，`UnrealTypeWeaver` 再在 IL 层给构造函数和 `Finalize` 注入注册/反注册逻辑。也就是说，类型声明补全和运行时样板分属“源码生成”和“程序集改写”两个正式阶段。

Angelscript 的生成物归属则更偏编译器内部缓冲区。`FAngelscriptPreprocessor::FFile` 里有单独的 `GeneratedCode` 数组；delegate 包装器、`StaticClass()` helper、`BindUFunction()` 之类的 `__generated` 片段先被拼成字符串，最后统一追加到 `ProcessedCode` 末尾再交给脚本编译器。它当然也在“生成代码”，但生成物不是 IDE 工程内的独立文件，而是脚本编译前的临时拼接结果。

```
[D6-Deep] Generated Artifact Ownership

[UnrealCSharp]
SyntaxReceiver
├─ ReportDiagnostic                 // 报 Roslyn 诊断
├─ Context.AddSource("*.gen.cs")   // 真实编译输入
└─ UnrealTypeWeaver
   ├─ inject register call in ctor
   └─ inject unregister in Finalize

[Angelscript]
Raw script file
├─ Preprocessor parses macros
├─ File.GeneratedCode += "__generated ..."
└─ append to ProcessedCode
   └─ compiler consumes merged buffer
```

关键源码 [1] `Reference/UnrealCSharp/Script/SourceGenerator/UnrealTypeSourceGenerator.cs:53-105,180-207`

```csharp
// ============================================================================
// 文件: Reference/UnrealCSharp/Script/SourceGenerator/UnrealTypeSourceGenerator.cs
// 函数: Execute(GeneratorExecutionContext Context)
// 位置: Roslyn SourceGenerator 直接向编译器注入 `.gen.cs`
// ============================================================================
public void Execute(GeneratorExecutionContext Context)
{
    if (!(Context.SyntaxReceiver is UnrealTypeReceiver unrealTypeReceiver))
    {
        return;
    }

    foreach (var error in unrealTypeReceiver.Errors)
    {
        Context.ReportDiagnostic(error); // ★ 生成规则直接变成 IDE/编译器诊断
    }

    foreach (var @interface in unrealTypeReceiver.Interfaces)
    {
        var source = "";
        ...
        source += $"\n\tpublic partial class U{@interface.Name.Substring(1)} : UInterface ";
        ...
        Context.AddSource(@interface.Name + ".gen.cs", source);
        // ★ 生成物是正式的 C# 编译单元，而不是隐藏字符串
    }
}

...

if (type.Value.HasOperatorEqualTo == false)
{
    source +=
        $"\t\tpublic static bool operator ==({type.Value.Name} A, {type.Value.Name} B)\n" +
        ...
        "\t\t\treturn ReferenceEquals(A, B) || UStructImplementation.UStruct_IdenticalImplementation(StaticStruct().GarbageCollectionHandle, A.GarbageCollectionHandle, B.GarbageCollectionHandle);\n" +
        "\t\t}\n";
}

...

Context.AddSource(type.Value.NameSpace + "." + type.Value.Name + ".gen.cs", source);
// ★ operator / StaticStruct 等样板成员也通过 `.gen.cs` 进入编译
```

关键源码 [2] `Reference/UnrealCSharp/Script/Weavers/UnrealTypeWeaver.cs:100-155`

```csharp
// ============================================================================
// 文件: Reference/UnrealCSharp/Script/Weavers/UnrealTypeWeaver.cs
// 位置: 编译完成后再改写 IL，补注册与回收逻辑
// ============================================================================
var ilProcessor = constructor.Body.GetILProcessor();

ilProcessor.InsertAfter(constructor.Body.Instructions[1], Instruction.Create(OpCodes.Ldarg_0));
ilProcessor.InsertAfter(constructor.Body.Instructions[2],
    Instruction.Create(OpCodes.Ldstr, GetPathName(Type)));
ilProcessor.InsertAfter(constructor.Body.Instructions[3],
    Instruction.Create(OpCodes.Call, ModuleDefinition.ImportReference(_structRegisterImplementation)));
// ★ 构造函数里补入注册调用

if (Type.Methods.Any(Method => Method.Name == "Finalize") == false)
{
    var destructor = new MethodDefinition("Finalize",
        MethodAttributes.HideBySig | MethodAttributes.Family | MethodAttributes.Virtual,
        ModuleDefinition.TypeSystem.Void);

    destructor.Body.Instructions.Add(Instruction.Create(OpCodes.Ldarg_0));
    destructor.Body.Instructions.Add(Instruction.Create(OpCodes.Call,
        Type.Methods.FirstOrDefault(Method => Method.Name == "get_GarbageCollectionHandle")));
    destructor.Body.Instructions.Add(Instruction.Create(OpCodes.Call,
        ModuleDefinition.ImportReference(_structUnRegisterImplementation)));
    // ★ `Finalize` 里补入反注册，说明还有独立的 post-compile 阶段
}
```

关键源码 [3] `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h:138-142`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:600-722,3989-4004`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h
// 位置: 预处理文件状态
// ============================================================================
FString ProcessedCode;
TArray<FString> GeneratedCode;
// ★ 生成物先存放在编译器内部缓冲区，而不是独立源码文件

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp
// 位置: delegate 等 helper 被拼成字符串
// ============================================================================
GeneratedCode += FString::Printf(TEXT("struct %s {"), *DelegateName);
GeneratedCode += FString::Printf(TEXT("%s() __generated no_discard {}"), *DelegateName);
...
GeneratedCode += TEXT("void BindUFunction(UObject Object, const FName& BindFunctionName) __generated { _Inner.BindUFunction(Object, BindFunctionName, __DelegateSignature(this)); }");
GeneratedCode += TEXT("UObject GetUObject() const property __generated { return _Inner.GetUObject(); }");
GeneratedCode += TEXT("FName GetFunctionName() const property __generated { return _Inner.GetFunctionName(); }");
...
File.GeneratedCode.Add(GeneratedCode);
// ★ 所有 helper 先只是字符串片段

...

for (FString& Generated : File.GeneratedCode)
{
    File.ProcessedCode += TEXT("\n\n");
    File.ProcessedCode += Generated;
}
// ★ 最后统一拼接到 ProcessedCode 末尾再送入脚本编译器
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 生成物可见性 | UnrealCSharp 的生成物是 Roslyn `AddSource` 注入的 `.gen.cs` | 实现方式不同 |
| 生成后处理 | UnrealCSharp 还有独立 IL Weaver 阶段改写程序集 | 当前扫描未定位到 Angelscript 对等的 post-compile IL 阶段；实现方式不同 |
| Angelscript 生成物归属 | Angelscript 的 `GeneratedCode` 属于预处理器内部缓冲区，最终并入 `ProcessedCode` | 不是“没有生成”，而是“生成物不作为独立 IDE 文件存在” |
| IDE 侧后果 | UnrealCSharp 的类型样板和诊断天然进入 C# 编译语义；Angelscript 更偏脚本编译器内部展开 | 实现质量差异 |

这一维的核心差别不是“有没有代码生成”，而是**生成物属于谁的世界**。UnrealCSharp 让生成物进入 IDE/编译器正式输入；Angelscript 让生成物停留在脚本编译流水线内部。

### [维度 D8] 性能模式可观测性：Mono trace callbacks vs dumpable JIT/precompiled tables

前文 D8 已经分析了 AOT/JIT 路径和 GC 所有权。本轮只看“这些性能模式能否被观察和核验”。UnrealCSharp 在 runtime 初始化时先 `RegisterMonoTrace()`，把 Mono 的 log/print/error channel 全部转接到 `LogUnrealCSharp`；随后 `RegisterProfiler()` 在 `UE_TRACE_ENABLED` 且命令行带 `-trace=CSharp` 时再安装 method enter/leave/tail-call 回调。这个设计更像实时事件流，适合边跑边看，但在本轮扫描到的源码里，没有定位到对等的“JIT/AOT 状态汇总表”产物。

Angelscript 则把性能模式显式 materialize（物化）成可落盘的表。`FAngelscriptStateDump::DumpAll()` 一次性输出 `RuntimeConfig.csv`、`JITDatabase.csv`、`PrecompiledData.csv`、`StaticJITState.csv` 等多张表；`AngelscriptDumpTests.cpp` 还会把这些 CSV 列成期望文件集做自动化校验。这意味着 Angelscript 不只“支持 JIT / precompiled data”，还把当前运行到底落在哪条性能路径上沉淀成可审计工件。

```
[D8-Deep] Performance Observability

[UnrealCSharp]
FMonoDomain::Initialize
├─ RegisterMonoTrace()
│  ├─ mono_trace_set_log_handler
│  ├─ mono_trace_set_print_handler
│  └─ mono_trace_set_printerr_handler
└─ RegisterProfiler()
   └─ "-trace=CSharp" -> Mono profiler callbacks

[Angelscript]
FAngelscriptStateDump::DumpAll
├─ RuntimeConfig.csv
├─ JITDatabase.csv
├─ PrecompiledData.csv
├─ StaticJITState.csv
└─ DumpTests verify expected artifact set
```

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp:46-57,114-133,760-767,794-798`、`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoProfiler.cpp:7-24`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp
// 函数: FMonoDomain::Initialize / RegisterMonoTrace / RegisterProfiler
// 位置: Mono 侧性能观测入口
// ============================================================================
void FMonoDomain::Initialize(const FMonoDomainInitializeParams& InParams)
{
    RegisterMonoTrace();
    RegisterAssemblyPreloadHook();
    ...
    mono_debug_init(MONO_DEBUG_FORMAT_MONO);
    Domain = mono_jit_init("UnrealCSharp");
    mono_domain_set(Domain, false);
    RegisterProfiler(); // ★ profiler 在 domain 初始化后注册
    ...
    RegisterLog();
    RegisterBinding();
}

void FMonoDomain::RegisterMonoTrace()
{
    mono_trace_set_log_handler(FMonoLog::Log, nullptr);
    mono_trace_set_print_handler(FMonoLog::Printf);
    mono_trace_set_printerr_handler(FMonoLog::PrintfError);
    // ★ 把 Mono 事件流直接转进 UE 日志体系
}

void FMonoDomain::RegisterProfiler()
{
#if UE_TRACE_ENABLED
    FMonoProfiler::Register();
#endif
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoProfiler.cpp
// 函数: FMonoProfiler::Register
// 位置: 仅在显式 trace 请求下开启 method 回调
// ============================================================================
void FMonoProfiler::Register()
{
    if (FString Channels; FParse::Value(FCommandLine::Get(), TEXT("-trace="), Channels, false))
    {
        if (Channels.ToLower().Contains(TEXT("CSharp")))
        {
            if (ProfilerHandle = mono_profiler_create(nullptr); ProfilerHandle != nullptr)
            {
                mono_profiler_set_method_enter_callback(ProfilerHandle, Method_Enter);
                mono_profiler_set_method_leave_callback(ProfilerHandle, Method_Leave);
                mono_profiler_set_method_exception_leave_callback(ProfilerHandle, Method_Exception_Leave);
                mono_profiler_set_method_tail_call_callback(ProfilerHandle, Method_Tail_Call);
                mono_profiler_set_call_instrumentation_filter_callback(ProfilerHandle, Call_Instrumentation_Filter);
                // ★ 观测形式是实时 callback，而不是固定 CSV 快照
            }
        }
    }
}
```

关键源码 [2] `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Log/FMonoLog.cpp:7-18,40-63`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Log/FMonoLog.cpp
// 位置: Mono trace/log 最终汇入 UE_LOG
// ============================================================================
void FMonoLog::Printf(const char* InString, const mono_bool IsStdout)
{
    if (UE_LOG_ACTIVE(LogUnrealCSharp, Log))
    {
        UE_LOG(LogUnrealCSharp, Log, TEXT("%s"), ANSI_TO_TCHAR(InString));
    }
}

void FMonoLog::Log(const char* InLogDomain, const char* InLogLevel, const char* InMessage, const mono_bool InFatal,
                   void* InUserdata)
{
    if (InFatal || 0 == FCStringAnsi::Strncmp("error", InLogLevel, 5))
    {
        UE_LOG(LogUnrealCSharp, Fatal, TEXT("%s%s%s"), ...);
    }
    else if (0 == FCStringAnsi::Strncmp("warning", InLogLevel, 7))
    {
        UE_LOG(LogUnrealCSharp, Warning, TEXT("%s%s%s"), ...);
    }
    else
    {
        UE_LOG(LogUnrealCSharp, Log, TEXT("%s%s%s"), ...);
    }
    // ★ UnrealCSharp 当前可直证的观测主路径是日志/trace 流
}
```

关键源码 [3] `Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.h:8-22,47-52`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp:150-185,371-403,1017-1099`、`Plugins/Angelscript/Source/AngelscriptTest/Dump/AngelscriptDumpTests.cpp:45-75`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.h
// 位置: 性能与运行态可观测性被定义成正式导出接口
// ============================================================================
struct FAngelscriptStateDump
{
    static FString DumpAll(FAngelscriptEngine& Engine, const FString& OutputDir = TEXT(""));
    ...
    static FTableResult DumpRuntimeConfig(FAngelscriptEngine& Engine, const FString& OutputDir);
    static FTableResult DumpJITDatabase(FAngelscriptEngine& Engine, const FString& OutputDir);
    static FTableResult DumpPrecompiledData(FAngelscriptEngine& Engine, const FString& OutputDir);
    static FTableResult DumpStaticJITState(FAngelscriptEngine& Engine, const FString& OutputDir);
    // ★ 运行态 / JIT / 预编译状态都是一等 dump 目标
};

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp
// 函数: DumpAll / DumpRuntimeConfig / DumpJITDatabase / DumpStaticJITState
// 位置: 把性能模式导出成 CSV 工件
// ============================================================================
TableResults.Add(DumpRuntimeConfig(Engine, ResolvedOutputDir));
...
TableResults.Add(DumpJITDatabase(Engine, ResolvedOutputDir));
TableResults.Add(DumpPrecompiledData(Engine, ResolvedOutputDir));
TableResults.Add(DumpStaticJITState(Engine, ResolvedOutputDir));
// ★ DumpAll 会把关键性能路径全部物化成表

AddConfigValue(TEXT("bGeneratePrecompiledData"), BoolToString(Config.bGeneratePrecompiledData));
AddConfigValue(TEXT("bIgnorePrecompiledData"), BoolToString(Config.bIgnorePrecompiledData));
// ★ 当前运行使用哪条预编译策略，能直接落盘

Writer.AddRow({ TEXT("Functions"), LexToString(JITDatabase.Functions.Num()), FString() });
Writer.AddRow({ TEXT("FunctionLookups"), LexToString(JITDatabase.FunctionLookups.Num()), FString() });
Writer.AddRow({ TEXT("TypeInfoLookups"), LexToString(JITDatabase.TypeInfoLookups.Num()), FString() });
// ★ JIT 数据库规模可直接量化

Writer.AddRow({
    LexToString(JITFileCount),
    LexToString(FunctionsToGenerateCount),
    LexToString(SharedHeaderCount),
    LexToString(ComputedOffsetsCount)
});
// ★ StaticJIT 的生成规模也会被导出
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Dump/AngelscriptDumpTests.cpp
// 位置: 自动化测试显式核验 dump 工件集合
// ============================================================================
TArray<FString> GetExpectedPhaseOneCsvFiles()
{
    return {
        TEXT("EngineOverview.csv"),
        TEXT("RuntimeConfig.csv"),
        ...
        TEXT("JITDatabase.csv"),
        TEXT("PrecompiledData.csv"),
        TEXT("StaticJITState.csv"),
        ...
        TEXT("DumpSummary.csv")
    };
    // ★ dump 不是临时调试脚本，而是有测试守护的正式输出 contract
}
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 观测形态 | UnrealCSharp 当前可直证的是 `trace/log callback` 流；Angelscript 可导出 CSV 快照 | 实现方式不同 |
| JIT / 预编译状态核验 | Angelscript 直接导出 `JITDatabase.csv`、`PrecompiledData.csv`、`StaticJITState.csv` | 实现质量差异 |
| 自动化保障 | 本轮已直证 Angelscript 有 dump 工件测试；当前未定位到 UnrealCSharp 对等工件测试 | “未定位到”，不能直接判定“没有实现” |
| 调试取舍 | UnrealCSharp 更偏实时追踪；Angelscript 更偏事后审计与回归对比 | 实现方式不同 |

这一维说明的不是谁“更快”，而是谁**更容易证明自己现在处在哪条性能路径上**。从源码证据看，Angelscript 在可审计性上明显更完整；UnrealCSharp 则更偏运行中追踪。

---

## 深化分析 (2026-04-08 20:21:05)

### [维度 D3] 混合继承链的生成顺序：UE-visible dependency DAG vs VM-internal module dependency table

前文 D3 已经拆过 `override patch` 和 Blueprint 子类修复。本轮继续向前追一步，关注“这些可被 Blueprint 继承的动态类型到底按什么顺序生成”。源码显示 UnrealCSharp 不是碰到一个 C# 类型就立刻生成对应 `UClass`，而是先在 UE 层显式构造 `FDynamicDependencyGraph`：父类、属性类型、函数签名和 interface 依赖都会先挂到节点上，再由图调度器决定先后次序。尤其重要的是，它把 `soft reference` 视为**不阻塞生成顺序**的边，这让 Blueprint 混合继承链可以在有软依赖时继续向前推进。

Angelscript 也不是“没有依赖管理”。但本轮直证到的依赖信息位于 AngelScript VM 内部的 `moduleDependencies`，由 `asCBuilder` 在脚本编译期标记，并在 `asCModule::UpdateModule()` 中递归更新依赖模块和 bytecode 引用。这一层的职责是**脚本模块一致性**，不是 UE 侧动态 `UClass/UBlueprint` 生成调度。两者都在处理依赖，只是承担依赖排序的层不同。

```
[D3-Deep] Mixed Inheritance Ordering

[UnrealCSharp]
FClassReflection
├─ collect Parent / Property / Function / Interface deps   // 收集动态类的所有 UE 级依赖
├─ FDynamicDependencyGraph::AddNode                        // 入图
├─ soft refs do not block scheduling                       // 软引用不阻塞
└─ Generator()
   ├─ generate dependency types first                      // 先生成硬依赖
   └─ reparent + CompileBlueprint(child)                   // 再修复 Blueprint 子类

[Angelscript]
asCBuilder
├─ MarkDependency / MarkStructuralDependency               // 编译期记录模块依赖
├─ moduleDependencies carries source location              // 依赖项带源码行列
└─ asCModule::UpdateModule
   └─ refresh dependent script modules / bytecode refs     // 刷新脚本模块与字节码引用
```

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicClassGenerator.cpp:51-75`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicClassGenerator.cpp
// 函数: FDynamicClassGenerator::Generator
// 位置: 动态类入依赖图
// ============================================================================
auto Node = FDynamicDependencyGraph::FNode(
    InClassReflection->GetName(), [InClassReflection]()
    {
        Generator(InClassReflection, EDynamicClassGeneratorType::DependencyGraph);
        // ★ 真正生成动作被包进图节点，而不是立即执行
    });

if (const auto Parent = InClassReflection->GetParent())
{
    if (Parent->HasAttribute(FReflectionRegistry::Get().GetUClassAttributeClass()))
    {
        Node.Dependency(FDynamicDependencyGraph::FDependency{
            Parent->GetName(), false
        });
        // ★ 父类如果本身也是动态 UClass，就先登记成硬依赖
    }
}

FDynamicGeneratorCore::GeneratorProperty(InClassReflection, Node);
FDynamicGeneratorCore::GeneratorFunction(InClassReflection, Node);
FDynamicGeneratorCore::GeneratorInterface(InClassReflection, Node);
// ★ 属性参数、函数签名、接口实现都会继续往同一张图上补边

FDynamicGeneratorCore::AddNode(Node);
```

关键源码 [2] `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicDependencyGraph.cpp:80-184`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicDependencyGraph.cpp
// 函数: FDynamicDependencyGraph::Generator
// 位置: 依赖图调度器
// ============================================================================
for (const auto& [Dependency, bIsSoftReference] : NodeArray[NodeMap[Element]].Dependencies)
{
    if (NodeArray[NodeMap[Element]].IsPending())
    {
        if (!bIsSoftReference)
        {
            bIsCompleted = false;
            // ★ 已经在 pending 状态时，只有硬依赖会继续阻塞
        }
    }
    else if (NodeArray[NodeMap[Element]].IsInitial())
    {
        if (!bIsSoftReference)
        {
            NodeArray[NodeMap[Element]].Pending();
            NodeStack.Push(Dependency);
            bIsCompleted = false;
            // ★ 初次访问节点时，先把硬依赖压栈，soft reference 不抢调度顺序
        }
    }
}

...

for (const auto& [Dependency, bIsSoftReference] : NodeArray[NodeMap[OutNode]].Dependencies)
{
    if (!bIsSoftReference)
    {
        auto Type = FDependency::GetType(Dependency);

        if (NodeArray[NodeMap[Type]].IsInitial())
        {
            bIsCompleted = false;
            break;
        }

        if (NodeArray[NodeMap[Type]].IsPending())
        {
            bIsPending = true;
            break;
        }
    }
}

if (bIsCompleted)
{
    NodeArray[NodeMap[OutNode]].Generator();
    // ★ 只有所有硬依赖满足，节点对应的 UE 类型才真正开始生成
}
```

关键源码 [3] `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_builder.cpp:6730-6831,6865-6877` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_module.cpp:2725-2739`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_builder.cpp
// 函数: asCBuilder::MarkStructuralDependency / MarkDependency / MarkHardValueDependency
// 位置: AngelScript 编译器内部依赖记录
// ============================================================================
asCModule::FModuleDependencyInfo* ExistingInfo = module->moduleDependencies.Find(DependentOnTypeInfo->module);
if (ExistingInfo != nullptr)
{
    ExistingInfo->bIsStructuralDependency = true;
    return;
}

asCModule::FModuleDependencyInfo DependencyInfo;
DependencyInfo.bIsStructuralDependency = true;
if (node != nullptr && script != nullptr)
{
    script->ConvertPosToRowCol(node->tokenPos, &DependencyInfo.FirstLineNumber, &DependencyInfo.FirstColumn);
    // ★ 依赖项不仅记录“依赖谁”，还带触发该依赖的源码位置
}

module->moduleDependencies.Add(DependentOnTypeInfo->module, DependencyInfo);

...

if (bValueDependenciesAreHard && (Function->name != "StaticClass" || !Function->traits.GetTrait(asTRAIT_GENERATED_FUNCTION)))
    MarkHardValueDependency(Function->module, node, script);
else
    MarkDependency(Function->module, node, script);
// ★ 这里同样区分普通依赖和 hard value dependency，但作用域仍在脚本模块层
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_module.cpp
// 函数: asCModule::UpdateModule
// 位置: 依赖模块更新传播
// ============================================================================
auto OldDependencies = MoveTemp(moduleDependencies);
moduleDependencies.Reset();
moduleDependencies.Reserve(OldDependencies.Num());

for (const auto& DependencyElem : OldDependencies)
{
    asCModule* DependencyModule = DependencyElem.Key;
    UpdateModule(DependencyModule);
    moduleDependencies.Add(DependencyModule, DependencyElem.Value);
    // ★ 模块更新时会递归刷新依赖模块，然后把依赖信息重新挂回当前模块
}
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 依赖排序承担层 | UnrealCSharp 在 UE 层显式维护 `FDynamicDependencyGraph`，直接决定动态 `UClass/UInterface/UStruct` 生成先后 | 实现方式不同 |
| 软依赖策略 | UnrealCSharp 直证 `soft reference` 不阻塞图调度；当前扫描到的 Angelscript 依赖记录主要是模块级 hard/structural/value 依赖 | 实现方式不同 |
| Blueprint 后果 | UnrealCSharp 的依赖图会直接影响后续 Blueprint 子类修复与重编译链 | 实现质量差异 |
| Angelscript 现状 | Angelscript 不是“没有依赖管理”，而是把依赖维护放在 VM 内部模块/bytecode 一致性层 | 不是“没有实现”，而是“职责落点不同” |

这一维真正的差别，不是“谁依赖更多”，而是**谁把依赖排序提升到了 UE 动态类型图这一层**。UnrealCSharp 把它做成了 Blueprint 继承链前置条件；Angelscript 当前更像编译器内部一致性机制。

### [维度 D11] 交付根与装载闭包：configurable assembly probe set vs script-root cache convention

前文 D11 已经分析过 staged assembly set、`BuildIdentifier` 与 `DataGuid` 封印。本轮换个角度，只看“运行时到底从哪里找交付物”。UnrealCSharp 的路径是三段式：`Project Settings` 暴露 `PublishDirectory` 和 `AssemblyLoader`，编辑器把 publish 目录加入 `DirectoriesToAlwaysStageAsUFS`，运行时再通过 Mono 的 `AssemblyPreloadHook` 从 `ProjectContentDir()/PublishDirectory` 和 `.NET runtime` 目录探测程序集。也就是说，**装载根、打包根、探测顺序**都是显式数据结构。

Angelscript 这边则更偏约定优于配置。`GetScriptRootDirectory()` 固定把 `ProjectDir()/Script` 作为第一根，插件脚本根排在后面；启动时按这个根去加载 `Binds.Cache` 和 `PrecompiledScript[_Config].Cache`；与此同时，动态脚本对象和脚本资产被挂进 `/Script/Angelscript` 与 `/Script/AngelscriptAssets` 两个常驻 `UPackage`。因此它的交付闭包由“脚本根约定 + cache 文件命名约定 + 引擎包对象”共同组成，而不是像 UnrealCSharp 那样把程序集探测器公开成一个可替换类。

```
[D11-Deep] Delivery Root and Probe Closure

[UnrealCSharp]
Project Settings
├─ PublishDirectory                                   // 交付目录
├─ CustomProjects                                     // 额外程序集
├─ AssemblyLoader class                               // 可替换装载策略
└─ Packaging
   └─ DirectoriesToAlwaysStageAsUFS += PublishDir     // 强制打包

Runtime
└─ AssemblyPreloadHook
   ├─ probe ProjectContentDir/PublishDirectory        // 先找项目发布目录
   ├─ probe Mono net directory                        // 再找 CLR 运行时目录
   └─ load assembly bytes into Mono                   // 字节流喂给 Mono

[Angelscript]
Runtime bootstrap
├─ DiscoverScriptRoots()
│  ├─ ProjectDir/Script first                         // 项目脚本根优先
│  └─ plugin Script roots later                       // 插件脚本根后置
├─ Load Binds.Cache                                   // 绑定缓存
├─ Load PrecompiledScript[_Config].Cache              // 预编译缓存
└─ create engine-owned packages
   ├─ /Script/Angelscript
   └─ /Script/AngelscriptAssets
```

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpSetting.h:90-104,140-141` 与 `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Setting/UnrealCSharpSetting.cpp:9-22,87-93`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpSetting.h
// 位置: 运行时装载策略与发布目录都暴露为配置项
// ============================================================================
const FGameContentDirectoryPath& GetPublishDirectory() const;
...
UAssemblyLoader* GetAssemblyLoader() const;

...

UPROPERTY(Config, EditAnywhere, Category = Domain)
TSubclassOf<UAssemblyLoader> AssemblyLoader;
// ★ 装载器不是隐藏实现细节，而是正式的项目级配置点
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Setting/UnrealCSharpSetting.cpp
// 位置: 默认装载器与默认对象获取
// ============================================================================
UUnrealCSharpSetting::UUnrealCSharpSetting(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer),
      PublishDirectory(DEFAULT_PUBLISH_DIRECTORY),
      ...
      AssemblyLoader(UAssemblyLoader::StaticClass()),
      ...
{
}

UAssemblyLoader* UUnrealCSharpSetting::GetAssemblyLoader() const
{
    return Cast<UAssemblyLoader>((AssemblyLoader->IsValidLowLevelFast()
                                      ? AssemblyLoader.Get()
                                      : UAssemblyLoader::StaticClass())
        ->GetDefaultObject());
    // ★ 最终消费的是 loader class 的 CDO，允许项目替换探测逻辑
}
```

关键源码 [2] `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp:1010-1049`、`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:209-233`、`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/AssemblyLoader.cpp:6-23`、`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp:490-513`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp
// 位置: 发布产物清单与运行时探测路径
// ============================================================================
TArray<FString> FUnrealCSharpFunctionLibrary::GetFullAssemblyPublishPath()
{
    return TArrayBuilder<FString>().
           Add(GetFullUEPublishPath()).
           Add(GetFullGamePublishPath()).
           Append(GetFullCustomProjectsPublishPath()).
           Build();
    // ★ 交付物集合是 UE.dll + Game.dll + CustomProjects.dll
}

TArray<FString> FUnrealCSharpFunctionLibrary::GetAssemblyPath()
{
    return TArrayBuilder<FString>().
           Add(FPaths::ProjectContentDir() / GetPublishDirectory()).
           Add(FMonoFunctionLibrary::GetNetDirectory()).
           Build();
    // ★ probe 顺序是“项目发布目录 -> .NET 运行时目录”
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp
// 函数: FUnrealCSharpEditorModule::UpdatePackagingSettings
// 位置: 把 publish 目录加入 UFS 打包列表
// ============================================================================
if (!bIsExisted)
{
    ProjectPackagingSettings->DirectoriesToAlwaysStageAsUFS.Add({PublishDirectory});
    ProjectPackagingSettings->TryUpdateDefaultConfigFile();
    // ★ 编辑器模块主动维护打包闭包，避免运行时 probe 到的目录没被 stage
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/AssemblyLoader.cpp
// 函数: UAssemblyLoader::Load
// 位置: 程序集按探测路径顺序查找
// ============================================================================
auto AssemblyPaths = FUnrealCSharpFunctionLibrary::GetAssemblyPath();

for (const auto& AssemblyPath : AssemblyPaths)
{
    if (const auto File = FPaths::Combine(AssemblyPath, InAssemblyName) + DLL_SUFFIX;
        IFileManager::Get().FileExists(*File))
    {
        TArray<uint8> Data;
        FFileHelper::LoadFileToArray(Data, *File);
        return Data;
        // ★ 找到就直接返回字节流，后续交给 Mono preload hook
    }
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp
// 函数: FMonoDomain::AssemblyPreloadHook
// 位置: Mono 解析依赖程序集时回调项目装载器
// ============================================================================
if (const auto AssemblyLoader = FUnrealCSharpFunctionLibrary::GetAssemblyLoader())
{
    if (const auto Data = AssemblyLoader->Load(AssemblyName);
        !Data.IsEmpty())
    {
        MonoAssembly* Assembly = nullptr;
        LoadAssembly(AssemblyName, Data, nullptr, &Assembly);
        return Assembly;
        // ★ Mono 吃到的不是文件路径，而是 loader 返回的内存字节流
    }
}
```

关键源码 [3] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:781-793,1326-1363,1425-1538,1583-1600,878-882` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp:610-666`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: GetScriptRootDirectory / DiscoverScriptRoots / Startup path
// 位置: 脚本根、cache 文件与常驻 package 初始化
// ============================================================================
const auto& AllRootPaths = CurrentEngine->AllRootPaths;
return AllRootPaths.IsEmpty() ? TEXT("") : CurrentEngine->AllRootPaths[0];
// ★ 第一个脚本根恒为项目根

FString RootPath = Dependencies.ConvertRelativePathToFull(Dependencies.GetProjectDir() / TEXT("Script"));
...
DiscoveredRootPaths.Insert(RootPath, 0);
// ★ 插件脚本根会被排序后追加，项目根拥有最高优先级

FAngelscriptBindDatabase::Get().Load(GetScriptRootDirectory() / TEXT("Binds.Cache"), bGeneratePrecompiledData);
...
Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript_Development.Cache");
if (!IFileManager::Get().FileExists(*Filename))
    Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript.Cache");
// ★ Bind DB 与 PrecompiledScript*.Cache 都依赖脚本根命名约定

if (bGeneratePrecompiledData)
{
    FString Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript.Cache");
    PrecompiledData->InitFromActiveScript();
    PrecompiledData->Save(Filename);
}
// ★ 生成态与消费态共用同一套脚本根 conventions

AngelscriptPackage = NewObject<UPackage>(nullptr, FName(TEXT("/Script/Angelscript")), RF_Public | RF_Standalone | RF_MarkAsRootSet);
AssetsPackage = NewObject<UPackage>(nullptr, FName(TEXT("/Script/AngelscriptAssets")), RF_Public | RF_Standalone | RF_MarkAsRootSet);
// ★ 运行时脚本对象并不只是“文件”，还被投影成常驻 UPackage
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp
// 位置: 脚本资产挂到 `/Script/AngelscriptAssets`
// ============================================================================
auto* AssetsPackage = FAngelscriptEngine::Get().AssetsPackage;
auto* ExistingObject = FindObject<UObject>(AssetsPackage, *AssetName);

...

ExistingObject = NewObject<UObject>(
    AssetsPackage,
    AssetClass,
    *AssetName,
    RF_Public | RF_Standalone | RF_MarkAsRootSet);
// ★ 新脚本资产直接生在 engine-owned package 下

...

AssetsPackage->GetMetaData().SetValue(ExistingObject, TEXT("ScriptAssetFilename"), *Filename);
AssetsPackage->GetMetaData().SetValue(ExistingObject, TEXT("ScriptAssetLineNumber"), *FString::Printf(TEXT("%d"), LineNumber));
// ★ 文件系统脚本与包内对象之间靠 metadata 继续关联
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 运行时探测策略 | UnrealCSharp 把 `AssemblyLoader` 公开成可替换 `UObject` 类，并把 probe 路径显式拆成 `PublishDirectory + NetDirectory` | 实现方式不同 |
| 打包闭包维护 | UnrealCSharp 编辑器显式把 publish 目录加入 `DirectoriesToAlwaysStageAsUFS` | 实现质量差异 |
| Angelscript 交付根 | Angelscript 把 `ProjectDir()/Script` 作为第一脚本根，再按约定加载 `Binds.Cache` 与 `PrecompiledScript*.Cache` | 实现方式不同 |
| 交付物形态 | Angelscript 不只是文件 cache，还把脚本对象投影到 `/Script/Angelscript` 与 `/Script/AngelscriptAssets` 两个常驻包 | 不是“没有打包”，而是“交付单位同时包含文件与包对象” |

这一维更准确的结论不是“谁更好部署”，而是**谁把装载闭包做成配置化探测器，谁把它做成脚本根约定与引擎包约定**。UnrealCSharp 更可替换；Angelscript 更约定化。

### [维度 D8] 诊断保真度与性能路径：debugger-ready CLR vs strip-debug bytecode/cache

前文 D8 已经分析过 `InternalCall`、StaticJIT、GC 和 dump 可观测性。本轮补的是另一个容易被忽略的性能问题：**优化路径会不会主动牺牲调试信息**。UnrealCSharp 的答案偏保守。源码显示它在正常初始化里默认 `MONO_AOT_MODE_NONE`，如果设置里开启调试，则额外注入 `--debugger-agent` 与 `--soft-breakpoints`，但无论是否打开远程调试，都会调用 `mono_debug_init(MONO_DEBUG_FORMAT_MONO)`。也就是说，Mono runtime 默认保留了 debug format 初始化。

Angelscript 的策略更激进。生成 precompiled data 时，它会显式 `SetEngineProperty(asEP_BUILD_WITHOUT_LINE_CUES, 1)`；而底层 `SaveByteCode(stripDebugInfo)` 也把 `lineNumbers`、`sectionIdxs`、变量名和参数名这些调试字段作为可裁剪项写进字节码。它不是完全抛弃可诊断性，而是把 live symbol fidelity 换成更轻的缓存载荷，再通过 `CodeCoverage` 报告从原始脚本文件反向重建可读性。这是性能和诊断保真度之间非常明确的 trade-off。

```
[D8-Deep] Performance vs Debug Fidelity

[UnrealCSharp]
startup
├─ mono_jit_set_aot_mode(MONO_AOT_MODE_NONE)          // 默认非 AOT-only 路径
├─ optional --debugger-agent + --soft-breakpoints     // 按设置开启远程调试
└─ mono_debug_init(MONO_DEBUG_FORMAT_MONO)            // 常规启动仍初始化 debug format

[Angelscript]
generate precompiled data
├─ SetEngineProperty(asEP_BUILD_WITHOUT_LINE_CUES, 1) // 关闭 line cues
├─ SaveByteCode(stripDebugInfo)
│  ├─ omit lineNumbers / sectionIdxs                  // 可裁掉行号与段信息
│  ├─ omit variable names                             // 可裁掉变量名
│  └─ omit parameter names                            // 可裁掉参数名
└─ CodeCoverage reports from original source
   └─ PruneGeneratedCode by original line count       // 用原始源码恢复可读报告
```

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp:90-118`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp
// 函数: FMonoDomain::Initialize
// 位置: Mono 启动时的调试/执行路径选择
// ============================================================================
mono_jit_set_aot_mode(MONO_AOT_MODE_NONE);

if (const auto UnrealCSharpSetting = FUnrealCSharpFunctionLibrary::GetMutableDefaultSafe<
    UUnrealCSharpSetting>())
{
    if (UnrealCSharpSetting->IsEnableDebug())
    {
        const auto Config = FString::Printf(TEXT(
            "--debugger-agent=transport=dt_socket,server=y,suspend=n,address=%s:%d"
        ),
                                            *UnrealCSharpSetting->GetHost(),
                                            UnrealCSharpSetting->GetPort());

        char* Options[] = {
            TCHAR_TO_ANSI(TEXT("--soft-breakpoints")),
            TCHAR_TO_ANSI(*Config)
        };

        mono_jit_parse_options(sizeof(Options) / sizeof(char*), Options);
        // ★ 远程调试代理是显式配置开关
    }
}

mono_debug_init(MONO_DEBUG_FORMAT_MONO);
// ★ 即使没开 debugger-agent，runtime 仍初始化 Mono debug format
```

关键源码 [2] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1433-1447` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_restore.cpp:4051-4064,4480-4555`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 生成预编译数据时主动关闭 line cues
// ============================================================================
if (bGeneratePrecompiledData)
{
    StaticJIT = new FAngelscriptStaticJIT();
    StaticJIT->PrecompiledData = PrecompiledData;

#if AS_CAN_GENERATE_JIT
    StaticJIT->bGenerateOutputCode = bGeneratePrecompiledData;
#endif

    Engine->SetEngineProperty(asEP_BUILD_WITHOUT_LINE_CUES, 1);
    Engine->SetJITCompiler(StaticJIT);
    // ★ 性能导向路径会先把 line cues 关掉，再进入 StaticJIT / precompiled data 流程
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_restore.cpp
// 函数: asCWriter::Write
// 位置: bytecode 保存时对调试信息做条件裁剪
// ============================================================================
WriteEncodedInt64(stripDebugInfo ? 1 : 0);
// ★ 文件头先写 stripDebugInfo 标志，表明后续字段是否包含调试信息

...

if( !stripDebugInfo )
{
    asUINT length = (asUINT)func->scriptData->lineNumbers.GetLength();
    WriteEncodedInt64(length);
    ...
    // ★ lineNumbers 与 sectionIdxs 只在保留调试信息时写出
}

...

if (!stripDebugInfo)
{
    WriteEncodedInt64(bytecodeNbrByPos[func->scriptData->variables[i]->declaredAtProgramPos]);
    WriteString(&func->scriptData->variables[i]->name);
}
// ★ 变量声明位点和变量名同样可被裁掉

...

if( !stripDebugInfo )
{
    WriteString(engine->scriptSectionNames[func->scriptData->scriptSectionIdx]);
    WriteEncodedInt64(func->scriptData->declaredAt);
}

...

if( !stripDebugInfo )
{
    count = asUINT(func->parameterNames.GetLength());
    WriteEncodedInt64(count);
    ...
}
// ★ 参数名也只在 debug-preserving 路径中保留
```

关键源码 [3] `Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/AngelscriptCodeCoverage.cpp:45-64,167-199`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/AngelscriptCodeCoverage.cpp
// 位置: 通过离线报告补回诊断可读性
// ============================================================================
bool FAngelscriptCodeCoverage::CoverageEnabled()
{
    return (GetDefault<UAngelscriptTestSettings>()->bEnableCodeCoverage ||
            FParse::Param(FCommandLine::Get(), TEXT("as-enable-code-coverage")));
    // ★ 可通过配置或命令行开启覆盖率，作为压缩调试信息后的补偿观测手段
}

void FAngelscriptCodeCoverage::StopRecordingAndWriteReport(const FString& OutputDir)
{
    bRecording = false;
    UE_LOG(Angelscript, Display, TEXT("Tests complete, writing coverage report to %s."), *OutputDir);
    WriteReportHtml(OutputDir);
    WriteCoverageSummaries(OutputDir);
}

...

for (TPair<FString, FLineCoverage>& FileCoverage : FilesToCoverage)
{
    int NumLinesInOriginal;
    bOk &= WriteFileCoverageReportHtml(FileCoverage.Key, FileCoverage.Value, OutputDir, NumLinesInOriginal);

    FileCoverage.Value.PruneGeneratedCode(NumLinesInOriginal);
    // ★ 用原始源码长度剪掉生成代码行，再输出 HTML/JSON，总结性地恢复可读调试信息
}
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 默认调试底座 | UnrealCSharp 常规启动就会 `mono_debug_init`，并可按设置启用 `--debugger-agent` | 实现方式不同 |
| 优化路径是否裁调试信息 | Angelscript 在 precompiled/StaticJIT 路径上会关闭 `line cues`，并允许 `stripDebugInfo` 裁掉多类 debug 字段 | 实现方式不同 |
| 性能与诊断取舍 | UnrealCSharp 更偏“先保留 Mono debug substrate，再按需接远程调试”；Angelscript 更偏“先瘦身字节码/缓存，再用 coverage/report 补诊断” | 实现质量差异 |
| Angelscript 可观测性 | 不是“开启 precompiled data 就完全不可调”，而是 live 调试信息减少，转为离线覆盖率与报告工件 | 不是“没有实现”，而是“诊断载体变化” |

这一维补出的核心差别是：**性能路径会不会主动吞掉源码级调试线索**。从源码证据看，UnrealCSharp 更偏保留型；Angelscript 更偏裁剪型，然后再用离线工件把一部分可观测性补回来。

---

## 深化分析 (2026-04-08 23:15:54)

### [维度 D1] Host toolchain 探测归属：SDK discovery + target stamping vs in-tree native build

前文 D1 已经把模块拆分、启动时序和第三方运行时接缝讲过。本轮只补一个之前没有单独拆开的前提条件：**插件到底由谁负责发现宿主机上的编译工具链**。UnrealCSharp 的答案是“插件自己发现、自己暴露配置、再把结果写进生成出来的 C# 工程”；Angelscript 的答案则更接近“脚本 VM 和 JIT 代码生成都收在仓库与 UE 原生编译链内部”，不需要额外去扫描外部 SDK 安装。

UnrealCSharp 这里有三层闭环。第一层，`UUnrealCSharpEditorSetting` 把 `DotNetPath` 做成 project setting，并通过 `GetOptions = "GetDotNetPathArray"` 在编辑器里动态列出候选路径；第二层，`GetDotNetPathArray()` 真正去跑 `dotnet --list-sdks`，把返回文本解析成 `dotnet.exe` 路径；第三层，`FSolutionGenerator::ReplaceTargetFramework()` 再根据 `DotnetVersion` 把模板里的空 `TargetFramework` 改写成 `net8/9/10.x`。这意味着 UnrealCSharp 不只是“依赖 dotnet”，而是把 **SDK 探测、版本选择、工程落盘** 合成一个正式配置面。

Angelscript 在这条链上明显不同。`AngelscriptRuntime.Build.cs` 直接把 `ThirdParty/angelscript/source` 暴露给 UE 模块，`FAngelscriptStaticJIT::WriteOutputCode()` 也只是把脚本 lowering 结果写成 `AS_JITTED_CODE/*.cpp`。源码能直接证明的是：它依赖 UE/C++ 原生构建工具，而不是像 UnrealCSharp 那样在插件内部维护一个外部 SDK 探测器。

```
[D1-Deep] Host Toolchain Ownership

[UnrealCSharp]
Project Settings
├─ DotNetPath (GetOptions=GetDotNetPathArray)        // 编辑器配置面
├─ DotnetVersion                                     // 目标框架版本选择
└─ FSolutionGenerator
   ├─ parse `dotnet --list-sdks`                     // 发现宿主 SDK
   ├─ choose dotnet.exe                              // 落到具体可执行文件
   └─ stamp <TargetFramework>netX.Y</TargetFramework>// 写入生成工程

[Angelscript]
UE Build.cs
├─ include ThirdParty/angelscript/source             // VM 源码随仓库进入编译
└─ StaticJIT::WriteOutputCode()
   └─ emit AS_JITTED_CODE/*.cpp                      // 继续走 UE/C++ 原生工具链
```

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpEditorSetting.h:49-52,95-96`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpEditorSetting.h
// 位置: DotNet SDK 探测入口被做成编辑器配置项
// ============================================================================
const FString& GetDotNetPath() const;

UFUNCTION()
TArray<FString> GetDotNetPathArray() const;

...

UPROPERTY(Config, EditAnywhere, Category = DotNet, meta = (GetOptions = "GetDotNetPathArray"))
FString DotNetPath = TEXT("");
// ★ 设置面不是填死一个路径，而是把 SDK 探测函数接进了编辑器 UI
```

关键源码 [2] `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Setting/UnrealCSharpEditorSetting.cpp:135-228`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Setting/UnrealCSharpEditorSetting.cpp
// 函数: UUnrealCSharpEditorSetting::GetDotNetPathArray
// 位置: 实际调用 `dotnet --list-sdks` 并回推出 dotnet.exe 路径
// ============================================================================
const FString DotNet = TEXT("dotnet");
const FString Params = TEXT("--list-sdks");

FUnrealCSharpFunctionLibrary::SyncProcess(DotNet, Params, [&Result](const int32, const FString& InResult)
{
    Result = InResult;
});

...

PathString = PathString.Replace(TEXT("\\"),TEXT("/"));
PathString = PathString.EndsWith(TEXT("sdk"))
                 ? PathString.Left(PathString.Len() - 3)
                 : PathString;
PathString = PathString + TEXT("dotnet.exe");

ResultArray.AddUnique(PathString);
// ★ 这里不是只记版本号，而是把 SDK 目录反推成真正可执行的 dotnet.exe
```

关键源码 [3] `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FSolutionGenerator.cpp:220-229` 与 `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp:1333-1342`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FSolutionGenerator.cpp
// 函数: FSolutionGenerator::ReplaceTargetFramework
// 位置: 生成工程时把 DotnetVersion 落成 TargetFramework
// ============================================================================
OutResult = OutResult.Replace(TEXT("<TargetFramework></TargetFramework>"),
                              *FString::Printf(TEXT(
                                      "<TargetFramework>net%d.%d</TargetFramework>"
                                  ),
                                               FUnrealCSharpFunctionLibrary::GetDotnetVersion(),
                                               DOTNET_MINOR_VERSION
                              )
);
// ★ 最终工程不是抽象“支持 .NET”，而是显式写死到 net8/net9/net10.x
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp
// 函数: FUnrealCSharpFunctionLibrary::GetDotnetVersion
// 位置: `Latest` 会折算成当前支持的最高主版本
// ============================================================================
int32 DotnetVersion = EDotnetVersion::Latest;

if (const auto UnrealCSharpSetting = GetMutableDefaultSafe<UUnrealCSharpSetting>())
{
    DotnetVersion = UnrealCSharpSetting->GetDotnetVersion();
}

return DotnetVersion == EDotnetVersion::Latest ? DotnetVersion - 1 : DotnetVersion;
// ★ `Latest` 不是运行时探测“本机最高版本”，而是插件内部支持矩阵里的最高主版本
```

关键源码 [4] `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp:45-48` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs:20-22`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp
// 位置: dotnet 可执行文件的默认回退路径
// ============================================================================
#if PLATFORM_WINDOWS
return TEXT("C:/Program Files/dotnet/dotnet.exe");
#else
return TEXT("/usr/local/share/dotnet/dotnet");
#endif
// ★ 即使用户不填设置，也存在面向宿主机安装路径的默认假设
```

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs
// 位置: Angelscript 直接把 VM 源码接进 UE Build 图
// ============================================================================
var AngelscriptThirdPartyPath = Path.Combine(ModuleDirectory, "ThirdParty", "angelscript");
PublicIncludePaths.Add(Path.Combine(AngelscriptThirdPartyPath, "source"));
PublicIncludePaths.Add(AngelscriptThirdPartyPath);
// ★ 这里看到的是 in-tree 第三方源码接入，而不是外部 SDK 探测
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 外部工具链发现者 | UnrealCSharp 在插件内维护 `dotnet --list-sdks` 探测逻辑，并把结果暴露为项目设置 | 实现方式不同 |
| 目标框架落点 | UnrealCSharp 把 `DotnetVersion` 直接写进生成出来的 `TargetFramework` | 实现质量差异 |
| 第三方运行时接入前提 | Angelscript 通过仓库内 `ThirdParty/angelscript/source` 进入 UE 原生构建，不依赖外部 SDK 探测 | 实现方式不同 |
| 宿主机假设 | UnrealCSharp 默认假设系统存在可定位的 dotnet 安装路径 | 不是“没有实现”，而是“前置依赖外显” |

这一维新增出来的核心区别是：**UnrealCSharp 把 host toolchain 当成插件 contract 的一部分；Angelscript 则把脚本 VM 尽量收进仓库与 UE 原生编译链内部**。

### [维度 D3] 输入绑定桥接粒度：synthetic transient UFunction vs delegate-first native binding

前文 D3 已经讲过 Blueprint override、子类修复和混合继承顺序。本轮只盯一个更细的交互面：**脚本把自己接进 `UInputComponent` / `UEnhancedInputComponent` 时，到底是“复用现有 delegate API”，还是“临时制造一个可绑定的 UFunction”**。

UnrealCSharp 的做法非常激进。C# 侧先通过 `GetDynamicBindingObjectImplementation()` 取出或创建 `UBlueprintGeneratedClass::DynamicBindingObjects` 中的 binding object，再在托管侧先做一次重复绑定去重；真正进入 native 后，`FRegisterInputComponent` / `FRegisterEnhancedInputComponent` 会 `NewObject<UFunction>`，补出参数 `FProperty`，调用 `AddFunctionToFunctionMap()` 挂进类的函数表，再立即走 `FCSharpEnvironment::GetEnvironment().GetBind()->Bind(...)` 把这个新函数 patch 回 C# 方法。删除时，托管侧还会直接 `RemoveFunction()`。也就是说，**输入绑定本身会改写类的反射面**。

Angelscript 在这一点上更保守。`UInputComponentScriptMixinLibrary` 和 `Bind_UEnhancedInputComponent.cpp` 都复用 UE 已有的 dynamic delegate 签名，直接把 `Delegate.GetUObject()` 和 `Delegate.GetFunctionName()` 传回 `BindAction()` / `BindDebugKey()` 等原生 API。源码注释还明确要求“specified function must be a UFUNCTION()”。这说明在 Angelscript 路径里，输入绑定并不会临时造新的 `UFunction`；脚本方法必须先通过已有类生成/BlueprintEvent 机制进入 UE 反射面。

```
[D3-Deep] Input Binding Bridge Grain

[UnrealCSharp]
C# BindAction / BindAxis
├─ get or create DynamicBindingObject               // 绑定对象挂在 BlueprintGeneratedClass 上
├─ dedupe by action/event/function name             // 托管侧先去重
├─ native NewObject<UFunction>                      // 临时造 UFunction
├─ AddFunctionToFunctionMap + Children chain        // 改写类反射表
└─ FCSharpBind::Bind(...)                           // 立刻 patch 回 C# override

[Angelscript]
Script mixin / bind DSL
├─ build UE dynamic delegate                        // 使用现成 DynamicSignature
├─ pass Delegate.GetUObject/GetFunctionName         // 只传对象与函数名
└─ call existing UInputComponent/UEnhancedInputComponent API
   // 不在这个调用链里动态造 UFunction
```

关键源码 [1] `Reference/UnrealCSharp/Script/UE/CoreUObject/InputComponent.cs:11-44,234-257`

```csharp
// ============================================================================
// 文件: Reference/UnrealCSharp/Script/UE/CoreUObject/InputComponent.cs
// 位置: 托管侧先取 binding object、去重，再发起 native 绑定
// ============================================================================
var InputActionDelegateBinding = UInputComponentImplementation
    .UInputComponent_GetDynamicBindingObjectImplementation<UInputActionDelegateBinding>(
        InObject.GetClass().GarbageCollectionHandle,
        UInputActionDelegateBinding.StaticClass().GarbageCollectionHandle);

foreach (var InputActionDelegate in InputActionDelegateBinding.InputActionDelegateBindings)
{
    if (InputActionDelegate.InputActionName == InActionName &&
        InputActionDelegate.InputKeyEvent == InKeyEvent &&
        InputActionDelegate.FunctionNameToBind.ToString() == InAction.Method.Name)
    {
        return;
        // ★ 托管侧先按 action/event/method 三元组去重
    }
}

...

UInputComponentImplementation.UInputComponent_BindActionImplementation(...);

...

InObject.GetClass().RemoveFunction(InAction.Method.Name);
// ★ 删除绑定时直接从类上移除临时函数
```

关键源码 [2] `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Domain/InternalCall/FRegisterInputComponent.cpp:17-39,268-290` 与 `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Domain/InternalCall/FRegisterEnhancedInputComponent.cpp:42-61,67-103,149-182`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Domain/InternalCall/FRegisterInputComponent.cpp
// 位置: 取/建 DynamicBindingObject，并在需要时合成临时 UFunction
// ============================================================================
auto DynamicBindingObject = UBlueprintGeneratedClass::GetDynamicBindingObject(ThisClass, BindingClass);

if (DynamicBindingObject == nullptr)
{
    DynamicBindingObject = NewObject<UDynamicBlueprintBinding>(GetTransientPackage(), BindingClass);
    ThisClass->DynamicBindingObjects.Add(DynamicBindingObject);
    // ★ binding object 不存在就挂进 BlueprintGeneratedClass::DynamicBindingObjects
}

...

const auto Function = NewObject<UFunction>(InClass, *InFunctionName, EObjectFlags::RF_Transient);
Function->FunctionFlags = FUNC_BlueprintEvent;
InProperty(Function);
Function->Bind();
Function->StaticLink(true);
InClass->AddFunctionToFunctionMap(Function, *InFunctionName);
Function->Next = InClass->Children;
InClass->Children = Function;
Function->AddToRoot();

FCSharpEnvironment::GetEnvironment().GetBind()->Bind(..., InClass, Function);
// ★ 这条链不是“找到现有 UFunction 去绑定”，而是先造一个再补进反射表
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Domain/InternalCall/FRegisterEnhancedInputComponent.cpp
// 位置: EnhancedInput 也复用同一策略，并把返回 binding 再包装回托管对象
// ============================================================================
const auto [InputAction, TriggerEvent, FunctionNameToBind] = *FCSharpEnvironment::GetEnvironment().
    GetStruct<FBlueprintEnhancedInputActionBinding>(InBlueprintEnhancedInputActionBinding);

const auto& EnhancedInputActionEventBinding = FoundObject->BindAction(
    InputAction,
    TriggerEvent,
    ObjectToBindTo,
    FunctionNameToBind
);

BindActionFunction(ObjectToBindTo->GetClass(),
                   FCSharpEnvironment::GetEnvironment().GetString<FName>(InFunctionNameToBind));
// ★ 先让 UE 生成真实的 binding，再补临时 UFunction 与 C# 侧桥接

FCSharpEnvironment::GetEnvironment().AddBindingReference<...>(
    FoundClass, Object, &EnhancedInputActionEventBinding);
// ★ 返回给 C# 的不是裸句柄，而是再包一层 binding reference
```

关键源码 [3] `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/InputComponentScriptMixinLibrary.h:20-30,60-74` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UEnhancedInputComponent.cpp:31-45`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/InputComponentScriptMixinLibrary.h
// 位置: Angelscript 输入绑定直接走 UE 已有 dynamic delegate API
// ============================================================================
/**
 * Bind a function to be called when a key bound to this action triggers a specific keyevent.
 * Specified function must be a UFUNCTION() and takes a single FKey as its argument.
 */
UFUNCTION(BlueprintCallable)
static void BindAction(UInputComponent* Component, const FName& ActionName, EInputEvent KeyEvent,
                       const FInputActionHandlerDynamicSignature& Delegate)
{
    FInputActionBinding AB(ActionName, KeyEvent);
    AB.ActionDelegate = Delegate;
    Component->AddActionBinding(AB);
    // ★ 这里直接把 dynamic delegate 塞进现有 binding 结构，没有造新 UFunction
}

...

UFUNCTION(BlueprintCallable)
static void BindAxis(UInputComponent* Component, const FName& AxisName,
                     const FInputAxisHandlerDynamicSignature& Delegate)
{
    FInputAxisBinding AB(AxisName);
    *(TInputUnifiedDelegate<FInputAxisHandlerSignature, FInputAxisHandlerDynamicSignature>*)&AB.AxisDelegate =
        TInputUnifiedDelegate<FInputAxisHandlerSignature, FInputAxisHandlerDynamicSignature>(Delegate);
    Component->AxisBindings.Emplace(MoveTemp(AB));
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UEnhancedInputComponent.cpp
// 位置: EnhancedInput 同样只是把 delegate 的 UObject/FName 转给原生 API
// ============================================================================
UEnhancedInputComponent_.Method(
    "FEnhancedInputActionEventBinding& BindAction(const UInputAction Action, ETriggerEvent TriggerEvent, FEnhancedInputActionHandlerDynamicSignature Delegate)",
    [](UEnhancedInputComponent& InputComponent, const UInputAction* Action, ETriggerEvent TriggerEvent,
       FEnhancedInputActionHandlerDynamicSignature Delegate) -> FEnhancedInputActionEventBinding&
    {
        return InputComponent.BindAction(Action, TriggerEvent, Delegate.GetUObject(), Delegate.GetFunctionName());
        // ★ 仍旧是“把 delegate 拆回 UObject + FunctionName”，而不是改写 UClass 反射表
    });
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 输入绑定对象归属 | UnrealCSharp 把 binding object 挂进 `UBlueprintGeneratedClass::DynamicBindingObjects` 并从 C# 侧维护去重 | 实现方式不同 |
| 绑定时是否改写反射表 | UnrealCSharp 会临时 `NewObject<UFunction>` 并 `AddFunctionToFunctionMap()` | 实现方式不同 |
| Angelscript 输入绑定 | Angelscript 直接复用 UE dynamic delegate 签名，注释明确要求目标函数先是 `UFUNCTION()` | 实现方式不同 |
| 删除绑定策略 | UnrealCSharp 删除时显式 `RemoveFunction()`；Angelscript 主要移除 binding 记录或句柄 | 实现质量差异 |

这一维新补出来的核心差别是：**UnrealCSharp 为了让普通 C# 方法参与 Blueprint/输入绑定，会在运行时扩展 UClass 反射表；Angelscript 更强调“先成为 UE 已知函数，再复用现成 delegate API”**。

### [维度 D8] 执行模式决策归属：runtime-selected Mono mode vs startup-generated precompiled/JIT mode

前文 D8 已经写过 `MONO_AOT_MODE_INTERP/NONE`、StaticJIT 和 debug 保真度。本轮只回答一个更靠“所有权”的问题：**JIT/AOT/预编译到底是谁决定的，是构建线程决定，还是运行时启动代码决定**。

UnrealCSharp 的分工相对清晰。`FCSharpCompilerRunnable` 的后台线程只负责跑 `dotnet build <Game.csproj>`；真正的 `Publish` 和 DLL copy 被模板 `Game.props` 接在 `AfterBuild` / `AfterTargets="Publish"` 上。也就是说，编辑器编译器线程并不显式选择 “JIT 版” 或 “AOT 版” 产物，它只是持续产出 managed assemblies。直到运行时 `FMonoDomain::Initialize()`，平台策略才落地：iOS 强制 `MONO_AOT_MODE_INTERP` 并注册 AOT 模块，其他平台默认 `MONO_AOT_MODE_NONE`，是否启调试代理也在这里决定。**执行模式是 runtime late-binding，而不是 build-time branching**。

Angelscript 则更早作决定。`Initialize_AnyThread()` 先根据 `bGeneratePrecompiledData / bUsePrecompiledData / bScriptDevelopmentMode` 计算这次启动要走哪条执行路径；如果生成预编译数据，就创建 `StaticJIT`、关闭 `line cues`、把 JIT compiler 塞进引擎；初始化编译后又可能 `WriteOutputCode()` 写出 `AS_JITTED_CODE/*.cpp`，或者 `Save(PrecompiledScript.Cache)`，随后直接 `RequestExitWithStatus(false, 0)`。这说明在 Angelscript 里，“执行模式选择”和“产物生成行为”是同一个启动期决策。

```
[D8-Deep] Execution Mode Ownership

[UnrealCSharp]
editor compile worker
├─ dotnet build Game.csproj                         // 仅发起 build
└─ Game.props
   ├─ AfterBuildPublish -> Publish                  // 模板接管 publish
   └─ CopyDllsAfterPublish -> Content/PublishDir    // 复制 DLL

runtime startup
├─ iOS -> MONO_AOT_MODE_INTERP                      // 平台决定执行模式
├─ desktop -> MONO_AOT_MODE_NONE
└─ optional debugger-agent / mono_debug_init

[Angelscript]
engine startup
├─ compute bGeneratePrecompiledData / bUsePrecompiledData
├─ optional StaticJIT + disable line cues
├─ InitialCompile()
├─ optional WriteOutputCode() -> AS_JITTED_CODE/*.cpp
├─ optional Save(PrecompiledScript.Cache)
└─ optional RequestExitWithStatus(0)                // 生成态强制退出
```

关键源码 [1] `Reference/UnrealCSharp/Source/Compiler/Private/FCSharpCompilerRunnable.cpp:248-299` 与 `Reference/UnrealCSharp/Template/Game.props:16-27`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/Compiler/Private/FCSharpCompilerRunnable.cpp
// 函数: FCSharpCompilerRunnable::Compile
// 位置: 编辑器后台线程只显式执行 dotnet build
// ============================================================================
static auto CompileTool = FUnrealCSharpFunctionLibrary::GetDotNet();

const auto CompileParam = FString::Printf(TEXT(
    "build \"%s\" --nologo -c %s"
),
                                          *FUnrealCSharpFunctionLibrary::GetGameProjectPath(),
                                          GetSolutionConfiguration() == ESolutionConfiguration::Debug
                                              ? TEXT("Debug")
                                              : TEXT("Release")
);

FUnrealCSharpFunctionLibrary::SyncProcess(CompileTool, CompileParam, OnComplete);
// ★ compile worker 只知道 build，不直接拼 publish/AOT 参数
```

```xml
<!-- =========================================================================
     文件: Reference/UnrealCSharp/Template/Game.props
     位置: 项目模板把 build 自动接成 publish + copy
     ====================================================================== -->
<Target Name="AfterBuildPublish" AfterTargets="Build">
    <MSBuild Projects="$(ProjectPath)" Targets="Publish" Properties="Configuration=$(Configuration)" />
</Target>

<Target Name="CopyDllsAfterPublish" AfterTargets="Publish">
    <ItemGroup>
        <PublishFiles Include="$(PublishDir)*.dll" />
    </ItemGroup>
    <Copy SourceFiles="@(PublishFiles)" DestinationFolder="$(ScriptOutputPath)" />
</Target>
<!-- ★ 产物交付语义写在工程模板里，而不是 compile worker 代码里 -->
```

关键源码 [2] `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp:54-90,93-115`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp
// 函数: FMonoDomain::Initialize
// 位置: 运行时启动阶段才真正决定 Mono 执行模式
// ============================================================================
#if PLATFORM_IOS
    mono_jit_set_aot_mode(MONO_AOT_MODE_INTERP);
    mono_aot_register_module(static_cast<void**>(mono_aot_module_System_Private_CoreLib_info));
    ...
#else
    ...
    mono_jit_set_aot_mode(MONO_AOT_MODE_NONE);
#endif

if (const auto UnrealCSharpSetting = FUnrealCSharpFunctionLibrary::GetMutableDefaultSafe<UUnrealCSharpSetting>())
{
    if (UnrealCSharpSetting->IsEnableDebug())
    {
        ...
        mono_jit_parse_options(sizeof(Options) / sizeof(char*), Options);
        // ★ 调试代理同样是 runtime 初始化时再决定
    }
}

mono_debug_init(MONO_DEBUG_FORMAT_MONO);
// ★ build 阶段只产出 DLL；真正怎么执行这些 DLL，由 runtime 初始化决定
```

关键源码 [3] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1425-1447,1512-1589` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp:3499-3504,3683-3695`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 启动期统一决定 precompiled/static JIT 路径
// ============================================================================
bGeneratePrecompiledData = RuntimeConfig.bGeneratePrecompiledData;
bScriptDevelopmentMode = RuntimeConfig.bIsEditor || RuntimeConfig.bDevelopmentMode;
bUsePrecompiledData = !bGeneratePrecompiledData && !RuntimeConfig.bIgnorePrecompiledData
    && !RuntimeConfig.bRunningCommandlet && !WITH_EDITOR && !bScriptDevelopmentMode;

if (bGeneratePrecompiledData)
{
    StaticJIT = new FAngelscriptStaticJIT();
    StaticJIT->PrecompiledData = PrecompiledData;
    Engine->SetEngineProperty(asEP_BUILD_WITHOUT_LINE_CUES, 1);
    Engine->SetJITCompiler(StaticJIT);
    // ★ 这里已经把“本次启动走生成态还是消费态”定死了
}

...

if (StaticJIT != nullptr && StaticJIT->bGenerateOutputCode)
{
    StaticJIT->WriteOutputCode();
    bForcedExit = true;
}

if (bGeneratePrecompiledData)
{
    PrecompiledData->InitFromActiveScript();
    PrecompiledData->Save(Filename);
    bForcedExit = true;
}

if (bForcedExit)
{
    FPlatformMisc::RequestExitWithStatus(false, 0);
}
// ★ 生成态不是“顺便导出一下”，而是启动流程的一种正式终态
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp
// 位置: StaticJIT 输出 C++ 文件与 DataGuid 信息
// ============================================================================
FString GenDir = FPaths::RootDir() / TEXT("AS_JITTED_CODE");
FileManager.MakeDirectory(*GenDir, true);
// ★ JIT 输出物是项目根下的 C++ 文件集合

...

FString InfoContent = FString::Printf(
    TEXT("#include \"StaticJIT/StaticJITHeader.h\"\n")
    TEXT("\n")
    TEXT("AS_FORCE_LINK static const FStaticJITCompiledInfo JitInfo(FGuid(%d, %d, %d, %d));\n"),
    PrecompiledData->DataGuid.A,
    PrecompiledData->DataGuid.B,
    PrecompiledData->DataGuid.C,
    PrecompiledData->DataGuid.D
);
// ★ JIT 产物会把本次 precompiled data 的 DataGuid 一起烙进 C++ 源文件
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| build worker 职责 | UnrealCSharp 的 compile worker 只显式发起 `dotnet build`，publish/copy 由 `Game.props` 模板接管 | 实现方式不同 |
| 执行模式决定时机 | UnrealCSharp 在 `FMonoDomain::Initialize()` 按平台决定 `MONO_AOT_MODE_INTERP/NONE` | 实现方式不同 |
| 启动期模式分叉 | Angelscript 在 `Initialize_AnyThread()` 就确定本次是生成 precompiled、消费 precompiled，还是生成 StaticJIT C++ | 实现质量差异 |
| 生成态终止行为 | Angelscript 生成 `AS_JITTED_CODE` 或 `PrecompiledScript.Cache` 后会强制退出进程 | 不是“没有实现”，而是“生成流程被设计成一次性启动模式” |

这一维新增出来的关键差别是：**UnrealCSharp 把“怎么执行 DLL”留到 runtime 再决定，Angelscript 则在启动期就把“本次运行是不是生成器”与“本次脚本执行模式”合并成同一个决策**。

---

## 深化分析 (2026-04-08 23:25:27)

### [维度 D2] 接口桥接语义：generic `TScriptInterface` handle graph vs generated `UInterface` inheritance graph

前几轮 D2 主要盯的是 override patch、descriptor 和自动绑定底座。本轮只追接口：**脚本侧声明一个 interface、把它挂成属性、再把对象当成该 interface 传递时，桥接单元到底是什么**。

UnrealCSharp 的答案是“generic `TScriptInterface` + multi-reference”。`FGeneratorCore` 直接把 `FInterfaceProperty` 生成为 `TScriptInterface<...>`，`FTypeBridge` 又把 `FInterfaceProperty` 反查成 `TScriptInterface` generic instance；运行时 `FInterfacePropertyDescriptor` 和 `FRegisterScriptInterface` 则统一把 `TScriptInterface<IInterface>` 放进 `FCSharpEnvironment` 的 `MultiReference`。这说明它的接口桥接重心放在“值容器一致性”，而不是先把一整棵接口关系树压进脚本 VM。

Angelscript 则把接口语义更多压进 UE 类型图本身。预处理器先把 `interface` 改写成 `UInterface` 语义，ClassGenerator 再真正创建 `CLASS_Interface | CLASS_Abstract` 的 `UClass`，实现类上递归填充 `NewClass->Interfaces`，缺方法时直接 `ScriptCompileError()`。运行时的快速 cast 只需要问 `ObjectClass->ImplementsInterface(TargetClass)`，因为接口关系已经被固化进 UE 的 `UClass` / `FImplementedInterface` 图里。

```
[D2-Deep] Interface Bridge Ownership

[UnrealCSharp]
C# declaration
├─ FGeneratorCore -> TScriptInterface<IMyInterface>    // 声明层就是泛型接口值
├─ FTypeBridge -> generic reflection instance          // FInterfaceProperty ↔ TScriptInterface<T>
├─ FInterfacePropertyDescriptor                        // MultiReference 持有接口值
└─ FRegisterScriptInterface                            // 对象/接口句柄双向转换

[Angelscript]
script interface declaration
├─ Preprocessor -> SuperClass = UInterface             // 先修正源码级继承
├─ ClassGenerator -> CLASS_Interface | Abstract        // 生成真实 UInterface UClass
├─ NewClass->Interfaces.Add(...)                       // 实现类递归挂接口图
└─ QuickScriptInterfaceCast -> ImplementsInterface     // 运行时只消费现成接口图
```

关键源码 [1] `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:144-149`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp
// 函数: FGeneratorCore::GetPropertyType
// 位置: 把 UE `FInterfaceProperty` 生成成 C# `TScriptInterface<T>`
// ============================================================================
if (const auto InterfaceProperty = CastField<FInterfaceProperty>(Property))
{
    return FString::Printf(TEXT(
        "TScriptInterface<%s>"
    ),
        *FUnrealCSharpFunctionLibrary::GetFullInterface(InterfaceProperty->InterfaceClass));
    // ★ 接口在声明层就是泛型值，而不是额外发明一套独立语法
}
```

关键源码 [2] `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:415-430`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp
// 函数: FTypeBridge::GetClass
// 位置: `FInterfaceProperty` 反查时也回到同一个 generic 壳
// ============================================================================
const auto FoundGenericClass = FReflectionRegistry::Get().GetTScriptInterfaceClass();

const auto FoundClass = FReflectionRegistry::Get().GetClass(InProperty->InterfaceClass);

return MakeGenericTypeInstance(FoundGenericClass, FoundClass);
// ★ 这里不是“把接口当 UObject 指针”，而是显式保留 `TScriptInterface<T>` 的泛型形状
```

关键源码 [3] `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/ObjectProperty/FInterfacePropertyDescriptor.cpp:4-45` 与 `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Domain/InternalCall/FRegisterScriptInterface.cpp:11-62`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/ObjectProperty/FInterfacePropertyDescriptor.cpp
// 位置: 属性 get/set 把接口值放进 multi-reference，再回填 UE `FScriptInterface`
// ============================================================================
const auto Object = Class->NewObject();

FCSharpEnvironment::GetEnvironment().AddMultiReference<TScriptInterface<IInterface>, true, false>(
    Class, Object, Src);

...

const auto Object = SrcMulti->GetObject();

Interface->SetObject(Object);
Interface->SetInterface(Object ? Object->GetInterfaceAddress(Property->InterfaceClass) : nullptr);
// ★ managed 侧拿到的是包装对象；native 回写时再恢复 object + interface pointer 二元组
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Domain/InternalCall/FRegisterScriptInterface.cpp
// 位置: `TScriptInterface` 独立注册成脚本侧 Library class
// ============================================================================
const auto FoundObject = FCSharpEnvironment::GetEnvironment().GetObject(InObject);

const auto ScriptInterface = new TScriptInterface<IInterface>(FoundObject);

const auto Class = FReflectionRegistry::Get().GetClass(InReflectionType);

FCSharpEnvironment::GetEnvironment().AddMultiReference<TScriptInterface<IInterface>, true, false>(
    Class, InMonoObject, ScriptInterface);

...

FClassBuilder(TEXT("TScriptInterface"), NAMESPACE_LIBRARY)
    .Function("Register", RegisterImplementation)
    .Function("Identical", IdenticalImplementation)
    .Function("UnRegister", UnRegisterImplementation)
    .Function("GetObject", GetObjectImplementation);
// ★ 接口值不是附带在某个 UObject 包装类上，而是单独注册了完整生命周期 API
```

关键源码 [4] `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGeneratorCore.cpp:1126-1130` 与 `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicInterfaceGenerator.cpp:261-267,271-310`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGeneratorCore.cpp
// 位置: interface metadata 额外保留 Blueprint 限制属性
// ============================================================================
static TArray<FClassReflection*> InterfaceMetaDataAttributes = {
    ReflectionRegistry.GetConversionRootAttributeClass(),
    ReflectionRegistry.GetCannotImplementInterfaceInBlueprintAttributeClass(),
    ReflectionRegistry.GetToolTipAttributeClass()
};
// ★ `CannotImplementInterfaceInBlueprintAttribute` 被当成接口生成输入，而不是编辑器期备注
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicInterfaceGenerator.cpp
// 位置: 动态 interface 生成后会回编译所有实现它的 BlueprintGeneratedClass
// ============================================================================
const auto Class = NewObject<UClass>(InOuter, *InName.RightChop(1), RF_Public);

Class->AddToRoot();

GeneratorInterface(InNameSpace, InName, Class, InParentClass, InProcessGenerator);

...

return InBlueprintGeneratedClass->ImplementsInterface(InClass);

...

FBlueprintEditorUtils::RefreshAllNodes(Blueprint);
FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
FKismetEditorUtilities::CompileBlueprint(Blueprint, BlueprintCompileOptions);
// ★ 接口变化会显式追到 Blueprint 重新编译，说明它仍要和 UE 接口模型保持一致
```

关键源码 [5] `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:788-791,951-953,2910-2913`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp
// 位置: interface 在预处理阶段就被压成 `UInterface` 语义
// ============================================================================
if (ClassDesc->bIsInterface)
{
    // Interfaces default to UInterface as their superclass
    ClassDesc->SuperClass = TEXT("UInterface");
}

...

// Interface inheriting from C++ UInterface — strip ": UInterface"

...

if (ClassDesc->bIsInterface && ClassDesc->SuperClass == TEXT("UInterface"))
{
    ClassDesc->bSuperIsCodeClass = true;
    ClassDesc->CodeSuperClass = UInterface::StaticClass();
}
// ★ 预处理器先把源码级 interface 关系收敛成 UE 可接受的继承表达
```

关键源码 [6] `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2770-2804,3359-3365,5060-5182` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:125-137`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 位置: 真实生成 interface UClass，并把实现约束写进 `NewClass->Interfaces`
// ============================================================================
UClass* SuperClass = InterfaceDesc->CodeSuperClass;
if (SuperClass == nullptr)
    SuperClass = UInterface::StaticClass();

NewClass->SetSuperStruct(SuperClass);
NewClass->ClassFlags |= CLASS_Interface | CLASS_Abstract;
// ★ 注意这里只设置 interface/abstract，不设置 CLASS_Native

...

ImplementedInterface.Class = InterfaceClass;
ImplementedInterface.PointerOffset = 0;
ImplementedInterface.bImplementedByK2 = true;
NewClass->Interfaces.Add(ImplementedInterface);

...

if (ImplFunc == nullptr || bResolvedToInterfaceStub)
{
    FAngelscriptEngine::Get().ScriptCompileError(
        ModuleData.NewModule, ClassDesc->LineNumber,
        FString::Printf(TEXT("Class %s implements interface %s but is missing required method '%s'."),
        *ClassDesc->ClassName, *InterfaceClass->GetName(), *InterfaceFunc->GetName()));
}
// ★ 接口不只是“能声明”，实现类是否补齐方法也在 class generation 阶段被硬校验
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: runtime cast 只消费已经生成好的 UE 接口图
// ============================================================================
UObject* Object = reinterpret_cast<UObject*>(ObjectPtr);
UClass* ObjectClass = Object != nullptr ? Object->GetClass() : nullptr;
const bool bImplementsInterface = ObjectClass != nullptr && ObjectClass->ImplementsInterface(TargetClass);
return bImplementsInterface;
// ★ 运行时无需再拼 generic interface wrapper，直接复用 UE 的 `ImplementsInterface`
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 声明层接口形态 | UnrealCSharp 直接生成 `TScriptInterface<T>`；Angelscript 先把 `interface` 声明压成 `UInterface` 继承图 | 实现方式不同 |
| 运行时接口载体 | UnrealCSharp 把接口值挂进 `MultiReference<TScriptInterface<IInterface>>`；Angelscript 直接消费 `UClass::Interfaces` 与 `ImplementsInterface()` | 实现方式不同 |
| 接口一致性校验位置 | Angelscript 在 class generation 阶段显式报“缺少 required method”；本轮定位到的 UnrealCSharp 证据集中在 generic bridge、metadata 与 Blueprint reinstance | 实现质量差异，Angelscript 的接口完备性约束更直接 |
| Blueprint 限制表达 | UnrealCSharp 通过 `CannotImplementInterfaceInBlueprintAttribute` 进入 interface metadata；Angelscript 通过 `CLASS_Interface` / `bImplementedByK2` 走 UE 原生接口模型 | 实现方式不同 |

这一维新增出来的关键差别是：**UnrealCSharp 把 interface 当成“泛型值桥接问题”，Angelscript 把 interface 当成“UE 类型图构造问题”**。前者让属性/参数桥接更统一，后者让 `ImplementsInterface`、缺方法报错和 Blueprint 参与方式都落在 UE 现成机制里。

### [维度 D3] 委托执行面：helper-backed handle API vs first-class delegate value types

前几轮 D3 写的是 Blueprint override、输入绑定和事件调用边界。本轮补一个更细的交互面：**脚本拿到一个 delegate 属性时，面对的是“句柄包装对象”，还是“真正的脚本值类型”**。

UnrealCSharp 的答案偏 runtime helper。`FDelegatePropertyDescriptor` 先把 `FScriptDelegate` 包成 `FDelegateHelper`，再通过 `FGarbageCollectionHandle` 和 `AddDelegateReference()` 暴露给 managed 侧；`FRegisterDelegate` 对外提供的是 `Bind / IsBound / GenericExecute0 / PrimitiveExecute1 / ... / CompoundExecute7` 这类固定 arity API；真正执行时又回到 `FCSharpDelegateDescriptor`，最终调用 `ProcessDelegate<UObject>()`。这说明它把 delegate 视作“需要额外 marshaling contract 的 native 资源”。

Angelscript 则更像把 delegate 直接做成脚本语言的一等值类型。`Bind_Delegates.cpp` 为每个 `UDelegateFunction` 声明对应的 `ValueClass<FScriptDelegate>` / `ValueClass<FMulticastScriptDelegate>`；预处理器自动在脚本生成的 wrapper 里插入 `_Inner` 字段和 `__Evt_ExecuteDelegate(_Inner)`；运行时 `Bind_BlueprintEvent.cpp` 会在执行前验证 bound function 是否匹配；StaticJIT 生成的代码仍然落到同一个 `ProcessDelegate` / `ProcessMulticastDelegate` 入口。也就是说，**声明层、解释执行层和 JIT 层都共享同一套 delegate 物理形态**。

```
[D3-Deep] Delegate Surface Model

[UnrealCSharp]
delegate property / managed wrapper
├─ FDelegatePropertyDescriptor -> FDelegateHelper      // 先包一层 helper
├─ GCHandle / DelegateRegistry                         // 通过句柄寻址 helper
├─ FRegisterDelegate fixed-arity API                   // Bind / Execute0/1/2/3/4/6/7
└─ FCSharpDelegateDescriptor -> ProcessDelegate        // 最终再落回 UE delegate 执行

[Angelscript]
delegate declaration / generated wrapper
├─ Bind_Delegates declares value class                 // 每个 signature 都是类型
├─ Preprocessor emits _Inner + Execute/Broadcast       // 脚本表面直接持有值
├─ __Evt_ExecuteDelegate validates + dispatches        // 运行时统一入口
└─ StaticJIT emits same ProcessDelegate call           // JIT 与解释执行共享落点
```

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Domain/InternalCall/FRegisterDelegate.cpp:14-196`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Domain/InternalCall/FRegisterDelegate.cpp
// 位置: managed 侧看到的是一组固定 arity 的 delegate helper API
// ============================================================================
const auto Class = FReflectionRegistry::Get().GetClass(InReflectionType);

FCSharpBind::Bind<FDelegateHelper>(Class, InMonoObject);

...

if (const auto DelegateHelper = FCSharpEnvironment::GetEnvironment().GetDelegate<FDelegateHelper>(
    InGarbageCollectionHandle))
{
    DelegateHelper->Bind(FoundObject, FoundMethod);
}

...

FClassBuilder(TEXT("FDelegate"), NAMESPACE_LIBRARY)
    .Function("Register", RegisterImplementation)
    .Function("UnRegister", UnRegisterImplementation)
    .Function("Bind", BindImplementation)
    .Function("IsBound", IsBoundImplementation)
    .Function("UnBind", UnBindImplementation)
    .Function("Clear", ClearImplementation)
    .Function("GenericExecute0", GenericExecute0Implementation)
    .Function("PrimitiveExecute1", PrimitiveExecute1Implementation)
    .Function("CompoundExecute1", CompoundExecute1Implementation)
    .Function("GenericExecute2", GenericExecute2Implementation)
    .Function("PrimitiveExecute3", PrimitiveExecute3Implementation)
    .Function("CompoundExecute3", CompoundExecute3Implementation)
    .Function("GenericExecute4", GenericExecute4Implementation)
    .Function("GenericExecute6", GenericExecute6Implementation)
    .Function("PrimitiveExecute7", PrimitiveExecute7Implementation)
    .Function("CompoundExecute7", CompoundExecute7Implementation);
// ★ delegate 表面 API 是按参数/返回值形状拆开的 helper 调用，不是脚本层一等值类型操作符
```

关键源码 [2] `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/DelegateProperty/FDelegatePropertyDescriptor.cpp:20-49`、`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Delegate/FDelegateHelper.cpp:18-47` 与 `Reference/UnrealCSharp/Source/UnrealCSharp/Public/Reflection/Function/FCSharpDelegateDescriptor.inl:13-19,23-30`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/DelegateProperty/FDelegatePropertyDescriptor.cpp
// 位置: delegate 属性先被包装成 `FDelegateHelper`
// ============================================================================
const auto SrcDelegateHelper = FCSharpEnvironment::GetEnvironment().GetDelegate<FDelegateHelper>(
    SrcGarbageCollectionHandle);

...

const auto DelegateHelper = new FDelegateHelper(Property->GetPropertyValuePtr(InAddress),
                                                Property->SignatureFunction);

Object = Class->NewObject();

FCSharpEnvironment::GetEnvironment().AddDelegateReference(OwnerGarbageCollectionHandle, InAddress,
                                                          DelegateHelper, Class, Object);
// ★ native delegate 不直接出现在 managed 属性里，而是变成 helper + gc-handle 的组合
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Delegate/FDelegateHelper.cpp
// 位置: helper 自己再持有一个 rooted `UDelegateHandler`
// ============================================================================
DelegateHandler = NewObject<UDelegateHandler>();

DelegateHandler->AddToRoot();

DelegateHandler->Initialize(InDelegate,
                            InSignatureFunction != nullptr
                                ? InSignatureFunction
                                : DelegateHandler->GetCallBack());

...

DelegateHandler->Bind(InObject, InMethod);
// ★ 真正的绑定关系被继续下沉到 `UDelegateHandler`
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Public/Reflection/Function/FCSharpDelegateDescriptor.inl
// 位置: helper API 最终还是回落到 UE `ProcessDelegate`
// ============================================================================
const auto Params = BufferAllocator.IsValid() ? BufferAllocator->Malloc() : nullptr;

InScriptDelegate->ProcessDelegate<UObject>(Params);

PROCESS_RETURN()
// ★ `Execute1/2/3/...` 的差异主要体现在参数缓冲与回填，不在最终 native delegate 落点
```

关键源码 [3] `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.cpp:57-116,568-630,1410-1439`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.cpp
// 位置: delegate 被注册成脚本语言的一等 value type，并带签名反查能力
// ============================================================================
struct FScriptDelegateType : TAngelscriptCppType<FScriptDelegate>
{
    FString Name;
    UDelegateFunction* Function;

    FORCEINLINE UDelegateFunction* GetSignature(const FAngelscriptTypeUsage& Usage) const
    {
        ...
        return (UDelegateFunction*)UserData;
    }
};

...

Delegate_.Method("bool IsBound() const", FUNC_TRIVIAL(FAngelscriptDelegateOperations::IsBound));
Delegate_.Method("UObject GetUObject() const", FUNC_TRIVIAL(FAngelscriptDelegateOperations::GetUObject));
Delegate_.Method("FName GetFunctionName() const", FUNC_TRIVIAL(FAngelscriptDelegateOperations::GetFunctionName));
Delegate_.Method("void Clear()", FUNC_TRIVIAL(FAngelscriptDelegateOperations::Clear));
Delegate_.Method("void BindUFunction(UObject Object, const FName& FunctionName)", FUNC(FAngelscriptDelegateOperations::BindUFunction), Ops);

...

FAngelscriptBinds::BindGlobalFunction("UDelegateFunction __DelegateSignature(?& Delegate)",
    FUNC_TRIVIAL(FAngelscriptDelegateOperations::GetDelegateSignature));
// ★ delegate 自己就是类型，自动生成代码还能反查 signature 做泛型绑定
```

关键源码 [4] `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:600-699`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp
// 位置: 预处理器自动给脚本 delegate 生成 `_Inner` 字段和 Execute/Broadcast 封装
// ============================================================================
GeneratedCode += FString::Printf(TEXT("struct %s {"), *DelegateName);

if (Delegate.bIsMulticast)
{
    GeneratedCode += TEXT("_FMulticastScriptDelegate _Inner;");
}
else
{
    GeneratedCode += TEXT("_FScriptDelegate _Inner;");
}

...

GeneratedCode += TEXT(" __Evt_ExecuteDelegate(_Inner);");

...

GeneratedCode += TEXT("if (!_Inner.IsBound()) { Throw(\"Executing unbound delegate.\"); return; }");
// ★ 脚本表面直接持有 delegate 值，生成代码负责把它转成统一的 runtime 调用
```

关键源码 [5] `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp:287-329,475-486` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp:2353-2364`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp
// 位置: 运行时在执行前先验证 bound function，再统一调 `ProcessDelegate`
// ============================================================================
UObject* BoundObject = Delegate.GetUObject();
UFunction* BoundFunction = BoundObject != nullptr ? BoundObject->FindFunction(Delegate.GetFunctionName()) : nullptr;
FString ValidationError;
if (!ValidateAgainstFunction(BoundFunction, ValidationError))
{
    AbortExecution(ValidationError);
    return;
}

Delegate.ProcessDelegate<UObject>(&ArgumentBuffer[0]);

...

FAngelscriptBinds::BindGlobalFunction("void __Evt_ExecuteDelegate(const _FScriptDelegate& Delegate)",
[](FScriptDelegate& Delegate)
{
    CurrentCall().ExecuteDelegate(Delegate);
});
// ★ 解释执行入口被收敛到 `__Evt_ExecuteDelegate`
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp
// 位置: StaticJIT 生成的 native code 也复用同一个 UE delegate 落点
// ============================================================================
JITContext.Line("((FScriptDelegate*){0})->ProcessDelegate<UObject>(&l_ParmStruct[0]);", Context.ArgumentValues[0]);
...
JITContext.Line("((FMulticastScriptDelegate*){0})->ProcessMulticastDelegate<UObject>(&l_ParmStruct[0]);", Context.ArgumentValues[0]);
// ★ JIT 不是另一套 delegate ABI，而是把同一条 native 调用链提前展开成 C++
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 脚本表面模型 | UnrealCSharp 对 managed 暴露的是 `FDelegate` helper class + 固定 arity API；Angelscript 直接把 delegate 做成 value type | 实现方式不同 |
| 属性桥接落点 | UnrealCSharp 每次通过 `FDelegateHelper` / `FGarbageCollectionHandle` 间接寻址原始 delegate；Angelscript 把 `_FScriptDelegate/_FMulticastScriptDelegate` 直接放进生成代码 | 实现方式不同 |
| 执行路径统一性 | Angelscript 的预处理器、运行时 `__Evt_ExecuteDelegate` 和 StaticJIT 都汇到同一个 `ProcessDelegate` 落点；UnrealCSharp 本轮定位到的是 helper API -> descriptor -> `ProcessDelegate` 的多层桥接 | 实现质量差异，Angelscript 的 delegate 执行面更扁平 |
| 签名校验证据 | Angelscript 直证存在 `CheckAngelscriptDelegateCompatibility()` 与详细错误消息；UnrealCSharp 当前片段直证的是 helper 绑定与参数 marshaling | 实现质量差异，Angelscript 的 delegate 诊断证据更直接 |

这一维新增出来的关键差别是：**UnrealCSharp 把 delegate 当成需要额外封装和寻址的 native 资源，Angelscript 则把 delegate 提升成脚本语言的一等值类型，并让解释执行与 StaticJIT 共用同一条执行面**。

### [维度 D9] 测试基础设施：manifest 缺席 vs dedicated automation module

前几轮基本都围绕生成、桥接和运行时。本轮额外补 D9，因为它直接解释了为什么 Angelscript 的很多 watcher / hot reload / interface 结论能拿出更密的源码证据链。

当前这份 UnrealCSharp 快照里，插件清单只声明了 `Runtime / Editor / Generator / Compiler / Core / CrossVersion / Program` 七个模块，没有对等的 `Test` module。结合本轮对 `Reference/UnrealCSharp/` 的全局检索，未定位到与 `WITH_DEV_AUTOMATION_TESTS` 或 `IMPLEMENT_SIMPLE_AUTOMATION_TEST` 对应的插件内 automation case。也就是说，**至少在当前参考快照里，它更像“生成链和 IDE contract 自校验”，而不是“自带一整层插件内回归测试壳”**。

Angelscript 则把测试设施做成正式模块。`Angelscript.uplugin` 直接声明 `AngelscriptTest`；`AngelscriptTest.Build.cs` 既依赖 `AngelscriptRuntime`，也在 editor 下引入 `CQTest`、`UnrealEd`、`AngelscriptEditor`；`UAngelscriptTestSettings` / `UAngelscriptTestUserSettings` 让 hot reload 单测、test discovery 和 code coverage 都有显式配置入口；`FAngelscriptCodeCoverage` 再把 coverage 生命周期挂到 `AutomationController` 的 start/stop；最后，hot reload 和 directory watcher 都有真正的 automation regression cases，而不是只留下运行时代码路径。

```
[D9-Deep] Test Infrastructure Ownership

[UnrealCSharp]
plugin manifest
├─ Runtime / Editor / Generator / Compiler / Program
└─ no dedicated Test module in current snapshot        // 本轮未定位插件内 automation layer

[Angelscript]
AngelscriptTest module
├─ shared helpers / isolated engine utilities          // 测试辅助层
├─ runtime integration tests                           // hot reload / interface / delegate / actor
├─ editor watcher tests                                // watcher correctness 回归
└─ AutomationController hooks -> CodeCoverage          // 测试运行自动产出覆盖率
```

关键源码 [1] `Reference/UnrealCSharp/UnrealCSharp.uplugin:18-52` 与 `Plugins/Angelscript/Angelscript.uplugin:18-33`

```jsonc
// ============================================================================
// 文件: Reference/UnrealCSharp/UnrealCSharp.uplugin
// 位置: 当前参考快照的模块清单
// ============================================================================
"Modules": [
    { "Name": "UnrealCSharp", "Type": "Runtime" },
    { "Name": "UnrealCSharpEditor", "Type": "Editor" },
    { "Name": "ScriptCodeGenerator", "Type": "Editor" },
    { "Name": "Compiler", "Type": "Editor" },
    { "Name": "UnrealCSharpCore", "Type": "Runtime" },
    { "Name": "CrossVersion", "Type": "Runtime" },
    { "Name": "SourceCodeGenerator", "Type": "Program" }
]
// ★ 这里没有独立 Test module，至少 manifest 层没有把测试当成插件一等模块
```

```jsonc
// ============================================================================
// 文件: Plugins/Angelscript/Angelscript.uplugin
// 位置: Angelscript 把测试模块直接放进插件清单
// ============================================================================
"Modules": [
    { "Name": "AngelscriptRuntime", "Type": "Runtime" },
    { "Name": "AngelscriptEditor", "Type": "Editor" },
    { "Name": "AngelscriptTest", "Type": "Editor" }
]
// ★ 测试不是仓库外的脚本，而是正式参与插件生命周期的 Editor 模块
```

关键源码 [2] `Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs:23-49` 与 `Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTestModule.cpp:5-16`

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs
// 位置: 测试模块既依赖 runtime，也依赖 editor 与专用测试工具
// ============================================================================
PublicDependencyModuleNames.AddRange(new string[]
{
    "Core",
    "CoreUObject",
    "Engine",
    "GameplayTags",
    "Json",
    "JsonUtilities",
    "AngelscriptRuntime",
});

if (Target.bBuildEditor)
{
    PrivateDependencyModuleNames.AddRange(new string[]
    {
        "CQTest",
        "Networking",
        "Sockets",
        "UnrealEd",
        "AngelscriptEditor",
    });
}
// ★ 这不是“顺手写几个自动化测试”，而是专门拉起了一个覆盖 runtime + editor 的测试模块
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTestModule.cpp
// 位置: 测试模块有自己的模块边界和日志类别
// ============================================================================
IMPLEMENT_MODULE(FAngelscriptTestModule, AngelscriptTest);

DEFINE_LOG_CATEGORY_STATIC(LogAngelscriptTest, Log, All);

void FAngelscriptTestModule::StartupModule()
{
    UE_LOG(LogAngelscriptTest, Log, TEXT("AngelscriptTest module started."));
}
// ★ 测试模块本身就是插件结构的一部分，不是零散 case 文件集合
```

关键源码 [3] `Plugins/Angelscript/Source/AngelscriptRuntime/Testing/AngelscriptTestSettings.h:19-29,43-80` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/AngelscriptCodeCoverage.cpp:22-49`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Testing/AngelscriptTestSettings.h
// 位置: hot reload 单测、test discovery 和 code coverage 都有显式 settings
// ============================================================================
UPROPERTY(EditAnywhere, config, Category = UnitTests)
bool bRunUnitTestsOnHotReload = true;

UPROPERTY(EditAnywhere, config, Category = UnitTests)
int LimitNModulesToTestOnHotReload = 0;

...

UPROPERTY(EditAnywhere, config, Category = Tests)
bool bEnableTestDiscovery = true;

...

UPROPERTY(EditAnywhere, config, Category = CodeCoverage, Meta = (ConfigRestartRequired = true))
bool bEnableCodeCoverage;
// ★ 测试开关和覆盖率不是硬编码命令行，而是正式暴露到项目设置
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/AngelscriptCodeCoverage.cpp
// 位置: coverage 生命周期直接挂到 AutomationController 上
// ============================================================================
AutomationController->OnTestsAvailable().AddRaw(this, &FAngelscriptCodeCoverage::OnTestsStarting);
AutomationController->OnTestsComplete().AddRaw(this, &FAngelscriptCodeCoverage::OnTestsStopping);

...

return (GetDefault<UAngelscriptTestSettings>()->bEnableCodeCoverage ||
        FParse::Param(FCommandLine::Get(), TEXT("as-enable-code-coverage")));
// ★ code coverage 不是独立脚本，而是随 automation test run 自动启停
```

关键源码 [4] `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptNativeScriptHotReloadTests.cpp:35-55` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp:94-102,151-158,219-222`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptNativeScriptHotReloadTests.cpp
// 位置: 真正验证 `FullReload -> SoftReloadOnly` 是否走已处理路径
// ============================================================================
ECompileResult InitialCompileResult = ECompileResult::Error;
CompileModuleWithResult(&Engine, ECompileType::FullReload, ModuleName, Filename, Source, InitialCompileResult);

...

ECompileResult ReloadCompileResult = ECompileResult::Error;
CompileModuleWithResult(&Engine, ECompileType::SoftReloadOnly, ModuleName, Filename, ReloadSource, ReloadCompileResult);

...

ReloadCompileResult == ECompileResult::FullyHandled || ReloadCompileResult == ECompileResult::PartiallyHandled
// ★ 这里不是只看“能不能编译”，而是直接断言 hot reload 走的是受控处理路径
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp
// 位置: watcher 队列行为对 add/remove/folder/rename 都有回归保护
// ============================================================================
AngelscriptEditor::Private::QueueScriptFileChanges(Changes, Engine->AllRootPaths, *Engine, FileManager, ...);

TestEqual(TEXT("DirectoryWatcher.Queue.ScriptAddAndRemove should queue one added script"),
          Engine->FileChangesDetectedForReload.Num(), 1);

...

TestEqual(TEXT("DirectoryWatcher.Queue.FolderAddScansContainedScripts should queue the two script files in the new folder"),
          Engine->FileChangesDetectedForReload.Num(), 2);

...

TestEqual(TEXT("DirectoryWatcher.Queue.RenameWindowTracksRemoveAndAdd should queue one removed script"),
          Engine->FileDeletionsDetectedForReload.Num(), 1);
// ★ watcher correctness 不是靠人工回归，而是明确落成自动化断言
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 插件内测试模块席位 | UnrealCSharp manifest 中没有独立 Test module；Angelscript 明确声明 `AngelscriptTest` | 没有实现（就插件内独立测试模块而言） |
| 热重载回归验证 | Angelscript 有 `FullReload -> SoftReloadOnly` automation case；当前 UnrealCSharp 快照未定位到对等插件内 automation case | 没有实现（就当前快照可见证据而言） |
| watcher 正确性保护 | Angelscript 对 add/remove/folder/rename 队列都有测试；UnrealCSharp 前文已见 watcher 代码，但本轮未定位到对等回归层 | 实现质量差异 |
| 覆盖率生命周期 | Angelscript 把 code coverage 挂到 `AutomationController` 起停并暴露 settings；本轮未定位到 UnrealCSharp 对等 automation-coupled coverage loop | 没有实现（就测试耦合式 coverage 而言） |

这一维新增出来的关键差别是：**UnrealCSharp 的自校验重心更像“生成链和 IDE contract 本身”，Angelscript 则把“运行时行为必须被自动化反复证明”也做成了插件结构的一部分**。这也是为什么 Angelscript 的 hot reload、watcher 和 interface 结论，源码证据往往不仅有实现，还有成套回归用例。

---

## 深化分析 (2026-04-08 23:47:39)

### [维度 D1] 编辑期调度契约：compile/generator/PIE barrier vs tick-driven reload lanes

前文 D1 已经覆盖模块拆分、第三方运行时接缝和 host toolchain 归属。本轮只补一个更底层的差异：**编辑器在什么时刻允许脚本侧状态发生变化**。UnrealCSharp 把 `directory watcher -> compile -> generator -> PIE` 串成一组硬门禁；Angelscript 则把 `watcher -> queue -> Tick -> Soft/FullReload` 拆成运行时 lane，PIE 中也允许继续走受限 reload。

```
[D1-Deep] Editor-Time Scheduling Contract

[UnrealCSharp]
DirectoryChanged(.cs)
├─ filter Proxy/obj + skip while generating
├─ FileChanges queue
├─ AppActivated && !PIE && !Generating
│  └─ FCSharpCompiler::Compile(FileChanges)
├─ OnBeginGenerator -> clear Tasks + FileChanges
└─ OnPreBeginPIE -> wait until IsCompiling() == false

[Angelscript]
DirectoryWatcher(.as / folder)
├─ queue add/remove/full-reload candidates
└─ Engine.Tick()
   ├─ HasGameWorld ? SoftReloadOnly : FullReload
   ├─ consume queued changes periodically
   └─ FullReloadSuggested/Required in PIE
      ├─ soft fallback when possible
      └─ keep old code and defer full reload when required
```

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:144-161,306-365`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp
// 位置: 行 144-161, 306-365，PIE 前等待编译完成，前台激活后才消费 watcher 队列
// ============================================================================
void FEditorListener::OnPreBeginPIE(const bool bIsSimulating)
{
    bIsPIEPlaying = true;

    while (FCSharpCompiler::Get().IsCompiling())
    {
        FThreadHeartBeat::Get().HeartBeat();
        FPlatformProcess::SleepNoStats(0.0001f);
        FTSTicker::GetCoreTicker().Tick(FApp::GetDeltaTime());
        FThreadManager::Get().Tick();
        FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
    }

    // ★ 进入 PIE 前必须把编译线程和派发到 game thread 的任务清空
    FEngineListener::OnPreBeginPIE(bIsSimulating);
}

void FEditorListener::OnApplicationActivationStateChanged(const bool IsActive)
{
    if (IsActive && !FileChanges.IsEmpty())
    {
        if (!bIsPIEPlaying && !bIsGenerating)
        {
            // ★ watcher 的变更只有在“窗口激活 + 非 PIE + 非 generating”时才真正编译
            FCSharpCompiler::Get().Compile(FileChanges);
            FileChanges.Reset();
        }
    }
}

void FEditorListener::OnDirectoryChanged(const TArray<FFileChangeData>& InFileChanges)
{
    if (UnrealCSharpEditorSetting->EnableDirectoryChanged() && !bIsGenerating)
    {
        static auto IgnoreDirectories = TArray<FString>{ PROXY_NAME, TEXT("obj") };
        ...
        if (!bIsIgnored)
        {
            // ★ watcher 只积压 `.cs` 变更，不直接触发 generator
            FileChanges.Add(FileChange);
        }
    }
}
```

关键源码 [2] `Reference/UnrealCSharp/Source/Compiler/Private/FCSharpCompilerRunnable.cpp:125-140,166-193,328-343`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/Compiler/Private/FCSharpCompilerRunnable.cpp
// 位置: 行 125-140, 166-193, 328-343，compiler runnable 把任务合并成最新态
// ============================================================================
void FCSharpCompilerRunnable::EnqueueTask(const TArray<FFileChangeData>& InFileChangeData)
{
    {
        FScopeLock ScopeLock(&CriticalSection);

        if (!Tasks.IsEmpty())
        {
            Tasks.Empty();
        }

        FileChanges.Append(InFileChangeData);
        Tasks.Enqueue(true);
    }

    // ★ 新任务到来时先清空旧 task，体现 latest-state wins 策略
    Event->Trigger();
}

void FCSharpCompilerRunnable::Compile(const TFunction<void()>& InFunction)
{
    ...
    bIsCompiling = true;
    Compile();

    const auto Task = FFunctionGraphTask::CreateAndDispatchWhenReady(... ENamedThreads::GameThread);
    FTaskGraphInterface::Get().WaitUntilTaskCompletes(Task);
    // ★ 编译线程会等待 game thread 上的 generator 回调执行完，阶段严格串行
    bIsCompiling = false;
}

void FCSharpCompilerRunnable::OnBeginGenerator()
{
    bIsGenerating = true;
    Tasks.Empty();
    FileChanges.Empty();
    // ★ generator 开始时直接丢弃积压队列，避免旧 watcher 事件穿透到新一轮生成
}
```

关键源码 [3] `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp:43-89` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2729-2829,3942-3992`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp
// 位置: 行 43-89，watcher 只负责把脚本变更排队给主引擎
// ============================================================================
void QueueScriptFileChanges(...)
{
    ...
    if (AbsolutePath.EndsWith(TEXT(".as")))
    {
        if (Change.Action == FFileChangeData::EFileChangeAction::FCA_Removed)
        {
            Engine.FileDeletionsDetectedForReload.AddUnique({ AbsolutePath, RelativePath });
        }
        else
        {
            Engine.FileChangesDetectedForReload.AddUnique({ AbsolutePath, RelativePath });
        }
        // ★ watcher 只维护 add/remove 队列
        continue;
    }
    ...
}

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 行 2729-2829, 3942-3992，Tick 决定 reload lane，SoftReloadOnly 下显式降级
// ============================================================================
void FAngelscriptEngine::Tick(float DeltaTime)
{
    if (!GIsEditor || HasGameWorld())
    {
        CheckForHotReload(ECompileType::SoftReloadOnly);
    }
    else
    {
        CheckForHotReload(ECompileType::FullReload);
    }
}

case FAngelscriptClassGenerator::EReloadRequirement::FullReloadSuggested:
    if (CompileType == ECompileType::SoftReloadOnly)
    {
        // ★ PIE 中不强拆对象，而是先 soft reload，并把 full reload 延后
        bWasFullyHandled = false;
        SwapInModules(CompiledModules, DiscardedModules);
        ClassGenerator.PerformSoftReload();
    }
    ...

case FAngelscriptClassGenerator::EReloadRequirement::FullReloadRequired:
    if (CompileType == ECompileType::SoftReloadOnly)
    {
        // ★ 如果 PIE 中必须 full reload，就保留旧代码继续跑，并记录 full reload required
        bShouldSwapInModules = false;
        bFullReloadRequired = true;
    }
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| watcher 的职责边界 | UnrealCSharp 的 watcher 同时承担过滤与延迟触发职责；Angelscript watcher 基本只做排队，真正是否 reload 交给 runtime tick | 实现方式不同 |
| 阶段串行化强度 | UnrealCSharp 在 `OnPreBeginPIE()` 显式等待 `IsCompiling()` 清零，并在 `OnBeginGenerator()` 清空历史任务；Angelscript 不阻塞进入 PIE，而是在 `Tick()` 内切到 `SoftReloadOnly` lane | 实现方式不同 |
| 积压任务处理策略 | UnrealCSharp 用 `Tasks.Empty() + FileChanges.Append()` 合并到“最新态”；Angelscript 维护 add/delete/full-reload 三类队列 | 实现质量差异，Angelscript 的变更因果保留更细 |
| PIE 中的失败恢复语义 | UnrealCSharp 当前证据里主要是“PIE 前硬等待”；Angelscript 在 `SoftReloadOnly` 下明确定义了 `Suggested`/`Required` 两级回退 | 实现质量差异，Angelscript 的受限运行态降级策略更明确 |
| 最终门禁所有者 | UnrealCSharp 由 editor listener 和 generator 生命周期共同拍板；Angelscript 由 runtime tick 和 class generator reload requirement 共同拍板 | 实现方式不同 |

这一维新增出来的关键差别是：**UnrealCSharp 追求“编辑器阶段强串行”，先阻止状态穿透，再进入 PIE；Angelscript 追求“运行时 lane 化”，允许在 PIE 中继续消化变更，但把风险压进 `SoftReloadOnly` 的降级规则里**。

### [维度 D6] override 来源映射维护：incremental CodeAnalysis JSON index vs compiler-private GeneratedCode

前文 D6 已经覆盖 generated solution、Analyzer 诊断、`.gen.cs` 与 IL weaving 的分工。本轮只补一个更具体的 IDE 基础设施问题：**“某个 Blueprint override / 动态类型到底来自哪个 C# 源文件”这类来源映射，是如何持续保持新鲜的**。UnrealCSharp 的答案是把它做成独立 `CodeAnalysis` 工具，并把结果落到 JSON 索引；Angelscript 的答案则是把 generated metadata 保留在编译阶段私有结构里，必要时在 coverage 统计时主动把 generated line 剪掉。

```
[D6-Deep] Source-Origin Mapping Ownership

[UnrealCSharp]
OnCompile(file changes)
├─ FCodeAnalysis::Analysis(file)                     // 单文件模式
├─ load old Dynamic/Override JSON
├─ remove stale entry for changed file
├─ Roslyn parse attributes: Override/UClass/...
├─ rewrite DynamicFile.json / OverrideFile.json
└─ BlueprintToolBar reload map and open source

[Angelscript]
Preprocessor pass
├─ build File.GeneratedCode in memory
├─ append generated fragments to module compile input
└─ coverage prunes generated lines after source EOF
```

关键源码 [1] `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FCodeAnalysis.cpp:12-34,54-89`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FCodeAnalysis.cpp
// 位置: 行 12-34, 54-89，单文件分析与全量分析共用外部 CodeAnalysis 程序
// ============================================================================
void FCodeAnalysis::Analysis(const FString& InFile)
{
    const auto AnalysisParam = FString::Printf(TEXT(
        "true \"%s\" \"%s\""
    ),
        *FPaths::ConvertRelativePathToFull(FUnrealCSharpFunctionLibrary::GetCodeAnalysisPath()),
        *InFile
    );

    // ★ `true` 表示单文件模式：只重算当前变更文件的来源映射
    FUnrealCSharpFunctionLibrary::SyncProcess(Program, AnalysisParam, [](const int32, const FString&) {});
}

void FCodeAnalysis::Analysis()
{
    auto AnalysisParam = FString::Printf(TEXT(
        "false \"%s\" \"%s\""
    ),
        *FPaths::ConvertRelativePathToFull(FUnrealCSharpFunctionLibrary::GetCodeAnalysisPath()),
        *FPaths::ConvertRelativePathToFull(FUnrealCSharpFunctionLibrary::GetGameDirectory())
    );

    for (const auto& CustomProjectsDirectory : FUnrealCSharpFunctionLibrary::GetCustomProjectsDirectory())
    {
        AnalysisParam += FString::Printf(TEXT(" \"%s\""), *CustomProjectsDirectory);
    }

    // ★ `false` 表示全量重扫整个来源树，重建所有来源索引
    FUnrealCSharpFunctionLibrary::SyncProcess(Program, AnalysisParam, [](const int32, const FString&) {});
}
```

关键源码 [2] `Reference/UnrealCSharp/Script/CodeAnalysis/CodeAnalysis.cs:17-85,184-253,260-387`

```csharp
// ============================================================================
// 文件: Reference/UnrealCSharp/Script/CodeAnalysis/CodeAnalysis.cs
// 位置: 行 17-85, 184-253, 260-387，单文件模式先删旧索引，再重写 override/dynamic 映射
// ============================================================================
private CodeAnalysis(string[] args)
{
    _bIsSingle = bool.Parse(args[0]);

    if (_bIsSingle)
    {
        ...
        if (_overrideFunction != null && _overrideFile != null)
        {
            var Pair = _overrideFile.FirstOrDefault(pair => pair.Value == _inputFileName);

            if (Pair.Key != null)
            {
                // ★ 先移除旧文件对应入口，避免 rename/移动文件后残留陈旧映射
                _overrideFunction.Remove(Pair.Key);
                _overrideFile.Remove(Pair.Key);
            }
        }
    }
}

private void AnalysisOverride(string inFile, CompilationUnitSyntax inRoot)
{
    ...
    if (IsOverride)
    {
        _overrideFile[$"{NamespaceDeclaration.Name}.{ClassDeclaration.Identifier}"] = inFile;
        _overrideFunction[$"{NamespaceDeclaration.Name}.{ClassDeclaration.Identifier}"] = Functions;
    }
    // ★ override 类与 override 函数名会被拆成两张表
}

private void AnalysisDynamic(string inFile, CompilationUnitSyntax inRoot)
{
    if (Attribute.ToString().Equals("UClass"))
    {
        _dynamic?["DynamicClass"].Add($"{NamespaceDeclaration.Name}.{ClassDeclaration.Identifier}");
        _dynamicFile[$"{NamespaceDeclaration.Name}.{ClassDeclaration.Identifier}"] = inFile;
        return;
    }
    ...
    if (Attribute.ToString().Equals("UInterface"))
    {
        _dynamic?["DynamicInterface"].Add($"{NamespaceDeclaration.Name}.{InterfaceDeclaration.Identifier}");
        _dynamicFile[$"{NamespaceDeclaration.Name}.{InterfaceDeclaration.Identifier}"] = inFile;
        return;
    }
}

private void WriteAll()
{
    File.WriteAllText(Path.Combine(_outputPathName, DynamicFileName), JsonSerializer.Serialize(_dynamic, ...));
    File.WriteAllText(Path.Combine(_outputPathName, DynamicFileFileName), JsonSerializer.Serialize(_dynamicFile, ...));
    File.WriteAllText(Path.Combine(_outputPathName, OverrideFunctionFileName), JsonSerializer.Serialize(_overrideFunction, ...));
    File.WriteAllText(Path.Combine(_outputPathName, OverrideFileFileName), JsonSerializer.Serialize(_overrideFile, ...));
    // ★ 结果被持久化成四张 JSON 表，而不是只停留在一次编译过程的内存对象里
}
```

关键源码 [3] `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:34-55,57-83,205-252`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp
// 位置: 行 34-55, 57-83, 205-252，Blueprint 工具栏直接消费 JSON 来源索引
// ============================================================================
void FUnrealCSharpBlueprintToolBar::OnEndGenerator()
{
    SetCodeAnalysisOverrideFilesMap();
    FDynamicGenerator::SetCodeAnalysisDynamicFilesMap();
    // ★ 每次生成结束后立即刷新来源表
}

void FUnrealCSharpBlueprintToolBar::BuildAction()
{
    ...
    if (const auto OverrideFile = GetOverrideFile(); !OverrideFile.IsEmpty())
    {
        FSourceCodeNavigation::OpenSourceFile(OverrideFile);
    }
    ...
    if (const auto OverrideFile = GetOverrideFile(); !OverrideFile.IsEmpty())
    {
        FCodeAnalysis::Analysis(OverrideFile);
    }
    // ★ editor UI 不重新猜路径，而是直接回到来源索引定位文件并重跑单文件分析
}

FString FUnrealCSharpBlueprintToolBar::GetOverrideFile() const
{
    ...
    if (const auto FoundOverrideFile = CodeAnalysisOverrideFilesMap.Find(Class))
    {
        return *FoundOverrideFile;
    }

    if (const auto FoundOverrideFile = DynamicOverrideFilesMap.Find(Class))
    {
        return *FoundOverrideFile;
    }

    return {};
}
```

关键源码 [4] `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h:141-142`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:565-725`、`Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/LineCoverage.h:28-40`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h
// 位置: 行 141-142，generated code 只是 FFile 的内部数组
// ============================================================================
TArray<FString> GeneratedCode;
// ★ 元数据停留在预处理阶段的文件对象内，不是独立持久化的 IDE 索引
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp
// 位置: 行 565-725，delegate wrapper 直接生成到 File.GeneratedCode
// ============================================================================
void FAngelscriptPreprocessor::ProcessDelegates(FFile& File)
{
    ...
    GeneratedCode += FString::Printf(TEXT("struct %s {"), *DelegateName);
    ...
    GeneratedCode += TEXT(" __Evt_ExecuteDelegate(_Inner);");
    ...
    File.GeneratedCode.Add(GeneratedCode);

    // ★ 生成物会并入本次模块编译输入，但这里没有额外写出“类型 -> 文件”的持久表
    ReplaceWithBlank(File.ChunkedCode[Delegate.ChunkIndex], Delegate.StartPosInChunk, Delegate.EndPosInChunk+1);
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/LineCoverage.h
// 位置: 行 28-40，coverage 会主动裁掉生成代码的映射行
// ============================================================================
void PruneGeneratedCode(int Cutoff)
{
    for (auto It = HitCounts.CreateIterator(); It; ++It)
    {
        if (It->Key > Cutoff)
        {
            It.RemoveCurrent();
        }
    }
}
// ★ generated code 被视为编译期附属物，而不是应长期暴露给 IDE/coverage 的源文件主体
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| override/dynamic 来源是否持久化 | UnrealCSharp 把来源映射落成 `DynamicFile.json`、`OverrideFile.json` 等持久索引；Angelscript 当前可见证据里没有对等的 IDE-facing 持久来源表 | 没有实现（就持久来源索引而言） |
| 增量更新方式 | UnrealCSharp 单文件模式会先删除旧入口，再重写变更文件的 override/dynamic 记录；Angelscript 的 generated metadata 在本轮证据里是随模块重编即时再生 | 实现方式不同 |
| IDE 消费路径 | UnrealCSharp 的 Blueprint toolbar 直接读取来源表来 `OpenSourceFile()` 并触发单文件重分析；Angelscript 这一层更依赖编译期对象和运行时元数据，而非离线 JSON index | 实现方式不同 |
| generated code 在工具链中的定位 | UnrealCSharp 把“类型来源映射”当成正式工件；Angelscript 通过 `PruneGeneratedCode()` 明确把 generated line 从 coverage 视角裁掉 | 实现质量差异，二者对 generated metadata 的产品化程度不同 |

这一维新增出来的关键差别是：**UnrealCSharp 把来源映射当作 editor contract，本身就是要长期存活、可独立刷新的数据；Angelscript 则把 generated metadata 当作编译器内部过渡层，重点在正确生成和正确忽略，而不是形成一套对 IDE 暴露的持久索引**。

### [维度 D11] 交付模式反向约束热重载：publish DLL closure vs precompiled cache short-circuit

前文 D11 已经覆盖过 staging 目录、assembly probe set、兼容性封印与交付根。本轮不重复这些结论，只追一个之前没有单独拆开的反向约束：**交付物长什么样，会反过来决定“这套系统还能不能热重载”**。UnrealCSharp 的交付单元是 publish 出来的 DLL 闭包；Angelscript 的交付单元则可以切到 `PrecompiledScript.Cache`。一旦交付单元不同，热重载策略也随之分叉。

```
[D11-Deep] Delivery Artifact Back-Pressure on Reload

[UnrealCSharp]
Build
├─ AfterBuildPublish -> dotnet Publish
├─ CopyDllsAfterPublish -> Content/<PublishDirectory>
├─ UpdatePackagingSettings() stage as UFS
└─ Runtime load
   ├─ GetFullAssemblyPublishPath() -> DLL set
   ├─ AssemblyLoader::Load(name) -> bytes
   └─ collectible AssemblyLoadContext.LoadFromStream

[Angelscript]
Runtime bootstrap
├─ bUsePrecompiledData = !Editor && !DevMode ...
├─ load PrecompiledScript.Cache
├─ optional StaticJIT transpiled code attach
└─ CheckForHotReload()
   └─ if bUsedPrecompiledDataForPreprocessor -> return
```

关键源码 [1] `Reference/UnrealCSharp/Template/Game.props:16-27` 与 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:209-233`

```xml
<!-- =========================================================================
文件: Reference/UnrealCSharp/Template/Game.props
位置: 行 16-27，Build 完成后自动 Publish，并把 DLL 复制到 ScriptOutputPath
============================================================================ -->
<Target Name="AfterBuildPublish" AfterTargets="Build">
    <MSBuild Projects="$(ProjectPath)" Targets="Publish" Properties="Configuration=$(Configuration)" />
</Target>
<Target Name="CopyDllsAfterPublish" AfterTargets="Publish">
    <ItemGroup>
        <PublishFiles Include="$(PublishDir)*.dll" />
    </ItemGroup>
    <MakeDir Directories="$(ScriptOutputPath)" Condition="!Exists('$(ScriptOutputPath)')" />
    <Copy SourceFiles="@(PublishFiles)" DestinationFolder="$(ScriptOutputPath)" />
</Target>
<!-- ★ 交付物不是源码脚本，而是一组 publish 后的 DLL -->
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp
// 位置: 行 209-233，PublishDirectory 会被自动加入打包 UFS 目录
// ============================================================================
void FUnrealCSharpEditorModule::UpdatePackagingSettings()
{
    const auto PublishDirectory = FUnrealCSharpFunctionLibrary::GetPublishDirectory();
    ...
    if (!bIsExisted)
    {
        ProjectPackagingSettings->DirectoriesToAlwaysStageAsUFS.Add({PublishDirectory});
        ProjectPackagingSettings->TryUpdateDefaultConfigFile();
    }
    // ★ 编辑器把 publish 根直接声明为打包资产根，开发态和交付态使用同一组 DLL 工件
}
```

关键源码 [2] `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp:1035-1049`、`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/AssemblyLoader.cpp:6-23`、`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp:490-531,579-698,755-757`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp
// 位置: 行 1035-1049，运行时先把 assembly set 收敛成固定 DLL 列表与探测根
// ============================================================================
TArray<FString> FUnrealCSharpFunctionLibrary::GetFullAssemblyPublishPath()
{
    return TArrayBuilder<FString>().
           Add(GetFullUEPublishPath()).
           Add(GetFullGamePublishPath()).
           Append(GetFullCustomProjectsPublishPath()).
           Build();
}

TArray<FString> FUnrealCSharpFunctionLibrary::GetAssemblyPath()
{
    return TArrayBuilder<FString>().
           Add(FPaths::ProjectContentDir() / GetPublishDirectory()).
           Add(FMonoFunctionLibrary::GetNetDirectory()).
           Build();
}
// ★ 运行时装载根与交付根共用 `PublishDirectory`
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/AssemblyLoader.cpp
// 位置: 行 6-23，按程序集名从 publish 根或 .NET runtime 根读取 DLL 字节
// ============================================================================
TArray<uint8> UAssemblyLoader::Load(const FString& InAssemblyName)
{
    auto AssemblyPaths = FUnrealCSharpFunctionLibrary::GetAssemblyPath();

    for (const auto& AssemblyPath : AssemblyPaths)
    {
        if (const auto File = FPaths::Combine(AssemblyPath, InAssemblyName) + DLL_SUFFIX;
            IFileManager::Get().FileExists(*File))
        {
            TArray<uint8> Data;
            FFileHelper::LoadFileToArray(Data, *File);
            return Data;
        }
    }

    return {};
}
// ★ 热更新单元就是 DLL 字节本身，而不是解释器内一段脚本文本
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp
// 位置: 行 490-531, 579-698, 755-757，preload hook + collectible ALC 构成 DLL 装载闭环
// ============================================================================
MonoAssembly* FMonoDomain::AssemblyPreloadHook(MonoAssemblyName* InAssemblyName, char** OutAssemblyPath, void* InUserData)
{
    ...
    if (const auto AssemblyLoader = FUnrealCSharpFunctionLibrary::GetAssemblyLoader())
    {
        if (const auto Data = AssemblyLoader->Load(AssemblyName); !Data.IsEmpty())
        {
            LoadAssembly(AssemblyName, Data, nullptr, &Assembly);
            return Assembly;
        }
    }
    return nullptr;
}

void FMonoDomain::InitializeAssemblyLoadContext()
{
    ...
    auto bIsCollectible = true;
    Params[1] = Value_Box(Get_Boolean_Class(), &bIsCollectible);
    const auto AssemblyLoadContextObject = Object_Init(AssemblyLoadContextClass, 2, Params);
    AssemblyLoadContextGCHandle = GCHandle_New_V2(AssemblyLoadContextObject, false);
    // ★ Editor 下的装载上下文是 collectible 的，说明 DLL 生命周期被显式隔离
}

void FMonoDomain::LoadAssembly(const TArray<FString>& InAssemblies)
{
    ...
    const auto AlcLoadFromStreamMethod = Class_Get_Method_From_Name(AssemblyLoadContextClass, "LoadFromStream", 1);
    ...
    const auto Result = Runtime_Invoke(AlcLoadFromStreamMethod, AssemblyLoadContextObject, Params);
    ...
}

void FMonoDomain::RegisterAssemblyPreloadHook()
{
    mono_install_assembly_preload_hook(AssemblyPreloadHook, nullptr);
}
```

关键源码 [3] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1425-1456,1512-1556,1583-1603,2729-2733`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 行 1425-1456, 1512-1556, 1583-1603, 2729-2733，预编译交付态会直接短路 hot reload
// ============================================================================
bGeneratePrecompiledData = RuntimeConfig.bGeneratePrecompiledData;
bScriptDevelopmentMode = RuntimeConfig.bIsEditor || RuntimeConfig.bDevelopmentMode;
bUsePrecompiledData = !bGeneratePrecompiledData && !RuntimeConfig.bIgnorePrecompiledData
    && !RuntimeConfig.bRunningCommandlet && !WITH_EDITOR && !bScriptDevelopmentMode;
// ★ 只有离开 editor/development mode 后，运行时才会切进 precompiled delivery mode

if (bUsePrecompiledData)
{
    ...
    PrecompiledData = new FAngelscriptPrecompiledData(Engine);
    PrecompiledData->Load(Filename);
    if (!bScriptDevelopmentMode)
        PrecompiledData->bMinimizeMemoryUsage = true;
}

if (bGeneratePrecompiledData)
{
    FString Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript.Cache");
    PrecompiledData->InitFromActiveScript();
    PrecompiledData->Save(Filename);
    bForcedExit = true;
}

if (bUsedPrecompiledDataForPreprocessor)
    return;
// ★ 一旦这次启动用的是 precompiled data，CheckForHotReload() 直接退出
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 交付单元是什么 | UnrealCSharp 的交付单元是 publish 出来的 DLL 闭包，并且 editor/runtime 都围绕这组 DLL 装载；Angelscript 可切到 `PrecompiledScript.Cache` + optional transpiled code | 实现方式不同 |
| 交付态是否保留热重载 | UnrealCSharp 在本轮定位到的 publish/load 路径里没有看到“进入发布工件模式即禁 reload”的显式门；Angelscript 则在 `bUsedPrecompiledDataForPreprocessor` 时直接短路 `CheckForHotReload()` | 没有实现（就 Angelscript 的预编译交付态热重载而言） |
| 开发态与打包态工件是否同根 | UnrealCSharp 把 `PublishDirectory` 同时用于 `CopyDllsAfterPublish`、`DirectoriesToAlwaysStageAsUFS` 和 runtime probe path；Angelscript 的 precompiled cache 属于另一种独立交付格式 | 实现方式不同 |
| 装载生命周期粒度 | UnrealCSharp 用 collectible `AssemblyLoadContext` 把 DLL 生命周期显式包起来；Angelscript 预编译路径更像“整份 cache 进入引擎态后不再增量替换” | 实现质量差异，UnrealCSharp 的交付物生命周期边界更明确 |

这一维新增出来的关键差别是：**UnrealCSharp 选择让“开发时可热换的工件”和“最终打包出去的工件”尽量保持同构，代价是要长期维护 DLL 发布闭环；Angelscript 则把预编译交付态单独收束成 cache/StaticJIT 路径，一旦启用就主动放弃 hot reload，换取更确定的启动与运行契约**。

---

## 深化分析 (2026-04-09 00:04:40)

本轮不再回到前文已经展开过的 `staging`、`override patch rollback` 或 `Blueprint child repair`。新增三条此前还没单独拆开的 contract：`D1` 看“脚本 runtime 自己是否拥有一段宿主事件循环”，`D3` 看 `SparseDelegate` 这种 Blueprint 边角交互是如何被脚本层消费的，`D8` 看 delegate/event 调用时参数缓冲到底由谁持有、何时复用、哪里设上限。

### [维度 D1] 宿主调度所有权：tickable CLR synchronization context vs engine-owned tick lanes

前文 D1 已经比较过模块图谱、`CrossVersion` 与 editor 调度门禁。本轮只追一个更底层的问题：**脚本运行时有没有自己的一段“宿主事件循环（host event loop）”**。UnrealCSharp 的答案是有，而且直接挂进 UE 的 `FTickableGameObject` 体系里。`FCSharpEnvironment::Initialize()` 创建的不是裸 `MonoDomain`，而是 `FDomain`；`FDomain` 自己实现 `Tick()`，启动时通过 `SynchronizationContext.Tick` 的 unmanaged thunk 把 managed 侧同步上下文接进 native tick。

Angelscript 的主路径则更 UE-native。`FAngelscriptRuntimeModule::StartupModule()` 在 editor 下注册 `FTSTicker` 兜底 ticker；真正有 game instance 宿主时，又由 `UAngelscriptGameInstanceSubsystem::Tick()` 驱动 `FAngelscriptEngine::Tick()`。而 `FAngelscriptEngine::Tick()` 当前可见职责是 `HotReload / DebugServer / world-context invariant`，在本轮定位到的主 tick 路径里，没有看到与 `SynchronizationContext` 对等的“通用脚本任务泵”。

```
[D1-Deep] Host Tick Ownership

[UnrealCSharp]
FCSharpEnvironment
└─ FDomain : FTickableGameObject                 // CLR domain 本身进入 UE tickable 集合
   ├─ InitializeSynchronizationContext()         // 查找 managed SynchronizationContext
   ├─ Method_Get_Unmanaged_Thunk()               // 取 Tick 的 unmanaged thunk
   └─ Tick(DeltaTime)                            // 每帧 pump managed side，异常回抛 UE

[Angelscript]
RuntimeModule::StartupModule
├─ AddTicker(TickFallbackPrimaryEngine)          // editor 兜底 ticker
├─ GameInstanceSubsystem::Tick()                 // 有宿主 world 时由 subsystem 驱动
└─ FAngelscriptEngine::Tick()
   ├─ CheckForHotReload(...)                     // 主 tick 任务是 reload lane
   ├─ DebugServer->Tick()                        // 调试服务
   └─ WorldContext invariant check               // 运行时一致性检查
```

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp:54-59`、`Reference/UnrealCSharp/Source/UnrealCSharp/Public/Domain/FDomain.h:6-26`、`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Domain/FDomain.cpp:20-45,102-129`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp
// 函数: FCSharpEnvironment::Initialize
// 位置: 行 54-59，environment 直接拥有一个 FDomain，而不是把 domain 藏在更深层 helper 内
// ============================================================================
void FCSharpEnvironment::Initialize()
{
    Domain = new FDomain({
        "",
        FUnrealCSharpFunctionLibrary::GetFullAssemblyPublishPath()
    }); // ★ runtime 环境一启动就把 domain 接进来
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Domain/FDomain.cpp
// 函数: FDomain::Initialize / Tick / InitializeSynchronizationContext
// 位置: 行 20-45, 102-129，domain 自己就是 tick root，并主动 pump managed SynchronizationContext
// ============================================================================
void FDomain::Initialize(const FMonoDomainInitializeParams& InParams)
{
    FMonoDomain::Initialize(InParams);
    InitializeSynchronizationContext(); // ★ CLR 初始化后立刻接上同步上下文
}

void FDomain::Tick(const float DeltaTime)
{
    if (SynchronizationContextTick != nullptr)
    {
        MonoObject* Exception{};
        SynchronizationContextTick(DeltaTime, &Exception); // ★ 每帧调用 managed Tick thunk

        if (Exception != nullptr)
        {
            FMonoDomain::Unhandled_Exception(Exception);   // ★ 异常回抛 UE 侧
        }
    }
}

void FDomain::InitializeSynchronizationContext()
{
    if (const auto TickMethod = SynchronizationContextClass->GetMethod(
        FUNCTION_SYNCHRONIZATION_CONTEXT_TICK, 1))
    {
        SynchronizationContextTick =
            (SynchronizationContextTickType)TickMethod->Method_Get_Unmanaged_Thunk();
        // ★ 不是每帧反射找方法，而是预取 unmanaged thunk
    }
}
```

关键源码 [2] `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp:416-419`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp:13-24,186-199`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp:12-29,81-86`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2794-2845`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp
// 函数: FMonoDomain::Method_Get_Unmanaged_Thunk
// 位置: 行 416-419，tick 入口最终落到 Mono 提供的 unmanaged thunk
// ============================================================================
void* FMonoDomain::Method_Get_Unmanaged_Thunk(MonoMethod* InMonoMethod)
{
    return mono_method_get_unmanaged_thunk(InMonoMethod); // ★ managed Tick 被提前编成 native callable 入口
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp
// 函数: FAngelscriptRuntimeModule::StartupModule / TickFallbackPrimaryEngine
// 位置: 行 13-24, 186-199，editor 兜底 ticker 只负责把时钟送到 engine
// ============================================================================
void FAngelscriptRuntimeModule::StartupModule()
{
    if (GIsEditor)
    {
        FallbackTickHandle = FTSTicker::GetCoreTicker().AddTicker(
            FTickerDelegate::CreateRaw(this, &FAngelscriptRuntimeModule::TickFallbackPrimaryEngine));
    }
}

bool FAngelscriptRuntimeModule::TickFallbackPrimaryEngine(float DeltaTime)
{
    if (!UAngelscriptGameInstanceSubsystem::HasAnyTickOwner())
    {
        if (FAngelscriptEngine* CurrentEngine = FAngelscriptEngine::TryGetCurrentEngine())
        {
            if (CurrentEngine->ShouldTick())
            {
                CurrentEngine->Tick(DeltaTime); // ★ ticker 不直接跑脚本任务，只把 tick 转发给 engine
            }
        }
    }

    return true;
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp
// 函数: UAngelscriptGameInstanceSubsystem::Initialize / Tick
// 位置: 行 12-29, 81-86，真正有 world 宿主时由 subsystem 成为 tick owner
// ============================================================================
void UAngelscriptGameInstanceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    ...
    if (PrimaryEngine != nullptr)
    {
        ++ActiveTickOwners; // ★ engine tick 的所有权交给 subsystem 计数管理
    }
}

void UAngelscriptGameInstanceSubsystem::Tick(float DeltaTime)
{
    if (PrimaryEngine != nullptr && PrimaryEngine->ShouldTick())
    {
        PrimaryEngine->Tick(DeltaTime); // ★ subsystem 只驱动 engine.tick
    }
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: FAngelscriptEngine::Tick
// 位置: 行 2794-2845，engine tick 的当前职责集中在 reload/debug/invariant
// ============================================================================
void FAngelscriptEngine::Tick(float DeltaTime)
{
#if AS_CAN_HOTRELOAD
    if (bScriptDevelopmentMode)
    {
        ...
        if (!GIsEditor || HasGameWorld())
        {
            CheckForHotReload(ECompileType::SoftReloadOnly);
        }
        else
        {
            CheckForHotReload(ECompileType::FullReload);
        }
    }
#endif

#if WITH_AS_DEBUGSERVER
    if (DebugServer != nullptr)
        DebugServer->Tick();
#endif

    UE_CLOG(GAmbientWorldContext != nullptr, Angelscript, Fatal,
        TEXT("Angelscript world context was improperly restored after use!"));
}
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| tick 根节点归属 | UnrealCSharp 让 `FDomain` 本身成为 `FTickableGameObject`；Angelscript 由 `RuntimeModule ticker -> subsystem -> engine` 三段 native 路径驱动 | 实现方式不同 |
| 通用脚本任务泵 | UnrealCSharp 在主路径中显式接入 `SynchronizationContext.Tick`；Angelscript 在本轮定位到的主 tick 路径中没有对等的通用脚本任务泵 | 没有实现（就主 tick 路径中的通用脚本任务泵而言） |
| tick 内核心职责 | UnrealCSharp tick 负责 pump managed sync context 并上抛异常；Angelscript tick 负责 `HotReload / DebugServer / invariant` | 实现方式不同 |
| 反射开销处理 | UnrealCSharp 预取 `Method_Get_Unmanaged_Thunk()`；Angelscript 当前 tick 路径没有对等的 managed thunk contract | 实现质量差异，UnrealCSharp 的脚本调度入口更显式 |

这一维新增出来的关键差别是：**UnrealCSharp 不只是“把 CLR 嵌进 UE”，而是把 managed 同步上下文也作为一个一等 tick participant 接进宿主循环；Angelscript 则保持“UE 先 tick native engine，engine 再决定是否做 reload/debug 检查”的 native-first 节奏**。

### [维度 D3] Sparse delegate 交互：lazy dummy materialization vs owner-resolved first-class value

前文 D3 已经写过普通 delegate/multicast 的表面 API。本轮只补 `SparseDelegate` 这个更边角、但很能体现设计取舍的交互面。UnrealCSharp 没有为 sparse delegate 再单开一条脚本语义链；它的做法是把 `FMulticastSparseDelegateProperty` 也纳入通用 property descriptor 工厂，然后在真正取值时，如果 sparse slot 还没有实体 `FMulticastScriptDelegate`，就先塞一个 `Dummy` `FScriptDelegate` 进去，把它物化成普通 multicast 视图，再交给 `FMulticastDelegateHelper` 统一包装。

Angelscript 则显式保留 sparse 语义。`FScriptSparseDelegateType` 直接把 `USparseDelegateFunction` 变成脚本类型的签名来源，脚本侧值类型就是 `FSparseDelegate`；`AddUFunction / Clear / Unbind*` 每次都通过 `FSparseDelegateStorage::ResolveSparseOwner()` 去找 owner，执行时 `CallSparseDelegate()` 也重新到 sparse storage 里拿真实 multicast delegate。这意味着 sparse 的“owner + name + signature”三元语义并没有在入口处被抹平成普通 multicast 指针。

```
[D3-Deep] Sparse Delegate Interop

[UnrealCSharp]
FPropertyDescriptor::Factory
└─ FMulticastSparseDelegatePropertyDescriptor
   ├─ GetMulticastDelegate()
   │  ├─ if null -> create FMulticastScriptDelegate
   │  ├─ add ScriptDelegate("Dummy")
   │  └─ SetMulticastDelegate()                  // 先把 sparse slot 物化成普通 multicast view
   └─ NewRef/NewWeakRef -> FMulticastDelegateHelper

[Angelscript]
DeclareSparseDelegate()
├─ Register FScriptSparseDelegateType            // 类型系统里保留 sparse 签名
├─ CreateProperty() -> FMulticastSparseDelegateProperty
└─ Operations
   ├─ ResolveSparseOwner(...)                    // 每次操作时解析 owner
   ├─ __Internal_AddUnique / __Internal_Clear
   └─ CallSparseDelegate -> GetMulticastDelegate(owner, name)
```

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/FPropertyDescriptor.cpp:85-111`、`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/DelegateProperty/FMulticastSparseDelegatePropertyDescriptor.cpp:3-22`、`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/DelegateProperty/FMulticastDelegatePropertyDescriptor.cpp:20-36,44-76`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/FPropertyDescriptor.cpp
// 函数: FPropertyDescriptor::Factory
// 位置: 行 85-111，sparse delegate 被纳入通用 property descriptor 工厂
// ============================================================================
NEW_PROPERTY_DESCRIPTOR(FDelegateProperty)
...
NEW_PROPERTY_DESCRIPTOR(FMulticastInlineDelegateProperty)
NEW_PROPERTY_DESCRIPTOR(FMulticastSparseDelegateProperty)
NEW_PROPERTY_DESCRIPTOR(FMulticastDelegateProperty)
// ★ 对脚本层来说，sparse 并没有先被提升成独立语义层，而是先被放进统一 descriptor 体系
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/DelegateProperty/FMulticastSparseDelegatePropertyDescriptor.cpp
// 函数: FMulticastSparseDelegatePropertyDescriptor::GetMulticastDelegate
// 位置: 行 3-22，空 sparse storage 会先被塞一个 Dummy 条目
// ============================================================================
const FMulticastScriptDelegate* FMulticastSparseDelegatePropertyDescriptor::GetMulticastDelegate(void* InAddress) const
{
    auto MulticastDelegate = Property->GetMulticastDelegate(InAddress);

    if (MulticastDelegate == nullptr)
    {
        FMulticastScriptDelegate MulticastScriptDelegate;
        FScriptDelegate ScriptDelegate;
        ScriptDelegate.BindUFunction(nullptr, TEXT("Dummy"));
        MulticastScriptDelegate.Add(ScriptDelegate);
        Property->SetMulticastDelegate(InAddress, MulticastScriptDelegate);
        MulticastDelegate = Property->GetMulticastDelegate(InAddress);
        // ★ 先制造一个占位 multicast，再把 sparse storage 暴露成普通 delegate 视图
    }

    return MulticastDelegate;
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/DelegateProperty/FMulticastDelegatePropertyDescriptor.cpp
// 函数: FMulticastDelegatePropertyDescriptor::Set / NewRef / NewWeakRef
// 位置: 行 20-36, 44-76，后续统一落到 helper + delegate registry
// ============================================================================
void FMulticastDelegatePropertyDescriptor::Set(void* Src, void* Dest) const
{
    const auto SrcMulticastDelegateHelper =
        FCSharpEnvironment::GetEnvironment().GetDelegate<FMulticastDelegateHelper>(SrcGarbageCollectionHandle);
    ...
    ScriptDelegate.BindUFunction(SrcMulticastDelegateHelper->GetUObject(),
                                 SrcMulticastDelegateHelper->GetFunctionName());
    MulticastScriptDelegate->Add(ScriptDelegate);
}

MonoObject* FMulticastDelegatePropertyDescriptor::NewRef(void* InAddress) const
{
    ...
    const auto MulticastDelegateHelper = new FMulticastDelegateHelper(
        const_cast<FMulticastScriptDelegate*>(GetMulticastDelegate(InAddress)),
        Property->SignatureFunction);
    ...
    FCSharpEnvironment::GetEnvironment().AddDelegateReference(...);
    // ★ 一旦 sparse slot 被物化，后续流程就和普通 multicast helper 一样了
}
```

关键源码 [2] `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.cpp:1125-1171,1218-1328`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp:709-746`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.cpp
// 函数: FScriptSparseDelegateType::CreateProperty / DeclareSparseDelegate / DeclareSparseDelegateOperations
// 位置: 行 1125-1171, 1218-1328，sparse delegate 在 Angelscript 里是 first-class value type
// ============================================================================
struct FScriptSparseDelegateType : public FAngelscriptType
{
    ...
    FProperty* CreateProperty(const FAngelscriptTypeUsage& Usage, const FPropertyParams& Params) const override
    {
        auto* Prop = new FMulticastSparseDelegateProperty(Params.Outer, Params.PropertyName, RF_Public);
        Prop->SignatureFunction = GetSignature(Usage);
        Prop->SetPropertyFlags(CPF_BlueprintAssignable | CPF_BlueprintCallable);
        return Prop; // ★ sparse 属性创建时仍保留 SignatureFunction
    }
    ...
};

void DeclareSparseDelegate(USparseDelegateFunction* Function)
{
    FAngelscriptType::Register(MakeShared<FScriptSparseDelegateType>(Decl, Function));
    auto Delegate_ = FAngelscriptBinds::ValueClass<FSparseDelegate>(Decl, FBindFlags());
    // ★ 脚本侧拿到的值类型就是 FSparseDelegate，不是普通 multicast 包装器
}

void AddSparseDelegateUFunction(FSparseDelegate* Delegate, asCScriptFunction* ScriptFunction,
    UObject* InObject, const FName& InFunctionName)
{
    ...
    USparseDelegateFunction* SparseDelegateFunc = CastChecked<USparseDelegateFunction>(Ops->SignatureFunction);
    UObject* OwningObject = FSparseDelegateStorage::ResolveSparseOwner(
        *Delegate, SparseDelegateFunc->OwningClassName, SparseDelegateFunc->DelegateName);
    Delegate->__Internal_AddUnique(OwningObject, SparseDelegateFunc->DelegateName, InnerDelegate);
    // ★ 每次绑定都显式解析 sparse owner，而不是隐藏在 property getter 里
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp
// 函数: CallSparseDelegate
// 位置: 行 709-746，执行期再次从 sparse storage 拿真实 multicast delegate
// ============================================================================
void CallSparseDelegate(asIScriptGeneric* InGeneric)
{
    ...
    FSparseDelegate& ScriptDelegate = *(FSparseDelegate*)Object;
    if (!ScriptDelegate.IsBound())
        return;

    ...
    USparseDelegateFunction* SparseDelegateFunc = CastChecked<USparseDelegateFunction>(Sig->UnrealFunction);
    UObject* OwningObject = FSparseDelegateStorage::ResolveSparseOwner(
        ScriptDelegate, SparseDelegateFunc->OwningClassName, SparseDelegateFunc->DelegateName);
    FMulticastScriptDelegate* MulticastDelegate =
        FSparseDelegateStorage::GetMulticastDelegate(OwningObject, SparseDelegateFunc->DelegateName);

    if (MulticastDelegate != nullptr)
    {
        Call.ExecuteMulticastDelegate(*MulticastDelegate);
    }
    else
    {
        FMulticastScriptDelegate EmptyDelegate;
        Call.ExecuteMulticastDelegate(EmptyDelegate);
    }
    // ★ 稀疏存储语义一直保留到调用最后一步
}
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 脚本侧看到的抽象 | UnrealCSharp 最终暴露的是 `FMulticastDelegateHelper` 统一包装器；Angelscript 直接暴露 `FSparseDelegate` first-class value type | 实现方式不同 |
| 稀疏 owner 语义在哪里保留 | UnrealCSharp 在本轮定位到的 sparse 路径里先做 `Dummy` 物化，再走普通 multicast helper；Angelscript 始终通过 `ResolveSparseOwner()` 显式保留 owner 语义 | 实现质量差异，Angelscript 的 sparse 语义更显式 |
| 空存储初始化策略 | UnrealCSharp 用 `Dummy` 条目把空 sparse slot 转成可包装视图；Angelscript 执行期若找不到真实 multicast delegate，则显式走空 delegate 分支 | 实现方式不同 |
| 属性签名来源 | UnrealCSharp 把 `FMulticastSparseDelegateProperty` 收进统一 descriptor 工厂；Angelscript 在 `CreateProperty()` 时把 `SignatureFunction` 作为 sparse type contract 固化下来 | 实现方式不同 |

这一维新增出来的关键差别是：**UnrealCSharp 倾向于把 sparse delegate 尽快适配进“普通 delegate helper”世界，从而复用既有桥接设施；Angelscript 则选择让 sparse 的 owner/name/signature 语义一直显式存在，直到真正绑定和执行时才解析到底层 storage**。

### [维度 D8] 委托调用缓冲生命周期：descriptor-owned persistent slab vs capped reusable event scratch

前文 D8 已经比较过普通 `UFunction` 跨边界调用的 `pool allocator` 和 `StaticJIT`。本轮只看 delegate/event callback 这条更细的性能路径，会发现 UnrealCSharp 和 Angelscript 对“参数缓冲归谁持有”这件事走了两条完全不同的路。

UnrealCSharp 在 delegate 路径里没有复用普通函数的 `pool allocator`，而是专门让 `FCSharpDelegateDescriptor` 选择 `FFunctionParamPersistentBufferAllocator`。这件事和它的 inline 执行接口是配套的：`Execute1/2/3/4/6/7` 与 `Broadcast2/4/6` 只 `Malloc()`，不 `Free()`，说明这里的参数缓冲天然就是 descriptor 生命周期的一部分。与之对应，普通 `FCSharpFunctionDescriptor::CallCSharp()` 仍然走 `pool allocator`，并在调用结束后显式 `Free()`。

Angelscript 的 delegate/event path 则完全不按“每个签名一个 descriptor buffer”来做。`Bind_BlueprintEvent.cpp` 里的 `FScriptCall` 直接内嵌 `ArgumentBuffer[1024]` 和 `ArgumentTypes[16]`，再配合 `GCurrentCall / GStoredCall` 做一次执行、一次清理、最多缓存一个实例的复用。它在 `PushArgument()` 和 `ValidateAgainstFunction()` 里保留显式上限与签名检查，换来的是缓冲大小非常可预测，但也把极限参数规模写死在了事件执行层。

```
[D8-Deep] Delegate Call Buffer Lifetime

[UnrealCSharp]
FCSharpDelegateDescriptor
├─ Factory(PersistentBufferAllocator)            // 每个 signature 一个常驻 Params slab
├─ Execute*/Broadcast* -> Malloc()               // 取同一块内存
├─ no per-call Free in delegate path             // 委托路径不做逐次归还
└─ Descriptor destruction -> free Params         // 生命周期在 descriptor 结束时关闭

[Angelscript]
FScriptCall
├─ ArgumentBuffer[1024] / ArgumentTypes[16]      // 固定上限 scratch
├─ CurrentCall()/StoredCall                      // 全局复用一个已缓存实例
├─ PushArgument() -> validate caps               // 运行时检查
└─ ExecuteCleanup()                              // 拷回引用并复用/释放
```

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FCSharpDelegateDescriptor.cpp:5-8,11-76`、`Reference/UnrealCSharp/Source/UnrealCSharp/Public/Reflection/Function/FCSharpDelegateDescriptor.inl:13-19,23-42,90-108`、`Reference/UnrealCSharp/Source/UnrealCSharp/Public/Reflection/Function/FFunctionParamBufferAllocator.h:41-71`、`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FFunctionParamBufferAllocator.cpp:58-83`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FCSharpDelegateDescriptor.cpp
// 函数: FCSharpDelegateDescriptor::FCSharpDelegateDescriptor / CallDelegate
// 位置: 行 5-8, 11-76，delegate descriptor 专门选择 persistent buffer allocator
// ============================================================================
FCSharpDelegateDescriptor::FCSharpDelegateDescriptor(UFunction* InFunction):
    Super(InFunction,
          FFunctionParamBufferAllocatorFactory::Factory<FFunctionParamPersistentBufferAllocator>(InFunction))
{
}

bool FCSharpDelegateDescriptor::CallDelegate(const UObject* InObject, const FMethodReflection* InMethod, void* InParams)
{
    const auto CSharpParams = FReflectionRegistry::Get().GetObjectClass()->NewArray(PropertyDescriptors.Num());
    ...
    // ★ 这里把回调参数直接映射到 CSharpParams，再写回 out/return
    return true;
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Public/Reflection/Function/FCSharpDelegateDescriptor.inl
// 函数: FCSharpDelegateDescriptor::Execute* / Broadcast*
// 位置: 行 13-19, 23-42, 90-108，delegate 执行路径只 Malloc 不 Free
// ============================================================================
template <auto ReturnType>
void FCSharpDelegateDescriptor::Execute1(const FScriptDelegate* InScriptDelegate, RETURN_BUFFER_SIGNATURE) const
{
    const auto Params = BufferAllocator.IsValid() ? BufferAllocator->Malloc() : nullptr;
    InScriptDelegate->ProcessDelegate<UObject>(Params);
    PROCESS_RETURN()
}

template <auto ReturnType>
void FCSharpDelegateDescriptor::Broadcast2(const FMulticastScriptDelegate* InMulticastScriptDelegate,
                                           IN_BUFFER_SIGNATURE) const
{
    const auto Params = BufferAllocator.IsValid() ? BufferAllocator->Malloc() : nullptr;
    PROCESS_SCRIPT_IN()
    InMulticastScriptDelegate->ProcessMulticastDelegate<UObject>(Params);
    // ★ inline 委托路径没有对等的 BufferAllocator->Free()
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FFunctionParamBufferAllocator.cpp
// 函数: FFunctionParamPersistentBufferAllocator
// 位置: 行 58-83，persistent allocator 持有一块常驻 Params
// ============================================================================
FFunctionParamPersistentBufferAllocator::FFunctionParamPersistentBufferAllocator(
    const TWeakObjectPtr<UFunction>& InFunction)
{
    Params = FMemory::Malloc(InFunction->ParmsSize, 16);
    FMemory::Memzero(Params, InFunction->ParmsSize);
}

void* FFunctionParamPersistentBufferAllocator::Malloc()
{
    return Params; // ★ 每次拿到的都是同一块 Params
}

void FFunctionParamPersistentBufferAllocator::Free(void* InMemory)
{
} // ★ 委托路径的 free 是 no-op
```

关键源码 [2] `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FUnrealFunctionDescriptor.cpp:3-5`、`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FCSharpFunctionDescriptor.cpp:23-26,165-177`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp:19-25,89-100,199-245,254-267,684-746`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FUnrealFunctionDescriptor.cpp
// 函数: FUnrealFunctionDescriptor::FUnrealFunctionDescriptor
// 位置: 行 3-5，普通函数路径仍使用 pool allocator
// ============================================================================
FUnrealFunctionDescriptor::FUnrealFunctionDescriptor(UFunction* InFunction):
    Super(InFunction,
          FFunctionParamBufferAllocatorFactory::Factory<FFunctionParamPoolBufferAllocator>(InFunction))
{
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FCSharpFunctionDescriptor.cpp
// 函数: FCSharpFunctionDescriptor::CallCSharp
// 位置: 行 23-26, 165-177，普通 C# 函数调用结束后会显式归还 pool buffer
// ============================================================================
if (InStack.Node != InStack.CurrentNativeFunction)
{
    Params = BufferAllocator.IsValid() ? BufferAllocator->Malloc() : nullptr;
    ...
}

if (Params != nullptr && Params != InStack.Locals)
{
    ...
    BufferAllocator->Free(Params); // ★ 和 delegate descriptor 的 persistent 路径形成对照
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp
// 函数: FScriptCall / PushArgument / ExecuteCleanup / CallSparseDelegate
// 位置: 行 19-25, 89-100, 199-245, 254-267, 684-746，事件调用使用固定上限 scratch buffer
// ============================================================================
#define AS_EVENT_MAX_ARGS 16
#define AS_EVENT_MAX_SIZE 1024

struct alignas(64) FScriptCall
{
    uint8 ArgumentBuffer[AS_EVENT_MAX_SIZE];
    FArgumentInBuffer ArgumentTypes[AS_EVENT_MAX_ARGS];
    int32 ArgumentIndex = 0;
    SIZE_T ArgumentOffset = 0;
    // ★ 参数缓冲大小和参数个数上限是编译期常量
};

template<bool TCheckErrors = true, bool TCopyInitialValue = true>
SCRIPTCALL_INLINE void PushArgument(FAngelscriptTypeUsage& Type, void* ValueRef)
{
    if ((TCheckErrors || DO_CHECK) && ArgumentIndex >= AS_EVENT_MAX_ARGS)
    {
        ResetArguments();
        FAngelscriptEngine::Throw("Too many arguments to event.");
        return;
    }
    ...
    if ((TCheckErrors || DO_CHECK) && ArgumentOffset + ArgumentSize >= AS_EVENT_MAX_SIZE)
    {
        ResetArguments();
        FAngelscriptEngine::Throw("Arguments to event too large.");
        return;
    }
}

SCRIPTCALL_INLINE void ExecuteCleanup()
{
    ResetArgumentsAndCopyBackReferences();
    if (GStoredCall == nullptr)
    {
        GStoredCall = this;
    }
    else
    {
        this->~FScriptCall();
        FMemory::Free(this);
    }
    // ★ 每次调用结束后只缓存一个可复用实例
}

void CallSparseDelegate(asIScriptGeneric* InGeneric)
{
    ...
    FScriptCall& Call = CurrentCall();
    ...
    Call.ExecuteMulticastDelegate(*MulticastDelegate);
    // ★ 每次执行都走同一套 scratch + 校验 + cleanup 逻辑
}
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 参数缓冲归属 | UnrealCSharp 的 delegate path 把 `Params` 交给 `FCSharpDelegateDescriptor` 长期持有；Angelscript 把事件参数装进 `FScriptCall` 全局 scratch | 实现方式不同 |
| 缓冲大小策略 | UnrealCSharp 由 `UFunction::ParmsSize` 决定 persistent buffer 大小；Angelscript 在事件层硬编码 `16 args / 1024 bytes` 上限 | 实现质量差异，二者在弹性与可预测性上取舍不同 |
| 复用粒度 | UnrealCSharp 是“每个 signature 一块常驻 slab”；Angelscript 是“全局最多缓存一个 `FScriptCall` 实例” | 实现方式不同 |
| 普通函数与委托是否共用策略 | UnrealCSharp 明确区分普通函数 `pool allocator` 和委托 `persistent allocator`；Angelscript 在本轮定位到的 Blueprint event / sparse delegate 路径里共用同一套 `FScriptCall` scratch 机制 | 实现方式不同 |

这里有一个需要显式说明的推断：**在本轮可见源码里，Angelscript 的 delegate/event path 选择了“每次执行都做显式容量与签名验证”；UnrealCSharp 的 delegate path 没有定位到对等的 per-call `ValidateAgainstFunction()`，更像依赖 descriptor 初始化时已经固定好的签名与缓冲布局。** 这个判断来自 `FCSharpDelegateDescriptor::Execute*` 与 `FScriptCall::ValidateAgainstFunction()` 的直接对照，而不是空泛推测。

---

## 深化分析 (2026-04-09 00:16:49)

本轮不再重复前面已经写过的 `override patch`、`delegate helper`、`publish staging` 等链路，而是补三个之前还没有被源码级闭环说明的问题：**绑定描述符到底何时物化和失效、脚本对象构造到底插在 UE 生命周期的哪一层、编辑期索引/类视图到底由什么事件驱动维护**。这三点分别对应 D2 / D3 / D6。

### [维度 D2] 绑定描述符生命周期：lazy hash materialization vs whole-database property construction

前文 D2 已经解释过 UnrealCSharp 的 `InternalCall` 与 override patch。本轮继续往下追，会发现它在“反射描述符什么时候真正创建、什么时候销毁”上做得非常细。`FCSharpBind::BindImplementation(UStruct*)` 并不立即把每个 `FProperty` 都包装成最终 descriptor，而是先给每个匹配到的字段计算 `FieldHash`，再把 `(FieldHash -> ClassDescriptor + FProperty*)` 塞进 `PropertyHashMap`。只有真正有人按 hash 访问这个属性时，`FClassRegistry::GetOrAddPropertyDescriptor()` 才会调用 `FClassDescriptor::AddPropertyDescriptor()` 执行 `FPropertyDescriptor::Factory()`，随后把 placeholder 从 `PropertyHashMap` 移到 `PropertyDescriptorMap`。这是一条明显的 lazy materialization（延迟物化）路径。

更关键的是，它的回收粒度也细到“单个 class descriptor 退场”。`FCSharpEnvironment::NotifyUObjectDeleted()` 一旦发现被销毁对象是 `UStruct`，会立即 `RemoveClassDescriptor()`；`FClassRegistry::RemoveClassDescriptor()` 删除 descriptor 前会先恢复原始 `ClassConstructor`；真正的 `FClassDescriptor::Deinitialize()` 又会把 `FunctionHashSet` / `PropertyHashSet` 对应的 descriptor 逐个清掉，并顺带把 `StaticClassSingleton` / `StaticStructSingleton` 置空。也就是说，UnrealCSharp 的反射缓存不是“环境活着就一直挂着”，而是和 class descriptor 生命周期紧耦合。

Angelscript 在当前可见路径上明显不是这个思路。`FAngelscriptTypeUsage::CreateProperty()` 直接把类型能力投射成 UE `FProperty`，`AngelscriptClassGenerator` 在生成类属性、函数返回值、函数参数时都立即调用 `CreateProperty()` 构造真实 `FProperty`；另一方面，`FAngelscriptBindDatabase` 只维护 `Structs / Classes / HeaderLinks` 这类更粗粒度的绑定数据库，启动时从 `Binds.Cache` 读入，运行时绑定完成后可以 `Clear()` 整个数据库。在本轮可见源码里，没有定位到与 `PropertyHashMap -> PropertyDescriptorMap -> RemovePropertyDescriptor(uint32)` 对等的“按 hash 懒建、按 hash 失效”机制。**这不是说 Angelscript 没有绑定生命周期管理，而是它的生命周期边界落在“类生成 / bind database”这一级，而不是 UnrealCSharp 那样的 descriptor registry 这一层。**

```
[D2-Deep] Descriptor Lifetime Boundary

[UnrealCSharp]
BindImplementation(UStruct)
├─ match __Field <-> FProperty                     // 先建立字段匹配
├─ AddPropertyHash(FieldHash, ClassDesc, Property) // 只登记 placeholder
├─ GetOrAddPropertyDescriptor(hash)                // 首次访问时才 Factory()
└─ NotifyUObjectDeleted(UStruct)
   └─ RemoveClassDescriptor
      └─ Deinitialize -> RemoveFunctionDescriptor / RemovePropertyDescriptor

[Angelscript]
ClassGenerator
├─ PropertyType.CreateProperty()                   // 生成期直接构造 FProperty
├─ ReturnType.CreateProperty()
├─ ArgDesc.Type.CreateProperty()
└─ BindDatabase
   ├─ Load(Binds.Cache)
   ├─ Save(Binds.Cache)
   └─ Clear()                                      // 数据库级清空，不是 descriptor 级失效
```

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FCSharpBind.cpp:75-170`、`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FClassRegistry.cpp:115-190`、`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Class/FClassDescriptor.cpp:16-64`、`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp:270-287`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FCSharpBind.cpp
// 函数: FCSharpBind::BindClassDefaultObject / BindImplementation
// 位置: 行 75-170，先挂 constructor hook，再把属性登记成 hash placeholder
// ============================================================================
bool FCSharpBind::BindClassDefaultObject(UObject* InObject)
{
    if (CanBind(InObject->GetClass()))
    {
        FClassRegistry::AddClassConstructor(InObject->GetClass()); // ★ 先接管 UClass 构造入口
        Bind<false>(InObject);
        return true;
    }
    ...
}

bool FCSharpBind::BindImplementation(UStruct* InStruct)
{
    ...
    const auto NewClassDescriptor = FCSharpEnvironment::GetEnvironment().AddClassDescriptor(InStruct);
    ...
    for (const auto& [PropertyName, Property] : Properties)
    {
        for (const auto& [FieldName, Field] : Fields)
        {
            if (FieldName == PropertyName)
            {
                auto FieldHash = GetTypeHash(Property);
                ...
                FCSharpEnvironment::GetEnvironment().AddPropertyHash(FieldHash, NewClassDescriptor, Property);
                // ★ 这里只记录 hash -> (descriptor owner, FProperty)，并不立即 new FPropertyDescriptor
                ...
            }
        }
    }
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FClassRegistry.cpp
// 函数: FClassRegistry::GetOrAddPropertyDescriptor / RemoveClassDescriptor / RemovePropertyDescriptor
// 位置: 行 115-190，按需物化，按类失效
// ============================================================================
void FClassRegistry::RemoveClassDescriptor(const UStruct* InStruct)
{
    if (const auto FoundClassDescriptor = ClassDescriptorMap.Find(InStruct))
    {
        if (const auto Class = Cast<UClass>(const_cast<UStruct*>(InStruct)))
        {
            if (const auto FoundClassConstructor = ClassConstructorMap.Find(Class))
            {
                Class->ClassConstructor = *FoundClassConstructor; // ★ descriptor 退场时连构造 hook 一起回滚
                ClassConstructorMap.Remove(Class);
            }
        }

        delete *FoundClassDescriptor;
        ClassDescriptorMap.Remove(InStruct);
    }
}

FPropertyDescriptor* FClassRegistry::GetOrAddPropertyDescriptor(const uint32 InPropertyHash)
{
    if (const auto FoundPropertyHash = PropertyHashMap.Find(InPropertyHash))
    {
        if (const auto FoundPropertyDescriptor =
                std::get<0>(*FoundPropertyHash)->AddPropertyDescriptor(std::get<1>(*FoundPropertyHash)))
        {
            PropertyHashMap.Remove(InPropertyHash);
            PropertyDescriptorMap.Add(InPropertyHash, FoundPropertyDescriptor);
            return FoundPropertyDescriptor; // ★ 首次需要时才真正 Factory()
        }
    }

    return nullptr;
}

void FClassRegistry::RemovePropertyDescriptor(const uint32 InPropertyHash)
{
    if (const auto FoundPropertyDescriptor = PropertyDescriptorMap.Find(InPropertyHash))
    {
        delete *FoundPropertyDescriptor;
        PropertyDescriptorMap.Remove(InPropertyHash);
    }
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Class/FClassDescriptor.cpp
// 函数: FClassDescriptor::Initialize / Deinitialize
// 位置: 行 16-64，descriptor 自己持有需要回收的 function/property hash 集合
// ============================================================================
void FClassDescriptor::Initialize()
{
    ...
    Class = FReflectionRegistry::Get().GetClass(Struct);
    Class->ConstructorClass(); // ★ class-level 初始化也绑定在 descriptor 初始化上
}

void FClassDescriptor::Deinitialize()
{
    ...
    for (const auto& FunctionHash : FunctionHashSet)
    {
        FCSharpEnvironment::GetEnvironment().RemoveFunctionDescriptor(FunctionHash);
    }
    FunctionHashSet.Empty();

    for (const auto& PropertyHash : PropertyHashSet)
    {
        FCSharpEnvironment::GetEnvironment().RemovePropertyDescriptor(PropertyHash);
    }
    PropertyHashSet.Empty(); // ★ 函数和属性 descriptor 都在这里成对回收
}
```

关键源码 [2] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:151,161,436,439`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2931-2938,3947-3977`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp:33-40,42-115`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1466-1509`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h
// 函数: FAngelscriptType / FAngelscriptTypeUsage::CreateProperty
// 位置: 行 151, 161, 436, 439，类型能力直接暴露为 CreateProperty 合约
// ============================================================================
virtual bool CanCreateProperty(const FAngelscriptTypeUsage& Usage) const { return false; }
virtual FProperty* CreateProperty(const FAngelscriptTypeUsage& Usage, const FPropertyParams& Params) const
{
    ensure(false);
    return nullptr;
}

FORCEINLINE bool CanCreateProperty() const { return Type.IsValid() && Type->CanCreateProperty(*this); }
FORCEINLINE FProperty* CreateProperty(const FAngelscriptType::FPropertyParams& Params) const
{
    return Type->CreateProperty(*this, Params); // ★ 没有中间 descriptor registry，直接产出 UE FProperty
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 函数: 类属性/返回值/参数属性生成
// 位置: 行 2931-2938, 3947-3977，生成期直接构造真实 FProperty
// ============================================================================
FProperty* NewProperty = PropertyType.CreateProperty(Params);
PropDesc->bHasUnrealProperty = true;

FProperty* NewProperty = ReturnType.CreateProperty(Params);
NewProperty->SetPropertyFlags(CPF_Parm | CPF_OutParm | CPF_ReturnParm);

FProperty* NewProperty = ArgDesc.Type.CreateProperty(Params);
NewProperty->SetPropertyFlags(CPF_Parm);
// ★ 当前路径是“生成时直接落 UE property”，不是“先登记 hash，按访问延迟物化”
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp
// 函数: FAngelscriptBindDatabase::Clear / Save / Load
// 位置: 行 33-40, 42-115，数据库层级的装载与清空
// ============================================================================
void FAngelscriptBindDatabase::Clear()
{
    Structs.Empty();
    Classes.Empty();
    BoundEnums.Empty();
    BoundDelegateFunctions.Empty();
    HeaderLinks.Empty(); // ★ 清空粒度是整张 bind database
}

void FAngelscriptBindDatabase::Load(const FString& Path, bool bGeneratingPrecompiledData)
{
    ...
    Serialize(Reader);
    if (Classes.Num() == 0 && Structs.Num() == 0)
    {
        UE_LOG(Angelscript, Fatal, TEXT("Unable to load script bind database, Script/Binds.Cache file is missing or old."));
    }
}
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 属性描述符物化时机 | UnrealCSharp 先登记 `PropertyHashMap`，首次访问时才 `Factory()`；Angelscript 在类/函数生成时直接 `CreateProperty()` | 实现方式不同 |
| 描述符失效粒度 | UnrealCSharp 能沿 `NotifyUObjectDeleted -> RemoveClassDescriptor -> RemovePropertyDescriptor` 走到 per-class/per-hash 回收；Angelscript 当前可见主路径是 bind database 级 `Clear()` | 实现质量差异，UnrealCSharp 的失效边界更细 |
| 运行时缓存形态 | UnrealCSharp 显式维护 `ClassDescriptor / FunctionDescriptor / PropertyDescriptor` registry；Angelscript 把类型能力压进 `FAngelscriptTypeUsage` 与生成出的 UE `FProperty` | 实现方式不同 |
| 生命周期治理重点 | UnrealCSharp 重点治理“描述符缓存是否过期”；Angelscript 重点治理“生成结果与 bind database 是否完整” | 实现方式不同 |

这里有一个需要显式说明的推断：**“Angelscript 没有 per-hash descriptor eviction” 是基于本轮新增检索到的 `AngelscriptType`、`AngelscriptClassGenerator`、`AngelscriptBindDatabase`、`AngelscriptEngine` 主路径做出的结论；它表示当前可见主路径里没有定位到对等机制，而不是断言整个仓库绝对不存在任何局部缓存。**

### [维度 D3] 对象构造接缝：constructor hook on native UClass vs custom UASClass construction pipeline

前文 D3 讨论过 Blueprint override 和 delegate 执行面，但还没有把“脚本对象到底怎样嵌进 UE 的 `UObject` 构造生命周期”说透。本轮补完后可以看到，UnrealCSharp 和 Angelscript 在这件事上的设计哲学几乎相反。

UnrealCSharp 的做法是**尽量不改 UE 的对象构造主干，只在已有 `UClass::ClassConstructor` 旁边插一个 managed hook**。`FCSharpBind::BindClassDefaultObject()` 在 CDO 首次被绑定时调用 `FClassRegistry::AddClassConstructor()`，把原始 `ClassConstructor` 暂存在 `ClassConstructorMap`，再把 `UClass::ClassConstructor` 替换成 `FClassRegistry::ClassConstructor`。这个 hook 进入后，先沿继承链找到原始 native constructor 并执行；只有在 `FDomain::IsLoadSucceed()` 且当前对象已经对应到 `MonoObject*` 时，才通过 `FClassReflection::ConstructorObject()` 去调用 managed `FUNCTION_OBJECT_CONSTRUCTOR`。配合 `FCSharpEnvironment::OnAsyncLoadingFlushUpdate()` 对 CDO/普通对象的延迟 bind，managed constructor 更像 native 生命周期上的 piggyback（搭车钩子）。

Angelscript 则是**直接把脚本类做成自己的 `UASClass` 构造管线**。`FAngelscriptClassGenerator` 在 finalize 阶段明确把 `ClassConstructor` 改写成 `UASClass::StaticActorConstructor / StaticComponentConstructor / StaticObjectConstructor`，并把 `ConstructFunction` / `DefaultsFunction` 存进 `UASClass`。后续 `UASClass::AllocScriptObject()` 直接复制 `StaticConstructObject_Internal` 的关键分支，手动 `StaticAllocateObject()` 并保存 `CurrentObjectInitializers`；`Static*Constructor()` 再先运行 `CodeSuperClass->ClassConstructor()`，随后在 UObject 内存中原位放置 `asCScriptObject`，创建默认组件，执行脚本 `ConstructFunction`，最后跑 `DefaultsFunction`；`FinishConstructObject()` 则负责在最顶层脚本类完成后收尾默认值执行。也就是说，Angelscript 不是在 UE 现有 constructor 上“加一刀”，而是自己拥有整条 script-object construction pipeline。

```
[D3-Deep] Object Construction Seam

[UnrealCSharp]
CDO discovered
├─ FCSharpBind::BindClassDefaultObject()
│  └─ AddClassConstructor(UClass)                // 替换成 hook constructor
├─ FClassRegistry::ClassConstructor()
│  ├─ call original native constructor
│  └─ if MonoObject exists -> ConstructorObject()
└─ managed ctor piggybacks on native lifecycle

[Angelscript]
Class generation finalize
├─ ClassConstructor = UASClass::Static*Constructor
├─ UpdateConstructAndDefaultsFunctions()
├─ UASClass::AllocScriptObject()                 // 自己分配 UObject + initializer
├─ UASClass::Static*Constructor()
│  ├─ CodeSuperClass->ClassConstructor()
│  ├─ placement new asCScriptObject
│  ├─ ExecuteConstructFunction()
│  └─ ExecuteDefaultsFunctions()
└─ FinishConstructObject()                       // 顶层脚本类收尾
```

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FCSharpBind.cpp:75-81`、`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FClassRegistry.cpp:105-112,193-226`、`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp:305-385`、`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Reflection/FClassReflection.cpp:580-585`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FCSharpBind.cpp
// 函数: FCSharpBind::BindClassDefaultObject
// 位置: 行 75-81，在 CDO 绑定时接管 UClass::ClassConstructor
// ============================================================================
bool FCSharpBind::BindClassDefaultObject(UObject* InObject)
{
    if (CanBind(InObject->GetClass()))
    {
        FClassRegistry::AddClassConstructor(InObject->GetClass()); // ★ 只在 class 可 bind 时才挂 hook
        Bind<false>(InObject);
        return true;
    }
    ...
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FClassRegistry.cpp
// 函数: FClassRegistry::AddClassConstructor / ClassConstructor
// 位置: 行 105-112, 193-226，先调用原生 constructor，再按需补 managed constructor
// ============================================================================
void FClassRegistry::AddClassConstructor(UClass* InClass)
{
    if (!ClassConstructorMap.Contains(InClass))
    {
        ClassConstructorMap.Add(InClass, InClass->ClassConstructor);
        InClass->ClassConstructor = &FClassRegistry::ClassConstructor;
    }
}

void FClassRegistry::ClassConstructor(const FObjectInitializer& InObjectInitializer)
{
    auto Class = InObjectInitializer.GetClass();
    while (Class != nullptr)
    {
        if (ClassConstructorMap.Contains(Class) && ClassConstructorMap[Class] != &FClassRegistry::ClassConstructor)
        {
            ClassConstructorMap[Class](InObjectInitializer); // ★ 原始 native constructor 先执行
            break;
        }
        Class = Class->GetSuperClass();
    }

    if (IsInGameThread() && FDomain::IsLoadSucceed())
    {
        const auto Object = InObjectInitializer.GetObj();
        if (const auto FoundMonoObject = FCSharpEnvironment::GetEnvironment().GetObject(Object))
        {
            ...
            if (const auto FoundClass = FReflectionRegistry::Get().GetClass(Object->GetClass()))
            {
                FoundClass->ConstructorObject(FoundMonoObject); // ★ managed ctor 是后置补刀
            }
        }
    }
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp
// 函数: FCSharpEnvironment::OnAsyncLoadingFlushUpdate
// 位置: 行 368-385，异步加载收敛后分别处理 CDO bind 和实例 ctor 调用
// ============================================================================
for (const auto& PendingBindObject : PendingBindObjects)
{
    if (PendingBindObject->HasAnyFlags(EObjectFlags::RF_ClassDefaultObject))
    {
        FCSharpBind::BindClassDefaultObject(PendingBindObject);
    }
    else
    {
        Bind<true>(PendingBindObject);
    }

    if (const auto FoundMonoObject = GetObject(PendingBindObject))
    {
        if (const auto FoundClass = FReflectionRegistry::Get().GetClass(PendingBindObject->GetClass()))
        {
            FoundClass->ConstructorObject(FoundMonoObject); // ★ 普通对象同样在 bind 后补 constructor
        }
    }
}
```

关键源码 [2] `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:5219-5220,5451-5464,5889-5903`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:1028-1083,1137-1171,1352-1494`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 函数: FinalizeActorClass / FinalizeComponentClass / FinalizeObjectClass / UpdateConstructAndDefaultsFunctions
// 位置: 行 5219-5220, 5451-5464, 5889-5903，class generator 直接指定脚本类自己的 constructor
// ============================================================================
ClassDesc->Class->ClassConstructor = &UASClass::StaticActorConstructor;
...
ClassDesc->Class->ClassConstructor = &UASClass::StaticComponentConstructor;
...
ClassDesc->Class->ClassConstructor = &UASClass::StaticObjectConstructor;

void FAngelscriptClassGenerator::UpdateConstructAndDefaultsFunctions(..., UASClass* Class)
{
    asCObjectType* ObjType = (asCObjectType*)Class->ScriptTypePtr;
    if (ObjType != nullptr)
    {
        Class->ConstructFunction = ObjType->GetEngine()->GetFunctionById(ObjType->beh.construct);
        auto* DefaultsFunction = (asCScriptFunction*)ObjType->GetMethodByDecl("void __InitDefaults()");
        if (DefaultsFunction != nullptr && DefaultsFunction->objectType == ObjType)
            Class->DefaultsFunction = DefaultsFunction;
        // ★ constructor/defaults 入口被固化到 UASClass 自身
    }
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp
// 函数: UASClass::AllocScriptObject / FinishConstructObject / StaticActorConstructor / StaticObjectConstructor
// 位置: 行 1028-1083, 1137-1171, 1352-1494，脚本对象分配、原生构造、脚本构造、默认值执行都由 UASClass 拥有
// ============================================================================
void* UASClass::AllocScriptObject(class asITypeInfo* ScriptType, size_t Size)
{
    ...
    Result = StaticAllocateObject(InClass, InOuter, InName, InFlags, InternalSetFlags, bCanRecycleSubObjects, &bRecycledSubobject);
    ...
    FObjectInitializer& Initializer = CurrentObjectInitializers.Emplace_GetRef(
        Result, nullptr, EObjectInitializerOptions::InitializeProperties, nullptr);
    (*Class->ClassConstructor)(Initializer); // ★ 分配后立即走脚本类自己的 constructor pipeline
    return Result;
}

void UASClass::FinishConstructObject(class asIScriptObject* ScriptObject, class asITypeInfo* ScriptType)
{
    ...
    if (TopClass->ScriptTypePtr == ScriptType)
    {
        CurrentObjectInitializers.RemoveAt(CurrentObjectInitializers.Num() - 1, 1, EAllowShrinking::No);
        ExecuteDefaultsFunctions(Object, TopClass); // ★ 顶层脚本类完成后再统一跑 defaults
    }
}

void UASClass::StaticActorConstructor(const FObjectInitializer& Initializer)
{
    ...
    Class->CodeSuperClass->ClassConstructor(Initializer); // ★ 先跑原生父类 constructor
    ...
    if (!bIsScriptAllocation && ScriptType != nullptr)
        new(Object) asCScriptObject(ScriptType);          // ★ 再把脚本对象原位放进 UObject 内存
    ...
    if (!bIsScriptAllocation)
        ExecuteConstructFunction(Object, Class);          // ★ 执行脚本构造函数
    if (bApplyDefaults && !bIsScriptAllocation)
        ExecuteDefaultsFunctions(Object, Class);          // ★ 再执行默认语句
}
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 构造插入点 | UnrealCSharp 在原生 `UClass::ClassConstructor` 外包一层 hook；Angelscript 直接把 `ClassConstructor` 改成 `UASClass::Static*Constructor` | 实现方式不同 |
| 原生 constructor 的地位 | UnrealCSharp 的 native constructor 仍然是主入口，managed ctor 是后置补充；Angelscript 由脚本类 constructor pipeline 主导，再显式调用 `CodeSuperClass->ClassConstructor()` | 实现方式不同 |
| 脚本对象内存布局 | UnrealCSharp 通过 `MonoObject*` 绑定到原生对象；Angelscript 在 `UObject` 内存里 `placement new asCScriptObject` | 实现方式不同 |
| 默认值执行时机 | UnrealCSharp 在本轮定位到的主路径里没有与 `DefaultsFunction` 对等的独立阶段；Angelscript 明确拆出 `ConstructFunction` 与 `DefaultsFunction` 两段 | 实现质量差异，Angelscript 的构造阶段更显式 |

这一维最关键的差别是：**UnrealCSharp 选择“保持 UE 原生对象模型不变，只挂 managed 钩子”，而 Angelscript 选择“脚本类自己拥有构造主路径，再把 native superclass 嵌回去”**。这不是谁绝对更先进，而是两者对 mixed inheritance chain 的所有权划分完全不同。

### [维度 D6] 编辑期事件扇入：asset-aware authoring graph vs script-file reload queue

前文 D6 已经讲过 `SolutionGenerator`、`CodeAnalysis` 和 `OverrideFile.json`。本轮只补“这些产物靠什么事件保持新鲜”。UnrealCSharp 的答案非常明确：**不是单靠目录监听，而是把编辑器里所有会影响 authoring artifact 的事件都汇总成一个 fan-in graph**。`FEditorListener` 在构造时同时订阅 `OnPostEngineInit`、`OnCompile`、`AssetRegistry.OnFilesLoaded`、`DirectoryWatcher`、PIE 边界等事件；`OnPostEngineInit()` 直接触发全量 `FCodeAnalysis::CodeAnalysis()` 与 `FDynamicGenerator::CodeAnalysisGenerator()`，`OnCompile()` 则对变更文件逐个 `FCodeAnalysis::Analysis(File)`，`OnFilesLoaded()` 之后再挂 `OnAssetAdded/Removed/Renamed/UpdatedOnDisk` 来驱动 `FAssetGenerator` 和文件删除/重建。

更深一层，`FClassCollector` 又单独维护“可被动态类对话框与 Blueprint 工具条看到的 class hierarchy”。它会同时监听 `OnDynamicClassUpdated`、`OnEndGenerator`、`ReloadCompleteDelegate`、`OnBlueprintCompiled` 和资产增删改名事件，并用 `RequestPopulateClassHierarchy()` 在同一帧内去重，真正重建延后到 `OnEndFrame` 再做。也就是说，UnrealCSharp 把 IDE 索引、override 文件、动态类层级都看成**编辑器事件驱动的数据视图**。

Angelscript 当前可见主路径不是这样。`AngelscriptDirectoryWatcherInternal::QueueScriptFileChanges()` 的职责很纯粹，就是把 `.as` 文件变化或文件夹增删转换成 `FileChangesDetectedForReload / FileDeletionsDetectedForReload`；`FAngelscriptEngine::CheckForHotReload()` 再在 runtime tick 里消费这个队列并触发 `PerformHotReload()`。本轮新增定位到的 Angelscript `AssetRegistry` 事件主路径出现在 `FAngelscriptDebugServer::BindAssetRegistry()`，它会把资产增删改消息推给 `ClientsThatWantDebugDatabase`，用途是**调试数据库同步**，不是 UnrealCSharp 那种“驱动代码分析、类层级、override 文件映射”的 authoring graph。也就是说，Angelscript 当然也用到了 Asset Registry，但当前可见主要用途不同。

```
[D6-Deep] Editor Event Fan-In

[UnrealCSharp]
Editor events
├─ OnPostEngineInit -> CodeAnalysis() + DynamicGenerator()
├─ OnCompile(file changes) -> Analysis(file) + DynamicFilesMap
├─ OnFilesLoaded
│  ├─ OnAssetAdded/Removed/Renamed/Updated -> AssetGenerator / delete old file
│  └─ ClassCollector::RequestPopulateClassHierarchy()
├─ OnBlueprintCompiled / ReloadComplete / OnDynamicClassUpdated
│  └─ coalesce to EndFrame PopulateClassHierarchy()
└─ DirectoryWatcher(.cs) -> queued compile on app focus restore

[Angelscript]
DirectoryWatcher(.as)
├─ QueueScriptFileChanges()
│  ├─ FileChangesDetectedForReload
│  └─ FileDeletionsDetectedForReload
└─ Engine::CheckForHotReload()
   └─ PerformHotReload()

AssetRegistry
└─ DebugServer::BindAssetRegistry()
   └─ send asset database delta to debugger clients
```

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:24-60,137-141,228-289,322-365` 与 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/NewClass/ClassCollector.cpp:31-63,131-149,301-344`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp
// 函数: FEditorListener::FEditorListener / OnPostEngineInit / OnCompile / OnFilesLoaded / OnDirectoryChanged
// 位置: 行 24-60, 137-141, 228-289, 322-365，编辑器事件统一汇入 authoring pipeline
// ============================================================================
OnPostEngineInitDelegateHandle = FCoreDelegates::OnPostEngineInit.AddRaw(this, &FEditorListener::OnPostEngineInit);
...
OnCompileDelegateHandle = FUnrealCSharpCoreModuleDelegates::OnCompile.AddRaw(this, &FEditorListener::OnCompile);
...
AssetRegistryModule.Get().OnFilesLoaded().AddRaw(this, &FEditorListener::OnFilesLoaded);
...
DirectoryWatcherModule.Get()->RegisterDirectoryChangedCallback_Handle(...);

void FEditorListener::OnPostEngineInit()
{
    FCodeAnalysis::CodeAnalysis();
    FDynamicGenerator::CodeAnalysisGenerator(); // ★ 编辑器初始化后立即刷新全量 authoring artifact
}

void FEditorListener::OnCompile(const TArray<FFileChangeData>& InFileChangeData)
{
    ...
    FCodeAnalysis::Analysis(File);               // ★ 增量编译时按文件重跑分析
    FDynamicGenerator::SetCodeAnalysisDynamicFilesMap();
}

void FEditorListener::OnFilesLoaded()
{
    AssetRegistryModule.Get().OnAssetAdded().AddRaw(this, &FEditorListener::OnAssetAdded);
    AssetRegistryModule.Get().OnAssetRemoved().AddRaw(this, &FEditorListener::OnAssetRemoved);
    AssetRegistryModule.Get().OnAssetRenamed().AddRaw(this, &FEditorListener::OnAssetRenamed);
    AssetRegistryModule.Get().OnAssetUpdatedOnDisk().AddRaw(this, &FEditorListener::OnAssetUpdatedOnDisk);
    // ★ 资产生命周期也被纳入代码生成/文件映射维护链
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/NewClass/ClassCollector.cpp
// 函数: FClassCollector::FClassCollector / RequestPopulateClassHierarchy / OnAssetAdded / OnAssetRemoved / OnAssetRenamed
// 位置: 行 31-63, 131-149, 301-344，类层级刷新是多事件汇流后的延迟视图重建
// ============================================================================
FClassCollector::FClassCollector()
{
    PopulateClassHierarchy();

    OnDynamicClassUpdatedDelegateHandle =
        FUnrealCSharpCoreModuleDelegates::OnDynamicClassUpdated.AddStatic(&FClassCollector::RequestPopulateClassHierarchy);
    OnEndGeneratorDelegateHandle =
        FUnrealCSharpCoreModuleDelegates::OnEndGenerator.AddStatic(&FClassCollector::RequestPopulateClassHierarchy);
    OnReloadCompleteDelegateDelegateHandle =
        FCoreUObjectDelegates::ReloadCompleteDelegate.AddStatic(&FClassCollector::OnReloadComplete);
    OnBlueprintCompiledDelegateHandle =
        GEditor->OnBlueprintCompiled().AddStatic(&FClassCollector::RequestPopulateClassHierarchy);
    ...
}

void FClassCollector::RequestPopulateClassHierarchy()
{
    static auto LastRequestFrame = 0u;
    if (LastRequestFrame == GFrameNumber)
    {
        return; // ★ 同一帧多次触发时去重
    }

    LastRequestFrame = GFrameNumber;
    OnEndFrameDelegateHandle = FCoreDelegates::OnEndFrame.AddStatic(&FClassCollector::PopulateClassHierarchy);
}

void FClassCollector::OnAssetAdded(const FAssetData& InAssetData)
{
    ...
    NodesSet.Add(MakeShared<FDynamicClassViewerNode>(InAssetData));
    RefreshAllNodes(); // ★ 类视图直接对资产改名/增删做增量维护
}
```

关键源码 [2] `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp:43-88`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2729-2828`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:2078-2168`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp
// 函数: QueueScriptFileChanges
// 位置: 行 43-88，文件监听只负责把脚本变化排队到 hot reload 队列
// ============================================================================
void QueueScriptFileChanges(..., FAngelscriptEngine& Engine, ...)
{
    ...
    if (AbsolutePath.EndsWith(TEXT(".as")))
    {
        if (Change.Action == FFileChangeData::EFileChangeAction::FCA_Removed)
        {
            Engine.FileDeletionsDetectedForReload.AddUnique({ AbsolutePath, RelativePath });
        }
        else
        {
            Engine.FileChangesDetectedForReload.AddUnique({ AbsolutePath, RelativePath });
        }
        ...
        continue;
    }
    ...
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: FAngelscriptEngine::CheckForHotReload / Tick
// 位置: 行 2729-2828，runtime 周期里消费脚本文件变更队列
// ============================================================================
void FAngelscriptEngine::CheckForHotReload(ECompileType CompileType)
{
    ...
    FileList.Append(FileChangesDetectedForReload);
    FileChangesDetectedForReload.Empty();
    ...
    for (const auto& DeletedFile : FileDeletionsDetectedForReload)
        FileList.AddUnique(DeletedFile);
    FileDeletionsDetectedForReload.Empty();
    ...
    PerformHotReload(CompileType, FileList); // ★ 队列的最终目的就是触发脚本热重载
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 函数: FAngelscriptDebugServer::BindAssetRegistry / SendAssetDatabase
// 位置: 行 2078-2168，资产事件在本轮可见主路径中用于调试数据库同步
// ============================================================================
void FAngelscriptDebugServer::BindAssetRegistry()
{
    ...
    AssetRegistry.OnAssetAdded().AddLambda([this](const FAssetData& AssetData)
    {
        if (ClientsThatWantDebugDatabase.Num() == 0)
            return;
        ...
        SendMessageToClient(ConnectedClient, EDebugMessageType::AssetDatabase, UpdateMessage);
    });
    AssetRegistry.OnAssetRemoved().AddLambda(...);
    AssetRegistry.OnAssetRenamed().AddLambda(...);
    ...
}
// ★ 当前定位到的 AssetRegistry 事件主路径服务于 debugger asset database，不是 IDE 代码分析索引
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 编辑期事件来源 | UnrealCSharp 把 `EngineInit/Compile/Asset/Blueprint/Reload/DirectoryWatcher` 全部汇流进 authoring pipeline；Angelscript 当前可见主路径以 `.as` 文件变化队列为核心 | 实现方式不同 |
| 资产事件的用途 | UnrealCSharp 资产事件直接驱动 `AssetGenerator`、类视图、override 映射维护；Angelscript 本轮定位到的资产事件主路径主要服务 `DebugServer` 的 asset database | 实现方式不同 |
| 类层级刷新策略 | UnrealCSharp 有 `ClassCollector` 做 `OnEndFrame` 去重刷新；Angelscript 本轮未定位到对等的“编辑器动态类层级视图维护器” | 没有实现（就当前可见的 authoring class hierarchy maintainer 而言） |
| 热重载与 IDE 支持的耦合度 | UnrealCSharp 把代码分析/动态类/资源文件生成绑定在同一套事件图里；Angelscript 把热重载队列与调试资产同步拆成不同路径 | 实现方式不同 |

这里也需要显式限定范围：**Angelscript 并不是“完全不用 Asset Registry”**，本轮新增证据已经定位到 `DebugServer`、`BlueprintImpact` 等资产相关能力；这里判断的是“在当前新增定位到的 IDE/authoring 主路径里，没有出现 UnrealCSharp 那种以资产与 Blueprint 事件为核心的代码分析/类视图维护闭环”。 

---

## 深化分析 (2026-04-09 00:27:40)

### [维度 D3] RPC / Blueprint 语义下沉：managed attribute lowering vs script-side `UFUNCTION` state machine

前面的 D3 主要看了 override patch、wrapper 和混合继承链。这一轮只补一个更细的交互点：**网络 RPC 语义到底在什么时候落成 UE `FunctionFlags`**。UnrealCSharp 的做法是“托管 attribute 直接下沉到 `UFunction::FunctionFlags`”。`FReflectionRegistry` 先把 `BlueprintImplementableEventAttribute`、`BlueprintNativeEventAttribute`、`ServerAttribute`、`ClientAttribute`、`NetMulticastAttribute`、`ReliableAttribute`、`WithValidationAttribute` 全部注册成可查询的反射类；随后 `FDynamicGeneratorCore::SetFlags()` 在构造动态 `UFunction` 时直接检查这些 attribute，并立即修改 `FUNC_Event / FUNC_BlueprintEvent / FUNC_NetServer / FUNC_NetClient / FUNC_NetMulticast / FUNC_NetReliable / FUNC_NetValidate`。

更关键的是，UnrealCSharp 在这个 lowering 阶段就直接拒绝了一组组合：`BlueprintNativeEvent` 或 `BlueprintImplementableEvent` 一旦遇到 `FUNC_Net`，就只记录错误日志，不再进入“可复制的 Blueprint 事件”路径。也就是说，**就当前定位到的动态 C# function 导出链而言，replicated BlueprintEvent 组合并没有被实现出来**。

Angelscript 的落点不一样。它不是在最终 `UFunction` 对象上边走边判，而是先在脚本预处理阶段把 `UFUNCTION(Server/Client/NetMulticast/WithValidation/BlueprintEvent)` 翻译成 `FunctionDesc` 的布尔状态；如果是 `Server`，还会在预处理阶段前置校验 `WithValidation`；如果是网络 Blueprint 事件，则先生成 caller wrapper，再把真正的脚本函数名后缀成 `_Implementation`。直到 `AngelscriptClassGenerator` 真正生成 `UFunction` 时，才把这些中间状态统一落成 `FUNC_Net* / FUNC_NetReliable / FUNC_NetValidate / FUNC_BlueprintEvent`。这是一条明确的两阶段 state machine，而不是单阶段 flag patch。

```
[D3-Deep] RPC / Blueprint Lowering Stage

[UnrealCSharp]
managed method attributes
├─ FReflectionRegistry caches attribute classes
└─ FDynamicGeneratorCore::SetFlags()
   ├─ BlueprintNativeEvent / ImplementableEvent -> FUNC_Event + FUNC_BlueprintEvent
   ├─ Server / Client / NetMulticast -> FUNC_Net*
   ├─ Reliable / WithValidation -> FUNC_NetReliable / FUNC_NetValidate
   └─ reject BlueprintEvent + replicated combo at lowering time

[Angelscript]
script UFUNCTION specifiers
├─ Preprocessor parses specifiers into FunctionDesc booleans
│  ├─ bBlueprintEvent / bBlueprintOverride
│  ├─ bNetServer / bNetClient / bNetMulticast
│  └─ bNetValidate / bBlueprintAuthorityOnly
├─ GenerateBlueprintEventWrapper()
│  └─ rename script entry to *_Implementation
└─ ClassGenerator materializes final UFunction flags
   ├─ FUNC_BlueprintEvent
   ├─ FUNC_Net*
   └─ default reliable unless explicitly unreliable
```

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Reflection/FReflectionRegistry.cpp:346-383`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Reflection/FReflectionRegistry.cpp
// 函数: FReflectionRegistry::Initialize
// 位置: 把托管 function attribute 注册成可查询的 reflection class
// ============================================================================
BlueprintImplementableEventAttributeClass = GetClass(
    COMBINE_NAMESPACE(NAMESPACE_ROOT, NAMESPACE_DYNAMIC), CLASS_BLUEPRINT_IMPLEMENTABLE_EVENT_ATTRIBUTE);

BlueprintNativeEventAttributeClass = GetClass(
    COMBINE_NAMESPACE(NAMESPACE_ROOT, NAMESPACE_DYNAMIC), CLASS_BLUEPRINT_NATIVE_EVENT_ATTRIBUTE);

ServerAttributeClass = GetClass(
    COMBINE_NAMESPACE(NAMESPACE_ROOT, NAMESPACE_DYNAMIC), CLASS_SERVER_ATTRIBUTE);

ClientAttributeClass = GetClass(
    COMBINE_NAMESPACE(NAMESPACE_ROOT, NAMESPACE_DYNAMIC), CLASS_CLIENT_ATTRIBUTE);

NetMulticastAttributeClass = GetClass(
    COMBINE_NAMESPACE(NAMESPACE_ROOT, NAMESPACE_DYNAMIC), CLASS_NET_MULTICAST_ATTRIBUTE);

ReliableAttributeClass = GetClass(
    COMBINE_NAMESPACE(NAMESPACE_ROOT, NAMESPACE_DYNAMIC), CLASS_RELIABLE_ATTRIBUTE);

WithValidationAttributeClass = GetClass(
    COMBINE_NAMESPACE(NAMESPACE_ROOT, NAMESPACE_DYNAMIC), CLASS_WITH_VALIDATION_ATTRIBUTE);
// ★ UnrealCSharp 先把托管 attribute 类型装进 registry，后续 SetFlags() 只做属性查询
```

关键源码 [2] `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGeneratorCore.cpp:515-705`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGeneratorCore.cpp
// 函数: FDynamicGeneratorCore::SetFlags
// 位置: 动态 UFunction 生成时直接把 C# attribute 降成 UE FunctionFlags
// ============================================================================
if (InReflection->HasAttribute(FReflectionRegistry::Get().GetBlueprintNativeEventAttributeClass()))
{
    if (InFunction->FunctionFlags & FUNC_Net)
    {
        UE_LOG(LogUnrealCSharp, Error, TEXT("BlueprintNativeEvent functions cannot be replicated!"));
        // ★ replicated BlueprintNativeEvent 在这里直接被拦住
    }
    ...
    InFunction->FunctionFlags |= FUNC_Event;
    InFunction->FunctionFlags |= FUNC_BlueprintEvent;
}

if (InReflection->HasAttribute(FReflectionRegistry::Get().GetBlueprintImplementableEventAttributeClass()))
{
    if (InFunction->FunctionFlags & FUNC_Net)
    {
        UE_LOG(LogUnrealCSharp, Error, TEXT("BlueprintImplementableEvent functions cannot be replicated!"));
        // ★ ImplementableEvent + Net 组合同样被拒绝
    }
    ...
    InFunction->FunctionFlags |= FUNC_Event;
    InFunction->FunctionFlags |= FUNC_BlueprintEvent;
    InFunction->FunctionFlags &= ~FUNC_Native;
}

if (InReflection->HasAttribute(FReflectionRegistry::Get().GetServerAttributeClass()))
{
    InFunction->FunctionFlags |= FUNC_Net;
    InFunction->FunctionFlags |= FUNC_NetServer;
}

if (InReflection->HasAttribute(FReflectionRegistry::Get().GetClientAttributeClass()))
{
    InFunction->FunctionFlags |= FUNC_Net;
    InFunction->FunctionFlags |= FUNC_NetClient;
}

if (InReflection->HasAttribute(FReflectionRegistry::Get().GetNetMulticastAttributeClass()))
{
    InFunction->FunctionFlags |= FUNC_Net;
    InFunction->FunctionFlags |= FUNC_NetMulticast;
}

if (InReflection->HasAttribute(FReflectionRegistry::Get().GetReliableAttributeClass()))
{
    InFunction->FunctionFlags |= FUNC_NetReliable;
}

if (InReflection->HasAttribute(FReflectionRegistry::Get().GetWithValidationAttributeClass()))
{
    InFunction->FunctionFlags |= FUNC_NetValidate;
}

if (InFunction->FunctionFlags & FUNC_Net)
{
    InFunction->FunctionFlags |= FUNC_Event;
    // ★ 网络函数最后统一补 FUNC_Event
}
```

关键源码 [3] `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:1592-1643`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3455-3483`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp
// 函数: UFUNCTION specifier parser
// 位置: 先把脚本 specifier 解析成 FunctionDesc 中间状态，并按需生成 wrapper
// ============================================================================
bool bAlreadyHasWrapper = FunctionDesc->bBlueprintEvent;

if (!bHadNotCallable)
    FunctionDesc->bBlueprintCallable = true;

FunctionDesc->bBlueprintEvent = true;
FunctionDesc->bNetMulticast = Spec.Name == PP_NAME_NetMulticast;
FunctionDesc->bNetClient = Spec.Name == PP_NAME_NetClient;
FunctionDesc->bNetServer = Spec.Name == PP_NAME_NetServer;

#if AS_ENFORCE_SERVER_RPC_VALIDATION
if (FunctionDesc->bNetServer)
{
    if (!Specs.FindByPredicate([](FSpecifier& CurSpec) -> bool { return CurSpec.Name == PP_NAME_WithValidation; }))
    {
        MacroError(File, Macro, FString::Printf(TEXT(
            "UFUNCTION() %s is marked as Server but does not have the WithValidation property specified!"
        ), *FunctionDesc->FunctionName));
        // ★ Server RPC 缺少 WithValidation 会在预处理期直接报错
    }
}
#endif

if (!bAlreadyHasWrapper)
{
    FunctionDesc->bCanOverrideEvent = false;
    GenerateBlueprintEventWrapper(File, Chunk, Macro, FunctionDesc);
    FunctionDesc->ScriptFunctionName += TEXT("_Implementation");
    // ★ 先生成 caller wrapper，再把真实脚本入口后缀成 _Implementation
}

else if (Spec.Name == PP_NAME_WithValidation)
{
    if (Specs.FindByPredicate([](FSpecifier& CurSpec) -> bool
        { return CurSpec.Name == PP_NAME_NetServer || CurSpec.Name == PP_NAME_NetClient; }))
    {
        FunctionDesc->bNetValidate = true;
    }
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 函数: 生成 UFunction flags
// 位置: ClassGenerator 最后把 FunctionDesc 布尔状态落成 UE FunctionFlags
// ============================================================================
if ((FunctionDesc->bBlueprintEvent && FunctionDesc->bCanOverrideEvent) || FunctionDesc->bBlueprintOverride)
    NewFunction->FunctionFlags |= FUNC_BlueprintEvent;
if (FunctionDesc->bNetMulticast)
    NewFunction->FunctionFlags |= FUNC_NetMulticast;
if (FunctionDesc->bNetClient)
    NewFunction->FunctionFlags |= FUNC_NetClient;
if (FunctionDesc->bNetServer)
    NewFunction->FunctionFlags |= FUNC_NetServer;
if (FunctionDesc->bNetValidate)
{
    NewFunction->FunctionFlags |= FUNC_NetValidate;
    FunctionsWithValidate.Add(NewFunction);
}
if ((NewFunction->FunctionFlags & FUNC_NetFuncFlags) != 0)
{
    NewFunction->FunctionFlags |= FUNC_Net;
    if (!FunctionDesc->bUnreliable)
        NewFunction->FunctionFlags |= FUNC_NetReliable;
    // ★ Angelscript 把 “是否网络函数” 和 “是否可靠” 作为生成期的第二阶段决策
}
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| RPC 元数据入口 | UnrealCSharp 由 `FReflectionRegistry + SetFlags()` 直接从托管 attribute 改 `FunctionFlags`；Angelscript 先经 `Preprocessor -> FunctionDesc`，再由 `ClassGenerator` 统一落旗标 | 实现方式不同 |
| `BlueprintEvent + Replication` 组合 | UnrealCSharp 当前动态导出链会在 `BlueprintNativeEvent/ImplementableEvent + FUNC_Net` 时直接报错；Angelscript 则通过 wrapper + `_Implementation` 路径承载网络 Blueprint 事件 | 没有实现（就 UnrealCSharp 当前动态 C# function 导出链的 replicated BlueprintEvent 而言） |
| `Server <-> WithValidation` 约束 | Angelscript 在预处理期强制 `Server` 必须配 `WithValidation`，并拒绝孤立的 `WithValidation`；UnrealCSharp 本轮定位到的 `SetFlags()` 只在 attribute 存在时设置 `FUNC_NetValidate`，未见对等的前置配对报错 | 实现质量差异 |
| 可靠性默认值 | UnrealCSharp 只有显式 `[Reliable]` 才置 `FUNC_NetReliable`；Angelscript 对网络函数默认 reliable，只有显式 `Unreliable` 才降级 | 实现方式不同 |

### [维度 D8] 重载收口策略：assembly discard vs precompiled pointer retarget

前面的 D8 已经覆盖了 `JIT/AOT`、GC 和调用缓冲。这一轮只补“当一轮生成/装载结束后，旧执行体是怎么退场的”。UnrealCSharp 的单位非常鲜明：**整组 managed assemblies + collectible `AssemblyLoadContext`**。`InitializeAssembly()` 在 editor 下先创建可回收的 `AssemblyLoadContext`，再把每个 DLL 通过 `LoadFromStream` 装进去；退场时 `UnloadAssembly()` 会统一释放每个 assembly 对应的 `GCHandle`、关闭所有 `MonoImage`、清空 `Assemblies/Images` 容器，最后 `DeinitializeAssemblyLoadContext()` 再显式调用 `AssemblyLoadContext.Unload()`。

Angelscript 的预编译/JIT 路线没有走“丢掉整份执行体再重装”这条路，而是选择**在原 VM 内重定向指针再裁剪运行时表**。`PrepareToFinalizePrecompiledModules()` 会把 `FJITDatabase` 里记录的函数入口、系统函数指针、类型信息指针统统从 precompiled reference 重新映射回 live runtime object；之后 `ClearUnneededRuntimeData()` 再按“运行时还需不需要”来擦空 object type 的 property/method table，并删除未使用的 system function。也就是说，Angelscript 在 finalize 阶段的目标不是丢弃整个 VM，而是让同一份 VM 内部的指针关系重新对齐，并把不再需要的编译态数据裁掉。

```
[D8-Deep] Reload Finalization Strategy

[UnrealCSharp]
InitializeAssembly()
├─ InitializeAssemblyLoadContext()        // collectible ALC
└─ LoadAssembly(DLL set)
   ├─ LoadFromStream()
   ├─ AssemblyGCHandles += reflection assembly
   └─ Images / Assemblies += loaded payload

Deinitialize()
├─ UnloadAssembly()
│  ├─ GCHandle_Free_V2(all assemblies)
│  ├─ mono_image_close(all images)
│  └─ reset Assemblies / Images
└─ DeinitializeAssemblyLoadContext()
   └─ AssemblyLoadContext.Unload()

[Angelscript]
Compile / finalize
├─ PrepareToFinalizePrecompiledModules()
│  ├─ rebind JIT function lookups
│  ├─ rebind system function pointers
│  └─ rebind type info lookups
├─ stage4 template validation
└─ ClearUnneededRuntimeData()
   ├─ erase property/method tables
   └─ delete unused system functions
```

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp:135-145`、`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp:560-567,606-625,734-748`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp
// 函数: FMonoDomain::Deinitialize / InitializeAssembly / DeinitializeAssemblyLoadContext / UnloadAssembly
// 位置: managed assemblies 的完整退场路径
// ============================================================================
void FMonoDomain::Deinitialize()
{
    if (bLoadSucceed)
    {
        FReflectionRegistry::Get().Deinitialize();
    }

    UnloadAssembly();
    DeinitializeAssembly();
    // ★ domain 退场先丢整个 assembly set，再做 assembly-level 收尾
}

void FMonoDomain::InitializeAssembly(const TArray<FString>& InAssemblies)
{
#if WITH_EDITOR
    InitializeAssemblyLoadContext();
#endif
    LoadAssembly(InAssemblies); // ★ 每轮装载都绑定到 collectible ALC
}

void FMonoDomain::DeinitializeAssemblyLoadContext()
{
    if (AssemblyLoadContextGCHandle == nullptr)
    {
        return;
    }

    const auto AssemblyLoadContextObject = GCHandle_Get_Target_V2(AssemblyLoadContextGCHandle);
    const auto AssemblyLoadContextClass = mono_object_get_class(AssemblyLoadContextObject);
    const auto UnloadMethod = Class_Get_Method_From_Name(AssemblyLoadContextClass, TEXT("Unload"), 0);

    Runtime_Invoke(UnloadMethod, AssemblyLoadContextObject, nullptr, &Exception);
    GCHandle_Free_V2(AssemblyLoadContextGCHandle);
    AssemblyLoadContextGCHandle = nullptr;
    // ★ ALC 是显式、可回收的装载边界
}

void FMonoDomain::UnloadAssembly()
{
    for (const auto GCHandle : AssemblyGCHandles)
    {
        GCHandle_Free_V2(GCHandle);
    }
    AssemblyGCHandles.Reset();

    for (const auto& Image : Images)
    {
        mono_image_close(Image);
    }
    Images.Reset();
    Assemblies.Reset();
    bLoadSucceed = false;
    // ★ 旧执行体按“整组 assembly”被丢弃，而不是就地修补入口表
}
```

关键源码 [2] `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp:2376-2418,2481-2542`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp
// 函数: PrepareToFinalizePrecompiledModules / ClearUnneededRuntimeData
// 位置: 预编译数据在同一 VM 内做指针重映射与裁剪
// ============================================================================
void FAngelscriptPrecompiledData::PrepareToFinalizePrecompiledModules()
{
    if (FAngelscriptEngine::IsScriptDevelopmentModeForCurrentContext())
        return;

    FJITDatabase& Database = FJITDatabase::Get();
    for (void** FuncPtr : Database.FunctionLookups)
    {
        *FuncPtr = GetFunction(FAngelscriptPrecompiledReference{ (int64)*FuncPtr }, false);
        // ★ 不是丢弃旧 JIT lookup，而是把 archive ref 重定向到 live function
    }

    for (void* SysPtr : Database.SystemFunctionPointerLookups)
    {
        auto* JitRef = (FJitRef_SystemFunctionPointer*)SysPtr;
        asCScriptFunction* Function = GetFunction(
            FAngelscriptPrecompiledReference{ (int64)JitRef->Pointer }, false);
        ...
        JitRef->Method = SysFunc->method;
        // ★ 系统函数指针同样在 finalize 前重绑
    }

    for (void** TypePtr : Database.TypeInfoLookups)
    {
        *TypePtr = GetTypeInfo(FAngelscriptPrecompiledReference{ (int64)*TypePtr }, false);
    }
}

void FAngelscriptPrecompiledData::ClearUnneededRuntimeData()
{
    auto ClearObjectType = [&](asCObjectType* objType)
    {
        objType->propertyTable.EraseAll();
        objType->methodTable.EraseAll();
    };

    for (int32 i = 0, Count = Engine->registeredObjTypes.GetLength(); i < Count; ++i)
    {
        asCObjectType* objType = Engine->registeredObjTypes[i];
        if (objType == nullptr)
            continue;
        ClearObjectType(objType);
    }

    for (int32 i = 0, Count = Engine->scriptFunctions.GetLength(); i < Count; ++i)
    {
        asCScriptFunction* func = Engine->scriptFunctions[i];
        if (func == nullptr)
            continue;
        if (func->funcType == asFUNC_SYSTEM && !func->isInUse)
        {
            ...
            asDELETE(func, asCScriptFunction);
            // ★ 编译态残留按“未使用 system function”粒度被裁掉
        }
    }
}
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 执行体替换粒度 | UnrealCSharp 以 `collectible AssemblyLoadContext + assembly set` 为替换单位；Angelscript 在原 VM 内重绑 lookup/pointer 后继续运行 | 实现方式不同 |
| finalize 前的核心动作 | UnrealCSharp 主要是释放 `GCHandle`、关闭 `MonoImage`、卸载 ALC；Angelscript 主要是 `PrepareToFinalizePrecompiledModules()` 重定向 JIT/type lookup | 实现方式不同 |
| 编译态数据清理 | Angelscript 额外提供 `ClearUnneededRuntimeData()` 在同一 VM 内裁掉 property/method table 和未使用 system function；UnrealCSharp 当前这条退场路径没有对等的“就地裁表”逻辑，因为它直接丢弃整组 assembly | 实现方式不同 |
| 热替换保留策略 | UnrealCSharp 倾向用可回收 assembly boundary 解决“旧代码如何退场”；Angelscript 倾向保留 VM，再把入口与元数据对齐到新状态 | 实现质量差异，侧重点不同 |

### [维度 D11] 交付闭包层级：DLL-only staged payload vs build-variant cache set

前面的 D11 已经分析过 `PublishDirectory`、`NetDirectory` 和 `BuildIdentifier/DataGuid`。这一轮只补“交付闭包里到底有哪些文件被插件自己承认”。UnrealCSharp 的证据非常直接：`Template/Game.props` 在 `Publish` 后只复制 `$(PublishDir)*.dll`；`GetFullAssemblyPublishPath()` 也只枚举 `UE.dll / Game.dll / CustomProjects.dll`；`UAssemblyLoader::Load()` 进一步把 runtime 查找固定成 `AssemblyName + ".dll"`。结合 `GetAssemblyPath()` 返回的两个根目录，可以得出一个很清晰的闭包模型：**项目侧交付物只有 staged DLL；基础运行时依赖则来自外部 `NetDirectory` 根**。

需要显式说明哪部分是源码直证、哪部分是推断。源码直证是“复制名单只匹配 `*.dll`”以及“装载器只按 `AssemblyName + DLL_SUFFIX` 查找”；据此可以推断 `.pdb`、`.deps.json`、`.runtimeconfig.json` 等 sidecar **不在当前插件自定义装载合同内**。它们即便被别的流程部署，也不会被这条插件内的 `UAssemblyLoader` 路径消费。

Angelscript 的交付闭包则是另一种形态。运行时首先从脚本根目录加载 `Binds.Cache`，再从插件基目录加载 `BindModules.Cache` 指定的 bind module 名单；预编译脚本部分则按 `Shipping/Test/Development` 构造 `PrecompiledScript_<Config>.Cache`，找不到时再退回 `PrecompiledScript.Cache`。这说明 Angelscript 的部署单位不是程序集依赖图，而是**脚本根目录缓存集 + 插件侧 bind module 清单**。

```
[D11-Deep] Delivery Closure Granularity

[UnrealCSharp]
MSBuild Build
├─ AfterBuildPublish -> Publish
├─ CopyDllsAfterPublish
│  └─ Content/PublishDir/*.dll
├─ eager assembly list
│  ├─ UE.dll
│  ├─ Game.dll
│  └─ CustomProjects.dll
└─ runtime probe roots
   ├─ ProjectContent/PublishDir
   └─ NetDirectory

[Angelscript]
Runtime startup
├─ ScriptRoot/Binds.Cache
├─ PluginBaseDir/BindModules.Cache
├─ ScriptRoot/PrecompiledScript_<Config>.Cache
└─ fallback ScriptRoot/PrecompiledScript.Cache
```

关键源码 [1] `Reference/UnrealCSharp/Template/Game.props:16-27`、`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp:1035-1049`、`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/AssemblyLoader.cpp:6-23`

```xml
<!-- =========================================================================
文件: Reference/UnrealCSharp/Template/Game.props
位置: Publish 后处理
说明: 交付清单只复制 PublishDir 下的 DLL
========================================================================== -->
<Target Name="AfterBuildPublish" AfterTargets="Build">
    <MSBuild Projects="$(ProjectPath)" Targets="Publish" Properties="Configuration=$(Configuration)" />
</Target>
<Target Name="CopyDllsAfterPublish" AfterTargets="Publish">
    <ItemGroup>
        <PublishFiles Include="$(PublishDir)*.dll" />
    </ItemGroup>
    <Copy SourceFiles="@(PublishFiles)" DestinationFolder="$(ScriptOutputPath)" />
    <!-- ★ 插件自定义交付名单是 DLL-only -->
</Target>
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp
// 函数: GetFullAssemblyPublishPath / GetAssemblyPath
// 位置: 运行时只关心 staged project assemblies + external net root
// ============================================================================
TArray<FString> FUnrealCSharpFunctionLibrary::GetFullAssemblyPublishPath()
{
    return TArrayBuilder<FString>().
           Add(GetFullUEPublishPath()).
           Add(GetFullGamePublishPath()).
           Append(GetFullCustomProjectsPublishPath()).
           Build();
    // ★ eager load 清单只列出 UE/Game/CustomProjects 的 DLL
}

TArray<FString> FUnrealCSharpFunctionLibrary::GetAssemblyPath()
{
    return TArrayBuilder<FString>().
           Add(FPaths::ProjectContentDir() / GetPublishDirectory()).
           Add(FMonoFunctionLibrary::GetNetDirectory()).
           Build();
    // ★ probe root 被拆成项目发布目录 + 外部 net runtime 目录
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/AssemblyLoader.cpp
// 函数: UAssemblyLoader::Load
// 位置: 装载器只按 AssemblyName + DLL_SUFFIX 查找
// ============================================================================
TArray<uint8> UAssemblyLoader::Load(const FString& InAssemblyName)
{
    auto AssemblyPaths = FUnrealCSharpFunctionLibrary::GetAssemblyPath();

    for (const auto& AssemblyPath : AssemblyPaths)
    {
        if (const auto File = FPaths::Combine(AssemblyPath, InAssemblyName) + DLL_SUFFIX;
            IFileManager::Get().FileExists(*File))
        {
            TArray<uint8> Data;
            FFileHelper::LoadFileToArray(Data, *File);
            return Data;
            // ★ 插件内装载路径完全不看 .pdb/.deps.json/.runtimeconfig.json
        }
    }
    return {};
}
```

关键源码 [2] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:781-793,1468-1477,1521-1555`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: GetScriptRootDirectory / Startup load path
// 位置: Angelscript 的交付闭包是脚本根目录缓存集 + 插件 bind module 清单
// ============================================================================
FString FAngelscriptEngine::GetScriptRootDirectory()
{
    const auto& AllRootPaths = CurrentEngine->AllRootPaths;
    return AllRootPaths.IsEmpty() ? TEXT("") : CurrentEngine->AllRootPaths[0];
    // ★ 第一个 root 被视为项目脚本根
}

FAngelscriptBindDatabase::Get().Load(GetScriptRootDirectory() / TEXT("Binds.Cache"), bGeneratePrecompiledData);

if (plugin)
{
    FAngelscriptBinds::LoadBindModules(plugin->GetBaseDir() / "BindModules.Cache");
    // ★ bind module 清单来自插件基目录，不在脚本根
}

Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript_Shipping.Cache");
...
if (!IFileManager::Get().FileExists(*Filename))
    Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript.Cache");

if (IFileManager::Get().FileExists(*Filename))
{
    PrecompiledData = new FAngelscriptPrecompiledData(Engine);
    PrecompiledData->Load(Filename);
    ...
    if (CompiledInfo != nullptr && CompiledInfo->PrecompiledDataGuid != PrecompiledData->DataGuid)
    {
        UE_LOG(Angelscript, Warning, TEXT(
            "Loaded angelscript precompiled data does not match the transpiled C++ in the game binary. Transpiled code will not be used!"
        ));
        // ★ build variant cache + static JIT 之间还有额外匹配校验
    }
}
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 插件自定义交付名单 | UnrealCSharp 的 `CopyDllsAfterPublish` 只复制 `*.dll`，`UAssemblyLoader` 也只按 `AssemblyName + ".dll"` 查找；Angelscript 交付的是 `Binds.Cache`、`BindModules.Cache`、`PrecompiledScript*.Cache` 这类缓存集 | 实现方式不同 |
| 运行时闭包根目录 | UnrealCSharp 闭包天然分成 `ProjectContent/PublishDir + NetDirectory` 两层；Angelscript 则主要依赖脚本根目录，再辅以插件基目录的 bind module 清单 | 实现方式不同 |
| sidecar 文件地位 | 基于 `Game.props` 与 `UAssemblyLoader::Load()` 可以推断，`.pdb/.deps.json/.runtimeconfig.json` 不在 UnrealCSharp 当前插件内装载合同中；Angelscript 的合同则根本不是程序集 sidecar 模型 | 实现方式不同（这里关于 sidecar 的缺席是源码推断） |
| 交付物与二进制匹配 | Angelscript 在 cache 集之外还会校验 `PrecompiledDataGuid` 与编进游戏二进制的 transpiled code；UnrealCSharp 这条 DLL-only 路径更依赖目录与文件名契约 | 实现质量差异 |

---

## 深化分析 (2026-04-09 00:40:58)

### [维度 D2] 暴露面治理：allowlist 先裁剪 vs failure-reason 后记账

前面的 D2 主要看 override 和类型桥接。这一轮只追问一个更前置的问题：**在真正生成绑定之前，谁决定“哪些东西有资格进入脚本暴露面”**。UnrealCSharp 的答案是“先按配置和声明属性裁剪，再进入生成”。`UUnrealCSharpEditorSetting` 先定义 `SupportedModule`、`SupportedAssetPath`、`SupportedAssetClass` 与 `bEnableExport`；`FGeneratorCore::BeginGenerator()` 把这些设置拷入生成期上下文，`IsSupported()` 再递归检查 package、super class、interface 与函数参数，只要上游有一层不满足就直接阻断该类型/函数进入生成链。与此同时，`ScriptNoExportAttribute`、`ExportAttribute`、`TextExportTransientAttribute` 等 attribute 已被注册进 `FReflectionRegistry`，后续 `FDynamicGeneratorCore` 会把它们下沉成 metadata 或 `CPF_ExportObject` / `CPF_TextExportTransient` 这类 property flag。

Angelscript 的函数表治理方式则明显更“审计型”。`AngelscriptFunctionTableExporter` 会先把所有 `BlueprintCallable/Pure` 都扫一遍，再调用 `AngelscriptFunctionSignatureBuilder` / `AngelscriptHeaderSignatureResolver` 尝试重建 native 头文件签名；失败不会在前面被 allowlist 直接拦掉，而是被记录成 `non-public`、`unexported-symbol`、`header-missing`、`overloaded-unresolved` 等 `failureReason`，最后统一写进 `AS_FunctionTable_SkippedEntries.csv` 与 `AS_FunctionTable_SkippedReasonSummary.csv`。同时，`AngelscriptFunctionTableCodeGenerator` 还会反向解析 `AngelscriptRuntime.Build.cs` 得到支持模块集，这说明它的“治理”更接近 **scan everything -> explain every rejection**，而不是 UnrealCSharp 那种 **pre-filter -> generate only admitted surface**。

```
[D2-Deep] Export Surface Governance

[UnrealCSharp]
EditorSetting
├─ SupportedModule
├─ SupportedAssetPath / SupportedAssetClass
├─ FGeneratorCore::IsSupported()
│  ├─ package gate
│  ├─ super/interface recursion
│  └─ parameter type recursion
└─ attribute lowering
   ├─ ScriptNoExportAttribute
   ├─ ExportAttribute
   └─ TextExportTransientAttribute

[Angelscript]
UHT scan
├─ LoadSupportedModules(Build.cs)
├─ visit BlueprintCallable/Pure
├─ TryBuild signature
│  ├─ success -> generate table shard
│  └─ fail -> failureReason
└─ write skipped CSV / reason summary
```

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpEditorSetting.h:122-148`、`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Setting/UnrealCSharpEditorSetting.cpp:48-73`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpEditorSetting.h
// 位置: 生成期暴露面配置
// 说明: allowlist 与 export 开关先存在 Project Settings，而不是散落在生成器内部
// ============================================================================
UPROPERTY(Config, EditAnywhere, Category = Generator,
    meta = (GetOptions = "GetModuleList", EditCondition = "!bIsGenerateAllModules"))
TArray<FString> SupportedModule;

UPROPERTY(Config, EditAnywhere, Category = Generator,
    meta = (LongPackageName, ForceShowEngineContent, ForceShowPluginContent, EditCondition = "bIsGenerateAsset"))
TArray<FDirectoryPath> SupportedAssetPath;

UPROPERTY(Config, EditAnywhere, Category = Generator, meta = (EditCondition = "bIsGenerateAsset"))
TArray<TSubclassOf<UObject>> SupportedAssetClass;

UPROPERTY(Config, EditAnywhere, Category = Generator)
bool bEnableExport;

UPROPERTY(Config, EditAnywhere, Category = Generator,
    meta = (GetOptions = "GetModuleList", EditCondition = "bEnableExport"))
TArray<FString> ExportModule;

UPROPERTY(Config, EditAnywhere, Category = Generator,
    meta = (GetOptions = "GetClassList", EditCondition = "bEnableExport"))
TArray<FString> ClassBlacklist;
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Setting/UnrealCSharpEditorSetting.cpp
// 函数: UUnrealCSharpEditorSetting::RegisterSettings
// 位置: 默认 allowlist 注入
// ============================================================================
static TSet<FString> DefaultSupportedModules =
{
    TEXT("Core"),
    TEXT("CoreUObject"),
    TEXT("Engine"),
    TEXT("SlateCore"),
    TEXT("UMG"),
    TEXT("UnrealCSharpCore"),
    FApp::GetProjectName()
};

static TSet<FString> DefaultSupportedAssetPaths =
{
    TEXT("/Game")
};

static TSet<TSubclassOf<UObject>> DefaultSupportedAssetClasses =
{
    UBlueprint::StaticClass(),
    UUserDefinedStruct::StaticClass(),
    UUserDefinedEnum::StaticClass(),
    UWidgetBlueprint::StaticClass()
};
// ★ 默认支持面本身就是“项目内容 + 常用可脚本化资产类型”
```

关键源码 [2] `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:794-875,940-978`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp
// 函数: IsSupported / BeginGenerator
// 位置: 真正把 allowlist 落成递归裁剪逻辑
// ============================================================================
bool FGeneratorCore::IsSupported(const UPackage* InPackage)
{
    if (InPackage != nullptr)
    {
        const auto& PackageName = InPackage->GetName();
        for (const auto& AssetPath : SupportedAssetPath)
        {
            if (PackageName.StartsWith(AssetPath))
            {
                return true; // ★ package 不在允许路径里，后续 class/struct/enum 全部失去资格
            }
        }
    }
    return false;
}

bool FGeneratorCore::IsSupported(const UClass* InClass)
{
    if (!IsSupported(InClass->GetPackage()))
        return false;

    if (const auto SuperClass = InClass->GetSuperClass())
    {
        if (!IsSupported(SuperClass))
            return false; // ★ 父类不被允许，子类也直接拒绝
    }

    for (const auto& Interface : InClass->Interfaces)
    {
        if (!IsSupported(Interface.Class))
            return false; // ★ interface 依赖同样要通过 allowlist
    }

    return true;
}

bool FGeneratorCore::IsSupported(const UFunction* InFunction)
{
    for (TFieldIterator<FProperty> ParamIterator(InFunction); ParamIterator && (ParamIterator->PropertyFlags & CPF_Parm); ++ParamIterator)
    {
        if (!IsSupported(*ParamIterator))
            return false; // ★ 参数类型一旦超出支持面，函数整体拒绝生成
    }
    return true;
}

void FGeneratorCore::BeginGenerator(const bool bIsFull)
{
    ...
    for (const auto& Module : UnrealCSharpEditorSetting->GetSupportedModule())
        SupportedModule.Add(FString::Printf(TEXT("%s.%s"), *NAMESPACE_ROOT, *Module));

    for (const auto& [Path] : UnrealCSharpEditorSetting->GetSupportedAssetPath())
        SupportedAssetPath.Add(*FString::Printf(TEXT("%s/"), *Path));

    for (const auto& AssetClass : UnrealCSharpEditorSetting->GetSupportedAssetClass())
        SupportedAssetClassName.Add(AssetClass->GetFName());
    // ★ 运行前先把设置拍平，后面所有生成器共用一份 admission contract
}
```

关键源码 [3] `Reference/UnrealCSharp/Script/UE/Dynamic/Property/ScriptNoExportAttribute.cs:1-9`、`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Reflection/FReflectionRegistry.cpp:262-263,325-326,647-648`、`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGeneratorCore.cpp:362-379,1200-1244`

```csharp
// ============================================================================
// 文件: Reference/UnrealCSharp/Script/UE/Dynamic/Property/ScriptNoExportAttribute.cs
// 位置: 声明侧显式 opt-out
// ============================================================================
namespace Script.Dynamic
{
    [AttributeUsage(AttributeTargets.Property)]
    public class ScriptNoExportAttribute : Attribute
    {
        private string Value { get; set; } = "true";
    }
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGeneratorCore.cpp
// 位置: attribute 被注册后，不只是“记住名字”，而是会下沉到 property/meta 生成
// ============================================================================
if (InReflection->HasAttribute(FReflectionRegistry::Get().GetTextExportTransientAttributeClass()))
{
    InProperty->SetPropertyFlags(CPF_TextExportTransient);
}

if (InReflection->HasAttribute(FReflectionRegistry::Get().GetExportAttributeClass()))
{
    InProperty->SetPropertyFlags(CPF_ExportObject);
}

static TArray<FClassReflection*> PropertyMetaDataAttributes = {
    ...
    ReflectionRegistry.GetScriptNoExportAttributeClass(),
    ...
};

static TArray<FClassReflection*> FunctionMetaDataAttributes = {
    ...
    ReflectionRegistry.GetScriptNoExportAttributeClass(),
    ...
};
// ★ ScriptNoExportAttribute 已经进入 property / function metadata 白名单，说明它不是 IDE 注释，而是生成期协议的一部分
```

关键源码 [4] `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:15-58,72-88,99-161`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs:43-80`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs:18-66,101-105`

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs
// 位置: 先扫描，再把拒绝原因落成台账
// ============================================================================
private sealed record AngelscriptSkippedFunctionEntry(
    string ModuleName,
    string ClassName,
    string FunctionName,
    string FailureReason);

foreach (UhtModule module in factory.Session.Modules)
{
    CountBlueprintCallableFunctions(module.ShortName, module.ScriptPackage, skippedEntries, ref classCount, ref functionCount, ref reconstructedCount, ref skippedCount);
}

if (AngelscriptFunctionSignatureBuilder.TryBuild(classObj, function, out AngelscriptFunctionSignature? signature, out string? failureReason))
{
    _ = signature!.BuildEraseMacro();
    reconstructedCount++;
}
else
{
    skippedEntries.Add(new AngelscriptSkippedFunctionEntry(
        moduleName,
        classObj.SourceName,
        function.SourceName,
        string.IsNullOrEmpty(failureReason) ? "unknown" : failureReason));
}

WriteSkippedEntriesCsv(factory, skippedEntries);
WriteSkippedReasonSummaryCsv(factory, skippedEntries);
// ★ 失败不会静默吞掉，而是稳定写成 CSV 供后续审计
```

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs
// 函数: TryBuild
// 位置: “失败类型” 本身就是这条治理链的重要产物
// ============================================================================
if (failureReason == "non-public" || failureReason == "unexported-symbol")
{
    return false;
}

if (failureReason == "overloaded-unresolved" && !IsWhitelistedDirectBindFallback(classObj, function))
{
    return false;
}

if (property.ArrayDimensions != null)
{
    failureReason = "static-array-parameter";
    return false;
}
// ★ 这里的核心不是 attribute，而是把无法自动重建的 native 形状精确分类
```

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs
// 函数: TryBuild
// 位置: 失败原因来自原生头文件可链接性与声明形状
// ============================================================================
if (classObj.HeaderFile == null || string.IsNullOrEmpty(classObj.HeaderFile.FilePath) || !File.Exists(classObj.HeaderFile.FilePath))
{
    failureReason = "header-missing";
    return false;
}

if (publicCandidates.Count == 0)
{
    failureReason = "non-public";
    return false;
}

if (!HasLinkableExport(classObj, classDeclaration, candidate.Declaration))
{
    failureReason = "unexported-symbol";
    return false;
}

failureReason = matchedUnexportedSymbol ? "unexported-symbol" : "overloaded-unresolved";
// ★ Angelscript 的 admission contract 更依赖 native 头文件事实，而不是脚本声明 attribute
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 暴露面入口 | UnrealCSharp 先由 `SupportedModule/SupportedAssetPath/SupportedAssetClass` 构建 allowlist，再进入生成；Angelscript 先扫 `BlueprintCallable/Pure`，失败后记 `failureReason` | 实现方式不同 |
| 声明侧 opt-out | UnrealCSharp 已有 `ScriptNoExportAttribute` 并进入 property/function metadata attribute 集；Angelscript 当前函数表自动导出链主要依赖 native metadata 与 header 失败原因 | 实现方式不同 |
| 审计可观测性 | Angelscript 会稳定产出 `AS_FunctionTable_SkippedEntries.csv` 与 `SkippedReasonSummary.csv`；本轮未定位到 UnrealCSharp 对等的“导出缺口台账”文件 | 实现质量差异 |
| 模块支持面的来源 | UnrealCSharp 的支持面来自 Project Settings；Angelscript 会反向解析 `AngelscriptRuntime.Build.cs` 构建支持模块集 | 实现方式不同 |

### [维度 D3] CustomThunk 自动化边界：managed attribute 预留点 vs native fallback 明确拒绝

前面的 D3 已经讨论过 override dispatch 和 Blueprint 继承链。这一轮补的是**自动化桥接在哪些地方主动停手**。UnrealCSharp 在声明侧显式暴露了 `CustomThunkAttribute`，`FReflectionRegistry` 也把它注册进动态 attribute 表；`FDynamicGeneratorCore::SetFlags()` 在看到该 attribute，或者看到 `ServiceRequestAttribute` 这种天然需要特殊 thunk 的函数时，会把本地变量 `FunctionExportFlags` 加上 `FUNCEXPORT_CustomThunk`。但要注意，这里要明确区分“源码直证”和“推断”：当前 reference 快照里，`EDynamicFunctionExportFlags` / `FunctionExportFlags` 的定义与赋值都只出现在 `FDynamicGeneratorCore.cpp`，本轮未在同仓库中定位到把这个 flag 序列化、写回 blueprint graph、或下发到后续 generator 的消费点。也就是说，**当前可见源码证明了 authoring hook 的存在，但没有证明 custom thunk 闭环已经在当前快照内可见地完成**。

Angelscript 则把这条边界写得很实。`AngelscriptFunctionTableCodeGenerator` 直接拒绝 `FunctionExportFlags` 含 `CustomThunk` 的 `UFunction` 进入自动函数表；运行时的 `BlueprintCallableReflectiveFallback` 也会先检查 `CustomThunk` metadata，命中后立刻返回 `RejectedCustomThunk`，完全不尝试 reflective fallback。换句话说，Angelscript 没有把 custom thunk 视作“也许还能自动处理一下”的情况，而是明确把它划出自动化边界，要求走 direct bind 或专门处理路径。

```
[D3-Deep] CustomThunk Automation Boundary

[UnrealCSharp]
C# declaration
├─ [CustomThunkAttribute]
├─ FReflectionRegistry registers attribute class
├─ FDynamicGeneratorCore::SetFlags()
│  └─ local FUNCEXPORT_CustomThunk bit
└─ current snapshot: no downstream sink located

[Angelscript]
Native UFunction
├─ UHT table generator
│  └─ reject FunctionExportFlags.CustomThunk
├─ reflective fallback
│  └─ reject metadata "CustomThunk"
└─ manual/direct bind path required
```

关键源码 [1] `Reference/UnrealCSharp/Script/UE/Dynamic/Function/CustomThunkAttribute.cs:1-8`、`Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/CoreMacro/FunctionAttributeMacro.h:21-29`、`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Reflection/FReflectionRegistry.cpp:379-383`、`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGeneratorCore.cpp:603-640`

```csharp
// ============================================================================
// 文件: Reference/UnrealCSharp/Script/UE/Dynamic/Function/CustomThunkAttribute.cs
// 位置: C# 声明侧的显式标记
// ============================================================================
namespace Script.Dynamic
{
    [AttributeUsage(AttributeTargets.Method)]
    public class CustomThunkAttribute : Attribute
    {
    }
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGeneratorCore.cpp
// 函数: SetFlags
// 位置: CustomThunk 被折叠成局部 export flag
// ============================================================================
if (InReflection->HasAttribute(FReflectionRegistry::Get().GetServiceRequestAttributeClass()))
{
    InFunction->FunctionFlags |= FUNC_Net;
    InFunction->FunctionFlags |= FUNC_NetReliable;
    InFunction->FunctionFlags |= FUNC_NetRequest;
    FunctionExportFlags |= static_cast<uint32>(EDynamicFunctionExportFlags::FUNCEXPORT_CustomThunk);
    // ★ 某些网络语义天然要求特殊 thunk
}

#if WITH_EDITOR
if (InReflection->HasAttribute(FReflectionRegistry::Get().GetCustomThunkAttributeClass()))
{
    FunctionExportFlags |= static_cast<uint32>(EDynamicFunctionExportFlags::FUNCEXPORT_CustomThunk);
    // ★ 脚本作者也可以显式要求 custom thunk
}
#endif
```

关键源码 [2] `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:497-514`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp:17-18,272-287,374-399`

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs
// 位置: 自动函数表对 CustomThunk 的入口拒绝
// ============================================================================
if (!AngelscriptFunctionTableExporter.IsBlueprintCallable(function))
{
    return false;
}

if (function.MetaData.ContainsKey("NotInAngelscript") ||
    (function.MetaData.ContainsKey("BlueprintInternalUseOnly") && !function.MetaData.ContainsKey("UsableInAngelscript")))
{
    return false;
}

return !function.FunctionExportFlags.ToString().Contains("CustomThunk", StringComparison.Ordinal);
// ★ CustomThunk 在 UHT 自动化阶段就被排除出函数表
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp
// 位置: 运行时 reflective fallback 再次明确拒绝 CustomThunk
// ============================================================================
const FName NAME_BlueprintCallableReflectiveFallback_CustomThunk(TEXT("CustomThunk"));

if (Function->HasMetaData(NAME_BlueprintCallableReflectiveFallback_CustomThunk))
{
    return EAngelscriptReflectiveFallbackEligibility::RejectedCustomThunk;
}

bool BindBlueprintCallableReflectiveFallback(
    TSharedRef<FAngelscriptType> InType,
    UFunction* Function,
    FAngelscriptFunctionSignature& Signature,
    FFuncEntry& Entry)
{
    Entry.bReflectiveFallbackBound = false;

    if (!ShouldBindBlueprintCallableReflectiveFallback(Function))
    {
        return false; // ★ 命中 CustomThunk 时，连 reflective fallback 尝试都不会发生
    }

    auto* ReflectiveSignature = new FBlueprintCallableReflectiveSignature();
    ReflectiveSignature->UnrealFunction = Function;
    ...
}
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| authoring contract | UnrealCSharp 暴露了 `CustomThunkAttribute` 作为声明侧入口；Angelscript 当前可见自动绑定链把 `CustomThunk` 当作 native exclusion marker | 实现方式不同 |
| 自动化边界的明确性 | Angelscript 在 `UHT table` 和 `reflective fallback` 两层都显式拒绝 `CustomThunk`；UnrealCSharp 当前快照只定位到 flag 聚合，未定位到后续 sink | 实现质量差异 |
| 闭环可见性 | 基于当前 `Reference/UnrealCSharp/` 源码快照，可以确认 hook 存在，但不能确认 custom thunk 流程已在可见源码内闭环；Angelscript 的拒绝路径则是源码直证 | 实现质量差异（这里对 UnrealCSharp 的“闭环未见”判断是本轮源码扫描推断） |

### [维度 D6] 编辑器中的代码资产可见性：first-class code items vs asset-backed navigation split

前面的 D6 已经覆盖了 `sln/csproj`、Analyzer、Weaver 和 source navigation 元数据。这一轮只看一个更贴编辑器操作面的事实：**代码在 Content Browser 里到底是不是一等对象**。UnrealCSharp 的答案是“是”。`FUnrealCSharpEditorModule::StartupModule()` 创建 `DynamicDataSource`，首次 tick 后主动 `ActivateDataSource("DynamicData")`；`UDynamicDataSource` 会把 `Dynamic Classes` 根挂进 Content Browser，给 `Add New` 菜单注入“新建动态类”入口，`EditItem()` 直接 `OpenSourceFile()`，`DeleteItem()` 甚至带 `FScopedTransaction` 与 undo 存档。也就是说，C# 源文件不是“从类跳过去看看”的二级体验，而是编辑器内容视图里可新增、可编辑、可删除的 first-class item。

Angelscript 也注册了自己的 Content Browser data source，但语义不一样。`OnEngineInitDone()` 激活的是 `AngelscriptData`，`UAngelscriptContentBrowserDataSource::CreateAssetItem()` 枚举的是 `FAngelscriptEngine::Get().AssetsPackage` 里的对象，并把类型标成 `Script Asset`；`GetItemPhysicalPath()`、`CanEditItem()`、`EditItem()`、`BulkEditItems()` 全都直接返回 `false`。真正的脚本源码导航是另一条独立链：`RegisterAngelscriptSourceNavigation()` 注册 `ISourceCodeNavigationHandler`，再由 `NavigateToClass/Function/Property` 把 `UASClass/UASFunction` 映射回 `.as` 文件并用 VS Code `--goto` 打开。换句话说，Angelscript 的 editor experience 是 **资产浏览面** 与 **源码跳转面** 分离，而不是像 UnrealCSharp 一样把代码文件直接变成 Content Browser 一等项。

```
[D6-Deep] Editor Authoring Surface

[UnrealCSharp]
StartupModule
├─ create DynamicDataSource
├─ ActivateDataSource("DynamicData")
├─ ContentBrowser root: Dynamic Classes
│  ├─ Add New Dynamic Class
│  ├─ Edit -> OpenSourceFile(.cs)
│  └─ Delete -> transaction + undo
└─ refresh on OnDynamicClassUpdated / OnEndGenerator

[Angelscript]
OnEngineInitDone
├─ create AngelscriptDataSource
├─ ActivateDataSource("AngelscriptData")
├─ ContentBrowser item: Script Asset
│  ├─ thumbnail / asset data only
│  └─ no Edit / no PhysicalPath
└─ separate source navigation handler
   └─ UASClass/UASFunction -> open `.as` in VS Code
```

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:102-123,176-180`、`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:59-88,523-590,690-752,788-833`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp
// 函数: FUnrealCSharpEditorModule::StartupModule / Tick
// 位置: DynamicDataSource 会在编辑器启动时被激活
// ============================================================================
UpdatePackagingSettings();

DynamicDataSource.Reset(NewObject<UDynamicDataSource>(GetTransientPackage(), "DynamicData"));
DynamicDataSource->Initialize();

TickHandle = FTSTicker::GetCoreTicker().AddTicker(...);

...

if (!bHasTicked)
{
    bHasTicked = true;
    IContentBrowserDataModule::Get().GetSubsystem()->ActivateDataSource("DynamicData");
    // ★ 不是被动等待用户打开某个面板，而是首次 tick 后就把动态类数据源接入 Content Browser
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp
// 位置: 动态类直接作为 Content Browser 一等项
// ============================================================================
void UDynamicDataSource::Initialize(const bool InAutoRegister)
{
    ...
    if (const auto Menu = UToolMenus::Get()->ExtendMenu("ContentBrowser.AddNewContextMenu"))
    {
        Menu->AddDynamicSection(..., [WeakThis](UToolMenu* InMenu)
        {
            if (WeakThis.IsValid())
            {
                WeakThis->PopulateAddNewContextMenu(InMenu);
            }
        });
    }

    BuildRootPathVirtualTree();
    // ★ 这里把“Add New Dynamic Class”塞进 Content Browser，而不是独立菜单
}

bool UDynamicDataSource::EditItem(const FContentBrowserItemData& InItem)
{
    const auto ItemDataPayload = GetFileItemDataPayload(InItem);
    return ItemDataPayload
        ? FSourceCodeNavigation::OpenSourceFile(ItemDataPayload->GetInternalPath().ToString())
        : false;
}

bool UDynamicDataSource::DeleteItem(const FContentBrowserItemData& InItem)
{
    ...
    FScopedTransaction Transaction(LOCTEXT(LOCTEXT_NAMESPACE, "Delete File"));
    ...
    DeleteFileChange->Apply(nullptr);
    GUndo->StoreUndo(this, MoveTemp(DeleteFileChange));
    return true;
    // ★ 删除的是实际源码文件，而且支持 undo
}

void UDynamicDataSource::OnNewClassRequested(const FName& InSelectedPath)
{
    ...
    FDynamicNewClassUtils::OpenAddDynamicClassToProjectDialog(SelectedFileSystemPath);
}

FContentBrowserItemData UDynamicDataSource::CreateFolderItem(const FName& InFolderPath)
{
    ...
    DisplayNameOverride = LOCTEXT("DynamicFolderDisplayName", "Dynamic Classes");
}

FContentBrowserItemData UDynamicDataSource::CreateFileItem(UClass* InClass)
{
    return FContentBrowserItemData(this,
        EContentBrowserItemFlags::Type_File | EContentBrowserItemFlags::Category_Class
        | EContentBrowserItemFlags::Category_Plugin,
        *GetVirtualPath(InClass),
        InClass->GetFName(),
        ...);
    // ★ item 类型就是 class/plugin，不是 asset 占位符
}
```

关键源码 [2] `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:111-119,354-354`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:16-29,65-120,138-195,225-243`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp:15-44,96-139`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 函数: OnEngineInitDone
// 位置: Angelscript 也激活 Content Browser data source，但承载对象不同
// ============================================================================
void OnEngineInitDone()
{
    auto* DataSource = NewObject<UAngelscriptContentBrowserDataSource>(
        GetTransientPackage(), "AngelscriptData", RF_MarkAsRootSet | RF_Transient);
    DataSource->Initialize();

    UContentBrowserDataSubsystem* ContentBrowserData = IContentBrowserDataModule::Get().GetSubsystem();
    ContentBrowserData->ActivateDataSource("AngelscriptData");
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp
// 位置: Content Browser 展示的是 script literal assets，不是源码文件
// ============================================================================
FContentBrowserItemData UAngelscriptContentBrowserDataSource::CreateAssetItem(UObject* Asset)
{
    ...
    return FContentBrowserItemData(
        this,
        EContentBrowserItemFlags::Type_File | EContentBrowserItemFlags::Category_Asset,
        *(TEXT("/All/Angelscript/") + Asset->GetName()),
        Asset->GetFName(),
        FText::FromString(DisplayName),
        Payload,
        *Payload->Path);
    // ★ 条目类别是 asset
}

if (InAttributeKey == ContentBrowserItemAttributes::ItemTypeName || InAttributeKey == NAME_Class ||
    InAttributeKey == NAME_Type || InAttributeKey == ContentBrowserItemAttributes::ItemTypeDisplayName)
{
    OutAttributeValue.SetValue(INVTEXT("Script Asset"));
    return true;
}

bool UAngelscriptContentBrowserDataSource::GetItemPhysicalPath(const FContentBrowserItemData& InItem, FString& OutDiskPath)
{
    return false;
}

bool UAngelscriptContentBrowserDataSource::CanEditItem(const FContentBrowserItemData& InItem, FText* OutErrorMsg)
{
    return false;
}

bool UAngelscriptContentBrowserDataSource::EditItem(const FContentBrowserItemData& InItem)
{
    return false;
}
// ★ 这个数据源负责“看见脚本资产”，不负责直接编辑源码
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp
// 位置: 源码跳转是另一条独立 handler
// ============================================================================
virtual bool NavigateToClass(const UClass* InClass) override
{
    ...
    OpenModule(Module, ClassDesc->LineNumber);
    return true;
}

virtual bool NavigateToFunction(const UFunction* InFunction) override
{
    auto* ASFunc = Cast<const UASFunction>(InFunction);
    ...
    OpenFile(Path, ASFunc->GetSourceLineNumber());
    return true;
}

void OpenFile(const FString& Path, int LineNo = -1)
{
    if (LineNo != -1)
        FPlatformMisc::OsExecute(nullptr, TEXT("code"), *FString::Printf(TEXT("--goto \"%s:%d\""), *Path, LineNo));
}

void RegisterAngelscriptSourceNavigation()
{
    FSourceCodeNavigation::AddNavigationHandler(new FAngelscriptSourceCodeNavigation);
}
// ★ Angelscript 有源码导航，但它不挂在 Content Browser item 的 EditItem 上
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| Content Browser 条目语义 | UnrealCSharp 暴露的是 `Dynamic Classes` 代码项；Angelscript 暴露的是 `Script Asset` 资产项 | 实现方式不同 |
| 在位编辑能力 | UnrealCSharp 的 data source 支持 `EditItem` 与 `DeleteItem + undo`；Angelscript 当前 data source 的 `GetItemPhysicalPath/CanEditItem/EditItem/BulkEditItems` 都返回 `false` | 没有实现（就 Content Browser 内代码项可编辑能力而言） |
| 源码导航归属 | UnrealCSharp 直接从 item 打开源码文件；Angelscript 通过独立 `ISourceCodeNavigationHandler` 完成类/函数跳转 | 实现方式不同 |
| authoring surface 完整性 | UnrealCSharp 把“新建/浏览/编辑/删除代码文件”收在同一 editor surface；Angelscript 当前是“资产浏览面 + 源码跳转面”分离 | 实现质量差异（这里评价的是单一 authoring surface 的完整性，不是否认 Angelscript 已有 source navigation） |

---
## 深化分析 (2026-04-09 00:52:32)

本轮不再重复前文已经写透的 `PublishDirectory`、`CrossVersion`、`Content Browser` 和 `precompiled data` 结论，只补三个之前还没有收束成明确源码合同的问题：常规停用后的重入语义、绑定状态到底归属 `environment` 还是 `engine`、以及交付闭包究竟由显式配置还是插件发现驱动。

### [维度 D1] 停用后的重入合同：symmetric active/inactive vs test-only reset

前面的 D1 已经写过启动时序。这一轮只看**停用以后能不能按同一合同再进入**。UnrealCSharp 的 `Core` 模块把 `FEngineListener` 作为成员长期持有，`SetActive(true/false)` 不是一次性的启动按钮，而是一条正式的状态切换链。`SetActive(false)` 最终会经过 `FUnrealCSharpCoreModule::SetActive(false)` 广播 `OnUnrealCSharpCoreModuleInActive`，再由 `FCSharpEnvironment::OnUnrealCSharpModuleInActive()` 进入 `Deinitialize()`，把 `Domain` 和整套 registry 全部销毁。源码可以直接证明，这是一条对称的 enter/leave contract。

Angelscript 当前快照的常规 shutdown 路径不同。`FAngelscriptRuntimeModule::ShutdownModule()` 会移除 fallback ticker，并释放 `OwnedPrimaryEngine`；但 `bInitializeAngelscriptCalled` 并不会在这里恢复为 `false`。真正清零这个 guard 的路径是 `ResetInitializeStateForTesting()`，而且被包在 `#if WITH_DEV_AUTOMATION_TESTS` 内。这里不能外推成“生产环境一定不能再次初始化”，但可以源码直证：**常规 shutdown 合同里没有把重新进入所需的 init guard reset 作为正式步骤，测试辅助路径里才有**。

```
[D1-Deep] Re-entry Contract After Normal Shutdown

[UnrealCSharp]
EngineListener
├─ SetActive(true)                               // 进入 active
│  └─ CoreModule::SetActive(true)
│     └─ Runtime active delegates
│        └─ FCSharpEnvironment::Initialize
└─ SetActive(false)                              // 离开 active
   └─ CoreModule::SetActive(false)
      └─ Runtime inactive delegates
         └─ FCSharpEnvironment::Deinitialize
            └─ delete Domain + registries

[Angelscript]
StartupModule
└─ InitializeAngelscript()
   └─ if bInitializeAngelscriptCalled -> return

ShutdownModule
├─ remove fallback ticker
└─ reset OwnedPrimaryEngine
   └─ normal path does not clear init guard

Test-only path
└─ ResetInitializeStateForTesting()
   └─ bInitializeAngelscriptCalled = false
```

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/UnrealCSharpCore.h:6-26`、`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Listener/FEngineListener.cpp:62-79`、`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/UnrealCSharpCore.cpp:19-34`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/UnrealCSharpCore.h
// 位置: Core 模块把 FEngineListener 作为长期成员保存
// ============================================================================
class UNREALCSHARPCORE_API FUnrealCSharpCoreModule final : public IModuleInterface
{
    ...
private:
    FEngineListener EngineListener; // ★ active/inactive 监听器不是临时对象，而是 Core 模块正式成员
    bool bIsActive = false;
};
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Listener/FEngineListener.cpp
// 函数: FEngineListener::SetActive
// 位置: 停用路径会显式回落到 CoreModule::SetActive(false)
// ============================================================================
void FEngineListener::SetActive(const bool InbIsActive)
{
    if (InbIsActive)
    {
        if (const auto UnrealCSharpSetting = FUnrealCSharpFunctionLibrary::GetMutableDefaultSafe<
            UUnrealCSharpSetting>())
        {
            if (UnrealCSharpSetting->IsEnableImmediatelyActive())
            {
                FUnrealCSharpCoreModule::Get().SetActive(true);
            }
        }
    }
    else
    {
        FUnrealCSharpCoreModule::Get().SetActive(false); // ★ 停用是正式路径，不是异常补丁
    }
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/UnrealCSharpCore.cpp
// 函数: FUnrealCSharpCoreModule::SetActive
// 位置: 只有状态变化时才广播 active / inactive
// ============================================================================
void FUnrealCSharpCoreModule::SetActive(const bool InbIsActive)
{
    if (bIsActive != InbIsActive)
    {
        bIsActive = InbIsActive;

        if (InbIsActive)
        {
            FUnrealCSharpCoreModuleDelegates::OnUnrealCSharpCoreModuleActive.Broadcast();
        }
        else
        {
            FUnrealCSharpCoreModuleDelegates::OnUnrealCSharpCoreModuleInActive.Broadcast();
        }
    }
}
```

关键源码 [2] `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp:136-237, 295-302`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp
// 函数: FCSharpEnvironment::Deinitialize / OnUnrealCSharpModuleInActive
// 位置: inactive 路径会对称拆掉 Domain 与全部桥接 registry
// ============================================================================
void FCSharpEnvironment::Deinitialize()
{
    ...
    if (BindingRegistry != nullptr)
    {
        delete BindingRegistry;
        BindingRegistry = nullptr;
    }
    ...
    if (CSharpBind != nullptr)
    {
        delete CSharpBind;
        CSharpBind = nullptr;
    }
    ...
    if (Domain != nullptr)
    {
        delete Domain;
        Domain = nullptr; // ★ CLR domain 不是常驻单例，inactive 时会被销毁
    }
}

void FCSharpEnvironment::OnUnrealCSharpModuleInActive()
{
    Deinitialize(); // ★ Runtime inactive 明确收口到环境析构
}
```

关键源码 [3] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp:13-39, 138-183`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp
// 函数: StartupModule / ShutdownModule / ResetInitializeStateForTesting
// 位置: 正常 shutdown 不清 bInitializeAngelscriptCalled；测试辅助路径才会清
// ============================================================================
void FAngelscriptRuntimeModule::StartupModule()
{
    if (GIsEditor || IsRunningCommandlet())
    {
        InitializeAngelscript();
    }
}

void FAngelscriptRuntimeModule::ShutdownModule()
{
    if (FallbackTickHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(FallbackTickHandle);
        FallbackTickHandle.Reset();
    }

    if (OwnedPrimaryEngine.IsValid())
    {
        FAngelscriptEngineContextStack::Pop(OwnedPrimaryEngine.Get());
        OwnedPrimaryEngine.Reset();
    }
    // ★ 这里没有把 bInitializeAngelscriptCalled 设回 false
}

void FAngelscriptRuntimeModule::InitializeAngelscript()
{
    if (bInitializeAngelscriptCalled)
        return;

    bInitializeAngelscriptCalled = true;
    ...
}

#if WITH_DEV_AUTOMATION_TESTS
void FAngelscriptRuntimeModule::ResetInitializeStateForTesting()
{
    ...
    bInitializeAngelscriptCalled = false; // ★ reset 在 test-only helper 中，不在标准 shutdown 路径中
}
#endif
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 常规停用是否对称释放运行时主体 | UnrealCSharp 的 inactive 路径会显式 `Deinitialize()`，销毁 `Domain` 与全部 registry | 实现方式不同 |
| 常规 shutdown 后的 guard reset | Angelscript 的标准 `ShutdownModule()` 没有清 `bInitializeAngelscriptCalled`；当前可见 reset 入口只在 `WITH_DEV_AUTOMATION_TESTS` 下 | 没有实现（就标准 shutdown path 内重置初始化 guard 而言） |
| 重入合同的归属 | UnrealCSharp 把 active/inactive 当成 Runtime 正式状态机；Angelscript 当前可见源码把“重置初始化状态”放在测试辅助层 | 实现质量差异 |

### [维度 D2] 绑定状态归属：environment-owned registry rebuild vs engine-owned bind database

前文 D2 主要看的是 override patch 和 UHT 生成入口。本轮只问一个更底层的问题：**这些绑定状态到底挂在哪一层对象上**。UnrealCSharp 的答案很明确。`FCSharpEnvironment::Environment` 是静态单例，`Initialize()` 直接 `new` 出 `DynamicRegistry`、`FCSharpBind`、`ClassRegistry`、`BindingRegistry` 等对象，再通过 `OnCSharpEnvironmentInitialize` 广播驱动 `FCSharpBind::OnCSharpEnvironmentInitialize()` 与 `FDynamicRegistry::OnCSharpEnvironmentInitialize()`。`FCSharpBind::Bind(UObject*)` 也先查 `FCSharpEnvironment::GetEnvironment().GetObject()` 缓存，miss 了才新建 wrapper。也就是说，桥接缓存、动态类注册和 CDO 绑定都收敛到同一个 `environment` 生命周期里。

Angelscript 的绑定状态则明确落在 `engine`。`EnsureSharedStateCreated()` 为每个 owning engine 创建 `BindState` 与 `BindDatabase`；`FAngelscriptBindDatabase::Get()` 先取当前 engine 的数据库，只有在没有 current engine 时才回退到 `LegacyBindDatabase`。初始化时会先读 `Binds.Cache`，再加载 bind modules，最后 `FAngelscriptBinds::CallBinds()` 回放静态 bind 数组。这里要区分清楚：Angelscript 不是“没有全局 fallback”，而是**主合同是 engine-owned，global fallback 只是兜底**。

```
[D2-Deep] Binding State Ownership

[UnrealCSharp]
FCSharpEnvironment (static singleton)
├─ DynamicRegistry
├─ FCSharpBind
├─ Class/Object/Struct/Binding registries
└─ OnCSharpEnvironmentInitialize
   ├─ bind all loaded class default objects
   └─ register dynamic classes into same environment

Bind(UObject)
├─ lookup environment object cache
└─ cache miss -> create wrapper + descriptor

[Angelscript]
FAngelscriptEngine::SharedState
├─ BindState
└─ BindDatabase
   ├─ load Binds.Cache
   ├─ load bind modules
   └─ CallBinds()

FAngelscriptBindDatabase::Get()
├─ current engine DB
└─ legacy fallback if no current engine
```

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp:30-81, 133-134`、`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FCSharpBind.cpp:24-47, 75-83, 515-523`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp
// 位置: Environment 是静态单例，绑定相关 registry 全挂在它下面
// ============================================================================
FCSharpEnvironment FCSharpEnvironment::Environment; // ★ 当前快照里是单例环境

void FCSharpEnvironment::Initialize()
{
    Domain = new FDomain({"", FUnrealCSharpFunctionLibrary::GetFullAssemblyPublishPath()});

    DynamicRegistry = new FDynamicRegistry();
    CSharpBind = new FCSharpBind();
    ClassRegistry = new FClassRegistry();
    ReferenceRegistry = new FReferenceRegistry();
    ObjectRegistry = new FObjectRegistry();
    StructRegistry = new FStructRegistry();
    ContainerRegistry = new FContainerRegistry();
    DelegateRegistry = new FDelegateRegistry();
    MultiRegistry = new FMultiRegistry();
    StringRegistry = new FStringRegistry();
    BindingRegistry = new FBindingRegistry();

    FUnrealCSharpModuleDelegates::OnCSharpEnvironmentInitialize.Broadcast();
    // ★ 所有绑定物化都锚在这个 environment initialize 事件上
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FCSharpBind.cpp
// 函数: Initialize / Bind / BindClassDefaultObject / OnCSharpEnvironmentInitialize
// 位置: CDO 回放与对象 wrapper cache 都依赖 FCSharpEnvironment
// ============================================================================
void FCSharpBind::Initialize()
{
    OnCSharpEnvironmentInitializeDelegateHandle = FUnrealCSharpModuleDelegates::OnCSharpEnvironmentInitialize.AddRaw(
        this, &FCSharpBind::OnCSharpEnvironmentInitialize);
}

MonoObject* FCSharpBind::Bind(UObject* InObject)
{
    if (const auto FoundObject = FCSharpEnvironment::GetEnvironment().GetObject(InObject))
    {
        return FoundObject; // ★ 先查 environment 级对象缓存
    }

    return Bind<false>(InObject);
}

bool FCSharpBind::BindClassDefaultObject(UObject* InObject)
{
    if (CanBind(InObject->GetClass()))
    {
        FClassRegistry::AddClassConstructor(InObject->GetClass());
        Bind<false>(InObject);
        return true;
    }
    return false;
}

void FCSharpBind::OnCSharpEnvironmentInitialize()
{
    for (const auto Class : TObjectRange<UClass>())
    {
        if (const auto DefaultObject = Class->GetDefaultObject(false))
        {
            BindClassDefaultObject(DefaultObject); // ★ environment 初始化时统一回放已加载 CDO
        }
    }
}
```

关键源码 [2] `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FDynamicRegistry.cpp:17-49`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FDynamicRegistry.cpp
// 函数: Initialize / OnCSharpEnvironmentInitialize / RegisterDynamic
// 位置: dynamic class 注册也收敛到同一个 environment initialize 事件
// ============================================================================
void FDynamicRegistry::Initialize()
{
    OnCSharpEnvironmentInitializeDelegateHandle = FUnrealCSharpModuleDelegates::OnCSharpEnvironmentInitialize.AddRaw(
        this, &FDynamicRegistry::OnCSharpEnvironmentInitialize);
}

void FDynamicRegistry::OnCSharpEnvironmentInitialize() const
{
    RegisterDynamic();
}

void FDynamicRegistry::RegisterDynamic() const
{
    if (FDomain::IsLoadSucceed())
    {
        FDynamicGeneratorCore::Generator(FReflectionRegistry::Get().GetUClassAttributeClass(),
            [](FClassReflection* InClass)
            {
                if (const auto DynamicClass = FDynamicClassGenerator::GetDynamicClass(InClass))
                {
                    FCSharpEnvironment::GetEnvironment().Bind<true>(DynamicClass);
                    // ★ 动态类也进入同一个 environment 的绑定图
                }
            });
    }
}
```

关键源码 [3] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp:14-24`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:989-998, 1466-1496, 1921-1921`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:195-214`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp
// 函数: FAngelscriptBindDatabase::Get
// 位置: 首选当前 engine 的 bind database，global 只做兜底
// ============================================================================
FAngelscriptBindDatabase& FAngelscriptBindDatabase::Get()
{
    if (FAngelscriptEngine* Engine = FAngelscriptEngine::TryGetCurrentEngine())
    {
        if (FAngelscriptBindDatabase* DB = Engine->GetBindDatabase())
        {
            return *DB;
        }
    }

    static FAngelscriptBindDatabase LegacyBindDatabase;
    return LegacyBindDatabase; // ★ 这是 fallback，不是主合同
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: EnsureSharedStateCreated / Initialize_AnyThread / BindScriptTypes
// 位置: 绑定状态归属于 owning engine 的 SharedState
// ============================================================================
void FAngelscriptEngine::EnsureSharedStateCreated()
{
    if (bOwnsEngine && !SharedState.IsValid())
    {
        SharedState = MakeShared<FAngelscriptOwnedSharedState>();
        SharedState->TypeDatabase = MakeUnique<FAngelscriptTypeDatabase>();
        SharedState->BindState = MakeUnique<FAngelscriptBindState>();
        SharedState->ToStringList = MakeUnique<TArray<FToStringType>>();
        SharedState->BindDatabase = MakeUnique<FAngelscriptBindDatabase>(); // ★ per-engine
    }
}

...
FAngelscriptBindDatabase::Get().Load(GetScriptRootDirectory() / TEXT("Binds.Cache"), bGeneratePrecompiledData);
...
EnsureSharedStateCreated();
BindScriptTypes(); // ★ 读 cache / 加载 bind module / 回放静态 bind 都发生在 engine initialize 内

...
FAngelscriptBinds::CallBinds(CollectDisabledBindNames());
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp
// 函数: FAngelscriptBinds::CallBinds
// 位置: 逐个回放静态 bind 数组
// ============================================================================
void FAngelscriptBinds::CallBinds(const TSet<FName>& DisabledBindNames)
{
    for (const FBindFunction& Bind : GetSortedBindArray())
    {
        if (DisabledBindNames.Contains(Bind.BindName))
        {
            continue;
        }

        Bind.Function(); // ★ 真正把 bind lambda 打进当前 engine 对应的运行时状态
    }
}
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 绑定状态的主归属 | UnrealCSharp 的主合同是 `FCSharpEnvironment` 单例；Angelscript 的主合同是 `FAngelscriptEngine::SharedState` | 实现方式不同 |
| 绑定物化触发点 | UnrealCSharp 通过 `OnCSharpEnvironmentInitialize` 统一回放 CDO 与 dynamic class；Angelscript 在 `Engine::Initialize()` 内先读 bind cache/module，再 `CallBinds()` | 实现方式不同 |
| 无 current engine 时的行为 | Angelscript 明确有 `LegacyBindDatabase` fallback；当前可见 UnrealCSharp 绑定状态没有同等级 `per-engine + fallback` 双层结构 | 实现质量差异 |
| 多 engine 隔离粒度 | Angelscript 源码直证 `BindDatabase`/`BindState` 是 per-engine；当前 UnrealCSharp 快照里绑定状态仍是 process-wide singleton environment | 实现质量差异（这里对 UnrealCSharp“未见 per-engine bind store”的判断基于当前源码快照） |

### [维度 D11] 交付闭包来源：one settings allowlist drives authoring/build/load vs plugin-root discovery

前文 D11 已经写过 `UE/Game/CustomProjects.dll`、`PublishDirectory`、`NetDirectory` 和 `Script root`。这一轮只问“**谁定义了这套闭包本身**”。UnrealCSharp 的答案非常集中：`UUnrealCSharpSetting::CustomProjects` 同时被 `DynamicNewClassUtils`、`FSolutionGenerator`、`FUnrealCSharpFunctionLibrary` 消费。也就是说，同一份配置既决定“新建类时有哪些 project 可选”，也决定 “solution 里有哪些 `csproj` 节点”，还决定运行时 eager load 的 DLL 名单。如果某个 managed project 不在这份 settings allowlist 里，它就不会自然进入 authoring/build/load 三条链。

Angelscript 的闭包来源则更偏发现式。`FAngelscriptEngineDependencies::CreateDefault()` 把 `IPluginManager::Get().GetEnabledPluginsWithContent()` 包成 `GetEnabledPluginScriptRoots` 回调；`DiscoverScriptRoots()` 从项目 `/Script` 根出发，再把启用插件的 `/Script` 根并进来；`FindAllScriptFilenames()` 最终对 `AllRootPaths` 下的 `*.as` 做全量扫描。这里不是“谁更高级”，而是**配置式 allowlist** 和 **启用插件驱动的 discovery** 两种完全不同的闭包定义方式。

```
[D11-Deep] Closure Source Of Deliverables

[UnrealCSharp]
UUnrealCSharpSetting.CustomProjects
├─ DynamicNewClassUtils::GetProjectContent        // 新建类可选 project
├─ FSolutionGenerator::ReplaceProjectPlaceholder  // 生成 sln/csproj 节点
└─ FUnrealCSharpFunctionLibrary
   ├─ GetCustomProjectsDirectory
   └─ GetFullAssemblyPublishPath
      └─ eager load UE.dll + Game.dll + CustomProjects.dll

[Angelscript]
IPluginManager::GetEnabledPluginsWithContent
└─ DiscoverScriptRoots
   ├─ Project/Script
   └─ EnabledPlugin/Script[*]
      └─ FindAllScriptFilenames("*.as")
```

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/NewClass/DynamicNewClassUtils.cpp:50-65`、`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FSolutionGenerator.cpp:303-355`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/NewClass/DynamicNewClassUtils.cpp
// 函数: FDynamicNewClassUtils::GetProjectContent
// 位置: 新建动态类对外暴露的 project 集合来自 Game + CustomProjects
// ============================================================================
TArray<FProjectContent> FDynamicNewClassUtils::GetProjectContent()
{
    TArray CustomProjectsName = {FUnrealCSharpFunctionLibrary::GetGameName()};
    CustomProjectsName.Append(FUnrealCSharpFunctionLibrary::GetCustomProjectsName());

    TArray<FProjectContent> ProjectContents;
    for (const auto& ProjectName : CustomProjectsName)
    {
        ProjectContents.Add(FProjectContent(ProjectName,
                                            FUnrealCSharpFunctionLibrary::GetFullScriptDirectory() /
                                            ProjectName));
    }

    return ProjectContents; // ★ authoring 入口已经被 allowlist 收敛
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FSolutionGenerator.cpp
// 函数: ReplaceProjectPlaceholder / ReplaceSolutionConfigurationPlatformsPlaceholder
// 位置: 同一份 CustomProjects 配置继续决定 sln 中有哪些工程节点
// ============================================================================
if (const auto UnrealCSharpSetting = FUnrealCSharpFunctionLibrary::GetMutableDefaultSafe<UUnrealCSharpSetting>())
{
    for (const auto& CustomProject : UnrealCSharpSetting->GetCustomProjects())
    {
        Projects += FString::Printf(TEXT(
            "Project(\"{%s}\") = \"%s\", \"%s\\%s%s\", \"{%s}\"\n"
            "EndProject\n"
        ),
            *CSHARP_GUID,
            *CustomProject.Name,
            *CustomProject.Name,
            *CustomProject.Name,
            *PROJECT_SUFFIX,
            *CustomProject.GUID());

        SolutionConfigurationPlatforms += FString::Printf(TEXT(
            "\t\t{%s}.Debug|Any CPU.ActiveCfg = Debug|Any CPU\n"
            "\t\t{%s}.Debug|Any CPU.Build.0 = Debug|Any CPU\n"
            "\t\t{%s}.Release|Any CPU.ActiveCfg = Release|Any CPU\n"
            "\t\t{%s}.Release|Any CPU.Build.0 = Release|Any CPU\n"
        ),
            *CustomProject.GUID(),
            *CustomProject.GUID(),
            *CustomProject.GUID(),
            *CustomProject.GUID());
        // ★ build graph 也复用同一份 allowlist
    }
}
```

关键源码 [2] `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp:776-805, 1020-1041`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp
// 函数: GetCustomProjectsName / GetCustomProjectsDirectory / GetFullCustomProjectsPublishPath / GetFullAssemblyPublishPath
// 位置: runtime load 闭包仍由同一份 CustomProjects 配置决定
// ============================================================================
TArray<FString> FUnrealCSharpFunctionLibrary::GetCustomProjectsName()
{
    TArray<FString> CustomProjectsName;
    if (const auto UnrealCSharpSetting = GetMutableDefaultSafe<UUnrealCSharpSetting>())
    {
        for (const auto& [Name] : UnrealCSharpSetting->GetCustomProjects())
        {
            CustomProjectsName.Add(Name);
        }
    }
    return CustomProjectsName;
}

TArray<FString> FUnrealCSharpFunctionLibrary::GetCustomProjectsDirectory()
{
    TArray<FString> CustomProjectsDirectory;
    if (const auto UnrealCSharpSetting = GetMutableDefaultSafe<UUnrealCSharpSetting>())
    {
        for (const auto& [Name] : UnrealCSharpSetting->GetCustomProjects())
        {
            CustomProjectsDirectory.Add(GetFullScriptDirectory() / Name);
        }
    }
    return CustomProjectsDirectory;
}

TArray<FString> FUnrealCSharpFunctionLibrary::GetFullCustomProjectsPublishPath()
{
    ...
    FullCustomProjectsPublishPath.Add(GetFullPublishDirectory() / Name + DLL_SUFFIX);
}

TArray<FString> FUnrealCSharpFunctionLibrary::GetFullAssemblyPublishPath()
{
    return TArrayBuilder<FString>().
           Add(GetFullUEPublishPath()).
           Add(GetFullGamePublishPath()).
           Append(GetFullCustomProjectsPublishPath()).
           Build(); // ★ 运行时 eager load 集合同样来自 allowlist
}
```

关键源码 [3] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:558-565, 1328-1358, 1999-2014`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 脚本闭包来自启用插件发现，而不是显式 project allowlist
// ============================================================================
Dependencies.GetEnabledPluginScriptRoots = []()
{
    TArray<FString> ScriptRoots;
    for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetEnabledPluginsWithContent())
    {
        ScriptRoots.Add(Plugin->GetBaseDir() / TEXT("Script"));
    }
    return ScriptRoots;
};

...
FString RootPath = Dependencies.ConvertRelativePathToFull(Dependencies.GetProjectDir() / TEXT("Script"));
...
for (const FString& PluginScriptRoot : Dependencies.GetEnabledPluginScriptRoots())
{
    const FString ScriptPath = Dependencies.ConvertRelativePathToFull(PluginScriptRoot);
    if (Dependencies.DirectoryExists(ScriptPath) && ScriptPath != RootPath)
    {
        DiscoveredRootPaths.Add(ScriptPath); // ★ enable plugin -> script root 自动进入闭包
    }
}

...
for (auto& Path : AllRootPaths)
{
    FindScriptFiles(
        IFileManager::Get(),
        TEXT(""),
        Path,
        TEXT("*.as"),
        OutFilenames,
        bSkipDevelopmentScripts,
        bSkipEditorScripts);
    // ★ 运行时直接按发现到的 roots 扫描脚本文件
}
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 闭包定义来源 | UnrealCSharp 由 `CustomProjects` settings allowlist 明确声明；Angelscript 由启用插件的 `Script` 根发现驱动 | 实现方式不同 |
| authoring/build/load 是否共用同一份名单 | UnrealCSharp 同一份 `CustomProjects` 同时驱动 `NewClass` UI、`sln/csproj` 生成和 eager load DLL 列表 | 实现质量差异 |
| 零配置扩展发现 | Angelscript 只要插件启用且带 `Script` 目录就会被扫描；当前可见 UnrealCSharp 路径没有对等的“自动发现额外 managed project”入口 | 没有实现（就零配置多 project discovery 而言） |
| 配置稳定性与变动成本 | UnrealCSharp 的闭包更稳定、更显式；Angelscript 的闭包会随 enabled plugin 集变化，但接入成本更低 | 实现质量差异（这里评价的是 contract 明确性 vs 接入摩擦，不是简单优劣） |

---

## 深化分析 (2026-04-09 01:05:28)

### [维度 D2] 桥接 ABI 断面：generated `InternalCall` export plane + patched `UFunction` import plane vs runtime-owned bind/event surface

前面的 D2 已经分别写过 override patch、descriptor 和接口桥接，但还有一个结构层面的差异没有单独拆开：**脚本 ABI 到底是一套还是两套**。UnrealCSharp 当前快照里，`managed -> native` 与 `native/Blueprint -> managed` 是两条显式分开的桥接面。前者由 `FBindingClassGenerator` 生成 `[MethodImpl(MethodImplOptions.InternalCall)]` extern stub，再由 `FMonoDomain::RegisterBinding()` 批量 `mono_add_internal_call()`；后者则由 `FCSharpBind` 改写 `UFunction::NativeFunc` 为 `UCSharpFunction::execCallCSharp`，把 UE 调用栈回流到 `FCSharpFunctionDescriptor::CallCSharp()`。

Angelscript 也不是“只有一条函数入口”，但它没有把 authoring contract 裂成 “generated managed stub 面” 和 “patched UFunction 面” 两个独立子系统。`BindBlueprintCallable()`、`BindMethodDirect()`、`BindGlobalFunction()`、`FScriptCall` 都仍然挂在同一个 Angelscript runtime bind/event 基础设施上，参数匹配、fallback 和事件缓冲共享 `FAngelscriptTypeUsage`/bind state 体系。

```
[D2-Deep] ABI Planes

[UnrealCSharp]
managed call
├─ generated `[MethodImpl(InternalCall)]` stubs    // 生成 extern 桥接声明
├─ FMonoDomain::RegisterBinding                    // 启动时批量注册 internal call
└─ native C++ bridge

UE / Blueprint callback
├─ FCSharpBind patches `UFunction::NativeFunc`     // 把原生 UFunction 改写成托管跳板
└─ UCSharpFunction::execCallCSharp                 // UE 调用栈回流 Mono

[Angelscript]
native callable path
├─ BindBlueprintCallable                           // 同一 bind 系统内做签名分析
└─ FAngelscriptBinds::BindMethodDirect/GlobalFunction

Blueprint event path
├─ FScriptCall buffer                              // 同一 runtime 内做参数暂存与校验
└─ Angelscript VM execution                        // 没有额外 managed stub 平面
```

关键源码 [1] `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FBindingClassGenerator.cpp:767-840`、`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp:783-792`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FBindingClassGenerator.cpp
// 位置: 767-840，生成 managed -> native 的 InternalCall extern stub
// ============================================================================
auto FunctionDeclaration = FString::Printf(TEXT(
    "\t\t[MethodImpl(MethodImplOptions.InternalCall)]\n"
    "\t\tpublic static extern void %s(nint InObject, byte* %s, byte* %s);\n"
),
    *BINDING_COMBINE_FUNCTION_IMPLEMENTATION(
        ClassContent,
        InClass->GetSubscript().GetGetImplementationName()),
    IN_BUFFER_TEXT,
    RETURN_BUFFER_TEXT
);

...

GetFunctionContent = FString::Printf(TEXT(
    "\t\t[MethodImpl(MethodImplOptions.InternalCall)]\n"
    "\t\tpublic static extern void %s(nint InObject, byte* %s);\n"
),
    *BINDING_COMBINE_FUNCTION_IMPLEMENTATION(
        ClassContent, (BINDING_PROPERTY_GET + PropertyName)),
    RETURN_BUFFER_TEXT
);
// ★ 生成器直接把桥接面写成 C# extern 函数，而不是运行时反射拼字符串
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp
// 函数: FMonoDomain::RegisterBinding
// 位置: 783-792，批量把 generated stub 注册为 Mono InternalCall
// ============================================================================
void FMonoDomain::RegisterBinding()
{
    for (const auto& Class : FBinding::Get().Register().GetClasses())
    {
        for (const auto& Method : Class->GetMethods())
        {
            mono_add_internal_call(TCHAR_TO_ANSI(*Method.GetMethod()), Method.GetFunction());
            // ★ generated stub 名字 -> native 函数指针，形成 managed -> native ABI 面
        }
    }
}
```

关键源码 [2] `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FCSharpBind.cpp:333-385`、`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/CSharpFunction.cpp:5-11`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FCSharpBind.cpp
// 位置: 333-385，Blueprint / UFunction -> managed 的回流面
// ============================================================================
const auto OverrideFunction = DuplicateFunction(OriginalFunction, InClass, *NewFunctionName);

auto FunctionHash = GetTypeHash(OriginalFunction);

FCSharpEnvironment::GetEnvironment().AddFunctionHash<FCSharpFunctionDescriptor>(
    FunctionHash, InClassDescriptor, OriginalFunction,
    FCSharpFunctionRegister(OriginalFunction, OverrideFunction,
                            OriginalFunction->FunctionFlags, OriginalFunction->GetNativeFunc()));

...

OriginalFunction->SetNativeFunc(UCSharpFunction::execCallCSharp);
OriginalFunction->FunctionFlags |= FUNC_Native;
// ★ 这里不是通过 InternalCall 回调，而是直接篡改 UFunction 的 NativeFunc

...

NewFunction = DuplicateFunction(OriginalFunction, InClass, FunctionName);
...
NewFunction->SetNativeFunc(UCSharpFunction::execCallCSharp);
NewFunction->FunctionFlags |= FUNC_Native;
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/CSharpFunction.cpp
// 函数: UCSharpFunction::execCallCSharp
// 位置: 5-11，UE 调用栈最终跳到托管执行器
// ============================================================================
DEFINE_FUNCTION(UCSharpFunction::execCallCSharp)
{
    if (const auto FunctionDescriptor = FCSharpEnvironment::GetEnvironment().GetOrAddFunctionDescriptor<
        FCSharpFunctionDescriptor>(GetTypeHash(Stack.CurrentNativeFunction)))
    {
        FunctionDescriptor->CallCSharp(Context, Stack, RESULT_PARAM);
        // ★ 这里才是真正的 native/Blueprint -> managed 回流点
    }
}
```

关键源码 [3] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:23-33`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp:17-150`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp:89-163`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp
// 位置: 23-33，bind/event 状态先收敛到同一个 bind state
// ============================================================================
static FAngelscriptBindState& GetBindState()
{
    if (FAngelscriptEngine* Engine = FAngelscriptEngine::TryGetCurrentEngine())
    {
        if (FAngelscriptBindState* State = Engine->GetBindState())
        {
            return *State;
        }
    }
    static FAngelscriptBindState LegacyBindState;
    return LegacyBindState; // ★ callable / event 基础设施共享同一个 runtime bind state
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp
// 位置: 17-150，native callable 仍在 Angelscript bind 基础设施内完成
// ============================================================================
void BindBlueprintCallable(
    TSharedRef<FAngelscriptType> InType,
    UFunction* Function,
    FAngelscriptMethodBind& DBBind)
{
    ...
    if (!bHasDirectNativePointer)
    {
        if (!BindBlueprintCallableReflectiveFallback(InType, Function, Signature, *Entry))
            return;
        ...
        return;
    }

    ...

    int FunctionId = FAngelscriptBinds::BindMethodDirect
    (
        InType->GetAngelscriptTypeName(),
        Signature.Declaration, ASFuncPtr, asCALL_THISCALL, Entry->Caller
    );
    Signature.ModifyScriptFunction(FunctionId);
    // ★ 无论直连还是 reflective fallback，都留在 Angelscript 自己的 bind surface
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp
// 位置: 89-163，Blueprint event 参数缓冲也仍由 Angelscript runtime 自管
// ============================================================================
struct alignas(64) FScriptCall
{
    uint8 ArgumentBuffer[AS_EVENT_MAX_SIZE];
    FArgumentInBuffer ArgumentTypes[AS_EVENT_MAX_ARGS];
    int32 ArgumentIndex = 0;
    SIZE_T ArgumentOffset = 0;

    SCRIPTCALL_INLINE bool ValidateAgainstFunction(const UFunction* Function, FString& OutErrorMessage) const
    {
        ...
        if (!DoesCallArgumentMatchProperty(ArgumentTypes[PropertyIndex].Type, *It))
        {
            OutErrorMessage = FString::Printf(TEXT(
                "Signature mismatch while executing '%s' at parameter '%s'."),
                *Function->GetName(), *It->GetName());
            return false;
        }
        ...
    }
    // ★ Blueprint event 没有绕去另一套 managed stub ABI，而是在 runtime buffer 内完成参数协议
};
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| ABI 平面数量 | UnrealCSharp 把 `managed -> native` 与 `native/Blueprint -> managed` 显式拆成 `InternalCall` 与 `UFunction.NativeFunc patch` 两套桥接面 | 实现方式不同 |
| 调用入口所有权 | UnrealCSharp 的 outward path 归 `FBindingClassGenerator + FMonoDomain`，inward path 归 `FCSharpBind + UCSharpFunction`；Angelscript 两类入口都仍留在 runtime bind/event 体系 | 实现方式不同 |
| 方向特化程度 | UnrealCSharp 每个方向都可以独立优化与替换，但桥接面更多；Angelscript 共享 `bind state + signature + event buffer`，表面更统一 | 实现质量差异 |
| 对 Angelscript 的参考点 | 当前 Angelscript 不是“少一层桥接”，而是选择把差异收敛到同一 runtime surface；如果后续要增强跨语言 authoring 体验，可以考虑只新增一层生成期 stub plane，而不动现有 event/runtime 面 | 实现质量差异 |

### [维度 D6] 文档注释交付面：generated XML doc comments vs runtime tooltip/doc index

前面的 D6 已经把 `sln/csproj`、Analyzer 和 source navigation 写透了，但**文档本身如何交付给开发者**还没拆开。UnrealCSharp 把文档作为代码生成产物的一部分：`UUnrealCSharpEditorSetting` 默认开启 `bIsGenerateFunctionComment`，`FClassGenerator` 读取 `UFunction` 的 `Comment` metadata，再交给 `FDoxygenConverter` 变成 `/// <summary>`、`/// <param>`、`/// <returns>` 写进生成出来的 C# 文件。这意味着文档与 IDE 补全、跳转、编译诊断在同一份源码资产里。

Angelscript 走的是另一条链：预处理器先把脚本注释折叠成 `ToolTip` metadata，再由 `FAngelscriptDocs` 收进 runtime 文档索引，随后 `AngelscriptDebugServer` 和 `DumpDocumentation()`/`DocumentationStats.csv` 消费。换句话说，Angelscript 当然“有文档能力”，但它的主要交付面是 debugger/dump/runtime metadata，而不是 IDE-native XML 注释文件。

```
[D6-Deep] Documentation Delivery Surface

[UnrealCSharp]
UFunction comment metadata
├─ editor setting enables comment generation        // 默认打开函数注释生成
├─ FClassGenerator reads `Comment` metadata         // 生成期读取原始注释
├─ FDoxygenConverter                                // Doxygen -> XML doc
└─ generated `/// <summary>/<param>/<returns>`      // 直接进入 C# 源码与 IDE

[Angelscript]
script / UE comment
├─ Preprocessor stores `ToolTip` metadata           // 注释先落到元数据
├─ FAngelscriptDocs maps                            // runtime 文档索引
├─ DebugServer JSON                                 // 调试器消费
└─ DumpDocumentation / DocumentationStats.csv       // 离线导出消费
```

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Setting/UnrealCSharpEditorSetting.cpp:21-34`、`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp:1095-1100`、`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp:460-468`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Setting/UnrealCSharpEditorSetting.cpp
// 位置: 21-34，函数注释生成默认开启
// ============================================================================
UUnrealCSharpEditorSetting::UUnrealCSharpEditorSetting(const FObjectInitializer& ObjectInitializer):
    Super(ObjectInitializer),
    ...
    bIsGenerateFunctionComment(true),
    bEnableExport(false),
    EditorConfiguration(ESolutionConfiguration::Debug),
    RuntimeConfiguration(ESolutionConfiguration::Release)
{
}
// ★ 文档注释不是临时调试功能，而是默认 authoring contract 的一部分
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp
// 函数: FUnrealCSharpFunctionLibrary::IsGenerateFunctionComment
// 位置: 1095-1100，生成器在 editor setting 上显式读这个开关
// ============================================================================
bool FUnrealCSharpFunctionLibrary::IsGenerateFunctionComment()
{
    if (const auto UnrealCSharpEditorSetting = GetMutableDefaultSafe<UUnrealCSharpEditorSetting>())
    {
        return UnrealCSharpEditorSetting->IsGenerateFunctionComment();
    }

    return false;
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp
// 位置: 460-468，生成期把 UFunction 的 Comment metadata 交给 Doxygen converter
// ============================================================================
FString FunctionComment;

if (FUnrealCSharpFunctionLibrary::IsGenerateFunctionComment())
{
    auto Comment = Function->GetMetaData(TEXT("Comment"));

    if (!Comment.IsEmpty())
    {
        FunctionComment = FDoxygenConverter(TEXT("\t\t"))(Comment);
        // ★ 注释直接变成待写入 C# 文件的源码片段
    }
}
```

关键源码 [2] `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FDoxygenConverter.cpp:351-472`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FDoxygenConverter.cpp
// 位置: 351-472，Doxygen 标签被转写成 XML doc comment
// ============================================================================
FString FDoxygenConverter::operator()(const FStringView& InText) const
{
    ...

    if (BriefIndex != 0)
    {
        StringBuilder.Append(Indent).Append(TEXT("/// <summary>\n"));
        ...
        StringBuilder.Append(Indent).Append(TEXT("/// </summary>\n"));
    }

    for (auto& Tag : TagData)
    {
        if (Tag == TagParam)
        {
            StringBuilder.Append(Indent).Append(TEXT("/// <param name=\""))
                .Append(Tag.ParamName).Append(TEXT("\">\n"));
            ...
            StringBuilder.Append(Indent).Append(TEXT("/// </param>\n"));
        }
        ...
    }

    if (ReturnIndex != 0)
    {
        StringBuilder.Append(Indent).Append(TEXT("/// <returns>\n"));
        ...
        StringBuilder.Append(Indent).Append(TEXT("/// </returns>\n"));
    }
}
// ★ 这条链的最终产物就是 IDE 可见的 XML 文档注释，而不是运行时 map
```

关键源码 [3] `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:771-772`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp:31-48`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:1575-1577,1751-1756`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp:944-947`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp
// 位置: 771-772，脚本注释先落成 ToolTip 元数据
// ============================================================================
if (Chunk.Comment.Len() != 0)
    ClassDesc->Meta.Add(PP_NAME_ToolTip, FormatCommentForToolTip(Chunk.Comment));
// ★ 注释先成为 metadata，而不是直接生成 IDE 注释文件
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp
// 位置: 31-48，runtime 维护文档索引
// ============================================================================
void FAngelscriptDocs::AddUnrealDocumentation(int FunctionId, FStringView Documentation, FStringView Category, UFunction* Function)
{
    FPassedDoc Doc;
    Doc.Tooltip = Documentation;
    Doc.Category = Category;
    Doc.Function = Function;

    UnrealDocumentation.Add(FunctionId, Doc);
}

void FAngelscriptDocs::AddUnrealDocumentationForProperty(int TypeId, int PropertyOffset, FStringView Documentation)
{
    UnrealPropertyDocumentation.Add(TPair<int,int>(TypeId, PropertyOffset), FString(Documentation));
}
// ★ 文档实体以 runtime map 形式存在
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 位置: 1575-1577, 1751-1756，调试协议直接消费 runtime 文档索引
// ============================================================================
const FString& Doc = FAngelscriptDocs::GetUnrealDocumentation(ScriptFunction->GetId());
if (Doc.Len() != 0)
    FuncDesc->SetStringField(TEXT("doc"), Doc);

...

const FString& Doc = FAngelscriptDocs::GetUnrealDocumentationForProperty(ClassTypeId, Offset);
if (Doc.Len() != 0 || Flags != 0)
    PropDesc.Add(MakeShared<FJsonValueString>(Doc));
// ★ 文档主要服务于 debugger/extension，而不是生成 `///` 源码
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp
// 位置: 944-947，state dump 也把文档索引当成统计对象
// ============================================================================
Writer.AddRow({ TEXT("UnrealDocumentation"), LexToString(FAngelscriptDocs::GetUnrealDocumentationCount()) });
Writer.AddRow({ TEXT("UnrealTypeDocumentation"), LexToString(FAngelscriptDocs::GetUnrealTypeDocumentationCount()) });
Writer.AddRow({ TEXT("GlobalVariableDocumentation"), LexToString(FAngelscriptDocs::GetGlobalVariableDocumentationCount()) });
Writer.AddRow({ TEXT("UnrealPropertyDocumentation"), LexToString(FAngelscriptDocs::GetUnrealPropertyDocumentationCount()) });
// ★ 文档在 Angelscript 里还是 dump/diagnostics 的一等观测对象
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 文档交付面 | UnrealCSharp 把注释生成进 C# 源码本身；Angelscript 把注释沉淀为 runtime metadata / debugger / dump 索引 | 实现方式不同 |
| IDE 原生文档体验 | UnrealCSharp 当前源码直证有 `Comment -> FDoxygenConverter -> /// XML` 链 | 实现质量差异 |
| 调试期文档可观测性 | Angelscript 文档能直接进入 DebugServer JSON 与 `DocumentationStats.csv` | 实现质量差异 |
| 对等缺口 | 当前可见 Angelscript 源码未定位到“从注释直接发射 IDE-native XML doc 源文件”的等价链路 | 没有实现（就 IDE-native 文档发射而言） |

### [维度 D11] 发布模型落点：framework-dependent IL payload on bundled Mono vs cache/JIT artifact set

前面的 D11 已经把 staging 根目录和闭包来源写过了，但**这套交付到底是不是 AOT/self-contained publish** 还没有单独落成源码结论。当前 UnrealCSharp 快照更接近“`dotnet build` 驱动的 IL DLL publish + 插件自带 Mono runtime”模型：编译线程执行的是 `dotnet build`，`Game.props` 再在 `AfterTargets="Build"` 后补跑 `Publish`，且只复制 `$(PublishDir)*.dll`；`FSolutionGenerator` 只填 `<TargetFramework>netX.Y</TargetFramework>`；运行时装载目录则是 `ProjectContentDir()/PublishDirectory` 加上插件 `Mono/lib/.../net`。结合 `FMonoDomain::Initialize()` 在非 iOS 平台显式设为 `MONO_AOT_MODE_NONE`、iOS 设为 `MONO_AOT_MODE_INTERP`，可以推断当前交付主路径不是 per-project NativeAOT，也不是 self-contained publish。

Angelscript 的交付面则是另一种闭包：`Binds.Cache`、`PrecompiledScript_<Config>.Cache`、`BuildIdentifier` 校验和 `StaticJIT` 编译进二进制的 `DataGuid` 一起组成 artifact set。它更像“脚本缓存 + 可选转译 C++ + 二进制封印”，而不是托管程序集 publish。

```
[D11-Deep] Delivery Model

[UnrealCSharp]
compile thread
├─ `dotnet build <Game.csproj>`                     // 编译线程只直接调用 build
├─ Game.props `AfterBuildPublish` -> `Publish`      // Build 后再触发 Publish
├─ copy `$(PublishDir)*.dll`                        // 项目侧只拷 DLL
└─ runtime probe roots
   ├─ `ProjectContent/PublishDirectory`
   └─ plugin `Mono/lib/.../net`                     // 基础托管库来自插件运行时

execution mode
├─ iOS -> `MONO_AOT_MODE_INTERP`
└─ others -> `MONO_AOT_MODE_NONE`

[Angelscript]
artifact set
├─ `Binds.Cache`
├─ `PrecompiledScript_<Config>.Cache`
├─ `BuildIdentifier` validation
└─ `StaticJITCompiledInfo(DataGuid)`                // 编译进二进制的匹配封印
```

关键源码 [1] `Reference/UnrealCSharp/Source/Compiler/Private/FCSharpCompilerRunnable.cpp:248-257`、`Reference/UnrealCSharp/Template/Game.props:16-27`、`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FSolutionGenerator.cpp:220-229`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/Compiler/Private/FCSharpCompilerRunnable.cpp
// 位置: 248-257，编译线程显式调用的是 dotnet build
// ============================================================================
static auto CompileTool = FUnrealCSharpFunctionLibrary::GetDotNet();

const auto CompileParam = FString::Printf(TEXT(
    "build \"%s\" --nologo -c %s"
),
    *FUnrealCSharpFunctionLibrary::GetGameProjectPath(),
    GetSolutionConfiguration() == ESolutionConfiguration::Debug
        ? TEXT("Debug")
        : TEXT("Release")
);
// ★ 编译线程没有直接传 `publish -r ...` 或 AOT/self-contained 参数
```

```xml
<!-- =========================================================================
文件: Reference/UnrealCSharp/Template/Game.props
位置: 16-27，Build 后再触发 Publish，但只拷贝 DLL
========================================================================== -->
<Target Name="AfterBuildPublish" AfterTargets="Build">
    <MSBuild Projects="$(ProjectPath)" Targets="Publish" Properties="Configuration=$(Configuration)" />
</Target>
<Target Name="CopyDllsAfterPublish" AfterTargets="Publish">
    <ItemGroup>
        <PublishFiles Include="$(PublishDir)*.dll" />
    </ItemGroup>
    <Copy SourceFiles="@(PublishFiles)" DestinationFolder="$(ScriptOutputPath)" />
</Target>
<!-- ★ 当前项目侧 publish payload 只看到 DLL，没有对等的 self-contained 运行时拷贝 -->
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FSolutionGenerator.cpp
// 位置: 220-229，模板只被填成 netX.Y 的 TargetFramework
// ============================================================================
void FSolutionGenerator::ReplaceTargetFramework(FString& OutResult)
{
    OutResult = OutResult.Replace(TEXT("<TargetFramework></TargetFramework>"),
                                  *FString::Printf(TEXT(
                                      "<TargetFramework>net%d.%d</TargetFramework>"
                                  ),
                                      FUnrealCSharpFunctionLibrary::GetDotnetVersion(),
                                      DOTNET_MINOR_VERSION
                                  )
    );
}
// ★ 当前源码直证的是 TargetFramework 注入，而不是 per-project AOT publish 配置注入
```

关键源码 [2] `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoFunctionLibrary.cpp:59-64`、`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp:1044-1049`、`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp:46-90`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoFunctionLibrary.cpp
// 位置: 59-64，插件自己提供 Mono `net` 目录
// ============================================================================
FString FMonoFunctionLibrary::GetNetDirectory()
{
    return FString::Printf(TEXT(
        "%s/net"),
        *GetLibDirectory()
    );
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp
// 位置: 1044-1049，runtime 查找程序集的根目录 = PublishDirectory + Mono net directory
// ============================================================================
TArray<FString> FUnrealCSharpFunctionLibrary::GetAssemblyPath()
{
    return TArrayBuilder<FString>().
           Add(FPaths::ProjectContentDir() / GetPublishDirectory()).
           Add(FMonoFunctionLibrary::GetNetDirectory()).
           Build();
}
// ★ 项目 DLL 与 Mono 基础库来自两个根，进一步说明 publish payload 不是自包含单包
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp
// 位置: 46-90，运行模式上也没有走 project-side native AOT
// ============================================================================
void FMonoDomain::Initialize(const FMonoDomainInitializeParams& InParams)
{
    ...
#if PLATFORM_IOS
    mono_jit_set_aot_mode(MONO_AOT_MODE_INTERP);
    mono_aot_register_module(static_cast<void**>(mono_aot_module_System_Private_CoreLib_info));
    ...
#else
    ...
    mono_jit_set_aot_mode(MONO_AOT_MODE_NONE);
#endif
    ...
}
// ★ 当前可见路径是 iOS 解释模式 / 其他平台非 AOT 模式，而不是项目程序集 NativeAOT
```

关键源码 [3] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1466-1555`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp:2627-2645`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp:3683-3695`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 1466-1555，交付物由 bind DB + precompiled cache + JIT 匹配组成
// ============================================================================
FAngelscriptBindDatabase::Get().Load(GetScriptRootDirectory() / TEXT("Binds.Cache"), bGeneratePrecompiledData);

...

if (bUsePrecompiledData)
{
    FString Filename;
#if UE_BUILD_SHIPPING
    Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript_Shipping.Cache");
#elif UE_BUILD_TEST
    Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript_Test.Cache");
#elif UE_BUILD_DEVELOPMENT
    Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript_Development.Cache");
#endif

    if (!IFileManager::Get().FileExists(*Filename))
        Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript.Cache");

    ...

    if (CompiledInfo != nullptr && CompiledInfo->PrecompiledDataGuid != PrecompiledData->DataGuid)
    {
        UE_LOG(Angelscript, Warning, TEXT(
            "Loaded angelscript precompiled data does not match the transpiled C++ in the game binary. Transpiled code will not be used!"));
        FJITDatabase::Get().Clear();
    }
}
// ★ 这里的交付合同是缓存/JIT artifact set，而不是托管程序集 publish
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp
// 位置: 2627-2645，precompiled cache 先做 build configuration 校验
// ============================================================================
int32 FAngelscriptPrecompiledData::GetCurrentBuildIdentifier()
{
#if UE_BUILD_DEBUG
    return 1;
#elif UE_BUILD_DEVELOPMENT
    return 2;
#elif UE_BUILD_TEST
    return 3;
#elif UE_BUILD_SHIPPING
    return 4;
#else
    return -1;
#endif
}

bool FAngelscriptPrecompiledData::IsValidForCurrentBuild()
{
    return BuildIdentifier == GetCurrentBuildIdentifier() && BuildIdentifier != -1;
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp
// 位置: 3683-3695，StaticJIT 把 DataGuid 编译进最终 C++ 产物
// ============================================================================
FString InfoContent = FString::Printf(
    TEXT("#include \"StaticJIT/StaticJITHeader.h\"\n")
    TEXT("\n")
    TEXT("AS_FORCE_LINK static const FStaticJITCompiledInfo JitInfo(FGuid(%d, %d, %d, %d));\n"),
    PrecompiledData->DataGuid.A,
    PrecompiledData->DataGuid.B,
    PrecompiledData->DataGuid.C,
    PrecompiledData->DataGuid.D
);
// ★ 二进制里保留一份与 precompiled cache 对齐的封印信息
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| UnrealCSharp 的 publish 形态 | 当前源码直证的是 `dotnet build -> Publish -> copy *.dll`，再配合插件自带 `Mono/lib/.../net` 装载 | 实现方式不同 |
| UnrealCSharp 的 AOT 路径 | 当前可见代码只有 iOS `MONO_AOT_MODE_INTERP` 与非 iOS `MONO_AOT_MODE_NONE`；未见 project-side NativeAOT/self-contained publish 闭环 | 没有实现（就当前源码快照里的项目交付路径而言） |
| Angelscript 的交付封印 | `BuildIdentifier` 和 `DataGuid` 同时约束 precompiled cache 与 compiled JIT 代码匹配关系 | 实现质量差异 |
| 交付目标差异 | UnrealCSharp 交付的是 managed IL payload + bundled CLR；Angelscript 交付的是脚本缓存/转译产物集合，二者优化方向不同 | 实现方式不同 |

---

## 深化分析 (2026-04-09 01:17:54)

### [维度 D1] 平台 ABI 适配落点：runtime native-lib remap vs compile-time callconv specialization

前面的 D1 主要停留在模块边界。这一轮继续往下钻第三方运行时真正“跨平台”的那一层，会发现 UnrealCSharp 和 Angelscript 把平台差异放在了完全不同的阶段。UnrealCSharp 把这件事压在 `FMonoDomain::Initialize()`：iOS 路径显式 `mono_dllmap_insert(..., "__Internal", ...)`，把 `System.Native`、`System.Net.Security.Native`、`System.IO.Compression.Native` 等托管依赖重映射到宿主进程；`DOTNET9 + Windows` 路径则把 `Mono/lib` 目录作为 `NATIVE_DLL_SEARCH_DIRECTORIES` 交给 `monovm_initialize()`。也就是说，Mono 本体可以是同一套插件资产，但 native 依赖查找策略是在运行时动态修补的。

Angelscript 则把 ABI 分流尽量前移到编译期。`as_config.h` 直接按 `_M_X64`、`__arm64__`、`_M_IX86` 等编译宏选择 `AS_X64_MSVC`、`AS_ARM64`、`AS_X86` 和一整套 `THISCALL_*` / `CDECL_*` 约束；如果没有命中受支持平台，就退回 `AS_MAX_PORTABILITY` 路径。换句话说，Angelscript 的平台适配不是“启动时修 DLL 搜索路径”，而是“编译期把正确调用约定写进最终二进制”。

```
[D1-Deep] Platform ABI Adaptation

[UnrealCSharp]
runtime bootstrap
├─ FMonoDomain::Initialize()
│  ├─ iOS -> mono_dllmap_insert("__Internal")     // 托管依赖映射到宿主进程
│  ├─ Windows/.NET9 -> monovm_initialize(...)     // 注入 native DLL 搜索根
│  └─ choose Mono AOT mode at runtime             // 启动时决定执行模式
└─ CLR boots after native-lib remap

[Angelscript]
compile-time embedding
├─ as_config.h selects AS_X64_MSVC / AS_ARM64 / AS_X86
├─ THISCALL/CDECL rules baked into VM build        // 调用约定在编译期固化
└─ fallback -> AS_MAX_PORTABILITY                 // 未命中 ABI 时走可移植慢路径
```

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp:54-90`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp
// 函数: FMonoDomain::Initialize
// 位置: Mono 启动时按平台修补 native 依赖解析
// ============================================================================
#if PLATFORM_IOS
    mono_jit_set_aot_mode(MONO_AOT_MODE_INTERP);

    mono_aot_register_module(static_cast<void**>(mono_aot_module_System_Private_CoreLib_info));

#if !DOTNET9
    mono_dllmap_insert(NULL, "System.Native", NULL, "__Internal", NULL);
    mono_dllmap_insert(NULL, "System.Net.Security.Native", NULL, "__Internal", NULL);
    mono_dllmap_insert(NULL, "System.IO.Compression.Native", NULL, "__Internal", NULL);
    mono_dllmap_insert(NULL, "System.Security.Cryptography.Native.Apple", NULL, "__Internal", NULL);
#if DOTNET8
    mono_dllmap_insert(NULL, "System.Globalization.Native", NULL, "__Internal", NULL);
#endif
#endif

    setenv("DOTNET_SYSTEM_GLOBALIZATION_INVARIANT", "1", TRUE);
#else
#if DOTNET9
#if PLATFORM_WINDOWS
    const char* PropertyKeys[] = { "NATIVE_DLL_SEARCH_DIRECTORIES" };
    const char* PropertyValues[] = { TCHAR_TO_ANSI(*LibDirectory) };

    monovm_initialize(std::size(PropertyKeys), PropertyKeys, PropertyValues);
    // ★ Windows/.NET9 不是靠默认 PATH，而是显式把 Mono lib 目录灌进 VM 初始化参数
#endif
#endif

    mono_jit_set_aot_mode(MONO_AOT_MODE_NONE);
#endif
```

关键源码 [2] `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_config.h:473-486,726-740,1198-1203`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_config.h
// 位置: AngelScript 在编译期选择 ABI/callconv 约束
// ============================================================================
#if defined(_M_X64)
    #define AS_X64_MSVC
    #undef AS_NO_THISCALL_FUNCTOR_METHOD
    #define AS_CALLEE_DESTROY_OBJ_BY_VAL
    #define AS_LARGE_OBJS_PASSED_BY_REF
    // ★ x64 MSVC 的 ABI 约束在预处理阶段已经定死
#endif

#if (defined(_ARM_) || defined(__arm__)) || defined(__arm64__)
#ifdef __arm64__
    #define AS_ARM64
#else
    #define AS_ARM
#endif
    #undef AS_NO_THISCALL_FUNCTOR_METHOD
    #define CDECL_RETURN_SIMPLE_IN_MEMORY
    #define STDCALL_RETURN_SIMPLE_IN_MEMORY
    #define THISCALL_RETURN_SIMPLE_IN_MEMORY
    // ★ ARM/ARM64 直接走另一套返回值与 thiscall 规则
#endif

// If there are no current support for native calling
// conventions, then compile with AS_MAX_PORTABILITY
#if (!defined(AS_X86) && !defined(AS_SH4) && !defined(AS_MIPS) && !defined(AS_PPC) && !defined(AS_PPC_64) && !defined(AS_XENON) && !defined(AS_X64_GCC) && !defined(AS_X64_MSVC) && !defined(AS_ARM) && !defined(AS_X64_MINGW)) && !defined(AS_ARM64)
    #ifndef AS_MAX_PORTABILITY
        // #define AS_MAX_PORTABILITY // AS FIX - native calling conventions were replaced with template incantations
    #endif
#endif
// ★ 未命中已知 ABI 时，设计上是切到可移植实现，而不是运行时再做 loader remap
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 平台差异的处理时机 | UnrealCSharp 在 `FMonoDomain` 启动阶段修 native 依赖解析；Angelscript 在 `as_config.h` 编译期选 ABI | 实现方式不同 |
| native 依赖查找模型 | UnrealCSharp 需要 `mono_dllmap_insert` / `NATIVE_DLL_SEARCH_DIRECTORIES` 这类 runtime loader seam | 实现方式不同 |
| 未知平台退路 | Angelscript 代码里明确保留 `AS_MAX_PORTABILITY` 退路；UnrealCSharp 当前可见代码更偏“按已知平台分支修补 Mono 启动” | 实现质量差异，Angelscript 的 ABI 退化策略更显式 |

### [维度 D3] WorldContext 语义归属：metadata projection vs hidden-argument trait split

前面的 D3 已经写过 override、RPC 和输入绑定。这一轮只看一个更细、但对 Blueprint authoring 很关键的语义点：`WorldContext` 到底寄存在什么地方。UnrealCSharp 的做法是把它完全留在 UE metadata 世界里。`WorldContextAttribute`、`CallableWithoutWorldContextAttribute`、`ShowWorldContextPinAttribute` 都只是普通 C# attribute；`FDynamicGeneratorCore::SetFieldMetaData()` 再统一把 attribute 名去掉 `Attribute` 后缀，直接写回 `UFunction` / `UClass` metadata。这意味着 `WorldContext` 在 UnrealCSharp 里不是单独的脚本 VM 语义，而是被降格成 UE 原生反射元数据，让 Blueprint/editor 自己解释。

Angelscript 则拆成两层。第一层，`AngelscriptClassGenerator` 会给静态函数自动补一个隐藏的 `_World_Context` 参数，并在 `UASFunction` 上记录 `WorldContextIndex` / `bIsWorldContextGenerated`；第二层，`Helper_FunctionSignature.h` 把这个参数再投到脚本函数的 `hiddenArgumentIndex` 和 `hiddenArgumentDefault="__WorldContext()"`，同时把 `asTRAIT_USES_WORLDCONTEXT` 当成单独 trait 控制。测试还进一步证明：`CallableWithoutWorldContext` 场景下，隐藏参数仍然保留，但 trait 会被清掉。也就是说，Angelscript 明确把“调用时是否注入 world context”和“工具/调试器是否把它视为 world-context function”拆成两个状态位。

```
[D3-Deep] WorldContext Semantics

[UnrealCSharp]
C# attributes
├─ [WorldContext("Foo")]
├─ [CallableWithoutWorldContext]
└─ [ShowWorldContextPin]
    ↓
FDynamicGeneratorCore::SetFieldMetaData()
    ↓
UFunction/UClass metadata
    ↓
UE Blueprint/editor interprets behavior

[Angelscript]
function generation
├─ maybe synthesize hidden `_World_Context`
├─ set hiddenArgumentDefault="__WorldContext()"
└─ toggle asTRAIT_USES_WORLDCONTEXT separately
    ↓
compiler / debugger / binder consume two channels
```

关键源码 [1] `Reference/UnrealCSharp/Script/UE/Dynamic/Function/WorldContextAttribute.cs:5-13`、`Reference/UnrealCSharp/Script/UE/Dynamic/Function/CallableWithoutWorldContextAttribute.cs:5-9`、`Reference/UnrealCSharp/Script/UE/Dynamic/Class/ShowWorldContextPinAttribute.cs:5-9`、`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGeneratorCore.cpp:802-826,1067-1088,1232-1273`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Script/UE/Dynamic/Function/WorldContextAttribute.cs
// 位置: C# 侧 world-context 元数据只是普通 attribute
// ============================================================================
[AttributeUsage(AttributeTargets.Method)]
public class WorldContextAttribute : Attribute
{
    public WorldContextAttribute(string InValue)
    {
        Value = InValue;
    }

    private string Value { get; set; }
}

// ============================================================================
// 文件: Reference/UnrealCSharp/Script/UE/Dynamic/Function/CallableWithoutWorldContextAttribute.cs
// 位置: 可选 world-context 也是 metadata attribute，而不是独立 VM trait
// ============================================================================
[AttributeUsage(AttributeTargets.Method)]
public class CallableWithoutWorldContextAttribute : Attribute
{
    private string Value { get; set; } = "true";
}

// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGeneratorCore.cpp
// 函数: SetFieldMetaData / SetMetaData / GetClassMetaDataAttributes / GetFunctionMetaDataAttributes
// 位置: attribute 被统一投影为 UE metadata
// ============================================================================
for (const auto& MetaDataAttribute : InMetaDataAttributes)
{
    if (InReflection->HasAttribute(MetaDataAttribute))
    {
        FDynamicGeneratorCore::SetMetaData(InField, MetaDataAttribute->GetName(),
                                           InReflection->GetAttributeValue(MetaDataAttribute));
        // ★ 统一把 attribute 转成 UField metadata
    }
}

void FDynamicGeneratorCore::SetMetaData(UFunction* InFunction, FReflection* InReflection)
{
    SetFieldMetaData(InFunction, GetFunctionMetaDataAttributes(), InReflection, [](){});
}

static TArray<FClassReflection*> ClassMetaDataAttributes = {
    ReflectionRegistry.GetShowWorldContextPinAttributeClass(),
    ...
};

static TArray<FClassReflection*> FunctionMetaDataAttributes = {
    ...
    ReflectionRegistry.GetCallableWithoutWorldContextAttributeClass(),
    ...
    ReflectionRegistry.GetWorldContextAttributeClass(),
    ...
};
// ★ WorldContext / CallableWithoutWorldContext / ShowWorldContextPin 都走同一条 metadata 投影链
```

关键源码 [2] `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3521-3555`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h:423-430`、`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp:700-703`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 位置: 静态函数默认补隐藏 world-context 参数
// ============================================================================
// Generate a hidden world context argument for all static functions by default
if (FunctionDesc->bIsStatic)
{
    FString* WorldContextParam = FunctionDesc->Meta.Find(NAME_Arg_WorldContext);
    ...
    if (ParamIndex == -1)
    {
        FAngelscriptArgumentDesc ArgDesc;
        ArgDesc.ArgumentName = TEXT("_World_Context");
        ArgDesc.Type.Type = FAngelscriptType::GetByClass(UObject::StaticClass());

        FProperty* Prop = AddFunctionArgument(NewFunction, ArgDesc, false);
        WorldContextProperty = Prop;

#if WITH_EDITOR
        NewFunction->SetMetaData(NAME_Arg_WorldContext, *Prop->GetName());
#endif

        NewFunction->WorldContextIndex = FunctionDesc->Arguments.Num();
        NewFunction->bIsWorldContextGenerated = true;
        // ★ 参数注入本身就是 class generator 的职责
    }
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h
// 位置: world-context 再下沉为脚本函数隐藏参数 + trait
// ============================================================================
if (WorldContextArgument != -1)
{
    ScriptFunction->hiddenArgumentIndex = WorldContextArgument;
    ScriptFunction->hiddenArgumentDefault = "__WorldContext()";
#if WITH_EDITOR
    if (!Function->HasMetaData(NAME_OptionalWorldContext))
        ScriptFunction->traits.SetTrait(asTRAIT_USES_WORLDCONTEXT, true);
#endif
}
// ★ 注入默认参数和设置“uses world context”不是一个状态位
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp
// 位置: 自动化测试直接验证隐藏参数和 trait 可分离
// ============================================================================
TestEqual(TEXT("... required functions"), RequiredScriptFunction->hiddenArgumentIndex, 0);
TestEqual(TEXT("... callable-without-world-context functions"), OptionalScriptFunction->hiddenArgumentIndex, 0);
TestTrue(TEXT("... required world-context functions with the world-context trait"),
         RequiredScriptFunction->traits.GetTrait(asTRAIT_USES_WORLDCONTEXT));
return TestFalse(TEXT("... callable-without-world-context functions with the world-context trait"),
                 OptionalScriptFunction->traits.GetTrait(asTRAIT_USES_WORLDCONTEXT));
// ★ Angelscript 显式保证：隐藏参数还在，但 trait 可以被清掉
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| WorldContext 的主语义载体 | UnrealCSharp 把 world-context 规则投影到 UE metadata；Angelscript 用 hidden arg + trait 双通道表达 | 实现方式不同 |
| `CallableWithoutWorldContext` 语义 | Angelscript 有源码和测试直证“隐藏参数保留，但 `asTRAIT_USES_WORLDCONTEXT` 清除” | 实现质量差异，Angelscript 的语义拆分更显式 |
| 编辑器/工具耦合面 | UnrealCSharp 更依赖 UE 原生 metadata 解释；Angelscript 编译器、调试器、文档都能直接读脚本函数 trait/hidden arg | 实现方式不同 |

### [维度 D8] `out/ref` 参数回写 ABI：rebuilt `FOutParmRec` chain vs staged copy-back slots

前面的 D8 已经写过 buffer allocator、GC 和可卸载程序集。这一轮只看一个更底层的 ABI 细节：跨语言调用完成以后，`out/ref` 参数到底怎么回写到原始调用者。UnrealCSharp 的桥接层为了尽量保持 UE `ProcessEvent` 语义，会在 `FFunctionDescriptor::Initialize()` 先把参数分成 `ReferencePropertyIndexes` 和 `OutPropertyIndexes`；真正执行 `FCSharpFunctionDescriptor::CallCSharp()` 时，如果当前不是 native 栈入口，还会从 `FFrame` 重新拼一条 `FOutParmRec` 链，把 `MostRecentPropertyAddress` 保留下来。之后构造 `MonoObject[]` 时，引用参数优先走这条链拿真实地址；回写阶段再通过 `FindOutParmRec()` 找回同一个 out/ref 槽位。这意味着 UnrealCSharp 追求的是“尽可能复用 UE 现成的 out-param 别名语义”。

Angelscript 则分两条路。对 reflective fallback，它分配 `ParameterBuffer`，再用一个固定上限的 `FReflectiveOutReference OutReferences[...]` 数组记住哪些参数需要回填；`ProcessEvent()` 返回后按数组顺序 copy-back。对 `UASFunction` 生成路径，它更进一步，直接把 `RESULT_PARAM`、对象指针和 value bits 组装成 `VMArgs`，调用后从 `OutValue` 或 `RESULT_PARAM` 取回结果，不重建 `FOutParmRec`。也就是说，Angelscript 的 generated path 比 UnrealCSharp 更贴近自有 VM ABI；只有 reflective fallback 才回到“先 copy in，再 copy out”的兼容模式。

```
[D8-Deep] out/ref Write-back ABI

[UnrealCSharp patched UFunction]
FProperty scan
├─ classify OutPropertyIndexes / ReferencePropertyIndexes
├─ rebuild FOutParmRec from FFrame
├─ marshal MonoObject[] with true ref addresses
└─ FindOutParmRec() -> write back to original caller storage

[Angelscript reflective fallback]
FProperty scan
├─ copy args into ParameterBuffer
├─ remember FReflectiveOutReference[MaxArgs]
├─ ProcessEvent()
└─ fixed loop copy-back to script arg addresses

[Angelscript generated UASFunction]
BP VM / JIT entry
├─ pack VMArgs + RESULT_PARAM
├─ call script/JIT directly
└─ return path copies from OutValue or type helper
```

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FFunctionDescriptor.cpp:25-57`、`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FCSharpFunctionDescriptor.cpp:17-65,80-149,188-198`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FFunctionDescriptor.cpp
// 函数: FFunctionDescriptor::Initialize
// 位置: 先把 out/ref 参数索引预分类
// ============================================================================
const auto IsNativeFunction =
    FUnrealCSharpFunctionLibrary::IsNativeFunction(Function->GetOwnerClass(), Function->GetFName());

for (TFieldIterator<FProperty> It(Function.Get()); It && (It->PropertyFlags & CPF_Parm); ++It)
{
    ...
    if (Property->HasAnyPropertyFlags(CPF_OutParm) && !Property->HasAnyPropertyFlags(CPF_ConstParm))
    {
        if (IsNativeFunction || Property->HasAnyPropertyFlags(CPF_ReferenceParm))
        {
            ReferencePropertyIndexes.Emplace(Index);
            // ★ 哪些参数需要按“真实引用地址”处理，先静态分类
        }

        OutPropertyIndexes.Emplace(Index);
    }
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FCSharpFunctionDescriptor.cpp
// 函数: FCSharpFunctionDescriptor::CallCSharp / FindOutParmRec
// 位置: 运行时重建 FOutParmRec，并据此做回写
// ============================================================================
FOutParmRec* NewOutParams{};

if (InStack.Node != InStack.CurrentNativeFunction)
{
    ...
    if (Property->HasAnyPropertyFlags(CPF_OutParm))
    {
        InStack.Step(InStack.Object, Property->ContainerPtrToValuePtr<uint8>(Params));

        const auto OutParam = (FOutParmRec*)FMemory_Alloca(sizeof(FOutParmRec));
        OutParam->PropAddr = InStack.MostRecentPropertyAddress != nullptr
                                 ? InStack.MostRecentPropertyAddress
                                 : Property->ContainerPtrToValuePtr<uint8>(Params);
        OutParam->Property = Property;
        // ★ 用 FFrame 现场恢复 UE 约定的 out-param 链
    }
}

...

if (ReferencePropertyIndexes.Contains(Index))
{
    ReferenceParam = FindOutParmRec(ReferenceParam, ReferencePropertyDescriptor->GetProperty());
    if (ReferenceParam != nullptr)
    {
        PropertyAddress = ReferenceParam->PropAddr;
        // ★ 引用参数优先取回原始调用者地址，而不是临时 Params 缓冲
    }
}

...

OutParams = FindOutParmRec(OutParams, OutPropertyDescriptor->GetProperty());
if (OutParams != nullptr)
{
    ...
    // ★ 回写阶段再次通过 FOutParmRec 找到同一个 out/ref 槽位
}

FOutParmRec* FCSharpFunctionDescriptor::FindOutParmRec(FOutParmRec* OutParam, const FProperty* OutProperty)
{
    while (OutParam != nullptr)
    {
        if (OutParam->Property == OutProperty)
        {
            return OutParam;
        }
        OutParam = OutParam->NextOutParm;
    }
    return nullptr;
}
```

关键源码 [2] `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp:68-75,302-366`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:181-210,305-330`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp
// 位置: reflective fallback 用固定数组记录回填槽位
// ============================================================================
void* ResolveScriptArgumentAddress(const FProperty* Property, void* ScriptArgumentAddress)
{
    if (Property != nullptr && Property->HasAnyPropertyFlags(CPF_ReferenceParm))
    {
        return ScriptArgumentAddress != nullptr ? *(void**)ScriptArgumentAddress : nullptr;
    }

    return ScriptArgumentAddress;
}

uint8* ParameterBuffer = static_cast<uint8*>(FMemory_Alloca(Function->ParmsSize));
InitializeParameterBuffer(Function, ParameterBuffer);

FReflectiveOutReference OutReferences[BlueprintCallableReflectiveFallbackMaxArgs];
int32 OutReferenceCount = 0;

...

Property->CopySingleValue(Destination, SourceAddress);

if (Property->HasAnyPropertyFlags(CPF_ReferenceParm) && !Property->HasAnyPropertyFlags(CPF_ConstParm))
{
    OutReferences[OutReferenceCount++] = { Property, SourceAddress };
    // ★ 这里只保存“属性 + 脚本侧地址”，没有重建 FOutParmRec 链
}

TargetObject->ProcessEvent(Function, ParameterBuffer);

for (int32 OutReferenceIndex = 0; OutReferenceIndex < OutReferenceCount; ++OutReferenceIndex)
{
    const FReflectiveOutReference& OutReference = OutReferences[OutReferenceIndex];
    OutReference.Property->CopySingleValue(
        OutReference.ScriptValue,
        OutReference.Property->ContainerPtrToValuePtr<void>(ParameterBuffer));
    // ★ 统一按记录数组做 copy-back
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp
// 位置: generated UASFunction 路径直接走 VMArgs / RESULT_PARAM
// ============================================================================
uint8* ArgStack = (uint8*)FMemory_Alloca(ASFunction->ArgStackSize);
asDWORD* VMArgs = (asDWORD*)FMemory_Alloca(8 * ArgumentCount + 16);

if (ASFunction->ReturnArgument.VMBehavior == UASFunction::EArgumentVMBehavior::ReturnObjectPOD)
{
    *(void**)VMArgs = RESULT_PARAM;
    VMArgs += 2;
}
else if (ASFunction->ReturnArgument.VMBehavior == UASFunction::EArgumentVMBehavior::ReturnObjectValue)
{
    ASFunction->ReturnArgument.Type.DestructValue(RESULT_PARAM);
    *(void**)VMArgs = RESULT_PARAM;
    VMArgs += 2;
}

...

switch (ASFunction->ReturnArgument.VMBehavior)
{
case UASFunction::EArgumentVMBehavior::ReferencePOD:
{
    void* RetValue = (void*&)OutValue;
    if (RetValue != nullptr)
        FMemory::Memcpy(RESULT_PARAM, RetValue, ASFunction->ReturnArgument.ValueBytes);
}
break;
case UASFunction::EArgumentVMBehavior::Reference:
{
    void* RetValue = (void*&)OutValue;
    if (RetValue != nullptr)
        ASFunction->ReturnArgument.Type.CopyValue(RetValue, RESULT_PARAM);
}
break;
...
}
// ★ generated path 直接围绕 VM return convention 工作，不需要 FOutParmRec 兼容层
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| `out/ref` 的核心兼容策略 | UnrealCSharp 重建 `FOutParmRec`，尽量贴 UE 原生调用语义；Angelscript fallback 用固定回填表，generated path 直接走自有 VM ABI | 实现方式不同 |
| 引用参数地址来源 | UnrealCSharp 从 `FFrame` 现场恢复真实 `PropAddr`；Angelscript fallback 只保存脚本侧地址快照 | 实现质量差异，UnrealCSharp 的 UE 语义保真更高 |
| 热路径优化方向 | Angelscript generated path 省掉 `FOutParmRec` 兼容层，回写更直达；UnrealCSharp 为了 patch `UFunction` 必须保留一层 UE ABI 适配 | 实现方式不同 |

---

## 深化分析 (2026-04-09 06:39:02)

### [维度 D2] 默认参数合同：dual-channel stringifier vs metadata round-trip

前面的 D2 已经写过 override patch、type bridge 和 `InternalCall` 平面。这一轮只看一个更细但很能说明设计取向的问题：**默认参数到底寄存在什么地方，谁负责把它变成脚本可见语法**。UnrealCSharp 在当前快照里其实有两条并行通道。第一条是 binding DSL 通道：`TFunctionInfo::Get()` 在模板实例化时直接把默认参数折叠成 `TArray<FString>`，`TDefaultArgument` 再把 `bool/float/FString/FRotator/UEnum` 这类 C++ 值编码成 C# 源码片段。第二条是 reflection export 通道：`FClassGenerator` 为反射导出的 `UFunction` 读取 `CPP_Default_*` metadata，再把它写进生成出来的 C# API。也就是说，默认参数在 UnrealCSharp 里并不是单一 substrate；一部分属于 compile-time binding descriptor，一部分属于 UE reflection metadata。

Angelscript 这里更统一。导入 UE 函数时，`Helper_FunctionSignature` 直接读取 `CPP_Default_*` metadata 和 `WorldContext` metadata 生成脚本签名；脚本生成 `UFunction` 时，`AngelscriptClassGenerator` 又把 `ArgDesc.DefaultValue` 经 `DefaultValue_AngelscriptToUnreal()` 反向写回 `CPP_Default_*`。同一套 UE metadata 同时承担“从 Unreal 到脚本的默认值投影”和“从脚本回 Unreal 的默认值回填”。这使它的默认参数更像 **metadata round-trip contract**，而不是 UnrealCSharp 那样分裂成 descriptor channel + metadata channel 两条链。

```
[D2-Deep] Default Argument Contract

[UnrealCSharp]
native binding DSL
├─ TFunctionInfo::Get(...)
├─ TDefaultArgument<T>::Get(...)              // 编译期把 C++ 默认值转成 C# 代码字面量
└─ FBindingFunction::GetDefaultArguments()

reflection export
├─ UFunction metadata `CPP_Default_*`
└─ FClassGenerator reads metadata            // 为生成的 C# API 补默认参数

[Angelscript]
UE import
├─ read `CPP_Default_*`
└─ build script signature defaults

script export
├─ read ArgDesc.DefaultValue
└─ write back `CPP_Default_*`                // 同一份 metadata 往返复用
```

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Binding/Function/TFunctionInfo.inl:18-25,79-120`、`Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Binding/Function/TDefaultArgument.inl:85-109,189-203,250-258,333-337,438-446`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Binding/Function/TFunctionInfo.inl
// 函数: TFunctionInfo<...>::Get
// 位置: 18-25, 79-120，binding descriptor 保存默认参数字符串数组
// ============================================================================
explicit TFunctionInfo(const TArray<FString>& InParamNames, const TArray<FString>& InDefaultArguments):
    ParamNames(InParamNames),
    DefaultArguments(InDefaultArguments), // ★ 默认参数直接落在 function descriptor 里
    FunctionInteract(EFunctionInteract::None),
    Return{TTypeInfo<Result>::Get()},
    Argument{TTypeInfo<Args>::Get()...}
{
}

virtual auto GetDefaultArguments() const -> const TArray<FString>& override
{
    return DefaultArguments;
}

template <typename... DefaultArguments>
static auto Get(const TArray<FString>& InParamNames, DefaultArguments&&... InDefaultArguments) -> FFunctionInfo*
{
    static TFunctionInfo Instance(InParamNames,
                                  TDefaultArguments<Args...>::Get(
                                      std::forward<DefaultArguments>(InDefaultArguments)...));
    // ★ 这里没有经过 UFunction metadata，而是模板实例化时直接构造字符串默认值
    return &Instance;
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Binding/Function/TDefaultArgument.inl
// 位置: 85-109, 189-203, 250-258, 333-337, 438-446，C++ 默认值到 C# 字面量的编译期编码
// ============================================================================
template <typename T>
struct TDefaultArgumentBuilder
{
    template <typename... Args>
    static auto Get(Args&&... InDefaultArguments)
    {
        return FString::Printf(TEXT(
            "new %s(%s)"),
                               *TName<T, T>::Get(),
                               *GetImplementation<Space>(InDefaultArguments...));
        // ★ 复合类型默认值直接拼成 `new Type(...)` 的 C# 代码
    }
};

template <typename T>
struct TDefaultArgument<T, std::enable_if_t<std::is_same_v<std::decay_t<T>, float>, T>>
{
    static auto Get(const float InValue)
    {
        return FString::Printf(TEXT("%sf"), *SanitizeFloat(InValue)); // ★ float -> `1.0f`
    }
};

template <typename T>
struct TDefaultArgument<T, std::enable_if_t<std::is_same_v<std::decay_t<T>, FString>, T>>
{
    static auto Get(const FString& InValue)
    {
        return FString::Printf(TEXT("new %s(\"%s\")"), *TName<T, T>::Get(), *InValue);
        // ★ FString -> `new FString("...")`
    }
};

template <typename T>
struct TDefaultArgument<T, std::enable_if_t<std::is_same_v<std::decay_t<T>, FRotator>, T>>
{
    static auto Get(const FRotator& InValue)
    {
        return TDefaultArgumentBuilder<T>::Get(InValue.Pitch, InValue.Yaw, InValue.Roll);
    }
};

template <typename T>
struct TDefaultArgument<T, std::enable_if_t<TIsEnum<std::decay_t<T>>::Value && !TIsNotUEnum<std::decay_t<T>>::Value, T>>
{
    static auto Get(const T& InValue)
    {
        return FString::Printf(TEXT("%s.%s"),
                               *StaticEnum<std::decay_t<T>>()->GetName(),
                               *StaticEnum<std::decay_t<T>>()->GetNameStringByValue(static_cast<int64>(InValue)));
        // ★ UEnum 默认值直接编码成 `EnumType.EnumMember`
    }
};
```

关键源码 [2] `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp:906-910,945-955,1280-1292`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp
// 函数: HasCppFunctionDefaultParam / GetCppFunctionDefaultParam / GeneratorCppFunctionDefaultParam
// 位置: 906-910, 945-955, 1280-1292，反射导出的默认参数从 `CPP_Default_*` metadata 读取
// ============================================================================
bool FClassGenerator::HasCppFunctionDefaultParam(const UFunction* InFunction, const FProperty* InProperty)
{
    const auto Key = FString::Printf(TEXT("CPP_Default_%s"), *InProperty->GetName());
    return InFunction->HasMetaData(*Key);
}

FString FClassGenerator::GetCppFunctionDefaultParam(const UFunction* InFunction, FProperty* InProperty)
{
    const auto Key = FString::Printf(TEXT("CPP_Default_%s"), *InProperty->GetName());
    if (!InFunction->HasMetaData(*Key))
    {
        return TEXT("");
    }

    const auto MetaData = InFunction->GetMetaData(*Key);
    // ★ 反射导出链并不看 `TFunctionInfo::DefaultArguments`，而是重新回到 UFunction metadata
}

FString FClassGenerator::GeneratorCppFunctionDefaultParam(const UFunction* InFunction, FProperty* InProperty)
{
    const auto Key = FString::Printf(TEXT("CPP_Default_%s"), *InProperty->GetName());
    if (!InFunction->HasMetaData(*Key))
    {
        return TEXT("");
    }

    const auto MetaData = InFunction->GetMetaData(*Key);
    return GeneratorFunctionDefaultParam(InProperty, MetaData);
}
```

关键源码 [3] `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h:210-235`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3994-4004`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h
// 位置: 210-235，导入 UE 函数时读取 metadata 默认值与 world-context 默认值
// ============================================================================
FString MetaStr = Function->GetMetaData(MetaName);
if (MetaStr == TEXT("None"))
    MetaStr = TEXT("");
ArgumentDefaults.Add(MetaStr); // ★ `CPP_Default_*` 直接进入脚本签名默认值

const FString& WorldContextParam = Function->GetMetaData(NAME_Signature_WorldContext);
if (WorldContextParam.Len() != 0)
{
    for (int32 ArgIndex = 0, ArgCount = ArgumentTypes.Num(); ArgIndex < ArgCount; ++ArgIndex)
    {
        if (ArgumentNames[ArgIndex] == WorldContextParam)
        {
            ArgumentDefaults[ArgIndex] = TEXT("__WorldContext()");
            WorldContextArgument = ArgIndex;
            break;
        }
    }
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 位置: 3994-4004，脚本导出回 UE 时把默认值重新写成 `CPP_Default_*`
// ============================================================================
#if WITH_EDITOR
if (ArgDesc.DefaultValue.Len() != 0)
{
    FString UnrealDefaultValue;
    if (ArgDesc.Type.DefaultValue_AngelscriptToUnreal(ArgDesc.DefaultValue, UnrealDefaultValue))
    {
        FString DefaultValueMeta = TEXT("CPP_Default_");
        DefaultValueMeta += ArgDesc.ArgumentName;
        NewFunction->SetMetaData(*DefaultValueMeta, *UnrealDefaultValue);
        // ★ script -> metadata -> Blueprint/editor，形成默认参数回写闭环
    }
}
#endif
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 默认参数的主存储面 | UnrealCSharp 同时存在 `FFunctionInfo::DefaultArguments` 与 `CPP_Default_*` 两条链；Angelscript import/export 都围绕 `CPP_Default_*` metadata 往返 | 实现方式不同 |
| 编译期字面量能力 | UnrealCSharp 的 `TDefaultArgument` 能把 `FString/FRotator/UEnum` 直接编码成 C# 源码片段 | 实现方式不同 |
| Blueprint / editor 复用面 | Angelscript 用同一份 metadata 驱动脚本签名和生成后的 `UFunction`，传播面更统一 | 实现质量差异，Angelscript 的默认值 substrate 更集中 |

### [维度 D6] 模板脚手架所有权：plugin-shipped scaffolds vs navigation-only editor path

前面的 D6 已经覆盖过 `sln/csproj`、Analyzer、Weaver、Content Browser code items。这一轮只看“**新脚本文件的第一行代码由谁拥有**”。UnrealCSharp 的答案很明确：由插件自己拥有。`FUnrealCSharpFunctionLibrary` 把 `Template/Dynamic` 和 `Template/Override` 做成正式目录约定；`FDynamicNewClassUtils::GetDynamicClassContent()` 会按 `AActor / UActorComponent / UUserWidget / UObject` 四种祖先类选模板、替换命名空间和父类；`SDynamicNewClassDialog::FinishClicked()` 直接把模板实例化结果写进目标 `.cs` 并打开源码。Blueprint toolbar 也是同一套模式，只不过它消费的是 `Template/Override/*.cs`。

Angelscript 当前可见 editor path 则更克制。`UAngelscriptContentBrowserDataSource` 的 `GetItemPhysicalPath/CanEditItem/EditItem/BulkEditItems` 都直接返回 `false`；`FAngelscriptSourceCodeNavigation` 的职责是把现有 `UASClass/UASFunction` 映射回既有 `.as` 文件路径并 `code --goto` 打开。这意味着目前可见链路重点在 **导航到已有源码**，而不是 **模板化生成新源码**。这里要明确区分“直证”和“范围化推断”：源码直证是 UnrealCSharp 有模板落盘链、Angelscript 当前 editor 入口是导航；基于对 `Plugins/Angelscript/Source/AngelscriptEditor/Private`（排除 `Tests`）中 `SaveStringToFile/OpenAdd/NewScript` 的检索未命中，可以推断当前快照没有对等的插件内 `.as` 脚手架写文件链。

```
[D6-Deep] Scaffold Ownership

[UnrealCSharp]
plugin Template/
├─ Dynamic/*.cs
├─ Override/*.cs
├─ DynamicNewClassUtils loads template
├─ replace namespace / parent / class name
└─ SaveStringToFile(target .cs) -> OpenSourceFile

[Angelscript]
editor objects
├─ ContentBrowserDataSource item
│  ├─ GetItemPhysicalPath -> false
│  └─ EditItem -> false
└─ SourceCodeNavigation
   └─ Open existing `.as` by path/line
```

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp:900-930`、`Reference/UnrealCSharp/Template/Dynamic/DynamicObject.cs:1-12`、`Reference/UnrealCSharp/Template/Override/Object.cs:1-12`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp
// 位置: 900-930，插件显式维护 Dynamic / Override 模板目录
// ============================================================================
FString FUnrealCSharpFunctionLibrary::GetPluginTemplateDirectory()
{
    return GetPluginDirectory() / PLUGIN_TEMPLATE_PATH;
}

FString FUnrealCSharpFunctionLibrary::GetPluginTemplateOverrideFileName(const UClass* InClass)
{
    return GetPluginTemplateOverrideDirectory() /
        FString::Printf(TEXT("%s%s"), *InClass->GetName(), *CSHARP_SUFFIX);
}

FString FUnrealCSharpFunctionLibrary::GetPluginTemplateDynamicFileName(const UClass* InClass)
{
    return GetPluginTemplateDynamicDirectory() /
        FString::Printf(TEXT("%s%s%s"), ...);
    // ★ 模板路径不是散落字符串，而是正式函数接口
}
```

```csharp
// ============================================================================
// 文件: Reference/UnrealCSharp/Template/Dynamic/DynamicObject.cs
// 位置: 动态类模板骨架
// ============================================================================
using Script.Dynamic;
using Script.CoreUObject;

namespace Script.CoreUObject
{
    [UClass]
    public partial class UDynamicObject : UObject
    {
        public UDynamicObject()
        {
        }
    }
}

// ============================================================================
// 文件: Reference/UnrealCSharp/Template/Override/Object.cs
// 位置: Blueprint override 模板骨架
// ============================================================================
using Script.CoreUObject;

namespace Script.CoreUObject
{
    [Override]
    public partial class UObject
    {
        public UObject()
        {
        }
    }
}
```

关键源码 [2] `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/NewClass/DynamicNewClassUtils.cpp:102-138,161-184`、`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/NewClass/SDynamicNewClassDialog.cpp:765-781`、`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:91-138`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/NewClass/DynamicNewClassUtils.cpp
// 位置: 102-138, 161-184，按祖先类挑模板并替换内容
// ============================================================================
const auto TemplateFileName = FUnrealCSharpFunctionLibrary::GetPluginTemplateDynamicFileName(AncestorClass);
FFileHelper::LoadFileToString(OutContent, *TemplateFileName);

OutContent.ReplaceInline(... *FUnrealCSharpFunctionLibrary::GetFullClass(AncestorClass),
                         ... *ParentClassName);
OutContent.ReplaceInline(... *OldClassFullName, ... *GeneratedClassName);
// ★ 模板实例化阶段就把 namespace / parent / class name 改写完

static TArray TemplateClasses =
{
    AActor::StaticClass(),
    UActorComponent::StaticClass(),
    UUserWidget::StaticClass(),
    UObject::StaticClass()
};
// ★ 脚手架模板的种类被显式限制在四个 authoring archetype
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/NewClass/SDynamicNewClassDialog.cpp
// 位置: 765-781，模板内容直接落盘为目标 `.cs`
// ============================================================================
if (const auto ParentClass = SelectedParentClassInfo->GetClass())
{
    FString NewClassContent;
    FDynamicNewClassUtils::GetDynamicClassContent(ParentClass, NewClassName, NewClassContent);

    FUnrealCSharpFunctionLibrary::SaveStringToFile(*GetDynamicFileName(), NewClassContent);
    CloseContainingWindow();
    FSourceCodeNavigation::OpenSourceFile(GetDynamicFileName());
    // ★ “创建类”动作本质上就是“模板实例化 + 写文件 + 打开源码”
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp
// 位置: 91-138，Blueprint override 同样走模板文件链
// ============================================================================
for (const auto TemplateClass : TemplateClasses)
{
    if (Blueprint->GeneratedClass->IsChildOf(TemplateClass))
    {
        const auto Template = FUnrealCSharpFunctionLibrary::GetPluginTemplateOverrideFileName(TemplateClass);
        FString Content;
        FFileHelper::LoadFileToString(Content, *Template);

        Content.ReplaceInline(... *FUnrealCSharpFunctionLibrary::GetClassNameSpace(TemplateClass),
                              ... *FUnrealCSharpFunctionLibrary::GetClassNameSpace(Blueprint->GeneratedClass));
        Content.ReplaceInline(... *FUnrealCSharpFunctionLibrary::GetFullClass(TemplateClass),
                              ... *FUnrealCSharpFunctionLibrary::GetFullClass(Blueprint->GeneratedClass));

        if (const auto FileName = GetFileName();
            FUnrealCSharpFunctionLibrary::SaveStringToFile(FileName, Content))
        {
            ...
        }
        // ★ override 文件也不是手写空白文件，而是模板实例化
    }
}
```

关键源码 [3] `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:182-199`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp:34-45,95-115`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp
// 位置: 182-199，Content Browser data source 不负责就地编辑脚本源码
// ============================================================================
bool UAngelscriptContentBrowserDataSource::GetItemPhysicalPath(const FContentBrowserItemData& InItem, FString& OutDiskPath)
{
    return false;
}

bool UAngelscriptContentBrowserDataSource::CanEditItem(const FContentBrowserItemData& InItem, FText* OutErrorMsg)
{
    return false;
}

bool UAngelscriptContentBrowserDataSource::EditItem(const FContentBrowserItemData& InItem)
{
    return false;
}

bool UAngelscriptContentBrowserDataSource::BulkEditItems(TArrayView<const FContentBrowserItemData> InItems)
{
    return false;
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp
// 位置: 34-45, 95-115，现有主路径是打开已存在的 `.as` 文件
// ============================================================================
virtual bool NavigateToFunction(const UFunction* InFunction) override
{
    auto* ASFunc = Cast<const UASFunction>(InFunction);
    if (ASFunc == nullptr)
        return false;

    FString Path = ASFunc->GetSourceFilePath();
    if (Path.Len() == 0)
        return false;

    OpenFile(Path, ASFunc->GetSourceLineNumber());
    return true;
}

void OpenFile(const FString& Path, int LineNo = -1)
{
    if (LineNo != -1)
        FPlatformMisc::OsExecute(nullptr, TEXT("code"), *FString::Printf(TEXT("--goto \"%s:%d\""), *Path, LineNo));
    else
        FPlatformMisc::OsExecute(nullptr, TEXT("code"), *FString::Printf(TEXT("\"%s\""), *Path));
    // ★ 这里是导航到既有文件，不是生成模板文件
}
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 脚手架所有权 | UnrealCSharp 把 `Template/Dynamic` 与 `Template/Override` 作为插件正式资产；Angelscript 当前可见 editor path 以导航现有源码为主 | 实现方式不同 |
| 新源码的创建入口 | UnrealCSharp 有“模板实例化 -> 写文件 -> 打开源码”的闭环 | 实现质量差异，UnrealCSharp 的 authoring entry point 更完整 |
| Angelscript 当前缺口范围 | 就插件内模板化 `.as` 脚手架而言，本轮在 `AngelscriptEditor/Private`（排除 `Tests`）未定位到对等写文件链 | 没有实现（限于模板化脚手架这一子能力） |

### [维度 D8] 调用形状专门化：finite call-family surface vs per-argument VMBehavior packing

前面的 D8 已经讲过 `JIT/AOT`、GC、缓冲生命周期。这一轮只看 **跨语言调用到底在什么层面被专门化**。UnrealCSharp 把专门化前移到了桥接 API surface：`FGeneratorCore::GetFunctionPrefix()` 只根据返回值是否 primitive 区分 `Primitive / Compound / Generic`；`GetFunctionIndex()` 再用 `hasReturn / hasInput / hasOutput / isNative / isNet` 五个位拼成编号；`FClassGenerator` 直接生成 `FFunctionImplementation.FFunction_%sCall%dImplementation(...)` 调用。native 侧 `FRegisterFunction` 只注册有限的 shape family：`Call0/1/2/3/4/6/7/8/9/10/11/14/15/16/18/24/26`。这说明它不是“每个函数一条专属 ABI”，而是“每个函数先被归入有限个 bucket，再复用 bucket 对应的 descriptor call”。

Angelscript 的专门化位置更靠后。`ASClass.cpp` 不暴露 `Call7` 这类 family 名称，而是读取每个 `UASFunction::FArgument` 的 `EArgumentVMBehavior`，在 `switch` 里逐个决定参数如何写入 `VMArgs`：`FloatExtendedToDouble`、`WorldContextObject`、`ObjectPointer`、`ReferencePOD`、`Reference`、`Value1/2/4/8Byte` 都是单独路径。最后直接调用 `JitFunction(Execution, VMArgStart, &OutValue)`。因此 UnrealCSharp 的优化落点是 **导出桥接面上的有限 ABI 桶**，Angelscript 的优化落点是 **每个函数自己的 argument behavior table**。两边都在专门化，但专门化发生的位置不同。

```
[D8-Deep] Call Shape Specialization

[UnrealCSharp]
FGeneratorCore
├─ GetFunctionPrefix()                  // Primitive / Compound / Generic
├─ GetFunctionIndex()                   // hasReturn / in / out / native / net bitmask
└─ FClassGenerator emits `FFunction_*CallNImplementation`
    ↓
FRegisterFunction
└─ finite exported call families        // 0,1,2,3,4,6,7,8,9,10,11,14,15,16,18,24,26
    ↓
descriptor->CallN(...)

[Angelscript]
UASFunction::Arguments[]
├─ each arg has VMBehavior
├─ switch per argument
│  ├─ Value1/2/4/8Byte
│  ├─ Reference / ReferencePOD
│  ├─ WorldContextObject
│  └─ FloatExtendedToDouble
└─ pack VMArgs sequentially -> JitFunction(...)
```

关键源码 [1] `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:576-645`、`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp:655-675`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp
// 位置: 576-645，函数先被压缩到 `prefix + index` 这组有限调用形状
// ============================================================================
bool FGeneratorCore::IsPrimitiveProperty(FProperty* Property)
{
    if (CastField<FByteProperty>(Property) || CastField<FUInt16Property>(Property) ||
        CastField<FUInt32Property>(Property) || CastField<FUInt64Property>(Property) ||
        CastField<FInt8Property>(Property) || CastField<FInt16Property>(Property) ||
        CastField<FIntProperty>(Property) || CastField<FInt64Property>(Property) ||
        CastField<FBoolProperty>(Property) || CastField<FFloatProperty>(Property) ||
        CastField<FEnumProperty>(Property) || CastField<FDoubleProperty>(Property))
    {
        return true;
    }

    return false;
}

FString FGeneratorCore::GetFunctionPrefix(FProperty* Property)
{
    return Property != nullptr
               ? IsPrimitiveProperty(Property)
                     ? TEXT("Primitive")
                     : TEXT("Compound")
               : TEXT("Generic");
    // ★ 返回值只分三类：primitive / compound / void
}

int32 FGeneratorCore::GetFunctionIndex(const bool bHasReturn, const bool bHasInput, const bool bHasOutput,
                                       const bool bIsNative, const bool bIsNet)
{
    return static_cast<int32>(bHasReturn) |
        static_cast<int32>(bHasInput) << 1 |
        static_cast<int32>(bHasOutput) << 2 |
        static_cast<int32>(bIsNative) << 3 |
        static_cast<int32>(bIsNet) << 4;
    // ★ 五个布尔位拼出 call-family 编号，而不是按函数签名逐个生 ABI
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp
// 位置: 655-675，生成代码时直接落到 `FFunction_*CallNImplementation`
// ============================================================================
auto FunctionCallBody = FString::Printf(TEXT(
    "FFunctionImplementation.FFunction_%sCall%dImplementation(%s, %s%s%s%s);\n"
),
                                        *FGeneratorCore::GetFunctionPrefix(FunctionReturnParam),
                                        FGeneratorCore::GetFunctionIndex(FunctionReturnParam != nullptr,
                                            FunctionParams.Num() - FunctionOutParamIndex.Num() != 0,
                                            !FunctionRefParamIndex.IsEmpty() || !FunctionOutParamIndex.IsEmpty(),
                                            Function->HasAnyFunctionFlags(FUNC_Native),
                                            Function->HasAnyFunctionFlags(FUNC_Net)),
                                        ...);
// ★ 绑定生成器在落 C# 代码之前，已经把函数归约成固定 call family
```

关键源码 [2] `Reference/UnrealCSharp/Script/UE/Library/FFunctionImplementation.cs:5-102`、`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Domain/InternalCall/FRegisterFunction.cpp:11-347`

```csharp
// ============================================================================
// 文件: Reference/UnrealCSharp/Script/UE/Library/FFunctionImplementation.cs
// 位置: 5-102，managed 侧只暴露有限个固定形状的 call family
// ============================================================================
public static unsafe class FFunctionImplementation
{
    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern void FFunction_GenericCall0Implementation(nint InMonoObject, uint InFunctionHash);

    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern void FFunction_PrimitiveCall3Implementation(nint InMonoObject, uint InFunctionHash,
        byte* InBuffer, byte* ReturnBuffer);

    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern void FFunction_CompoundCall7Implementation(nint InMonoObject, uint InFunctionHash,
        byte* InBuffer, byte* OutBuffer, byte* ReturnBuffer);

    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern void FFunction_GenericCall26Implementation(nint InMonoObject, uint InFunctionHash,
        byte* InBuffer);
    // ★ API 面上暴露的是有限形状，而不是“一函数一桥”
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Domain/InternalCall/FRegisterFunction.cpp
// 位置: 11-347，native 侧把有限形状映射到 descriptor->CallN(...)
// ============================================================================
static void PrimitiveCall7Implementation(const FGarbageCollectionHandle InGarbageCollectionHandle,
                                         const uint32 InFunctionHash, IN_BUFFER_SIGNATURE, OUT_BUFFER_SIGNATURE,
                                         RETURN_BUFFER_SIGNATURE)
{
    if (const auto FoundObject = FCSharpEnvironment::GetEnvironment().GetObject(InGarbageCollectionHandle))
    {
        if (const auto FunctionDescriptor = FCSharpEnvironment::GetEnvironment().GetOrAddFunctionDescriptor<
            FUnrealFunctionDescriptor>(InFunctionHash))
        {
            FunctionDescriptor->Call7<EFunctionReturnType::Primitive>(
                FoundObject, IN_BUFFER, OUT_BUFFER, RETURN_BUFFER);
            // ★ bucket 命中后才进入真正的 descriptor dispatch
        }
    }
}

FClassBuilder(TEXT("FFunction"), NAMESPACE_LIBRARY)
    .Function("GenericCall0", GenericCall0Implementation)
    .Function("PrimitiveCall1", PrimitiveCall1Implementation)
    .Function("CompoundCall1", CompoundCall1Implementation)
    .Function("GenericCall2", GenericCall2Implementation)
    .Function("PrimitiveCall3", PrimitiveCall3Implementation)
    .Function("CompoundCall3", CompoundCall3Implementation)
    .Function("GenericCall4", GenericCall4Implementation)
    .Function("GenericCall6", GenericCall6Implementation)
    .Function("PrimitiveCall7", PrimitiveCall7Implementation)
    .Function("CompoundCall7", CompoundCall7Implementation)
    .Function("GenericCall8", GenericCall8Implementation)
    .Function("PrimitiveCall9", PrimitiveCall9Implementation)
    .Function("CompoundCall9", CompoundCall9Implementation)
    .Function("GenericCall10", GenericCall10Implementation)
    .Function("PrimitiveCall11", PrimitiveCall11Implementation)
    .Function("CompoundCall11", CompoundCall11Implementation)
    .Function("GenericCall14", GenericCall14Implementation)
    .Function("PrimitiveCall15", PrimitiveCall15Implementation)
    .Function("CompoundCall15", CompoundCall15Implementation)
    .Function("GenericCall16", GenericCall16Implementation)
    .Function("GenericCall18", GenericCall18Implementation)
    .Function("GenericCall24", GenericCall24Implementation)
    .Function("GenericCall26", GenericCall26Implementation);
// ★ exported ABI 面是显式枚举的有限集合
```

关键源码 [3] `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:181-339`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp
// 位置: 181-339，Angelscript 按 `EArgumentVMBehavior` 逐参打包 VMArgs
// ============================================================================
uint8* ArgStack = (uint8*)FMemory_Alloca(ASFunction->ArgStackSize);
int32 ArgumentCount = ASFunction->Arguments.Num();
asDWORD* VMArgs = (asDWORD*)FMemory_Alloca(8 * ArgumentCount + 16);
asDWORD* VMArgStart = VMArgs;

for (int32 i = 0; i < ArgumentCount; ++i)
{
    auto& Arg = ASFunction->Arguments[i];
    switch (Arg.VMBehavior)
    {
        case UASFunction::EArgumentVMBehavior::FloatExtendedToDouble:
            ...
            *(double*)VMArgs = (double)Value;
            VMArgs += 2;
            break;
        case UASFunction::EArgumentVMBehavior::WorldContextObject:
            ...
            *(void**)VMArgs = Ptr;
            VMArgs += 2;
            break;
        case UASFunction::EArgumentVMBehavior::ReferencePOD:
            ...
            *(void**)VMArgs = &RefValue;
            VMArgs += 2;
            break;
        case UASFunction::EArgumentVMBehavior::Value1Byte:
        case UASFunction::EArgumentVMBehavior::Value2Byte:
        case UASFunction::EArgumentVMBehavior::Value4Byte:
            ...
            *(asDWORD*)VMArgs = Value;
            VMArgs += 1;
            break;
        case UASFunction::EArgumentVMBehavior::Value8Byte:
            ...
            *(asQWORD*)VMArgs = Value;
            VMArgs += 2;
            break;
    }
}

(JitFunction)(Execution, VMArgStart, &OutValue);
// ★ 专门化发生在每个参数的 behavior 表，而不是导出 API family 名上
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 专门化落点 | UnrealCSharp 在 bridge surface 上枚举 finite call families；Angelscript 在每个 `UASFunction` 的 `VMBehavior` 打包逻辑里专门化 | 实现方式不同 |
| 扩展新 ABI 形状的成本 | UnrealCSharp 新形状要同时扩 `FGeneratorCore/FClassGenerator/FFunctionImplementation/FRegisterFunction`；Angelscript 更可能改 `EArgumentVMBehavior` 与 packing 逻辑 | 实现方式不同 |
| 性能结论边界 | 当前源码只能证明“优化位置不同”，不能单凭结构直接下结论谁更快 | 实现质量差异不可直接下结论，需要基准测试 |

---

## 深化分析 (2026-04-09 06:48:27)

### [维度 D3] 多播委托扇出所有权：callback UObject fan-out vs direct delegate slot binding

前面的 D3 已经写过 delegate API 表面形态。这一轮只追一个更底层的问题：**多播委托的“多路分发账本”到底由谁持有**。UnrealCSharp 的答案是“由额外创建出来的 callback UObject 持有”。`FMulticastDelegateHelper::Initialize()` 先创建并 `AddToRoot()` 一个 `UMulticastDelegateHandler`，随后只往真实的 `FMulticastScriptDelegate` 里挂入一条 `this.CSharpCallBack`；真正的多监听关系存放在 `DelegateWrappers`，也就是 `(UObject, FMethodReflection)` 的集合。回调发生时，`UMulticastDelegateHandler::ProcessEvent()` 只识别这一个 callback 名，再循环 `DelegateWrappers`，逐个交给 `FCSharpDelegateDescriptor::CallDelegate()` 做参数编组和托管调用。

Angelscript 的落点不同。`Bind_Delegates.cpp` 在 `AddUFunction()` / `AddUFunction_Signature()` 里每次都即时构造一个 `FScriptDelegate InnerDelegate`，校验签名后直接 `Delegate->AddUnique(InnerDelegate)`；如果是带 payload 的封装，则 `FAngelscriptDelegateWithPayload` 直接把 `Object + FunctionName + Payload` 持久化在值类型里，执行时自己 `FindFunction()` 后 `ProcessEvent()`。也就是说，Angelscript 的多播关系仍然留在 UE 原生 delegate 容器或脚本值对象里，没有像 UnrealCSharp 那样再插入一层“callback carrier UObject”。

```
[D3-Deep] Multicast Fanout Ownership

[UnrealCSharp]
FMulticastDelegateHelper
├─ NewObject<UMulticastDelegateHandler> + AddToRoot   // 创建回调宿主
├─ bind one ScriptDelegate -> this.CSharpCallBack     // UE 侧只看到一个 callback
├─ DelegateWrappers[(UObject, FMethodReflection)]*    // 真正的多监听账本
└─ ProcessEvent -> loop wrappers -> CallDelegate      // 回调时再扇出到 managed methods

[Angelscript]
FMulticastScriptDelegate / payload value
├─ validate target UFunction signature                // 绑定前校验签名
├─ create one FScriptDelegate per target              // 每个监听者都是原生 delegate slot
├─ AddUnique(InnerDelegate)                           // 直接加入 UE delegate list
└─ optional payload struct -> Object + FunctionName   // payload 也由值对象自己持有
```

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Delegate/FMulticastDelegateHelper.cpp:19-39`、`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Delegate/MulticastDelegateHandler.cpp:27-66,75-152`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Delegate/FMulticastDelegateHelper.cpp
// 位置: 19-39，helper 只负责创建/销毁 rooted handler
// ============================================================================
void FMulticastDelegateHelper::Initialize(FMulticastScriptDelegate* InMulticastDelegate, UFunction* InSignatureFunction)
{
    MulticastDelegateHandler = NewObject<UMulticastDelegateHandler>();

    MulticastDelegateHandler->AddToRoot(); // ★ 回调宿主被强引用，生命周期独立于调用者栈帧

    MulticastDelegateHandler->Initialize(InMulticastDelegate,
                                         InSignatureFunction != nullptr
                                             ? InSignatureFunction
                                             : MulticastDelegateHandler->GetCallBack());
}

void FMulticastDelegateHelper::Deinitialize()
{
    if (MulticastDelegateHandler != nullptr)
    {
        MulticastDelegateHandler->Deinitialize();
        MulticastDelegateHandler->RemoveFromRoot(); // ★ teardown 也集中在这层
        MulticastDelegateHandler = nullptr;
    }
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Delegate/MulticastDelegateHandler.cpp
// 位置: 27-66, 75-152，多播真正绑定到 handler，自定义扇出存在 DelegateWrappers
// ============================================================================
void UMulticastDelegateHandler::Initialize(FMulticastScriptDelegate* InMulticastScriptDelegate,
                                           UFunction* InSignatureFunction)
{
    bNeedFree = InMulticastScriptDelegate == nullptr;
    MulticastScriptDelegate = InMulticastScriptDelegate != nullptr
                                  ? InMulticastScriptDelegate
                                  : new FMulticastScriptDelegate();

    DelegateDescriptor = new FCSharpDelegateDescriptor(InSignatureFunction);
}

void UMulticastDelegateHandler::Add(UObject* InObject, FMethodReflection* InMethod)
{
    if (MulticastScriptDelegate != nullptr)
    {
        if (!MulticastScriptDelegate->Contains(ScriptDelegate))
        {
            ScriptDelegate.Unbind();
            ScriptDelegate.BindUFunction(this, *FUNCTION_CSHARP_CALLBACK);
            MulticastScriptDelegate->Add(ScriptDelegate);
            // ★ UE delegate list 里只挂入一个指向 handler 的 callback
        }
    }

    DelegateWrappers.Add({InObject, InMethod}); // ★ managed 侧多监听账本留在额外 map
}

void UMulticastDelegateHandler::ProcessEvent(UFunction* Function, void* Parms)
{
    if (Function != nullptr && Function->GetName() == FUNCTION_CSHARP_CALLBACK)
    {
        if (DelegateDescriptor != nullptr)
        {
            for (const auto& [Key, Value] : DelegateWrappers)
            {
                DelegateDescriptor->CallDelegate(Key.Get(), Value, Parms); // ★ 回调时再扇出
            }
        }
    }
    else
    {
        UObject::ProcessEvent(Function, Parms);
    }
}

void UMulticastDelegateHandler::Remove(UObject* InObject, FMethodReflection* InMethod)
{
    DelegateWrappers.Remove({InObject, InMethod});

    if (DelegateWrappers.IsEmpty())
    {
        if (MulticastScriptDelegate != nullptr)
        {
            MulticastScriptDelegate->RemoveAll(this);
            ScriptDelegate.Unbind(); // ★ 最后一个监听者移除时，才把 handler 从 UE delegate list 撤下
        }
    }
}
```

关键源码 [2] `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FCSharpDelegateDescriptor.cpp:11-46`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.cpp:1052-1099`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FAngelscriptDelegateWithPayload.cpp:35-83`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FCSharpDelegateDescriptor.cpp
// 位置: 11-46，handler 里的每个 wrapper 最终都要再做一次托管参数编组
// ============================================================================
bool FCSharpDelegateDescriptor::CallDelegate(const UObject* InObject, const FMethodReflection* InMethod, void* InParams)
{
    const auto CSharpParams = FReflectionRegistry::Get().GetObjectClass()->NewArray(PropertyDescriptors.Num());

    for (auto Index = 0; Index < PropertyDescriptors.Num(); ++Index)
    {
        if (const auto PropertyDescriptor = PropertyDescriptors[Index])
        {
            void* Object = nullptr;
            PropertyDescriptor->Get<std::false_type>(PropertyDescriptor->ContainerPtrToValuePtr<void>(InParams),
                                                     &Object);
            FDomain::Array_Set(CSharpParams, Index, static_cast<MonoObject*>(Object));
        }
    }

    if (const auto ReturnValue = InMethod->Runtime_Invoke_Array(
            FCSharpEnvironment::GetEnvironment().GetObject(InObject), CSharpParams);
        ReturnValue != nullptr && ReturnPropertyDescriptor != nullptr)
    {
        ...
    }

    return true;
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.cpp
// 位置: 1052-1099，多播绑定直接把每个目标 materialize 成 FScriptDelegate slot
// ============================================================================
FDelegateOps* Ops = (FDelegateOps*)Function->userData;
if (!CheckAngelscriptDelegateCompatibility(Ops->SignatureFunction, CallFunction))
{
    ...
    return;
}

FScriptDelegate InnerDelegate;
InnerDelegate.BindUFunction(InObject, InFunctionName);

Delegate->AddUnique(InnerDelegate); // ★ 监听关系直接存在 UE 原生 delegate list
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FAngelscriptDelegateWithPayload.cpp
// 位置: 35-83，payload 版本也不创建额外 handler UObject，而是值对象自持状态
// ============================================================================
bool FAngelscriptDelegateWithPayload::IsBound() const
{
    return Object.IsValid() && !FunctionName.IsNone();
}

void FAngelscriptDelegateWithPayload::ExecuteIfBound() const
{
    if (!Object.IsValid() || FunctionName.IsNone())
    {
        return;
    }

    UFunction* Function = Object->FindFunction(FunctionName);
    if (Function == nullptr)
    {
        return;
    }

    Object->ProcessEvent(Function, Payload.IsValid() ? (void*)Payload.GetMemory() : nullptr);
    // ★ payload 与目标函数名都留在值对象内部，不借助额外 callback carrier
}

void FAngelscriptDelegateWithPayload::BindUFunction(UObject* InObject, FName InFunctionName)
{
    ...
    Payload.Reset();
    Object = InObject;
    FunctionName = InFunctionName;
}
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 多播账本归属 | UnrealCSharp 把“谁该被回调”保存在 `DelegateWrappers`，UE 侧只看到一个 handler callback；Angelscript 把每个监听者直接落成 `FScriptDelegate` slot | 实现方式不同 |
| 清理触发点 | UnrealCSharp 只有在 `DelegateWrappers` 变空时才从原生多播委托里 `RemoveAll(this)`；Angelscript 没有等价的中间宿主，需要直接管理真实 delegate slot | 实现方式不同 |
| payload 所有权 | Angelscript 的 payload delegate 由值对象自持 `Object/FunctionName/Payload`；本轮未在 UnrealCSharp 定位到等价“值对象自持 payload 回调”的公共封装 | 没有实现（限于对等 payload delegate value 这一子能力） |

### [维度 D6] 条件编译合同：generated `UE_X_Y_OR_LATER` symbols vs runtime preprocessor flags

前面的 D6 已经分析过 Analyzer、Weaver 和 `.gen.cs`。这一轮只补一个更偏“工程合同”的点：**版本条件与上下文条件是在 IDE/编译器里显式建模，还是在脚本编译前动态注入**。UnrealCSharp 的做法是把这件事做成 `sln/csproj/props` 图谱的一部分。`FSolutionGenerator::ReplaceDefineConstants()` 会按当前引擎版本生成 `UE_5_0_OR_LATER ... UE_<Major>_<Minor>_OR_LATER`，并在非 cook 场景追加 `WITH_EDITOR`；`UE.csproj` 固定 import `Shared.props`，`Game.csproj` 则由 `ReplaceImport()` 改写为 import `<GameName>.props`，再由 `ReplaceProjectReference()` 指向生成好的 `UE.csproj`。结果是：**Roslyn、Analyzer、Weaver 和 IDE 编译诊断看到的是同一套版本符号**。

Angelscript 把类似信息留在脚本预处理器里。`FAngelscriptPreprocessor` 启动时动态填充 `EDITOR / EDITORONLY_DATA / COOK_COMMANDLET / RELEASE / TEST / WITH_SERVER_CODE`，再依据 “simulate cooked / force preprocess editor code” 重写这些值。这样做的优点是脚本编译上下文与运行时状态贴得很近，但这些条件不是以工程符号的形式暴露给外部 IDE/语言服务，而是只在 Angelscript 自己的 preprocess 阶段生效。

```
[D6-Deep] Conditional Compilation Contract

[UnrealCSharp]
FSolutionGenerator
├─ ReplaceDefineConstants()                 // 写入 UE_X_Y_OR_LATER / WITH_EDITOR
├─ ReplaceImport()                          // Game.csproj -> import Game.props
├─ ReplaceProjectReference()                // Game -> UE project reference
└─ Shared.props imported by UE/Game         // Roslyn/IDE 直接看到相同符号集

[Angelscript]
FAngelscriptPreprocessor
├─ Add EDITOR / EDITORONLY_DATA
├─ Add COOK_COMMANDLET / RELEASE / TEST
├─ Add WITH_SERVER_CODE
└─ override flags for simulated cooked/editor
   // 条件只在脚本预处理阶段可见
```

关键源码 [1] `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FSolutionGenerator.cpp:166-176,179-208,232-240`、`Reference/UnrealCSharp/Template/UE.csproj:1-8`、`Reference/UnrealCSharp/Template/Shared.props:1-14`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FSolutionGenerator.cpp
// 位置: 166-176, 179-208, 232-240，条件编译符号和项目引用被写进生成产物
// ============================================================================
void FSolutionGenerator::ReplaceImport(FString& OutResult)
{
    OutResult = OutResult.Replace(TEXT("<Import Project=\"\" Condition=\"Exists('')\" />"),
                                  *FString::Printf(TEXT(
                                      "<Import Project=\"%s%s\" Condition=\"Exists('%s%s')\" />"
                                  ),
                                                   *FUnrealCSharpFunctionLibrary::GetGameName(),
                                                   *PROPS_SUFFIX,
                                                   *FUnrealCSharpFunctionLibrary::GetGameName(),
                                                   *PROPS_SUFFIX));
    // ★ Game project 不再是裸模板，而是显式导入生成后的 Game.props
}

void FSolutionGenerator::ReplaceDefineConstants(FString& OutResult)
{
    FString DefineConstants;

    for (auto MajorVersion = 5; MajorVersion <= ENGINE_MAJOR_VERSION; ++MajorVersion)
    {
        for (auto MinorVersion = 0; MinorVersion <= ENGINE_MINOR_VERSION; ++MinorVersion)
        {
            DefineConstants += FString::Printf(TEXT("UE_%d_%d_OR_LATER;"), MajorVersion, MinorVersion);
        }
    }

    if (!IsRunningCookCommandlet())
    {
        DefineConstants += TEXT("WITH_EDITOR");
    }

    DefineConstants = FString::Printf(TEXT("<DefineConstants>$(DefineConstants);%s</DefineConstants>"),
                                      *DefineConstants);
    OutResult = OutResult.Replace(TEXT("<DefineConstants></DefineConstants>"), *DefineConstants);
    // ★ 版本/上下文条件被提升为 MSBuild / Roslyn 层的正式 symbol contract
}

void FSolutionGenerator::ReplaceProjectReference(FString& OutResult)
{
    OutResult = OutResult.Replace(TEXT("<ProjectReference Include=\"\" />"),
                                  *FString::Printf(TEXT("<ProjectReference Include=\"..\\%s\\%s%s\" />"),
                                                   *FUnrealCSharpFunctionLibrary::GetUEName(),
                                                   *FUnrealCSharpFunctionLibrary::GetUEName(),
                                                   *PROJECT_SUFFIX));
    // ★ Game 项目显式引用 UE 项目，版本符号沿项目图传播
}
```

```xml
<!-- =========================================================================
     文件: Reference/UnrealCSharp/Template/UE.csproj
     位置: 1-8，UE 工程显式导入 Shared.props
     ====================================================================== -->
<Project Sdk="Microsoft.NET.Sdk">
  <Import Project="../Shared.props" Condition="Exists('../Shared.props')" />
  <PropertyGroup>
    <TargetFramework></TargetFramework>
  </PropertyGroup>
  <ItemGroup>
    <Compile Include="..\..\Plugins\UnrealCSharp\Script\UE\**" Exclude="..\..\Plugins\UnrealCSharp\Script\UE\**\*.DS_Store"></Compile>
  </ItemGroup>
</Project>

<!-- =========================================================================
     文件: Reference/UnrealCSharp/Template/Shared.props
     位置: 1-14，DefineConstants 是专门留给生成器回填的占位
     ====================================================================== -->
<Project>
  <PropertyGroup>
    <AllowUnsafeBlocks>True</AllowUnsafeBlocks>
    <RootNamespace>Script</RootNamespace>
    <GenerateDependencyFile>false</GenerateDependencyFile>
    <DefineConstants></DefineConstants>
  </PropertyGroup>
  ...
</Project>
```

关键源码 [2] `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:38-72`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp
// 位置: 38-72，脚本编译条件由 runtime/preprocess 上下文动态注入
// ============================================================================
FAngelscriptPreprocessor::FAngelscriptPreprocessor()
{
    PreprocessorFlags.Add(TEXT("EDITOR"), FAngelscriptEngine::ShouldUseEditorScriptsForCurrentContext());
    PreprocessorFlags.Add(TEXT("EDITORONLY_DATA"),
                          WITH_EDITORONLY_DATA &&
                          ((!IsRunningGame() && !IsRunningDedicatedServer()) ||
                           FAngelscriptEngine::ShouldUseEditorScriptsForCurrentContext()));
    PreprocessorFlags.Add(TEXT("COOK_COMMANDLET"), IsRunningCookCommandlet());
    PreprocessorFlags.Add(TEXT("RELEASE"), UE_BUILD_SHIPPING || UE_BUILD_TEST);
    PreprocessorFlags.Add(TEXT("TEST"), !UE_BUILD_SHIPPING);
    PreprocessorFlags.Add(TEXT("WITH_SERVER_CODE"), WITH_SERVER_CODE);

    ...

#if WITH_EDITOR
    if (FAngelscriptEngine::IsSimulatingCookedForCurrentContext())
    {
        PreprocessorFlags.Add(TEXT("EDITOR"), false);
        PreprocessorFlags.Add(TEXT("EDITORONLY_DATA"), false);
        PreprocessorFlags.Add(TEXT("RELEASE"), true);
        PreprocessorFlags.Add(TEXT("TEST"), false);
    }

    if (FAngelscriptEngine::IsForcingPreprocessEditorCodeForCurrentContext())
    {
        PreprocessorFlags.Add(TEXT("EDITOR"), true);
        PreprocessorFlags.Add(TEXT("EDITORONLY_DATA"), true);
    }
#endif
    // ★ 条件是“脚本预处理期的动态上下文”，不是 IDE 工程层的静态 symbol 集
}
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 版本符号落点 | UnrealCSharp 把 `UE_X_Y_OR_LATER` 写进 `Shared.props`，由 `csproj` 导入后直接进入 Roslyn/MSBuild；Angelscript 把条件保留在脚本预处理器里 | 实现方式不同 |
| IDE 可见性 | UnrealCSharp 的版本/上下文条件对 Analyzer、Weaver、IDE 编译诊断天然可见；本轮未定位到 Angelscript 对等的外部工程符号出口 | 没有实现（限于“项目级条件编译符号出口”这一子能力） |
| 上下文精度 | Angelscript 的 `simulate cooked / force preprocess editor code` 能按当前运行上下文重写 flags；这不是能力缺失，而是比 UnrealCSharp 更偏 runtime-driven 的条件模型 | 实现方式不同 |

### [维度 D8] thunk 使用边界：host tick escape hatch vs VM-wide JIT compiler ownership

前面的 D1 已经写过 `SynchronizationContext.Tick` 通过 unmanaged thunk 接入 UE tick。这一轮不重复“有无 tick bridge”，只看 **unmanaged thunk 到底被用在多大范围**。源码显示 UnrealCSharp 的 thunk 是一个很窄的 escape hatch：`FDomain::InitializeSynchronizationContext()` 只对 `SynchronizationContext.Tick` 取一次 `Method_Get_Unmanaged_Thunk()`，之后 `Tick()` 直接调函数指针；但普通方法调用仍然走 `FMethodReflection::Runtime_Invoke()` / `Runtime_Invoke_Array()`，底层就是 `mono_runtime_invoke()`。甚至在前一节看到的委托扇出场景里，`FCSharpDelegateDescriptor::CallDelegate()` 也还是用 `Runtime_Invoke_Array()` 去调用托管方法。换句话说，UnrealCSharp 优化的是 **宿主事件循环这一条窄入口**，不是把整个脚本执行面都改造成自有 JIT 调度面。

Angelscript 的优化范围更大。`FAngelscriptEngine::Initialize()` 在生成 precompiled data 时直接创建 `FAngelscriptStaticJIT` 并 `Engine->SetJITCompiler(StaticJIT)`；后续若命中缓存，还会校验 `PrecompiledDataGuid` 与编进二进制的 transpiled code 是否一致。真正执行脚本函数时，`ASClass.cpp` 里的 UObject entrypoint 不是调一个“通用 runtime invoke”，而是把参数按 `VMBehavior` 打包后直接调 `JitFunction(...)`。因此两边不是“一个有 JIT、一个没有”；真正的区别是 **UnrealCSharp 的 native escape hatch 只包住少数 host callback，而 Angelscript 拥有插件自有的 VM 级 JIT compiler 接缝**。

```
[D8-Deep] Scope of Native Escape Hatches

[UnrealCSharp]
FDomain::InitializeSynchronizationContext
├─ Get Tick method
├─ Method_Get_Unmanaged_Thunk()              // 只缓存宿主 tick 入口
└─ Tick() -> function pointer

general managed calls
├─ FMethodReflection::Runtime_Invoke()
├─ FCSharpDelegateDescriptor::CallDelegate()
└─ FMonoDomain::Runtime_Invoke() -> mono_runtime_invoke

[Angelscript]
FAngelscriptEngine::Initialize
├─ new FAngelscriptStaticJIT
├─ Engine->SetJITCompiler(StaticJIT)         // VM 级编译器接缝
├─ load/validate PrecompiledData + JIT DB
└─ UASFunction entrypoint -> JitFunction(...) // 脚本函数体直接进 native/JIT 路径
```

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharp/Public/Domain/FDomain.h:6-10,22-27,50-57`、`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Domain/FDomain.cpp:34-46,102-117`、`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Reflection/FMethodReflection.cpp:77-89`、`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp:249-269,416-419`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Public/Domain/FDomain.h
// 位置: 6-10, 22-27, 50-57，thunk 缓存只为 SynchronizationContext tick 预留
// ============================================================================
class UNREALCSHARP_API FDomain final : public FTickableGameObject
{
private:
    typedef void (*SynchronizationContextTickType)(float, MonoObject**);

public:
    virtual void Tick(float DeltaTime) override;
    virtual bool IsTickable() const override;
    virtual TStatId GetStatId() const override;

private:
    void InitializeSynchronizationContext();
    void DeinitializeSynchronizationContext();

private:
    SynchronizationContextTickType SynchronizationContextTick; // ★ 这里只缓存一个特定 host callback
};
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Domain/FDomain.cpp
// 位置: 34-46, 102-117，只有 tick 入口走 unmanaged thunk
// ============================================================================
void FDomain::Tick(const float DeltaTime)
{
    if (SynchronizationContextTick != nullptr)
    {
        MonoObject* Exception{};
        SynchronizationContextTick(DeltaTime, &Exception); // ★ 每帧直接调 thunk

        if (Exception != nullptr)
        {
            FMonoDomain::Unhandled_Exception(Exception);
        }
    }
}

void FDomain::InitializeSynchronizationContext()
{
    if (const auto SynchronizationContextClass = FReflectionRegistry::Get().GetClass(...))
    {
        if (const auto InitializeMethod = SynchronizationContextClass->GetMethod(...))
        {
            InitializeMethod->Runtime_Invoke(); // ★ 初始化仍是普通反射调用
        }

        if (const auto TickMethod = SynchronizationContextClass->GetMethod(...))
        {
            SynchronizationContextTick =
                (SynchronizationContextTickType)TickMethod->Method_Get_Unmanaged_Thunk();
        }
    }
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Reflection/FMethodReflection.cpp
// 位置: 77-89，通用方法调用仍然回到 Runtime_Invoke / Runtime_Invoke_Array
// ============================================================================
MonoObject* FMethodReflection::Runtime_Invoke(void* InMonoObject, void** InParams) const
{
    return FMonoDomain::Runtime_Invoke(Method, InMonoObject, InParams);
}

MonoObject* FMethodReflection::Runtime_Invoke_Array(void* InMonoObject, MonoArray* InParams) const
{
    return FMonoDomain::Runtime_Invoke_Array(Method, InMonoObject, InParams);
}

void* FMethodReflection::Method_Get_Unmanaged_Thunk() const
{
    return FMonoDomain::Method_Get_Unmanaged_Thunk(Method);
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp
// 位置: 249-269, 416-419，普通调用最终落到 mono_runtime_invoke
// ============================================================================
MonoObject* FMonoDomain::Runtime_Invoke(MonoMethod* InFunction, void* InMonoObject, void** InParams,
                                        MonoObject** InExc)
{
    return InFunction != nullptr ? mono_runtime_invoke(InFunction, InMonoObject, InParams, InExc) : nullptr;
}

void* FMonoDomain::Method_Get_Unmanaged_Thunk(MonoMethod* InMonoMethod)
{
    return mono_method_get_unmanaged_thunk(InMonoMethod);
}
```

关键源码 [2] `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FCSharpDelegateDescriptor.cpp:11-30`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1425-1447,1513-1601`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:181-307`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FCSharpDelegateDescriptor.cpp
// 位置: 11-30，委托扇出这种常见热路径也仍然走 Runtime_Invoke_Array
// ============================================================================
bool FCSharpDelegateDescriptor::CallDelegate(const UObject* InObject, const FMethodReflection* InMethod, void* InParams)
{
    const auto CSharpParams = FReflectionRegistry::Get().GetObjectClass()->NewArray(PropertyDescriptors.Num());
    ...
    if (const auto ReturnValue = InMethod->Runtime_Invoke_Array(
            FCSharpEnvironment::GetEnvironment().GetObject(InObject), CSharpParams);
        ReturnValue != nullptr && ReturnPropertyDescriptor != nullptr)
    {
        ...
    }
    return true;
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 1425-1447, 1513-1601，Angelscript 拥有插件自有的 VM 级 JIT compiler 接缝
// ============================================================================
bGeneratePrecompiledData = RuntimeConfig.bGeneratePrecompiledData;
bUsePrecompiledData = !bGeneratePrecompiledData && !RuntimeConfig.bIgnorePrecompiledData
    && !RuntimeConfig.bRunningCommandlet && !WITH_EDITOR && !bScriptDevelopmentMode;

if (bGeneratePrecompiledData)
{
    PrecompiledData = new FAngelscriptPrecompiledData(Engine);
    StaticJIT = new FAngelscriptStaticJIT();
    StaticJIT->PrecompiledData = PrecompiledData;
    Engine->SetEngineProperty(asEP_BUILD_WITHOUT_LINE_CUES, 1);
    Engine->SetJITCompiler(StaticJIT); // ★ JIT compiler 接在整个 AngelScript VM 上
}

if (bUsePrecompiledData)
{
    PrecompiledData = new FAngelscriptPrecompiledData(Engine);
    PrecompiledData->Load(Filename);

    if (!PrecompiledData->IsValidForCurrentBuild())
    {
        ...
    }
    else
    {
        const FStaticJITCompiledInfo* CompiledInfo = FStaticJITCompiledInfo::Get();
        if (CompiledInfo != nullptr && CompiledInfo->PrecompiledDataGuid != PrecompiledData->DataGuid)
        {
            FJITDatabase::Get().Clear(); // ★ JIT 产物与缓存要做 build-level 对齐校验
        }
    }
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp
// 位置: 181-307，脚本函数入口把参数打包后直接调用 JitFunction
// ============================================================================
uint8* ArgStack = (uint8*)FMemory_Alloca(ASFunction->ArgStackSize);
asDWORD* VMArgs = (asDWORD*)FMemory_Alloca(8 * ArgumentCount + 16);
asDWORD* VMArgStart = VMArgs;

for (int32 i = 0; i < ArgumentCount; ++i)
{
    auto& Arg = ASFunction->Arguments[i];
    switch (Arg.VMBehavior)
    {
        case UASFunction::EArgumentVMBehavior::FloatExtendedToDouble:
            ...
            break;
        case UASFunction::EArgumentVMBehavior::Reference:
            ...
            break;
        case UASFunction::EArgumentVMBehavior::Value8Byte:
            ...
            break;
    }
}

asQWORD OutValue = 0;
FAngelscriptGameThreadScopeWorldContext WorldContext(NewWorldContext);
(JitFunction)(Execution, VMArgStart, &OutValue); // ★ 函数体执行直接进入 JIT/native 路径
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| thunk 使用范围 | UnrealCSharp 的 unmanaged thunk 目前直证只覆盖 `SynchronizationContext.Tick` 这类窄 host callback；普通方法和 delegate 回调仍主要走 `mono_runtime_invoke` | 实现方式不同 |
| VM 级 JIT 接缝 | Angelscript 通过 `Engine->SetJITCompiler(StaticJIT)` 挂接插件自有 JIT compiler；本轮未见 UnrealCSharp 对等的“插件自有 VM 级 JIT 编译器”接缝 | 没有实现（限于该层级接缝） |
| 性能判断边界 | 结构上能证明“优化范围不同”，不能只凭这些源码断言 CLR 路径一定慢于 Angelscript StaticJIT | 实现质量差异不可直接下结论，需要基准测试 |

---

## 深化分析 (2026-04-09 06:59:41)

### [维度 D11] 装载接缝覆盖范围：primary assembly list vs dependency-only preload seam

前面的 D1 / D11 多次写到了 `AssemblyLoader`、`PublishDirectory` 和 collectible `AssemblyLoadContext`，但还有一个边界问题没有单独落结论：**`UAssemblyLoader` 到底控制整套交付物，还是只控制二级依赖解析**。源码显示它只控制后者。`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp:54-59` 先把 `GetFullAssemblyPublishPath()` 产出的 `UE/Game/CustomProjects` 根程序集清单直接传给 `FDomain`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp:560-567,628-724` 随后对这批显式路径直接做 `AssemblyLoadContext.LoadFromStream` 或 `LoadAssembly(BaseFilename, AssemblyPath, ...)`。`UAssemblyLoader::Load()` 则只在 `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp:755-757,490-507` 安装并触发的 `AssemblyPreloadHook()` 里，以“程序集名”而不是“显式路径列表”的方式参与。

这意味着 UnrealCSharp 的装载合同其实分成两层：第一层是**主交付物清单**，由 `GetFullAssemblyPublishPath()` 决定并直接灌进 domain；第二层是**运行时依赖缺失回调**，由 `mono_install_assembly_preload_hook` 把请求转交给 `UAssemblyLoader`。旧文档已经写过它“可配置”，但本轮的新发现是：这个可配置 seam 并不决定首批 root payload 的选择顺序，它只决定 root payload 运行起来以后，额外依赖从哪里取字节。

Angelscript 在这一点上更像“单层显式根文件集”。`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1466-1477` 直接按固定路径加载 `Binds.Cache` 与 `BindModules.Cache`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1521-1554` 再按 build variant 选择 `PrecompiledScript*.Cache` 并执行 `IsValidForCurrentBuild()` / `PrecompiledDataGuid` 校验。也就是说，Angelscript 的主交付物根路径虽然不如 UnrealCSharp 那样拆成“root list + dependency loader seam”两层，但它对 root payload 的构建兼容性封印更强。

```
[D11-Deep] Root Payload vs Dependency Loader

[UnrealCSharp]
FCSharpEnvironment::Initialize
├─ GetFullAssemblyPublishPath()                 // root DLL list
├─ FDomain::InitializeAssembly(list)
│  ├─ AssemblyLoadContext.LoadFromStream(path)  // editor root payload
│  └─ LoadAssembly(name, path, ...)             // non-editor root payload
└─ Mono asks missing dependency by name
   └─ UAssemblyLoader::Load(name)               // dependency-only seam

[Angelscript]
FAngelscriptEngine::Initialize
├─ Load Binds.Cache                             // root cache 1
├─ Load BindModules.Cache                       // root cache 2
└─ Load PrecompiledScript*.Cache                // root cache 3
   ├─ IsValidForCurrentBuild()
   └─ PrecompiledDataGuid check
```

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp:54-59`、`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp:1035-1049`、`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp:560-567,628-724,755-757`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp
// 函数: FCSharpEnvironment::Initialize
// 位置: root assembly list 在 environment 初始化阶段就被固定下来
// ============================================================================
void FCSharpEnvironment::Initialize()
{
    Domain = new FDomain({
        "",
        FUnrealCSharpFunctionLibrary::GetFullAssemblyPublishPath() // ★ 首批装载的是显式 DLL 列表
    });
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp
// 函数: FUnrealCSharpFunctionLibrary::GetFullAssemblyPublishPath / GetAssemblyPath
// 位置: 主交付物列表与依赖探测路径是两套不同数据
// ============================================================================
TArray<FString> FUnrealCSharpFunctionLibrary::GetFullAssemblyPublishPath()
{
    return TArrayBuilder<FString>().
           Add(GetFullUEPublishPath()).
           Add(GetFullGamePublishPath()).
           Append(GetFullCustomProjectsPublishPath()).
           Build(); // ★ root payload = UE/Game/CustomProjects 的显式 DLL 清单
}

TArray<FString> FUnrealCSharpFunctionLibrary::GetAssemblyPath()
{
    return TArrayBuilder<FString>().
           Add(FPaths::ProjectContentDir() / GetPublishDirectory()).
           Add(FMonoFunctionLibrary::GetNetDirectory()).
           Build(); // ★ dependency probe roots 只有 PublishDirectory + NetDirectory
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp
// 函数: FMonoDomain::InitializeAssembly / LoadAssembly / RegisterAssemblyPreloadHook
// 位置: 首批 assembly 直接按路径装载，preload hook 只接二级依赖
// ============================================================================
void FMonoDomain::InitializeAssembly(const TArray<FString>& InAssemblies)
{
#if WITH_EDITOR
    InitializeAssemblyLoadContext();
#endif
    LoadAssembly(InAssemblies); // ★ 先消费显式 root assembly list
}

const auto AlcLoadFromStreamMethod = Class_Get_Method_From_Name(AssemblyLoadContextClass, "LoadFromStream", 1);
for (const auto& AssemblyPath : InAssemblies)
{
    ...
    Params[0] = BaseStream;
    const auto Result = Runtime_Invoke(AlcLoadFromStreamMethod, AssemblyLoadContextObject, Params);
    // ★ editor 下 root payload 直接按文件路径流式装进 ALC
}

LoadAssembly(FPaths::GetBaseFilename(AssemblyPath), AssemblyPath, &Image, &Assembly);
// ★ non-editor 下同样直接按显式路径装载，不经过 UAssemblyLoader

void FMonoDomain::RegisterAssemblyPreloadHook()
{
    mono_install_assembly_preload_hook(AssemblyPreloadHook, nullptr); // ★ 这里才把 loader seam 接给 Mono 依赖解析
}
```

关键源码 [2] `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/AssemblyLoader.cpp:6-23`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1466-1477,1521-1554`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/AssemblyLoader.cpp
// 函数: UAssemblyLoader::Load
// 位置: 依赖解析 seam 只接收 assembly name，不接收 root payload list
// ============================================================================
TArray<uint8> UAssemblyLoader::Load(const FString& InAssemblyName)
{
    auto AssemblyPaths = FUnrealCSharpFunctionLibrary::GetAssemblyPath();

    for (const auto& AssemblyPath : AssemblyPaths)
    {
        if (const auto File = FPaths::Combine(AssemblyPath, InAssemblyName) + DLL_SUFFIX;
            IFileManager::Get().FileExists(*File))
        {
            TArray<uint8> Data;
            FFileHelper::LoadFileToArray(Data, *File);
            return Data; // ★ 这里只按“缺哪个名字”补字节，不决定第一批 root DLL 是谁
        }
    }

    return {};
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 1466-1477, 1521-1554，Angelscript 直接按显式缓存根加载并校验
// ============================================================================
FAngelscriptBindDatabase::Get().Load(GetScriptRootDirectory() / TEXT("Binds.Cache"), bGeneratePrecompiledData);

if (plugin)
{
    FAngelscriptBinds::LoadBindModules(plugin->GetBaseDir() / "BindModules.Cache");
}

Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript_Development.Cache");
...
PrecompiledData->Load(Filename);

if (!PrecompiledData->IsValidForCurrentBuild())
{
    ...
}
else
{
    const FStaticJITCompiledInfo* CompiledInfo = FStaticJITCompiledInfo::Get();
    if (CompiledInfo != nullptr && CompiledInfo->PrecompiledDataGuid != PrecompiledData->DataGuid)
    {
        ...
    }
}
// ★ Angelscript 没有“root payload 先显式装，再额外回调二级 loader”这层拆分；但 root cache 的 build seal 更强
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| `AssemblyLoader` 覆盖范围 | UnrealCSharp 的 `UAssemblyLoader` 只服务于 `AssemblyPreloadHook` 触发的二级依赖解析；首批 root DLL 由 `GetFullAssemblyPublishPath()` 直接决定 | 实现方式不同 |
| root payload 组织方式 | UnrealCSharp 把“主交付物清单”与“依赖字节提供者”拆成两层；Angelscript 把 `Binds.Cache / BindModules.Cache / PrecompiledScript*.Cache` 作为单层显式根文件集处理 | 实现方式不同 |
| root payload 兼容性封印 | 本轮可见 UnrealCSharp root DLL 装载路径没有对等于 `IsValidForCurrentBuild()` / `PrecompiledDataGuid` 的预装校验；Angelscript 对 precompiled root payload 有明确校验 | 实现质量差异，Angelscript 的 root payload seal 更显式 |

### [维度 D2] 容器中委托的自动绑定底座：recursive delegate materialization vs subtype capability graph

前面的 D2 已经讲过 override patch、`InternalCall` export plane 和默认参数。这一轮只看一个更能暴露绑定 substrate 差异的 case：**容器里嵌套 delegate 时，自动绑定到底落在哪一层**。UnrealCSharp 的答案是“生成期先物化 delegate 类型，运行期再复用通用 container helper”。`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:158-165,231-252` 显示，`FArrayProperty / FMapProperty / FSetProperty` 在拼出 `TArray<T> / TMap<K,V> / TSet<T>` 之前，会先对 inner/key/value 执行 `FDelegateGenerator::Generator()`；而 `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FDelegateGenerator.cpp:27-85,413-475` 会把命中的 `FDelegateProperty / FMulticastDelegateProperty` 直接生成成独立的 C# wrapper，并在 ctor/dtor 里执行注册与反注册。到了运行时，`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Domain/InternalCall/FRegisterMap.cpp:14-22` 和 `Reference/UnrealCSharp/Source/UnrealCSharp/Public/Registry/FContainerRegistry.inl:37-62` 再根据 `MonoReflectionType` 的 generic arguments 去绑定 `FMapHelper` / `FArrayHelper` / `FSetHelper`，也就是把“容器怎么操作”完全交给通用 helper + registry。

Angelscript 则把这件事收束在 `FAngelscriptTypeUsage` 的 subtype 图上。`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:349-448` 的 `FAngelscriptTypeUsage` 本身就带 `SubTypes`，`Bind_TMap.cpp:1161-1178` 与 `Bind_TSet.cpp:621-633` 会从 `FMapProperty/FSetProperty` 递归构造 key/value/element usage；随后 `Bind_TMap.cpp:110-167`、`Bind_TSet.cpp:91-129` 用同一组 `SubTypes` 同时回答 `CanCreateProperty / CanHashValue / CanCopy / MatchesProperty / CreateProperty`。delegate 这边只需要在 `Bind_Delegates.cpp:432-453,1021-1034,1331-1341` 中把 `FScriptDelegateType` / `FMulticastScriptDelegateType` 注册进类型系统，容器不需要再额外做一轮“生成可见 wrapper class”的工作。

这两套方案都不是“没有自动化”。真正的差异在于：UnrealCSharp 把自动化的主战场放在**source-visible wrapper 物化**，因此 IDE 侧能直接看见具体 delegate/container 类型；Angelscript 把自动化的主战场放在**runtime subtype capability graph**，因此容器约束、GC、属性创建和匹配规则都集中在 `FAngelscriptTypeUsage` + 各模板类型实现里。

```
[D2-Deep] Nested Delegate in Container

[UnrealCSharp]
FGeneratorCore::GetPropertyType(FMapProperty)
├─ Generator(KeyProp) / Generator(ValueProp)    // nested delegate discovered during codegen
├─ emit "TMap<K, V>" in generated C#
└─ runtime RegisterMap(reflectionType)
   ├─ GetGenericArgument(0/1)
   └─ FContainerRegistry keeps helper by GC handle

[Angelscript]
FAngelscriptTypeUsage::FromProperty(FMapProperty)
├─ SubTypes[0] = key usage
├─ SubTypes[1] = value usage
├─ Bind_TMap::CanCreateProperty / CanCopy / MatchesProperty
└─ CreateProperty() builds FMapProperty from subtype graph
```

关键源码 [1] `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:158-165,231-252`、`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FDelegateGenerator.cpp:27-85,413-475`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp
// 函数: FGeneratorCore::GetPropertyType
// 位置: 容器类型在生成期递归发现 inner/key/value 中的 delegate
// ============================================================================
if (const auto ArrayProperty = CastField<FArrayProperty>(Property))
{
    FDelegateGenerator::Generator(ArrayProperty->Inner); // ★ 先物化内层 delegate

    return FString::Printf(TEXT("TArray<%s>"), *GetPropertyType(ArrayProperty->Inner));
}

if (const auto MapProperty = CastField<FMapProperty>(Property))
{
    FDelegateGenerator::Generator(MapProperty->KeyProp);   // ★ key 里有 delegate 也在这里提前生成
    FDelegateGenerator::Generator(MapProperty->ValueProp); // ★ value 同理

    return FString::Printf(TEXT("TMap<%s, %s>"),
                           *GetPropertyType(MapProperty->KeyProp),
                           *GetPropertyType(MapProperty->ValueProp));
}

if (const auto SetProperty = CastField<FSetProperty>(Property))
{
    FDelegateGenerator::Generator(SetProperty->ElementProp);
    return FString::Printf(TEXT("TSet<%s>"), *GetPropertyType(SetProperty->ElementProp));
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FDelegateGenerator.cpp
// 函数: FDelegateGenerator::Generator(FDelegateProperty*) / Generator(FMulticastDelegateProperty*)
// 位置: delegate wrapper 是生成期的显式源码产物
// ============================================================================
void FDelegateGenerator::Generator(FDelegateProperty* InDelegateProperty)
{
    ...
    Delegate.Add({NameSpaceContent, ClassContent});

    const auto ConstructorContent = FString::Printf(TEXT(
        "\t\tpublic %s() => FDelegateImplementation.FDelegate_RegisterImplementation(this, GetType());\n"
    ), *ClassContent);

    const auto DestructorContent = FString::Printf(TEXT(
        "\n\t\t~%s() => FDelegateImplementation.FDelegate_UnRegisterImplementation(%s);\n\n"
    ), *ClassContent, *PROPERTY_GARBAGE_COLLECTION_HANDLE);
    // ★ 单播 delegate 生成独立 wrapper，并把注册/反注册写进产物
}

void FDelegateGenerator::Generator(FMulticastDelegateProperty* InMulticastDelegateProperty)
{
    ...
    Delegate.Add({NameSpaceContent, ClassContent});

    const auto ConstructorContent = FString::Printf(TEXT(
        "\t\tpublic %s() => FMulticastDelegateImplementation.FMulticastDelegate_RegisterImplementation(this, GetType());\n"
    ), *ClassContent);

    const auto DestructorContent = FString::Printf(TEXT(
        "\n\t\t~%s() => FMulticastDelegateImplementation.FMulticastDelegate_UnRegisterImplementation(%s);\n\n"
    ), *ClassContent, *PROPERTY_GARBAGE_COLLECTION_HANDLE);
    // ★ 多播 delegate 也是单独的源码 wrapper，而不是纯 runtime 类型标签
}
```

关键源码 [2] `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Domain/InternalCall/FRegisterMap.cpp:14-22`、`Reference/UnrealCSharp/Source/UnrealCSharp/Public/Registry/FContainerRegistry.inl:37-62`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:349-448`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp:110-167,1161-1178`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.cpp:432-453,1021-1034,1331-1341`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Domain/InternalCall/FRegisterMap.cpp
// 函数: FRegisterMap::RegisterImplementation
// 位置: runtime 容器操作通过 reflection generic arguments 绑定通用 helper
// ============================================================================
static void RegisterImplementation(MonoObject* InMonoObject, MonoReflectionType* InReflectionType)
{
    const auto Class = FReflectionRegistry::Get().GetClass(InReflectionType);

    FCSharpBind::Bind<FMapHelper>(Class,
                                  Class->GetGenericArgument(),
                                  Class->GetGenericArgument(1),
                                  InMonoObject);
    // ★ 运行时不再重新“生成”类型，只把 generic arguments 接到通用 map helper
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Public/Registry/FContainerRegistry.inl
// 位置: helper 与 GC handle / native address 的映射由统一 registry 持有
// ============================================================================
static auto AddReference(Class* InRegistry, const FGarbageCollectionHandle& InOwner,
                         const typename FContainerValueMapping::FAddressType InAddress,
                         typename FContainerValueMapping::ValueType InValue,
                         FClassReflection* InClass, MonoObject* InMonoObject)
{
    const auto GarbageCollectionHandle = FGarbageCollectionHandle::NewRef(InClass, InMonoObject, true);

    (InRegistry->*Address2GarbageCollectionHandle).Add(InAddress, GarbageCollectionHandle);
    (InRegistry->*GarbageCollectionHandle2Value).Add(GarbageCollectionHandle, InValue);

    return FCSharpEnvironment::GetEnvironment().AddReference(
        InOwner,
        new TContainerReference<std::remove_pointer_t<typename FContainerValueMapping::ValueType>>(
            GarbageCollectionHandle));
    // ★ 容器实例的生命周期账本由通用 registry 保存
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp
// 位置: 110-167, 1161-1178，容器能力直接由 subtype graph 驱动
// ============================================================================
bool CanCreateProperty(const FAngelscriptTypeUsage& Usage) const override
{
    if (Usage.SubTypes.Num() != 2)
        return false;
    return Usage.SubTypes[0].CanCreateProperty() && Usage.SubTypes[0].CanHashValue()
        && Usage.SubTypes[1].CanCreateProperty();
}

FProperty* CreateProperty(const FAngelscriptTypeUsage& Usage, const FPropertyParams& Params) const override
{
    auto* MapProp = new FMapProperty(Params.Outer, Params.PropertyName, RF_Public);
    MapProp->KeyProp = Usage.SubTypes[0].CreateProperty(...);
    MapProp->ValueProp = Usage.SubTypes[1].CreateProperty(...);
    MapProp->MapLayout = FScriptMap::GetScriptLayout(
        Usage.SubTypes[0].GetValueSize(),
        Usage.SubTypes[0].GetValueAlignment(),
        Usage.SubTypes[1].GetValueSize(),
        Usage.SubTypes[1].GetValueAlignment());
    return MapProp; // ★ 属性创建、layout 与能力判断都集中在 subtype graph
}

FAngelscriptType::RegisterTypeFinder([MapType](FProperty* Property, FAngelscriptTypeUsage& Usage) -> bool
{
    ...
    Usage.Type = MapType;
    Usage.SubTypes.Add(KeyType);
    Usage.SubTypes.Add(ValueType);
    return true; // ★ 从 FProperty 递归构出 key/value usage
});
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.cpp
// 位置: delegate 作为 runtime type 注册进统一类型系统
// ============================================================================
void DeclareDelegate(UDelegateFunction* Function)
{
    FString Decl = CreateAngelscriptNameForDelegate(Function);
    FAngelscriptType::Register(MakeShared<FScriptDelegateType>(Decl, Function));
    // ★ delegate 是类型系统的一部分，不需要额外生成 IDE 可见 wrapper 源文件
}

FAngelscriptType::Register(MakeShared<FMulticastScriptDelegateType>(Decl, Function));

auto DelegateInternal = MakeShared<FScriptDelegateType>();
FAngelscriptType::SetScriptDelegate(DelegateInternal);
FAngelscriptType::Register(DelegateInternal);
// ★ internal delegate / multicast delegate 也统一归入同一个 runtime type registry
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 容器内嵌 delegate 的发现时机 | UnrealCSharp 在 `GetPropertyType()` 递归到容器 inner/key/value 时就提前调用 `FDelegateGenerator`；Angelscript 在 `RegisterTypeFinder` / `FromProperty()` 阶段构 subtype graph | 实现方式不同 |
| 容器能力约束落点 | UnrealCSharp 的容器行为主要由通用 helper + registry 承担；Angelscript 把 `CanCreateProperty / CanHashValue / CanCopy / CreateProperty` 收束到模板类型实现 | 实现方式不同 |
| IDE 可见类型产物 | UnrealCSharp 会物化 source-visible delegate wrapper；Angelscript delegate 主要是 runtime type registry 成员，本轮未定位到对等的源码产物生成链 | 实现质量差异，UnrealCSharp 的 IDE 可见度更高；Angelscript 的能力约束更集中 |

### [维度 D3] 输入绑定合成函数的生命周期责任：rooted synthetic UFunction vs existing delegate slot reuse

前文已经确认 UnrealCSharp 的输入绑定会临时造 `UFunction`。这一轮继续往下看**这些合成函数的生命周期责任归谁持有**。`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Domain/InternalCall/FRegisterInputComponent.cpp:268-290` 和 `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Domain/InternalCall/FRegisterEnhancedInputComponent.cpp:80-98` 显示，binding 路径不仅 `NewObject<UFunction>`、`AddFunctionToFunctionMap()`、改写 `Children` 链，还会显式 `AddToRoot()`，然后立刻通过 `GetBind()->Bind(..., Function)` 把这个新 `UFunction` patch 回 C#。而托管侧 `Reference/UnrealCSharp/Script/UE/CoreUObject/InputComponent.cs:234-257` 与 `Reference/UnrealCSharp/Script/UE/CoreUObject/EnhancedInputComponent.cs:53-80` 在删除时做的是“移除 binding record + `RemoveFunction(name)`”；本轮在这条插件内路径上没有定位到对称的 `RemoveFromRoot()` / `MarkAsGarbage()`。

这里必须区分“源码能证什么、不能证什么”。能证的是：**插件层的创建路径显式 root，插件层的删除路径显式 remove function**。不能证的是：是否一定会泄漏，因为 `UClass::RemoveFunction()` 的引擎内部清理细节不在本轮证据范围内。因此本轮结论不是“这里有 bug”，而是“插件层对 synthetic `UFunction` 的退出合同没有像创建合同那样直接写在同一层代码里”。

Angelscript 在输入绑定上则明显更保守。`Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/InputComponentScriptMixinLibrary.h:20-30,65-73` 直接向 `UInputComponent` 填 `FInputActionBinding` / `FInputAxisBinding`；注释明确要求“specified function must be a UFUNCTION()”。`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UEnhancedInputComponent.cpp:31-44` 也是直接把 `Delegate.GetUObject()` 和 `Delegate.GetFunctionName()` 传给 `InputComponent.BindAction/BindDebugKey`。也就是说，Angelscript 的输入桥接不会再额外创造一份 rooted synthetic `UFunction`；它要求目标函数先成为 UE 反射面的一部分，再去占用现成 delegate slot。

```
[D3-Deep] Input Binding Lifetime Ownership

[UnrealCSharp]
C# BindAction / BindAxis
├─ update DynamicBindingObject record            // managed-side ledger
├─ native BindFunction()
│  ├─ NewObject<UFunction>(RF_Transient)
│  ├─ AddFunctionToFunctionMap + Children
│  ├─ AddToRoot()
│  └─ patch function back to C#
└─ RemoveAction()
   ├─ remove binding record
   └─ UClass::RemoveFunction(name)              // plugin path未见对称 unroot

[Angelscript]
BindAction / BindAxis / EnhancedInput BindAction
├─ require existing UFUNCTION target
├─ fill native binding record
└─ pass Delegate.GetUObject() + GetFunctionName()
   // no synthetic UFunction allocation in plugin path
```

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Domain/InternalCall/FRegisterInputComponent.cpp:268-290`、`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Domain/InternalCall/FRegisterEnhancedInputComponent.cpp:80-98`、`Reference/UnrealCSharp/Script/UE/CoreUObject/InputComponent.cs:234-257`、`Reference/UnrealCSharp/Script/UE/CoreUObject/EnhancedInputComponent.cs:53-80`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Domain/InternalCall/FRegisterInputComponent.cpp
// 位置: 268-290，输入绑定会把合成函数直接挂进类反射表并 root 住
// ============================================================================
const auto Function = NewObject<UFunction>(InClass, *InFunctionName, EObjectFlags::RF_Transient);

Function->FunctionFlags = FUNC_BlueprintEvent;
...
Function->Bind();
Function->StaticLink(true);

InClass->AddFunctionToFunctionMap(Function, *InFunctionName);
Function->Next = InClass->Children;
InClass->Children = Function;

Function->AddToRoot(); // ★ 创建路径显式 root synthetic UFunction

FCSharpEnvironment::GetEnvironment().GetBind()->Bind(..., InClass, Function);
// ★ 随后立刻把这个新 UFunction patch 回托管方法
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Domain/InternalCall/FRegisterEnhancedInputComponent.cpp
// 位置: 80-98，EnhancedInput 路径同样 root 新函数
// ============================================================================
const auto Function = NewObject<UFunction>(InClass, *InFunctionName, EObjectFlags::RF_Transient);
...
InClass->AddFunctionToFunctionMap(Function, *InFunctionName);
Function->Next = InClass->Children;
InClass->Children = Function;

Function->AddToRoot(); // ★ 增强输入也沿用同一套 rooted synthetic function 策略

FCSharpEnvironment::GetEnvironment().GetBind()->Bind(..., FoundClass, Function);
```

```csharp
// ============================================================================
// 文件: Reference/UnrealCSharp/Script/UE/CoreUObject/InputComponent.cs
// 位置: 234-257，托管侧删除路径只显式移除 binding record 和 UClass function
// ============================================================================
public void RemoveAction(FName InActionName, EInputEvent InKeyEvent, UObject InObject, Action<FKey> InAction)
{
    ...
    InObject.GetClass().RemoveFunction(InAction.Method.Name);
    // ★ 本轮在插件路径上未定位到与 AddToRoot 对称的 RemoveFromRoot
}
```

```csharp
// ============================================================================
// 文件: Reference/UnrealCSharp/Script/UE/CoreUObject/EnhancedInputComponent.cs
// 位置: 53-80，EnhancedInput 删除路径同样以 RemoveBinding + RemoveFunction 收口
// ============================================================================
public void RemoveAction(UObject InObject, FEnhancedInputActionEventBinding InEnhancedInputActionEventBinding,
    Action<FInputActionValue, float, float, UInputAction> InAction)
{
    ...
    UEnhancedInputComponentImplementation.UEnhancedInputComponent_RemoveBindingImplementation(
        GarbageCollectionHandle,
        InEnhancedInputActionEventBinding.GarbageCollectionHandle);

    InObject.GetClass().RemoveFunction(InAction.Method.Name);
    // ★ 插件层 teardown 合同止步于此
}
```

关键源码 [2] `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/InputComponentScriptMixinLibrary.h:20-30,65-73`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UEnhancedInputComponent.cpp:31-44`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/InputComponentScriptMixinLibrary.h
// 位置: 20-30, 65-73，直接复用 UE 现成 binding record，不额外造函数
// ============================================================================
/**
 * Bind a function to be called when a key bound to this action triggers a specific keyevent.
 * Specified function must be a UFUNCTION() and takes a single FKey as its argument.
 */
UFUNCTION(BlueprintCallable)
static void BindAction(UInputComponent* Component, const FName& ActionName, EInputEvent KeyEvent, const FInputActionHandlerDynamicSignature& Delegate)
{
    FInputActionBinding AB(ActionName, KeyEvent);
    AB.ActionDelegate = Delegate;
    Component->AddActionBinding(AB); // ★ 直接写入原生 binding record
}

UFUNCTION(BlueprintCallable)
static void BindAxis(UInputComponent* Component, const FName& AxisName, const FInputAxisHandlerDynamicSignature& Delegate)
{
    FInputAxisBinding AB(AxisName);
    *(TInputUnifiedDelegate<FInputAxisHandlerSignature, FInputAxisHandlerDynamicSignature>*)&AB.AxisDelegate =
        TInputUnifiedDelegate<FInputAxisHandlerSignature, FInputAxisHandlerDynamicSignature>(Delegate);
    Component->AxisBindings.Emplace(MoveTemp(AB)); // ★ 仍是复用现成 delegate slot
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UEnhancedInputComponent.cpp
// 位置: 31-44，增强输入也直接把现成 UObject + FunctionName 交给原生 API
// ============================================================================
UEnhancedInputComponent_.Method(
    "FEnhancedInputActionEventBinding& BindAction(const UInputAction Action, ETriggerEvent TriggerEvent, FEnhancedInputActionHandlerDynamicSignature Delegate)",
    [](UEnhancedInputComponent& InputComponent, const UInputAction* Action, ETriggerEvent TriggerEvent, FEnhancedInputActionHandlerDynamicSignature Delegate) -> FEnhancedInputActionEventBinding&
    {
        return InputComponent.BindAction(Action, TriggerEvent, Delegate.GetUObject(), Delegate.GetFunctionName());
        // ★ 不创建 synthetic UFunction，只消费已有反射入口
    });
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 输入绑定的反射面策略 | UnrealCSharp 在绑定时会创建并 root 一个 synthetic `UFunction`；Angelscript 直接复用已有 `UFUNCTION` 对应的 delegate slot | 实现方式不同 |
| 生命周期合同对称性 | UnrealCSharp 的插件层创建路径显式 `AddToRoot()`，删除路径显式 `RemoveFunction()`，但本轮未在同层代码里看到对称的 `RemoveFromRoot()`；Angelscript 不承担这类 synthetic function 生命周期 | 实现质量差异，UnrealCSharp 的 teardown 合同更分散 |
| 能力边界 | UnrealCSharp 的做法换来更强的运行时输入桥接灵活性；Angelscript 则以“目标函数必须先是 `UFUNCTION`”换取更简单的生命周期边界 | 实现方式不同 |

---

## 深化分析 (2026-04-09 07:12:30)

### [维度 D1] Mono 接入的真实所有权：`Mono` 模块依赖 + 双根运行时目录

前面的 D1 把 UnrealCSharp 简化成“`UnrealCSharpCore` 依赖 `Mono` 模块”。这一层没错，但还不够精确。继续追源码会发现，它不是把 CLR 完全交给外部黑箱模块，而是把接入合同拆成两层：**构建图里依赖 `Mono` 模块，运行时路径上仍由插件自己定义 Mono 目录布局**。`UnrealCSharpCore.build.cs` 负责把 `Mono` 放进模块依赖；`FMonoFunctionLibrary::GetMonoDirectory()` 则明确把 editor 态 Mono 根定位到 `Plugin/Source/ThirdParty/Mono`，non-editor 态定位到 `Project/Binaries/<Platform>/Mono`；`FMonoDomain::Initialize()` 再按这个路径拼 `lib/...` 并交给 `monovm_initialize()` / `mono_dllmap_insert()`。

这意味着 UnrealCSharp 的第三方运行时接入不是“像 Angelscript 一样把 VM 源码直接编进 Runtime”，也不是“纯外部 SDK 黑箱”。更准确的表述是：**它把 CLR 封装成独立 module，但 Mono 资产目录和平台装载路径仍是插件自己拥有的架构一部分**。对照 Angelscript，`AngelscriptRuntime.Build.cs` 直接把 `ThirdParty/angelscript/source` 加进 include path，VM 代码和 Runtime 模块在编译期就是同一棵树。

```
[D1-Deep] CLR Ownership Topology

[UnrealCSharp]
UnrealCSharpCore.build.cs
├─ PublicDependency: Mono
├─ Editor root  -> Plugin/Source/ThirdParty/Mono
├─ Runtime root -> Project/Binaries/<Platform>/Mono
└─ FMonoDomain::Initialize()
   ├─ GetLibDirectory()
   ├─ configure dll search / dllmap
   └─ choose AOT mode

[Angelscript]
AngelscriptRuntime.Build.cs
└─ include ModuleDirectory/ThirdParty/angelscript/source
   // VM source folded into runtime compile unit
```

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharpCore/UnrealCSharpCore.build.cs:38-45`、`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoFunctionLibrary.cpp:4-29`

```csharp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/UnrealCSharpCore.build.cs
// 位置: 38-45，构建图里显式依赖 Mono 模块
// ============================================================================
PublicDependencyModuleNames.AddRange(
    new string[]
    {
        "Core",
        "Projects",
        "Mono"
        // ★ CLR 先在模块图里被建模成正式依赖
    }
);
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoFunctionLibrary.cpp
// 函数: FMonoFunctionLibrary::GetMonoDirectory
// 位置: 4-29，Mono 根目录不是黑箱，而是插件自己定义的路径合同
// ============================================================================
FString FMonoFunctionLibrary::GetMonoDirectory()
{
#if WITH_EDITOR
    return FString::Printf(TEXT(
        "%s/Source/ThirdParty/Mono"),
                           *FUnrealCSharpFunctionLibrary::GetPluginDirectory()
    );
    // ★ Editor 直接从插件目录下的 ThirdParty/Mono 取运行时资产
#else
    return FString::Printf(TEXT(
        "%s/Binaries/%s/Mono"),
                           *FPaths::ProjectDir(),
#if PLATFORM_WINDOWS
                           TEXT("Win64")
#elif PLATFORM_ANDROID
                           TEXT("Android")
#elif PLATFORM_IOS
                           TEXT("IOS")
#elif PLATFORM_LINUX
                           TEXT("Linux")
#elif PLATFORM_MAC_X86
                           TEXT("macOS_x86_64")
#elif PLATFORM_MAC_ARM64
                           TEXT("Mac")
#endif
    );
    // ★ 打包后又切到项目 Binaries/<Platform>/Mono
#endif
}
```

关键源码 [2] `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp:75-90`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp
// 函数: FMonoDomain::Initialize
// 位置: 75-90，Mono 启动时继续消费上面的目录合同
// ============================================================================
#if DOTNET9
#if PLATFORM_WINDOWS
const auto LibDirectory = FMonoFunctionLibrary::GetLibDirectory();

const char* PropertyKeys[] = {
    "NATIVE_DLL_SEARCH_DIRECTORIES"
};
const char* PropertyValues[] = {
    TCHAR_TO_ANSI(*LibDirectory),
};

monovm_initialize(std::size(PropertyKeys), PropertyKeys, PropertyValues);
// ★ 这里不是靠系统 PATH 猜目录，而是按插件定义的 Mono/lib 路径显式喂给 VM
#endif
#endif

mono_jit_set_aot_mode(MONO_AOT_MODE_NONE);
```

关键源码 [3] `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs:20-22`

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs
// 位置: 20-22，AngelScript VM 直接以内嵌源码形态编进 Runtime
// ============================================================================
var AngelscriptThirdPartyPath = Path.Combine(ModuleDirectory, "ThirdParty", "angelscript");
PublicIncludePaths.Add(Path.Combine(AngelscriptThirdPartyPath, "source"));
PublicIncludePaths.Add(AngelscriptThirdPartyPath);
// ★ Runtime 直接感知 VM 源码布局
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 第三方运行时接入表述 | UnrealCSharp 不能再简化成“纯外部 Mono 黑箱”；源码直证它同时拥有 `Mono` 模块依赖和 `Source/ThirdParty/Mono -> Binaries/<Platform>/Mono` 的路径合同 | 实现方式不同 |
| 平台资产布局 | UnrealCSharp 在 editor 与 non-editor 之间切换 Mono 根目录；Angelscript 主要在编译期直接吞入 `ThirdParty/angelscript/source` | 实现方式不同 |
| Runtime 对第三方布局的感知粒度 | UnrealCSharp 的 `FMonoDomain` 仍需显式知道 `lib` 目录并配置 native search path；Angelscript Runtime 更直接依赖源码布局 | 实现方式不同 |

### [维度 D3] 输入绑定合同补完：参数签名现场生成 + 集中回滚寄存器

前一轮 D3 已经确认 UnrealCSharp 会在输入绑定时临时造 `UFunction`。本轮继续往下看，发现它做得比“造一个函数壳子”更深：**每一种输入绑定都会现场拼出自己的参数 schema**。`BindAction` 先给 synthetic `UFunction` 塞 `FStructProperty Key`；`BindAxis` 塞 `FFloatProperty AxisValue`；`BindTouch` 一次塞 `FStructProperty Location + FEnumProperty FingerIndex`；随后才 `Bind()` / `StaticLink()` / `AddToRoot()` 并交给 `FCSharpBind::Bind()`。也就是说，UnrealCSharp 的输入桥接不是单纯复用 UE 既有 delegate signature，而是自己在绑定点重新构造一份最小反射面。

更重要的是，上一轮在 callsite 层只看到了 `AddToRoot()`，没继续追 descriptor 持有者，所以只看到了“创建”，没看到“退出”。本轮补完后可以确认：synthetic `UFunction` 的 teardown 并不是缺失，而是**被集中下沉到了 `FCSharpFunctionRegister` 的析构**。`FCSharpBind::Bind()` 在把函数 hash 注册进 `ClassRegistry` 时，会连同一个 `FCSharpFunctionRegister` 一起存进去；`FCSharpFunctionDescriptor` 再以值成员方式持有这个 register；最终析构时统一 `RemoveFunctionFromFunctionMap()`、`RemoveFromRoot()` 或 `MarkAsGarbage()`。这说明 UnrealCSharp 的输入绑定生命周期合同是“callsite 负责造签名和接管，descriptor/register 层负责回滚”。

Angelscript 在同一问题上的边界明显更窄。`UInputComponentScriptMixinLibrary` 和 `Bind_UEnhancedInputComponent.cpp` 直接要求目标是现成 `UFUNCTION()`，然后把 `FInputActionHandlerDynamicSignature` / `FInputAxisHandlerDynamicSignature` 或 `Delegate.GetUObject()+GetFunctionName()` 交给 UE 原生绑定 API。它没有为每一种输入类别重新合成参数 property 图，也不需要额外的 rollback register。

```
[D3-Deep] Input Bridge Contract

[UnrealCSharp]
BindAction / BindTouch / BindVectorAxis
├─ BindFunction()
│  ├─ NewObject<UFunction>(RF_Transient)
│  ├─ add FProperty schema per input kind
│  ├─ AddToRoot()
│  └─ FCSharpBind::Bind(...)
│     └─ AddFunctionHash(FCSharpFunctionRegister)
└─ FCSharpFunctionRegister::~FCSharpFunctionRegister()
   ├─ RemoveFunctionFromFunctionMap()
   ├─ RemoveFromRoot() / MarkAsGarbage()
   └─ restore original flags/native func when needed

[Angelscript]
BindAction / BindAxis / EnhancedInput BindAction
├─ require existing dynamic signature / UFUNCTION
├─ fill FInput*Binding or call native BindAction
└─ no synthetic UFunction schema / no rollback register
```

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Domain/InternalCall/FRegisterInputComponent.cpp:77-91,109-117,188-219,255-290`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Domain/InternalCall/FRegisterInputComponent.cpp
// 位置: 77-91, 109-117, 188-219, 255-290
// 说明: 输入绑定不是只造 UFunction 名字，而是现场合成每种输入类型的参数 schema
// ============================================================================
BindFunction(InClass, InFunctionName, [](UFunction* InFunction)
{
    const auto Property = new FStructProperty(InFunction, TEXT("Key"), RF_Public | RF_Transient);
    Property->Struct = FKey::StaticStruct();
    Property->SetPropertyFlags(CPF_Parm);
    InFunction->AddCppProperty(Property);
    // ★ BindAction 现场补出 FKey 参数
});

BindFunction(InClass, InFunctionName, [](UFunction* InFunction)
{
    const auto Property = new FFloatProperty(InFunction, TEXT("AxisValue"), RF_Public | RF_Transient);
    Property->SetPropertyFlags(CPF_Parm);
    InFunction->AddCppProperty(Property);
    // ★ BindAxis 现场补出 float 参数
});

BindFunction(InClass, InFunctionName, [](UFunction* InFunction)
{
    const auto LocationProperty = new FStructProperty(InFunction, TEXT("Location"), RF_Public | RF_Transient);
    LocationProperty->Struct = TBaseStructure<FVector2D>().Get();
    LocationProperty->SetPropertyFlags(CPF_Parm);
    InFunction->AddCppProperty(LocationProperty);

    const auto FingerIndexProperty = new FEnumProperty(InFunction, TEXT("FingerIndex"), RF_Public | RF_Transient);
    FingerIndexProperty->SetEnum(StaticEnum<ETouchIndex::Type>());
    FingerIndexProperty->SetPropertyFlags(CPF_Parm);
    InFunction->AddCppProperty(FingerIndexProperty);
    // ★ BindTouch 甚至要现场拼两段参数 schema
});

const auto Function = NewObject<UFunction>(InClass, *InFunctionName, EObjectFlags::RF_Transient);
Function->FunctionFlags = FUNC_BlueprintEvent;
InProperty(Function);
Function->Bind();
Function->StaticLink(true);
InClass->AddFunctionToFunctionMap(Function, *InFunctionName);
Function->Next = InClass->Children;
InClass->Children = Function;
Function->AddToRoot();

FCSharpEnvironment::GetEnvironment().GetBind()->Bind(
    FCSharpEnvironment::GetEnvironment().GetRegistry<FClassRegistry>()->GetClassDescriptor(InClass),
    InClass,
    Function);
// ★ schema 合成、反射表挂载、root 和后续 bind patch 都发生在同一条输入桥接链
```

关键源码 [2] `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FCSharpBind.cpp:327-340,377-384`、`Reference/UnrealCSharp/Source/UnrealCSharp/Public/Reflection/Function/FCSharpFunctionDescriptor.h:12-25`、`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FCSharpFunctionRegister.cpp:29-66`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FCSharpBind.cpp
// 位置: 327-340, 377-384，bind patch 时把回滚寄存器一起挂进 function hash
// ============================================================================
const auto OverrideFunction = DuplicateFunction(OriginalFunction, InClass, *NewFunctionName);

FCSharpEnvironment::GetEnvironment().AddFunctionHash<FCSharpFunctionDescriptor>(
    FunctionHash, InClassDescriptor, OriginalFunction,
    FCSharpFunctionRegister(OriginalFunction, OverrideFunction,
                            OriginalFunction->FunctionFlags, OriginalFunction->GetNativeFunc()));
// ★ override 路径把“原函数 + 新函数 + 旧 native func”一起装进 register

NewFunction = DuplicateFunction(OriginalFunction, InClass, FunctionName);

FCSharpEnvironment::GetEnvironment().AddFunctionHash<FCSharpFunctionDescriptor>(
    FunctionHash, InClassDescriptor, NewFunction, FCSharpFunctionRegister(NewFunction, OriginalFunction));
// ★ 非 override synthetic function 也不是裸挂，仍然有 register 托管退出合同
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Public/Reflection/Function/FCSharpFunctionDescriptor.h
// 位置: 12-25，descriptor 以值成员持有 register
// ============================================================================
explicit FCSharpFunctionDescriptor(UFunction* InFunction, FCSharpFunctionRegister&& InFunctionRegister);

private:
    FCSharpFunctionRegister FunctionRegister;
    // ★ 不是旁路表，而是 function descriptor 自己拥有回滚器
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FCSharpFunctionRegister.cpp
// 位置: 29-66，回滚实际集中发生在析构里
// ============================================================================
FCSharpFunctionRegister::~FCSharpFunctionRegister()
{
    const auto InOriginalFunction = OriginalFunction.Get(true);
    const auto InCallCSharpFunction = Function.Get(true);

    if (InOriginalFunction != nullptr && InCallCSharpFunction != nullptr)
    {
        UFunction* FunctionRemove;

        if (InOriginalFunction->GetOuter() == InCallCSharpFunction->GetOuter())
        {
            InCallCSharpFunction->FunctionFlags = OriginalFunctionFlags;
            InCallCSharpFunction->SetNativeFunc(OriginalNativeFuncPtr);
            FunctionRemove = InOriginalFunction;
            // ★ override 情况先恢复原函数，再决定移除谁
        }
        else
        {
            FunctionRemove = InCallCSharpFunction;
        }

        if (const auto Class = Cast<UClass>(FunctionRemove->GetOuter()))
        {
            Class->RemoveFunctionFromFunctionMap(FunctionRemove);
        }

        if (FunctionRemove->IsRooted())
        {
            FunctionRemove->RemoveFromRoot();
        }
        else
        {
            FunctionRemove->MarkAsGarbage();
        }
        // ★ 上一轮在 callsite 层没追到的 teardown 实际在这里统一收口
    }
}
```

关键源码 [3] `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/InputComponentScriptMixinLibrary.h:19-29,60-73`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UEnhancedInputComponent.cpp:31-44`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/InputComponentScriptMixinLibrary.h
// 位置: 19-29, 60-73，直接要求现成 UFUNCTION + 动态签名
// ============================================================================
/**
 * Bind a function to be called when a key bound to this action triggers a specific keyevent.
 * Specified function must be a UFUNCTION() and takes a single FKey as its argument.
 */
UFUNCTION(BlueprintCallable)
static void BindAction(UInputComponent* Component, const FName& ActionName, EInputEvent KeyEvent,
    const FInputActionHandlerDynamicSignature& Delegate)
{
    FInputActionBinding AB(ActionName, KeyEvent);
    AB.ActionDelegate = Delegate;
    Component->AddActionBinding(AB);
}

UFUNCTION(BlueprintCallable)
static void BindAxis(UInputComponent* Component, const FName& AxisName,
    const FInputAxisHandlerDynamicSignature& Delegate)
{
    FInputAxisBinding AB(AxisName);
    *(TInputUnifiedDelegate<FInputAxisHandlerSignature, FInputAxisHandlerDynamicSignature>*)&AB.AxisDelegate =
        TInputUnifiedDelegate<FInputAxisHandlerSignature, FInputAxisHandlerDynamicSignature>(Delegate);
    Component->AxisBindings.Emplace(MoveTemp(AB));
    // ★ 直接复用 UE 已有动态签名，不自己拼参数 property
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UEnhancedInputComponent.cpp
// 位置: 31-44，增强输入也只是把 UObject/FunctionName 透传给 UE 原生绑定
// ============================================================================
UEnhancedInputComponent_.Method(
    "FEnhancedInputActionEventBinding& BindAction(const UInputAction Action, ETriggerEvent TriggerEvent, FEnhancedInputActionHandlerDynamicSignature Delegate)",
    [](UEnhancedInputComponent& InputComponent, const UInputAction* Action, ETriggerEvent TriggerEvent, FEnhancedInputActionHandlerDynamicSignature Delegate) -> FEnhancedInputActionEventBinding&
    {
        return InputComponent.BindAction(Action, TriggerEvent, Delegate.GetUObject(), Delegate.GetFunctionName());
    });
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 输入桥接的签名所有权 | UnrealCSharp 在绑定点现场合成 `FProperty` 参数图；Angelscript 直接复用 UE 现成动态签名 / `UFUNCTION` | 实现方式不同 |
| 上一轮生命周期观察的补正 | UnrealCSharp 的 teardown 不是没有实现，而是被 `FCSharpFunctionRegister` 集中持有并在析构时统一回滚 | 实现方式不同 |
| 复杂度落点 | UnrealCSharp 以更复杂的 synthetic reflection 面换运行时绑定灵活性；Angelscript 以预先存在的反射入口约束脚本侧自由度 | 实现方式不同 |

### [维度 D11] 交付闭包的真实边界：项目程序集载荷 + 插件自带 CLR 基座

前面的 D11 已经写过 `PublishDirectory`、`AssemblyLoader` 和 `PrecompiledScript.Cache`。这一轮把这几条线合起来看，能得到一个更精确的交付结论：**UnrealCSharp 的最终可运行闭包不是单一目录，而是至少两层载荷根共同组成**。第一层是项目自己发布出来的程序集，`Game.props` 在 `AfterBuild` 后直接执行 `Publish`，随后只把 `$(PublishDir)*.dll` 复制到 `Content/<PublishDirectory>`；`FUnrealCSharpEditorModule::UpdatePackagingSettings()` 又把这个 `PublishDirectory` 自动加进 `DirectoriesToAlwaysStageAsUFS`。第二层是 CLR 自己的基础库与 native runtime，运行时通过 `GetAssemblyPath()` 同时探测 `ProjectContent/<PublishDirectory>` 和 `FMonoFunctionLibrary::GetNetDirectory()`；而 `GetNetDirectory()` 又回到上一节说的 `Source/ThirdParty/Mono` / `Binaries/<Platform>/Mono`。

因此，UnrealCSharp 的交付闭包更像“**项目载荷 + 插件运行时基座**”的组合：前者由 project publish 流水线持续产出，后者由 Mono 目录合同提供。与之对照，Angelscript 在运行时装载的外部交付物主要集中在 `GetScriptRootDirectory()` 下的 `Binds.Cache`、`PrecompiledScript*.Cache` 和脚本本体；VM 本身已经在插件二进制内，所以运行时外部载荷根更少。

```
[D11-Deep] Delivery Closure Ownership

[UnrealCSharp]
MSBuild / Editor
├─ Publish -> copy *.dll to Content/<PublishDirectory>
├─ stage PublishDirectory as UFS
└─ Runtime probe roots
   ├─ ProjectContent/<PublishDirectory>   // UE/Game/CustomProjects DLLs
   └─ Mono/lib/.../net                    // framework/base assemblies

[Angelscript]
Runtime startup
├─ ScriptRoot/Binds.Cache
├─ ScriptRoot/PrecompiledScript*.Cache
├─ ScriptRoot/*.as
└─ plugin binary already contains VM / optional StaticJIT
```

关键源码 [1] `Reference/UnrealCSharp/Template/Game.props:16-27`、`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp:1005-1049`、`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:209-233`

```xml
<!-- ============================================================================
     文件: Reference/UnrealCSharp/Template/Game.props
     位置: 16-27，项目 publish 流水线只复制 DLL 到脚本输出目录
     ============================================================================ -->
<Target Name="AfterBuildPublish" AfterTargets="Build">
    <MSBuild Projects="$(ProjectPath)" Targets="Publish" Properties="Configuration=$(Configuration)" />
</Target>
<Target Name="CopyDllsAfterPublish" AfterTargets="Publish">
    <ItemGroup>
        <PublishFiles Include="$(PublishDir)*.dll" />
    </ItemGroup>
    <Copy SourceFiles="@(PublishFiles)" DestinationFolder="$(ScriptOutputPath)" />
    <!-- ★ 作者态项目只显式交付程序集 DLL -->
</Target>
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp
// 位置: 1005-1049，运行时 root payload 与 dependency probe roots 不是同一层
// ============================================================================
FString FUnrealCSharpFunctionLibrary::GetFullPublishDirectory()
{
    return FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / GetPublishDirectory());
}

TArray<FString> FUnrealCSharpFunctionLibrary::GetFullAssemblyPublishPath()
{
    return TArrayBuilder<FString>().
           Add(GetFullUEPublishPath()).
           Add(GetFullGamePublishPath()).
           Append(GetFullCustomProjectsPublishPath()).
           Build();
    // ★ root payload 是 UE/Game/CustomProjects 这批显式 DLL
}

TArray<FString> FUnrealCSharpFunctionLibrary::GetAssemblyPath()
{
    return TArrayBuilder<FString>().
           Add(FPaths::ProjectContentDir() / GetPublishDirectory()).
           Add(FMonoFunctionLibrary::GetNetDirectory()).
           Build();
    // ★ dependency probe roots 还要再加 Mono net 基础库目录
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp
// 位置: 209-233，编辑器自动 staging 的只有 PublishDirectory
// ============================================================================
void FUnrealCSharpEditorModule::UpdatePackagingSettings()
{
    const auto PublishDirectory = FUnrealCSharpFunctionLibrary::GetPublishDirectory();

    ...

    if (!bIsExisted)
    {
        ProjectPackagingSettings->DirectoriesToAlwaysStageAsUFS.Add({PublishDirectory});
        ProjectPackagingSettings->TryUpdateDefaultConfigFile();
        // ★ 当前源码直证的 packaging-settings hook 只覆盖项目程序集目录
    }
}
```

关键源码 [2] `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoFunctionLibrary.cpp:4-65`、`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/AssemblyLoader.cpp:6-23`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoFunctionLibrary.cpp
// 位置: 4-65，CLR 基础库与 native runtime 的目录由 Mono 路径合同提供
// ============================================================================
FString FMonoFunctionLibrary::GetMonoDirectory()
{
#if WITH_EDITOR
    return FString::Printf(TEXT("%s/Source/ThirdParty/Mono"), *FUnrealCSharpFunctionLibrary::GetPluginDirectory());
#else
    return FString::Printf(TEXT("%s/Binaries/%s/Mono"), *FPaths::ProjectDir(), TEXT("Win64"));
#endif
}

FString FMonoFunctionLibrary::GetLibDirectory()
{
    return FString::Printf(TEXT("%s/lib/%s/%s"), *GetMonoDirectory(), MONO_CONFIGURATION, TEXT("Win64"));
}

FString FMonoFunctionLibrary::GetNetDirectory()
{
    return FString::Printf(TEXT("%s/net"), *GetLibDirectory());
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/AssemblyLoader.cpp
// 函数: UAssemblyLoader::Load
// 位置: 6-23，依赖解析明确在两套根目录中探测 DLL
// ============================================================================
TArray<uint8> UAssemblyLoader::Load(const FString& InAssemblyName)
{
    auto AssemblyPaths = FUnrealCSharpFunctionLibrary::GetAssemblyPath();

    for (const auto& AssemblyPath : AssemblyPaths)
    {
        if (const auto File = FPaths::Combine(AssemblyPath, InAssemblyName) + DLL_SUFFIX;
            IFileManager::Get().FileExists(*File))
        {
            TArray<uint8> Data;
            FFileHelper::LoadFileToArray(Data, *File);
            return Data;
        }
    }

    return {};
    // ★ 项目程序集目录和 Mono net 基础库目录在同一条 dependency probe path 上
}
```

关键源码 [3] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1469-1477,1504-1529`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 1469-1477, 1504-1529，运行时外部交付物主要集中在 Script root
// ============================================================================
FAngelscriptBindDatabase::Get().Load(GetScriptRootDirectory() / TEXT("Binds.Cache"), bGeneratePrecompiledData);

if (plugin)
{
    FAngelscriptBinds::LoadBindModules(plugin->GetBaseDir() / "BindModules.Cache");
}

FAngelscriptBindDatabase::Get().Save(GetScriptRootDirectory() / TEXT("Binds.Cache"));

#if UE_BUILD_SHIPPING
Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript_Shipping.Cache");
#elif UE_BUILD_TEST
Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript_Test.Cache");
#elif UE_BUILD_DEVELOPMENT
Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript_Development.Cache");
#endif

if (!IFileManager::Get().FileExists(*Filename))
    Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript.Cache");
// ★ Script root 下的 cache 集合就是运行时主要外部载荷
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 交付闭包根目录 | UnrealCSharp 的运行时闭包至少横跨 `Content/<PublishDirectory>` 与 `Mono/lib/.../net` 两套根；Angelscript 的外部载荷更集中在 `Script root` | 实现方式不同 |
| 编辑器自动打包钩子覆盖范围 | UnrealCSharp 当前源码直证的自动 staging hook 只针对 `PublishDirectory`；Mono 基础库目录则通过独立路径合同参与运行时解析 | 实现方式不同 |
| 载荷审计面 | UnrealCSharp 需要同时审计项目程序集和 CLR 基座目录；Angelscript 更像“脚本缓存集 + 插件二进制 VM” | 实现方式不同 |

---

## 深化分析 (2026-04-09 07:22:44)

本轮不再复写前面已经多轮展开的 `D1 / D3 / D11`。新增三条之前还没有单独落成 contract 的链路：`D2` 看 binding helper 的身份与生命周期到底挂在哪一层，`D6` 看生成代码是否被明确拆成作者层与 ABI 层，`D8` 看跨语言调用时临时参数缓冲到底落在 managed 侧还是 native 侧。

### [维度 D2] Binding helper 身份平面：lazy descriptor DAG + owner-tied GC handle

前面的 D2 已经把 `override patch`、`InternalCall` 平面和默认参数写过了，但还有一个更底层的问题没单独拆开：**非 `UObject` 的 binding helper 在 C# 世界里到底靠什么维持身份**。继续追源码会发现，UnrealCSharp 这里不是“每次调一次 helper 就临时造一次 wrapper”，而是先用 `TBindingClassBuilder` 把 `FActorSpawnParameters` 这类 native helper 描述成 binding class，再由 `FBinding` 把 register 列表惰性固化成 `FBindingClass`，随后生成 C# partial wrapper 和 implementation stub，最后靠 `FBindingRegistry` 把 “native helper 地址 ↔ `FGarbageCollectionHandle` ↔ `MonoObject*`” 绑成一条长期存在的身份链。

更关键的是，这条身份链不是孤立对象池。`FBindingRegistry::AddReference(const FGarbageCollectionHandle& InOwner, ...)` 在为 helper 创建 `NewRef` 之后，还会向 `FReferenceRegistry` 挂一条 `FBindingReference`；而 `FBindingReference` 析构时又会反过来调用 `RemoveBindingReference()`。这说明 UnrealCSharp 的 helper 生命周期合同是“**owner 持有 helper，helper 反向登记自己的回收入口**”，不是单纯的地址缓存。

Angelscript 在这个问题上的落点明显不同。当前源码路径里，`FAngelscriptBinds` 只维护 `BindName + BindOrder + lambda` 的 replay 列表；真正执行时按顺序重放注册函数，把对象类型、方法、属性直接注册进 AngelScript engine。也就是说，Angelscript 的脚本侧类型身份主要落在 AngelScript 自己的 `type/function` 注册表里，而不是额外再铺一层 “native helper 地址 ↔ 脚本对象” 的通用 registry。

```
[D2-Deep] Helper Identity Plane

[UnrealCSharp]
TBindingClassBuilder / BINDING_CLASS
├─ FBinding::Register() dedupe by class name      // 先建 register 图，不立即发布
├─ FBindingClassRegister -> FBindingClass         // 惰性物化 descriptor DAG
├─ FBindingClassGenerator                         // 生成 partial wrapper + implementation stub
├─ FMonoDomain::RegisterBinding                   // mono_add_internal_call 发布 ABI
└─ FBindingRegistry
   ├─ address -> GC handle -> MonoObject
   └─ owner -> FBindingReference -> teardown

[Angelscript]
Bind_*.cpp / UHT shard
├─ FAngelscriptBinds::RegisterBinds               // 收集 lambda
└─ FAngelscriptBinds::CallBinds                   // 直接重放到 AngelScript engine
```

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Domain/InternalCall/FRegisterWorld.cpp:12-156`、`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Binding/FBinding.cpp:10-65`、`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Binding/Class/FBindingClassRegister.cpp:20-143`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Domain/InternalCall/FRegisterWorld.cpp
// 位置: 12-156，native helper 先被声明为 binding class，再在运行时按 handle 取回
// ============================================================================
BINDING_CLASS(FActorSpawnParameters)

struct FRegisterActorSpawnParameters
{
    FRegisterActorSpawnParameters()
    {
        TBindingClassBuilder<FActorSpawnParameters, false>(NAMESPACE_LIBRARY)
            .Property("Name", BINDING_PROPERTY(&FActorSpawnParameters::Name))
            .Property("Template", BINDING_PROPERTY(&FActorSpawnParameters::Template))
            .Function("GetbNoFail", GetbNoFailImplementation)
            .Function("SetbNoFail", SetbNoFailImplementation);
        // ★ helper 先进入 binding descriptor 世界，而不是临时反射对象
    }
};

static MonoObject* SpawnActorImplementation(..., const FGarbageCollectionHandle InActorSpawnParameters)
{
    const auto FoundActorSpawnParameters = FCSharpEnvironment::GetEnvironment().GetBinding<
        FActorSpawnParameters>(InActorSpawnParameters);

    const auto Actor = FoundWorld->SpawnActor<AActor>(FoundClass,
                                                      *FoundTransform,
                                                      FoundActorSpawnParameters != nullptr
                                                          ? *FoundActorSpawnParameters
                                                          : FActorSpawnParameters());
    // ★ 后续 native 调用不是重新解析 C# 对象，而是直接按 GC handle 找回 helper 地址
    return FCSharpEnvironment::GetEnvironment().Bind(Actor);
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Binding/FBinding.cpp
// 位置: 10-65，descriptor 先去重，再惰性物化
// ============================================================================
FBinding& FBinding::Register()
{
    if (!bIsRegister)
    {
        bIsRegister = true;

        for (const auto& Class : ClassRegisters)
        {
            Classes.Emplace(Class->operator FBindingClass*());
            delete Class;
        }

        ClassRegisters.Empty();
        // ★ register 列表只在真正发布时一次性转成 FBindingClass
    }

    return *this;
}

FBindingClassRegister*& FBinding::Register(...)
{
    if (!InIsReflectionClass)
    {
        const auto Class = InClassFunction();

        for (auto& ClassRegister : ClassRegisters)
        {
            if (!ClassRegister->IsReflectionClass() && Class == ClassRegister->GetClass())
            {
                return ClassRegister;
                // ★ 非 reflection binding class 先按类名去重
            }
        }
    }

    return ClassRegisters.Add_GetRef(new FBindingClassRegister(...));
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Binding/Class/FBindingClassRegister.cpp
// 位置: 20-143，register 不只记属性/函数，还记 implementation 名字与继承图
// ============================================================================
FBindingClassRegister::operator FBindingClass*() const
{
    ...
    for (const auto& Method : MethodRegisters)
    {
        Methods.Emplace(FBindingMethod(Method));
    }

    return new FBindingClass(BaseClassFunction.IsSet() ? BaseClassFunction.GetValue()() : FString(),
                             ClassFunction(),
                             ImplementationNameSpace,
                             IsProjectClassFunction(),
                             bIsReflectionClass,
                             FBindingTypeInfo(TypeInfoRegister),
                             FBindingSubscript(SubscriptRegister),
                             Properties,
                             Functions,
                             Methods);
    // ★ 这里把“类名 / implementation namespace / 继承 / type info / 方法地址”收敛成正式 descriptor
}

void FBindingClassRegister::BindingMethod(const FString& InImplementationName, const void* InFunction)
{
    MethodRegisters.Emplace([=]()
    {
        return BINDING_COMBINE_CLASS(ImplementationNameSpace,
                                     BINDING_COMBINE_CLASS_IMPLEMENTATION(GetClass())) +
               BINDING_COMBINE_FUNCTION(
                   BINDING_COMBINE_FUNCTION_IMPLEMENTATION(GetClass(), InImplementationName));
        // ★ C# 侧最终看到的 method 名字在 register 阶段就被拼出来
    }, InFunction);
}

void FBindingClassRegister::Inheritance(...)
{
    BaseClassFunction = InBaseClassFunction;
    FBinding::Get().Register(InBaseClassFunction, ...);
    // ★ 继承边不是生成期临时扫出来，而是在 register 图上显式声明
}
```

关键源码 [2] `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp:783-791`、`Reference/UnrealCSharp/Source/UnrealCSharp/Public/Registry/FBindingRegistry.inl:14-44`、`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FBindingRegistry.cpp:17-68`、`Reference/UnrealCSharp/Source/UnrealCSharp/Public/Reference/FBindingReference.h:6-15`、`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FReferenceRegistry.cpp:35-57`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp
// 位置: 783-791，descriptor 最终被发布成 Mono InternalCall 平面
// ============================================================================
void FMonoDomain::RegisterBinding()
{
    for (const auto& Class : FBinding::Get().Register().GetClasses())
    {
        for (const auto& Method : Class->GetMethods())
        {
            mono_add_internal_call(TCHAR_TO_ANSI(*Method.GetMethod()), Method.GetFunction());
            // ★ descriptor DAG 发布完成后，ABI 才真正进入 CLR
        }
    }
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Public/Registry/FBindingRegistry.inl
// 位置: 14-44；Reference/.../FBindingReference.h:6-15；
//       Reference/.../FReferenceRegistry.cpp:35-57
// 说明: helper 身份不只是一条 map，而是 owner-tied 生命周期合同
// ============================================================================
template <typename T, auto IsNeedFree>
auto FBindingRegistry::AddReference(const T* InObject, FClassReflection* InClass, MonoObject* InMonoObject)
{
    const auto GarbageCollectionHandle = FGarbageCollectionHandle::NewWeakRef(InClass, InMonoObject, true);
    BindingAddress2GarbageCollectionHandle.Add(static_cast<void*>(const_cast<T*>(InObject)), GarbageCollectionHandle);
    ...
    // ★ 无 owner 情况下只建 weak handle + address map
}

template <typename T>
auto FBindingRegistry::AddReference(const FGarbageCollectionHandle& InOwner, const T* InObject,
                                    FClassReflection* InClass, MonoObject* InMonoObject)
{
    const auto GarbageCollectionHandle = FGarbageCollectionHandle::NewRef(InClass, InMonoObject, true);
    BindingAddress2GarbageCollectionHandle.Add(static_cast<void*>(const_cast<T*>(InObject)), GarbageCollectionHandle);
    ...
    return FCSharpEnvironment::GetEnvironment()
        .AddReference(InOwner, new FBindingReference(GarbageCollectionHandle));
    // ★ 有 owner 时再挂一条 FBindingReference，把 helper 生命周期收束到 owner
}

virtual ~FBindingReference() override
{
    (void)FCSharpEnvironment::GetEnvironment().RemoveBindingReference(GarbageCollectionHandle);
    // ★ owner 释放时反向通知 binding registry 清掉 helper 句柄
}

bool FReferenceRegistry::AddReference(const FGarbageCollectionHandle& InOwner, FReference* InReference)
{
    if (!ReferenceRelationship.Contains(InOwner))
    {
        ReferenceRelationship.Add(InOwner, {});
    }

    ReferenceRelationship[InOwner].Emplace(InReference);
    // ★ 引用关系显式记录在 owner -> reference 列表里
    return true;
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FBindingRegistry.cpp
// 位置: 17-68，registry 同时维护 address -> MonoObject 与反向清理
// ============================================================================
void FBindingRegistry::Deinitialize()
{
    for (auto& [Key, Value] : GarbageCollectionHandle2BindingAddress.Get())
    {
        FGarbageCollectionHandle::Free<true>(Key);

        if (Value.bNeedFree)
        {
            delete Value.AddressWrapper;
        }
        // ★ registry 退出时不仅释放 handle，还可能回收 helper 包装地址
    }
}

MonoObject* FBindingRegistry::GetObject(const FBindingValueMapping::FAddressType InAddress)
{
    const auto FoundGarbageCollectionHandle = BindingAddress2GarbageCollectionHandle.Find(InAddress);
    return FoundGarbageCollectionHandle != nullptr ? static_cast<MonoObject*>(*FoundGarbageCollectionHandle) : nullptr;
}
```

关键源码 [3] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:120-205`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp
// 位置: 120-205，Angelscript 的核心合同仍是 bind lambda replay
// ============================================================================
struct FBindFunction
{
    FName BindName;
    int32 BindOrder;
    TFunction<void()> Function;
};

void FAngelscriptBinds::RegisterBinds(FName BindName, int32 BindOrder, TFunction<void()> Function)
{
    GetBindArray().Add({BindName.IsNone() ? MakeUnnamedBindName() : BindName, BindOrder, MoveTemp(Function)});
    // ★ 这里只记“名字 + 顺序 + lambda”，没有通用 helper identity registry
}

void FAngelscriptBinds::CallBinds(const TSet<FName>& DisabledBindNames)
{
    for (const FBindFunction& Bind : GetSortedBindArray())
    {
        if (DisabledBindNames.Contains(Bind.BindName))
        {
            UE_LOG(Angelscript, Log, TEXT("Skipping bind '%s'"), *Bind.BindName.ToString());
            continue;
        }

        Bind.Function();
        // ★ 真正的类型/函数身份直接落进 AngelScript engine 注册表
    }
}
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| helper 身份的落点 | UnrealCSharp 为非 `UObject` helper 明确铺了一层 `address -> GC handle -> MonoObject` registry；Angelscript 主要直接把类型/函数身份注册进 AngelScript engine | 实现方式不同 |
| 生命周期归属 | UnrealCSharp 的 helper 可挂到 owner 的 `FReferenceRegistry` 上，析构再回调 `RemoveBindingReference()`；Angelscript 当前这条 bind replay 路径没有等价的通用 helper ownership plane | 实现方式不同 |
| descriptor 物化时机 | UnrealCSharp 先建 register 图、按类名去重，再惰性转成 `FBindingClass` 并发布；Angelscript 更偏 eager replay | 实现方式不同 |

### [维度 D6] 生成代码可见面分层：author-facing partial wrappers vs compile-only shard outputs

前面的 D6 已经把 `sln/csproj`、Analyzer、模板脚手架和 Content Browser 代码入口写得很细，但还有一个很关键的问题没有单独落结论：**生成代码本身是不是作者可见的一等资产**。继续往下看，UnrealCSharp 的答案是明确的“是，而且故意拆成两层”。`FBindingClassGenerator::Generator()` 对每个 binding class 都同时执行 `GeneratorPartial()` 和 `GeneratorImplementation()`；前者生成 `public partial class`，里面是属性、下标、默认参数、`IGarbageCollectionHandle` 和类型安全 wrapper；后者生成 `public static unsafe partial class`，只保留 `[MethodImpl(MethodImplOptions.InternalCall)]` 的 ABI stub。两层文件都写进同一个 `BindingDirectory` 下，因此 IDE 看到的不是单一“自动生成代码”，而是作者层和 ABI 层并列存在的生成树。

Angelscript 的生成面则更偏 build artifact。`AngelscriptFunctionTableCodeGenerator` 生成的是 `AS_FunctionTable_*.cpp` 分片、`Summary.json`、`ModuleSummary.csv`、`Entries.csv`，并会主动删除 stale shard；它甚至还会反查 `AngelscriptRuntime.Build.cs` 来决定支持模块范围。这条链非常扎实，但它产出的主要是**编译期/诊断期资产**，不是像 UnrealCSharp 那样直接进入脚本作者日常编辑面的 `.cs` wrapper。就本轮定位到的这条路径而言，Angelscript 还没有对等的 “作者层 wrapper + ABI 层 stub” 双轨生成面。

```
[D6-Deep] Generated Surface Split

[UnrealCSharp]
FBindingClassGenerator
├─ GeneratorPartial()                         // 作者直接消费的 public partial class
│  ├─ property/indexer/method wrappers
│  └─ IGarbageCollectionHandle contract
└─ GeneratorImplementation()                  // ABI 层 static unsafe partial class
   └─ InternalCall extern stubs

[Angelscript]
AngelscriptFunctionTableCodeGenerator
├─ AS_FunctionTable_*.cpp                     // 编译分片
├─ AS_FunctionTable_Summary.json              // 汇总诊断
├─ AS_FunctionTable_ModuleSummary.csv
└─ DeleteStaleOutputs()                       // 清理旧分片
```

关键源码 [1] `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FBindingClassGenerator.cpp:18-25`、`:672-740`、`:743-948`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FBindingClassGenerator.cpp
// 位置: 18-25，生成入口天然就是双轨
// ============================================================================
void FBindingClassGenerator::Generator(const FBindingClass* InClass)
{
    if (InClass->IsSet())
    {
        GeneratorPartial(InClass);
        GeneratorImplementation(InClass);
        // ★ 每个 binding class 同时生成作者层与 ABI 层，而不是只出一份 helper
    }
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FBindingClassGenerator.cpp
// 位置: 672-740，作者层 partial wrapper 直接进生成目录
// ============================================================================
if (!InClass->IsReflectionClass() && InClass->GetBaseClass().IsEmpty())
{
    GCHandleContent = FString::Printf(TEXT(
        "\t\tpublic nint %s { get; set; }\n"
    ),
                                      *PROPERTY_GARBAGE_COLLECTION_HANDLE);
    // ★ 非 reflection binding class 自动带 IGarbageCollectionHandle 合同
}

auto Content = FString::Printf(TEXT(
    ...
    "\tpublic partial class %s%s\n"
    ...
),
                               *ClassContent,
                               InClass->GetBaseClass().IsEmpty()
                                   ? (InClass->IsReflectionClass() ? TEXT("") : TEXT(" : IGarbageCollectionHandle"))
                                   : ...);

auto DirectoryName = FPaths::Combine(
    FUnrealCSharpFunctionLibrary::GetGenerationPath(TEXT("/") + NameSpaceContent[0].Replace(TEXT("."), TEXT("/"))),
    FUnrealCSharpFunctionLibrary::GetBindingDirectory());

const auto FileName = FPaths::Combine(DirectoryName, FileBaseName) + CSHARP_SUFFIX;
FUnrealCSharpFunctionLibrary::SaveStringToFile(FileName, Content);
// ★ wrapper 文件按 namespace/binding 目录落盘，IDE 可直接感知
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FBindingClassGenerator.cpp
// 位置: 919-948，ABI 层是另一份 static unsafe partial class
// ============================================================================
auto Content = FString::Printf(TEXT(
    ...
    "\tpublic static unsafe partial class %s\n"
    ...
),
                               *ImplementationNameSpaceContent,
                               *ClassImplementationContent,
                               *FunctionContent);

const auto FileName = FPaths::Combine(DirectoryName,
                                      BINDING_COMBINE_CLASS_IMPLEMENTATION(FileBaseName)) + CSHARP_SUFFIX;
FUnrealCSharpFunctionLibrary::SaveStringToFile(FileName, Content);
// ★ ABI stub 与作者层 wrapper 分文件保存，但仍处在同一条生成树里
```

关键源码 [2] `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:115-139`、`:166-205`、`:282-331`、`:334-445`

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs
// 位置: 115-139，产物主体是编译期 `.cpp` shard
// ============================================================================
int shardCount = (entries.Count + MaxEntriesPerShard - 1) / MaxEntriesPerShard;
for (int shardIndex = 0; shardIndex < shardCount; shardIndex++)
{
    string outputPath = factory.MakePath($"AS_FunctionTable_{module.ShortName}_{shardIndex:D3}", ".cpp");
    factory.CommitOutput(outputPath, BuildShard(...));
    generatedPaths.Add(outputPath);
    ...
    // ★ 这里生成的是编译输入分片，不是作者编辑面的脚本 wrapper
}
```

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs
// 位置: 166-205, 282-331, 334-445
// 说明: 同一路径产出 summary/csv，并按 Build.cs 解析支持模块、清理旧分片
// ============================================================================
string summaryPath = factory.MakePath("AS_FunctionTable_Summary", ".json");
...
File.WriteAllText(summaryPath, summaryJson, Encoding.UTF8);
WriteModuleSummaryCsv(factory, moduleSummaries);
WriteEntryCsv(factory, csvEntries);
// ★ 诊断产物和分片是同一套 build artifact 树

builder.Append("AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_AS_FunctionTable_")
    .Append(moduleShortName)
    .Append('_')
    .Append(shardIndex.ToString("D3"))
    .AppendLine("((int32)FAngelscriptBinds::EOrder::Late + 50, []()");
// ★ shard 的目标仍是生成 native bind replay 代码

string buildCsPath = ResolveRuntimeBuildCsPath(factory);
...
foreach (string rawLine in File.ReadAllLines(buildCsPath))
{
    ...
}
// ★ 生成器甚至会反查 Runtime Build.cs 决定模块范围

foreach (string existingFile in Directory.EnumerateFiles(outputDirectory, "AS_FunctionTable_*.cpp"))
{
    if (!generatedPaths.Contains(existingFile))
    {
        File.Delete(existingFile);
    }
}
// ★ stale output 清理的是旧 `.cpp` shard，不是作者层 wrapper
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 生成代码的层次 | UnrealCSharp 明确拆出作者层 `public partial class` 与 ABI 层 `static unsafe partial class`；Angelscript 这条路径主要生成 `.cpp/json/csv` build artifact | 实现方式不同 |
| 生成产物的 IDE 可见性 | UnrealCSharp 的 wrapper/stub 都落到生成目录并可被 IDE 直接消费；Angelscript 在本轮定位到的函数表路径上没有对等的 author-facing wrapper 层 | 部分没有实现 + 实现方式不同 |
| 生成器关注点 | UnrealCSharp 更偏“让作者直接使用生成 API”；Angelscript 更偏“让编译期覆盖率、分片和诊断可追踪” | 实现质量差异，目标不同 |

### [维度 D8] 临时参数缓冲落点：managed `stackalloc` slabs vs native `UASFunction_*_JIT` shape classes

前面的 D8 已经比较过 `AOT/JIT`、`out/ref` 回写、call-family 和 buffer allocator，但还有一个很具体、也很能说明优化哲学差异的问题没有单独拆开：**调用时那块最短命的参数缓冲，到底是谁来持有**。UnrealCSharp 的答案是“尽量前推到 generated managed callsite”。`FBindingClassGenerator::GeneratorPartial()` 在属性、下标和普通函数 wrapper 里统一生成 `unsafe { var InBuffer = stackalloc byte[...] }`、`OutBuffer`、`ReturnBuffer`，然后把 primitive 值或 `GarbageCollectionHandle` 指针写进去，再调用固定形态的 `InternalCall` 入口。换句话说，UnrealCSharp 把临时 scratch slab 放在生成出来的 C# wrapper 里，native 侧更多看到的是“已经编好 layout 的字节块 + function hash”。

Angelscript 则把这件事压在 native `UFunction` 形状上。`ASClass.h` 直接声明一组 `UASFunction_*_JIT` 子类；到 `ASClass.cpp` 里，每个子类都自己负责 `CheckGameThreadExecution()`、`VerifyScriptVirtualResolved()`、从 `FFrame` 或 `Parms` 里取 typed value，然后直接走 `MakeRawJITCall_Arg<T>()` / `MakeRawJITCall_ReturnValue<T>()`。也就是说，Angelscript 不是在作者侧生成 `unsafe stackalloc` wrapper，而是在 native runtime 里用不同 `UASFunction` 子类承接不同参数形状。

```
[D8-Deep] Scratch Buffer Placement

[UnrealCSharp]
generated partial wrapper
├─ stackalloc InBuffer / OutBuffer / ReturnBuffer
├─ write primitive or GCHandle into slab
└─ FFunctionImplementation.FFunction_*Call*Implementation
   └─ InternalCall crosses into native

[Angelscript]
generated UASFunction_*_JIT subclass
├─ StepCompiledIn<FProperty>() / read Parms
├─ typed local value on native stack
└─ MakeRawJITCall_Arg<T>() / ReturnValue<T>()
```

关键源码 [1] `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FBindingClassGenerator.cpp:463-658`、`Reference/UnrealCSharp/Script/UE/Library/FFunctionImplementation.cs:5-100`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FBindingClassGenerator.cpp
// 位置: 463-658，wrapper 侧统一生成 stackalloc scratch slab
// ============================================================================
if (!Params.IsEmpty())
{
    auto BufferSize = 0;

    for (auto Index = 0; Index < Params.Num(); ++Index)
    {
        InBufferBody += FString::Printf(TEXT(
            "\t\t\t\t*(%s*)(%s%s) = %s;\n\n"
        ),
                                        Params[Index]->IsPrimitive() ? *Params[Index]->GetName() : TEXT("nint"),
                                        IN_BUFFER_TEXT,
                                        ...,
                                        Params[Index]->IsPrimitive()
                                            ? *FunctionParamName[Index]
                                            : *FString::Printf(TEXT("%s?.%s ?? nint.Zero"),
                                                               *FunctionParamName[Index],
                                                               *PROPERTY_GARBAGE_COLLECTION_HANDLE));
        BufferSize += Params[Index]->GetBufferSize();
    }

    InBufferBody = FString::Printf(TEXT(
        "\t\t\t\tvar %s = stackalloc byte[%d];\n\n"
        "%s"
    ), IN_BUFFER_TEXT, BufferSize, *InBufferBody);
    // ★ 参数 slab 在生成出来的 C# wrapper 里现场分配
}

if (!FunctionRefParamIndex.IsEmpty())
{
    OutBufferBody = FString::Printf(TEXT(
        "\t\t\t\tvar %s = stackalloc byte[%d];\n\n"
    ), OUT_BUFFER_TEXT, BufferSize);
}

if (Function.GetReturn() != nullptr)
{
    ReturnBufferBody = FString::Printf(TEXT(
        "\t\t\t\tvar %s = stackalloc byte[%d];\n\n"
    ), RETURN_BUFFER_TEXT, Function.GetReturn()->GetBufferSize());
}

auto FunctionCallBody = FString::Printf(TEXT(
    "%s.%s(%s, %s, %s, %s);\n"
),
                                        *BINDING_COMBINE_CLASS_IMPLEMENTATION(ClassContent),
                                        *BINDING_COMBINE_FUNCTION_IMPLEMENTATION(...),
                                        ...,
                                        bHasInBuffer ? IN_BUFFER_TEXT : TEXT("null"),
                                        bHasOutBuffer ? OUT_BUFFER_TEXT : TEXT("null"),
                                        bHasReturnBuffer ? RETURN_BUFFER_TEXT : TEXT("null"));
// ★ native 入口只看到三块可选 byte*，scratch 布局已经在 managed wrapper 端完成
```

```csharp
// ============================================================================
// 文件: Reference/UnrealCSharp/Script/UE/Library/FFunctionImplementation.cs
// 位置: 5-100，跨边界入口是有限个 call family，而不是每函数一条 ABI
// ============================================================================
public static unsafe class FFunctionImplementation
{
    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern void FFunction_GenericCall2Implementation(nint InMonoObject, uint InFunctionHash,
        byte* InBuffer);

    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern void FFunction_PrimitiveCall7Implementation(nint InMonoObject, uint InFunctionHash,
        byte* InBuffer, byte* OutBuffer, byte* ReturnBuffer);

    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern void FFunction_GenericCall14Implementation(nint InMonoObject, uint InFunctionHash,
        byte* InBuffer, byte* OutBuffer);
    // ★ 作者层 wrapper 分配 stackalloc，ABI 层只复用有限个 call family
}
```

关键源码 [2] `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h:356-497`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:2972-3198`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h
// 位置: 356-497，参数形状先被编进不同的 UASFunction 子类
// ============================================================================
UCLASS()
class ANGELSCRIPTRUNTIME_API UASFunction_NoParams_JIT : public UASFunction
{
    GENERATED_BODY()
public:
    virtual void RuntimeCallFunction(UObject* Object, FFrame& Stack, RESULT_DECL) override;
    virtual void RuntimeCallEvent(UObject* Object, void* Parms) override;
};

UCLASS()
class ANGELSCRIPTRUNTIME_API UASFunction_DWordArg_JIT : public UASFunction
{
    GENERATED_BODY()
public:
    virtual void RuntimeCallFunction(UObject* Object, FFrame& Stack, RESULT_DECL) override;
    virtual void RuntimeCallEvent(UObject* Object, void* Parms) override;
};

UCLASS()
class ANGELSCRIPTRUNTIME_API UASFunction_ObjectReturn_JIT : public UASFunction
{
    GENERATED_BODY()
public:
    virtual void RuntimeCallFunction(UObject* Object, FFrame& Stack, RESULT_DECL) override;
    virtual void RuntimeCallEvent(UObject* Object, void* Parms) override;
};
// ★ scratch 形状先被离散化到 native class 层，而不是作者侧 wrapper 层
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp
// 位置: 2972-3198，native 子类自己读参数并直接调用 raw JIT
// ============================================================================
void UASFunction_NoParams_JIT::RuntimeCallFunction(UObject* Object, FFrame& Stack, RESULT_DECL)
{
    if (!CheckGameThreadExecution(this))
        return;
    VerifyScriptVirtualResolved(this, Object);

    P_FINISH;
    MakeRawJITCall_NoParam(Object, JitFunction_Raw);
}

void UASFunction_DWordArg_JIT::RuntimeCallFunction(UObject* Object, FFrame& Stack, RESULT_DECL)
{
    if (!CheckGameThreadExecution(this))
        return;
    VerifyScriptVirtualResolved(this, Object);

    asDWORD ArgumentValue;
    Stack.StepCompiledIn<FProperty>(&ArgumentValue);

    P_FINISH;
    MakeRawJITCall_Arg<asDWORD>(Object, JitFunction_Raw, ArgumentValue);
    // ★ typed local value 直接从 FFrame 读出，不需要作者侧 byte slab
}

void UASFunction_ObjectReturn_JIT::RuntimeCallFunction(UObject* Object, FFrame& Stack, RESULT_DECL)
{
    if (!CheckGameThreadExecution(this))
        return;
    VerifyScriptVirtualResolved(this, Object);

    P_FINISH;
    *(UObject**)RESULT_PARAM = MakeRawJITCall_ReturnValue<UObject*>(Object, JitFunction_Raw);
    // ★ 返回值也在 native 子类里原地写回 RESULT_PARAM
}
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 最短命 scratch buffer 的落点 | UnrealCSharp 把 `In/Out/ReturnBuffer` 前推到生成出来的 managed wrapper，用 `stackalloc` 现场分配；Angelscript 把参数形状下沉到 native `UASFunction_*_JIT` 子类 | 实现方式不同 |
| ABI 专门化阶段 | UnrealCSharp 先分配 byte slab，再走有限 `InternalCall` family；Angelscript 先选中 native shape class，再直接 `MakeRawJITCall_*` | 实现方式不同 |
| 作者侧复杂度 | UnrealCSharp 让生成代码承担 `unsafe` 与 byte layout；Angelscript 让 runtime class 承担参数展开与返回写回 | 实现方式不同 |

---

## 深化分析 (2026-04-09 07:32:45)

### [维度 D7] Content Browser 对象语义：code workspace data source vs asset mirror

前面的多轮分析已经把 `Generator / Compile / Runtime` 链路拆得很细，但还没有单独回答一个编辑器层面很关键的问题：**Content Browser 里出现的到底是“可直接编辑的代码工作区”，还是“脚本相关资产的镜像视图”**。继续下钻后可以确认，UnrealCSharp 在这里走的是第一种路线。`UDynamicDataSource` 不是简单列出某个 package 下的对象，而是给动态类建立一棵虚拟路径树，把 Content Browser item 反解成磁盘上的 `.cs` 文件路径，并直接接上 `OpenSourceFile()`、可撤销删除和立即重新编译。

Angelscript 当前这条链路则明显偏向第二种路线。`UAngelscriptContentBrowserDataSource` 的 item payload 直接挂的是 `AssetsPackage` 里的 `UObject`，路径语义是 `/All/Angelscript/<AssetName>` 虚拟资产路径；它能提供缩略图和 `Legacy_TryGetPackagePath()`，但 `GetItemPhysicalPath()`、`CanEditItem()`、`EditItem()`、`BulkEditItems()` 都明确返回 `false`。换句话说，Angelscript 的 Content Browser 数据源当前主要是**脚本资产浏览入口**，不是**源码工作区入口**。

```
[D7] Content Browser Ownership

[UnrealCSharp]
Content Browser /DynamicData
├─ virtual root tree                         // 先建虚拟目录树
├─ class item -> physical `.cs` path        // item 可反解到磁盘源码
├─ EditItem -> OpenSourceFile()             // 直接跳源码编辑器
├─ DeleteItem -> undo + ImmediatelyCompile()// 删除即触发编译闭环
└─ Add New -> Dynamic Class dialog          // 同一入口继续创建源码

[Angelscript]
Content Browser /All/Angelscript
├─ payload = UObject in AssetsPackage       // 数据源核心是脚本资产对象
├─ thumbnail via FAssetData                 // 资产浏览能力完整
├─ Legacy_TryGetPackagePath()               // 仍停留在 package 语义
└─ physical path / edit / bulk edit = false // 不承担源码工作区职责
```

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:31-88`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp
// 位置: 31-88，数据源初始化时就把文件删除回滚和“新建动态类”入口接进 Content Browser
// ============================================================================
FDeleteFileChange::FDeleteFileChange(const FString& InFilePath, const FString& InFileContent):
	FilePath(InFilePath),
	FileContent(InFileContent)
{
}

void FDeleteFileChange::Apply(UObject* Object)
{
	if (IFileManager::Get().FileExists(*FilePath))
	{
		IFileManager::Get().Delete(*FilePath);

		FCSharpCompiler::Get().ImmediatelyCompile();
		// ★ 删除源码文件不是孤立文件操作，而是编辑器内可撤销的“删文件 + 立刻编译”
	}
}

void FDeleteFileChange::Revert(UObject* Object)
{
	FUnrealCSharpFunctionLibrary::SaveStringToFile(*FilePath, FileContent);

	FCSharpCompiler::Get().ImmediatelyCompile();
	// ★ Undo 会把文件内容写回并重新编译，说明数据源直接拥有代码工作流语义
}

void UDynamicDataSource::Initialize(const bool InAutoRegister)
{
	Super::Initialize(InAutoRegister);

	OnDynamicClassUpdatedHandle = FUnrealCSharpCoreModuleDelegates::OnDynamicClassUpdated.AddUObject(
		this, &UDynamicDataSource::OnDynamicClassUpdated);

	OnEndGeneratorHandle = FUnrealCSharpCoreModuleDelegates::OnEndGenerator.AddUObject(
		this, &UDynamicDataSource::OnEndGenerator);

	if (const auto Menu = UToolMenus::Get()->ExtendMenu("ContentBrowser.AddNewContextMenu"))
	{
		Menu->AddDynamicSection(...,
			FNewToolMenuDelegate::CreateLambda(
				[WeakThis = TWeakObjectPtr<UDynamicDataSource>(this)](UToolMenu* InMenu)
				{
					if (WeakThis.IsValid())
					{
						WeakThis->PopulateAddNewContextMenu(InMenu);
						// ★ “新建动态类”直接作为 Content Browser 上下文菜单的一部分
					}
				}));
	}

	BuildRootPathVirtualTree();
}
```

关键源码 [2] `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:518-590`、`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicHierarchy.cpp:130-151`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp
// 位置: 518-590，文件 item 可直接打开、批量打开、删除并接入撤销系统
// ============================================================================
bool UDynamicDataSource::CanEditItem(const FContentBrowserItemData& InItem, FText* OutErrorMsg)
{
	return true;
}

bool UDynamicDataSource::EditItem(const FContentBrowserItemData& InItem)
{
	const auto ItemDataPayload = GetFileItemDataPayload(InItem);

	return ItemDataPayload
		       ? FSourceCodeNavigation::OpenSourceFile(ItemDataPayload->GetInternalPath().ToString())
		       : false;
	// ★ item 的“编辑”定义就是打开真实源码文件
}

bool UDynamicDataSource::DeleteItem(const FContentBrowserItemData& InItem)
{
	...
	if (GEditor != nullptr && IsValid(GEditor->Trans))
	{
		if (FString Content; FFileHelper::LoadFileToString(Content, *InternalPath))
		{
			FScopedTransaction Transaction(...);

			if (GUndo != nullptr)
			{
				auto DeleteFileChange = MakeUnique<FDeleteFileChange>(InternalPath, Content);

				DeleteFileChange->Apply(nullptr);
				GUndo->StoreUndo(this, MoveTemp(DeleteFileChange));
				// ★ 删除进入 UE Undo 栈，而不是插件自己偷偷删文件
				return true;
			}
		}
	}
	...
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicHierarchy.cpp
// 位置: 130-151，虚拟路径可反解到磁盘脚本目录
// ============================================================================
FString FDynamicHierarchy::ConvertInternalPathToFileSystemPath(const FString& InInternalPath)
{
	auto FileSystemPath = InInternalPath;

	if (FileSystemPath.IsEmpty() ||
		!FileSystemPath.StartsWith(TEXT("/")))
	{
		return FString();
	}

	const auto SecondSlashIndex = FileSystemPath.Find(TEXT("/"), ESearchCase::IgnoreCase, ESearchDir::FromStart, 1);

	FileSystemPath = SecondSlashIndex != INDEX_NONE
		                 ? FileSystemPath.RightChop(SecondSlashIndex)
		                 : TEXT("");

	if (FileSystemPath.IsEmpty())
	{
		return FString();
	}

	return FUnrealCSharpFunctionLibrary::GetFullScriptDirectory() / FileSystemPath;
	// ★ Content Browser 内部路径最终落回真实 `Script/` 目录
}
```

关键源码 [3] `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:16-31`、`:182-231`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp
// 位置: 16-31, 182-231，当前数据源承载的是脚本资产视图，不是源码工作区
// ============================================================================
FContentBrowserItemData UAngelscriptContentBrowserDataSource::CreateAssetItem(UObject* Asset)
{
	auto Payload = MakeShared<FAngelscriptContentBrowserPayload>();
	Payload->Path = Asset->GetPathName();
	Payload->Asset = Asset;

	return FContentBrowserItemData(
		this,
		EContentBrowserItemFlags::Type_File | EContentBrowserItemFlags::Category_Asset,
		*(TEXT("/All/Angelscript/") + Asset->GetName()), Asset->GetFName(), FText::FromString(DisplayName), Payload, *Payload->Path);
	// ★ item 身份是 `AssetsPackage` 中的 UObject，而不是磁盘脚本文件
}

bool UAngelscriptContentBrowserDataSource::GetItemPhysicalPath(const FContentBrowserItemData& InItem, FString& OutDiskPath)
{
	return false;
}

bool UAngelscriptContentBrowserDataSource::CanEditItem(const FContentBrowserItemData& InItem, FText* OutErrorMsg)
{
	return false;
}

bool UAngelscriptContentBrowserDataSource::EditItem(const FContentBrowserItemData& InItem)
{
	return false;
}

bool UAngelscriptContentBrowserDataSource::Legacy_TryGetPackagePath(const FContentBrowserItemData& InItem, FName& OutPackagePath)
{
	if (InItem.GetOwnerDataSource() == this && InItem.IsFile())
	{
		auto Payload = StaticCastSharedPtr<const FAngelscriptContentBrowserPayload>(InItem.GetPayload());
		OutPackagePath = *Payload->Path;
		return true;
	}

	return false;
}
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| Content Browser item 的真实身份 | UnrealCSharp item 可以反解到 `Script/` 下真实 `.cs` 文件；Angelscript item 对应的是 `AssetsPackage` 里的脚本资产对象 | 实现方式不同 |
| 编辑与删除语义 | UnrealCSharp 支持 `OpenSourceFile()`、可撤销删文件和删除后立即编译；Angelscript 当前路径上 `GetItemPhysicalPath/CanEditItem/EditItem/BulkEditItems` 都未实现 | 部分没有实现 |
| 编辑器入口职责 | UnrealCSharp 把 Content Browser 当成代码工作区入口；Angelscript 目前更像脚本资产浏览面板 | 实现质量差异，目标面不同 |

### [维度 D11] 打包合同落点：editor-side UFS staging mutation vs script-root cache convention

前面的 D11 已经比较过 publish payload、cache 命名和预编译数据封印，但还有一个更底层的问题值得单独抽出来：**“交付物一定会被带进包里”这件事，究竟是在编辑器配置阶段就被写死，还是运行时按目录约定自己去找。** 继续追源码后可以看到，UnrealCSharp 的答案明显偏前者。`FUnrealCSharpEditorModule::UpdatePackagingSettings()` 在 Editor 启动时就把 `PublishDirectory` 写进 `DirectoriesToAlwaysStageAsUFS`；而 `FUnrealCSharpFunctionLibrary` 又把最终 DLL 闭包和程序集探测路径集中到 `GetFullAssemblyPublishPath()` / `GetAssemblyPath()`。

再往下看 `FMonoFunctionLibrary`，它把 Mono 基座的根目录也拆成了 editor/runtime 双态：编辑器内从插件 `Source/ThirdParty/Mono` 读，运行时从项目 `Binaries/<Platform>/Mono` 读。两段组合起来，UnrealCSharp 的交付合同可以概括为：**Editor 主动把 `Content/<PublishDirectory>` staged 进去，Runtime 再从 staged 的程序集目录和随包的 Mono `net` 目录装载。**

Angelscript 则没有走这条路。当前定位到的主装载链路里，`FAngelscriptEngine` 先从 `DiscoverScriptRoots()` 得到项目脚本根，再在这个根下读 `Binds.Cache` 和 `PrecompiledScript[_Config].Cache`；脚本运行时对象则挂到 `/Script/Angelscript` 与 `/Script/AngelscriptAssets` 两个编译内 package 下。预编译缓存是否合法由 `BuildIdentifier` 校验，JIT 产物是否匹配再由 `PrecompiledDataGuid` 做二次核对。就本轮定位到的路径而言，Angelscript 依赖的是**script-root 约定 + cache 命名 + build seal**，而不是编辑器侧自动写入 staging 配置。

```
[D11] Delivery Contract

[UnrealCSharp]
Editor Startup
├─ UpdatePackagingSettings()
│  └─ add Content/<PublishDirectory> to UFS staging
├─ payload identity
│  ├─ UE.dll / Game.dll / Custom.dll
│  └─ Content/<PublishDirectory>
└─ runtime probe set
   ├─ Content/<PublishDirectory>
   └─ Mono/lib/.../net

[Angelscript]
Engine Initialize
├─ DiscoverScriptRoots()                     // 先确定项目脚本根
├─ load Binds.Cache                         // 绑定数据库
├─ load PrecompiledScript[_Config].Cache    // 预编译缓存
├─ validate BuildIdentifier / DataGuid      // 构建封印校验
└─ create /Script/Angelscript packages      // 运行时对象挂入内存 package
```

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:209-234`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp
// 函数: FUnrealCSharpEditorModule::UpdatePackagingSettings
// 位置: Editor 启动时主动写入打包 staging 目录
// ============================================================================
void FUnrealCSharpEditorModule::UpdatePackagingSettings()
{
	const auto PublishDirectory = FUnrealCSharpFunctionLibrary::GetPublishDirectory();

	if (const auto ProjectPackagingSettings = FUnrealCSharpFunctionLibrary::GetMutableDefaultSafe<
		UProjectPackagingSettings>())
	{
		bool bIsExisted{};

		for (const auto& [Path] : ProjectPackagingSettings->DirectoriesToAlwaysStageAsUFS)
		{
			if (Path == PublishDirectory)
			{
				bIsExisted = true;
				break;
			}
		}

		if (!bIsExisted)
		{
			ProjectPackagingSettings->DirectoriesToAlwaysStageAsUFS.Add({PublishDirectory});
			ProjectPackagingSettings->TryUpdateDefaultConfigFile();
			// ★ 不是“文档建议用户去配”，而是插件自己把 Publish 目录写入 staging config
		}
	}
}
```

关键源码 [2] `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp:995-1049`、`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoFunctionLibrary.cpp:4-63`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp
// 位置: 995-1049，交付 DLL 集合与程序集探测路径集中定义
// ============================================================================
FString FUnrealCSharpFunctionLibrary::GetPublishDirectory()
{
	if (const auto UnrealCSharpSetting = GetMutableDefaultSafe<UUnrealCSharpSetting>())
	{
		return UnrealCSharpSetting->GetPublishDirectory();
	}

	return DEFAULT_PUBLISH_DIRECTORY;
}

TArray<FString> FUnrealCSharpFunctionLibrary::GetFullAssemblyPublishPath()
{
	return TArrayBuilder<FString>().
	       Add(GetFullUEPublishPath()).
	       Add(GetFullGamePublishPath()).
	       Append(GetFullCustomProjectsPublishPath()).
	       Build();
	// ★ 发布态“哪些程序集必须存在”在这里被收敛成一组确定路径
}

TArray<FString> FUnrealCSharpFunctionLibrary::GetAssemblyPath()
{
	return TArrayBuilder<FString>().
	       Add(FPaths::ProjectContentDir() / GetPublishDirectory()).
	       Add(FMonoFunctionLibrary::GetNetDirectory()).
	       Build();
	// ★ Runtime 探测路径 = 项目 Content/Publish + Mono net 目录
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoFunctionLibrary.cpp
// 位置: 4-63，Mono 基座目录在 editor/runtime 间切换
// ============================================================================
FString FMonoFunctionLibrary::GetMonoDirectory()
{
#if WITH_EDITOR
	return FString::Printf(TEXT(
		"%s/Source/ThirdParty/Mono"),
	                       *FUnrealCSharpFunctionLibrary::GetPluginDirectory()
	);
#else
	return FString::Printf(TEXT(
		"%s/Binaries/%s/Mono"),
	                       *FPaths::ProjectDir(),
	                       ...);
#endif
}

FString FMonoFunctionLibrary::GetLibDirectory()
{
	return FString::Printf(TEXT(
		"%s/lib/%s/%s"),
	                       *GetMonoDirectory(),
	                       MONO_CONFIGURATION,
	                       ...);
}

FString FMonoFunctionLibrary::GetNetDirectory()
{
	return FString::Printf(TEXT(
		"%s/net"),
	                       *GetLibDirectory()
	);
}
```

关键源码 [3] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:781-794`、`:1425-1556`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 781-794, 1425-1556，Angelscript 的装载闭包以 script root + cache 约定为核心
// ============================================================================
FString FAngelscriptEngine::GetScriptRootDirectory()
{
	...
	const auto& AllRootPaths = CurrentEngine->AllRootPaths;
	return AllRootPaths.IsEmpty() ? TEXT("") : CurrentEngine->AllRootPaths[0];
	// ★ 第一根脚本根目录就是后续 cache / bind DB 的默认宿主
}

bGeneratePrecompiledData = RuntimeConfig.bGeneratePrecompiledData;
bScriptDevelopmentMode = RuntimeConfig.bIsEditor || RuntimeConfig.bDevelopmentMode;
bUsePrecompiledData = !bGeneratePrecompiledData && !RuntimeConfig.bIgnorePrecompiledData
	&& !RuntimeConfig.bRunningCommandlet && !WITH_EDITOR && !bScriptDevelopmentMode;

AllRootPaths = DiscoverScriptRoots(/*bOnlyProjectRoot =*/ true);

#if AS_USE_BIND_DB
FAngelscriptBindDatabase::Get().Load(GetScriptRootDirectory() / TEXT("Binds.Cache"), bGeneratePrecompiledData);
#endif

if (bUsePrecompiledData)
{
	FString Filename;
#if UE_BUILD_SHIPPING
	Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript_Shipping.Cache");
#elif UE_BUILD_TEST
	Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript_Test.Cache");
#elif UE_BUILD_DEVELOPMENT
	Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript_Development.Cache");
#endif

	if (!IFileManager::Get().FileExists(*Filename))
		Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript.Cache");

	if (IFileManager::Get().FileExists(*Filename))
	{
		PrecompiledData = new FAngelscriptPrecompiledData(Engine);
		PrecompiledData->Load(Filename);

		if (!PrecompiledData->IsValidForCurrentBuild())
		{
			delete PrecompiledData;
			PrecompiledData = nullptr;
			// ★ 当前路径上强调的是 root 约定 + cache 文件名 + 构建匹配校验
		}
	}
}
```

关键源码 [4] `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp:2642-2689`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp
// 位置: 2642-2689，预编译缓存是否合法由构建标识封印控制
// ============================================================================
bool FAngelscriptPrecompiledData::IsValidForCurrentBuild()
{
	return BuildIdentifier == GetCurrentBuildIdentifier() && BuildIdentifier != -1;
	// ★ 首层合法性检查是 BuildIdentifier
}

void FAngelscriptPrecompiledData::Save(const FString& Filename)
{
	TArray<uint8> Data;
	FMemoryWriter Writer(Data, true);
	Writer << *this;
	FFileHelper::SaveArrayToFile(Data, *Filename);
}

void FAngelscriptPrecompiledData::Load(const FString& Filename)
{
	FFileHelper::LoadFileToArray(LoadedData, *Filename);
	FMemoryReaderWithPtr Reader(LoadedData);
	Reader << *this;
	// ★ cache 是运行时按文件名读回，再反序列化重建
}
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| “交付物一定进包” 的责任归属 | UnrealCSharp 在 Editor 启动时主动把 `PublishDirectory` 写入 `DirectoriesToAlwaysStageAsUFS`；本轮定位到的 Angelscript 装载链路未见等价的自动 staging 写入 | 当前路径上没有实现 + 实现方式不同 |
| 运行时装载闭包 | UnrealCSharp 的探测集合是 `Content/<PublishDirectory>` DLL 闭包 + Mono `net` 目录；Angelscript 依赖 `GetScriptRootDirectory()` 下的 `Binds.Cache` / `PrecompiledScript*.Cache` | 实现方式不同 |
| 兼容性封印 | UnrealCSharp 更强调固定程序集路径集合与 Mono 基座目录；Angelscript 额外用 `BuildIdentifier` 与 `PrecompiledDataGuid` 双层校验 cache/JIT 匹配 | 实现质量差异，封印粒度不同 |

### [维度 D5] 调试协议接缝：Mono soft debugger attach vs custom DebugServer V2

虽然本轮任务的主焦点不在 D5，但顺着 `Domain` 和 `Debugging` 目录继续追下去后，这里其实有一条非常清晰、且对架构取舍影响极大的分叉：**UnrealCSharp 是把调试能力交给 Mono/.NET 现成协议栈，还是像 Angelscript 那样自己定义完整调试协议。** 源码给出的答案很直接。UnrealCSharp 只在 `UUnrealCSharpSetting` 里暴露 `bEnableDebug / Host / Port`，然后在 `FMonoDomain::Initialize()` 中把 `--soft-breakpoints` 和 `--debugger-agent=transport=dt_socket,...` 交给 Mono，再 `mono_debug_init()`。也就是说，UE 侧只负责把调试 socket 打开，协议语义由 Mono 生态承担。

Angelscript 这边则完全相反。`DEBUG_SERVER_VERSION 2`、`EDebugMessageType`、消息 envelope、`SendDebugDatabase()`、`SendAssetDatabase()`、`Pause/Continue/Step`、`Diagnostics`、`CreateBlueprint`、`SetDataBreakpoints` 都是插件自己定义和维护的。`FAngelscriptEngine` 启动时创建 `FAngelscriptDebugServer`，之后异常、单步、诊断、资源数据库同步都从这个自定义服务器出入。这意味着 Angelscript 的调试面并不是“脚本语言默认给什么就用什么”，而是把 UE 特有语义也拉进了调试协议本身。

```
[D5] Debug Transport

[UnrealCSharp]
Project Settings(Debug)
├─ bEnableDebug / Host / Port
└─ FMonoDomain::Initialize()
   ├─ mono_jit_parse_options("--soft-breakpoints")
   ├─ mono_jit_parse_options("--debugger-agent=dt_socket")
   └─ mono_debug_init()

[Angelscript]
Engine Startup
├─ new FAngelscriptDebugServer(port)
├─ FTcpListener accepts client
├─ custom envelope + EDebugMessageType
├─ DebugDatabase / AssetDatabase / Diagnostics
└─ PauseExecution / line callbacks / data breakpoints
```

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpSetting.h:140-159`、`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp:46-118`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpSetting.h
// 位置: 140-159，调试输入面只暴露“是否开启 + host + port”三个配置
// ============================================================================
UPROPERTY(Config, EditAnywhere, Category = Domain)
TSubclassOf<UAssemblyLoader> AssemblyLoader;

UPROPERTY(Config, EditAnywhere, Category = Debug)
bool bEnableDebug;

UPROPERTY(Config, EditAnywhere, Category = Debug)
FString Host;

UPROPERTY(Config, EditAnywhere, Category = Debug)
int32 Port;
// ★ UE 侧不自定义消息协议，配置面就是“给 Mono debugger agent 传什么地址”
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp
// 位置: 46-118，Domain 初始化时直接接上 Mono soft debugger
// ============================================================================
void FMonoDomain::Initialize(const FMonoDomainInitializeParams& InParams)
{
	RegisterMonoTrace();
	RegisterAssemblyPreloadHook();

	if (Domain == nullptr)
	{
		...
		mono_jit_set_aot_mode(MONO_AOT_MODE_NONE);

		if (const auto UnrealCSharpSetting = FUnrealCSharpFunctionLibrary::GetMutableDefaultSafe<
			UUnrealCSharpSetting>())
		{
			if (UnrealCSharpSetting->IsEnableDebug())
			{
				const auto Config = FString::Printf(TEXT(
					"--debugger-agent=transport=dt_socket,server=y,suspend=n,address=%s:%d"
				),
				                                    *UnrealCSharpSetting->GetHost(),
				                                    UnrealCSharpSetting->GetPort());

				char* Options[] = {
					TCHAR_TO_ANSI(TEXT("--soft-breakpoints")),
					TCHAR_TO_ANSI(*Config)
				};

				mono_jit_parse_options(sizeof(Options) / sizeof(char*), Options);
				// ★ UE 侧只负责把 Mono debugger agent 参数组装好
			}
		}

		mono_debug_init(MONO_DEBUG_FORMAT_MONO);
		Domain = mono_jit_init("UnrealCSharp");
		mono_domain_set(Domain, false);
	}
}
```

关键源码 [2] `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h:15-92`、`:640-692`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h
// 位置: 15-92, 640-692，自定义调试协议从消息类型、包封装到发送 API 全部由插件定义
// ============================================================================
#define DEBUG_SERVER_VERSION 2

enum class EDebugMessageType : uint8
{
	Diagnostics,
	RequestDebugDatabase,
	DebugDatabase,

	StartDebugging,
	StopDebugging,
	Pause,
	Continue,
	RequestCallStack,
	CallStack,
	ClearBreakpoints,
	SetBreakpoint,
	...
	DebugServerVersion,
	CreateBlueprint,
	ReplaceAssetDefinition,
	SetDataBreakpoints,
	ClearDataBreakpoints,
};

struct FAngelscriptDebugMessageEnvelope
{
	EDebugMessageType MessageType = EDebugMessageType::Disconnect;
	TArray<uint8> Body;
};

template<typename T>
void SendMessageToAll(EDebugMessageType MessageType, T& Message)
{
	TArray<uint8> Body;
	FMemoryWriter BodyWriter(Body);
	BodyWriter << Message;

	TArray<uint8> Buffer;
	if (!SerializeDebugMessageEnvelope(MessageType, Body, Buffer))
	{
		return;
	}
	...
}

void SendDebugDatabase(FSocket* Client);
void SendAssetDatabase(FSocket* Client);
// ★ 不只是“断点/单步”，连 debug DB、asset DB、蓝图创建也被纳入协议面
```

关键源码 [3] `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:402-431`、`:820-907`、`:1493-1506`、`:2159-2205`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 位置: 402-431, 820-907, 1493-1506, 2159-2205，运行时自建 TCP server，并主动推送调试/资产数据库
// ============================================================================
FAngelscriptDebugServer::FAngelscriptDebugServer(FAngelscriptEngine* InOwnerEngine, int Port)
{
	OwnerEngine = InOwnerEngine;
	Listener = new FTcpListener(FIPv4Endpoint(FIPv4Address::Any, Port));
	Listener->OnConnectionAccepted().BindRaw(this, &FAngelscriptDebugServer::HandleConnectionAccepted);
	UE_LOG(Angelscript, Log, TEXT("Angelscript debug server listening on %s"), *Listener->GetLocalEndpoint().ToText().ToString());
	// ★ 调试入口不是寄宿在外部 runtime，而是插件自己起 socket server
}

void FAngelscriptDebugServer::HandleMessage(EDebugMessageType MessageType, FArrayReaderPtr Datagram, class FSocket* Client)
{
	if (MessageType == EDebugMessageType::RequestDebugDatabase)
	{
		ClientsThatWantDebugDatabase.Add(Client);
		SendDebugDatabase(Client);
		FAngelscriptEngine::Get().EmitDiagnostics(Client);
	}
	else if (MessageType == EDebugMessageType::Pause)
	{
		bBreakNextScriptLine = true;
		FAngelscriptEngine::Get().UpdateLineCallbackState();
	}
	else if (MessageType == EDebugMessageType::StepOver)
	{
		...
		FAngelscriptEngine::Get().UpdateLineCallbackState();
	}
	else if (MessageType == EDebugMessageType::StartDebugging)
	{
		FStartDebuggingMessage Msg;
		*Datagram << Msg;

		bIsDebugging = true;
		AngelscriptDebugServer::DebugAdapterVersion = Msg.DebugAdapterVersion;

		FDebugServerVersionMessage DebugServerVersionMessage;
		DebugServerVersionMessage.DebugServerVersion = DEBUG_SERVER_VERSION;
		SendMessageToClient(Client, EDebugMessageType::DebugServerVersion, DebugServerVersionMessage);
		// ★ 握手、版本协商、状态切换都在自定义协议里
	}
}

void FAngelscriptDebugServer::SendDebugDatabase(FSocket* Client)
{
	FAngelscriptDebugDatabaseSettings DebugSettings;
	DebugSettings.bAutomaticImports = FAngelscriptEngine::Get().ShouldUseAutomaticImportMethod();
	DebugSettings.bFloatIsFloat64 = GetDefault<UAngelscriptSettings>()->bScriptFloatIsFloat64;
	SendMessageToClient(Client, EDebugMessageType::DebugDatabaseSettings, DebugSettings);
	// ★ 协议直接同步脚本语言配置，而不是只做低层断点控制
}

void FAngelscriptDebugServer::SendAssetDatabase(FSocket* Client)
{
	...
	SendMessageToClient(Client, EDebugMessageType::AssetDatabaseInit, InitMessage);
	AssetRegistry.EnumerateAllAssets([&](const FAssetData& AssetData) -> bool
	{
		...
		AddAssetToMessage(Message, AssetData);
		...
	});
	SendMessageToClient(Client, EDebugMessageType::AssetDatabaseFinished, FinishedMessage);
	// ★ 调试协议把 UE 资产数据库也纳入同步范围
}
```

关键源码 [4] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1450-1456`、`:4469-4518`、`:5718-5744`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 1450-1456, 4469-4518, 5718-5744，Engine 主循环把诊断、断点、暂停都路由到 DebugServer
// ============================================================================
/*
Start the debug server that external tools can connect to.
*/
#if WITH_AS_DEBUGSERVER
if ((!bUsePrecompiledData || bScriptDevelopmentMode) && FApp::HasProjectName())
{
	DebugServer = new FAngelscriptDebugServer(this, RuntimeConfig.DebugServerPort);
}
#endif

void FAngelscriptEngine::EmitDiagnostics(FDiagnostics& Diag, class FSocket* Client)
{
	if (DebugServer == nullptr)
		return;
	...
	if (Client == nullptr)
		DebugServer->SendMessageToAll(EDebugMessageType::Diagnostics, Message);
	else
		DebugServer->SendMessageToClient(Client, EDebugMessageType::Diagnostics, Message);
	// ★ 编译诊断不是 IDE 本地私有状态，而是服务器协议的一部分
}

bool FAngelscriptEngine::TryBreakpointAngelscriptDebugging(const TCHAR* Message)
{
	...
	Manager.DebugServer->PauseExecution(&StopMessage);
	return true;
	// ★ 断点暂停由 Engine 主动切回 DebugServer，而不是依赖外部运行时默认行为
}
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 调试协议归属 | UnrealCSharp 通过 Mono `debugger-agent` 暴露标准调试 socket；Angelscript 自定义 `DebugServer V2`、消息枚举、消息封装和服务端状态机 | 实现方式不同 |
| UE 语义是否进入调试层 | UnrealCSharp 在本轮定位到的链路里没有同级别的 `AssetDatabase / CreateBlueprint / Diagnostics` 自定义协议面；Angelscript 把这些 UE 语义直接纳入调试协议 | 当前路径上部分没有实现 |
| 高级断点能力 | UnrealCSharp 当前证据链主要是 Mono soft breakpoints；Angelscript 额外实现了 Windows 硬件 data breakpoints 和脚本行级暂停控制 | 实现质量差异，能力面更宽 |

---

## 深化分析 (2026-04-09 07:41:47)

### [维度 D4] 热重载仲裁归属：editor-event compile debounce vs engine-tick queued reload

这一轮沿着 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:22-63`、`:144-159`、`:306-391` 继续往下看，可以确认 UnrealCSharp 的热重载主控权仍然牢牢放在 **Editor 事件面**。`DirectoryWatcher`、`AssetRegistry`、`PIE` 和 `Slate` 激活状态都在 `FEditorListener` 里汇流；`.cs` 文件改动先进入 `FileChanges` 缓冲，只有编辑器重新激活且当前不在 PIE / generator 阶段时才触发 `FCSharpCompiler::Compile(FileChanges)`。资产侧改动则在同一监听器里直接包裹 `FGeneratorCore::BeginGenerator(false) -> InGenerator() -> FCSharpCompiler::Compile() -> EndGenerator(false)`。这条路径的核心不是“即时编译”，而是**让 Editor 会话自己决定何时可以安全编译**。

Angelscript 这一侧，`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp:43-89` 只负责把 `.as` 文件与目录级变化写进 `FAngelscriptEngine` 的 `FileChangesDetectedForReload / FileDeletionsDetectedForReload` 队列；真正是否执行热重载、执行 `SoftReloadOnly` 还是 `FullReload`，是 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2743-2779`、`:2794-2829` 和 `:3936-3999` 在 engine tick 与 class generation 阶段决定的。更关键的是，Angelscript 会把 `ErrorNeedFullReload` 与 `PartiallyHandled` 的文件重新写入 `QueuedFullReloadFiles`（`AngelscriptEngine.cpp:4168-4186`），也就是**失败与延后重载本身也是 engine-owned state**，而不是 editor listener 一次性消费完就结束。

```
[D4] Hot Reload Arbitration

[UnrealCSharp]
DirectoryWatcher / AssetRegistry / PIE
├─ FEditorListener collects file deltas        // 编辑器事件扇入
├─ gate: !bIsPIEPlaying && !bIsGenerating      // PIE / 生成期禁入
├─ editor activated -> FCSharpCompiler::Compile
└─ asset change -> BeginGenerator -> Compile   // 资产改动直接生成+编译

[Angelscript]
DirectoryWatcher
├─ queue .as / folder deltas into Engine       // 只入队，不立即编译
├─ Engine::Tick chooses Soft vs Full reload    // 由世界状态仲裁
├─ ClassGenerator computes reload requirement  // 软/全量/错误
└─ queue retry / full reload after PIE         // 失败与延后重载有显式记忆
```

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:22-63, 144-159, 306-391`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp
// 函数: FEditorListener::FEditorListener / OnPreBeginPIE / OnApplicationActivationStateChanged / OnDirectoryChanged / OnAssetChanged
// 位置: 22-63, 144-159, 306-391
// 说明: 热重载入口完全挂在 editor 事件面，编译时机由监听器节流
// ============================================================================
if (!IsRunningCookCommandlet())
{
	OnPreBeginPIEDelegateHandle = FEditorDelegates::PreBeginPIE.AddRaw(this, &FEditorListener::OnPreBeginPIE);
	OnPrePIEEndedDelegateHandle = FEditorDelegates::PrePIEEnded.AddRaw(this, &FEditorListener::OnPrePIEEnded);
	OnCancelPIEDelegateHandle = FEditorDelegates::CancelPIE.AddRaw(this, &FEditorListener::OnCancelPIE);

	OnBeginGeneratorDelegateHandle = FUnrealCSharpCoreModuleDelegates::OnBeginGenerator.AddRaw(
		this, &FEditorListener::OnBeginGenerator);
	OnEndGeneratorDelegateHandle = FUnrealCSharpCoreModuleDelegates::OnEndGenerator.AddRaw(
		this, &FEditorListener::OnEndGenerator);
	OnCompileDelegateHandle = FUnrealCSharpCoreModuleDelegates::OnCompile.AddRaw(
		this, &FEditorListener::OnCompile);

	AssetRegistryModule.Get().OnFilesLoaded().AddRaw(this, &FEditorListener::OnFilesLoaded);

	for (const auto& Directory : FUnrealCSharpFunctionLibrary::GetChangedDirectories())
	{
		DirectoryWatcherModule.Get()->RegisterDirectoryChangedCallback_Handle(
			Directory,
			IDirectoryWatcher::FDirectoryChanged::CreateRaw(this, &FEditorListener::OnDirectoryChanged),
			OnDirectoryChangedDelegateHandle,
			IDirectoryWatcher::WatchOptions::IncludeDirectoryChanges
		);
	}
}

void FEditorListener::OnPreBeginPIE(const bool bIsSimulating)
{
	bIsPIEPlaying = true;

	while (FCSharpCompiler::Get().IsCompiling())
	{
		// ★ PIE 之前先把当前编译 drain 掉，避免进入运行态后还有悬空编译
		FThreadHeartBeat::Get().HeartBeat();
		FPlatformProcess::SleepNoStats(0.0001f);
		FTSTicker::GetCoreTicker().Tick(FApp::GetDeltaTime());
		FThreadManager::Get().Tick();
		FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
	}
}

void FEditorListener::OnApplicationActivationStateChanged(const bool IsActive)
{
	if (IsActive && !FileChanges.IsEmpty())
	{
		if (!bIsPIEPlaying && !bIsGenerating)
		{
			FCSharpCompiler::Get().Compile(FileChanges); // ★ 不是立刻编译，而是等 editor 激活再消费缓冲
			FileChanges.Reset();
		}
	}
}

void FEditorListener::OnDirectoryChanged(const TArray<FFileChangeData>& InFileChanges)
{
	if (UnrealCSharpEditorSetting->EnableDirectoryChanged() && !bIsGenerating)
	{
		for (const auto& FileChange : InFileChanges)
		{
			if (FPaths::GetExtension(FileChange.Filename) == TEXT("cs"))
			{
				// ★ proxy / obj 被过滤，真正进入编译队列的是作者写的 .cs 文件
				FileChanges.Add(FileChange);
			}
		}
	}
}

void FEditorListener::OnAssetChanged(const FAssetData& InAssetData, const TFunction<void()>& InGenerator) const
{
	if (UnrealCSharpEditorSetting->EnableAssetChanged() && !bIsPIEPlaying && !bIsGenerating)
	{
		FGeneratorCore::BeginGenerator(false);

		if (FGeneratorCore::IsSupported(InAssetData))
		{
			InGenerator();
			FCSharpCompiler::Get().Compile(); // ★ 资产变化会直接驱动声明生成 + 编译
		}

		FGeneratorCore::EndGenerator(false);
	}
}
```

关键源码 [2] `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp:43-89`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2743-2770, 2794-2829, 3936-3999, 4168-4186`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp
// 函数: AngelscriptEditor::Private::QueueScriptFileChanges
// 位置: 43-89
// 说明: Editor watcher 只把变化写入 engine 队列，不直接编译
// ============================================================================
void QueueScriptFileChanges(const TArray<FFileChangeData>& Changes, const TArray<FString>& RootPaths, FAngelscriptEngine& Engine, IFileManager& FileManager, const FEnumerateLoadedScripts& EnumerateLoadedScripts)
{
	for (const FFileChangeData& Change : Changes)
	{
		...
		Engine.LastFileChangeDetectedTime = FPlatformTime::Seconds();

		if (AbsolutePath.EndsWith(TEXT(".as")))
		{
			if (Change.Action == FFileChangeData::EFileChangeAction::FCA_Removed)
				Engine.FileDeletionsDetectedForReload.AddUnique({ AbsolutePath, RelativePath });
			else
				Engine.FileChangesDetectedForReload.AddUnique({ AbsolutePath, RelativePath });

			UE_LOG(Angelscript, Log, TEXT("Queued script file change for primary engine reload: %s"), *RelativePath);
			continue;
		}

		// ★ 目录级改动会展开成脚本文件集合后再入队
		...
	}
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: FAngelscriptEngine::CheckForHotReload / Tick / CompileModule
// 位置: 2743-2770, 2794-2829, 3936-3999, 4168-4186
// 说明: engine tick 决定 reload 类型，并显式记住需要后补的 full reload
// ============================================================================
FileList.Append(FileChangesDetectedForReload);
FileChangesDetectedForReload.Empty();

if (FileList.Num() != 0 || FPlatformTime::Seconds() - LastFileChangeDetectedTime > 0.2)
{
	for (const auto& DeletedFile : FileDeletionsDetectedForReload)
		FileList.AddUnique(DeletedFile);
	FileDeletionsDetectedForReload.Empty();
}

if (CompileType != ECompileType::SoftReloadOnly)
{
	for (const auto& QueuedFile : QueuedFullReloadFiles)
		FileList.AddUnique(QueuedFile);
	QueuedFullReloadFiles.Empty();
}

if (FileList.Num() != 0)
{
	PerformHotReload(CompileType, FileList); // ★ 真正编译发生在 engine 主循环消费队列时
}

void FAngelscriptEngine::Tick(float DeltaTime)
{
	...
	if (!GIsEditor || HasGameWorld())
	{
		CheckForHotReload(ECompileType::SoftReloadOnly); // ★ PIE / game world 期间强制 soft reload
	}
	else
	{
		CheckForHotReload(ECompileType::FullReload);
	}
}

switch (ReloadReq)
{
	case FAngelscriptClassGenerator::EReloadRequirement::SoftReload:
		SwapInModules(CompiledModules, DiscardedModules);
		ClassGenerator.PerformSoftReload();
		break;
	case FAngelscriptClassGenerator::EReloadRequirement::FullReloadSuggested:
		if (CompileType == ECompileType::SoftReloadOnly)
		{
			bWasFullyHandled = false;
			SwapInModules(CompiledModules, DiscardedModules);
			ClassGenerator.PerformSoftReload(); // ★ 先软切，PIE 结束后再补 full reload
		}
		else
		{
			SwapInModules(CompiledModules, DiscardedModules);
			ClassGenerator.PerformFullReload();
		}
		break;
	case FAngelscriptClassGenerator::EReloadRequirement::FullReloadRequired:
		if (CompileType == ECompileType::SoftReloadOnly)
		{
			bShouldSwapInModules = false;
			bFullReloadRequired = true; // ★ 不能安全 full reload 时保持旧代码继续运行
		}
		else
		{
			SwapInModules(CompiledModules, DiscardedModules);
			ClassGenerator.PerformFullReload();
		}
		break;
}

if (Result == ECompileResult::ErrorNeedFullReload)
{
	for (const auto& RepeatFile : AllCompiledFiles)
		QueuedFullReloadFiles.Add(RepeatFile); // ★ 需要后补的文件集合会被记住

	PreviouslyFailedReloadFiles.Append(AllCompiledFiles);
}
else if (Result == ECompileResult::PartiallyHandled)
{
	for (const auto& RepeatFile : AllCompiledFiles)
		QueuedFullReloadFiles.Add(RepeatFile);
}
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 重载仲裁落点 | UnrealCSharp 由 `FEditorListener` 统一汇聚 `DirectoryWatcher / AssetRegistry / PIE / Slate activation` 后再决定编译；Angelscript watcher 只入队，真正仲裁在 `FAngelscriptEngine::Tick()` | 实现方式不同 |
| PIE 期间行为 | UnrealCSharp 在 `OnPreBeginPIE()` 里等待当前编译 drain 完成，并在 `!bIsPIEPlaying` 前提下才允许资产触发生成/编译；Angelscript 在 game world / PIE 期间仍会检查热重载，但强制降为 `SoftReloadOnly` | 实现方式不同 |
| 失败恢复策略 | UnrealCSharp 当前证据链更像 editor 侧即时节流；Angelscript 会把 `ErrorNeedFullReload / PartiallyHandled` 文件回写到 `QueuedFullReloadFiles`，等待后续可执行 full reload 的时机 | 实现质量差异，Angelscript 的失败记忆更完整 |

### [维度 D7] 编辑器动作入口归属：workspace-native authoring surface vs operational extension mesh

这一维本轮不再重复前面已经写过的 “Content Browser 对象语义” 结论，而是往前追动作入口本身。UnrealCSharp 在 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:102-106, 173-190, 237-309` 与 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:60-85, 523-529, 704-760, 824-840` 的组合里，把“生成、浏览、打开、创建动态类”都直接放进 `DynamicData` 这条 authoring surface：Editor module 启动时构造并激活数据源；数据源给 `ContentBrowser.AddNewContextMenu` 动态加节；`EditItem()` 直接 `OpenSourceFile()`；generator 完成后 `OnEndGenerator()` 刷新虚拟树；`CreateFileItem()` 则把脚本类映射成真正的内容浏览器文件项。再叠加 `UnrealCSharpBlueprintToolBar.cpp:50-67, 176-200` 的 Blueprint toolbar 动作，作者日常工作面基本收束在 Content Browser 与 Blueprint editor 两个现成入口。

Angelscript 不是没有数据源，`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:111-119` 也会激活 `AngelscriptData`。但继续追下去会发现，它的主战场并不止于“把脚本显示成文件项”，而是把 editor surface 进一步变成可脚本化的 **extension mesh**。`ScriptEditorMenuExtension.cpp:25-43` 把 extension 注册绑定到脚本 reload 生命周期；`:845-1110` 则从所有 `UASClass` 中扫描继承 `UScriptEditorMenuExtension` 的脚本类，并把它们接入 `LevelViewport`、`ContentBrowser` 和 `ToolMenu` 多种入口。再加上 `AngelscriptSourceCodeNavigation.cpp:6-138` 的导航 handler，以及 `BlueprintImpactScanCommandlet.cpp:55-120` 的命令式分析入口，Angelscript 的 editor 集成更像“让脚本去改造 editor 行为”和“把分析能力外发给 commandlet / IDE”，而不仅是提供一个代码文件树。

```
[D7] Editor Entry Surface

[UnrealCSharp]
EditorModule
├─ create/activate DynamicData source           // 代码文件进入 Content Browser
├─ AddNewContextMenu -> New Dynamic Class       // 新建入口挂在内容浏览器
├─ EditItem -> OpenSourceFile                   // 编辑即打开源文件
└─ Blueprint toolbar -> Open / Analyze / Override

[Angelscript]
EditorModule
├─ activate AngelscriptData source              // 同样接入内容浏览器
├─ RegisterToolsMenuEntries                     // Tools/Programming 操作入口
├─ ScriptEditorMenuExtension registry           // 脚本类扩展 Level/ContentBrowser/ToolMenus
├─ SourceCodeNavigation handler                 // UASClass/UASFunction 直跳脚本行
└─ BlueprintImpact commandlet                   // 编辑器外部批处理分析
```

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:102-106, 173-190, 237-309`、`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:60-85, 523-529, 704-760, 824-840`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp
// 函数: FUnrealCSharpEditorModule::StartupModule / Tick / Generator
// 位置: 102-106, 173-190, 237-309
// 说明: Editor module 把生成流程和 DynamicData 数据源串成同一 authoring surface
// ============================================================================
UpdatePackagingSettings();

DynamicDataSource.Reset(NewObject<UDynamicDataSource>(GetTransientPackage(), "DynamicData"));
DynamicDataSource->Initialize();

void FUnrealCSharpEditorModule::Tick(const float InDeltaTime)
{
	if (!bHasTicked)
	{
		bHasTicked = true;
		IContentBrowserDataModule::Get().GetSubsystem()->ActivateDataSource("DynamicData");
		// ★ 第一次 tick 才真正把脚本工作区接入 Content Browser
	}
}

void FUnrealCSharpEditorModule::Generator()
{
	FUnrealCSharpCoreModuleDelegates::OnBeginGenerator.Broadcast();
	...
	FCSharpCompiler::Get().ImmediatelyCompile();
	FUnrealCSharpCoreModuleDelegates::OnEndGenerator.Broadcast(); // ★ 工具条 / 数据源都靠这个事件刷新
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp
// 函数: UDynamicDataSource::Initialize / EditItem / PopulateAddNewContextMenu / OnEndGenerator / CreateFileItem
// 位置: 60-85, 523-529, 704-760, 824-840
// 说明: 代码文件被包装成 Content Browser 一等公民
// ============================================================================
OnEndGeneratorHandle = FUnrealCSharpCoreModuleDelegates::OnEndGenerator.AddUObject(
	this, &UDynamicDataSource::OnEndGenerator);

if (const auto Menu = UToolMenus::Get()->ExtendMenu("ContentBrowser.AddNewContextMenu"))
{
	Menu->AddDynamicSection(..., FNewToolMenuDelegate::CreateLambda(
		[WeakThis = TWeakObjectPtr<UDynamicDataSource>(this)](UToolMenu* InMenu)
		{
			if (WeakThis.IsValid())
			{
				WeakThis->PopulateAddNewContextMenu(InMenu); // ★ Add New 入口直接接到动态类创建
			}
		}));
}

bool UDynamicDataSource::EditItem(const FContentBrowserItemData& InItem)
{
	const auto ItemDataPayload = GetFileItemDataPayload(InItem);
	return ItemDataPayload
		? FSourceCodeNavigation::OpenSourceFile(ItemDataPayload->GetInternalPath().ToString())
		: false; // ★ 点击“编辑”就是打开实际 .cs 文件
}

void UDynamicDataSource::PopulateAddNewContextMenu(UToolMenu* InMenu)
{
	...
	FDynamicNewClassContextMenu::MakeContextMenu(InMenu, SelectedClassPaths, OnOpenNewDynamicClassRequested);
}

void UDynamicDataSource::OnEndGenerator()
{
	UpdateHierarchy(); // ★ generator 完成后刷新虚拟树，而不是等用户手动刷新
}

FContentBrowserItemData UDynamicDataSource::CreateFileItem(UClass* InClass)
{
	return FContentBrowserItemData(
		this,
		EContentBrowserItemFlags::Type_File | EContentBrowserItemFlags::Category_Class,
		*GetVirtualPath(InClass),
		InClass->GetFName(),
		FText(),
		MakeShared<FDynamicFileItemDataPayload>(*FDynamicGenerator::GetDynamicNormalizeFile(InClass), InClass)
	);
}
```

关键源码 [2] `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicNewClassContextMenu.cpp:46-58`、`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:50-67, 176-200`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicNewClassContextMenu.cpp
// 函数: FDynamicNewClassContextMenu::MakeContextMenu
// 位置: 46-58
// 说明: 新建动态类本身就是 Content Browser 的原生动作
// ============================================================================
auto& Section = InMenu->AddSection("ContentBrowserNewClass",
                                   LOCTEXT("ClassMenuHeading", "New Dynamic Class"));
Section.AddMenuEntry(
	"NewClass",
	LOCTEXT("NewClassLabel", "New dynamic Class..."),
	NewClassToolTip,
	FSlateIcon(FUnrealCSharpEditorStyle::GetStyleSetName(), "UnrealCSharpEditor.PluginAction"),
	FUIAction(
		FExecuteAction::CreateStatic(&FDynamicNewClassContextMenu::ExecuteNewClass, ClassCreationPath,
		                             InOnOpenNewDynamicClassRequested),
		CanExecuteClassActionsDelegate
	)
);
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp
// 函数: FUnrealCSharpBlueprintToolBar::OnEndGenerator / BuildAction / GenerateBlueprintExtender
// 位置: 50-67, 176-200
// 说明: Blueprint editor 直接暴露 Open File / Code Analysis / Override Blueprint 动作
// ============================================================================
void FUnrealCSharpBlueprintToolBar::OnEndGenerator()
{
	SetCodeAnalysisOverrideFilesMap();
	FDynamicGenerator::SetCodeAnalysisDynamicFilesMap(); // ★ 生成结束后刷新 override 文件映射
}

CommandList->MapAction(
	FUnrealCSharpEditorCommands::Get().OpenFile,
	FExecuteAction::CreateLambda([this]
	{
		if (const auto OverrideFile = GetOverrideFile(); !OverrideFile.IsEmpty())
		{
			if (IFileManager::Get().FileExists(*OverrideFile))
			{
				FSourceCodeNavigation::OpenSourceFile(OverrideFile);
			}
		}
	}), FCanExecuteAction());

if (HasOverrideFile())
{
	MenuBuilder.AddMenuEntry(Commands.OpenFile, NAME_None, LOCTEXT("OpenFile", "Open File"));
	MenuBuilder.AddMenuEntry(Commands.CodeAnalysis, NAME_None, LOCTEXT("CodeAnalysis", "Code Analysis"));
}
else
{
	MenuBuilder.AddMenuEntry(Commands.OverrideBlueprint, NAME_None, LOCTEXT("OverrideBlueprint", "Override Blueprint"));
}
```

关键源码 [3] `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:111-119, 351-416, 696-745`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:25-43, 845-1110`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp:6-138`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp:55-120`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 函数: OnEngineInitDone / FAngelscriptEditorModule::StartupModule / RegisterToolsMenuEntries
// 位置: 111-119, 351-416, 696-745
// 说明: Angelscript 也接 Content Browser，但更大动作面在 Tools 菜单与 editor service
// ============================================================================
void OnEngineInitDone()
{
	auto* DataSource = NewObject<UAngelscriptContentBrowserDataSource>(GetTransientPackage(), "AngelscriptData", RF_MarkAsRootSet | RF_Transient);
	DataSource->Initialize();

	UContentBrowserDataSubsystem* ContentBrowserData = IContentBrowserDataModule::Get().GetSubsystem();
	ContentBrowserData->ActivateDataSource("AngelscriptData");
}

void FAngelscriptEditorModule::StartupModule()
{
	RegisterAngelscriptSourceNavigation();
	FCoreDelegates::OnPostEngineInit.AddStatic(&OnEngineInitDone);

	UScriptEditorMenuExtension::InitializeExtensions(); // ★ 脚本化 menu extension 的注册从这里起步
	...
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FAngelscriptEditorModule::RegisterToolsMenuEntries));
}

void FAngelscriptEditorModule::RegisterToolsMenuEntries()
{
	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("MainFrame.MainMenu.Tools");
	FToolMenuSection& Section = Menu->FindOrAddSection("Programming");

	Section.AddMenuEntry(
		"ASOpenCode",
		NSLOCTEXT("Angelscript", "OpenCode.Label", "Open Angelscript workspace (VS Code)"),
		NSLOCTEXT("Angelscript", "OpenCode.ToolTip", "Opens Visual Studio Code in this project's Angelscript workspace"),
		FSourceCodeNavigation::GetOpenSourceCodeIDEIcon(),
		Action);

	Section.AddMenuEntry(
		"Function Tests",
		NSLOCTEXT("Angelscript", "OpenCode.Label", "Run Function Tests"),
		NSLOCTEXT("Angelscript", "OpenCode.ToolTip", "Runs some Tests for debugging purposes"),
		FSourceCodeNavigation::GetOpenSourceCodeIDEIcon(),
		TestAction);
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp
// 函数: UScriptEditorMenuExtension::InitializeExtensions / RegisterExtensions
// 位置: 25-43, 845-1110
// 说明: editor 扩展入口可由脚本类自身声明，并跟随 reload 生命周期重建
// ============================================================================
void UScriptEditorMenuExtension::InitializeExtensions()
{
	FAngelscriptClassGenerator::OnPostReload.AddLambda([](bool bFullReload)
	{
		if (bFullReload)
		{
			UScriptEditorMenuExtension::UnregisterExtensions();
			UScriptEditorMenuExtension::RegisterExtensions();
			// ★ 不是模块里手工列菜单，而是 full reload 后从脚本类重新扫描
		}
	});

	if (FAngelscriptEngine::IsInitialized() && FAngelscriptEngine::Get().IsInitialCompileFinished())
	{
		UScriptEditorMenuExtension::RegisterExtensions();
	}
}

void UScriptEditorMenuExtension::RegisterExtensions()
{
	for (TObjectIterator<UASClass> ScriptClass; ScriptClass; ++ScriptClass)
	{
		if (!ScriptClass->IsChildOf(UScriptEditorMenuExtension::StaticClass()))
			continue;
		...

		switch (CDO->ExtensionMenu)
		{
		case EScriptEditorMenuExtensionLocation::LevelViewport_ContextMenu:
			LevelEditorModule.GetAllLevelViewportContextMenuExtenders().Add(...);
			break;
		...
		case EScriptEditorMenuExtensionLocation::ToolMenu:
		{
			auto* Menu = UToolMenus::Get()->ExtendMenu(Registered.ExtensionPoint);
			FToolMenuSection& Section = Menu->AddSection(...);
			Section.AddDynamicEntry(Registered.SectionName, FNewToolMenuSectionDelegate::CreateLambda([CDO, Menu](FToolMenuSection& DynamicSection)
			{
				bool bIsMenu = (Menu->MenuType == EMultiBoxType::MenuBar) || (Menu->MenuType == EMultiBoxType::Menu);
				CDO->BuildToolMenuSection(DynamicSection, FExtenderSelection(), bIsMenu);
				// ★ 同一脚本类型既能接 LevelViewport，也能接 ToolMenu / ContentBrowser
			}));
		}
		break;
		}
	}
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp
// 函数: FAngelscriptSourceCodeNavigation::NavigateToClass / NavigateToFunction / RegisterAngelscriptSourceNavigation
// 位置: 6-138
// 说明: 编辑器导航直接按脚本模块与行号跳 VS Code
// ============================================================================
class FAngelscriptSourceCodeNavigation : public ISourceCodeNavigationHandler
{
	virtual bool NavigateToClass(const UClass* InClass) override
	{
		...
		OpenModule(Module, ClassDesc->LineNumber);
		return true;
	}

	virtual bool NavigateToFunction(const UFunction* InFunction) override
	{
		auto* ASFunc = Cast<const UASFunction>(InFunction);
		...
		OpenFile(Path, ASFunc->GetSourceLineNumber());
		return true;
	}
};

void RegisterAngelscriptSourceNavigation()
{
	FSourceCodeNavigation::AddNavigationHandler(new FAngelscriptSourceCodeNavigation);
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp
// 函数: UAngelscriptBlueprintImpactScanCommandlet::Main
// 位置: 55-120
// 说明: 一部分 editor 能力被设计成 commandlet，可脱离交互式 UI 运行
// ============================================================================
int32 UAngelscriptBlueprintImpactScanCommandlet::Main(const FString& Params)
{
	if (!FAngelscriptEngine::Get().bDidInitialCompileSucceed)
	{
		return static_cast<int32>(EBlueprintImpactCommandletExitCode::EngineNotReady);
	}

	...
	const AngelscriptEditor::BlueprintImpact::FBlueprintImpactScanResult ScanResult =
		AngelscriptEditor::BlueprintImpact::ScanBlueprintAssets(
			FAngelscriptEngine::Get(),
			AssetRegistryModule.Get(),
			Request);

	return ScanResult.FailedAssetLoads > 0
		? static_cast<int32>(EBlueprintImpactCommandletExitCode::AssetScanFailure)
		: static_cast<int32>(EBlueprintImpactCommandletExitCode::Success);
	// ★ 这里的 editor contract 已经超出 UI 菜单，变成可批处理的分析入口
}
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 代码作者入口 | UnrealCSharp 把 “创建/浏览/编辑动态代码” 收敛到 `DynamicData` + Blueprint toolbar；Angelscript 虽有 `AngelscriptData`，但主动作面还包括 Tools 菜单、导航 handler 和 commandlet | 实现方式不同 |
| reload 后 UI 重建 | UnrealCSharp 的数据源和 toolbar 主要靠 `OnEndGenerator` 刷新；Angelscript 的脚本菜单扩展显式绑定 `OnPostReload`，full reload 后会重新注册所有脚本扩展 | 实现质量差异，Angelscript 的扩展重建合同更明确 |
| 编辑器能力外发 | UnrealCSharp 当前证据链更偏交互式 authoring；Angelscript 把 Blueprint impact 这种 editor 分析能力做成 `UCommandlet` | 当前路径上 UnrealCSharp 没有等价实现 |

### [维度 D9] 测试合同落点：metadata-level self-checks vs first-class runtime/editor test planes

这一维我刻意不重复前面“有没有 `AngelscriptTest` 模块”的基础结论，而是把问题换成：**测试语义到底落在哪一层**。就当前 `Reference/UnrealCSharp` 快照可见源码而言，`UnrealCSharp.uplugin:18-53` 暴露的是 `Runtime / Editor / ScriptCodeGenerator / Compiler / Core / CrossVersion / Program`，没有单独的 `Test` 模块。与此同时，我能直接落到源码行号的 test 语义，主要是 `Script/UE/Dynamic/Property/IgnoreForMemberInitializationTestAttribute.cs:5-8` 这个 attribute；它会在 `FReflectionRegistry.cpp:584-585, 1964-1966` 被缓存为一个普通 reflection class，并在 `FDynamicGeneratorCore.cpp:1170-1192` 与 `HideInDetailPanel / InlineEditConditionToggle / MetaClass` 等编辑器/元数据 attribute 一起被收进同一个 attribute 集合。也就是说，在当前可见证据里，UnrealCSharp 的 test 语义更像是**生成器内部某类成员初始化校验的例外开关**，而不是单独的执行平面。

Angelscript 则把测试直接建成插件构成的一部分。`Plugins/Angelscript/Angelscript.uplugin:18-33` 显式声明 `AngelscriptTest` 模块；`AngelscriptTest.Build.cs:23-49` 又把它连到 `AngelscriptRuntime`、`AngelscriptEditor`、`CQTest`、`Networking`、`Sockets` 和 `UnrealEd`。更重要的是，测试并不只是“有很多 `.cpp` 文件”这么简单。`Plugins/Angelscript/Source/AngelscriptRuntime/Testing/UnitTest.cpp:531-654` 显示 runtime 自己内建了 `FHotReloadTestRunner`，会在 hot reload 后按照依赖邻近度与 batch 大小执行模块级 unit tests；而 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactScannerTests.cpp:337-349` 又证明 editor commandlet 的返回码也有专门 automation test 覆盖。换句话说，Angelscript 把测试合同下沉到了 runtime reload、editor commandlet 和独立 test module 三个平面。

```
[D9] Test Plane Ownership

[UnrealCSharp]
Managed attribute
├─ IgnoreForMemberInitializationTestAttribute
├─ ReflectionRegistry caches attribute class
└─ DynamicGenerator treats it as metadata input   // 测试语义内嵌在生成管线

[Angelscript]
Plugin manifest
├─ AngelscriptTest module                         // 独立测试模块
├─ Runtime Testing / hot-reload batch runner      // 热重载后自动回归
├─ Editor commandlet tests                        // 编辑器命令面单测
└─ CQTest / Automation / native helpers           // 多平面验证
```

关键源码 [1] `Reference/UnrealCSharp/UnrealCSharp.uplugin:18-53`、`Reference/UnrealCSharp/Script/UE/Dynamic/Property/IgnoreForMemberInitializationTestAttribute.cs:5-8`、`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Reflection/FReflectionRegistry.cpp:584-585, 1964-1966`、`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGeneratorCore.cpp:1170-1192`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/UnrealCSharp.uplugin
// 位置: 18-53
// 说明: 当前模块清单里没有独立 Test 模块
// ============================================================================
"Modules": [
	{ "Name": "UnrealCSharp", "Type": "Runtime" },
	{ "Name": "UnrealCSharpEditor", "Type": "Editor" },
	{ "Name": "ScriptCodeGenerator", "Type": "Editor" },
	{ "Name": "Compiler", "Type": "Editor" },
	{ "Name": "UnrealCSharpCore", "Type": "Runtime" },
	{ "Name": "CrossVersion", "Type": "Runtime" },
	{ "Name": "SourceCodeGenerator", "Type": "Program" }
]
```

```csharp
// ============================================================================
// 文件: Reference/UnrealCSharp/Script/UE/Dynamic/Property/IgnoreForMemberInitializationTestAttribute.cs
// 位置: 5-8
// 说明: 我在当前快照里直接定位到的显式 test 语义是这个 attribute
// ============================================================================
[AttributeUsage(AttributeTargets.Property)]
public class IgnoreForMemberInitializationTestAttribute : Attribute
{
    private string Value { get; set; } = "true";
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Reflection/FReflectionRegistry.cpp
// 函数: FReflectionRegistry::FReflectionRegistry / GetIgnoreForMemberInitializationTestAttributeClass
// 位置: 584-585, 1964-1966
// 说明: test attribute 被当成普通 reflection class 缓存
// ============================================================================
IgnoreForMemberInitializationTestAttributeClass = GetClass(
	COMBINE_NAMESPACE(NAMESPACE_ROOT, NAMESPACE_DYNAMIC), CLASS_IGNORE_FOR_MEMBER_INITIALIZATION_TEST_ATTRIBUTE);

FClassReflection* FReflectionRegistry::GetIgnoreForMemberInitializationTestAttributeClass() const
{
	return IgnoreForMemberInitializationTestAttributeClass;
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGeneratorCore.cpp
// 位置: 1170-1192
// 说明: test attribute 与其他 metadata attribute 一起进入生成器 attribute 集
// ============================================================================
{
	ReflectionRegistry.GetExposeFunctionCategoriesAttributeClass(),
	ReflectionRegistry.GetExposeOnSpawnAttributeClass(),
	ReflectionRegistry.GetFilePathFilterAttributeClass(),
	ReflectionRegistry.GetRelativeToGameDirAttributeClass(),
	ReflectionRegistry.GetFixedIncrementAttributeClass(),
	ReflectionRegistry.GetForceShowEngineContentAttributeClass(),
	ReflectionRegistry.GetForceShowPluginContentAttributeClass(),
	ReflectionRegistry.GetHideAlphaChannelAttributeClass(),
	ReflectionRegistry.GetHideInDetailPanelAttributeClass(),
	ReflectionRegistry.GetHideViewOptionsAttributeClass(),
	ReflectionRegistry.GetIgnoreForMemberInitializationTestAttributeClass(),
	ReflectionRegistry.GetInlineEditConditionToggleAttributeClass(),
	...
};
// ★ 这里的 “test” 没有形成独立 runner，而是作为生成管线里的属性例外规则存在
```

关键源码 [2] `Plugins/Angelscript/Angelscript.uplugin:18-33`、`Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs:23-49`、`Plugins/Angelscript/Source/AngelscriptRuntime/Testing/UnitTest.cpp:531-654`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactScannerTests.cpp:337-349`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Angelscript.uplugin
// 位置: 18-33
// 说明: Test 模块就是插件正式模块的一部分
// ============================================================================
"Modules": [
	{ "Name": "AngelscriptRuntime", "Type": "Runtime", "LoadingPhase": "PostDefault" },
	{ "Name": "AngelscriptEditor", "Type": "Editor", "LoadingPhase": "PostDefault" },
	{ "Name": "AngelscriptTest", "Type": "Editor", "LoadingPhase": "PostDefault" }
]
```

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs
// 位置: 23-49
// 说明: 测试模块同时接 runtime / editor / CQTest
// ============================================================================
PublicDependencyModuleNames.AddRange(new string[]
{
	"Core",
	"CoreUObject",
	"Engine",
	"GameplayTags",
	"Json",
	"JsonUtilities",
	"AngelscriptRuntime",
});

if (Target.bBuildEditor)
{
	PrivateDependencyModuleNames.AddRange(new string[]
	{
		"CQTest",
		"Networking",
		"Sockets",
		"UnrealEd",
		"AngelscriptEditor",
	});
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Testing/UnitTest.cpp
// 函数: FHotReloadTestRunner::PrepareTests / RunTests
// 位置: 531-654
// 说明: hot reload 后的验证不是人工流程，而是 runtime 内建 runner
// ============================================================================
void FHotReloadTestRunner::PrepareTests(...)
{
	if (!ShouldRunUnitTestsOnHotReload())
		return;

	// ★ 编译完成后先规划哪些模块的 unit tests 需要在 hot reload 后跑
	for (const TSharedRef<FAngelscriptModuleDesc>& Module : ModulesToCompile)
	{
		if (Module->UnitTestFunctions.Num() > 0)
		{
			TestAfterHotReload.Add(Module);
		}
	}
}

bool FHotReloadTestRunner::RunTests(FAngelscriptEngine* AngelscriptManager)
{
	if (!ShouldRunUnitTestsOnHotReload())
		return true;

	if (TestAfterHotReload.Num() > 0)
	{
		// ★ 为了规避大批量测试把 editor 卡死，按 batch 计划并在批次间允许 tick / GC
		int TestsPerBatch = GetDefault<UAngelscriptTestSettings>()->GarbageCollectEveryNTests;
		...
		AllUnitTestsPass = RunAngelscriptUnitTests(TestBatch, AngelscriptManager, CurrentBatchOnHotReload, TotalBatchesOnHotReload);
		++CurrentBatchOnHotReload;
	}

	return AllUnitTestsPass;
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactScannerTests.cpp
// 函数: FAngelscriptBlueprintImpactCommandletInvalidFileTest::RunTest
// 位置: 337-349
// 说明: editor commandlet 行为也有专门 automation test 覆盖
// ============================================================================
bool FAngelscriptBlueprintImpactCommandletInvalidFileTest::RunTest(const FString& Parameters)
{
	UAngelscriptBlueprintImpactScanCommandlet* Commandlet = NewObject<UAngelscriptBlueprintImpactScanCommandlet>();
	if (!TestNotNull(TEXT("BlueprintImpact.CommandletInvalidFile should create the commandlet object"), Commandlet))
	{
		return false;
	}

	return TestEqual(
		TEXT("BlueprintImpact.CommandletInvalidFile should return the invalid-arguments exit code for a missing ChangedScriptFile"),
		Commandlet->Main(TEXT("ChangedScriptFile=J:/Missing/DoesNotExist.txt")),
		1);
}
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 测试合同所在层 | UnrealCSharp 当前可见源码里的 test 语义主要体现为 `IgnoreForMemberInitializationTestAttribute` 这类生成器内部例外规则；Angelscript 把 `AngelscriptTest` 做成正式模块 | 实现方式不同 |
| 热重载回归执行 | UnrealCSharp 本轮未定位到等价的 hot reload test runner；Angelscript runtime 直接在 `Tick()` 路径旁挂 `FHotReloadTestRunner` 的批次执行模型 | 当前路径上 UnrealCSharp 没有实现 |
| editor 工具验证 | UnrealCSharp 本轮证据链里未见等价 commandlet automation test；Angelscript 的 `BlueprintImpactScanCommandlet` 有直接返回码测试 | 当前路径上 UnrealCSharp 没有实现 |

---
## 深化分析 (2026-04-09 07:58:25)

### [维度 D1] 生成工具执行宿主：spawned Roslyn CLI vs UHT-hosted plugin assembly

前文已经把 `Runtime / Editor / Core / Compiler` 的模块切分写过了。这一轮不重复“模块有几个”，只追一个更底层的问题：**生成工具到底寄生在哪个执行宿主里**。继续往下看源码会发现，UnrealCSharp 的 authoring toolchain 并不完全停留在 UE 进程内部。`FUnrealCSharpEditorModule::Generator()` 先调 `FSolutionGenerator::Generator()`，把 `CodeAnalysis / SourceGenerator / Weavers` 三套 C# 工程从模板落到 `Script/` 工作树；随后 `FCodeAnalysis::Compile()` 显式 `dotnet build` 这个生成出来的 `CodeAnalysis.csproj`，`FCodeAnalysis::Analysis()` 再直接运行生成出的 `CodeAnalysis.exe`。也就是说，它的生成链是 **Editor 编排 + 外部 CLI 执行体**。

Angelscript 的 UHT 工具则落在另一侧。`AngelscriptUHTTool.ubtplugin.csproj` 直接引用 `EpicGames.UHT.dll` 与 `UnrealBuildTool.dll`，输出路径固定到 `Binaries/DotNET/UnrealBuildTool/Plugins/AngelscriptUHTTool/`；`AngelscriptFunctionTableExporter.cs` 用 `[UhtExporter(... CompileOutput ...)]` 把函数表生成器注册成 UHT exporter。换句话说，Angelscript 这条链路是 **UHT 进程内加载插件程序集并直接消费 `factory.Session.Modules`**，而不是像 UnrealCSharp 一样，每轮再派生并运行一个独立分析程序。

```
[D1] Tool Host Boundary

[UnrealCSharp]
UE Editor
├─ FUnrealCSharpEditorModule::Generator()          // 编辑器编排入口
├─ FSolutionGenerator::Generator()                 // 先重建 Script/CodeAnalysis / SourceGenerator / Weavers
├─ FCodeAnalysis::Compile()                        // dotnet build CodeAnalysis.csproj
└─ FCodeAnalysis::Analysis()                       // 运行 CodeAnalysis.exe 产出索引

[Angelscript]
UnrealHeaderTool
├─ load AngelscriptUHTTool.dll                     // UBT/UHT 插件程序集
├─ [UhtExporter CompileOutput]                     // 在 UHT 内注册导出器
└─ AngelscriptFunctionTableExporter::Export()      // 同一 UHT session 内写 .cpp / .json / .csv
```

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:68-100,237-250`、`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FSolutionGenerator.cpp:9-125`、`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FCodeAnalysis.cpp:5-89`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp
// 函数: FUnrealCSharpEditorModule::StartupModule / Generator
// 位置: 68-100, 237-250
// 说明: Editor 模块本身就是 toolchain orchestrator，直接暴露 CodeAnalysis / SolutionGenerator / Compile / Generator 四个入口
// ============================================================================
CodeAnalysisConsoleCommand = MakeUnique<FAutoConsoleCommand>(
	TEXT("UnrealCSharp.Editor.CodeAnalysis"), TEXT(""),
	FConsoleCommandDelegate::CreateLambda(
		[]()
		{
			FCodeAnalysis::CodeAnalysis();
		}));

SolutionGeneratorConsoleCommand = MakeUnique<FAutoConsoleCommand>(
	TEXT("UnrealCSharp.Editor.SolutionGenerator"), TEXT(""),
	FConsoleCommandDelegate::CreateLambda(
		[]()
		{
			FSolutionGenerator::Generator();
		}));

GeneratorConsoleCommand = MakeUnique<FAutoConsoleCommand>(
	TEXT("UnrealCSharp.Editor.Generator"), TEXT(""),
	FConsoleCommandDelegate::CreateLambda(
		[]()
		{
			Generator();
		}));

void FUnrealCSharpEditorModule::Generator()
{
	FUnrealCSharpCoreModuleDelegates::OnBeginGenerator.Broadcast();
	// ★ 全量生成前先重建工具工程，再进入后续 CodeAnalysis / Class / Struct / Enum / Binding 生成链
	FSolutionGenerator::Generator();
	...
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FSolutionGenerator.cpp
// 函数: FSolutionGenerator::Generator
// 位置: 9-125
// 说明: UnrealCSharp 不是只引用一套静态工具二进制，而是每轮先把工具工程源码落到 Script/
// ============================================================================
void FSolutionGenerator::Generator()
{
	const auto ScriptPath = FUnrealCSharpFunctionLibrary::GetPluginScriptDirectory();

	CopyTemplate(
		FUnrealCSharpFunctionLibrary::GetCodeAnalysisProjectPath(),
		ScriptPath / CODE_ANALYSIS_NAME / CODE_ANALYSIS_NAME + PROJECT_SUFFIX,
		{ &FSolutionGenerator::ReplaceTargetFramework, &FSolutionGenerator::AddProjectGeneratorHeaderComment });

	CopyTemplate(
		FPaths::Combine(FUnrealCSharpFunctionLibrary::GetSourceGeneratorPath(), SOURCE_GENERATOR_NAME + PROJECT_SUFFIX),
		ScriptPath / SOURCE_GENERATOR_NAME / SOURCE_GENERATOR_NAME + PROJECT_SUFFIX,
		{ &FSolutionGenerator::AddProjectGeneratorHeaderComment });

	CopyTemplate(
		FPaths::Combine(FUnrealCSharpFunctionLibrary::GetWeaversPath(), WEAVERS_NAME + PROJECT_SUFFIX),
		ScriptPath / WEAVERS_NAME / WEAVERS_NAME + PROJECT_SUFFIX,
		{ &FSolutionGenerator::ReplaceOutputPath, &FSolutionGenerator::AddProjectGeneratorHeaderComment });

	// ★ 这里的 CodeAnalysis / SourceGenerator / Weavers 都是作者期工具源码树，不是 runtime payload
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FCodeAnalysis.cpp
// 函数: FCodeAnalysis::CodeAnalysis / Compile / Analysis
// 位置: 5-89
// 说明: 代码分析明确跨出 UE 进程边界，先 build 再执行生成出来的 CodeAnalysis.exe
// ============================================================================
void FCodeAnalysis::CodeAnalysis()
{
	Compile();
	Analysis();
}

void FCodeAnalysis::Compile()
{
	static auto CompileTool = FUnrealCSharpFunctionLibrary::GetDotNet();

	const auto CompileParam = FString::Printf(TEXT(
		"build \"%s\" --nologo -c Debug"
	), *FUnrealCSharpFunctionLibrary::GetCodeAnalysisProjectPath());

	FUnrealCSharpFunctionLibrary::SyncProcess(CompileTool, CompileParam, [](const int32, const FString&) {});
	// ★ 先把 CodeAnalysis.csproj 编译成独立可执行工具
}

void FCodeAnalysis::Analysis()
{
	const auto Program = FPaths::Combine(
		FUnrealCSharpFunctionLibrary::GetCodeAnalysisCSProjPath(),
		FString::Printf(TEXT("%s%s"), *CODE_ANALYSIS_NAME, TEXT(".exe")));

	FUnrealCSharpFunctionLibrary::SyncProcess(Program, AnalysisParam, [](const int32, const FString&) {});
	// ★ 然后直接拉起 CodeAnalysis.exe 做全量/单文件语法分析
}
```

关键源码 [2] `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptUHTTool.ubtplugin.csproj:1-18,40-52`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:21-54`

```xml
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptUHTTool.ubtplugin.csproj
// 位置: 1-18, 40-52
// 说明: 这是给 UBT/UHT 装载的 .NET 插件程序集，不是运行时脚本交付物
// ============================================================================
<Project Sdk="Microsoft.NET.Sdk">
  <Import Project="$(EngineDir)\Source\Programs\Shared\UnrealEngine.csproj.props" />
  <PropertyGroup>
    <TargetFramework>net8.0</TargetFramework>
    <OutputType>Library</OutputType>
    <AssemblyName>AngelscriptUHTTool</AssemblyName>
    <OutputPath>..\..\Binaries\DotNET\UnrealBuildTool\Plugins\AngelscriptUHTTool\</OutputPath>
    <TreatWarningsAsErrors>true</TreatWarningsAsErrors>
  </PropertyGroup>

  <ItemGroup>
    <Reference Include="EpicGames.UHT">
      <HintPath>$(EngineDir)\Binaries\DotNET\UnrealBuildTool\EpicGames.UHT.dll</HintPath>
    </Reference>
    <Reference Include="UnrealBuildTool">
      <HintPath>$(EngineDir)\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.dll</HintPath>
    </Reference>
  </ItemGroup>
</Project>
```

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs
// 函数: Export(IUhtExportFactory factory)
// 位置: 21-54
// 说明: 生成器以 UHT exporter 身份运行在 UHT session 内，直接消费 Session.Modules
// ============================================================================
[UhtExporter(
	Name = "AngelscriptFunctionTable",
	Description = "Exports Angelscript function table data",
	Options = UhtExporterOptions.Default | UhtExporterOptions.CompileOutput,
	CppFilters = ["AS_FunctionTable_*.cpp"],
	ModuleName = "AngelscriptRuntime")]
private static void Export(IUhtExportFactory factory)
{
	int generatedFileCount = AngelscriptFunctionTableCodeGenerator.Generate(factory);

	foreach (UhtModule module in factory.Session.Modules)
	{
		CountBlueprintCallableFunctions(module.ShortName, module.ScriptPackage, skippedEntries, ...);
	}

	WriteSkippedEntriesCsv(factory, skippedEntries);
	WriteSkippedReasonSummaryCsv(factory, skippedEntries);
	// ★ 没有额外 CLI hop，直接在 UHT 当前会话里完成暴露统计与输出
}
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 生成工具宿主 | UnrealCSharp 当前链路是 `Editor -> dotnet build -> CodeAnalysis.exe`；Angelscript 当前链路是 `UHT -> AngelscriptUHTTool.dll` | 实现方式不同 |
| 工具源码来源 | UnrealCSharp 每轮先把 `CodeAnalysis / SourceGenerator / Weavers` 工程从模板落到 `Script/`；Angelscript UHT tool 源码固定在插件仓库并作为 `.ubtplugin` 编译 | 实现方式不同 |
| 进程边界 | UnrealCSharp 当前可见路径至少跨两个外部进程边界；Angelscript 当前证据链停留在同一 UHT session 内 | 实现方式不同 |

### [维度 D6] 索引失效策略：whole-output rewrite vs shard-level stale cleanup

前面的 D6 已经写过 `OverrideFile.json`、`DynamicFile.json` 和 IDE 导航。这一轮不再重复“有哪些索引文件”，只看 **这些索引文件是怎么失效和重建的**。UnrealCSharp 的 `CodeAnalysis.cs` 明确采用“完整快照重写”模型：单文件模式先读回 `Dynamic.json / DynamicFile.json / OverrideFunction.json / OverrideFile.json`，再把当前输入文件旧的 override owner 从 map 中移除；进入 `Analysis()` 后，只要输出目录存在，就先把目录里的所有文件删光，再重新分析输入文件集合，最后一次性回写四张 JSON 表。也就是说，UnrealCSharp 的 sidecar index 是 **目录级 snapshot**，而不是按文件分片独立生存。

Angelscript 的函数表生成器则是另一种策略。`AngelscriptFunctionTableCodeGenerator::Generate()` 先维护 `generatedPaths`，每个模块输出稳定命名的 `AS_FunctionTable_<Module>_<Shard>.cpp` 分片；完成后只在 `DeleteStaleOutputs()` 里删除那些“不再属于本轮生成集合”的旧 shard 文件。与此同时，summary/coverage 产物被显式拆成 `AS_FunctionTable_Summary.json`、`AS_FunctionTable_ModuleSummary.csv`、`AS_FunctionTable_Entries.csv`，exporter 再补 `SkippedEntries.csv` 与 `SkippedReasonSummary.csv`。所以 Angelscript 这条链不是“清空目录再重写一切”，而是 **分片产物按路径集合失效，统计表按本轮 session 重写**。

```
[D6] Index Invalidation Strategy

[UnrealCSharp]
CodeAnalysis single/full run
├─ single mode: load existing JSON maps
├─ remove old owner entry for changed file
├─ ensure output directory exists
├─ delete every file under output directory
├─ parse .cs syntax trees again
└─ rewrite Dynamic.json / DynamicFile.json / OverrideFunction.json / OverrideFile.json

[Angelscript]
UHT function-table generation
├─ GenerateModule() -> commit AS_FunctionTable_<Module>_<Shard>.cpp
├─ track generatedPaths
├─ DeleteStaleOutputs() -> delete stale shard cpp only
├─ rewrite Summary.json / ModuleSummary.csv / Entries.csv
└─ exporter rewrites SkippedEntries.csv / SkippedReasonSummary.csv
```

关键源码 [1] `Reference/UnrealCSharp/Script/CodeAnalysis/CodeAnalysis.cs:17-84,141-170,184-252,359-412`

```csharp
// ============================================================================
// 文件: Reference/UnrealCSharp/Script/CodeAnalysis/CodeAnalysis.cs
// 函数: CodeAnalysis(string[] args) / Analysis / AnalysisOverride / WriteAll
// 位置: 17-84, 141-170, 184-252, 359-412
// 说明: 单文件模式先回收旧 owner，再把整个输出目录当成完整快照重写
// ============================================================================
private CodeAnalysis(string[] args)
{
    _bIsSingle = bool.Parse(args[0]);

    if (_bIsSingle)
    {
        ...
        if (_overrideFunction != null && _overrideFile != null)
        {
            var Pair = _overrideFile.FirstOrDefault(pair => pair.Value == _inputFileName);
            if (Pair.Key != null)
            {
                _overrideFunction.Remove(Pair.Key);
                _overrideFile.Remove(Pair.Key);
                // ★ 当前文件旧的 override 归属会先被撤销
            }
        }
    }
}

private void Analysis()
{
    if (!Directory.Exists(_outputPathName))
    {
        Directory.CreateDirectory(_outputPathName);
    }

    foreach (var Item in Directory.GetFiles(_outputPathName))
    {
        File.Delete(Item);
        // ★ 全量模式不是“删部分”，而是直接清空输出目录
    }

    ...
    WriteAll();
}

private void AnalysisOverride(string inFile, CompilationUnitSyntax inRoot)
{
    ...
    if (Functions.Count > 0)
    {
        _overrideFunction[$"{NamespaceDeclaration.Name}.{ClassDeclaration.Identifier}"] = Functions;
        // ★ class -> override methods 的来源表完全由这一步重建
    }
}

private void WriteAll()
{
    File.WriteAllText(Path.Combine(_outputPathName, DynamicFileName), ...);
    File.WriteAllText(Path.Combine(_outputPathName, DynamicFileFileName), ...);
    File.WriteAllText(Path.Combine(_outputPathName, OverrideFunctionFileName), ...);
    File.WriteAllText(Path.Combine(_outputPathName, OverrideFileFileName), ...);
    // ★ 四张 sidecar 表总是整表重写
}
```

关键源码 [2] `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:51-76,166-206,432-447`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:99-161`

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs
// 函数: Generate / WriteGenerationSummary / DeleteStaleOutputs
// 位置: 51-76, 166-206, 432-447
// 说明: shard `.cpp` 只按 generatedPaths 做定向失效，summary/csv 则按本轮 session 整体重写
// ============================================================================
public static int Generate(IUhtExportFactory factory)
{
    HashSet<string> generatedPaths = new(StringComparer.OrdinalIgnoreCase);
    ...
    DeleteStaleOutputs(factory, generatedPaths);
    WriteGenerationSummary(factory, moduleSummaries, csvEntries, generatedFileCount);
    WriteCoverageDiagnostics(moduleSummaries);
}

private static void WriteGenerationSummary(...)
{
    string summaryPath = factory.MakePath("AS_FunctionTable_Summary", ".json");
    ...
    File.WriteAllText(summaryPath, summaryJson, Encoding.UTF8);
    WriteModuleSummaryCsv(factory, moduleSummaries);
    WriteEntryCsv(factory, csvEntries);
    // ★ summary/json/csv 是“本轮 session 的统计面”，不是按 class/file 做 sidecar 增量维护
}

private static void DeleteStaleOutputs(IUhtExportFactory factory, HashSet<string> generatedPaths)
{
    foreach (string existingFile in Directory.EnumerateFiles(outputDirectory, "AS_FunctionTable_*.cpp"))
    {
        if (!generatedPaths.Contains(existingFile))
        {
            File.Delete(existingFile);
            // ★ 只删 stale shard，不清空整个输出目录
        }
    }
}
```

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs
// 函数: WriteSkippedEntriesCsv / WriteSkippedReasonSummaryCsv
// 位置: 99-161
// 说明: 失败原因表被设计成正式产物，服务的是“绑定覆盖率诊断”，不是作者源码定位
// ============================================================================
private static void WriteSkippedEntriesCsv(IUhtExportFactory factory, List<AngelscriptSkippedFunctionEntry> skippedEntries)
{
    string csvPath = factory.MakePath("AS_FunctionTable_SkippedEntries", ".csv");
    ...
    File.WriteAllText(csvPath, builder.ToString(), Encoding.UTF8);
}

private static void WriteSkippedReasonSummaryCsv(IUhtExportFactory factory, List<AngelscriptSkippedFunctionEntry> skippedEntries)
{
    string csvPath = factory.MakePath("AS_FunctionTable_SkippedReasonSummary", ".csv");
    ...
    File.WriteAllText(csvPath, builder.ToString(), Encoding.UTF8);
    // ★ 输出关注点是 failure reason 分布，而不是 class/file owner map
}
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 索引状态模型 | UnrealCSharp 的 `Dynamic/Override` 四张 JSON 是目录级完整快照；Angelscript 的主生成物是 shard `.cpp` 加 summary/coverage 表 | 实现方式不同 |
| 失效粒度 | UnrealCSharp full run 会清空输出目录；Angelscript 只删除 stale `AS_FunctionTable_*.cpp` 分片 | 实现质量差异（仅就生成 shard 稳定性而言，Angelscript 的失效粒度更细） |
| 诊断产物重点 | UnrealCSharp 重心是 `class/file/override intent` 映射；Angelscript 重心是 `direct/stub/skipped reason` 覆盖统计 | 实现方式不同 |

### [维度 D4] 变更晋升屏障：compile-complete promotion vs queue-and-tick consumption

这一维我刻意不重复前文已经写过的 “directory watcher + hot reload queue” 基础结构，只收束一个更关键的事实：**文件变化到底在什么时刻被提升成“可以影响运行时状态”的正式输入**。UnrealCSharp 当前可见路径里，这个屏障不是“编译成功”，而是“编译进程返回”。`FEditorListener::OnDirectoryChanged()` 先把 `.cs` 变化缓存起来；窗口重新激活时，`OnApplicationActivationStateChanged()` 才触发 `FCSharpCompiler::Get().Compile(FileChanges)`。真正值得记账的细节在 `FCSharpCompilerRunnable::Compile()`：内部 `Compile()` 通过 `SyncProcess(dotnet build ...)` 获得 `InReturnCode`，但返回码只决定通知栏显示“Compilation succeeded / failed”，外层 `Compile(const TFunction<void()>&)` 无论成功还是失败，都会继续 `OnCompile.Broadcast(FileChanges)` 并执行 `FDynamicGenerator::Generator(FileChanges)`。这意味着 UnrealCSharp 当前路径的晋升屏障是 **compile complete**，不是 **compile success**。

Angelscript 的屏障则完全不同。`QueueScriptFileChanges()` 一旦看到 `.as` 变化就直接往 `FileChangesDetectedForReload / FileDeletionsDetectedForReload` 塞条目，并记录 `LastFileChangeDetectedTime`；`CheckForHotReload()` 到 tick 时才统一 drain 队列，对删除操作额外留出 `0.2s` rename window，然后把文件集交给 `PerformHotReload()`。因此 Angelscript 当前路径的晋升屏障是 **queue 被 tick 消费**，而不是某个外部编译器返回码。

```
[D4] Change Promotion Barrier

[UnrealCSharp]
DirectoryWatcher(.cs)
├─ queue FileChanges in editor memory
├─ app focus restore
├─ FCSharpCompilerRunnable::Compile()
│  ├─ SyncProcess(dotnet build ...)
│  └─ regardless of return code -> OnCompile.Broadcast + FDynamicGenerator::Generator
└─ OnCompile -> FCodeAnalysis::Analysis(file) + SetCodeAnalysisDynamicFilesMap()

[Angelscript]
QueueScriptFileChanges(.as)
├─ FileChangesDetectedForReload / FileDeletionsDetectedForReload
├─ record LastFileChangeDetectedTime
├─ 0.2s delete delay for rename window
└─ Tick -> CheckForHotReload() -> PerformHotReload()
```

关键源码 [1] `Reference/UnrealCSharp/Source/Compiler/Private/FCSharpCompilerRunnable.cpp:148-193,248-299,328-343`、`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:176-225,228-248,300-315`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/Compiler/Private/FCSharpCompilerRunnable.cpp
// 函数: DoWork / Compile(const TFunction<void()>&) / Compile()
// 位置: 148-193, 248-299
// 说明: 编译返回码当前只影响通知，不影响 OnCompile / DynamicGenerator 是否继续推进
// ============================================================================
void FCSharpCompilerRunnable::DoWork()
{
	Compile([&]()
	{
		FDynamicGenerator::Generator(FileChanges);
		FileChanges.Empty();
	});
}

void FCSharpCompilerRunnable::Compile(const TFunction<void()>& InFunction)
{
	...
	Compile(); // ★ 先跑外部 dotnet build

	const auto Task = FFunctionGraphTask::CreateAndDispatchWhenReady(
		[InFunction, this]()
		{
			if (!GExitPurge)
			{
				FUnrealCSharpCoreModuleDelegates::OnCompile.Broadcast(FileChanges);
				InFunction();
				// ★ 这里没有检查 build return code；只要 compile 过程返回，就会继续推进
			}
		},
		...,
		ENamedThreads::GameThread);
}

const auto OnComplete = [&NotificationInfo](const int32 InReturnCode, const FString& InResult)
{
	if (InReturnCode == 0)
	{
		NotificationInfo = new FNotificationInfo(FText::FromString(TEXT("Compilation succeeded")));
	}
	else
	{
		NotificationInfo = new FNotificationInfo(FText::FromString(TEXT("Compilation failed")));
		UE_LOG(LogUnrealCSharp, Error, TEXT("%s"), *InResult);
	}
	// ★ 返回码只决定 UI/日志，不回传为后续 Generate 的 gating 条件
};
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp
// 函数: OnBeginGenerator / OnEndGenerator / OnCompile / OnApplicationActivationStateChanged
// 位置: 176-225, 228-248, 300-315
// 说明: `.cs` 变化先缓存，等窗口重新激活后再统一送进 compile；compile 返回后再刷新索引
// ============================================================================
void FEditorListener::OnBeginGenerator()
{
	...
	bIsGenerating = true;
	FileChanges.Reset();
}

void FEditorListener::OnCompile(const TArray<FFileChangeData>& InFileChangeData)
{
	for (const auto& File : FileChange)
	{
		if (IFileManager::Get().FileExists(*File))
		{
			FCodeAnalysis::Analysis(File);
		}
	}

	FDynamicGenerator::SetCodeAnalysisDynamicFilesMap();
	// ★ compile 返回后，变更文件会被再次送进单文件 CodeAnalysis 并重写 dynamic map
}

void FEditorListener::OnApplicationActivationStateChanged(const bool IsActive)
{
	if (IsActive)
	{
		if (!FileChanges.IsEmpty() && !bIsPIEPlaying && !bIsGenerating)
		{
			FCSharpCompiler::Get().Compile(FileChanges);
			FileChanges.Reset();
			// ★ 真正的“晋升动作”发生在窗口重新激活时，不是文件系统回调瞬间
		}
	}
}
```

关键源码 [2] `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp:43-89`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2729-2778,2794-2828`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp
// 函数: QueueScriptFileChanges
// 位置: 43-89
// 说明: `.as` 文件变化会立刻进入 reload 队列，而不是先经过外部编译器返回码
// ============================================================================
void QueueScriptFileChanges(...)
{
	for (const FFileChangeData& Change : Changes)
	{
		...
		Engine.LastFileChangeDetectedTime = FPlatformTime::Seconds();

		if (AbsolutePath.EndsWith(TEXT(".as")))
		{
			if (Change.Action == FFileChangeData::EFileChangeAction::FCA_Removed)
			{
				Engine.FileDeletionsDetectedForReload.AddUnique({ AbsolutePath, RelativePath });
			}
			else
			{
				Engine.FileChangesDetectedForReload.AddUnique({ AbsolutePath, RelativePath });
			}

			UE_LOG(Angelscript, Log, TEXT("Queued script file change for primary engine reload: %s"), *RelativePath);
			continue;
		}
	}
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: CheckForHotReload / Tick
// 位置: 2729-2778, 2794-2828
// 说明: reload 的正式晋升点发生在 tick 消费队列时，删除还会额外等待 rename window
// ============================================================================
void FAngelscriptEngine::CheckForHotReload(ECompileType CompileType)
{
	TArray<FFilenamePair> FileList;
	FileList.Append(FileChangesDetectedForReload);
	FileChangesDetectedForReload.Empty();

	if (FileList.Num() != 0 || FPlatformTime::Seconds() - LastFileChangeDetectedTime > 0.2)
	{
		for (const auto& DeletedFile : FileDeletionsDetectedForReload)
			FileList.AddUnique(DeletedFile);
		FileDeletionsDetectedForReload.Empty();
		// ★ 删除特意延迟 0.2s，给 rename 留合并窗口
	}

	if (FileList.Num() != 0)
	{
		PerformHotReload(CompileType, FileList);
	}
}

void FAngelscriptEngine::Tick(float DeltaTime)
{
	...
	if (!GIsEditor || HasGameWorld())
	{
		CheckForHotReload(ECompileType::SoftReloadOnly);
	}
	else
	{
		CheckForHotReload(ECompileType::FullReload);
	}
	// ★ 队列在 tick 中被正式消费，PIE/game world 只允许 soft reload
}
```

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 变更晋升屏障 | UnrealCSharp 当前路径在 `SyncProcess(dotnet build)` 返回后就会 `OnCompile.Broadcast + DynamicGenerator::Generator`，成功/失败只影响通知栏；Angelscript 当前路径在 tick drain 队列时才进入 `PerformHotReload` | 实现方式不同 |
| failure gating | 就当前可见代码而言，UnrealCSharp 没有把后续生成推进绑定到 compile success；Angelscript 这里的正式晋升条件是“队列被消费”，不是外部编译器返回码 | 实现质量差异（仅就 failure gating 这一子问题而言，UnrealCSharp 当前 gate 更宽） |
| rename 吸收 | Angelscript 对删除操作显式保留 `0.2s` rename window；UnrealCSharp 当前 `.cs` 路径主要依赖“失焦累计 + 重新激活时统一编译” | 实现方式不同 |

---

## 深化分析 (2026-04-09 08:10:14)

### [维度 D2] 元数据主权：attribute catalog projection vs metadata interpretation

前面的 D2 已经写过 override、默认参数和接口桥接。这一轮只追一个还没单独落成结论的问题：**脚本系统里 `ScriptName`、`ToolTip`、`Category`、`WorldContext`、可见性、默认值这些元数据，到底是谁的主权**。源码显示，UnrealCSharp 把这件事做成了 `attribute catalog -> UE reflection` 的正向投影链。`FReflectionRegistry::Initialize()` 先把 `UClassAttribute`、`UPropertyAttribute`、`BlueprintCallableAttribute`、`ServerAttribute`、`CategoryAttribute` 等 attribute class 注册进统一 registry；`FDynamicGeneratorCore::GetClassMetaDataAttributes()` / `GetPropertyMetaDataAttributes()` / `GetFunctionMetaDataAttributes()` 再按反射对象种类维护 allowlist；最终 `SetFieldMetaData()` 统一把 attribute 名去掉 `Attribute` 后缀，直接写回 `UField::SetMetaData()`，`SetFlags()` 则同步改写 `FunctionFlags`。对动态 C# 类型来说，authoring attribute 本身就是 UE 反射事实的上游输入。

Angelscript 在当前这条 native exposure 链上的方向几乎相反。`FAngelscriptFunctionSignature` 和 `GetPropertyBindParams()` 读取的是**已经存在于 `UFunction` / `FProperty` 上的 UE metadata 与 flags**：`ScriptName`、`CPP_Default_*`、`WorldContext`、`DeterminesOutputType`、`ScriptMixin`、`ScriptNoDiscard`、`BlueprintVisible`、`BlueprintAssignable`、`NotInAngelscript` 等；UHT 函数表生成器也只是继续按 `NotInAngelscript`、`BlueprintInternalUseOnly` 过滤。也就是说，就本轮定位到的 binding/UHT 代码路径而言，Angelscript 更像“反射 metadata 的解释器”，而不是像 UnrealCSharp 那样提供一套通用 attribute catalog 去反向生产 UE metadata。

```
[D2-Deep] Metadata Ownership Direction

[UnrealCSharp]
C# attributes
├─ FReflectionRegistry stores attribute classes      // 先把 authoring 词汇表注册成 reflection class
├─ Get*MetaDataAttributes() selects allowlists       // 按 class/property/function 分类允许下沉
├─ SetFlags() mutates FunctionFlags/ClassFlags       // 行为标志直接落到 UE 反射对象
└─ SetFieldMetaData() -> UField::SetMetaData         // metadata 名称也直接写回 UE
   // author intent becomes UE reflection fact

[Angelscript]
UE UFunction / FProperty / UHT node
├─ Helper_FunctionSignature reads metadata           // ScriptName/default/world context/mixin
├─ Helper_PropertyBind reads flags + metadata        // read/write/edit 权限来自现有 property 状态
├─ UHT generator filters export surface              // NotInAngelscript / InternalUseOnly
└─ script declaration / docs / bind params derived   // 脚本可见面由现有反射信息推导
   // existing UE metadata becomes script exposure contract
```

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Reflection/FReflectionRegistry.cpp:100-125,346-382,548-549`、`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGeneratorCore.cpp:1063-1285`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Reflection/FReflectionRegistry.cpp
// 函数: FReflectionRegistry::Initialize
// 位置: attribute catalog 的注册中心
// ============================================================================
OverrideAttributeClass = GetClass(
    COMBINE_NAMESPACE(NAMESPACE_ROOT, NAMESPACE_CORE_UOBJECT), CLASS_OVERRIDE_ATTRIBUTE);

UClassAttributeClass = GetClass(
    COMBINE_NAMESPACE(NAMESPACE_ROOT, NAMESPACE_DYNAMIC), CLASS_U_CLASS_ATTRIBUTE);
UStructAttributeClass = GetClass(
    COMBINE_NAMESPACE(NAMESPACE_ROOT, NAMESPACE_DYNAMIC), CLASS_U_STRUCT_ATTRIBUTE);
UEnumAttributeClass = GetClass(
    COMBINE_NAMESPACE(NAMESPACE_ROOT, NAMESPACE_DYNAMIC), CLASS_U_ENUM_ATTRIBUTE);
UPropertyAttributeClass = GetClass(
    COMBINE_NAMESPACE(NAMESPACE_ROOT, NAMESPACE_DYNAMIC), CLASS_U_PROPERTY_ATTRIBUTE);
UFunctionAttributeClass = GetClass(
    COMBINE_NAMESPACE(NAMESPACE_ROOT, NAMESPACE_DYNAMIC), CLASS_U_FUNCTION_ATTRIBUTE);

BlueprintCallableAttributeClass = GetClass(
    COMBINE_NAMESPACE(NAMESPACE_ROOT, NAMESPACE_DYNAMIC), CLASS_BLUEPRINT_CALLABLE_ATTRIBUTE);
BlueprintImplementableEventAttributeClass = GetClass(
    COMBINE_NAMESPACE(NAMESPACE_ROOT, NAMESPACE_DYNAMIC), CLASS_BLUEPRINT_IMPLEMENTABLE_EVENT_ATTRIBUTE);
BlueprintNativeEventAttributeClass = GetClass(
    COMBINE_NAMESPACE(NAMESPACE_ROOT, NAMESPACE_DYNAMIC), CLASS_BLUEPRINT_NATIVE_EVENT_ATTRIBUTE);
ServerAttributeClass = GetClass(
    COMBINE_NAMESPACE(NAMESPACE_ROOT, NAMESPACE_DYNAMIC), CLASS_SERVER_ATTRIBUTE);
CategoryAttributeClass = GetClass(
    COMBINE_NAMESPACE(NAMESPACE_ROOT, NAMESPACE_DYNAMIC), CLASS_CATEGORY_ATTRIBUTE);
// ★ 动态类型 authoring 语汇先被注册成可查询的 reflection class，而不是零散硬编码在单个生成器里
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGeneratorCore.cpp
// 函数: GetClassMetaDataAttributes / GetPropertyMetaDataAttributes / GetFunctionMetaDataAttributes
// 位置: metadata allowlist 与投影入口
// ============================================================================
static TArray<FClassReflection*> ClassMetaDataAttributes = {
    ReflectionRegistry.GetHideCategoriesAttributeClass(),
    ReflectionRegistry.GetToolTipAttributeClass(),
    ReflectionRegistry.GetDisplayNameAttributeClass(),
    ReflectionRegistry.GetScriptNameAttributeClass(),
    ReflectionRegistry.GetShowWorldContextPinAttributeClass(),
    ReflectionRegistry.GetBlueprintThreadSafeAttributeClass(),
    ReflectionRegistry.GetUsesHierarchyAttributeClass()
    // ★ class 级 metadata 不是“见到什么都下沉”，而是先经过 allowlist
};

static TArray<FClassReflection*> PropertyMetaDataAttributes = {
    ReflectionRegistry.GetToolTipAttributeClass(),
    ReflectionRegistry.GetDisplayNameAttributeClass(),
    ReflectionRegistry.GetScriptNameAttributeClass(),
    ReflectionRegistry.GetClampMinAttributeClass(),
    ReflectionRegistry.GetClampMaxAttributeClass(),
    ReflectionRegistry.GetCategoryAttributeClass(),
    ReflectionRegistry.GetExposeOnSpawnAttributeClass(),
    ReflectionRegistry.GetMakeEditWidgetAttributeClass(),
    ReflectionRegistry.GetCustomizePropertyAttributeClass()
    // ★ property 也有独立 allowlist，保证 metadata 下沉是分对象种类治理的
};

static TArray<FClassReflection*> FunctionMetaDataAttributes = {
    ReflectionRegistry.GetCallInEditorAttributeClass(),
    ReflectionRegistry.GetToolTipAttributeClass(),
    ReflectionRegistry.GetCategoryAttributeClass(),
    ReflectionRegistry.GetDisplayNameAttributeClass(),
    ReflectionRegistry.GetScriptNameAttributeClass(),
    ReflectionRegistry.GetCallableWithoutWorldContextAttributeClass(),
    ReflectionRegistry.GetKeywordsAttributeClass(),
    ReflectionRegistry.GetLatentAttributeClass(),
    ReflectionRegistry.GetWorldContextAttributeClass(),
    ReflectionRegistry.GetDeterminesOutputTypeAttributeClass(),
    ReflectionRegistry.GetBitmaskEnumAttributeClass()
    // ★ function metadata 也走同一套 catalog + allowlist 机制
};
```

关键源码 [2] `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGeneratorCore.cpp:791-873,894-910`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGeneratorCore.cpp
// 函数: SetMetaData / SetFlags
// 位置: 把 attribute 值真正写回 UE reflection object
// ============================================================================
void FDynamicGeneratorCore::SetMetaData(FField* InField, const FString& InAttribute, const FString& InValue)
{
    InField->SetMetaData(*InAttribute.LeftChop(9), *InValue);
    // ★ 去掉 Attribute 后缀后直接写入 UE metadata key
}

template <typename T>
static void SetFieldMetaData(T InField, const TArray<FClassReflection*>& InMetaDataAttributes,
                             FReflection* InReflection, const TFunction<void()>& InSetMetaData)
{
    for (const auto& MetaDataAttribute : InMetaDataAttributes)
    {
        if (InReflection->HasAttribute(MetaDataAttribute))
        {
            FDynamicGeneratorCore::SetMetaData(InField, MetaDataAttribute->GetName(),
                                               InReflection->GetAttributeValue(MetaDataAttribute));
            // ★ attribute value 被原样投影到 UField metadata
        }
    }

    InSetMetaData();
}

if (InReflection->HasAttribute(FReflectionRegistry::Get().GetBlueprintableAttributeClass()))
{
    SetMetaData(InClass, CLASS_IS_BLUEPRINT_BASE_ATTRIBUTE, TEXT("true"));
    SetMetaData(InClass, CLASS_BLUEPRINT_TYPE_ATTRIBUTE, TEXT("true"));
    // ★ 一些 attribute 还会被转换成 UE 约定的布尔 metadata
}
```

关键源码 [3] `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h:85-123,204-279,414-510`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_PropertyBind.h:9-92`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:497-514`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h
// 函数: GetScriptNameForFunction / InitFromFunction / ModifyScriptFunction
// 位置: binding 侧消费现有 UE metadata，生成脚本声明与 traits
// ============================================================================
if (InFunction->HasMetaData(NAME_Signature_ScriptName))
{
    OutScriptName = GetPrimaryScriptName(InFunction->GetMetaData(NAME_Signature_ScriptName));
    // ★ 先读现有 ScriptName，再决定脚本函数名
}

if (Function->HasMetaData(MetaName))
{
    FString MetaStr = Function->GetMetaData(MetaName);
    ArgumentDefaults.Add(MetaStr);
    // ★ 默认参数不是脚本 authoring 产物，而是从 UE metadata 回读
}

const FString& MixinClasses = Function->GetOuterUClass()->GetMetaData(NAME_Signature_ScriptMixin);
// ★ mixin 语义也来自现有 UClass metadata

FAngelscriptDocs::AddUnrealDocumentation(
    FunctionId,
    Function->GetMetaData(NAME_Signature_ToolTip),
    Function->GetMetaData(NAME_Signature_Category),
    Function);
Function->SetMetaData(NAME_AS_Tooltip, *ScriptTooltip);
// ★ 最后只把派生出来的脚本文档补回去，不是反向构造完整 UE metadata catalog
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_PropertyBind.h
// 函数: GetPropertyBindParams
// 位置: property 暴露权限完全由现有 flags/metadata 推导
// ============================================================================
const bool bHasScriptReadOnly = Property->HasMetaData(NAME_ScriptReadOnly);
const bool bHasScriptReadWrite = Property->HasMetaData(NAME_ScriptReadWrite);
const bool bHasNotInAngelscript = Property->HasMetaData(NAME_NotInAngelscript);

if (bHasNotInAngelscript)
{
    Params.bCanRead = false;
    Params.bCanWrite = false;
    Params.bCanEdit = false;
}
else if (Property->HasAnyPropertyFlags(CPF_BlueprintVisible))
{
    Params.bCanRead = true;
    if (!Property->HasAnyPropertyFlags(CPF_BlueprintReadOnly))
        Params.bCanWrite = true;
    // ★ 绑定权限来自已有 UE flags，不是脚本侧 attribute catalog 下沉
}
```

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs
// 函数: ShouldIncludeFunction
// 位置: UHT 只继续消费 native metadata 做过滤
// ============================================================================
if (function.MetaData.ContainsKey("NotInAngelscript") ||
    (function.MetaData.ContainsKey("BlueprintInternalUseOnly") && !function.MetaData.ContainsKey("UsableInAngelscript")))
{
    return false;
    // ★ 这里的 metadata 作用是“裁剪已有暴露面”，不是从脚本 authoring 反向生产 metadata
}
```

设计取舍上，两者不是“谁更先进”，而是元数据流向不同。UnrealCSharp 用更重的 attribute registry 和 allowlist，换来**动态类型 authoring 与 UE 反射更高的语义同构性**；Angelscript 则把 binding 链牢牢锚在现有 `UFunction/FProperty/UHT` 事实之上，减少 duplicated authoring surface，但在本轮定位到的 native exposure 代码路径里，没有看到对等的“通用 attribute catalog -> UE reflection 投影层”。

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| metadata 的主权方向 | UnrealCSharp 让 C# attribute 直接下沉成 UE metadata/flags；Angelscript 当前 binding/UHT 路径主要读取既有 UE metadata 再推导脚本声明 | 实现方式不同 |
| metadata catalog 治理 | UnrealCSharp 有显式 `FReflectionRegistry + Get*MetaDataAttributes()` catalog/allowlist；Angelscript 当前证据更多是按 `FName` 逐项读取并解释 | 实现质量差异，UnrealCSharp 的 catalog 化更系统 |
| 通用 metadata 投影层 | UnrealCSharp 已直证存在；Angelscript 在本轮定位到的 native exposure 链上未见等价“通用 authoring attribute 投影层” | 没有实现（限于本轮扫描到的 binding/UHT 源码路径） |

### [维度 D3] RPC 方法体所有权：post-compile `_Implementation` clone vs source rename-and-wrapper

前面的 D3 已经写过 RPC flag lowering 和 Blueprint wrapper。这一轮继续往下追一个更底层的问题：**作者写下的 RPC 方法体，最终由谁持有**。UnrealCSharp 的答案是“编译后由 IL Weaver 重新分配”。`UnrealTypeWeaver::ProcessRpcMethods()` 会在程序集层扫描带 `UFunctionAttribute + Server/Client/NetMulticast` 的方法，先通过 `CreateRpcMethodImplementation()` 新建一个带 `[Override]` 的 `MethodName_Implementation`，把原方法的局部变量、异常处理器和指令完整拷过去；然后 `ModifyRpcMethod()` 清空原方法体，把它改写成固定 dispatcher：按参数布局 `localloc` 缓冲、对象参数先取 `GarbageCollectionHandle`、最后通过 `FFunction_GenericCall24Implementation` / `FFunction_GenericCall26Implementation` 走统一的 native RPC 调度。

Angelscript 把同样的分离动作前移到了脚本预处理期。`FAngelscriptPreprocessor` 在处理 `BlueprintEvent`、`Server`、`Client`、`NetMulticast` specifier 时，会先决定是否需要 `GenerateBlueprintEventWrapper()`，随后直接把 `FunctionDesc->ScriptFunctionName` 改成 `*_Implementation`；如果是 `Server` RPC 还会在预处理期强制检查 `WithValidation`。到 `AngelscriptClassGenerator` 真正落 `UFunction` 时，看到的已经是“wrapper + renamed implementation”这套中间状态，而不是原始 authoring 符号。两边最后都出现 `_Implementation`，但一个发生在 **IL post-pass**，一个发生在 **source preprocess**。

```
[D3-Deep] RPC Body Ownership

[UnrealCSharp]
C# method with [UFunction] + [Server/Client/NetMulticast]
├─ UnrealTypeWeaver::ProcessRpcMethods()           // 发生在程序集改写阶段
├─ CreateRpcMethodImplementation()
│  └─ clone original body -> Method_Implementation // 作者方法体被挪到 sibling method
└─ ModifyRpcMethod()
   ├─ clear original method body
   ├─ pack args into stack buffer
   └─ call FFunction_GenericCall24/26Implementation
      // original symbol becomes dispatcher

[Angelscript]
script UFUNCTION specifiers
├─ Preprocessor parses BlueprintEvent / Net flags  // 发生在 class generation 之前
├─ GenerateBlueprintEventWrapper()                 // 先生成 caller wrapper
├─ ScriptFunctionName += "_Implementation"         // 作者函数名在源码处理中就被改写
└─ ClassGenerator materializes final UFunction     // 后续只消费中间状态
   // source symbol becomes implementation before runtime class exists
```

关键源码 [1] `Reference/UnrealCSharp/Script/Weavers/UnrealTypeWeaver.cs:689-705,827-989`、`Reference/UnrealCSharp/Script/UE/Library/FFunctionImplementation.cs:97-100`

```csharp
// ============================================================================
// 文件: Reference/UnrealCSharp/Script/Weavers/UnrealTypeWeaver.cs
// 函数: ProcessRpcMethods / CreateRpcMethodImplementation / ModifyRpcMethod
// 位置: RPC 方法体在 IL 层被拆成 implementation + dispatcher
// ============================================================================
private void ProcessRpcMethods(TypeDefinition Type)
{
    var rpcMethods = Type.GetMethods()
        .Where(Method => Method.CustomAttributes.Any(Attribute =>
            Attribute.AttributeType.FullName == _ufunctionAttributeType.FullName))
        .Where(Method => Method.CustomAttributes.Any(Attribute =>
            Attribute.AttributeType.FullName == _serverAttributeType.FullName ||
            Attribute.AttributeType.FullName == _clientAttributeType.FullName ||
            Attribute.AttributeType.FullName == _netMulticastAttributeType.FullName))
        .ToList();

    foreach (var method in rpcMethods)
    {
        Type.Methods.Add(CreateRpcMethodImplementation(method));
        ModifyRpcMethod(Type, method);
        // ★ 先复制作者原始方法体，再把原方法改成 dispatcher
    }
}

private MethodDefinition CreateRpcMethodImplementation(MethodDefinition Method)
{
    var newMethod = new MethodDefinition(Method.Name + "_Implementation", MethodAttributes.Public,
        Method.ReturnType);

    newMethod.CustomAttributes.Add(new CustomAttribute(constructorRef));
    // ★ 新方法自动带 [Override]，后续会走 runtime override/dispatch 链

    foreach (var instruction in Method.Body.Instructions)
    {
        newMethod.Body.Instructions.Add(instruction);
        // ★ 原始方法指令被整体迁移到 _Implementation
    }
}

private void ModifyRpcMethod(TypeDefinition Type, MethodDefinition Method)
{
    Method.Body.GetILProcessor().Clear();
    ...
    Method.Body.GetILProcessor().Append(Instruction.Create(OpCodes.Call,
        ModuleDefinition.ImportReference(Method.Parameters.Count > 0
            ? _genericCall26Implementation
            : _genericCall24Implementation)));
    Method.Body.GetILProcessor().Append(Instruction.Create(OpCodes.Ret));
    // ★ 原方法体被清空后，只留下参数打包 + 固定 dispatcher 调用
}
```

```csharp
// ============================================================================
// 文件: Reference/UnrealCSharp/Script/UE/Library/FFunctionImplementation.cs
// 位置: dispatcher 最终落到固定 InternalCall 族
// ============================================================================
[MethodImpl(MethodImplOptions.InternalCall)]
public static extern void FFunction_GenericCall24Implementation(nint InMonoObject, uint InFunctionHash);

[MethodImpl(MethodImplOptions.InternalCall)]
public static extern void FFunction_GenericCall26Implementation(nint InMonoObject, uint InFunctionHash,
    byte* InBuffer);
// ★ RPC dispatcher 的 ABI 面是固定的，不是每个方法单独生成一个 native export
```

关键源码 [2] `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:1499-1529,1592-1624`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3455-3483`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp
// 位置: BlueprintEvent / RPC 在预处理期先生成 wrapper，再改写 implementation 名
// ============================================================================
else if (Spec.Name == PP_NAME_BlueprintEvent)
{
    bool bAlreadyHasWrapper = FunctionDesc->bBlueprintEvent;
    FunctionDesc->bBlueprintEvent = true;
    FunctionDesc->bCanOverrideEvent = true;

    if (!bAlreadyHasWrapper)
    {
        GenerateBlueprintEventWrapper(File, Chunk, Macro, FunctionDesc);
        FunctionDesc->ScriptFunctionName += TEXT("_Implementation");
        // ★ 作者函数名在源码阶段就改成 *_Implementation
    }
}

else if (Spec.Name == PP_NAME_NetMulticast || Spec.Name == PP_NAME_NetClient || Spec.Name == PP_NAME_NetServer)
{
    bool bAlreadyHasWrapper = FunctionDesc->bBlueprintEvent;
    FunctionDesc->bBlueprintEvent = true;
    FunctionDesc->bNetServer = Spec.Name == PP_NAME_NetServer;

    if (FunctionDesc->bNetServer)
    {
        if (!Specs.FindByPredicate([](FSpecifier& CurSpec) -> bool { return CurSpec.Name == PP_NAME_WithValidation; }))
        {
            MacroError(File, Macro, FString::Printf(TEXT(
                "UFUNCTION() %s is marked as Server but does not have the WithValidation property specified!"
            ), *FunctionDesc->FunctionName));
            // ★ 预处理期就强制 Server RPC 的 validation 合同
        }
    }

    if (!bAlreadyHasWrapper)
    {
        GenerateBlueprintEventWrapper(File, Chunk, Macro, FunctionDesc);
        FunctionDesc->ScriptFunctionName += TEXT("_Implementation");
    }
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 位置: class generator 只消费前面准备好的中间状态并落最终 flags
// ============================================================================
if (FunctionDesc->bBlueprintCallable && !FunctionDesc->bIsPrivate)
    NewFunction->FunctionFlags |= FUNC_BlueprintCallable;
if ((FunctionDesc->bBlueprintEvent && FunctionDesc->bCanOverrideEvent) || FunctionDesc->bBlueprintOverride)
    NewFunction->FunctionFlags |= FUNC_BlueprintEvent;
if (FunctionDesc->bNetMulticast)
    NewFunction->FunctionFlags |= FUNC_NetMulticast;
if (FunctionDesc->bNetClient)
    NewFunction->FunctionFlags |= FUNC_NetClient;
if (FunctionDesc->bNetServer)
    NewFunction->FunctionFlags |= FUNC_NetServer;
if ((NewFunction->FunctionFlags & FUNC_NetFuncFlags) != 0)
{
    NewFunction->FunctionFlags |= FUNC_Net;
    if (!FunctionDesc->bUnreliable)
        NewFunction->FunctionFlags |= FUNC_NetReliable;
    // ★ 这里不再关心“原始函数体在哪”，只消费 preprocess 产出的状态机结果
}
```

设计取舍上，UnrealCSharp 的做法把 dispatcher ABI 固定得非常干净，但代价是**作者写下的原始方法体要到编译后才被移动到 `_Implementation`**，需要引入 IL surgery；Angelscript 则把 wrapper / implementation 分离保留在脚本编译流水线内部，validation 和 specifier 冲突也能更早暴露，但这意味着 preprocessor/class generator 要承担更多语义状态机责任。

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| `_Implementation` 的形成阶段 | UnrealCSharp 在 IL post-pass 克隆方法体生成 `_Implementation`；Angelscript 在 preprocess 阶段直接改写 `ScriptFunctionName` | 实现方式不同 |
| 原始 authoring body 的归属 | UnrealCSharp 的原方法体最终迁移到 sibling implementation method；Angelscript 的作者函数体在源码处理中就被重命名并保留在脚本编译链里 | 实现方式不同 |
| RPC 合同暴露时机 | Angelscript 会在 preprocess 阶段强制 `Server + WithValidation`；UnrealCSharp 当前这条源码证据链更偏“weaver 生成 dispatcher + runtime 调度” | 实现质量差异，Angelscript 的前置校验更早 |

### [维度 D8] 属性热路径所有权：IL-rewritten accessors vs runtime-generated accessors

前面的 D8 已经写过 call family、临时参数缓冲和 JIT/PrecompiledData。这一轮只看一个更细、但更贴热点路径的问题：**属性访问的可执行体是在哪一层成型的**。UnrealCSharp 的答案是“编译后直接写死在程序集里”。`UnrealTypeWeaver::ProcessUClassProperty()` / `ProcessUStructProperty()` 会先删掉 C# 自动 backing field，再为每个属性补一个静态 `__PropertyName` hash 字段；getter/setter 方法体随后被整个清空并改写成 `localloc` 缓冲 + `GarbageCollectionHandle` 装箱/拆箱 + `FProperty_*Implementation` 固定 `InternalCall`。换句话说，最终跑在 CLR 里的不是作者看到的自动属性，而是一段**按属性专门化过的 native stub body**。

Angelscript 则把属性执行面保留在 runtime bind 阶段。`BindProperties()` 先枚举 `FProperty`，再通过 `GetPropertyBindParams()` 按 `ScriptReadOnly`、`ScriptReadWrite`、`NotInAngelscript`、`BlueprintVisible`、`BlueprintAssignable` 等 metadata/flags 推导读写权限；随后由 `Usage.Type->BindProperty()` 或各类型专门化实现，把 `GetFoo()` / `SetFoo()` 这类 accessor 注册到 `FAngelscriptBinds`。例如 primitive bool 专门化会显式注册 `GetX()` / `SetX(bool)` 并把 `NativeProperty` 指针作为 user data 绑定。也就是说，Angelscript 的属性热路径是**运行时类型系统里生成出来的 accessor surface**，而不是编译后写死在作者程序集中的 accessor body。

```
[D8-Deep] Property Hot Path Ownership

[UnrealCSharp]
weaver stage
├─ remove auto backing field                    // 不保留 C# 自动属性的默认存储
├─ add static hash field per property           // 每个属性保留稳定 lookup key
├─ rewrite getter/setter IL
│  ├─ localloc temp buffer
│  ├─ convert object args via GC handle
│  └─ call FProperty_*Implementation
└─ compiled assembly carries final accessor body
   // property path is pre-specialized in IL

[Angelscript]
runtime bind stage
├─ enumerate FProperty from UClass              // 先拿到现有 UE 属性
├─ derive bind params from metadata/flags       // 读写/编辑权限此时决定
├─ Usage.Type->BindProperty()                   // 类型系统生成 accessor
└─ BindClass.Method("GetX"/"SetX", NativeProperty)
   // property path is materialized inside runtime bind graph
```

关键源码 [1] `Reference/UnrealCSharp/Script/Weavers/UnrealTypeWeaver.cs:293-405,429-481,497-608`、`Reference/UnrealCSharp/Script/UE/Library/FPropertyImplementation.cs:5-21`

```csharp
// ============================================================================
// 文件: Reference/UnrealCSharp/Script/Weavers/UnrealTypeWeaver.cs
// 函数: ProcessUClassProperty / ProcessUStructProperty
// 位置: 自动属性在 IL 层被改写成固定 native bridge stub
// ============================================================================
var backingField = Type.Fields.FirstOrDefault(Field =>
    Field.Name == fieldName && Field.FieldType.FullName == Property.PropertyType.FullName);
if (backingField != null)
{
    Type.Fields.Remove(backingField);
    // ★ 先移除 C# 自动属性默认 backing field
}

if (field == null)
{
    field = new FieldDefinition("__" + Property.Name,
        FieldAttributes.Private | FieldAttributes.Static, ModuleDefinition.TypeSystem.UInt32);
    Type.Fields.Add(field);
    // ★ 每个属性补一个静态 hash/slot 字段，后续 accessor 直接用它做 lookup
}

ilProcessor.Clear();
ilProcessor.Append(Instruction.Create(OpCodes.Ldc_I4_S, BufferSize));
ilProcessor.Append(Instruction.Create(OpCodes.Conv_U));
ilProcessor.Append(Instruction.Create(OpCodes.Localloc));
...
ilProcessor.Append(Instruction.Create(OpCodes.Call,
    ModuleDefinition.ImportReference(_setObjectPropertyImplementation)));
// ★ setter 最终只剩“打包参数 -> 调固定 InternalCall”

ilProcessor.Clear();
ilProcessor.Append(Instruction.Create(OpCodes.Ldarg_0));
ilProcessor.Append(Instruction.Create(OpCodes.Call,
    ModuleDefinition.ImportReference(getGarbageCollectionHandleMethod)));
ilProcessor.Append(Instruction.Create(OpCodes.Ldsfld, ModuleDefinition.ImportReference(field)));
ilProcessor.Append(Instruction.Create(OpCodes.Ldloc_0));
ilProcessor.Append(Instruction.Create(OpCodes.Call,
    ModuleDefinition.ImportReference(_getObjectPropertyImplementation)));
...
ilProcessor.Append(Instruction.Create(OpCodes.Ret));
// ★ getter 同样被替换成固定桥接桩；UStruct 版本只是落到 _get/_setStructPropertyImplementation
```

```csharp
// ============================================================================
// 文件: Reference/UnrealCSharp/Script/UE/Library/FPropertyImplementation.cs
// 位置: 属性访问最终只有四个固定 InternalCall 面
// ============================================================================
[MethodImpl(MethodImplOptions.InternalCall)]
public static extern void FProperty_GetObjectPropertyImplementation(nint InMonoObject,
    uint InPropertyHash, byte* ReturnBuffer);

[MethodImpl(MethodImplOptions.InternalCall)]
public static extern void FProperty_SetObjectPropertyImplementation(nint InMonoObject,
    uint InPropertyHash, byte* InBuffer);

[MethodImpl(MethodImplOptions.InternalCall)]
public static extern void FProperty_GetStructPropertyImplementation(nint InMonoObject,
    uint InPropertyHash, byte* ReturnBuffer);

[MethodImpl(MethodImplOptions.InternalCall)]
public static extern void FProperty_SetStructPropertyImplementation(nint InMonoObject,
    uint InPropertyHash, byte* InBuffer);
// ★ per-property 差异被折叠进 hash/IL specialization，native ABI 面保持固定
```

关键源码 [2] `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_PropertyBind.h:9-92`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1063-1125`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Primitives.cpp:236-290`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_PropertyBind.h
// 函数: GetPropertyBindParams
// 位置: 先根据 metadata/flags 推导属性可见性，再决定是否生成 accessor
// ============================================================================
const bool bHasScriptReadOnly = Property->HasMetaData(NAME_ScriptReadOnly);
const bool bHasScriptReadWrite = Property->HasMetaData(NAME_ScriptReadWrite);
const bool bHasNotInAngelscript = Property->HasMetaData(NAME_NotInAngelscript);

if (bHasNotInAngelscript)
{
    Params.bCanRead = false;
    Params.bCanWrite = false;
    Params.bCanEdit = false;
}
else if (Property->HasAnyPropertyFlags(CPF_BlueprintVisible))
{
    Params.bCanRead = true;
    if (!Property->HasAnyPropertyFlags(CPF_BlueprintReadOnly))
        Params.bCanWrite = true;
    // ★ 访问权限在 runtime bind 之前按现有 UE 状态推导
}

if (Property->HasAnyPropertyFlags(CPF_BlueprintAssignable))
{
    Params.bCanRead = true;
    Params.bCanWrite = true;
    Params.bCanEdit = true;
    // ★ delegate 属性单独提升权限
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp
// 函数: BindProperties
// 位置: 属性 accessor 在 runtime bind 阶段被注册进类型系统
// ============================================================================
for (TFieldIterator<FProperty> It(Class, EFieldIterationFlags::IncludeDeprecated); It; ++It)
{
    FProperty* Property = *It;
    FAngelscriptType::FBindParams Params = GetPropertyBindParams(Property);
    Params.BindClass = &Binds;

    if(!Params.bCanRead && !Params.bCanWrite && !Params.bCanEdit)
        continue;

    FAngelscriptTypeUsage Usage = FAngelscriptTypeUsage::FromProperty(Property);
    if (!Usage.IsValid())
        continue;

    if (Usage.Type->BindProperty(Usage, Params, Property))
    {
        FAngelscriptPropertyBind DBProp;
        DBProp.UnrealPath = Property->GetName();
        DBProp.bCanWrite = Params.bCanWrite;
        DBProp.bCanRead = Params.bCanRead;
        DBProp.bCanEdit = Params.bCanEdit;
        DBProperties.Add(DBProp);
        // ★ accessor 不只注册到 VM，还会把合同记进 bind DB，供后续重放/诊断
    }
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Primitives.cpp
// 函数: FAngelscriptBoolType::BindProperty
// 位置: primitive 专门化显式生成 GetX / SetX accessor
// ============================================================================
if (Params.bCanRead && !BindClass.HasGetter(NativeProperty->GetName()))
{
    FString Decl = FString::Printf(TEXT("bool Get%s() const"), *PropName);
    BindClass.Method(Decl, FUNC_TRIVIAL(FAngelscriptBindHelpers::GetBoolFromProperty), (void*)NativeProperty);
    FAngelscriptBinds::PreviousBindPassScriptFunctionAsFirstParam();
    // ★ getter 是 runtime 注册出来的方法，不是编译后写死的 stub body
}

if ((Params.bCanWrite || Params.bCanEdit) && !BindClass.HasSetter(NativeProperty->GetName()))
{
    FString Decl = FString::Printf(TEXT("void Set%s(bool Value)"), *PropName);
    BindClass.Method(Decl, FUNC_TRIVIAL(FAngelscriptBindHelpers::SetBoolFromProperty), Params, (void*)NativeProperty);
    FAngelscriptBinds::PreviousBindPassScriptFunctionAsFirstParam();
    // ★ setter 同样是 bind graph 的一个节点，NativeProperty 指针作为 user data 传入
}
```

设计取舍上，UnrealCSharp 把属性可执行体前移到 weaver 阶段，换来更固定的 managed hot path；Angelscript 则把属性执行面放在 runtime bind graph，优点是 native `FProperty` 到脚本 accessor 的映射更统一、也更容易被 bind DB/诊断系统记录，但属性热路径本身不是编译后 baked 的 per-property stub。

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 属性执行体生成时机 | UnrealCSharp 在 IL post-pass 直接改写 getter/setter；Angelscript 在 runtime bind 阶段生成 accessor 方法 | 实现方式不同 |
| 属性身份载体 | UnrealCSharp 用每属性静态 hash 字段 + `FProperty_*Implementation`；Angelscript 用 `NativeProperty` user data + bind DB 记录 | 实现方式不同 |
| 编译后 per-property stub | UnrealCSharp 已直证存在；Angelscript 在当前 native property bind 路径下未见等价的编译后 accessor stub 生成层 | 没有实现（限于本轮扫描到的 property bind 源码路径） |

---

## 深化分析 (2026-04-09 08:19:58)

### [维度 D2] 调用标识注入合同：late-filled hash slots vs generated registration lines

前文已经写过 UnrealCSharp 的 `InternalCall` 平面和 `override patch`。这一轮补的是一个更细的绑定合同问题：**生成代码里的“函数/属性身份”到底何时变成可执行 token**。继续追源码后可以看到，UnrealCSharp 不是在生成期把最终地址或 hash 写死，而是先在生成出来的 C# partial class 里放 `private static uint __Foo = 0;` 这类占位字段；运行时 `FCSharpBind` 再扫描 Mono class 里所有 `__` 前缀字段，把 `GetTypeHash(FProperty/UFunction)` 回填进去，同时向 `ClassRegistry` 登记 descriptor。也就是说，**源码层只拥有占位 slot，真实调用标识在 bind 阶段才注入**。

Angelscript 当前函数表路径则是另一种做法。`AngelscriptFunctionTableCodeGenerator` 直接把 `FAngelscriptBinds::AddFunctionEntry(...)` 注册语句烘焙进 `AS_FunctionTable_<Module>_<Shard>.cpp`，运行时只需加载 bind modules 并回放 bind 数组。这里没有与 UnrealCSharp 对等的“作者可见 hash slot 回填层”；生成产物一开始就是 native registration line。

```
[D2-Deep] Symbol Identity Injection

[UnrealCSharp]
generated partial class
├─ private static uint __Health                     // 先生成占位 slot
├─ private static uint __Jump                       // wrapper 只引用 slot，不写死 hash
└─ wrapper call -> FProperty/FFunctionImplementation(..., __X, ...)
runtime bind
├─ scan Mono fields with "__" prefix
├─ write GetTypeHash(FProperty/UFunction)
└─ AddPropertyHash / AddFunctionHash

[Angelscript]
UHT scan
├─ collect BlueprintCallable entries
├─ BuildRegistrationLine()
│  └─ AddFunctionEntry(UClass, "Func", { ERASE_* })
└─ shard .cpp compiled into module
   └─ runtime replays bind array
```

关键源码 [1] `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp:168-231,554-558,655-670,788-793`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp:168-231,554-558,655-670,788-793
// 函数: FClassGenerator::Generator
// 位置: 先在生成出来的 partial class 中埋入 `__X` 占位字段，再让 wrapper 调用使用这些字段
// ============================================================================
auto EncodePropertyName = FUnrealCSharpFunctionLibrary::Encode(*PropertyIterator);
auto DummyPropertyName = FString::Printf(TEXT("__%s"), *EncodePropertyName);

PropertyContent += FString::Printf(TEXT(
    "\t\t\t\t\tFPropertyImplementation.FProperty_GetObjectPropertyImplementation(%s, %s, %s);\n"
    ...
    "\t\t\t\t\tFPropertyImplementation.FProperty_SetObjectPropertyImplementation(%s, %s, %s);\n"
),
    *PROPERTY_GARBAGE_COLLECTION_HANDLE,
    *DummyPropertyName,   // ★ wrapper 不直接持有真实 hash，只持有占位字段
    RETURN_BUFFER_TEXT,
    *PROPERTY_GARBAGE_COLLECTION_HANDLE,
    *DummyPropertyName,
    IN_BUFFER_TEXT
);

PropertyNameContent += FString::Printf(TEXT(
    "%s\t\tprivate static uint %s = 0;\n"
),
    PropertyNameSet.IsEmpty() ? TEXT("") : TEXT("\n"),
    *DummyPropertyName      // ★ 生成期只写入 0，占位等待 runtime 回填
);

auto DummyFunctionName = FString::Printf(TEXT("__%s"), *EncodeFunctionName);

auto FunctionCallBody = FString::Printf(TEXT(
    "FFunctionImplementation.FFunction_%sCall%dImplementation(%s, %s%s%s%s);\n"
),
    *FGeneratorCore::GetFunctionPrefix(FunctionReturnParam),
    FGeneratorCore::GetFunctionIndex(...),
    bIsStatic == true ? *FString::Printf(TEXT("StaticClass().%s"), *PROPERTY_GARBAGE_COLLECTION_HANDLE)
                      : *PROPERTY_GARBAGE_COLLECTION_HANDLE,
    *DummyFunctionName,    // ★ 函数调用同样只消费 `__FunctionName` slot
    ...
);

FunctionNameContent += FString::Printf(TEXT(
    "%s\t\tprivate static uint %s = 0;\n"
),
    FunctionNameSet.IsEmpty() ? TEXT("") : TEXT("\n"),
    *DummyFunctionName
);
```

关键源码 [2] `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FCSharpBind.cpp:133-170,206-223`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FCSharpBind.cpp:133-170,206-223
// 函数: FCSharpBind::BindImplementation
// 位置: runtime 扫描 `__` 字段并回填真实 hash，同时登记 property/function descriptor
// ============================================================================
for (const auto& [Name, Field] : Class->GetFields())
{
    if (Name.StartsWith(TEXT("__")))
    {
        Fields.Add(Name.RightChop(2), Field->GetField());
        // ★ 先把作者源码里看得到的 `__X` 字段收集出来
    }
}

for (const auto& [PropertyName, Property] : Properties)
{
    ...
    auto FieldHash = GetTypeHash(Property);
    if (auto FoundField = Class->GetField(FString::Printf(TEXT("__%s"), *FieldName)))
    {
        FoundField->SetValue(Class, &FieldHash); // ★ 真正的 property token 在 bind 阶段才注入
    }

    FCSharpEnvironment::GetEnvironment().AddPropertyHash(FieldHash, NewClassDescriptor, Property);
    // ★ 同时把 hash -> descriptor 的 runtime 映射建起来
}

for (const auto& [FunctionName, Function] : Functions)
{
    ...
    auto FieldHash = GetTypeHash(Function);
    if (auto FoundField = Class->GetField(FString::Printf(TEXT("__%s"), *FieldName)))
    {
        FoundField->SetValue(Class, &FieldHash); // ★ function token 同样是 late fill
    }

    FCSharpEnvironment::GetEnvironment().AddFunctionHash<FUnrealFunctionDescriptor>(
        FieldHash, NewClassDescriptor, Function);
}
```

关键源码 [3] `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:19-22,120-123,301-324`

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:19-22,120-123,301-324
// 函数: AngelscriptGeneratedFunctionEntry.BuildRegistrationLine / BuildShard
// 位置: 生成期直接烘焙 native registration line
// ============================================================================
public string BuildRegistrationLine()
{
    return $"\tFAngelscriptBinds::AddFunctionEntry({ClassName}::StaticClass(), \"{FunctionName}\", {{ {EraseMacro} }});";
    // ★ 这里生成出来的已经是最终注册语句，不存在作者可见的 hash 占位字段
}

string outputPath = factory.MakePath($"AS_FunctionTable_{module.ShortName}_{shardIndex:D3}", ".cpp");
factory.CommitOutput(outputPath, BuildShard(...));
// ★ 分片文件直接提交为 `.cpp` 产物

builder.Append("AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_AS_FunctionTable_")
    .Append(moduleShortName)
    .Append('_')
    .Append(shardIndex.ToString("D3"))
    .AppendLine("((int32)FAngelscriptBinds::EOrder::Late + 50, []()");
builder.AppendLine("{");

for (int entryIndex = startIndex; entryIndex < startIndex + entryCount; entryIndex++)
{
    builder.AppendLine(entries[entryIndex].BuildRegistrationLine());
    // ★ runtime 只需加载模块并执行 bind lambda，不需要二次回填 token
}
```

设计取舍上，UnrealCSharp 这套 slot 回填机制把“源码可见 API 形状”和“运行时 token 身份”拆开了。优点是生成代码可以保持稳定，`GetTypeHash()` 的最终值延迟到 bind 时统一注入；代价是 wrapper 在 bind 完成前并不拥有可执行身份。Angelscript 则把注册语句直接烘焙进 shard `.cpp`，优点是 native replay 路径短、身份面简单；代价是没有一个作者可见的、可被 IDE 直接观察的 late-bound token 层。

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 调用标识的落点 | UnrealCSharp 生成 `private static uint __X` 占位 slot，runtime bind 再回填 `GetTypeHash()`；Angelscript UHT 直接生成 `AddFunctionEntry(...)` 注册语句 | 实现方式不同 |
| 生成代码与运行时的耦合方式 | UnrealCSharp 是 “源码占位 + runtime 注入”；Angelscript 是 “生成期烘焙 native registration line” | 实现方式不同 |
| 作者可见的 ABI 身份层 | UnrealCSharp 当前源码直证存在 `__Property/__Function` 字段层；Angelscript 当前函数表路径未见对等作者可见 token slot | 部分没有实现（限于本轮扫描到的函数表生成主路径） |

### [维度 D6] 工作区再生成所有权：template workspace snapshot + selective overwrite

前面的 D6 已经写过 `sln/csproj`、Analyzer、Weaver 和导航。这一轮只补一个更偏“工作区 ownership（所有权）”的问题：**生成出来的 C# 工作区，到底哪些文件是插件拥有，哪些文件是用户拥有**。源码给出的答案很明确。`FSolutionGenerator::Generator()` 每轮都会把插件 `Template/` 下的 `CodeAnalysis`、`SourceGenerator`、`Weavers`、`UE.csproj`、`Game.props`、`Shared.props` 和 `Script.sln` 物化到项目 `Script/` 工作树；默认 `CopyTemplate()` 会覆盖已有文件。唯一的显式例外是 `Game.csproj`，它在生成时把 `bReplaceExistingFile` 设成 `false`，也就是**首次创建后不再覆盖**。

Angelscript 的 UHT 生成器则是另一套 ownership 模型。它输出的是 `AS_FunctionTable_<Module>_<Shard>.cpp` 这类纯 generator-owned shard 文件；生成完成后，`DeleteStaleOutputs()` 会删除不再属于本轮 `generatedPaths` 集合的旧 shard。也就是说，**产物是否继续存在完全由本轮生成集合决定**，没有对等的“保留一个用户可编辑工作区壳”的例外。

```
[D6-Deep] Workspace Ownership Split

[UnrealCSharp]
Plugin/Template
├─ UE.csproj / Game.csproj template
├─ Game.props / Shared.props
├─ CodeAnalysis / SourceGenerator / Weavers
└─ Script.sln
      |
      v
Project/Script workspace
├─ regenerated every run                           // UE.csproj / Game.props / Shared.props / tools / sln
└─ preserved after first create                    // Game.csproj

[Angelscript]
UHT session
├─ CommitOutput(AS_FunctionTable_*.cpp)
├─ Write summary/json/csv
└─ DeleteStaleOutputs()
   └─ remove obsolete shard cpp
```

关键源码 [1] `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FSolutionGenerator.cpp:9-126,128-152`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FSolutionGenerator.cpp:9-126,128-152
// 函数: FSolutionGenerator::Generator / CopyTemplate
// 位置: 每轮把插件模板物化到项目 `Script/` 工作区；只有 `Game.csproj` 明确声明“不覆盖已有文件”
// ============================================================================
CopyTemplate(FUnrealCSharpFunctionLibrary::GetUEProjectPath(),
    TemplatePath / DEFAULT_UE_NAME + PROJECT_SUFFIX,
    {...});

CopyTemplate(FUnrealCSharpFunctionLibrary::GetGameProjectPath(),
    TemplatePath / DEFAULT_GAME_NAME + PROJECT_SUFFIX,
    TArray<TFunction<void(FString& OutResult)>>
    {
        &FSolutionGenerator::ReplaceImport,
        &FSolutionGenerator::ReplaceTargetFramework,
        &FSolutionGenerator::ReplaceProjectReference
    },
    false); // ★ 唯一显式保留已有文件的主工程壳

CopyTemplate(FUnrealCSharpFunctionLibrary::GetGamePropsPath(),
    TemplatePath / DEFAULT_GAME_NAME + PROPS_SUFFIX,
    {...});

CopyTemplate(FPaths::Combine(FUnrealCSharpFunctionLibrary::GetFullScriptDirectory(),
                FUnrealCSharpFunctionLibrary::GetScriptDirectory() + SOLUTION_SUFFIX),
    TemplatePath / SOLUTION_NAME + SOLUTION_SUFFIX,
    {...});

void FSolutionGenerator::CopyTemplate(const FString& Dest, const FString& Src, const bool bReplaceExistingFile)
{
    if (auto& FileManager = IFileManager::Get(); !FileManager.FileExists(*Dest) || bReplaceExistingFile)
    {
        FileManager.Copy(*Dest, *Src);
        // ★ 默认策略是“文件不存在就建；存在且允许覆盖就重写”
    }
}

void FSolutionGenerator::CopyTemplate(const FString& Dest, const FString& Src,
                                      const TArray<TFunction<void(FString& OutResult)>>& InFunction,
                                      const bool bReplaceExistingFile)
{
    if (auto& FileManager = IFileManager::Get(); !FileManager.FileExists(*Dest) || bReplaceExistingFile)
    {
        FString Result;
        FFileHelper::LoadFileToString(Result, *Src);
        for (const auto& Function : InFunction)
        {
            Function(Result); // ★ 先按当前工程上下文改写模板，再落盘
        }
        FUnrealCSharpFunctionLibrary::SaveStringToFile(*Dest, Result);
    }
}
```

关键源码 [2] `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:51-77,120-123,432-445`

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:51-77,120-123,432-445
// 函数: Generate / DeleteStaleOutputs
// 位置: 产物完全由本轮生成集合接管；旧 shard 若不在 generatedPaths 中就被删除
// ============================================================================
HashSet<string> generatedPaths = new(StringComparer.OrdinalIgnoreCase);

foreach (UhtModule module in factory.Session.Modules)
{
    ...
    generatedFileCount += moduleSummary.ShardCount;
}

DeleteStaleOutputs(factory, generatedPaths);
WriteGenerationSummary(factory, moduleSummaries, csvEntries, generatedFileCount);
// ★ 本轮 session 结束时，以 generatedPaths 为准清理旧 shard

string outputPath = factory.MakePath($"AS_FunctionTable_{module.ShortName}_{shardIndex:D3}", ".cpp");
factory.CommitOutput(outputPath, BuildShard(...));
generatedPaths.Add(outputPath);
// ★ shard `.cpp` 是典型的 generator-owned 产物

foreach (string existingFile in Directory.EnumerateFiles(outputDirectory, "AS_FunctionTable_*.cpp"))
{
    if (!generatedPaths.Contains(existingFile))
    {
        File.Delete(existingFile); // ★ 旧 shard 的生死只由本轮生成集合决定
    }
}
```

设计取舍上，UnrealCSharp 这条链把工作区分成两层：工具链骨架由插件持续再生成，主 authoring project shell 允许项目方保留本地修改。这样做的好处是插件可以持续收紧 Analyzer/Weaver/props/sln 合同，又不必强行覆盖用户主工程；坏处是 ownership 边界变复杂，`Game.csproj` 与其他模板文件的生命周期不对称。Angelscript 的 shard 生成则更纯粹，所有生成产物都是一次性、可回收的编译资产，确定性更强，但没有对等的“持久 IDE 工作区壳”。

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 工作区 source of truth | UnrealCSharp 的 canonical source 在插件 `Template/`，每轮再物化到项目 `Script/`；Angelscript 函数表 shard 以本轮 `generatedPaths` 为准 | 实现方式不同 |
| 用户可编辑的保留面 | UnrealCSharp 仅对 `Game.csproj` 显式 `bReplaceExistingFile=false`；当前 Angelscript 函数表生成路径未见对等“保留一个用户可编辑 generated shell”的例外 | 实现质量差异 |
| 旧产物清理策略 | UnrealCSharp 是“多数模板文件覆盖式刷新 + 一个主工程壳保留”；Angelscript 是“stale shard 删除 + summary 重写” | 实现方式不同 |

### [维度 D11] 发布合同锚点：preserved `Game.csproj` + regenerated `Game.props`

前面的 D11 已经写过 `PublishDirectory`、`CopyDllsAfterPublish` 和 `ScriptRoot`。这一轮补的是一个更深的 build/deploy contract：**UnrealCSharp 的发布语义到底锚在用户项目文件里，还是锚在插件持续再生成的 props 文件里**。源码显示答案是后者。`FSolutionGenerator::ReplaceImport()` 会把生成出来的 `Game.csproj` 改写成 import `<GameName>.props`；而上一节已经看到，`Game.csproj` 首次创建后默认不再覆盖，`Game.props` 却会持续再生成。再看 `Template/Game.props`，Analyzer 引用、Weaver 文件、`AfterBuildPublish`、`CopyDllsAfterPublish` 全都写在这个 props 里。也就是说，**即使项目方保留自己的 `Game.csproj`，真正的发布合同仍由插件重写的 `Game.props` 持续掌控**。

Angelscript 的部署锚点则不在 host build graph。当前主链路里，runtime 直接从 `GetScriptRootDirectory()` 读取 `Binds.Cache`、按插件基目录读 `BindModules.Cache`，再按构建配置选择 `PrecompiledScript_<Config>.Cache` 或 `PrecompiledScript.Cache`，并用 `DataGuid` 校验 JIT 产物是否还能复用。这里的发布语义是 **runtime-root contract**，不是一个会被 IDE/MSBuild import 的工程层合同。

```
[D11-Deep] Delivery Contract Anchor

[UnrealCSharp]
preserved Game.csproj
└─ Import <GameName>.props                         // 指向持续再生成的 props
   └─ regenerated Game.props
      ├─ Analyzer / Weaver references
      ├─ AfterBuildPublish -> Publish
      └─ CopyDllsAfterPublish -> Content/<PublishDirectory>

[Angelscript]
runtime startup
├─ ScriptRoot/Binds.Cache
├─ PluginRoot/BindModules.Cache
└─ ScriptRoot/PrecompiledScript[_Config].Cache
   └─ validate build + DataGuid
```

关键源码 [1] `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FSolutionGenerator.cpp:87-104,166-176,210-217` 与 `Reference/UnrealCSharp/Template/Game.props:1-28`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FSolutionGenerator.cpp:87-104,166-176,210-217
// 函数: FSolutionGenerator::Generator / ReplaceImport / ReplaceOutputPath
// 位置: `Game.csproj` 保留，但通过 import 把发布合同挂到持续再生成的 `Game.props`
// ============================================================================
CopyTemplate(FUnrealCSharpFunctionLibrary::GetGameProjectPath(),
    TemplatePath / DEFAULT_GAME_NAME + PROJECT_SUFFIX,
    {
        &FSolutionGenerator::ReplaceImport,
        &FSolutionGenerator::ReplaceTargetFramework,
        &FSolutionGenerator::ReplaceProjectReference
    },
    false); // ★ 主工程壳保留

CopyTemplate(FUnrealCSharpFunctionLibrary::GetGamePropsPath(),
    TemplatePath / DEFAULT_GAME_NAME + PROPS_SUFFIX,
    {
        &FSolutionGenerator::ReplaceOutputPath,
        &FSolutionGenerator::AddProjectGeneratorHeaderComment
    }); // ★ props 持续重写

void FSolutionGenerator::ReplaceImport(FString& OutResult)
{
    OutResult = OutResult.Replace(TEXT("<Import Project=\"\" Condition=\"Exists('')\" />"),
        *FString::Printf(TEXT("<Import Project=\"%s%s\" Condition=\"Exists('%s%s')\" />"),
            *FUnrealCSharpFunctionLibrary::GetGameName(),
            *PROPS_SUFFIX,
            *FUnrealCSharpFunctionLibrary::GetGameName(),
            *PROPS_SUFFIX));
    // ★ 发布/分析合同不是写死在 Game.csproj 体内，而是转接到 props
}

void FSolutionGenerator::ReplaceOutputPath(FString& OutResult)
{
    OutResult = OutResult.Replace(TEXT("<ScriptOutputPath></ScriptOutputPath>"),
        *FString::Printf(TEXT("<ScriptOutputPath>..\\..\\Content\\%s</ScriptOutputPath>"),
            *FUnrealCSharpFunctionLibrary::GetPublishDirectory()));
}
```

```xml
<!-- =========================================================================
文件: Reference/UnrealCSharp/Template/Game.props:1-28
位置: 持续再生成的 props 内部直接声明 Analyzer、Weaver 与 Publish/Copy 目标
============================================================================ -->
<Project>
    <Import Project="../Shared.props" Condition="Exists('../Shared.props')" />
    <ItemGroup>
        <WeaverFiles Include="$(ProjectDir)..\Weavers\bin\$(Configuration)\netstandard2.0\Weavers.dll" WeaverClassNames="UnrealTypeWeaver" />
    </ItemGroup>
    <ItemGroup>
        <ProjectReference Include="..\SourceGenerator\SourceGenerator.csproj" OutputItemType="Analyzer" ReferenceOutputAssembly="false"/>
        <ProjectReference Include="..\Weavers\Weavers.csproj" ReferenceOutputAssembly="false" />
    </ItemGroup>
    <Target Name="AfterBuildPublish" AfterTargets="Build">
        <MSBuild Projects="$(ProjectPath)" Targets="Publish" Properties="Configuration=$(Configuration)" />
    </Target>
    <Target Name="CopyDllsAfterPublish" AfterTargets="Publish">
        <PropertyGroup>
            <ScriptOutputPath></ScriptOutputPath>
        </PropertyGroup>
        <ItemGroup>
            <PublishFiles Include="$(PublishDir)*.dll" />
        </ItemGroup>
        <MakeDir Directories="$(ScriptOutputPath)" Condition="!Exists('$(ScriptOutputPath)')" />
        <Copy SourceFiles="@(PublishFiles)" DestinationFolder="$(ScriptOutputPath)" />
        <!-- ★ 交付规则稳定地锚在 props，而不是锚在用户可随意改动的 Game.csproj 主体 -->
    </Target>
</Project>
```

关键源码 [2] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1466-1555`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1466-1555
// 函数: FAngelscriptEngine::Initialize
// 位置: 部署合同直接锚在 runtime root 与 cache 命名规则，而不是 host build graph
// ============================================================================
FAngelscriptBindDatabase::Get().Load(GetScriptRootDirectory() / TEXT("Binds.Cache"), bGeneratePrecompiledData);
// ★ bind database 固定从项目 Script root 读取

TSharedPtr<IPlugin> plugin = IPluginManager::Get().FindPlugin("Angelscript");
if (plugin)
{
    FAngelscriptBinds::LoadBindModules(plugin->GetBaseDir() / "BindModules.Cache");
    // ★ bind modules 固定从插件根读取
}

if (bUsePrecompiledData)
{
    FString Filename;
#if UE_BUILD_SHIPPING
    Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript_Shipping.Cache");
#elif UE_BUILD_TEST
    Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript_Test.Cache");
#elif UE_BUILD_DEVELOPMENT
    Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript_Development.Cache");
#endif

    if (!IFileManager::Get().FileExists(*Filename))
        Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript.Cache");

    if (IFileManager::Get().FileExists(*Filename))
    {
        PrecompiledData = new FAngelscriptPrecompiledData(Engine);
        PrecompiledData->Load(Filename);
        ...
        if (CompiledInfo != nullptr && CompiledInfo->PrecompiledDataGuid != PrecompiledData->DataGuid)
        {
            UE_LOG(Angelscript, Warning, TEXT("Loaded angelscript precompiled data does not match the transpiled C++ in the game binary. Transpiled code will not be used!"));
            FJITDatabase::Get().Clear();
            // ★ 交付有效性由 runtime cache seal 决定，不依赖工程 import 图
        }
    }
}
```

设计取舍上，UnrealCSharp 把“用户可定制的主工程壳”和“插件拥有的发布合同”拆成两层，优点是可以在不覆盖 `Game.csproj` 的前提下持续强制 Analyzer/Weaver/Publish 规则；代价是 build graph 的真实控制面分散在 `csproj + props` 两层文件里，理解成本更高。Angelscript 则把交付合同留在 runtime root 规则和 cache seal 上，优点是与 host build system 耦合更低，代价是部署语义更少以 IDE/MSBuild 形式显性化。

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 发布合同真正的 owner | UnrealCSharp 当前源码直证是“`Game.csproj` 保留，但 `Game.props` 持续再生成并承载 Publish/Copy 语义”；Angelscript 当前主路径由 runtime root + cache 命名/校验承载 | 实现方式不同 |
| 用户定制与工具链强约束的边界 | UnrealCSharp 允许保留主工程壳，但通过 import 重拿发布控制权；Angelscript 当前路径未见对等的 host-project build graph 锚点 | 实现质量差异 |
| 交付语义的可见层级 | UnrealCSharp 的发布规则对 IDE/MSBuild 是显式可见的；Angelscript 的发布规则主要对 runtime 初始化流程可见 | 实现方式不同 |

---

## 深化分析 (2026-04-09 23:30:10)

### [维度 D3] Latent 语义落点：attribute lowering vs explicit latent payload

前面的 D3 已经分别比较过 Blueprint override、RPC、输入绑定和委托互操作。这一轮只追一个此前没有单独落结论的点：**“潜伏调用（Latent）”语义到底是通过 `UFunction/UProperty/UClass` 元数据投影进入 UE，还是作为脚本层显式值/辅助 API 交给作者手工拼装。**

UnrealCSharp 当前快照更接近前者。作者侧先在 C# 里声明 `[Latent]`、`[LatentInfo("...")]`、`[NeedsLatentFixup("...")]`、`[LatentCallbackTarget]`、`[ExposedAsyncProxy("...")]`；随后 `FDynamicGeneratorCore` 把这些 attribute class 列进 `GetFunctionMetaDataAttributes()` / `GetPropertyMetaDataAttributes()` / `GetClassMetaDataAttributes()` allowlist，再通过 `SetFieldMetaData()` 统一下沉到 `UField::SetMetaData()`。也就是说，latent 语义在 UnrealCSharp 里不是“有个 `FLatentActionInfo` 能传就算支持”，而是**作者意图先进入反射层，最终变成 Blueprint 节点元数据的一部分**。

Angelscript 在本轮扫描到的主路径里更接近后者。`Bind_FLatentActionInfo.cpp` 直接把 `FLatentActionInfo` 作为普通值类型绑定出来，`IntegrationTest.cpp` 则为测试场景额外提供 `AddLatentAutomationCommand(...)` 这类显式 helper。当前扫描范围覆盖了 `AngelscriptRuntime / AngelscriptUHTTool / AngelscriptEditor` 中与 latent 直接相关的命中点，但**没有定位到与 UnrealCSharp 这套 `Latent/LatentInfo/NeedsLatentFixup/ExposedAsyncProxy -> UField metadata` 对等的脚本作者侧元数据投影链**。这里应归类为“当前路径上部分没有实现”，而不是笼统地说“没有 latent 支持”。

```
[D3-Deep] Latent Semantic Ownership

[UnrealCSharp]
author C# type
├─ [Class] ExposedAsyncProxyAttribute
├─ [Method] LatentAttribute / LatentInfoAttribute
├─ [Property] NeedsLatentFixup / LatentCallbackTarget
└─ FDynamicGeneratorCore::SetFieldMetaData()
   └─ UClass / UFunction / FProperty metadata     // latent 语义进入 UE 反射层

[Angelscript]
script/runtime helper
├─ Bind_FLatentActionInfo                          // 暴露值类型载荷
├─ AddLatentAutomationCommand(...)                // 显式测试辅助 API
└─ scanned generator paths
   └─ no matching script-authored latent metadata lowering located
```

关键源码 [1] `Reference/UnrealCSharp/Script/UE/Dynamic/Function/LatentAttribute.cs:1-9`、`Reference/UnrealCSharp/Script/UE/Dynamic/Function/LatentInfoAttribute.cs:1-14`、`Reference/UnrealCSharp/Script/UE/Dynamic/Property/NeedsLatentFixupAttribute.cs:1-14`、`Reference/UnrealCSharp/Script/UE/Dynamic/Property/LatentCallbackTargetAttribute.cs:1-9`、`Reference/UnrealCSharp/Script/UE/Dynamic/Class/ExposedAsyncProxyAttribute.cs:1-14`、`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGeneratorCore.cpp:791-829,1079-1087,1211-1268`

```csharp
// ============================================================================
// 文件: Reference/UnrealCSharp/Script/UE/Dynamic/Function/LatentAttribute.cs
// 位置: 作者侧直接声明 latent 语义
// ============================================================================
[AttributeUsage(AttributeTargets.Method)]
public class LatentAttribute : Attribute
{
    private string Value { get; set; } = "true"; // ★ 不是普通注释，而是后续会进入反射投影的 attribute
}

// ============================================================================
// 文件: Reference/UnrealCSharp/Script/UE/Dynamic/Function/LatentInfoAttribute.cs
// 位置: 指定哪一个参数是 latent info
// ============================================================================
[AttributeUsage(AttributeTargets.Method)]
public class LatentInfoAttribute : Attribute
{
    public LatentInfoAttribute(string InValue)
    {
        Value = InValue; // ★ 这里保存的是 UE 元数据值，不只是 C# 层注解
    }

    private string Value { get; set; }
}

// ============================================================================
// 文件: Reference/UnrealCSharp/Script/UE/Dynamic/Property/NeedsLatentFixupAttribute.cs
// 位置: 属性侧 latent fixup 标记
// ============================================================================
[AttributeUsage(AttributeTargets.Property)]
public class NeedsLatentFixupAttribute : Attribute
{
    public NeedsLatentFixupAttribute(string InValue)
    {
        Value = InValue;
    }

    private string Value { get; set; }
}

// ============================================================================
// 文件: Reference/UnrealCSharp/Script/UE/Dynamic/Property/LatentCallbackTargetAttribute.cs
// 位置: 回调目标标记
// ============================================================================
[AttributeUsage(AttributeTargets.Property)]
public class LatentCallbackTargetAttribute : Attribute
{
    private string Value { get; set; } = "true";
}

// ============================================================================
// 文件: Reference/UnrealCSharp/Script/UE/Dynamic/Class/ExposedAsyncProxyAttribute.cs
// 位置: Async proxy 暴露点
// ============================================================================
[AttributeUsage(AttributeTargets.Class)]
public class ExposedAsyncProxyAttribute : Attribute
{
    public ExposedAsyncProxyAttribute(string InValue)
    {
        Value = InValue;
    }

    private string Value { get; set; }
}

// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGeneratorCore.cpp
// 函数: SetMetaData / SetFieldMetaData / GetClassMetaDataAttributes / GetPropertyMetaDataAttributes / GetFunctionMetaDataAttributes
// 位置: attribute allowlist 最终统一写入 UE reflection metadata
// ============================================================================
void FDynamicGeneratorCore::SetMetaData(FField* InField, const FString& InAttribute, const FString& InValue)
{
    InField->SetMetaData(*InAttribute.LeftChop(9), *InValue);
    // ★ 去掉 Attribute 后缀，直接把作者侧 attribute 名转成 UE metadata key
}

template <typename T>
static void SetFieldMetaData(T InField, const TArray<FClassReflection*>& InMetaDataAttributes,
                             FReflection* InReflection, const TFunction<void()>& InSetMetaData)
{
    for (const auto& MetaDataAttribute : InMetaDataAttributes)
    {
        if (InReflection->HasAttribute(MetaDataAttribute))
        {
            FDynamicGeneratorCore::SetMetaData(InField, MetaDataAttribute->GetName(),
                                               InReflection->GetAttributeValue(MetaDataAttribute));
            // ★ 只要命中 allowlist，就把 C# attribute 值写回 UE 字段元数据
        }
    }

    InSetMetaData();
}

static TArray<FClassReflection*> ClassMetaDataAttributes = {
    ...,
    ReflectionRegistry.GetExposedAsyncProxyAttributeClass(),
    ...
}; // ★ class 级 async proxy 也在投影目录中

static TArray<FClassReflection*> PropertyMetaDataAttributes = {
    ...,
    ReflectionRegistry.GetNeedsLatentFixupAttributeClass(),
    ReflectionRegistry.GetLatentCallbackTargetAttributeClass(),
    ...
}; // ★ property 级 latent fixup / callback target 被正式纳入

static TArray<FClassReflection*> FunctionMetaDataAttributes = {
    ...,
    ReflectionRegistry.GetLatentAttributeClass(),
    ReflectionRegistry.GetLatentInfoAttributeClass(),
    ...
}; // ★ method 级 latent / latent info 同样是正式元数据通道
```

关键源码 [2] `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FLatentActionInfo.cpp:1-25`、`Plugins/Angelscript/Source/AngelscriptRuntime/Testing/IntegrationTest.cpp:671-708`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FLatentActionInfo.cpp
// 位置: latent 在当前路径首先表现为一个显式值类型绑定
// ============================================================================
AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_FLatentActionInfo((int32)FAngelscriptBinds::EOrder::Late + 1, []
{
    auto FLatentActionInfo_ = FAngelscriptBinds::ExistingClass("FLatentActionInfo");

    FLatentActionInfo_.Constructor("void f(int32 InLinkage, int32 InUUID, const FName InFunctionName, UObject InCallbackTarget)",
    [](FLatentActionInfo* Address, int32 InLinkage, int32 InUUID, const FName InFunctionName, UObject* InCallbackTarget)
    {
        new(Address) FLatentActionInfo();
        Address->Linkage = InLinkage;
        Address->UUID = InUUID;
        Address->ExecutionFunction = InFunctionName;
        Address->CallbackTarget = InCallbackTarget;
        // ★ latent 所需信息在这里就是普通字段，不是脚本作者 attribute 被投影后的 UFunction metadata
    });

    FLatentActionInfo_.Property("int32 Linkage", &FLatentActionInfo::Linkage);
    FLatentActionInfo_.Property("int32 UUID", &FLatentActionInfo::UUID);
    FLatentActionInfo_.Property("FName ExecutionFunction", &FLatentActionInfo::ExecutionFunction);
    FLatentActionInfo_.Property("UObject unresolved_object CallbackTarget", &FLatentActionInfo::CallbackTarget);
});

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Testing/IntegrationTest.cpp
// 函数: BindAddLatentAutomationCommand
// 位置: latent 场景通过显式 helper API 接入测试运行器
// ============================================================================
void BindAddLatentAutomationCommand(FAngelscriptBinds& T)
{
    const FString Signature = FString::Printf(TEXT("void AddLatentAutomationCommand(ULatentAutomationCommand LatentCommand, float32 TimeoutSecs=%f)"), DEFAULT_LATENT_COMMAND_TIMEOUT);
    T.Method(Signature, [](FAngelscriptIntegrationTest& Self, ULatentAutomationCommand& LatentCommand, float TimeoutSecs) {
        UWorld* TestWorld = GetTestWorld();
        LatentCommand.SetWorld(TestWorld);
        LatentCommand.SetAssociatedTest(Self.AsShared());
        Self.RememberLatentCommand(&LatentCommand);
        FLatentCommandContext Context = Self.DefineLatentCommand(TestWorld, TimeoutSecs);
        // ★ latent 的执行与回溯在这里靠显式 helper/command 对象串起来

        if (LatentCommand.RunsOnClient())
        {
            ADD_LATENT_AUTOMATION_COMMAND(AngelscriptLatentCommandClient(Self, Context, LatentCommand, ClientState, ClientExecutor));
        }
        else
        {
            ADD_LATENT_AUTOMATION_COMMAND(AngelscriptLatentCommand(Self, Context, LatentCommand));
        }
    });
}
```

设计取舍上，UnrealCSharp 这条链把 latent 语义前移到了作者声明和反射生成阶段，优点是 Blueprint 节点行为能从 metadata 一次性推导，坏处是 generator 必须维护一套专门的 attribute catalog。Angelscript 当前路径把 latent 语义更多保留在显式数据结构和 helper API 上，优点是实现直观、局部可控，坏处是 latent/async 语义没有被统一提升为脚本作者可声明的反射合同。

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| latent 语义进入 UE 的方式 | UnrealCSharp 用 `Latent/LatentInfo/NeedsLatentFixup/LatentCallbackTarget/ExposedAsyncProxy` attribute 再经 `SetFieldMetaData()` 写入 UE reflection metadata；Angelscript 当前主路径把 `FLatentActionInfo` 和 latent command helper 暴露为显式值/API | 实现方式不同 |
| async proxy 暴露平面 | UnrealCSharp 的 `ExposedAsyncProxyAttribute` 已进入 class metadata allowlist；本轮扫描到的 Angelscript latent 路径未见脚本作者侧对等声明平面 | 当前路径上 Angelscript 部分没有实现 |
| 作者心智模型 | UnrealCSharp 更接近“声明 latent 语义”；Angelscript 更接近“显式传递 latent 载荷并调用 helper” | 实现质量差异，前者在 Blueprint 节点一致性上更强，后者在运行时直观性上更强 |

### [维度 D5] 混合栈调试语义：runtime-delegated debugger vs plugin-authored debug knowledge graph

前面的 D5 已经说明过双方在“是否自己定义调试协议”上的分叉。这一轮只补更细的一层：**混合 Script / Blueprint / C++ 栈帧的编号、资产索引和跳转定义语义，到底是谁来建模。**

UnrealCSharp 当前源码直证显示，插件侧只暴露 `bEnableDebug / Host / Port` 三个设置，再在 `FMonoDomain::Initialize()` 中把 `--soft-breakpoints` 和 `--debugger-agent=transport=dt_socket,...` 交给 Mono，并调用 `mono_debug_init()`。也就是说，插件自己负责的是“把 Mono 调试端口打开”，而不是“发明一套 UE 语义调试数据库”。

Angelscript 则把这层语义显式做成插件合同。`FAngelscriptGoToDefinition`、`FAngelscriptDebugDatabaseSettings`、`FAngelscriptAssetDatabase` 都是协议层一等消息；`SendCallStack()` 会把 Blueprint 栈帧、脚本栈帧和 C++ bound function 栈帧合并后一起发给前端；`ResolveDebuggerFrame()` 还要把前端 frame index 重新解释成 script frame 或 blueprint frame；`GoToDefinition()` 则进一步把脚本符号翻译成 `UFunction/FProperty/UClass` 并直接走 `FSourceCodeNavigation`。这说明 Angelscript 的调试器不是单纯“连上脚本 VM”而已，而是**插件自己持有一张 UE 语义知识图谱**。

```
[D5-Deep] Debug Semantic Ownership

[UnrealCSharp]
Project Settings
├─ bEnableDebug / Host / Port
└─ FMonoDomain::Initialize()
   ├─ --soft-breakpoints
   ├─ --debugger-agent=dt_socket
   └─ mono_debug_init()
      └─ mixed-frame semantics delegated to Mono debugger

[Angelscript]
FAngelscriptDebugServer
├─ SendCallStack()                     // 合并 BP / Script / C++ 栈帧
├─ ResolveDebuggerFrame()              // 前端 frame id -> 实际 script/BP frame
├─ SendDebugDatabase()                 // 发送引擎/脚本设置与类型数据库
├─ SendAssetDatabase()                 // 发送资产索引
└─ GoToDefinition()                    // 符号 -> UFunction/FProperty/UClass 导航
```

关键源码 [1] `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpSetting.h:146-153`、`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp:93-118`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpSetting.h
// 位置: 插件侧暴露的调试设置面
// ============================================================================
UPROPERTY(Config, EditAnywhere, Category = Debug)
bool bEnableDebug;

UPROPERTY(Config, EditAnywhere, Category = Debug)
FString Host;

UPROPERTY(Config, EditAnywhere, Category = Debug)
int32 Port;
// ★ 插件设置面只暴露“是否启用 + socket 地址”，没有看到 UE 资产索引或 mixed-frame 语义结构

// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp
// 函数: FMonoDomain::Initialize
// 位置: 只把调试端口与软断点能力交给 Mono
// ============================================================================
if (UnrealCSharpSetting->IsEnableDebug())
{
    const auto Config = FString::Printf(TEXT(
        "--debugger-agent=transport=dt_socket,server=y,suspend=n,address=%s:%d"
    ),
                                        *UnrealCSharpSetting->GetHost(),
                                        UnrealCSharpSetting->GetPort());

    char* Options[] = {
        TCHAR_TO_ANSI(TEXT("--soft-breakpoints")),
        TCHAR_TO_ANSI(*Config)
    };

    mono_jit_parse_options(sizeof(Options) / sizeof(char*), Options);
    // ★ UnrealCSharp 插件侧到这里为止，后面的栈帧/断点/符号语义交给 Mono
}

mono_debug_init(MONO_DEBUG_FORMAT_MONO);
Domain = mono_jit_init("UnrealCSharp");
mono_domain_set(Domain, false);
```

关键源码 [2] `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h:453-544`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:1288-1377,1391-1475,1493-1515,2159-2203,2282-2343`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h
// 位置: UE 语义直接进入调试协议类型定义
// ============================================================================
struct FAngelscriptGoToDefinition : FDebugMessage
{
    FString TypeName;
    FString SymbolName;
}; // ★ 前端可以直接请求“跳转到某个脚本符号对应的 UE 定义”

struct FAngelscriptAssetDatabase : FDebugMessage
{
    TArray<FString> Assets;
}; // ★ 调试协议原生携带资产数据库

struct FAngelscriptDebugDatabaseSettings : FDebugMessage
{
    bool bAutomaticImports = false;
    bool bFloatIsFloat64 = false;
    bool bUseAngelscriptHaze = false;
    bool bDeprecateStaticClass = false;
    bool bDisallowStaticClass = false;
}; // ★ 协议里直接携带脚本语言配置开关

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 函数: GoToDefinition / SendCallStack / SendDebugDatabase / SendAssetDatabase / ResolveDebuggerFrame
// 位置: mixed-frame 和 UE 资产/符号语义都由插件自己建模
// ============================================================================
void FAngelscriptDebugServer::GoToDefinition(const FAngelscriptGoToDefinition GoTo)
{
    ...
    if (ScriptFunction != nullptr)
    {
        UFunction* UnrealFunction = FAngelscriptDocs::LookupAngelscriptFunction(ScriptFunction->GetId());
        if (UnrealFunction != nullptr)
        {
            FSourceCodeNavigation::NavigateToFunction(UnrealFunction);
            return;
            // ★ 脚本符号先翻译成 UE 反射对象，再走编辑器源码导航
        }
    }
    ...
}

void FAngelscriptDebugServer::SendCallStack(FSocket* Client)
{
    ...
    // ★ 先插入 Blueprint 栈帧
    int BPFrame = Context->GetBlueprintCallstackFrame(i);
    ...
    Frame.Name = FString::Printf(TEXT("(BP) %s"), *Function->GetDisplayNameText().ToString());
    ...

    // ★ 再插入 script / C++ bound function 栈帧
    if (ScriptFunction->GetFuncType() == asEFuncType::asFUNC_SYSTEM)
    {
        Frame.Name = FString::Printf(TEXT("(C++) %s"), *FunctionName);
    }
    else
    {
        Frame.Name = ANSI_TO_TCHAR(ScriptFunction->GetName());
        Frame.LineNumber = Context->GetLineNumber(i, nullptr, &SectionName);
    }
}

void FAngelscriptDebugServer::SendDebugDatabase(FSocket* Client)
{
    FAngelscriptDebugDatabaseSettings DebugSettings;
    DebugSettings.bAutomaticImports = FAngelscriptEngine::Get().ShouldUseAutomaticImportMethod();
    DebugSettings.bFloatIsFloat64 = GetDefault<UAngelscriptSettings>()->bScriptFloatIsFloat64;
    SendMessageToClient(Client, EDebugMessageType::DebugDatabaseSettings, DebugSettings);
    // ★ 调试前端拿到的不只是源码位置信息，还包括语言/引擎行为开关
}

void FAngelscriptDebugServer::SendAssetDatabase(FSocket* Client)
{
    ...
    AssetRegistry.EnumerateAllAssets([&](const FAssetData& AssetData) -> bool
    {
        AddAssetToMessage(Message, AssetData);
        if (Message.Assets.Num() > 50)
        {
            SendMessageToClient(Client, EDebugMessageType::AssetDatabase, Message);
            Message.Assets.Reset();
        }
        return true;
    });
    // ★ 资产数据库分批下发，供调试器查询/联动
}

int FAngelscriptDebugServer::ResolveDebuggerFrame(int DebuggerFrame)
{
    ...
    ResolvedFrames.Insert(FLAG_BlueprintFrame | BPStackIndex, 0);
    ...
    ResolvedFrames.Insert(i, 0);
    ...
    return ResolvedFrames[DebuggerFrame];
    // ★ 前端 frame index 不是直接等于 VM 栈索引，而是插件自己维护的 mixed-frame 映射
}
```

设计取舍上，UnrealCSharp 复用 Mono debugger 的好处是实现量小、与 .NET 生态兼容；代价是 UE 特有语义主要停留在插件外部工具链或运行时自身，当前源码路径里看不到对等的“资产数据库 + mixed-frame resolver + go-to-definition”插件合同。Angelscript 的好处是 UE 语义都能直接被调试器消费；代价是插件自己要维护完整协议、版本兼容和前后端一致性。

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 调试语义 owner | UnrealCSharp 当前路径里主要是 `Host/Port + debugger-agent`，mixed-frame 语义委托给 Mono；Angelscript 明确由 `FAngelscriptDebugServer` 自己建模 | 实现方式不同 |
| 混合栈帧处理 | Angelscript 有显式 `ResolveDebuggerFrame()` 把前端 frame 映射到 script/BP 栈；本轮未在 UnrealCSharp 插件侧定位到对等 frame normalizer | 当前路径上 UnrealCSharp 部分没有实现 |
| UE 语义进入调试层的深度 | Angelscript 直接下发 `DebugDatabaseSettings`、`AssetDatabase` 并支持 `GoToDefinition`；UnrealCSharp 当前插件侧调试面更轻 | 实现质量差异，Angelscript 的 UE 专用调试面更完整 |

### [维度 D9] 约束验证介质：compiler diagnostics vs executable example corpus

前面的 D9 已经比较过“有没有 dedicated test module”和“hot reload 后会不会跑测试”。这一轮只收束一个更窄、也更有方法论差异的问题：**用户写脚本时，约束错误到底是在“编译器诊断”里被立即阻断，还是先进入“可执行示例/回归测试”再验证。**

UnrealCSharp 当前快照把一大类 authoring 约束前移到了 Roslyn `SourceGenerator`。`UnrealTypeSourceGenerator` 直接定义 `UC_ERROR_01..05`，把“动态 `UClass/UStruct` 必须是 `partial`”“文件名必须和类型名匹配”“`UClass` 必须有基类”“动态类型名必须唯一”这类规则作为 **C# 编译错误** 抛出来。它的优势是反馈非常早，通常作者在 IDE/build 阶段就会被挡住；代价是这些规则主要覆盖“声明形状正确性”，并不天然等价于“最终 UE 行为正确”。

Angelscript 则把很大一部分 public-facing authoring surface 做成可执行示例。`AngelscriptScriptExampleActorTest.cpp` 直接把示例脚本内嵌成 `FScriptExampleSource`，`RunScriptExampleCompileTest()` 再在干净共享引擎里把示例拼成内存模块并编译，模块名、依赖示例和虚拟路径都被显式断言。换句话说，Angelscript 更像是把“文档/示例是否还能编译”当成自动化回归的一部分，而不是只依赖静态诊断。配合 `FHotReloadTestRunner`，它还会在 hot reload 后按模块批次执行 unit tests，这进一步说明它的验证介质偏向**可执行工件**。

```
[D9-Deep] Validation Medium

[UnrealCSharp]
author C# file
├─ UnrealTypeSourceGenerator
│  ├─ partial required
│  ├─ file/type name match
│  ├─ UClass base required
│  └─ type unique
└─ build fails in compiler diagnostics

[Angelscript]
example / module / reload output
├─ FScriptExampleSource + RunScriptExampleCompileTest()
│  └─ compile public examples in clean engine clone
└─ FHotReloadTestRunner
   └─ execute module unit tests after reload
```

关键源码 [1] `Reference/UnrealCSharp/Script/SourceGenerator/UnrealTypeSourceGenerator.cs:13-45,482-526,707-718`

```csharp
// ============================================================================
// 文件: Reference/UnrealCSharp/Script/SourceGenerator/UnrealTypeSourceGenerator.cs
// 位置: 把作者约束直接做成编译期错误
// ============================================================================
public static readonly DiagnosticDescriptor ErrorDynamicClassNotAPartialClass = new DiagnosticDescriptor(
    "UC_ERROR_01",
    "UClass or UStruct must be a partial class", "{0} \"{1}\" must be a partial class",
    "UnrealCSharp",
    DiagnosticSeverity.Error,
    isEnabledByDefault: true);

public static readonly DiagnosticDescriptor ErrorFileNameNotMatch = new DiagnosticDescriptor(
    "UC_ERROR_02",
    "The file name and class name do not match", "The file where {0} \"{1}\" is located must be \"{2}\"",
    "UnrealCSharp",
    DiagnosticSeverity.Error,
    isEnabledByDefault: true);

public static readonly DiagnosticDescriptor ErrorUClassHasNoBaseClass = new DiagnosticDescriptor(
    "UC_ERROR_04",
    "UClass must have a base class", "{0}",
    "UnrealCSharp",
    DiagnosticSeverity.Error,
    isEnabledByDefault: true);

public static readonly DiagnosticDescriptor ErrorTypeMustBeUnique = new DiagnosticDescriptor(
    "UC_ERROR_05",
    "Type must be unique", "{0}",
    "UnrealCSharp",
    DiagnosticSeverity.Error,
    isEnabledByDefault: true);

Errors.Add(Diagnostic.Create(UnrealTypeSourceGenerator.ErrorUClassHasNoBaseClass,
    ...,
    $"{name} must have a base class"));

if (Syntax.Modifiers.ToArray().Any(Modifier => Modifier.Text == "partial") == false)
{
    Errors.Add(Diagnostic.Create(UnrealTypeSourceGenerator.ErrorDynamicClassNotAPartialClass,
        ...,
        dynamicType.ToString().Replace("EType.", ""), name));
    // ★ “能不能成为动态 UE 类型”先在 C# 编译器里拦住
}

if (Types.Add(Name))
{
    return true;
}

Errors.Add(Diagnostic.Create(UnrealTypeSourceGenerator.ErrorTypeMustBeUnique,
    ...,
    $"{Name} must be unique"));
// ★ 重名同样是即时编译错误，而不是等运行时/测试阶段再发现
```

关键源码 [2] `Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleActorTest.cpp:9-94`、`Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleTestSupport.cpp:16-59`、`Plugins/Angelscript/Source/AngelscriptRuntime/Testing/UnitTest.cpp:531-610`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleActorTest.cpp
// 位置: public-facing 示例脚本直接就是 automation fixture
// ============================================================================
const AngelscriptScriptExamples::FScriptExampleSource GActorExample = {
    TEXT("Example_Actor.as"),
    TEXT(R"ANGELSCRIPT(
class AExampleActor_UnitTest : AActor
{
    UPROPERTY()
    int ExampleValue = 15;

    UFUNCTION(BlueprintOverride)
    void BeginPlay()
    {
        ScriptOnlyMethod();
        NewOverridableMethod();
    }

    UFUNCTION(BlueprintEvent)
    void NewOverridableMethod()
    {
        Log("Blueprint did not override this event.");
    }
};)ANGELSCRIPT"),
    nullptr,
    nullptr,
};

bool FAngelscriptScriptExampleActorTest::RunTest(const FString& Parameters)
{
    return AngelscriptScriptExamples::RunScriptExampleCompileTest(*this, GActorExample);
    // ★ 示例不是文档摆设，而是每次自动化都要真实编译
}

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleTestSupport.cpp
// 函数: RunScriptExampleCompileTest
// 位置: 示例统一在干净共享引擎里编译，并验证模块名/依赖/虚拟路径
// ============================================================================
bool RunScriptExampleCompileTest(FAutomationTestBase& Test, const FScriptExampleSource& Example)
{
    if (!Test.TestNotNull(TEXT("Script example file name should be set"), Example.ExampleFileName))
    {
        return false;
    }

    const FString ExampleFileName = Example.ExampleFileName;
    const FString ModuleNameString = FPaths::GetBaseFilename(ExampleFileName);
    ...

    FAngelscriptEngine& Engine = AcquireCleanSharedCloneEngine();
    ...
    const FString VirtualFileName = FString::Printf(TEXT("ScriptExamples/%s"), *ExampleFileName);
    const bool bCompiled = CompileAnnotatedModuleFromMemory(&Engine, ModuleName, VirtualFileName, CombinedScriptCode);
    Test.TestTrue(*FString::Printf(TEXT("Compile example '%s' succeeds"), *ExampleFileName), bCompiled);
    return bCompiled;
    // ★ 文档/示例的“活性”由真正的编译执行来证明
}

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Testing/UnitTest.cpp
// 函数: FHotReloadTestRunner::PrepareTests / RunTests
// 位置: hot reload 后还能继续批次执行模块级 tests
// ============================================================================
void FHotReloadTestRunner::PrepareTests(...)
{
    if (!ShouldRunUnitTestsOnHotReload())
    {
        return;
    }
    ...
    if (Module->UnitTestFunctions.Num() > 0)
    {
        TestAfterHotReload.Add(Module);
    }
}

bool FHotReloadTestRunner::RunTests(FAngelscriptEngine* AngelscriptManager)
{
    if (!ShouldRunUnitTestsOnHotReload())
        return true;

    bool AllUnitTestsPass = true;
    // ★ 不只验证“示例能编译”，还验证“热重载后模块测试仍能执行”
    ...
}
```

设计取舍上，UnrealCSharp 的强项是错误前置，类型形状类问题在 IDE/build 阶段就会爆；弱项是当前扫描路径没有呈现出与 Angelscript `Examples + HotReloadTestRunner` 对等的可执行公开样例回归面。Angelscript 的强项是“对外示例”本身就是可执行测试资产；弱项是作者在敲代码时拿到的即时结构性诊断，不像 Roslyn generator 那样早。

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 约束错误出现时机 | UnrealCSharp 把 `partial`、基类、文件名、唯一性等规则前移到编译器诊断；Angelscript 更依赖自动化编译/执行示例来证明 authoring surface 正常 | 实现方式不同 |
| public example 的验证方式 | Angelscript 直接把 `Example_*.as` 作为 automation fixture 编译；本轮未在 UnrealCSharp 当前路径定位到对等的 executable example corpus | 当前路径上 UnrealCSharp 部分没有实现 |
| 回归验证介质 | Angelscript 还把 hot reload 后的 unit tests 批次执行纳入 runtime 验证面；UnrealCSharp 当前强项更偏 IDE/build-time authoring diagnostics | 实现质量差异，两者分别优化“反馈更早”与“工件更可执行” |

---

## 深化分析 (2026-04-09 23:40:41)

### [维度 D3] 属性回调契约：attribute 直接下沉 vs specifier 先验校验

前面的 D3 主要看 override、RPC、latent 和输入绑定。本轮只补一个更贴 Blueprint authoring 的问题：**属性级回调语义是在“落成 UE 反射对象时直接改旗标”，还是在“类真正生成前先验校验回调合同”**。

UnrealCSharp 当前路径更偏第一种。`ReplicatedAttribute` / `ReplicatedUsingAttribute` / `BlueprintGetterAttribute` / `BlueprintSetterAttribute` 先作为托管 attribute 进入 `FReflection`，`FDynamicGeneratorCore::SetFlags(FProperty*)` / `SetFlags(UFunction*)` 再直接把它们下沉成 `CPF_Net`、`CPF_RepNotify`、`CPF_BlueprintVisible`、`FUNC_BlueprintCallable`、`FUNC_BlueprintPure`。本轮能直接落到源码行号的主路径里，看到的是“置旗标 + 少量冲突日志”，没有看到与 Angelscript `VerifyPropertySpecifiers()` 对等的 getter/setter/repnotify 签名 verifier。

Angelscript 则把这件事拆成三段：`Preprocessor` 先把 `ReplicatedUsing / BlueprintGetter / BlueprintSetter` 解析进 `PropDesc->Meta`，`FAngelscriptEngine::VerifyPropertySpecifiers()` 再逐项校验“函数是否存在 / getter 是否 `BlueprintPure` / getter 返回值是否匹配 / setter 参数个数与类型是否匹配 / repnotify 参数是否匹配”，最后 `AngelscriptClassGenerator` 才真正把 `CPF_Net / CPF_RepNotify / RepNotifyFunc` 落到新建 `FProperty`。所以它的 property callback 不是“声明到位就直接生效”，而是带一层显式前置验证。

```
[D3-Deep] Property Callback Contract

[UnrealCSharp]
C# property/function attributes
├─ FReflection stores attribute values                 // attribute class -> string[] 值表
├─ FDynamicGeneratorCore::SetFlags(FProperty)
│  ├─ CPF_Net / CPF_RepNotify
│  ├─ RepNotifyFunc
│  └─ CPF_BlueprintVisible
└─ FDynamicGeneratorCore::SetFlags(UFunction)
   ├─ FUNC_BlueprintCallable
   └─ FUNC_BlueprintPure

[Angelscript]
UProperty specifiers in script
├─ Preprocessor -> PropDesc->Meta                      // 先解析 specifier
├─ VerifyPropertySpecifiers()                          // 先校验函数存在与签名
│  ├─ VerifyRepFunc
│  ├─ VerifyBlueprintSetFunc
│  └─ VerifyBlueprintGetFunc
└─ AngelscriptClassGenerator
   └─ CPF_Net / CPF_RepNotify / RepNotifyFunc          // 最后再落到 UE FProperty
```

关键源码 [1] `Reference/UnrealCSharp/Script/UE/Dynamic/Property/ReplicatedAttribute.cs:6-15`、`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Reflection/FReflection.cpp:18-31`、`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGeneratorCore.cpp:393-411,648-671`

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGeneratorCore.cpp
// 函数: FDynamicGeneratorCore::SetFlags(FProperty*) / SetFlags(UFunction*)
// 位置: 属性与函数级 callback specifier 直接下沉为 UE flags
// ============================================================================
if (InReflection->HasAttribute(FReflectionRegistry::Get().GetReplicatedAttributeClass()))
{
    InProperty->SetPropertyFlags(CPF_Net);

    InProperty->SetBlueprintReplicationCondition(static_cast<ELifetimeCondition>(
        UKismetStringLibrary::Conv_StringToInt(
            InReflection->GetAttributeValue(FReflectionRegistry::Get().GetReplicatedUsingAttributeClass(), 0))));
    // ★ 这里直接把 attribute 值转成 replication condition
}

if (InReflection->HasAttribute(FReflectionRegistry::Get().GetReplicatedUsingAttributeClass()))
{
    InProperty->SetPropertyFlags(CPF_Net | CPF_RepNotify);
    InProperty->RepNotifyFunc = FName(
        InReflection->GetAttributeValue(FReflectionRegistry::Get().GetReplicatedUsingAttributeClass(), 0));
    InProperty->SetBlueprintReplicationCondition(static_cast<ELifetimeCondition>(
        UKismetStringLibrary::Conv_StringToInt(
            InReflection->GetAttributeValue(FReflectionRegistry::Get().GetReplicatedUsingAttributeClass(), 1))));
    // ★ `ReplicatedUsing` 同时承载 callback 名与 replication condition
}

if (InReflection->HasAttribute(FReflectionRegistry::Get().GetBlueprintGetterAttributeClass()))
{
    if (InFunction->FunctionFlags & FUNC_Event)
    {
        UE_LOG(LogUnrealCSharp, Error, TEXT("Function cannot be a blueprint event and a blueprint getter"));
    }

    InFunction->FunctionFlags |= FUNC_BlueprintCallable;
    InFunction->FunctionFlags |= FUNC_BlueprintPure;
    // ★ 当前可见主路径里是“置旗标 + 冲突日志”，没有看到对 getter 返回值/参数个数的同层 verifier
}

if (InReflection->HasAttribute(FReflectionRegistry::Get().GetBlueprintSetterAttributeClass()))
{
    if (InFunction->FunctionFlags & FUNC_Event)
    {
        UE_LOG(LogUnrealCSharp, Error, TEXT("Function cannot be a blueprint event and a blueprint setter."));
    }

    InFunction->FunctionFlags |= FUNC_BlueprintCallable;
}

// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Reflection/FReflection.cpp
// 函数: FReflection::GetAttributeValue
// 位置: attribute 值按“attribute class 键”读取
// ============================================================================
FString FReflection::GetAttributeValue(const FClassReflection* InAttribute, const int32 InIndex) const
{
    const auto FoundAttributeAttributeValue = AttributeValues.Find(InAttribute);

    return FoundAttributeAttributeValue != nullptr
               ? (FoundAttributeAttributeValue->IsValidIndex(InIndex)
                      ? (*FoundAttributeAttributeValue)[InIndex]
                      : FString())
               : FString();
    // ★ 因为查值是按 attribute class 键进行，所以上面的 `ReplicatedAttribute` 分支
    //   当前直观看起来把 lifetime condition 读取耦合到了 `ReplicatedUsingAttribute`
}
```

关键源码 [2] `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:2579-2735`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2508-2725`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2945-2957`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp
// 位置: 先把 property callback specifier 写进 PropDesc 元数据
// ============================================================================
else if (Spec.Name == PP_NAME_ReplicatedUsing)
{
    if (!Spec.Value.IsEmpty())
    {
        PropDesc->bReplicated = true;
        PropDesc->bRepNotify = true;
        PropDesc->Meta.Add(PP_NAME_ReplicatedUsing, Spec.Value);
    }
}
else if (Spec.Name == PP_NAME_BlueprintSetter)
{
    if (!Spec.Value.IsEmpty())
    {
        PropDesc->Meta.Add(PP_NAME_BlueprintSetter, Spec.Value);
    }
}
else if (Spec.Name == PP_NAME_BlueprintGetter)
{
    if (!Spec.Value.IsEmpty())
    {
        PropDesc->Meta.Add(PP_NAME_BlueprintGetter, Spec.Value);
    }
}

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: VerifyPropertySpecifiers / VerifyRepFunc / VerifyBlueprintSetFunc / VerifyBlueprintGetFunc
// 位置: 在 class generator 落旗标前，先校验 property callback 合同
// ============================================================================
FString* RepNotifyFunc = Property->Meta.Find(NAME_ReplicatedUsing);
bPassedVerification &= VerifyRepFunc(RepNotifyFunc, Property, Class, Module);

FString* BlueprintSetFunc = Property->Meta.Find(NAME_BlueprintSetter);
bPassedVerification &= VerifyBlueprintSetFunc(BlueprintSetFunc, Property, Class, Module);

FString* BlueprintGetFunc = Property->Meta.Find(NAME_BlueprintGetter);
bPassedVerification &= VerifyBlueprintGetFunc(BlueprintGetFunc, Property, Class, Module);
// ★ 这里不是“直接改旗标”，而是先跑存在性/参数个数/类型一致性验证

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 位置: 前置验证通过后，才真正把 replication 语义落到 UE FProperty
// ============================================================================
if (PropDesc->bReplicated)
{
    NewProperty->SetPropertyFlags(CPF_Net);
    NewProperty->SetBlueprintReplicationCondition(PropDesc->ReplicationCondition);

    if (PropDesc->bRepNotify)
    {
        FString* RepNotifyFunc = PropDesc->Meta.Find(TEXT("ReplicatedUsing"));
        if (RepNotifyFunc != nullptr)
        {
            NewProperty->SetPropertyFlags(CPF_RepNotify);
            NewProperty->RepNotifyFunc = FName(**RepNotifyFunc);
        }
    }
}
```

设计取舍上，UnrealCSharp 的优点是 authoring attribute 到 UE reflection flag 的映射非常直接，动态 C# 类型生成链更短；代价是 property callback 的合法性更多依赖生成期的“正确声明”本身，本轮主路径里没有定位到 Angelscript 那种集中 verifier。Angelscript 的优点是错误更早、诊断更贴 property 语义；代价是 preprocessor 和 verifier 都要维护一套额外的 specifier contract。

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| property callback 生效时机 | UnrealCSharp 当前主路径是 `attribute -> SetFlags -> UE flags` 直接下沉；Angelscript 是 `specifier -> VerifyPropertySpecifiers -> ClassGenerator` 三段式 | 实现方式不同 |
| getter/setter/repnotify 合法性校验 | Angelscript 有显式存在性/参数/返回值 verifier；本轮未在 UnrealCSharp property lowering 主路径定位到对等集中 verifier | 当前路径上 UnrealCSharp 部分没有实现 |
| replication condition 归属 | UnrealCSharp 当前源码里 `ReplicatedAttribute` 分支读取 condition 时调用的是 `GetReplicatedUsingAttributeClass()`；Angelscript 把 `ReplicationCondition` 与 `ReplicatedUsing` callback 分开持有 | 实现质量差异，UnrealCSharp 当前实现看起来存在更紧的属性间耦合 |

### [维度 D6] 编译基线主权：generated `Shared.props` vs editor-only navigation hooks

前面的 D6 已经把 `sln/csproj`、Analyzer、Weaver 和 Content Browser 代码入口写得很细。本轮不重复“有没有 solution”，只追一个更底层的问题：**一旦工作区生成出来，真正统一 IDE/编译器行为的那层 baseline（基线）由谁拥有**。

UnrealCSharp 的答案是 `Shared.props`。`UE.csproj` 模板直接 import `Shared.props`，`Game.csproj` 则先留空 import 位，之后由 `FSolutionGenerator::ReplaceImport()` 改写成 import `<GameName>.props`；同一个生成器还负责往这套工程图里注入 `TargetFramework`、`UE_X_Y_OR_LATER` / `WITH_EDITOR` 宏和 publish 输出目录。配合 `Shared.props` 本体里的 `AllowUnsafeBlocks=True`、`GenerateDependencyFile=false`、Debug 下 `DebugType=Embedded`，可以看到 UnrealCSharp 并不是只生成“几个 csproj 壳”，而是把编译器基线、诊断环境和构建产物形态一起写进导出的工作区。

Angelscript 在当前仓库里的 IDE 支撑则更 UE-editor-native。`RegisterAngelscriptSourceNavigation()` 安装的是 `ISourceCodeNavigationHandler`，它根据 `UASClass/UASFunction` 保留下来的源码路径和行号，直接调用 `code --goto` 打开脚本文件；`AngelscriptSourceNavigationTests.cpp` 再断言运行时生成类/函数确实保留了源码文件路径与行号。这说明 Angelscript 的 IDE 体验很强，但它落点是“编辑器导航 handler”，不是“插件导出的外部编译器基线”。

```
[D6-Deep] Compiler Baseline Ownership

[UnrealCSharp]
FSolutionGenerator
├─ ReplaceImport()                           // csproj -> props 关联
├─ ReplaceDefineConstants()                  // UE_* / WITH_EDITOR
├─ ReplaceTargetFramework()                  // netX.Y
└─ Shared.props
   ├─ AllowUnsafeBlocks=True
   ├─ GenerateDependencyFile=false
   └─ DebugType=Embedded (Debug)

[Angelscript]
RegisterAngelscriptSourceNavigation()
├─ UASClass / UASFunction carry file+line
├─ Open VS Code with `code --goto`
└─ automation tests verify source navigation
```

关键源码 [1] `Reference/UnrealCSharp/Template/Shared.props:1-15`、`Reference/UnrealCSharp/Template/UE.csproj:1-8`、`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FSolutionGenerator.cpp:166-229`

```xml
<!-- =========================================================================
文件: Reference/UnrealCSharp/Template/Shared.props
位置: 导出工作区的全局编译基线
============================================================================ -->
<Project>
  <PropertyGroup>
    <AllowUnsafeBlocks>True</AllowUnsafeBlocks>
    <RootNamespace>Script</RootNamespace>
    <GenerateDependencyFile>false</GenerateDependencyFile>
    <DefineConstants></DefineConstants>
  </PropertyGroup>
  <PropertyGroup Condition=" '$(Configuration)' == 'Debug' ">
    <DebugType>Embedded</DebugType>
    <NoWarn>0109;1701;1702;8500</NoWarn>
  </PropertyGroup>
</Project>

<!-- =========================================================================
文件: Reference/UnrealCSharp/Template/UE.csproj
位置: UE 工程模板显式 import Shared.props
============================================================================ -->
<Project Sdk="Microsoft.NET.Sdk">
  <Import Project="../Shared.props" Condition="Exists('../Shared.props')" />
  <PropertyGroup>
    <TargetFramework></TargetFramework>
  </PropertyGroup>
</Project>
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FSolutionGenerator.cpp
// 函数: ReplaceImport / ReplaceDefineConstants / ReplaceTargetFramework
// 位置: 生成器持续重写工作区编译合同
// ============================================================================
OutResult = OutResult.Replace(TEXT("<Import Project=\"\" Condition=\"Exists(\'\')\" />"),
                              *FString::Printf(TEXT(
                                  "<Import Project=\"%s%s\" Condition=\"Exists(\'%s%s\')\" />"
                              ),
                                  *FUnrealCSharpFunctionLibrary::GetGameName(),
                                  *PROPS_SUFFIX,
                                  *FUnrealCSharpFunctionLibrary::GetGameName(),
                                  *PROPS_SUFFIX));
// ★ `Game.csproj` 的真实 import 在生成阶段才被写实

if (!IsRunningCookCommandlet())
{
    DefineConstants += TEXT("WITH_EDITOR");
}

OutResult = OutResult.Replace(TEXT("<TargetFramework></TargetFramework>"),
                              *FString::Printf(TEXT(
                                  "<TargetFramework>net%d.%d</TargetFramework>"
                              ),
                                  FUnrealCSharpFunctionLibrary::GetDotnetVersion(),
                                  DOTNET_MINOR_VERSION));
// ★ 目标框架、版本宏、是否带 WITH_EDITOR 都由同一个生成器集中注入
```

关键源码 [2] `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp:6-139`、`Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSourceNavigationTests.cpp:69-78`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp
// 位置: IDE 支撑落点是 editor 内部 navigation handler，而不是外部工程文件
// ============================================================================
class FAngelscriptSourceCodeNavigation : public ISourceCodeNavigationHandler
{
public:
    virtual bool NavigateToFunction(const UFunction* InFunction) override
    {
        auto* ASFunc = Cast<const UASFunction>(InFunction);
        if (ASFunc == nullptr)
            return false;

        FString Path = ASFunc->GetSourceFilePath();
        if (Path.Len() == 0)
            return false;

        OpenFile(Path, ASFunc->GetSourceLineNumber());
        return true;
        // ★ 这里直接基于 runtime 保留的 file+line 打开源码
    };
...
    void OpenFile(const FString& Path, int LineNo = -1)
    {
        if (LineNo != -1)
            FPlatformMisc::OsExecute(nullptr, TEXT("code"), *FString::Printf(TEXT("--goto \"%s:%d\""), *Path, LineNo));
    }
};

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSourceNavigationTests.cpp
// 位置: 自动化测试验证 runtime script symbol 仍然携带源码坐标
// ============================================================================
TestTrue(TEXT("Source navigation should recognize generated script class"), FSourceCodeNavigation::CanNavigateToClass(RuntimeClass));
TestTrue(TEXT("Source navigation should recognize generated script function"), FSourceCodeNavigation::CanNavigateToFunction(RuntimeFunction));
// ★ Angelscript 的 IDE 合同重点是“运行时符号还能跳回源码”
```

设计取舍上，UnrealCSharp 的好处是 Roslyn、Analyzer、Weaver、IDE 编译器看到的是同一套导出基线，工作区外显程度很高；代价是插件要持续拥有并重写 `props/csproj` 合同。Angelscript 的好处是编辑器内导航链更轻，和 UE runtime 符号绑定更紧；代价是当前路径里没有对等的“外部工程级 compiler baseline”出口。

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 编译器基线 owner | UnrealCSharp 由 `Shared.props + FSolutionGenerator` 集中拥有 `unsafe/target framework/defines/debug type`；Angelscript 当前 IDE 支撑主路径是 editor navigation handler | 实现方式不同 |
| IDE 能力的主要载体 | UnrealCSharp 更像“导出工作区 + 统一 props”；Angelscript 更像“runtime symbol -> file/line 导航” | 实现方式不同 |
| 外部工程级合同 | 本轮未在 Angelscript 当前路径定位到对等的 exported compiler baseline 文件族；其强项在 editor/runtime 导航而非外部工作区建模 | 当前路径上 Angelscript 部分没有实现 |

### [维度 D11] 调试/依赖 sidecar 策略：embedded symbols + DLL-only payload vs script/cache artifact set

前面的 D11 已经写过 publish root、装载闭包和 `Game.props` ownership。本轮继续下钻一个更细的问题：**调试符号与依赖 sidecar 到底是不是正式交付物的一部分**。

UnrealCSharp 这一点在当前源码里非常清楚。`Shared.props` 全局设 `GenerateDependencyFile=false`，Debug 下设 `DebugType=Embedded`；`Game.props` 的 `CopyDllsAfterPublish` 只复制 `$(PublishDir)*.dll`；runtime 侧 `GetFullAssemblyPublishPath()` / `GetAssemblyPath()` 也只按 `UE.dll / Game.dll / CustomProjects.dll` 与 `.NET runtime` 根目录工作。换句话说，至少在插件自己控制的 staging 合同里，**托管 payload 的一等公民就是 DLL**，Debug 符号倾向嵌入 DLL，而不是外置 `.pdb/.deps.json` sidecar 再显式纳入装载闭包。

Angelscript 的交付面则天然是多工件（multi-artifact）模型。启动时它会从脚本根显式读 `Binds.Cache`、`BindModules.Cache`、`PrecompiledScript[_Config].Cache`；如果当前二进制里带了 transpiled StaticJIT 代码，还要再用 `PrecompiledDataGuid` 和 `FStaticJITCompiledInfo` 做一致性核对，不匹配就主动清空 `FJITDatabase`。也就是说，Angelscript 并不追求“DLL-only payload”，而是接受脚本/cache/编译信息分布在多份工件上。

```
[D11-Deep] Sidecar Strategy

[UnrealCSharp]
dotnet build Game.csproj
├─ Shared.props
│  ├─ GenerateDependencyFile=false
│  └─ DebugType=Embedded (Debug)
├─ Game.props AfterBuildPublish
│  └─ CopyDllsAfterPublish -> *.dll only
└─ runtime
   ├─ UE.dll / Game.dll / CustomProjects.dll
   └─ Mono net directory

[Angelscript]
script root
├─ Binds.Cache
├─ BindModules.Cache
├─ PrecompiledScript[_Config].Cache
├─ optional transpiled StaticJIT code in binary
└─ DataGuid / compiled-info compatibility check
```

关键源码 [1] `Reference/UnrealCSharp/Template/Shared.props:1-15`、`Reference/UnrealCSharp/Template/Game.props:16-27`、`Reference/UnrealCSharp/Source/Compiler/Private/FCSharpCompilerRunnable.cpp:248-257`、`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp:1035-1049`

```xml
<!-- =========================================================================
文件: Reference/UnrealCSharp/Template/Shared.props
位置: sidecar 策略的第一层约束
============================================================================ -->
<GenerateDependencyFile>false</GenerateDependencyFile>
<DebugType>Embedded</DebugType>
<!-- ★ 生成依赖清单被关闭，Debug 符号优先嵌入 DLL -->

<!-- =========================================================================
文件: Reference/UnrealCSharp/Template/Game.props
位置: Publish 后只搬运 DLL
============================================================================ -->
<Target Name="CopyDllsAfterPublish" AfterTargets="Publish">
    <ItemGroup>
        <PublishFiles Include="$(PublishDir)*.dll" />
    </ItemGroup>
    <Copy SourceFiles="@(PublishFiles)" DestinationFolder="$(ScriptOutputPath)" />
</Target>
<!-- ★ 即使 publish 生成其它 sidecar，这条 staging 合同当前也只复制 `*.dll` -->
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/Compiler/Private/FCSharpCompilerRunnable.cpp
// 函数: FCSharpCompilerRunnable::Run
// 位置: 编辑器编译线程显式执行的是 `dotnet build`
// ============================================================================
const auto CompileParam = FString::Printf(TEXT(
    "build \"%s\" --nologo -c %s"
),
    *FUnrealCSharpFunctionLibrary::GetGameProjectPath(),
    GetSolutionConfiguration() == ESolutionConfiguration::Debug ? TEXT("Debug") : TEXT("Release"));
// ★ publish/copy 由 props target 接管，而不是这里手写复制 sidecar

// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp
// 函数: GetFullAssemblyPublishPath / GetAssemblyPath
// 位置: runtime 装载合同直接按 DLL 清单与两层根目录工作
// ============================================================================
TArray<FString> FUnrealCSharpFunctionLibrary::GetFullAssemblyPublishPath()
{
    return TArrayBuilder<FString>().
           Add(GetFullUEPublishPath()).
           Add(GetFullGamePublishPath()).
           Append(GetFullCustomProjectsPublishPath()).
           Build();
}

TArray<FString> FUnrealCSharpFunctionLibrary::GetAssemblyPath()
{
    return TArrayBuilder<FString>().
           Add(FPaths::ProjectContentDir() / GetPublishDirectory()).
           Add(FMonoFunctionLibrary::GetNetDirectory()).
           Build();
}
```

关键源码 [2] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1466-1599`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 交付物不是单一 DLL 集，而是脚本/cache/编译信息多工件闭包
// ============================================================================
FAngelscriptBindDatabase::Get().Load(GetScriptRootDirectory() / TEXT("Binds.Cache"), bGeneratePrecompiledData);

if (plugin)
{
    FAngelscriptBinds::LoadBindModules(plugin->GetBaseDir() / "BindModules.Cache");
}

if (bUsePrecompiledData)
{
    FString Filename;
#if UE_BUILD_SHIPPING
    Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript_Shipping.Cache");
#elif UE_BUILD_TEST
    Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript_Test.Cache");
#elif UE_BUILD_DEVELOPMENT
    Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript_Development.Cache");
#endif

    if (!IFileManager::Get().FileExists(*Filename))
        Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript.Cache");

    if (IFileManager::Get().FileExists(*Filename))
    {
        PrecompiledData = new FAngelscriptPrecompiledData(Engine);
        PrecompiledData->Load(Filename);

        if (!PrecompiledData->IsValidForCurrentBuild())
        {
            delete PrecompiledData;
            PrecompiledData = nullptr;
        }
        else
        {
            const FStaticJITCompiledInfo* CompiledInfo = FStaticJITCompiledInfo::Get();
            if (CompiledInfo != nullptr && CompiledInfo->PrecompiledDataGuid != PrecompiledData->DataGuid)
            {
                FJITDatabase::Get().Clear();
            }
            // ★ cache 文件、编译进 binary 的 StaticJIT 信息、当前 build guid 三者要一起配套
        }
    }
}
```

设计取舍上，UnrealCSharp 这条链更偏“发布 DLL 就是 payload”，优点是装载面简单、交付根清晰；代价是 publish sidecar 与调试信息更多被压缩进 DLL/生成阶段，外显度较低。Angelscript 的多工件模型优点是缓存、bind database、StaticJIT 一致性都能独立建模和校验；代价是交付闭包天然比 DLL-only payload 更分散。

#### 新增对比结论

| 观察点 | 新发现 | 差距判断 |
| --- | --- | --- |
| 调试符号载体 | UnrealCSharp 的 generated workspace 在 Debug 下使用 `DebugType=Embedded`，当前 staging 合同又只复制 `*.dll`；Angelscript 的调试/编译信息分布在脚本、cache 与 debug server 协议里 | 实现方式不同 |
| 依赖 manifest 地位 | UnrealCSharp 当前 `Shared.props` 直接设 `GenerateDependencyFile=false`，且运行时装载合同只按 DLL 根目录工作；Angelscript 本来就不是程序集 manifest 模型 | 实现方式不同 |
| 交付工件形态 | UnrealCSharp 更接近 DLL-only managed payload；Angelscript 明确是 `Binds.Cache + BindModules.Cache + PrecompiledScript*.Cache + optional StaticJIT` 多工件闭包 | 实现方式不同 |
