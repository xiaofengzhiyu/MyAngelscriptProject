# UHT 插件生成注册代码 - 启动性能分析

## 1. 概述

本文档分析 Angelscript 插件中 **UHT Function Table 生成系统** 的完整架构、启动注册流程、与 UE5 原生注册机制的对比，以及当前启动时间开销的根因和优化方向。

---

## 2. 系统架构总览

```
UHT 编译阶段 (C# 工具链)                          运行时启动阶段 (C++ Runtime)
                                                    
AngelscriptFunctionTableExporter                    FAngelscriptRuntimeModule::StartupModule()
  [UhtExporter 入口]                                        |
        |                                                   v
        v                                           InitializeAngelscript()
AngelscriptFunctionTableCodeGenerator                       |
  .Generate()                                               v
        |                                           FAngelscriptEngine::Initialize()
        |--- LoadSupportedModules()                         |
        |      (解析 Build.cs 依赖列表)                      |--- PreInitialize_GameThread()
        |                                                   |--- Initialize_AnyThread()
        |--- GenerateModule() x N                           |       |
        |      |                                            |       |--- 加载 BindModules.Cache
        |      |--- CollectEntries()                        |       |--- LoadModule() x N (加载绑定模块)
        |      |      (遍历 UHT 类型树)                      |       |--- EnsureSharedStateCreated()
        |      |                                            |       |--- BindScriptTypes()
        |      |--- ShouldGenerate() 过滤                    |       |       |
        |      |--- TryBuild() 签名恢复                      |       |       v
        |      |--- 分片 (256 条/shard)                      |       |    CallBinds() ← 核心瓶颈
        |      |--- BuildShard() 生成 .cpp                   |       |       |
        |      v                                            |       |       |--- 手写 Bind_*.cpp (EOrder::Late)
        | AS_FunctionTable_Engine_000.cpp                    |       |       |--- UHT AS_FunctionTable_*
        | AS_FunctionTable_Engine_001.cpp                    |       |       |      (EOrder::Late + 50)
        | ...                                               |       |       v
        |                                                   |       |   AddFunctionEntry() x N
        |--- DeleteStaleOutputs()                           |       |       |
        |--- WriteGenerationSummary()                       |       |       v
        v                                                   |       |   ClassFuncMaps 填充完成
  AS_FunctionTable_Summary.json                             |       |
  AS_FunctionTable_Entries.csv                              |       |--- Bind_BlueprintCallable 消费
  AS_FunctionTable_SkippedEntries.csv                       |       |       ClassFuncMaps
                                                            |       |       |
                                                            |       |       |--- 有直接绑定 → 原生调用
                                                            |       |       |--- 无绑定 → 反射回退
                                                            |       |
                                                            |       |--- InitialCompile()
                                                            |
                                                            |--- PostInitialize_GameThread()
                                                            v
                                                      引擎就绪, 脚本可执行
```

---

## 3. UHT 代码生成工具链详解

### 3.1 文件结构

```
Plugins/Angelscript/Source/AngelscriptUHTTool/
    AngelscriptUHTTool.cs                          // 空命名空间标记
    AngelscriptFunctionTableExporter.cs            // UHT Exporter 入口 (174 行)
    AngelscriptFunctionTableCodeGenerator.cs       // 代码生成核心 (533 行)
    AngelscriptFunctionSignatureBuilder.cs         // 函数签名构建 (135 行)
    AngelscriptHeaderSignatureResolver.cs          // C++ 头文件解析 (773 行)
```

### 3.2 Exporter 入口

```csharp
// AngelscriptFunctionTableExporter.cs
// 作用: UHT 编译完成后的导出入口, 注册为 UHT 插件
// 生命周期: UBT 编译时由 UHT 框架调用一次
// 输出: AS_FunctionTable_*.cpp 文件 + 诊断 CSV

[UnrealHeaderTool]
internal static class AngelscriptFunctionTableExporter
{
    [UhtExporter(
        Name = "AngelscriptFunctionTable",
        Description = "Exports Angelscript function table data",
        Options = UhtExporterOptions.Default | UhtExporterOptions.CompileOutput,
        CppFilters = ["AS_FunctionTable_*.cpp"],  // 告知 UBT 本插件管理这些 .cpp
        ModuleName = "AngelscriptRuntime")]        // 生成代码编译进 AngelscriptRuntime 模块
    private static void Export(IUhtExportFactory factory)
    {
        // 1. 核心: 调用代码生成器, 输出 AS_FunctionTable_*.cpp 分片
        int generatedFileCount = AngelscriptFunctionTableCodeGenerator.Generate(factory);

        // 2. 遍历所有 UHT 模块, 统计 BlueprintCallable 函数覆盖率
        foreach (UhtModule module in factory.Session.Modules)
        {
            CountBlueprintCallableFunctions(module.ShortName, module.ScriptPackage, ...);
        }

        // 3. 输出诊断报告: 跳过条目 CSV + 跳过原因汇总 CSV
        WriteSkippedEntriesCsv(factory, skippedEntries);
        WriteSkippedReasonSummaryCsv(factory, skippedEntries);
    }
}
```

### 3.3 代码生成核心流程

```csharp
// AngelscriptFunctionTableCodeGenerator.cs
// 作用: 将 UHT 反射数据转化为 C++ 注册代码
// 核心常量: MaxEntriesPerShard = 256 (每个 .cpp 文件最多 256 个函数条目)

internal static class AngelscriptFunctionTableCodeGenerator
{
    private const int MaxEntriesPerShard = 256;  // 分片大小上限

    public static int Generate(IUhtExportFactory factory)
    {
        // 第一步: 解析 AngelscriptRuntime.Build.cs 获取支持的模块列表
        AngelscriptSupportedModules supportedModules = LoadSupportedModules(factory);

        // 第二步: 对每个支持的模块生成注册代码
        foreach (UhtModule module in factory.Session.Modules)
        {
            if (!supportedModules.All.Contains(module.ShortName))
                continue;  // 跳过不在依赖列表中的模块

            GenerateModule(factory, module, ...);
        }

        // 第三步: 清理过时的生成文件 + 写入汇总报告
        DeleteStaleOutputs(factory, generatedPaths);
        WriteGenerationSummary(factory, moduleSummaries, csvEntries, generatedFileCount);
    }
}
```

### 3.4 函数条目收集与过滤

```csharp
// AngelscriptFunctionTableCodeGenerator.cs - CollectEntries()
// 作用: 递归遍历 UHT 类型树, 收集符合条件的 BlueprintCallable 函数
// 调用链: Generate() → GenerateModule() → CollectEntries()

private static void CollectEntries(IUhtExportFactory factory, UhtType type,
    SortedSet<string> includes, List<AngelscriptGeneratedFunctionEntry> entries)
{
    if (type is UhtClass classObj)
    {
        foreach (UhtType child in classObj.Children)
        {
            if (child is UhtFunction function && ShouldGenerate(classObj, function))
            {
                // 收集 include 路径
                string includePath = factory.GetModuleShortestIncludePath(...);
                includes.Add(normalizedIncludePath);

                string eraseMacro;
                if (classObj.ClassType == UhtClassType.Interface)
                {
                    // 接口函数无法直接绑定, 使用桩条目
                    eraseMacro = "ERASE_NO_FUNCTION()";
                }
                else if (AngelscriptFunctionSignatureBuilder.TryBuild(
                    classObj, function, out var signature, out _))
                {
                    // 签名恢复成功 → 生成直接绑定宏
                    eraseMacro = signature!.BuildEraseMacro();
                }
                else
                {
                    // 签名恢复失败 → 桩条目, 运行时走反射回退
                    eraseMacro = "ERASE_NO_FUNCTION()";
                }

                entries.Add(new AngelscriptGeneratedFunctionEntry(
                    classObj.SourceName, function.SourceName, eraseMacro));
            }
        }
    }

    // 递归处理子类型
    foreach (UhtType child in type.Children)
        CollectEntries(factory, child, includes, entries);
}
```

### 3.5 过滤规则 (ShouldGenerate)

```csharp
// AngelscriptFunctionTableCodeGenerator.cs - ShouldGenerate()
// 作用: 判断一个 UFunction 是否应该生成注册条目
// 返回 false 的条件汇总:

private static bool ShouldGenerate(UhtClass classObj, UhtFunction function)
{
    // ❌ 1. 头文件不存在或来自 /Private/ 目录
    if (classObj.HeaderFile == null || !IsSupportedHeader(classObj.HeaderFile.FilePath))
        return false;

    // ❌ 2. 不是 BlueprintCallable 或 BlueprintPure
    if (!AngelscriptFunctionTableExporter.IsBlueprintCallable(function))
        return false;

    // ❌ 3. 标记了 NotInAngelscript 元数据
    if (function.MetaData.ContainsKey("NotInAngelscript"))
        return false;

    // ❌ 4. BlueprintInternalUseOnly 且无 UsableInAngelscript 覆盖
    if (function.MetaData.ContainsKey("BlueprintInternalUseOnly")
        && !function.MetaData.ContainsKey("UsableInAngelscript"))
        return false;

    // ❌ 5. 硬编码排除列表
    if (classObj.SourceName == "UUniversalObjectLocatorScriptingExtensions"
        && (function.SourceName == "MakeUniversalObjectLocator" || ...))
        return false;

    // ❌ 6. CustomThunk 函数 (需要手写实现)
    return !function.FunctionExportFlags.ToString().Contains("CustomThunk");
}
```

### 3.6 签名恢复三阶段管线

```csharp
// AngelscriptFunctionSignatureBuilder.cs - TryBuild()
// 作用: 将 UHT 元数据恢复为可编译的 C++ 函数签名
// 输出: AngelscriptFunctionSignature record → BuildEraseMacro() 生成 ERASE_* 宏

public static bool TryBuild(UhtClass classObj, UhtFunction function,
    out AngelscriptFunctionSignature? signature, out string? failureReason)
{
    // ====== 阶段 1: 头文件精确解析 ======
    // 尝试从 C++ 头文件中解析出真实声明
    if (AngelscriptHeaderSignatureResolver.TryBuild(classObj, function,
        out signature, out failureReason))
        return true;  // 成功 → 使用头文件中的精确签名

    // ====== 阶段 2: 不可恢复的失败 ======
    if (failureReason == "non-public")       return false;  // 非公开函数
    if (failureReason == "unexported-symbol") return false;  // 无导出符号
    if (failureReason == "overloaded-unresolved"             // 重载歧义
        && !IsWhitelistedDirectBindFallback(classObj, function))
        return false;

    // ====== 阶段 3: UHT 元数据回退 ======
    // 从 UHT 的 ParameterProperties 和 ReturnProperty 重建签名
    List<string> parameterTypes = new();
    foreach (UhtType parameterType in function.ParameterProperties.Span)
    {
        if (parameterType is not UhtProperty property) return false;
        if (property.ArrayDimensions != null) return false;  // 静态数组不支持
        parameterTypes.Add(BuildParameterType(property));
    }

    string returnType = function.ReturnProperty is UhtProperty returnProperty
        ? BuildReturnType(returnProperty) : "void";

    signature = new AngelscriptFunctionSignature(
        classObj.SourceName, function.SourceName,
        returnType, parameterTypes,
        IsStatic: HasFunctionFlag(function, "Static"),
        IsConst:  HasFunctionFlag(function, "Const"),
        UseExplicitSignature: true);  // 使用显式签名 (带完整参数类型)
    return true;
}
```

### 3.7 ERASE 宏生成

```csharp
// AngelscriptFunctionSignature record - BuildEraseMacro()
// 作用: 根据签名信息生成对应的类型擦除宏调用
// 输出格式举例:
//   显式签名: ERASE_METHOD_PTR(AActor, GetActorLocation, () const, ERASE_ARGUMENT_PACK(FVector))
//   自动签名: ERASE_AUTO_METHOD_PTR(AActor, GetActorLocation)

public string BuildEraseMacro()
{
    if (UseExplicitSignature)
    {
        // 显式签名模式: 完整参数列表 + 返回类型
        string parameterPack = ParameterTypes.Count == 0
            ? "()" : $"({string.Join(", ", ParameterTypes)})";
        if (IsConst && !IsStatic)
            parameterPack += " const";

        return IsStatic
            ? $"ERASE_FUNCTION_PTR({OwningType}::{FunctionName}, {parameterPack}, ERASE_ARGUMENT_PACK({ReturnType}))"
            : $"ERASE_METHOD_PTR({OwningType}, {FunctionName}, {parameterPack}, ERASE_ARGUMENT_PACK({ReturnType}))";
    }

    // 自动签名模式: 编译器自动推导
    return IsStatic
        ? $"ERASE_AUTO_FUNCTION_PTR({OwningType}::{FunctionName})"
        : $"ERASE_AUTO_METHOD_PTR({OwningType}, {FunctionName})";
}
```

### 3.8 分片生成

```csharp
// AngelscriptFunctionTableCodeGenerator.cs - BuildShard()
// 作用: 为每个分片生成一个完整的 .cpp 文件
// 生成文件命名: AS_FunctionTable_{ModuleName}_{ShardIndex:D3}.cpp
// 条目上限: 256 条/分片

private static StringBuilder BuildShard(string moduleShortName, bool editorOnly,
    SortedSet<string> includes, List<AngelscriptGeneratedFunctionEntry> entries,
    int startIndex, int entryCount, int shardIndex, int shardCount)
{
    StringBuilder builder = new();

    if (editorOnly)
        builder.AppendLine("#if WITH_EDITOR");

    builder.AppendLine("PRAGMA_DISABLE_DEPRECATION_WARNINGS");
    builder.AppendLine("#include \"CoreMinimal.h\"");
    builder.AppendLine("#include \"Core/AngelscriptBinds.h\"");
    builder.AppendLine("#include \"Core/AngelscriptEngine.h\"");
    builder.AppendLine("#include \"Core/FunctionCallers.h\"");

    // 模块相关头文件
    foreach (string include in includes)
        builder.Append("#include \"").Append(include).AppendLine("\"");

    // 生成 AS_FORCE_LINK 强制链接的静态绑定对象
    builder.Append("AS_FORCE_LINK const FAngelscriptBinds::FBind ")
        .Append($"Bind_AS_FunctionTable_{moduleShortName}_{shardIndex:D3}")
        .AppendLine("((int32)FAngelscriptBinds::EOrder::Late + 50, []()");
    builder.AppendLine("{");

    // 逐条写入 AddFunctionEntry 调用
    for (int i = startIndex; i < startIndex + entryCount; i++)
        builder.AppendLine(entries[i].BuildRegistrationLine());

    builder.AppendLine("});");
    builder.AppendLine("PRAGMA_ENABLE_DEPRECATION_WARNINGS");

    if (editorOnly)
        builder.AppendLine("#endif");

    return builder;
}
```

**生成的 C++ 代码示例:**

```cpp
// 文件: AS_FunctionTable_Engine_000.cpp (自动生成, 勿手动修改)
PRAGMA_DISABLE_DEPRECATION_WARNINGS
#include "CoreMinimal.h"
#include "Core/AngelscriptBinds.h"
#include "Core/AngelscriptEngine.h"
#include "Core/FunctionCallers.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"
// ... 更多 include ...

AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_AS_FunctionTable_Engine_000(
    (int32)FAngelscriptBinds::EOrder::Late + 50, []()
{
    FAngelscriptBinds::AddFunctionEntry(AActor::StaticClass(), "GetActorLocation",
        { ERASE_METHOD_PTR(AActor, GetActorLocation, () const, ERASE_ARGUMENT_PACK(FVector)) });
    FAngelscriptBinds::AddFunctionEntry(AActor::StaticClass(), "SetActorLocation",
        { ERASE_METHOD_PTR(AActor, SetActorLocation, (const FVector&), ERASE_ARGUMENT_PACK(bool)) });
    // ... 最多 256 条 ...

    UE_LOG(Angelscript, Log,
        TEXT("[UHT] Registered %d generated BlueprintCallable entries for module %s shard %d/%d"),
        256, TEXT("Engine"), 1, 16);
});
PRAGMA_ENABLE_DEPRECATION_WARNINGS
```

---

## 4. 运行时注册机制详解

### 4.1 核心数据结构

```cpp
// AngelscriptBinds.h - 运行时注册状态
// 作用: 存储所有注册数据的中心仓库, 每个引擎实例独立

struct FAngelscriptBindState
{
    // 核心: UClass → 函数名 → 函数入口 (类型擦除指针 + 调用器)
    TMap<UClass*, TMap<FString, FFuncEntry>> ClassFuncMaps;

    // 运行时/编辑器类数据库
    TMap<FString, TArray<TObjectPtr<UClass>>> RuntimeClassDB;
#if WITH_EDITOR
    TMap<FString, TArray<TObjectPtr<UClass>>> EditorClassDB;
#endif

    // 绑定模块名称列表 (从 BindModules.Cache 加载)
    TArray<FString> BindModuleNames;

    // 跳过绑定的配置
    TMap<UClass*, TSet<FString>> SkipBinds;       // 按类+函数名跳过
    TSet<TTuple<FName, FName>> SkipBindNames;      // 按名称对跳过
    TSet<FName> SkipBindClasses;                   // 按类名跳过

    // 上一个绑定的函数/属性 ID (用于链式配置)
    int32 PreviouslyBoundFunction = -1;
    int32 PreviouslyBoundGlobalProperty = -1;
};
```

### 4.2 FFuncEntry 与类型擦除

```cpp
// FunctionCallers.h
// 作用: 将 C++ 成员函数指针/全局函数指针统一擦除为 25 字节的通用表示

// 类型擦除的函数指针容器 (25 字节, 容纳 MSVC 64 位虚继承成员指针)
struct FGenericFuncPtr
{
    union {
        struct { FTypeErasedFuncPtr  func; char dummy[25 - sizeof(FTypeErasedFuncPtr)]; }  f;
        struct { FTypeErasedMethodPtr mthd; char dummy[25 - sizeof(FTypeErasedMethodPtr)]; } m;
        char dummy[25];
    } ptr;
    uint8 flag;  // 1 = generic, 2 = global func, 3 = method
};

// 函数入口: 存储在 ClassFuncMaps 中的最终条目
struct FFuncEntry
{
    FGenericFuncPtr FuncPtr;                    // 类型擦除的函数指针 (26 bytes)
    ASAutoCaller::FunctionCaller Caller;        // 泛型调用器 (通过 void** 参数数组调用)
    bool bReflectiveFallbackBound = false;      // 是否使用了反射回退
};

// ERASE 宏族: 编译时生成类型擦除 + 调用器对
#define ERASE_METHOD_PTR(c, m, p, r) \
    FMethodPtrHelper<sizeof(void(c::*)())>::Convert( \
        static_cast<r(c::*)p>(&c::m)),            /* → FGenericFuncPtr */ \
    ASAutoCaller::MakeFunctionCaller(              \
        static_cast<r(c::*)p>(&c::m))             /* → FunctionCaller */

#define ERASE_AUTO_METHOD_PTR(c, m) \
    MakeAutoMethodPtr(&c::m),                      /* → FGenericFuncPtr */ \
    ASAutoCaller::MakeFunctionCaller(&c::m)        /* → FunctionCaller */

#define ERASE_NO_FUNCTION() \
    FGenericFuncPtr{},                             /* → 空指针 */ \
    ASAutoCaller::FunctionCaller{}                 /* → 空调用器 */
```

### 4.3 绑定注册与执行

```cpp
// AngelscriptBinds.h/cpp - AddFunctionEntry
// 作用: 将函数入口写入 ClassFuncMaps, 首次写入优先 (手写绑定 > UHT 生成)
// 调用时机: CallBinds() 执行所有 FBind lambda 时
// 性能特性: O(1) 哈希表插入, 无排序

static void AddFunctionEntry(UClass* Class, FString Name, FFuncEntry Entry)
{
    auto& ClassFuncMaps = GetClassFuncMaps();
    if (ClassFuncMaps.Contains(Class))
    {
        if (!ClassFuncMaps[Class].Contains(Name))  // 首次写入优先!
        {
            ClassFuncMaps[Class].Add(Name, Entry);
        }
    }
    else
    {
        ClassFuncMaps.Add(Class, TMap<FString, FFuncEntry>()).Add(Name, Entry);
    }
}
```

### 4.4 绑定排序与执行

```cpp
// AngelscriptBinds.cpp
// 作用: 收集所有 FBind, 排序后顺序执行

struct FBindFunction
{
    FName BindName;
    int32 BindOrder;           // 排序优先级
    TFunction<void()> Function; // 绑定 lambda
};

static TArray<FBindFunction>& GetBindArray()
{
    static TArray<FBindFunction> BindArray;  // 全局静态数组
    return BindArray;
}

// 注册: FBind 构造函数 → RegisterBinds() → 追加到全局数组
void FAngelscriptBinds::RegisterBinds(FName BindName, int32 BindOrder,
    TFunction<void()> Function)
{
    GetBindArray().Add({
        BindName.IsNone() ? MakeUnnamedBindName() : BindName,
        BindOrder, MoveTemp(Function)
    });
}

// 执行: 排序 + 逐条调用 (单线程, 串行)
void FAngelscriptBinds::CallBinds(const TSet<FName>& DisabledBindNames)
{
    for (const FBindFunction& Bind : GetSortedBindArray())  // O(n log n) 排序
    {
        if (DisabledBindNames.Contains(Bind.BindName))
        {
            UE_LOG(Angelscript, Log, TEXT("Skipping bind '%s'"), *Bind.BindName.ToString());
            continue;
        }
        Bind.Function();  // 逐条执行绑定 lambda
    }
}
```

### 4.5 EOrder 优先级体系

```cpp
// 绑定执行顺序决定了谁先注册 → 首次写入优先
enum class EOrder : int32
{
    Early  = -100,  // 基础类型注册 (FVector, FRotator 等)
    Normal =    0,  // 标准绑定 (大部分手写 Bind_*.cpp)
    Late   =  100,  // 后期绑定 (依赖其他绑定的类型)
};

// UHT 生成的注册使用 EOrder::Late + 50 = 150
// 这意味着: 手写绑定 (Late=100) 先于 UHT 生成 (150) 执行
// 由于 AddFunctionEntry 首次写入优先, 手写绑定始终覆盖 UHT 生成
```

---

## 5. 运行时绑定消费: 三层决策体系

### 5.1 消费流程

当 Angelscript 引擎需要将 BlueprintCallable UFunction 暴露给脚本时，按以下优先级决策:

```
UFunction 发现
    |
    v
检查 ClassFuncMaps[UClass][FunctionName]
    |
    |--- 存在且 FuncPtr 有效 ──→ 直接原生绑定 (Tier 1)
    |                              使用 FGenericFuncPtr + FunctionCaller
    |                              零反射开销, 近原生 ABI 调用速度
    |
    |--- 存在但 FuncPtr 为空 ──→ ERASE_NO_FUNCTION() 桩条目
    |     (即 ERASE_NO_FUNCTION)   转入反射回退 (Tier 3)
    |
    |--- 不存在 ────────────────→ 反射回退 (Tier 3)
                                   使用 ProcessEvent + FProperty 编组
                                   bReflectiveFallbackBound = true
```

### 5.2 反射回退机制

```cpp
// 反射回退路径 (简化)
// 当 ClassFuncMaps 中无直接绑定时, 使用 UE 反射系统调用

void InvokeReflectiveUFunctionFromGenericCall(
    UObject* TargetObject, UFunction* Function, asIScriptGeneric* Generic)
{
    // 1. 栈分配参数缓冲区
    uint8* ParameterBuffer = (uint8*)FMemory_Alloca(Function->ParmsSize);

    // 2. 从 Angelscript 参数逐个拷贝到 UE 参数缓冲区
    for (TFieldIterator<FProperty> It(Function); It; ++It)
    {
        FProperty* Property = *It;
        void* Dest = Property->ContainerPtrToValuePtr<void>(ParameterBuffer);
        void* Src  = ResolveScriptArgumentAddress(Property, ScriptArgAddress);
        Property->CopySingleValue(Dest, Src);
    }

    // 3. 通过 UE 反射系统调用 (ProcessEvent)
    TargetObject->ProcessEvent(Function, ParameterBuffer);

    // 4. 拷贝回 out/ref 参数
    for (auto& OutRef : OutReferences)
    {
        OutRef.Property->CopySingleValue(OutRef.ScriptValue,
            OutRef.Property->ContainerPtrToValuePtr<void>(ParameterBuffer));
    }
}
```

---

## 6. UE5 原生注册 vs 当前插件注册

### 6.1 核心差异对比

```
UE5 原生 (Z_Construct + StaticRegisterNatives)         Angelscript 插件 (AddFunctionEntry)
                                                        
目标: 构建反射元数据 (UClass/UFunction)                  目标: 构建直接调用表 (FGenericFuncPtr)
                                                        
Z_Construct_UClass_AActor()                             AddFunctionEntry(AActor::StaticClass(),
  → 创建 UClass 对象                                        "GetActorLocation",
  → 填充属性描述符                                           { ERASE_METHOD_PTR(...) })
  → 注册到 GUObjectArray                                    → 存入 ClassFuncMaps TMap
                                                        
调用方式: ProcessEvent(UFunction*, Params)               调用方式: FuncPtr.Call(void** Args)
  → 反射查找 FNativeFuncPtr                                → 直接调用类型擦除的原生指针
  → 通过 thunk 分派                                         → 编译时生成的参数编组
  → 运行时参数编组 (FProperty 迭代)                          → 零反射开销
                                                        
注册时机: 模块静态初始化                                  注册时机: CallBinds() 中排序执行
  (DLL 加载时 FRegisterCompiledInInfo)                     (引擎初始化时按 EOrder 排序)
                                                        
线程安全: ✅ 是 (CDO 构造有锁)                            线程安全: ❌ 否 (AngelScript API 非线程安全)
                                                        
增量更新: ✅ 按需构造 (懒加载)                            增量更新: ❌ 每次启动全量执行
```

### 6.2 调用链对比图

```
====== UE5 原生 BlueprintCallable 调用 ======

Blueprint VM / C++ 调用
    |
    v
UObject::ProcessEvent(UFunction*, void* Parms)
    |
    v
UFunction::Invoke(UObject*, FFrame&, RESULT_DECL)      // 虚函数分派
    |
    v
FNativeFuncPtr Thunk                                    // void(*)(UObject*, FFrame&, void*)
    |                                                   // 编译时生成, 参数从 FFrame 栈解码
    v
实际 C++ 函数体


====== Angelscript 直接绑定调用 (Tier 1) ======

Angelscript VM 执行脚本
    |
    v
asIScriptContext::Execute()
    |
    v
asCScriptFunction (已注册的系统函数)
    |
    |--- asCALL_THISCALL ──→ 直接通过 asSFuncPtr 调用成员函数
    |                          无中间层, 直接 ABI 调用
    |
    |--- asCALL_CDECL_OBJFIRST ──→ 全局函数, this 作为第一参数
    v
实际 C++ 函数体


====== Angelscript 反射回退调用 (Tier 3) ======

Angelscript VM 执行脚本
    |
    v
asIScriptContext::Execute()
    |
    v
asCScriptFunction (asCALL_GENERIC)
    |
    v
CallBlueprintCallableReflectiveFallback()
    |
    v
InvokeReflectiveUFunctionFromGenericCall()
    |--- 分配参数缓冲区 (栈上)
    |--- FProperty 逐个拷贝参数
    |--- UObject::ProcessEvent()
    |--- 拷贝回 out/ref 参数
    v
实际 C++ 函数体 (通过 UE 反射间接调用)
```

---

## 7. 启动性能瓶颈分析

### 7.1 时间分布

```
启动耗时构成 (概念模型, 非精确测量)

FAngelscriptEngine::Initialize()
├── PreInitialize_GameThread()           ~2ms      (分配器, 配置加载)
├── Initialize_AnyThread()               ~主要开销
│   ├── 引擎属性配置                      ~1ms      (25+ SetEngineProperty 调用)
│   ├── BindModules.Cache 加载            ~1ms      (文件 I/O)
│   ├── Bind 模块 DLL 加载               ~?ms      (FModuleManager::LoadModule)
│   ├── EnsureSharedStateCreated()        ~1ms
│   │
│   ├── ★ CallBinds() ← 核心瓶颈 ★      ~100-500ms+
│   │   ├── GetSortedBindArray()          ~1ms      (排序)
│   │   ├── 手写 Bind_*.cpp 执行          ~??ms     (123 个绑定文件)
│   │   │   └── 每个: RegisterObjectType / RegisterObjectMethod x N
│   │   │       → AngelScript 引擎内部字符串解析 + 类型注册
│   │   │
│   │   ├── ★ UHT AS_FunctionTable_* ★   ~??ms     (数千条 AddFunctionEntry)
│   │   │   └── 每条: StaticClass() 查找 + TMap 插入
│   │   │       → 数千次 UClass::StaticClass() + FString 哈希
│   │   │
│   │   └── 其他绑定                       ~??ms
│   │
│   ├── BindScriptTypes()                 ~??ms     (消费 ClassFuncMaps)
│   │   └── 遍历所有 UClass 的 BlueprintCallable 函数
│   │       对每个: 查询 ClassFuncMaps → 直接绑定 or 反射回退
│   │       → RegisterObjectMethod() x N (注册到 AngelScript 引擎)
│   │
│   ├── InitialCompile()                  ~??ms     (脚本编译)
│   │   └── 发现 .as 文件 → 预处理 → 编译模块
│   │
│   └── 写入/加载预编译缓存               ~??ms
│
└── PostInitialize_GameThread()           ~1ms      (广播完成事件)
```

### 7.2 瓶颈根因

**根因 1: CallBinds() 全量串行执行**

```
问题: 每次启动都执行所有绑定, 无缓存/增量机制
                                                    
启动 → CallBinds()
         ↓
    [排序全部 FBind]  ← O(n log n), n = 数百个绑定
         ↓
    [逐条执行 lambda] ← 串行, 无法并行
         ↓
    每个 lambda 内部:
      RegisterObjectType()    → AngelScript 引擎字符串解析
      RegisterObjectMethod()  → 签名解析 + 函数注册
      RegisterObjectProperty() → 属性注册
      AddFunctionEntry()      → TMap 插入
         ↓
    全部执行完毕才能进入下一阶段
```

**根因 2: AngelScript API 非线程安全**

```
以下 AngelScript API 均持有全局状态, 不可并行调用:
                                                    
asIScriptEngine::RegisterObjectType()    ← 修改全局类型表
asIScriptEngine::RegisterObjectMethod()  ← 修改全局函数表
asIScriptEngine::SetDefaultNamespace()   ← 修改全局命名空间
GetPreviouslyBoundFunctionRef()          ← 静态变量, 串行耦合
```

**根因 3: UHT 生成的大量条目全量注册**

```
UHT 生成数据规模 (以实际项目为例):
                                                    
总 BlueprintCallable 函数: ~13,469
签名恢复成功 (直接绑定):   ~9,017  (67%)
签名恢复失败 (桩条目):     ~4,452  (33%)
                                                    
即使桩条目 (ERASE_NO_FUNCTION) 仍需:
  - UClass::StaticClass() 调用
  - FString 构造
  - TMap 查找 + 插入
  x 4,452 次 = 非平凡的累积开销
```

**根因 4: BindScriptTypes() 二次遍历**

```
CallBinds() 填充 ClassFuncMaps 后,
BindScriptTypes() 再次遍历所有 UClass:
                                                    
对每个 UClass:
  对每个 BlueprintCallable UFunction:
    查询 ClassFuncMaps[UClass][FunctionName]
    if 有直接绑定:
      RegisterObjectMethod(签名, 直接指针)
    else:
      创建反射回退签名
      RegisterObjectMethod(签名, 反射分派器)
                                                    
这是第二轮全量遍历, 叠加 RegisterObjectMethod 的字符串解析开销
```

---

## 8. UE5 原生为什么不存在这个问题

```
UE5 原生模式:
                                                    
1. Z_Construct 是懒加载的:
   Z_Construct_UClass_AActor() 只在首次访问时执行
   → 启动时不会一次性构造所有 UClass
                                                    
2. FNativeFuncPtr 通过 StaticRegisterNatives 一次性批量注册:
   static FNativeFunctionRegistrar Registrar(
       UMyClass::StaticClass(),
       {{"MyFunc", &UMyClass::execMyFunc}});
   → 简单的函数指针数组, 无字符串解析
                                                    
3. ProcessEvent 在调用时才做参数编组:
   不需要启动时预注册调用约定
   → 启动零额外成本
                                                    
4. 反射元数据通过代码生成嵌入 DLL:
   .gen.cpp 编译进模块, DLL 加载时自动初始化
   → 增量编译, 按需加载

而 Angelscript 插件必须:
   启动时 → 全量扫描所有 UFunction
   → 为每个函数构建 AngelScript 签名字符串
   → 调用 RegisterObjectMethod 注册到 AS 引擎
   → 这个过程无法懒加载 (AS 引擎需要完整类型信息才能编译脚本)
```

---

## 9. 优化方向

### 9.1 方向 A: 减少注册条目数量

```
策略: 桩条目延迟注册
                                                    
当前: ERASE_NO_FUNCTION() 桩条目仍然调用 AddFunctionEntry()
     在 ClassFuncMaps 中占位, 运行时最终走反射回退
                                                    
优化: 桩条目不调用 AddFunctionEntry()
     → BindScriptTypes() 发现无条目时直接走反射回退
     → 减少 ~4,452 次 TMap 插入操作
     → 减少 ClassFuncMaps 内存占用
                                                    
预估收益: 减少 ~33% 的 AddFunctionEntry 调用
风险: 低 (桩条目本质上就是空操作)
```

### 9.2 方向 B: 注册结果缓存

```
策略: 将 AngelScript 引擎的注册状态序列化/反序列化
                                                    
当前: 每次启动重新执行所有 RegisterObjectType/Method
     AngelScript 引擎从零构建类型系统
                                                    
优化: 首次启动后将 AS 引擎内部状态快照到磁盘
     后续启动直接加载快照, 跳过注册阶段
                                                    
挑战: AngelScript 引擎内部状态复杂, 序列化困难
     类型表、函数表、命名空间等均有交叉引用
     需要 AS 引擎层面支持 (当前不支持)
                                                    
预估收益: 理论上可将注册时间降低 80%+
风险: 高 (需要修改 AngelScript 引擎核心)
```

### 9.3 方向 C: 分阶段按需注册

```
策略: 只在脚本实际引用时才注册函数
                                                    
当前: 启动时注册所有 BlueprintCallable 函数
     即使脚本只用到其中 10%
                                                    
优化: 
  阶段 1 (启动时): 只注册类型 (RegisterObjectType)
  阶段 2 (编译时): 当编译器遇到函数调用时, 按需注册
                                                    
挑战: AngelScript 编译器需要完整类型信息
     方法签名影响重载决议
     需要修改编译器的类型查找逻辑
                                                    
预估收益: 大幅减少启动注册量 (只注册实际使用的函数)
风险: 中高 (需要修改编译器行为, 可能影响补全等功能)
```

### 9.4 方向 D: 批量注册 API

```
策略: 减少 AngelScript 引擎 API 调用次数
                                                    
当前: 每个方法 = 1 次 RegisterObjectMethod() 调用
     每次调用: 签名字符串解析 + 类型查找 + 注册
                                                    
优化: 引入批量注册 API
     RegisterObjectMethods(ClassName, MethodArray, Count)
     → 一次性解析类名, 批量注册所有方法
     → 减少重复的类名查找开销
                                                    
挑战: 需要修改/扩展 AngelScript 引擎 API
预估收益: 减少 ~30-50% 的注册时间 (消除重复查找)
风险: 中 (需要 AS 引擎层改动, 但改动可控)
```

### 9.5 方向 E: 预编译数据路径优化

```
策略: 利用已有的预编译数据机制绕过注册
                                                    
当前: bUsePrecompiledData 路径已存在但不跳过绑定
     预编译数据包含编译后的字节码, 但仍需注册类型
                                                    
优化: 预编译数据扩展为包含 AS 引擎类型快照
     Shipping 构建: 加载快照 → 跳过 CallBinds()
     Editor 构建: 仍执行完整注册 (支持热重载)
                                                    
预估收益: Shipping 构建启动时间减少 80%+
风险: 中 (仅影响 Shipping, Editor 保持原有行为)
```

### 9.6 优化优先级矩阵

```
方向                  收益     风险    实现难度   推荐优先级
                                                
A: 桩条目延迟注册     低-中    低      低         ★★★★★ (立即可做)
D: 批量注册 API       中       中      中         ★★★★☆ (中期)
E: 预编译数据扩展     高       中      中-高      ★★★☆☆ (中期, Shipping)
C: 按需注册           高       中-高   高         ★★☆☆☆ (长期)
B: 状态缓存           极高     高      极高       ★☆☆☆☆ (探索性)
```

---

## 10. 完整生命周期与调用链

```
[UBT 编译时]
                                                
UnrealBuildTool
    └─ UnrealHeaderTool (UHT)
        └─ [UhtExporter] AngelscriptFunctionTableExporter.Export()
            ├─ AngelscriptFunctionTableCodeGenerator.Generate()
            │   ├─ LoadSupportedModules()
            │   │   └─ 解析 AngelscriptRuntime.Build.cs
            │   │       └─ 提取 DependencyModuleNames.AddRange() 中的模块名
            │   │
            │   ├─ [per module] GenerateModule()
            │   │   ├─ CollectEntries()                                [递归遍历 UHT 类型树]
            │   │   │   ├─ ShouldGenerate()                            [过滤规则]
            │   │   │   └─ AngelscriptFunctionSignatureBuilder.TryBuild()
            │   │   │       ├─ AngelscriptHeaderSignatureResolver.TryBuild()  [阶段1: 头文件解析]
            │   │   │       │   ├─ GetSanitizedHeader()                [去注释+缓存]
            │   │   │       │   ├─ FindCandidates()                    [查找函数声明]
            │   │   │       │   ├─ 公开性检查                            [过滤 private/protected]
            │   │   │       │   └─ HasLinkableExport()                 [检查链接可见性]
            │   │   │       │
            │   │   │       ├─ [阶段2: 不可恢复失败检查]
            │   │   │       │   └─ non-public / unexported / overloaded
            │   │   │       │
            │   │   │       └─ [阶段3: UHT 元数据回退]
            │   │   │           ├─ BuildParameterType() x N
            │   │   │           └─ BuildReturnType()
            │   │   │
            │   │   ├─ 排序 (ClassName, FunctionName)
            │   │   ├─ 分片 (256 条/shard)
            │   │   └─ BuildShard() → AS_FunctionTable_Module_NNN.cpp
            │   │
            │   ├─ DeleteStaleOutputs()
            │   └─ WriteGenerationSummary()
            │       ├─ AS_FunctionTable_Summary.json
            │       ├─ AS_FunctionTable_ModuleSummary.csv
            │       └─ AS_FunctionTable_Entries.csv
            │
            ├─ CountBlueprintCallableFunctions()              [覆盖率验证]
            ├─ WriteSkippedEntriesCsv()
            └─ WriteSkippedReasonSummaryCsv()


[C++ 编译时]
                                                
MSVC/Clang 编译 AS_FunctionTable_*.cpp
    └─ AS_FORCE_LINK 确保静态对象不被链接器优化掉
        └─ FBind 静态构造函数执行
            └─ RegisterBinds(EOrder::Late + 50, lambda)
                └─ GetBindArray().Add({BindName, 150, lambda})


[运行时启动]
                                                
UE 模块加载器
    └─ FAngelscriptRuntimeModule::StartupModule()
        └─ InitializeAngelscript()                            [单次执行守卫]
            └─ FAngelscriptEngine::Initialize()
                │
                ├─ PreInitialize_GameThread()
                │   ├─ asSetAllocScriptObjectFunction()
                │   ├─ LoadSettings()
                │   └─ asCreateScriptEngine()
                │
                ├─ Initialize_AnyThread()                     [可配置线程]
                │   ├─ SetEngineProperty() x 25+
                │   ├─ DiscoverScriptRoots()
                │   │
                │   ├─ ★ CallBinds() ★                       [核心瓶颈]
                │   │   ├─ GetSortedBindArray()               [排序所有 FBind]
                │   │   │
                │   │   ├─ [EOrder::Early = -100]
                │   │   │   └─ 基础值类型注册 (FVector, FRotator...)
                │   │   │
                │   │   ├─ [EOrder::Normal = 0]
                │   │   │   └─ 手写 Bind_*.cpp (123 个文件)
                │   │   │       └─ RegisterObjectType / RegisterObjectMethod
                │   │   │
                │   │   ├─ [EOrder::Late = 100]
                │   │   │   └─ 手写后期绑定
                │   │   │
                │   │   └─ [EOrder::Late + 50 = 150]
                │   │       └─ UHT 生成的 AS_FunctionTable_*
                │   │           └─ AddFunctionEntry() x ~9,000+
                │   │               └─ ClassFuncMaps[UClass][Name] = FFuncEntry
                │   │
                │   ├─ BindScriptTypes()                      [消费 ClassFuncMaps]
                │   │   └─ 遍历所有 UClass.BlueprintCallable
                │   │       ├─ ClassFuncMaps 有直接绑定 → RegisterObjectMethod(直接指针)
                │   │       └─ ClassFuncMaps 无绑定 → RegisterObjectMethod(反射回退)
                │   │
                │   └─ InitialCompile()                       [脚本编译]
                │
                └─ PostInitialize_GameThread()
                    └─ Broadcast OnInitialCompileFinished()


[运行时调用]
                                                
Angelscript 脚本调用 UE 函数
    └─ asIScriptContext::Execute()
        └─ 已注册的系统函数
            ├─ 直接绑定 → FGenericFuncPtr → 原生 C++ 函数
            └─ 反射回退 → ProcessEvent → FProperty 编组 → 原生 C++ 函数
```

---

## 11. 关键数据格式

### 11.1 生成的文件格式

**AS_FunctionTable_Summary.json:**
```json
{
  "totalGeneratedEntries": 9017,
  "totalDirectBindEntries": 6200,
  "totalStubEntries": 2817,
  "directBindRate": 0.6876,
  "stubRate": 0.3124,
  "totalShardCount": 42,
  "moduleCount": 15,
  "modules": [
    {
      "moduleName": "Engine",
      "editorOnly": false,
      "totalEntries": 4096,
      "directBindEntries": 3500,
      "stubEntries": 596,
      "directBindRate": 0.8545,
      "stubRate": 0.1455,
      "shardCount": 16
    }
  ]
}
```

**AS_FunctionTable_Entries.csv:**
```
ModuleName,EditorOnly,ClassName,FunctionName,EntryKind,EraseMacro,ShardIndex
Engine,false,AActor,GetActorLocation,Direct,"ERASE_METHOD_PTR(AActor, GetActorLocation, () const, ERASE_ARGUMENT_PACK(FVector))",1
Engine,false,AActor,K2_DestroyActor,Stub,ERASE_NO_FUNCTION(),1
```

**AS_FunctionTable_SkippedReasonSummary.csv:**
```
FailureReason,SkippedCount
overloaded-unresolved,245
non-public,123
unexported-symbol,87
static-array-parameter,12
```

### 11.2 运行时数据结构

```cpp
// 内存布局概要

ClassFuncMaps: TMap<UClass*, TMap<FString, FFuncEntry>>
├─ Key: UClass* (8 bytes)
├─ Value: TMap<FString, FFuncEntry>
│   ├─ Key: FString (函数名, ~32 bytes 含 SSO)
│   └─ Value: FFuncEntry
│       ├─ FGenericFuncPtr (26 bytes)
│       │   ├─ ptr union (25 bytes)
│       │   │   ├─ .f.func: FTypeErasedFuncPtr (8 bytes) + padding
│       │   │   ├─ .m.mthd: FTypeErasedMethodPtr (8-24 bytes) + padding
│       │   │   └─ .dummy[25]: raw storage
│       │   └─ flag: uint8 (1 byte)
│       │       ├─ 1 = generic
│       │       ├─ 2 = global function
│       │       └─ 3 = method
│       │
│       ├─ ASAutoCaller::FunctionCaller (~16 bytes)
│       │   ├─ union { MethodCallerPtr, FunctionCallerPtr }
│       │   └─ type: int (1=function, 2=method)
│       │
│       └─ bReflectiveFallbackBound: bool (1 byte)
│
│ 每条 FFuncEntry 约 ~44 bytes
│ 9,000 条 ≈ ~396 KB (不含 TMap 开销)

GetBindArray(): TArray<FBindFunction>
├─ FBindFunction
│   ├─ BindName: FName (8 bytes)
│   ├─ BindOrder: int32 (4 bytes)
│   └─ Function: TFunction<void()> (~32 bytes, 含堆分配的 lambda)
│
│ 数百个 FBind ≈ ~14 KB
```

---

## 12. 总结

| 维度 | UE5 原生 | Angelscript 当前方案 | 差异根因 |
|------|---------|---------------------|---------|
| 注册时机 | DLL 加载时 (懒加载) | 引擎初始化时 (全量) | AS 编译器需要完整类型信息 |
| 注册方式 | FRegisterCompiledInInfo (批量) | RegisterObjectMethod (逐条) | AS API 限制 |
| 线程安全 | 是 (原子操作/锁) | 否 (全局状态) | AS 引擎设计限制 |
| 调用开销 | ProcessEvent (反射) | 直接 ABI / 反射回退 | 直接绑定 = 零反射 |
| 启动成本 | ~0 (懒加载) | ~100-500ms+ (全量注册) | 架构差异 |
| 增量更新 | 按需构造 | 无 (每次全量) | 无缓存机制 |

**核心结论:** 启动时间开销的根本原因是 AngelScript 引擎要求在脚本编译前完成所有类型注册，且注册 API 非线程安全，导致必须在启动时串行执行数千次注册调用。最现实的优化路径是 **方向 A (桩条目延迟)** + **方向 D (批量注册)** 组合，短期内可获得显著收益且风险可控。
