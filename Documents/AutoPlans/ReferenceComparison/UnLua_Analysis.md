# UnLua 源码分析

> **分析对象**: UnLua (Tencent) / 当前仓库快照
> **源码路径**: `Reference/UnLua/`
> **对比基准**: `Plugins/Angelscript/`
> **分析日期**: 2026-04-08

`UnLua` 的主轴不是把 Lua 类型系统强行嵌进 UE，而是直接复用 `UClass`、`UFunction`、`FProperty` 这些现成反射对象，把 Lua 运行时挂到 UE 对象生命周期上。对比之下，`Angelscript` 更像是在 UE 里构建一套脚本类型系统和编译链，因此两者在绑定、热重载、调试三个维度上的重心明显不同：`UnLua` 偏轻接入，`Angelscript` 偏强类型与内建基础设施。

本轮按用户指定重点展开 `D1`、`D2`、`D3`、`D4`、`D5`、`D9`、`D10`。未展开的 `D6`、`D7`、`D8`、`D11` 不在本轮结论范围内。

## 插件架构总览

```
[UnLua] Plugin Architecture
├─ UnLua Runtime                                  // Lua VM、反射绑定、Blueprint 覆写
│  ├─ LuaEnv / UnLuaManager                       // 绑定入口与对象生命周期
│  ├─ Registries / ReflectionUtils                // 反射类型注册与描述
│  └─ LuaFunction / LuaOverrides                  // UFunction 覆写桥
├─ UnLuaEditor                                    // 工具栏、热重载、IntelliSense
│  ├─ Toolbars                                    // 编辑器入口
│  ├─ DirectoryWatcher / HotReload                // 文件变更与重载触发
│  └─ IntelliSenseGenerator                       // 声明生成
└─ UnLuaTestSuite                                 // Automation 测试插件
   ├─ TestCommon                                  // 测试宏与执行框架
   └─ Tests                                       // Binding / Issue / Regression

[Angelscript] Plugin Architecture
├─ AngelscriptRuntime                             // 编译器、运行时、绑定、调试服务
├─ AngelscriptEditor                              // 编辑器扩展
└─ AngelscriptTest                                // 回归、示例、调试器测试
```

从模块边界看，`UnLua` 把“运行时最小闭包”和“开发体验附加能力”拆得更开：`UnLua` 运行时只依赖 `Lua` 与少量 UE runtime 模块，编辑器能力单独放到 `UnLuaEditor`，测试再拆成 `UnLuaTestSuite`。`Angelscript` 则把绑定、编译、热重载、调试等横切能力更集中地放进 `AngelscriptRuntime`，因此 runtime 模块天然更重，但能力闭包更完整。

## [维度 D1] 模块划分与构建

### 实现概述

`UnLua` 的构建切分非常明确。`UnLua` runtime 负责 Lua 虚拟机与反射桥；`UnLuaEditor` 吃下 `BlueprintGraph`、`DirectoryWatcher`、`Networking`、`Sockets`、`ToolMenus` 等开发期依赖；测试则独立为 `UnLuaTestSuite` 插件。这个分层意味着发布时可以把运行时代码与编辑器增强能力较干净地隔离。

`Angelscript` 当前实现更偏“核心能力内聚”。`AngelscriptRuntime` 的 `Build.cs` 直接拉入 `GameplayTags`、`StructUtils`、`EnhancedInput`、`GameplayAbilities`、`Sockets` 等大量能力，说明它不仅是脚本 VM 包装层，还是绑定与调试能力的承载中心。`AngelscriptEditor` 则只承接 Editor UI 与资产侧扩展。

```
[D1] Module Responsibility Split
UnLua
├─ UnLua.Build.cs                                 // 保持 runtime 轻量
├─ UnLuaEditor.Build.cs                           // 编辑器、网络、菜单、监听器
└─ UnLuaTestSuite.Build.cs                        // 测试隔离为独立插件

Angelscript
├─ AngelscriptRuntime.Build.cs                    // 运行时同时承载绑定/调试/较多引擎能力
├─ AngelscriptEditor.Build.cs                     // 编辑器集成
└─ AngelscriptTest.Build.cs                       // 测试模块同插件内维护
```

关键源码 [1]：`UnLua` runtime 与 editor 依赖切分

```csharp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/UnLua.Build.cs
// 函数: UnLua::UnLua
// 行号: 48-66, 106-111
// 位置: runtime 只依赖少量核心模块，并通过编译宏打开/关闭热重载
// ============================================================================
PublicDependencyModuleNames.AddRange(
    new[]
    {
        "Core",
        "CoreUObject",
        "Engine",
        "Slate",
        "InputCore",
        "Lua"           // ★ runtime 对 Lua 的唯一直接耦合点
    }
);

if (Target.bBuildEditor)
{
    OptimizeCode = CodeOptimization.Never;
    PrivateDependencyModuleNames.Add("UnrealEd"); // 编辑器下才拉入 UnrealEd
}

string hotReloadMode;
if (!config.GetString(section, "HotReloadMode", out hotReloadMode))
    hotReloadMode = "Manual";

var withHotReload = hotReloadMode != "Never";
PublicDefinitions.Add("UNLUA_WITH_HOT_RELOAD=" + (withHotReload ? "1" : "0")); // ★ 运行时通过宏感知是否启用热重载
```

```csharp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/UnLuaEditor.Build.cs
// 函数: UnLuaEditor::UnLuaEditor
// 行号: 36-45, 47-95
// 位置: editor 模块承担工具、监听、网络与菜单扩展职责
// ============================================================================
string EngineDir = Path.GetFullPath(Target.RelativeEnginePath);
PrivateIncludePaths.AddRange(
    new[]
    {
        "UnLuaEditor/Private",
        "UnLua/Private", // ★ editor 直接读取 runtime 内部实现
        Path.Combine(EngineDir, "Source/Editor/AnimationBlueprintEditor/Private"),
        Path.Combine(EngineDir, "Source/Runtime/Slate/Private"),
    }
);

PrivateDependencyModuleNames.AddRange(
    new[]
    {
        "Core",
        "CoreUObject",
        "Engine",
        "UnrealEd",
        "DeveloperToolSettings",
        "UMG",
        "UMGEditor",
        "BlueprintGraph",
        "DirectoryWatcher", // ★ 文件变更监听
        "Networking",       // ★ 编辑器期网络功能
        "Sockets",
        "UnLua",
        "Lua",
        "ToolMenus"         // ★ 工具栏菜单入口
    }
);
```

### 设计取舍

- `UnLua` 把 editor 能力外置，换来 runtime 边界清晰、宿主接入成本低，但也意味着某些开发体验能力天然只能在 editor 模式提供。
- `Angelscript` 把更多横切能力收进 runtime，换来统一入口和更强的能力闭包，但 runtime 编译依赖更大、模块职责更重。
- `UnLuaEditor` 直接包含 `UnLua/Private`，说明它的模块边界虽然分开了，但实现层仍有内部耦合；这属于“物理拆分，逻辑协作”，不是完全隔离。

### 与 Angelscript 对比

| 对比点 | UnLua 证据 | Angelscript 证据 | 结论 |
| --- | --- | --- | --- |
| runtime 依赖规模 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/UnLua.Build.cs:48-66` | `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs:29-65` | `UnLua` runtime 更轻；`AngelscriptRuntime` 承担更多横切能力 |
| editor 职责 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/UnLuaEditor.Build.cs:47-95` | `Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs:12-40` | 两者都拆 editor，但 `UnLuaEditor` 明显承担更多开发体验基础设施 |
| 测试组织 | `Reference/UnLua/Plugins/UnLuaTestSuite/...` | `Plugins/Angelscript/Source/AngelscriptTest/...` | 都有独立测试层，只是 `UnLua` 以独立插件存在，`Angelscript` 以内置模块存在 |

**差距类型**: `实现方式不同`

**可吸收点**:

- 中优先级：`AngelscriptRuntime` 可以继续审视哪些 editor-only 依赖能外移，降低 runtime 编译面。
- 低优先级：`UnLua` 这种“runtime 轻、editor 重”的拆法不一定适合 `Angelscript`，因为 `Angelscript` 的编译器、热重载、调试服务本来就在 runtime 生命周期内工作。

## [维度 D2] 反射绑定机制

### 实现概述

`UnLua` 的主绑定路径是“运行时按需反射注册”。对象进入 `FLuaEnv::TryBind()` 后，先判断类是否实现 `UUnLuaInterface`，再定位 Lua module；真正需要把 UE 类型暴露给 Lua 时，`FClassRegistry::RegisterReflectedType()` 通过 `UStruct` / `UEnum` 反射对象即时生成描述。也就是说，`UnLua` 的默认路径并不要求先为每个 `UClass` 写一份手工注册代码。

`Angelscript` 的默认路径正好相反。当前仓库大量能力通过 `Bind_*.cpp` 静态注册，例如 `Bind_AActor.cpp` 逐个调用 `Method(...)` 明确暴露签名；同时又在 `BlueprintCallableReflectiveFallback.cpp` 中提供“反射回退”能力，用来覆盖没有手写绑定的 `BlueprintCallable` 函数。这个结构说明 `Angelscript` 并非完全没有反射绑定，而是把反射当作补洞机制，而不是主路径。

```
[D2] Reflection Binding Flow
UnLua
├─ UObject enters FLuaEnv::TryBind()              // 绑定入口
├─ Check UUnLuaInterface / dynamic binding        // 判定绑定来源
├─ ModuleLocator resolves Lua module              // 定位脚本
└─ FClassRegistry::RegisterReflectedType()        // ★ 需要时按反射对象注册

Angelscript
├─ Bind_*.cpp static registration                 // ★ 主路径是显式注册
├─ FAngelscriptBinds::ExistingClass(...).Method()
└─ BlueprintCallableReflectiveFallback            // 仅对未直绑函数做回退
```

关键源码 [1]：`UnLua` 把 `UObject` 绑定决策与反射注册串起来

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp
// 函数: FLuaEnv::TryBind
// 行号: 336-386
// 位置: 绑定入口先判定静态绑定，再回退到动态绑定
// ============================================================================
bool FLuaEnv::TryBind(UObject* Object)
{
    const auto Class = Object->IsA<UClass>() ? static_cast<UClass*>(Object) : Object->GetClass();

    static UClass* InterfaceClass = UUnLuaInterface::StaticClass();
    const bool bImplUnluaInterface = Class->ImplementsInterface(InterfaceClass);

    if (!bImplUnluaInterface)
    {
        // ★ 没有实现接口时，才走 dynamic binding
        if (!GLuaDynamicBinding.IsValid(Class))
            return false;

        return GetManager()->Bind(Object, *GLuaDynamicBinding.ModuleName, GLuaDynamicBinding.InitializerTableRef);
    }

    if (!ensureMsgf(ModuleLocator, TEXT("Invalid lua module locator")))
        return false;

    const auto ModuleName = ModuleLocator->Locate(Object); // ★ 从 UE 对象反查 Lua module
    if (ModuleName.IsEmpty())
        return false;

    return GetManager()->Bind(Object, *ModuleName, GLuaDynamicBinding.InitializerTableRef);
}
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp
// 函数: FClassRegistry::RegisterReflectedType
// 行号: 59-105
// 位置: 根据 metatable 名或 UStruct 反射对象即时注册类型描述
// ============================================================================
FClassDesc* FClassRegistry::RegisterReflectedType(const char* MetatableName)
{
    FClassDesc* Ret = Find(MetatableName);
    if (Ret)
        return Ret;

    const char* TypeName = MetatableName[0] == 'U' || MetatableName[0] == 'A' || MetatableName[0] == 'F'
        ? MetatableName + 1
        : MetatableName;

    const auto Type = LoadReflectedType(TypeName); // ★ 直接从 UE 反射系统取类型
    if (!Type)
        return nullptr;

    const auto StructType = Cast<UStruct>(Type);
    if (StructType)
    {
        Ret = RegisterInternal(StructType, UTF8_TO_TCHAR(MetatableName)); // ★ 按需注册
        return Ret;
    }

    return nullptr;
}
```

关键源码 [2]：`Angelscript` 主路径仍是显式注册，反射回退只覆盖局部场景

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp
// 函数: Bind_AActor_Base lambda
// 行号: 25-37
// 位置: 手写绑定是 Angelscript 的主暴露路径
// ============================================================================
auto AActor_ = FAngelscriptBinds::ExistingClass("AActor");

AActor_.Method("bool IsActorInitialized() const", METHOD_TRIVIAL(AActor, IsActorInitialized));
AActor_.Method("bool HasActorBegunPlay() const", METHOD_TRIVIAL(AActor, HasActorBegunPlay));
AActor_.Method("FVector GetActorLocation() const", METHOD_TRIVIAL(AActor, GetActorLocation));
AActor_.Method("FRotator GetActorRotation() const", METHOD_TRIVIAL(AActor, GetActorRotation));
// ★ 每个暴露点都显式写出脚本签名与原生函数
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp
// 函数: BindBlueprintCallableReflectiveFallback
// 行号: 374-420
// 位置: 只有在满足条件时才走反射回退
// ============================================================================
bool BindBlueprintCallableReflectiveFallback(
    TSharedRef<FAngelscriptType> InType,
    UFunction* Function,
    FAngelscriptFunctionSignature& Signature,
    FFuncEntry& Entry)
{
    Entry.bReflectiveFallbackBound = false;

    if (!ShouldBindBlueprintCallableReflectiveFallback(Function))
        return false; // ★ 不满足策略直接退出，不是默认路径

    if (!Signature.bAllTypesValid || Signature.ArgumentTypes.Num() > BlueprintCallableReflectiveFallbackMaxArgs)
        return false;

    if (IsScriptDeclarationAlreadyBound(InType, Signature))
        return false;

    auto* ReflectiveSignature = new FBlueprintCallableReflectiveSignature();
    ReflectiveSignature->UnrealFunction = Function;

    if (!BindReflectiveFunction(InType, Signature, ReflectiveSignature))
    {
        delete ReflectiveSignature;
        return false;
    }

    Entry.bReflectiveFallbackBound = true;
    return true;
}
```

### 设计取舍

- `UnLua` 选择零胶水主路径，换来“接入已有 UE 类型的边际成本低”，但调用签名检查更多发生在运行时，IDE 和编译期约束天然较弱。
- `Angelscript` 选择显式绑定主路径，换来脚本签名、文档性和可控性更强，但人肉维护 `Bind_*.cpp` 与生成表的开销更高。
- `Angelscript` 的反射回退不是缺陷修补，而是有意识地把“全自动暴露”限制在安全边界内，避免所有 `UFunction` 都自动进入脚本表面。

### 与 Angelscript 对比

| 对比点 | UnLua 证据 | Angelscript 证据 | 结论 |
| --- | --- | --- | --- |
| 默认绑定入口 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:336-386` | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp:25-37` | `UnLua` 默认按对象运行时反射绑定；`Angelscript` 默认手写注册 |
| 类型注册方式 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:59-105` | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp:374-420` | `UnLua` 把反射注册作为主路径；`Angelscript` 把反射作为 fallback |
| 显式注册成本 | 低，主要成本在 module 命名与运行时约束 | 高，维护 `Bind_*.cpp` 与生成表 | 这是两者最根本的工程取舍差异 |

**差距类型**:

- `Angelscript` 相对 `UnLua` 的“零胶水主路径”属于 `没有实现`
- 两者整体绑定哲学属于 `实现方式不同`

**可吸收点**:

- 高优先级：`Angelscript` 可以继续扩大“受控反射回退”的覆盖面，例如把更多稳定、签名简单的 `BlueprintCallable` 函数纳入自动暴露，降低手写绑定密度。
- 中优先级：不要直接照搬 `UnLua` 式全自动主路径；`Angelscript` 当前大量设计依赖强类型声明、生成表和脚本编译期约束，贸然切换会冲击现有调试、热重载与 IDE 体验。

## [维度 D3] Blueprint 交互

### 实现概述

`UnLua` 解决的是“如何把脚本挂到一个已经存在的 Blueprint 资产上”。它要求 Blueprint / C++ 类实现 `UUnLuaInterface`，由 `GetModuleName()` 返回 Lua 文件路径；`UUnLuaManager::BindClass()` 收集 Lua table 里的函数名后，用 `ULuaFunction::Override()` 把同名 `UFunction` 替换成 `execCallLua` / `execScriptCallLua`。因此，脚本是附着在现有 `UClass` 上的。

`Angelscript` 解决的是“脚本如何成为 UE 类型系统的一部分”。`BlueprintOverride`、`BlueprintEvent` 和 `Mixin` 都会被翻译成带 `FUNC_BlueprintEvent` 元数据的 `UFunction`，再通过反射调用桥回到 UE。这里的脚本不是附着在现成 Blueprint 上，而是参与生成可被 Blueprint 再继承、再覆写的脚本类。

```
[D3] Blueprint Interop Model
UnLua
├─ Blueprint/C++ class implements UUnLuaInterface
├─ GetModuleName() -> Lua module path
├─ UUnLuaManager::BindClass() scans Lua table
└─ ULuaFunction::Override() swaps target UFunction

Angelscript
├─ Script class declares UFUNCTION(BlueprintOverride/Event)
├─ ClassGenerator sets Blueprint metadata/flags
├─ Bind_BlueprintEvent stores reflective signature
└─ CallEventWithSignature / CallMixinWithSignature bridges invocation
```

关键源码 [1]：`UnLua` 通过接口和 `ULuaFunction` 覆写既有 Blueprint 行为

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaInterface.h
// 函数: IUnLuaInterface::GetModuleName
// 行号: 23-38
// 位置: Blueprint 通过接口声明脚本 module 名称
// ============================================================================
UINTERFACE()
class UNLUA_API UUnLuaInterface : public UInterface
{
    GENERATED_BODY()
};

class UNLUA_API IUnLuaInterface
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintNativeEvent)
    FString GetModuleName() const; // ★ 返回 Content/Script 下相对路径
};
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaManager.cpp
// 函数: UUnLuaManager::BindClass
// 行号: 305-316
// 位置: 遍历 Lua table，把同名 UFunction 替换成 Lua 桥
// ============================================================================
UnLua::LowLevel::GetFunctionNames(Env->GetMainState(), Ref, BindInfo.LuaFunctions);
ULuaFunction::GetOverridableFunctions(Class, BindInfo.UEFunctions);

for (const auto& LuaFuncName : BindInfo.LuaFunctions)
{
    UFunction** Func = BindInfo.UEFunctions.Find(LuaFuncName);
    if (Func)
    {
        UFunction* Function = *Func;
        ULuaFunction::Override(Function, Class, LuaFuncName); // ★ 真正把 Blueprint 事件入口切到 Lua
    }
}
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaFunction.cpp
// 函数: ULuaFunction::SetActive
// 行号: 210-239
// 位置: 激活后把目标 UFunction 的 native thunk 替换成 Lua 调度入口
// ============================================================================
if (bAdded)
{
    SetNativeFunc(execCallLua);
    Class->AddFunctionToFunctionMap(this, *GetName());
}
else
{
    Function->FunctionFlags |= FUNC_Native;
    Function->SetNativeFunc(&execScriptCallLua);              // ★ 用 Lua thunk 接管
    Function->GetOuterUClass()->AddNativeFunction(*Function->GetName(), &execScriptCallLua);
    Function->Script.Empty();
    Function->Script.AddUninitialized(ScriptMagicHeaderSize + sizeof(ULuaFunction*));
    FPlatformMemory::WriteUnaligned<ULuaFunction*>(Data + ScriptMagicHeaderSize, this);
}
```

关键源码 [2]：`Angelscript` 用 `BlueprintOverride` / `Mixin` 把脚本函数纳入 UE 类型系统

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp
// 函数: CallEventWithSignature / CallMixinWithSignature
// 行号: 492-544
// 位置: Blueprint 事件与 Mixin 调用最终都落到反射桥
// ============================================================================
struct FBlueprintEventSignature
{
    FAngelscriptTypeUsage ReturnType;
    FAngelscriptTypeUsage Arguments[AS_EVENT_MAX_ARGS];
    FAngelscriptTypeUsage MixinType;
    UFunction* UnrealFunction = nullptr;
};

void CallEventWithSignature(asIScriptGeneric* InGeneric)
{
    asCGeneric* Generic = static_cast<asCGeneric*>(InGeneric);
    auto* Sig = (FBlueprintEventSignature*)Generic->GetFunction()->GetUserData();
    InvokeReflectiveUFunctionFromGenericCall(Generic, static_cast<UObject*>(Generic->GetObject()), Sig->UnrealFunction);
}

void CallMixinWithSignature(asIScriptGeneric* InGeneric)
{
    asCGeneric* Generic = static_cast<asCGeneric*>(InGeneric);
    auto* Sig = (FBlueprintEventSignature*)Generic->GetFunction()->GetUserData();
    InvokeReflectiveUFunctionFromGenericCall(Generic, Sig->StaticObject, Sig->UnrealFunction, true); // ★ Mixin 走独立桥接路径
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 函数: ClassGenerator function finalize path
// 行号: 3435-3458
// 位置: 脚本函数被标上 Mixin / BlueprintEvent 元数据，进入 UE 反射系统
// ============================================================================
if (ScriptFunction->traits.GetTrait(asTRAIT_MIXIN)
    && ScriptFunction->parameterNames.GetLength() >= 1)
{
    FString MixinArgumentName = ANSI_TO_TCHAR(ScriptFunction->parameterNames[0].AddressOf());
    NewFunction->SetMetaData(NAME_Function_MixinArgument, *MixinArgumentName); // ★ 标记 mixin self 参数
    NewFunction->SetMetaData(NAME_Function_DefaultToSelf, *MixinArgumentName);
}

if (FunctionDesc->bBlueprintCallable && !FunctionDesc->bIsPrivate)
    NewFunction->FunctionFlags |= FUNC_BlueprintCallable;
if ((FunctionDesc->bBlueprintEvent && FunctionDesc->bCanOverrideEvent) || FunctionDesc->bBlueprintOverride)
    NewFunction->FunctionFlags |= FUNC_BlueprintEvent; // ★ 让 Blueprint 继续参与覆写链
```

### 设计取舍

- `UnLua` 的优势是可以把 Lua 直接附着到已有 Blueprint 资产，不必引入新的脚本类层级，适合“给现有内容加脚本”。
- `Angelscript` 的优势是脚本函数进入 UE 类型系统后，`BlueprintOverride` / `BlueprintEvent` / `Mixin` 可以形成更稳定的脚本类继承链，适合“脚本本身就是类型定义者”。
- `UnLua` 的覆写机制本质上是替换 `UFunction` thunk；`Angelscript` 的机制本质上是生成新的脚本类函数描述并把它们正规化为 UE 反射对象。两者抽象层级并不相同。

### 与 Angelscript 对比

| 对比点 | UnLua 证据 | Angelscript 证据 | 结论 |
| --- | --- | --- | --- |
| 脚本挂载入口 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaInterface.h:23-38` | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3435-3458` | `UnLua` 依赖接口返回 module 路径；`Angelscript` 依赖脚本类与元数据生成 |
| 覆写实现 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaManager.cpp:305-316` + `.../LuaFunction.cpp:210-239` | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp:492-544` | `UnLua` 是替换既有 `UFunction` thunk；`Angelscript` 是桥接生成后的 Blueprint 事件函数 |
| Mixin 能力 | 未见等价 Mixin 抽象 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3435-3442` | `Angelscript` 在“把一组方法混入现有类型”上表达力更强 |

**差距类型**: `实现方式不同`

**可吸收点**:

- 高优先级：`Angelscript` 可以吸收 `UnLuaInterface + GetModuleName()` 这种“把脚本附着到现有 Blueprint 资产”的轻量入口，作为脚本类体系之外的快速接入模式。
- 中优先级：`UnLua` 侧的 `UFunction` 覆写方案不应直接照搬到 `Angelscript` 主链，因为这会绕开 `Angelscript` 现有的类生成、Mixin 元数据和 Blueprint 再继承能力。

## [维度 D4] 热重载

### 实现概述

`UnLua` 的热重载明显是 Lua 层方案。`UnLua.Build.cs` 只通过 `UNLUA_WITH_HOT_RELOAD` 编译宏决定是否带上这套能力；真正的重载逻辑在 `Plugins/UnLua/Content/Script/UnLua/HotReload.lua`。该脚本维护 `loaded_module_times`，比较文件修改时间后找出变更模块，再在 sandbox 中重新执行 chunk，最后通过 `merge_objects()` 回填旧 table、迁移函数 upvalue，尽量保住运行期状态。

`Angelscript` 的热重载则是编译器/类型系统级方案。`ECompileType` 区分 `SoftReloadOnly` 与 `FullReload`；`PerformHotReload()` 会做依赖扩张、模块重编译、类/属性重连；测试直接校验 soft reload 后原 `UClass` 是否保持同一实例。这说明 `Angelscript` 的核心目标不是“重新 require 一段脚本”，而是“在保持 UE 类型对象稳定的前提下重编译脚本模块”。

```
[D4] Hot Reload Flow
UnLua
├─ Track loaded_module_times                       // 记录 Lua 文件修改时间
├─ Detect modified modules                         // 比较时间戳
├─ sandbox.load() + xpcall()                      // 在隔离环境重新执行
└─ merge_objects()                                // 合并 table / upvalue 状态

Angelscript
├─ ECompileType::SoftReloadOnly / FullReload
├─ PerformHotReload() builds dependency closure
├─ Recompile affected script modules
└─ Preserve / relink generated UClass + UFunction // 由测试回归保证
```

关键源码 [1]：`UnLua` 通过 sandbox 和对象合并维持模块状态

```lua
-- ============================================================================
-- 文件: Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua
-- 函数: make_sandbox / merge_objects / M.reload
-- 行号: 112-169, 223-249, 604-624
-- 位置: Lua 层热重载核心，按模块重载并合并旧状态
-- ============================================================================
local loaded_module_times = {}

local function get_last_modified_time(module_name)
    local filename = config.script_root_path .. module_name:gsub("%.", "/") .. ".lua"
    return UE.UUnLuaFunctionLibrary.GetFileLastModifiedTimestamp(filename)
end

local function require(module_name, ...)
    if package.loaded[module_name] ~= nil then
        return package.loaded[module_name], nil
    end

    local func, env = load(module_name)
    if func then
        local _, new_module = xpcall(func, load_error_handler, ...)
        if loaded_modules[module_name] == nil then
            loaded_modules[module_name] = new_module
            package.loaded[module_name] = new_module
            loaded_module_times[module_name] = get_last_modified_time(module_name) -- ★ 记录当前版本时间戳
            return new_module, nil
        end
    end
end

local function merge_objects(module_res)
    for _, m in ipairs(module_res) do
        for index, v in ipairs(m.values) do
            for name, value_map in pairs(v) do
                for old, new in pairs(value_map) do
                    if type(new) == "table" and not sandbox.is_loaded(new) then
                        for k, nv in pairs(new) do
                            old[k] = nv -- ★ 新表内容回填到旧表，保住外部引用
                        end
                    elseif type(new) == "function" then
                        local id = debug.upvalueid(new, i)
                        local uv = m.upvalue_map[id]
                        if uv then
                            debug.setupvalue(new, i, uv.replaced_upvalue) -- ★ 迁移 upvalue
                        end
                    end
                end
            end
        end
    end
end

function M.reload(module_names)
    local modified_modules = {}

    for module_name, time in pairs(loaded_module_times) do
        local current_time = get_last_modified_time(module_name)
        if current_time ~= time then
            modified_modules[#modified_modules + 1] = module_name
            loaded_module_times[module_name] = current_time
        end
    end

    if #modified_modules > 0 then
        reload_modules(modified_modules) -- ★ 按模块级别重载
    end
end
```

关键源码 [2]：`Angelscript` 的热重载是编译级与类型级修复流程

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h
// 函数: ECompileType
// 行号: 97-109
// 位置: 明确把热重载分成 soft 与 full 两条路径
// ============================================================================
enum class ECompileType : uint8
{
    Initial,
    SoftReloadOnly, // ★ 尽量保留现有类型对象
    FullReload,     // ★ 允许更激进的全量重载
};
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: FAngelscriptEngine::PerformHotReload
// 行号: 2253-2335
// 位置: 先扩展依赖闭包，再决定哪些模块需要重新编译
// ============================================================================
bool FAngelscriptEngine::PerformHotReload(ECompileType CompileType, const TArray<FFilenamePair>& InReloadFiles)
{
    TArray<FFilenamePair> FileList;
    FileList.Append(InReloadFiles);

    TSet<FFilenamePair> FilesToHotReload;
    if (FileList.Num() > 0)
    {
        if (GAngelscriptRecompileAvoidance && ShouldUseAutomaticImportMethod())
        {
            FilesToHotReload.Append(FileList);
        }
        else
        {
            TMap<FString, FAngelscriptModuleDesc*> RelativeFileToModule;
            for (auto& Module : ActiveModules)
            {
                auto ModulePtr = &(Module.Value.Get());
                for (const auto& Section : ModulePtr->Code)
                    RelativeFileToModule.Add(Section.RelativeFilename, ModulePtr);
            }
            // ★ 这里开始递归找出依赖于变更文件的模块集合
        }
    }
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPropertyTests.cpp
// 函数: Soft reload regression
// 行号: 84-133
// 位置: 回归测试直接验证 soft reload 保住原 UClass 实例
// ============================================================================
UClass* ClassV1 = FindGeneratedClass(&Engine, TEXT("USoftReloadTarget"));
UFunction* GetVersionV1 = FindGeneratedFunction(ClassV1, TEXT("GetVersion"));

ECompileResult ReloadResult = ECompileResult::Error;
CompileModuleWithResult(&Engine, ECompileType::SoftReloadOnly, TEXT("SoftReloadMod"), TEXT("SoftReloadMod.as"), ScriptV2, ReloadResult);

UClass* ClassAfterReload = FindGeneratedClass(&Engine, TEXT("USoftReloadTarget"));
TestEqual(TEXT("Soft reload should preserve the generated UClass instance"), ClassAfterReload, ClassV1); // ★ 类型对象不换壳
```

### 设计取舍

- `UnLua` 热重载的优势是粒度细、成本低，对纯 Lua 逻辑非常友好；代价是它主要照顾 Lua module 自身状态，而不是 UE 类型系统级的一致性。
- `Angelscript` 热重载的优势是能维护 `UClass` / `UFunction` 级稳定性，更适合强类型脚本；代价是实现复杂度、失败恢复和依赖分析成本更高。
- 两者都在尝试“保状态”，但保的对象不同：`UnLua` 保 table / upvalue，`Angelscript` 保 UE 反射对象身份。

### 与 Angelscript 对比

| 对比点 | UnLua 证据 | Angelscript 证据 | 结论 |
| --- | --- | --- | --- |
| 重载粒度 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:112-169,604-624` | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2253-2335` | `UnLua` 是模块级；`Angelscript` 是编译模块级并带依赖扩张 |
| 状态保持对象 | `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:223-249` | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPropertyTests.cpp:84-133` | `UnLua` 保 Lua 对象图；`Angelscript` 保 `UClass` 身份 |
| 开关机制 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/UnLua.Build.cs:106-111` | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:97-109` | 两者都有明确开关，但控制层级不同 |

**差距类型**: `实现方式不同`

**可吸收点**:

- 中优先级：`Angelscript` 可吸收 `UnLua` 对“函数闭包/局部状态迁移”的思路，用于缩小 soft reload 触发面，而不必每次都上升到类型级修复。
- 低优先级：`UnLua` 不需要照搬 `Angelscript` 的全量依赖闭包策略，因为 Lua `require` 模型天然更适合轻量重载。

## [维度 D5] 调试与开发体验

### 实现概述

就当前仓库快照看，`UnLua` 的调试体验分成两层。第一层是仓外集成：`Docs/CN/Debugging.md` 明确要求用户安装 `LuaPanda` 或 `LuaHelper`，并在 Lua 代码里 `require("LuaPanda").start(...)`。第二层是仓内辅助：`UnLuaDebugBase` 提供 call stack、local variables、upvalues 抽取函数，便于外部 IDE 或崩溃现场读取 Lua 状态。

`Angelscript` 则把调试服务直接做进仓内 runtime。`AngelscriptDebugServer.h` 定义了消息信封与版本握手，`ProcessScriptLine()` 处理断点/数据断点停机，`AngelscriptDebuggerSmokeTests.cpp` 还对调试握手做了自动化回归。也就是说，`Angelscript` 当前仓内已经拥有可测试的调试协议服务，而当前 `UnLua` 快照仓内未看到同层的 `DAP / debug adapter server` 实现。

```
[D5] Debugging Stack
UnLua
├─ Docs/CN/Debugging.md                           // 指导接入 LuaPanda / LuaHelper
├─ Lua code require("LuaPanda").start(...)
└─ UnLuaDebugBase                                 // 暴露堆栈与变量抓取辅助

Angelscript
├─ AngelscriptDebugServer                         // 仓内调试协议服务
├─ Message envelope / version handshake
├─ Line callback + breakpoints + data breakpoints
└─ Debugger smoke tests                           // 自动回归
```

关键源码 [1]：`UnLua` 当前快照把 IDE 调试能力交给外部 Lua 调试器

```markdown
<!-- =========================================================================
文件: Reference/UnLua/Docs/CN/Debugging.md
函数: 调试接入说明
行号: 1-14
位置: 官方文档直接要求接入外部 Lua 调试器
=========================================================================== -->
## 使用 LuaPanda / LuaHelper 调试

1. 从 VSCode 应用市场安装 LuaPanda / LuaHelper
2. 从 LuaPanda 官方仓库获取 `LuaPanda.lua`
3. 在 Lua 代码中加入 `require("LuaPanda").start("127.0.0.1",8818)`  <!-- ★ 调试入口不在 UnLua runtime 内部 -->

注：调试器依赖 `luasocket`，UnLua 已通过扩展插件集成
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDebugBase.h
// 函数: GetStackVariables / GetLuaCallStack
// 行号: 75-95
// 位置: 仓内提供的是调试辅助 API，而不是完整调试服务
// ============================================================================
UNLUA_API bool GetStackVariables(
    lua_State *L,
    int32 StackLevel,
    TArray<FLuaVariable> &LocalVariables,
    TArray<FLuaVariable> &Upvalues,
    int32 Level = MAX_int32);

UNLUA_API FString GetLuaCallStack(lua_State *L);
// ★ 这些 API 负责暴露运行时堆栈状态，便于外部调试器或 IDE 使用
```

关键源码 [2]：`Angelscript` 把调试协议、停机逻辑和回归测试都放进仓内

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h
// 函数: 调试消息定义
// 行号: 82-140
// 位置: 仓内定义调试协议消息体与版本握手
// ============================================================================
struct FAngelscriptDebugMessageEnvelope
{
    EDebugMessageType MessageType = EDebugMessageType::Disconnect;
    TArray<uint8> Body;
};

struct FStartDebuggingMessage : FDebugMessage
{
    int32 DebugAdapterVersion = 0; // ★ 调试适配器版本握手
};

struct FDebugServerVersionMessage : FDebugMessage
{
    int32 DebugServerVersion = 0;
};
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 函数: FAngelscriptDebugServer::ProcessScriptLine
// 行号: 465-545
// 位置: 逐行回调内处理数据断点、停机与清理
// ============================================================================
void FAngelscriptDebugServer::ProcessScriptLine(class asCContext* Context)
{
    if (!bIsDebugging || bIsPaused || IsEngineExitRequested())
        return;

    if (DataBreakpoints.Num() > 0 && bBreakNextScriptLine)
    {
        SyncActiveDataBreakpointsToAuthoritativeState();

        for (int i = DataBreakpoints.Num() - 1; i >= 0; i--)
        {
            auto& Breakpoint = DataBreakpoints[i];
            if (Breakpoint.bTriggered)
            {
                FStoppedMessage Msg;
                Msg.Text = InfoText;
                Msg.Reason = TEXT("exception");
                PauseExecution(&Msg); // ★ 命中断点后直接停机并通知客户端
            }
        }
    }
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSmokeTests.cpp
// 函数: Debugger.Smoke.Handshake
// 行号: 26-90
// 位置: 自动化测试验证调试客户端能完成版本握手
// ============================================================================
FAngelscriptDebuggerTestClient Client;
Client.Connect(TEXT("127.0.0.1"), Session.GetPort());
Client.SendStartDebugging(2);

TOptional<FAngelscriptDebugMessageEnvelope> Envelope = Client.ReceiveEnvelope();
if (Envelope.IsSet() && Envelope->MessageType == EDebugMessageType::DebugServerVersion)
{
    const TOptional<FDebugServerVersionMessage> DebugServerVersion =
        FAngelscriptDebuggerTestClient::DeserializeMessage<FDebugServerVersionMessage>(Envelope.GetValue());
    TestEqual(TEXT("Debug server version"), DebugServerVersion->DebugServerVersion, DEBUG_SERVER_VERSION); // ★ 握手可回归测试
}
```

### 设计取舍

- `UnLua` 复用外部 Lua 调试生态，集成成本低、IDE 兼容面广，但仓内无法对完整调试链路做闭环回归。
- `Angelscript` 自带调试服务，能力闭包完整且可测试，但维护成本显著更高，协议兼容也需要自己背。
- `UnLua` 的仓内调试辅助 API 并不弱，它解决的是“如何读取 Lua 栈信息”，只是没有在当前快照中继续上推到完整调试服务层。

### 与 Angelscript 对比

| 对比点 | UnLua 证据 | Angelscript 证据 | 结论 |
| --- | --- | --- | --- |
| IDE 调试入口 | `Reference/UnLua/Docs/CN/Debugging.md:1-14` | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h:82-140` | `UnLua` 当前快照依赖外部调试器；`Angelscript` 仓内自带调试协议 |
| 运行时栈信息抓取 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDebugBase.h:75-95` + `.../Private/UnLuaDebugBase.cpp:614-669` | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:465-545` | 两者都有运行时状态访问，但抽象层级不同 |
| 回归测试 | 当前快照未见仓内调试链路自动化测试 | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSmokeTests.cpp:26-90` | `Angelscript` 在仓内调试闭环上更完整 |

**差距类型**: `当前快照仓内未实现同层能力`

**可吸收点**:

- 高优先级：`Angelscript` 不必吸收 `UnLua` 的“依赖外部调试器”做法，但可以吸收其“把运行时状态抓取 API 独立出来”的接口思路，降低调试服务与 VM 内核耦合。
- 中优先级：如果 `Angelscript` 未来考虑多 IDE 生态，可评估同时输出兼容外部调试器的数据层，而不放弃现有 DebugServer V2。

## [维度 D9] 测试基础设施

### 实现概述

`UnLua` 把测试做成独立插件 `UnLuaTestSuite`。从 `Build.cs` 看，它直接依赖 `Lua`、`UnLua`、`UMG`，并设置 `PrecompileForTargets = Any`，说明它被设计成可独立编译、可随工程接入的测试载体。`UnLuaTestCommon.h` 又提供了 `IMPLEMENT_UNLUA_LATENT_TEST`、`IMPLEMENT_UNLUA_INSTANT_TEST` 等宏，把 setup / perform / teardown 固定成统一套路。`BindingTest.cpp` 则直接在 UE Automation 环境里跑 Lua chunk 与世界 tick，覆盖静态绑定、动态绑定、冲突优先级等关键场景。

`Angelscript` 也有独立测试层，但组织方式更偏“仓库标准化”。`AngelscriptTest.Build.cs` 在一个模块内分出 `Core`、`Debugger`、`Dump`、`Native`、`ClassGenerator` 等目录；`Documents/Guides/Test.md` 进一步规定所有测试统一走 `Tools/RunTests.ps1` / `Tools/RunTestSuite.ps1`，并强制带超时、独立输出目录。也就是说，`UnLua` 更像“插件自带测试 harness”，`Angelscript` 更像“仓库级测试规约 + 模块化测试集”。

```
[D9] Test Infrastructure
UnLua
├─ UnLuaTestSuite plugin                           // 测试以独立插件存在
├─ UnLuaTestCommon macros                          // 固定 setup / perform / teardown
└─ BindingTest.cpp                                 // 直接执行 Lua chunk 做集成验证

Angelscript
├─ AngelscriptTest module                          // 测试以内置模块存在
├─ Documents/Guides/Test.md                        // 统一入口与超时规范
└─ Prefix-based suites + specialized directories   // 绑定/调试/热重载分类更细
```

关键源码 [1]：`UnLuaTestSuite` 提供统一测试宏与绑定回归

```csharp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/UnLuaTestSuite.Build.cs
// 函数: UnLuaTestSuite::UnLuaTestSuite
// 行号: 33-64
// 位置: 测试以独立插件存在，可单独预编译
// ============================================================================
PublicDependencyModuleNames.AddRange(
    new[]
    {
        "Core",
        "CoreUObject",
        "Engine",
        "Slate"
    }
);

PrivateDependencyModuleNames.AddRange(
    new[]
    {
        "Lua",
        "UnLua",
        "UMG"
    }
);

PrecompileForTargets = PrecompileTargetsType.Any; // ★ 测试插件可单独预编译
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Public/UnLuaTestCommon.h
// 函数: IMPLEMENT_UNLUA_LATENT_TEST / IMPLEMENT_UNLUA_INSTANT_TEST
// 行号: 171-205
// 位置: 统一 latent / instant 测试骨架
// ============================================================================
#define IMPLEMENT_UNLUA_LATENT_TEST(TestClass, PrettyName) \
IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass##_Runner, PrettyName, (...)) \
bool TestClass##_Runner::RunTest(const FString& Parameters) \
{ \
    TestClass* TestInstance = new TestClass(); \
    TestInstance->SetTestRunner(*this); \
    ADD_LATENT_AUTOMATION_COMMAND(FUnLuaTestCommand_SetUpTest(TestInstance)); \
    ADD_LATENT_AUTOMATION_COMMAND(FUnLuaTestCommand_PerformTest(TestInstance)); \
    ADD_LATENT_AUTOMATION_COMMAND(FUnLuaTestCommand_TearDownTest(TestInstance)); \
    return true; \
}

#define IMPLEMENT_UNLUA_INSTANT_TEST(TestClass, PrettyName) \
IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass##Runner, PrettyName, (...)) \
bool TestClass##Runner::RunTest(const FString& Parameters) \
{ \
    bool bSuccess = false; \
    TestClass* TestInstance = new TestClass(); \
    if (TestInstance->SetUp()) \
    { \
        bSuccess = TestInstance->InstantTest(); \
        TestInstance->TearDown(); \
    } \
    delete TestInstance; \
    return bSuccess; \
}
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Tests/BindingTest.cpp
// 函数: FUnLuaTest_StaticBinding / FUnLuaTest_DynamicBinding
// 行号: 48-99
// 位置: 在真实世界 tick 中验证静态绑定与动态绑定
// ============================================================================
bool FUnLuaTest_StaticBinding::RunTest(const FString& Parameters)
{
    Run([this](lua_State* L, UWorld* World)
    {
        const char* Chunk1 = R"(
            local ActorClass = UE.UClass.Load('/UnLuaTestSuite/Tests/Binding/BP_UnLuaTestActor_StaticBinding.BP_UnLuaTestActor_StaticBinding_C')
            G_Actor = World:SpawnActor(ActorClass)
        )";
        UnLua::RunChunk(L, Chunk1);

        World->Tick(LEVELTICK_All, SMALL_NUMBER); // ★ 通过真实 tick 驱动绑定完成

        const char* Chunk2 = R"(
            return G_Actor:RunTest()
        )";
        UnLua::RunChunk(L, Chunk2);
    });

    return true;
}
```

### 设计取舍

- `UnLua` 的测试优势是插件自洽，拿走 `UnLuaTestSuite` 就能把关键集成场景一起带走；缺点是仓库级执行规范与 CI 入口在当前快照里不够显式。
- `Angelscript` 的测试优势是组织规约更强，适合长期维护大规模回归；缺点是示例与测试容易混在同一工程语境里，新读者需要先理解仓库约定。
- 对 CI 成熟度，本地快照里既没有 `Reference/UnLua/.github`，也没有仓库根 `.github`，因此不能根据当前证据判断 UnLua CI 是否薄弱，只能说“当前快照未见 CI 配置”。

### 与 Angelscript 对比

| 对比点 | UnLua 证据 | Angelscript 证据 | 结论 |
| --- | --- | --- | --- |
| 测试载体 | `Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/UnLuaTestSuite.Build.cs:33-64` | `Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs:12-49` | 两者都有独立测试层，只是 `UnLua` 用插件，`Angelscript` 用模块 |
| 测试骨架 | `Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Public/UnLuaTestCommon.h:171-205` | `Documents/Guides/Test.md:1-12,46-80` | `UnLua` 把规范固化成宏；`Angelscript` 把规范固化成脚本入口与文档规则 |
| 场景覆盖示例 | `Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Tests/BindingTest.cpp:48-176` | `Plugins/Angelscript/Source/AngelscriptTest/...` 多目录分层 | `Angelscript` 场景切分更细；`UnLua` 单插件集成回归更集中 |
| CI 集成 | 当前快照未见 `.github` | 当前仓库也未见 `.github` | **证据不足，不做成熟度优劣判断** |

**差距类型**: `实现方式不同`

**可吸收点**:

- 中优先级：`Angelscript` 可吸收 `UnLua` 这种“插件自带 test harness”的封装方式，降低外部工程复用测试的成本。
- 中优先级：`UnLua` 若继续加强仓库治理，可借鉴 `Documents/Guides/Test.md` 这种统一入口、统一超时、统一产物目录的显式规范。

## [维度 D10] 文档与示例组织

### 实现概述

`UnLua` 的文档组织是典型的“上手导向”。`README.md` 从安装、第一次绑定、Lua 模板生成一路写到教程列表、FAQ、API 与实现原理；目录上同时存在 `Docs/CN`、`Docs/EN`、`Docs/Images`，并在 `Content/Script/Tutorials` 中放可直接运行的教程脚本。更关键的是，`UnLuaEditor` 还把 `Hot Reload` 与 `Generate IntelliSense` 放到了编辑器菜单里，意味着文档、示例和工具入口是互相打通的。

`Angelscript` 当前仓库的文档更偏维护者/开发者导向，例如 `Documents/Guides/Test.md` 这种工程规范文档非常完整，但“面向新用户的脚本教程流”更多散落在 `AngelscriptTest/Examples` 源文件里。这并不是没有示例，而是“示例在测试里、文档在指南里、编辑器入口再单独找”，入门路径没有 `UnLua` 那么一条线串到底。

```
[D10] Docs & Examples Layout
UnLua
├─ README.md                                      // 安装 -> 绑定 -> 教程 -> 文档总索引
├─ Docs/CN / Docs/EN / Docs/Images                // 双语文档
├─ Content/Script/Tutorials                       // 可直接运行的教程脚本
└─ Editor menu                                    // Hot Reload / Generate IntelliSense

Angelscript
├─ Documents/Guides/*.md                          // 工程与维护指南
├─ AngelscriptTest/Examples/*.cpp                 // 示例嵌在测试源码中
└─ Editor/Runtime features                        // 能力存在，但入口分散
```

关键源码 [1]：`UnLua` 的 README 直接把安装、教程、文档索引串成一条 onboarding 路径

```markdown
<!-- =========================================================================
文件: Reference/UnLua/README.md
函数: 快速开始 / 更多示例 / 文档
行号: 29-69
位置: README 直接承担 onboarding 索引页角色
=========================================================================== -->
## 安装
1. 复制 `Plugins` 目录到 UE 工程根目录
2. 重新启动 UE 工程

## 开始 UnLua 之旅
1. 在 UnLua 工具栏中选择 `绑定`
2. 在 `GetModule` 函数中填入 Lua 文件路径
3. 选择 `创建Lua模版文件`
4. 打开 `Content/Script/...` 编写代码

## 更多示例
* `01_HelloWorld`
* `02_OverrideBlueprintEvents`
* `04_DynamicBinding`
* `10_Replications`

## 文档
常用文档：`Settings` | `Debugging` | `IntelliSense` | `FAQ`
```

关键源码 [2]：`UnLuaEditor` 把文档里常提的开发动作直接做成菜单与生成器

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp
// 函数: FMainMenuToolbar::Initialize / GenerateUnLuaSettingsMenu
// 行号: 82-128
// 位置: 编辑器主菜单直接挂出 Hot Reload 与 IntelliSense 入口
// ============================================================================
UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.User");
FToolMenuSection& Section = ToolbarMenu->AddSection("UnluaSettings");
Section.AddEntry(FToolMenuEntry::InitComboButton(..., LOCTEXT("UnLua_Label", "UnLua"), ...));

MenuBuilder.BeginSection(NAME_None, LOCTEXT("Section_Action", "Action"));
MenuBuilder.AddMenuEntry(Commands.HotReload, NAME_None, LOCTEXT("HotReload", "Hot Reload"));
MenuBuilder.AddMenuEntry(Commands.GenerateIntelliSense, NAME_None, LOCTEXT("GenerateIntelliSense", "Generate IntelliSense")); // ★ 文档动作直接变成菜单动作
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp
// 函数: FUnLuaIntelliSenseGenerator::Initialize / UpdateAll
// 行号: 42-90
// 位置: IntelliSense 生成器监听资产变化并批量生成声明
// ============================================================================
OutputDir = IPluginManager::Get().FindPlugin("UnLua")->GetBaseDir() + "/Intermediate/IntelliSense";

FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
AssetRegistryModule.Get().OnAssetAdded().AddRaw(this, &FUnLuaIntelliSenseGenerator::OnAssetAdded);
AssetRegistryModule.Get().OnAssetUpdated().AddRaw(this, &FUnLuaIntelliSenseGenerator::OnAssetUpdated); // ★ 智能提示跟着资产变化更新

TArray<FAssetData> BlueprintAssets;
TArray<const UField*> NativeTypes;
AssetRegistryModule.Get().GetAssets(Filter, BlueprintAssets);
CollectTypes(NativeTypes);
```

### 设计取舍

- `UnLua` 的强项是“从 README 到编辑器菜单再到教程脚本”形成闭环，新用户容易在一个下午内跑通第一个例子。
- `Angelscript` 的强项是工程指南和回归样例完整，但面向初学者的路径不够集中，读者需要在 `Documents/`、测试源码、编辑器功能之间自己拼图。
- `UnLua` 通过 `Docs/CN` / `Docs/EN` 双语目录降低了团队传播摩擦，这一点对插件推广非常实际。

### 与 Angelscript 对比

| 对比点 | UnLua 证据 | Angelscript 证据 | 结论 |
| --- | --- | --- | --- |
| README onboarding | `Reference/UnLua/README.md:29-69` | 当前仓库缺少同等密度的单页 onboarding 索引 | `UnLua` 新手路径更集中 |
| 文档目录组织 | `Reference/UnLua/Docs/{CN,EN,Images}` | `Documents/Guides/*.md` | `UnLua` 更偏用户文档；`Angelscript` 更偏工程文档 |
| 示例载体 | `Reference/UnLua/Content/Script/Tutorials/*` | `Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleActorTest.cpp:1-80` | `UnLua` 示例更接近最终用户消费形态；`Angelscript` 示例更接近测试/源码阅读形态 |
| 编辑器入口 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:82-128` | 当前仓库存在 editor 功能但入门入口更分散 | `UnLua` 的 discoverability 更强 |

**差距类型**:

- 文档组织方式总体属于 `实现方式不同`
- 面向新用户的 onboarding 完整度属于 `实现质量差异`

**可吸收点**:

- 高优先级：`Angelscript` 值得补一份像 `UnLua README` 那样的单页上手索引，把安装、脚本模板、首个示例、调试、测试、常见入口串起来。
- 高优先级：把当前 `AngelscriptTest/Examples` 中最能代表功能面的示例再导出成“用户可直接照抄的脚本文件”，不要只藏在测试源码里。
- 中优先级：评估是否补齐双语文档目录，至少先把最常用的 onboarding 与 debug 文档做成稳定入口。

## 小结

本轮最关键的结论有四个：

- `UnLua` 的核心竞争力是 `D2 + D3` 这一组组合拳：用 UE 反射系统做零胶水暴露，再用 `UnLuaInterface + GetModuleName()` 把 Lua 附着到既有 Blueprint 资产。它降低的是“把脚本加进现有项目”的成本。
- `Angelscript` 的核心竞争力是 `D4 + D5`：热重载、调试协议、脚本类生成都围绕“脚本是正式类型系统成员”构建。它提升的是“把脚本当第一等代码资产长期维护”的能力。
- 两者并不是简单的“谁更先进”，而是解决不同问题。`UnLua` 更轻、更快接入；`Angelscript` 更强类型、更内建。
- 真正值得 `Angelscript` 吸收的，不是照搬 `UnLua` 的 Lua runtime 细节，而是吸收其低摩擦接入层、单页 onboarding 与用户视角的教程组织。

建议优先级：

1. 高优先级：为 `Angelscript` 补一条类似 `UnLuaInterface + GetModuleName()` 的轻量挂载入口，让已有 Blueprint 资产可以更低成本地接入脚本。
2. 高优先级：重构文档入口，产出一份单页 onboarding 文档，并把现有示例从测试源码中抽成用户可消费样例。
3. 中优先级：继续扩大受控的反射 fallback 范围，降低手写绑定密度，但不要破坏现有强类型主路径。

## 与 Angelscript 差异速查

| 维度 | UnLua 做法 | Angelscript 做法 | 差距类型 | 对 Angelscript 的启发 |
| --- | --- | --- | --- | --- |
| D1 模块划分 | runtime 轻量，editor/test 外置 | runtime 内聚更多横切能力 | 实现方式不同 | 审视 editor-only 依赖外移空间 |
| D2 反射绑定 | 运行时零胶水主路径 | 手写绑定主路径 + 反射 fallback | 没有实现 + 实现方式不同 | 扩大受控自动暴露范围 |
| D3 Blueprint 交互 | `UnLuaInterface` 附着既有 Blueprint | `BlueprintOverride/Event` + `Mixin` 融入脚本类体系 | 实现方式不同 | 增加轻量挂载入口 |
| D4 热重载 | `require`/sandbox/table-upvalue 合并 | 编译级 soft/full reload + 类型重连 | 实现方式不同 | 借鉴更细粒度状态迁移 |
| D5 调试 | 当前快照依赖外部 `LuaPanda` / `LuaHelper` | 仓内 DebugServer V2 + 回归测试 | 当前快照仓内未实现同层能力 | 保持内建调试，同时抽离状态读取层 |
| D9 测试 | 独立 `UnLuaTestSuite` 插件 | `AngelscriptTest` 模块 + 仓库级运行规范 | 实现方式不同 | 为外部工程复用提供更轻 test harness |
| D10 文档与示例 | README + 双语 Docs + Tutorials + Editor 菜单闭环 | Guides + Tests/Examples，入口较分散 | 实现方式不同 + 实现质量差异 | 优先补单页 onboarding 与直接可跑示例 |

---

## 深化分析 (2026-04-08 18:26:10)

### [维度 D2] 反射绑定的真正分界：`UnLua` 把显式工作下沉到类型桥，`Angelscript` 把显式工作放在 API 选择层

前文把 `UnLua` 总结成“零胶水反射”，这一轮补充一个更底层的事实：`UnLua` 省掉的是**项目侧 per-class / per-function 胶水文件**，不是省掉**插件内部的类型桥接工作**。`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1537-1658` 维护了一整张 `FProperty -> FPropertyDesc` 分发表；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/PropertyRegistry.cpp:25-91,316-407` 又会把 Lua 栈上的 primitive、`table.__name`、`userdata metatable.__name` 归一成临时 `FProperty` / `ITypeInterface`。也就是说，`UnLua` 的“零胶水”本质是把显式劳动集中进插件内部的一层通用 adapter，而不是彻底取消 adapter。

更细的一点在 `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/ClassDesc.cpp:72-95,117-143`：`FClassDesc::RegisterField()` 对非 native `UScriptStruct` 会主动剥掉 Blueprint 生成字段名末尾的 GUID 后缀，再递归到真正拥有该字段的 `OuterStruct` 注册。这让 `UnLua` 的零胶水覆盖面不只限于原生 `UClass`，连 Blueprint 生成的临时 struct 成员也尽量做了名字归一化。

对比 `Angelscript`，它并不是完全没有 `FProperty` 级泛型搬运能力。`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp:254-420` 已经能用 `FProperty` 循环构造参数缓冲、拷回 `out ref`、写回 return；但 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp:72-90` 明确把这条路径放在“没有 direct native pointer 时的 fallback”。换句话说，两者差异不只是“自动 vs 手写”，而是**把手写复杂度放在哪里**。

```
[D2-Deep] Where The Manual Work Lives
UnLua
├─ [1] Discover reflected field/type at runtime    // 运行时发现类型
├─ [2] Normalize BP-generated field names          // 处理 Blueprint 生成字段名噪声
├─ [3] FPropertyDesc::Create() dispatch matrix     // ★ 维护一张类型适配矩阵
└─ [4] FFunctionDesc uses descriptors to marshal   // 调用时按描述器搬运参数

Angelscript
├─ [1] Bind_BlueprintCallable() chooses entry      // 先找直绑 native pointer
├─ [2] Reflective fallback only when needed        // 没有直绑时才回退
├─ [3] Eligibility gate limits surface             // interface/custom thunk/超参函数都拒绝
└─ [4] FProperty loop marshals params/returns      // 泛型搬运在 fallback 层
```

关键源码 [1]：`UnLua` 的自动暴露建立在一张显式 `FPropertyDesc` 分发表之上

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp
// 函数: FPropertyDesc::Create
// 行号: 1537-1658
// 位置: 按 FProperty 类别选择专用 bridge，而不是“完全无类型适配代码”
// ============================================================================
FPropertyDesc* FPropertyDesc::Create(FProperty *InProperty)
{
    FPropertyDesc* PropertyDesc = nullptr;
    int32 Type = ::GetPropertyType(InProperty);
    switch (Type)
    {
    case CPT_Int8:
    case CPT_Int16:
    case CPT_Int:
    case CPT_Int64:
    case CPT_UInt16:
    case CPT_UInt32:
    case CPT_UInt64:
        PropertyDesc = new FIntegerPropertyDesc(InProperty);      // ★ 整数族统一走整数描述器
        break;
    case CPT_Float:
    case CPT_Double:
        PropertyDesc = new FFloatPropertyDesc(InProperty);        // ★ 浮点族单独处理
        break;
    case CPT_Array:
        PropertyDesc = new FArrayPropertyDesc(InProperty);        // ★ 容器各有专用 bridge
        break;
    case CPT_Map:
        PropertyDesc = new FMapPropertyDesc(InProperty);
        break;
    case CPT_Set:
        PropertyDesc = new FSetPropertyDesc(InProperty);
        break;
    case CPT_Struct:
        PropertyDesc = new FScriptStructPropertyDesc(InProperty); // ★ struct 不是统一 memcpy
        break;
    case CPT_Delegate:
        PropertyDesc = new FDelegatePropertyDesc(InProperty);     // ★ delegate 也要单独桥接
        break;
    case CPT_MulticastDelegate:
        PropertyDesc = new TMulticastDelegatePropertyDesc<FMulticastScriptDelegate>(InProperty);
        break;
    }

    if (PropertyDesc)
        PropertyDesc->SetPropertyType(Type);
    return PropertyDesc;
}
```

关键源码 [2]：`Angelscript` 的 `FProperty` 泛型搬运存在，但被明确限制在 fallback 路径

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp
// 函数: EvaluateReflectiveFallbackEligibility / InvokeReflectiveUFunctionFromGenericCall
// 行号: 254-287, 290-371
// 位置: 先筛掉不安全/不稳定场景，再用 FProperty 循环构造参数缓冲
// ============================================================================
EAngelscriptReflectiveFallbackEligibility EvaluateReflectiveFallbackEligibility(const UFunction* Function)
{
    if (Function == nullptr)
        return EAngelscriptReflectiveFallbackEligibility::RejectedNullFunction;

    const UClass* OwningClass = Function->GetOuterUClass();
    if (OwningClass == nullptr)
        return EAngelscriptReflectiveFallbackEligibility::RejectedMissingOwningClass;

    if (OwningClass->HasAnyClassFlags(CLASS_Interface))
        return EAngelscriptReflectiveFallbackEligibility::RejectedInterfaceClass; // ★ interface 直接拒绝

    if (Function->HasMetaData(NAME_BlueprintCallableReflectiveFallback_CustomThunk))
        return EAngelscriptReflectiveFallbackEligibility::RejectedCustomThunk;    // ★ custom thunk 不交给通用回退

    if (GetNonReturnParameterCount(Function) > BlueprintCallableReflectiveFallbackMaxArgs)
        return EAngelscriptReflectiveFallbackEligibility::RejectedTooManyArguments;

    return EAngelscriptReflectiveFallbackEligibility::Eligible;
}

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
        FProperty* Property = *It;
        if (Property->HasAnyPropertyFlags(CPF_ReturnParm))
            continue;

        void* Destination = Property->ContainerPtrToValuePtr<void>(ParameterBuffer);
        void* ScriptArgumentAddress = Generic->GetAddressOfArg(ScriptArgIndex);
        void* SourceAddress = ResolveScriptArgumentAddress(Property, ScriptArgumentAddress);
        Property->CopySingleValue(Destination, SourceAddress);     // ★ 参数搬运依旧基于 FProperty
        ...
    }

    TargetObject->ProcessEvent(Function, ParameterBuffer);         // ★ 真正调用 UE 反射入口
    ...
    DestroyParameterBuffer(Function, ParameterBuffer);
    return true;
}
```

新增对比结论：

- `UnLua` 的“零胶水”更准确地说是“零项目侧胶水”；插件内部仍维护一层完整 `FPropertyDesc` / `ITypeInterface` 体系。
- `Angelscript` 已经具备 `FProperty` 级参数泛型搬运能力，但它故意不把这条路径做成默认入口，而是受 `eligibility + signature` 双重约束。
- 差距判断：
  - “现有 UE 类型按反射即时暴露”作为主路径：`Angelscript` 相对 `UnLua` 属于 `没有实现`
  - “参数/返回值能否按 `FProperty` 泛型搬运”：不是没有，而是 `实现方式不同`

### [维度 D3] Blueprint 交互的真正差异：`UnLua` 在运行时 patch 既有行为面，`Angelscript` 在编译期重写脚本语义面

前文已经说明 `UnLuaInterface + GetModuleName()` 与 `BlueprintOverride/Mixin` 的差别，这一轮补的是“覆写到底落在哪一层”。`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaFunction.cpp:102-131` 证明 `UnLua` 的可覆写集合不只包含 `BlueprintEvent`，还会把 `ClassReps` 中所有 `CPF_RepNotify` 对应的 `RepNotifyFunc` 收进来；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaManager.cpp:305-358,367-415` 又进一步把 `AnimNotify_*` 和 `InputAction/InputAxis/Touch/Gesture` 通过命名约定接进 Lua。这里的核心不是“脚本声明了哪些事件”，而是“运行时扫描现有类已有的行为面，然后 patch 上去”。

`UnLua` 真正的覆写动作发生在 `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaFunction.cpp:197-239`：原始 `UFunction` 会被复制成 `__Overridden` 备份，原函数的 native thunk 被改写成 `execScriptCallLua`，同时 `Function->Script` 写入一段带 `ScriptMagicHeader` 的 `ULuaFunction*` 指针。这说明 `UnLua` 的 Blueprint 交互本质上是**运行时替换现有 `UFunction` 的执行入口**。

`Angelscript` 的路径则相反。`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:1499-1529,1575-1588,1654-1681` 在预处理阶段就强约束 `BlueprintOverride`：不能是 `static`、不能和 `BlueprintEvent` 共存、不能和网络 specifier 共存；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h:276-305` 再把带 `ScriptMixin` metadata 的 static Unreal 函数改写成脚本成员函数。这里的关键不是 patch 原函数，而是**在脚本声明阶段把函数语义重新分类**。补充一点，`Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/InputComponentScriptMixinLibrary.h:24-109` 也说明 `Angelscript` 对输入绑定的处理更偏“提供 mixin API 让脚本显式调用”，而不是像 `UnLua` 一样按命名约定自动接管现有 `InputComponent` 绑定表。

```
[D3-Deep] Override Surface Placement
UnLua
├─ Existing UFunction set on BlueprintGeneratedClass   // 既有类的行为面
│  ├─ BlueprintEvent
│  ├─ RepNotifyFunc
│  ├─ AnimNotify_* by naming convention
│  └─ Input delegates on UInputComponent
└─ ULuaFunction::SetActive() swaps native thunk        // ★ 运行时替换执行入口

Angelscript
├─ Script UFUNCTION specifiers                         // 脚本声明时决定语义
│  ├─ BlueprintEvent / BlueprintOverride validation
│  ├─ ScriptMixin rewrites static -> member
│  └─ ClassGenerator writes RepNotify metadata
└─ Bind_BlueprintEvent() builds typed bridge           // 类型化桥接进入 ProcessEvent
```

关键源码 [1]：`UnLua` 把 `RepNotify` 一并纳入可覆写集合

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaFunction.cpp
// 函数: ULuaFunction::GetOverridableFunctions
// 行号: 102-131
// 位置: 不是只覆写 BlueprintEvent，还额外扫描 RepNotifyFunc
// ============================================================================
void ULuaFunction::GetOverridableFunctions(UClass* Class, TMap<FName, UFunction*>& Functions)
{
    for (TFieldIterator<UFunction> It(Class, EFieldIteratorFlags::IncludeSuper, EFieldIteratorFlags::ExcludeDeprecated, EFieldIteratorFlags::IncludeInterfaces); It; ++It)
    {
        UFunction* Function = *It;
        if (!IsOverridable(Function))
            continue;
        FName FuncName = Function->GetFName();
        if (!Functions.Find(FuncName))
            Functions.Add(FuncName, Function);
    }

    for (int32 i = 0; i < Class->ClassReps.Num(); ++i)
    {
        FProperty* Property = Class->ClassReps[i].Property;
        if (!Property->HasAnyPropertyFlags(CPF_RepNotify))
            continue;
        UFunction* Function = Class->FindFunctionByName(Property->RepNotifyFunc);
        if (Function && !Functions.Find(Property->RepNotifyFunc))
            Functions.Add(Property->RepNotifyFunc, Function);      // ★ 复制现有复制回调面到 Lua
    }
}
```

关键源码 [2]：`UnLua` 覆写不是 wrapper 调度，而是直接替换原 `UFunction` 的 thunk

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaFunction.cpp
// 函数: ULuaFunction::SetActive
// 行号: 197-239
// 位置: 激活时直接改写原 UFunction 的执行入口
// ============================================================================
void ULuaFunction::SetActive(const bool bActive)
{
    ...
    if (bActive)
    {
        ...
        SetSuperStruct(Function->GetSuperStruct());
        Script = Function->Script;
        Children = Function->Children;
        ChildProperties = Function->ChildProperties;
        PropertyLink = Function->PropertyLink;

        Function->FunctionFlags |= FUNC_Native;
        Function->SetNativeFunc(&execScriptCallLua);               // ★ 原函数 thunk 改成 Lua 调度入口
        Function->GetOuterUClass()->AddNativeFunction(*Function->GetName(), &execScriptCallLua);
        Function->Script.Empty();
        Function->Script.AddUninitialized(ScriptMagicHeaderSize + sizeof(ULuaFunction*));
        const auto Data = Function->Script.GetData();
        FPlatformMemory::Memcpy(Data, ScriptMagicHeader, ScriptMagicHeaderSize);
        FPlatformMemory::WriteUnaligned<ULuaFunction*>(Data + ScriptMagicHeaderSize, this); // ★ 把 ULuaFunction 指针塞回 Script
    }
    ...
}
```

关键源码 [3]：`Angelscript` 在预处理和 mixin 签名阶段就把 override 语义“锁死”

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp
// 函数: 处理 UFUNCTION specifiers
// 行号: 1575-1588, 1654-1681
// 位置: BlueprintOverride 的合法性在预处理阶段就被严格约束
// ============================================================================
else if (Spec.Name == PP_NAME_NetMulticast || Spec.Name == PP_NAME_NetServer || Spec.Name == PP_NAME_NetClient)
{
    if (FunctionDesc->bBlueprintOverride)
    {
        MacroError(File, Macro, FString::Printf(TEXT("UFUNCTION() %s cannot both be BlueprintOverride and have network specifiers"), *FunctionDesc->FunctionName));
        bHasError = true;
        continue;                                                  // ★ 覆写和网络语义不能混用
    }
    ...
}
else if (Spec.Name == PP_NAME_BlueprintOverride)
{
    if (FunctionDesc->bIsStatic)
    {
        MacroError(File, Macro, FString::Printf(TEXT("Global UFUNCTION() %s may not be BlueprintOverride."), *FunctionDesc->FunctionName));
        bHasError = true;
        continue;                                                  // ★ static 直接禁止
    }
    ...
    FunctionDesc->bBlueprintEvent = true;
    FunctionDesc->bBlueprintOverride = true;
    FunctionDesc->ScriptFunctionName += TEXT("_Implementation");   // ★ 在脚本名层面避免冲突
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h
// 函数: FAngelscriptFunctionSignature::InitFromFunction
// 行号: 276-305
// 位置: ScriptMixin 不是运行时 patch，而是签名构建时把 static Unreal API 改写成成员函数
// ============================================================================
bool bFoundMixin = false;
const FString& MixinClasses = Function->GetOuterUClass()->GetMetaData(NAME_Signature_ScriptMixin);
if (MixinClasses.Len() != 0 && ArgumentTypes.Num() > 0
    && (ArgumentTypes[0].IsObjectPointer() || ArgumentTypes[0].bIsReference))
{
    ...
    if (FirstParamType == Mixin)
    {
        if (ArgumentTypes[0].bIsConst)
            bForceConst = true;

        ArgumentTypes.RemoveAt(0);
        ArgumentNames.RemoveAt(0);
        ArgumentDefaults.RemoveAt(0);
        ClassName = Mixin;
        bStaticInScript = false;                                   // ★ 这里把 static Unreal 函数改写成脚本成员函数
        bFoundMixin = true;
        ...
    }
}
```

新增对比结论：

- `UnLua` 的 Blueprint 交互核心是“扫描既有行为面后做运行时 patch”；`RepNotify`、`AnimNotify_*`、输入绑定都属于这条思路的延伸。
- `Angelscript` 的核心是“在脚本声明阶段确定 override / mixin 语义，再生成稳定签名和桥接”；输入层也更偏显式 API，而不是命名约定自动接管。
- 差距判断：
  - “对现有 Blueprint 资产做低侵入运行时接管”属于 `实现方式不同`
  - “override 语义是否在编译前就可验证、可拒绝冲突组合”这点上，`Angelscript` 明显更强，属于 `实现质量差异`

### [维度 D4] 热重载的真正分野：`UnLua` 修补 Lua 对象图，`Angelscript` 管理编译事务与失败队列

前文把 `UnLua` 热重载概括成 `require` 刷新，这一轮补充其真正复杂度：C++ 侧几乎只是触发器。`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:448-450` 只有一行 `DoString("UnLua.HotReload()")`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorFunctionLibrary.cpp:27-36,112-118` 也只是 `DirectoryWatcher` 发现 Lua 文件变化后，在 `Auto` 模式下调用同一入口。真正的热重载策略全部下沉在 `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua`。

`HotReload.lua` 做的不是简单 `package.loaded[name] = nil`。`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:114-169` 先记录 `loaded_module_times`，并用 `debug.setupvalue(chunk, 1, env)` 把新 chunk 装进 sandbox；`...:258-365` 再枚举新旧函数的 upvalue；`...:367-478` 会继续扫描运行栈、本地变量、`_G`、registry、userdata uservalue；`...:553-625` 才根据时间戳选择被改动模块并执行 merge。换句话说，`UnLua` 的热重载强项不是“入口轻”，而是**在 Lua VM 内部尽量修补现有对象图**。

`Angelscript` 的重点则在另一端。`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3936-4005` 把 reload 明确分成 `SoftReload`、`FullReloadSuggested`、`FullReloadRequired`、`Error`；`...:4168-4187` 还会把需要延后的 full reload 文件写入 `QueuedFullReloadFiles`，失败文件写入 `PreviouslyFailedReloadFiles`。并且 `Plugins/Angelscript/Source/AngelscriptRuntime/Testing/UnitTest.cpp:531-650` 会在 hot reload 后按 batch 执行单元测试，`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp:981-1013` 还能把公开 reload 队列导出为 `HotReloadState.csv`。因此 `Angelscript` 的强项不是“更细粒度替换闭包”，而是**失败恢复路径、测试挂钩和可观测性都更明确**。

```
[D4-Deep] Reload Responsibility Boundary
UnLua
├─ DirectoryWatcher / manual menu                    // 入口只是触发器
├─ HotReload.lua sandbox.load()                      // 在隔离 env 中加载新模块
├─ match_module + match_upvalues                     // 计算旧函数/新函数/upvalue 映射
├─ merge_objects + update_global                     // ★ 修补 stack/_G/registry/uservalue 引用
└─ on script error -> sandbox.exit()                 // 失败恢复主要停留在 Lua 侧

Angelscript
├─ Compile + ClassGenerator.Setup()                  // 先决定 soft/full/error
├─ SwapInModules + PerformSoftReload/FullReload      // 事务式替换模块
├─ QueuedFullReloadFiles / PreviouslyFailedReloadFiles
├─ FHotReloadTestRunner batches unit tests
└─ DumpHotReloadState() exports public queues        // 公开可观测状态
```

关键源码 [1]：`UnLua` 热重载真正做的是“修补旧对象图”，不是简单重新 `require`

```lua
-- ============================================================================
-- 文件: Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua
-- 函数: update_modules
-- 行号: 480-547
-- 位置: 先合并模块值映射，再把运行中的引用从旧函数/旧表替换到新对象
-- ============================================================================
local function update_modules(old_modules, new_modules, new_envs)
    local result = {}
    for i, old_module in ipairs(old_modules) do
        local new_module = new_modules[i]
        ...
        moduleres.values = match_module(new_module_info, old_module)
        ...
        moduleres.upvalue_map = match_upvalues(moduleres.values, old_module_upvalues) -- ★ 先求出 upvalue 替换表
        result[i] = moduleres
    end

    merge_objects(result)                                                        -- ★ 旧 module 表就地更新
    local all_value_maps = {}
    for _, rv in ipairs(result) do
        for _, v in ipairs(rv.values) do
            for name, value_map in pairs(v) do
                for key, value in pairs(value_map) do
                    all_value_maps[key] = value
                end
            end
        end
    end

    update_global(all_value_maps)                                                -- ★ 继续扫描 stack/_G/registry/userdata
end
```

关键源码 [2]：`Angelscript` 把 reload 当作编译事务处理，并明确区分可延后与不可延后失败

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: FAngelscriptEngine::CompileModules (reload decision path)
// 行号: 3936-3997, 4168-4187
// 位置: class generation 先给出 reload requirement，再决定是立即替换还是排队等待 full reload
// ============================================================================
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
            ClassGenerator.PerformSoftReload();                       // ★ 先 soft reload，稍后再 full reload
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
            bFullReloadRequired = true;                               // ★ 当前不能 full reload，就保持旧代码继续运行
        }
        else
        {
            SwapInModules(CompiledModules, DiscardedModules);
            ClassGenerator.PerformFullReload();
        }
        break;
    ...
}

if (Result == ECompileResult::ErrorNeedFullReload)
{
    for (const auto& RepeatFile : AllCompiledFiles)
        QueuedFullReloadFiles.Add(RepeatFile);                        // ★ 需要稍后补做 full reload 的文件进入队列
    PreviouslyFailedReloadFiles.Append(AllCompiledFiles);
}
else if (Result == ECompileResult::Error)
{
    PreviouslyFailedReloadFiles.Append(AllCompiledFiles);             // ★ 失败文件保留，等待下轮自动重试
}
else if (Result == ECompileResult::PartiallyHandled)
{
    for (const auto& RepeatFile : AllCompiledFiles)
        QueuedFullReloadFiles.Add(RepeatFile);
}
```

新增对比结论：

- `UnLua` 热重载的难点在 Lua VM 内部状态迁移：module table、function upvalue、运行栈、本地变量、registry 都可能被补丁式更新。
- `Angelscript` 热重载的难点在 Unreal 类型系统事务：什么时候允许 soft reload，什么时候必须 full reload，失败文件如何排队，reload 后如何自动回归测试。
- 差距判断：
  - “闭包 / upvalue / 栈帧级别的对象图修补”相对 `UnLua`，`Angelscript` 当前属于 `没有实现同层机制`
  - “失败恢复、队列化延期、测试挂钩、状态导出”相对 `UnLua`，`Angelscript` 属于 `实现方式不同`，且从源码证据看完成度更高

本轮新增结论只补前文未展开的内部机制，不改变前面的总体判断：`UnLua` 更擅长把脚本低摩擦地贴到现有 UE 资产与运行中 Lua 状态上；`Angelscript` 更擅长把脚本当成第一等类型资产来管理其编译、覆写语义和 reload 事务。

---

## 深化分析 (2026-04-08 18:45:35)

### [维度 D5] 调试链路的真实边界：`UnLua` 把值展开做进 runtime，把会话层外包给外部 Lua 调试器

前文已经指出当前快照中的 `UnLua` 调试入口依赖 `LuaPanda / LuaHelper`。这一轮补的是更细的层次划分：`UnLua` 仓内并不是只有一篇调试文档，而是已经实现了相当完整的“调试数据面”。`Reference/UnLua/Plugins/UnLua/Source/UnLua/UnLua.Build.cs:91-96` 和 `.../UnLuaEditorSettings.h:57-63` 说明 `bEnableDebug` 是编译期开关；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaDebugBase.cpp:206-307,429-515,614-696` 则把 `userdata.__name`、`FProperty`、Lua locals / upvalues / call stack 统一抽成可读调试值。也就是说，`UnLua` 当前快照把“如何看到值”这件事放在了插件内核里。

缺的部分是“如何发消息、如何建会话、如何完成断点协议”。`Reference/UnLua/Docs/CN/Debugging.md:5-14` 明确要求用户把 `LuaPanda.lua` 放进工程脚本目录，并在 Lua 脚本里主动 `require("LuaPanda").start(...)`。因此，当前快照里 `UnLua` 的仓内能力更像 **debug state provider**，而不是 **debug session host**。对比之下，`Angelscript` 的 `DebugServer V2` 把 envelope、版本握手、line callback、PauseExecution、协议 round-trip test 都做在了仓内，调试会话边界更闭合。

```
[D5-Deep] Debug Layer Boundary
UnLua
├─ Build flag UNLUA_ENABLE_DEBUG                   // 编译期开关
├─ UnLuaDebugBase                                  // locals/upvalues/callstack/value expansion
├─ lua hook reuse                                  // 超时保护与 trace 都复用行级 hook
└─ LuaPanda / LuaHelper / LuaSocket               // ★ 会话、传输、DAP 适配在仓外

Angelscript
├─ Debug value reification                         // 容器和值类型展开
├─ DebugServer protocol envelopes                  // StartDebugging / Version / transport
├─ Engine line callback -> ProcessScriptLine       // 行回调直接接到停机逻辑
└─ Protocol + smoke tests                          // ★ 协议和会话链路可回归
```

关键源码 [1]：`UnLua` 仓内已实现“值展开 + 栈信息提取”，但它停在数据层

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaDebugBase.cpp
// 函数: FLuaDebugValue::BuildFromUserdata / BuildFromUProperty / GetStackVariables
// 行号: 213-235, 245-284, 621-665
// 位置: 调试值展开既能看 Lua 局部变量，也能深入 UE 容器与 UObject 字段
// ============================================================================
if (lua_getmetatable(L, Index) > 0)
{
    lua_pushstring(L, "__name");
    lua_rawget(L, -2);
    if (lua_isstring(L, -1))
    {
        ClassNamePtr = lua_tostring(L, -1);                    // ★ 从 metatable 取出桥接后的 UE 类型名
    }
    lua_pop(L, 2);
}

if (ClassName == TEXT("TArray"))
{
    FLuaArray *Array = (FLuaArray*)ContainerPtr;
    FProperty *InnerProperty = Array->Inner->GetUProperty();
    FScriptArrayHelper ArrayHelper = FScriptArrayHelper::CreateHelperFormInnerProperty(InnerProperty, Array->ScriptArray);
    BuildFromTArray(ArrayHelper, InnerProperty);               // ★ 继续按 FProperty 展开容器元素
}
else if (ClassName == TEXT("TMap"))
{
    FLuaMap *Map = (FLuaMap*)ContainerPtr;
    FProperty *KeyProperty = Map->KeyInterface->GetUProperty();
    FProperty *ValueProperty = Map->ValueInterface->GetUProperty();
    FScriptMapHelper MapHelper = FScriptMapHelper::CreateHelperFormInnerProperties(KeyProperty, ValueProperty, Map->Map);
    BuildFromTMap(MapHelper);                                  // ★ 容器调试值不只是打印地址
}

if (!lua_getstack(L, StackLevel, &ar) || !lua_getinfo(L, "nSlu", &ar))
    return false;

const char *VarName = lua_getlocal(L, &ar, i);                // ★ 提取 locals
const char *UpvalueName = lua_getupvalue(L, FunctionIdx, i);  // ★ 提取 upvalues
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaDeadLoopCheck.cpp
// 函数: FDeadLoopCheck::FGuard::SetTimeout / OnLuaLineEvent
// 行号: 102-113
// 位置: 仓内还直接复用了 Lua line hook 做超时保护
// ============================================================================
void FDeadLoopCheck::FGuard::SetTimeout()
{
    const auto L = Owner->Env->GetMainState();
    const auto Hook = lua_gethook(L);
    if (Hook == nullptr)
        lua_sethook(L, OnLuaLineEvent, LUA_MASKLINE, 0);       // ★ 进入行级 hook
}

void FDeadLoopCheck::FGuard::OnLuaLineEvent(lua_State* L, lua_Debug* ar)
{
    lua_sethook(L, nullptr, 0, 0);
    luaL_error(L, "lua script exec timeout");                  // ★ 说明 hook 已进入运行时控制面
}
```

关键源码 [2]：`UnLua` 的调试会话入口在仓外；`Angelscript` 则把协议和停机逻辑做进仓内

```markdown
<!-- =========================================================================
文件: Reference/UnLua/Docs/CN/Debugging.md
函数: 调试接入说明
行号: 5-14
位置: 调试会话通过外部 Lua 调试器建立，而不是由 UnLua runtime 直接托管
=========================================================================== -->
1. 从VSCode应用市场安装 LuaPanda / LuaHelper
2. 从 LuaPanda 官方仓库获取 `LuaPanda.lua`，放入 `{UE工程}/Content/Script`
3. 在 Lua 代码中加入 `require("LuaPanda").start("127.0.0.1",8818)`   <!-- ★ 会话建立发生在仓外脚本 -->

注：调试器依赖 `luasocket`，UnLua 已通过扩展插件集成
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 函数: FAngelscriptDebugServer::ProcessScriptLine / StartDebugging handler
// 行号: 465-545, 897-910
// 位置: 协议握手、停机与断点管理都由仓内 DebugServer 接管
// ============================================================================
void FAngelscriptDebugServer::ProcessScriptLine(class asCContext* Context)
{
    if (!bIsDebugging || bIsPaused || IsEngineExitRequested())
        return;

    if (DataBreakpoints.Num() > 0 && bBreakNextScriptLine)
    {
        ...
        FStoppedMessage Msg;
        Msg.Text = InfoText;
        Msg.Reason = TEXT("exception");
        PauseExecution(&Msg);                                  // ★ 行回调直接停机并通知客户端
    }
}

else if (MessageType == EDebugMessageType::StartDebugging)
{
    FStartDebuggingMessage Msg;
    *Datagram << Msg;

    bIsDebugging = true;
    AngelscriptDebugServer::DebugAdapterVersion = Msg.DebugAdapterVersion;

    FDebugServerVersionMessage DebugServerVersionMessage;
    DebugServerVersionMessage.DebugServerVersion = DEBUG_SERVER_VERSION;
    SendMessageToClient(Client, EDebugMessageType::DebugServerVersion, DebugServerVersionMessage); // ★ 仓内完成版本握手
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/Helper_Reification.h
// 函数: ReifyDebugValueTemplate
// 行号: 56-68
// 位置: Angelscript 也有值展开层，但它与 DebugServer 在同一套仓内调试栈里
// ============================================================================
FASDebugValue* ReifyDebugValueTemplate(int32 ReifyType, FDebugValuePrototype& Values, int32 Offset, int32 GenericSize)
{
    switch ((EReifiedType)ReifyType)
    {
    case EReifiedType::Unknown:
    default:
        return Values.Create<TGenericValue>(Offset, GenericSize);
    #define REIFY(TYPE, ENUM) \
    case EReifiedType::ENUM:\
        return Values.Create<TTemplateValue<TYPE>>(Offset);    // ★ 为调试器创建强类型 debug value
    REIFIED_TYPES
    #undef REIFY
    }
}
```

新增对比结论：

- `UnLua` 当前快照不是“没有仓内调试能力”，而是已经有 `debug value + call stack + line hook` 的数据层，只是把调试会话和协议适配留给了 `LuaPanda / LuaHelper`。
- `Angelscript` 的 `DebugServer V2` 强项不是单纯“支持断点”，而是把 `协议消息 -> line callback -> stop reason -> value reification -> 协议测试` 做成了一条仓内闭环。
- 差距判断：
- “仓内自带调试会话/协议服务”相对 `Angelscript`，`UnLua` 当前快照属于 `没有实现同层能力`
- “运行时值展开与栈信息采集”不是没有，而是 `实现方式不同`

### [维度 D9] 测试运行模型的新增发现：`UnLua` 是插件自带 harness，`Angelscript` 是编译生命周期内建测试系统

前文已经覆盖了 `UnLuaTestSuite` 独立插件这一事实，这一轮补的是“它具体怎么跑”。`Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Public/UnLuaTestCommon.h:162-212` 把测试骨架分成 `IMPLEMENT_UNLUA_LATENT_TEST`、`IMPLEMENT_UNLUA_INSTANT_TEST` 和 `BEGIN_TESTSUITE` 三类；`.../Private/UnLuaTestCommon.cpp:60-97` 又在 `SetUp()` 里统一启动 `UnLua`、创建 `UGameInstance`、决定是临时世界还是 `AutomationOpenMap`。也就是说，`UnLua` 的测试模型是“把 UE Automation 的样板代码包起来，让插件使用者更容易写 issue 回归”。

进一步看，`Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Specs/*.spec.cpp` 用 spec 风格测 API / setting，`.../Private/Tests/BindingTest.cpp` 直接跑 Lua chunk 验证静态/动态绑定，`.../Private/Tests/IssueOverridesTest.cpp` 用真实地图和蓝图资产回放线上 bug，`.../Private/Perfs/UnLuaBenchmarkFunctionLibrary.cpp` 还会把 benchmark 写成 CSV。它的层次其实不浅，但 orchestration 仍然是显式注册、显式挑选，和编译/热重载链没有直接耦合。

`Angelscript` 的结构刚好相反。`Plugins/Angelscript/Source/AngelscriptRuntime/Testing/DiscoverTests.cpp:191-208` 会按 `IntegrationTest_` / `ComplexIntegrationTest_` 自动发现脚本测试；`.../Testing/UnitTest.cpp:531-652` 在 hot reload 后只挑受影响模块并分 batch 执行；`.../Testing/LatentAutomationCommandClientExecutor.cpp:237-258` 甚至把 client/server latent test executor 做成可复制 actor。这说明 `Angelscript` 的测试不是外挂在仓库边上的“验证件”，而是编译生命周期里的一等成员。

```
[D9-Deep] Test Execution Model
UnLua
├─ Spec (*.spec.cpp)                               // API / setting specs
├─ Instant/Latent macros                           // 显式声明测试骨架
├─ OpenMap + asset-backed regressions              // 地图/蓝图回归
├─ Benchmark csv writer                            // 性能结果落盘
└─ Manual selection                                // ★ 测试发现与编译链未直接耦合

Angelscript
├─ DiscoverTests() by naming convention            // 从脚本符号发现测试
├─ HotReloadTestRunner::PrepareTests               // 只挑受影响模块
├─ RunTests in batches + GC                        // 热重载后分批执行
└─ LatentAutomationCommandClientExecutor           // ★ 支持网络化集成测试
```

关键源码 [1]：`UnLua` 的 test harness 重点是把 UE Automation 包装成插件友好的固定套路

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Public/UnLuaTestCommon.h
// 函数: IMPLEMENT_UNLUA_LATENT_TEST / IMPLEMENT_UNLUA_INSTANT_TEST
// 行号: 171-205
// 位置: latent / instant / testsuite 三套骨架统一封装
// ============================================================================
#define IMPLEMENT_UNLUA_LATENT_TEST(TestClass, PrettyName) \
IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass##_Runner, PrettyName, (EAutomationTestFlags::ClientContext | EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)) \
bool TestClass##_Runner::RunTest(const FString& Parameters) \
{ \
TestClass* TestInstance = new TestClass(); \
TestInstance->SetTestRunner(*this); \
ADD_LATENT_AUTOMATION_COMMAND(FUnLuaTestCommand_SetUpTest(TestInstance)); \
ADD_LATENT_AUTOMATION_COMMAND(FUnLuaTestCommand_PerformTest(TestInstance)); \
ADD_LATENT_AUTOMATION_COMMAND(FUnLuaTestCommand_TearDownTest(TestInstance)); \
return true; \
}

#define IMPLEMENT_UNLUA_INSTANT_TEST(TestClass, PrettyName) \
IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass##Runner, PrettyName, (EAutomationTestFlags::ClientContext | EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)) \
bool TestClass##Runner::RunTest(const FString& Parameters) \
{ \
bool bSuccess = false; \
TestClass* TestInstance = new TestClass(); \
TestInstance->SetTestRunner(*this); \
if (TestInstance->SetUp()) \
{ \
bSuccess = TestInstance->InstantTest(); \
TestInstance->TearDown(); \
}\
delete TestInstance; \
return bSuccess; \
}
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/UnLuaTestCommon.cpp
// 函数: FUnLuaTestBase::SetUp
// 行号: 60-97
// 位置: 测试基类统一决定是创建临时 World 还是打开真实地图
// ============================================================================
bool FUnLuaTestBase::SetUp()
{
    UnLua::Startup();
    L = UnLua::GetState();

    GameInstance = NewObject<UGameInstance>(GEngine);
    GameInstance->InitializeStandalone();
    WorldContext = GameInstance->GetWorldContext();

    const auto& MapName = GetMapName();
    if (MapName.IsEmpty())
    {
        if (!WorldContext->World())
        {
            const auto World = UWorld::CreateWorld(EWorldType::Game, false, "UnLuaTest");
            World->SetGameInstance(GameInstance);               // ★ 没地图时起一个独立测试世界
            WorldContext->SetCurrentWorld(World);
        }
    }
    else
    {
        if (InstantTest())
            GEngine->LoadMap(*WorldContext, URL, nullptr, Error);
        else
            AutomationOpenMap(MapName);                         // ★ 有地图时切进真实资产回归
    }

    return true;
}
```

关键源码 [2]：`UnLua` 的测试层次横跨 spec、地图回归和 benchmark；`Angelscript` 则把发现与调度都做进 runtime

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Specs/LuaEnv.spec.cpp
// 函数: FLuaEnvSpec::Define
// 行号: 23-31, 34-49
// 位置: spec 风格验证 API 级行为
// ============================================================================
BEGIN_DEFINE_SPEC(FLuaEnvSpec, "UnLua.API.FLuaEnv", EAutomationTestFlags::ProductFilter | EAutomationTestFlags::ApplicationContextMask)
    TSharedPtr<UnLua::FLuaEnv> Env;
END_DEFINE_SPEC(FLuaEnvSpec)

Describe(TEXT("创建Lua环境"), [this]()
{
    It(TEXT("支持多个Lua环境"), EAsyncExecution::TaskGraphMainThread, [this]()
    {
        UnLua::FLuaEnv Env1;
        UnLua::FLuaEnv Env2;
        Env1.DoString("return 1");
        Env2.DoString("return 2");                             // ★ API 级行为不依赖地图资产
        TEST_EQUAL((int32)lua_tointeger(Env1.GetMainState(), -1), 1);
        TEST_EQUAL((int32)lua_tointeger(Env2.GetMainState(), -1), 2);
    });
});
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Tests/IssueOverridesTest.cpp
// 函数: FIssueOverridesTest::RunTest
// 行号: 29-55
// 位置: 回归测试直接加载真实地图和蓝图资源，复放覆写链问题
// ============================================================================
const auto MapName = TEXT("/UnLuaTestSuite/Tests/Regression/IssueOverrides/IssueOverrides.IssueOverrides");
ADD_LATENT_AUTOMATION_COMMAND(FOpenMapLatentCommand(MapName))
ADD_LATENT_AUTOMATION_COMMAND(FEngineWaitLatentCommand(0.5));
ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this] {
    const auto L = UnLua::GetState();
    lua_getglobal(L, "Counter");
    TestEqual(TEXT("Counter"), (int)lua_tointeger(L, -1), 4);

    const auto Obj = NewObject<UIssueOverridesObject>();
    UnLua::PushUObject(L, Obj);
    lua_setglobal(L, "G_IssueObject");
    UnLua::RunChunk(L, "return G_IssueObject:CollectInfo()");  // ★ 地图状态 + Lua 覆写一起回放
    TestEqual(TEXT("Result1"), (int32)lua_tointeger(L, -1), 2);
    return true;
}));
ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Perfs/UnLuaBenchmarkFunctionLibrary.cpp
// 函数: UUnLuaBenchmarkFunctionLibrary::Stop
// 行号: 46-50
// 位置: benchmark 结果直接输出成 CSV，便于插件外部消费
// ============================================================================
void UUnLuaBenchmarkFunctionLibrary::Stop()
{
    const auto Message = FString::Join(Messages, TEXT("\n"));
    const auto FilePath = FString::Printf(TEXT("%sBenchmark/%s-Benchmark-%s.csv"), *FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()), *BenchmarkTitle, *FDateTime::Now().ToString());
    FFileHelper::SaveStringToFile(Message, *FilePath);         // ★ 基准结果直接落盘
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Testing/DiscoverTests.cpp
// 函数: DiscoverIntegrationTests
// 行号: 191-208
// 位置: 通过命名约定自动发现脚本测试，不需要逐个注册
// ============================================================================
void DiscoverIntegrationTests(const FAngelscriptModuleDesc& Module, TMap<FString, FAngelscriptTestDesc>& IntegrationTestFunctions)
{
    TMap<FString, asIScriptFunction*> IntegrationTestRunTestFunctions;
    OneArgFunctionFilter Filter = CreateOneArgFilter("IntegrationTest_", "", "FIntegrationTest");
    DiscoverWithFilter(Module, IntegrationTestRunTestFunctions, Filter);
    RegisterSimpleFunctions(IntegrationTestRunTestFunctions, IntegrationTestFunctions);

    OneArgFunctionFilter ComplexFilter = CreateOneArgFilter("ComplexIntegrationTest_", "", "FIntegrationTest");
    DiscoverWithFilter(Module, ComplexIntegrationTestRunTestFunctions, ComplexFilter); // ★ 发现逻辑直接建在脚本符号上
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Testing/UnitTest.cpp
// 函数: FHotReloadTestRunner::PrepareTests / RunTests
// 行号: 531-590, 599-649
// 位置: hot reload 结束后只挑受影响模块，并按 batch 执行
// ============================================================================
if (!ShouldRunUnitTestsOnHotReload())
{
    return;
}

for (const TSharedRef<FAngelscriptModuleDesc>& Module : ModulesToCompile)
{
    if (Module->UnitTestFunctions.Num() > 0)
    {
        TestAfterHotReload.Add(Module);                        // ★ 热重载后记录待执行模块
    }
}

if (TestAfterHotReload.Num() > 0)
{
    int TestsPerBatch = GetDefault<UAngelscriptTestSettings>()->GarbageCollectEveryNTests;
    ...
    AllUnitTestsPass = RunAngelscriptUnitTests(TestBatch, AngelscriptManager, CurrentBatchOnHotReload, TotalBatchesOnHotReload); // ★ 编译后自动回归
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Testing/LatentAutomationCommandClientExecutor.cpp
// 函数: ALatentAutomationCommandClientExecutor::ReplicateSubobjects / GetLifetimeReplicatedProps
// 行号: 242-258
// 位置: client/server 集成测试可以复制 latent command 到客户端执行
// ============================================================================
bool ALatentAutomationCommandClientExecutor::ReplicateSubobjects(UActorChannel* Channel, FOutBunch* Bunch, FReplicationFlags* RepFlags)
{
    bool bWroteSomething = Super::ReplicateSubobjects(Channel, Bunch, RepFlags);
    bWroteSomething |= Channel->ReplicateSubobject(LatentCommand, *Bunch, *RepFlags); // ★ 测试命令本身可复制
    return bWroteSomething;
}

void ALatentAutomationCommandClientExecutor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    DOREPLIFETIME(ALatentAutomationCommandClientExecutor, LatentCommand);
    DOREPLIFETIME(ALatentAutomationCommandClientExecutor, bCanStartBefore);
    DOREPLIFETIME(ALatentAutomationCommandClientExecutor, bCanStartUpdate);
    DOREPLIFETIME(ALatentAutomationCommandClientExecutor, bCanStartAfter);
}
```

新增对比结论：

- `UnLua` 的测试基础设施并不单薄，它至少覆盖了 `spec -> binding/instant -> map-backed regression -> benchmark` 四层。
- `UnLua` 当前快照更像“插件自带 test harness”，重点是方便插件自身和接入工程复放问题；`Angelscript` 更像“测试是编译与热重载生命周期的一部分”。
- 对 `Reference/UnLua` 代码树做额外清单搜索时，除 `.git` 样例文件外未检出 `.github/workflows`、`appveyor`、`Jenkinsfile`、`gitlab-ci` 等 CI 清单；这只能说明当前仓库快照未暴露 CI 证据，不能反推上游一定没有 CI。
- 差距判断：
- “按脚本命名约定自动发现测试 + hot reload 后按影响面自动回归”相对 `Angelscript`，`UnLua` 当前快照属于 `没有实现同层机制`
- “插件可复用的 test harness + asset-backed regression”相对 `Angelscript` 不是没有，而是 `实现方式不同`

### [维度 D10] 文档闭环的新发现：`UnLua` 的文档是可执行工作流，不只是 README 索引

前文已经总结过 `README + Docs + Tutorials` 的目录结构，这一轮补的是“这些文档如何和编辑器行为真正闭环”。`Reference/UnLua/README.md:35-69` 先把新手导向 `Quickstart_For_UE_Newbie.md`，再把 13 个教程脚本直接列成可点击入口；`Reference/UnLua/Docs/CN/Quickstart_For_UE_Newbie.md:11-30` 又把“绑定 -> 生成模板 -> 打开文件管理器 -> 把 `Content/Script` 加入 IDE”写成图文流程。关键点在于这些文档并不是停在纸面上，而是和 `UnLuaEditor` 的真实按钮、模板文件、commandlet 一一对应。

`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:257-313` 说明“创建 Lua 模板文件”不是一句教程文案，而是实际会读取 `GetModuleName()`、定位 `Config/LuaTemplates/*.lua`、替换 `TemplateName / ClassName` 并生成目标脚本；`.../Config/LuaTemplates/Actor.lua:9-36` 又把常见生命周期 stub 预填进去。再往后，`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp:55-123` 与 `Reference/UnLua/Docs/CN/IntelliSense.md:5-17` 组成了“导出 `Intermediate/IntelliSense` -> IDE 加入工作区 -> 直接看到 `UE.` 补全”的后续链路。这意味着 `UnLua` 的文档组织不是单纯解释功能，而是在组织一个从学习到脚手架再到编码的操作系统。

`Angelscript` 当前仓库也不是没有文档能力，但组织方向不同。`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:528,2224-2226` + `.../Core/AngelscriptDocs.cpp:407-457,682-721` 显示它更擅长生成机器可消费的 API 参考头文件；`Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleActorTest.cpp:9-33` 也内嵌了高质量示例脚本，只是这些示例被包在 automation test 里，而不是像 `UnLua` 那样直接以用户脚本文件形态出现在 `Content/Script/Tutorials/`。

```
[D10-Deep] Learn -> Scaffold -> Run Loop
UnLua
├─ README / Quickstart                             // 新手入口
├─ Toolbar.Bind + CreateLuaTemplate               // 编辑器生成脚手架
├─ Config/LuaTemplates/*.lua                      // 类别化模板
├─ Tutorials/*.lua                                // 可直接运行的脚本示例
└─ IntelliSense commandlet + workspace doc        // ★ 写代码阶段继续给提示

Angelscript
├─ Example test sources embed .as strings         // 示例主要活在测试里
├─ DumpDocumentation(-dump-as-doc)                // 生成 API 参考
└─ Macro/test guides                              // 偏维护者与测试作者导向
```

关键源码 [1]：`UnLua` 的文档入口直接对应编辑器动作和生成产物

```markdown
<!-- =========================================================================
文件: Reference/UnLua/README.md
函数: 开始UnLua之旅 / 更多示例 / 文档
行号: 35-69
位置: README 不只是索引，还明确把用户引向模板生成、教程脚本和专题文档
=========================================================================== -->
**注意**: 如果你是一位UE萌新，推荐使用更详细的 [图文版教学](Docs/CN/Quickstart_For_UE_Newbie.md)
1. 在 UnLua 工具栏中选择 `绑定`
2. 在 `GetModule` 函数中填入 Lua 文件路径
3. 选择 `创建Lua模版文件`
4. 打开 `Content/Script/...` 编写代码

更多示例：
- `07_CallLatentFunction`
- `10_Replications`
- `12_CustomLoader`

详细介绍：
- `UnLua_Programming_Guide.md`
- `How_To_Implement_Overriding.md`
- `API.md`
```

```markdown
<!-- =========================================================================
文件: Reference/UnLua/Docs/CN/Quickstart_For_UE_Newbie.md
函数: 快速入门示例
行号: 11-30
位置: 图文教程直接描述编辑器按钮与 IDE 组织方式
=========================================================================== -->
## 2. 绑定到Lua
- 点击 UnLua 菜单栏中的“绑定”，默认会自动根据蓝图路径填充 Lua 模块路径
- 如果需要修改绑定路径，可以找到 `GetModuleName` 函数并双击修改

## 3. 生成Lua模版代码
点击 UnLua 菜单栏中的“生成Lua模板文件”，会在工程 `Content/Script` 目录下生成

最后，用你喜欢的编辑器打开生成的文件，开始编写你的代码吧
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp
// 函数: FUnLuaEditorToolbar::CreateLuaTemplate_Executed
// 行号: 265-310
// 位置: 教程中的“生成模板”会真实读取 module 名、模板文件并写出目标 Lua 文件
// ============================================================================
const auto Func = Class->FindFunctionByName(FName("GetModuleName"));
Class->GetDefaultObject()->ProcessEvent(Func, &ModuleName);    // ★ 从 Blueprint 真实读取 module path

const auto RelativePath = ModuleName.Replace(TEXT("."), TEXT("/"));
const auto FileName = FString::Printf(TEXT("%s%s.lua"), *GLuaSrcFullPath, *RelativePath);

auto RelativeFilePath = "Config/LuaTemplates" / TemplateClassName + ".lua";
auto FullFilePath = FPaths::ProjectConfigDir() / RelativeFilePath;
if (!FPaths::FileExists(FullFilePath))
    FullFilePath = BaseDir / RelativeFilePath;

FFileHelper::LoadFileToString(Content, *FullFilePath);
Content = Content.Replace(TEXT("TemplateName"), *TemplateName)
                 .Replace(TEXT("ClassName"), *UnLua::IntelliSense::GetTypeName(Class));
FFileHelper::SaveStringToFile(Content, *FileName, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM); // ★ 真正生成脚手架
```

```lua
-- ============================================================================
-- 文件: Reference/UnLua/Plugins/UnLua/Config/LuaTemplates/Actor.lua
-- 函数: Actor template
-- 行号: 9-36
-- 位置: 模板本身就是教学脚手架，直接预填常见生命周期函数
-- ============================================================================
---@type ClassName
local M = UnLua.Class()

-- function M:Initialize(Initializer)
-- end

-- function M:ReceiveBeginPlay()
-- end

-- function M:ReceiveTick(DeltaSeconds)
-- end

-- function M:ReceiveActorBeginOverlap(OtherActor)
-- end

return M
```

关键源码 [2]：`UnLua` 后续写码链路继续由 IntelliSense 和教程脚本承接；`Angelscript` 更偏 API dump + test-embedded example

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp
// 函数: UUnLuaIntelliSenseCommandlet::Main / SaveFile
// 行号: 55-123
// 位置: 反射类型、导出函数和 Blueprint 提示都能批量生成为 IDE 产物
// ============================================================================
const auto ExportedReflectedClasses = UnLua::GetExportedReflectedClasses();
const auto ExportedNonReflectedClasses = UnLua::GetExportedNonReflectedClasses();
const auto ExportedEnums = UnLua::GetExportedEnums();
const auto ExportedFunctions = UnLua::GetExportedFunctions();

for (auto Pair : ExportedReflectedClasses)
{
    Pair.Value->GenerateIntelliSense(GeneratedFileContent);
    SaveFile(ModuleName, Pair.Key, GeneratedFileContent);      // ★ 每个类型单独生成提示文件
}

if (ParamsMap.Contains(BPKey) && ParamsMap[BPKey] == TEXT("1"))
{
    auto Generator = FUnLuaIntelliSenseGenerator::Get();
    Generator->Initialize();
    Generator->UpdateAll();                                    // ★ Blueprint 侧提示也能顺带刷新
}

FString Directory = FString::Printf(TEXT("%sIntelliSense/%s"), *IntermediateDir, *ModuleName);
FileManager.MakeDirectory(*Directory);                         // ★ 产物落到 Intermediate/IntelliSense
```

```markdown
<!-- =========================================================================
文件: Reference/UnLua/Docs/CN/IntelliSense.md
函数: 智能提示使用说明
行号: 5-17
位置: 文档明确告诉用户如何把生成产物接进 IDE 工作区
=========================================================================== -->
打开 UnLua 工具栏，点击导出智能提示，会在 `{UE工程}/Plugins/UnLua/Intermediate` 下生成 `IntelliSense`

以 VSCode 为例，一个工作区中分别添加 `Script` 和 `IntelliSense`

验证智能提示是否工作，只需要在任意 lua 文件中输入 `UE.`
```

```lua
-- ============================================================================
-- 文件: Reference/UnLua/Content/Script/Tutorials/07_CallLatentFunction.lua
-- 函数: tutorial body
-- 行号: 1-24
-- 位置: 教程脚本本身会自报示例来源，直接可运行而不是只存在于文档描述里
-- ============================================================================
--[[
    说明：在Lua协程中可以方便的使用UE4的Latent函数实现延迟执行的效果
]] --

local Screen = require "Tutorials.Screen"
local M = UnLua.Class()

function M:ReceiveBeginPlay()
    local msg = [[
    —— 本示例来自 "Content/Script/Tutorials.07_CallLatentFunction.lua"
    ]]
    Screen.Print(msg)                                          // ★ 运行时直接告诉用户自己正在看的示例来源
end
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleActorTest.cpp
// 函数: GActorExample
// 行号: 9-33
// 位置: Angelscript 的高质量示例目前主要作为测试输入内嵌在 cpp 文件里
// ============================================================================
const AngelscriptScriptExamples::FScriptExampleSource GActorExample = {
    TEXT("Example_Actor.as"),
    TEXT(R"ANGELSCRIPT(/*
 * Script classes can always derive from the same classes that
 * blueprints can be derived from.
 */

// For example, we can make a new Actor class
class AExampleActor_UnitTest : AActor
{
    UPROPERTY()
    int ExampleValue = 15;

    UFUNCTION()
    void BlueprintAccessibleMethod()
    {
        Log("BlueprintAccessibleMethod Called");
    }
})ANGELSCRIPT")
};                                                             // ★ 示例质量很高，但消费入口仍在测试代码里
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: command-line config parsing / initial compile
// 行号: 528, 2224-2226
// 位置: 文档导出主要由命令行 flag 触发，偏 API reference 生产流程
// ============================================================================
Config.bDumpDocumentation = FParse::Param(FCommandLine::Get(), TEXT("dump-as-doc"));

if (RuntimeConfig.bDumpDocumentation)
{
    FAngelscriptDocs::DumpDocumentation(Engine);               // ★ 通过运行时 flag 导出文档
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp
// 函数: FAngelscriptDocs::DumpDocumentation
// 行号: 682-712
// 位置: 导出的文档更像自动生成的 API 参考头文件，而不是新手教程
// ============================================================================
FString Filename = FPaths::ProjectDir() / TEXT("/Docs/angelscript/generated") / ClassDoc.ClassName + TEXT(".hpp");

Content += FString::Printf(TEXT("/* Class: %s \n %s */ \n class %s"),
    *ClassDoc.ClassName, *ClassDoc.Documentation, *ClassDoc.ClassName);

for (FDocProperty& PropDoc : ClassDoc.Properties)
{
    Content += FString::Printf(TEXT("\n/* Variable: %s \n %s */\n"), *PropDoc.Name, *PropDoc.Documentation);
    Content += FString::Printf(TEXT("%s;"), *PropDoc.Declaration); // ★ 产物偏参考手册，不是教程脚手架
}
```

新增对比结论：

- `UnLua` 在 `D10` 的核心优势不只是文档多，而是把 `README -> 图文 Quickstart -> Editor 按钮 -> Lua template -> IntelliSense -> Tutorials` 串成了一条可执行学习链。
- `Angelscript` 现有的例子并不少，`Examples/*.cpp` 里的脚本片段质量也很高；但它们主要服务于测试与维护，而不是直接作为用户工作区里的脚本样例分发。
- `Angelscript` 的 `DumpDocumentation` 更像自动 API reference 生成器；它解决的是“如何把符号表导出成文档”，不是“如何让新用户 30 分钟内跑通第一个脚本类”。
- 差距判断：
- “文档是否存在”不是差距，属于 `实现方式不同`
- “从上手说明到脚手架生成再到 IDE 提示是否形成闭环”这点上，`UnLua` 相对当前 `Angelscript` 明显更强，属于 `实现质量差异`

---

## 深化分析 (2026-04-08 18:57:09)

本轮只补前文没有掰开的 6 个边界：`D2` 的 marshaller 边界、`D3` 的覆写所有权、`D4` 的 reload 升级条件、`D5` 的调试器归属、`D9` 的测试入口 DSL、`D10` 的教程与资产双向映射。

### [维度 D2] 零胶水的真实边界：`UClass/UFunction` 零注册，不等于调用期零桥接

前文已经说明 `UnLua` 以 `UStruct / UFunction / FProperty` 为主反射入口，这一轮补的是“零胶水到底零到哪一层”。从 `FClassDesc::RegisterField()` 和 `FFunctionDesc::PreCall()/PostCall()` 可以看到，`UnLua` 确实不要求为每个 `UClass` 写 `Bind_*.cpp`，但它仍然要在首次访问字段时构建 `FPropertyDesc` / `FFunctionDesc`，并在每次反射调用时遍历 `Properties[]` 做参数写入、`out`/返回值拷回和清理。换句话说，它省掉的是“暴露清单的手工维护”，不是“类型桥接”的实现成本。

`Angelscript` 这边的主路径恰好反过来：`Bind_AActor.cpp` 这种静态绑定把签名成本前移到开发期；只有 `BlueprintCallableReflectiveFallback` 这条补洞路径，才会像 `UnLua` 一样在调用时遍历 `FProperty`、组装参数 buffer 并 `ProcessEvent()`。这意味着两边的核心差异不是有没有反射，而是谁把成本放在“写绑定”还是“跑调用”。

```
[D2-Deep] Exposure Cost vs Call Cost
UnLua
├─ [1] RegisterField(name)                        // 首次访问字段才建桥
│  ├─ FindPropertyByName / FindFunctionByName     // 直接查 UE 反射对象
│  ├─ FPropertyDesc::Create(...)                  // 按 FProperty 类型挑 marshaller
│  └─ FFunctionDesc(...)                          // 运行时生成函数描述
└─ [2] Reflected call
   ├─ PreCall loop Properties[]                   // 每次调用都写参数
   └─ PostCall loop Out/Return                    // 每次调用都做 copy-back

Angelscript
├─ [1] Bind_*.cpp static signatures               // 先把热点 API 写死
│  └─ ExistingClass(...).Method(...)              // 默认直绑路径
└─ [2] Reflective fallback only
   ├─ Iterate FProperty                           // 仅 fallback 走通用桥
   ├─ CopySingleValue / ProcessEvent              // 运行时参数封送
   └─ Copy back ref/out/return                    // 回填脚本值
```

关键源码 [1]：`UnLua` 把字段发现与 marshaller 创建延迟到首次访问

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/ClassDesc.cpp
// 函数: FClassDesc::RegisterField
// 行号: 56-139
// 位置: 首次访问字段时，按名称查询 FProperty / UFunction 并缓存对应描述器
// ============================================================================
TSharedPtr<FFieldDesc> FClassDesc::RegisterField(FName FieldName, FClassDesc* QueryClass)
{
    Load();

    TSharedPtr<FFieldDesc>* FieldDescPtr = Fields.Find(FieldName);
    if (FieldDescPtr)
    {
        return *FieldDescPtr;                                  // ★ 第二次访问直接命中缓存
    }

    FProperty* Property = Struct->FindPropertyByName(FieldName);
    UFunction* Function = (!Property && bIsClass) ? AsClass()->FindFunctionByName(FieldName) : nullptr;
    if (!Property && !Function)
        return nullptr;

    if (Property)
    {
        TSharedPtr<FPropertyDesc> Ptr(FPropertyDesc::Create(Property)); // ★ 按 FProperty 真类型创建桥接器
        FieldDesc->FieldIndex = Properties.Add(Ptr);
    }
    else
    {
        FParameterCollection* DefaultParams = FunctionCollection ? FunctionCollection->Functions.Find(FieldName) : nullptr;
        FieldDesc->FieldIndex = Functions.Add(MakeShared<FFunctionDesc>(Function, DefaultParams)); // ★ 函数桥同样是运行时生成
    }
}
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp
// 函数: FFunctionDesc::PreCall / PostCall
// 行号: 279-409
// 位置: 反射调用并不是“无桥接”；每次调用都要遍历属性做参数封送和 copy-back
// ============================================================================
void FFunctionDesc::PreCall(lua_State* L, int32 NumParams, int32 FirstParamIndex, FFlagArray& CleanupFlags, void* Params, void* Userdata)
{
    int32 ParamIndex = 0;
    for (int32 i = 0; i < Properties.Num(); ++i)
    {
        const auto& Property = Properties[i];
        Property->InitializeValue(Params);                     // ★ 先构造参数槽

        if (ParamIndex < NumParams)
        {
            CleanupFlags[i] = Property->WriteValue_InContainer(L, Params, FirstParamIndex + ParamIndex, false); // ★ Lua -> UE
        }
        else if (!Property->IsOutParameter() && DefaultParams)
        {
            IParamValue **DefaultValue = DefaultParams->Parameters.Find(Property->GetProperty()->GetFName());
            if (DefaultValue)
            {
                Property->CopyValue(Params, (*DefaultValue)->GetValue()); // ★ 缺参时填默认值
                CleanupFlags[i] = true;
            }
        }
        ++ParamIndex;
    }
}

int32 FFunctionDesc::PostCall(lua_State * L, int32 NumParams, int32 FirstParamIndex, void* Params, const FFlagArray& CleanupFlags)
{
    int32 NumReturnValues = 0;

    if (ReturnPropertyIndex > INDEX_NONE)
    {
        const auto& Property = Properties[ReturnPropertyIndex];
        if (CleanupFlags[ReturnPropertyIndex])
            Property->ReadValue_InContainer(L, Params, true); // ★ UE -> Lua 返回值
        else
            lua_pushvalue(L, FirstParamIndex + ReturnPropertyIndex);
        ++NumReturnValues;
    }

    for (int32 Index : OutPropertyIndices)
    {
        const auto& Property = Properties[Index];
        if (Index >= NumParams || !Property->CopyBack(L, Params, FirstParamIndex + Index))
        {
            Property->ReadValue_InContainer(L, Params, true); // ★ out/ref 参数回写
            ++NumReturnValues;
        }
    }

    for (int32 i = 0; i < Properties.Num(); ++i)
    {
        if (CleanupFlags[i])
            Properties[i]->DestroyValue(Params);              // ★ 参数 buffer 清理
    }

    return NumReturnValues;
}
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp
// 函数: FPropertyDesc::Create
// 行号: 1537-1616
// 位置: “零胶水”没有消灭类型桥，只是把桥从按类手写改成按 FProperty 类型分派
// ============================================================================
FPropertyDesc* FPropertyDesc::Create(FProperty *InProperty)
{
    FPropertyDesc* PropertyDesc = nullptr;
    int32 Type = ::GetPropertyType(InProperty);
    switch (Type)
    {
    case CPT_Int:
    case CPT_Int64:
    case CPT_UInt32:
        PropertyDesc = new FIntegerPropertyDesc(InProperty);  // ★ 数值类型仍是手写 marshaller
        break;
    case CPT_Float:
    case CPT_Double:
        PropertyDesc = new FFloatPropertyDesc(InProperty);
        break;
    case CPT_Enum:
        PropertyDesc = new FEnumPropertyDesc(InProperty);
        break;
    case CPT_ObjectReference:
    case CPT_WeakObjectReference:
        PropertyDesc = new FObjectPropertyDesc(InProperty, false);
        break;
    case CPT_Interface:
        PropertyDesc = new FInterfacePropertyDesc(InProperty);
        break;
    case CPT_Array:
        PropertyDesc = new FArrayPropertyDesc(InProperty);    // ★ 容器也要专门桥接
        break;
    }
    return PropertyDesc;
}
```

关键源码 [2]：`Angelscript` 把热点 API 写成显式签名，只有 fallback 走通用 `FProperty` 封送

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp
// 函数: Bind_AActor_Base lambda
// 行号: 25-37
// 位置: 默认路径是显式写出脚本声明，而不是运行时按名称即席生成
// ============================================================================
auto AActor_ = FAngelscriptBinds::ExistingClass("AActor");

AActor_.Method("bool IsActorInitialized() const", METHOD_TRIVIAL(AActor, IsActorInitialized));
AActor_.Method("bool HasActorBegunPlay() const", METHOD_TRIVIAL(AActor, HasActorBegunPlay));
AActor_.Method("bool IsHidden() const", METHOD_TRIVIAL(AActor, IsHidden));
AActor_.Method("FVector GetActorLocation() const", METHOD_TRIVIAL(AActor, GetActorLocation));
AActor_.Method("FRotator GetActorRotation() const", METHOD_TRIVIAL(AActor, GetActorRotation));
AActor_.Method("void SetActorScale3D(FVector NewScale3D)", METHOD_TRIVIAL(AActor, SetActorScale3D));
// ★ 常用路径的签名成本在绑定期付掉，运行期不必再扫描 FProperty
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp
// 函数: InvokeReflectiveUFunctionFromGenericCall / BindBlueprintCallableReflectiveFallback
// 行号: 294-420
// 位置: 只有 reflective fallback 才像 UnLua 一样遍历 FProperty 做通用封送
// ============================================================================
for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
{
    FProperty* Property = *It;
    if (Property->HasAnyPropertyFlags(CPF_ReturnParm))
        continue;

    void* Destination = Property->ContainerPtrToValuePtr<void>(ParameterBuffer);
    void* ScriptArgumentAddress = Generic->GetAddressOfArg(ScriptArgIndex);
    void* SourceAddress = ResolveScriptArgumentAddress(Property, ScriptArgumentAddress);
    Property->CopySingleValue(Destination, SourceAddress);    // ★ Script -> UE 参数拷贝

    if (Property->HasAnyPropertyFlags(CPF_ReferenceParm) && !Property->HasAnyPropertyFlags(CPF_ConstParm))
        OutReferences[OutReferenceCount++] = { Property, SourceAddress };
}

TargetObject->ProcessEvent(Function, ParameterBuffer);        // ★ 通用反射执行

if (!ShouldBindBlueprintCallableReflectiveFallback(Function))
    return false;
if (!Signature.bAllTypesValid || Signature.ArgumentTypes.Num() > BlueprintCallableReflectiveFallbackMaxArgs)
    return false;
if (IsScriptDeclarationAlreadyBound(InType, Signature))
    return false;                                             // ★ fallback 只补未直绑的洞
```

新增对比结论：

- `UnLua` 的“零胶水”精确说法应是“零 `Bind_*.cpp` 暴露清单”，不是“零运行时 marshaller”。
- `Angelscript` 并非完全没有反射桥；它把通用 `FProperty` 封送压缩到 `reflective fallback` 这条次路径，而不是主路径。
- 这意味着两边比较时不能只看“有没有自动暴露”，还要看“热点 API 的调用期开销放在哪一侧”。

差距判断：

- `Angelscript` 相对 `UnLua` 的“按 `UClass/UFunction` 直接零注册接入”属于 `没有实现`
- `UnLua` 相对 `Angelscript` 的“默认直绑快路径”属于 `实现方式不同`
- 两者在 marshaller 层面都不是“零代码”，只是代码落点不同，属于 `实现方式不同`

### [维度 D3] 覆写粒度的根本差异：`UnLua` 改写已有事件槽位，`Angelscript` 管理可继承的脚本类型

这一轮最值得补的是“Blueprint 交互”的所有权边界。`UnLua` 的入口是 `GetModuleName()`：先让现有 Blueprint / C++ 类实现 `UUnLuaInterface`，再在绑定时把 Lua table 里的函数名映射到已有 `UFunction` 上，最后通过 `ULuaFunction::SetActive()` 改写原函数的 native thunk 或直接把新函数塞进 `FunctionMap`。因此它本质上是“给既有 UClass 换行为”。

`Angelscript` 的 `BlueprintOverride` / `BlueprintEvent` 则发生在类生成阶段。`FAngelscriptClassGenerator` 会先检查父类里是否真的存在该 event、签名是否一致、是否跨了 editor-only 边界；通过后再生成 Blueprint-visible wrapper。`Mixin` 也不是 `GetModuleName()` 那种附着脚本，而是把带 `ScriptMixin` 元数据的 Unreal 静态函数，在脚本侧改写成“像成员函数一样调用”的语法糖。也就是说，两边都能“和 Blueprint 交互”，但一个在改已有槽位，一个在定义未来的继承规则。

```
[D3-Deep] Override Ownership
UnLua
├─ CDO implements UUnLuaInterface                // 现有类声明脚本入口
├─ GetModuleName() -> module path                // 由对象反查模块
├─ BindClass scans Lua table                     // 按名字匹配已有 UFunction
└─ ULuaFunction::SetActive()
   ├─ replace native thunk                       // 改写原事件入口
   └─ AddFunctionToFunctionMap                   // 或往现有类挂新函数

Angelscript
├─ Parse UFUNCTION(BlueprintOverride/Event)      // 先分析脚本类声明
├─ Validate parent event + signature             // 编译期校验覆写合法性
├─ BindBlueprintEvent()                          // 生成 Blueprint-visible wrapper
└─ ScriptMixin metadata
   ├─ remove first Unreal arg                    // 把第一个参数折叠成 self
   └─ bind as member-like script method          // 形成 mixin 语义
```

关键源码 [1]：`UnLua` 通过接口定位 module，再直接改写已有 `UFunction`

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaModuleLocator.cpp
// 函数: ULuaModuleLocator::Locate
// 行号: 31-42
// 位置: 绑定入口不是“生成新类型”，而是从现有类的 CDO 上取 GetModuleName()
// ============================================================================
if (CDO->HasAnyFlags(RF_NeedInitialization))
    return "";

if (!CDO->GetClass()->ImplementsInterface(UUnLuaInterface::StaticClass()))
    return "";

return IUnLuaInterface::Execute_GetModuleName(CDO);           // ★ Blueprint/C++ 类自己声明 Lua 模块名
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaFunction.cpp
// 函数: ULuaFunction::Override / SetActive
// 行号: 139-239
// 位置: 覆写的核心动作是复制旧函数并替换 native thunk，而不是重建一条继承链
// ============================================================================
void ULuaFunction::Override(UFunction* Function, UClass* Class, bool bAddNew)
{
    bAdded = bAddNew;
    From = Function;

    if (Function->GetNativeFunc() == execScriptCallLua)
    {
        const auto LuaFunction = Get(Function);
        Overridden = LuaFunction->GetOverridden();            // ★ 已覆写过就沿用旧副本
    }
    else
    {
        const auto DestName = FString::Printf(TEXT("%s__Overridden"), *Function->GetName());
        Overridden = static_cast<UFunction*>(StaticDuplicateObject(Function, GetOuter(), *DestName)); // ★ 备份原函数
        Overridden->SetNativeFunc(Function->GetNativeFunc());
    }

    SetActive(true);
}

void ULuaFunction::SetActive(const bool bActive)
{
    const auto Function = From.Get();
    const auto Class = Cast<ULuaOverridesClass>(GetOuter())->GetOwner();

    if (bAdded)
    {
        SetNativeFunc(execCallLua);
        Class->AddFunctionToFunctionMap(this, *GetName());    // ★ 新增函数直接挂到现有 UClass
    }
    else
    {
        Function->FunctionFlags |= FUNC_Native;
        Function->SetNativeFunc(&execScriptCallLua);          // ★ 原函数入口被 Lua thunk 接管
        Function->GetOuterUClass()->AddNativeFunction(*Function->GetName(), &execScriptCallLua);
    }
}
```

关键源码 [2]：`Angelscript` 在类生成阶段校验覆写关系；`Mixin` 则是签名重写，不是模块附着

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 函数: FAngelscriptClassGenerator::AnalyzeClassData 中的 BlueprintOverride 校验段
// 行号: 733-786
// 位置: 覆写是否合法在脚本类生成期就会被判定，而不是等运行时按名字碰撞
// ============================================================================
if (FunctionDesc->bBlueprintOverride)
{
    auto* ParentFunction = GetBlueprintEventByScriptName(CodeSuperClass, FunctionDesc->FunctionName);
    if (ParentFunction == nullptr)
    {
        FAngelscriptEngine::Get().ScriptCompileError(...,
            TEXT("BlueprintOverride method %s in class %s does not exist in superclass %s."));
        ClassData.ReloadReq = EReloadRequirement::Error;      // ★ 父类没有事件，直接编译错误
    }
    else if (!SuperFunctionDesc->bBlueprintEvent && !SuperFunctionDesc->bBlueprintOverride)
    {
        FAngelscriptEngine::Get().ScriptCompileError(...,
            TEXT("BlueprintOverride method %s in class %s is not marked BlueprintEvent in superclass %s."));
        ClassData.ReloadReq = EReloadRequirement::Error;      // ★ 父类不是 event，也不允许覆写
    }
    else if (!SuperFunctionDesc->SignatureMatches(FunctionDesc))
    {
        FAngelscriptEngine::Get().ScriptCompileError(...,
            TEXT("BlueprintOverride method %s in class %s does not match signature of event declared in superclass %s."));
        ClassData.ReloadReq = EReloadRequirement::Error;      // ★ 签名不一致在生成期就拦住
    }
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h
// 函数: FAngelscriptFunctionSignature::InitFromFunction 内部 mixin 处理段
// 行号: 276-314
// 位置: ScriptMixin 的本质是把 Unreal 静态函数重写成脚本成员式调用
// ============================================================================
const FString& MixinClasses = Function->GetOuterUClass()->GetMetaData(NAME_Signature_ScriptMixin);
if (MixinClasses.Len() != 0 && ArgumentTypes.Num() > 0
    && (ArgumentTypes[0].IsObjectPointer() || ArgumentTypes[0].bIsReference))
{
    TArray<FString> MixinList;
    MixinClasses.ParseIntoArray(MixinList, TEXT(" "));

    FString FirstParamType = ArgumentTypes[0].Type->GetAngelscriptTypeName();
    for (const FString& Mixin : MixinList)
    {
        if (FirstParamType == Mixin)
        {
            ArgumentTypes.RemoveAt(0);                        // ★ 把第一个 Unreal 参数折叠成脚本 self
            ArgumentNames.RemoveAt(0);
            ArgumentDefaults.RemoveAt(0);
            ClassName = Mixin;
            bStaticInScript = false;                         // ★ 从静态函数改成成员式方法
            bFoundMixin = true;
            break;
        }
    }
}
```

新增对比结论：

- `UnLua` 的 Blueprint 覆写更像“把 Lua 行为附着到现有 UClass 上”，不是让 Lua 成为一条新的类继承源。
- `Angelscript` 的 `BlueprintOverride` 价值不只在能覆写，而在它把“能不能覆写”前移到类生成期校验。
- `Mixin` 与 `GetModuleName()` 不是同层能力：前者是调用表面塑形，后者是对象绑定入口。

差距判断：

- `UnLua` 相对 `Angelscript` 的“覆写签名编译期校验”属于 `没有实现`
- `Angelscript` 相对 `UnLua` 的“Blueprint 资产附着脚本入口（GetModuleName）”属于 `实现方式不同`
- `Mixin` 与 `UnLuaInterface` 解决的问题不同，属于 `实现方式不同`

### [维度 D4] 热重载不能简单概括成“Lua require 刷新 vs Angelscript 全量重编译”

这一轮补出的关键事实是：`UnLua` 和 `Angelscript` 都没有表面说法那么单纯。`UnLua` 的 editor 侧确实用 `DirectoryWatcher` 监听脚本目录，自动模式下直接调用 `HotReload()`；Lua 侧则用自定义 `require` 记录 `loaded_module_times`，只重载发生变化的模块，再通过 `update_modules()` 尝试替换函数对象、匹配 upvalue、更新全局引用，尽量保住旧状态。如果新模块执行失败，它会在进入 merge 前 `sandbox.exit()` 返回，旧模块不被覆写。

`Angelscript` 则并不是“永远 full reload”。当前代码有明确的 `SoftReload / FullReloadSuggested / FullReloadRequired / Error` 四档。只是当改动碰到类形状或 `BlueprintEvent` 这种必须重走 Blueprint 虚函数路径的点时，才会从 `Suggested` 升到 `Required`。因此更准确的结论是：`UnLua` 的粒度按脚本 module，`Angelscript` 的粒度按脚本类型图和 reload requirement 传播。

```
[D4-Deep] Reload Decision Path
UnLua
├─ DirectoryWatcher -> HotReload()               // 编辑器自动触发
├─ custom require                                // 接管 _G.require
├─ loaded_module_times                           // 按文件时间找脏模块
└─ update_modules()
   ├─ replace functions                          // 尽量更新行为
   ├─ match upvalues                             // 尽量迁移闭包状态
   └─ update globals                             // 尽量保住全局引用

Angelscript
├─ analyze module/type delta                     // 先分析改动影响面
├─ classify reload requirement                   // Soft / Suggested / Required / Error
├─ propagate dependency graph                    // 沿类型依赖传播
└─ escalate on BlueprintEvent / shape changes    // 命中条件才升级到 full
```

关键源码 [1]：`UnLua` 用目录监听触发热重载，并把 `_G.require` 重定向到自定义 loader

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorFunctionLibrary.cpp
// 函数: UUnLuaEditorFunctionLibrary::WatchScriptDirectory / OnLuaFilesModified
// 行号: 27-35, 112-117
// 位置: 编辑器侧只负责发现文件变化并触发全局热重载入口
// ============================================================================
FDirectoryWatcherModule& DirectoryWatcherModule = FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>("DirectoryWatcher");
IDirectoryWatcher* DirectoryWatcher = DirectoryWatcherModule.Get();
if (DirectoryWatcher)
{
    const auto& Delegate = IDirectoryWatcher::FDirectoryChanged::CreateStatic(&UUnLuaEditorFunctionLibrary::OnLuaFilesModified);
    const auto& ScriptRootPath = UUnLuaFunctionLibrary::GetScriptRootPath();
    DirectoryWatcher->RegisterDirectoryChangedCallback_Handle(ScriptRootPath, Delegate, DirectoryWatcherHandle);
}

void UUnLuaEditorFunctionLibrary::OnLuaFilesModified(const TArray<FFileChangeData>& FileChanges)
{
    const auto& Settings = *GetDefault<UUnLuaEditorSettings>();
    if (Settings.HotReloadMode != EHotReloadMode::Auto)
        return;
    UUnLuaFunctionLibrary::HotReload();                      // ★ 发现改动后直接触发 HotReload
}
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaLib.cpp
// 函数: UnLua::Lib::Open
// 行号: 245-269
// 位置: 初始化 Lua 状态时，把全局 require 重定向到 HotReload 版本
// ============================================================================
luaL_requiref(L, "UnLua", LuaOpen, 1);

#if UNLUA_WITH_HOT_RELOAD
luaL_dostring(L, R"(
    pcall(function() _G.require = require('UnLua.HotReload').require end)
)");                                                         // ★ 不是外部工具做热更，而是直接替换 require
#endif
```

```lua
-- ============================================================================
-- 文件: Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua
-- 函数: local require / update_modules / M.reload
-- 行号: 147-176, 480-549, 604-627
-- 位置: 按模块时间戳检测脏文件，并尝试做状态迁移式热更新
-- ============================================================================
local function require(module_name, ...)
    if package.loaded[module_name] ~= nil then
        return package.loaded[module_name], nil              -- ★ 已加载模块直接复用
    end

    local loaded = loaded_modules[module_name]
    if loaded then
        package.loaded[module_name] = loaded
        return loaded, nil                                   -- ★ 自己维护 loaded_modules 缓存
    end

    local func, env = load(module_name)
    if func then
        local _, new_module = xpcall(func, load_error_handler, ...)
        loaded_modules[module_name] = new_module
        loaded_module_times[module_name] = get_last_modified_time(module_name) -- ★ 记录时间戳
        return new_module, nil
    end
end

local function update_modules(old_modules, new_modules, new_envs)
    for i, old_module in ipairs(old_modules) do
        local new_module = new_modules[i]
        local old_module_upvalues = collect_module_upvalues(old_module)
        local moduleres = { values = {}, upvalue_map = {}, old_module = old_module }

        moduleres.values = match_module(new_module_info, old_module)
        moduleres.upvalue_map = match_upvalues(moduleres.values, old_module_upvalues) -- ★ 尝试迁移闭包状态
    end

    merge_objects(result)
    update_global(all_value_maps)                            // ★ 更新全局引用而不是整表丢弃
end

function M.reload(module_names)
    for module_name, time in pairs(loaded_module_times) do
        local current_time = get_last_modified_time(module_name)
        if current_time ~= time then
            modified_modules[#modified_modules + 1] = module_name
        end
    end

    if #modified_modules > 0 then
        reload_modules(modified_modules)                     // ★ 只重载发生变化的模块
    end
end
```

关键源码 [2]：`Angelscript` 当前实现有软/全量分层，`BlueprintEvent` 变更会强制升级

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h
// 函数: FAngelscriptClassGenerator::EReloadRequirement
// 行号: 21-58
// 位置: 热重载不是二元开关，而是显式建模成四档 requirement
// ============================================================================
struct FAngelscriptClassGenerator
{
    enum EReloadRequirement
    {
        SoftReload,
        FullReloadSuggested,
        FullReloadRequired,
        Error,
    };

    struct FReloadPropagation
    {
        EReloadRequirement ReloadReq = EReloadRequirement::SoftReload; // ★ 默认先从 SoftReload 开始
        TArray<FReloadPropagation*> PendingDependees;
    };
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 函数: FAngelscriptClassGenerator::AnalyzeClassData 内部新增函数处理段
// 行号: 1269-1285
// 位置: 新增函数通常只是建议 full reload；新增 BlueprintEvent 才强制升级
// ============================================================================
// We added a new function, we should suggest a full reload but not require it
if (ClassData.ReloadReq < EReloadRequirement::FullReloadSuggested)
{
    ClassData.ReloadReq = EReloadRequirement::FullReloadSuggested;
    ClassData.ReloadReqLines.AddUnique(NewFunctionDesc->LineNumber);
}

// Unless this is a BlueprintEvent, which requires a full reload to be added
if (NewFunctionDesc->bBlueprintEvent)
{
    if (ClassData.ReloadReq < EReloadRequirement::FullReloadRequired)
    {
        ClassData.ReloadReq = EReloadRequirement::FullReloadRequired; // ★ BlueprintEvent 明确升级到 Required
        ClassData.ReloadReqLines.AddUnique(NewFunctionDesc->LineNumber);
    }
}
```

新增对比结论：

- `UnLua` 的热重载不只是 `package.loaded[name] = nil` 级别刷新，它明确在做“模块状态迁移”。
- 当前 `Angelscript` 代码库已经不是“只有全量重编译”这一种形态，而是有 `SoftReload -> FullReloadSuggested -> FullReloadRequired` 的升级链。
- 两边的核心差异不是“谁支持热重载”，而是“脏区粒度按 Lua module 还是按脚本类型图”。

差距判断：

- `Angelscript` 相对 `UnLua` 的“按 Lua module 增量刷新并尽量迁移 upvalue/global 状态”属于 `没有实现`
- `UnLua` 相对 `Angelscript` 的“显式 reload requirement 图传播”属于 `没有实现`
- 用户所说的 `Angelscript` “全量重编译热重载”在当前仓库快照里应修正为 `存在软/全量分层，但 BlueprintEvent 等改动会升级为 FullReloadRequired`

### [维度 D5] 调试链路的归属边界：`UnLua` 提供 hook/变量采样底座，`Angelscript` 提供内建协议栈

这一轮最关键的新证据来自 `Reference/UnLua/Docs/CN/Debugging.md`。当前仓库快照里，`UnLua` 官方调试文档直接要求安装 `LuaPanda / LuaHelper`，再在脚本里手动 `require("LuaPanda").start("127.0.0.1", 8818)`。也就是说，前端调试协议和 IDE 集成主要归外部 Lua 调试器所有。UE 侧源码里确实有两类底座：一类是 `lua_sethook`，但这里挂上的 `Hook()` 主要用于 `Unreal Insights` CPU 事件采样；另一类是 `UnLuaDebugBase.cpp`，它能从 Lua C API 里抓局部变量、upvalue 和调用栈并序列化成 UE 结构体。这些都是“调试承载点”，但当前快照没有看到像 `AngelscriptDebugServer` 那样的内建 socket/debug protocol 实现。

`Angelscript` 则是另一种 ownership：`EDebugMessageType`、`SerializeDebugMessageEnvelope()`、变量/调用栈消息、版本握手、数据断点协议都在 runtime 内部；Windows 平台还会把数据断点映射到 CPU debug registers。它不是把 Lua 调试器接进来，而是自己就是调试器后端。

```
[D5-Deep] Debugger Ownership
UnLua
├─ VSCode plugin (LuaPanda / LuaHelper)          // 前端协议由外部插件提供
├─ Lua script calls start("127.0.0.1", 8818)     // 调试会话从脚本侧发起
└─ UE side
   ├─ lua_sethook for profiling/checks           // 提供 hook 点
   └─ locals/upvalues/callstack serializer       // 提供变量采样底座

Angelscript
├─ runtime DebugServer                           // 后端驻留在引擎进程里
├─ custom debug message envelope                 // 自定义协议与版本协商
├─ Variables / CallStack / GoToDefinition        // 变量、栈、跳转
└─ DataBreakpoints                               // Windows 下直接落到硬件断点
```

关键源码 [1]：`UnLua` 当前快照的调试前端依赖外部 Lua 调试器；UE 侧主要提供 hook 与变量采样

```markdown
<!-- =========================================================================
文件: Reference/UnLua/Docs/CN/Debugging.md
函数: 使用 LuaPanda / LuaHelper 调试
行号: 1-18
位置: 官方文档直接把调试工作流交给外部 VSCode Lua 调试器
=========================================================================== -->
# 调试
## 使用 LuaPanda / LuaHelper 调试

1. 从VSCode应用市场安装 LuaPanda / LuaHelper
2. 从 LuaPanda 官方仓库获取 `LuaPanda.lua`，放入 `{UE工程}/Content/Script` 目录
3. 在Lua代码中加入 `require("LuaPanda").start("127.0.0.1",8818)`

注：调试器依赖 `luasocket`，UnLua已通过扩展插件集成
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp
// 函数: Hook / FLuaEnv::FLuaEnv
// 行号: 43-70, 166-170
// 位置: UE 侧 hook 主要接到 Unreal Insights，而不是自带一条调试消息通道
// ============================================================================
void Hook(lua_State* L, lua_Debug* ar)
{
    lua_getinfo(L, "nSl", ar);
    if (ar->what == FName("Lua"))
    {
        const auto EventName = FString::Printf(TEXT("%s [%s:%d]"), ...);
        if (ar->event == 0)
            FCpuProfilerTrace::OutputBeginDynamicEvent(*EventName); // ★ call/ret hook 接到 profiler
        else
            FCpuProfilerTrace::OutputEndEvent();
    }
}

if (FDeadLoopCheck::Timeout)
    UE_LOG(LogUnLua, Warning, TEXT("Profiling will not working when DeadLoopCheck enabled."))
else
    lua_sethook(L, Hook, LUA_MASKCALL | LUA_MASKRET, 0);      // ★ 这里挂的是 profiling hook，不是 DAP server
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaDebugBase.cpp
// 函数: GetLuaVariables / GetLuaCallStack
// 行号: 621-687
// 位置: UE 侧能枚举 locals/upvalues/callstack，说明它具备调试数据采样底座
// ============================================================================
if (!lua_getstack(L, StackLevel, &ar))
    return false;
if (!lua_getinfo(L, "nSlu", &ar))
    return false;

for (int32 i = 1; ; ++i)
{
    const char *VarName = lua_getlocal(L, &ar, i);
    if (!VarName)
        break;
    Variable.Key = UTF8_TO_TCHAR(VarName);
    Variable.Value.Build(L, -1, Level);                        // ★ 局部变量被序列化成 UE 结构
}

for (int32 i = 1; ; ++i)
{
    const char *UpvalueName = lua_getupvalue(L, FunctionIdx, i);
    if (!UpvalueName)
        break;
    Upvalue.Key = UTF8_TO_TCHAR(UpvalueName);
    Upvalue.Value.Build(L, -1, Level);                         // ★ upvalue 同样能被采样
}

while (lua_getstack(L, Depth++, &ar))
{
    lua_getinfo(L, "nSl", &ar);
    CallStack += FString::Printf(TEXT("Source : %s, name : %s, Line : %d \n"), ...); // ★ 也能导出 Lua 栈
}
```

关键源码 [2]：`Angelscript` 内建自定义调试协议、变量消息和数据断点

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h
// 函数: EDebugMessageType / FAngelscriptVariable / FAngelscriptDataBreakpoints
// 行号: 25-80, 399-449
// 位置: 调试协议、变量结构和数据断点消息全部是 runtime 自己定义的
// ============================================================================
enum class EDebugMessageType : uint8
{
    RequestCallStack,
    CallStack,
    ClearBreakpoints,
    SetBreakpoint,
    RequestVariables,
    Variables,
    GoToDefinition,
    DebugServerVersion,
    SetDataBreakpoints,
    ClearDataBreakpoints,                                    // ★ 协议层直接有 DataBreakpoint 消息
};

struct FAngelscriptDataBreakpoints : FDebugMessage
{
    TArray<FAngelscriptDataBreakpoint> Breakpoints;          // ★ 数据断点是一级消息类型
};

struct FAngelscriptVariable : FDebugMessage
{
    FString Name;
    FString Value;
    FString Type;
    uint64 ValueAddress;
    uint8 ValueSize;
    bool bHasMembers = false;                                // ★ 变量查看支持地址和成员展开
};
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 函数: DataBreakpoint_Windows::ApplyBreakpointsToThreadContext
// 行号: 135-202
// 位置: Windows 平台下数据断点会真正落到 CPU debug registers
// ============================================================================
for (uint8 RegisterToModify = 0; RegisterToModify < Breakpoints.Num(); RegisterToModify++)
{
    switch (RegisterToModify)
    {
    case 0: Context.Dr0 = Breakpoints[RegisterToModify].Address; break;
    case 1: Context.Dr1 = Breakpoints[RegisterToModify].Address; break;
    case 2: Context.Dr2 = Breakpoints[RegisterToModify].Address; break;
    case 3: Context.Dr3 = Breakpoints[RegisterToModify].Address; break;
    }

    Context.Dr7 |= static_cast<DWORD64>(0x1) << (RegisterToModify * 2); // ★ 启用硬件断点槽

    DWORD64 BreakpointSettings = 0x1; // Writes only
    switch (Breakpoints[RegisterToModify].AddressSize)
    {
    case 2: BreakpointSettings |= 0b0100; break;
    case 4: BreakpointSettings |= 0b1100; break;
    case 8: BreakpointSettings |= 0b1000; break;
    }

    Context.Dr7 |= static_cast<DWORD64>(BreakpointSettings) << (16 + RegisterToModify * 4);
}
```

新增对比结论：

- 就当前仓库快照看，`UnLua` 的调试能力核心是“可被外部 Lua debugger 消费的 hook 与变量采样底座”，不是“插件内建调试服务器”。
- `Angelscript` 的调试 ownership 更强：协议、变量模型、数据断点、版本协商都在自己 runtime 里。
- 因此这不是“有调试 / 没调试”的差距，而是“调试器后端在不在引擎进程里”的差距。

差距判断：

- `UnLua` 相对 `Angelscript` 的“内建 UE 侧 debug protocol server”属于 `没有实现`
- `UnLua` 并非没有调试基础设施；locals/upvalues/callstack 采样已经存在，属于 `实现方式不同`
- “`UnLua` 内置 DAP 协议实现”在当前快照里没有找到 UE 侧源码证据；本轮结论只能精确到“外接 Lua debugger + UE 侧采样底座”

### [维度 D9] 测试入口的组织方式：`UnLua` 用 C++ 宏 DSL 复放问题，`Angelscript` 用脚本符号发现并挂到 hot reload 生命周期

前文已经提过 `UnLua` 有 `spec / regression / benchmark` 四层，这一轮补的是“这些测试是怎么被组织起来的”。`UnLuaTestCommon.h` 在 UE Automation 之上又包了一层宏 DSL：`IMPLEMENT_UNLUA_INSTANT_TEST`、`IMPLEMENT_UNLUA_LATENT_TEST`、`BEGIN_TESTSUITE` 把 `SetUp / InstantTest / TearDown / latent command` 统一起来，所以 issue 回归文件看起来非常像“一个问题一个微型剧本”。`BindingTest.cpp` 直接按 `UnLua.API.Binding.*` 命名核心 API 语义；`IssueOverridesTest.cpp` 则把地图加载、Lua 全局、对象构造和回放步骤写成连续 latent command，明显以“复现实例问题”为中心。

`Angelscript` 的组织逻辑不同。它并不要求作者写 C++ test wrapper；而是直接扫描脚本符号前缀 `IntegrationTest_ / ComplexIntegrationTest_` 去发现测试，再把受影响模块塞进 `FHotReloadTestRunner`，在热重载后按 batch 跑。两边都能自动化，但一个把重点放在“资产级 bug replay”，另一个把重点放在“编译后快速反馈”。

```
[D9-Deep] Test Entry Style
UnLua
├─ C++ macro DSL                                 // 测试作者先写 UE automation 包装
│  ├─ IMPLEMENT_UNLUA_INSTANT_TEST               // 即时测试
│  ├─ IMPLEMENT_UNLUA_LATENT_TEST                // latent 测试
│  └─ BEGIN_TESTSUITE                            // 场景化回归
└─ Naming by API / Regression / IssueNNN         // 组织围绕具体问题与功能点

Angelscript
├─ DiscoverWithFilter("IntegrationTest_")        // 直接从脚本符号发现
├─ DiscoverWithFilter("ComplexIntegrationTest_") // 复杂测试额外有 GetTests 入口
└─ HotReloadTestRunner
   ├─ select affected modules                    // 受影响模块优先
   └─ run in batches after compile               // 编译后批量回归
```

关键源码 [1]：`UnLua` 用宏 DSL 固化测试形态，并把 issue 回归写成可复放脚本

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Public/UnLuaTestCommon.h
// 函数: IMPLEMENT_UNLUA_INSTANT_TEST / BEGIN_TESTSUITE
// 行号: 171-215
// 位置: 在 UE Automation 之上再包一层 DSL，降低每个回归测试的样板代码
// ============================================================================
#define IMPLEMENT_UNLUA_INSTANT_TEST(TestClass, PrettyName) \
IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass##Runner, PrettyName, (EAutomationTestFlags::ClientContext | EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)) \
bool TestClass##Runner::RunTest(const FString& Parameters) \
{ \
bool bSuccess = false; \
TestClass* TestInstance = new TestClass(); \
TestInstance->SetTestRunner(*this); \
if (TestInstance->SetUp()) \
{ \
bSuccess = TestInstance->InstantTest();                      /* ★ 把 SetUp/InstantTest/TearDown 固化成统一骨架 */ \
TestInstance->TearDown(); \
} \
delete TestInstance; \
return bSuccess; \
}

#define BEGIN_TESTSUITE(TestClass, PrettyName) \
namespace UnLuaTestSuite { \
IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, PrettyName, (EAutomationTestFlags::ClientContext | EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter))
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Tests/IssueOverridesTest.cpp
// 函数: FIssueOverridesTest::RunTest
// 行号: 25-58
// 位置: 一个覆盖问题的回归测试，就是一段完整的地图+对象+Lua 回放剧本
// ============================================================================
BEGIN_TESTSUITE(FIssueOverridesTest, TEXT("UnLua.Regression.IssueOverrides 覆写引起的问题"))

const auto MapName = TEXT("/UnLuaTestSuite/Tests/Regression/IssueOverrides/IssueOverrides.IssueOverrides");
ADD_LATENT_AUTOMATION_COMMAND(FOpenMapLatentCommand(MapName))
ADD_LATENT_AUTOMATION_COMMAND(FEngineWaitLatentCommand(0.5));
ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this] {
    const auto L = UnLua::GetState();
    lua_getglobal(L, "Counter");
    TestEqual(TEXT("Counter"), (int)lua_tointeger(L, -1), 4); // ★ 先验证地图驱动的 Lua 状态

    const auto Obj = NewObject<UIssueOverridesObject>();
    UnLua::PushUObject(L, Obj);
    lua_setglobal(L, "G_IssueObject");

    UnLua::RunChunk(L, "return G_IssueObject:CollectInfo()");
    TestEqual(TEXT("Result1"), (int32)lua_tointeger(L, -1), 2); // ★ 再验证对象级覆写行为
    return true;
}));
ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
```

关键源码 [2]：`Angelscript` 直接从脚本符号发现测试，并把执行窗口绑到 hot reload

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Testing/DiscoverTests.cpp
// 函数: DiscoverIntegrationTests
// 行号: 192-208
// 位置: 测试入口不是 C++ 宏，而是脚本符号命名约定
// ============================================================================
TMap<FString, asIScriptFunction*> IntegrationTestRunTestFunctions;
OneArgFunctionFilter Filter = CreateOneArgFilter("IntegrationTest_", "", "FIntegrationTest");
DiscoverWithFilter(Module, IntegrationTestRunTestFunctions, Filter);
RegisterSimpleFunctions(IntegrationTestRunTestFunctions, IntegrationTestFunctions); // ★ 按前缀自动收集

TMap<FString, asIScriptFunction*> ComplexIntegrationTestRunTestFunctions;
OneArgFunctionFilter ComplexFilter = CreateOneArgFilter("ComplexIntegrationTest_", "", "FIntegrationTest");
DiscoverWithFilter(Module, ComplexIntegrationTestRunTestFunctions, ComplexFilter);
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Testing/UnitTest.cpp
// 函数: FHotReloadTestRunner::PrepareTests / RunTests
// 行号: 532-649
// 位置: 测试发现结果会继续参与 hot reload 后的模块筛选与批量执行
// ============================================================================
if (!ShouldRunUnitTestsOnHotReload())
    return;

for (const TSharedRef<FAngelscriptModuleDesc>& Module : ModulesToCompile)
{
    if (Module->UnitTestFunctions.Num() > 0)
        TestAfterHotReload.Add(Module);                       // ★ 编译后只保留有测试的模块
}

while (TestsInBatch <= TestsPerBatch && TestAfterHotReload.Num() > 0)
{
    TSharedRef<FAngelscriptModuleDesc> Module = TestAfterHotReload.Pop(false);
    TestBatch.Add(Module);
    TestsInBatch += Module->UnitTestFunctions.Num();
}
AllUnitTestsPass = RunAngelscriptUnitTests(TestBatch, AngelscriptManager, CurrentBatchOnHotReload, TotalBatchesOnHotReload); // ★ 热重载后批量回归
```

新增对比结论：

- `UnLua` 的测试更像“问题档案馆”，重点是让某个具体 issue 或 API 行为可以被地图/对象/脚本三方一起复放。
- `Angelscript` 的测试更像“编译生命周期的一部分”，重点是让模块改完以后尽快自动发现并回归。
- 两边都有自动化，但入口风格完全不同：`UnLua` 偏作者显式编排，`Angelscript` 偏符号约定驱动。

差距判断：

- `UnLua` 相对 `Angelscript` 的“按脚本符号自动发现测试”属于 `没有实现`
- `Angelscript` 相对 `UnLua` 的“按 issue 编号沉淀资产级复现场景库”属于 `实现方式不同`
- 关于 CI，本轮仍未在 `Reference/UnLua` 快照里找到新增流水线配置证据，因此不扩大旧结论

### [维度 D10] 文档与示例的更深一层：`UnLua` 把 README、脚本、资产路径做成了双向索引

前文已经写过 `README + Docs + Tutorials` 的闭环，这一轮新增的是“为什么它比普通教程目录更好用”。`README.md` 不是只列出教程标题，而是把 `01~13` 的脚本文件直接当入口；`01_HelloWorld.lua`、`10_Replications.lua` 这类脚本头部又反过来写明自己绑定的 `.map` 或 `.uasset` 路径，并在运行时把自己的脚本路径打印到屏幕上。于是用户可以从 README 跳到脚本，再从脚本头部跳回资产，再从运行时 UI 确认自己看的就是当前示例。这种“文档 <-> 脚本 <-> 资产”三向互链，在 UE 插件里其实很少见。

`Angelscript` 这里不是没有示例，而是示例默认住在 `Examples/*.cpp` 的内嵌字符串里。它同样能提供高质量的教学片段，但导航方向通常是“测试文件 -> 字符串常量”，而不是“README -> 工作区脚本 -> 对应资产”。这会直接影响新用户第一次在工程里找例子的成本。

```
[D10-Deep] Example Navigation
UnLua
├─ README numbered tutorial list                 // 从首页就能点进脚本
├─ tutorial header -> bound map/uasset path      // 脚本反向指向资产
├─ runtime message -> script source path          // 运行时确认示例来源
└─ user can jump doc <-> script <-> asset         // 三向可导航

Angelscript
├─ example source in C++ test constant            // 示例主要活在测试文件里
└─ navigation path = test file -> embedded string // 对用户工作区不够直接
```

关键源码 [1]：`UnLua` 的教程入口与脚本头注刻意做成双向索引

```markdown
<!-- =========================================================================
文件: Reference/UnLua/README.md
函数: 更多示例 / 文档
行号: 35-69
位置: README 直接把 numbered tutorials 暴露成一线入口，而不是埋在示例工程深处
=========================================================================== -->
1. 新建蓝图后打开，在UnLua工具栏中选择 `绑定`
2. 在接口的 `GetModule` 函数中填入Lua文件路径
3. 选择UnLua工具栏中的 `创建Lua模版文件`
4. 打开 `Content/Script/GameModes/BP_MyGameMode.lua` 编写你的代码

# 更多示例
* [01_HelloWorld](Content/Script/Tutorials/01_HelloWorld.lua)
* [02_OverrideBlueprintEvents](Content/Script/Tutorials/02_OverrideBlueprintEvents.lua)
* [10_Replications](Content/Script/Tutorials/10_Replications.lua)
* [12_CustomLoader](Content/Script/Tutorials/12_CustomLoader.lua)
// ★ 首页入口直接就是工作区里的真实脚本文件
```

```lua
-- ============================================================================
-- 文件: Reference/UnLua/Content/Script/Tutorials/01_HelloWorld.lua
-- 函数: tutorial header / M:Initialize
-- 行号: 1-21
-- 位置: 示例脚本头部反向指向绑定资产，运行时再把自己的脚本路径打印出来
-- ============================================================================
--[[
    例如：
    本脚本由 "Content/Tutorials/01_HelloWorld/HelloWorld.map" 的关卡蓝图绑定  -- ★ 先从脚本跳回资产
]]

function M:Initialize()
    local msg = [[
    Hello World!

    —— 本示例来自 "Content/Script/Tutorials/01_HelloWorld.lua"            -- ★ 运行时再告诉用户当前脚本来源
    ]]
    Screen.Print(msg)
end
```

```lua
-- ============================================================================
-- 文件: Reference/UnLua/Content/Script/Tutorials/10_Replications.lua
-- 函数: tutorial header
-- 行号: 1-31
-- 位置: 复杂示例会同时标出对应 uasset 与脚本文件，形成双向索引
-- ============================================================================
--[[
    蓝图示例：
    Content/Tutorials/10_Replications/ChatCharacter.uasset    -- ★ 指回蓝图资产

    脚本示例：
    Content/Script/Tutorials/ChatCharacter.lua                -- ★ 指向实际脚本实现
]] --

function M:ReceiveBeginPlay()
    local msg = [[
    —— 本示例来自 "Content/Script/Tutorials.10_Replications.lua"
    ]]
    Screen.Print(msg)                                         // ★ 运行时继续保留导航信息
end
```

关键源码 [2]：`Angelscript` 的示例质量很高，但默认消费入口仍是测试源码

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleActorTest.cpp
// 函数: GActorExample
// 行号: 8-36
// 位置: 示例主要以内嵌字符串形式存放在测试文件中，而不是用户工作区脚本
// ============================================================================
const AngelscriptScriptExamples::FScriptExampleSource GActorExample = {
    TEXT("Example_Actor.as"),
    TEXT(R"ANGELSCRIPT(
// For example, we can make a new Actor class
class AExampleActor_UnitTest : AActor
{
    UPROPERTY()
    int ExampleValue = 15;

    default bReplicates = true;
    default Tags.Add(n"ExampleTag");

    UFUNCTION()
    void BlueprintAccessibleMethod()
    {
        Log("BlueprintAccessibleMethod Called");
    }
})ANGELSCRIPT")
};                                                             // ★ 例子本身很好，但主要入口仍是 C++ test 文件
```

新增对比结论：

- `UnLua` 的教程组织优势不只是“文件多”，而是把 README、脚本、资产路径做成了三向可导航的索引系统。
- `Angelscript` 的例子内容质量不差，但默认驻留位置更偏维护者视角，而不是终端用户工作区视角。
- 这会直接影响“第一次找示例”的路径长度：`UnLua` 更短，`Angelscript` 更依赖先进入测试源码。

差距判断：

- “有没有示例”不是差距，属于 `实现方式不同`
- “示例是否能从首页直接落到工作区脚本，并反向定位到资产”这一点上，`UnLua` 相对当前 `Angelscript` 属于 `实现质量差异`

---

## 深化分析 (2026-04-08 19:16:16)

### [维度 D2] 启动阶段的绑定策略差异：`UnLua` 只预热少数反射类族，`Angelscript` 在 engine 创建时重放全局 bind pass

前文讨论过 `UnLua` 的“零胶水”主要体现在**项目侧不必维护 per-class 绑定文件**。这一轮补的是启动阶段的成本控制。`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaSettings.h:54-56` 和 `.../Private/UnLuaSettings.cpp:19-24` 表明，`UnLua` 并不会在激活时把整个 UE 反射面一次性灌进 Lua，而是默认只把 `UBlueprintFunctionLibrary`、`UAnimNotifyState`、`UAnimNotify` 三类高频入口放进 `PreBindClasses`。`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaModule.cpp:103-127` 随后在 `SetActive(true)` 时遍历当前已加载 `UClass`，命中这些类族才 `Env->TryBind(Class)`。

这和 `Angelscript` 的启动思路明显不同。`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1915-1925` 直接在 `BindScriptTypes()` 里调用 `FAngelscriptBinds::CallBinds(CollectDisabledBindNames())`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:144-214` 又把所有静态 `FBind` 排序后逐个执行。再结合 `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineTests.cpp:606-670` 可以看到，`Angelscript` 连“full create 会重放 bind，clone create 不会重放”都做了自动化断言。这说明 `Angelscript` 的启动绑定是**全局可观测的初始化阶段**，而 `UnLua` 的预绑定更像**给零胶水主路径做局部预热**。

```
[D2] Startup Binding Strategy
UnLua
├─ UnLua.Build.cs -> AUTO_UNLUA_STARTUP           // 编译期把自动启动做成宏
├─ UUnLuaSettings.PreBindClasses                  // 只配置少数高频类族
├─ FUnLuaModule::SetActive(true)
│  └─ iterate loaded UClass
│     └─ if child of PreBindClasses -> Env->TryBind(Class)
└─ Goal: warm selected reflected families only

Angelscript
├─ FAngelscriptEngine::BindScriptTypes()
│  └─ FAngelscriptBinds::CallBinds(...)           // 全局 bind pass
├─ FAngelscriptBinds::GetSortedBindArray()        // 按 BindOrder 排序
├─ Full engine create -> replay binds once
└─ Clone engine create -> skip bind replay        // 测试直接覆盖
```

关键源码 [1]：`UnLua` 的预绑定不是“全量预注册”，而是一个可配置的类族预热表

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaSettings.cpp
// 函数: UUnLuaSettings::UUnLuaSettings
// 行号: 19-24
// 位置: 默认只预绑定三类常见入口类型，而不是整个 UE 反射面
// ============================================================================
UUnLuaSettings::UUnLuaSettings(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    PreBindClasses.Add(UBlueprintFunctionLibrary::StaticClass()); // ★ 常见静态函数库
    PreBindClasses.Add(UAnimNotifyState::StaticClass());          // ★ 动画通知常在引擎侧回调
    PreBindClasses.Add(UAnimNotify::StaticClass());
}
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaModule.cpp
// 函数: FUnLuaModule::SetActive
// 行号: 103-127
// 位置: 激活时只扫描当前已加载类，并对命中预绑定名单的类做 TryBind
// ============================================================================
const auto& Settings = *GetMutableDefault<UUnLuaSettings>();
const auto EnvLocatorClass = *Settings.EnvLocatorClass == nullptr ? ULuaEnvLocator::StaticClass() : *Settings.EnvLocatorClass;
EnvLocator = NewObject<ULuaEnvLocator>(GetTransientPackage(), EnvLocatorClass);

for (const auto Class : TObjectRange<UClass>())
{
    for (const auto& ClassPath : Settings.PreBindClasses)
    {
        const auto TargetClass = ClassPath.ResolveClass();
        if (!TargetClass)
            continue;

        if (Class->IsChildOf(TargetClass))
        {
            const auto Env = EnvLocator->Locate(Class);
            Env->TryBind(Class);                                 // ★ 只对命中的类族做预热绑定
            break;
        }
    }
}
```

关键源码 [2]：`Angelscript` 把启动绑定做成全局排序后的 bind pass，并显式测试 clone 模式不重放

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp
// 函数: GetSortedBindArray / FAngelscriptBinds::CallBinds
// 行号: 144-214
// 位置: 所有静态注册的 bind 会被排序后统一执行
// ============================================================================
static TArray<FBindFunction> GetSortedBindArray()
{
    TArray<FBindFunction> SortedBinds = GetBindArray();
    SortedBinds.Sort();                                          // ★ 统一按 BindOrder 排序
    return SortedBinds;
}

void FAngelscriptBinds::CallBinds(const TSet<FName>& DisabledBindNames)
{
    for (const FBindFunction& Bind : GetSortedBindArray())
    {
        if (DisabledBindNames.Contains(Bind.BindName))
            continue;

        Bind.Function();                                         // ★ engine 创建时整批重放 bind
    }
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineTests.cpp
// 函数: FAngelscriptStartupBindObservationFullCreateTest::RunTest
//      FAngelscriptStartupBindObservationCloneCreateTest::RunTest
// 行号: 606-670
// 位置: 自动化测试直接断言 full create 重放 bind，而 clone create 不重放
// ============================================================================
TUniquePtr<FAngelscriptEngine> Engine = FAngelscriptEngine::CreateTestingFullEngine(Config, Dependencies);
const FAngelscriptBindExecutionSnapshot Snapshot = FAngelscriptBindExecutionObservation::GetLastSnapshot();
TestEqual(TEXT("... should observe a single startup bind pass"), FAngelscriptBindExecutionObservation::GetInvocationCount(), 1);

TUniquePtr<FAngelscriptEngine> CloneEngine = FAngelscriptEngine::CreateCloneFrom(*SourceEngine, Config);
TestEqual(TEXT("... clone creation"), FAngelscriptBindExecutionObservation::GetInvocationCount(), 0); // ★ clone 不重复跑 bind
```

新增对比结论：

- `UnLua` 在启动期并没有兑现“全自动暴露一切”的策略，而是通过 `PreBindClasses` 只预热最可能被引擎主动回调的类族，把零胶水的冷启动成本压缩到局部。
- `Angelscript` 的绑定成本更前置：engine 创建时就重放整个有序 bind pass，但换来的是 bind 顺序、禁用列表和 clone 语义都可观测、可测试。
- 据此可判断，两者差异不只是“自动 vs 手写”，还包括“选择性预热 vs 全局确定性初始化”。

差距判断：

- `Angelscript` 相对 `UnLua` 的 `PreBindClasses` 这类“局部预热反射类族”机制，属于 `实现方式不同`
- `Angelscript` 在启动绑定可观测性和回归覆盖上更完整，这一点属于 `实现质量差异`

### [维度 D4] 目录监听语义的分工差异：`UnLua` 的 watcher 只负责触发，`Angelscript` 的 watcher 先把文件系统事件归类

前文已经讲过 `UnLua.HotReload.lua` 如何合并旧 table 和 upvalue。这一轮补的是**热重载前端触发层**。`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorFunctionLibrary.cpp:27-36,112-118` 显示，`UnLua` editor 只在脚本目录上挂一个 `DirectoryWatcher`；收到任何 Lua 目录变化后，如果 `HotReloadMode` 是 `Auto`，就直接调用 `UUnLuaFunctionLibrary::HotReload()`。这一路继续通过 `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaFunctionLibrary.cpp:31-34` 和 `.../Private/LuaEnv.cpp:448-450` 汇到 `DoString("UnLua.HotReload()")`。同时 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorCommands.cpp:19-31` 把 `Alt+L` 手动热重载也映射到同一入口。也就是说，`UnLua` 的 watcher 层基本不关心“这是新增、删除、改名还是文件夹变更”，复杂度全部下沉给 Lua 层。

`Angelscript` 的 watcher 责任更重。`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:366-381` 会对所有脚本根目录注册监听；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp:43-89` 则把 `.as` 文件新增、删除、文件夹新增、文件夹删除分别放进 `FileChangesDetectedForReload` 或 `FileDeletionsDetectedForReload`，文件夹新增还会扫描内部脚本，文件夹删除会枚举已加载脚本。随后 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2729-2829` 再在 tick 中统一消费队列，并根据是否在 PIE / game world 决定 `SoftReloadOnly` 还是 `FullReload`。更关键的是，`Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp:15-223` 直接把 add/remove/folder-add/folder-remove/rename 五种 watcher 语义做成了自动化回归。

```
[D4] Editor Trigger Topology
UnLua
├─ WatchScriptDirectory(Content/Script)
├─ OnLuaFilesModified(...)
│  ├─ if HotReloadMode != Auto -> return
│  └─ UUnLuaFunctionLibrary::HotReload()
├─ Alt+L / toolbar -> same HotReload()
└─ FLuaEnv::HotReload() -> DoString("UnLua.HotReload()")

Angelscript
├─ Watch all script roots
├─ QueueScriptFileChanges(...)
│  ├─ .as add -> FileChangesDetectedForReload
│  ├─ .as remove -> FileDeletionsDetectedForReload
│  ├─ folder add -> scan contained scripts
│  └─ folder remove / rename -> enumerate loaded scripts
├─ Tick() consumes queued changes
└─ choose SoftReloadOnly or FullReload by runtime context
```

关键源码 [1]：`UnLua` 的目录监听层故意保持极薄，所有入口都收敛到同一个 `HotReload()`

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorFunctionLibrary.cpp
// 函数: UUnLuaEditorFunctionLibrary::WatchScriptDirectory / OnLuaFilesModified
// 行号: 27-36, 112-118
// 位置: 目录监听只负责触发，不解析文件系统事件细节
// ============================================================================
void UUnLuaEditorFunctionLibrary::WatchScriptDirectory()
{
    FDirectoryWatcherModule& DirectoryWatcherModule = FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>("DirectoryWatcher");
    IDirectoryWatcher* DirectoryWatcher = DirectoryWatcherModule.Get();
    if (DirectoryWatcher)
    {
        const auto& Delegate = IDirectoryWatcher::FDirectoryChanged::CreateStatic(&UUnLuaEditorFunctionLibrary::OnLuaFilesModified);
        const auto& ScriptRootPath = UUnLuaFunctionLibrary::GetScriptRootPath();
        DirectoryWatcher->RegisterDirectoryChangedCallback_Handle(ScriptRootPath, Delegate, DirectoryWatcherHandle);
    }
}

void UUnLuaEditorFunctionLibrary::OnLuaFilesModified(const TArray<FFileChangeData>& FileChanges)
{
    const auto& Settings = *GetDefault<UUnLuaEditorSettings>();
    if (Settings.HotReloadMode != EHotReloadMode::Auto)
        return;
    UUnLuaFunctionLibrary::HotReload();                          // ★ 不区分 add/remove/rename，直接触发统一入口
}
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaFunctionLibrary.cpp
// 函数: UUnLuaFunctionLibrary::HotReload
// 行号: 31-34
// 位置: editor 与 Blueprint 入口最终都汇总到 module 层热重载
// ============================================================================
void UUnLuaFunctionLibrary::HotReload()
{
    IUnLuaModule::Get().HotReload();
}
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp
// 函数: FLuaEnv::HotReload
// 行号: 448-450
// 位置: runtime 最终只是执行 Lua 层总入口
// ============================================================================
void FLuaEnv::HotReload()
{
    DoString("UnLua.HotReload()");
}
```

关键源码 [2]：`Angelscript` 在 watcher 层先做文件系统语义归类，并且把这些语义做成了回归测试

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp
// 函数: QueueScriptFileChanges
// 行号: 43-89
// 位置: watcher 先把文件系统事件归类，再把结果排队给 runtime 消费
// ============================================================================
void QueueScriptFileChanges(const TArray<FFileChangeData>& Changes, const TArray<FString>& RootPaths, FAngelscriptEngine& Engine, IFileManager& FileManager, const FEnumerateLoadedScripts& EnumerateLoadedScripts)
{
    for (const FFileChangeData& Change : Changes)
    {
        const FString AbsolutePath = FPaths::ConvertRelativePathToFull(Change.Filename);
        FString RelativePath;
        if (!TryMakeRelativeScriptPath(AbsolutePath, RootPaths, RelativePath))
            continue;

        if (AbsolutePath.EndsWith(TEXT(".as")))
        {
            if (Change.Action == FFileChangeData::EFileChangeAction::FCA_Removed)
                Engine.FileDeletionsDetectedForReload.AddUnique({ AbsolutePath, RelativePath });
            else
                Engine.FileChangesDetectedForReload.AddUnique({ AbsolutePath, RelativePath });
            continue;                                           // ★ 单文件变化先归类，不立即编译
        }

        if (Change.Action == FFileChangeData::EFileChangeAction::FCA_Removed)
        {
            for (const FAngelscriptEngine::FFilenamePair& LoadedScript : EnumerateLoadedScripts(AbsolutePath / TEXT("")))
                Engine.FileDeletionsDetectedForReload.AddUnique(LoadedScript); // ★ 文件夹删除时枚举已加载脚本
        }
        else if (Change.Action == FFileChangeData::EFileChangeAction::FCA_Added && FileManager.DirectoryExists(*AbsolutePath))
        {
            FAngelscriptEngine::FindScriptFiles(FileManager, RelativePath, AbsolutePath, TEXT("*.as"), ContainedScriptFiles, false, false); // ★ 文件夹新增时扫描脚本
        }
    }
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp
// 函数: watcher test declarations
// 行号: 15-38, 194-223
// 位置: add/remove/folder-add/folder-remove/rename 语义全部有自动化覆盖
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAngelscriptDirectoryWatcherScriptQueueTest,
    "Angelscript.Editor.DirectoryWatcher.Queue.ScriptAddAndRemove",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAngelscriptDirectoryWatcherRenameWindowTest,
    "Angelscript.Editor.DirectoryWatcher.Queue.RenameWindowTracksRemoveAndAdd",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

const TArray<FFileChangeData> Changes = {
    MakeFileChange(OldAbsolutePath, FFileChangeData::FCA_Removed),
    MakeFileChange(NewAbsolutePath, FFileChangeData::FCA_Added)
};

TestEqual(TEXT("... should queue one removed script"), Engine->FileDeletionsDetectedForReload.Num(), 1);
TestEqual(TEXT("... should queue one added script"), Engine->FileChangesDetectedForReload.Num(), 1); // ★ rename 被建模成 remove + add
```

新增对比结论：

- `UnLua` 的热重载复杂度主要在 Lua 层对象图修补；editor watcher 只做一个非常薄的统一触发器。
- `Angelscript` 把更多复杂度前移到 watcher 层，先把磁盘事件翻译成稳定的“待新增 / 待删除脚本队列”，再由 runtime 决定 soft/full reload。
- 因此，两者不能都概括成“目录监听 + 热重载”。真正不同的是：`UnLua` 选择让 watcher 无状态，`Angelscript` 选择让 watcher 具备事件语义。

差距判断：

- 热重载整体仍属于 `实现方式不同`
- 仅看 watcher 这一子层，`Angelscript` 在文件系统事件建模和回归覆盖上更完整，属于 `实现质量差异`

### [维度 D9] 测试执行入口的形态差异：`UnLua` 把测试交给样例工程，`Angelscript` 额外提供 headless commandlet 与热重载后批量运行

前文已经分析过 `UnLuaTestCommon.h` 的宏 DSL，这一轮补的是“这些测试怎么进入执行系统”。`Reference/UnLua/Plugins/UnLuaTestSuite/UnLuaTestSuite.uplugin:17-29` 把 `UnLuaTestSuite` 定义为 `EnabledByDefault=false` 的独立 runtime 插件；`Reference/UnLua/TPSProject.uproject:16-33` 又在样例工程里显式启用 `RuntimeTests`、`EditorTests`、`FunctionalTestingEditor` 与 `UnLuaTestSuite`。再结合 `Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Public/UnLuaTestCommon.h:171-212`，可以看出 `UnLua` 当前快照的主测试入口是“打开样例工程，走 UE Automation 流程”。`Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/UnLuaTestSuite.Build.cs:43-64` 的 `PrecompileForTargets = Any` 也说明它被设计成可随工程搬运的测试插件。

`Angelscript` 则进一步把测试做成了命令行友好的工程能力。`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptTestCommandlet.cpp:5-24` 的 `UAngelscriptTestCommandlet::Main()` 会按“初次编译失败 / 单元测试失败 / 未初始化 struct 成员”分别返回 `1 / 2 / 3`；这是一种明显面向 headless/CI 的出口码设计。与此同时，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2481-2489` + `.../Testing/UnitTest.cpp:531-650` 还把“热重载后只跑受影响模块测试，并按 batch 执行”内建到 runtime。也就是说，`UnLua` 的测试更多是**样例工程承载的插件回归**，而 `Angelscript` 的测试更接近**引擎生命周期内建的自动验证入口**。

```
[D9] Test Execution Topology
UnLua
├─ UnLuaTestSuite.uplugin (EnabledByDefault=false)
├─ TPSProject.uproject enables
│  ├─ RuntimeTests
│  ├─ EditorTests
│  ├─ FunctionalTestingEditor
│  └─ UnLuaTestSuite
├─ IMPLEMENT_UNLUA_* wraps Automation tests
└─ Primary path: sample project + Automation runner

Angelscript
├─ UAngelscriptTestCommandlet::Main()
│  ├─ initial compile fail -> exit 1
│  ├─ unit tests fail -> exit 2
│  └─ uninitialized struct members -> exit 3
├─ PerformHotReload(...) -> PrepareTests(...)
└─ FHotReloadTestRunner batches impacted modules
```

关键源码 [1]：`UnLua` 的测试入口是“独立测试插件 + 样例工程启用”

```json
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/UnLuaTestSuite.uplugin
// 函数: descriptor
// 行号: 17-29
// 位置: 测试套件默认不开启，需要由样例工程或宿主工程显式启用
// ============================================================================
"EnabledByDefault": false,
"Plugins": [
    {
        "Name": "UnLua",
        "Enabled": true
    }
],
"Modules": [
    {
        "Name": "UnLuaTestSuite",
        "Type": "Runtime",
        "LoadingPhase": "Default"
    }
]
```

```json
// ============================================================================
// 文件: Reference/UnLua/TPSProject.uproject
// 函数: plugin list
// 行号: 16-33
// 位置: 样例工程把运行时/编辑器测试插件和 UnLuaTestSuite 一起打开
// ============================================================================
"Plugins": [
    { "Name": "RuntimeTests", "Enabled": true },
    { "Name": "EditorTests", "Enabled": true },
    { "Name": "FunctionalTestingEditor", "Enabled": true },
    { "Name": "UnLuaTestSuite", "Enabled": true }               // ★ 通过样例工程进入测试体系
]
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Public/UnLuaTestCommon.h
// 函数: IMPLEMENT_UNLUA_LATENT_TEST / IMPLEMENT_UNLUA_INSTANT_TEST
// 行号: 171-212
// 位置: 测试通过 Automation 宏包装后挂进 UE 测试框架
// ============================================================================
#define IMPLEMENT_UNLUA_LATENT_TEST(TestClass, PrettyName) \
IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass##_Runner, PrettyName, (EAutomationTestFlags::ClientContext | EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)) \
bool TestClass##_Runner::RunTest(const FString& Parameters) \
{ \
ADD_LATENT_AUTOMATION_COMMAND(FUnLuaTestCommand_SetUpTest(TestInstance)); \
ADD_LATENT_AUTOMATION_COMMAND(FUnLuaTestCommand_PerformTest(TestInstance)); \
ADD_LATENT_AUTOMATION_COMMAND(FUnLuaTestCommand_TearDownTest(TestInstance)); \
return true; \
}
```

关键源码 [2]：`Angelscript` 额外实现了 headless test commandlet，并把热重载后的增量测试内建到 runtime

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptTestCommandlet.cpp
// 函数: UAngelscriptTestCommandlet::Main
// 行号: 5-24
// 位置: 独立 commandlet 为命令行/CI 提供明确退出码
// ============================================================================
int32 UAngelscriptTestCommandlet::Main(const FString& Params)
{
    if (!FAngelscriptEngine::Get().bDidInitialCompileSucceed)
        return 1;                                               // ★ 初次编译失败

    if (!RunAngelscriptUnitTests(FAngelscriptEngine::Get().GetActiveModules(), &FAngelscriptEngine::Get(), 0, 0))
        return 2;                                               // ★ 单元测试失败

    if (FStructUtils::AttemptToFindUninitializedScriptStructMembers() != 0)
        return 3;                                               // ★ 运行后健全性校验失败

    return 0;
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: FAngelscriptEngine::PerformHotReload
// 行号: 2481-2489
// 位置: 热重载编译完成后，只准备受影响模块的测试
// ============================================================================
if (GEngine && bCompletedAssetScan && HotReloadTestRunner != nullptr && HotReloadTestRunner->ShouldRunUnitTestsOnHotReload())
{
    TArray<FString> RelativeFileList;
    for (const auto& FilenamePair : FileList)
        RelativeFileList.Add(FilenamePair.RelativePath);

    HotReloadTestRunner->PrepareTests(GetActiveModules(), CompiledModules, RelativeFileList, ShouldUseAutomaticImportMethod()); // ★ 只挑受影响模块
}
```

新增对比结论：

- `UnLua` 的测试分发方式更像“把一个可插拔测试插件和样例工程一起交给使用者”。
- `Angelscript` 的测试执行入口更工程化：既能由 commandlet 无界面执行，又能在热重载后只跑受影响模块测试。
- 据此可推断，若目标是 CI/命令行自动验证，当前 `Angelscript` 的入口更直接；当前 `UnLua` 快照能确认的主路径仍是工程内 Automation 组合。

差距判断：

- “测试是否独立成插件”属于 `实现方式不同`
- “是否提供同层级的 headless test entry + 增量热重载测试入口”这一点上，`Angelscript` 相对当前 `UnLua` 属于 `没有实现同层入口`

### [维度 D10] 文档交付物的最后一环：`UnLua` 连 IDE workspace 都随仓库一起发，`Angelscript` 现有文档更多面向维护者

前文已经覆盖了 `README + Docs + Tutorials` 的结构。这一轮补的是**文档交付物最终长成什么样**。`Reference/UnLua/Docs/CN/IntelliSense.md:5-17` 不只是告诉用户“导出智能提示”，而是明确要求参考 `Reference/UnLua/TPSProject.code-workspace:1-14`；这个 workspace 已经预填了 `Content/Script`、`Plugins/UnLua/Intermediate/IntelliSense` 和 `Saved/Logs` 三个目录。再结合 `Reference/UnLua/README.md:29-69`，可以看出 `UnLua` 不只是给一套教程文本，而是在仓库里直接附带一份**可打开、可写代码、可看日志的 IDE 工作区模板**。

对比当前 `Angelscript` 仓库，显式可见的文档交付物更偏维护者工作流。`Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md:1-18,41-64` 讲的是 `ASTEST_CREATE_ENGINE_*` / `ASTEST_BEGIN_*` 宏如何选型；`Plugins/Angelscript/AGENTS.md:3-10` 则约束测试目录、命名和层级放置。它们当然有价值，但目标读者更像“仓库贡献者/维护者”，不是“第一次接入插件的脚本作者”。这不是好坏问题，而是文档交付物的对象不同。

```
[D10] Documentation Artifact Shape
UnLua
├─ README.md -> Quickstart / Tutorials / Docs
├─ Docs/CN/IntelliSense.md -> points to TPSProject.code-workspace
├─ TPSProject.code-workspace
│  ├─ Content/Script
│  ├─ Intermediate/IntelliSense
│  └─ Saved/Logs
└─ Goal: ship a ready-to-open Lua authoring workspace

Angelscript
├─ TESTING_GUIDE.md
├─ AGENTS.md
└─ Goal: standardize contributor-side test layout and engine lifecycle
```

关键源码 [1]：`UnLua` 直接把“工作区长什么样”也写进仓库交付物

```md
<!-- =========================================================================
文件: Reference/UnLua/Docs/CN/IntelliSense.md
函数: 文档步骤
行号: 5-17
位置: 文档不仅说明导出补全，还直接指向示例工程的 workspace 文件
=========================================================================== -->
打开UnLua工具栏，点击导出智能提示，会在`{UE工程}/Plugins/UnLua/Intermediate`下生成`IntelliSense`的目录。

以VSCode为例，参考示例工程的[TPSProject.code-workspace](../../TPSProject.code-workspace)。
一个工作区中分别添加了`Script`和`IntelliSense`，这样每次生成智能提示信息后会自动刷新，就不需要再手动拷贝了。

验证智能提示信息是否工作，只需要在工程的任意lua文件中输入`UE.`。
```

```json
// ============================================================================
// 文件: Reference/UnLua/TPSProject.code-workspace
// 函数: workspace descriptor
// 行号: 1-14
// 位置: IDE 工作区把脚本、补全缓存和日志目录预先组织好
// ============================================================================
{
    "folders": [
        { "path": "Content/Script" },                           // ★ 直接写脚本
        { "path": "Plugins/UnLua/Intermediate/IntelliSense" }, // ★ 直接消费生成的补全
        { "path": "Saved/Logs" }                               // ★ 直接查看运行日志
    ],
    "settings": {}
}
```

关键源码 [2]：`Angelscript` 现有显式文档更多是在规范贡献者如何写和放测试

```md
<!-- =========================================================================
文件: Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md
函数: Overview / Decision Tree
行号: 1-18, 41-64
位置: 文档主轴是测试宏和 engine 生命周期如何选择
=========================================================================== -->
# Angelscript Test Conventions & Macro Guide

This project uses a two-layer macro system defined in `Shared/AngelscriptTestMacros.h` to reduce test boilerplate.

Need to test engine core / bind environment / hot-reload?
  YES --> ASTEST_CREATE_ENGINE_FULL + BEGIN/END_FULL

Testing engine creation itself / multi-engine interaction / production engine?
  YES --> Use IMPLEMENT_SIMPLE_AUTOMATION_TEST directly, no macros.
```

```md
<!-- =========================================================================
文件: Plugins/Angelscript/AGENTS.md
函数: test placement rules
行号: 3-10
位置: 仓库规则文件进一步规定测试目录与命名，明显偏维护者视角
=========================================================================== -->
- `Source/AngelscriptRuntime/Tests/` 只放运行时内部 C++ 测试，Automation 前缀统一用 `Angelscript.CppTests.*`。
- `Source/AngelscriptEditor/Private/Tests/` 只放 Editor 内部测试，Automation 前缀统一用 `Angelscript.Editor.*`。
- `Source/AngelscriptTest/` 使用 `Angelscript.TestModule.*` 前缀。
- 新增测试源文件统一以 `Angelscript` 开头。
```

新增对比结论：

- `UnLua` 的文档交付物已经延伸到 IDE workspace 这一层，等于把“写脚本的桌面”也随仓库一起分发。
- `Angelscript` 当前显式文档更偏贡献者和维护者流程，适合规范测试编写，但不直接提供终端脚本作者的工作区模板。
- 因此，这里的差异不是“有没有文档”，而是“文档最终交付的是工作台，还是规则手册”。

差距判断：

- 文档组织主轴属于 `实现方式不同`
- 在“是否把 IDE 工作区作为可直接消费的交付物一起提供”这一点上，`UnLua` 相对当前 `Angelscript` 属于 `实现质量差异`

---

## 深化分析 (2026-04-08 19:26:15)

### [维度 D2] 零胶水的安全成本落点：`UnLua` 把 owner/type/lifetime 校验放到运行时属性访问边界，`Angelscript` 把更多约束前移到绑定签名

前几轮已经说明 `UnLua` 的“零胶水”并不等于插件内部没有 bridge。这一轮继续往里看，能看到它还把一整层**运行时安全网**一起背上了。`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/LowLevel.cpp:135-154` 会在真正访问 `FProperty` 之前检查 `ContainerPtr` 对应 `UObject` 是否真的是该 property 的 owner class；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1321-1352,1408-1416` 又在 delegate 读写路径上同时检查“property descriptor 还有效吗”“宿主对象是否已经 released”“Lua 侧传进来的值是不是 `table/function`”。这说明 `UnLua` 的零胶水主路径确实降低了项目侧注册成本，但它把更多错误隔离责任推迟到了**每次属性/委托访问时**。

`Angelscript` 的取舍正好相反。当前仓库里，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp:25-37` 仍然是按类显式登记暴露面；而 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_CppType.h:125-141` 已经把预期的 `PropertyType` 固定进 `StepCompiledIn<PropertyType>` / `StepCompiledInRef<PropertyType>`。也就是说，`Angelscript` 不是没有泛型搬运，而是先由绑定代码决定“这次到底消费哪一种 `FProperty` 语义”，再让运行时按该静态假设搬运参数。于是两边真正不同的，不只是“自动 vs 手写”，而是**错误在绑定前暴露，还是在访问时兜底**。

```
[D2] Safety Responsibility Split
UnLua
├─ Lua value -> metatable/userdata resolution      // 先解析动态值是什么
├─ CheckPropertyOwner()                            // 校验 owner class
├─ IsReleasedPtr() / CheckPropertyType()           // 校验生命周期和 Lua 值类别
└─ Delegate/property access                        // 最后才真正读写

Angelscript
├─ Bind_*.cpp selects exposed API                  // 先人工决定暴露面
├─ Helper_CppType<PropertyType> fixes type         // 绑定时固定期望的 PropertyType
└─ StepCompiledIn<PropertyType>() marshals args    // 运行时按静态签名搬运
```

关键源码 [1]：`UnLua` 的动态访问面要自己承担 owner/type/lifetime 防护

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/LowLevel.cpp
// 函数: UnLua::LowLevel::CheckPropertyOwner
// 行号: 135-154
// 位置: 零胶水访问 FProperty 前，先确认 ContainerPtr 真的是该 property 的 owner
// ============================================================================
bool CheckPropertyOwner(lua_State* L, UnLua::ITypeOps* InProperty, void* InContainerPtr)
{
#if ENABLE_TYPE_CHECK == 1
    if (InProperty->StaticExported)
        return true;                                            // ★ 静态导出类型不需要 owner 校验

    UnLua::ITypeInterface* TypeInterface = (UnLua::ITypeInterface*)InProperty;
    FProperty* Property = TypeInterface->GetUProperty();
    if (!Property)
        return true;

    UObject* Object = (UObject*)InContainerPtr;
    UClass* OwnerClass = Property->GetOwnerClass();
    if (!OwnerClass)
        return true;

    if (Object->IsA(OwnerClass))
        return true;                                            // ★ 只有 owner 匹配才允许继续访问

    luaL_error(L, TCHAR_TO_UTF8(*FString::Printf(
        TEXT("Access property from invalid owner. %s should be a %s."),
        *Object->GetName(), *OwnerClass->GetName())));          // ★ 错对象立即抛 Lua 错误
#endif
}
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp
// 函数: FDelegatePropertyDesc::ReadValue_InContainer / WriteValue_InContainer / CheckPropertyType
// 行号: 1321-1352, 1408-1416
// 位置: delegate 读写除了类型桥接，还要显式兜住 descriptor 失效、released object、Lua 值类别错误
// ============================================================================
if (UNLIKELY(!PropertyPtr.IsValid()))
{
    UE_LOG(LogUnLua, Warning, TEXT("attempt to read invalid property '%s'"), *Name);
    lua_pushnil(L);
    return;                                                     // ★ descriptor 已失效，直接拒绝
}

if (UnLua::LowLevel::IsReleasedPtr(ContainerPtr))
{
    UE_LOG(LogUnLua, Warning, TEXT("attempt to write property '%s' on released object"), *Name);
    return false;                                               // ★ 宿主 UObject 已释放，不再继续写
}

int32 Type = lua_type(L, IndexInStack);
if (Type != LUA_TNIL)
{
    if (Type != LUA_TTABLE && Type != LUA_TFUNCTION)
    {
        ErrorMsg = FString::Printf(TEXT("table or function needed but got %s"),
            UTF8_TO_TCHAR(lua_typename(L, Type)));              // ★ delegate 只接受 table/function 形态
        return false;
    }
}
```

关键源码 [2]：`Angelscript` 先用显式绑定决定 API，再用静态 `PropertyType` 消费参数

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp
// 函数: Bind_AActor_Base lambda
// 行号: 25-37
// 位置: 哪些 AActor API 进入脚本面，先由绑定文件逐项决定
// ============================================================================
AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_AActor_Base((int32)FAngelscriptBinds::EOrder::Late-1, []
{
    auto AActor_ = FAngelscriptBinds::ExistingClass("AActor");

    AActor_.Method("bool IsActorInitialized() const", METHOD_TRIVIAL(AActor, IsActorInitialized));
    AActor_.Method("bool HasActorBegunPlay() const", METHOD_TRIVIAL(AActor, HasActorBegunPlay));
    AActor_.Method("bool IsHidden() const", METHOD_TRIVIAL(AActor, IsHidden));
    AActor_.Method("FVector GetActorLocation() const", METHOD_TRIVIAL(AActor, GetActorLocation));
    AActor_.Method("FRotator GetActorRotation() const", METHOD_TRIVIAL(AActor, GetActorRotation));
    AActor_.Method("void SetActorScale3D(FVector NewScale3D)", METHOD_TRIVIAL(AActor, SetActorScale3D));
    AActor_.Method("void SetActorTickInterval(float32 TickInterval)", METHOD_TRIVIAL(AActor, SetActorTickInterval));
});
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_CppType.h
// 函数: TAngelscriptCppPropertyType<PropertyType>::SetArgument
// 行号: 125-141
// 位置: 参数搬运时直接使用静态 PropertyType，而不是先从 Lua metatable 动态判别 owner/type
// ============================================================================
FProperty* CreateProperty(const FAngelscriptTypeUsage& Usage, const FAngelscriptType::FPropertyParams& Params) const override
{
    return new PropertyType(Params.Outer, Params.PropertyName, RF_Public);
}

void SetArgument(const FAngelscriptTypeUsage& Usage, int32 ArgumentIndex, class asIScriptContext* Context, struct FFrame& Stack, const FAngelscriptType::FArgData& Data) const override
{
    NativeType* ValuePtr = (NativeType*)Data.StackPtr;
    new(ValuePtr) NativeType();

    if (Usage.bIsReference)
    {
        NativeType& ObjRef = Stack.StepCompiledInRef<PropertyType, NativeType>(ValuePtr);
        Context->SetArgAddress(ArgumentIndex, &ObjRef);         // ★ 期望的 UE PropertyType 已经在模板参数里锁死
    }
    else
    {
        Stack.StepCompiledIn<PropertyType>(ValuePtr);           // ★ 运行时直接按固定 PropertyType 取值
        Context->SetArgObject(ArgumentIndex, ValuePtr);
    }
}
```

新增对比结论：

- `UnLua` 的“零胶水”确实降低了项目侧注册工作量，但为了让动态反射路径可用，它在 property/delegate 访问边界上堆了更多运行时校验。
- `Angelscript` 的显式绑定成本更高，但好处是大量约束在“写绑定代码”和“确定 `PropertyType`”时就已经前移，运行时不需要再从 Lua 值动态反推 owner/type。
- 因此，这里的真实对比不只是“谁更自动”，而是“错误更早暴露，还是更晚兜底”。

差距判断：

- “现有 UE 类型零项目侧挂接”为主路径这一点，`Angelscript` 相对 `UnLua` 属于 `没有实现`
- “owner/type/lifetime 防护放在访问边界还是绑定签名前移”属于 `实现方式不同`
- 仅从错误暴露时机看，`Angelscript` 的显式绑定 API 更早失败、更容易在开发阶段定位，属于 `实现质量差异`

### [维度 D3] Blueprint 资产谁持有脚本入口：`UnLua` 让现有 Blueprint 资产保存 module path，`Angelscript` 让脚本类成为 Blueprint 父类

前几轮已经分析过 `GetModuleName()` 和 `BlueprintOverride` 的运行时/编译时差异。这一轮补的是**作者工作流与资产所有权**。`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:138-201,257-372` 说明 `UnLua` 的编辑器入口不是单纯加个菜单，而是直接修改 Blueprint 资产本身：点击 `Bind` 后，工具栏会给 Blueprint 实现 `UUnLuaInterface`、自动推导 `LuaModuleName`、把它写进 `GetModuleName` graph 的默认值、编译 Blueprint 并把图打开；随后 `CreateLuaTemplate / RevealInExplorer / CopyAsRelativePath` 又全部从同一个 `GetModuleName()` 反查脚本路径。这意味着在 `UnLua` 模型里，**资产自己持有“我对应哪个 Lua module”这个事实**。

`Angelscript` 当前可直接验证到的路径则是脚本父类优先。`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningScriptClassToBlueprintTraceTests.cpp:129-232` 先把一段带 `UFUNCTION(BlueprintOverride)` 的脚本源码编译成 `ScriptClass`，再创建 Blueprint child 继承这个脚本类，最后验证 `BeginPlay` 覆写和 `ActorLabel` 默认值都沿着“脚本父类 -> Blueprint 子类 -> Actor 实例”传播。也就是说，`Angelscript` 的 Blueprint 交互主轴不是“给现有资产贴一个脚本路径”，而是“先产出脚本类，再让 Blueprint 继承它”。这也解释了为什么前文的 `ScriptMixin` 不是 `GetModuleName()` 的对位能力：`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h:278-299` 里它只是在签名构建时把 static Unreal API 改写成脚本成员函数，并不承担资产挂接入口。

```
[D3] Asset Ownership Model
UnLua
├─ Existing Blueprint asset                        // 现有资产就是入口
│  ├─ Implement UUnLuaInterface
│  ├─ GetModuleName stores module path
│  └─ Toolbar creates/reveals Lua file
└─ Runtime binds asset -> Lua module               // 运行时按资产反查脚本

Angelscript
├─ Script source compiles to generated UClass      // 先有脚本父类
├─ Blueprint child inherits script class           // 再由 Blueprint 继承
└─ Defaults/override flow parent -> child          // 行为和默认值顺着继承链传播
```

关键源码 [1]：`UnLua` 的编辑器工作流把 module path 和脚手架直接绑在 Blueprint 资产上

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp
// 函数: FUnLuaEditorToolbar::BindToLua_Executed
// 行号: 138-199
// 位置: Bind 操作不是只打标记，而是直接改 Blueprint 资产、填 GetModuleName、编译并打开图
// ============================================================================
if (TargetClass->ImplementsInterface(UUnLuaInterface::StaticClass()))
    return;

const auto Ok = FBlueprintEditorUtils::ImplementNewInterface(
    Blueprint, FTopLevelAssetPath(UUnLuaInterface::StaticClass())); // ★ 直接给资产加接口
if (!Ok)
    return;

FString LuaModuleName;
if (bIsAltDown)
{
    const auto Package = Blueprint->GetTypedOuter(UPackage::StaticClass());
    LuaModuleName = Package->GetName().RightChop(6).Replace(TEXT("/"), TEXT(".")); // ★ 直接从资产包路径推导 module 名
}
else
{
    const auto ModuleLocator = Cast<ULuaModuleLocator>(Settings->ModuleLocatorClass->GetDefaultObject());
    LuaModuleName = ModuleLocator->Locate(TargetClass);                              // ★ 或由 locator 反查
}

InterfaceDesc.Graphs[0]->Nodes[1]->Pins[1]->DefaultValue = LuaModuleName;          // ★ 把 module path 写回 GetModuleName 图
MyBlueprintEditor->Compile();
MyBlueprintEditor->OpenGraphAndBringToFront(GraphToOpen);                           // ★ 直接把作者带到资产内的入口图
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp
// 函数: FUnLuaEditorToolbar::CreateLuaTemplate_Executed / RevealInExplorer_Executed
// 行号: 257-341
// 位置: 后续模板生成、定位脚本文件都继续以 GetModuleName 为单一真相源
// ============================================================================
const auto Func = Class->FindFunctionByName(FName("GetModuleName"));
Class->GetDefaultObject()->ProcessEvent(Func, &ModuleName);                         // ★ 先从资产拿 module path

const auto RelativePath = ModuleName.Replace(TEXT("."), TEXT("/"));
const auto FileName = FString::Printf(TEXT("%s%s.lua"), *GLuaSrcFullPath, *RelativePath);

for (auto TemplateClass = Class; TemplateClass; TemplateClass = TemplateClass->GetSuperClass())
{
    auto RelativeFilePath = "Config/LuaTemplates" / TemplateClassName + ".lua";
    ...
    Content = Content.Replace(TEXT("TemplateName"), *TemplateName)
                     .Replace(TEXT("ClassName"), *UnLua::IntelliSense::GetTypeName(Class));
    FFileHelper::SaveStringToFile(Content, *FileName, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM); // ★ 直接把脚本落到 module 对应路径
}

if (IFileManager::Get().FileExists(*FileName))
{
    FPlatformProcess::ExploreFolder(*FileName);                                     // ★ 资产仍然能反查回脚本文件
}
```

关键源码 [2]：`Angelscript` 当前主路径是“脚本类先生成，再让 Blueprint 继承”

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningScriptClassToBlueprintTraceTests.cpp
// 函数: FAngelscriptLearningScriptClassToBlueprintTraceTest::RunTest
// 行号: 129-232
// 位置: 测试直接把脚本父类 -> Blueprint 子类 -> 实例运行 的链路固定成回归证据
// ============================================================================
UCLASS()
class ALearningScriptClassToBlueprintActor : AActor
{
    UPROPERTY()
    int BeginPlayCount = 0;

    UPROPERTY()
    FString ActorLabel = "ScriptParent";

    UFUNCTION(BlueprintOverride)
    void BeginPlay()
    {
        BeginPlayCount += 1;                                  // ★ 覆写先写在脚本父类里
    }
}

UClass* ScriptClass = CompileScriptModule(...);               // ★ 第一步：先把脚本源码编译成脚本 UClass

Blueprint.BlueprintAsset = CreateTransientLearningBlueprintChild(*this, ScriptClass, TEXT("LearningScriptClassToBlueprintChild"));
Trace.AddStep(TEXT("CreateBlueprintChild"), TEXT("Created a transient Blueprint asset that inherits from the generated script class"));

Trace.AddKeyValue(TEXT("InheritsFromScriptClass"), BlueprintClass->IsChildOf(ScriptClass) ? TEXT("true") : TEXT("false"));
BeginPlayActor(*Actor);

const bool bBeginPlayCountCorrect = TestEqual(
    TEXT("Blueprint actor should preserve the script BeginPlay override"), BeginPlayCount, 1);
const bool bActorLabelCorrect = TestEqual(
    TEXT("Blueprint actor should inherit script property defaults"), ActorLabel, FString(TEXT("ScriptParent"))); // ★ 默认值也从脚本父类向下流动
```

新增对比结论：

- `UnLua` 的 Blueprint 接入优势不只在运行时 patch，而在于它把“脚本入口”直接落在现有 Blueprint 资产上，适合给已有内容资产做低侵入挂接。
- `Angelscript` 的主路径更适合 script-first 开发：先写脚本类，再让 Blueprint 子类消费脚本父类的 override 和默认值。
- 因而，这一维真正的差异不是“能不能和 Blueprint 交互”，而是“脚本入口由资产持有，还是由脚本类型层持有”。

差距判断：

- “现有 Blueprint 资产直接保存脚本 module path 并由编辑器工作流驱动”这一点，`Angelscript` 相对 `UnLua` 属于 `没有实现`
- “脚本父类成为 Blueprint 继承链一等成员”这一点，`UnLua` 相对 `Angelscript` 属于 `没有实现同层能力`
- 两者整体 Blueprint 交互模型属于 `实现方式不同`

### [维度 D5] 调试协议归属的源码纠偏：当前快照里 `UnLua` 依赖外部 Lua 调试器，`Angelscript DebugServer V2` 使用仓内自定义二进制协议

这一轮最重要的新发现，是需要对分析口径做一次纠偏。基于当前仓库快照，`Reference/UnLua/Docs/CN/Debugging.md:1-16` 明确要求用户安装 `LuaPanda / LuaHelper`，手工把 `LuaPanda.lua` 放进工程，再在脚本里写 `require("LuaPanda").start("127.0.0.1",8818)`；与此同时，`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorFunctionLibrary.cpp:27-112` 可见的 editor 侧网络代码只做两件事：脚本目录监听和“向固定 IP/8080 发 UDP 查询新版本”。结合这两点，当前快照能确认到的是：`UnLua` 仓内并没有暴露出一个和 `AngelscriptDebugServer` 同层级的调试会话服务器。前几轮已经确认它有 debug value / stack sampling 底座；这一轮补的是**会话与协议 ownership 并不在仓内**。

`Angelscript` 这边也需要纠偏。`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h:25-79,442-458` 与 `.../AngelscriptDebugServer.cpp:52-90,395-407,667-692,897-1105` 能明确看出 `DebugServer V2` 并不是 DAP/`Content-Length` 风格文本协议，而是“`int32 MessageLength + uint8 MessageType + binary body`”的自定义二进制 envelope；它自己监听 TCP 连接，处理 `StartDebugging / RequestCallStack / SetBreakpoint / RequestVariables / SetDataBreakpoints`，并在 `PauseExecution()` 里主动发送 `HasStopped / HasContinued`。因此，如果严格按当前源码说，正确对比对象应修正为：**`UnLua` 外接 Lua 调试器工作流** vs **`Angelscript` 仓内自定义 DebugServer V2**。

```
[D5] Debug Protocol Ownership (Current Snapshot)
UnLua
├─ Docs -> install LuaPanda / LuaHelper           // 调试前端来自仓外
├─ Script calls require("LuaPanda").start(...)    // 会话从 Lua 脚本侧发起
├─ UE side watcher + update checker               // 仓内可见网络代码不负责调试会话
└─ Session/protocol lives outside repo            // 当前快照未见仓内 DAP/debug server

Angelscript
├─ TCP listener accepts client                    // 仓内自带服务端
├─ int32 length + uint8 type envelope             // 自定义二进制消息封包
├─ StartDebugging -> DebugServerVersion           // 版本握手
├─ RequestCallStack / Variables / Breakpoints     // 调试命令由仓内处理
└─ PauseExecution sends HasStopped/HasContinued   // 停机/继续循环也在仓内
```

关键源码 [1]：当前 `UnLua` 快照把调试会话交给外部 Lua 调试器；仓内可见 socket 代码只做版本查询

```md
<!-- =========================================================================
文件: Reference/UnLua/Docs/CN/Debugging.md
函数: 使用 LuaPanda / LuaHelper 调试
行号: 1-16
位置: 官方工作流明确要求外部 Lua 调试器和脚本侧 start() 调用
=========================================================================== -->
# 调试
## 使用 LuaPanda / LuaHelper 调试

1. 从VSCode应用市场安装 LuaPanda / LuaHelper
2. 从 LuaPanda 官方仓库获取 `LuaPanda.lua`，放入 `{UE工程}/Content/Script` 目录
3. 在 Lua 代码中加入 `require("LuaPanda").start("127.0.0.1",8818)`   <!-- ★ 会话由外部调试器工作流建立 -->

注：调试器依赖 `luasocket`，UnLua已通过扩展插件集成
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorFunctionLibrary.cpp
// 函数: WatchScriptDirectory / FetchNewVersion / OnLuaFilesModified
// 行号: 27-112
// 位置: 当前 editor 侧可见网络逻辑只有目录监听与“检查新版本”，没有仓内调试协议处理器
// ============================================================================
void UUnLuaEditorFunctionLibrary::WatchScriptDirectory()
{
    DirectoryWatcher->RegisterDirectoryChangedCallback_Handle(
        ScriptRootPath, Delegate, DirectoryWatcherHandle);     // ★ 只负责脚本目录监听
}

void UUnLuaEditorFunctionLibrary::FetchNewVersion()
{
    const auto RemoteAddr = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
    RemoteAddr->SetIp(TEXT("101.226.141.148"), bIsValid);
    RemoteAddr->SetPort(8080);

    const auto Client = FUdpSocketBuilder(TEXT("Client")).Build();
    ...
    const FString Msg = FString::Printf(TEXT("cmd=3&tag=glcoud.unlua.update&version=%s&engine=%s&user_name=%s"), ...);
    Client->Send((uint8*)Converted.Get(), Converted.Length(), Sent); // ★ socket 这里用于版本查询，不是调试握手
}

void UUnLuaEditorFunctionLibrary::OnLuaFilesModified(const TArray<FFileChangeData>& FileChanges)
{
    if (Settings.HotReloadMode != EHotReloadMode::Auto)
        return;
    UUnLuaFunctionLibrary::HotReload();                         // ★ 另一个入口是热重载，而不是调试消息分发
}
```

关键源码 [2]：`Angelscript DebugServer V2` 是仓内自定义二进制协议，不是 DAP 文本协议

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h
// 函数: EDebugMessageType / FAngelscriptVariables
// 行号: 25-79, 442-458
// 位置: 协议命令和变量消息结构全部由 runtime 自己定义
// ============================================================================
enum class EDebugMessageType : uint8
{
    StartDebugging,
    StopDebugging,
    RequestCallStack,
    CallStack,
    ClearBreakpoints,
    SetBreakpoint,
    RequestVariables,
    Variables,
    RequestEvaluate,
    Evaluate,
    GoToDefinition,
    DebugServerVersion,
    SetDataBreakpoints,
    ClearDataBreakpoints,                                      // ★ 数据断点也是一等协议消息
};

struct FAngelscriptVariables : FDebugMessage
{
    TArray<FAngelscriptVariable> Variables;                    // ★ 变量查看消息体也是自定义二进制序列化
};
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 函数: SerializeDebugMessageEnvelope / HandleConnectionAccepted / PauseExecution / ProcessMessages
// 行号: 52-90, 395-407, 667-692, 897-1105
// 位置: 从传输封包到握手、停机、变量请求，全部由仓内 DebugServer 自己处理
// ============================================================================
bool SerializeDebugMessageEnvelope(EDebugMessageType MessageType, const TArray<uint8>& Body, TArray<uint8>& OutBuffer)
{
    const int32 MessageLength = static_cast<int32>(sizeof(uint8)) + Body.Num();
    const uint8 MessageTypeByte = static_cast<uint8>(MessageType);
    Writer << const_cast<int32&>(MessageLength);
    Writer << const_cast<uint8&>(MessageTypeByte);             // ★ 先写长度和 uint8 类型，再拼 binary body
    OutBuffer.Append(Body);
}

Listener = new FTcpListener(FIPv4Endpoint(FIPv4Address::Any, Port));
Listener->OnConnectionAccepted().BindRaw(this, &FAngelscriptDebugServer::HandleConnectionAccepted); // ★ 仓内自己监听客户端

SendMessageToAll(EDebugMessageType::HasStopped, *StopMessage);
while (bIsPaused)
{
    ProcessMessages();
    FPlatformProcess::Sleep(0);                               // ★ 停机循环也由 DebugServer 维护
}

else if (MessageType == EDebugMessageType::StartDebugging)
{
    *Datagram << Msg;
    bIsDebugging = true;
    DebugServerVersionMessage.DebugServerVersion = DEBUG_SERVER_VERSION;
    SendMessageToClient(Client, EDebugMessageType::DebugServerVersion, DebugServerVersionMessage); // ★ 版本握手
}
else if (MessageType == EDebugMessageType::RequestCallStack)
{
    CallstackRequests.Add(Client);                            // ★ 调用栈请求进入仓内队列
}
else if (MessageType == EDebugMessageType::RequestVariables)
{
    ...
    Vars.Variables.Add(Var);
    SendMessageToClient(Client, EDebugMessageType::Variables, Vars); // ★ 变量请求/响应也走同一自定义协议
}
```

新增对比结论：

- 基于当前源码快照，`UnLua` 应被描述为“提供调试数据采样底座，并接入外部 Lua 调试器工作流”，而不是“仓内自带 DAP server”。
- `Angelscript DebugServer V2` 的优势是仓内闭环，但它的协议形态是自定义二进制 envelope，不是 DAP 文本协议。
- 因此，这一维最重要的不是“谁支持调试”，而是“调试后端 ownership 在仓内，还是在仓外工具链”。

差距判断：

- “仓内自带同层级调试会话服务器”这一点，`UnLua` 相对当前 `Angelscript` 属于 `没有实现同层能力`
- “是否直接采用 DAP 协议”按当前可见源码，两边都不能直接下结论为仓内 DAP，实现口径应修正为 `实现方式不同`
- `Angelscript` 在会话托管、断点管理、变量请求与停机循环的一体化程度上更完整，属于 `实现质量差异`

---

## 深化分析 (2026-04-08 19:36:36)

### [维度 D4] 删除/重命名语义：`UnLua` 没有独立的 remove queue，`Angelscript` 明确建模 add/remove/rename window

前文已经把 `UnLua` 热重载的“修改文件 -> sandbox 重载 -> merge old/new object graph”讲清了。这一轮补的是**文件系统事件语义**。`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorFunctionLibrary.cpp:112-118` 表明 editor 回调完全不看 `FFileChangeData::Action`，只要目录有变化且模式为 `Auto`，就直接调用一次 `UUnLuaFunctionLibrary::HotReload()`；Lua 侧 `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:604-624` 也只是遍历 `loaded_module_times`，按“旧 module 名 -> 当前时间戳”比较筛出候选。也就是说，当前快照里的 `UnLua` 并没有把“新增 / 删除 / 重命名”先变成不同的 reload 事件，而是统一折叠成“再次尝试按旧名字加载模块”。

更关键的是失败路径。`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:553-601` 在 `sandbox.load(module_name)` 返回 `nil`，或 `xpcall(func, ...)` 失败时，直接 `sandbox.exit(); return`；但同文件 `1-18,147-169` 可见旧模块仍保存在 `loaded_modules` 与 `package.loaded` 中，并没有对应的删除分支。因此，从当前源码严格推断：**删除/重命名在 `UnLua` 里更像“触发了一次失败重载，旧模块继续存活”**，而不是显式的 remove/add 迁移。

`Angelscript` 的 watcher 责任则明显更重。`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp:43-89` 会把 `.as` 文件删除写入 `FileDeletionsDetectedForReload`，新增/修改写入 `FileChangesDetectedForReload`，文件夹删除还会枚举已加载脚本，文件夹新增会扫描新目录中的 `.as`；随后 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2749-2755` 又专门把删除队列延后 `0.2s` 合并，以捕获 rename window。`Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp:194-223` 还把“重命名 = 一个 remove + 一个 add”固定成自动化回归。

```
[D4-Deep] Delete And Rename Semantics
UnLua
├─ DirectoryWatcher callback                       // 不看具体 Action
├─ HotReload() scans loaded_module_times           // 只按旧 module 名和时间戳比较
├─ sandbox.load(old_name) on missing file          // 删除/改名后仍按旧名尝试加载
└─ sandbox.exit(); old module survives             // 失败时没有独立 remove path

Angelscript
├─ QueueScriptFileChanges()                        // 先把文件事件分类
│  ├─ FCA_Removed -> FileDeletionsDetectedForReload
│  └─ FCA_Added/Modified -> FileChangesDetectedForReload
├─ 0.2s rename window                             // 延后消费删除队列，等待 rename 对侧出现
└─ PerformHotReload(FileList)                     // add/remove 一起进入 reload 决策
```

关键源码 [1]：`UnLua` 的前端回调不保留文件事件语义；Lua 侧失败时保留旧模块

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorFunctionLibrary.cpp
// 函数: UUnLuaEditorFunctionLibrary::OnLuaFilesModified
// 行号: 112-118
// 位置: editor 层不区分 Added / Removed / Modified，统一触发 HotReload
// ============================================================================
void UUnLuaEditorFunctionLibrary::OnLuaFilesModified(const TArray<FFileChangeData>& FileChanges)
{
    const auto& Settings = *GetDefault<UUnLuaEditorSettings>();
    if (Settings.HotReloadMode != EHotReloadMode::Auto)
        return;
    UUnLuaFunctionLibrary::HotReload();                         // ★ 不消费 FileChanges 内的 Action
}
```

```lua
-- ============================================================================
-- 文件: Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua
-- 函数: reload_modules / M.reload
-- 行号: 13-18, 147-169, 553-624
-- 位置: 旧模块常驻 loaded_modules/package.loaded；重载候选只来自时间戳扫描
-- ============================================================================
local loaded_modules = setmetatable({}, { __mode = "v" })      -- ★ 旧模块缓存

local function require(module_name, ...)
    if package.loaded[module_name] ~= nil then
        return package.loaded[module_name], nil                -- ★ 先返回旧缓存
    end
    ...
    if loaded_modules[module_name] == nil then
        loaded_modules[module_name] = new_module
        package.loaded[module_name] = new_module
        loaded_module_times[module_name] = get_last_modified_time(module_name)
    end
end

local function reload_modules(module_names)
    ...
    local func, env = sandbox.load(module_name)
    if func ~= nil then
        local ok, new_module = xpcall(func, load_error_handler)
        if not ok then
            sandbox.exit()
            return                                          -- ★ 失败时直接退出，本轮不清掉旧模块
        end
        ...
    else
        sandbox.exit()
        return                                              -- ★ 文件不存在/按旧名找不到时同样提前退出
    end
end

function M.reload(module_names)
    ...
    for module_name, time in pairs(loaded_module_times) do
        local current_time = get_last_modified_time(module_name)
        if current_time ~= time then
            modified_modules[#modified_modules + 1] = module_name
            loaded_module_times[module_name] = current_time   -- ★ 只按旧名字比较时间戳
        end
    end
end
```

关键源码 [2]：`Angelscript` 把 add/remove/rename window 先建模，再消费到 reload 队列

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp
// 函数: AngelscriptEditor::Private::QueueScriptFileChanges
// 行号: 43-89
// 位置: watcher 层先把 script add/remove/folder add/folder remove 分类入队
// ============================================================================
void QueueScriptFileChanges(const TArray<FFileChangeData>& Changes, const TArray<FString>& RootPaths, FAngelscriptEngine& Engine, IFileManager& FileManager, const FEnumerateLoadedScripts& EnumerateLoadedScripts)
{
    for (const FFileChangeData& Change : Changes)
    {
        ...
        if (AbsolutePath.EndsWith(TEXT(".as")))
        {
            if (Change.Action == FFileChangeData::EFileChangeAction::FCA_Removed)
            {
                Engine.FileDeletionsDetectedForReload.AddUnique({ AbsolutePath, RelativePath }); // ★ 删除单独入队
            }
            else
            {
                Engine.FileChangesDetectedForReload.AddUnique({ AbsolutePath, RelativePath });   // ★ 新增/修改单独入队
            }
            continue;
        }

        if (Change.Action == FFileChangeData::EFileChangeAction::FCA_Removed)
        {
            for (const FAngelscriptEngine::FFilenamePair& LoadedScript : EnumerateLoadedScripts(AbsolutePath / TEXT("")))
            {
                Engine.FileDeletionsDetectedForReload.AddUnique(LoadedScript);                    // ★ 文件夹删除枚举已加载脚本
            }
        }
        else if (Change.Action == FFileChangeData::EFileChangeAction::FCA_Added && FileManager.DirectoryExists(*AbsolutePath))
        {
            FAngelscriptEngine::FindScriptFiles(..., ContainedScriptFiles, false, false);         // ★ 文件夹新增扫描内部 .as
            ...
        }
    }
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: FAngelscriptEngine::CheckForHotReload
// 行号: 2749-2755
// 位置: 删除队列延后消费，为 rename window 保留对侧新增事件
// ============================================================================
if (FileList.Num() != 0 || FPlatformTime::Seconds() - LastFileChangeDetectedTime > 0.2)
{
    for (const auto& DeletedFile : FileDeletionsDetectedForReload)
        FileList.AddUnique(DeletedFile);                      // ★ 删除不会立刻吞掉，而是等一个 rename window
    FileDeletionsDetectedForReload.Empty();
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp
// 函数: FAngelscriptDirectoryWatcherRenameWindowTest::RunTest
// 行号: 194-223
// 位置: 重命名语义被固定为 remove + add 两个队列项
// ============================================================================
const TArray<FFileChangeData> Changes = {
    MakeFileChange(OldAbsolutePath, FFileChangeData::FCA_Removed),
    MakeFileChange(NewAbsolutePath, FFileChangeData::FCA_Added)
};

AngelscriptEditor::Private::QueueScriptFileChanges(Changes, Engine->AllRootPaths, *Engine, FileManager, ...);

TestEqual(TEXT("... should queue one removed script"), Engine->FileDeletionsDetectedForReload.Num(), 1);
TestEqual(TEXT("... should queue one added script"), Engine->FileChangesDetectedForReload.Num(), 1); // ★ rename 被明确建模成双事件
```

新增对比结论：

- `UnLua` 当前快照的热重载强项仍然在 Lua VM 内的对象图修补，不在文件事件建模；删除/改名最终会退化成“按旧名重载失败，旧模块继续存活”。
- `Angelscript` 把文件系统语义前移到 watcher 层，收益不是“更自动”，而是 rename / folder remove / delayed delete 这些边界条件都能被显式表达和测试。

差距判断：

- “显式 add/remove/rename queue + watcher 自动化回归”这一层，`UnLua` 相对 `Angelscript` 属于 `没有实现`
- “状态保持主要发生在 VM 合并层还是 watcher + compile pipeline 联动层”属于 `实现方式不同`
- 在文件系统边界条件的可观测性与可测试性上，`Angelscript` 更强，属于 `实现质量差异`

### [维度 D2] 反射描述缓存生命周期：`UnLua` 是 `per-env` 惰性缓存，`Angelscript` 是持久化 `bind database`

前文已经分析过“零胶水反射 vs 显式绑定”的主路径。这一轮补的是**绑定描述本身活多久、由谁负责失效**。`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:102-112,265-274,721-730` 显示 `FClassRegistry` 是跟着 `FLuaEnv` 一起创建的运行时 registry；它注册 `OnAsyncLoadingFlushUpdate` 和 `GUObjectArray` 删除监听，说明 registry 的第一职责不是生成静态清单，而是跟着 live UObject / async loading 状态维护一份“当前 LuaEnv 可安全使用的反射描述缓存”。

具体到 `FClassRegistry` 与 `FClassDesc`，`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:59-137,265-322` 与 `.../Private/ReflectionUtils/ClassDesc.cpp:29-58,152-179` 能看到完整生命周期：首次访问 metatable 时才 `RegisterReflectedType()`；如果 registry 里已有 `FClassDesc` 但 `Struct` 已失效，`PushMetatable()` 会先 `Unregister(Ret, true)`；`NotifyUObjectDeleted()` 又会在 `UObject` 销毁时 `UnLoad()` 并清空 `Fields / Properties / Functions`。因此，`UnLua` 的反射描述虽然“零胶水”，但它不是一张稳定的全局类型表，而是一份**按需生成、按对象生命周期失效的运行时缓存**。

`Angelscript` 的缓存模型则完全不同。`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h:1-5,123-139` 直接把 `FAngelscriptBindDatabase` 定义成“editor 生成、cooked game 复用”的 cache file；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:714-755,1489-1505` 与 `.../Binds/Bind_UStruct.cpp:865-905,1175-1183,1415-1418` 表明类/结构体的 bind 描述会被写进 `Classes` / `Structs` 数组，再由后续 bind pass 回放；`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp:874-919` 还能把这份 database 导出成 CSV。换句话说，`Angelscript` 的绑定缓存更像**持久化的编译产物**，而不是 live object 驱动的临时反射镜像。

```
[D2-Deep] Reflection Descriptor Lifetime
UnLua
├─ FLuaEnv creates registries                      // 每个 LuaEnv 自带一套 registry
├─ RegisterReflectedType() on demand              // 首次访问时才建 FClassDesc
├─ GUObject delete / async loading hooks          // 跟踪 live UObject 生命周期
└─ FClassDesc::UnLoad() clears fields             // 失效后清空字段/函数缓存

Angelscript
├─ Editor bind pass enumerates UE types           // 先生成 bind 描述
├─ FAngelscriptBindDatabase::{Classes,Structs}    // 描述进入全局数据库
├─ Runtime/cooked bind pass replays database      // 后续阶段按数据库回放
└─ StateDump exports BindDatabase_*.csv           // 数据库可被导出和审计
```

关键源码 [1]：`UnLua` 把反射描述缓存绑在 `FLuaEnv` 和 live UObject 生命周期上

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp
// 函数: FLuaEnv ctor / NotifyUObjectDeleted / RegisterDelegates
// 行号: 102-112, 265-274, 721-730
// 位置: registry 跟着 LuaEnv 创建，并通过对象删除/async loading 委托维持有效性
// ============================================================================
UELib::Open(L);

ObjectRegistry = new FObjectRegistry(this);
ClassRegistry = new FClassRegistry(this);
ClassRegistry->Initialize();                                   // ★ 每个 LuaEnv 一份 registry

FunctionRegistry = new FFunctionRegistry(this);
DelegateRegistry = new FDelegateRegistry(this);
ContainerRegistry = new FContainerRegistry(this);
PropertyRegistry = new FPropertyRegistry(this);
EnumRegistry = new FEnumRegistry(this);

void FLuaEnv::NotifyUObjectDeleted(const UObjectBase* ObjectBase, int32 Index)
{
    UObject* Object = (UObject*)ObjectBase;
    ...
    ClassRegistry->NotifyUObjectDeleted(Object);               // ★ UObject 删除时驱动 registry 失效
    EnumRegistry->NotifyUObjectDeleted(Object);
}

OnAsyncLoadingFlushUpdateHandle = FCoreDelegates::OnAsyncLoadingFlushUpdate.AddRaw(this, &FLuaEnv::OnAsyncLoadingFlushUpdate);
GUObjectArray.AddUObjectDeleteListener(this);                  // ★ 异步加载和对象删除都能触发缓存维护
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp
// 函数: RegisterReflectedType / PushMetatable / NotifyUObjectDeleted
// 行号: 59-137, 265-322
// 位置: metatable 访问时惰性注册；发现失效 struct 时主动剔除旧描述
// ============================================================================
FClassDesc* FClassRegistry::RegisterReflectedType(const char* MetatableName)
{
    FClassDesc* Ret = Find(MetatableName);
    if (Ret)
    {
        Classes.FindOrAdd(Ret->AsStruct(), Ret);               // ★ 命中缓存则复用
        return Ret;
    }
    ...
    Ret = RegisterInternal(StructType, UTF8_TO_TCHAR(MetatableName)); // ★ 首次访问才建描述
    return Ret;
}

bool FClassRegistry::PushMetatable(lua_State* L, const char* MetatableName)
{
    int Type = luaL_getmetatable(L, MetatableName);
    if (Type == LUA_TTABLE)
    {
        FClassDesc* Ret = Find(MetatableName);
        if (Ret && Ret->IsClass() && !Ret->IsStructValid())
        {
            Unregister(Ret, true);                             // ★ 旧 struct 失效时先剔掉 metatable
        }
        else
        {
            return true;
        }
    }
    ...
}

void FClassRegistry::NotifyUObjectDeleted(UObject* Object)
{
    Unregister((UStruct*)Object);                              // ★ UObject 删除直接驱动 class desc 卸载
}
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/ClassDesc.cpp
// 函数: FClassDesc::Load / FClassDesc::UnLoad
// 行号: 152-179
// 位置: 失效描述会清空字段/属性/函数，下次访问再懒加载
// ============================================================================
void FClassDesc::Load()
{
    if (Struct.IsValid())
        return;
    if (GIsGarbageCollecting)
        return;

    UnLoad();
    ...
    Struct = Found;
    RawStructPtr = Found;                                      // ★ 仅在需要时重新找回 UStruct
}

void FClassDesc::UnLoad()
{
    Fields.Empty();
    Properties.Empty();
    Functions.Empty();                                         // ★ 缓存字段全部清空，等待下次惰性重建
}
```

关键源码 [2]：`Angelscript` 把 bind 描述做成持久化 database，而不是 live registry

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h
// 函数: FAngelscriptBindDatabase
// 行号: 1-5, 123-139
// 位置: 绑定数据库的定位就是 editor 生成、cooked 复用的缓存文件
// ============================================================================
/*
    The bind database is a cache file generated in the editor that is used in the cooked
    game to correctly bind all C++ symbols to angelscript without requiring full
    editor metadata to be in the cooked game.
*/

class FAngelscriptBindDatabase
{
public:
    static FAngelscriptBindDatabase& Get();
    void Save(const FString& Filename);
    void Load(const FString& Filename, bool bGeneratingPrecompiledData);
    void Clear();

    TArray<FAngelscriptStructBind> Structs;
    TArray<FAngelscriptClassBind> Classes;                     // ★ 绑定描述以全局数据库形式持久化
    ...
};
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp
// 函数: Bind_UClass database replay / database population
// 行号: 714-755, 1489-1505
// 位置: 一轮 pass 读取 database 回放，另一轮 pass 把新描述写回 database
// ============================================================================
for (auto& DBBind : FAngelscriptBindDatabase::Get().Classes)
{
    UClass* Class = FindObject<UClass>(nullptr, *DBBind.UnrealPath);
    ...
    DBBind.ResolvedClass = Class;
    BindUClass(Class, DBBind.TypeName);                        // ★ 先按 database 回放类型
}

for (auto& DBBind : FAngelscriptBindDatabase::Get().Classes)
{
    ...
    for (auto& DBFunc : DBBind.Methods)
    {
        UFunction* Function = Class->FindFunctionByName(*DBFunc.UnrealPath);
        ...
        BindBlueprintCallable(ClassType.ToSharedRef(), Function, DBFunc); // ★ 再按 database 回放方法
    }
}

FAngelscriptBinds Binds = FAngelscriptBinds::ExistingClass(TypeName);
...
BindOrder.DBBind.TypeName = TypeName;
BindOrder.DBBind.UnrealPath = BindOrder.Class->GetPathName();
FAngelscriptBindDatabase::Get().Classes.Add(BindOrder.DBBind); // ★ 新描述写回全局 database
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UStruct.cpp
// 函数: Bind_StructDetails
// 行号: 865-905, 1175-1183, 1415-1418
// 位置: struct 绑定也走同一数据库：读取已有记录，生成新记录，再持久化
// ============================================================================
for (auto& DBBind : FAngelscriptBindDatabase::Get().Structs)
{
    UScriptStruct* Struct = DBBind.ResolvedStruct;
    ...
    auto Binds = FAngelscriptBinds::ExistingClass(DBBind.TypeName);       // ★ 读取已有 struct bind 描述
    ...
}

FAngelscriptStructBind DBBind;
DBBind.TypeName = TypeName;
DBBind.UnrealPath = Struct->GetPathName();                                // ★ 生成本轮新的 struct 描述
...
FAngelscriptBindDatabase::Get().Structs.Add(DBBind);                      // ★ 写回 database
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp
// 函数: DumpBindDatabaseStructs / DumpBindDatabaseClasses
// 行号: 874-919
// 位置: bind database 可被直接导出，说明它被当作正式产物审计
// ============================================================================
const FAngelscriptBindDatabase& BindDatabase = FAngelscriptBindDatabase::Get();
...
for (const FAngelscriptStructBind& StructBind : BindDatabase.Structs)
{
    Writer.AddRow({ StructBind.TypeName, StructBind.TypeName, LexToString(StructBind.Properties.Num()), TEXT("0") });
}
...
for (const FAngelscriptClassBind& ClassBind : BindDatabase.Classes)
{
    Writer.AddRow({ ClassBind.TypeName, ClassBind.TypeName, LexToString(ClassBind.Methods.Num()) }); // ★ database 可直接体检/导出
}
```

新增对比结论：

- `UnLua` 的“零胶水”不只是少写绑定代码，还意味着绑定描述要跟着 live `UObject` 状态一起漂移；它更像运行时镜像，不像稳定产物。
- `Angelscript` 的绑定成本更高，但收益之一是能把绑定结果固化为 `bind database`，再被 cooked/runtime/state dump 复用和审计。
- 所以这里真正的差异不只是“自动 vs 手写”，而是“临时反射缓存” vs “持久化绑定产物”。

差距判断：

- “可持久化、可导出、可在 cooked 复用的 bind database”这一层，`UnLua` 相对 `Angelscript` 属于 `没有实现`
- “按 live UObject 生命周期失效，还是按 editor 生成产物回放”属于 `实现方式不同`
- 在绑定结果可审计性与可复用性上，`Angelscript` 更强；在运行时即插即用接入现有 UE 类型上，`UnLua` 更轻，这里属于双向取舍，不直接判单边优劣

### [维度 D9] 测试执行入口所有权：`UnLua` 把测试挂到样例工程，`Angelscript` 把 runner 收进仓库

前文已经覆盖 `UnLuaTestSuite` 的宏 DSL、spec / regression / perf 分层，以及 `Angelscript` 的 hot reload test runner。这一轮补的是**“谁拥有测试执行入口”**。`Reference/UnLua/Plugins/UnLuaTestSuite/UnLuaTestSuite.uplugin:17-29` 表明 `UnLuaTestSuite` 是 `EnabledByDefault=false` 的独立 runtime 插件；`Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/UnLuaTestSuite.Build.cs:33-64` 又把它设成 `PrecompileTargetsType.Any`。随后 `Reference/UnLua/TPSProject.uproject:16-33` 明确启用了 `RuntimeTests`、`EditorTests`、`FunctionalTestingEditor` 和 `UnLuaTestSuite`。这说明当前快照下，`UnLua` 的测试主要是**作为“随样例工程启用的 UE Automation 内容”存在**。

`Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/UnLuaTestCommon.cpp:60-97` 进一步说明 orchestration 也在工程内：`SetUp()` 会手动 `UnLua::Startup()`、创建 `UGameInstance`、决定是临时 `UWorld` 还是 `AutomationOpenMap(MapName)`。因此，`UnLua` 这里的重点是“给插件用户带一套可搬走的测试工程资产与 harness”，而不是“在仓库根给出统一脚本 runner”。

`Angelscript` 的 ownership 则更偏仓库层。`Tools/RunTests.ps1:42-97,240-275` 统一读取 `AgentConfig.ini`、加 worktree mutex、强制 `TimeoutMs`、设置 `-ABSLOG/-ReportExportPath`、默认 `-NullRHI` 并输出 `RunMetadata.json`；`Tools/RunTestSuite.ps1:41-84,126-160` 又把 `Smoke / NativeCore / RuntimeCpp / HotReload / Debugger / ScenarioSamples` 固化为具名 suite；`Documents/Guides/Test.md:5-11,120-131,258-260` 更进一步把这套 runner 明确规定为本仓库标准入口。两者相比，差的不是“有没有自动化测试”，而是谁负责把它变成**可重复、可脚本化、可给 CI/Agent 直接调用的命令契约**。

```
[D9-Deep] Test Entry Ownership
UnLua
├─ UnLuaTestSuite.uplugin                          // 测试以 opt-in plugin 存在
├─ TPSProject.uproject enables test plugins       // 样例工程决定入口
├─ FUnLuaTestBase::SetUp() boots world/map        // orchestration 在工程内
└─ Primary path: UE Automation in sample project  // 通过工程跑测试

Angelscript
├─ Tools/RunTests.ps1                             // 仓库级标准 runner
├─ timeout + mutex + output layout + NullRHI      // 执行契约被脚本固化
├─ Tools/RunTestSuite.ps1                         // 具名 suite 调度层
└─ Guides/Test.md                                 // 本地 / Agent / CI 共用同一入口
```

关键源码 [1]：`UnLua` 当前快照把测试入口绑在 opt-in plugin + 样例工程上

```json
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/UnLuaTestSuite.uplugin
// 函数: 插件描述
// 行号: 17-29
// 位置: 测试插件默认不启用，需要宿主工程显式打开
// ============================================================================
"EnabledByDefault": false,
"Modules": [
    {
        "Name": "UnLuaTestSuite",
        "Type": "Runtime",
        "LoadingPhase": "Default"
    }
]                                                           // ★ 测试首先是一个 opt-in runtime plugin
```

```csharp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/UnLuaTestSuite.Build.cs
// 函数: UnLuaTestSuite::UnLuaTestSuite
// 行号: 33-64
// 位置: 测试模块可预编译到任意 target，但执行入口仍由宿主工程决定
// ============================================================================
PublicDependencyModuleNames.AddRange(
    new[]
    {
        "Core",
        "CoreUObject",
        "Engine",
        "Slate"
    }
);

PrivateDependencyModuleNames.AddRange(
    new[]
    {
        "Lua",
        "UnLua",
        "UMG"
    }
);

PrecompileForTargets = PrecompileTargetsType.Any;            // ★ 便于搬运，但不等于仓库自带 runner
```

```json
// ============================================================================
// 文件: Reference/UnLua/TPSProject.uproject
// 函数: 工程插件列表
// 行号: 16-33
// 位置: 样例工程显式打开 RuntimeTests / EditorTests / FunctionalTestingEditor / UnLuaTestSuite
// ============================================================================
"Plugins": [
    { "Name": "RuntimeTests", "Enabled": true },
    { "Name": "EditorTests", "Enabled": true },
    { "Name": "FunctionalTestingEditor", "Enabled": true },
    { "Name": "UnLuaTestSuite", "Enabled": true }            // ★ 当前主入口是样例工程的 Automation 运行环境
]
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/UnLuaTestCommon.cpp
// 函数: FUnLuaTestBase::SetUp
// 行号: 60-97
// 位置: orchestration 在测试基类里完成：启动 UnLua、建世界、按需开地图
// ============================================================================
bool FUnLuaTestBase::SetUp()
{
    UnLua::Startup();
    L = UnLua::GetState();                                   // ★ 测试启动时手动拉起 UnLua

    GameInstance = NewObject<UGameInstance>(GEngine);
    GameInstance->InitializeStandalone();
    WorldContext = GameInstance->GetWorldContext();

    const auto& MapName = GetMapName();
    if (MapName.IsEmpty())
    {
        if (!WorldContext->World())
        {
            const auto World = UWorld::CreateWorld(EWorldType::Game, false, "UnLuaTest");
            ...
        }
    }
    else if (InstantTest())
    {
        GEngine->LoadMap(*WorldContext, URL, nullptr, Error); // ★ 需要地图时在工程内直接拉起
    }
    else
    {
        AutomationOpenMap(MapName);                           // ★ latent test 走 UE Automation 地图入口
    }
}
```

关键源码 [2]：`Angelscript` 把 runner/suite/规则都收敛到仓库根

```powershell
# ============================================================================
# 文件: Tools/RunTests.ps1
# 函数: 参数解析与执行主路径
# 行号: 42-97, 240-275
# 位置: 标准 runner 统一收口超时、互斥锁、日志/报告目录、Editor 命令行
# ============================================================================
$agentConfig = Resolve-AgentConfiguration -ProjectRoot $projectRoot
$resolvedTimeoutMs = Resolve-TimeoutMs -RequestedTimeoutMs $TimeoutMs -DefaultTimeoutMs $defaultTimeoutMs -ParameterName 'TimeoutMs'
$editorCmd = Join-Path $agentConfig.EngineRoot 'Engine\\Binaries\\Win64\\UnrealEditor-Cmd.exe'

$worktreeMutex = Acquire-NamedMutex -Name $worktreeMutexName -TimeoutMs 0
if ($null -eq $worktreeMutex) {
    Write-Host '[error] Another build or test command is already running for this worktree.'
    return
}

$argumentList = @(
    $agentConfig.ProjectFile
    "-ExecCmds=Automation RunTests $target; Quit"
    '-TestExit=Automation Test Queue Empty'
    '-BUILDMACHINE'
    '-Unattended'
    '-NoPause'
    '-NoSplash'
    '-stdout'
    '-FullStdOutLogOutput'
    '-UTF8Output'
    "-ABSLOG=$($outputLayout.LogPath)"
    "-ReportExportPath=$($outputLayout.ReportPath)"
    '-NOSOUND'
)

if (-not $Render) {
    $argumentList += '-NullRHI'                              # ★ headless 默认值也由 runner 统一决定
}

Write-Utf8JsonFile -Path $metadataPath -Value ([PSCustomObject]@{
    TimeoutMs   = $resolvedTimeoutMs
    OutputRoot  = $outputLayout.OutputRoot
    LogPath     = $outputLayout.LogPath
    ReportPath  = $outputLayout.ReportPath
    ExitCode    = $scriptExitCode
})                                                           # ★ 每次 run 都有独立元数据产物
```

```powershell
# ============================================================================
# 文件: Tools/RunTestSuite.ps1
# 函数: suiteDefinitions / 执行循环
# 行号: 41-84, 126-160
# 位置: 仓库把常用测试波次固化成具名 suite，而不是散落的手写命令
# ============================================================================
$suiteDefinitions = [ordered]@{
    "Smoke" = @(
        @{ Prefix = "Angelscript.CppTests.MultiEngine"; Label = "MultiEngine" }
        @{ Prefix = "Angelscript.CppTests.Engine.DependencyInjection"; Label = "DependencyInjection" }
        @{ Prefix = "Angelscript.CppTests.Subsystem"; Label = "Subsystem" }
        @{ Prefix = "Angelscript.TestModule.Engine.BindConfig"; Label = "BindConfig" }
        @{ Prefix = "Angelscript.TestModule.Shared.EngineHelper"; Label = "SharedEngineHelper" }
        @{ Prefix = "Angelscript.TestModule.Parity"; Label = "Parity" }
    )
    "HotReload" = @(
        @{ Prefix = "Angelscript.TestModule.HotReload"; Label = "HotReload" }
    )
    "Debugger" = @(
        @{ Prefix = "Angelscript.CppTests.Debug."; Label = "CppDebugger" }
        @{ Prefix = "Angelscript.TestModule.Debugger."; Label = "TestModuleDebugger" }
    )
}

for ($index = 0; $index -lt $selectedSuite.Count; ++$index) {
    ...
    & powershell.exe @argList
    if ($LASTEXITCODE -ne 0) {
        throw "Suite '$Suite' failed while executing prefix '$($entry.Prefix)' (label '$runLabel')."
    }
}                                                           # ★ suite 失败语义和标签命名被脚本统一固定
```

```md
<!-- =========================================================================
文件: Documents/Guides/Test.md
函数: 强制规则 / runner 行为
行号: 5-11, 120-131, 258-260
位置: 文档层明确规定本仓库测试只能走官方 runner
=========================================================================== -->
- 本仓库的标准自动化测试入口是 `Tools\RunTests.ps1`
- 具名 suite 只能通过 `Tools\RunTestSuite.ps1` 调度
- 所有测试命令都必须显式带超时，且超时不得超过 `900000ms`
- 每次测试都必须写入自己的独立输出目录

`Tools\RunTests.ps1` 会自动：
- 读取当前 worktree 的 `AgentConfig.ini`
- 以统一参数启动 `UnrealEditor-Cmd.exe`
- 通过 `-ABSLOG` 与 `-ReportExportPath` 把日志和报告写入当前 run 的独立目录

- `Tools\RunTests.ps1` / `Tools\RunTestSuite.ps1` 负责仓库内标准自动化测试入口、日志、摘要和超时收口
- 常规本地回归、AI Agent 执行和普通 CI 不要绕过官方 runner 去手写 `RunUAT` / `UnrealEditor-Cmd.exe`
```

新增对比结论：

- `UnLua` 当前测试体系的优势是“可随插件和样例工程一起搬走”，适合 issue 回归、资产驱动验证和对外演示。
- `Angelscript` 的优势则是 runner ownership 明确：本地、Agent、CI 共用同一组脚本入口，超时、输出、suite slicing 都有仓库级契约。
- 因而这一维的关键差异不是“有没有测试”，而是“测试入口归样例工程所有，还是归仓库 runner 所有”。

差距判断：

- “仓库根标准 runner + suite 调度层 + 强制执行规范”这一层，`UnLua` 相对 `Angelscript` 属于 `没有实现`
- “测试夹具更多跟着样例工程和内容资产走，还是跟着仓库级脚本 runner 走”属于 `实现方式不同`
- 对于 CI / Agent 直接接入的可重复性，`Angelscript` 更强；对于把测试随插件整体分发给外部工程复用，`UnLua` 的 opt-in test plugin 更轻，这里同样是取舍差异

---

## 深化分析 (2026-04-08 19:48:03)

### [维度 D2] “零胶水”边界的源码补充：`UnLua` 实际是“反射主路径 + 静态导出底座”双层模型

前几轮已经把 `UnLua` 的主路径说明为“直接复用 `UClass/UFunction/FProperty` 反射对象”。这一轮补的是**这条主路径的边界**。当前源码并不是“所有类型都走纯反射、完全没有手写层”。`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaBase.h:37-58,85-90` 明确把 `ITypeOps` 分成普通反射类型与 `StaticExported` 类型两档；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaEx.h:442-499,547-548` 又提供 `BEGIN_EXPORT_* / EXPORT_UNTYPED_CLASS / ADD_LIB` 这一整套宏式注册层；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/BaseLib/LuaLib_Object.cpp:281-329` 则直接把 `UObject`、`FSoftObjectPtr` 这类基础入口做成静态导出类。

更关键的是这两层在运行时待遇不同。`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/LowLevel.cpp:135-155` 的 `CheckPropertyOwner()` 一上来就对 `StaticExported` 直接放行，不再走 `FProperty->GetOwnerClass()` 那套 owner 校验。这说明 `UnLua` 所谓“零胶水”更准确的说法是：**项目业务类型默认零胶水；底层基础库和包装类型仍靠一层集中式手写导出做兜底**。对比之下，`Angelscript` 的手写层并没有被压缩到底座，而是直接铺在常规 gameplay API 表面，例如 `Bind_AActor.cpp` 这种 `ExistingClass(...).Method(...)` 仍是主路径。

```
[D2-Deep] Binding Tier Boundary
UnLua
├─ Reflected gameplay types -> UClass / FProperty bridge    // 业务类型主路径仍是反射
├─ Static exported base libs -> EXPORT_* macros             // 容器、指针、基础 helper 走静态导出
└─ LowLevel special-case StaticExported                     // 运行时约束也按两层处理

Angelscript
├─ ExistingClass(...).Method(...)                           // 常规 API 直接显式注册
└─ Reflective fallback                                      // 仅在受控范围内补洞
```

关键源码 [1]：`UnLua` 明确保留了一层静态导出类型体系

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaBase.h
// 函数: UnLua::ITypeOps / UnLua::IExportedProperty
// 行号: 37-58, 85-90
// 位置: 类型系统从根上区分“普通反射类型”和“静态导出类型”
// ============================================================================
struct ITypeOps
{
    ITypeOps() { StaticExported = false; };                  // ★ 默认走普通反射路径
    ...
    bool StaticExported;
};

struct IExportedProperty : public ITypeOps
{
    IExportedProperty() { StaticExported = true;}            // ★ 静态导出属性单独打标
    virtual ~IExportedProperty() {}
    virtual void Register(lua_State *L) = 0;
};
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaEx.h
// 函数: EXPORT_UNTYPED_CLASS / BEGIN_EXPORT_CLASS_EX / ADD_LIB
// 行号: 442-499, 547-548
// 位置: UnLua 通过宏式 helper 建立静态导出类注册表
// ============================================================================
#define EXPORT_UNTYPED_CLASS(Name, bIsReflected, Lib) \
    struct FExported##Name##Helper \
    { \
        FExported##Name##Helper() \
        { \
            UnLua::IExportedClass *Class = UnLua::FindExportedClass(#Name); \
            if (!Class) \
            { \
                ExportedClass = new UnLua::TExportedClassBase<bIsReflected>(#Name); \
                UnLua::ExportClass(ExportedClass);            /* ★ 缺省找不到时创建静态导出类 */ \
                Class = ExportedClass; \
            } \
            Class->AddLib(Lib);                               /* ★ 把 helper lib 接到导出类上 */ \
        } \
        UnLua::IExportedClass *ExportedClass; \
    };

#define BEGIN_EXPORT_CLASS_EX(bIsReflected, Name, Suffix, Type, SuperTypeName, ...) \
    struct FExported##Name##Suffix##Helper \
    { \
        typedef Type ClassType; \
        UnLua::TExportedClass<bIsReflected, Type, ##__VA_ARGS__> *ExportedClass; \
        FExported##Name##Suffix##Helper() \
        { \
            auto *Class = (UnLua::TExportedClass<bIsReflected, Type, ##__VA_ARGS__>*)UnLua::FindExportedClass(#Name); \
            if (!Class) \
            { \
                ExportedClass = new UnLua::TExportedClass<bIsReflected, Type, ##__VA_ARGS__>(#Name, SuperTypeName); \
                UnLua::ExportClass((UnLua::IExportedClass*)ExportedClass); /* ★ 注册导出类 */ \
                Class = ExportedClass; \
            }

#define ADD_LIB(Lib) \
            Class->AddLib(Lib);                               // ★ 导出类再挂一层 Lua 库
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/LowLevel.cpp
// 函数: CheckPropertyOwner
// 行号: 135-155
// 位置: 静态导出属性会绕过基于 UProperty owner 的反射校验
// ============================================================================
bool CheckPropertyOwner(lua_State* L, UnLua::ITypeOps* InProperty, void* InContainerPtr)
{
#if ENABLE_TYPE_CHECK == 1
    if (InProperty->StaticExported)
        return true;                                         // ★ 静态导出类型不走 owner class 校验

    UnLua::ITypeInterface* TypeInterface = (UnLua::ITypeInterface*)InProperty;
    FProperty* Property = TypeInterface->GetUProperty();
    ...
    if (Object->IsA(OwnerClass))
        return true;
```

关键源码 [2]：`UnLua` 的手写层主要沉到底层包装库；`Angelscript` 的手写层直接暴露在常规 API 表面

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/BaseLib/LuaLib_Object.cpp
// 函数: UObject / FSoftObjectPtr static export
// 行号: 281-329
// 位置: UObject 与软引用包装类型通过宏式静态导出注册，而不是等运行时反射即时生成
// ============================================================================
BEGIN_EXPORT_REFLECTED_CLASS(UObject)
    ADD_LIB(UObjectLib)                                      // ★ UObject 基础行为走静态库补底
END_EXPORT_CLASS()

BEGIN_EXPORT_CLASS(FSoftObjectPtr, const UObject*)
    ADD_CONST_FUNCTION_EX("IsValid", bool, IsValid)
    ADD_CONST_FUNCTION_EX("IsNull", bool, IsNull)
    ADD_CONST_FUNCTION_EX("IsPending", bool, IsPending)
    ADD_FUNCTION_EX("Reset", void, Reset)
    ADD_FUNCTION_EX("Set", void, operator=, const UObject*)
    ADD_CONST_FUNCTION_EX("Get", UObject*, Get)
    ADD_CONST_FUNCTION_EX("LoadSynchronous", UObject*, LoadSynchronous)
    ADD_LIB(FSoftObjectPtrLib)                               // ★ 指针包装类型也走导出底座
END_EXPORT_CLASS()
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp
// 函数: Bind_AActor_Base lambda
// 行号: 25-37
// 位置: Angelscript 的显式注册直接覆盖普通 gameplay API，而不只限于底层包装类型
// ============================================================================
auto AActor_ = FAngelscriptBinds::ExistingClass("AActor");

AActor_.Method("bool IsActorInitialized() const", METHOD_TRIVIAL(AActor, IsActorInitialized));
AActor_.Method("bool HasActorBegunPlay() const", METHOD_TRIVIAL(AActor, HasActorBegunPlay));
AActor_.Method("bool IsHidden() const", METHOD_TRIVIAL(AActor, IsHidden));
AActor_.Method("FVector GetActorLocation() const", METHOD_TRIVIAL(AActor, GetActorLocation));
AActor_.Method("FRotator GetActorRotation() const", METHOD_TRIVIAL(AActor, GetActorRotation));
AActor_.Method("UGameInstance GetGameInstance() const", METHODPR_TRIVIAL(UGameInstance*, AActor, GetGameInstance, () const));
// ★ 这里的手写层就是用户日常会直接消费的 API 表面
```

新增对比结论：

- `UnLua` 的“零胶水”应理解为**项目侧业务类型默认不写 per-class glue**，而不是“仓内完全没有手写绑定层”。
- `UnLua` 把手写导出层收缩到底层基础库与包装类型；`Angelscript` 则把显式绑定铺到更外层的 gameplay API 面。
- 因而真正差异不是“一个自动、一个手写”这么简单，而是“手写层藏在底座，还是显露在主 API 表面”。

差距判断：

- `UnLua` 相对“绝对纯反射、完全无静态导出层”的说法属于 `实现方式不同`，不是源码层面的事实
- `Angelscript` 相对 `UnLua` 的“底层集中静态导出 + 业务层反射主路径”属于 `实现方式不同`

### [维度 D3] 覆写链持久化模型：`UnLua` 只保留一个原始槽位备份，`Angelscript` 维护显式父类覆写链

前几轮已经说明 `UnLua` 是“改写既有 `UFunction` 槽位”，`Angelscript` 是“在类生成阶段校验 `BlueprintOverride`”。这一轮补的是**覆写链如何被保存**。`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaFunction.cpp:151-167` 清楚表明：如果目标函数已经被 `execScriptCallLua` 接管，新的 `ULuaFunction` 不会再复制一份新的父链，而是直接复用旧 `LuaFunction->GetOverridden()`；如果还没接管，才生成一次 `FunctionName__Overridden` 备份。随后 `Restore()` 也只是把原始 native thunk、flags 和 script 字节码写回去。这意味着 `UnLua` 在同一个 Blueprint event 槽位上保存的是**单个“原始实现基线”**，而不是一条逐层叠加的覆写链。

`Angelscript` 则是另一种模型。`Reference/UnLua/...` 里不存在类似 `GetBlueprintEventByScriptName()` 的 superclass 递归查找；而 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp:69-86` 明确按 `UClass -> SuperClass -> ...` 向上查找事件定义；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:732-786` 进一步要求 `BlueprintOverride` 必须真的命中父类事件、签名一致、editor-only 边界一致，否则直接编译报错。也就是说，`Angelscript` 保存的是**显式可验证的父类覆写关系**，不是“当前槽位改完能跑就行”。

```
[D3-Deep] Override Lineage Storage
UnLua
├─ first override -> duplicate Foo__Overridden          // 生成一次原始基线
├─ rebind/reload -> reuse existing Overridden           // 不继续叠新链
└─ restore -> put original native thunk back            // 恢复同一个槽位

Angelscript
├─ per-class event map                                  // 每个类单独记自己的 event
├─ GetBlueprintEventByScriptName() climbs supers        // 查找显式父链
└─ BlueprintOverride validated at compile time          // 继承关系先校验再生成
```

关键源码 [1]：`UnLua` 在同一槽位上只维护一个 `__Overridden` 基线

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaFunction.cpp
// 函数: ULuaFunction::Override / ULuaFunction::Restore
// 行号: 151-167, 181-188
// 位置: 已覆写的函数会复用旧备份，不会继续生成多层 override 链
// ============================================================================
if (Function->GetNativeFunc() == execScriptCallLua)
{
    // ★ 目标 UFunction 已经被 Lua 接管过，直接沿用旧的原始实现备份
    const auto LuaFunction = Get(Function);
    Overridden = LuaFunction->GetOverridden();
    check(Overridden);
}
else
{
    const auto DestName = FString::Printf(TEXT("%s__Overridden"), *Function->GetName());
    if (Function->HasAnyFunctionFlags(FUNC_Native))
        GetOuterUClass()->AddNativeFunction(*DestName, *Function->GetNativeFunc());
    Overridden = static_cast<UFunction*>(StaticDuplicateObject(Function, GetOuter(), *DestName));
    Overridden->ClearInternalFlags(EInternalObjectFlags::Native);
    Overridden->StaticLink(true);
    Overridden->SetNativeFunc(Function->GetNativeFunc());    // ★ 只保存一份原始 thunk
}

...

const auto Old = From.Get();
if (!Old)
    return;
Old->Script = Script;
Old->SetNativeFunc(Overridden->GetNativeFunc());
Old->GetOuterUClass()->AddNativeFunction(*Old->GetName(), Overridden->GetNativeFunc());
Old->FunctionFlags = Overridden->FunctionFlags;              // ★ Restore 也只是回到同一个原始基线
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaFunction.cpp
// 函数: ULuaFunction::SetActive
// 行号: 225-239
// 位置: 原始 UFunction 槽位被直接改写成 Lua thunk，而不是生成一条显式父链
// ============================================================================
SetSuperStruct(Function->GetSuperStruct());
Script = Function->Script;
Children = Function->Children;
ChildProperties = Function->ChildProperties;
PropertyLink = Function->PropertyLink;

Function->FunctionFlags |= FUNC_Native;
Function->SetNativeFunc(&execScriptCallLua);
Function->GetOuterUClass()->AddNativeFunction(*Function->GetName(), &execScriptCallLua);
Function->Script.Empty();
Function->Script.AddUninitialized(ScriptMagicHeaderSize + sizeof(ULuaFunction*));
// ★ 当前槽位只写入“这次由哪个 ULuaFunction 接管”的指针
```

关键源码 [2]：`Angelscript` 把父类覆写链做成显式可验证的继承关系

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp
// 函数: GetBlueprintEventByScriptName
// 行号: 69-86
// 位置: 事件查找会沿 UClass 继承链向上追溯
// ============================================================================
UFunction* GetBlueprintEventByScriptName(UClass* Class, const FString& ScriptName)
{
    UClass* CheckClass = Class;
    while(CheckClass != nullptr)
    {
        auto* List = GBlueprintEventsByScriptName.Find(CheckClass);
        if (List != nullptr)
        {
            auto** Function = List->Find(ScriptName);
            if (Function != nullptr)
            {
                return *Function;                             // ★ 找到后返回当前层或父层 event
            }
        }
        CheckClass = CheckClass->GetSuperClass();             // ★ 显式沿父类链追溯
    }
    return nullptr;
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 函数: BlueprintOverride validation
// 行号: 732-786
// 位置: 覆写关系先校验父类存在性、事件标记、签名一致性，再允许生成
// ============================================================================
if (FunctionDesc->bBlueprintOverride)
{
    FunctionDesc->OriginalFunctionName = FunctionDesc->FunctionName;
    auto* ParentFunction = GetBlueprintEventByScriptName(CodeSuperClass, FunctionDesc->FunctionName);
    if (ParentFunction != nullptr)
        FunctionDesc->FunctionName = ParentFunction->GetName();

    if (ParentFunction == nullptr)
    {
        ...
        FAngelscriptEngine::Get().ScriptCompileError(...,
            TEXT("BlueprintOverride method %s in class %s does not exist in superclass %s."));
        ClassData.ReloadReq = EReloadRequirement::Error;      // ★ 父链断了直接报错
    }
    else if (!SuperFunctionDesc->bBlueprintEvent && !SuperFunctionDesc->bBlueprintOverride)
    {
        ...
    }
    else if (!SuperFunctionDesc->SignatureMatches(FunctionDesc))
    {
        ...
    }
}
```

新增对比结论：

- `UnLua` 优化的是“把当前槽位换成 Lua 实现，并保留一个可恢复的原始基线”；它不把 repeated override 建模成多层 ancestry。
- `Angelscript` 优化的是“脚本类/Blueprint 类之间的显式覆写契约”；父类链能被查询、验证、报错。
- 因而这里的核心差异不是“能不能 override”，而是“override 被保存成一个可恢复槽位，还是一条可验证继承链”。

差距判断：

- `UnLua` 相对 `Angelscript` 这种“编译期校验父类覆写链”的能力，当前快照属于 `没有实现同层验证`
- 两者整体 Blueprint 覆写模型属于 `实现方式不同`

### [维度 D4] 热重载作用域补充：`UnLua` 一次 fan-out 到多个 `FLuaEnv`，`Angelscript` 每次只处理当前 engine 的模块事务

前几轮已经把文件事件、删除语义、full/soft reload 升级条件写清了。这一轮补的是**reload scope 到底落在哪个运行时单元上**。`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/LuaEnvLocator.h:23-50` 与 `.../Private/LuaEnvLocator.cpp:40-82` 显示，`UnLua` 的 `EnvLocator` 本身就是一层作用域抽象：默认 locator 只有一个 `Env`，`ULuaEnvLocator_ByGameInstance` 则额外维护 `TMap<UGameInstance, FLuaEnv>`。一旦 editor 或 Blueprint 入口触发热重载，`HotReload()` 会先打默认 `Env`，再遍历全部 `GameInstance` 对应的 Lua VM；每个 VM 最终只是执行 `DoString("UnLua.HotReload()")`。也就是说，**一次热重载请求会被 fan-out 到 locator 所管理的全部 Lua 环境**。

`Angelscript` 当前快照则把 reload state 放在 `FAngelscriptEngine` 实例里。`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:718-766` 说明所有 runtime API 都先解析“当前 engine”；`...:2729-2777` 的 `CheckForHotReload()` 只消费这个 engine 自己的 `FileChangesDetectedForReload / FileDeletionsDetectedForReload / QueuedFullReloadFiles`；`...:2253-2335` 再从该 engine 的 `ActiveModules` 构造依赖闭包。这不是“全局所有 script VM 一起 reload”，而是**当前 engine 对自己模块图做一笔编译事务**。因此，与其说 `UnLua` 是“module reload”、`Angelscript` 是“full reload”，不如更准确地说：**前者按 locator 广播到多个 Lua VM，后者按 engine/module graph 执行一次事务性重编译**。

```
[D4-Deep] Reload Scope Ownership
UnLua
├─ EnvLocatorClass selectable                          // scope 由 locator 决定
├─ Env (default)
├─ Envs[GameInstance A]
├─ Envs[GameInstance B]
└─ HotReload() -> every FLuaEnv runs UnLua.HotReload()

Angelscript
├─ resolve current engine from context/subsystem       // 先确定当前 engine
├─ engine-local queues for file changes                // queue 挂在 engine 上
└─ PerformHotReload() over engine.ActiveModules        // 对该 engine 模块图做事务
```

关键源码 [1]：`UnLua` 的热重载作用域由 `EnvLocator` 决定，并可一次广播到多个 Lua VM

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/LuaEnvLocator.h
// 函数: ULuaEnvLocator / ULuaEnvLocator_ByGameInstance
// 行号: 23-50
// 位置: 热重载 scope 不是写死的全局单例，而是由 locator 持有的 Env 集合
// ============================================================================
class UNLUA_API ULuaEnvLocator : public UObject
{
public:
    virtual UnLua::FLuaEnv* Locate(const UObject* Object);
    virtual void HotReload();
    virtual void Reset();

    TSharedPtr<UnLua::FLuaEnv, ESPMode::ThreadSafe> Env;     // ★ 默认 scope
};

class UNLUA_API ULuaEnvLocator_ByGameInstance : public ULuaEnvLocator
{
public:
    virtual UnLua::FLuaEnv* Locate(const UObject* Object) override;
    virtual void HotReload() override;
    virtual void Reset() override;

    TMap<TWeakObjectPtr<UGameInstance>, TSharedPtr<UnLua::FLuaEnv, ESPMode::ThreadSafe>> Envs; // ★ 多 GameInstance scope
};
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnvLocator.cpp
// 函数: ULuaEnvLocator_ByGameInstance::Locate / HotReload
// 行号: 40-82
// 位置: 一个 locator 可持有多个 FLuaEnv；HotReload 会逐个 fan-out
// ============================================================================
UnLua::FLuaEnv* ULuaEnvLocator_ByGameInstance::Locate(const UObject* Object)
{
    ...
    const auto Exists = Envs.Find(GameInstance);
    if (Exists)
        return (*Exists).Get();

    const TSharedPtr<UnLua::FLuaEnv, ESPMode::ThreadSafe> Ret = MakeShared<UnLua::FLuaEnv, ESPMode::ThreadSafe>();
    Ret->SetName(FString::Printf(TEXT("Env_%d"), Envs.Num() + 1));
    Ret->Start();
    Envs.Add(GameInstance, Ret);                             // ★ 不同 GameInstance 拥有不同 Lua VM
    return Ret.Get();
}

void ULuaEnvLocator_ByGameInstance::HotReload()
{
    if (Env)
        Env->HotReload();
    for (const auto& Pair : Envs)
        Pair.Value->HotReload();                             // ★ 同一次 reload 广播到全部现存 Env
}
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp
// 函数: FLuaEnv::HotReload
// 行号: 448-450
// 位置: 每个 Env 的热重载入口仍然只是执行同一段 Lua 侧算法
// ============================================================================
void FLuaEnv::HotReload()
{
    DoString("UnLua.HotReload()");                           // ★ scope 的差异在 Env 数量，不在 Lua 代码内容
}
```

关键源码 [2]：`Angelscript` 的热重载围绕“当前 engine 的模块图”展开，而不是一次广播多个 VM

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: FAngelscriptEngine::TryGetCurrentEngine / Get
// 行号: 718-766
// 位置: 所有热重载与编译入口都先解析当前 engine 上下文
// ============================================================================
FAngelscriptEngine* FAngelscriptEngine::TryGetCurrentEngine()
{
    if (FAngelscriptEngine* ScopedEngine = FAngelscriptEngineContextStack::Peek())
    {
        return ScopedEngine;                                 // ★ 先看显式 engine scope
    }

    if (UAngelscriptGameInstanceSubsystem* Subsystem = UAngelscriptGameInstanceSubsystem::GetCurrent())
    {
        if (FAngelscriptEngine* AttachedEngine = Subsystem->GetEngine())
        {
            return AttachedEngine;                           // ★ 否则回落到当前 GI 附着的 engine
        }
    }

    return nullptr;
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: FAngelscriptEngine::CheckForHotReload / PerformHotReload
// 行号: 2253-2335, 2729-2777
// 位置: reload 队列和依赖扩张都挂在当前 engine 的模块集合上
// ============================================================================
void FAngelscriptEngine::CheckForHotReload(ECompileType CompileType)
{
    ...
    TArray<FFilenamePair> FileList;
    FileList.Append(FileChangesDetectedForReload);
    FileChangesDetectedForReload.Empty();
    ...
    if (FileList.Num() != 0)
    {
        UE_LOG(Angelscript, Log, TEXT("Primary engine consuming %d queued script file change(s) for hot reload."), FileList.Num());
        PerformHotReload(CompileType, FileList);             // ★ 当前 engine 消费自己的 queue
    }
}

bool FAngelscriptEngine::PerformHotReload(ECompileType CompileType, const TArray<FFilenamePair>& InReloadFiles)
{
    ...
    TSet<FFilenamePair> FilesToHotReload;
    ...
    FilesToHotReload.Reserve(ActiveModules.Num() * 2);
    ...
    for (auto& Module : ActiveModules)
    {
        auto ModulePtr = &(Module.Value.Get());              // ★ 依赖扩张完全围绕当前 engine 的 ActiveModules
        for (const auto& Section : ModulePtr->Code)
            RelativeFileToModule.Add(Section.RelativeFilename, ModulePtr);
    }
```

新增对比结论：

- `UnLua` 的热重载 scope 可以是“当前 locator 管的全部 Lua VM”；这更像一条 fan-out 广播。
- `Angelscript` 的热重载 scope 是“当前 engine 的模块图事务”；这更像一次有边界的编译提交。
- 所以把两边简单说成“`require` 刷新 vs 全量重编译”仍然不够准，真正差异还包括**作用域是多 VM 广播，还是单 engine 事务**。

差距判断：

- 两者热重载作用域模型属于 `实现方式不同`
- `Angelscript` 相对 `UnLua` 这种“一个入口同时刷新 locator 下全部 Lua 环境”的 fan-out 机制，当前快照未见同层广播式实现，可归为 `没有实现同层机制`

---

## 深化分析 (2026-04-08 19:58:19)

### [维度 D5] 调试启动责任的新增发现：`UnLua` 把 attach 责任交给用户脚本，`Angelscript` 在 engine 启动期直接拉起 debug server

前几轮已经把协议归属、值展开和会话层边界拆开了。这一轮补的是**“调试会话到底由谁启动”**。`UnLua` 当前快照里，调试文档明确要求用户自己安装 `LuaPanda / LuaHelper`、手工放入 `LuaPanda.lua`，再在 Lua 脚本里显式执行 `require("LuaPanda").start("127.0.0.1",8818)`。与此同时，`bEnableDebug` 只是在 `UnLua.Build.cs` 里展开成 `UNLUA_ENABLE_DEBUG` 编译宏，源码里主要用它包住额外日志和调试例程；`LuaEnv.cpp` 中唯一看到的 `lua_sethook` 还挂在 `ENABLE_UNREAL_INSIGHTS` 下，用于 profiling，而不是远程调试握手。也就是说，**UnLua 的 runtime 可以提供调试辅助数据，但“什么时候开始会话、怎么连上 IDE”并不由插件自己接管**。

`Angelscript` 当前快照则相反。`FAngelscriptEngine` 在 engine 启动阶段，满足条件就直接 new `FAngelscriptDebugServer`；server 自己监听 TCP 端口、接受连接，并在收到 `StartDebugging` 后回送 `DebugServerVersion`。测试侧也不是去脚本里插一段 `start()`，而是拿一个现成的 production engine，直接连到已启动的 server。这里的关键差异不是“有没有 debug helper”，而是**会话 bootstrap 是用户脚本负责，还是 engine 生命周期负责**。

```
[D5-Deep] Debugger Bootstrap Ownership
UnLua
├─ Docs/CN/Debugging.md                              // 文档先要求用户准备外部调试器
├─ copy LuaPanda.lua into Content/Script            // 外部 runtime 脚本手工放入工程
├─ script executes require("LuaPanda").start(...)   // 会话由用户脚本显式启动
├─ UNLUA_ENABLE_DEBUG adds helper/log code          // 编译宏只打开辅助代码
└─ attach lifecycle lives outside UnLua             // 会话生命周期不由插件托管

Angelscript
├─ FAngelscriptEngine startup                       // engine 创建期介入
├─ new FAngelscriptDebugServer(port)                // 直接拉起监听 server
├─ listener accepts TCP client                      // server 自己接连接
├─ client sends StartDebugging                      // 协议内建握手
└─ server replies DebugServerVersion                // 会话进入 debugging 模式
```

关键源码 [1]：`UnLua` 的调试文档要求用户在脚本里手动启动外部调试器

```md
<!-- ============================================================================
文件: Reference/UnLua/Docs/CN/Debugging.md
行号: 5-14
位置: 调试入口写在文档里，且启动动作发生在用户 Lua 代码中
============================================================================ -->
1. 从VSCode应用市场安装 LuaPanda / LuaHelper
2. 从 LuaPanda 官方仓库获取 `LuaPanda.lua`，放入 `{UE工程}/Content/Script`
3. 在Lua代码中加入 `require("LuaPanda").start("127.0.0.1",8818)`  <!-- ★ attach 由用户脚本显式触发 -->

注：调试器依赖 `luasocket`，UnLua 已通过扩展插件集成。             <!-- ★ 会话依赖外部调试器生态 -->
```

关键源码 [2]：`bEnableDebug` 在 `UnLua` 里首先是编译宏，不是 debug server 启动器

```csharp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/UnLua.Build.cs
// 函数: configure definitions
// 行号: 91-92
// 位置: Editor 设置里的 bEnableDebug 被翻译成编译期开关
// ============================================================================
loadBoolConfig("bAutoStartup", "AUTO_UNLUA_STARTUP", true);
loadBoolConfig("bEnableDebug", "UNLUA_ENABLE_DEBUG", false);   // ★ 打开的是编译宏，不是网络监听
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaCore.cpp
// 函数: PushObjectCore
// 行号: 564-566
// 位置: debug 宏打开后主要增加诊断日志
// ============================================================================
#if UNLUA_ENABLE_DEBUG != 0
    UE_LOG(LogUnLua, Log, TEXT("%s : %p,%s,%s"),
        ANSI_TO_TCHAR(__FUNCTION__), Object, *Object->GetName(), *MetatableName);   // ★ 这里是日志，而不是 attach
#endif
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp
// 函数: FLuaEnv constructor path
// 行号: 166-170
// 位置: 当前仓内的 lua_sethook 绑定到 Unreal Insights profiling，而非远程调试会话
// ============================================================================
#if ENABLE_UNREAL_INSIGHTS && CPUPROFILERTRACE_ENABLED
    if (FDeadLoopCheck::Timeout)
        UE_LOG(LogUnLua, Warning, TEXT("Profiling will not working when DeadLoopCheck enabled."))
    else
        lua_sethook(L, Hook, LUA_MASKCALL | LUA_MASKRET, 0);   // ★ 这是 profiling hook，不是 LuaPanda 握手
#endif
```

关键源码 [3]：`Angelscript` 在 engine 创建期直接拉起 server，并通过协议握手进入调试态

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: FAngelscriptEngine startup
// 行号: 1450-1455
// 位置: debug server 生命周期属于 engine，而不是脚本本身
// ============================================================================
/*
Start the debug server that external tools can connect to.
*/
#if WITH_AS_DEBUGSERVER
if ((!bUsePrecompiledData || bScriptDevelopmentMode) && FApp::HasProjectName())
{
    DebugServer = new FAngelscriptDebugServer(this, RuntimeConfig.DebugServerPort);  // ★ engine 直接托管 server
}
#endif
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 函数: FAngelscriptDebugServer ctor / HandleMessage
// 行号: 402-408, 897-907
// 位置: server 自己监听连接，并在 StartDebugging 后返回版本握手
// ============================================================================
FAngelscriptDebugServer::FAngelscriptDebugServer(FAngelscriptEngine* InOwnerEngine, int Port)
{
    OwnerEngine = InOwnerEngine;
    Listener = new FTcpListener(FIPv4Endpoint(FIPv4Address::Any, Port));
    Listener->OnConnectionAccepted().BindRaw(this, &FAngelscriptDebugServer::HandleConnectionAccepted);
    UE_LOG(Angelscript, Log, TEXT("Angelscript debug server listening on %s"), *Listener->GetLocalEndpoint().ToText().ToString());
}

...

else if (MessageType == EDebugMessageType::StartDebugging)
{
    FStartDebuggingMessage Msg;
    *Datagram << Msg;

    bIsDebugging = true;                                          // ★ server 自己切到 debugging state
    AngelscriptDebugServer::DebugAdapterVersion = Msg.DebugAdapterVersion;

    FDebugServerVersionMessage DebugServerVersionMessage;
    DebugServerVersionMessage.DebugServerVersion = DEBUG_SERVER_VERSION;
    SendMessageToClient(Client, EDebugMessageType::DebugServerVersion, DebugServerVersionMessage);  // ★ 内建版本握手
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp
// 函数: StartSteppingDebuggerSession
// 行号: 17-32
// 位置: 测试直接连接已经可调试的 production engine，而不是修改脚本去 start()
// ============================================================================
FAngelscriptDebuggerSessionConfig SessionConfig;
SessionConfig.ExistingEngine = TryGetRunningProductionDebuggerEngine();               // ★ 先取现成 engine
...
if (!Test.TestTrue(TEXT("Debugger stepping should initialize the debugger session"), Session.Initialize(SessionConfig)))
{
    return false;
}

if (!Test.TestTrue(TEXT("Debugger stepping should connect the test client"), Client.Connect(TEXT("127.0.0.1"), Session.GetPort())))
{
    return false;                                                                      // ★ 客户端直接连 server 端口
}
```

新增对比结论：

- `UnLua` 当前快照把调试 bootstrap 留在“文档 + 用户脚本”层，插件自身主要提供调试辅助数据与诊断代码。
- `Angelscript` 把调试 bootstrap 提前到 engine 生命周期，server、端口监听、版本握手都在仓内实现。
- 因而这里真正的差异不是“能不能调试”，而是“调试会话由脚本层手工拉起，还是由 runtime 自己托管”。

差距判断：

- `UnLua` 相对 `Angelscript DebugServer V2` 这种 `engine-owned` 的内建启动链，当前快照属于 `没有实现同层自动启动机制`
- 两者整体调试接入模型属于 `实现方式不同`

### [维度 D9] 回归用例载体的新发现：`UnLua` 用 issue + map + Lua module 三件套复现现场，`Angelscript` 的大量 fixture 直接从内存编译脚本

前几轮主要写了 runner、suite 和热重载挂点。这一轮补的是**“一个 regression case 到底装在哪些载体里”**。`UnLua` 当前快照里的不少 regression case 都是按 issue 号建目录，并把 C++ test、`.umap/.uasset`、Lua module 拆成三块：C++ 测试先打开 `"/UnLuaTestSuite/Tests/Regression/Issue603/Issue603"` 这类 map，再让 Blueprint / UObject 通过 `GetModuleName()` 落到 `Tests.Regression.IssueOverrides.IssueOverridesObject` 这样的脚本模块。也就是说，**回归样例首先是一个真实资产场景，然后才是脚本断言**。这样做的价值是能够连 Blueprint 编译、资产路径、模块绑定一起回放；代价是单个 case 更重，更依赖编辑器资产存在。

`Angelscript` 在我这轮补读到的调试 fixture / script example 里，主流做法则是把脚本文本直接写在 C++ 字符串里，利用 `CompileAnnotatedModuleFromMemory()` 或 `CompileModuleFromMemory()` 即时生成模块；断点行号、eval path 也在 fixture 里动态生成。也就是说，**不少测试单元先是“代码字符串 + helper”，其次才是磁盘脚本/资产**。这种方式更轻，更利于 headless automation，但如果要复现 Blueprint 资产序列化、地图切换、副本状态这类问题，还得额外上 scenario/asset 级测试。

```
[D9-Deep] Regression Artifact Carrier
UnLua
├─ IssueNNNTest.cpp                                  // C++ harness 以 issue 号归档
├─ open /UnLuaTestSuite/Tests/Regression/...         // 先打开真实 map
├─ BP/UObject assets under Content/Tests/...         // 资产负责触发真实绑定链
└─ Lua module under Content/Script/Tests/...         // 脚本模块与 issue 目录平行组织

Angelscript
├─ *_Test.cpp keeps script text in FString literal   // 示例与 fixture 常直接写在测试源码里
├─ fixture parses markers/eval paths                 // 行号、求值路径运行时生成
├─ CompileAnnotatedModuleFromMemory(...)             // 不依赖先落盘脚本
└─ discard module after assertions                   // case 生命周期更轻量
```

关键源码 [1]：`UnLua` 的 regression case 明确先打开 issue 对应的地图，再执行断言

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Tests/Issue603Test.cpp
// 函数: FIssue603Test::RunTest
// 行号: 22-30
// 位置: issue 回归首先是 map 复放，不是纯函数级断言
// ============================================================================
BEGIN_TESTSUITE(FIssue603Test, TEXT("UnLua.Regression.Issue603 在Lua监听的事件中再次触发事件会导致崩溃"))

bool FIssue603Test::RunTest(const FString& Parameters)
{
    const auto MapName = TEXT("/UnLuaTestSuite/Tests/Regression/Issue603/Issue603"); // ★ 直接绑定到 issue 地图
    ADD_LATENT_AUTOMATION_COMMAND(FOpenMapLatentCommand(MapName))
    ADD_LATENT_AUTOMATION_COMMAND(FEngineWaitLatentCommand(1.0));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}
```

关键源码 [2]：`UnLua` 的 issue case 还会把 UObject 绑定到同 issue 目录下的 Lua module

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Tests/IssueOverridesTest.cpp
// 函数: FIssueOverridesTest::RunTest
// 行号: 27-54
// 位置: C++ test 打开地图后，再把测试对象和 Lua 全局拼到一起验证覆写行为
// ============================================================================
bool FIssueOverridesTest::RunTest(const FString& Parameters)
{
    const auto MapName = TEXT("/UnLuaTestSuite/Tests/Regression/IssueOverrides/IssueOverrides.IssueOverrides");
    ADD_LATENT_AUTOMATION_COMMAND(FOpenMapLatentCommand(MapName))                      // ★ 资产场景先启动
    ...
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this] {
        const auto Obj = NewObject<UIssueOverridesObject>();
        Obj->AddToRoot();

        UnLua::PushUObject(L, Obj);
        lua_setglobal(L, "G_IssueObject");

        UnLua::RunChunk(L, "return G_IssueObject:CollectInfo()");
        ...
        return true;
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Tests/IssueOverridesTest.h
// 函数: UIssueOverridesObject::GetModuleName_Implementation
// 行号: 34-37
// 位置: UObject 绑定的 Lua module 名称继续沿用 issue 目录结构
// ============================================================================
virtual FString GetModuleName_Implementation() const override
{
    return TEXT("Tests.Regression.IssueOverrides.IssueOverridesObject");               // ★ module path 直接映射到回归目录
}
```

关键源码 [3]：`Angelscript` 的示例 / 调试 fixture 更偏向“内存脚本 + helper”模型

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleTestSupport.cpp
// 函数: AngelscriptScriptExamples::RunScriptExampleCompileTest
// 行号: 28-58
// 位置: 示例测试把脚本文本直接拼成模块，再从内存编译
// ============================================================================
const FString ExampleFileName = Example.ExampleFileName;
const FString ModuleNameString = FPaths::GetBaseFilename(ExampleFileName);
...
FString CombinedScriptCode;
if (Example.DependencyScriptText != nullptr)
{
    CombinedScriptCode += Example.DependencyScriptText;
    CombinedScriptCode += TEXT("\n\n");
}

CombinedScriptCode += Example.ScriptText;

const FString VirtualFileName = FString::Printf(TEXT("ScriptExamples/%s"), *ExampleFileName);
const bool bCompiled = CompileAnnotatedModuleFromMemory(&Engine, ModuleName, VirtualFileName, CombinedScriptCode); // ★ 不要求先准备 map / asset
Test.TestTrue(*FString::Printf(TEXT("Compile example '%s' succeeds"), *ExampleFileName), bCompiled);
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerScriptFixture.cpp
// 函数: MakeFixture / FAngelscriptDebuggerScriptFixture::Compile
// 行号: 19-56, 214-221
// 位置: 调试 fixture 在内存中解析 marker、生成 eval path，再即时编译模块
// ============================================================================
RawScriptSource.ParseIntoArrayLines(Lines, false);
...
LineMarkers.Add(FName(*MarkerName), LineIndex + 1);                                     // ★ 行号 marker 直接从字符串提取
...
Fixture.ScriptSource = FString::Join(Lines, TEXT("\n"));
Fixture.LineMarkers = MoveTemp(LineMarkers);
Fixture.EvalPaths = MoveTemp(EvalPaths);

...

if (bUseAnnotatedCompilation)
{
    return CompileAnnotatedModuleFromMemory(&Engine, ModuleName, Filename, ScriptSource); // ★ fixture 直接内存编译
}
return CompileModuleFromMemory(&Engine, ModuleName, Filename, ScriptSource);
```

新增对比结论：

- `UnLua` 的很多 regression case 是“issue 编号驱动的真实场景复放”，核心载体是 map、asset 和 Lua module 的组合。
- `Angelscript` 这轮补读到的 example/debugger fixture 则明显偏“代码字符串 + helper + in-memory compile”。
- 前者更擅长保留真实资产上下文，后者更擅长把 case 压缩成轻量、可组合、可 headless 的最小复现单元。

差距判断：

- 两者回归用例载体模型属于 `实现方式不同`
- 就本轮已读源码而言，`UnLua` 相对 `Angelscript` 这种大量 `in-memory fixture` 的轻量化做法，`没有实现同层普适机制`
- 补充校验：当前仓库快照中未见可直接引用的仓内 CI workflow 定义，因此本轮只能确认测试载体与组织方式，不能据此断言其外部 CI 质量差异

### [维度 D10] 文档工具链的新发现：`UnLua` 的教程步骤直接落到工具栏与 commandlet，`Angelscript` 的文档工具更偏 API 参考导出

前几轮已经说明 `UnLua` 的 README、脚本路径、教程目录是互相索引的。这一轮补的是**“文档里的动作有没有仓内工具直接承接”**。`UnLua` 的新手文档不是停在“你可以这么做”，而是明确写“点击 UnLua 菜单栏中的绑定 / 生成 Lua 模板 / 导出智能提示”；对应源码里确实存在 `Bind`、`CreateLuaTemplate`、`GenerateIntelliSense` 等 editor command，菜单也把这些动作直接暴露出来。更进一步，`FUnLuaIntelliSenseGenerator` 会监听 `AssetRegistry` 的 add/remove/rename/update，把导出的 `.lua` stub 写到 `Plugins/UnLua/Intermediate/IntelliSense`；`UUnLuaIntelliSenseCommandlet` 则提供了批量生成入口。也就是说，**文档步骤和插件工具链是一一对齐的**。

`Angelscript` 当前快照里的文档工具则更像“reference exporter”。`FAngelscriptDocs::DumpDocumentation()` 先遍历 script engine、收集 type/property/function 元数据，再把内容写成 `Docs/angelscript/generated/*.hpp`。这种做法对 API 参考、调试数据库和补全文档很有价值，但它面向的是**把已有绑定导出成参考视图**，而不是像 UnLua 那样，把 onboarding 本身做成 editor 动作。因此，本轮更准确的对比不是“谁文档更多”，而是**一个把文档写成用户操作流，另一个把文档写成生成型 reference**。

```
[D10-Deep] Documentation Toolchain Ownership
UnLua
├─ Quickstart / IntelliSense docs                   // 文档直接描述用户点击哪些菜单
├─ editor commands: Bind / CreateLuaTemplate        // 菜单动作和文档措辞一致
├─ GenerateIntelliSense in main menu                // 导出动作直接暴露在 UI
├─ AssetRegistry-driven generator                   // 生成结果随资产变化更新
└─ commandlet writes Plugin/UnLua/Intermediate/...  // 还能批量生成

Angelscript
├─ traverse asIScriptEngine + UObject metadata      // 先收集元数据
├─ build FDocClass / FDocProperty tables            // 组织成中间文档模型
└─ dump Docs/angelscript/generated/*.hpp            // 输出为 reference snapshot
```

关键源码 [1]：`UnLua` 的教程明确要求用户点击工具栏动作

```md
<!-- ============================================================================
文件: Reference/UnLua/Docs/CN/Quickstart_For_UE_Newbie.md
行号: 11-20
位置: 新手文档把“绑定”和“生成模板”直接描述成菜单动作
============================================================================ -->
## 2. 绑定到Lua
- 双击打开上一步新建好的蓝图，点击 UnLua 菜单栏中的“绑定”                     <!-- ★ 文档直接对应 editor action -->
- 如果下次需要修改绑定的路径，可以找到 `GetModuleName` 函数并双击进行修改

## 3. 生成Lua模版代码
点击 UnLua 菜单栏中的“生成Lua模板文件”，会在工程 `Content/Script` 目录下生成。  <!-- ★ 文档直接承诺了输出位置 -->
```

```md
<!-- ============================================================================
文件: Reference/UnLua/Docs/CN/IntelliSense.md
行号: 1-17
位置: IntelliSense 文档同样以“点击导出”作为入口，而不是让用户自己拼生成脚本
============================================================================ -->
## 1. 生成智能提示信息
打开 UnLua 工具栏，点击导出智能提示，会在 `{UE工程}/Plugins/UnLua/Intermediate` 下生成 `IntelliSense` 目录。  <!-- ★ 文档步骤就是插件功能入口 -->

## 2. 加入到LuaIDE
参考示例工程的 `TPSProject.code-workspace`，工作区中分别添加了 `Script` 和 `IntelliSense`。                        <!-- ★ 文档继续承接输出物如何消费 -->
```

关键源码 [2]：`UnLua` 的 editor command、菜单和生成器真正把这些文档动作落成了工具

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorCommands.cpp
// 函数: FUnLuaEditorCommands::RegisterCommands
// 行号: 21-30
// 位置: 文档里提到的操作在 editor command 层都有明确命令 ID
// ============================================================================
UI_COMMAND(CreateLuaTemplate, "Create Lua Template", "Create lua template file", EUserInterfaceActionType::Button, FInputChord());
UI_COMMAND(BindToLua, "Bind", "Implement UnLuaInterface", EUserInterfaceActionType::Button, FInputChord());
...
UI_COMMAND(GenerateIntelliSense, "Generate IntelliSense", "Generate intelliSense files", EUserInterfaceActionType::Button, FInputChord()); // ★ 文档动作对应真实命令
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp
// 函数: FMainMenuToolbar::GenerateUnLuaSettingsMenu
// 行号: 118-126
// 位置: 这些命令被挂到主菜单，用户按文档操作就能真正触发
// ============================================================================
MenuBuilder.BeginSection(NAME_None, LOCTEXT("Section_Action", "Action"));
MenuBuilder.AddMenuEntry(Commands.HotReload, NAME_None, LOCTEXT("HotReload", "Hot Reload"));
MenuBuilder.AddMenuEntry(Commands.GenerateIntelliSense, NAME_None, LOCTEXT("GenerateIntelliSense", "Generate IntelliSense")); // ★ 菜单入口与文档措辞一致
MenuBuilder.EndSection();
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp
// 函数: FUnLuaEditorToolbar::BindCommands / CreateLuaTemplate_Executed
// 行号: 37-44, 257-310
// 位置: “生成 Lua 模板”不是说明文，而是能直接执行的编辑器动作
// ============================================================================
CommandList->MapAction(Commands.CreateLuaTemplate, FExecuteAction::CreateRaw(this, &FUnLuaEditorToolbar::CreateLuaTemplate_Executed));
CommandList->MapAction(Commands.BindToLua, FExecuteAction::CreateRaw(this, &FUnLuaEditorToolbar::BindToLua_Executed));
...
const auto FileName = FString::Printf(TEXT("%s%s.lua"), *GLuaSrcFullPath, *RelativePath);
...
FFileHelper::LoadFileToString(Content, *FullFilePath);
Content = Content.Replace(TEXT("TemplateName"), *TemplateName)
                 .Replace(TEXT("ClassName"), *UnLua::IntelliSense::GetTypeName(Class));
FFileHelper::SaveStringToFile(Content, *FileName, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);  // ★ 模板文件真的被生成到项目脚本目录
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp
// 函数: Initialize / SaveFile
// 行号: 42-55, 222-233
// 位置: IntelliSense 不是一次性导出，而是被做成持续更新的生成器
// ============================================================================
void FUnLuaIntelliSenseGenerator::Initialize()
{
    ...
    OutputDir = IPluginManager::Get().FindPlugin("UnLua")->GetBaseDir() + "/Intermediate/IntelliSense";

    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    AssetRegistryModule.Get().OnAssetAdded().AddRaw(this, &FUnLuaIntelliSenseGenerator::OnAssetAdded);
    AssetRegistryModule.Get().OnAssetRemoved().AddRaw(this, &FUnLuaIntelliSenseGenerator::OnAssetRemoved);
    AssetRegistryModule.Get().OnAssetRenamed().AddRaw(this, &FUnLuaIntelliSenseGenerator::OnAssetRenamed);
    AssetRegistryModule.Get().OnAssetUpdated().AddRaw(this, &FUnLuaIntelliSenseGenerator::OnAssetUpdated); // ★ 文档输出物会跟资产变化同步
}

...

const FString FilePath = FString::Printf(TEXT("%s/%s.lua"), *Directory, *FileName);
...
if (FileContent != GeneratedFileContent)
    FFileHelper::SaveStringToFile(GeneratedFileContent, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM); // ★ 真正写盘
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp
// 函数: UUnLuaIntelliSenseCommandlet ctor / Main
// 行号: 21-29, 55-56
// 位置: 除了菜单入口，还提供批量生成的 commandlet 通道
// ============================================================================
UUnLuaIntelliSenseCommandlet::UUnLuaIntelliSenseCommandlet(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    IntermediateDir = FPaths::ProjectPluginsDir();
    IntermediateDir += TEXT("UnLua/Intermediate/");
}

int32 UUnLuaIntelliSenseCommandlet::Main(const FString &Params)
{
    ...
    const auto ExportedReflectedClasses = UnLua::GetExportedReflectedClasses();        // ★ commandlet 走的是同一套导出模型
```

关键源码 [3]：`Angelscript` 的文档工具当前更像 reference exporter

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp
// 函数: FAngelscriptDocs::DumpDocumentation
// 行号: 407-430, 517-563, 682-709
// 位置: 文档生成先扫描 script engine，再写出 header-like 参考文件
// ============================================================================
void FAngelscriptDocs::DumpDocumentation(asIScriptEngine* Engine)
{
#if WITH_EDITOR
    TMap<FString, FDocClass> Classes;
    auto* ScriptEngine = FAngelscriptEngine::Get().Engine;                               // ★ 文档来源是当前 engine 元数据
    ...
    for (int32 TypeIndex = 0; TypeIndex < TypeCount; ++TypeIndex)
    {
        auto* ScriptType = ScriptEngine->GetObjectTypeByIndex(TypeIndex);
        ...
        if (Class != nullptr)
        {
            ClassDoc.Documentation = Class->GetMetaData("ToolTip");
            ...
        }
        ...
        Prop.Documentation = PropDesc->GetMetaData("ToolTip");                           // ★ 把 UE 元数据回收进文档模型
    }
    ...
    FString Filename = FPaths::ProjectDir() / TEXT("/Docs/angelscript/generated") / ClassDoc.ClassName + TEXT(".hpp");
    ...
    Content += FString::Printf(TEXT("/* Class: %s \n %s */ \n class %s"),
        *ClassDoc.ClassName, *ClassDoc.Documentation, *ClassDoc.ClassName);             // ★ 输出形态是 generated reference file
```

新增对比结论：

- `UnLua` 的文档在当前快照里是一条完整的“文档步骤 -> editor command -> 生成结果”闭环。
- `Angelscript` 的文档工具在当前快照里更偏“扫描绑定元数据 -> 导出 reference snapshot”。
- 两边都在做文档化，但一个优化的是 onboarding 操作流，另一个优化的是 API/reference 可导出性。

差距判断：

- 两者文档工具链属于 `实现方式不同`
- 就当前已读源码而言，`Angelscript` 相对 `UnLua` 这种“文档步骤直接映射 editor action / commandlet”的交付方式，`没有实现同层用户引导入口`

---

## 深化分析 (2026-04-08 20:11:00)

本轮只补前文还没展开透的三个点：`UFunction` 语义补偿、调试变量模型、测试分层粒度。

### [维度 D3] `UFunction` 语义补偿：`UnLua` 把 `RPC/latent` 细节吞进反射桥，`Angelscript` 把网络/Blueprint 约束前移到声明期

前文已经讲过 `UnLuaInterface` 和 `BlueprintOverride` 的大方向差异，这一轮补的是**脚本作者在函数层到底要自己处理多少 UE 语义细节**。`UnLua` 在 `FFunctionDesc` 里直接吸收了两类 UE 约定：一类是网络函数命名，`FUNC_Net` 会把脚本可见名改成 `Func_RPC`；另一类是 latent 调用，如果脚本没有显式传 `FLatentActionInfo`，桥接层会自动生成 `OnLatentActionCompleted` 回调并把 `Env.GetManager()` 填进 `CallbackTarget`。这意味着 `UnLua` 的“零胶水”不只是少写 `Bind_*.cpp`，还把一部分 **UE 运行时调用礼仪** 一起藏进了桥接层。

`Angelscript` 当前快照的 ownership 则完全不同。`NetFunction/Server/Client/NetMulticast` 和 `BlueprintOverride` 的组合关系在预处理阶段就被校验；如果需要 Blueprint/网络包装器，`GenerateBlueprintEventWrapper()` 会直接生成 wrapper，并把脚本实现名后缀成 `_Implementation`。到了类生成阶段，这些布尔描述再被翻译成 `FUNC_Net* / FUNC_BlueprintEvent` 等真实 `UFunction` flags。换句话说，`Angelscript` 不是在调用时帮脚本“猜”该怎么补 UE 语义，而是在**声明期强制作者把语义讲清楚**。

```
[D3-Deep] UFunction Semantic Ownership
UnLua
├─ UFunction enters FFunctionDesc                    // 运行时读反射签名
├─ FUNC_Net -> expose name as Func_RPC              // 吞掉 UE 网络命名细节
├─ LatentInfo missing -> synthesize callback        // 自动补 FLatentActionInfo
└─ Script calls reflected function directly         // 作者面对的是简化后的 Lua 入口

Angelscript
├─ Preprocessor reads UFUNCTION specifiers          // 声明期先做语义分类
├─ reject BlueprintOverride + Net* invalid mixes    // 组合非法直接报错
├─ GenerateBlueprintEventWrapper + _Implementation  // 先产出 wrapper
└─ ClassGenerator writes FUNC_Net* / BlueprintEvent // 再固化成 UE 反射对象
```

关键源码 [1]：`UnLua` 在函数描述阶段直接补网络名和 latent 回调

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp
// 函数: FFunctionDesc::FFunctionDesc / FFunctionDesc::PreCall
// 行号: 41-74, 286-302
// 位置: 运行时函数桥不仅枚举参数，还主动吸收 UE 的 RPC / latent 约定
// ============================================================================
if (InFunction->HasAnyFunctionFlags(FUNC_Net))
    LuaFunctionName = MakeUnique<FTCHARToUTF8>(*FString::Printf(TEXT("%s_RPC"), *FuncName)); // ★ 网络函数对 Lua 暴露为 *_RPC
else
    LuaFunctionName = MakeUnique<FTCHARToUTF8>(*FuncName);

static const FName NAME_LatentInfo = TEXT("LatentInfo");
for (TFieldIterator<FProperty> It(InFunction); It && (It->PropertyFlags & CPF_Parm); ++It)
{
    FProperty *Property = *It;
    FPropertyDesc* PropertyDesc = FPropertyDesc::Create(Property);
    int32 Index = Properties.Add(TUniquePtr<FPropertyDesc>(PropertyDesc));
    if (PropertyDesc->IsReturnParameter())
    {
        ReturnPropertyIndex = Index;
    }
    else if (LatentPropertyIndex == INDEX_NONE && Property->GetFName() == NAME_LatentInfo)
    {
        LatentPropertyIndex = Index; // ★ 先记住 latent 参数位置
    }
}

...

if (i == LatentPropertyIndex)
{
    const int32 ThreadRef = *((int32*)Userdata);
    if(lua_type(L, FirstParamIndex + ParamIndex) == LUA_TUSERDATA)
    {
        FLatentActionInfo Info = UnLua::Get<FLatentActionInfo>(L, FirstParamIndex + ParamIndex, UnLua::TType<FLatentActionInfo>());
        Property->CopyValue(ContainerPtr, &Info);              // ★ 脚本显式传了就直接用
        continue;
    }

    auto& Env = UnLua::FLuaEnv::FindEnvChecked(L);
    FLatentActionInfo LatentActionInfo(
        ThreadRef,
        GetTypeHash(FGuid::NewGuid()),
        TEXT("OnLatentActionCompleted"),
        (Env.GetManager()));                                   // ★ 没传则自动补 UE latent 回调
    Property->CopyValue(ContainerPtr, &LatentActionInfo);
    continue;
}
```

关键源码 [2]：`Angelscript` 要求在声明期把 `Net* / BlueprintOverride` 关系说清楚，再落成 `UFunction` flags

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp
// 函数: FAngelscriptPreprocessor::PreprocessFunctionSpecifiers
// 行号: 1541-1568, 1578-1624, 1654-1680
// 位置: 网络 / Blueprint 组合在预处理阶段就被校验和改写
// ============================================================================
if (FunctionDesc->bBlueprintOverride)
{
    MacroError(File, Macro, FString::Printf(TEXT("UFUNCTION() %s cannot be both NetFunction and BlueprintOverride"), *FunctionDesc->FunctionName));
    bHasError = true;
    continue;                                                  // ★ 非法组合直接拒绝
}

FunctionDesc->bBlueprintEvent = true;
FunctionDesc->bNetFunction = true;
...
GenerateBlueprintEventWrapper(File, Chunk, Macro, FunctionDesc);
FunctionDesc->ScriptFunctionName += TEXT("_Implementation");   // ★ wrapper 和脚本实现名在声明期定型

...

if (FunctionDesc->bBlueprintOverride)
{
    MacroError(File, Macro, FString::Printf(TEXT("UFUNCTION() %s cannot both be BlueprintOverride and have network specifiers"), *FunctionDesc->FunctionName));
    bHasError = true;
    continue;
}

FunctionDesc->bBlueprintEvent = true;
FunctionDesc->bNetMulticast = Spec.Name == PP_NAME_NetMulticast;
FunctionDesc->bNetClient = Spec.Name == PP_NAME_NetClient;
FunctionDesc->bNetServer = Spec.Name == PP_NAME_NetServer;
...
FunctionDesc->ScriptFunctionName += TEXT("_Implementation");

...

FunctionDesc->bBlueprintEvent = true;
FunctionDesc->bBlueprintOverride = true;                       // ★ override 走继承来的 wrapper，不再运行时猜测
FunctionDesc->ScriptFunctionName += TEXT("_Implementation");
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FLatentActionInfo.cpp
// 函数: Bind_FLatentActionInfo lambda
// 行号: 7-24
// 位置: latent 调用所需结构体由绑定显式暴露，脚本作者自己构造
// ============================================================================
auto FLatentActionInfo_ = FAngelscriptBinds::ExistingClass("FLatentActionInfo");

FLatentActionInfo_.Constructor(
    "void f(int32 InLinkage, int32 InUUID, const FName InFunctionName, UObject InCallbackTarget)",
    [](FLatentActionInfo* Address, int32 InLinkage, int32 InUUID, const FName InFunctionName, UObject* InCallbackTarget)
{
    new(Address) FLatentActionInfo();
    Address->Linkage = InLinkage;
    Address->UUID = InUUID;
    Address->ExecutionFunction = InFunctionName;
    Address->CallbackTarget = InCallbackTarget;                // ★ latent 信息是显式 API，不是自动注入
});
```

新增对比结论：

- `UnLua` 的零胶水在当前快照里已经延伸到 `RPC` 命名和 latent callback 生成，作者少碰一层 UE 细节。
- `Angelscript` 的强项不是“更自动”，而是**更早暴露约束**；网络、Blueprint 和 override 的冲突在编译前就失败，不等到运行时反射桥再补救。
- 两者不是简单的“一个高级一个低级”，而是“运行时补偿”对 “声明期契约”。

差距判断：

- `RPC / latent` 语义归属属于 `实现方式不同`
- 以当前快照为准，`Angelscript` 相对 `UnLua` 这种“未传 `FLatentActionInfo` 也自动补回调”的体验，`没有实现同层自动补偿`

### [维度 D5] 变量观察模型：`UnLua` 输出有深度上限的值快照，`Angelscript` 输出可寻址变量以支撑 `DataBreakpoint`

前文已经把“谁拥有调试会话”说清了，这一轮补的是**调试器真正拿到的变量是什么形态**。`UnLua` 的 `FLuaDebugValue` 更像一个“可打印快照树”：它把 `table / userdata / TArray / TMap / TSet / UStruct` 递归展开成 `ReadableValue + Keys + Values`，但递归深度被 `MAX_LUA_VALUE_DEPTH = 4` 硬性截断。也就是说，`UnLua` 当前快照的仓内调试数据面，本质上是为了“把 Lua/UE 混合值变成人类可读文本树”。

`Angelscript` 的 `FAngelscriptVariable` 则明确是“可继续追踪的调试实体”。除了 `Name/Value/Type` 之外，协议层还会在 `DebugAdapterVersion >= 2` 时把 `ValueAddress` 和 `ValueSize` 一起发给前端；`RequestVariables` / `RequestEvaluate` 也会把 `GetAddressToMonitor()` 的结果写回消息。于是客户端不仅能显示值，还能继续把它升级成 data breakpoint 目标。这是两边非常核心的模型差异：`UnLua` 优先**可读展开**，`Angelscript` 优先**可寻址监视**。

```
[D5-Deep] Debug Value Materialization
UnLua
├─ lua stack value -> FLuaDebugValue
├─ BuildFromUserdata / BuildFromUStruct / containers
├─ ReadableValue + child Keys/Values
└─ depth capped at 4                               // 面向快照阅读

Angelscript
├─ debugger scope -> FAngelscriptVariable
├─ Name / Value / Type / bHasMembers
├─ ValueAddress + ValueSize when adapter v2
└─ client can arm DataBreakpoint                  // 面向继续监视
```

关键源码 [1]：`UnLua` 把调试值构造成深度受限的可读树

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDebugBase.h
// 函数: FLuaDebugValue 声明
// 行号: 21-50
// 位置: 调试值对象天生带深度上限和树形 children 容器
// ============================================================================
#define MAX_LUA_VALUE_DEPTH 4                                    // ★ 仓内调试值最多只展开 4 层

struct UNLUA_API FLuaDebugValue
{
    FString ReadableValue;                                       // ★ 直接面向人类阅读
    FString Type;
    int32 Depth;
    bool bAlreadyBuilt;
    TArray<FLuaDebugValue> Keys;
    TArray<FLuaDebugValue> Values;                               // ★ children 继续嵌套
};
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaDebugBase.cpp
// 函数: FLuaDebugValue::Build / AddKey / BuildFromUStruct / BuildFromTArray / BuildFromTMap
// 行号: 43-126, 128-154, 324-392
// 位置: 仓内调试数据面把 Lua/UE 混合值展开成文本快照树
// ============================================================================
void FLuaDebugValue::Build(lua_State *L, int32 Index, int32 Level)
{
    int32 ValueType = lua_type(L, Index);
    Type = lua_typename(L, ValueType);
    ...
    case LUA_TTABLE:
    {
        ...
        while (lua_next(L, Index) != 0)
        {
            FLuaDebugValue *Key = AddKey();
            if (Key)
            {
                Key->Build(L, -2, Level - 1);
                FLuaDebugValue *Value = AddValue();
                Value->Build(L, -1, Level - 1);                  // ★ 递归展开 table
            }
            lua_pop(L, 1);
        }
        ReadableValue = FString::Printf(TEXT("table(size=%d): 0x%p"), TableSize, TableAddress);
    }
}

FLuaDebugValue* FLuaDebugValue::AddKey()
{
    if (Depth >= MAX_LUA_VALUE_DEPTH)
    {
        return nullptr;                                          // ★ 超过 4 层直接截断
    }
    ...
}

for (FProperty *Property = Struct->PropertyLink; Property; Property = Property->PropertyLinkNext)
{
    FLuaDebugValue *Key = AddKey();
    if (Key)
    {
        Key->ReadableValue = Property->GetNameCPP();
    }
    FLuaDebugValue *Value = AddValue();
    if (Value)
    {
        Value->BuildFromUProperty(Property, Property->ContainerPtrToValuePtr<void>(ContainerPtr)); // ★ UObject/UStruct 继续展开成快照树
    }
}
```

关键源码 [2]：`Angelscript` 变量协议把“值地址”一起发给客户端，直接服务于数据断点

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h
// 函数: FAngelscriptVariable 序列化
// 行号: 416-438
// 位置: 协议变量不止是显示字符串，还带地址和大小
// ============================================================================
struct FAngelscriptVariable : FDebugMessage
{
    FString Name;
    FString Value;
    FString Type;
    uint64 ValueAddress;                                         // ★ 调试前端可继续监视这块内存
    uint8 ValueSize;
    bool bHasMembers = false;

    FORCEINLINE friend FArchive& operator<<(FArchive& Ar, FAngelscriptVariable& Msg)
    {
        Ar << Msg.Name;
        Ar << Msg.Value;
        Ar << Msg.Type;
        Ar << Msg.bHasMembers;

        if (AngelscriptDebugServer::DebugAdapterVersion >= 2)
        {
            Ar << Msg.ValueAddress;
            Ar << Msg.ValueSize;                                 // ★ v2 协议显式扩展出 data breakpoint 所需信息
        }
        return Ar;
    }
};
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 函数: FAngelscriptDebugServer::HandleMessage
// 行号: 1081-1128
// 位置: RequestVariables / RequestEvaluate 都把监视地址带回协议
// ============================================================================
else if (MessageType == EDebugMessageType::RequestVariables)
{
    ...
    Var.Name = Value.Name;
    Var.Value = Value.Value;
    Var.Type = Value.Type;
    Var.ValueAddress = reinterpret_cast<uint64>(Value.GetAddressToMonitor()); // ★ 变量不仅可看，还可继续监视
    Var.ValueSize = Value.GetAddressToMonitorValueSize();
    Var.bHasMembers = Value.bHasMembers;
    ...
}
else if (MessageType == EDebugMessageType::RequestEvaluate)
{
    ...
    Var.ValueAddress = reinterpret_cast<uint64>(Value.GetAddressToMonitor());
    Var.ValueSize = Value.GetAddressToMonitorValueSize();        // ★ evaluate 结果同样可升级为 data breakpoint 目标
    ...
}
```

新增对比结论：

- `UnLua` 当前仓内数据面偏“快照调试器”：重点是把 Lua/UE 混合对象解释成人能读的树。
- `Angelscript` 当前仓内数据面偏“监视调试器”：变量协议天然服务于后续 data breakpoint 和二次求值。
- 两边都能“看变量”，但一边返回的是**结构化描述**，另一边返回的是**协议级可追踪实体**。

差距判断：

- 变量观察模型属于 `实现方式不同`
- 以当前快照为准，`UnLua` 相对 `Angelscript DebugServer V2` 这种“变量消息自带地址/大小”的协议层能力，`没有实现同层地址化变量模型`

### [维度 D9] 测试分层粒度：`UnLua` 主要切成 `API Spec + Issue 回归`，`Angelscript` 把 `protocol / native / learning scenario` 再拆成三层

前文已经覆盖了 `UnLuaTestSuite` 的宏 DSL、地图回归和执行入口。本轮补的是**测试层与测试责任是不是继续往下拆**。`UnLua` 当前快照能清楚看到两大主层：一层是 `BEGIN_DEFINE_SPEC` 形式的 `UnLua.API.*`，用于直接验证 `FLuaEnv` 这类核心 API；另一层是 `BEGIN_TESTSUITE` 形式的 `UnLua.Regression.*`，用于 issue 驱动的真实地图/资产复现。这个分层对插件回归已经足够实用，但它没有继续在仓内拆出“协议单测 / 脱 UE 原生引擎单测 / 教学追踪场景”这样的更细责任面。

`Angelscript` 当前仓库则明显再多切了一刀。`Source/AngelscriptRuntime/Tests/` 里放的是 runtime 内部协议/transport 级 `CppTests`；`Source/AngelscriptTest/Native/` 里放的是只依赖公共 `angelscript.h` / `AngelscriptInclude.h` 的 standalone native-core 测试；`Source/AngelscriptTest/Learning/Runtime/` 则是带 trace 的教学/场景复现测试。这个分层的价值是：**协议坏了、公共 SDK 坏了、脚本到 UE 场景坏了**，会在不同测试层各自爆炸，而不是都挤进同一种 Automation case 里。

```
[D9-Deep] Test Layer Taxonomy
UnLua
├─ Specs: UnLua.API.*                              // API 行为规格
│  └─ BEGIN_DEFINE_SPEC + ProductFilter
└─ Regression: UnLua.Regression.*                  // issue / map / asset 回放
   └─ BEGIN_TESTSUITE + latent map commands

Angelscript
├─ Runtime CppTests: Angelscript.CppTests.*        // 协议/transport/内部机制
├─ Native Core: Angelscript.TestModule.Native.*    // 公共 SDK / standalone engine
└─ Learning Scenarios: Angelscript.TestModule.Learning.* // 教学 trace / UE 场景链路
```

关键源码 [1]：`UnLua` 的主层级是 `Spec` 和 `Regression/TestSuite`

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Public/UnLuaTestCommon.h
// 函数: IMPLEMENT_UNLUA_LATENT_TEST / IMPLEMENT_UNLUA_INSTANT_TEST / BEGIN_TESTSUITE
// 行号: 171-212
// 位置: TestSuite 宏把大多数测试收敛成 instant / latent / suite 三种壳
// ============================================================================
#define IMPLEMENT_UNLUA_LATENT_TEST(TestClass, PrettyName) \
IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass##_Runner, PrettyName, (EAutomationTestFlags::ClientContext | EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter))

#define IMPLEMENT_UNLUA_INSTANT_TEST(TestClass, PrettyName) \
IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass##Runner, PrettyName, (EAutomationTestFlags::ClientContext | EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter))

#define BEGIN_TESTSUITE(TestClass, PrettyName) \
namespace UnLuaTestSuite { \
IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, PrettyName, (EAutomationTestFlags::ClientContext | EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter))
// ★ 主壳是 UE Automation 的 suite / latent 变体
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Specs/LuaEnv.spec.cpp
// 函数: FLuaEnvSpec::Define
// 行号: 23-62
// 位置: API 规格测试直接对 FLuaEnv 做行为断言
// ============================================================================
BEGIN_DEFINE_SPEC(FLuaEnvSpec, "UnLua.API.FLuaEnv", EAutomationTestFlags::ProductFilter | EAutomationTestFlags::ApplicationContextMask)
...
It(TEXT("支持多个Lua环境"), EAsyncExecution::TaskGraphMainThread, [this]()
{
    UnLua::FLuaEnv Env1;
    UnLua::FLuaEnv Env2;
    Env1.DoString("return 1");
    Env2.DoString("return 2");
    TEST_EQUAL((int32)lua_tointeger(Env1.GetMainState(), -1), 1);
    TEST_EQUAL((int32)lua_tointeger(Env2.GetMainState(), -1), 2); // ★ 直接测 API 语义
});
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Tests/Issue603Test.cpp
// 函数: FIssue603Test::RunTest
// 行号: 22-30
// 位置: Regression 层回放真实地图，而不是只做函数级断言
// ============================================================================
BEGIN_TESTSUITE(FIssue603Test, TEXT("UnLua.Regression.Issue603 在Lua监听的事件中再次触发事件会导致崩溃"))

bool FIssue603Test::RunTest(const FString& Parameters)
{
    const auto MapName = TEXT("/UnLuaTestSuite/Tests/Regression/Issue603/Issue603");
    ADD_LATENT_AUTOMATION_COMMAND(FOpenMapLatentCommand(MapName))
    ADD_LATENT_AUTOMATION_COMMAND(FEngineWaitLatentCommand(1.0));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());        // ★ issue 回归以真实地图驱动
    return true;
}
```

关键源码 [2]：`Angelscript` 在仓内把协议、Native Core 和教学场景再拆成三层

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugTransportTests.cpp
// 函数: FAngelscriptDebugTransportSingleEnvelopeTest::RunTest
// 行号: 18-94
// 位置: Runtime CppTests 直接盯 transport / envelope，不需要脚本场景资产
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAngelscriptDebugTransportSingleEnvelopeTest,
    "Angelscript.CppTests.Debug.Transport.SingleEnvelope",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptDebugTransportSingleEnvelopeTest::RunTest(const FString& Parameters)
{
    ...
    TestEqual(TEXT("... should store the payload length as type-byte plus body"), MessageLength, static_cast<int32>(sizeof(uint8)) + Body.Num());
    TestEqual(TEXT("... should preserve the message type"), Envelope.MessageType, EDebugMessageType::DebugServerVersion); // ★ 直接测协议线格式
    ...
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Native/AngelscriptNativeCompileTests.cpp
// 函数: FAngelscriptNativeCompileSimpleFunctionTest::RunTest
// 行号: 10-56
// 位置: Native 层只创建 standalone script engine，验证公共 SDK 面
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAngelscriptNativeCompileSimpleFunctionTest,
    "Angelscript.TestModule.Native.Compile.SimpleFunction",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptNativeCompileSimpleFunctionTest::RunTest(const FString& Parameters)
{
    FNativeMessageCollector Messages;
    asIScriptEngine* ScriptEngine = CreateNativeEngine(&Messages); // ★ 不走 FAngelscriptEngine 内部私有面
    ...
    asIScriptModule* Module = BuildNativeModule(ScriptEngine, "NativeCompileSimpleFunction", "int Test() { return 42; }");
    return TestNotNull(TEXT("... should expose the compiled function"), GetNativeFunctionByDecl(Module, "int Test()"));
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningScriptClassToBlueprintTraceTests.cpp
// 函数: FAngelscriptLearningScriptClassToBlueprintTraceTest::RunTest
// 行号: 111-242
// 位置: Learning 层把脚本类 -> Blueprint child -> Actor instance 整条链路记成 trace
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAngelscriptLearningScriptClassToBlueprintTraceTest,
    "Angelscript.TestModule.Learning.Runtime.ScriptClassToBlueprint",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptLearningScriptClassToBlueprintTraceTest::RunTest(const FString& Parameters)
{
    ...
    Trace.AddStep(TEXT("CompileScriptClass"), TEXT("Compiled the script parent class with BlueprintOverride ..."));
    ...
    Trace.AddStep(TEXT("CreateBlueprintChild"), TEXT("Created a transient Blueprint asset that inherits from the generated script class"));
    ...
    Trace.AddStep(TEXT("SpawnBlueprintActor"), TEXT("Spawned an actor instance from the Blueprint-generated class into a test world"));
    ...
    const bool bInheritanceCorrect = TestTrue(TEXT("Blueprint class should inherit from the script parent"), BlueprintClass->IsChildOf(ScriptClass));
    const bool bBeginPlayCountCorrect = TestEqual(TEXT("Blueprint actor should preserve the script BeginPlay override"), BeginPlayCount, 1); // ★ 场景层顺便输出学习 trace
    ...
}
```

新增对比结论：

- `UnLua` 的测试当前更像“双层模型”：API 规格 + issue 场景回放。
- `Angelscript` 的测试当前更像“三层责任拆分”：runtime 机制、公共 native core、带 trace 的 UE 场景链路。
- 差别不在“谁测试更多”，而在**仓内是否继续把失败面切细**。

差距判断：

- 测试分层粒度属于 `实现方式不同`
- 以当前快照为准，`UnLua` 相对 `Angelscript` 这种“runtime wire-format / native public surface / learning scenario”并行分层，`没有实现同层仓内拆分`

---

## 深化分析 (2026-04-08 23:15:23)

### [维度 D2] 构建期默认参数采集：`UnLua` 生成“默认实参数据库”，`Angelscript` 生成“绑定覆盖率账本”

前几轮已经把 `UnLua` 的主绑定路径说明为“运行时反射 + 少量静态导出底座”。这一轮补的是**构建期到底还有没有第二条补偿链**。答案是有，而且作用非常具体：`UnLuaDefaultParamCollector` 在 `UHT/ScriptGenerator` 阶段把 `UFUNCTION` 默认参数和 `AutoCreateRefTerm` 参数收集成 `FFunctionCollection/FParameterCollection`，运行时 `FClassDesc` 再把这份表挂到类描述上，`FFunctionDesc::PreCall()` 在 Lua 实参缺位时直接补默认值。

`Angelscript` 也有很重的构建期链路，但目的不同。`AngelscriptUHTTool` 不是在生成“缺参时要填什么”的数据库，而是在生成“哪些 `BlueprintCallable` 成功 direct bind，哪些只能 stub，哪些跳过且为什么”的账本，然后再由自动化测试校验 `Summary.json / CSV / skipped reason` 是否自洽。也就是说，两边都在 build 阶段做补偿，但 `UnLua` 面向**运行时调用正确性**，`Angelscript` 面向**自动绑定可观测性**。

```
[D2-Deep] Build-Time Reflection Supplements
UnLua
├─ UHT exporter scans EngineRuntime/GameRuntime   // 只处理运行时模块
├─ Emit FFunctionCollection / FParameterCollection // 生成默认实参表
├─ FClassDesc binds GDefaultParamCollection       // 类描述持有默认参数集
└─ FFunctionDesc fills omitted Lua args           // 调用前补默认值

Angelscript
├─ UHT exporter scans BlueprintCallable/Pure      // 统计自动绑定候选
├─ Emit AS_FunctionTable_*.cpp shards             // 生成函数表分片
├─ Emit Summary.json + Module/Entry CSV           // 输出 direct/stub 账本
└─ Emit SkippedEntries/Reasons + tests            // 跳过原因进入正式产物
```

关键源码 [1]：`UnLua` 在构建期把默认参数固化成 C++ 产物

```csharp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaDefaultParamCollectorUbtPlugin/UnLuaDefaultParamCollectorUbtPlugin.cs
// 函数: UnLuaDefaultParamCollectorUbtPluginExporter / Generate / ExportParamProperty
// 行号: 16-23, 58-69, 119-166
// 位置: UHT exporter 只扫描 runtime 模块，并把默认参数写入 GeneratedContentBuilder
// ============================================================================
[UnrealHeaderTool]
class UnLuaDefaultParamCollectorUbtPlugin
{
    [UhtExporter(Name = "UnLua", Description = "UnLua Default Param Collector", Options = UhtExporterOptions.Default, ModuleName = "UnLua")]
    private static void UnLuaDefaultParamCollectorUbtPluginExporter(IUhtExportFactory factory)
    {
        new UnLuaDefaultParamCollectorUbtPlugin(factory).Generate(); // ★ 进入构建期参数采集
    }

    private void Generate()
    {
        foreach (UhtPackage package in Session.Packages)
        {
            var moduleType = package.Module.ModuleType;
            ParseModule(package.Module.Name, moduleType, package.Module.OutputDirectory);
            if (moduleType != UHTModuleType.EngineRuntime && moduleType != UHTModuleType.GameRuntime)
            {
                continue;                                            // ★ 非 runtime 模块直接跳过
            }
            QueueClassExports(package, package);
        }

        Finish();
    }

    private void ExportFunction(UhtClass classObj, UhtFunction function)
    {
        ...
        var autoCreateRefTerm = metaData.GetValueOrDefault("AutoCreateRefTerm");
        ...
        foreach (UhtType child in function.Children)
        {
            if (child is UhtProperty property && CanExportParamProperty(classObj, property))
            {
                ExportParamProperty(classObj, function, property, metaData, autoEmitParameterNames);
            }
        }
    }

    private void ExportParamProperty(UhtClass classObj, UhtFunction function, UhtProperty property, UhtMetaData metaData, string[] autoEmitParameterNames)
    {
        if (!FindDefaultValueString(metaData, property, out string valueStr))
        {
            if (!Array.Exists(autoEmitParameterNames, element => element == property.SourceName))
            {
                return;                                              // ★ 没默认值、也不是 AutoCreateRefTerm，就不进产物
            }
        }

        if (property is UhtStructProperty structProperty)
        {
            var structTypeName = structProperty.ScriptStruct.EngineName;
            if (structTypeName.Equals("Vector"))
            {
                if (string.IsNullOrEmpty(valueStr))
                {
                    PreAddProperty(classObj, function);
                    GeneratedContentBuilder.AppendFormat("PC->Parameters.Add(TEXT(\"{0}\"), SharedFVector_Zero);\r\n", property.SourceName);
                    // ★ 默认参数不是运行时现算，而是在生成代码里直接写死
                }
            }
        }
    }
}
```

关键源码 [2]：`UnLua` 运行时直接消费这份默认实参表

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/ClassDesc.cpp
// 函数: FClassDesc::FClassDesc / FClassDesc::RegisterField
// 行号: 38-42, 137-140
// 位置: 类描述构造时挂上默认参数集合，函数描述创建时再把该函数的默认参数传进去
// ============================================================================
if (bIsClass)
{
    Size = Struct->GetStructureSize();
    FunctionCollection = GDefaultParamCollection.Find(*ClassName);   // ★ 先按类名拿到默认参数集合
}

...

check(Function);
FParameterCollection* DefaultParams = FunctionCollection ? FunctionCollection->Functions.Find(FieldName) : nullptr;
FieldDesc->FieldIndex = Functions.Add(MakeShared<FFunctionDesc>(Function, DefaultParams)); // ★ 把该函数的默认参数集交给 FFunctionDesc
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp
// 函数: FFunctionDesc::PreCall
// 行号: 321-333
// 位置: Lua 少传参数时，用 build 期生成的默认值补齐 native 调用参数
// ============================================================================
else if (!Property->IsOutParameter())
{
    if (DefaultParams)
    {
        IParamValue **DefaultValue = DefaultParams->Parameters.Find(Property->GetProperty()->GetFName());
        if (DefaultValue)
        {
            const void *ValuePtr = (*DefaultValue)->GetValue();
            Property->CopyValue(Params, ValuePtr);                  // ★ 缺参时直接写回 native 参数缓冲
            CleanupFlags[i] = true;
        }
    }
}
```

关键源码 [3]：`Angelscript` 把构建期产物做成覆盖率账本与跳过原因表

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs
// 函数: Export / CountBlueprintCallableFunctions / WriteSkippedEntriesCsv / WriteSkippedReasonSummaryCsv
// 行号: 21-45, 65-88, 99-161
// 位置: UHT exporter 不采集默认值，而是统计自动绑定候选、跳过项和失败原因
// ============================================================================
[UhtExporter(
    Name = "AngelscriptFunctionTable",
    Description = "Exports Angelscript function table data",
    Options = UhtExporterOptions.Default | UhtExporterOptions.CompileOutput,
    CppFilters = ["AS_FunctionTable_*.cpp"],
    ModuleName = "AngelscriptRuntime")]
private static void Export(IUhtExportFactory factory)
{
    ...
    int generatedFileCount = AngelscriptFunctionTableCodeGenerator.Generate(factory);
    ...
    WriteSkippedEntriesCsv(factory, skippedEntries);               // ★ 正式写出 skipped 明细
    WriteSkippedReasonSummaryCsv(factory, skippedEntries);         // ★ 再按 reason 聚合
}

private static void CountBlueprintCallableFunctions(string moduleName, UhtType type, List<AngelscriptSkippedFunctionEntry> skippedEntries, ref int classCount, ref int functionCount, ref int reconstructedCount, ref int skippedCount)
{
    if (type is UhtClass classObj)
    {
        ...
        if (AngelscriptFunctionSignatureBuilder.TryBuild(classObj, function, out AngelscriptFunctionSignature? signature, out string? failureReason))
        {
            _ = signature!.BuildEraseMacro();
            reconstructedCount++;                                  // ★ 能重建签名的，记为可生成项
        }
        else
        {
            skippedCount++;
            skippedEntries.Add(new AngelscriptSkippedFunctionEntry(
                moduleName,
                classObj.SourceName,
                function.SourceName,
                string.IsNullOrEmpty(failureReason) ? "unknown" : failureReason)); // ★ 失败原因保留到产物
        }
    }
}
```

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs
// 函数: WriteGenerationSummary / WriteModuleSummaryCsv
// 行号: 166-205, 218-241
// 位置: 生成 JSON/CSV 正式产物，记录 direct/stub/shard 的模块分布
// ============================================================================
private static void WriteGenerationSummary(IUhtExportFactory factory, List<AngelscriptModuleGenerationSummary> moduleSummaries, List<AngelscriptGeneratedFunctionCsvEntry> csvEntries, int generatedFileCount)
{
    int totalGeneratedEntries = moduleSummaries.Sum(static summary => summary.TotalEntries);
    int totalDirectBindEntries = moduleSummaries.Sum(static summary => summary.DirectBindEntries);
    int totalStubEntries = moduleSummaries.Sum(static summary => summary.StubEntries);
    double directBindRate = totalGeneratedEntries > 0 ? (double)totalDirectBindEntries / totalGeneratedEntries : 0.0;
    double stubRate = totalGeneratedEntries > 0 ? (double)totalStubEntries / totalGeneratedEntries : 0.0;

    string summaryPath = factory.MakePath("AS_FunctionTable_Summary", ".json");
    ...
    File.WriteAllText(summaryPath, summaryJson, Encoding.UTF8);    // ★ 正式产物不是日志，而是可被测试和脚本消费的 JSON
    WriteModuleSummaryCsv(factory, moduleSummaries);
    WriteEntryCsv(factory, csvEntries);
}

private static void WriteModuleSummaryCsv(IUhtExportFactory factory, List<AngelscriptModuleGenerationSummary> moduleSummaries)
{
    ...
    builder.AppendLine("ModuleName,EditorOnly,TotalEntries,DirectBindEntries,StubEntries,DirectBindRate,StubRate,ShardCount");
    ...
    File.WriteAllText(csvPath, builder.ToString(), Encoding.UTF8); // ★ 模块级账本独立输出
}
```

关键源码 [4]：`Angelscript` 还用自动化测试守护这些 build 产物

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp
// 函数: FAngelscriptGeneratedFunctionTableSummaryOutputTest::RunTest / FAngelscriptGeneratedFunctionTableSkippedReasonSummaryCsvOutputTest::RunTest
// 行号: 459-590, 706-749
// 位置: 生成账本不是“写了就算”，而是被测试强制校验字段、比率和 skipped 聚合一致性
// ============================================================================
const FString SummaryPath = FPaths::Combine(GeneratedDirectory, TEXT("AS_FunctionTable_Summary.json"));
...
if (!TestTrue(TEXT("Generated function table summary test should find the UHT summary json output"), FFileHelper::LoadFileToString(SummaryJson, *SummaryPath)))
{
    return false;
}
...
TestEqual(TEXT("Generated function table summary test should match the generated binding registration count"), TotalGeneratedEntries, CountedRegistrations);
TestTrue(TEXT("Generated function table summary test should keep directBindRate and stubRate normalized"), FMath::Abs((DirectBindRate + StubRate) - 1.0) < 1.e-9);
...
const FString ReasonSummaryCsvPath = FPaths::Combine(GeneratedDirectory, TEXT("AS_FunctionTable_SkippedReasonSummary.csv"));
...
TestEqual(TEXT("Generated function table skipped reason summary test should write the expected summary csv header"), SummaryLines[0], TEXT("FailureReason,SkippedCount"));
...
SummedSkippedCount += FCString::Atoi(*Columns[1]);
...
TestEqual(TEXT("Generated function table skipped reason summary test should keep aggregate counts aligned with the skipped entry csv"), SummedSkippedCount, SkippedLines.Num() - 1);
```

新增对比结论：

- `UnLua` 的 build 期补偿是**调用语义补偿**，目标是让 Lua 少传参数时仍能正确落到 `UFunction`。
- `Angelscript` 的 build 期补偿是**自动绑定可观测性补偿**，目标是让 direct/stub/skipped 的分布可统计、可回归、可定位。
- 这说明两边都不是“纯 runtime”或“纯手写”一刀切，而是各自在 build 阶段补自己最痛的那一刀。

差距判断：

- 构建期补偿机制整体属于 `实现方式不同`
- 以当前快照为准，`Angelscript` 相对 `UnLua` 这种 `GDefaultParamCollection` 独立默认实参数据库，`没有实现`

### [维度 D5] 调试会话契约归属：`UnLua` 把 attach 交给外部 Lua 调试器，`Angelscript` 把握手/过滤器/数据断点收进仓内协议

前几轮已经证明 `UnLua` 当前快照里没有仓内 `DAP` server。这一轮补的是**仓库究竟拥有调试会话的哪一层契约**。`UnLua` 的文档直接要求用户去装 `LuaPanda / LuaHelper`、手动拷 `LuaPanda.lua`、并在脚本里执行 `require("LuaPanda").start("127.0.0.1", 8818)`。也就是说，session 的启动、协议、IDE 配置都明确交给外部 Lua 调试器。

但 `UnLua` 不是完全没做开发期支撑。它在 runtime 暴露了 `GetScriptRootPath()`、`GetFileLastModifiedTimestamp()` 和 `HotReload()`，自带的 `HotReload.lua` 直接依赖这组 API 把模块名映射到磁盘脚本，再按时间戳决定是否 reload。这说明 `UnLua` 仓内真正拥有的是**脚本文件身份与刷新信息**，而不是 debugger wire protocol。

`Angelscript` 则相反。`AngelscriptDebugServer.h` 把 `StartDebugging / DebugServerVersion / RequestBreakFilters / SetDataBreakpoints` 等消息类型直接定义在 runtime 里；`AngelscriptDebugServer.cpp` 收到 `StartDebugging` 就回 `DebugServerVersion`，收到 `RequestBreakFilters` 就回过滤器；`AngelscriptDebuggerSmokeTests.cpp` 还对这条握手链路做自动化测试。这不是“支持调试”而已，而是**仓库拥有调试协议本身**。

```
[D5-Deep] Debug Session Contract Ownership
UnLua
├─ Docs tell user to install LuaPanda/LuaHelper   // 外部调试器拥有会话协议
├─ User copies LuaPanda.lua into Content/Script   // attach 前提由用户完成
├─ Script calls require("LuaPanda").start(...)    // 会话由 Lua 脚本显式启动
└─ Runtime only exposes file path/timestamp APIs  // 仓内拥有文件身份，不拥有协议栈

Angelscript
├─ Runtime defines StartDebugging/BreakFilters    // 协议消息仓内定义
├─ Debug server replies DebugServerVersion        // 握手仓内实现
├─ Runtime owns data breakpoints                  // 硬件断点/协议状态都在仓内
└─ Smoke tests verify session handshake           // 协议闭环可自动回归
```

关键源码 [1]：`UnLua` 明确把 attach 契约交给外部 Lua 调试器

```md
<!-- =========================================================================
文件: Reference/UnLua/Docs/CN/Debugging.md
函数: 调试文档入口
行号: 1-18
位置: 文档直接要求用户安装外部 VSCode Lua 调试器，并在脚本里手工 start
=========================================================================== -->
# 调试
## 使用 LuaPanda / LuaHelper 调试

### 一、准备工作
1. 从VSCode应用市场安装 [LuaPanda](https://marketplace.visualstudio.com/items?itemName=stuartwang.luapanda) / [LuaHelper](https://marketplace.visualstudio.com/items?itemName=yinfei.luahelper)
2. 从 [LuaPanda官方仓库](https://github.com/Tencent/LuaPanda/tree/master/Debugger) 获取`LuaPanda.lua`，放入`{UE工程}/Content/Script`目录
3. 在Lua代码中加入 `require("LuaPanda").start("127.0.0.1",8818)`    <!-- ★ 会话由用户脚本显式启动 -->

### 二、开始调试
VSCode环境请参考各插件的配置文档，这里不再赘述。
...
注：调试器依赖`luasocket`，UnLua已通过扩展插件集成...
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaFunctionLibrary.h
// 函数: UUnLuaFunctionLibrary declaration
// 行号: 27-34
// 位置: runtime 暴露给脚本的是文件根路径、时间戳和热重载入口
// ============================================================================
UFUNCTION(BlueprintCallable)
static FString GetScriptRootPath();

UFUNCTION(BlueprintCallable)
static int64 GetFileLastModifiedTimestamp(FString Path);

UFUNCTION(BlueprintCallable)
static void HotReload();                                          // ★ 仓内拥有的是脚本身份/刷新 API
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaFunctionLibrary.cpp
// 函数: UUnLuaFunctionLibrary::GetScriptRootPath / GetFileLastModifiedTimestamp / HotReload
// 行号: 20-33
// 位置: 路径和时间戳都是为了让 Lua 侧工具脚本能定位实际文件并触发 reload
// ============================================================================
FString UUnLuaFunctionLibrary::GetScriptRootPath()
{
    return FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() + TEXT("Script/")); // ★ 模块名 -> 文件路径
}

int64 UUnLuaFunctionLibrary::GetFileLastModifiedTimestamp(FString Path)
{
    const FDateTime FileTime = IFileManager::Get().GetTimeStamp(*Path);
    return FileTime.GetTicks();                                   // ★ reload 侧用时间戳判断文件是否变化
}

void UUnLuaFunctionLibrary::HotReload()
{
    IUnLuaModule::Get().HotReload();
}
```

```lua
-- ============================================================================
-- 文件: Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua
-- 函数: config / get_last_modified_time
-- 行号: 15-19, 112-119
-- 位置: Lua 层 hot reload 脚本直接消费路径和时间戳 API
-- ============================================================================
local config = {
    debug = false,
    script_root_path = UE.UUnLuaFunctionLibrary.GetScriptRootPath(), -- ★ 用 UE 提供的根路径定位磁盘脚本
    ignore_modules = ignore_modules
}

...

local function get_last_modified_time(module_name)
    local filename = config.script_root_path .. module_name:gsub("%.", "/") .. ".lua"
    return UE.UUnLuaFunctionLibrary.GetFileLastModifiedTimestamp(filename) -- ★ 是否 reload 由文件时间戳决定
end
```

关键源码 [2]：`Angelscript` 把握手、过滤器和数据断点协议收进 runtime

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h
// 函数: EDebugMessageType / FStartDebuggingMessage / FDebugServerVersionMessage / FAngelscriptBreakFilters
// 行号: 15-16, 25-80, 103-126, 483-504
// 位置: 调试消息类型、版本协商和 break filters 都是仓内协议对象
// ============================================================================
#define DEBUG_SERVER_VERSION 2

enum class EDebugMessageType : uint8
{
    ...
    StartDebugging,
    ...
    RequestBreakFilters,
    BreakFilters,
    ...
    DebugServerVersion,
    ...
    SetDataBreakpoints,
    ClearDataBreakpoints,                                         // ★ 数据断点也是协议一等成员
};

struct FStartDebuggingMessage : FDebugMessage
{
    int32 DebugAdapterVersion = 0;                                // ★ 客户端先报 adapter 版本
    ...
};

struct FDebugServerVersionMessage : FDebugMessage
{
    int32 DebugServerVersion = 0;                                 // ★ 服务端回自己的协议版本
    ...
};

struct FAngelscriptBreakFilters : FDebugMessage
{
    TArray<FString> Filters;
    TArray<FString> FilterTitles;                                 // ★ 过滤器数据结构同样仓内定义
    ...
};
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 函数: FAngelscriptDebugServer::HandleMessage
// 行号: 897-910, 1147-1159
// 位置: runtime 直接处理 StartDebugging 和 RequestBreakFilters
// ============================================================================
else if (MessageType == EDebugMessageType::StartDebugging)
{
    FStartDebuggingMessage Msg;
    *Datagram << Msg;

    bIsDebugging = true;
    AngelscriptDebugServer::DebugAdapterVersion = Msg.DebugAdapterVersion; // ★ 记录客户端 adapter 版本

    FDebugServerVersionMessage DebugServerVersionMessage;
    DebugServerVersionMessage.DebugServerVersion = DEBUG_SERVER_VERSION;
    SendMessageToClient(Client, EDebugMessageType::DebugServerVersion, DebugServerVersionMessage); // ★ 立刻回握手版本
    ...
}
...
else if (MessageType == EDebugMessageType::RequestBreakFilters)
{
    TMap<FName, FString> FilterList;
    FAngelscriptRuntimeModule::GetDebugBreakFilters().ExecuteIfBound(FilterList);

    FAngelscriptBreakFilters Filters;
    for (auto& Elem : FilterList)
    {
        Filters.Filters.Add(Elem.Key.ToString());
        Filters.FilterTitles.Add(Elem.Value);
    }

    SendMessageToClient(Client, EDebugMessageType::BreakFilters, Filters); // ★ 过滤器通过仓内协议返回
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSmokeTests.cpp
// 函数: FAngelscriptDebuggerSmokeHandshakeTest::RunTest
// 行号: 16-133
// 位置: 自动化测试验证 StartDebugging -> DebugServerVersion -> BreakFilters -> StopDebugging 整条链路
// ============================================================================
FAngelscriptDebuggerTestSession Session;
...
if (!TestTrue(TEXT("Debugger.Smoke.Handshake should send StartDebugging"), Client.SendStartDebugging(2)))
{
    return false;
}
...
if (Envelope.IsSet() && Envelope->MessageType == EDebugMessageType::DebugServerVersion)
{
    DebugVersionEnvelope = MoveTemp(Envelope);                    // ★ 测试要求服务端必须返回版本消息
    return true;
}
...
TestEqual(TEXT("Debugger.Smoke.Handshake should report the current debug server version"), DebugServerVersion->DebugServerVersion, DEBUG_SERVER_VERSION);
...
if (!TestTrue(TEXT("Debugger.Smoke.Handshake should request debugger break filters"), Client.SendRequestBreakFilters()))
{
    return false;
}
...
if (Envelope.IsSet() && Envelope->MessageType == EDebugMessageType::BreakFilters)
{
    BreakFiltersEnvelope = MoveTemp(Envelope);                    // ★ 过滤器响应同样被回归测试覆盖
    return true;
}
```

新增对比结论：

- `UnLua` 仓内拥有的是“文件路径、时间戳、reload 入口”这层开发辅助 API，不拥有 debugger session 协议。
- `Angelscript` 仓内拥有的是“消息类型、版本协商、break filters、data breakpoint”这整条协议链，而且有自动化回归。
- 这里不应简单解读为“谁更先进”，而是**谁拥有调试会话的生命周期**。

差距判断：

- 调试会话 ownership 属于 `实现方式不同`
- 以当前快照为准，这一维度上 `Angelscript` 相对 `UnLua` 不构成“缺少实现”，而是把更多责任收进了仓内 runtime

### [维度 D9] 测试宿主闭环：`UnLua` 把测试环境声明在 sample `.uproject`，`Angelscript` 把测试矩阵声明在 runner

前文已经区分了 “plugin 自带 harness” 和 “仓库级 runner”。这一轮补的是**测试环境真正由谁来声明**。`UnLua` 的答案写在 `TPSProject.uproject` 里：样例工程显式启用 `RuntimeTests`、`EditorTests`、`FunctionalTestingEditor` 和 `UnLuaTestSuite`。这说明 UnLua 的测试闭环首先是“拿一个准备好的宿主工程，把所需 engine test plugin 和测试 plugin 一次启起来”。

`UnLuaTestSuite.Build.cs` 本身其实不重，它只声明 `Lua`、`UnLua`、`UMG`、`UnrealEd` 等依赖，更多环境拼装工作被外包给 `TPSProject`。也就是说，`UnLua` 的回归入口更像**样例工程承载的测试环境**。

`Angelscript` 的环境声明则落在 runner。`Tools/RunTests.ps1` 读取 `AgentConfig.ini`、强制超时、创建独立 `ABSLOG/ReportExportPath`、默认 `-NullRHI`、worktree mutex 防并发；`Tools/RunTestSuite.ps1` 再把 `Smoke / NativeCore / RuntimeCpp / HotReload / Debugger / ScenarioSamples` 这些 suite 显式枚举出来。这里的主角不再是某个 sample project，而是**仓库脚本化命令契约**。

```
[D9-Deep] Test Host Ownership
UnLua
├─ TPSProject.uproject enables RuntimeTests       // 宿主工程显式启 engine 测试插件
├─ TPSProject.uproject enables EditorTests
├─ TPSProject.uproject enables FunctionalTestingEditor
└─ TPSProject.uproject enables UnLuaTestSuite     // 测试闭环随 sample project 分发

Angelscript
├─ RunTests.ps1 resolves AgentConfig.ini          // 环境由 runner 统一装配
├─ Enforce timeout / mutex / output isolation     // 命令契约仓库级固定
├─ Default -NullRHI / ABSLOG / ReportExportPath   // headless 规范内建
└─ RunTestSuite.ps1 enumerates named suites       // 测试矩阵由 suite 脚本声明
```

关键源码 [1]：`UnLua` 的测试环境首先是一个准备好的宿主工程

```json
{
    "FileVersion": 3,
    "Modules": [
        {
            "Name": "TPSProject",
            "Type": "Runtime"
        }
    ],
    "Plugins": [
        {
            "Name": "RuntimeTests",
            "Enabled": true
        },
        {
            "Name": "EditorTests",
            "Enabled": true
        },
        {
            "Name": "FunctionalTestingEditor",
            "Enabled": true
        },
        {
            "Name": "UnLuaTestSuite",
            "Enabled": true
        }
    ]
}
```

上面的 JSON 对应 `Reference/UnLua/TPSProject.uproject:1-34`。重点不在插件名本身，而在**测试闭环被 sample project 明确持有**。

```csharp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/UnLuaTestSuite.Build.cs
// 函数: UnLuaTestSuite::ctor
// 行号: 33-64
// 位置: 测试模块自身依赖并不复杂，更多环境约束由 TPSProject.uproject 持有
// ============================================================================
PublicDependencyModuleNames.AddRange(
    new[]
    {
        "Core",
        "CoreUObject",
        "Engine",
        "Slate"
    }
);

PrivateDependencyModuleNames.AddRange(
    new[]
    {
        "Lua",
        "UnLua",
        "UMG"
    }
);

if (Target.bBuildEditor)
{
    PrivateDependencyModuleNames.Add("UnrealEd");                // ★ 测试模块只声明能力依赖
}

PrecompileForTargets = PrecompileTargetsType.Any;
```

关键源码 [2]：`Angelscript` 的测试环境由 runner 和 suite 显式声明

```powershell
# ============================================================================
# 文件: Tools/RunTests.ps1
# 函数: 参数解析 / 运行主流程
# 行号: 1-35, 42-101, 133-194
# 位置: runner 统一装配超时、互斥、headless 参数、日志和报告目录
# ============================================================================
[CmdletBinding(DefaultParameterSetName = 'Prefix')]
param(
    [string]$TestPrefix,
    [string]$Group,
    [string]$Label = '',
    [string]$OutputRoot = '',
    [int]$TimeoutMs = 0,
    [switch]$Render,
    [switch]$NoReport
)

$exitCodes = @{
    Success      = 0
    TestFailed   = 1
    TimedOut     = 2
    ConfigError  = 3
    WorktreeBusy = 4
}

...

$agentConfig = Resolve-AgentConfiguration -ProjectRoot $projectRoot
$resolvedTimeoutMs = Resolve-TimeoutMs -RequestedTimeoutMs $TimeoutMs -DefaultTimeoutMs $defaultTimeoutMs -ParameterName 'TimeoutMs'
...
$outputLayout = New-CommandOutputLayout -ProjectRoot $projectRoot -Category 'Tests' -Label $Label -RequestedOutputRoot $OutputRoot -LogFileName 'Automation.log'
...
$worktreeMutex = Acquire-NamedMutex -Name $worktreeMutexName -TimeoutMs 0
...
$argumentList = @(
    $agentConfig.ProjectFile
    "-ExecCmds=Automation RunTests $target; Quit"
    '-TestExit=Automation Test Queue Empty'
    '-BUILDMACHINE'
    '-Unattended'
    '-NoPause'
    '-NoSplash'
    '-stdout'
    '-FullStdOutLogOutput'
    '-UTF8Output'
    "-ABSLOG=$($outputLayout.LogPath)"                           // ★ 日志目录由 runner 强制隔离
    "-ReportExportPath=$($outputLayout.ReportPath)"             // ★ 报告目录同样由 runner 统一控制
    '-NOSOUND'
)

if (-not $Render) {
    $argumentList += '-NullRHI'                                 // ★ headless 默认值不是文档约定，而是脚本约定
}
```

```powershell
# ============================================================================
# 文件: Tools/RunTestSuite.ps1
# 函数: suiteDefinitions / 执行循环
# 行号: 41-84, 113-160
# 位置: 具名 suite 直接把仓库测试矩阵编码成命令层结构
# ============================================================================
$suiteDefinitions = [ordered]@{
    "Smoke" = @(
        @{ Prefix = "Angelscript.CppTests.MultiEngine"; Label = "MultiEngine" }
        @{ Prefix = "Angelscript.CppTests.Engine.DependencyInjection"; Label = "DependencyInjection" }
        @{ Prefix = "Angelscript.CppTests.Subsystem"; Label = "Subsystem" }
        @{ Prefix = "Angelscript.TestModule.Engine.BindConfig"; Label = "BindConfig" }
        @{ Prefix = "Angelscript.TestModule.Shared.EngineHelper"; Label = "SharedEngineHelper" }
        @{ Prefix = "Angelscript.TestModule.Parity"; Label = "Parity" }
    )
    "NativeCore" = @(
        @{ Prefix = "Angelscript.TestModule.Native"; Label = "Native" }
    )
    "RuntimeCpp" = @(
        @{ Prefix = "Angelscript.CppTests"; Label = "CppTests" }
    )
    "HotReload" = @(
        @{ Prefix = "Angelscript.TestModule.HotReload"; Label = "HotReload" }
    )
    "Debugger" = @(
        @{ Prefix = "Angelscript.CppTests.Debug."; Label = "CppDebugger" }
        @{ Prefix = "Angelscript.TestModule.Debugger."; Label = "TestModuleDebugger" }
    )
    "ScenarioSamples" = @(
        @{ Prefix = "Angelscript.TestModule.Actor"; Label = "Actor" }
        @{ Prefix = "Angelscript.TestModule.Component"; Label = "Component" }
        @{ Prefix = "Angelscript.TestModule.Delegate"; Label = "Delegate" }
        @{ Prefix = "Angelscript.TestModule.Interface"; Label = "Interface" }
    )
}

...

$selectedSuite = $suiteDefinitions[$Suite]
...
for ($index = 0; $index -lt $selectedSuite.Count; ++$index) {
    ...
    & powershell.exe @argList                                    // ★ suite 只是调度层，真正执行统一回到 RunTests.ps1
    if ($LASTEXITCODE -ne 0) {
        throw "Suite '$Suite' failed while executing prefix '$($entry.Prefix)' ..."
    }
}
```

新增对比结论：

- `UnLua` 的测试主语是 sample host project；谁启用了 `TPSProject.uproject`，谁就拿到了整套测试环境。
- `Angelscript` 的测试主语是 runner；谁调用 `RunTests.ps1/RunTestSuite.ps1`，谁就拿到了统一的执行契约。
- 两边都不是“有没有自动化测试”的问题，而是**环境声明写在工程里，还是写在命令层里**。

差距判断：

- 测试宿主 ownership 属于 `实现方式不同`
- 以当前快照为准，这一维度上 `Angelscript` 相对 `UnLua` 不构成缺少测试能力；差异主要在测试入口的打包方式

### [维度 D10] 教程与示例的执行载体：`UnLua` 用 sample project 教学，`Angelscript` 用 automation example 保活

前几轮已经覆盖了 `README/Docs/Tutorials` 的目录关系。这一轮补的是**示例最终以什么形式活着**。`UnLua` 的 `README.md` 直接把用户送进一串编号教程，链接落到 `Content/Script/Tutorials/*.lua`；这些 Lua 教程又会显式指向 sample 工程里的 C++ helper，例如 `12_CustomLoader.lua` 直接写明“本示例 C++ 源码：Source/TPSProject/TutorialBlueprintFunctionLibrary.cpp”，运行时也真会去调用 `UTutorialBlueprintFunctionLibrary.SetupCustomLoader()`。也就是说，`UnLua` 的教程不是静态片段，而是**README -> Lua tutorial -> sample native helper** 的纵向教学链。

`Angelscript` 的示例则主要活在自动化层。大量 `Source/AngelscriptTest/Examples/*.cpp` 把 `Example_*.as` 直接写成 `FScriptExampleSource`，由 `RunScriptExampleCompileTest()` 组合脚本文本、映射成 `ScriptExamples/<file>` 虚拟文件名、再交给测试引擎编译；覆盖率型示例还会继续在 world 里实例化类、读属性、断言默认值。也就是说，`Angelscript` 示例的主宿主是**regression harness**，不是独立 sample project。

```
[D10-Deep] Example Carrier
UnLua
├─ README links numbered Tutorials/*.lua          // 文档入口先把人送到可运行脚本
├─ Tutorial lua names sample C++ helper path      // 脚本直接指向原生教学代码
├─ TPSProject helper exposes BlueprintCallable API // 原生辅助能力来自示例工程
└─ Result: sample project is the teaching surface // 教学载体是完整工程

Angelscript
├─ Example tests embed or load Example_*.as       // 示例先进入 automation harness
├─ RunScriptExampleCompileTest compiles in memory // 示例先证明“可编译”
├─ Coverage tests spawn/runtime-assert behavior   // 再证明“可运行”
└─ Result: test harness is the example surface    // 示例载体是回归系统
```

关键源码 [1]：`UnLua` 的教程链条直接落到 sample project 的脚本与 C++ helper

```md
<!-- =========================================================================
文件: Reference/UnLua/README.md
函数: 更多示例
行号: 41-54
位置: README 直接把教程入口绑定到仓内真实 Lua 脚本
=========================================================================== -->
# 更多示例
  * [01_HelloWorld](Content/Script/Tutorials/01_HelloWorld.lua) 快速开始的例子
  * [02_OverrideBlueprintEvents](Content/Script/Tutorials/02_OverrideBlueprintEvents.lua) 覆盖蓝图事件（Overridden Functions）
  * [03_BindInputs](Content/Script/Tutorials/03_BindInputs.lua) 输入事件绑定
  * [04_DynamicBinding](Content/Script/Tutorials/04_DynamicBinding.lua) 动态绑定
  * [05_BindDelegates](Content/Script/Tutorials/05_BindDelegates.lua) 委托的绑定、解绑、触发
  * [06_NativeContainers](Content/Script/Tutorials/06_NativeContainers.lua) 引擎层原生容器访问
  * [07_CallLatentFunction](Content/Script/Tutorials/07_CallLatentFunction.lua) 在协程中调用 `Latent` 函数
  * [08_CppCallLua](Content/Script/Tutorials/08_CppCallLua.lua) 从C++调用Lua
  * [09_StaticExport](Content/Script/Tutorials/09_StaticExport.lua) 静态导出自定义类型到Lua使用
  * [10_Replications](Content/Script/Tutorials/10_Replications.lua) 覆盖网络复制事件
  * [11_ReleaseUMG](Content/Script/Tutorials/11_ReleaseUMG.lua) 释放UMG相关对象
  * [12_CustomLoader](Content/Script/Tutorials/12_CustomLoader.lua) 自定义加载器   <!-- ★ README 直接指向可运行脚本 -->
  * [13_AnimNotify](Content/Script/Tutorials/AN_FootStep.lua) 动画通知
```

```lua
-- ============================================================================
-- 文件: Reference/UnLua/Content/Script/Tutorials/12_CustomLoader.lua
-- 函数: M:ReceiveBeginPlay
-- 行号: 1-11, 26-39
-- 位置: 教程脚本不只讲概念，而是明确说明对应的 sample C++ helper，并在运行时调用它
-- ============================================================================
--[[
    说明：通过绑定 FUnLuaDelegates::CustomLoadLuaFile 可以实现自定义Lua加载器
    ...
    本示例C++源码：
    Source\TPSProject\TutorialBlueprintFunctionLibrary.cpp          -- ★ 教程脚本直接指向 sample 原生代码
]]

function M:ReceiveBeginPlay()
    ...
    UE.UTutorialBlueprintFunctionLibrary.SetupCustomLoader(1)      -- ★ 教学能力来自 sample project helper
    Screen.Print(string.format("FromCustomLoader1:%s", require("Tutorials")))
    ...
    UE.UTutorialBlueprintFunctionLibrary.SetupCustomLoader(2)
    Screen.Print(string.format("FromCustomLoader2:%s", require("Tutorials")))
    ...
    UE.UTutorialBlueprintFunctionLibrary.SetupCustomLoader(0)
end
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Source/TPSProject/TutorialBlueprintFunctionLibrary.cpp
// 函数: CustomLoader1 / CustomLoader2 / UTutorialBlueprintFunctionLibrary::SetupCustomLoader
// 行号: 46-105
// 位置: sample 工程真的提供了教程脚本调用的原生辅助能力
// ============================================================================
bool CustomLoader1(UnLua::FLuaEnv& Env, const FString& RelativePath, TArray<uint8>& Data, FString& FullPath)
{
    const auto SlashedRelativePath = RelativePath.Replace(TEXT("."), TEXT("/"));
    FullPath = FString::Printf(TEXT("%s%s.lua"), *GLuaSrcFullPath, *SlashedRelativePath);
    ...
}

bool CustomLoader2(UnLua::FLuaEnv& Env, const FString& RelativePath, TArray<uint8>& Data, FString& FullPath)
{
    ...
    lua_getglobal(L, "package");
    lua_getfield(L, -1, "path");                                  // ★ 第二种教程直接示范按 package.path 查找
    ...
}

void UTutorialBlueprintFunctionLibrary::SetupCustomLoader(int Index)
{
    switch (Index)
    {
    case 0:
        FUnLuaDelegates::CustomLoadLuaFile.Unbind();
        break;
    case 1:
        FUnLuaDelegates::CustomLoadLuaFile.BindStatic(CustomLoader1);
        break;
    case 2:
        FUnLuaDelegates::CustomLoadLuaFile.BindStatic(CustomLoader2); // ★ 教程脚本调用的就是这个切换器
        break;
    }
}
```

关键源码 [2]：`Angelscript` 把示例直接收编为 automation input 和 runtime coverage

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleTestSupport.cpp
// 函数: AngelscriptScriptExamples::RunScriptExampleCompileTest
// 行号: 16-59
// 位置: 示例不是文档附件，而是先进入 automation harness 编译
// ============================================================================
bool RunScriptExampleCompileTest(FAutomationTestBase& Test, const FScriptExampleSource& Example)
{
    ...
    const FString ExampleFileName = Example.ExampleFileName;
    const FString ModuleNameString = FPaths::GetBaseFilename(ExampleFileName);
    ...
    FAngelscriptEngine& Engine = AcquireCleanSharedCloneEngine();
    const FName ModuleName(*ModuleNameString);
    ...
    if (Example.DependencyScriptText != nullptr)
    {
        CombinedScriptCode += Example.DependencyScriptText;        // ★ 依赖脚本一起拼进测试输入
        CombinedScriptCode += TEXT("\n\n");
    }

    CombinedScriptCode += Example.ScriptText;

    const FString VirtualFileName = FString::Printf(TEXT("ScriptExamples/%s"), *ExampleFileName);
    const bool bCompiled = CompileAnnotatedModuleFromMemory(&Engine, ModuleName, VirtualFileName, CombinedScriptCode);
    Test.TestTrue(*FString::Printf(TEXT("Compile example '%s' succeeds"), *ExampleFileName), bCompiled); // ★ 示例先证明“可编译”
    return bCompiled;
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleCoverageTests.cpp
// 函数: CompileCoverageExample / FAngelscriptScriptExampleCoverageActorTest::RunTest
// 行号: 25-62, 159-215
// 位置: 覆盖率示例继续在 runtime 里实例化类、读默认值、断言行为
// ============================================================================
UClass* CompileCoverageExample(
    FAutomationTestBase& Test,
    FAngelscriptEngine& Engine,
    FName ModuleName,
    const TCHAR* RelativePath,
    FName GeneratedClassName)
{
    const FString AbsolutePath = GetCoverageExampleAbsolutePath(RelativePath);
    ...
    return CompileScriptModule(Test, Engine, ModuleName, RelativePath, ScriptSource, GeneratedClassName); // ★ 从磁盘示例脚本编到真实脚本类
}

bool FAngelscriptScriptExampleCoverageActorTest::RunTest(const FString& Parameters)
{
    ...
    UClass* ScriptClass = CompileCoverageExample(*this, Engine, ModuleName, TEXT("Documents/Plans/Plan_ScriptExamplesExpansion/Coverage/Example_Coverage_Actor.as"), TEXT("ACoverageExampleActor"));
    ...
    AActor* Actor = SpawnScriptActor(*this, Spawner, ScriptClass);
    ...
    TestEqual(TEXT("Coverage actor example should preserve reflected int defaults"), Health, 125); // ★ 示例不只编译，还断言默认值
    TestEqual(TEXT("Coverage actor example should preserve reflected string defaults"), DisplayName, FString(TEXT("CoverageActor")));
    TestTrue(TEXT("Coverage actor example should execute BeginPlay override"), bBeginPlayTriggered);
    TestTrue(TEXT("Coverage actor example should enable replication through default statements"), Actor->GetIsReplicated());
}
```

新增对比结论：

- `UnLua` 的示例主宿主是 `TPSProject`，目标是让外部用户“打开工程就能跑教程”。
- `Angelscript` 的示例主宿主是 `Automation`，目标是让仓库维护者“改动后还能证明示例继续成立”。
- 这不是“谁文档更多”的差别，而是**示例的第一责任人是谁：学习者，还是回归系统**。

差距判断：

- 示例载体属于 `实现方式不同`
- 若目标是面向外部用户交付一套可直接打开的教学工程，当前 `Angelscript` 相对 `UnLua` 这种 `TPSProject` 式对外 sample host，`没有实现同层交付物`

---

## 深化分析 (2026-04-08 23:31:35)

### [维度 D2] 签名 ownership 的新增发现：`UnLua` 把 `UFunction` 适配器延迟到第一次字段访问，`Angelscript` 把“哪些函数能自动进脚本”前移到 UHT 账本

前面几轮已经确认 `UnLua` 的主路径是反射按需绑定，`Angelscript` 的主路径是显式绑定 + 反射回退。本轮补的是更细的一层：**签名是谁来拥有、在什么时候被物化**。`UnLua` 不是“完全没有适配器”，而是把适配器创建推迟到脚本第一次摸到某个字段时；届时 `FClassDesc::RegisterField()` 才会把 `UFunction` 包成 `FFunctionDesc`，并在后续每次调用时由 `PreCall()` 动态做参数类型校验、默认参数填充和 latent 参数补偿。也就是说，它省掉的是作者手写 glue，不是调用期的描述构建。

`Angelscript` 这边，新发现是显式绑定成本并不全落在 `Bind_*.cpp`。当前仓库已经把一部分“可自动恢复的 `BlueprintCallable/Pure` 函数签名”前移到了 `AngelscriptUHTTool`：UHT exporter 会遍历模块、尝试重建函数签名、把失败原因落成 CSV；运行时的 `BlueprintCallableReflectiveFallback` 只接收通过资格判断且脚本声明尚未绑定的函数。这意味着 `Angelscript` 的工程成本不是纯手写，而是**UHT 账本 + 运行时回退**两段式。

```
[D2-Deep] Signature Ownership
UnLua
├─ Script touches field on demand                   // 第一次访问字段才建描述
├─ FClassDesc::RegisterField()
│  ├─ FindPropertyByName / FindFunctionByName
│  └─ FPropertyDesc::Create / FFunctionDesc(...)
└─ FFunctionDesc::PreCall()                         // 每次调用再做类型检查、默认参数、latent 补偿

Angelscript
├─ UHT exporter scans BlueprintCallable/Pure        // 编译期先扫函数表
├─ TryBuild() -> signature | failureReason          // 失败原因入账
├─ Skipped CSV becomes observability artifact       // 形成“为什么没自动绑定”的账本
└─ Runtime reflective fallback binds only eligible functions
```

关键源码 [1]：`UnLua` 的签名对象是惰性创建的，但参数补偿在调用期持续发生

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/ClassDesc.cpp
// 函数: FClassDesc::RegisterField
// 行号: 68-71, 117-142
// 位置: 第一次访问字段时，才把 UProperty/UFunction 变成可缓存的描述对象
// ============================================================================
FProperty* Property = Struct->FindPropertyByName(FieldName);
UFunction* Function = (!Property && bIsClass) ? AsClass()->FindFunctionByName(FieldName) : nullptr;

if (OuterStruct != Struct)
{
    FClassDesc* OuterClass = Env->GetClassRegistry()->RegisterReflectedType(OuterStruct);
    return OuterClass->RegisterField(FieldName, QueryClass);      // ★ 先沿继承链找到真正 owner
}

if (Property)
{
    TSharedPtr<FPropertyDesc> Ptr(FPropertyDesc::Create(Property));
    FieldDesc->FieldIndex = Properties.Add(Ptr);                  // ★ 属性描述按需创建后缓存
    ++FieldDesc->FieldIndex;
}
else
{
    FParameterCollection* DefaultParams = FunctionCollection ? FunctionCollection->Functions.Find(FieldName) : nullptr;
    FieldDesc->FieldIndex = Functions.Add(MakeShared<FFunctionDesc>(Function, DefaultParams));
    ++FieldDesc->FieldIndex;
    FieldDesc->FieldIndex = -FieldDesc->FieldIndex;               // ★ 函数描述同样在首次访问时物化
}
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp
// 函数: FFunctionDesc::FFunctionDesc / FFunctionDesc::PreCall
// 行号: 31-75, 279-333
// 位置: 惰性创建的函数描述会记录返回值、out/ref、latent 槽位，并在每次调用前填参数
// ============================================================================
FFunctionDesc::FFunctionDesc(UFunction *InFunction, FParameterCollection *InDefaultParams)
    : DefaultParams(InDefaultParams), ReturnPropertyIndex(INDEX_NONE), LatentPropertyIndex(INDEX_NONE)
{
    for (TFieldIterator<FProperty> It(InFunction); It && (It->PropertyFlags & CPF_Parm); ++It)
    {
        FProperty *Property = *It;
        FPropertyDesc* PropertyDesc = FPropertyDesc::Create(Property);   // ★ 每个参数都建类型桥
        int32 Index = Properties.Add(TUniquePtr<FPropertyDesc>(PropertyDesc));
        if (PropertyDesc->IsReturnParameter())
            ReturnPropertyIndex = Index;
        else if (LatentPropertyIndex == INDEX_NONE && Property->GetFName() == NAME_LatentInfo)
            LatentPropertyIndex = Index;                                 // ★ latent 槽位单独记账
        else if (Property->HasAnyPropertyFlags(CPF_OutParm | CPF_ReferenceParm) && !Property->HasAnyPropertyFlags(CPF_ConstParm))
            OutPropertyIndices.Add(Index);
    }
}

void FFunctionDesc::PreCall(lua_State* L, int32 NumParams, int32 FirstParamIndex, FFlagArray& CleanupFlags, void* Params, void* Userdata)
{
    ...
    if (ParamIndex < NumParams)
    {
#if ENABLE_TYPE_CHECK == 1
        if (Property->CheckPropertyType(L, FirstParamIndex + ParamIndex, ErrorMsg))
            CleanupFlags[i] = Property->WriteValue_InContainer(L, Params, FirstParamIndex + ParamIndex, false); // ★ 调用期做类型检查和写入
#endif
    }
    else if (!Property->IsOutParameter() && DefaultParams)
    {
        IParamValue **DefaultValue = DefaultParams->Parameters.Find(Property->GetProperty()->GetFName());
        if (DefaultValue)
        {
            const void *ValuePtr = (*DefaultValue)->GetValue();
            Property->CopyValue(Params, ValuePtr);                        // ★ 默认参数同样在调用期补
            CleanupFlags[i] = true;
        }
    }
}
```

关键源码 [2]：`Angelscript` 先在 UHT 阶段形成“能自动恢复哪些函数签名”的账本

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs
// 函数: AngelscriptFunctionTableExporter::Export / CountBlueprintCallableFunctions
// 行号: 21-53, 65-96, 99-161
// 位置: 编译期扫描 BlueprintCallable/Pure 函数，并把失败原因导出成 CSV
// ============================================================================
[UhtExporter(
    Name = "AngelscriptFunctionTable",
    Description = "Exports Angelscript function table data",
    Options = UhtExporterOptions.Default | UhtExporterOptions.CompileOutput,
    CppFilters = ["AS_FunctionTable_*.cpp"],
    ModuleName = "AngelscriptRuntime")]
private static void Export(IUhtExportFactory factory)
{
    ...
    foreach (UhtModule module in factory.Session.Modules)
    {
        CountBlueprintCallableFunctions(module.ShortName, module.ScriptPackage, skippedEntries, ref classCount, ref functionCount, ref reconstructedCount, ref skippedCount);
    }

    WriteSkippedEntriesCsv(factory, skippedEntries);               // ★ 把“没法自动恢复”的原因写出来
    WriteSkippedReasonSummaryCsv(factory, skippedEntries);
}

private static void CountBlueprintCallableFunctions(...)
{
    if (child is UhtFunction function && IsBlueprintCallable(function))
    {
        functionCount++;
        if (AngelscriptFunctionSignatureBuilder.TryBuild(classObj, function, out AngelscriptFunctionSignature? signature, out string? failureReason))
            reconstructedCount++;                                  // ★ 能恢复的函数进入自动账本
        else
            skippedEntries.Add(new AngelscriptSkippedFunctionEntry(moduleName, classObj.SourceName, function.SourceName, failureReason ?? "unknown"));
    }
}
```

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs
// 函数: AngelscriptFunctionSignatureBuilder::TryBuild
// 行号: 43-100
// 位置: 不是所有 BlueprintCallable 都能自动恢复；失败原因是显式建模的
// ============================================================================
public static bool TryBuild(UhtClass classObj, UhtFunction function, out AngelscriptFunctionSignature? signature, out string? failureReason)
{
    ...
    if (failureReason == "non-public" || failureReason == "unexported-symbol")
        return false;                                              // ★ 某些函数直接判定不自动恢复

    if (failureReason == "overloaded-unresolved" && !IsWhitelistedDirectBindFallback(classObj, function))
        return false;

    foreach (UhtType parameterType in function.ParameterProperties.Span)
    {
        if (parameterType is not UhtProperty property)
        {
            failureReason = "non-property-parameter";
            return false;
        }

        if (property.ArrayDimensions != null)
        {
            failureReason = "static-array-parameter";
            return false;                                          // ★ 自动路径失败会留下明确原因
        }
    }

    signature = new AngelscriptFunctionSignature(..., true);
    failureReason = null;
    return true;
}
```

关键源码 [3]：`Angelscript` 的反射回退只接收已经通过 runtime 资格门槛的函数

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp
// 函数: BindBlueprintCallableReflectiveFallback
// 行号: 374-420
// 位置: runtime 回退不是“全开”，而是建立在编译期/运行期双重筛选之后
// ============================================================================
bool BindBlueprintCallableReflectiveFallback(
    TSharedRef<FAngelscriptType> InType,
    UFunction* Function,
    FAngelscriptFunctionSignature& Signature,
    FFuncEntry& Entry)
{
    Entry.bReflectiveFallbackBound = false;

    if (!ShouldBindBlueprintCallableReflectiveFallback(Function))
        return false;                                              // ★ 先过 runtime eligibility

    if (!Signature.bAllTypesValid || Signature.ArgumentTypes.Num() > BlueprintCallableReflectiveFallbackMaxArgs)
        return false;

    if (IsScriptDeclarationAlreadyBound(InType, Signature))
        return false;                                              // ★ 已有显式绑定时不覆盖

    ...
    Entry.bReflectiveFallbackBound = true;
    return true;
}
```

新增对比结论：

- `UnLua` 的“零胶水”更准确地说是“零作者 glue，非零调用适配器”。源码证据是 `RegisterField()` 首次访问才创建 `FFunctionDesc`，而 `PreCall()` 每次都要做参数装箱与默认值补偿。
- `Angelscript` 的显式绑定成本已经部分被 `UHT exporter` 吸收。它不是纯靠人肉维护 `Bind_*.cpp`，而是用 `TryBuild()` + `skipped CSV` 把自动恢复边界显式化。
- 真正的差异不在“有没有反射”，而在“签名可观测性属于运行期缓存，还是构建期账本”。

差距判断：

- `Angelscript` 相对 `UnLua` 的“首次访问再建描述对象”属于 `实现方式不同`，不是缺少反射能力。
- `UnLua` 相对 `Angelscript` 当前这种 `skipped CSV / failureReason` 级别的自动绑定可观测性，属于 `没有实现同层观测账本`。
- 若目标是降低手写绑定密度，`Angelscript` 更值得吸收的是 `UHT 账本化自动恢复`，而不是直接退回 `UnLua` 式纯运行时签名恢复。

### [维度 D4] 热重载事务边界的新增发现：`UnLua` 的原子单元是“本次 Lua module 批次”，`Angelscript` 的原子单元是“文件队列 + 编译结果状态机”

前文已经覆盖 `UnLua` 的对象图修补和 `Angelscript` 的编译事务。本轮新增的是**失败时谁来记账、下次谁来重试**。`UnLua` 的热重载核心在 `HotReload.lua`：它先把目标模块全部放进 sandbox，任何一个 module 的 `xpcall()` 失败都会直接 `sandbox.exit(); return`，因此这批 module 在真正执行 `update_modules()` 之前不会污染旧世界；一旦全部通过，它又会原地改写旧 module table、复制新字段、重接 upvalue，再通过 `ULuaEnvLocator_ByGameInstance::HotReload()` fan-out 到所有 `FLuaEnv`。所以 `UnLua` 的原子性是“这批 module 要么都 patch，要么一个都不 patch”，但它没有仓内的 failed queue、full reload queue。

`Angelscript` 则把失败和回放都建模进 `FAngelscriptEngine`。`PerformHotReload()` 会把 `PreviouslyFailedReloadFiles` 重新并入当前批次，失败时区分 `preprocess failed`、`Error`、`ErrorNeedFullReload`、`PartiallyHandled`，分别写回 `PreviouslyFailedReloadFiles` 或 `QueuedFullReloadFiles`；随后由 `Tick()` 按当前环境选择 `SoftReloadOnly` 还是 `FullReload`，`CheckForHotReload()` 只负责消费已经排好的文件队列。这让 `Angelscript` 的热重载不是一次性 patch，而是一个可以延迟升级、自动重试的状态机。

```
[D4-Deep] Reload Transaction Boundary
UnLua
├─ ULuaEnvLocator_ByGameInstance::HotReload()      // 对所有 FLuaEnv fan-out
├─ sandbox.load all target modules
├─ any xpcall failure -> sandbox.exit() + return   // 本批次整体放弃
└─ success -> update_modules -> merge_objects      // 原地 patch 旧 table / upvalue

Angelscript
├─ CheckForHotReload() collects file/add/delete queues
├─ PerformHotReload() merges PreviouslyFailedReloadFiles
├─ Preprocess -> CompileModules
├─ ErrorNeedFullReload -> QueuedFullReloadFiles
├─ Error -> PreviouslyFailedReloadFiles
└─ Next tick chooses SoftReloadOnly or FullReload
```

关键源码 [1]：`UnLua` 的批次原子性来自 sandbox 全量预装载，成功后才原地 patch

```lua
-- ============================================================================
-- 文件: Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua
-- 函数: reload_modules / merge_objects / M.reload
-- 行号: 223-248, 553-624
-- 位置: 先在 sandbox 中尝试装载全部 module；任何一个失败就退出，本轮不做对象图改写
-- ============================================================================
local function merge_objects(module_res)
    for _, m in ipairs(module_res) do
        ...
        elseif type(new) == "table" and not sandbox.is_loaded(new) then
            for k, nv in pairs(new) do
                old[k] = nv                                       -- ★ 成功后直接原地覆盖旧 table
            end
        elseif type(new) == "function" then
            ...
            debug.setupvalue(new, i, uv.replaced_upvalue)         -- ★ upvalue 也在原对象上重接
        end
    end
end

local function reload_modules(module_names)
    ...
    sandbox.enter(tmp_modules)
    ...
    for _, module_name in ipairs(module_names) do
        local func, env = sandbox.load(module_name)
        if func ~= nil then
            local ok, new_module = xpcall(func, load_error_handler)
            if not ok then
                sandbox.exit()
                return                                            -- ★ 任一 module 失败，本批次整体取消
            end
            ...
        else
            sandbox.exit()
            return
        end
    end

    update_modules(old_modules, new_modules, module_envs)         -- ★ 只有全部通过才真正改旧世界
    sandbox.exit()
end

function M.reload(module_names)
    ...
    if #modified_modules > 0 then
        reload_modules(modified_modules)                          -- ★ 没有 failed queue，直接按修改集重试
    end
end
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnvLocator.cpp
// 函数: ULuaEnvLocator_ByGameInstance::HotReload
// 行号: 76-82
// 位置: 热重载不是只作用一个 Lua VM，而是 fan-out 到当前 locator 维护的全部 env
// ============================================================================
void ULuaEnvLocator_ByGameInstance::HotReload()
{
    if (Env)
        Env->HotReload();
    for (const auto& Pair : Envs)
        Pair.Value->HotReload();                                  // ★ 当前 locator 持有的多个 env 一起热更
}
```

关键源码 [2]：`Angelscript` 把失败恢复、延迟 full reload 和自动重试都编码进状态机

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: FAngelscriptEngine::PerformHotReload
// 行号: 2253-2461, 2467-2490
// 位置: 热重载先并入历史失败文件，再做依赖扩张、预处理、编译和测试准备
// ============================================================================
bool FAngelscriptEngine::PerformHotReload(ECompileType CompileType, const TArray<FFilenamePair>& InReloadFiles)
{
    ...
    for (auto& FailedFile : PreviouslyFailedReloadFiles)
    {
        if (!FileManager.FileExists(*FailedFile.AbsolutePath))
            AlreadyDeletedFiles.Add(FailedFile);
        FileList.AddUnique(FailedFile);                           // ★ 历史失败文件自动并入本轮
    }
    PreviouslyFailedReloadFiles.Empty();

    ...
    Preprocessor.AddFile(PathPair.RelativePath, PathPair.AbsolutePath, bTreatAsDeleted);
    ...
    if (!bPreprocessSuccess)
    {
        UE_LOG(Angelscript, Error, TEXT("Hot reload failed in preprocessing. Keeping all old angelscript code."));
        PreviouslyFailedReloadFiles.Append(FileList);             // ★ 预处理失败直接记账，下轮重试
        return false;
    }

    ECompileResult Result = CompileModules(CompileType, Preprocessor.GetModulesToCompile(), CompiledModules);
    if (Result == ECompileResult::ErrorNeedFullReload)
        return false;                                             // ★ 交给后续状态处理决定如何升级
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: FAngelscriptEngine::CheckForHotReload / FAngelscriptEngine::Tick / CompileModules 尾部状态处理
// 行号: 2729-2779, 2794-2829, 4130-4187
// 位置: CheckForHotReload 消费文件队列；Tick 决定 soft/full；编译结果再决定失败文件和 full reload 队列
// ============================================================================
void FAngelscriptEngine::CheckForHotReload(ECompileType CompileType)
{
    ...
    if (FileList.Num() != 0)
    {
        PerformHotReload(CompileType, FileList);
    }
}

void FAngelscriptEngine::Tick(float DeltaTime)
{
    ...
    if (!GIsEditor || HasGameWorld())
        CheckForHotReload(ECompileType::SoftReloadOnly);          // ★ PIE / runtime 只能 soft reload
    else
        CheckForHotReload(ECompileType::FullReload);              // ★ editor 空闲态才允许 full reload
}

ECompileResult Result = ECompileResult::FullyHandled;
if (!bShouldSwapInModules || bHadCompileErrors)
    Result = bFullReloadRequired ? ECompileResult::ErrorNeedFullReload : ECompileResult::Error;
else if (!bWasFullyHandled)
    Result = ECompileResult::PartiallyHandled;

if (Result == ECompileResult::ErrorNeedFullReload)
{
    for (const auto& RepeatFile : AllCompiledFiles)
        QueuedFullReloadFiles.Add(RepeatFile);                    // ★ 需要升级成 full reload 的文件单独入队
    PreviouslyFailedReloadFiles.Append(AllCompiledFiles);
}
else if (Result == ECompileResult::Error)
{
    PreviouslyFailedReloadFiles.Append(AllCompiledFiles);         // ★ 普通失败保留自动重试
}
else if (Result == ECompileResult::PartiallyHandled)
{
    for (const auto& RepeatFile : AllCompiledFiles)
        QueuedFullReloadFiles.Add(RepeatFile);                    // ★ 软重载没完全收敛，延迟补一次 full reload
}
```

新增对比结论：

- `UnLua` 的热重载原子边界是“module 批次”，不是“文件状态机”。其仓内策略重点是尽量保住旧 table/upvalue，不是维护失败文件账本。
- `Angelscript` 的热重载原子边界是“文件队列 + 编译状态”。失败、部分成功、需要升级成 full reload 都会留下显式队列，下一轮继续消费。
- 这解释了为什么 `UnLua` 更适合轻量业务 patch，而 `Angelscript` 更适合结构化脚本系统的可恢复编译循环。

差距判断：

- `UnLua` 相对 `Angelscript` 的 `PreviouslyFailedReloadFiles / QueuedFullReloadFiles` 这类失败队列模型，属于 `没有实现同层失败记账机制`。
- `Angelscript` 相对 `UnLua` 的原地 table/upvalue 修补不是“更好”或“更差”，而是 `实现方式不同`；它承担的是类型/类重载事务，而不是 Lua 闭包修补。
- 若 `Angelscript` 要吸收 `UnLua` 经验，可借鉴的是“先 sandbox 全量试装再提交”的批次语义；若 `UnLua` 要补强工程性，最缺的是失败队列与 reload 升级策略。

### [维度 D9] 机器调用契约的新增发现：`UnLua` 交付的是可被 UE Automation 发现的测试名，`Angelscript` 交付的是仓库自带 runner contract

前几轮已经写过测试分层、宿主工程和 sample project。本轮补的是更贴近 CI 的问题：**机器拿到仓库之后，最小的稳定调用面是什么**。`UnLua` 当前快照把这个调用面停在 UE Automation 层。`UnLuaTestCommon.h` 提供 `IMPLEMENT_SIMPLE_AUTOMATION_TEST` 宏包装，`LuaEnv.spec.cpp` 这种 spec 把测试名挂成 `"UnLua.API.FLuaEnv"`。这意味着外部系统当然可以调用它，但调用约定主要依赖“你知道该怎么起 UE Automation”。结合本轮补充扫描，`Reference/UnLua/` 当前快照里没有随仓附带 `.github/workflows`、`Jenkinsfile` 之类的 runner/CI 清单，仓内没有把这层契约再向前封装。

`Angelscript` 则多走了一步。`Tools/RunTests.ps1` 不只是“方便手工执行”的脚本，它定义了 exit code、超时预算、互斥锁、`ABSLOG`、`ReportExportPath`、`-NullRHI` 等稳定参数；`Tools/RunTestSuite.ps1` 再把 `Smoke / Bindings / Internals / HotReload / Debugger / ScenarioSamples` 这些 suite 名编进仓库。对 CI 来说，真正可依赖的不是单个测试前缀，而是这套 runner contract。

```
[D9-Deep] Machine Invocation Contract
UnLua
├─ IMPLEMENT_SIMPLE_AUTOMATION_TEST macros         // 测试先注册成 UE Automation 名称
├─ Specs publish names like UnLua.API.FLuaEnv
├─ TPSProject / UnLuaTestSuite provide host context
└─ Current snapshot ships no repo-level runner manifest

Angelscript
├─ RunTests.ps1 defines timeout / exit codes / logs
├─ RunTestSuite.ps1 defines named suites
├─ Mutex + report path + NullRHI are standardized
└─ Repo already ships a CI-ready invocation contract
```

关键源码 [1]：`UnLua` 的稳定接口是 Automation 名称，不是仓库级 runner

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Public/UnLuaTestCommon.h
// 函数: IMPLEMENT_UNLUA_LATENT_TEST / IMPLEMENT_TESTSUITE_TEST
// 行号: 171-212
// 位置: 测试被注册成 UE Automation 测试名，调用约定停留在 Automation 框架层
// ============================================================================
#define IMPLEMENT_UNLUA_LATENT_TEST(TestClass, PrettyName) \
IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass##_Runner, PrettyName, (EAutomationTestFlags::ClientContext | EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)) \
bool TestClass##_Runner::RunTest(const FString& Parameters) \
{ \
    TestClass* TestInstance = new TestClass(); \
    ... \
    ADD_LATENT_AUTOMATION_COMMAND(FUnLuaTestCommand_PerformTest(TestInstance)); \
    ... \
    return true; \
}

#define IMPLEMENT_TESTSUITE_TEST(TestClass, PrettyName) \
IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, PrettyName, (EAutomationTestFlags::ClientContext | EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter))
// ★ 仓内提供的是 Automation 注册宏，而不是统一 runner / suite 入口
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Specs/LuaEnv.spec.cpp
// 函数: BEGIN_DEFINE_SPEC(FLuaEnvSpec, "UnLua.API.FLuaEnv", ...)
// 行号: 23-27, 34-69
// 位置: 测试名称直接暴露给 UE Automation；外部系统需要自己知道如何筛选并启动它们
// ============================================================================
BEGIN_DEFINE_SPEC(FLuaEnvSpec, "UnLua.API.FLuaEnv", EAutomationTestFlags::ProductFilter | EAutomationTestFlags::ApplicationContextMask)
    TSharedPtr<UnLua::FLuaEnv> Env;
END_DEFINE_SPEC(FLuaEnvSpec)

void FLuaEnvSpec::Define()
{
    Describe(TEXT("创建Lua环境"), [this]()
    {
        It(TEXT("支持多个Lua环境"), EAsyncExecution::TaskGraphMainThread, [this]()
        {
            ...
        });
    });
}
// ★ 当前快照里测试命名非常清晰，但“怎么批量、超时、产物目录、失败码”并不在同一层交付
```

关键源码 [2]：`Angelscript` 把 CI 需要的运行契约直接收进仓库脚本

```powershell
# ============================================================================
# 文件: Tools/RunTests.ps1
# 函数: 参数解析 / 主流程
# 行号: 1-35, 42-105, 133-219
# 位置: runner 显式定义 exit code、超时、互斥锁、日志和报告目录
# ============================================================================
[CmdletBinding(DefaultParameterSetName = 'Prefix')]
param(
    [string]$TestPrefix,
    [string]$Group,
    [string]$Label = '',
    [string]$OutputRoot = '',
    [int]$TimeoutMs = 0,
    [switch]$Render,
    [switch]$NoReport
)

$exitCodes = @{
    Success      = 0
    TestFailed   = 1
    TimedOut     = 2
    ConfigError  = 3
    WorktreeBusy = 4
}

$worktreeMutex = Acquire-NamedMutex -Name $worktreeMutexName -TimeoutMs 0
...
$argumentList = @(
    $agentConfig.ProjectFile
    "-ExecCmds=Automation RunTests $target; Quit"
    ...
    "-ABSLOG=$($outputLayout.LogPath)"                            # ★ 机器可稳定收集日志
    "-ReportExportPath=$($outputLayout.ReportPath)"              # ★ 报告目录契约化
)

if (-not $Render) {
    $argumentList += '-NullRHI'                                  # ★ headless 默认行为内建
}
```

```powershell
# ============================================================================
# 文件: Tools/RunTestSuite.ps1
# 函数: suiteDefinitions / 执行循环
# 行号: 41-84, 113-160
# 位置: suite 名称就是仓库级 CI 接口，不要求外部系统自己拼接一堆 Automation 前缀
# ============================================================================
$suiteDefinitions = [ordered]@{
    "Smoke" = @(
        @{ Prefix = "Angelscript.CppTests.MultiEngine"; Label = "MultiEngine" }
        @{ Prefix = "Angelscript.CppTests.Engine.DependencyInjection"; Label = "DependencyInjection" }
        ...
    )
    "Bindings" = @(
        @{ Prefix = "Angelscript.TestModule.Bindings"; Label = "Bindings" }
    )
    "HotReload" = @(
        @{ Prefix = "Angelscript.TestModule.HotReload"; Label = "HotReload" }
    )
    "Debugger" = @(
        @{ Prefix = "Angelscript.CppTests.Debug."; Label = "CppDebugger" }
        @{ Prefix = "Angelscript.TestModule.Debugger."; Label = "TestModuleDebugger" }
    )
}

for ($index = 0; $index -lt $selectedSuite.Count; ++$index) {
    ...
    & powershell.exe @argList                                    # ★ suite 入口仓库内稳定存在
    if ($LASTEXITCODE -ne 0) {
        throw "Suite '$Suite' failed while executing prefix '$($entry.Prefix)' ..."
    }
}
```

新增对比结论：

- `UnLua` 当前快照并不缺测试，而是把“机器如何调用这些测试”留给了外部 UE Automation 调度器或宿主工程。
- `Angelscript` 当前仓库把这一层也产品化了：测试选择、日志产物、失败码、headless 默认值都已经形成稳定脚本接口。
- 这解释了为什么 `Angelscript` 更容易直接挂到 CI，而 `UnLua` 更像“你先有一个 UE 自动化运行环境，再把测试插件接进去”。

差距判断：

- `UnLua` 相对 `Angelscript` 这种仓库自带 `runner + suite` 契约，属于 `没有实现同层 CI 入口封装`。
- `UnLua` 的 Automation 宏与 spec 组织并不差，这部分不是质量问题；它和 `Angelscript` 的核心差异在于 `实现方式不同`，即接口停在 Automation 名称层还是上提到仓库脚本层。
- 如果 `Angelscript` 继续演进 CI，优先级更高的不是再加更多测试名，而是保持 `RunTests.ps1 / RunTestSuite.ps1` 这类契约稳定；这正是 `UnLua` 当前快照里没有同层交付物的地方。

---

## 深化分析 (2026-04-08 23:45:28)

### [维度 D3] 覆写入口何时进入类表：`UnLua` 在绑定期替换现有槽位，`Angelscript` 在类生成期固化 `BlueprintOverride/Mixin`

前文已经区分过 `UnLuaInterface + GetModuleName()` 和 `BlueprintOverride/Mixin` 的模型差异。这一轮补的是一个更底层、也更影响编辑器体验的问题：**脚本覆写到底在什么时候成为 `UClass` 的正式函数表成员**。`UnLua` 的答案是“等绑定发生时再 patch”。`IUnLuaInterface::GetModuleName()` 只负责提供脚本路径；真正改 `FunctionMap` 的动作发生在 `UUnLuaManager::BindClass()`，它先读取 Lua table 函数名，再调用 `ULuaFunction::Override()`。源码里专门有 `__UClassBindSucceeded` 哨兵来兼容 Blueprint recompile 清空 `FuncMap` 的场景，这进一步说明 `UnLua` 的覆写面不是类生成时天然存在，而是**绑定后补进去、蓝图重编译后再补一次**。

`Angelscript` 则把这一步前移到脚本编译和类生成期。预处理器看到 `BlueprintEvent` 时就调用 `GenerateBlueprintEventWrapper()` 并把脚本实现后缀成 `_Implementation`；类生成器随后验证 `BlueprintOverride` 是否真在父类存在、签名是否匹配，再把 `Mixin` 参数名和 `FUNC_BlueprintEvent` 等 flag 直接写进新建的 `UFunction`，最后在 `NewClass->AddFunctionToFunctionMap()` 后完成 `StaticLink` 和 `GetDefaultObject(true)`。因此 `Angelscript` 的覆写/混入不是“运行时 patch 现有槽位”，而是**在类成型前就把 Blueprint 语义固化进 `UClass`**。

```
[D3-Deep] Override Surface Activation Timing
UnLua
├─ Blueprint/C++ class implements UUnLuaInterface   // 资产只先保存 module path
├─ UUnLuaManager::BindClass()                       // 真正进入绑定流程
├─ GetFunctionNames + GetOverridableFunctions       // 运行时扫描 Lua/UE 两侧函数面
├─ ULuaFunction::Override()                         // ★ 绑定期替换 native thunk / FunctionMap
└─ "__UClassBindSucceeded" sentinel                 // Blueprint recompile 后靠哨兵重挂

Angelscript
├─ Preprocessor sees BlueprintEvent/Override        // 声明期先改写语义
├─ GenerateBlueprintEventWrapper()                  // 生成 wrapper + _Implementation
├─ ClassGenerator validates superclass signature    // 编译期检查 override 合法性
├─ NewFunction gets Mixin metadata / Blueprint flag // 元数据在类生成期落地
└─ AddFunctionToFunctionMap() before class finalize // ★ 类完成前已进入函数表
```

关键源码 [1]：`UnLua` 的覆写面在绑定时才插入 `UClass`

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaInterface.h
// 函数: IUnLuaInterface::GetModuleName
// 行号: 33-38
// 位置: 接口层只返回脚本路径，本身并不创建或注册新的 UFunction
// ============================================================================
UFUNCTION(BlueprintNativeEvent)
FString GetModuleName() const;                                      // ★ 这里只决定 module 路径
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaManager.cpp
// 函数: UUnLuaManager::BindClass
// 行号: 253-267, 300-315, 333-340
// 位置: 绑定时才扫描 Lua table 并替换 Class 上对应的 UFunction
// ============================================================================
if (Classes.Contains(Class))
{
#if WITH_EDITOR
    if (Class->FindFunctionByName("__UClassBindSucceeded", EIncludeSuperFlag::Type::ExcludeSuper))
        return true;                                                // ★ Blueprint recompile 后靠哨兵判断是否已重挂

    ULuaFunction::RestoreOverrides(Class);                          // ★ FuncMap 被清空后先恢复再重绑
#endif
}

auto& BindInfo = Classes.Add(Class);
BindInfo.Class = Class;
BindInfo.ModuleName = InModuleName;
BindInfo.TableRef = Ref;

UnLua::LowLevel::GetFunctionNames(Env->GetMainState(), Ref, BindInfo.LuaFunctions);
ULuaFunction::GetOverridableFunctions(Class, BindInfo.UEFunctions);

for (const auto& LuaFuncName : BindInfo.LuaFunctions)
{
    UFunction** Func = BindInfo.UEFunctions.Find(LuaFuncName);
    if (Func)
        ULuaFunction::Override(*Func, Class, LuaFuncName);          // ★ 直到绑定时才真的 patch 进类表
}

for (const auto& Iter : BindInfo.UEFunctions)
{
    if (Class->FindFunctionByName(Iter.Key, EIncludeSuperFlag::Type::ExcludeSuper))
    {
        Class->AddFunctionToFunctionMap(Iter.Value, "__UClassBindSucceeded"); // ★ 成功标记本身也是后插入
        break;
    }
}
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaFunction.cpp
// 函数: ULuaFunction::SetActive
// 行号: 210-239
// 位置: 激活覆写时，把 UFunction 的 native thunk 和 Script 字节码替换到 Lua 桥
// ============================================================================
if (bAdded)
{
    SetSuperStruct(Function);
    FunctionFlags |= FUNC_Native;
    SetNativeFunc(execCallLua);
    Class->AddFunctionToFunctionMap(this, *GetName());             // ★ 新函数在激活时才进入 FunctionMap
}
else
{
    SetSuperStruct(Function->GetSuperStruct());
    Script = Function->Script;
    Children = Function->Children;
    ChildProperties = Function->ChildProperties;

    Function->FunctionFlags |= FUNC_Native;
    Function->SetNativeFunc(&execScriptCallLua);                   // ★ 旧函数入口被改写到 Lua thunk
    Function->GetOuterUClass()->AddNativeFunction(*Function->GetName(), &execScriptCallLua);
    Function->Script.Empty();
    Function->Script.AddUninitialized(ScriptMagicHeaderSize + sizeof(ULuaFunction*));
}
```

关键源码 [2]：`Angelscript` 在预处理/类生成期把覆写和 mixin 语义写进 `UClass`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp
// 函数: FAngelscriptPreprocessor::GenerateBlueprintEventWrapper 调用点
// 行号: 1499-1529
// 位置: 看到 BlueprintEvent 时就生成 wrapper，并把脚本实现名后缀成 _Implementation
// ============================================================================
else if (Spec.Name == PP_NAME_BlueprintEvent)
{
    if (FunctionDesc->bBlueprintOverride)
    {
        MacroError(File, Macro, FString::Printf(TEXT("UFUNCTION() %s cannot be both BlueprintEvent and BlueprintOverride."), *FunctionDesc->FunctionName));
        continue;
    }

    bool bAlreadyHasWrapper = FunctionDesc->bBlueprintEvent;
    FunctionDesc->bBlueprintEvent = true;
    FunctionDesc->bCanOverrideEvent = true;

    if (!bAlreadyHasWrapper)
    {
        GenerateBlueprintEventWrapper(File, Chunk, Macro, FunctionDesc); // ★ 先生成 Blueprint wrapper
        FunctionDesc->ScriptFunctionName += TEXT("_Implementation");      // ★ 脚本实现名在预处理期就改写
    }
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 函数: 覆写合法性校验 + UFunction 物化
// 行号: 732-780, 2820-2836, 3435-3458
// 位置: 先验证 override，再把 mixin metadata / Blueprint flag 写入新函数，最后加入类表
// ============================================================================
if (FunctionDesc->bBlueprintOverride)
{
    auto* ParentFunction = GetBlueprintEventByScriptName(CodeSuperClass, FunctionDesc->FunctionName);
    ...
    if (!SuperFunctionDesc.IsValid())
        FAngelscriptEngine::Get().ScriptCompileError(...);         // ★ 父类不存在事件，编译期直接报错
    else if (!SuperFunctionDesc->SignatureMatches(FunctionDesc))
        FAngelscriptEngine::Get().ScriptCompileError(...);         // ★ 签名不匹配也在编译期拦住
}

UFunction* NewFunction = NewObject<UFunction>(NewClass, *FuncName, RF_Public);
NewFunction->FunctionFlags = FUNC_Event | FUNC_BlueprintEvent | FUNC_Public;
NewFunction->Bind();
NewFunction->StaticLink(true);
NewClass->Children = NewFunction;
NewClass->AddFunctionToFunctionMap(NewFunction, NewFunction->GetFName()); // ★ 类完成前已进入函数表

if (ScriptFunction->traits.GetTrait(asTRAIT_MIXIN) && ScriptFunction->parameterNames.GetLength() >= 1)
{
    FString MixinArgumentName = ANSI_TO_TCHAR(ScriptFunction->parameterNames[0].AddressOf());
    NewFunction->SetMetaData(NAME_Function_MixinArgument, *MixinArgumentName); // ★ mixin 语义是类级 metadata
    NewFunction->SetMetaData(NAME_Function_DefaultToSelf, *MixinArgumentName);
}

if ((FunctionDesc->bBlueprintEvent && FunctionDesc->bCanOverrideEvent) || FunctionDesc->bBlueprintOverride)
    NewFunction->FunctionFlags |= FUNC_BlueprintEvent;             // ★ Blueprint 事件标记在类生成期落地
```

新增对比结论：

- `UnLua` 的轻量入口优势不只在于 `GetModuleName()` 简单，而在于它把“脚本是否接管行为面”推迟到绑定期决定，因此可以低摩擦地贴到既有 Blueprint 资产上。
- `Angelscript` 的 `BlueprintOverride/Mixin` 优势不只在语法表达力，而在于这些语义在类生成期就进入 `UClass`，所以父类检查、签名匹配、Blueprint 可见性都能提前成立。
- 这不是“谁更灵活”的空泛比较，而是**类表 ownership 在运行时还是编译期**的根本差异。

差距判断：

- `Angelscript` 相对 `UnLua` 的“绑定后再 patch 行为面”属于 `实现方式不同`，不是缺少 Blueprint 交互。
- `UnLua` 相对 `Angelscript` 这种“覆写/混入在类生成期就进入函数表”的机制，属于 `没有实现同层预固化类表能力`。
- 如果 `Angelscript` 要吸收 `UnLua` 经验，更合适的方向是补一个轻量 attach 入口，而不是削弱现有编译期类生成约束。

### [维度 D5] 断点 ownership 的新增发现：`UnLua` 仓内只有“栈/变量观测面”，`Angelscript` 仓内拥有断点落点与重放规则

前几轮已经把“谁拥有调试会话协议”讲清了，这一轮补的是一个更偏调试器内核的层次：**谁来决定断点应该落在哪一行、热重载后如何继续有效**。`UnLua` 当前快照仓内提供的是 `GetStackVariables()`、`GetLuaCallStack()` 这一层观察接口，它能把 Lua/UE 混合值和当前 `source/currentline` 暴露出来；但文档仍然要求用户安装 `LuaPanda` / `LuaHelper`、拷入 `LuaPanda.lua` 并手工 `start()`。这说明仓内没有同层的 breakpoint bookkeeping，也没有源码 canonicalization / next-code-line relocation / reload 后重放。

`Angelscript` 则把这一层直接做进 `DebugServer V2`。`SetBreakpoint` 先 `CanonizeFilename()`，再按文件名或模块名定位脚本模块，并用 `FindNextLineWithCode()` 把用户请求行吸附到最近的可执行代码行；如果行号被改写，还会回发 `SetBreakpoint` 变更消息给客户端。运行时 `ProcessScriptLine()` 按 `Section + Line` 查 `SectionBreakpoints`，而 `ReapplyBreakpoints()` 又会在模块重载后重新解析模块并恢复 `hasBreakPoints`。更关键的是，这整条链路有自动化测试验证 top frame 的 `Source/LineNumber` 是否真的回到目标 fixture。于是 `Angelscript` 不只是“能断点”，而是**仓内拥有断点语义本身**。

```
[D5-Deep] Breakpoint Ownership
UnLua
├─ Docs require LuaPanda/LuaHelper + LuaPanda.lua  // attach 与断点协议属于外部工具
├─ GetStackVariables / GetLuaCallStack             // 仓内只提供观测 API
└─ source/currentline exposed as runtime snapshot  // 断点落点规则不在仓内

Angelscript
├─ SetBreakpoint -> CanonizeFilename()             // 文件名先归一化
├─ Resolve module + FindNextLineWithCode()         // 请求行自动吸附到可执行行
├─ ProcessScriptLine checks SectionBreakpoints     // 停机判定仓内完成
├─ ReapplyBreakpoints() after module reload        // 热重载后继续重放
└─ Breakpoint tests assert top frame source/line   // 行号语义有自动回归
```

关键源码 [1]：`UnLua` 当前仓内只拥有“观测面”，不拥有断点布线规则

```md
<!-- ============================================================================
文件: Reference/UnLua/Docs/CN/Debugging.md
函数: 调试接入说明
行号: 1-14
位置: 仓内文档明确把 attach、Lua 调试脚本和 IDE 配置交给外部工具
============================================================================ -->
# 调试
## 使用 LuaPanda / LuaHelper 调试

1. 从VSCode应用市场安装 LuaPanda / LuaHelper
2. 从 LuaPanda 官方仓库获取 `LuaPanda.lua`，放入 `{UE工程}/Content/Script` 目录
3. 在Lua代码中加入 `require("LuaPanda").start("127.0.0.1",8818)`  <!-- ★ attach 由用户脚本触发 -->

注：调试器依赖 `luasocket`，UnLua 已通过扩展插件集成
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDebugBase.h
// 函数: FLuaDebugValue / GetStackVariables / GetLuaCallStack
// 行号: 21-24, 29-50, 84-91
// 位置: 仓内主要暴露值快照和调用栈 API，没有断点消息或文件行号重映射对象
// ============================================================================
#define MAX_LUA_VALUE_DEPTH 4

struct UNLUA_API FLuaDebugValue
{
    FString ReadableValue;
    FString Type;
    int32 Depth;
    TArray<FLuaDebugValue> Keys;
    TArray<FLuaDebugValue> Values;                               // ★ 这是值展开树，不是断点协议对象
};

UNLUA_API bool GetStackVariables(lua_State *L, int32 StackLevel, TArray<FLuaVariable> &LocalVariables, TArray<FLuaVariable> &Upvalues, int32 Level = MAX_int32);
UNLUA_API FString GetLuaCallStack(lua_State *L);                 // ★ 只承诺返回调用栈描述
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaDebugBase.cpp
// 函数: GetLuaCallStack
// 行号: 672-686
// 位置: 仓内源码定位能力停在 "source + currentline" 文本层
// ============================================================================
while (lua_getstack(L, Depth++, &ar))
{
    lua_getinfo(L, "nSl", &ar);
    FString DisplayInfo = FString::Printf(
        TEXT("Source : %s, name : %s, Line : %d \n"),
        UTF8_TO_TCHAR(ar.source),
        UTF8_TO_TCHAR(ar.name),
        ar.currentline);                                         // ★ 这里只是读取当前行，不负责断点落点与回放
    CallStack += DisplayInfo;
}
```

关键源码 [2]：`Angelscript` 把断点落点、触发与重放都做成仓内协议行为

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 函数: FAngelscriptDebugServer::HandleMessage(SetBreakpoint 分支)
// 行号: 955-1057
// 位置: 仓内负责文件名归一化、模块定位、最近可执行行吸附和断点变更回传
// ============================================================================
FAngelscriptBreakpoint BP;
*Datagram << BP;
FString OriginalFilename = BP.Filename;
BP.Filename = CanonizeFilename(BP.Filename);                     // ★ 先统一文件名格式

auto ModuleDesc = Manager.GetModuleByFilenameOrModuleName(BP.Filename, BP.ModuleName);
const FString& Key = ModuleDesc.IsValid() ? ModuleDesc->ModuleName : BP.Filename;
TSharedPtr<FFileBreakpoints>& Active = Breakpoints.FindOrAdd(Key);

int32 WantedLine = BP.LineNumber;
int32 CodeLine = -1;

for (int32 i = 0, Count = FoundModule->scriptFunctions.GetLength(); i < Count; ++i)
{
    asCScriptFunction* Func = FoundModule->scriptFunctions[i];
    int32 LineInFunc = Func->FindNextLineWithCode(WantedLine);   // ★ 自动找最近可执行代码行
    ...
    if (BestLine == -1 || (LineInFunc - WantedLine) < (BestLine - WantedLine))
        BestLine = LineInFunc;
}

if (CodeLine != WantedLine && BP.Id != -1)
{
    FAngelscriptBreakpoint ChangedBP;
    ChangedBP.Filename = OriginalFilename;
    ChangedBP.LineNumber = CodeLine;
    ChangedBP.Id = BP.Id;
    SendMessageToClient(Client, EDebugMessageType::SetBreakpoint, ChangedBP); // ★ 把修正后的真实行号回传给前端
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 函数: FAngelscriptDebugServer::ProcessScriptLine / ReapplyBreakpoints
// 行号: 576-603, 1197-1215
// 位置: 运行时按 section+line 判定断点命中；模块重载后还能重放断点状态
// ============================================================================
else if (BreakpointCount > 0
    && Context->m_currentFunction != nullptr
    && Context->m_currentFunction->module != nullptr
    && Context->m_currentFunction->module->hasBreakPoints)
{
    const char* Section = nullptr;
    int32 Line = Context->GetLineNumber(0, nullptr, &Section);   // ★ 当前执行点是 section + line
    TSharedPtr<FFileBreakpoints>& ActiveBreakpoints = SectionBreakpoints.FindOrAdd(Section);
    ...
    if (ActiveBreakpoints->Lines.Contains(Line) && !bWasIgnored)
        bIsPaused = true;                                         // ★ 停机规则就在仓内
}

for (auto& BreakpointElem : Breakpoints)
{
    auto ModuleDesc = Manager.GetModuleByModuleName(BreakpointElem.Key);
    FileBreakpoints->Module = ModuleDesc;
    if (ModuleDesc.IsValid())
        ((asCModule*)ModuleDesc->ScriptModule)->hasBreakPoints = true; // ★ 热重载后重放断点状态
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp
// 函数: FAngelscriptDebuggerBreakpointHitLineTest::RunTest
// 行号: 370-430
// 位置: 自动化测试直接验证断点停在目标 fixture 的文件与行号
// ============================================================================
FAngelscriptBreakpoint Breakpoint;
Breakpoint.Filename = Fixture.Filename;
Breakpoint.ModuleName = Fixture.ModuleName.ToString();
Breakpoint.LineNumber = Fixture.GetLine(TEXT("BreakpointHelperLine"));
Client.SendSetBreakpoint(Breakpoint);

const FAngelscriptCallStack& Callstack = MonitorResult.CapturedCallstack.GetValue();
TestTrue(TEXT("... should report the fixture filename in the top stack frame"), Callstack.Frames[0].Source.EndsWith(Fixture.Filename));
TestEqual(TEXT("... should stop at the requested helper line"), Callstack.Frames[0].LineNumber, Fixture.GetLine(TEXT("BreakpointHelperLine"))); // ★ 文件与行号语义有回归保障
```

新增对比结论：

- `UnLua` 当前仓内真正拥有的是“把 Lua/UE 运行时状态解释出来”的能力，而不是“定义断点该怎么生效”的能力。
- `Angelscript` DebugServer V2 的核心价值不只在能停下来，而在于它拥有文件名归一化、可执行行吸附、热重载后重放这些断点语义规则。
- 因而两者差异不只是“外部调试器 vs 内建调试器”，而是**源码定位和断点 bookkeeping 是否属于仓库本身**。

差距判断：

- `UnLua` 相对 `Angelscript` 这种仓内 `breakpoint canonicalization + reapply` 机制，属于 `没有实现同层断点管理语义`。
- `Angelscript` 相对 `UnLua` 暴露 `GetStackVariables()/GetLuaCallStack()` 这种纯观测 API，不是缺失，而是 `实现方式不同`；它把更多能力直接合进了协议栈。
- 如果 `Angelscript` 要吸收 `UnLua` 经验，值得吸收的是把“观测面”继续抽薄成独立 API；不值得回退的是断点 ownership。

### [维度 D10] 教学材料的驻留形态：`UnLua` 的教程是工程内真实资产，`Angelscript` 的多数示例是测试夹具

前几轮已经写过 `README -> Docs -> Tutorials` 的索引关系。这一轮补的是一个更容易被忽略、但对用户上手成本影响很大的差异：**教程最终是否作为真实工程资产驻留**。`UnLua` 的 `README.md` 不是只列一堆文档链接，而是把编号教程直接指向 `Content/Script/Tutorials/*.lua`；教程脚本本身又会写明绑定到哪个关卡蓝图或示例关卡；编辑器工具栏进一步在 Blueprint 资产上直接提供 `Bind`、`Create Lua Template`、`Reveal in Explorer`。换句话说，`UnLua` 的教学材料是**README 可点、Content Browser 可见、工具栏可操作**的真实资产链。

`Angelscript` 当前仓库的“示例”更像高质量测试夹具。`Examples/*.cpp` 里把示例脚本写成 C++ 字符串常量，`RunScriptExampleCompileTest()` 再通过 `CompileAnnotatedModuleFromMemory()` 把它编译成 `ScriptExamples/<Example>.as` 这种虚拟文件名；而 `Template_Blueprint.cpp` / `Template_BlueprintWorldTick.cpp` 创建的蓝图包路径是 `/Temp/...`，明确是 transient package。这样做对回归测试非常好，因为示例和断言永远绑在一起；但它并不构成 `UnLua` 那种“用户打开工程就能浏览并运行”的持久教程资产面。

```
[D10-Deep] Teaching Artifact Residency
UnLua
├─ README links numbered tutorials                 // 教程入口是稳定文件路径
├─ Tutorial script comments point to real maps     // 脚本直接说明绑定到哪个关卡/蓝图
├─ Blueprint toolbar exposes Bind/Create Template  // 编辑器里直接操作真实资产
└─ Teaching materials live in project content      // ★ 文档、脚本、资产三者同住工程

Angelscript
├─ Examples/*.cpp store script text as literals    // 示例主要驻留在测试源码
├─ CompileAnnotatedModuleFromMemory()              // 编译成虚拟 ScriptExamples/*
├─ Template_* create /Temp transient blueprints    // 资产默认是临时包
└─ Example corpus is optimized for regression      // ★ 更像测试夹具，不是常驻教程工程
```

关键源码 [1]：`UnLua` 的教程链是“真实文件 + 真实资产 + 真实工具入口”

```md
<!-- ============================================================================
文件: Reference/UnLua/README.md
函数: 更多示例
行号: 41-54
位置: README 直接把用户送进编号教程脚本，而不是送进测试源码
============================================================================ -->
# 更多示例
  * [01_HelloWorld](Content/Script/Tutorials/01_HelloWorld.lua) 快速开始的例子
  * [02_OverrideBlueprintEvents](Content/Script/Tutorials/02_OverrideBlueprintEvents.lua) 覆盖蓝图事件（Overridden Functions）
  * [03_BindInputs](Content/Script/Tutorials/03_BindInputs.lua) 输入事件绑定
  ...
  * [12_CustomLoader](Content/Script/Tutorials/12_CustomLoader.lua) 自定义加载器
  * [13_AnimNotify](Content/Script/Tutorials/AN_FootStep.lua) 动画通知
```

```lua
-- ============================================================================
-- 文件: Reference/UnLua/Content/Script/Tutorials/01_HelloWorld.lua
-- 函数: 文件头说明
-- 行号: 1-6
-- 位置: 教程脚本直接声明自己绑定到哪个关卡蓝图，说明它不是抽象片段，而是工程内真实课件
-- ============================================================================
--[[
    说明：在蓝图中实现UnLuaInterface接口，并通过 GetModuleName 指定脚本路径，即可绑定到Lua

    例如：
    本脚本由 "Content/Tutorials/01_HelloWorld/HelloWorld.map" 的关卡蓝图绑定
]]
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp
// 函数: FUnLuaEditorToolbar::BuildToolbar
// 行号: 59-72
// 位置: 教程动作不是文档里说说而已，而是直接出现在 Blueprint 资产的工具栏菜单
// ============================================================================
const auto BindingStatus = GetBindingStatus(Blueprint);
FMenuBuilder MenuBuilder(true, CommandList);
if (BindingStatus == NotBound)
{
    MenuBuilder.AddMenuEntry(Commands.BindToLua, NAME_None, LOCTEXT("Bind", "Bind"));
}
else
{
    MenuBuilder.AddMenuEntry(Commands.CopyAsRelativePath, NAME_None, LOCTEXT("CopyAsRelativePath", "Copy as Relative Path"));
    MenuBuilder.AddMenuEntry(Commands.RevealInExplorer, NAME_None, LOCTEXT("RevealInExplorer", "Reveal in Explorer"));
    MenuBuilder.AddMenuEntry(Commands.CreateLuaTemplate, NAME_None, LOCTEXT("CreateLuaTemplate", "Create Lua Template")); // ★ 直接在资产上生成教学/脚手架文件
    MenuBuilder.AddMenuEntry(Commands.UnbindFromLua, NAME_None, LOCTEXT("Unbind", "Unbind"));
}
```

关键源码 [2]：`Angelscript` 的多数示例驻留在测试源码和临时包，而不是常驻内容资产

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleActorTest.cpp
// 函数: GActorExample
// 行号: 9-23
// 位置: 示例脚本直接写成 C++ 字符串字面量，天然更接近测试夹具
// ============================================================================
const AngelscriptScriptExamples::FScriptExampleSource GActorExample = {
    TEXT("Example_Actor.as"),
    TEXT(R"ANGELSCRIPT(/*
 * Script classes can always derive from the same classes that
 * blueprints can be derived from.
 */

// For example, we can make a new Actor class
class AExampleActor_UnitTest : AActor
{
    UPROPERTY()
    int ExampleValue = 15;
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleTestSupport.cpp
// 函数: AngelscriptScriptExamples::RunScriptExampleCompileTest
// 行号: 16-58
// 位置: 示例被编译到虚拟路径 ScriptExamples/*，并在测试结束后丢弃 module
// ============================================================================
FAngelscriptEngine& Engine = AcquireCleanSharedCloneEngine();
const FName ModuleName(*ModuleNameString);
ON_SCOPE_EXIT
{
    Engine.DiscardModule(*ModuleName.ToString());                 // ★ 示例 module 是测试期临时对象
};

FString CombinedScriptCode;
CombinedScriptCode += Example.ScriptText;

const FString VirtualFileName = FString::Printf(TEXT("ScriptExamples/%s"), *ExampleFileName);
const bool bCompiled = CompileAnnotatedModuleFromMemory(&Engine, ModuleName, VirtualFileName, CombinedScriptCode); // ★ 文件名本身就是虚拟路径
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Template/Template_Blueprint.cpp
// 函数: CreateTransientBlueprintChild
// 行号: 21-44
// 位置: Blueprint 模板也默认生成到 /Temp transient package，强调它是测试工装而非长期教学资产
// ============================================================================
const FString PackagePath = FString::Printf(
    TEXT("/Temp/AngelscriptTemplate_%.*s_%s"),
    Suffix.Len(),
    Suffix.GetData(),
    *FGuid::NewGuid().ToString(EGuidFormats::Digits));
UPackage* BlueprintPackage = CreatePackage(*PackagePath);
BlueprintPackage->SetFlags(RF_Transient);                         // ★ 明确是临时包
```

新增对比结论：

- `UnLua` 教程体系的关键价值不只是“文档多”，而是教学材料长期驻留在工程内容层，用户可以沿着 README、脚本、关卡、工具栏一路点进去。
- `Angelscript` 当前示例体系的关键价值不只是“覆盖面广”，而是每个示例都天然可回归、可断言、可丢弃，更适合作为工程自测样本库。
- 因此这里的差异不是“有没有示例”，而是**示例的首要归属是教学资产还是测试夹具**。

差距判断：

- `Angelscript` 相对 `UnLua` 这种“真实内容资产驻留”的教程路径，属于 `没有实现同层常驻教程资产面`。
- `UnLua` 相对 `Angelscript` 这种“示例即测试夹具”的做法，属于 `实现方式不同`，不是示例质量不足。
- 如果 `Angelscript` 要补强 `D10`，优先级最高的不是再写更多示例字符串，而是把一部分稳定示例沉淀成用户可浏览、可运行、可复用的持久资产入口。

---

## 深化分析 (2026-04-08 23:58:03)

### [维度 D2] interface/delegate 的 ownership：`UnLua` 在属性桥即时拼装，`Angelscript` 在类型系统提前固化

前几轮把 `UnLua` 的“零胶水”主要放在 `UClass/UFunction` 主路径上看。这一轮往 `FInterfaceProperty`、`FDelegateProperty` 再下钻后，可以看到一个更精确的边界：`UnLua` 的 zero-glue 核心是“运行时拿 UE 反射对象现配桥”，不是“所有高级类型都天然成为脚本语言的一等公民”。`Angelscript` 则相反，它宁可在编译期/绑定期多做工作，也要把 interface / delegate 的形状提前固化进脚本类型系统。

```
[D2-Deep] Interface And Delegate Ownership
UnLua
├─ CreateTypeInterface() inspects Lua value/metatable // 先看 Lua 值长什么样
├─ GetFieldProperty(UField) synthesizes transient FProperty // 临时拼装 FProperty
├─ FInterfacePropertyDesc -> PushUObject / ImplementsInterface // interface 仍落回 UObject
└─ FDelegateRegistry -> clone delegate + cache ULuaDelegateHandler // delegate 运行时托管

Angelscript
├─ Preprocessor parses ", IMyInterface"              // 语法阶段记录接口
├─ ClassGenerator -> NewClass->Interfaces.Add(...)  // 生成期写入 UClass 接口表
├─ Verify required interface methods                // 缺方法直接编译报错
└─ Bind_Delegates -> ValueClass + BindUFunction     // 委托是脚本一等类型
```

关键源码 [1]：`UnLua` 先从 Lua 值反推出一个临时 `FProperty`，再决定桥接方式

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/PropertyRegistry.cpp
// 函数: FPropertyRegistry::CreateTypeInterface / GetFieldProperty
// 行号: 25-60, 316-405
// 位置: 不是预生成类型表，而是看到 Lua 值后再即时拼装对应的 UE Property 桥
// ============================================================================
TSharedPtr<ITypeInterface> FPropertyRegistry::CreateTypeInterface(lua_State* L, int32 Index)
{
    ...
    case LUA_TTABLE:
    {
        lua_pushstring(L, "__name");
        Type = lua_rawget(L, Index);
        if (Type == LUA_TSTRING)
        {
            const char* Name = lua_tostring(L, -1);
            auto ClassDesc = Env->GetClassRegistry()->Find(Name);
            if (ClassDesc)
            {
                TypeInterface = GetFieldProperty(ClassDesc->AsStruct());    // ★ 表里带 __name 就回推到 UE Field
            }
            else
            {
                auto EnumDesc = Env->GetEnumRegistry()->Find(Name);
                if (EnumDesc)
                    TypeInterface = GetFieldProperty(EnumDesc->GetEnum());
                else
                    TypeInterface = FindTypeInterface(lua_tostring(L, -1));
            }
        }
        ...
    }
}

TSharedPtr<ITypeInterface> FPropertyRegistry::GetFieldProperty(UField* Field)
{
    ...
    if (const auto Class = Cast<UClass>(Field))
    {
        const auto ObjectProperty = new FObjectProperty(PropertyCollector, Params);
        ObjectProperty->PropertyClass = Class;
        Property = ObjectProperty;                                        // ★ UClass -> FObjectProperty
    }
    else if (const auto ScriptStruct = Cast<UScriptStruct>(Field))
    {
        const auto StructProperty = new FStructProperty(PropertyCollector, Params);
        StructProperty->Struct = ScriptStruct;
        StructProperty->ElementSize = ScriptStruct->PropertiesSize;
        Property = StructProperty;                                        // ★ UStruct -> FStructProperty
    }
    else if (const auto Enum = Cast<UEnum>(Field))
    {
        const auto EnumProperty = new FEnumProperty(PropertyCollector, NAME_None, RF_Transient, 0, CPF_HasGetValueTypeHash, Enum);
        ...
        Property = EnumProperty;                                          // ★ UEnum -> FEnumProperty
    }

    const auto Ret = TSharedPtr<ITypeInterface>(FPropertyDesc::Create(Property));
```

关键源码 [2]：`UnLua` 的 interface / delegate 仍主要是“属性桥 + 运行时 handler”

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp
// 函数: FInterfacePropertyDesc::GetValueInternal / SetValueInternal / CheckPropertyType
// 行号: 541-580
// 位置: interface 在 Lua 侧没有独立类型对象，读写都回到 UObject + ImplementsInterface 检查
// ============================================================================
virtual void GetValueInternal(lua_State *L, const void *ValuePtr, bool bCreateCopy) const override
{
    ...
    const FScriptInterface &Interface = InterfaceProperty->GetPropertyValue(ValuePtr);
    UnLua::PushUObject(L, Interface.GetObject());                         // ★ interface 直接退化成 UObject 暴露
}

virtual bool SetValueInternal(lua_State *L, void *ValuePtr, int32 IndexInStack, bool bCopyValue) const override
{
    FScriptInterface *Interface = (FScriptInterface*)ValuePtr;
    UObject *Value = UnLua::GetUObject(L, IndexInStack);
    Interface->SetObject(Value);
    Interface->SetInterface(Value ? Value->GetInterfaceAddress(InterfaceProperty->InterfaceClass) : nullptr);
    return true;                                                          // ★ 写回时才补 InterfaceAddress
}

if ((Class) && (!Class->ImplementsInterface(InterfaceProperty->InterfaceClass)))
{
    ErrorMsg = FString::Printf(TEXT("implements of interface %s is needed but got nil for object %s"),
        *InterfaceProperty->InterfaceClass->GetName(), *Class->GetName()); // ★ 约束发生在运行时类型检查
    return false;
}
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/DelegateRegistry.cpp
// 函数: FDelegateRegistry::Register / Bind / Add
// 行号: 85-97, 168-191, 237-261
// 位置: delegate 并非“直接把 Lua 函数塞进 UE”，而是注册、克隆并缓存原生 handler
// ============================================================================
FScriptDelegate* FDelegateRegistry::Register(FScriptDelegate* Delegate, FDelegateProperty* Property)
{
    auto Cloned = new FScriptDelegate();
    *Cloned = *Delegate;                                                  // ★ 先克隆一个原生 delegate 存档
    FDelegateInfo NewInfo;
    NewInfo.SignatureFunction = CastField<FDelegateProperty>(Property)->SignatureFunction;
    NewInfo.bDeleteOnRemove = true;
    Delegates.Add(Cloned, NewInfo);
    return Cloned;
}

void FDelegateRegistry::Bind(lua_State* L, int32 Index, FScriptDelegate* Delegate, UObject* SelfObject)
{
    const auto LuaFunction = lua_topointer(L, Index);
    const auto DelegatePair = FLuaDelegatePair(SelfObject, LuaFunction);
    ...
    const auto Handler = CreateHandler(Ref, Info.Owner.Get(), SelfObject);
    Handler->BindTo(Delegate);                                            // ★ 真正绑定的是 ULuaDelegateHandler
    Env->AutoObjectReference.Add(Handler);
    CachedHandlers.Add(DelegatePair, Handler);                            // ★ 同一个 object + lua closure 会复用 handler
    Info.Handlers.Add(Handler);
}

void FDelegateRegistry::Add(lua_State* L, int32 Index, void* Delegate, UObject* SelfObject)
{
    ...
    const auto Handler = CreateHandler(Ref, Info.Owner.Get(), SelfObject);
    Env->AutoObjectReference.Add(Handler);
    Handler->AddTo(Info.MulticastProperty, Delegate);                     // ★ multicast 也是运行时 add/remove，而不是静态声明
    CachedHandlers.Add(DelegatePair, Handler);
    Info.Handlers.Add(Handler);
}
```

关键源码 [3]：`Angelscript` 把 interface / delegate 提前写进脚本类型系统

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp
// 函数: class/interface 继承子句解析片段
// 行号: 800-820
// 位置: 脚本里的 implements 信息在预处理阶段就进入 ClassDesc
// ============================================================================
if (Chunk.Type == EChunkType::Class || Chunk.Type == EChunkType::Interface)
{
    ...
    for (int32 i = 1; i < InheritanceList.Num(); ++i)
    {
        FString InterfaceName = InheritanceList[i].TrimStartAndEnd();
        if (InterfaceName.Len() > 0)
        {
            ClassDesc->ImplementedInterfaces.Add(InterfaceName);          // ★ 不是运行时猜测，而是语法期显式记录
        }
    }
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 函数: interface 写入与校验片段
// 行号: 5111-5183
// 位置: 生成期递归补齐接口表，并验证实现类是否真的提供了必需函数
// ============================================================================
TFunction<void(UClass*)> AddInterfaceRecursive = [&](UClass* InterfaceClass)
{
    ...
    FImplementedInterface ImplementedInterface;
    ImplementedInterface.Class = InterfaceClass;
    ImplementedInterface.PointerOffset = 0;
    ImplementedInterface.bImplementedByK2 = true;
    NewClass->Interfaces.Add(ImplementedInterface);                       // ★ 直接写入 UClass::Interfaces
};

for (const FString& InterfaceName : ClassDesc->ImplementedInterfaces)
{
    UClass* InterfaceClass = ResolveInterfaceClass(InterfaceName);
    if (InterfaceClass != nullptr && InterfaceClass->HasAnyClassFlags(CLASS_Interface))
    {
        AddInterfaceRecursive(InterfaceClass);
    }
    else
    {
        ModuleData.NewModule->bModuleSwapInError = true;                  // ★ 非法接口直接阻止 swap-in
    }
}

for (const FImplementedInterface& Impl : NewClass->Interfaces)
{
    ...
    UFunction* ImplFunc = NewClass->FindFunctionByName(InterfaceFunc->GetFName());
    ...
    if (ImplFunc == nullptr || bResolvedToInterfaceStub)
    {
        FAngelscriptEngine::Get().ScriptCompileError(...);                // ★ 缺少接口方法时编译期报错
        ModuleData.NewModule->bModuleSwapInError = true;
    }
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.cpp
// 函数: DeclareDelegateOperations / Bind_Delegates / FAngelscriptDelegateOperations::BindUFunction
// 行号: 439-452, 508-566, 1389-1441
// 位置: delegate 作为脚本 value class 注册，并在绑定时验证签名
// ============================================================================
FAngelscriptType::Register(MakeShared<FScriptDelegateType>(Decl, Function));

auto Delegate_ = FAngelscriptBinds::ValueClass<FScriptDelegate>(Decl, BindFlags);
Delegate_.Constructor("void f()", FUNC_TRIVIAL(FAngelscriptDelegateOperations::Construct));
Delegate_.Destructor("void f()", FUNC_TRIVIAL(FAngelscriptDelegateOperations::Destruct));
Delegate_.Constructor(CopyDecl, FUNC_TRIVIAL(FAngelscriptDelegateOperations::CopyConstruct));
Delegate_.Method(AssignDecl, FUNC_TRIVIAL(FAngelscriptDelegateOperations::Assign)); // ★ 委托先成为脚本一等值类型

if (!CheckAngelscriptDelegateCompatibility(Signature, CallFunction))
{
    FString Message = FString::Printf(TEXT("Specified function is not compatible with delegate function.\n\nDelegate: %s\n\nAttempted Bind: %s"),
        *GetSignatureStringForFunction(Signature), *GetSignatureStringForFunction(CallFunction));
    FAngelscriptEngine::Throw(TCHAR_TO_ANSI(*Message));                   // ★ 绑定时给出签名不兼容错误
    return;
}

auto Delegate_ = FAngelscriptBinds::ExistingClass("_FScriptDelegate");
Delegate_.Method("void BindUFunction(UObject Object, const FName& FunctionName, UDelegateFunction Signature)",
    FUNC(FAngelscriptDelegateOperations::BindUFunction_Signature));       // ★ 脚本侧 API 直接围绕 delegate 类型展开
```

新增对比结论：

- `UnLua` 的 zero-glue 在 interface / delegate 上更接近“运行时属性桥自动适配”，不是“把 interface / delegate 变成编译期脚本类型”。
- `Angelscript` 在这两个点上明显更重，但收益是接口实现完整性、delegate 签名兼容性和脚本 API 形状都能更早被确认。
- 这里真正的差异不是“支不支持 interface / delegate”，而是**类型 ownership 在 property marshalling 层，还是在 class/type system 层**。

差距判断：

- `Angelscript` 相对 `UnLua` 这种“运行时临时拼装 `FProperty` 再桥接”的路径，属于 `实现方式不同`，不是没有反射能力。
- `UnLua` 相对 `Angelscript` 这种“接口方法完备性校验 + delegate 一等类型注册”的路径，属于 `没有实现同层前置类型约束`。
- 就 interface / delegate 这一小层而言，`Angelscript` 的约束完整度属于 `实现质量差异`，因为错误更早暴露，且直接挂在编译/生成流程里。

### [维度 D4] 类型形状差分的 ownership：`UnLua` 把结构风险留给业务，`Angelscript` 把 reload policy 做成显式判定

前几轮已经覆盖了 watcher、批次和多 env fan-out。这一轮补的是更深一层的问题：**脚本改动如果已经触到 class/property/function signature 这种“类型形状变化”，是谁来决定还能不能安全热更**。`UnLua` 的答案在 `HotReload.lua` 文件头就写得很直白：它尽量替换函数和 upvalue，但“原表改变、类型改变”由业务保证正确，最坏情况是重启。`Angelscript` 的答案则是把差异分析做进 `ClassGenerator`，显式把结果分成 `SoftReload / FullReloadSuggested / FullReloadRequired / Error`。

```
[D4-Deep] Shape-Change Reload Policy
UnLua
├─ track loaded_module_times                       // 只先看模块文件是否变了
├─ sandbox.load()/xpcall()                        // 在 Lua 沙盒里重载模块
├─ match_module()/match_upvalues()                // 对比函数与 upvalue
├─ update_global() patches live tables            // 回写运行中的 table / closure
└─ table/type shape correctness left to game code // ★ 结构风险由业务兜底

Angelscript
├─ AnalyzeReloadFromMemory()                      // 先算 reload requirement
├─ property/super/signature/meta deltas           // 类型形状变化单独分类
├─ ShouldFullReload()                             // 决定 soft 还是 full
├─ CreateFullReloadClass / LinkSoftReloadClasses  // 执行不同路径
└─ tests assert reload requirement per delta      // ★ 差分策略有回归测试
```

关键源码 [1]：`UnLua` 的 hot reload 目标是“尽量替换”，并明确把类型变化风险留给业务

```lua
-- ============================================================================
-- 文件: Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua
-- 函数: 文件头注释 / reload_modules / M.reload
-- 行号: 1-7, 553-624
-- 位置: HotReload 的核心承诺是替换函数和 upvalue，不是分析 UE 类型形状差异
-- ============================================================================
------------------------------------------
--- HotFix - 运行时HotFix支持 参考云风方案
--- 替换运行环境中的函数，保持upvalue和运行时的table
--- 为开发期设计，尽量替换。最差的结果就是重新启动
--- 原表改变，类型改变 后逻辑由业务保证正确
--- 基于lua 5.3，重定向require，使用env加载实现沙盒
------------------------------------------

local function reload_modules(module_names)
    ...
    sandbox.enter(tmp_modules)
    ...
    local func, env = sandbox.load(module_name)
    if func ~= nil then
        local ok, new_module = xpcall(func, load_error_handler)
        if not ok then
            sandbox.exit()
            return                                              -- ★ 失败就直接中止这轮 reload
        end
        ...
        old_modules[#old_modules+1] = loaded_modules[module_name]
        new_modules[#new_modules+1] = new_module
    end
    ...
    update_modules(old_modules, new_modules, module_envs)       -- ★ 核心是替换 table / function / upvalue 图
    sandbox.exit()
end

function M.reload(module_names)
    if module_names then
        reload_modules(module_names)
        return
    end

    local modified_modules = {}
    for module_name, time in pairs(loaded_module_times) do
        if not ignore_modules[module_name] then
            local current_time = get_last_modified_time(module_name)
            if current_time ~= time then
                modified_modules[#modified_modules + 1] = module_name
                loaded_module_times[module_name] = current_time
            end
        end
    end

    if #modified_modules > 0 then
        reload_modules(modified_modules)                        -- ★ 入口粒度是“变更 module 列表”
    end
end
```

关键源码 [2]：`UnLua` 的真正补丁单位是函数、upvalue 和全局引用图

```lua
-- ============================================================================
-- 文件: Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua
-- 函数: update_modules / update_global
-- 行号: 367-455, 480-549
-- 位置: 热更成功后的动作是把新旧 module 的函数和 upvalue 映射回正在运行的对象图
-- ============================================================================
local function update_modules(old_modules, new_modules, new_envs)
    ...
    moduleres.values = match_module(new_module_info, old_module)          -- ★ 先找旧函数 -> 新函数映射
    ...
    moduleres.upvalue_map = match_upvalues(moduleres.values, old_module_upvalues) -- ★ 再找 upvalue 替换关系
    ...
    merge_objects(result)
    ...
    update_global(all_value_maps)                                         -- ★ 最后回写全局 / 栈 / userdata
end

local function update_global(value_map)
    ...
    local function update_running_stack(co, level)
        ...
        local nv = value_map[v]
        if nv then
            debug.setlocal(co, level + 1, i, nv)                          -- ★ 连运行栈里的 local 都会被替换
            update_table(nv)
        else
            update_table(v)
        end
        ...
    end

    function update_table(root)
        ...
        elseif t == "userdata" then
            local user_value = debug.getuservalue(root)
            if user_value then
                local nv = value_map[user_value]
                if nv then
                    debug.setuservalue(root, user_value)                  -- ★ userdata 也按 uservalue 回写
                    update_table(nv)
                else
                    update_table(user_value)
                end
            end
        end
    end
end
```

关键源码 [3]：`Angelscript` 把类型形状变化转成显式 `ReloadReq`，并用测试锁住判定语义

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 函数: FAngelscriptClassGenerator::Setup / ShouldFullReload / CreateFullReloadClass
// 行号: 1077-1088, 1300-1330, 2144-2178
// 位置: superclass / property / metadata / 新类等变化会抬高 reload requirement，再分流到 soft/full reload
// ============================================================================
if (PrevStruct != nullptr)
{
    if (ClassData.OldClass->SuperClass != ClassData.NewClass->SuperClass)
    {
        if (ClassData.ReloadReq < EReloadRequirement::FullReloadRequired)
            ClassData.ReloadReq = EReloadRequirement::FullReloadRequired; // ★ 父类变更直接要求 full reload
    }

    // Check if any properties from the old class have been
    // removed or changed type.
    for (auto OldPropertyDesc : ClassData.OldClass->Properties)           // ★ 属性形状变化也会升级 ReloadReq
    {
        ...
    }
}

if (!ClassData.NewClass->Meta.OrderIndependentCompareEqual(ClassData.OldClass->Meta))
{
    if (ClassData.ReloadReq < EReloadRequirement::FullReloadSuggested)
        ClassData.ReloadReq = EReloadRequirement::FullReloadSuggested;    // ★ metadata 变化至少建议 full reload
}
...
else
{
    if (ClassData.ReloadReq < EReloadRequirement::FullReloadSuggested)
        ClassData.ReloadReq = EReloadRequirement::FullReloadSuggested;    // ★ brand-new class 也不是简单 soft reload
}

for (auto& ClassData : ModuleData.Classes)
{
    if (ShouldFullReload(ClassData))
    {
        if (ClassData.NewClass->bIsStruct)
            CreateFullReloadStruct(ModuleData, ClassData);
        else
            CreateFullReloadClass(ModuleData, ClassData);                 // ★ full reload 路径
    }
    else
    {
        LinkSoftReloadClasses(ModuleData, ClassData);                     // ★ soft reload 路径
    }
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadAnalysisTests.cpp
// 函数: Reload 分析回归测试
// 行号: 123-134, 166-174, 259-270, 304-315, 354-365
// 位置: 属性数、父类、增删类、函数签名变化分别对应不同 ReloadRequirement
// ============================================================================
const bool bAnalyzed = AnalyzeReloadFromMemory(&Engine, TEXT("ReloadPropertyMod"), TEXT("ReloadPropertyMod.as"),
    ScriptV2, ReloadRequirement, bWantsFullReload, bNeedsFullReload);
TestTrue(TEXT("Property count change should request a full reload path"), bWantsFullReload || bNeedsFullReload);

const bool bAnalyzed = AnalyzeReloadFromMemory(&Engine, TEXT("ReloadSuperMod"), TEXT("ReloadSuperMod.as"),
    ScriptV2, ReloadRequirement, bWantsFullReload, bNeedsFullReload);
TestEqual(TEXT("Super-class change should require a full reload"),
    ReloadRequirement, FAngelscriptClassGenerator::FullReloadRequired);

const bool bAnalyzed = AnalyzeReloadFromMemory(&Engine, TEXT("ReloadClassAddedMod"), TEXT("ReloadClassAddedMod.as"),
    ScriptV2, ReloadRequirement, bWantsFullReload, bNeedsFullReload);
TestEqual(TEXT("Class add should suggest a full reload"),
    ReloadRequirement, FAngelscriptClassGenerator::FullReloadSuggested);

const bool bAnalyzed = AnalyzeReloadFromMemory(&Engine, TEXT("ReloadClassRemovedMod"), TEXT("ReloadClassRemovedMod.as"),
    ScriptV2, ReloadRequirement, bWantsFullReload, bNeedsFullReload);
TestEqual(TEXT("Class remove should require a full reload"),
    ReloadRequirement, FAngelscriptClassGenerator::FullReloadRequired);

const bool bAnalyzed = AnalyzeReloadFromMemory(&Engine, TEXT("ReloadFunctionMod"), TEXT("ReloadFunctionMod.as"),
    ScriptV2, ReloadRequirement, bWantsFullReload, bNeedsFullReload);
TestEqual(TEXT("Function signature change should require a full reload"),
    ReloadRequirement, FAngelscriptClassGenerator::FullReloadRequired);
```

新增对比结论：

- `UnLua` 的 hot reload 更像 Lua 世界的 live object graph patcher；它很擅长保住 closure、upvalue 和 table identity，但对“反射形状是否还能安全换入”没有同层分析器。
- `Angelscript` 的 hot reload 更像一个显式的结构差分器；它先判断 delta 属于 soft 还是 full，再决定怎么 link / reinstance。
- 因而这不是简单的“require 刷新 vs 全量重编译”，而是**谁拥有 type-shape risk assessment** 的差异。

差距判断：

- `UnLua` 相对 `Angelscript` 这种 `ReloadRequirement` 分层与回归测试，属于 `没有实现同层热重载差分判定器`。
- `Angelscript` 相对 `UnLua` 这种函数/upvalue/table 级 live patch，属于 `实现方式不同`，不是没有热更能力。
- 如果 `Angelscript` 要吸收 `UnLua` 经验，最值得吸收的是 body-only 改动时对 closure identity 的保留策略；如果 `UnLua` 要吸收 `Angelscript` 经验，最值得补的是结构差分和 machine-checkable reload requirement。

### [维度 D9] CI 契约的交付层级：`UnLua` 交付 UE Automation 测试名，`Angelscript` 交付退出码、错误开关和覆盖率产物

前几轮已经讲过两边都有不少测试。这一轮补的是**CI 真正能消费什么**。从仓内源码看，`UnLua` 交付的是一批用 `IMPLEMENT_SIMPLE_AUTOMATION_TEST` 暴露出来的 Automation tests，再辅以 latent command 基础设施；也就是说，仓内最稳定的契约是“测试名可被 UE Automation 发现”。`Angelscript` 在这之上又补了一层：commandlet 退出码、`as-exit-on-error` 命令行失败策略、以及覆盖率报告输出。这让它的 CI 契约不再只是一组测试用例，而是**可无人值守执行的 verdict + artifact**。这里关于“`UnLua` 的 runner 编排主要留给仓外 UE Automation 宿主”的判断，是基于当前源码交付物形态的推断，不是对外部流水线脚本的断言。

```
[D9-Deep] CI Contract Ownership
UnLua
├─ IAutomationLatentCommand helpers                // 测试执行细节靠 latent command
├─ IMPLEMENT_SIMPLE_AUTOMATION_TEST(...)          // 对外契约是可发现的 test name
└─ host runner decides exit code / reporting      // ★ CI 编排仍依赖仓外 UE Automation 宿主

Angelscript
├─ UAngelscriptTestCommandlet::Main()             // 仓内直接返回 0/1/2/3
├─ as-exit-on-error                               // 编译失败可强制退出
├─ HotReloadTestRunner                            // 热重载后还能选择性跑受影响测试
└─ CodeCoverage -> Saved/CodeCoverage             // ★ 覆盖率产物也由仓内生成
```

关键源码 [1]：`UnLua` 测试基础设施的最终交付物是 Automation test registration

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Public/UnLuaTestCommon.h
// 函数: FUnLuaTestCommand_* / IMPLEMENT_UNLUA_LATENT_TEST / IMPLEMENT_TESTSUITE_TEST
// 行号: 25-40, 172-212
// 位置: 测试框架围绕 UE Automation latent command 和 IMPLEMENT_SIMPLE_AUTOMATION_TEST 宏展开
// ============================================================================
class FUnLuaTestCommand_WaitOneTick : public IAutomationLatentCommand
{
public:
    virtual bool Update() override;                                       // ★ 运行模型建立在 UE Automation latent command 之上
};

#define IMPLEMENT_UNLUA_LATENT_TEST(TestClass, PrettyName) \
IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass##_Runner, PrettyName, \
    (EAutomationTestFlags::ClientContext | EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)) \
bool TestClass##_Runner::RunTest(const FString& Parameters) \
{ \
    TestClass* TestInstance = new TestClass(); \
    TestInstance->SetTestRunner(*this); \
    ADD_LATENT_AUTOMATION_COMMAND(FUnLuaTestCommand_SetUpTest(TestInstance)); \
    ADD_LATENT_AUTOMATION_COMMAND(FUnLuaTestCommand_PerformTest(TestInstance)); \
    ADD_LATENT_AUTOMATION_COMMAND(FUnLuaTestCommand_TearDownTest(TestInstance)); \
    return true;                                                          // ★ 对外暴露的是 Automation test，本身不定义 CI exit contract \
}

#define IMPLEMENT_TESTSUITE_TEST(TestClass, PrettyName) \
IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, PrettyName, \
    (EAutomationTestFlags::ClientContext | EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter))
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Tests/BindingTest.cpp
// 函数: FUnLuaTest_StaticBinding::RunTest / FUnLuaTest_Overridden::RunTest
// 行号: 51-74, 157-176
// 位置: 具体测试以 Automation case 名称注册，断言结果仍留给 UE Automation 框架消费
// ============================================================================
bool FUnLuaTest_StaticBinding::RunTest(const FString& Parameters)
{
    Run([this](lua_State* L, UWorld* World)
    {
        ...
        const auto Error = lua_tostring(L, -1);
        TEST_EQUAL(Error, "");                                            // ★ 断言落回 Automation test runner
    });

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnLuaTest_Overridden,
    TEXT("UnLua.API.Binding.Overridden 覆写：同一个Lua脚本绑定到不同类"),
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter);

bool FUnLuaTest_Overridden::RunTest(const FString& Parameters)
{
    ...
    const auto Actual = lua_tostring(L, -1);
    TEST_EQUAL(Actual, "BP ABC Lua");                                     // ★ 可被 CI 消费的是 test name + pass/fail
    return true;
}
```

关键源码 [2]：`Angelscript` 仓内直接定义了 machine-readable 的测试 verdict

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptTestCommandlet.cpp
// 函数: UAngelscriptTestCommandlet::Main
// 行号: 5-24
// 位置: commandlet 把初编失败 / 单元测试失败 / 未初始化 struct 成员分成不同退出码
// ============================================================================
int32 UAngelscriptTestCommandlet::Main(const FString& Params)
{
    if (!FAngelscriptEngine::Get().bDidInitialCompileSucceed)
    {
        return 1;                                                         // ★ 编译失败
    }

    if (!RunAngelscriptUnitTests(FAngelscriptEngine::Get().GetActiveModules(), &FAngelscriptEngine::Get(), 0, 0))
    {
        return 2;                                                         // ★ 测试失败
    }

#if WITH_EDITOR
    if (FStructUtils::AttemptToFindUninitializedScriptStructMembers() != 0)
    {
        return 3;                                                         // ★ 结构体未初始化成员
    }
#endif

    return 0;                                                             // ★ 成功
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: FAngelscriptRuntimeConfig::Create / Compile path
// 行号: 520-528, 2099-2105, 2481-2489
// 位置: command line 可以要求“编译失败立即退出”，热重载后还能预编排要跑的测试集合
// ============================================================================
Config.bExitOnError = FParse::Param(FCommandLine::Get(), TEXT("as-exit-on-error")); // ★ CI 可用命令行开关
...

if (!bSuccess && (RuntimeConfig.bRunningCommandlet || RuntimeConfig.bExitOnError))
{
    UE_LOG(Angelscript, Error, TEXT("Cannot run when angelscript has failed to compile. Requesting exit."));
    FPlatformMisc::RequestExit(true);                                      // ★ 编译失败时主动退出，避免后续步骤继续污染结果
}

if (GEngine && bCompletedAssetScan && HotReloadTestRunner != nullptr
    && HotReloadTestRunner->ShouldRunUnitTestsOnHotReload())
{
    ...
    HotReloadTestRunner->PrepareTests(GetActiveModules(), CompiledModules, RelativeFileList,
        ShouldUseAutomaticImportMethod());                                 // ★ 热重载后还能预选受影响测试
}
```

关键源码 [3]：`Angelscript` 把覆盖率报告也收进仓内测试契约

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/AngelscriptCodeCoverage.cpp
// 函数: AddTestFrameworkHooks / CoverageEnabled / StopRecordingAndWriteReport
// 行号: 22-29, 31-49, 59-65
// 位置: 覆盖率记录与 AutomationController 对接，并可由命令行打开
// ============================================================================
void FAngelscriptCodeCoverage::AddTestFrameworkHooks()
{
    IAutomationControllerModule& AutomationModule =
        FModuleManager::LoadModuleChecked<IAutomationControllerModule>("AutomationController");
    IAutomationControllerManagerRef AutomationController = AutomationModule.GetAutomationController();
    AutomationController->OnTestsAvailable().AddRaw(this, &FAngelscriptCodeCoverage::OnTestsStarting);
    AutomationController->OnTestsComplete().AddRaw(this, &FAngelscriptCodeCoverage::OnTestsStopping); // ★ 测试开始/结束自动钩住覆盖率
}

bool FAngelscriptCodeCoverage::CoverageEnabled()
{
    return (GetDefault<UAngelscriptTestSettings>()->bEnableCodeCoverage ||
            FParse::Param(FCommandLine::Get(), TEXT("as-enable-code-coverage"))); // ★ 命令行可直接给 CI 打开覆盖率
}

void FAngelscriptCodeCoverage::StopRecordingAndWriteReport(const FString& OutputDir)
{
    bRecording = false;
    WriteReportHtml(OutputDir);
    WriteCoverageSummaries(OutputDir);                                    // ★ 输出覆盖率 artifact 到 Saved/CodeCoverage
}
```

新增对比结论：

- `UnLua` 当前仓内更像“把测试内容交给 UE Automation 发现”，而不是“把 CI 判定逻辑和产物格式也一并交付”。
- `Angelscript` 当前仓内已经把 test verdict、compile-fail exit policy、hot reload 后的 test selection、coverage artifact 一起定义出来了。
- 这意味着两者在 `D9` 上的真正差异不是“有没有测试”，而是**仓库是否自己拥有 machine-consumable CI contract**。

差距判断：

- `UnLua` 相对 `Angelscript` 的 commandlet exit code、`as-exit-on-error`、coverage artifact 这一层，属于 `没有实现同层 CI 契约与测试产物交付`。
- `Angelscript` 相对 `UnLua` 这种纯 UE Automation test registration，属于 `实现方式不同`；它换来了更强的无人值守能力，也承担了更重的 runtime/test infrastructure 维护成本。
- 从 CI 友好度看，这里可以判作 `实现质量差异`，因为 `Angelscript` 给出了更稳定的机器接口，而 `UnLua` 当前快照更多依赖仓外 runner 编排。

---

## 深化分析 (2026-04-09 00:10:14)

### [维度 D2] 调用期参数桥的 ownership：`UnLua` 在每次 `UFunction` 调用上重跑 `FPropertyDesc` 解释器，`Angelscript` 更依赖已固化的签名与参数缓冲

前几轮的 `D2` 更偏“怎么暴露类型”。这一轮补的是**暴露之后怎么真正调用**。`UnLua` 的零胶水主路径并没有消除调用期开销，而是把它集中进 `FFunctionDesc`：每次 `CallUE()` 都要做 `self` 解析、对象合法性检查、默认参数填充、latent 注入、`out/ref` 回写。`Angelscript` 则把主路径拆成更前置的签名固化和更后置的最小 copy-back：`BlueprintEvent/Mixin` 先用 `FScriptCall` 构造连续参数缓冲，再在 `ProcessEvent()` 前验证参数形状；只有 reflective fallback 才回到更通用的 `FProperty` copy 模式。

```
[D2-Deep] Call-Site Marshaling Ownership
UnLua
├─ CallUE()                                         // 每次调用先决定 self / local / remote
│  ├─ CheckObject()                                 // 运行时 self/lifetime/type 校验
│  ├─ PreCall()                                     // 逐参数写入、默认值、latent 注入
│  ├─ ProcessEvent() / CallRemoteFunction()
│  └─ PostCall()                                    // 逐个 return/out copy back
└─ CallLuaInternal()                                // 覆写路径同样逐属性读写 Lua 栈

Angelscript
├─ FScriptCall::PushArgument()                      // 先把参数按已知 type 排入连续缓冲
├─ ValidateAgainstFunction()                        // 执行前检查 signature / buffer size
├─ ProcessEvent()
├─ ResetArgumentsAndCopyBackReferences()            // 只回写 mutable ref
└─ ReflectiveFallback                               // 通用路径才做 FProperty copy/copy-back
```

关键源码 [1]：`UnLua` 把调用适配成本放在 `FFunctionDesc` 的逐属性解释器中

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp
// 函数: FFunctionDesc::CallUE / PreCall / PostCall / CheckObject
// 行号: 171-235, 279-345, 352-409, 531-575
// 位置: 每次 Lua -> UE 调用都会重新解释属性列表，并在前后做对象/参数整理
// ============================================================================
int32 FFunctionDesc::CallUE(lua_State *L, int32 NumParams, void *Userdata)
{
    ...
    Object = bStaticFunc ? Function->GetOuterUClass()->GetDefaultObject() : UnLua::GetUObject(L, 1, false);
    if (UNLIKELY(!CheckObject(Object, Error)))
        return luaL_error(L, TCHAR_TO_UTF8(*Error));                     // ★ self/lifetime/type 在调用时兜底

    const auto Params = Buffer->Get();
    PreCall(L, NumParams, FirstParamIndex, CleanupFlags, Params, Userdata); // ★ 逐参数准备
    ...
    Object->UObject::ProcessEvent(FinalFunction, Params);
    ...
    int32 NumReturnValues = PostCall(L, NumParams, FirstParamIndex, Params, CleanupFlags); // ★ 逐参数回写
    Buffer->Pop(Params);
    return NumReturnValues;
}

void FFunctionDesc::PreCall(lua_State* L, int32 NumParams, int32 FirstParamIndex, FFlagArray& CleanupFlags, void* Params, void* Userdata)
{
    ...
    if (i == LatentPropertyIndex)
    {
        FLatentActionInfo LatentActionInfo(...);
        Property->CopyValue(ContainerPtr, &LatentActionInfo);            // ★ latent 参数由桥接层注入
        continue;
    }
    if (ParamIndex < NumParams)
        CleanupFlags[i] = Property->WriteValue_InContainer(L, Params, FirstParamIndex + ParamIndex, false);
    else if (!Property->IsOutParameter() && DefaultParams)
        Property->CopyValue(Params, ValuePtr);                           // ★ 缺省实参在调用期补齐
}

int32 FFunctionDesc::PostCall(lua_State * L, int32 NumParams, int32 FirstParamIndex, void* Params, const FFlagArray& CleanupFlags)
{
    ...
    if (ReturnPropertyIndex > INDEX_NONE)
        Property->ReadValue_InContainer(L, Params, true);                // ★ return/out 也在调用后逐项回写
    ...
}

bool FFunctionDesc::CheckObject(UObject* Object, FString& Error) const
{
    if (Object == nullptr || Object->IsUnreachable() || Object->HasAnyFlags(RF_NeedInitialization))
        return false;
    ...
    Error = FString::Printf(TEXT("attempt to call UFunction '%s' on invalid self type. '%s' required but got '%s'."),
                            *FuncName, *TargetClass->GetName(), *Object->GetClass()->GetName());
    return false;                                                        // ★ 最终还是运行时 self-type 校验
}
```

关键源码 [2]：`Angelscript` 在事件主路径上先验证签名，再执行 `ProcessEvent()`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp
// 函数: FScriptCall::ValidateAgainstFunction / PushArgument / ResetArgumentsAndCopyBackReferences / ExecuteEvent
// 行号: 104-123, 125-163, 183-191, 200-245, 270-284
// 位置: 参数布局、签名校验和 ref 回写被拆成显式阶段
// ============================================================================
SCRIPTCALL_INLINE bool ValidateAgainstFunction(const UFunction* Function, FString& OutErrorMessage) const
{
    ...
    if (!DoesCallArgumentMatchProperty(ArgumentTypes[PropertyIndex].Type, *It))
        return false;                                                    // ★ 先检查参数形状
    ...
    if (Function->ParmsSize != ArgumentOffset)
        return false;                                                    // ★ 再检查缓冲区大小
    return true;
}

template<bool TCheckErrors = true, bool TCopyInitialValue = true>
SCRIPTCALL_INLINE void PushArgument(FAngelscriptTypeUsage& Type, void* ValueRef)
{
    ...
    Type.ConstructValue(StoredPtr);
    if (Type.bIsReference)
        Type.CopyValue(OrigValueRef, StoredPtr);                         // ★ 主路径依赖已知 type 的 CopyValue
    else
        Type.CopyValue(ValueRef, StoredPtr);
}

SCRIPTCALL_INLINE void ResetArgumentsAndCopyBackReferences()
{
    if (ArgType.Type.bIsReference && !ArgType.Type.bIsConst)
        ArgType.Type.CopyValue(StoredPtr, ArgType.Reference);            // ★ 调用后只回写 mutable ref
}

SCRIPTCALL_INLINE void ExecuteEvent(UObject* Object, FName EventName)
{
    if (!ValidateAgainstFunction(Function, ValidationError))
    {
        AbortExecution(ValidationError);                                  // ★ 进 ProcessEvent 前就挡住错误
        return;
    }
    Object->ProcessEvent(Function, &ArgumentBuffer[0]);
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp
// 函数: CallBlueprintCallableReflectiveFallback
// 行号: 302-370
// 位置: fallback 路径保留通用性，但通用成本只留在 fallback 分支
// ============================================================================
for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
{
    ...
    void* SourceAddress = ResolveScriptArgumentAddress(Property, ScriptArgumentAddress);
    Property->CopySingleValue(Destination, SourceAddress);               // ★ fallback 才回到 FProperty copy
    if (Property->HasAnyPropertyFlags(CPF_ReferenceParm) && !Property->HasAnyPropertyFlags(CPF_ConstParm))
        OutReferences[OutReferenceCount++] = { Property, SourceAddress };
}

TargetObject->ProcessEvent(Function, ParameterBuffer);

for (int32 OutReferenceIndex = 0; OutReferenceIndex < OutReferenceCount; ++OutReferenceIndex)
{
    OutReference.Property->CopySingleValue(
        OutReference.ScriptValue,
        OutReference.Property->ContainerPtrToValuePtr<void>(ParameterBuffer)); // ★ 仅做 ref/out copy-back
}
```

新增对比结论：

- `UnLua` 的“零胶水”在调用期并不免费；它换来的是一个统一的逐属性解释器。
- `Angelscript` 的主路径把更多约束前移到 bind/class generation 阶段，因此调用期更像执行“已验证的参数缓冲”。
- 这不是“谁更自动”的问题，而是**谁在运行时承担参数形状适配成本**。

差距判断：

- 在“统一的通用 `UFunction` 调用解释器”这一点上，`Angelscript` 相对 `UnLua` 属于 `实现方式不同`，不是没有参数桥，而是拆成了 `prevalidated event buffer + reflective fallback`。
- 在“调用前显式拒绝 signature/buffer mismatch”这一点上，`UnLua` 相对 `Angelscript` 可以判作 `实现质量差异`，因为 `UnLua` 更多依赖运行时检查与日志，而 `Angelscript` 主路径直接在 `ProcessEvent()` 前终止。

### [维度 D5] 诊断载荷的 ownership：`UnLua` 默认交付日志字符串，`Angelscript` 交付结构化 `Diagnostics` 消息与停止事件

前面的 `D5` 已经区分了“外部 Lua 调试器”与“内建 DebugServer”。这一轮补的是**错误到底以什么形状离开 runtime**。`UnLua` 默认路径是把 Lua 错误扩成 traceback 文本再 `UE_LOG`；虽然留了 `FUnLuaDelegates::ReportLuaCallError` 扩展点，但默认出口仍然是日志字符串。`Angelscript` 则把编译诊断和调试会话消息都协议化：`Diagnostics` 自带 `filename + line + character + severity`，运行时异常用 `Stopped(reason=exception)` 停住，再让客户端按需请求 callstack。

```
[D5-Deep] Diagnostic Delivery Shape
UnLua
├─ lua_pushcfunction(ReportLuaCallError)
├─ lua_pcall() / luaL_loadbufferx()
└─ UE_LOG("Lua error message: ...")                // 默认出口是日志文本
   └─ FUnLuaDelegates::ReportLuaCallError          // 允许业务替换错误处理器

Angelscript
├─ FAngelscriptDiagnostic                          // message + line + character + severity
├─ EmitDiagnostics() -> DebugServer::SendMessage   // 编译诊断直接走协议消息
├─ ProcessException() -> Stopped(reason=exception)
└─ RequestCallStack                                // 栈信息按需单独请求
```

关键源码 [1]：`UnLua` 默认错误出口是 traceback 文本与日志，不是结构化诊断包

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaBase.cpp
// 函数: ReportLuaCallError
// 行号: 151-180
// 位置: 默认错误处理器先允许业务替换，否则把 traceback 打成日志
// ============================================================================
int32 ReportLuaCallError(lua_State *L)
{
    if (FUnLuaDelegates::ReportLuaCallError.IsBound())
        return FUnLuaDelegates::ReportLuaCallError.Execute(L);           // ★ 允许业务接管，但默认并不输出结构化包

    if (lua_type(L, -1) == LUA_TSTRING)
    {
        const char *ErrorString = lua_tostring(L, -1);
        luaL_traceback(L, L, ErrorString, 1);                           // ★ 默认把错误扩成 traceback 文本
        ErrorString = lua_tostring(L, -1);
        UE_LOG(LogUnLua, Error, TEXT("Lua error message: %s"), UTF8_TO_TCHAR(ErrorString));
    }
    else if (lua_type(L, -1) == LUA_TTABLE)
    {
        ...
        UE_LOG(LogUnLua, Error, TEXT("Lua error message %d : %s"), MessageIndex++, UTF8_TO_TCHAR(ErrorString));
    }
    lua_pop(L, 1);
    return 0;
}
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp
// 函数: FLuaEnv::DoString / LoadBuffer
// 行号: 389-410, 429-434
// 位置: 执行失败时上层主要拿到 bool，细节则留在错误处理器写日志
// ============================================================================
bool FLuaEnv::DoString(const FString& Chunk, const FString& ChunkName)
{
    lua_pushcfunction(L, ReportLuaCallError);
    ...
    const auto Result = lua_pcall(L, 0, LUA_MULTRET, MsgHandlerIdx);
    if (Result == LUA_OK)
        return true;
    ...
    return false;                                                        // ★ 成功/失败与错误细节分离
}

bool FLuaEnv::LoadBuffer(lua_State* InL, const char* Buffer, const size_t Size, const char* InName)
{
    const int32 Code = luaL_loadbufferx(InL, Buffer, Size, InName, nullptr);
    if (Code != LUA_OK)
    {
        UE_LOG(LogUnLua, Warning, TEXT("Failed to call luaL_loadbufferx, error code: %d"), Code);
        ReportLuaCallError(InL);                                          // ★ 语法/加载错误同样走日志出口
        return false;
    }
    return true;
}
```

关键源码 [2]：`Angelscript` 把诊断做成协议消息，并把异常停止与 callstack 拉取拆开

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h
// 函数: FAngelscriptDiagnostic / FAngelscriptDiagnostics
// 行号: 144-173
// 位置: 诊断消息本身就是结构化载荷，而不是纯字符串
// ============================================================================
struct FAngelscriptDiagnostic : FDebugMessage
{
    FString Message;
    int32 Line;
    int32 Character;
    bool bIsError;
    bool bIsInfo;
    ...
};

struct FAngelscriptDiagnostics : FDebugMessage
{
    FString Filename;
    TArray<FAngelscriptDiagnostic> Diagnostics;                           // ★ 文件级诊断列表直接上协议线
    ...
};
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: FAngelscriptEngine::EmitDiagnostics
// 行号: 4469-4518
// 位置: 编译诊断被重新包装成 DebugServer 消息发给客户端
// ============================================================================
void FAngelscriptEngine::EmitDiagnostics(FDiagnostics& Diag, class FSocket* Client)
{
    FAngelscriptDiagnostics Message;
    Message.Filename = Diag.Filename;
    for (auto& Ms : Diag.Diagnostics)
    {
        FAngelscriptDiagnostic New;
        New.Message = Ms.Message;
        New.Line = Ms.Row;
        New.Character = Ms.Column;
        New.bIsError = Ms.bIsError;
        New.bIsInfo = Ms.bIsInfo;
        Message.Diagnostics.Add(New);                                     // ★ 行列号与严重级别都进协议
    }

    if (Client == nullptr)
        DebugServer->SendMessageToAll(EDebugMessageType::Diagnostics, Message);
    else
        DebugServer->SendMessageToClient(Client, EDebugMessageType::Diagnostics, Message);
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 函数: FAngelscriptDebugServer::ProcessException / HandleMessage
// 行号: 455-462, 822-827, 924-927
// 位置: 运行时异常先发 stopped event；诊断库与 callstack 走独立消息
// ============================================================================
FStoppedMessage StopMsg;
StopMsg.Reason = TEXT("exception");
StopMsg.Text = ANSI_TO_TCHAR(Context->GetExceptionString());
PauseExecution(&StopMsg);                                                // ★ 运行时异常先停住
...
if (MessageType == EDebugMessageType::RequestDebugDatabase)
{
    SendDebugDatabase(Client);
    FAngelscriptEngine::Get().EmitDiagnostics(Client);                   // ★ 接入时立即同步 diagnostics
}
...
else if (MessageType == EDebugMessageType::RequestCallStack)
{
    CallstackRequests.Add(Client);                                       // ★ 栈信息按需再拉
}
```

新增对比结论：

- `UnLua` 仓内当然有错误处理，但默认交付物是 traceback 字符串和 `UE_LOG`，更适合人眼，不适合机器直接消费。
- `Angelscript` 的编译诊断已经具备 `filename + line + column + severity` 的结构化消息形状，客户端不需要从文本里反解析位置。
- `Angelscript` 也不是把所有信息塞进单包里；运行时异常仍然拆成 `StoppedMessage + RequestCallStack`，说明它的优势在于**协议化**而不是“大包揽”。

差距判断：

- `UnLua` 相对 `Angelscript` 这种仓内结构化 diagnostics channel，属于 `没有实现同层诊断载荷协议`。
- `Angelscript` 相对 `UnLua` 的 delegate-overridable 错误回调，不是没有错误处理，而是 `实现方式不同`；它更强调固定消息协议，而不是让业务重写默认 reporter。
- 如果目标是 IDE/CI 直接消费错误位置，`Angelscript` 这里可以判作 `实现质量差异` 优势；如果目标是最低侵入地挂到任意 Lua 调试器或自定义 reporter，`UnLua` 的 hook 反而更轻。

### [维度 D6] 智能提示产物的消费对象：`UnLua` 生成 Lua stub 树给 IDE，`Angelscript` 的 UHT 产物主要给 runtime bind pipeline

这一节是补前文空白。`UnLua` 的“智能提示支持”不是 README 文案，而是一条完整产线：导出接口自己实现 `GenerateIntelliSense()`，编辑器初始化时挂上 `AssetRegistry` 监听器，菜单和 commandlet 都能触发输出，最终在 `Intermediate/IntelliSense/` 下生成一棵 `.lua` 声明树给 Lua IDE 消费。`Angelscript` 也有很重的代码生成，但第一消费者不是脚本 IDE，而是 runtime 绑定闭环：`AngelscriptUHTTool` 输出 `AS_FunctionTable_*.cpp` shard、skip CSV 和 summary/coverage 诊断，编辑器里也明确标注 UHT pipeline 才是主路径。

```
[D6-Deep] Codegen Consumer
UnLua
├─ IExportedClass/Function::GenerateIntelliSense() // 类型自己负责写 stub 文本
├─ FUnLuaIntelliSenseGenerator                      // 监听资产变化并增量输出 *.lua
├─ MainMenu / Commandlet                            // 编辑器按钮 + headless 入口
└─ Intermediate/IntelliSense/...                   // IDE 直接消费

Angelscript
├─ AngelscriptUHTTool exporter                      // 扫描 BlueprintCallable/Pure
├─ AS_FunctionTable_*.cpp shards                    // 产物给 runtime 注册表
├─ Binds.Cache / BindModules.Cache                  // 启动时加载绑定数据库
└─ Legacy Native Bind Generator (Debug Only)        // 用户侧 generator 不是主路径
```

关键源码 [1]：`UnLua` 的导出接口把 `GenerateIntelliSense()` 作为正式能力面

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaBase.h
// 函数: IExportedProperty / IExportedFunction / IExportedClass / IExportedEnum
// 行号: 86-145
// 位置: 反射导出接口在 WITH_EDITOR 下统一暴露 GenerateIntelliSense() 能力
// ============================================================================
struct IExportedProperty
{
#if WITH_EDITOR
    virtual void GenerateIntelliSense(FString &Buffer) const = 0;       // ★ 属性级 stub 生成接口
#endif
};

struct IExportedFunction
{
#if WITH_EDITOR
    virtual FString GetName() const = 0;
    virtual void GenerateIntelliSense(FString &Buffer) const = 0;       // ★ 函数级 stub 生成接口
#endif
};

struct IExportedClass
{
#if WITH_EDITOR
    virtual void GenerateIntelliSense(FString &Buffer) const = 0;       // ★ 类级 stub 生成接口
#endif
};

struct IExportedEnum
{
#if WITH_EDITOR
    virtual FString GetName() const = 0;
    virtual void GenerateIntelliSense(FString &Buffer) const = 0;       // ★ 枚举级 stub 生成接口
#endif
};
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp
// 函数: Initialize / UpdateAll / Export / SaveFile
// 行号: 42-55, 58-107, 148-166, 223-233
// 位置: 编辑器初始化后常驻监听资产变化，并把结果写成 Intermediate/IntelliSense/*.lua
// ============================================================================
void FUnLuaIntelliSenseGenerator::Initialize()
{
    OutputDir = IPluginManager::Get().FindPlugin("UnLua")->GetBaseDir() + "/Intermediate/IntelliSense";
    AssetRegistryModule.Get().OnAssetAdded().AddRaw(this, &FUnLuaIntelliSenseGenerator::OnAssetAdded);
    AssetRegistryModule.Get().OnAssetRemoved().AddRaw(this, &FUnLuaIntelliSenseGenerator::OnAssetRemoved);
    AssetRegistryModule.Get().OnAssetRenamed().AddRaw(this, &FUnLuaIntelliSenseGenerator::OnAssetRenamed);
    AssetRegistryModule.Get().OnAssetUpdated().AddRaw(this, &FUnLuaIntelliSenseGenerator::OnAssetUpdated); // ★ 资产变化直接驱动 stub 更新
}

void FUnLuaIntelliSenseGenerator::UpdateAll()
{
    AssetRegistryModule.Get().GetAssets(Filter, BlueprintAssets);
    CollectTypes(NativeTypes);
    ...
    for (int32 i = 0; i < BlueprintAssets.Num(); i++)
        OnAssetUpdated(BlueprintAssets[i]);                              // ★ Blueprint 资产逐个刷新
    for (const auto Type : NativeTypes)
        Export(Type);                                                    // ★ Native 类型也统一导出
}

void FUnLuaIntelliSenseGenerator::Export(const UField* Field)
{
    const FString Content = UnLua::IntelliSense::Get(Field);
    SaveFile(ModuleName, FileName, Content);                             // ★ 每个类型输出成独立 .lua 文件
}

void FUnLuaIntelliSenseGenerator::SaveFile(const FString& ModuleName, const FString& FileName, const FString& GeneratedFileContent)
{
    if (FileContent != GeneratedFileContent)
        FFileHelper::SaveStringToFile(GeneratedFileContent, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp
// 函数: UUnLuaIntelliSenseCommandlet::Main
// 行号: 29-114
// 位置: 同一套 stub 产线也能以 commandlet 方式 headless 运行
// ============================================================================
int32 UUnLuaIntelliSenseCommandlet::Main(const FString &Params)
{
    const auto ExportedReflectedClasses = UnLua::GetExportedReflectedClasses();
    const auto ExportedNonReflectedClasses = UnLua::GetExportedNonReflectedClasses();
    const auto ExportedEnums = UnLua::GetExportedEnums();
    const auto ExportedFunctions = UnLua::GetExportedFunctions();
    ...
    Pair.Value->GenerateIntelliSense(GeneratedFileContent);
    SaveFile(ModuleName, Pair.Key, GeneratedFileContent);                // ★ 静态导出类型也走同一接口
    ...
    if (ParamsMap.Contains(BPKey) && ParamsMap[BPKey] == TEXT("1"))
        Generator->UpdateAll();                                          // ★ 可选再把 Blueprint 资产一并导出
    return 0;
}
```

关键源码 [2]：`Angelscript` 的代码生成主轴是 UHT function table 和 bind coverage，不是用户侧 IDE stub 文件

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs
// 函数: Export
// 行号: 21-48
// 位置: UHT exporter 的职责是导出 Angelscript function table 数据与 skipped CSV
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
    WriteSkippedEntriesCsv(factory, skippedEntries);
    WriteSkippedReasonSummaryCsv(factory, skippedEntries);               // ★ 输出是 function table + skipped diagnostics
}
```

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs
// 函数: Generate / GenerateModule / WriteCoverageDiagnostics
// 行号: 51-79, 81-139, 142-163
// 位置: 生成器真正写出的是 AS_FunctionTable_*.cpp shard 和 coverage diagnostics
// ============================================================================
public static int Generate(IUhtExportFactory factory)
{
    ...
    DeleteStaleOutputs(factory, generatedPaths);
    WriteGenerationSummary(factory, moduleSummaries, csvEntries, generatedFileCount);
    WriteCoverageDiagnostics(moduleSummaries);                           // ★ 关注点是 bind coverage
    return generatedFileCount;
}

private static AngelscriptModuleGenerationSummary? GenerateModule(...)
{
    ...
    string outputPath = factory.MakePath($"AS_FunctionTable_{module.ShortName}_{shardIndex:D3}", ".cpp");
    factory.CommitOutput(outputPath, BuildShard(...));                   // ★ 实际产物是 C++ 注册表 shard
    ...
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 函数: 菜单注册 / GenerateNativeBinds
// 行号: 728-730, 999-1003
// 位置: 编辑器里明确声明 legacy generator 只是 debug 用，主路径已切到 UHT pipeline
// ============================================================================
BindSection.AddMenuEntry
(
    "ASGenerateBindings",
    NSLOCTEXT("Angelscript", "GenerateBind.Label", "Legacy Native Bind Generator (Debug Only)"),
    NSLOCTEXT("Angelscript", "GenerateBind.ToolTip", "Legacy editor-side generator retained only for debugging old FunctionCallers output. The UHT-based AngelscriptUHTTool pipeline is the primary path."),
    ...
);

void FAngelscriptEditorModule::GenerateNativeBinds()
{
    GenerateBindDatabases();                                             // ★ 这里生成的也是 bind database，不是 IDE 声明树
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: runtime startup bind DB load/save
// 行号: 1466-1505
// 位置: 运行时直接消费 Binds.Cache 与 BindModules.Cache，说明生成物的第一消费者是 engine
// ============================================================================
FAngelscriptBindDatabase::Get().Load(GetScriptRootDirectory() / TEXT("Binds.Cache"), bGeneratePrecompiledData); // ★ 启动时加载 bind database
...
FAngelscriptBinds::LoadBindModules(plugin->GetBaseDir() / "BindModules.Cache");
...
FAngelscriptBindDatabase::Get().Save(GetScriptRootDirectory() / TEXT("Binds.Cache")); // ★ 输出目标仍是 runtime cache
```

新增对比结论：

- `UnLua` 的 IntelliSense 支持是一条用户可直接消费的产线，最终产物就是 IDE 能读的 `.lua` 文件树。
- `Angelscript` 的代码生成同样很重，但第一消费者是 bind/runtime/UHT 覆盖率闭环，而不是脚本作者手边的编辑器。
- 因而这里不能简单说 `UnLua` “更先进”、`Angelscript` “没有生成器”；更准确的是：`UnLua` 把生成物投向**编写脚本的人**，`Angelscript` 把生成物投向**保证绑定闭合与运行时启动的人**。

差距判断：

- 相对 `UnLua` 这种 `Intermediate/IntelliSense/*.lua` 用户侧 stub 树，`Angelscript` 当前属于 `没有实现同层 IDE 声明产物交付`。
- 相对 `Angelscript` 这种 UHT function table shard、skipped CSV、coverage summary 的绑定闭环，`UnLua` 属于 `实现方式不同`；它更关心 IDE 提示，不关心 bind coverage 账本。
- 如果目标是脚本作者开箱即用的补全体验，`UnLua` 这里可以判作 `实现质量差异` 优势；如果目标是绑定覆盖率、自动导出统计和 runtime 可消费缓存，`Angelscript` 反而更强。

---

## 深化分析 (2026-04-09 00:26:44)

### [维度 D4] 热重载扩展面的 ownership：`UnLua` 把 reload hook 交给 Lua 资产，`Angelscript` 把 reload hook 交给 C++ delegate

前几轮已经把 `UnLua` 的 sandbox merge 和 `Angelscript` 的 `SoftReload / FullReload` 状态机讲过了。这一轮只补一个之前没单独拆开的点：**如果项目方想在 reload 生命周期里插手，扩展点到底归哪一层所有**。

`UnLua` 当前快照里，热重载并不只是“用 Lua 实现”，而是**把扩展面本身做成 Lua 资产**。`UnLua.HotReload` 的 native 入口只负责执行 `require('UnLua.HotReload').reload()`；真正的扩展接口在 `Content/Script/UnLua/HotReload.lua` 顶层：`config.ignore_modules` 决定哪些模块跳过时间戳扫描，`hook.module_loaded` 决定模块初次加载和重载成功之后要不要再补业务逻辑。换句话说，项目方要改 reload 行为，首先改的是脚本文件，而不是编译插件。

`Angelscript` 的扩展点则更偏宿主/插件开发者视角。`FAngelscriptClassGenerator` 把 `OnClassReload`、`OnStructReload`、`OnFullReload`、`OnPostReload` 暴露为 C++ `multicast delegate`；但真正决定走软重载、全量重载还是保留旧代码的，是 `EReloadRequirement` 和 `AngelscriptEngine.cpp` 里的编译内核状态机。也就是说，两边都可扩展，只是 `UnLua` 让**脚本资产**拥有 reload hook，`Angelscript` 让 **C++ API** 拥有 reload hook。

```
[D4-Deep] Reload Extension Ownership
UnLua
├─ DirectoryWatcher / menu trigger                // editor 只负责触发
├─ UUnLuaFunctionLibrary::HotReload()
├─ require('UnLua.HotReload').reload()
└─ Content/Script/UnLua/HotReload.lua
   ├─ config.ignore_modules                       // Lua 层控制忽略集
   ├─ hook.module_loaded                          // Lua 层控制回调
   └─ sandbox + update_modules                    // Lua 层控制修补策略

Angelscript
├─ FAngelscriptClassGenerator delegates           // C++ 层广播 reload 事件
├─ EReloadRequirement                             // C++ 层判定 reload 级别
├─ engine switch Soft/Full/Error                  // C++ 层执行策略
└─ queued failed/full-reload files                // C++ 层保存恢复状态
```

关键源码 [1]：`UnLua` 的 native 热重载入口只做一次 Lua 调度，策略与 hook 都在脚本资产里

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaLib.cpp
// 函数: HotReload
// 行号: 51-59
// 位置: C++ 入口本身不持有 reload 策略，只负责把控制权交给 Lua 资产
// ============================================================================
static int HotReload(lua_State* L)
{
#if UNLUA_WITH_HOT_RELOAD
    if (luaL_dostring(L, "require('UnLua.HotReload').reload()") != 0)   // ★ 直接把控制平面交给脚本文件
    {
        LogError(L);
    }
#endif
    return 0;
}
```

```lua
-- ============================================================================
-- 文件: Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua
-- 函数: 顶层 config/hook 定义 + call_hook + require/reload_modules
-- 行号: 13-25, 101-109, 163-170, 589-592
-- 位置: reload 可扩展面直接以 Lua table 暴露给项目方
-- ============================================================================
local loaded_modules = setmetatable({}, { __mode = "v" })
local ignore_modules = {}
local config = {
    debug = false,
    script_root_path = UE.UUnLuaFunctionLibrary.GetScriptRootPath(),
    ignore_modules = ignore_modules                                   -- ★ 忽略策略属于 Lua 资产
}
local hook = {
    module_loaded = nil                                               -- ★ 生命周期 hook 也属于 Lua 资产
}

local function call_hook(name, ...)
    local func = hook[name]
    if not func then
        return
    end
    local ok, result = pcall(func, ...)
    if not ok then
        print(string.format("calling hook function '%s' failed : %s", name, result))
    end
end

if func then
    local _, new_module = xpcall(func, load_error_handler, ...)
    if loaded_modules[module_name] == nil then
        loaded_modules[module_name] = new_module
        package.loaded[module_name] = new_module
        loaded_module_times[module_name] = get_last_modified_time(module_name)
        call_hook("module_loaded", new_module, module_name, false)    -- ★ 首次加载也走同一 hook
    end
end

old_modules[#old_modules+1] = loaded_modules[module_name]
new_modules[#new_modules+1] = new_module
module_envs[#module_envs+1] = env
call_hook("module_loaded", new_module, module_name, true)             -- ★ reload 成功后再次回调
```

关键源码 [2]：`Angelscript` 的扩展点是 C++ delegate，策略决策仍由编译内核状态机掌握

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h
// 函数: FAngelscriptClassGenerator static delegates / EReloadRequirement
// 行号: 12-38
// 位置: reload 事件对外开放，但开放的是 C++ delegate，不是脚本侧 hook table
// ============================================================================
DECLARE_MULTICAST_DELEGATE_OneParam(FOnAngelscriptPostReload, bool);
DECLARE_MULTICAST_DELEGATE(FOnAngelscriptFullReload);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAngelscriptLiteralAssetReload, UObject*, UObject*);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAngelscriptClassReload, UClass*, UClass*);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAngelscriptStructReload, UScriptStruct*, UScriptStruct*);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAngelscriptDelegateReload, UDelegateFunction*, UDelegateFunction*);

struct FAngelscriptClassGenerator
{
    enum EReloadRequirement
    {
        SoftReload,
        FullReloadSuggested,
        FullReloadRequired,
        Error,
    };

    static ANGELSCRIPTRUNTIME_API FOnAngelscriptClassReload OnClassReload;
    static ANGELSCRIPTRUNTIME_API FOnAngelscriptStructReload OnStructReload;
    static ANGELSCRIPTRUNTIME_API FOnAngelscriptDelegateReload OnDelegateReload;
    static ANGELSCRIPTRUNTIME_API FOnAngelscriptFullReload OnFullReload;
    static ANGELSCRIPTRUNTIME_API FOnAngelscriptPostReload OnPostReload; // ★ 扩展点在 C++ API
};
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: reload requirement switch / queue bookkeeping
// 行号: 3936-4005, 4168-4187
// 位置: 真正的 reload 策略与失败恢复由编译内核统一决定
// ============================================================================
switch (ReloadReq)
{
    case FAngelscriptClassGenerator::EReloadRequirement::SoftReload:
        SwapInModules(CompiledModules, DiscardedModules);
        ClassGenerator.PerformSoftReload();
        break;
    case FAngelscriptClassGenerator::EReloadRequirement::FullReloadSuggested:
        ...
        break;
    case FAngelscriptClassGenerator::EReloadRequirement::FullReloadRequired:
        ...
        bShouldSwapInModules = false;
        bFullReloadRequired = true;                                   // ★ “不能 reload” 也是内核状态
        break;
    case FAngelscriptClassGenerator::EReloadRequirement::Error:
        ...
        bShouldSwapInModules = false;
        bHadCompileErrors = true;
        break;
}

if (Result == ECompileResult::ErrorNeedFullReload)
{
    for (const auto& RepeatFile : AllCompiledFiles)
        QueuedFullReloadFiles.Add(RepeatFile);                        // ★ 延后全量重载队列
    PreviouslyFailedReloadFiles.Append(AllCompiledFiles);            // ★ 失败恢复队列
}
else if (Result == ECompileResult::Error)
{
    PreviouslyFailedReloadFiles.Append(AllCompiledFiles);
}
else if (Result == ECompileResult::PartiallyHandled)
{
    for (const auto& RepeatFile : AllCompiledFiles)
        QueuedFullReloadFiles.Add(RepeatFile);
}
```

新增对比结论：

- `UnLua` 的一个独特点不只是“热重载逻辑在 Lua 里”，而是**reload hook surface 也在 Lua 里**，所以项目方可以直接改脚本资产来插入行为。
- `Angelscript` 也提供 reload 扩展点，但它面向的是插件/宿主 C++ 集成者；脚本作者拿到的不是 hook table，而是已经执行完的 reload 结果。

差距判断：

- 相对 `UnLua` 这种脚本侧 `config + hook` 的 reload 扩展面，`Angelscript` 当前属于 `没有实现同层脚本化扩展面`。
- 相对 `Angelscript` 这种 `EReloadRequirement + queued files + failed files` 的内核状态机，`UnLua` 属于 `实现方式不同`；它给的是可改策略脚本，不是固定状态枚举。
- 如果目标是让项目层快速插业务回调，`UnLua` 更轻；如果目标是让 reload 结果稳定传播给 editor/runtime 子系统，`Angelscript` 的 C++ delegate 更稳。

### [维度 D9] 测试产物的消费对象：`UnLua` 的 benchmark 更像实验记录，`Angelscript` 的 coverage 是 automation gate artifact

前文已经覆盖过 `UnLuaTestSuite` 的宏 DSL、issue 回归和 `Angelscript` 的热重载后批量测试。本轮补的是另一个更偏 CI / 工具链的问题：**测试跑完以后，仓库实际交付给机器或人看的产物是什么形状**。

`UnLua` 当前快照里，功能正确性的主路径仍然是 UE Automation：`IMPLEMENT_UNLUA_INSTANT_TEST` 这种宏最终返回 `bool`。但性能数据走的是另一条完全不同的链路：`BP_UnLuaBenchmark.lua` 只是不断 `StartTimer / StopTimer`，`UUnLuaBenchmarkFunctionLibrary::Stop()` 把字符串拼成 CSV 并落到 `Saved/Benchmark/`。这里没有阈值、没有 `TestTrue`、也没有对 CSV 结构本身的仓内单测，所以它更像给人做横向观测的实验记录，而不是 automation gate。

`Angelscript` 则把覆盖率产物直接挂进 Automation 生命周期。`FAngelscriptCodeCoverage::AddTestFrameworkHooks()` 在 `OnTestsAvailable / OnTestsComplete` 之间自动开始和停止录制；`StopRecordingAndWriteReport()` 会生成 HTML 和 `coverage_summary.json`；并且 `AngelscriptCodeCoverageTests.cpp` 继续验证 coverage 统计逻辑本身。这意味着它交付的是**机器可消费、且产物生成器自身也被测试保护**的 artifact。

```
[D9-Deep] Test Artifact Contract
UnLua
├─ IMPLEMENT_UNLUA_*                              // pass/fail 仍走 Automation bool
├─ Benchmark lua loops                            // 性能路径单独存在
├─ StartTimer / StopTimer
└─ Saved/Benchmark/*.csv                          // ★ 人工对比型产物

Angelscript
├─ AutomationController lifecycle hooks           // 测试框架事件直接驱动
├─ StartRecording / StopRecordingAndWriteReport
├─ Saved/CodeCoverage/*.html
├─ Saved/CodeCoverage/coverage_summary.json       // ★ 机器可消费型产物
└─ Coverage automation tests                      // 产物生成器本身也被验证
```

关键源码 [1]：`UnLua` 的 pass/fail 与 benchmark artifact 是两条分开的路径

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Public/UnLuaTestCommon.h
// 函数: IMPLEMENT_UNLUA_INSTANT_TEST
// 行号: 187-205
// 位置: 正常功能测试最终仍然返回 bool，属于 UE Automation 的 pass/fail 合约
// ============================================================================
#define IMPLEMENT_UNLUA_INSTANT_TEST(TestClass, PrettyName) \
IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass##Runner, PrettyName, (EAutomationTestFlags::ClientContext | EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)) \
bool TestClass##Runner::RunTest(const FString& Parameters) \
{ \
bool bSuccess = false; \
TestClass* TestInstance = new TestClass(); \
TestInstance->SetTestRunner(*this); \
if (TestInstance->SetUp()) \
{ \
bSuccess = TestInstance->InstantTest();                               /* ★ 最终产物是 bool */ \
TestInstance->TearDown(); \
}\
delete TestInstance; \
return bSuccess; \
}
```

```lua
-- ============================================================================
-- 文件: Reference/UnLua/Content/Script/Tests/Benchmark/BP_UnLuaBenchmark.lua
-- 函数: M:Run
-- 行号: 19-29, 169-180
-- 位置: benchmark 只记录时间，不参与 pass/fail 断言
-- ============================================================================
StartTimer("read int32")
for i=1, N do
    local MeshID = Proxy.MeshID
end
StopTimer()

StartTimer("write int32")
for i=1, N do
    Proxy.MeshID = i
end
StopTimer()
...
StartTimer("bool GetMeshInfo(int32&, FString&, FVector&, TArray<int32>&, TArray<FVector>&, TArray<FVector>&) const")
for i=1, N do
    local bResult, ID, Name = Proxy:GetMeshInfo(0, "", COM, Indices, Positions, PredictedPositions)
end
StopTimer()
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Perfs/UnLuaBenchmarkFunctionLibrary.cpp
// 函数: UUnLuaBenchmarkFunctionLibrary::Stop
// 行号: 46-50
// 位置: benchmark 结束时只是把结果写成 CSV，没有阈值判断与结构校验
// ============================================================================
void UUnLuaBenchmarkFunctionLibrary::Stop()
{
    const auto Message = FString::Join(Messages, TEXT("\n"));
    const auto FilePath = FString::Printf(TEXT("%sBenchmark/%s-Benchmark-%s.csv"), *FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()), *BenchmarkTitle, *FDateTime::Now().ToString());
    FFileHelper::SaveStringToFile(Message, *FilePath);                // ★ 产物面向人工读取
}
```

关键源码 [2]：`Angelscript` 把 coverage 产物直接挂到 automation 生命周期，并且测试产物生成器本身

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/AngelscriptCodeCoverage.cpp
// 函数: AddTestFrameworkHooks / CoverageEnabled / StopRecordingAndWriteReport
// 行号: 22-29, 45-65
// 位置: 覆盖率录制被绑定到 AutomationController 生命周期，支持 CI 命令行开关
// ============================================================================
void FAngelscriptCodeCoverage::AddTestFrameworkHooks()
{
    IAutomationControllerModule& AutomationModule =
        FModuleManager::LoadModuleChecked<IAutomationControllerModule>("AutomationController");
    IAutomationControllerManagerRef AutomationController = AutomationModule.GetAutomationController();
    AutomationController->OnTestsAvailable().AddRaw(this, &FAngelscriptCodeCoverage::OnTestsStarting);
    AutomationController->OnTestsComplete().AddRaw(this, &FAngelscriptCodeCoverage::OnTestsStopping); // ★ 直接挂到测试生命周期
}

bool FAngelscriptCodeCoverage::CoverageEnabled()
{
    return (GetDefault<UAngelscriptTestSettings>()->bEnableCodeCoverage ||
            FParse::Param(FCommandLine::Get(), TEXT("as-enable-code-coverage"))); // ★ 支持 CI 命令行 gate
}

void FAngelscriptCodeCoverage::StopRecordingAndWriteReport(const FString& OutputDir)
{
    bRecording = false;
    UE_LOG(Angelscript, Display, TEXT("Tests complete, writing coverage report to %s."), *OutputDir);
    WriteReportHtml(OutputDir);
    WriteCoverageSummaries(OutputDir);                                 // ★ 生成稳定 artifact
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/CoverageReportGenerator.cpp
// 函数: WriteTopLevelCoverageJson
// 行号: 282-305
// 位置: 仓内直接输出 coverage_summary.json，适合脚本化消费
// ============================================================================
bool WriteTopLevelCoverageJson(FCoverageNode& Root, const FString& OutputDir)
{
    ...
    auto Json = MakeShared<FJsonObject>();
    Json->SetObjectField(TEXT("total"), CountsToJson(Root.Counts));
    Json->SetArrayField(TEXT("coverage"), Directories);
    ...
    FString OutputFile = FPaths::Combine(OutputDir, TEXT("coverage_summary.json"));
    if (!FFileHelper::SaveStringToFile(JsonString, *OutputFile,         // ★ 输出稳定 JSON，而不是自由文本
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        ...
    }
    return true;
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptCodeCoverageTests.cpp
// 函数: FAngelscriptCodeCoverageTests0::RunTest / FAngelscriptCodeCoverageTests3::RunTest
// 行号: 17-45, 151-181
// 位置: 覆盖率统计与聚合逻辑本身也有 Automation 保护
// ============================================================================
bool FAngelscriptCodeCoverageTests0::RunTest(const FString& Parameters)
{
    FAngelscriptEngine& Manager = FAngelscriptEngine::Get();
    FAngelscriptCodeCoverage Coverage;
    for (TSharedRef<struct FAngelscriptModuleDesc>& Module : Manager.GetActiveModules())
    {
        Coverage.MapExecutableLines(*Module);                          // ★ 用真实模块验证 integration path
    }
    ...
}

bool FAngelscriptCodeCoverageTests3::RunTest(const FString& Parameters)
{
    FCoverageNode Root;
    ...
    AddCoverageLeaf(Root, "A\\B\\C.as", C);
    AddCoverageLeaf(Root, "A/B/D.as", D);
    ...
    FCoverageCounts Result = ComputeCoverage(Root);                    // ★ 聚合算法单独有测试
    ...
}
```

新增对比结论：

- `UnLua` 的 benchmark 很实用，但它交付的是“实验记录”，不是“阈值化 gate”；结果主要给人看，不是先给机器判。
- `Angelscript` 的 coverage 体系正好相反：它把 artifact 直接嵌进 automation 生命周期，连产物生成器自身都受 Automation 保护。

差距判断：

- 相对 `Angelscript` 这种 `Automation hook + JSON/HTML + artifact self-test` 的链路，`UnLua` 当前属于 `没有实现同层 machine-readable gate artifact`。
- 相对 `UnLua` 这种 sample 侧可直接跑的 benchmark CSV 实验台，`Angelscript` 当前更接近 `实现方式不同`；重心在覆盖率 gate，而不是把脚本侧微基准作为主要交付面。
- 如果目标是 CI / 报表稳定性，`Angelscript` 更强；如果目标是快速观察“某类绑定调用大概有多贵”，`UnLua` 的 benchmark 入口更直接。

### [维度 D10] 教程是否直接教授插件扩展：`UnLua` 的教程在教 extension seam，`Angelscript` 的示例主要在教 compile contract

前文已经写过 `README -> Docs -> Tutorials` 的目录闭环。这一轮再往下一层，补一个更细的观察：**这些教程到底是在教“怎么用插件”，还是已经开始教“怎么扩展插件本身”**。

`UnLua` 的 sample project 教程已经明显跨过了“纯 API 示例”的边界。`09_StaticExport.lua` 开头直接把读者指到 `Source/TPSProject/TutorialObject.cpp`，运行时再实例化 `UE.FTutorialObject("教程")`；对应 C++ helper 里真的存在 `BEGIN_EXPORT_CLASS(FTutorialObject)`、`ADD_FUNCTION(GetTitle)` 和自定义 `__call` 构造逻辑。`12_CustomLoader.lua` 也一样，脚本头注释直接告诉你去看 `TutorialBlueprintFunctionLibrary.cpp`，运行时再切 `SetupCustomLoader(1/2/0)`；而 native helper 真正把 `FUnLuaDelegates::CustomLoadLuaFile` 绑定到两种 loader。也就是说，`UnLua` 的教程已经开始承担**扩展 cookbook** 的职责。

`Angelscript` 当前仓库的示例主载体则仍然是 regression harness。`Example_Actor.as` 这类示例并不以真实脚本文件或 sample asset 存在，而是嵌在 `FScriptExampleSource` 的 C++ 字符串常量里，再由 `RunScriptExampleCompileTest()` 编译成 `ScriptExamples/<Example>.as` 这种虚拟路径。这样做当然很好，它让示例永远和断言绑在一起；但它教给读者的主要是“这段脚本应该如何通过编译并具备哪些语义”，而不是“去 sample project 改哪个 native helper 才能扩展插件”。

```
[D10-Deep] Extension Tutorial Carrier
UnLua
├─ Tutorial .lua header comment                   // 先告诉读者 native helper 在哪
├─ runtime calls UE.FTutorialObject / UE.UTutorialBlueprintFunctionLibrary
├─ TPSProject native helper                        // 原生扩展代码和教程成对出现
└─ Result: sample project teaches plugin extension

Angelscript
├─ FScriptExampleSource raw string in test .cpp   // 示例先是测试夹具
├─ RunScriptExampleCompileTest()
├─ virtual file ScriptExamples/*                  // 编译时临时映射
└─ Result: examples teach language/bind contract
```

关键源码 [1]：`UnLua` 教程直接把读者带到 native extension 源码，再在运行时调用对应扩展点

```lua
-- ============================================================================
-- 文件: Reference/UnLua/Content/Script/Tutorials/09_StaticExport.lua
-- 函数: M:ReceiveBeginPlay
-- 行号: 1-23
-- 位置: 教程正文直接标注 native helper 路径，并在脚本里实例化静态导出类型
-- ============================================================================
--[[
    说明：

    本示例C++源码：
    \Source\TPSProject\TutorialObject.cpp                      -- ★ 先把扩展源码路径直接告诉用户
]]--

local M = UnLua.Class()

function M:ReceiveBeginPlay()
    ...
    local tutorial = UE.FTutorialObject("教程")               -- ★ 教程直接消费 sample project 导出的原生类型
    msg = string.format("tutorial -> %s\n\ntutorial:GetTitle() -> %s", tostring(tutorial), tutorial:GetTitle())
    ...
end
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Source/TPSProject/TutorialObject.cpp
// 函数: FTutorialObject_New / exported class registration
// 行号: 11-29, 38-42
// 位置: sample project helper 真正展示“如何给 UnLua 增加一个静态导出类型”
// ============================================================================
static int32 FTutorialObject_New(lua_State* L)
{
    const auto NumParams = lua_gettop(L);
    if (NumParams != 2)
    {
        UNLUA_LOGERROR(L, LogUnLua, Log, TEXT("%s: Invalid parameters!"), ANSI_TO_TCHAR(__FUNCTION__));
        return 0;
    }

    const auto UserData = NewUserdataWithPadding(L, sizeof(FTutorialObject), "FTutorialObject");
    new(UserData) FTutorialObject(UTF8_TO_TCHAR(NameChars));            // ★ 教程对应的构造逻辑就写在 sample helper 里
    return 1;
}

BEGIN_EXPORT_CLASS(FTutorialObject)
ADD_FUNCTION(GetTitle)
ADD_LIB(FTutorialObjectLib)                                             // ★ 教程同时在教 static export 宏怎么写
END_EXPORT_CLASS()
IMPLEMENT_EXPORTED_CLASS(FTutorialObject)
```

```lua
-- ============================================================================
-- 文件: Reference/UnLua/Content/Script/Tutorials/12_CustomLoader.lua
-- 函数: M:ReceiveBeginPlay
-- 行号: 1-39
-- 位置: 教程直接演示自定义 loader，并把 native helper 文件路径写在注释里
-- ============================================================================
--[[
    说明：通过绑定 FUnLuaDelegates::CustomLoadLuaFile 可以实现自定义Lua加载器
    ...
    本示例C++源码：
    Source\TPSProject\TutorialBlueprintFunctionLibrary.cpp              -- ★ 再次把原生实现位置直接暴露给读者
]]--

function M:ReceiveBeginPlay()
    ...
    UE.UTutorialBlueprintFunctionLibrary.SetupCustomLoader(1)
    Screen.Print(string.format("FromCustomLoader1:%s", require("Tutorials")))

    package.loaded["Tutorials"] = nil
    package.path = package.path .. ";./?/Index.lua"
    UE.UTutorialBlueprintFunctionLibrary.SetupCustomLoader(2)
    Screen.Print(string.format("FromCustomLoader2:%s", require("Tutorials")))

    UE.UTutorialBlueprintFunctionLibrary.SetupCustomLoader(0)
end
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Source/TPSProject/TutorialBlueprintFunctionLibrary.cpp
// 函数: CustomLoader1 / CustomLoader2 / UTutorialBlueprintFunctionLibrary::SetupCustomLoader
// 行号: 46-59, 61-89, 91-104
// 位置: 教程配套的 native helper 直接展示如何挂 UnLua loader 扩展点
// ============================================================================
bool CustomLoader1(UnLua::FLuaEnv& Env, const FString& RelativePath, TArray<uint8>& Data, FString& FullPath)
{
    ...
    FullPath.ReplaceInline(TEXT(".lua"), TEXT("/Index.lua"));
    if (FFileHelper::LoadFileToArray(Data, *FullPath, FILEREAD_Silent))
        return true;
    return false;
}

bool CustomLoader2(UnLua::FLuaEnv& Env, const FString& RelativePath, TArray<uint8>& Data, FString& FullPath)
{
    ...
    for (auto& Part : Parts)
    {
        ...
        if (FFileHelper::LoadFileToArray(Data, *FullPath, FILEREAD_Silent))
            return true;
    }
    return false;
}

void UTutorialBlueprintFunctionLibrary::SetupCustomLoader(int Index)
{
    switch (Index)
    {
    case 0:
        FUnLuaDelegates::CustomLoadLuaFile.Unbind();
        break;
    case 1:
        FUnLuaDelegates::CustomLoadLuaFile.BindStatic(CustomLoader1);
        break;
    case 2:
        FUnLuaDelegates::CustomLoadLuaFile.BindStatic(CustomLoader2);   // ★ 教程在教 extension seam，而不是只教 API 用法
        break;
    }
}
```

关键源码 [2]：`Angelscript` 示例主要以 compile fixture 形式存在，优先保证 regression，而不是 sample-native 教学链

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleActorTest.cpp
// 函数: GActorExample / FAngelscriptScriptExampleActorTest::RunTest
// 行号: 9-95
// 位置: 示例源码以内嵌字符串存在，首先是自动化测试夹具
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
};)ANGELSCRIPT"),
    nullptr,
    nullptr,
};

bool FAngelscriptScriptExampleActorTest::RunTest(const FString& Parameters)
{
    return AngelscriptScriptExamples::RunScriptExampleCompileTest(*this, GActorExample); // ★ 示例先服务于 regression
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleTestSupport.cpp
// 函数: AngelscriptScriptExamples::RunScriptExampleCompileTest
// 行号: 16-59
// 位置: 示例被编译到虚拟文件名，而不是 sample project 中的持久教程资产
// ============================================================================
bool RunScriptExampleCompileTest(FAutomationTestBase& Test, const FScriptExampleSource& Example)
{
    ...
    const FString ExampleFileName = Example.ExampleFileName;
    const FString ModuleNameString = FPaths::GetBaseFilename(ExampleFileName);
    ...
    FString CombinedScriptCode;
    if (Example.DependencyScriptText != nullptr)
    {
        ...
        CombinedScriptCode += TEXT("\n\n");
    }

    CombinedScriptCode += Example.ScriptText;

    const FString VirtualFileName = FString::Printf(TEXT("ScriptExamples/%s"), *ExampleFileName);
    const bool bCompiled = CompileAnnotatedModuleFromMemory(&Engine, ModuleName, VirtualFileName, CombinedScriptCode); // ★ 示例文件名本身是虚拟路径
    Test.TestTrue(*FString::Printf(TEXT("Compile example '%s' succeeds"), *ExampleFileName), bCompiled);
    return bCompiled;
}
```

新增对比结论：

- `UnLua` 教程的一个深层优势不只是“示例多”，而是**教程直接把扩展点和 native helper 源码路径暴露给用户**，所以它天然适合作为插件扩展入门材料。
- `Angelscript` 示例的强项则是 regression 绑定更紧。示例源码和断言放在同一份 C++ 自动化夹具里，能持续验证语言特性、绑定和编译行为，但不直接承担 sample-native 教学职责。

差距判断：

- 相对 `UnLua` 这种“教程脚本直接指向 native extension helper”的交付方式，`Angelscript` 当前属于 `没有实现同层工程内扩展教程载体`。
- 相对 `Angelscript` 这种 compile-locked example fixture，`UnLua` 属于 `实现方式不同`；它更强调开放式 sample project 教学，而不是把每个示例都锁进回归夹具。
- 如果 `Angelscript` 需要补 onboarding，值得吸收的不是简单再写几篇文档，而是增加一组**会直接指向最小 native helper 文件的 sample 级教程**，同时保留现有 `Examples/*.cpp` 回归夹具。

---

## 深化分析 (2026-04-09 00:38:01)

### [维度 D5] 调试与保护机制的通道预算：`UnLua` 复用同一个 `lua_sethook` 槽位，`Angelscript` 把 timeout/debug/data breakpoint 拆到不同机制

前几轮已经把协议归属、断点 ownership 和诊断载荷拆开了。这一轮补一个更底层但很关键的点：**开发期观测能力到底共享多少 VM 级“通道”**。`UnLua` 当前快照里，`Unreal Insights` profiling 与 `DeadLoopCheck` 都落在 `lua_State` 的 hook 机制上。`FLuaEnv` 创建时，如果开了 `DeadLoopCheck`，源码会直接警告 profiling 不工作；`FDeadLoopCheck::FGuard::SetTimeout()` 还会先查 `lua_gethook(L)`，只有 hook 为空才装入自己的 line hook。这不是“功能没做完”，而是 Lua VM 本身只有一条 hook 通道，插件必须在 profiling / timeout / 其他 hook 之间做让位。

`Angelscript` 的切法不同。脚本 context 创建时会同时装 `SetLineCallback()`、`SetStackPopCallback()` 和 `SetLoopDetectionCallback()`；`DebugServer` 再在 Windows 下额外挂 `AddVectoredExceptionHandler()` 承载 data breakpoint。`UpdateLineCallbackState()` 负责决定 line callback 是否真正工作，因此 debug / coverage 仍然共享“脚本行回调”这条面，但**loop timeout** 和 **data breakpoint** 已经被拆出，不会像 `UnLua` 一样直接抢占同一个 VM hook slot。

```
[D5-Deep] Instrumentation Channel Budget
UnLua
├─ lua_sethook(CALL|RET)                         // Unreal Insights profiling
├─ lua_sethook(LINE)                             // DeadLoop timeout
└─ one lua_State hook slot                       // 二者会争用同一安装位

Angelscript
├─ SetLineCallback()                             // line/debug/coverage 分发入口
├─ SetLoopDetectionCallback()                    // timeout 独立回调
├─ SetStackPopCallback()                         // 栈帧退栈观测
└─ AddVectoredExceptionHandler()                 // data breakpoint 走 OS 层
```

关键源码 [1]：`UnLua` 的 profiling 与 timeout 明确共用 `lua_sethook`

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp
// 函数: FLuaEnv::FLuaEnv
// 行号: 115-116, 166-170
// 位置: 创建 Lua env 时初始化 DeadLoopCheck，并在 profiling hook 与 timeout 之间做互斥判断
// ============================================================================
DanglingCheck = new FDanglingCheck(this);
DeadLoopCheck = new FDeadLoopCheck(this);                          // ★ timeout guard 常驻于 env
...
#if ENABLE_UNREAL_INSIGHTS && CPUPROFILERTRACE_ENABLED
if (FDeadLoopCheck::Timeout)
    UE_LOG(LogUnLua, Warning, TEXT("Profiling will not working when DeadLoopCheck enabled."))
else
    lua_sethook(L, Hook, LUA_MASKCALL | LUA_MASKRET, 0);           // ★ profiling 也占用同一个 hook slot
#endif
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaDeadLoopCheck.cpp
// 函数: FDeadLoopCheck::FGuard::SetTimeout / OnLuaLineEvent
// 行号: 102-113
// 位置: timeout 触发时先检查当前 hook 是否为空，然后装入自己的 line hook
// ============================================================================
void FDeadLoopCheck::FGuard::SetTimeout()
{
    const auto L = Owner->Env->GetMainState();
    const auto Hook = lua_gethook(L);
    if (Hook == nullptr)
        lua_sethook(L, OnLuaLineEvent, LUA_MASKLINE, 0);           // ★ 只有空槽位时才能装 timeout hook
}

void FDeadLoopCheck::FGuard::OnLuaLineEvent(lua_State* L, lua_Debug* ar)
{
    lua_sethook(L, nullptr, 0, 0);                                // ★ 命中后把 hook 清掉
    luaL_error(L, "lua script exec timeout");
}
```

关键源码 [2]：`Angelscript` 把 line/debug、timeout、data breakpoint 拆到不同回调面

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: FAngelscriptEngine::CreateContext / UpdateLineCallbackState / AngelscriptLoopDetectionCallback
// 行号: 1707-1717, 5429-5451, 5566-5588
// 位置: context 创建时同时挂入 line callback 与 loop detection，后续按状态决定 line callback 是否实际参与
// ============================================================================
auto* Context = (asCContext*)Engine->CreateContext();
Context->SetExceptionCallback(asFUNCTION(LogAngelscriptException), 0, asCALL_CDECL);
#if WITH_AS_DEBUGVALUES || WITH_AS_DEBUGSERVER
Context->SetLineCallback(AngelscriptLineCallback);                 // ★ debug / coverage 统一从这里分发
Context->SetStackPopCallback(AngelscriptStackPopCallback);
#endif
#if WITH_EDITOR
if (!IsRunningCommandlet())
    Context->SetLoopDetectionCallback(AngelscriptLoopDetectionCallback); // ★ timeout 独立回调
#endif
...
if (DebugServer != nullptr)
{
    if (DebugServer->bIsDebugging)
        bEverRunLineCallback = true;
    if (DebugServer->DataBreakpoints.Num() != 0)
        bEverRunLineCallback = true;
    if (DebugServer->bBreakNextScriptLine)
        bAlwaysRunLineCallback = true;
}
...
if (MaximumScriptExecutionTime > 0)
{
    if (Context->m_loopDetectionTimer < CurrentTime - MaximumScriptExecutionTime)
    {
        Context->SetException("Script function took too long to execute. Potentially an infinite loop? (timeout controlled by EditorMaximumScriptExecutionTime setting)");
        return;
    }
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 函数: FAngelscriptDebugServer ctor / UpdateDataBreakpoints
// 行号: 409-415, 1275-1285
// 位置: data breakpoint 通过 Windows debug register exception handler 承载，不和 line callback 抢同一个 API 槽位
// ============================================================================
#if PLATFORM_WINDOWS && WITH_AS_DEBUGSERVER
DataBreakpoint_Windows::GActiveDebugServer.Store(this);
if (DataBreakpoint_Windows::DegbugRegisterExceptionHandlerHandle)
{
    ::RemoveVectoredExceptionHandler(DataBreakpoint_Windows::DegbugRegisterExceptionHandlerHandle);
}
DataBreakpoint_Windows::DegbugRegisterExceptionHandlerHandle = ::AddVectoredExceptionHandler(0, DataBreakpoint_Windows::DebugRegisterExceptionHandler); // ★ OS 级 data breakpoint 通道
#endif
...
void FAngelscriptDebugServer::UpdateDataBreakpoints()
{
    RebuildActiveDataBreakpoints();
#if PLATFORM_WINDOWS && WITH_AS_DEBUGSERVER
    {
        DataBreakpoint_Windows::FUpdateDebugRegisterThread UpdateDebugRegisters(
            DataBreakpoint_Windows::GetThreadAgnosticCurrentThreadHandle(),
            DataBreakpoints);                                       // ★ 数据断点走硬件寄存器，不占 loop timeout 通道
    }
#endif
    FAngelscriptEngine::Get().UpdateLineCallbackState();
}
```

新增对比结论：

- `UnLua` 的调试/保护底座并不弱，但它受限于 Lua VM 单一 hook 通道，profiling 与 timeout 在当前实现里存在明确互斥。
- `Angelscript` 也不是完全“多通道互不影响”；debug 与 coverage 仍会复用 line callback，但 timeout 和 data breakpoint 已经被拆出到独立机制。
- 这意味着两者差异不只在“有没有调试器”，还在**底层 instrumentation budget 的分配方式**。

差距判断：

- 相对 `Angelscript` 这种 `line callback + loop callback + OS data breakpoint` 的拆分，`UnLua` 在当前快照上属于 `实现质量差异`，因为同类开发期能力更容易互相让位。
- 相对 `UnLua` 这种直接复用 Lua C API hook 的轻量实现，`Angelscript` 属于 `实现方式不同`；它用更重的引擎内核集成换取更少的通道冲突。
- 这里不应判成 `没有实现`，因为 `UnLua` 的 profiling 与 timeout 都已经实现，只是实现落点更集中。

### [维度 D9] Editor toolchain 的回归 ownership：`UnLua` 把编辑器能力做进模块，但测试主阵地仍是 runtime harness；`Angelscript` 直接为 editor internals 写 automation

前文多次分析了 `UnLua` 的 runtime/spec/issue/benchmark 测试层。这一轮补的是**编辑器侧能力由谁来回归**。`UnLuaEditorModule` 在启动时会立刻挂上脚本目录监听、工具栏、`IntelliSenseGenerator` 和打包设置修正；这说明 editor toolchain 在 `UnLua` 里不是附属脚本，而是真正会改项目状态的正式能力面。

但测试 ownership 仍然主要落在 `UnLuaTestSuite` 这个 runtime test plugin 上。`UnLuaTestSuite.Build.cs` 只显式依赖 `Lua`、`UnLua`、`UMG`，编辑器下额外拉 `UnrealEd`，却没有把 `UnLuaEditor` 自身作为独立被测单元组织起来。结合当前仓库检索 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/**/*.cpp` 未发现 `*Test*.cpp`，可以推断当前快照里 editor helper 的回归更多依赖人工使用样例工程，而不是同层 automation。

`Angelscript` 则明显更“把 editor internals 当产品功能”。`AngelscriptDirectoryWatcherTests.cpp` 直接验证 add/remove/folder-add/folder-remove/rename 五类 watcher 语义；`AngelscriptBlueprintImpactScannerTests.cpp` 继续验证 path normalization、影响符号构建、commandlet 参数错误、磁盘资产扫描等 editor-only 能力。也就是说，`Angelscript` 不只是测试“脚本能不能跑”，而是把 **editor pipeline 本身** 纳入 regression contract。

```
[D9-Deep] Editor Regression Ownership
UnLua
├─ UnLuaEditorModule wires watcher/intellisense/package staging
├─ UnLuaTestSuite remains the main automation carrier
└─ current snapshot has no UnLuaEditor/Private/Tests/*.cpp   // 基于仓库检索的推断

Angelscript
├─ AngelscriptEditor internals
│  ├─ DirectoryWatcher tests
│  └─ BlueprintImpact / Commandlet tests
└─ editor helpers enter automation catalog directly
```

关键源码 [1]：`UnLua` 的 editor toolchain 是正式模块职责，而不是临时样例脚本

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp
// 函数: FUnLuaEditorModule::StartupModule / OnPostEngineInit / SetupPackagingSettings
// 行号: 48-69, 98-102, 186-239
// 位置: editor 模块在启动期接管目录监听、工具栏、stub 生成和打包目录注入
// ============================================================================
virtual void StartupModule() override
{
    Style = FUnLuaEditorStyle::GetInstance();
    FUnLuaEditorCommands::Register();
    FCoreDelegates::OnPostEngineInit.AddRaw(this, &FUnLuaEditorModule::OnPostEngineInit);
    MainMenuToolbar = MakeShareable(new FMainMenuToolbar);
    BlueprintToolbar = MakeShareable(new FBlueprintToolbar);
    AnimationBlueprintToolbar = MakeShareable(new FAnimationBlueprintToolbar);
    UUnLuaEditorFunctionLibrary::WatchScriptDirectory();          // ★ editor 直接接管脚本目录监听
    ...
    SetupPackagingSettings();                                     // ★ 还会主动修改项目打包配置
}
...
MainMenuToolbar->Initialize();
BlueprintToolbar->Initialize();
AnimationBlueprintToolbar->Initialize();
FUnLuaIntelliSenseGenerator::Get()->Initialize();                // ★ IntelliSense generator 也在 editor 生命周期内常驻
...
auto ScriptPaths = TArray<FString>{TEXT("Script"), TEXT("../Plugins/UnLua/Content/Script")};
...
PackagingSettings->DirectoriesToAlwaysStageAsUFS.Add(DirectoryPath); // ★ editor 模块负责交付期目录注入
```

关键源码 [2]：`UnLua` 的主测试载体仍然是 runtime test plugin，而不是 editor internal test bundle

```csharp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/UnLuaTestSuite.Build.cs
// 函数: UnLuaTestSuite::UnLuaTestSuite
// 行号: 33-64
// 位置: 当前测试模块主要围绕 runtime/plugin harness 组织，未把 UnLuaEditor 作为独立测试边界显式建模
// ============================================================================
PublicDependencyModuleNames.AddRange(
    new[]
    {
        "Core",
        "CoreUObject",
        "Engine",
        "Slate"
    }
);

PrivateDependencyModuleNames.AddRange(
    new[]
    {
        "Lua",
        "UnLua",
        "UMG"                                                // ★ 主体还是 runtime 功能回归
    }
);

if (Target.bBuildEditor)
{
    PrivateDependencyModuleNames.Add("UnrealEd");            // ★ editor 下加的是通用编辑器依赖，不是 UnLuaEditor 自测边界
}

PrecompileForTargets = PrecompileTargetsType.Any;
```

关键源码 [3]：`Angelscript` 明确把 editor internals 做成 automation catalog

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp
// 函数: automation declarations / FAngelscriptDirectoryWatcherScriptQueueTest::RunTest
// 行号: 15-38, 75-102
// 位置: watcher 行为不是隐式依赖人工验证，而是 editor automation 的正式目标
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAngelscriptDirectoryWatcherScriptQueueTest,
    "Angelscript.Editor.DirectoryWatcher.Queue.ScriptAddAndRemove",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
...
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAngelscriptDirectoryWatcherRenameWindowTest,
    "Angelscript.Editor.DirectoryWatcher.Queue.RenameWindowTracksRemoveAndAdd",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
...
AngelscriptEditor::Private::QueueScriptFileChanges(Changes, Engine->AllRootPaths, *Engine, FileManager, [](const FString&)
{
    return TArray<FAngelscriptEngine::FFilenamePair>();
});

TestEqual(TEXT("DirectoryWatcher.Queue.ScriptAddAndRemove should queue one added script"), Engine->FileChangesDetectedForReload.Num(), 1);
TestEqual(TEXT("DirectoryWatcher.Queue.ScriptAddAndRemove should queue one removed script"), Engine->FileDeletionsDetectedForReload.Num(), 1);
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactScannerTests.cpp
// 函数: automation declarations
// 行号: 22-75
// 位置: BlueprintImpact scanner 与 commandlet 参数边界也被直接纳入 editor automation
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAngelscriptBlueprintImpactNormalizePathsTest,
    "Angelscript.Editor.BlueprintImpact.NormalizePaths",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
...
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAngelscriptBlueprintImpactCommandletInvalidFileTest,
    "Angelscript.Editor.BlueprintImpact.CommandletInvalidFile",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
...
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAngelscriptBlueprintImpactFindBlueprintAssetsDiskBackedTest,
    "Angelscript.Editor.BlueprintImpact.FindBlueprintAssetsDiskBacked",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
```

新增对比结论：

- `UnLua` 的 editor toolchain 已经很完整，但当前回归 contract 主要仍由 `UnLuaTestSuite` 这个 runtime harness 承担。
- `Angelscript` 的一个明显不同点，是把 editor internals 当成和 runtime 一样需要固定自动化语义的对象。
- 因而这里真正的差别不是“有没有 editor 功能”，而是**editor 功能是否拥有同层自动化所有权**。

差距判断：

- 相对 `Angelscript` 这种显式 `Source/AngelscriptEditor/Private/Tests/*.cpp` 自动化闭环，`UnLua` 在当前快照上可判作 `没有实现同层 editor-internal automation coverage`。这一点基于源码组织与仓库检索推断，不是单靠 README 判断。
- 相对 `UnLua` 以样例工程承载大部分测试入口的做法，`Angelscript` 属于 `实现方式不同`；它选择把更多内部编辑器工具公开为可回归 contract。
- 如果目标是降低 watcher / commandlet / blueprint scanner 这类 editor helper 的回归风险，`Angelscript` 这里体现为 `实现质量差异` 优势。

### [维度 D11] 交付物形态：`UnLua` 交付“原始脚本目录 + UFS staged content”，`Angelscript` 交付“脚本根目录 + cache/precompiled artifacts”

前文主要集中在绑定、热重载、调试和文档。这一轮补一个之前没展开但很影响产品定位的维度：**最终打包给项目、机器、版本管理和热更新系统的到底是什么形状**。

`UnLua` 的交付假设非常直接。`UUnLuaFunctionLibrary::GetScriptRootPath()` 把脚本根固定到 `ProjectContent/Script/`；`UnLuaEditorModule::SetupPackagingSettings()` 则自动把 `Script`、`../Plugins/UnLua/Content/Script` 以及 `UnLuaExtensions` 插件里的 `Content/Script` 目录加入 `DirectoriesToAlwaysStageAsUFS`。这说明 `UnLua` 的主交付物是**原始 `.lua` 文件目录树**，通过 UE 内容打包规则带进包体，运行时继续按路径读取。

`Angelscript` 的形状不同。它会发现 `ProjectDir/Script` 与所有启用插件的 `PluginBaseDir/Script` 作为源码根，但启动期同时加载 `Binds.Cache`、`BindModules.Cache`，并在合适条件下读取 `PrecompiledScript[_Shipping|_Test|_Development].Cache`。一旦进入 fully precompiled 模式，源码还会明确提示热重载关闭。也就是说，`Angelscript` 不是单纯“把脚本文本一起带上”，而是允许交付**脚本源码 + 绑定缓存 + 预编译缓存**这组三层产物。

```
[D11-Deep] Shipping Artifact Shape
UnLua
├─ ProjectContent/Script
├─ Plugins/UnLua/Content/Script
├─ DirectoriesToAlwaysStageAsUFS
└─ runtime loads raw .lua from staged content

Angelscript
├─ ProjectDir/Script + PluginBaseDir/Script
├─ Binds.Cache / BindModules.Cache
├─ PrecompiledScript[_Config].Cache
└─ runtime may bypass preprocessing and disable hot reload
```

关键源码 [1]：`UnLua` 把脚本根和打包目录都固定为“原始脚本内容”

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaFunctionLibrary.cpp
// 函数: UUnLuaFunctionLibrary::GetScriptRootPath
// 行号: 20-23
// 位置: 运行时脚本根直接指向 ProjectContent/Script
// ============================================================================
FString UUnLuaFunctionLibrary::GetScriptRootPath()
{
    return FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() + TEXT("Script/")); // ★ 主交付物就是内容目录里的原始 lua
}
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp
// 函数: FUnLuaEditorModule::SetupPackagingSettings
// 行号: 186-239
// 位置: editor 启动期自动把脚本目录注入 UFS 打包列表
// ============================================================================
auto ScriptPaths = TArray<FString>{TEXT("Script"), TEXT("../Plugins/UnLua/Content/Script")};
...
if (!Plugin->CanContainContent())
    continue;
...
auto ScriptPath = ContentDir / "Script";
if (FPaths::MakePathRelativeTo(ScriptPath, *FPaths::ProjectContentDir()))
    ScriptPaths.Add(ScriptPath);                                      // ★ 扩展插件脚本也按内容目录 staging
...
PackagingSettings->DirectoriesToAlwaysStageAsUFS.Add(DirectoryPath);  // ★ 最终交付的是 staged raw script
```

关键源码 [2]：`Angelscript` 同时管理源码根、绑定缓存和预编译缓存

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: FAngelscriptEngineDependencies::CreateDefault / DiscoverScriptRoots
// 行号: 558-565, 1326-1363
// 位置: 先发现 project/plugin 脚本根，再交给 runtime 统一管理
// ============================================================================
Dependencies.GetEnabledPluginScriptRoots = []()
{
    TArray<FString> ScriptRoots;
    for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetEnabledPluginsWithContent())
    {
        ScriptRoots.Add(Plugin->GetBaseDir() / TEXT("Script"));        // ★ 插件脚本根不走 Content/Script，而是插件根下的 Script
    }
    return ScriptRoots;
};
...
FString RootPath = Dependencies.ConvertRelativePathToFull(Dependencies.GetProjectDir() / TEXT("Script"));
...
for (const FString& PluginScriptRoot : Dependencies.GetEnabledPluginScriptRoots())
{
    const FString ScriptPath = Dependencies.ConvertRelativePathToFull(PluginScriptRoot);
    if (Dependencies.DirectoryExists(ScriptPath))
        DiscoveredRootPaths.Add(ScriptPath);
}
DiscoveredRootPaths.Insert(RootPath, 0);                              // ★ project script root 始终排在最前
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: Initialize_AnyThread / InitialCompile
// 行号: 1425-1477, 1513-1534, 1583-1587, 2046-2056
// 位置: 交付期既消费 Binds.Cache/BindModules.Cache，也可切到 PrecompiledScript.Cache
// ============================================================================
bGeneratePrecompiledData = RuntimeConfig.bGeneratePrecompiledData;
bScriptDevelopmentMode = RuntimeConfig.bIsEditor || RuntimeConfig.bDevelopmentMode;
bUsePrecompiledData = !bGeneratePrecompiledData && !RuntimeConfig.bIgnorePrecompiledData
    && !RuntimeConfig.bRunningCommandlet && !WITH_EDITOR && !bScriptDevelopmentMode;
...
FAngelscriptBindDatabase::Get().Load(GetScriptRootDirectory() / TEXT("Binds.Cache"), bGeneratePrecompiledData); // ★ 绑定缓存是正式交付物
...
FAngelscriptBinds::LoadBindModules(plugin->GetBaseDir() / "BindModules.Cache");                                  // ★ 插件侧还有 bind module cache
...
if (bUsePrecompiledData)
{
    FString Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript_Development.Cache");
    if (!IFileManager::Get().FileExists(*Filename))
        Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript.Cache");
    if (IFileManager::Get().FileExists(*Filename))
    {
        PrecompiledData = new FAngelscriptPrecompiledData(Engine);
        PrecompiledData->Load(Filename);                          // ★ 运行时可直接吃预编译缓存
    }
}
...
if (bGeneratePrecompiledData)
{
    FString Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript.Cache");
    PrecompiledData->InitFromActiveScript();
    PrecompiledData->Save(Filename);                             // ★ 也能反向产出预编译交付物
}
...
if (PrecompiledData != nullptr && bUsePrecompiledData && !bScriptDevelopmentMode)
{
    ModulesToCompile = PrecompiledData->GetModulesToCompile();
    UE_LOG(Angelscript, Warning, TEXT("Using fully precompiled scripts. Hot reloading is disabled for this run."));
    UE_LOG(Angelscript, Warning, TEXT("Delete PrecompiledScript.Cache or run with -as-development-mode flag to enable hot reload."));
}
```

新增对比结论：

- `UnLua` 的部署契约偏“内容型”：把原始脚本目录作为 staged content 带进包体，运行时继续按路径读取。
- `Angelscript` 的部署契约偏“构建产物型”：源码根仍存在，但真正重要的交付物还包括 bind cache 与 precompiled cache。
- 这会直接影响后续能力取舍：`UnLua` 更自然支持 raw script 替换与 sample-style 扩展；`Angelscript` 更自然支持 cooked/precompiled 启动闭环。

差距判断：

- 相对 `Angelscript` 这种 `Binds.Cache + BindModules.Cache + PrecompiledScript.Cache` 三层交付，`UnLua` 当前属于 `没有实现同层预编译缓存交付模型`。
- 相对 `UnLua` 这种自动把脚本目录并入 `DirectoriesToAlwaysStageAsUFS` 的内容型发布路径，`Angelscript` 主要是 `实现方式不同`；当前重点在编译缓存而不是 raw script staging。
- 如果目标是热更新 raw script 和样例工程直观性，`UnLua` 更直接；如果目标是减少运行时预处理/编译成本，`Angelscript` 这里体现为 `实现质量差异` 优势。

---
## 深化分析 (2026-04-09 00:47:38)

### [维度 D3] Blueprint 重编译后的覆写恢复契约：`UnLua` 修补被清空的 `FunctionMap`，`Angelscript` 直接重建 class/action graph

前文多次比较过 `BlueprintOverride` 的语义面，这一轮补的是**Blueprint 重新编译之后，谁负责把脚本覆写重新接回编辑器与运行时**。`UnLua` 的答案是“继续 patch 现有 `UClass`，并在发现 `FuncMap` 被清空时重放 override”；`Angelscript` 的答案是“让 class generator 重新生成 `UFunction`，然后显式刷新 Blueprint action database 和 compiled 广播”。

这意味着两者差异不只在“Lua 覆写 vs Angelscript Mixin”，还在**重编译恢复的所有权落点**：`UnLua` 主要修补旧类上的槽位，`Angelscript` 主要维护新旧类切换时的 editor/runtime 一致性。

```
[D3-Deep] Blueprint Recompile Recovery
UnLua
├─ BindClass() patches existing UFunction          // 绑定时直接替换现有槽位
├─ Blueprint recompile may clear FuncMap           // UE 编辑器重编译会破坏类函数映射
├─ "__UClassBindSucceeded" sentinel detects state  // 通过哨兵判断该类曾经成功绑定
└─ RestoreOverrides(Class) replays Lua thunks      // 再次把 execScriptCallLua 接回去

Angelscript
├─ ClassGenerator creates new UFunction            // 重载时重新生成函数对象
├─ AddFunctionToFunctionMap(NewFunction)           // 新函数进入新类映射
├─ ClassReloadHelper refreshes old/new actions     // 显式刷新 BlueprintActionDatabase
└─ BroadcastBlueprintCompiled() on full reload     // 让编辑器按正式编译流程更新视图
```

关键源码 [1]：`UnLua` 明确把“Blueprint Recompile 清空 `FuncMap`”当成运行时恢复分支处理

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaManager.cpp
// 函数: UUnLuaManager::BindClass
// 行号: 260-340
// 位置: 绑定类时检查 Blueprint recompile 后的函数映射状态，并在需要时重放 override
// ============================================================================
if (Classes.Contains(Class))
{
#if WITH_EDITOR
    // ★ Blueprint Recompile 后，类上可能还“记得”绑定过，但 FuncMap 已被清空
    if (Class->FindFunctionByName("__UClassBindSucceeded", EIncludeSuperFlag::Type::ExcludeSuper))
        return true;

    ULuaFunction::RestoreOverrides(Class); // ★ 重新把覆写函数接回当前类
#else
    return true;
#endif
}
...
for (const auto& LuaFuncName : BindInfo.LuaFunctions)
{
    UFunction** Func = BindInfo.UEFunctions.Find(LuaFuncName);
    if (Func)
    {
        UFunction* Function = *Func;
        ULuaFunction::Override(Function, Class, LuaFuncName); // ★ 继续 patch 旧类上的 UFunction
    }
}
...
#if WITH_EDITOR
// ★ 用哨兵名标记“这个类已经成功绑过”，用于识别重编译后的恢复路径
for (const auto& Iter : BindInfo.UEFunctions)
{
    auto& FuncName = Iter.Key;
    auto& Function = Iter.Value;
    if (Class->FindFunctionByName(FuncName, EIncludeSuperFlag::Type::ExcludeSuper))
    {
        Class->AddFunctionToFunctionMap(Function, "__UClassBindSucceeded");
        break;
    }
}
#endif
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaOverridesClass.cpp
// 函数: ULuaOverridesClass::Create / SetActive
// 行号: 19-29, 39-55
// 位置: 覆写类本身是 transient patch 容器，激活/停用时只刷新 owner 的函数缓存
// ============================================================================
ULuaOverridesClass* ULuaOverridesClass::Create(UClass* Class)
{
    ...
    Ret->ClassFlags |= CLASS_NewerVersionExists; // ★ 直接绕过 BlueprintActionDatabase 的常规刷新路径
    ...
    Ret->Owner = Class;
    Ret->AddToOwner();
    return Ret;
}

void ULuaOverridesClass::SetActive(const bool bActive)
{
    const auto Class = Owner.Get();
    if (!Class)
        return;

    for (TFieldIterator<ULuaFunction> It(this, EFieldIteratorFlags::ExcludeSuper); It; ++It)
    {
        const auto LuaFunction = *It;
        LuaFunction->SetActive(bActive);           // ★ 真正的 patch/repatch 发生在每个 ULuaFunction 上
    }

    Class->ClearFunctionMapsCaches();             // ★ 恢复之后只清当前 owner 的函数缓存
    if (bActive)
        AddToOwner();
    else
        RemoveFromOwner();
}
```

关键源码 [2]：`Angelscript` 把重载恢复建模为“重建新类 + 刷新 editor action graph”

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 函数: class generation path
// 行号: 2820-2830, 3615-3624
// 位置: 热重载/类生成时不是 patch 旧槽位，而是把新的 UFunction 链进新类
// ============================================================================
UFunction* NewFunction = NewObject<UFunction>(NewClass, *FuncName, RF_Public);
NewFunction->FunctionFlags = FUNC_Event | FUNC_BlueprintEvent | FUNC_Public;
...
NewFunction->Next = NewClass->Children;
NewClass->Children = NewFunction;
NewClass->AddFunctionToFunctionMap(NewFunction, NewFunction->GetFName()); // ★ 新类直接拥有新的函数映射
...
// Link into class
NewFunction->Next = NewClass->Children;
NewClass->Children = NewFunction;

// ★ 所有新生成函数都被正式加回 owner class 的 function map
NewClass->AddFunctionToFunctionMap(NewFunction, NewFunction->GetFName());

NewFunction->StaticLink(true);
NewFunction->FinalizeArguments();
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h
// 函数: class reload delegates
// 行号: 79-95, 139-154
// 位置: editor 侧显式刷新 BlueprintActionDatabase，并在 full reload 后广播 BlueprintCompiled
// ============================================================================
if (OldClass != nullptr)
{
    if (!bRefreshedAll && GEngine != nullptr)
    {
        auto& Database = FBlueprintActionDatabase::Get();
        Database.RefreshClassActions(OldClass);   // ★ 旧类 action graph 显式刷新
    }
}

if (NewClass != nullptr)
{
    if (!bRefreshedAll && GEngine != nullptr)
    {
        auto& Database = FBlueprintActionDatabase::Get();
        Database.RefreshClassActions(NewClass);   // ★ 新类 action graph 也显式刷新
    }
}
...
if (ReloadState().bRefreshAllActions && GEngine != nullptr)
{
    auto& Database = FBlueprintActionDatabase::Get();
    Database.RefreshAll();
}

if (bFullReload && GEditor != nullptr)
{
    GEditor->BroadcastBlueprintCompiled();        // ★ 让编辑器按“正式蓝图编译完成”路径同步 UI/菜单
}
```

新增对比结论：

- `UnLua` 的 Blueprint 恢复模型是“旧类 patch 的重放机制”：核心证据是 `__UClassBindSucceeded` 哨兵、`RestoreOverrides(Class)` 和 `ClearFunctionMapsCaches()`。
- `Angelscript` 的 Blueprint 恢复模型是“新类生成 + editor action refresh”：核心证据是 `NewClass->AddFunctionToFunctionMap()` 与 `ClassReloadHelper` 对 `FBlueprintActionDatabase` / `BroadcastBlueprintCompiled()` 的显式调用。
- 因而两者差异不只是覆写语法，而是**重编译后 editor contract 的归属方式**。

差距判断：

- `UnLua` 在 Blueprint 重编译恢复上不是 `没有实现`；源码明确显示它已经实现恢复分支。
- 相对 `Angelscript` 这种“新类重建 + action database 显式刷新”的模式，`UnLua` 这里主要是 `实现方式不同`。
- 如果关注的是 editor action graph 与 class list 的显式一致性，`Angelscript` 证据链更完整；这可以视为局部 `实现质量差异`，但不是功能有无差异。

### [维度 D9] 测试/CI 契约会反向进入构建链：`UnLua` 的 UHT 补丁与测试插件彼此牵制，`Angelscript` 把生成产物做成可断言的 CI artifact

这一轮补的是前文没有单独拎出来的一点：**测试基础设施会不会反过来影响构建与 CI 契约本身**。`UnLua` 并不是没有 build-time tool，它有 `UnLuaDefaultParamCollectorUbtPlugin` 来生成 `DefaultParamCollection.inl`；但同仓库里的 `UnLuaTestSuite` 又声明了 `CanBeUsedWithUnrealHeaderTool=true`，意味着测试插件本身也会进入 UHT 参与面。`Angelscript` 则把 UHT 生成链直接定义成可验证产物，随后用 automation 测试去校验 `json/csv` 输出是否自洽。

换句话说，`UnLua` 的测试/构建边界更容易互相影响；`Angelscript` 的生成链更像一个“必须产出并必须被核对”的 CI artifact contract。

```
[D9-Deep] Build/Test Contract Ownership
UnLua
├─ UnLuaDefaultParamCollectorUbtPlugin           // 构建期收集默认参数
│  └─ writes DefaultParamCollection.inl
├─ UnLuaTestSuite.uplugin                        // 测试插件也声明进入 UHT
│  └─ CanBeUsedWithUnrealHeaderTool = true
└─ test plugin can affect full-C# UHT adoption   // 测试层和构建层边界不完全隔离

Angelscript
├─ AngelscriptFunctionTableExporter              // UHT 导出绑定函数表
│  ├─ AS_FunctionTable_Summary.json
│  ├─ AS_FunctionTable_ModuleSummary.csv
│  ├─ AS_FunctionTable_Entries.csv
│  └─ AS_FunctionTable_Skipped*.csv
└─ GeneratedFunctionTableTests                   // automation 直接校验这些产物
```

关键源码 [1]：`UnLua` 的构建辅助模块负责生成默认参数表，但测试插件同样声明可被 UHT 调用

```csharp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaDefaultParamCollectorUbtPlugin/UnLuaDefaultParamCollectorUbtPlugin.cs
// 函数: ctor / Generate / ParseModule / Finish
// 行号: 25-69, 388-429
// 位置: C# UHT 插件只负责导出 DefaultParamCollection.inl
// ============================================================================
public UnLuaDefaultParamCollectorUbtPlugin(IUhtExportFactory factory)
{
    Factory = factory;
    ...
    GeneratedContentBuilder.Append("// Generated By C# UbtPlugin\r\n");
    GeneratedContentBuilder.Append("FFunctionCollection* FC = nullptr;\r\n");
    GeneratedContentBuilder.Append("FParameterCollection* PC = nullptr;\r\n");
    ...                                             // ★ 产物是默认参数数据库，不是诊断/覆盖率报表
}

private void Generate()
{
    foreach (UhtPackage package in Session.Packages)
    {
        var moduleType = package.Module.ModuleType;
        ParseModule(package.Module.Name, moduleType, package.Module.OutputDirectory);
        if (moduleType != UHTModuleType.EngineRuntime && moduleType != UHTModuleType.GameRuntime)
        {
            continue;                               // ★ 只扫 runtime 模块
        }
        QueueClassExports(package, package);
    }
}
...
GeneratedContentBuilder.AppendFormat("FC = &GDefaultParamCollection.Add(TEXT(\"{0}\"));\r\n", classObj.EngineNamePrefix + classObj.EngineName);
GeneratedContentBuilder.AppendFormat("PC = &FC->Functions.Add(TEXT(\"{0}\"));\r\n", function.StrippedFunctionName);
...
string filePath = Factory.MakePath("DefaultParamCollection", ".inl");
if (File.Exists(filePath))
{
    ...
    Factory.CommitOutput(filePath, GeneratedContentBuilder);   // ★ 最终提交的是 .inl 文件
}
```

```json
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/UnLuaTestSuite.uplugin
// 位置: 测试插件元数据
// 行号: 13-17
// 位置: 测试插件本身声明可以参与 UHT
// ============================================================================
"CanContainContent": true,
"IsBetaVersion": false,
"Installed": false,
"CanBeUsedWithUnrealHeaderTool": true,            // ★ 测试插件也进入 UHT 参与面
"EnabledByDefault": false,
```

关键源码 [2]：`Angelscript` 直接把 UHT 生成物做成 summary/csv，并用测试校验这些产物

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs
// 函数: Export
// 行号: 21-47
// 位置: UHT exporter 不只生成 C++ shard，还统计 skipped entries 与失败原因
// ============================================================================
[UhtExporter(
    Name = "AngelscriptFunctionTable",
    Description = "Exports Angelscript function table data",
    Options = UhtExporterOptions.Default | UhtExporterOptions.CompileOutput,
    CppFilters = ["AS_FunctionTable_*.cpp"],
    ModuleName = "AngelscriptRuntime")]
private static void Export(IUhtExportFactory factory)
{
    ...
    int generatedFileCount = AngelscriptFunctionTableCodeGenerator.Generate(factory);
    ...
    WriteSkippedEntriesCsv(factory, skippedEntries);           // ★ 把 skipped 明细写成 csv
    WriteSkippedReasonSummaryCsv(factory, skippedEntries);     // ★ 把失败原因聚合也写成 csv
    ...
}
```

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs
// 函数: WriteGenerationSummary / WriteModuleSummaryCsv / WriteEntryCsv
// 行号: 166-246
// 位置: 生成阶段同步产出 json/csv 统计物，供测试和 CI 消费
// ============================================================================
string summaryPath = factory.MakePath("AS_FunctionTable_Summary", ".json");
...
string summaryJson = JsonSerializer.Serialize(
    new
    {
        totalGeneratedEntries,
        totalDirectBindEntries,
        totalStubEntries,
        directBindRate,
        stubRate,
        totalShardCount = generatedFileCount,
        moduleCount = moduleSummaries.Count,
        modules = moduleSummaries.Select(summary => new
        {
            moduleName = summary.ModuleName,
            editorOnly = summary.EditorOnly,
            totalEntries = summary.TotalEntries,
            directBindEntries = summary.DirectBindEntries,
            stubEntries = summary.StubEntries,
            ...
        }),
    },
    new JsonSerializerOptions { WriteIndented = true });

File.WriteAllText(summaryPath, summaryJson, Encoding.UTF8);    // ★ 先写 summary json
WriteModuleSummaryCsv(factory, moduleSummaries);               // ★ 再写 module summary csv
WriteEntryCsv(factory, csvEntries);                            // ★ 再写 entry detail csv
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp
// 函数: SummaryOutputTest / CsvOutputTest / SkippedReasonSummaryCsvOutputTest
// 行号: 459-527, 594-645, 706-727
// 位置: automation 直接断言 UHT 产物内容，而不是只断言“生成成功”
// ============================================================================
const FString SummaryPath = FPaths::Combine(GeneratedDirectory, TEXT("AS_FunctionTable_Summary.json"));
...
TestEqual(TEXT("Generated function table summary test should match the generated binding registration count"), TotalGeneratedEntries, CountedRegistrations);
TestTrue(TEXT("Generated function table summary test should keep directBindRate and stubRate normalized"), FMath::Abs((DirectBindRate + StubRate) - 1.0) < 1.e-9);
...
const FString ModuleCsvPath = FPaths::Combine(GeneratedDirectory, TEXT("AS_FunctionTable_ModuleSummary.csv"));
const FString EntryCsvPath = FPaths::Combine(GeneratedDirectory, TEXT("AS_FunctionTable_Entries.csv"));
...
TestEqual(TEXT("Generated function table csv test should write the expected module csv header"), ModuleLines[0], TEXT("ModuleName,EditorOnly,TotalEntries,DirectBindEntries,StubEntries,DirectBindRate,StubRate,ShardCount"));
TestEqual(TEXT("Generated function table csv test should write the expected entry csv header"), EntryLines[0], TEXT("ModuleName,EditorOnly,ClassName,FunctionName,EntryKind,EraseMacro,ShardIndex"));
...
const FString ReasonSummaryCsvPath = FPaths::Combine(GeneratedDirectory, TEXT("AS_FunctionTable_SkippedReasonSummary.csv"));
...
TestEqual(TEXT("Generated function table skipped reason summary test should write the expected summary csv header"), SummaryLines[0], TEXT("FailureReason,SkippedCount"));
```

新增对比结论：

- `UnLua` 不是没有构建期代码生成；它已经实现了默认参数收集的 UHT 插件。
- 但从源码组织看，`UnLuaTestSuite` 这个测试插件也进入 UHT 参与面，说明测试层与构建层边界并不完全隔离。
- `Angelscript` 的不同点不是“也有 UHT 工具”这么简单，而是它把 `summary/csv/skipped-reason` 当成正式 CI artifact，并为这些 artifact 写了 automation 断言。

差距判断：

- 相对 `Angelscript` 这种“生成物即 CI artifact”的做法，`UnLua` 当前属于 `没有实现同层 machine-verifiable 生成产物断言`。
- `UnLua` 的默认参数收集工具本身不是 `没有实现`，它属于 `实现方式不同`：更偏解决编译期默认参数可用性，而不是生成覆盖率/跳过原因账本。
- `UnLuaTestSuite` 进入 UHT 参与面的设计，对样例工程和插件自测很方便，但在 CI 纯净性上相对 `Angelscript` 属于局部 `实现质量差异`。

### [维度 D10] Onboarding 入口被做成编辑器功能：`UnLua` 提供 asset→module→template 的专用脚手架，`Angelscript` 当前更偏通用 editor prompt

前文已经分析过 `Docs/`、`Tutorials/`、`README` 的组织。这一轮补一个更靠近“第一天上手”的点：**仓库有没有把 onboarding 文档直接做成 editor 里的专用动作**。`UnLua` 的答案是“有，而且链路是闭环的”。

`FUnLuaEditorToolbar` 在绑定 Blueprint 到 Lua 时，不只是给你一段文档说明，而是自动实现 `UnLuaInterface`、按 `Alt` 或 `ULuaModuleLocator` 规则填好 `GetModuleName`、编译并打开该图；随后 `CreateLuaTemplate_Executed()` 又会沿父类链去找 `Config/LuaTemplates/*.lua`，最终直接把脚本文件落盘。相比之下，`AngelscriptEditor` 当前公开出来的主入口更偏**通用型编辑器 prompt**：选择资产、弹窗填参数、调用已有 editor extension function，而不是专门为“把某个资产接到脚本上”提供一条固定脚手架。

```
[D10-Deep] Onboarding Entry Path
UnLua
├─ BindToLua toolbar action                      // 一键给 Blueprint 挂接口
│  ├─ ModuleLocator or Alt derives module path   // 自动确定 module 名
│  ├─ compile and open GetModuleName graph       // 直接把用户带到要编辑的位置
│  └─ CreateLuaTemplate walks parent templates   // 沿父类模板链生成脚本文件
└─ asset -> module -> script file                // onboarding 是一条专用流水线

Angelscript
├─ ScriptAssetMenuExtension gathers functions    // 先收集可对当前资产执行的函数
├─ ScriptEditorPrompts builds parameter dialog   // 通过通用参数窗体补输入
└─ executes existing editor extension function   // 更像“编辑器扩展执行器”而非专用脚手架
```

关键源码 [1]：`UnLua` 的 toolbar 直接把“绑定 + 定位 + 生成模板”做成一条 editor 动作链

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp
// 函数: BindToLua_Executed / CreateLuaTemplate_Executed
// 行号: 150-199, 257-313
// 位置: editor 工具栏不是只弹说明，而是直接修改 Blueprint、填模块名并生成脚本模板
// ============================================================================
const auto Ok = FBlueprintEditorUtils::ImplementNewInterface(Blueprint, FTopLevelAssetPath(UUnLuaInterface::StaticClass()));
if (!Ok)
    return;

FString LuaModuleName;
const auto ModifierKeys = FSlateApplication::Get().GetModifierKeys();
const auto bIsAltDown = ModifierKeys.IsLeftAltDown() || ModifierKeys.IsRightAltDown();
if (bIsAltDown)
{
    const auto Package = Blueprint->GetTypedOuter(UPackage::StaticClass());
    LuaModuleName = Package->GetName().RightChop(6).Replace(TEXT("/"), TEXT(".")); // ★ Alt 直接按包路径推导 module 名
}
else
{
    const auto Settings = GetDefault<UUnLuaSettings>();
    if (Settings && Settings->ModuleLocatorClass)
    {
        const auto ModuleLocator = Cast<ULuaModuleLocator>(Settings->ModuleLocatorClass->GetDefaultObject());
        LuaModuleName = ModuleLocator->Locate(TargetClass);                           // ★ 否则走可配置的 ModuleLocator
    }
}

if (!LuaModuleName.IsEmpty())
{
    ...
    InterfaceDesc.Graphs[0]->Nodes[1]->Pins[1]->DefaultValue = LuaModuleName;       // ★ 直接把 GetModuleName 的默认值写进去
}
...
MyBlueprintEditor->Compile();
const auto Func = Blueprint->GeneratedClass->FindFunctionByName(FName("GetModuleName"));
const auto GraphToOpen = FBlueprintEditorUtils::FindScopeGraph(Blueprint, Func);
MyBlueprintEditor->OpenGraphAndBringToFront(GraphToOpen);                            // ★ 编译后直接打开目标图
...
FString ModuleName;
Class->GetDefaultObject()->ProcessEvent(Func, &ModuleName);
...
for (auto TemplateClass = Class; TemplateClass; TemplateClass = TemplateClass->GetSuperClass())
{
    auto TemplateClassName = TemplateClass->GetName().EndsWith("_C") ? TemplateClass->GetName().LeftChop(2) : TemplateClass->GetName();
    auto RelativeFilePath = "Config/LuaTemplates" / TemplateClassName + ".lua";
    auto FullFilePath = FPaths::ProjectConfigDir() / RelativeFilePath;
    if (!FPaths::FileExists(FullFilePath))
        FullFilePath = BaseDir / RelativeFilePath;                                   // ★ 先查项目模板，再回退到插件模板
    ...
    Content = Content.Replace(TEXT("TemplateName"), *TemplateName)
                     .Replace(TEXT("ClassName"), *UnLua::IntelliSense::GetTypeName(Class));

    FFileHelper::SaveStringToFile(Content, *FileName, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM); // ★ 直接落盘 .lua
    break;
}
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaModuleLocator.cpp
// 函数: ULuaModuleLocator::Locate / ULuaModuleLocator_ByPackage::Locate
// 行号: 18-65
// 位置: module 定位规则本身也是可扩展代码，而不是写死在文档里
// ============================================================================
FString ULuaModuleLocator::Locate(const UObject* Object)
{
    ...
    if (!CDO->GetClass()->ImplementsInterface(UUnLuaInterface::StaticClass()))
    {
        return "";
    }

    return IUnLuaInterface::Execute_GetModuleName(CDO);              // ★ 默认定位规则就是读取接口实现
}

FString ULuaModuleLocator_ByPackage::Locate(const UObject* Object)
{
    const auto Class = Object->IsA<UClass>() ? static_cast<const UClass*>(Object) : Object->GetClass();
    const auto Key = Class->GetFName();
    const auto Cached = Cache.Find(Key);
    if (Cached)
        return *Cached;

    FString ModuleName;
    if (Class->IsNative())
    {
        ModuleName = Class->GetName();
    }
    else
    {
        ModuleName = Object->GetOutermost()->GetName();
        ...
        ModuleName = ModuleName.Replace(TEXT("/"), TEXT(".")).RightChop(ChopCount); // ★ Blueprint 资产可直接按包路径转 module 名
    }
    Cache.Add(Key, ModuleName);
    return ModuleName;
}
```

关键源码 [2]：`AngelscriptEditor` 当前公开入口更偏“执行已有扩展函数”，不是专用脚手架

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptAssetMenuExtension.cpp
// 函数: UScriptAssetMenuExtension::CallFunctionOnSelection
// 行号: 41-115
// 位置: 资产菜单扩展的核心工作是根据选择对象组织参数，然后调用现有函数
// ============================================================================
void UScriptAssetMenuExtension::CallFunctionOnSelection(UFunction* Function, FExtenderSelection Selection) const
{
    ...
    for (const FAssetData& Asset : Selection.SelectedAssets)
    {
        if (!SupportsAsset(Asset))
            continue;
        ...
        if (bFunctionTakesObject)
            CallWithObjects.Add(Asset.GetAsset());
        else
            CallWithStructs.Add(MakeShared<FStructOnScope>(AssetDataStruct, (uint8*)&Asset));
    }

    FScriptEditorPromptOptions Options;
    if (CallWithStructs.Num() != 0)
    {
        FScriptEditorPrompts::ShowPromptToCallFunction(
            this,
            Function->GetFName(),
            Options,
            CallWithStructs);                                       // ★ 用 prompt 调现有扩展函数
    }
    else if (CallWithObjects.Num() != 0)
    {
        FScriptEditorPrompts::ShowPromptToCallFunction(
            this,
            Function->GetFName(),
            Options,
            CallWithObjects);
    }
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorPrompts.cpp
// 函数: FScriptEditorPrompts::ShowPromptToCallFunction
// 行号: 182-245
// 位置: prompt 系统负责构建参数窗体并执行函数，强调的是通用调用，而不是脚本模板生成
// ============================================================================
bool FScriptEditorPrompts::ShowPromptToCallFunction(const UObject* Object, FName FunctionName, FScriptEditorPromptOptions Options, TArray<UObject*> FirstParameterObjects)
{
    if (Object == nullptr)
        return false;

    UFunction* Function = Object->GetClass()->FindFunctionByName(FunctionName);
    if (Function == nullptr)
        return false;

    TSharedRef<FStructOnScope> FuncParams = MakeShared<FStructOnScope>(Function);    // ★ 先为任意函数创建参数结构
    ...
    if (UEdGraphSchema_K2::FindFunctionParameterDefaultValue(Function, *It, Defaults))
    {
        It->ImportText_Direct(*Defaults, It->ContainerPtrToValuePtr<uint8>(FuncParams->GetStructMemory()), nullptr, PPF_None);
    }
    ...
    if (bHasParametersToFill)
    {
        if (Options.WindowTitle.IsEmpty())
            Options.WindowTitle = Function->GetDisplayNameText();

        bool bProceed = ShowPromptForStruct(FuncParams, Options);                     // ★ 通用参数窗体
        if (!bProceed)
            return false;
    }

    FEditorScriptExecutionGuard ScriptGuard;
    if (FirstParameterObjects.Num() != 0 && FirstParamProperty != nullptr)
    {
        for (UObject* ParamObject : FirstParameterObjects)
        {
            ...
        }
    }
}
```

新增对比结论：

- `UnLua` 的 onboarding 不只是 `Docs/Quickstart`；源码里存在一条专门的 asset→module→template editor 流水线。
- `Angelscript` 也不是没有 editor tooling；它有通用的 `ScriptAssetMenuExtension + ScriptEditorPrompts` 机制，但这套机制更像“执行编辑器扩展函数”的基础设施。
- 因而两者差异不是“有工具 vs 没工具”，而是**专用 onboarding scaffold** 是否被正式产品化。

差距判断：

- 相对 `UnLua` 这一套 `GetModuleName + ModuleLocator + LuaTemplates` 专用脚手架，`Angelscript` 当前属于 `没有实现同层 asset-to-script onboarding scaffold`。
- 但如果把比较对象限定为“editor extensibility”，`Angelscript` 并不是 `没有实现`，而是 `实现方式不同`：它提供的是通用 prompt/扩展函数调用框架。
- 如果目标是新用户首次把资产接入脚本的路径最短化，`UnLua` 在当前快照上体现为明确的 `实现质量差异` 优势。

---

## 深化分析 (2026-04-09 01:00:37)

### [维度 D3] 输入事件的 ownership：`UnLua` 接管 Blueprint 现有输入槽位，`Angelscript` 暴露显式绑定原语

前文已经把 `UnLuaInterface/GetModuleName` 与 `BlueprintOverride/Mixin` 的主路径讲过了，这一轮只补一个更具体、但直接影响脚本接入工作流的点：**输入事件究竟是谁来“接管”**。`UnLua` 不只是让 Lua 覆写普通 `UFunction`；它还把 Blueprint 输入系统纳入同一套“现有类行为面 patch”模型。`Input.lua` 先把 `BindKey/BindAction/BindAxis` 变成 `Module.__UnLuaInputBindings` 里的闭包和 `UnLuaInput_*` 隐藏函数名，`UUnLuaManager::BindClass()` 再在 `UBlueprintGeneratedClass` 上执行 `UnLua.Input.PerformBindings()`，随后 `ReplaceActionInputs()/ReplaceAxisInputs()` 对活体 `UInputComponent` 重新绑 delegate，甚至在缺失成对事件时主动补 `Released` 绑定。

`Angelscript` 当前快照的 ownership 则明显不同。它不是让某个 manager 扫描脚本函数命名，再反向 patch 现有输入图；而是把 `UInputComponent::BindAction/BindAxis`、`UEnhancedInputComponent::BindAction/BindDebugKey` 这些原语直接暴露给脚本作者。脚本显式拿到组件，再显式绑定 delegate，输入系统保持“组件持有绑定”的 UE 原生形状。两边都支持脚本化输入，但一个偏**接管现有 Blueprint 输入槽位**，另一个偏**让脚本显式声明输入绑定**。

```
[D3-Deep] Input Binding Ownership
UnLua
├─ Lua module calls UnLua.Input.BindAction(...)     // Lua 先声明输入意图
│  └─ stores __UnLuaInputBindings closures          // 把绑定动作缓存到模块表
├─ UUnLuaManager::BindClass()                       // 绑定类时执行 PerformBindings
│  └─ injects Blueprint DynamicBindingObjects       // 把输入绑定写进 BPGC
└─ ReplaceActionInputs / ReplaceAxisInputs          // Actor 启用输入后再 patch 活体组件
   └─ rebinds delegates by generated function name  // 用隐藏函数名重绑 delegate

Angelscript
├─ script gets UInputComponent / UEnhancedInputComponent // 脚本直接拿组件
├─ calls BindAction / BindAxis / BindDebugKey           // 显式调用绑定 API
└─ component stores delegate directly                   // 绑定归组件自己持有
```

关键源码 [1]：`UnLua` 先在 Lua 层缓存输入绑定动作，再在类绑定期统一执行

```lua
-- ============================================================================
-- 文件: Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/Input.lua
-- 函数: M.BindKey / M.BindAction / M.BindAxis / M.PerformBindings
-- 行号: 46-70, 85-106, 115-148
-- 位置: Lua 模块先记录输入绑定闭包，真正注入 Blueprint 发生在 PerformBindings
-- ============================================================================
function M.BindKey(Module, KeyName, KeyEvent, Handler, Args)
    Args = Args or {}
    Module.__UnLuaInputBindings = Module.__UnLuaInputBindings or {}
    local FunctionName = MakeLuaFunction(Module, string.format("UnLuaInput_%s_%s%s", KeyName, KeyEvent, ModifierSuffix), Handler, 0)
    local Bindings = Module.__UnLuaInputBindings
    table.insert(Bindings, function(Manager, Class)
        local BindingObject = Manager:GetOrAddBindingObject(Class, UE.UInputKeyDelegateBinding)
        ...
        Binding.FunctionNameToBind = FunctionName                    -- ★ Blueprint 侧实际绑定的是隐藏函数名
        BindingObject.InputKeyDelegateBindings:Add(Binding)
        Manager:Override(Class, "InputAction", FunctionName)         -- ★ 再把入口 thunk 切到 Lua
    end)
end

function M.BindAction(Module, ActionName, KeyEvent, Handler, Args)
    Module.__UnLuaInputBindings = Module.__UnLuaInputBindings or {}
    local FunctionName = MakeLuaFunction(Module, string.format("UnLuaInput_%s_%s", ActionName, KeyEvent), Handler, 0)
    table.insert(Module.__UnLuaInputBindings, function(Manager, Class)
        local BindingObject = Manager:GetOrAddBindingObject(Class, UE.UInputActionDelegateBinding)
        Binding.InputActionName = ActionName
        Binding.InputKeyEvent = UE.EInputEvent["IE_" .. KeyEvent]
        Binding.FunctionNameToBind = FunctionName                    -- ★ Action 名先翻译成 Blueprint binding object
        BindingObject.InputActionDelegateBindings:Add(Binding)
        Manager:Override(Class, "InputAction", FunctionName)
    end)
end

function M.BindAxis(Module, AxisName, Handler, Args)
    Module.__UnLuaInputBindings = Module.__UnLuaInputBindings or {}
    local FunctionName = MakeLuaFunction(Module, string.format("UnLuaInput_%s", AxisName), Handler, 0)
    table.insert(Module.__UnLuaInputBindings, function(Manager, Class)
        local BindingObject = Manager:GetOrAddBindingObject(Class, UE.UInputAxisDelegateBinding)
        Binding.InputAxisName = AxisName
        Binding.FunctionNameToBind = FunctionName                    -- ★ Axis 也是先写入 BPGC binding object
        BindingObject.InputAxisDelegateBindings:Add(Binding)
        Manager:Override(Class, "InputAxis", FunctionName)
    end)
end

function M.PerformBindings(Module, Manager, Class)
    local Bindings = Module.__UnLuaInputBindings
    if not Bindings then
        return
    end

    for _, Binding in ipairs(Bindings) do
        xpcall(Binding, function(Error)
            UnLua.LogError(Error .. "\n" .. debug.traceback())
        end, Manager, Class)                                          -- ★ 类绑定阶段统一回放输入绑定闭包
    end
end
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaManager.cpp
// 函数: UUnLuaManager::BindClass / ReplaceActionInputs / ReplaceAxisInputs
// 行号: 300-317, 346-358, 367-397, 497-513
// 位置: 先把 Lua 输入绑定写进 BPGC，再把活体 InputComponent delegate 换到 Lua
// ============================================================================
auto& BindInfo = Classes.Add(Class);
...
UnLua::LowLevel::GetFunctionNames(Env->GetMainState(), Ref, BindInfo.LuaFunctions);
ULuaFunction::GetOverridableFunctions(Class, BindInfo.UEFunctions);

for (const auto& LuaFuncName : BindInfo.LuaFunctions)
{
    UFunction** Func = BindInfo.UEFunctions.Find(LuaFuncName);
    if (Func)
    {
        UFunction* Function = *Func;
        ULuaFunction::Override(Function, Class, LuaFuncName);         // ★ 先覆写类上已有 UFunction
    }
}
...
if (auto BPGC = Cast<UBlueprintGeneratedClass>(Class))
{
    ...
    InputTable.Call("PerformBindings", ModuleTable, this, BPGC);      // ★ 再执行 Lua 侧缓存的输入绑定闭包
}

FName FuncName = FName(*FString::Printf(TEXT("%s_%s"), *ActionName, SReadableInputEvent[IAB.KeyEvent]));
if (LuaFunctions.Find(FuncName))
{
    ULuaFunction::Override(InputActionFunc, Class, FuncName);
    IAB.ActionDelegate.BindDelegate(Actor, FuncName);                 // ★ 现有 ActionBinding 改绑到 Lua
}

if (!IS_INPUT_ACTION_PAIRED(IAB))
{
    ...
    InputComponent->AddActionBinding(AB);                             // ★ 缺失配对事件时主动补一条 binding
}

if (LuaFunctions.Find(IAB.AxisName))
{
    ULuaFunction::Override(InputAxisFunc, Class, IAB.AxisName);
    IAB.AxisDelegate.BindDelegate(Actor, IAB.AxisName);               // ★ AxisBinding 同样在活体组件层补绑
}
```

关键源码 [2]：`Angelscript` 暴露的是输入绑定原语，绑定所有权仍在组件本身

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/InputComponentScriptMixinLibrary.h
// 函数: UInputComponentScriptMixinLibrary::BindAction / BindAxis / BindAxisKey
// 行号: 24-30, 64-90
// 位置: 脚本作者显式把 delegate 绑定到 UInputComponent
// ============================================================================
UFUNCTION(BlueprintCallable)
static void BindAction(UInputComponent* Component, const FName& ActionName, EInputEvent KeyEvent, const FInputActionHandlerDynamicSignature& Delegate)
{
    FInputActionBinding AB( ActionName, KeyEvent );
    AB.ActionDelegate = Delegate;
    Component->AddActionBinding(AB);                                  // ★ 直接把 binding 放进组件
}

UFUNCTION(BlueprintCallable)
static void BindAxis(UInputComponent* Component, const FName& AxisName, const FInputAxisHandlerDynamicSignature& Delegate)
{
    FInputAxisBinding AB( AxisName );
    *(TInputUnifiedDelegate<FInputAxisHandlerSignature, FInputAxisHandlerDynamicSignature>*)&AB.AxisDelegate
        = TInputUnifiedDelegate<FInputAxisHandlerSignature, FInputAxisHandlerDynamicSignature>(Delegate);
    Component->AxisBindings.Emplace(MoveTemp(AB));                    // ★ 不扫命名约定，直接写组件数组
}

UFUNCTION(BlueprintCallable)
static void BindAxisKey(UInputComponent* Component, const FName& AxisKey, const FInputAxisHandlerDynamicSignature& Delegate)
{
    FInputAxisKeyBinding AB(AxisKey);
    *(TInputUnifiedDelegate<FInputAxisHandlerSignature, FInputAxisHandlerDynamicSignature>*)&AB.AxisDelegate
        = TInputUnifiedDelegate<FInputAxisHandlerSignature, FInputAxisHandlerDynamicSignature>(Delegate);
    Component->AxisKeyBindings.Emplace(MoveTemp(AB));
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UEnhancedInputComponent.cpp
// 函数: Bind_UEnhancedInputComponent lambda
// 行号: 7-45
// 位置: EnhancedInput 也是把原生 BindAction/BindDebugKey 原样交给脚本
// ============================================================================
auto UEnhancedInputComponent_ = FAngelscriptBinds::ExistingClass("UEnhancedInputComponent");
...
UEnhancedInputComponent_.Method(
    "FEnhancedInputActionEventBinding& BindAction(const UInputAction Action, ETriggerEvent TriggerEvent, FEnhancedInputActionHandlerDynamicSignature Delegate)",
    [](UEnhancedInputComponent& InputComponent, const UInputAction* Action, ETriggerEvent TriggerEvent, FEnhancedInputActionHandlerDynamicSignature Delegate) -> FEnhancedInputActionEventBinding&
    {
        return InputComponent.BindAction(Action, TriggerEvent, Delegate.GetUObject(), Delegate.GetFunctionName()); // ★ 仍是组件自身 API
    });
...
UEnhancedInputComponent_.Method(
    "FInputDebugKeyBinding& BindDebugKey(const FInputChord Chord, const EInputEvent KeyEvent, FInputDebugKeyHandlerDynamicSignature Delegate, bool bExecuteWhenPaused = true)",
    [](UEnhancedInputComponent& InputComponent, const FInputChord Chord, const EInputEvent KeyEvent, FInputDebugKeyHandlerDynamicSignature Delegate, bool bExecuteWhenPaused) -> FInputDebugKeyBinding&
    {
        return InputComponent.BindDebugKey(Chord, KeyEvent, Delegate.GetUObject(), Delegate.GetFunctionName(), bExecuteWhenPaused);
    });
```

新增对比结论：

- `UnLua` 的输入支持不是单纯“能在 Lua 里响应输入”，而是把 Blueprint 现有输入槽位和活体 `InputComponent` 都纳入 patch 流程。
- `Angelscript` 也不是没有输入脚本化；它已经把 `UInputComponent` 与 `UEnhancedInputComponent` 的绑定原语开放给脚本，只是 ownership 保持在组件层，而不是 manager 层。
- 因而这里不能写成“UnLua 有，Angelscript 没有”，更准确的是：`UnLua` 擅长**接管既有 Blueprint 输入面**，`Angelscript` 擅长**显式、原生形状的组件级绑定**。

差距判断：

- 相对 `UnLua` 这种“按命名约定自动接管现有 Blueprint 输入槽位”的做法，`Angelscript` 当前属于 `没有实现同层 auto-patch Blueprint input binding`。
- 如果比较的是“脚本能不能绑定输入”，`Angelscript` 并不是 `没有实现`，而是明显的 `实现方式不同`。
- 如果目标是把已有 Blueprint 资产无侵入挂到脚本输入逻辑上，`UnLua` 体现为局部 `实现质量差异` 优势；如果目标是让脚本作者显式控制绑定时机与组件对象，`Angelscript` 的当前模型更直接。

### [维度 D5] 源码跳转的 ownership：`UnLua` 主要交付 module/file 可达性，`Angelscript` 交付 symbol-to-line 导航

前几轮已经分析过协议、断点和变量观察，这一轮只补一个更落地的开发体验问题：**当用户已经定位到脚本符号后，谁负责把他带回源码**。`UnLua` 当前仓内 editor 侧更偏“确认这个 Blueprint 绑定了哪个 Lua module，以及对应文件在不在”。`GetBindingStatus()` 最终只落到 `ModuleLocator -> module name -> .lua 文件是否存在`；工具栏动作也主要是 `RevealInExplorer` 或复制相对路径。这对“脚本文件在哪”很有帮助，但粒度仍停留在 module/file 级别。

`Angelscript` 当前快照已经把这条链往前推到 symbol 级。`FAngelscriptSourceCodeNavigation` 为 `UASClass/UASFunction/UASStruct` 注册了导航处理器，能直接拿 `UASFunction::GetSourceFilePath()/GetSourceLineNumber()` 跳到脚本定义行；`FAngelscriptDebugServer::GoToDefinition()` 又把 debugger 侧的 symbol 查询转成 `FSourceCodeNavigation::NavigateToFunction/Property/Class`。也就是说，这里不是“调试器一套、编辑器一套”，而是仓内维护了一条统一的 symbol-to-line 导航桥。

```
[D5-Deep] Source Navigation Ownership
UnLua
├─ GetBindingStatus(Blueprint)                    // 检查是否实现接口与文件是否存在
├─ ModuleLocator -> module path                   // 先得到 module 名
└─ RevealInExplorer / CopyRelativePath            // 用户再去外部 IDE 打开文件

Angelscript
├─ DebugServer GoToDefinition                     // 调试端发 symbol 查询
│  └─ LookupAngelscriptFunction / type/property   // 映射回 Unreal/script symbol
├─ FAngelscriptSourceCodeNavigation               // 统一导航处理器
└─ code --goto file:line                          // 直接跳到脚本定义行
```

关键源码 [1]：`UnLua` editor 侧主要解决“绑定文件可达性”，而不是 symbol 级导航

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorCore.cpp
// 函数: GetBindingStatus
// 行号: 22-52
// 位置: 绑定状态的最终判定依据是 module 名和对应 .lua 文件是否存在
// ============================================================================
ELuaBindingStatus GetBindingStatus(const UBlueprint* Blueprint)
{
    ...
    if (!Target->ImplementsInterface(UUnLuaInterface::StaticClass()))
        return ELuaBindingStatus::NotBound;
    ...
    const auto ModuleLocator = Cast<ULuaModuleLocator>(Settings->ModuleLocatorClass->GetDefaultObject());
    const auto ModuleName = ModuleLocator->Locate(Target);             // ★ 先从 Blueprint 反查 module 名
    if (ModuleName.IsEmpty())
        return ELuaBindingStatus::Unknown;

    const auto RelativePath = ModuleName.Replace(TEXT("."), TEXT("/")) + TEXT(".lua");
    const auto FullPath = GLuaSrcFullPath + "/" + RelativePath;
    if (!FPaths::FileExists(FullPath))
        return ELuaBindingStatus::BoundButInvalid;                     // ★ 只检查文件可达性，不解析符号行号

    return ELuaBindingStatus::Bound;
}
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp
// 函数: FUnLuaEditorToolbar::RevealInExplorer_Executed / CopyAsRelativePath_Executed
// 行号: 315-376
// 位置: 工具栏动作主要是 reveal/copy 文件路径
// ============================================================================
const auto Func = TargetClass->FindFunctionByName(FName("GetModuleName"));
...
FString ModuleName;
DefaultObject->UObject::ProcessEvent(Func, &ModuleName);              // ★ 还是先读取 module 名

const auto RelativePath = ModuleName.Replace(TEXT("."), TEXT("/"));
const auto FileName = FString::Printf(TEXT("%s%s.lua"), *GLuaSrcFullPath, *RelativePath);

if (IFileManager::Get().FileExists(*FileName))
{
    FPlatformProcess::ExploreFolder(*FileName);                       // ★ 打开资源管理器，而不是定位到某个符号行
}
...
const auto RelativePath = ModuleName.Replace(TEXT("."), TEXT("/")) + TEXT(".lua");
FPlatformApplicationMisc::ClipboardCopy(*RelativePath);               // ★ 另一个动作只是复制相对路径
```

关键源码 [2]：`Angelscript` 把 debugger 查询、editor 导航和脚本源位置信息连成一条链

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp
// 函数: FAngelscriptSourceCodeNavigation::NavigateToFunction / NavigateToProperty / OpenFile
// 行号: 34-45, 53-65, 110-115, 136-138
// 位置: editor 为脚本类型注册统一导航处理器，直接按文件行号跳转
// ============================================================================
virtual bool NavigateToFunction(const UFunction* InFunction) override
{
    auto* ASFunc = Cast<const UASFunction>(InFunction);
    if (ASFunc == nullptr)
        return false;
    FString Path = ASFunc->GetSourceFilePath();
    if (Path.Len() == 0)
        return false;

    OpenFile(Path, ASFunc->GetSourceLineNumber());                    // ★ 直接定位到脚本定义行
    return true;
};

virtual bool NavigateToProperty(const FProperty* InProperty) override
{
    ...
    auto PropertyDesc = ClassDesc->GetProperty(InProperty->GetNameCPP());
    if (!PropertyDesc.IsValid())
        return false;

    OpenModule(Module, PropertyDesc->LineNumber);                     // ★ 属性同样按脚本行号跳转
    return true;
}

void OpenFile(const FString& Path, int LineNo = -1)
{
    if (LineNo != -1)
        FPlatformMisc::OsExecute(nullptr, TEXT("code"), *FString::Printf(TEXT("--goto \"%s:%d\""), *Path, LineNo));
}

void RegisterAngelscriptSourceNavigation()
{
    FSourceCodeNavigation::AddNavigationHandler(new FAngelscriptSourceCodeNavigation); // ★ 注册到编辑器统一导航系统
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp
// 函数: UASFunction::GetSourceFilePath / UASFunction::GetSourceLineNumber
// 行号: 1535-1559
// 位置: 脚本函数对象自己保存源文件与声明行，供导航与调试共用
// ============================================================================
FString UASFunction::GetSourceFilePath() const
{
    ...
    auto Module = Manager.GetModule(ScriptFunction->GetModule());
    if (!Module.IsValid())
        return TEXT("");
    if (Module->Code.Num() == 0)
        return TEXT("");
    return Module->Code[0].AbsoluteFilename;                          // ★ 路径来自脚本模块描述
}

int UASFunction::GetSourceLineNumber() const
{
    if (ScriptFunction == nullptr)
        return -1;

    auto* RealFunc = ((asCScriptFunction*)ScriptFunction);
    auto* scriptData = RealFunc->scriptData;
    if (scriptData == nullptr)
        return -1;

    return (scriptData->declaredAt & 0xFFFFF) + 1;                    // ★ 行号来自编译器保留的声明位置
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 函数: FAngelscriptDebugServer::GoToDefinition
// 行号: 1288-1370
// 位置: debugger 收到 go-to-definition 请求后，统一走 SourceCodeNavigation
// ============================================================================
asIScriptFunction* ScriptFunction = nullptr;
...
UFunction* UnrealFunction = FAngelscriptDocs::LookupAngelscriptFunction(ScriptFunction->GetId());
if (UnrealFunction != nullptr)
{
    FSourceCodeNavigation::NavigateToFunction(UnrealFunction);        // ★ 调试器与编辑器共用同一条跳转桥
    return;
}
...
FProperty* Property = AssociatedClass->FindPropertyByName(*GoTo.SymbolName);
if (Property != nullptr)
{
    FSourceCodeNavigation::NavigateToProperty(Property);
    return;
}
...
FSourceCodeNavigation::NavigateToClass(AssociatedClass);
```

新增对比结论：

- `UnLua` 在当前仓内已经很好地产品化了“这个 Blueprint 绑的是哪个 Lua 文件、文件是否存在”这一层反馈。
- `Angelscript` 进一步把源码导航做到 symbol 级，并让 debugger 与 editor 共享同一条导航链。
- 因而这里的差异不是“有没有 editor tooling”，而是**工具链停留在 file 级，还是继续进入 symbol-to-line 级**。

差距判断：

- 相对 `Angelscript` 当前这套 symbol-to-line 导航桥，`UnLua` 仓内属于 `没有实现同层 symbol 级源码跳转`。
- 如果只比较“脚本文件能不能从编辑器里被找到”，`UnLua` 并不是 `没有实现`，而是 `实现方式不同`：它选择了 module/file 可达性与路径操作。
- 如果目标是调试会话里直接从脚本符号跳回定义行，`Angelscript` 在当前快照上体现为明确的 `实现质量差异` 优势。

### [维度 D6] IDE 产物保鲜的 ownership：`UnLua` 维护磁盘 stub 树，`Angelscript` 维护内存中的文档与源位置信息

前文已经补过 D6 的“产物最终给谁消费”。这一轮只补一个更细的点：**这些 IDE 辅助信息靠谁来保鲜**。`UnLua` 的答案是“靠一棵真实存在的磁盘 stub 树”。`FUnLuaIntelliSenseGenerator` 在 editor 初始化时直接挂 `AssetRegistry` 的 `OnAssetAdded/Removed/Renamed/Updated`，每次资产变化都会把 Blueprint 或原生类型重新导出到 `Intermediate/IntelliSense/<Module>/<Type>.lua`，并在删除或重命名时同步删旧文件。也就是说，`UnLua` 把 IDE 支持当成一个需要持续维护的**文件系统产物**。

`Angelscript` 当前快照则没有同层“对外 stub 目录”的保鲜回路；它把 IDE/帮助信息保存在运行期元数据里。`FAngelscriptDocs` 用 `FunctionId/TypeId/PropertyOffset` 做键保存 tooltip 与 `UFunction` 指针，`UASFunction`/`UASClass` 自己保存源文件和源行号，editor/debugger 再按需读取这些内存态元数据完成跳转和文档展示。这不是“没有 IDE 支持”，而是把 freshness contract 绑在**类生成和符号生命周期**上，而不是磁盘声明树上。

```
[D6-Deep] IDE Metadata Freshness
UnLua
├─ AssetRegistry events                           // 资产变化就是 stub 变化触发器
├─ Export / SaveFile / DeleteFile                 // 增量维护 *.lua 声明文件
└─ Intermediate/IntelliSense/<Module>/<Type>.lua  // 外部 IDE 消费真实文件树

Angelscript
├─ UASFunction / UASClass source metadata         // 源文件与行号随脚本类型常驻
├─ FAngelscriptDocs maps docs by id               // 文档按 function/type/property id 缓存
└─ editor/debugger query live metadata            // 不维护外部 stub 树
```

关键源码 [1]：`UnLua` 的 IntelliSense 不是一次性导出，而是随资产生命周期增量维护

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp
// 函数: Initialize / Export / SaveFile / DeleteFile / OnAssetAdded / OnAssetRemoved / OnAssetRenamed
// 行号: 42-55, 148-166, 222-278
// 位置: editor 常驻监听资产事件，持续维护 Intermediate/IntelliSense 文件树
// ============================================================================
void FUnLuaIntelliSenseGenerator::Initialize()
{
    OutputDir = IPluginManager::Get().FindPlugin("UnLua")->GetBaseDir() + "/Intermediate/IntelliSense";

    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    AssetRegistryModule.Get().OnAssetAdded().AddRaw(this, &FUnLuaIntelliSenseGenerator::OnAssetAdded);
    AssetRegistryModule.Get().OnAssetRemoved().AddRaw(this, &FUnLuaIntelliSenseGenerator::OnAssetRemoved);
    AssetRegistryModule.Get().OnAssetRenamed().AddRaw(this, &FUnLuaIntelliSenseGenerator::OnAssetRenamed);
    AssetRegistryModule.Get().OnAssetUpdated().AddRaw(this, &FUnLuaIntelliSenseGenerator::OnAssetUpdated); // ★ 资产变化即 stub 变化
}

void FUnLuaIntelliSenseGenerator::Export(const UField* Field)
{
    auto ModuleName = Package->GetName();
    ...
    FString FileName = UnLua::IntelliSense::GetTypeName(Field);
    if (FileName.EndsWith("_C"))
        FileName.LeftChopInline(2);
    const FString Content = UnLua::IntelliSense::Get(Field);
    SaveFile(ModuleName, FileName, Content);                            // ★ 每个类型独立写成一个 .lua 文件
}

void FUnLuaIntelliSenseGenerator::SaveFile(const FString& ModuleName, const FString& FileName, const FString& GeneratedFileContent)
{
    const FString FilePath = FString::Printf(TEXT("%s/%s.lua"), *Directory, *FileName);
    ...
    if (FileContent != GeneratedFileContent)
        FFileHelper::SaveStringToFile(GeneratedFileContent, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM); // ★ 内容变了才落盘
}

void FUnLuaIntelliSenseGenerator::DeleteFile(const FString& ModuleName, const FString& FileName)
{
    const FString FilePath = FString::Printf(TEXT("%s/%s.lua"), *Directory, *FileName);
    if (FileManager.FileExists(*FilePath))
        FileManager.Delete(*FilePath);                                  // ★ 删除/重命名时同步清理旧 stub
}

void FUnLuaIntelliSenseGenerator::OnAssetAdded(const FAssetData& AssetData)
{
    ...
    OnAssetUpdated(AssetData);
    ExportUE(Types);                                                    // ★ 新资产进入后还会刷新公共 UE stub
}

void FUnLuaIntelliSenseGenerator::OnAssetRemoved(const FAssetData& AssetData)
{
    ...
    DeleteFile(FString("/Game"), AssetData.AssetName.ToString());       // ★ 资产删除直接删对应声明文件
}

void FUnLuaIntelliSenseGenerator::OnAssetRenamed(const FAssetData& AssetData, const FString& OldPath)
{
    const FString OldPackageName = FPackageName::GetShortName(OldPath);
    DeleteFile("/Game", OldPackageName);                                // ★ 先删旧名
    OnAssetUpdated(AssetData);                                          // ★ 再生成新名
}
```

关键源码 [2]：`Angelscript` 把 IDE 辅助信息留在运行期元数据里，而不是维护外部声明树

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp
// 函数: FAngelscriptDocs::AddUnrealDocumentation / AddUnrealDocumentationForType /
//       AddUnrealDocumentationForProperty / LookupAngelscriptFunction
// 行号: 31-124
// 位置: 文档与原生函数映射都按 id 常驻在内存表里
// ============================================================================
void FAngelscriptDocs::AddUnrealDocumentation(int FunctionId, FStringView Documentation, FStringView Category, UFunction* Function)
{
    FPassedDoc Doc;
    Doc.Tooltip = Documentation;
    Doc.Category = Category;
    Doc.Function = Function;

    UnrealDocumentation.Add(FunctionId, Doc);                          // ★ 文档按 FunctionId 存在内存里
}

void FAngelscriptDocs::AddUnrealDocumentationForType(int TypeId, FStringView Documentation)
{
    UnrealTypeDocumentation.Add(TypeId, FString(Documentation));       // ★ 类型文档按 TypeId 缓存
}

void FAngelscriptDocs::AddUnrealDocumentationForProperty(int TypeId, int PropertyOffset, FStringView Documentation)
{
    UnrealPropertyDocumentation.Add(TPair<int,int>(TypeId, PropertyOffset), FString(Documentation)); // ★ 属性文档按类型+偏移缓存
}

UFunction* FAngelscriptDocs::LookupAngelscriptFunction(int FunctionId)
{
    FPassedDoc* Found = UnrealDocumentation.Find(FunctionId);
    if (Found == nullptr)
        return nullptr;
    else
        return Found->Function;                                        // ★ editor/debugger 后续直接拿回 UFunction
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp
// 函数: UASClass::GetSourceFilePath / UASFunction::GetSourceFilePath / UASFunction::GetSourceLineNumber
// 行号: 1497-1507, 1535-1559
// 位置: 脚本类/函数对象自己保存源文件和行号，供导航与文档系统按需读取
// ============================================================================
FString UASClass::GetSourceFilePath() const
{
    ...
    auto Module = Manager.GetModule(((asITypeInfo*)ScriptTypePtr)->GetModule());
    ...
    return Module->Code[0].AbsoluteFilename;                           // ★ 类的源文件直接来自模块代码段
}

FString UASFunction::GetSourceFilePath() const
{
    ...
    auto Module = Manager.GetModule(ScriptFunction->GetModule());
    ...
    return Module->Code[0].AbsoluteFilename;                           // ★ 函数与类共用模块源路径
}

int UASFunction::GetSourceLineNumber() const
{
    ...
    auto* RealFunc = ((asCScriptFunction*)ScriptFunction);
    auto* scriptData = RealFunc->scriptData;
    if (scriptData == nullptr)
        return -1;

    return (scriptData->declaredAt & 0xFFFFF) + 1;                     // ★ 行号来自编译期记录，而不是磁盘 stub
}
```

新增对比结论：

- `UnLua` 的 IDE 支持是“文件产物优先”模型，资产增删改名都会直接反映到 `Intermediate/IntelliSense` 目录树。
- `Angelscript` 的 IDE 支持更像“活体元数据优先”模型，源码位置和文档随着脚本类/函数对象常驻，而不是对外维护一套声明文件。
- 因而这里的关键差异不是“有没有提示支持”，而是**提示支持的 freshness contract 放在磁盘文件树，还是放在运行期元数据表**。

差距判断：

- 相对 `UnLua` 这种随资产事件增量维护的外部 stub 文件树，`Angelscript` 当前属于 `没有实现同层 filesystem stub lifecycle`。
- 如果比较的是“编辑器与调试器能不能拿到最新的符号位置和文档”，`Angelscript` 并不是 `没有实现`，而是明显的 `实现方式不同`。
- 如果目标是让外部脚本 IDE 直接消费一棵稳定的声明文件树，`UnLua` 当前更贴近该目标；如果目标是让 editor/debugger 始终围绕活体脚本符号工作，`Angelscript` 的现有路径更内聚。

---

## 深化分析 (2026-04-09 01:16)

### [维度 D5] 调试表达式与混合作用域 grammar 的 ownership：`UnLua` 暴露 `lua_State` 观察面，`Angelscript` 交付跨 Script/Blueprint 的查询语法

前几轮已经把“谁拥有调试会话协议”和“谁拥有断点落点”拆开了。这一轮只补一个更细但直接影响 IDE/调试器可用性的层次：**调试器如何描述一个可求值路径，以及这个路径能跨过哪些 frame/scope**。就当前仓库快照看，`UnLua` 仓内公开调试面仍然围绕 `lua_State` 与 `lua_Debug` 展开：`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDebugBase.h:29-91` 只公开了 `FLuaDebugValue`、`GetStackVariables()` 和 `GetLuaCallStack()`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaDebugBase.cpp:614-721` 的实现也只枚举 locals、upvalues，并把调用栈拼成字符串。它当然不是“没有调试数据”，但仓内并没有同层的 expression grammar、scope path 或 mixed Blueprint/script frame resolver 暴露出来。

`Angelscript` 的 ownership 则更靠内核：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h:25-80,632-637` 把 `RequestVariables/RequestEvaluate`、`GetDebuggerValue()`、`GetDebuggerScope()`、`ResolveDebuggerFrame()` 都定义为 runtime 协议面；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:2281-2815` 进一步支持 `0:Expr`、`%local%`、`%this%`、`%module%`，还能把 Blueprint frame 插入 script frame 序列里。更关键的是，这套 grammar 不是隐式约定，而是被 `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerScriptFixture.cpp:62-170` 的测试 fixture 固化成契约，例如 `0:%module%.GlobalCounter`、`0:%local%`、`0:%this%`。

```
[D5-Deep] Debug Expression Ownership
UnLua
├─ lua_State / lua_Debug                           // 仓内调试面围绕 Lua C API
├─ GetStackVariables() -> locals + upvalues        // 返回值集合
├─ GetLuaCallStack() -> "Source/name/Line" string  // 返回字符串化调用栈
└─ IDE/debugger owns expression grammar            // 仓内未看到同层 path grammar

Angelscript
├─ RequestVariables / RequestEvaluate              // runtime 自己暴露查询请求
├─ ResolveDebuggerFrame()                          // 统一解析 Script + Blueprint frame
├─ GetDebuggerValue()                              // 识别 0:Expr / %local% / %this% / %module%
└─ automation fixtures own eval paths              // 测试直接固化 grammar 契约
```

关键源码 [1]：`UnLua` 仓内调试接口把 ownership 停在“值与调用栈抽取”，没有再往前定义可求值路径语法

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDebugBase.h
// 函数: FLuaDebugValue / GetStackVariables / GetLuaCallStack
// 行号: 29-91
// 位置: 当前仓内公开调试接口面
// ============================================================================
struct UNLUA_API FLuaDebugValue
{
    FString ReadableValue;
    FString Type;
    int32 Depth;
    bool bAlreadyBuilt;
    TArray<FLuaDebugValue> Keys;
    TArray<FLuaDebugValue> Values;                                  // ★ 可以展开 Lua/UE 复合值
    ...
};

UNLUA_API bool GetStackVariables(
    lua_State *L,
    int32 StackLevel,
    TArray<FLuaVariable> &LocalVariables,
    TArray<FLuaVariable> &Upvalues,
    int32 Level = MAX_int32);                                       // ★ 公开 API 只承诺 locals/upvalues

UNLUA_API FString GetLuaCallStack(lua_State *L);                    // ★ 公开 API 只承诺 call stack 字符串
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaDebugBase.cpp
// 函数: GetStackVariables / GetLuaCallStack
// 行号: 614-721
// 位置: 调试数据面的真实实现
// ============================================================================
bool GetStackVariables(lua_State *L, int32 StackLevel, TArray<FLuaVariable> &LocalVariables, TArray<FLuaVariable> &Upvalues, int32 Level)
{
    ...
    for (int32 i = 1; ; ++i)
    {
        const char *VarName = lua_getlocal(L, &ar, i);              // ★ 逐个抽 locals
        if (!VarName)
            break;
        ...
        Variable.Key = UTF8_TO_TCHAR(VarName);
        Variable.Value.Build(L, -1, Level);                         // ★ 值展开留在 FLuaDebugValue
        lua_pop(L, 1);
    }

    if (lua_getinfo(L, "f", &ar))
    {
        ...
        const char *UpvalueName = lua_getupvalue(L, FunctionIdx, i);// ★ 再逐个抽 upvalues
        ...
    }

    return true;
}

FString GetLuaCallStack(lua_State *L)
{
    ...
    while (lua_getstack(L, Depth++, &ar))
    {
        lua_getinfo(L, "nSl", &ar);
        FString DisplayInfo = FString::Printf(
            TEXT("Source : %s, name : %s, Line : %d \\n"),
            UTF8_TO_TCHAR(ar.source),
            UTF8_TO_TCHAR(ar.name),
            ar.currentline);                                        // ★ 结果是字符串化栈描述
        CallStack += DisplayInfo;
    }

    return CallStack;
}
```

关键源码 [2]：`Angelscript` 把 frame 解析、scope 前缀和 evaluate 请求都收进 runtime 契约

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h
// 函数: EDebugMessageType / GetDebuggerValue / GetDebuggerScope / ResolveDebuggerFrame
// 行号: 25-80, 632-637
// 位置: debugger 协议面直接声明 variables / evaluate / scope 解析能力
// ============================================================================
enum class EDebugMessageType : uint8
{
    ...
    RequestVariables,
    Variables,

    RequestEvaluate,
    Evaluate,
    GoToDefinition,
    ...
};

bool GetDebuggerValue(const FString& Path, FDebuggerValue& Value, int32* InOutFrame = nullptr, TArray<FDebuggerValue>* OutInnerValues = nullptr);
bool GetDebuggerScope(const FString& Path, FDebuggerScope& Scope);
int ResolveDebuggerFrame(int DebuggerFrame);                        // ★ frame resolver 是仓内协议的一部分
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 函数: ResolveDebuggerFrame / GetDebuggerValue / GetDebuggerScope
// 行号: 2281-2450, 2692-2815
// 位置: 混合 Script/Blueprint frame 与 scope grammar 的核心实现
// ============================================================================
if (Path.FindChar(':', ColonIndex))
{
    if (!Path.IsValidIndex(ColonIndex+1) || Path[ColonIndex+1] != ':')
    {
        LexFromString(Frame, *Path.Left(ColonIndex));
        NamePath = Path.RightChop(ColonIndex+1);                    // ★ 支持 0:Expr 这种 frame:path 语法
    }
}
...
if (Expr[0].Name == TEXT("%local%"))
{
    bHasPrefix = true;
    bPrefixLocal = true;                                            // ★ 显式 local scope 前缀
}
else if (Expr[0].Name == TEXT("%this%"))
{
    bHasPrefix = true;
    bPrefixThis = true;                                             // ★ 显式 this scope 前缀
}
else if (Expr[0].Name == TEXT("%module%"))
{
    bHasPrefix = true;
    bPrefixModule = true;                                           // ★ 显式 module scope 前缀
}
...
if ((Frame & FLAG_BlueprintFrame) != 0)
{
    ...
    UObject* StackFrameObject = BPStack->GetCurrentScriptStack()[BPFrame]->Object;
    ...
    if (Expr[0].Name == TEXT("this"))
    {
        if (Usage.GetDebuggerValue(Address, CurrentValue))
            bValidValue = true;                                     // ★ Blueprint frame 也能被统一求值
    }
}
...
if (CurrentValue.Name == TEXT("%local%"))
{
    ...
    Scope.Values.Add(MoveTemp(VarValue));                           // ★ %local% 能展开成完整 scope
}
```

关键源码 [3]：`Angelscript` 的测试直接把 grammar 写成回归契约，而不是只靠人工约定

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerScriptFixture.cpp
// 函数: CreateBreakpointFixture / CreateCallstackFixture
// 行号: 62-170
// 位置: debugger fixture 把可求值路径固化为自动化输入
// ============================================================================
{
    {TEXT("LocalValuePath"), TEXT("0:LocalValue")},
    {TEXT("ThisStoredValuePath"), TEXT("0:this.StoredValue")},
    {TEXT("ThisScopePath"), TEXT("0:%this%")}                       // ★ this scope 本身就是可请求对象
},
...
{
    {TEXT("LeafLocalValuePath"), TEXT("0:LocalValue")},
    {TEXT("LeafCombinedPath"), TEXT("0:Combined")},
    {TEXT("ThisMemberValuePath"), TEXT("0:this.MemberValue")},
    {TEXT("ModuleGlobalCounterPath"), TEXT("0:%module%.GlobalCounter")},
    {TEXT("LocalScopePath"), TEXT("0:%local%")},
    {TEXT("ModuleScopePath"), TEXT("0:%module%")}                   // ★ scope grammar 被 fixture 明确写死
};
```

新增对比结论：

- `UnLua` 在这一层并不是 `没有调试能力`，而是把仓内 ownership 停在 `lua_State` 观测面：值展开、locals/upvalues 抽取、call stack 描述都已具备。
- 相对 `Angelscript` 当前这套 `frame:path + scope prefix + Blueprint/script mixed frame` 的 grammar，`UnLua` 当前源码树内属于 `没有实现同层 debugger scope grammar / mixed-frame resolver`。
- 如果比较“调试功能总体是否存在”，两者是 `实现方式不同`；如果比较“仓内是否已经交付可查询、可测试、可跨 Blueprint/script 的表达式模型”，`Angelscript` 在当前快照上体现为明确的 `实现质量差异` 优势。

### [维度 D10] 示例 source-of-truth 的 ownership：`UnLua` 以真实教程资产为权威，`Angelscript` 当前处于真实脚本 / 测试内联 / Coverage 资产并存阶段

前文已经覆盖过 `UnLua` 的 `Docs/` 与 `Tutorials/` 结构。这一轮只补一个更偏交付面的判断：**用户真正应该跟着哪一组文件学，仓库又把哪一组文件当成“权威示例”**。`UnLua` 当前快照的 ownership 很直接：`Reference/UnLua/README.md:34-69` 把 13 个教程直接链接到 `Content/Script/Tutorials/*.lua`；教程脚本自己又继续回指实际绑定资产或配套 C++ 文件，例如 `Reference/UnLua/Content/Script/Tutorials/01_HelloWorld.lua:1-21` 标出绑定的 map，`Reference/UnLua/Content/Script/Tutorials/08_CppCallLua.lua:1-31` / `Reference/UnLua/Content/Script/Tutorials/09_StaticExport.lua:1-23` 直接注明 `Source/TPSProject/*.cpp`；而这些 C++ helper 也确实存在于 `Reference/UnLua/Source/TPSProject/` 下并可被教程调用。这意味着 UnLua 的“公开文档入口”和“真实可运行示例”是同一条链。

`Angelscript` 当前快照则是另一种权衡。它当然不是没有真实示例：`Script/Example_Actor.as:1-74` 就是一份完整脚本文件，`Documents/Plans/Plan_ScriptExamplesExpansion/Coverage/*.as` 也被自动化测试直接从磁盘加载。但 `Script/Examples/README.md:1-21` 明确写着当前首波真实交付源仍然是 `Documents/Plans/Plan_ScriptExamplesExpansion/Coverage/`，未来才考虑提升为 `Script/Examples/` 正式入口；与此同时，`Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleActorTest.cpp:9-87` 又把 `Example_Actor.as` 的主题示例以内联字符串保留了一份，`Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleTestSupport.cpp:16-59` 再把这些字符串编译成虚拟路径 `ScriptExamples/<Name>.as`。换句话说，当前快照里 `Angelscript` 的示例 authority 还没有收敛成单一 public source-of-truth，而是用户交付面与测试回归面并存。

```
[D10-Deep] Example Source-of-Truth Ownership
UnLua
├─ README -> Content/Script/Tutorials/*.lua        // 公开入口直接指向真实教程脚本
├─ Tutorial script -> map / C++ helper path        // 脚本再反指真实资产与示例代码
└─ sample C++ lives in TPSProject                  // 用户看到的教程就是仓库权威示例

Angelscript
├─ Script/Example_Actor.as                         // 真实脚本示例
├─ Script/Examples/README.md                       // 公开入口仍是过渡说明
├─ inline FScriptExampleSource                     // 测试内联字符串也是权威之一
└─ Coverage/*.as + automation loader              // 磁盘资产由测试直接验证
```

关键源码 [1]：`UnLua` 的 README 直接把公开教程入口绑到真实脚本文件，教程脚本又继续回指真实资产 / C++ 配套

```markdown
<!-- =========================================================================
文件: Reference/UnLua/README.md
函数: N/A
行号: 34-69
位置: README 直接列出可运行的教程脚本入口
========================================================================== -->
1. 新建蓝图后打开，在UnLua工具栏中选择 `绑定`
2. 在接口的 `GetModule` 函数中填入Lua文件路径
3. 选择UnLua工具栏中的 `创建Lua模版文件`
4. 打开 `Content/Script/GameModes/BP_MyGameMode.lua` 编写你的代码

# 更多示例
* [01_HelloWorld](Content/Script/Tutorials/01_HelloWorld.lua)
* [08_CppCallLua](Content/Script/Tutorials/08_CppCallLua.lua)
* [09_StaticExport](Content/Script/Tutorials/09_StaticExport.lua)      <!-- ★ 公开入口直接就是可运行脚本 -->
```

```lua
-- ============================================================================
-- 文件: Reference/UnLua/Content/Script/Tutorials/01_HelloWorld.lua
-- 文件: Reference/UnLua/Content/Script/Tutorials/08_CppCallLua.lua
-- 文件: Reference/UnLua/Content/Script/Tutorials/09_StaticExport.lua
-- 行号: 1-21, 1-31, 1-23
-- 位置: 教程脚本继续反指真实绑定资产与配套 C++ 源码
-- ============================================================================
-- 01_HelloWorld.lua
-- 本脚本由 "Content/Tutorials/01_HelloWorld/HelloWorld.map" 的关卡蓝图绑定   -- ★ 脚本直接告诉用户绑定资产是谁
...
-- —— 本示例来自 "Content/Script/Tutorials/01_HelloWorld.lua"

-- 08_CppCallLua.lua
-- 本示例C++源码：
-- Source\TPSProject\TutorialBlueprintFunctionLibrary.cpp                -- ★ 教程文件直接点名配套 C++
...
UE.UTutorialBlueprintFunctionLibrary.CallLuaByGlobalTable()
UE.UTutorialBlueprintFunctionLibrary.CallLuaByFLuaTable()

-- 09_StaticExport.lua
-- 本示例C++源码：
-- \Source\TPSProject\TutorialObject.cpp                                -- ★ 静态导出教程也指向真实 helper
...
local tutorial = UE.FTutorialObject("教程")
```

关键源码 [2]：`UnLua` 被教程点名的 C++ helper 真实存在，说明 README -> Tutorial -> Sample Code 是闭环

```cpp
// ============================================================================
// 文件: Reference/UnLua/Source/TPSProject/TutorialBlueprintFunctionLibrary.h
// 文件: Reference/UnLua/Source/TPSProject/TutorialBlueprintFunctionLibrary.cpp
// 文件: Reference/UnLua/Source/TPSProject/TutorialObject.cpp
// 行号: 12-19, 11-44, 38-42
// 位置: 教程引用的 BlueprintFunctionLibrary / exported type 都是工程内真实代码
// ============================================================================
UFUNCTION(BlueprintCallable, meta = (DisplayName = "CallLuaByGlobalTable", Category = "UnLua Tutorial"))
static void CallLuaByGlobalTable();

UFUNCTION(BlueprintCallable, meta = (DisplayName = "CallLuaByFLuaTable", Category = "UnLua Tutorial"))
static void CallLuaByFLuaTable();                                     // ★ 教程里调用的入口在头文件中公开

void UTutorialBlueprintFunctionLibrary::CallLuaByGlobalTable()
{
    ...
    const auto RetValues = UnLua::CallTableFunc(Env.GetMainState(), "G_08_CppCallLua", "CallMe", 1.1f, 2.2f);
    ...                                                               // ★ 08_CppCallLua.lua 的 C++ 配套逻辑确实存在
}

BEGIN_EXPORT_CLASS(FTutorialObject)
ADD_FUNCTION(GetTitle)
ADD_LIB(FTutorialObjectLib)
END_EXPORT_CLASS()
IMPLEMENT_EXPORTED_CLASS(FTutorialObject)                             // ★ 09_StaticExport.lua 消费的类型不是伪代码
```

关键源码 [3]：`Angelscript` 当前快照里的示例 authority 分成真实脚本、过渡 README、测试内联字符串和 Coverage 磁盘资产四条线

```markdown
<!-- =========================================================================
文件: Script/Examples/README.md
函数: N/A
行号: 1-21
位置: 当前公开入口仍然声明 Coverage 目录才是真实交付源
========================================================================== -->
本目录保留为后续正式公开的 Script 示例入口。

- 当前首波需要交付的 Coverage `.as` 资产先放在 `Documents/Plans/Plan_ScriptExamplesExpansion/Coverage/`。
- 这里暂时只保留长期入口说明，等这批交付资产稳定后，再决定是否同步提升为 `Script/Examples/` 正式示例。
- 当前波次以 `Documents/Plans/Plan_ScriptExamplesExpansion/Coverage/` 为真实交付源，避免与测试内联字符串再次分叉。   <!-- ★ README 明示当前 authority 不在本目录 -->
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleTestSupport.h
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleTestSupport.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleActorTest.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleCoverageTests.cpp
// 行号: 7-13, 16-59, 9-87, 25-61
// 位置: 测试系统同时接受“内联字符串示例”和“磁盘 Coverage 资产”两种 authority
// ============================================================================
struct FScriptExampleSource
{
    const wchar_t* ExampleFileName;
    const wchar_t* ScriptText;
    const wchar_t* DependencyFileName;
    const wchar_t* DependencyScriptText;                              // ★ 示例首先可以以内联字符串存在
};

const FString VirtualFileName = FString::Printf(TEXT("ScriptExamples/%s"), *ExampleFileName);
const bool bCompiled = CompileAnnotatedModuleFromMemory(&Engine, ModuleName, VirtualFileName, CombinedScriptCode);
// ★ 测试把内联示例编译成虚拟路径，而不是读取 Script/Example_Actor.as

const AngelscriptScriptExamples::FScriptExampleSource GActorExample = {
    TEXT("Example_Actor.as"),
    TEXT(R"ANGELSCRIPT(
class AExampleActor_UnitTest : AActor
{
    ...
})ANGELSCRIPT"),
    nullptr,
    nullptr,
};                                                                    // ★ Actor 示例主题在测试里保留了第二份文本 authority

const FString AbsolutePath = GetCoverageExampleAbsolutePath(RelativePath);
if (!Test.TestTrue(..., FFileHelper::LoadFileToString(ScriptSource, *AbsolutePath)))
    return nullptr;                                                   // ★ Coverage 示例则走磁盘文件 authority
```

关键源码 [4]：`Angelscript` 仍然有真实示例文件，但当前 public/test 两条链尚未完全收束到它

```cpp
// ============================================================================
// 文件: Script/Example_Actor.as
// 行号: 1-74
// 位置: 仓库中确实存在真实 Actor 脚本示例
// ============================================================================
class AExampleActorType : AActor
{
    UPROPERTY()
    int ExampleValue = 15;

    default bReplicates = true;
    default Tags.Add(n"ExampleTag");

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
}
```

新增对比结论：

- `UnLua` 当前快照的公开教程入口、真实脚本文件、配套 C++ helper 三者是同一条 authority chain；用户从 README 点进去的就是仓库认可的权威示例。
- `Angelscript` 并不是 `没有示例`，也不是 `没有真实脚本资产`；它当前的问题是 authority split：真实脚本、测试内联字符串、Coverage 磁盘资产分别承担了公开展示与自动化验证职责。
- 如果比较“是否有示例与自动化防漂移”，两者是 `实现方式不同`，而且 `Angelscript` 在 anti-drift 上反而更强；如果比较“是否已经形成单一、用户可直接进入的 public source-of-truth 教程链”，相对 `UnLua` 当前快照，`Angelscript` 属于 `没有实现同层 unified public example entry`。
- 站在最终交付质量上看，`UnLua` 在用户导向的 tutorial delivery 上体现为明确的 `实现质量差异` 优势；`Angelscript` 的优势则是在示例与 automation 更紧耦合，但 public example surface 仍处于过渡态。

---

## 深化分析 (2026-04-09 06:37:06)

### [维度 D2] 反射表面的新增边界：`UnLua` 的零胶水主路径只覆盖 `UE reflection` 可命名对象，`Angelscript` 的显式 bind 可以主动暴露非反射工具命名空间

前面几轮已经说明过两者在“主路径是反射还是手写绑定”上的差异。这一轮只补一个更靠 API 表面的结论：**`UnLua` 的零胶水主路径并不等于“任意 UE C++ 能力都能零成本进入脚本”，它的自动面本质上被 `UField/FProperty` 边界锁住；超出这条边界，就必须切到静态导出 seam。**

`UnLua` 在 `FPropertyRegistry::CreateTypeInterface()` 里先按 Lua 值类型分流，再通过 metatable `__name` 反查 `ClassRegistry` / `EnumRegistry` / 反射字段；真正落地时，`FPropertyDesc::Create()` 也只会在 `CPT_*` 这套 `FProperty` 分类里选具体桥接器。换句话说，它的“自动暴露”默认只对 UE 已经进入反射系统的对象形状成立。仓库里确实也暴露了文件类等纯 C++ helper，但那不是零胶水主路径，而是 `BEGIN_EXPORT_CLASS` / `ADD_LIB` 这条显式静态导出通道。

`Angelscript` 在这一点上是另一套哲学。`Bind_FPaths.cpp`、`Bind_FFileHelper.cpp` 直接把 `FPaths`、`FFileHelper` 这种并不以 `UClass/UFunction` 形式存在的工具 API 作为脚本命名空间和全局函数暴露出来，因此它的脚本表面不受 UE reflection 可见性上限约束，而受“团队是否愿意手写/维护 bind”约束。

```
[D2-Deep] API Surface Ceiling
UnLua
├─ Lua value -> PropertyRegistry::CreateTypeInterface()   // 先按 Lua 值类别分流
├─ metatable.__name -> ClassRegistry / EnumRegistry       // 只认可已命名反射对象
├─ FPropertyDesc::Create(FProperty*)                      // ★ 自动桥只覆盖 FProperty 家族
└─ non-reflected helper -> BEGIN_EXPORT_CLASS / ADD_LIB   // 超出边界时切到静态导出

Angelscript
├─ Bind_FPaths.cpp                                        // ★ 直接暴露 FPaths namespace
├─ Bind_FFileHelper.cpp                                  // ★ 直接暴露文件 helper
└─ surface ceiling = maintained bind set                 // 表面上限由显式 bind 集合决定
```

关键源码 [1]：`UnLua` 的零胶水入口先找 metatable / reflected field，再交给 `FPropertyDesc`

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/PropertyRegistry.cpp
// 函数: FPropertyRegistry::CreateTypeInterface
// 行号: 25-91
// 位置: 根据 Lua 值类型和 metatable.__name 反查 reflected type
// ============================================================================
TSharedPtr<ITypeInterface> FPropertyRegistry::CreateTypeInterface(lua_State* L, int32 Index)
{
    Index = LowLevel::AbsIndex(L, Index);

    TSharedPtr<ITypeInterface> TypeInterface;
    int32 Type = lua_type(L, Index);
    switch (Type)
    {
    case LUA_TBOOLEAN:
        TypeInterface = GetBoolProperty();
        break;
    case LUA_TNUMBER:
        TypeInterface = lua_isinteger(L, Index) > 0 ? GetIntProperty() : GetFloatProperty();
        break;
    case LUA_TTABLE:
        {
            lua_pushstring(L, "__name");
            Type = lua_rawget(L, Index);
            if (Type == LUA_TSTRING)
            {
                const char* Name = lua_tostring(L, -1);
                auto ClassDesc = Env->GetClassRegistry()->Find(Name);
                if (ClassDesc)
                {
                    TypeInterface = GetFieldProperty(ClassDesc->AsStruct()); // ★ 命中的是 reflected UStruct
                }
                else
                {
                    auto EnumDesc = Env->GetEnumRegistry()->Find(Name);
                    if (EnumDesc)
                        TypeInterface = GetFieldProperty(EnumDesc->GetEnum()); // ★ 命中的是 reflected UEnum
                    else
                        TypeInterface = FindTypeInterface(lua_tostring(L, -1));
                }
            }
            lua_pop(L, 1);
        }
        break;
    case LUA_TUSERDATA:
        {
            lua_getmetatable(L, Index);
            if (lua_istable(L, -1))
            {
                lua_getfield(L, -1, "__name");
                if (lua_isstring(L, -1))
                {
                    const char* Name = lua_tostring(L, -1);
                    FClassDesc* ClassDesc = Env->GetClassRegistry()->Find(Name);
                    if (ClassDesc)
                        TypeInterface = GetFieldProperty(ClassDesc->AsStruct()); // ★ 仍然回到 reflected class/struct
                }
                lua_pop(L, 1);
            }
            lua_pop(L, 1);
        }
        break;
    default:
        break;
    }

    return TypeInterface;
}
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp
// 函数: FPropertyDesc::Create
// 行号: 1537-1616
// 位置: 自动桥接器只覆盖 UE reflection 的 FProperty 分类
// ============================================================================
FPropertyDesc* FPropertyDesc::Create(FProperty *InProperty)
{
    FPropertyDesc* PropertyDesc = nullptr;
    int32 Type = ::GetPropertyType(InProperty);
    switch (Type)
    {
    case CPT_Int8:
    case CPT_Int16:
    case CPT_Int:
    case CPT_Int64:
    case CPT_UInt16:
    case CPT_UInt32:
    case CPT_UInt64:
        {
            PropertyDesc = new FIntegerPropertyDesc(InProperty);      // ★ 仍然是 FProperty 子类分派
            break;
        }
    case CPT_Float:
    case CPT_Double:
        {
            PropertyDesc = new FFloatPropertyDesc(InProperty);
            break;
        }
    case CPT_Enum:
        {
            PropertyDesc = new FEnumPropertyDesc(InProperty);
            break;
        }
    case CPT_ObjectReference:
    case CPT_WeakObjectReference:
    case CPT_LazyObjectReference:
        {
            PropertyDesc = new FObjectPropertyDesc(InProperty, false);
            break;
        }
    case CPT_SoftObjectReference:
        {
            PropertyDesc = new FSoftObjectPropertyDesc(InProperty);
            break;
        }
    case CPT_Interface:
        {
            PropertyDesc = new FInterfacePropertyDesc(InProperty);
            break;
        }
    case CPT_Array:
        {
            PropertyDesc = new FArrayPropertyDesc(InProperty);        // ★ 容器也仍然建立在 reflected inner property 上
            break;
        }
    ...
    }
}
```

关键源码 [2]：`UnLua` 要跨出 reflection 边界时，会显式切到导出宏通道

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaEx.h
// 宏: BEGIN_EXPORT_CLASS_EX / ADD_FUNCTION / ADD_LIB
// 行号: 482-560
// 位置: 非零胶水路径通过导出宏显式登记类、函数与 luaL_Reg
// ============================================================================
#define BEGIN_EXPORT_CLASS_EX(bIsReflected, Name, Suffix, Type, SuperTypeName, ...) \
    struct FExported##Name##Suffix##Helper \
    { \
        typedef Type ClassType; \
        ... \
        FExported##Name##Suffix##Helper() \
            : ExportedClass(nullptr) \
        { \
            UnLua::TExportedClass<bIsReflected, Type, ##__VA_ARGS__> *Class = ...; \
            if (!Class) \
            { \
                ExportedClass = new UnLua::TExportedClass<bIsReflected, Type, ##__VA_ARGS__>(#Name, SuperTypeName); \
                UnLua::ExportClass((UnLua::IExportedClass*)ExportedClass);      // ★ 这里是显式注册，不是反射自动发现
                Class = ExportedClass; \
            }

#define ADD_FUNCTION(Function) \
            Class->AddFunction(#Function, &ClassType::Function);               // ★ 手工点名导出函数

#define ADD_LIB(Lib) \
            Class->AddLib(Lib);                                                // ★ 手工挂入 luaL_Reg 表

#define IMPLEMENT_EXPORTED_CLASS(Name) \
    FExported##Name##Helper FExported##Name##Helper::StaticInstance;
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/BaseLib/LuaLib_File.cpp
// 行号: 479-488
// 位置: File helper 的暴露通过导出宏显式完成
// ============================================================================
BEGIN_EXPORT_NAMED_CLASS(File,UE4File)
ADD_LIB(UE4FileLib)
ADD_FUNCTION(Open)
ADD_FUNCTION(Close)
ADD_FUNCTION(Seek)
ADD_FUNCTION(TotalSize)
ADD_FUNCTION(Flush)
ADD_FUNCTION(IsValid)
END_EXPORT_CLASS()
IMPLEMENT_EXPORTED_CLASS(File)                                         // ★ File 不是 reflected UObject，而是手工导出 helper
```

关键源码 [3]：`Angelscript` 可以直接把非反射工具 API 暴露成脚本命名空间

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FPaths.cpp
// 行号: 6-66
// 位置: 直接把 FPaths 命名空间函数送入脚本表面
// ============================================================================
AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_FPaths(FAngelscriptBinds::EOrder::Late, []
{
    FAngelscriptBinds::FNamespace ns("FPaths");

    FAngelscriptBinds::BindGlobalFunction("FString RootDir()", &FPaths::RootDir);
    FAngelscriptBinds::BindGlobalFunction("FString EngineDir()", &FPaths::EngineDir);
    FAngelscriptBinds::BindGlobalFunction("FString ProjectDir()", &FPaths::ProjectDir);
    FAngelscriptBinds::BindGlobalFunction("bool FileExists(const FString& InPath)", &FPaths::FileExists);
    FAngelscriptBinds::BindGlobalFunction("bool DirectoryExists(const FString& InPath)", &FPaths::DirectoryExists);
    FAngelscriptBinds::BindGlobalFunction("FString ConvertRelativePathToFull(const FString& InPath)",
        [](const FString& InPath) -> FString { return FPaths::ConvertRelativePathToFull(InPath); });
});                                                                     // ★ 这里不依赖 UClass/UFunction，也不依赖 reflected property
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FFileHelper.cpp
// 行号: 30-49
// 位置: 文件读写 helper 也是直接显式暴露
// ============================================================================
AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_FFileHelper(FAngelscriptBinds::EOrder::Late, []
{
    FAngelscriptBinds::FNamespace ns("FFileHelper");

    auto EEncodingOptions_ = FAngelscriptBinds::Enum("EEncodingOptions");
    EEncodingOptions_["AutoDetect"] = FFileHelper::EEncodingOptions::AutoDetect;
    EEncodingOptions_["ForceUTF8"] = FFileHelper::EEncodingOptions::ForceUTF8;

    FAngelscriptBinds::BindGlobalFunction("bool LoadFileToString(FString& Result, const FString& Filename, FFileHelper::EHashOptions HashOptions = FFileHelper::EHashOptions::None, uint32 ReadFlags = uint32(EFileRead::None))",
        [](FString& Result, const FString& Filename, FFileHelper::EHashOptions HashOptions, uint32 ReadFlags) { return FFileHelper::LoadFileToString(Result, *Filename, HashOptions, ReadFlags); });
    FAngelscriptBinds::BindGlobalFunction("bool SaveStringToFile(const FString& String, const FString& Filename, FFileHelper::EEncodingOptions EncodingOptions = FFileHelper::EEncodingOptions::AutoDetect, uint32 WriteFlags = uint32(EFileWrite::None))",
        [](const FString& String, const FString& Filename, FFileHelper::EEncodingOptions EncodingOptions, uint32 WriteFlags) { return FFileHelper::SaveStringToFile(String, *Filename, EncodingOptions, &IFileManager::Get(), WriteFlags); });
});                                                                     // ★ 非反射工具函数直接成为脚本 API
```

新增对比结论：

- `UnLua` 不是 `没有非反射 helper`，而是 `零胶水主路径没有覆盖非反射 helper`；一旦跨出 `UField/FProperty` 边界，就要进入 `BEGIN_EXPORT_CLASS/ADD_LIB` 这条显式导出通道。
- `Angelscript` 在这个点上不是“自动化更强”，而是 `实现方式不同`：它承认 API surface 需要人工治理，所以可以把 `FPaths`、`FFileHelper` 这类 reflection 外能力直接纳入脚本表面。
- 如果比较“能否在不写额外导出代码的情况下消费非反射 UE 工具 API”，相对 `Angelscript` 当前快照，`UnLua` 的零胶水主路径属于 `没有实现`。
- 如果比较“对常规 `UClass/UFunction/FProperty` 暴露的边际成本”，`UnLua` 仍有明显优势；但这是以脚本表面被 reflection ceiling 限制为代价换来的。

### [维度 D3] Blueprint schema 增长权：`UnLua` 主要替换已有事件槽位，`Angelscript` 能把脚本成员生成成真实 `UPROPERTY/UFunction`

前几轮已经讨论过 override 链和 Blueprint 入口归属。本轮补的不是“谁能覆写事件”，而是**脚本是否有能力把新的 reflected schema 写回 UE 类型系统**。这件事对 plugin 交付面很关键，因为它决定了脚本究竟只是“挂在 Blueprint 上的行为层”，还是“能增长 Blueprint 可见成员表的类型层”。

`UnLua` 的 `BindClass()` 会先收集 Lua table 里的函数名，再从 `ULuaFunction::GetOverridableFunctions()` 给出的 `UEFunctions` 里按名字匹配；只有名字命中已有 `UFunction`，才会走 `ULuaFunction::Override()`。后续 `SetActive()` 的工作也主要是替换 native thunk 或把包装后的 `ULuaFunction` 放进 `FunctionMap`。这说明它的核心能力是“接管现有槽位”，不是“根据 Lua 源定义新增 reflected property/function schema”。

`Angelscript` 则明确支持 schema 增长。预处理阶段 `ProcessPropertyMacro()` 先把脚本里的 `UPROPERTY` 记成 `FAngelscriptPropertyDesc`；类生成阶段 `AddClassProperties()` 再对这些描述调用 `PropertyType.CreateProperty()` 真正生成 `FProperty`；而函数侧甚至会 `NewObject<UFunction>(NewClass, *FuncName, RF_Public)` 把新函数挂入 `UStruct::Children` 和 `FunctionMap`。因此脚本写出的成员不只是“运行时可调用”，而是会真正变成 Blueprint / reflection 系统看得见的 schema。

```
[D3-Deep] Schema Growth Ownership
UnLua
├─ Lua module -> GetFunctionNames()                  // 先拿 Lua table 里的函数名
├─ GetOverridableFunctions(Class, UEFunctions)       // 只拿已有 UFunction 槽位
├─ if name matched -> ULuaFunction::Override()       // ★ 替换已有入口
└─ no property/function synthesis from Lua source    // 默认不从 Lua 文本生成新 schema

Angelscript
├─ UPROPERTY/UFUNCTION in script -> descriptor       // 预处理阶段先建描述
├─ AddClassProperties() -> CreateProperty()          // ★ 生成真实 FProperty
├─ NewObject<UFunction>(NewClass, Name)              // ★ 生成真实 UFunction
└─ Blueprint sees generated schema                   // 新成员进入 UE reflection
```

关键源码 [1]：`UnLua` 的 `BindClass()` 只会拿 Lua 名字去命中现有 `UEFunctions`

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaManager.cpp
// 函数: UUnLuaManager::BindClass
// 行号: 300-340
// 位置: 只替换 Class 上已经存在的可覆写 UFunction
// ============================================================================
auto& BindInfo = Classes.Add(Class);
BindInfo.Class = Class;
BindInfo.ModuleName = InModuleName;
BindInfo.TableRef = Ref;

UnLua::LowLevel::GetFunctionNames(Env->GetMainState(), Ref, BindInfo.LuaFunctions);
ULuaFunction::GetOverridableFunctions(Class, BindInfo.UEFunctions);

// 用LuaTable里所有的函数来替换Class上对应的UFunction
for (const auto& LuaFuncName : BindInfo.LuaFunctions)
{
    UFunction** Func = BindInfo.UEFunctions.Find(LuaFuncName);
    if (Func)
    {
        UFunction* Function = *Func;
        ULuaFunction::Override(Function, Class, LuaFuncName);        // ★ 名字命中已有 UFunction 才会覆写
    }
}

if (Class->IsChildOf<UAnimInstance>())
{
    for (const auto& LuaFuncName : BindInfo.LuaFunctions)
    {
        if (!BindInfo.UEFunctions.Find(LuaFuncName) && LuaFuncName.ToString().StartsWith(TEXT("AnimNotify_")))
            ULuaFunction::Override(AnimNotifyFunc, Class, LuaFuncName); // ★ 特例也仍然是借现有事件缝隙挂接
    }
}
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaFunction.cpp
// 函数: ULuaFunction::SetActive
// 行号: 197-239
// 位置: 覆写的核心动作是替换 thunk / FunctionMap，而不是生成一批新的 property schema
// ============================================================================
void ULuaFunction::SetActive(const bool bActive)
{
    ...
    if (bActive)
    {
        if (bAdded)
        {
            check(!Class->FindFunctionByName(GetFName(), EIncludeSuperFlag::ExcludeSuper));
            SetSuperStruct(Function);
            FunctionFlags |= FUNC_Native;
            ClearInternalFlags(EInternalObjectFlags::Native);
            SetNativeFunc(execCallLua);

            Class->AddFunctionToFunctionMap(this, *GetName());        // ★ 新增的是 Lua wrapper function map 项
            if (Function->HasAnyFunctionFlags(FUNC_Native))
                Class->AddNativeFunction(*GetName(), &ULuaFunction::execCallLua);
        }
        else
        {
            SetSuperStruct(Function->GetSuperStruct());
            Script = Function->Script;
            Children = Function->Children;
            ChildProperties = Function->ChildProperties;
            PropertyLink = Function->PropertyLink;

            Function->FunctionFlags |= FUNC_Native;
            Function->SetNativeFunc(&execScriptCallLua);              // ★ 直接接管已有 UFunction 的 native thunk
            Function->GetOuterUClass()->AddNativeFunction(*Function->GetName(), &execScriptCallLua);
            Function->Script.Empty();
            ...
        }
    }
}
```

关键源码 [2]：`Angelscript` 先在预处理阶段记下脚本 property，再在类生成阶段物化为 `FProperty`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp
// 函数: FAngelscriptPreprocessor::ProcessPropertyMacro
// 行号: 2395-2405
// 位置: 脚本中的 UPROPERTY 先变成 property descriptor
// ============================================================================
void FAngelscriptPreprocessor::ProcessPropertyMacro(FFile& File, FChunk& Chunk, FMacro& Macro)
{
    // Create the property descriptor
    auto PropDesc = MakeShared<FAngelscriptPropertyDesc>();
    PropDesc->LineNumber = Macro.FileLineNumber;

    auto ClassDesc = Chunk.ClassDesc;
    if (!ensure(ClassDesc.IsValid()))
        return;

    ClassDesc->Properties.Add(PropDesc);                              // ★ 脚本字段先进入类描述

    PropDesc->PropertyName = Macro.Name;
    PropDesc->LiteralType = Macro.SubjectType;
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 函数: AddClassProperties
// 行号: 2921-2957
// 位置: property descriptor 会被真正物化成 UE FProperty
// ============================================================================
if (PropDesc.IsValid())
{
    // Add the property as an exported UPROPERTY() on the class
    FAngelscriptTypeUsage PropertyType = PropDesc->PropertyType;

    FAngelscriptType::FPropertyParams Params;
    Params.Struct = InStruct;
    Params.Outer = InStruct;
    Params.PropertyName = FName(ScriptProp->name.AddressOf());

    FProperty* NewProperty = PropertyType.CreateProperty(Params);     // ★ 这里真正创建反射属性

    PropDesc->bHasUnrealProperty = true;

    for (auto& Elem : PropDesc->Meta)
        NewProperty->SetMetaData(Elem.Key, *Elem.Value);

    if (PropDesc->bReplicated)
    {
        NewProperty->SetPropertyFlags(CPF_Net);
        NewProperty->SetBlueprintReplicationCondition(PropDesc->ReplicationCondition);
        ...
    }
}
```

关键源码 [3]：`Angelscript` 还能直接向新类挂入真实 `UFunction`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 行号: 2820-2830
// 位置: runtime class generation 直接创建新的 BlueprintEvent UFunction
// ============================================================================
UFunction* NewFunction = NewObject<UFunction>(NewClass, *FuncName, RF_Public);
NewFunction->FunctionFlags = FUNC_Event | FUNC_BlueprintEvent | FUNC_Public;
NewFunction->ReturnValueOffset = MAX_uint16;
NewFunction->FirstPropertyToInit = nullptr;
NewFunction->Bind();
NewFunction->StaticLink(true);

// Link into UStruct::Children so TFieldIterator<UFunction> can find it
NewFunction->Next = NewClass->Children;
NewClass->Children = NewFunction;
NewClass->AddFunctionToFunctionMap(NewFunction, NewFunction->GetFName()); // ★ 新函数进入反射可枚举链
```

新增对比结论：

- `UnLua` 不是 `没有 Blueprint 覆写`，而是它的核心能力集中在 `override existing slots`；当前仓内没有同层“从 Lua 文本生成新的 Blueprint-visible property/function schema”的实现证据。
- `Angelscript` 在这一层体现为明确的 `实现方式不同`：脚本类不仅能替换行为，还能增长 UE reflection schema，自然也更适合作为 Blueprint 父类继续被编辑器消费。
- 如果比较“脚本能否新增一个真正出现在 reflection/Blueprint 成员表里的字段或函数”，相对 `Angelscript` 当前快照，`UnLua` 属于 `没有实现`。
- 这不是简单的高下判断。`UnLua` 因为不增长 schema，所以绑定更轻、对既有资产侵入更小；`Angelscript` 则用更重的 class generation 换来了可继承、可编辑、可复制的类型层能力。

### [维度 D9] latent 测试编排的 ownership：`UnLua` 复用 UE 的 map/PIE latent harness，`Angelscript` 把 latent command 做成可复制脚本对象

前面几轮已经分析过 runner、CI 和 fixture 形态。这一轮补的是**异步测试编排单元到底是什么**。`UnLua` 和 `Angelscript` 都有 latent / automation 测试，但二者把“异步步骤”的所有权放在完全不同的层。

`UnLua` 的 latent test 本质上是 UE Automation framework 上的一层 DSL 包装。`IMPLEMENT_UNLUA_LATENT_TEST` 展开后只是把 `SetUpTest`、`PerformTest`、`TearDownTest` 三个 latent command 挂进 UE 队列；具体的场景装配则交给 `FOpenMapLatentCommand` 负责，它会在 editor 中装配 map、发起 PIE、等待 world ready，然后在 issue test 里通过 `FFunctionLatentCommand` 执行业务断言。也就是说，UnLua 的异步测试单元仍然是“Automation latent command + map session + Lua chunk/assert”。

`Angelscript` 则把 latent step 抬成了脚本可定义的 `ULatentAutomationCommand` UObject。它既有 `Before/Update/After` 生命周期，也实现了 `CallRemoteFunction()` 与 `GetLifetimeReplicatedProps()`，还能在 `RunsOnClient()` 时由 `ALatentAutomationCommandClientExecutor` 复制到客户端执行。结果是：异步测试场景、网络复制、脚本态状态字段，全部进入同一个测试对象 contract。

```
[D9-Deep] Latent Test Orchestration Unit
UnLua
├─ IMPLEMENT_UNLUA_LATENT_TEST                       // 生成 UE automation test 壳
├─ FOpenMapLatentCommand                            // 打开 map / PIE / 等待 world
├─ FFunctionLatentCommand                           // 在回调里跑 Lua chunk 与断言
└─ async unit = automation command + map session    // 异步所有权在 harness

Angelscript
├─ ULatentAutomationCommand                         // 脚本定义 Before/Update/After
├─ AddLatentAutomationCommand()                     // 绑定到 integration test
├─ CallRemoteFunction / RepLifetimeProps            // ★ latent command 自带网络与复制语义
└─ ALatentAutomationCommandClientExecutor           // ★ 客户端执行器复制子对象
```

关键源码 [1]：`UnLua` latent test 只是对 UE automation latent command 的打包

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Public/UnLuaTestCommon.h
// 宏: IMPLEMENT_UNLUA_LATENT_TEST
// 行号: 171-185
// 位置: latent test 生成的是 UE automation test + 三段 latent command
// ============================================================================
#define IMPLEMENT_UNLUA_LATENT_TEST(TestClass, PrettyName) \
IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass##_Runner, PrettyName, (EAutomationTestFlags::ClientContext | EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)) \
bool TestClass##_Runner::RunTest(const FString& Parameters) \
{ \
/* spawn test instance. Setup should be done in test's constructor */ \
TestClass* TestInstance = new TestClass(); \
TestInstance->SetTestRunner(*this); \
/* set up */ \
ADD_LATENT_AUTOMATION_COMMAND(FUnLuaTestCommand_SetUpTest(TestInstance)); \
/* run latent command to update */ \
ADD_LATENT_AUTOMATION_COMMAND(FUnLuaTestCommand_PerformTest(TestInstance)); \
/* run latent command to tear down */ \
ADD_LATENT_AUTOMATION_COMMAND(FUnLuaTestCommand_TearDownTest(TestInstance)); \
return true; \
}                                                                       // ★ 异步语义本身仍然托管给 UE automation framework
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Public/TestCommands.h
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/TestCommands.cpp
// 行号: 24-37, 43-127
// 位置: map/PIE orchestration 也是以 latent command 形式存在
// ============================================================================
class FOpenMapLatentCommand : public IAutomationLatentCommand
{
public:
    explicit FOpenMapLatentCommand(FString MapName, bool bForceReload = false);
    virtual bool Update() override;
    void LoadMap();

private:
    FString MapName;
    bool bForceReload;
    TUniquePtr<FWaitForMapToLoadCommand> WaitForMapToLoad;
};

void FOpenMapLatentCommand::LoadMap()
{
    ...
    if (bNeedLoadEditorMap || bForceReload)
    {
        if (bPieRunning)
            GEditor->EndPlayMap();
        FEditorFileUtils::LoadMap(*MapName, false, true);             // ★ 先切 editor map
    }

    ...
    GEditor->RequestPlaySession(RequestParams);
    GEditor->StartQueuedPlaySessionRequest();                         // ★ 再拉起 PIE
    WaitForMapToLoad = MakeUnique<FWaitForMapToLoadCommand>();       // ★ 然后等待 world ready
}
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Tests/IssueOverridesTest.cpp
// 函数: FIssueOverridesTest::RunTest
// 行号: 27-55
// 位置: 具体 issue 回归由 map command + function callback 组合起来
// ============================================================================
bool FIssueOverridesTest::RunTest(const FString& Parameters)
{
    const auto MapName = TEXT("/UnLuaTestSuite/Tests/Regression/IssueOverrides/IssueOverrides.IssueOverrides");
    ADD_LATENT_AUTOMATION_COMMAND(FOpenMapLatentCommand(MapName))
    ADD_LATENT_AUTOMATION_COMMAND(FEngineWaitLatentCommand(0.5));
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this] {
        const auto L = UnLua::GetState();
        lua_getglobal(L, "Counter");
        const auto Result = (int)lua_tointeger(L, -1);
        TestEqual(TEXT("Counter"), Result, 4);                        // ★ 业务断言发生在 callback 里
        ...
        return true;
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}
```

关键源码 [2]：`Angelscript` 把 latent step 做成脚本对象，并让它天然具备网络调用与复制能力

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Testing/LatentAutomationCommand.h
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Testing/LatentAutomationCommand.cpp
// 行号: 9-75, 89-127
// 位置: latent automation command 本身就是可脚本化、可复制的 UObject
// ============================================================================
UCLASS(Blueprintable)
class ULatentAutomationCommand : public UObject
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintNativeEvent)
    void Before();

    UFUNCTION(BlueprintNativeEvent)
    bool Update();

    UFUNCTION(BlueprintNativeEvent)
    void After();

    UFUNCTION(BlueprintCallable, Category = "LatentAutomationCommand")
    bool HasAuthority() const;

    bool RunsOnClient() const;
    bool IsSupportedForNetworking() const override;
    int32 GetFunctionCallspace(UFunction* Function, FFrame* Stack) override;
    bool CallRemoteFunction(UFunction* Function, void* Parms, struct FOutParmRec* OutParms, FFrame* Stack) override;
    void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    ...
};

bool ULatentAutomationCommand::IsSupportedForNetworking() const
{
    return true;                                                      // ★ latent command 自己声明支持网络
}

bool ULatentAutomationCommand::CallRemoteFunction(UFunction* Function, void* Parms, struct FOutParmRec* OutParms, FFrame* Stack)
{
    AActor* Owner = GetTypedOuter<AActor>();
    UNetDriver* NetDriver = Owner->GetNetDriver();
    if (NetDriver)
    {
        NetDriver->ProcessRemoteFunction(Owner, Function, Parms, OutParms, Stack, this); // ★ RPC 直接从 latent command 发出
        return true;
    }
    return false;
}

void ULatentAutomationCommand::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    DOREPLIFETIME(ULatentAutomationCommand, bAllowTimeout);
    DOREPLIFETIME(ULatentAutomationCommand, bAlsoRunOnClient);

    UASClass* asClass = Cast<UASClass>(GetClass());
    if (asClass && asClass->ScriptTypePtr != nullptr)
    {
        asClass->GetLifetimeScriptReplicationList(OutLifetimeProps);  // ★ 脚本字段也能进入复制清单
    }
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Testing/IntegrationTest.cpp
// 函数: BindAddLatentAutomationCommand / AngelscriptLatentCommandClient::Update
// 行号: 671-708, 521-557
// 位置: integration test 会在需要时拉起客户端执行器来复制 latent command
// ============================================================================
void BindAddLatentAutomationCommand(FAngelscriptBinds& T)
{
    const FString Signature = FString::Printf(TEXT("void AddLatentAutomationCommand(ULatentAutomationCommand LatentCommand, float32 TimeoutSecs=%f)"), DEFAULT_LATENT_COMMAND_TIMEOUT);
    T.Method(Signature, [](FAngelscriptIntegrationTest& Self, ULatentAutomationCommand& LatentCommand, float TimeoutSecs) {
        ...
        LatentCommand.SetWorld(TestWorld);
        LatentCommand.SetAssociatedTest(Self.AsShared());             // ★ latent command 与脚本测试对象关联
        ...
        if (LatentCommand.RunsOnClient())
        {
            FLatentCommandClientStateMachine ClientState = FLatentCommandClientStateMachine();
            ALatentAutomationCommandClientExecutor* ClientExecutor = nullptr;
            ADD_LATENT_AUTOMATION_COMMAND(AngelscriptLatentCommandClient(Self, Context, LatentCommand, ClientState, ClientExecutor));
        }
        else
        {
            ADD_LATENT_AUTOMATION_COMMAND(AngelscriptLatentCommand(Self, Context, LatentCommand));
        }
    });
}

bool AngelscriptLatentCommandClient::Update()
{
    ...
    case EClientLatentCommandState::CREATE_CLIENT_EXECUTOR:
    {
        APlayerController* Controller = GetTestWorld()->GetFirstPlayerController();
        if (Controller == nullptr)
            break;

        FActorSpawnParameters SpawnParms;
        SpawnParms.Owner = Controller;
        ClientExecutor = GetTestWorld()->SpawnActor<ALatentAutomationCommandClientExecutor>(ALatentAutomationCommandClientExecutor::StaticClass(), SpawnParms);
        ClientExecutor->SetTest(&LatentCommand);                     // ★ 客户端执行器持有并复制这个 latent command
        ClientState.SetCurrentState(EClientLatentCommandState::SETUP);
        break;
    }
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Testing/LatentAutomationCommandClientExecutor.cpp
// 函数: ReplicateSubobjects / GetLifetimeReplicatedProps
// 行号: 237-258
// 位置: 客户端执行器把 latent command 作为 replicated subobject 发送
// ============================================================================
bool ALatentAutomationCommandClientExecutor::IsSupportedForNetworking() const
{
    return true;
}

bool ALatentAutomationCommandClientExecutor::ReplicateSubobjects(UActorChannel* Channel, FOutBunch* Bunch, FReplicationFlags* RepFlags)
{
    bool bWroteSomething = Super::ReplicateSubobjects(Channel, Bunch, RepFlags);

    bWroteSomething |= Channel->ReplicateSubobject(LatentCommand, *Bunch, *RepFlags); // ★ latent command 本体被复制给客户端

    return bWroteSomething;
}

void ALatentAutomationCommandClientExecutor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    DOREPLIFETIME(ALatentAutomationCommandClientExecutor, LatentCommand);
    DOREPLIFETIME(ALatentAutomationCommandClientExecutor, bCanStartBefore);
    DOREPLIFETIME(ALatentAutomationCommandClientExecutor, bCanStartUpdate);
    DOREPLIFETIME(ALatentAutomationCommandClientExecutor, bCanStartAfter);
}
```

新增对比结论：

- `UnLua` 不是 `没有 latent test`，它的 latent harness 很完整；但当前仓内证据显示，它的异步编排单元仍然是 `UE automation latent command + map/PIE session`，而不是脚本自带的 replicated test object。
- `Angelscript` 在这一层是明确的 `实现方式不同`：脚本作者写的是 `ULatentAutomationCommand` 对象本身，异步状态、网络 RPC、复制字段和客户端执行器都围绕这一个 contract 组织。
- 如果比较“脚本测试步骤能否自己成为可复制 UObject，并把脚本字段进入 replication list”，相对 `Angelscript` 当前快照，`UnLua` 属于 `没有实现同层 contract`。
- 从适用场景看，`UnLua` 更适合 issue/map 复现型回归；`Angelscript` 更适合验证脚本 runtime、复制、调试器、hot reload 等需要测试对象长期持有状态的场景。这是能力焦点差异，不是简单优劣。

---

## 深化分析 (2026-04-09 06:48:30)

### [维度 D5] 调试传输栈的 ownership：`UnLua` 把 socket 能力做成 Lua 扩展，`Angelscript` 把 transport/session 做进 runtime

前面的 `D5` 已经区分过 `debug state provider` 和 `debug session host`。这一轮继续往下拆，关注**调试链路真正“上线连线”的那一层由谁持有**。当前快照里的 `UnLua` 并不是没有网络侧能力，但这个能力并不归核心 `UnLua` runtime 所有：`Reference/UnLua/Docs/CN/Debugging.md:5-14` 直接要求用户把 `LuaPanda.lua` 放进工程脚本目录，并在 Lua 代码里主动执行 `require("LuaPanda").start("127.0.0.1", 8818)`；同时 `Reference/UnLua/Plugins/UnLuaExtensions/LuaSocket/Source/Private/LuaSocketModule.cpp:20-36` 说明 `LuaSocket` 是一个独立扩展插件，它只是在 `FLuaEnv::OnCreated` 时注入 `socket` / `mime` loader，并把扩展脚本目录拼到 `UnLua.PackagePath`。也就是说，仓内交付的是**transport primitive**，不是完整的仓内会话宿主。

`Angelscript` 的 ownership 明显更靠内核。`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h:10-31,584-693` 直接把 `TcpListener`、`FSocket` client 列表、`QueuedSends` 和消息类型枚举放进 runtime 调试器对象；`.../AngelscriptDebugServer.cpp:402-408,897-907` 又在构造时起 `FTcpListener`，收到 `StartDebugging` 后立刻回发 `DebugServerVersion`。因此 `Angelscript DebugServer V2` 的关键差异不是“也能连 IDE”，而是**transport、message envelope、client bookkeeping、session handshake 全都在仓内 C++ runtime 里闭合**。

```
[D5-Deep] Debug Transport Ownership
UnLua
├─ Docs/CN/Debugging.md -> require("LuaPanda").start(...)   // 会话启动由用户脚本触发
├─ Plugins/UnLuaExtensions/LuaSocket                        // socket 能力位于扩展插件
├─ FLuaSocketModule::OnLuaEnvCreated()                      // 向 FLuaEnv 注入 socket/mime loader
└─ external Lua debugger owns session/protocol             // 仓内不持有同层调试会话栈

Angelscript
├─ FAngelscriptDebugServer ctor -> FTcpListener(...)       // runtime 自己监听端口
├─ PendingClients / Clients / QueuedSends                  // 仓内维护 client 生命周期
├─ HandleMessage(StartDebugging)                           // 协议握手入口
└─ SendMessageToClient(DebugServerVersion)                 // 同一协议栈回发版本
```

关键源码 [1]：`UnLua` 的调试接入说明与 `LuaSocket` 扩展注入方式

```cpp
// ============================================================================
// 文件: Reference/UnLua/Docs/CN/Debugging.md
// 行号: 5-14
// 位置: 调试文档明确要求用户自己在 Lua 脚本里启动外部调试器
// ============================================================================
1. 从VSCode应用市场安装 LuaPanda / LuaHelper
2. 从 LuaPanda 官方仓库获取 `LuaPanda.lua`，放入 `{UE工程}/Content/Script` 目录
3. 在Lua代码中加入 `require("LuaPanda").start("127.0.0.1",8818)`   // ★ 会话启动由脚本主动发起

注：调试器依赖 `luasocket`，UnLua 已通过扩展插件集成；                       // ★ transport 依赖位于扩展层
如果发现无法连接请检查 `{UE工程}/Plugins/UnLuaExtensions` 目录是否存在。
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaExtensions/LuaSocket/Source/Private/LuaSocketModule.cpp
// 函数: FLuaSocketModule::StartupModule / OnLuaEnvCreated
// 行号: 20-36
// 位置: LuaSocket 通过 FLuaEnv 创建回调向 Lua VM 注入 transport primitive
// ============================================================================
void FLuaSocketModule::StartupModule()
{
    UnLua::FLuaEnv::OnCreated.AddStatic(&FLuaSocketModule::OnLuaEnvCreated);    // ★ 等 LuaEnv 建好后再挂 transport 能力
}

void FLuaSocketModule::OnLuaEnvCreated(UnLua::FLuaEnv& Env)
{
    using namespace UnLuaExtensions::LuaSocket;
    Env.AddBuiltInLoader(TEXT("socket"), luaopen_socket_core);
    Env.AddBuiltInLoader(TEXT("socket.core"), luaopen_socket_core);
    Env.AddBuiltInLoader(TEXT("mime.core"), luaopen_mime_core);
    Env.DoString("UnLua.PackagePath = UnLua.PackagePath .. ';/Plugins/UnLuaExtensions/LuaSocket/Content/Script/?.lua'");
    // ★ runtime 这里只负责把 socket 模块塞进 Lua 环境，不负责调试握手与 client 生命周期
}
```

关键源码 [2]：`Angelscript` 把 transport、消息封装与握手都做进 `DebugServer`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 行号: 10-31, 584-693, 402-408, 897-907
// 位置: runtime 自己持有 listener、client 队列与协议握手消息
// ============================================================================
#include "Common/TcpListener.h"

enum class EDebugMessageType : uint8
{
    Diagnostics,
    RequestDebugDatabase,
    DebugDatabase,
    StartDebugging,                                                   // ★ 握手消息类型进入仓内协议枚举
    ...
    DebugServerVersion,
};

class FTcpListener* Listener;
TQueue<class FSocket*, EQueueMode::Mpsc> PendingClients;
TArray<class FSocket*> Clients;
TMap<FSocket*, TArray<FQueuedMessage>> QueuedSends;                   // ★ 发送队列也由 runtime 持有

FAngelscriptDebugServer::FAngelscriptDebugServer(FAngelscriptEngine* InOwnerEngine, int Port)
{
    OwnerEngine = InOwnerEngine;
    Listener = new FTcpListener(FIPv4Endpoint(FIPv4Address::Any, Port));
    Listener->OnConnectionAccepted().BindRaw(this, &FAngelscriptDebugServer::HandleConnectionAccepted);
}

else if (MessageType == EDebugMessageType::StartDebugging)
{
    FStartDebuggingMessage Msg;
    *Datagram << Msg;

    bIsDebugging = true;
    AngelscriptDebugServer::DebugAdapterVersion = Msg.DebugAdapterVersion;

    FDebugServerVersionMessage DebugServerVersionMessage;
    DebugServerVersionMessage.DebugServerVersion = DEBUG_SERVER_VERSION;
    SendMessageToClient(Client, EDebugMessageType::DebugServerVersion, DebugServerVersionMessage); // ★ 同一协议栈立即回发版本
}
```

新增对比结论：

- `UnLua` 当前快照不是 `没有调试网络能力`；它已经把 `LuaSocket` 作为扩展插件交付，并主动注入到 `FLuaEnv`。但这仍然属于 `实现方式不同`，因为 transport primitive 与 debug session host 被拆成了两层。
- 如果比较“仓库自身是否拥有同层 TCP listener + message envelope + session handshake”，相对 `Angelscript DebugServer V2`，`UnLua` 当前快照属于 `没有实现`。
- `UnLua` 的好处是能直接复用 Lua 调试器生态，仓内维护面更小；代价是会话协定、适配器兼容与首连步骤不由插件自己完全控制。
- `Angelscript` 的好处是调试 transport 可以被仓内测试、dump 和协议版本统一约束；代价是插件要自己承担协议演进与多客户端生命周期管理。

### [维度 D9] 测试环境可虚拟化程度：`UnLua` 以真实地图/资产回放问题，`Angelscript` 把文件系统与脚本根发现做成依赖注入缝

这一轮补的不是“测试多不多”，而是**测试是不是必须拉起真实工程资产**。`UnLua` 当前快照的测试 DSL 明显围绕真实工程场景组织：`Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Public/UnLuaTestCommon.h:171-197` 把 test body 包装成 `IMPLEMENT_UNLUA_LATENT_TEST / IMPLEMENT_UNLUA_INSTANT_TEST`；`.../Private/Tests/IssueOverridesTest.cpp:27-55` 直接写死 `"/UnLuaTestSuite/Tests/Regression/IssueOverrides/IssueOverrides.IssueOverrides"` map 路径，再在 live `lua_State` 里取全局变量、跑 `RunChunk()`。这说明 `UnLua` 的 regression contract 很强，但它天然把“真实资产存在、路径正确、PIE/world 正常启动”也变成了测试前提。

`Angelscript` 则额外多了一层**可纯 C++ 断言的环境逻辑测试**。`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:86-95` 把 `GetProjectDir`、`DirectoryExists`、`MakeDirectory`、`GetEnabledPluginScriptRoots` 都抽成 `FAngelscriptEngineDependencies`；`.../Tests/AngelscriptDependencyInjectionTests.cpp:47-90,143-188,194-240` 再直接在测试里注入假的 project/plugin root、假的目录存在性和假的建目录行为，最后断言 `DiscoverScriptRoots()` 的排序、去重、缺失 root 跳过和 editor 自动建目录逻辑。也就是说，`Angelscript` 并不是只会用真实 world 跑回归，它还把一部分宿主环境逻辑提前提炼成了**无资产、无磁盘、无地图的 pure contract test**。

```
[D9-Deep] Test Environment Virtualization
UnLua
├─ IMPLEMENT_UNLUA_* macros                         // 在 UE Automation 外再包一层 DSL
├─ FOpenMapLatentCommand                            // 真实 map / PIE orchestration
├─ IssueOverridesTest -> "/UnLuaTestSuite/Tests/..." // 回归强依赖 sample assets
└─ lua_State / RunChunk()                           // 断言发生在 live VM 上

Angelscript
├─ FAngelscriptEngineDependencies                   // 文件系统与脚本根是可注入依赖
├─ CreateForTesting(Config, Dependencies)           // 创建隔离测试引擎
├─ DiscoverScriptRoots(false)                       // 直接断言排序/去重/缺失 root 处理
└─ no map / no asset / no disk required             // 纯 contract test
```

关键源码 [1]：`UnLua` 的测试 DSL 默认把真实地图与 live Lua VM 当作回归载体

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Public/UnLuaTestCommon.h
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Tests/IssueOverridesTest.cpp
// 行号: 171-197, 27-55
// 位置: test macro 负责挂 latent/instant command；具体回归直接绑定真实 map 与 lua_State
// ============================================================================
#define IMPLEMENT_UNLUA_LATENT_TEST(TestClass, PrettyName) \
IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass##_Runner, PrettyName, (...)) \
bool TestClass##_Runner::RunTest(const FString& Parameters) \
{ \
    TestClass* TestInstance = new TestClass(); \
    TestInstance->SetTestRunner(*this); \
    ADD_LATENT_AUTOMATION_COMMAND(FUnLuaTestCommand_SetUpTest(TestInstance)); \
    ADD_LATENT_AUTOMATION_COMMAND(FUnLuaTestCommand_PerformTest(TestInstance)); \
    ADD_LATENT_AUTOMATION_COMMAND(FUnLuaTestCommand_TearDownTest(TestInstance)); \
    return true; \
}

bool FIssueOverridesTest::RunTest(const FString& Parameters)
{
    const auto MapName = TEXT("/UnLuaTestSuite/Tests/Regression/IssueOverrides/IssueOverrides.IssueOverrides");
    ADD_LATENT_AUTOMATION_COMMAND(FOpenMapLatentCommand(MapName))                // ★ 回归直接依赖 sample map
    ADD_LATENT_AUTOMATION_COMMAND(FEngineWaitLatentCommand(0.5));
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this] {
        const auto L = UnLua::GetState();
        lua_getglobal(L, "Counter");                                             // ★ 断言依赖 live lua_State
        const auto Result = (int)lua_tointeger(L, -1);
        TestEqual(TEXT("Counter"), Result, 4);
        UnLua::RunChunk(L, "return G_IssueObject:CollectInfo()");
        ...
        return true;
    }));
    return true;
}
```

关键源码 [2]：`Angelscript` 把宿主环境发现逻辑抽成可注入依赖，再单独验证它

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDependencyInjectionTests.cpp
// 行号: 86-95, 47-90, 143-188, 194-240
// 位置: 文件系统与脚本根是可注入依赖；测试可在无真实磁盘/资产时断言发现逻辑
// ============================================================================
struct FAngelscriptEngineDependencies
{
    TFunction<FString()> GetProjectDir;
    TFunction<FString(const FString&)> ConvertRelativePathToFull;
    TFunction<bool(const FString&)> DirectoryExists;
    TFunction<bool(const FString&, bool)> MakeDirectory;
    TFunction<TArray<FString>()> GetEnabledPluginScriptRoots;                    // ★ 环境来源被显式抽象
};

bool FAngelscriptInjectedScriptRootDiscoveryTest::RunTest(const FString& Parameters)
{
    FAngelscriptEngineDependencies Dependencies;
    Dependencies.GetProjectDir = [](){ return FString(TEXT("C:/InjectedProject")); };
    Dependencies.DirectoryExists = [](const FString& Path)
    {
        return Path == TEXT("C:/InjectedProject/Script")
            || Path == TEXT("C:/Plugins/Beta/Script")
            || Path == TEXT("C:/Plugins/Alpha/Script");
    };
    Dependencies.GetEnabledPluginScriptRoots = []()
    {
        return TArray<FString>{ TEXT("C:/Plugins/Beta/Script"), TEXT("C:/Plugins/Alpha/Script") };
    };

    TUniquePtr<FAngelscriptEngine> Engine = FAngelscriptEngine::CreateForTesting(Config, Dependencies);
    TArray<FString> Roots = Engine->DiscoverScriptRoots(false);
    TestEqual(TEXT("Injected project root should be first"), Roots[0], FString(TEXT("C:/InjectedProject/Script")));
    TestEqual(TEXT("Injected plugin roots should be sorted deterministically"), Roots[1], FString(TEXT("C:/Plugins/Alpha/Script")));
}

bool FAngelscriptInjectedMissingPluginScriptRootSkipTest::RunTest(const FString& Parameters)
{
    Dependencies.GetEnabledPluginScriptRoots = []()
    {
        return TArray<FString>
        {
            TEXT("C:/Plugins/Missing/Script"),
            TEXT("C:/Plugins/Alpha/Script"),
            TEXT("C:/InjectedSkipProject/Script"),
        };
    };
    ...
    TestEqual(TEXT("Missing plugin roots should be skipped and project root should not be duplicated"), Roots.Num(), 2);
}

bool FAngelscriptInjectedEditorCreatesProjectScriptRootTest::RunTest(const FString& Parameters)
{
    Config.bIsEditor = true;
    Dependencies.DirectoryExists = [](const FString& Path) { return false; };
    Dependencies.MakeDirectory = [&bMakeDirectoryCalled, &CreatedPath](const FString& Path, bool bTree)
    {
        bMakeDirectoryCalled = true;
        CreatedPath = Path;
        return true;                                                             // ★ 是否创建目录也能被纯测试断言
    };
    ...
    TestEqual(TEXT("Created project root should be returned by discovery"), Roots[0], FString(TEXT("C:/InjectedEditorProject/Script")));
}
```

新增对比结论：

- `UnLua` 当前快照不是 `没有自动化测试`；它的 regression harness 很成熟，只是主战场放在真实 sample assets 与 live VM 复现。
- `Angelscript` 在这一层属于 `实现方式不同`：它除了 world-backed regression，还显式保留了一层依赖注入式 contract test，用来验证宿主环境逻辑。
- 如果比较“脚本宿主环境发现逻辑能否脱离真实磁盘/地图/资产做纯 C++ 验证”，相对 `Angelscript` 当前快照，`UnLua` 属于 `没有实现同层测试缝`。
- 这不是简单高下判断。`UnLua` 的做法更接近真实用户现场；`Angelscript` 的做法更利于把根发现、去重、排序、缺失容错这类基础设施问题尽早钉死。

### [维度 D10] 工作流入口的机器可发现性：`UnLua` 把搜索入口压缩进 `PackagePath` 字符串，`Angelscript` 把 script roots 做成显式 API 与 commandlet

前面的 `D10` 已经分析过文档、教程和 editor 脚手架。这一轮只补一个更偏 agent/tooling 的问题：**如果一个工具想知道“脚本到底会从哪里被加载”，它应该读哪一条 contract**。`UnLua` 当前快照的答案偏向“读文档，再改字符串”。`Reference/UnLua/Docs/CN/FAQ.md:74-82` 明确告诉用户修改 `UnLua.PackagePath`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:560-566,614-636` 又说明 runtime 在加载时先看 `CustomLoadLuaFile` delegate，再把 `PackagePath` 按 `;` 拆成 pattern，优先查 `ProjectPersistentDownloadDir`，随后再查 `ProjectDir`。这个入口非常灵活，但 contract 主要以**可变字符串 + FAQ 说明**存在。

`Angelscript` 则把“脚本入口在哪”做成了显式、可查询、可测试的 runtime contract。`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:542-565,1326-1369` 先在默认依赖里声明 `GetProjectDir` 与 `GetEnabledPluginScriptRoots`，然后 `DiscoverScriptRoots()` 统一生成 `ProjectDir/Script` 与所有 `PluginBaseDir/Script`，并保证 project root 在前、plugin roots 排序稳定；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAllScriptRootsCommandlet.cpp:5-20` 又把结果直接以 `{ "AngelscriptScriptRoots": [...] }` JSON 风格打印出来。对人来说，这是一条更稳定的 workflow entry point；对工具来说，这甚至已经接近一个 machine-readable discovery API。

```
[D10-Deep] Workflow Entry Discovery Contract
UnLua
├─ FAQ.md -> edit UnLua.PackagePath string          // 人工维护搜索路径
├─ CustomLoadLuaFile delegate                       // 可以完全接管文件加载
├─ PackagePath.ParseIntoArray(';')                  // runtime 按字符串 pattern 解析
├─ ProjectPersistentDownloadDir first               // 先查下载目录
└─ ProjectDir fallback                              // 再查工程目录

Angelscript
├─ GetProjectDir / GetEnabledPluginScriptRoots      // roots 来源是显式依赖
├─ DiscoverScriptRoots()                            // 统一生成 project + plugin Script roots
├─ deterministic sort + project root first          // 搜索顺序是稳定 contract
└─ AllScriptRootsCommandlet -> JSON-like output     // 工具可直接消费
```

关键源码 [1]：`UnLua` 的工作流入口是一条“FAQ + 可变 `PackagePath` + 可选 delegate”的组合 contract

```cpp
// ============================================================================
// 文件: Reference/UnLua/Docs/CN/FAQ.md
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp
// 行号: 74-82, 560-566, 614-636
// 位置: 自定义脚本入口主要通过 PackagePath 字符串与 CustomLoadLuaFile delegate 完成
// ============================================================================
## 为什么改了`package.path`没有效果，可以自定义`require`查找目录吗？
UE有自己的文件系统，如果有自定义查找目录的需求，可以修改 `UnLua.PackagePath` 来实现，比如：
UnLua.PackagePath = UnLua.PackagePath .. ';Plugins/UnLuaExtensions/LuaProtobuf/Content/Script/?.lua'
// ★ 文档把入口 contract 告诉给人，而不是暴露结构化 roots 查询接口

if (FUnLuaDelegates::CustomLoadLuaFile.IsBound())
{
    const FString FileName(UTF8_TO_TCHAR(lua_tostring(L, 1)));
    TArray<uint8> Data;
    FString ChunkName(TEXT("chunk"));
    if (FUnLuaDelegates::CustomLoadLuaFile.Execute(Env, FileName, Data, ChunkName))
        ...                                                               // ★ delegate 可以完全接管加载路径
}

const auto PackagePath = UnLuaLib::GetPackagePath(L);
TArray<FString> Patterns;
if (PackagePath.ParseIntoArray(Patterns, TEXT(";"), false) == 0)
    return 0;

for (auto& Pattern : Patterns)
{
    Pattern.ReplaceInline(TEXT("?"), *FileName);
    const auto PathWithPersistentDir = FPaths::Combine(FPaths::ProjectPersistentDownloadDir(), Pattern);
    FullPath = FPaths::ConvertRelativePathToFull(PathWithPersistentDir);
    if (FFileHelper::LoadFileToArray(Data, *FullPath, FILEREAD_Silent))
        return LoadIt();                                                   // ★ 先查下载目录
}

for (auto& Pattern : Patterns)
{
    const auto PathWithProjectDir = FPaths::Combine(FPaths::ProjectDir(), Pattern);
    FullPath = FPaths::ConvertRelativePathToFull(PathWithProjectDir);      // ★ 再查工程目录
}
```

关键源码 [2]：`Angelscript` 把 roots 发现与导出做成显式 contract，便于工具和 agent 直接查询

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAllScriptRootsCommandlet.cpp
// 行号: 542-565, 1326-1369, 5-20
// 位置: runtime 先定义 root discovery 依赖，再提供命令行可消费的 roots 导出入口
// ============================================================================
Dependencies.GetProjectDir = []()
{
    return FPaths::ProjectDir();
};
Dependencies.GetEnabledPluginScriptRoots = []()
{
    TArray<FString> ScriptRoots;
    for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetEnabledPluginsWithContent())
    {
        ScriptRoots.Add(Plugin->GetBaseDir() / TEXT("Script"));            // ★ 所有启用插件统一暴露 Script 根
    }
    return ScriptRoots;
};

TArray<FString> FAngelscriptEngine::DiscoverScriptRoots(bool bOnlyProjectRoot) const
{
    FString RootPath = Dependencies.ConvertRelativePathToFull(Dependencies.GetProjectDir() / TEXT("Script"));
    ...
    for (const FString& PluginScriptRoot : Dependencies.GetEnabledPluginScriptRoots())
    {
        const FString ScriptPath = Dependencies.ConvertRelativePathToFull(PluginScriptRoot);
        if (Dependencies.DirectoryExists(ScriptPath) && ScriptPath != RootPath)
        {
            DiscoveredRootPaths.Add(ScriptPath);
        }
    }
    DiscoveredRootPaths.Sort();
    DiscoveredRootPaths.Insert(RootPath, 0);                               // ★ project root 永远排第一
    return DiscoveredRootPaths;
}

int32 UAngelscriptAllScriptRootsCommandlet::Main(const FString& Params)
{
    const auto AllScriptRoots = FAngelscriptEngine::MakeAllScriptRoots();
    ...
    UE_LOG(Angelscript, Display, TEXT("{ \"AngelscriptScriptRoots\": %s}\n"), *Result);
    // ★ roots 被导出成机器可消费的 JSON 风格结果
    return 0;
}
```

新增对比结论：

- `UnLua` 在这一层不是 `没有扩展入口`；相反，它的 `PackagePath + CustomLoadLuaFile` 非常灵活，尤其适合下载目录、运行时 patch 和项目侧自定义 loader。
- `Angelscript` 的强项则不是“更能改路径”，而是把 root discovery 做成了 `实现方式不同` 的显式 contract：API、命令行入口、排序规则和测试都在仓内。
- 如果比较“工具/agent 能否不读 FAQ、直接通过仓内稳定接口枚举全部脚本入口”，相对 `Angelscript` 当前快照，`UnLua` 属于 `没有实现同层 machine-readable entry point`。
- 对 `Angelscript` 来说，这个差异值得吸收的不是 `PackagePath` 这类字符串入口本身，而是两层经验要分开看：需要临时 patch/下载脚本时可以借鉴 `UnLua` 的可变 loader seam；需要给工具和 agent 降低入口发现成本时，应继续坚持显式 roots API / commandlet 这条路。

---

## 深化分析 (2026-04-09 06:59:02)

### [维度 D3] Blueprint 覆写入口的 authoring contract：`UnLua` 把 module path 写进 Blueprint 资产图，`Angelscript` 把 override 合法性写进 class generator

这一轮补的不是“都能不能覆写 Blueprint”，而是**作者在编辑期到底改哪里，错误又会在什么时候被拦下**。`UnLua` 的编辑器入口是直接改 Blueprint 资产：点击工具栏“绑定”后，先给目标 Blueprint 实现 `UUnLuaInterface`，再把推导出的 `LuaModuleName` 直接写进 `GetModuleName` 图节点默认值；后续生成模板文件时，又从同一个 `GetModuleName` 回读路径。这意味着 `GetModuleName` 既是运行时绑定入口，也是编辑器工作流的单一事实来源。

`Angelscript` 的落点则不是资产图，而是脚本声明和类生成器。`BlueprintOverride` 一旦写进脚本类，`AngelscriptClassGenerator` 就会在生成 `UFunction/UClass` 之前检查父类是否真的存在该事件、父类是否声明为 `BlueprintEvent`、签名是否匹配、`const` 与 `editor-only` 是否一致；任何一项不满足都会直接 `ScriptCompileError`，不会把一个“看起来绑定好了、实际语义不对”的覆写入口发布出去。

```
[D3-Deep] Blueprint Authoring Contract
UnLua
├─ Blueprint asset                                 // 资产本身保存脚本入口
├─ Toolbar.BindToLua()                             // 编辑器给资产加 UnLuaInterface
├─ Write GetModuleName default pin                 // ★ module path 写进图节点默认值
├─ CreateLuaTemplate() reads GetModuleName         // ★ 模板生成复用同一入口
└─ Runtime BindClass() patches UFunction slots     // 运行时再把事件槽位切到 Lua

Angelscript
├─ .as declarations                                // 覆写入口写在脚本声明里
├─ ClassGenerator validates BlueprintOverride      // ★ 先校验父类/签名/const/editor-only
├─ ScriptCompileError on mismatch                  // 语义不合法直接拒绝发布
└─ Generated UClass/UFunction become Blueprint API // 通过类生成结果进入 Blueprint 继承链
```

关键源码 [1]：`UnLua` 的编辑器把 `GetModuleName` 当作资产级 source-of-truth

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp
// 函数: FUnLuaEditorToolbar::BindToLua_Executed / CreateLuaTemplate_Executed
// 行号: 138-199, 257-310
// 位置: 编辑器直接改 Blueprint 资产图，再从同一个 GetModuleName 回读模板路径
// ============================================================================
if (TargetClass->ImplementsInterface(UUnLuaInterface::StaticClass()))
    return;

const auto Ok = FBlueprintEditorUtils::ImplementNewInterface(
    Blueprint, FTopLevelAssetPath(UUnLuaInterface::StaticClass()));       // ★ 先把接口挂进资产
if (!Ok)
    return;

if (bIsAltDown)
{
    const auto Package = Blueprint->GetTypedOuter(UPackage::StaticClass());
    LuaModuleName = Package->GetName().RightChop(6).Replace(TEXT("/"), TEXT("."));
}
else
{
    const auto ModuleLocator = Cast<ULuaModuleLocator>(Settings->ModuleLocatorClass->GetDefaultObject());
    LuaModuleName = ModuleLocator->Locate(TargetClass);                   // ★ 也可以由 locator 推导
}

if (!LuaModuleName.IsEmpty())
{
    const auto InterfaceDesc = *Blueprint->ImplementedInterfaces.FindByPredicate([](const FBPInterfaceDescription& Desc)
    {
        return Desc.Interface == UUnLuaInterface::StaticClass();
    });
    InterfaceDesc.Graphs[0]->Nodes[1]->Pins[1]->DefaultValue = LuaModuleName; // ★ module path 写进 GetModuleName 图节点
}

FString ModuleName;
Class->GetDefaultObject()->ProcessEvent(Func, &ModuleName);              // ★ 后续模板生成继续从 GetModuleName 回读
const auto RelativePath = ModuleName.Replace(TEXT("."), TEXT("/"));
const auto FileName = FString::Printf(TEXT("%s%s.lua"), *GLuaSrcFullPath, *RelativePath);
FFileHelper::SaveStringToFile(Content, *FileName, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
```

关键源码 [2]：`Angelscript` 把 override 的正确性前移到类生成阶段

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 函数: FAngelscriptClassGenerator::FinalizeModuleClasses 中 BlueprintOverride 校验逻辑
// 行号: 732-785, 968-1013
// 位置: 生成 UFunction/UClass 前先校验覆写目标、签名与限定符
// ============================================================================
if (FunctionDesc->bBlueprintOverride)
{
    auto* ParentFunction = GetBlueprintEventByScriptName(CodeSuperClass, FunctionDesc->FunctionName);

    if (ParentFunction == nullptr)
    {
        ...
        if (!SuperFunctionDesc.IsValid())
        {
            FAngelscriptEngine::Get().ScriptCompileError(...,
                TEXT("BlueprintOverride method %s in class %s does not exist in superclass %s."));
            ClassData.ReloadReq = EReloadRequirement::Error;              // ★ 父类里没有就直接报错
        }
        else if (!SuperFunctionDesc->bBlueprintEvent && !SuperFunctionDesc->bBlueprintOverride)
        {
            FAngelscriptEngine::Get().ScriptCompileError(...,
                TEXT("... is not marked BlueprintEvent in superclass ..."));
            ClassData.ReloadReq = EReloadRequirement::Error;              // ★ 父类不是 BlueprintEvent 也不能覆写
        }
        else if (!SuperFunctionDesc->SignatureMatches(FunctionDesc))
        {
            FAngelscriptEngine::Get().ScriptCompileError(...,
                TEXT("... does not match signature of event declared in superclass ..."));
            ClassData.ReloadReq = EReloadRequirement::Error;              // ★ script 父类签名不一致
        }
    }

    if (bTypeMismatch || bArgCountMismatch)
    {
        FAngelscriptEngine::Get().ScriptCompileError(...,
            TEXT("BlueprintOverride method %s in class %s does not match function signature of event in superclass %s.\nExpected Signature: %s"));
        ClassData.ReloadReq = EReloadRequirement::Error;                  // ★ C++/UE 事件签名不一致
    }

    if (ParentFunction->HasAnyFunctionFlags(FUNC_Const) && !ScriptFunction->IsReadOnly())
    {
        FAngelscriptEngine::Get().ScriptCompileError(...,
            TEXT("... is not const, but is overriding a const method ..."));
        ClassData.ReloadReq = EReloadRequirement::Error;                  // ★ const 约束也前移到编译期
    }
}
```

新增对比结论：

- `UnLua` 的强项在于 **资产侧 authoring 体验**：module path、模板生成和文件定位都围绕同一个 `GetModuleName` 展开，蓝图作者几乎不需要理解运行时反射桥细节。
- `Angelscript` 的强项在于 **语义前置校验**：覆写是否成立、签名是否一致、`const/editor-only` 是否匹配，都在 class generator 阶段被拒绝或放行。
- 如果比较“是否把 Blueprint 脚本入口直接固化在资产图里并提供一键模板生成”，相对 `UnLua` 当前快照，`Angelscript` 属于 `没有实现同层 asset-centric scaffold`。
- 如果比较“是否在脚本类发布前就严格拒绝非法 `BlueprintOverride`”，相对 `Angelscript` 当前快照，`UnLua` 这条编辑器绑定链属于 `没有实现同层 compile-time override validation`。
- 这不是简单高下。`UnLua` 更适合 Blueprint-first 的内容工作流；`Angelscript` 更适合把脚本类当成正式类型系统成员来维护。

### [维度 D5] 调试能力的启用门槛：`UnLua` 先过配置宏与外部调试器两道门，`Angelscript` 先开 listener 再按需打开 line callback

前面已经把“谁有仓内协议栈、谁依赖外部 Lua 调试器”说清了。这一轮补的是**调试能力默认暴露到哪一层**。`UnLua` 的 `bEnableDebug` 先在 `UnLuaEditorSettings` 里默认为 `false`，再由 `UnLua.Build.cs` 折叠成 `UNLUA_ENABLE_DEBUG` 宏；文档侧又要求用户自己放入 `LuaPanda.lua` 并在脚本里显式 `require(...).start(...)`。也就是说，`UnLua` 当前快照把“调试 session 是否存在”交给了项目配置和用户脚本。

`Angelscript` 则把 `DebugServer` 作为 non-shipping / non-test build 的运行时部件：`WITH_AS_DEBUGSERVER` 在 `AngelscriptEngine.h` 直接跟随 build flavor 打开，engine 初始化时满足条件就构造 `FAngelscriptDebugServer` 并监听端口。但这也不等于它把所有单步/行钩子成本永久打开；`UpdateLineCallbackState()` 只有在 session 真正开始、存在 `DataBreakpoints` 或请求下一行中断时，才把 line callback 保持在持续活跃状态。

```
[D5-Deep] Debug Activation Budget
UnLua
├─ UUnLuaEditorSettings.bEnableDebug = false       // 默认先关
├─ UnLua.Build.cs -> UNLUA_ENABLE_DEBUG            // 编译宏决定是否带调试例程
├─ Docs: place LuaPanda.lua + require().start()    // 外部调试器和启动脚本由用户负责
└─ Runtime only exposes hooks/logs when enabled    // 默认不自带常驻 session host

Angelscript
├─ WITH_AS_DEBUGSERVER = !TEST && !SHIPPING        // build flavor 决定是否编进 runtime
├─ Engine init -> new FAngelscriptDebugServer      // 满足条件就先开 listener
├─ TCP listener accepts debugger clients           // session host 常驻在 runtime
└─ UpdateLineCallbackState() is demand-driven      // 只有调试中/数据断点时才持续跑 line callback
```

关键源码 [1]：`UnLua` 先把 debug 变成配置宏，再要求项目脚本显式 attach

```csharp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorSettings.h
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/UnLua.Build.cs
// 行号: 53-60, 68-111
// 位置: debug 是否编进 runtime 先受 editor config 控制
// ============================================================================
UPROPERTY(config, EditAnywhere, Category = "Build")
bool bEnableDebug = false;                                                // ★ 默认关闭

loadBoolConfig("bEnableDebug", "UNLUA_ENABLE_DEBUG", false);              // ★ 读 ini 后折成编译宏
loadBoolConfig("bEnableUnrealInsights", "ENABLE_UNREAL_INSIGHTS", false);

string hotReloadMode;
if (!config.GetString(section, "HotReloadMode", out hotReloadMode))
    hotReloadMode = "Manual";
PublicDefinitions.Add("UNLUA_WITH_HOT_RELOAD=" + (hotReloadMode != "Never" ? "1" : "0"));
```

```md
<!-- =========================================================================
文件: Reference/UnLua/Docs/CN/Debugging.md
行号: 1-15
位置: 仓内文档明确要求外部 Lua 调试器脚本和显式启动语句
=========================================================================== -->
1. 从VSCode应用市场安装 LuaPanda / LuaHelper
2. 从 LuaPanda 官方仓库获取 `LuaPanda.lua`，放入 `{UE工程}/Content/Script` 目录
3. 在Lua代码中加入 `require("LuaPanda").start("127.0.0.1",8818)`

注：调试器依赖 `luasocket`，UnLua 已通过扩展插件集成
```

关键源码 [2]：`Angelscript` 把 `DebugServer` 做成 runtime 常驻部件，但把执行期开销做成按需打开

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 行号: 16-20, 78, 1452-1455, 402-408, 5434-5442
// 位置: debug server 由 build flavor 和 engine 启动条件决定，line callback 再按需激活
// ============================================================================
#define WITH_AS_DEBUGSERVER (!UE_BUILD_TEST && !UE_BUILD_SHIPPING)         // ★ 非 test/shipping 默认编进来
int32 DebugServerPort = 27099;

#if WITH_AS_DEBUGSERVER
if ((!bUsePrecompiledData || bScriptDevelopmentMode) && FApp::HasProjectName())
{
    DebugServer = new FAngelscriptDebugServer(this, RuntimeConfig.DebugServerPort); // ★ engine 初始化就起 listener
}
#endif

FAngelscriptDebugServer::FAngelscriptDebugServer(FAngelscriptEngine* InOwnerEngine, int Port)
{
    Listener = new FTcpListener(FIPv4Endpoint(FIPv4Address::Any, Port));
    Listener->OnConnectionAccepted().BindRaw(this, &FAngelscriptDebugServer::HandleConnectionAccepted);
    UE_LOG(Angelscript, Log, TEXT("Angelscript debug server listening on %s"), *Listener->GetLocalEndpoint().ToText().ToString());
}

if (DebugServer != nullptr)
{
    if (DebugServer->bIsDebugging)
        bEverRunLineCallback = true;
    if (DebugServer->DataBreakpoints.Num() != 0)
        bEverRunLineCallback = true;
    if (DebugServer->bBreakNextScriptLine)
        bAlwaysRunLineCallback = true;                                     // ★ 只有真正调试中才持续保持行回调
}
```

新增对比结论：

- `UnLua` 当前快照不是 `没有调试能力`；它明确保留了接入外部 Lua 调试器的 seam，只是默认不会替用户把 session host 拉起来。
- `Angelscript` 当前快照也不是“粗暴地永远高成本调试”；它是 `listener 常驻 + line callback 按需` 的两段式设计。
- 如果比较“仓内是否存在默认可连接的 debug session host”，相对 `Angelscript` 当前快照，`UnLua` 属于 `没有实现`。
- 如果比较“是否默认把调试例程和 attach 责任交给项目配置/用户脚本”，两者属于 `实现方式不同`；`UnLua` 更保守，`Angelscript` 更像把调试当成平台能力。
- 对 `Angelscript` 来说，可借鉴的不应是把 `DebugServer` 拆掉，而是像 `UnLua` 一样继续明确区分“transport 常驻”与“高频 hook 何时开启”的成本边界。

### [维度 D10] 脚本路径的单一事实来源：`UnLua` 用 `GetModuleName` 串起资产工作流，`Angelscript` 用 `ChangedScript`/`CodeSection` 串起工具工作流

这一轮补的不是“教程多不多”，而是**仓库到底把脚本路径当成人类作者的事实来源，还是当成工具链的事实来源**。`UnLua` 的答案非常集中：`GetModuleName`。`Quickstart` 明确告诉用户“绑定”时会填好路径、以后改路径就双击 `GetModuleName`；编辑器代码里，生成模板、在文件管理器中显示、复制相对路径，也都先从这个函数回读。这是典型的 asset-centric contract。

`Angelscript` 的答案则偏 tooling-centric。`BlueprintImpact` 先把 `ChangedScripts` 做规范化，再拿它去匹配每个 module 的 `CodeSection.RelativeFilename`；`BlueprintImpactScanCommandlet` 进一步把 `-ChangedScript=` / `-ChangedScriptFile=` 变成命令行 contract，并输出 JSON 风格统计。这里的脚本路径首先服务于 repo 级分析、CI 和批处理，而不是服务于某个单独 Blueprint 资产上的“绑定入口”。

```
[D10-Deep] Script Path Source Of Truth
UnLua
├─ Quickstart tells user: edit GetModuleName        // 文档直接教用户改这个函数
├─ Toolbar.BindToLua writes GetModuleName pin       // 绑定动作写入同一入口
├─ CreateLuaTemplate reads GetModuleName            // 模板生成依赖同一路径
├─ RevealInExplorer / CopyAsRelativePath            // 文件定位和复制也复用它
└─ Source of truth = Blueprint asset-local function // 以资产为中心

Angelscript
├─ Commandlet accepts ChangedScript / ChangedScriptFile // 工具从文件路径输入开始
├─ NormalizeChangedScriptPaths()                    // 先去重、归一化、排序
├─ FindModulesForChangedScripts()                   // 再匹配 Module->CodeSection.RelativeFilename
├─ ScanBlueprintAssets() -> JSON summary            // 输出给 CI / automation / agent
└─ Source of truth = script file paths + code sections // 以仓库/工具链为中心
```

关键源码 [1]：`UnLua` 把 `GetModuleName` 做成教程与编辑器动作的共同入口

```md
<!-- =========================================================================
文件: Reference/UnLua/Docs/CN/Quickstart_For_UE_Newbie.md
文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp
行号: 11-30, 265-376
位置: 文档和编辑器都围绕 GetModuleName 工作；它既是用户入口，也是文件入口
=========================================================================== -->
## 2. 绑定到Lua
- 点击UnLua菜单栏中的“绑定”，默认会自动根据蓝图路径填充好Lua的模块路径
- 如果下次需要修改绑定的路径，可以找到 `GetModuleName` 函数并双击进行修改

## 3. 生成Lua模版代码
点击“生成Lua模板文件”，会在工程 `Content/Script` 目录下生成

const auto Func = Class->FindFunctionByName(FName("GetModuleName"));
Class->GetDefaultObject()->ProcessEvent(Func, &ModuleName);               // ★ 模板生成从 GetModuleName 回读
const auto RelativePath = ModuleName.Replace(TEXT("."), TEXT("/"));
const auto FileName = FString::Printf(TEXT("%s%s.lua"), *GLuaSrcFullPath, *RelativePath);

const auto DefaultObject = TargetClass->GetDefaultObject();
DefaultObject->UObject::ProcessEvent(Func, &ModuleName);                  // ★ Reveal/Copy 也先读同一个函数
const auto RelativePath = ModuleName.Replace(TEXT("."), TEXT("/")) + TEXT(".lua");
FPlatformApplicationMisc::ClipboardCopy(*RelativePath);
```

关键源码 [2]：`Angelscript` 把脚本路径 contract 暴露给 impact analysis 与 commandlet

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp
// 行号: 71-109, 63-90
// 位置: script path 先被规范化，再匹配 module code section，最后导出命令行结果
// ============================================================================
TArray<FString> NormalizeChangedScriptPaths(const TArray<FString>& ChangedScripts)
{
    TSet<FString> UniquePaths;
    for (const FString& ChangedScript : ChangedScripts)
    {
        const FString Normalized = NormalizeScriptPath(ChangedScript);
        if (!Normalized.IsEmpty())
            UniquePaths.Add(Normalized);                                  // ★ 先做统一规范化
    }
    TArray<FString> Result = UniquePaths.Array();
    Result.Sort();
    return Result;
}

for (const TSharedRef<FAngelscriptModuleDesc>& Module : Modules)
{
    for (const FAngelscriptModuleDesc::FCodeSection& CodeSection : Module->Code)
    {
        if (NormalizedChangedScripts.Contains(NormalizeScriptPath(CodeSection.RelativeFilename)))
        {
            MatchingModules.Add(Module);                                  // ★ path -> module 的匹配基于代码分段
            break;
        }
    }
}

if (FParse::Value(*Params, TEXT("ChangedScript="), ChangedScriptsValue))
    AppendChangedScriptsFromDelimitedValue(ChangedScriptsValue, Request.ChangedScripts);
if (FParse::Value(*Params, TEXT("ChangedScriptFile="), ChangedScriptsFile))
    TryReadChangedScriptsFile(ChangedScriptsFile, Request.ChangedScripts);

UE_LOG(Angelscript, Display,
    TEXT("{ \"BlueprintImpact\": { \"FullScan\": %s, \"ChangedScripts\": %d, \"MatchingModules\": %d, ... } }"),
    Request.IsFullScan() ? TEXT("true") : TEXT("false"),
    ScanResult.NormalizedChangedScripts.Num(),
    ScanResult.MatchingModules.Num());                                    // ★ 输出给工具/CI/agent 消费
```

新增对比结论：

- `UnLua` 在这一层的优势不是“路径系统更强”，而是把路径入口压缩成 Blueprint 作者能直接编辑的一个函数，教程、模板和文件定位都围绕它展开。
- `Angelscript` 的优势则不是“更适合单个资产绑定”，而是把路径 contract 做成工具可消费的输入输出：`ChangedScript(s)` 作为输入，`MatchingModules/Matches` 作为输出。
- 如果比较“单个 Blueprint 资产能否直接承载并回显它自己的脚本路径入口”，相对 `UnLua` 当前快照，`Angelscript` 属于 `没有实现同层 asset-local source-of-truth`。
- 如果比较“能否从命令行/CI 直接拿一组 changed script path 做 Blueprint 影响分析”，相对 `Angelscript` 当前快照，`UnLua` 属于 `没有实现同层 machine-oriented workflow contract`。
- 这两条经验都值得分开吸收：`UnLua` 值得借鉴的是**作者入口统一**，`Angelscript` 现有路线值得继续加强的是**工具入口稳定**。

---

## 深化分析 (2026-04-09 07:08:46)

### [维度 D1] 卫星扩展插件的 ownership：`UnLua` 把增量能力做成 `FLuaEnv` 卫星，`Angelscript` 把增量能力编进 `bind surface`

前文主要把 `UnLua` 讲成“runtime + editor + tests”。这一轮补的是**额外能力到底长在 core 里，还是长在 core 外**。当前快照里的 `UnLua` 不只是一个插件，而是一套以 `UnLua` 为核心、以 `UnLuaExtensions/*` 为卫星的交付拓扑：core plugin 只声明 `UnLua / UnLuaEditor / UnLuaDefaultParamCollector` 三个模块，而 `LuaSocket` 这类能力以独立 `uplugin` 形式依赖 `UnLua`，再通过 `FLuaEnv::OnCreated` 在每个 Lua VM 创建时补 loader 与 `PackagePath`。

`Angelscript` 的路线不同。它当然也能继续长能力，但多数增量面直接落在 `AngelscriptRuntime` 内部：`Bind_FunctionLibraryMixins.cpp`、`Bind_Json.cpp` 这种文件直接把 mixin helper、`Json` namespace 等 API 编进 runtime bind pass，而不是拆成独立卫星插件再在 engine 启动后订阅一个 `OnCreated` 缝。也就是说，两边都能扩展，但**UnLua 先把“能力载体”做成插件边界，Angelscript 先把“能力表面”做成脚本 API 边界**。

```
[D1-Deep] Capability Carrier Topology
UnLua
├─ UnLua.uplugin                                  // core plugin
│  ├─ UnLua Runtime
│  ├─ UnLuaEditor
│  └─ UnLuaDefaultParamCollector
├─ LuaSocket.uplugin -> depends on UnLua          // 卫星插件
│  └─ OnCreated -> AddBuiltInLoader + PackagePath
└─ Other UnLuaExtensions/*                        // 同类扩展继续外挂

Angelscript
├─ Angelscript.uplugin                            // 单一交付插件
│  ├─ AngelscriptRuntime
│  ├─ AngelscriptEditor
│  └─ AngelscriptTest
└─ Bind_*.cpp / FunctionLibraries/*               // 增量能力直接编进 runtime bind surface
```

关键源码 [1]：`UnLua` 的扩展能力首先表现为独立插件，再接到 `FLuaEnv` 生命周期上

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/UnLua.uplugin
// 文件: Reference/UnLua/Plugins/UnLuaExtensions/LuaSocket/LuaSocket.uplugin
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/LuaEnv.h
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp
// 文件: Reference/UnLua/Plugins/UnLuaExtensions/LuaSocket/Source/Private/LuaSocketModule.cpp
// 行号: 23-40, 16-29, 43-50, 161-164, 20-37
// 位置: core 只提供 env 生命周期缝；LuaSocket 作为卫星插件挂进来
// ============================================================================
// UnLua.uplugin
"Modules": [
    { "Name": "UnLua", "Type": "Runtime", "LoadingPhase": "PreDefault" },
    { "Name": "UnLuaEditor", "Type": "Editor", "LoadingPhase": "Default" },
    { "Name": "UnLuaDefaultParamCollector", "Type": "Program", "LoadingPhase": "PostConfigInit" }
]

// LuaSocket.uplugin
"Modules": [
    { "Name": "LuaSocket", "Type": "Runtime", "LoadingPhase": "PreLoadingScreen" }
],
"Plugins": [
    { "Name": "UnLua", "Enabled": true }                              // ★ 卫星插件显式依赖 core
]

// LuaEnv.h / LuaEnv.cpp
DECLARE_MULTICAST_DELEGATE_OneParam(FOnCreated, FLuaEnv&);
static FOnCreated OnCreated;
...
UnLuaLib::Open(L);
OnCreated.Broadcast(*this);                                            // ★ 每个 Env 创建后广播扩展缝

// LuaSocketModule.cpp
void FLuaSocketModule::StartupModule()
{
    UnLua::FLuaEnv::OnCreated.AddStatic(&FLuaSocketModule::OnLuaEnvCreated);
}

void FLuaSocketModule::OnLuaEnvCreated(UnLua::FLuaEnv& Env)
{
    Env.AddBuiltInLoader(TEXT("socket"), luaopen_socket_core);         // ★ 注入 native loader
    Env.AddBuiltInLoader(TEXT("socket.core"), luaopen_socket_core);
    Env.AddBuiltInLoader(TEXT("mime.core"), luaopen_mime_core);
    Env.DoString("UnLua.PackagePath = UnLua.PackagePath .. ';/Plugins/UnLuaExtensions/LuaSocket/Content/Script/?.lua'");
}
```

关键源码 [2]：`Angelscript` 的增量能力更多直接变成 runtime 内的 bind 表面

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Angelscript.uplugin
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FunctionLibraryMixins.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Json.cpp
// 行号: 18-48, 7-31, 762-775
// 位置: 交付物是单一插件；helper API 直接注册进 runtime bind pass
// ============================================================================
// Angelscript.uplugin
"Modules": [
    { "Name": "AngelscriptRuntime", "Type": "Runtime", "LoadingPhase": "PostDefault" },
    { "Name": "AngelscriptEditor", "Type": "Editor", "LoadingPhase": "PostDefault" },
    { "Name": "AngelscriptTest", "Type": "Editor", "LoadingPhase": "PostDefault" }
]

// Bind_FunctionLibraryMixins.cpp
AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_FunctionLibraryMixins(..., []
{
    auto RuntimeCurveLinearColor_ = FAngelscriptBinds::ExistingClass("FRuntimeCurveLinearColor");
    RuntimeCurveLinearColor_.Method("void AddDefaultKey(...)", ...);    // ★ helper 能力直接落进现有类

    FAngelscriptBinds::FNamespace RuntimeCurveLinearColorHelperNs("URuntimeCurveLinearColorMixinLibrary");
    FAngelscriptBinds::BindGlobalFunction("void AddDefaultKey(...)", ...);
});

// Bind_Json.cpp
{
    FAngelscriptBinds::FNamespace ns("Json");                           // ★ 非反射工具命名空间也直接在 runtime 暴露
    FAngelscriptBinds::BindGlobalFunction("FString ValueTypeToString(EJsonType T)", &ValueTypeToString);
    FAngelscriptBinds::BindGlobalFunction("FJsonObject ParseString(const FString& JsonStr)", ...);
}
```

新增对比结论：

- `UnLua` 的一个新增认知点不是“插件多”，而是**扩展能力拥有独立交付边界**；`LuaSocket` 不是 `UnLua` 里的一个 feature flag，而是一个可以单独启停、单独依赖的卫星插件。
- `Angelscript` 并不是 `没有扩展能力`；它当前快照更多采用 `实现方式不同`：通过 `Bind_*.cpp`、`FunctionLibraries/*`、runtime namespace 继续长 API，而不是先长插件边界。
- 如果比较“是否已经形成像 `UnLuaExtensions/*` 这样公开、稳定的卫星扩展约定”，相对 `UnLua` 当前快照，`Angelscript` 属于 `没有实现同层 delivery convention`。
- 如果比较“是否能继续暴露非反射 helper / 第三方能力到脚本面”，两者属于 `实现方式不同`，不是简单的有无差距。

### [维度 D4] 扩展脚本如何进入自动热重载范围：`UnLua` 的 `PackagePath` 比 watcher 更宽，`Angelscript` 用统一 root discovery 驱动 watcher / commandlet

这一轮最值得记住的新发现不在 `HotReload.lua` 本身，而在**“哪些路径会被自动观察”与“哪些路径会被 reload 算法解析”是否一致**。`UnLua` 的卫星扩展会在 `OnCreated` 时把 `;/Plugins/UnLuaExtensions/.../Content/Script/?.lua` 追加进 `UnLua.PackagePath`；`HotReload.lua` 在 `M.reload()` 中按 `loaded_module_times -> get_last_modified_time()` 重新探测模块时间戳，而底层文件解析又会遍历 `PackagePath`。这说明**手动触发 reload 时，extension script 是可见的**。

但 editor 自动模式另一头并不对称。`WatchScriptDirectory()` 只注册 `UUnLuaFunctionLibrary::GetScriptRootPath()`，而这个函数硬编码返回 `Project/Content/Script/`；与此同时，`SetupPackagingSettings()` 又会枚举启用插件中 `ContentDir` 包含 `UnLuaExtensions` 的目录，把这些卫星脚本加到 `DirectoriesToAlwaysStageAsUFS`。由这些源码可推断：**UnLua 当前快照的“打包可见路径集合”与“auto watcher 路径集合”并不完全一致**。卫星扩展脚本能被打包、能被 `PackagePath` 解析，但未见同层的 editor 自动文件监听覆盖。

`Angelscript` 在这一点上更统一。`DiscoverScriptRoots()` 先收集 project `Script/`，再泛化收集所有 `GetEnabledPluginsWithContent()` 的 `PluginBaseDir/Script`，排序后形成同一个 root set；`AngelscriptEditorModule` 的 watcher 直接遍历 `MakeAllScriptRoots()` 注册目录监听，`AngelscriptAllScriptRootsCommandlet` 也复用同一入口输出 JSON。也就是说，**自动监听、机器查询、script root discoverability 都来自同一个 source-of-truth**。

```
[D4-Deep] Root Coverage Consistency
UnLua
├─ OnCreated appends PackagePath for extension scripts // reload resolver 看得见
├─ HotReload.lua scans loaded_module_times             // 手动 reload 可重查这些模块
├─ WatchScriptDirectory -> Project/Content/Script only // auto watcher 只盯单一路径
└─ SetupPackagingSettings stages UnLuaExtensions/*     // staged paths 又比 watcher 更宽

Angelscript
├─ DiscoverScriptRoots() -> project + plugin Script dirs
├─ MakeAllScriptRoots() reused by DirectoryWatcher
├─ AllScriptRootsCommandlet reuses same root set
└─ watcher / tooling / machine output share one source-of-truth
```

关键源码 [1]：`UnLua` 的 extension script 进入 `PackagePath`，但 auto watcher 只看 project `Script/`

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorFunctionLibrary.cpp
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaFunctionLibrary.cpp
// 行号: 186-205, 27-36, 20-23
// 位置: staged path 会枚举 UnLuaExtensions/*，但 watcher 只注册 project Script 根
// ============================================================================
// UnLuaEditorModule.cpp
auto ScriptPaths = TArray<FString>{TEXT("Script"), TEXT("../Plugins/UnLua/Content/Script")};
const auto Plugins = IPluginManager::Get().GetEnabledPlugins();
for (const auto Plugin : Plugins)
{
    if (!Plugin->CanContainContent())
        continue;
    const auto ContentDir = Plugin->GetContentDir();
    if (!ContentDir.Contains("UnLuaExtensions"))
        continue;                                                       // ★ 通过目录约定识别卫星扩展
    auto ScriptPath = ContentDir / "Script";
    if (FPaths::DirectoryExists(ScriptPath))
        ScriptPaths.Add(ScriptPath);                                    // ★ 打包时会把这些脚本带上
}

// UnLuaEditorFunctionLibrary.cpp
const auto& ScriptRootPath = UUnLuaFunctionLibrary::GetScriptRootPath();
DirectoryWatcher->RegisterDirectoryChangedCallback_Handle(ScriptRootPath, Delegate, DirectoryWatcherHandle);

// UnLuaFunctionLibrary.cpp
FString UUnLuaFunctionLibrary::GetScriptRootPath()
{
    return FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() + TEXT("Script/")); // ★ watcher 只盯 project root
}
```

关键源码 [2]：`UnLua` 的手动 reload 仍然可以通过 `PackagePath` 看到更宽的脚本集合

```lua
-- ============================================================================
-- 文件: Reference/UnLua/Plugins/UnLuaExtensions/LuaSocket/Source/Private/LuaSocketModule.cpp
-- 文件: Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua
-- 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp
-- 行号: 30-37, 604-624, 614-639
-- 位置: extension 先扩展 PackagePath；HotReload 再按 module->timestamp 和 PackagePath 解析文件
-- ============================================================================
// LuaSocketModule.cpp
void FLuaSocketModule::OnLuaEnvCreated(UnLua::FLuaEnv& Env)
{
    Env.DoString("UnLua.PackagePath = UnLua.PackagePath .. ';/Plugins/UnLuaExtensions/LuaSocket/Content/Script/?.lua'");
}

-- HotReload.lua
function M.reload(module_names)
    ...
    for module_name, time in pairs(loaded_module_times) do
        if not ignore_modules[module_name] then
            local current_time = get_last_modified_time(module_name)    -- ★ 这里会继续按 module 名找真实文件
            if current_time ~= time then
                modified_modules[#modified_modules + 1] = module_name
            end
        end
    end
end

// LuaEnv.cpp
const auto PackagePath = UnLuaLib::GetPackagePath(L);
if (PackagePath.ParseIntoArray(Patterns, TEXT(";"), false) == 0)
    return 0;
for (auto& Pattern : Patterns)
{
    const auto PathWithProjectDir = FPaths::Combine(FPaths::ProjectDir(), Pattern);
    FullPath = FPaths::ConvertRelativePathToFull(PathWithProjectDir);   // ★ 文件解析会遍历整条 PackagePath
    if (FFileHelper::LoadFileToArray(Data, *FullPath, FILEREAD_Silent))
        return LoadIt();
}
```

关键源码 [3]：`Angelscript` 把 project/plugin script root 做成统一 discoverability，并让 watcher / commandlet 共用

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAllScriptRootsCommandlet.cpp
// 行号: 558-566, 1326-1363, 367-381, 5-19
// 位置: 同一个 root discovery 同时服务 watcher、tooling 和 machine-readable 输出
// ============================================================================
Dependencies.GetEnabledPluginScriptRoots = []()
{
    TArray<FString> ScriptRoots;
    for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetEnabledPluginsWithContent())
        ScriptRoots.Add(Plugin->GetBaseDir() / TEXT("Script"));         // ★ 泛化收集所有启用插件 Script 根
    return ScriptRoots;
};

TArray<FString> FAngelscriptEngine::DiscoverScriptRoots(bool bOnlyProjectRoot) const
{
    FString RootPath = Dependencies.ConvertRelativePathToFull(Dependencies.GetProjectDir() / TEXT("Script"));
    ...
    for (const FString& PluginScriptRoot : Dependencies.GetEnabledPluginScriptRoots())
    {
        const FString ScriptPath = Dependencies.ConvertRelativePathToFull(PluginScriptRoot);
        if (Dependencies.DirectoryExists(ScriptPath) && ScriptPath != RootPath)
            DiscoveredRootPaths.Add(ScriptPath);
    }
    DiscoveredRootPaths.Sort();
    DiscoveredRootPaths.Insert(RootPath, 0);                            // ★ root set 明确、稳定、可复用
    return DiscoveredRootPaths;
}

TArray<FString> AllRootPaths = FAngelscriptEngine::MakeAllScriptRoots();
for (const auto& RootPath : AllRootPaths)
{
    DirectoryWatcher->RegisterDirectoryChangedCallback_Handle(*RootPath, IDirectoryWatcher::FDirectoryChanged::CreateStatic(&OnScriptFileChanges), WatchHandle, IDirectoryWatcher::IncludeDirectoryChanges);
}

const auto AllScriptRoots = FAngelscriptEngine::MakeAllScriptRoots();
UE_LOG(Angelscript, Display, TEXT("{ \"AngelscriptScriptRoots\": %s}\n"), *Result); // ★ commandlet 也输出同一集合
```

新增对比结论：

- `UnLua` 并不是 `没有实现 plugin script reload`；从 `PackagePath` 和 `HotReload.lua` 的组合看，手动 reload 路径仍可覆盖卫星扩展脚本。
- 但如果比较“extension script 是否自动进入 editor 文件监听集合”，相对 `Angelscript` 当前快照，`UnLua` 属于 `没有实现同层 generic watcher coverage`。这是由 watcher 只注册 `Project/Content/Script/`、而打包与 `PackagePath` 又覆盖更宽路径这一组源码共同推断出的结果。
- `Angelscript` 在这里的优势不是“热重载算法一定更好”，而是 **root discovery 的 source-of-truth 更统一**：watcher、commandlet、machine-readable output 共用同一入口。
- 这条经验对 `Angelscript` 也有反向价值：如果未来真的引入类似 `UnLuaExtensions/*` 的卫星扩展，必须继续保持 `discover -> watch -> report -> package` 四条链共用同一 root set，避免出现 `UnLua` 这种“resolver 比 watcher 认识更多路径”的分裂。

### [维度 D9] 测试插件的 spillover：`UnLuaTestSuite` 是可拆卸 harness，但启用后会反向影响 UHT 选择；`Angelscript` 把测试 contract 收进仓库工具和 commandlet

前文已经反复比较过 case 组织、suite 颗粒度、runner contract。这一轮补的是**测试基础设施会不会反过来影响 build graph**。`UnLua` 的答案是“会，而且仓内 README 明说了”。`TPSProject.uproject` 默认启用了 `UnLuaTestSuite`；`UnLuaTestSuite.uplugin` 又把自己声明成 `CanBeUsedWithUnrealHeaderTool=true` 的 runtime plugin；`UnLuaDefaultParamCollectorUbtPlugin/README.md` 进一步明确写出，只要项目还包含 `UnLuaTestSuite`，引擎编译时就仍会因为它没有 C# 版本而退回 C++ UHT。也就是说，**UnLua 的测试 harness 虽然可拆卸，但启用后会真实改变 build-time codegen path**。

`Angelscript` 的当前快照则更像把测试 contract 收进 repo tooling。`Angelscript.uplugin` 直接携带 `AngelscriptTest` editor module；`Tools/RunTests.ps1` 固定输出 `Automation RunTests ... -ReportExportPath=...`，`Tools/RunTestSuite.ps1` 又把 `HotReload`、`Debugger`、`ScenarioSamples` 等 suite taxonomy 稳定化；`UAngelscriptTestCommandlet::Main()` 用 `1 / 2 / 3` 明确区分初次编译失败、单元测试失败、struct 成员未初始化；而 `AngelscriptEditorModule.cpp` 直接把 `AngelscriptUHTTool` 标成 primary path。这里的重心不是“把 test 做成可选插件”，而是**把 machine-consumable build/test contract 做成仓库自己的固定接口**。

```
[D9-Deep] Test Harness Spillover
UnLua
├─ TPSProject.uproject enables UnLuaTestSuite      // 样例工程直接带测试插件
├─ UnLuaTestSuite.uplugin -> CanBeUsedWithUHT      // 测试插件进入 UHT 参与面
└─ README warns: project still falls back to C++ UHT

Angelscript
├─ Angelscript.uplugin includes AngelscriptTest    // 测试模块随主插件交付
├─ RunTests.ps1 -> stable Automation CLI contract
├─ RunTestSuite.ps1 -> stable suite taxonomy
├─ AngelscriptTestCommandlet -> stable exit codes
└─ AngelscriptUHTTool remains an explicit primary pipeline
```

关键源码 [1]：`UnLuaTestSuite` 是可拆卸测试插件，但它会反向进入 UHT 参与面

```cpp
// ============================================================================
// 文件: Reference/UnLua/TPSProject.uproject
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/UnLuaTestSuite.uplugin
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaDefaultParamCollectorUbtPlugin/README.md
// 行号: 16-33, 16-29, 35-43
// 位置: 样例工程默认启用测试插件；README 明确记录了它对 UHT 路径的副作用
// ============================================================================
// TPSProject.uproject
"Plugins": [
    { "Name": "RuntimeTests", "Enabled": true },
    { "Name": "EditorTests", "Enabled": true },
    { "Name": "FunctionalTestingEditor", "Enabled": true },
    { "Name": "UnLuaTestSuite", "Enabled": true }                       // ★ sample project 默认把 test harness 带上
]

// UnLuaTestSuite.uplugin
"CanBeUsedWithUnrealHeaderTool": true,                                  // ★ 测试插件进入 UHT 参与面
"EnabledByDefault": false,
"Plugins": [
    { "Name": "UnLua", "Enabled": true }
],
"Modules": [
    { "Name": "UnLuaTestSuite", "Type": "Runtime", "LoadingPhase": "Default" }
]

// README.md
UnLuaTestSuite 也是一个可以被引擎 UHT 调用的插件...
DEPRECATED: C++ UHT being used because 'UnLuaTestSuite' does not have a C# version.
如果一个项目包含了 UnLuaTestSuite 插件，在编译时引擎就仍然会调用 C++ 版本的 UHT。 // ★ 副作用是仓内文档明确声明的
```

关键源码 [2]：`Angelscript` 把测试入口、suite taxonomy、退出码与 UHT primary path 分别做成稳定 contract

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Angelscript.uplugin
// 文件: Tools/RunTests.ps1
// 文件: Tools/RunTestSuite.ps1
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptTestCommandlet.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptUHTTool.ubtplugin.csproj
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 行号: 18-33, 83-97, 68-83, 5-24, 1-15, 726-730
// 位置: test contract、exit code 和 UHT primary path 都在仓库内显式定义
// ============================================================================
// Angelscript.uplugin
"Modules": [
    { "Name": "AngelscriptRuntime", "Type": "Runtime", "LoadingPhase": "PostDefault" },
    { "Name": "AngelscriptEditor", "Type": "Editor", "LoadingPhase": "PostDefault" },
    { "Name": "AngelscriptTest", "Type": "Editor", "LoadingPhase": "PostDefault" } // ★ 测试模块跟主插件一起交付
]

// RunTests.ps1
$argumentList = @(
    $agentConfig.ProjectFile,
    "-ExecCmds=Automation RunTests $target; Quit",                      // ★ 机器调用 contract 固定
    '-TestExit=Automation Test Queue Empty',
    "-ReportExportPath=$($outputLayout.ReportPath)"
)

// RunTestSuite.ps1
"HotReload" = @(
    @{ Prefix = "Angelscript.TestModule.HotReload"; Label = "HotReload" }
)
"Debugger" = @(
    @{ Prefix = "Angelscript.CppTests.Debug."; Label = "CppDebugger" },
    @{ Prefix = "Angelscript.TestModule.Debugger."; Label = "TestModuleDebugger" }
)                                                                        // ★ suite taxonomy 也是仓库定义的

// AngelscriptTestCommandlet.cpp
if (!FAngelscriptEngine::Get().bDidInitialCompileSucceed)
    return 1;
if (!RunAngelscriptUnitTests(...))
    return 2;
if (FStructUtils::AttemptToFindUninitializedScriptStructMembers() != 0)
    return 3;                                                            // ★ CI 可直接消费退出码

// AngelscriptUHTTool.ubtplugin.csproj
<AssemblyName>AngelscriptUHTTool</AssemblyName>
<OutputPath>..\..\Binaries\DotNET\UnrealBuildTool\Plugins\AngelscriptUHTTool\</OutputPath>

// AngelscriptEditorModule.cpp
NSLOCTEXT("Angelscript", "GenerateBind.ToolTip", "Legacy editor-side generator retained only for debugging old FunctionCallers output. The UHT-based AngelscriptUHTTool pipeline is the primary path.")
```

新增对比结论：

- `UnLua` 在这一层的优势是 **test harness 可拆卸**：测试可以作为独立插件挂进 sample project，而不是强绑定到主插件交付物。
- 代价也同样明确：当前快照里，只要 sample project 启用了 `UnLuaTestSuite`，测试插件就会对 UHT 选择产生副作用。这一点不是推测，而是仓内 README 直接说明的事实。
- `Angelscript` 的优势不是“测试更多”，而是 **machine contract 更仓库内建**：suite 名、Automation CLI、exit code、UHT primary path 都有固定落点。
- 如果比较“是否提供与主插件解耦的独立测试插件”，相对 `UnLua` 当前快照，`Angelscript` 属于 `没有实现同层 detachable harness`。
- 如果比较“测试基础设施是否会把 codegen path 拉回另一套 UHT 实现”，就当前快照与本轮检索到的证据看，`UnLua` 相对 `Angelscript` 体现出 `实现质量差异（build isolation）`。

---

## 深化分析 (2026-04-09 07:18:06)

### [维度 D2] 默认参数的 ownership：`UnLua` 把 native default 物化成运行时数据库，`Angelscript` 把 default 前移到声明与元数据

前文已经比较过两边的“参数桥”本身，这一轮补的是**默认参数到底在哪一层被兑现**。`UnLua` 因为 Lua 侧调用 native `UFunction` 不经过静态编译，所以它需要一份可以在调用期查询的 `DefaultParamCollection`；UHT 插件把 `CPP_Default_*` / metadata 解析成 `FParameterCollection`，运行时 `FFunctionDesc::PreCall()` 在“实参不足”时直接拷进参数缓冲。

`Angelscript` 的 ownership 更前移。对现有 UE `UFunction`，`FAngelscriptFunctionSignature::InitFromFunction()` 先把 `CPP_Default_*` 元数据读进 `ArgumentDefaults`，再把 default 写进脚本声明字符串；后续反射 fallback 的 generic thunk 只做 `CopySingleValue()`，它假定 AngelScript 编译期已经把缺省实参补成完整调用。反过来，对脚本声明出来的新 `UFUNCTION`，`ClassGenerator` 又把 Angelscript 默认值回写成 UE 侧 `CPP_Default_*` metadata。

```
[D2-Deep] Default Argument Ownership
UnLua
├─ UHT plugin scans metadata / CPP_Default_*        // 构建 DefaultParamCollection
├─ GDefaultParamCollection keyed by class+function  // 运行时可查询账本
└─ FFunctionDesc::PreCall() fills missing params    // 调用期补默认值

Angelscript
├─ Read CPP_Default_* into ArgumentDefaults         // 先从 UE 元数据取 default
├─ Build script declaration with defaults           // 编译期由 AngelScript 使用
├─ Reflective fallback copies provided args only    // thunk 不再二次查默认值
└─ Script-defined defaults -> CPP_Default_* meta    // 反向写回 UE metadata
```

关键源码 [1]：`UnLua` 先生成默认参数数据库，再在调用期补齐缺失实参

```csharp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaDefaultParamCollectorUbtPlugin/UnLuaDefaultParamCollectorUbtPlugin.cs
// 位置: 383-405
// 定位: UHT 导出阶段把函数默认值写进 DefaultParamCollection
// ============================================================================
private void PreAddProperty(UhtClass classObj, UhtFunction function)
{
    if (!bCurrentClassWritten)
    {
        bCurrentClassWritten = true;
        GeneratedContentBuilder.AppendFormat("FC = &GDefaultParamCollection.Add(TEXT(\"{0}\"));\r\n", classObj.EngineNamePrefix + classObj.EngineName);
    }
    if (!bCurrentFunctionWritten)
    {
        bCurrentFunctionWritten = true;
        GeneratedContentBuilder.AppendFormat("PC = &FC->Functions.Add(TEXT(\"{0}\"));\r\n", function.StrippedFunctionName);
    }
}

private static bool FindDefaultValueString(UhtMetaData metaData, UhtProperty property, out string value)
{
    var hasValue = metaData.TryGetValue(property.SourceName, out string? tempValue);
    if (!hasValue)
    {
        var cppKey = "CPP_Default_" + property.SourceName;              // ★ native 默认值来自 UE 元数据
        hasValue = metaData.TryGetValue(cppKey, out tempValue);
    }
    value = tempValue ?? string.Empty;
    return hasValue;
}
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp
// 位置: 321-333
// 定位: Lua 调用 native UFunction 时，缺失参数在 PreCall 阶段从默认值账本补齐
// ============================================================================
else if (!Property->IsOutParameter())
{
    if (DefaultParams)
    {
        // ★ 调用期查询 DefaultParamCollection，缺哪个参数就补哪个参数
        IParamValue **DefaultValue = DefaultParams->Parameters.Find(Property->GetProperty()->GetFName());
        if (DefaultValue)
        {
            const void *ValuePtr = (*DefaultValue)->GetValue();
            Property->CopyValue(Params, ValuePtr);
            CleanupFlags[i] = true;
        }
    }
}
```

关键源码 [2]：`Angelscript` 先把默认值前移到 declaration / metadata，runtime fallback 不再维护另一份 default 数据库

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h
// 位置: 204-214, 321-323
// 定位: 从 UE metadata 读取默认值，并把它们写进脚本声明
// ============================================================================
FString DefaultMeta = TEXT("CPP_Default_");
DefaultMeta += Property->GetName();

FName MetaName = *DefaultMeta;
if (Function->HasMetaData(MetaName))
{
    FString MetaStr = Function->GetMetaData(MetaName);
    if (MetaStr == TEXT("None"))
        MetaStr = TEXT("");
    ArgumentDefaults.Add(MetaStr);                                      // ★ 默认值直接进入脚本签名
}
else
{
    ArgumentDefaults.Add(TEXT("-"));
}

Declaration = FAngelscriptType::BuildFunctionDeclaration(
    ReturnType, ScriptName, ArgumentTypes, ArgumentNames, ArgumentDefaults, ...);
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 位置: 327-335, 3995-4003
// 定位: reflective fallback 只复制实际传入的参数；脚本默认值则被反向写成 UE metadata
// ============================================================================
// BlueprintCallableReflectiveFallback.cpp
if (!ensure(ScriptArgIndex < Generic->GetArgCount()))
{
    DestroyParameterBuffer(Function, ParameterBuffer);
    return false;                                                       // ★ thunk 期望编译后的调用已补齐 default
}

void* ScriptArgumentAddress = Generic->GetAddressOfArg(ScriptArgIndex);
void* SourceAddress = ResolveScriptArgumentAddress(Property, ScriptArgumentAddress);
Property->CopySingleValue(Destination, SourceAddress);

// AngelscriptClassGenerator.cpp
if (ArgDesc.DefaultValue.Len() != 0)
{
    FString UnrealDefaultValue;
    if (ArgDesc.Type.DefaultValue_AngelscriptToUnreal(ArgDesc.DefaultValue, UnrealDefaultValue))
    {
        FString DefaultValueMeta = TEXT("CPP_Default_");
        DefaultValueMeta += ArgDesc.ArgumentName;
        NewFunction->SetMetaData(*DefaultValueMeta, *UnrealDefaultValue); // ★ 脚本默认值再回写到 UE 世界
    }
}
```

新增对比结论：

- `UnLua` 在这里的关键点不是“支持默认参数”，而是 **必须在 runtime 持有 native 默认值数据库**，因为 Lua 调用 native 函数时没有编译期补参阶段。
- `Angelscript` 在这里的关键点不是“没有默认参数数据库”，而是 **实现方式不同**：它把 UE 默认值提前编进脚本声明，把脚本默认值再回写到 UE metadata，尽量避免 runtime fallback 再维护一套 `DefaultParamCollection`。
- 如果比较“是否存在与 `UnLua` 对等的 runtime default-param database”，相对 `UnLua` 当前快照，`Angelscript` 属于 `没有实现同层 runtime default database`。
- 如果比较“默认值是否能在 Script → UE / UE → Script 双向流动”，`Angelscript` 反而体现出 `实现方式不同` 的优势：它的 class generator 明确做了双向转换；`UnLua` 当前快照更偏 native → runtime call 的单向消费。

### [维度 D3] 覆写补丁驻留在哪一层：`UnLua` 用 transient shadow class 贴补丁，`Angelscript` 把 override/mixin 写成生成类 schema

这一轮补的不是“谁支持 Blueprint 覆写”，而是**覆写数据最后住在哪个对象图层级**。`UnLua` 的补丁实体是 `ULuaOverridesClass`。`FLuaOverrides::Override()` 先为目标 `UClass` 找或创建一个 transient override class，再把原函数 duplicate 成 `ULuaFunction` 挂进去；激活时要么替换原 `UFunction` 的 native thunk，要么把继承来的函数名补进当前类的 `FunctionMap`。这是一层运行时 patch overlay，不是重新定义 class schema。

`Angelscript` 走的是另一条路。预处理器先在源码阶段约束 `BlueprintOverride` 的合法性，然后 `ClassGenerator` 直接在生成类上 `NewObject<UFunction>`、挂 `FUNC_BlueprintEvent` / `FUNC_BlueprintCallable` / `MixinArgument` 元数据；再往后，`StaticJIT::PrecompiledData` 还会把 `bBlueprintOverride` / `bBlueprintEvent` 等 flag 序列化保存。也就是说，`Angelscript` 的 override/mixin 不是“挂一个 patch object”，而是**把 schema 本身做成可持久化的生成产物**。

```
[D3-Deep] Override Payload Residency
UnLua
├─ FLuaOverrides::GetOrAddOverridesClass()         // 为目标 UClass 创建 transient shadow class
├─ Duplicate UFunction -> ULuaFunction            // 覆写体存放在 override class
├─ SetActive(true) patches native thunk/map       // 激活时才接管入口
└─ Restore/Suspend/Resume toggle patch layer      // patch 可整体摘除

Angelscript
├─ Preprocessor validates BlueprintOverride        // 先约束 authoring contract
├─ ClassGenerator creates real UFunction schema    // 直接写进生成类
├─ Mixin metadata stored on generated function     // 绑定 self / mixin 参数
└─ PrecompiledData persists override flags         // 覆写语义进入持久化产物
```

关键源码 [1]：`UnLua` 的覆写驻留在 transient `ULuaOverridesClass`，激活/恢复都是围绕 patch layer 运作

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaOverrides.cpp
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaOverridesClass.cpp
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaFunction.cpp
// 位置: 36-82, 39-55, 210-240
// 定位: UnLua 覆写不是重建类，而是为现有 UClass 附加一个可开关的 patch 层
// ============================================================================
// LuaOverrides.cpp
const auto OverridesClass = GetOrAddOverridesClass(Class);              // ★ 先拿 shadow class
...
FObjectDuplicationParameters DuplicationParams(Function, OverridesClass);
DuplicationParams.DestClass = ULuaFunction::StaticClass();
LuaFunction = static_cast<ULuaFunction*>(StaticDuplicateObjectEx(DuplicationParams));
...
LuaFunction->Override(Function, Class, bAddNew);
LuaFunction->Bind();

// LuaOverridesClass.cpp
for (TFieldIterator<ULuaFunction> It(this, EFieldIteratorFlags::ExcludeSuper); It; ++It)
{
    const auto LuaFunction = *It;
    LuaFunction->SetActive(bActive);                                    // ★ Suspend/Resume 只是切 patch 层开关
}

Class->ClearFunctionMapsCaches();
if (bActive)
    AddToOwner();
else
    RemoveFromOwner();

// LuaFunction.cpp
if (bAdded)
{
    check(!Class->FindFunctionByName(GetFName(), EIncludeSuperFlag::ExcludeSuper));
    SetSuperStruct(Function);
    FunctionFlags |= FUNC_Native;
    SetNativeFunc(execCallLua);
    Class->AddFunctionToFunctionMap(this, *GetName());                  // ★ 继承函数在子类 map 中补一层入口
}
else
{
    Function->FunctionFlags |= FUNC_Native;
    Function->SetNativeFunc(&execScriptCallLua);                        // ★ 直接 patch 原 UFunction thunk
    Function->GetOuterUClass()->AddNativeFunction(*Function->GetName(), &execScriptCallLua);
}
```

关键源码 [2]：`Angelscript` 把 override/mixin 直接做成生成类 schema，并把关键 flag 写进预编译数据

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp
// 位置: 2820-2830, 3436-3458, 460-462, 2951-2953
// 定位: override/mixin 是生成类的一部分，不是 runtime patch overlay
// ============================================================================
// AngelscriptClassGenerator.cpp
UFunction* NewFunction = NewObject<UFunction>(NewClass, *FuncName, RF_Public);
NewFunction->FunctionFlags = FUNC_Event | FUNC_BlueprintEvent | FUNC_Public; // ★ 先把 schema 生出来
...
NewFunction->Next = NewClass->Children;
NewClass->Children = NewFunction;
NewClass->AddFunctionToFunctionMap(NewFunction, NewFunction->GetFName());

if (ScriptFunction->traits.GetTrait(asTRAIT_MIXIN) && ScriptFunction->parameterNames.GetLength() >= 1)
{
    FString MixinArgumentName = ANSI_TO_TCHAR(ScriptFunction->parameterNames[0].AddressOf());
    NewFunction->SetMetaData(NAME_Function_MixinArgument, *MixinArgumentName);
    NewFunction->SetMetaData(NAME_Function_DefaultToSelf, *MixinArgumentName); // ★ mixin 语义也写进生成函数
}

if ((FunctionDesc->bBlueprintEvent && FunctionDesc->bCanOverrideEvent) || FunctionDesc->bBlueprintOverride)
    NewFunction->FunctionFlags |= FUNC_BlueprintEvent;

// PrecompiledData.cpp
bBlueprintCallable = FunctionDesc->bBlueprintCallable;
bBlueprintOverride = FunctionDesc->bBlueprintOverride;                  // ★ 覆写语义进入持久化产物
bBlueprintEvent = FunctionDesc->bBlueprintEvent;
...
FunctionDesc->bBlueprintCallable = bBlueprintCallable;
FunctionDesc->bBlueprintOverride = bBlueprintOverride;
FunctionDesc->bBlueprintEvent = bBlueprintEvent;
```

新增对比结论：

- `UnLua` 的优势不是“更灵活”，而是 **覆写补丁可独立启停/恢复**：`Suspend/Resume/Restore` 都是在 shadow class 层工作，不需要把原类完整重建一遍。
- `Angelscript` 的优势不是“也能覆写”，而是 **override/mixin 语义直接进入 generated schema 与 precompiled manifest**，这让后续 Blueprint、StaticJIT、工具链都能共享同一份类定义。
- 如果比较“是否有与 `ULuaOverridesClass` 对等的 runtime patch carrier”，相对 `UnLua` 当前快照，`Angelscript` 属于 `没有实现同层 transient patch carrier`；它用类重建与 schema 持久化替代了这层对象。
- 如果比较“脚本侧是否能把新成员语义长期写进 UE 类型系统”，相对 `Angelscript` 当前快照，`UnLua` 属于 `没有实现同层 generated-schema persistence`。它能把继承函数入口补进子类 `FunctionMap`，但这仍是函数 patch，不是类 schema 扩展。

### [维度 D5] 符号目录是谁维护：`UnLua` 把断点会话外包给外部 Lua 调试器，`Angelscript` 把 debug database 与 client subscription 收进协议

前文已经比较过断点、停机和 transport，这一轮补的是**“调试客户端首次连上来，谁负责把符号世界解释给它”**。`UnLua` 当前快照里，官方文档直接要求用户安装 `LuaPanda` / `LuaHelper`，把 `LuaPanda.lua` 放进工程脚本目录，再在业务 Lua 里主动 `require(...).start()`；仓内公开的调试接口则集中在 `GetStackVariables()`、`GetLuaCallStack()`、`PrintCallStack()` 这类 live-state 采样函数上。

`Angelscript` 则把“符号目录初始化”做成了协议主路径。调试客户端一发 `RequestDebugDatabase`，服务端就把它加入 `ClientsThatWantDebugDatabase`，先下发 `DebugDatabaseSettings`，再分块发送 `DebugDatabase` JSON，最后显式发送 `DebugDatabaseFinished` 并继续发送 asset database。这里交付的不是一段“请你自己去 attach 的脚本”，而是**仓内自定义调试协议拥有的 symbol catalog 生命周期**。

```
[D5-Deep] Symbol Catalog Ownership
UnLua
├─ Docs tell user to install LuaPanda/LuaHelper    // 外部调试器负责会话层
├─ Lua script calls require(...).start()           // attach 由业务脚本主动发起
└─ UnLuaDebugBase exposes stack/value samplers     // 仓内主要提供现场采样

Angelscript
├─ Client sends RequestDebugDatabase               // 会话先请求仓内符号目录
├─ DebugDatabaseSettings                           // 先同步语言/运行时配置
├─ DebugDatabase (chunked JSON)                    // 分块下发类型与声明
├─ DebugDatabaseFinished                           // 明确 catalog 结束边界
└─ Asset database follows                          // 再补资产视图
```

关键源码 [1]：`UnLua` 官方路径是外部 Lua 调试器 attach，仓内公开接口更偏现场采样

```md
<!-- =========================================================================
文件: Reference/UnLua/Docs/CN/Debugging.md
位置: 4-14
定位: 官方调试文档明确要求用户安装外部 Lua 调试器并在业务脚本中主动 start
============================================================================ -->
1. 从VSCode应用市场安装 LuaPanda / LuaHelper
2. 从 LuaPanda 官方仓库获取 `LuaPanda.lua`，放入 `{UE工程}/Content/Script` 目录
3. 在Lua代码中加入 `require("LuaPanda").start("127.0.0.1",8818)`

注：调试器依赖 `luasocket`，UnLua 已通过扩展插件集成...
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDebugBase.h
// 位置: 75-94
// 定位: 仓内公开调试接口聚焦于栈和变量采样，而不是自带 symbol database 协议
// ============================================================================
/**
 * Get local variables and upvalues of the runtime stack
 */
UNLUA_API bool GetStackVariables(lua_State *L, int32 StackLevel, TArray<FLuaVariable> &LocalVariables, TArray<FLuaVariable> &Upvalues, int32 Level = MAX_int32);

/**
 * Get call stack
 */
UNLUA_API FString GetLuaCallStack(lua_State *L);

/* 在IDE断点调试窗口中直接运行UnLua::PrintCallStack(L)来打印当前堆栈 */
void PrintCallStack(lua_State* L);
```

关键源码 [2]：`Angelscript` 把调试符号目录做成 DebugServer V2 的显式消息流

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 位置: 25-42, 536-553, 581-588, 822-827, 1499-1515, 2046-2049
// 定位: DebugServer V2 不只传断点/停止事件，还持有完整的 symbol catalog 初始化协议
// ============================================================================
// AngelscriptDebugServer.h
enum class EDebugMessageType : uint8
{
    Diagnostics,
    RequestDebugDatabase,                                          // ★ 客户端先请求符号目录
    DebugDatabase,
    ...
    HasStopped,
    ...
    DebugDatabaseFinished,
};

struct FAngelscriptDebugDatabaseSettings : FDebugMessage
{
    bool bAutomaticImports = false;
    bool bFloatIsFloat64 = false;
    bool bUseAngelscriptHaze = false;
    bool bDeprecateStaticClass = false;
    bool bDisallowStaticClass = false;                              // ★ 目录还携带语言/运行时配置
};

TArray<class FSocket*> ClientsThatWantDebugDatabase;               // ★ 服务器显式维护 catalog 订阅者

// AngelscriptDebugServer.cpp
if (MessageType == EDebugMessageType::RequestDebugDatabase)
{
    ClientsThatWantDebugDatabase.Add(Client);
    SendDebugDatabase(Client);
    FAngelscriptEngine::Get().EmitDiagnostics(Client);
}

FAngelscriptDebugDatabaseSettings DebugSettings;
...
SendMessageToClient(Client, EDebugMessageType::DebugDatabaseSettings, DebugSettings);
...
SendMessageToClient(Client, EDebugMessageType::DebugDatabase, DB); // ★ 分块发送 JSON debug database

FEmptyMessage Message;
SendMessageToClient(Client, EDebugMessageType::DebugDatabaseFinished, Message); // ★ 显式结束 catalog 传输
SendAssetDatabase(Client);
```

新增对比结论：

- `UnLua` 这里不是 `没有调试`；更准确的判断是 `实现方式不同`：仓内主交付物是对 `lua_State` 的现场采样能力，而断点会话、源码映射和 IDE 交互主要交给外部 Lua 调试器。
- `Angelscript` 的新增优势不只是“内置 debugger”，而是 **仓内拥有 symbol catalog 的订阅、配置、分块传输和完成边界**。这让调试客户端不必依赖业务脚本自己 `require(...).start()` 才能知道类型世界。
- 如果比较“是否存在与 `RequestDebugDatabase -> DebugDatabaseFinished` 对等的仓内符号目录协议”，相对 `Angelscript` 当前快照，`UnLua` 属于 `没有实现同层 repo-owned symbol catalog protocol`。
- 代价也同样明确：`Angelscript` 需要长期维护自定义协议、设置版本和资产目录同步；`UnLua` 把这部分复杂度外包给 `LuaPanda/LuaHelper`，维护面更小，但仓内控制面也更弱。

---

## 深化分析 (2026-04-09 07:32:06)

### [维度 D9] 测试作者语言的 ownership：`UnLua` 让作者围绕 `lua_State/UWorld` 写回归，`Angelscript` 让作者围绕 engine mode + module helper 写回归

前几轮已经把 runner、sample asset 和 latent orchestration 拆开了。这一轮只补**测试作者在 `.cpp` 里真正面对的语言表面**。`UnLua` 当前快照里，作者拿到的核心对象是 `FUnLuaTestBase` 暴露的 `lua_State* L`、`UGameInstance*`、`FWorldContext*`，再配合 `RUNNER_TEST_*` / `TEST_*` 断言宏去操作 live VM。无论是 `BindingTest.cpp` 那种手工 `Run([this](lua_State* L, UWorld* World){...})`，还是 `Issue346Test.cpp` 里覆写 `SetUp()` 直接 `RunChunk()`、`lua_tostring()`，测试作者面对的基本单位都是“已经启动的脚本 VM + 世界状态”。

`Angelscript` 当前快照则把作者表面切成另一套语言：先选 `ASTEST_CREATE_ENGINE_FULL/SHARE_CLEAN/...`，再进入 `ASTEST_BEGIN_*` 生命周期块，然后通过 `ASTEST_COMPILE_RUN_INT`、`CompileModuleWithSummary()`、`AnalyzeReloadFromMemory()`、`ExecuteIntFunction()` 等 helper 驱动“脚本文本 -> module desc -> compile/execute/assert”流水线。`AngelscriptMacroValidationTests.cpp` 里的测试几乎不直接碰 `asIScriptContext` 或原始参数栈，而是把 `ModuleName + Source + Decl` 作为主要作者输入。这说明两边差异不只是“都能写 Automation test”，而是**测试作者究竟是在写 live scene script，还是在写 compile/execute contract**。

```
[D9-Deep] Test Authoring Surface
UnLua
├─ IMPLEMENT_UNLUA_* / BEGIN_TESTSUITE              // 注册 UE Automation 外壳
├─ FUnLuaTestBase exposes L / World / GameInstance  // 作者拿到 live VM/world
├─ UnLua::RunChunk / lua_getglobal / lua_tostring   // 断言直接围绕 Lua C API
└─ RUNNER_TEST_* / TEST_*                           // 失败即从当前步骤早退

Angelscript
├─ ASTEST_CREATE_ENGINE_*                           // 先选隔离档位
├─ ASTEST_BEGIN_*                                   // 再进入明确生命周期块
├─ ASTEST_COMPILE_RUN_INT / ASTEST_BUILD_MODULE     // 作者主要写 module + source + decl
└─ CompileModuleWithSummary / AnalyzeReload...      // helper 直接返回编译/重载诊断
```

关键源码 [1]：`UnLua` 的测试作者表面是 live `lua_State/world`，断言宏只是在这个表面上做薄包装

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Public/UnLuaTestCommon.h
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Tests/BindingTest.cpp
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Tests/Issue346Test.cpp
// 行号: 118-159, 171-245; 21-45, 53-71; 28-58
// 位置: 测试壳负责把 L/world 暴露给作者；具体回归直接操作 Lua VM 与世界对象
// ============================================================================
// UnLuaTestCommon.h
struct UNLUATESTSUITE_API FUnLuaTestBase
{
protected:
    lua_State* L;                         // ★ 测试作者直接拿到 live Lua VM
    FAutomationTestBase* TestRunner;
    UGameInstance* GameInstance;
    FWorldContext* WorldContext;
};

#define IMPLEMENT_UNLUA_INSTANT_TEST(TestClass, PrettyName) \
IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass##Runner, PrettyName, (...)) \
bool TestClass##Runner::RunTest(const FString& Parameters) \
{ \
    TestClass* TestInstance = new TestClass(); \
    TestInstance->SetTestRunner(*this); \
    if (TestInstance->SetUp()) \
    { \
        bSuccess = TestInstance->InstantTest(); \
        TestInstance->TearDown(); \
    } \
    delete TestInstance; \
    return bSuccess; \
}

#define RUNNER_TEST_EQUAL(expression, expected)\
    if (!GetTestRunner().TestEqual(TEXT(#expression), expression, expected))\
    {\
        return true;                    // ★ 失败后从当前步骤早退，测试作者仍停留在 live harness 语义里
    }

// BindingTest.cpp
static void Run(TFunction<void(lua_State*, UWorld*)> Test)
{
    UnLua::Startup();
    const auto L = UnLua::GetState();
    const auto World = UWorld::CreateWorld(EWorldType::Game, false, "UnLuaTest");
    ...
    UnLua::PushUObject(L, World);
    lua_setglobal(L, "World");          // ★ 世界对象直接推入 Lua 全局

    Test(L, World);                     // ★ 业务测试主体直接收到 lua_State + UWorld

    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    UnLua::Shutdown();
}

// Issue346Test.cpp
virtual bool SetUp() override
{
    ...
    FUnLuaTestBase::SetUp();

    const auto Chunk = R"(
        local FunctionLibrary = UE.UObject.Load("/UnLuaTestSuite/Tests/Regression/Issue346/BFL_Issue346.BFL_Issue346_C")
        local Result = FunctionLibrary.Test("lua")
        return Result
    )";
    UnLua::RunChunk(L, Chunk);          // ★ 断言前先直接跑 Lua chunk
    const auto Result = lua_tostring(L, -1);
    RUNNER_TEST_EQUAL(Result, "blueprint and lua");
    return true;
}
```

关键源码 [2]：`Angelscript` 把测试作者表面前移成“选择引擎档位 + 使用 helper 驱动 module contract”

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestMacros.h
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.h
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Validation/AngelscriptMacroValidationTests.cpp
// 行号: 34-69, 82-122, 162-193; 18-45; 319-350, 372-431; 82-142
// 位置: 作者先声明引擎生命周期，再用 helper 驱动 compile / reload / execute
// ============================================================================
// AngelscriptTestMacros.h
#define ASTEST_CREATE_ENGINE_FULL() \
    (*[this]() -> TUniquePtr<FAngelscriptEngine>& { \
        static thread_local TUniquePtr<FAngelscriptEngine> _FullEngine; \
        _FullEngine = AngelscriptTestSupport::CreateIsolatedFullEngine(); \
        return _FullEngine; \
    }())

#define ASTEST_BEGIN_SHARE_CLEAN \
    { \
        FAngelscriptEngineScope _AutoEngineScope(Engine);

#define ASTEST_COMPILE_RUN_INT(Engine, ModuleName, Source, FuncDecl, OutResult) \
    do { \
        asIScriptModule* _Module = AngelscriptTestSupport::BuildModule(*this, Engine, ModuleName, Source); \
        asIScriptFunction* _Function = AngelscriptTestSupport::GetFunctionByDecl(*this, *_Module, FuncDecl); \
        if (!AngelscriptTestSupport::ExecuteIntFunction(*this, Engine, *_Function, OutResult)) { return false; } \
    } while (false)

// AngelscriptTestEngineHelper.h
struct FAngelscriptCompileTraceSummary
{
    ECompileType CompileType = ECompileType::SoftReloadOnly;
    ECompileResult CompileResult = ECompileResult::Error;
    TArray<FString> ModuleNames;
    TArray<FAngelscriptCompileTraceDiagnosticSummary> Diagnostics; // ★ helper 直接把编译诊断做成正式测试数据
};

bool AnalyzeReloadFromMemory(...);
bool ExecuteIntFunction(...);

// AngelscriptTestEngineHelper.cpp
bool AnalyzeReloadFromMemory(...)
{
    const bool bCompiled = PreprocessAndCompile(Engine, ECompileType::SoftReloadOnly, MoveTemp(Filename), MoveTemp(Script), &CompileResult);
    ...
    case ECompileResult::PartiallyHandled:
        OutReloadRequirement = FAngelscriptClassGenerator::FullReloadSuggested;
        bOutWantsFullReload = true;                                // ★ 重载分析也是 helper 正式返回值
        return true;
}

bool ExecuteIntFunction(...)
{
    ...
    const int PrepareResult = Context->Prepare(Function);
    ...
    const int ExecuteResult = Context->Execute();
    ...
    OutResult = static_cast<int32>(Context->GetReturnDWord());     // ★ 返回值提取被 helper 吞掉
    return true;
}

// AngelscriptMacroValidationTests.cpp
bool FAngelscriptGlobalBindingsMacroValidationTest::RunTest(const FString& Parameters)
{
    FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_FULL();
    ASTEST_BEGIN_FULL

    int32 Result = 0;
    ASTEST_COMPILE_RUN_INT(Engine, "ASGlobalVariableCompatMacro", TEXT(R"( int Entry(){ return 1; } )"), TEXT("int Entry()"), Result);
    bPassed = TestEqual(TEXT("..."), Result, 1);                   // ★ 作者主要在写 source/declaration contract

    ASTEST_END_FULL
    return bPassed;
}
```

新增对比结论：

- `UnLua` 在这里不是 `没有测试 DSL`；它明确实现了 `FUnLuaTestBase + IMPLEMENT_UNLUA_* + RUNNER_TEST_*` 这一层包装。
- 但如果比较“测试作者是否拥有与 `module/source/decl/compile/reload diagnostics` 对等的 helper-oriented surface”，相对 `Angelscript` 当前快照，`UnLua` 属于 `没有实现同层 authoring surface`。
- 两边整体属于 `实现方式不同`：`UnLua` 更像在写 live integration scene，`Angelscript` 更像在写 compile/execute contract。
- 如果只看“编译/重载诊断是否作为测试 helper 的正式返回物”这一点，`Angelscript` 证据链更完整；这可以视为局部 `实现质量差异`，但不是功能有无差异。

### [维度 D9] 测试隔离档位与清理 contract：`UnLua` 主要提供单一 harness 分支，`Angelscript` 把 isolation budget 做成显式选择

前几轮已经说过 `UnLua` 会 `SetUp()` 世界、`AutomationOpenMap()` 或临时建 `UWorld`。这一轮补的是**谁来定义“隔离到什么程度、失败后清到什么程度”**。`UnLua` 当前快照里，这个 contract 主要集中在 `FUnLuaTestBase::SetUp/TearDown()` 一条主路径：统一 `UnLua::Startup()`、统一持有 `L`、统一创建 `UGameInstance`/`WorldContext`，然后按 `InstantTest()` 与否分成“立即销毁世界并 `Shutdown()`”或“通过 `AddLatent()` 延迟 `Shutdown()`”两支。`BindingTest.cpp` 里的 `Run()` helper 也是同样思路，测试清理本质上仍围绕一套 live world/VM harness。

`Angelscript` 则把这件事提升成**测试作者可选的 isolation budget**。宏层就区分 `FULL / SHARE / SHARE_CLEAN / SHARE_FRESH / CLONE / NATIVE`；工具层的 `ResetSharedCloneEngine()` 不仅丢弃 active modules，还会遍历 raw AngelScript modules、`DeleteDiscardedModules()`，再清理 `ScriptTypePtr == nullptr` 的 `UASClass`、`RemoveFromRoot()`、`ClearFlags(RF_Standalone)` 并触发 `CollectGarbage()`。`AcquireFreshSharedCloneEngine()` 甚至会先销毁 shared + stray global 再重新获取 clean engine。换句话说，`Angelscript` 不只是“有 teardown”，而是把**清理强度与复用成本**做成了正式的 fixture contract。

```
[D9-Deep] Isolation and Cleanup Contract
UnLua
├─ FUnLuaTestBase::SetUp()                         // 统一 Startup + GameInstance + WorldContext
├─ InstantTest ? destroy-now : latent shutdown    // 主要只有即时/延迟两支
├─ BindingTest::Run() reuses same harness idea    // helper 也围绕同一 live VM/world
└─ isolation profile mostly implicit in harness    // 隔离强度不作为显式档位暴露给作者

Angelscript
├─ FULL / SHARE / SHARE_CLEAN / SHARE_FRESH        // 宏层先选隔离预算
├─ CLONE / NATIVE                                  // 进一步区分 wrapper vs raw SDK
├─ ResetSharedCloneEngine()                        // 丢弃 active modules + raw AS modules
├─ RemoveFromRoot / ClearFlags / GC                // 连 detached generated class 都显式清理
└─ isolation profile is an explicit author choice  // 复用成本与清理强度被正式编码
```

关键源码 [1]：`UnLua` 的清理 contract 主要集中在单一 harness 的分支逻辑里

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/UnLuaTestCommon.cpp
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Tests/BindingTest.cpp
// 行号: 60-115; 21-45
// 位置: SetUp/TearDown 与测试 helper 都围绕同一套 VM/world harness 展开
// ============================================================================
// UnLuaTestCommon.cpp
bool FUnLuaTestBase::SetUp()
{
    UnLua::Startup();
    L = UnLua::GetState();                              // ★ 同一入口统一拿到 VM

    GameInstance = NewObject<UGameInstance>(GEngine);
    GameInstance->InitializeStandalone();
    WorldContext = GameInstance->GetWorldContext();
    ...
    if (MapName.IsEmpty())
    {
        const auto World = UWorld::CreateWorld(EWorldType::Game, false, "UnLuaTest");
        World->SetGameInstance(GameInstance);
        WorldContext->SetCurrentWorld(World);
    }
    else if (InstantTest())
    {
        GEngine->LoadMap(*WorldContext, URL, nullptr, Error);       // ★ 即时测试直接切世界
    }
    else
    {
        AutomationOpenMap(MapName);                                 // ★ latent 测试改走 Automation map 路径
    }
    return true;
}

void FUnLuaTestBase::TearDown()
{
    if (InstantTest())
    {
        const auto World = GetWorld();
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
        UnLua::Shutdown();                                          // ★ 即时测试直接关
    }
    else
    {
        AddLatent([this] { UnLua::Shutdown(); });                   // ★ latent 测试延后关
    }
}

// BindingTest.cpp
static void Run(TFunction<void(lua_State*, UWorld*)> Test)
{
    UnLua::Startup();
    ...
    Test(L, World);
    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    UnLua::Shutdown();                                              // ★ helper 依旧复用同一清理哲学
}
```

关键源码 [2]：`Angelscript` 把 isolation tier 和 reset 深度做成显式、可组合的 contract

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestMacros.h
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h
// 行号: 34-69, 82-154; 207-269, 378-432
// 位置: 宏层选择隔离档位；工具层定义 shared engine reset 的清理深度
// ============================================================================
// AngelscriptTestMacros.h
#define ASTEST_CREATE_ENGINE_FULL() ...
#define ASTEST_CREATE_ENGINE_SHARE_CLEAN() AngelscriptTestSupport::AcquireCleanSharedCloneEngine()
#define ASTEST_CREATE_ENGINE_SHARE_FRESH() AngelscriptTestSupport::AcquireFreshSharedCloneEngine()
#define ASTEST_CREATE_ENGINE_CLONE() ...
#define ASTEST_CREATE_ENGINE_NATIVE() asCreateScriptEngine(ANGELSCRIPT_VERSION)

#define ASTEST_BEGIN_FULL \
    { \
        FAngelscriptEngineScope _AutoEngineScope(Engine); \
        ON_SCOPE_EXIT \
        { \
            const TArray<TSharedRef<FAngelscriptModuleDesc>> _ActiveModules = Engine.GetActiveModules(); \
            for (const TSharedRef<FAngelscriptModuleDesc>& _Module : _ActiveModules) \
            { \
                Engine.DiscardModule(*_Module->ModuleName);          // ★ FULL/CLONE 在 block 退出时自动丢模块
            } \
        };

// AngelscriptTestUtilities.h
inline void ResetSharedCloneEngine(FAngelscriptEngine& Engine)
{
    const TArray<TSharedRef<FAngelscriptModuleDesc>> ActiveModules = Engine.GetActiveModules();
    for (const TSharedRef<FAngelscriptModuleDesc>& Module : ActiveModules)
    {
        Engine.DiscardModule(*Module->ModuleName);                   // ★ 先丢 wrapper 级 module
    }

    if (asCScriptEngine* ScriptEngine = reinterpret_cast<asCScriptEngine*>(Engine.GetScriptEngine()))
    {
        ...
        ScriptEngine->DiscardModule(ModuleNameAnsi.Get());
        ScriptEngine->DeleteDiscardedModules();                      // ★ 再丢 raw AngelScript module
    }

    for (TObjectIterator<UASClass> It; It; ++It)
    {
        if (It->ScriptTypePtr == nullptr)
        {
            if (It->IsRooted())
                It->RemoveFromRoot();                                // ★ detached generated class 也清理
            It->ClearFlags(RF_Standalone);
        }
    }

    CollectGarbage(RF_NoFlags, true);
}

inline FAngelscriptEngine& AcquireCleanSharedCloneEngine()
{
    FAngelscriptEngine& Engine = GetOrCreateSharedCloneEngine();
    ResetSharedCloneEngine(Engine);                                  // ★ clean = 复用 shared engine，但先重置
    return Engine;
}

inline FAngelscriptEngine& AcquireFreshSharedCloneEngine()
{
    DestroySharedAndStrayGlobalTestEngine();                         // ★ fresh = 连 shared/global 都先销毁
    return AcquireCleanSharedCloneEngine();
}
```

新增对比结论：

- `UnLua` 在这里不是 `没有清理`；源码明确显示它有 `Startup/Shutdown`、world destroy 和 latent defer teardown。
- 但如果比较“测试作者是否能显式选择与 `FULL/SHARE/FRESH/CLONE/NATIVE` 对等的 isolation profile”，相对 `Angelscript` 当前快照，`UnLua` 属于 `没有实现同层 fixture isolation profile`。
- 两边整体仍属于 `实现方式不同`：`UnLua` 让 harness 隐式决定隔离强度，`Angelscript` 让作者显式选 isolation budget。
- 如果关注的是 shared test engine 污染后的可恢复性，`Angelscript` 的 reset 证据链更完整；这可以视为局部 `实现质量差异`，但不应反推 `UnLua` 的 harness 无法工作。

### [维度 D8] 参数缓冲生命周期的 ownership：`UnLua` 把复用责任放进 `FFunctionDesc`，`Angelscript` 让 reflective fallback 保持一次性栈缓冲

前几轮已经分析过 `UnLua` 的调用桥会在 `FFunctionDesc::PreCall/PostCall` 中逐属性搬运参数。这一轮补的是**这些参数到底由谁持有、复用多久**。`UnLua` 当前快照里，`FFunctionDesc` 构造时就执行 `Buffer = FParamBufferFactory::Get(*InFunction)`，也就是每个 `UFunction` 描述都会绑定一份参数缓冲分配策略；如果打开 `ENABLE_PERSISTENT_PARAM_BUFFER`，实际使用的是 `FParamBufferAllocator_Persistent`，它在 allocator 内部保留 `Buffers[]`，`Get()` 只在不足时扩容，`Pop()` 只是回退计数器，不释放内存。这意味着 `UnLua` 不只是“每次调用再 `Memzero` 一块参数区”，而是**把参数缓冲复用也视为函数描述的一部分**。

`Angelscript` 当前源码里，能直接对位比较的是 `BlueprintCallableReflectiveFallback.cpp`。这里的 reflective fallback 每次进入都 `FMemory_Alloca(Function->ParmsSize)`，随后 `InitializeParameterBuffer()`、`ProcessEvent()`、copy-back `out/ref`、最后 `DestroyParameterBuffer()`。当前已读源码里没有与 `FParamBufferAllocator_Persistent` 对等的 fallback-local allocator；结合前文已经证实的 direct-bind 主路径主要依赖已固化签名与连续 argument buffer，可以推断这是**有意把持久化复杂度留在 `UnLua` 的反射主路径，而不是留在 `Angelscript` 的补洞路径**。这是一条基于当前可见源码的推断，不是单纯的性能结论。

```
[D8-Deep] Param Buffer Lifetime Ownership
UnLua
├─ FFunctionDesc ctor -> Buffer = FParamBufferFactory::Get()
├─ Persistent allocator owns Buffers[] per function
├─ CallUE / ExecuteDelegate -> Buffer->Get()
├─ ProcessEvent / ProcessDelegate
└─ Buffer->Pop() returns slot without freeing

Angelscript
├─ Reflective fallback call starts
├─ FMemory_Alloca(ParmsSize) on stack
├─ InitializeParameterBuffer()
├─ ProcessEvent()
├─ Copy back out/ref + return
└─ DestroyParameterBuffer() then stack unwinds
```

关键源码 [1]：`UnLua` 的参数缓冲策略在 `FFunctionDesc` 构造期就被绑定，并且可选择持久复用

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/ParamBufferAllocator.h
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/ParamBufferAllocator.cpp
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp
// 行号: 17-74; 5-70; 31-52, 206-235, 249-255
// 位置: allocator 生命周期被挂到每个 FFunctionDesc 上，而不是临时散落在每次调用里
// ============================================================================
// ParamBufferAllocator.h
class FParamBufferAllocator_Persistent : public FParamBufferAllocator
{
private:
    uint8 Counter;
    uint16 ParmsSize;
    TArray<void*> Buffers;                  // ★ allocator 自己长期持有 buffer 池
};

// ParamBufferAllocator.cpp
void* FParamBufferAllocator_Persistent::Get()
{
    if (Counter < Buffers.Num())
        return Buffers[Counter++];          // ★ 有空槽就直接复用

    Counter++;
    const auto Buffer = FMemory::Malloc(ParmsSize, 16);
    FMemory::Memzero(Buffer, ParmsSize);
    Buffers.Add(Buffer);                    // ★ 不够时才扩容
    return Buffer;
}

void FParamBufferAllocator_Persistent::Pop(void* Memory)
{
    check(Counter > 0);
    Counter--;                              // ★ 归还 slot，不释放底层内存
    check(Buffers[Counter] == Memory);
}

TSharedRef<FParamBufferAllocator> FParamBufferFactory::Get(const UFunction& Func)
{
#if ENABLE_PERSISTENT_PARAM_BUFFER
    return MakeShareable(new FParamBufferAllocator_Persistent(Func)); // ★ 默认可切到持久 allocator
#else
    return MakeShareable(new FParamBufferAllocator_Always(Func));
#endif
}

// FunctionDesc.cpp
FFunctionDesc::FFunctionDesc(UFunction *InFunction, FParameterCollection *InDefaultParams)
{
    ...
    Buffer = FParamBufferFactory::Get(*InFunction);  // ★ 每个函数描述绑定一份 buffer policy
}

int32 FFunctionDesc::CallUE(lua_State *L, int32 NumParams, void *Userdata)
{
    ...
    const auto Params = Buffer->Get();
    ...
    Object->UObject::ProcessEvent(FinalFunction, Params);
    ...
    Buffer->Pop(Params);                             // ★ 调用结束只归还，不必立即 free
    return NumReturnValues;
}

int32 FFunctionDesc::ExecuteDelegate(lua_State *L, int32 NumParams, int32 FirstParamIndex, FScriptDelegate *ScriptDelegate)
{
    const auto Params = Buffer->Get();
    ScriptDelegate->ProcessDelegate<UObject>(Params);
    Buffer->Pop(Params);                             // ★ delegate 路径也走同一 allocator
    return NumReturnValues;
}
```

关键源码 [2]：`Angelscript` 的 reflective fallback 把参数缓冲保留为一次性栈对象，而不是持久池

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp
// 行号: 51-66, 302-370
// 位置: fallback 每次调用都构造/销毁一块临时参数缓冲，不持有跨调用 buffer 池
// ============================================================================
void InitializeParameterBuffer(const UFunction* Function, uint8* Buffer)
{
    FMemory::Memzero(Buffer, Function->ParmsSize);
    for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
    {
        It->InitializeValue_InContainer(Buffer);
    }
}

void DestroyParameterBuffer(const UFunction* Function, uint8* Buffer)
{
    for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
    {
        It->DestroyValue_InContainer(Buffer);
    }
}

bool InvokeReflectiveUFunctionFromGenericCall(...)
{
    uint8* ParameterBuffer = static_cast<uint8*>(FMemory_Alloca(Function->ParmsSize)); // ★ 每次调用临时栈分配
    InitializeParameterBuffer(Function, ParameterBuffer);

    for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
    {
        ...
        Property->CopySingleValue(Destination, SourceAddress);
        if (Property->HasAnyPropertyFlags(CPF_ReferenceParm) && !Property->HasAnyPropertyFlags(CPF_ConstParm))
            OutReferences[OutReferenceCount++] = { Property, SourceAddress };
    }

    TargetObject->ProcessEvent(Function, ParameterBuffer);
    ...
    DestroyParameterBuffer(Function, ParameterBuffer);                                   // ★ 调用结束后立即销毁
    return true;
}
```

新增对比结论：

- 这轮补出的关键修正是：`UnLua` 的运行时反射调用不只是“每次重跑 `FProperty` 解释器”，它还把**参数缓冲复用策略**内建进了 `FFunctionDesc`。
- 如果比较“反射主路径/反射 fallback 是否拥有与 `FParamBufferAllocator_Persistent` 对等的持久缓冲池”，相对 `UnLua` 当前快照，`Angelscript` 在当前可见 fallback 实现上属于 `没有实现同层 persistent param buffer reuse`。
- 但整体上不应直接判成 `实现质量差异`。更准确的判断是 `实现方式不同`：`UnLua` 把 runtime 反射调用当主路径，所以值得维护 per-function buffer policy；`Angelscript` 则把这类通用参数缓冲成本主要留在补洞分支。
- 上一条关于 `Angelscript` 设计意图的结论，是基于当前 `BlueprintCallableReflectiveFallback.cpp` 与前文已证 direct-bind 主路径的源码推断，不是单独凭性能想象得出的判断。

---

## 深化分析 (2026-04-09 07:42:45)

### [维度 D5] 调试值树的物化时机：`UnLua` 先递归拍平快照，`Angelscript` 再按路径请求节点

前几轮已经把“谁拥有调试 transport / breakpoint protocol”讲清楚了，但**值本身是怎么被构造成调试树**，还没有真正落到源码。补读之后可以更明确地说：`UnLua` 的 `FLuaDebugValue` 是一个**自带 `Keys[]/Values[]` 子树的递归快照对象**，调用 `Build()` 时就会一路把 `table`、`userdata`、`UStruct`、`TArray/TMap/TSet` 展开；`Angelscript` 则先为函数构建 `FDebugValuePrototype`，真正停在某个 frame 时才 `Instantiate()` 当前变量，再由调试客户端通过 `RequestVariables(Path)` / `RequestEvaluate(Path)` 按路径拿当前节点。两边都支持“看值”，但一个更像**本地快照树**，另一个更像**可寻址变量图**。

```
[D5-Deep] Debug Value Materialization
UnLua
├─ lua_getstack / lua_getlocal / lua_getupvalue   // 直接从 Lua VM 栈取值
├─ FLuaDebugValue::Build()                        // ★ 当场递归展开
│  ├─ table -> Keys[] / Values[]                 // 直接把子节点塞进当前结构
│  ├─ userdata -> UStruct/TArray/TMap/TSet       // 借 UE 反射继续展开
│  └─ ReadableValue / Type                       // 形成可读文本
└─ caller gets self-contained snapshot tree       // 调用方拿到的是完整快照

Angelscript
├─ GetDebugPrototype(Function)                    // ★ 先缓存变量布局 prototype
├─ Frame.Prototype->Instantiate(StackFrame)       // 命中 frame 时实例化当前值
├─ RequestVariables(Path)                         // 客户端按路径请求当前 scope
├─ RequestEvaluate(Path)                          // 客户端按路径求值
└─ response carries Name/Value/Type/address/size  // 节点可继续向下请求成员
```

关键源码 [1]：`UnLua` 的调试值在采样时就被递归展开成完整树

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDebugBase.h
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaDebugBase.cpp
// 行号: 29-72; 43-120, 206-340, 620-666
// 位置: FLuaDebugValue 直接持有子节点数组，Build() 时同步展开 Lua/UE 混合值
// ============================================================================
struct UNLUA_API FLuaDebugValue
{
    FString ReadableValue;
    FString Type;
    int32 Depth;
    bool bAlreadyBuilt;
    TArray<FLuaDebugValue> Keys;                   // ★ 调试值自己持有 key 子树
    TArray<FLuaDebugValue> Values;                 // ★ 调试值自己持有 value 子树

private:
    void Build(lua_State *L, int32 Index, int32 Level = MAX_int32);
    void BuildFromUserdata(lua_State *L, int32 Index);
    void BuildFromUStruct(UStruct *Struct, void *ContainerPtr, UObjectBaseUtility *ContainerObject = nullptr);
    void BuildFromTArray(FScriptArrayHelper &ArrayHelper, const FProperty *InnerProperty);
    void BuildFromTMap(FScriptMapHelper &MapHelper);
    void BuildFromTSet(FScriptSetHelper &SetHelper);
};

void FLuaDebugValue::Build(lua_State *L, int32 Index, int32 Level)
{
    if (!L || bAlreadyBuilt)
        return;

    int32 ValueType = lua_type(L, Index);
    Type = lua_typename(L, ValueType);
    switch (ValueType)
    {
    case LUA_TTABLE:
        if (Level > 0)
        {
            lua_pushnil(L);
            while (lua_next(L, Index) != 0)
            {
                FLuaDebugValue *Key = AddKey();
                Key->Build(L, -2, Level - 1);      // ★ 直接递归构造 key 子树

                FLuaDebugValue *Value = AddValue();
                Value->Build(L, -1, Level - 1);    // ★ 直接递归构造 value 子树
                lua_pop(L, 1);
            }
        }
        break;
    case LUA_TUSERDATA:
        BuildFromUserdata(L, Index);               // ★ userdata 继续下钻到 UE 反射对象/容器
        break;
    }
}

void FLuaDebugValue::BuildFromUserdata(lua_State *L, int32 Index)
{
    void *ContainerPtr = UnLua::GetPointer(L, Index);
    ...
    if (Struct)
    {
        BuildFromUStruct(Struct, ContainerPtr, ContainerObject); // ★ 结构体直接按 PropertyLink 展开
    }
    else if (ClassName == TEXT("TArray"))
    {
        BuildFromTArray(ArrayHelper, InnerProperty);             // ★ 原生容器也被直接展开成可读树
    }
    else if (ClassName == TEXT("TMap"))
    {
        BuildFromTMap(MapHelper);
    }
    else if (ClassName == TEXT("TSet"))
    {
        BuildFromTSet(SetHelper);
    }
}

void FLuaDebugValue::BuildFromUStruct(UStruct *Struct, void *ContainerPtr, UObjectBaseUtility *ContainerObject)
{
    for (FProperty *Property = Struct->PropertyLink; Property; Property = Property->PropertyLinkNext)
    {
        FLuaDebugValue *Key = AddKey();
        Key->ReadableValue = Property->GetNameCPP();

        FLuaDebugValue *Value = AddValue();
        Value->BuildFromUProperty(Property, Property->ContainerPtrToValuePtr<void>(ContainerPtr));
    }
}

...

const char *VarName = lua_getlocal(L, &ar, i);
Variable.Key = UTF8_TO_TCHAR(VarName);
Variable.Value.Depth = 0;
Variable.Value.Build(L, -1, Level);               // ★ 取局部变量时立刻构造完整值树
```

关键源码 [2]：`Angelscript` 先缓存变量布局，再用路径请求当前节点

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 行号: 5338-5418, 5497-5514; 424-448; 1081-1128
// 位置: DebugPrototype 缓存变量布局，DebugServer 再按 Path 返回当前节点载荷
// ============================================================================
FDebugValuePrototype* GetDebugPrototype(asIScriptFunction* Function)
{
    asCScriptFunction* Func = (asCScriptFunction*)Function;
    if (Func->DebugPrototypePtr != nullptr)
        return (FDebugValuePrototype*)Func->DebugPrototypePtr;    // ★ 同一函数先复用 debug prototype

    FDebugValuePrototype* Proto = new FDebugValuePrototype;
    ...
    FASDebugValue* DebugValue = Type.CreateDebugValue(*Proto, -Offset * 4);
    if (DebugValue != nullptr)
        DebugValue->Name = FName(ANSI_TO_TCHAR(VarName));         // ★ 这里只固化名字、类型和取值 offset

    Func->DebugPrototypePtr = Proto;
    return Proto;
}

...

if (Frame.Prototype != nullptr)
{
    Frame.Variables = (FDebugValues*)Frame.Prototype->Instantiate(
        ((asCContext*)Context)->GetStackFrame(i)                  // ★ 真正命中 frame 时才实例化当前值
    );
}

struct FAngelscriptVariable
{
    FString Name;
    FString Value;
    FString Type;
    uint64 ValueAddress = 0;                                      // ★ 额外携带可监视地址
    int32 ValueSize = 0;
    bool bHasMembers = false;                                     // ★ 由客户端决定是否继续下探
};

else if (MessageType == EDebugMessageType::RequestVariables)
{
    FString Path;
    *Datagram << Path;                                            // ★ 客户端按 Path 请求某个 scope

    if (GetDebuggerScope(Path, Scope))
    {
        for (auto& Value : Scope.Values)
        {
            FAngelscriptVariable Var;
            Var.Name = Value.Name;
            Var.Value = Value.Value;
            Var.Type = Value.Type;
            Var.ValueAddress = reinterpret_cast<uint64>(Value.GetAddressToMonitor());
            Var.ValueSize = Value.GetAddressToMonitorValueSize();
            Var.bHasMembers = Value.bHasMembers;
            Vars.Variables.Add(Var);                              // ★ 返回当前层节点，不一次性递归打平全树
        }
    }
}
```

新增对比结论：

- `UnLua` 在这里不是 `没有变量观察能力`；它明确实现了 `lua stack -> FLuaDebugValue tree` 的递归快照。
- 如果比较“仓内是否存在与 `RequestVariables/RequestEvaluate + Path + ValueAddress` 对等的**按路径查询变量协议**”，相对 `Angelscript` 当前快照，`UnLua` 属于 `没有实现同层仓内变量查询协议`。
- 两边整体属于 `实现方式不同`：`UnLua` 的优势是**一次采样即可得到可读快照**，`Angelscript` 的优势是**值节点可继续寻址、适配 `DataBreakpoint` 与分层展开**。
- 如果关注大型对象/深层成员的增量展开与地址级监视，`Angelscript` 这一层证据链更完整；这可以视为调试协议层的局部 `实现质量差异`，但不应抹掉 `UnLua` 已有的值树构建能力。

### [维度 D9] 测试结果的最终判定权：`UnLua` 以 UE Automation 原生 contract 为终点，`Angelscript` 追加仓库级 summary/exit normalization

前面几轮已经讲过 `UnLua` 有自己的 harness、`Angelscript` 有自己的 test module。这一轮补的是**测试跑完之后，谁有权决定“这次仓库运行算不算成功”**。`UnLua` 当前快照里，`IMPLEMENT_UNLUA_LATENT_TEST` / `IMPLEMENT_UNLUA_INSTANT_TEST` 直接落到 `IMPLEMENT_SIMPLE_AUTOMATION_TEST`，再由 `SetUp()` / `TearDown()` 和 `AutomationOpenMap()` 驱动运行；也就是说，终点 contract 仍然是 UE Automation 自己的测试名与 pass/fail。`Angelscript` 则在仓库外面再包了一层 `RunTests.ps1` 与 `GetAutomationReportSummary.ps1`：先规范 group/输出目录，再把 `Automation Report + Log` 归一成 `Summary.json`，甚至会在 **process exit code 为 0** 但结构化报告缺失或日志里有阻塞错误时，主动把最终退出码提升成失败。

```
[D9-Deep] Test Result Ownership
UnLua
├─ IMPLEMENT_SIMPLE_AUTOMATION_TEST               // 直接接 UE Automation
├─ SetUp / InstantTest / Latent commands          // harness 负责环境
├─ AutomationOpenMap / UnLua::Startup            // 运行时准备
└─ final contract == UE Automation result/name    // 仓库不再额外重判

Angelscript
├─ RunTests.ps1 validates group/output layout      // ★ 先标准化仓库入口
├─ UnrealEditor-Cmd Automation RunTests ...        // 再调用 UE Automation
├─ GetAutomationReportSummary.ps1                  // ★ 解析 report + log
├─ Summary.json records failures/hints/source      // 生成稳定产物
└─ wrapper may promote exit 0 -> failure           // process code 不是最终判定
```

关键源码 [1]：`UnLua` 的测试 contract 直接落在 UE Automation 宏与 latent command 上

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Public/UnLuaTestCommon.h
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/UnLuaTestCommon.cpp
// 行号: 171-244; 60-108
// 位置: UnLua 自己封装了 harness，但最终仍把结果交给 UE Automation 宏体系
// ============================================================================
#define IMPLEMENT_UNLUA_LATENT_TEST(TestClass, PrettyName) \
IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass##_Runner, PrettyName, (EAutomationTestFlags::ClientContext | EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)) \
bool TestClass##_Runner::RunTest(const FString& Parameters) \
{ \
    TestClass* TestInstance = new TestClass(); \
    TestInstance->SetTestRunner(*this); \
    ADD_LATENT_AUTOMATION_COMMAND(FUnLuaTestCommand_SetUpTest(TestInstance)); \
    ADD_LATENT_AUTOMATION_COMMAND(FUnLuaTestCommand_PerformTest(TestInstance)); \
    ADD_LATENT_AUTOMATION_COMMAND(FUnLuaTestCommand_TearDownTest(TestInstance)); \
    return true;                                                // ★ 结果语义仍由 UE Automation 驱动
}

#define IMPLEMENT_UNLUA_INSTANT_TEST(TestClass, PrettyName) \
IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass##Runner, PrettyName, (EAutomationTestFlags::ClientContext | EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)) \
bool TestClass##Runner::RunTest(const FString& Parameters) \
{ \
    bool bSuccess = false; \
    TestClass* TestInstance = new TestClass(); \
    TestInstance->SetTestRunner(*this); \
    if (TestInstance->SetUp()) \
    { \
        bSuccess = TestInstance->InstantTest();                 // ★ harness 只返回 pass/fail 给 Automation
        TestInstance->TearDown(); \
    } \
    delete TestInstance; \
    return bSuccess; \
}

bool FUnLuaTestBase::SetUp()
{
    UnLua::Startup();
    L = UnLua::GetState();
    ...
    if (MapName.IsEmpty())
    {
        const auto World = UWorld::CreateWorld(EWorldType::Game, false, "UnLuaTest");
        ...
    }
    else if (InstantTest())
    {
        GEngine->LoadMap(*WorldContext, URL, nullptr, Error);   // ★ instant test 直接切地图
    }
    else
    {
        AutomationOpenMap(MapName);                             // ★ latent test 复用 UE map harness
    }
    return true;
}
```

关键源码 [2]：`Angelscript` 在 Automation 外再包了一层仓库级运行与结果归一

```powershell
# ============================================================================
# 文件: Tools/RunTests.ps1
# 文件: Tools/GetAutomationReportSummary.ps1
# 行号: 54-86, 214-235; 289-346
# 位置: 先执行 UE Automation，再把 report/log 归一成 Summary.json 与最终退出码
# ============================================================================
# Tools/RunTests.ps1
$definedGroups = @(Get-DefinedAutomationGroups -ProjectRoot $projectRoot)
if ($definedGroups -notcontains $Group) {
    throw "Unknown automation group '$Group'. Defined groups: $($definedGroups -join ', ')"   # ★ 先把仓库入口标准化
}

$outputLayout = New-CommandOutputLayout -ProjectRoot $projectRoot -Category 'Tests' -Label $Label -RequestedOutputRoot $OutputRoot -LogFileName 'Automation.log'
$summaryPath = Join-Path $outputLayout.OutputRoot 'Summary.json'

$argumentList = @(
    $agentConfig.ProjectFile
    "-ExecCmds=Automation RunTests $target; Quit"                                      # ★ 真正执行仍调用 UE Automation
    '-TestExit=Automation Test Queue Empty'
)

$summaryObject = & (Join-Path $PSScriptRoot 'GetAutomationReportSummary.ps1') `
    -ReportPath $outputLayout.ReportPath `
    -LogPath $outputLayout.LogPath `
    -ExitCode $processExitCode `
    -BucketName $target `
    -SummaryPath $summaryPath

if ($processExitCode -eq 0 -and $null -ne $summaryRecord) {
    $missingStructuredSummary = $summarySource -eq 'None'
    if ($failedCount -gt 0 -or $failedTests.Count -gt 0 -or $logHints.Count -gt 0 -or $missingStructuredSummary) {
        $scriptExitCode = $exitCodes.TestFailed                                      // ★ wrapper 可以把 0 提升成失败
    }
}

# Tools/GetAutomationReportSummary.ps1
$summary = [ordered]@{
    BucketName      = $BucketName
    ExitCode        = $ExitCode
    ReportPath      = $ReportPath
    LogPath         = $LogPath
    FailedTests     = @()
    LogFailureHints = @()
    SummarySource   = 'None'
}

if ($null -ne $reportSummary) {
    $summary.ReportJsonPath = $reportSummary.SourceFile
    $summary.Total = $reportSummary.Total
    $summary.Passed = $reportSummary.Passed
    $summary.Failed = $reportSummary.Failed
    $summary.Skipped = $reportSummary.Skipped
    $summary.FailedTests = $reportSummary.FailedTests
    $summary.SummarySource = 'ReportJson'                                            // ★ 尽量以结构化报告为准
}

$summary.LogFailureHints = @(Get-LogFailureHints -Path $LogPath)
if ($summary.SummarySource -eq 'None' -and $ExitCode -eq 0) {
    $summary.LogFailureHints = @($summary.LogFailureHints + 'Structured automation report was not produced.')
}
```

新增对比结论：

- `UnLua` 在这里不是 `没有测试基础设施`；它已经实现了 `macro DSL + harness + map latent command`。
- 但如果比较“仓库是否交付与 `Summary.json + log hint + exit-code promotion` 对等的**repo-level runner contract**”，相对 `Angelscript` 当前快照，`UnLua` 属于 `没有实现同层仓库运行包装层`。
- 两边整体属于 `实现方式不同`：`UnLua` 把测试终点交给 UE Automation 原生结果；`Angelscript` 把 UE Automation 当作中间层，再补一层适合 CI / 本地脚本消费的稳定产物。
- 如果关注 CI 稳定性和脚本化消费，`Angelscript` 这一层可视为局部 `实现质量差异`；但这说的是**仓库 runner 契约**，不是说 `UnLua` 的测试内容本身更浅。

### [维度 D10] 上手流程是否直接驱动编辑器动作：`UnLua` 的教程文字与工具栏实现共用 `GetModuleName` 主线，`Angelscript` 当前入口更偏通用编辑器能力

前几轮已经讲过 `UnLua` 文档和教程很多。这一轮补的是**文档是不是“照着做就会触发仓内某条明确工具链”**。`UnLua` 的答案很明确：`Quickstart` 教你“绑定到 Lua → 修改 `GetModuleName` → 生成 Lua 模版”，而 editor toolbar 代码里，`BuildNodeMenu()` 只在 `GetModuleName` 图上挂 `Reveal in Explorer`，`CreateLuaTemplate_Executed()` 也直接执行 `GetModuleName` 拿 module path，再按父类链查找 `Config/LuaTemplates/*.lua`。`IntelliSense.md` 说会写到 `Plugins/UnLua/Intermediate/IntelliSense`，而 `FUnLuaIntelliSenseGenerator::Initialize()` / `SaveFile()` 和 commandlet 代码正好在做这件事。也就是说，`UnLua` 的文档不是“介绍功能”，而是在描述一条**仓内已经编码好的 authoring pipeline**。

`Angelscript` 当前 editor 入口则更通用。`ShowCreateBlueprintPopup()` 根据脚本类的 `RelativeSourceFilePath` 猜默认资产目录并创建 Blueprint/DataAsset；`RegisterToolsMenuEntries()` 暴露的是 “Open workspace / Generate bindings / Function tests” 这类全局入口；`FScriptEditorPrompts::ShowPromptToCallFunction()` 则是把 `UFunction` 参数反射成通用 prompt form。它同样强，但重点在**通用 editor integration**，而不是像 `UnLua` 那样围绕单一 `module path` 组织一整套新手路径。

```
[D10-Deep] Onboarding Workflow Surface
UnLua
├─ Quickstart: Bind -> edit GetModuleName -> Create Lua Template
├─ Node menu only appears on GetModuleName graph
├─ CreateLuaTemplate executes GetModuleName() to locate target script
├─ Template lookup walks class -> super class -> Config/LuaTemplates
├─ IntelliSense generator listens to AssetRegistry and writes Intermediate/IntelliSense
└─ docs + toolbar + workspace share one authoring contract

Angelscript
├─ ShowCreateBlueprintPopup(UASClass)            // 从 script class 创建 Blueprint/DataAsset
├─ Tools menu: Open workspace / Generate binds   // 全局入口
├─ ScriptEditorPrompts                           // 把 UFunction 参数转成通用表单
└─ onboarding leans generic editor integration   // 不是 module-path-specific scaffold
```

关键源码 [1]：`UnLua` 文档直接描述仓内已编码好的 `GetModuleName -> template/intellisense` 工作流

```md
<!-- =========================================================================
文件: Reference/UnLua/Docs/CN/Quickstart_For_UE_Newbie.md
文件: Reference/UnLua/Docs/CN/CustomTemplate.md
文件: Reference/UnLua/Docs/CN/IntelliSense.md
行号: 11-20; 3-10; 5-17
位置: 文档写的是具体编辑器动作与产物路径，不是抽象能力介绍
============================================================================ -->
- 双击打开上一步新建好的蓝图，点击UnLua菜单栏中的“绑定”，默认会自动根据蓝图路径填充好Lua的模块路径。
- 如果下次需要修改绑定的路径，可以找到 `GetModuleName` 函数并双击进行修改。
- 点击UnLua菜单栏中的“生成Lua模板文件”，会在工程 `Content/Script` 目录下生成。

在绑定类型到Lua后，可以通过 `UnLua工具栏 -> 创建Lua模版文件` 来快速在绑定的路径下生成对应的Lua文件。
默认会根据类型名称在 `UnLua/Config/LuaTemplates` 目录下来查找模版文件，如果文件不存在会使用它的父类名称来继续查找。

打开UnLua工具栏，点击导出智能提示，会在`{UE工程}/Plugins/UnLua/Intermediate`下生成`IntelliSense`的目录。
一个工作区中分别添加了`Script`和`IntelliSense`，这样每次生成智能提示信息后会自动刷新。
```

关键源码 [2]：`UnLuaEditor` 代码严格围绕 `GetModuleName` 和模版/IntelliSense 产物路径组织

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp
// 行号: 109-124, 257-313, 315-377; 42-58, 120-172; 29-125
// 位置: 文档提到的操作在 editor code 里都能找到一一对应的实现
// ============================================================================
void FUnLuaEditorToolbar::BuildNodeMenu()
{
    UToolMenu* BPMenu = UToolMenus::Get()->ExtendMenu("GraphEditor.GraphNodeContextMenu.K2Node_FunctionResult");
    BPMenu->AddDynamicSection("UnLua", FNewToolMenuDelegate::CreateLambda([this](UToolMenu* ToolMenu)
    {
        UGraphNodeContextMenuContext* GraphNodeCtx = ToolMenu->FindContext<UGraphNodeContextMenuContext>();
        if (GraphNodeCtx && GraphNodeCtx->Graph && GraphNodeCtx->Graph->GetName() == "GetModuleName")
        {
            UnLuaSection.AddEntry(... "Reveal in Explorer");       // ★ 只在 GetModuleName 图上给脚本路径入口
        }
    }), ...);
}

void FUnLuaEditorToolbar::CreateLuaTemplate_Executed()
{
    const auto Func = Class->FindFunctionByName(FName("GetModuleName"));
    FString ModuleName;
    Class->GetDefaultObject()->ProcessEvent(Func, &ModuleName);    // ★ 模版路径直接来自 GetModuleName

    const auto RelativePath = ModuleName.Replace(TEXT("."), TEXT("/"));
    const auto FileName = FString::Printf(TEXT("%s%s.lua"), *GLuaSrcFullPath, *RelativePath);

    for (auto TemplateClass = Class; TemplateClass; TemplateClass = TemplateClass->GetSuperClass())
    {
        auto RelativeFilePath = "Config/LuaTemplates" / TemplateClassName + ".lua";
        auto FullFilePath = FPaths::ProjectConfigDir() / RelativeFilePath;
        if (!FPaths::FileExists(FullFilePath))
            FullFilePath = BaseDir / RelativeFilePath;             // ★ 工程模板优先，其次插件模板

        ...
        FFileHelper::SaveStringToFile(Content, *FileName, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        break;
    }
}

void FUnLuaIntelliSenseGenerator::Initialize()
{
    OutputDir = IPluginManager::Get().FindPlugin("UnLua")->GetBaseDir() + "/Intermediate/IntelliSense";
    AssetRegistryModule.Get().OnAssetAdded().AddRaw(this, &FUnLuaIntelliSenseGenerator::OnAssetAdded);
    AssetRegistryModule.Get().OnAssetRenamed().AddRaw(this, &FUnLuaIntelliSenseGenerator::OnAssetRenamed); // ★ 让 stub 目录跟资产变化同步
}

void FUnLuaIntelliSenseGenerator::Export(const UField* Field)
{
    auto ModuleName = Package->GetName();
    FString FileName = UnLua::IntelliSense::GetTypeName(Field);
    const FString Content = UnLua::IntelliSense::Get(Field);
    SaveFile(ModuleName, FileName, Content);                       // ★ 把反射结果写成磁盘 stub
}

int32 UUnLuaIntelliSenseCommandlet::Main(const FString &Params)
{
    const auto ExportedReflectedClasses = UnLua::GetExportedReflectedClasses();
    ...
    SaveFile(ModuleName, TEXT("GlobalFunctions"), GeneratedFileContent);

    if (ParamsMap.Contains(TEXT("BP")) && ParamsMap[TEXT("BP")] == TEXT("1"))
    {
        auto Generator = FUnLuaIntelliSenseGenerator::Get();
        Generator->Initialize();
        Generator->UpdateAll();                                    // ★ commandlet 与 editor 走同一套产物目录
    }
}
```

关键源码 [3]：`AngelscriptEditor` 入口更偏通用 editor integration，而不是单一 module-path scaffolding

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorPrompts.cpp
// 行号: 405-532, 696-737; 182-238
// 位置: Angelscript 更强调“从 script class/ufunction 泛化 editor 行为”
// ============================================================================
FAngelscriptRuntimeModule::GetEditorCreateBlueprint().AddLambda(
    [](UASClass* ScriptClass)
    {
        FAngelscriptEditorModule::ShowCreateBlueprintPopup(ScriptClass);   // ★ 从 script class 创建 Blueprint/DataAsset
    }
);

void FAngelscriptEditorModule::ShowCreateBlueprintPopup(UASClass* Class)
{
    FString AssetPath;
    ...
    FString ScriptRelativePath = Class->GetRelativeSourceFilePath();
    ScriptRelativePath.ParseIntoArray(Subfolders, TEXT("/"), true);
    ...
    Asset = FKismetEditorUtilities::CreateBlueprint(
        Class, Package, AssetName, BPTYPE_Normal,
        BlueprintClass, BlueprintGeneratedClass, FName("AngelscriptCreateBlueprint")
    );                                                                  // ★ 重点是把 script class 接进标准资产创建流程
}

void FAngelscriptEditorModule::RegisterToolsMenuEntries()
{
    Section.AddMenuEntry("ASOpenCode", ..., "Open Angelscript workspace (VS Code)", ...); // ★ 全局 workspace 入口
    BindSection.AddMenuEntry("ASGenerateBindings", ..., "Legacy Native Bind Generator (Debug Only)", ...);
    Section.AddMenuEntry(...);                                                           // ★ 还有 test/function 等全局入口
}

bool FScriptEditorPrompts::ShowPromptToCallFunction(const UObject* Object, FName FunctionName, FScriptEditorPromptOptions Options, TArray<UObject*> FirstParameterObjects)
{
    UFunction* Function = Object->GetClass()->FindFunctionByName(FunctionName);
    TSharedRef<FStructOnScope> FuncParams = MakeShared<FStructOnScope>(Function);

    for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
    {
        FString Defaults;
        if (UEdGraphSchema_K2::FindFunctionParameterDefaultValue(Function, *It, Defaults))
        {
            It->ImportText_Direct(*Defaults, It->ContainerPtrToValuePtr<uint8>(FuncParams->GetStructMemory()), nullptr, PPF_None);
        }
    }

    return ShowPromptForStruct(FuncParams, Options);                                     // ★ 这里提供的是通用反射 prompt，而不是脚本模版脚手架
}
```

新增对比结论：

- `UnLua` 在这里不是“文档写得多”这么简单；它把 `GetModuleName`、模版查找、脚本路径 reveal、IntelliSense 输出目录和 VSCode workspace 串成了一条单一 onboarding 主线。
- 如果比较“是否存在与 `GetModuleName -> CreateLuaTemplate -> RevealInExplorer -> IntelliSense stub tree` 对等的**module-path-specific onboarding scaffold**”，相对 `UnLua` 当前快照，`Angelscript` 属于 `没有实现同层定向脚手架`。
- 两边整体属于 `实现方式不同`：`UnLua` 倾向为新手规定一条窄而清晰的路径；`Angelscript` 倾向提供更通用的 editor capability，再让使用者自行组合。
- 这不自动意味着 `UnLua` 更好。对插件成熟期的 `Angelscript` 来说，是否补这一层取决于目标是“降低首次接入成本”还是“保持 editor API 泛化能力”。如果当前优先级是 onboarding 资产与 workflow entry points，`UnLua` 这条线的可吸收价值是高优先级的。

---

## 深化分析 (2026-04-09 07:57:36)

### [维度 D4] 热重载控制面的 ownership：`UnLua` 的“点名重载”真正驻留在 Lua 资产层，`Angelscript` 的选择权驻留在 engine 文件队列

前几轮已经把 `UnLua` 的对象图修补、`Angelscript` 的编译事务、以及 watcher / rename / queue 机制讲清了。这一轮补的是一个更细但很关键的层次：**谁有权决定“这次只重载哪几个目标”**。

`UnLua` 当前快照里，这个控制面实际分成三层。第一层是对外声明：`Reference/UnLua/Plugins/UnLua/Content/IntelliSense/UnLua.lua:15-18` 把 API 写成 `UnLua.HotReload(ModuleNames)`。第二层是 native 桥：`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaLib.cpp:51-59,84-90` 把 `HotReload` 暴露成 C 函数，但函数体只是执行 `require('UnLua.HotReload').reload()`，没有继续把 Lua 栈上的 `ModuleNames` 往下传。第三层才是 Lua 资产：`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:604-624` 真正实现了 `reload(module_names)`，传参时直接 `reload_modules(module_names)`，不传时才按时间戳扫描。也就是说，从当前源码严格看，**定向 module 选择器是 Lua 资产层的 seam，不是 C++ / editor 默认入口的同层能力**。

`Angelscript` 则把这件事做在 engine C++ 里。`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2253-2284` 的 `PerformHotReload(ECompileType, const TArray<FFilenamePair>& InReloadFiles)` 明确接受“本次要处理的文件集合”；`...:2729-2770,2859-2894` 又说明这些集合来自 `FileChangesDetectedForReload`、`FileDeletionsDetectedForReload`、`QueuedFullReloadFiles` 和后台扫描线程。当前快照可见的控制面是 engine / editor / 测试 helper 调 `PerformHotReload()` 或喂队列，而不是脚本侧主动传入一个 module list。

```
[D4-Deep] Reload Selection Control Plane
UnLua
├─ IntelliSense: UnLua.HotReload(ModuleNames)       // 对外声明支持点名模块
├─ native UnLuaLib::HotReload()                     // C++ 桥只触发 reload()
└─ require('UnLua.HotReload').reload(module_names)  // 真正的定向选择器在 Lua 资产层

Angelscript
├─ background scan builds FileChangesDetectedForReload // 先由 engine 采集文件变化
├─ CheckForHotReload() drains queue                    // 汇总 add/delete/full-reload queue
└─ PerformHotReload(CompileType, InReloadFiles)        // 定向集合由 engine C++ 持有
```

关键源码 [1]：`UnLua` 对外 API 声明、native 桥和 Lua 资产实现并不在同一层

```lua
-- ============================================================================
-- 文件: Reference/UnLua/Plugins/UnLua/Content/IntelliSense/UnLua.lua
-- 位置: 对外声明 `UnLua.HotReload(ModuleNames)` 支持定向模块列表
-- ============================================================================
---Hot reload lua modules
---@param ModuleNames table @[opt]Specify a list of module names that need hot reloading.
function UnLua.HotReload(ModuleNames)
end
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaLib.cpp
// 函数: UnLua::UnLuaLib::HotReload
// 位置: native 桥只执行 reload()，没有把 Lua 栈上的模块列表继续下传
// ============================================================================
static int HotReload(lua_State* L)
{
#if UNLUA_WITH_HOT_RELOAD
    if (luaL_dostring(L, "require('UnLua.HotReload').reload()") != 0)   // ★ 这里没有把参数传给 reload(module_names)
    {
        LogError(L);
    }
#endif
    return 0;
}

static constexpr luaL_Reg UnLua_Functions[] = {
    {"Log", LogInfo},
    {"LogWarn", LogWarn},
    {"LogError", LogError},
    {"HotReload", HotReload},                                            // ★ `UnLua.HotReload` 实际绑定到这个 C 函数
    {"Ref", Ref},
    {"Unref", Unref},
    {"FTextEnabled", nullptr},
    {NULL, NULL}
};
```

```lua
-- ============================================================================
-- 文件: Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua
-- 函数: M.reload
-- 位置: 真正的模块列表选择器在 Lua 资产层；传参时直接按给定列表 reload
-- ============================================================================
function M.reload(module_names)
    if module_names then
        reload_modules(module_names)           -- ★ 只有这一层真正消费模块列表
        return
    end

    local modified_modules = {}

    for module_name, time in pairs(loaded_module_times) do
        if not ignore_modules[module_name] then
            local current_time = get_last_modified_time(module_name)
            if current_time ~= time then
                modified_modules[#modified_modules + 1] = module_name
                loaded_module_times[module_name] = current_time
            end
        end
    end
    print("modified modules:", dump(modified_modules))
    if #modified_modules > 0 then
        reload_modules(modified_modules)
    end
end
```

关键源码 [2]：`Angelscript` 的热重载选择器直接由 engine C++ 持有

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: FAngelscriptEngine::PerformHotReload / CheckForHotReload / CheckForFileChanges
// 位置: 热重载目标集合由 engine 自己构造、排队和消费
// ============================================================================
bool FAngelscriptEngine::PerformHotReload(ECompileType CompileType, const TArray<FFilenamePair>& InReloadFiles)
{
    TArray<FFilenamePair> FileList;
    FileList.Append(InReloadFiles);                                   // ★ 调用者显式给出本次要处理的文件

    for (auto& FailedFile : PreviouslyFailedReloadFiles)
    {
        FileList.AddUnique(FailedFile);                               // ★ 失败文件自动并回本批次
    }
    PreviouslyFailedReloadFiles.Empty();

    TSet<FFilenamePair> FilesToHotReload;                             // ★ 后续再扩张依赖闭包
    ...
}

void FAngelscriptEngine::CheckForHotReload(ECompileType CompileType)
{
    TArray<FFilenamePair> FileList;
    FileList.Append(FileChangesDetectedForReload);
    FileChangesDetectedForReload.Empty();

    if (FileList.Num() != 0 || FPlatformTime::Seconds() - LastFileChangeDetectedTime > 0.2)
    {
        for (const auto& DeletedFile : FileDeletionsDetectedForReload)
            FileList.AddUnique(DeletedFile);                          // ★ add / delete / queued full reload 都先变成文件集合
        FileDeletionsDetectedForReload.Empty();
    }

    if (FileList.Num() != 0)
    {
        PerformHotReload(CompileType, FileList);                      // ★ 最终由 engine 自己消费这组文件
    }
}

void FAngelscriptEngine::CheckForFileChanges()
{
    TArray<FFilenamePair> Filenames;
    FindAllScriptFilenames(Filenames);

    for (FFilenamePair& Filename : Filenames)
    {
        ...
        FileChangesDetectedForReload.Add(Filename);                   // ★ 变化首先进入 engine 队列
    }
}
```

新增对比结论：

- `UnLua` 在这里不是 `没有定向热重载`；真正的 `module_names` 选择器确实存在，只是驻留在 `Content/Script/UnLua/HotReload.lua` 这一层。
- 从当前快照看，`UnLua` 的对外声明与 native 桥并不完全同层对齐：`IntelliSense` 写成 `HotReload(ModuleNames)`，但 `UnLuaLib.cpp` 暴露的 C 函数并没有继续传递参数。这更像控制面分层，而不是能力不存在。
- 如果比较“是否有同层 script-level public reload selector”，相对 `UnLua` 当前快照，`Angelscript` 属于 `没有实现`；当前 `Angelscript` 的选择权主要在 engine C++ / editor / test helper。
- 两边整体属于 `实现方式不同`：`UnLua` 让高级用户直接改 Lua 资产控制重载目标；`Angelscript` 让 engine 先把文件变化变成事务输入，再做依赖扩张和状态机处理。

### [维度 D9] 测试策略配置面的 ownership：`UnLua` 把环境编排写进 fixture / 用例，`Angelscript` 把回归预算提升为 `DeveloperSettings`

前文已经分析过 `UnLua` 的 harness、issue 回归和 benchmark，也分析过 `Angelscript` 的 runner、summary 与 artifact。这一轮只补一个此前没展开的面：**测试环境策略是不是一个全局可配置对象**。

`UnLua` 当前快照里，基础 harness 当然存在，但测试环境的策略主要散在 fixture 和单个用例内部。`Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Public/UnLuaTestCommon.h:125-154` 与 `.../Private/UnLuaTestCommon.cpp:60-97` 表明，`FUnLuaTestBase` 只提供一个通用框架：先 `UnLua::Startup()`，再根据 `GetMapName()` 与 `InstantTest()` 决定是临时建 `UWorld` 还是 `AutomationOpenMap()`。真正跑哪张图、注入哪些蓝图、是否切两次图，通常继续写在各个 issue 文件里，例如 `Issue295Test.cpp:23-50` 直接把两个 map path 和 UMG 复现场景写死在用例体内；`LuaDeadLoopCheck.spec.cpp:34-57` 则在 spec 内部直接修改 `UUnLuaSettings::DeadLoopCheck`。这意味着 `UnLua` 的测试策略更像“每个回归文件自带一份小型环境脚本”。

`Angelscript` 的形状明显不同。`Plugins/Angelscript/Source/AngelscriptRuntime/Testing/AngelscriptTestSettings.h:8-124` 把“热重载后是否自动跑回归、每批最多测多少模块、IntegrationTest 地图根、命名约束、覆盖率、网络仿真、debug break”统一做成 `UDeveloperSettings`。`IntegrationTest.cpp:293-344,820-834` 在真正运行前会把这些设置施加到 `ULevelEditorPlaySettings`、network emulation、debug break 和 map naming contract 上；`CodeCoverage.cpp:22-49` 又把同一套 `UAngelscriptTestSettings` 接进 Automation 生命周期和命令行开关。也就是说，**测试政策本身就是仓内可枚举、可设置、可回放的一等配置对象**。

```
[D9-Deep] Test Policy Configuration Surface
UnLua
├─ FUnLuaTestBase::SetUp()                         // 统一 world/map harness
├─ test overrides GetMapName()/SetUp()            // 具体环境继续写在用例里
├─ issue tests hardcode asset/map paths           // 回归策略随文件局部增长
└─ spec mutates UUnLuaSettings inline             // 设置测试也在用例体内改值

Angelscript
├─ UAngelscriptTestUserSettings                   // hot reload 后测试批次预算
├─ UAngelscriptTestSettings                       // map root / naming / coverage / net emulation
├─ IntegrationTest.cpp reads settings             // 统一施加到 PIE、网络和断点策略
└─ CodeCoverage hooks honor same settings         // 配置直接进入 automation 生命周期
```

关键源码 [1]：`UnLua` 的测试环境框架是统一的，但具体策略主要落在 fixture / 用例体内

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Public/UnLuaTestCommon.h
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/UnLuaTestCommon.cpp
// 位置: harness 只定义通用 world/map 分支，具体策略由 GetMapName()/SetUp()/Issue 文件继续填充
// ============================================================================
virtual bool InstantTest() { return false; }
virtual bool SetUp();
virtual FString GetMapName() { return ""; }                           // ★ 默认没有全局地图策略，交给具体测试决定

bool FUnLuaTestBase::SetUp()
{
    UnLua::Startup();
    L = UnLua::GetState();

    GameInstance = NewObject<UGameInstance>(GEngine);
    GameInstance->InitializeStandalone();
    WorldContext = GameInstance->GetWorldContext();

    const auto& MapName = GetMapName();
    if (MapName.IsEmpty())
    {
        const auto World = UWorld::CreateWorld(EWorldType::Game, false, "UnLuaTest"); // ★ 没地图时临时造世界
        World->SetGameInstance(GameInstance);
        WorldContext->SetCurrentWorld(World);
    }
    else if (InstantTest())
    {
        GEngine->LoadMap(*WorldContext, FURL(*MapName), nullptr, Error);               // ★ instant test 直接切图
    }
    else
    {
        AutomationOpenMap(MapName);                                                    // ★ latent test 走 UE 的 map harness
    }

    return true;
}
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Tests/Issue295Test.cpp
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Specs/LuaDeadLoopCheck.spec.cpp
// 位置: 具体回归策略与设置修改直接写进单个测试文件
// ============================================================================
bool FIssue295Test::RunTest(const FString& Parameters)
{
    const auto MapName1 = TEXT("/UnLuaTestSuite/Tests/Regression/Issue295/Issue295_1");
    const auto MapName2 = TEXT("/UnLuaTestSuite/Tests/Regression/Issue295/Issue295_2");

    ADD_LATENT_AUTOMATION_COMMAND(FOpenMapLatentCommand(MapName1))      // ★ 哪张图、切几次图，直接写死在 issue 文件里
    ...
    ADD_LATENT_AUTOMATION_COMMAND(FOpenMapLatentCommand(MapName2))
    ...
}

void FLuaDeadLoopCheckSpec::Define()
{
    Describe(TEXT("DeadLoopCheck"), [this]()
    {
        It(TEXT("设置防止无限循环的超时时间"), EAsyncExecution::TaskGraphMainThread, [this]()
        {
            auto& Settings = *GetMutableDefault<UUnLuaSettings>();
            Settings.DeadLoopCheck = 1;                                 // ★ 设置测试直接在 spec 体内改运行时设置
            ...
        });
    });
}
```

关键源码 [2]：`Angelscript` 把测试政策提升为全局 `DeveloperSettings`，并统一接入执行框架

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Testing/AngelscriptTestSettings.h
// 位置: 测试预算、命名约束、覆盖率、网络仿真统一建模为 DeveloperSettings
// ============================================================================
UCLASS(config = EditorPerProjectUserSettings, meta = (DisplayName = "Angelscript Test User Settings"))
class UAngelscriptTestUserSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, config, Category = UnitTests)
    bool bRunUnitTestsOnHotReload = true;

    UPROPERTY(EditAnywhere, config, Category = UnitTests)
    int LimitNModulesToTestOnHotReload = 0;
};

UCLASS(config=Editor, defaultconfig, meta=(DisplayName="Angelscript Test Settings"))
class UAngelscriptTestSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, config, Category = IntegrationTests)
    FString IntegrationTestMapRoot;

    UPROPERTY(EditAnywhere, config, Category = CodeCoverage, Meta = (ConfigRestartRequired = true))
    bool bEnableCodeCoverage;

    UPROPERTY(EditAnywhere, config, Category = Debugging)
    bool bEnableDebugBreaksInTests;

    UPROPERTY(EditAnywhere, config, Category = IntegrationTests)
    FString IntegrationTestNamingConvention;

    UPROPERTY(EditAnywhere, config, Category = Tests)
    bool bEnableNetworkEmulation;
    ...
};
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Testing/IntegrationTest.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/AngelscriptCodeCoverage.cpp
// 位置: 同一套测试设置会统一施加到 PIE/network/debug/coverage 生命周期
// ============================================================================
const UAngelscriptTestSettings* TestSettings = GetDefault<UAngelscriptTestSettings>();
PlayInSettings->NetworkEmulationSettings.bIsNetworkEmulationEnabled = TestSettings->bEnableNetworkEmulation;
PlayInSettings->NetworkEmulationSettings.InPackets.MinLatency = TestSettings->InPacketsMinLatency;
PlayInSettings->NetworkEmulationSettings.OutPackets.PacketLossPercentage = TestSettings->OutPacketsPacketLossPercentage; // ★ 网络仿真来自全局测试设置

if (!GetDefault<UAngelscriptTestSettings>()->bEnableDebugBreaksInTests)
{
    AngelscriptDisableDebugBreaks();                                                   // ★ 断点策略也来自同一设置面
}

FString NamingConvention = GetDefault<UAngelscriptTestSettings>()->IntegrationTestNamingConvention;
FString MapRoot = GetDefault<UAngelscriptTestSettings>()->IntegrationTestMapRoot;
*OutMapObjectPath = MapRoot + OutTestFuncDesc->Function->GetName();                    // ★ map naming contract 统一由设置给出

AutomationController->OnTestsAvailable().AddRaw(this, &FAngelscriptCodeCoverage::OnTestsStarting);
AutomationController->OnTestsComplete().AddRaw(this, &FAngelscriptCodeCoverage::OnTestsStopping);
return (GetDefault<UAngelscriptTestSettings>()->bEnableCodeCoverage ||
        FParse::Param(FCommandLine::Get(), TEXT("as-enable-code-coverage")));          // ★ coverage 同时支持 config 和 CI 命令行
```

新增对比结论：

- `UnLua` 在这里不是 `没有测试策略`；它的优势恰恰是灵活，issue 回归文件可以直接把地图、蓝图、Lua chunk 和 GC 时机写成一段现场剧本。
- 如果比较“是否有与 `UAngelscriptTestSettings` 同层的全局测试政策对象”，相对 `Angelscript` 当前快照，`UnLua` 属于 `没有实现`。
- 两边整体属于 `实现方式不同`：`UnLua` 把环境编排留在测试作者手里，便于快速复现线上问题；`Angelscript` 把测试预算、命名、覆盖率和网络条件前移成统一配置，更利于 CI / 工具 / 大批量回归治理。
- 对 `Angelscript` 可吸收的点不是照搬 `UnLua` 的“策略散落”，而是保留 issue 文件的现场剧本表达力；对 `UnLua` 可吸收的点则是把高频政策抽成全局 settings，降低回归入口分散度。

### [维度 D10] 设置文档的单一事实来源：`UnLua` 把文档、菜单、Project Settings 和编译宏绑在同一组键上，`Angelscript` 更偏“源码设置类 + Project Settings + runtime dump”

前文已经把 `UnLua` 的教程、workspace、脚手架和 `GetModuleName` 主线讲过了。这一轮只补一件此前没拆开的事：**设置本身是不是被组织成一份“可执行手册”**。

`UnLua` 当前快照里，这条链是闭合的。`Reference/UnLua/Docs/CN/Settings.md:1-115` 先把“运行时设置 / 编辑器设置”按用户语言展开；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaSettings.h:31-56` 与 `.../UnLuaEditor/Private/UnLuaEditorSettings.h:45-87` 定义了对应字段；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaModule.cpp:238-249` 和 `.../UnLuaEditor/Private/UnLuaEditorModule.cpp:114-123` 把 Runtime / Editor 两套设置页注册进 `Project -> Plugins`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:40-49,123-135` 又把工具栏“Settings -> Runtime / Editor”直接连到这两个页面；最后 `Reference/UnLua/Plugins/UnLua/Source/UnLua/UnLua.Build.cs:72-111` 再从同一个 `/Script/UnLuaEditor.UnLuaEditorSettings` section 读 `bEnableDebug`、`HotReloadMode`、`LuaVersion` 等键生成编译宏。也就是说，**文档、菜单、Project Settings 和 build-time 行为共用同一组词汇表**。

`Angelscript` 当前快照也不是没有设置面。`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:384-393` 把 `UAngelscriptSettings` 注册进 `Project -> Plugins -> Angelscript`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h:41-130` 定义了 compiler / import / bind / Blueprint 默认行为等大量字段；`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp:371-403` 还能把 `RuntimeConfig` 导成 `RuntimeConfig.csv`。从这些源码**可以推断**：`Angelscript` 当前设置体系更强调“源码是真正的 source-of-truth，必要时再由 dump / test / 工具去观测它”，而不像 `UnLua` 那样先给终端用户一份集中式设置手册。

```
[D10-Deep] Settings Documentation Contract
UnLua
├─ Docs/CN/Settings.md                           // 先用用户语言解释每个选项
├─ Toolbar -> Settings -> Runtime/Editor         // 编辑器可直达对应设置页
├─ UUnLuaSettings / UUnLuaEditorSettings         // Project Settings 字段定义
└─ UnLua.Build.cs reads same config section      // 编译宏与文档同名

Angelscript
├─ UAngelscriptSettings                          // Project Settings 字段定义
├─ Editor registers one settings page            // 源码里有统一入口
└─ DumpRuntimeConfig() -> RuntimeConfig.csv      // 更强调配置观测与导出
```

关键源码 [1]：`UnLua` 的设置文档直接对应实际设置项和用户入口

```md
<!-- =========================================================================
文件: Reference/UnLua/Docs/CN/Settings.md
位置: 文档不是泛泛介绍，而是按 Runtime / Editor 两页真实设置项逐条解释
============================================================================ -->
# 设置

通过 `UnLua工具栏->设置` 打开，或者从 `项目设置-> 插件` 界面找到对应的选项卡。

## 一、运行时设置
### 启动模块名称
### 无限循环检测
### Lua环境分配器
### Lua模块定位器
### 预绑定类型列表

## 二、编辑器设置
### 热重载模式
- 自动：在代码发生变更时立即重载
- 手动：通过快捷键 `Alt+L` 或工具栏 `热重载` 菜单选项触发热重载
- 永不：禁用热重载机制

### 启用UnLua调试代码
### 启用Insights分析支持
### 启用类型检查
### 自定义Lua版本
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaModule.cpp
// 位置: 文档中的 Runtime / Editor 设置页都有真实菜单入口和 Project Settings 注册点
// ============================================================================
if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
    SettingsModule->ShowViewer("Project", "Plugins", "UnLua");            // ★ 工具栏可直达 runtime 设置页

if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
    SettingsModule->ShowViewer("Project", "Plugins", "UnLua Editor");     // ★ 工具栏可直达 editor 设置页

SubMenuBuilder.AddMenuEntry(Commands.OpenRuntimeSettings, NAME_None, LOCTEXT("OpenRuntimeSettings", "Runtime"));
SubMenuBuilder.AddMenuEntry(Commands.OpenEditorSettings, NAME_None, LOCTEXT("OpenEditorSettings", "Editor"));

const TSharedPtr<ISettingsSection> Section = SettingsModule->RegisterSettings(
    "Project", "Plugins", "UnLua Editor",
    LOCTEXT("UnLuaEditorSettingsName", "UnLua Editor"),
    LOCTEXT("UnLuaEditorSettingsDescription", "UnLua Editor Settings"),
    GetMutableDefault<UUnLuaEditorSettings>());                           // ★ editor 设置页注册

const auto Section = SettingsModule->RegisterSettings(
    "Project", "Plugins", "UnLua",
    LOCTEXT("UnLuaEditorSettingsName", "UnLua"),
    LOCTEXT("UnLuaEditorSettingsDescription", "UnLua Runtime Settings"),
    GetMutableDefault<UUnLuaSettings>());                                 // ★ runtime 设置页注册
```

```csharp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/UnLua.Build.cs
// 位置: build-time 宏直接读取与 UI / 文档同名的 UnLuaEditorSettings 配置键
// ============================================================================
const string section = "/Script/UnLuaEditor.UnLuaEditorSettings";

loadBoolConfig("bAutoStartup", "AUTO_UNLUA_STARTUP", true);
loadBoolConfig("bEnableDebug", "UNLUA_ENABLE_DEBUG", false);
loadBoolConfig("bEnableTypeChecking", "ENABLE_TYPE_CHECK", true);
loadBoolConfig("bEnableUnrealInsights", "ENABLE_UNREAL_INSIGHTS", false);
loadStringConfig("LuaVersion", "UNLUA_LUA_VERSION", "lua-5.4.3");

string hotReloadMode;
if (!config.GetString(section, "HotReloadMode", out hotReloadMode))
    hotReloadMode = "Manual";

var withHotReload = hotReloadMode != "Never";
PublicDefinitions.Add("UNLUA_WITH_HOT_RELOAD=" + (withHotReload ? "1" : "0"));  // ★ 文档里的 HotReloadMode 直接决定编译宏
```

关键源码 [2]：`Angelscript` 当前更强调“源码设置类 + Project Settings + dump 可观测性”

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp
// 位置: 设置体系很完整，但主要交付形态是源码定义与运行时导出
// ============================================================================
SettingsModule->RegisterSettings(
    "Project", "Plugins", "Angelscript",
    NSLOCTEXT("Angelscript", "AngelscriptSettingsTitle", "Angelscript"),
    NSLOCTEXT("Angelscript", "AngelscriptSettingsDescription", "Configuration for behavior of the angelscript compiler and script engine."),
    GetMutableDefault<UAngelscriptSettings>());                           // ★ 当前统一入口是一个 Project Settings 页面

UPROPERTY(Config, EditDefaultsOnly, Category = "Angelscript", Meta = (ConfigRestartRequired = true))
bool bAllowImplicitPropertyAccessors = true;

UPROPERTY(Config, EditDefaultsOnly, Category = "Backwards Compatibility", Meta = (ConfigRestartRequired = true))
bool bAutomaticImports = true;

UPROPERTY(Config, EditDefaultsOnly, Category = "Angelscript", Meta = (ConfigRestartRequired = true))
bool bDefaultFunctionBlueprintCallable = true;                            // ★ 设置项首先以源码字段存在

AddConfigValue(TEXT("bDevelopmentMode"), BoolToString(Config.bDevelopmentMode));
AddConfigValue(TEXT("bIgnorePrecompiledData"), BoolToString(Config.bIgnorePrecompiledData));
AddConfigValue(TEXT("bDumpDocumentation"), BoolToString(Config.bDumpDocumentation));
AddConfigValue(TEXT("DebugServerPort"), LexToString(Config.DebugServerPort));
AddConfigValue(TEXT("DisabledBindNames"), JoinNames(Config.DisabledBindNames));
return SaveTable(OutputDir, TEXT("RuntimeConfig.csv"), Writer);           // ★ 当前仓内还提供统一的 runtime config 导出面
```

新增对比结论：

- `UnLua` 在这里不是单纯“有很多文档”；它把 `Settings.md`、工具栏菜单、Project Settings 页面和 build-time 配置键做成了一组统一词汇表。
- `Angelscript` 在这里也不是 `没有设置面`；相反，`UAngelscriptSettings` 很完整，`Project Settings` 入口和 `RuntimeConfig.csv` 观测面也都存在。
- 两边整体属于 `实现方式不同`：`UnLua` 更偏用户导向的集中设置手册；`Angelscript` 更偏源码定义与可观测性优先。后一句是基于当前源码与文档交付形态的推断。
- 如果当前优先级真的是“onboarding assets & workflow entry points”，`UnLua` 这条经验最值得吸收的不是增加更多设置项，而是把**已有设置项组织成一份终端用户能顺着走的单页手册**。

---

## 深化分析 (2026-04-09 08:09:13)

### [维度 D3] `WorldContext` 语义的 ownership：`UnLua` 更像“调用点显式传 `self/world`”，`Angelscript` 把它做成签名与 VM 的隐藏契约

前几轮已经把 `UnLuaInterface`、`GetModuleName()` 和 `BlueprintOverride / Mixin` 的主路径讲清了。这一轮补一个更底层但很影响 authoring 体验的点：**脚本作者到底要不要自己显式携带 `WorldContext`**。

从当前 `UnLua` 快照看，`WorldContext` 更像调用点责任。实际脚本里，`UKismetSystemLibrary.LineTraceSingle`、`UWidgetBlueprintLibrary.Create` 这类 UE API 都是把 `self` 当第一参数显式传进去；运行时桥 `FFunctionDesc::PreCall()` 确实会自动补 `latent`、默认参数、`out/ref`，但当前可见分支里没有与 `WorldContext` metadata 对位的隐藏参数处理。这里关于 “`UnLua` 没有仓内 hidden world-context pipeline” 的判断，是基于 `FunctionDesc::PreCall()` 的分支集合与真实脚本调用方式做出的推断。

`Angelscript` 则把这层语义前移进签名与 VM。`Helper_FunctionSignature.h` 在看到 `WorldContext` pin metadata 时，直接把默认值改写成 `__WorldContext()`；`ASClass.cpp` 执行函数时会把这个隐藏参数写回 ambient world context；底层 `as_context.cpp` 甚至会在 `BlueprintThreadSafe` 或无效世界上下文场景下直接抛 internal exception。也就是说，`Angelscript` 不是只提供了一个方便函数，而是把 `WorldContext` 变成了**调试器、运行时和异常路径都认识的正式 contract**。

```
[D3-Deep] WorldContext Ownership
UnLua
├─ Lua call site passes self/world explicitly         // 调用点自己带上下文
├─ FFunctionDesc::PreCall() fills latent/default/out  // 桥接层主要补调用礼仪
└─ UE native function resolves world from argument    // WorldContext 仍落回原生 API

Angelscript
├─ UFunction metadata -> "__WorldContext()"          // 构建签名时隐藏参数默认值
├─ ASClass assigns ambient world context              // VM 执行前写入上下文
└─ invalid context -> internal exception              // 运行时直接守住错误路径
```

关键源码 [1]：`UnLua` 的实际用法与调用桥都把 `WorldContext` 留在调用点/UE 原生函数侧

```lua
-- ============================================================================
-- 文件: Reference/UnLua/Content/Script/Weapon/BP_WeaponBase_C.lua
-- 函数: M:Fire / M:GetFireInfo
-- 行号: 79-92
-- 位置: 真实脚本直接把 self 作为第一参数传给需要 WorldContext 的 UE API
-- ============================================================================
local bResult = UE.UKismetSystemLibrary.LineTraceSingle(
    self, Start, End, UE.ETraceTypeQuery.Weapon, false, nil, UE.EDrawDebugTrace.None, nil, true)
-- ★ WorldContext 没有被隐藏；脚本作者显式传 self

local bResult = UE.UKismetSystemLibrary.LineTraceSingle(
    self, TraceStart, TraceEnd, UE.ETraceTypeQuery.Weapon, false, nil, UE.EDrawDebugTrace.None, HitResult, true)
-- ★ 第二个调用同样显式携带 self
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp
// 函数: FFunctionDesc::PreCall
// 行号: 279-345
// 位置: 当前可见特殊分支只有 latent / return / default / out-ref，没有 WorldContext 专门分支
// ============================================================================
for (int32 i = 0; i < Properties.Num(); ++i)
{
    const auto& Property = Properties[i];
    Property->InitializeValue(Params);
    if (i == LatentPropertyIndex)
    {
        FLatentActionInfo LatentActionInfo(...);
        Property->CopyValue(ContainerPtr, &LatentActionInfo);   // ★ 自动补 latent
        continue;
    }
    if (i == ReturnPropertyIndex)
    {
        CleanupFlags[i] = ParamIndex >= NumParams || !Property->CopyBack(L, FirstParamIndex + ParamIndex, Params);
        continue;
    }
    if (ParamIndex < NumParams)
    {
        CleanupFlags[i] = Property->WriteValue_InContainer(L, Params, FirstParamIndex + ParamIndex, false);
    }
    else if (!Property->IsOutParameter())
    {
        IParamValue **DefaultValue = DefaultParams->Parameters.Find(Property->GetProperty()->GetFName());
        if (DefaultValue)
            Property->CopyValue(Params, (*DefaultValue)->GetValue());      // ★ 自动补默认参数
    }
}
```

关键源码 [2]：`Angelscript` 把 `WorldContext` 变成隐藏参数、ambient state 和异常守卫的一体化 contract

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_context.cpp
// 位置: 构建签名时写入 "__WorldContext()"，执行时分配 ambient context，非法使用时直接报错
// ============================================================================
const FString& WorldContextParam = Function->GetMetaData(NAME_Signature_WorldContext);
if (WorldContextParam.Len() != 0)
{
    if (ArgumentNames[ArgIndex] == WorldContextParam)
    {
        ArgumentDefaults[ArgIndex] = TEXT("__WorldContext()"); // ★ 签名层隐藏默认值
        WorldContextArgument = ArgIndex;
    }
}

if (ASFunction->bIsWorldContextGenerated)
{
    P_GET_OBJECT(UObject, WorldContext);
    PrevWorldContext = (UObject*)FAngelscriptEngine::GetAmbientWorldContext();
    FAngelscriptEngine::AssignWorldContext(WorldContext);       // ★ 执行前把上下文写进 ambient state
    AS_ENSURE(WorldContext != nullptr, TEXT("Null WorldContext passed into static function call '%s'"), *ASFunction->GetName());
}

if (!GIsAngelscriptWorldContextAvailable && FAngelscriptEngine::Get().ConfigSettings->bErrorWhenUsingInvalidWorldContext)
{
    SetInternalException("Calling a function that requires WorldContext, but the current object is not in a world.");
    return 0;                                                   // ★ 非法上下文直接抛异常
}
```

新增对比结论：

- `UnLua` 在这里不是 `没有支持 WorldContext`；它的做法是把上下文继续留在调用点或原生函数签名里，由脚本显式传 `self/world`。
- 相对 `Angelscript` 当前这条 `__WorldContext() -> ambient context -> invalid-context guard` 管道，`UnLua` 属于 `实现方式不同`；若比较“隐藏参数 + 统一异常守卫”的同层能力，则当前快照里 `UnLua` 可视为 `没有实现`。
- `Angelscript` 的收益是脚本作者少写一层样板、调试器与运行时能共享同一套上下文语义；代价是 runtime/编译器/调试器都要维护这条契约。

### [维度 D5] 自动求值的安全边界：`UnLua` 仓内调试 API 停在快照读取，`Angelscript` 把 accessor 自动执行做成受黑名单与 world-context 保护的 runtime 能力

前几轮已经把“谁拥有调试 transport”和“谁拥有变量树”拆开了。这一轮补的是更细的一层：**调试器在停住以后，能不能安全地帮用户自动执行一个 accessor / 无参函数来取值**。

`UnLua` 当前仓内公开面相对克制。`UnLuaDebugBase.h` 暴露的是 `FLuaDebugValue`、`GetStackVariables()`、`GetLuaCallStack()` 这类快照读取 API；它可以把 Lua/UE 混合值展开出来，但当前可见 public API 没有与 “evaluate this expression/function” 对位的仓内入口。也就是说，`UnLua` 当前快照把“取当前值”这件事放在仓内，把“要不要执行函数做自动求值”留给外部调试器生态。

`Angelscript` 当前则把自动求值做成 runtime 里的正式能力。`GetDebuggerValueFromFunction()` 会先限制只允许对象零参函数或只有隐藏 `__WorldContext()` 的全局函数，再查两套黑名单：无条件禁用，以及“没有有效 `WorldContext` 时禁用”；通过检查后才真正执行脚本函数并把返回值包装成 `FDebuggerValue`。这意味着 `Angelscript` 调试器不仅“能看变量”，还明确拥有**哪些函数可以被自动调用**的仓内策略面。

```
[D5-Deep] Automatic Evaluation Guardrails
UnLua
├─ lua_State -> GetStackVariables() / GetLuaCallStack()  // 仓内提供快照读取
├─ FLuaDebugValue builds readable tree                   // 展开当前值
└─ whether to evaluate code is external-tool concern     // 仓内未见同层 evaluate contract

Angelscript
├─ RequestEvaluate / debugger path                       // 协议层允许按路径取值
├─ GetDebuggerValueFromFunction()                        // 只放行安全 accessor
├─ blacklist + world-context gate                        // 配置层限制自动执行
└─ execute accessor -> FDebuggerValue                    // 通过后才真正求值
```

关键源码 [1]：`UnLua` 当前仓内调试公开面是“快照读取 API”，不是仓内自动求值器

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDebugBase.h
// 位置: public API 暴露的是调试值树与栈变量快照
// ============================================================================
struct UNLUA_API FLuaDebugValue
{
    FString ReadableValue;
    FString Type;
    TArray<FLuaDebugValue> Keys;
    TArray<FLuaDebugValue> Values;              // ★ 这是“当前值快照树”
};

UNLUA_API bool GetStackVariables(
    lua_State *L,
    int32 StackLevel,
    TArray<FLuaVariable> &LocalVariables,
    TArray<FLuaVariable> &Upvalues,
    int32 Level = MAX_int32);                   // ★ 公开接口是 locals/upvalues 读取

UNLUA_API FString GetLuaCallStack(lua_State *L); // ★ 另一条公开接口是调用栈文本
```

关键源码 [2]：`Angelscript` 自动求值在 runtime 内部带着黑名单和 `WorldContext` 守卫

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp
// 位置: 自动求值先过配置黑名单与上下文检查，再真正执行 accessor
// ============================================================================
UPROPERTY(Config)
TSet<FString> DebuggerBlacklistAutomaticFunctionEvaluation;

UPROPERTY(Config)
TSet<FString> DebuggerBlacklistAutomaticFunctionEvaluationWithoutWorldContext = {
    "AActor.GetWorldTimerManager",
    "AActor.GetGameInstance",
    "AActor.GetPhysicsVolume",
    "AActor.GetActorTimeDilation",
};                                                                  // ★ 没有有效 world 时，这些 accessor 一律不自动执行

if (Object == nullptr)
{
    if (ScriptFunction->GetParamCount() == 1)
    {
        if (ScriptFunction->hiddenArgumentIndex != 0 || ScriptFunction->hiddenArgumentDefault != "__WorldContext()")
            return false;                                            // ★ 只放行隐藏 WorldContext 的零成本函数
        else
            bHasWorldContext = true;
    }
}

if (Config->DebuggerBlacklistAutomaticFunctionEvaluation.Contains(FunctionPath))
    return false;

if (Object == nullptr || ((ScriptFunction->GetObjectType()->GetFlags() & asOBJ_REF) == 0) || ((UObject*)Object)->GetWorld() == nullptr)
{
    if (Config->DebuggerBlacklistAutomaticFunctionEvaluationWithoutWorldContext.Contains(FunctionPath))
        return false;                                                // ★ 无世界上下文时再套一层黑名单
}

Context->Execute();                                                  // ★ 只有通过守卫后才真正执行函数求值
```

新增对比结论：

- `UnLua` 在这里不是 `没有调试能力`；它的仓内能力已经能稳定给出变量树和调用栈。
- 如果比较“仓内是否存在与 `GetDebuggerValueFromFunction()` 对等的自动 accessor 求值 contract”，相对 `Angelscript` 当前快照，`UnLua` 属于 `没有实现`。
- `Angelscript` 的额外复杂度也是真实存在的：一旦把自动求值拉进 runtime，就必须长期维护黑名单、`WorldContext` 规则和异常路径，否则调试器本身会变成副作用入口。

### [维度 D10] IntelliSense 语义表面的 ownership：`UnLua` 主动把 C++/UE 类型翻译成 Lua 读者语言，`Angelscript` 更坚持保留脚本声明原貌

前几轮已经把 `README -> Docs -> Tutorials -> IntelliSense` 这条交付链讲过了。这一轮只补一个此前没展开的细节：**最终落到 IDE 里的那一行声明，究竟是在翻译用户语言，还是在保留引擎/脚本语言本貌**。

`UnLua` 的选择明显偏“翻译”。`UnLuaEx.inl` 里有一整层 `TTypeIntelliSense` 模板，把 `int32` 统一映成 `integer`，把 `float/double` 映成 `number`，把 `void*` 映成 `lightuserdata`，再由 `GenerateArgsIntelliSense()` 产出 `---@param / @return` 和 `function _G.Name(P0)` 这种 Lua IDE 直接能吃的表面。也就是说，`UnLua` 的 IntelliSense 不是简单 dump UE 反射声明，而是做了一次**面向 Lua 使用者的语义重写**。

`Angelscript` 的方向相反。`FAngelscriptType::BuildFunctionDeclaration()` 直接按 `GetAngelscriptDeclaration()` 拼接 `ReturnType FunctionName(Args...)`，`Helper_FunctionSignature.h` 再把这个 declaration 存进绑定产物。它当然也会把 Unreal 默认值转换成 AngelScript 默认值，但目标始终是**保持最终字符串仍然是 AngelScript 语言本身的 declaration**，而不是再翻译成另一套 IDE 友好术语。

```
[D10-Deep] Authoring Surface Language
UnLua
├─ C++ / UE type -> TTypeIntelliSense                // 先翻译术语
├─ GenerateArgsIntelliSense()                        // 产出 ---@param / ---@return
└─ function _G.Name(P0) end                          // IDE 最终读到 Lua 语法

Angelscript
├─ FProperty/UFunction -> FAngelscriptTypeUsage      // 先得到脚本侧类型
├─ BuildFunctionDeclaration()                        // 直接拼 AngelScript declaration
└─ declaration stays close to compiler/runtime       // 不再额外翻译为另一套术语
```

关键源码 [1]：`UnLua` IntelliSense 在生成阶段就把类型语言翻译成 Lua 读者表面

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaEx.inl
// 位置: IntelliSense 生成不是直接复刻 C++ 声明，而是先做类型术语翻译
// ============================================================================
template <> struct TTypeIntelliSense<void*>   { static FString GetName() { return TEXT("lightuserdata"); } };
template <> struct TTypeIntelliSense<int32>   { static FString GetName() { return TEXT("integer"); } };
template <> struct TTypeIntelliSense<float>   { static FString GetName() { return TEXT("number"); } };
template <> struct TTypeIntelliSense<bool>    { static FString GetName() { return TEXT("boolean"); } };
template <> struct TTypeIntelliSense<FString> { static FString GetName() { return TEXT("string"); } };
// ★ UE/C++ 类型先被归一成 Lua IDE 术语

FString ReturnTypeName = TTypeIntelliSense<...>::GetName();
Buffer += FString::Printf(TEXT("---@return %s\r\n"), *ReturnTypeName);  // ★ 输出 EmmyLua 风格注解

Buffer += FString::Printf(TEXT("function _G.%s(%s) end\r\n\r\n"), *Name, *ArgList);
// ★ 最终交付给 IDE 的是 Lua 函数壳，不是 C++/UE 原样声明
```

关键源码 [2]：`Angelscript` 更坚持保留 AngelScript declaration 原貌

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h
// 位置: 绑定产物里的声明仍然是 AngelScript 语言本身的 declaration
// ============================================================================
if (ReturnType.IsValid())
    Declaration = ReturnType.GetAngelscriptDeclaration(FAngelscriptType::EAngelscriptDeclarationMode::FunctionReturnValue);
else
    Declaration = TEXT("void");
Declaration += TEXT(" ");
Declaration += FunctionName;
Declaration += TEXT("(");
Declaration += ArgumentTypes[i].GetAngelscriptDeclaration(FAngelscriptType::EAngelscriptDeclarationMode::FunctionArgument);
Declaration += TEXT(" ");
Declaration += ArgumentNames[i];
// ★ 这里做的是“Unreal 默认值 -> AngelScript 默认值”转换，不是再翻译成另一套 IDE 术语

Declaration = FAngelscriptType::BuildFunctionDeclaration(
    ReturnType, ScriptName, ArgumentTypes, ArgumentNames, ArgumentDefaults,
    (Function->HasAnyFunctionFlags(FUNC_Const) && !bStaticInScript) || bForceConst);
// ★ 产物继续沿用 AngelScript declaration 作为权威表面
```

新增对比结论：

- `UnLua` 在 `D10` 的一个新增认知点，不只是“有 IntelliSense 产物”，而是它连术语都替用户翻译过了；这解释了为什么它的 onboarding 读起来更像 Lua 工作流，而不是 UE 反射工作流。
- `Angelscript` 在这里不是 `没有 IDE/文档产物`；它更像是 `实现方式不同`，优先保持 declaration 与编译器/runtime 一致，减少“文档语言”和“真实脚本语言”之间的二义性。
- 如果 `Angelscript` 后续要吸收 `UnLua` 经验，更合适的方向不是替换现有 declaration，而是在其外层增加一层面向新手的术语映射或 glossaries；否则会削弱当前强类型脚本表面的精确性。

---

## 深化分析 (2026-04-09 08:19:17)

### [维度 D2] 脚本表面命名权：`UnLua` 大体沿用 UE reflection 原名，`Angelscript` 把 rename / namespace 做成正式绑定层

前几轮已经把 `UnLua` 和 `Angelscript` 的“绑定方式”讲清了，但还没把**最终脚本 API 名字到底由谁决定**单独拆出来。当前源码里，`UnLua` 的主路径更像“先信任 UE reflection 的原名，再做少量运行时补偿”。`FFunctionDesc` 构造时直接把 `InFunction->GetName()` 写进 `FuncName`，只有 `FUNC_Net` 会额外暴露成 `*_RPC`；`FClassDesc::RegisterField()` 也是先拿 `FieldName` 直接做 `FindPropertyByName()/FindFunctionByName()`，只有蓝图脚本结构体那类带随机后缀的场景，才做一次 `DisplayName` 清洗。这意味着 `UnLua` 的脚本表面命名权，多数时候仍属于 UE 自身的反射名字。

`Angelscript` 则明显把命名权上移到了绑定层。`Helper_FunctionSignature.h` 先看 `ScriptName` metadata；没有显式别名时，再统一剥掉 `K2_ / BP_ / AS_ / Received_ / Receive` 这类 UE authoring 前缀，并在剥前缀后做一次冲突检测，避免把两个不同原生函数压成同一个脚本名。类库 namespace 也不是简单照抄 `UClass` 名字，而是允许用类级 `ScriptName` 或配置化 prefix/suffix strip 生成脚本 namespace。也就是说，`Angelscript` 的脚本 API 命名不是 reflection 的自然副产品，而是 bind policy 的正式产物。

```
[D2-Deep] Script Surface Naming Ownership
UnLua
├─ UFunction::GetName() -> FuncName                // 原生函数名直接进入描述符
├─ FUNC_Net => Name_RPC                            // 只对网络函数做少量改写
├─ FindPropertyByName / FindFunctionByName         // 字段查找优先沿用 reflection 名
└─ script surface stays close to UE symbols        // 脚本作者更多面对 UE 原名

Angelscript
├─ metadata ScriptName -> primary alias            // 先尊重显式脚本别名
├─ strip K2_/BP_/AS_/Receive* prefixes             // 再做规则化重写
├─ collision check before finalizing name          // 防止别名冲突
└─ namespace rewrite for library classes           // 类库名字也可被正式治理
```

关键源码 [1]：`UnLua` 的函数名和字段查找大体沿用 UE reflection 原名

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp
// 行号: 38-44
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/ClassDesc.cpp
// 行号: 69-70, 90
// 位置: 函数描述符和字段注册优先沿用原生 reflection 名，只在少数场景做补偿
// ============================================================================
FuncName = InFunction->GetName();

if (InFunction->HasAnyFunctionFlags(FUNC_Net))
    LuaFunctionName = MakeUnique<FTCHARToUTF8>(*FString::Printf(TEXT("%s_RPC"), *FuncName));
else
    LuaFunctionName = MakeUnique<FTCHARToUTF8>(*FuncName); // ★ 非网络函数直接暴露原名

FProperty* Property = Struct->FindPropertyByName(FieldName);
UFunction* Function = (!Property && bIsClass) ? AsClass()->FindFunctionByName(FieldName) : nullptr;
// ★ 字段查找先按 reflection 名直接命中

if (DisplayName == FieldNameStr)
{
    Property = *PropertyIt; // ★ 只有脚本结构体后缀清洗场景才补一次“显示名对齐”
    break;
}
```

关键源码 [2]：`Angelscript` 把函数别名、前后缀剥离和 namespace 改写做成统一规则

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h
// 行号: 88-115, 128-157
// 位置: 脚本名先看 metadata，再走 prefix/suffix strip 与冲突检查
// ============================================================================
FString OutScriptName = InFunction->GetName();

if (InFunction->HasMetaData(NAME_Signature_ScriptName))
{
    OutScriptName = GetPrimaryScriptName(InFunction->GetMetaData(NAME_Signature_ScriptName));
}
else
{
    bChangedName |= OutScriptName.RemoveFromStart(TEXT("K2_"));
    bChangedName |= OutScriptName.RemoveFromStart(TEXT("BP_"));
    bChangedName |= OutScriptName.RemoveFromStart(TEXT("AS_"));

    if (InFunction->HasAnyFunctionFlags(FUNC_BlueprintEvent))
    {
        bChangedName |= OutScriptName.RemoveFromStart(TEXT("Received_"));
        bChangedName |= OutScriptName.RemoveFromStart(TEXT("Receive"));
    }

    if (UFunction* ExistingFunction = OwningClass->FindFunctionByName(*OutScriptName))
    {
        OutScriptName = InFunction->GetName(); // ★ 剥前缀后若撞名，则退回原始函数名
    }
}

if (FAngelscriptEngine::bUseScriptNameForBlueprintLibraryNamespaces
    && InFunction->GetOuterUClass()->HasMetaData(NAME_Signature_ScriptName))
{
    Namespace = InFunction->GetOuterUClass()->GetMetaData(NAME_Signature_ScriptName);
}
else
{
    // ★ 否则再走 blueprint library namespace 的 prefix/suffix strip
    for(const auto& Prefix : FAngelscriptEngine::BlueprintLibraryNamespacePrefixesToStrip)
    {
        if(Namespace.RemoveFromStart(Prefix))
            break;
    }

    for(const auto& Suffix : FAngelscriptEngine::BlueprintLibraryNamespaceSuffixesToStrip)
    {
        if(Namespace.RemoveFromEnd(Suffix))
            break;
    }
}
```

新增对比结论：

- `UnLua` 在这里不是 `没有命名治理`；它已经会处理 `RPC` 后缀和少数结构体字段名清洗，但大方向仍是 `实现方式不同`，即让脚本表面尽量贴近 UE reflection 原名。
- 如果比较“是否存在与 `ScriptName + namespace rewrite + collision guard` 对等的统一命名层”，相对 `Angelscript` 当前快照，`UnLua` 属于 `没有实现同层显式命名 contract`。
- `Angelscript` 的收益是脚本表面更可打磨、更适合做长期 API 治理；代价是 metadata 规则、冲突判定和文档都必须围着这套命名层持续维护。

### [维度 D4] loader surface 与 auto-reload surface 的错位：`UnLua` 能从 custom loader 加载，但自动热重载只盯 `script_root_path` 文件时间戳

前几轮已经覆盖了 `UnLua` 的 merge/sandbox/reload 事务。这一轮补的是一个更细的边界：**所有“能被加载”的模块，是否也都天然“能被自动发现并热重载”**。从源码看，答案是否定的。`FLuaEnv` 启动时把 `LoadFromCustomLoader` 插在 `package.searchers` 的前面，再把 `LoadFromFileSystem` 放在后面；也就是说，一个模块完全可以来自 delegate / `CustomLoaders` 提供的字节流，而不是来自 `script_root_path` 下的实体文件。

但 `HotReload.lua` 的自动扫描并不追踪“模块来自哪个 searcher”，它只在第一次 `require()` 成功时记下 `loaded_module_times[module_name] = get_last_modified_time(module_name)`，而 `get_last_modified_time()` 只是把 `module_name` 拼成 `config.script_root_path + *.lua` 再去查文件时间。`M.reload()` 也只是把这张表重新扫一遍。由此可以做一个有源码支撑的推断：**对于不落在 `script_root_path` 物理文件上的 custom-loaded module，当前快照没有与其对等的自动变更发现 contract**；它们仍然可以被手动点名 `reload(module_names)`，但不会自动进入“扫时间戳即重载”的主路径。

`Angelscript` 当前在这点上更收敛。它的脚本来源被 `DiscoverScriptRoots()` 显式收窄到 project `Script/` 和 enabled plugin `Script/`；随后 directory watcher 只有在文件路径能相对到这些 root 时，才会把 `.as` 变更排进 reload 队列。两边都支持扩展，但 `Angelscript` 的 load surface 和 reload surface 更接近同一张文件身份表。

```
[D4-Deep] Load Surface vs Reload Surface
UnLua
├─ package.searchers[2] = custom loader            // 可以从外部 loader 注入字节流
├─ package.searchers[3] = file system loader       // 也可以走常规文件系统
├─ loaded_module_times tracks script_root_path     // 自动重载只记物理文件时间
└─ loadable module != always auto-reload-visible   // 两个 surface 不完全重合

Angelscript
├─ DiscoverScriptRoots() -> AllRootPaths           // 先定义合法脚本根
├─ watcher only accepts paths under roots          // 只有根下文件能进队列
├─ module records keep absolute/relative filename  // 编译与热重载共享文件身份
└─ load surface stays narrower but more aligned    // 来源更窄，但一致性更强
```

关键源码 [1]：`UnLua` 同时支持 custom loader 与 file system loader，但自动重载只看 `script_root_path` 文件时间

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp
// 行号: 98-100, 557-583
// 文件: Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua
// 行号: 114-118, 169, 604-617
// 位置: 运行时加载面允许 custom loader，但自动重载面只维护 script_root_path 文件时间
// ============================================================================
AddSearcher(LoadFromCustomLoader, 2);
AddSearcher(LoadFromFileSystem, 3);
AddSearcher(LoadFromBuiltinLibs, 4);
// ★ 模块解析优先级里，custom loader 先于 file system

if (Env.CustomLoaders.Num() == 0)
    return 0;

for (auto& Loader : Env.CustomLoaders)
{
    if (!Loader.Execute(Env, FileName, Data, ChunkName))
        continue; // ★ 这里允许模块来自外部 loader 提供的字节流
}
```

```lua
-- ============================================================================
-- 文件: Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua
-- 行号: 114-118, 169, 604-617
-- 位置: 自动重载记录的是 script_root_path 下的文件修改时间，而不是“实际命中的 searcher”
-- ============================================================================
local loaded_module_times = {}

local function get_last_modified_time(module_name)
    local filename = config.script_root_path .. module_name:gsub("%.", "/") .. ".lua"
    return UE.UUnLuaFunctionLibrary.GetFileLastModifiedTimestamp(filename) -- ★ 只查物理脚本根
end

loaded_module_times[module_name] = get_last_modified_time(module_name)

function M.reload(module_names)
    for module_name, time in pairs(loaded_module_times) do
        local current_time = get_last_modified_time(module_name)
        if current_time ~= time then
            modified_modules[#modified_modules + 1] = module_name
            loaded_module_times[module_name] = current_time
        end
    end
end
```

关键源码 [2]：`Angelscript` 把 load/reload 的合法来源统一约束到显式 script roots

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 行号: 558-563, 1334-1356
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp
// 行号: 12, 57-86
// 位置: 脚本根先被显式发现，随后 watcher 只接受根下 .as 文件进入 reload 队列
// ============================================================================
Dependencies.GetEnabledPluginScriptRoots = []()
{
    TArray<FString> ScriptRoots;
    for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetEnabledPluginsWithContent())
    {
        ScriptRoots.Add(Plugin->GetBaseDir() / TEXT("Script")); // ★ plugin 级 script root 明确可枚举
    }
    return ScriptRoots;
};

FString RootPath = Dependencies.ConvertRelativePathToFull(Dependencies.GetProjectDir() / TEXT("Script"));
for (const FString& PluginScriptRoot : Dependencies.GetEnabledPluginScriptRoots())
{
    const FString ScriptPath = Dependencies.ConvertRelativePathToFull(PluginScriptRoot);
    if (Dependencies.DirectoryExists(ScriptPath) && ScriptPath != RootPath)
    {
        DiscoveredRootPaths.Add(ScriptPath); // ★ load surface 先被收敛成根目录集合
    }
}

if (!TryMakeRelativeScriptPath(AbsolutePath, RootPaths, RelativePath))
{
    continue; // ★ 不在 script roots 下的文件变更，根本不会进入 reload 队列
}

if (AbsolutePath.EndsWith(TEXT(".as")))
{
    Engine.FileChangesDetectedForReload.AddUnique({ AbsolutePath, RelativePath });
}
```

新增对比结论：

- `UnLua` 在这里不是 `没有热重载`；它只是把 loader extensibility 放得比 auto-reload coverage 更宽。
- 若比较“能被运行时加载的模块是否天然共享同一套自动变更发现路径”，相对 `Angelscript` 当前基于 `script roots` 的做法，`UnLua` 对 custom loader 这层属于 `没有实现同层 auto-reload coverage`。这里是基于当前源码的推断，不是无证断言。
- 这不是简单的优劣关系。`UnLua` 赢在加载入口灵活，适合做教学和扩展 seam；`Angelscript` 赢在来源更单一，因此 watcher、impact scan、module record 更容易共享一致的文件身份。

### [维度 D11] 部署覆盖层的 ownership：`UnLua` 原生支持 `ProjectPersistentDownloadDir` 覆盖，`Angelscript` 当前更依赖显式 roots + precompiled artifacts

部署维度前几轮提过 `UFS staged content` 和 `cache/precompiled artifacts`，但还没把**“补丁脚本从哪里覆盖原始脚本”**这件事拆出来。`UnLua` 的 file system loader 在当前快照里给了一个非常直接的答案：同一套 `PackagePath` pattern，会先拼到 `ProjectPersistentDownloadDir()` 去找；找不到才回退到 `ProjectDir()`。这不是文档层面的推荐，而是运行时搜索顺序本身。也就是说，`UnLua` 的 loose-file patch seam 已经内建到 loader 里了。

`Angelscript` 则更强调“先枚举 roots，再决定是否走 precompiled data”。`DiscoverScriptRoots()` 把 project `Script/` 与 enabled plugin `Script/` 收成一组可枚举根目录；`AngelscriptAllScriptRootsCommandlet` 还能直接把这组 roots 导出来。另一方面，runtime config 明确区分 `bGeneratePrecompiledData / bIgnorePrecompiledData / bUsePrecompiledData`，并在非 editor、非 development、非 commandlet 场景下尝试走 precompiled data。这说明当前 `Angelscript` 的 packaged deployment 主轴更像“显式根目录 + 编译产物”，而不是“persistent loose-file override”。

```
[D11-Deep] Deployment Surface
UnLua
├─ PackagePath patterns                            // 同一套模块路径规则
├─ ProjectPersistentDownloadDir first              // 先查下载/补丁目录
├─ ProjectDir second                               // 再查工程内脚本
└─ loose-file overlay is built into loader         // 覆盖能力是运行时默认行为

Angelscript
├─ project Script/ + plugin Script/ roots          // 来源先被枚举成显式 roots
├─ roots exportable via commandlet                 // 机器可发现
├─ bGenerate/bUsePrecompiledData switches          // 打包期另有预编译产物通道
└─ packaged runtime favors explicit artifacts      // 更偏根目录+产物，而不是下载覆盖层
```

关键源码 [1]：`UnLua` 的 file system loader 先查 `ProjectPersistentDownloadDir`，后查 `ProjectDir`

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp
// 行号: 626-635
// 位置: 同一套 PackagePath pattern 先命中下载目录，再回退到工程目录
// ============================================================================
for (auto& Pattern : Patterns)
{
    Pattern.ReplaceInline(TEXT("?"), *FileName);
    const auto PathWithPersistentDir = FPaths::Combine(FPaths::ProjectPersistentDownloadDir(), Pattern);
    FullPath = FPaths::ConvertRelativePathToFull(PathWithPersistentDir);
    if (FFileHelper::LoadFileToArray(Data, *FullPath, FILEREAD_Silent))
        return LoadIt(); // ★ 下载目录里的 loose file 可以直接覆盖主包脚本
}

for (auto& Pattern : Patterns)
{
    const auto PathWithProjectDir = FPaths::Combine(FPaths::ProjectDir(), Pattern);
    FullPath = FPaths::ConvertRelativePathToFull(PathWithProjectDir);
    if (FFileHelper::LoadFileToArray(Data, *FullPath, FILEREAD_Silent))
        return LoadIt(); // ★ 打包/工程内脚本是第二优先级
}
```

关键源码 [2]：`Angelscript` 把部署来源做成显式 script roots，并区分 precompiled data 模式

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h
// 行号: 71-73, 544
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 行号: 558-563, 1334-1356, 1425-1433
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAllScriptRootsCommandlet.cpp
// 行号: 7-19
// 位置: roots 是显式可枚举对象，打包运行时再决定是否转向 precompiled data
// ============================================================================
bool bGeneratePrecompiledData = false;
bool bIgnorePrecompiledData = false;
bool bUsePrecompiledData = false; // ★ 部署形态被建模成明确配置项

for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetEnabledPluginsWithContent())
{
    ScriptRoots.Add(Plugin->GetBaseDir() / TEXT("Script"));
}

FString RootPath = Dependencies.ConvertRelativePathToFull(Dependencies.GetProjectDir() / TEXT("Script"));
DiscoveredRootPaths.Insert(RootPath, 0); // ★ roots 先被收敛成一张显式列表

bGeneratePrecompiledData = RuntimeConfig.bGeneratePrecompiledData;
bUsePrecompiledData = !bGeneratePrecompiledData && !RuntimeConfig.bIgnorePrecompiledData
    && !RuntimeConfig.bRunningCommandlet && !WITH_EDITOR && !bScriptDevelopmentMode;
// ★ packaged runtime 更偏“是否用预编译产物”的模式开关

const auto AllScriptRoots = FAngelscriptEngine::MakeAllScriptRoots();
UE_LOG(Angelscript, Display, TEXT("{ \"AngelscriptScriptRoots\": %s}\n"), *Result);
// ★ script roots 还能被 commandlet 直接导出给外部工具消费
```

新增对比结论：

- `UnLua` 在这里不是单纯“部署更灵活”，而是 `实现方式不同`：它把 `persistent download overlay` 做成了默认 loader 顺序的一部分。
- 如果比较“是否存在与 `ProjectPersistentDownloadDir -> ProjectDir` 对等的 loose-file override seam”，相对 `UnLua` 当前快照，`Angelscript` 属于 `没有实现同层 persistent overlay loader`。
- 反过来，`Angelscript` 也不是 `没有部署优化`；它在当前快照里更强调 `显式 script roots + precompiled data`。这与 `UnLua` 的 loose-file patch 路线是两种不同交付哲学，不应简单判成 `实现质量差异`。

---

## 深化分析 (2026-04-09 08:28:48)

### [维度 D3] 绑定契约是否可在编辑器里“修补”：`UnLua` 把 Blueprint 绑定做成 toolbar repair loop，`Angelscript` 把合法性前移到 class generator gate

前文已经把 `UnLua` 的绑定入口在 `Blueprint asset`、`Angelscript` 的入口在 `script class schema` 讲清了。这一轮补的是更细的作者体验层差异：**当作者尚未完成绑定，或者绑定写错时，插件到底提供“编辑器内修补回路”，还是提供“编译期拒绝并报错”的 gate**。

`UnLuaEditorToolbar` 当前快照里不是只放了几个菜单项，它实际上把 `Bind -> Fill ModuleName -> Compile -> Open GetModuleName graph -> Create Lua Template` 串成了一条恢复路径。`BindToLua_Executed()` 会先给现有 Blueprint 实现 `UUnLuaInterface`，再根据 `Alt` 快捷键或 `ULuaModuleLocator` 自动算出脚本路径，直接把结果写进接口图的默认值，最后重新编译并把编辑器焦点切到 `GetModuleName`。`CreateLuaTemplate_Executed()` 则继续沿着同一个 contract，从 `GetModuleName` 反查模块名、按类继承链搜索 template 文件并把 `ClassName` / `TemplateName` 实体化成 `.lua` 文件。也就是说，`UnLua` 把“绑定契约修补”本身做成了编辑器工作流。

`Angelscript` 当前分支的取向正相反。`BlueprintOverride` 相关约束在 `AngelscriptClassGenerator.cpp` 内被强制做成编译 gate：父类不存在、不是 `BlueprintEvent`、签名不匹配、`const` 不一致、`editor-only` 越界，都会直接触发 `ScriptCompileError` 并把 reload 状态打成 `Error`。Editor 侧虽然提供 “Create Blueprint from script class” 弹窗，但那是**在脚本类已有效生成之后**帮用户创建派生资产，不承担“给现有 Blueprint 补一段 module path”这类修补责任。

```
[D3-Deep] Authoring Repair Surface
UnLua
├─ Toolbar.BindToLua                             // 对现有 Blueprint 直接加接口
│  ├─ Alt / ModuleLocator -> module path         // 自动补全 GetModuleName
│  ├─ write node default value                   // 把绑定路径写回图节点
│  └─ compile + open GetModuleName graph         // 立刻把修补点暴露给作者
└─ Toolbar.CreateLuaTemplate                     // 按同一 contract 继续生成脚本文件

Angelscript
├─ author writes BlueprintOverride in .as        // 入口在脚本类声明
├─ ClassGenerator validates parent/signature     // 编译期合法性检查
├─ invalid override -> ScriptCompileError        // 错误直接阻断 class generation
└─ valid class -> optional Create Blueprint UI   // 只在 schema 成功后创建子资产
```

关键源码 [1]：`UnLua` 把绑定修补和模板生成都放进 Blueprint toolbar

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp
// 函数: FUnLuaEditorToolbar::BindToLua_Executed / CreateLuaTemplate_Executed
// 行号: 159-199, 265-310
// 位置: 现有 Blueprint 资产的绑定路径可以在编辑器内被自动补全、回写并继续生成脚本模板
// ============================================================================
FString LuaModuleName;
const auto ModifierKeys = FSlateApplication::Get().GetModifierKeys();
const auto bIsAltDown = ModifierKeys.IsLeftAltDown() || ModifierKeys.IsRightAltDown();
if (bIsAltDown)
{
    const auto Package = Blueprint->GetTypedOuter(UPackage::StaticClass());
    LuaModuleName = Package->GetName().RightChop(6).Replace(TEXT("/"), TEXT(".")); // ★ 直接按蓝图资源路径推导 module
}
else
{
    const auto Settings = GetDefault<UUnLuaSettings>();
    if (Settings && Settings->ModuleLocatorClass)
    {
        const auto ModuleLocator = Cast<ULuaModuleLocator>(Settings->ModuleLocatorClass->GetDefaultObject());
        LuaModuleName = ModuleLocator->Locate(TargetClass);                         // ★ 否则走配置化 module locator
    }
}

if (!LuaModuleName.IsEmpty())
{
    const auto InterfaceDesc = *Blueprint->ImplementedInterfaces.FindByPredicate([](const FBPInterfaceDescription& Desc)
    {
        return Desc.Interface == UUnLuaInterface::StaticClass();
    });
    InterfaceDesc.Graphs[0]->Nodes[1]->Pins[1]->DefaultValue = LuaModuleName;      // ★ 直接把 module path 写回 GetModuleName 图节点
}

const auto BlueprintEditors = FModuleManager::LoadModuleChecked<FBlueprintEditorModule>("Kismet").GetBlueprintEditors();
for (auto BlueprintEditor : BlueprintEditors)
{
    const auto MyBlueprintEditor = static_cast<FBlueprintEditor*>(&BlueprintEditors[0].Get());
    if (!MyBlueprintEditor || MyBlueprintEditor->GetBlueprintObj() != Blueprint)
        continue;
    MyBlueprintEditor->Compile();

    const auto Func = Blueprint->GeneratedClass->FindFunctionByName(FName("GetModuleName"));
    const auto GraphToOpen = FBlueprintEditorUtils::FindScopeGraph(Blueprint, Func);
    MyBlueprintEditor->OpenGraphAndBringToFront(GraphToOpen);                       // ★ 修补后立刻把入口图打开给作者确认
}

const auto Func = Class->FindFunctionByName(FName("GetModuleName"));
Class->GetDefaultObject()->ProcessEvent(Func, &ModuleName);                         // ★ 模板生成继续以 GetModuleName 为唯一事实来源

auto RelativeFilePath = "Config/LuaTemplates" / TemplateClassName + ".lua";
auto FullFilePath = FPaths::ProjectConfigDir() / RelativeFilePath;
if (!FPaths::FileExists(FullFilePath))
    FullFilePath = BaseDir / RelativeFilePath;                                      // ★ 先项目级 template，再插件默认 template

Content = Content.Replace(TEXT("TemplateName"), *TemplateName)
                 .Replace(TEXT("ClassName"), *UnLua::IntelliSense::GetTypeName(Class));
FFileHelper::SaveStringToFile(Content, *FileName, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
```

关键源码 [2]：`Angelscript` 把 `BlueprintOverride` 做成 class generation 的硬性 gate，只在成功后提供 Blueprint 创建 UI

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 函数: FAngelscriptClassGenerator::Analyze ... BlueprintOverride validation path
// 行号: 732-785, 968-1014
// 位置: BlueprintOverride 的合法性在编译期被完整验证，失败直接阻断 reload / class generation
// ============================================================================
if (FunctionDesc->bBlueprintOverride)
{
    auto* ParentFunction = GetBlueprintEventByScriptName(CodeSuperClass, FunctionDesc->FunctionName);
    if (ParentFunction == nullptr)
    {
        if (AngelscriptSuperClass.IsValid())
        {
            // ★ 脚本父类链上不存在可覆写方法，直接报 compile error
            FAngelscriptEngine::Get().ScriptCompileError(ModuleData.NewModule, FunctionDesc->LineNumber, FString::Printf(
                TEXT("BlueprintOverride method %s in class %s does not exist in superclass %s."),
                *FunctionDesc->FunctionName, *ClassData.NewClass->ClassName, *AngelscriptSuperClass->ClassName));
            ClassData.ReloadReq = EReloadRequirement::Error;
        }
    }

    if (bTypeMismatch || bArgCountMismatch)
    {
        FAngelscriptEngine::Get().ScriptCompileError(ModuleData.NewModule, FunctionDesc->LineNumber, FString::Printf(
            TEXT("BlueprintOverride method %s in class %s does not match function signature of event in superclass %s.\nExpected Signature: %s"),
            *FunctionDesc->FunctionName, *ClassData.NewClass->ClassName, *CodeSuperClass->GetName(), *ExpectedSignature));
        ClassData.ReloadReq = EReloadRequirement::Error;                             // ★ 签名不匹配直接阻断
    }

    if (ParentFunction->HasAnyFunctionFlags(FUNC_Const))
    {
        if (!ScriptFunction->IsReadOnly())
        {
            FAngelscriptEngine::Get().ScriptCompileError(ModuleData.NewModule, FunctionDesc->LineNumber, FString::Printf(
                TEXT("BlueprintOverride method %s in class %s is not const, but is overriding a const method. Please add 'const' to the end of the function declaration."),
                *FunctionDesc->FunctionName, *ClassData.NewClass->ClassName));
            ClassData.ReloadReq = EReloadRequirement::Error;                         // ★ const 正确性也是编译 gate
        }
    }
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 函数: FAngelscriptEditorModule::StartupModule / ShowCreateBlueprintPopup
// 行号: 404-409, 418-430
// 位置: editor 入口主要是“从已成功生成的 script class 创建 Blueprint/Asset”
// ============================================================================
FAngelscriptRuntimeModule::GetEditorCreateBlueprint().AddLambda(
    [](UASClass* ScriptClass)
    {
        FAngelscriptEditorModule::ShowCreateBlueprintPopup(ScriptClass);            // ★ 入口对象已经是有效的 UASClass
    }
);

void FAngelscriptEditorModule::ShowCreateBlueprintPopup(UASClass* Class)
{
    const bool bIsDataAsset = Class->IsChildOf<UDataAsset>();
    // ★ 这里处理的是“基于 script class 创建派生资产”，不是修补现有 Blueprint 的绑定字符串
}
```

新增对比结论：

- `UnLua` 这层新增认知点，不是单纯“有工具栏”，而是它把 `UUnLuaInterface + GetModuleName + template file` 三件事做成了同一条 editor repair loop。
- 若比较“现有 Blueprint 资产是否存在与 `BindToLua -> 自动回填 -> OpenGraph -> CreateLuaTemplate` 对等的修补链”，相对 `UnLua` 当前快照，`Angelscript` 属于 `没有实现同层 asset-local repair loop`。
- 反过来，`Angelscript` 也不是简单“编辑器弱”；它把错误尽量前移到 class generator 的 compile gate，这属于 `实现方式不同`，收益是 BlueprintOverride 约束更早暴露、状态更少依赖资产内字符串。

### [维度 D9] 质量通道是否统一进入 automation gate：`UnLua` 把 correctness 与 benchmark 分成两条线，`Angelscript` 把 example/coverage 折回同一 automation 管线

之前几轮已经说明 `UnLua` 有 `Tests/`、`Specs/` 两套回归层，以及 `Angelscript` 有大量 automation tests。这一轮新增的是**质量产物最终落在哪个 gate**。当前源码显示，`UnLua` 至少存在两条并行质量通道：一条是 `IMPLEMENT_SIMPLE_AUTOMATION_TEST` 驱动的正确性回归；另一条是 `Perfs/` 目录下的 benchmark 工具链，它通过 `UBlueprintFunctionLibrary` 暴露 `Start/Stop`，把耗时写进 `Saved/Benchmark/*.csv`。这两条线并没有在仓内汇成同一个 automation artifact。

`Angelscript` 则更倾向把“样例是否还能编译”和“测试期间的覆盖率产物”都折回 UE automation 生命周期。`Examples/*.cpp` 里的脚本样例并不是单独的教学附件，而是 `IMPLEMENT_SIMPLE_AUTOMATION_TEST` 包起来的 compile regression；`FAngelscriptCodeCoverage` 又直接挂到 `AutomationController` 的 `OnTestsAvailable/OnTestsComplete` 回调上，在测试开始时清空 hit map，结束时写出 `Saved/CodeCoverage` 报告。这意味着 `Angelscript` 的 example、automation 和 coverage artifact 在当前快照里是同一条生命周期。

```
[D9-Deep] Quality Channel Ownership
UnLua
├─ Tests / Specs -> UE Automation pass/fail       // 正确性回归
└─ Perfs -> BlueprintCallable benchmark helpers   // 性能测量是第二条通道
   └─ Saved/Benchmark/*.csv                       // 产物写成 sidecar CSV

Angelscript
├─ Examples/*.cpp -> automation compile tests     // 示例本身就是回归
└─ CodeCoverage -> AutomationController hooks     // 覆盖率跟随 automation 生命周期
   └─ Saved/CodeCoverage                          // 产物直接挂在测试结束点
```

关键源码 [1]：`UnLua` 的 correctness 与 benchmark 是两条不同的交付通道

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Tests/BindingTest.cpp
// 函数: FUnLuaTest_StaticBinding::RunTest
// 行号: 21-25, 48-74
// 位置: 正确性测试明确走 UE Automation contract
// ============================================================================
static void Run(TFunction<void(lua_State*, UWorld*)> Test)
{
    UnLua::Startup();
    const auto L = UnLua::GetState();                                                   // ★ test harness 启动真实 Lua VM
    // ...
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FUnLuaTest_StaticBinding,
    TEXT("UnLua.API.Binding.Static 静态绑定，继承UnLuaInterface::GetModuleName指定脚本路径"),
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter);

bool FUnLuaTest_StaticBinding::RunTest(const FString& Parameters)
{
    Run([this](lua_State* L, UWorld* World)
    {
        UnLua::RunChunk(L, Chunk1);
        World->Tick(LEVELTICK_All, SMALL_NUMBER);
        UnLua::RunChunk(L, Chunk2);
        const auto Error = lua_tostring(L, -1);
        TEST_EQUAL(Error, "");                                                          // ★ 这里的终点是 automation pass/fail
    });

    return true;
}
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Public/Perfs/UnLuaBenchmarkFunctionLibrary.h
// 函数: UUnLuaBenchmarkFunctionLibrary declaration
// 行号: 21-37
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Perfs/UnLuaBenchmarkFunctionLibrary.cpp
// 函数: UUnLuaBenchmarkFunctionLibrary::Start / Stop
// 行号: 25-50
// 位置: benchmark 通过 BlueprintCallable API 采样并输出 CSV，而不是直接进入 automation gate
// ============================================================================
UCLASS()
class UUnLuaBenchmarkFunctionLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable) static void Start(const FString& Title, const int32 N);
    UFUNCTION(BlueprintCallable) static void StartTimer(const FString& Title);
    UFUNCTION(BlueprintCallable) static void StopTimer();
    UFUNCTION(BlueprintCallable) static void Stop();                                    // ★ benchmark 被建模成可脚本/蓝图调用的辅助 API
};

void UUnLuaBenchmarkFunctionLibrary::Start(const FString& Title, const int32 N)
{
    BenchmarkTitle = Title;
    BenchmarkMultiplier = 1000000000.0f / N;
    Messages.Reset();
    FBlueprintCoreDelegates::SetScriptMaximumLoopIterations(0x7FFFFFFF);               // ★ 为性能测量放宽循环限制
}

void UUnLuaBenchmarkFunctionLibrary::Stop()
{
    const auto Message = FString::Join(Messages, TEXT("\n"));
    const auto FilePath = FString::Printf(TEXT("%sBenchmark/%s-Benchmark-%s.csv"), *FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()), *BenchmarkTitle, *FDateTime::Now().ToString());
    FFileHelper::SaveStringToFile(Message, *FilePath);                                  // ★ 最终产物是 Saved/Benchmark 下的 CSV sidecar
}
```

关键源码 [2]：`Angelscript` 把样例和覆盖率都并入 automation 生命周期

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleActorTest.cpp
// 函数: FAngelscriptScriptExampleActorTest::RunTest
// 行号: 9-10, 90-95
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleTestSupport.cpp
// 函数: AngelscriptScriptExamples::RunScriptExampleCompileTest
// 行号: 16-59
// 位置: 示例文本本身就是 automation compile regression 的输入
// ============================================================================
const AngelscriptScriptExamples::FScriptExampleSource GActorExample = {
    TEXT("Example_Actor.as"),                                                            // ★ 示例名先被映射成 module 名
    TEXT(R"ANGELSCRIPT(/* ... */)ANGELSCRIPT"),
    nullptr,
    nullptr,
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAngelscriptScriptExampleActorTest,
    "Angelscript.TestModule.ScriptExamples.Actor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptScriptExampleActorTest::RunTest(const FString& Parameters)
{
    return AngelscriptScriptExamples::RunScriptExampleCompileTest(*this, GActorExample);
}

bool RunScriptExampleCompileTest(FAutomationTestBase& Test, const FScriptExampleSource& Example)
{
    const FString ModuleNameString = FPaths::GetBaseFilename(Example.ExampleFileName);
    FAngelscriptEngine& Engine = AcquireCleanSharedCloneEngine();
    const FName ModuleName(*ModuleNameString);
    // ...
    const FString VirtualFileName = FString::Printf(TEXT("ScriptExamples/%s"), *ExampleFileName);
    const bool bCompiled = CompileAnnotatedModuleFromMemory(&Engine, ModuleName, VirtualFileName, CombinedScriptCode);
    Test.TestTrue(*FString::Printf(TEXT("Compile example '%s' succeeds"), *ExampleFileName), bCompiled); // ★ 示例直接进入 pass/fail
    return bCompiled;
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/AngelscriptCodeCoverage.cpp
// 函数: FAngelscriptCodeCoverage::AddTestFrameworkHooks / OnTestsStarting / OnTestsStopping
// 行号: 22-42
// 位置: 覆盖率产物直接挂在 automation controller 生命周期上
// ============================================================================
void FAngelscriptCodeCoverage::AddTestFrameworkHooks()
{
    IAutomationControllerModule& AutomationModule =
        FModuleManager::LoadModuleChecked<IAutomationControllerModule>("AutomationController");
    IAutomationControllerManagerRef AutomationController = AutomationModule.GetAutomationController();
    AutomationController->OnTestsAvailable().AddRaw(this, &FAngelscriptCodeCoverage::OnTestsStarting);
    AutomationController->OnTestsComplete().AddRaw(this, &FAngelscriptCodeCoverage::OnTestsStopping);     // ★ 起止点直接绑定 automation
}

void FAngelscriptCodeCoverage::OnTestsStarting(EAutomationControllerModuleState::Type Type)
{
    if (Type == EAutomationControllerModuleState::Type::Running) {
        StartRecording();
    }
}

void FAngelscriptCodeCoverage::OnTestsStopping()
{
    FString OutputDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("CodeCoverage"));
    StopRecordingAndWriteReport(OutputDir);                                             // ★ 覆盖率报告是测试结束时的标准产物
}
```

新增对比结论：

- `UnLua` 并不是“没有测试基础设施”；正确性回归这层它已经完整复用了 UE Automation。
- 新增差异在于质量通道拆分方式：benchmark 当前更像 `实现方式不同`，它是 `BlueprintCallable helper -> CSV sidecar`，而不是 automation controller 的原生 gate。这里还可基于 `Private/Perfs/` 目录扫描做一个推断：当前快照未见与 `IMPLEMENT_SIMPLE_AUTOMATION_TEST` 对等的 perf automation 入口。
- 如果比较“是否存在与 `FAngelscriptCodeCoverage::AddTestFrameworkHooks()` 对等的仓内 coverage artifact hook”，相对 `Angelscript` 当前快照，`UnLua` 属于 `没有实现同层 automation-bound coverage gate`。

### [维度 D10] 教程是否被交付成可本地化内容资产：`UnLua` 把 onboarding 做成 README → Toolbar → Tutorial asset → Localization/Workspace 的闭环

前面已经多次说明 `UnLua` 的文档不是孤立 README，这一轮再向前推进一层：**它不仅有工作流文档，还把教程文本、编辑器 UI、教学脚本、工作区和本地化配置一起交付成工程内容**。从 `README.md` 到 `Content/Script/Tutorials/*.lua`，再到 `TPSProject.code-workspace`、`DefaultEditor.ini` 和 `Plugins/UnLua/Config/Localization/UnLua.ini`，当前快照里已经形成一条完整的 onboarding delivery surface。

具体看源码与配置：`README.md` 直接把入门步骤写成“在工具栏绑定、填 `GetModuleName`、创建模板、编辑 `Content/Script/...`”；`01_HelloWorld.lua` 又反向注明自己是被哪个地图蓝图绑定的，说明教程脚本和资产是互相索引的。`TPSProject.code-workspace` 把 `Content/Script`、`Plugins/UnLua/Intermediate/IntelliSense`、`Saved/Logs` 放进同一个 workspace，减少“文档讲一步、IDE 还要手工配一步”的断层。更关键的是，`DefaultEditor.ini` 里单独为 `Content/Tutorial/*` 建了 `EditorTutorials` gather target，`UnLua.ini` 则把插件自身的本地化源与 `en/zh-Hans` 产物路径固化下来，甚至 `UnLua.manifest` 还能追溯某条 UI 文案来自哪个 `.cpp` 行号。这说明 `UnLua` 的 onboarding 在当前快照里已经是**可本地化、可打包、可追溯**的内容资产。

`Angelscript` 当前的示例体系则更偏“回归资产”。例如 `Examples/AngelscriptScriptExampleActorTest.cpp` 里把示例代码嵌进 `FScriptExampleSource`，再由 `RunScriptExampleCompileTest()` 在内存里编译。这种方式对保证示例不过期非常强，但它的 primary consumer 是 automation，而不是 editor tutorial / localized content。基于当前插件目录扫描，我没有看到与 `Content/Tutorial/* + Localization/*.ini + plugin locres` 对等的教程资产管线；这并不表示 `Angelscript` 没有文档，而是它的 onboarding source-of-truth 目前主要分散在 `Documents/Guides/*.md` 与 `AngelscriptTest/Examples`。

```
[D10-Deep] Onboarding Delivery Surface
UnLua
├─ README quickstart                              // 文档直接指向工具栏动作
├─ Content/Script/Tutorials/*.lua                 // 教学脚本与真实地图/蓝图互相索引
├─ TPSProject.code-workspace                      // Script / IntelliSense / Logs 一起交付
└─ Localization pipeline
   ├─ DefaultEditor.ini -> Content/Tutorial/*     // 教程资产进入 gather target
   └─ Plugins/UnLua/Config/Localization/UnLua.ini // 插件 UI 文案生成 locres

Angelscript
├─ Documents/Guides/*.md                          // 维护者/使用者文档
├─ AngelscriptTest/Examples/*.cpp                 // 示例先作为 automation fixture
└─ no equal tutorial-asset localization pipeline  // 基于当前插件目录扫描的推断
```

关键源码 [1]：`UnLua` 的 quickstart、教程脚本、workspace 与 localization 配置互相闭环

```md
<!-- =========================================================================
文件: Reference/UnLua/README.md
函数: N/A
行号: 35-54
位置: README 直接把工具栏动作、模块路径和教程脚本连成入门路径
=========================================================================== -->
**注意**: 如果你是一位UE萌新，推荐使用更详细的[图文版教学](Docs/CN/Quickstart_For_UE_Newbie.md)继续以下步骤。
  1. 新建蓝图后打开，在UnLua工具栏中选择 `绑定`（可同时按住`Alt`键自动生成第2步的路径）
  2. 在接口的 `GetModule` 函数中填入Lua文件路径，如 `GameModes.BP_MyGameMode`
  3. 选择UnLua工具栏中的 `创建Lua模版文件`
  4. 打开 `Content/Script/GameModes/BP_MyGameMode.lua` 编写你的代码

# 更多示例
  * [01_HelloWorld](Content/Script/Tutorials/01_HelloWorld.lua) 快速开始的例子
  * [02_OverrideBlueprintEvents](Content/Script/Tutorials/02_OverrideBlueprintEvents.lua) 覆盖蓝图事件（Overridden Functions）
  * [03_BindInputs](Content/Script/Tutorials/03_BindInputs.lua) 输入事件绑定
```

```lua
-- ============================================================================
-- 文件: Reference/UnLua/Content/Script/Tutorials/01_HelloWorld.lua
-- 位置: 教学脚本自己回指绑定它的真实地图资产
-- ============================================================================
--[[
    说明：在蓝图中实现UnLuaInterface接口，并通过 GetModuleName 指定脚本路径，即可绑定到Lua

    例如：
    本脚本由 "Content/Tutorials/01_HelloWorld/HelloWorld.map" 的关卡蓝图绑定
]]

local Screen = require "Tutorials.Screen"
-- ★ 教程脚本不是孤立片段，而是与真实资产路径互相索引
```

```ini
; ============================================================================
; 文件: Reference/UnLua/Config/DefaultEditor.ini
; 行号: 11, 18, 21
; 文件: Reference/UnLua/Plugins/UnLua/Config/Localization/UnLua.ini
; 行号: 1-10, 13-22
; 文件: Reference/UnLua/TPSProject.code-workspace
; 行号: 2-11
; 位置: 教程资产、本地化产物和 IDE workspace 被一起建模
; ============================================================================
+EngineTargetsSettings=(Name="EditorTutorials", ..., GatherFromPackages=(IsEnabled=True,IncludePathWildcards=((Pattern="Content/Tutorial/*")), ...))
+GameTargetsSettings=(Name="UnLua", ..., GatherFromTextFiles=(IsEnabled=True,SearchDirectories=((Path="Plugins/UnLua/Source")), ...))
+LocalizationPaths=%GAMEDIR%Content/Localization/UnLua                                ; ★ 教程/UI 文本进入 localization 路径

[CommonSettings]
SourcePath=Plugins/UnLua/Content/Localization/UnLua
DestinationPath=Plugins/UnLua/Content/Localization/UnLua
NativeCulture=en
CulturesToGenerate=en
CulturesToGenerate=zh-Hans                                                            ; ★ 插件文案明确生成双语 locres

{
    "folders": [
        { "path": "Content/Script" },
        { "path": "Plugins/UnLua/Intermediate/IntelliSense" },
        { "path": "Saved/Logs" }                                                      // ★ 连 workspace 都围绕教程执行面组织
    ]
}
```

关键源码 [2]：`Angelscript` 当前的示例更像 automation fixture，而不是本地化教程资产

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleActorTest.cpp
// 行号: 9-25, 90-95
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleTestSupport.cpp
// 行号: 16-59
// 位置: 示例源码驻留在 C++ automation fixture 中，消费方首先是测试框架
// ============================================================================
const AngelscriptScriptExamples::FScriptExampleSource GActorExample = {
    TEXT("Example_Actor.as"),
    TEXT(R"ANGELSCRIPT(/*
 * Script classes can always derive from the same classes that
 * blueprints can be derived from.
 */
class AExampleActor_UnitTest : AActor
{
    UPROPERTY()
    int ExampleValue = 15;
    // ★ 示例以 C++ 字符串字面量的形式驻留在测试里
})ANGELSCRIPT"),
    nullptr,
    nullptr,
};

bool RunScriptExampleCompileTest(FAutomationTestBase& Test, const FScriptExampleSource& Example)
{
    const FString ModuleNameString = FPaths::GetBaseFilename(Example.ExampleFileName);
    const FString VirtualFileName = FString::Printf(TEXT("ScriptExamples/%s"), *ExampleFileName);
    const bool bCompiled = CompileAnnotatedModuleFromMemory(&Engine, ModuleName, VirtualFileName, CombinedScriptCode);
    Test.TestTrue(*FString::Printf(TEXT("Compile example '%s' succeeds"), *ExampleFileName), bCompiled); // ★ 主要消费对象是 automation
    return bCompiled;
}
```

新增对比结论：

- `UnLua` 这里的新增发现，不是“文档多一点”，而是它把 onboarding 真正做成了 `README + Tutorial asset + Workspace + Localization` 的交付组合。
- 若比较“是否存在与 `Content/Tutorial/* + Localization/*.ini + locres` 对等的教程资产管线”，相对 `UnLua` 当前快照，`Angelscript` 属于 `没有实现同层 localized tutorial asset pipeline`。这个判断里“未见插件内 `Content/Localization` 与 tutorial asset 目录”部分，属于基于当前仓库目录扫描的推断。
- 反过来，`Angelscript` 也不是 `没有示例`；它在 examples 这层属于 `实现方式不同`，强项是示例不会轻易过期，因为它们本身就是 compile regression，只是用户上手发现性和本地化交付层还没有被做成 `UnLua` 这种内容资产形态。

---

## 深化分析 (2026-04-09 23:26:11)

### [维度 D4] 热重载相位的可观测性：`UnLua` 当前只公开“成功后 module_loaded”，`Angelscript` 公开显式的 reload phase

前几轮已经把 `reload hook` 的 ownership 区分成“Lua 资产”与 “C++ delegate”。这一轮继续往下拆，关注 **扩展方到底能观察到哪些阶段**。当前 `UnLua` 快照里，`HotReload.lua` 顶层 `hook` 表只有一个 `module_loaded` 槽位；首次 `require` 成功和 reload 成功都会回调它，但 `sandbox.load()` 失败或 `xpcall()` 失败时只会 `sandbox.exit(); return`，没有对等的 `before_reload / reload_failed / post_reinstance` 事件。也就是说，脚本层能可靠接到的是“模块已经成功进来”，看不到完整事务相位。

`Angelscript` 当前快照则把 reload phase 明确拆开给 editor/runtime 协同层消费。`FClassReloadHelper::Init()` 订阅 `OnStructReload / OnClassReload / OnDelegateReload / OnEnumChanged / OnFullReload / OnPostReload`；`OnFullReload` 进入 `PerformReinstance()`，在里面做 Blueprint 依赖扫描、pin type 替换、`ReparentHierarchies()`、重新编译和 GC；`OnPostReload(bool)` 再刷新 `BlueprintActionDatabase`、广播 `BlueprintCompiled`、重建 volume geometry，并最终清空 `ReloadState`。这不是简单“也有 hook”，而是 **reload 事务边界本身已被建模成一组 typed phase**。

```
[D4-Deep] Reload Phase Observability
UnLua
├─ hook.module_loaded only                        // 只在成功加载/重载后触发
├─ load/xpcall failure -> sandbox.exit + return   // 失败直接返回
└─ no typed post-reinstance phase                 // 当前源码未见同层阶段事件

Angelscript
├─ OnStructReload / OnClassReload                 // 收集替换对象
├─ OnFullReload -> PerformReinstance()            // 执行真正重实例化
└─ OnPostReload(bool)                             // 刷新 Blueprint/Action/Geometry 后清状态
```

关键源码 [1]：`UnLua` 当前公开的热重载相位主要停在 `module_loaded` 成功回调

```lua
-- ============================================================================
-- 文件: Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua
-- 行号: 20-25, 101-109, 571-595
-- 位置: 顶层 hook 表只有一个成功回调槽位；失败路径没有对等 phase hook
-- ============================================================================
local hook = {
    module_loaded = nil                                           -- ★ 当前公开面只有“模块已加载”
}

local function call_hook(name, ...)
    local func = hook[name]
    if not func then
        return
    end
    local ok, result = pcall(func, ...)
    if not ok then
        print(string.format("calling hook function '%s' failed : %s", name, result))
    end
end

for _, module_name in ipairs(module_names) do
    if loaded_modules[module_name] == nil then
        sandbox.require(module_name)
    else
        local func, env = sandbox.load(module_name)
        if func ~= nil then
            local ok, new_module = xpcall(func, load_error_handler)
            if not ok then
                sandbox.exit()
                return                                             -- ★ 失败直接返回，没有 reload_failed 回调
            end
            -- ...
            call_hook("module_loaded", new_module, module_name, true) -- ★ 只有成功后才通知
        else
            sandbox.exit()
            return                                                 -- ★ load 失败同样没有额外 phase
        end
    end
end
```

关键源码 [2]：`Angelscript` 把 reload 流程拆成多段 typed delegate，并显式清理尾状态

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h
// 行号: 50-57, 59-75, 132-175
// 位置: reload phase 先被收集为 typed delegate，再进入 full/post 两段事务
// ============================================================================
static void Init()
{
    FAngelscriptClassGenerator::OnStructReload.AddLambda(
    [](UScriptStruct* OldStruct, UScriptStruct* NewStruct)
    {
        ReloadState().ReloadStructs.Add(OldStruct, NewStruct);    // ★ 结构体替换单独成相位
        ReloadState().bRefreshAllActions = true;
    });

    FAngelscriptClassGenerator::OnClassReload.AddLambda(
    [](UClass* OldClass, UClass* NewClass)
    {
        if (OldClass != nullptr)
            ReloadState().ReloadClasses.Add(OldClass, NewClass);  // ★ 类替换单独成相位
        else
            ReloadState().NewClasses.Add(NewClass);
        // ...
    });

    FAngelscriptClassGenerator::OnFullReload.AddLambda(
    []()
    {
        ReloadState().PerformReinstance();                        // ★ full reload 进入重实例化阶段
    });

    FAngelscriptClassGenerator::OnPostReload.AddLambda(
    [](bool bFullReload)
    {
        if (ReloadState().bRefreshAllActions && GEngine != nullptr)
        {
            auto& Database = FBlueprintActionDatabase::Get();
            Database.RefreshAll();                                // ★ 后处理阶段统一刷新编辑器面
        }
        // ...
        ReloadState() = FReloadState();                          // ★ 最后显式清空事务状态
    });
}
```

新增对比结论：

- 两边整体仍属于 `实现方式不同`：`UnLua` 把热重载设计成“尽量修补 Lua 对象图”，因此更关心成功后的模块替换；`Angelscript` 把它设计成“影响 UE 类型系统的事务”，因此需要显式 phase。
- 如果比较“是否存在与 `OnFullReload -> PerformReinstance -> OnPostReload` 对等的 phase-complete 通知面”，相对 `Angelscript` 当前快照，`UnLua` 属于 `没有实现同层完整事务相位公开面`。
- 这不等于 `UnLua` 热重载更弱；它只是把复杂度更多压进 `HotReload.lua` 内部 merge，而不是暴露给扩展者观察。

### [维度 D9] 测试执行的宿主假设：`UnLua` regression 默认是 headful PIE，`Angelscript` 标准入口默认是 headless CLI

前几轮已经讲过 runner ownership 和 sample project。新增发现是 **测试到底假定“有人在编辑器里”还是“机器直接跑命令”**。`UnLuaTestSuite` 的 `FOpenMapLatentCommand::LoadMap()` 当前会查 `WorldContexts`、必要时 `LoadMap()`、再通过 `FLevelEditorModule::GetFirstActiveViewport()` 拿活动视口，构造 `FRequestPlaySessionParams`，最后 `RequestPlaySession()` + `StartQueuedPlaySessionRequest()` 拉起 PIE。这说明很多 regression case 的默认执行宿主不是纯命令行，而是一个能开 editor world、能拿 viewport 的 headful 编辑器会话。

`Angelscript` 的标准入口则反过来。`Tools/RunTests.ps1` 固定调用 `UnrealEditor-Cmd.exe`，默认加 `-Unattended -NoPause -stdout -ABSLOG -ReportExportPath`，没开 `-Render` 时还会再加 `-NullRHI`；同时 `UAngelscriptTestCommandlet::Main()` 直接把 `初次编译失败 / 单元测试失败 / 未初始化 struct 成员` 映射成 `1 / 2 / 3`。也就是说，当前仓库把“无视口、无人值守、可脚本消费”当成标准 contract，而不是额外适配层。

```
[D9-Deep] Test Host Assumption
UnLua
├─ latent command opens map in editor             // 先进入真实 editor world
├─ fetch active viewport                          // 依赖 LevelEditor viewport
└─ RequestPlaySession()                           // 用 PIE 回放回归

Angelscript
├─ UnrealEditor-Cmd.exe                           // 命令行 editor 进程
├─ -Unattended -NoPause -NullRHI                  // 默认无人值守
└─ commandlet / runner exit codes                 // 结果可被脚本直接消费
```

关键源码 [1]：`UnLua` 的地图型 regression 明确依赖 active viewport 与 PIE 启动

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/TestCommands.cpp
// 函数: UnLuaTestSuite::FOpenMapLatentCommand::LoadMap
// 行号: 69-127
// 位置: latent regression 不是纯命令行 world，而是显式拉起编辑器中的 PIE 会话
// ============================================================================
bool bNeedLoadEditorMap = true;
bool bPieRunning = false;
// ★ 先检查当前 editor/world 状态
const TIndirectArray<FWorldContext> WorldContexts = GEngine->GetWorldContexts();
// ...
if (bNeedLoadEditorMap || bForceReload)
{
    if (bPieRunning)
        GEditor->EndPlayMap();
    FEditorFileUtils::LoadMap(*MapName, false, true);            // ★ 必要时先切 editor map
}

FLevelEditorModule& LevelEditorModule = FModuleManager::Get().GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
TSharedPtr<IAssetViewport> ActiveLevelViewport = LevelEditorModule.GetFirstActiveViewport();

FRequestPlaySessionParams RequestParams;
ULevelEditorPlaySettings* EditorPlaySettings = NewObject<ULevelEditorPlaySettings>();
EditorPlaySettings->SetPlayNumberOfClients(1);
EditorPlaySettings->bLaunchSeparateServer = false;
RequestParams.EditorPlaySettings = EditorPlaySettings;
RequestParams.DestinationSlateViewport = ActiveLevelViewport;    // ★ 明确依赖 active viewport

GEditor->RequestPlaySession(RequestParams);
GEditor->StartQueuedPlaySessionRequest();                        // ★ 最终拉起 PIE
WaitForMapToLoad = MakeUnique<FWaitForMapToLoadCommand>();
```

关键源码 [2]：`Angelscript` 的仓库标准入口默认按 headless contract 组织，并补稳定退出码

```powershell
# ============================================================================
# 文件: Tools/RunTests.ps1
# 行号: 83-100, 214-236
# 位置: 标准 runner 直接面向命令行、超时、日志和结构化摘要
# ============================================================================
$argumentList = @(
    $agentConfig.ProjectFile
    "-ExecCmds=Automation RunTests $target; Quit"
    '-TestExit=Automation Test Queue Empty'
    '-BUILDMACHINE'
    '-Unattended'                                              # ★ 默认无人值守
    '-NoPause'
    '-NoSplash'
    '-stdout'
    '-FullStdOutLogOutput'
    '-UTF8Output'
    "-ABSLOG=$($outputLayout.LogPath)"
    "-ReportExportPath=$($outputLayout.ReportPath)"
    '-NOSOUND'
)

if (-not $Render) {
    $argumentList += '-NullRHI'                                # ★ 默认不依赖图形输出
}

$summaryObject = & (Join-Path $PSScriptRoot 'GetAutomationReportSummary.ps1') `
    -ReportPath $outputLayout.ReportPath `
    -LogPath $outputLayout.LogPath `
    -ExitCode $processExitCode `
    -BucketName $target `
    -SummaryPath $summaryPath

if ($processExitCode -eq 0 -and $null -ne $summaryRecord) {
    # ★ 即使进程返回 0，只要结构化报告缺失或有失败，也提升最终退出码
    if ($failedCount -gt 0 -or $failedTests.Count -gt 0 -or $logHints.Count -gt 0 -or $missingStructuredSummary) {
        $scriptExitCode = $exitCodes.TestFailed
    }
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptTestCommandlet.cpp
// 函数: UAngelscriptTestCommandlet::Main
// 行号: 5-24
// 位置: commandlet 直接给出机器可消费的失败类型
// ============================================================================
int32 UAngelscriptTestCommandlet::Main(const FString& Params)
{
    if (!FAngelscriptEngine::Get().bDidInitialCompileSucceed)
    {
        return 1;                                               // ★ 初次编译失败
    }

    if (!RunAngelscriptUnitTests(FAngelscriptEngine::Get().GetActiveModules(), &FAngelscriptEngine::Get(), 0, 0))
    {
        return 2;                                               // ★ 单元测试失败
    }

    if (FStructUtils::AttemptToFindUninitializedScriptStructMembers() != 0)
    {
        return 3;                                               // ★ 结构体初始化失败
    }

    return 0;
}
```

新增对比结论：

- 两边在这一层属于 `实现方式不同`：`UnLua` 更偏“把真实编辑器/PIE 行为也当作 regression 的一部分”，`Angelscript` 更偏“把运行 contract 压成命令行”。
- 如果比较“仓内是否默认交付与 active viewport 无关的 headless-first 标准执行面”，相对 `Angelscript` 当前快照，`UnLua` 属于 `没有实现同层默认标准入口`。
- 这也解释了为什么 `UnLua` 的测试更擅长回放真实地图/蓝图交互，而 `Angelscript` 更容易直接接入 CI、agent 和批处理脚本。

### [维度 D10] 文档 authority 的边界：`UnLua` 的首要 onboarding surface 在 sample project root，`Angelscript` 当前 authority 更偏 repo 文档

前几轮已经说明 `UnLua` 有完整教程链。这一轮补的是 **这条 authority 到底落在哪个边界**。当前 `UnLua` 仓库里，安装步骤先说“复制 `Plugins` 到你的 UE 工程”；但真正的新手入口随后立刻跳到 sample project 根目录下的 `Content/Script/...`、`Content/Tutorial/*` 和 `%GAMEDIR%Content/Localization/UnLua`。这意味着它的教学 authority 并不是“插件本体自带一套完全封闭的 onboarding surface”，而是“插件工具 + sample project content + project config”三者共同组成。

`Angelscript` 当前快照的 authority 则更偏 repo。`Script/Examples/README.md` 明确说当前真实交付源先放在 `Documents/Plans/Plan_ScriptExamplesExpansion/Coverage/`，`Documents/Guides/Test.md` 又把 `Tools/RunTests.ps1` 规定为唯一标准入口。也就是说，当前 `Angelscript` 不是把 onboarding authority 放进 sample project `Content/*`，而是把它放在 `repo docs + repo tools + repo script examples` 这一组文本/脚本契约里。对当前仓库来说，这很符合维护导向；但对“拿走插件就想立刻看到教程资产”的体验，它和 `UnLua` 是两条不同路线。

```
[D10-Deep] Documentation Authority Boundary
UnLua
├─ install plugin into UE project                 // 插件只是第一步
├─ author in Project/Content/Script               // 真正写码入口落在工程根
├─ tutorials under Content/Tutorial/*             // 教学资产属于 sample project
└─ %GAMEDIR%/Content/Localization/UnLua           // 本地化路径也挂在工程根

Angelscript
├─ Script/Examples/README.md                      // 示例 authority 先写在 repo
├─ Documents/Guides/Test.md                       // 运行入口也写在 repo
└─ Tools/RunTests.ps1                             // 标准 workflow 由 repo tooling 承担
```

关键源码 [1]：`UnLua` 的安装说明与教程 authority 明确跨出插件目录，落到 sample project 根

```md
<!-- =========================================================================
文件: Reference/UnLua/README.md
行号: 29-39, 41-63
文件: Reference/UnLua/Docs/CN/Quickstart_For_UE_Newbie.md
行号: 11-30
文件: Reference/UnLua/Config/DefaultEditor.ini
行号: 11, 21
位置: 教程入口、脚本输出目录与本地化路径都指向 sample project root
=========================================================================== -->
## 安装
  1. 复制 `Plugins` 目录到你的UE工程根目录。
  2. 重新启动你的UE工程

## 开始UnLua之旅
  1. 新建蓝图后打开，在UnLua工具栏中选择 `绑定`
  2. 在接口的 `GetModule` 函数中填入Lua文件路径
  3. 选择UnLua工具栏中的 `创建Lua模版文件`
  4. 打开 `Content/Script/GameModes/BP_MyGameMode.lua` 编写你的代码   <!-- ★ 写码入口已落到工程 Content -->

# 更多示例
  * [01_HelloWorld](Content/Script/Tutorials/01_HelloWorld.lua)
  * [12_CustomLoader](Content/Script/Tutorials/12_CustomLoader.lua)

## 2. 绑定到Lua
- 双击打开上一步新建好的蓝图，点击UnLua菜单栏中的“绑定”
## 3. 生成Lua模版代码
点击UnLua菜单栏中的“生成Lua模板文件”，会在工程 `Content/Script` 目录下生成。   <!-- ★ 教程权威在 sample project 根 -->

+EngineTargetsSettings=(Name="EditorTutorials", ..., GatherFromPackages=(IsEnabled=True,IncludePathWildcards=((Pattern="Content/Tutorial/*")), ...))
+LocalizationPaths=%GAMEDIR%Content/Localization/UnLua                                  <!-- ★ 本地化路径同样挂在工程根 -->
```

关键源码 [2]：`Angelscript` 当前把“示例 authority”和“标准入口”都放在 repo 文本契约里

```md
<!-- =========================================================================
文件: Script/Examples/README.md
行号: 1-14
文件: Documents/Guides/Test.md
行号: 5-11, 46-52
位置: 当前公开 authority 主要是 repo 文档、repo 脚本和 repo runner
=========================================================================== -->
# Script 示例目录

本目录保留为后续正式公开的 Script 示例入口。
- 当前首波需要交付的 Coverage `.as` 资产先放在 `Documents/Plans/Plan_ScriptExamplesExpansion/Coverage/`。
- 当前波次以 `Documents/Plans/Plan_ScriptExamplesExpansion/Coverage/` 为真实交付源，避免与测试内联字符串再次分叉。  <!-- ★ authority 先写在 repo 文档 -->

- 本仓库的标准自动化测试入口是 `Tools\RunTests.ps1`。
- 具名 suite 只能通过 `Tools\RunTestSuite.ps1` 调度。
- 所有测试命令都必须显式带超时。
- 每次测试都必须写入自己的独立输出目录。                                                       <!-- ★ workflow authority 也在 repo guide -->

### 按测试前缀运行
`powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\RunTests.ps1 -TestPrefix "Angelscript.TestModule.Bindings." -Label bindings -TimeoutMs 600000`
```

新增对比结论：

- 这一层最准确的判断不是“谁文档更多”，而是 `authority boundary` 不同：`UnLua` 是 sample-project-root-centric，`Angelscript` 当前是 repo-centric。
- 如果比较“打开工程内容目录就能沿着 `Content/Script/Tutorials` 与 `Content/Tutorial/*` 走完整 onboarding”，相对 `UnLua` 当前快照，`Angelscript` 属于 `没有实现同层 sample-project tutorial surface`。
- 反过来，如果比较“规则、标准入口和真实 authority 是否集中在一组 repo 文本契约里”，当前 `Angelscript` 其实更集中；这属于 `实现方式不同`，不是单向优劣。

---

## 深化分析 (2026-04-09 23:36:44)

### [维度 D2] `UFunction` 完整调用契约由谁兜底：`UnLua` 的反射桥直接承担 `callspace/out-return`，`Angelscript` 只把这一层保留在受限 fallback 里

前几轮已经把“零胶水不等于零桥接”拆清了。这一轮继续往下钻，关注 **完整 `UFunction` 调用契约到底挂在哪一层**。当前 `UnLua` 快照里，`FFunctionDesc::CallUE()` 不是简单地 `ProcessEvent()` 一下，而是先做对象合法性检查，再根据 `GetFunctionCallspace()` 决定本地执行还是 `CallRemoteFunction()`，最后由 `PostCall()` 把 `out` / return 写回 Lua；反向 `CallLuaInternal()` 还会拿 `FOutParmRec` 把 UE 传进来的 `out` 槽位回填。这说明 `UnLua` 的反射主路径，实际上承担了 **`UFunction` 参数方向 + RPC callspace + 返回值序约** 这一整套 UE 调用礼仪。

`Angelscript` 当前快照里，最接近这条路径的是 `BlueprintCallableReflectiveFallback`。但它在 `BindBlueprintCallableReflectiveFallback()` 处先被 `ShouldBind...`、`bAllTypesValid`、`BlueprintCallableReflectiveFallbackMaxArgs` 等条件裁掉；真正执行时也只是临时分配一块参数缓冲，把 script args 拷进去，记录非 const `ref`，`ProcessEvent()` 后再把 `ref` / return 拷回。也就是说，`Angelscript` 不是没有这一层，而是**有意把它限制成补洞路径，而不是默认的 reflected invocation substrate**。

```
[D2-Deep] Full Reflected Call Contract
UnLua
├─ FFunctionDesc::CallUE()                        // 统一反射调用入口
│  ├─ CheckObject()                               // 校验对象生命周期
│  ├─ GetFunctionCallspace()                      // 判定 local / remote
│  ├─ PreCall()                                   // 组装参数缓冲
│  ├─ ProcessEvent / CallRemoteFunction           // 按 callspace 执行
│  └─ PostCall()                                  // 回写 out / return
└─ CallLuaInternal()                              // UE -> Lua 同样回填 out/return

Angelscript
├─ Bind_*.cpp / generated entries                 // 主路径仍是显式绑定
└─ BlueprintCallableReflectiveFallback
   ├─ gate by type/arg count                      // 先限制可自动暴露范围
   ├─ temp parameter buffer                       // 临时参数缓冲
   ├─ ProcessEvent()                              // 执行目标 UFunction
   └─ copy ref/return back                        // 只对 fallback 子集兜底
```

关键源码 [1]：`UnLua` 的 reflected call path 直接承担 `local/remote/out/return`

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp
// 函数: FFunctionDesc::CallUE / FFunctionDesc::CallLuaInternal
// 行号: 171-235, 474-527
// 位置: 一条主路径同时承担 callspace 决策、ProcessEvent/Remote 调用和 out/return 回写
// ============================================================================
int32 FFunctionDesc::CallUE(lua_State *L, int32 NumParams, void *Userdata)
{
    // ... 前文省略 Object / FirstParamIndex 的初始化分支
    UObject* Object = bStaticFunc
        ? Function->GetOuterUClass()->GetDefaultObject()          // ★ static 调 CDO
        : UnLua::GetUObject(L, 1, false);

    FString Error;
    if (UNLIKELY(!CheckObject(Object, Error)))
        return luaL_error(L, TCHAR_TO_UTF8(*Error));              // ★ 先校验对象生命周期

    int32 Callspace = Object->GetFunctionCallspace(Function.Get(), nullptr);
    bool bRemote = Callspace & FunctionCallspace::Remote;
    bool bLocal = Callspace & FunctionCallspace::Local;

    // ... 前文省略 CleanupFlags / FinalFunction 的准备逻辑
    const auto Params = Buffer->Get();
    PreCall(L, NumParams, FirstParamIndex, CleanupFlags, Params, Userdata); // ★ 统一写参数缓冲

    if (bLocal)
    {
        Object->UObject::ProcessEvent(FinalFunction, Params);     // ★ 本地走 ProcessEvent
    }
    if (bRemote && !bLocal)
    {
        Object->CallRemoteFunction(FinalFunction, Params, nullptr, nullptr); // ★ 远端走 RPC
    }

    int32 NumReturnValues = PostCall(L, NumParams, FirstParamIndex, Params, CleanupFlags); // ★ 回写 out / return
    Buffer->Pop(Params);
    return NumReturnValues;
}

FOutParmRec *OutParam = OutParams;
for (int32 i = 0; i < OutPropertyIndices.Num(); i++)
{
    const auto& OutProperty = Properties[OutPropertyIndices[i]];
    if (OutProperty->IsReferenceParameter())
        continue;

    OutParam = FindOutParmRec(OutParam, OutProperty->GetProperty());
    if (OutParam)
    {
        if (OutPropertyIndex - ErrorHandlerIndex > NumResultOnStack)
        {
            OutProperty->CopyBack(
                OutParam->PropAddr,
                OutProperty->GetProperty()->ContainerPtrToValuePtr<void>(InParams)); // ★ Lua 没回推时，退回输入缓冲
        }
        else
        {
            OutProperty->WriteValue(L, OutParam->PropAddr, OutPropertyIndex, true);  // ★ Lua 显式回推时，直接写回 UE out 槽
        }
    }
    OutPropertyIndex++;
}

if (ReturnPropertyIndex > INDEX_NONE)
{
    const auto& ReturnProperty = Properties[ReturnPropertyIndex];
    ReturnProperty->WriteValue(L, RetValueAddress, -NumResultOnStack, true);         // ★ 返回值也由同一桥复制
}
```

关键源码 [2]：`Angelscript` 的 reflected bridge 只在 fallback 子集中工作

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp
// 函数: BindBlueprintCallableReflectiveFallback / InvokeReflectiveUFunctionFromGenericCall
// 行号: 374-420, 302-370
// 位置: fallback 先过资格 gate，再用临时参数缓冲处理 ref/out/return
// ============================================================================
bool BindBlueprintCallableReflectiveFallback(
    TSharedRef<FAngelscriptType> InType,
    UFunction* Function,
    FAngelscriptFunctionSignature& Signature,
    FFuncEntry& Entry)
{
    if (!ShouldBindBlueprintCallableReflectiveFallback(Function))
        return false;                                             // ★ 先过策略 gate

    if (!Signature.bAllTypesValid || Signature.ArgumentTypes.Num() > BlueprintCallableReflectiveFallbackMaxArgs)
        return false;                                             // ★ 再过类型/参数数量 gate

    auto* ReflectiveSignature = new FBlueprintCallableReflectiveSignature();
    ReflectiveSignature->UnrealFunction = Function;
    return BindReflectiveFunction(InType, Signature, ReflectiveSignature);
}

uint8* ParameterBuffer = static_cast<uint8*>(FMemory_Alloca(Function->ParmsSize));
InitializeParameterBuffer(Function, ParameterBuffer);             // ★ 临时参数缓冲，不是持久主路径状态

// ... 前文省略 Generic / ScriptArgIndex / OutReferenceCount 的初始化
for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
{
    FProperty* Property = *It;
    if (Property->HasAnyPropertyFlags(CPF_ReturnParm))
        continue;

    void* Destination = Property->ContainerPtrToValuePtr<void>(ParameterBuffer);
    void* SourceAddress = ResolveScriptArgumentAddress(Property, Generic->GetAddressOfArg(ScriptArgIndex));
    Property->CopySingleValue(Destination, SourceAddress);

    if (Property->HasAnyPropertyFlags(CPF_ReferenceParm) && !Property->HasAnyPropertyFlags(CPF_ConstParm))
        OutReferences[OutReferenceCount++] = { Property, SourceAddress }; // ★ 只记录 ref/out 回写
}

TargetObject->ProcessEvent(Function, ParameterBuffer);

for (int32 OutReferenceIndex = 0; OutReferenceIndex < OutReferenceCount; ++OutReferenceIndex)
{
    OutReference.Property->CopySingleValue(
        OutReference.ScriptValue,
        OutReference.Property->ContainerPtrToValuePtr<void>(ParameterBuffer));        // ★ 回写非 const ref
}

if (FProperty* ReturnProperty = Function->GetReturnProperty())
{
    ReturnProperty->CopySingleValue(
        Generic->GetAddressOfReturnLocation(),
        ReturnProperty->ContainerPtrToValuePtr<void>(ParameterBuffer));               // ★ 回写 return
}
```

新增对比结论：

- 这一层最准确的差异不是“谁支持 `out/ref`”，而是谁把 **完整 reflected `UFunction` contract** 当主路径维护。`UnLua` 当前快照是 `反射主路径承担完整契约`，`Angelscript` 当前快照是 `显式绑定主路径 + 受限反射 fallback`。
- 如果比较“是否存在一条默认面向所有 reflected `UFunction` 的 `callspace/out/return` 统一桥”，相对 `UnLua` 当前快照，`Angelscript` 属于 `没有实现同层主路径`；但这不是功能空洞，而是有意的 `实现方式不同`。
- `Angelscript` 的收益是类型面、脚本声明面更可控；代价是 reflective fallback 很难像 `UnLua` 那样自然扩成“一切反射函数皆可调用”的默认入口。

### [维度 D3] Blueprint 覆写面宽度的新增边界：`UnLua` 可以接管 Blueprint 资产里现成函数槽位，`Angelscript BlueprintOverride` 只认预先声明好的 event seam

前几轮已经把 `GetModuleName()` 和 `BlueprintOverride/Mixin` 的 ownership 讲清了。这一轮补的是 **覆写面到底有多宽**。`UnLua` 的 `ULuaFunction::IsOverridable()` 只要看到 `FUNC_BlueprintEvent` 就放行，而 `GetOverridableFunctions()` 会把类、本类接口、父类接口上的可覆写 `UFunction` 都扫进 map；`BindClass()` 再按 Lua table 里的函数名直接替换已有 `UFunction`。基于这段源码，可以做一个有证据的推断：**凡是最终落成 `FUNC_BlueprintEvent` 的 Blueprint 资产函数/事件，都会天然进入这张可 patch 的现成槽位表**。`README_EN` 第 9 行把这个产品意图写得更直白: "All Events/Functions defined in Blueprints"。

`Angelscript` 则把合法性收得更紧。`AngelscriptClassGenerator.cpp` 在编译期明确要求 `BlueprintOverride` 必须在超类里找到对应函数，而且该函数必须是 `BlueprintImplementableEvent` / `BlueprintNativeEvent`，或者脚本超类里已声明成 `BlueprintEvent/BlueprintOverride`；仓内示例注释也直接写明这一点。这说明 `Angelscript` 的 `BlueprintOverride` 解决的是“沿着已声明 seam 覆写”，不是“在现有 Blueprint 资产图里按名字接管任意函数槽位”。`Mixin` 更是另一类能力，它是类型表面扩展，不是资产局部 patch。

```
[D3-Deep] Override Surface Breadth
UnLua
├─ iterate existing UFunction slots               // 先扫已有函数槽位
├─ FUNC_BlueprintEvent => overridable             // Blueprint 资产函数天然进入表
├─ Lua name matches existing UFunction            // 按名字命中槽位
└─ ULuaFunction::Override()                       // 直接接管现成入口

Angelscript
├─ script declares UFUNCTION(BlueprintOverride)   // 先声明要覆写的事件
├─ class generator validates superclass seam      // 编译期验证超类存在且合法
├─ only BlueprintEvent/BPNative/BPImplementable   // 只认已声明 event seam
└─ mixin extends type surface                     // 不是资产局部 patch
```

关键源码 [1]：`UnLua` 的 override surface 是“现有 `UFunction` 槽位集合”

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaFunction.cpp
// 函数: ULuaFunction::IsOverridable / ULuaFunction::GetOverridableFunctions
// 行号: 74-79, 102-131
// 位置: 只要函数带有 BlueprintEvent 语义，就会被收入可覆写表
// ============================================================================
bool ULuaFunction::IsOverridable(const UFunction* Function)
{
    static constexpr uint32 FlagMask = FUNC_Native | FUNC_Event | FUNC_Net;
    static constexpr uint32 FlagResult = FUNC_Native | FUNC_Event;
    return Function->HasAnyFunctionFlags(FUNC_BlueprintEvent)     // ★ Blueprint 资产函数也会命中这里
        || (Function->FunctionFlags & FlagMask) == FlagResult;
}

void ULuaFunction::GetOverridableFunctions(UClass* Class, TMap<FName, UFunction*>& Functions)
{
    for (TFieldIterator<UFunction> It(Class, EFieldIteratorFlags::IncludeSuper, EFieldIteratorFlags::ExcludeDeprecated, EFieldIteratorFlags::IncludeInterfaces); It; ++It)
    {
        UFunction* Function = *It;
        if (!IsOverridable(Function))
            continue;
        if (!Functions.Find(Function->GetFName()))
            Functions.Add(Function->GetFName(), Function);        // ★ 先收集现成槽位，不新造 schema
    }
}
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaManager.cpp
// 函数: UUnLuaManager::BindClass
// 行号: 305-316
// 位置: Lua table 里的函数名只要命中现有 UFunction，就直接替换
// ============================================================================
UnLua::LowLevel::GetFunctionNames(Env->GetMainState(), Ref, BindInfo.LuaFunctions);
ULuaFunction::GetOverridableFunctions(Class, BindInfo.UEFunctions);

for (const auto& LuaFuncName : BindInfo.LuaFunctions)
{
    UFunction** Func = BindInfo.UEFunctions.Find(LuaFuncName);
    if (Func)
    {
        UFunction* Function = *Func;
        ULuaFunction::Override(Function, Class, LuaFuncName);    // ★ 按名字接管已有 Blueprint/UFunction 槽位
    }
}
```

关键源码 [2]：`Angelscript` 明确把 `BlueprintOverride` 限定在超类已声明的 event seam 上

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 函数: FAngelscriptClassGenerator::AnalyzeClass
// 行号: 792-826
// 位置: BlueprintOverride 不允许“按名字接管任意父类函数”，必须命中合法 event seam
// ============================================================================
if (auto* NonEvent = CodeSuperClass->FindFunctionByName(*FunctionDesc->FunctionName))
{
    if (NonEvent->HasAnyFunctionFlags(FUNC_BlueprintEvent))
    {
        // ★ 这里只是在提示 ScriptName/去前缀后的正确事件名
    }

    FAngelscriptEngine::Get().ScriptCompileError(ModuleData.NewModule, FunctionDesc->LineNumber, FString::Printf(
        TEXT("Method %s in parent class %s is not a BlueprintImplementableEvent or BlueprintNativeEvent in C++ and cannot be overridden."),
        *NonEvent->GetName(), *NonEvent->GetOwnerClass()->GetName()));
}
else
{
    FAngelscriptEngine::Get().ScriptCompileError(ModuleData.NewModule, FunctionDesc->LineNumber, FString::Printf(
        TEXT("BlueprintOverride method %s in class %s does not exist in superclass %s, or is not a BlueprintImplementableEvent or BlueprintNativeEvent in C++."),
        *FunctionDesc->FunctionName, *ClassData.NewClass->ClassName, *CodeSuperClass->GetName()));
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleActorTest.cpp
// 行号: 49-58
// 位置: 仓内示例直接把 BlueprintOverride 的 authoring contract 写死在注释里
// ============================================================================
/*
 * Sometimes, rather than creating a new function, you want to
 * override a function in a C++ parent class, such as BeginPlay.
 * This requires marking the method as BlueprintOverride.
 *
 * BlueprintOverride works on C++ methods that are declared either
 * BlueprintImplementEvent or BlueprintNativeEvent.
 */
UFUNCTION(BlueprintOverride)
void BeginPlay()
```

新增对比结论：

- 这一层最准确的差异是 **override surface breadth**。`UnLua` 的优势是“现有 Blueprint 资产里已经落成 `UFunction` 的面，几乎都能直接 patch”；`Angelscript` 的优势是“每个 override seam 都先被 class generator 审核过”。
- 如果比较“是否存在与 `UnLua` 同层、面向 Blueprint 资产现成函数图的按名接管能力”，相对 `UnLua` 当前快照，`Angelscript` 属于 `没有实现同层 asset-local patch surface`。
- `Mixin` 不应被误判成这个缺口的直接对位项。它对应的是 `实现方式不同` 的“类型扩展面”，不是 `UnLua` 这种“接管已有 Blueprint 函数槽位”的 patch 面。

### [维度 D5] 调试协议演进面的 ownership：`Angelscript` 把 payload 版本兼容也纳入仓内回归，`UnLua` 当前快照没有同层仓内协议演进面

前几轮已经确认，当前 `UnLua` 快照的调试会话更多依赖 `LuaPanda/LuaHelper`，而 `Angelscript` 拥有仓内 `DebugServer V2`。这一轮继续往下拆，不再讨论“谁建会话”，而是讨论 **谁拥有协议演进本身**。`Angelscript` 当前快照里，`StartDebugging` 消息会把 `DebugAdapterVersion` 记进 runtime；随后 server 在组包 call stack 时按版本条件决定是否填 `ModuleName`，对应的协议单测又显式验证 `Variables V1` 不应携带 `ValueAddress/ValueSize`，`V2` 必须保留它们。也就是说，`Angelscript` 不是只有一个“能连上的 debug server”，而是把 **payload backward-compatibility** 当成仓内 contract 来维护。

`UnLua` 这边，当前可见证据仍然停在“如何接入外部 Lua 调试器”和“如何在 IDE 断点窗口打印 call stack”。`Docs/CN/Debugging.md` 要求用户手工放入 `LuaPanda.lua` 并在脚本里 `require(...).start(...)`；`UnLuaDebugBase.h` 则只暴露 `GetLuaCallStack/PrintCallStack` 这种状态读取接口。基于当前仓库快照，**没有看到与 `DebugAdapterVersion`、payload round-trip test、字段按版本增减同层的仓内协议演进面**。这不是说 `LuaPanda` 没有版本兼容，而是说这部分 ownership 当前不在 `UnLua` 核心仓里。

```
[D5-Deep] Protocol Evolution Ownership
UnLua
├─ Docs: install LuaPanda / LuaHelper             // 协议演进跟着外部调试器走
├─ script require("LuaPanda").start(...)          // 会话 bootstrap 在外部工作流
└─ GetLuaCallStack / PrintCallStack               // 仓内只保留状态读取面

Angelscript
├─ StartDebugging carries adapter version         // 客户端先声明版本
├─ runtime stores DebugAdapterVersion             // 版本进入仓内协议栈
├─ payload fields gated by version                // 按版本加减字段
└─ protocol round-trip tests                      // V1/V2 差异直接被自动化守住
```

关键源码 [1]：`UnLua` 当前仓内证据停在“外部调试器接入说明 + call stack helper”

```md
<!-- =========================================================================
文件: Reference/UnLua/Docs/CN/Debugging.md
行号: 1-12
文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDebugBase.h
行号: 91-94
位置: 调试会话由外部工作流建立，仓内公开面主要是状态读取 helper
=========================================================================== -->
# 调试
## 使用 LuaPanda / LuaHelper 调试

1. 从VSCode应用市场安装 LuaPanda / LuaHelper
2. 从 LuaPanda 官方仓库获取 `LuaPanda.lua`，放入 `{UE工程}/Content/Script` 目录
3. 在Lua代码中加入 `require("LuaPanda").start("127.0.0.1",8818)`      <!-- ★ 会话 bootstrap 明确在仓外 -->

UNLUA_API FString GetLuaCallStack(lua_State *L);
/* 在IDE断点调试窗口中直接运行UnLua::PrintCallStack(L)来打印当前堆栈 */
void PrintCallStack(lua_State* L);                                 <!-- ★ 仓内公开面停在 call stack helper -->
```

关键源码 [2]：`Angelscript` 把协议版本演进写进 runtime 和自动化测试

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 函数: FAngelscriptDebugServer::HandleMessage / FAngelscriptDebugServer::SendCallStack
// 行号: 897-907, 1467-1474
// 位置: adapter version 先进入 runtime，再影响后续 payload 结构
// ============================================================================
else if (MessageType == EDebugMessageType::StartDebugging)
{
    FStartDebuggingMessage Msg;
    *Datagram << Msg;

    bIsDebugging = true;
    AngelscriptDebugServer::DebugAdapterVersion = Msg.DebugAdapterVersion; // ★ 客户端版本进入仓内协议栈

    FDebugServerVersionMessage DebugServerVersionMessage;
    DebugServerVersionMessage.DebugServerVersion = DEBUG_SERVER_VERSION;
    SendMessageToClient(Client, EDebugMessageType::DebugServerVersion, DebugServerVersionMessage);
}

Frame.Name = ANSI_TO_TCHAR(ScriptFunction->GetName());
Frame.Source = SectionName ? ANSI_TO_TCHAR(SectionName) : TEXT("");
if (AngelscriptDebugServer::DebugAdapterVersion >= 1)
    Frame.ModuleName = ScriptFunction->GetModuleName() ? ANSI_TO_TCHAR(ScriptFunction->GetModuleName()) : TEXT(""); // ★ 字段是否存在受版本控制
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp
// 函数: FAngelscriptDebugProtocolVariablesVersion1RoundTripTest::RunTest /
//       FAngelscriptDebugProtocolVariablesVersion2RoundTripTest::RunTest
// 行号: 121-170
// 位置: V1/V2 变量消息的字段差异直接被 round-trip 测试固化
// ============================================================================
FScopedDebugAdapterVersionOverride AdapterVersionScope(1);
FAngelscriptVariable Message;
Message.ValueAddress = 0xDEADBEEF;
Message.ValueSize = 8;
// ★ V1 写出后，旧 payload 不应保留新字段
TestEqual(TEXT("... should leave ValueAddress at the default when V1 omits it"), RoundTripped.ValueAddress, uint64{0});
TestEqual(TEXT("... should leave ValueSize at the default when V1 omits it"), RoundTripped.ValueSize, uint8{0});

// ... 进入第二个测试函数后，版本切到 V2
FScopedDebugAdapterVersionOverride AdapterVersionScope(2);
FAngelscriptVariable Message;
Message.ValueAddress = 0x12345678;
Message.ValueSize = 4;
// ★ V2 则必须保留新增字段
TestEqual(TEXT("... should preserve the value address"), RoundTripped.ValueAddress, Message.ValueAddress);
TestEqual(TEXT("... should preserve the value size"), RoundTripped.ValueSize, Message.ValueSize);
```

新增对比结论：

- 这一层不是“谁有调试器”的重复表述，而是 **谁拥有协议演进 contract**。`Angelscript` 当前快照把 payload 版本兼容直接纳入 runtime 和 automation；`UnLua` 当前快照把这层 ownership 留给外部 Lua 调试器工作流。
- 如果比较“仓内是否存在与 `DebugAdapterVersion + round-trip test` 对等的协议演进面”，相对 `Angelscript` 当前快照，`UnLua` 属于 `没有实现同层仓内协议版本演进面`。这里是对当前仓库快照的判断，不是对 `LuaPanda/LuaHelper` 生态本身的否定。
- 这也解释了两边维护面的差异：`Angelscript` 需要自己维护 wire compatibility；`UnLua` 核心仓因此更轻，但调试协议的可控性和可回归性不在插件核心仓内。
