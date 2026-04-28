# UEAS2 (Hazelight) 引擎 UHT 修改方案分析

> **分析日期**：2026-04-16
> **分析目标**：`J:\UnrealEngine\UEAS2` (Hazelight Angelscript 引擎分支)
> **对比基线**：当前 AngelscriptProject 的 `AngelscriptUHTTool` 插件方案
> **关联文档**：`UHT_FunctionTable_Registration_Analysis.md`、`Documents/Hazelight/ScriptClassImplementation.md`

---

## 1. 概述

UEAS2 (Hazelight) 采用 **直接修改 UE 引擎源码** 的方式，将 Angelscript 的函数指针信息嵌入到 UHT 标准代码生成流程中。核心思路是：在每个 `.gen.cpp` 中 `StaticRegisterNatives` 注册原生函数时，同时携带类型擦除的 Angelscript 函数指针，使其成为 UE 反射系统的"一等公民"。

而我们的方案是 **纯插件方式**：通过一个独立的 UHT Exporter 插件 (`AngelscriptUHTTool`) 额外生成 `AS_FunctionTable_*.cpp` 文件，在运行时启动阶段通过 `AddFunctionEntry()` 填充一张独立查找表 (`ClassFuncMaps`)。

---

## 2. UEAS2 修改的文件清单

### 2.1 UHT / 构建系统 (C#, 11 个文件)

| 文件 | 修改内容 |
|------|---------|
| `EpicGames.Core/UnrealEngineTypes.cs` | 新增 `EAngelscriptPropertyFlags` 枚举 |
| `EpicGames.UHT/Types/UhtProperty.cs` | 为 `UhtProperty` 添加 `AngelscriptPropertyFlags` 属性 (7 处) |
| `EpicGames.UHT/Parsers/UhtPropertyParser.cs` | 解析时检测 `const` / `&` 并设置对应 flag (4 处) |
| `EpicGames.UHT/Parsers/UhtFunctionParser.cs` | 检测 `WorldContext` 参数并设置 flag |
| `EpicGames.UHT/Types/Properties/UhtEnumProperty.cs` | `TEnumAsByte<>` 检测，设置 `CppEnumAsByte` flag |
| `EpicGames.UHT/Types/Properties/UhtStrProperty.cs` | `const FString&` 特殊处理 (2 处) |
| `EpicGames.UHT/Types/UhtFunction.cs` | `ScriptCallable` 元数据处理 |
| `EpicGames.UHT/Specifiers/UhtFunctionSpecifiers.cs` | 新增 `ScriptCallable` 函数修饰符 |
| `EpicGames.UHT/Specifiers/UhtPropertyMemberSpecifiers.cs` | 新增 `ScriptReadOnly` / `ScriptEditOnly` 属性修饰符 |
| `EpicGames.UHT/Utils/UhtPropertyResolveArgs.cs` | `ConstTemplateArg` 检测 |
| `EpicGames.UHT/Exporters/CodeGen/UhtHeaderCodeGeneratorCppFile.cs` | **核心**: `GetOriginalCppDeclaration()` + `GetASFunctionPointers()` + 嵌入 `.gen.cpp` |

### 2.2 运行时 / 引擎核心 (C++, 10+ 个文件)

| 文件 | 修改内容 |
|------|---------|
| `CoreUObject/Public/UObject/CoreNative.h` | 新增 `ERASE_*` 宏族 + `ASAutoCaller` 命名空间 (233 行) |
| `CoreUObject/Public/UObject/UObjectGlobals.h` | 新增 `ASAutoCaller::FReflectedFunctionPointers` 结构体 + 修改 `FClassNativeFunction` |
| `CoreUObject/Public/UObject/Script.h` | 新增 `FUNC_RuntimeGenerated = 0x10` 函数标志 |
| `CoreUObject/Public/UObject/Class.h` | `UClass` 添加 `ASReflectedFunctionPointers` / `ScriptTypePtr` / `bIsScriptClass` 字段; `UFunction` 添加虚方法; `UScriptStruct` 添加 `bIsScriptStruct` + FakeVTable 修改 |
| `CoreUObject/Public/UObject/UnrealType.h` | `FProperty` 添加 `AngelscriptPropertyFlags` (uint16) |
| `CoreUObject/Public/UObject/ObjectMacros.h` | 新增 `APF_*` C++ 属性标志枚举 |
| `CoreUObject/Public/UObject/EnumProperty.h` | 枚举属性扩展 |
| `CoreUObject/Private/UObject/Class.cpp` | `ASReflectedFunctionPointers` 填充逻辑 |
| `CoreUObject/Private/UObject/ScriptCore.cpp` | 6 处执行路径分流 (FUNC_RuntimeGenerated) |
| `CoreUObject/Private/UObject/UObjectGlobals.cpp` | 4 处对象初始化逻辑修改 |
| `Engine/Classes/GameFramework/Actor.h` | Actor 相关修改 |
| `Engine/Classes/Engine/Blueprint.h` | Blueprint 相关修改 |

---

## 3. UEAS2 核心架构：将函数指针嵌入 .gen.cpp

### 3.1 数据结构

```cpp
// UObjectGlobals.h - UEAS2 新增
// 作用: 类型擦除的函数指针对, 存储在每个 UClass 上

namespace ASAutoCaller
{
    using TFunctionPtr = void(*)();
    using FunctionCallerPtr = void(*)(TFunctionPtr FunctionPtr,
                                      void** Parameters, void* ReturnValue);

    // 核心结构: 一个函数指针 + 一个泛型调用器
    struct FReflectedFunctionPointers
    {
        void* FunctionPointerOrRetriever;  // 全局函数: 直接指针
                                            // 成员函数: 指向 MethodPointerRetriever 的指针
        FunctionCallerPtr CallerPtr;        // 编译时生成的参数编组调用器
    };
}
```

```cpp
// UObjectGlobals.h - UEAS2 修改了 UE 原生结构
// 作用: 在 StaticRegisterNatives 的每个函数条目中嵌入 AS 函数指针

namespace UE::CodeGen
{
    struct FClassNativeFunction
    {
        const UTF8CHAR* NameUTF8;                          // 原有: 函数名
        void (*Pointer)(UObject*, FFrame&, RESULT_DECL);   // 原有: exec thunk 指针

        // ★ UEAS2 新增: Angelscript 类型擦除函数指针
        ASAutoCaller::FReflectedFunctionPointers ASFunctionPointers;
    };
}
```

```cpp
// Class.h - UEAS2 直接在 UClass 基类上添加字段
// 影响: 所有 UClass 实例 (包括与 Angelscript 无关的原生类) 都多了这些字段

class UClass : public UStruct
{
    // ... 原有字段 ...

    // ★ UEAS2 新增:
    TMap<FName, ASAutoCaller::FReflectedFunctionPointers> ASReflectedFunctionPointers;
    void* ScriptTypePtr = nullptr;
    bool bIsScriptClass = false;
};
```

### 3.2 ERASE 宏 (引擎内置版)

```cpp
// CoreNative.h - UEAS2 版本
// 作用: 在 .gen.cpp 中使用, 编译时生成类型擦除函数指针对
// 与我们的版本的关键区别: 使用 **模板非类型参数** 做编译时展开

// 空指针 (无法绑定的函数)
#define ERASE_NO_FUNCTION() \
    ASAutoCaller::FReflectedFunctionPointers{nullptr, nullptr}

// 成员函数: 使用模板非类型参数, 编译时完全展开
#define ERASE_METHOD_PTR(c, m, p, r) \
    ASAutoCaller::GetReflectedFunctionPointers<                    \
        decltype(static_cast<r(c::*)p>(&c::m)),  /* 类型 */       \
        static_cast<r(c::*)p>(&c::m)             /* 值(非类型模板参数) */ \
    >(static_cast<r(c::*)p>(&c::m))

// 全局/静态函数
#define ERASE_FUNCTION_PTR(f, p, r) \
    ASAutoCaller::GetReflectedFunctionPointers<                    \
        decltype(static_cast<r(*)p>(f)),                           \
        static_cast<r(*)p>(f)                                      \
    >(static_cast<r(*)p>(f))
```

### 3.3 编译时调用器生成

```cpp
// CoreNative.h - ASAutoCaller 命名空间
// 作用: 通过模板特化为每个函数生成专用的参数编组 thunk

// 全局函数: 编译时直接内联展开
template<typename FunctionPointerType, FunctionPointerType FunctionPointer,
         typename ReturnType, typename... ParamTypes>
constexpr FReflectedFunctionPointers GetReflectedFunctionPointers(
    ReturnType(*FunctionPtr)(ParamTypes...))
{
    return FReflectedFunctionPointers{
        (void*)FunctionPointer,                        // 直接存储函数指针
        &RedirectFunctionCallerThunk<                  // 编译时生成的 thunk
            FunctionPointerType, FunctionPointer,
            ReturnType, ParamTypes...>
    };
}

// 成员函数: 通过 MethodPointerRetriever 间接获取 (因为成员指针不能直接转 void*)
template<typename FunctionPointerType, FunctionPointerType FunctionPointer,
         typename ReturnType, typename ObjectType, typename... ParamTypes>
constexpr FReflectedFunctionPointers GetReflectedFunctionPointers(
    ReturnType(ObjectType::*FunctionPtr)(ParamTypes...))
{
    return FReflectedFunctionPointers{
        (void*)&MethodPointerRetriever<FunctionPointerType, FunctionPointer>,
        &RedirectMethodCallerThunk<
            FunctionPointerType, FunctionPointer,
            ReturnType, ObjectType, ParamTypes...>
    };
}

// MethodPointerRetriever: 运行时将成员指针拷贝到指定地址
template<typename FunctionPointerType, FunctionPointerType FunctionPointer>
void MethodPointerRetriever(void* DestPtr)
{
    FunctionPointerType SourcePtr = FunctionPointer;
    memcpy(DestPtr, &SourcePtr, sizeof(FunctionPointerType));
}
```

### 3.4 UHT 代码生成 (嵌入 .gen.cpp)

```csharp
// UhtHeaderCodeGeneratorCppFile.cs (2538-2689 行)
// 作用: 在 .gen.cpp 的 StaticRegisterNatives 中嵌入 ASFunctionPointers

// 第一步: 从 UHT 元数据 + AngelscriptPropertyFlags 恢复原始 C++ 声明
private static string GetOriginalCppDeclaration(UhtProperty? Property)
{
    if (Property == null || Property.IsStaticArray || Property is UhtInterfaceProperty)
        return "";

    bool bIsRef = Property.AngelscriptPropertyFlags
                    .HasFlag(EAngelscriptPropertyFlags.CppRef);
    bool bIsConst = Property.AngelscriptPropertyFlags
                    .HasFlag(EAngelscriptPropertyFlags.CppConst);

    StringBuilder builder = new();
    if (bIsConst)
        builder.Append("const ");

    // 处理 TEnumAsByte 包装
    if (Property is UhtByteProperty ByteProp && ByteProp.Enum != null
        && ByteProp.AngelscriptPropertyFlags
            .HasFlag(EAngelscriptPropertyFlags.CppEnumAsByte))
    {
        builder.Append("TEnumAsByte<").Append(ByteProp.Enum.CppType).Append(">");
    }
    else
    {
        Property.AppendText(builder, UhtPropertyTextType.GenericFunctionArgOrRetVal);
    }

    if (Property.MetaData.ContainsKey("PointerConst"))
        builder.Append(" const");
    if (bIsRef)
        builder.Append("&");

    return builder.ToString();
}

// 第二步: 组装 ERASE_METHOD_PTR / ERASE_FUNCTION_PTR 宏调用
private static string GetASFunctionPointers(UhtClass classObj, UhtFunction function)
{
    // ... 收集返回类型、参数类型、const 修饰 ...
    // 对于静态方法:
    //   ERASE_FUNCTION_PTR(&ClassName::FuncName, (params), ERASE_ARGUMENT_PACK(RetType))
    // 对于实例方法:
    //   ERASE_METHOD_PTR(ClassName, FuncName, (params)const, ERASE_ARGUMENT_PACK(RetType))
    // 失败时:
    //   ERASE_NO_FUNCTION()
}
```

```csharp
// 第三步: 嵌入到 .gen.cpp 的 StaticRegisterNatives 函数表中
// (UhtHeaderCodeGeneratorCppFile.cs 2788-2791 行)

// 原始 UE 生成的代码:
//   { .NameUTF8 = "FuncName", .Pointer = &AMyClass::execFuncName },
//
// UEAS2 修改后:
//   { .NameUTF8 = "FuncName", .Pointer = &AMyClass::execFuncName,
//     .ASFunctionPointers = ERASE_METHOD_PTR(AMyClass, FuncImpl, (params)const,
//                           ERASE_ARGUMENT_PACK(RetType)) },

builder
    .Append($"::exec{function.EngineName}")
    .Append($", .ASFunctionPointers = {GetASFunctionPointers(classObj, function)} }},\r\n");
```

**生成的 .gen.cpp 示例:**

```cpp
// AActor.gen.cpp (UEAS2 版本)
// 注意: 这是标准 UHT 生成文件, 不是额外的插件文件

static constexpr UE::CodeGen::FClassNativeFunction Funcs[] = {
    { .NameUTF8 = UTF8TEXT("GetActorLocation"),
      .Pointer = &AActor::execGetActorLocation,
      .ASFunctionPointers = ERASE_METHOD_PTR(AActor, GetActorLocation,
                            ()const, ERASE_ARGUMENT_PACK(FVector)) },

    { .NameUTF8 = UTF8TEXT("SetActorLocation"),
      .Pointer = &AActor::execSetActorLocation,
      .ASFunctionPointers = ERASE_METHOD_PTR(AActor, SetActorLocation,
                            (const FVector&), ERASE_ARGUMENT_PACK(bool)) },

    { .NameUTF8 = UTF8TEXT("K2_DestroyActor"),
      .Pointer = &AActor::execK2_DestroyActor,
      .ASFunctionPointers = ERASE_NO_FUNCTION() },
    // ...
};
```

### 3.5 运行时: 函数指针自动填充 UClass

```cpp
// Class.cpp - UEAS2 修改 RegisterNatives 流程
// 作用: 当模块加载时, StaticRegisterNatives 自动将 ASFunctionPointers
//       从 FClassNativeFunction 表复制到 UClass::ASReflectedFunctionPointers

// UE 原生流程:
//   StaticRegisterNatives → RegisterFunctions() → 填充 FuncMap (FName → FNativeFuncPtr)
//
// UEAS2 追加:
//   同时将 ASFunctionPointers 填充到 UClass::ASReflectedFunctionPointers TMap

void FNativeFunctionRegistrar::RegisterFunctions(
    UClass* Class, TConstArrayView<FClassNativeFunction> InFunctions)
{
    for (const auto& Func : InFunctions)
    {
        // 原有: 注册原生函数指针
        RegisterFunction(Class, Func.NameUTF8, Func.Pointer);

        // ★ UEAS2 新增: 注册 AS 函数指针
        if (Func.ASFunctionPointers.CallerPtr != nullptr)
        {
            Class->ASReflectedFunctionPointers.Add(
                FName(Func.NameUTF8),
                Func.ASFunctionPointers);
        }
    }
}
```

### 3.6 EAngelscriptPropertyFlags 管线

```csharp
// EpicGames.Core/UnrealEngineTypes.cs
// 作用: UHT 解析阶段标记每个属性的 C++ 原始类型特征

public enum EAngelscriptPropertyFlags : ushort
{
    None             = 0,
    CppConst         = 0x0001,  // 原始声明含 const 限定
    CppRef           = 0x0002,  // 原始声明含 & 引用
    CppEnumAsByte    = 0x0004,  // 使用 TEnumAsByte<> 包装
    RuntimeGenerated = 0x0008,  // 运行时创建的属性
    WorldContext     = 0x0010,  // WorldContext 参数
    ConstTemplateArg = 0x0020,  // const 模板参数
}
```

```
C++ 头文件声明
    |
    v
UhtPropertyParser 解析
    |--- 遇到 const → 设置 CppConst
    |--- 遇到 & → 设置 CppRef
    |--- const FString& → 设置 CppConst | CppRef (UhtStrProperty 特殊处理)
    |--- TEnumAsByte<> → 设置 CppEnumAsByte (UhtEnumProperty)
    |--- WorldContext 元数据 → 设置 WorldContext (UhtFunctionParser)
    |--- const 模板参数 → 设置 ConstTemplateArg (UhtPropertyResolveArgs)
    |
    v
UhtProperty.AngelscriptPropertyFlags 存储
    |
    v
GetOriginalCppDeclaration() 消费
    |--- CppConst → 生成 "const " 前缀
    |--- CppRef → 生成 "&" 后缀
    |--- CppEnumAsByte → 生成 "TEnumAsByte<EnumType>"
    |
    v
ERASE_METHOD_PTR(Class, Func, (const FString&), ...) 生成到 .gen.cpp
    |
    v
编译时: 编译器根据精确签名实例化模板, 生成正确的调用 thunk
    |
    v
运行时: UClass::ASReflectedFunctionPointers 按 FName 查找 → 直接调用
```

```
运行时 FProperty 也存储此标志:

// UnrealType.h - FProperty 扩展
struct FProperty
{
    // ... 原有字段 ...
    uint16 AngelscriptPropertyFlags;  // ★ UEAS2 新增: 运行时可读
};

// .gen.cpp 中 property 创建时写入:
//   builder.Append("0x").AppendFormat("{0:x4}", (ushort)AngelscriptPropertyFlags);
```

---

## 4. 两种方案对比

### 4.1 架构差异

```
UEAS2 方案: 嵌入引擎 .gen.cpp                    我们的方案: 独立 UHT Exporter 插件
                                                
[UHT 阶段]                                       [UHT 阶段]
UhtPropertyParser                                AngelscriptFunctionTableExporter
    → 设置 AngelscriptPropertyFlags                  → 独立的 [UhtExporter] 入口
UhtHeaderCodeGeneratorCppFile                    AngelscriptFunctionTableCodeGenerator
    → GetASFunctionPointers()                        → CollectEntries()
    → 嵌入 FClassNativeFunction 表                    → AngelscriptFunctionSignatureBuilder
                                                      → AngelscriptHeaderSignatureResolver
[生成文件]                                        [生成文件]
AActor.gen.cpp (修改后的标准文件)                  AS_FunctionTable_Engine_000.cpp (额外文件)
    ↓ FClassNativeFunction.ASFunctionPointers        ↓ AS_FORCE_LINK FBind lambda
                                                
[运行时]                                          [运行时]
DLL 加载 → StaticRegisterNatives                  DLL 加载 → FBind 静态构造
    → RegisterFunctions()                             → RegisterBinds()
    → UClass::ASReflectedFunctionPointers 填充        → GetBindArray().Add()
    ★ 零额外启动代码                               CallBinds() → 排序 + 逐条执行
                                                     → AddFunctionEntry() × N
                                                     → ClassFuncMaps 填充
                                                     ★ 额外启动时间
```

### 4.2 签名恢复策略对比

```
UEAS2: 在 UHT 解析阶段收集信息 (精确)            我们: 从头文件反向解析 (尽力而为)
                                                
UhtPropertyParser                                AngelscriptHeaderSignatureResolver
  → 解析 C++ token 时直接记录                       → 读取头文件源码
  → 遇到 const → 标记 CppConst                     → 去注释 + 缓存
  → 遇到 & → 标记 CppRef                           → 正则匹配函数声明
  → 精确捕获编译器看到的签名                          → 多候选过滤 + 类型匹配
                                                    → 公开性检查
优势:                                             优势:
  ✅ 100% 精确 (UHT 就是编译器前端)                  ✅ 不修改引擎
  ✅ 无头文件解析开销                                ✅ 独立可维护
  ✅ 处理所有 UHT 已知的类型                         
                                                  劣势:
劣势:                                               ❌ 头文件解析不完美 (~67% 成功率)
  ❌ 必须修改引擎 UHT 代码                           ❌ 重载歧义无法解决
  ❌ 每次 UE 升级需要重新合并                         ❌ private/unexported 函数跳过
```

### 4.3 运行时性能差异

```
====== UEAS2: 启动时零额外成本 ======

UE 模块加载 (DLL Load)
    └─ StaticRegisterNatives()           ← UE 原生流程, 必须执行
        └─ RegisterFunctions()
            └─ 遍历 FClassNativeFunction[]
                ├─ FuncMap.Add(Name, Pointer)        ← UE 原有
                └─ ASReflectedFunctionPointers.Add() ← UEAS2 追加
                    (仅 TMap::Add, 极轻量)
                                                
总额外成本: 每个 UClass 一次 TMap::Add 批量操作
            随 DLL 加载自然执行, 无独立启动阶段


====== 我们: 需要独立的启动阶段 ======

UE 模块加载 (DLL Load)
    └─ FBind 静态构造函数
        └─ RegisterBinds() → GetBindArray().Add()
            (仅注册 lambda, 不执行)

FAngelscriptEngine::Initialize()
    └─ CallBinds()                       ← ★ 独立启动阶段
        └─ GetSortedBindArray()          ← O(n log n) 排序
        └─ 逐条执行 Bind lambda
            └─ AddFunctionEntry() × ~9,000
                ├─ UClass::StaticClass()  ← 每次查找 UClass
                ├─ FString 构造           ← 函数名
                └─ TMap 双重查找 + 插入
                                                
总额外成本: ~100-500ms (取决于函数数量和机器性能)
```

### 4.4 对比总表

| 维度 | UEAS2 (引擎内嵌) | 我们 (UHT Exporter 插件) |
|------|------------------|------------------------|
| **引擎修改** | 22+ C++ + 11 C# 文件 | **零** |
| **签名恢复精度** | ~100% (UHT 原生数据) | ~67% (头文件解析) |
| **函数指针存储位置** | `UClass::ASReflectedFunctionPointers` | `FAngelscriptBindState::ClassFuncMaps` |
| **注册时机** | DLL 加载时 (StaticRegisterNatives 顺带) | 引擎初始化时 (CallBinds 独立阶段) |
| **启动额外成本** | ~0 (随 UE 原生注册免费执行) | ~100-500ms (独立 CallBinds 阶段) |
| **内存影响** | 所有 UClass 增大 (~40 bytes TMap 基础开销) | 仅插件内部 ClassFuncMaps |
| **UE 升级成本** | 每次大版本需重新合并 | 无 |
| **ABI 兼容性** | 破坏 (所有 .gen.cpp 重新生成) | 完全兼容 vanilla UE5 |
| **可分发形式** | 必须随引擎源码分发 | 独立插件 |
| **新增 UFunction 标志** | `FUNC_RuntimeGenerated (0x10)` | 无 (复用 `FUNC_Native`) |
| **FProperty 扩展** | `uint16 AngelscriptPropertyFlags` | 无 |

---

## 5. UEAS2 的性能优势根因分析

### 5.1 核心优势: 搭便车

```
UEAS2 的函数指针注册搭了 UE StaticRegisterNatives 的便车:

UE 原生必须执行:
    StaticRegisterNatives → RegisterFunctions → FuncMap.Add(Name, Pointer)
                                                
UEAS2 仅在此基础上追加一行:
    ASReflectedFunctionPointers.Add(Name, FReflectedFunctionPointers)

这意味着:
  1. 没有独立的启动阶段 → 零额外启动时间
  2. 没有排序操作 → 无 O(n log n) 开销  
  3. 没有 UClass::StaticClass() 查找 → 已在 RegisterFunctions 上下文中
  4. 没有 FString 构造 → 直接用 FName (UE 已提供)
  5. 没有双重 TMap 查找 → 直接 Add 到当前 UClass
```

### 5.2 签名信息也是搭便车获得

```
UEAS2 的 AngelscriptPropertyFlags 在 UHT 解析阶段就已经收集:

UHT 原生必须执行:
    解析 C++ 头文件 → 构建 UhtProperty → 生成 .gen.cpp
                                                
UEAS2 仅在解析过程中追加标记:
    遇到 const → AngelscriptPropertyFlags |= CppConst    (1 行代码)
    遇到 &     → AngelscriptPropertyFlags |= CppRef      (1 行代码)

而我们必须:
    重新读取头文件 → 去注释 → 正则匹配 → 多候选过滤 → 类型比对
    (AngelscriptHeaderSignatureResolver: 773 行代码)
```

---

## 6. 完整调用链对比图

```
====== UEAS2 方案 ======

[UHT 编译时]

C++ 头文件
    └─ UhtPropertyParser.cs
        ├─ 原有: 解析属性类型、标志
        └─ ★ 追加: 设置 AngelscriptPropertyFlags (CppConst/CppRef/...)
            └─ UhtHeaderCodeGeneratorCppFile.cs
                ├─ GetOriginalCppDeclaration(Property)     [从 flags 恢复声明]
                └─ GetASFunctionPointers(classObj, function) [生成 ERASE_* 宏]
                    └─ 写入 .gen.cpp:
                        FClassNativeFunction { Name, Pointer, ASFunctionPointers }


[C++ 编译时]

AActor.gen.cpp 编译
    └─ ERASE_METHOD_PTR(AActor, GetActorLocation, ()const, ...)
        └─ 编译器实例化 GetReflectedFunctionPointers<>
            ├─ FunctionPointerOrRetriever = &MethodPointerRetriever<...>
            └─ CallerPtr = &RedirectMethodCallerThunk<...>
        └─ 结果: constexpr FReflectedFunctionPointers 嵌入 FClassNativeFunction[]


[运行时启动]

UE 模块加载器
    └─ StaticRegisterNatives_AActor()
        └─ RegisterFunctions(AActor::StaticClass(), Funcs)
            └─ 遍历 Funcs[]
                ├─ RegisterFunction(Name, Pointer)          [UE 原有]
                └─ ★ AActor::ASReflectedFunctionPointers    [UEAS2 追加]
                       .Add(FName("GetActorLocation"),
                            FReflectedFunctionPointers{...})
                                                
    ★ 注册完成! 无需任何额外启动阶段


[运行时使用]

Angelscript 插件绑定 BlueprintCallable 函数
    └─ 遍历 UClass 的 UFunction
        └─ 查询 Class->ASReflectedFunctionPointers[FuncName]
            ├─ 有 → 直接使用 CallerPtr 作为调用器
            └─ 无 → 走反射回退 (ProcessEvent)


====== 我们的方案 ======

[UHT 编译时]

C++ 头文件
    └─ AngelscriptFunctionTableExporter (独立 UHT 插件)
        └─ AngelscriptFunctionTableCodeGenerator.Generate()
            └─ CollectEntries() → 遍历 UHT 类型树
                └─ AngelscriptFunctionSignatureBuilder.TryBuild()
                    ├─ 阶段1: AngelscriptHeaderSignatureResolver
                    │          (773 行, 重新读取和解析头文件)
                    ├─ 阶段2: 不可恢复失败检查
                    └─ 阶段3: UHT 元数据回退
            └─ BuildShard() → AS_FunctionTable_Engine_000.cpp


[C++ 编译时]

AS_FunctionTable_Engine_000.cpp 编译 (额外文件)
    └─ AS_FORCE_LINK FBind 静态构造
        └─ RegisterBinds(EOrder::Late + 50, lambda)


[运行时启动]

UE 模块加载器
    └─ (原有 StaticRegisterNatives 照常执行, 不涉及 AS)

FAngelscriptEngine::Initialize()
    └─ ★ CallBinds()                      [额外启动阶段]
        └─ GetSortedBindArray()            [排序]
        └─ 手写 Bind_*.cpp 执行            [EOrder::Normal/Late]
        └─ UHT AS_FunctionTable_* 执行     [EOrder::Late + 50]
            └─ AddFunctionEntry() × ~9,000
                └─ ClassFuncMaps[UClass][Name] = FFuncEntry

    └─ ★ BindScriptTypes()                [消费 ClassFuncMaps]
        └─ 遍历所有 UClass.BlueprintCallable
            └─ 查询 ClassFuncMaps → RegisterObjectMethod


[运行时使用]

Angelscript VM 调用 UE 函数
    └─ 已在 BindScriptTypes() 注册的 AS 系统函数
        ├─ 直接绑定 → FGenericFuncPtr → 原生 C++ 函数
        └─ 反射回退 → ProcessEvent
```

---

## 7. 能否借鉴 UEAS2 思路优化我们的方案

### 7.1 不可直接移植的部分

```
以下 UEAS2 特性需要引擎修改, 不可移植:

1. FClassNativeFunction 结构体扩展
   → 需要修改 UObjectGlobals.h
   → 破坏 ABI

2. UClass 添加 ASReflectedFunctionPointers 字段
   → 需要修改 Class.h
   → 影响所有 UClass 实例

3. AngelscriptPropertyFlags 在 UHT 解析时收集
   → 需要修改 UhtPropertyParser.cs / UhtFunctionParser.cs
   → 是引擎 UHT 代码

4. FUNC_RuntimeGenerated 函数标志
   → 需要修改 Script.h
   → 影响 ScriptCore.cpp 执行路径
```

### 7.2 可借鉴的优化思路

```
思路 A: 模拟 "搭便车" 效果

当前问题: CallBinds() 在引擎初始化时执行 ~9,000 次 AddFunctionEntry()
UEAS2 优势: 函数指针随 DLL 加载自动填充 UClass

可能的优化:
  → 将 AS_FunctionTable 从 lambda 改为编译时 constexpr 数组
  → 利用 C++ 全局对象初始化 (DLL load time) 填充一个全局表
  → CallBinds() 时直接批量 memcpy/swap, 而非逐条 TMap::Add
  → 预估收益: 减少 50%+ AddFunctionEntry 时间


思路 B: 预计算 FName 哈希

当前问题: AddFunctionEntry 每次构造 FString → FName 转换 → TMap 查找
UEAS2 优势: 直接用 FName (UE RegisterFunctions 已提供)

可能的优化:
  → UHT 生成时预计算 FName 哈希值
  → 或在编译时生成 FName 常量字面量
  → 避免运行时字符串哈希开销
  → 预估收益: 减少 ~20% AddFunctionEntry 时间


思路 C: 生成静态数组替代 lambda

当前:
    AS_FORCE_LINK FBind Bind_XXX(150, []() {
        AddFunctionEntry(AActor::StaticClass(), "GetActorLocation", { ERASE_... });
        AddFunctionEntry(AActor::StaticClass(), "SetActorLocation", { ERASE_... });
    });

优化后:
    // 编译时静态数组
    static const struct { UClass*(*GetClass)(); const char* Name; FFuncEntry Entry; }
    FunctionTable_Engine[] = {
        { &AActor::StaticClass, "GetActorLocation", { ERASE_METHOD_PTR(...) } },
        { &AActor::StaticClass, "SetActorLocation", { ERASE_METHOD_PTR(...) } },
    };

    // 启动时批量注册
    for (const auto& Item : FunctionTable_Engine)
        ClassFuncMaps.FindOrAdd(Item.GetClass()).Add(Item.Name, Item.Entry);

优势: 减少 lambda 间接调用, 更 cache-friendly 的连续内存访问


思路 D: 利用 AngelscriptPropertyFlags 提升签名恢复率

UEAS2 在 UHT 阶段收集 const/ref 信息是 100% 精确的
我们的 HeaderSignatureResolver 只有 ~67% 成功率

可能的改进:
  → 在 UHT Exporter 中访问 FProperty 的原始 token 信息
  → UHT 的 UhtProperty 类已有 PropertyFlags 和类型信息
  → 更深入利用 UHT 提供的 API, 减少头文件解析依赖
  → 预估收益: 签名恢复率从 67% → 85%+, 减少反射回退
```

### 7.3 优化优先级

```
优化方向                            收益     风险    复杂度   推荐
                                                        
C: 静态数组替代 lambda               中       低      低       ★★★★★
B: 预计算 FName 哈希                 低-中    低      低       ★★★★☆
A: 批量 memcpy 替代逐条插入          中       中      中       ★★★☆☆
D: 提升签名恢复率                    中       低      中       ★★★☆☆
```

---

## 8. 总结

| | UEAS2 (Hazelight) | 我们 (AngelscriptProject) |
|---|---|---|
| **设计哲学** | 引擎即平台，深度集成 | 插件即产品，零侵入 |
| **UHT 修改** | 修改 11 个 C# 文件，嵌入 .gen.cpp | 独立 Exporter 插件，额外 .cpp 文件 |
| **签名恢复** | UHT 解析时原生收集 (100%) | 头文件反向解析 (~67%) |
| **启动性能** | 零额外成本 (搭 StaticRegisterNatives 便车) | ~100-500ms (独立 CallBinds 阶段) |
| **维护成本** | UE 升级需重新合并 | 无 |
| **分发方式** | 引擎分支 | 独立插件 |

**核心结论**: UEAS2 的启动性能优势来自于 "搭便车" —— 将 AS 函数指针嵌入 UE 本就必须执行的 StaticRegisterNatives 流程中，实现了零额外启动成本。这一优势是引擎侵入性的直接回报。

我们作为纯插件方案，虽然无法直接复制这一架构，但可以通过 **静态数组替代 lambda**、**预计算 FName**、**批量注册** 等手段，将启动时间从当前的 100-500ms 显著缩减，在保持零引擎修改的前提下尽可能逼近 UEAS2 的性能水平。

---

## 9. AngelscriptProject 可行优化方案

> 以下所有方案均基于 **零引擎修改** 前提，按 **收益/风险/实现复杂度** 分级。
> 当前实测数据: 6,042 条 UHT 生成条目, 32 个分片, 14 个模块, 直接绑定率 56.57%。

### 9.1 方案总览与优先级矩阵

```
ID   方案名称                          收益    风险   复杂度  阶段     推荐
                                                                    
P0   预分配 ClassFuncMaps 容量          低-中   极低   极低    立即     ★★★★★
P1   FString → FName 键替换            中-高   低     低-中   短期     ★★★★★
P2   消除 AddFunctionEntry 双重查找    中      极低   极低    立即     ★★★★★
P3   桩条目 (Stub) 跳过注册            中      低     低      短期     ★★★★☆
P4   静态数组替代 lambda               中      低     中      短期     ★★★★☆
P5   按 UClass 分组批量注册            中-高   低     中      短期     ★★★★☆
P6   合并 Delegate 双重遍历            低-中   极低   极低    立即     ★★★★☆
P7   排序一次 + 缓存                   低      极低   极低    立即     ★★★☆☆
P8   扁平数组替代嵌套 TMap             高      中     高      中期     ★★★☆☆
P9   按需延迟注册 (低频模块)           中      中     中      中期     ★★☆☆☆
P10  并行分片注册                      高      高     高      长期     ★★☆☆☆
```

---

### 9.2 P0: 预分配 ClassFuncMaps 容量

**问题分析:**

```cpp
// AngelscriptBinds.h - 当前实现
// ClassFuncMaps 是嵌套 TMap, 默认初始容量很小
// 插入 6,042 条目时, 内部多次 rehash (重新哈希 + 搬迁全部数据)
TMap<UClass*, TMap<FString, FFuncEntry>> ClassFuncMaps;

// TMap rehash 触发时机: 当负载因子超过阈值 (通常 ~0.7)
// 从空开始插入 6,042 条目, 预计触发 ~12 次 rehash
// 每次 rehash: O(n) 全量搬迁 + 内存分配
```

**修改方案:**

```cpp
// AngelscriptBinds.cpp - 在 CallBinds() 执行前预分配
// 位置: FAngelscriptEngine::Initialize() 中 CallBinds() 调用前

void FAngelscriptEngine::PreAllocateBindState()
{
    auto& ClassFuncMaps = FAngelscriptBinds::GetClassFuncMaps();
    
    // 外层 TMap: 预估 ~500 个 UClass (实际可能 300-600)
    ClassFuncMaps.Reserve(512);
    
    // 注意: 内层 TMap 无法统一预分配, 但外层预分配已消除主要 rehash
}
```

**收益:** 消除外层 TMap 的 ~12 次 rehash, 节约 10-20% 的插入时间
**风险:** 极低 (Reserve 只是预分配, 无逻辑变化)
**代码改动:** ~3 行

---

### 9.3 P1: FString → FName 键替换

**问题分析:**

```cpp
// 当前: AddFunctionEntry 使用 FString 作为内层 TMap 键
static void AddFunctionEntry(UClass* Class, FString Name, FFuncEntry Entry)
//                                          ^^^^^^^^
// FString 键的问题:
//   1. 哈希计算: O(字符串长度), 每个字符参与哈希
//   2. 比较: 哈希碰撞时需要逐字符比较
//   3. 内存: 每个 FString 独立堆分配 (~32 bytes + 字符串数据)
//   4. 构造: 从 const char* 字面量构造 FString 需要 UTF-8→TCHAR 转换 + 堆分配

// 生成代码中的调用:
FAngelscriptBinds::AddFunctionEntry(
    AActor::StaticClass(),
    "GetActorLocation",        // ← 这里构造了一个临时 FString
    { ERASE_METHOD_PTR(...) });

// 消费时也是 FString 查找:
// Bind_BlueprintCallable.cpp
auto* map = ClassFuncMaps.Find(OwningClass);
FFuncEntry* Entry = map->Find(Function->GetName());  // GetName() 返回 FString
```

**修改方案:**

```cpp
// 步骤 1: 将 FAngelscriptBindState 的 ClassFuncMaps 键改为 FName
struct FAngelscriptBindState
{
    // 修改前:
    // TMap<UClass*, TMap<FString, FFuncEntry>> ClassFuncMaps;
    
    // 修改后:
    TMap<UClass*, TMap<FName, FFuncEntry>> ClassFuncMaps;
    //                 ^^^^^
    // FName 优势:
    //   哈希: O(1), 预计算并缓存在全局 FName 表中
    //   比较: O(1), 直接比较 ComparisonIndex (int32)
    //   内存: 8 bytes 固定大小, 无堆分配
    //   构造: 从字面量构造走 FName 全局表缓存
};

// 步骤 2: 修改 AddFunctionEntry 签名
static void AddFunctionEntry(UClass* Class, FName Name, FFuncEntry Entry)
{
    auto& ClassFuncMaps = GetClassFuncMaps();
    auto& FuncMap = ClassFuncMaps.FindOrAdd(Class);
    FuncMap.FindOrAdd(Name, MoveTemp(Entry));
}

// 步骤 3: 修改 UHT 生成的代码模板 (AngelscriptFunctionTableCodeGenerator.cs)
// 修改前:
//   FAngelscriptBinds::AddFunctionEntry(AActor::StaticClass(), "GetActorLocation", ...);
// 修改后:
//   FAngelscriptBinds::AddFunctionEntry(AActor::StaticClass(),
//       FName(TEXT("GetActorLocation")), ...);

// 步骤 4: 修改消费端 (Bind_BlueprintCallable.cpp)
// 修改前:
//   FFuncEntry* Entry = map->Find(Function->GetName());
// 修改后:
//   FFuncEntry* Entry = map->Find(Function->GetFName());
//                                           ^^^^^^^^ 直接用 FName, 无转换
```

**收益分析:**

```
                        FString 键              FName 键
哈希计算 (每次查找)     O(n), n=字符串长度       O(1), 缓存的 int32
比较 (碰撞时)           O(n), 逐字符比较         O(1), int32 比较
内存 (每个键)           ~32+n bytes (堆分配)     8 bytes (栈内)
构造 (从字面量)         堆分配 + 拷贝            全局表查找 (通常缓存命中)

6,042 次插入 + 后续查找, 预计总节约: 30-50% 的键操作时间
```

**风险:** 低 (FName 是 UE 标准键类型, 广泛用于 TMap; 需确认所有消费点)
**代码改动:** ~20 行核心修改 + UHT 模板调整

---

### 9.4 P2: 消除 AddFunctionEntry 双重查找

**问题分析:**

```cpp
// AngelscriptBinds.h 497-512 行 - 当前实现有冗余查找
static void AddFunctionEntry(UClass* Class, FString Name, FFuncEntry Entry)
{
    auto& ClassFuncMaps = GetClassFuncMaps();
    if (ClassFuncMaps.Contains(Class))      // ← 第一次查找 (外层)
    {
        if (!ClassFuncMaps[Class].Contains(Name))  // ← 第二次查找 (外层) + 第一次查找 (内层)
        {
            ClassFuncMaps[Class].Add(Name, Entry);  // ← 第三次查找 (外层) + 插入 (内层)
        }
    }
    else
    {
        ClassFuncMaps.Add(Class, TMap<FString, FFuncEntry>()).Add(Name, Entry);
    }
}
// 最坏情况: 外层查找 3 次 + 内层查找 2 次 = 5 次哈希操作
```

**修改方案:**

```cpp
// 使用 FindOrAdd 消除冗余查找
static void AddFunctionEntry(UClass* Class, FName Name, FFuncEntry Entry)
{
    auto& ClassFuncMaps = GetClassFuncMaps();
    auto& FuncMap = ClassFuncMaps.FindOrAdd(Class);  // ← 1 次外层查找 (找到或创建)
    FuncMap.FindOrAdd(Name, MoveTemp(Entry));          // ← 1 次内层查找 (找到或创建)
}
// 总计: 外层 1 次 + 内层 1 次 = 2 次哈希操作
// 减少 60% 的哈希操作次数
```

**注意:** `FindOrAdd(Key, Value)` 语义是"如果不存在则插入 Value"，与原始的"首次写入优先"行为一致。若 UE 版本的 `FindOrAdd` 不接受第二参数，可改为:

```cpp
static void AddFunctionEntry(UClass* Class, FName Name, FFuncEntry Entry)
{
    auto& FuncMap = GetClassFuncMaps().FindOrAdd(Class);
    if (!FuncMap.Contains(Name))
    {
        FuncMap.Add(Name, MoveTemp(Entry));
    }
}
// 仍然是 外层 1 次 + 内层 2 次 = 3 次, 但比原来的 5 次好
```

**收益:** 减少 40-60% 的哈希操作次数
**风险:** 极低 (语义不变)
**代码改动:** ~5 行

---

### 9.5 P3: 桩条目 (Stub) 跳过注册

**问题分析:**

```
当前数据: 6,042 条 UHT 生成条目中, 2,624 条 (43.43%) 是 ERASE_NO_FUNCTION() 桩条目

桩条目的 FFuncEntry 内容:
    FGenericFuncPtr = {空}       → FuncPtr 无效
    FunctionCaller  = {空}       → Caller 无效
    bReflectiveFallbackBound = false

桩条目在运行时的效果:
    BindScriptTypes() 查询 ClassFuncMaps → 找到桩条目 → FuncPtr 无效
    → 仍然走反射回退 (ProcessEvent)
    → 与"未找到条目"的行为完全相同!

也就是说: 注册 2,624 个桩条目 = 纯粹浪费
    - 浪费 2,624 次 TMap 插入
    - 浪费 ~115 KB 内存 (2,624 × 44 bytes FFuncEntry)
    - 浪费哈希表空间, 降低查找性能 (更高负载因子)
```

**修改方案:**

```csharp
// AngelscriptFunctionTableCodeGenerator.cs - CollectEntries()
// 修改: 桩条目不生成 AddFunctionEntry 调用

// 修改前 (468-477 行):
else if (AngelscriptFunctionSignatureBuilder.TryBuild(...))
{
    eraseMacro = signature!.BuildEraseMacro();
}
else
{
    eraseMacro = "ERASE_NO_FUNCTION()";  // ← 桩条目仍然注册
}
entries.Add(new AngelscriptGeneratedFunctionEntry(..., eraseMacro));

// 修改后:
else if (AngelscriptFunctionSignatureBuilder.TryBuild(...))
{
    eraseMacro = signature!.BuildEraseMacro();
    entries.Add(new AngelscriptGeneratedFunctionEntry(..., eraseMacro));
}
// else: 不添加桩条目, 运行时自然走反射回退
```

**消费端无需修改:**

```cpp
// Bind_BlueprintCallable.cpp - 消费端已经处理"未找到"情况
auto* map = ClassFuncMaps.Find(OwningClass);
if (map)
{
    FFuncEntry* Entry = map->Find(FuncName);
    if (Entry && Entry->FuncPtr.IsBound())
    {
        // 直接绑定路径
    }
    else
    {
        // 反射回退路径 ← 桩条目和"未找到"都走这里
    }
}
else
{
    // 反射回退路径
}
```

**收益:**

```
减少条目:     2,624 / 6,042 = 43.4% 的 AddFunctionEntry 调用
减少内存:     ~115 KB (2,624 × 44 bytes)
减少哈希操作: 2,624 × 2~5 次 = 5,248~13,120 次哈希操作
预计时间节约: 30-40% 的 UHT 分片注册时间
```

**风险:** 低 (桩条目本来就不提供直接绑定; 需确认无代码依赖桩条目的存在性)
**代码改动:** UHT 生成器 ~5 行 C# 修改

---

### 9.6 P4: 静态数组替代 lambda

**问题分析:**

```cpp
// 当前: 每个分片是一个 lambda, 运行时逐条调用 AddFunctionEntry
AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_AS_FunctionTable_Engine_000(
    (int32)FAngelscriptBinds::EOrder::Late + 50, []()
{
    // 256 次独立的函数调用, 每次:
    //   1. 调用 StaticClass() (虚函数查找)
    //   2. 构造 FString ("GetActorLocation")
    //   3. 构造 FFuncEntry (ERASE_METHOD_PTR 宏展开)
    //   4. 调用 AddFunctionEntry (TMap 查找 + 插入)
    FAngelscriptBinds::AddFunctionEntry(
        AActor::StaticClass(), "GetActorLocation",
        { ERASE_METHOD_PTR(AActor, GetActorLocation, () const, ERASE_ARGUMENT_PACK(FVector)) });
    // × 256
});
```

**修改方案:**

```cpp
// 方案: 生成编译时 constexpr/static 数组, 启动时批量注册

// 生成的代码 (新格式):
struct FStaticFuncEntry
{
    UClass* (*GetClass)();     // 延迟求值 StaticClass (避免静态初始化顺序问题)
    FName FuncName;             // 编译时 FName (或 const char* 运行时转)
    FFuncEntry Entry;           // ERASE_* 宏展开的结果
};

static const FStaticFuncEntry GFunctionTable_Engine_000[] = {
    { &AActor::StaticClass, FName(TEXT("GetActorLocation")),
      { ERASE_METHOD_PTR(AActor, GetActorLocation, ()const, ERASE_ARGUMENT_PACK(FVector)) } },
    { &AActor::StaticClass, FName(TEXT("SetActorLocation")),
      { ERASE_METHOD_PTR(AActor, SetActorLocation, (const FVector&), ERASE_ARGUMENT_PACK(bool)) } },
    // ...
};

// 注册函数: 批量插入, 连续内存访问
AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_AS_FunctionTable_Engine_000(
    (int32)FAngelscriptBinds::EOrder::Late + 50, []()
{
    FAngelscriptBinds::AddFunctionEntries(GFunctionTable_Engine_000,
        UE_ARRAY_COUNT(GFunctionTable_Engine_000));
});

// 新增批量注册函数:
static void AddFunctionEntries(const FStaticFuncEntry* Entries, int32 Count)
{
    auto& ClassFuncMaps = GetClassFuncMaps();
    for (int32 i = 0; i < Count; ++i)
    {
        UClass* Class = Entries[i].GetClass();
        auto& FuncMap = ClassFuncMaps.FindOrAdd(Class);
        FuncMap.FindOrAdd(Entries[i].FuncName, Entries[i].Entry);
    }
}
```

**收益:**

```
1. Cache 友好: 静态数组在连续内存中, 预取器可有效工作
2. 减少函数调用: 从 256 次 AddFunctionEntry 调用 → 1 次 AddFunctionEntries
3. 减少 FString 构造: 如果用 FName 键, 完全消除 FString 堆分配
4. 编译优化: constexpr 数组允许编译器更好地优化
5. 与 P1 (FName 键) 和 P2 (消除双重查找) 叠加效果显著

预估: 减少 40-60% 的分片注册时间
```

**风险:** 低 (纯代码生成变更, 语义不变; 需注意 static 初始化顺序)
**代码改动:** UHT 生成器 BuildShard() 重写 (~50 行 C#) + 运行时 AddFunctionEntries (~15 行 C++)

---

### 9.7 P5: 按 UClass 分组批量注册

**问题分析:**

```
当前: 条目按 (ClassName, FunctionName) 字母序排列
      同一个 UClass 的条目是连续的 (排序保证)
      但每次 AddFunctionEntry 仍独立查找外层 TMap

例如 AActor 有 ~80 个函数:
    AddFunctionEntry(AActor::StaticClass(), "Func1", ...);  // 查找外层 TMap
    AddFunctionEntry(AActor::StaticClass(), "Func2", ...);  // 又查找外层 TMap
    AddFunctionEntry(AActor::StaticClass(), "Func3", ...);  // 又查找外层 TMap
    // × 80 次, 每次都重新查找 AActor 在外层 TMap 的位置
```

**修改方案:**

```cpp
// 生成按 UClass 分组的注册代码
AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_AS_FunctionTable_Engine_000(
    (int32)FAngelscriptBinds::EOrder::Late + 50, []()
{
    auto& ClassFuncMaps = FAngelscriptBinds::GetClassFuncMaps();

    // AActor 组: 80 个函数, 外层 TMap 只查找 1 次
    {
        auto& FuncMap = ClassFuncMaps.FindOrAdd(AActor::StaticClass());
        FuncMap.Reserve(80);  // 预分配内层容量
        FuncMap.Add(FName(TEXT("Func1")), { ERASE_METHOD_PTR(...) });
        FuncMap.Add(FName(TEXT("Func2")), { ERASE_METHOD_PTR(...) });
        // × 80, 但外层查找只有 1 次
    }

    // USceneComponent 组: 30 个函数
    {
        auto& FuncMap = ClassFuncMaps.FindOrAdd(USceneComponent::StaticClass());
        FuncMap.Reserve(30);
        FuncMap.Add(FName(TEXT("Func1")), { ERASE_METHOD_PTR(...) });
        // × 30
    }
});
```

**UHT 生成器修改:**

```csharp
// AngelscriptFunctionTableCodeGenerator.cs - BuildShard() 重写
// 当前: entries 已按 (ClassName, FunctionName) 排序
// 新增: 在生成代码时按 ClassName 分组

private static StringBuilder BuildShard(...)
{
    // ... 头部代码 ...

    // 按 ClassName 分组
    string currentClass = null;
    int classEntryCount = 0;

    for (int i = startIndex; i < startIndex + entryCount; i++)
    {
        var entry = entries[i];
        if (entry.ClassName != currentClass)
        {
            if (currentClass != null)
                builder.AppendLine("\t}");  // 关闭前一组

            currentClass = entry.ClassName;
            classEntryCount = entries.Skip(i).TakeWhile(e => e.ClassName == currentClass).Count();

            builder.AppendLine($"\t{{");
            builder.AppendLine($"\t\tauto& FuncMap = ClassFuncMaps.FindOrAdd({currentClass}::StaticClass());");
            builder.AppendLine($"\t\tFuncMap.Reserve({classEntryCount});");
        }
        // 生成内层 Add (不需要外层查找)
        builder.AppendLine($"\t\tFuncMap.Add(FName(TEXT(\"{entry.FunctionName}\")), {{ {entry.EraseMacro} }});");
    }
    if (currentClass != null)
        builder.AppendLine("\t}");

    // ... 尾部代码 ...
}
```

**收益:**

```
以 Engine 模块为例 (4,054 条目, ~300 个 UClass):
    修改前: 4,054 次外层 TMap 查找
    修改后: ~300 次外层 TMap 查找 + 300 次 Reserve
    减少:   ~3,754 次外层查找 = 92.6% 减少

叠加内层预分配:
    修改前: 内层 TMap 多次 rehash (每个 UClass 的函数 TMap)
    修改后: Reserve 后无 rehash

预估: 减少 50-70% 的 TMap 操作时间
```

**风险:** 低 (语义等价, 只是重新组织生成代码的结构)
**代码改动:** UHT 生成器 ~40 行 C# + 运行时暴露 `GetClassFuncMaps()` (已暴露)

---

### 9.8 P6: 合并 Delegate 双重遍历

**问题分析:**

```cpp
// Bind_Delegates.cpp - 当前存在两次独立的 UDelegateFunction 遍历

// 第一次遍历 (EOrder::Late - 100): 声明阶段
for (UDelegateFunction* Function : TObjectRange<UDelegateFunction>())
{
    // 注册 delegate 类型到 AS 引擎
}

// 第二次遍历 (EOrder::Late): 绑定阶段  ← 实测 18.770ms
for (UDelegateFunction* Function : TObjectRange<UDelegateFunction>())
{
    // 绑定 delegate 方法到 AS 引擎
}

// TObjectRange<UDelegateFunction>() 遍历整个 UObject 哈希表
// 这是一个全局扫描操作, 代价不低
```

**修改方案:**

```cpp
// 合并为单次遍历
AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_Delegates_Combined(
    FAngelscriptBinds::EOrder::Late, []()
{
    for (UDelegateFunction* Function : TObjectRange<UDelegateFunction>())
    {
        // Phase 1: 声明 (原第一次遍历的逻辑)
        DeclareDelegate(Function);
        
        // Phase 2: 绑定 (原第二次遍历的逻辑)
        BindDelegate(Function);
    }
});
```

**收益:** 消除一次 `TObjectRange<UDelegateFunction>()` 全局扫描, 节约 ~50% Delegate 阶段时间 (~9ms)
**风险:** 极低 (需确认两阶段之间无全局依赖; 如果有,可在单次遍历中先收集再处理)
**代码改动:** ~30 行 C++ 重构

---

### 9.9 P7: 排序一次 + 缓存

**问题分析:**

```cpp
// AngelscriptBinds.cpp - GetSortedBindArray() 每次调用都复制+排序
static TArray<FBindFunction> GetSortedBindArray()
{
    TArray<FBindFunction> SortedBinds = GetBindArray();  // ← 全量复制
    SortedBinds.Sort();                                   // ← O(n log n) 排序
    return SortedBinds;                                   // ← 返回值复制
}

// 该函数被多处调用:
//   CallBinds()               → 主路径
//   GetAllRegisteredBindNames() → 信息查询
//   GetBindInfoList()          → 信息查询
```

**修改方案:**

```cpp
// 方案: 排序一次, 缓存结果, 标记脏位

static bool bBindArraySorted = false;

static TArray<FBindFunction>& GetSortedBindArray()
{
    auto& BindArray = GetBindArray();
    if (!bBindArraySorted)
    {
        BindArray.Sort();
        bBindArraySorted = true;
    }
    return BindArray;  // 返回引用, 无复制
}

void FAngelscriptBinds::RegisterBinds(FName BindName, int32 BindOrder,
    TFunction<void()> Function)
{
    GetBindArray().Add({...});
    bBindArraySorted = false;  // 标记需要重新排序
}
```

**收益:** 消除重复排序和数组复制; 多次调用只排序一次
**风险:** 极低 (排序结果在 RegisterBinds 之后不变)
**代码改动:** ~10 行

---

### 9.10 P8: 扁平数组替代嵌套 TMap

**问题分析:**

```
当前数据结构:
TMap<UClass*, TMap<FName, FFuncEntry>> ClassFuncMaps
    │              │
    │              └─ 内层 TMap: 哈希桶数组 + 链表节点
    │                  每个 UClass ~10-80 个函数
    │                  内存: 散布在堆上各处
    │
    └─ 外层 TMap: 哈希桶数组
        ~300-500 个 UClass
        每次查找: 计算哈希 → 桶索引 → 链表遍历

Cache 行为:
    查找一个函数: 2 次 TMap 查找 = 2 次哈希 + 2 次指针追踪
    指针追踪 → L1/L2 cache miss (内层 TMap 不连续)
    ~6,042 个 FFuncEntry 散布在内存各处 → cache 利用率极低
```

**修改方案:**

```cpp
// 方案: 单一扁平数组 + 二级索引

struct FClassFuncEntry
{
    UClass* Class;
    FName FuncName;
    FFuncEntry Entry;
};

struct FOptimizedBindState
{
    // 核心存储: 按 (Class, FuncName) 排序的连续数组
    TArray<FClassFuncEntry> SortedEntries;
    
    // 一级索引: UClass* → 在 SortedEntries 中的起始偏移和数量
    TMap<UClass*, TPair<int32, int32>> ClassIndex;  // {Offset, Count}

    // 查找: O(1) UClass 查找 + O(log n) 二分查找函数名
    FFuncEntry* Find(UClass* Class, FName FuncName)
    {
        auto* Range = ClassIndex.Find(Class);
        if (!Range) return nullptr;
        
        // 在 [Offset, Offset+Count) 范围内二分查找
        auto* Begin = SortedEntries.GetData() + Range->Key;
        auto* End = Begin + Range->Value;
        auto* It = Algo::LowerBound(Begin, End, FuncName,
            [](const FClassFuncEntry& E, FName Name) { return E.FuncName < Name; });
        
        if (It != End && It->FuncName == FuncName)
            return &It->Entry;
        return nullptr;
    }
    
    // 构建: 收集完所有条目后一次性排序 + 构建索引
    void Build()
    {
        SortedEntries.Sort([](const FClassFuncEntry& A, const FClassFuncEntry& B)
        {
            if (A.Class != B.Class) return A.Class < B.Class;
            return A.FuncName.FastLess(B.FuncName);
        });
        
        // 构建一级索引
        UClass* CurrentClass = nullptr;
        int32 StartIdx = 0;
        for (int32 i = 0; i <= SortedEntries.Num(); ++i)
        {
            UClass* ThisClass = (i < SortedEntries.Num()) ? SortedEntries[i].Class : nullptr;
            if (ThisClass != CurrentClass)
            {
                if (CurrentClass)
                    ClassIndex.Add(CurrentClass, {StartIdx, i - StartIdx});
                CurrentClass = ThisClass;
                StartIdx = i;
            }
        }
    }
};
```

**收益:**

```
内存布局:
    修改前: N 个独立 TMap, 每个在堆上散布
    修改后: 1 个连续 TArray, cache line 友好

查找性能:
    修改前: 2 次哈希查找 (外层 + 内层), 2+ 次 cache miss
    修改后: 1 次哈希查找 (ClassIndex) + O(log n) 二分 (连续内存, cache 友好)
    对于 n=80 个函数/类: log₂(80) ≈ 7 次比较, 全部在连续内存上

内存节约:
    修改前: TMap 开销 = 哈希桶数组 + 每节点 sizeof(TPair) + 对齐
            ~300 个内层 TMap × (~256 bytes 基础开销) ≈ ~75 KB 元数据
    修改后: 1 个 TArray + 1 个 ClassIndex TMap
            ~6,042 × sizeof(FClassFuncEntry) ≈ ~290 KB 连续数据
            + ~300 × 12 bytes ClassIndex ≈ ~3.6 KB
            总元数据开销减少 ~70 KB
            
预估: 查找性能提升 2-3x, 构建性能提升 30-50%
```

**风险:** 中 (需要修改所有 ClassFuncMaps 的消费点; 构建后不可变, 不支持增量添加)
**代码改动:** ~100 行 C++ (新数据结构 + 修改所有消费点)

---

### 9.11 P9: 按需延迟注册 (低频模块)

**问题分析:**

```
当前: 所有 14 个模块的函数表在启动时全部注册
实际使用率分析:

模块                条目数   桩条目率   典型使用频率
GameplayTags         35     100%       低 (全是桩, 全走反射)
AssetRegistry        48     89.6%      低 (编辑器工具类)
Landscape            27     44.4%      低 (关卡设计专用)
EngineSettings        3     33.3%      极低
NavigationSystem     33     21.2%      中 (AI 导航)

这些模块合计: 146 条目, 占总量 2.4%
但如果项目脚本不使用这些模块, 注册完全浪费
```

**修改方案:**

```cpp
// 方案: 标记低频模块为延迟加载, 首次访问时才注册

// 1. UHT 生成器标记延迟模块
static const TSet<FString> DeferredModules = {
    TEXT("GameplayTags"), TEXT("AssetRegistry"),
    TEXT("Landscape"), TEXT("EngineSettings")
};

// 2. 生成的代码使用延迟注册
AS_FORCE_LINK const FAngelscriptBinds::FDeferredBind Bind_AS_FunctionTable_GameplayTags(
    TEXT("GameplayTags"),
    []() { /* 注册逻辑 */ }
);

// 3. 首次访问时触发
FFuncEntry* FAngelscriptBinds::FindFunctionEntry(UClass* Class, FName Name)
{
    auto* Result = GetClassFuncMaps().Find(Class);
    if (!Result)
    {
        // 尝试延迟加载该 Class 所属模块
        TryLoadDeferredModule(Class);
        Result = GetClassFuncMaps().Find(Class);
    }
    return Result ? Result->Find(Name) : nullptr;
}
```

**收益:** 减少启动时 146 条不必要的注册; 更重要的是提供了扩展机制
**风险:** 中 (需要建立模块→UClass 的反向映射; 首次访问有延迟)
**代码改动:** ~60 行

---

### 9.12 P10: 并行分片注册

**问题分析:**

```
当前 32 个分片严格串行执行:
    Shard 0 → Shard 1 → ... → Shard 31
                                                
AngelScript Register API 非线程安全, 但:
    AddFunctionEntry() 只操作 ClassFuncMaps (TMap)
    不调用任何 AngelScript 引擎 API
    只有后续的 BindScriptTypes() 才调用 RegisterObjectMethod()
                                                
这意味着: AddFunctionEntry 阶段理论上可以并行!
```

**修改方案:**

```cpp
// 方案: 分片并行填充 ClassFuncMaps, 使用分段锁

// 1. 为 ClassFuncMaps 添加分段锁 (按 UClass* 哈希分段)
static constexpr int32 NUM_LOCK_SEGMENTS = 16;
static FRWLock ClassFuncMapLocks[NUM_LOCK_SEGMENTS];

static FRWLock& GetLockForClass(UClass* Class)
{
    uint32 Hash = GetTypeHash(Class);
    return ClassFuncMapLocks[Hash % NUM_LOCK_SEGMENTS];
}

// 2. 线程安全的 AddFunctionEntry
static void AddFunctionEntryThreadSafe(UClass* Class, FName Name, FFuncEntry Entry)
{
    FRWScopeLock Lock(GetLockForClass(Class), SLT_Write);
    auto& FuncMap = GetClassFuncMaps().FindOrAdd(Class);
    FuncMap.FindOrAdd(Name, MoveTemp(Entry));
}

// 3. 并行执行分片
ParallelFor(ShardCount, [&](int32 ShardIndex)
{
    ExecuteShard(ShardIndex);  // 内部调用 AddFunctionEntryThreadSafe
});
```

**收益:** 理论上 N 核 → N 倍加速 (受锁竞争限制, 实际 2-4x)
**风险:** 高 (并发 TMap 操作需要仔细验证; 分段锁粒度需要调优)
**代码改动:** ~50 行 C++ + 所有分片的生成代码需要使用线程安全版本

---

### 9.13 组合优化方案推荐

#### 阶段一: 立即可做 (1-2 小时, 预估节约 40-60%)

```
P0 (预分配)  +  P2 (消除双重查找)  +  P7 (排序缓存)  +  P6 (合并 Delegate)
                                                
改动量: ~50 行 C++
风险: 极低
预估效果:
    P0: -10~20% TMap 插入时间
    P2: -40~60% 哈希操作次数
    P7: -100% 重复排序开销
    P6: -50% Delegate 阶段 (~9ms)
    组合: -40~60% 总启动注册时间
```

#### 阶段二: 短期实施 (1-2 天, 在阶段一基础上再节约 30-50%)

```
P1 (FName 键)  +  P3 (跳过桩条目)  +  P5 (按 Class 分组)
                                                
改动量: ~80 行 C++ + ~50 行 C# (UHT 生成器)
风险: 低
预估效果:
    P1: -30~50% 键操作时间
    P3: -43% 条目数量 (从 6,042 → ~3,418)
    P5: -92% 外层 TMap 查找
    组合: 在阶段一基础上再减少 30-50%
```

#### 阶段三: 中期实施 (3-5 天, 最大化性能)

```
P4 (静态数组)  +  P8 (扁平数组)
                                                
改动量: ~150 行 C++ + ~50 行 C# (UHT 生成器)
风险: 中
预估效果:
    P4: 编译时数据 + cache 友好
    P8: 查找性能 2-3x 提升
    组合: 接近 UEAS2 的性能水平
```

#### 预估总效果

```
                    当前         阶段一后      阶段二后      阶段三后
AddFunctionEntry    6,042 次     6,042 次     3,418 次      3,418 次
外层 TMap 查找       ~6,042 次    ~6,042 次    ~300 次       0 (扁平数组)
内层 TMap 查找       ~6,042 次    ~6,042 次    ~3,418 次     O(log n) 二分
键类型              FString      FString      FName         FName
哈希操作/次         O(n)         O(n)         O(1)          O(1)
内存布局            散布堆上     散布堆上     散布堆上      连续数组
排序开销            每次调用     一次缓存     一次缓存      构建时一次
预估总时间          100%         40-60%       20-35%        10-20%
```

---

## 10. ClassFuncMaps 容器结构优化: 复合键扁平 TMap 方案

> 本节是对 P1 / P2 / P8 的整合落地方案，将嵌套 `TMap<UClass*, TMap<FString, FFuncEntry>>`
> 替换为单层 `TMap<FClassFuncKey, FFuncEntry>`，一次性消除三层性能缺陷。

### 10.1 当前结构的三层缺陷

#### 缺陷 1: 嵌套 TMap 双重指针追踪

```
查找一个函数需要:

外层 TMap.Find(UClass*)               → hash(指针) → 桶 → 拿到内层 TMap*
    └─ 内层 TMap.Find(FString)        → hash(字符串) → 桶 → 拿到 FFuncEntry*

两次 hash, 两次指针跳转, 两次 cache miss
内层 TMap 对象散布在堆上各处 → 完全无 cache 局部性
~500 个内层 TMap 对象 × ~256 bytes 基础开销 = ~128 KB 纯元数据浪费
```

#### 缺陷 2: FString 键的全链路代价

```cpp
// ====== 写入端 (AddFunctionEntry, 调用 6,042 次) ======

// UHT 生成的代码:
FAngelscriptBinds::AddFunctionEntry(
    AActor::StaticClass(),
    "GetActorLocation",        // ← const char* → FString: 堆分配 + UTF8→TCHAR 转换
    { ERASE_METHOD_PTR(...) });

// AddFunctionEntry 内部:
ClassFuncMaps[Class].Contains(Name)  // ← FString hash: O(字符串长度), 逐字符
ClassFuncMaps[Class].Add(Name, ...)  // ← FString 拷贝: 又一次堆分配

// 每条插入: 2 次 FString 堆分配 + 2 次 O(n) 哈希
// 6,042 条总计: ~12,084 次堆分配 + ~12,084 次 O(n) 哈希


// ====== 读取端 (Bind_BlueprintCallable.cpp 39-43 行, 数千次) ======

FString Name = Function->GetFName().ToString();  // ← FName→FString: 堆分配+拷贝!
auto* map = FAngelscriptBinds::GetClassFuncMaps().Find(OwningClass);
if (map)
    Entry = map->Find(Name);  // ← FString hash: O(字符串长度)

// 每次读取: 1 次 FString 堆分配 + 1 次 O(n) 哈希
// UFunction 本身已有 FName (O(1) 哈希), 却被转成 FString 再做 O(n) 哈希
```

#### 缺陷 3: AddFunctionEntry 的冗余查找

```cpp
// AngelscriptBinds.h 497-512 行 — 当前实现
static void AddFunctionEntry(UClass* Class, FString Name, FFuncEntry Entry)
{
    auto& ClassFuncMaps = GetClassFuncMaps();
    if (ClassFuncMaps.Contains(Class))         // ← 查找 1 (外层 hash)
    {
        if (!ClassFuncMaps[Class].Contains(Name))  // ← 查找 2 (外层 hash) + 查找 3 (内层 hash)
        {
            ClassFuncMaps[Class].Add(Name, Entry); // ← 查找 4 (外层 hash) + 插入 (内层)
        }
    }
    else
    {
        ClassFuncMaps.Add(Class, TMap<FString, FFuncEntry>()).Add(Name, Entry);
    }
}
// 最坏路径: 3 次外层哈希 + 2 次内层哈希 = 5 次哈希操作 / 每条插入
// 6,042 条 × 5 次 = ~30,210 次哈希操作
```

### 10.2 替换方案: 复合键扁平 TMap

```
修改前:                                    修改后:

TMap<UClass*, ─────────┐                   TMap<FClassFuncKey, FFuncEntry>
  │                    │                       │
  ├─ AActor* ────→ TMap<FString, FFuncEntry>   ├─ {AActor*, "GetActorLocation"} → FFuncEntry
  │                  ├─ "GetActorLocation"     ├─ {AActor*, "SetActorLocation"} → FFuncEntry
  │                  ├─ "SetActorLocation"     ├─ {AActor*, "K2_DestroyActor"} → FFuncEntry
  │                  └─ "K2_DestroyActor"      ├─ {USceneComponent*, "GetRelativeLocation"} → FFuncEntry
  │                                            └─ ...
  ├─ USceneComponent* → TMap<FString, ...>
  │                      └─ ...             一层结构: 1 次哈希, 1 次查找, 0 次指针追踪
  └─ ...                                    FName 键: O(1) 哈希, O(1) 比较
                                            连续内存: cache 友好
两层结构: 2 次哈希, 2+ 次指针追踪
FString 键: O(n) 哈希, O(n) 比较
散布堆上: cache 不友好
```

#### 10.2.1 复合键定义

```cpp
// AngelscriptBinds.h — 新增

// 复合键: UClass* + FName, 扁平存储在单层 TMap 中
struct FClassFuncKey
{
    UClass* Class;
    FName   FuncName;

    FORCEINLINE bool operator==(const FClassFuncKey& Other) const
    {
        // 两个 O(1) 比较: 指针比较 + FName::ComparisonIndex 比较
        return Class == Other.Class && FuncName == Other.FuncName;
    }

    FORCEINLINE friend uint32 GetTypeHash(const FClassFuncKey& Key)
    {
        // 两个 O(1) 哈希合并, 无字符串遍历
        // UClass*: hash = 指针值右移 (O(1))
        // FName:   hash = 预缓存的 ComparisonIndex (O(1))
        return HashCombineFast(GetTypeHash(Key.Class), GetTypeHash(Key.FuncName));
    }
};
```

#### 10.2.2 FAngelscriptBindState 修改

```cpp
// AngelscriptBinds.h — 修改 FAngelscriptBindState

struct ANGELSCRIPTRUNTIME_API FAngelscriptBindState
{
    // ========== 修改前 ==========
    // TMap<UClass*, TMap<FString, FFuncEntry>> ClassFuncMaps;

    // ========== 修改后 ==========
    TMap<FClassFuncKey, FFuncEntry> ClassFuncMaps;

    // 其余字段完全不变:
    TMap<FString, TArray<TObjectPtr<UClass>>> RuntimeClassDB;
#if WITH_EDITOR
    TMap<FString, TArray<TObjectPtr<UClass>>> EditorClassDB;
#endif
    TArray<FString> BindModuleNames;
    TMap<UClass*, TSet<FString>> SkipBinds;
    TSet<TTuple<FName, FName>> SkipBindNames;
    TSet<FName> SkipBindClasses;
    int32 PreviouslyBoundFunction = -1;
    int32 PreviouslyBoundGlobalProperty = -1;
};
```

#### 10.2.3 GetClassFuncMaps 返回类型修改

```cpp
// AngelscriptBinds.h — 修改声明
static TMap<FClassFuncKey, FFuncEntry>& GetClassFuncMaps();

// AngelscriptBinds.cpp — 修改实现
TMap<FClassFuncKey, FFuncEntry>& FAngelscriptBinds::GetClassFuncMaps()
{
    return GetBindState().ClassFuncMaps;
}
```

#### 10.2.4 AddFunctionEntry 重写

```cpp
// AngelscriptBinds.h — 替换原有实现
// 修改前: 5 次哈希操作 + FString 堆分配
// 修改后: 1 次哈希操作 + 0 次堆分配

// 主入口: FName 键 (推荐, UHT 生成代码和新代码使用)
static void AddFunctionEntry(UClass* Class, FName Name, FFuncEntry Entry)
{
    auto& ClassFuncMaps = GetClassFuncMaps();
    // FindOrAdd: 哈希一次, 不存在则就地构造, 已存在则跳过
    // 保持"首次写入优先"语义: 手写绑定先于 UHT 生成执行
    ClassFuncMaps.FindOrAdd(FClassFuncKey{Class, Name}, MoveTemp(Entry));
}

// 兼容入口: FString 键 (过渡期, 供未迁移的手写 Bind_*.cpp 使用)
static void AddFunctionEntry(UClass* Class, const FString& Name, FFuncEntry Entry)
{
    AddFunctionEntry(Class, FName(*Name), MoveTemp(Entry));
}
```

#### 10.2.5 消费端修改 (Bind_BlueprintCallable.cpp)

```cpp
// Bind_BlueprintCallable.cpp 34-44 行

// ========== 修改前 ==========
UClass* OwningClass = CastChecked<UClass>(Function->GetOuter());
FFuncEntry* Entry = nullptr;

if (OwningClass != nullptr)
{
    FString Name = Function->GetFName().ToString();  // ← FName→FString: 堆分配!
    auto* map = FAngelscriptBinds::GetClassFuncMaps().Find(OwningClass);  // 查找 1
    if (map)
        Entry = map->Find(Name);  // 查找 2 (FString hash)
}

// ========== 修改后 ==========
UClass* OwningClass = CastChecked<UClass>(Function->GetOuter());
FFuncEntry* Entry = nullptr;

if (OwningClass != nullptr)
{
    // 直接用 FName: 0 次堆分配, 1 次 O(1) 哈希查找
    Entry = FAngelscriptBinds::GetClassFuncMaps().Find(
        FClassFuncKey{OwningClass, Function->GetFName()});
}
```

#### 10.2.6 辅助查询函数 (供测试和调试使用)

```cpp
// AngelscriptBinds.h — 新增辅助函数

// 单条查询: 替代原来的两层 Find
static FFuncEntry* FindFunctionEntry(UClass* Class, FName Name)
{
    return GetClassFuncMaps().Find(FClassFuncKey{Class, Name});
}

// 按 Class 遍历: 替代原来的 Find(UClass*) 获取内层 TMap 再遍历
// 注意: O(N 总条目), 仅用于测试/调试, 不在启动热路径上
static void ForEachFunctionEntry(UClass* Class,
    TFunctionRef<void(FName FuncName, const FFuncEntry& Entry)> Callback)
{
    for (const auto& Pair : GetClassFuncMaps())
    {
        if (Pair.Key.Class == Class)
        {
            Callback(Pair.Key.FuncName, Pair.Value);
        }
    }
}

// 统计某个 Class 的条目数 (测试用)
static int32 CountFunctionEntries(UClass* Class)
{
    int32 Count = 0;
    for (const auto& Pair : GetClassFuncMaps())
    {
        if (Pair.Key.Class == Class)
            ++Count;
    }
    return Count;
}
```

#### 10.2.7 UHT 生成器修改 (C#)

```csharp
// AngelscriptFunctionTableCodeGenerator.cs — BuildRegistrationLine()

// 修改前:
public string BuildRegistrationLine()
{
    return $"\tFAngelscriptBinds::AddFunctionEntry("
         + $"{ClassName}::StaticClass(), \"{FunctionName}\", "
         + $"{{ {EraseMacro} }});";
}

// 修改后:
public string BuildRegistrationLine()
{
    return $"\tFAngelscriptBinds::AddFunctionEntry("
         + $"{ClassName}::StaticClass(), "
         + $"FName(TEXT(\"{FunctionName}\")), "    // ← FName 替代裸字符串
         + $"{{ {EraseMacro} }});";
}
```

#### 10.2.8 测试代码适配

```cpp
// AngelscriptGeneratedFunctionTableTests.cpp — 典型修改模式

// ========== 修改前 ==========
const TMap<UClass*, TMap<FString, FFuncEntry>>& ClassFuncMaps =
    FAngelscriptBinds::GetClassFuncMaps();
const TMap<FString, FFuncEntry>* ActorFunctionMap =
    ClassFuncMaps.Find(AActor::StaticClass());
if (ActorFunctionMap)
{
    for (const TPair<FString, FFuncEntry>& FuncEntry : *ActorFunctionMap)
    {
        // ... 检查 FuncEntry.Key 和 FuncEntry.Value ...
    }
}

// ========== 修改后 ==========
// 单条查询:
FFuncEntry* Entry = FAngelscriptBinds::FindFunctionEntry(
    AActor::StaticClass(), FName(TEXT("GetActorLocation")));
TestNotNull(TEXT("Actor should have GetActorLocation"), Entry);

// 遍历某个 Class 的全部条目:
int32 ActorEntryCount = 0;
bool bHasCallableEntry = false;
FAngelscriptBinds::ForEachFunctionEntry(AActor::StaticClass(),
    [&](FName FuncName, const FFuncEntry& Entry)
    {
        ActorEntryCount++;
        if (Entry.FuncPtr.IsBound())
            bHasCallableEntry = true;
    });
TestTrue(TEXT("Actor should have entries"), ActorEntryCount > 0);

// 统计总条目数 (替代遍历整个嵌套 TMap):
int32 TotalCount = FAngelscriptBinds::GetClassFuncMaps().Num();
TestTrue(TEXT("Should have many entries"), TotalCount > 1000);
```

### 10.3 完整受影响文件清单

```
文件                                                      改动内容                              改动量
                                                                                              
Core/AngelscriptBinds.h                                   FClassFuncKey 定义                    ~15 行
                                                          ClassFuncMaps 类型替换                ~3 行
                                                          AddFunctionEntry 重写                 ~12 行
                                                          GetClassFuncMaps 签名修改             ~1 行
                                                          FindFunctionEntry 辅助函数            ~15 行
                                                          ForEachFunctionEntry 辅助函数         ~12 行
                                                                                              小计 ~58 行

Core/AngelscriptBinds.cpp                                 GetClassFuncMaps 返回类型             ~1 行
                                                                                              小计 ~1 行

Binds/Bind_BlueprintCallable.cpp                          消费端 Find 改为复合键                ~3 行
                                                                                              小计 ~3 行

Core/FunctionCallers.h                                    无 (FFuncEntry 不变)                  0 行

AngelscriptUHTTool/                                       BuildRegistrationLine 加 FName        ~2 行
  AngelscriptFunctionTableCodeGenerator.cs                                                    小计 ~2 行

AngelscriptTest/Core/                                     FindFunctionEntry 替代两层 Find       ~35 行
  AngelscriptBindConfigTests.cpp                          ForEachFunctionEntry 替代遍历
  AngelscriptGeneratedFunctionTableTests.cpp                                                  小计 ~35 行
                                                                                              
                                                                                    总计 ~99 行
```

```
分类统计:
  核心运行时 (影响启动性能):  ~62 行 C++ + ~2 行 C#
  测试代码 (机械替换):        ~35 行 C++
  
  总共需要修改的文件:         6 个
  不需要修改的文件:           FunctionCallers.h, 所有 Bind_*.cpp (除 BlueprintCallable),
                              所有其他运行时代码
```

### 10.4 性能对比

```
                            嵌套 TMap + FString (当前)       扁平 TMap + FName (优化后)

写入 (AddFunctionEntry × 6,042)
  哈希次数/条               3-5 次                           1 次
  哈希单次复杂度            外层 O(1) + 内层 O(字符串长度)   O(1) + O(1) 合并 = O(1)
  堆分配/条                 1-2 次 (FString 键)              0 次 (FName 栈内 8 bytes)
  6,042 条总计              ~24,168 次哈希                    ~6,042 次哈希
                            ~12,084 次堆分配                  0 次堆分配

读取 (BindBlueprintCallable × 数千)
  查找次数/函数             2 次 (外层 + 内层)               1 次
  FName→FString 转换        每次 1 次 (堆分配)               0 次
  哈希单次复杂度            O(1) + O(字符串长度)             O(1)
  数千次读取总计             2N 次哈希 + N 次堆分配          N 次哈希 + 0 次堆分配

Cache 行为
  数据布局                  外层桶 → 内层 TMap 对象 (堆散布) → FFuncEntry (堆散布)
                            3 级指针追踪, 3 次 cache miss
  优化后                    桶 → FClassFuncKey+FFuncEntry (TMap 节点内)
                            1 级指针追踪, 1 次 cache miss

内存占用 (6,042 条)
  修改前:
    外层 TMap 元数据                  ~8 KB
    ~500 个内层 TMap 对象             ~500 × 256 B = ~128 KB
    6,042 个 FString 键               ~6,042 × 40 B = ~242 KB
    6,042 个 FFuncEntry               ~6,042 × 44 B = ~266 KB
    总计                              ~644 KB

  修改后:
    单层 TMap 元数据                  ~8 KB
    6,042 个 FClassFuncKey (16 B)     ~6,042 × 16 B = ~97 KB
    6,042 个 FFuncEntry (44 B)        ~6,042 × 44 B = ~266 KB
    总计                              ~371 KB (节约 ~42%)
```

### 10.5 调用链对比

```
====== 修改前: 嵌套 TMap + FString ======

AddFunctionEntry("GetActorLocation")
    │
    ├─ const char* → FString 构造         ← 堆分配 + UTF8→TCHAR
    ├─ ClassFuncMaps.Contains(UClass*)     ← 外层哈希 #1
    ├─ ClassFuncMaps[UClass*]              ← 外层哈希 #2 (重复!)
    │   └─ .Contains(FString)              ← 内层哈希 #1, O(字符串长度)
    ├─ ClassFuncMaps[UClass*]              ← 外层哈希 #3 (又重复!)
    │   └─ .Add(FString, FFuncEntry)       ← 内层插入 + FString 键拷贝 (堆分配)
    └─ 完成 (5 次哈希, 2 次堆分配)

BindBlueprintCallable 消费:
    │
    ├─ Function->GetFName().ToString()     ← FName→FString: 堆分配!
    ├─ ClassFuncMaps.Find(UClass*)         ← 外层哈希 #1
    │   └─ 返回 TMap<FString,FFuncEntry>*  ← 指针追踪 (cache miss)
    └─ innerMap->Find(FString)             ← 内层哈希 #1, O(字符串长度)
        └─ 返回 FFuncEntry*               ← 又一次指针追踪 (cache miss)
    (2 次哈希 + 1 次堆分配 + 2 次 cache miss)


====== 修改后: 扁平 TMap + FName ======

AddFunctionEntry(FName(TEXT("GetActorLocation")))
    │
    ├─ FClassFuncKey{Class, FuncName} 构造 ← 栈上 16 bytes, 零堆分配
    └─ ClassFuncMaps.FindOrAdd(Key, Entry) ← 1 次 O(1) 哈希, 就地插入
    完成 (1 次哈希, 0 次堆分配)

BindBlueprintCallable 消费:
    │
    ├─ Function->GetFName()                ← 直接取 FName, 零转换
    ├─ FClassFuncKey{OwningClass, FName}   ← 栈上构造
    └─ ClassFuncMaps.Find(Key)             ← 1 次 O(1) 哈希
        └─ 返回 FFuncEntry*
    (1 次哈希 + 0 次堆分配 + 1 次 cache miss)
```

### 10.6 风险与回退策略

```
风险点                                        缓解措施

1. ForEachFunctionEntry 遍历是 O(N 总条目)    仅用于测试/调试, 不在启动路径上;
   (原来 Find(UClass*) 获取内层 TMap 是 O(1))  如需高频按 Class 遍历, 可加 ClassIndex 辅助表

2. 测试代码中大量 TMap<FString,...> 类型引用   机械替换, 编译器会报出所有遗漏点
                                               可用 static_assert 验证新旧类型不混用

3. FindOrAdd 的"首次写入优先"语义              FindOrAdd(Key, DefaultValue) 仅在 Key 不存在时
                                               插入 DefaultValue, 已存在则返回现有值 — 语义一致

4. 手写 Bind_*.cpp 中的 FString 入口           保留 FString 重载作为过渡,
                                               内部转 FName 后调用主入口

5. ResetBindState() 清空                       TMap::Reset() 行为不变, 无影响
```

### 10.7 与其他优化方案的叠加

```
此方案与其他方案的兼容性:

P0 (预分配)       → 叠加: ClassFuncMaps.Reserve(6500) 仍然有效
P3 (跳过桩条目)   → 叠加: 条目从 6,042 减至 ~3,418, 扁平 TMap 更小
P4 (静态数组)     → 叠加: 批量 AddFunctionEntries 可直接操作扁平 TMap
P5 (按 Class 分组) → 部分替代: 扁平 TMap 无外层查找, P5 的分组收益被吸收
                    但 P5 的内层 Reserve 仍有意义 → 改为总量 Reserve
P6 (合并 Delegate) → 独立: 无冲突
P7 (排序缓存)     → 独立: 无冲突
P9 (延迟注册)     → 叠加: FindFunctionEntry 可加延迟加载钩子
P10 (并行注册)    → 叠加: 扁平 TMap + 分段锁比嵌套 TMap 更容易实现

推荐实施顺序:
    1. 本方案 (P1+P2+P8 整合)  → 一次性解决容器结构问题
    2. P0 (Reserve)            → 1 行追加
    3. P3 (跳过桩条目)          → UHT 生成器修改
    4. P7 (排序缓存)            → 独立小改动
    5. P6 (合并 Delegate)       → 独立重构
```

---

## 11. FAngelscriptBindState 与 AngelScript 引擎的绑定机制

> 本节分析 `FAngelscriptBindState` 中的数据如何最终流入 AngelScript 脚本引擎 (`asCScriptEngine`)，
> 以及整个所有权链、生命周期、和运行时调用路径。

### 11.1 所有权链总览

FAngelscriptEngine 的所有权有两种模式，取决于启动时序：

**模式 A: 编辑器 / Commandlet 启动** — Module 拥有引擎

```
FAngelscriptRuntimeModule::StartupModule()
    │  bIsEditor || bIsRunningCommandlet → InitializeAngelscript()
    │  此时 GameInstance 尚不存在, Subsystem 未创建
    │
    └─ OwnedPrimaryEngine: TUniquePtr<FAngelscriptEngine>    ← Module 拥有
        └─ Push 到 EngineContextStack
                │
                │  稍后 GameInstance 创建:
                v
UAngelscriptGameInstanceSubsystem::Initialize()
    └─ PrimaryEngine = TryGetCurrentEngine()                  ← 找到 Module 的引擎
       bOwnsPrimaryEngine = false                              ← Subsystem 不拥有, 只持有裸指针
       (只负责 Tick 转发, 不负责生命周期)
```

**模式 B: 运行时启动 (非编辑器)** — Subsystem 拥有引擎

```
FAngelscriptRuntimeModule::StartupModule()
    └─ !bIsEditor && !bIsRunningCommandlet → 跳过初始化
                │
                │  GameInstance 创建:
                v
UAngelscriptGameInstanceSubsystem::Initialize()
    └─ PrimaryEngine = TryGetCurrentEngine()                  ← 返回 nullptr (Module 没创建)
       PrimaryEngine = &OwnedEngine                            ← Subsystem 拥有 (UPROPERTY 成员)
       bOwnsPrimaryEngine = true
       Push 到 EngineContextStack
       OwnedEngine.Initialize()                                ← Subsystem 驱动初始化
```

**内部结构 (两种模式共享):**

```
FAngelscriptEngine (无论 Module 还是 Subsystem 拥有)
    │
    ├─ Engine: asCScriptEngine*                // AngelScript 脚本引擎实例
    │
    └─ SharedState: TSharedPtr<FAngelscriptOwnedSharedState>
        │  拥有 (TUniquePtr)
        ├─ ScriptEngine: asCScriptEngine*      // 同一指针的副本 (便于共享访问)
        ├─ PrimaryContext: asCContext*          // 主线程执行上下文
        ├─ TypeDatabase: FAngelscriptTypeDatabase
        ├─ BindState: FAngelscriptBindState    ← ★ 本节分析对象
        │   ├─ ClassFuncMaps                   // UClass→函数→FFuncEntry 查找表
        │   ├─ RuntimeClassDB                  // 运行时类数据库
        │   ├─ BindModuleNames                 // 绑定模块名称列表
        │   ├─ SkipBinds / SkipBindNames       // 跳过绑定配置
        │   └─ PreviouslyBoundFunction         // 上一个绑定的函数 ID (串行耦合)
        ├─ ToStringList
        ├─ BindDatabase: FAngelscriptBindDatabase
        └─ PrecompiledData / StaticJIT / DebugServer
```

### 11.2 BindState 的创建与生命周期

```cpp
// AngelscriptEngine.cpp:989-998
// 创建时机: FAngelscriptEngine::Initialize() → EnsureSharedStateCreated()

void FAngelscriptEngine::EnsureSharedStateCreated()
{
    if (bOwnsEngine && !SharedState.IsValid())
    {
        SharedState = MakeShared<FAngelscriptOwnedSharedState>();
        SharedState->TypeDatabase = MakeUnique<FAngelscriptTypeDatabase>();
        SharedState->BindState = MakeUnique<FAngelscriptBindState>();   // ← 创建
        SharedState->ToStringList = MakeUnique<TArray<FToStringType>>();
        SharedState->BindDatabase = MakeUnique<FAngelscriptBindDatabase>();
    }
}

// 访问: FAngelscriptEngine::GetBindState()
FAngelscriptBindState* FAngelscriptEngine::GetBindState() const
{
    return SharedState.IsValid() ? SharedState->BindState.Get() : nullptr;
}

// 销毁时机: FAngelscriptEngine::Shutdown() 中
//   SharedState->BindState.Reset();   // ← 销毁
//   SharedState->ScriptEngine->ShutDownAndRelease();
```

### 11.3 GetBindState() 的全局访问路径

```cpp
// AngelscriptBinds.cpp:23-34
// BindState 的全局访问入口 — 所有 FAngelscriptBinds 静态方法都通过此函数

static FAngelscriptBindState& GetBindState()
{
    // 优先: 从当前引擎上下文获取
    if (FAngelscriptEngine* Engine = FAngelscriptEngine::TryGetCurrentEngine())
    {
        if (FAngelscriptBindState* State = Engine->GetBindState())
        {
            return *State;
        }
    }
    // 回退: 使用静态局部变量 (兼容无引擎场景)
    static FAngelscriptBindState LegacyBindState;
    return LegacyBindState;
}
```

```
TryGetCurrentEngine() 的查找路径:

1. FAngelscriptEngineContextStack::Peek()
   └─ GAngelscriptEngineContextStack (TArray<FAngelscriptEngine*> 全局栈)
      └─ 返回栈顶引擎 (如果有)

2. (回退) UAngelscriptGameInstanceSubsystem::GetCurrent()
   └─ 从当前 GameInstance 获取子系统
      └─ 返回子系统持有的引擎

3. (回退) nullptr → 使用 LegacyBindState 静态实例
```

### 11.4 BindState 数据流入 AngelScript 引擎的完整路径

```
FAngelscriptBindState 的数据不直接存储在 asCScriptEngine 中。
它是一个 **中间查找表**，在绑定执行阶段被读取，
其信息通过 AngelScript 注册 API 转化为 asCScriptEngine 内部的类型/函数记录。

完整数据流:

[阶段 1: 填充 BindState]

DLL 加载时
    └─ AS_FORCE_LINK FBind 静态构造
        └─ RegisterBinds(Order, Lambda) → GetBindArray().Add()
                                           (仅注册 lambda, 不执行)

FAngelscriptEngine::Initialize()
    └─ EnsureSharedStateCreated()         → 创建 BindState
    └─ BindScriptTypes()
        └─ CallBinds()
            └─ 逐条执行已排序的 FBind lambda:

            [手写 Bind_*.cpp lambda 执行时]:
              ├─ FAngelscriptBinds::ReferenceClass("AActor", AActor::StaticClass())
              │   └─ asCScriptEngine::RegisterObjectType(...)     ← ★ 写入 AS 引擎
              ├─ FAngelscriptBinds::Method("FVector GetActorLocation()", &AActor::GetActorLocation)
              │   └─ asCScriptEngine::RegisterObjectMethod(...)   ← ★ 写入 AS 引擎
              └─ FAngelscriptBinds::Property("float Health", offsetof(...))
                  └─ asCScriptEngine::RegisterObjectProperty(...) ← ★ 写入 AS 引擎

            [UHT 生成 AS_FunctionTable_*.cpp lambda 执行时]:
              └─ AddFunctionEntry(AActor::StaticClass(), "GetActorLocation", {ERASE_...})
                  └─ BindState.ClassFuncMaps[AActor]["GetActorLocation"] = FFuncEntry
                     ★ 此阶段只写入 BindState, 不触及 AS 引擎


[阶段 2: 消费 BindState → 写入 AS 引擎]

    [手写 Bind_BlueprintType.cpp 遍历所有 UClass]:
      └─ 对每个 BlueprintCallable UFunction:
          ├─ BindBlueprintCallable(InType, Function, ...)
          │   ├─ ClassFuncMaps.Find(OwningClass)       ← 读取 BindState
          │   │   └─ map->Find(FunctionName)           ← 读取 BindState
          │   │       └─ 拿到 FFuncEntry (FuncPtr + Caller)
          │   │
          │   ├─ [有直接绑定 (FuncPtr.IsBound())]
          │   │   ├─ memcpy FFuncEntry.FuncPtr → asSFuncPtr
          │   │   └─ FAngelscriptBinds::BindMethodDirect(
          │   │       ClassName, Signature, asSFuncPtr,
          │   │       asCALL_THISCALL, Entry->Caller)
          │   │       └─ asCScriptEngine::RegisterObjectMethod(...)  ← ★ 写入 AS 引擎
          │   │
          │   └─ [无直接绑定 (桩条目 / 未注册)]
          │       └─ BindBlueprintCallableReflectiveFallback(...)
          │           └─ asCScriptEngine::RegisterObjectMethod(
          │               ..., asCALL_GENERIC, ...)                  ← ★ 写入 AS 引擎
          │               (注册反射分派器为 GENERIC 调用)
          │
          └─ PreviouslyBoundFunction = FunctionId                    ← 写回 BindState
```

### 11.5 BindState 各字段的作用与消费者

```cpp
struct FAngelscriptBindState
{
    // ======================== 核心查找表 ========================

    TMap<UClass*, TMap<FString, FFuncEntry>> ClassFuncMaps;
    // 写入者: AddFunctionEntry() — 手写 Bind_*.cpp + UHT 生成的 AS_FunctionTable_*.cpp
    // 读取者: BindBlueprintCallable() — 在 Bind_BlueprintType.cpp 遍历所有 UClass 时
    // 作用:   存储 UClass×函数名 → FFuncEntry(类型擦除函数指针 + 泛型调用器)
    // 流向:   FFuncEntry.FuncPtr → memcpy → asSFuncPtr → RegisterObjectMethod()
    //         FFuncEntry.Caller  → 作为 asFunctionCaller 传给 RegisterObjectMethod()
    //         最终写入 asCScriptEngine 的函数表

    // ======================== 类数据库 ========================

    TMap<FString, TArray<TObjectPtr<UClass>>> RuntimeClassDB;
#if WITH_EDITOR
    TMap<FString, TArray<TObjectPtr<UClass>>> EditorClassDB;
#endif
    // 写入者: FAngelscriptBinds::ReferenceClass() 等
    // 读取者: 类型查找、脚本编译时的类型解析
    // 作用:   按名称索引已绑定的 UClass, 用于运行时类型查找

    // ======================== 模块管理 ========================

    TArray<FString> BindModuleNames;
    // 写入者: LoadBindModules() — 从 BindModules.Cache 文件加载
    // 读取者: Initialize_AnyThread() — 遍历并 LoadModule() 加载 DLL
    // 作用:   记录需要加载的绑定模块 DLL 名称

    // ======================== 跳过绑定配置 ========================

    TMap<UClass*, TSet<FString>> SkipBinds;
    // 写入者: SkipFunctionEntry() — 手写绑定中标记要跳过的函数
    // 读取者: CheckForSkip() — BindBlueprintCallable 之前检查
    // 作用:   允许手写绑定覆盖 UHT 生成的绑定 (跳过特定函数)

    TSet<TTuple<FName, FName>> SkipBindNames;
    // 写入者: AddSkipEntry(ClassName, FunctionName)
    // 读取者: CheckForSkipEntry() — 按名称对跳过
    // 作用:   更细粒度的跳过控制

    TSet<FName> SkipBindClasses;
    // 写入者: AddSkipClass(ClassName)
    // 读取者: CheckForSkipClass() — 按类名跳过整个类
    // 作用:   跳过整个类的所有函数绑定

    // ======================== 串行状态 ========================

    int32 PreviouslyBoundFunction = -1;
    // 写入者: OnBind() — 每次 RegisterObjectMethod 成功后
    // 读取者: GetPreviousBind() / DeprecatePreviousBind() / SetPreviousBindIsEditorOnly() 等
    // 作用:   记录最近一次绑定的函数 ID, 用于链式配置 (设置属性修饰符)
    // ★ 这是并行化的主要障碍: 每次绑定修改此值, 后续调用依赖它

    int32 PreviouslyBoundGlobalProperty = -1;
    // 同上, 用于全局属性
};
```

### 11.6 FFuncEntry → asCScriptEngine 的转换细节

```cpp
// Bind_BlueprintCallable.cpp:72-98
// 当 ClassFuncMaps 中找到有效的直接绑定时:

auto* DirectNativePointer = &Entry->FuncPtr;                    // FGenericFuncPtr*
const bool bHasDirectNativePointer =
    DirectNativePointer != nullptr && DirectNativePointer->IsBound();

if (bHasDirectNativePointer)
{
    // FGenericFuncPtr 与 asSFuncPtr 是内存兼容的 (static_assert 保证)
    asSFuncPtr ASFuncPtr;
    static_assert(sizeof(asSFuncPtr) == sizeof(FGenericFuncPtr),
        "FGenericFuncPtr must be the same struct as asSFuncPtr");
    FMemory::Memcpy(&ASFuncPtr, DirectNativePointer, sizeof(asSFuncPtr));

    // 根据函数类型选择注册方式:
    if (Signature.bStaticInScript && Signature.bGlobalScope)
    {
        // 全局函数 (如 UKismetSystemLibrary::PrintString)
        FAngelscriptBinds::BindGlobalFunction(
            Signature.Declaration,    // "void PrintString(const FString& in)"
            ASFuncPtr,                // 类型擦除的函数指针
            Entry->Caller);           // 泛型调用器
        // → asCScriptEngine::RegisterGlobalFunction(
        //       signature, funcPtr, asCALL_CDECL, caller)
    }
    else if (Signature.bStaticInUnreal)
    {
        // 静态函数转 Mixin 成员 (如 UKismetMathLibrary 的数学函数)
        FAngelscriptBinds::BindMethodDirect(
            Signature.ClassName,      // "AActor"
            Signature.Declaration,    // "FVector GetActorForwardVector() const"
            ASFuncPtr, asCALL_CDECL_OBJFIRST,
            Entry->Caller);
        // → asCScriptEngine::RegisterObjectMethod(
        //       "AActor", signature, funcPtr, asCALL_CDECL_OBJFIRST, caller)
    }
    else
    {
        // 实例成员方法 (最常见路径)
        FAngelscriptBinds::BindMethodDirect(
            InType->GetAngelscriptTypeName(),  // "AActor"
            Signature.Declaration,              // "FVector GetActorLocation() const"
            ASFuncPtr, asCALL_THISCALL,
            Entry->Caller);
        // → asCScriptEngine::RegisterObjectMethod(
        //       "AActor", signature, funcPtr, asCALL_THISCALL, caller)
    }
}
else
{
    // 反射回退: 无直接函数指针
    BindBlueprintCallableReflectiveFallback(InType, Function, Signature, *Entry);
    // → asCScriptEngine::RegisterObjectMethod(
    //       typeName, signature, genericDispatcher, asCALL_GENERIC)
}
```

### 11.7 完整生命周期时序图

```
时间轴 →

[DLL 加载时]
    │
    │  FBind 静态构造函数执行 (全局对象初始化)
    │  ├─ Bind_AActor.cpp:         RegisterBinds(EOrder::Late, lambda_A)
    │  ├─ Bind_BlueprintType.cpp:  RegisterBinds(EOrder::Late, lambda_B)
    │  ├─ AS_FunctionTable_000:    RegisterBinds(EOrder::Late+50, lambda_C)
    │  └─ ... (数百个 FBind)
    │         │
    │         └─→ GetBindArray() 收集所有 lambda (不执行)
    │                               BindState 尚不存在
    │
    v
[引擎初始化]
    │
    │  FAngelscriptRuntimeModule::StartupModule()
    │  └─ InitializeAngelscript()
    │     └─ FAngelscriptEngine::Initialize()
    │        │
    │        ├─ [1] asCreateScriptEngine()        → 创建 asCScriptEngine
    │        │
    │        ├─ [2] EnsureSharedStateCreated()
    │        │       └─ BindState = MakeUnique<FAngelscriptBindState>()
    │        │          ★ BindState 此时诞生, ClassFuncMaps 为空
    │        │
    │        ├─ [3] BindScriptTypes() → CallBinds()
    │        │       │
    │        │       ├─ GetSortedBindArray()       → 排序所有 FBind
    │        │       │
    │        │       ├─ [EOrder::Early] 基础类型绑定
    │        │       │   └─ ReferenceClass("AActor", AActor::StaticClass())
    │        │       │       └─ asCScriptEngine::RegisterObjectType("AActor", ...)
    │        │       │          ★ 写入 AS 引擎: 注册类型
    │        │       │
    │        │       ├─ [EOrder::Normal] 标准手写绑定
    │        │       │   └─ Bind_AActor.cpp lambda:
    │        │       │       auto AActor_ = ExistingClass("AActor");
    │        │       │       AActor_.Method("FVector GetActorLocation() const", ...)
    │        │       │       └─ asCScriptEngine::RegisterObjectMethod(...)
    │        │       │          ★ 写入 AS 引擎: 注册手写方法
    │        │       │
    │        │       ├─ [EOrder::Late] Bind_BlueprintType lambda:
    │        │       │   └─ 遍历所有 UClass
    │        │       │       └─ BindBlueprintCallable(InType, Function, ...)
    │        │       │           ├─ ClassFuncMaps.Find(Class, FuncName)
    │        │       │           │   ★ 读取 BindState (此时已被 UHT 分片填充)
    │        │       │           ├─ [有直接绑定]
    │        │       │           │   └─ BindMethodDirect(Sig, FuncPtr, Caller)
    │        │       │           │       └─ asCScriptEngine::RegisterObjectMethod(...)
    │        │       │           │          ★ 写入 AS 引擎: 直接绑定
    │        │       │           └─ [无直接绑定]
    │        │       │               └─ BindReflectiveFallback(...)
    │        │       │                   └─ asCScriptEngine::RegisterObjectMethod(
    │        │       │                       ..., genericDispatcher, asCALL_GENERIC)
    │        │       │                      ★ 写入 AS 引擎: 反射回退
    │        │       │
    │        │       └─ [EOrder::Late+50] UHT AS_FunctionTable_* lambda:
    │        │           └─ AddFunctionEntry(Class, Name, FFuncEntry)
    │        │               └─ BindState.ClassFuncMaps[Class][Name] = FFuncEntry
    │        │                  ★ 只写入 BindState, 不触及 AS 引擎
    │        │                  (等待后续 Bind_BlueprintType 消费)
    │        │
    │        │  ★ 注意执行顺序:
    │        │    EOrder::Late (=100) 的 Bind_BlueprintType 先于
    │        │    EOrder::Late+50 (=150) 的 UHT 分片
    │        │    但手写 AddFunctionEntry (EOrder::Late 内)
    │        │    也先于 UHT 分片 (EOrder::Late+50)
    │        │    → 手写条目首次写入优先, UHT 条目不会覆盖
    │        │
    │        ├─ [4] InitialCompile()
    │        │       └─ 编译 .as 脚本文件
    │        │          脚本中调用 "AActor.GetActorLocation()"
    │        │          └─ asCScriptEngine 查找已注册的方法
    │        │             └─ 找到 → 编译成功
    │        │
    │        └─ [5] PostInitialize_GameThread()
    │                └─ 广播 OnInitialCompileFinished
    │
    v
[运行时]
    │
    │  脚本执行 actor.GetActorLocation()
    │  └─ asIScriptContext::Execute()
    │      └─ asCScriptFunction (已注册的系统函数)
    │          ├─ [直接绑定] asCALL_THISCALL
    │          │   └─ 通过 asSFuncPtr 直接调用 AActor::GetActorLocation
    │          │      (零反射, 原生 ABI 速度)
    │          │
    │          └─ [反射回退] asCALL_GENERIC
    │              └─ GenericDispatcher(asIScriptGeneric*)
    │                  └─ UObject::ProcessEvent(UFunction*, Params)
    │                     (通过 UE 反射系统间接调用)
    │
    v
[销毁]
    │
    │  FAngelscriptEngine::Shutdown()
    │  ├─ SharedState->ScriptEngine->ShutDownAndRelease()
    │  │   └─ asCScriptEngine 销毁 (所有注册的类型/方法/属性一起释放)
    │  └─ SharedState->BindState.Reset()
    │      └─ FAngelscriptBindState 销毁
    │         └─ ClassFuncMaps / RuntimeClassDB / SkipBinds 等全部释放
    │
    v
  [结束]
```

### 11.8 关键设计要点

#### BindState 是中间层, 不是最终存储

```
BindState (ClassFuncMaps)           asCScriptEngine (内部类型系统)
    │                                    │
    │  中间查找表                         │  最终存储
    │  FString/FName → FFuncEntry        │  asITypeInfo → asCScriptFunction
    │  生命周期: 引擎初始化期间填充        │  生命周期: 引擎全生命周期
    │  用途: BindBlueprintCallable 消费   │  用途: 脚本编译 + 执行
    │                                    │
    │  ★ 初始化完成后, ClassFuncMaps      │  ★ 所有注册信息已内化到
    │    不再被读取 (理论上可释放)         │    asCScriptEngine 的类型表中
    │                                    │
    └──────── 单向流入 ───────────→──────┘
              (RegisterObjectMethod 等 API)
```

#### PreviouslyBoundFunction 串行耦合机制

```cpp
// 这是并行化的核心障碍

// 绑定一个方法:
int FunctionId = Engine->RegisterObjectMethod(className, signature, funcPtr, ...);
OnBind(FunctionId, ...);
    └─ GetPreviouslyBoundFunctionRef() = FunctionId;  // ← 存入 BindState

// 紧接着的链式配置:
FAngelscriptBinds::SetPreviousBindIsEditorOnly(true);
    └─ auto* Function = (asCScriptFunction*)Engine->GetFunctionById(
           GetPreviouslyBoundFunctionRef());     // ← 从 BindState 读取
       Function->traits.SetTrait(asTRAIT_EDITOR_ONLY, true);
                                                 // ← 修改 AS 引擎内部状态

// 这个 "绑定 → 配置" 的两步模式要求严格串行:
//   如果两个线程同时绑定, PreviouslyBoundFunction 会被覆盖
//   导致链式配置修改错误的函数
```

#### 多引擎实例隔离

```
FAngelscriptEngine 支持多实例 (测试、Clone):

Engine A (Primary)                    Engine B (Clone/Test)
    │                                     │
    ├─ SharedState A                      ├─ SharedState B (独立)
    │   ├─ ScriptEngine A                 │   ├─ ScriptEngine B (独立)
    │   └─ BindState A                    │   └─ BindState B (独立)
    │                                     │
    └─ Push 到 ContextStack               └─ Push 到 ContextStack

GetBindState() 通过 TryGetCurrentEngine() 获取栈顶引擎的 BindState
→ 不同引擎实例的 BindState 完全隔离
→ ClassFuncMaps 不会在引擎间串扰

LegacyBindState (静态局部变量):
    当 ContextStack 为空时的回退
    用于 DLL 加载时 FBind 构造函数中的极早期访问
```

---

## 12. 其他 UE 脚本方案的函数绑定与地址管理对比

> 基于 `Reference/` 目录下的 4 个第三方脚本插件源码分析。
> 对比维度: 函数发现方式、函数地址存储、注册时机、调用桥接机制。

### 12.1 总览对比表

```
                Angelscript (我们)       UnrealCSharp           UnLua              puerts             sluaunreal
语言            AngelScript             C#                     Lua                TypeScript/JS       Lua
引擎修改        零                       零                     零                  零                  零

函数发现        UHT 编译时 + 运行时      运行时反射扫描         运行时 FindFunction  运行时 TFieldIterator 运行时 FindFunction
                遍历 UClass                                    By Name             遍历 UClass          By Name

注册时机        ★ 启动时全量注册        启动时种子哈希表       ★ 懒加载            ★ 懒加载             ★ 懒加载
                (CallBinds 串行)        + 首次调用懒创建       (首次访问时)         (首次访问类时)       (首次访问时)

函数指针存储    FFuncEntry              无 (TWeakObjectPtr     TWeakObjectPtr      TWeakObjectPtr      FindFunctionByName
                (FGenericFuncPtr        <UFunction>)           <UFunction>         <UFunction>         (每次查找) +
                + FunctionCaller)                                                                     LuaFunctionAccelerator
                                                                                                      缓存

调用方式        直接 ABI / 反射回退     FFI P/Invoke →         Lua thunk →         FastCall (FFrame) / CppBinding 模板 /
                                       UFunction::Invoke      ProcessEvent         SlowCall            ProcessEvent
                                                                                  (ProcessEvent)

参数编组        编译时模板生成           stackalloc 缓冲区      预分配 ParamBuffer  PropertyTranslator  LuaFunctionAccelerator
                void** 参数数组          + 22 个 FFI 入口       + FPropertyDesc    双向转换器            缓存 Checker/Pusher

启动开销        ★ 100-500ms            种子哈希表 ~ms          ~0 (纯懒加载)       ~0 (纯懒加载)        ~0 (静态注册极少)
                (全量注册)              (轻量种子)
```

### 12.2 UnrealCSharp: 三层哈希 + 懒初始化

```
架构: 轻量种子 → 首次调用时懒创建描述符 → 缓存

[启动时]
FCSharpEnvironment::Initialize()
    └─ 运行时反射扫描所有 UClass
        └─ 对每个 BlueprintCallable UFunction:
            hash = GetTypeHash(UFunction*)
            CSharpFunctionHashMap.Add(hash, {ClassDesc*, UFunction*, RegisterInfo})
            ★ 只存元数据元组, 不创建描述符 (轻量)

[首次调用时]
C# 调用 UE 函数 → P/Invoke → GenericCall()
    └─ GetOrAddFunctionDescriptor<FCSharpFunctionDescriptor>(hash)
        ├─ FunctionDescriptorMap.Find(hash) → 命中 → 返回缓存
        └─ 未命中:
            CSharpFunctionHashMap.Find(hash) → 取出元组
            创建 FFunctionDescriptor (参数描述符, 缓冲区布局)
            FunctionDescriptorMap.Add(hash, descriptor) → 缓存
            CSharpFunctionHashMap.Remove(hash) → 释放种子
            ★ 种子 → 描述符: 一次性迁移

[数据结构]
FFunctionDescriptor:
    TWeakObjectPtr<UFunction> Function     // GC 安全的弱引用
    TArray<FPropertyDescriptor*> Params    // 参数描述
    // ★ 不存储原生函数指针! 通过 UFunction::Invoke() 调用

[调用桥接]
C# → P/Invoke (22 个入口: Call0~Call26, 按参数数量分) →
    解析 GC handle → FFunctionDescriptor →
    UFunction::Invoke(Object, Stack, Result)
    ★ 始终走 UE 反射路径, 无直接函数指针
```

**关键特点:**
- **不存储原生函数指针** — 始终通过 `UFunction::Invoke()` 调用
- **种子→描述符迁移** — 启动时只存哈希, 首次调用时才创建描述符并释放种子
- **22 个 FFI 入口** — 按参数数量编译时特化, 避免运行时参数数量判断

### 12.3 UnLua: 纯懒加载 + 弱引用描述符

```
架构: 零启动注册 → 首次访问类时创建描述符 → 弱引用 + 缓冲池

[启动时]
    ★ 无全量注册, 无函数扫描, 启动开销 ~0

[首次访问函数时]
Lua 调用 UObject:Method()
    └─ FFunctionRegistry::Invoke()
        └─ 查找/创建 FFunctionDesc:
            AsClass()->FindFunctionByName(FuncName)  ← UE 反射查找
            创建 FFunctionDesc:
                TWeakObjectPtr<UFunction> Function   ← GC 安全弱引用
                TArray<FPropertyDesc> Params          ← 参数元数据
                TSharedPtr<FParamBufferAllocator> Buffer ← 预分配参数缓冲
            缓存到 FClassDesc.Functions

[数据结构]
FFunctionDesc:
    TWeakObjectPtr<UFunction> Function     // ★ 弱引用, 非裸指针
    TArray<FPropertyDesc> Params
    TSharedPtr<FParamBufferAllocator> Buffer // 参数缓冲池 (避免每次 malloc)
    TArray<int32> OutPropertyIndices        // out/ref 参数索引

FClassDesc:
    TMap<FName, FFieldDesc> Fields          // 懒加载字段缓存
    TArray<FFunctionDesc> Functions         // 已缓存的函数描述符

[调用桥接]
Lua → ULuaFunction::execCallLua() (FUNC_Native thunk)
    → FFunctionRegistry::Invoke()
        → FFunctionDesc::CallLua()
            → PreCall(): Lua args → 参数缓冲区 (FProperty 编组)
            → UObject::ProcessEvent(Function, Buffer)
            → PostCall(): 参数缓冲区 → Lua 返回值
    ★ 始终走 ProcessEvent, 无直接函数指针
    ★ 参数缓冲区复用, 无逐次分配
```

**关键特点:**
- **零启动开销** — 完全按需发现, 不预扫描
- **TWeakObjectPtr** — 函数引用随 GC 自动失效, 无悬垂指针风险
- **缓冲区池** — `FParamBufferAllocator` 按签名预分配, 跨调用复用

### 12.4 puerts: 按类懒加载 + 双路径调用

```
架构: 零启动注册 → 首次访问类时遍历该类所有函数 → 缓存 FunctionTemplate

[启动时]
    ★ 无全量注册, 无函数扫描, 启动开销 ~0

[首次访问 UClass 时]
JS 访问 UObject
    → FindOrAddCppObject()
        → GetTemplateOfClass()
            → FStructWrapper::InitTemplateProperties()
                └─ TFieldIterator<UFunction>(UClass) 遍历该类所有函数
                    └─ 对每个函数创建 FFunctionTranslator:
                        TWeakObjectPtr<UFunction> Function
                        vector<unique_ptr<FPropertyTranslator>> Arguments
                        unique_ptr<FPropertyTranslator> Return
                        uint32 ParamsBufferSize
                    └─ 存入:
                        MethodsMap[FName] = shared_ptr<FFunctionTranslator>   (实例方法)
                        FunctionsMap[FName] = shared_ptr<FFunctionTranslator> (静态方法)

[数据结构]
FFunctionTranslator:
    TWeakObjectPtr<UFunction> Function     // ★ 弱引用
    TWeakObjectPtr<UObject> BindObject     // 静态函数的绑定对象 (延迟初始化)
    vector<unique_ptr<FPropertyTranslator>> Arguments  // 参数双向转换器
    unique_ptr<FPropertyTranslator> Return
    uint32 ParamsBufferSize                // FFrame 栈大小

FStructWrapper:
    TMap<FName, shared_ptr<FFunctionTranslator>> MethodsMap    // 实例方法
    TMap<FName, shared_ptr<FFunctionTranslator>> FunctionsMap  // 静态方法

[调用桥接 — 双路径]
V8 调用 → FFunctionTranslator::Call()
    │
    ├─ [FastCall] 原生函数 (FUNC_Native):
    │   手动构建 FFrame (参数栈)
    │   CallFunction->Invoke(Object, Stack, ReturnAddr)
    │   ★ 直接 Invoke, 跳过 ProcessEvent 前置检查
    │
    └─ [SlowCall] Blueprint/RPC 函数:
        CallObject->ProcessEvent(Function, Params)
        ★ 标准反射路径
```

**关键特点:**
- **按类批量加载** — 首次访问某 UClass 时遍历其所有函数, 后续 O(1) 查找
- **FastCall 优化** — 原生函数直接 `Invoke()`, 跳过 `ProcessEvent` 的参数验证/RPC 检查
- **PropertyTranslator** — 每个参数类型有专用的 JS↔UE 转换器, 避免运行时类型判断

### 12.5 sluaunreal: 静态导出 + 动态反射双层

```
架构: 静态 CppBinding 模板 (编译时) + 动态反射懒加载 (运行时)

[启动时 — 静态层]
DLL 加载 → LuaClass 静态构造 → luaclasses->Add(setupFunc)
LuaState 创建 → LuaClass::reg(L) → 遍历 luaclasses 执行所有 setupFunc
    └─ DefLuaClass(UMyClass)
        ├─ DefLuaMethod("MyFunc", &UMyClass::MyFunc)
        │   └─ LuaCppBinding<decltype(&UMyClass::MyFunc), &UMyClass::MyFunc>::LuaCFunction
        │      ★ 函数指针作为模板非类型参数, 编译时硬编码
        │      ★ 零运行时查找, 直接调用
        └─ addMethod(L, "MyFunc", LuaCFunction)
            └─ lua_pushcfunction(L, LuaCFunction) → Lua 表

[运行时 — 动态层]
Lua 访问未静态导出的 UObject 成员:
    └─ LuaObject::objectIndex(L)
        └─ cls->FindFunctionByName(name)  ← UE 反射懒查找
            └─ LuaFunctionAccelerator::create(UFunction)
                TMap<UFunction*, LuaFunctionAccelerator*> cache ← 全局缓存
                ├─ TArray<FCheckerInfo> paramsChecker   // 参数类型检查器
                ├─ TArray<FPusherInfo> outPropsPusher   // 输出参数编组器
                └─ 缓存后后续调用 O(1) 查找

[数据结构 — 静态层]
template<typename T, T FuncPtr, int Offset>
struct LuaCppBinding {
    static int LuaCFunction(lua_State* L) {
        // ★ FuncPtr 是编译时常量, 直接内联调用
        // 参数从 Lua 栈读取, 类型安全的模板展开
        auto* obj = LuaObject::checkValue<OwnerClass>(L, 1);
        auto result = (obj->*FuncPtr)(readArg<Arg1>(L, 2), ...);
        LuaObject::push(L, result);
        return 1;
    }
};

[数据结构 — 动态层]
LuaFunctionAccelerator:
    // ★ 不存储函数指针, 通过 UFunction 反射调用
    TArray<FCheckerInfo> paramsChecker     // 预计算的参数校验
    TArray<FPusherInfo> outPropsPusher     // 预计算的输出编组
    // 调用: func->Invoke(obj, params)

[调用桥接]
静态路径: Lua → lua_CFunction → LuaCppBinding::LuaCFunction
          → (obj->*FuncPtr)(args...)        ★ 直接 ABI 调用, 零反射

动态路径: Lua → objectIndex → FindFunctionByName → LuaFunctionAccelerator
          → UFunction::Invoke(obj, params)  ★ 反射调用, 有缓存
```

**关键特点:**
- **双层模型** — 静态导出 (编译时函数指针) + 动态反射 (运行时懒加载)
- **模板非类型参数** — `LuaCppBinding<decltype(&Func), &Func>` 在编译时硬编码函数地址, 与 UEAS2 的 `ERASE_METHOD_PTR` 思路一致
- **动态层零启动开销** — `FindFunctionByName` 按需查找, `LuaFunctionAccelerator` 缓存元数据

### 12.6 核心设计差异分析

```
函数地址管理策略频谱:

完全静态 ←──────────────────────────────────→ 完全动态

UEAS2        sluaunreal    Angelscript     puerts    UnLua    UnrealCSharp
(.gen.cpp    (CppBinding   (FFuncEntry     (Weak     (Weak    (Weak
 嵌入)        模板)         查找表)         Ptr)      Ptr)     Ptr +
                                                              Hash)
  │              │              │              │        │        │
  │              │              │              │        │        │
  ▼              ▼              ▼              ▼        ▼        ▼
编译时确定    编译时确定    UHT 生成时     首次访问   首次访问  首次调用
地址嵌入      模板参数      恢复并缓存     类时发现   时发现    时创建
.gen.cpp      + 直接调用    + 类型擦除     + 缓存     + 缓存    描述符

启动成本:    启动成本:    启动成本:       启动成本:  启动成本: 启动成本:
  零            极低        100-500ms       零          零        低
```

```
为什么其他方案不需要启动时全量注册?

UnLua / puerts / sluaunreal / UnrealCSharp 共同特征:
    1. 脚本 VM 不要求预先知道所有可用类型
       → Lua/JS/C# VM 可以动态发现和调用函数
       → 无需启动时注册完整类型系统

    2. 使用 TWeakObjectPtr<UFunction> 或 FindFunctionByName
       → 通过 UE 反射在运行时查找, 不预存函数指针
       → 调用时才解析, 分摊到每次调用

    3. 调用始终走 ProcessEvent 或 UFunction::Invoke
       → 不需要类型擦除的函数指针 (FFuncEntry/FGenericFuncPtr)
       → 反射系统本身就是调用桥接

AngelScript 的特殊约束:
    1. AS 编译器要求编译前完整注册类型系统
       → 类、方法、属性必须在脚本编译之前全部注册到 asCScriptEngine
       → 不能懒加载: 如果方法未注册, 编译器会报"未知方法"

    2. AS 引擎使用自己的函数表而非 UE 反射
       → 注册时需要提供 asSFuncPtr (AS 格式的函数指针)
       → 或者 asCALL_GENERIC + 分派器
       → 不能直接用 UFunction*, 必须转换为 AS 可用的格式

    3. 直接绑定路径需要类型擦除的函数指针
       → ERASE_METHOD_PTR 生成 FGenericFuncPtr
       → 比 ProcessEvent 快 10-100x (零反射开销)
       → 这是 AngelScript 的性能优势, 但启动时需要预注册
```

### 12.7 其他方案可借鉴的思路

```
思路                          来源              可行性      说明

懒加载发现                    UnLua/puerts       ❌ 不可行   AS 编译器要求预注册
TWeakObjectPtr 替代裸指针     所有方案            ⚠ 部分     ClassFuncMaps 生命周期已受控,
                                                            但 ForEachFunctionEntry 可考虑
种子→描述符迁移               UnrealCSharp       ✅ 可借鉴   启动时只存轻量种子 (UClass*+FName),
                                                            消费时才创建 FFuncEntry
参数缓冲区池                  UnLua              ✅ 可借鉴   反射回退路径的参数编组可复用缓冲
FastCall 双路径               puerts             ✅ 已有     我们的直接绑定 = FastCall,
                                                            反射回退 = SlowCall
模板非类型参数                sluaunreal/UEAS2   ✅ 已使用   ERASE_METHOD_PTR 已使用此技术
按类批量加载                  puerts             ✅ 可借鉴   P5 方案的按 Class 分组与此一致

最有价值的借鉴:
    UnrealCSharp 的 "种子→描述符" 模式:
        启动时: 只存 {UClass*, FName, 轻量标记} 到种子表 (~16 bytes/条)
        消费时: Bind_BlueprintType 遍历到该函数时, 才从种子表取出并创建 FFuncEntry
        → 减少启动时的 FFuncEntry 构造和 TMap 插入开销
        → 与我们的 P3 (跳过桩条目) + P4 (静态数组) 组合效果类似
```

---

## 13. Reference 脚本方案的 UFunction 调用分派机制对比

本节聚焦于 UFunction 的 **运行时调用路径** — 即脚本 VM 调用 UE C++ 函数时, 从脚本侧发起
到最终执行 C++ 函数体的全链路分析, 包括参数编组、分派策略、性能优化手段。

### 13.1 UE 原生的三种调用路径

```
对比基准 — UE 原生调用路径:

路径 A: ProcessEvent (最重/最通用)
┌─────────────┐    ┌─────────────────┐    ┌────────────────┐    ┌──────────────┐
│ 调用者      │───→│ UObject::       │───→│ PreScript /    │───→│ UFunction::  │
│             │    │ ProcessEvent()  │    │ RPC 检查 /     │    │ Invoke()     │
│             │    │                 │    │ Multicast 分发 │    │              │
└─────────────┘    └─────────────────┘    └────────────────┘    └──────┬───────┘
                                                                       │
                                                                       ▼
                                                                ┌──────────────┐
                                                                │ FFrame 构造  │
                                                                │ + Func->Func │
                                                                │ (NativeFunc) │
                                                                └──────────────┘

路径 B: UFunction::Invoke (中等/跳过 ProcessEvent 前置检查)
┌─────────────┐    ┌──────────────┐    ┌──────────────┐
│ 调用者      │───→│ UFunction::  │───→│ FFrame 构造  │
│             │    │ Invoke()     │    │ + NativeFunc │
└─────────────┘    └──────────────┘    └──────────────┘

路径 C: 直接 ABI 调用 (最轻/编译期绑定)
┌─────────────┐    ┌──────────────────────┐
│ 调用者      │───→│ 函数指针直接调用     │
│             │    │ (零反射开销)         │
└─────────────┘    └──────────────────────┘

ProcessEvent 的开销:
    1. FindFunctionChecked (FName 哈希查找)
    2. ShouldCallFunction 权限检查
    3. CallRemoteFunction (RPC 判定)
    4. 蓝图 Multicast 分发
    5. FFrame 分配 + 初始化
    6. 参数从 Parms buffer 拷贝到 FFrame::Locals
    7. 最终才到 NativeFunc / Script bytecode 执行
```

### 13.2 UnrealCSharp — 双路径分级调度

```
架构概览:

    C# 侧                                  C++ 侧
    ┌──────────────┐                        ┌────────────────────────────┐
    │ 生成的绑定类 │───[P/Invoke]──────────→│ 22 个 Call 方法            │
    │ UObject.XXX()│                        │ Call0 ~ Call26             │
    └──────────────┘                        └───────────┬────────────────┘
                                                        │
                                          ┌─────────────┴──────────────┐
                                          │                            │
                                          ▼                            ▼
                                ┌──────────────────┐      ┌────────────────────┐
                                │ 路径 A: 低编号    │      │ 路径 B: 高编号      │
                                │ Call0 ~ Call7     │      │ Call8 ~ Call26      │
                                │ (ProcessEvent)    │      │ (FFrame + Invoke)   │
                                └──────────────────┘      └────────────────────┘

路径 A: ProcessEvent 路径 (Call0 ~ Call7)
──────────────────────────────────────────
    适用: 参数少、简单类型、需要 RPC/蓝图兼容

    C# 调用 → P/Invoke → Call<N>(InObject, UFunction*)
       │
       ▼
    分配参数缓冲区 (InBuffer)
       │
       ▼
    将 C# 参数逐个编组到 InBuffer
    (MonoObject* → FProperty 格式)
       │
       ▼
    InObject->ProcessEvent(UFunction*, InBuffer)
       │
       ▼
    从 OutBuffer 提取返回值和 out 参数
       │
       ▼
    编组回 C# (FProperty 格式 → MonoObject*)

路径 B: 直接 FFrame 路径 (Call8 ~ Call26)
──────────────────────────────────────────
    适用: 参数多、性能敏感路径

    C# 调用 → P/Invoke → Call<N>(InObject, UFunction*, Params...)
       │
       ▼
    手动构造 FFrame:
        FFrame NewStack(InObject, UFunction*)
        NewStack.Locals = 参数缓冲区
       │
       ▼
    逐参数写入 FFrame.Locals:
        FProperty->CopyCompleteValue(Locals + Offset, &Param)
       │
       ▼
    UFunction->Invoke(InObject, NewStack, ReturnValueAddress)
       │    ↑ 跳过 ProcessEvent 的全部前置检查
       │
       ▼
    从 NewStack.OutParms 链提取 out 参数
       │
       ▼
    返回值从 ReturnValueAddress 编组回 C#

关键优化:
    1. FunctionDescriptor 缓存: 按函数 hash 缓存 {UFunction*, 参数偏移表, 类型标记}
    2. 三缓冲区设计: InBuffer / OutBuffer / ReturnBuffer 分离
    3. 22 个 Call 变体: 按参数数量特化, 避免可变参数开销
    4. 描述符按需创建: 首次调用时才从 UFunction 元数据生成描述符
```

### 13.3 UnLua — 统一 ProcessEvent 路径

```
架构概览:

    Lua 侧                                 C++ 侧
    ┌──────────────┐                        ┌────────────────────────────┐
    │ Obj:Func()   │───[lua_CFunction]─────→│ FFunctionDesc::CallUE()   │
    │              │                        │ (统一入口)                 │
    └──────────────┘                        └───────────┬────────────────┘
                                                        │
                                                        ▼
                                            ┌────────────────────────┐
                                            │ 始终 ProcessEvent      │
                                            │ (无 FastCall 路径)     │
                                            └────────────────────────┘

调用链详解:

    Lua 栈: [self, arg1, arg2, ...]
       │
       ▼
    lua_CFunction 入口 (自动生成的 thunk)
       │
       ▼
    FFunctionDesc::CallUE(lua_State*, int32 NumParams, int32 FirstParamIndex)
       │
       ├──→ PreCall(): Lua 参数 → C++ 参数缓冲区
       │        │
       │        ▼
       │    FParamBufferAllocator::Alloc(ParamsSize)  ← 缓冲区池复用
       │        │
       │        ▼
       │    for each FPropertyDesc:
       │        读取 Lua 栈 → FProperty->InitializeValue → 写入 Buffer
       │
       ├──→ 实际调用:
       │        │
       │        ├─ 接口类型?
       │        │     └─ 查找实际 UObject, 解析正确的 UFunction
       │        │
       │        ├─ RPC 函数?
       │        │     └─ CallRemoteFunction (不走 ProcessEvent 全路径)
       │        │
       │        └─ 普通函数:
       │              UObject->ProcessEvent(UFunction*, ParamBuffer)
       │
       └──→ PostCall(): C++ 结果 → Lua 返回值
                │
                ▼
            for each out/return FPropertyDesc:
                从 Buffer 读取 → 推入 Lua 栈
                │
                ▼
            FParamBufferAllocator::Free(Buffer)  ← 归还缓冲池

反向路径 (UE 调用 Lua 脚本):

    UFunction (FUNC_Native, 由 ULuaFunction 创建)
       │
       ▼
    ULuaFunction::execCallLua (注册为 NativeFunc thunk)
       │
       ▼
    FFunctionRegistry::Invoke(lua_State*, FFrame&)
       │
       ▼
    从 FFrame.Locals 提取参数 → 推入 Lua 栈
       │
       ▼
    lua_pcall(L, nArgs, nResults, errHandler)
       │
       ▼
    Lua 函数执行 → 返回值写回 FFrame.OutParms

关键特性:
    1. FParamBufferAllocator: TLS 缓冲池, 避免每次调用都 malloc
       ┌─────────────────────────────────────┐
       │ ThreadLocal Pool                     │
       │ ┌────┐ ┌────┐ ┌────┐                │
       │ │256B│ │512B│ │1KB │ ...             │
       │ └────┘ └────┘ └────┘                │
       │ Alloc: 从 >= ParamsSize 的桶取       │
       │ Free:  归还到对应桶                  │
       └─────────────────────────────────────┘

    2. FFunctionDesc 缓存: 每个绑定的 UFunction 只创建一次描述符
       - 参数偏移、大小、类型信息预计算
       - 避免每次调用都遍历 UFunction->ChildProperties

    3. 接口函数特殊处理:
       - 接口类型参数先解析为 UObject*
       - 再从 UObject 的 Class 查找正确的 UFunction 实例

    4. 无 FastCall 优化:
       - 所有调用统一走 ProcessEvent
       - 优势: 代码路径单一, RPC/蓝图/网络复制全部兼容
       - 劣势: 无法绕过 ProcessEvent 的前置检查开销
```

### 13.4 puerts — FastCall / SlowCall 双路径

```
架构概览:

    JavaScript 侧                           C++ 侧
    ┌──────────────┐                        ┌────────────────────────────┐
    │ obj.Func()   │───[V8 Binding]────────→│ PropertyTranslator 入口   │
    │              │                        │                            │
    └──────────────┘                        └───────────┬────────────────┘
                                                        │
                                          ┌─────────────┴──────────────┐
                                          │                            │
                                          ▼                            ▼
                                ┌──────────────────┐      ┌────────────────────┐
                                │ FastCall 路径     │      │ SlowCall 路径      │
                                │ FUNC_Native      │      │ 蓝图/RPC 函数      │
                                │ && !Net 函数     │      │ ProcessEvent       │
                                └──────────────────┘      └────────────────────┘

FastCall 路径 (核心优化):
──────────────────────────
    判定条件:
        UFunction->HasAnyFunctionFlags(FUNC_Native)    ← 是原生 C++ 函数
        && !UFunction->HasAnyFunctionFlags(FUNC_Net)   ← 不是网络函数

    V8 回调 → CFunctionInfoByPtrBridge::Call()
       │
       ▼
    分配参数缓冲区 (FFrame::Locals 大小)
       │
       ▼
    手动构造 FFrame:
        FFrame NewStack(Obj, UFunction*);
        NewStack.Code = nullptr;       ← 关键! 标记为原生调用
        NewStack.Locals = Buffer;
       │
       ▼
    PropertyTranslator 逐参数编组:
        for each param:
            Translator->JsToUEInContainer(Isolate, Context, v8Value, Buffer, false)
            // JS Value → 写入 Buffer 对应偏移
       │
       ▼
    预构建 FOutParmRec 链:
        ┌──────────┐    ┌──────────┐    ┌──────────┐
        │ OutParm0 │───→│ OutParm1 │───→│ OutParm2 │──→ nullptr
        │ .Prop    │    │ .Prop    │    │ .Prop    │
        │ .Addr    │    │ .Addr    │    │ .Addr    │
        └──────────┘    └──────────┘    └──────────┘
        NewStack.OutParms = &OutParm0;
       │
       ▼
    UFunction->Invoke(Obj, NewStack, ReturnValueAddress)
       │    ↑ 直接调用! 绕过 ProcessEvent 全部前置逻辑:
       │    │   - 无 FindFunctionChecked
       │    │   - 无 ShouldCallFunction 权限检查
       │    │   - 无 CallRemoteFunction RPC 判定
       │    │   - 无蓝图 Multicast 分发
       │
       ▼
    PropertyTranslator 提取返回值和 out 参数:
        Translator->UEToJsInContainer(Isolate, Context, Buffer, false)
       │
       ▼
    返回 v8::Value 到 JavaScript

SlowCall 路径:
──────────────────
    适用: FUNC_BlueprintEvent / FUNC_Net / 非 FUNC_Native

    V8 回调 → CFunctionInfoByPtrBridge::Call()
       │
       ▼
    分配参数缓冲区
       │
       ▼
    PropertyTranslator 逐参数编组 (同 FastCall)
       │
       ▼
    Obj->ProcessEvent(UFunction*, Buffer)
       │    ↑ 走完整 ProcessEvent 流程
       │
       ▼
    PropertyTranslator 提取返回值 (同 FastCall)

关键优化:
    1. Code = nullptr 技巧:
       FFrame 构造时 Code 设为 nullptr
       → UFunction::Invoke 识别为原生调用
       → 直接跳转到 Func->Func (NativeFunc 指针)
       → 避免 Script bytecode 解释器介入

    2. FOutParmRec 链预构建:
       - 在首次调用时构建 out 参数链
       - 后续调用复用, 避免每次遍历 UFunction->ChildProperties

    3. PropertyTranslator 特化:
       ┌──────────────────────────────────────────┐
       │ FProperty 类型     → Translator 实例      │
       ├──────────────────────────────────────────┤
       │ FIntProperty       → FInt32Translator     │
       │ FFloatProperty     → FFloatTranslator     │
       │ FBoolProperty      → FBoolTranslator      │
       │ FStrProperty       → FStringTranslator    │
       │ FObjectProperty    → FObjectTranslator    │
       │ FStructProperty    → FStructTranslator    │
       │ FArrayProperty     → FArrayTranslator     │
       │ FMapProperty       → FMapTranslator       │
       │ ...                                       │
       └──────────────────────────────────────────┘
       每种 FProperty 类型有专用 Translator, 内联 JS↔UE 转换逻辑

    4. 静态函数识别:
       - FUNC_Static → 不需要 this 指针
       - 第一个参数不从 JS this 取, 全从参数列表取
```

### 13.5 sluaunreal — 三路径架构

```
架构概览:

    Lua 侧                                 C++ 侧
    ┌──────────────┐
    │ 调用方式     │
    └──────┬───────┘
           │
     ┌─────┴──────┬─────────────────┐
     ▼            ▼                 ▼
┌─────────┐  ┌──────────┐  ┌──────────────┐
│ 路径 A  │  │ 路径 B   │  │ 路径 C       │
│ 静态    │  │ 动态     │  │ Override     │
│ 绑定    │  │ 反射     │  │ (UE→Lua)    │
└─────────┘  └──────────┘  └──────────────┘

路径 A: 静态绑定 (LuaCppBinding 模板)
──────────────────────────────────────
    编译期特化, 零反射, 直接 ABI 调用

    Lua 调用 → lua_CFunction
       │
       ▼
    LuaCppBinding<decltype(&UClass::Method), &UClass::Method>::LuaCFunc
       │
       ▼
    模板参数解包:
        template<typename Ret, typename Cls, typename... Args,
                 Ret(Cls::*Method)(Args...)>
        static int LuaCFunc(lua_State* L) {
            Cls* Self = LuaObject::checkValue<Cls*>(L, 1);
            // 逐参数从 Lua 栈提取:
            auto arg0 = LuaObject::checkValue<Arg0>(L, 2);
            auto arg1 = LuaObject::checkValue<Arg1>(L, 3);
            ...
            // 直接调用 C++ 方法 (编译期函数指针):
            Ret result = (Self->*Method)(arg0, arg1, ...);
            // 推入返回值:
            LuaObject::push(L, result);
            return 1;
        }
       │
       ▼
    无 ProcessEvent, 无 FFrame, 无 UFunction*
    完全等价于手写 C++ 调用

    性能: ≈ 原生 C++ 调用 + Lua 编组开销
           比 ProcessEvent 路径快 10-50x

路径 B: 动态反射 (LuaFunctionAccelerator)
──────────────────────────────────────────
    运行时绑定, 使用缓存的反射元数据

    Lua 调用 → lua_CFunction (通用 thunk)
       │
       ▼
    LuaFunctionAccelerator::call(lua_State* L)
       │
       ├──→ 参数缓冲区分配 (预计算大小)
       │
       ├──→ 逐参数编组 (使用缓存的 FProperty 元数据):
       │        for each CachedParam:
       │            param.prop->InitializeValue(Buffer + param.offset);
       │            readParam(L, param, Buffer);  // Lua → C++
       │
       ├──→ UFunction->Invoke(Obj, NewStack, ReturnAddr)
       │        ↑ 使用 Invoke 而非 ProcessEvent
       │        ↑ 手动构造 FFrame, Code = nullptr
       │
       └──→ 返回值/out 参数编组回 Lua:
                for each OutParam:
                    pushParam(L, param, Buffer);  // C++ → Lua

    关键: LuaFunctionAccelerator 的缓存结构:
        ┌─────────────────────────────────────┐
        │ struct LuaFunctionAccelerator       │
        │ ┌─────────────────────────────────┐ │
        │ │ UFunction* func                 │ │
        │ │ int paramsSize   (预计算)       │ │
        │ │ int returnOffset (预计算)       │ │
        │ │ TArray<CachedParam> params:     │ │
        │ │   ├─ FProperty* prop            │ │
        │ │   ├─ int offset                 │ │
        │ │   ├─ bool isOut                 │ │
        │ │   └─ bool isReturn              │ │
        │ └─────────────────────────────────┘ │
        └─────────────────────────────────────┘
        → 首次调用创建, 后续调用复用
        → 避免每次调用遍历 UFunction->ChildProperties

路径 C: LuaOverrider (UE 调用 Lua)
────────────────────────────────────
    用于蓝图事件/虚函数的 Lua 重写

    UE 调用 UFunction (FUNC_BlueprintEvent)
       │
       ▼
    UFunction->Func 已被 hook 为 LuaOverrider thunk
       │
       ▼
    LuaOverrider::luaOverrideFunc(UObject* Context, FFrame& Stack, void* Result)
       │
       ▼
    从 FFrame.Locals 提取参数 → 推入 Lua 栈
       │
       ▼
    lua_pcall(L, nArgs, nResults, errHandler)
       │
       ▼
    Lua 函数执行
       │
       ▼
    返回值从 Lua 栈写回 Result 指针
```

### 13.6 四方案调用分派对比总表

```
┌──────────────┬──────────────────┬──────────────────┬──────────────────┬──────────────────┐
│ 维度         │ UnrealCSharp     │ UnLua            │ puerts           │ sluaunreal       │
├──────────────┼──────────────────┼──────────────────┼──────────────────┼──────────────────┤
│ 快速路径     │ Call8-26:        │ 无               │ FastCall:        │ 静态绑定:        │
│              │ FFrame+Invoke    │ (统一ProcessEvent)│ FFrame+Invoke    │ 直接ABI调用      │
│              │                  │                  │ Code=nullptr     │ 零反射            │
├──────────────┼──────────────────┼──────────────────┼──────────────────┼──────────────────┤
│ 慢速路径     │ Call0-7:         │ FFunctionDesc::  │ SlowCall:        │ 动态反射:        │
│              │ ProcessEvent     │ CallUE()         │ ProcessEvent     │ Invoke+缓存元数据│
├──────────────┼──────────────────┼──────────────────┼──────────────────┼──────────────────┤
│ 路径选择依据 │ 参数数量         │ 无选择(单路径)  │ FUNC_Native &&   │ 是否有静态绑定   │
│              │ (编译期决定)     │                  │ !FUNC_Net        │ 模板注册         │
├──────────────┼──────────────────┼──────────────────┼──────────────────┼──────────────────┤
│ ProcessEvent │ Call0-7 使用     │ 始终使用         │ SlowCall 使用    │ 不使用           │
│ 使用情况     │ Call8+ 绕过      │                  │ FastCall 绕过    │ (Invoke 或直调)  │
├──────────────┼──────────────────┼──────────────────┼──────────────────┼──────────────────┤
│ 参数编组     │ 三缓冲区         │ FParamBuffer-    │ PropertyTrans-   │ 静态: 模板解包   │
│ 策略         │ In/Out/Return    │ Allocator 池     │ lator 特化       │ 动态: 缓存元数据 │
├──────────────┼──────────────────┼──────────────────┼──────────────────┼──────────────────┤
│ 缓存机制     │ FunctionDesc     │ FFunctionDesc    │ PropertyTrans-   │ LuaFunction-     │
│              │ (按hash缓存)     │ (每UFunc一个)    │ lator (每Prop)   │ Accelerator      │
├──────────────┼──────────────────┼──────────────────┼──────────────────┼──────────────────┤
│ RPC 兼容     │ ProcessEvent路径 │ 原生支持         │ SlowCall路径     │ Override路径     │
│              │ 自动处理         │ CallRemoteFunc   │ 自动处理         │ 自动处理         │
├──────────────┼──────────────────┼──────────────────┼──────────────────┼──────────────────┤
│ out 参数处理 │ OutBuffer 分离   │ PostCall 回写    │ FOutParmRec 链   │ CachedParam      │
│              │                  │                  │ (预构建)         │ isOut 标记       │
├──────────────┼──────────────────┼──────────────────┼──────────────────┼──────────────────┤
│ 快速路径     │ ≈ Invoke 级别    │ N/A              │ ≈ Invoke 级别    │ ≈ 原生 C++ 调用  │
│ 相对性能     │ (省 ProcessEvent │                  │ (省 ProcessEvent │ (省全部反射)     │
│              │  前置开销)       │                  │  前置开销)       │                  │
└──────────────┴──────────────────┴──────────────────┴──────────────────┴──────────────────┘
```

### 13.7 与 AngelScript 调用路径的对照

```
AngelScript 当前的调用路径:

路径 1: 直接绑定 (56.57% 的函数)
────────────────────────────────
    AS 脚本调用
       │
       ▼
    asCScriptEngine 查找注册的 asSFuncPtr
       │
       ▼
    asCALL_CDECL / asCALL_THISCALL 跳转
       │
       ▼
    ERASE_METHOD_PTR 生成的 thunk 函数
       │
       ▼
    直接调用 C++ 方法 (类型擦除的函数指针)

    → 等价于 sluaunreal 的静态绑定路径
    → 零 ProcessEvent, 零 FFrame, 零反射
    → 最快路径

路径 2: 反射回退 (43.43% 的函数)
────────────────────────────────
    AS 脚本调用
       │
       ▼
    asCScriptEngine 查找注册的 asCALL_GENERIC 分派器
       │
       ▼
    ASAutoCaller::FunctionCaller (通用编组函数)
       │
       ▼
    从 AS 参数 → 构造参数缓冲区
       │
       ▼
    UFunction->Invoke(Obj, FFrame, ReturnAddr)  或  ProcessEvent
       │
       ▼
    返回值编组回 AS

    → 等价于 puerts 的 SlowCall / sluaunreal 的动态反射

对照总结:
┌──────────────────┬───────────────────────────────────────────┐
│ AngelScript 路径 │ 等价的 Reference 方案                      │
├──────────────────┼───────────────────────────────────────────┤
│ 直接绑定         │ sluaunreal 静态绑定 (最快)                │
│ (ERASE_*_PTR)    │ = 编译期函数指针, 直接 ABI 调用           │
├──────────────────┼───────────────────────────────────────────┤
│ 反射回退         │ puerts SlowCall / UnLua CallUE            │
│ (FunctionCaller) │ = ProcessEvent 或 Invoke + 参数编组       │
├──────────────────┼───────────────────────────────────────────┤
│ UHT 桩条目       │ 无对应 (AS 特有)                          │
│ (bReflective-    │ = 占位符, 消费时才决定走反射              │
│  FallbackBound)  │                                           │
└──────────────────┴───────────────────────────────────────────┘

可借鉴的调用分派优化:
    1. puerts 的 Code=nullptr 技巧:
       反射回退路径可用 Invoke 替代 ProcessEvent, 省去前置检查
       → 已在路径2中部分采用, 可扩大覆盖范围

    2. sluaunreal 的 LuaFunctionAccelerator 缓存模式:
       对反射回退路径, 预计算并缓存 {参数偏移, 大小, 类型} 元数据
       → 避免每次调用都遍历 UFunction->ChildProperties
       → 与 P7 (FunctionCaller 缓存) 方案一致

    3. UnLua 的 FParamBufferAllocator 池化:
       反射回退路径的参数缓冲区复用
       → 减少 malloc/free 频率
       → 与 P8 提到的参数缓冲区优化方向一致

    4. UnrealCSharp 的分级策略:
       按函数特征选择最优路径, 而非统一走一条路
       → AngelScript 已有 "直接绑定 vs 反射回退" 双路径
       → 可进一步细分: 无参数函数 → 最简 thunk, 有 out 参数 → FOutParmRec 预构建
```
