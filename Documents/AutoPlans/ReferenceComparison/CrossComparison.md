# Reference 横向对比分析

> **对比范围**: Hazelight (UEAS2) / UnrealCSharp / UnLua / puerts / sluaunreal / 当前 Angelscript
> **分析日期**: 2026-04-08
> **前置依赖**: 本轮未发现 `Hazelight_Analysis.md`、`UnrealCSharp_Analysis.md`、`UnLua_Analysis.md`、`puerts_Analysis.md`、`sluaunreal_Analysis.md`，以下结论直接来自源码取证
> **判定说明**: `Full` = 主路径完整且可直接使用；`Partial` = 有实现但覆盖不完整或实现路径不同；`None` = 本轮未定位到对应实现；`N/A` = 该插件设计上不适用

这一轮最明显的格局不是“谁功能多”，而是“谁把 contract 前置到了 build/codegen 阶段”。Hazelight 与当前 Angelscript 仍属于同一技术谱系：静态 bind、bind database、显式 reload state machine；UnrealCSharp 与 puerts 更偏“工具链先行”；UnLua 与 sluaunreal 更偏“运行时 hook + 解释器/VM 驱动”。

当前 Angelscript 相比 Hazelight 已经不是简单移植版。源码上可以直接看到三处演化：一是 `AngelscriptTest` 独立为模块；二是 `EnhancedInput` / `GameplayAbilities` 依赖被内聚进 `AngelscriptRuntime`；三是 `AngelscriptUHTTool`、`CodeCoverage`、`DebugDatabase` 一类开发资产已经成形，但还没有像 UnrealCSharp / puerts 那样被提升成更独立的模块化工具链。

## [D1] 插件架构与模块划分

### 各插件实现概览

```
Module Strategy
HZ : Code + Editor + Loader + ext plugins        // 核心精简，功能拆到外挂插件
AS : Runtime + Editor + Test                     // 当前实现把测试并入主插件
UC : Runtime + Core + Editor + Compiler + Gen   // 工具链型拆分最明显
UL : Runtime + Editor + Program                 // 运行时简洁，额外带默认参数采集
PU : Runtime(3) + Editor + Program              // JS backend + 声明生成分层
SL : Runtime + Profiler                         // 最轻量，偏运行时与性能剖析
```

从模块数看，UnrealCSharp 与 puerts 代表“多模块协作平台”，Hazelight / 当前 Angelscript / UnLua / sluaunreal 更像“插件内闭环”。但当前 Angelscript 与 Hazelight 的差异不在模块数量，而在“能力摆放位置”：Hazelight 把 `EnhancedInput`、`GAS` 放到外挂插件；当前 Angelscript 把这类依赖直接收进 `AngelscriptRuntime`。

### 详细对比

#### 模块边界

- 当前 Angelscript 在 `Plugins/Angelscript/Angelscript.uplugin:18-34` 只暴露 `AngelscriptRuntime`、`AngelscriptEditor`、`AngelscriptTest` 三个模块，说明其设计重点是“运行时内聚 + 编辑器配套 + 测试隔离”，而不是像 Hazelight 一样再保留一个单独的 loader。
- Hazelight 在 `References.HazelightAngelscriptEngineRoot/Engine/Plugins/Angelscript/Angelscript.uplugin:18-34` 暴露 `AngelscriptCode`、`AngelscriptEditor`、`AngelscriptLoader`，并且在 `Engine/Plugins` 下继续拆出 `AngelscriptEnhancedInput`、`AngelscriptGAS`。这不是“当前插件缺少 loader”，而是“当前插件把能力边界向主 runtime 收拢了”。
- UnrealCSharp 在 `Reference/UnrealCSharp/UnrealCSharp.uplugin:18-54` 中把 runtime、editor、generator、compiler、program 分成 7 个模块；puerts 在 `Reference/puerts/unreal/Puerts/Puerts.uplugin:15-49` 中把 `JsEnv`、`WasmCore`、`DeclarationGenerator`、`ParamDefaultValueMetas` 等拆开。它们更适合长期维护大型工具链，但模块协调成本也更高。
- UnLua 与 sluaunreal 的拆分更保守。UnLua 维持 `UnLua` / `UnLuaEditor` / `UnLuaDefaultParamCollector` 三段，sluaunreal 只有 `slua_unreal` / `slua_profile` 两段，体现出“先保 runtime 通路，再补 editor 辅助”的思路。

#### 依赖内聚与第三方边界

- 当前 Angelscript 的 `AngelscriptRuntime.Build.cs:29-65` 已经把 `StructUtils`、`EnhancedInput`、`GameplayAbilities`、`GameplayTasks` 直接列进依赖链；Hazelight 对应 `AngelscriptCode.Build.cs:13-44` 并未这样做。两者不是“有无能力”的差异，而是“主插件内聚”与“可选插件解耦”的差异。
- puerts 和 UnLua 都需要显式第三方 runtime 分发。`Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs:360-408` 直接处理 `v8.dll`、`libv8.dylib` 等 staged runtime；`Reference/UnLua/Plugins/UnLua/Source/ThirdParty/Lua/Lua.Build.cs:29-80` 则把 Lua 当外部模块编译与拷贝。
- sluaunreal 的 `slua_unreal.Build.cs:31-113` 也是平台库直连，只是工具链层没有 puerts 那么重。

[1] 当前 Angelscript 与 Hazelight 在 Build.cs 上的边界差异：

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs
// 函数: ModuleRules constructor
// 位置: 29-65，当前 Angelscript runtime 依赖声明
// ============================================================================
PublicDependencyModuleNames.AddRange(new string[]
{
    "GameplayTags",
    "StructUtils", // ★ 当前插件把 StructUtils 直接并入 runtime 公共依赖
});

PrivateDependencyModuleNames.AddRange(new string[]
{
    "EnhancedInput",   // ★ 输入扩展直接进入 runtime
    "GameplayAbilities",
    "GameplayTasks",
});

// ============================================================================
// 文件: References.HazelightAngelscriptEngineRoot/Engine/Plugins/Angelscript/Source/AngelscriptCode/AngelscriptCode.Build.cs
// 函数: ModuleRules constructor
// 位置: 13-44，Hazelight 核心模块依赖声明
// ============================================================================
PublicDependencyModuleNames.AddRange(new string[]
{
    "GameplayTags", // 这里只保留核心依赖
});

PrivateDependencyModuleNames.AddRange(new string[]
{
    "PhysicsCore",
    "CoreOnline",   // ★ 没有把 EnhancedInput / GAS 合并进主模块
});
```

### 对比矩阵

| 功能点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 运行时 / 编辑器分模块 | Full | Full | Full | Full | Full | Full |
| 测试模块独立存在 | None | None | Partial | None | None | Full |
| 可选能力拆为外挂插件 | Full | Partial | Partial | Partial | None | Partial |
| 第三方 runtime 显式分发 | Partial | Partial | Full | Full | Full | Partial |
| 工具链模块独立提升到 uplugin | Partial | Full | Partial | Full | None | Partial |

### 小结与建议

- Angelscript 在 D1 上不是“模块不够”，而是“模块边界收得太紧”。如果后续继续扩张 `EnhancedInput`、`GameplayAbilities`、`UI` 等绑定，建议优先把高耦合领域能力重新拆回可选插件，优先级 `P1`。
- 当前实现保留 `AngelscriptTest` 是正确方向，这一点比 Hazelight 更利于长期回归；建议继续保持，不要回退进 runtime。
- 值得吸收的是 UnrealCSharp / puerts 的“工具链模块显式化”，而不是它们的全部模块数量。当前 `AngelscriptUHTTool` 已存在，下一步应考虑把它从“源码目录中的工具”提升为更可见的 build contract，优先级 `P1`。

## [D2] 反射绑定机制

### 各插件实现概览

```
Reflection Binding Flow
HZ/AS : UClass/FProperty -> Bind_BlueprintType -> TypeUsage -> Bind DB
UC    : UObject/UStruct -> Registry graph -> InternalCall builder -> Mono
UL    : Class + Lua table -> DynamicBindingObjects -> ULuaFunction::Override
PU    : GeneratedClass -> RedirectToTypeScript -> execCallJS
SL    : LuaCppBinding templates + LuaOverrider hook
```

这里的核心差异不是“谁支持反射”，而是“反射结果落到哪里”。当前 Angelscript / Hazelight 把反射结果落到强类型 bind 与 bind database；UnrealCSharp 落到 registry graph 与 Mono internal call；UnLua、puerts、sluaunreal 则更强调运行时 override 与 dispatch。

### 详细对比

#### 类型暴露入口

- 当前 Angelscript 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1063-1115` 中直接遍历 `UClass` 的 `FProperty`，通过 `FAngelscriptTypeUsage::FromProperty()` 与 `Usage.Type->BindProperty()` 进入统一类型系统，并把结果写进 `DBProperties`。这是一条“静态 bind + 缓存复制”的主路径。
- Hazelight 的 `References.HazelightAngelscriptEngineRoot/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_BlueprintType.cpp:1039-1105` 与当前几乎同源，说明当前插件在 D2 上的基础框架并不是新造轮子，而是在原方案上继续扩展。
- UnrealCSharp 的入口则是 registry graph。`FCSharpEnvironment` 初始化时同时创建 `ClassRegistry`、`StructRegistry`、`DelegateRegistry`、`BindingRegistry`，随后在 `FRegisterStruct` 里用 `TBindingClassBuilder<UStruct>` 把 `StaticStruct` / `Register` / `Identical` / `UnRegister` 这样的托管桥函数挂进去。它不是靠一批 `Bind_*.cpp` 手写文件直接驱动，而是靠 registry 与 builder 抽象统一调度。
- sluaunreal 的 `LuaCppBinding.h:250-319` 体现出另一种思路：用模板展开把 C++ 静态函数、成员函数、const 成员函数转换成 `LuaCFunction`。它能覆盖大量导出场景，但在 UE 反射层面的统一建模程度低于 Angelscript / UnrealCSharp。

#### Blueprint override 与绑定生命周期

- 当前 Angelscript 在 `Bind_BlueprintEvent.cpp:553-641` 中把 `UFunction` 解析成 `FAngelscriptFunctionSignature`，然后分 `global / mixin / method` 三种脚本入口注册，同时把 `Signature.ScriptName -> UFunction` 写进 `GBlueprintEventsByScriptName`。这意味着它把 Blueprint event override 和普通 method bind 连接到了同一签名系统。
- UnLua 的 `UnLuaManager.cpp:224-320` 先把 `UDynamicBlueprintBinding` 挂进 `UBlueprintGeneratedClass::DynamicBindingObjects`，再读 Lua module table，把同名 Lua 函数批量 `ULuaFunction::Override()` 到 `UFunction` 上。这里的 owner 更偏“类绑定对象 + Lua table”，而不是像 Angelscript 一样先建立统一类型签名层。
- puerts 的 `TypeScriptGeneratedClass.cpp:221-243` 则把 `UFunction` 直接 `SetNativeFunc(&UTypeScriptGeneratedClass::execCallJS)`，属于“生成类 + dispatch redirect”路线。它在 Blueprint 互操作上很直接，但反射绑定的核心 contract 更偏 generated class，而非类型数据库。

#### UInterface / Delegate 粒度

- 当前 Angelscript 对 `UClass`、`UStruct`、`UEnum`、delegate 的主路径已比较完整，但 `UInterface` 仍是分裂 owner。结合 `Bind_BlueprintType.cpp`、`Bind_UObject.cpp` 与 `ClassGenerator` 相关路径，本轮更像是“已有局部能力但未统一进主链”，因此这里应判 `Partial`，不是 `None`。
- UnrealCSharp 由于 registry graph 天然区分 `Class`、`Struct`、`Delegate` 等 family，在接口一类类型上更接近“统一入口”思路。
- UnLua、puerts、sluaunreal 更偏运行时 dispatch，因此在 interface / delegate 的主体验上通常依赖实际 override/hook 场景，而不是像 Angelscript 一样把它们都提前压进 bind database。

#### 绑定缓存与构建产物

- 当前 Angelscript / Hazelight 都有 bind database，因此反射绑定不是“每次启动纯运行时现算”；后文 D11 可看到它们在 cook 阶段会把 `Binds.Cache` 与 `.Headers` 固化下来。
- UnrealCSharp 与 puerts 的缓存重点不在 bind database，而在 generated code / declaration artifacts。
- UnLua、sluaunreal 更偏运行时装配，本轮未定位到与 Angelscript bind database 对等的缓存系统。

[1] 当前 Angelscript 的属性绑定主循环：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp
// 函数: BindProperties
// 位置: 1063-1115，按 FProperty 枚举并写回 bind database
// ============================================================================
void BindProperties(FAngelscriptBinds Binds, TSharedRef<FAngelscriptType> Type, TArray<FAngelscriptPropertyBind>& DBProperties)
{
    UClass* Class = Type->GetClass(FAngelscriptTypeUsage::DefaultUsage);

    for (TFieldIterator<FProperty> It(Class, EFieldIterationFlags::IncludeDeprecated); It; ++It)
    {
        FProperty* Property = *It;

        if (!FAngelscriptEngine::ShouldUseEditorScriptsForCurrentContext() && Property->HasAnyPropertyFlags(CPF_EditorOnly))
            continue; // ★ 运行时上下文直接裁掉 editor-only 属性

        FAngelscriptTypeUsage Usage = FAngelscriptTypeUsage::FromProperty(Property);
        if (!Usage.IsValid())
            continue; // ★ 先把 UE property 映射到统一的 Angelscript type usage

        if (Usage.Type->BindProperty(Usage, Params, Property))
        {
            FAngelscriptPropertyBind DBProp;
            DBProp.UnrealPath = Property->GetName();
            DBProperties.Add(DBProp); // ★ 成功绑定后把结果复制进数据库，供后续缓存/重放
            continue;
        }
    }
}
```

[2] UnrealCSharp 的 registry + builder 路线：

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp
// 函数: FCSharpEnvironment constructor body
// 位置: 55-85，环境初始化时构建 registry graph
// ============================================================================
Domain = new FDomain({ "", FUnrealCSharpFunctionLibrary::GetFullAssemblyPublishPath() });
DynamicRegistry = new FDynamicRegistry();
ClassRegistry = new FClassRegistry();
StructRegistry = new FStructRegistry();
DelegateRegistry = new FDelegateRegistry();
BindingRegistry = new FBindingRegistry(); // ★ 先建 registry，再由后续绑定过程填充

// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Domain/InternalCall/FRegisterStruct.cpp
// 函数: FRegisterStruct::FRegisterStruct
// 位置: 56-63，UStruct 桥接注册
// ============================================================================
FRegisterStruct()
{
    TBindingClassBuilder<UStruct>(NAMESPACE_LIBRARY)
        .Function("StaticStruct", StaticStructImplementation)
        .Function("Register", RegisterImplementation)
        .Function("Identical", IdenticalImplementation)
        .Function("UnRegister", UnRegisterImplementation); // ★ builder 统一注册托管桥函数
}
```

### 对比矩阵

| 功能点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| `UClass` / `UStruct` 自动暴露 | Full | Full | Partial | Partial | Partial | Full |
| 统一 property type bridge | Full | Full | Partial | Partial | Partial | Full |
| Blueprint override 纳入同一绑定体系 | Full | Partial | Full | Full | Full | Full |
| bind 结果可缓存 / 重放 | Full | Partial | None | Partial | None | Full |
| `UInterface` 进入统一 owner | Partial | Full | Partial | Partial | None | Partial |
| delegate 暴露为一等公民 | Full | Full | Partial | Partial | Partial | Full |

### 小结与建议

- Angelscript 在 D2 的优势是“类型系统与 bind database 一体化”，不是简单的手写绑定文件数量。这一点应继续保持，优先级 `P0`。
- 真正的差距不在 `UClass` / `UStruct`，而在 `UInterface` 这种边角 family 还没有完全并入统一 owner。建议优先吸收 UnrealCSharp 的“registry family 一致性”思路，优先级 `P0`。
- 对比 UnLua / puerts / sluaunreal 可以看到，运行时 hook 路线更灵活，但 contract 更晚。Angelscript 不应退回纯 hook 模式，而应在现有静态 bind 基础上补齐 interface/property family。

## [D3] Blueprint 交互

### 各插件实现概览

```
Blueprint Interop
AS/HZ : UFunction -> Signature -> BindMethodDirect -> ScriptName map
UC    : Generated override file + runtime bind          // 以托管文件工作流为主
UL    : DynamicBindingObject + Lua table override
PU    : GeneratedClass -> execCallJS
SL    : duplicateUFunction + hookBpScript
```

Blueprint 交互的分野非常清晰。当前 Angelscript / Hazelight 走“签名桥 + 类型系统”；UnLua 走“动态绑定对象 + Lua table”；puerts 与 sluaunreal 则直接改写 `UFunction` dispatch。

### 详细对比

#### 脚本覆写 Blueprint event

- 当前 Angelscript 在 `Bind_BlueprintEvent.cpp:553-641` 中对每个 `UFunction` 构造 `FAngelscriptFunctionSignature`，再分发到 `BindGlobalFunctionDirect` 或 `BindMethodDirect`。这条路径的优点是 override 仍服从统一签名验证，缺点是签名系统必须足够完整，否则边缘类型会直接掉出主链。
- UnLua 在 `UnLuaManager.cpp:253-316` 中先载入 Lua module table，再把同名 Lua 函数逐个 `ULuaFunction::Override()` 到 `UFunction` 上。优点是接 Blueprint 快；代价是 override contract 更晚，很多问题只有运行时才暴露。
- puerts 的 `TypeScriptGeneratedClass.cpp:221-243` 用 `SetNativeFunc(&execCallJS)` 把 `UFunction` 改成 JS dispatch，优点是通路短、generated class 清晰；缺点是 override contract 与 generated class 深度耦合。
- sluaunreal 在 `LuaOverrider.cpp:1174-1298` 中读 Lua module、构造 self table、找同名函数，再通过 `hookBpScript()` 或 `duplicateUFunction()` 注入。它比 UnLua 更接近“直接 patch UFunction 行为”。

#### 脚本调用 Blueprint 函数

- 当前 Angelscript 的方法绑定本身就是通过 `BindMethodDirect` 挂到脚本引擎，因此调用 BlueprintCallable 函数时与普通 method bind 的入口相同。
- UnLua / sluaunreal 在运行时直接依靠 Lua module 与 hook 后的 `UFunction` 交互，路径更短，但调用行为更依赖当时的 class state。
- puerts 在 generated class 中把 public / event 函数改为 JS dispatch，因此“Blueprint 调 JS”与“JS 调 Blueprint”可以走同一套 generated class contract。

#### 混合继承链

- 当前 Angelscript 因为脚本类生成与 Blueprint event bind 是两条相邻但独立的子系统，混合继承链处理更稳，但也更依赖 reload/class generator 正确维护 class state。
- UnLua / sluaunreal 的混合链条更像“Blueprint class 上附加脚本行为”；puerts 更像“Blueprint class 直接变成 generated script class 的宿主”；UnrealCSharp 更偏“托管 override file + generated stub”。

[1] 当前 Angelscript 的 Blueprint event 桥：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp
// 函数: BindBlueprintEvent
// 位置: 553-641，Blueprint event -> Angelscript 签名桥
// ============================================================================
FAngelscriptFunctionSignature Signature;
Signature.InitFromDB(InType, Function, DBBind, /* bInitTypes= */ true);

if (!Signature.bAllTypesValid)
    return; // ★ 未知类型直接退出，说明 override 仍受统一签名系统约束

if (Signature.bStaticInScript)
{
    int32 FunctionId = FAngelscriptBinds::BindGlobalFunctionDirect(
        Signature.Declaration, asFUNCTION(CallStaticWithSignature), asCALL_GENERIC, ASAutoCaller::FunctionCaller::Make(), Sig);
    Signature.ModifyScriptFunction(FunctionId);
}
else
{
    int32 FunctionId = FAngelscriptBinds::BindMethodDirect(
        InType->GetAngelscriptTypeName(), Signature.Declaration,
        asFUNCTION(CallEventWithSignature), asCALL_GENERIC, ASAutoCaller::FunctionCaller::Make(), Sig);
    Signature.ModifyScriptFunction(FunctionId);
}

GBlueprintEventsByScriptName.FindOrAdd(CastChecked<UClass>(Function->GetOuter())).Add(Signature.ScriptName, Function);
// ★ script name 到 UFunction 的索引被保留下来，后续调用与跳转都能复用
```

[2] UnLua 的 Blueprint class 绑定与 override：

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaManager.cpp
// 函数: GetOrAddBindingObject / BindClass
// 位置: 224-320，Blueprint class -> Lua module override
// ============================================================================
UDynamicBlueprintBinding* BindingObject = UBlueprintGeneratedClass::GetDynamicBindingObject(Class, BindingClass);
if (!BindingObject)
{
    BindingObject = (UDynamicBlueprintBinding*)NewObject<UObject>(GetTransientPackage(), BindingClass);
    BPGC->DynamicBindingObjects.Add(BindingObject); // ★ 先把 binding object 挂到蓝图生成类上
}

UnLua::LowLevel::GetFunctionNames(Env->GetMainState(), Ref, BindInfo.LuaFunctions);
ULuaFunction::GetOverridableFunctions(Class, BindInfo.UEFunctions);

for (const auto& LuaFuncName : BindInfo.LuaFunctions)
{
    UFunction** Func = BindInfo.UEFunctions.Find(LuaFuncName);
    if (Func)
    {
        ULuaFunction::Override(*Func, Class, LuaFuncName); // ★ 逐个把 Lua 同名函数改写到 UFunction 上
    }
}
```

### 对比矩阵

| 功能点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 脚本覆写 Blueprint event | Full | Partial | Full | Full | Full | Full |
| 脚本调用 BlueprintCallable | Full | Partial | Full | Full | Full | Full |
| Blueprint 调脚本函数 | Full | Partial | Full | Full | Full | Full |
| 混合继承链有显式 owner | Full | Partial | Partial | Full | Partial | Full |
| override 前做签名校验 | Full | Partial | Partial | Partial | Partial | Full |

### 小结与建议

- Angelscript 在 D3 的关键优势是“Blueprint 交互仍落在统一签名系统里”，这点不要牺牲，优先级 `P0`。
- 值得吸收的是 UnrealCSharp / puerts 对 generated artifact 的显式工作流，而不是 UnLua/slua 的纯运行时灵活性。对 Angelscript 来说，最有价值的是让 Blueprint override 对应的 generated metadata 更可见，优先级 `P1`。
- sluaunreal 与 puerts 提醒了一个点：当 override 目标很多时，直接 UFunction redirect/hook 的改动面更小。Angelscript 可以局部借鉴这种“重定向通知”思想，但不应放弃当前签名层。

## [D4] 热重载

### 各插件实现概览

```
Reload Strategy
HZ/AS : file queue -> compile -> ReloadRequirement -> soft/full switch
UC    : domain/assembly reload centric
UL    : DoString("UnLua.HotReload()")
PU    : file watcher -> ReloadSource -> generated class rebind
SL    : object/class hook refresh, no explicit reload ladder found
```

当前 Angelscript 与 Hazelight 在 D4 上明显属于一档：它们都把 reload 需求分成 `SoftReload`、`FullReloadSuggested`、`FullReloadRequired`，并且在不能 full reload 时保留旧模块继续运行。这个设计强于 UnLua 的 module 级 reload，也比 sluaunreal 当前取证到的“按对象/class hook 修补”更可控。

### 详细对比

#### 变更检测机制

- 当前 Angelscript 的 `AngelscriptEngine.cpp:2729-2778` 负责消费文件变更队列、删除队列与全量重载队列；真正的 reload 判定在 `3914-3999`。这说明它不是“单纯发现文件改了就全量重编”，而是把检测与决策拆开。
- Hazelight 的 `AngelscriptManager.cpp:1481-1520` 与 `2621-2682` 走的是同一套 lineage。
- puerts 在 `PuertsEditorModule.cpp:116-150` 里注册 `FSourceFileWatcher`，文件更新后直接 `JsEnv->ReloadSource()`。这更快，但 contract 落点在 `JsEnv` 与 generated class，缺少像 Angelscript 那样的 `ReloadRequirement` 梯度。
- UnLua 的 `LuaEnv.cpp:448-450` 只有 `DoString("UnLua.HotReload()")`，其粒度更接近模块脚本逻辑刷新，而不是 class/property 语义重建。

#### 重载粒度

- 当前 Angelscript 最有价值的不是“能热重载”，而是“把重载粒度显式编码到枚举里”。`FullReloadSuggested` 与 `FullReloadRequired` 的区别，直接对应 PIE 中能否先 soft reload、之后再补 full reload。
- puerts 的粒度控制主要由 generated class rebind 与 `ReloadSource()` 组合完成，更像“脚本源更新 + 类重绑定”；不是没有粒度，而是粒度 owner 不在统一枚举上。
- UnLua 与 sluaunreal 在本轮取证中没有看到与 `ReloadRequirement` 对等的统一状态机，因此判为 `Partial`。

#### 失败恢复与旧代码保持

- 当前 Angelscript 在 `FullReloadRequired + SoftReloadOnly` 场景下明确打印错误、保留旧代码活跃、并把 full reload 需求留待后续。这里体现的是“失败时保守回退”，而不是“热重载失败就把运行时打坏”。
- UnLua / puerts 当然也能在 reload 失败时保留旧脚本状态，但本轮没有定位到像 Angelscript 一样直白的“旧模块继续激活”状态机分支，因此在可观测性上当前实现更强。

#### rebind 通知

- puerts 的优势在 `TypeScriptGeneratedClass.cpp:77-110,221-243` 体现得更明显：source reload 之后，它还有 generated class 级的 rebind/redirect contract。
- 当前 Angelscript 的短板不是 reload 梯度，而是 reload 后没有 puerts 那么显式的 `rebind notify` 合同；这一点属于“实现质量差异”，不是“没有热重载”。

[1] 当前 Angelscript 的 reload 梯度：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: compile/reload switch
// 位置: 3914-4005，按 ReloadRequirement 分流
// ============================================================================
auto ReloadReq = ClassGenerator.Setup();

switch (ReloadReq)
{
    case FAngelscriptClassGenerator::EReloadRequirement::SoftReload:
        SwapInModules(CompiledModules, DiscardedModules);
        ClassGenerator.PerformSoftReload();
        break;

    case FAngelscriptClassGenerator::EReloadRequirement::FullReloadSuggested:
        if (CompileType == ECompileType::SoftReloadOnly)
        {
            // ★ PIE 中先 soft reload，并显式提示稍后补 full reload
            bWasFullyHandled = false;
            SwapInModules(CompiledModules, DiscardedModules);
            ClassGenerator.PerformSoftReload();
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
            // ★ 无法 full reload 时保留旧代码继续运行
            bShouldSwapInModules = false;
            bFullReloadRequired = true;
        }
        else
        {
            SwapInModules(CompiledModules, DiscardedModules);
            ClassGenerator.PerformFullReload();
        }
        break;
}
```

[2] UnLua 与 puerts 的热重载入口对比：

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp
// 函数: FLuaEnv::HotReload
// 位置: 448-450，Lua 模块级 reload 入口
// ============================================================================
void FLuaEnv::HotReload()
{
    DoString("UnLua.HotReload()"); // ★ 入口非常轻，粒度取决于 Lua 侧实现
}

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp
// 函数: FPuertsEditorModule::OnPostEngineInit
// 位置: 116-150，文件监听 + source reload
// ============================================================================
SourceFileWatcher = MakeShared<PUERTS_NAMESPACE::FSourceFileWatcher>(
    [this](const FString& InPath)
    {
        if (JsEnv.IsValid())
        {
            TArray<uint8> Source;
            if (FFileHelper::LoadFileToArray(Source, *InPath))
            {
                JsEnv->ReloadSource(InPath, puerts::PString((const char*) Source.GetData(), Source.Num()));
                // ★ 直接把变更文件送进 JS runtime；速度快，但 reload 语义不靠统一枚举表达
            }
        }
    });
```

### 对比矩阵

| 功能点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 文件变更检测入口 | Full | Partial | Partial | Full | Partial | Full |
| `Soft` / `Full` 明确分级 | Full | Partial | None | Partial | None | Full |
| PIE 中延迟 full reload | Full | Partial | None | Partial | None | Full |
| 失败时保留旧代码继续运行 | Full | Partial | Partial | Partial | Partial | Full |
| reload 后显式 rebind contract | Partial | Partial | None | Full | Partial | Partial |

### 小结与建议

- Angelscript 在 D4 上的主问题不是“没有热重载”，而是“reload 后缺少像 puerts 那样显式的 rebind 通知”。建议优先补这个闭环，优先级 `P1`。
- 当前 `ReloadRequirement` 梯度非常值得保留，这部分质量已经高于大多数参考实现，优先级 `P0`。
- 如果要继续演进，最值得吸收的是 puerts 的 `source reload -> class rebind` 闭环，而不是回退到 UnLua 的纯脚本级 reload。

## [D5] 调试与开发体验

### 各插件实现概览

```
Debug Experience
HZ/AS : custom debug protocol + variables + goto definition + asset db
UC    : Mono debugger agent (dt_socket)
UL    : Lua stack/callstack/value inspection helpers
PU    : V8 inspector over websocket
SL    : remote profiler server
```

调试能力上没有绝对单一冠军，而是两条路线：当前 Angelscript / Hazelight 走“UE 内自定义协议 + 编辑器能力”；UnrealCSharp / puerts 走“直接复用现成 VM 调试协议”；UnLua / sluaunreal 更偏运行时观测。

### 详细对比

#### 调试协议

- 当前 Angelscript 的 `AngelscriptDebugServer.h:25-80` 明确列出 `Diagnostics`、`RequestCallStack`、`Variables`、`Evaluate`、`GoToDefinition`、`AssetDatabase`、`SetDataBreakpoints` 等消息类型。这不是单一断点调试，而是把代码导航、资源查询、变量查看都塞进一条自定义协议。
- Hazelight 的 `AngelscriptDebugServer.h:23-77` 与当前几乎同源。
- UnrealCSharp 在 `FMonoDomain.cpp:93-115` 里直接配置 `--debugger-agent=transport=dt_socket,server=y,...`，属于“把调试交给 Mono debugger”。优点是标准工具能直接接；缺点是 UE 资产层导航能力不在协议里。
- puerts 的 `V8InspectorImpl.cpp:282-324` 则直接建立 V8 Inspector websocket server，天然适配 Chrome DevTools 一类工具。

#### 变量查看与调用栈

- 当前 Angelscript 有完整 `RequestVariables` / `Variables` / `RequestCallStack` / `CallStack` 消息，对应的 contract 清晰。
- UnLua 的 `UnLuaDebugBase.h:29-95` 提供 `FLuaDebugValue`、`GetStackVariables()`、`GetLuaCallStack()`，在 Lua 栈调试上非常实用，但协议层没有 Angelscript / puerts 那么完整。
- sluaunreal 本轮主要定位到 `slua_profile` 的远程 profiler，而不是完整 source-level debugger，因此在 D5 更适合判 `Partial`。

#### IDE 与导航集成

- 当前 Angelscript 的 `AngelscriptSourceCodeNavigation.cpp:6-116` 能对 class/function/property/struct 调用 `code --goto`，把 UE 里的对象定位回脚本源文件行号。这属于“调试体验”与“IDE 导航”打通。
- UnrealCSharp 的 `UnrealCSharpBlueprintToolBar.cpp:21-83` 则提供 Blueprint 工具栏中的 `OpenFile` 与 `CodeAnalysis` 行为，说明它把 IDE 体验前移到 Blueprint editor。

[1] 当前 Angelscript 的调试消息总表：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h
// 函数: EDebugMessageType
// 位置: 25-80，调试协议消息类型
// ============================================================================
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
    SetBreakpoint,
    StepOver,
    StepIn,
    StepOut,
    RequestVariables,
    Variables,
    RequestEvaluate,
    Evaluate,
    GoToDefinition,
    AssetDatabase,
    SetDataBreakpoints,
    ClearDataBreakpoints,
}; // ★ 断点、变量、跳转、资产查询都在同一协议里
```

[2] UnrealCSharp 与 puerts 的标准 VM 调试入口：

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp
// 函数: FMonoDomain::Initialize
// 位置: 93-115，启用 Mono 调试代理
// ============================================================================
const auto Config = FString::Printf(TEXT(
    "--debugger-agent=transport=dt_socket,server=y,suspend=n,address=%s:%d"
), *UnrealCSharpSetting->GetHost(), UnrealCSharpSetting->GetPort());
mono_jit_parse_options(sizeof(Options) / sizeof(char*), Options);
mono_debug_init(MONO_DEBUG_FORMAT_MONO); // ★ 直接复用 Mono debugger 协议

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp
// 函数: V8InspectorClientImpl::V8InspectorClientImpl
// 位置: 282-324，创建 V8 inspector websocket server
// ============================================================================
V8Inspector = v8_inspector::V8Inspector::create(Isolate, this);
V8Inspector->contextCreated(v8_inspector::V8ContextInfo(InContext, CtxGroupID, CtxName));
Server.set_open_handler(std::bind(&V8InspectorClientImpl::OnOpen, this, std::placeholders::_1));
Server.set_message_handler(std::bind(&V8InspectorClientImpl::OnReceiveMessage, this, std::placeholders::_1, std::placeholders::_2));
// ★ 直接接入 V8 inspector 生态，而不是自定义协议
```

### 对比矩阵

| 功能点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 断点 / 单步 | Full | Full | Partial | Full | Partial | Full |
| 变量查看 | Full | Full | Full | Full | Partial | Full |
| 调用栈查看 | Full | Full | Full | Full | Partial | Full |
| `GoToDefinition` / 导航 | Full | Partial | Partial | Partial | None | Full |
| 资源 / 资产级调试协助 | Full | None | None | None | None | Full |
| 标准 VM 调试协议兼容 | Partial | Full | Partial | Full | None | Partial |

### 小结与建议

- Angelscript 在 D5 的优势是“UE 内体验闭环”而不是“标准协议兼容性”。这对项目开发效率很重要，优先级 `P0`。
- 最值得吸收的是 puerts / UnrealCSharp 的标准协议兼容层。建议在保留自定义协议的前提下，探索更标准的 adapter，而不是推倒重来，优先级 `P2`。
- UnrealCSharp 的 Blueprint toolbar 工作流也值得借鉴。当前 Angelscript 已有 `GoToDefinition`，下一步可以把常用操作直接放进 Blueprint / Content Browser 表面，优先级 `P1`。

## [D6] 代码生成与 IDE 支持

### 各插件实现概览

```
Codegen / IDE Artifacts
HZ : docs dump + examples
AS : UHT exporter + header signature resolver + docs dump + debug db
UC : ScriptCodeGenerator + SourceCodeGenerator + override file workflow
UL : IntelliSense generator + commandlet
PU : DeclarationGenerator + ParamDefaultValueMetas + d.ts
SL : no first-class declaration pipeline found
```

D6 是当前 Angelscript 增量最明显的维度。相对 Hazelight，本仓库已经出现 `AngelscriptUHTTool`、`DebugDatabase`、`Docs/angelscript/generated/*.hpp` 三类资产；它的问题不是“没有 IDE 支持”，而是“这些产物还没有收束成单一 contract”。

### 详细对比

#### 生成触发点

- 当前 Angelscript 在 `AngelscriptFunctionTableExporter.cs:21-53` 里把自己注册成 `UhtExporter(Name = "AngelscriptFunctionTable")`，并限制输出为 `AS_FunctionTable_*.cpp`。这意味着它已经把代码生成前移到了 UHT 阶段。
- 与之配套的 `AngelscriptHeaderSignatureResolver.cs:29-117` 会重新扫描 header，要求候选声明必须 `public` 且 `HasLinkableExport()`。这是非常关键的质量控制：不是“能找到同名函数就生成”，而是“必须能链接、能复现签名”。
- puerts 的 `ParamDefaultValueMetasModule.cpp:20-59` 把自己注册成 `ScriptGenerator`，同时 `TemplateBindingGenerator.cpp:193-216` 再把已注册 class 统一输出到 `Typing/cpp/index.d.ts`。它的优势在“UE 元数据生成器 + TS 声明生成器”组合得更完整。
- UnLua 的 `UnLuaIntelliSenseGenerator.cpp:143-166` 与 `UnLuaIntelliSenseCommandlet.cpp:29-120` 则强调“把 UField/UBlueprint 导出成 IntelliSense 文件”，更偏 IDE 体验而不是构建期 contract。

#### 产物类型

- 当前 Angelscript 至少有三类产物：`AS_FunctionTable_*.cpp`、`Docs/angelscript/generated/*.hpp`、debug database。三者分别服务于绑定、API 可读性与调试。
- puerts 的核心产物是 `d.ts` 与 default-value metadata，天然更友好于 TypeScript IDE。
- UnrealCSharp 的强项不在单一产物文件，而在 `ScriptCodeGenerator` / `SourceCodeGenerator` / override file workflow 的闭环。
- sluaunreal 本轮未定位到与上述几家对等的 first-class 声明生成链，因此应判 `None`，不是“质量差异”。

#### 跳转定义与可链接性

- 当前 `AngelscriptHeaderSignatureResolver.cs` 明确区分 `class-range`、`declaration-missing`、`non-public`、`unexported-symbol`、`overloaded-unresolved`。这说明它非常关注“IDE 看到的声明是否真能连到 C++ 符号”。
- UnLua 与 puerts 的类型提示文件更偏“补全友好”，但像当前 Angelscript 这样把 linkable export 校验显式写进 resolver 的做法，本轮只在当前实现中看得最清楚。

[1] 当前 Angelscript 的 UHT exporter 与签名解析器：

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs
// 函数: Export
// 位置: 21-53，注册 Angelscript UHT exporter
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
    Console.WriteLine("... wrote {0} module files.", generatedFileCount);
    // ★ 这里不是简单吐文本，而是明确把输出接进编译产物
}

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs
// 函数: TryResolveSignature / HasLinkableExport
// 位置: 29-117，校验候选声明是否可链接
// ============================================================================
if (publicCandidates.Count == 0)
{
    failureReason = "non-public";
    return false; // ★ 不接受非 public 声明
}

if (!HasLinkableExport(classObj, classDeclaration, candidate.Declaration))
{
    failureReason = "unexported-symbol";
    return false; // ★ 不只是同名，还必须是可链接导出
}

if (classObj.HeaderFile?.Module?.ShortName.Equals("AngelscriptRuntime", StringComparison.OrdinalIgnoreCase) == true)
{
    return true; // ★ 自身 runtime 特判，提高内部函数表生成成功率
}
```

[2] puerts 与 UnLua 的 IDE 产物路径：

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/TemplateBindingGenerator.cpp
// 函数: UTemplateBindingGenerator::Gen_Implementation
// 位置: 193-216，输出 TypeScript 声明文件
// ============================================================================
PUERTS_NAMESPACE::ForeachRegisterClass(
    [&](const PUERTS_NAMESPACE::JSClassDefinition* ClassDefinition)
    {
        if (ClassDefinition->TypeId && ClassDefinition->ScriptName)
        {
            Gen.GenClass(ClassDefinition); // ★ 把已注册 class 聚合进统一的 d.ts
        }
    });

const FString FilePath = FPaths::ProjectDir() / TEXT("Typing/cpp/index.d.ts");
FFileHelper::SaveStringToFile(Gen.Output.Buffer, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp
// 函数: FUnLuaIntelliSenseGenerator::Export
// 位置: 143-166，导出单个 UField 的 IntelliSense 文件
// ============================================================================
FString FileName = UnLua::IntelliSense::GetTypeName(Field);
if (FileName.EndsWith("_C"))
    FileName.LeftChopInline(2);
const FString Content = UnLua::IntelliSense::Get(Field);
SaveFile(ModuleName, FileName, Content); // ★ 更偏 IDE 补全资产，而不是编译期 contract
```

### 对比矩阵

| 功能点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 构建期代码生成入口 | Partial | Full | Partial | Full | None | Full |
| IDE 类型声明 / 提示文件 | Partial | Full | Full | Full | None | Full |
| 默认参数 / 辅助元数据产物 | None | Partial | Partial | Full | None | Partial |
| 跳转定义支持 | Partial | Full | Partial | Partial | None | Full |
| 链接可见性校验 | None | Partial | None | Partial | None | Full |

### 小结与建议

- D6 是当前 Angelscript 最值得继续投入的方向，因为它已经具备 UHT exporter、header resolver、docs dump 三个基础件，优先级 `P0`。
- 最值得吸收的是 puerts 的“生成器模块显式化 + 单一产物入口”与 UnrealCSharp 的“生成与打开文件/分析工作流联动”，优先级 `P1`。
- 当前最大的 gap 不是没有生成，而是产物 contract 分散。建议把 `FunctionTable`、`docs.hpp`、`debug database` 组织成一套对外可描述的 artifact family，优先级 `P1`。

## [D7] 编辑器集成

### 各插件实现概览

```
Editor Integration
HZ/AS : menu extension + source navigation + editor examples
UC    : Blueprint toolbar + open file + code analysis
UL    : save/package hooks + IntelliSense commandlet
PU    : editor compiler + source watcher + code analyze
SL    : profiler panel / transport
```

如果只看“能不能在编辑器里动起来”，当前 Angelscript 已经很强。它不仅有 source navigation，还有大量 `LevelEditor` / `ToolMenu` 扩展点；不足之处反而是缺少像 UnrealCSharp 那种更聚焦 Blueprint authoring 的高频按钮。

### 详细对比

#### 菜单、面板与扩展点

- 当前 Angelscript 的 `ScriptEditorMenuExtension.cpp:900-989` 持续往 `LevelViewport`、toolbar menu 等多个入口注册 extender，说明脚本类本身可以成为编辑器 UI 扩展的 owner。
- Hazelight 的 `Script-Examples/EditorExamples` 目录也说明其编辑器方向并非空白，但当前仓库把这条链继续工程化了。
- UnrealCSharp 在 `UnrealCSharpBlueprintToolBar.cpp:21-40` 中把 Blueprint editor 扩展为带 `OpenFile`、`CodeAnalysis`、`OverrideBlueprint` 的工作流。它不是菜单面最广，但更贴近 Blueprint 作者的常用路径。

#### 资产保存与命令行工具

- UnLua 的 `UnLuaEditorModule.cpp:155-188` 在包保存时对 `ULuaFunction::SuspendOverrides()` / `ResumeOverrides()` 做保护，并调用 `SetupPackagingSettings()`。这类保存兼容措施比当前 Angelscript 更偏“生产稳定性”。
- 当前 Angelscript 在 commandlet 方面更强，D9 会看到它已经能通过 `UAngelscriptTestCommandlet` 直接跑脚本测试。

#### 导航能力

- 当前 `AngelscriptSourceCodeNavigation.cpp:6-116` 能对 class/function/property/struct 执行 `code --goto`，并根据脚本类元数据直接定位行号。
- UnrealCSharp 更依赖 `FSourceCodeNavigation::OpenSourceFile()` 与 override file workflow。

[1] 当前 Angelscript 的编辑器扩展入口：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp
// 函数: 菜单扩展注册分支
// 位置: 900-989，LevelEditor/Viewport menu extender 注册
// ============================================================================
auto Delegate = FLevelEditorModule::FLevelEditorMenuExtender::CreateLambda(
    [CDO](const TSharedRef<FUICommandList> CommandList) -> TSharedRef<FExtender>
    {
        return CDO->Extend(CommandList, FExtenderSelection());
    }
);
LevelEditorModule.GetAllLevelViewportOptionsMenuExtenders().Add(Delegate);

LevelEditorModule.GetAllLevelViewportShowMenuExtenders().Add(Delegate);
LevelEditorModule.GetAllLevelEditorToolbarViewMenuExtenders().Add(Delegate);
LevelEditorModule.GetAllLevelEditorToolbarBuildMenuExtenders().Add(Delegate);
LevelEditorModule.GetAllLevelEditorToolbarCompileMenuExtenders().Add(Delegate);
// ★ 同一脚本扩展对象可挂到多个 LevelEditor 入口
```

[2] UnrealCSharp 与 UnLua 的编辑器辅助：

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp
// 函数: FUnrealCSharpBlueprintToolBar::Initialize / BuildAction
// 位置: 21-83，Blueprint toolbar 行为
// ============================================================================
BlueprintEditorModule.GetMenuExtensibilityManager()->GetExtenderDelegates().Add(...);
CommandList->MapAction(FUnrealCSharpEditorCommands::Get().OpenFile, ...);
CommandList->MapAction(FUnrealCSharpEditorCommands::Get().CodeAnalysis, ...);
// ★ 重点不是扩展面多，而是把高频作者动作放到 Blueprint 工具栏

// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp
// 函数: OnPackageSaving / OnPackageSaved
// 位置: 155-182，保存期间暂停/恢复 override
// ============================================================================
for (const auto Pair : SuspendedPackages)
    ULuaFunction::SuspendOverrides(Pair.Value);

if (SuspendedPackages.Contains(Package))
{
    ULuaFunction::ResumeOverrides(SuspendedPackages[Package]);
    SuspendedPackages.Remove(Package);
}
// ★ 把编辑器保存流程与脚本 override 生命周期绑在一起
```

### 对比矩阵

| 功能点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 菜单 / 工具栏扩展 | Full | Full | Partial | Partial | Partial | Full |
| 源码导航 | Full | Full | Partial | Partial | None | Full |
| 保存 / 打包流程 editor hook | Partial | Partial | Full | Partial | Partial | Partial |
| 编辑器命令行工具 | Partial | Partial | Full | Partial | None | Full |
| 面向 Blueprint 作者的专用工作流 | Partial | Full | Partial | Partial | None | Partial |

### 小结与建议

- Angelscript 在 D7 上已经具备很强的“扩展面”，但缺的是更聚焦的“作者路径”。建议优先吸收 UnrealCSharp 的 Blueprint toolbar 组织方式，优先级 `P1`。
- UnLua 的保存期 `SuspendOverrides/ResumeOverrides` 也值得参考，尤其当 Angelscript 的脚本类与编辑器资产耦合更深之后，优先级 `P1`。
- 当前实现无需追求更多扩展入口数量，重点应转向“把最常用的生成、跳转、校验动作前置到高频 UI”。

## [D8] 性能与优化

### 各插件实现概览

```
Optimization Style
HZ/AS : native AngelScript + static bind + compile-out + bind cache
UC    : Mono domain + managed/native bridge
UL    : Lua runtime + override dispatch
PU    : V8/QuickJS switch + bytecode option + staged libs
SL    : Lua bridge + profiler
```

这一维本轮没有跑基准，只能依据实现结构做判断。按源码形态看，当前 Angelscript / Hazelight 的优势在“调用链短、bind 静态化、可编译裁剪”；puerts 的优势在“backend 选择与字节码选项”；UnrealCSharp 则以托管生态换取更重的桥接成本。

### 详细对比

#### 调用路径与裁剪

- 当前 Angelscript 在 `AngelscriptBinds.cpp:464-553` 中为 `CompileOutInTest`、`CompileOutIfNoLog`、`CompileOutAsEnsure`、`CompileOutAsCheck` 等路径设置 `compileOutType`。这说明它会在 shipping / test / simulate cooked 上主动裁掉开发期函数。
- 这类优化与 bind database 联合使用时，运行时不必保留全部开发工具入口。
- UnLua / sluaunreal 更依赖解释器与 hook dispatch，灵活但很难像 Angelscript 这样把 API 在编译期成批裁剪掉。

#### backend 与 VM 选择

- puerts 的 `JsEnv.Build.cs:399-408` 支持 `SingleThreaded`、`WITH_V8_BYTECODE`、多 V8 版本与 QuickJS 路线，是几家里在 backend 选择上最激进的。
- UnrealCSharp 使用 Mono domain，调试和语言生态很好，但 managed/native 边界天然更重。

#### 观测与优化闭环

- 当前 Angelscript 还有 `CodeCoverage/CoverageReportGenerator.cpp:25-79` 这样的覆盖率产物生成器。它不是直接性能优化，但意味着项目已经开始建设“可量化反馈”，这比单纯宣称性能更有工程价值。

[1] 当前 Angelscript 的编译裁剪：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp
// 函数: CompileOutInTest / CompileOutIfNoLog / CompileOutAsEnsure
// 位置: 464-553，按构建配置裁剪绑定函数
// ============================================================================
if (UE_BUILD_TEST || UE_BUILD_SHIPPING || (WITH_EDITOR && FAngelscriptEngine::IsSimulatingCookedForCurrentContext()))
{
    Function->compileOutType = asECompileOutType::CompileOutEntirely;
}

if (UE_BUILD_SHIPPING || (WITH_EDITOR && FAngelscriptEngine::IsSimulatingCookedForCurrentContext()))
{
    Function->compileOutType = asECompileOutType::ReplaceWithFirstParam;
}
// ★ 说明优化不是只靠运行时开关，而是提前把开发函数从脚本调用链裁掉
```

[2] puerts 的 backend / bytecode 选项：

```csharp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs
// 函数: ThirdParty / AddRuntimeDependencies
// 位置: 360-408，runtime backend 与 bytecode 选项
// ============================================================================
RuntimeDependencies.Add(DestDllPath, DllPath, StagedFileType.NonUFS);

if (SingleThreaded)
{
    PrivateDefinitions.Add("USING_SINGLE_THREAD_PLATFORM");
}

if (WithByteCode)
{
    PrivateDefinitions.Add("WITH_V8_BYTECODE"); // ★ 明确支持 V8 bytecode 路径
}
```

### 对比矩阵

| 功能点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 编译期裁剪开发函数 | Full | Partial | None | Partial | None | Full |
| 绑定缓存 / 预计算 | Full | Partial | None | Partial | None | Full |
| backend 可切换 | None | None | None | Full | None | None |
| 外部 VM 依赖 | None | Full | Full | Full | Full | None |
| 观测产物辅助优化 | Partial | Partial | Partial | Partial | Partial | Full |

### 小结与建议

- 按实现结构推断，Angelscript 的性能路线是合理的：尽量静态绑定、尽量 compile-out、尽量使用缓存。这一主线应保持，优先级 `P0`。
- 值得吸收的是 puerts 的“backend / 产物选项显式化”，不是 V8 本身。当前 Angelscript 未来若继续扩展 `StaticJIT` 或预编译路径，也应把配置项与产物显式暴露出来，优先级 `P2`。
- 本轮没有跑 benchmark，因此这里只能得出“实现结构更利于优化”，不能直接下结论说谁绝对更快。

## [D9] 测试基础设施

### 各插件实现概览

```
Test Infrastructure
HZ : examples-driven, no dedicated test module found in this pass
AS : AutomationTest + Test module + Test commandlet + Coverage
UC : no dedicated test path found in this pass
UL : TestSuite + Automation/Spec tests
PU : no first-class test module found in this pass
SL : profiler/diagnostic focused
```

这一维当前 Angelscript 的优势很突出。它已经不是“有几个示例脚本”，而是有独立 `AngelscriptTest` 模块、AutomationTest、脚本测试 commandlet，以及覆盖率 HTML 生成器。

### 详细对比

#### 测试组织

- 当前 Angelscript 在 `AngelscriptEngineCoreTests.cpp:28-122` 中直接声明多个 `IMPLEMENT_SIMPLE_AUTOMATION_TEST`，覆盖 engine create/destroy、compile snippet、execute snippet、full destroy/recreate 等核心生命周期。
- `AngelscriptTestCommandlet.cpp:5-24` 又把脚本单元测试接进了命令行入口。这样 editor 自动化与 commandlet 回归是串起来的。
- UnLua 的 `UnLuaTestSuite` 本轮已定位到大量 `IMPLEMENT_SIMPLE_AUTOMATION_TEST` 与 `BEGIN_DEFINE_SPEC`，说明它也有成熟测试资产，只是组织上更像独立测试仓段，而不是像当前 Angelscript 那样直接并入主插件模块。
- Hazelight、UnrealCSharp、puerts、sluaunreal 在本轮取证路径中未看到与当前 Angelscript 对等的“独立测试模块 + commandlet + coverage”组合，因此不应误判为“实现质量低”，而是“本轮未定位到同级基础设施”。

#### 覆盖率与反馈

- 当前 `CoverageReportGenerator.cpp:25-79` 能把命中次数写成 HTML 报告。这让脚本测试结果不仅是 pass/fail，还能落到文件/行。

[1] 当前 Angelscript 的自动化测试入口：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineCoreTests.cpp
// 函数: Automation tests declarations
// 位置: 28-122，核心生命周期测试
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAngelscriptTestModuleLifecycleTest,
    "Angelscript.TestModule.Engine.CreateDestroy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAngelscriptTestModuleCompileSnippetTest,
    "Angelscript.TestModule.Engine.CompileSnippet",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

const int CompileResult = Module->CompileFunction("CompileSnippet", Source, 0, 0, &Function);
TestEqual(TEXT("Compile test should compile the snippet successfully"), CompileResult, asSUCCESS);
// ★ 已经在核心 API 层建立可回归的 smoke test
```

[2] 当前 Angelscript 的 commandlet 与覆盖率生成：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptTestCommandlet.cpp
// 函数: UAngelscriptTestCommandlet::Main
// 位置: 5-24，命令行测试入口
// ============================================================================
if (!RunAngelscriptUnitTests(FAngelscriptEngine::Get().GetActiveModules(), &FAngelscriptEngine::Get(), 0, 0))
{
    return 2; // ★ commandlet 可直接作为自动回归入口
}

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/CoverageReportGenerator.cpp
// 函数: WriteFileCoverageReportHtml
// 位置: 25-79，HTML 覆盖率报告
// ============================================================================
if (*HitCount > 0)
{
    Line = FString::Printf(TEXT("<span class=\"covered\" title=\"The line was hit %d times\">%s</span>"), *HitCount, *Line);
}
else
{
    Line = FString::Printf(TEXT("<span class=\"not-covered\" title=\"This line was not hit\">%s</span>"), *Line);
}
// ★ 行级覆盖率能直接回流到脚本文件
```

### 对比矩阵

| 功能点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 独立测试模块 | None | None | Partial | None | None | Full |
| AutomationTest / Spec | None | None | Full | None | None | Full |
| Commandlet 测试入口 | None | None | Partial | None | None | Full |
| 覆盖率产物 | None | None | None | None | None | Full |
| 回归覆盖 engine 生命周期 | None | Partial | Partial | None | None | Full |

### 小结与建议

- D9 是当前 Angelscript 的明显强项，应继续保持“测试模块 + commandlet + coverage”的三件套，优先级 `P0`。
- 下一步最值得吸收的不是别家测试框架，而是 UnLua 那种把测试用例持续组织成更清晰 suite/spec 的方式，优先级 `P2`。
- 对于本轮未定位到测试基础设施的插件，应判为“未发现”而不是武断判差。

## [D10] 文档与示例组织

### 各插件实现概览

```
Docs / Samples
HZ : docs dump + Script-Examples/{Examples,GAS,EnhancedInput,Editor}
AS : docs dump + generated .hpp + test examples
UC : codegen-oriented docs/assets, human docs weaker in source path
UL : IntelliSense artifacts + editor commandlet
PU : d.ts as executable API catalog
SL : source-first, doc assets weak in this pass
```

D10 上的最大对比是“人读文档”与“工具消费文档”的比例。Hazelight 强在场景样例；当前 Angelscript 强在自动生成 `.hpp` API 文档；puerts 强在 `d.ts`；UnLua 强在 IntelliSense 文件。

### 详细对比

#### API 参考生成

- 当前 Angelscript 在 `AngelscriptDocs.cpp:675-755` 中遍历 `Classes`，输出 `Docs/angelscript/generated/*.hpp`。这意味着脚本 API 参考不是手写 wiki，而是从运行时 bind 信息反推出可浏览头文件。
- Hazelight 也有 `AngelscriptDocs.cpp` 同类逻辑，但当前仓库又叠加了 UHT tool 与 debug database，因此 API 文档资产比 Hazelight 更完整。
- puerts 的 `Typing/cpp/index.d.ts` 本质上也承担 API catalog 作用，只是目标读者更偏 IDE 而非人。

#### 示例组织

- Hazelight 的 `Script-Examples` 明确拆成 `Examples`、`GASExamples`、`EnhancedInputExamples`、`EditorExamples`，这在“按业务场景引导上手”上优于当前仓库。
- 当前 Angelscript 虽然也有 `Plugins/Angelscript/Source/AngelscriptTest/Examples/*`，但示例更多混在测试资产附近，不如 Hazelight 那样有明确主题目录。
- UnLua、puerts、UnrealCSharp 在本轮路径中更多呈现为工具链产物，而不是像 Hazelight 那样强主题样例树。

[1] 当前 Angelscript 的 API 文档导出：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp
// 函数: 文档导出主循环
// 位置: 675-755，生成 Docs/angelscript/generated/*.hpp
// ============================================================================
FString Filename = FPaths::ProjectDir() / TEXT("/Docs/angelscript/generated") / ClassDoc.ClassName + TEXT(".hpp");

Content += FString::Printf(TEXT("/* Class: %s \n %s */ \n class %s"),
    *ClassDoc.ClassName, *ClassDoc.Documentation, *ClassDoc.ClassName);

for (FDocProperty& PropDoc : ClassDoc.Properties)
{
    Content += FString::Printf(TEXT("\n/* Variable: %s \n %s */\n"), *PropDoc.Name, *PropDoc.Documentation);
    Content += FString::Printf(TEXT("%s;"), *PropDoc.Declaration);
}

for (FDocFunc& FuncDoc : ClassDoc.Functions)
{
    Content += FString::Printf(TEXT("\n/* Function: %s \n %s */\n"), *FuncDoc.Name, *FuncDoc.Documentation);
    Content += FString::Printf(TEXT("%s {}"), *FuncDoc.Declaration);
}

FFileHelper::SaveStringToFile(Content, *Filename); // ★ 直接生成可浏览头文件形式的 API 参考
```

[2] puerts 的声明型 API 目录：

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/TemplateBindingGenerator.cpp
// 函数: UTemplateBindingGenerator::Gen_Implementation
// 位置: 193-216，输出 Typing/cpp/index.d.ts
// ============================================================================
PUERTS_NAMESPACE::ForeachRegisterClass(...);
const FString FilePath = FPaths::ProjectDir() / TEXT("Typing/cpp/index.d.ts");
FFileHelper::SaveStringToFile(Gen.Output.Buffer, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
// ★ 产物更偏 IDE/类型系统消费，而不是直接给人阅读
```

### 对比矩阵

| 功能点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 自动 API 参考导出 | Full | Partial | Partial | Full | None | Full |
| 强主题示例目录 | Full | Partial | Partial | Partial | Partial | Partial |
| 生成型 IDE 文档资产 | Partial | Full | Full | Full | None | Full |
| 文档产物直接来自源码 | Full | Full | Full | Full | Partial | Full |
| 上手路径按场景组织 | Full | Partial | Partial | Partial | Partial | Partial |

### 小结与建议

- Angelscript 在 D10 上最应吸收的是 Hazelight 的“按场景组织示例”，而不是继续只增加生成文档数量，优先级 `P1`。
- 当前 `.hpp` 文档导出已经很好，建议继续保留；但最好把测试例子与教学例子分离，避免“文档即测试资产”混用，优先级 `P1`。
- puerts 说明了声明文件也可以成为文档资产。当前 Angelscript 若后续统一 D6 合同，D10 也会随之受益。

## [D11] 部署与打包

### 各插件实现概览

```
Packaging / Deployment
HZ/AS : cook-time bind cache + headers cache
UC    : runtime/core split, no code protection path found in this pass
UL    : platform whitelist + Lua runtime dependency + packaging settings
PU    : staged runtime deps + V8/QuickJS + bytecode option
SL    : platform Lua libs + profiler
```

这一维 puerts 明显最强，因为它把 runtime 依赖分发、平台库、bytecode 选项都写进了 Build.cs。当前 Angelscript / Hazelight 不是完全没有部署策略，而是更偏“cooked bind cache”，对脚本保护、签名、字节码分发的显式程度弱很多。

### 详细对比

#### cook 期缓存

- 当前 Angelscript 在 `AngelscriptBindDatabase.cpp:42-101` 中把 bind database 序列化到 `Path`，cook 期间写 `Binds.Cache`，并在 editor 下额外写 `.Headers`。这意味着它的部署思路是“先把绑定关系预烘焙”，而不是让运行时每次全量重新扫描。
- Hazelight 的 `AngelscriptBindDatabase.cpp:31-75` 是同一路线，并且还有 `AngelscriptLoader` 模块承接 runtime 侧加载。这说明当前实现不是“没有部署方案”，而是把 loader 合并进了其他模块。

#### 平台与运行库分发

- puerts 在 `JsEnv.Build.cs:360-408` 中显式 `RuntimeDependencies.Add(..., StagedFileType.NonUFS)`，并对 Windows/macOS 等平台分别布置 V8 库，同时提供 `WITH_V8_BYTECODE` 选项。这是几家里最完整的部署 contract。
- UnLua 的 `UnLua.uplugin:23-40` 给出 `WhitelistPlatforms`，`ThirdParty/Lua/Lua.Build.cs:29-80` 把 Lua 动态库当 runtime dependency 处理，属于“基础分发完整，但脚本保护策略不突出”。
- sluaunreal 也有平台 Lua 库分发，但本轮未定位到脚本打包加密或签名链路。
- UnrealCSharp 本轮主要定位到模块拆分与 Mono domain，未定位到 assembly 签名/加密路径，因此这里只能判 `Partial`。

#### 脚本保护与版本兼容

- puerts 是本轮唯一在源码中清晰暴露 bytecode/staged backend 选项的实现。
- 当前 Angelscript / Hazelight / UnLua / sluaunreal 在本轮源码中都没有清晰定位到脚本加密或签名机制，因此应判为 `Partial` 或 `None`，不能武断写成“完全没有部署能力”。

[1] 当前 Angelscript 的 cook 缓存：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp
// 函数: FAngelscriptBindDatabase::Save
// 位置: 42-101，cook 期写 bind cache 与 header cache
// ============================================================================
Serialize(Writer);
bool bSaveSuccess = FFileHelper::SaveArrayToFile(Data, *Path);
if (IsRunningCookCommandlet())
{
    if (!bSaveSuccess)
    {
        UE_LOG(Angelscript, Error, TEXT("Unable to write the Script/Binds.Cache file during cook"));
    }
}

for (auto& Bind : Classes)
{
    UClass* Class = FindObject<UClass>(nullptr, *Bind.UnrealPath);
    if (FSourceCodeNavigation::FindClassHeaderPath(Class, HeaderPath) && IFileManager::Get().FileSize(*HeaderPath) != INDEX_NONE)
        Headers.Add(FAngelscriptClassHeader{Bind.UnrealPath, HeaderPath});
}

FFileHelper::SaveArrayToFile(HeaderData, *(Path + TEXT(".Headers")));
// ★ cook 后运行时可以直接消费缓存，而不是重新全量扫描 bind 信息
```

[2] puerts 的运行库 staged 与 bytecode 开关：

```csharp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs
// 函数: AddRuntimeDependencies / ThirdParty
// 位置: 360-408，运行库 staged 与 bytecode 选项
// ============================================================================
var DestDllPath = Path.Combine("$(BinaryOutputDir)", DllName);
RuntimeDependencies.Add(DestDllPath, DllPath, StagedFileType.NonUFS);

if (WithByteCode)
{
    PrivateDefinitions.Add("WITH_V8_BYTECODE");
}
// ★ 部署 contract 被明确写进构建脚本
```

### 对比矩阵

| 功能点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| cook 期脚本 / 绑定缓存 | Full | Partial | Partial | Partial | Partial | Full |
| runtime 依赖 staged 分发 | Partial | Partial | Full | Full | Full | Partial |
| 平台 whitelist / 适配声明 | Partial | Partial | Full | Full | Partial | Partial |
| 独立 loader / 运行时装载模块 | Full | Partial | None | Partial | None | None |
| 字节码 / 加密 / 签名显式方案 | None | None | None | Full | None | None |

### 小结与建议

- 当前 Angelscript 在 D11 上并非空白，`Binds.Cache` 就是部署 contract 的一部分；但这条 contract 更偏“绑定缓存”，不是完整的“脚本分发与保护”体系，优先级 `P1`。
- puerts 最值得借鉴的是“把部署选项显式写进 Build.cs 并形成 staged contract”，优先级 `P1`。
- 如果 Angelscript 后续要支持更强的预编译或脚本保护，应优先在现有 cook cache 基础上演进，而不是另起一套完全平行的打包链。

## 总结

横向看下来，当前 Angelscript 最强的维度是 `D2 反射绑定机制`、`D4 热重载`、`D9 测试基础设施`；最值得继续深挖的是 `D6 代码生成与 IDE 支持`。Hazelight 仍然是其最近的血缘参考，但当前实现已经明显往“更强内聚、更强测试、更强工具化”方向偏移。

最值得吸收的经验不是某一个插件的整套技术栈，而是三类局部模式：

- `UnrealCSharp`: 用更统一的 registry family 和 editor authoring workflow 管理生成物与导航。
- `puerts`: 用显式 generator / staged runtime / rebind contract 把工具链与部署 contract 写透。
- `Hazelight`: 保持主题化示例与可选能力拆分，让主 runtime 不至于继续膨胀。

按优先级建议：

- `P0`: 补齐 `UInterface` 进入统一 bind owner；保持现有 `ReloadRequirement` 与测试三件套。
- `P1`: 统一 D6 产物 contract；把高频生成/跳转/分析动作前移到编辑器；重新评估可选功能模块拆分。
- `P2`: 评估更标准的调试 adapter、rebind 通知闭环，以及更显式的预编译/打包选项。

---

## 深化分析 (2026-04-08 18:31:37)

这一轮不再重复上一轮已经写清的“有没有断点 / 有没有生成器 / 有没有菜单”。补充的重点是三个更容易误判的问题：

- `D5`: 调试 contract 到底由插件自己定义，还是借宿主 VM 协议。
- `D6`: IDE / 代码生成产物谁是权威源，产物之间是否闭环。
- `D7`: 编辑器 integration 是“资产中心”还是“工具中心”。

### [D5] 调试 contract 的归属

#### 各插件实现概览

```
[D5-Deep] Debug Contract Ownership
HZ : custom protocol -> DebugDatabaseSettings -> AssetDatabase stream
AS : custom protocol -> envelope helper -> DebugDatabaseSettings -> protocol tests
UC : Mono debugger-agent (dt_socket) -> IDE owns transport
UL : Lua stack/value helper API -> host IDE/plugin decides UI
PU : V8 Inspector websocket -> DevTools owns protocol
SL : profiler TCP stream -> observation only
```

这一轮新增结论不是“谁能下断点”，而是“调试 contract 谁说了算”。Hazelight / 当前 Angelscript 仍然是最强的 `UE-owned protocol` 路线；UnrealCSharp / puerts 明确把 transport contract 交给 Mono / V8；UnLua / sluaunreal 只把“观测能力”暴露出来，完整 debugger UI 并不在当前仓内。

#### 详细对比

##### 子维度 1：协议 owner

- 当前 Angelscript 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:52-107` 把 `length + type + body` 抽成 `SerializeDebugMessageEnvelope()` / `TryDeserializeDebugMessageEnvelope()`，并显式处理半包与非法长度。这说明协议 framing 已经是独立 contract，而不是散落在 `SendMessage*` 模板里的细节。
- Hazelight 对应 `J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Debugging/AngelscriptDebugServer.cpp:1368-1393` 仍然是同一协议家族，但更多是“内联发送 + 内联 database settings”。当前仓库是在同一血缘上把 transport contract 做硬了，而不是协议代际分叉。
- UnrealCSharp 在 `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp:93-116` 直接开启 `--debugger-agent=transport=dt_socket`；puerts 在 `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:301-327` 直接创建 `V8Inspector` websocket server。两者的共同点是：插件不拥有线协议，插件只拥有“把 VM 暴露给标准 debugger”的接入点。
- UnLua 只提供 `GetStackVariables()` / `GetLuaCallStack()` 这类调试读取 API，源码位于 `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDebugBase.h:84-91` 与 `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaDebugBase.cpp:615-687`。这条线的 owner 是“宿主 IDE 或外部调试插件”，不是仓内 protocol。
- sluaunreal 当前能定位到的是 profiler transport，而不是 source-level debugger。`Reference/sluaunreal/Plugins/slua_unreal/Source/slua_profile/Private/slua_remote_profile.cpp:22-60` 只暴露 `slua.ProfilerPort` 与 `FTcpListener`。

##### 子维度 2：调试数据库是否只是数据，还是语言模式 contract

- 当前 Angelscript 的 `SendDebugDatabase()` 不只是发 symbol dump。`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:1493-1505` 先把 `bAutomaticImports`、`bFloatIsFloat64`、`bUseAngelscriptHaze`、`StaticClass` 策略序列化成 `FAngelscriptDebugDatabaseSettings` 再发送。这说明 debug database 已经承担“把脚本语言模式同步给 IDE”的职责。
- Hazelight 对应 `J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Debugging/AngelscriptDebugServer.cpp:1368-1383` 也会先发 `DebugDatabaseSettings`，但字段集不同：Hazelight 仍然暴露 `bExposeGlobalFunctions` 与 `bDeprecateActorGenerics`。这说明当前插件不是简单照搬，而是把 language-mode contract 改成了更贴近当前语义系统的配置面。
- UnrealCSharp / puerts 的调试配置更多留在 VM 初始化或 inspector 端点层。`FMonoDomain.cpp:93-116` 里 host/port 只影响 debugger-agent；`V8InspectorImpl.cpp:301-327` 里 port 只影响 websocket server。它们没有像 Angelscript 这样把语言语义再序列化进单独的 debug metadata 包。

##### 子维度 3：协议可回归性

- 当前 Angelscript 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp:1-77,230-245` 明确把 `Debug.Protocol.*.RoundTrip` 做成自动化测试，连 `DebugDatabaseSettings` 都要求往返一致。这里的关键不是“有测试”三个字，而是调试协议已经被当成正式 API 来守护。
- 本轮没有在 Hazelight、UnrealCSharp、UnLua、puerts、sluaunreal 的当前快照里定位到与之同等级的“协议 round-trip 自动化测试”。
- 因此 D5 上最强的不是“支持断点最多”，而是“当前 Angelscript 把 UE-owned debug contract 做成了带版本、带 framing、带测试的协议资产”。

[1] 当前 Angelscript 把 debugger envelope 抽成独立 helper，而不是继续把 framing 混在发送模板里：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 函数: SerializeDebugMessageEnvelope / TryDeserializeDebugMessageEnvelope
// 位置: 52-107，调试协议 framing helper
// ============================================================================
bool SerializeDebugMessageEnvelope(EDebugMessageType MessageType, const TArray<uint8>& Body, TArray<uint8>& OutBuffer)
{
	OutBuffer.Reset();
	FMemoryWriter Writer(OutBuffer);
	const int32 MessageLength = static_cast<int32>(sizeof(uint8)) + Body.Num();
	const uint8 MessageTypeByte = static_cast<uint8>(MessageType);
	Writer << const_cast<int32&>(MessageLength);
	Writer << const_cast<uint8&>(MessageTypeByte);
	OutBuffer.Append(Body); // ★ 统一写 length + type + body
	return true;
}

bool TryDeserializeDebugMessageEnvelope(TArray<uint8>& InOutBuffer, FAngelscriptDebugMessageEnvelope& OutEnvelope, bool& bOutHasEnvelope, FString* OutError)
{
	if (MessageLength <= 0 || MessageLength > 1024 * 1024)
	{
		*OutError = FString::Printf(TEXT("Received debugger envelope with invalid message length %d."), MessageLength);
		return false; // ★ 非法长度直接视为协议错误
	}

	if (InOutBuffer.Num() < TotalEnvelopeSize)
	{
		return true; // ★ 半包不报错，等待后续数据
	}

	InOutBuffer.RemoveAt(0, TotalEnvelopeSize, EAllowShrinking::No);
	bOutHasEnvelope = true;
	return true;
}
```

[2] 当前 Angelscript 与 Hazelight 都把 `DebugDatabaseSettings` 放进协议，但字段集已经分叉，说明 IDE contract 在跟随语言语义演化：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 函数: FAngelscriptDebugServer::SendDebugDatabase
// 位置: 1493-1505，当前插件发送当前语义模式
// ============================================================================
FAngelscriptDebugDatabaseSettings DebugSettings;
DebugSettings.bAutomaticImports = FAngelscriptEngine::Get().ShouldUseAutomaticImportMethod();
DebugSettings.bFloatIsFloat64 = GetDefault<UAngelscriptSettings>()->bScriptFloatIsFloat64;
DebugSettings.bUseAngelscriptHaze = !!WITH_ANGELSCRIPT_HAZE;
DebugSettings.bDeprecateStaticClass = GetDefault<UAngelscriptSettings>()->StaticClassDeprecation == EAngelscriptStaticClassMode::Deprecated;
DebugSettings.bDisallowStaticClass = GetDefault<UAngelscriptSettings>()->StaticClassDeprecation == EAngelscriptStaticClassMode::Disallowed;
SendMessageToClient(Client, EDebugMessageType::DebugDatabaseSettings, DebugSettings);

// ============================================================================
// 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Debugging/AngelscriptDebugServer.cpp
// 函数: FAngelscriptDebugServer::SendDebugDatabase
// 位置: 1368-1383，Hazelight 的旧字段集
// ============================================================================
FAngelscriptDebugDatabaseSettings DebugSettings;
DebugSettings.bAutomaticImports = true;
DebugSettings.bFloatIsFloat64 = GetDefault<UAngelscriptSettings>()->bScriptFloatIsFloat64;
DebugSettings.bUseAngelscriptHaze = !!WITH_ANGELSCRIPT_HAZE;
DebugSettings.bDeprecateStaticClass = GetDefault<UAngelscriptSettings>()->StaticClassDeprecation == EAngelscriptDeprecationMode::Deprecated;
DebugSettings.bDisallowStaticClass = GetDefault<UAngelscriptSettings>()->StaticClassDeprecation == EAngelscriptDeprecationMode::Disallowed;
DebugSettings.bExposeGlobalFunctions = GetDefault<UAngelscriptSettings>()->bExposeGlobalFunctionsToOtherScriptFiles;
DebugSettings.bDeprecateActorGenerics = GetDefault<UAngelscriptSettings>()->DeprecateOldActorGenericMethods == EAngelscriptDeprecationMode::Deprecated;
// ★ 两边都把 settings 当协议一部分，但当前插件的 settings 已经换代
```

[3] 当前 Angelscript 把调试协议当正式 API 回归；UnrealCSharp / puerts 则明确把 transport 交给宿主 VM：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp
// 函数: FAngelscriptDebugProtocolDatabaseSettingsRoundTripTest::RunTest
// 位置: 230-245，协议字段往返测试
// ============================================================================
FAngelscriptDebugDatabaseSettings Message;
Message.bAutomaticImports = true;
Message.bFloatIsFloat64 = true;
Message.bUseAngelscriptHaze = false;

const FAngelscriptDebugDatabaseSettings RoundTripped = RoundTripMessage(Message);
TestEqual(TEXT("Debug.Protocol.DatabaseSettings.RoundTrip should preserve automatic imports"), RoundTripped.bAutomaticImports, Message.bAutomaticImports);
TestEqual(TEXT("Debug.Protocol.DatabaseSettings.RoundTrip should preserve float width"), RoundTripped.bFloatIsFloat64, Message.bFloatIsFloat64);

// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp
// 函数: FMonoDomain::Initialize
// 位置: 93-116，调试 transport 委托给 Mono
// ============================================================================
const auto Config = FString::Printf(TEXT(
	"--debugger-agent=transport=dt_socket,server=y,suspend=n,address=%s:%d"
), *UnrealCSharpSetting->GetHost(), UnrealCSharpSetting->GetPort());
mono_jit_parse_options(sizeof(Options) / sizeof(char*), Options);
mono_debug_init(MONO_DEBUG_FORMAT_MONO); // ★ 直接进入 Mono debugger agent

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp
// 函数: V8InspectorClientImpl::V8InspectorClientImpl
// 位置: 301-327，调试 transport 委托给 V8 Inspector
// ============================================================================
V8Inspector = v8_inspector::V8Inspector::create(Isolate, this);
Server.set_open_handler(std::bind(&V8InspectorClientImpl::OnOpen, this, std::placeholders::_1));
Server.set_message_handler(
	std::bind(&V8InspectorClientImpl::OnReceiveMessage, this, std::placeholders::_1, std::placeholders::_2));
Server.listen(Port); // ★ 标准 websocket inspector 通道
```

#### 对比矩阵（本轮新增观察点）

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 插件自定义调试消息枚举 | Full | None | None | None | Partial | Full |
| `DebugDatabase` / `AssetDatabase` 进入调试通道 | Full | None | None | None | None | Full |
| 语言模式设置随协议下发 | Full | None | None | None | None | Full |
| 协议 round-trip 自动化测试 | None | None | None | None | None | Full |
| 标准 VM 调试通道直连 | None | Full | Partial | Full | None | None |

#### 小结与建议

- 这一轮更明确地说明：Angelscript 在 D5 的核心强项不是“功能点多”，而是“插件自己拥有调试 contract”。这条线应继续保持，优先级 `P0`。
- 最值得吸收的不是放弃自定义协议，而是像 UnrealCSharp / puerts 那样补一个 adapter 层，让标准 IDE 更容易接入，优先级 `P2`。
- 当前 `DebugDatabaseSettings` 已经是语言模式 contract。后续如果继续引入新的 script mode 开关，必须同步把它们当作调试协议字段来治理，而不是只改本地设置对象，优先级 `P1`。

### [D6] 生成产物的 authority

#### 各插件实现概览

```
[D6-Deep] Artifact Authority
HZ : docs.hpp + live debug database
AS : UHT function table + summary json/csv + docs.hpp + live debug database
UC : code-analysis json -> generator -> toolbar override-file map
UL : exported binding objects -> IntelliSense .lua -> menu/commandlet
PU : UHT default metas + runtime class registry -> index.d.ts
SL : external config.json -> wrapper output_dir
```

上一轮说过“Angelscript 产物分散”；这一轮可以更具体地说出：分散并不等于失控。当前 Angelscript 的强项是 **生成前的合法性筛选**，而 UnrealCSharp 的强项是 **单一索引同时驱动 generator 与 editor authoring**。puerts / UnLua 强在 IDE 友好产物，sluaunreal 则最依赖外部路径配置。

#### 详细对比

##### 子维度 1：谁决定“这条声明可以生成”

- 当前 Angelscript 的 `AngelscriptFunctionTableExporter.cs:21-53` 已经把 `BlueprintCallable / BlueprintPure` 统计、`skipped reason` 输出和 `AS_FunctionTable_*.cpp` 生成挂到 UHT exporter 上；真正关键的是 `AngelscriptHeaderSignatureResolver.cs:18-55` 会在写产物前先检查 `header-missing`、`class-range`、`declaration-missing`、`non-public`、`unexported-symbol`。它的 authority 是“头文件里可链接、可公开、可还原的声明”。
- Hazelight 在 `J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/AngelscriptDocs.cpp:668-742` 仍只有 `Docs/angelscript/generated/*.hpp` 与 debug database 一类产物；当前仓库是沿着 Hazelight 的 docs 导出链继续向前扩，而不是另起一套文档系统。
- UnrealCSharp 的 authority 不在 header 符号可见性，而在 code analysis 产物。`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:972-977` 会从 `GetCodeAnalysisPath()/OverrideFunctions.json` 读取 `OverrideFunctionsMap`。同一份索引既服务 generator，也被 editor toolbar 消费，因此它的 authority 是“静态分析产物”。
- puerts 的 authority 分成两半：`Reference/puerts/unreal/Puerts/Source/CSharpParamDefaultValueMetas/CSharpParamDefaultValueMetas.cs:25-38` 用 UHT exporter 采默认参数元数据；`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/TemplateBindingGenerator.cpp:194-216` 再从已注册 `JSClassDefinition` 集合导出 `Typing/cpp/index.d.ts`。这带来 IDE 体验，但 authority 并不单点。
- UnLua 的 authority 更贴近运行时导出对象。`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp:64-120` 直接对 `ExportedReflectedClasses`、`ExportedNonReflectedClasses`、`ExportedEnums`、`ExportedFunctions` 调 `GenerateIntelliSense()`，再落盘为 `.lua`。它不做 linkability gate，而是信任导出表本身。
- sluaunreal 的 `Reference/sluaunreal/Tools/config.json:65-79` 说明 wrapper generator 依赖 `solution_dir`、`ue_vcproj`、巨大的 `include_path` 与 `preprocess` 列表。它的 authority 是“外部工具配置是否与宿主工程对齐”，这是几家里最脆弱的一类 contract。

##### 子维度 2：产物是否形成闭环

- 当前 Angelscript 至少有四类产物：`AS_FunctionTable_*.cpp`、`AS_FunctionTable_Summary.json/.csv`、`Docs/angelscript/generated/*.hpp`、通过 debug protocol 流式发送的 debug database。问题不在“没有产物”，而在 authority 被分布在 `UHT / runtime / docs / debugger` 四条链上。
- UnrealCSharp 的闭环更紧：`OverrideFunctionsMap` 先成为 `ScriptCodeGenerator` 输入，再被 `FUnrealCSharpBlueprintToolBar` 读取，用来决定 `OpenFile / CodeAnalysis / OverrideBlueprint` 的作者路径。也就是“生成索引”和“编辑器入口”共用同一份 authority。
- UnLua 的闭环是“同一个 IntelliSense generator 既可被菜单触发，也可被 commandlet 触发，最终都写到同一套 `.lua` 文件”。`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:143-234` 与 `.../Commandlets/UnLuaIntelliSenseCommandlet.cpp:64-120` 说明这一点。
- puerts 的闭环偏 IDE：默认参数 metadata 来自 UHT exporter，但 `index.d.ts` 来自 runtime class registry。它把“参数默认值”和“类型声明”都产出了，却没有把它们再收束成一个统一 artifact family。

##### 子维度 3：当前 Angelscript 的真实优势在哪里

- 不是“产物数量最多”，而是“失败原因显式”。`AngelscriptHeaderSignatureResolver.cs:18-55` 与 `AngelscriptFunctionTableCodeGenerator.cs:166-215` 让当前仓库可以明确知道：为什么某个 `BlueprintCallable` 没进 direct bind，为什么某模块只有 stub，为什么某个符号不能链接。
- 这类 “failure reason + summary json/csv” 能力，在本轮取证范围里只有当前 Angelscript 做得最完整。
- 因此 D6 上最应吸收的不是更多文件类型，而是把这些现有 authority 再收束成单一 contract，例如一个统一的 `artifact manifest`，把 function table、docs、debug database、skipped reason 串起来。

[1] 当前 Angelscript 的 UHT exporter 不是只写 `.cpp`，还会显式输出 `skipped reason` 与 summary：

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs
// 函数: Export
// 位置: 21-53，UHT exporter 负责统计与 skipped reason 输出
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
	WriteSkippedReasonSummaryCsv(factory, skippedEntries);
	// ★ 生成链本身就带“失败可解释性”
}

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs
// 函数: WriteGenerationSummary
// 位置: 166-215，输出 summary json/csv
// ============================================================================
string summaryPath = factory.MakePath("AS_FunctionTable_Summary", ".json");
File.WriteAllText(summaryPath, summaryJson, Encoding.UTF8);
WriteModuleSummaryCsv(factory, moduleSummaries);
WriteEntryCsv(factory, csvEntries);
// ★ 不是只生成 bind shard，还会写出 direct/stub/module 级摘要
```

[2] 当前 Angelscript 的 authority 入口是“可链接声明”，而不是“同名即可”：

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs
// 函数: TryBuild
// 位置: 18-55，生成前的可链接性 gate
// ============================================================================
if (classObj.HeaderFile == null || string.IsNullOrEmpty(classObj.HeaderFile.FilePath) || !File.Exists(classObj.HeaderFile.FilePath))
{
	failureReason = "header-missing";
	return false;
}

List<CandidateDeclaration> publicCandidates = candidates.FindAll(static candidate => candidate.IsPublic);
if (publicCandidates.Count == 0)
{
	failureReason = "non-public";
	return false; // ★ 非 public 声明直接拒绝
}

if (!HasLinkableExport(classObj, classDeclaration, candidate.Declaration))
{
	failureReason = "unexported-symbol";
	return false; // ★ 同名还不够，必须能链接
}
```

[3] UnrealCSharp / puerts / UnLua / sluaunreal 的 authority 分别落在分析索引、双源产物、导出对象和外部配置：

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp
// 函数: FGeneratorCore::BeginGenerator
// 位置: 972-977，OverrideFunctionsMap 是生成 authority
// ============================================================================
OverrideFunctionsMap = FUnrealCSharpFunctionLibrary::LoadFileToArray(FString::Printf(TEXT(
	"%s/%s.json"
), *FUnrealCSharpFunctionLibrary::GetCodeAnalysisPath(), *OVERRIDE_FUNCTION));
// ★ generator 先读取 code analysis 产物，再决定 override 面

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/CSharpParamDefaultValueMetas/CSharpParamDefaultValueMetas.cs
// 函数: UnLuaDefaultParamCollectorUbtPluginExporter
// 位置: 25-38，UHT exporter 负责默认参数元数据
// ============================================================================
[UhtExporter(Name = "Puerts", Description = "Puerts Default Values Collector", Options = UhtExporterOptions.Default, ModuleName = "JsEnv")]
private static void UnLuaDefaultParamCollectorUbtPluginExporter(IUhtExportFactory factory)
{
	var paramDefaultValueMetas = new CSharpParamDefaultValueMetas(factory);
	paramDefaultValueMetas.Generate();
	paramDefaultValueMetas.OutputIfNeeded(bHasGameRuntime);
}

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/TemplateBindingGenerator.cpp
// 函数: UTemplateBindingGenerator::Gen_Implementation
// 位置: 194-216，runtime registry 导出 index.d.ts
// ============================================================================
PUERTS_NAMESPACE::ForeachRegisterClass(
	[&](const PUERTS_NAMESPACE::JSClassDefinition* ClassDefinition)
	{
		if (ClassDefinition->TypeId && ClassDefinition->ScriptName)
			Gen.GenClass(ClassDefinition);
	});
const FString FilePath = FPaths::ProjectDir() / TEXT("Typing/cpp/index.d.ts");
FFileHelper::SaveStringToFile(Gen.Output.Buffer, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp
// 函数: UUnLuaIntelliSenseCommandlet::Main
// 位置: 64-120，直接从导出对象生成 IntelliSense 文件
// ============================================================================
Pair.Value->GenerateIntelliSense(GeneratedFileContent);
SaveFile(ModuleName, Pair.Key, GeneratedFileContent);
Enum->GenerateIntelliSense(GeneratedFileContent);
SaveFile(ModuleName, Enum->GetName(), GeneratedFileContent);
SaveFile(ModuleName, TEXT("GlobalFunctions"), GeneratedFileContent);

// ============================================================================
// 文件: Reference/sluaunreal/Tools/config.json
// 位置: 65-79，外部 generator 的 authority 落在路径配置
// ============================================================================
"output_dir": "{solution_dir}/Plugins/slua_unreal/Source/slua_unreal/Private/",
"ue_vcproj": "{solution_dir}/Intermediate/ProjectFiles/UE5.vcxproj",
"include_path": "...",
"preprocess": "...",
// ★ 只要 solution_dir / include / preprocess 偏掉，生成 contract 就会漂移
```

#### 对比矩阵（本轮新增观察点）

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 一等公民 UHT exporter 入口 | None | Partial | Partial | Full | None | Full |
| 生成前可链接性校验 | None | Partial | None | None | None | Full |
| `summary json/csv` 一类生成摘要 | None | Partial | None | None | None | Full |
| 同一 authority 同时服务 generator 与 editor UI | None | Full | Full | Partial | None | Partial |
| runtime 可流式发送 IDE 元数据 | Full | None | None | None | None | Full |
| 高度依赖外部路径配置的 generator | None | None | None | None | Full | None |

#### 小结与建议

- 当前 Angelscript 在 D6 上的最强点已经不是“会生成”，而是“会告诉你为什么没生成”。这条线应继续强化，优先级 `P0`。
- 最值得吸收的是 UnrealCSharp 的单一 authority 闭环：同一份分析索引既驱动 generator，也驱动 editor toolbar。当前 Angelscript 适合把 `FunctionTable summary / docs / debug database` 串成统一 manifest，优先级 `P1`。
- puerts / UnLua 说明 IDE 友好产物不一定要等同于 build contract。当前 Angelscript 没必要放弃 linkability gate，但应该把已有 artifact family 更清晰地对外命名，优先级 `P1`。

### [D7] 编辑器 integration 的重心

#### 各插件实现概览

```
[D7-Deep] Editor Surface Orientation
HZ : ContentBrowserDataSource + CreateBlueprint popup
AS : ContentBrowserDataSource + CreateBlueprint popup + wide menu extenders
UC : Blueprint toolbar -> OpenFile / CodeAnalysis / OverrideBlueprint
UL : menu + blueprint toolbar + save/package hooks + IntelliSense init
PU : editor-owned JsEnv starts CodeAnalyze tool runtime
SL : hidden Nomad profiler tab + ticker
```

上一轮 D7 更偏“有多少菜单入口”。这一轮补出来的差异是：Hazelight / 当前 Angelscript 的 editor integration 其实是 **资产中心**；UnrealCSharp 是 **Blueprint 作者路径中心**；UnLua 是 **保存/打包稳定性中心**；puerts / sluaunreal 则更像 **在编辑器里托管一个工具 runtime**。

#### 详细对比

##### 子维度 1：资产是否进入编辑器的一等视图

- 当前 Angelscript 在 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:111-118` 会在 engine init 后创建 `UAngelscriptContentBrowserDataSource` 并激活 `AngelscriptData`。`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:16-28` 又把脚本资产映射到 `/All/Angelscript/...` 虚拟路径。也就是说，它不是只给菜单加按钮，而是把脚本资产接进了 Content Browser 数据模型。
- Hazelight 对应 `J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:136-140` 与 `.../AngelscriptContentBrowserDataSource.cpp:16-28` 基本同构。这里应判为“血缘继承并保留”，不是“当前插件只剩菜单扩展”。
- UnrealCSharp、UnLua、puerts、sluaunreal 在本轮检视范围里都没有与 `UContentBrowserDataSource` 同等级的脚本资产虚拟数据源。

##### 子维度 2：作者高频动作在哪里

- 当前 Angelscript 的 `ShowCreateBlueprintPopup()` 位于 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:418-476`。它会根据 `UASClass` 的 `RelativeSourceFilePath` 倒推默认目录、给出 `BP_` / `DA_` 默认名，再调 `CreateModalSaveAssetDialog`。这是一条明确的“从 script class 到 UE asset”的作者路径。
- UnrealCSharp 的高频动作不在资产创建，而在 Blueprint toolbar。`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:21-88` 直接把 `OpenFile / CodeAnalysis / OverrideBlueprint` 绑到 Blueprint editor，并在 generator 结束后刷新 override-file map。它比 Angelscript 更贴作者当下的编辑动作。
- UnLua 同时有主菜单和 Blueprint toolbar，但真正有特色的是保存期保护。`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:155-182` 在 package save 前 `SuspendOverrides()`、保存后 `ResumeOverrides()`，这是一条明显围绕编辑器稳定性设计的 hook。
- puerts 的 editor integration 不是 native toolbar，而是 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:141-150` 里启动一个 editor-owned `JsEnv`，跑 `PuertsEditor/CodeAnalyze`。它更像“在编辑器内宿主一个分析工具 runtime”。
- sluaunreal 的 editor 面更聚焦。`Reference/sluaunreal/Plugins/slua_unreal/Source/slua_profile/Private/slua_profile.cpp:70-83` 只注册 hidden `NomadTabSpawner` 和 ticker，职责非常单纯，就是 profiler UI。

##### 子维度 3：当前 Angelscript 在 D7 上真正缺的是什么

- 不是缺入口数量。当前仓库已经有 `ContentBrowserDataSource`、`CreateBlueprint popup`、`ScriptEditorMenuExtension` 多路 extender。
- 真正缺的是像 UnrealCSharp 那样“围绕当前 Blueprint 资产给出三个最常用按钮”的短路径。也就是说，当前 Angelscript 强在 surface area，弱在 task-oriented authoring。
- 另一个缺口是像 UnLua 那样对 package save / packaging settings 做更强保护。当前脚本类一旦继续深入绑定资产工作流，这条稳定性链会变得更重要。

[1] 当前 Angelscript 不是只扩菜单，而是直接接管 Content Browser data source：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 函数: OnEngineInitDone
// 位置: 111-118，激活脚本数据源
// ============================================================================
auto* DataSource = NewObject<UAngelscriptContentBrowserDataSource>(GetTransientPackage(), "AngelscriptData", RF_MarkAsRootSet | RF_Transient);
DataSource->Initialize();

UContentBrowserDataSubsystem* ContentBrowserData = IContentBrowserDataModule::Get().GetSubsystem();
ContentBrowserData->ActivateDataSource("AngelscriptData"); // ★ 脚本资产进入 Content Browser 数据层

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp
// 函数: UAngelscriptContentBrowserDataSource::CreateAssetItem
// 位置: 16-28，虚拟脚本资产项
// ============================================================================
Payload->Path = Asset->GetPathName();
Payload->Asset = Asset;
FString DisplayName = Asset->GetName();
DisplayName.RemoveFromStart(TEXT("Asset_"));
return FContentBrowserItemData(
	this,
	EContentBrowserItemFlags::Type_File | EContentBrowserItemFlags::Category_Asset,
	*(TEXT("/All/Angelscript/") + Asset->GetName()), Asset->GetFName(), FText::FromString(DisplayName), Payload, *Payload->Path);
// ★ 真正把脚本对象映射进虚拟内容路径
```

[2] 当前 Angelscript 的作者路径是“script class -> Blueprint/DataAsset 创建向导”，不是只给菜单挂回调：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 函数: FAngelscriptEditorModule::ShowCreateBlueprintPopup
// 位置: 418-476，从脚本类推导默认资产路径并打开保存对话框
// ============================================================================
const bool bIsDataAsset = Class->IsChildOf<UDataAsset>();
if (bIsDataAsset)
	Title = FString::Printf(TEXT("Create Asset of %s%s"), Class->GetPrefixCPP(), *Class->GetName());
else
	Title = FString::Printf(TEXT("Create Blueprint of %s%s"), Class->GetPrefixCPP(), *Class->GetName());

if (!AssetPath.StartsWith(TEXT("/")))
{
	FString ScriptRelativePath = Class->GetRelativeSourceFilePath();
	// ★ 通过脚本源文件相对路径倒推最合理的默认目录
	...
	AssetPath = InitialDirectory / AssetPath;
}

SaveAssetDialogConfig.DefaultPath = FPaths::GetPath(AssetPath);
SaveAssetDialogConfig.DefaultAssetName = FPaths::GetCleanFilename(AssetPath);
SaveAssetDialogConfig.AssetClassNames.Add(Class->GetClassPathName());
// ★ 这是明确的作者向导，而不是泛泛的“创建资产”入口
```

[3] UnrealCSharp / UnLua / puerts / sluaunreal 在 editor surface 上分别押注不同中心：

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp
// 函数: Initialize / BuildAction
// 位置: 21-88，Blueprint 工具栏高频动作
// ============================================================================
BlueprintEditorModule.GetMenuExtensibilityManager()->GetExtenderDelegates().Add(...);
SetCodeAnalysisOverrideFilesMap();
FDynamicGenerator::SetCodeAnalysisDynamicFilesMap();

CommandList->MapAction(FUnrealCSharpEditorCommands::Get().OpenFile, ...);
CommandList->MapAction(FUnrealCSharpEditorCommands::Get().CodeAnalysis, ...);
CommandList->MapAction(FUnrealCSharpEditorCommands::Get().OverrideBlueprint, ...);
// ★ 作者在 Blueprint editor 里直接拥有打开、分析、生成 override 三个动作

// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp
// 函数: OnPackageSaving / OnPackageSaved
// 位置: 155-182，保存期间暂停/恢复 override
// ============================================================================
for (const auto Pair : SuspendedPackages)
	ULuaFunction::SuspendOverrides(Pair.Value);

if (SuspendedPackages.Contains(Package))
{
	ULuaFunction::ResumeOverrides(SuspendedPackages[Package]);
	SuspendedPackages.Remove(Package);
}
// ★ 编辑器保存流程优先保障 override 稳定性

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp
// 函数: FPuertsEditorModule::StartupModule
// 位置: 141-150，在 editor 内部启动分析用 JsEnv
// ============================================================================
if (SourceFileWatcher.IsValid())
{
	SourceFileWatcher->OnSourceLoaded(InPath);
}
JsEnv->Start("PuertsEditor/CodeAnalyze");
// ★ editor integration 的核心是“分析 runtime”，不是 native toolbar

// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_profile/Private/slua_profile.cpp
// 函数: Fslua_profileModule::StartupModule
// 位置: 70-83，单独 profiler tab
// ============================================================================
sluaProfilerInspector = MakeShareable(new SProfilerInspector);
FGlobalTabmanager::Get()->RegisterNomadTabSpawner(slua_profileTabName,
	FOnSpawnTab::CreateRaw(this, &Fslua_profileModule::OnSpawnPluginTab))
	.SetDisplayName(LOCTEXT("Flua_wrapperTabTitle", "slua Profiler"))
	.SetMenuType(ETabSpawnerMenuType::Hidden);
// ★ editor 面高度聚焦在 profiler，而不是脚本 authoring
```

#### 对比矩阵（本轮新增观察点）

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| `ContentBrowserDataSource` 级虚拟脚本视图 | Full | None | None | None | None | Full |
| 从脚本类直接创建 Blueprint / DataAsset 向导 | Full | Partial | Partial | Partial | None | Full |
| Blueprint 工具栏高频作者按钮 | Partial | Full | Full | Partial | None | Partial |
| 保存期 override 保护 | None | None | Full | None | None | None |
| 编辑器内宿主脚本分析 runtime | None | Full | Partial | Full | None | None |
| 独立 profiler / 观测 tab | None | None | None | None | Full | None |

#### 小结与建议

- D7 的新增结论是：当前 Angelscript 实际上比首轮总结更偏“资产中心”。`ContentBrowserDataSource + CreateBlueprint popup` 让它在脚本资产 authoring 上比单纯菜单扩展更深入，优先级判断应维持 `P1` 但评价更正面。
- 最值得吸收的是 UnrealCSharp 的 task-oriented authoring：围绕当前 Blueprint 资产给出 `Open / Analyze / Override` 这种三键工作流，优先级 `P1`。
- 另一个高价值补点是 UnLua 的保存期保护。当前 Angelscript 一旦继续扩张脚本资产工作流，`PreSave / PostSave` 上的 override 安全带会很必要，优先级 `P1`。

---

## 深化分析 (2026-04-08 18:43:40)

### [D4] 热重载 authority 的归属

#### 各插件实现概览

```
[D4-Deep] Reload Authority Owner
HZ : compile result -> soft/full reload                  // 仍是 class-generation 中心
AS : compile result -> queued retry -> hot-reload tests  // 决策与回归被绑在一起
UC : domain / assembly reload                            // 本轮未见源码级 file queue
UL : config gate -> Lua HotReload.lua                    // C++ 只触发，Lua 侧修补对象图
PU : loaded-file watcher -> ReloadSource -> lazy rebind
SL : loadfile delegate -> preview simulate hook          // 边界停在文件流与对象 hook
```

前文已经比较过 `SoftReload / FullReload` 梯度；这一轮不再重复那个结论，而是补“谁真正拥有 reload authority”。同样叫热重载，不同插件把决定权放在完全不同的位置：当前 Angelscript / Hazelight 放在 compile transaction，UnLua 放在 Lua 运行时对象图修补，puerts 放在已加载源码 watcher 与 lazy rebind，sluaunreal 甚至没有进入统一 reload 状态机，而是停在文件流和 preview hook。

#### 详细对比

##### 子维度 1：谁决定当前这次变更能不能 reload

- 当前 Angelscript 的 owner 已经不是单个 `switch`，而是“`CompileResult` -> `ReloadRequirement` -> hot reload tests”三段式。`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2481-2490` 会在热重载开始前准备测试 batch；`.../AngelscriptEngine.cpp:3938-4002` 再按 `SoftReload / FullReloadSuggested / FullReloadRequired / Error` 决定是否换入新模块、是否保留旧代码；`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp:319-348` 与 `.../AngelscriptHotReloadAnalysisTests.cpp:75-175` 又把“从内存分析 reload requirement”做成了测试入口。
- Hazelight 仍然沿用 compile-time/class-generation owner，这一点在前面已有首轮证据。本轮新增差异不在枚举名，而在当前插件已经把判定逻辑工程化成测试矩阵。
- UnLua 把 authority 切成两层。`Reference/UnLua/Plugins/UnLua/Source/UnLua/UnLua.Build.cs:106-111` 先用配置生成 `UNLUA_WITH_HOT_RELOAD`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorSettings.h:32-47` 再给 editor 一个 `Manual / Auto / Never` 模式；真正的 reload 动作则退化成 `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaLib.cpp:51-58` 的 `require('UnLua.HotReload').reload()`。也就是说，C++ 不拥有细粒度结构性判定，Lua VM 才是最后 owner。
- puerts 的 authority 落在“已加载文件 + JS runtime”。`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:122-150` 只监听已经进入 `JsEnv` 的源码，并直接 `ReloadSource()`；`Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:424-438` 对 C++ hot reload 的响应也只是重新创建 `JsEnv`。它不是没有 reload contract，而是 contract 不在统一的类型/结构枚举上。
- sluaunreal 更接近“加载器 + 预览 hook”。`Reference/sluaunreal/Source/democpp/MyGameInstance.cpp:41-58` 明确通过 `setLoadFileDelegate()` 从 `Content/Lua` 读取 `.lua/.luac`；`Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaSimulate.cpp:98-108` 的 preview simulate 也只是在新建 `LuaState` 后复用这条 loader。这里没有与 Angelscript 对等的结构性 reload owner。

##### 子维度 2：失败恢复和可观测性落在哪

- 当前 Angelscript 的优势不只是“会区分 soft/full”，而是失败恢复有公开状态。`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3972-4002` 明确写出“当前不能 full reload 时保持旧代码继续运行”；结合前文首轮已记录的 `QueuedFullReloadFiles / PreviouslyFailedReloadFiles`，可以形成明确的待重试模型。
- UnLua 的失败恢复更偏运行时 patch。`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaFunction.cpp:81-100` 的 `Override / RestoreOverrides / SuspendOverrides / ResumeOverrides` 说明其核心关心点是 override 生命周期，而不是对外暴露一组待重试队列。这里不是“没有恢复”，而是恢复 owner 在 Lua 对象图和 override 层。
- puerts 有 rebind，但缺少 Angelscript 这种显式排队。`ReloadSource()` 失败只打日志；`JsEnv` rebuild 也更像重新挂接 runtime，而不是保留一份“这批文件稍后必须 full reload”的事务状态。
- sluaunreal 本轮没有定位到与 `QueuedFullReloadFiles` 对等的公开状态面。`LuaSimulate.cpp:100-103` 甚至直接在 `LoadFileDelegate` 缺失时报错返回。这说明它当前的策略更像“加载器失败即终止当前模拟”，而不是“延后重试”。

[1] 当前 Angelscript 把热重载判定、旧代码保留和测试准备放在同一条事务链：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: FAngelscriptEngine::PerformHotReload / compile reload switch
// 位置: 2481-2490, 3938-4002
// ============================================================================
if (GEngine && bCompletedAssetScan && HotReloadTestRunner != nullptr && HotReloadTestRunner->ShouldRunUnitTestsOnHotReload())
{
    // ★ 热重载一开始就把本轮受影响文件交给测试 runner
    HotReloadTestRunner->PrepareTests(GetActiveModules(), CompiledModules, RelativeFileList, ShouldUseAutomaticImportMethod());
}

switch (ReloadReq)
{
    case FAngelscriptClassGenerator::EReloadRequirement::SoftReload:
        SwapInModules(CompiledModules, DiscardedModules);
        ClassGenerator.PerformSoftReload();
        break;

    case FAngelscriptClassGenerator::EReloadRequirement::FullReloadRequired:
        if (CompileType == ECompileType::SoftReloadOnly)
        {
            // ★ 当前环境不能 full reload 时，显式保留旧代码继续运行
            bShouldSwapInModules = false;
            bFullReloadRequired = true;
        }
        else
        {
            SwapInModules(CompiledModules, DiscardedModules);
            ClassGenerator.PerformFullReload();
        }
        break;
}
```

[2] 当前 Angelscript 的 reload requirement 不是“约定”，而是被测试 helper 和场景测试直接锁死：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp
// 函数: AnalyzeReloadFromMemory
// 位置: 319-348
// ============================================================================
switch (CompileResult)
{
case ECompileResult::FullyHandled:
    OutReloadRequirement = FAngelscriptClassGenerator::SoftReload;
    return bCompiled;

case ECompileResult::PartiallyHandled:
    OutReloadRequirement = FAngelscriptClassGenerator::FullReloadSuggested;
    bOutWantsFullReload = true;
    return true;

case ECompileResult::ErrorNeedFullReload:
    OutReloadRequirement = FAngelscriptClassGenerator::FullReloadRequired;
    bOutWantsFullReload = true;
    bOutNeedsFullReload = true;
    return true;
}

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadAnalysisTests.cpp
// 函数: FAngelscriptAnalyzeReload*::RunTest
// 位置: 75-175
// ============================================================================
TestEqual(TEXT("Unchanged module should remain soft reload"), ReloadRequirement, FAngelscriptClassGenerator::SoftReload);
TestTrue(TEXT("Property count change should not remain soft reload"),
    ReloadRequirement == FAngelscriptClassGenerator::FullReloadRequired
    || ReloadRequirement == FAngelscriptClassGenerator::FullReloadSuggested);
TestEqual(TEXT("Super-class change should require a full reload"),
    ReloadRequirement, FAngelscriptClassGenerator::FullReloadRequired);
// ★ body-only / property-count / super-class 三类变化被直接映射成固定 contract
```

[3] UnLua 的 hot reload authority 先被配置裁剪，再交给 Lua 侧 patch：

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/UnLua.Build.cs
// 位置: 106-111
// ============================================================================
string hotReloadMode;
if (!config.GetString(section, "HotReloadMode", out hotReloadMode))
    hotReloadMode = "Manual";

var withHotReload = hotReloadMode != "Never";
PublicDefinitions.Add("UNLUA_WITH_HOT_RELOAD=" + (withHotReload ? "1" : "0"));

// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorSettings.h
// 位置: 32-47
// ============================================================================
enum class EHotReloadMode : uint8
{
    Manual,
    Auto,
    Never
};

UPROPERTY(config, EditAnywhere, Category = "Coding", meta = (defaultValue = 0))
EHotReloadMode HotReloadMode;

// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaLib.cpp
// 位置: 51-58
// ============================================================================
static int HotReload(lua_State* L)
{
#if UNLUA_WITH_HOT_RELOAD
    if (luaL_dostring(L, "require('UnLua.HotReload').reload()") != 0)
    {
        LogError(L);
    }
#endif
    return 0;
}
// ★ C++ 只是触发 reload，真正的对象图修补留给 Lua 侧
```

[4] puerts / sluaunreal 更像“运行时 loader owner”，不是结构性 reload owner：

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp
// 位置: 122-150
// ============================================================================
SourceFileWatcher = MakeShared<PUERTS_NAMESPACE::FSourceFileWatcher>(
    [this](const FString& InPath)
    {
        if (JsEnv.IsValid())
        {
            TArray<uint8> Source;
            if (FFileHelper::LoadFileToArray(Source, *InPath))
            {
                JsEnv->ReloadSource(InPath, puerts::PString((const char*) Source.GetData(), Source.Num()));
            }
        }
    });
JsEnv->Start("PuertsEditor/CodeAnalyze");

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp
// 位置: 424-438
// ============================================================================
FCoreUObjectDelegates::ReloadCompleteDelegate.AddLambda(
    [&](EReloadCompleteReason)
    {
        if (Enabled)
        {
            MakeSharedJsEnv(); // ★ C++ hot reload 后重建 JsEnv，而不是维护待重试文件队列
        }
    });

// ============================================================================
// 文件: Reference/sluaunreal/Source/democpp/MyGameInstance.cpp
// 位置: 41-58
// ============================================================================
state->setLoadFileDelegate([](const char* fn, FString& filepath)->TArray<uint8> {
    path /= "Lua";
    path /= filename.Replace(TEXT("."), TEXT("/"));
    TArray<FString> luaExts = { TEXT(".lua"), TEXT(".luac") };
    for (auto& it : luaExts) {
        auto fullPath = path + *it;
        FFileHelper::LoadFileToArray(Content, *fullPath);
        if (Content.Num() > 0) {
            filepath = fullPath;
            return MoveTemp(Content);
        }
    }
    return {};
});

// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaSimulate.cpp
// 位置: 98-108
// ============================================================================
if (Delegate == nullptr)
{
    Log::Error("lua Simulation Error. LoadFileDelegate not set.");
    return;
}
SluaState = new NS_SLUA::LuaState("", nullptr);
SluaState->setLoadFileDelegate(Delegate);
SluaState->init();
// ★ 这里的 contract 是“能否重新建一个 LuaState 并重新挂 loader”，不是 soft/full reload lattice
```

#### 对比矩阵（本轮新增观察点）

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| reload authority 位于 compile/class-generation | Full | Partial | None | None | None | Full |
| reload authority 位于脚本运行时对象图修补 | None | None | Full | Partial | Partial | None |
| 用户可配置 `Manual / Auto / Never` 一类模式 | None | None | Full | Partial | None | None |
| 热重载判定被自动化测试直接锁定 | None | None | None | None | None | Full |
| 失败文件具备显式排队/延后 full reload 语义 | Partial | None | None | None | None | Full |
| watcher 只跟踪已加载脚本而不是统一文件队列 | None | None | None | Full | Partial | None |

#### 小结与建议

- 当前 Angelscript 在 D4 上新增确认的强项，是“reload contract 已经工程化成测试约束”。这比单纯有 `SoftReload / FullReload` 更重要，优先级 `P0`。
- 最值得吸收的是 UnLua 的用户可见模式开关，而不是它的 Lua-side patch 路线。当前 Angelscript 适合补一个 editor 级的 `Manual / Auto / Never` 策略面板，但不要放弃现有 compile transaction owner，优先级 `P1`。
- puerts 提醒了另一件事：当前 Angelscript 仍然缺一个更显式的“reload 完成后重绑通知面”。这不是回退到 watcher-owner，而是在保留队列和测试前提下补一个更可消费的 rebind event，优先级 `P1`。

### [D9] 测试 authority 的落点

#### 各插件实现概览

```
[D9-Deep] Test Authority Surface
HZ : runtime/editor/loader only                      // 本轮未见 first-class test surface
AS : Runtime tests + Editor tests + Test module + coverage
UC : runtime/editor/generator/compiler              // 本轮未见公开 test module
UL : UnLuaTestSuite plugin -> macro DSL -> specs/issues
PU : runtime/editor/program                         // 本轮未见公开 test module
SL : runtime/profiler/demo                          // 本轮未见 automation surface
```

这一轮补出来的关键不是“谁有更多 `AutomationTest`”，而是“谁定义了 correctness contract”。当前 Angelscript 把 contract 分散在 `Runtime`、`Editor`、`Test module` 三层，并且连 coverage 都进入了自动化测试；UnLua 则把 contract 外置到单独 `UnLuaTestSuite` 插件，用 spec、issue regression 和宏 DSL 长期维护。两边都强，但 authority 的落点完全不同。

#### 详细对比

##### 子维度 1：测试 surface 是内嵌主插件，还是单独测试插件

- 当前 Angelscript 在 `Plugins/Angelscript/Angelscript.uplugin:18-32` 直接公开 `AngelscriptRuntime`、`AngelscriptEditor`、`AngelscriptTest` 三模块，意味着测试不是“仓库外另找工程”，而是主插件的一等组成部分。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptCodeCoverageTests.cpp:13-67` 说明 runtime 层会直接验证覆盖率映射和 HTML 报告；`Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp:15-38` 又说明 editor 内部行为也有独立测试入口。这种分层和 `HotReloadTestRunner` 形成了闭环。
- UnLua 把 authority 放在独立插件。`Reference/UnLua/Plugins/UnLuaTestSuite/UnLuaTestSuite.uplugin:24-28` 暴露的是单独的 `UnLuaTestSuite` runtime 模块，不侵入 `UnLua` 主插件结构。这里不是“测试更弱”，而是“测试资产被外置并可长期沉淀”。
- Hazelight、UnrealCSharp、puerts、sluaunreal 本轮在插件树内都未定位到与 `AngelscriptTest` 或 `UnLuaTestSuite` 对等的公开测试 surface；这里继续维持保守判定，不武断外推到仓库外工具链。

##### 子维度 2：测试 contract 是 API/spec，还是 issue regression

- 当前 Angelscript 的特色是“按层级拆 contract”。`Runtime` 侧覆盖内部引擎、coverage、debug protocol；`Editor` 侧覆盖 watcher 和蓝图影响扫描；`AngelscriptTest` 则承担热重载、继承、learning 场景。
- UnLua 的特色是“把 issue 号和 spec 名字变成长期 API contract”。`Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Public/UnLuaTestCommon.h:171-214` 提供 `IMPLEMENT_UNLUA_LATENT_TEST / IMPLEMENT_UNLUA_INSTANT_TEST / BEGIN_TESTSUITE` 宏；`.../Private/Specs/LuaEnv.spec.cpp:23-58` 用 `BEGIN_DEFINE_SPEC` 做 API 行为契约；`.../Private/Tests/Issue603Test.cpp:22-33` 则把真实 bug 直接固化成 `UnLua.Regression.Issue603`。
- 这意味着 UnLua 的测试 authority 更偏“回归案例库 + API 教科书”；当前 Angelscript 的 authority 更偏“内部行为面 + 基础设施自检”。两者不是强弱差异，而是组织策略不同。

[1] 当前 Angelscript 的测试 authority 是内建分层，而不是单点目录：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Angelscript.uplugin
// 位置: 18-32
// ============================================================================
"Modules": [
    {
        "Name": "AngelscriptRuntime",
        "Type": "Runtime"
    },
    {
        "Name": "AngelscriptEditor",
        "Type": "Editor"
    },
    {
        "Name": "AngelscriptTest",
        "Type": "Editor"
    }
]

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptCodeCoverageTests.cpp
// 位置: 13-67
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAngelscriptCodeCoverageTests0,
    "Angelscript.CppTests.AngelscriptCodeCoverage.IntegrationTest",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter);

Coverage.StartRecording();
for (TSharedRef<struct FAngelscriptModuleDesc>& Module : Manager.GetActiveModules())
{
    Coverage.MapExecutableLines(*Module);
    ...
    Coverage.HitLine(*Module, Line.Key);
}
Coverage.StopRecordingAndWriteReport(TempDir);
// ★ 覆盖率不是单独脚本工具，而是自动化测试的一部分

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp
// 位置: 15-38
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAngelscriptDirectoryWatcherScriptQueueTest,
    "Angelscript.Editor.DirectoryWatcher.Queue.ScriptAddAndRemove",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAngelscriptDirectoryWatcherRenameWindowTest,
    "Angelscript.Editor.DirectoryWatcher.Queue.RenameWindowTracksRemoveAndAdd",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
// ★ editor 内部 contract 直接在插件内维护
```

[2] UnLua 把测试能力做成独立插件，并同时维护宏 DSL、spec 和 issue regression：

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/UnLuaTestSuite.uplugin
// 位置: 24-28
// ============================================================================
"Modules": [
    {
        "Name": "UnLuaTestSuite",
        "Type": "Runtime",
        "LoadingPhase": "Default"
    }
]

// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Public/UnLuaTestCommon.h
// 位置: 171-214
// ============================================================================
#define IMPLEMENT_UNLUA_LATENT_TEST(TestClass, PrettyName) \
IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass##_Runner, PrettyName, ...)

#define IMPLEMENT_UNLUA_INSTANT_TEST(TestClass, PrettyName) \
IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass##Runner, PrettyName, ...)

#define BEGIN_TESTSUITE(TestClass, PrettyName) \
namespace UnLuaTestSuite { \
IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, PrettyName, ...)
// ★ 先把 latent / instant / testsuite 三类测试作者体验抽成宏

// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Specs/LuaEnv.spec.cpp
// 位置: 23-58
// ============================================================================
BEGIN_DEFINE_SPEC(FLuaEnvSpec, "UnLua.API.FLuaEnv", EAutomationTestFlags::ProductFilter | EAutomationTestFlags::ApplicationContextMask)
    TSharedPtr<UnLua::FLuaEnv> Env;
END_DEFINE_SPEC(FLuaEnvSpec)

It(TEXT("支持多个Lua环境"), EAsyncExecution::TaskGraphMainThread, [this]()
{
    UnLua::FLuaEnv Env1;
    UnLua::FLuaEnv Env2;
    Env1.DoString("return 1");
    Env2.DoString("return 2");
});

// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Tests/Issue603Test.cpp
// 位置: 22-33
// ============================================================================
BEGIN_TESTSUITE(FIssue603Test, TEXT("UnLua.Regression.Issue603 在Lua监听的事件中再次触发事件会导致崩溃"))
bool FIssue603Test::RunTest(const FString& Parameters)
{
    const auto MapName = TEXT("/UnLuaTestSuite/Tests/Regression/Issue603/Issue603");
    ADD_LATENT_AUTOMATION_COMMAND(FOpenMapLatentCommand(MapName))
    ADD_LATENT_AUTOMATION_COMMAND(FEngineWaitLatentCommand(1.0));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}
// ★ issue 号、地图资产和 latent 流程一起构成回归 contract
```

#### 对比矩阵（本轮新增观察点）

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 公开独立测试模块/插件 | None | None | Full | None | None | Full |
| runtime / editor 测试分层清晰 | None | None | Partial | None | None | Full |
| latent / instant 一类测试宏 DSL | None | None | Full | None | None | None |
| spec 风格 API 契约 | None | None | Full | None | None | Partial |
| issue 编号回归资产长期沉淀 | None | None | Full | None | None | Partial |
| 覆盖率被纳入自动化测试 | None | None | None | None | None | Full |
| 热重载后自动准备测试 batch | None | None | None | None | None | Full |

#### 小结与建议

- 当前 Angelscript 在 D9 上新增确认的优势，是“测试 authority 已经渗透到 runtime/editor/test module 三层，并能与 hot reload / coverage 联动”。这条线应继续保持，优先级 `P0`。
- 最值得吸收的是 UnLua 的“issue 编号回归语义”和“宏 DSL”。当前 Angelscript 适合在 `AngelscriptTest` 中补一套更稳定的回归命名与作者入口，而不是把所有场景都继续堆成手写样板，优先级 `P1`。
- 如果后续 regression asset 继续膨胀，可以考虑吸收 `UnLuaTestSuite` 的组织方式，把资产重的回归样例进一步外置；但当前不应牺牲已有的 runtime/editor 内测覆盖，优先级 `P2`。

### [D11] 部署 boundary 的真实载体

#### 各插件实现概览

```
[D11-Deep] Packaging Boundary
HZ : Binds.Cache + PrecompiledScript + Loader
AS : Binds.Cache + .Headers + PrecompiledScript[_Config] + NativeThunk
UC : assemblies / domain toolchain                    // 本轮未见同级 artifact contract
UL : Lua runtime lib + platform whitelist + raw file load
PU : staged VM DLLs + optional V8 bytecode + file loader
SL : loadfile delegate reads .lua/.luac + preview reuse
```

这一轮补出来的真正差异，不是“谁会打包 DLL”。更关键的是“打包后真正成为 authority 的是什么”。当前 Angelscript / Hazelight 的 authority 是引擎内部可恢复状态；UnLua / sluaunreal 的 authority 仍然是文件系统上的脚本字节流；puerts 的 authority 是“外部 VM 二进制 + 文件系统脚本”的组合。部署边界对象不同，后续能做的优化也完全不同。

#### 详细对比

##### 子维度 1：打包后究竟在恢复什么

- 当前 Angelscript 的 artifact 不只是 `Binds.Cache`。`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1425-1556` 会根据 build 配置选择 `PrecompiledScript_Shipping/Test/Development.Cache`，校验 `IsValidForCurrentBuild()` 与 `StaticJIT` GUID；`.../AngelscriptEngine.cpp:4284-4390` 再用 `ApplyToModule_Stage1/2/3` 把预编译模块恢复回 AngelScript 内部结构。这里打包的是“可恢复执行状态”，不是脚本原文件。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp:42-123` 进一步说明 `Binds.Cache` 与 `.Headers` 也是部署 contract 的一部分。当前仓库比 Hazelight 多出的关键，不只是首轮已记下的 cooked bind cache，而是 `.Headers`、build-specific cache filename 和 JIT GUID 校验。
- 更关键的新点是 `FUNC_Native + NativeThunk`。`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3428-3429` 会把生成的 `UASFunction` 直接标成 `FUNC_Native` 并挂到 `UASFunctionNativeThunk`；`.../ASClass.cpp:1940-1945` 再把调用收口到 `RuntimeCallFunction()`。这意味着 packaged build 最终并不是“解释器旁路调用”，而是回到 UE 原生 `UFunction` 调用路径。
- Hazelight 仍沿前面已记录的 `Binds.Cache + PrecompiledScript + Loader` 路线。本轮新增结论是：当前 Angelscript 在同一血缘上把部署边界进一步收口到了 `NativeThunk + build validation`，实现更硬。

##### 子维度 2：脚本文件在 packaged runtime 里还是否是一等 authority

- UnLua 仍然把 Lua 文件当 authority。`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaBase.cpp:66-84` 明确通过相对路径解析出 `FullFilePath` 再 `LoadFileToArray()`；`Reference/UnLua/Plugins/UnLua/Source/ThirdParty/Lua/Lua.Build.cs:209-211` 只是确保 Lua runtime library 跟包；`Reference/UnLua/Plugins/UnLua/UnLua.uplugin:23-35` 用 `WhitelistPlatforms` 限定平台。
- puerts 的 authority 也是“运行库 + 文件系统脚本”。`Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs:360-408` 会把 V8/Node/QuickJS 相关 DLL 以 `NonUFS` staged 进包，并可通过 `WITH_V8_BYTECODE` 打开字节码后缀；但 `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DefaultJSModuleLoader.cpp:67-139` 仍然按 `.js/.mjs/.cjs/.json` 和可选 `.mbc/.cbc` 搜索、`OpenRead()` 文件。也就是说，puerts 优化的是 VM 边界，不是把脚本内容完全转化成引擎内状态。
- sluaunreal 也停在文件边界。`Reference/sluaunreal/Source/democpp/MyGameInstance.cpp:41-58` 的 loader 直接从 `Content/Lua` 读取 `.lua/.luac`；`Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaSimulate.cpp:98-108` 说明 preview 模拟同样复用这条 loader。它甚至没有进入像 puerts 那样的 staged VM/runtime 组合。
- UnrealCSharp 本轮在插件树内仍主要暴露 runtime/editor/generator/compiler 模块，未定位到与上述三类 artifact 同级的公开打包 contract，因此继续保守判 `Partial`。

[1] 当前 Angelscript 的 packaged runtime 在恢复 cache，并把最终调用面收口到 `NativeThunk`：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 1425-1556, 2046-2058, 4284-4390
// ============================================================================
bUsePrecompiledData = !bGeneratePrecompiledData && !RuntimeConfig.bIgnorePrecompiledData
    && !RuntimeConfig.bRunningCommandlet && !WITH_EDITOR && !bScriptDevelopmentMode;

FAngelscriptBindDatabase::Get().Load(GetScriptRootDirectory() / TEXT("Binds.Cache"), bGeneratePrecompiledData);

Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript_Shipping.Cache");
...
PrecompiledData->Load(Filename);
if (!PrecompiledData->IsValidForCurrentBuild())
{
    UE_LOG(Angelscript, Warning, TEXT("Loaded angelscript precompiled data was for a different build configuration. Discarding all precompiled data."));
}

if (PrecompiledData != nullptr && bUsePrecompiledData && !bScriptDevelopmentMode)
{
    bUsedPrecompiledDataForPreprocessor = true;
    ModulesToCompile = PrecompiledData->GetModulesToCompile();
    UE_LOG(Angelscript, Warning, TEXT("Using fully precompiled scripts. Hot reloading is disabled for this run."));
}

if (CompiledModule->CodeHash == Module->CodeHash)
{
    CompiledModule->ApplyToModule_Stage1(*PrecompiledData, ScriptModule);
    Module->bLoadedPrecompiledCode = true;
}
...
Module->PrecompiledData->ApplyToModule_Stage2(*PrecompiledData, ScriptModule);
Module->PrecompiledData->ApplyToModule_Stage3(*PrecompiledData, ScriptModule);
// ★ 运行时恢复的是 engine-internal state，而不是再次解释原始脚本文件

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp
// 位置: 42-123
// ============================================================================
Serialize(Writer);
bool bSaveSuccess = FFileHelper::SaveArrayToFile(Data, *Path);
...
FFileHelper::SaveArrayToFile(HeaderData, *(Path + TEXT(".Headers")));
...
FFileHelper::LoadFileToArray(Data, *Path);
Serialize(Reader);
if (Classes.Num() == 0 && Structs.Num() == 0)
{
    UE_LOG(Angelscript, Fatal, TEXT("Unable to load script bind database, Script/Binds.Cache file is missing or old."));
}
// ★ `Binds.Cache + .Headers` 自身就是部署 contract

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 位置: 3428-3429
// ============================================================================
NewFunction->FunctionFlags |= FUNC_Native;
NewFunction->SetNativeFunc(&UASFunctionNativeThunk);

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp
// 位置: 1940-1945
// ============================================================================
void UASFunctionNativeThunk(UObject* Object, FFrame& Stack, RESULT_DECL)
{
    UASFunction* Function = Cast<UASFunction>(Stack.Node);
    check(Function != nullptr);
    Function->RuntimeCallFunction(Object, Stack, RESULT_PARAM);
}
// ★ packaged build 最终通过 UE 原生 thunk 路径进入脚本运行时
```

[2] UnLua / puerts / sluaunreal 的 packaged authority 仍停在 runtime library 与文件加载器：

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaBase.cpp
// 位置: 66-84
// ============================================================================
bool LoadFile(lua_State *L, const FString &RelativeFilePath, const char *Mode, int32 Env)
{
    FString FullFilePath = GetFullPathFromRelativePath(RelativeFilePath);
    ...
    TArray<uint8> Data;
    bool bSuccess = FFileHelper::LoadFileToArray(Data, *FullFilePath, 0);
    ...
}
// ★ 运行时仍按相对路径解析并读取 Lua 文件

// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/ThirdParty/Lua/Lua.Build.cs
// 位置: 209-211
// ============================================================================
PublicDefinitions.Add("LUA_USE_MACOSX");
PublicAdditionalLibraries.Add(libFile);
RuntimeDependencies.Add(libFile);

// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/UnLua.uplugin
// 位置: 23-35
// ============================================================================
"Name": "UnLua",
"Type": "Runtime",
"WhitelistPlatforms": [ "Win64", "Mac", "IOS", "Android", "Linux" ]
// ★ UnLua 的部署 contract 仍然是“Lua runtime lib + 平台白名单 + 原始脚本文件”

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs
// 位置: 360-408
// ============================================================================
var DestDllPath = Path.Combine("$(BinaryOutputDir)", DllName);
RuntimeDependencies.Add(DestDllPath, DllPath, StagedFileType.NonUFS);
...
if (WithByteCode)
{
    PrivateDefinitions.Add("WITH_V8_BYTECODE");
}

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DefaultJSModuleLoader.cpp
// 位置: 67-139
// ============================================================================
return SearchModuleWithExtInDir(Dir, RequiredModule + ".js", Path, AbsolutePath) ||
       SearchModuleWithExtInDir(Dir, RequiredModule + ".mjs", Path, AbsolutePath) ||
       SearchModuleWithExtInDir(Dir, RequiredModule + ".cjs", Path, AbsolutePath) ||
#if defined(WITH_V8_BYTECODE)
       SearchModuleWithExtInDir(Dir, RequiredModule + ".mbc", Path, AbsolutePath) ||
       SearchModuleWithExtInDir(Dir, RequiredModule + ".cbc", Path, AbsolutePath) ||
#endif
       SearchModuleWithExtInDir(Dir, RequiredModule / "package.json", Path, AbsolutePath);

IFileHandle* FileHandle = PlatformFile.OpenRead(*Path);
// ★ puerts 可 stage VM，可选 bytecode，但脚本内容仍然按文件系统 authority 装载

// ============================================================================
// 文件: Reference/sluaunreal/Source/democpp/MyGameInstance.cpp
// 位置: 41-58
// ============================================================================
state->setLoadFileDelegate([](const char* fn, FString& filepath)->TArray<uint8> {
    path /= "Lua";
    path /= filename.Replace(TEXT("."), TEXT("/"));
    TArray<FString> luaExts = { TEXT(".lua"), TEXT(".luac") };
    ...
    FFileHelper::LoadFileToArray(Content, *fullPath);
});
// ★ slua 的部署边界最接近“原始字节流 + 自定义 loader”
```

#### 对比矩阵（本轮新增观察点）

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 引擎内部可恢复 cache 是主打包 artifact | Full | Partial | None | None | None | Full |
| 外部 runtime binary / VM 需要显式 staged | Partial | Partial | Full | Full | Partial | Partial |
| 运行时默认仍按文件系统读取脚本内容 | Partial | Partial | Full | Full | Full | Partial |
| 平台 whitelist / staging contract 在 `uplugin` / `Build.cs` 显式声明 | Partial | Partial | Full | Full | Partial | Partial |
| 启动期存在 build mismatch / hash 校验 | Partial | Partial | None | None | None | Full |
| `UFunction` 调用面被收口到原生 thunk / native bridge | Full | Partial | Partial | Partial | Partial | Full |

#### 小结与建议

- 当前 Angelscript 在 D11 上新增确认的关键强项，是“打包边界已经从 cache 一路收口到 `NativeThunk`”。这说明它不是简单的脚本缓存方案，而是 packaged runtime contract，优先级 `P0`。
- 最值得吸收的是 puerts/UnLua 的“部署选项显式化”，不是它们的文件加载边界。当前 Angelscript 适合继续保留 `PrecompiledScript` 路线，同时把 `ignore/generate precompiled data`、平台行为和 staged 依赖写得更显式，优先级 `P1`。
- sluaunreal 提醒了另一个边界：只要 authority 仍停在文件 loader，就很难得到当前 Angelscript 这种 build mismatch 校验与多阶段恢复能力。因此当前插件不应回退到“文件脚本 + 自定义 loader”为主的部署模型，优先级 `P0`。

---
## 深化分析 (2026-04-08 18:58:27)

### [D2] 绑定 authority 的最终落点

#### 各插件实现概览

```
[D2-Deep] Binding Authority Landing
AS(now) : UHT FunctionTable -> FuncPtr ? DirectBind : EligibilityGate -> ReflectiveFallback
HZ      : ASReflectedFunctionPointers -> DirectBind only
UC      : Reflected UFunction scan -> Duplicate/Redirect -> execCallCSharp
UL      : LoadReflectedType -> FClassDesc registry -> metatable dispatch
PU      : UFunction -> FFunctionTranslator cache -> ProcessEvent bridge
SL      : LuaCppBinding<T> template -> LuaCFunction / native closure
```

本轮不再重述“谁支持 UClass / UStruct 暴露”。新增观察点是：反射绑定最后到底落在哪个 authority 对象上。当前 Angelscript 已经不是 Hazelight 的纯 direct bind 路线，而是 `UHT FunctionTable + direct bind + reflective fallback` 三轨结构。UnLua / puerts / sluaunreal 不是“没有实现绑定”，而是 authority 落在 `registry / translator / template closure`，属于实现方式不同。

#### 详细对比

##### 子维度 1：绑定 authority 的最终宿主

- 当前 Angelscript 先在 UHT 阶段生成 `AS_FunctionTable_Summary.json`、module summary CSV 与 shard 注册体，再在运行时根据 `Entry->FuncPtr` 是否有效决定走 direct bind 还是 reflective fallback。authority 先落在可统计的 FunctionTable，再落到真实调用面。
- Hazelight 的 `Bind_BlueprintCallable.cpp` 仍直接依赖 `OwningClass->ASReflectedFunctionPointers`。拿不到 `CallerPtr / FunctionPointerOrRetriever` 就直接返回。这不是“路线不同”而已，而是在同血缘方案下少了一条 fallback 轨道。
- UnrealCSharp 会把匹配到的 reflected `UFunction` 重定向到 `UCSharpFunction::execCallCSharp`；UnLua 把 authority 收口到 `FClassDesc`；puerts 收口到 `FFunctionTranslator`；sluaunreal 收口到模板实例化后的 `LuaCFunction`。

[1] 当前 Angelscript 先把 binding authority 物化为 UHT artifact，再在运行时做 direct/fallback 分流：

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs
// 位置: 168-195, 302-324
// ============================================================================
int totalGeneratedEntries = moduleSummaries.Sum(static summary => summary.TotalEntries);
int totalDirectBindEntries = moduleSummaries.Sum(static summary => summary.DirectBindEntries);
int totalStubEntries = moduleSummaries.Sum(static summary => summary.StubEntries);
double directBindRate = totalGeneratedEntries > 0 ? (double)totalDirectBindEntries / totalGeneratedEntries : 0.0;
double stubRate = totalGeneratedEntries > 0 ? (double)totalStubEntries / totalGeneratedEntries : 0.0;
// ★ UHT 阶段已经把 direct/stub 比例固化成 artifact，而不是等运行时猜测覆盖率

builder.Append("AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_AS_FunctionTable_")
    .Append(moduleShortName)
    .Append('_')
    .Append(shardIndex.ToString("D3"))
    .AppendLine("((int32)FAngelscriptBinds::EOrder::Late + 50, []()");
// ★ authority 的第一落点是分片 FunctionTable，不是零散手写 bind 点
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp
// 位置: 72-90
// ============================================================================
auto* DirectNativePointer = &Entry->FuncPtr;
const bool bHasDirectNativePointer = DirectNativePointer != nullptr && DirectNativePointer->IsBound();
if (!bHasDirectNativePointer)
{
    if (!BindBlueprintCallableReflectiveFallback(InType, Function, Signature, *Entry))
        return;
    return;
}
// ★ current AS 不是“没有 native pointer 就放弃”，而是转入第三轨 reflective fallback

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp
// 位置: 267-279, 382-420
// ============================================================================
if (OwningClass->HasAnyClassFlags(CLASS_Interface))
    return EAngelscriptReflectiveFallbackEligibility::RejectedInterfaceClass;
if (Function->HasMetaData(NAME_BlueprintCallableReflectiveFallback_CustomThunk))
    return EAngelscriptReflectiveFallbackEligibility::RejectedCustomThunk;
if (GetNonReturnParameterCount(Function) > BlueprintCallableReflectiveFallbackMaxArgs)
    return EAngelscriptReflectiveFallbackEligibility::RejectedTooManyArguments;
...
if (!BindReflectiveFunction(InType, Signature, ReflectiveSignature))
    return false;
Entry.bReflectiveFallbackBound = true;
// ★ fallback 不是黑箱兜底，而是带拒绝原因和绑定标记的独立 authority

// ============================================================================
// 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_BlueprintCallable.cpp
// 位置: 41-48, 62-68, 102-107
// ============================================================================
auto* FuncInMap = OwningClass->ASReflectedFunctionPointers.Find(Function->GetFName());
if (FuncInMap == nullptr)
    return;
if (FuncInMap->CallerPtr == nullptr)
    return;
...
int FunctionId = FAngelscriptBinds::BindMethodDirect(
    InType->GetAngelscriptTypeName(),
    Signature.Declaration, FunctionPointers.FunctionPointer, asCALL_THISCALL, FunctionPointers);
// ★ Hazelight 仍以 direct bind 为终点，没有 current AS 这一轮新增的第三轨
```

##### 子维度 2：authority 缺口是静默放弃，还是显式分类

- 当前 Angelscript 明确区分 `RejectedInterfaceClass / RejectedCustomThunk / RejectedTooManyArguments`。这说明它不仅知道“没有 direct bind”，还知道“为什么没有”。
- Hazelight 在同一路线下应判定为“没有实现同级 fallback 机制”，不是简单的实现方式不同。
- UnrealCSharp / UnLua / puerts / sluaunreal 则是从一开始就把 authority 设计到 bridge / registry / translator / closure 上，因此这几者应判为“实现方式不同”。

[2] 其他插件的 authority 宿主并不相同，不能简单写成“没有反射绑定”：

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FCSharpBind.cpp
// 位置: 240-292, 333-366
// ============================================================================
if (Function->HasAnyFunctionFlags(FUNC_BlueprintEvent) && !Function->HasAnyFunctionFlags(FUNC_Final))
{
    ...
    Bind(NewClassDescriptor, FoundClass, MethodName, Function);
}
...
const auto OverrideFunction = DuplicateFunction(OriginalFunction, InClass, *NewFunctionName);
OriginalFunction->SetNativeFunc(UCSharpFunction::execCallCSharp);
OriginalFunction->FunctionFlags |= FUNC_Native;
// ★ UnrealCSharp 把 authority 最终收口到 native bridge

// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp
// 位置: 68-76, 90-105
// ============================================================================
const auto Type = LoadReflectedType(TypeName);
...
Ret = RegisterInternal(StructType, UTF8_TO_TCHAR(MetatableName));
// ★ UnLua 的 authority 落在 `FClassDesc` registry

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp
// 位置: 38-69
// ============================================================================
auto Iter = MethodsMap.Find(InFunction->GetFName());
if (!Iter)
{
    auto FunctionTranslator = std::make_shared<FFunctionTranslator>(InFunction, false);
    MethodsMap.Add(InFunction->GetFName(), FunctionTranslator);
}
// ★ puerts 的 authority 落在按 `UFunction` 缓存的 translator

// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaCppBinding.h
// 位置: 285-300
// ============================================================================
static RET invoke(lua_State* L,void* ptr,ARG&&... args) {
    T* thisptr = (T*)ptr;
    return (thisptr->*func)( std::forward<ARG>(args)... );
}
static int LuaCFunction(lua_State* L) {
    void* p = LuaObject::checkUD<T>(L,1);
    using f = FunctionBind<decltype(&invoke), invoke, 2>;
    return f::invoke(L,p);
}
// ★ slua 的 authority 更接近模板展开后的原生 closure
```

#### 对比矩阵（本轮新增观察点）

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| UHT 产出 direct/stub 统计 artifact | None | None | None | None | None | Full |
| runtime direct bind 缺失时自动切换 fallback | None | Partial | N/A | N/A | N/A | Full |
| fallback rejection taxonomy 显式可读 | None | None | None | None | None | Full |
| 绑定 authority 最终落到 native bridge / thunk | Full | Full | Partial | Partial | Full | Full |
| 绑定 authority 最终落到 registry / translator / closure | None | Partial | Full | Full | Full | Partial |
| direct bind 强依赖现成 native pointer | Full | Partial | None | None | None | Partial |

#### 小结与建议

- 当前 Angelscript 相比 Hazelight 的新增优势，不是“多绑定了几个函数”，而是在同血缘方案下已经从单轨 direct bind 进化成 `FunctionTable + direct bind + reflective fallback` 三轨 authority。这属于实现质量差异，优先级 `P0`。
- UnrealCSharp / UnLua / puerts / sluaunreal 在 D2 上大多应判为“实现方式不同”，不应误写成“没有反射绑定”。
- 最值得吸收的不是改走别人的主线，而是把 current AS 的三轨命中情况继续做成可观测数据：建议补 `fallback hit rate / reject reason / per-module authority mix` 的运行时统计与 editor 展示，优先级 `P1`。

### [D3] Blueprint override 的 owner 与 patch 深度

#### 各插件实现概览

```
[D3-Deep] Override Owner / Patch Depth
AS(now) : ClassGenerator -> UASFunction(FUNC_Native) -> UASFunctionNativeThunk
HZ      : ClassGenerator -> UASFunction(FUNC_RuntimeGenerated) -> event binder
UC      : Registry bind -> DuplicateFunction / OriginalFunction -> execCallCSharp
UL      : UnLuaManager + ULuaFunction -> Override / Restore
PU      : UTypeScriptGeneratedClass -> LazyLoadRedirect / Redirect / Cancel
SL      : LuaOverrider -> duplicate super-call + override hook
```

本轮新增视角不是“能不能覆写 BlueprintEvent”，而是谁持有 override owner，以及 patch 打到 UE 调用链的哪一层。当前 Angelscript 的关键新发现是：override owner 已经前移到 class generation 阶段，`UASFunction` 一生成就挂上 `FUNC_Native + UASFunctionNativeThunk`。Hazelight 同位置仍主要停在 `FUNC_RuntimeGenerated`。这不是表面写法差异，而是 patch 深度差异。

#### 详细对比

##### 子维度 1：override owner 究竟挂在哪

- 当前 Angelscript 的 owner 在 `UASFunction` 与 generated class 本身。`AllocateFunctionFor()` 后马上 `SetSuperStruct()`、写入 Blueprint flags、并把 native 调用面收口到 `UASFunctionNativeThunk`。后面的 `Bind_BlueprintEvent.cpp` 更多是在补 script-name 索引，而不是首次决定 override owner。
- Hazelight 也在类生成阶段创建 `UASFunction`，但该对象默认仍保留 `FUNC_RuntimeGenerated`。它后续同样通过 `Bind_BlueprintEvent.cpp` 建索引，但 patch 深度没有 current AS 那么靠近 UE 原生 dispatch。
- UnrealCSharp 的 owner 在 `FCSharpBind`；UnLua 的 owner 在 `UUnLuaManager + ULuaFunction`；puerts 的 owner 在 `UTypeScriptGeneratedClass`；sluaunreal 的 owner 在 `LuaOverrider`。这些都属于“实现方式不同”，不是没有 override。

[1] 当前 Angelscript 已经把 script override 前推到 class generation；Hazelight 仍停在 `FUNC_RuntimeGenerated`：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 位置: 3411-3458
// ============================================================================
auto* NewFunction = UASFunction::AllocateFunctionFor(NewClass, FunctionName, FunctionDesc);
NewFunction->SetSuperStruct(ParentFunction);
...
NewFunction->ScriptFunction = FunctionDesc->ScriptFunction;
NewFunction->FunctionFlags |= FUNC_Native;
NewFunction->SetNativeFunc(&UASFunctionNativeThunk);
...
if ((FunctionDesc->bBlueprintEvent && FunctionDesc->bCanOverrideEvent) || FunctionDesc->bBlueprintOverride)
    NewFunction->FunctionFlags |= FUNC_BlueprintEvent;
// ★ 当前版本在生成期就把 Blueprint-facing script function 接入 UE native thunk

// ============================================================================
// 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/ClassGenerator/AngelscriptClassGenerator.cpp
// 位置: 3259-3303
// ============================================================================
auto* NewFunction = UASFunction::AllocateFunctionFor(NewClass, FunctionName, FunctionDesc);
NewFunction->SetSuperStruct(ParentFunction);
...
NewFunction->FunctionFlags |= FUNC_RuntimeGenerated;
NewFunction->ScriptFunction = FunctionDesc->ScriptFunction;
...
if ((FunctionDesc->bBlueprintEvent && FunctionDesc->bCanOverrideEvent) || FunctionDesc->bBlueprintOverride)
    NewFunction->FunctionFlags |= FUNC_BlueprintEvent;
// ★ Hazelight 同位置未见 current AS 的 `FUNC_Native + NativeThunk` 收口
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp
// 位置: 605-640
// ============================================================================
int32 FunctionId = FAngelscriptBinds::BindGlobalFunctionDirect(Signature.Declaration,
    asFUNCTION(CallStaticWithSignature), asCALL_GENERIC, ASAutoCaller::FunctionCaller::Make(), Sig);
...
GBlueprintEventsByScriptName.FindOrAdd(CastChecked<UClass>(Function->GetOuter())).Add(Signature.ScriptName, Function);
// ★ current AS 的 bind 阶段更像补 script-name 索引，owner 已经在类生成阶段定型
```

##### 子维度 2：patch 深度与回滚 surface

- UnrealCSharp 会在 bind 阶段 duplicate 并把 original/new function 改成 `execCallCSharp`；UnLua 会 duplicate、activate，并保留 `Restore()`；puerts 提供 `CancelFunctionRedirection()`；sluaunreal 会额外 duplicate super-call 再 hook override。后四者的共同点，是 owner 更靠近 bind/runtime patch，而不是 class generation。
- 当前 Angelscript 比 Hazelight 更深的一点，在于生成出来的 `UASFunction` 已经是 UE 看见的 native function。这属于实现质量差异。
- 当前 Angelscript 目前文档里未见与 UnLua / puerts 同级的显式 restore / cancel surface，因此这里应判为“没有实现公开撤销接口”，而不是“override 能力不足”。

[2] UnLua、puerts、sluaunreal、UnrealCSharp 都有 override，但 owner 与撤销面完全不同：

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaManager.cpp
// 位置: 305-316
// ============================================================================
UnLua::LowLevel::GetFunctionNames(Env->GetMainState(), Ref, BindInfo.LuaFunctions);
ULuaFunction::GetOverridableFunctions(Class, BindInfo.UEFunctions);
...
ULuaFunction::Override(Function, Class, LuaFuncName);
// ★ manager 负责收集配对，真正的 override owner 在 `ULuaFunction`

// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaFunction.cpp
// 位置: 160-189, 232-256
// ============================================================================
Overridden = static_cast<UFunction*>(StaticDuplicateObject(Function, GetOuter(), *DestName));
...
Function->FunctionFlags |= FUNC_Native;
Function->SetNativeFunc(&execScriptCallLua);
...
Function->SetNativeFunc(Overridden->GetNativeFunc());
Function->FunctionFlags = Overridden->FunctionFlags;
// ★ UnLua 的突出能力是 restore 语义

// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FCSharpBind.cpp
// 位置: 333-366, 377-386
// ============================================================================
const auto OverrideFunction = DuplicateFunction(OriginalFunction, InClass, *NewFunctionName);
...
OriginalFunction->SetNativeFunc(UCSharpFunction::execCallCSharp);
OriginalFunction->FunctionFlags |= FUNC_Native;
...
NewFunction = DuplicateFunction(OriginalFunction, InClass, FunctionName);
NewFunction->SetNativeFunc(UCSharpFunction::execCallCSharp);
// ★ UnrealCSharp 的 owner 在 registry bind，而不是 class generator
```

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/TypeScriptGeneratedClass.cpp
// 位置: 127-137, 203-229, 245-269
// ============================================================================
// 蓝图继承ts类...由于目前不支持ts继承蓝图...
...
Function->FunctionFlags |= FUNC_Native;
Function->SetNativeFunc(&UTypeScriptGeneratedClass::execLazyLoadCallJS);
...
InFunction->SetNativeFunc(&UTypeScriptGeneratedClass::execCallJS);
...
Function->FunctionFlags &= ~FUNC_Native;
Function->Bind();
// ★ puerts 公开了 cancel/rebind surface，但混合继承链有明确限制

// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaOverrider.cpp
// 位置: 1275-1283, 1398-1449
// ============================================================================
if (func && (func->FunctionFlags & OverrideFuncFlags)) {
    if (hookBpScript(func, cls, (FNativeFuncPtr)&ULuaOverrider::luaOverrideFunc)) {
        hookCounter++;
    }
}
...
auto supercallFunc = duplicateUFunction(func, cls, FName(*(SUPER_CALL_FUNC_NAME_PREFIX + func->GetName())), func->GetNativeFunc());
...
overrideFunc->SetNativeFunc(hookFunc);
overrideFunc->Script.Insert(Code, CodeSize, 0);
// ★ slua 把 owner 放在 overrider，并额外生成 super-call
```

##### 子维度 3：对象级 patch 与继承边界

- puerts 明确写了“目前不支持 ts 继承蓝图”，这是公开边界，应判为“没有实现该继承方向”，而不是模糊地写“支持较弱”。
- sluaunreal 的 `bHookInstancedObj` 路径说明 owner 可以下沉到对象级；current AS 目前更像类生成期一次性定型。
- 如果 current AS 未来要引入显式 rebind/cancel，最适合吸收的是 puerts/slua 的 surface 设计，但不应破坏现在的 `UASFunction + NativeThunk` 主线。

#### 对比矩阵（本轮新增观察点）

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| override owner 在 class generation 阶段落地 | Full | None | None | Partial | None | Full |
| 生成期即写入 `FUNC_Native` | None | None | None | None | None | Full |
| bind 阶段通过 duplicate + native bridge 改写 `UFunction` | None | Full | Full | Full | Full | Partial |
| 显式 restore / cancel surface | None | None | Full | Full | Partial | None |
| duplicate super-call 辅助 | None | None | Partial | None | Full | None |
| 对象级 hook / instanced patch | None | None | None | None | Full | None |
| 混合继承链存在公开限制说明 | None | None | None | Full | None | None |

#### 小结与建议

- 当前 Angelscript 在 D3 上最有价值的新确认，是“override owner 已经被前推到 class generation，并在生成期就接入 UE native thunk”。这比 Hazelight 更深，不是简单的实现方式差异，而是 patch 深度的质量提升，优先级 `P0`。
- 最值得吸收的是 UnLua / puerts / sluaunreal 的显式 rebind / restore / cancel surface，而不是把 owner 再后移。建议 current AS 在保留 `UASFunction + NativeThunk` 主线的前提下，补一层可控回滚接口，优先级 `P1`。
- puerts 的“继承限制显式化”和 slua 的“对象级 hook”都值得借鉴，但它们解决的是不同问题。当前 Angelscript 不应为了追求对象级 patch 而破坏现有 class-generation 合法性校验，优先级 `P2`。

### [D8] 优化 authority 的发生层级

#### 各插件实现概览

```
[D8-Deep] Optimization Authority Layers
AS(now) : Compile(UHT summary) -> Load(BindDB + PrecompiledData + StaticJIT) -> Call(ContextPool + CompileOut)
HZ      : Load(BindDB + PrecompiledData + StaticJIT) -> Call(CompileOut)
UC      : Generate fixed-shape InternalCall families -> Hash-based native bridge
UL      : Build flag -> PersistentParamBuffer -> Fast userdata/container cache
PU      : Backend flags -> Translator cache -> SlowCall / FastCall
SL      : UFunction accelerator cache -> Weak object/enum/property/function cache
```

这一轮新增结论不是“哪门语言更快”，而是优化 authority 分布在哪一层。当前 Angelscript 同时把优化放在编译期、装载期和调用期；Hazelight 主要在装载期与 compile-out；UnLua / puerts / sluaunreal 更强调桥接层快路径；UnrealCSharp 则通过固定形态 `InternalCall` 把 ABI 预先离散化。每家都有优化，但层级不同。

#### 详细对比

##### 子维度 1：编译期与装载期，谁在提前做工作

- 当前 Angelscript 的装载期优化链最完整：`BindDatabase`、`BindModules.Cache`、`PrecompiledData`、`StaticJIT`、build mismatch 校验都在 `FAngelscriptEngine` 启动过程中串起来；再加上 UHT 侧 `AS_FunctionTable_Summary`，它不是纯 runtime 调优。
- Hazelight 有非常相近的 `PrecompiledData + StaticJIT + BindDatabase + compileOut` 主线，但这一轮没有读到 current AS 那种 UHT direct/stub summary 与 bind module 自动分片日志，因此应判为“优化层级较少”，不是“没有优化”。
- UnrealCSharp 更像在生成期把调用 ABI 分桶；它的优势在“减少 bridge 形态分支”，不是 cache/JIT pipeline。

[1] 当前 Angelscript 的优化 authority 跨编译期 artifact、装载期 cache/JIT、调用期 context pool 与 compile-out；Hazelight 少了最前面的可观测层：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 1433-1556, 1795-1824
// ============================================================================
StaticJIT = new FAngelscriptStaticJIT();
StaticJIT->PrecompiledData = PrecompiledData;
Engine->SetJITCompiler(StaticJIT);
...
FAngelscriptBindDatabase::Get().Load(GetScriptRootDirectory() / TEXT("Binds.Cache"), bGeneratePrecompiledData);
...
PrecompiledData->Load(Filename);
if (!PrecompiledData->IsValidForCurrentBuild())
{
    UE_LOG(Angelscript, Warning, TEXT("Loaded angelscript precompiled data was for a different build configuration. Discarding all precompiled data."));
}
...
if (CompiledInfo != nullptr && CompiledInfo->PrecompiledDataGuid != PrecompiledData->DataGuid)
{
    UE_LOG(Angelscript, Warning, TEXT("Loaded angelscript precompiled data does not match the transpiled C++ in the game binary. Transpiled code will not be used!"));
    FJITDatabase::Get().Clear();
}
...
if (asCContext* MatchingContext = TryTakeContextFromPool(LocalPool.FreeContexts, DesiredScriptEngine))
{
    Context = MatchingContext;
}
// ★ current AS 把 load-time artifact validation 与 call-time context reuse 放在同一条主线里

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp
// 位置: 531-554
// ============================================================================
if (UE_BUILD_TEST || UE_BUILD_SHIPPING || (WITH_EDITOR && FAngelscriptEngine::IsSimulatingCookedForCurrentContext()))
{
    Function->compileOutType = asECompileOutType::ReplaceWithFirstParam;
}
...
Function->compileOutType = asECompileOutType::CompileOutEntirely;
// ★ compile-out 让部分调用在产物层就变成 no-op 或替身

// ============================================================================
// 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/AngelscriptManager.cpp
// 位置: 361-489
// ============================================================================
bUsePrecompiledData = !bGeneratePrecompiledData && !FParse::Param(FCommandLine::Get(), TEXT("as-ignore-precompiled-data"))
    && !IsRunningCommandlet() && !WITH_EDITOR && !bScriptDevelopmentMode;
...
FAngelscriptBindDatabase::Get().Load(GetScriptRootDirectory() / TEXT("Binds.Cache"), bGeneratePrecompiledData);
...
PrecompiledData->Load(Filename);
if (!PrecompiledData->IsValidForCurrentBuild())
{
    UE_LOG(Angelscript, Warning, TEXT("Loaded angelscript precompiled data was for a different build configuration. Discarding all precompiled data."));
}
// ★ Hazelight 同样重视装载期优化，但本轮未见 current AS 同级的 UHT summary contract
```

##### 子维度 2：桥接层快路径由谁维护

- UnrealCSharp 通过固定形态 `InternalCall` 族，把一部分 ABI 选择提前离散化；UnLua 把优化压在参数缓冲与 userdata/container cache；puerts 压在 `FastCall` 和 translator cache；slua 压在 `LuaFunctionAccelerator` 和 LuaState 多级 cache。
- 当前 Angelscript 的调用期优势主要不在参数 marshaling fast path，而在 `thread-local context pool` 与 `compileOutType`。也就是说，它更强调“尽量少进桥接”，而不是“桥接后尽量快”。

[2] UnrealCSharp 与 UnLua 更偏 ABI/参数快路径；puerts 与 sluaunreal 更偏 translator/accelerator cache：

```csharp
// ============================================================================
// 文件: Reference/UnrealCSharp/Script/UE/Library/FFunctionImplementation.cs
// 位置: 7-102
// ============================================================================
[MethodImpl(MethodImplOptions.InternalCall)]
public static extern void FFunction_GenericCall0Implementation(nint InMonoObject, uint InFunctionHash);
...
[MethodImpl(MethodImplOptions.InternalCall)]
public static extern void FFunction_PrimitiveCall3Implementation(nint InMonoObject, uint InFunctionHash,
    byte* InBuffer, byte* ReturnBuffer);
...
[MethodImpl(MethodImplOptions.InternalCall)]
public static extern void FFunction_CompoundCall15Implementation(nint InMonoObject, uint InFunctionHash,
    byte* InBuffer, byte* OutBuffer, byte* ReturnBuffer);
// ★ UnrealCSharp 通过固定桥接族减少 ABI 分支

// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/UnLua.Build.cs
// 位置: 91-95
// ============================================================================
loadBoolConfig("bEnablePersistentParamBuffer", "ENABLE_PERSISTENT_PARAM_BUFFER", true);

// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/ParamBufferAllocator.cpp
// 位置: 38-70
// ============================================================================
if (Counter < Buffers.Num())
    return Buffers[Counter++];
...
return MakeShareable(new FParamBufferAllocator_Persistent(Func));

// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaCore.cpp
// 位置: 320-356, 446-507
// ============================================================================
if (Type == LUA_TUSERDATA)
{
    ...
}
...
lua_getfield(L, LUA_REGISTRYINDEX, "ScriptContainerMap");
...
lua_rawset(L, -4);
// ★ UnLua 把调用期优化压在 param buffer、userdata 快取和 container cache
```

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp
// 位置: 277-350
// ============================================================================
void FFunctionTranslator::SlowCall(...)
{
    CallObject->UObject::ProcessEvent(CallFunction, Params);
}
void FFunctionTranslator::FastCall(...)
{
    FFrame NewStack(CallObject, CallFunction, Params, nullptr, Function->ChildProperties);
    ...
}
// ★ puerts 明确区分 `SlowCall / FastCall`

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp
// 位置: 38-69
// ============================================================================
auto Iter = MethodsMap.Find(InFunction->GetFName());
if (!Iter)
{
    auto FunctionTranslator = std::make_shared<FFunctionTranslator>(InFunction, false);
    MethodsMap.Add(InFunction->GetFName(), FunctionTranslator);
}
// ★ translator 本身也按 `UFunction` 缓存

// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaFunctionAccelerator.cpp
// 位置: 33-56, 145-179
// ============================================================================
LuaFunctionAccelerator::LuaFunctionAccelerator(UFunction* inFunc)
    : func(inFunc)
    , bLuaOverride(ULuaOverrider::isUFunctionHooked(inFunc))
{
    ...
    paramsChecker.Add(checkerInfo);
}
...
auto value = new LuaFunctionAccelerator(inFunc);
cache.Emplace(inFunc, value);
// ★ slua 把参数检查、out 参数处理、latent 判定前置到 accelerator cache

// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaState.cpp
// 位置: 86-94, 570-583
// ============================================================================
cacheObjRef = newCacheTable(L);
cacheEnumRef = luaL_ref(L, LUA_REGISTRYINDEX);
cacheClassPropRef = luaL_ref(L, LUA_REGISTRYINDEX);
cacheClassFuncRef = luaL_ref(L, LUA_REGISTRYINDEX);
// ★ slua 还维护 object / enum / class prop / class func 多级 cache
```

##### 子维度 3：跨层可观测 contract 是否存在

- 当前 Angelscript 在 D8 上最特别的，不只是优化点多，而是编译期 `FunctionTable summary`、装载期 `PrecompiledData` 校验、调用期 `compileOutType/context pool` 可以互相对位。
- 其他插件都能找到各自优化点，但本轮源码里没有读到 current AS 这种贯穿编译期到调用期的统一 artifact。因此不要把“局部快路径很多”误判成“整体 authority 更强”。

#### 对比矩阵（本轮新增观察点）

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 编译期产生可观察的 binding 指标 | None | Partial | None | None | None | Full |
| 启动期 `PrecompiledData + StaticJIT` 主线 | Full | None | None | None | None | Full |
| build mismatch / GUID 校验 | Full | None | None | None | None | Full |
| 调用期 thread-local context pool | Partial | None | None | None | None | Full |
| 参数缓冲复用 / container cache | None | None | Full | Partial | Partial | Partial |
| fast path / specialized ABI bridge | Partial | Full | Partial | Full | Full | Full |
| compile-out development-only API | Full | None | None | None | None | Full |
| `UFunction` 级 translator / accelerator cache | None | None | None | Full | Full | Partial |

#### 小结与建议

- 当前 Angelscript 在 D8 上新增确认的优势，是优化 authority 覆盖了编译期、装载期、调用期三层，而不是只在某一层做局部快路径。这是 current AS 与 Hazelight/其他插件最实质的结构差异，优先级 `P0`。
- UnrealCSharp、UnLua、puerts、sluaunreal 的优化都值得借鉴，但吸收方向应按层级区分：`UnrealCSharp = ABI 预分桶`，`UnLua = param buffer / userdata cache`，`puerts = translator fast path`，`slua = per-UFunction accelerator`。
- 对 current AS 的最直接建议，是继续保持 `PrecompiledData + StaticJIT + direct bind` 主线，同时补两类可观测性：`reflective/direct hit rate` 与 `context pool / compileOut hotspot` 诊断。优先级 `P1`。如果未来继续向调用期优化下探，优先吸收 puerts/slua 的 `per-UFunction` 热点统计方式，而不是削弱现有装载期 pipeline，优先级 `P2`。

---

## 深化分析 (2026-04-08 19:13:43)

本轮已复读 `Hazelight_Analysis.md`、`UnrealCSharp_Analysis.md`、`UnLua_Analysis.md`、`puerts_Analysis.md`、`sluaunreal_Analysis.md`。以下只补前文相对薄的 `D1` 与 `D10`，并修正一个事实前提：纵向分析文档本轮确实存在，下面的判断同时来自纵向结论复用与本轮源码复核。

### [D1] 工具链边界是否进入模块图

#### 各插件实现概览

```
[D1-Deep] Toolchain Boundary Visibility
HZ : Code + Editor + Loader                         // docs/test hidden inside runtime
AS : Runtime + Editor + Test | UHTTool(.ubtplugin) // test visible, UHT tool hidden from uplugin
UC : Runtime + Core + Editor + ScriptCodeGenerator + Compiler + SourceCodeGenerator
UL : Runtime + Editor(IntelliSense/templates) + DefaultParamCollector
PU : WasmCore + JsEnv + Puerts + PuertsEditor + DeclarationGenerator + ParamDefaultValueMetas
SL : slua_unreal + slua_profile | Tools/lua-wrapper + democpp
```

这一轮补到源码后，`D1` 最关键的新发现不是“谁模块多”，而是“谁把工具链 contract 暴露给了模块图”。`UnrealCSharp` 与 `puerts` 让生成器/Program 成为插件声明的一部分；`UnLua` 处在中间态，`Program` 被显式声明，但 IntelliSense 仍放在 `UnLuaEditor`；Hazelight 与当前 Angelscript 则更偏“runtime owns everything”，只是当前 Angelscript 已经把测试层剥离出来，而 `AngelscriptUHTTool` 仍停留在 `.ubtplugin` 层。

#### 详细对比

##### 子维度 1：codegen / declaration tool 是否被提升为第一等模块

- `UnrealCSharp` 在 `Reference/UnrealCSharp/UnrealCSharp.uplugin:18-54` 里直接暴露 `ScriptCodeGenerator`、`Compiler`、`SourceCodeGenerator`。这意味着工程使用者从插件描述文件就能看见“运行时 + 编辑器 + 生成器 + Program”的完整工具链边界。
- `puerts` 的 `Reference/puerts/unreal/Puerts/Puerts.uplugin:15-48` 同样把 `DeclarationGenerator` 与 `ParamDefaultValueMetas` 写进模块图；声明生成与默认值元数据采集都不是隐式流程。
- `UnLua` 在 `Reference/UnLua/Plugins/UnLua/UnLua.uplugin:23-40` 已经把 `UnLuaDefaultParamCollector` 提升成 `Program`，但 IntelliSense 仍归 `UnLuaEditor` 持有，因此它是“部分显式化”，不是像 `UnrealCSharp`/`puerts` 那样把主要工具链完全拆成独立 module/program。
- 当前 Angelscript 的 `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptUHTTool.ubtplugin.csproj:1-18,40-53` 证明 UHT 导出器已经是独立的 build-time plugin，但 `Plugins/Angelscript/Angelscript.uplugin:18-33` 只暴露 `Runtime / Editor / Test` 三模块。结论应判为“工具存在但未进入插件模块图”，不是“没有工具链”。
- `sluaunreal` 的 `Reference/sluaunreal/Plugins/slua_unreal/slua_unreal.uplugin:16-27` 只有 `slua_unreal` 与 `slua_profile`；静态导出工具则在 `Reference/sluaunreal/Tools/README.md:1-37` 以外部 `lua-wrapper` 形式存在。这与 `puerts`/`UnrealCSharp` 的差异是“可见性与集成方式不同”，不是“功能缺失”。

[1] 各插件 `uplugin` 暴露的工具链边界：

```jsonc
// ============================================================================
// 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Angelscript.uplugin
// 位置: 18-34，Hazelight 只有 Code / Editor / Loader 三模块
// ============================================================================
"Modules": [
	{ "Name": "AngelscriptCode", "Type": "Runtime", "LoadingPhase": "PostDefault" },
	{ "Name": "AngelscriptEditor", "Type": "Editor", "LoadingPhase": "PostDefault" },
	{ "Name": "AngelscriptLoader", "Type": "Runtime", "LoadingPhase": "PostDefault" }
]

// ============================================================================
// 文件: Plugins/Angelscript/Angelscript.uplugin
// 位置: 18-33，当前 AS 把 Test 暴露出来，但 UHT tool 不在模块列表
// ============================================================================
"Modules": [
	{ "Name": "AngelscriptRuntime", "Type": "Runtime", "LoadingPhase": "PostDefault" },
	{ "Name": "AngelscriptEditor", "Type": "Editor", "LoadingPhase": "PostDefault" },
	{ "Name": "AngelscriptTest", "Type": "Editor", "LoadingPhase": "PostDefault" }
]

// ============================================================================
// 文件: Reference/UnrealCSharp/UnrealCSharp.uplugin
// 位置: 18-54，生成器 / 编译器 / Program 都在模块图中可见
// ============================================================================
"Modules": [
	{ "Name": "UnrealCSharp", "Type": "Runtime", "LoadingPhase": "Default" },
	{ "Name": "UnrealCSharpEditor", "Type": "Editor", "LoadingPhase": "Default" },
	{ "Name": "ScriptCodeGenerator", "Type": "Editor", "LoadingPhase": "Default" },
	{ "Name": "Compiler", "Type": "Editor", "LoadingPhase": "Default" },
	{ "Name": "UnrealCSharpCore", "Type": "Runtime", "LoadingPhase": "Default" },
	{ "Name": "CrossVersion", "Type": "Runtime", "LoadingPhase": "Default" },
	{ "Name": "SourceCodeGenerator", "Type": "Program", "LoadingPhase": "PostConfigInit" }
]

// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/UnLua.uplugin
// 位置: 23-40，UnLua 把默认参数采集提成 Program，但 IntelliSense 仍留在 Editor
// ============================================================================
"Modules": [
	{ "Name": "UnLua", "Type": "Runtime", "LoadingPhase": "PreDefault" },
	{ "Name": "UnLuaEditor", "Type": "Editor", "LoadingPhase": "Default" },
	{ "Name": "UnLuaDefaultParamCollector", "Type": "Program", "LoadingPhase": "PostConfigInit" }
]

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Puerts.uplugin
// 位置: 15-48，声明生成器与默认值元数据工具都是显式模块
// ============================================================================
"Modules": [
	{ "Name": "WasmCore", "Type": "Runtime", "LoadingPhase": "PreDefault" },
	{ "Name": "JsEnv", "Type": "Runtime", "LoadingPhase": "PreDefault" },
	{ "Name": "DeclarationGenerator", "Type": "Editor", "LoadingPhase": "Default" },
	{ "Name": "ParamDefaultValueMetas", "Type": "Program", "LoadingPhase": "PostConfigInit" },
	{ "Name": "Puerts", "Type": "Runtime", "LoadingPhase": "PostEngineInit" },
	{ "Name": "PuertsEditor", "Type": "Editor", "LoadingPhase": "PostEngineInit" }
]

// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/slua_unreal.uplugin
// 位置: 16-27，slua 只有 runtime / profiler，工具链不进入插件模块图
// ============================================================================
"Modules": [
	{ "Name": "slua_unreal", "Type": "Runtime", "LoadingPhase": "PreLoadingScreen" },
	{ "Name": "slua_profile", "Type": "Editor", "LoadingPhase": "PreDefault" }
]
```

[2] `uplugin` 外的 build-time tool 仍然真实存在，但 owner 不同：

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptUHTTool.ubtplugin.csproj
// 位置: 1-18, 40-53，当前 AS 的 UHT 导出器是 .NET ubtplugin，不是 uplugin 模块
// ============================================================================
<Project Sdk="Microsoft.NET.Sdk">
  <Import Project="$(EngineDir)\Source\Programs\Shared\UnrealEngine.csproj.props" />
  <PropertyGroup>
    <TargetFramework>net8.0</TargetFramework>
    <OutputType>Library</OutputType>
    <AssemblyName>AngelscriptUHTTool</AssemblyName>
    <OutputPath>..\..\Binaries\DotNET\UnrealBuildTool\Plugins\AngelscriptUHTTool\</OutputPath>
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

// ============================================================================
// 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/ScriptCodeGenerator.Build.cs
// 位置: 25-46，UnrealCSharp 把生成器直接做成 Editor module
// ============================================================================
PublicDependencyModuleNames.AddRange(new string[]
{
	"Core",
	"UnrealCSharpCore"
});

PrivateDependencyModuleNames.AddRange(new string[]
{
	"CoreUObject",
	"Engine",
	"Slate",
	"SlateCore",
	"UMGEditor",
	"UnrealEd",
	"UnrealCSharpCore",
	"CrossVersion",
	"Json"
});

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/DeclarationGenerator.Build.cs
// 位置: 21-40, 53-59，puerts 的 declaration 工具与 runtime 模块显式互相依赖
// ============================================================================
PublicDependencyModuleNames.AddRange(new string[]
{
	"Core",
	"CoreUObject",
	"UMG",
	"UnrealEd",
	"LevelEditor",
	"Engine",
	"Slate",
	"SlateCore",
	"Projects",
	"JsEnv",
	"Puerts",
	"ToolMenus",
});

if (JsEnv.WithSourceControl)
{
	PrivateDependencyModuleNames.Add("PuertsEditor");
}

// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/UnLuaEditor.Build.cs
// 位置: 59-84，UnLua 把 IntelliSense / 模板能力并入 Editor module
// ============================================================================
PrivateDependencyModuleNames.AddRange(new[]
{
	"Core",
	"CoreUObject",
	"Engine",
	"UnrealEd",
	"Projects",
	"UMG",
	"UMGEditor",
	"BlueprintGraph",
	"DirectoryWatcher",
	"Networking",
	"Sockets",
	"UnLua",
	"Lua",
	"ToolMenus"
});
```

##### 子维度 2：测试 / 学习资产是否从 runtime 主模块剥离

- Hazelight 的 `J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/AngelscriptTestCommandlet.cpp:1-24` 和 `.../Private/Testing/DiscoverTests.cpp:166-203` 表明测试发现与命令行入口都还埋在 `AngelscriptCode` 里，属于“runtime owns validation”。
- 当前 Angelscript 则在 `Plugins/Angelscript/Angelscript.uplugin:29-33` 与 `Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs:23-49` 中把验证层正式拆成 `AngelscriptTest` 模块。这不是功能新增，而是架构边界调整。
- `sluaunreal` 的学习资产走了另一条路：`democpp.uproject` 与 `Tools/README.md:33-37` 说明样例与静态导出工具都在插件主模块之外。它也实现了“剥离”，但 owner 更靠近独立 demo/tool，而不是正式测试模块。

[3] Hazelight 的测试入口仍在 runtime；当前 Angelscript 已拆成独立测试模块：

```cpp
// ============================================================================
// 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/AngelscriptTestCommandlet.cpp
// 位置: 1-24，Hazelight 的测试 commandlet 仍属于 AngelscriptCode
// ============================================================================
int32 UAngelscriptTestCommandlet::Main(const FString& Params)
{
	if (!FAngelscriptManager::Get().bDidInitialCompileSucceed)
	{
		return 1;
	}

	if (!RunAngelscriptUnitTests(FAngelscriptManager::Get().GetActiveModules(), &FAngelscriptManager::Get(), 0, 0))
	{
		return 2;
	}

	return 0;
}

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs
// 位置: 23-49，当前 AS 把验证层正式作为独立模块暴露给 build graph
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

#### 对比矩阵

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| `uplugin` 中显式声明 codegen / declaration 模块 | None | Full | Partial | Full | None | None |
| 独立 build-time tool 以 `Program` 或 `.ubtplugin` 形式存在 | None | Full | Full | Full | Partial | Full |
| 测试 / 验证层从 runtime 主模块正式剥离 | None | None | None | None | Partial | Full |
| 工具链 owner 可仅通过模块图直接识别 | Partial | Full | Partial | Full | None | Partial |

#### 小结与建议

- 当前 Angelscript 在 `D1` 上的新定位不是“模块过少”，而是“已经拥有 build-time tool，但没有把它提升成用户一眼能看见的 plugin contract”。这与 `UnrealCSharp` / `puerts` 的差距属于“显式化程度不同”，优先级 `P1`。
- `AngelscriptTest` 独立成模块仍是当前方案最值得保留的结构演进，和 Hazelight 相比这是清晰的实现质量提升，优先级 `P0`。
- 如果后续继续扩张 `UHT`、声明生成、文档导出，建议先学 `UnrealCSharp` / `puerts` 的“工具链边界显式化”，而不是盲目复制它们的全部模块数量；对 `slua` 的外置工具路线，只适合作为补充，不适合作为主线。

### [D10] onboarding artifact 的 owner：文档、模板、教程、示例谁真正负责

#### 各插件实现概览

```
[D10-Deep] Onboarding Artifact Flow
HZ : bind/docs macros -> DumpDocumentation() -> generated .hpp
AS : bind/docs macros + example tests -> generated .hpp + compile-checked examples
UC : Template/*.cs + FSolutionGenerator -> ready C# solution
UL : README -> toolbar/template -> AssetRegistry export + runnable tutorials
PU : README -> GenDTS command -> *.d.ts + builtin libs
SL : README + democpp project + lua-wrapper guide
```

这一轮把 `D10` 往下挖后，差异不再只是“谁文档多”，而是“新用户第一次接触插件时，真正能落地的 artifact 是什么”。Hazelight / 当前 Angelscript 的主文档资产其实是由绑定系统导出的伪头文件；`UnrealCSharp` 的主入口是脚手架模板；`UnLua` 的强项是“文档 + 教程内容 + 编辑器生成”三件套；`puerts` 更像“README + `.d.ts` 生成按钮”；`sluaunreal` 则把 onboarding 压到 demo project 和工具说明。

#### 详细对比

##### 子维度 1：API 文档是运行时导出物，还是外部文档站 / IDE 产物

- Hazelight 与当前 Angelscript 都在运行时持有 `FAngelscriptDocs::DumpDocumentation()`，并把结果写到 `Docs/angelscript/generated/*.hpp`。这意味着两者的 API 参考是“绑定系统副产物”，而不是独立站点。
- 但两者并不完全相同。Hazelight 的 `J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/AngelscriptDocs.h:4-15,20-35` 仍保留 `SCRIPT_PROPERTY_DOCUMENTATION` 与 `SCRIPT_MANUAL_BIND_META`；当前 Angelscript 的 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.h:4-12,16-36` 已只剩函数/全局变量入口。这里应判为“文档 authoring surface 收窄”，不是“没有文档导出”。
- `UnLua` 的 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:42-106,169-234` 表明它把 IDE 提示导出建立在 `AssetRegistry` 扫描之上，并额外复制插件自带 IntelliSense 内容。它更像“编辑器生成的 hint/stub”，而不是 Hazelight/AS 那种 runtime API 参考。
- `puerts` 的 `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/GenDTSCommands.cpp:13-17` 只暴露了一个“Generate *.d.ts and copy some js builtin libs”的按钮；从 `Reference/puerts/unreal/README.md:1-15` 看，Unreal 侧 onboarding 更依赖 README 与声明生成，不像 `UnLua` 那样有仓库内教程矩阵。
- `UnrealCSharp` 的主入口也不是 runtime docs，而是脚手架模板；`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FSolutionGenerator.cpp:9-126` 会把 `Template/*.csproj`、`Template/*.cs`、`.sln` 复制并替换成项目内可编译结构。

[1] Hazelight / 当前 Angelscript 的文档导出同源，但当前 authoring surface 更窄：

```cpp
// ============================================================================
// 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/AngelscriptDocs.h
// 位置: 4-15, 20-35，Hazelight 仍暴露 property 与 manual meta 文档入口
// ============================================================================
#if WITH_EDITOR
#define AS_DOC(FunctionId, Documentation) FAngelscriptDocs::AddUnrealDocumentation(FunctionId, Documentation);
#define SCRIPT_BIND_DOCUMENTATION(Documentation) FAngelscriptDocs::AddUnrealDocumentation(FAngelscriptBinds::GetPreviousFunctionId(), TEXT(Documentation), TEXT(""), nullptr);
#define SCRIPT_GLOBAL_DOCUMENTATION(Documentation) FAngelscriptDocs::AddDocumentationForGlobalVariable(FAngelscriptBinds::GetPreviousGlobalVariableId(), TEXT(Documentation));
#define SCRIPT_PROPERTY_DOCUMENTATION(Binds, Documentation) FAngelscriptDocs::AddUnrealDocumentationForProperty(Binds.GetTypeId(), FAngelscriptBinds::GetPreviousPropertyOffset(), TEXT(Documentation));
#define SCRIPT_MANUAL_BIND_META(MetaName, MetaValue) FAngelscriptDocs::AddScriptFunctionMeta(FAngelscriptBinds::GetPreviousFunctionId(), TEXT(MetaName), TEXT(MetaValue));
#endif

struct ANGELSCRIPTCODE_API FAngelscriptDocs
{
	static void AddUnrealDocumentationForProperty(int TypeId, int PropertyOffset, FStringView Documentation);
	static void AddScriptFunctionMeta(int FunctionId, FStringView MetaName, FStringView MetaValue);
	static const TMap<FString, FString>* GetScriptFunctionMeta(int FunctionId);
};

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.h
// 位置: 4-12, 16-36，当前 AS 仍保留函数/全局变量文档，但 property/meta 入口已经不在头文件里
// ============================================================================
#if WITH_EDITOR
#define AS_DOC(FunctionId, Documentation) FAngelscriptDocs::AddUnrealDocumentation(FunctionId, Documentation);
#define SCRIPT_BIND_DOCUMENTATION(Documentation) FAngelscriptDocs::AddUnrealDocumentation(FAngelscriptBinds::GetPreviousFunctionId(), TEXT(Documentation), TEXT(""), nullptr);
#define SCRIPT_GLOBAL_DOCUMENTATION(Documentation) FAngelscriptDocs::AddDocumentationForGlobalVariable(FAngelscriptBinds::GetPreviousGlobalVariableId(), TEXT(Documentation));
#endif

struct ANGELSCRIPTRUNTIME_API FAngelscriptDocs
{
	static void AddUnrealDocumentation(int FunctionId, FStringView Documentation, FStringView Category, UFunction* Function);
	static void AddDocumentationForGlobalVariable(int GlobalVariableId, FStringView Documentation);
	static void DumpDocumentation(asIScriptEngine* Engine);
};

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp
// 位置: 407-515, 675-755，当前 AS 仍会从 script engine / UProperty / UFunction 收集元数据并落地到 generated headers
// ============================================================================
void FAngelscriptDocs::DumpDocumentation(asIScriptEngine* Engine)
{
	TMap<FString, FDocClass> Classes;
	...
	auto ResolveAccessors = [&](FDocClass& Class, UClass* UnrealClass = nullptr, const FString* StaticName = nullptr)
	{
		...
		Prop.Documentation = PropDesc->GetMetaData("ToolTip");
		Prop.Category = PropDesc->GetMetaData("Category");
		...
	};
	...
	FString Filename = FPaths::ProjectDir() / TEXT("/Docs/angelscript/generated") / ClassDoc.ClassName + TEXT(".hpp");
	FFileHelper::SaveStringToFile(Content, *Filename);
}
```

[2] `UnrealCSharp` 的 onboarding 主体是“能直接编译的模板”，不是运行时导出的 API 文档：

```csharp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FSolutionGenerator.cpp
// 位置: 9-126，脚手架会把 Template/*.csproj / *.cs / *.sln 复制到项目路径
// ============================================================================
void FSolutionGenerator::Generator()
{
	const auto TemplatePath = FUnrealCSharpFunctionLibrary::GetPluginTemplateDirectory();
	const auto ScriptPath = FUnrealCSharpFunctionLibrary::GetPluginScriptDirectory();

	CopyTemplate(FUnrealCSharpFunctionLibrary::GetUEProjectPath(), TemplatePath / DEFAULT_UE_NAME + PROJECT_SUFFIX, ...);
	CopyTemplate(FUnrealCSharpFunctionLibrary::GetGameProjectPath(), TemplatePath / DEFAULT_GAME_NAME + PROJECT_SUFFIX, ...);
	CopyTemplate(FPaths::Combine(FUnrealCSharpFunctionLibrary::GetFullScriptDirectory(),
	                FUnrealCSharpFunctionLibrary::GetScriptDirectory() + SOLUTION_SUFFIX),
	                TemplatePath / SOLUTION_NAME + SOLUTION_SUFFIX, ...);
}

// ============================================================================
// 文件: Reference/UnrealCSharp/Template/Override/Actor.cs
// 位置: 1-24，模板本身就是“可编译的最小 override 示例”
// ============================================================================
[Override]
public partial class AActor
{
	[Override]
	public override void ReceiveBeginPlay()
	{
		base.ReceiveBeginPlay();
	}
}
```

##### 子维度 2：仓库里的示例是“说明性文本”，还是“可执行 / 可校验资产”

- 当前 Angelscript 的 `Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleTestSupport.cpp:16-59` 直接把示例脚本拼成内存模块并编译。这意味着示例不是 README 附件，而是自动化验证输入。
- `UnLua` 的 `Reference/UnLua/README.md:29-69` 明确把 `Content/Script/Tutorials/*.lua` 与 `Content/Tutorials/*.umap` 当成教程矩阵；`Reference/UnLua/Source/TPSProject/TutorialBlueprintFunctionLibrary.cpp:11-105` 还把 `CppCallLua`、`CustomLoader` 这些教程写成了真实 C++ 入口。它的 onboarding 不是抽象文档，而是“文章 + 关卡 + 脚本 + 蓝图库”四件套。
- `sluaunreal` 的 `Reference/sluaunreal/Source/democpp/MyGameInstance.cpp:36-65` 在 demo project 中直接创建 `LuaState` 并绑定 `LoadFileDelegate`，说明它偏“独立示例工程 first”；`Reference/sluaunreal/README.md:83-210` 也围绕 demo 代码解释 `override` 与 `Blueprint` 交互。
- 本轮在 `Reference/puerts/unreal/` 目录里检索到的 Unreal 侧 onboarding 入口，主要是 `README.md` 与 `DeclarationGenerator`；没有定位到与 `Reference/UnLua/Content/Tutorials/`、`Reference/UnrealCSharp/Template/`、`Reference/sluaunreal/democpp.uproject` 对等的仓库内示例工程。因此这里应判为“仓库内示例较弱”，不是“没有文档”。
- Hazelight 仍有测试与文档导出，但本轮未定位到与 `UnLua Tutorials` 或 `democpp` 对等的新手样例工程；它更像“工程内 API/测试基础设施”，不强调独立 onboarding 资产。

[3] 当前 Angelscript、UnLua、slua 的示例资产都可执行，但 owner 完全不同：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleTestSupport.cpp
// 位置: 16-59，当前 AS 把示例脚本作为自动化测试输入，编译成功才算样例有效
// ============================================================================
bool RunScriptExampleCompileTest(FAutomationTestBase& Test, const FScriptExampleSource& Example)
{
	const FString ModuleNameString = FPaths::GetBaseFilename(Example.ExampleFileName);
	FAngelscriptEngine& Engine = AcquireCleanSharedCloneEngine();
	...
	const FString VirtualFileName = FString::Printf(TEXT("ScriptExamples/%s"), *ExampleFileName);
	const bool bCompiled = CompileAnnotatedModuleFromMemory(&Engine, ModuleName, VirtualFileName, CombinedScriptCode);
	Test.TestTrue(*FString::Printf(TEXT("Compile example '%s' succeeds"), *ExampleFileName), bCompiled);
	return bCompiled;
}

// ============================================================================
// 文件: Reference/UnLua/Source/TPSProject/TutorialBlueprintFunctionLibrary.cpp
// 位置: 11-105，UnLua 的教程不是空文档，而是实际可运行的 C++ / Lua / loader 样例
// ============================================================================
void UTutorialBlueprintFunctionLibrary::CallLuaByGlobalTable()
{
	UnLua::FLuaEnv Env;
	const auto bSuccess = Env.DoString("G_08_CppCallLua = require 'Tutorials.08_CppCallLua'");
	check(bSuccess);
	...
}

void UTutorialBlueprintFunctionLibrary::SetupCustomLoader(int Index)
{
	switch (Index)
	{
	case 1:
		FUnLuaDelegates::CustomLoadLuaFile.BindStatic(CustomLoader1);
		break;
	case 2:
		FUnLuaDelegates::CustomLoadLuaFile.BindStatic(CustomLoader2);
		break;
	}
}

// ============================================================================
// 文件: Reference/sluaunreal/Source/democpp/MyGameInstance.cpp
// 位置: 36-65，slua 的 onboarding 更接近“单独 demo project 演示完整运行时接线”
// ============================================================================
void UMyGameInstance::CreateLuaState()
{
	NS_SLUA::LuaState::onInitEvent.AddUObject(this, &UMyGameInstance::LuaStateInitCallback);
	state = new NS_SLUA::LuaState("SLuaMainState", this);
	state->setLoadFileDelegate([](const char* fn, FString& filepath)->TArray<uint8> {
		FString path = FPaths::ProjectContentDir();
		path /= "Lua";
		...
		FFileHelper::LoadFileToArray(Content, *fullPath);
		...
	});
	state->init();
}
```

[4] `UnLua` / `puerts` 的“文档入口”也体现了 owner 差异：

```md
<!-- =========================================================================
文件: Reference/UnLua/README.md
位置: 29-69，README 直接把教程、文档、最佳实践入口列成矩阵
============================================================================ -->
## 快速开始
1. 新建蓝图后打开，在UnLua工具栏中选择 `绑定`
2. 在接口的 `GetModule` 函数中填入Lua文件路径
3. 选择UnLua工具栏中的 `创建Lua模版文件`
4. 打开 `Content/Script/GameModes/BP_MyGameMode.lua` 编写你的代码

# 更多示例
* [01_HelloWorld](Content/Script/Tutorials/01_HelloWorld.lua)
* [02_OverrideBlueprintEvents](Content/Script/Tutorials/02_OverrideBlueprintEvents.lua)
...
* [13_AnimNotify](Content/Script/Tutorials/AN_FootStep.lua)

<!-- =========================================================================
文件: Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/GenDTSCommands.cpp
位置: 13-17，Unreal 侧最直接的 onboarding 动作为“生成 *.d.ts”
============================================================================ -->
void FGenDTSCommands::RegisterCommands()
{
	UI_COMMAND(
		PluginAction, "puerts", "Generate *.d.ts and copy some js builtin libs", EUserInterfaceActionType::Button, FInputChord());
}

<!-- =========================================================================
文件: Reference/puerts/unreal/README.md
位置: 1-15，puerts Unreal 目录的说明入口更偏概览，而不是仓库内教程矩阵
============================================================================ -->
## puerts for unreal
* 无需胶水代码的生成即可访问任意蓝图接口
* 非蓝图类可以通过生成代码静态Binding访问
* 支持TypeScript类继承一个UClass，并支持override其父类的函数
```

#### 对比矩阵

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 运行时元数据可直接导出本地 API 参考 | Full | Partial | Partial | Partial | None | Full |
| bind 点可手工补 property / meta 文档 | Full | None | None | None | None | Partial |
| 仓库内自带可执行 onboarding scaffold / tutorial | Partial | Full | Full | Partial | Full | Full |
| 示例资产被自动化验证而非仅供手工阅读 | Partial | None | None | None | None | Full |
| onboarding 主入口偏“生成器 / 模板”而非“文档站” | None | Full | Partial | Full | Partial | Partial |

#### 小结与建议

- 当前 Angelscript 在 `D10` 上最独特的新确认，是“文档导出与示例验证都还留在仓库内部，并且示例进入了自动化测试”。这比单纯 README 更可靠，优先级 `P0`。
- 与 Hazelight 对比，当前 AS 的差距不是没有 docs dump，而是 `SCRIPT_PROPERTY_DOCUMENTATION` / `SCRIPT_MANUAL_BIND_META` 这类 authoring surface 没有继续显式保留。若后续要提升 API 参考质量，优先恢复这些入口，优先级 `P1`。
- 与 `UnLua` / `UnrealCSharp` / `puerts` 对比，当前 AS 缺的不是“再写一份文档”，而是一个更面向新用户的 starter artifact：例如 editor 内一键生成脚本模板、starter module 或 docs index。优先级 `P1`。
- `sluaunreal` 证明 demo project 对解释运行时 wiring 很有效，但它不替代 CI。当前 Angelscript 最值得保持的路线仍然是“可执行示例 + 自动化验证”，再叠加更轻量的新手入口，而不是退回纯 demo/README 模式。

---

## 深化分析 (2026-04-08 19:32:42)

### [D1] VM / ThirdParty 的真实物化边界

#### 各插件实现概览

```
VM Materialization Boundary
HZ : plugin-root ThirdParty/include+source -> AngelscriptCode
AS : module-root ThirdParty/angelscript -> AngelscriptRuntime
UC : UnrealCSharpCore -> Mono module -> editor Source/ThirdParty / packaged Binaries/<Platform>/Mono
UL : UnLua runtime -> external Lua module -> per-platform build/copy/runtime dependency
PU : JsEnv -> select V8/QuickJS/Node backend -> stage DLL/dylib + copy Content/Typing
SL : slua_unreal -> External headers + Library/*.a|*.lib -> link by platform
```

缩写说明：HZ = Hazelight，AS = 当前 Angelscript，UC = UnrealCSharp，UL = UnLua，PU = puerts，SL = sluaunreal。

这一轮新增结论是：`D1` 里最容易被“模块数量”掩盖的，其实不是谁拆了几个 `Runtime/Editor`，而是脚本 VM 最终以什么形态进入 UE build graph。当前 Angelscript 与 Hazelight 都还是“源码内嵌型 VM”；UnLua 是“外部 Lua module + 按平台构建/拷贝”；UnrealCSharp 是“UE module 依赖 Mono module，但 packaged runtime 路径在 C++ 里显式切换”；puerts 则进一步把 backend 选择、binary staging、`Content/Typing` 拷贝都推到了 `JsEnv.Build.cs`。

#### 详细对比

##### 子维度 1：`ThirdParty` 是模块自持，还是插件 / runtime 包自持

- Hazelight 的 `AngelscriptCode.Build.cs` 仍通过 `../Plugins/Angelscript/ThirdParty/include|source` 引入 AngelScript，说明 VM 归属仍然挂在插件根目录，而不是 `AngelscriptCode` 模块自己持有。
- 当前 Angelscript 把 third-party 路径下沉到 `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript`，`AngelscriptRuntime.Build.cs:20-22` 直接以 `ModuleDirectory` 拼路径。这是一个很小但很实用的边界调整：后续 `Runtime` 模块可以在不理解插件根目录布局的情况下自洽编译。
- UnrealCSharp 没把 .NET runtime 直接揉进 `UnrealCSharp` 主模块，而是让 `UnrealCSharpCore` 公共依赖 `Mono`（`UnrealCSharpCore.build.cs:38-56`），然后再由 `FMonoFunctionLibrary` 决定 editor / packaged 两套 Mono 目录。也就是说，它的 VM 边界主要不在 Build.cs include path，而在运行时目录选择。
- UnLua 的 `UnLua.Build.cs:48-57` 直接依赖外部 `Lua` module，而 `ThirdParty/Lua/Lua.Build.cs:29-53` 再决定如何在各平台编译或拷贝 Lua 库；这是标准的“runtime module 不直接碰底层库细节”的 UE 外部模块分层。
- puerts 的 `JsEnv.Build.cs:154-170` 不是单一 VM，而是 build-time backend selector：`Nodejs / QuickJS / V8` 三路只会走一条。它的边界最强，但复杂度也最高，因为 backend 决策、staging 和脚本资产拷贝都发生在一个 Build.cs 内。
- sluaunreal 的 `slua_unreal.Build.cs:31-76` 仍然是最传统的“`External/` 头文件 + `Library/` 平台库列表”模型；运行时没有像 UnrealCSharp 那样的二次路径切换，也没有像 puerts 那样的 backend 选择层。

[1] 当前 Angelscript 与 Hazelight 的差异，不在“有没有内嵌源码”，而在 third-party 归属点：

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs
// 位置: 20-22
// 说明: 当前 AS 把 AngelScript third-party 下沉到 Runtime 模块目录
// ============================================================================
var AngelscriptThirdPartyPath = Path.Combine(ModuleDirectory, "ThirdParty", "angelscript");
PublicIncludePaths.Add(Path.Combine(AngelscriptThirdPartyPath, "source"));
PublicIncludePaths.Add(AngelscriptThirdPartyPath); // ★ 模块自己持有 VM 源码入口

// ============================================================================
// 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/AngelscriptCode.Build.cs
// 位置: 60-64
// 说明: Hazelight 仍然通过插件根目录引用 ThirdParty
// ============================================================================
var PluginPath = "../Plugins/Angelscript";
PublicIncludePaths.Add(PluginPath + "/ThirdParty/include");
PublicIncludePaths.Add(PluginPath + "/ThirdParty/source"); // ★ ThirdParty 归属仍在插件根，不在 Code 模块
```

##### 子维度 2：packaged runtime 是谁恢复，恢复到哪里

- UnrealCSharp 的 packaged 边界最显式。`FMonoFunctionLibrary::GetMonoDirectory()` 在 editor 下返回 `Plugin/Source/ThirdParty/Mono`，而在非 editor 下切到 `Project/Binaries/<Platform>/Mono`。这说明 Mono 不是“只要编过就行”，而是 packaged runtime 的一等资产。
- UnLua 的 `Lua.Build.cs` 更像“构建期保证 runtime 存在”。`BuildForWin64()` 会在缺库时先 `CMake()` 再 copy 到目标目录，之后 `SetupForRuntimeDependency()` 把 DLL/PDB 加入 runtime dependency；macOS 分支则直接 `RuntimeDependencies.Add(libFile)`。也就是说，UnLua 把“生成库”和“把库带进包里”合并在 external module。
- puerts 在 `JsEnv.Build.cs:360-368` 把 staged binary contract 写得最直白：`RuntimeDependencies.Add(DestDllPath, DllPath, StagedFileType.NonUFS)`。而且同一文件继续按 Win/macOS/iOS/Android 分别列出 V8/Node/QuickJS 的库名，这是一种强约束、强维护成本的部署模型。
- sluaunreal 主要依赖静态或平台库直接链接，没有像 UnrealCSharp 那样的 packaged 路径切换代码，也没有 puerts 那种大量 `RuntimeDependencies.Add`。它的部署边界因此更轻，但“库去哪了”更多交给平台链接期。

[2] UnrealCSharp 与 puerts 都有显式的 packaged runtime contract，只是 owner 不同：

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoFunctionLibrary.cpp
// 函数: FMonoFunctionLibrary::GetMonoDirectory
// 位置: 4-29
// 说明: UnrealCSharp 在代码里显式区分 editor 与 packaged 的 Mono 目录
// ============================================================================
#if WITH_EDITOR
	return FString::Printf(TEXT(
		"%s/Source/ThirdParty/Mono"),
	                       *FUnrealCSharpFunctionLibrary::GetPluginDirectory()
	);
#else
	return FString::Printf(TEXT(
		"%s/Binaries/%s/Mono"),
	                       *FPaths::ProjectDir(),
	                       TEXT("Win64"));
	// ★ packaged runtime 不再回插件目录，而是回项目 Binaries
#endif

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs
// 函数: AddRuntimeDependencies
// 位置: 360-368
// 说明: puerts 把 VM binary staging 直接固化在 Build.cs
// ============================================================================
void AddRuntimeDependencies(string[] DllNames, string LibraryPath, bool Delay)
{
	foreach (var DllName in DllNames)
	{
		if(Delay) PublicDelayLoadDLLs.Add(DllName);
		var DllPath = Path.Combine(LibraryPath, DllName);
		var DestDllPath = Path.Combine("$(BinaryOutputDir)", DllName);
		RuntimeDependencies.Add(DestDllPath, DllPath, StagedFileType.NonUFS); // ★ binary staging 是 build contract
	}
}
```

##### 子维度 3：build 过程是否顺手改写项目脚本资产

- 当前 Angelscript 与 Hazelight 在这一维都相对克制；就本轮取证到的 `Build.cs` 而言，主要仍停留在 include path / module dependency，不会像 puerts 那样每次 build 都 copy `Content/Typing`。
- puerts 的 `JsEnv.Build.cs:174-181` 会把插件里的 `Content` 与 `Typing` 复制到项目目录；这让首次使用更顺手，但也意味着 VM build graph 已经越过“编译 C++ 插件”本身，开始主动改写项目脚本侧资产。
- UnrealCSharp 也有类似的“越界但可控”动作，不过不是在 `Build.cs`，而是在 `FEditorListener` / generator 链里处理 C# proxy / binding / compile；它把副作用放在 editor generator，而不是每次 link 时执行。
- UnLua 与 sluaunreal 没有看到这种 build 阶段批量拷贝脚本目录的做法。UnLua 更像“external Lua module 保证 runtime 库存在，模板 / IntelliSense 另走 editor 工具”；slua 则更偏“外部工具配好再导出”。

[3] puerts 与 UnLua / slua 的差异，不是“谁支持更多平台”，而是“谁允许 build 修改项目脚本资产”：

```csharp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs
// 位置: 174-181
// 说明: puerts build 会同步脚本资产与声明文件到项目目录
// ============================================================================
string coreJSPath = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", "Content"));
string destDirName = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", "..", "..", "Content"));
DirectoryCopy(coreJSPath, destDirName, true);

string srcDtsDirName  = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", "Typing"));
string dstDtsDirName = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", "..", "..", "Typing"));
DirectoryCopy(srcDtsDirName, dstDtsDirName, true); // ★ build 同步 IDE 侧声明资产

// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/ThirdParty/Lua/Lua.Build.cs
// 位置: 58-80
// 说明: UnLua external module 主要关注 Lua runtime 库本身，不改写项目脚本目录
// ============================================================================
private void BuildForWin64()
{
	var dllPath = GetLibraryPath();
	...
	if (!dstFiles.All(File.Exists))
	{
		var buildDir = CMake();
		CopyDirectory(Path.Combine(buildDir, m_Config), dirPath);
	}
	SetupForRuntimeDependency(dllPath, "Win64"); // ★ 保证库能进包，但不动 Content/Script
}

// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/slua_unreal.Build.cs
// 位置: 42-75
// 说明: slua 主要按平台直连外部库
// ============================================================================
if (Target.Platform == UnrealTargetPlatform.IOS)
{
	PublicAdditionalLibraries.Add(Path.Combine(externalLib, "iOS/liblua.a"));
}
else if (Target.Platform == UnrealTargetPlatform.Win64)
{
	PublicAdditionalLibraries.Add(Path.Combine(externalLib, "Win64/lua.lib"));
}
// ★ 主要是平台库表，不承担项目脚本资产同步
```

#### 对比矩阵（本轮新增观察点）

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| `ThirdParty` 明确收进具体 runtime module 目录 | Partial | None | None | None | None | Full |
| VM 通过独立 UE module 暴露给上层 runtime | None | Full | Full | Partial | None | None |
| packaged runtime 路径由代码显式切换 | None | Full | None | None | None | None |
| Build.cs 显式枚举并 stage 平台二进制 | None | Partial | Full | Full | Partial | None |
| build 阶段直接同步脚本 / 声明资产到项目目录 | None | None | None | Full | None | None |

#### 小结与建议

- 当前 Angelscript 在 `D1` 上值得保留的新增确认，是“把 AngelScript third-party 下沉到 `AngelscriptRuntime` 模块目录”，这比 Hazelight 的插件根路径更利于模块自洽，优先级 `P0`。
- 不值得直接照搬 puerts 的，是 `Build.cs` 级别的 `Content/Typing` copy side-effect。它适合 TS 生态，但会把 runtime module 变成项目资产同步器；当前 AS 若未来扩展 IDE 产物，优先放在生成器或 commandlet，而不是 runtime Build.cs，优先级 `P1`。
- 如果未来 Angelscript 要引入额外 VM binary 或 packaged runtime，最值得吸收的是 UnrealCSharp / UnLua 的“外部 runtime module + packaged path/staging contract”思路，而不是继续把所有部署逻辑塞进 `AngelscriptRuntime.Build.cs`，优先级 `P1`。

### [D5] 调试 bootstrap 的启动拓扑与 editor 反向耦合

#### 各插件实现概览

```
Debug Bootstrap Topology
HZ/AS : engine init -> FTcpListener -> StartDebugging handshake -> editor asset save can depend on active client
UC    : Mono domain init -> debugger-agent(dt_socket) -> IDE attaches later
UL    : runtime debug value/callstack builders + editor debug toolbar slot
PU    : PuertsModule settings -> JsEnv/JsEnvGroup -> V8 Inspector websocket -> optional WaitDebugger
SL    : profiler module startup -> editor tab + FTcpListener(8081)
```

这一轮新增结论不是“谁有断点”，而是“调试 bootstrap 在谁手里，以及它会不会反向影响日常 editor authoring”。当前 Angelscript / Hazelight 的调试器不仅是外部 attach 工具，还已经渗进了编辑器工作流；puerts 的 `WaitDebugger` 只影响 runtime 启动；UnrealCSharp 只是给 Mono 开 `dt_socket`；UnLua 与 slua 当前仓内更像“暴露观测对象”或“开启 profiler 通道”，并不会把 editor 作者动作直接绑到调试客户端存活上。

#### 详细对比

##### 子维度 1：debug transport 在什么时候启动

- 当前 Angelscript 在 `FAngelscriptEngine::Initialize()` 早期直接创建 `FAngelscriptDebugServer(this, RuntimeConfig.DebugServerPort)`，随后 `FAngelscriptDebugServer` 构造函数立即 `new FTcpListener(...)` 监听端口。这意味着 transport owner 明确在插件 runtime，而不是外部 IDE。
- Hazelight 的 `FAngelscriptManager` 也是同一路线，只是 owner 还是 manager，而不是 engine-context 化后的 `FAngelscriptEngine`。
- UnrealCSharp 没有自己造一条协议，而是在 `FMonoDomain::Initialize()` 里通过 `mono_jit_parse_options("--debugger-agent=transport=dt_socket,...")` 打开 Mono 原生调试代理；transport owner 是 Mono，不是插件。
- puerts 的启动链分成两段：`PuertsModule.cpp:224-238` 决定是否以 debug port 构造 `FJsEnv` 并 `WaitDebugger()`；真正的 websocket inspector 则在 `JsEnvImpl.cpp:619` 调 `CreateV8Inspector()`，再由 `V8InspectorImpl.cpp:319-355` 启动 server。
- UnLua 当前仓内能定位到的是 runtime 变量/调用栈 API：`UnLuaDebugBase.cpp:620-669` 用 `lua_getstack / lua_getlocal / lua_getupvalue` 暴露局部变量与 upvalue；transport 本身不在本轮定位到的 in-tree 代码里。
- slua 当前仓内最明确的 transport 是 profiler server：`slua_remote_profile.cpp:52-60` 用 `FTcpListener` 起监听，`slua_profile.cpp:48-77` 则注册 editor tab。也就是说，源码里可见的“远程联机能力”首先服务 profiler。

[1] 当前 Angelscript / Hazelight 的 debug server 是 runtime 初始化的第一等产物，并且会反馈到编辑器：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 1449-1455
// 说明: 当前 AS 在 runtime 初始化时直接起 debug server
// ============================================================================
/*
	Start the debug server that external tools can connect to.
*/
#if WITH_AS_DEBUGSERVER
if ((!bUsePrecompiledData || bScriptDevelopmentMode) && FApp::HasProjectName())
{
	DebugServer = new FAngelscriptDebugServer(this, RuntimeConfig.DebugServerPort); // ★ transport owner 在 runtime
}
#endif

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 位置: 402-407, 897-907
// 说明: server 构造后立即监听；真正进入 debugging 需要显式握手
// ============================================================================
FAngelscriptDebugServer::FAngelscriptDebugServer(FAngelscriptEngine* InOwnerEngine, int Port)
{
	OwnerEngine = InOwnerEngine;
	Listener = new FTcpListener(FIPv4Endpoint(FIPv4Address::Any, Port));
	Listener->OnConnectionAccepted().BindRaw(this, &FAngelscriptDebugServer::HandleConnectionAccepted); // ★ 监听 socket
}

else if (MessageType == EDebugMessageType::StartDebugging)
{
	FStartDebuggingMessage Msg;
	*Datagram << Msg;
	bIsDebugging = true; // ★ 进入调试态必须收到客户端握手
	AngelscriptDebugServer::DebugAdapterVersion = Msg.DebugAdapterVersion;
}

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 位置: 121-128
// 说明: 调试客户端缺席会反向阻塞 literal asset 保存
// ============================================================================
if (UCurveFloat* Curve = Cast<UCurveFloat>(Object))
{
	if (!FAngelscriptEngine::Get().HasAnyDebugServerClients())
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("Visual Studio Code extension must be running to save a script literal curve")));
		return; // ★ editor authoring 被 active debug client 反向约束
	}
}
```

##### 子维度 2：attach / wait 语义是不是 runtime 启动的一部分

- UnrealCSharp 明确选择“不等待”。`FMonoDomain.cpp:98-110` 里的 `suspend=n` 说明 domain 启起来以后 IDE 可以晚 attach；这对 editor 工作流最温和，但插件自己也就不掌握“什么时候开始真正 debug”。
- puerts 反而把 `WaitDebugger` 作为显式 runtime 选项。单 `JsEnv` 模式下，`Settings.WaitDebugger` 会直接调用 `JsEnv->WaitDebugger(Settings.WaitDebuggerTimeout)`；group mode 则明确打印“不支持”。这是一种只约束 runtime start、不约束 editor authoring 的折中。
- 当前 Angelscript / Hazelight 不走 `wait-for-debugger`，而是 server 先常驻，之后靠 `StartDebugging` 包切入调试态。这让运行时更灵活，但也导致 editor 中某些“脚本文字资产保存”动作不得不关心 client 是否在线。

[2] UnrealCSharp 与 puerts 都支持“晚 attach”，只是实现层完全不同：

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp
// 位置: 96-116
// 说明: UnrealCSharp 把 attach 语义委托给 Mono debugger-agent
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
	mono_jit_parse_options(sizeof(Options) / sizeof(char*), Options); // ★ attach 语义交给 Mono
}
mono_debug_init(MONO_DEBUG_FORMAT_MONO);

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp
// 位置: 224-238
// 说明: puerts 把 wait-for-debugger 作为 runtime 启动选项
// ============================================================================
if (Settings.DebugEnable)
{
	JsEnv = MakeShared<PUERTS_NAMESPACE::FJsEnv>(
		std::make_shared<PUERTS_NAMESPACE::DefaultJSModuleLoader>(Settings.RootPath),
		std::make_shared<PUERTS_NAMESPACE::FDefaultLogger>(),
		DebuggerPortFromCommandLine < 0 ? Settings.DebugPort : DebuggerPortFromCommandLine);
}

if (Settings.WaitDebugger)
{
	JsEnv->WaitDebugger(Settings.WaitDebuggerTimeout); // ★ 只阻塞 JS runtime start，不反向要求 editor client 常驻
}

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp
// 位置: 319-355
// 说明: 真正的 attach endpoint 是 V8 Inspector websocket
// ============================================================================
Server.set_http_handler(std::bind(&V8InspectorClientImpl::OnHTTP, this, std::placeholders::_1));
Server.set_open_handler(std::bind(&V8InspectorClientImpl::OnOpen, this, std::placeholders::_1));
Server.set_message_handler(
	std::bind(&V8InspectorClientImpl::OnReceiveMessage, this, std::placeholders::_1, std::placeholders::_2));
Server.listen(Port);
Server.start_accept();
UE_LOG(LogV8Inspector, Log, TEXT("Startup Inspector Successfully!")); // ★ attach 点是 websocket inspector
```

##### 子维度 3：仓内可见的“调试能力”到底是 transport，还是观测对象

- UnLua 的强项在 runtime 观测对象，而不是本轮源码里可见的 transport。`FLuaDebugValue` 能递归展开 table、userdata、`UStruct`、`TArray`、`TMap`、`TSet`；`GetLuaCallStack()` 则直接基于 `lua_getstack` 拼调用栈。这说明它的 repo 更强调“把 Lua runtime 状态做成 UE 可消费对象”。
- slua 当前仓内最清晰的“远程面”是 profiler：`slua_profile.cpp` 注册 editor tab，`slua_remote_profile.cpp` 起 `FTcpListener`。也就是说，它在当前源码里更像“性能/观测工具插件”，而不是像当前 Angelscript 那样把 source-level debugger 协议收回插件 runtime。

[3] UnLua 与 slua 的仓内可见调试面，更接近观测对象 / profiler：

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDebugBase.h
// 位置: 29-63
// 说明: UnLua 先把调试值抽象成可递归展开的数据结构
// ============================================================================
struct UNLUA_API FLuaDebugValue
{
	FString ReadableValue;
	FString Type;
	int32 Depth;
	bool bAlreadyBuilt;
	TArray<FLuaDebugValue> Keys;
	TArray<FLuaDebugValue> Values; // ★ 调试值首先是 runtime object model
	...
};

// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaDebugBase.cpp
// 位置: 620-669, 672-687
// 说明: 变量和调用栈直接从 Lua runtime 拉取
// ============================================================================
lua_Debug ar;
if (!lua_getstack(L, StackLevel, &ar))
{
	return false;
}
...
const char *VarName = lua_getlocal(L, &ar, i);
Variable.Value.Build(L, -1, Level); // ★ 逐个构建 local / upvalue 调试值

while (lua_getstack(L, Depth++, &ar))
{
	lua_getinfo(L, "nSl", &ar);
	CallStack += FString::Printf(TEXT("Source : %s, name : %s, Line : %d \n"), UTF8_TO_TCHAR(ar.source), UTF8_TO_TCHAR(ar.name), ar.currentline);
}

// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_profile/Private/slua_profile.cpp
// 位置: 48-77
// 说明: slua 当前源码里最清晰的远程能力入口是 profiler tab
// ============================================================================
void Fslua_profileModule::StartupModule()
{
#if WITH_EDITOR
	Flua_profileCommands::Register();
	...
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(slua_profileTabName,
		FOnSpawnTab::CreateRaw(this, &Fslua_profileModule::OnSpawnPluginTab))
		.SetDisplayName(LOCTEXT("Flua_wrapperTabTitle", "slua Profiler"));
	TickDelegate = FTickerDelegate::CreateRaw(this, &Fslua_profileModule::Tick); // ★ editor profiler 面板
#endif
}
```

#### 对比矩阵（本轮新增观察点）

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 插件 runtime 自带监听型 transport | Full | None | None | Full | Full | Full |
| 明确复用 VM 原生调试协议 | None | Full | None | Full | None | None |
| `wait-for-debugger` 是显式 runtime 选项 | None | None | None | Full | None | None |
| editor 作者动作依赖活跃调试客户端 | Full | None | None | None | None | Full |
| runtime 内建结构化变量 / 调用栈对象模型 | Partial | Partial | Full | Partial | Partial | Full |
| 当前仓内最可见的远程面更偏 profiler 而非 source debugger | None | None | None | None | Full | None |

#### 小结与建议

- 当前 Angelscript 在 `D5` 上新增确认的优势，是“transport、握手、语言语义、editor 资产桥都还在插件自己手里”。这条线不要丢，优先级 `P0`。
- 最值得立即处理的不是协议本身，而是 editor 反向耦合：`OnLiteralAssetSaved()` 现在要求 debug client 在线，这和 `UnrealCSharp` / `puerts` 的 attach 语义相比过重。建议把“保存 literal asset”从“必须有 active debugger client”解耦，优先级 `P1`。
- 如果要补 runtime start 体验，最可吸收的是 puerts 的 `WaitDebugger` 思路，而不是退回到把 transport 完全交给 VM。换句话说，可以借鉴“等待 attach”的交互，不要放弃当前 AS 的语言级 debug contract，优先级 `P1`。

### [D6] 生成产物 freshness contract：谁清陈旧文件，谁避免无效重写

#### 各插件实现概览

```
Artifact Freshness Contract
HZ : handwritten binds / docs dump visible; no in-tree stale-shard pipeline found in this round
AS : UHT export -> shard list -> delete stale AS_FunctionTable_*.cpp -> json/csv summary
UC : generator begin -> optional recursive delete proxy/binding dirs -> watch changes -> recompile
UL : commandlet/editor generator -> save only on content diff; template file only created if target missing
PU : delete old ue.d.ts/ue_bp.d.ts -> copy Typing/JS -> rewrite version-marked decl blocks
SL : external tool + config.json -> freshness mainly depends on manual export
```

这一轮新增结论是：`D6` 里真正拉开差距的，不只是“有没有生成器”，而是“陈旧产物由谁清、写盘噪音由谁控、生成结果有没有可审计 summary”。当前 Angelscript 在“自动清陈旧 shard + 输出 summary/CSV”上最强；UnLua 在“只在内容变化时写盘”上最克制；puerts 在“用版本标记做局部重写”上最灵活；UnrealCSharp 则选择最强势的 freshness 手段：允许生成前整目录清空。

#### 详细对比

##### 子维度 1：清陈旧产物是增量清理，还是全量清空

- 当前 Angelscript 的 `AngelscriptFunctionTableCodeGenerator.cs:51-75,432-445` 是最明确的增量清理：先记录本轮 `generatedPaths`，然后只删除 output 目录中不在集合里的 `AS_FunctionTable_*.cpp`。这意味着它对 stale output 有显式定义，而不是“反正重写一遍”。
- UnrealCSharp 的 freshness 更激进。`FEditorListener::OnBeginGenerator()` 在设置启用时会对 `UEProxyDirectory / GameProxyDirectory / BindingDirectory` 直接 `DeleteDirectoryRecursively()`。这种策略强保证干净，但代价是生成 churn 很大。
- puerts 介于两者之间：它会先删 `Typing/ue/ue.d.ts` 和 `Typing/ue/ue_bp.d.ts`，但蓝图类型声明内部不是盲写，而是依靠 `TYPE_DECL_START / TYPE_DECL_END` 版本标记重建可替换片段。
- UnLua 更保守：`SaveFile()` 先读旧内容，只有 `FileContent != GeneratedFileContent` 才写盘；模板文件如果已存在则直接拒绝覆盖。它不追求强制干净，而是优先减少无意义改写。
- slua 当前仓内可见的新鲜度保障主要靠外部工具流程。`Tools/README.md:37-46` 明说“已有 generated file，但如果不够需要改 `config*.json` 再导出”；`Tools/config.json:60-74` 则暴露巨大的 `include_path / preprocess` 配置。也就是说 freshness owner 在工具操作者，不在 UE plugin runtime/editor。

[1] 当前 Angelscript 把“删陈旧 shard”和“生成 summary”同时做掉：

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs
// 位置: 51-78, 166-215, 432-445
// 说明: 当前 AS 的 freshness contract 是“本轮生成集合 + stale 清理 + summary 统计”
// ============================================================================
public static int Generate(IUhtExportFactory factory)
{
	HashSet<string> generatedPaths = new(StringComparer.OrdinalIgnoreCase);
	...
	DeleteStaleOutputs(factory, generatedPaths); // ★ 先定义“这轮应该留下什么”
	WriteGenerationSummary(factory, moduleSummaries, csvEntries, generatedFileCount); // ★ 再把结果写成 json/csv
	WriteCoverageDiagnostics(moduleSummaries);
	return generatedFileCount;
}

private static void WriteGenerationSummary(...)
{
	string summaryPath = factory.MakePath("AS_FunctionTable_Summary", ".json");
	...
	File.WriteAllText(summaryPath, summaryJson, Encoding.UTF8);
	WriteModuleSummaryCsv(factory, moduleSummaries);
	WriteEntryCsv(factory, csvEntries); // ★ 产物不是只有 cpp shard，还有可审计 summary
}

private static void DeleteStaleOutputs(IUhtExportFactory factory, HashSet<string> generatedPaths)
{
	foreach (string existingFile in Directory.EnumerateFiles(outputDirectory, "AS_FunctionTable_*.cpp"))
	{
		if (!generatedPaths.Contains(existingFile))
		{
			File.Delete(existingFile); // ★ 只删“这一轮不再需要”的 shard
		}
	}
}
```

##### 子维度 2：写盘策略是 destructive reset，还是 diff-save

- UnrealCSharp 在这个维度最偏 destructive。`OnBeginGenerator()` 允许整个 proxy / binding 目录递归删除；优点是不会残留旧文件，缺点是每轮 generator 都会产生大范围文件 churn。
- UnLua 相反，它的 commandlet 与 editor generator 都走 `LoadFileToString -> compare -> SaveStringToFile`。这不仅减少写盘，也减少 source control 噪音。对于 IDE 产物，这种策略比“删目录再重建”更友好。
- puerts 的版本标记策略是一种“结构化 diff-save”。它既不完全信任旧文件，也不全盘抹掉所有 typing，而是借 `TYPE_DECL_START` / `TYPE_DECL_END` 与 `FileVersionString` 在文件内找可替换区域。

[2] UnrealCSharp 与 UnLua 分别站在 destructive reset / diff-save 两端：

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp
// 位置: 176-219
// 说明: UnrealCSharp 允许在生成前直接清空 proxy/binding 目录
// ============================================================================
void FEditorListener::OnBeginGenerator()
{
	...
	if (UnrealCSharpEditorSetting->EnableDeleteProxyDirectory())
	{
		if (PlatformFile.DirectoryExists(*UEProxyDirectory))
		{
			PlatformFile.DeleteDirectoryRecursively(*UEProxyDirectory); // ★ 强制干净，但 churn 最大
		}
		...
	}

	if (UnrealCSharpEditorSetting->EnableDeleteBindingDirectory())
	{
		if (PlatformFile.DirectoryExists(*UEBindingDirectory))
		{
			PlatformFile.DeleteDirectoryRecursively(*UEBindingDirectory);
		}
		...
	}

	bIsGenerating = true;
	FileChanges.Reset();
}

// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp
// 位置: 117-133
// 说明: UnLua commandlet 先比较内容，再决定是否写盘
// ============================================================================
FString FileContent;
FFileHelper::LoadFileToString(FileContent, *FilePath);
if (FileContent != GeneratedFileContent)
{
	bool bResult = FFileHelper::SaveStringToFile(GeneratedFileContent, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	check(bResult); // ★ 没变就不写
}

// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp
// 位置: 287-310
// 说明: 模板文件已存在时，直接拒绝覆盖
// ============================================================================
if (FPaths::FileExists(FileName))
{
	UE_LOG(LogUnLua, Warning, TEXT("%s"), *FText::Format(LOCTEXT("FileAlreadyExists", "Lua file ({0}) is already existed!"), FText::FromString(TemplateName)).ToString());
	return; // ★ template authoring 默认是 append-only，不做 destructive overwrite
}
...
FFileHelper::SaveStringToFile(Content, *FileName, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
```

##### 子维度 3：触发入口和局部重写 contract 是否显式

- puerts 把生成入口和局部重写 contract 写得很明确。`GenDTSCommands.cpp:13-16` 注册 editor 按钮；`DeclarationGenerator.cpp:420-446` 先删旧 `ue.d.ts/ue_bp.d.ts` 并同步 `Typing/JavaScript`；`571-585` 再依赖 `TYPE_DECL_START / TYPE_DECL_END` 与 `FileVersionString` 扫描旧文件块。这意味着它虽然会删入口文件，但蓝图声明内部仍是“可局部重写”的。
- 当前 Angelscript 的生成入口主要在 UHT/export 阶段，不是 editor toolbar；优点是 build 一定会跑，缺点是 authoring 手感不像 UnLua / puerts 那样直观。
- slua 的触发入口主要在外部工具链。`Tools/README.md:37-46` 明说“修改 `config*.json` 再导出”；因此它的局部更新能力更多取决于工具本身，而不是 UE 内部有没有显式的 stale/diff contract。

[3] puerts 的 freshness 不是简单重写，而是“入口文件重建 + 版本块替换”：

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/GenDTSCommands.cpp
// 位置: 13-16
// 说明: 生成动作在 editor 里有显式按钮
// ============================================================================
void FGenDTSCommands::RegisterCommands()
{
	UI_COMMAND(
		PluginAction, "puerts", "Generate *.d.ts and copy some js builtin libs", EUserInterfaceActionType::Button, FInputChord());
}

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp
// 位置: 420-446, 571-585
// 说明: puerts 先清入口文件，再用版本标记做局部重写
// ============================================================================
PlatformFile.DeleteFile(*(PuertsBaseDir / TEXT("Typing/ue/ue.d.ts")));
PlatformFile.DeleteFile(*(PuertsBaseDir / TEXT("Typing/ue/ue_bp.d.ts")));
PlatformFile.CopyDirectoryTree(*ProjectTypingDir, *(PuertsBaseDir / TEXT("Typing")), false);
...
Output << TYPE_DECL_START << (KV.Value.IsAssociation ? TYPE_ASSOCIATION : KV.Value.FileVersionString) << "\n";
Output << NameToDecl.Value;
Output << TYPE_DECL_END << "\n"; // ★ 文件内每个 declaration block 都自带版本边界

static const FString Start = TEXT(TYPE_DECL_START);
static const FString End = TEXT(TYPE_DECL_END);
int Pos = FileContent.Find(*Start, ESearchCase::CaseSensitive);
while (Pos >= 0)
{
	int VersionInfoEnd = FileContent.Find(TEXT("\n"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Pos + Start.Len());
	int DeclEnd = FileContent.Find(*End, ESearchCase::CaseSensitive, ESearchDir::FromStart, VersionInfoEnd + 1);
	FString FileVersionString = FileContent.Mid(Pos + Start.Len(), VersionInfoEnd - Pos - Start.Len()); // ★ 旧块可被结构化识别
	...
}
```

#### 对比矩阵（本轮新增观察点）

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 自动删除陈旧生成文件 | N/A | Partial | Partial | Full | None | Full |
| 显式“内容未变则不写盘” | N/A | None | Full | None | None | None |
| 支持生成前全目录 reset | N/A | Full | None | None | None | None |
| 生成结果伴随 machine-readable summary / csv | None | None | None | None | None | Full |
| editor 内存在显式生成按钮 / 命令 | None | Partial | Full | Full | None | None |
| 主要依赖外部工具配置保持新鲜度 | None | None | None | None | Full | None |

#### 小结与建议

- 当前 Angelscript 在 `D6` 上最值得保持的新确认，是“`generatedPaths -> DeleteStaleOutputs() -> Summary JSON/CSV`”这一整条 freshness contract。它已经比单纯重写文件更接近可审计生成系统，优先级 `P0`。
- 最值得吸收的是 UnLua 的 diff-save 和 puerts 的版本块替换。前者能显著降低 source control 噪音，后者适合把单一大文件拆成可局部重写的稳定片段；两者都比 UnrealCSharp 式全目录 delete 更温和，优先级 `P1`。
- 不建议当前 AS 直接退回 UnrealCSharp 的 destructive reset 路线。只有当后续生成体系出现大量 rename / split / merge、且 `generatedPaths` 已不足以准确表达“陈旧性”时，才值得考虑引入目录级 reset，优先级 `P2`。

---

## 深化分析 (2026-04-08 19:49:26)

### [D4] 文件事件语义与失败后旧状态的存活 contract

#### 各插件实现概览

```
[D4-Deep-2] Event Semantics & Failure Survival
HZ : add/remove queue + delete delay + retry/full-reload memory
AS : add/remove queue + rename-window tests + keep-old-code tests + retry/full-reload memory
UC : .cs watcher -> FileChanges -> generator/compile -> new assembly bytes
UL : any file event -> HotReload() -> scan loaded_module_times -> keep old module on failure
PU : watch loaded .js only -> modified-only callback -> ReloadSource / forceReload loaded modules
SL : loadfile delegate + preview simulate -> project owns rename/delete/retry semantics
```

上一轮 `D4` 已经回答了“谁拥有 reload authority”。这一轮只补两个更贴近作者体验的问题：

- **文件系统事件有没有被建模成不同语义**。也就是新增、删除、重命名、仅修改，插件到底区不区分。
- **失败后旧状态怎么活下来**。是显式排队重试、保留旧模块继续运行，还是简单报错然后由作者自己再试一次。

这一层拉开的差距，比“支不支持热重载”更实在。当前 Angelscript / Hazelight 真正强的是 **事件语义和失败记忆都进入了插件状态机**；UnLua 的复杂度在 Lua VM 内的 module graph merge；puerts 的 contract 更窄，只针对已加载 `.js`；UnrealCSharp 和 slua 则更接近“编译/加载入口”，不是“统一 reload 事务”。

#### 详细对比

##### 子维度 1：文件事件有没有被建模成不同语义

- 当前 Angelscript 的 `FAngelscriptEngine::CheckForHotReload()` 会把 `FileChangesDetectedForReload` 与 `FileDeletionsDetectedForReload` 分开管理，并且对删除事件额外保留 `0.2s` 窗口，等待 rename 的“新文件”立刻出现。`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2729-2770` 直接写了这个 delete-delay 逻辑；`Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp:17-37,79-102,194-222` 又把“新增/删除分队列”和“rename 同时保留 old/new 路径”锁成自动化测试。这里不是简单 watcher，而是显式事件模型。
- Hazelight 的 `J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/AngelscriptManager.cpp:1481-1520` 仍保留同一套 delete-delay + `QueuedFullReloadFiles` 机制。差异不在算法本身，而在当前仓库多了一层 editor watcher 自动化测试兜底。
- UnrealCSharp 也有目录监听，但语义更薄。`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:322-364` 只筛出 `.cs` 文件，且忽略 `Proxy/obj` 目录；之后这些变更被塞进 `FileChanges`，交给 generator/compile 线程处理。也就是说，事件含义是“哪些 C# 文件值得重新分析”，不是“脚本运行态该怎样迁移”。
- UnLua 的 editor watcher 根本不消费 `FFileChangeData::Action`。`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorFunctionLibrary.cpp:112-118` 只要目录有变更且模式为 `Auto`，就直接 `HotReload()`；Lua 侧 `Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:612-624` 再遍历 `loaded_module_times`，按“旧 module 名 -> 当前时间戳”筛候选模块。删除、改名和修改不会先被建模成不同事件，而是统一折叠成“按旧名字重新尝试加载”。
- puerts 的 watcher 更窄，只监听**已经被加载过**的文件，而且只响应 `FCA_Modified && .js`。`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/SourceFileWatcher.cpp:22-74` 先在 `OnSourceLoaded()` 时登记 watched dir / watched file / md5，再在 `OnDirectoryChanged()` 里只对修改过的已加载 `.js` 做回调。新增文件、重命名、未加载文件，都不在这层语义里。
- sluaunreal 当前可见契约停在 `LoadFileDelegate`。`Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaState.h:167-189` 只承诺 `doFile()` 的来源由 delegate 决定；demo 的 `Reference/sluaunreal/Source/democpp/MyGameInstance.cpp:42-58` 也只是把 `Content/Lua` 下的 `.lua/.luac` 读成字节流。事件语义完全留给项目侧资源系统，不在插件统一管理。

[1] 当前 Angelscript 把 add/remove/rename 当成不同事件，而不是一个“目录有变化”的布尔开关：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: FAngelscriptEngine::CheckForHotReload
// 位置: 2729-2770
// 说明: 删除事件不会立刻消费，而是等待 rename 窗口；full reload 队列也单独并入
// ============================================================================
void FAngelscriptEngine::CheckForHotReload(ECompileType CompileType)
{
	...
	FileList.Append(FileChangesDetectedForReload);
	FileChangesDetectedForReload.Empty();

	// ★ 删除稍后再并入，给 rename 一个“旧文件先删、新文件随后加”的观察窗口
	if (FileList.Num() != 0 || FPlatformTime::Seconds() - LastFileChangeDetectedTime > 0.2)
	{
		for (const auto& DeletedFile : FileDeletionsDetectedForReload)
			FileList.AddUnique(DeletedFile);
		FileDeletionsDetectedForReload.Empty();
	}

	if (CompileType != ECompileType::SoftReloadOnly)
	{
		for (const auto& QueuedFile : QueuedFullReloadFiles)
			FileList.AddUnique(QueuedFile); // ★ full reload 延后重试也是另一条独立队列
		QueuedFullReloadFiles.Empty();
	}
	...
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp
// 函数: FAngelscriptDirectoryWatcherScriptQueueTest / FAngelscriptDirectoryWatcherRenameWindowTest
// 位置: 79-102, 194-222
// 说明: 当前仓库把“新增/删除分开排队”和“rename 同时保留 old/new 路径”做成自动化测试
// ============================================================================
TestEqual(TEXT("DirectoryWatcher.Queue.ScriptAddAndRemove should queue one added script"), Engine->FileChangesDetectedForReload.Num(), 1);
TestEqual(TEXT("DirectoryWatcher.Queue.ScriptAddAndRemove should queue one removed script"), Engine->FileDeletionsDetectedForReload.Num(), 1);

const TArray<FFileChangeData> Changes = {
	MakeFileChange(OldAbsolutePath, FFileChangeData::FCA_Removed),
	MakeFileChange(NewAbsolutePath, FFileChangeData::FCA_Added)
};
...
TestTrue(TEXT("DirectoryWatcher.Queue.RenameWindowTracksRemoveAndAdd should retain the old filename in the deletion queue"),
	ContainsFilenamePair(Engine->FileDeletionsDetectedForReload, OldAbsolutePath, TEXT("Rename/OldName.as")));
return TestTrue(TEXT("DirectoryWatcher.Queue.RenameWindowTracksRemoveAndAdd should retain the new filename in the addition queue"),
	ContainsFilenamePair(Engine->FileChangesDetectedForReload, NewAbsolutePath, TEXT("Rename/NewName.as")));
// ★ rename 不是“模糊的一次变更”，而是 remove/add 成对保留
```

[2] UnLua 和 puerts 都有 watcher，但事件语义完全不同：

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorFunctionLibrary.cpp
// 函数: UUnLuaEditorFunctionLibrary::OnLuaFilesModified
// 位置: 112-118
// 说明: editor 层不区分 Added / Removed / Modified，统一触发 HotReload
// ============================================================================
void UUnLuaEditorFunctionLibrary::OnLuaFilesModified(const TArray<FFileChangeData>& FileChanges)
{
	const auto& Settings = *GetDefault<UUnLuaEditorSettings>();
	if (Settings.HotReloadMode != EHotReloadMode::Auto)
		return;
	UUnLuaFunctionLibrary::HotReload(); // ★ FileChanges 只充当“有变化了”的触发器
}
```

```
-- ============================================================================
-- 文件: Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua
-- 函数: M.reload / require
-- 位置: 151-169, 612-624
-- 说明: Lua 层只按旧 module 名和时间戳工作，不先区分 rename / delete / modify
-- ============================================================================
if package.loaded[module_name] ~= nil then
	return package.loaded[module_name], nil
end
...
loaded_modules[module_name] = new_module
package.loaded[module_name] = new_module
loaded_module_times[module_name] = get_last_modified_time(module_name)

for module_name, time in pairs(loaded_module_times) do
	local current_time = get_last_modified_time(module_name)
	if current_time ~= time then
		modified_modules[#modified_modules + 1] = module_name
		loaded_module_times[module_name] = current_time
	end
end
-- ★ 语义是“按旧名字重新探测模块时间戳”，不是“先识别这是一场 rename”
```

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/SourceFileWatcher.cpp
// 函数: FSourceFileWatcher::OnSourceLoaded / OnDirectoryChanged
// 位置: 22-74
// 说明: puerts 只监视已经 load 过的 .js，并且只响应 Modified
// ============================================================================
void FSourceFileWatcher::OnSourceLoaded(const FString& InPath)
{
	...
	if (!WatchedFiles[Dir].Contains(FileName))
	{
		FMD5Hash Hash = FMD5Hash::HashFile(*InPath);
		WatchedFiles[Dir].Add(FileName, Hash); // ★ 先被 load 过，才会进入 watcher 账本
	}
}

void FSourceFileWatcher::OnDirectoryChanged(const TArray<FFileChangeData>& FileChanges)
{
	...
	if (Change.Action == FFileChangeData::FCA_Modified && Change.Filename.EndsWith(TEXT(".js")))
	{
		...
		if (WatchedFiles[Dir].Contains(FileName))
		{
			FMD5Hash Hash = FMD5Hash::HashFile(*NotifyPath);
			if (WatchedFiles[Dir][FileName] != Hash)
			{
				OnWatchedFileChanged(NotifyPath); // ★ 只回调已加载、确实 hash 变化的脚本
				WatchedFiles[Dir][FileName] = Hash;
			}
		}
	}
}
```

##### 子维度 2：失败后旧状态怎么活下来

- 当前 Angelscript 的失败语义是显式事务。`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:4168-4186` 会把 `ErrorNeedFullReload` 的文件塞进 `QueuedFullReloadFiles`，普通 `Error` 也会进入 `PreviouslyFailedReloadFiles`；`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp:418-503` 进一步验证“broken reload 之后旧 `UFunction` 还能继续返回原值”。这说明“保留旧代码继续跑”不是口头约定，而是被测试钉住的 contract。
- Hazelight 仍有相同的失败记忆账本。`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/AngelscriptManager.cpp:972-1066,2848-2866` 可以看到 `PreviouslyFailedReloadFiles` 与 `QueuedFullReloadFiles` 的同构逻辑；只是当前仓库把“旧代码继续可执行”补成了自动化测试。
- UnrealCSharp 当前源码更像“编译窗口”而非“失败重试窗口”。`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:322-364` 把 `.cs` 变化聚合成 `FileChanges`；`Reference/UnrealCSharp/Source/Compiler/Private/FCSharpCompilerRunnable.cpp:22-26,135-154,328-343` 则在 generator begin/end 时清空变更集。也就是说，它的显式状态是“本轮需要重新生成什么”，不是“哪些旧 assembly 继续保活，哪些文件要延迟 full reload”。
- UnLua 失败时也会保旧状态，但方式不同。`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:151-169` 里 `loaded_modules` / `package.loaded` 是旧状态的真实 owner；`553-601` 里如果 `sandbox.load()` 或 `xpcall()` 失败，就直接 `sandbox.exit(); return`，不会去删掉旧 `loaded_modules[module_name]`。因此它更像“失败后旧 module 继续按旧名字存活”，而不是“把这批文件登记成待重试事务”。
- puerts 的失败恢复更轻。`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:1468-1513` 的 `JsHotReload()` 如果找不到模块就 `Warn`，如果 JS callback 抛错就 `Error`；`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:205-245` 的 `forceReload()` 也只会给当前 `moduleCache` 项打 `__forceReload` 标记，不存在与 Angelscript 对等的失败队列。
- sluaunreal 目前甚至没有插件级失败记忆账本。`Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaSimulate.cpp:98-108` 在 `LoadFileDelegate` 缺失时直接报错返回；失败恢复和版本淘汰明显在项目层。

[3] 当前 Angelscript 把“保留旧代码继续运行”做成了显式失败语义和自动化测试：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: hot reload compile result tail
// 位置: 4168-4186
// 说明: 失败文件和需要 full reload 的文件都有独立账本
// ============================================================================
if (Result == ECompileResult::ErrorNeedFullReload)
{
	for (const auto& RepeatFile : AllCompiledFiles)
		QueuedFullReloadFiles.Add(RepeatFile); // ★ 软重载环境里先记账，等可 full reload 再处理

	PreviouslyFailedReloadFiles.Append(AllCompiledFiles);
}
else if (Result == ECompileResult::Error)
{
	PreviouslyFailedReloadFiles.Append(AllCompiledFiles); // ★ 普通编译错误也进入失败记忆
}
else if (Result == ECompileResult::PartiallyHandled)
{
	for (const auto& RepeatFile : AllCompiledFiles)
		QueuedFullReloadFiles.Add(RepeatFile);
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp
// 函数: FAngelscriptHotReloadFailureKeepsOldCodeTest::RunTest
// 位置: 418-503
// 说明: 当前仓库直接验证“坏 reload 不会替换掉旧的可执行 UFunction”
// ============================================================================
const bool bCompiled = CompileModuleWithResult(&Engine, ECompileType::SoftReloadOnly,
	ModuleName, TEXT("HotReloadFailureKeepsOldCode.as"), BrokenScript, ReloadResult);
TestFalse(TEXT("Failure fallback test should fail the broken hot reload compile"), bCompiled);
TestTrue(TEXT("Failure fallback test should report an error reload state"),
	ReloadResult == ECompileResult::Error || ReloadResult == ECompileResult::ErrorNeedFullReload);
...
if (!TestTrue(TEXT("Failure fallback test should still execute the old generated function after reload failure"),
	ExecuteGeneratedIntEventOnGameThread(&Engine, TestObject, GetValueBeforeFailure, ResultAfterFailure)))
{
	return false;
}
TestEqual(TEXT("Failure fallback test should keep the old code active after the broken reload"), ResultAfterFailure, 5);
// ★ “保持旧代码继续跑”已经不是推断，而是测试覆盖的正式 contract
```

```lua
-- ============================================================================
-- 文件: Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua
-- 函数: reload_modules
-- 位置: 553-601
-- 说明: UnLua 失败时不建失败队列，而是直接退出 sandbox，旧 loaded_modules 保持不变
-- ============================================================================
for _, module_name in ipairs(module_names) do
	if loaded_modules[module_name] == nil then
		sandbox.require(module_name)
	else
		local func, env = sandbox.load(module_name)
		if func ~= nil then
			local ok, new_module = xpcall(func, load_error_handler)
			if not ok then
				sandbox.exit()
				return -- ★ 失败即退出 merge，旧模块仍在 loaded_modules / package.loaded 中
			end
			...
		else
			sandbox.exit()
			return
		end
	end
end
```

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp
// 函数: FJsEnvImpl::JsHotReload / ReloadModule
// 位置: 1468-1513
// 说明: puerts 失败时更像“记录日志并结束本轮 callback”，没有失败文件账本
// ============================================================================
if (ModuleLoader->Search(TEXT(""), ModuleName.ToString(), OutPath, OutDebugPath))
{
	...
	(void)(LocalReloadJs->Call(Context, v8::Undefined(Isolate), 3, Args));
	if (TryCatch.HasCaught())
	{
		Logger->Error(FString::Printf(TEXT("reload module exception %s"),
			*FV8Utils::TryCatchToString(Isolate, &TryCatch)));
	}
}
else
{
	Logger->Warn(FString::Printf(TEXT("not find js module [%s]"), *ModuleName.ToString()));
	return; // ★ 没有显式 retry queue
}
```

##### 子维度 3：对当前 Angelscript 的真实启发

- 不要把当前 watcher 简化成 “目录变了 -> 统一 `HotReload()`” 的薄触发器。源码已经证明，`add/remove/rename/full-reload-later` 这些事件语义正是当前 Angelscript 相对 UnLua / puerts 的核心质量差异。
- 真正值得吸收的不是“让 watcher 更薄”，而是**给操作者一个更可见的 reload 策略面**。比如借鉴 UnLua 的 `Manual / Auto / Never` 交互层，让作者可以关闭自动消费队列，但底层仍保留现有的事件和失败账本。
- 对 UnrealCSharp / slua 这类“编译入口 / loader 入口”式方案，最该学的是扩展点设计，不是把当前 AS 的事务状态机打散。当前 AS 已经证明，只有显式失败记忆，才能把“坏脚本先别替换旧逻辑”做稳。

#### 对比矩阵（本轮新增观察点）

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| `add / remove / modify` 事件在插件层分开建模 | Full | Partial | None | Partial | None | Full |
| rename 会同时保留旧路径和新路径语义 | Full | None | None | None | None | Full |
| reload 失败后旧脚本逻辑显式继续可执行 | Full | Partial | Partial | Partial | None | Full |
| 存在显式 `PreviouslyFailed...` / `QueuedFullReload...` 账本 | Full | None | None | None | None | Full |
| watcher 只关注已加载文件 | None | None | None | Full | N/A | None |
| 插件边界只到 loader 注入点，由项目自管 retry / rename / delete | None | None | None | Partial | Full | None |
| “失败保旧代码”有自动化测试直接兜底 | None | None | None | None | None | Full |

#### 小结与建议

- 当前 Angelscript 在 `D4` 上本轮新增确认的强项，不是“有热重载”，而是“**事件语义 + 失败记忆 + 旧代码存活**”都进入了插件状态机，并且被测试覆盖。这是 `P0` 级别的护城河。
- 最值得吸收的是 UnLua 的**用户可见 reload 模式开关**，不是它把 watcher 做薄的做法。建议给当前 AS 补 `Manual / Auto / Never` 一类策略面板，但底层继续保留现有事件账本，优先级 `P1`。
- 不建议为追求“简单”而退回 puerts / slua 式的窄 watcher 或纯 loader 边界。那些方案适合文件级脚本系统，但会直接丢掉当前 AS 在 rename / full-reload-later / keep-old-code 上的质量优势，优先级判断为 `P0` 保持现状。

### [D11] 部署操作者界面：开关、路径与 loader 注入点

#### 各插件实现概览

```
[D11-Deep-2] Operator-Facing Deployment Surface
HZ : cmd flags (-as-development-mode / -as-generate-precompiled-data / -as-ignore-precompiled-data) + project/plugin Script roots
AS : wider cmd flags + dependency-based Script-root discovery + build-specific precompiled cache names
UC : editor settings -> ScriptDirectory / PublishDirectory -> assembly search in Content/<Publish> + .NET dir
UL : fixed platform whitelist + staged Lua runtime lib + runtime resolves RelativeFilePath to full path
PU : build-time backend flags + staged VM DLLs + loader search in ProjectContent/<ScriptRoot> (+ JavaScript fallback)
SL : project-supplied LoadFileDelegate + Content/Lua .lua/.luac + prebuilt lua.lib
```

上一轮 `D11` 已经比较过“打包后真正恢复的 artifact 是什么”。这一轮不再重复 artifact，而是补**操作者真正能改什么**：

- 是命令行开关、editor setting、build-time define，还是根本交给项目写一个 loader。
- 脚本/程序集/VM 运行库到底从哪些根目录开始找。
- 这些路径和开关是插件自己定死，还是给工程保留了可替换面。

这一层直接决定后续是否容易接 CI、补丁系统、多仓库脚本布局，以及作者对“为什么这次包里没找到脚本/assembly”的可解释性。

#### 详细对比

##### 子维度 1：部署行为的操作者界面是什么

- 当前 Angelscript 的操作者界面最明确。`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:514-531` 的 `FAngelscriptEngineConfig::FromCurrentProcess()` 不只支持 `-as-development-mode / -as-generate-precompiled-data / -as-ignore-precompiled-data`，还暴露了 `-as-simulate-cooked / -as-skip-write-bind-db / -as-write-bind-db / -as-exit-on-error / -asdebugport=` 等开关。也就是说，部署/运行模式不是硬编码在 build 里，而是显式进了 runtime config。
- Hazelight 仍然是这条线的前身。`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/AngelscriptManager.cpp:361-367` 只暴露三类关键开关：生成 precompiled data、development mode、忽略 precompiled data。当前仓库是在同一血缘上把 operator surface 做得更完整。
- UnrealCSharp 的操作者界面不在 command line，而在设置项。`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp:995-1065` 通过 `GetPublishDirectory()`、`GetScriptDirectory()` 把 publish/script 目录转成实际路径；这些路径又回到 editor setting。对于 C# 项目，这种设置驱动比命令行更符合作者心智。
- UnLua 的部署界面更少暴露给运行时用户。`Reference/UnLua/Plugins/UnLua/UnLua.uplugin:20-35` 主要通过 `WhitelistPlatforms` 控制模块在哪些平台加载；`Reference/UnLua/Plugins/UnLua/Source/ThirdParty/Lua/Lua.Build.cs:209-211,470` 负责把 Lua 运行库带进包。路径策略本身没有像 UnrealCSharp/puerts/slua 那样独立成可配置 loader 面。
- puerts 把一部分界面放在 build-time backend 选择上。`Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs:360-408` 通过 `RuntimeDependencies.Add(..., StagedFileType.NonUFS)` staged 不同 VM DLL，并在 `WithByteCode` 时打开 `WITH_V8_BYTECODE`。它的 operator surface 更多体现在“这次包用哪套 VM 后端”。
- sluaunreal 的操作者界面最“工程自定义”。`Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaState.h:167-189` 只要求你提供 `LoadFileDelegate`；demo 里 `Reference/sluaunreal/Source/democpp/MyGameInstance.cpp:42-58` 选择从 `Content/Lua` 读 `.lua/.luac`，`Reference/sluaunreal/Plugins/slua_unreal/make_win.bat:1-13` 则负责准备 `lua.lib`。插件不给统一部署面，项目自己决定。

[1] 当前 Angelscript 的 operator surface 已经是 runtime config，而不只是几个散落命令行：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: FAngelscriptEngineConfig::FromCurrentProcess
// 位置: 514-531
// 说明: 当前 AS 的部署/运行模式由显式 config 收口，便于命令行、CI、commandlet 统一驱动
// ============================================================================
FAngelscriptEngineConfig FAngelscriptEngineConfig::FromCurrentProcess()
{
	FAngelscriptEngineConfig Config;
	Config.bForceThreadedInitialize = FParse::Param(FCommandLine::Get(), TEXT("as-force-threaded-initialize"));
	Config.bSkipThreadedInitialize = FParse::Param(FCommandLine::Get(), TEXT("as-skip-threaded-initialize"));
	Config.bSimulateCooked = FParse::Param(FCommandLine::Get(), TEXT("as-simulate-cooked"));
	Config.bGeneratePrecompiledData = FParse::Param(FCommandLine::Get(), TEXT("as-generate-precompiled-data"));
	Config.bDevelopmentMode = FParse::Param(FCommandLine::Get(), TEXT("as-development-mode"));
	Config.bIgnorePrecompiledData = FParse::Param(FCommandLine::Get(), TEXT("as-ignore-precompiled-data"));
	Config.bSkipWriteBindDB = FParse::Param(FCommandLine::Get(), TEXT("as-skip-write-bind-db"));
	Config.bWriteBindDB = FParse::Param(FCommandLine::Get(), TEXT("as-write-bind-db"));
	Config.bExitOnError = FParse::Param(FCommandLine::Get(), TEXT("as-exit-on-error"));
	FParse::Value(FCommandLine::Get(), TEXT("-asdebugport="), Config.DebugServerPort);
	...
}
```

```cpp
// ============================================================================
// 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/AngelscriptManager.cpp
// 函数: 初始化期命令行开关读取
// 位置: 361-367
// 说明: Hazelight 的 operator surface 更窄，但已经具备当前 AS 的核心前身
// ============================================================================
bGeneratePrecompiledData = FParse::Param(FCommandLine::Get(), TEXT("as-generate-precompiled-data"));
bScriptDevelopmentMode = GIsEditor || FParse::Param(FCommandLine::Get(), TEXT("as-development-mode"));
bUsePrecompiledData = !bGeneratePrecompiledData && !FParse::Param(FCommandLine::Get(), TEXT("as-ignore-precompiled-data"))
	&& !IsRunningCommandlet() && !WITH_EDITOR && !bScriptDevelopmentMode;
// ★ 当前 AS 不是换路线，而是在这条线之上把 runtime config 展开得更细
```

[2] UnrealCSharp 把部署入口收在 setting 驱动的路径体系，而不是命令行：

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp
// 函数: GetFullPublishDirectory / GetAssemblyPath / GetScriptDirectory / GetFullScriptDirectory
// 位置: 995-1065
// 说明: 发布目录、程序集搜索路径、源码目录都来自 setting，而不是硬编码命令行
// ============================================================================
FString FUnrealCSharpFunctionLibrary::GetFullPublishDirectory()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / GetPublishDirectory());
}

TArray<FString> FUnrealCSharpFunctionLibrary::GetAssemblyPath()
{
	return TArrayBuilder<FString>().
	       Add(FPaths::ProjectContentDir() / GetPublishDirectory()).
	       Add(FMonoFunctionLibrary::GetNetDirectory()).
	       Build();
}

FString FUnrealCSharpFunctionLibrary::GetFullScriptDirectory()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / GetScriptDirectory());
}
// ★ 操作者真正改的是 PublishDirectory / ScriptDirectory，而不是某个 cache 文件名
```

##### 子维度 2：脚本/程序集/运行库从哪里开始找

- 当前 Angelscript 相比 Hazelight 的新差异，不是根目录名，而是**根目录发现方式**。`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1326-1363` 通过 `Dependencies.GetEnabledPluginScriptRoots()` 做依赖注入，再把 `Project/Script` 插到搜索顺序首位。Hazelight 的 `J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/AngelscriptManager.cpp:269-304` 则直接走 `IPluginManager::GetEnabledPluginsWithContent()`。前者更利于测试、commandlet 和外部宿主替换。
- UnrealCSharp 的运行时查找逻辑更像程序集加载器。`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/AssemblyLoader.cpp:6-18` 会按 `GetAssemblyPath()` 列表顺序搜索 DLL 字节。这里的 authority 不是脚本源码目录，而是 publish 后的 assembly root。
- UnLua 仍以“相对脚本路径 -> 绝对文件路径 -> 读原始字节流”为中心。`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaBase.cpp:68-84` 的 `LoadFile()` 先把 `RelativeFilePath` 变成 `FullFilePath`，再 `LoadFileToArray()`。这说明它的 operator contract 更偏“确保脚本文件出现在约定位置”，而不是“替换 loader”。
- puerts 的搜索根最多。`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DefaultJSModuleLoader.cpp:67-139` 会先从调用方目录往上回溯 `node_modules`，再退回 `ProjectContent/<ScriptRoot>`，最后在 `ScriptRoot != JavaScript` 时再给默认 `ProjectContent/JavaScript` 一次兜底。再叠加 `.js/.mjs/.cjs/.json/.mbc/.cbc` 多后缀，路径灵活性最高，但搜索面也最宽。
- sluaunreal 的脚本根本不由插件定义。`LoadFileDelegate` 可以指到任意来源；demo 只是一个示例实现。它的好处是灵活，代价是项目必须自己回答“最终从哪找脚本、怎么做版本/签名/热更源切换”。

[3] 当前 Angelscript / Hazelight 的共同点是“项目脚本根优先”，不同点是当前 AS 把根目录发现做成了可注入依赖：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: FAngelscriptEngine::DiscoverScriptRoots
// 位置: 1326-1363
// 说明: 当前 AS 的搜索顺序是 Project/Script 优先，再枚举启用插件的 Script 根
// ============================================================================
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
DiscoveredRootPaths.Insert(RootPath, 0); // ★ project root 永远排第一
```

```cpp
// ============================================================================
// 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/AngelscriptManager.cpp
// 函数: FAngelscriptManager::MakeAllScriptRoots
// 位置: 269-304
// 说明: Hazelight 也是 project-first，但通过 IPluginManager 直接枚举插件
// ============================================================================
FString RootPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Script"));
...
for (auto& Plugin : PluginManager.GetEnabledPluginsWithContent())
{
	auto ScriptPath = FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir() / TEXT("Script"));
	if (FileManager.DirectoryExists(*ScriptPath) && ScriptPath != RootPath)
	{
		AllRootPaths.Add(ScriptPath);
	}
}
AllRootPaths.Sort();
AllRootPaths.Insert(RootPath, 0);
// ★ 路线相同，但当前 AS 的新价值在于“根目录来源可被 Dependencies 替换”
```

[4] UnLua / puerts / slua 分别代表“固定相对路径”、“宽搜索 loader”和“完全项目自定义 loader”三种极端：

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/UnLua.uplugin
// 文件: Reference/UnLua/Plugins/UnLua/Source/ThirdParty/Lua/Lua.Build.cs
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaBase.cpp
// 位置: 20-35, 209-211, 68-84
// 说明: UnLua 通过平台白名单和 runtime dependency 约束“能在哪跑”，运行时再按相对路径读脚本
// ============================================================================
"Name": "UnLua",
"Type": "Runtime",
"LoadingPhase": "PreDefault",
"WhitelistPlatforms": [ "Win64", "Mac", "IOS", "Android", "Linux" ]

PublicAdditionalLibraries.Add(libFile);
RuntimeDependencies.Add(libFile);

FString FullFilePath = GetFullPathFromRelativePath(RelativeFilePath);
...
bool bSuccess = FFileHelper::LoadFileToArray(Data, *FullFilePath, 0);
// ★ operator 主要控制“文件放在哪个平台包里”，不是替换 loader
```

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DefaultJSModuleLoader.cpp
// 位置: 360-408, 67-139
// 说明: puerts 同时暴露 VM runtime staging 和 module loader 搜索策略
// ============================================================================
var DestDllPath = Path.Combine("$(BinaryOutputDir)", DllName);
RuntimeDependencies.Add(DestDllPath, DllPath, StagedFileType.NonUFS);
...
if (WithByteCode)
{
	PrivateDefinitions.Add("WITH_V8_BYTECODE");
}

return SearchModuleWithExtInDir(Dir, RequiredModule + ".js", Path, AbsolutePath) ||
       SearchModuleWithExtInDir(Dir, RequiredModule + ".mjs", Path, AbsolutePath) ||
       SearchModuleWithExtInDir(Dir, RequiredModule + ".cjs", Path, AbsolutePath) ||
#if defined(WITH_V8_BYTECODE)
       SearchModuleWithExtInDir(Dir, RequiredModule + ".mbc", Path, AbsolutePath) ||
       SearchModuleWithExtInDir(Dir, RequiredModule + ".cbc", Path, AbsolutePath) ||
#endif
       SearchModuleWithExtInDir(Dir, RequiredModule / "package.json", Path, AbsolutePath);
...
return SearchModuleInDir(FPaths::ProjectContentDir() / ScriptRoot, RequiredModule, Path, AbsolutePath) ||
       (ScriptRoot != TEXT("JavaScript") &&
           SearchModuleInDir(FPaths::ProjectContentDir() / TEXT("JavaScript"), RequiredModule, Path, AbsolutePath));
// ★ operator 可以同时动 VM backend、bytecode 开关和脚本根目录
```

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaState.h
// 文件: Reference/sluaunreal/Source/democpp/MyGameInstance.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/make_win.bat
// 位置: 167-189, 42-58, 1-13
// 说明: slua 的部署入口是“你给我一个 loader”；demo 只是选择了 Content/Lua + .lua/.luac
// ============================================================================
// file how to loading depend on load delegation
LuaVar doFile(const char* fn, LuaVar* pEnv = nullptr);
...
void setLoadFileDelegate(LoadFileDelegate func);

state->setLoadFileDelegate([](const char* fn, FString& filepath)->TArray<uint8> {
	FString path = FPaths::ProjectContentDir();
	path /= "Lua";
	...
	TArray<FString> luaExts = { UTF8_TO_TCHAR(".lua"), UTF8_TO_TCHAR(".luac") };
	...
});

copy /Y build_win64\RelWithDebInfo\lua.lib Library\Win64\lua.lib
// ★ 运行库和脚本源都由工程自己接线，插件只负责 VM + bridge
```

##### 子维度 3：对当前 Angelscript 的真实启发

- 当前 Angelscript 相比 Hazelight 的一条真正进化，不是又多了几个 cache 文件，而是 **runtime config 与脚本根发现都更可注入、更适合自动化环境**。这条线应该继续保持，不要重新把路径发现写死回 `IPluginManager` 直连，优先级 `P0`。
- 最值得吸收的是 UnrealCSharp 的**设置驱动目录面**。如果后续真出现多仓库脚本布局、外部生成目录、CI 产物分流等需求，当前 AS 可以考虑把“额外 script root / output root”做成显式 setting，而不是让用户只能改工程目录结构，优先级 `P1`。
- puerts 和 slua 的 loader 灵活性值得借鉴，但只能作为**可选扩展层**。一旦把当前 AS 的 project/plugin root contract 改成完全自由 loader，就会直接削弱现有 `Binds.Cache / PrecompiledScript` 的确定性，优先级 `P2`。
- UnLua 提醒了另一个细节：平台 whitelist 和 runtime dependency 最好持续显式化。当前 AS 已经有很强的 cache 校验，但平台/依赖边界的声明性还可以继续增强，优先级 `P1`。

#### 对比矩阵（本轮新增观察点）

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 部署/运行模式有显式 command line 开关 | Full | None | None | None | None | Full |
| 脚本或发布目录来自 setting / project 配置 | None | Full | Partial | Partial | Full | Partial |
| 默认自动发现 `Project/Script` + enabled plugin `Script` 根 | Full | None | None | None | None | Full |
| 运行时主搜索面是 publish 后的 assembly / VM binary 根 | None | Full | Partial | Full | Partial | None |
| loader 路径可由项目代码完全接管 | None | None | None | Partial | Full | None |
| 平台白名单 / runtime dependency 在插件描述或 Build.cs 中显式声明 | Partial | Partial | Full | Full | Partial | Partial |
| 搜索顺序对 project root 有显式优先级保证 | Full | N/A | Partial | Partial | N/A | Full |

#### 小结与建议

- 当前 Angelscript 在 `D11` 上本轮新增确认的强项，是“**operator surface 明确且 deterministic**”。命令行开关、project-first script roots、build-specific cache 名称已经构成了非常稳定的部署控制面，这一层建议按 `P0` 继续保持。
- 最值得吸收的是 UnrealCSharp 的**设置驱动目录能力**，以及 UnLua/puerts 那种更显式的平台与 runtime dependency 声明。建议把“额外路径/平台行为”的 operator surface 做得更显式，但不要牺牲现有 cache 一致性，优先级 `P1`。
- 不建议把当前 AS 直接改造成 slua 式完全自由 loader。那种自由度适合线上 patch/CDN 方案，但会把“为什么这次 build 找不到脚本/为什么 cache 不匹配”的责任全部推回项目层，和当前 AS 的确定性路线冲突，优先级 `P2`。

---

## 深化分析 (2026-04-08 20:02:02)

### [D7] headless tooling / commandlet contract 的 owner

#### 各插件实现概览

```
[D7-Headless] Non-Interactive Tooling Entry
HZ : runtime path -> AllScriptRoots / Test commandlets
AS : runtime path -> AllScriptRoots / Test / BlueprintImpact commandlets
UL : UnLuaIntelliSenseCommandlet -> Intermediate/IntelliSense/*.lua (+BP=1)
UC : CookCommandlet hook -> OnFilesLoaded -> Generator()      // 不是独立 UCommandlet
PU : editor watcher disabled in commandlet; runtime keeps CLI/debug parsing
SL : profiler / debug helper disabled in commandlet
```

上一轮 `D7` 看的重点是 editor surface；这一轮补的是 **把编辑器 UI 拿掉以后，各插件还剩下什么自动化入口**。这里最重要的差异不是“有没有菜单”，而是“谁把 headless 运行当成一等能力”。

#### 详细对比

##### 子维度 1：谁真的把 commandlet 当成 first-class operator surface

- 当前 Angelscript 已经不是“editor 工具顺便能在命令行跑”。`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp:13-25` 在 `StartupModule()` 里直接把 `IsRunningCommandlet()` 视为正常启动条件；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAllScriptRootsCommandlet.cpp:5-21`、`.../AngelscriptTestCommandlet.cpp:5-25` 和 `Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp:55-120` 又把脚本根枚举、脚本单测批执行、Blueprint 影响面扫描拆成了三个显式入口。这里应判为 `Full`。
- Hazelight 的 headless 路线是当前 AS 的直接前身。`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/AngelscriptAllScriptRootsCommandlet.cpp:5-21` 和 `.../AngelscriptTestCommandlet.cpp:5-25` 与当前 AS 基本同构，但还没有 Blueprint impact 一类额外 operator tool，所以应判为 `Partial` 而不是 `None`。
- UnLua 的思路不同。它确实有独立的 `UUnLuaIntelliSenseCommandlet`，`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp:29-114` 直接导出静态绑定类型和可选的 Blueprint IntelliSense；但这个入口只负责 IDE 产物，不负责测试、资产影响分析或运行时健康检查，所以是“单点工具 commandlet”，判为 `Partial`。
- UnrealCSharp 在本轮源码快照中**没有定位到独立 `UCommandlet` 工具类**。它的 headless 路线落在 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:115-123`：如果正在 `CookCommandlet`，就在 `AssetRegistry::OnFilesLoaded` 之后调用 `Generator()`。这说明它是 `cook-aware editor hook`，不是 commandlet-first tool surface。
- puerts 和 sluaunreal 在这一维度上的共同点是“尽量别在 commandlet 时把 editor tool 拉起来”。`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:76-78` 明确把 `Enabled` 设为 `IsWatchEnabled() && !IsRunningCommandlet()`；`Reference/sluaunreal/Plugins/slua_unreal/Source/slua_profile/Private/slua_profile.cpp:70-83` 与 `Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaState.cpp:548-555` 也都在 commandlet 下关闭 profiler / dead-loop guard。这里不是“有另一套 headless tool”，而是“主动退让”，应判为 `None`。

[1] 当前 Angelscript 的 headless path 不是旁路，而是 runtime 的正式启动路径：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp
// 函数: FAngelscriptRuntimeModule::StartupModule
// 位置: 13-25
// 说明: 当前 AS 在 commandlet 下直接初始化脚本引擎，不依赖 editor UI
// ============================================================================
void FAngelscriptRuntimeModule::StartupModule()
{
	if (GIsEditor || IsRunningCommandlet())
	{
		InitializeAngelscript(); // ★ commandlet 也是一等启动环境
	}

	if (GIsEditor)
	{
		FallbackTickHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateRaw(this, &FAngelscriptRuntimeModule::TickFallbackPrimaryEngine));
	}
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp
// 函数: UAngelscriptBlueprintImpactScanCommandlet::Main
// 位置: 55-120
// 说明: 这一条命令行路径已经具备参数输入、结构化日志和分级退出码
// ============================================================================
int32 UAngelscriptBlueprintImpactScanCommandlet::Main(const FString& Params)
{
	if (!FAngelscriptEngine::Get().bDidInitialCompileSucceed)
	{
		UE_LOG(Angelscript, Error, TEXT("Blueprint impact commandlet requires a successfully initialized Angelscript engine."));
		return static_cast<int32>(EBlueprintImpactCommandletExitCode::EngineNotReady);
	}

	if (FParse::Value(*Params, TEXT("ChangedScriptFile="), ChangedScriptsFile))
	{
		if (!TryReadChangedScriptsFile(ChangedScriptsFile, Request.ChangedScripts))
		{
			return static_cast<int32>(EBlueprintImpactCommandletExitCode::InvalidArguments);
		}
	}

	const AngelscriptEditor::BlueprintImpact::FBlueprintImpactScanResult ScanResult = AngelscriptEditor::BlueprintImpact::ScanBlueprintAssets(
		FAngelscriptEngine::Get(),
		AssetRegistryModule.Get(),
		Request);

	UE_LOG(Angelscript, Display,
		TEXT("{ \"BlueprintImpact\": { \"FullScan\": %s, \"ChangedScripts\": %d, \"MatchingModules\": %d, \"Classes\": %d, \"Structs\": %d, \"Enums\": %d, \"Delegates\": %d, \"CandidateAssets\": %d, \"Matches\": %d, \"FailedAssetLoads\": %d } }"),
		Request.IsFullScan() ? TEXT("true") : TEXT("false"),
		ScanResult.NormalizedChangedScripts.Num(),
		ScanResult.MatchingModules.Num(),
		ScanResult.Symbols.Classes.Num(),
		ScanResult.Symbols.Structs.Num(),
		ScanResult.Symbols.Enums.Num(),
		ScanResult.Symbols.Delegates.Num(),
		ScanResult.CandidateAssets.Num(),
		ScanResult.Matches.Num(),
		ScanResult.FailedAssetLoads);

	return ScanResult.FailedAssetLoads > 0
		? static_cast<int32>(EBlueprintImpactCommandletExitCode::AssetScanFailure)
		: static_cast<int32>(EBlueprintImpactCommandletExitCode::Success);
}
```

[2] UnLua / UnrealCSharp / puerts / slua 在 headless contract 上各自落在不同层：

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp
// 函数: UUnLuaIntelliSenseCommandlet::Main
// 位置: 29-114
// 说明: UnLua 的 commandlet 是单点产物导出器，目标很聚焦
// ============================================================================
int32 UUnLuaIntelliSenseCommandlet::Main(const FString &Params)
{
	const auto ExportedReflectedClasses = UnLua::GetExportedReflectedClasses();
	...
	SaveFile(ModuleName, TEXT("GlobalFunctions"), GeneratedFileContent);

	ParseCommandLine(*Params, Tokens, Switches, ParamsMap);
	if (ParamsMap.Contains(TEXT("BP")) && ParamsMap[TEXT("BP")] == TEXT("1"))
	{
		auto Generator = FUnLuaIntelliSenseGenerator::Get();
		Generator->Initialize();
		Generator->UpdateAll(); // ★ 可选地把 Blueprint IntelliSense 也一并导出
	}
	return 0;
}

// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp
// 函数: FUnrealCSharpEditorModule::StartupModule
// 位置: 115-123
// 说明: UnrealCSharp 的非交互入口是 cook hook，不是独立 commandlet 类
// ============================================================================
if (IsRunningCookCommandlet())
{
	if (const auto AssetRegistryModule = FModuleManager::LoadModulePtr<FAssetRegistryModule>(TEXT("AssetRegistry")))
	{
		AssetRegistryModule->Get().OnFilesLoaded().AddLambda([]()
		{
			Generator(); // ★ cook 完成资源装载后再跑生成
		});
	}
}

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp
// 函数: FPuertsEditorModule::StartupModule
// 位置: 76-79
// 说明: puerts 的 editor watcher 在 commandlet 下直接关闭
// ============================================================================
void FPuertsEditorModule::StartupModule()
{
	Enabled = IPuertsModule::Get().IsWatchEnabled() && !IsRunningCommandlet();
	...
}

// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_profile/Private/slua_profile.cpp
// 函数: Fslua_profileModule::StartupModule
// 位置: 70-83
// 说明: slua_profile 只在 editor 且非 commandlet 时注册 profiler tab
// ============================================================================
if (GIsEditor && !IsRunningCommandlet())
{
	sluaProfilerInspector = MakeShareable(new SProfilerInspector);
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(slua_profileTabName,
		FOnSpawnTab::CreateRaw(this, &Fslua_profileModule::OnSpawnPluginTab))
		.SetDisplayName(LOCTEXT("Flua_wrapperTabTitle", "slua Profiler"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);
}
```

##### 子维度 2：headless 输出是给人看，还是给机器消费

- 当前 Angelscript 的 `AllScriptRoots` 和 `BlueprintImpact` commandlet 都输出明确 JSON 形状，`BlueprintImpact` 还区分 `InvalidArguments / EngineNotReady / AssetScanFailure` 三档错误码；这已经明显偏 CI / automation contract，而不是“打印几行日志给开发者看”。
- Hazelight 的 `AllScriptRootsCommandlet` 同样会输出 `{ "AngelscriptScriptRoots": [...] }`，`AngelscriptTestCommandlet` 也有 `1/2/3` 三类退出码；但它还没有当前 AS 这种“参数化分析器”级别的工具面，所以机器可消费面仍更窄。
- UnLua 的产物面是文件树，不是 JSON。`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp:117-133` 会把结果写到 `ProjectPlugins/UnLua/Intermediate/IntelliSense/...`，适合 IDE 消费，不适合直接回答“哪些资产受影响”这种分析问题。
- UnrealCSharp 的 cook hook 更像“保证产物存在”，并没有额外定义命令行结果协议；puerts/slua 在这一轮源码里也没有发现对等的 machine-readable tool output。

##### 子维度 3：对当前 Angelscript 的真实启发

- 当前 Angelscript 在 `D7` 上新增确认的优势，不只是 editor integration 深，而是 **headless operator surface 已经比多数参考插件更完整**。这一层应按 `P0` 保持。
- 最值得吸收的是 UnLua 那种**单目标 commandlet** 思路。当前 AS 已经有大而全的 runtime + editor tool，但后续仍可把 `docs dump / bind summary / debug manifest` 继续拆成更窄的命令行入口，优先级 `P1`。
- UnrealCSharp 提醒的是另一条线：有些工具不一定非要单独做成 `UCommandlet`，cook-aware hook 也有价值；但这只能作为补充，不能替代当前 AS 已经很清晰的 commandlet contract，优先级 `P2`。

#### 对比矩阵（本轮新增观察点）

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 公开独立 `UCommandlet` 入口 | Full | None | Full | None | None | Full |
| commandlet 可完成非 UI 的一等任务 | Partial | Partial | Full | None | None | Full |
| tooling 支持命令行参数驱动 | None | None | Full | None | None | Full |
| 产物可直接被机器消费（JSON / 文件树） | Partial | Partial | Full | None | None | Full |
| 退出码区分失败类型 | Partial | None | None | None | None | Full |
| editor watcher / profiler 在 commandlet 下显式退让 | N/A | Full | N/A | Full | Full | Full |

#### 小结与建议

- 当前 Angelscript 在 `D7` 上本轮新增确认的强项，是 **headless tooling 已经是正式产品面，不是调试副产物**。`RuntimeModule` 的 commandlet 启动、JSON 输出和分级退出码这三件事建议按 `P0` 继续保持。
- 最值得吸收的是 UnLua 的“单目标 commandlet”风格。当前 AS 后续若继续扩工具链，建议优先补更窄、更稳定的 `docs / manifest / summary` 入口，而不是把所有事情都继续塞进 editor 菜单，优先级 `P1`。
- 不建议把当前 AS 回退到 puerts/slua 这种“commandlet 下只做 feature gating，不提供工具入口”的路线；那会直接削弱已有自动化价值，优先级 `P2`。

### [D9] 测试批执行 contract：谁负责 discover、batch、exit code

#### 各插件实现概览

```
[D9-Batch] Test Discovery And Batch Owner
HZ : post-scan discover script tests -> TestCommandlet(1/2/3)
AS : post-scan discover script tests -> TestCommandlet(1/2/3) + settings gate
UL : UE Automation macros/specs/issues -> map assets / latent commands
UC : no public Test surface in plugin modules
PU : no Unreal-side test batch surface in plugin modules
SL : no automation batch surface in plugin modules; democpp acts as sample app
```

上一轮 `D9` 讨论的是“测试 authority 放在哪一层”；这一轮补的是 **真正批量执行时，谁发现测试、谁命名测试、谁定义退出语义**。

#### 详细对比

##### 子维度 1：测试是从脚本里 discover，还是从 C++ automation 宏里注册

- 当前 Angelscript 与 Hazelight 的核心特征，是**把脚本函数命名约定直接变成 discover protocol**。`Plugins/Angelscript/Source/AngelscriptRuntime/Testing/DiscoverTests.cpp:20-59` 的 `CreateOneArgFilter()` 明确要求函数名、参数个数、参数类型都匹配；`...:172-208` 又把 `Test_`、`ComplexUnitTest_`、`IntegrationTest_`、`ComplexIntegrationTest_` 四类入口分开收集。这里的 authority 明确落在脚本模块本身。
- 更关键的是，当前 AS 与 Hazelight 都不是“编译完立刻扫”。`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2201-2218` 与 `J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/AngelscriptManager.cpp:1109-1118` 都会等 `AssetManager` 初始扫描完成后再调用 `DiscoverTests()`。这说明它们已经意识到测试 discover 依赖资产世界的稳定状态。
- UnLua 的 authority 完全不同。`Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Public/UnLuaTestCommon.h:171-215` 通过 `IMPLEMENT_UNLUA_LATENT_TEST / IMPLEMENT_UNLUA_INSTANT_TEST / BEGIN_TESTSUITE` 宏把测试注册到 UE Automation；`Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Specs/LuaEnv.spec.cpp:23-69` 又用 `BEGIN_DEFINE_SPEC` 和 `It(...)` 写 API 契约。这里的 discover owner 是 C++ automation framework，不是 Lua 脚本命名规范。
- 在本轮源码快照里，UnrealCSharp / puerts / sluaunreal 的插件模块表中都没有与 `AngelscriptTest` 或 `UnLuaTestSuite` 对等的公开测试模块，且未在插件 `Source` 主路径内定位到对等的 automation batch surface。这里应保守判定为 `None`，而不是外推到仓库外可能存在的私有测试环境。

[1] 当前 Angelscript 的测试 discover protocol 是脚本函数签名本身：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Testing/DiscoverTests.cpp
// 函数: CreateOneArgFilter / DiscoverUnitTests / DiscoverIntegrationTests
// 位置: 20-59, 172-208
// 说明: 通过函数名前缀 + 单参数类型，自动从脚本模块中发现测试
// ============================================================================
OneArgFunctionFilter CreateOneArgFilter(const char* NameStartsWith, const char* NameEndsWith, const char* ArgType)
{
	return [=](asIScriptFunction *F) {
		...
		if (F->GetParamCount() != 1)
			return false;
		...
		asITypeInfo *Type = F->GetEngine()->GetTypeInfoById(TypeId);
		if (FCStringAnsi::Strcmp(Type->GetName(), ArgType) != 0)
			return false;
		...
		return true;
	};
}

void DiscoverUnitTests(const FAngelscriptModuleDesc &Module, TMap<FString, FAngelscriptTestDesc> &UnitTestFunctions)
{
	OneArgFunctionFilter Filter = CreateOneArgFilter("Test_", "", "FUnitTest");
	DiscoverWithFilter(Module, UnitTestRunTestFunctions, Filter);
	...
	ComplexFilter = CreateOneArgFilter("ComplexUnitTest_", "GetTests", "TArray");
	DiscoverWithFilter(Module, ComplexUnitTestGetTestFunctions, ComplexFilter);
}

void DiscoverIntegrationTests(const FAngelscriptModuleDesc& Module, TMap<FString, FAngelscriptTestDesc>& IntegrationTestFunctions)
{
	OneArgFunctionFilter Filter = CreateOneArgFilter("IntegrationTest_", "", "FIntegrationTest");
	DiscoverWithFilter(Module, IntegrationTestRunTestFunctions, Filter);
	...
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: 初始化后测试发现
// 位置: 2201-2218, 2232-2249
// 说明: 当前 AS 先等待 AssetManager 初始扫描，再按 settings gate 做 discover
// ============================================================================
FCoreDelegates::OnPostEngineInit.AddLambda([&]()
{
	UAssetManager* AssetManager = UAssetManager::GetIfInitialized();
	if (AssetManager != nullptr)
	{
		AssetManager->CallOrRegister_OnCompletedInitialScan(
			FSimpleMulticastDelegate::FDelegate::CreateLambda([&]() {
				DiscoverTests(); // ★ 资产扫描完成后才 discover，避免测试列表过早稳定
				bCompletedAssetScan = true;
			}));
	}
});

void FAngelscriptEngine::DiscoverTests()
{
	if (!GetDefault<UAngelscriptTestSettings>()->bEnableTestDiscovery)
		return;
	if (bSimulateCooked || IsRunningCookCommandlet())
		return;
	for (auto& ActiveModule : GetActiveModules())
	{
		DiscoverUnitTests(*ActiveModule, ActiveModule->UnitTestFunctions);
		DiscoverIntegrationTests(*ActiveModule, ActiveModule->IntegrationTestFunctions);
	}
}
```

[2] UnLua 把 batch contract 交给 UE Automation，而不是脚本 discover：

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Public/UnLuaTestCommon.h
// 位置: 171-215
// 说明: UnLua 先用宏 DSL 固定 latent / instant / testsuite 三种注册方式
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

#define BEGIN_TESTSUITE(TestClass, PrettyName) \
namespace UnLuaTestSuite { \
IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, PrettyName, (...))

// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Specs/LuaEnv.spec.cpp
// 位置: 23-69
// 说明: API 行为契约通过 spec 明确表达，而不是靠脚本函数命名
// ============================================================================
BEGIN_DEFINE_SPEC(FLuaEnvSpec, "UnLua.API.FLuaEnv", EAutomationTestFlags::ProductFilter | EAutomationTestFlags::ApplicationContextMask)
	TSharedPtr<UnLua::FLuaEnv> Env;
END_DEFINE_SPEC(FLuaEnvSpec)

It(TEXT("支持多个Lua环境"), EAsyncExecution::TaskGraphMainThread, [this]()
{
	UnLua::FLuaEnv Env1;
	UnLua::FLuaEnv Env2;
	Env1.DoString("return 1");
	Env2.DoString("return 2");
	TEST_EQUAL(A, 1);
	TEST_EQUAL(B, 2);
});

// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Tests/Issue603Test.cpp
// 位置: 22-33
// 说明: regression 直接绑定地图资产和 latent automation command
// ============================================================================
BEGIN_TESTSUITE(FIssue603Test, TEXT("UnLua.Regression.Issue603 在Lua监听的事件中再次触发事件会导致崩溃"))
bool FIssue603Test::RunTest(const FString& Parameters)
{
	const auto MapName = TEXT("/UnLuaTestSuite/Tests/Regression/Issue603/Issue603");
	ADD_LATENT_AUTOMATION_COMMAND(FOpenMapLatentCommand(MapName))
	ADD_LATENT_AUTOMATION_COMMAND(FEngineWaitLatentCommand(1.0));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}
```

##### 子维度 2：批执行结果是谁定义的

- 当前 Angelscript 与 Hazelight 的 `TestCommandlet` 都把失败语义收成三档：`1` 代表初始编译失败，`2` 代表单元测试失败，`3` 代表 `FStructUtils::AttemptToFindUninitializedScriptStructMembers()` 不为零。`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptTestCommandlet.cpp:5-25` 与 `J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/AngelscriptTestCommandlet.cpp:5-25` 几乎同构。这是一种非常明确的 commandlet batch contract。
- UnLua 没有在插件层额外定义退出码协议；它把 batch 和结果收口交给 UE Automation 本身。优点是与 UE 原生测试生态天然一致，缺点是对外暴露的“插件自定义失败类型”更少。
- UnrealCSharp / puerts / sluaunreal 在这份插件快照里没有定位到对等的 test commandlet，因此也没有发现插件自定义的 batch exit contract。

##### 子维度 3：对当前 Angelscript 的真实启发

- 当前 Angelscript 在 `D9` 上最独特的点，不是测试数量，而是 **脚本 discover protocol + commandlet exit contract 已经闭环**。这一层建议按 `P0` 保持。
- 最值得吸收的是 UnLua 的两条组织经验：一是 `Issue603` 这种**问题编号直接进入测试名**，二是 spec 风格 API 契约。当前 AS 很适合在不放弃脚本 discover 的前提下，再补一层更稳定的 C++ 侧行为规范测试，优先级 `P1`。
- 另一个值得补的点，是给 `AngelscriptTestCommandlet` 增加 machine-readable summary。现在 exit code 已经很好，但还缺一份像 `BlueprintImpact` 那样可被 CI 直接消费的 JSON 摘要，优先级 `P1`。

#### 对比矩阵（本轮新增观察点）

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 等待初始 asset scan 后再 discover tests | Full | None | None | None | None | Full |
| 测试 discover authority 落在脚本命名约定 | Full | None | None | None | None | Full |
| UE Automation 宏 / spec DSL 是主入口 | None | None | Full | None | None | None |
| 公开独立 test commandlet | Full | None | None | None | None | Full |
| 退出码区分失败类型 | Full | None | None | None | None | Full |
| regression 直接绑定地图 / 资产样例 | None | None | Full | None | None | Partial |

#### 小结与建议

- 当前 Angelscript 在 `D9` 上本轮新增确认的强项，是 **脚本测试可以被自动发现并以 commandlet 形式稳定批执行**。这条线比“多几个测试文件”更难得，建议按 `P0` 继续保持。
- 最值得吸收的是 UnLua 的命名与组织策略，而不是它放弃 commandlet 的路线。建议给当前 AS 的 batch 结果补 `issue-id / category / asset` 三类更稳定的标签，优先级 `P1`。
- 不建议因为想学 spec DSL，就把当前 AS 的脚本 discover path 整个推翻成纯 C++ 注册。两条路线可以叠加，但脚本 discover 是当前 AS 很少见的差异化优势，优先级 `P0` 保持。

### [D10] onboarding artifact 的 freshness contract：谁负责防止示例 / 文档陈旧

#### 各插件实现概览

```
[D10-Freshness] Artifact Drift Prevention
HZ : dump-as-doc -> generated .hpp
AS : dump-as-doc -> generated .hpp + automation compile-check examples
UL : commandlet / editor generator -> IntelliSense files (write-if-changed)
UC : template copier seeds projects; some files stop auto-overwrite after first seed
PU : manual button -> Typing/cpp/index.d.ts
SL : demo project / manual loader sample
```

上一轮 `D10` 看的重点是“artifact 的 owner 是谁”；这一轮补的是 **artifact 怎么保持新鲜，谁来为 drift 负责**。

#### 详细对比

##### 子维度 1：API 参考是从 live runtime 重新导出，还是一次性模板

- Hazelight 与当前 Angelscript 都把 API 参考建立在 live runtime 之上。当前 AS 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:528-529, 2224-2227` 通过 `dump-as-doc` 命令行开关触发 `FAngelscriptDocs::DumpDocumentation(Engine)`；Hazelight 对应 `J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/AngelscriptManager.cpp:1131-1135` 也是同一路线。`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp:407-515, 675-730` 与 Hazelight 对应实现都会读取当前 engine/type metadata，再输出 `Docs/angelscript/generated/*.hpp`。这意味着只要导出动作发生，文档就天然贴近当前绑定状态。
- UnLua 的 freshness contract 更偏增量生成。`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:42-106` 会监听 `AssetRegistry` 并重新导出 Blueprint / Native type IntelliSense；`...:222-233` 的 `SaveFile()` 又只在内容变化时写盘。这里的价值不是“生成更多文件”，而是**生成动作不会制造无意义 churn**。
- puerts 的 `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/TemplateBindingGenerator.cpp:193-216` 会从当前 `ForeachRegisterClass` 的 live registry 生成 `Typing/cpp/index.d.ts`，新鲜度本身没问题；问题在于刷新入口主要仍是 `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/GenDTSCommands.cpp:13-17` 那个 editor button，所以 freshness 依赖操作者主动触发。
- UnrealCSharp 更像脚手架系统，而不是 live docs exporter。`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FSolutionGenerator.cpp:9-126` 会从 `Template/*.csproj / *.cs / *.sln` 复制出工程结构；但 `...:128-152` 的 `CopyTemplate()` 只在目标文件不存在或显式要求替换时才覆盖，且 `...:87-96` 对游戏工程模板甚至传入了 `false`。这意味着它优先保护用户编辑，代价是**模板本身的长期 freshness 不是系统保证，而是用户自己决定是否重建**。
- sluaunreal 在这一维度上最依赖人工。`Reference/sluaunreal/Source/democpp/MyGameInstance.cpp:36-64` 展示的是手动接线的 demo loader；本轮没有定位到与之配套的自动校验或增量生成链。这里应判为 `None`，不是“弱一点的 `Partial`”。

[1] 当前 Angelscript 沿用 Hazelight 的 live-runtime 文档导出，但额外把它做成显式 operator flag：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: FAngelscriptEngineConfig::FromCurrentProcess / 初始化结束后的 docs dump
// 位置: 520-529, 2224-2227
// 说明: docs dump 由显式命令行开关驱动，导出后立刻退出，适合自动化
// ============================================================================
Config.bWriteBindDB = FParse::Param(FCommandLine::Get(), TEXT("as-write-bind-db"));
Config.bExitOnError = FParse::Param(FCommandLine::Get(), TEXT("as-exit-on-error"));
Config.bDumpDocumentation = FParse::Param(FCommandLine::Get(), TEXT("dump-as-doc")); // ★ 文档导出是正式 operator flag

if (RuntimeConfig.bDumpDocumentation)
{
	FAngelscriptDocs::DumpDocumentation(Engine);
	FPlatformMisc::RequestExit(false); // ★ 产物导出完成后直接退出，不混在 editor 交互里
}

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp
// 函数: FAngelscriptDocs::DumpDocumentation
// 位置: 407-515, 675-730
// 说明: 文档内容直接来自 live engine/type metadata，再落地到 generated headers
// ============================================================================
void FAngelscriptDocs::DumpDocumentation(asIScriptEngine* Engine)
{
	TMap<FString, FDocClass> Classes;
	...
	FProperty* PropDesc = UnrealClass->FindPropertyByName(*Prop.Name);
	if (PropDesc != nullptr)
	{
		Prop.Documentation = PropDesc->GetMetaData("ToolTip");
		Prop.Category = PropDesc->GetMetaData("Category");
	}
	...
	FString Filename = FPaths::ProjectDir() / TEXT("/Docs/angelscript/generated") / ClassDoc.ClassName + TEXT(".hpp");
	...
	FFileHelper::SaveStringToFile(Content, *Filename);
}
```

[2] 当前 AS 的新优势不只是会导文档，还会把示例编译进自动化测试：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleActorTest.cpp
// 函数: FAngelscriptScriptExampleActorTest::RunTest
// 位置: 90-95
// 说明: 示例不是 README 附件，而是 automation test 的正式输入
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAngelscriptScriptExampleActorTest, "Angelscript.TestModule.ScriptExamples.Actor", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptScriptExampleActorTest::RunTest(const FString& Parameters)
{
	return AngelscriptScriptExamples::RunScriptExampleCompileTest(*this, GActorExample);
}

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleTestSupport.cpp
// 函数: RunScriptExampleCompileTest
// 位置: 16-59
// 说明: 每个示例都会在独立 engine clone 中编译，drift 会立刻转成测试失败
// ============================================================================
bool RunScriptExampleCompileTest(FAutomationTestBase& Test, const FScriptExampleSource& Example)
{
	FAngelscriptEngine& Engine = AcquireCleanSharedCloneEngine();
	...
	const FString VirtualFileName = FString::Printf(TEXT("ScriptExamples/%s"), *ExampleFileName);
	const bool bCompiled = CompileAnnotatedModuleFromMemory(&Engine, ModuleName, VirtualFileName, CombinedScriptCode);
	Test.TestTrue(*FString::Printf(TEXT("Compile example '%s' succeeds"), *ExampleFileName), bCompiled);
	return bCompiled;
}
```

[3] UnLua / UnrealCSharp / puerts 代表三种不同的 freshness contract：

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp
// 函数: Initialize / SaveFile
// 位置: 42-56, 222-233
// 说明: UnLua 既监听资产变化，也避免无变化重写
// ============================================================================
void FUnLuaIntelliSenseGenerator::Initialize()
{
	OutputDir = IPluginManager::Get().FindPlugin("UnLua")->GetBaseDir() + "/Intermediate/IntelliSense";
	AssetRegistryModule.Get().OnAssetAdded().AddRaw(this, &FUnLuaIntelliSenseGenerator::OnAssetAdded);
	AssetRegistryModule.Get().OnAssetRemoved().AddRaw(this, &FUnLuaIntelliSenseGenerator::OnAssetRemoved);
	AssetRegistryModule.Get().OnAssetRenamed().AddRaw(this, &FUnLuaIntelliSenseGenerator::OnAssetRenamed);
	AssetRegistryModule.Get().OnAssetUpdated().AddRaw(this, &FUnLuaIntelliSenseGenerator::OnAssetUpdated);
}

void FUnLuaIntelliSenseGenerator::SaveFile(const FString& ModuleName, const FString& FileName, const FString& GeneratedFileContent)
{
	FString FileContent;
	FFileHelper::LoadFileToString(FileContent, *FilePath);
	if (FileContent != GeneratedFileContent)
		FFileHelper::SaveStringToFile(GeneratedFileContent, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

// ============================================================================
// 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FSolutionGenerator.cpp
// 函数: Generator / CopyTemplate
// 位置: 87-96, 128-152
// 说明: UnrealCSharp 优先保护用户改过的模板文件，因此 freshness 并不总是系统兜底
// ============================================================================
CopyTemplate(
	FUnrealCSharpFunctionLibrary::GetGameProjectPath(),
	TemplatePath / DEFAULT_GAME_NAME + PROJECT_SUFFIX,
	TArray<TFunction<void(FString& OutResult)>>{ ... },
	false); // ★ 这里明确不覆盖已有游戏工程模板

void FSolutionGenerator::CopyTemplate(const FString& Dest, const FString& Src,
	const TArray<TFunction<void(FString& OutResult)>>& InFunction,
	const bool bReplaceExistingFile)
{
	if (auto& FileManager = IFileManager::Get(); !FileManager.FileExists(*Dest) || bReplaceExistingFile)
	{
		...
		FUnrealCSharpFunctionLibrary::SaveStringToFile(*Dest, Result);
	}
}

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/TemplateBindingGenerator.cpp
// 函数: UTemplateBindingGenerator::Gen_Implementation
// 位置: 193-216
// 说明: puerts 的 d.ts 足够新鲜，但刷新时机主要取决于操作者是否点了生成按钮
// ============================================================================
void UTemplateBindingGenerator::Gen_Implementation() const
{
	PUERTS_NAMESPACE::ForeachRegisterClass(
		[&](const PUERTS_NAMESPACE::JSClassDefinition* ClassDefinition)
		{
			if (ClassDefinition->TypeId && ClassDefinition->ScriptName)
			{
				Gen.GenClass(ClassDefinition);
			}
		});

	const FString FilePath = FPaths::ProjectDir() / TEXT("Typing/cpp/index.d.ts");
	FFileHelper::SaveStringToFile(Gen.Output.Buffer, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
```

##### 子维度 2：示例 / 教程的 drift 由谁负责

- 当前 Angelscript 在这条线上比 Hazelight 明显更进一步。Hazelight 本轮确认了 `dump-as-doc` 路线仍在，但没有在插件源码里定位到与当前 `Angelscript.TestModule.ScriptExamples.*` 对等的 compile-checked 示例层；因此这里应判为“当前 AS 的实现质量更高”，不是“路线不同”。
- UnLua 的 README 和教程矩阵仍然很强，`Reference/UnLua/README.md:29-69` 明确把 `Content/Script/Tutorials/*.lua`、工具栏步骤和文档链接串成 onboarding 流程；但这套教程 freshness 更像“靠工程内容长期维护”，并没有像当前 AS 一样把每个教程条目都转成独立 compile test。
- UnrealCSharp 的模板路径提供了很好的 first-run onboarding，但由于 `CopyTemplate()` 常常选择“已有就不覆盖”，所以它天然更尊重项目定制，也天然更容易让模板和最新 runtime 能力逐步漂移。这不是 bug，而是刻意的 tradeoff。
- puerts 的 Unreal 侧 README 与 `GenDTS` 按钮让用户能较快拿到类型声明，但示例 freshness 在这份插件快照里仍然更依赖人工触发，而不是持续自动验证。
- sluaunreal 的 `democpp` 说明文档价值很高，但 freshness 完全归 demo 维护者负责；插件本身没有自动验证 sample 是否仍与 runtime 行为同步。

##### 子维度 3：对当前 Angelscript 的真实启发

- 当前 Angelscript 在 `D10` 上最值得保留的优势，是 **live-runtime docs + compile-checked examples** 这套双保险。前者保证 API 参考跟着绑定走，后者保证 onboarding 样例不会悄悄烂掉，优先级 `P0`。
- 最值得吸收的是 UnLua 的 `write-if-changed` 策略。当前 AS 的 `DumpDocumentation()` 仍然直接 `SaveStringToFile()`；如果后续文档导出频率升高，建议补一层内容比较，减少无意义文件 churn，优先级 `P1`。
- UnrealCSharp 与 puerts 提醒了另一个现实：有些 artifact 本来就该尊重用户修改，不应该每次硬覆盖。当前 AS 若将来补模板或 stub 生成，应该从一开始就区分“可重建产物”和“用户会手改的脚手架”，优先级 `P1`。

#### 对比矩阵（本轮新增观察点）

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| artifact 直接来自 live runtime / registry | Full | None | Full | Full | None | Full |
| 生成时避免无变化重写 | None | None | Full | None | None | None |
| 示例 / 教程被自动化测试直接校验 | None | None | Partial | None | None | Full |
| 提供非交互刷新入口（commandlet / CLI flag） | Full | Partial | Full | None | None | Full |
| 首次生成后 freshness 容易回到用户手工维护 | None | Full | Partial | Partial | Full | None |
| onboarding artifact 会因代码变更立刻显错 | Partial | Partial | Partial | Partial | None | Full |

#### 小结与建议

- 当前 Angelscript 在 `D10` 上本轮新增确认的最强点，是 **不仅能导文档，还能自动证明示例仍可编译**。这比单纯“文档多”更稀缺，建议按 `P0` 保持。
- 最值得吸收的是 UnLua 的低噪声生成策略。建议给当前 AS 的 docs / summary / manifest 产物补 `write-if-changed`，优先级 `P1`。
- 如果后续扩展模板或 stub 生成，不要简单照搬 UnrealCSharp 的“首次拷贝后默认不覆盖”，也不要走 slua 的“完全靠 demo 人工维护”。更合理的路线是把 artifact 显式分成“可重建”和“可手改”两类，并为前者保留自动验证，优先级 `P1`。

---
## 深化分析 (2026-04-08 20:21:30)

### [D5] source identity 的 owner：metadata / override map / VM frame / inspector url

#### 各插件实现概览

```
[D5-Identity] Source Identity Owner
HZ      : UASFunction/UASClass metadata -> editor navigation + debug server
AS(now) : UASFunction/UASClass metadata (+1-based line) -> editor navigation + debug server
UC      : CodeAnalysis/*.json override map -> toolbar OpenSourceFile
UL      : lua_Debug(source/currentline) -> callstack/locals only
PU      : ModuleLoader(OutDebugPath) + Inspector(scriptId <-> url)
SL      : lua_Debug(short_src/linedefined) -> profiler counter tree
```

前面两轮 `D5` 已经看过“协议 owner”和“bootstrap 拓扑”；这一轮补的是 **file / line / url 这类 source identity 到底挂在哪层对象上**。这件事决定了“跳转定义、调用栈、热更、profiling 名称”能不能复用同一份身份信息。

#### 详细对比

##### 子维度 1：identity 是持久 metadata，还是瞬时 frame 信息

- 当前 Angelscript 与 Hazelight 都把 source identity 挂在 `UASClass / UASFunction` 上。`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:1497-1559` 与 `J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/ClassGenerator/ASClass.cpp:1459-1534` 都直接从 `Module->Code[0].AbsoluteFilename` 和 `scriptData->declaredAt` 取 file / line；`AngelscriptSourceCodeNavigation.cpp` 则直接消费这份 metadata 打开源码。差别不在“有没有”，而在**行号归一化质量**：当前 AS 在 `.../ASClass.cpp:1548-1559` 对 `declaredAt` 做了 `+1`，Hazelight 在 `.../ASClass.cpp:1524-1534` 仍返回原始值，更像内部索引。
- UnrealCSharp 的 identity owner 不是 runtime frame，也不是 `UFunction` metadata，而是 `CodeAnalysis/*.json`。`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:34-36, 57-68, 205-212` 表明 editor 初始化先加载 `CodeAnalysisOverrideFilesMap`，之后 `OpenFile` 直接 `FSourceCodeNavigation::OpenSourceFile(OverrideFile)`。这里的好处是 authoring path 稳定；代价是 live stack frame 本身并不持有同一份 source identity。
- UnLua 与 sluaunreal 的 identity 都更接近 VM frame。`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaDebugBase.cpp:614-685` 通过 `lua_getstack + lua_getinfo("nSlu"/"nSl")` 拿到 `ar.source / ar.currentline`；`Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaStatProfile.cpp:53-54, 311-316` 则把 `short_src + linedefined` 拼成 profiler counter name。它们都能观测当前位置，但 identity 生命周期跟 frame 绑定，不是可长期复用的 editor metadata。
- puerts 的 identity owner 既不是 symbol metadata，也不是 Lua 那种即时 frame，而是 `url`。`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:4079-4096` 把 `OutPath + OutDebugPath` 返回给 JS；`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:56-67, 134-137` 用 `debugPath` 执行脚本并在 `Pak:` 场景回退到 `fullPath`；`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js:21-23, 67-89` 则把 `scriptId <-> url` 当成 hot reload 与 debugger 的同一把钥匙。这里 owner 是 inspector url contract，不是 UObject/world 里的类型系统。

##### 子维度 2：同一份 identity 会不会跨 editor / debugger / profiler 复用

- 当前 Angelscript 这条线最完整。`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp:34-44` 用 `UASFunction::GetSourceFilePath()` / `GetSourceLineNumber()` 做 editor 跳转；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:1288-1310, 1493-1505` 又把 symbol 查询和 debug database 建在同一套 script metadata 之上。也就是说，editor navigation 与 debugger 没有各自维护一份 path mapping。
- Hazelight 基本同构，但细节更粗。`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp:34-44` 也直接读取 `UASFunction` metadata；`.../Debugging/AngelscriptDebugServer.cpp:1163-1195, 1368-1383` 也是同一路线。这里应判为“实现方式相同、细节质量不同”，而不是“Hazelight 没有实现”。
- UnrealCSharp 的 editor authoring 复用性很好，但 scope 更窄。`OpenFile` 和 `CodeAnalysis` 都围绕同一份 `OverrideFile` 工作，可是这份 identity 是**生成映射文件**，不是 live runtime identity；它能很好支持“打开哪个 `.cs` 文件”，却不天然回答“当前栈帧在哪一行”。
- UnLua / sluaunreal 的 source identity 主要被日志、调用栈和 profiler 使用，而不是 editor-level 定义跳转 authority。它们不是没有 identity，而是 owner 落在 VM debug API，因此复用面不同。
- puerts 的 url identity 适合 debugger / HMR，未见当前快照里有与 UE editor `NavigateToFunction` 对等的 source navigation 接口。因此这里应判为“实现方式不同”，不是“没有 source identity”。

[1] 当前 Angelscript 与 Hazelight 都让 `UASFunction` 自带 file / line，但当前 AS 额外做了 1-based 行号归一化：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp
// 函数: UASClass::GetSourceFilePath / UASFunction::GetSourceFilePath / GetSourceLineNumber
// 位置: 1497-1559
// 说明: 当前 AS 把 source identity 挂在脚本类型/函数 metadata 上
// ============================================================================
FString UASClass::GetSourceFilePath() const
{
	return Module->Code[0].AbsoluteFilename; // ★ 文件路径不是调试器临时推导，而是 metadata 字段
}

FString UASFunction::GetSourceFilePath() const
{
	return Module->Code[0].AbsoluteFilename;
}

int UASFunction::GetSourceLineNumber() const
{
	return (scriptData->declaredAt & 0xFFFFF) + 1; // ★ 当前 AS 明确转成 1-based 行号
}

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp
// 函数: NavigateToFunction
// 位置: 34-44
// 说明: editor 导航直接消费同一份 metadata
// ============================================================================
FString Path = ASFunc->GetSourceFilePath();
OpenFile(Path, ASFunc->GetSourceLineNumber()); // ★ editor 不需要额外 path map

// ============================================================================
// 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/ClassGenerator/ASClass.cpp
// 函数: UASFunction::GetSourceLineNumber
// 位置: 1524-1534
// 说明: Hazelight 同路线，但行号仍更像原始内部值
// ============================================================================
int UASFunction::GetSourceLineNumber() const
{
	return scriptData->declaredAt & 0xFFFFF; // ★ 未做 +1 归一化
}
```

[2] UnrealCSharp、UnLua、puerts、sluaunreal 分别把 source identity 放在映射文件、VM frame、inspector url 与 profiler frame 上：

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp
// 位置: 34-36, 63-68, 205-212
// 说明: UnrealCSharp 的 source identity owner 是 CodeAnalysis json
// ============================================================================
SetCodeAnalysisOverrideFilesMap();
FDynamicGenerator::SetCodeAnalysisDynamicFilesMap();
...
if (const auto OverrideFile = GetOverrideFile(); !OverrideFile.IsEmpty())
{
	FSourceCodeNavigation::OpenSourceFile(OverrideFile); // ★ 打开的是 override map 指向的文件
}
...
CodeAnalysisOverrideFilesMap = FUnrealCSharpFunctionLibrary::LoadFileToString(FString::Printf(TEXT(
	"%s/%s.json"
), *FUnrealCSharpFunctionLibrary::GetCodeAnalysisPath(), *OVERRIDE_FILE));

// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaDebugBase.cpp
// 位置: 614-685
// 说明: UnLua 的 source identity 只在 lua_Debug frame 生命周期内存在
// ============================================================================
if (!lua_getinfo(L, "nSlu", &ar))
{
	return false;
}
...
lua_getinfo(L, "nSl", &ar);
FString DisplayInfo = FString::Printf(TEXT("Source : %s, name : %s, Line : %d \n"),
	UTF8_TO_TCHAR(ar.source), UTF8_TO_TCHAR(ar.name), ar.currentline);

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp
// 位置: 4079-4096
// 文件: Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js
// 位置: 21-23, 67-89
// 说明: puerts 的 source identity owner 是 url/scriptId
// ============================================================================
if (ModuleLoader->Search(RequiringDir, ModuleName, OutPath, OutDebugPath))
{
	Result->Set(Context, 0, FV8Utils::ToV8String(Isolate, OutPath)).Check();
	Result->Set(Context, 1, FV8Utils::ToV8String(Isolate, OutDebugPath)).Check();
}
...
if (msg.method === "Debugger.scriptParsed") {
	parsedScript.set(msg.params.scriptId, msg.params.url);
	parsedScript.set(msg.params.url, msg.params.scriptId); // ★ scriptId <-> url 是 debugger/HMR 共同索引
}

// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaStatProfile.cpp
// 位置: 53-54, 311-316
// 说明: slua 把短路径和定义行挂到 profiler counter 名称，不是 editor metadata
// ============================================================================
lua_getinfo(L, "nSl", ar);
...
FString counterName = FString::Printf(TEXT("[Lua Trace]:%s [Script]:%s [Line]:%d"),
	UTF8_TO_TCHAR(functionName), UTF8_TO_TCHAR(ar->short_src), ar->linedefined);
```

#### 对比矩阵（本轮新增观察点）

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 持久化 file/line 挂在类型或函数 metadata | Full | None | None | None | None | Full |
| editor 导航直接消费该 source identity | Full | Full | None | None | None | Full |
| source identity 主要来自生成映射文件 | None | Full | None | None | None | None |
| source identity 主要来自 live VM frame | None | None | Full | None | Full | None |
| source identity 主要以 url/scriptId 形式存在 | None | None | None | Full | None | None |
| 行号或路径有显式归一化 / 回退处理 | Partial | Partial | Partial | Full | Partial | Full |

#### 小结与建议

- 当前 Angelscript 在这一观察点上的核心优势不是“能跳转源码”，而是 **editor navigation 与 debugger 共享同一份 metadata-owned source identity**。这条线应按 `P0` 保持，不建议退回到“调用栈临时拼 path”的路线。
- 最值得吸收的是 puerts 的路径回退与别名处理。`modular.js:134-137` 的 `Pak:` 回退、`hot_reload.js:73-76` 的 slash 归一化，适合补到当前 AS 的远程调试 / cooked source 映射中，优先级 `P1`。
- UnrealCSharp 的 override map 值得作为 **metadata 缺失时的次级 fallback**，但不应替代当前 AS 的 metadata primary owner。否则会把 runtime / editor / debugger 三条线拆开，优先级 `P1`。

### [D6] unsupported symbol 的账本：reasonful manifest / bool cache / positive-only registry / ad-hoc runtime error

#### 各插件实现概览

```
[D6-NegativeSpace] Unsupported Symbol Ledger
HZ      : 本轮未定位到 in-tree reason ledger / summary layer
AS(now) : failureReason -> skipped entries csv -> reason summary csv -> runtime SkipReason
UC      : SupportedMap<bool> cache
UL      : exported registries + blacklist + continue
PU      : GenFunction()==false -> continue; rare @deprecated note
SL      : local luaL_error("Unsupported type ...")
```

前面两轮 `D6` 分别看过“artifact authority”和“freshness contract”；这一轮补的是 **negative space 是否被显式记账**。也就是“哪些符号没生成、为什么没生成、谁能看到这件事”。

#### 详细对比

##### 子维度 1：unsupported / skipped 是不是一等产物

- 当前 Angelscript 在这条线上最完整。`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs:18-105` 会返回 `header-missing`、`class-range`、`declaration-missing`、`non-public`、`unexported-symbol`、`overloaded-unresolved`；`AngelscriptFunctionTableExporter.cs:27-45, 65-88, 99-161` 再把这些原因写进 `AS_FunctionTable_SkippedEntries.csv` 与 `AS_FunctionTable_SkippedReasonSummary.csv`；到了 runtime，`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp:847-871` 的 `BindRegistrations.csv` 继续保留 `SkipReason` 列。这里不是“偶尔打一条 log”，而是 **build-time 与 runtime 都有账本**。
- UnrealCSharp 有 pruning，但没有 reason ledger。`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:812-875` 反复 `SupportedMap.Add(..., false/true)`；一旦父类、接口、参数类型不支持，就缓存 `false` 返回。它能快速回答“支不支持”，但回答不了“为什么不支持”。
- UnLua 更像 positive-only registry。`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp:55-99` 只遍历 `ExportedReflectedClasses / ExportedEnums / ExportedFunctions`，黑名单直接 `continue`。这条线不是没有筛选，而是**没被导出的东西不会进入账本**。
- puerts 介于两者之间。`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1162-1169` 遇到 `!GenFunction(...)` 直接 `continue`；但 `...:1219-1224` 又会把 unsupported super overload 降级成 `@deprecated Unsupported super overloads.`。这说明它并非完全沉默，但 ledger 非常局部。
- sluaunreal 更接近 ad-hoc runtime error。`Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaArray.cpp:300-314` 在不支持的容器元素类型上直接 `luaL_error("Unsupported type[%d] of LuaArray!", type)`。这里有错误信息，但没有跨文件、跨模块可汇总的账本。
- Hazelight 在本轮可见快照里未定位到与当前 AS 对等的 reason ledger。对 `J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript` 以 `SkipReason / AS_FunctionTable_Summary / coverage diagnostics / unexported-symbol` 为关键词做仓级检索均未命中，因此此处应暂判 `None`。这表示**当前快照里没有集中账本层**，不等于 Hazelight runtime 从不跳过任何符号。

##### 子维度 2：negative space 谁能看到

- 当前 Angelscript 把 negative space 同时暴露给三类读者。UHT exporter 产出 `SkippedEntries.csv` 给构建作者；`SkippedReasonSummary.csv` 与 `WriteCoverageDiagnostics()` 给模块级 coverage 视图；runtime `StateDump` 则让运行中 bind 状态也能对齐 `SkipReason`。这已经接近“可审计资产”。
- UnrealCSharp 的 `SupportedMap<bool>` 更像 generator 内部缓存。它很适合剪枝和避免重复判断，但如果用户问“某个 API 为什么没进生成结果”，仓内没有同级别的显式 answer file。
- UnLua / puerts 都更依赖“从缺席反推失败”。UnLua 的 `.lua` IntelliSense 里没有某个类或函数时，开发者需要自己回头查 export registry；puerts 也主要是从 `continue` 后缺失的声明反推 unsupported。
- slua 的错误面最晚，通常要到具体运行路径打到 `luaL_error` 才会暴露 unsupported。它不是“做得差”，而是 owner 明确落在 runtime site，而不是 build artifact。

[1] 当前 Angelscript 把 skipped reason 当成正式产物，而不是布尔缓存：

```
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs
// 函数: TryBuild
// 位置: 18-105
// 说明: 生成前会给出明确 failureReason
// ============================================================================
if (classObj.HeaderFile == null || string.IsNullOrEmpty(classObj.HeaderFile.FilePath) || !File.Exists(classObj.HeaderFile.FilePath))
{
	failureReason = "header-missing";
	return false;
}
...
if (candidates.Count == 0)
{
	failureReason = "declaration-missing";
	return false;
}
...
if (publicCandidates.Count == 0)
{
	failureReason = "non-public";
	return false;
}
...
failureReason = matchedUnexportedSymbol ? "unexported-symbol" : "overloaded-unresolved";
return false;

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs
// 位置: 27-45, 83-87, 99-161
// 说明: skipped entry 与 reason summary 都会落盘
// ============================================================================
List<AngelscriptSkippedFunctionEntry> skippedEntries = new();
...
WriteSkippedEntriesCsv(factory, skippedEntries);
WriteSkippedReasonSummaryCsv(factory, skippedEntries); // ★ 不仅记单条，还按 reason 聚合
...
skippedEntries.Add(new AngelscriptSkippedFunctionEntry(
	moduleName,
	classObj.SourceName,
	function.SourceName,
	string.IsNullOrEmpty(failureReason) ? "unknown" : failureReason));
...
builder.AppendLine("FailureReason,SkippedCount");
foreach (var reasonGroup in skippedEntries.GroupBy(static entry => entry.FailureReason, StringComparer.Ordinal))
{
	builder.Append(reasonGroup.Key).Append(',').Append(reasonGroup.Count()).Append("\r\n");
}

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp
// 函数: DumpBindRegistrations
// 位置: 847-871
// 说明: runtime dump 继续保留 SkipReason 列
// ============================================================================
Writer.AddHeader({
	TEXT("BindName"),
	TEXT("BindModule"),
	TEXT("bIsSkipped"),
	TEXT("SkipReason") // ★ 负空间账本延续到运行态
});
```

[2] 其他插件大多只有布尔缓存、正向 registry、局部 stub 或运行时错误：

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp
// 位置: 812-875
// 说明: UnrealCSharp 只缓存 supported bool，不记录 reason
// ============================================================================
if (!IsSupported(InClass->GetPackage()))
{
	SupportedMap.Add(InClass, false);
	return false;
}
...
if (!IsSupported(*ParamIterator))
{
	SupportedMap.Add(InFunction, false);
	return false; // ★ 只有 yes/no，没有 why
}

// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp
// 位置: 55-99
// 说明: UnLua 只遍历 exported registry，缺席项不会进入账本
// ============================================================================
const auto ExportedReflectedClasses = UnLua::GetExportedReflectedClasses();
...
if (ClassBlackList.Contains(Pair.Key))
	continue;
...
if (FuncBlackList.Contains(Function->GetName()))
	continue; // ★ 跳过发生了，但没有集中 reason ledger

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp
// 位置: 1162-1169, 1219-1224
// 说明: puerts 大多 silent continue，少量 unsupported 会降级成 deprecated stub
// ============================================================================
if (!GenFunction(Tmp, Function, true, false, false, true))
{
	continue; // ★ 大多数 unsupported 直接从生成结果里消失
}
...
Buff << "    /**\n";
Buff << "     * @deprecated Unsupported super overloads.\n";
Buff << "     */\n"; // ★ 只有这一类情况被显式留痕

// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaArray.cpp
// 位置: 300-314
// 说明: slua 的 unsupported 更像运行时局部错误
// ============================================================================
if (!prop) {
	luaL_error(L, "Unsupported type[%d] of LuaArray!", type); // ★ 错误存在，但没有中心化账本
}
```

#### 对比矩阵（本轮新增观察点）

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 按函数记录 failure reason | None | None | None | Partial | None | Full |
| 按 reason 聚合 summary / coverage | None | None | None | None | None | Full |
| runtime / state dump 继续保留 skip reason | None | None | None | None | None | Full |
| unsupported 主要以 bool cache 形式存在 | None | Full | None | None | None | None |
| unsupported 主要表现为 positive-only registry 或 silent continue | None | None | Full | Full | None | None |
| unsupported 主要到运行时局部错误才暴露 | None | None | None | None | Full | None |

#### 小结与建议

- 当前 Angelscript 在这一观察点上的稀缺能力，是 **negative space 本身就是 artifact**。这比“生成更多文件”更重要，建议按 `P0` 保持。
- 最值得继续加强的是把 UHT `failureReason` 与 runtime `SkipReason` 收敛成统一 taxonomy。当前两条线都已经有 reason 字段，但枚举尚未完全统一，优先级 `P1`。
- UnrealCSharp 的 `SupportedMap<bool>` 适合拿来做前置剪枝，但不应替代当前 AS 的 reasonful ledger；puerts 的 `deprecated stub` 则提示了另一条可吸收路线：对“部分不可用但可说明”的符号，优先在 IDE 产物里显式留痕，优先级 `P1`。

### [D8] performance observability surface：script-visible scope / VM-owned auto tracing / editor profiler / third-party-only profiler

#### 各插件实现概览

```
[D8-Observability] Performance Surface Owner
HZ      : script-visible RAII scope -> UE stats / CPU trace
AS(now) : script-visible RAII scope + automation metrics.json artifact
UC      : Mono profiler callback -> UE trace event
UL      : build/editor setting -> lua hook + UFunction trace -> Unreal Insights
PU      : Wasm3 op profiler / inspector side path; no main JS UE trace found in this round
SL      : Lua hook -> profiler tree + editor profiler tab; override call also has STAT scope
```

前面 `D8` 已经看过“优化 authority 在哪一层发生”；这一轮补的是 **性能可观测面到底由谁拥有**。也就是脚本作者自己能不能插桩、VM 会不会自动打点、结果是在 Unreal trace 里、editor profiler 里，还是只留在局部第三方 runtime。

#### 详细对比

##### 子维度 1：谁能主动发出 performance signal

- 当前 Angelscript 与 Hazelight 都把 signal surface 暴露给脚本作者。`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Stats.cpp:33-70` 与 `.../Bind_FCpuProfilerTraceScoped.cpp:4-13`，以及 Hazelight 对应 `J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_Stats.cpp:33-70`、`.../Bind_FCpuProfilerTraceScoped.cpp:4-13` 说明脚本侧能直接构造 `FScopeCycleCounter` 和 `FCpuProfilerTraceScoped`。这不是 runtime 偷偷打点，而是**脚本作者显式创建 scope**。
- UnrealCSharp 的 owner 是 VM。`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp:114-120` 初始化 domain 时 `RegisterProfiler()`；`FMonoProfiler.cpp:7-27, 30-52` 再把 Mono method enter/leave 回调接到 `FCpuProfilerTrace::OutputBeginDynamicEvent/OutputEndEvent`。C# 代码不用手写 scope，对应代价是信号命名和粒度更多由 Mono profiler 决定。
- UnLua 的 owner 介于配置与 VM 之间。`Reference/UnLua/Plugins/UnLua/Source/UnLua/UnLua.Build.cs:91-96` 通过 `ENABLE_UNREAL_INSIGHTS` 编译开关启用；`UnLuaEditorSettings.h:61-63` 又把它暴露为 editor setting；`LuaEnv.cpp:42-69` 用 `lua_getinfo("nSl")` 自动给 Lua 函数打 trace；`FunctionDesc.cpp:78-82` 还对 `UFunction -> Lua` bridge 做了 `TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*FuncName)`。因此它是 **host-owned auto tracing**，不是 script-authored explicit scopes。
- sluaunreal 的 owner 更偏 editor profiler。`Reference/sluaunreal/Plugins/slua_unreal/Source/slua_profile/Private/slua_profile.cpp:70-83, 205-216` 注册了 profiler tab，并把 `WatchBegin / WatchEnd` 刷到 profiler 树；`LuaOverrider.cpp:131-135` 也会为 override call 建 `FScopeCycleCounter`。这说明它不是完全没有接入 UE 统计，但主要观察面仍是 slua 自己的 profiler UI。
- puerts 在这份快照里没有形成统一的主脚本观测面。仓级检索 `FCpuProfilerTrace / TRACE_CPUPROFILER / OutputBeginDynamicEvent` 在 `Reference/puerts/unreal/Puerts` 下未命中；当前可见的 profiler 主要是 `WasmCore` 第三方库。`m3_exec.h:48-56` 只有在 `d_m3EnableOpProfiling` 时才走 `profileOp`，`m3_info.c:502-516` 维护 `s_opProfilerCounts`，`m3_env.c:248-253` 在 runtime 释放时 `m3_PrintProfilerInfo()`。这应判为“观测面存在，但主要局限于子运行时”，不是“完全没有性能信息”。

##### 子维度 2：结果是 live trace，还是结构化 artifact

- 当前 Angelscript 比 Hazelight 多出的关键层，是**结构化 artifact**。`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEnginePerformanceTests.cpp:87-97` 会写 `startup.total_seconds` 等指标；`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptPerformanceTestUtils.h:40-78` 统一输出 `metrics.json`。这意味着它不仅能在 trace 中看，还能被自动化基线消费。
- Hazelight 本轮只确认了 script-visible scope 这层；对 `J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript` 以 `metrics.json / WritePerformanceMetricsArtifact / startup.total_seconds` 做仓级检索未命中。因此这里应判为“实现质量差异”，不是“路线不同”。
- UnrealCSharp 与 UnLua 都强在 live trace / Insights 接入，但本轮没有定位到对等的结构化性能 artifact 生成层。它们更像“直接进 profiler”，不是“自动化落盘”。
- sluaunreal 强在 editor session 内的 profiler 树，而不是测试产物。
- puerts 当前快照里的 profiler 输出主要在 Wasm runtime release 时打印，没有统一落到 Unreal 自动化 artifact。

[1] 当前 Angelscript / Hazelight 都把 profiling scope 暴露给脚本，但只有当前 AS 继续把结果写成 `metrics.json`：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Stats.cpp
// 位置: 33-70
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FCpuProfilerTraceScoped.cpp
// 位置: 4-13
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/FCpuProfilerTraceScoped.h
// 位置: 14-23
// 说明: 当前 AS 的 performance surface 可以被脚本显式创建
// ============================================================================
struct FScriptScopeCycleCounter
{
	FScopeCycleCounter Counter;
	...
};
...
auto FScopeCycleCounter_ = FAngelscriptBinds::ValueClass<FScriptScopeCycleCounter>("FScopeCycleCounter", CounterFlags);
...
FCpuProfilerTraceScoped_.Constructor("void f(const FName& EventID)", [](FCpuProfilerTraceScoped* Address, const FName& EventID)
{
	new(Address) FCpuProfilerTraceScoped(EventID); // ★ 脚本可显式创建 CPU trace scope
});
...
FCpuProfilerTraceScoped(const FName& EventID)
{
	FCpuProfilerTrace::OutputBeginDynamicEvent(EventID);
}
~FCpuProfilerTraceScoped()
{
	FCpuProfilerTrace::OutputEndEvent();
}

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEnginePerformanceTests.cpp
// 位置: 87-97
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptPerformanceTestUtils.h
// 位置: 40-78
// 说明: 当前 AS 额外把性能结果落成结构化 artifact
// ============================================================================
LogPerformanceMetric(TEXT("startup.total_seconds"), StartupTotals);
...
const FString MetricsPath = WritePerformanceMetricsArtifact(RunId, TestGroup, Metrics, Notes);
Test.TestTrue(TEXT("Startup performance test should write a metrics.json artifact"),
	FPlatformFileManager::Get().GetPlatformFile().FileExists(*MetricsPath));
...
RootObject->SetStringField(TEXT("run_id"), RunId);
RootObject->SetStringField(TEXT("test_group"), TestGroup);
RootObject->SetArrayField(TEXT("metrics"), MetricValues);
const FString MetricsPath = FPaths::Combine(MetricsDirectory, TEXT("metrics.json"));

// ============================================================================
// 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_Stats.cpp
// 位置: 33-70
// 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_FCpuProfilerTraceScoped.cpp
// 位置: 4-13
// 说明: Hazelight 与当前 AS 在 script-visible scope 上同构
// ============================================================================
auto FScopeCycleCounter_ = FAngelscriptBinds::ValueClass<FScriptScopeCycleCounter>("FScopeCycleCounter", CounterFlags);
...
FCpuProfilerTraceScoped_.Constructor("void f(const FName& EventID)", [](FCpuProfilerTraceScoped* Address, const FName& EventID)
{
	new(Address) FCpuProfilerTraceScoped(EventID);
});
```

[2] UnrealCSharp、UnLua、puerts、sluaunreal 分别代表 VM-owned trace、host-owned Insights、third-party profiler、editor profiler：

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp
// 位置: 114-120
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoProfiler.cpp
// 位置: 7-27, 41-52
// 说明: UnrealCSharp 由 Mono profiler 自动把 C# 方法映射成 UE trace event
// ============================================================================
mono_debug_init(MONO_DEBUG_FORMAT_MONO);
Domain = mono_jit_init("UnrealCSharp");
mono_domain_set(Domain, false);
RegisterProfiler(); // ★ VM 初始化时注册 profiler
...
const auto EventName = FString::Printf(TEXT("[C#] %s.%s.%s"),
	*MethodNamespace, *ClassName, *MethodName);
FCpuProfilerTrace::OutputBeginDynamicEvent(*EventName);
...
FCpuProfilerTrace::OutputEndEvent();

// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/UnLua.Build.cs
// 位置: 91-96
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorSettings.h
// 位置: 61-63
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp
// 位置: 42-69
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp
// 位置: 78-82
// 说明: UnLua 通过 build flag + editor setting 打开自动 trace
// ============================================================================
loadBoolConfig("bEnableUnrealInsights", "ENABLE_UNREAL_INSIGHTS", false);
...
bool bEnableUnrealInsights = false; // ★ 用户通过 editor setting 决定是否启用
...
lua_getinfo(L, "nSl", ar);
...
FCpuProfilerTrace::OutputBeginDynamicEvent(*EventName);
...
TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*FuncName); // ★ bridge 调用也进入 Insights

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/WasmCore/ThirdPart/wasm3/m3_exec.h
// 位置: 48-56
// 文件: Reference/puerts/unreal/Puerts/Source/WasmCore/ThirdPart/wasm3/m3_info.c
// 位置: 502-516
// 文件: Reference/puerts/unreal/Puerts/Source/WasmCore/ThirdPart/wasm3/m3_env.c
// 位置: 248-253
// 说明: 当前可见 profiler 主要局限于 Wasm3 子运行时
// ============================================================================
# if d_m3EnableOpProfiling
	d_m3RetSig  profileOp(d_m3OpSig, cstr_t i_operationName);
#   define nextOp() M3_MUSTTAIL return profileOp(d_m3OpAllArgs, __FUNCTION__)
# endif
...
static M3ProfilerSlot s_opProfilerCounts[d_m3ProfilerSlotMask + 1] = {};
...
m3_PrintProfilerInfo(); // ★ 释放 Wasm runtime 时打印 profiler 信息

// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_profile/Private/slua_profile.cpp
// 位置: 70-83, 205-216
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaOverrider.cpp
// 位置: 131-135
// 说明: slua 的主观察面在 editor profiler UI，override call 额外进 STAT
// ============================================================================
sluaProfilerInspector = MakeShareable(new SProfilerInspector);
FGlobalTabmanager::Get()->RegisterNomadTabSpawner(slua_profileTabName, ...);
...
SluaProfilerDataManager::WatchBegin(short_src, linedefined, name, nanoseconds, funcProfilerRoot, profilerStack);
...
const TStatId StatId = FDynamicStats::CreateStatId<STAT_GROUP_TO_FStatGroup(STATGROUP_Lua)>(statName);
FScopeCycleCounter CycleCounter(StatId);
```

#### 对比矩阵（本轮新增观察点）

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 脚本作者可显式创建 profiling scope / counter | Full | None | None | None | Partial | Full |
| 运行时会自动为脚本调用打点 | None | Full | Full | None | Full | None |
| 编辑器内建 profiler 面板 / tab | None | None | None | None | Full | None |
| 自动化会写结构化性能 artifact | None | None | None | None | None | Full |
| 观测面主要局限于子运行时或局部路径 | None | None | None | Full | Partial | None |

#### 小结与建议

- 当前 Angelscript 在这个观察点上的独特优势，是 **script-visible scope + 可落盘 metrics artifact** 两层同时存在。前者适合临场定位，后者适合自动化基线，建议按 `P0` 保持。
- 最值得吸收的是 UnrealCSharp / UnLua 的 auto tracing 思路。当前 AS 现有 surface 更偏“作者自己插桩”；如果要继续增强开发体验，建议补一层可开关的 runtime-level 自动 trace，把常见 script entrypoint 自动接入 Unreal Insights，优先级 `P1`。
- slua 的 editor profiler tree 证明“会话内热点树”仍有价值；如果当前 AS 后续补 editor profiler，不应替代现有 trace / artifact，而应作为第三层观察面。puerts 这份快照则提醒另一面：不要让 profiler 长期只停留在第三方子运行时里，优先级 `P1`。

---

## 深化分析 (2026-04-08 20:38:43)

这一轮不再重复前文已经展开过的 `transport/bootstrap/source identity`、`commandlet exit code`、`test layering` 等主题，只补两个还没被单独横向钉死的观察点：

- `D5`：调试器真正拿到的变量，到底是**可寻址监视实体**、**可读快照树**，还是完全让位给外部 VM / Inspector。
- `D9`：测试真正的 operator surface 在哪里，回归 case 装在什么载体里，产物能不能直接喂给 CI / Agent。

### [D5] 变量物化模型：addressable watch entity / snapshot tree / VM-owned remote object / profiler-only stream

#### 各插件实现概览

```
[D5-ValueModel] Debug Value Materialization
HZ      : Variables/Evaluate -> ValueAddress + ValueSize -> DataBreakpoint
AS(now) : Variables/Evaluate -> ValueAddress + ValueSize -> DataBreakpoint
UL      : FLuaDebugValue -> ReadableValue + Keys/Values (depth<=4)
UC      : Mono debugger-agent -> value model owned by Mono
PU      : V8InspectorSession -> remote object/property model owned by Inspector
SL      : profiler TCP stream + editor tab; no peer variable protocol found
```

前文已经比较过 `D5` 的协议 owner、bootstrap 和 source identity；这一轮只看**变量本体**。同样叫“查看变量”，Hazelight / 当前 Angelscript 返回的是带地址的协议实体，UnLua 返回的是带深度限制的可读树，UnrealCSharp / puerts 则把变量模型交回 Mono / V8，slua 当前快照里仍只看到 profiler 线路。

#### 详细对比

##### 子维度 1：变量 identity 是“地址”，还是“快照描述”

- 当前 Angelscript 的 `FAngelscriptVariable` 不是单纯展示字符串。`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h:416-442` 直接把 `ValueAddress` 和 `ValueSize` 放进消息体；`.../AngelscriptDebugServer.cpp:1081-1128` 在 `RequestVariables` / `RequestEvaluate` 里用 `GetAddressToMonitor()` 与 `GetAddressToMonitorValueSize()` 回填地址。这意味着协议拿到的是**可继续监视的实体**，不是一次性文本。
- Hazelight 对应 `J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Debugging/AngelscriptDebugServer.h:339-352` 与 `.../AngelscriptDebugServer.cpp:981-1019` 基本同构。这里应判为“血缘延续”，不是“当前 AS 新造了另一套变量模型”。
- UnLua 的 `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDebugBase.h:21-62` 与 `.../Private/UnLuaDebugBase.cpp:43-154,614-669` 则把调试值定义成 `ReadableValue + Keys + Values`。`GetStackVariables()` 先枚举 `lua_getlocal / lua_getupvalue`，再把每个值递归 `Build()` 成树。它的优势是**一眼可读**；代价是这个树没有协议级地址身份。
- UnrealCSharp 没在插件内定义对等的变量 envelope。`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp:93-120` 只负责打开 `--debugger-agent=transport=dt_socket` 和 `mono_debug_init()`；变量怎么被物化，owner 在 Mono debugger，不在插件自定义协议里。
- puerts 同样把这层 owner 交给 Inspector。`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:93-116` 直接 `connect()` 成 `V8InspectorSession`，`...:291-333` 再注册 websocket inspector server。也就是说，变量对象首先是 V8 Inspector protocol 的 remote object，不是插件自定义 `ValueAddress` 消息。
- sluaunreal 在当前快照里未定位到对等 source-level 变量协议。`Reference/sluaunreal/Plugins/slua_unreal/Source/slua_profile/Private/slua_remote_profile.cpp:22-60` 只起 `slua.ProfilerPort` 的 `FTcpListener`；`.../slua_profile.cpp:70-83` 只注册 profiler tab。这里应判 `None`，而不是把 profiler transport 误写成 debugger variable model。

##### 子维度 2：这个模型实际解锁什么能力

- 当前 Angelscript / Hazelight 的直接收益是 data breakpoint。因为 `Variables/Evaluate` 已经带地址，`SetDataBreakpoints / ClearDataBreakpoints` 才有稳定目标。这里不是“协议多两个字段”而已，而是**变量查看和内存监视本来就是一条链**。
- UnLua 的直接收益是复杂容器可读性。`BuildFromUStruct / BuildFromTArray / BuildFromTMap / BuildFromTSet` 让 table、userdata、UE 容器都能变成人类可读的树；但 `MAX_LUA_VALUE_DEPTH = 4` 也明确说明它优先的是“可控展开”，不是“无限深追踪”。
- UnrealCSharp / puerts 的收益是直接吃成熟 IDE / DevTools 生态。这里不是没有变量能力，而是**变量能力不由插件仓库 own**。这会降低自定义协议负担，但也意味着插件自己很难像当前 AS 一样，把“变量地址”进一步接到 data breakpoint 或 UE 侧特定观测面。

##### 子维度 3：对当前 Angelscript 的真实启发

- 当前 Angelscript 在这一观察点上的强项，不只是“比 UnLua 多个地址字段”，而是**把 watch / evaluate / data breakpoint 统一到同一个变量实体模型**。这条线应按 `P0` 保持。
- UnLua 提醒了另一条值得吸收的增强：当前 AS 的变量已经可寻址，但人类可读性仍主要靠 `Value.Value` 字符串。后续完全可以在不放弃地址模型的前提下，再叠一层类似 `ReadableValue + child preview` 的结构化摘要，优先级 `P1`。
- UnrealCSharp / puerts 证明“把变量模型交给标准 VM debugger”也能形成强工具链，但那适合已经接受 Mono / V8 生态的语言。对当前 AS 来说，更合理的是**保留 UE-owned address model，再补 adapter**，而不是反向放弃自有变量协议，优先级 `P2`。

[1] 当前 Angelscript / Hazelight 的变量协议都把地址和大小视为一等字段，`RequestVariables` / `RequestEvaluate` 直接填充 monitor address：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h
// 位置: 416-442
// 说明: 当前 AS 的变量消息不止是显示字符串，还带可继续监视的地址与大小
// ============================================================================
struct FAngelscriptVariable : FDebugMessage
{
	FString Name;
	FString Value;
	FString Type;
	uint64 ValueAddress;
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
			Ar << Msg.ValueAddress; // ★ 变量 identity 是地址，不只是文字
			Ar << Msg.ValueSize;
		}
		return Ar;
	}
};

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 位置: 1081-1128
// 说明: variables / evaluate 路径都会回填 monitor address
// ============================================================================
else if (MessageType == EDebugMessageType::RequestVariables)
{
	...
	Var.ValueAddress = reinterpret_cast<uint64>(Value.GetAddressToMonitor());
	Var.ValueSize = Value.GetAddressToMonitorValueSize();
	...
	SendMessageToClient(Client, EDebugMessageType::Variables, Vars);
}
else if (MessageType == EDebugMessageType::RequestEvaluate)
{
	...
	Var.ValueAddress = reinterpret_cast<uint64>(Value.GetAddressToMonitor());
	Var.ValueSize = Value.GetAddressToMonitorValueSize();
	SendMessageToClient(Client, EDebugMessageType::Evaluate, Var);
}

// ============================================================================
// 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Debugging/AngelscriptDebugServer.h
// 位置: 339-352
// 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Debugging/AngelscriptDebugServer.cpp
// 位置: 981-1019
// 说明: Hazelight 在变量地址模型上与当前 AS 同一家族
// ============================================================================
struct FAngelscriptVariable : FDebugMessage
{
	FString Name;
	FString Value;
	FString Type;
	uint64 ValueAddress;
	uint8 ValueSize;
	...
};
...
Var.ValueAddress = reinterpret_cast<uint64>(Value.GetAddressToMonitor());
Var.ValueSize = Value.GetAddressToMonitorValueSize();
```

[2] UnLua 的调试值是深度受限的快照树，优先保证“看懂值”，而不是“继续监视这块内存”：

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDebugBase.h
// 位置: 21-62
// 说明: 调试值本体是 ReadableValue + Keys/Values 树，并且仓内写死最大展开深度
// ============================================================================
#define MAX_LUA_VALUE_DEPTH 4

struct UNLUA_API FLuaDebugValue
{
	void Build(lua_State *L, int32 Index, int32 Level = MAX_int32);
	FString ReadableValue;
	FString Type;
	int32 Depth;
	bool bAlreadyBuilt;
	TArray<FLuaDebugValue> Keys;
	TArray<FLuaDebugValue> Values; // ★ children 直接挂在值对象上
	...
};

// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaDebugBase.cpp
// 位置: 43-154, 614-669
// 说明: locals / upvalues 会被递归 Build 成可读快照树，但深度被截断
// ============================================================================
void FLuaDebugValue::Build(lua_State *L, int32 Index, int32 Level)
{
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
				Value->Build(L, -1, Level - 1);
			}
			...
		}
		ReadableValue = FString::Printf(TEXT("table(size=%d): 0x%p"), TableSize, TableAddress);
	}
	...
}

FLuaDebugValue* FLuaDebugValue::AddKey()
{
	if (Depth >= MAX_LUA_VALUE_DEPTH)
	{
		return nullptr; // ★ 递归深度被硬限制
	}
	...
}

bool GetStackVariables(lua_State *L, int32 StackLevel, TArray<FLuaVariable> &LocalVariables, TArray<FLuaVariable> &Upvalues, int32 Level)
{
	...
	const char *VarName = lua_getlocal(L, &ar, i);
	...
	Variable.Value.Depth = 0;
	Variable.Value.Build(L, -1, Level); // ★ local / upvalue 都被物化成快照树
	...
}
```

[3] UnrealCSharp、puerts、sluaunreal 在这个观察点上分别代表“VM-owned debugger model”“Inspector-owned remote object”“profiler-only stream”：

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp
// 位置: 93-120
// 说明: UnrealCSharp 只负责打开 Mono debugger-agent，变量模型 owner 不在插件协议里
// ============================================================================
if (UnrealCSharpSetting->IsEnableDebug())
{
	const auto Config = FString::Printf(TEXT(
		"--debugger-agent=transport=dt_socket,server=y,suspend=n,address=%s:%d"
	), *UnrealCSharpSetting->GetHost(), UnrealCSharpSetting->GetPort());
	...
	mono_jit_parse_options(sizeof(Options) / sizeof(char*), Options);
}
mono_debug_init(MONO_DEBUG_FORMAT_MONO); // ★ 仓内入口是 Mono debugger substrate

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp
// 位置: 93-116, 291-333
// 说明: puerts 直接连到 V8 Inspector session；值对象属于 Inspector protocol
// ============================================================================
V8InspectorSession = InV8Inspector->connect(InCxtGroupID, this, DummyState, v8_inspector::V8Inspector::kFullyTrusted);
...
V8InspectorSession->dispatchProtocolMessage(StringView); // ★ 插件转发 protocol message，本身不定义变量 envelope
...
V8Inspector = v8_inspector::V8Inspector::create(Isolate, this);
V8Inspector->contextCreated(v8_inspector::V8ContextInfo(InContext, CtxGroupID, CtxName));
...
Server.listen(Port);
Server.start_accept(); // ★ attach 点是 websocket inspector

// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_profile/Private/slua_remote_profile.cpp
// 位置: 22-60
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_profile/Private/slua_profile.cpp
// 位置: 70-83
// 说明: 当前快照里 slua 暴露的是 profiler port 与 editor tab，不是 source-level variable protocol
// ============================================================================
FAutoConsoleVariableRef CVarSluaProfilerPort(
	TEXT("slua.ProfilerPort"),
	NS_SLUA::FProfileServer::Port,
	TEXT("Slua profiler server port.\n"),
	ECVF_Default);
...
Listener = new FTcpListener(ListenEndpoint); // ★ 远程链路首先服务 profiler
...
sluaProfilerInspector = MakeShareable(new SProfilerInspector);
FGlobalTabmanager::Get()->RegisterNomadTabSpawner(slua_profileTabName, ...);
```

#### 对比矩阵（本轮新增观察点）

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 插件协议变量自带 `ValueAddress / ValueSize` | Full | None | None | None | None | Full |
| `RequestVariables / Evaluate` 直接回填 monitor address | Full | None | None | None | None | Full |
| 调试值内建 `Keys / Values` 树形 children | None | None | Full | None | None | None |
| 仓内显式限制变量展开深度 | None | None | Full | None | None | None |
| 变量模型 owner 主要在外部 VM / Inspector | None | Full | None | Full | None | None |
| 当前快照仅定位到 profiler 远程链路 | None | None | None | None | Full | None |

#### 小结与建议

- 当前 Angelscript 在这个观察点上的关键优势，是 **变量查看、表达式求值、data breakpoint 共用同一套 addressable entity model**。这不是“比别人多两个字段”，而是功能链条更完整，建议按 `P0` 保持。
- 最值得吸收的是 UnLua 的**可读快照层**。当前 AS 不应该放弃 `ValueAddress`，但完全可以在地址模型之上增加 `ReadablePreview` / `ChildrenSummary` 一类结构，让 watch window 更容易读，优先级 `P1`。
- UnrealCSharp / puerts 提醒的不是“要不要改成标准 VM debugger”，而是 adapter 价值。当前 AS 若要继续改善 IDE 接入，应该补标准协议适配层，而不是拆掉自有变量实体，优先级 `P2`。

### [D9] 测试 operator contract：repo-owned runner / project-owned test plugin / commandlet-only batch / no explicit runner

#### 各插件实现概览

```
[D9-Operator] Test Entry And Artifact Ownership
HZ      : plugin commandlet -> exit code 1/2/3
AS(now) : RunTests.ps1 -> per-run artifacts -> suite fanout -> commandlet/Automation
UL      : TPSProject + UnLuaTestSuite plugin -> Spec + Issue map regression
UC      : cook-aware/editor-aware code exists; no peer test runner located in this round
PU      : commandlet-aware modules exist; no peer test runner located in this round
SL      : demo/tool config exists; no peer test runner located in this round
```

前文 `D9` 已经写过 test layering、脚本 discover 和 commandlet batch contract；这一轮补的是**operator 真正操作的入口**。也就是：谁定义“标准跑法”、谁负责超时/产物/隔离目录、回归 case 主要装在内存脚本里还是地图资产里。

#### 详细对比

##### 子维度 1：官方入口是仓库 runner、插件 commandlet，还是样例工程

- 当前 Angelscript 的 owner 已经上提到仓库脚本层。`Tools/RunTests.ps1:45-111` 会统一解析 `TimeoutMs`、预热 `TargetInfo`、申请 worktree mutex，并强制把 `-ABSLOG`、`-ReportExportPath`、默认 `-NullRHI` 拼进 `UnrealEditor-Cmd.exe` 参数；`Tools/RunTestSuite.ps1:41-148` 又把 `Smoke / NativeCore / RuntimeCpp / Debugger / ScenarioSamples` 固化成官方 suite。`Documents/Guides/Test.md:5-11,120-131` 甚至把“只能走这两个入口”写成仓库规则。这说明测试入口 owner 已经不是 UE Automation 默认命令，而是 repo-owned runner。
- Hazelight 仍停在插件 commandlet 边界。`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/AngelscriptTestCommandlet.cpp:5-25` 定义了 `0/1/2/3` 退出码；当前 AS 对应 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptTestCommandlet.cpp:5-25` 保留了同一批语义，但现在上面又叠了一层 repo runner。这里应判为“当前 AS 在同血缘上新增 operator 层”，不是“Haze 只有 commandlet 所以更弱”。
- UnLua 的入口 owner 在**样例工程 + opt-in test plugin**。`Reference/UnLua/TPSProject.uproject:16-33` 明确启用了 `RuntimeTests`、`EditorTests`、`FunctionalTestingEditor`、`UnLuaTestSuite`；`Reference/UnLua/Plugins/UnLuaTestSuite/UnLuaTestSuite.uplugin:24-30` 把 `UnLuaTestSuite` 声明成独立 runtime module；`.../UnLuaTestCommon.cpp:60-97` 则在 `SetUp()` 里手动 `UnLua::Startup()`、创建 `UGameInstance`、决定是临时 world 还是 `AutomationOpenMap(MapName)`。这条线的重点不是 runner，而是“带着测试工程一起分发”。
- 对 `Reference/UnrealCSharp`、`Reference/puerts`、`Reference/sluaunreal` 以 `RunTests.ps1`、`RunMetadata.json`、`BEGIN_DEFINE_SPEC`、`UnLuaTestSuite` 等关键词做本轮仓级检索，没有定位到与当前 AS repo runner 或 UnLua test plugin 对等的入口；当前命中更多是 `IsRunningCookCommandlet()` 或 `!IsRunningCommandlet()` 一类环境感知代码。因此这里应保守标为 `None`，并明确是“当前快照未定位到对等 operator surface”。

##### 子维度 2：回归 case 主要装在什么载体里

- 当前 Angelscript 的大量 case 更偏**内存脚本夹具**。`Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleTestSupport.cpp:16-59` 先 `AcquireCleanSharedCloneEngine()`，再把 `Example.ScriptText` 通过 `CompileAnnotatedModuleFromMemory()` 即时编译。这样做的好处是 case 轻、headless 友好、组合成本低。
- UnLua 的回归 case 更偏**问题编号 + 地图资产 + latent command**。`Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Public/UnLuaTestCommon.h:171-215` 提供 `IMPLEMENT_UNLUA_LATENT_TEST / BEGIN_TESTSUITE` 宏壳；`.../Private/Specs/LuaEnv.spec.cpp:23-62` 把 API contract 写成 `UnLua.API.*` spec；`.../Private/Tests/Issue603Test.cpp:22-33` 则把真实 bug 固化成 `UnLua.Regression.Issue603`，并通过 `FOpenMapLatentCommand(MapName)` 回放真实资产场景。它的价值是能连 Blueprint、地图、脚本路径一起回归；代价是单 case 更重。
- Hazelight 当前快照里本轮没有继续定位到与 UnLua issue-map 对等的资产回归层，也没看到像当前 AS 这样系统化的“内存编译 helper”被抬成 repo runner 约定。因此这里更适合写成“当前 AS / UnLua 各自代表两端”，而不是强行把 HZ 填成某一侧。

##### 子维度 3：machine-readable 产物是谁负责

- 当前 Angelscript 的 operator artifact 很明确。`Tools/RunTests.ps1:69-100,137-178` 会生成 `RunMetadata.json`、`Summary.json`，并把 `LogPath / ReportPath / TimeoutMs / Arguments / Prewarm / BuildBatLockWait` 都落成结构化数据；`Documents/Guides/Test.md:179-196` 进一步把 `Automation.log / Report / RunMetadata.json / Summary.json` 规定成 run 私有产物。这里的关键不是“有日志”，而是**测试运行本身被做成了可消费 artifact**。
- Hazelight 的 `AngelscriptTestCommandlet.cpp` 只有退出码，没有当前 AS 这种 repo-owned summary / metadata 层。这里属于“实现质量差异”，不是“没有测试”。
- UnLua 当前快照里能确认的是 UE Automation + sample project + test plugin；本轮没有定位到对等的仓库级 `RunMetadata.json` / `Summary.json` 规范。它更像“工程内回归能力强”，而不是“runner artifact 强”。

[1] 当前 Angelscript 已经把测试入口、超时、目录隔离和产物收口成仓库级 runner，而不是让每个调用方各写一遍 `UnrealEditor-Cmd.exe`：

```powershell
# ============================================================================
# 文件: Tools/RunTests.ps1
# 位置: 45-111, 137-178
# 说明: 单次 run 的超时、互斥、日志、报告、metadata 都由官方 runner 统一管理
# ============================================================================
$defaultTimeoutMs = $agentConfig.TestDefaultTimeoutMs
$resolvedTimeoutMs = Resolve-TimeoutMs -RequestedTimeoutMs $TimeoutMs -DefaultTimeoutMs $defaultTimeoutMs -ParameterName 'TimeoutMs'
$deadlineUtc = New-ExecutionDeadline -TimeoutMs $resolvedTimeoutMs
...
$outputLayout = New-CommandOutputLayout -ProjectRoot $projectRoot -Category 'Tests' -Label $Label -RequestedOutputRoot $OutputRoot -LogFileName 'Automation.log'
$metadataPath = Join-Path $outputLayout.OutputRoot 'RunMetadata.json'
$summaryPath = Join-Path $outputLayout.OutputRoot 'Summary.json'
...
$worktreeMutex = Acquire-NamedMutex -Name $worktreeMutexName -TimeoutMs 0
...
$argumentList = @(
	$agentConfig.ProjectFile
	"-ExecCmds=Automation RunTests $target; Quit"
	...
	"-ABSLOG=$($outputLayout.LogPath)"
	"-ReportExportPath=$($outputLayout.ReportPath)"
	'-NOSOUND'
)
if (-not $Render)
{
	$argumentList += '-NullRHI' # ★ headless 是默认 contract，不是调用方自己记
}
...
Write-JsonFile -Path $metadataPath -InputObject ([PSCustomObject]@{
	TimeoutMs       = $resolvedTimeoutMs
	OutputRoot      = $outputLayout.OutputRoot
	LogPath         = $outputLayout.LogPath
	ReportPath      = $outputLayout.ReportPath
	SummaryPath     = $summaryPath
	Arguments       = $argumentList
	...
})

# ============================================================================
# 文件: Tools/RunTestSuite.ps1
# 位置: 41-148
# 说明: suite 不是文档约定，而是正式脚本中的 fanout contract
# ============================================================================
$suiteDefinitions = [ordered]@{
	"Smoke" = @(
		@{ Prefix = "Angelscript.CppTests.MultiEngine"; Label = "MultiEngine" }
		...
	)
	"Debugger" = @(
		@{ Prefix = "Angelscript.CppTests.Debug."; Label = "CppDebugger" }
		@{ Prefix = "Angelscript.TestModule.Debugger."; Label = "TestModuleDebugger" }
	)
	"ScenarioSamples" = @(
		@{ Prefix = "Angelscript.TestModule.Actor"; Label = "Actor" }
		...
	)
}
...
if ($TimeoutMs -gt 900000) {
	throw "TimeoutMs cannot exceed 900000ms."
}
...
$argList += @("-File", $runTestsPath, "-TestPrefix", $entry.Prefix, "-Label", $runLabel)

# ============================================================================
# 文件: Documents/Guides/Test.md
# 位置: 5-11, 120-131, 179-196
# 说明: 文档层把 runner 与产物布局上升成仓库规则
# ============================================================================
- 本仓库的标准自动化测试入口是 `Tools\RunTests.ps1`。
- 具名 suite 只能通过 `Tools\RunTestSuite.ps1` 调度；
- 所有测试命令都必须显式带超时，且超时不得超过 `900000ms`。
- 每次测试都必须写入自己的独立输出目录；
...
- 非渲染模式下追加 `-NullRHI`
- 通过 `-ABSLOG` 与 `-ReportExportPath` 把日志和报告写入当前 run 的独立目录
...
Saved/Tests/<Label>/<RunId>/
  Automation.log
  Report/
  RunMetadata.json
  Summary.json
```

[2] 当前 Angelscript / Hazelight 的 commandlet batch 仍同构，但当前 AS 已经在它上面叠了 repo-owned runner：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptTestCommandlet.cpp
// 位置: 5-25
// 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/AngelscriptTestCommandlet.cpp
// 位置: 5-25
// 说明: 两边都保留了 0/1/2/3 退出码 contract；差异在于 current AS 上面多了一层 repo runner
// ============================================================================
int32 UAngelscriptTestCommandlet::Main(const FString& Params)
{
	if (!FAngelscriptEngine::Get().bDidInitialCompileSucceed)
	{
		return 1; // ★ 初始编译失败
	}
	if (!RunAngelscriptUnitTests(FAngelscriptEngine::Get().GetActiveModules(), &FAngelscriptEngine::Get(), 0, 0))
	{
		return 2; // ★ 单元测试失败
	}
#if WITH_EDITOR
	if (FStructUtils::AttemptToFindUninitializedScriptStructMembers() != 0)
	{
		return 3; // ★ 额外结构体初始化体检失败
	}
#endif
	return 0;
}
```

[3] UnLua 的测试入口 owner 在样例工程和 test plugin，自带 spec DSL 与 issue-map regression；当前 AS 的 fixture 则大量用内存脚本即时编译：

```cpp
// ============================================================================
// 文件: Reference/UnLua/TPSProject.uproject
// 位置: 16-33
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/UnLuaTestSuite.uplugin
// 位置: 24-30
// 说明: UnLua 把测试作为 sample project + opt-in plugin 组合分发
// ============================================================================
"Plugins": [
	{ "Name": "RuntimeTests", "Enabled": true },
	{ "Name": "EditorTests", "Enabled": true },
	{ "Name": "FunctionalTestingEditor", "Enabled": true },
	{ "Name": "UnLuaTestSuite", "Enabled": true }
]
...
"Modules": [
	{
		"Name": "UnLuaTestSuite",
		"Type": "Runtime",
		"LoadingPhase": "Default"
	}
]

// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Public/UnLuaTestCommon.h
// 位置: 171-215
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/UnLuaTestCommon.cpp
// 位置: 60-97
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Specs/LuaEnv.spec.cpp
// 位置: 23-62
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Tests/Issue603Test.cpp
// 位置: 22-33
// 说明: UnLua 同时有 spec、latent testsuite、map-based regression
// ============================================================================
#define IMPLEMENT_UNLUA_LATENT_TEST(TestClass, PrettyName) ...
#define BEGIN_TESTSUITE(TestClass, PrettyName) ...
...
bool FUnLuaTestBase::SetUp()
{
	UnLua::Startup();
	...
	if (MapName.IsEmpty())
	{
		...
	}
	else
	{
		AutomationOpenMap(MapName); // ★ 测试 world / map 由样例工程资产决定
	}
	return true;
}
...
BEGIN_DEFINE_SPEC(FLuaEnvSpec, "UnLua.API.FLuaEnv", ...)
...
BEGIN_TESTSUITE(FIssue603Test, TEXT("UnLua.Regression.Issue603 在Lua监听的事件中再次触发事件会导致崩溃"))
const auto MapName = TEXT("/UnLuaTestSuite/Tests/Regression/Issue603/Issue603");
ADD_LATENT_AUTOMATION_COMMAND(FOpenMapLatentCommand(MapName))

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleTestSupport.cpp
// 位置: 16-59
// 说明: 当前 AS 的不少 regression / example fixture 先是“内存脚本 + clean engine clone”
// ============================================================================
bool RunScriptExampleCompileTest(FAutomationTestBase& Test, const FScriptExampleSource& Example)
{
	FAngelscriptEngine& Engine = AcquireCleanSharedCloneEngine();
	...
	const FString VirtualFileName = FString::Printf(TEXT("ScriptExamples/%s"), *ExampleFileName);
	const bool bCompiled = CompileAnnotatedModuleFromMemory(&Engine, ModuleName, VirtualFileName, CombinedScriptCode);
	Test.TestTrue(*FString::Printf(TEXT("Compile example '%s' succeeds"), *ExampleFileName), bCompiled);
	return bCompiled; // ★ 载体是脚本文本，不是预置地图资产
}
```

#### 对比矩阵（本轮新增观察点）

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 仓库级官方 runner script | None | None | None | None | None | Full |
| 官方 suite 调度层与超时透传 | None | None | None | None | None | Full |
| 插件内公开 `TestCommandlet` 批执行入口 | Full | None | None | None | None | Full |
| 测试入口 owner 在 sample project + opt-in test plugin | None | None | Full | None | None | None |
| 每个 run 都落 machine-readable metadata / summary | None | None | None | None | None | Full |
| 大量 regression case 以内存脚本即时编译为载体 | None | None | None | None | None | Full |
| issue 编号 + 地图资产 + latent command 是主要回归形态 | None | None | Full | None | None | None |

#### 小结与建议

- 当前 Angelscript 在这个观察点上的独特优势，是 **repo-owned runner + per-run metadata artifact + 轻量内存脚本夹具** 三层一起存在。它比单纯有 commandlet 更适合 CI、Agent 和多 worktree 并行，建议按 `P0` 保持。
- 最值得吸收的是 UnLua 的**issue 编号与地图资产回归语义**。当前 AS 已经很强于 headless/轻夹具，但还缺一层更稳定的“问题编号 -> 真实资产场景”索引，优先级 `P1`。
- 不建议为了学 UnLua 的 regression 资产化，就放弃现有 `RunTests.ps1` / `RunTestSuite.ps1` 和内存脚本 helper。更合理的路线是叠加：继续保留 repo runner 与轻量 case，再把少数 Blueprint/地图问题沉成 issue-asset regression，优先级 `P1`。

---

## 深化分析 (2026-04-08 23:23:55)

### [D2] 绑定注册时机与生命周期：谁负责“出生”和“死亡”

#### 各插件实现概览

```
[D2-Lifecycle] Binding Registration Lifetime
HZ      : static FBind(order) -> global bind array -> CallBinds() -> manager lifetime
AS(now) : static FBind(name+order) -> BindModules.Cache -> CallBinds(disabled set) -> engine lifetime
UC      : FCSharpEnvironment::Initialize -> create registries + Domain -> RegisterBinding() -> environment lifetime
UL      : FLuaEnv boot -> ClassRegistry alive -> PushMetatable/Register on first use -> LuaEnv lifetime
PU      : StructWrapper::Get*Translator on first access -> MethodsMap/FunctionsMap cache -> JsEnv/Wrapper lifetime
SL      : LuaState::init cache tables -> LuaFunctionAccelerator::findOrAdd(UFunction) -> LuaState/UFunction lifetime
```

前面的 `D2` 已经回答过 binding authority 落在哪。本轮补的是另一个容易混淆的问题：**绑定对象到底什么时候创建，谁持有它，什么时候销毁**。同样叫“反射绑定”，Hazelight / 当前 Angelscript 属于“进程级静态注册”，UnrealCSharp 属于“environment 级 registry”，UnLua / puerts / sluaunreal 则更接近“脚本环境或 wrapper 级懒物化”。

这一层直接决定两个工程属性：一是启动时一次性付多少钱，二是热重载或对象销毁时谁来回收旧 binding。把这层看清，才能避免把 “没有全局 bind 表” 误判成 “没有绑定能力”。

#### 详细对比

##### 子维度 1：global static register，还是 scoped registry

- 当前 Angelscript 仍沿用血缘上的 `static FBind` 路线，但已经把注册单元从 “只有顺序” 提升成 “`BindName + BindOrder + disabled-set + observation`”。`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:438-476` 与 `.../AngelscriptBinds.cpp:151-173,195-218` 显示每个 bind 在静态构造期就进入全局数组，真正执行时再按名字过滤、记录执行观测。
- Hazelight 的同类实现明显更薄。`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/AngelscriptBinds.h:507-525` 与 `.../Private/AngelscriptBinds.cpp:34-43` 只有 `BindOrder + Function` 两元组，没有 bind 名字、禁用名单、执行观测，也没有当前插件新增的 `BindModules.Cache` 装载路径。这是同路线下的**实现质量差异**，不是两者路线不同。
- UnrealCSharp 则不是静态 bind 表，而是 `FCSharpEnvironment` 启动时成批创建 `ClassRegistry / StructRegistry / DelegateRegistry / BindingRegistry`，随后由 `FMonoDomain::RegisterBinding()` 把 internal call 注册进 Mono。`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp:54-87,154-236` 与 `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp:123-132,783-790` 说明它的生命周期跟 `environment/domain` 绑定，而不是跟进程级静态数组绑定。

[1] 当前 Angelscript 与 Hazelight 在“注册对象诞生时机”上同源，但当前 AS 多了名字、禁用与模块清单三层 contract：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h
// 函数: FAngelscriptBinds::FBind
// 位置: 438-475
// 说明: 当前 AS 的 bind 在静态构造期注册，但注册单元已经带 bind name
// ============================================================================
struct ANGELSCRIPTRUNTIME_API FBind
{
	FBind(FName BindName, int32 BindOrder, TFunction<void()> Function)
	{
		FAngelscriptBinds::RegisterBinds(BindName, BindOrder, MoveTemp(Function));
	}
	...
};

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp
// 函数: FAngelscriptBinds::RegisterBinds / CallBinds
// 位置: 151-173, 195-218
// 说明: 注册先进入全局数组，真正执行时可按 disabled set 过滤并记录观测
// ============================================================================
void FAngelscriptBinds::RegisterBinds(FName BindName, int32 BindOrder, TFunction<void()> Function)
{
	GetBindArray().Add({BindName.IsNone() ? MakeUnnamedBindName() : BindName, BindOrder, MoveTemp(Function)});
}

for (const FBindFunction& Bind : GetSortedBindArray())
{
	if (DisabledBindNames.Contains(Bind.BindName))
	{
		UE_LOG(Angelscript, Log, TEXT("Skipping bind '%s'"), *Bind.BindName.ToString());
		continue;
	}

	FAngelscriptBindExecutionObservation::RecordExecutedBind(Bind.BindName);
	Bind.Function(); // ★ 执行期才真正把类型/函数注册进脚本引擎
}

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h
// 函数: FAngelscriptBinds::LoadBindModules
// 位置: 583-602
// 说明: 当前 AS 还把 auto-generated bind module 名单持久化为缓存文件
// ============================================================================
static void LoadBindModules(FString Path)
{
	auto& BindModuleNames = GetBindModuleNames();
	FFileHelper::LoadFileToStringArray(BindModuleNames, *Path);
}

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: FAngelscriptEngine::InitializeAngelscript
// 位置: 1473-1496
// 说明: 先读 BindModules.Cache，再动态装载 bind modules，再统一 CallBinds
// ============================================================================
if (plugin)
{
	FAngelscriptBinds::LoadBindModules(plugin->GetBaseDir() / "BindModules.Cache");
}

for (FString ModuleName : FAngelscriptBinds::GetBindModuleNames())
{
	if (!ModuleName.IsEmpty())
	{
		FModuleManager::Get().LoadModule(FName(ModuleName), ELoadModuleFlags::LogFailures);
	}
}

BindScriptTypes(); // ★ 真正绑定发生在缓存装载之后

// ============================================================================
// 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/AngelscriptBinds.h
// 函数: FAngelscriptBinds::FBind
// 位置: 507-525
// 说明: Hazelight 仍是纯 order-only 的静态 bind
// ============================================================================
struct ANGELSCRIPTCODE_API FBind
{
	FBind(int32 BindOrder, TFunction<void()> Function)
	{
		FAngelscriptBinds::RegisterBinds(BindOrder, Function);
	}
	...
};

// ============================================================================
// 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/AngelscriptBinds.cpp
// 函数: FAngelscriptBinds::RegisterBinds / CallBinds
// 位置: 34-43
// 说明: 只有顺序，没有 bind name / disabled / observation / bind-module 清单
// ============================================================================
void FAngelscriptBinds::RegisterBinds(int32 BindOrder, TFunction<void()> Function)
{
	GetBindArray().Add({BindOrder, Function});
}

GetBindArray().Sort();
for (auto& Function : GetBindArray())
	Function.Function();
```

##### 子维度 2：懒注册发生在 type、function，还是整个 env

- UnLua 的 owner 是 `FLuaEnv + FClassRegistry`。`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:104-110` 先在 env 启动时创建 registry，但 `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:108-130,247-314` 显示 metatable 与 `FClassDesc` 真正是在 `PushMetatable / Register` 首次访问时才懒创建，并在 `NotifyUObjectDeleted()` 时注销。这不是“没有绑定预注册”，而是**env 级容器 + type 级 lazy materialization**。
- puerts 更进一步，把 `FFunctionTranslator` 的生命周期压到 `StructWrapper` 内部。`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:38-67,282-327` 说明 method/function translator 首次访问才 `make_shared`，之后再把 `FunctionTemplate` 填回 JS prototype 或 object template。这里的 owner 不是整个 VM，而是 wrapper 和具体 `UFunction`。
- sluaunreal 则把缓存切得更细。`Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaState.cpp:526-583` 先在 `LuaState::init()` 建立 `cacheObjRef / cacheEnumRef / cacheClassPropRef / cacheClassFuncRef` 四级缓存；`Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaFunctionAccelerator.cpp:145-179` 再在 `findOrAdd(UFunction*)` 时懒建 `LuaFunctionAccelerator`。对象删除时 `LuaState.cpp:798-803` 还会同步移除 accelerator 和 cache。它的生命周期是 `LuaState + UFunction` 双维度。
- UnrealCSharp 介于两者之间。它不像 UnLua/puerts/slua 那样把大部分 binding 推迟到首次访问，而是先 eager 创建 environment 中心 registry；但类、属性、函数 descriptor 又允许按 hash 单独增删，`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp:424-458` 明确提供 `RemoveClassDescriptor / RemoveFunctionDescriptor / RemovePropertyDescriptor`。这应判为 `Partial lazy / Full scoped-lifetime`。

[2] UnLua / puerts / sluaunreal 都能懒物化绑定，但懒点并不相同：

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp
// 函数: FClassRegistry::PushMetatable / Register / RegisterInternal
// 位置: 108-130, 247-314
// 说明: UnLua 先有 env-level registry，真正的 reflected type 在第一次 Push/Register 时创建
// ============================================================================
bool FClassRegistry::PushMetatable(lua_State* L, const char* MetatableName)
{
	int Type = luaL_getmetatable(L, MetatableName);
	...
	FClassDesc* ClassDesc = RegisterReflectedType(MetatableName); // ★ 首次访问时加载 reflected type
	...
}

FClassDesc* FClassRegistry::Register(const char* MetatableName)
{
	const auto L = Env->GetMainState();
	if (!PushMetatable(L, MetatableName))
		return nullptr;
	lua_pop(L, 1);
	return Name2Classes.FindChecked(FName(UTF8_TO_TCHAR(MetatableName)));
}

void FClassRegistry::NotifyUObjectDeleted(UObject* Object)
{
	Unregister((UStruct*)Object); // ★ 生命周期跟 LuaEnv + UE object 一起收尾
}

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp
// 函数: FStructWrapper::GetMethodTranslator / GetFunctionTranslator
// 位置: 38-67
// 说明: puerts 的 translator 是 wrapper 内部按 UFunction 首次访问创建
// ============================================================================
auto Iter = MethodsMap.Find(InFunction->GetFName());
if (!Iter)
{
	std::shared_ptr<FFunctionTranslator> FunctionTranslator = std::make_shared<FFunctionTranslator>(InFunction, false);
	MethodsMap.Add(InFunction->GetFName(), FunctionTranslator);
	return FunctionTranslator; // ★ 首次访问付费，后续走缓存
}

// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaState.cpp
// 函数: LuaState::init
// 位置: 526-583
// 说明: slua 先起 LuaState 级 cache table，再按 UFunction 懒建 accelerator
// ============================================================================
cacheObjRef = newCacheTable(L);
lua_newtable(L);
cacheEnumRef = luaL_ref(L, LUA_REGISTRYINDEX);
lua_newtable(L);
cacheClassPropRef = luaL_ref(L, LUA_REGISTRYINDEX);
lua_newtable(L);
cacheClassFuncRef = luaL_ref(L, LUA_REGISTRYINDEX);

// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaFunctionAccelerator.cpp
// 函数: LuaFunctionAccelerator::findOrAdd / clear
// 位置: 145-179
// 说明: accelerator 的 owner 是 UFunction cache，不是全局 bind graph
// ============================================================================
auto ret = cache.Find(inFunc);
if (ret)
{
	return *ret;
}

auto value = new LuaFunctionAccelerator(inFunc);
cache.Emplace(inFunc, value);
...
cache.Empty(); // ★ state close 时整批清空
```

#### 对比矩阵（本轮新增观察点）

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 静态构造期就形成全局 bind 表 | Full | None | None | None | None | Full |
| bind 单元自带名字 / 可禁用 / 可观测 | None | Partial | None | None | None | Full |
| 绑定生命周期显式绑定到 env / domain | None | Full | Full | Partial | Full | Partial |
| type / function 首次访问时才懒物化 | None | Partial | Full | Full | Full | None |
| 对象删除时有明确 unregister / cache eviction | None | Full | Full | Partial | Full | Partial |
| 额外有 bind-module 清单把发现与执行拆开 | None | None | None | None | None | Full |

#### 小结与建议

- 当前 Angelscript 在这一维的新增优势，不是“也有 static bind”，而是把 static bind 从 `order-only` 升级成了 **named / disable-able / observable / module-list-backed**。这条主线应按 `P0` 保持。
- UnrealCSharp / UnLua / puerts / sluaunreal 提醒的不是“要不要放弃静态 bind”，而是**能否把稀有路径改成 scoped lazy**。最值得 current AS 吸收的是为 reflective fallback 或 editor-only bind 增加更细粒度的 lazy materialization，优先级 `P1`。
- 不建议把 current AS 整体回退成纯 env-lazy 方案。它的强项仍是可预估、可统计、可测试的全局 bind contract；更合理的方向是在这条主线上补局部 lazy，而不是推倒重来，优先级 `P1`。

### [D4] 状态保持 contract：UObject 重实例化、脚本表合并，还是整域重建

#### 各插件实现概览

```
[D4-State] Reload State Survival
HZ/AS   : old UFunction detach -> temp buffer -> CDO/script object reinitialize -> force GC
UC      : unload AssemblyLoadContext + registries -> rebuild domain state -> no in-place UObject script payload migration found
UL      : sandbox load new module -> merge functions into old module -> match upvalues -> update stack/_G/registry
PU      : NotifyRebind -> MakeSureInject + FinishInjection -> walk GeneratedObjects and re-inject JS side
SL      : objectTableMap + LuaState caches live while state lives -> onLuaStateClose clear maps -> new state rebuilds
```

前面的 `D4` 已经把 reload authority 和失败队列写清了。本轮只补 **state preservation 策略本身**。这一步很关键，因为“支持热重载”并不等于“脚本状态会留下来”。当前 Angelscript / Hazelight 保的是 `UObject/CDO` 上的脚本负载；UnLua 保的是 Lua module table 与 upvalue；puerts 保的是 generated object 对应的 JS 注入关系；UnrealCSharp 当前取证到的是 `domain/assembly` 级重建；slua 则更接近“LuaState 活着就保，LuaState 关掉就清”。

#### 详细对比

##### 子维度 1：状态是原位迁移，还是整个 VM / domain 重建

- 当前 Angelscript 的 state contract 仍然最接近 UE 原生对象模型。`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2126-2160,2422-2444,4735-4768` 显示 full/soft reload 后会重新 link class；如果发生 reinstance，则先把旧 `UASFunction::ScriptFunction` 置空避免旧类误调，再把 CDO script object 析构、从临时缓冲拷回 instanced/stateful 属性、重新构造 script object，最后强制 GC 清掉旧实例。状态 owner 是 `UObject + script object layout`。
- Hazelight 的 CDO / GC 路径与当前几乎同源。`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/ClassGenerator/AngelscriptClassGenerator.cpp:2367-2376,2400-2402,4575-4608` 也会清空旧 `ScriptFunction`、reinstance CDO、GC 旧实例。因此这两者在“状态放在 UObject 一侧”这一点上属于**实现方式相同**。
- UnrealCSharp 当前取证到的状态边界更靠近 domain。`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp:54-87,154-236` 会成批创建并析构 registry；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp:135-145,560-575,606-625,734-741` 则在 deinitialize 时 `UnloadAssembly()` 并卸载 `AssemblyLoadContext`。本轮没有定位到与 current AS 对等的 `UObject script payload` 原位搬运逻辑，因此应判为“状态 owner 更偏 domain/assembly 生命周期”，不是简单写成“没有热重载”。

[1] 当前 Angelscript / Hazelight 把 reload 后的 state preservation 放在 `UObject + CDO` 一侧：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 函数: FAngelscriptClassGenerator::PerformReload
// 位置: 2126-2160, 2422-2444
// 说明: 先决定 soft/full reload，再把旧函数断开，并在 reinstance 后强制 GC
// ============================================================================
void FAngelscriptClassGenerator::PerformReload(bool bFullReload)
{
	...
	for (auto& ModuleData : Modules)
	{
		for (auto& ClassData : ModuleData.Classes)
		{
			if (ShouldFullReload(ClassData))
			{
				CreateFullReloadClass(ModuleData, ClassData);
			}
			else
			{
				LinkSoftReloadClasses(ModuleData, ClassData);
			}
		}
	}
	...
	if (Function == nullptr)
		continue;
	Function->ScriptFunction = nullptr; // ★ 先把旧 UASFunction 脱钩，避免旧类误调用
	...
	if (bReinstancingAny)
		GEngine->ForceGarbageCollection(true); // ★ reinstance 后主动清旧实例
}

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 函数: CDO reinstance path
// 位置: 4735-4768
// 说明: 当前 AS 在 CDO 上做 script object 原位搬运
// ============================================================================
asCScriptObject* ScriptObject = (asCScriptObject*)FAngelscriptEngine::UObjectToAngelscript(CDO);
DestructScriptObject(ScriptObject, Class, (asCObjectType*)OldScriptType);
...
Copy.Type.CopyValue(OriginalPtr, NewPtr); // ★ 先从临时缓冲恢复 instanced 属性
...
ReinitializeScriptObject(ScriptObject, Class, (asCObjectType*)ScriptType);
...
Copy.Type.CopyValue(OriginalPtr, NewPtr); // ★ 再把新 layout 下的值拷回新 script object

// ============================================================================
// 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/ClassGenerator/AngelscriptClassGenerator.cpp
// 函数: CDO reinstance path
// 位置: 2367-2376, 2400-2402, 4575-4608
// 说明: Hazelight 在 state preservation 策略上与 current AS 同血缘
// ============================================================================
Function->ScriptFunction = nullptr;
...
if (bReinstancingAny)
	GEngine->ForceGarbageCollection(true);
...
asCScriptObject* ScriptObject = (asCScriptObject*)FAngelscriptManager::UObjectToAngelscript(CDO);
DestructScriptObject(ScriptObject, Class, (asCObjectType*)OldScriptType);
...
ReinitializeScriptObject(ScriptObject, Class, (asCObjectType*)ScriptType);
```

##### 子维度 2：脚本侧状态是 merge 还是 discard

- UnLua 的策略跟当前 AS 完全不同，它保的是 Lua 模块图。`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:2-7` 直接写明“替换运行环境中的函数，保持 upvalue 和运行时的 table”；`.../HotReload.lua:480-547,553-624` 则把新模块加载进 sandbox，把新函数写回旧 module、匹配 upvalue、最后更新运行栈、`_G` 和 registry。这应判为**实现方式不同**，不是“没有状态保持”。
- puerts 的保留面是 generated object 到 JS prototype/injection 的关系。`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/TypeScriptGeneratedClass.cpp:77-99` 先在 `NotifyRebind()` 上清 `NeedReBind` 并通知 dynamic invoker；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:2325-2366` 再遍历 `GeneratedObjects`，跳过 `REINST_` 临时对象，重新 `FindOrAdd` / `MakeSureInject`。它保的是 live object 的 JS 侧附着关系，不是 UObject 内存布局。
- sluaunreal 的状态 contract 更脆弱一些。`Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaOverrider.cpp:414-423` 会把 `UObject -> Lua table` 挂进 `objectTableMap`；但 `.../LuaOverrider.cpp:344-360` 一旦 `LuaState` 关闭就移除整张表，并在 editor 下清 `FuncMap`。结合 `Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaState.cpp:521-583` 的 cache 初始化/clear，可见它更像“state 跟 LuaState 共生”，不是跨 state 的迁移。
- UnrealCSharp 则更接近 discard-and-recreate。`FMonoDomain::Deinitialize()` 与 `UnloadAssembly()` 本身并不意味着“完全丢状态”，但本轮源码里没有定位到与 UnLua/puerts/current AS 对等的 merge/reinject/reinstance 逻辑，因此最稳妥的判定是：**状态 contract 主要落在 domain/assembly rebuild，而非 per-object migration**。

[2] UnLua / puerts / sluaunreal 的 state preservation 各保不同层：

```
-- ============================================================================
-- 文件: Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua
-- 位置: 2-7, 480-547, 553-624
-- 说明: UnLua 明确在 Lua module/upvalue 层做状态合并
-- ============================================================================
--- 替换运行环境中的函数，保持upvalue和运行时的table
--- 为开发期设计，尽量替换。最差的结果就是重新启动

if new_module[HOT_RELOAD_MARK] then
    ...
else
    local new_module_info = collect_module_info(new_module)
    local old_module_upvalues = collect_module_upvalues(old_module)
    moduleres.values = match_module(new_module_info, old_module)

    for k, v in pairs(new_module) do
        if type(v) == "function" then
            old_module[k] = v        -- ★ 直接把新函数写回旧 module table
        end
        if old_module[k] == nil then
            old_module[k] = v
        end
    end

    moduleres.upvalue_map = match_upvalues(moduleres.values, old_module_upvalues)
end

merge_objects(result)
update_global(all_value_maps)          -- ★ 继续修正运行栈、_G 和 registry

local func, env = sandbox.load(module_name)
local ok, new_module = xpcall(func, load_error_handler)
if not ok then
    sandbox.exit()
    return                             -- ★ 加载失败时不合并旧状态
end

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/TypeScriptGeneratedClass.cpp
// 位置: 77-99
// 说明: puerts 先发 rebind 信号，再由 JsEnv 重新注入 live objects
// ============================================================================
if (TsClass->NeedReBind && TsClass->DynamicInvoker.IsValid())
{
	TsClass->NeedReBind = false;
	...
	CachedClass->DynamicInvoker.Pin()->NotifyReBind(CachedClass); // ★ 状态 owner 在 generated class + invoker
}

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp
// 位置: 2325-2366
// 说明: rebind 时遍历 live generated objects，并跳过 REINST_ 临时对象
// ============================================================================
MakeSureInject(Class, false, false);
FinishInjection(Class);
...
for (TWeakObjectPtr<UObject>& Iter : Class->GeneratedObjects)
{
	auto Object = Iter.Get();
	if (!Object || ObjectMap.Find(Object))
		continue;
	if (Object->GetClass()->GetName().StartsWith(TEXT("REINST_")))
		continue; // ★ 跳过重编译中的临时对象
	__USE(FindOrAdd(Isolate, Context, Object->GetClass(), Object, true));
	...
	MakeSureInject(ClassMayNeedReBind, false, false);
}

// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaOverrider.cpp
// 位置: 414-423, 344-360
// 说明: slua 的对象表状态跟 LuaState 共生，state close 时直接清掉
// ============================================================================
auto &tableMap = objectTableMap.FindOrAdd(L);
tableMap.Add(obj, {table, isInstance}); // ★ 对象到 Lua table 的活跃映射

if (objectTableMap.Contains(L))
{
	...
	overrideInterface->FuncMap.Empty(); // ★ editor 下连缓存的 override 函数表也一起清
	objectTableMap.Remove(L);
}
```

#### 对比矩阵（本轮新增观察点）

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| `UObject/CDO` script payload 原位重实例化 | Full | None | None | None | None | Full |
| 模块 table / upvalue 图做 merge 而不是重建 | None | None | Full | Partial | None | None |
| live generated objects 会被显式重新注入 | N/A | None | None | Full | None | N/A |
| reload 时旧函数 / 旧 hook 会被显式脱钩 | Full | Partial | Full | Full | Partial | Full |
| 状态 owner 更偏整域/整 VM 生命周期 | None | Full | None | Partial | Full | None |
| state close / reload 伴随明确 cache teardown | Partial | Full | Partial | Partial | Full | Full |

#### 小结与建议

- 当前 Angelscript 在这一层最重要的强项，是 **把脚本状态牢牢锚在 UObject/CDO 重实例化** 上，而不是把状态丢给脚本 VM 自己兜。对深度 UE 集成插件来说，这条路线仍应按 `P0` 保持。
- 最值得吸收的是 UnLua 与 puerts 的两类补强，而不是它们的主路线本身。`UnLua` 值得借的是“对纯脚本单例/表状态给出 merge hook”；`puerts` 值得借的是“reload 后显式 walk live objects 并 rebind”。两者都可作为 current AS 的 `P1` 增量能力。
- UnrealCSharp / sluaunreal 提醒的是另一条边界：只要 state contract 主要依赖 domain/LuaState teardown，热重载就更像“重启局部运行时”。current AS 不应回退到这一路线，优先级 `P0`。

### [D8] 冷启动摊销与批量注册：谁在启动时付费，谁把成本推到首触发

#### 各插件实现概览

```
[D8-ColdStart] Cost Amortization
HZ      : load Binds.Cache -> CallBinds() -> PrecompiledData/StaticJIT
AS(now) : UHT summary/shards -> BindModules.Cache -> load bind modules -> CallBinds(disabled set) -> PrecompiledData + context pool
UC      : domain init -> assembly load -> reflection registry init -> RegisterBinding(all internal calls)
UL      : env boot -> defer reflected type register to PushMetatable/Register -> persistent param buffer reuse
PU      : lazy StructWrapper translator/template cache -> FastCall when possible -> watcher only for loaded files
SL      : LuaState cache tables at init -> per-UFunction accelerator on demand -> clear on delete/state close
```

前面的 `D8` 已经比较过优化 authority 和可观测面。本轮只看一个更具体的问题：**冷启动成本是怎么摊平的**。这里的差异不是“谁更快”这种空话，而是“谁在 build/startup 阶段一次性付费，谁把成本推迟到第一次类型/函数触发”。这条线直接影响 editor 首开延迟、PIE 第一次脚本调用抖动，以及 CI/commandlet 的整体耗时形态。

#### 详细对比

##### 子维度 1：启动期一次性预热，还是首次触发懒付费

- 当前 Angelscript 明显属于“前置摊销”路线。`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:166-240` 先在 UHT 阶段写 `AS_FunctionTable_Summary.json` 和 module summary CSV；运行时再在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1468-1496,1512-1515` 读取 `Binds.Cache`、`BindModules.Cache`、装载 auto-generated bind modules、统一 `BindScriptTypes()`，然后才进入 precompiled data 路径。这意味着大部分 binding discovery 成本已经被推到 build/startup，而不是散落在第一次脚本调用里。
- Hazelight 也走前置摊销，但层级更少。`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/AngelscriptManager.cpp:404-417,792-795` 只有 `Binds.Cache -> CallBinds()` 这一级，再往后接 `PrecompiledData + StaticJIT`。它有装载期优化，但没有 current AS 这轮新增的 UHT summary / bind-module 分片 contract。
- UnrealCSharp 也是启动期重付费路线，只是付费点换成 `.NET domain + assembly + internal call`。`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp:46-132,783-790` 会先初始化 domain、加载程序集、初始化反射 registry，再把所有 binding 方法注册成 `mono_add_internal_call`。它把成本压在 runtime boot，而不是首次 `UFunction` 触发。

[1] 当前 Angelscript 把冷启动摊销明确拆成 `UHT artifact -> bind-module cache -> bind db -> precompiled data` 四层；Hazelight 只有中后两层：

```
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs
// 函数: WriteGenerationSummary / WriteModuleSummaryCsv
// 位置: 166-240
// 说明: current AS 在 UHT 阶段就把 direct/stub/shard 成本写成 artifact
// ============================================================================
int totalGeneratedEntries = moduleSummaries.Sum(static summary => summary.TotalEntries);
int totalDirectBindEntries = moduleSummaries.Sum(static summary => summary.DirectBindEntries);
int totalStubEntries = moduleSummaries.Sum(static summary => summary.StubEntries);
...
string summaryPath = factory.MakePath("AS_FunctionTable_Summary", ".json");
...
builder.AppendLine("ModuleName,EditorOnly,TotalEntries,DirectBindEntries,StubEntries,DirectBindRate,StubRate,ShardCount");

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: FAngelscriptEngine::InitializeAngelscript / BindScriptTypes
// 位置: 1468-1496, 1915-1921
// 说明: 启动时先吃缓存，再统一 CallBinds，而不是首个函数调用时再发现 binding
// ============================================================================
FAngelscriptBindDatabase::Get().Load(GetScriptRootDirectory() / TEXT("Binds.Cache"), bGeneratePrecompiledData);
FAngelscriptBinds::LoadBindModules(plugin->GetBaseDir() / "BindModules.Cache");
...
FModuleManager::Get().LoadModule(FName(ModuleName), ELoadModuleFlags::LogFailures);
...
BindScriptTypes();
...
FAngelscriptBinds::CallBinds(CollectDisabledBindNames());

// ============================================================================
// 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/AngelscriptManager.cpp
// 函数: InitializeAngelscript / BindScriptTypes
// 位置: 404-417, 792-795
// 说明: Hazelight 也前置加载 bind database，但缺少 current AS 的 bind-module / summary 分层
// ============================================================================
FAngelscriptBindDatabase::Get().Load(GetScriptRootDirectory() / TEXT("Binds.Cache"), bGeneratePrecompiledData);
...
BindScriptTypes();
...
FAngelscriptBinds::CallBinds();
```

##### 子维度 2：首触发热路径，靠 translator/cache，还是靠上下文池

- 当前 Angelscript 在调用期的摊销方式仍以上下文重用为主。`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1722-1747,1815-1840` 显示线程本地 pool 和 global pool 会优先回收 `asCContext`；而 `.../AngelscriptEngine.cpp:1512-1624` 与前文已分析的 `PrecompiledData + StaticJIT` 则把“是否需要重新编译/重新解析”尽量压到装载期。它更像“尽量别重复搭执行现场”。
- UnLua、puerts、sluaunreal 则更偏首次调用热点优化。`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:108-130,247-314` 把 reflected type 注册延迟到第一次 metatable push；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/ParamBufferAllocator.cpp:38-69` 再用 persistent param buffer 摊平后续调用分配成本。puerts 用 `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:38-67` 的 translator cache 和 `.../FunctionTranslator.cpp:265-314` 的 `FastCall/SlowCall` 分流来消化首触发成本。slua 则用 `Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaState.cpp:570-583` 的 cache tables 与 `.../LuaFunctionAccelerator.cpp:145-179` 的 per-`UFunction` accelerator 达成同样目的。
- UnrealCSharp 的热路径更接近“启动后保持 registry 常驻”。`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp:54-87` 与 `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp:123-132,783-790` 说明它宁可在 domain 初始化时把 registry 和 internal call 一次性挂好，也不走 UnLua/puerts/slua 那种按函数第一次触发才补 translator 的路线。

[2] UnLua / puerts / sluaunreal 更像“轻启动 + 首触发懒缓存”；UnrealCSharp 则是“重启动 + 常驻 registry”：

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp
// 函数: FMonoDomain::Initialize / RegisterBinding
// 位置: 46-132, 783-790
// 说明: UnrealCSharp 在 domain 启动期一次性加载 assembly 并注册 internal call
// ============================================================================
InitializeAssembly(InParams.Assemblies);
if (bLoadSucceed)
{
	FReflectionRegistry::Get().Initialize();
}
RegisterBinding();
...
for (const auto& Class : FBinding::Get().Register().GetClasses())
{
	for (const auto& Method : Class->GetMethods())
	{
		mono_add_internal_call(TCHAR_TO_ANSI(*Method.GetMethod()), Method.GetFunction());
	}
}

// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp
// 函数: FClassRegistry::PushMetatable / Register
// 位置: 108-130, 247-314
// 说明: UnLua 把 reflected type 的注册延迟到第一次 metatable 访问
// ============================================================================
FClassDesc* ClassDesc = RegisterReflectedType(MetatableName); // ★ type 首次触发才付 binding 成本
...
return Name2Classes.FindChecked(FName(UTF8_TO_TCHAR(MetatableName)));

// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/ParamBufferAllocator.cpp
// 函数: FParamBufferAllocator_Persistent::Get / FParamBufferFactory::Get
// 位置: 38-69
// 说明: 调用期用 persistent buffer 摊平后续参数缓冲分配
// ============================================================================
if (Counter < Buffers.Num())
	return Buffers[Counter++];
...
return MakeShareable(new FParamBufferAllocator_Persistent(Func));

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp
// 函数: FStructWrapper::GetMethodTranslator
// 位置: 38-67
// 说明: puerts 的 per-UFunction translator 在首触发时创建
// ============================================================================
auto Iter = MethodsMap.Find(InFunction->GetFName());
if (!Iter)
{
	auto FunctionTranslator = std::make_shared<FFunctionTranslator>(InFunction, false);
	MethodsMap.Add(InFunction->GetFName(), FunctionTranslator);
	return FunctionTranslator;
}

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp
// 函数: FFunctionTranslator::Call / FastCall / SlowCall
// 位置: 265-314
// 说明: 命中 native/非-net/Ubergraph 时切到 FastCall，否则走 SlowCall
// ============================================================================
if ((Function->FunctionFlags & FUNC_Native) && !(Function->FunctionFlags & FUNC_Net) &&
	!CallFunctionPtr->HasAnyFunctionFlags(FUNC_UbergraphFunction))
{
	FastCall(Isolate, Context, Info, CallObject, CallFunctionPtr, Params);
}
else
{
	SlowCall(Isolate, Context, Info, CallObject, CallFunctionPtr, Params);
}

// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaState.cpp
// 函数: LuaState::init
// 位置: 570-583
// 说明: slua 先把对象/枚举/类属性/类函数 cache table 一次性立起来
// ============================================================================
cacheObjRef = newCacheTable(L);
cacheEnumRef = luaL_ref(L, LUA_REGISTRYINDEX);
cacheClassPropRef = luaL_ref(L, LUA_REGISTRYINDEX);
cacheClassFuncRef = luaL_ref(L, LUA_REGISTRYINDEX);

// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaFunctionAccelerator.cpp
// 函数: LuaFunctionAccelerator::findOrAdd
// 位置: 145-179
// 说明: 每个 UFunction 的解析成本只在首次调用时付一次
// ============================================================================
auto ret = cache.Find(inFunc);
if (ret)
{
	return *ret;
}
auto value = new LuaFunctionAccelerator(inFunc);
cache.Emplace(inFunc, value);
```

[3] 当前 Angelscript 的调用期摊销不靠 translator cache，而靠上下文池和装载期产物复用：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: AngelscriptRequestContext / FAngelscriptContext constructor path
// 位置: 1722-1747, 1815-1840
// 说明: current AS 把调用期重复成本压到 context pool，而不是每个 UFunction 单独 translator
// ============================================================================
auto& LocalPool = GAngelscriptContextPool;
if (asCContext* Context = TryTakeContextFromPool(LocalPool.FreeContexts, Engine))
{
	return Context; // ★ 线程本地池优先
}

if (asCContext* MatchingContext = TryTakeContextFromPool(LocalPool.FreeContexts, DesiredScriptEngine))
{
	Context = MatchingContext;
	return;
}

FScopeLock Lock(&CurrentEngine->GlobalContextPoolLock);
if (asCContext* MatchingContext = TryTakeContextFromPool(CurrentEngine->GlobalContextPool, DesiredScriptEngine))
{
	Context = MatchingContext;
	Context->MovedToNewThread(); // ★ 再退回全局池
}
```

#### 对比矩阵（本轮新增观察点）

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 启动前已有 build-time binding artifact | None | Partial | None | None | None | Full |
| 启动时会先读取 bind/cache 再统一注册 | Full | Partial | None | None | None | Full |
| `bind-module` 分片把发现与执行拆开 | None | None | None | None | None | Full |
| runtime boot 就 eager 创建主要 registry / domain | None | Full | Partial | Partial | Partial | Partial |
| reflected type / function 首触发才懒建桥接对象 | None | Partial | Full | Full | Full | None |
| 调用期主要靠 context / param / translator cache 摊销 | Full | Partial | Full | Full | Full | Full |
| cache eviction / clear path 在源码中显式可见 | None | Full | Full | Partial | Full | Full |

#### 小结与建议

- 当前 Angelscript 在冷启动摊销上的独特价值，是 **把 discovery 成本前推到 UHT artifact 和 bind-module 清单，再把调用期重复成本压到 context pool**。这是它和 Hazelight 拉开工程代差的关键点，建议按 `P0` 保持。
- 最值得吸收的外部经验，不是把 current AS 改成全面 lazy，而是借 UnLua / puerts / slua 的**局部懒缓存**策略，专门优化 reflective fallback、长尾绑定和 editor-only 路径，优先级 `P1`。
- 如果未来 startup 时间继续上升，优先考虑两件事：一是把 `BindModules.Cache` 继续细分成更可裁剪的模块组；二是把 `FunctionTable summary` 和 runtime timing 关联起来，做出“哪些 bind module 付费高但命中低”的账本。两者都属于在现有路线上的增强，而不是改道，优先级 `P1`。

---

## 深化分析 (2026-04-08 23:47:08)

### [D2] Delegate surface 的 authority：签名化值类型，还是 helper / proxy UObject

前文已经比较过 `UClass / UStruct / UFunction` 的绑定覆盖面。本轮只补一个此前还没有横向摊开的点：**delegate 在各方案里到底被当成什么对象来治理**。这里不是“支不支持 delegate”的问题，而是“delegate 的 authority 落在签名类型、helper API、还是 proxy UObject”。

#### 各插件实现概览

```
[D2-DelegateSurface] Who Owns Delegate Identity
HZ      : UDelegateFunction scan -> DeclareDelegate/DeclareMulticastDelegate -> script value type
UC      : C# helper API -> InternalCall -> UDelegateHandler / FCSharpDelegateDescriptor
UL      : Lua function -> ULuaDelegateHandler -> FDelegateRegistry cache -> FFunctionDesc
PU      : $Delegate<T> / $MulticastDelegate<T> -> UDynamicDelegateProxy -> DynamicInvoker
SL      : ULuaDelegate UObject -> LuaVar + UFunction -> ProcessEvent
AS(now) : UDelegateFunction scan -> DeclareDelegate/DeclareMulticastDelegate -> script value type
```

- `Hazelight` 与当前 `Angelscript` 在这一层几乎仍是同一血缘：都先扫 `UDelegateFunction`，再声明成脚本值类型。对应源码分别在 `J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_Delegates.cpp:1337-1360` 和 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.cpp:1331-1385`。
- `UnrealCSharp`、`UnLua`、`puerts`、`sluaunreal` 都没有把 delegate authority 留在纯值类型里。它们最终都依赖某个 `UObject` handler / proxy 的 `ProcessEvent()` 把 UE callback 转回脚本层，只是脚本侧 facade 不同。

#### 详细对比

##### 子维度 1：脚本层看到的 delegate 是“签名类型”还是“helper API”

- 当前 `Angelscript` 与 `Hazelight` 都是 **签名化值类型**。`Bind_Delegates.cpp` 先注册 `FScriptDelegateType / FMulticastScriptDelegateType`，再枚举所有 `UDelegateFunction`，按 `sparse / multicast / single-cast` 三路声明脚本类型，并把 `FDelegateProperty / FMulticastDelegateProperty` 反查回 `SignatureFunction`。这意味着 delegate 在脚本表面是语言一等值，而不是“某个 helper 对象的副产品”。
- `UnrealCSharp` 的脚本表面是 **helper API + InternalCall**。`Reference/UnrealCSharp/Script/UE/Library/FDelegateImplementation.cs:7-29` 只暴露 `Register / Bind / IsBound / Clear / Execute*` 这类 helper 入口；真正的签名匹配和调用发生在原生侧 `UDelegateHandler` 与 `FCSharpDelegateDescriptor`。它有 typed facade，但 authority 仍在 helper。
- `UnLua` 没有生成 per-signature 的 Lua delegate type。它的 public surface 更像“把 Lua function 粘到 `ULuaDelegateHandler` 上”，再由 `FDelegateRegistry` 以 `FFunctionDesc` 做签名兼容和实参编解码。
- `puerts` 的 TypeScript 表面比 `UnLua` 更强类型，但仍不是 runtime first-class value type。`Reference/puerts/unreal/Puerts/Typing/ue/puerts.d.ts:13-26` 给出 `$Delegate<T>` / `$MulticastDelegate<T>` 泛型接口；真正持有回调关系的是 `UDynamicDelegateProxy`，不是 TS 类型本身。
- `sluaunreal` 最直接：`ULuaDelegate` 就是显式 `UObject`，内部持有 `LuaVar` 和 `UFunction*`。它没有把 delegate 再抬成和 `Hazelight / current AS` 同级的签名值类型。

[1] 当前 `Angelscript` 仍然把 delegate declaration 当成绑定系统的一等类型注册；`Hazelight` 对应 `J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_Delegates.cpp:1337-1360` 是同构实现：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.cpp
// 函数: Bind_Delegate_Declarations
// 位置: 1331-1385
// 说明: 先注册 delegate 类型，再按 UDelegateFunction 声明脚本值类型
// ============================================================================
AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_Delegate_Declarations(FAngelscriptBinds::EOrder::Early, []
{
	FAngelscriptScopeTimer Timer(TEXT("delegate declarations"));

	auto DelegateInternal = MakeShared<FScriptDelegateType>();
	FAngelscriptType::SetScriptDelegate(DelegateInternal);
	FAngelscriptType::Register(DelegateInternal);

	auto MulticastInternal = MakeShared<FMulticastScriptDelegateType>();
	FAngelscriptType::SetScriptMulticastDelegate(MulticastInternal);
	FAngelscriptType::Register(MulticastInternal);

	for (UDelegateFunction* Function : TObjectRange<UDelegateFunction>())
	{
		if (!Function->HasAnyFunctionFlags(FUNC_Delegate))
			continue;
		if (auto* SparseFunction = Cast<USparseDelegateFunction>(Function))
			DeclareSparseDelegate(SparseFunction);
		else if (Function->HasAnyFunctionFlags(FUNC_MulticastDelegate))
			DeclareMulticastDelegate(Function);
		else
			DeclareDelegate(Function);
	}

	FAngelscriptType::RegisterTypeFinder([](FProperty* Property, FAngelscriptTypeUsage& Usage) -> bool
	{
		FDelegateProperty* DelegateProperty = CastField<FDelegateProperty>(Property);
		if (DelegateProperty == nullptr)
			return false;

		auto Type = FAngelscriptType::GetByData(DelegateProperty->SignatureFunction);
		if (Type.IsValid())
		{
			Usage.Type = Type; // ★ 属性类型直接回指签名函数对应的脚本 delegate 类型
			return true;
		}
		return false;
	});
});
```

##### 子维度 2：真正拥有 callback identity 的对象是谁

- `UnrealCSharp` 把 callback identity 放在 `UDelegateHandler`。`Bind()` 时先把 `ScriptDelegate` 绑到 `FUNCTION_CSHARP_CALLBACK`，随后 `ProcessEvent()` 再用 `FCSharpDelegateDescriptor::CallDelegate()` 反射回托管方法。delegate value 只是句柄，真正 owner 是 handler + descriptor。
- `UnLua` 把 callback identity 放在 `FDelegateRegistry` 的缓存表与 `ULuaDelegateHandler` 里。`Bind()` / `Add()` 会把 `(SelfObject, LuaFunction)` 组成 key，缓存命中则复用 handler，没命中才 `CreateHandler()`。这比单纯“每次 Add 一个新 UObject”更像显式 callback registry。
- `puerts` 也有明确 proxy owner。`JsEnvImpl.cpp:2472-2519` 创建或复用 `UDynamicDelegateProxy`，把 `JsFunction` 持久化进 proxy，再用 `BindUFunction(DelegateProxy, NAME_Fire)` 把 UE delegate 指回 proxy。`DynamicDelegateProxy.cpp:25-36` 的 `ProcessEvent()` 是唯一反向入口。
- `sluaunreal` 的 owner 是 `ULuaDelegate` 本身。`bindFunction()` 保存 `LuaVar` 与 `UFunction*`，`ProcessEvent()` 直接 `callByUFunction()`。这条链比 `UnLua / puerts` 更短，但 callback 生命周期也更依赖对象本身的释放时机。
- `Hazelight / current AS` 则恰好相反：它们没有把 delegate 关系外提成 helper `UObject`。authority 仍留在脚本 delegate 值、UE delegate property 和 signature function 之间。这属于 **实现方式不同**，不是 **没有实现 handler 层**。

[2] `UnrealCSharp` 把 delegate owner 放在 `UDelegateHandler`；`UnLua` 则把 owner 拆成 `ULuaDelegateHandler + FDelegateRegistry`：

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Delegate/DelegateHandler.cpp
// 函数: UDelegateHandler::Bind / ProcessEvent
// 位置: 4-65
// 说明: C# delegate 先绑定到 callback UFUNCTION，再由 descriptor 反调托管方法
// ============================================================================
void UDelegateHandler::ProcessEvent(UFunction* Function, void* Parms)
{
	if (Function != nullptr && Function->GetName() == FUNCTION_CSHARP_CALLBACK)
	{
		if (DelegateDescriptor != nullptr)
		{
			DelegateDescriptor->CallDelegate(DelegateWrapper.Object.Get(), DelegateWrapper.Method, Parms); // ★ 回到托管方法
		}
	}
	else
	{
		UObject::ProcessEvent(Function, Parms);
	}
}

void UDelegateHandler::Bind(UObject* InObject, FMethodReflection* InMethod)
{
	if (ScriptDelegate != nullptr)
	{
		if (!ScriptDelegate->IsBound())
		{
			ScriptDelegate->BindUFunction(this, *FUNCTION_CSHARP_CALLBACK); // ★ delegate 实际绑到 handler 自己
		}
	}

	DelegateWrapper = {InObject, InMethod};
}

// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaDelegateHandler.cpp
// 函数: ULuaDelegateHandler::BindTo / AddTo / ProcessEvent
// 位置: 31-79
// 说明: UnLua 把 Lua function 附着到 handler UObject，再交给 registry 执行
// ============================================================================
void ULuaDelegateHandler::BindTo(FScriptDelegate* InDelegate)
{
	Delegate = InDelegate;
	InDelegate->BindUFunction(this, NAME_Dummy);
}

void ULuaDelegateHandler::AddTo(FMulticastDelegateProperty* InProperty, void* InDelegate)
{
	Delegate = InDelegate;
	FScriptDelegate DynamicDelegate;
	DynamicDelegate.BindUFunction(this, NAME_Dummy);
	TMulticastDelegateTraits<FMulticastDelegateType>::AddDelegate(InProperty, MoveTemp(DynamicDelegate), nullptr, InDelegate);
}

void ULuaDelegateHandler::ProcessEvent(UFunction* Function, void* Parms)
{
	if (Registry)
		Registry->Execute(this, Parms); // ★ 真正的签名编解码继续下沉到 registry
}
```

[3] `UnLua` 的 registry 会按 `(SelfObject, LuaFunction)` 复用 handler；这与 `puerts` 的 proxy reuse 思路同类，只是 key 不同：

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/DelegateRegistry.cpp
// 函数: FDelegateRegistry::Bind / Add / CreateHandler
// 位置: 168-191, 237-261, 319-325
// 说明: callback 不是临时对象，而是 registry 管的缓存项
// ============================================================================
const auto DelegatePair = FLuaDelegatePair(SelfObject, LuaFunction);
const auto Cached = CachedHandlers.Find(DelegatePair);
if (Cached && Cached->IsValid())
{
	(*Cached)->BindTo(Delegate);
	Info.Handlers.Add(*Cached);
	return;
}

lua_pushvalue(L, Index);
const auto Ref = luaL_ref(L, LUA_REGISTRYINDEX);
const auto Handler = CreateHandler(Ref, Info.Owner.Get(), SelfObject);
Handler->BindTo(Delegate);
Env->AutoObjectReference.Add(Handler);
CachedHandlers.Add(DelegatePair, Handler);

ULuaDelegateHandler* FDelegateRegistry::CreateHandler(int LuaRef, UObject* Owner, UObject* SelfObject)
{
	const auto Ret = NewObject<ULuaDelegateHandler>();
	Ret->Registry = this;
	Ret->LuaRef = LuaRef;
	Ret->SelfObject = SelfObject ? SelfObject : Owner;
	return Ret;
}
```

[4] `puerts` 与 `sluaunreal` 都依赖 proxy / object owner，但 `puerts` 额外给了 TS typed facade：

```ts
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Typing/ue/puerts.d.ts
// 位置: 13-26
// 说明: TypeScript 侧有泛型 facade，但 runtime owner 仍不是这个接口本身
// ============================================================================
interface $Delegate<T extends (...args: any) => any> {
    Bind(fn : T): void;
    Bind(target: Object, methodName: string): void;
    Unbind(): void;
    IsBound(): boolean;
    Execute(...a: ArgumentTypes<T>) : ReturnType<T>;
}

interface $MulticastDelegate<T extends (...args: any) => any> {
    Add(fn : T): void;
    Add(target: Object, methodName: string): void;
    Remove(fn : T): void;
    Broadcast(...a: ArgumentTypes<T>) : ReturnType<T>;
    Clear(): void;
}
```

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DynamicDelegateProxy.cpp
// 位置: 25-36
// 说明: 真正接 UE delegate 的是 proxy UObject
// ============================================================================
void UDynamicDelegateProxy::ProcessEvent(UFunction*, void* Params)
{
	auto PinedDynamicInvoker = DynamicInvoker.Pin();
	if (PinedDynamicInvoker && Owner.IsValid())
	{
		if (ensureAlwaysMsgf(!JsFunction.IsEmpty(), TEXT("Invalid JS Function")))
		{
			PinedDynamicInvoker->InvokeDelegateCallback(this, Params); // ★ proxy 才是 runtime callback owner
		}
	}
}

// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaDelegate.cpp
// 位置: 45-62
// 说明: slua 的 delegate owner 直接就是 ULuaDelegate 自身
// ============================================================================
void ULuaDelegate::ProcessEvent(UFunction* f, void* Parms) {
	if (luafunction == nullptr || ufunction == nullptr) {
		return;
	}
	luafunction->callByUFunction(ufunction, reinterpret_cast<uint8*>(Parms)); // ★ 没有中间 registry，直接调用 LuaVar
}

void ULuaDelegate::bindFunction(NS_SLUA::lua_State* L,int p,UFunction* ufunc) {
	luaL_checktype(L,p,LUA_TFUNCTION);
	luafunction = new NS_SLUA::LuaVar(L,p,NS_SLUA::LuaVar::LV_FUNCTION);
	ufunction = ufunc;
}
```

#### 对比矩阵（本轮新增观察点）

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 扫 `UDelegateFunction` 后生成签名化脚本 delegate 类型 | Full | Partial | None | Partial | None | Full |
| runtime helper / proxy `UObject` 持有 callback identity | None | Full | Full | Full | Full | None |
| callback 绑定对象会按函数身份缓存/复用 | None | Partial | Full | Full | None | None |
| single-cast / multicast 在脚本 API 里显式分离 | Full | Full | Partial | Full | Partial | Full |
| delegate 反调脚本依赖 `ProcessEvent` override | None | Full | Full | Full | Full | None |

#### 小结与建议

- 当前 `Angelscript` 在这一层的核心优势，不是“delegate 支持更多”，而是 **delegate authority 没有漂移到 helper UObject**。这让 delegate 行为更接近语言一等值，应按 `P0` 保持。
- 最值得吸收的是 `UnLua / puerts` 的 **callback identity cache**，尤其是 `(owner, function-ref)` 或 proxy reuse 这类去重手段。它们能减少频繁 `Add / Remove` 时的临时对象 churn，优先级 `P1`。
- `UnrealCSharp / sluaunreal` 的 handler / proxy 路线说明：当语言 runtime 自带强反射或闭包语义时，这条路可行；但对当前 `Angelscript` 来说，把 authority 从签名值类型回退到 helper UObject 没有收益，优先级 `P0`。

### [D3] UE 反向调用脚本的 owner：直接绑 delegate，还是运行时注入 `UFunction`

前文已经比较过 `RPC / BlueprintOverride` 的 wrapper 语义。本轮不重述那部分，只补另一条更底层的横向线索：**当 UE 事件、输入或 delegate 反向进入脚本时，究竟是谁拥有这个 callback slot**。这里的差异直接决定了各方案的可变更面到底落在 `delegate binding`、`UInputComponent`、还是 `UClass / UFunction` 元数据。

#### 各插件实现概览

```
[D3-ReverseInvocation] Callback Slot Owner
HZ      : ScriptCallable mixin -> FInputActionBinding.ActionDelegate = Delegate
UC      : InternalCall -> DynamicBindingObject + transient UFunction(FUNC_BlueprintEvent)
UL      : scan InputComponent -> ULuaFunction::Override(...) -> patch/add action bindings
PU      : JS fn -> UDynamicDelegateProxy::Fire
          or mixin -> generated UFunction + FUNC_BlueprintCallable|Event|Native
SL      : duplicateUFunction(...) -> BindDelegate(actor, funcName) -> patched InputComponent
AS(now) : BindAction(Action, TriggerEvent, Delegate.GetUObject(), Delegate.GetFunctionName())
```

- `Hazelight` 与当前 `Angelscript` 仍然是 **delegate-first**：callback slot 直接落在现有 dynamic delegate 上，不先新建 `UFunction`。
- `UnrealCSharp`、`UnLua`、`puerts`、`sluaunreal` 都会在某处把 callback authority 写回 UE metadata。差异只是写回的位置不同：`UC` 更偏 `UClass + DynamicBindingObject`，`UL / SL` 更偏 duplicated override `UFunction + UInputComponent`，`PU` 则是 `proxy delegate` 与 `generated Blueprint function` 双轨并存。

#### 详细对比

##### 子维度 1：输入回调接入，是复用现有 delegate，还是先造 callback slot

- 当前 `Angelscript` 的 `UEnhancedInputComponent` 绑定非常直接。`BindAction()` / `BindDebugKey()` 只把 `Delegate.GetUObject()` 与 `Delegate.GetFunctionName()` 传给 UE 原生接口，没有额外的 `UFunction` 注入步骤。这说明当前实现把 authority 留在 delegate 本身。
- `Hazelight` 旧路线同样是 direct bind。`InputComponentScriptMixinLibrary::BindAction()` 直接构造 `FInputActionBinding`，把 `AB.ActionDelegate = Delegate` 后塞回 `UInputComponent`。它与当前实现的差异不是“有没有 direct bind”，而是当前仓库把入口移到了静态 bind file，而不是 `ScriptMixinLibrary`。
- `UnrealCSharp` 走相反路线。`FRegisterInputComponent.cpp:20-36` 会先把 `UDynamicBlueprintBinding` 放进 `UBlueprintGeneratedClass::DynamicBindingObjects`；随后 `:268-290` 再创建 transient `UFunction`，打上 `FUNC_BlueprintEvent`，挂进类函数表。这不是“调用 input API”那么简单，而是先把 callback slot 写进 UE class metadata。
- `UnLua` 与 `sluaunreal` 都会扫描现有 `UInputComponent`，然后按 `ActionName_EventName` 规则补 `UFunction` override，再回写原 binding 或新增 paired binding。区别是 `UnLua` 复用 `ULuaFunction::Override()`；`slua` 明确 `duplicateUFunction()`。
- `puerts` 更分裂。delegate callback 路径走 `UDynamicDelegateProxy`；但 class / mixin 路径会在 `TypeScriptGeneratedClass` 或 `JSGeneratedClass` 里把函数打成 `BlueprintCallable | BlueprintEvent | Native`，让 callback slot 成为 generated class 的一部分。

[1] 当前 `Angelscript` 与 `Hazelight` 都保留 direct bind 语义，没有先造 transient callback `UFunction`：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UEnhancedInputComponent.cpp
// 位置: 31-45
// 说明: current AS 直接把 delegate 的 UObject / FunctionName 交给 UE 原生 BindAction
// ============================================================================
UEnhancedInputComponent_.Method(
	"FEnhancedInputActionEventBinding& BindAction(const UInputAction Action, ETriggerEvent TriggerEvent, FEnhancedInputActionHandlerDynamicSignature Delegate)",
	[](UEnhancedInputComponent& InputComponent, const UInputAction* Action, ETriggerEvent TriggerEvent, FEnhancedInputActionHandlerDynamicSignature Delegate) -> FEnhancedInputActionEventBinding&
	{
		return InputComponent.BindAction(Action, TriggerEvent, Delegate.GetUObject(), Delegate.GetFunctionName()); // ★ callback slot 直接复用 UE delegate
	});

// ============================================================================
// 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/FunctionLibraries/InputComponentScriptMixinLibrary.h
// 函数: UInputComponentScriptMixinLibrary::BindAction
// 位置: 22-27
// 说明: Hazelight 同样是 direct bind，只是入口还在 ScriptMixinLibrary
// ============================================================================
UFUNCTION(ScriptCallable)
static void BindAction(UInputComponent* Component, const FName& ActionName, EInputEvent KeyEvent, const FInputActionHandlerDynamicSignature& Delegate)
{
	FInputActionBinding AB( ActionName, KeyEvent );
	AB.ActionDelegate = Delegate;
	Component->AddActionBinding(AB); // ★ 不创建新 UFunction，直接把 delegate 写进 binding
}
```

[2] `UnrealCSharp` 会先把 binding object 和 transient `UFunction` 写进类元数据，再让 input 回调落到这个 slot：

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Domain/InternalCall/FRegisterInputComponent.cpp
// 位置: 27-36, 268-290
// 说明: 输入绑定不止是调用 API，而是先把 callback slot 写进 UBlueprintGeneratedClass / UFunction map
// ============================================================================
auto DynamicBindingObject = UBlueprintGeneratedClass::GetDynamicBindingObject(ThisClass, BindingClass);
if (DynamicBindingObject == nullptr)
{
	DynamicBindingObject = NewObject<UDynamicBlueprintBinding>(GetTransientPackage(), BindingClass);
	ThisClass->DynamicBindingObjects.Add(DynamicBindingObject); // ★ binding object 进入 class metadata
}

const auto Function = NewObject<UFunction>(InClass, *InFunctionName, EObjectFlags::RF_Transient);
Function->FunctionFlags = FUNC_BlueprintEvent;
InProperty(Function);
Function->Bind();
Function->StaticLink(true);
InClass->AddFunctionToFunctionMap(Function, *InFunctionName); // ★ callback slot 被写进函数表
Function->Next = InClass->Children;
InClass->Children = Function;

FCSharpEnvironment::GetEnvironment().GetBind()->Bind(
	FCSharpEnvironment::GetEnvironment().GetRegistry<FClassRegistry>()->GetClassDescriptor(InClass),
	InClass,
	Function);
```

##### 子维度 2：谁会主动改写已有 `UInputComponent` / generated class

- `UnLua` 的 input hook 是显式扫描与补绑。`UUnLuaManager::ReplaceActionInputs()` 会遍历既有 `ActionBindings`，命中 Lua 函数名就 `ULuaFunction::Override()`，并在缺少 paired action 时自动补一个新的 `FInputActionBinding`。这比 direct bind 更激进，但也更适合“接管现有蓝图输入图”。
- `sluaunreal` 走的是同类路线，只是 owner 更偏 duplicated `UFunction`。`duplicateUFunction()` 先复制模板函数，再 `overrideActionInputs()` 里把它绑回 actor 的 input binding。这里的 authority 明显落在 patched class metadata。
- `puerts` 既有 proxy delegate 路线，也有 generated class 路线。`JsEnvImpl.cpp:2472-2510` 会创建 `UDynamicDelegateProxy` 并 `BindUFunction(DelegateProxy, NAME_Fire)`；而 `:1578-1581` 又把 TypeScript-generated functions 打上 `FUNC_BlueprintCallable | FUNC_BlueprintEvent | FUNC_Public | FUNC_Native`。它是本轮里唯一明确双轨的方案。
- 当前 `Angelscript / Hazelight` 没有进入这类“扫描已有 binding 然后批量改写”的状态机。它们更像给脚本作者一条 **稳定、显式、低 mutation** 的 callback 接入面。

[3] `UnLua` 与 `sluaunreal` 都会回写现有 `UInputComponent`，但一个靠 `Override()`，一个靠 `duplicateUFunction()`：

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaManager.cpp
// 函数: UUnLuaManager::ReplaceActionInputs
// 位置: 367-417
// 说明: UnLua 扫描现有 ActionBinding，命中后 override，并自动补 paired binding
// ============================================================================
for (int32 i = 0; i < NumActionBindings; ++i)
{
	FInputActionBinding &IAB = InputComponent->GetActionBinding(i);
	FName FuncName = FName(*FString::Printf(TEXT("%s_%s"), *ActionName, SReadableInputEvent[IAB.KeyEvent]));
	if (LuaFunctions.Find(FuncName))
	{
		ULuaFunction::Override(InputActionFunc, Class, FuncName); // ★ 先给类补 callback slot
		IAB.ActionDelegate.BindDelegate(Actor, FuncName);         // ★ 再改回现有 binding
	}

	if (!IS_INPUT_ACTION_PAIRED(IAB))
	{
		...
		ULuaFunction::Override(InputActionFunc, Class, FuncName);
		FInputActionBinding AB(Name, IE);
		AB.ActionDelegate.BindDelegate(Actor, FuncName);
		InputComponent->AddActionBinding(AB); // ★ 缺配对时主动补 binding
	}
}

// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaOverrider.cpp
// 函数: duplicateUFunction / overrideActionInputs
// 位置: 1110-1145, 1651-1698
// 说明: slua 把 input 回调显式复制成新 UFunction，再回写 ActionBinding
// ============================================================================
UFunction* duplicateUFunction(UFunction* templateFunction, UClass* outerClass, FName newFuncName, FNativeFuncPtr nativeFunc)
{
	if (templateFunction->HasAnyFunctionFlags(FUNC_Native))
	{
		outerClass->AddNativeFunction(*newFuncName.ToString(), nativeFunc);
	}

	FObjectDuplicationParameters duplicationParams(templateFunction, outerClass);
	duplicationParams.DestName = newFuncName;
	UFunction* newFunc = Cast<UFunction>(StaticDuplicateObjectEx(duplicationParams));
	outerClass->AddFunctionToFunctionMap(newFunc, newFuncName); // ★ duplicated callback slot 进入类函数表
	return newFunc;
}

if (luaFunctions.Find(funcName))
{
	auto inputFunc = duplicateUFunction(inputActionFunc, actorClass, funcName, (FNativeFuncPtr)&ULuaOverrider::luaOverrideFunc);
	inputActionBinding.ActionDelegate.BindDelegate(actor, funcName);
}
...
auto inputFunc = duplicateUFunction(inputActionFunc, actorClass, funcName, (FNativeFuncPtr)&ULuaOverrider::luaOverrideFunc);
FInputActionBinding inputActionBinding(actionName, InputEvents[i]);
inputActionBinding.ActionDelegate.BindDelegate(actor, funcName);
inputComponent->AddActionBinding(inputActionBinding);
```

[4] `puerts` 的 callback owner 分成 proxy delegate 与 generated class 两轨：

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp
// 位置: 2472-2510, 1578-1581
// 说明: delegate 路线靠 proxy；class/mixin 路线靠 generated function flags
// ============================================================================
DelegateProxy = NewObject<UDynamicDelegateProxy>();
DelegateProxy->Owner = Iter->second.Owner;
DelegateProxy->SignatureFunction = Iter->second.SignatureFunction;
DelegateProxy->DynamicInvoker = DynamicInvoker;
Iter->second.Proxy = DelegateProxy;
...
DelegateProxy->JsFunction = v8::UniquePersistent<v8::Function>(Isolate, v8::Local<v8::Function>::Cast(Apply));

FScriptDelegate Delegate;
Delegate.BindUFunction(DelegateProxy, NAME_Fire); // ★ delegate callback 进入 proxy::Fire

Function->FunctionFlags |= FUNC_BlueprintCallable | FUNC_BlueprintEvent | FUNC_Public | FUNC_Native;
Function->SetNativeFunc(&UTypeScriptGeneratedClass::execLazyLoadCallJS);
TypeScriptGeneratedClass->AddNativeFunction(*Function->GetName(), &UTypeScriptGeneratedClass::execLazyLoadCallJS); // ★ generated class 轨
```

#### 对比矩阵（本轮新增观察点）

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 直接用现有 delegate target/function 接入 callback，不先造 `UFunction` | Full | None | None | Partial | None | Full |
| 运行时会新建/复制 `UFunction` 作为 callback slot | None | Full | Full | Full | Full | None |
| 会主动遍历并改写现有 `UInputComponent` bindings | None | None | Full | None | Full | None |
| 缺失的 paired input binding 会自动补齐 | None | None | Full | None | Full | None |
| callback authority 主要落在 mutable class metadata 而不是稳定 delegate 值 | None | Full | Full | Full | Full | None |

#### 小结与建议

- 当前 `Angelscript` 在这一维最重要的优势，是 **callback authority 仍主要落在 delegate 值与原生 binding API**，而不是运行时可变的 `UFunction` / `UClass` 元数据。这条低 mutation 路线建议按 `P0` 保持。
- 最值得吸收的是 `UnLua / sluaunreal` 的 **可选自动接管现有 input graph** 能力，尤其是“扫描已有 binding + 自动补 paired action”这层工程化入口。它适合作为 editor-side 或 opt-in runtime helper，优先级 `P1`。
- `UnrealCSharp / puerts` 证明“把 callback slot 写回 class metadata”在需要 Blueprint-visible generated class 时很有价值；但这更像特定模式的能力，不应成为当前 `Angelscript` 的默认 callback authority，优先级 `P2`。

### [D6] 类型声明 contract 的归属：文本提示、structured artifact，还是直接改写编译产物

前文已经比较过 `.d.ts / IntelliSense / generated .hpp` 的数量差异。本轮不再数文件，而是补更关键的一层：**这些产物到底只是给人和 IDE 看，还是已经进入宿主编译/装载 contract**。这条线能直接区分“文档资产”“提示资产”和“真正影响编译单元的生成资产”。

#### 各插件实现概览

```
[D6-TypeContract] Artifact Ownership
HZ      : runtime docs dump -> Docs/angelscript/generated/*.hpp
UC      : Roslyn SourceGenerator -> *.gen.cs -> Fody Weaver injects GC/path/property logic
UL      : Editor generator / commandlet -> Intermediate/IntelliSense/*.lua (write-if-changed)
PU      : DeclarationGenerator -> Typing/ue/ue.d.ts + ue_bp.d.ts + version restore cache
SL      : external lua-wrapper.exe(libclang) + pre-generated wrapper files
AS(now) : UHT summary JSON/CSV + runtime docs dump -> Docs/angelscript/generated/*.hpp
```

- `Hazelight` 与当前 `Angelscript` 的共同点，是都保留了 `Docs/angelscript/generated/*.hpp` 这条 runtime doc dump 链；差别在于当前仓库又叠加了 `AngelscriptUHTTool` 的 `AS_FunctionTable_Summary.json` / `ModuleSummary.csv`，开始出现真正 machine-readable 的账本。
- `UnrealCSharp` 是本轮里最特殊的：它不只是生成文本文件，而是 **先用 SourceGenerator 生成 `.gen.cs`，再用 Weaver 改写编译后的 managed type**。也就是说，它的 type contract 已经进入 C# 编译产物本身。
- `UnLua` 与 `puerts` 都主要生成 IDE / compiler-facing 文本资产，但 freshness contract 不同：`UnLua` 偏 AssetRegistry + write-if-changed；`puerts` 偏 `ue_bp.d.ts` version restore + `BlueprintTypeDeclInfoCache`。
- `sluaunreal` 的代码生成 contract 仍明显在插件外。仓内只保留 runtime/editor module，静态导出工具还是 `Tools/lua-wrapper.exe + libclang` 这条外部链路。

#### 详细对比

##### 子维度 1：产物只是“给人看”，还是已经进入宿主编译 contract

- `Hazelight` 的 D6 主产物仍是人读 API 文档。`AngelscriptDocs.cpp` 把 `ClassDoc` 落成 `Docs/angelscript/generated/<Class>.hpp`，它更像 generated reference header，而不是编译输入。
- 当前 `Angelscript` 在这个基础上多了一层 build-time 账本。`AngelscriptFunctionTableCodeGenerator.cs:166-225` 会把 direct bind / stub / shard 数量写进 `AS_FunctionTable_Summary.json` 与 `AS_FunctionTable_ModuleSummary.csv`。这仍不是 IDE type declaration，但已经是 machine-readable build artifact。
- `UnrealCSharp` 明显更重。`UnrealTypeSourceGenerator.cs:104-207` 直接 `Context.AddSource(...".gen.cs")`；`UnrealTypeWeaver.cs:54-78,183-220` 又在编译后补 `GarbageCollectionHandle` property、register / unregister 所需 IL。它不是“多了一份提示文件”，而是 **编译单元被改写了**。
- `UnLua` 的 `.lua` IntelliSense 文件与 `puerts` 的 `.d.ts` 都更偏语言工具链输入。不同的是 `UnLua` 只承诺 hint / stub，`puerts` 的 `.d.ts` 直接进入 TypeScript type-checking contract。
- `sluaunreal` 的 `lua-wrapper` 说明了另一条路：essential gap-filling codegen 仍在外部 tool，且只补“反射和 LuaCppBinding 都够不到的 USTRUCT 缺口”。这里应判为 **实现位置不同**，不是 **完全没有 D6 能力**。

[1] 当前 `Angelscript` 在 `Hazelight` 的文档导出链上，又叠加了一层 structured UHT summary：

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs
// 函数: WriteGenerationSummary / WriteModuleSummaryCsv
// 位置: 166-225
// 说明: current AS 不只导出 docs，还把 binding 覆盖率写成 machine-readable artifact
// ============================================================================
int totalGeneratedEntries = moduleSummaries.Sum(static summary => summary.TotalEntries);
int totalDirectBindEntries = moduleSummaries.Sum(static summary => summary.DirectBindEntries);
int totalStubEntries = moduleSummaries.Sum(static summary => summary.StubEntries);

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
			shardCount = summary.ShardCount,
		}),
	},
	new JsonSerializerOptions { WriteIndented = true });

File.WriteAllText(summaryPath, summaryJson, Encoding.UTF8); // ★ 账本进入 build artifact
WriteModuleSummaryCsv(factory, moduleSummaries);
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp
// 位置: 675-710
// 说明: current AS 仍保留 Hazelight 血缘里的 generated .hpp API docs
// ============================================================================
for (auto It : Classes)
{
	FDocClass& ClassDoc = It.Value;
	...
	FString Filename = FPaths::ProjectDir() / TEXT("/Docs/angelscript/generated") / ClassDoc.ClassName + TEXT(".hpp");
	...
	Content += FString::Printf(TEXT("/* Class: %s \n %s */ \n class %s"),
		*ClassDoc.ClassName, *ClassDoc.Documentation, *ClassDoc.ClassName);
}
```

[2] `Hazelight` 只有 docs dump；`UnrealCSharp` 则直接把 contract 推进编译单元与 IL：

```cpp
// ============================================================================
// 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/AngelscriptDocs.cpp
// 位置: 668-705
// 说明: Hazelight 的 D6 产物仍主要是 generated .hpp 文档
// ============================================================================
for (auto It : Classes)
{
	FDocClass& ClassDoc = It.Value;
	...
	FString Filename = FPaths::ProjectDir() / TEXT("/Docs/angelscript/generated") / ClassDoc.ClassName + TEXT(".hpp");
	...
	Content += FString::Printf(TEXT("/* Class: %s \n %s */ \n class %s"),
		*ClassDoc.ClassName, *ClassDoc.Documentation, *ClassDoc.ClassName);
}
```

```csharp
// ============================================================================
// 文件: Reference/UnrealCSharp/Script/SourceGenerator/UnrealTypeSourceGenerator.cs
// 位置: 104-207
// 说明: 先生成 .gen.cs，再把结构体的 GC / StaticStruct 约束编进源码
// ============================================================================
Context.AddSource(@interface.Name + ".gen.cs", source);
...
if (type.Value.DynamicType == EDynamicType.UStruct)
{
	var interfaceBody = type.Value.HasBase || type.Value.HasGarbageCollectionHandle
		? ": IStaticStruct"
		: ": IStaticStruct, IGarbageCollectionHandle";
	...
	if (type.Value.HasGarbageCollectionHandle == false && type.Value.HasBase == false)
	{
		source += "\t\tpublic nint GarbageCollectionHandle { get; set; }\n"; // ★ 这已经不是 IDE hint，而是编译源码
	}
	...
	Context.AddSource(type.Value.NameSpace + "." + type.Value.Name + ".gen.cs", source);
}

// ============================================================================
// 文件: Reference/UnrealCSharp/Script/Weavers/UnrealTypeWeaver.cs
// 位置: 54-78, 183-220
// 说明: 再用 Weaver 改写已编译类型，补 GCHandle backing field / getter / setter
// ============================================================================
public override void Execute()
{
	GetAllMeta();
	GetAllDynamic();
	...
	_structTypes.ForEach(ProcessStructGarbageCollectionHandle);
	_structTypes.ForEach(ProcessStructRegister);
	_classTypes.ForEach(ProcessUClassType);
	_structTypes.ForEach(ProcessUStructType);
}

private void ProcessStructGarbageCollectionHandle(TypeDefinition Type)
{
	if (Type.Properties.Any(Property => Property.Name == "GarbageCollectionHandle"))
	{
		return;
	}

	var garbageCollectionHandle = new PropertyDefinition("GarbageCollectionHandle", PropertyAttributes.None,
		ModuleDefinition.TypeSystem.IntPtr);
	var garbageCollectionHandleBackingField = new FieldDefinition("<GarbageCollectionHandle>k__BackingField",
		FieldAttributes.Private, ModuleDefinition.TypeSystem.IntPtr);
	...
	Type.Methods.Add(getter);
	...
}
```

##### 子维度 2：freshness contract 是怎么保证的

- `UnLua` 的 freshness contract 很清晰：`Initialize()` 把 `AssetRegistry` 的 `OnAssetAdded / Removed / Renamed / Updated` 都挂上；`SaveFile()` 则只在内容变化时写盘。它不是单纯“能导出 IntelliSense”，而是明确避免无意义 churn。
- `puerts` 的 freshness 更重版本恢复。`DeclarationGenerator.cpp:564-685` 会先从旧的 `ue_bp.d.ts` 恢复 `BlueprintTypeDeclInfoCache`，再用 package version 判断 `Changed / IsExist / FileVersionString`。这让 `.d.ts` 生成可以带着历史 state 增量前进。
- 当前 `Angelscript` 的 `AS_FunctionTable_Summary.json / csv` 也是 build-time freshness contract，但它还没有像 `UnLua / puerts` 那样把“是否有必要重写 artifact”工程化成统一 schema。
- `sluaunreal` 的 freshness 仍依赖外部人工触发 `lua-wrapper`。`Tools/README.md:35-42` 明说要修改 `config*.json` 后再导出；而 `slua_unreal.uplugin:16-24` 也说明插件自身模块图里并没有等价的 codegen module / program。这里应判为 **有能力，但集成位置更外置**。

[3] `UnLua` 的 IntelliSense 走 editor watcher + write-if-changed；`puerts` 的 `.d.ts` 则走 version restore：

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp
// 位置: 42-56, 222-233
// 说明: UnLua 用 AssetRegistry watcher + 写前比对保证产物新鲜度
// ============================================================================
void FUnLuaIntelliSenseGenerator::Initialize()
{
	if (bInitialized)
		return;

	OutputDir = IPluginManager::Get().FindPlugin("UnLua")->GetBaseDir() + "/Intermediate/IntelliSense";

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	AssetRegistryModule.Get().OnAssetAdded().AddRaw(this, &FUnLuaIntelliSenseGenerator::OnAssetAdded);
	AssetRegistryModule.Get().OnAssetRemoved().AddRaw(this, &FUnLuaIntelliSenseGenerator::OnAssetRemoved);
	AssetRegistryModule.Get().OnAssetRenamed().AddRaw(this, &FUnLuaIntelliSenseGenerator::OnAssetRenamed);
	AssetRegistryModule.Get().OnAssetUpdated().AddRaw(this, &FUnLuaIntelliSenseGenerator::OnAssetUpdated);
}

void FUnLuaIntelliSenseGenerator::SaveFile(const FString& ModuleName, const FString& FileName, const FString& GeneratedFileContent)
{
	const FString FilePath = FString::Printf(TEXT("%s/%s.lua"), *Directory, *FileName);
	FString FileContent;
	FFileHelper::LoadFileToString(FileContent, *FilePath);
	if (FileContent != GeneratedFileContent)
		FFileHelper::SaveStringToFile(GeneratedFileContent, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM); // ★ 避免无意义重写
}

// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp
// 位置: 64-133
// 说明: 同一套生成器既可命令行批量跑，也可选带 Blueprint IntelliSense
// ============================================================================
Pair.Value->GenerateIntelliSense(GeneratedFileContent);
SaveFile(ModuleName, Pair.Key, GeneratedFileContent);
...
if (ParamsMap.Contains(BPKey) && ParamsMap[BPKey] == TEXT("1"))
{
	auto Generator = FUnLuaIntelliSenseGenerator::Get();
	Generator->Initialize();
	Generator->UpdateAll();
}
```

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp
// 位置: 420-457, 568-685
// 说明: puerts 的 .d.ts 生成带 version restore，支持增量恢复 Blueprint decl cache
// ============================================================================
PlatformFile.DeleteFile(*(PuertsBaseDir / TEXT("Typing/ue/ue.d.ts")));
PlatformFile.DeleteFile(*(PuertsBaseDir / TEXT("Typing/ue/ue_bp.d.ts")));
...
const FString UEDeclarationFilePath = FPaths::ProjectDir() / TEXT("Typing/ue/ue.d.ts");
FFileHelper::SaveStringToFile(ToString(), *UEDeclarationFilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
...
const FString BPDeclarationFilePath = FPaths::ProjectDir() / TEXT("Typing/ue/ue_bp.d.ts");
FFileHelper::SaveStringToFile(ToString(), *BPDeclarationFilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

void FTypeScriptDeclarationGenerator::RestoreBlueprintTypeDeclInfos(const FString& FileContent, bool InGenFull)
{
	...
	FString FileVersionString = FileContent.Mid(Pos + Start.Len(), VersionInfoEnd - Pos - Start.Len());
	...
	if (BlueprintTypeDeclInfoPtr)
	{
		BlueprintTypeDeclInfoPtr->NameToDecl.Add(TypeName, TypeDecl);
	}
	else
	{
		bool bIsExist = InGenFull ? false : bIsAssociation;
		BlueprintTypeDeclInfoCache.Add(
			FName(*PackageName), {NameToDecl, FileVersionString, bIsExist, true, bIsAssociation}); // ★ 旧产物先恢复成 cache
	}
}

...
BlueprintTypeDeclInfoPtr->Changed = InGenFull || (FileVersion != BlueprintTypeDeclInfoPtr->FileVersionString); // ★ package version 决定是否重写
```

#### 对比矩阵（本轮新增观察点）

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 会生成面向人读 / IDE 的文本声明或文档产物 | Full | Full | Full | Full | Partial | Full |
| 仓内有独立 structured artifact（`json/csv` 或 versioned cache） | None | None | None | Full | None | Full |
| post-gen rewrite / weaving 会直接改写编译产物 | None | Full | None | None | None | None |
| freshness 逻辑在源码中显式可见 | None | Full | Full | Full | Partial | Partial |
| essential gap-filling codegen 依赖插件外手工工具 | None | None | None | None | Full | None |

#### 小结与建议

- 当前 `Angelscript` 在 D6 上的新增价值，不只是“文档更多”，而是 **已经同时拥有 human-readable docs 与 machine-readable generation summary**。这条双产物路线值得按 `P0` 保持。
- 最值得吸收的是 `UnLua / puerts` 的 freshness contract：前者的 `write-if-changed`，后者的 `version restore cache`。当前 `Angelscript` 最该补的是把 `docs / summary / debug database` 收束成统一 versioned schema，优先级 `P1`。
- `UnrealCSharp` 的 `SourceGenerator + Weaver` 说明最强的 type contract 可以直接进入编译单元；但这条路对当前 `Angelscript` 过重。值得吸收的是“单一 schema 驱动多产物”的纪律，不是照搬 IL weaving，优先级 `P2`。
- `sluaunreal` 的 `lua-wrapper` 提醒了一个反例：**不要让 essential D6 contract 脱离插件内流程，变成外部手工工具的责任**。current AS 应避免往这条路回退，优先级 `P0`。

---

## 深化分析 (2026-04-09 00:08:53)

### [D1] 版本兼容 owner：dedicated module、shared shim header，还是散落在 bind/runtime 内联分支

前文 D1 已经比较过模块数量与 ThirdParty 边界；这一轮不重复“模块多不多”，只补一个更容易长期拉开维护成本的轴：**UE 版本漂移到底由谁统一承担**。同样都是兼容 UE 5.x，有的插件把这件事提升成第一等模块，有的做成共享 shim header，有的则直接把版本差异物化成分片生成文件；当前 `Angelscript` / `Hazelight` 则更像“谁碰到 API 漂移，谁就在本地文件里写 `#if`”。

#### 各插件实现概览

```
[D1-VersionOwner] Compatibility Knowledge Placement
UC      : CrossVersion module -> UEVersion.h feature flags -> referenced by editor/core/compiler
UL      : UnLuaCompatibility.h -> macro/type alias shim -> runtime files include one shared owner
PU      : UECompatible.h + many inline #if in JsEnv/Editor/Generator
SL      : LuaWrapper.h -> select LuaWrapper5.xHead.inc shard by engine version
HZ/AS   : inline #if inside Bind_*.cpp / Manager / Engine, no dedicated compatibility owner
```

#### 详细对比

##### 子维度 1：兼容 owner 有没有进入模块图

- `UnrealCSharp` 是六个方案里最“显式”的。`CrossVersion` 不只是一个公共头，而是直接进入 `UnrealCSharpEditor.Build.cs:37-63` 的私有依赖图；兼容知识随后被编码进 `CrossVersion/Public/UEVersion.h:5-110` 的 feature flag 宏。这意味着它把“版本差异”提升成了能被整个工具链显式依赖的模块 contract。
- `UnLua` 没有再单拆一个 `CrossVersion` module，但它把大量 UE 差异集中进 `Private/UnLuaCompatibility.h:15-149`。这里既有 `FProperty/UProperty` 时代差异，也有 delegate、`FindFirstObject`、输入绑定等行为兼容。它不是模块级 owner，而是**共享 shim header owner**。
- `puerts` 有一个较薄的 `UECompatible.h:17-57`，负责 `FUETicker`、`FUEObjectIterator`、`FindAnyType()` 一类通用差异，但真正的版本漂移并没有被完全收束进去。源码里仍能看到 `JsEnv.Build.cs`、`JSGeneratedClass.cpp`、`PEBlueprintAsset.cpp`、`DeclarationGenerator.cpp` 等多点内联 `#if`。因此它更像“有一个小公共门面，但主责任仍分散”。
- `sluaunreal` 走的是另一条路：`LuaWrapper.h:21-38` 不是只做宏兼容，而是直接按引擎版本选择 `LuaWrapper4.18Head.inc`、`LuaWrapper5.1Head.inc`、`LuaWrapper5.4Head.inc` 等不同 shard。这里的版本漂移已经被**物化成多个生成分片**，优点是生成产物清晰，代价是版本面一扩张，文件矩阵会迅速变厚。
- 当前 `Angelscript` 与 `Hazelight` 在这条轴上都还停在“局部 inline guard”阶段。`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FMath.cpp:70-80`、`J:/UnrealEngine/UEAS2/.../Bind_FMath.cpp:71-81` 与 `J:/UnrealEngine/UEAS2/.../AngelscriptManager.cpp:227-231` 都说明它们有兼容实现，但 owner 没有被提升成共享 shim header 或独立模块。这里应判为 **实现方式不同**，不是 **没有实现兼容**。

##### 子维度 2：兼容知识被表达成什么

- `UnrealCSharp` 把兼容语义抽成 feature flag，而不是原始 `ENGINE_MINOR_VERSION` 判断。`UE_FIELD_NOTIFICATION`、`UE_F_OPTIONAL_PROPERTY`、`UE_T_BASE_STRUCTURE_F_FRAME_RATE` 这一类 flag 让调用点只需要问“特性有没有”，不必每次重新理解版本区间。
- `UnLua` 的 `UnLuaCompatibility.h` 更像传统 shim layer：它通过 `typedef`、`#define` 和 traits 包装把新旧 UE API 抹平成统一名字，例如 `FProperty/UProperty`、`GetChildProperties()`、`FMulticastDelegateType`。这比 scattered `#if` 更容易阅读，但仍然是“语法面兼容”，不是 feature table。
- `puerts` 的 `UECompatible.h` 只收敛最通用的一层，真正细节还是由每个 subsystem 自己判断版本。例如对象查找、iterator、Blueprint metadata、generated class 等都保留本地 `#if`。这让 subsystem 可以自定义差异，但全局盘点版本断点会更难。
- `sluaunreal` 则把“兼容知识”直接烘焙进 version-specific wrapper shards。它不是在 runtime 里解释差异，而是在包含哪个 `LuaWrapper*.inc` 时就已经决定“这一版引擎该暴露什么包装层”。
- 当前 `Angelscript` / `Hazelight` 还没有一个类似 `UnLuaCompatibility.h` 或 `UEVersion.h` 的统一 owner。优势是修改单点绑定时很直接；代价是当同一类 UE 漂移同时影响多个 bind 文件时，修复模式往往会复制扩散。

[1] `UnrealCSharp` 把兼容层正式提升成模块依赖，再由 feature flag 宏供全链路消费：

```csharp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/UnrealCSharpEditor.Build.cs
// 位置: 37-63
// 说明: CrossVersion 不是“顺手 include 的公共头”，而是 editor 模块的正式依赖
// ============================================================================
PrivateDependencyModuleNames.AddRange(
	new string[]
	{
		"ScriptCodeGenerator",
		"Compiler",
		"UnrealCSharpCore",
		"CrossVersion", // ★ 版本兼容 owner 进入模块图
	}
);
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/CrossVersion/Public/UEVersion.h
// 位置: 5-18, 94-110
// 说明: compatibility 不再以散落的 ENGINE_MINOR_VERSION 判断存在，而是变成 feature flag 表
// ============================================================================
#define UE_VERSION_START(MajorVersion, MinorVersion, PatchVersion) \
	UE_GREATER_SORT(ENGINE_MAJOR_VERSION, MajorVersion, UE_GREATER_SORT(ENGINE_MINOR_VERSION, MinorVersion, UE_GREATER_SORT(ENGINE_PATCH_VERSION, PatchVersion, true)))

#define UE_T_BASE_STRUCTURE_F_INT_POINT !UE_VERSION_START(5, 1, 0)
#define UE_U_CLASS_ADD_REFERENCED_OBJECTS !UE_VERSION_START(5, 1, 0)
...
#define UE_FIELD_NOTIFICATION UE_VERSION_START(5, 3, 0)
#define UE_F_OPTIONAL_PROPERTY UE_VERSION_START(5, 3, 0)
#define UE_E_CONTENT_BROWSER_ITEM_FLAGS_CATEGORY_PLUGIN UE_VERSION_START(5, 4, 0)
#define UE_T_BASE_STRUCTURE_F_FRAME_RATE !UE_VERSION_START(5, 5, 0)
// ★ 调用点以后问“特性是否存在”，而不是重新编码一遍版本区间
```

[2] `UnLua / puerts / sluaunreal` 分别代表 shared shim、thin facade 与 version shard：

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaCompatibility.h
// 位置: 15-38, 80-90
// 说明: UnLua 把大量 UE API 漂移集中到一个共享 shim header
// ============================================================================
#include "Runtime/Launch/Resources/Version.h"
#include "Misc/EngineVersionComparison.h"

#if ENGINE_MAJOR_VERSION <= 4 && ENGINE_MINOR_VERSION < 20
#define GET_INPUT_ACTION_NAME(IAB) IAB.ActionName
#define IS_INPUT_ACTION_PAIRED(IAB) IAB.bPaired
#else
#define GET_INPUT_ACTION_NAME(IAB) IAB.GetActionName()
#define IS_INPUT_ACTION_PAIRED(IAB) IAB.IsPaired()
#endif

#if ENGINE_MAJOR_VERSION < 5
typedef float unluaReal;
#else
typedef double unluaReal;
#endif
// ★ 同类差异尽量收敛到一个 header，再由 runtime 代码复用

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Public/UECompatible.h
// 位置: 17-50
// 说明: puerts 有一个通用 facade，但没有把所有版本漂移都收束进来
// ============================================================================
FORCEINLINE bool UEObjectIsPendingKill(const UObject* Test)
{
#if ENGINE_MAJOR_VERSION > 4
	return !IsValid(Test) || Test->IsUnreachable();
#else
	return Test->IsPendingKillOrUnreachable();
#endif
}

#if ENGINE_MAJOR_VERSION > 4
#define FUETicker FTSTicker
#else
#define FUETicker FTicker
#endif

template <typename T>
T* FindAnyType(const TCHAR* InShortName)
{
#if (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 1) || ENGINE_MAJOR_VERSION > 5
	return FindFirstObject<T>(InShortName, EFindFirstObjectOptions::EnsureIfAmbiguous | EFindFirstObjectOptions::NativeFirst, ELogVerbosity::Error);
#else
	return FindObject<T>(ANY_PACKAGE, InShortName);
#endif
}
// ★ 这里只兜底最通用的差异；其余 subsystem 仍大量保留本地 #if

// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaWrapper.h
// 位置: 21-38
// 说明: slua 把版本差异直接物化成 wrapper shard 选择
// ============================================================================
#if ((ENGINE_MINOR_VERSION<25) && (ENGINE_MAJOR_VERSION==4))
	#include "LuaWrapper4.18Head.inc"
#elif ((ENGINE_MINOR_VERSION>=25) && (ENGINE_MAJOR_VERSION==4))
	#include "LuaWrapper4.25Head.inc"
#elif ((ENGINE_MINOR_VERSION==1) && (ENGINE_MAJOR_VERSION==5))
	#include "LuaWrapper5.1Head.inc"
#elif ((ENGINE_MINOR_VERSION==2) && (ENGINE_MAJOR_VERSION==5))
	#include "LuaWrapper5.2Head.inc"
#elif ((ENGINE_MINOR_VERSION>=3) && (ENGINE_MAJOR_VERSION==5))
	...
#endif
// ★ 兼容结果直接进入不同的生成分片，而不是统一 shim API
```

[3] 当前 `Angelscript / Hazelight` 的 owner 仍在 bind/runtime 局部内联：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FMath.cpp
// 位置: 68-80
// 说明: current AS 的 UE 版本差异直接留在具体 bind 文件
// ============================================================================
FAngelscriptBinds::BindGlobalFunction("bool IsPowerOfTwo(int32 Value) no_discard", FUNC_TRIVIAL(FMath::IsPowerOfTwo<int32>));

#if ENGINE_MAJOR_VERSION >= 5
FAngelscriptBinds::BindGlobalFunction("float64 SmoothStep(float64 A, float64 B, float64 X) no_discard", FUNCPR_TRIVIAL(double, FMath::SmoothStep, (double, double, double)));
#endif
// ★ 哪个 API 只在 UE5 存在，就在本 bind 文件直接写分支

// ============================================================================
// 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/AngelscriptManager.cpp
// 位置: 227-231
// 说明: Hazelight 也同样以 runtime 局部 #if 处理版本差异
// ============================================================================
#if WITH_EDITOR && ENGINE_MAJOR_VERSION >= 5
asSetResolveObjectPtrFunction(&AngelscriptResolveObjectPtr);
#endif
// ★ 兼容语义没有被抽到单独 module/header，而是跟着调用点走
```

#### 对比矩阵（本轮新增观察点）

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 专用 compatibility module 进入依赖图 | None | Full | None | None | None | None |
| 共享 shim header 统一承担大部分 UE 漂移 | None | Partial | Full | Partial | None | None |
| 版本差异被物化成 version-sharded generated/include 产物 | None | None | None | None | Full | None |
| 兼容知识主要散落在 runtime / bind 本地 `#if` | Full | Partial | Partial | Full | Partial | Full |
| 调用点消费的是 feature flag，而不是裸版本判断 | None | Full | Partial | Partial | None | None |

#### 小结与建议

- 当前 `Angelscript` 在这一维**不是没有兼容层**，而是兼容 owner 仍然分散。与 `Hazelight` 一样，谁遇到 API 变化，谁就在本地 `#if` 修补；这在小变更上高效，但在跨多个 bind family 的版本漂移上不利于盘点，优先级判断应为 `P1`。
- 最值得吸收的是 `UnrealCSharp` 的“feature flag table”思路，但不需要一步走到完整 `CrossVersion` module。更现实的做法是先新增一个 plugin 内共享 compatibility header，把当前最常见的 `FProperty`、iterator、`FindFirstObject`、input/delegate 差异统一进去，优先级 `P1`。
- `sluaunreal` 的 version shard 方案提醒了另一面：如果未来 `AngelscriptUHTTool` 生成物越来越多，版本兼容不要完全靠文件分叉去扛，否则 shard 组合会迅速膨胀，优先级 `P0` 避免。

### [D5] 异常 surfacing contract：谁拼最终报错文本，谁把它送到 UE / debugger / CLR

前文 D5 已比较协议、source identity 和变量模型；这一轮只补“脚本出错后，**最后那段开发者真正看到的错误文本**是谁拥有”的横向差异。这个点会直接决定：能不能带调用栈、能不能进 UE 日志、能不能同步进 debugger、能不能让宿主自定义错误呈现。

#### 各插件实现概览

```
[D5-ExceptionSurface] Fault Path
HZ/AS : VM exception callback -> UE_LOG + screen/devEnsure -> DebugServer stop(reason=exception)
UC    : mono_runtime_invoke -> mono_unhandled_exception -> host pulls managed StackTrace via Utils.GetTraceback()
UL    : lua_pcall -> ReportLuaCallError -> delegate override or default luaL_traceback -> UE_LOG
PU    : V8 TryCatch -> TryCatchToString -> Logger->Error / CurrentStackTrace
SL    : pushErrorHandler -> luaL_traceback -> onError delegate / Log::Error
```

#### 详细对比

##### 子维度 1：最终错误字符串是谁组装的

- 当前 `Angelscript` 把异常 surfacing 明确放在插件 engine 层。`AngelscriptEngine.cpp:5242-5310` 中，`LogAngelscriptException()` 先消费 VM 给出的 `GetExceptionString()`，再补 UE callstack、`PrintString`、`devEnsure`，最后把同一份 `Context` 继续送进 `DebugServer::ProcessException()`。这意味着**最终错误文本的 owner 是插件 engine，而不是纯 AngelScript VM**。
- `Hazelight` 基本同构。`AngelscriptManager.cpp:3805-3844` 和 `Debugging/AngelscriptDebugServer.cpp:347-354` 说明它同样会把异常写进 `UE_LOG`、屏幕提示和 debugger stop message。当前仓库并没有改掉这条 owner，只是把它从 `Manager` 继续工程化到了更多测试/调试资产上。
- `UnLua` 的 owner 更开放。`UnLuaBase.cpp:151-180` 的 `ReportLuaCallError()` 首先检查 `FUnLuaDelegates::ReportLuaCallError` 是否被绑定；只有未绑定时，才回退到默认的 `luaL_traceback + UE_LOG`。也就是说，**错误字符串默认由插件提供，但作者可以接管 reporter**。
- `puerts` 把字符串拼装责任放在 V8 adapter。`V8Utils.h:187-224` 用 `TryCatchToString()` 把 exception、stack trace、file/line 统一转成 `FString`；随后 `JsEnvImpl.cpp:1967-2009`、`1278`、`1494` 等处把结果交给 `Logger->Error()`。它的 owner 不是 JS 业务层，而是 C++ V8 bridge。
- `sluaunreal` 在 Lua VM 层插了一个 error handler。`LuaState.cpp:71-84` 先 `luaL_traceback()`，再把结果交给 `LuaState::onError()`；`onError()` 又允许 `errorDelegate` 自定义，否则落回 `Log::Error()`。它和 `UnLua` 都可注入，但 `slua` 的注入点更贴近 VM 错误函数本身。
- `UnrealCSharp` 的 owner 最特殊。`FMonoDomain.cpp:249-295` 对每次 `mono_runtime_invoke` 的异常直接走 `mono_unhandled_exception()`；同时 `FDomain.cpp:89-99` 和 `Script/UE/CoreUObject/Utils.cs:29-44` 又保留了一条 managed `StackTrace` 反查路径，`FCSharpEnvironment.cpp:19-23` 还会在 signal handler 里把这份 traceback 打到 UE 日志。这里的最终错误拥有者更偏 **CLR / managed runtime**，而不是 Unreal 插件自身。

##### 子维度 2：错误最终通过什么 surface 到达开发者

- 当前 `Angelscript / Hazelight` 的 surface 最完整：`UE_LOG`、屏幕红字、`devEnsure`、debugger `Stopped` message 同时都在。对作者来说，同一次脚本异常能同时到日志、游戏画面和远端调试器。这不是单纯“有 debugger”，而是异常 contract 已经横跨多条 surface。
- `UnLua` 默认只有 `UE_LOG` + traceback，但因为 `ReportLuaCallError` 可被 delegate 接管，真正的 surface 可以由宿主项目决定。它的长处是可注入，短处是默认 contract 不会自动同步给独立 debugger 协议。
- `puerts` 主要把异常送进 `Logger->Error()`，并提供 `CurrentStackTrace()` 这类主动拉取接口。它更像“把 V8 fault 规整成宿主可打印字符串”，而不是像 `Angelscript` 那样再包一层 UE-specific debugger stop contract。
- `sluaunreal` 默认 surface 也偏日志/委托。`errorDelegate` 允许宿主把错误转给自己的面板或 UI，但插件本身没有等价 `Stopped(reason=exception)` 这样的调试协议语义。
- `UnrealCSharp` 更依赖宿主/CLR 对 unhandled exception 的既有处理方式。优点是与 managed runtime 语义一致；代价是按“单个脚本回调”定制 UE-specific 错误 surface 的粒度更粗。

[1] 当前 `Angelscript` 把同一份异常同时送到 `UE_LOG`、屏幕提示与 `DebugServer V2`：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 5242-5310
// 说明: final exception string owner 在插件 engine，而不是只停在 AngelScript VM
// ============================================================================
void LogAngelscriptException(const ANSICHAR* ExceptionString)
{
	if (ExceptionString == nullptr)
		ExceptionString = "NO EXCEPTION";

	UE_LOG(Angelscript, Error, TEXT("%s"), ANSI_TO_TCHAR(ExceptionString)); // ★ 日志 surface

	TArray<FString> Trace;
	GetStackTrace(Trace);
	for (FString& Line : Trace)
		UE_LOG(Angelscript, Error, TEXT("%s"), *Line); // ★ engine 负责补 callstack

	if (GEngine != nullptr)
	{
		UKismetSystemLibrary::PrintString(
			GAmbientWorldContext,
			FString::Printf(TEXT("Angelscript Exception: %s\n%s"), ANSI_TO_TCHAR(ExceptionString), Trace.Num() >= 2 ? *Trace[1] : TEXT("")),
			true, false, FLinearColor::Red, 30.f); // ★ 屏幕 surface
	}
	...
}

void LogAngelscriptException(asIScriptContext* Context)
{
	const ANSICHAR* ExceptionString = Context->GetExceptionString();
	LogAngelscriptException(ExceptionString);

#if WITH_AS_DEBUGSERVER
	if (IsInGameThread())
	{
		if (auto* DebugServer = FAngelscriptEngine::Get().DebugServer)
			DebugServer->ProcessException(Context); // ★ 同一份异常继续进调试协议
	}
#endif
}

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 位置: 455-462
// 说明: debugger 停止原因直接继承同一份 exception 文本
// ============================================================================
FStoppedMessage StopMsg;
StopMsg.Reason = TEXT("exception");

const ANSICHAR* ExceptionString = Context->GetExceptionString();
if (ExceptionString)
	StopMsg.Text = ANSI_TO_TCHAR(ExceptionString);

PauseExecution(&StopMsg); // ★ UE-specific debugger stop surface
```

[2] `UnLua / puerts / sluaunreal` 都会补 traceback，但 owner 落点不同：

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaBase.cpp
// 位置: 151-180
// 说明: UnLua 允许宿主接管 reporter；默认 reporter 才补 traceback 并写日志
// ============================================================================
int32 ReportLuaCallError(lua_State *L)
{
	if (FUnLuaDelegates::ReportLuaCallError.IsBound())
	{
		return FUnLuaDelegates::ReportLuaCallError.Execute(L); // ★ reporter owner 可外置
	}

	int32 Type = lua_type(L, -1);
	if (Type == LUA_TSTRING)
	{
		const char *ErrorString = lua_tostring(L, -1);
		luaL_traceback(L, L, ErrorString, 1);
		ErrorString = lua_tostring(L, -1);
		UE_LOG(LogUnLua, Error, TEXT("Lua error message: %s"), UTF8_TO_TCHAR(ErrorString));
	}
	...
}

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Public/V8Utils.h
// 位置: 187-224
// 说明: puerts 先在 V8 adapter 层把 exception/stack/file:line 折叠成 FString
// ============================================================================
FORCEINLINE static FString TryCatchToString(v8::Isolate* Isolate, v8::TryCatch* TryCatch)
{
	v8::String::Utf8Value Exception(Isolate, TryCatch->Exception());
	FString ExceptionStr(UTF8_TO_TCHAR(*Exception));
	...
	if (TryCatch->StackTrace(Context).ToLocal(&StackTrace))
	{
		FString StackTraceStr(*StackTraceVal);
		ExceptionStr.Append("\n").Append(StackTraceStr); // ★ stack 也在这里组装
	}
	...
}

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp
// 位置: 1967-2009
// 说明: 业务回调面统一消费 TryCatchToString 的结果
// ============================================================================
v8::TryCatch TryCatch(Isolate);
Iter->second->CallJs(...);
if (TryCatch.HasCaught())
{
	Logger->Error(FString::Printf(TEXT("js callback exception %s"), *FV8Utils::TryCatchToString(Isolate, &TryCatch)));
}
...
if (TryCatch.HasCaught())
{
	Logger->Error(FString::Printf(TEXT("js callback exception %s"), *FV8Utils::TryCatchToString(Isolate, &TryCatch)));
}

// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaState.cpp
// 位置: 71-84
// 说明: slua 在 VM error handler 中统一补 traceback，再交给 delegate/log
// ============================================================================
int error(lua_State* L) {
	const char* err = lua_tostring(L,1);
	luaL_traceback(L,L,err,1);
	err = lua_tostring(L,2);
	auto ls = LuaState::get(L);
	ls->onError(err); // ★ 最终 surface 可走 errorDelegate，也可走 Log::Error
	lua_pop(L,1);
	return 0;
}

void LuaState::onError(const char* err) {
	if (errorDelegate.IsBound()) errorDelegate.Broadcast(err);
	else Log::Error("%s", err);
}
```

[3] `UnrealCSharp` 更把异常 surfacing 交回 CLR / managed traceback 体系：

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp
// 位置: 249-295
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Domain/FDomain.cpp
// 位置: 89-99
// 说明: native 侧只要拿到 Mono exception，就交给 CLR；需要 traceback 时再主动回调 managed helper
// ============================================================================
MonoObject* Exception = nullptr;
const auto ReturnValue = Runtime_Invoke(InFunction, InMonoObject, InParams, &Exception);
if (Exception != nullptr)
{
	Unhandled_Exception(Exception);
	return nullptr;
}
...
void FMonoDomain::Unhandled_Exception(MonoObject* InException)
{
	mono_unhandled_exception(InException); // ★ 最终 owner 偏 CLR，而不是 UE 插件
}

FString FDomain::GetTraceback()
{
	if (const auto UtilsClass = FReflectionRegistry::Get().GetUtilsClass())
	{
		if (const auto TracebackMethod = UtilsClass->GetMethod(FUNCTION_UTILS_GET_TRACEBACK, 0))
		{
			return FString(UTF8_TO_TCHAR(String_To_UTF8((MonoString*)TracebackMethod->Runtime_Invoke())));
		}
	}
	return {};
}
```

```csharp
// ============================================================================
// 文件: Reference/UnrealCSharp/Script/UE/CoreUObject/Utils.cs
// 位置: 29-44
// 说明: managed 侧 traceback 语义由 CLR 的 StackTrace 决定
// ============================================================================
private static string GetTraceback()
{
	var Traceback = new StringBuilder();
	var Trace = new StackTrace();

	foreach (var Frame in Trace.GetFrames())
	{
		Traceback.Append(string.Format("at {0}.{1} in {2}:{3}\r\n",
			Frame.GetMethod().DeclaringType.FullName,
			Frame.GetMethod().Name,
			Frame.GetFileName(),
			Frame.GetFileLineNumber()));
	}

	return Traceback.ToString(); // ★ traceback 的语义仍由 managed runtime 决定
}
```

#### 对比矩阵（本轮新增观察点）

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 默认会补调用栈 / traceback | Full | Partial | Full | Full | Full | Full |
| 错误 reporter 可由宿主显式接管 | None | None | Full | Partial | Partial | None |
| 异常可直接转成调试器 `Stopped(reason=exception)` 语义 | Full | None | None | None | None | Full |
| 默认同时进入 `UE_LOG` 与额外可视 surface | Full | Partial | None | None | Partial | Full |
| 最终 owner 更偏宿主 runtime，而不是插件自有统一 formatter | None | Full | Partial | Partial | Partial | None |

#### 小结与建议

- 当前 `Angelscript` 在这一维最强的地方，不是“异常更多”，而是**同一份异常文本同时驱动日志、屏幕、assert 和调试器停机语义**。这条多 surface contract 建议按 `P0` 保持，不要退回只写日志。
- 最值得吸收的是 `UnLua / sluaunreal` 的“reporter 可注入”能力。当前 `Angelscript` 已经有丰富默认 surface，但还没有等价的宿主可替换 reporter；如果后续要接企业内诊断平台或自定义 editor panel，这是最自然的扩展点，优先级 `P1`。
- `puerts` 证明把字符串格式化从业务调用点抽到 `TryCatchToString()` 这样的 adapter 层很有价值。当前 `Angelscript` 也可以把 `GetExceptionString + GetStackTrace + DebugServer stop text` 进一步收束成一个共享 formatter，降低多个 surface 的重复拼装，优先级 `P1`。

### [D8] 故障遏制 contract：谁真的能把跑飞脚本停下来

前文 D8 已经覆盖执行快路径与性能观测；这一轮只补一个更偏“保命”的轴：**脚本跑飞时，插件自己有没有 runtime-owned kill switch**。这个问题在 Lua 系和 Angelscript 系里都不是抽象性能话题，而是 editor 是否还能活着的问题。

#### 各插件实现概览

```
[D8-Containment] Runaway Script Guard
HZ/AS : AngelScript line callback timeout (editor-only) + exclusion scope
UL    : FRunnable watchdog -> timeout guard -> inject lua line hook -> luaL_error
SL    : FRunnable watchdog -> LuaScriptCallGuard -> inject line hook unless debugger already owns hook
PU    : no equal runtime kill-switch located; visible timeout is debugger-attach wait
UC    : no equal dead-loop guard located in current source scan
```

#### 详细对比

##### 子维度 1：计时器挂在哪里，何时判定“跑飞”

- 当前 `Angelscript` 与 `Hazelight` 都把判定权挂在 AngelScript VM 的 line callback 上。`AngelscriptSettings.h:132-138` 明确写死这只在 editor 有效；`AngelscriptEngine.cpp:5566-5588` 与 `UEAS2/AngelscriptManager.cpp:4064-4086` 则说明 VM 大约每执行十万行脚本会回调一次，超时后直接 `Context->SetException(...)`。这是一种**VM 内生型** kill switch。
- `UnLua` 把计时器放在独立的 `FRunnable` watchdog 线程上。`LuaDeadLoopCheck.cpp:36-114` 里的 `FRunner` 每秒累加 `TimeoutCounter`，超时后不是直接终止 Lua VM，而是把一个 line hook 安装到主 `lua_State` 上，并在下一次行事件时 `luaL_error("lua script exec timeout")`。判定权在后台线程，真正的终止点仍回到 Lua VM。
- `sluaunreal` 也是 watchdog 线程，但更强调 RAII guard。`LuaState.cpp:1173-1267` 中，`LuaScriptCallGuard` 在进入脚本调用时 `scriptEnter()`，超时后触发 `onTimeout()`，然后按需把 `scriptTimeout` line hook 装进 Lua VM。它和 `UnLua` 一样不是“硬中断线程”，而是“延迟到下一条 Lua 行再抛错”。
- `puerts` 这轮没有定位到等价 runtime kill switch。当前最接近 timeout 语义的 `JsEnvImpl.h:99-114` 其实只是 `WaitDebugger(timeout)`，它控制的是 debugger attach 等待时长，不是脚本执行预算。这里要区分 **没有定位到 kill contract** 与 **只有 debugger timeout**，不能混写成“已有脚本超时保护”。
- `UnrealCSharp` 这轮同样没有在 `Source/UnrealCSharp*` 中定位到等价 dead-loop guard。可见证据主要集中在异常 surfacing、traceback 与 assembly/domain 生命周期，不是执行时长裁断。因此这里更适合判 `None*`，而不是武断推成 `Partial`。

##### 子维度 2：超时后如何真正把脚本停下来

- 当前 `Angelscript / Hazelight` 直接把超时转成脚本异常字符串。这条路径最短，也天然兼容前述 D5 的异常 surface。
- `UnLua / sluaunreal` 都不是立即从后台线程杀掉脚本，而是利用 `lua_sethook(..., LUA_MASKLINE, 0)` 在下一条 Lua line 回调里抛错。好处是线程安全、不会粗暴破坏 VM；代价是它们都依赖“脚本仍能回到下一条 line hook”。
- `sluaunreal` 在 `LuaScriptCallGuard::onTimeout()` 里专门检查 `lua_gethook(L)`，只有当前没有 debugger hook 时才装自己的 timeout hook。这说明它显式考虑了**与 debugger 抢 hook owner** 的问题。
- `UnLua` 则没有同等级的 debugger coexistence 逻辑，但 `LuaEnv.cpp:160-165` 明确写了 DeadLoopCheck 与 profiling hook 的冲突提示。这也是 contract，只是它选择“告知冲突”，不是“协商 hook owner”。

[1] 当前 `Angelscript / Hazelight` 的 kill switch 内生在 AngelScript VM，且明确标注 editor-only：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h
// 位置: 132-138
// 说明: current AS 明确把 loop timeout contract 限定在 editor
// ============================================================================
/**
 * Only in editor:
 * If a script function takes longer than this time to execute, kill it with an exception.
 * This allows the editor to recover from accidental infinite loops, but does not work in cooked games!
 */
UPROPERTY(Config, EditDefaultsOnly, Category = "Angelscript")
float EditorMaximumScriptExecutionTime = 1.f;

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 5566-5601
// 说明: line callback 超时后直接把故障转换成脚本异常；trusted native scope 可临时排除
// ============================================================================
void AngelscriptLoopDetectionCallback(asCContext* Context)
{
	float MaximumScriptExecutionTime = UAngelscriptSettings::Get().EditorMaximumScriptExecutionTime;
	...
	if (Context->m_loopDetectionTimer < CurrentTime - MaximumScriptExecutionTime)
	{
		Context->SetException("Script function took too long to execute. Potentially an infinite loop? (timeout controlled by EditorMaximumScriptExecutionTime setting)");
		return; // ★ 直接进入 D5 的异常 surface
	}
}

FAngelscriptExcludeScopeFromLoopTimeout::FAngelscriptExcludeScopeFromLoopTimeout()
{
	Context = (asCContext*)asGetActiveContext();
	if (Context != nullptr)
	{
		Context->m_loopDetectionExclusionCounter += 1; // ★ trusted native scope 可以暂时排除 timeout
	}
}

// ============================================================================
// 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/AngelscriptManager.cpp
// 位置: 4064-4086
// 说明: Hazelight 的 loop detection owner 与 current AS 基本同构
// ============================================================================
if (Context->m_loopDetectionTimer < CurrentTime - MaximumScriptExecutionTime)
{
	Context->SetException("Script function took too long to execute. Potentially an infinite loop? (timeout controlled by EditorMaximumScriptExecutionTime setting)");
	return;
}
```

[2] `UnLua / sluaunreal` 都采用 watchdog thread + line hook 注入，但 `slua` 多了一层 debugger coexistence：

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaDeadLoopCheck.cpp
// 位置: 21-114
// 说明: UnLua 的 watchdog 线程负责计时，真正的“终止”发生在下一次 Lua line event
// ============================================================================
TUniquePtr<FDeadLoopCheck::FGuard> FDeadLoopCheck::MakeGuard()
{
	if (Timeout <= 0)
		return TUniquePtr<FGuard>();
	return MakeUnique<FGuard>(this);
}

uint32 FDeadLoopCheck::FRunner::Run()
{
	while (bRunning)
	{
		FPlatformProcess::Sleep(1.0f);
		...
		if (TimeoutCounter.GetValue() < Timeout)
			continue;

		const auto Guard = TimeoutGuard.load();
		if (Guard)
		{
			TimeoutGuard.store(nullptr);
			Guard->SetTimeout(); // ★ 超时后先装 hook，不直接粗暴打断线程
		}
	}
}

void FDeadLoopCheck::FGuard::OnLuaLineEvent(lua_State* L, lua_Debug* ar)
{
	lua_sethook(L, nullptr, 0, 0);
	luaL_error(L, "lua script exec timeout"); // ★ 真正的 kill 点回到 Lua VM
}

// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp
// 位置: 458-462
// 说明: 具体脚本调用显式包着 dead-loop guard
// ============================================================================
const auto Guard = Env.GetDeadLoopCheck()->MakeGuard();
if (lua_pcall(L, NumParams, LUA_MULTRET, -(NumParams + 2)) != LUA_OK)
{
	lua_settop(L, ErrorHandlerIndex - 1);
	return false;
}

// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaState.cpp
// 位置: 1173-1267
// 说明: slua 也是 watchdog thread，但会先检查当前 hook owner，避免直接覆盖 debugger hook
// ============================================================================
uint32 FDeadLoopCheck::Run()
{
	while (stopCounter.GetValue() == 0) {
		FPlatformProcess::Sleep(1.0f);
		if (frameCounter.GetValue() != 0) {
			timeoutCounter.Increment();
			if(timeoutCounter.GetValue() >= MaxLuaExecTime)
				onScriptTimeout();
		}
	}
}

void LuaScriptCallGuard::onTimeout()
{
	auto hook = lua_gethook(L);
	// if debugger isn't exists
	if (hook == nullptr) {
		lua_sethook(L, scriptTimeout, LUA_MASKLINE, 0); // ★ 只有 hook 空闲时才接管
	}
}

void LuaScriptCallGuard::scriptTimeout(lua_State *L, lua_Debug *ar)
{
	lua_sethook(L, nullptr, 0, 0);
	luaL_error(L, "script exec timeout");
}
```

[3] `puerts` 这轮能定位到的 timeout 只是 debugger attach 语义，不是 runtime kill contract：

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.h
// 位置: 99-114
// 说明: 这里的 timeout 是“等 debugger attach 多久”，不是脚本执行预算
// ============================================================================
virtual void WaitDebugger(double timeout) override
{
	const auto startTime = FDateTime::Now();
	while (Inspector && !Inspector->Tick())
	{
		if (timeout > 0)
		{
			auto now = FDateTime::Now();
			if ((now - startTime).GetTotalSeconds() >= timeout)
			{
				break; // ★ 只结束 attach wait，不会给脚本抛 timeout exception
			}
		}
	}
}
```

#### 对比矩阵（本轮新增观察点）

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 插件自带 runtime dead-loop / runaway kill switch | Full | None* | Full | None* | Full | Full |
| kill switch 明确受 editor-only 边界约束 | Full | None | None | None | None | Full |
| 允许 trusted scope 临时排除 timeout | Full | None | None | None | Partial | Full |
| 显式处理与 debugger / profiler hook 的冲突 | Partial | None | Partial | None | Full | Partial |
| 可在 cooked/runtime 继续使用同一套超时 contract | None | None* | Full | None* | Full | None |

\* `None` 表示本轮在对应插件源码里未定位到等价 plugin-owned dead-loop guard；`puerts` 可见 timeout 仅用于 `WaitDebugger`，`UnrealCSharp` 当前可见证据集中在异常 surfacing 与 domain 生命周期。

#### 小结与建议

- 当前 `Angelscript` 在这一维的真正优势不是“会超时”，而是 **timeout 直接回到同一条异常/调试 surface**，同时还有 `FAngelscriptExcludeScopeFromLoopTimeout` 这种显式安全区。这个 contract 建议按 `P0` 保持。
- `sluaunreal` 的 `lua_gethook()` 检查值得吸收。当前 `Angelscript` 也有 debugger 与 line callback 并存的复杂状态，但还没有同等级的“hook owner 协商”语义；如果后续 loop detection 与更复杂的 stepping/data breakpoint 产生耦合，优先级 `P1`。
- `UnLua` 提醒了另一个现实取舍：运行时 watchdog 和 profiler/trace hook 会互相挤占。当前 `Angelscript` 如果未来继续强化 editor loop detection，建议把“当前 timeout owner / profiler owner / debugger owner”做成更显式的状态表，而不是只靠若干布尔条件，优先级 `P1`。

---
## 深化分析 (2026-04-09 00:25:42)

### [D3] 脚本行为真正挂在哪个载体：custom UClass / duplicated UFunction / instance proxy

前文 `D3` 已经讨论过 override owner 和 `UE -> script` 调用入口；这一轮不重复那些结论，只补一个更底层的问题：**脚本行为在 UE 里最终“附着”到什么对象上**。同样都能覆写事件，但如果 carrier 分别是 `custom UClass`、`duplicated UFunction`、或者 `instance proxy table`，后续的继承链稳定性、编辑器可见性、热重载回滚粒度都会完全不同。

#### 各插件实现概览

```
[D3-Carrier] Physical Behavior Carrier
HZ / AS   : UASClass
            ├─ custom script UClass                     // 脚本类本身就是一等 UE 类
            ├─ inject / link UFunction into class map   // 行为入口提前进入类表
            └─ instance dispatch follows class metadata  // 实例只消费类元数据

UC        : UBlueprintGeneratedClass + UCSharpFunction  // 类层生成 + 函数层 duplicated carrier
UnLua     : existing UClass + ULuaFunction + table ref  // 不生成脚本父类，patch 现有类
puerts    : UTypeScriptGeneratedClass + JS object       // 生成类承载入口，实例侧 JS 对象承载实现
slua      : existing UClass + hooked UFunction + luaSelfTable
            ├─ class metadata                           // 可调用入口仍写回 UFunction / class map
            └─ instance luaSelfTable                    // 实例行为状态放在 Lua table
```

#### 详细对比

##### 子维度 1：主 carrier 是“类”，还是“函数”

- 当前 `Angelscript` 与 `Hazelight` 的主 carrier 都是 `UASClass`。`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h:14-35` 和 `J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/ClassGenerator/ASClass.h:11-24` 都表明脚本类直接继承 `UClass`，不是在普通 `UClass` 外再套一层旁路 registry。当前 `Angelscript` 在 `.../AngelscriptClassGenerator.cpp:2581-2590` 创建 `UASClass` 后，又会在 `...:2820-2835` 为 Blueprint event 预建 `UFunction` 并立刻挂进 `Children + FunctionMap`。这说明行为 carrier 先物化成类元数据，再交给实例消费。
- `UnrealCSharp` 不是“没有类 carrier”，而是 **hybrid carrier**。`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicClassGenerator.cpp:300-426` 会实际创建 `UBlueprintGeneratedClass + UBlueprint`；但 `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FCSharpBind.cpp:331-365,469-503` 又把 override 入口 duplicated 成 `UCSharpFunction`，并把原函数 `SetNativeFunc(UCSharpFunction::execCallCSharp)`。也就是说，层级 carrier 在类上，执行 carrier 却落在函数级 thunk 上。
- `UnLua` 则明确不走“脚本父类”这条路。`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaManager.cpp:253-317` 先拿到 Lua module table 并 `luaL_ref`，随后对匹配到的 UE `UFunction` 调 `ULuaFunction::Override`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaFunction.cpp:139-239` 再决定是复制旧函数保存 supercall，还是直接 patch 现有 `UFunction` 的 native func/script buffer。这里的主 carrier 是“既有 `UClass` + 被 patch 的 `UFunction`”，不是新类。
- `puerts` 和 `UnrealCSharp` 一样也生成类，但 owner 更集中。`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/TypeScriptGeneratedClass.cpp:123-229` 显示 `UTypeScriptGeneratedClass` 自己负责 `ObjectInitialize()`、`TsConstruct()` 和 `BlueprintEvent -> execLazyLoadCallJS / execCallJS` 的重定向，因此类 carrier 比 `UnrealCSharp` 更强，函数 carrier 只是这个 generated class 的局部重定向表。
- `sluaunreal` 继续走 patch 既有类的路线，但比 `UnLua` 更显式地拆成“双 carrier”。`Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaOverrider.cpp:1208-1303` 先构出 `luaSelfTable`；再在 `...:1381-1432` 里 duplicated / hooked `UFunction` 写回类。调用入口在类元数据，真实实例语义在 `luaSelfTable`。

##### 子维度 2：实例侧是否还有第二个 carrier

- 当前 `Angelscript / Hazelight` 的实例主要只是“拿着 `UASClass` 跑”。从本轮采样里没有看到等价 `luaSelfTable`、`JS object cache` 这样的 per-instance proxy 成为行为 owner。这里不是“没有实例状态”，而是**实例状态没有成为 override contract 的主要载体**。
- `UnLua` 与 `sluaunreal` 都需要实例侧 Lua 对象参与。前者把 Lua module/table 通过 `luaL_ref` 固定在 registry，并在对象绑定时拼出 `INSTANCE` table；后者更直接，`luaSelfTable` 就是实例侧脚本语义的宿主。这两者都更依赖“对象创建后再把脚本世界接上来”。
- `puerts` 也有明显的实例侧 carrier。`TsConstruct(this, Object)` 说明 generated class 只是入口，真正对象语义还要落到 JS runtime 里的对象图里。因此 `puerts` 是“类 carrier + 实例 JS object”双层结构，而不是单纯 `UFunction` patch。
- `UnrealCSharp` 的实例侧 owner 介于两者之间。采样代码里没看到 `luaSelfTable` 这种显式 proxy table，但 duplicated `UCSharpFunction` 最终还是要落到 managed object / method hash 上，故应判为 `Partial`，不是 `None`。

[1] 当前 `Angelscript / Hazelight` 把脚本行为先物化成 `UASClass`，再把 `UFunction` 接入类表：

```cpp
// ============================================================================
// [1] 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h
// 位置: 14-35
// 说明: current AS 直接把脚本类做成 `UClass` 子类，carrier 从一开始就是类元数据
// ============================================================================
class ANGELSCRIPTRUNTIME_API UASClass : public UClass
{
	GENERATED_BODY()
public:
	UClass* CodeSuperClass = nullptr;
	UASClass* NewerVersion = nullptr;
	int32 ContainerSize = 0;
	int32 ScriptPropertyOffset = 0;
	class asIScriptFunction* ConstructFunction;
	class asIScriptFunction* DefaultsFunction;
	UE::GC::FSchemaOwner ReferenceSchema; // ★ 这也说明类本身还是 GC contract 的宿主
};

// ============================================================================
// [1] 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 位置: 2581-2590, 2820-2835, 3615-3620
// 说明: 创建 `UASClass` 后，Blueprint event 的 `UFunction` 会在类生成阶段就进入类表
// ============================================================================
UASClass* NewClass = NewObject<UASClass>(
	FAngelscriptEngine::GetPackage(),
	UASClass::StaticClass(),
	FName(*UnrealName),
	RF_Public | RF_Standalone | RF_MarkAsRootSet
);

if (ScriptType != nullptr)
	ScriptType->SetUserData(NewClass); // ★ script type 直接反向指向 carrier class

UFunction* NewFunction = NewObject<UFunction>(NewClass, *FuncName, RF_Public);
NewFunction->FunctionFlags = FUNC_Event | FUNC_BlueprintEvent | FUNC_Public;
NewFunction->Bind();
NewFunction->StaticLink(true);
NewFunction->Next = NewClass->Children;
NewClass->Children = NewFunction;
NewClass->AddFunctionToFunctionMap(NewFunction, NewFunction->GetFName()); // ★ 行为入口提前进入类表

// 普通生成函数也统一链接进 class metadata
NewFunction->Next = NewClass->Children;
NewClass->Children = NewFunction;
NewClass->AddFunctionToFunctionMap(NewFunction, NewFunction->GetFName());

// ============================================================================
// [1] 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/ClassGenerator/ASClass.h
// 位置: 11-24
// 说明: Hazelight 同样使用 `UASClass`，说明两者在 carrier 选择上是同血缘方案
// ============================================================================
class ANGELSCRIPTCODE_API UASClass : public UClass
{
	GENERATED_BODY()
public:
	UClass* CodeSuperClass = nullptr;
	UASClass* NewerVersion = nullptr;
	int32 ContainerSize = 0;
	int32 ScriptPropertyOffset = 0;
	class asIScriptFunction* ConstructFunction;
	class asIScriptFunction* DefaultsFunction;
};
```

[2] `UnrealCSharp` 的 carrier 是“生成类 + duplicated `UCSharpFunction`”双层组合：

```cpp
// ============================================================================
// [2] 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicClassGenerator.cpp
// 位置: 300-318, 404-426
// 说明: 先物化 `UBlueprintGeneratedClass + UBlueprint`，类层 carrier 真实存在
// ============================================================================
void FDynamicClassGenerator::BeginGenerator(UBlueprintGeneratedClass* InClass, UClass* InParentClass)
{
	BeginGenerator(static_cast<UClass*>(InClass), InParentClass);
	Cast<UBlueprint>(InClass->ClassGeneratedBy)->ParentClass = InParentClass;
	InClass->ClassFlags &= ~CLASS_Native;
}

UBlueprintGeneratedClass* FDynamicClassGenerator::GeneratorBlueprintGeneratedClass(
	UPackage* InOuter, const FString& InNameSpace, const FString& InName, UClass* InParentClass,
	const TFunction<void(UClass*)>& InProcessGenerator)
{
	auto Class = NewObject<UBlueprintGeneratedClass>(InOuter, *InName, RF_Public);
	const auto Blueprint = NewObject<UBlueprint>(Class, *InName.LeftChop(2));
	Blueprint->SkeletonGeneratedClass = Class;
	Blueprint->GeneratedClass = Class;
	Class->ClassGeneratedBy = Blueprint;
	GeneratorClass(InNameSpace, InName, Class, InParentClass, InProcessGenerator);
	return Class;
}

// ============================================================================
// [2] 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FCSharpBind.cpp
// 位置: 331-365, 469-503
// 说明: 但 override 真正的执行入口仍被 duplicated 到 `UCSharpFunction`
// ============================================================================
const auto OverrideFunction = DuplicateFunction(OriginalFunction, InClass, *NewFunctionName);

FCSharpEnvironment::GetEnvironment().AddFunctionHash<FCSharpFunctionDescriptor>(
	FunctionHash, InClassDescriptor, OriginalFunction,
	FCSharpFunctionRegister(OriginalFunction, OverrideFunction,
	                        OriginalFunction->FunctionFlags, OriginalFunction->GetNativeFunc()));

OriginalFunction->SetNativeFunc(UCSharpFunction::execCallCSharp); // ★ 原函数被改写成桥接 thunk
OriginalFunction->FunctionFlags |= FUNC_Native;

FObjectDuplicationParameters ObjectDuplicationParameters(InOriginalFunction, InClass);
ObjectDuplicationParameters.DestClass = UCSharpFunction::StaticClass();
const auto NewFunction = Cast<UFunction>(StaticDuplicateObjectEx(ObjectDuplicationParameters));
NewFunction->SetNativeFunc(InOriginalFunction->GetNativeFunc());
NewFunction->StaticLink(true);
InClass->AddFunctionToFunctionMap(NewFunction, InFunctionName); // ★ duplicated 函数自己也是 class map 成员
```

[3] `UnLua` 不造脚本父类，而是把 Lua table 与 `ULuaFunction` patch 写回现有 `UClass`：

```cpp
// ============================================================================
// [3] 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaManager.cpp
// 位置: 273-317
// 说明: BindClass 先把 Lua module 固定进 registry，再用它覆写现有类上的函数
// ============================================================================
const auto Type = UnLua::LowLevel::GetLoadedModule(L, TCHAR_TO_UTF8(*InModuleName));
...
lua_pushvalue(L, -1);
const auto Ref = luaL_ref(L, LUA_REGISTRYINDEX);

auto& BindInfo = Classes.Add(Class);
BindInfo.TableRef = Ref; // ★ 类绑定信息持有 Lua table ref

UnLua::LowLevel::GetFunctionNames(Env->GetMainState(), Ref, BindInfo.LuaFunctions);
ULuaFunction::GetOverridableFunctions(Class, BindInfo.UEFunctions);

for (const auto& LuaFuncName : BindInfo.LuaFunctions)
{
	UFunction** Func = BindInfo.UEFunctions.Find(LuaFuncName);
	if (Func)
	{
		UFunction* Function = *Func;
		ULuaFunction::Override(Function, Class, LuaFuncName); // ★ 直接 patch 既有类
	}
}

// ============================================================================
// [3] 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaFunction.cpp
// 位置: 160-166, 210-239
// 说明: `ULuaFunction` 既保存 supercall carrier，也会把原始函数改造成 Lua 入口
// ============================================================================
const auto DestName = FString::Printf(TEXT("%s__Overridden"), *Function->GetName());
Overridden = static_cast<UFunction*>(StaticDuplicateObject(Function, GetOuter(), *DestName));
Overridden->StaticLink(true);
Overridden->SetNativeFunc(Function->GetNativeFunc()); // ★ duplicated 旧函数作为 supercall carrier

if (bAdded)
{
	SetNativeFunc(execCallLua);
	Class->AddFunctionToFunctionMap(this, *GetName());
}
else
{
	Function->FunctionFlags |= FUNC_Native;
	Function->SetNativeFunc(&execScriptCallLua); // ★ 原函数被改写成 Lua thunk
	Function->GetOuterUClass()->AddNativeFunction(*Function->GetName(), &execScriptCallLua);
	Function->Script.Empty();
	Function->Script.AddUninitialized(ScriptMagicHeaderSize + sizeof(ULuaFunction*));
}
```

[4] `puerts / sluaunreal` 都有实例侧 carrier，但 `puerts` 偏 generated class，`slua` 偏 `luaSelfTable`：

```cpp
// ============================================================================
// [4] 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/TypeScriptGeneratedClass.cpp
// 位置: 169-229
// 说明: puerts 的 generated class 自己负责对象初始化与函数重定向
// ============================================================================
void UTypeScriptGeneratedClass::ObjectInitialize(const FObjectInitializer& ObjectInitializer)
{
	auto Object = ObjectInitializer.GetObj();
	...
	PinedDynamicInvoker->TsConstruct(this, Object); // ★ 实例真正进入 JS 世界

	if (UNLIKELY(!RedirectedToTypeScript))
	{
		for (TFieldIterator<UFunction> FuncIt(this, EFieldIteratorFlags::ExcludeSuper); FuncIt; ++FuncIt)
		{
			auto Function = *FuncIt;
			if (!Function->IsNative() && Function->HasAnyFunctionFlags(FUNC_BlueprintEvent))
			{
				Function->FunctionFlags |= FUNC_Native;
				Function->SetNativeFunc(&UTypeScriptGeneratedClass::execLazyLoadCallJS);
				AddNativeFunction(*Function->GetName(), &UTypeScriptGeneratedClass::execLazyLoadCallJS);
			}
		}
		RedirectedToTypeScript = true;
	}
}

// ============================================================================
// [4] 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaOverrider.cpp
// 位置: 1208-1303, 1381-1432
// 说明: slua 先建 `luaSelfTable`，再把 override 入口写回 class metadata
// ============================================================================
NS_SLUA::LuaVar luaSelfTable;
...
if (!luaSelfTable.isTable()) {
	return false;
}

for (auto& funcName : funcNames) {
	UFunction* func = cls->FindFunctionByName(funcName, EIncludeSuperFlag::IncludeSuper);
	if (!func && (funcName.ToString()).StartsWith(TEXT("AnimNotify_"))) {
		func = duplicateUFunction(animNotifyTemplate, cls, funcName, (FNativeFuncPtr)&ULuaOverrider::luaOverrideFunc);
	}
	if (func && (func->FunctionFlags & OverrideFuncFlags)) {
		hookBpScript(func, cls, (FNativeFuncPtr)&ULuaOverrider::luaOverrideFunc);
	}
}

setmetatable(luaSelfTable, (void*)obj, bNetReplicated);
ULuaOverrider::addObjectTable(L, obj, luaSelfTable, bHookInstancedObj); // ★ 实例语义落在 luaSelfTable

auto supercallFunc = duplicateUFunction(func, cls, FName(*(SUPER_CALL_FUNC_NAME_PREFIX + func->GetName())), func->GetNativeFunc());
overrideFunc->SetNativeFunc(hookFunc);
overrideFunc->Script.Insert(Code, CodeSize, 0); // ★ 类上的 UFunction 入口被改写
```

#### 对比矩阵（本轮新增观察点）

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 自定义 `UClass` 子类是主行为 carrier | Full | Partial | None | Full | None | Full |
| override 主要依赖 duplicated / patched `UFunction` 接管 | Partial | Full | Full | Partial | Full | Partial |
| 实例侧脚本对象 / 表是行为 contract 的必要 carrier | None | Partial | Full | Full | Full | None |
| 支持“脚本父类先进入类表，再被 Blueprint 继续继承” | Full | Full | N/A | Full | N/A | Full |
| 默认路线是不生成脚本父类、只 patch 既有 UE class | None | Partial | Full | None | Full | None |

#### 小结与建议

- 当前 `Angelscript` 在这个问题上的结构优势非常明确：**脚本行为的主 carrier 仍然是 `UASClass`，不是某个晚绑定的 per-instance proxy**。这让 Blueprint 继承链、GC、编辑器数据源、热重载回滚都能继续围绕“类”工作，这条主线建议按 `P0` 保持。
- `UnrealCSharp / UnLua / sluaunreal` 说明 duplicated / patched `UFunction` 很适合做“局部桥接层”，尤其适合只想接管少数 override 入口的场景。当前 `Angelscript` 若未来要引入更细粒度、非类级的 override injection，可以把这种函数 carrier 当成补充能力，优先级 `P1`，但不应取代 `UASClass` 主路线。
- `puerts` 证明“类 carrier 强”和“实例侧脚本对象仍存在”并不矛盾。如果后续 `Angelscript` 想增加某些实例级脚本宿主对象，应让它附着在 `UASClass` contract 之下，而不是把 carrier 完整迁走，优先级 `P2`。

### [D7] authoring truth location：脚本事实最终写进哪个 editor 对象

前文 `D7` 已经比较过 editor surface 和 headless tooling；这一轮只补 **authoring truth location**。也就是：作者在编辑器里点完按钮之后，哪一个 UE 对象 / 资产 / 外部文件会成为“以后都以它为准”的事实源。这个问题直接决定工作流是否可见、是否易查、以及多人协作时冲突会落在哪一层。

#### 各插件实现概览

```
[D7-TruthLocation] Where Editor Writes the Authoritative Link
HZ / AS   : ContentBrowser script item -> UASClass -> optional child UBlueprint
            // truth 主要留在 UE 资产 / 类体系里

UC        : DynamicDataSource item + OverrideFunctions.json + external .cs
            // truth 分裂在 UE 动态类条目与代码分析映射

UnLua     : UBlueprint interface graph pin ("GetModuleName") -> external .lua path
            // truth 写进 Blueprint 图节点默认值

puerts    : UPEBlueprintAsset / UTypeScriptBlueprint -> generated Blueprint package
            + PuertsEditor CodeAnalyze

slua      : runtime lua path / luaSelfTable
            + slua_profile editor window
            // 本轮未见等价 editor-owned script artifact
```

#### 详细对比

##### 子维度 1：truth 是保存在 UE 资产里，还是保存在外部路径映射里

- 当前 `Angelscript` 与 `Hazelight` 这轮最重要的新确认，不是“有 Content Browser 扩展”，而是 **authoring truth 的第一落点仍然是 UE 里的对象**。`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:111-118` 激活 `AngelscriptData` data source；`.../AngelscriptContentBrowserDataSource.cpp:16-28` 又用真实 `UObject* Asset` 组装 `/All/Angelscript/...` 条目；`.../AngelscriptEditorModule.cpp:504-517` 最终把 `UASClass` 继续转成普通 `UBlueprint` 子类。也就是说，truth 不是“某个外部脚本路径字符串”，而是插件自有资产视图和 `UASClass` 自身。
- `UnrealCSharp` 的 truth 明显是 split authority。`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:788-840` 把动态类显示成 `Dynamic Classes` 虚拟目录；但 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:21-40,205-212` 初始化时又会加载 `OverrideFunctions.json`，之后 `OpenFile / CodeAnalysis` 都围绕这份映射找外部 `.cs` 文件。因此它不是“没有 editor truth”，而是“UE 资产 truth”与“外部源码 truth”并存。
- `UnLua` 则把 truth 写得更隐蔽。`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:138-184` 在 `BindToLua_Executed()` 里把 `LuaModuleName` 写到 `Blueprint->ImplementedInterfaces` 对应 graph 节点 pin 的 `DefaultValue`；后续 `...:315-372` 再通过 `GetModuleName` 回读这个字符串并拼出 `.lua` 路径。也就是说，truth 被埋进 Blueprint graph，而不是专门的 script asset。
- `puerts` 的 truth 比 `UnLua` 显式得多。`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Public/PEBlueprintAsset.h:62-90` 定义了 `UPEBlueprintAsset`，直接持有 `GeneratedClass / Blueprint / Package / HasConstructor`；`.../PEBlueprintAsset.cpp:87-165,1345-1357` 则负责 `LoadOrCreate()`、写回 `HasConstructor`、标记修改并重新 compile。它既依赖外部 `.ts` 分析器，又把 engine-side truth 落在专门资产对象上。
- `sluaunreal` 本轮没有读到等价 truth artifact。`Reference/sluaunreal/Plugins/slua_unreal/slua_unreal.uplugin:1-28` 只声明 runtime `slua_unreal` 和 editor `slua_profile`；`Reference/sluaunreal/Plugins/slua_unreal/Source/slua_profile/Private/slua_profile.cpp:53-75` 的 editor 代码也只是注册 profiler menu / tab。配合本轮对 `Reference/sluaunreal/Plugins/slua_unreal/Source/` 的扫描，没有看到 `ContentBrowserDataSource`、`asset class`、`Blueprint toolbar`、`factory` 等等价结构，因此应判为“editor-owned truth artifact 未定位到”，不是“只是实现方式不同”。

##### 子维度 2：作者动作完成后，编辑器以后如何再找回脚本

- 当前 `Angelscript / Hazelight` 的回查路径最像“正常 UE 资产流”：Content Browser 里的脚本条目先指向真实 `Asset->GetPathName()`，再由 `ShowCreateBlueprintPopup(UASClass*)` 把它转成 Blueprint 工作流。回查 owner 是 UE asset/object path。
- `UnrealCSharp` 的回查路径则更像“索引驱动的 IDE launcher”。当前资产只提供 class context，真正的源码入口来自 `OverrideFunctions.json`。这意味着 editor 操作很顺手，但如果映射和真实文件漂移，truth 就会分叉。
- `UnLua` 的回查路径是“Blueprint graph pin -> module string -> file path”。优点是实现极轻；缺点是事实源埋在节点默认值里，不做专门 UI 很难一眼看出。
- `puerts` 的回查路径介于两者之间。`UPEBlueprintAsset` 能提供明确的 `Blueprint / GeneratedClass / Package`，而 `PuertsEditorModule` 在 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:116-150` 中又启动 `PuertsEditor/CodeAnalyze` 和 file watcher。因此它把“资产 truth”和“源码分析 truth”都摆到台面上，而不是只藏在某个 pin。
- `sluaunreal` 本轮采样里，editor 回查主要还是 profiler / runtime 侧观测，没有构成常规 authoring truth 的闭环。

[1] 当前 `Angelscript / Hazelight` 的 truth 留在 editor-owned datasource 和 `UASClass -> Blueprint` 工作流里：

```cpp
// ============================================================================
// [1] 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 位置: 111-118, 404-409, 510-517
// 说明: current AS 先激活脚本 data source，再把 `UASClass` 转成普通 Blueprint 资产
// ============================================================================
void OnEngineInitDone()
{
	auto* DataSource = NewObject<UAngelscriptContentBrowserDataSource>(GetTransientPackage(), "AngelscriptData", RF_MarkAsRootSet | RF_Transient);
	DataSource->Initialize();
	UContentBrowserDataSubsystem* ContentBrowserData = IContentBrowserDataModule::Get().GetSubsystem();
	ContentBrowserData->ActivateDataSource("AngelscriptData"); // ★ 脚本 truth 先进入 Content Browser
}

FAngelscriptRuntimeModule::GetEditorCreateBlueprint().AddLambda(
	[](UASClass* ScriptClass)
	{
		FAngelscriptEditorModule::ShowCreateBlueprintPopup(ScriptClass);
	});

Asset = FKismetEditorUtilities::CreateBlueprint(
	Class, Package, AssetName, BPTYPE_Normal,
	BlueprintClass, BlueprintGeneratedClass, FName("AngelscriptCreateBlueprint")
); // ★ 最终 authoring truth 仍回到普通 UE Blueprint 资产

// ============================================================================
// [1] 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp
// 位置: 16-28
// 说明: Content Browser 条目直接承载真实 `UObject* Asset`
// ============================================================================
auto Payload = MakeShared<FAngelscriptContentBrowserPayload>();
Payload->Path = Asset->GetPathName();
Payload->Asset = Asset;

return FContentBrowserItemData(
	this,
	EContentBrowserItemFlags::Type_File | EContentBrowserItemFlags::Category_Asset,
	*(TEXT("/All/Angelscript/") + Asset->GetName()), Asset->GetFName(), FText::FromString(DisplayName), Payload, *Payload->Path);

// ============================================================================
// [1] 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 位置: 136-140, 538-541
// 说明: Hazelight 的 editor truth location 与 current AS 同构
// ============================================================================
auto* DataSource = NewObject<UAngelscriptContentBrowserDataSource>(GetTransientPackage(), "AngelscriptData", RF_MarkAsRootSet | RF_Transient);
ContentBrowserData->ActivateDataSource("AngelscriptData");

Asset = FKismetEditorUtilities::CreateBlueprint(
	Class, Package, AssetName, BPTYPE_Normal,
	BlueprintClass, BlueprintGeneratedClass, FName("AngelscriptCreateBlueprint")
);
```

[2] `UnrealCSharp` 的 truth 同时落在 UE 动态类条目和外部 `OverrideFunctions.json`：

```cpp
// ============================================================================
// [2] 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp
// 位置: 788-840
// 说明: 编辑器里有明确的 "Dynamic Classes" 虚拟根，但 payload 指向的是生成后的文件/类
// ============================================================================
if (InFolderPath == *DYNAMIC_ROOT_INTERNAL_PATH)
{
	DisplayNameOverride = LOCTEXT("DynamicFolderDisplayName", "Dynamic Classes");
}

return FContentBrowserItemData(this,
	EContentBrowserItemFlags::Type_File | EContentBrowserItemFlags::Category_Class,
	*GetVirtualPath(InClass),
	InClass->GetFName(),
	FText(),
	MakeShared<FDynamicFileItemDataPayload>(
		*FDynamicGenerator::GetDynamicNormalizeFile(InClass), InClass)
);

// ============================================================================
// [2] 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp
// 位置: 21-40, 57-80, 205-212
// 说明: Blueprint authoring 入口最终还是依赖外部代码分析映射
// ============================================================================
void FUnrealCSharpBlueprintToolBar::Initialize()
{
	auto& BlueprintEditorModule = FModuleManager::LoadModuleChecked<FBlueprintEditorModule>("Kismet");
	...
	SetCodeAnalysisOverrideFilesMap();   // ★ 先装载 OverrideFunctions.json
	FDynamicGenerator::SetCodeAnalysisDynamicFilesMap();
}

if (const auto OverrideFile = GetOverrideFile(); !OverrideFile.IsEmpty())
{
	if (IFileManager::Get().FileExists(*OverrideFile))
	{
		FSourceCodeNavigation::OpenSourceFile(OverrideFile); // ★ 真正打开的是外部 .cs 文件
		FCodeAnalysis::Analysis(OverrideFile);
	}
}

CodeAnalysisOverrideFilesMap = FUnrealCSharpFunctionLibrary::LoadFileToString(FString::Printf(TEXT(
	"%s/%s.json"
),
	*FUnrealCSharpFunctionLibrary::GetCodeAnalysisPath(),
	*OVERRIDE_FILE
));
```

[3] `UnLua / puerts` 都把 truth 留在 UE 侧，但前者藏在 Blueprint graph，后者做成专门资产对象：

```cpp
// ============================================================================
// [3] 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp
// 位置: 177-199, 328-341
// 说明: UnLua 的 truth 被直接写进 Blueprint graph pin 默认值
// ============================================================================
if (!LuaModuleName.IsEmpty())
{
	const auto InterfaceDesc = *Blueprint->ImplementedInterfaces.FindByPredicate([](const FBPInterfaceDescription& Desc)
	{
		return Desc.Interface == UUnLuaInterface::StaticClass();
	});
	InterfaceDesc.Graphs[0]->Nodes[1]->Pins[1]->DefaultValue = LuaModuleName; // ★ truth 就是这里的字符串
}

const auto Func = Blueprint->GeneratedClass->FindFunctionByName(FName("GetModuleName"));
FString ModuleName;
const auto DefaultObject = TargetClass->GetDefaultObject();
DefaultObject->UObject::ProcessEvent(Func, &ModuleName);
const auto FileName = FString::Printf(TEXT("%s%s.lua"), *GLuaSrcFullPath, *RelativePath);

// ============================================================================
// [3] 文件: Reference/puerts/unreal/Puerts/Source/PuertsEditor/Public/PEBlueprintAsset.h
// 位置: 62-90
// 说明: puerts 为 authoring truth 提供了专门资产对象
// ============================================================================
class PUERTSEDITOR_API UPEBlueprintAsset : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintReadOnly, Category = "PEBlueprintAsset")
	UClass* GeneratedClass;

	UPROPERTY(BlueprintReadOnly, Category = "PEBlueprintAsset")
	UBlueprint* Blueprint;

	UPROPERTY(BlueprintReadOnly, Category = "PEBlueprintAsset")
	UPackage* Package;

	UPROPERTY(BlueprintReadOnly, Category = "PEBlueprintAsset")
	bool NeedSave;

	UPROPERTY()
	bool HasConstructor;
};

// ============================================================================
// [3] 文件: Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp
// 位置: 94-165, 1347-1357
// 说明: 资产对象负责 LoadOrCreate / Save / Compile，truth 不再藏在图节点里
// ============================================================================
Blueprint = LoadObject<UBlueprint>(nullptr, *PackageName, nullptr, LOAD_NoWarn | LOAD_NoRedirects);
if (!Blueprint)
{
	Blueprint = FKismetEditorUtilities::CreateBlueprint(
		ParentClass, Package, *InName, BlueprintType, BlueprintClass, BlueprintGeneratedClass, FName("PuertsAutoGen"));
	GeneratedClass = Blueprint->GeneratedClass;
}

auto TypeScriptGeneratedClass = Cast<UTypeScriptGeneratedClass>(GeneratedClass);
NeedSave = NeedSave || (TypeScriptGeneratedClass->HasConstructor != HasConstructor);
TypeScriptGeneratedClass->HasConstructor = HasConstructor;
FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
FKismetEditorUtilities::CompileBlueprint(Blueprint); // ★ authoring truth 写回专门资产并触发 compile
```

[4] `sluaunreal` 本轮能确认的 editor surface 主要还是 profiler，而不是 script authoring artifact：

```cpp
// ============================================================================
// [4] 文件: Reference/sluaunreal/Plugins/slua_unreal/slua_unreal.uplugin
// 位置: 1-28
// 说明: 插件声明只有 runtime `slua_unreal` 和 editor `slua_profile`
// ============================================================================
"Modules": [
  {
    "Name": "slua_unreal",
    "Type": "Runtime",
    "LoadingPhase": "PreLoadingScreen"
  },
  {
    "Name": "slua_profile",
    "Type": "Editor",
    "LoadingPhase": "PreDefault"
  }
]

// ============================================================================
// [4] 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_profile/Private/slua_profile.cpp
// 位置: 53-75
// 说明: 采样到的 editor 入口是 profiler 菜单与 tab，而不是 script asset / blueprint authoring
// ============================================================================
PluginCommands->MapAction(
	Flua_profileCommands::Get().OpenPluginWindow,
	FExecuteAction::CreateRaw(this, &Fslua_profileModule::PluginButtonClicked),
	FCanExecuteAction());

FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
LevelEditorModule.GetMenuExtensibilityManager()->AddExtender(MenuExtender);

sluaProfilerInspector = MakeShareable(new SProfilerInspector);
FGlobalTabmanager::Get()->RegisterNomadTabSpawner(slua_profileTabName,
	FOnSpawnTab::CreateRaw(this, &Fslua_profileModule::OnSpawnPluginTab))
	.SetDisplayName(LOCTEXT("Flua_wrapperTabTitle", "slua Profiler"));
```

#### 对比矩阵（本轮新增观察点）

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 自定义 Content Browser data source / 虚拟根承载脚本条目 | Full | Full | None | None | None | Full |
| authoring truth 明确写进 UE 资产 / package / 类元数据 | Full | Partial | Full | Full | None | Full |
| authoring truth 主要依赖外部文件或映射而不是 UE 资产本身 | None | Full | Partial | Partial | Full | None |
| 插件自带专门 script authoring artifact，而不是隐藏在普通图节点里 | Full | Partial | None | Full | None | Full |
| editor 侧已形成“从当前上下文找回脚本 truth”的闭环 | Full | Full | Full | Full | None | Partial |

#### 小结与建议

- 当前 `Angelscript` 这轮新增确认的强点，不是“按钮更多”，而是 **authoring truth 仍主要落在 UE 资产 / `UASClass` 体系里**。这意味着脚本事实能被 Content Browser、Blueprint 工作流、资产路径和后续 diff/审核共同消费，这条路线建议按 `P0` 保持。
- 最值得吸收的是 `UnrealCSharp` 的 task-oriented authoring，但要吸收的是“围绕当前资产给出最短路径”，不是“把 truth 迁到外部映射”。当前 `Angelscript` 适合在保持资产 truth 的前提下，补一组类似 `Open Script / Analyze / Create Child Blueprint` 的上下文动作，优先级 `P1`。
- `puerts` 说明如果一个工作流需要额外的语义位，比如 `HasConstructor`、生成选项、校验状态，把它做成专门资产字段会比像 `UnLua` 那样藏在 graph pin 里更稳。当前 `Angelscript` 若后续要扩大 editor authoring 面，可以考虑在脚本资产或其 companion object 上显式持有这些元数据，优先级 `P1`。
- `UnLua` 的 pin-default-value 方案说明“最小改动”是可行的，但它更适合作为轻量适配，而不适合作为当前 `Angelscript` 的主 truth contract，优先级 `P2`。`sluaunreal` 这种 runtime path 为主、editor artifact 缺席的路线则不建议作为默认目标，优先级 `P0` 避免。

### [D8] 生命周期 authority：UE GC schema、dual handle registry，还是 Lua registry/cache

前面的 `D8` 已经分别看过优化层级、可观测面和故障遏制；这一轮只补**对象存活 contract 到底由谁说了算**。这里不是“有没有 GC”这种空话，而是：脚本字段、跨语言对象、容器搬移和对象释放时，最终由 UE GC schema、双 runtime handle ledger，还是 Lua/V8 registry/cache 决定对象还能不能活。

#### 各插件实现概览

```
[D8-LifetimeAuthority] Who Owns Reachability
HZ / AS   : UASClass.ReferenceSchema
            ├─ DetectAngelscriptReferences        // 扫脚本字段
            ├─ EmitReferenceInfo                  // 递归声明引用形状
            ├─ AssembleReferenceTokenStream       // 并入 UE GC token stream
            └─ container invalidation             // 容器搬移时失效旧引用

UC        : Mono GCHandle <-> UObject registry
            + FReferenceRegistry(FGCObject)       // UE / Mono 双账本

UnLua     : luaL_ref registry + Auto/Manual ObjectReferencer
puerts    : V8 weak persistent + FObjectRetainer + object cache
slua      : LuaState cache tables + UD_AUTOGC userdata + UObject listeners
```

#### 详细对比

##### 子维度 1：reachability 是直接并入 UE GC，还是交给桥接账本

- 当前 `Angelscript` 与 `Hazelight` 在这条线上最接近“原生 UE”。`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h:34` 与 `J:/UnrealEngine/UEAS2/.../ASClass.h:74` 都给 `UASClass` 挂了 `UE::GC::FSchemaOwner ReferenceSchema`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2827-2835` 在类构建时就 `AssembleReferenceTokenStream()`；`...:4859-4926` 再扫描脚本属性并 `EmitReferenceInfo()`。这意味着脚本字段不是靠一个外部 registry “顺便保活”，而是**直接长进 UE GC schema**。
- `UnrealCSharp` 走的是典型 dual-runtime ledger。`Reference/UnrealCSharp/Script/Weavers/UnrealTypeWeaver.cs:183-236` 先在 managed 类型里注入 `GarbageCollectionHandle` 属性；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Reflection/FClassReflection.cpp:596-616` 创建 handle 后又把它写回对象；`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FObjectRegistry.cpp:69-101` 和 `.../FReferenceRegistry.cpp:6-35` 再分别维护 `UObject <-> handle` 对照、以及 `FGCObject` 形式的 keepalive 集合。所以它不是“没有生命周期管理”，而是 reachability owner 横跨 Mono GC 和 UE GC。
- `UnLua` 的 owner 更偏“Lua registry + UE referencer 混合”。`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/LuaEnv.h:161-171` 里 `FLuaEnv` 同时持有 `AutoObjectReference` / `ManualObjectReference`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ObjectReferencer.h:22-58` 说明它们本质是 `FGCObject`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ObjectRegistry.cpp:113-158,217-246` 则把对象表 `luaL_ref` 到 registry，并通过 manual ref proxy 决定哪些 `UObject` 要被 UE 侧显式保活。owner 是混合的，而不是单边。
- `puerts` 更像“显式双 GC 握手”。`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/ObjectRetainer.cpp:22-58` 的 `FObjectRetainer` 是 UE 侧 keepalive 集；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/CppObjectMapper.cpp:298-327` 给 JS object 设 weak callback；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:1757-1784` 在 JS 拿引用时 `Retain()`，解绑时 `Release()`。这里 owner 不是单一一方，而是桥接层自己维护的双边协议。
- `sluaunreal` 则更偏 `LuaState` / cache own the world。`Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaState.cpp:526-583` 初始化时建立 `cacheObjRef / cacheEnumRef / cacheClassPropRef / cacheClassFuncRef`，并挂上 UObject create/delete listener；`Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaObject.h:136-146,816-857` 又把 userdata 的 `UD_AUTOGC / UD_UOBJECT` 等 flag 作为释放协议的一部分。它不是完全不接 UE，但 reachability 语义明显更偏 LuaState cache。

##### 子维度 2：容器搬移和桥接对象释放时，谁负责兜底

- 当前 `Angelscript / Hazelight` 这轮还有一个其他插件没看到的强点：**容器搬移也被并入生命周期 contract**。`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp:43-84` 与 `J:/UnrealEngine/UEAS2/.../Bind_TArray.cpp:43-80` 在数组重新分配时会 `InvalidateReferencesToMemoryBlock()`，同时 `EmitReferenceInfo()` 又会把数组声明成 `ReferenceArray / StructArray`。这不是普通“保活”，而是把“旧地址失效”也做成了 runtime 规则。
- `UnrealCSharp` 的兜底更多体现在 handle ledger 完整性上。对象从 registry 中移除时，`FObjectRegistry::RemoveReference()` 会同步删掉 `Object2GarbageCollectionHandleMap / GarbageCollectionHandle2Object` 并 `Free` handle；`FReferenceRegistry` 析构时也会把残留 reference handle 一并释放。优势是 bridge 对账清晰；代价是 owner 不再是纯 UE GC。
- `UnLua` 的兜底分成 auto 与 manual 两路。自动路由走 `FObjectReferencer`；手动路由则通过 `FManualRefProxy` 和 `Env->AddManualObjectReference()` / `RemoveManualObjectReference()`。这让它对复杂 Lua 生命周期更灵活，但也意味着“谁该活着”部分要靠上层显式决策。
- `puerts` 的兜底最像协议栈：JS object 走弱引用 finalize，UE object 走 `FObjectRetainer` 保活，解绑时还会把 JS 对象内部 pointer 改成 `RELEASED_UOBJECT`，防止悬空继续访问。这个 contract 很强，但复杂度比 current AS 高一个层级。
- `sluaunreal` 的兜底更多依赖 cache/link 清理和 userdata `__gc`。`LuaObject::addRef/removeRef/cacheObj/removeObjCache` 都挂在 `LuaState` 下，说明对象活多久很大程度由 LuaState cache 生命周期决定，而不是 UE GC token stream。

[1] 当前 `Angelscript / Hazelight` 把脚本引用直接并入 UE GC schema，而且连容器搬移都纳入 contract：

```cpp
// ============================================================================
// [1] 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h
// 位置: 34-35
// 说明: current AS 的类对象自己持有 GC schema owner
// ============================================================================
UE::GC::FSchemaOwner ReferenceSchema;

// ============================================================================
// [1] 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 位置: 2827-2835, 4859-4926
// 说明: 类构建后立刻装配 token stream；脚本字段再被扫描进 schema
// ============================================================================
NewFunction->Next = NewClass->Children;
NewClass->Children = NewFunction;
NewClass->AddFunctionToFunctionMap(NewFunction, NewFunction->GetFName());
NewClass->Bind();
NewClass->StaticLink(true);
NewClass->AssembleReferenceTokenStream(); // ★ 生命周期 owner 继续留在 UE GC

UE::GC::FSchemaBuilder Schema(0);
...
if (!bAddedAsUnrealProperty)
{
	if(PropertyType.HasReferences())
	{
		RefParams.AtOffset = PropertyOffset;
		PropertyType.EmitReferenceInfo(RefParams); // ★ 递归把脚本字段转成 GC schema
	}
}

UE::GC::FSchemaView View(Schema.Build(GetARO(Class)), UE::GC::EOrigin::Other);
Class->ReferenceSchema.Set(View);

// ============================================================================
// [1] 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp
// 位置: 43-84
// 说明: current AS 连容器重分配后的悬空引用都显式失效
// ============================================================================
Context->InvalidateReferencesToMemoryBlock(Array.GetData(), Array.GetAllocatedSize(Ops->NumBytesPerElement));

if (Usage.SubTypes[0].Type->IsObjectPointer())
{
	Params.Schema->Add(UE::GC::DeclareMember(Params.Names.Top(), Params.AtOffset, UE::GC::EMemberType::ReferenceArray, InnerSchema.Build()));
}
else
{
	Usage.SubTypes[0].EmitReferenceInfo(InnerParams);
	Params.Schema->Add(UE::GC::DeclareMember(Params.Names.Top(), Params.AtOffset, UE::GC::EMemberType::StructArray, InnerSchema.Build()));
}

// ============================================================================
// [1] 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_TArray.cpp
// 位置: 43-80
// 说明: Hazelight 也有同类 contract，说明 current AS 继承并保持了这条原生 GC 路线
// ============================================================================
Context->InvalidateReferencesToMemoryBlock(Array.GetData(), Array.GetAllocatedSize(Ops->NumBytesPerElement));
Params.Schema->Add(UE::GC::DeclareMember(Params.Names.Top(), Params.AtOffset, UE::GC::EMemberType::ReferenceArray, InnerSchema.Build()));
```

[2] `UnrealCSharp` 的生命周期 authority 是“GCHandle + registry + FGCObject”双账本：

```cpp
// ============================================================================
// [2] 文件: Reference/UnrealCSharp/Script/Weavers/UnrealTypeWeaver.cs
// 位置: 183-236
// 说明: 先把 `GarbageCollectionHandle` 注入 managed 类型，为双边生命周期准备统一句柄
// ============================================================================
var garbageCollectionHandle = new PropertyDefinition("GarbageCollectionHandle", PropertyAttributes.None,
    ModuleDefinition.TypeSystem.IntPtr);
...
instructions.Add(Instruction.Create(OpCodes.Ldarg_0));
instructions.Add(Instruction.Create(OpCodes.Ldfld, garbageCollectionHandleBackingField));
...
Type.Properties.Add(garbageCollectionHandle); // ★ 每个 managed 对象都能回指自己的 GC handle

// ============================================================================
// [2] 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Reflection/FClassReflection.cpp
// 位置: 596-616
// 说明: native 侧创建 handle 后回写进 managed object
// ============================================================================
auto GarbageCollectionHandle = FMonoDomain::GCHandle_New_V2(InMonoObject, bPinned);
if (const auto FoundProperty = GetProperty(PROPERTY_GARBAGE_COLLECTION_HANDLE))
{
	FoundProperty->SetValue(InMonoObject, InParams, nullptr); // ★ handle 成为跨边界统一 identity
}

// ============================================================================
// [2] 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FObjectRegistry.cpp
// 位置: 69-90
// 说明: UObject 和 handle 建立双向对账表
// ============================================================================
const auto GarbageCollectionHandle = FGarbageCollectionHandle::NewRef(InClass, InMonoObject, true);
Object2GarbageCollectionHandleMap.Add(InObject, GarbageCollectionHandle);
GarbageCollectionHandle2Object.Add(GarbageCollectionHandle, &*InObject);

FGarbageCollectionHandle::Free<false>(*FoundGarbageCollectionHandle);
(void)FCSharpEnvironment::GetEnvironment().RemoveReference(*FoundGarbageCollectionHandle);

// ============================================================================
// [2] 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FReferenceRegistry.cpp
// 位置: 6-27
// 说明: 再用 `FGCObject` 把 UE 侧仍需保活的对象收进来
// ============================================================================
FReferenceRegistry::~FReferenceRegistry()
{
	for (const auto& [PLACEHOLDER, Value] : ReferenceRelationship.Get())
	{
		for (const auto& Reference : Value)
		{
			auto GarbageCollectionHandle = static_cast<FGarbageCollectionHandle>(*Reference);
			FGarbageCollectionHandle::Free<true>(GarbageCollectionHandle);
		}
	}
}

void FReferenceRegistry::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObjects(ObjectArray);
}
```

[3] `UnLua / puerts` 都是“双边 keepalive”，但 `UnLua` 偏 registry + manual proxy，`puerts` 偏 weak finalize：

```cpp
// ============================================================================
// [3] 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/LuaEnv.h
// 位置: 161-171
// 说明: `FLuaEnv` 同时持有 auto/manual 两组 UE referencer
// ============================================================================
FObjectReferencer AutoObjectReference;
FObjectReferencer ManualObjectReference;
UUnLuaManager* Manager = nullptr;
FClassRegistry* ClassRegistry;
FObjectRegistry* ObjectRegistry;

// ============================================================================
// [3] 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ObjectRegistry.cpp
// 位置: 125-158, 217-246
// 说明: 一边把对象表 `luaL_ref` 进 registry，一边按需加 manual keepalive
// ============================================================================
lua_getfield(L, LUA_REGISTRYINDEX, REGISTRY_KEY);
lua_pushlightuserdata(L, Object);
lua_newtable(L); // create a Lua table ('INSTANCE')
...
const auto Ret = luaL_ref(L, LUA_REGISTRYINDEX);
ObjectRefs.Add(Object, Ret); // ★ Lua registry 成为对象 identity owner 之一

Env->AddManualObjectReference(Object);
auto Ptr = lua_newuserdata(L, sizeof(FManualRefProxy));
auto Proxy = new(Ptr)FManualRefProxy;
Proxy->Object = Object; // ★ 手动引用再回写到 UE referencer

// ============================================================================
// [3] 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/ObjectRetainer.cpp
// 位置: 22-58
// 说明: puerts 用 `FGCObject` 风格的 retainer 保活 UE 对象
// ============================================================================
if (!RetainedObjects.Contains(Object))
{
	RetainedObjects.Add(Object);
}

void FObjectRetainer::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObjects(RetainedObjects);
}

// ============================================================================
// [3] 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/CppObjectMapper.cpp
// 位置: 298-327
// 说明: JS 对象侧再挂弱引用 finalize，双 GC 在桥接层汇合
// ============================================================================
CacheNodePtr->Value.Reset(Isolate, JSObject);

if (!PassByPointer)
{
	CacheNodePtr->MustCallFinalize = true;
	CacheNodePtr->Value.SetWeak<JSClassDefinition>(
		ClassDefinition, CDataGarbageCollectedWithFree, v8::WeakCallbackType::kInternalFields);
}
else
{
	CacheNodePtr->Value.SetWeak<JSClassDefinition>(
		ClassDefinition, CDataGarbageCollectedWithoutFree, v8::WeakCallbackType::kInternalFields);
}
```

[4] `sluaunreal` 更像“LuaState cache 持有 authority”，对象活多久强依赖状态机与 userdata flag：

```cpp
// ============================================================================
// [4] 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaState.cpp
// 位置: 534-583
// 说明: LuaState 初始化时就建立对象缓存和 UObject 生命周期监听
// ============================================================================
pgcHandler = FCoreUObjectDelegates::GetPostGarbageCollect().AddRaw(this, &LuaState::onEngineGC);
wcHandler = FWorldDelegates::OnWorldCleanup.AddRaw(this, &LuaState::onWorldCleanup);
GUObjectArray.AddUObjectDeleteListener(this);
GUObjectArray.AddUObjectCreateListener(this);

cacheObjRef = newCacheTable(L);
lua_newtable(L);
cacheEnumRef = luaL_ref(L, LUA_REGISTRYINDEX);
lua_newtable(L);
cacheClassPropRef = luaL_ref(L, LUA_REGISTRYINDEX);
lua_newtable(L);
cacheClassFuncRef = luaL_ref(L, LUA_REGISTRYINDEX); // ★ reachability 语义大量落在 LuaState cache

// ============================================================================
// [4] 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaObject.h
// 位置: 136-146, 816-857
// 说明: userdata flag 本身就是释放 contract 的组成部分
// ============================================================================
#define UD_AUTOGC 1
#define UD_UOBJECT 1<<6
#define UD_USTRUCT 1<<7

ud->flag = gc!=nullptr?UD_AUTOGC:UD_NOFLAG;
if (F) ud->flag |= UD_UOBJECT;

ud->flag = UD_AUTOGC | flag;
if (F) ud->flag |= UD_UOBJECT;
setupMetaTable(L, tn, gcSharedUD<BOXPUD, mode>); // ★ `__gc` 行为由 userdata flag 决定

// ============================================================================
// [4] 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaObject.cpp
// 位置: 2635-2680
// 说明: add/remove ref、link、cache 都委托给 LuaState，而不是 UE GC schema
// ============================================================================
void LuaObject::addRef(lua_State* L,UObject* obj,void* ud,bool ref) {
	auto sl = LuaState::get(L);
	sl->addRef(obj,ud,ref);
}

void LuaObject::removeRef(lua_State* L,UObject* obj,void* ud/*=nullptr*/) {
	auto sl = LuaState::get(L);
	sl->unlinkUObject(obj,ud);
}

void LuaObject::cacheObj(lua_State* L, void* obj) {
	LuaState* ls = LuaState::get(L);
	addCache(L, obj, ls->cacheObjRef);
}
```

#### 对比矩阵（本轮新增观察点）

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 脚本字段 reachability 直接并入 UE GC schema / token stream | Full | None | None | None | None | Full |
| 桥接层显式维护跨 runtime handle / weak-finalizer ledger | None | Full | Partial | Full | Partial | None |
| 通过 `FGCObject` / referencer 形式显式保活 UE 对象 | None | Full | Full | Full | None | None |
| 容器搬移时显式失效旧引用地址 | Full | None | None | None | None | Full |
| 生命周期主 authority 更接近 UE GC，而不是 VM registry / cache | Full | Partial | Partial | Partial | None | Full |

#### 小结与建议

- 当前 `Angelscript` 在这一维真正难得的地方，不只是“能保活 UObject”，而是 **脚本引用已经被翻译成 UE GC 自己能理解的 schema / token stream**。这让生命周期 authority 仍然归 UE，而不是漂到外部 VM registry。这条路线建议按 `P0` 保持。
- `puerts / UnrealCSharp / UnLua` 的桥接账本各有优点，尤其在泄漏诊断、跨 runtime identity 跟踪上更显式。当前 `Angelscript` 值得吸收的是它们的**可观测性**，例如引入可选的 live reference ledger / debug dump，用于解释“谁在持有谁”，但不应把它变成主生命周期 owner，优先级 `P1`。
- `UnLua` 的 manual ref proxy 和 `puerts` 的 weak finalize 提醒了另一个现实：一旦引入脚本 runtime 自己的对象图，生命周期问题会立刻从“GC schema”升级成“桥接协议”。当前 `Angelscript` 如果未来扩展更强的脚本侧对象宿主，应优先把这些对象继续映射回 `ReferenceSchema`，而不是退回 LuaState/cache 式主导，优先级 `P1`。
- `sluaunreal` 的路线说明“把 authority 放在 VM cache”可以工作，但成本是 UE 很难天然理解脚本对象图。对当前 `Angelscript` 来说，这不是可取的默认方向，优先级 `P0` 避免。

---

## 深化分析 (2026-04-09 00:56:15)

### [维度 D6] IDE 真值归属：runtime 导出数据库，还是 build-time 声明工作区

前面的 `D6` 已经分别写过生成器、导航和编辑器入口；这一轮只补一个更决定长期演化方向的判断：**IDE 看到的“真值”到底来自哪里**。源码对比后，可以把 6 个方案分成三类：

- `Hazelight / 当前 Angelscript`：IDE 真值在 runtime 里，靠 `DebugDatabase + SourceNavigation` 把“已经注册成功的脚本表面”回送给工具。
- `UnrealCSharp / puerts`：IDE 真值在 build/editor 产物里，前者是 `.sln/.csproj + Analyzer + Weaver`，后者是 `Typing/ue.d.ts + TypeScript LanguageService`。
- `UnLua / sluaunreal`：中间态。`UnLua` 明确生成 `Intermediate/IntelliSense` 符号镜像，且绑定到资产事件；`sluaunreal` 当前可见 codegen 主要产出 runtime wrapper，而不是正式 IDE symbol artifact。这一项对 `sluaunreal` 的判断带推断性质，依据是本轮只在源码中定位到 `Tools/config.json -> LuaWrapper*.inc` 这条生成链。

```
[D6-TruthOwner] IDE Contract Owner
HZ / AS
├─ runtime registration                           // 真值先在脚本引擎里成立
├─ DebugDatabase JSON                            // IDE 再请求实时类型库
└─ SourceNavigation -> real file                 // 跳转直接回源码文件

UC
├─ template solution / csproj                    // 先生成可编译工作区
├─ Analyzer + Weaver                             // IDE 约束进入构建链
└─ publish workspace                             // runtime 消费发布产物

UL
├─ Editor generator                              // 编辑器里导出符号
├─ Intermediate/IntelliSense                     // 产物落在中间目录
└─ AssetRegistry events                          // 蓝图变更驱动增量刷新

PU
├─ DeclarationGenerator -> ue.d.ts / ue_bp.d.ts // 先生成声明
├─ TypeScript LanguageService                    // IDE 主机常驻
└─ CodeAnalyze pipeline                          // 语义检查与资产同步共用

SL
├─ Tools/config.json -> Private/*.inc            // 可见生成物是 runtime wrapper
└─ no in-tree symbol artifact found              // 本轮未定位到对等 IDE 声明产物
```

#### 详细对比

#### 子维度 1：IDE 真值是在“运行后回读”，还是“运行前先生成”

- `当前 Angelscript` 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:1493-1515` 明确把 `DebugDatabaseSettings` 和序列化后的 JSON 数据库发给客户端；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp:96-138` 再把 `UASClass/UASStruct` 直接映射回真实 `.as` 文件。也就是说，工具看到的是 runtime 已注册成功后的真值，而不是一套预生成 stub。
- `Hazelight` 保持同族路线。`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Debugging/AngelscriptDebugServer.cpp:1368-1393` 同样先发 `DebugDatabaseSettings` 再发 JSON 数据库；`.../AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp:97-132` 则把 VS Code workspace/path 组合进源码跳转参数。它与 current AS 的差异更像字段演化，而不是 owner 改变。
- `UnrealCSharp` 把 IDE contract 前移到工作区生成。`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FSolutionGenerator.cpp:32-106` 连续拷贝 `SourceGenerator`、`Weavers`、项目模板和 `props`；`Reference/UnrealCSharp/Template/Game.props:10-18` 把 `Weavers.dll` 和 `SourceGenerator.csproj` 挂成 `WeaverFiles` 与 `Analyzer`。这意味着 IDE 看到的是“可编译工作区 + 分析器规则”。
- `UnLua` 的真值是 editor 侧镜像。`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:42-107` 把输出目录固定到 `Intermediate/IntelliSense`，并通过 `AssetRegistry` 事件和 `UpdateAll()` 持续重建蓝图/原生类型提示。
- `puerts` 的真值是 `d.ts + LanguageService` 双件套。`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:418-457` 删除旧 `ue.d.ts/ue_bp.d.ts`、复制 `Typing/` 和 `Content/JavaScript`，再写回新的声明文件；`Reference/puerts/unreal/Puerts/PuertsEditor/CodeAnalyze.ts:343-354` 常驻 `ts.createLanguageService(...)`，IDE 语义分析直接消费这套声明。
- `sluaunreal` 当前可见生成物主要是 runtime wrapper。`Reference/sluaunreal/Tools/config.json:65-77` 把输出目录指到 `Plugins/slua_unreal/Source/slua_unreal/Private/`；`Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaWrapper.cpp:55-66` 再按 UE 版本 `#include "LuaWrapper5.x.inc"`。基于当前源码树，本轮没有定位到 `ue.d.ts` / `IntelliSense` 对等级别的 in-tree symbol emitter，因此这里将其判为“runtime wrapper first”，这是源码范围内的推断。

#### 子维度 2：刷新触发是资产事件、语言服务，还是 runtime 请求

- `当前 Angelscript / Hazelight` 的刷新点仍主要是“有客户端请求 debug database”之后再发最新真值；这让 IDE 永远看到的是 runtime 实际表面，但也意味着离线场景下缺少一个稳定的磁盘声明快照。
- `UnrealCSharp` 的更新触发更靠生成/编译链。`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:59-88` 监听 `OnDynamicClassUpdated` 与 `OnEndGenerator`，说明编辑器 code surface 和生成器生命周期是强绑定的，但它的 symbol truth 主要还是 `.sln/.csproj + Analyzer/Weaver`。
- `UnLua` 把刷新做进 `AssetRegistry`。`.../UnLuaIntelliSenseGenerator.cpp:49-53` 注册 `OnAssetAdded/Removed/Renamed/Updated`，`...:58-107` 则在 `UpdateAll()` 里把蓝图和原生类型一起重导。
- `puerts` 把刷新做进 TypeScript host。`Reference/puerts/unreal/Puerts/PuertsEditor/CodeAnalyze.ts:343-354` 的 `LanguageService` 是常驻主机，`...:448-475` 则会在扫描 TS 源文件时检查 `UE.PEBlueprintAsset.Existed(...)` 与目录监听队列。

[1] `Hazelight / 当前 Angelscript` 的 IDE 真值都来自 runtime 已注册表面，而不是磁盘 stub：

```cpp
// ============================================================================
// [1] 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 位置: 1493-1515
// 说明: current AS 先发调试数据库设置，再发 runtime 当前真值的 JSON
// ============================================================================
void FAngelscriptDebugServer::SendDebugDatabase(FSocket* Client)
{
	FAngelscriptDebugDatabaseSettings DebugSettings;
	DebugSettings.bAutomaticImports = FAngelscriptEngine::Get().ShouldUseAutomaticImportMethod();
	DebugSettings.bFloatIsFloat64 = GetDefault<UAngelscriptSettings>()->bScriptFloatIsFloat64;
	SendMessageToClient(Client, EDebugMessageType::DebugDatabaseSettings, DebugSettings);

	auto Root = MakeShared<FJsonObject>();
	FAngelscriptDebugDatabase DB;
	auto JsonWriter = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&DB.Database);
	FJsonSerializer::Serialize(Root, JsonWriter);
	SendMessageToClient(Client, EDebugMessageType::DebugDatabase, DB); // ★ IDE 看到的是 runtime 已注册成功后的表面
}

// ============================================================================
// [1] 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp
// 位置: 96-138
// 说明: 源码跳转直接落回真实 `.as` 文件，而不是跳到生成 stub
// ============================================================================
void OpenModule(TSharedPtr<FAngelscriptModuleDesc> Module, int LineNo = -1)
{
	FString Path = Module->Code[0].AbsoluteFilename;
	if (LineNo != -1)
		FPlatformMisc::OsExecute(nullptr, TEXT("code"), *FString::Printf(TEXT("--goto \"%s:%d\""), *Path, LineNo));
}

void RegisterAngelscriptSourceNavigation()
{
	FSourceCodeNavigation::AddNavigationHandler(new FAngelscriptSourceCodeNavigation);
}

// ============================================================================
// [1] 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Debugging/AngelscriptDebugServer.cpp
// 位置: 1368-1393
// 说明: Hazelight 也是同一路线，只是 settings 字段稍多
// ============================================================================
void FAngelscriptDebugServer::SendDebugDatabase(FSocket* Client)
{
	FAngelscriptDebugDatabaseSettings DebugSettings;
	DebugSettings.bAutomaticImports = true;
	DebugSettings.bFloatIsFloat64 = GetDefault<UAngelscriptSettings>()->bScriptFloatIsFloat64;
	SendMessageToClient(Client, EDebugMessageType::DebugDatabaseSettings, DebugSettings);
	FAngelscriptDebugDatabase DB;
	SendMessageToClient(Client, EDebugMessageType::DebugDatabase, DB);
}
```

[2] `UnrealCSharp / UnLua / puerts / sluaunreal` 则把 IDE 产物前置到 build/editor 侧：

```csharp
// ============================================================================
// [2] 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FSolutionGenerator.cpp
// 位置: 32-106
// 说明: UnrealCSharp 直接生成 IDE 工作区及其 Analyzer / Weaver 依赖
// ============================================================================
CopyTemplate(FPaths::Combine(FUnrealCSharpFunctionLibrary::GetSourceGeneratorPath(), SOURCE_GENERATOR_NAME + PROJECT_SUFFIX),
    ScriptPath / SOURCE_GENERATOR_NAME / SOURCE_GENERATOR_NAME + PROJECT_SUFFIX, ...);
CopyTemplate(FPaths::Combine(FUnrealCSharpFunctionLibrary::GetWeaversPath(), WEAVERS_NAME + PROJECT_SUFFIX),
    ScriptPath / WEAVERS_NAME / WEAVERS_NAME + PROJECT_SUFFIX, ...);
CopyTemplate(FUnrealCSharpFunctionLibrary::GetGameProjectPath(),
    TemplatePath / DEFAULT_GAME_NAME + PROJECT_SUFFIX, ...);

// ============================================================================
// [2] 文件: Reference/UnrealCSharp/Template/Game.props
// 位置: 10-18
// 说明: Analyzer 和 Weaver 被正式接进 csproj，而不是额外脚本
// ============================================================================
<WeaverFiles Include="$(ProjectDir)..\Weavers\bin\$(Configuration)\netstandard2.0\Weavers.dll" WeaverClassNames="UnrealTypeWeaver" />
<ProjectReference Include="..\SourceGenerator\SourceGenerator.csproj" OutputItemType="Analyzer" ReferenceOutputAssembly="false"/>
<ProjectReference Include="..\Weavers\Weavers.csproj" ReferenceOutputAssembly="false" />

// ============================================================================
// [2] 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp
// 位置: 42-107
// 说明: UnLua 生成 `Intermediate/IntelliSense`，并把蓝图/原生类型一起导出
// ============================================================================
OutputDir = IPluginManager::Get().FindPlugin("UnLua")->GetBaseDir() + "/Intermediate/IntelliSense";
AssetRegistryModule.Get().OnAssetAdded().AddRaw(this, &FUnLuaIntelliSenseGenerator::OnAssetAdded);
AssetRegistryModule.Get().OnAssetUpdated().AddRaw(this, &FUnLuaIntelliSenseGenerator::OnAssetUpdated);
...
ExportUE(NativeTypes);
ExportUnLua(); // ★ 符号镜像是 editor 产物，不依赖 runtime 调试连接
```

```cpp
// ============================================================================
// [3] 文件: Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp
// 位置: 418-457
// 说明: puerts 把 `ue.d.ts` / `ue_bp.d.ts` 直接写回项目侧 Typing 目录
// ============================================================================
PlatformFile.DeleteFile(*(PuertsBaseDir / TEXT("Typing/ue/ue.d.ts")));
PlatformFile.DeleteFile(*(PuertsBaseDir / TEXT("Typing/ue/ue_bp.d.ts")));
PlatformFile.CopyDirectoryTree(*ProjectTypingDir, *(PuertsBaseDir / TEXT("Typing")), false);
FFileHelper::SaveStringToFile(ToString(), *UEDeclarationFilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
FFileHelper::SaveStringToFile(ToString(), *BPDeclarationFilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

// ============================================================================
// [3] 文件: Reference/puerts/unreal/Puerts/PuertsEditor/CodeAnalyze.ts
// 位置: 343-354
// 说明: TypeScript LanguageService 是编辑器语义主机，而不是离线文本导出
// ============================================================================
let service = ts.createLanguageService(servicesHost, ts.createDocumentRegistry());

function getProgramFromService() {
    while(true) {
        try {
            return service.getProgram();
        } catch (e) {
            console.error(e);
        }
        service = ts.createLanguageService(servicesHost, ts.createDocumentRegistry());
    }
}

// ============================================================================
// [3] 文件: Reference/sluaunreal/Tools/config.json
// 位置: 65-77
// 说明: 当前可见生成链把输出直接打到 runtime 私有目录，服务对象是 wrapper，不是 IDE 符号文件
// ============================================================================
"output_dir": "{solution_dir}/Plugins/slua_unreal/Source/slua_unreal/Private/",
"win": {
    "solution_dir": "../",
    "ue4_dir": "C:/Program Files/Epic Games/UE_5.2",
    "ue_vcproj": "{solution_dir}/Intermediate/ProjectFiles/UE5.vcxproj",
    "include_path": "...",
    "preprocess": "..."
}

// ============================================================================
// [3] 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaWrapper.cpp
// 位置: 55-66
// 说明: 生成结果在编译时作为版本分片 wrapper 被直接 include
// ============================================================================
#if ((ENGINE_MINOR_VERSION<25) && (ENGINE_MAJOR_VERSION==4))
    #include "LuaWrapper4.18.inc"
#elif ((ENGINE_MINOR_VERSION==4) && (ENGINE_MAJOR_VERSION==5))
    #include "LuaWrapper5.4.inc"
#endif
```

#### 对比矩阵（本轮新增观察点）

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| runtime 导出 live type DB 作为 IDE 真值 | Full | None | None | None | None | Full |
| build/editor 阶段生成正式声明/工作区产物 | None | Full | Full | Full | Partial | None |
| 符号刷新直接绑定资产/编辑器事件 | None | Partial | Full | Full | None | None |
| IDE 主机内置正式 analyzer / language service | None | Full | Partial | Full | None | None |
| 当前可见 codegen 主要服务 runtime wrapper 而非 IDE 符号 | None | None | None | None | Full | None |

#### 小结与建议

- `Angelscript` 不该丢掉 `DebugDatabase` 这条路线；它的价值是 **IDE 看到的一定是 runtime 真正接受的表面**，这比 stub 更权威，优先级 `P0` 保持。
- 真正值得吸收的是 `UnLua / puerts` 的“离线可消费 symbol artifact”。建议在保留 runtime truth 的前提下，补一层可缓存到磁盘的 `DebugDatabase snapshot` 或轻量声明文件，优先级 `P1`。
- `UnrealCSharp` 的 `.sln + Analyzer + Weaver` 说明“IDE 约束进入构建链”可以极大提升 authoring 质量。但对当前 `Angelscript` 来说，直接照搬整套工程工作区过重，建议只吸收“前置校验”和“明确产物目录”的思路，优先级 `P2`。

### [维度 D7] 编辑器变更权限：谁有权自动改项目内容，谁只能提供入口

前面的 `D7` 已经覆盖过菜单、内容浏览器和部分 editor hook；这一轮只聚焦一个更实际的问题：**编辑器到底有多大权限去改项目内容**。这一条拉开了几家方案的边界：

- `Hazelight` 最激进，它不仅有插件自己的 `ContentBrowserDataSource`，还直接改写了引擎原生 `GameModeInfoCustomizer` 和 `AIGraphTypes`。
- `当前 Angelscript` 明显更保守：允许显式弹窗创建 Blueprint / DataAsset，但 `AngelscriptDataSource` 自己不负责直接编辑底层源码文件。
- `UnrealCSharp` 把代码文件变成 Content Browser 一等项，允许 Add New / Edit / Delete，甚至删除时走 undo + 立即重新编译。
- `puerts` 把“自动改 Blueprint 资产”前移到 `CodeAnalyze.ts`，TS 语义扫描完成后会驱动 `PEBlueprintAsset` 保存与 `CompileBlueprint`。
- `UnLua / sluaunreal` 当前源码更像 helper-oriented surface。`UnLuaEditor` 负责 toolbar、目录监听和保存钩子；`slua_profile` 负责 profiler 面板和 tick。基于本轮检索，未定位到与 `UDynamicDataSource` / `PEBlueprintAsset` 对等的 code-backed asset owner，这一判断对 `UnLua` 带推断性质。

```
[D7-MutationAuthority] Editor Mutation Owner
HZ
├─ plugin data source                             // 插件自有脚本资产面
├─ modal blueprint creation                       // 显式创建资产
└─ engine-native patches                          // 原生 class picker / AI graph 直接识别 script class

AS
├─ plugin data source                             // 只展示 Script Asset
├─ modal save dialog                              // 显式创建 Blueprint / DataAsset
└─ no item-level edit/delete in data source       // 数据源本身不改源码文件

UC
├─ Add New Dynamic Class                          // 直接在 Content Browser 建代码项
├─ Edit / Delete file with undo                   // 代码文件是 first-class item
└─ compile on change                              // 改动后立刻触发编译

UL
├─ toolbar / watcher / package-save hooks         // helper-oriented editor surface
└─ no code-backed asset owner found               // 本轮检索未见对等内容 owner

PU
├─ CodeAnalyze auto-start                         // 编辑器启动即接管分析
├─ PEBlueprintAsset reconcile                     // 根据源码自动同步 BP 资产
└─ CompileBlueprint on save                       // 资产回写后立刻编译

SL
└─ profiler tab + menu                            // 当前 editor 面主要是观测，不是内容变更
```

#### 详细对比

#### 子维度 1：编辑器能不能把“代码/脚本”当一等内容项

- `UnrealCSharp` 是最彻底的。`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:59-88` 往 `ContentBrowser.AddNewContextMenu` 注入动态段；`...:523-590` 则允许 `EditItem()` 直接 `OpenSourceFile()`，并在 `DeleteItem()` 中走 `FScopedTransaction + GUndo + ImmediatelyCompile()`。这不是“菜单打开文件”，而是 editor 正式拥有底层代码文件。
- `当前 Angelscript` 则是“资产面”和“源码面”分离。`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:111-119` 激活的是 `AngelscriptData`；`.../AngelscriptContentBrowserDataSource.cpp:17-29` 创建的是 `Category_Asset` 条目；`...:182-199` 明确 `GetItemPhysicalPath/CanEditItem/EditItem/BulkEditItems` 全部返回 `false`。源码编辑仍走 `SourceNavigation`，不是由 Content Browser item 接管。
- `Hazelight` 在插件自有 surface 上与 current AS 同族，`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:133-140` 同样激活 `AngelscriptData`；但它额外侵入了引擎原生 UI。

#### 子维度 2：资产变更是显式用户动作，还是背景分析器自动回写

- `当前 Angelscript` 的项目结构改动明显偏显式。`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:433-494` 先推导默认路径，再弹 `CreateModalSaveAssetDialog()`；这条链要求用户确认目标包名和资产名后才真正创建 Blueprint。
- `Hazelight` 也保留这条显式创建链，但更进一步让引擎原生浏览器和图编辑器“认识” script class。`J:/UnrealEngine/UEAS2/Engine/Source/Editor/UnrealEd/Public/GameModeInfoCustomizer.h:312-327` 直接允许 `bIsScriptClass` 进入浏览/跳转；`.../AIGraphTypes.cpp:60-93` 又让脚本类节点显示正常名字。
- `puerts` 则把变更权限交给代码分析器。`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:150-151` 启动时直接 `JsEnv->Start("PuertsEditor/CodeAnalyze")`；`Reference/puerts/unreal/Puerts/PuertsEditor/CodeAnalyze.ts:447-475` 在扫描源码时会检查 `UE.PEBlueprintAsset.Existed(...)` 并驱动刷新队列；`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:1345-1358` 则在 `Save()` 中 `MarkBlueprintAsModified()` 并 `CompileBlueprint()`。这里 editor 拥有“后台自动对齐 Blueprint 资产”的权限。
- `UnLua` 的 editor owner 更像“辅助者”。`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:48-70,88-105` 负责 toolbar、目录监听、包保存事件和 `IntelliSenseGenerator` 初始化。基于本轮对 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/` 检索 `ContentBrowserDataSource|CreateBlueprint|CreateModalSaveAssetDialog|OpenSourceFile` 未命中，可以推断它没有把 editor 变成一个 code-backed content owner。
- `sluaunreal` 目前的 editor 变更面最小。`Reference/sluaunreal/Plugins/slua_unreal/Source/slua_profile/Private/slua_profile.cpp:48-83` 的 `StartupModule()` 基本只注册菜单、Nomad Tab 和 tick。

[1] `Hazelight / 当前 Angelscript` 把变更权限收在“显式用户动作 + 少量原生 UI 接管”：

```cpp
// ============================================================================
// [1] 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 位置: 433-494
// 说明: current AS 创建 Blueprint 前必须先走显式保存弹窗
// ============================================================================
if (!AssetPath.StartsWith(TEXT("/")))
{
	FString ScriptRelativePath = Class->GetRelativeSourceFilePath();
	...
	AssetPath = InitialDirectory / AssetPath;
}

FSaveAssetDialogConfig SaveAssetDialogConfig;
SaveAssetDialogConfig.DefaultPath = FPaths::GetPath(AssetPath);
SaveAssetDialogConfig.DefaultAssetName = FPaths::GetCleanFilename(AssetPath);
...
FString SaveObjectPath = ContentBrowserModule.Get().CreateModalSaveAssetDialog(SaveAssetDialogConfig);
if (!SaveObjectPath.IsEmpty())
{
	const FString UserPackageName = FPackageName::ObjectPathToPackageName(SaveObjectPath);
	...
} // ★ 变更项目内容前必须用户确认包路径和资产名

// ============================================================================
// [1] 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp
// 位置: 17-29, 182-199
// 说明: 当前数据源负责展示 Script Asset，但不接管底层源码编辑
// ============================================================================
return FContentBrowserItemData(
	this,
	EContentBrowserItemFlags::Type_File | EContentBrowserItemFlags::Category_Asset,
	*(TEXT("/All/Angelscript/") + Asset->GetName()), ...);

bool UAngelscriptContentBrowserDataSource::GetItemPhysicalPath(...) { return false; }
bool UAngelscriptContentBrowserDataSource::CanEditItem(...) { return false; }
bool UAngelscriptContentBrowserDataSource::EditItem(...) { return false; }
bool UAngelscriptContentBrowserDataSource::BulkEditItems(...) { return false; }

// ============================================================================
// [1] 文件: J:/UnrealEngine/UEAS2/Engine/Source/Editor/UnrealEd/Public/GameModeInfoCustomizer.h
// 位置: 312-327
// 说明: Hazelight 额外把原生 GameMode 浏览器改成直接认识 script class
// ============================================================================
bool CanSyncToClass(const UClass* Class) const
{
	return (Class != NULL && (Class->ClassGeneratedBy != NULL || Class->bIsScriptClass));
}

if (Class->bIsScriptClass)
{
	FSourceCodeNavigation::NavigateToClass(Class);
	return;
}
```

[2] `UnrealCSharp / puerts` 则给了 editor 更强的自动改写权限：

```cpp
// ============================================================================
// [2] 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp
// 位置: 59-88, 523-590
// 说明: UnrealCSharp 把代码文件作为 Content Browser 一等项，可直接编辑/删除
// ============================================================================
if (const auto Menu = UToolMenus::Get()->ExtendMenu("ContentBrowser.AddNewContextMenu"))
{
	Menu->AddDynamicSection(..., [WeakThis = TWeakObjectPtr<UDynamicDataSource>(this)](UToolMenu* InMenu)
	{
		if (WeakThis.IsValid())
		{
			WeakThis->PopulateAddNewContextMenu(InMenu);
		}
	});
}

bool UDynamicDataSource::EditItem(const FContentBrowserItemData& InItem)
{
	return ItemDataPayload
		? FSourceCodeNavigation::OpenSourceFile(ItemDataPayload->GetInternalPath().ToString())
		: false;
}

FScopedTransaction Transaction(LOCTEXT(LOCTEXT_NAMESPACE, "Delete File"));
DeleteFileChange->Apply(nullptr);
GUndo->StoreUndo(this, MoveTemp(DeleteFileChange)); // ★ 删除代码文件也进入 undo 栈

// ============================================================================
// [2] 文件: Reference/puerts/unreal/Puerts/PuertsEditor/CodeAnalyze.ts
// 位置: 447-475
// 说明: puerts 在后台扫描 TS 源文件时，就会判定 Blueprint 资产是否需要重建/刷新
// ============================================================================
const {moduleFileName, modulePath} = getClassPathInfo(fileName);
let BPExisted = false;
if (moduleFileName) {
    BPExisted = UE.PEBlueprintAsset.Existed(moduleFileName, modulePath);
}
if (!versionsFileExisted || restoredFileVersions[fileName].version != fileVersions[fileName].version || !BPExisted) {
    onSourceFileAddOrChange(fileName, false, program, false);
    changed = true;
}

// ============================================================================
// [2] 文件: Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp
// 位置: 1345-1358
// 说明: 资产保存会直接修改 Blueprint 并触发编译
// ============================================================================
if (NeedSave)
{
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint); // ★ 背景分析器驱动的资产回写最终会落成编译动作
}
```

[3] `UnLua / sluaunreal` 的 editor surface 更偏 helper，而不是内容 owner：

```cpp
// ============================================================================
// [3] 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp
// 位置: 48-70, 88-105
// 说明: UnLuaEditor 负责 toolbar、目录监听、打包设置和 IntelliSense 初始化
// ============================================================================
virtual void StartupModule() override
{
	FCoreDelegates::OnPostEngineInit.AddRaw(this, &FUnLuaEditorModule::OnPostEngineInit);
	MainMenuToolbar = MakeShareable(new FMainMenuToolbar);
	BlueprintToolbar = MakeShareable(new FBlueprintToolbar);
	UUnLuaEditorFunctionLibrary::WatchScriptDirectory();
	SetupPackagingSettings();
}

void OnPostEngineInit()
{
	MainMenuToolbar->Initialize();
	BlueprintToolbar->Initialize();
	FUnLuaIntelliSenseGenerator::Get()->Initialize();
}

// ============================================================================
// [3] 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_profile/Private/slua_profile.cpp
// 位置: 48-83
// 说明: slua 的 editor 面主要是 profiler 菜单、tab 和 tick
// ============================================================================
void Fslua_profileModule::StartupModule()
{
	PluginCommands->MapAction(...);
	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	...
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(slua_profileTabName,
		FOnSpawnTab::CreateRaw(this, &Fslua_profileModule::OnSpawnPluginTab));
}
```

#### 对比矩阵（本轮新增观察点）

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 引擎原生 editor surface 直接识别脚本类 | Full | None | None | None | None | None |
| 插件拥有自己的 content/code browser surface | Full | Full | None | None | None | Full |
| 从 editor surface 直接 edit/delete 底层代码文件 | None | Full | None | None | None | None |
| 自动根据源码分析回写 Blueprint 资产 | None | None | None | Full | None | None |
| 显式弹窗创建 Blueprint / DataAsset | Full | None | None | Partial | None | Full |

#### 小结与建议

- `Angelscript` 当前这条“显式确认后再改项目内容”的边界是健康的，优先级 `P0` 保持。它比 `puerts` 的后台资产对齐更可预测，也更符合插件独立交付场景。
- 真正值得吸收的是 `UnrealCSharp` 的“代码项一等公民”体验，但建议只吸收到 **显式命令 + 明确 undo** 的层级，不要默认允许后台脚本分析器直接改项目资产，优先级 `P1`。
- `Hazelight` 的引擎原生 surface patch 很强，但这条路线成本最高。当前 `Angelscript` 若要提升 editor 渗透率，应优先尝试插件自有 surface 或 editor extender，而不是重新引入 engine patch 依赖，优先级 `P2`。

### [维度 D11] 交付物身份：是源码目录、publish 目录、缓存快照，还是宿主 loader

前面的 `D11` 已经写过 staging 和预编译；这一轮只补一个更核心的判断：**最终交付物的“身份 authority”到底在谁手里**。横向对比后可以看到 5 条完全不同的路线：

- `Hazelight / 当前 Angelscript`：authority 在插件自己手里，正式交付物包括 `Binds.Cache`、`PrecompiledScript*.Cache`，current AS 还多了一层 `BindModules.Cache`；缓存有效性再由 `BuildIdentifier` 闸门约束。
- `UnrealCSharp`：authority 在 `PublishDirectory`。`AfterBuildPublish` 把 DLL 发布并复制到脚本输出目录，editor 再把这个 publish 目录强制加入 `DirectoriesToAlwaysStageAsUFS`。
- `UnLua`：authority 在原始脚本目录树。`ProjectContent/Script` 和扩展插件的 `Content/Script` 被直接加入 UFS staging。
- `puerts`：authority 分裂为两层，一层是项目/插件里的 `Typing` 与 `Content/JavaScript`，另一层是 UBT staged 的 `v8/node` 动态库。它更像“脚本内容 + backend runtime”双件交付。
- `sluaunreal`：authority 很大程度交回宿主。`LuaState::loadFile()` 优先走 `LoadFileDelegate`，插件本身不强制规定脚本必须来自哪个目录或哪种包格式。

```
[D11-ArtifactIdentity] Delivery Authority
HZ / AS
├─ ScriptRoot                                     // 仍有源码根
├─ Binds.Cache / PrecompiledScript*.Cache         // 正式交付物之一
├─ BindModules.Cache (AS only)                    // 当前额外多一层模块缓存
└─ BuildIdentifier gate                           // build 不匹配则拒收

UC
├─ PublishDirectory under ProjectContent          // 发布目录是主身份
├─ AfterBuildPublish -> copy dlls                 // 构建后生成正式产物
└─ DirectoriesToAlwaysStageAsUFS                  // 打包时强制随包走

UL
├─ ProjectContent/Script                          // 原始脚本目录
├─ Plugin Content/Script                          // 扩展脚本目录
└─ stage as UFS                                   // 文本脚本就是主交付物

PU
├─ copy Typing / Content/JavaScript               // 内容层交付物
└─ RuntimeDependencies.Add(...)                   // backend 动态库交付物

SL
└─ LoadFileDelegate(fn, filepath)                 // 宿主回调决定字节来源
```

#### 详细对比

#### 子维度 1：主交付物是“源码树”、`publish` 目录，还是 cache/snapshot

- `当前 Angelscript` 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1466-1477` 启动即读取 `Binds.Cache` 和 `BindModules.Cache`；`...:1517-1538` 再优先找 `PrecompiledScript_Shipping/Test/Development.Cache`，找不到才回退 `PrecompiledScript.Cache`。这说明它的正式交付物已经不只是 `Script/` 源文件。
- `Hazelight` 同样把 `Binds.Cache + PrecompiledScript*.Cache` 作为正式交付物，见 `J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/AngelscriptManager.cpp:404-427,442-461`。与 current AS 的差异在于，它还没有 `BindModules.Cache` 这一层。
- `UnrealCSharp` 的主身份非常明确：`Reference/UnrealCSharp/Template/Game.props:16-27` 在 `AfterBuildPublish` 后执行 `Publish`，再把 `@(PublishFiles)` 复制到 `$(ScriptOutputPath)`；`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:209-233` 把 `PublishDirectory` 强制加入 UFS staging。这里真正被交付的是发布后的 DLL 集合，而不是源码树。
- `UnLua` 则相反。`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaFunctionLibrary.cpp:20-23` 直接把脚本根定为 `ProjectContent/Script/`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:186-239` 再把 `Script`、`../Plugins/UnLua/Content/Script` 和扩展插件脚本目录都加入 `DirectoriesToAlwaysStageAsUFS`。
- `puerts` 的源码里可以直接看到“双件交付”。`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:418-426` 会复制 `Typing/` 和 `Content/JavaScript`；`Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs:361-367,517-523` 则把 `v8.dll`、`libnode.dll` 一类 native runtime 作为 `RuntimeDependencies` staged。
- `sluaunreal` 的主身份反而被抽空。`Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaState.cpp:153-155` 中 `loadFile()` 先询问 `loadFileDelegate`，`...:651-652` 再由 `setLoadFileDelegate()` 注入宿主回调。也就是说，插件并不要求脚本必须落在某个固定目录里；最终 authority 在宿主的 loader。

#### 子维度 2：交付物有没有显式 build/version 闸门

- `当前 Angelscript` 与 `Hazelight` 都有明确 build gate。`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp:2627-2644` 和 `J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/StaticJIT/PrecompiledData.cpp:2575-2592` 都把 `UE_BUILD_DEBUG/DEVELOPMENT/TEST/SHIPPING` 映射成 `BuildIdentifier`，并用 `IsValidForCurrentBuild()` 拒绝错误 build 消费缓存。
- `UnrealCSharp / UnLua / puerts` 在本轮读取的打包源码里都没有看到与 `BuildIdentifier` 对等的 artifact gate。它们当然各自有发布目录或后端版本约束，但当前可见源码没有把“某份脚本/声明/发布物只能给某个 build 吃”做成正式的统一闸门。
- `sluaunreal` 的 `LoadFileDelegate` 路线把版本问题推给了宿主协议；插件自己没有给出一个统一的 build gate。

[1] `Hazelight / 当前 Angelscript` 都把 cache/snapshot 做成正式交付物，而且有显式 build gate：

```cpp
// ============================================================================
// [1] 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 1466-1477, 1517-1538
// 说明: current AS 先读 bind/cache，再按 build 配置选择预编译脚本 cache
// ============================================================================
#if AS_USE_BIND_DB
FAngelscriptBindDatabase::Get().Load(GetScriptRootDirectory() / TEXT("Binds.Cache"), bGeneratePrecompiledData);
#endif

if (plugin)
{
	FAngelscriptBinds::LoadBindModules(plugin->GetBaseDir() / "BindModules.Cache"); // ★ current AS 额外多一层模块缓存
}

#if UE_BUILD_SHIPPING
Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript_Shipping.Cache");
#elif UE_BUILD_DEVELOPMENT
Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript_Development.Cache");
#endif
if (IFileManager::Get().FileExists(*Filename))
{
	PrecompiledData = new FAngelscriptPrecompiledData(Engine);
	PrecompiledData->Load(Filename);
	if (!PrecompiledData->IsValidForCurrentBuild())
	{
		delete PrecompiledData;
		PrecompiledData = nullptr;
	}
}

// ============================================================================
// [1] 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp
// 位置: 2627-2644
// 说明: build id 是正式 artifact gate，不是日志级提示
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
// [2] 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/AngelscriptManager.cpp
// 位置: 404-427, 442-461
// 说明: Hazelight 同样以 bind DB + precompiled cache 为正式交付物
// ============================================================================
#if AS_USE_BIND_DB
FAngelscriptBindDatabase::Get().Load(GetScriptRootDirectory() / TEXT("Binds.Cache"), bGeneratePrecompiledData);
#endif

UE_LOG(Angelscript, Log, TEXT("Writing angelscript bind database to Binds.Cache file"));
FAngelscriptBindDatabase::Get().Save(GetScriptRootDirectory() / TEXT("Binds.Cache"));

#if UE_BUILD_SHIPPING
Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript_Shipping.Cache");
#elif UE_BUILD_DEVELOPMENT
Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript_Development.Cache");
#endif
if (IFileManager::Get().FileExists(*Filename))
{
	PrecompiledData = new FAngelscriptPrecompiledData(Engine);
	PrecompiledData->Load(Filename);
	if (!PrecompiledData->IsValidForCurrentBuild())
	{
		delete PrecompiledData;
		PrecompiledData = nullptr;
	}
}
```

[2] `UnrealCSharp / UnLua` 的 authority 则更偏目录式 staging：

```xml
<!-- =========================================================================
     [3] 文件: Reference/UnrealCSharp/Template/Game.props
     位置: 16-27
     说明: 发布后的 DLL 会被复制到脚本输出目录，publish 目录就是正式交付物
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
```

```cpp
// ============================================================================
// [3] 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp
// 位置: 209-233
// 说明: editor 再把 publish 目录加入 UFS staging
// ============================================================================
const auto PublishDirectory = FUnrealCSharpFunctionLibrary::GetPublishDirectory();
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

// ============================================================================
// [3] 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp
// 位置: 995-1007
// 说明: PublishDirectory 本身就是一个正式配置项
// ============================================================================
FString FUnrealCSharpFunctionLibrary::GetPublishDirectory()
{
	return UnrealCSharpSetting->GetPublishDirectory();
}
FString FUnrealCSharpFunctionLibrary::GetFullPublishDirectory()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / GetPublishDirectory());
}

// ============================================================================
// [3] 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaFunctionLibrary.cpp
// 位置: 20-23
// 说明: UnLua 直接把 Content/Script 当主交付物
// ============================================================================
FString UUnLuaFunctionLibrary::GetScriptRootPath()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() + TEXT("Script/"));
}

// ============================================================================
// [3] 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp
// 位置: 186-239
// 说明: 所有脚本目录都会被加入 UFS staging
// ============================================================================
auto ScriptPaths = TArray<FString>{TEXT("Script"), TEXT("../Plugins/UnLua/Content/Script")};
...
PackagingSettings->DirectoriesToAlwaysStageAsUFS.Add(DirectoryPath); // ★ 原始脚本目录就是交付物
```

[3] `puerts / sluaunreal` 则把 authority 放在 backend runtime 或宿主 loader：

```csharp
// ============================================================================
// [4] 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs
// 位置: 174-181, 361-367, 517-523
// 说明: puerts 同时复制内容/声明目录，并把 backend 动态库作为 RuntimeDependencies staged
// ============================================================================
string coreJSPath = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", "Content"));
string destDirName = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", "..", "..", "Content"));
DirectoryCopy(coreJSPath, destDirName, true);

string srcDtsDirName  = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", "Typing"));
string dstDtsDirName = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", "..", "..", "Typing"));
DirectoryCopy(srcDtsDirName, dstDtsDirName, true);

var DestDllPath = Path.Combine("$(BinaryOutputDir)", DllName);
RuntimeDependencies.Add(DestDllPath, DllPath, StagedFileType.NonUFS);
RuntimeDependencies.Add("$(TargetOutputDir)/libnode.dll", Path.Combine(V8LibraryPath, "libnode.dll"));

// ============================================================================
// [4] 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaState.cpp
// 位置: 153-155, 651-652
// 说明: slua 把脚本字节来源交给宿主回调，插件不强制规定统一目录布局
// ============================================================================
TArray<uint8> LuaState::loadFile(const char* fn,FString& filepath) {
	if(loadFileDelegate) return loadFileDelegate(fn,filepath); // ★ authority 在宿主 loader
	return TArray<uint8>();
}

void LuaState::setLoadFileDelegate(LoadFileDelegate func) {
	loadFileDelegate = func;
}
```

#### 对比矩阵（本轮新增观察点）

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 主交付物以 raw script/text 目录树为主 | Partial | None | Full | Partial | None | Partial |
| 主交付物以 published binary/runtime 目录为主 | None | Full | None | None | None | None |
| 正式交付物包含 bind/precompiled cache | Full | None | None | None | None | Full |
| build 规则显式 staged 外部 runtime 二进制 | None | Partial | Partial | Full | None | None |
| 交付物消费前有显式 build/version gate | Full | None | None | None | None | Full |
| 宿主可通过 callback 完全接管脚本字节来源 | None | None | None | None | Full | None |

#### 小结与建议

- `Angelscript` 当前最值得坚持的是 **cache/snapshot + build gate** 组合。这让它在交付一致性上明显强于 raw script 路线，优先级 `P0` 保持。
- 真正值得补的是“交付物目录语义更清楚”。`UnrealCSharp` 的 `PublishDirectory`、`UnLua` 的 `Script` staging 都比 current AS 的“源码根 + 多种 cache 文件”更容易被外部团队理解。建议为 `Angelscript` 增加一个明确的 deliverable contract 文档，必要时补 `ScriptArtifacts/` 之类的显式输出根，优先级 `P1`。
- `sluaunreal` 的 `LoadFileDelegate` 说明“把 authority 交给宿主 loader”可以满足更灵活的线上分发，但会显著削弱插件自己的 artifact 治理。对当前 `Angelscript` 来说，更适合把这类能力做成高级扩展点，而不是默认交付模型，优先级 `P2`。

---

## 深化分析 (2026-04-09 01:17:05)

### [D2] 泛型容器的 authority：脚本拿到的是“原生值类型模板”，还是“wrapper/helper object”

#### 各插件实现概览

```
[D2-ContainerCarrier] Generic Container Ownership
HZ/AS : AngelScript template value type -> FScriptArray/FScriptMap/FScriptSet
UC    : GC handle registry -> FArrayHelper/FMapHelper/FSetHelper
UL    : weak ScriptContainerMap -> FLuaArray/FLuaMap/FLuaSet userdata
PU    : JS wrapper object -> internal fields(ptr + translator) + d.ts facade
SL    : LuaArray/LuaMap/LuaSet wrapper -> FProperty-driven ref/copy split
```

这一轮补到源码后，`D2` 上最关键的新结论不是“谁支持 `TArray/TMap/TSet`”。六家都支持某种形式的 UE 容器桥接，真正拉开差异的是 **容器本体最终落在哪个 authority object 上**。当前 Angelscript / Hazelight 把容器直接做成 AngelScript 模板值类型；UnrealCSharp / UnLua / puerts / sluaunreal 则都把容器先包进 helper 或 wrapper，再交给脚本语言表面。

#### 详细对比

##### 子维度 1：容器 carrier 是原生值类型，还是运行时 wrapper

- `当前 Angelscript` 与 `Hazelight` 走的是最“类型系统内化”的路线。`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp:1345-1492,1634-1695` 与 `J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Binds\Bind_TArray.cpp:1344-1492,1634-1695` 把 `FScriptArray` 直接注册成 `TArray<class T>` 模板值类型，`FArrayProperty` 又能通过 `RegisterTypeFinder()` 回映回同一套 `FAngelscriptTypeUsage`。这意味着 **脚本签名、UE 反射属性、GC schema** 使用的是同一容器类型对象，而不是“脚本侧一个 wrapper，UE 侧另一个 helper”。
- `UnrealCSharp` 明确把 carrier 放在 helper 层。`Reference/UnrealCSharp/Source/UnrealCSharp/Public/Registry/FContainerRegistry.h:19-56` 与 `.../FContainerRegistry.inl:19-84` 建立的是 `FGarbageCollectionHandle <-> FArrayHelper/FMapHelper/FSetHelper <-> native address` 三元映射；`Reference/UnrealCSharp/Source/UnrealCSharp/Public/Reflection/Container/FArrayHelper.h:5-77` 又说明脚本真正拿到的是 helper 对象，不是裸 `FScriptArray`。这里不是“没有泛型容器”，而是 **容器 identity 归 helper/GC handle**。
- `UnLua` 的 owner 是 Lua userdata。`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ContainerRegistry.cpp:22-39,58-91` 在 Lua registry 里创建 `ScriptContainerMap` 弱值表，并把 `FScriptArray/FScriptSet/FScriptMap` 包装成 `FLuaArray/FLuaSet/FLuaMap`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Containers/LuaArray.h:57-76` 则把 `ITypeInterface` 和 `FScriptArray*` 绑在 wrapper 上。脚本表面能像操作容器一样操作，但 runtime authority 仍是 userdata wrapper。
- `puerts` 把“语法表面”和“运行时 carrier”拆成两层。`Reference/puerts/unreal/Puerts/Typing/ue/puerts.d.ts:36-78,116-118` 给 TypeScript 暴露 `TArray<T>/TSet<T>/TMap<TKey, TValue>` 泛型接口；但 `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/ContainerWrapper.cpp:14-32,35-58` 显示 V8 真正持有的是 wrapper object，其 internal fields 存放 `FScriptArray*` 与 `FPropertyTranslator*`。也就是说，**类型名是泛型，authority 仍是 wrapper**。
- `sluaunreal` 更显式地承认 wrapper 是 carrier。`Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaArray.h:25-50,101-122` 定义了 `LuaArray` 类，内部持有 `FProperty* inner` 与 `FScriptArray* array`；模板 `push(const TArray<T>&)` 只是帮它从 C++ 类型推导 `FProperty`。与当前 Angelscript 相比，这里属于“实现方式不同”，不是“少了容器支持”。

[1] 当前 Angelscript / Hazelight 把容器类型直接纳入脚本类型系统，而不是先包一层 helper：

```cpp
// ============================================================================
// [1] 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp
// 位置: 1345-1492, 1634-1695
// 说明: `TArray<T>` 是模板值类型；每个模板实例的操作缓存直接挂在 `TypeInfo::UserData`
// ============================================================================
AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_TArray(FAngelscriptBinds::EOrder::Early, []
{
	FBindFlags Flags;
	Flags.bTemplate = true;
	Flags.TemplateType = "<T>";

	auto TArray_ = FAngelscriptBinds::ValueClass<FScriptArray>("TArray<class T>", Flags);
	TArray_.TemplateCallback("bool f(int&in Type, int&out ErrorMessage)",
	[](asITypeInfo* TemplateType, asCString* ErrorMessage) -> bool
	{
		return ValidateArrayOperations(TemplateType, ErrorMessage) != nullptr;
	});

	auto ArrayType = MakeShared<FAngelscriptArrayType>();
	FAngelscriptType::RegisterTypeFinder([ArrayType](FProperty* Property, FAngelscriptTypeUsage& Usage) -> bool
	{
		FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property);
		if (ArrayProp == nullptr)
			return false;

		FAngelscriptTypeUsage InnerUsage = FAngelscriptTypeUsage::FromProperty(ArrayProp->Inner);
		Usage.Type = ArrayType;
		Usage.SubTypes.Add(InnerUsage);
		return true; // ★ `FArrayProperty` 会回到同一套脚本模板类型
	});
});

FArrayOperations* Ops = (FArrayOperations*)TemplateType->GetUserData();
if (Ops != nullptr)
{
	return Ops; // ★ 同一模板实例只分析一次元素类型
}

// ============================================================================
// [1] 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Binds\Bind_TArray.cpp
// 位置: 1477-1492
// 说明: Hazelight 仍是同一条血缘路径，`FArrayProperty -> TArray<T>` 的回映是同构的
// ============================================================================
auto ArrayType = MakeShared<FAngelscriptArrayType>();
FAngelscriptType::RegisterTypeFinder([ArrayType](FProperty* Property, FAngelscriptTypeUsage& Usage) -> bool
{
	FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property);
	if (ArrayProp == nullptr)
		return false;
	FAngelscriptTypeUsage InnerUsage = FAngelscriptTypeUsage::FromProperty(ArrayProp->Inner);
	Usage.Type = ArrayType;
	Usage.SubTypes.Add(InnerUsage);
	return true;
});
```

[2] `UnLua` 与 `puerts` 都把容器落到 wrapper 上，只是前者是 Lua userdata，后者是 V8 object：

```cpp
// ============================================================================
// [2] 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ContainerRegistry.cpp
// 位置: 22-39, 58-91
// 说明: UnLua 用弱表缓存 `FScriptArray/FScriptMap/FScriptSet -> FLua* wrapper`
// ============================================================================
FContainerRegistry::FContainerRegistry(FLuaEnv* Env)
    : Env(Env)
{
    const auto L = Env->GetMainState();
    lua_pushstring(L, "ScriptContainerMap");
    LowLevel::CreateWeakValueTable(L);
    lua_pushvalue(L, -1);
    MapRef = luaL_ref(L, -1);
    lua_rawset(L, LUA_REGISTRYINDEX); // ★ 同一 native 容器地址会复用 wrapper identity
}

void FContainerRegistry::FindOrAdd(lua_State* L, FScriptArray* ContainerPtr, TSharedPtr<ITypeInterface> ElementType)
{
    void* Userdata = CacheScriptContainer(L, ContainerPtr, FScriptContainerDesc::Array, [&](void* Cached)
    {
        const auto Array = (FLuaArray*)Cached;
        return Array->Inner == ElementType;
    });
    if (Userdata)
        new(Userdata) FLuaArray(ContainerPtr, ElementType, FLuaArray::OwnedByOther);
}

// ============================================================================
// [2] 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/ContainerWrapper.cpp
// 位置: 14-32, 43-57
// 说明: puerts 的 `TArray<T>` 语法背后是 JS wrapper object + translator
// ============================================================================
v8::Local<v8::FunctionTemplate> FScriptArrayWrapper::ToFunctionTemplate(v8::Isolate* Isolate)
{
    auto Result = v8::FunctionTemplate::New(Isolate, New);
    Result->InstanceTemplate()->SetInternalFieldCount(4); // 0 Ptr, 1 Property
    Result->PrototypeTemplate()->Set(FV8Utils::InternalString(Isolate, "Add"), v8::FunctionTemplate::New(Isolate, Add));
    Result->PrototypeTemplate()->Set(FV8Utils::InternalString(Isolate, "Get"), v8::FunctionTemplate::New(Isolate, Get));
    return Result;
}

auto Self = FV8Utils::GetPointerFast<FScriptArray>(Info.Holder(), 0);
auto Inner = FV8Utils::GetPointerFast<FPropertyTranslator>(Info.Holder(), 1);
Inner->JsToUE(Isolate, Context, Info[i], DataPtr, false); // ★ wrapper 持有 translator，再做元素桥接
```

[3] `UnrealCSharp` 与 `sluaunreal` 分别把 authority 放到 helper registry 与显式 wrapper class：

```cpp
// ============================================================================
// [3] 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Public/Registry/FContainerRegistry.inl
// 位置: 37-62
// 说明: helper 通过 GC handle 和 native address 建稳定映射
// ============================================================================
const auto GarbageCollectionHandle = FGarbageCollectionHandle::NewRef(InClass, InMonoObject, true);
(InRegistry->*Address2GarbageCollectionHandle).Add(InAddress, GarbageCollectionHandle);
(InRegistry->*GarbageCollectionHandle2Value).Add(GarbageCollectionHandle, InValue);

return FCSharpEnvironment::GetEnvironment().AddReference(
	InOwner,
	new TContainerReference<std::remove_pointer_t<typename FContainerValueMapping::ValueType>>(
		GarbageCollectionHandle)); // ★ 容器 identity 落在 helper + GC handle

// ============================================================================
// [3] 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaArray.h
// 位置: 25-50
// 说明: slua 把 `TArray<T>` 推导成 `LuaArray` wrapper，而不是直接暴露 `FScriptArray`
// ============================================================================
class SLUA_UNREAL_API LuaArray : public FGCObject {
public:
    template<typename T>
    static typename std::enable_if<DeduceType<T>::value != EPropertyClass::Struct, int>::type push(lua_State* L, const TArray<T>& v) {
        FProperty* prop = PropertyProto::createDeduceProperty<T>();
        auto array = reinterpret_cast<const FScriptArray*>(&v);
        return push(L, prop, const_cast<FScriptArray*>(array), false);
    }
private:
    FProperty* inner;
    FScriptArray* array; // ★ wrapper 才是 Lua 侧真正的 carrier
};
```

#### 对比矩阵（本轮新增观察点）

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 容器本体直接以脚本值类型模板承载 | Full | None | None | None | None | Full |
| 容器本体主要由 wrapper/helper object 承载 | None | Full | Full | Full | Full | None |
| 同一 native 容器地址有显式 identity cache / reuse | N/A | Full | Full | Partial | None | N/A |
| `FArrayProperty/FMapProperty/FSetProperty` 能直接回映到脚本容器类型 | Full | Partial | Partial | Partial | Partial | Full |

#### 小结与建议

- 当前 `Angelscript` 在这个观察点上的强项非常明确：**容器 authority 还在主类型系统里**。这让 `Bind`、`GC schema`、调试值和脚本签名能共用一套 `FAngelscriptTypeUsage`，优先级 `P0` 保持。
- 真正值得吸收的不是把 runtime API 改成 `wrapper-first`，而是吸收 `UnLua/UnrealCSharp` 的 **identity cache 思路**，用于调试器、编辑器预览或外部工具桥接层，优先级 `P1`。
- `puerts` 的经验说明“语法表面泛型化”与“runtime wrapper 化”可以共存。如果未来 `Angelscript` 需要给 IDE 或 Web tooling 导出容器 facade，可以单独做 facade artifact，不必动 runtime 容器 owner，优先级 `P2`。

### [D6] 默认参数 canonicalization：谁把 `CPP_Default_*` 变成目标语言可用字面量

#### 各插件实现概览

```
[D6-DefaultArg] Default Value Pipeline
HZ/AS : UFunction meta -> Helper_FunctionSignature -> type-level literal conversion -> public declaration
UC    : UFunction meta -> FClassGenerator -> C# signature/body code
UL    : ScriptGenerator/UHT -> DefaultParamCollection.inl -> runtime PreCall fill
PU    : ScriptGenerator/UHT -> InitParamDefaultMetas.inl -> FunctionTranslator import/parse
SL    : no in-tree default-param pipeline located in this round
```

本轮新增发现是：各插件都不是简单“读一下 `CPP_Default_*`”。真正差异在于 **默认参数什么时候被 canonicalize，最后落到哪种 artifact**。当前 Angelscript / Hazelight 把默认值直接变成脚本声明的一部分；UnLua / puerts 则先生成数据库，再在 runtime call bridge 缺参时回填；UnrealCSharp 介于两者之间，直接把默认值展开进 C# 代码文本。

#### 详细对比

##### 子维度 1：默认参数进入“公开签名”，还是只进入“运行时补值表”

- `当前 Angelscript` 与 `Hazelight` 都在 `Helper_FunctionSignature.h` 读取 `CPP_Default_*`，再交给 `FAngelscriptType::BuildFunctionDeclaration()` 把 UE 元数据翻成 AngelScript 字面量。`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h:204-233` 与 `.../Core/AngelscriptType.cpp:582-629` 说明默认值最终进入 `Signature.Declaration`。这意味着缺参能力属于脚本语言签名 contract，而不是运行时补丁。
- 这里还有一个本轮才钉实的分叉点：`当前 Angelscript` 把 world context 默认值写成 `__WorldContext()`（`Helper_FunctionSignature.h:222-231`），而 Hazelight 仍是 `__WorldContext`（`J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Binds\Helper_FunctionSignature.h:225-234`）。这不是“有没有实现”的差异，而是 **canonicalization 质量差异**，当前仓库已经把它提升成可直接调用的函数式默认值。
- `UnrealCSharp` 选择把 canonicalization 写进生成代码。`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp:480-535,945-979,1313-1395` 一方面把 `CPP_Default_*` 展开成 C# 签名里的 ` = Enum.Value / = 10`，另一方面又为结构体生成 `??= new FVector(...)` 这类 body code。默认值不是 runtime lookup，而是 **生成出的 C# 源码本身**。
- `UnLua` 的 default pipeline 完全不同。`Reference/UnLua/Plugins/UnLua/Source/UnLuaDefaultParamCollector/Private/UnLuaDefaultParamCollector.cpp:22-44,134-181,409-445` 与 `.../UnLuaDefaultParamCollectorUbtPlugin.cs:119-176,397-421` 会把默认值预编成 `DefaultParamCollection.inl`；runtime 再通过 `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/ClassDesc.cpp:38-42` 绑定 `GDefaultParamCollection`，最终在 `FunctionDesc.cpp:321-332` 的 `PreCall()` 缺参分支中 `CopyValue()`。因此默认值并不进入 Lua 函数签名文本，而是 **运行时补值数据库**。
- `puerts` 与 `UnLua` 同类，但解析责任更偏 translator。`Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/Private/ParamDefaultValueMetasModule.cpp:74-103,119-151` 生成 `InitParamDefaultMetas.inl`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp:14-31,42-52,143-205` 再把默认值喂给 `FDefaultValueHelper::ParseVector/ParseRotator` 或 `ImportText_Direct`。这意味着 TS/JS 公开 API 和 runtime 默认值表不是一个 authority。
- `sluaunreal` 本轮对 `Reference/sluaunreal/Plugins/slua_unreal` 以 `CPP_Default_`、`DefaultParam`、`InitParamDefault`、`DefaultValueHelper` 做仓级检索未命中。这里应判为 **当前源码范围未定位到对等 pipeline**，而不是武断写成“确认没有默认参数支持”。

[1] 当前 Angelscript / Hazelight 都把默认参数前移到签名构建阶段，但当前仓库的 world context canonicalization 更完整：

```cpp
// ============================================================================
// [1] 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h
// 位置: 204-233
// 说明: 从 `CPP_Default_*` 读取 UE metadata，并把 world context 改写成可调用默认值
// ============================================================================
FString DefaultMeta = TEXT("CPP_Default_");
DefaultMeta += Property->GetName();

FName MetaName = *DefaultMeta;
if (Function->HasMetaData(MetaName))
{
	FString MetaStr = Function->GetMetaData(MetaName);
	if (MetaStr == TEXT("None"))
		MetaStr = TEXT("");
	ArgumentDefaults.Add(MetaStr);
}

const FString& WorldContextParam = Function->GetMetaData(NAME_Signature_WorldContext);
if (WorldContextParam.Len() != 0)
{
	for (int32 ArgIndex = 0, ArgCount = ArgumentTypes.Num(); ArgIndex < ArgCount; ++ArgIndex)
	{
		if (ArgumentNames[ArgIndex] == WorldContextParam)
		{
			ArgumentDefaults[ArgIndex] = TEXT("__WorldContext()"); // ★ 当前仓库已提升为可直接调用的默认值
			WorldContextArgument = ArgIndex;
			break;
		}
	}
}

// ============================================================================
// [1] 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp
// 位置: 582-629
// 说明: 默认值会被翻译成 AngelScript 字面量，直接进 public declaration
// ============================================================================
if (ArgumentDefaults.IsValidIndex(i) && ArgumentDefaults[i] != TEXT("-"))
{
	if (ArgumentTypes[i].DefaultValue_UnrealToAngelscript(ArgumentDefaults[i], AngelscriptDefaultValue))
		bValidDefault = true;
}
else
{
	if (ArgumentTypes[i].DefaultValue_AngelscriptFallback(AngelscriptDefaultValue))
		bValidDefault = true;
}

if (i > LastArgumentWithoutDefault && AngelscriptDefaultValues[i].Len() > 0)
{
	Declaration += TEXT(" = ");
	Declaration += AngelscriptDefaultValues[i]; // ★ 默认值进入脚本签名文本
}

// ============================================================================
// [1] 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Binds\Helper_FunctionSignature.h
// 位置: 225-234
// 说明: Hazelight 仍把 world context 记为原始 token，而不是函数式默认值
// ============================================================================
if (ArgumentNames[ArgIndex] == WorldContextParam)
{
	ArgumentDefaults[ArgIndex] = TEXT("__WorldContext");
	WorldContextArgument = ArgIndex;
	break;
}
```

[2] `UnLua` 与 `puerts` 都把默认值先沉成 build/UHT 产物，再在 runtime call bridge 缺参时补齐：

```cpp
// ============================================================================
// [2] 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaDefaultParamCollector/Private/UnLuaDefaultParamCollector.cpp
// 位置: 22-41, 141-150, 167-180
// 说明: UnLua 先把默认值编进 `DefaultParamCollection.inl`
// ============================================================================
static bool FindDefaultValueString(const TMap<FName, FString>* MetaMap, const FProperty* Param, FString& OutString)
{
    const FString* DefaultValue = MetaMap->Find(*Param->GetName());
    if (DefaultValue)
    {
        OutString = *DefaultValue; // ★ Blueprint metadata 优先
        return true;
    }

    const FName CppKey(*(FString(TEXT("CPP_Default_")) + Param->GetName()));
    const FString* CppDefaultValue = MetaMap->Find(CppKey);
    if (CppDefaultValue)
    {
        OutString = *CppDefaultValue;
        return true;
    }
    return false;
}

if (!FindDefaultValueString(MetaMap, Property, ValueStr))
{
    if (AutoEmitParameterNames.Find(Property->GetName()) == INDEX_NONE)
        continue;
}
GeneratedFileContent += FString::Printf(TEXT("PC->Parameters.Add(TEXT(\"%s\"), new FVectorParamValue(FVector(%ff,%ff,%ff)));\r\n"),
                                        *Property->GetName(), X, Y, Z);

// ============================================================================
// [2] 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp
// 位置: 321-332
// 说明: 运行时只有在 Lua 少传参数时才从默认值表回填
// ============================================================================
else if (!Property->IsOutParameter())
{
    if (DefaultParams)
    {
        IParamValue **DefaultValue = DefaultParams->Parameters.Find(Property->GetProperty()->GetFName());
        if (DefaultValue)
        {
            const void *ValuePtr = (*DefaultValue)->GetValue();
            Property->CopyValue(Params, ValuePtr); // ★ 默认值 authority 在 runtime param buffer
            CleanupFlags[i] = true;
        }
    }
}

// ============================================================================
// [2] 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp
// 位置: 14-31, 143-205
// 说明: puerts 也先读 inl 表，再在 translator 层解析复杂字面量
// ============================================================================
static TMap<FName, TMap<FName, TMap<FName, FString>>> ParamDefaultMetas;
static void ParamDefaultMetasInit()
{
#include "InitParamDefaultMetas.inl"
}

TMap<FName, FString>* MetaMap = GetParamDefaultMetaFor(InFunction);
if (MetaMap)
{
    FString* DefaultValuePtr = MetaMap->Find(Property->GetFName());
    if (DefaultValuePtr && !DefaultValuePtr->IsEmpty())
    {
        if (const StructPropertyMacro* StructProp = CastFieldMacro<StructPropertyMacro>(Property))
        {
            if (StructProp->Struct == TBaseStructure<FVector>::Get())
            {
                FVector* Vector = (FVector*) PropValuePtr;
                FDefaultValueHelper::ParseVector(**DefaultValuePtr, *Vector);
                continue;
            }
        }

        Property->ImportText_Direct(**DefaultValuePtr, PropValuePtr, nullptr, PPF_None);
    }
}
```

[3] `UnrealCSharp` 直接把默认值展开成 C# 代码，而不是在 runtime lookup 表里延迟解释：

```cpp
// ============================================================================
// [3] 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp
// 位置: 480-535, 945-979, 1313-1379
// 说明: 默认参数一部分进 C# 方法签名，一部分进生成 body
// ============================================================================
if (bGeneratorFunctionDefaultParam == false)
{
	bGeneratorFunctionDefaultParam = HasFunctionDefaultParam(Function, FunctionParams[Index]);
}

FunctionDeclarationBody += FString::Printf(TEXT(
	"%s %s%s%s"),
	*FGeneratorCore::GetPropertyType(FunctionParams[Index]),
	*FUnrealCSharpFunctionLibrary::Encode(FunctionParams[Index]),
	bGeneratorFunctionDefaultParam ? *GetFunctionDefaultParam(Function, FunctionParams[Index]) : TEXT(""),
	Index == FunctionParams.Num() - 1 ? TEXT("") : TEXT(", "));

const auto Key = FString::Printf(TEXT("CPP_Default_%s"), *InProperty->GetName());
const auto MetaData = InFunction->GetMetaData(*Key);
return FString::Printf(TEXT(" = %s.%s"), *EnumName, *MetaData); // ★ 直接进 C# 签名文本

if (CastField<FNameProperty>(InProperty))
{
	return FString::Printf(TEXT("\t\t\t\t%s ??= new FName(\"%s\");\n\n"),
	                       *FUnrealCSharpFunctionLibrary::Encode(InProperty),
	                       *InMetaData);
}
if (StructProperty->Struct == TBaseStructure<FVector>::Get())
{
	return FString::Printf(TEXT("\t\t\t\t%s ??= new FVector(%lf, %lf, %lf);\n\n"),
	                       *FUnrealCSharpFunctionLibrary::Encode(InProperty),
	                       TCString<TCHAR>::Atod(*Value[0]),
	                       TCString<TCHAR>::Atod(*Value[1]),
	                       TCString<TCHAR>::Atod(*Value[2]));
}
```

#### 对比矩阵（本轮新增观察点）

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 默认参数直接进入公开脚本/语言签名文本 | Full | Full | None | None | None | Full |
| build/UHT 阶段生成默认值数据库或 inl 产物 | None | Partial | Full | Full | None | None |
| 缺参时由 runtime bridge 自动回填参数 buffer | None | None | Full | Full | None | None |
| `FVector/FRotator/FLinearColor/...` 有专门解析路径 | Full | Full | Full | Full | None | Full |

#### 小结与建议

- 当前 `Angelscript` 在这个观察点上的核心优势，是 **默认值直接进入脚本声明**。这比 `UnLua/puerts` 的“运行时补值表”更适合作为语言 contract，优先级 `P0` 保持。
- 真正值得吸收的是 `UnLua/puerts` 的 **build-time 默认值资产化**。当前 `Angelscript` 已有类型级 literal conversion，但 IDE、docs、debug metadata 仍缺统一默认值 manifest；建议新增可选导出资产，而不是把默认值 owner 下放到 runtime bridge，优先级 `P1`。
- `__WorldContext()` 相比 Hazelight 的 `__WorldContext` 已经是更好的 canonicalization，不应回退。后续如果继续补默认参数外部产物，应沿用当前仓库这套“最终可调用字面量”语义，优先级 `P0`。

### [D8] 容器桥接的快路径：缓存、scratch buffer 与 copy 成本由谁承担

#### 各插件实现概览

```
[D8-ContainerCost] Cost Owner
HZ/AS : template-type op cache + POD memcpy / compare-function cache
UC    : helper object + FPropertyDescriptor reuse + GC-handle registry
UL    : wrapper object + single-element scratch buffer + weak container cache
PU    : wrapper internal field + FPropertyTranslator + stack scratch
SL    : ref/copy split + POD/NoDestructor branch + explicit clone warning
```

`D8` 这一轮不再泛谈“谁更快”。更具体的新发现是：**容器桥接的成本由谁在什么时候支付**。当前 Angelscript / Hazelight 把成本前移到模板实例缓存；UnrealCSharp 把成本摊到长生命周期 helper；UnLua / puerts 则用 scratch buffer 和 translator 减少每次操作的临时分配；slua 明确承认“非引用包装会很贵”，并靠属性标志减损。

#### 详细对比

##### 子维度 1：快路径是“模板缓存”，还是“wrapper 局部优化”

- `当前 Angelscript` 与 `Hazelight` 把容器优化做在模板实例级别。`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp:136-145,1634-1695` 与 `J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Binds\Bind_TArray.cpp:136-145,1634-1695` 先把 `FArrayOperations` 缓存在 `TemplateType->UserData`，再根据 `Type.NeedCopy()` 判定能否直接 `FMemory::Memcpy`。这是典型的 **一次类型分析，多次调用复用**。
- `UnrealCSharp` 的成本 owner 在 helper 层。`Reference/UnrealCSharp/Source/UnrealCSharp/Public/Registry/FContainerRegistry.inl:37-62` 让 helper 与 GC handle 长生命周期绑定；`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Container/FArrayHelper.cpp:11-21,208-227` 又把 `FPropertyDescriptor` 持有在 helper 内部，删除元素时还会针对 `CPF_IsPlainOldData | CPF_NoDestructor` 走跳过析构的快路径。这里不是“无缓存”，而是 **缓存粒度从模板实例变成 helper object**。
- `UnLua` 的优化非常务实。`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Containers/LuaArray.h:57-63` 在 `FLuaArray` 构造时就分配一个 `ElementCache`，`LuaLib_Array.cpp:144-149,165-168,186-191` 在 `AddUnique / Find / Insert` 时重复使用这一块 scratch buffer。再叠加 `ContainerRegistry.cpp:27-31,58-91` 的弱表缓存，说明它把热点放在 **减少 Lua/C++ 边界上的临时对象 churn**。
- `puerts` 的容器快路径集中在 wrapper object 本身。`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/ContainerWrapper.cpp:18-30,43-57,247-263` 把 `FPropertyTranslator*` 存进 V8 internal field，并在查找时用 `FMemory_Alloca` 申请栈上临时值。这里的好处是无额外 heap alloc；代价是每个元素仍要穿过 `JsToUE/UEToJs` translator。
- `sluaunreal` 的源码反而最直白。`Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaArray.cpp:32-40,42-61` 直接写着 “deep-copy construct FScriptArray, it's very expensive”；同时 `...:226-251` 又在 `constructItems()/destructItems()` 中对 `CPF_ZeroConstructor`、`CPF_NoDestructor` 做分支。也就是说，slua 的策略不是把深拷贝藏起来，而是 **显式区分 ref 与 copy 的成本模型**。

[1] 当前 Angelscript / Hazelight 的容器快路径是“模板实例缓存 + POD memcpy”：

```cpp
// ============================================================================
// [1] 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp
// 位置: 136-145, 1634-1695
// 说明: 同一模板实例缓存 `FArrayOperations`，POD 元素可直接 memcpy
// ============================================================================
if (!SubType.NeedCopy())
{
	// ★ 完全 POD，直接走整块内存复制
	if (SourceNum > DestNum)
		DestinationArray.Add(SourceNum - DestNum, ElementSize, ElementAlignment);
	else if(DestNum > SourceNum)
		DestinationArray.Remove(SourceNum, DestNum - SourceNum, ElementSize, ElementAlignment);
	FMemory::Memcpy(DestinationArray.GetData(), SourceArray.GetData(), SourceNum * ElementSize);
	return;
}

FArrayOperations* Ops = (FArrayOperations*)TemplateType->GetUserData();
if (Ops != nullptr)
{
	return Ops; // ★ 模板实例缓存命中后，不再重复分析子类型
}

Ops->NumBytesPerElement = Type.GetValueSize();
Ops->Alignment = Type.GetValueAlignment();
Ops->bNeedConstruct = Type.NeedConstruct();
Ops->bNeedDestruct = Type.NeedDestruct();
Ops->bNeedCopy = Type.NeedCopy();
Ops->bCanCompare = Type.CanCompare();

// ============================================================================
// [1] 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Binds\Bind_TArray.cpp
// 位置: 1634-1695
// 说明: Hazelight 在这一层仍是同构实现，说明这是该技术谱系的原始快路径
// ============================================================================
FArrayOperations* Ops = (FArrayOperations*)TemplateType->GetUserData();
if (Ops != nullptr)
{
	return Ops;
}
```

[2] `UnrealCSharp`、`UnLua`、`puerts`、`sluaunreal` 都在 wrapper/helper 层做局部快路径，只是粒度不同：

```cpp
// ============================================================================
// [2] 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Container/FArrayHelper.cpp
// 位置: 11-21, 208-227
// 说明: helper 持有 `FPropertyDescriptor`，删除元素时按属性标志决定是否析构
// ============================================================================
InnerPropertyDescriptor = FPropertyDescriptor::Factory(InProperty);
if (InData != nullptr)
{
	ScriptArray = static_cast<FScriptArray*>(InData);
}
else
{
	ScriptArray = new FScriptArray();
}

if (!(InnerPropertyDescriptor->GetPropertyFlags() & (CPF_IsPlainOldData | CPF_NoDestructor)))
{
	for (auto Index = 0; Index < InCount; ++Index, Dest += InnerPropertyDescriptor->GetElementSize())
	{
		InnerPropertyDescriptor->DestroyValue(Dest);
	}
}
ScriptArray->Remove(InIndex, InCount, InnerPropertyDescriptor->GetElementSize(), __STDCPP_DEFAULT_NEW_ALIGNMENT__);

// ============================================================================
// [2] 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Containers/LuaArray.h
// 位置: 57-63
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/BaseLib/LuaLib_Array.cpp
// 位置: 144-149, 165-168
// 说明: UnLua 为单元素操作常驻一块 scratch buffer，避免频繁分配
// ============================================================================
FLuaArray(const FScriptArray* InScriptArray, TSharedPtr<UnLua::ITypeInterface> InInnerInterface, EScriptArrayFlag Flag = OwnedByOther)
    : ScriptArray((FScriptArray*)InScriptArray), Inner(InInnerInterface), ElementCache(nullptr), ElementSize(Inner->GetSize()), ScriptArrayFlag(Flag)
{
    ElementCache = FMemory::Malloc(ElementSize, Inner->GetAlignment()); // ★ 一次分配，多次复用
}

Array->Inner->Initialize(Array->ElementCache);
Array->Inner->WriteValue_InContainer(L, Array->ElementCache, 2);
int32 Index = Array->AddUnique(Array->ElementCache);
Array->Inner->Destruct(Array->ElementCache);

// ============================================================================
// [2] 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/ContainerWrapper.cpp
// 位置: 43-57, 247-263
// 说明: translator 常驻 wrapper internal field，临时比较值走栈上 scratch
// ============================================================================
auto Self = FV8Utils::GetPointerFast<FScriptArray>(Info.Holder(), 0);
auto Inner = FV8Utils::GetPointerFast<FPropertyTranslator>(Info.Holder(), 1);
Inner->Property->InitializeValue(DataPtr);
Inner->JsToUE(Isolate, Context, Info[i], DataPtr, false);

void* Dest = FMemory_Alloca(GetSizeWithAlignment(Property));
Property->InitializeValue(Dest);
Inner->JsToUE(Isolate, Context, Info[0], Dest, false); // ★ 栈上临时值，避免额外 heap alloc

// ============================================================================
// [2] 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaArray.cpp
// 位置: 32-40, 42-61, 226-251
// 说明: slua 明确承认非引用包装需要深拷贝，同时对 POD / NoDestructor 走快路径
// ============================================================================
// blueprint stack will destroy the TArray
// so deep-copy construct FScriptArray
// it's very expensive
arrayP->CopyCompleteValue(destArray, srcArray);

if (!(inner->PropertyFlags & (CPF_IsPlainOldData | CPF_NoDestructor)))
{
	for (int32 i = 0 ; i < count; i++, Dest += inner->ElementSize)
	{
		inner->DestroyValue(Dest);
	}
}

if (inner->PropertyFlags & CPF_ZeroConstructor)
{
	FMemory::Memzero(Dest, count * inner->ElementSize); // ★ 零构造元素直接清零
}
```

#### 对比矩阵（本轮新增观察点）

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 类型/translator/helper 会缓存到长生命周期对象 | Full | Full | Full | Full | Partial | Full |
| 对 `POD / NoDestructor / ZeroConstructor` 有显式快路径 | Full | Full | Partial | None | Full | Full |
| 单元素桥接使用 scratch buffer / stack temp 降低分配 | None | None | Full | Full | None | None |
| 非引用包装会显式触发深拷贝 | None | None | None | None | Full | None |

#### 小结与建议

- 当前 `Angelscript` 在这个观察点上的正确方向是 **继续把容器热路径前移到模板实例缓存**。这条线比 wrapper-first 路线更适合它现有的强类型 runtime，优先级 `P0` 保持。
- 真正值得吸收的是 `UnLua` 的 **single-element scratch buffer** 思路，尤其适合将来给 debugger value builder、编辑器预览或 reflective fallback 的容器访问做减 allocation 优化，优先级 `P1`。
- `UnrealCSharp` 与 `sluaunreal` 提醒了另一面：编辑器/工具桥接可以接受 helper/wrapper 层，只要它自己承担生命周期和快路径责任。当前 `Angelscript` 如果后续为外部工具补容器 facade，更应该把它限制在工具层，而不是改写 runtime 主路径，优先级 `P2`。

---
## 深化分析 (2026-04-09 01:47:53)

### [D7] tooling surface 的 shared semantic core：GUI / listener / commandlet 是否共用同一语义内核

#### 各插件实现概览

```
[D7-SharedCore] Tool Surface -> Semantic Core
AS(now) : menu/reload + commandlet -> BlueprintImpact::Analyze*/Scan*   // 同一蓝图影响分析内核
HZ      : menu + roots/test commandlets -> parallel entry points        // 未见同等级共享 analyzer
UC      : toolbar + console + listener/cook -> Generator() + FCodeAnalysis
UL      : toolbar + commandlet -> IntelliSenseGenerator.UpdateAll / Export*
PU      : menu + console -> GenUeDts() -> FTypeScriptDeclarationGenerator
SL      : profiler tab + external lua-wrapper -> split toolchains
```

前文 `D7` 已经比较过谁拥有 editor surface、谁拥有 headless surface。本轮不再重复“入口数量”，而是补一个更深的判断：**这些入口背后是不是同一个 semantic core 在工作**。这会直接决定 GUI、listener、commandlet 三条路径的行为是否一致，也决定后续修一处是否真的能同时修三处。

#### 详细对比

##### 子维度 1：tool surface 背后是否真的只有一套语义内核

- 当前 `Angelscript` 在这个角度上已经明显强于 Hazelight。`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:108-145` 的交互式 reload 路径逐个 Blueprint 调 `AngelscriptEditor::BlueprintImpact::AnalyzeLoadedBlueprint(...)`；`Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp:63-85` 的 commandlet 路径则调 `ScanBlueprintAssets(...)`；而 `Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp:278-305` 又证明 `ScanBlueprintAssets()` 内部最终还是回到同一个 `AnalyzeLoadedBlueprint(...)`。这不是“都有工具”，而是 **GUI 和 headless 共用同一个 Blueprint impact 语义内核**，应判为 `Full`。
- `UnrealCSharp` 同样是 `Full`，只是 shared core 的主题不是 impact analysis，而是 codegen pipeline。`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:68-100` 的 console command、`...:237-299` 的 `Generator()`、`...:115-123` 的 cook hook，以及 `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:137-142` 的 editor listener，最终都会回到 `FCodeAnalysis::CodeAnalysis()` 与同一组 generator。它的差异在于“入口很多，但 owner 是一条统一生成流水线”。
- `UnLua` 是 `Partial`。`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:34-37,123-126` 的 toolbar 直接调 `FUnLuaIntelliSenseGenerator::Get()->UpdateAll()`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp:101-112` 在 `BP=1` 时也会回到同一个 `UpdateAll()`。但同一个 commandlet 前半段 `:55-99` 又自己枚举 `ExportedReflectedClasses / ExportedEnums / ExportedFunctions` 并逐个 `GenerateIntelliSense()`。也就是说，Blueprint authoring 这一半共享 core，静态导出这一半仍有 duplicated orchestration。
- `puerts` 也是 `Partial`。`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1634-1687` 证明菜单按钮和 `Puerts.Gen` console command 都会回到 `GenUeDts()`；`...:1615-1632,1700-1705` 又证明 `GenUeDts()` 最终调用同一个 `FTypeScriptDeclarationGenerator::GenTypeScriptDeclaration(...)`。但当前快照里没有对等 commandlet，因此这是 **editor surfaces 共享 core**，不是 **GUI / headless / cook 全面共享**。
- `Hazelight` 的结论要写得更精确一些：不是“没有工具”，而是 **没有证据表明 editor surface 与 non-UI surface 共用同一个深语义分析器**。`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:434,711-718` 负责菜单注册，`.../AngelscriptAllScriptRootsCommandlet.cpp:5-21` 与 `.../AngelscriptTestCommandlet.cpp:5-25` 则分别负责根路径枚举和测试批执行；它们是并列 operator entry，而不是当前 `Angelscript BlueprintImpact` 这种“上层不同、底层同核”的结构。这里应归类为 **实现方式不同**，不是 **没有实现 editor/headless tooling**。
- `sluaunreal` 基本应判 `None`。插件内更显著的是 `Reference/sluaunreal/Plugins/slua_unreal/Source/slua_profile/Private/slua_profile.cpp:70-83,127-143` 的 profiler tab；静态导出说明则在 `Reference/sluaunreal/Tools/README.md:1-37`，已经是插件外 `Tools/` 目录的另一条链。也就是说，**工具 surface 与语义核心没有在插件内收束成一套共享内核**。

[1] 当前 `Angelscript` 的 GUI reload 与 commandlet 不是两份逻辑，而是共享同一个 `BlueprintImpact` 语义核：

```cpp
// ============================================================================
// [1] 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp
// 位置: 108-145
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp
// 位置: 278-305
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp
// 位置: 63-85
// 说明: 交互式 reload 与 headless commandlet 最终都回到同一组 BlueprintImpact API
// ============================================================================
for (TObjectIterator<UBlueprint> BlueprintIt; BlueprintIt; ++BlueprintIt)
{
	UBlueprint* BP = *BlueprintIt;
	TArray<AngelscriptEditor::BlueprintImpact::EBlueprintImpactReason> ImpactReasons;
	const bool bHasDependency = AngelscriptEditor::BlueprintImpact::AnalyzeLoadedBlueprint(*BP, ImpactSymbols, ImpactReasons);
	// ★ reload helper 直接用 AnalyzeLoadedBlueprint 判定受影响蓝图
}

FBlueprintImpactScanResult ScanBlueprintAssets(const FAngelscriptEngine& Engine, IAssetRegistry& AssetRegistry, const FBlueprintImpactRequest& Request)
{
	Result.Symbols = BuildImpactSymbols(Result.MatchingModules);
	Result.CandidateAssets = FindBlueprintAssets(AssetRegistry, Request.bIncludeOnlyOnDiskAssets);
	...
	if (AnalyzeLoadedBlueprint(*Blueprint, Result.Symbols, Match.Reasons))
	{
		Result.Matches.Add(Match); // ★ commandlet path 内部仍然回到同一个 AnalyzeLoadedBlueprint
	}
}

const AngelscriptEditor::BlueprintImpact::FBlueprintImpactScanResult ScanResult =
	AngelscriptEditor::BlueprintImpact::ScanBlueprintAssets(
		FAngelscriptEngine::Get(),
		AssetRegistryModule.Get(),
		Request);
// ★ headless 入口只是换了 orchestration，语义核心没有分叉
```

[2] `UnrealCSharp` 的 shared core 是统一 codegen pipeline，而不是散落的按钮回调：

```cpp
// ============================================================================
// [2] 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp
// 位置: 68-100, 237-299
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp
// 位置: 137-142
// 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FCodeAnalysis.cpp
// 位置: 5-10, 54-89
// 说明: console / toolbar / listener / cook hook 最终共用同一 analysis + generation 流水线
// ============================================================================
CodeAnalysisConsoleCommand = MakeUnique<FAutoConsoleCommand>(
	TEXT("UnrealCSharp.Editor.CodeAnalysis"), TEXT(""),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		FCodeAnalysis::CodeAnalysis();
	}));

GeneratorConsoleCommand = MakeUnique<FAutoConsoleCommand>(
	TEXT("UnrealCSharp.Editor.Generator"), TEXT(""),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		Generator();
	}));

void FUnrealCSharpEditorModule::Generator()
{
	FSolutionGenerator::Generator();
	FCodeAnalysis::CodeAnalysis();
	FDynamicGenerator::CodeAnalysisGenerator();
	...
	FBindingEnumGenerator::Generator();
}

void FEditorListener::OnPostEngineInit()
{
	FCodeAnalysis::CodeAnalysis();
	FDynamicGenerator::CodeAnalysisGenerator();
	// ★ editor listener 没有重写第二套分析器，而是直接复用同一 core
}

void FCodeAnalysis::CodeAnalysis()
{
	Compile();
	Analysis(); // ★ 统一语义核心收敛在 FCodeAnalysis
}
```

[3] `UnLua` 与 `puerts` 都有 shared core，但共享面各自只覆盖了一部分 tooling surface：

```cpp
// ============================================================================
// [3] 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp
// 位置: 34-37, 123-126
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp
// 位置: 101-112
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp
// 位置: 58-68, 143-173
// 说明: UnLua 的 toolbar 与 commandlet 在 Blueprint IntelliSense 这一支共享 generator
// ============================================================================
CommandList->MapAction(FUnLuaEditorCommands::Get().GenerateIntelliSense, FExecuteAction::CreateLambda([]
{
	FUnLuaIntelliSenseGenerator::Get()->UpdateAll();
}), FCanExecuteAction());

MenuBuilder.AddMenuEntry(Commands.GenerateIntelliSense, NAME_None, LOCTEXT("GenerateIntelliSense", "Generate IntelliSense"));

if (ParamsMap.Contains(BPKey) && ParamsMap[BPKey] == TEXT("1"))
{
	auto Generator = FUnLuaIntelliSenseGenerator::Get();
	Generator->Initialize();
	Generator->UpdateAll(); // ★ commandlet 只在 Blueprint 分支回到同一个 generator
}

void FUnLuaIntelliSenseGenerator::UpdateAll()
{
	const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	// ★ 共享内核 owner 在 generator，不在菜单回调里
}

void FUnLuaIntelliSenseGenerator::Export(const UField* Field)
{
	const FString Content = UnLua::IntelliSense::Get(Field);
	SaveFile(ModuleName, FileName, Content);
}

// ============================================================================
// [3] 文件: Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp
// 位置: 1615-1632, 1634-1687, 1700-1705
// 说明: puerts 的菜单按钮和 console command 共用同一个 GenUeDts/GenTypeScriptDeclaration 核心
// ============================================================================
void GenUeDts(bool InGenFull, FName InSearchPath)
{
	GenTypeScriptDeclaration(InGenFull, InSearchPath);
	GenTypeScriptCppDeclaration();
}

void GenUeDtsCallback()
{
	GenUeDts(false, NAME_None);
}

ConsoleCommand = MakeUnique<FAutoConsoleCommand>(TEXT("Puerts.Gen"), TEXT("Execute GenDTS action"),
	FConsoleCommandWithArgsDelegate::CreateLambda([this](const TArray<FString>& Args)
	{
		...
		this->GenUeDts(GenFull, SearchPath); // ★ 菜单和 console 都回到同一个 GenUeDts
	}));

void GenTypeScriptDeclaration(bool InGenFull, FName InSearchPath) override
{
	FTypeScriptDeclarationGenerator TypeScriptDeclarationGenerator;
	TypeScriptDeclarationGenerator.RestoreBlueprintTypeDeclInfos(InGenFull);
	TypeScriptDeclarationGenerator.LoadAllWidgetBlueprint(InSearchPath, InGenFull);
	TypeScriptDeclarationGenerator.GenTypeScriptDeclaration(true, true);
}
```

#### 对比矩阵（本轮新增观察点）

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| GUI surface 与 non-UI surface 明确共用同一 public semantic core | None | Full | Partial | Partial | None | Full |
| listener / cook hook 也能复用同一 core，而不是旁路复制 | None | Full | None | None | None | Full |
| shared core 位于插件内主模块，而不是外部 `Tools/` 工具链 | Full | Full | Full | Full | None | Full |
| commandlet / console 只是换 orchestration，不换语义 owner | Partial | Full | Partial | Full | None | Full |

#### 小结与建议

- 当前 `Angelscript` 在这个观察点上的真正优势，不是“工具多”，而是 **`BlueprintImpact` 已经形成共享语义内核**。这条线建议按 `P0` 保持，后续 editor / commandlet / CI 相关分析器都应优先沿用这种结构。
- 最值得吸收的是 `UnrealCSharp` 的 **统一 orchestration pipeline**。当前 AS 已经在 BlueprintImpact 上做到这一点，但 dump、docs、bind summary 等工具仍可继续往“公共内核 + 多 surface”收敛，优先级 `P1`。
- `UnLua` 和 `puerts` 提醒了一个中间态风险：表面上多入口共存，但只共享一半核心，另一半仍靠 duplicated orchestration。当前 AS 若继续扩工具，应该尽量避免落入这种 `Partial shared core` 状态，优先级 `P1`。
- `sluaunreal` 的 external-tool split 不适合作为当前 AS 主线。它适合补充型工具，不适合承载核心 authoring contract，优先级 `P2`。

### [D9] 测试结果的 artifact owner：只是“有测试”，还是“结果可被脚本直接消费”

#### 各插件实现概览

```
[D9-ArtifactOwner] Test Execution -> Machine Artifact
AS(now) : Automation JSON -> Summary.json + RunMetadata.json + coverage HTML/JSON
HZ      : commandlet exit code + coverage HTML/JSON
UL      : UE Automation cases + benchmark CSV
UC      : no peer test module / summary artifact located in current snapshot
PU      : testing hooks + wasm sample binding; no repo-owned regression summary
SL      : profiler .sluastat binary with versioning, not regression artifact
```

前文 `D9` 已经分析过 runner owner 和 operator contract。本轮只看一个更硬的问题：**测试结果最终是谁来拥有**。是 UE 日志里飘几行字，还是仓库明确生成 `Summary.json / coverage.json / benchmark.csv / profiler binary` 这类机器可消费 artifact。

#### 详细对比

##### 子维度 1：pass/fail 结果是否被收束成结构化 artifact

- 当前 `Angelscript` 在这条线上最完整。`Tools/GetAutomationReportSummary.ps1:73-80,168-238,289-356` 会优先寻找 `AutomationReport.json / report.json / index.json` 等 JSON 报告，再统一规约成 `BucketName / ExitCode / Total / Passed / Failed / FailedTests / SummarySource`，最终写出 `Summary.json`。`Tools/RunAutomationTests.ps1:213-239` 与 `Tools/RunTests.ps1:201-298` 进一步根据这个结构化结果 **提升最终退出码**。这说明 artifact owner 已经从“UE 自动化原始输出”上提到了 repo runner 本身，判定应为 `Full`。
- `Hazelight` 有测试结果，但 owner 更分散。`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/AngelscriptTestCommandlet.cpp:5-25` 只定义 `0/1/2/3` 退出码；`.../CodeCoverage/AngelscriptCodeCoverage.cpp:21-40,58-64,181-198` 与 `.../CodeCoverage/CoverageReportGenerator.h:39-62` 又把 coverage 写成 `index.html` 与 top-level JSON。也就是说，它拥有 **coverage artifact**，但没有当前 AS 这种 repo-owned `Summary.json / RunMetadata.json` 汇总层，应判为 `Partial`。
- `UnLua` 的回归能力很强，但 artifact owner 更偏测试工程本身。`Reference/UnLua/TPSProject.uproject:16-33` 开启 `RuntimeTests / EditorTests / FunctionalTestingEditor / UnLuaTestSuite`；`Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Tests/IssueOverridesTest.cpp:25-58` 这样的 case 直接通过 UE Automation 报告结果；此外 `Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Perfs/UnLuaBenchmarkFunctionLibrary.cpp:46-50` 还能写出 `Benchmark/*.csv`。但这仍是 **UE Automation + benchmark CSV**，不是 repo-owned 统一 regression summary，因此应判为 `Partial`。
- `UnrealCSharp` 在当前快照里没有定位到对等 artifact owner。`Reference/UnrealCSharp/UnrealCSharp.uplugin:18-54` 的模块列表只有 `Runtime / Editor / ScriptCodeGenerator / Compiler / UnrealCSharpCore / CrossVersion / SourceCodeGenerator`，没有 test module；本轮也没有定位到 `Summary.json`、coverage exporter 或 test runner。这里应判为 `None`，并明确是“当前源码快照未定位到对等测试 artifact contract”。
- `puerts` 只能判 `Partial`。`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnv.cpp:42-49` 与 `.../JsEnvImpl.cpp:1170-1187` 提供了 `Request*GarbageCollectionForTesting()` 这类 test hook；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PuertsWasm/WasmTestForStaticBinding.cpp:21-28` 也有极小的 static binding test sample。但这些都还停在“便于测试”的代码层，没有上升到仓库自有 summary artifact。
- `sluaunreal` 需要避免误判。`Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/SluaProfilerDataManager.cpp:644-650` 生成 `.sluastat` 文件，`...:665-668` 还会做 `ProfileVersion` 检查；但这属于 profiler artifact，不是 regression result。因此在“机器可消费 artifact”这个观察点上，它不是 `None`，而是 **有 artifact 但 owner 属于 profiler，不属于 test summary**，整体应判 `Partial`。

[1] 当前 `Angelscript` 的 runner 已经把原始 UE automation 报告收束成仓库自己的 `Summary.json`，并让它参与最终退出码判定：

```powershell
# ============================================================================
# [1] 文件: Tools/GetAutomationReportSummary.ps1
# 位置: 73-80, 168-238, 289-356
# 文件: Tools/RunAutomationTests.ps1
# 位置: 213-239
# 说明: structured summary 不是附带输出，而是正式测试 contract 的一部分
# ============================================================================
$priorityNames = @('index.json', 'report.json', 'AutomationReport.json', 'TestReport.json')
$files = Get-ChildItem -LiteralPath $Path -Recurse -File -Filter *.json

$reportSummary = $null
foreach ($candidate in Get-PreferredReportFiles -Path $ReportPath) {
    $candidateSummary = Try-GetReportSummaryFromJson -JsonFile $candidate.FullName
    if ($null -ne $candidateSummary) {
        $reportSummary = $candidateSummary
        break
    }
}

$summary = [ordered]@{
    BucketName      = $BucketName
    ExitCode        = $ExitCode
    ReportJsonPath  = $null
    Total           = $null
    Passed          = $null
    Failed          = $null
    FailedTests     = @()
    SummarySource   = 'None'
}

$serializableSummary | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $SummaryPath -Encoding UTF8
# ★ runner 自己定义统一 summary schema，而不是把 UE 原始 report 直接交给调用方解析

$summaryObject = & (Join-Path $PSScriptRoot 'GetAutomationReportSummary.ps1') `
    -ReportPath $outputLayout.ReportPath `
    -LogPath $outputLayout.LogPath `
    -ExitCode $processExitCode `
    -BucketName $target `
    -SummaryPath $summaryPath

if ($processExitCode -eq 0 -and $null -ne $summaryRecord) {
    $missingStructuredSummary = $summarySource -eq 'None'
    if ($failedCount -gt 0 -or $failedTests.Count -gt 0 -or $logHints.Count -gt 0 -or $missingStructuredSummary) {
        $scriptExitCode = $exitCodes.TestFailed
        # ★ structured artifact 能反向提升最终退出码，说明它不是“展示层”，而是 gate
    }
}
```

[2] `Hazelight` 已经有 machine-readable coverage artifact，但 artifact owner 主要停在 plugin 内 coverage 子系统，而不是 repo runner：

```cpp
// ============================================================================
// [2] 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/CodeCoverage/AngelscriptCodeCoverage.cpp
// 位置: 21-40, 58-64, 181-198
// 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Tests/AngelscriptCodeCoverageTests.cpp
// 位置: 14-18, 66-83
// 说明: Hazelight 的测试生命周期会自动写 coverage artifact，且 integration test 会验证文件存在
// ============================================================================
AutomationController->OnTestsAvailable().AddRaw(this, &FAngelscriptCodeCoverage::OnTestsStarting);
AutomationController->OnTestsComplete().AddRaw(this, &FAngelscriptCodeCoverage::OnTestsStopping);

void FAngelscriptCodeCoverage::OnTestsStopping()
{
	FString OutputDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("CodeCoverage"));
	StopRecordingAndWriteReport(OutputDir);
}

void FAngelscriptCodeCoverage::StopRecordingAndWriteReport(const FString& OutputDir)
{
	WriteReportHtml(OutputDir);
	WriteCoverageSummaries(OutputDir); // ★ 自动测试完成后立刻落 HTML + summary JSON
}

if (!WriteTopLevelCoverageJson(Root, OutputDir))
{
	UE_LOG(Angelscript, Warning, TEXT("Failed writing code coverage JSON"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAngelscriptCodeCoverageTests0,
	"Angelscript.CppTests.AngelscriptCodeCoverage.IntegrationTest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter);

Coverage.StopRecordingAndWriteReport(TempDir);
TestTrue(FString::Printf(TEXT("Should have written an index.html at %s"), *ExpectedIndexPath),
	PlatformFile.FileExists(*ExpectedIndexPath));
// ★ artifact 被测试反向锁住，但仓库没有再往上叠一层统一 Summary.json / metadata owner
```

[3] `UnLua`、`UnrealCSharp`、`puerts`、`sluaunreal` 这四者的 artifact owner 分别落在不同层级，且都没达到当前 AS 的 repo-owned regression summary：

```cpp
// ============================================================================
// [3] 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Perfs/UnLuaBenchmarkFunctionLibrary.cpp
// 位置: 46-50
// 文件: Reference/UnrealCSharp/UnrealCSharp.uplugin
// 位置: 18-54
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnv.cpp
// 位置: 42-49
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PuertsWasm/WasmTestForStaticBinding.cpp
// 位置: 21-28
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/SluaProfilerDataManager.cpp
// 位置: 644-650, 665-668
// 说明: 这四个仓库分别代表 benchmark CSV、无 test module、testing hook、profiler binary 四种 owner
// ============================================================================
void UUnLuaBenchmarkFunctionLibrary::Stop()
{
	const auto FilePath = FString::Printf(TEXT("%sBenchmark/%s-Benchmark-%s.csv"),
		*FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()),
		*BenchmarkTitle, *FDateTime::Now().ToString());
	FFileHelper::SaveStringToFile(Message, *FilePath);
	// ★ UnLua 有 machine artifact，但这是 benchmark owner，不是统一 regression summary
}

"Modules": [
	{ "Name": "UnrealCSharp", "Type": "Runtime" },
	{ "Name": "UnrealCSharpEditor", "Type": "Editor" },
	{ "Name": "ScriptCodeGenerator", "Type": "Editor" },
	{ "Name": "Compiler", "Type": "Editor" },
	{ "Name": "UnrealCSharpCore", "Type": "Runtime" },
	{ "Name": "CrossVersion", "Type": "Runtime" },
	{ "Name": "SourceCodeGenerator", "Type": "Program" }
]
// ★ 当前快照模块表里未见对等 test module

void FJsEnv::RequestMinorGarbageCollectionForTesting()
{
	GameScript->RequestMinorGarbageCollectionForTesting();
}
// ★ puerts 有 testing hook，但 hook 不等于 summary artifact owner

WASM_BEGIN_LINK_GLOBAL(TestMath, 0)
WASM_LINK_GLOBAL(atan2_ue_bind)
WASM_END_LINK_GLOBAL(TestMath, 0)
// ★ 这是静态绑定测试样例，不是 repo-owned regression report

FString filePath = FPaths::ProfilingDir() + "/Sluastats/" + ... + ".sluastat";
...
if (version != ProfileVersion)
{
	UE_LOG(Slua, Warning, TEXT("sluastat file version mismatch: %d, %d"), version, ProfileVersion);
	return;
}
// ★ slua 的 machine artifact owner 在 profiler，不在 pass/fail regression
```

#### 对比矩阵（本轮新增观察点）

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 每次 run 自动生成统一 pass/fail summary artifact | None | None | None | None | None | Full |
| structured artifact 可反向影响最终退出码 | None | None | None | None | None | Full |
| coverage artifact 会随自动化测试生命周期自动落盘 | Full | None | None | None | None | Full |
| 仓库拥有 benchmark / profiler / perf 类 machine artifact，但不是 regression summary | None | None | Full | Partial | Full | Partial |
| 当前源码快照中能定位到明确 test module / test project owner | Partial | None | Full | None | None | Full |

#### 小结与建议

- 当前 `Angelscript` 在这个观察点上的差异化优势，是 **测试结果 owner 已经上提到 repo runner**。`Summary.json`、退出码提升和 coverage artifact 一起构成了完整的 machine-consumable contract，这条线应按 `P0` 保持。
- `Hazelight` 提供的真正启发不是“补更多测试”，而是 **coverage artifact 可以先在 runtime/plugin 层自洽，再由 runner 往上汇总**。当前 AS 已经比它更进一步，后续应继续保持这两层分工，不必回退成只有 coverage 文件、没有统一 summary 的状态。
- `UnLua` 提醒了另一个值得吸收的点：benchmark / perf artifact 也应有清晰 owner。但它应作为当前 AS regression artifact 的补充层，而不是替代层，优先级 `P1`。
- `puerts` 和 `sluaunreal` 说明“有 test hook / 有 profiler binary”并不等于“有 regression artifact owner”。当前 AS 后续若补性能或 debugger 工具，也应显式区分 **测试摘要** 与 **分析副产物**，优先级 `P1`。

### [D11] 部署前的 self-verification gate：谁会主动拒绝旧缓存、缺失工件或 build 不匹配

#### 各插件实现概览

```
[D11-SelfGate] Artifact -> Gate Behavior
AS(now) : Binds.Cache fatal + BuildIdentifier check + StaticJIT GUID mismatch clears JIT db
HZ      : same lineage gate: Binds.Cache fatal + build/guid mismatch fallback
UC      : stage PublishDirectory + assembly probing; missing DLL => soft miss, no build seal
UL      : raw script path lookup; missing file => warning/return false, no artifact seal
PU      : editor MD5/version cache for TS analysis, not runtime deployment seal
SL      : script loading delegated to project; version gate only protects .sluastat profiler file
```

前文 `D11` 已经比较过 operator surface 和路径配置。本轮只看更硬的一层：**真正发生加载时，谁会主动拒绝旧 artifact，谁只会软失败，谁甚至只在 editor 辅助缓存上做 freshness 检查**。

#### 详细对比

##### 子维度 1：核心 runtime artifact 是否有 fail-fast gate

- 当前 `Angelscript` 与 `Hazelight` 在这条线上是同一技术谱系，而且当前仓库已经保持了 upstream 的安全下限。`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp:103-115` 会在 `Binds.Cache` 缺失或过旧导致 `Classes/Structs` 为空时直接 `Fatal`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp:2642-2645` 又把 precompiled data 是否可用收束成 `BuildIdentifier == GetCurrentBuildIdentifier()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1536-1555` 进一步在 `PrecompiledDataGuid` 与编进二进制的 StaticJIT guid 不一致时清空 `FJITDatabase`。这不是单点 warning，而是一整套 **缺失即 fatal、旧版本即 discard、guid 不符即禁用 JIT** 的 gate。`Hazelight` 在 `J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/AngelscriptBindDatabase.cpp:99-110` 与 `.../AngelscriptManager.cpp:458-477` 上保持同构实现，因此这里两者都应判 `Full`。
- `UnrealCSharp` 的 contract 明显更软。`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:211-233` 会把 `PublishDirectory` 自动加入 `DirectoriesToAlwaysStageAsUFS`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp:1005-1048` 又把 publish 后的 DLL 路径列成 authority；但 `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/AssemblyLoader.cpp:6-23` 与 `.../FMonoDomain.cpp:500-513` 最终只是“找到就加载，找不到返回空”。这说明它有 **部署路径 contract**，但没有当前 AS/HZ 这种 build-sealed self-gate，应判 `Partial`。
- `UnLua` 的 gate 更像普通 loader 失败。`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaCore.cpp:59-69` 只是把 `ProjectPersistentDownloadDir` 作为热更新优先目录；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaBase.cpp:68-85` 遇到缺失脚本时发 warning 并返回 `false`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/DefaultParamCollection.cpp:27-35` 与 `Reference/UnLua/Plugins/UnLua/Source/UnLuaDefaultParamCollector/Private/UnLuaDefaultParamCollector.cpp:72-95` 说明它确实有 build-time 生成的 `DefaultParamCollection.inl`，但这一套没有暴露出 build identifier / data guid 风格的部署 seal。因此这里不能写成“没有部署策略”，更准确的判断是 **实现方式不同，且 self-verification gate 更弱**。
- `puerts` 当前快照里最强的 freshness 检查不在 runtime deployment，而在 editor code analyze cache。`Reference/puerts/unreal/Puerts/PuertsEditor/CodeAnalyze.ts:269-275,495-510` 会为 `.ts` 文件保存 MD5 到 `ts_file_versions_info.json`，修改时只在 MD5 变化后重跑处理并重写版本文件。这是 **editor semantic cache freshness**，不是 runtime script / bytecode / VM artifact seal。因此在本轮观察点上应判 `Partial`，而且要明确是“freshness owner 在 editor cache，不在部署物本身”。
- `sluaunreal` 几乎是另一端。`Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaState.h:167-189` 说明脚本加载完全依赖项目提供 `LoadFileDelegate`；`Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaState.cpp:651-653` 只是把这个 delegate 存进去；唯一明显的版本 gate 反而在 `Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/ProfileDataDefine.h:25-29` 与 `.../SluaProfilerDataManager.cpp:665-668`，它只保护 `.sluastat` profiler binary。这里应判为 `None` for deployment self-gate，而不是 `None` for all versioning。

[1] 当前 `Angelscript` 已经把 bind cache、precompiled data 和 StaticJIT 编译产物串成连续的 self-verification gate：

```cpp
// ============================================================================
// [1] 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp
// 位置: 103-115
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp
// 位置: 2642-2645
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h
// 位置: 74-79
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 1531-1555
// 说明: 缺失 bind cache、build id 不符、JIT guid 不符三层 gate 都是 runtime 主路径的一部分
// ============================================================================
void FAngelscriptBindDatabase::Load(const FString& Path, bool bGeneratingPrecompiledData)
{
	...
	if (Classes.Num() == 0 && Structs.Num() == 0)
	{
		UE_LOG(Angelscript, Fatal, TEXT("Unable to load script bind database, Script/Binds.Cache file is missing or old. This will cause script compilation and execution to fail."));
	}
}

bool FAngelscriptPrecompiledData::IsValidForCurrentBuild()
{
	return BuildIdentifier == GetCurrentBuildIdentifier() && BuildIdentifier != -1;
}

struct FStaticJITCompiledInfo
{
	FGuid PrecompiledDataGuid;
	...
};

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
			FJITDatabase::Get().Clear(); // ★ guid 不符时直接拒绝使用已编译 JIT 产物
		}
	}
}
```

[2] `Hazelight` 在 self-gate 上与当前 AS 同谱系；`UnrealCSharp` 则更多是路径 contract，没有 build seal：

```cpp
// ============================================================================
// [2] 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/AngelscriptBindDatabase.cpp
// 位置: 99-110
// 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/AngelscriptManager.cpp
// 位置: 453-478
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp
// 位置: 211-233
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp
// 位置: 1005-1048
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/AssemblyLoader.cpp
// 位置: 6-23
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp
// 位置: 500-513
// 说明: HZ 会主动拒绝旧 cache；UnrealCSharp 主要是“确保路径可 stage + 可探测”
// ============================================================================
if (Classes.Num() == 0 && Structs.Num() == 0)
{
	UE_LOG(Angelscript, Fatal, TEXT("Unable to load script bind database, Script/Binds.Cache file is missing or old. This will cause script compilation and execution to fail."));
}

if (!PrecompiledData->IsValidForCurrentBuild())
{
	delete PrecompiledData;
	PrecompiledData = nullptr;
}
...
if (CompiledInfo != nullptr && CompiledInfo->PrecompiledDataGuid != PrecompiledData->DataGuid)
{
	FJITDatabase::Get().Clear();
}

const auto PublishDirectory = FUnrealCSharpFunctionLibrary::GetPublishDirectory();
...
ProjectPackagingSettings->DirectoriesToAlwaysStageAsUFS.Add({PublishDirectory});
// ★ UnrealCSharp 会尽量保证 assembly 被带进包

TArray<FString> FUnrealCSharpFunctionLibrary::GetFullAssemblyPublishPath()
{
	return TArrayBuilder<FString>().
	       Add(GetFullUEPublishPath()).
	       Add(GetFullGamePublishPath()).
	       Append(GetFullCustomProjectsPublishPath()).
	       Build();
}

TArray<uint8> UAssemblyLoader::Load(const FString& InAssemblyName)
{
	for (const auto& AssemblyPath : FUnrealCSharpFunctionLibrary::GetAssemblyPath())
	{
		if (const auto File = FPaths::Combine(AssemblyPath, InAssemblyName) + DLL_SUFFIX;
			IFileManager::Get().FileExists(*File))
		{
			TArray<uint8> Data;
			FFileHelper::LoadFileToArray(Data, *File);
			return Data;
		}
	}
	return {}; // ★ 找不到就软失败，不带 build seal
}

if (const auto Data = AssemblyLoader->Load(AssemblyName); !Data.IsEmpty())
{
	LoadAssembly(AssemblyName, Data, nullptr, &Assembly);
	return Assembly;
}
return nullptr;
```

[3] `UnLua`、`puerts`、`sluaunreal` 都有某种形式的 freshness / missing-file 处理，但 owner 不在核心 deployment artifact 上：

```cpp
// ============================================================================
// [3] 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaCore.cpp
// 位置: 59-69
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaBase.cpp
// 位置: 68-85
// 文件: Reference/puerts/unreal/Puerts/PuertsEditor/CodeAnalyze.ts
// 位置: 269-275, 495-510
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/ProfileDataDefine.h
// 位置: 25-29
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/SluaProfilerDataManager.cpp
// 位置: 644-650, 665-668
// 说明: UnLua 是 raw script missing-file warning，puerts 是 editor MD5 cache，slua 是 profiler sidecar versioning
// ============================================================================
FString RealFullFilePath = FullFilePath.Replace(*ProjectDir, *ProjectPersistentDownloadDir);
if (IFileManager::Get().FileExists(*RealFullFilePath))
{
	FullFilePath = RealFullFilePath;
}
else if (!IFileManager::Get().FileExists(*FullFilePath))
{
	FullFilePath = "";
}

if (FullFilePath.IsEmpty())
{
	UE_LOG(LogUnLua, Warning, TEXT("the lua file try to load does not exist! : %s"), *RelativeFilePath);
	return false; // ★ 缺文件时是 warning + false，不是 build-sealed hard gate
}

const versionsFilePath = tsi.getDirectoryPath(configFilePath) + "/ts_file_versions_info.json";
fileVersions[fileName] = { version: UE.FileSystemOperation.FileMD5Hash(fileName), processed: false, isBP: false};
...
if (md5 === fileVersions[fileName].version) {
	console.log(fileName + " md5 not changed, so skiped!");
} else {
	fileVersions[fileName].version = md5;
	onSourceFileAddOrChange(fileName, true);
}
UE.FileSystemOperation.WriteFile(versionsFilePath, JSON.stringify(fileVersions, null, 4));
// ★ 这是 editor typing/Blueprint 分析缓存 freshness，不是 runtime deployment seal

static int32 ProfileVersion = 4;
...
FString filePath = FPaths::ProfilingDir() + "/Sluastats/" + ... + ".sluastat";
...
if (version != ProfileVersion)
{
	UE_LOG(Slua, Warning, TEXT("sluastat file version mismatch: %d, %d"), version, ProfileVersion);
	return;
}
// ★ slua 的显式版本 gate 保护的是 profiler sidecar，不是脚本部署物
```

#### 对比矩阵（本轮新增观察点）

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 缺失或过旧的核心 metadata artifact 会触发 fatal / discard | Full | Partial | Partial | None | None | Full |
| deployable artifact 本身带 `BuildIdentifier` / `DataGuid` 等 seal | Full | None | None | None | None | Full |
| 缺 payload 时主要表现为软失败或 warning，而非 hard gate | None | Full | Full | N/A | Partial | None |
| freshness 检查主要发生在 editor / auxiliary cache，而不是 runtime deployable artifact | None | None | None | Full | Full | None |
| 当前可见版本 gate 保护的是 profiler / tool sidecar，而不是脚本部署物 | None | None | None | None | Full | None |

#### 小结与建议

- 当前 `Angelscript` 在这个观察点上的结论很明确：**已经达到 Hazelight 同级的 self-verification gate 强度**。`Binds.Cache`、`BuildIdentifier`、`PrecompiledDataGuid` 三层 gate 建议按 `P0` 保持，不应为了追求 loader 灵活性而削弱。
- 真正值得吸收的不是 `UnLua/puerts/sluaunreal` 的“更软”策略，而是它们给出的反例启发：如果核心部署物没有 seal，freshness 检查往往会下沉到 editor cache、下载目录或 profiler sidecar，最终很难解释线上为什么用了旧工件。当前 AS 应避免走这条路。
- 如果后续还要继续加强，可以借鉴当前 runner / dump 体系，给 `bind cache rejected / precompiled data discarded / jit guid mismatch` 再补一层 machine-readable rejection report，但应作为现有 hard gate 的补充，不是替代，优先级 `P1`。

---

## 深化分析 (2026-04-09 02:10:53)

### [D3] WorldContext 的真实 owner：hidden arg、ambient state，还是纯 metadata

前文已经比较过 `Blueprint` override 和行为挂载点。本轮只收窄到一个更底层的问题：`WorldContext` 到底由谁“拥有”。这 6 个方案实际分成 4 类：当前 `Angelscript` / `Hazelight` 把它落在 hidden arg + generated param；`UnrealCSharp` / `puerts` 主要落在 metadata authoring surface；`UnLua` 基本把它当普通参数；`sluaunreal` 则只在少量 `BlueprintLibrary` 函数里把它当成 `UGameplayStatics::GetGameInstance()` 的入口。

#### 各插件实现概览

```
WorldContext Ownership
AS(now) : HiddenArg + auto-generated _World_Context + test-owned CallableWithoutWorldContext contract
HZ      : HiddenArg + explicit CallableWithoutWorldContext/OptionalWorldContext runtime handling
UC      : C# Attribute -> UE MetaData (WorldContext / CallableWithoutWorldContext / ShowWorldContextPin)
UL      : Ordinary CPF_Parm enumeration, no dedicated WorldContext carrier found
PU      : TS decorator MetaKey surface; CallableWithoutWorldContext exposed but deprecated for blueprint function
SL      : BlueprintLibrary-only WorldContextObject -> UGameplayStatics::GetGameInstance()
```

#### 详细对比

##### 子维度 1：runtime 是否主动生成并隐藏 world-context 参数

- 当前 `Angelscript` 仍然把 `WorldContext` 放在 runtime signature 层。`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h:223-231` 会读取 `WorldContext` metadata，把对应参数默认值标准化为 `__WorldContext()`；`.../Helper_FunctionSignature.h:425-429` 再把这个参数写进 `hiddenArgumentIndex/hiddenArgumentDefault`。这说明它不是把 `WorldContext` 留给上层脚本作者手动传，而是主动变成 hidden arg。
- `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3521-3555` 又会给 static function 自动生成 `_World_Context`，并记录 `WorldContextIndex` 与 `bIsWorldContextGenerated`。因此当前 AS 的 owner 其实是“双层”的：一层在 UHT/class generator，另一层在 signature helper。
- `Hazelight` 是同一模型，但 owner 更完整。`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Helper_FunctionSignature.h:446-449` 会同时用 `OptionalWorldContext` 和 `CallableWithoutWorldContext` 决定要不要给脚本函数打 `asTRAIT_USES_WORLDCONTEXT`；`.../ClassGenerator/AngelscriptClassGenerator.cpp:3412-3413` 又把这两个 metadata 都折叠成 `bIsWorldContextOptional = true`。也就是说，HZ 把 contract 固定在 runtime/class-generator，而不是测试名义或外层文档。

[1] 当前 `Angelscript` 与 `Hazelight` 的关键差异，不在于有没有 hidden arg，而在于 `CallableWithoutWorldContext` 是否还在 runtime 链路里有明确 owner：

```cpp
// ============================================================================
// [1] 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h
// 位置: 223-231, 425-429
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 位置: 3521-3555
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp
// 位置: 239-241, 700-703
// 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Helper_FunctionSignature.h
// 位置: 446-449
// 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/ClassGenerator/AngelscriptClassGenerator.cpp
// 位置: 3412-3413
// 说明: 当前 AS 仍然会隐藏 world-context 参数，但源码里可直接看到的 trait/optional 判断只剩
//       OptionalWorldContext；HZ 则把 CallableWithoutWorldContext 继续保留在 runtime owner 链上
// ============================================================================
const FString& WorldContextParam = Function->GetMetaData(NAME_Signature_WorldContext);
if (WorldContextParam.Len() != 0)
{
	for (int32 ArgIndex = 0, ArgCount = ArgumentTypes.Num(); ArgIndex < ArgCount; ++ArgIndex)
	{
		if (ArgumentNames[ArgIndex] == WorldContextParam)
		{
			ArgumentDefaults[ArgIndex] = TEXT("__WorldContext()");
			WorldContextArgument = ArgIndex;
		}
	}
}
...
ScriptFunction->hiddenArgumentIndex = WorldContextArgument;
ScriptFunction->hiddenArgumentDefault = "__WorldContext()";
#if WITH_EDITOR
if (!Function->HasMetaData(NAME_OptionalWorldContext))
	ScriptFunction->traits.SetTrait(asTRAIT_USES_WORLDCONTEXT, true);
#endif

if (FunctionDesc->bIsStatic)
{
	// ★ 当前 AS 对 static function 直接生成 fake world-context 参数
	ArgDesc.ArgumentName = TEXT("_World_Context");
	...
	NewFunction->WorldContextIndex = FunctionDesc->Arguments.Num();
	NewFunction->bIsWorldContextGenerated = true;
}

// ★ 但测试侧仍把 “CallableWithoutWorldContext 保留 hidden arg、清 trait” 当成 contract
TestEqual(TEXT("... should hide the world-context argument for callable-without-world-context functions"), OptionalScriptFunction->hiddenArgumentIndex, 0);
return TestFalse(TEXT("... should not mark callable-without-world-context functions with the world-context trait"),
	OptionalScriptFunction->traits.GetTrait(asTRAIT_USES_WORLDCONTEXT));

// HZ upstream
if (!Function->HasMetaData(NAME_OptionalWorldContext) && !Function->HasMetaData(NAME_CallableWithoutWorldContext))
	ScriptFunction->traits.SetTrait(asTRAIT_USES_WORLDCONTEXT, true);

if (FunctionDesc->Meta.Contains(NAME_Arg_CallableWithoutWorldContext) || FunctionDesc->Meta.Contains(NAME_Arg_OptionalWorldContext))
	NewFunction->bIsWorldContextOptional = true;
```

##### 子维度 2：`CallableWithoutWorldContext` / `ShowWorldContextPin` 是 runtime contract，还是 authoring metadata

- `UnrealCSharp` 把 `WorldContext`、`CallableWithoutWorldContext`、`ShowWorldContextPin` 都显式建成 attribute 类型，`Reference/UnrealCSharp/Script/UE/Dynamic/Function/WorldContextAttribute.cs:5-13`、`.../CallableWithoutWorldContextAttribute.cs:5-8`、`.../Class/ShowWorldContextPinAttribute.cs:5-8` 是 authoring surface；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGeneratorCore.cpp:791-809,824-826,1067-1084,1232-1273` 再把这些 attribute 统一写回 UE metadata。这里 owner 在“脚本语言声明 -> UE metadata”链路，不在 runtime hidden arg。
- `puerts` 更明显是 tooling/typing owner。`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/uelazyload.js:741,788,814` 把 `ShowWorldContextPin`、`CallableWithoutWorldContext`、`WorldContext` 都注册成 metadata key；`Reference/puerts/unreal/Puerts/Typing/ue/puerts_decorators.d.ts:995-999` 甚至直接写明 `CallableWithoutWorldContext` “currently not supported by blueprint function”。这说明它暴露了 authoring surface，但没有把这条 contract 下沉成像 AS/HZ 那样的 hidden arg runtime owner。
- `UnLua` 本轮没有定位到 dedicated `WorldContext` carrier。`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:51-60` 与 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSense.cpp:193-216` 都是在 `CPF_Parm` 顺序上枚举参数并读取 `CPP_Default_*`，没有额外的 world-context 语义层。
- `sluaunreal` 则是更局部的做法。`Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaBlueprintLibrary.h:42-46,60-61` 只有 `BlueprintLibrary` 上的 `meta=(WorldContext="WorldContextObject")`；对应实现 `.../Private/LuaBlueprintLibrary.cpp:51-54,79-82,124-126` 立刻用 `UGameplayStatics::GetGameInstance(WorldContextObject)` 做 ambient lookup。这不是 general contract，而是 helper API 的局部约定。

[2] `UnrealCSharp` 把 world-context 相关语义显式物化成 attribute，再统一写回 UE metadata：

```csharp
// ============================================================================
// [2] 文件: Reference/UnrealCSharp/Script/UE/Dynamic/Function/WorldContextAttribute.cs
// 位置: 5-13
// 文件: Reference/UnrealCSharp/Script/UE/Dynamic/Function/CallableWithoutWorldContextAttribute.cs
// 位置: 5-8
// 文件: Reference/UnrealCSharp/Script/UE/Dynamic/Class/ShowWorldContextPinAttribute.cs
// 位置: 5-8
// 说明: UC 把这些 contract 先做成 C# authoring surface，再交给 generator 统一落回 UE metadata
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

[AttributeUsage(AttributeTargets.Method)]
public class CallableWithoutWorldContextAttribute : Attribute
{
    private string Value { get; set; } = "true";
}

[AttributeUsage(AttributeTargets.Class)]
public class ShowWorldContextPinAttribute : Attribute
{
    private string Value { get; set; } = "true";
}
```

[3] `puerts` 暴露了相同 metadata surface，但把 `CallableWithoutWorldContext` 明确留在 tooling 层而不是 blueprint runtime contract：

```typescript
// ============================================================================
// [3] 文件: Reference/puerts/unreal/Puerts/Typing/ue/puerts_decorators.d.ts
// 位置: 692, 995-999, 1215
// 文件: Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/uelazyload.js
// 位置: 741, 788, 814
// 说明: puerts 允许作者写这些 metadata，但自己也明确标出其中一部分并未成为 blueprint runtime contract
// ============================================================================
let ShowWorldContextPin: MetaKey;
...
/**
 * [FunctionMetadata] Used for BlueprintCallable functions that have a WorldContext pin ...
 * @deprecated
 * currently not supported by blueprint function
 */
let CallableWithoutWorldContext: MetaKey;
...
let WorldContext: MetaKey;

// runtime-side metadata key registry
"ShowWorldContextPin": MetaDataInst,
"CallableWithoutWorldContext": MetaDataInst,
"WorldContext": MetaDataInst,
```

#### 对比矩阵（本轮新增观察点）

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| runtime 会主动生成或隐藏 world-context 参数 | Full | None | None | None | None | Full |
| `CallableWithoutWorldContext` 在 runtime 链路里有显式 owner | Full | None | None | None | None | Partial |
| script authoring surface 显式暴露 `WorldContext` / `ShowWorldContextPin` | Partial | Full | None | Full | Partial | Partial |
| 实现主要依赖局部 ambient lookup，而不是通用 contract | None | None | None | None | Full | None |
| 实现主要把它当普通参数枚举处理 | None | None | Full | Partial | None | None |

#### 小结与建议

- 当前 `Angelscript` 的 hidden-arg 模型本身没有退化，真正的漂移点是：`CallableWithoutWorldContext` 的 owner 已经不再像 `Hazelight` 那样稳定地留在 runtime/class-generator，而更像是测试语义仍在、runtime 显式分支已淡出。这个差距应判为**实现方式漂移**，不是“完全没有 world-context 能力”。
- `P0` 建议是二选一，但必须明确：要么恢复 `CallableWithoutWorldContext` 的 runtime owner，按 `Hazelight` 的 `Helper_FunctionSignature.h:448-449` 与 `AngelscriptClassGenerator.cpp:3412-3413` 重新补齐；要么正式规定 `OptionalWorldContext` 是唯一 canonical key，并同步改测试名、文档和 UHT 生成语义，避免 contract 继续分裂。
- `P1` 建议继续保留当前 AS 的 hidden-arg / generated-param 方案。对比 `UnrealCSharp` / `puerts` 这类 metadata-first 方案，它更能保证 runtime 端行为一致，不会把 world-context 语义完全外包给 authoring 工具链。
- `P2` 可以吸收 `UnrealCSharp` / `puerts` 的 authoring surface，把 `WorldContext` / `ShowWorldContextPin` 暴露得更明确，但不要把 owner 从 runtime 层移走。

### [D5/D6] metadata propagation 的 owner：debugger/docs/typing 谁真正接到 UE metadata

前文已经比较过 debugger bootstrap、type declaration 和 unsupported symbol manifest。本轮只追踪一条更具体的链：**metadata 从 UField/UFunction 出发后，到底有没有真正到 debugger、docs 或 typing surface**。这里差异非常大。当前 `Angelscript` 与 `Hazelight` 都有 metadata-aware 的 docs/debugger，但当前 AS 的覆盖面更窄；`UnrealCSharp`、`UnLua`、`puerts` 则更像“metadata -> 代码生成/声明生成”的链路；`sluaunreal` 在本轮源码扫描里没有暴露 dedicated metadata export path。

#### 各插件实现概览

```
UE MetaData Propagation
AS(now) : manual bind meta -> debugger whitelist(3 keys) / docs ToolTip+Category ; payload wildcard lost
HZ      : manual bind meta -> debugger whitelist(4 keys incl DelegateWildcardParam) / docs ToolTip+Category
UC      : C# Attribute -> SetMetaData(UField) -> ScriptCodeGenerator reads Comment / ToolTip / defaults
UL      : CopyMetadata(override function) -> IntelliSense reads ToolTip / CPP_Default_*
PU      : UEMeta.js SetMetaData(class/function/param/property) -> DeclarationGenerator reads MetaMap + ToolTip
SL      : raw UFUNCTION metadata exists, but no dedicated export path located in this round
```

#### 详细对比

##### 子维度 1：delegate metadata 是否真的进入 debugger/client

- 当前 `Angelscript` 的 debugger 只转发 3 个 meta key。`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:34-37,1600-1607` 的 `NAMES_InformedMeta` 只有 `DelegateBindType`、`DelegateFunctionParam`、`DelegateObjectParam`；而 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FAngelscriptDelegateWithPayload.cpp:28-32` 的 `BindWithPayload(...)` 也没有配 `SCRIPT_MANUAL_BIND_META`。两者叠加的结果是：payload delegate 的 wildcard 语义没有进入 debugger/client surface。
- `Hazelight` 则保留了完整链。`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_FAngelscriptDelegateWithPayload.cpp:27-39` 给 `BindUFunction` 和 `BindWithPayload` 都写了 metadata，其中后者额外包含 `DelegateWildcardParam = Payload`；`.../Debugging/AngelscriptDebugServer.cpp:34-39,1478-1485` 也把它加入 `NAMES_InformedMeta`。这不是“有没有 delegate payload 功能”的差别，而是**metadata 是否真的传播到工具面**的质量差异。

[1] 当前 AS 与 HZ 在 delegate payload metadata 传播链上的差距非常具体，而且是两头都少一截还是完整闭环的区别：

```cpp
// ============================================================================
// [1] 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 位置: 34-37, 1600-1607
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FAngelscriptDelegateWithPayload.cpp
// 位置: 28-32
// 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Debugging/AngelscriptDebugServer.cpp
// 位置: 34-39, 1478-1485
// 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_FAngelscriptDelegateWithPayload.cpp
// 位置: 27-39
// 说明: 当前 AS 有 payload bind runtime 功能，但没把 wildcard metadata 发进 debugger；HZ 则是完整闭环
// ============================================================================
const TArray<FName> NAMES_InformedMeta = {
	"DelegateBindType",
	"DelegateFunctionParam",
	"DelegateObjectParam",
};
...
for (FName MetaTag : NAMES_InformedMeta)
{
	const FString& MetaValue = UnrealFunction->GetMetaData(MetaTag);
	...
}

Delegate_.Method("void BindWithPayload(UObject Object, const FName& FunctionName, const ?&in Payload)",
[](FAngelscriptDelegateWithPayload& Delegate, UObject* Object, const FName& FunctionName, void* PayloadPtr, int PayloadScriptTypeId)
{
	Delegate.BindUFunctionWithPayload(Object, FunctionName, PayloadPtr, PayloadScriptTypeId);
});
// ★ 当前 AS 这里没有 SCRIPT_MANUAL_BIND_META，payload 语义不会自动进入 debug/client 元数据

// HZ upstream
const TArray<FName> NAMES_InformedMeta = {
	"DelegateBindType",
	"DelegateFunctionParam",
	"DelegateObjectParam",
	"DelegateWildcardParam",
};
...
SCRIPT_MANUAL_BIND_META("DelegateObjectParam", "Object");
SCRIPT_MANUAL_BIND_META("DelegateFunctionParam", "FunctionName");
SCRIPT_MANUAL_BIND_META("DelegateBindType", "FInternalEmptyDelegateWithPayload");
SCRIPT_MANUAL_BIND_META("DelegateWildcardParam", "Payload");
```

##### 子维度 2：`ToolTip` / `CPP_Default_*` 是否真的进入 docs / typing / IntelliSense

- 当前 `Angelscript` 的 metadata downstream 主要是 docs。`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp:31-38` 把 passed doc 挂到 `FunctionId`；`.../AngelscriptDocs.cpp:495-504,535-563` 会从 `ToolTip` / `Category` 抽回函数、属性、类文档。它能说明“metadata 会进入 docs”，但本轮源码里没有看到同强度的 typing/export surface。
- `Hazelight` 在 docs 上与当前 AS 同谱系，`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/AngelscriptDocs.cpp:355-359,488-497,528,555-556` 也会回填 `ToolTip` / `Category`。真正新增的是 debugger metadata coverage，而不是 docs 结构本身。
- `UnrealCSharp` 把 owner 放在“统一写 metadata，再统一读 metadata 生成 C# 声明”。`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGeneratorCore.cpp:791-809,824-826` 会把 attribute 去掉 `Attribute` 后缀后写进 `SetMetaData`；`.../FDynamicGeneratorCore.cpp:1067-1084,1232-1273` 又声明 class/function 级要接受 `ToolTip`、`ShowWorldContextPin`、`CallableWithoutWorldContext`、`WorldContext` 等 attribute。随后 `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp:464-469,946-960,1097-1100,1289-1310` 会再从 `Comment`、`CPP_Default_*` 和参数 metadata 里生成声明文本。这说明 UC 的 metadata owner 不是 debugger，而是 code generator。
- `UnLua` 走的是“复制 metadata 到 override UFunction，再给 IntelliSense 消费”。`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaFunction.cpp:139-145` 用 `UMetaData::CopyMetadata(Function, this)`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSense.cpp:193-216,275-277` 读取参数 `ToolTip`、`CPP_Default_*` 和属性 `ToolTip`。因此它虽没有 HZ/AS 风格的 debugger informed meta，但 metadata 确实进了 IDE surface。
- `puerts` 则把 metadata 流水线做得最结构化。`Reference/puerts/unreal/Puerts/Content/JavaScript/PuertsEditor/UEMeta.js:649,1025,1096,1445-1446` 在 class/function/param/property 四层都写 `SetMetaData`；`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:996-1004,1189-1192` 再读取 `MetaMap` 与 `ToolTip`；同时 `Reference/puerts/unreal/Puerts/Typing/ue/puerts_decorators.d.ts:1584` 把 `ToolTip` 自身也暴露成 `MetaKey`。owner 明显在 declaration/typing pipeline。
- `sluaunreal` 这轮只看到了 helper library 的 raw `UFUNCTION metadata`。对 `Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal` 执行 `rg -n "GetMetaData|SetMetaData|UMetaData"` 返回 `NO_MATCH`，因此本轮更准确的判断是：**未定位到 dedicated metadata export path**，而不是绝对断言它完全没有 metadata 使用。

[2] `UnrealCSharp` 与 `UnLua` 都不是简单“把 metadata 写进 UField 就完了”，而是各自把 metadata 接到了生成链路：

```cpp
// ============================================================================
// [2] 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGeneratorCore.cpp
// 位置: 791-809, 824-826
// 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp
// 位置: 464-469, 946-960, 1289-1310
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaFunction.cpp
// 位置: 139-145
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSense.cpp
// 位置: 193-216, 275-277
// 说明: UC 是 “attribute -> UE metadata -> C# declaration”，UL 是 “copy metadata -> IntelliSense”
// ============================================================================
void FDynamicGeneratorCore::SetMetaData(FField* InField, const FString& InAttribute, const FString& InValue)
{
	InField->SetMetaData(*InAttribute.LeftChop(9), *InValue);
}
...
SetFieldMetaData(InFunction, GetFunctionMetaDataAttributes(), InReflection, [](){});

auto Comment = Function->GetMetaData(TEXT("Comment"));
...
const auto MetaData = InFunction->GetMetaData(*Key); // ★ 读取 CPP_Default_* / param metadata 生成声明

#if WITH_METADATA
UMetaData::CopyMetadata(Function, this); // ★ override 后的 Lua UFunction 继承原函数 metadata
#endif

const FString& PropertyComment = Property->GetMetaData(NAME_ToolTip);
...
FName KeyName = FName(*FString::Printf(TEXT("CPP_Default_%s"), *Property->GetName()));
const FString& Value = Function->GetMetaData(KeyName);
...
const FString& ToolTip = Property->GetMetaData(NAME_ToolTip);
```

[3] `puerts` 的 metadata 传播链从 JS authoring 一直连到 declaration generator：

```javascript
// ============================================================================
// [3] 文件: Reference/puerts/unreal/Puerts/Content/JavaScript/PuertsEditor/UEMeta.js
// 位置: 649, 1025, 1096, 1445-1446
// 文件: Reference/puerts/unreal/Puerts/Typing/ue/puerts_decorators.d.ts
// 位置: 1584
// 说明: puerts 在 class/function/param/property 四层都保留 metadata，再让 typing/declaration 消费
// ============================================================================
metaData.forEach((value, key) => { metaDataResult.SetMetaData(key, value); });
...
metaData.forEach((value, key) => { metaDataResult.SetMetaData(key, value); });
...
metaData.forEach((value, key) => { metaDataResult.SetMetaData(key, value); });
...
metaData.forEach((value, key) => { 
    metaDataResult.SetMetaData(key, value); 
});
...
let ToolTip: MetaKey;
```

#### 对比矩阵（本轮新增观察点）

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| metadata 有集中写入入口 | Full | Full | Partial | Full | None | Partial |
| debugger/client 会收到结构化 function metadata | Full | None | None | None | None | Partial |
| `ToolTip` / `CPP_Default_*` 等 metadata 会进入 docs / typing / IntelliSense | Partial | Full | Full | Full | None | Partial |
| delegate payload metadata 能到达工具面 | Full | N/A | N/A | N/A | N/A | None |
| 本轮源码能定位到 dedicated metadata export path | Full | Full | Full | Full | None | Partial |

#### 小结与建议

- 当前 `Angelscript` 在 metadata propagation 上并不是“没有”，而是**有 docs、有少量 debugger meta，但没有形成像 `UnrealCSharp` / `UnLua` / `puerts` 那样的统一 downstream**。这应判为**实现质量差异**，不是“完全缺失”。
- `P0` 建议直接补齐 `DelegateWildcardParam` 传播闭环：在 `Bind_FAngelscriptDelegateWithPayload.cpp` 增加 `SCRIPT_MANUAL_BIND_META("DelegateWildcardParam", "Payload")`，并把 `AngelscriptDebugServer.cpp` 的 `NAMES_InformedMeta` 扩成与 HZ 一致。这个改动局部、收益明确，而且已有 upstream 证据。
- `P1` 建议把当前 `AngelscriptDocs.cpp` 的 metadata 读取逻辑继续向 structured export 推进，例如让 docs/debugger 之外的 IDE surface 也能消费同一份 metadata，而不是每个工具各自重新猜。
- `P2` 可吸收 `UnrealCSharp` / `puerts` 的“authoring metadata -> generated artifact”思想，但不必照搬其语言层装饰器模型；AS 更适合把 metadata 收束成 engine-facing manifest 或 debug schema。

### [D8] out/ref ABI 的 owner：FOutParmRec、staged copy-back，还是 VM-native lowering

前文已经比较过容器桥接、冷启动摊销和故障遏制。本轮只看 `out/ref` 参数 ABI 的最终 owner。这里最值得区分的是：当前 `Angelscript` / `Hazelight` 并不是“没有 out/ref 支持”，而是把 owner 放在自己控制的 VM lowering；`UnrealCSharp` / `UnLua` / `puerts` / `sluaunreal` 则普遍复用 UE 的 `FOutParmRec` / `FFrame` 语义，再在外层做桥接。

#### 各插件实现概览

```
Out/Ref ABI Owner
AS(now) : Generated ASClass => VMArgs/RESULT_PARAM fast path; ReflectiveFallback => scratch buffer + manual copy-back
HZ      : Same VM-native lowering lineage, with ReferencePOD/Return copy-back
UC      : Preclassify ref/out indexes + rebuild/scan FOutParmRec from FFrame
UL      : FParamBuffer + FOutParmRec + FindOutParmRec around ProcessEvent
PU      : Mixed path: BP call rebuilds FOutParmRec, native call reuses Stack.OutParms
SL      : LuaFunctionAccelerator scratch params/outParams + FOutParmRec + referencePusher
```

#### 详细对比

##### 子维度 1：当前 `Angelscript` / `Hazelight` 把 ABI owner 放在哪一层

- 当前 `Angelscript` 有两个明显不同的路径。`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:181-210,223-245` 在 generated script function 路径里直接构建 `VMArgs`，把 `RESULT_PARAM`、`WorldContextObject`、对象指针等按 `EArgumentVMBehavior` 压进 AngelScript VM 调用约定里；这里没有 `FOutParmRec`。这是一条 **VM-native lowering** 路径。
- 但 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp:20-24,51-57,302-370` 的 reflective fallback 则完全是另一种 owner：先分配 `ParameterBuffer`，再把 `CPF_ReferenceParm` 收集成 `FReflectiveOutReference[]`，`ProcessEvent` 之后手工做第二次 `CopySingleValue()` 回写。也就是说，当前 AS 不是统一一条 ABI，而是“生成路径走 VM-native，兜底路径走 staged copy-back”。
- `Hazelight` 在已分析到的生成路径上与当前 AS 同谱系。`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/ClassGenerator/ASClass.cpp:140-166,183-210,267-306` 也是 `VMArgs + RESULT_PARAM + ReferencePOD`，并在返回阶段把引用/返回值拷回 `RESULT_PARAM`。差异不在模型，而在当前 AS 额外多了一条 reflective fallback owner。

[1] 当前 AS / HZ 的关键不是“支不支持 out/ref”，而是 owner 是否落在 plugin 自己的 VM ABI：

```cpp
// ============================================================================
// [1] 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp
// 位置: 20-24, 51-57, 302-370
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp
// 位置: 181-210, 223-245
// 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/ClassGenerator/ASClass.cpp
// 位置: 140-166, 183-210, 267-306
// 说明: 当前 AS 是 “generated path 走 VM-native，fallback path 走 staged copy-back”；HZ 在生成路径上同谱系
// ============================================================================
struct FReflectiveOutReference
{
	FProperty* Property = nullptr;
	void* ScriptValue = nullptr;
};

void InitializeParameterBuffer(const UFunction* Function, uint8* Buffer)
{
	FMemory::Memzero(Buffer, Function->ParmsSize);
	...
}

uint8* ParameterBuffer = static_cast<uint8*>(FMemory_Alloca(Function->ParmsSize));
...
if (Property->HasAnyPropertyFlags(CPF_ReferenceParm) && !Property->HasAnyPropertyFlags(CPF_ConstParm))
{
	OutReferences[OutReferenceCount++] = { Property, SourceAddress };
}
...
TargetObject->ProcessEvent(Function, ParameterBuffer);
...
OutReference.Property->CopySingleValue(
	OutReference.ScriptValue,
	OutReference.Property->ContainerPtrToValuePtr<void>(ParameterBuffer)); // ★ 第二次 copy-back

// generated AS function path
asDWORD* VMArgs = (asDWORD*)FMemory_Alloca(8 * ArgumentCount + 16);
...
*(void**)VMArgs = RESULT_PARAM; // ★ 返回值直接走 VM 调用约定
...
case UASFunction::EArgumentVMBehavior::WorldContextObject:
{
	Stack.StepCompiledIn<FProperty>(&Ptr);
	NewWorldContext = (UObject*)Ptr;
	*(void**)VMArgs = Ptr;
	VMArgs += 2;
}

// HZ upstream
case UASFunction::EArgumentVMBehavior::ReferencePOD:
{
	uint8& RefValue = Stack.StepCompiledInRef<FProperty, uint8>(StackPtr);
	*(void**)VMArgs = &RefValue; // ★ 引用直接降到 VM ABI
}
...
if (RetValue != nullptr)
	FMemory::Memcpy(RESULT_PARAM, RetValue, ASFunction->ReturnArgument.ValueBytes);
```

##### 子维度 2：其他 4 个插件如何复用 UE `FOutParmRec`

- `UnrealCSharp` 在 `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FFunctionDescriptor.cpp:49-56` 先把 `ReferencePropertyIndexes` / `OutPropertyIndexes` 预分类；`.../FCSharpFunctionDescriptor.cpp:21,37-53,90-98,137-145,188-192` 再从 `FFrame` 重建 `FOutParmRec` 链、按 property 查找 `PropAddr` 并回写。owner 明确在 UE out-param 机制，不在 CLR 自定义 ABI。
- `UnLua` 同样复用 UE 语义。`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:92-112` 会在 `bUnpackParams` 路径上为 `CPF_OutParm` 构造 `FOutParmRec`；`...:227` 之后 `CallLuaInternal()` 再通过 `FindOutParmRec()` 把结果写回。它的 scratch buffer 只是临时承载，ABI owner 仍是 UE。
- `puerts` 是混合模型，但仍以 `FOutParmRec` 为主。`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp:315-324` 会为分析到的 `CPF_OutParm` 构造 `FOutParmRec`；`...:493-537` 在 `CallByBP` 慢路径里重建 `NewOutParms`；而 `...:575-582,605-611` 又能在另一条路径上直接复用 `Stack.OutParms`。这是本轮看到的 `FOutParmRec` owner 使用得最彻底的一家。
- `sluaunreal` 则在 `Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaFunctionAccelerator.cpp:46-48,191-219,239-271,334-336,406-427` 里同时维护 `params`、`propertyList`、`outParams` scratch buffer，并给 `outParmRecProps` 逐个创建 `FOutParmRec`。它比 UC/UL 多了一层自己的 `referencePusher` / `outParams` bookkeeping，但 ABI owner 仍然是以 UE out/ref 语义为核心。

[2] 另外 4 个方案虽然桥接层写法不同，但都没有像 AS/HZ 那样把 generated-call ABI 完全改造成 VM-native lowering：

```cpp
// ============================================================================
// [2] 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FFunctionDescriptor.cpp
// 位置: 49-56
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FCSharpFunctionDescriptor.cpp
// 位置: 21, 37-53, 90-98, 137-145, 188-192
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp
// 位置: 92-112, 227, 415-419, 431-478
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp
// 位置: 315-324, 493-537, 575-582, 605-611
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaFunctionAccelerator.cpp
// 位置: 46-48, 191-219, 334-336, 406-427
// 说明: 这 4 家都把 UE 的 FOutParmRec / FFrame 当作 ABI owner，再在外层做语言桥接
// ============================================================================
if (Property->HasAnyPropertyFlags(CPF_OutParm) && !Property->HasAnyPropertyFlags(CPF_ConstParm))
{
	if (IsNativeFunction || Property->HasAnyPropertyFlags(CPF_ReferenceParm))
	{
		ReferencePropertyIndexes.Emplace(Index);
	}
	OutPropertyIndexes.Emplace(Index);
}
...
FOutParmRec* NewOutParams{};
...
const auto OutParam = (FOutParmRec*)FMemory_Alloca(sizeof(FOutParmRec));
OutParam->PropAddr = InStack.MostRecentPropertyAddress != nullptr
	? InStack.MostRecentPropertyAddress
	: Property->ContainerPtrToValuePtr<uint8>(Params);
...
ReferenceParam = FindOutParmRec(ReferenceParam, ReferencePropertyDescriptor->GetProperty());

FOutParmRec* OutParms = Stack.OutParms;
...
FOutParmRec* Out = (FOutParmRec*)FMemory_Alloca(sizeof(FOutParmRec));
...
Object->UObject::ProcessEvent(FinalFunction, Params);
...
if (OutParam->Property == OutProperty)
	return OutParam;

FOutParmRec* Out = nullptr;
if (Property->HasAnyPropertyFlags(CPF_OutParm))
{
	Out = (FOutParmRec*) FMemory_Alloca(sizeof(FOutParmRec));
}
...
FOutParmRec* Out = Stack.OutParms;
while (Out->Property != Property)
{
	Out = Out->NextOutParm;
}
...
auto OutParmRec = GetMatchOutParmRec(OutParms, Arguments[i]->Property);

if (prop->HasAnyPropertyFlags(CPF_OutParm))
{
	outParmRecProps.Add(prop);
}
...
auto out = (FOutParmRec*)FMemory_Alloca(sizeof(FOutParmRec));
out->Property = prop;
out->PropAddr = prop->ContainerPtrToValuePtr<uint8>(params);
...
pusherInfo.referencePusher(L, prop, src, reinterpret_cast<void*>(*(outParams + index)));
```

#### 对比矩阵（本轮新增观察点）

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| analyzed generated-call path 直接做 VM-native lowering | Full | None | None | None | None | Full |
| analyzed bridge path 以 `FOutParmRec` 为显式 ABI owner | None | Full | Full | Full | Full | Partial |
| 明确存在 scratch params buffer / staged bridge | Partial | Full | Full | Full | Full | Full |
| 插件自定义代码负责最终 copy-back，而不是完全依赖 UE 默认流程 | Full | Partial | Partial | Partial | Partial | Full |
| 同一插件内部存在两套 owner（fast path 与 fallback path） | Partial | None | None | Full | Partial | Full |

#### 小结与建议

- 当前 `Angelscript` 在 `D8` 的差异应该判为**实现方式不同**，不是“缺少 out/ref 能力”。它的优势在于 generated path 直接控制 VM ABI，避免所有调用都绕 UE `FOutParmRec`；它的代价在于 reflective fallback 需要额外 scratch buffer 和 copy-back。
- `P0` 建议继续保留 generated path 的 VM-native lowering，不要为了和其他插件表面一致而退回完全 `FOutParmRec`-centric 的桥接。对 AS 这种自带 VM 的方案，这是核心优势，不是负担。
- `P1` 值得吸收的是 `puerts` / `UnrealCSharp` 那种“能复用 `Stack.OutParms` 就复用，必须慢路径时再重建”的分层思路。当前 AS 如果要继续优化 `BlueprintCallableReflectiveFallback.cpp`，优先方向不是再加更多缓存，而是减少 fallback 命中频率，或给其中一部分场景补 `FOutParmRec` 兼容快路径。
- `P2` 建议补一层可观测性：统计 reflective fallback 命中次数、参数规模和 copy-back 成本。否则很难判断这条慢路径究竟只是少数兜底，还是已经在吞掉大量脚本调用开销。

---

## 深化分析 (2026-04-09 02:26:16)

### [D9] 回归夹具的真实宿主：公开示例到底会不会自动变成 release gate

前面的 `D9` 已经把 runner、batch 和 artifact owner 拆开了；这一轮只补一个更靠近“长期可维护性”的问题：**仓库里给用户看的公开示例、教程和对外脚本，到底有没有被同一套回归机制保护**。源码表明，6 个方案的差异不在“有没有测试”，而在**测试夹具和公开示例是否共享 carrier**。

#### 各插件实现概览

```
Public Example -> Regression Fixture
HZ : script module prefixes -> UnitTest_/IntegrationTest_     // 测试出生在脚本模块里
AS : FScriptExampleSource + file-backed coverage .as          // 公开示例显式要求挂自动化
UC : public docs/template visible, no plugin-tree fixture hit // 本轮仅见脚手架与发布链
UL : Tutorials/*.lua public; UnLuaTestSuite guards APIs/issues// 教程链与测试链并存但分离
PU : typings/editor analysis visible, no plugin-tree fixture  // 当前 UE 插件子树未定位自动化夹具
SL : README/demo visible, no plugin-tree fixture              // demo 工程存在，但未见同层自动化入口
```

#### 详细对比

- `Hazelight` 的测试 contract 仍然是“脚本函数命名约定 -> 运行时发现”。`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Testing/DiscoverTests.cpp:166-202` 直接按 `Test_` / `ComplexUnitTest_` / `IntegrationTest_` / `ComplexIntegrationTest_` 前缀扫描 `asIScriptFunction`。这说明它的 **test carrier 是脚本模块本身**。同时，公开示例仍位于 `J:/UnrealEngine/UEAS2/Script-Examples/Examples/Example_Actor.as:1-40` 这一类 file-backed `.as` 目录。两边都存在，但**公开示例树本身并没有在同一段代码里被发现为测试夹具**，因此这里更准确的判断是：`有测试`，但 `公开示例 != 自动化夹具`。
- 当前 `Angelscript` 则把这两条链故意拉近。`Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleTestSupport.cpp:16-40` 的 `RunScriptExampleCompileTest()` 直接把 `FScriptExampleSource` 变成 automation input；`Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleCoverageTests.cpp:40-62` 又显式从磁盘读取 `Documents/Plans/Plan_ScriptExamplesExpansion/Coverage/*.as` 再编译。更关键的是 `Script/Examples/README.md:12-14` 已经把“每个新增示例至少要关联一个自动化验证入口”写成维护约束。这里不是“示例很多”，而是 **public/staged example 被要求进入 regression gate**。
- `UnLua` 同时拥有很强的测试链和很强的教程链，但 **carrier 分离**。`Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Public/UnLuaTestCommon.h:172-212` 说明它有一整套 `IMPLEMENT_SIMPLE_AUTOMATION_TEST` / latent command 宏体系；`Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Tests/BindingTest.cpp:48-164`、`.../Private/Specs/*.spec.cpp` 也表明 API/issue regression 很完整。与此同时，`Reference/UnLua/README.md:35-54` 把用户导向 `Content/Script/Tutorials/*.lua`；`Reference/UnLua/Content/Script/Tutorials/12_CustomLoader.lua:1-38` 和 `Reference/UnLua/Source/TPSProject/TutorialBlueprintFunctionLibrary.cpp:46-105` 又把教程脚本与 sample helper 闭环起来。结论不能写成“UnLua 教程不受保护”，更准确的说法是：**测试插件和公开教程都存在，但不是同一棵 fixture tree**。
- `UnrealCSharp`、`puerts`、`sluaunreal` 在本轮分析范围内都没有定位到与上面三家等价的 UE 插件内自动化夹具树。本轮分别对 `Reference/UnrealCSharp/Source`、`Reference/puerts/unreal/Puerts/{Source,PuertsEditor}`、`Reference/sluaunreal/{Plugins/slua_unreal/Source,Source/democpp}` 执行 `rg -n "IMPLEMENT_SIMPLE_AUTOMATION_TEST|BEGIN_DEFINE_SPEC|[Fact]|[Test]|RunTest("`，结果均为 `NO_MATCH`。这只能支撑“**在当前分析范围内未定位到同层 fixture**”，不能外推成“整个上游仓库绝对没有测试”。

[1] 当前 `Angelscript` 把示例直接收编为 automation fixture，而不是只保留文档文本：

```cpp
// ============================================================================
// [1] 文件: Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleTestSupport.cpp
// 位置: 16-40
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleCoverageTests.cpp
// 位置: 40-62
// 文件: Script/Examples/README.md
// 位置: 12-14
// 说明: 当前 AS 的公开 / staged 示例并不是“看完就算”，而是被要求进入自动化验证
// ============================================================================
bool RunScriptExampleCompileTest(FAutomationTestBase& Test, const FScriptExampleSource& Example)
{
    const FString ExampleFileName = Example.ExampleFileName;
    const FString ModuleNameString = FPaths::GetBaseFilename(ExampleFileName);
    ...
    FAngelscriptEngine& Engine = AcquireCleanSharedCloneEngine(); // ★ 示例先进入干净测试引擎
}

UClass* CompileCoverageExample(
    FAutomationTestBase& Test, FAngelscriptEngine& Engine, FName ModuleName,
    const TCHAR* RelativePath, FName GeneratedClassName)
{
    const FString AbsolutePath = GetCoverageExampleAbsolutePath(RelativePath);
    FFileHelper::LoadFileToString(ScriptSource, *AbsolutePath);   // ★ file-backed `.as` 也直接成为测试输入
    return CompileScriptModule(Test, Engine, ModuleName, RelativePath, ScriptSource, GeneratedClassName);
}

- 当前波次以 `Documents/Plans/Plan_ScriptExamplesExpansion/Coverage/` 为真实交付源，避免与测试内联字符串再次分叉。
- 每个新增示例至少要关联一个自动化验证入口或在对应 Plan 中登记验证策略。
```

[2] `Hazelight` 与 `UnLua` 都很重视测试，但 fixture owner 不同：前者出生在脚本函数前缀，后者出生在独立 `UnLuaTestSuite` 宏体系

```cpp
// ============================================================================
// [2] 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Testing/DiscoverTests.cpp
// 位置: 166-202
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Public/UnLuaTestCommon.h
// 位置: 172-212
// 说明: HZ 把测试出生点放进 script module；UL 则把它放进独立测试插件宏
// ============================================================================
void DiscoverUnitTests(const FAngelscriptModuleDesc& Module, TMap<FString, FAngelscriptTestDesc>& UnitTestFunctions)
{
    OneArgFunctionFilter Filter = CreateOneArgFilter("Test_", "", "FUnitTest");
    DiscoverWithFilter(Module, UnitTestRunTestFunctions, Filter);
    RegisterSimpleFunctions(UnitTestRunTestFunctions, UnitTestFunctions);   // ★ 以脚本前缀命名约定发现测试
}

void DiscoverIntegrationTests(const FAngelscriptModuleDesc& Module, TMap<FString, FAngelscriptTestDesc>& IntegrationTestFunctions)
{
    OneArgFunctionFilter Filter = CreateOneArgFilter("IntegrationTest_", "", "FIntegrationTest");
    DiscoverWithFilter(Module, IntegrationTestRunTestFunctions, Filter);
    RegisterSimpleFunctions(IntegrationTestRunTestFunctions, IntegrationTestFunctions);
}

#define IMPLEMENT_UNLUA_LATENT_TEST(TestClass, PrettyName) \
IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass##_Runner, PrettyName, ...) \
bool TestClass##_Runner::RunTest(const FString& Parameters) \
{ \
    TestClass* TestInstance = new TestClass(); \
    ADD_LATENT_AUTOMATION_COMMAND(FUnLuaTestCommand_SetUpTest(TestInstance)); \
    ADD_LATENT_AUTOMATION_COMMAND(FUnLuaTestCommand_PerformTest(TestInstance)); \
    ADD_LATENT_AUTOMATION_COMMAND(FUnLuaTestCommand_TearDownTest(TestInstance)); \
    return true; \
} // ★ `UnLua` 把测试 carrier 独立到专门插件和宏体系
```

#### 对比矩阵（本轮新增观察点）

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 本轮在插件分析范围内定位到显式 automation fixture | Full | None | Full | None | None | Full |
| 公开示例 / 教程直接进入自动化回归 | Partial | None | Partial | None | None | Full |
| 测试 carrier 与公开 onboarding carrier 是同一棵树 | Partial | None | None | None | None | Partial |
| 有独立测试插件 / 模块作为回归宿主 | None | None | Full | None | None | Full |
| 差距性质 | 实现方式不同 | 本轮未定位 | 实现方式不同 | 本轮未定位 | 本轮未定位 | 当前最闭合 |

#### 小结与建议

- 当前 `Angelscript` 在这条线上真正领先的不是“测试数量更多”，而是**把公开 / staged 示例纳入了回归约束**。这属于 `实现质量差异`，不是单纯 `有无测试`。
- `P0` 建议继续保持 `Script/Examples` / companion `.as` 资产必须挂自动化的规则，不要退回“示例只写文档、不跑验证”。
- `P1` 可以吸收 `UnLua` 的一点长处：保留独立测试插件的同时，把 public tutorial 和 regression asset 之间的映射写得更直白，而不是只靠维护者理解。
- `P2` 对 `Hazelight` 血缘链来说，值得补的是“`Script-Examples` 与 discovered test function 的显式映射账本”；否则公开示例与测试仍然是两套平行资产。

### [D10] 公开 authoring surface 的 canonical source：教程脚本、typed artifact，还是脚手架模板

前文的 `D10` 已经比较过 onboarding owner 和 freshness。这里继续往下问：**一个首次接触插件的人，真正被仓库当成“权威入口”的东西是什么**。源码显示，6 个方案分成了 4 类：`file-backed tutorial`、`generated reference/typing`、`template scaffold`、`demo host project`。

#### 各插件实现概览

```
Canonical Authoring Surface
HZ : Script-Examples/*.as + generated Docs/angelscript/generated/*.hpp
AS : Script/Example_Actor.as + staged Script/Examples/README -> companion Coverage/*.as
UC : README -> external docs; FSolutionGenerator -> template scaffold
UL : README -> Tutorials/*.lua -> TPSProject helper C++
PU : DeclarationGenerator -> Typing/ue/*.d.ts ; PuertsEditor -> CodeAnalyze
SL : README snippets -> democpp host + LoadFileDelegate
```

#### 详细对比

- `Hazelight` 与当前 `Angelscript` 共享同一份 `Example_Actor.as` 血缘文本，`J:/UnrealEngine/UEAS2/Script-Examples/Examples/Example_Actor.as:1-40` 与 `Script/Example_Actor.as:1-40` 基本同源；差异不在脚本文本，而在**仓库如何声明它是公开真相**。`Hazelight` 仍把它放在稳定的 `Script-Examples/` 树下；当前仓库则在 `Script/Examples/README.md:3-14` 明确写出“正式公开入口还在 staging，当前波次真实交付源位于 companion `Coverage/` 目录”。所以这里不能写成“当前没有示例”，而应写成：**当前存在真实 `.as` 示例，但 public authority 仍然分裂在 `Script/Example_Actor.as`、companion `Coverage/*.as` 和测试内联字符串之间**。
- `UnLua` 的公开 authoring 面最线性。`Reference/UnLua/README.md:35-54` 直接把新手送入 `Content/Script/Tutorials/*.lua`；`Reference/UnLua/Content/Script/Tutorials/12_CustomLoader.lua:1-38` 又在脚本头部直接点名 `Source/TPSProject/TutorialBlueprintFunctionLibrary.cpp`；对应 helper 在 `Reference/UnLua/Source/TPSProject/TutorialBlueprintFunctionLibrary.cpp:46-105` 真实存在，并且真的绑定 `FUnLuaDelegates::CustomLoadLuaFile`。这条链是 **README -> 教程脚本 -> sample helper** 的单一真相。
- `puerts` 则把 authoring surface 做成 IDE-first。`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:417-451` 每次生成都会清旧 `ue.d.ts / ue_bp.d.ts`、同步项目 `Typing/` 和 `Content/JavaScript` 回插件，再把新的声明写回项目；`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:138-150` 直接起 `JsEnv` 执行 `PuertsEditor/CodeAnalyze`；`Reference/puerts/unreal/Puerts/PuertsEditor/CodeAnalyze.ts:65-70` 又把 TypeScript compiler host 指向插件内 `Content/JavaScript/PuertsEditor/node_modules/typescript/lib/tsc.js`。对 puerts 来说，**typed artifact 本身就是公开文档和作者入口**，而不是 sample project。
- `UnrealCSharp` 也不是以 sample script 为第一入口。`Reference/UnrealCSharp/README.md:26-30` 把公开文档放在站外 docs；`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FSolutionGenerator.cpp:9-40` 则从插件模板树生成 `CodeAnalysis`、`SourceGenerator` 等工程；`Reference/UnrealCSharp/Template/Game.props:16-26` 继续把 build/publish 脚手架固化到模板里。也就是说，`UnrealCSharp` 的公开 authoring surface 更像 **可生成的工作区模板**。
- `sluaunreal` 的公开 surface 更依赖 demo host。`Reference/sluaunreal/README.md:83-110` 只给出 inline Lua 用法片段；真实加载路径却在 `Reference/sluaunreal/Source/democpp/MyGameInstance.cpp:36-60`，由 demo 工程自己通过 `setLoadFileDelegate(...)` 决定 `Content/Lua/*.lua/.luac`。同时 `Reference/sluaunreal/README.md:365-373` 还提到 `TestPerf.lua`，而当前快照里只在 `Reference/sluaunreal/Content/Lua/Test.lua:48` 看到了被注释掉的 `require 'TestPerf'`。因此 slua 的问题也不能简单说成“文档差”，更准确是：**公开入口与真实 demo / 性能样例之间的 source-of-truth 有漂移风险**。

[1] 当前 `Angelscript` 与 `UnLua` 的差异，不是“有没有示例”，而是 public source-of-truth 是否已经收敛

```cpp
// ============================================================================
// [1] 文件: Script/Examples/README.md
// 位置: 3-14
// 文件: Reference/UnLua/README.md
// 位置: 35-54
// 文件: Reference/UnLua/Content/Script/Tutorials/12_CustomLoader.lua
// 位置: 1-38
// 文件: Reference/UnLua/Source/TPSProject/TutorialBlueprintFunctionLibrary.cpp
// 位置: 46-105
// 说明: 当前 AS 还在显式声明“正式入口待收敛”；UL 的 README/教程/helper 已是一条链
// ============================================================================
本目录保留为后续正式公开的 Script 示例入口。
...
- 当前波次以 `Documents/Plans/Plan_ScriptExamplesExpansion/Coverage/` 为真实交付源，避免与测试内联字符串再次分叉。

**注意**: 如果你是一位UE萌新，推荐使用更详细的图文版教学继续以下步骤。
* [12_CustomLoader](Content/Script/Tutorials/12_CustomLoader.lua) 自定义加载器   // ★ README 直接指向可运行脚本

-- 本示例C++源码：
-- Source\TPSProject\TutorialBlueprintFunctionLibrary.cpp                     -- ★ 教程脚本自己点名 helper
UE.UTutorialBlueprintFunctionLibrary.SetupCustomLoader(1)
Screen.Print(string.format("FromCustomLoader1:%s", require("Tutorials")))

bool CustomLoader1(...){ FFileHelper::LoadFileToArray(...); }                // ★ helper 真实存在
void UTutorialBlueprintFunctionLibrary::SetupCustomLoader(int Index)
{
    FUnLuaDelegates::CustomLoadLuaFile.BindStatic(CustomLoader1);            // ★ 教程入口与运行时代码同源
}
```

[2] `puerts` 与 `UnrealCSharp` 的 authoring surface 更像工具链制品，而不是教程树；`sluaunreal` 则依赖 demo host 提供真实入口

```cpp
// ============================================================================
// [2] 文件: Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp
// 位置: 417-451
// 文件: Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp
// 位置: 138-150
// 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FSolutionGenerator.cpp
// 位置: 9-40
// 文件: Reference/sluaunreal/Source/democpp/MyGameInstance.cpp
// 位置: 36-60
// 说明: PU/UC 把作者入口做成 typed artifact 或 workspace template；SL 则让 demo host 决定真实入口
// ============================================================================
PlatformFile.DeleteFile(*(PuertsBaseDir / TEXT("Typing/ue/ue.d.ts")));
PlatformFile.CopyDirectoryTree(*ProjectTypingDir, *(PuertsBaseDir / TEXT("Typing")), false);
PlatformFile.CopyDirectoryTree(*(ProjectContentDir / TEXT("JavaScript")), *(PuertsBaseDir / TEXT("Content") / TEXT("JavaScript")), true);
FFileHelper::SaveStringToFile(ToString(), *UEDeclarationFilePath);           // ★ typing 先是正式产物

JsEnv = MakeShared<FJsEnv>(std::make_shared<DefaultJSModuleLoader>(TEXT("JavaScript")), ...);
JsEnv->Start("PuertsEditor/CodeAnalyze");                                    // ★ editor 直接消费 typing / JS 根

void FSolutionGenerator::Generator()
{
    CopyTemplate(GetCodeAnalysisProjectPath(), ScriptPath / CODE_ANALYSIS_NAME / ...);
    CopyTemplate(GetSourceGeneratorPath(), ScriptPath / SOURCE_GENERATOR_NAME / ...); // ★ 作者入口是脚手架模板
}

state = new NS_SLUA::LuaState("SLuaMainState", this);
state->setLoadFileDelegate([](const char* fn, FString& filepath)->TArray<uint8> {
    path /= "Lua";
    path /= filename.Replace(TEXT("."), TEXT("/"));                           // ★ slua 的真实入口住在 demo host loader
});
```

#### 对比矩阵（本轮新增观察点）

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| public authoring surface 首选 file-backed script/tutorial | Full | None | Full | Partial | Partial | Partial |
| generated artifact 本身就是作者入口 | Partial | Full | Partial | Full | None | Partial |
| README / public entry 能直接跳到真实可运行资产 | Partial | Partial | Full | Partial | Partial | Partial |
| 仓库明确声明 source-of-truth 所在位置 | Partial | Partial | Full | Full | None | Full |
| 当前快照可见 source-of-truth 分裂 / 漂移风险 | None | Partial | None | Partial | Full | Full |

#### 小结与建议

- 当前 `Angelscript` 在 `D10` 上的关键问题已经不是“缺示例”，而是**示例 authority 尚未收敛**。这应判为 `实现质量差异`，不是 `没有实现`。
- `P0` 建议尽快把当前 `Script/Example_Actor.as`、companion `Coverage/*.as`、以及 `FScriptExampleSource` 内联示例收束到单一公开真相。`Script/Examples/README.md` 已经把问题写明，下一步应兑现，而不是继续容忍三处并存。
- `P1` 值得吸收 `UnLua` 的一点非常具体：让公开脚本文件直接点名配套 native helper 路径，这比单纯扩 README 更能降低第一次阅读成本。
- `P2` `puerts` / `UnrealCSharp` 提醒了另一个方向：如果未来 `Angelscript` 想强化 IDE 入口，可以把 docs/debugger 之外的一部分 authoring surface 做成可生成工件，但这不能替代 file-backed 示例。

### [D11] 交付物谁写、谁读、是否允许运行期 / 编辑期变更

前文已经比较过 deployment boundary、operator interface 和 self-verification。这一轮只盯一个更底层的 contract：**交付物是“build/publish 后的不可变产物”，还是“运行时 / commandlet 会继续读写的活动缓存”，抑或“根本由宿主 loader 决定”**。

#### 各插件实现概览

```
Artifact Writer / Reader
HZ : commandlet writes Binds.Cache(.Headers) -> runtime validates BuildIdentifier/DataGuid
AS : commandlet writes Binds.Cache -> runtime validates BuildIdentifier/DataGuid
UC : MSBuild Publish copies *.dll -> editor stages UFS -> AssemblyLoader reads bytes
UL : FLuaEnv loaders read .lua from PersistentDownloadDir / ProjectDir / custom loader
PU : DeclarationGenerator syncs Typing/JavaScript; DefaultJSModuleLoader reads JS source
SL : host demo provides LoadFileDelegate; core only stores delegate pointer
```

#### 详细对比

- 当前 `Angelscript` 的交付物是典型的 **plugin-defined mutable cache**。`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1466-1555` 启动时先读 `Binds.Cache`，必要时按 `RuntimeConfig.bRunningCommandlet` / `bSkipWriteBindDB` / `bWriteBindDB` 决定是否回写；随后再按构建配置选择 `PrecompiledScript_{Shipping|Test|Development}.Cache` 或 `PrecompiledScript.Cache`。`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp:2627-2649` 又用 `BuildIdentifier` 做 build validity，`.../AngelscriptEngine.cpp:1551-1555` 再用 `StaticJIT` 的 `PrecompiledDataGuid` 做第二层匹配。这条链说明它不是单纯“打包脚本文本”，而是**把 cache file 当正式交付契约**。
- `Hazelight` 与当前插件在 artifact family 上同谱系，但写入 guard 更细。`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/AngelscriptManager.cpp:419-427` 在 `!AS_USE_BIND_DB` 路径除了 `as-skip-write-bind-db` / `as-write-bind-db` 外，还额外识别 `cookworker`，避免 cook worker 写入 `Binds.Cache`；`.../Private/AngelscriptBindDatabase.cpp:37-94` 还会对 `Binds.Cache.Headers` 写失败打 cook 错误日志。当前仓库则把状态收束为 `bRunningCommandlet` + dump 可观测字段（`Dump/AngelscriptStateDump.cpp:394-400`），**后验诊断更好，但单写者 guard 粒度比 HZ 粗**。这属于 `实现质量差异`，不是 artifact 模型改变。
- `UnrealCSharp` 的交付物更接近 **immutable publish output**。`Reference/UnrealCSharp/Template/Game.props:16-26` 在 `Build -> Publish` 后复制 `$(PublishDir)*.dll`；`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:209-233` 再把 `PublishDirectory` 填进 `DirectoriesToAlwaysStageAsUFS`；运行期 `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/AssemblyLoader.cpp:6-20` 只按 assembly path 读取 `.dll` 字节。这里 writer 明确在 build/publish，runtime 不再改写 artifact。
- `UnLua` 的交付模型是 **source-first loader chain**。`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaFunctionLibrary.cpp:20-23` 把根固定到 `Content/Script/`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:557-638` 先尝试 `CustomLoadLuaFile` / `CustomLoaders`，再从 `ProjectPersistentDownloadDir` 和 `ProjectDir` 依 `package.path` 读 `.lua`。它支持下载目录覆盖、项目目录 fallback 和自定义 loader，但**没有像 AS 那样由插件自己定义的 cache artifact 账本**。
- `puerts` 则拆成两套 artifact：作者侧的 mutable typing / JS 根，以及运行期的 source module loader。`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:417-451` 会同步 `Typing/` 和 `Content/JavaScript` 回插件并重写项目 `ue.d.ts`；运行期 `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DefaultJSModuleLoader.cpp:92-141` 再从 require 目录、父目录和 `ProjectContentDir()/ScriptRoot` 读取 JS 文本。对 puerts 来说，**delivery 不等于 declaration**，而是 “JS source runtime + typing authoring artifact” 双轨制。
- `sluaunreal` 则把 delivery boundary 彻底交给宿主。`Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaState.h:110,189,264` 只声明 `LoadFileDelegate` 并保存函数指针；真正的文件路径和扩展名决策在 demo host `Reference/sluaunreal/Source/democpp/MyGameInstance.cpp:36-60`，由 `setLoadFileDelegate(...)` 自己拼 `Content/Lua/<module>.lua/.luac`。`Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaState.cpp:651-653` 甚至只是简单赋值。这说明 slua 的插件核心并不拥有 canonical artifact writer。

[1] `Hazelight` / 当前 `Angelscript` 的核心不是“有没有 cache”，而是 cache 的 writer guard 到哪一级

```cpp
// ============================================================================
// [1] 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 1466-1555
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp
// 位置: 2627-2649
// 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/AngelscriptManager.cpp
// 位置: 419-474
// 说明: 两边 artifact family 相同，但 HZ 对 cookworker 单写者的 guard 更细
// ============================================================================
FAngelscriptBindDatabase::Get().Load(GetScriptRootDirectory() / TEXT("Binds.Cache"), bGeneratePrecompiledData);
...
if ((RuntimeConfig.bRunningCommandlet && !bSkipWriteBindDB) || bForceWriteBindDB)
{
    FAngelscriptBindDatabase::Get().Save(GetScriptRootDirectory() / TEXT("Binds.Cache")); // ★ 当前 AS 用 commandlet 级 gate 控制写入
}
...
Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript_Development.Cache");
if (!PrecompiledData->IsValidForCurrentBuild()) { ... }                      // ★ 先看 BuildIdentifier
if (CompiledInfo != nullptr && CompiledInfo->PrecompiledDataGuid != PrecompiledData->DataGuid) { ... } // ★ 再看 StaticJIT DataGuid

int32 FAngelscriptPrecompiledData::GetCurrentBuildIdentifier()
{
    return 2; // UE_BUILD_DEVELOPMENT 例子，最终与 BuildIdentifier 对比
}

const bool bIsCookWorker = FParse::Param(FCommandLine::Get(), TEXT("cookworker"));
if ((IsRunningCommandlet() && !bSkipWriteBindDB && !bIsCookWorker) || bForceWriteBindDB)
{
    FAngelscriptBindDatabase::Get().Save(GetScriptRootDirectory() / TEXT("Binds.Cache")); // ★ HZ 额外跳过 cookworker 写入
}
```

[2] `UnrealCSharp` 的 artifact writer 在 build/publish；runtime 只是消费者

```cpp
// ============================================================================
// [2] 文件: Reference/UnrealCSharp/Template/Game.props
// 位置: 16-26
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp
// 位置: 209-233
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/AssemblyLoader.cpp
// 位置: 6-20
// 说明: UC 的 `.dll` 是 publish 产物；editor 负责 staging，runtime 只按路径读
// ============================================================================
<Target Name="AfterBuildPublish" AfterTargets="Build">
    <MSBuild Projects="$(ProjectPath)" Targets="Publish" ... />
</Target>
<Target Name="CopyDllsAfterPublish" AfterTargets="Publish">
    <PublishFiles Include="$(PublishDir)*.dll" />                            // ★ writer 在 publish 阶段
</Target>

if (!bIsExisted)
{
    ProjectPackagingSettings->DirectoriesToAlwaysStageAsUFS.Add({PublishDirectory}); // ★ editor 只负责把 publish 目录加入打包
}

TArray<uint8> UAssemblyLoader::Load(const FString& InAssemblyName)
{
    if (IFileManager::Get().FileExists(*File))
    {
        FFileHelper::LoadFileToArray(Data, *File);                           // ★ runtime 只是读 `.dll`
        return Data;
    }
}
```

[3] `UnLua` / `puerts` / `sluaunreal` 都偏 source loader，但 owner 分别落在插件 loader、authoring toolchain 与宿主 delegate

```cpp
// ============================================================================
// [3] 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp
// 位置: 557-638
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DefaultJSModuleLoader.cpp
// 位置: 92-141
// 文件: Reference/sluaunreal/Source/democpp/MyGameInstance.cpp
// 位置: 36-60
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaState.cpp
// 位置: 651-653
// 说明: UL/PU/SL 都读源码，但 canonical writer/loader owner 并不相同
// ============================================================================
if (FUnLuaDelegates::CustomLoadLuaFile.IsBound()) { ... }                    // ★ 先让宿主自定义 loader 接管
for (auto& Pattern : Patterns)
{
    const auto PathWithPersistentDir = FPaths::Combine(FPaths::ProjectPersistentDownloadDir(), Pattern);
    if (FFileHelper::LoadFileToArray(Data, *FullPath, FILEREAD_Silent))
        return LoadIt();                                                     // ★ 再读下载目录 / 项目目录源码
}

return SearchModuleInDir(FPaths::ProjectContentDir() / ScriptRoot, RequiredModule, Path, AbsolutePath);
... 
FileHandle = PlatformFile.OpenRead(*Path);                                   // ★ puerts 运行期直接读 JS 文本

state->setLoadFileDelegate([](const char* fn, FString& filepath)->TArray<uint8> {
    path /= "Lua";
    path /= filename.Replace(TEXT("."), TEXT("/"));
    FFileHelper::LoadFileToArray(Content, *fullPath);                        // ★ slua 的真实 delivery owner 在 host lambda
});

void LuaState::setLoadFileDelegate(LoadFileDelegate func)
{
    loadFileDelegate = func;                                                 // ★ 插件核心只保存 delegate，不拥有 canonical path 规则
}
```

#### 对比矩阵（本轮新增观察点）

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 插件自定义 cache / binary artifact 作为正式交付物 | Full | Full | None | Partial | None | Full |
| runtime 会继续读取源码文件而不是只读生成产物 | Partial | None | Full | Full | Full | Partial |
| build / publish 阶段明确是 artifact writer | Partial | Full | None | Partial | None | Partial |
| plugin core 自己实现 build-id / guid 级合法性校验 | Full | Partial | None | None | None | Full |
| canonical loader owner 在宿主侧而不是插件核心 | None | Partial | Partial | None | Full | None |

#### 小结与建议

- 当前 `Angelscript` 的 `D11` 差异核心不是“有没有部署链”，而是**部署链属于 plugin-owned mutable cache 协议**。这和 `UnrealCSharp` 的 immutable publish output、`UnLua`/`slua` 的 source-first host loader 是三种完全不同的 contract。
- `P0` 建议保持 `BuildIdentifier + DataGuid` 双重校验，不要为简化部署而退回纯 source-first 模式。对 AS 这种带 precompiled/static JIT 的系统，这是必要的交付自校验。
- `P1` 值得直接吸收 `Hazelight` 的一点：恢复更精细的 `cookworker` 单写者 guard，以及 `.Headers` 写失败的即时日志。当前仓库的可观测性很强，但写入 guard 粒度确实退了一步。
- `P2` 如果未来要改善最终用户体验，可以借鉴 `UnrealCSharp` 的“publish 目录自动 staging”做法，但不要混淆为 artifact 模型本身迁移；`Angelscript` 的核心仍然是 cache + build-aware validation，而不是 DLL publish。

---

## 深化分析 (2026-04-09 02:40:00)

### [D3/D8] latent / coroutine 的 owner：VM-native suspend，还是 metadata / proxy callback

前文已经比较过 `WorldContext`、`out/ref ABI` 和 delegate carrier。这一轮只补一个此前没有单独拆开的 contract：**遇到 UE latent / async API 时，真正负责“挂起-恢复”的 owner 在哪一层**。同样都能“调用异步功能”，但 `UnLua` / `sluaunreal` 直接把 VM coroutine 接到 `FLatentActionInfo`；`UnrealCSharp` / `puerts` 更像声明层或 helper 层把 latent 语义编码出去；`Hazelight` / 当前 `Angelscript` 则主要暴露 UE 原生 callback / proxy surface，而不是把 gameplay suspend 做成脚本 VM contract。

#### 各插件实现概览

```
Latent / Async Continuation Ownership
HZ : FLatentActionInfo value bind + ScriptCallable async helpers        // UE callback/proxy surface
AS : FLatentActionInfo value bind + BlueprintCallable async helpers     // UE callback/proxy surface
UC : generated attributes (Latent/LatentInfo/NeedsLatentFixup/Proxy)   // metadata-first latent contract
UL : FunctionDesc injects FLatentActionInfo -> lua_yield -> manager     // VM-native coroutine suspend
PU : JS decorators + Promise batch wrapper                              // helper-level async contract
SL : accelerator injects FLatentActionInfo -> lua_yield -> delegate     // VM-native coroutine suspend
```

#### 详细对比

##### 子维度 1：continuation identity 最终挂在哪

- `UnLua` 和 `sluaunreal` 把 continuation identity 明确挂在 VM 线程上。前者在 `FFunctionDesc::CallUE()` 里把 `ThreadRef` 写进 `FLatentActionInfo`，随后 `Class_CallLatentFunction()` 直接 `lua_yield()`；后者在 `LuaFunctionAccelerator` 里用 `ULatentDelegate::getThreadRef()` 生成 thread ref，再由 `LuaObject` 返回 `lua_yield()`。这不是“帮用户少传一个参数”，而是 **VM thread 自己拥有 resume token**。
- `UnrealCSharp` 与 `puerts` 没把 continuation token 做成 VM suspend owner，而是先做成 declaration / metadata contract。`UnrealCSharp` 的 `FDynamicGeneratorCore` 明确把 `Latent / LatentInfo / NeedsLatentFixup / LatentCallbackTarget / ExposedAsyncProxy` 都列进 generated attribute 白名单；`puerts` 则把同名 metadata 装成 JS decorator，并另外提供 `Promise` / `batchCall` helper。这类方案的强项是 authoring surface 清楚，但 suspend 本身不在脚本 VM 核心。
- `Hazelight` 与当前 `Angelscript` 在 gameplay runtime 里更接近 **UE callback/proxy-first**。两边都直接把 `FLatentActionInfo` 当作可绑定值类型暴露出来，并且公开表面更多是 `AsyncSaveGameToSlot`、`WaitGameplayEventToActor` 之类 UE native callback / proxy wrapper。当前仓库确实还存在 `Plugins/Angelscript/Source/AngelscriptRuntime/Testing/IntegrationTest.cpp:213-230,470-492` 的 latent automation DSL，但那是 test runtime contract，不是 gameplay script suspend owner。

##### 子维度 2：恢复入口到底归谁

- `UnLua` 的恢复入口落在 `UUnLuaManager::OnLatentActionCompleted()` / `UUnLuaLatentAction::OnLegacyCallback()` 这条 VM manager 语义链；`sluaunreal` 则落在 `ULatentDelegate::OnLatentCallback()` -> `LuaState::resumeThread()`。两边的共同点是：**恢复入口是脚本运行时自己的对象**，不是单纯一个 Blueprint proxy 返回值。
- `UnrealCSharp` / `puerts` 的恢复入口更像运行时 helper 或 generated surface 的后续效果。`ExposedAsyncProxyAttribute` 与 JS `Promise` wrapper 都说明两边愿意把“等待结果”表达成 target-language authoring 友好的 facade，但源码层没看到与 `lua_yield` 同位的 VM suspend primitive。
- `Hazelight` / 当前 `Angelscript` 的恢复责任主要仍交给 UE 自己的 callback target / async proxy。`Bind_FLatentActionInfo.cpp` 暴露的是 `ExecutionFunction` 和 `CallbackTarget` 字段，`GameplayLibrary` / `UAngelscriptAbilityAsyncLibrary` 暴露的是 delegate / proxy 风格入口，这使它们更像“脚本可调用的 UE async API”，而不是“脚本 VM 自带 suspend/resume 模型”。

[1] `Hazelight` / 当前 `Angelscript` 公开的是 `FLatentActionInfo` 值类型和 UE 原生 async helper；runtime 表面是 callback / proxy-first

```cpp
// ============================================================================
// [1] 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FLatentActionInfo.cpp
// 位置: 5-24
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/GameplayLibrary.h
// 位置: 36-64
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilityAsyncLibrary.h
// 位置: 18-56
// 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/FunctionLibraries/GameplayLibrary.h
// 位置: 36-63
// 说明: 两边都把 latent surface 暴露成 UE callback/proxy API；当前仓库只是把 `ScriptCallable` 收缩成 `BlueprintCallable`
// ============================================================================
auto FLatentActionInfo_ = FAngelscriptBinds::ExistingClass("FLatentActionInfo");
FLatentActionInfo_.Constructor("void f(int32 InLinkage, int32 InUUID, const FName InFunctionName, UObject InCallbackTarget)", ...);
FLatentActionInfo_.Property("FName ExecutionFunction", &FLatentActionInfo::ExecutionFunction);
FLatentActionInfo_.Property("UObject unresolved_object CallbackTarget", &FLatentActionInfo::CallbackTarget);
// ★ 暴露的是 latent token 值对象本身，不是 VM suspend primitive

UFUNCTION(BlueprintCallable)
static void AsyncSaveGameToSlot(..., FAsyncSaveGameToSlotDynamicDelegate Delegate)
{
    UGameplayStatics::AsyncSaveGameToSlot(..., FAsyncSaveGameToSlotDelegate::CreateLambda(...));
}
// ★ 当前 AS 公开的是 delegate 风格 async helper

UFUNCTION(BlueprintCallable, Category = "Ability|Tasks")
static UAbilityAsync_WaitGameplayEvent* WaitGameplayEventToActor(...)
{
    return UAbilityAsync_WaitGameplayEvent::WaitGameplayEventToActor(...);
}
// ★ 公开的是 UE async proxy 返回值

UFUNCTION(ScriptCallable)
static void AsyncLoadGameFromSlot(..., FAsyncLoadGameFromSlotDynamicDelegate Delegate)
{
    UGameplayStatics::AsyncLoadGameFromSlot(..., FAsyncLoadGameFromSlotDelegate::CreateLambda(...));
}
// ★ HZ 同样是 callback helper 路线，只是 authoring 标记仍保留 `ScriptCallable`
```

[2] `UnrealCSharp` / `puerts` 先把 latent 语义抬成 generated metadata / decorator，再由 helper surface 提供 async facade

```cpp
// ============================================================================
// [2] 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGeneratorCore.cpp
// 位置: 1078-1088, 1210-1212, 1267-1268
// 文件: Reference/UnrealCSharp/Script/UE/Dynamic/Function/LatentAttribute.cs
// 位置: 5-9
// 文件: Reference/UnrealCSharp/Script/UE/Dynamic/Function/LatentInfoAttribute.cs
// 位置: 5-14
// 文件: Reference/UnrealCSharp/Script/UE/Dynamic/Property/NeedsLatentFixupAttribute.cs
// 位置: 5-14
// 文件: Reference/UnrealCSharp/Script/UE/Dynamic/Property/LatentCallbackTargetAttribute.cs
// 位置: 5-9
// 文件: Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/uelazyload.js
// 位置: 808-809, 934-935, 1013-1014
// 文件: Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/callable.js
// 位置: 152-168, 245-276
// 说明: UC/PU 都先把 latent 语义投影成声明或 decorator，再提供 helper 级 async facade
// ============================================================================
ReflectionRegistry.GetExposedAsyncProxyAttributeClass(),
ReflectionRegistry.GetNeedsLatentFixupAttributeClass(),
ReflectionRegistry.GetLatentCallbackTargetAttributeClass(),
ReflectionRegistry.GetLatentAttributeClass(),
ReflectionRegistry.GetLatentInfoAttributeClass();
// ★ latent 先进入 generated metadata 白名单

[AttributeUsage(AttributeTargets.Method)]
public class LatentAttribute : Attribute { private string Value { get; set; } = "true"; }
[AttributeUsage(AttributeTargets.Method)]
public class LatentInfoAttribute : Attribute { public LatentInfoAttribute(string InValue) { Value = InValue; } }
// ★ C# 层看到的是 attribute contract

"Latent": MetaDataInst,
"LatentInfo": MetaDataInst,
"NeedsLatentFixup": MetaDataInst,
"LatentCallbackTarget": MetaDataInst,
// ★ puerts 把同名语义做成 JS decorator

cfg[name] = isAsync ? function() {
    return new Promise(function(resolve, reject) {
        pendingAsyncCall.push({args: ..., resolve: ..., reject: reject});
    });
} : function() { ... };
// ★ async facade 最终落在 Promise + batchCall helper，而不是 VM coroutine suspend
```

[3] `UnLua` / `sluaunreal` 把 latent continuation 绑定到 VM thread，再直接 `lua_yield`

```cpp
// ============================================================================
// [3] 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp
// 位置: 53-66, 286-301
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaCore.cpp
// 位置: 962-970, 1287-1306
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaLatentAction.cpp
// 位置: 58-62, 90-106
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaFunctionAccelerator.cpp
// 位置: 23-29, 252-263, 375-386
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaObject.cpp
// 位置: 891-899
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LatentDelegate.cpp
// 位置: 27-45
// 说明: UL/SL 都让 VM thread 直接拥有 latent resume token
// ============================================================================
else if (LatentPropertyIndex == INDEX_NONE && Property->GetFName() == NAME_LatentInfo)
{
    LatentPropertyIndex = Index;
}
...
FLatentActionInfo LatentActionInfo(ThreadRef, GetTypeHash(FGuid::NewGuid()), TEXT("OnLatentActionCompleted"), (Env.GetManager()));
Property->CopyValue(ContainerPtr, &LatentActionInfo);
// ★ UnLua 把 ThreadRef 写进 latent token

if (Function->IsLatentFunction())
{
    lua_pushcclosure(L, Class_CallLatentFunction, 1);
}
...
int32 NumResults = Function->CallUE(L, NumParams, &ThreadRef);
return lua_yield(L, NumResults);
// ★ 真正挂起的是 Lua coroutine

void UUnLuaLatentAction::OnLegacyCallback(int32 InLinkage)
{
    Callback.Unbind();
    Env->ResumeThread(InLinkage);
}
// ★ 恢复入口回到 VM manager

if (checkerInfo.bLatent)
{
    ULatentDelegate* latentObj = LuaObject::getLatentDelegate(mainThread);
    int threadRef = latentObj->getThreadRef(L);
    FLatentActionInfo LatentActionInfo(threadRef, GetTypeHash(FGuid::NewGuid()), *ULatentDelegate::NAME_LatentCallback, latentObj);
    prop->CopySingleValue(..., &LatentActionInfo);
    isLatentFunction = true;
}
...
if (isLatentFunction)
    return lua_yield(L, outParamCount);
// ★ slua 同样把挂起点做进 Lua VM

void ULatentDelegate::OnLatentCallback(int32 threadRef)
{
    luaState->resumeThread(threadRef);
}
// ★ 恢复入口是插件自己的 delegate owner
```

#### 对比矩阵（本轮新增观察点）

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| gameplay runtime 具备 VM-native suspend / resume primitive | None | None | Full | None | Full | None |
| latent 语义先进入 generated metadata / decorator | None | Full | Partial | Full | Partial | None |
| async public surface 主要是 callback / proxy / Promise helper | Full | Full | Partial | Full | Partial | Full |
| continuation identity 直接绑定脚本线程 / coroutine ref | None | None | Full | None | Full | None |

#### 小结与建议

- 当前 `Angelscript` 在这一维度上应判为 **实现方式不同**，不是“没有异步能力”。它已经有 `FLatentActionInfo` bind、save-game async helper、GAS async proxy wrapper，只是 **没有把 gameplay latent 做成 VM-native suspend contract**。
- `P0` 如果目标是补齐 `UnLua` / `sluaunreal` 级别的 coroutine latent 体验，就不能继续只加 `GameplayLibrary` / proxy wrapper，必须补一条独立的“script thread handle -> latent callback target -> resume”语义链。
- `P1` 无论是否引入 coroutine，都值得先吸收 `UnrealCSharp` / `puerts` 的一部分做法：把 `Latent`、`LatentInfo`、`NeedsLatentFixup`、`LatentCallbackTarget` 这些语义投影到 IDE / docs / generated artifact，避免当前 AS 用户只能靠经验分辨“这是 callback-first 还是 latent-first API”。
- `P2` 当前仓库的 `IntegrationTest.cpp` 已经有 latent automation DSL，但不要把它误判成 gameplay runtime 已经具备同等级 suspend 语义。这两条 contract 现在是分离的，而且分离本身是合理设计。

### [D4] reload safe point 的 owner：compile transaction、editor barrier，还是 object state

前文已经比较过 `reload authority`、文件事件语义和状态保持。这一轮只盯一个更底层的问题：**当 reload“现在不安全”时，谁有权说 `not now`，以及旧状态会以什么方式活到下一个 safe point**。这条线把 6 个方案明显分成了 5 类：`Hazelight` / 当前 `Angelscript` 是 compile transaction owner；`UnrealCSharp` 是 editor compile / PIE barrier owner；`UnLua` 是 operator mode + save hook owner；`puerts` 更像 immediate source reload + PIE 布局禁改；`sluaunreal` 则把 safe point 收缩到 object `PostLoad` / `PreBeginPIE`。

#### 各插件实现概览

```
Reload Safe Point Owner
HZ : SoftReloadOnly / FullReload / queue-later                // compile transaction
AS : SoftReloadOnly / FullReload / queue-later                // compile transaction
UC : wait compiler before PIE + suppress compile in PIE       // editor barrier
UL : Manual/Auto/Never + package-save suspend/resume          // operator + save hook
PU : watcher reloads watched JS immediately; PIE forbids layout mutation
SL : object hook checks RF_NeedPostLoad / PreBeginPIE preview // object-state safe point
```

#### 详细对比

##### 子维度 1：谁决定“这次不能现在做”

- `Hazelight` / 当前 `Angelscript` 的决定权在 compile transaction。两边都先根据 `!GIsEditor || HasGameWorld()` 把本次检查分流到 `SoftReloadOnly` 或 `FullReload`；再由 `EReloadRequirement::FullReloadSuggested / FullReloadRequired` 决定是降级 soft reload、保旧代码，还是把 full reload 排队到以后。`not now` 不是 watcher 的布尔开关，而是 compile result 的一部分。
- `UnrealCSharp` 把 `not now` 放到 editor barrier。`OnPreBeginPIE()` 会阻塞等待编译线程清空；`OnApplicationActivationStateChanged()` 与 `OnAssetChanged()` 又都明确要求 `!bIsPIEPlaying && !bIsGenerating` 才触发 compile。这里没有 `FullReloadSuggested` 这类等级枚举，但 safe point 非常明确。
- `UnLua` 的 `not now` 更偏 operator 语义。设置里先有 `Manual / Auto / Never`；目录变更时只有 `Auto` 模式才自动 `HotReload()`；而 PIE 保存 package 时又会 `SuspendOverrides()` / `ResumeOverrides()`。这说明它把安全窗口交给“用户选择的热更模式 + 保存钩子”，而不是统一 transaction state machine。
- `puerts` 的 `not now` 最薄。对已加载 JS 文件，watcher 几乎是立刻 `ReloadSource()`；但如果要改 `TypeScriptGeneratedClass` 的 class layout，则 `PEBlueprintAsset` 会直接在 PIE 中拒绝。它不是没有 safe point，而是 **只有结构性改动才有强闸门**。
- `sluaunreal` 的 safe point 更靠对象状态。`LuaOverrider::tryHook()` 只在 game thread 且对象不再 `RF_NeedPostLoad` 时立即接管；否则进入 `asyncLoadedObjects` 等待下一次 `onAsyncLoadingFlushUpdate()`。另外 editor preview simulate 会在 `PreBeginPIE` 被停止。safe point owner 不是文件事务，而是对象生命周期。

##### 子维度 2：被阻塞后的旧状态怎么活下来

- `Hazelight` / 当前 `Angelscript` 会把文件放进 `QueuedFullReloadFiles`，必要时还保留旧模块继续运行。这意味着“稍后再 full reload”是正式账本，不是单纯日志提示。
- `UnrealCSharp` 更像把“新变更先压住”。它在进入 PIE 前等编译结束，在 PIE / generator 中禁止再触发 compile，因此旧状态主要靠 **不让新编译进入** 来存活。
- `UnLua` 的策略是“保存期间先拆，再恢复”。PIE package save 前 suspend overrides，保存后 resume。这里旧状态的生存载体不是 reload queue，而是 override map 本身。
- `puerts` 对脚本文本更激进，对 class layout 更保守。已加载 JS 改了就尝试重载；类布局一旦牵涉生成 Blueprint / GeneratedClass，则直接在 PIE 报错并返回。
- `sluaunreal` 则让对象留在 `asyncLoadedObjects` 或 preview 模拟体系里，等对象 flags 满足条件再 hook。旧状态的生存单位是对象，而不是文件批次。

[1] `Hazelight` / 当前 `Angelscript` 把 safe point 正式编码成 compile type + reload requirement，并把 blocked full reload 排队到以后

```cpp
// ============================================================================
// [1] 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h
// 位置: 23-28
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 2819-2828, 3942-3978, 4168-4186
// 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/AngelscriptManager.cpp
// 位置: 1570-1578, 2625-2660, 2848-2866
// 说明: HZ/AS 把 reload safe point 做成 compile transaction，而不是 watcher 的立即动作
// ============================================================================
enum EReloadRequirement
{
    SoftReload,
    FullReloadSuggested,
    FullReloadRequired,
    Error,
};
// ★ severity 先被编码进状态枚举

if (!GIsEditor || HasGameWorld())
{
    CheckForHotReload(ECompileType::SoftReloadOnly);
}
else
{
    CheckForHotReload(ECompileType::FullReload);
}
// ★ PIE / game world 时只允许 soft reload

case FAngelscriptClassGenerator::EReloadRequirement::FullReloadSuggested:
    if (CompileType == ECompileType::SoftReloadOnly)
    {
        UE_LOG(Angelscript, Warning, TEXT("... A Full Reload will be queued for after PIE ends."));
        bWasFullyHandled = false;
        SwapInModules(...);
        ClassGenerator.PerformSoftReload();
    }
// ★ 建议 full reload 时，PIE 内先 soft reload，再把 full reload 延后

if (Result == ECompileResult::ErrorNeedFullReload || Result == ECompileResult::PartiallyHandled)
{
    for (const auto& RepeatFile : AllCompiledFiles)
        QueuedFullReloadFiles.Add(RepeatFile);
}
// ★ blocked full reload 不是丢掉，而是进入正式队列
```

[2] `UnrealCSharp` 的 safe point 是 editor barrier：先等编译线程清空，再在 PIE / generator 之外放行 compile

```cpp
// ============================================================================
// [2] 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp
// 位置: 144-161, 308-315, 337-365, 373-389
// 文件: Reference/UnrealCSharp/Source/Compiler/Private/FCSharpCompilerRunnable.cpp
// 位置: 166-193
// 说明: UC 不做 soft/full reload 梯度，而是把 safe point 放到 editor compile barrier
// ============================================================================
void FEditorListener::OnPreBeginPIE(const bool bIsSimulating)
{
    bIsPIEPlaying = true;
    while (FCSharpCompiler::Get().IsCompiling())
    {
        FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
    }
}
// ★ 进入 PIE 前先把编译排空

if (!bIsPIEPlaying && !bIsGenerating)
{
    FCSharpCompiler::Get().Compile(FileChanges);
}
// ★ 应用激活后只有不在 PIE / generator 才放行文件编译

if (!bIsPIEPlaying && !bIsGenerating)
{
    FGeneratorCore::BeginGenerator(false);
    ...
    FCSharpCompiler::Get().Compile();
    FGeneratorCore::EndGenerator(false);
}
// ★ 资产生成同样要求 editor barrier 打开

bIsCompiling = true;
Compile();
const auto Task = FFunctionGraphTask::CreateAndDispatchWhenReady(..., ENamedThreads::GameThread);
FTaskGraphInterface::Get().WaitUntilTaskCompletes(Task);
bIsCompiling = false;
// ★ 编译线程最终还是要在 game thread barrier 上闭环
```

[3] `UnLua` / `puerts` / `sluaunreal` 的 safe point 更分散：用户模式、结构性禁改和对象状态各自为 owner

```cpp
// ============================================================================
// [3] 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorSettings.h
// 位置: 31-47
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorFunctionLibrary.cpp
// 位置: 112-118
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp
// 位置: 155-182
// 文件: Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp
// 位置: 122-137
// 文件: Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp
// 位置: 52-64, 119-123
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaOverrider.cpp
// 位置: 662-690, 976-1004
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaSimulate.cpp
// 位置: 24-37
// 说明: UL/PU/SL 的 safe point 不在统一 compile transaction，而是分别落到用户模式、PIE 结构闸门和对象状态
// ============================================================================
enum class EHotReloadMode : uint8
{
    Manual,
    Auto,
    Never
};
if (Settings.HotReloadMode != EHotReloadMode::Auto)
    return;
UUnLuaFunctionLibrary::HotReload();
// ★ UnLua 先让用户决定 hot reload 是否自动发生

if (!GEditor || !GEditor->PlayWorld)
    return;
...
ULuaFunction::SuspendOverrides(...);
...
ULuaFunction::ResumeOverrides(...);
// ★ PIE 保存 package 时先 suspend，再 resume

if (JsEnv.IsValid())
{
    if (FFileHelper::LoadFileToArray(Source, *InPath))
    {
        JsEnv->ReloadSource(InPath, ...);
    }
}
// ★ puerts 对已加载脚本文本是 immediate reload

if (IsPlaying())
{
    UE_LOG(PuertsEditorModule, Error, TEXT("change the layout of class[%s] in PIE mode is forbiden!"), ...);
    return false;
}
// ★ 但结构性改动在 PIE 中直接禁掉

if (IsInGameThread() && !bPostLoad)
{
    if (!obj->HasAnyFlags(RF_NeedPostLoad) || bHookImmediate)
    {
        ...
        bindOverrideFuncs(obj, cls);
        return true;
    }
}
...
if (obj && !obj->HasAnyFlags(RF_NeedPostLoad) && !obj->HasAnyFlags(RF_NeedInitialization))
{
    bindOverrideFuncs(obj, cls);
}
// ★ slua 的 safe point 是对象 lifecycle，而不是文件事务

PIEHandle = FEditorDelegates::PreBeginPIE.AddRaw(this, &LuaSimulate::OnPreBeginPIE);
void LuaSimulate::OnPreBeginPIE(const bool bIsSimulating)
{
    StopSimulateLua();
}
// ★ 进入 PIE 前还会主动停掉 editor preview simulate
```

#### 对比矩阵（本轮新增观察点）

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| reload safe point 被正式编码成显式 severity / transaction 枚举 | Full | None | None | None | None | Full |
| PIE / game world 内允许降级路径并把 full reload 延后 | Full | Partial | None | None | None | Full |
| 进入 PIE 前会先等待编译 / 生成 barrier 清空 | None | Full | None | None | Partial | None |
| editor 设置显式决定 hot reload 是 `Manual / Auto / Never` | None | None | Full | None | None | None |
| PIE 中只对结构性 class layout 改动设置硬闸门 | None | None | None | Full | None | None |
| safe point 真正落在对象 `PostLoad` / `NeedInitialization` 状态 | None | None | None | None | Full | None |

#### 小结与建议

- 当前 `Angelscript` 在 `D4` 上新增确认的关键强项，是 **safe point owner 仍然牢牢握在 compile transaction 里**。`SoftReloadOnly`、`FullReloadSuggested`、`FullReloadRequired` 决定的不只是“怎么 reload”，而是“什么时候允许结构变更进入系统”。这应判为 `实现质量差异`，不是 `实现方式不同`。
- `P0` 不建议把当前 AS 退回 `puerts` 式的 immediate reload 或 `UnLua` 式的单层 operator mode。那会直接损失掉“PIE 内降级 soft reload + 之后自动补 full reload”的质量优势。
- `P1` 值得直接吸收 `UnLua` 的一点，是在现有 transaction owner 之上补一个用户可见的 policy surface，例如 `Manual / Auto / Never`。注意是**叠加**在现有机制上，不是拿来替代 compile transaction。
- `P2` 如果未来当前 AS 的 editor 工具链继续增多，可以借鉴 `UnrealCSharp` 的做法，在进入 PIE 前更明确地 drain 后台任务或生成器 barrier；但这只是 operator experience 增强，不改变当前 `reload owner` 结论。

### [D3] 异步加载完成后的对象接管 safe point：flush fence、generated-class rebind，还是无插件自有队列

前文已经比较过 generated class carrier、GC ledger 和 hot reload transaction。这一轮只补一个更隐蔽的接缝：**对象在 async loading / postload 之后，谁负责把脚本绑定或 override 补上**。这条线把 6 个方案分成三类：`UnrealCSharp` / `UnLua` / `sluaunreal` 都有插件自有候选队列，并把 `OnAsyncLoadingFlushUpdate` 当成正式 drain fence；`puerts` 不走全局 flush，而是走 `GeneratedObjects + NeedReBind`；`Hazelight` / 当前 `Angelscript` 则只在 threaded initialize loop 里 `Broadcast()` flush update，本轮检索 `Plugins/Angelscript/Source` 与 `UEAS2` 插件源码里的 `OnAsyncLoadingFlushUpdate.Add*` 未见 consumer，这里的判断因此带推断性质。

#### 各插件实现概览

```
Post-Async-Load Script Attachment
HZ : threaded init waits -> Broadcast(OnAsyncLoadingFlushUpdate)        // progress pump
AS : threaded init waits -> Broadcast(OnAsyncLoadingFlushUpdate)        // progress pump
UC : NotifyUObjectCreated -> AsyncLoadingObjectArray -> Bind           // environment-owned queue
UL : TryBind in async thread -> Candidates -> TryBind on flush         // env-owned queue
PU : NeedReBind / GeneratedObjects -> NotifyReBind -> MakeSureInject   // generated-class rebind
SL : tryHook -> asyncLoadedObjects -> bindOverrideFuncs on flush       // override-owned queue
```

#### 详细对比

##### 子维度 1：谁维护 async-loaded candidate ledger

- `UnrealCSharp` 的 owner 最完整。`NotifyUObjectCreated()` 会把 CDO 或非 game-thread 创建的对象塞进 `AsyncLoadingObjectArray`；`OnAsyncLoadingFlushUpdate()` 再根据 `RF_NeedPostLoad`、`AsyncLoading`、`Async` 等 flags 过滤，最后调用 `BindClassDefaultObject()` / `Bind<true>()` 和 `ConstructorObject()`。也就是说，**flush fence 之后不仅 bind，还继续补 constructor side effect**。
- `UnLua` 更轻。`TryBind()` 一旦发现在 async loading thread，就只把实现了 `UUnLuaInterface` 或命中 dynamic binding 的对象放进 `Candidates`；`OnAsyncLoadingFlushUpdate()` 再重新 `TryBind()`。它没有 UC 那么多后置 constructor 动作，但队列 owner 同样清晰。
- `sluaunreal` 的 owner 更偏 override。`tryHook()` 如果对象还 `RF_NeedPostLoad` 或不满足立即接管条件，就进 `asyncLoadedObjects`；flush 时再 `bindOverrideFuncs()`。队列里的 payload 不是“待绑定类描述符”，而是“待改写 override 的对象”。
- `puerts` 走另一条路。它没把 `OnAsyncLoadingFlushUpdate` 作为统一候选队列，而是让 `UTypeScriptGeneratedClass::NotifyRebind()` 和 `FJsEnvImpl::NotifyReBind()` 围绕 `GeneratedObjects`、`NeedReBind`、`MakeSureInject()` 做 generated-class rebind。这里的 safe point owner 是 generated class/object graph，而不是 engine-wide flush delegate。
- `Hazelight` / 当前 `Angelscript` 这一层最保守。当前能看到的是 threaded initialize 等待循环里手动 `Broadcast(OnAsyncLoadingFlushUpdate)`，用来让 game thread 在初始化期间继续泵 task graph；没有看到同位的插件自有候选队列 consumer。更准确的判断是：**AS/HZ 目前没有把 async-loaded object attachment 提升成插件内一等账本**。

##### 子维度 2：flush fence 真正做什么

- `UnrealCSharp` 把 flush fence 当“bind + constructor finalize”阶段。
- `UnLua` 把 flush fence 当“延迟执行 `TryBind`”阶段。
- `sluaunreal` 把 flush fence 当“override hook 的补接管”阶段。
- `puerts` 并不共享这个 fence；它自己的 rebind 是在 `GeneratedObjects` 集合上遍历注入。
- `Hazelight` / 当前 `Angelscript` 的 flush 更像 progress pump。这里的 `Broadcast()` 不是“消费队列并 attach script behavior”，而是 threaded initialize wait-loop 的一部分。

[1] `Hazelight` / 当前 `Angelscript` 对 `OnAsyncLoadingFlushUpdate` 的可见使用仍停在 threaded initialize progress pump

```cpp
// ============================================================================
// [1] 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 825-848
// 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/AngelscriptManager.cpp
// 位置: 170-184
// 说明: 当前 AS/HZ 都会在 threaded initialize 等待期间广播 flush update；本轮对两边源码检索 `OnAsyncLoadingFlushUpdate.Add*` 未见插件自有 consumer
// ============================================================================
if (ShouldInitializeThreaded())
{
    volatile bool bInitializationDone = false;
    AsyncTask(ENamedThreads::AnyHiPriThreadHiPriTask, [&] {
        Initialize_AnyThread();
        bInitializationDone = true;
    });

    while (!bInitializationDone)
    {
        FCoreDelegates::OnAsyncLoadingFlushUpdate.Broadcast();
        FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
        FPlatformProcess::Sleep(0.002f);
    }
}
// ★ 这里可见的是 progress pump，不是 async-loaded object candidate queue
```

[2] `UnrealCSharp` / `UnLua` / `sluaunreal` 都把 flush delegate 当成正式 drain fence，只是队列 owner 不同

```cpp
// ============================================================================
// [2] 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp
// 位置: 87-88, 244-266, 305-385
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Public/Environment/FCSharpEnvironment.h
// 位置: 310-315
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp
// 位置: 348-356, 669-718, 721-730
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/LuaEnv.h
// 位置: 153-165, 181
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaOverrider.cpp
// 位置: 539-542, 701-703, 976-1013
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaOverrider.h
// 位置: 118-149
// 说明: UC/UL/SL 都有插件自有候选队列，并明确用 flush delegate 把它们 drain 掉
// ============================================================================
OnAsyncLoadingFlushUpdateHandle = FCoreDelegates::OnAsyncLoadingFlushUpdate.AddRaw(
    this, &FCSharpEnvironment::OnAsyncLoadingFlushUpdate);
...
AsyncLoadingObjectArray.Add(InObject);
...
if (Object->HasAnyFlags(RF_NeedPostLoad) || Object->HasAnyInternalFlags(...AsyncLoading...) || ...)
{
    continue;
}
PendingBindObjects.Add(Object);
...
Bind<true>(PendingBindObject);
FoundClass->ConstructorObject(FoundMonoObject);
// ★ UC：flush 后不只 bind，还补 constructor finalize

if (IsInAsyncLoadingThread())
{
    FScopeLock Lock(&CandidatesLock);
    Candidates.AddUnique(Object);
    return false;
}
...
if (Object->HasAnyFlags(RF_NeedPostLoad) || Object->HasAnyInternalFlags(AsyncObjectFlags) || ...)
{
    continue;
}
LocalCandidates.Add(Object);
...
TryBind(Object);
// ★ UL：flush fence 只是把延迟 bind 再执行一遍

asyncLoadingFlushUpdateHandle = FCoreDelegates::OnAsyncLoadingFlushUpdate.AddRaw(this, &LuaOverrider::onAsyncLoadingFlushUpdate);
...
asyncLoadedObjects.Add(AsyncLoadedObject{ (UObject*)obj });
...
if (obj && !obj->HasAnyFlags(RF_NeedPostLoad) && !obj->HasAnyFlags(RF_NeedInitialization))
{
    bindOverrideFuncs(obj, cls);
}
// ★ SL：flush 后做的是 override hook 接管
```

[3] `puerts` 不用 global flush fence，而是用 generated class / object set 自己完成 rebind

```cpp
// ============================================================================
// [3] 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/TypeScriptGeneratedClass.cpp
// 位置: 77-99, 107-119
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp
// 位置: 2325-2366
// 说明: PU 的 async/rebind safe point 在 generated class/object graph，而不是 `OnAsyncLoadingFlushUpdate`
// ============================================================================
void UTypeScriptGeneratedClass::NotifyRebind(UClass* Class)
{
    if (TsClass->NeedReBind && TsClass->DynamicInvoker.IsValid())
    {
        TsClass->NeedReBind = false;
        ...
        CachedClass->DynamicInvoker.Pin()->NotifyReBind(CachedClass);
    }
}
// ★ rebind 入口是 generated class 自己

void FJsEnvImpl::NotifyReBind(UTypeScriptGeneratedClass* Class)
{
    MakeSureInject(Class, false, false);
    FinishInjection(Class);
    for (TWeakObjectPtr<UObject>& Iter : Class->GeneratedObjects)
    {
        auto Object = Iter.Get();
        if (!Object || ObjectMap.Find(Object))
            continue;
        if (Object->GetClass()->GetName().StartsWith(TEXT("REINST_")))
            continue;
        __USE(FindOrAdd(..., Object->GetClass(), Object, true));
        ...
        MakeSureInject(ClassMayNeedReBind, false, false);
    }
}
// ★ owner 是 generated objects 集合和 inject pipeline，不是 global flush queue
```

#### 对比矩阵（本轮新增观察点）

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 插件自有 async-loaded candidate queue | None | Full | Full | None | Full | None |
| `OnAsyncLoadingFlushUpdate` 真正承担 bind / override drain | None | Full | Full | None | Full | None |
| 同一 flush fence 会显式检查 `RF_NeedPostLoad` / `AsyncLoading` / `NeedInitialization` | None | Full | Full | None | Full | None |
| 不用 global flush，而是 generated class / object set 自管 rebind | None | None | None | Full | None | None |
| 当前可见 flush 语义主要是 threaded initialize progress pump | Full | None | None | None | None | Full |

#### 小结与建议

- 这一轮新增确认的结论是：当前 `Angelscript` 在“async-loaded UObject 自动接管”这条线上，**并没有像 UC / UL / SL 那样建立插件自有候选账本**。这应判为 `实现方式不同`，不是立即的缺陷，因为当前 AS 的主 owner 仍是 compile transaction，而不是 object hook。
- `P0` 如果未来当前 AS 要支持更多“异步加载后自动附着脚本行为”的场景，单靠 `Broadcast(OnAsyncLoadingFlushUpdate)` 不够，必须补一条显式 candidate ledger，并定义何时从 `RF_NeedPostLoad` / `AsyncLoading` 状态转为可接管。
- `P1` 最值得优先吸收的是 `UnLua` / `UnrealCSharp` 的最小模型：先收一份轻量 `Candidates` / `AsyncLoadingObjectArray`，再在 flush fence 后执行 bind。不要一开始就跳到 `sluaunreal` 那种更重的 override queue。
- `P2` `puerts` 的 `GeneratedObjects + NeedReBind` 模型只有在 generated class 真正成为当前 AS 的主 carrier 时才值得参考。只在部分 editor workflow 上引入它，会导致 owner 再次分裂。

---

## 深化分析 (2026-04-09 06:42:26)

本轮不重复前文已经展开过的 `reload authority`、`headless tooling` 和 `runner / artifact owner`。这里只补 3 个更底层、但对插件长期交付边界更关键的横向问题：`D1` 看“为了接入脚本，宿主必须被改多少”；`D6` 看“类型分析和 IDE 生成到底跑在哪个进程里”；`D9` 看“验证代码有没有真正进入 build graph，而不是只停留在 runtime 私有角落”。

### [D1] 宿主侵入预算：engine patch、plugin-side build tool，还是 external wrapper

这一维度前文已经比较过“模块怎么拆”，本轮只补一个更底层的问题：**脚本系统为了接入 UE，究竟把改动压在 engine、本插件的 build tool、还是 UE 进程之外的外部工具上**。这决定的不只是架构美观，而是升级成本、移植成本和交付边界。

#### 各插件实现概览

```
Host Intrusion Budget
HZ : Engine UHT patch + Engine editor patch + Engine plugin          // 直接改 engine 源码
AS : Plugin modules + plugin-side UBT/UHT tool                       // 插件自带 build-time 扩展
UC : UHT-capable plugin + Program modules                            // 生成器挂在插件模块图里
UL : UHT-capable plugin + Program module                             // 生成器仍在 UE 插件生态内
PU : UHT-capable plugin + Editor/Program modules                     // 编辑器模块内再嵌语言服务
SL : Runtime plugin + external lua-wrapper + config.json             // 关键导出能力在 UE 外部
```

#### 详细对比

##### 子维度 1：反射 / 代码生成 hook 点归谁拥有

- `Hazelight` 的 owner 最激进。它不是“插件调用 UHT”，而是直接把脚本语义注入 `EpicGames.UHT.Types.UhtProperty`，并让 engine editor 原生代码识别 `bIsScriptClass`。这意味着脚本接入点属于 engine 自身，而不是 plugin extension。
- 当前 `Angelscript` 刚好走相反方向：`Angelscript.uplugin` 只公开 `Runtime / Editor / Test` 三个交付模块，`UHTTool` 不进入模块图；真正的接入点在单独的 `.ubtplugin.csproj` 和 `[UhtExporter]` exporter 上。也就是说，**脚本接入能力仍属于插件，但它挂在 build graph，而不是 runtime module graph**。
- `UnrealCSharp` / `UnLua` / `puerts` 都把接入能力保留在 UE 插件生态内，但表达方式更“显式”：`uplugin` 直接暴露 `Program` 或 generator/editor module。相比当前 AS，这三家更容易被使用者一眼看见“生成器在哪”，代价是交付面和模块图更重。
- `sluaunreal` 则进一步把关键导出能力放到 UE 外部。`slua_unreal.uplugin` 只保留 runtime/editor 模块，真正的静态导出依赖 `Tools/lua-wrapper` 和 `config.json`。这不是“没有实现生成”，而是**owner 不在 UE build graph 里**。

[1] 当前 `Angelscript` 把 UHT 扩展放在 plugin-side build tool，而不是暴露到 `.uplugin` 模块图

```csharp
// ============================================================================
// [1] 文件: Plugins/Angelscript/Angelscript.uplugin
// 位置: 18-34
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptUHTTool.ubtplugin.csproj
// 位置: 1-53
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs
// 位置: 12-27
// 说明: 当前 AS 的 runtime/editor/test 交付面保持在 `.uplugin` 内；真正的 codegen hook 通过 UBT/UHT 扩展挂到 build 侧
// ============================================================================
"Modules": [
    { "Name": "AngelscriptRuntime", "Type": "Runtime" },
    { "Name": "AngelscriptEditor",  "Type": "Editor"  },
    { "Name": "AngelscriptTest",    "Type": "Editor"  }
]
// ★ `.uplugin` 没有暴露 UHT generator/program module

<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <OutputPath>..\..\Binaries\DotNET\UnrealBuildTool\Plugins\AngelscriptUHTTool\</OutputPath>
  </PropertyGroup>
  <ItemGroup>
    <Reference Include="EpicGames.UHT">
      <HintPath>$(EngineDir)\Binaries\DotNET\UnrealBuildTool\EpicGames.UHT.dll</HintPath>
    </Reference>
  </ItemGroup>
</Project>
// ★ build hook 放在 UBT/UHT 插件输出目录，而不是 runtime 模块

[UnrealHeaderTool]
[UhtExporter(
    Name = "AngelscriptFunctionTable",
    Options = UhtExporterOptions.Default | UhtExporterOptions.CompileOutput,
    ModuleName = "AngelscriptRuntime")]
private static void Export(IUhtExportFactory factory)
// ★ exporter 在 UHT 进程内执行，但 owner 仍然是插件侧
```

[2] `Hazelight` 把脚本语义直接压进 engine UHT 类型系统和 editor 原生逻辑

```cpp
// ============================================================================
// [2] 文件: J:/UnrealEngine/UEAS2/Engine/Source/Programs/Shared/EpicGames.UHT/Types/UhtProperty.cs
// 位置: 759-764
// 文件: J:/UnrealEngine/UEAS2/Engine/Source/Editor/AIGraph/Private/AIGraphTypes.cpp
// 位置: 425-431
// 说明: UEAS2 的 hook 点属于 engine 自己；这不是插件“调用” engine，而是 engine 本身知道 script property / script class
// ============================================================================
// AS FIX(LV): Angelscript-specific property flags
public EAngelscriptPropertyFlags AngelscriptPropertyFlags { get; set; }
// END AS FIX
// ★ UHT 核心类型直接新增 Angelscript 字段

for (TObjectIterator<UClass> It; It; ++It)
{
    UClass* TestClass = *It;
    const bool bValidASNode = TestClass->bIsScriptClass && !TestClass->HasAnyClassFlags(CLASS_NewerVersionExists);
    if ((TestClass->HasAnyClassFlags(CLASS_Native) || bValidASNode) && TestClass->IsChildOf(RootNodeClass))
// ★ editor 原生逻辑直接接受 script class
```

##### 子维度 2：宿主升级成本落在哪一层

- `Hazelight` 的好处是“原生感”最强，很多 editor/runtime 组件不需要再绕插件 adapter；代价也最直接，就是 engine fork 成本最高。`UhtProperty.cs:759-764` 和 `AIGraphTypes.cpp:425-431` 只是本轮命中的两个代表点，不是全部改动面。
- 当前 `Angelscript` 的 budget 更克制。它确实耦合 UBT/UHT 内部接口，因为 `.ubtplugin.csproj` 直接引用 `EpicGames.UHT.dll`；但这份耦合被限定在插件自己的 build extension 层，而没有蔓延到 engine fork。这属于 `实现方式不同`，不是“能力弱于 Hazelight”。
- `UnrealCSharp` / `UnLua` / `puerts` 的升级压力主要落在插件模块边界和 editor program 边界。它们不需要 engine fork，但 generator 入口本身会成为插件公开模块图的一部分。
- `sluaunreal` 的升级压力落在外部 wrapper 的环境适配上。`Tools/README.md:3-5,35-42` 和 `Tools/config.json:65-77` 说明它依赖 `.NET + libclang + include_path/preprocess` 配置，这不是 engine fork，但会把宿主环境漂移风险推给工具配置。

##### 子维度 3：交付面是否把“生成器”暴露给使用者

- 当前 `Angelscript` 的一个新确认优势，是**运行时交付面很干净**。使用者从 `.uplugin` 看到的是 runtime/editor/test，不会被迫理解 generator/program module。
- `UnrealCSharp` / `UnLua` / `puerts` 更显式，优点是入口清楚，缺点是模块图自带“构建工具产品面”。例如 `UnrealCSharp.uplugin:17-53` 直接公开 `ScriptCodeGenerator`、`Compiler`、`SourceCodeGenerator`；`UnLua.uplugin:23-40` 公开 `UnLuaDefaultParamCollector`；`Puerts.uplugin:14-47` 公开 `DeclarationGenerator` 和 `ParamDefaultValueMetas`。
- `sluaunreal` 的交付面也很“干净”，但原因不是隔离得更好，而是关键导出能力根本不在 UE 插件图内。这里应判为 `实现方式不同`，不是同类优势。

#### 对比矩阵

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 需要 engine source patch 才能成立核心接入 | Full | None | None | None | None | None |
| 核心接入能力保留在插件自有 build extension | None | None | None | None | None | Full |
| `.uplugin` 直接暴露 generator / Program module | None | Full | Full | Full | None | None |
| 关键导出能力主要依赖 UE 外部 wrapper / config | None | Partial | None | None | Full | None |
| runtime/editor 交付模块图保持聚焦，不混入生成器 | Partial | None | None | None | Partial | Full |

#### 小结与建议

- 本轮新增确认的结论是：当前 `Angelscript` 在 `D1` 上的最独特位置，不是“模块更少”，而是**把宿主侵入预算压在 plugin-side build tool，而不是 engine fork，也不是 `.uplugin` 模块图**。这应判为 `实现质量差异`，因为它同时保住了插件边界和 UHT 进程内能力。
- `P0` 不建议为追求“更原生”去复制 `Hazelight` 的 engine patch 路线，除非未来明确把“维护 engine fork”升级为项目边界。否则迁移成本会立刻从插件级问题变成引擎分支问题。
- `P1` 当前 AS 值得继续坚持的，是 `AngelscriptUHTTool` 这条隔离层。即使未来扩展更多 generator，也应优先放在 build extension 层，而不是把 `Program` 模块继续塞进 `.uplugin` 主图。
- `P2` 如果未来必须引入 UE 外部工具，只应让它承担可选的补充导出或 IDE 后处理，不应让核心反射接入退化成 `sluaunreal` 式的外部 wrapper owner。

### [D6] 工具链执行拓扑：in-process exporter、spawned proc，还是 embedded language service

前文已经比较过“谁能生成声明”和“有没有 commandlet”。这一轮只补一个更底层的问题：**生成器到底跑在哪个进程里，以及这个进程边界决定了什么样的增量模型、故障边界和环境漂移风险**。

#### 各插件实现概览

```
Toolchain Execution Topology
HZ : UHT process -> engine-patched type system                       // engine/UHT in-process
AS : UHT process -> AngelscriptFunctionTableExporter                 // plugin/UHT in-process
UL : UE Editor/Commandlet -> IntelliSense generator                  // editor in-process
PU : UE Editor -> JsEnv->Start(CodeAnalyze) -> TS language service   // embedded long-lived service
UC : UE Editor -> SyncProcess/CreateProc -> external exe/dotnet      // spawned external process
SL : external lua-wrapper(.NET + libclang) -> generated C++          // UE 外部独立工具
```

#### 详细对比

##### 子维度 1：主计算发生在哪个地址空间

- `Hazelight` 与当前 `Angelscript` 最接近，二者都把核心类型处理压在 UHT 进程内。区别只在 owner：`Hazelight` 通过 engine patch 让 UHT 原生知道 `AngelscriptPropertyFlags`；当前 AS 则通过 `[UhtExporter]` 在 plugin extension 层运行 exporter。
- `UnLua` 的 IntelliSense 生成主路径明显在 UE editor / commandlet 进程里。`UnLuaIntelliSenseCommandlet::Main()` 直接读取导出类型后生成文件，而 `FUnLuaIntelliSenseGenerator::Initialize()` 则订阅 `AssetRegistry` 事件，把增量更新也保留在 editor 内存里。
- `puerts` 更进一步，不只是“在 editor 里生成”，而是让 `PuertsEditorModule` 启动 `JsEnv` 后加载 `PuertsEditor/CodeAnalyze`，并在 TypeScript 侧长期持有 `LanguageService`。这是一种 `embedded language service` 拓扑。
- `UnrealCSharp` 和 `sluaunreal` 把主计算挪到 UE 进程外。区别是 `UnrealCSharp` 仍由 UE 侧主动 `CreateProc()` 驱动，并会 `dotnet build`；`sluaunreal` 则更像一套独立 wrapper/tooling 环境。

[1] 当前 `Angelscript` 的核心生成拓扑是 plugin/UHT in-process

```csharp
// ============================================================================
// [1] 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs
// 位置: 12-27
// 说明: 当前 AS 的 function table exporter 直接以 UHT exporter 形式注册
// ============================================================================
[UnrealHeaderTool]
internal static class AngelscriptFunctionTableExporter
{
    [UhtExporter(
        Name = "AngelscriptFunctionTable",
        Description = "Exports Angelscript function table data",
        Options = UhtExporterOptions.Default | UhtExporterOptions.CompileOutput,
        CppFilters = ["AS_FunctionTable_*.cpp"],
        ModuleName = "AngelscriptRuntime")]
    private static void Export(IUhtExportFactory factory)
// ★ 这说明生成在 UHT 进程内完成，而不是 editor 启动外部工具
```

[2] `UnrealCSharp` 的主生成拓扑明确跨出 UE 进程

```cpp
// ============================================================================
// [2] 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FCodeAnalysis.cpp
// 位置: 26-49, 68-86
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp
// 位置: 1458-1496
// 说明: UE 侧只是 orchestration owner；真正分析和编译通过外部 exe / dotnet 进程完成
// ============================================================================
const auto AnalysisParam = FString::Printf(TEXT("true \"%s\" \"%s\""), ...);
FUnrealCSharpFunctionLibrary::SyncProcess(Program, AnalysisParam, ...);

const auto CompileParam = FString::Printf(TEXT("build \"%s\" --nologo -c Debug"), ...);
FUnrealCSharpFunctionLibrary::SyncProcess(CompileTool, CompileParam, ...);
// ★ 这里已经不是进程内 exporter，而是 spawn 外部执行体

auto ProcessHandle = FPlatformProcess::CreateProc(
    *InURL,
    *InParms,
    false,
    true,
    true,
    &OutProcessID,
    1,
    nullptr,
    WritePipe,
    ReadPipe);
while (ProcessHandle.IsValid() && FPlatformProcess::IsApplicationRunning(OutProcessID))
{
    FPlatformProcess::Sleep(0.01f);
    Result.Append(FPlatformProcess::ReadPipe(ReadPipe));
}
// ★ UE 进程负责等待、读 pipe、收集输出，但不承担核心分析计算
```

[3] `UnLua` / `puerts` / `sluaunreal` 分别代表 editor in-process、embedded service、external wrapper 三种不同 topology

```cpp
// ============================================================================
// [3] 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp
// 位置: 29-114
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp
// 位置: 42-53
// 文件: Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp
// 位置: 138-150
// 文件: Reference/puerts/unreal/Puerts/PuertsEditor/CodeAnalyze.ts
// 位置: 343-353
// 文件: Reference/sluaunreal/Tools/README.md
// 位置: 3-5, 35-42
// 说明: 这三家都不是 UHT exporter，但 topology 完全不同
// ============================================================================
int32 UUnLuaIntelliSenseCommandlet::Main(const FString &Params)
{
    const auto ExportedReflectedClasses = UnLua::GetExportedReflectedClasses();
    ...
    if (ParamsMap.Contains(BPKey) && ParamsMap[BPKey] == TEXT("1"))
    {
        auto Generator = FUnLuaIntelliSenseGenerator::Get();
        Generator->Initialize();
        Generator->UpdateAll();
    }
}
// ★ UL：commandlet / editor 进程内直接生成

OutputDir = IPluginManager::Get().FindPlugin("UnLua")->GetBaseDir() + "/Intermediate/IntelliSense";
AssetRegistryModule.Get().OnAssetAdded().AddRaw(this, &FUnLuaIntelliSenseGenerator::OnAssetAdded);
// ★ UL：增量更新仍由 editor 内部事件驱动

JsEnv->Start("PuertsEditor/CodeAnalyze");
let service = ts.createLanguageService(servicesHost, ts.createDocumentRegistry());
return service.getProgram();
// ★ PU：在 editor 内部常驻一个 TS language service

lua-wrapper 是 slua-unreal 的静态代码导出工具，该工具采用 c# 开发，依赖 ... libclang 5.0.0（32位）
lua-wrapper runs on windows and mac platforms ... please modify the config*.json file ...
// ★ SL：关键能力不在 UE 进程，而在外部 wrapper + 配置环境
```

##### 子维度 2：增量复用和状态保留的 owner 是谁

- 当前 `Angelscript` / `Hazelight` 的 owner 是 UHT session。好处是类型世界最一致，生成结果与 header 扫描天然同一拍；代价是它们更适合“结构真相”的导出，不天然负责 editor 级语义服务。
- `UnLua` 的 owner 是 editor 事件系统。`UnLuaIntelliSenseGenerator.cpp:47-53` 说明它把 `AssetRegistry` 变化直接转成增量生成触发，这种反馈速度通常优于重新跑一次 UHT，但类型真相要以 editor 当前资产状态为准。
- `puerts` 的 owner 是常驻 `LanguageService`。它不仅复用 AST/Program，还在失败时重建 service，`CodeAnalyze.ts:345-353` 已经明写了“文件读取偶发失败时重建 language service”。
- `UnrealCSharp` 的 owner 最弱，因为跨进程意味着每次分析都要重新建执行环境；`sluaunreal` 更极端，owner 直接是外部工具配置与 `include_path/preprocess`。

##### 子维度 3：故障边界和环境漂移风险

- `UnrealCSharp` / `sluaunreal` 的优点是把崩溃、依赖冲突、语言运行时问题隔离到 UE 进程外；缺点是最容易受路径、SDK、外部 runtime 版本影响。
- `puerts` / `UnLua` 的优点是反馈快、状态热；缺点是 editor 内部常驻服务需要自己做恢复、重建和脏状态治理。
- 当前 `Angelscript` 的 UHT in-process 路线在“结构真相”这一层最稳定，但如果未来 IDE 能力继续扩张，仅靠 UHT exporter 不足以支撑更细粒度的 editor 交互。这不是缺陷，而是 topology 边界。

#### 对比矩阵

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 核心类型导出主路径运行在 UHT 进程内 | Full | None | None | None | None | Full |
| 核心 IDE / 声明生成主路径运行在 UE editor / commandlet 内 | Partial | Partial | Full | Full | None | None |
| 通过 OS 级外部进程执行关键分析 / 编译 | None | Full | None | None | Full | None |
| 存在常驻的 in-memory language service / semantic cache | None | None | Partial | Full | None | None |
| 明显依赖外部工具配置文件或 include/preprocess 环境 | None | Partial | None | None | Full | None |

#### 小结与建议

- 本轮新增确认的结论是：当前 `Angelscript` 在 `D6` 上最该守住的，不是“有没有 IDE 支持”，而是**核心类型导出仍然是 UHT in-process**。这让它在结构一致性上更接近 `Hazelight`，明显优于 `UnrealCSharp` / `sluaunreal` 的外部进程路径。
- `P0` 不建议把当前 AS 的核心类型导出迁到外部进程。那会把今天相对稳定的 UHT 真相层，换成更多路径、runtime、SDK 和配置漂移问题。
- `P1` 最值得吸收的是 `UnLua` 的 editor-side incremental owner：在现有 UHT exporter 之上，再加一层由 `AssetRegistry` 或脚本文件 watcher 驱动的轻量 IDE artifact 更新，而不是拿它替代 UHT。
- `P2` 如果未来要做更强的语义补全 / 跳转，可以参考 `puerts` 的 `embedded language service` 设计，但应把它建立在 AS 已有 generated metadata 之上，让常驻服务消费既有导出，而不是重新成为另一套真相来源。

### [D9] 验证载体的 build participation：独立 test module、runtime-private testing，还是公开图中缺席

前文已经比较过 runner、报告和覆盖率。本轮只补一个容易被忽略的底层问题：**测试代码到底住在哪一层，以及它是否真的进入插件自己的 build graph / UHT 参与面**。这决定了验证能力是“产品的一部分”，还是“散落在运行时私有实现里的习惯性代码”。

#### 各插件实现概览

```
Validation Carrier Placement
HZ : Angelscript.uplugin -> no test module -> Runtime/Private/Testing       // 测试逻辑混在 runtime 私有层
AS : Angelscript.uplugin -> AngelscriptTest(Editor module)                  // 显式 test carrier
UL : UnLuaTestSuite.uplugin -> UnLuaTestSuite(Runtime module + content)     // 独立 test plugin
PU : Puerts.uplugin -> no visible test carrier                              // 公开插件图中未见测试载体
SL : slua_unreal.uplugin -> no visible test carrier                         // 公开插件图中未见测试载体
```

#### 详细对比

##### 子维度 1：测试 carrier 是否对 build graph 可见

- 当前 `Angelscript` 的可见性最好。`Angelscript.uplugin:18-34` 明确公开 `AngelscriptTest` 模块；`AngelscriptTest.Build.cs:6-52` 又表明它显式依赖 `AngelscriptRuntime`，在 editor 目标下再接入 `CQTest`、`UnrealEd`、`AngelscriptEditor`。这意味着验证载体不是“顺手写在 runtime 里”，而是正式 build participant。
- `UnLua` 走的是独立 test plugin 路线。`UnLuaTestSuite.uplugin:1-28` 显示它是单独插件，`CanContainContent = true`，并且 `CanBeUsedWithUnrealHeaderTool = true`；`UnLuaTestSuite.Build.cs:17-64` 说明它是一个真正可编译、可预编译的 module。
- `Hazelight` 不是“没有测试”，而是**测试发现逻辑混在 runtime 私有目录**。`Angelscript.uplugin:18-34` 公开的只有 `AngelscriptCode / AngelscriptEditor / AngelscriptLoader`，没有 test module；而 `DiscoverTests.cpp:166-203` 把 `Test_ / ComplexUnitTest_ / IntegrationTest_` 发现逻辑写在 `AngelscriptCode/Private/Testing/`。
- `puerts` 与 `sluaunreal` 在本轮扫描到的公开插件描述符里，都未见独立 test carrier。这应判为“**公开 build participation 缺席**”，不是断言“仓库绝对没有测试”。

[1] 当前 `Angelscript` 的测试载体是插件图里显式可见的独立 module

```cpp
// ============================================================================
// [1] 文件: Plugins/Angelscript/Angelscript.uplugin
// 位置: 18-34
// 文件: Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs
// 位置: 6-52
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptCoreExecutionTests.cpp
// 位置: 10-23
// 说明: 当前 AS 的验证代码并未回流到 runtime 私有层，而是有独立 build carrier 和实际 automation test 实现
// ============================================================================
"Modules": [
    { "Name": "AngelscriptRuntime", "Type": "Runtime" },
    { "Name": "AngelscriptEditor",  "Type": "Editor"  },
    { "Name": "AngelscriptTest",    "Type": "Editor"  }
]
// ★ test module 对插件图公开可见

public class AngelscriptTest : ModuleRules
{
    public AngelscriptTest(ReadOnlyTargetRules Target) : base(Target)
    {
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "Engine", "AngelscriptRuntime" });
        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.AddRange(new string[] { "CQTest", "UnrealEd", "AngelscriptEditor" });
        }
    }
}
// ★ test carrier 明确参与 UBT 依赖图

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAngelscriptCoreCreateCompileExecuteTest,
    "Angelscript.TestModule.Angelscript.Core.CreateCompileExecute",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
// ★ 这里不是“只有 module，没有测试”，而是 module 下有正式 automation test
```

[2] `Hazelight` 的测试发现逻辑存在，但 carrier 混在 runtime private

```cpp
// ============================================================================
// [2] 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Angelscript.uplugin
// 位置: 18-34
// 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Testing/DiscoverTests.cpp
// 位置: 166-203
// 说明: HZ 有 testing 逻辑，但没有公开的 test module；验证载体与 runtime 实现层混住
// ============================================================================
"Modules": [
    { "Name": "AngelscriptCode",   "Type": "Runtime" },
    { "Name": "AngelscriptEditor", "Type": "Editor"  },
    { "Name": "AngelscriptLoader", "Type": "Runtime" }
]
// ★ 公开插件图中没有独立 test carrier

void DiscoverUnitTests(const FAngelscriptModuleDesc &Module, ...)
{
    OneArgFunctionFilter Filter = CreateOneArgFilter("Test_", "", "FUnitTest");
    DiscoverWithFilter(Module, UnitTestRunTestFunctions, Filter);
    ...
}
void DiscoverIntegrationTests(const FAngelscriptModuleDesc& Module, ...)
{
    OneArgFunctionFilter Filter = CreateOneArgFilter("IntegrationTest_", "", "FIntegrationTest");
    DiscoverWithFilter(Module, IntegrationTestRunTestFunctions, Filter);
    ...
}
// ★ 测试发现是 runtime 私有实现的一部分，不是单独 build participant
```

[3] `UnLua` 把验证载体单独做成 test plugin，并允许 content / UHT 一起参与

```cpp
// ============================================================================
// [3] 文件: Reference/UnLua/Plugins/UnLuaTestSuite/UnLuaTestSuite.uplugin
// 位置: 1-28
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/UnLuaTestSuite.Build.cs
// 位置: 17-64
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Tests/BindingTest.cpp
// 位置: 48-70
// 说明: UL 的验证载体不只是“有测试文件”，而是单独 test plugin + module + content 的完整参与面
// ============================================================================
"FriendlyName": "UnLuaTestSuite",
"CanContainContent": true,
"CanBeUsedWithUnrealHeaderTool": true,
"Modules": [
    { "Name": "UnLuaTestSuite", "Type": "Runtime", "LoadingPhase": "Default" }
]
// ★ test plugin 自己就是公开的 build / UHT 参与者

public class UnLuaTestSuite : ModuleRules
{
    PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "Slate" });
    PrivateDependencyModuleNames.AddRange(new[] { "Lua", "UnLua", "UMG" });
    PrecompileForTargets = PrecompileTargetsType.Any;
}
// ★ test carrier 有自己的 ModuleRules 和预编译策略

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnLuaTest_StaticBinding, TEXT("UnLua.API.Binding.Static ..."), ...);
const char* Chunk1 = R"(
    local ActorClass = UE.UClass.Load('/UnLuaTestSuite/Tests/Binding/BP_UnLuaTestActor_StaticBinding...')
    G_Actor = World:SpawnActor(ActorClass)
)";
// ★ 测试还直接消费 test plugin 自带内容资产
```

##### 子维度 2：测试逻辑与 runtime/editor 的耦合形态

- 当前 `Angelscript` 是“**同插件内独立 test module**”。这比 `Hazelight` 的 runtime-private testing 更清晰，因为 owner 明确、依赖关系明确、构建参与面也明确。
- `UnLua` 是“**独立 test plugin**”。隔离度更高，适合携带测试专用资产和脚本路径；代价是分发和启用边界更复杂。
- `Hazelight` 是“**runtime-private testing**”。优势是就地访问内部实现最方便；代价是测试 carrier 难以与产品 carrier 解耦，也更难在描述符层面对外表达“这是验证面”。
- `puerts` / `sluaunreal` 至少在公开插件图上是“**缺席**”。这里要明确区分：本轮证据只能证明“公开 build carrier 不可见”，不能反推出“仓库没有任何验证手段”。

##### 子维度 3：UHT / content 是否进入验证参与面

- 当前 `AngelscriptTest` 是 editor module，适合 C++ automation 与 editor 能力验证，但它不像 `UnLuaTestSuite` 那样在独立 test plugin 层携带明确的 content boundary。
- `UnLuaTestSuite` 的价值在于：测试不仅有 module，还有内容路径、BP 资产、UHT 参与面。这种 carrier 更适合验证“脚本 + asset + Blueprint”完整链路。
- `Hazelight` 当前这条线更偏 runtime 层面的 discover/dispatch，不像 `UnLua` 那样把测试资产和 test carrier 一起产品化。

#### 对比矩阵

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 插件描述符显式暴露 test module / test plugin | None | None | Full | None | None | Full |
| 测试载体有独立 `ModuleRules` 并正式参与 UBT | None | None | Full | None | None | Full |
| 测试逻辑主要混在 runtime private 目录 | Full | None | None | None | None | None |
| test carrier 可携带独立 content / asset 边界 | None | None | Full | None | None | Partial |
| 公开插件图中未见验证载体 | Full | Full | None | Full | Full | None |

#### 小结与建议

- 本轮新增确认的结论是：当前 `Angelscript` 在 `D9` 上的优势，不只在“测试多”，而是**验证载体已经正式进入插件自己的 build graph**。这与 `Hazelight` 的 runtime-private testing 是本质差异，应判为 `实现质量差异`。
- `P0` 不建议把当前 AS 的测试再回流到 runtime 私有目录。那会损失现在已经建立好的边界清晰度，也会让 test owner 再次模糊。
- `P1` 如果未来测试资产继续增长，最值得吸收的是 `UnLuaTestSuite` 的一部分思路：在保留当前 `AngelscriptTest` module 的前提下，视需要追加一个更轻的 test-content carrier，而不是把所有东西都塞回一个 module。
- `P2` 对 `puerts` / `sluaunreal`，本轮更准确的结论不是“测试差”，而是“公开 build participation 不可见”。这类项目如果要作为长期对标，后续应继续检索其仓库外层 CI / Tests 目录；但在本维度上，当前 AS 已经明显走得更完整。

---

## 深化分析 (2026-04-09 06:57:21)

### [D2/D3] 脚本到底能不能“长出” UE reflection schema：native class、Blueprint asset，还是只 patch 既有槽位

前文已经比较过 binding owner 和 override owner，但还没有把一个更底层的问题钉死：**脚本语言产物到底会不会把新的 `UFunction/FProperty` 写进 UE 反射世界**。这一点必须和“把已有 `UFunction` 改指到脚本 VM”分开看，因为两者决定了完全不同的 Blueprint、复制和编辑器后续能力。

#### 各插件实现概览

```
Schema Growth Carrier
HZ/AS : script desc -> CreateProperty/UFunction -> UASClass                // native class schema
UC    : C# reflection -> FTypeBridge::Factory -> UBlueprintGeneratedClass  // dynamic native schema
PU    : TS metadata -> PEBlueprintAsset::NewVariables                      // Blueprint asset schema
UL    : Lua table -> Override existing UFunction                           // existing slot patch
SL    : Lua table -> hook existing UFunction                               // existing slot patch
```

术语说明：

| 术语 | 含义 |
| --- | --- |
| `native class schema` | 直接新增到 `UClass/UFunction/FProperty` 的反射成员 |
| `Blueprint asset schema` | 先写到 `UBlueprint::NewVariables` 等 asset 声明，再由 Blueprint 编译产出运行时类 |
| `existing slot patch` | 不新增成员，只接管已存在的 `UFunction` 或 wrapper 槽位 |

#### 详细对比

##### 子维度 1：谁真正新增了 reflected `UFunction/FProperty`

- `Hazelight` 与当前 `Angelscript` 都是 **native class schema**。`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Preprocessor/AngelscriptPreprocessor.cpp:2351-2400` 与 `.../ClassGenerator/AngelscriptClassGenerator.cpp:2781-2791` 表明上游就已经把 `Replicated/ReplicatedUsing` 之类 specifier 先记到 `PropDesc`，再在类生成阶段把它们落成真实 `FProperty`；当前仓库对应路径 `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:2540-2585`、`.../ClassGenerator/AngelscriptClassGenerator.cpp:2820-2835,2923-2957` 保留了同一条 owner，只是继续扩展了测试和工具链。
- `UnrealCSharp` 这一轮确认也不是“只有 override，没有 schema growth”。`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGeneratorCore.cpp:925-939` 直接把 `FTypeBridge::Factory<true>(...)` 产物 `AddCppProperty()` 到目标字段 owner；`...:958-975` 又 `NewObject<UFunction>` 并补回参数/返回值 `FProperty`。换句话说，C# authoring 最终也会长出新的 UE 反射成员，只是 owner 在 `UBlueprintGeneratedClass` 与 dynamic generator。
- `puerts` 处于 **Blueprint asset schema** 路线，不是纯 override。`Reference/puerts/unreal/Puerts/Content/JavaScript/PuertsEditor/UEMeta.js:1442-1449` 会先把 TypeScript decorator/specifier 整理成 `PEPropertyMetaData`；`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:1188-1235` 再把这些 flag/meta 写进 `Blueprint->NewVariables`。这说明 `puerts` 也能新增成员，但 authority 主要在 Blueprint asset，而不是一个自定义 native script class。
- `UnLua` 目前可见路径仍是 **existing slot patch**。`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaManager.cpp:305-316` 只是把 Lua table 里的函数名对到 `UEFunctions`，随后 `ULuaFunction::Override()` 接管原函数；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaFunction.cpp:214-239` 进一步证明它做的是 `SetNativeFunc(execCallLua/execScriptCallLua)` 和 `FunctionMap` 改写，而不是生成新的 reflected property/member。
- `sluaunreal` 同样以 **existing slot patch** 为主。`Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaOverrider.cpp:1174-1263` 先从 Lua module 中找函数名；`...:1381-1419` 再通过 `duplicateUFunction()` + `hookBpScript()` 接管原函数。它会复制 super/override thunk，但本轮没有定位到和 `UASClass` / `FDynamicGeneratorCore` 对等的通用 reflected property 生成链。

[1] 当前 `Angelscript` 仍然直接在 `UASClass` 上长出新的 `UFunction/FProperty`

```cpp
// ============================================================================
// [1] 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 位置: 2820-2835, 2923-2957
// 说明: 当前 AS 的 schema growth owner 在 native script class，而不是后置 patch
// ============================================================================
UFunction* NewFunction = NewObject<UFunction>(NewClass, *FuncName, RF_Public);
NewFunction->FunctionFlags = FUNC_Event | FUNC_BlueprintEvent | FUNC_Public;
NewFunction->Bind();
NewFunction->StaticLink(true);

// ★ 把新函数插进 UClass 的 children / function map，后续 TFieldIterator<UFunction> 可见
NewFunction->Next = NewClass->Children;
NewClass->Children = NewFunction;
NewClass->AddFunctionToFunctionMap(NewFunction, NewFunction->GetFName());

FProperty* NewProperty = PropertyType.CreateProperty(Params);
PropDesc->bHasUnrealProperty = true;

// ★ 新属性不是脚本侧幻象，而是真正落成 UE 的 FProperty
if (PropDesc->bReplicated)
{
    NewProperty->SetPropertyFlags(CPF_Net);
    NewProperty->SetBlueprintReplicationCondition(PropDesc->ReplicationCondition);
    if (PropDesc->bRepNotify)
    {
        NewProperty->SetPropertyFlags(CPF_RepNotify);
        NewProperty->RepNotifyFunc = FName(**RepNotifyFunc);
    }
}
```

[2] `UnrealCSharp` 也会生成新的 `FProperty/UFunction`，只是 owner 在 dynamic generator

```cpp
// ============================================================================
// [2] 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGeneratorCore.cpp
// 位置: 925-939, 958-975
// 说明: C# reflection 先过 type bridge，再真正写入 UE 反射字段
// ============================================================================
const auto CppProperty = FTypeBridge::Factory<true>(
    Property->GetReflectionType(), InField, FName(Name), EObjectFlags::RF_Public);

SetFlags(CppProperty, Property);
InField->AddCppProperty(CppProperty); // ★ 新属性直接加入字段 owner

auto Function = NewObject<UFunction>(InClass, FName(Pair.Key), RF_Public | RF_Transient);
if (const auto Return = Method->GetReturn())
{
    if (const auto Property = FTypeBridge::Factory<true>(Return, Function, "", RF_Public | RF_Transient))
    {
        Property->SetPropertyFlags(CPF_Parm | CPF_OutParm | CPF_ReturnParm);
        Function->AddCppProperty(Property); // ★ 新函数的参数/返回值同样落到反射层
    }
}
```

[3] `puerts` 的 schema growth 不在 native class builder，而在 Blueprint asset metadata 回写

```cpp
// ============================================================================
// [3] 文件: Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp
// 文件: Reference/puerts/unreal/Puerts/Content/JavaScript/PuertsEditor/UEMeta.js
// 位置: PEBlueprintAsset.cpp:154-163, 1188-1235; UEMeta.js:1442-1449
// 说明: PU 先产出 property metadata，再回写 Blueprint->NewVariables
// ============================================================================
Blueprint = FKismetEditorUtilities::CreateBlueprint(
    ParentClass, Package, *InName, BlueprintType, BlueprintClass, BlueprintGeneratedClass, FName("PuertsAutoGen"));
GeneratedClass = Blueprint->GeneratedClass;
// ★ 先确保 Blueprint asset 存在，后续变量定义都写到这个 asset 上

EPropertyFlags InputFlags = static_cast<EPropertyFlags>((static_cast<uint64>(InHFLags) << 32) + InLFlags);
InputFlags |= InMetaData->PropertyFlags;
AddMemberVariable(InNewVarName, InGraphPinType, InPinValueType, InLFlags, InHFLags, InLifetimeCondition);
NeedSave = InMetaData->Apply(Blueprint->NewVariables[VarIndex]) || NeedSave;
// ★ 真正的 schema owner 是 Blueprint->NewVariables，不是自定义 native script class

let metaDataResult = new UE.PEPropertyMetaData();
metaDataResult.SetPropertyFlags(Number(FinalFlags >> 32n), Number(FinalFlags & 0xffffffffn));
metaDataResult.SetRepCallbackName(RepCallbackName);
// ★ TypeScript decorator/specifier 最终先进入 PEPropertyMetaData
```

[4] `UnLua` 与 `sluaunreal` 的主路径仍是 patch 已有函数槽位

```cpp
// ============================================================================
// [4] 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaManager.cpp
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaFunction.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaOverrider.cpp
// 位置: UnLuaManager.cpp:305-316; LuaFunction.cpp:214-239; LuaOverrider.cpp:1398-1419
// 说明: UL / SL 都先找现有函数，再接管 thunk；不是通用 reflected member 生成
// ============================================================================
for (const auto& LuaFuncName : BindInfo.LuaFunctions)
{
    UFunction** Func = BindInfo.UEFunctions.Find(LuaFuncName);
    if (Func)
    {
        UFunction* Function = *Func;
        ULuaFunction::Override(Function, Class, LuaFuncName); // ★ UL：命中已有 UFunction 才覆写
    }
}

Function->SetNativeFunc(&execScriptCallLua);
Function->GetOuterUClass()->AddNativeFunction(*Function->GetName(), &execScriptCallLua);
// ★ UL：接管的是原函数的 native thunk

auto supercallFunc = duplicateUFunction(func, cls, FName(*(SUPER_CALL_FUNC_NAME_PREFIX + func->GetName())), func->GetNativeFunc());
overrideFunc->SetNativeFunc(hookFunc);
overrideFunc->Script.Insert(Code, CodeSize, 0);
// ★ SL：复制 / hook 现有 UFunction，而不是新增一套通用成员 schema
```

#### 对比矩阵

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 脚本侧可新增 reflected `UFunction` | Full | Full | None | Partial | None | Full |
| 脚本侧可新增 reflected property/member | Full | Full | None | Partial | None | Full |
| schema 主 owner 在 native `UClass/UFunction/FProperty` | Full | Full | None | Partial | None | Full |
| schema 主 owner 在 `Blueprint->NewVariables` / asset 层 | None | Partial | None | Full | None | None |
| 主路径是 existing-slot patch | Partial | Partial | Full | Partial | Full | Partial |

#### 小结与建议

- 这一轮新增确认的关键结论是：`Angelscript` 在 D2/D3 上的核心护城河，不只是“bind 多”，而是**脚本能直接增长 UE native reflection schema**。这与 `UnLua/sluaunreal` 的 override-only 路线是能力类别差异，不是实现细节差异。
- `UnrealCSharp` 是最接近的参考对象。它证明“native schema growth”并不必然绑定单一脚本 VM；值得吸收的是它把 `type bridge -> metadata -> reinstance` 做成更规整的 generator contract，优先级 `P1`。
- `puerts` 的启发不在替代 native class 路线，而在 **Blueprint asset schema** 这层。若后续 AS 要增强“只改 Blueprint-facing surface、不必引入完整 native script class”的工作流，可以参考它的 `metadata -> NewVariables -> CompileBlueprint` 链，优先级 `P2`。
- 对 `UnLua/sluaunreal` 这类 patch 路线，本轮更准确的结论应是：它们**没有实现同层通用 schema growth**，但在快速覆盖已有 Blueprint event 时更轻。这不是低质量，而是目标不同。

### [D2/D11] 脚本自有状态能否进入网络复制：真实业务属性、Blueprint 变量，还是 proxy serialization

前文已经讲过生命周期和 latent test，但还没有把 **script-owned state 最终通过哪种 carrier 进入 UE replication** 说透。这里必须区分三种完全不同的 owner：直接是业务类自己的 `FProperty`、先落在 `Blueprint->NewVariables`、以及根本不长业务属性而是走独立 proxy/serialization 结构。

#### 各插件实现概览

```
Replication Carrier
HZ/AS : script specifier -> CPF_Net on generated FProperty -> class-owned replication list
UC    : C# attribute -> CPF_Net / CPF_RepNotify on dynamic FProperty -> generated class tracks replicated count
PU    : TS metadata -> Blueprint->NewVariables NetFlags / OnRep graph -> CompileBlueprint
SL    : Lua table -> ClassLuaReplicated + FLuaNetSerialization proxy         // proxy payload
UL    : override existing function only                                      // no script-owned replication schema located
```

#### 详细对比

##### 子维度 1：复制声明写到哪里

- `Hazelight` 与当前 `Angelscript` 都是 **业务类自身 member property**。上游 `J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Preprocessor/AngelscriptPreprocessor.cpp:2351-2400` / `.../ClassGenerator/AngelscriptClassGenerator.cpp:2781-2791`，以及当前 `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:2540-2585` / `.../ClassGenerator/AngelscriptClassGenerator.cpp:2945-2957`，都表明 `Replicated/ReplicatedUsing` 是先记在 property desc，再直接落到新生成的 `FProperty` 上。
- `UnrealCSharp` 也是 **真实 member property** 路线。`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGeneratorCore.cpp:393-409` 会把 `[Replicated]` / `[ReplicatedUsing]` attribute 直接翻成 `CPF_Net | CPF_RepNotify`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicClassGenerator.cpp:544-585` 还会在 BlueprintGeneratedClass 侧同步 `NumReplicatedProperties` 与变量描述。
- `puerts` 的 owner 更偏 **Blueprint asset variable list**。`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:1119-1141` 直接比较 `Blueprint->NewVariables[VarIndex].PropertyFlags` 的 `NetMask`，必要时自动补 `OnRep_*` graph；也就是说，复制语义先属于 Blueprint asset，再由编译产出运行时类。
- `sluaunreal` 不是“没有复制”，而是 **proxy serialization**。`Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaActor.h:30-33` / `.../Private/LuaActor.cpp:44-50` 只真正复制一个 `FLuaNetSerialization LuaNetSerialization` 字段；Lua 脚本里的 `GetLifetimeReplicatedProps()` 列表则由 `Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaNet.cpp:137-204` 动态构建成 `ClassLuaReplicated` + 临时 `UStruct` 子字段。
- `UnLua` 在当前源码边界里没有定位到对等的 script-owned replicated property 生成链。已定位到的主路径仍是 `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaManager.cpp:305-316` 与 `.../LuaFunction.cpp:214-239` 的函数 override，因此这里应判为 **没有实现同层复制 carrier**，不是简单写成“网络能力差”。

[1] 当前 `Angelscript` 的复制 owner 直接落在新生成的业务属性上

```cpp
// ============================================================================
// [1] 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp
// 位置: 2540-2585; 2945-2957; 894-909
// 说明: replicate specifier -> generated FProperty -> class-owned lifetime list
// ============================================================================
PropDesc->bReplicated = true;
PropDesc->bRepNotify = true;
PropDesc->Meta.Add(PP_NAME_ReplicatedUsing, Spec.Value);
// ★ 复制语义先记在脚本 property descriptor

if (PropDesc->bReplicated)
{
    NewProperty->SetPropertyFlags(CPF_Net);
    NewProperty->SetBlueprintReplicationCondition(PropDesc->ReplicationCondition);
    if (PropDesc->bRepNotify)
    {
        NewProperty->SetPropertyFlags(CPF_RepNotify);
        NewProperty->RepNotifyFunc = FName(**RepNotifyFunc);
    }
}
// ★ 最终直接落到业务类自己的 FProperty

for (TFieldIterator<FProperty> It(this, EFieldIteratorFlags::ExcludeSuper); It; ++It)
{
    FProperty* Prop = *It;
    if (Prop != NULL && Prop->GetPropertyFlags() & CPF_Net)
    {
        OutLifetimeProps.AddUnique(FLifetimeProperty(Prop->RepIndex, Prop->GetBlueprintReplicationCondition()));
    }
}
// ★ runtime 读取的也是类自身 property list，而不是外部 proxy
```

[2] `UnrealCSharp` 也把复制 flag 写到动态生成的真实 `FProperty`

```cpp
// ============================================================================
// [2] 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGeneratorCore.cpp
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicClassGenerator.cpp
// 位置: 393-409; 544-585
// 说明: C# attribute 最终进入真实 property flags，并同步 BlueprintGeneratedClass 统计
// ============================================================================
if (InReflection->HasAttribute(FReflectionRegistry::Get().GetReplicatedAttributeClass()))
{
    InProperty->SetPropertyFlags(CPF_Net);
    InProperty->SetBlueprintReplicationCondition(static_cast<ELifetimeCondition>(...));
}

if (InReflection->HasAttribute(FReflectionRegistry::Get().GetReplicatedUsingAttributeClass()))
{
    InProperty->SetPropertyFlags(CPF_Net | CPF_RepNotify);
    InProperty->RepNotifyFunc = FName(...);
}
// ★ 复制 owner 在动态生成的真实 FProperty 上

if (InProperty->HasAnyPropertyFlags(CPF_Net))
{
    if (const auto BlueprintGeneratedClass = Cast<UBlueprintGeneratedClass>(InClass))
    {
        BlueprintGeneratedClass->NumReplicatedProperties++;
    }
}
// ★ generated class 自己维护 replicated-property 统计
```

[3] `puerts` 把复制 flag / `OnRep` 回写到 `Blueprint->NewVariables`

```cpp
// ============================================================================
// [3] 文件: Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp
// 文件: Reference/puerts/unreal/Puerts/Content/JavaScript/PuertsEditor/UEMeta.js
// 位置: 1119-1141, 1188-1235; UEMeta.js:1442-1449
// 说明: PU 的复制 owner 在 Blueprint asset 变量列表，而不是 native script class
// ============================================================================
const uint64 NetMask = CPF_Net | CPF_RepNotify;
uint64 NetFlags = InFlags & NetMask;
if ((Variable.PropertyFlags & NetMask) != NetFlags)
{
    Variable.PropertyFlags &= ~NetMask;
    Variable.PropertyFlags |= NetFlags;
    if (Variable.PropertyFlags & CPF_RepNotify)
    {
        FString NewFuncNameStr = FString::Printf(TEXT("OnRep_%s"), *NewVarName.ToString());
        UEdGraph* FuncGraph = FBlueprintEditorUtils::CreateNewGraph(
            Blueprint, NewFuncName, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
        Blueprint->NewVariables[VarIndex].RepNotifyFunc = NewFuncName;
    }
}
// ★ 复制 flag 和 OnRep graph 都在 Blueprint asset 上自动补齐

let metaDataResult = new UE.PEPropertyMetaData();
metaDataResult.SetPropertyFlags(Number(FinalFlags >> 32n), Number(FinalFlags & 0xffffffffn));
metaDataResult.SetRepCallbackName(RepCallbackName);
// ★ TS decorator 先降到 metadata，再喂给 Blueprint asset
```

[4] `sluaunreal` 的复制 carrier 是 `LuaNetSerialization` proxy，而不是业务类真实成员

```cpp
// ============================================================================
// [4] 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaActor.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaNet.cpp
// 位置: LuaActor.cpp:44-50; LuaNet.cpp:137-204
// 说明: SL 复制的是 wrapper/proxy，Lua 脚本成员先被转成临时 UStruct 子字段
// ============================================================================
void ALuaActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME_CONDITION(ALuaActor, LuaNetSerialization, COND_None);
}
// ★ Actor 真正公开给 UE 复制系统的是一个 FLuaNetSerialization 成员

FProperty* prop = cls->FindPropertyByName(TEXT("LuaNetSerialization"));
NS_SLUA::LuaVar getLifetimeFunc = luaModule.getFromTable<NS_SLUA::LuaVar>("GetLifetimeReplicatedProps");
auto& classReplicated = *classLuaReplicatedMap.Add(cls, new ClassLuaReplicated());
classReplicated.ownerProperty = prop;

auto ustruct = NewObject<UStruct>();
classReplicated.ustruct = ustruct;
childProperty = NS_SLUA::PropertyProto::createProperty(..., ustruct);
// ★ Lua 列出来的 replicated 字段先进入 proxy UStruct，不直接变成业务类 member property
```

#### 对比矩阵

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 脚本侧可声明 `Replicated / RepNotify` | Full | Full | None | Full | Partial | Full |
| 最终进入业务类自身 `FProperty` 的 `CPF_Net` | Full | Full | None | Partial | None | Full |
| 自动补 `RepNotify` / `OnRep_*` 绑定 | Full | Full | None | Full | Partial | Full |
| 复制主 carrier 是 wrapper / proxy struct | None | None | None | None | Full | None |
| 当前源码可见主路径仍只覆写已有函数 | None | None | Full | None | None | None |

#### 小结与建议

- 本轮新增最重要的判断是：`Angelscript` 在复制这一层的优势，不只是“支持 replicated”，而是**复制 carrier 仍然是业务类自己的真实成员 property**。这与 `sluaunreal` 的 proxy serialization 是本质差异。
- `UnrealCSharp` 证明另一条可行路线是“attribute -> dynamic property -> generated class stats”，这和当前 AS 最接近。若后续要统一 attribute/specifier 处理或复制统计，可优先参考 UC，优先级 `P1`。
- `puerts` 的价值在于它把 `RepNotify` 图自动补到 Blueprint asset。对 AS 来说，这更像一个 Blueprint authoring 经验，而不是复制底层 owner 的替代方案；建议作为 `P2` 参考。
- `sluaunreal` 则提示了另一类问题：**当脚本状态无法或不值得变成业务类真实成员时，proxy serialization 仍然是可用退路**。这类方案适合高度动态的数据面，但不应反过来替换当前 AS 已经稳定的 native property 路线。

### [D7] “创建一个脚本类型”是否被产品化：asset-first、source-first，还是只给 tool button

这一轮再补一个 editor-facing 问题：**操作员在 UE Editor 里到底如何开始“创建一个脚本类型”**。这不是菜单多少的问题，而是作者入口的 product shape：是显式弹窗建 asset、显式向项目写入新源码、还是只给一个工具按钮让后台自己扫。

#### 各插件实现概览

```
Type Authoring Entry
HZ/AS : popup -> SaveAssetDialog -> CreateBlueprint from UASClass         // asset-first
UC    : toolbar/menu -> dialog -> write new .cs file                      // source-first
PU    : menu/console -> GenDts ; editor boot -> CodeAnalyze auto sync     // analyzer-driven
UL    : toolbar + watcher + save hooks                                    // helper-oriented
SL    : profiler button / lua-wrapper external exe                        // tool-oriented
```

#### 详细对比

##### 子维度 1：作者入口是显式向用户暴露，还是隐式后台对齐

- `Hazelight` 与当前 `Angelscript` 都是 **asset-first**。`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:437-531` 与当前 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:418-527` 的 `ShowCreateBlueprintPopup()` 都会先弹 `CreateModalSaveAssetDialog`，再把现成 `UASClass` 转成 Blueprint/DataAsset。作者入口非常直白：先有脚本类，再显式建 UE asset。
- `UnrealCSharp` 是 **source-first**。`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpPlayToolBar.cpp:50-56` 把 `OpenNewDynamicClass` 命令映射到 `FDynamicNewClassUtils::OpenAddDynamicClassToProjectDialog(...)`；`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/NewClass/DynamicNewClassUtils.cpp:12-31` 会起一个固定大小 dialog；`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/NewClass/SDynamicNewClassDialog.cpp:773-781` 再模板化生成 `.cs` 文件并直接打开源码。也就是说，作者入口首先是“写一个新类型源文件”。
- `puerts` 更像 **analyzer-driven**。`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/GenDTSCommands.cpp:13-17` 暴露给用户的显式按钮只是“Generate *.d.ts and copy some js builtin libs”；`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1640-1669` 额外提供 `Puerts.Gen` console command；而真正的类型/资产对齐则在 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:138-150` 启动 `CodeAnalyze` 后后台进行。用户感知到的是“生成声明”和“写 TS”，而不是一个明确的“创建脚本类”向导。
- `UnLua` 当前编辑器面更像 **helper-oriented**。`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:48-69` 公开的是 toolbar、目录监听、包保存事件和 packaging 设置；本轮对 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/` 检索 `CreateModalSaveAssetDialog / CreateBlueprint / OpenNewDynamicClass` 未命中，因此没有定位到与 AS/UC 对等的类型创建 wizard。
- `sluaunreal` 公开的是 **tool-oriented button**。`Reference/sluaunreal/Plugins/lua_wrapper/Source/lua_wrapper/Private/lua_wrapperCommands.cpp:7-10` 的按钮名就叫 “Generate Lua Interface”；`Reference/sluaunreal/Plugins/lua_wrapper/Source/lua_wrapper/Private/lua_wrapper.cpp:122-128` 点击后直接 `system(.../lua-wrapper.exe)`。这不是“创建类型”，而是“起一个外部生成器”。`slua_profile` 则是 profiler 入口，不是 authoring wizard。

[1] 当前 `Angelscript / Hazelight` 把作者入口做成显式 asset popup

```cpp
// ============================================================================
// [1] 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 位置: 418-527
// 说明: 当前 AS 的作者入口是显式弹窗创建 Blueprint/DataAsset
// ============================================================================
FSaveAssetDialogConfig SaveAssetDialogConfig;
SaveAssetDialogConfig.DefaultPath = FPaths::GetPath(AssetPath);
SaveAssetDialogConfig.DefaultAssetName = FPaths::GetCleanFilename(AssetPath);

FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
FString SaveObjectPath = ContentBrowserModule.Get().CreateModalSaveAssetDialog(SaveAssetDialogConfig);

UPackage* Package = CreatePackage(*UserPackageName);
if (bIsDataAsset)
{
    Asset = NewObject<UDataAsset>(Package, Class, AssetName, RF_Public | RF_Transactional | RF_Standalone);
}
else
{
    Asset = FKismetEditorUtilities::CreateBlueprint(
        Class, Package, AssetName, BPTYPE_Normal, BlueprintClass, BlueprintGeneratedClass, FName("AngelscriptCreateBlueprint"));
}
// ★ 用户点击一次按钮，立刻得到一个可见的 UE asset；Hazelight 同名函数路径基本同构
```

[2] `UnrealCSharp` 的作者入口先写新源码文件，再交给 dynamic generator

```cpp
// ============================================================================
// [2] 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpPlayToolBar.cpp
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/NewClass/DynamicNewClassUtils.cpp
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/NewClass/SDynamicNewClassDialog.cpp
// 位置: 50-56; 12-31; 773-781
// 说明: UC 先暴露一个新类型向导，再把模板源码写入项目
// ============================================================================
CommandList->MapAction(
    FUnrealCSharpEditorCommands::Get().OpenNewDynamicClass,
    FExecuteAction::CreateLambda([]
    {
        FDynamicNewClassUtils::OpenAddDynamicClassToProjectDialog(
            FUnrealCSharpFunctionLibrary::GetGameDirectory());
    }));

const auto NewClassDialog =
    SNew(SDynamicNewClassDialog)
    .ParentWindow(AddCodeWindow)
    .InitialPath(InInitialPath);
// ★ 作者入口是显式 dialog，不是后台 watcher

FDynamicNewClassUtils::GetDynamicClassContent(ParentClass, NewClassName, NewClassContent);
FUnrealCSharpFunctionLibrary::SaveStringToFile(*GetDynamicFileName(), NewClassContent);
FSourceCodeNavigation::OpenSourceFile(GetDynamicFileName());
// ★ 真正 first-class 的动作是“写一个新的 .cs 类型文件”
```

[3] `puerts` 暴露给操作员的主入口仍是生成声明与启动分析器

```cpp
// ============================================================================
// [3] 文件: Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/GenDTSCommands.cpp
// 文件: Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp
// 文件: Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp
// 位置: GenDTSCommands.cpp:13-17; DeclarationGenerator.cpp:1640-1669; PuertsEditorModule.cpp:138-150
// 说明: PU 的显式入口是 d.ts / console command；类型对齐更多交给后台 CodeAnalyze
// ============================================================================
UI_COMMAND(
    PluginAction, "puerts", "Generate *.d.ts and copy some js builtin libs", EUserInterfaceActionType::Button, FInputChord());

PluginCommands->MapAction(FGenDTSCommands::Get().PluginAction,
    FExecuteAction::CreateRaw(this, &FDeclarationGenerator::GenUeDtsCallback), FCanExecuteAction());

ConsoleCommand = MakeUnique<FAutoConsoleCommand>(TEXT("Puerts.Gen"), TEXT("Execute GenDTS action"), ...);
// ★ 操作员可见的是“生成声明”，不是一个 modal create-type wizard

JsEnv = MakeShared<PUERTS_NAMESPACE::FJsEnv>(std::make_shared<PUERTS_NAMESPACE::DefaultJSModuleLoader>(TEXT("JavaScript")), ...);
JsEnv->Start("PuertsEditor/CodeAnalyze");
// ★ 真正的类型/资产同步更多由后台分析器自动驱动
```

[4] `UnLua / sluaunreal` 的 editor 公开面更像 helper/tool，而不是 first-class type wizard

```cpp
// ============================================================================
// [4] 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp
// 文件: Reference/sluaunreal/Plugins/lua_wrapper/Source/lua_wrapper/Private/lua_wrapperCommands.cpp
// 文件: Reference/sluaunreal/Plugins/lua_wrapper/Source/lua_wrapper/Private/lua_wrapper.cpp
// 位置: UnLuaEditorModule.cpp:48-69; lua_wrapperCommands.cpp:7-10; lua_wrapper.cpp:122-128
// 说明: UL 暴露的是 watcher/save hooks；SL 暴露的是外部工具按钮
// ============================================================================
virtual void StartupModule() override
{
    FCoreDelegates::OnPostEngineInit.AddRaw(this, &FUnLuaEditorModule::OnPostEngineInit);
    MainMenuToolbar = MakeShareable(new FMainMenuToolbar);
    BlueprintToolbar = MakeShareable(new FBlueprintToolbar);
    UUnLuaEditorFunctionLibrary::WatchScriptDirectory();
    UPackage::PreSavePackageWithContextEvent.AddRaw(this, &FUnLuaEditorModule::OnPackageSavingWithContext);
    SetupPackagingSettings();
}
// ★ UL：当前可见 surface 偏辅助工作流，没有对等 new-type dialog 证据

UI_COMMAND(OpenPluginWindow, "LuaWrapper", "Generate Lua Interface (Windows only)", EUserInterfaceActionType::Button, FInputGesture());

auto cmd = contentDir + TEXT("/../Tools/lua-wrapper.exe");
system(TCHAR_TO_UTF8(*cmd));
// ★ SL：按钮的语义是启动外部生成器，不是 editor 内创建一种新脚本类型
```

#### 对比矩阵

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 存在显式 modal create-type / create-asset 向导 | Full | Full | None | None | None | Full |
| 入口动作直接写入新的源码文件 | None | Full | None | None | None | None |
| 入口动作直接创建可见 Blueprint / DataAsset | Full | Partial | None | Partial | None | Full |
| 操作员主入口更像 codegen / tooling 按钮 | Partial | Partial | Partial | Full | Full | Partial |
| 需要跳出 UE 进程调用 external tool | None | None | None | None | Full | None |

#### 小结与建议

- 这一轮新增的判断是：`Angelscript` 在 D7 上最独特的地方，不只是有 Content Browser data source，而是**把“从脚本类创建 UE asset”做成了显式、可见、可控的 operator flow**。这和 `puerts` 的后台 analyzer 驱动、`sluaunreal` 的 external tool button 都不同。
- 最值得吸收的是 `UnrealCSharp` 的 **source-first scaffold**。当前 AS 若想降低新脚本类的上手成本，可以考虑增加一个“新建 `.as` 类模板”的可选 wizard，但不应取代现有 `ShowCreateBlueprintPopup`；优先级 `P1`。
- `puerts` 的自动 asset reconcile 适合“代码写完后后台补资产”，但默认隐藏变更较多。对 AS 来说，更适合作为可选同步器，而不是默认主入口，优先级 `P2`。
- `UnLua/sluaunreal` 在这一维不是“没有 editor integration”，而是 **integration 重点不在类型创建**。如果后续要比较 D7，必须持续区分“帮助你编辑已有脚本”与“帮助你创建一个新类型”这两类完全不同的入口。

---

## 深化分析 (2026-04-09 07:28:42)

### [D2/D6/D11] 自动暴露面的 authority：谁有权把“可调用面”写成正式生成产物

前面已经比较过“有无生成器”和“产物是不是 live schema”。这一轮只补更底层的一层：**脚本 surface 的分类权到底挂在哪一层**。`Hazelight (UEAS2)` 把它挂在 engine/UHT 主链，`当前 Angelscript` 挂在 `stock UHT exporter + runtime metadata` 双轨，`UnrealCSharp / UnLua / puerts / sluaunreal` 则分别落到 editor generator、commandlet stub、toolbar/analyzer 和 external wrapper。差距不只是“有没有代码生成”，而是**谁定义 canonical callable surface**。

#### 各插件实现概览

```
Exposure Authority
HZ    : patched UHT specifier -> engine metadata -> editor/runtime consumers // 主链 owner
AS    : stock UHT exporter -> AS_FunctionTable_*.cpp + runtime metadata      // build/runtime 双轨
UC    : editor Generator() -> generated .cs/.binding -> compiler             // editor-owned codegen
UL    : editor/commandlet -> IntelliSense stubs in Intermediate              // authoring/stub owner
PU    : toolbar/console -> .d.ts + CodeAnalyze                               // tooling/analyzer owner
SL    : reflection/LuaCppBinding first, lua-wrapper last                     // supplement-only tool
```

#### 详细对比

##### 子维度 1：谁决定“这个函数算不算脚本主 surface”

- `Hazelight (UEAS2)` 的 classification owner 仍在引擎主链。`UhtFunctionSpecifiers.cs` 直接把 `ScriptCallable` 注册成 UHT specifier，`UhtFunction.cs` 又把它并入 default-value metadata 回写条件；再往下，`FProperty` 直接多出 `AngelscriptPropertyFlags`，`PropertyEditor` 也直接识别 `APF_RuntimeGenerated`。这说明 `ScriptCallable` 和 runtime-generated property 在 UEAS2 里不是“插件私有约定”，而是 **engine 默认理解的 schema**。
- `当前 Angelscript` 已经把 classification owner 改成双轨。`AngelscriptFunctionTableCodeGenerator::ShouldGenerate()` 只接受 `BlueprintCallable/Pure` 并输出 `AS_FunctionTable_*.cpp`，但 runtime 的 `Bind_BlueprintType.cpp` 仍然会在绑定期消费 `NAME_ScriptCallable` metadata。也就是说，**build-time 的 canonical artifact 与 runtime 的可见 callable 面已经不再完全同构**。
- `UnrealCSharp` 的 classification owner 不在 UHT，而在 editor generator pipeline。`FUnrealCSharpEditorModule::Generator()` 显式串起 `Solution / CodeAnalysis / Class / Struct / Enum / Asset / Binding` 多个生成阶段，再写文件、编译。这意味着它的 canonical surface 更像“本插件 generator 愿意生成什么”，而不是 UE build graph 原生知道什么。
- `UnLua` inspected 到的 owner 更偏 authoring stub。`FUnLuaIntelliSenseGenerator` 监听 asset registry，把产物写到 `Intermediate/IntelliSense`；`UnLuaIntelliSenseCommandlet` 又能把 reflected / non-reflected classes、enums 和 global functions 批量落盘。它有 formal artifact，但这个 artifact 主要服务 IDE / authoring，而不是改变 UE 主干对 callable surface 的理解。
- `puerts` 的 owner 则明显是 tooling surface。`GenDTSCommands.cpp` 暴露的主入口是 “Generate *.d.ts”，`DeclarationGenerator.cpp` 还注册了 `Puerts.Gen` console command；与此同时 `PuertsEditorModule` 会起一个 `CodeAnalyze` JS env。换句话说，`puerts` 的 canonical typing surface 是 **工具侧生成和 analyzer 共同维护的投影面**。
- `sluaunreal` 最不一样。`Tools/README.md` 把 `lua-wrapper` 明确写成“反射/LuaCppBinding 不支持时的补位工具”，而且范围限定为 engine `USTRUCT`。`lua_wrapper.cpp` 只是一个启动外部 exe 的按钮。这不是一条“统一自动导出链”，而是一条**最后兜底的 supplement path**。

[1] `Hazelight (UEAS2)` 把 `ScriptCallable` 和 runtime-generated property 写进 engine/UHT 主链；当前 `Angelscript` 则把 build-time exporter 与 runtime metadata 分开。

```csharp
// ============================================================================
// [1] 文件: J:\UnrealEngine\UEAS2\Engine\Source\Programs\Shared\EpicGames.UHT\Specifiers\UhtFunctionSpecifiers.cs
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Programs\Shared\EpicGames.UHT\Types\UhtFunction.cs
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Runtime\CoreUObject\Public\UObject\UnrealType.h
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Runtime\CoreUObject\Public\UObject\ObjectMacros.h
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Editor\PropertyEditor\Private\PropertyNode.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp
// 位置: UhtFunctionSpecifiers.cs:23-29; UhtFunction.cs:570-573; UnrealType.h:185-188;
//       ObjectMacros.h:482-491; PropertyNode.cpp:440-441; AngelscriptFunctionTableExporter.cs:21-27;
//       AngelscriptFunctionTableCodeGenerator.cs:490-514; Bind_BlueprintType.cpp:1311-1314
// 说明: HZ 是 engine/UHT 主链 owner；current AS 是 stock-UHT exporter + runtime metadata 双轨
// ============================================================================
[UhtSpecifier(Extends = UhtTableNames.Function, ValueType = UhtSpecifierValueType.Legacy)]
private static void ScriptCallableSpecifier(UhtSpecifierContext specifierContext)
{
    UhtFunction function = (UhtFunction)specifierContext.Type;
    function.MetaData.Add("ScriptCallable", "");                    // ★ HZ：UHT 直接认识 ScriptCallable
}

bool storeCppDefaultValueInMetaData = FunctionFlags.HasAnyFlags(EFunctionFlags.BlueprintCallable | EFunctionFlags.Exec)
    || MetaData.ContainsKey("ScriptCallable");                      // ★ HZ：连 default-value metadata 主链都把它并进来

EPropertyFlags PropertyFlags;
uint16 RepIndex;
uint16 AngelscriptPropertyFlags;                                    // ★ HZ：property layout 本体有 Angelscript 扩展位

enum EAngelscriptPropertyFlags : uint16
{
    APF_CppConst             = 0x0001,
    APF_CppRef               = 0x0002,
    APF_CppEnumAsByte        = 0x0004,
    APF_RuntimeGenerated     = 0x0008,
    APF_WorldContext         = 0x0010,
    APF_ConstTemplateArg     = 0x0020,
};

SetNodeFlags(EPropertyNodeFlags::NoCacheAddress, MyProperty && (MyProperty->AngelscriptPropertyFlags & APF_RuntimeGenerated));
// ★ HZ：editor 主干直接消费 runtime-generated property 语义

[UhtExporter(
    Name = "AngelscriptFunctionTable",
    Description = "Exports Angelscript function table data",
    Options = UhtExporterOptions.Default | UhtExporterOptions.CompileOutput,
    CppFilters = ["AS_FunctionTable_*.cpp"],
    ModuleName = "AngelscriptRuntime")]
// ★ current AS：build-time 入口变成 stock-UHT exporter，而不是 patched UHT specifier

if (!AngelscriptFunctionTableExporter.IsBlueprintCallable(function))
{
    return false;                                                   // ★ current AS：自动导出面只覆盖 BlueprintCallable/Pure
}

else if (Function->HasMetaData(NAME_ScriptCallable))
    BindBlueprintCallable(InType, Function, DBMethod, *TargetName);
// ★ current AS：runtime 仍能吃 ScriptCallable，但这条路和 exporter 已经不是同一 owner
```

##### 子维度 2：谁真正写出产物，谁只是补位

- `UnrealCSharp` 的生成步骤是显式 orchestrated pipeline。用户点击 generator 后，插件自己依次跑 solution、code analysis、class/struct/enum/asset/binding generator，再 `SaveStringToFile()`，最后触发编译。它的优势是**产物责任集中且可见**；代价是 generator 成为真正的 authority，UE build graph 只是宿主。
- `UnLua` 的产物更偏 stub / IDE sidecar。它既能在 editor session 中增量更新，也能用 commandlet 离线批量导出。这条路对 authoring 非常友好，但它不把 callable classification 上升成 engine/build 主链能力。
- `puerts` 把 `.d.ts` 与 analyzer 分成“显式按钮/console command”和“后台 `CodeAnalyze` env”两段。强项是 tooling 可扩展，但 canonical typing surface 仍是投影层，不是原生 schema。
- `sluaunreal` 的 `lua-wrapper` 明确是 supplement。README 甚至直接规定优先级：“先 reflection，再 LuaCppBinding，最后才用 `lua-wrapper`”。这说明它从设计上就不是 primary owner。

[2] `UnrealCSharp` 的 callable/artifact surface 由 editor generator pipeline 明确串起，再落盘和编译。

```cpp
// ============================================================================
// [2] 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp
// 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FStructGenerator.cpp
// 位置: UnrealCSharpEditor.cpp:237-309; FStructGenerator.cpp:358-362
// 说明: UC 的 canonical surface 不是 UHT 主链，而是 editor-owned generator pipeline
// ============================================================================
void FUnrealCSharpEditorModule::Generator()
{
    FUnrealCSharpCoreModuleDelegates::OnBeginGenerator.Broadcast();
    ...
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
    FCSharpCompiler::Get().ImmediatelyCompile();                    // ★ 生成、落盘、编译是同一条 editor 流水线
    FUnrealCSharpCoreModuleDelegates::OnEndGenerator.Broadcast();
}

const auto FileName = FGeneratorCore::GetFileName(InScriptStruct);
FGeneratorCore::AddGeneratorFile(FileName);
FUnrealCSharpFunctionLibrary::SaveStringToFile(FileName, Content); // ★ artifact writer 是 generator 自己
```

[3] `UnLua` 把 IntelliSense 产物写进 `Intermediate`，同时支持 commandlet 批量导出；`puerts` 则把 `.d.ts` 生成和 `CodeAnalyze` 分层。

```cpp
// ============================================================================
// [3] 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp
// 文件: Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/GenDTSCommands.cpp
// 文件: Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp
// 文件: Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp
// 位置: UnLuaIntelliSenseGenerator.cpp:47-53,58-79; UnLuaIntelliSenseCommandlet.cpp:55-70,91-123;
//       GenDTSCommands.cpp:13-16; DeclarationGenerator.cpp:1640-1669; PuertsEditorModule.cpp:138-150
// 说明: UL 是 editor/commandlet stub owner；PU 是 toolbar/console + analyzer owner
// ============================================================================
OutputDir = IPluginManager::Get().FindPlugin("UnLua")->GetBaseDir() + "/Intermediate/IntelliSense";
AssetRegistryModule.Get().OnAssetAdded().AddRaw(this, &FUnLuaIntelliSenseGenerator::OnAssetAdded);
AssetRegistryModule.Get().OnAssetRemoved().AddRaw(this, &FUnLuaIntelliSenseGenerator::OnAssetRemoved);
// ★ UL：产物目录和增量刷新都由插件自己维护

const auto ExportedReflectedClasses = UnLua::GetExportedReflectedClasses();
const auto ExportedNonReflectedClasses = UnLua::GetExportedNonReflectedClasses();
...
Pair.Value->GenerateIntelliSense(GeneratedFileContent);
SaveFile(ModuleName, Pair.Key, GeneratedFileContent);              // ★ UL：commandlet 也能离线批量导出

UI_COMMAND(PluginAction, "puerts", "Generate *.d.ts and copy some js builtin libs", EUserInterfaceActionType::Button, FInputChord());
ConsoleCommand = MakeUnique<FAutoConsoleCommand>(TEXT("Puerts.Gen"), TEXT("Execute GenDTS action"), ...);
// ★ PU：显式按钮和 console command 都是正式入口

JsEnv = MakeShared<PUERTS_NAMESPACE::FJsEnv>(..., TEXT("--max-old-space-size=2048"));
JsEnv->Start("PuertsEditor/CodeAnalyze");                          // ★ PU：后台 analyzer 继续维护投影面
```

[4] `sluaunreal` 明确把 `lua-wrapper` 定位成 supplement，而不是 primary export owner。

```md
<!-- =========================================================================
[4] 文件: Reference/sluaunreal/Tools/README.md
文件: Reference/sluaunreal/Plugins/lua_wrapper/Source/lua_wrapper/Private/lua_wrapper.cpp
位置: Tools/README.md:7-19,35-37; lua_wrapper.cpp:122-128
说明: SL 的 static export 是补位型工具，且需要跳出 UE 进程
=========================================================================== -->
lua-wrapper 是作为 slua-unreal 中 lua 导出接口的补充
1. 反射
2. LuaCppbinding
3. lua-wrapper，通过静态代码生成导出以上两种方式不支持的接口

1. 不支持导出自定义类型
2. 不支持导出可反射的类型
3. 导出类型限定于引擎中的 USTRUCT 类型
4. 优先使用反射或者 LuaCppBinding 导出类型，最后才考虑使用 lua-wrapper

auto cmd = contentDir + TEXT("/../Tools/lua-wrapper.exe");
system(TCHAR_TO_UTF8(*cmd));
<!-- ★ SL：真正的 writer 在外部 exe，UE 插件只是按钮壳 -->
```

#### 对比矩阵

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| engine/UHT patched callable classifier 是 primary owner | Full | None | None | None | None | None |
| stock-UHT exporter 直接写 compile output | None | None | None | None | None | Full |
| editor/commandlet 显式写 authoring artifact | None | Full | Full | Full | None | None |
| external tool 是补位导出必经路径 | None | None | None | None | Full | None |
| `ScriptCallable` 与 `BlueprintCallable` 仍处于同一自动导出面 | Full | N/A | N/A | N/A | N/A | Partial |

#### 小结与建议

- 本轮新增最重要的判断不是“当前 Angelscript 没有自动导出”，而是**自动导出的 authority 已经从 UEAS2 的 engine/UHT 主链，迁到了 `stock UHT exporter`；同时 runtime 仍保留一条 `ScriptCallable` metadata 旁路**。这解释了为什么 current 更易交付，但 wrapper authoring 的 canonical source 不再只有一条。
- 最值得吸收的是把这条 split 明文化。建议为 `ScriptCallable-only` surface 增加正式 skipped manifest / audit 输出，或者新增独立 exporter phase；优先级 `P1`。现在 build 能看到什么、runtime 又额外接受什么，仍然需要读源码才能知道。
- `UnrealCSharp` 和 `UnLua` 值得借鉴的是**显式 artifact lifecycle**：谁开始生成、谁写文件、谁结束生成都清楚可见。对 current AS 来说，可把这套“开始/结束/陈旧文件清理” contract 引入 `AngelscriptUHTTool` 的外围工具层，优先级 `P1`。
- `puerts` 的 “button + console command + analyzer” 三层入口适合 current AS 参考为 `P2`；`sluaunreal` 的 external supplement path 则更适合保留为补位路线，不应成为默认主路径。

### [D4] reload 的收口点：谁在换入前判资格，谁在换入后把世界缝合回去

前面已经比较过 safe point、watcher 和状态保持。这一轮只补 **reload closure owner**：改动进入系统以后，最后到底是哪一层负责把世界缝回一致状态。`Hazelight` 和 `当前 Angelscript` 仍然是 transaction-style compile/swap 路线，但 current 已经把 **资格判定、依赖传播、换入后 import 修复、失败回滚** 全部收进插件自己的闭环；其余参考插件则分别把 closure 收敛到 `UE reinstance`、`Lua require reload`、`JS env/module reload` 或 `host bytes provider`。

#### 各插件实现概览

```
Reload Closure
HZ    : changed files -> Preprocessor -> CompileModules -> keep old code on failure    // transaction gate
AS    : classify reload req -> propagate deps -> swap modules -> resolve imports        // strongest closure
UC    : regenerate dynamic class -> ReplaceInstancesOfClass                              // reinstance owner
UL    : EnvLocator.HotReload -> DoString("UnLua.HotReload()")                           // Lua env owner
PU    : watcher/hash -> ReloadSource/ReloadModule ; C++ hot reload -> MakeSharedJsEnv   // JS env owner
SL    : host setLoadFileDelegate(module -> bytes path)                                   // host boundary
```

#### 详细对比

##### 子维度 1：换入前有没有正式的资格判定和拒绝路径

- `Hazelight (UEAS2)` inspected 到的主入口仍是经典 transaction gate：`PerformHotReload()` 先收集 changed files，再走 `Preprocessor.Preprocess()`，预处理失败就明确打印 “Keeping all old angelscript code.”。这意味着它的第一责任是**不让坏 reload 进入 swap**。
- `当前 Angelscript` 延续了这条主线，但把 classification 做得更细。`AngelscriptClassGenerator.cpp` 不再把“新类但允许 soft path 创建”一律抬成 `FullReloadRequired`，而是允许降到 `FullReloadSuggested`；之后再把 requirement 递归传播给依赖者。这里的 owner 已经不是“单类局部判断”，而是**依赖图级别的 reload contract**。
- `UnrealCSharp / UnLua / puerts / sluaunreal` 在本轮 inspected 路径里都没有看到与 `EReloadRequirement` 对等的公开 classifier。它们的 closure 更像“先接受变化，再由 reinstance / VM reload / bytes loader 去收口”，而不是先做结构化资格分级。

[1] `Hazelight` 和 `当前 Angelscript` 都有 compile gate，但 current 额外把 requirement 传播与 post-swap repair 做成插件内闭环。

```cpp
// ============================================================================
// [1] 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\AngelscriptManager.cpp
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\ClassGenerator\AngelscriptClassGenerator.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: AngelscriptManager.cpp:1161-1257; UEAS2 AngelscriptClassGenerator.cpp:1058-1064;
//       current AngelscriptClassGenerator.cpp:1063-1082,2040-2076; current AngelscriptEngine.cpp:4057-4085
// 说明: HZ 提供 transaction gate；current 把 reload req 传播与换入后 repair 进一步插件化
// ============================================================================
bool FAngelscriptManager::PerformHotReload(ECompileType CompileType, const TArray<FFilenamePair>& InReloadFiles)
{
    ...
    bool bPreprocessSuccess = Preprocessor.Preprocess();
    if (!bPreprocessSuccess)
    {
        UE_LOG(Angelscript, Error, TEXT("Hot reload failed in preprocessing. Keeping all old angelscript code."));
        PreviouslyFailedReloadFiles.Append(FileList);
        EmitDiagnostics();
        return false;                                                // ★ HZ：坏改动在 swap 前被挡住
    }

    ECompileResult Result = CompileModules(CompileType, Preprocessor.GetModulesToCompile(), CompiledModules);
    ...
}

if (ClassData.OldClass->SuperClass != ClassData.NewClass->SuperClass)
{
    if (ClassData.ReloadReq < EReloadRequirement::FullReloadRequired)
        ClassData.ReloadReq = EReloadRequirement::FullReloadRequired; // ★ HZ：结构变化直接抬高 reload req
}

if (ReplacedClass != nullptr)
{
    // current：新类允许降到 suggested，让 SoftReloadOnly 仍可换入
    if (ClassData.ReloadReq < EReloadRequirement::FullReloadSuggested)
        ClassData.ReloadReq = EReloadRequirement::FullReloadSuggested;
}

ResolvePendingReloadDependees(&ClassData);                           // ★ current：把 reload req 递归推给依赖者

if (!ShouldUseAutomaticImportMethod())
{
    ResolveAllDeclaredImports();                                     // ★ current：成功 swap 后主动修补 import
}
else
{
    ScriptModule->UpdateReferencesInReflectionDataOnly(ReverseUpdateMap);
    UpdateScriptReferencesInUnrealData(ReverseUpdateMap, Module);
    ScriptModule->UpdateReferencesInScriptBytecode(ReverseUpdateMap); // ★ current：失败时再把旧引用缝回去
}
```

##### 子维度 2：换入后的“最终缝合”到底交给谁

- `UnrealCSharp` 的 closure owner 是 `FBlueprintCompileReinstancer::ReplaceInstancesOfClass`。也就是说，它收口的中心不在脚本 module graph，而在 **UE class/object replacement**。
- `UnLua` 则把 closure 压缩进 Lua env：`FLuaEnv::HotReload()` 只是执行 `UnLua.HotReload()`；`ULuaEnvLocator_ByGameInstance::HotReload()` 再把这件事 fan-out 给多个 env。它的优点是简单，代价是 reload contract 更依赖 Lua 侧脚本库。
- `puerts` 有两条不同 closure。JS 源文件改动时，`SourceFileWatcher` 做 MD5 去重后触发 `ReloadSource/ReloadModule`；而 C++ hot reload 时，`PuertsModule.cpp` 直接 `MakeSharedJsEnv()` 重建整个 env。也就是说，它把 closure 主要收敛在 **JS env / module cache**。
- `sluaunreal` inspected 到的公开边界更靠宿主：`MyGameInstance.cpp` 用 `setLoadFileDelegate()` 决定模块名如何映射到 `.lua/.luac` 字节。也就是说，这条链真正 owning reload / hotupdate 字节入口的是**宿主项目**，不是插件本体的事务系统。

[2] `UnrealCSharp` 的收口点是 UE reinstance，而不是 file-diff classifier。

```cpp
// ============================================================================
// [2] 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicClassGenerator.cpp
// 位置: 435-452
// 说明: UC 在 inspected 路径里的 closure owner 是 ReplaceInstancesOfClass
// ============================================================================
FReplaceInstancesOfClassParameters ReplaceInstancesOfClassParameters;
ReplaceInstancesOfClassParameters.OriginalCDO = InOldClass->GetDefaultObject(false);
FBlueprintCompileReinstancer::ReplaceInstancesOfClass(InOldClass, InNewClass, ReplaceInstancesOfClassParameters);
// ★ UC：真正把世界缝回去的是 UE 现有 reinstance 设施
```

[3] `UnLua / puerts / sluaunreal` 的 closure 分别落在 Lua env、JS env 和 host bytes loader。

```cpp
// ============================================================================
// [3] 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaLib.cpp
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnvLocator.cpp
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/SourceFileWatcher.cpp
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp
// 文件: Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp
// 文件: Reference/sluaunreal/Source/democpp/MyGameInstance.cpp
// 位置: UnLuaLib.cpp:51-59; LuaEnv.cpp:448-450; LuaEnvLocator.cpp:76-81;
//       SourceFileWatcher.cpp:57-80; JsEnvImpl.cpp:1468-1516; PuertsModule.cpp:424-438;
//       MyGameInstance.cpp:41-56
// 说明: UL 是 require()/env reload，PU 是 watcher/hash + env rebuild，SL 是宿主 bytes provider
// ============================================================================
if (luaL_dostring(L, "require('UnLua.HotReload').reload()") != 0)
{
    LogError(L);
}

void FLuaEnv::HotReload()
{
    DoString("UnLua.HotReload()");                                   // ★ UL：closure 压在 Lua env
}

void ULuaEnvLocator_ByGameInstance::HotReload()
{
    if (Env)
        Env->HotReload();
    for (const auto& Pair : Envs)
        Pair.Value->HotReload();                                     // ★ UL：多 env fan-out 也是 locator 负责
}

if (WatchedFiles[Dir][FileName] != Hash)
{
    OnWatchedFileChanged(NotifyPath);
    WatchedFiles[Dir][FileName] = Hash;                              // ★ PU：先靠 watcher/hash 去抖，再触发 reload
}

void FJsEnvImpl::ReloadModule(FName ModuleName, const FString& JsSource)
{
    JsHotReload(ModuleName, JsSource);                               // ★ PU：模块热换入在 JS env 内完成
}

FCoreUObjectDelegates::ReloadCompleteDelegate.AddLambda(
    [&](EReloadCompleteReason)
    {
        if (Enabled)
        {
            MakeSharedJsEnv();                                       // ★ PU：C++ hot reload 时直接重建整套 env
        }
    });

state->setLoadFileDelegate([](const char* fn, FString& filepath)->TArray<uint8> {
    FString path = FPaths::ProjectContentDir();
    path /= "Lua";
    path /= filename.Replace(TEXT("."), TEXT("/"));
    ...
    FFileHelper::LoadFileToArray(Content, *fullPath);                // ★ SL：公开 seams 在“模块名 -> 字节”映射
});
```

#### 对比矩阵

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 显式 `ReloadRequirement` 分类器 | Full | None | None | None | None | Full |
| changed-file 先经过 preprocess/compile gate，失败保留旧状态 | Full | None | None | None | None | Full |
| `ResolveAllDeclaredImports` 级别的 plugin-owned post-swap repair | None | None | None | None | None | Full |
| runtime env/module reload 是 primary closure | None | None | Full | Full | None | None |
| C++ hot reload 直接重建整套脚本 runtime env | None | None | None | Full | None | None |
| host-supplied bytes loader 是公开热更边界 | None | None | None | None | Full | None |

#### 小结与建议

- 本轮新增的核心结论是：`当前 Angelscript` 在 D4 上的优势不只是“能热重载”，而是**把 reload closure 做成了完整的三段式 contract：资格分类 -> 依赖传播 -> swap 后 repair / 失败回滚**。 inspected 到的其他参考插件都只覆盖了这三段中的一段或两段。
- `UnrealCSharp` 值得借鉴的是 `ReplaceInstancesOfClass` 这类**聚焦 UE 对象层的缝合手段**，优先级 `P2`。它不替代 current 的 transaction pipeline，但可能适合某些更窄的 editor-only 换入路径。
- `UnLua` 和 `puerts` 的价值在于它们提醒 current：**轻量 env-level reload** 仍然是好用的开发期快捷通道。建议把这类快捷路径保留为 editor/dev mode 优化，而不是拿来替换正式 reload contract，优先级 `P2`。
- `sluaunreal` 的 `bytes provider` 更像部署 ABI，不应误判成等价的 hot reload 系统。对 current AS 来说，它更适合作为 D11 的备选交付 seam，而不是 D4 的主参考。

### [D7/D10] 脚本入口的 canonical source-of-truth：作者第一次编辑的到底是 asset、source、path 还是 tool

前面已经比较过菜单、面板和示例数量。这一轮只补一个更“产品形态”的问题：**作者第一次碰到的对象是什么，机器第二次还能不能无歧义地继续接管**。`Hazelight / 当前 Angelscript` 仍是 asset-first，但 current 额外拥有 machine-facing 的 `ChangedScript(s)` commandlet contract；`UnrealCSharp` 是 source-first；`UnLua` 把 truth location 压缩进 Blueprint 上的 `GetModuleName`；`puerts` 更像 analyzer/path-first；`sluaunreal` 则是 tool/host-first。

#### 各插件实现概览

```
Authoring Truth Location
HZ/AS : UASClass -> ShowCreateBlueprintPopup -> modal SaveAssetDialog -> Blueprint/DataAsset // asset-first
AS    : ChangedScript(s) CLI -> NormalizePath -> Module.CodeSection matching                  // machine-first sidecar
UC    : toolbar -> new-class dialog -> write .cs -> open source                               // source-first
UL    : Blueprint GetModuleName -> template path -> Content/Script                            // asset-local path
PU    : CodeAnalyze env + SourceFileWatcher on loaded JS files                                // analyzer/path-first
SL    : LuaWrapper button + setLoadFileDelegate(module -> bytes path)                         // tool/host-first
```

#### 详细对比

##### 子维度 1：人类作者的第一步动作到底是什么

- `Hazelight (UEAS2)` 和 `当前 Angelscript` 的第一步动作仍然是显式 modal popup：`ShowCreateBlueprintPopup()` 先决定 asset path，再走 `CreateModalSaveAssetDialog`，最后生成 `Blueprint / DataAsset`。这是一条非常典型的 **asset-first** authoring flow。
- `UnrealCSharp` 则相反。toolbar 直接映射 `OpenNewDynamicClass`，然后起 `SDynamicNewClassDialog`，模板化写出新的 `.cs` 文件并打开源码。第一步动作不是“创建一个 asset”，而是**生成一个新源文件**。
- `UnLua` 的第一步动作通常是 Blueprint 里绑定路径。官方 Quickstart 明确告诉用户：路径入口就是 `GetModuleName`；editor toolbar 在生成模板时也先回读这个函数。也就是说，authoring truth location 是 **asset-local function**，而不是外部配置表。
- `puerts` 在 inspected 路径里没有对等 new-type wizard。它更像“先有 JS/TS 源，再由 `CodeAnalyze` 和 watcher 接管路径变化”。用户面第一眼看到的是 `.d.ts` generator 和后台 analyzer，而不是 create-type dialog。
- `sluaunreal` 的显式入口是 `LuaWrapper` 按钮；真正的模块路径到字节文件映射，则在宿主 `setLoadFileDelegate()` 里完成。也就是说，它把 authoring truth location 分裂成 **tool button + host path mapping** 两块。

[1] `Hazelight / 当前 Angelscript` 的第一作者动作仍是 asset-first modal popup。

```cpp
// ============================================================================
// [1] 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptEditor\Private\AngelscriptEditorModule.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 位置: UEAS2 AngelscriptEditorModule.cpp:437-505; current AngelscriptEditorModule.cpp:418-517
// 说明: HZ 与 current AS 的人类作者入口都是显式 create-asset popup
// ============================================================================
FString AssetPath;
if (FAngelscriptCodeModule::GetEditorGetCreateBlueprintDefaultAssetPath().IsBound())
    AssetPath = FAngelscriptCodeModule::GetEditorGetCreateBlueprintDefaultAssetPath().Execute(Class);
...
FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
FString SaveObjectPath = ContentBrowserModule.Get().CreateModalSaveAssetDialog(SaveAssetDialogConfig);
// ★ HZ：先让作者明确落哪个 asset path，再继续

if (FAngelscriptRuntimeModule::GetEditorGetCreateBlueprintDefaultAssetPath().IsBound())
    AssetPath = FAngelscriptRuntimeModule::GetEditorGetCreateBlueprintDefaultAssetPath().Execute(Class);
...
Asset = FKismetEditorUtilities::CreateBlueprint(
    Class, Package, AssetName, BPTYPE_Normal,
    BlueprintClass, BlueprintGeneratedClass, FName("AngelscriptCreateBlueprint"));
// ★ current AS：同一资产化入口仍然保留，只是 owner 从 CodeModule 收到 RuntimeModule
```

[2] `当前 Angelscript` 同时还拥有 machine-facing 的 `ChangedScript(s)` contract，这点和 HZ、UL、UC、PU、SL 都不一样。

```cpp
// ============================================================================
// [2] 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp
// 位置: AngelscriptBlueprintImpactScanner.cpp:71-109; AngelscriptBlueprintImpactScanCommandlet.cpp:63-97
// 说明: current AS 同一插件里同时产品化了 human-facing asset flow 与 machine-facing changed-script flow
// ============================================================================
TArray<FString> NormalizeChangedScriptPaths(const TArray<FString>& ChangedScripts)
{
    TSet<FString> UniquePaths;
    ...
    Result.Sort();
    return Result;                                                   // ★ 先把 path 变成稳定集合
}

for (const FAngelscriptModuleDesc::FCodeSection& CodeSection : Module->Code)
{
    if (NormalizedChangedScripts.Contains(NormalizeScriptPath(CodeSection.RelativeFilename)))
    {
        MatchingModules.Add(Module);                                 // ★ 再按 CodeSection 做 module 命中
        break;
    }
}

if (FParse::Value(*Params, TEXT("ChangedScript="), ChangedScriptsValue))
{
    AppendChangedScriptsFromDelimitedValue(ChangedScriptsValue, Request.ChangedScripts);
}
if (FParse::Value(*Params, TEXT("ChangedScriptFile="), ChangedScriptsFile))
{
    TryReadChangedScriptsFile(ChangedScriptsFile, Request.ChangedScripts);
}
// ★ current AS：机器入口不是隐式 watcher，而是正式 CLI contract
```

##### 子维度 2：路径/脚本事实最终写在哪

- `UnrealCSharp` 的 truth location 是新源码文件本身。dialog 成功后直接 `SaveStringToFile()` 并 `OpenSourceFile()`，等于明确告诉用户：**真正的类型事实在 `.cs` 文件**。
- `UnLua` 则把事实写进 Blueprint 的 `GetModuleName`。官方文档教你修改它，toolbar 也从它回读路径后生成模板。路径的 human-readable owner 非常集中。
- `puerts` 的 truth location 更偏 path/analyzer。`PuertsEditorModule` 启动 `CodeAnalyze`，加载过的源文件再由 `SourceFileWatcher` 接管。这里 canonical truth 不是某个 asset 上的函数，而是**module loader + loaded path set**。
- `sluaunreal` 的 truth location 则在宿主字节路径映射。按钮只负责叫起工具，真正 `module -> bytes` 的事实在 `setLoadFileDelegate()`。

[3] `UnrealCSharp` 是典型的 source-first；`UnLua` 则把 path truth 压缩进 Blueprint 上的 `GetModuleName`。

```cpp
// ============================================================================
// [3] 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpPlayToolBar.cpp
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/NewClass/DynamicNewClassUtils.cpp
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/NewClass/SDynamicNewClassDialog.cpp
// 文件: Reference/UnLua/Docs/CN/Quickstart_For_UE_Newbie.md
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaModuleLocator.cpp
// 位置: UnrealCSharpPlayToolBar.cpp:50-56; DynamicNewClassUtils.cpp:12-29; SDynamicNewClassDialog.cpp:773-781;
//       Quickstart_For_UE_Newbie.md:11-20; UnLuaEditorToolbar.cpp:265-310; LuaModuleLocator.cpp:37-42
// 说明: UC 的 truth 在新源码文件；UL 的 truth 在 Blueprint `GetModuleName`
// ============================================================================
CommandList->MapAction(
    FUnrealCSharpEditorCommands::Get().OpenNewDynamicClass,
    FExecuteAction::CreateLambda([]
    {
        FDynamicNewClassUtils::OpenAddDynamicClassToProjectDialog(
            FUnrealCSharpFunctionLibrary::GetGameDirectory());
    }));

FDynamicNewClassUtils::GetDynamicClassContent(ParentClass, NewClassName, NewClassContent);
FUnrealCSharpFunctionLibrary::SaveStringToFile(*GetDynamicFileName(), NewClassContent);
FSourceCodeNavigation::OpenSourceFile(GetDynamicFileName());        // ★ UC：事实最终落在新 .cs 文件

如果下次需要修改绑定的路径，可以找到 `GetModuleName` 函数并双击进行修改
点击UnLua菜单栏中的“生成Lua模板文件”，会在工程 `Content/Script` 目录下生成
// ★ UL：文档直接把 path truth 指向 Blueprint 上的 GetModuleName

const auto Func = Class->FindFunctionByName(FName("GetModuleName"));
Class->GetDefaultObject()->ProcessEvent(Func, &ModuleName);
const auto RelativePath = ModuleName.Replace(TEXT("."), TEXT("/"));
const auto FileName = FString::Printf(TEXT("%s%s.lua"), *GLuaSrcFullPath, *RelativePath);
// ★ UL：toolbar 从同一个函数回读路径，再写模板文件

return IUnLuaInterface::Execute_GetModuleName(CDO);                 // ★ UL：locator 也把它当 canonical source
```

[4] `puerts` 和 `sluaunreal` 的可见入口更像 analyzer/tool，而不是 asset/source-first wizard。

```cpp
// ============================================================================
// [4] 文件: Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/SourceFileWatcher.cpp
// 文件: Reference/sluaunreal/Plugins/lua_wrapper/Source/lua_wrapper/Private/lua_wrapperCommands.cpp
// 文件: Reference/sluaunreal/Plugins/lua_wrapper/Source/lua_wrapper/Private/lua_wrapper.cpp
// 文件: Reference/sluaunreal/Source/democpp/MyGameInstance.cpp
// 位置: PuertsEditorModule.cpp:138-150; SourceFileWatcher.cpp:22-48; lua_wrapperCommands.cpp:7-10;
//       lua_wrapper.cpp:122-128; MyGameInstance.cpp:41-56
// 说明: PU 是 analyzer/path-first；SL 是 tool button + host path mapping
// ============================================================================
JsEnv = MakeShared<PUERTS_NAMESPACE::FJsEnv>(..., [this](const FString& InPath)
{
    if (SourceFileWatcher.IsValid())
    {
        SourceFileWatcher->OnSourceLoaded(InPath);
    }
}, TEXT("--max-old-space-size=2048"));
JsEnv->Start("PuertsEditor/CodeAnalyze");                           // ★ PU：先起 analyzer env

void FSourceFileWatcher::OnSourceLoaded(const FString& InPath)
{
    FString Dir = FPaths::GetPath(InPath);
    ...
    WatchedFiles[Dir].Add(FileName, Hash);                          // ★ PU：path truth 在 loaded file set
}

UI_COMMAND(OpenPluginWindow, "LuaWrapper", "Generate Lua Interface (Windows only)", EUserInterfaceActionType::Button, FInputGesture());
auto cmd = contentDir + TEXT("/../Tools/lua-wrapper.exe");
system(TCHAR_TO_UTF8(*cmd));                                        // ★ SL：第一入口是 external tool button

state->setLoadFileDelegate([](const char* fn, FString& filepath)->TArray<uint8> {
    FString path = FPaths::ProjectContentDir();
    path /= "Lua";
    path /= filename.Replace(TEXT("."), TEXT("/"));
    ...
});                                                                 // ★ SL：真正的 truth 在宿主路径映射
```

#### 对比矩阵

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 显式 modal create-type / create-asset 向导 | Full | Full | None | None | None | Full |
| primary first action 直接写新的源码文件 | None | Full | Partial | None | None | None |
| asset-local function/path 是 canonical truth | None | None | Full | None | None | None |
| 同一插件同时提供 human-facing 创建流与 machine-facing changed-script contract | None | None | None | None | None | Full |
| analyzer / external tool 是主要可见入口 | None | None | Partial | Full | Full | None |

#### 小结与建议

- 这一轮新增最重要的判断是：`当前 Angelscript` 的独特性不只在于 asset-first，而在于**同一插件里同时拥有 human-facing 的 `ShowCreateBlueprintPopup()` 和 machine-facing 的 `ChangedScript(s)` commandlet contract**。这是目前五个参考插件里最完整的双入口形态。
- `UnLua` 值得 current AS 参考的是 **asset-local truth location**。如果后续希望作者在单个 Blueprint 上更容易回看脚本来源，可以考虑增加一个更显式的 asset-local path/alias 入口，而不是只让路径事实停留在 `CodeSection.RelativeFilename` 与外部脚本文件上；优先级 `P1`。
- `UnrealCSharp` 的 **source-first scaffold** 仍然值得继续吸收，尤其适合作为 current AS 的可选第二入口；优先级 `P1`。这样既不破坏现有 asset-first 流，也能降低“先手写一个 `.as` 类”的摩擦。
- `puerts / sluaunreal` 则提醒 current：analyzer/background sync 和 external tool 适合做高阶辅助，但不适合替代主入口。对 AS 来说，这类能力更适合作为 `P2` 辅助层，而不是默认 authoring truth。

---

## 深化分析 (2026-04-09 07:32:17)

### [D2/D8] 调用面快路径的 authority：谁把调用成本前移到生成/缓存阶段，谁保留解释式兜底

前面已经比较过“谁有生成器”“谁有 reflective fallback”“谁有 StaticJIT”。这一轮只补更贴近运行时成本的一层：**高频 `UFunction` 调用真正在哪一层被 specialization**。`Hazelight / 当前 Angelscript` 把主快路径提前到 bind table，再由 `StaticJIT` 处理脚本侧二次降本；`UnrealCSharp` 把静态 surface 生成给 C#，但参数编解码仍落在 runtime descriptor；`UnLua / puerts / sluaunreal` 则明显更依赖 runtime translator / cache，而不是 compile-time 直接固化。

#### 各插件实现概览

```
Callsite Specialization
HZ    : reflected map -> direct bind -> StaticJIT script-side AOT              // 无插件内 reflective fallback
AS    : function table -> direct bind ; miss -> reflective fallback ; script -> StaticJIT
UC    : generated InternalCall declarations -> runtime function descriptor marshal
UL    : FunctionDesc + PropertyDesc cache -> ProcessEvent / CallRemoteFunction
PU    : StructWrapper cache -> FunctionTranslator -> FastCall / SlowCall
SL    : LuaFunctionAccelerator + Lua cache tables ; wrapper supplements gaps
```

#### 详细对比

##### 子维度 1：native callable surface 是在生成期就被固定，还是运行时再解释

- `当前 Angelscript` 已经把 `BlueprintCallable` 主路径拆成两层。第一层由 `AS_FunctionTable_*` 生成物和 `FFuncEntry` 提供 direct native pointer；第二层仅在 direct entry 缺失时才进入 `BindBlueprintCallableReflectiveFallback()`。这不是简单的“有 fallback”，而是**把 direct bind 视为主合同，把 reflective path 明确降级成补位合同**。
- `Hazelight (UEAS2)` 在 inspected 路径里仍然只有 reflected map -> `BindMethodDirect()` 这一条主快路径，没有 current 这种插件内 reflective fallback 分支。也就是说，`HZ` 的 specialization 更“纯”，但也更依赖 reflected map 完整性。
- `UnrealCSharp` 的 generated artifact 只把 callable face 变成 `[MethodImpl(MethodImplOptions.InternalCall)]` 的托管声明；真正的参数读取、`OutParm` 链构造和 `FFrame` 解释仍发生在 `FCSharpFunctionDescriptor::CallCSharp()`。因此它是 **surface compile-time, marshal runtime** 的混合模式，不等于 `HZ/AS` 这种原生函数指针直接入表。
- `UnLua` 的 `FFunctionDesc` 在构造阶段就缓存了 `FPropertyDesc`、`ReturnPropertyIndex`、`OutPropertyIndices` 和参数 buffer factory，但调用仍经由 `PreCall() -> ProcessEvent()/CallRemoteFunction() -> PostCall()`。它比纯反射直调更快，但 specialization owner 明确在 runtime descriptor。
- `puerts` 也不是纯解释式。`FStructWrapper` 会缓存 `FPropertyTranslator` / `FFunctionTranslator`，然后在调用点按 `native && !net && !ubergraph` 分成 `FastCall()` 和 `SlowCall()`。因此它的 optimization grain 在 **per-UFunction runtime translator**，而不是 build-time generated direct pointer。
- `sluaunreal` 同样把热点收口在 runtime cache。`LuaFunctionAccelerator::findOrAdd()` 以 `UFunction*` 为 key 缓存参数检查器和引用信息，`LuaObject::cacheFunction()` / `cachePropertyOperator()` 再把查找到的函数和属性访问器塞回 Lua 用户值或元表。它还有 `LuaWrapper` 补位，但 wrapper 不是主通路 owner。

[1] `当前 Angelscript` 与 `Hazelight` 的 direct bind 主链对比：current 明确分叉出 reflective fallback，HZ 没有。

```cpp
// ============================================================================
// [1] 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Binds\Bind_BlueprintCallable.cpp
// 位置: current Bind_BlueprintCallable.cpp:72-90,95-125;
//       current BlueprintCallableReflectiveFallback.cpp:374-420;
//       HZ Bind_BlueprintCallable.cpp:62-110
// 说明: current AS 先找 direct pointer，缺失时退到 reflective fallback；HZ 只有 direct bind 主链
// ============================================================================
auto* DirectNativePointer = &Entry->FuncPtr;
const bool bHasDirectNativePointer = DirectNativePointer != nullptr && DirectNativePointer->IsBound();
if (!bHasDirectNativePointer)
{
    if (!BindBlueprintCallableReflectiveFallback(InType, Function, Signature, *Entry))
        return;                                                      // ★ current AS：只有 direct 缺失才退到 fallback

    SCRIPT_NATIVE_UFUNCTION(Function, FPackageName::ObjectPathToObjectName(DBBind.UnrealPath), false);
    return;
}

FMemory::Memcpy(&ASFuncPtr, DirectNativePointer, sizeof(asSFuncPtr));
int FunctionId = FAngelscriptBinds::BindMethodDirect(
    Signature.ClassName,
    Signature.Declaration, ASFuncPtr,
    asCALL_CDECL_OBJFIRST, Entry->Caller);                           // ★ current AS：direct native pointer 直接入表

if (!ShouldBindBlueprintCallableReflectiveFallback(Function))
{
    return false;
}
...
if (!BindReflectiveFunction(InType, Signature, ReflectiveSignature))
{
    delete ReflectiveSignature;
    return false;
}
Entry.bReflectiveFallbackBound = true;                               // ★ current AS：fallback 是显式降级路径

FASBindFunctionPointers FunctionPointers;
FunctionPointers.FunctionCaller = asFunctionCaller((asFunctionCaller::FunctionCallerPtr)FuncInMap->CallerPtr);
...
int FunctionId = FAngelscriptBinds::BindMethodDirect(
    InType->GetAngelscriptTypeName(),
    Signature.Declaration, FunctionPointers.FunctionPointer, asCALL_THISCALL, FunctionPointers);
// ★ HZ：只有 reflected map -> direct bind，没有 current 这条 reflective fallback 分支
```

[2] `UnrealCSharp / UnLua / puerts / sluaunreal` 都更依赖 runtime descriptor 或 runtime cache，而不是 build-time 直接指针。

```cpp
// ============================================================================
// [2] 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FBindingClassGenerator.cpp
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/CSharpFunction.cpp
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FCSharpFunctionDescriptor.cpp
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaFunctionAccelerator.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaObject.cpp
// 位置: FBindingClassGenerator.cpp:767-839; CSharpFunction.cpp:5-11; FCSharpFunctionDescriptor.cpp:17-70;
//       FunctionDesc.cpp:31-73,171-233,279-347; StructWrapper.cpp:21-35,38-66,282-305;
//       FunctionTranslator.cpp:93-136,228-307; LuaFunctionAccelerator.cpp:33-60,145-178;
//       LuaObject.cpp:671-709,711-738,1309-1334
// 说明: 这四个插件都把热路径收敛到 runtime cache / translator；快不快取决于 descriptor 是否被复用
// ============================================================================
"\t\t[MethodImpl(MethodImplOptions.InternalCall)]\n"
"\t\tpublic static extern void %s(nint InObject, byte* %s, byte* %s);\n";
// ★ UC：先把 callable face 生成为 C# InternalCall 声明

if (const auto FunctionDescriptor = FCSharpEnvironment::GetEnvironment().GetOrAddFunctionDescriptor<
    FCSharpFunctionDescriptor>(GetTypeHash(Stack.CurrentNativeFunction)))
{
    FunctionDescriptor->CallCSharp(Context, Stack, RESULT_PARAM);   // ★ UC：真正 marshal 仍在 runtime descriptor
}

for (auto Property = static_cast<FProperty*>(Function->ChildProperties);
     *InStack.Code != EX_EndFunctionParms;
     Property = static_cast<FProperty*>(Property->Next))
{
    ...
    InStack.Step(InStack.Object, Property->ContainerPtrToValuePtr<uint8>(Params));
}                                                                   // ★ UC：运行时逐属性走 FFrame

FPropertyDesc* PropertyDesc = FPropertyDesc::Create(Property);
int32 Index = Properties.Add(TUniquePtr<FPropertyDesc>(PropertyDesc));
...
PreCall(L, NumParams, FirstParamIndex, CleanupFlags, Params, Userdata);
Object->UObject::ProcessEvent(FinalFunction, Params);
// ★ UL：构造期缓存 PropertyDesc，调用期仍走 PreCall/ProcessEvent/PostCall

auto Iter = MethodsMap.Find(InFunction->GetFName());
if (!Iter)
{
    FunctionTranslator = std::make_shared<FFunctionTranslator>(InFunction, false);
    MethodsMap.Add(InFunction->GetFName(), FunctionTranslator);
}
...
if ((Function->FunctionFlags & FUNC_Native) && !(Function->FunctionFlags & FUNC_Net) &&
    !CallFunctionPtr->HasAnyFunctionFlags(FUNC_UbergraphFunction))
{
    FastCall(Isolate, Context, Info, CallObject, CallFunctionPtr, Params);
}
else
{
    SlowCall(Isolate, Context, Info, CallObject, CallFunctionPtr, Params);
}
// ★ PU：translator cache + fast/slow 双路径，是 runtime specialization

LuaFunctionAccelerator::LuaFunctionAccelerator(UFunction* inFunc)
    : func(inFunc)
    , bLuaOverride(ULuaOverrider::isUFunctionHooked(inFunc))
{
    ...
    paramsChecker.Add(checkerInfo);                                 // ★ SL：按 UFunction 缓存参数检查器
}

auto ret = cache.Find(inFunc);
if (ret)
{
    return *ret;
}
...
cachePropertyOperator(L, up, cls, (void*)pusher, (void*)getChecker(up), false);
return pusher(L, up, up->ContainerPtrToValuePtr<uint8>(obj), 0, nullptr);
// ★ SL：命中后把函数/属性 operator 塞回 Lua cache，减少后续查找
```

##### 子维度 2：脚本 VM 侧有没有第二阶段 specialization

- `Hazelight` 和 `当前 Angelscript` 在插件层都拥有真正的第二阶段 specialization：不仅 native callable face 先 direct bind，脚本侧还会挂 `StaticJIT`，并在需要时写出 transpiled output 与 `PrecompiledScript.Cache`。这意味着它们的性能策略是 **native fast path + script-side AOT/JIT** 两段式。
- 其余四个参考插件在 inspected 路径里都没有对等的“插件自有脚本 AOT/JIT artifact”证据。`puerts` 最终可以受益于 V8/QuickJS 自身优化，但那是 runtime 自带能力；`UnrealCSharp` 依赖 CLR/Mono；`UnLua / sluaunreal` 也主要停留在 descriptor/cache 层，不是插件自己输出第二阶段机器友好产物。

#### 对比矩阵

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| pre-generated direct native-call surface | Full | Full | None | None | Partial | Full |
| runtime descriptor/cache 负责参数 marshal | Partial | Full | Full | Full | Full | Partial |
| 插件内 reflective fallback 处理 direct-entry miss | None | N/A | N/A | N/A | N/A | Full |
| plugin-owned second-stage script AOT/JIT artifact | Full | None | None | None | None | Full |

#### 小结与建议

- 这一轮新增最重要的结论是：`当前 Angelscript` 在 D2/D8 上真正领先的不是“Bind 文件多”，而是**已经形成三段式快路径 contract：function table direct bind -> reflective fallback 降级 -> StaticJIT 二次 specialization**。这个组合在参考插件里仍然最接近 `HZ`，但比 `HZ` 多了一条插件内 fallback 安全网。
- `puerts / UnLua / sluaunreal` 值得 current AS 吸收的不是它们放弃生成，而是**runtime descriptor/cache 的中间层**。建议为 current 的 reflective fallback 增加更细的 `per-UFunction marshal plan` 缓存，而不是所有 fallback 都走同一套泛化流程；优先级 `P1`。
- `UnrealCSharp` 提醒 current：**generated surface 与 runtime marshal 可以分层演进**。如果未来某些 callable family 很难进入 `AS_FunctionTable_*`，也可以先接受“surface 生成，marshal 运行时化”的中间态，而不必在 `direct bind` 与“完全没有”之间二选一；优先级 `P2`。
- `HZ/current AS` 的 `StaticJIT` 路线仍应保持 `P0`。其余参考插件本轮都没有提供等价的插件自有第二阶段脚本 specialization 证据，不值得为了“与主流一致”而弱化这条能力。

### [D5] 调试 transport 的 authority：谁定义远程调试 ABI，谁只是把外部调试器接进来

前面已经比较过断点、变量和 debugger metadata。这一轮只补**调试线缆本身**：外部工具到底通过什么 wire format / transport 连进来，以及这个 ABI 是插件自己定义的，还是脚本 runtime 自带的。这里的差异非常实在，因为它直接决定“插件能不能承载脚本专有概念”，例如 `DebugDatabase`、`ReplaceAssetDefinition`、`CreateBlueprint` 这类 generic inspector 根本不认识的消息。

#### 各插件实现概览

```
Debug Transport ABI
HZ    : TCP custom header [len][type][body] + custom message enum            // protocol V1 style
AS    : TCP custom envelope + versioned message enum + configurable port     // DebugServer V2
UC    : Mono soft debugger dt_socket                                         // CLR owns wire format
UL    : build-time debug switch only ; no plugin-owned remote transport found
PU    : V8 Inspector + protocol channel + WaitDebugger loop                  // inspector owns wire format
SL    : TCP profile server on 8081                                           // profiler, not breakpoint debugger
```

#### 详细对比

##### 子维度 1：wire format 是插件自定义，还是复用宿主 runtime 协议

- `当前 Angelscript` 的 `EDebugMessageType`、`FAngelscriptDebugMessageEnvelope`、`SerializeDebugMessageEnvelope()` 和 `TryDeserializeDebugMessageEnvelope()` 说明它明确拥有一条**插件自有二进制调试协议**。再往下看 message enum，会发现 transport 承载的不只是停继续/断点，还有 `RequestDebugDatabase`、`CreateBlueprint`、`ReplaceAssetDefinition` 等脚本专有 IDE 语义。
- `Hazelight (UEAS2)` 属于同一家族，但 framing 还停在更原始的 `[int32 length][uint8 type][body]` 头。也就是说，它同样自定义协议，只是 current 已经把 framing helper 和 envelope 概念正式提升成公共接口，属于明显的 V2 化。
- `UnrealCSharp` 完全不同。它不自己造 debug wire format，而是把 `Mono` 的 `soft debugger agent` 打开到 `dt_socket`，由 CLR 调试协议接管外部连接。好处是能直接复用成熟调试器；代价是协议层不理解 UE/插件专有调试对象。
- `puerts` 同样复用宿主 runtime 协议，但对象换成 `V8 Inspector`。`CreateV8Inspector(InDebugPort, &Context)`、`CreateV8InspectorChannel()`、`DispatchProtocolMessage()` 说明它的调试 ABI 直接贴着 inspector protocol，而不是 Puerts 自己重写一套。
- `UnLua` 在 inspected `Source/` 路径里只定位到 `bEnableDebug` 编译开关和 editor setting，没有定位到插件自有 socket / inspector server。因此这一轮不能把它判成与 `AS/HZ/UC/PU` 同级的正式远程调试 transport，只能保守记为“有 debug routine compile switch，但 transport 证据不足”。
- `sluaunreal` 的显式远程 transport 在 `slua_profile`，而且从类型名和消息流看是 **profile server**，不是 breakpoint debugger。把它误读成“等价的远程调试器”会高估其 D5 完成度。

[1] `当前 Angelscript` 与 `Hazelight` 都是插件自有 TCP 协议，但 current 已经把 framing 提升成显式 envelope helper。

```cpp
// ============================================================================
// [1] 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Debugging\AngelscriptDebugServer.h
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\AngelscriptManager.cpp
// 位置: current DebugServer.h:25-40,86-93,646-686; current AngelscriptEngine.cpp:528-529,1452-1455;
//       HZ DebugServer.h:23-37,71-78,571-587,599-607; HZ AngelscriptManager.cpp:391-393
// 说明: HZ/AS 都有自定义 TCP debugger，但 current 把协议 framing 正式化成 envelope API
// ============================================================================
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
};

struct FAngelscriptDebugMessageEnvelope
{
    EDebugMessageType MessageType = EDebugMessageType::Disconnect;
    TArray<uint8> Body;
};

ANGELSCRIPTRUNTIME_API bool SerializeDebugMessageEnvelope(EDebugMessageType MessageType, const TArray<uint8>& Body, TArray<uint8>& OutBuffer);
ANGELSCRIPTRUNTIME_API bool TryDeserializeDebugMessageEnvelope(TArray<uint8>& InOutBuffer, FAngelscriptDebugMessageEnvelope& OutEnvelope, bool& bOutHasEnvelope, FString* OutError = nullptr);
// ★ current AS：framing 已经是公开 helper，而不是隐式写死在 send path

FParse::Value(FCommandLine::Get(), TEXT("-asdebugport="), Config.DebugServerPort);
...
DebugServer = new FAngelscriptDebugServer(this, RuntimeConfig.DebugServerPort);
// ★ current AS：debug port 是正式 runtime config，server 直接挂到当前 engine owner

TArray<uint8> Buffer;
if (!SerializeDebugMessageEnvelope(MessageType, Body, Buffer))
{
    return;
}
// ★ current AS：发包统一走 envelope serializer

int32 MessageLength = Body.Num();
uint8 MessageTypeByte = (uint8)MessageType;
Writer << MessageLength;
Writer << MessageTypeByte;
Buffer.Append(Body);
// ★ HZ：仍然是手写 [len][type][body] 头部，属于同家族但更原始的 framing 版本

int Port = FAngelscriptManager::ConfigSettings->ConnectionPort;
FParse::Value(FCommandLine::Get(), TEXT("-asdebugport="), Port);
DebugServer = new FAngelscriptDebugServer(Port);
// ★ HZ：也是插件自有 TCP server，但 lifecycle 仍挂在 manager，而不是 engine owner
```

[2] `UnrealCSharp / puerts / UnLua / sluaunreal` 的 transport owner 分别落在 Mono、V8 Inspector、build flag，以及 profiler server。

```cpp
// ============================================================================
// [2] 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp
// 文件: Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.h
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/UnLua.Build.cs
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorSettings.h
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_profile/Private/slua_remote_profile.cpp
// 位置: FMonoDomain.cpp:96-110; PuertsModule.cpp:196-238; JsEnvImpl.cpp:347-373,619,4536-4581;
//       JsEnvImpl.h:99-107; UnLua.Build.cs:91-95; UnLuaEditorSettings.h:57-60;
//       slua_remote_profile.cpp:22-30,52-59,139-158
// 说明: UC/PU 复用外部 runtime 协议；UL 只有 debug 编译开关证据；SL 公开的是 profiler transport
// ============================================================================
const auto Config = FString::Printf(TEXT(
    "--debugger-agent=transport=dt_socket,server=y,suspend=n,address=%s:%d"
),
    *UnrealCSharpSetting->GetHost(),
    UnrealCSharpSetting->GetPort()
);
mono_jit_parse_options(sizeof(Options) / sizeof(char*), Options);
// ★ UC：直接把 Mono soft debugger agent 打开到 dt_socket，协议不由插件自定义

if (Settings.DebugEnable)
{
    JsEnv = MakeShared<PUERTS_NAMESPACE::FJsEnv>(
        std::make_shared<PUERTS_NAMESPACE::DefaultJSModuleLoader>(Settings.RootPath),
        std::make_shared<PUERTS_NAMESPACE::FDefaultLogger>(),
        DebuggerPortFromCommandLine < 0 ? Settings.DebugPort : DebuggerPortFromCommandLine);
}
...
if (Settings.WaitDebugger)
{
    JsEnv->WaitDebugger(Settings.WaitDebuggerTimeout);
}

Inspector = CreateV8Inspector(InDebugPort, &Context);
...
InspectorChannel = Inspector->CreateV8InspectorChannel();
InspectorChannel->DispatchProtocolMessage(TCHAR_TO_UTF8(*Message));
// ★ PU：wire format 直接来自 V8 Inspector，插件只负责接线与 bootstrap

loadBoolConfig("bEnableDebug", "UNLUA_ENABLE_DEBUG", false);
...
bool bEnableDebug = false;
// ★ UL：本轮 inspected source 只定位到 debug routine compile switch，未定位到插件自有远程 transport

FAutoConsoleVariableRef CVarSluaProfilerPort(
    TEXT("slua.ProfilerPort"),
    NS_SLUA::FProfileServer::Port,
    TEXT("Slua profiler server port.\n"),
    ECVF_Default);
int32 FProfileServer::Port = 8081;
...
Listener = new FTcpListener(ListenEndpoint);
Listener->OnConnectionAccepted().BindRaw(this, &FProfileServer::HandleConnectionAccepted);
// ★ SL：这里公开的是 profiler server，不是 breakpoint / single-step debugger
```

##### 子维度 2：bootstrap 和配置入口归谁管

- `当前 Angelscript` 与 `Hazelight` 都支持 `-asdebugport=`，但 current 已经把端口解析并入 `RuntimeConfig`，再在 engine 初始化时决定是否启动 `FAngelscriptDebugServer(this, RuntimeConfig.DebugServerPort)`。这说明它的 debug transport 已经是 engine-owned runtime config，而不是 manager 辅助开关。
- `UnrealCSharp` 的 bootstrap 在 `MonoDomain` 初始化阶段完成；`Host/Port` 来自 `UUnrealCSharpSetting`，不是 editor 临时面板。这更像“把 C# runtime 调试代理一起启动”，而不是 plugin 侧消息层。
- `puerts` 的 bootstrap 由 `PuertsModule` 调 `FJsEnv` / `FJsEnvGroup` 时完成，并支持 `DebugEnable`、`DebugPort`、`WaitDebugger` 和命令行覆盖；它把体验做得很完整，但协议 owner 仍然是 inspector。
- `sluaunreal` 的配置入口是 `slua.ProfilerPort` console variable，说明它更像工具链遥测口；`UnLua` 本轮只定位到 build setting，没有找到等价的 runtime bootstrap。

#### 对比矩阵

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 插件自定义远程调试协议 | Full | None | None | None | None | Full |
| 直接复用外部 runtime 调试协议 | None | Full | None | Full | None | None |
| runtime/config 中可见端口入口 | Full | Full | Partial | Full | Full | Full |
| 插件自有远程 breakpoint debugger server | Full | Partial | None | Partial | None | Full |
| 公开 transport 实际是 profiler / telemetry 而非 debugger | None | None | None | None | Full | None |

#### 小结与建议

- 这一轮新增最关键的判断是：`当前 Angelscript` 在 D5 上的核心资产不是“有断点”，而是**仍然拥有插件自定义的远程调试 ABI**。只要 `DebugDatabase`、`CreateBlueprint`、`ReplaceAssetDefinition` 这种脚本专有语义还要在线上传递，就不该轻易退回纯 `dt_socket` / pure inspector。
- `Hazelight` 证明这条 custom-TCP 路线本身是成熟的；`当前 Angelscript` 的增量在于**把 framing 正式化成 envelope helper 并把 owner 下沉到 engine runtime**。这条方向应保持 `P0`。
- `UnrealCSharp` 与 `puerts` 值得 current 借鉴的是**标准调试器接入方式**，不是它们的 wire format 本身。更现实的吸收方式是给 current custom protocol 增加 adapter/bridge，而不是替换掉底层协议；优先级 `P1`。
- `UnLua` 本轮没有找到正式 remote transport 证据，因此不应把它误判成“只是没写进文档”；`sluaunreal` 的 `slua_profile` 也不应误算成等价 debugger。对 current AS 来说，这两者更像“提醒你区分 debugger 与 profiler 的交付边界”，而不是直接参考实现。

---

## 深化分析 (2026-04-09 07:46:05)

前文已经把 `authority`、`freshness`、`deployment boundary` 大体写过。本轮不再重复这些结论，只补 3 个更底层、也更容易指导实现取舍的问题：`D4` 看“热重载到底记住了什么”，`D6` 看“生成产物具体落到哪一层、谁有权覆写”，`D11` 看“交付时真正依赖哪些文件名和路径 contract”。

### [D4] 热重载真正记住了什么

#### 各插件实现概览

```
[D4] Change Memory Model
HZ      : FailedFiles + QueuedFullReload + ReloadRequirement   // 失败文件、延迟全量重载、结构变更分级都入账
AS(now) : FailedFiles + QueuedFullReload + ReloadRequirement   // 同谱系，但 owner 已落到 engine-owned runtime
UC      : FileChanges + CompileTask queue                      // 记住的是待编译 C# 文件批次
UL      : DirectoryWatcher -> HotReload()                      // 触发即执行，缺少结构升级账本
PU      : WatchedFiles[dir][file]=MD5 -> ReloadSource()        // 记住的是已加载 JS 文件与 hash
SL      : requireModule() only                                 // 按需加载模块，没有统一 reload ledger
```

#### 详细对比

##### 子维度 1：变更记忆是“事务账本”还是“触发器缓存”

- `当前 Angelscript` 与 `Hazelight` 是这一轮里唯二把热重载做成正式事务账本的方案。两边都会把上次失败的文件重新并回 `FileList`，并把当前无法完成的结构性变更压进 `QueuedFullReloadFiles`，等允许时机再做 `FullReload`。差异不在模型，而在 owner：`Hazelight` 仍由 `FAngelscriptManager` 持有，`当前 Angelscript` 则由 `FAngelscriptEngine` 持有。
- `UnrealCSharp` 也会“记住变化”，但它记住的是 `FileChanges` 批次，而不是“哪些脚本已经进入不一致状态”。`FEditorListener` 先把 `.cs` 变化累积到 `FileChanges`，只有 editor 激活、且不在 `PIE / generating` 时才触发 `FCSharpCompiler::Compile(FileChanges)`；`FCSharpCompilerRunnable` 再把批次喂给 `FDynamicGenerator::Generator(FileChanges)`。这更像 debounce + batch compile，不是 typed reload transaction。
- `UnLua` 的记忆层最薄。editor watcher 只在 `HotReloadMode == Auto` 时调用一次 `UUnLuaFunctionLibrary::HotReload()`；运行时 `FLuaEnv::HotReload()` 直接执行 `UnLua.HotReload()`，脚本侧再通过替换 `_G.require` 接管模块重载。它有热重载，但没有像 AS/HZ 那样显式保留“待全量重载”的结构升级账本。
- `puerts` 介于两者之间。`FSourceFileWatcher` 会为已加载过的 `.js` 文件保存 MD5，并且只在 hash 变化时触发 `OnWatchedFileChanged()`；随后 `FJsEnvImpl::ReloadModule()` / `ReloadSource()` 调 JS 侧 `__reload`。因此它不是“完全无记忆”，只是记忆内容是 `loaded path + hash`，而不是“structural reload requirement”。
- `sluaunreal` 需要严格区分“能加载 Lua 模块”与“有统一热重载事务”。本轮 inspected 的 `slua_unreal.Build.cs` 依赖里没有 `DirectoryWatcher`，运行时主路径也只是 `LuaState::requireModule()` 与 `LuaOverrider::BindOverrideFuncs()` 拉起模块；这说明它有模块加载能力，但在当前插件核心里没有对等的 central reload ledger。

[1] `当前 Angelscript` 与 `Hazelight` 的 reload ledger 是同一家族语义：失败文件回并、结构变更升级、无法立即全量时延迟队列。

```cpp
// ============================================================================
// [1] 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\AngelscriptManager.cpp
// 位置: AngelscriptEngine.cpp:2253-2280,3936-3991,4168-4186;
//       AngelscriptManager.cpp:1161-1188,2619-2674,2848-2866
// 说明: 两边都把 failed file、soft/full reload 分级、deferred full reload 作为正式状态机
// ============================================================================
for (auto& FailedFile : PreviouslyFailedReloadFiles)
{
    // ★ 上一轮失败的文件不会丢，下一轮自动回并
    FileList.AddUnique(FailedFile);
}
PreviouslyFailedReloadFiles.Empty();

switch (ReloadReq)
{
    case FAngelscriptClassGenerator::EReloadRequirement::SoftReload:
        // ★ 纯代码改动可直接 soft reload
        ClassGenerator.PerformSoftReload();
        break;
    case FAngelscriptClassGenerator::EReloadRequirement::FullReloadRequired:
        if (CompileType == ECompileType::SoftReloadOnly)
        {
            // ★ 当前时机不能 full reload，就保留旧代码并把升级需求记账
            bShouldSwapInModules = false;
            bFullReloadRequired = true;
        }
        else
        {
            ClassGenerator.PerformFullReload();
        }
        break;
}

if (Result == ECompileResult::ErrorNeedFullReload)
{
    for (const auto& RepeatFile : AllCompiledFiles)
        QueuedFullReloadFiles.Add(RepeatFile); // ★ 延迟到允许时机再做 full reload
    PreviouslyFailedReloadFiles.Append(AllCompiledFiles);
}
```

[2] `UnrealCSharp / UnLua / puerts / sluaunreal` 的“记忆结构”分别落在 compile batch、脚本 trigger、loaded-file hash、以及纯 require path。

```cpp
// ============================================================================
// [2] 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp
// 文件: Reference/UnrealCSharp/Source/Compiler/Private/FCSharpCompilerRunnable.cpp
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorFunctionLibrary.cpp
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaLib.cpp
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/SourceFileWatcher.cpp
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/slua_unreal.Build.cs
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaState.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaOverrider.cpp
// 位置: FEditorListener.cpp:52-63,306-316,322-364;
//       FCSharpCompilerRunnable.cpp:125-154;
//       UnLuaEditorFunctionLibrary.cpp:27-35,112-117;
//       LuaEnv.cpp:448-450; UnLuaLib.cpp:51-54,266-268;
//       SourceFileWatcher.cpp:22-35,52-79; JsEnvImpl.cpp:1504-1536;
//       slua_unreal.Build.cs:78-104; LuaState.cpp:768-770; LuaOverrider.cpp:1194-1198
// 说明: UC 记住 file batch；UL/PU 记住 trigger 或 loaded-file hash；SL 没有 central watcher
// ============================================================================
FileChanges.Add(FileChange);
...
if (IsActive && !FileChanges.IsEmpty() && !bIsPIEPlaying && !bIsGenerating)
{
    FCSharpCompiler::Get().Compile(FileChanges); // ★ UC：把一批文件变化统一交给 compiler/generator
}
...
FileChanges.Append(InFileChangeData);
FDynamicGenerator::Generator(FileChanges);       // ★ UC：真正消费的是文件批次，不是 schema reload state

if (Settings.HotReloadMode != EHotReloadMode::Auto)
    return;
UUnLuaFunctionLibrary::HotReload();             // ★ UL：watcher 只负责触发
...
DoString("UnLua.HotReload()");                  // ★ UL：runtime 直接跳到脚本侧 reload
...
pcall(function() _G.require = require('UnLua.HotReload').require end) // ★ UL：脚本层接管 require

if (Change.Action == FFileChangeData::FCA_Modified && Change.Filename.EndsWith(TEXT(".js")))
{
    if (WatchedFiles[Dir][FileName] != Hash)
        OnWatchedFileChanged(NotifyPath);       // ★ PU：只对已加载过且 hash 变化的 JS 生效
}
...
JsHotReload(ModuleName, JsSource);              // ★ PU：module 粒度 reload，不区分 soft/full structural gate

PrivateDependencyModuleNames.AddRange(new string[]
{
    "CoreUObject", "Engine", "Slate", "SlateCore", "UMG", "InputCore", "NetCore"
    // ★ SL：本轮 inspected plugin core 未见 DirectoryWatcher 依赖
});
...
lua_getglobal(L, "require");
NS_SLUA::LuaVar luaModule = sluaState->requireModule(TCHAR_TO_UTF8(*luaFilePath)); // ★ SL：核心路径是 require，不是 reload ledger
```

#### 对比矩阵

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| failed-file retry ledger | Full | None | None | None | None | Full |
| deferred full-reload queue | Full | None | None | None | None | Full |
| structural soft/full gate | Full | None | None | None | None | Full |
| changed-file batch queue | Partial | Full | None | Partial | None | Partial |
| loaded-file hash dedupe | None | None | None | Full | None | None |
| central watcher absent but require-based load exists | None | None | None | None | Full | None |

#### 小结与建议

- `当前 Angelscript` 在 D4 上真正稀缺的资产不是“有 watcher”，而是**typed reload ledger**。`PreviouslyFailedReloadFiles + QueuedFullReloadFiles + ReloadRequirement` 这套账本必须继续保留，优先级 `P0`。
- `puerts` 值得吸收的是**loaded-file hash dedupe**。它适合放在 current AS 的文件进入 reload 队列之前，降低无意义重编译；但它只能做前置降噪，不能替代 structural gate，优先级 `P1`。
- `UnrealCSharp` 值得吸收的是**editor activation debounce**。如果 current AS 后续还要继续打磨 editor 体验，优先先借鉴这种“失焦期间只积压、不立即编译”的节流策略，而不是弱化现有 full-reload gate，优先级 `P1`。
- `UnLua / sluaunreal` 说明“能热更脚本”与“有可回滚的热重载事务”是两回事。对 current AS 这种会改写 `UClass/UFunction/FProperty` 的强类型插件，不应把 trigger-only 路线误判成同等能力。

### [D6] 生成产物到底落到哪一层，谁有权覆写

#### 各插件实现概览

```
[D6] Output Sink / Overwrite Permission
HZ      : StaticJIT text/cpp -> runtime generated files        // compiler-facing output
AS(now) : UHT shard cpp + json/csv -> compile output          // build graph + audit artifacts
UC      : .sln/.csproj/.cs -> project workspace               // user workspace first
UL      : Intermediate/IntelliSense/*.lua                     // IDE sidecar, write-if-changed
PU      : Project/Typing + Plugin/Content/JavaScript          // mirror + overwrite old artifacts
SL      : external lua-wrapper.exe + Tools/config.json        // sink delegated to external tool
```

#### 详细对比

##### 子维度 1：产物 sink 是 build graph、workspace，还是 sidecar

- `当前 Angelscript` 的 sink 最硬。`AngelscriptFunctionTableExporter` 直接作为 `UhtExporter(Name = "AngelscriptFunctionTable")` 运行，输出被限制为 `AS_FunctionTable_*.cpp`；`AngelscriptFunctionTableCodeGenerator` 同时再写 `AS_FunctionTable_Summary.json`、`AS_FunctionTable_ModuleSummary.csv`、`AS_FunctionTable_Entries.csv`。这意味着它不是“顺手吐一点提示文本”，而是**build-visible glue + machine-readable 审计产物**一起生成。
- `Hazelight` inspected 到的 generator sink 仍偏 compiler-facing。`AngelscriptStaticJIT.cpp` 在写输出文件时把 `ExternalDeclarations` 和 `extern` function declaration 直接拼进生成内容；这能服务编译器，但不是一个独立的 IDE artifact family。
- `UnrealCSharp` 的 sink 明确是 workspace。`FSolutionGenerator::Generator()` 从 template 树复制 `.csproj / .cs / .sln` 到项目工作区，随后 editor console command 再调 `FSolutionGenerator::Generator()` 和 `FCSharpCompiler::Compile()`。它的主要目标不是让 UE build graph 直接消费中间文件，而是先形成一套可编辑、可编译的 C# 工作区。
- `UnLua` 的 sink 则固定在 `Intermediate/IntelliSense`。它监听 `AssetRegistry`，把 `UBlueprint / UField` 导出成 `.lua` 提示文件；这些文件只服务 IDE / authoring，不会进入宿主 C++ 编译单元。
- `puerts` 的 sink 是双写的。它先删旧 `ue.d.ts / ue_bp.d.ts`，再把项目 `Typing/` 与 `Content/JavaScript` 镜像回插件，然后把新的声明写回项目目录。也就是说，一个生成动作同时服务 runtime JS 资产和 TS declaration。
- `sluaunreal` 的 sink 明显在插件外。editor 按钮点击后直接起 `lua-wrapper.exe`；真正导出哪些类型、从哪些头文件抓符号，由 `Tools/config.json` 控制。plugin module 只负责“拉起外部工具”，不是 generator lifecycle owner。

##### 子维度 2：覆写策略是保守保护，还是强制重建

- `当前 Angelscript` 对 build glue 采取强制重建。`factory.CommitOutput(...)` 会重新提交 shard，随后 `Directory.EnumerateFiles(..., "AS_FunctionTable_*.cpp")` 删除所有未在本轮 `generatedPaths` 里的旧 shard。这是典型的“可重建编译产物 owner”思路。
- `UnrealCSharp` 明显更保守。`CopyTemplate()` 只有在目标文件不存在，或者显式 `bReplaceExistingFile` 时才写入；这等于把 workspace 文件默认视为“用户可能会手改的脚手架”。
- `UnLua` 最克制。`SaveFile()` 会先读旧内容，只有内容变化才落盘；它避免 source control 噪音，也避免 IDE sidecar 每次全量改时间戳。
- `puerts` 最激进。声明生成前先删旧 `.d.ts`，再同步目录，再重写输出；这保证了 TS 侧不会吃到残留旧声明，但也意味着 typing/output owner 明确掌握在 generator 自己手里。
- `sluaunreal` 的覆写策略由外部 `lua-wrapper.exe` 决定；plugin 代码层本身没有 inspected 到等价的 `write-if-changed` 或 stale cleanup contract。

[1] `当前 Angelscript` 与 `Hazelight` 的 sink 都是 compiler-facing，但 current AS 已经把 summary/csv 一起做成正式产物，并显式清理过期 shard。

```cpp
// ============================================================================
// [1] 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\StaticJIT\AngelscriptStaticJIT.cpp
// 位置: AngelscriptFunctionTableExporter.cs:21-35;
//       AngelscriptFunctionTableCodeGenerator.cs:115-121,166-185,218-241,302-324,434-445;
//       AngelscriptStaticJIT.cpp:3671-3680
// 说明: current AS 的 sink 是 UHT compile output + summary/csv；HZ 的 sink 仍主要面向编译器
// ============================================================================
[UhtExporter(
    Name = "AngelscriptFunctionTable",
    CppFilters = ["AS_FunctionTable_*.cpp"],
    ModuleName = "AngelscriptRuntime")]
private static void Export(IUhtExportFactory factory)
{
    int generatedFileCount = AngelscriptFunctionTableCodeGenerator.Generate(factory); // ★ current AS：UHT 直接产出编译单元
}

string outputPath = factory.MakePath($"AS_FunctionTable_{module.ShortName}_{shardIndex:D3}", ".cpp");
factory.CommitOutput(outputPath, BuildShard(...));                                     // ★ shard cpp 进入 compile output
...
string summaryPath = factory.MakePath("AS_FunctionTable_Summary", ".json");
...
string csvPath = factory.MakePath("AS_FunctionTable_ModuleSummary", ".csv");
File.WriteAllText(csvPath, builder.ToString(), Encoding.UTF8);                         // ★ 同时保留 machine-readable 审计产物
...
builder.Append("AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_AS_FunctionTable_");
...
foreach (string existingFile in Directory.EnumerateFiles(outputDirectory, "AS_FunctionTable_*.cpp"))
{
    if (!generatedPaths.Contains(existingFile))
        File.Delete(existingFile);                                                     // ★ current AS：主动清理 stale shard
}

for (auto& Elem : File.ExternalDeclarations)
{
    FullContent.Append(Elem.Value + "\n");
}
FullContent.Append(FString::Printf(TEXT("extern %s;\n"), *GenerateData.FunctionDeclaration)); // ★ HZ：仍是 compiler-facing output
```

[2] `UnrealCSharp / UnLua / puerts / sluaunreal` 的 sink 分别是 workspace、Intermediate sidecar、project/plugin 双写目录，以及 external exe。

```cpp
// ============================================================================
// [2] 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FSolutionGenerator.cpp
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp
// 文件: Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp
// 文件: Reference/sluaunreal/Plugins/lua_wrapper/Source/lua_wrapper/Private/lua_wrapper.cpp
// 文件: Reference/sluaunreal/Tools/config.json
// 位置: FSolutionGenerator.cpp:9-24,58-70,128-152;
//       UnrealCSharpEditor.cpp:76-90;
//       UnLuaIntelliSenseGenerator.cpp:47-48,143-166,222-233;
//       DeclarationGenerator.cpp:417-451,1702-1705;
//       lua_wrapper.cpp:122-133; config.json:1-24
// 说明: UC/UL/PU/SL 的产物 owner 分别是 workspace、Intermediate、双写 mirror、external tool
// ============================================================================
CopyTemplate(FUnrealCSharpFunctionLibrary::GetCodeAnalysisProjectPath(), ...);         // ★ UC：工作区模板
...
CopyTemplate(..., TArray<TFunction<void(FString& OutResult)>>{ ... });
...
if (!FileManager.FileExists(*Dest) || bReplaceExistingFile)
{
    FUnrealCSharpFunctionLibrary::SaveStringToFile(*Dest, Result);                     // ★ UC：默认保护已有文件
}
...
FSolutionGenerator::Generator();
FCSharpCompiler::Get().Compile([](){});                                                // ★ UC：editor 入口显式串起 generator + compile

OutputDir = IPluginManager::Get().FindPlugin("UnLua")->GetBaseDir() + "/Intermediate/IntelliSense";
...
SaveFile(ModuleName, FileName, Content);                                               // ★ UL：sink 在 Intermediate/IntelliSense
...
if (FileContent != GeneratedFileContent)
    FFileHelper::SaveStringToFile(GeneratedFileContent, *FilePath, ...);               // ★ UL：write-if-changed

PlatformFile.DeleteFile(*(PuertsBaseDir / TEXT("Typing/ue/ue.d.ts")));
PlatformFile.CopyDirectoryTree(*ProjectTypingDir, *(PuertsBaseDir / TEXT("Typing")), false);
PlatformFile.CopyDirectoryTree(*(FPaths::ProjectContentDir() / TEXT("JavaScript")), ... , true);
FFileHelper::SaveStringToFile(ToString(), *UEDeclarationFilePath, ...);                // ★ PU：双写 mirror + 强制重建声明
...
TypeScriptDeclarationGenerator.GenTypeScriptDeclaration(true, true);

auto cmd = contentDir + TEXT("/../Tools/lua-wrapper.exe");
system(TCHAR_TO_UTF8(*cmd));                                                           // ★ SL：module 只负责拉起外部生成器
...
"export_files": { ... },
"filter": [ ... ]                                                                       // ★ SL：类型筛选 contract 在 config.json，不在 plugin code
```

#### 对比矩阵

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| build-visible generated C++ output | Full | None | None | Partial | Partial | Full |
| machine-readable generation summary | None | None | None | None | None | Full |
| proactive stale cleanup / overwrite | None | None | Partial | Full | None | Full |
| write-if-changed / no-overwrite protection | None | Full | Full | None | None | None |
| default sink under `Intermediate` | None | None | Full | None | None | None |
| external executable required for main generation path | None | None | None | None | Full | None |

#### 小结与建议

- `当前 Angelscript` 在 D6 上最值得保留的，不是“产物多”，而是**build-visible glue 与 machine-readable summary 同源生成**。这让 `FunctionTable` 既能参与编译，又能做审计，优先级 `P0`。
- `UnLua` 值得吸收的是**write-if-changed**。如果 current AS 未来继续扩展 docs/debug sidecar，优先把低噪声写盘策略补进去，而不是把 build glue 也改成保守不覆写，优先级 `P1`。
- `UnrealCSharp` 值得吸收的是**把用户会手改的脚手架和可重建产物严格分开**。current AS 如果后续补 starter template / IDE stub，应从第一天起就区分这两类 sink，优先级 `P1`。
- `puerts` 提醒当前 AS：如果 generator 同时在同步 authoring 资产与 runtime 资产，必须接受 owner 更强势、覆写更激进。current AS 若无强需求，不应把 TS/JS 式 mirror side-effect 直接搬进现有 UHT 链。
- `sluaunreal` 的 external tool 模式只适合“用户显式维护导出清单”的场景，不适合作为 current AS 的默认主路径。

### [D11] 交付真正依赖哪些文件名和路径 contract

#### 各插件实现概览

```
[D11] Delivery Filename Contract
HZ      : <ScriptRoot>/Binds.Cache + PrecompiledScript_{Cfg}.Cache
AS(now) : <ScriptRoot>/Binds.Cache + PrecompiledScript_{Cfg}.Cache
UC      : <ProjectContent>/<PublishDir>/{UE,Game,Custom}.dll
UL      : Content/Script/?.lua + Plugins/UnLua/Content/Script/?.lua
PU      : $(BinaryOutputDir)/v8*.dll + Content/JavaScript/*.js|*.mbc|*.cbc
SL      : Content/Lua/<module>.lua|.luac + Library/<Platform>/liblua*
```

#### 详细对比

##### 子维度 1：runtime 启动时真正找哪些文件

- `当前 Angelscript` 与 `Hazelight` 的路径 contract 高度一致：先读 `<ScriptRoot>/Binds.Cache`，再按 build config 优先查 `PrecompiledScript_Shipping.Cache`、`PrecompiledScript_Test.Cache`、`PrecompiledScript_Development.Cache`，最后回退 `PrecompiledScript.Cache`。差异在 current AS 把命令行开关正式收进 `RuntimeConfig`，而 Hazelight 仍是 manager 级静态路径。
- `UnrealCSharp` 的 contract 则是 `ProjectContent/<PublishDirectory>/*.dll`。`GetFullUEPublishPath()`、`GetFullGamePublishPath()` 和 `GetFullCustomProjectsPublishPath()` 明确列出运行时要消费哪些 DLL；`FCSharpEnvironment::Initialize()` 直接把 `GetFullAssemblyPublishPath()` 交给 `FDomain`。它的正式交付物是 assembly set，而不是脚本文本。
- `UnLua` 的 contract 是 loose source path。editor 会把 `Script`、`../Plugins/UnLua/Content/Script` 以及扩展插件脚本目录都加入 `DirectoriesToAlwaysStageAsUFS`；运行时的默认 `PackagePath` 只声明 `.lua` 搜索模式。也就是说，UnLua 的 deployment unit 是 staged Lua source tree。
- `puerts` 的 contract 分成两部分：native runtime 由 `JsEnv.Build.cs` 把 `v8.dll`、`v8_libplatform.dll` 等 staged 到 `$(BinaryOutputDir)`；脚本 payload 则来自 `Content/JavaScript`，并且 `modular.js` 明确识别 `.mbc` / `.cbc` bytecode。
- `sluaunreal` 的 contract 也分成两部分，但 owner 更分裂：plugin `Build.cs` 负责按平台链接 `Library/<Platform>/liblua.a/.lib`；脚本文件路径则由宿主 sample `MyGameInstance.cpp` 去 `Content/Lua/<module>.lua` / `.luac` 自己探测。这里必须区分“平台运行库由插件定义”与“脚本路径由 host 定义”。

##### 子维度 2：文件找到了以后，谁会做有效性 gate

- `当前 Angelscript` 与 `Hazelight` 仍然是 inspected 方案里 gate 最强的一组。两边都会在加载 `PrecompiledData` 后做 `IsValidForCurrentBuild()` 检查；current AS 还额外把 `StaticJITCompiledInfo::PrecompiledDataGuid` 与 `PrecompiledData->DataGuid` 对齐，不匹配就清 `FJITDatabase`。这不是“实现方式不同”，而是实打实的质量差异。
- `UnrealCSharp` 的 gate 主要是“路径存在即可消费”。editor 确保 `PublishDirectory` 被加入 UFS，runtime 直接把 `GetFullAssemblyPublishPath()` 交给 domain loader；本轮 inspected 源码里没有看到与 current AS 同级的 build/GUID seal。
- `UnLua`、`puerts`、`sluaunreal` 都有明确路径 contract，但 inspected 代码里没有 current AS/HZ 这种“artifact 自带 build seal”的 gate。对这些方案来说，正确 staging 和正确 loader path 比 artifact self-seal 更关键。

[1] `当前 Angelscript / Hazelight / UnrealCSharp` 的交付 contract 一个是 cache family，一个是 publish DLL family。

```cpp
// ============================================================================
// [1] 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\AngelscriptManager.cpp
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp
// 位置: AngelscriptEngine.cpp:522-524,1425-1469,1513-1598;
//       AngelscriptManager.cpp:361-520;
//       FUnrealCSharpFunctionLibrary.cpp:995-1047;
//       UnrealCSharpEditor.cpp:211-232;
//       FCSharpEnvironment.cpp:54-59
// 说明: AS/HZ 交付物是 cache family；UC 交付物是 publish 后的 DLL family
// ============================================================================
Config.bGeneratePrecompiledData = FParse::Param(FCommandLine::Get(), TEXT("as-generate-precompiled-data"));
Config.bIgnorePrecompiledData = FParse::Param(FCommandLine::Get(), TEXT("as-ignore-precompiled-data"));
...
FAngelscriptBindDatabase::Get().Load(GetScriptRootDirectory() / TEXT("Binds.Cache"), bGeneratePrecompiledData);
...
Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript_Development.Cache");
if (!IFileManager::Get().FileExists(*Filename))
    Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript.Cache");
...
PrecompiledData->Load(Filename);
if (!PrecompiledData->IsValidForCurrentBuild())
{
    // ★ AS/HZ：artifact 自带 build validity gate
    PrecompiledData = nullptr;
}
if (CompiledInfo != nullptr && CompiledInfo->PrecompiledDataGuid != PrecompiledData->DataGuid)
{
    FJITDatabase::Get().Clear(); // ★ current AS：JIT 产物还要过第二层 guid gate
}

return GetFullPublishDirectory() / GetUEName() + DLL_SUFFIX;
return GetFullPublishDirectory() / GetGameName() + DLL_SUFFIX;
...
ProjectPackagingSettings->DirectoriesToAlwaysStageAsUFS.Add({PublishDirectory});       // ★ UC：publish 目录自动进包
...
Domain = new FDomain({
    "",
    FUnrealCSharpFunctionLibrary::GetFullAssemblyPublishPath()                         // ★ UC：runtime 直接消费 DLL family
});
```

[2] `UnLua / puerts / sluaunreal` 的 contract 都更“路径化”：脚本目录、字节码扩展名、native runtime 依赖分别由不同层声明。

```cpp
// ============================================================================
// [2] 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp
// 文件: Reference/UnLua/Plugins/UnLua/Content/IntelliSense/UnLua.lua
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs
// 文件: Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js
// 文件: Reference/sluaunreal/Source/democpp/MyGameInstance.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaState.h
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/slua_unreal.Build.cs
// 位置: UnLuaEditorModule.cpp:188-227; UnLua.lua:1-5;
//       JsEnv.Build.cs:360-408; modular.js:151-154;
//       MyGameInstance.cpp:45-56; LuaState.h:28-29; slua_unreal.Build.cs:31-76
// 说明: UL/PU/SL 的 deployment contract 主要是路径、扩展名和 native runtime staging
// ============================================================================
auto ScriptPaths = TArray<FString>{TEXT("Script"), TEXT("../Plugins/UnLua/Content/Script")};
...
PackagingSettings->DirectoriesToAlwaysStageAsUFS.Add(DirectoryPath);                   // ★ UL：脚本目录本身就是交付物
...
UnLua.PackagePath = "Content/Script/?.lua;Plugins/UnLua/Content/Script/?.lua";        // ★ UL：默认只声明 .lua 搜索模式

RuntimeDependencies.Add(DestDllPath, DllPath, StagedFileType.NonUFS);                  // ★ PU：native V8 运行库单独 staging
...
if (fullPath.endsWith(".mbc") || fullPath.endsWith(".cbc")) {
    bytecode = script;                                                                  // ★ PU：显式支持 bytecode 扩展名
}

path /= "Lua";
TArray<FString> luaExts = { UTF8_TO_TCHAR(".lua"), UTF8_TO_TCHAR(".luac") };
for (auto& it : luaExts) {
    auto fullPath = path + *it;                                                         // ★ SL：host sample 自己决定 .lua/.luac 查找顺序
}
...
#define SLUA_LUACODE "[sluacode]"                                                       // ★ SL：运行时也把字节码当独立载荷类型看待
...
PublicAdditionalLibraries.Add(Path.Combine(externalLib, "Win64/lua.lib"));             // ★ SL：native Lua 运行库由 plugin build.cs 管
```

#### 对比矩阵

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| formal runtime filename/path contract | Full | Full | Full | Full | Full | Full |
| auto stage/UFS registration from editor | None | Full | Full | None | None | None |
| build-time native runtime dependency staging | N/A | None | N/A | Full | Full | N/A |
| explicit bytecode extension support in inspected loader path | None | None | None | Full | Full | None |
| runtime self-validation gate before consuming artifact | Full | None | None | None | None | Full |

#### 小结与建议

- `当前 Angelscript` 在 D11 上最不该丢的资产，是 **artifact self-validation gate**。`Binds.Cache + PrecompiledScript_{Cfg}.Cache + PrecompiledDataGuid` 这一整条链是它区别于 loose-source 方案的核心，优先级 `P0`。
- 如果后续要改善最终用户打包体验，最值得直接吸收的是 `UnrealCSharp` / `UnLua` 的**editor 自动 staging**，而不是把 current AS 回退成 loose-source-only 模型。也就是说，优先补“目录自动进包”，不要改“交付物是什么”，优先级 `P1`。
- `puerts` 值得 current AS 参考的是**把 native runtime staging 与 script payload contract 明确拆开**。如果 future AS 真要引入新的 bytecode/binary payload，也应先把这两层分离，再谈 loader 细节，优先级 `P1`。
- `sluaunreal` 提醒了一个边界：平台运行库由 plugin 管、脚本文件路径由 host 管，可以是合理设计。但 current AS 目前的优势就在于 plugin 自己定义 cache contract；不应在没有强需求时把 canonical script root 完全下放给宿主工程。

---

## 深化分析 (2026-04-09 08:03:47)

### [D1] 模块真正“生效”的时机：`LoadingPhase` 之后，谁按下 activate 开关

#### 各插件实现概览

```
[D1] Activation Gate After Module Load
AS(now) : PostDefault     -> editor/commandlet init now -> packaged runtime via UGameInstanceSubsystem
HZ      : PostDefault     -> Loader::StartupModule      -> InitializeAngelscript immediately
UC      : Default         -> modules load only          -> FEngineListener + bEnableImmediatelyActive
UL      : PreDefault      -> module + settings ready    -> SetActive on AUTO_UNLUA_STARTUP / PIE / server
PU      : PostEngineInit  -> RegisterSettings           -> Enable only when AutoModeEnable
SL      : PreLoadingScreen-> profiler/sim infra only    -> host GameInstance creates LuaState
```

本轮新增观察点不是“谁加载得更早”，而是**`LoadingPhase` 与脚本运行时真正 ready 之间是否还有第二道 gate**。最容易误判的是 `sluaunreal` 和 `当前 Angelscript`：前者虽然 `PreLoadingScreen` 最早，但 inspected 到的 plugin 启动函数只拉起 profiler / simulate 基础设施；后者虽然 `PostDefault` 更晚，但在 editor / commandlet 路径会立刻建主引擎，而 packaged runtime 则把 owner 下放给 `UGameInstanceSubsystem`。

#### 详细对比

##### 子维度 1：模块加载时刻与脚本 VM 真正激活时刻是否重合

- `Hazelight` 是本轮对比里最接近“模块加载即 runtime ready”的方案。`AngelscriptLoader` 在 `StartupModule()` 内直接调用 `FAngelscriptCodeModule::InitializeAngelscript()`，后者再进入 `FAngelscriptManager::GetOrCreate().Initialize()`。也就是说，它把真正的启动门槛绑定在 loader 模块加载本身。
- `当前 Angelscript` 刻意把 editor/commandlet 与 packaged game 分成两条路。`FAngelscriptRuntimeModule::StartupModule()` 只有在 `GIsEditor || IsRunningCommandlet()` 时才立即 `InitializeAngelscript()`；如果运行时此刻还没有主引擎，`UAngelscriptGameInstanceSubsystem::Initialize()` 会在游戏实例层补建 `OwnedEngine`。这不是“没初始化”，而是**激活 owner 从独立 loader 改成了 runtime module + subsystem 双门控**。
- `UnrealCSharp` 把“模块已加载”和“CLR domain 已激活”拆得最明显。`UnrealCSharp` / `UnrealCSharpCore` 的 `StartupModule()` 基本不做重活，真正激活由 `FEngineListener::SetActive()` 依据 `bEnableImmediatelyActive` 决定，再广播 `OnUnrealCSharpCoreModuleActive`，最后由 `FCSharpEnvironment::Initialize()` 消费 publish 后的 assembly 集合。也就是说，module load 只是装好 wiring，不等于 CLR ready。
- `UnLua` 介于两者之间。它的 runtime 模块 `PreDefault` 加载时会先 `RegisterSettings()`、建 console command、挂 PIE delegate；只有在 `AUTO_UNLUA_STARTUP` 条件成立且进入 game/dedicated server 或 PIE 生命周期时，才会 `SetActive(true)`，随后基于 `UUnLuaSettings` 选择 `EnvLocator`、`PreBindClasses` 并实际绑定类。
- `puerts` 则把 activation 显式交给 config。`FPuertsModule::StartupModule()` 第一件事是 `RegisterSettings()`，随后只有在 `Settings.AutoModeEnable` 为真时才 `Enable()`；`Enable()` 又会根据 editor/game 环境决定是否立刻 `MakeSharedJsEnv()`。因此它的 `PostEngineInit` 更多是在等引擎世界 ready，再根据设置决定要不要起 JS 环境。
- `sluaunreal` 最能说明“加载早不代表 VM 早”。`slua_unreal` 模块虽然 `PreLoadingScreen` 就进来，但 `StartupModule()` 本身只做 `Simulate.OnStartupModule()` 和 `SluaProfilerDataManager::StartManager()`；真正创建 `LuaState` 的 inspected 代码仍在宿主 sample `UMyGameInstance::CreateLuaState()` 里。

[1] `当前 Angelscript` 与 `Hazelight` 的差异，不在 `LoadingPhase`，而在“谁拥有第二道 activation gate”。

```cpp
// ============================================================================
// [1] 文件: Plugins/Angelscript/Angelscript.uplugin
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Angelscript.uplugin
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptLoader\Private\AngelscriptLoaderModule.cpp
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\AngelscriptCodeModule.cpp
// 位置: Angelscript.uplugin:18-33; AngelscriptRuntimeModule.cpp:13-25,138-165;
//       AngelscriptGameInstanceSubsystem.cpp:12-29;
//       UEAS2/Angelscript.uplugin:18-33; AngelscriptLoaderModule.cpp:6-9;
//       AngelscriptCodeModule.cpp:124-132
// 说明: current AS 把 packaged runtime 的最终 owner 下放到 subsystem；HZ 仍是 loader 直接拉起 manager
// ============================================================================
"Name": "AngelscriptRuntime",
"LoadingPhase": "PostDefault"
...
void FAngelscriptRuntimeModule::StartupModule()
{
	if (GIsEditor || IsRunningCommandlet())
	{
		InitializeAngelscript();                                                        // ★ editor / commandlet 路径：模块加载即初始化
	}
}

void FAngelscriptRuntimeModule::InitializeAngelscript()
{
	if (FAngelscriptEngine* CurrentEngine = FAngelscriptEngine::TryGetCurrentEngine())
	{
		CurrentEngine->Initialize();
	}
	else
	{
		OwnedPrimaryEngine = MakeUnique<FAngelscriptEngine>();
		FAngelscriptEngineContextStack::Push(OwnedPrimaryEngine.Get());
		OwnedPrimaryEngine->Initialize();                                               // ★ 没有现成 engine 时，runtime module 自己托管一份
	}
}

void UAngelscriptGameInstanceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	PrimaryEngine = FAngelscriptEngine::TryGetCurrentEngine();
	if (PrimaryEngine == nullptr)
	{
		PrimaryEngine = &OwnedEngine;
		FAngelscriptEngineContextStack::Push(PrimaryEngine);
		OwnedEngine.Initialize();                                                       // ★ packaged game 路径：真正 owner 变成 GameInstanceSubsystem
		bOwnsPrimaryEngine = true;
	}
}

"Name": "AngelscriptLoader",
"LoadingPhase": "PostDefault"
...
void FAngelscriptLoaderModule::StartupModule()
{
	FAngelscriptCodeModule::InitializeAngelscript();                                   // ★ HZ：loader 自己就是第二道 gate
}

void FAngelscriptCodeModule::InitializeAngelscript()
{
	FModuleManager::Get().LoadModuleChecked(TEXT("AngelscriptCode"));
	FAngelscriptManager::GetOrCreate().Initialize();                                   // ★ HZ：直接进入 manager 初始化
}
```

[2] `UnrealCSharp / UnLua / puerts / sluaunreal` 都存在“模块已加载，但 runtime 是否激活还要再看一层”的设计，只是 gate owner 不同。

```cpp
// ============================================================================
// [2] 文件: Reference/UnrealCSharp/UnrealCSharp.uplugin
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Listener/FEngineListener.cpp
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp
// 文件: Reference/UnLua/Plugins/UnLua/UnLua.uplugin
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaModule.cpp
// 文件: Reference/puerts/unreal/Puerts/Puerts.uplugin
// 文件: Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/slua_unreal.uplugin
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/slua_unreal.cpp
// 文件: Reference/sluaunreal/Source/democpp/MyGameInstance.cpp
// 位置: UnrealCSharp.uplugin:18-53; FEngineListener.cpp:45-71; FCSharpEnvironment.cpp:54-59;
//       UnLua.uplugin:23-40; UnLuaModule.cpp:48-77,91-108;
//       Puerts.uplugin:15-48; PuertsModule.cpp:405-446,453-469;
//       slua_unreal.uplugin:16-26; slua_unreal.cpp:20-27; MyGameInstance.cpp:36-65
// 说明: UC/UL/PU/SL 都把“加载”与“真正创建脚本执行环境”拆成两步
// ============================================================================
"Name": "UnrealCSharp",
"LoadingPhase": "Default"
...
void FEngineListener::OnLoadingPhaseComplete(const ELoadingPhase::Type LoadingPhase, const bool bSuccess)
{
	if (bSuccess && LoadingPhase == ELoadingPhase::Type::PostDefault)
	{
		SetActive(true);                                                                // ★ UC：等加载阶段完成，再看设置决定是否激活
	}
}

void FEngineListener::SetActive(const bool InbIsActive)
{
	if (InbIsActive)
	{
		if (UnrealCSharpSetting->IsEnableImmediatelyActive())
		{
			FUnrealCSharpCoreModule::Get().SetActive(true);                             // ★ 还要过 bEnableImmediatelyActive
		}
	}
}

void FCSharpEnvironment::Initialize()
{
	Domain = new FDomain({
		"",
		FUnrealCSharpFunctionLibrary::GetFullAssemblyPublishPath()                     // ★ 真正 runtime ready 的时刻，是 assembly domain 被创建
	});
}

"Name": "UnLua",
"LoadingPhase": "PreDefault"
...
virtual void StartupModule() override
{
	RegisterSettings();
	...
	if (IsRunningGame() || IsRunningDedicatedServer())
		SetActive(true);                                                                // ★ UL：是否自动激活取决于 AUTO_UNLUA_STARTUP + 当前运行态
}

virtual void SetActive(const bool bActive) override
{
	if (bActive)
	{
		const auto& Settings = *GetMutableDefault<UUnLuaSettings>();
		EnvLocator = NewObject<ULuaEnvLocator>(GetTransientPackage(), EnvLocatorClass); // ★ 真正 runtime owner 是 EnvLocator / LuaEnv
	}
}

"Name": "Puerts",
"LoadingPhase": "PostEngineInit"
...
void FPuertsModule::StartupModule()
{
	RegisterSettings();
	...
	if (Settings.AutoModeEnable)
	{
		Enable();                                                                      // ★ PU：还要过 AutoModeEnable
	}
}

void FPuertsModule::Enable()
{
	Enabled = true;
	...
	MakeSharedJsEnv();                                                                // ★ JS 环境在 Enable 阶段才创建
}

"Name": "slua_unreal",
"LoadingPhase": "PreLoadingScreen"
...
void Fslua_unrealModule::StartupModule()
{
	Simulate.OnStartupModule();
	SluaProfilerDataManager::StartManager();                                           // ★ SL：module 启动只拉基础设施，不建 LuaState
}

void UMyGameInstance::CreateLuaState()
{
	state = new NS_SLUA::LuaState("SLuaMainState", this);                              // ★ inspected owner 仍在宿主 GameInstance
	state->init();
}
```

#### 对比矩阵

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| module startup 就直接创建主脚本运行时 | Full | None | Partial | Partial | None | Partial |
| runtime activation 另有第二道 gate | None | Full | Full | Full | Full | Full |
| packaged game 与 editor 走不同 activation 路径 | None | Partial | Partial | Partial | Full | Full |
| 最终 activation owner 在宿主对象层（非模块） | None | Partial | Partial | None | Full | Full |
| config flag 能完全阻止自动 activation | None | Full | Partial | Full | N/A | None |

#### 小结与建议

- `当前 Angelscript` 这轮最值得明确保留的不是 `PostDefault` 本身，而是**editor/commandlet 与 packaged game 分离的 activation owner**。这让它可以在 editor 提前建 runtime，同时在真实游戏里把 tick/生命周期交给 `UGameInstanceSubsystem`，优先级 `P0`。
- `Hazelight` 的 loader 直启路径更短，但也更难在无引擎补丁前提下细分 owner。current AS 不应回退成单一 loader 直启模型。
- `UnrealCSharp / puerts` 值得吸收的是**把 activation gate 显式参数化**。如果 current AS 未来要提供“只在某些 target / mode 自动起脚本 runtime”的开关，应做成像 `bEnableImmediatelyActive` / `AutoModeEnable` 这种明确 gate，而不是靠分散命令行参数隐式组合，优先级 `P1`。
- `sluaunreal` 提醒了一个重要边界：早加载模块并不等于早建 VM。current AS 若未来要把 runtime owner 继续下沉到宿主层，必须同时保住现在这套 editor/commandlet 初始化闭环，否则只会让工具链变松。

### [D7] 配置真值的归属：`Project Settings`、显式 ini 回读，还是 external json

#### 各插件实现概览

```
[D7] Config Truth Location
AS(now) : UAngelscriptSettings(Project/Plugins) -> runtime/preprocessor pull on demand
          + UAngelscriptTestSettings(Project/Editor) -> test-only knobs
HZ      : UAngelscriptSettings(Project/Plugins) -> runtime pull on demand
UC       : UUnrealCSharpSetting + UUnrealCSharpEditorSetting
          -> seed defaults -> packaging update -> compile/runtime paths
UL       : UUnLuaSettings + UUnLuaEditorSettings
          -> runtime env knobs + build timestamp + UFS staging
PU       : UPuertsSetting + explicit DefaultPuerts.ini reload
          -> activation/debug/watch behavior
SL       : Tools/config.json + host MyGameInstance path logic
          -> inspected plugin startup has no Project Settings owner
```

本轮新增观察点是：**同样叫“配置”，有的只是让 runtime 读取，有的会反向改 packaging/build graph，有的甚至根本不在 UE Settings 里。** 这比单看有没有 `UCLASS(config=...)` 更能解释插件的产品化程度与副作用边界。

#### 详细对比

##### 子维度 1：配置对象分几层，分别服务谁

- `当前 Angelscript` 与 `Hazelight` 的核心思路很稳：一份 `UAngelscriptSettings` 负责 runtime/compiler 行为，额外再用 `UAngelscriptTestUserSettings` / `UAngelscriptTestSettings` 承接测试配置。它们被注册到 `Project -> Plugins` 或 `Project -> Editor`，运行时和预处理器都是按需 `GetMutableDefault<UAngelscriptSettings>()` 拉取当前值。这里的优点是 side effect 小，缺点是设置变更不会主动改 packaging / build graph。
- `UnrealCSharp` 把设置面拆得最产品化。`UUnrealCSharpSetting` 管 publish/runtime/debug；`UUnrealCSharpEditorSetting` 管脚本目录、支持模块、支持资产、编译与生成开关。更关键的是 editor settings 在注册时会**写入默认 supported modules/asset paths/classes**，而 editor module 启动时还会调用 `UpdatePackagingSettings()` 自动把 publish 目录加进 `DirectoriesToAlwaysStageAsUFS`。
- `UnLua` 同样拆成 runtime 与 editor 两层，但比 UC 更“编译链驱动”。runtime settings 决定 `EnvLocator`、`StartupModuleName`、`PreBindClasses`；editor settings 则控制热重载、IntelliSense、`bLuaCompileAsCpp` 等，并且 `OnSettingsModified()` 会直接 touch `UnLua.Build.cs` 时间戳，强迫 UBT 感知构建选项变化。
- `puerts` 只有一份 `UPuertsSetting`，但它的配置 owner 反而更“硬”：`FPuertsModule::RegisterSettings()` 在注册 UI 之后还要**显式重读 `DefaultPuerts.ini`**，因为源码注释明确说明 NonPak Game 场景下 ini 加载晚于模块加载。也就是说，puerts 的设置不是 UI 装饰，而是 startup correctness 的一部分。
- `sluaunreal` 在本轮 inspected 到的 plugin 启动路径里没有等价的 `RegisterSettings()`；代码生成参数落在 `Tools/config.json`，运行时脚本根路径则落在宿主 `MyGameInstance.cpp`。这不是“没配置”，而是**配置真值散在 external tool + host project，而不是插件自己的 Project Settings**。

[1] `当前 Angelscript / UnrealCSharp / UnLua` 都有 UE Settings surface，但 side effect 级别完全不同。

```cpp
// ============================================================================
// [1] 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Testing/AngelscriptTestSettings.h
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Setting/UnrealCSharpSetting.cpp
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Setting/UnrealCSharpEditorSetting.cpp
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaSettings.h
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorSettings.h
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaModule.cpp
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp
// 位置: AngelscriptEditorModule.cpp:384-393; AngelscriptSettings.h:41-52;
//       AngelscriptEngine.cpp:1291-1295; AngelscriptPreprocessor.cpp:523-527;
//       AngelscriptTestSettings.h:8-29,32-69;
//       UnrealCSharpSetting.cpp:26-37; UnrealCSharpEditorSetting.cpp:44-115;
//       UnrealCSharpEditor.cpp:209-233;
//       UnLuaSettings.h:23-56; UnLuaEditorSettings.h:39-110;
//       UnLuaModule.cpp:238-260; UnLuaEditorModule.cpp:114-140,208-233
// 说明: AS 主要是 read-on-use；UC/UL 的 settings 同时也是 build / packaging / generator orchestration 中枢
// ============================================================================
SettingsModule->RegisterSettings(
	"Project", "Plugins", "Angelscript",
	...,
	GetMutableDefault<UAngelscriptSettings>());                                          // ★ AS：把 runtime/compiler 配置挂到 Project/Plugins

UCLASS(Config=Engine, DefaultConfig)
class ANGELSCRIPTRUNTIME_API UAngelscriptSettings : public UObject                    // ★ AS：单一 runtime settings 对象
{
	UPROPERTY(Config, EditDefaultsOnly, Category = "Angelscript")
	TArray<FString> PreprocessorFlags;
	...
};

ConfigSettings = GetMutableDefault<UAngelscriptSettings>();                            // ★ AS：runtime 初始化时按需读取
...
auto* ConfigSettings = GetMutableDefault<UAngelscriptSettings>();                      // ★ AS：预处理器也按需读取

UCLASS(config = EditorPerProjectUserSettings, meta = (DisplayName = "Angelscript Test User Settings"))
class UAngelscriptTestUserSettings : public UDeveloperSettings                         // ★ AS：测试配置单独分层，不污染 runtime settings
{
	UPROPERTY(EditAnywhere, config, Category = UnitTests)
	bool bRunUnitTestsOnHotReload = true;
}

SettingsModule->RegisterSettings("Project", "Plugins", "UnrealCSharpSettings", ...);   // ★ UC：runtime settings
...
for (const auto& DefaultSupportedModule : DefaultSupportedModules)
{
	MutableDefaultUnrealCSharpEditorSetting->SupportedModule.AddUnique(DefaultSupportedModule);
}                                                                                      // ★ UC：editor settings 注册时顺手播种默认支持面
...
ProjectPackagingSettings->DirectoriesToAlwaysStageAsUFS.Add({PublishDirectory});
ProjectPackagingSettings->TryUpdateDefaultConfigFile();                                 // ★ UC：settings/startup 直接改 packaging config

UCLASS(Config=UnLuaSettings, DefaultConfig, Meta=(DisplayName="UnLua"))
class UNLUA_API UUnLuaSettings : public UObject
{
	UPROPERTY(Config, EditAnywhere, Category="Runtime")
	FString StartupModuleName = TEXT("");                                               // ★ UL：runtime 层决定 env / startup / prebind
}

UCLASS(config=UnLuaEditor, defaultconfig, meta=(DisplayName="UnLuaEditor"))
class UNLUAEDITOR_API UUnLuaEditorSettings : public UObject
{
	UPROPERTY(config, EditAnywhere, Category = "Coding")
	bool bGenerateIntelliSense = true;
	UPROPERTY(config, EditAnywhere, Category = "Build")
	bool bLuaCompileAsCpp = false;                                                       // ★ UL：editor/build 行为另放一层
}

Section->OnModified().BindRaw(this, &FUnLuaModule::OnSettingsModified);                // ★ UL：runtime settings 有修改回调
...
FileManager.SetTimeStamp(*BuildFile, FDateTime::UtcNow());                             // ★ UL：editor settings 改动会反向戳 Build.cs
...
PackagingSettings->DirectoriesToAlwaysStageAsUFS.Add(DirectoryPath);                   // ★ UL：还会顺手写入 staging 目录
```

[2] `puerts` 的 config 会显式回读 ini 保证 startup correctness；`sluaunreal` 则把真值放在 external json 和宿主代码，不进入插件自己的 Settings 面板。

```cpp
// ============================================================================
// [2] 文件: Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsSetting.h
// 文件: Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp
// 文件: Reference/sluaunreal/Tools/config.json
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/slua_unreal.cpp
// 文件: Reference/sluaunreal/Source/democpp/MyGameInstance.cpp
// 位置: PuertsSetting.h:15-56; PuertsModule.cpp:353-393,405-446,482-497;
//       Tools/config.json:1-79; slua_unreal.cpp:20-27; MyGameInstance.cpp:45-59
// 说明: PU 的 settings 直接决定启动行为；SL 的配置面则不在 plugin settings，而在 tool + host
// ============================================================================
UCLASS(config = Puerts, defaultconfig, meta = (DisplayName = "Puerts"))
class UPuertsSetting : public UObject
{
	UPROPERTY(config, EditAnywhere, Category = "Default JavaScript Environment")
	FString RootPath = "JavaScript";
	UPROPERTY(config, EditAnywhere, Category = "Default JavaScript Environment")
	bool AutoModeEnable = false;
	UPROPERTY(config, EditAnywhere, Category = "Default JavaScript Environment")
	bool WatchDisable = false;                                                           // ★ PU：root/debug/watch/activation 都在一份 settings 里
}

auto SettingsSection = SettingsModule->RegisterSettings(..., GetMutableDefault<UPuertsSetting>());
SettingsSection->OnModified().BindRaw(this, &FPuertsModule::HandleSettingsSaved);      // ★ PU：保存设置会即时 Enable/Disable
...
// NonPak Game 打包下, Puerts ini的加载时间晚于模块加载, 因此依然要显式的执行ini的读入
if (GConfig->DoesSectionExist(SectionName, PuertsConfigIniPath))
{
	GConfig->GetBool(SectionName, TEXT("AutoModeEnable"), Settings.AutoModeEnable, PuertsConfigIniPath);
	GConfig->GetBool(SectionName, TEXT("WatchDisable"), Settings.WatchDisable, PuertsConfigIniPath);
}                                                                                      // ★ PU：显式回读 DefaultPuerts.ini 保证启动期读到正确值

if (Settings.AutoModeEnable != Enabled)
{
	if (Settings.AutoModeEnable)
		Enable();
	else
		Disable();                                                                      // ★ PU：settings 改动直接控制 runtime 生灭
}

"output_dir": "{solution_dir}/Plugins/slua_unreal/Source/slua_unreal/Private/",
"win": {
	"solution_dir": "../",
	"ue4_dir": "C:/Program Files/Epic Games/UE_5.2",
	"ue_vcproj": "{solution_dir}/Intermediate/ProjectFiles/UE5.vcxproj",
	...
}                                                                                      // ★ SL：codegen 真值在 external tool config，不在 Project Settings

void Fslua_unrealModule::StartupModule()
{
	Simulate.OnStartupModule();
	SluaProfilerDataManager::StartManager();                                             // ★ inspected startup 路径没有 RegisterSettings
}

FString path = FPaths::ProjectContentDir();
path /= "Lua";
TArray<FString> luaExts = { UTF8_TO_TCHAR(".lua"), UTF8_TO_TCHAR(".luac") };           // ★ SL：脚本根路径与扩展名 contract 由宿主 sample 决定
```

#### 对比矩阵

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| runtime 与 editor 配置显式分层 | Partial | Full | Full | None | None | Partial |
| 保存设置会主动改 packaging / build / generator 状态 | None | Full | Full | Partial | None | None |
| startup 期显式回读 ini 修正加载顺序问题 | None | None | Partial | Full | None | None |
| dedicated test settings surface | Full | None | None | None | None | Full |
| 配置真值主要落在 external json / host code 而非插件 Settings | None | None | None | None | Full | None |

#### 小结与建议

- `当前 Angelscript` 当前的设置面属于**低副作用、按需读取**模型，这和它强调 runtime 内聚、测试可控是匹配的，优先级 `P0`。
- 但 `UnrealCSharp / UnLua` 证明了另一件事：**当某个目录、本地工作区或生成器输出本来就是正式交付物时，配置层应该敢于直接写 packaging/build graph**。如果 current AS 后续补 starter workspace、自动 staging 或更多 IDE sidecar，最值得吸收的是这种“settings 直接拥有 side effect”的模式，优先级 `P1`。
- `puerts` 的显式 ini 回读非常值得 current AS 借鉴。只要某个配置在 `StartupModule()` 前后有加载时序风险，就不应默认相信 CDO 已经是最终值；要像 `puerts` 一样把“读对配置”当成启动 contract 的一部分，优先级 `P1`。
- `sluaunreal` 则是反例提醒：external json + host-owned path contract 适合 wrapper 工具链，但不适合 current AS 现在这条 plugin-first 交付路线。除非明确要把更多权力下放给宿主工程，否则不应复制这种配置散落模型。

### [D11] 交付物是否仍可读：源码树、字节码、`DLL`，还是带 build seal 的 binary snapshot

#### 各插件实现概览

```
[D11] Payload Form At Ship Time
AS(now) : Binds.Cache + PrecompiledScript*.Cache -> binary serialized snapshot -> BuildIdentifier/DataGuid gate
HZ      : Binds.Cache + PrecompiledScript*.Cache -> binary serialized snapshot -> BuildIdentifier/DataGuid gate
UC      : Content/<PublishDir>/*.dll             -> managed assembly set
UL      : Content/Script/*.lua                   -> filesystem bytes -> luaL_loadbufferx
PU      : Content/JavaScript/*.js|*.mbc|*.cbc    -> source or bytecode at module loader
SL      : Content/Lua/*.lua|*.luac               -> host load delegate picks readable or bytecode chunk
```

前文已经讨论过路径 contract 和 staging；本轮新增角度是**交付物“长什么样”**。这直接决定三个问题：作者能否直接看到 shipped payload、runtime 能否在消费前做强校验、以及后续是否容易引入双格式交付。

#### 详细对比

##### 子维度 1：交付物是源码、编译产物，还是脚本快照

- `当前 Angelscript` 与 `Hazelight` 的 shipped core artifact 都不是源码树，而是两级 binary snapshot：`Binds.Cache` 保存 bind database，`PrecompiledScript*.Cache` 保存预编译脚本快照。`FAngelscriptPrecompiledData::Save()` 明确走 `FMemoryWriter` + `FFileHelper::SaveArrayToFile()`，并把 `DataGuid / BuildIdentifier / Modules / FunctionReferences` 整包序列化进去。也就是说，它们交付的是**脚本状态快照**，不是源码本身。
- `UnrealCSharp` 的最终交付物也不是源码，但形态完全不同：它通过 `dotnet build` 产出一组 `ProjectContent/<PublishDir>/*.dll`，runtime 再把 `GetFullAssemblyPublishPath()` 交给 `FDomain`。这不是“缺少脚本交付”，而是**把脚本语言交付物提升成 managed assembly set**。
- `UnLua` 的默认公开 contract 仍然是源码树。`UnLua.PackagePath` 默认只暴露 `Content/Script/?.lua;Plugins/UnLua/Content/Script/?.lua`，`FLuaEnv::LoadFromFileSystem()` 再从这些路径读字节喂给 `luaL_loadbufferx`。底层 Lua 当然能解释 bytecode chunk，但在 inspected 的公开 package path / loader contract 上，它默认仍是 `.lua` source-first。
- `puerts` 明确支持双格式。`modular.js` 里 `.mbc` / `.cbc` 会被识别为 bytecode；`.js / .mjs / .cjs` 则仍按源码模块处理。它是本轮 inspected 方案里对“源码与字节码并存”产品化最明确的一种。
- `sluaunreal` 则把双格式选择权交给宿主。`MyGameInstance.cpp` 的 load delegate 会依次查 `.lua` 与 `.luac`；plugin 运行时也用 `SLUA_LUACODE` 把 Lua bytecode 当成独立载荷类型看待。这比 `UnLua` 更接近“可选 opaque payload”，但 owner 明显在宿主项目，而不是插件 runtime 自己。

##### 子维度 2：交付物在被消费前有没有 build/version 闸门

- `当前 Angelscript` 与 `Hazelight` 在 inspected 方案里 gate 最强。除了 `BuildIdentifier`，还会在 runtime load 后再核对 `StaticJITCompiledInfo::PrecompiledDataGuid` 与 `PrecompiledData->DataGuid`；不匹配就清掉 `FJITDatabase`。这意味着它们不是“读到文件就用”，而是**二次验证后才允许 binary snapshot 参与执行**。
- `UnrealCSharp` 的 gate 更偏“文件存在 + CLR 可装载”。它把 assembly path 明确化，但本轮 inspected 代码里没有 current AS/HZ 这种 artifact 自带 build seal。
- `UnLua`、`puerts`、`sluaunreal` 的 inspected loader path 都更偏 runtime-format gate：路径、扩展名、chunk 类型对了就交给 VM。这里不是“实现更差”，而是**设计目标不同**。这些方案把版本兼容问题更多交给源码/字节码生成链和宿主 staging，而不是 artifact self-seal。

[1] `当前 Angelscript / Hazelight` 的关键不是“有 cache 文件”，而是**cache 文件本身就是带 build seal 的 binary snapshot**。

```cpp
// ============================================================================
// [1] 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\StaticJIT\PrecompiledData.h
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\StaticJIT\PrecompiledData.cpp
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\AngelscriptManager.cpp
// 位置: PrecompiledData.h:568-612; PrecompiledData.cpp:2620-2688;
//       AngelscriptEngine.cpp:1466-1556,1583-1587;
//       UEAS2/PrecompiledData.h:630-674; UEAS2/PrecompiledData.cpp:2568-2638;
//       AngelscriptManager.cpp:404-477,505-509
// 说明: current AS / HZ 交付物都是 binary snapshot，而且在消费前会过 build/guid 双重校验
// ============================================================================
FGuid DataGuid;
int32 BuildIdentifier = -1;
...
Ar << Data.DataGuid;
Ar << Data.BuildIdentifier;
Ar << Data.Modules;
Ar << Data.FunctionReferences;                                                         // ★ binary snapshot 把脚本结构完整写进 archive

bool FAngelscriptPrecompiledData::IsValidForCurrentBuild()
{
	return BuildIdentifier == GetCurrentBuildIdentifier() && BuildIdentifier != -1;     // ★ 第一层 gate：build config 必须匹配
}

void FAngelscriptPrecompiledData::Save(const FString& Filename)
{
	TArray<uint8> Data;
	FMemoryWriter Writer(Data, true);
	Writer.SetIsPersistent(true);
	Writer << *this;
	FFileHelper::SaveArrayToFile(Data, *Filename);                                       // ★ 正式落盘的是二进制序列化数据
}

FAngelscriptBindDatabase::Get().Load(GetScriptRootDirectory() / TEXT("Binds.Cache"), ...);
...
Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript_Development.Cache");
...
PrecompiledData->Load(Filename);
if (!PrecompiledData->IsValidForCurrentBuild())
{
	PrecompiledData = nullptr;                                                          // ★ 第二层之前先过 build gate
}
...
if (CompiledInfo != nullptr && CompiledInfo->PrecompiledDataGuid != PrecompiledData->DataGuid)
{
	FJITDatabase::Get().Clear();                                                        // ★ 还有 guid gate，避免旧 JIT 误配新快照
}

FString Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript.Cache");
PrecompiledData->InitFromActiveScript();
PrecompiledData->Save(Filename);                                                      // ★ HZ/current AS 都会主动生成快照产物
```

[2] `UnrealCSharp / UnLua / puerts / sluaunreal` 的交付物形态完全不同：assembly、source、bytecode、host-owned dual-format 并存。

```cpp
// ============================================================================
// [2] 文件: Reference/UnrealCSharp/Source/Compiler/Private/FCSharpCompilerRunnable.cpp
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp
// 文件: Reference/UnLua/Plugins/UnLua/Content/IntelliSense/UnLua.lua
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaBase.cpp
// 文件: Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js
// 文件: Reference/sluaunreal/Source/democpp/MyGameInstance.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaState.h
// 位置: FCSharpCompilerRunnable.cpp:248-299; FUnrealCSharpFunctionLibrary.cpp:1005-1041;
//       FCSharpEnvironment.cpp:54-59; UnLua.lua:4-5; LuaEnv.cpp:614-638;
//       UnLuaBase.cpp:95-104; modular.js:148-155; MyGameInstance.cpp:45-59; LuaState.h:28-29
// 说明: UC/UL/PU/SL 的 shipped payload 分别是 assembly、.lua、.js/.mbc/.cbc、.lua/.luac
// ============================================================================
const auto CompileParam = FString::Printf(TEXT("build \"%s\" --nologo -c %s"), ...);  // ★ UC：先把脚本世界编成 .NET build 产物
...
return FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / GetPublishDirectory());
...
return TArrayBuilder<FString>().
	Add(GetFullUEPublishPath()).
	Add(GetFullGamePublishPath()).
	Append(GetFullCustomProjectsPublishPath()).
	Build();                                                                            // ★ UC：正式交付物是一组 publish DLL
...
Domain = new FDomain({
	"",
	FUnrealCSharpFunctionLibrary::GetFullAssemblyPublishPath()
});                                                                                    // ★ runtime 直接吃 assembly set

UnLua.PackagePath = "Content/Script/?.lua;Plugins/UnLua/Content/Script/?.lua";        // ★ UL：默认公开 contract 仍是 .lua 源码路径
...
if (FFileHelper::LoadFileToArray(Data, *FullPath, FILEREAD_Silent))
	return LoadIt();                                                                    // ★ UL：从文件系统读 chunk
...
int32 Code = luaL_loadbufferx(L, Chunk, ChunkSize, ChunkName, Mode);                   // ★ 最终交给 Lua VM 解释 chunk

let isESM = outerIsESM === true || fullPath.endsWith(".mjs") || fullPath.endsWith(".mbc");
if (fullPath.endsWith(".cjs") || fullPath.endsWith(".cbc")) isESM = false;
if (fullPath.endsWith(".mbc") || fullPath.endsWith(".cbc")) {
	bytecode = script;
	script = generateEmptyCode(getSourceLengthFromBytecode(bytecode));                  // ★ PU：源码与 bytecode 都是正式一等交付物
}

TArray<FString> luaExts = { UTF8_TO_TCHAR(".lua"), UTF8_TO_TCHAR(".luac") };
for (auto& it : luaExts) {
	auto fullPath = path + *it;
	FFileHelper::LoadFileToArray(Content, *fullPath);                                   // ★ SL：宿主自己决定优先拿源码还是字节码
}
...
#define SLUA_LUACODE "[sluacode]"                                                       // ★ SL：运行时显式区分 Lua bytecode 载荷
```

#### 对比矩阵

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 默认主交付物仍是人类可直接阅读的源码树 | None | None | Full | Full | Full | None |
| inspected loader 对编译后/字节码格式有一等支持 | Full | Full | None | Full | Full | Full |
| artifact 自带 build/version seal 再决定是否消费 | Full | None | None | None | None | Full |
| shipped payload 由宿主项目决定选源码还是编译格式 | None | None | None | Partial | Full | None |
| runtime 直接消费的是 managed/native 编译产物而非脚本文本 | None | Full | None | Partial | Partial | Full |

#### 小结与建议

- `当前 Angelscript` 在 D11 上的真正护城河不是“有缓存”，而是**binary snapshot + build/guid 双闸门**。这条链必须保留，优先级 `P0`。
- 如果 future AS 想要提供“更不透明的 shipped payload”，最值得参考的不是 `UnLua` 的 loose-source 模型，而是 `puerts / sluaunreal` 这种**源码与字节码并存、由 loader 明确识别扩展名**的双格式思路；但无论如何都应把 current AS 的 `BuildIdentifier/DataGuid` gate 保留下来，优先级 `P1`。
- `UnrealCSharp` 提醒 current AS：**交付物可以不是脚本文本，也可以是可编译、可装载的正式二进制族**。如果未来要引入更多 prebuilt artifact，应该像 UC 一样明确“编译器输出目录”和“runtime 装载目录”，而不是把所有 opaque payload 都混进脚本源目录，优先级 `P1`。
- `UnLua` 的 source-first 模型不是“没有实现部署”，而是有意把 authoring tree 直接当部署单元。current AS 若没有强需求，不应为了所谓易用性回退到这种公开源码树交付。

---

## 深化分析 (2026-04-09 08:19:07)

这一轮不再重复 D4/D6/D11 已经写透的主链，而是补三条更偏 **editor productization** 的横向轴线：脚本插件实际挂进了 UE editor 哪些原生子系统、Blueprint 图本体到底由谁写、以及编辑器后台到底是谁在持续盯变化并驱动回流。

### [D7] UE 挂载点拓扑：脚本编辑器到底挂进了哪些原生子系统

前面 D7 已经确认“有多少入口”和“truth 落在哪”。本轮继续下钻的是更底层的一层：**这些入口具体挂进了 UE 哪些 editor subsystem**。这决定了一个插件到底是“做了一层 helper UI”，还是“真正进入了 Content Browser / BlueprintEditor / PropertyEditor / compiler / watcher 这些主干对象图”。

#### 各插件实现概览

```
[D7-HookTopology] Native Editor Mount Points
HZ/AS
├─ ContentBrowserDataSource                 // 脚本资产虚拟视图
├─ LevelEditor + ContentBrowser extenders   // 广覆盖菜单挂点
└─ ClassReloadHelper                        // 热重载后 Blueprint 图修复

UC
├─ PropertyEditor custom layouts            // 目录/路径属性定制
├─ ContentBrowser DynamicDataSource         // 动态类虚拟树 + AddNew
├─ BlueprintEditor toolbar                  // OpenFile / CodeAnalysis / Override
└─ Listener + Ticker                        // 目录/资产变化闭环

UL
├─ MainMenu toolbar                         // 顶层入口
├─ BlueprintEditor toolbar                  // Blueprint 快捷动作
├─ AnimationBlueprint toolbar               // AnimBlueprint 快捷动作
└─ DirectoryWatcher + PackageSave hooks     // 文件/保存期控制环

PU
├─ Blueprint compiler hook                  // TypeScript Blueprint 编译入口
├─ PEDirectoryWatcher                       // TS/JS 源码监听
└─ resident JsEnv(CodeAnalyze)              // 编辑器内分析 runtime

SL
└─ LevelEditor menu + NomadTab + Tick       // profiler-only editor surface
```

#### 详细对比

##### 子维度 1：是把脚本内容投影进 Content Browser，还是只给现有 editor 加按钮

- `当前 Angelscript` 与 `Hazelight` 在这条线上仍然同族。两边都在 `OnPostEngineInit` 后创建 `UAngelscriptContentBrowserDataSource`，把脚本资产激活到 `AngelscriptData`；同时 `ScriptEditorMenuExtension` 又把脚本菜单挂进 `LevelEditor` 与 `ContentBrowser`。区别不在“挂没挂进去”，而在 current AS 额外把 `StateDumpExtension` 与更多测试/工具接上了同一 editor 模块。
- 但 current AS / HZ 这条 `ContentBrowserDataSource` 仍然是**投影型**而不是**可写型**。`GetItemPhysicalPath()`、`CanEditItem()`、`EditItem()`、`BulkEditItems()` 都直接返回 `false`。也就是说，它们把脚本对象纳入了 `ContentBrowserData`，但没有把源码编辑权限一起交给这个 data source。
- `UnrealCSharp` 的挂载面最“硬”。`StartupModule()` 同时进入 `PropertyEditor`、`ContentBrowser`、`BlueprintEditor`、ticker/cook hook；`UDynamicDataSource` 还会给 `ContentBrowser.AddNewContextMenu` 注入动态段。这里不是“有一个按钮”，而是多个 editor subsystem 同时成为 C# authoring surface。
- `UnLua` 的 editor surface 更像对现有 asset editor 做增强。它没有 `ContentBrowserDataSource`，但 `MainMenuToolbar`、`BlueprintToolbar`、`AnimationBlueprintToolbar`、`DirectoryWatcher`、`PackageSaved` hook 都被明确注册进 editor 生命周期，因此它的 owner 是“已有 UE asset editor”而不是“新增脚本资产树”。
- `puerts` 的 native 挂点数量没有 UC 那么多，但挂点的语义很特别。它在 editor 模块里显式 `RegisterCompilerForBP(UTypeScriptBlueprint::StaticClass(), &MakeCompiler)`，再起一个 resident `JsEnv` 运行 `PuertsEditor/CodeAnalyze`。也就是说，很多高层语义不是由 `SWidget` 或 toolbar 持有，而是由 editor 内常驻的 JS analyzer 持有。
- `sluaunreal` 则最克制。就本轮 inspected 的 `slua_profile` 而言，核心只有 `LevelEditor` 菜单、`NomadTab` 和 ticker 驱动的 profiler 刷新；没有定位到与 `ContentBrowserDataSource`、`BlueprintEditor` extender 或 `PropertyEditor` customization 对等的结构。

[1] `当前 Angelscript / Hazelight` 都把脚本内容投影进 `ContentBrowserData`，但 data source 本身不接管源码编辑：

```cpp
// ============================================================================
// [1] 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptEditor\Private\AngelscriptEditorModule.cpp
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptEditor\Private\AngelscriptContentBrowserDataSource.cpp
// 位置: current AS EditorModule.cpp:111-119,351-364; current AS DataSource.cpp:16-29,182-199;
//       current AS ScriptEditorMenuExtension.cpp:673-710;
//       HZ EditorModule.cpp:133-140,361-372; HZ DataSource.cpp:16-29,182-199
// 说明: HZ/AS 同时占据 ContentBrowserData 与 menu-extender 两条 editor 主干，但 data source 保持只读
// ============================================================================
auto* DataSource = NewObject<UAngelscriptContentBrowserDataSource>(GetTransientPackage(), "AngelscriptData", RF_MarkAsRootSet | RF_Transient);
DataSource->Initialize();
ContentBrowserData->ActivateDataSource("AngelscriptData");            // ★ 脚本资产先进入 ContentBrowser 数据层

FClassReloadHelper::Init();
UScriptEditorMenuExtension::InitializeExtensions();                   // ★ 再把热重载 helper 与菜单扩展挂进 editor 模块

return FContentBrowserItemData(
	this,
	EContentBrowserItemFlags::Type_File | EContentBrowserItemFlags::Category_Asset,
	*(TEXT("/All/Angelscript/") + Asset->GetName()), ...);             // ★ item 是虚拟资产条目，不是磁盘文件直通

bool UAngelscriptContentBrowserDataSource::GetItemPhysicalPath(...) { return false; }
bool UAngelscriptContentBrowserDataSource::CanEditItem(...) { return false; }
bool UAngelscriptContentBrowserDataSource::EditItem(...) { return false; }            // ★ data source 不拥有源码编辑权

FLevelEditorModule& LevelEditorModule = FModuleManager::Get().LoadModuleChecked<FLevelEditorModule>("LevelEditor");
FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
// ★ 菜单扩展继续把脚本入口挂到 LevelEditor / ContentBrowser 多个原生 surface
```

[2] `UnrealCSharp / UnLua / puerts / sluaunreal` 的挂载点 owner 明显不同：分别偏 `ContentBrowser + PropertyEditor`、已有 asset editor、resident analyzer、以及 profiler tab。

```cpp
// ============================================================================
// [2] 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/BlueprintToolbar.cpp
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/AnimationBlueprintToolbar.cpp
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorFunctionLibrary.cpp
// 文件: Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_profile/Private/slua_profile.cpp
// 位置: UC Editor.cpp:49-67,104-108; UC DynamicDataSource.cpp:59-88,704-723;
//       UC BlueprintToolBar.cpp:21-39; UL EditorModule.cpp:48-70,88-105;
//       UL BlueprintToolbar.cpp:21-30; UL AnimationBlueprintToolbar.cpp:24-33;
//       UL EditorFunctionLibrary.cpp:27-36; PU PuertsEditorModule.cpp:120-151;
//       SL slua_profile.cpp:60-79
// 说明: 四个参考实现分别把 owner 放到不同 editor subsystem
// ============================================================================
PropertyEditorModule.RegisterCustomPropertyTypeLayout("GameContentDirectoryPath", ...);
DynamicDataSource.Reset(NewObject<UDynamicDataSource>(GetTransientPackage(), "DynamicData"));
DynamicDataSource->Initialize();                                     // ★ UC：PropertyEditor + ContentBrowserData 双挂载

if (const auto Menu = UToolMenus::Get()->ExtendMenu("ContentBrowser.AddNewContextMenu"))
{
	Menu->AddDynamicSection(..., ...);
}                                                                    // ★ UC：ContentBrowser AddNew 正式拥有“新建动态类”入口

BlueprintEditorModule.GetMenuExtensibilityManager()->GetExtenderDelegates().Add(...);
OnEndGeneratorDelegateHandle = FUnrealCSharpCoreModuleDelegates::OnEndGenerator.AddRaw(...);
                                                                     // ★ UC：Blueprint editor 直接消费 generator 生命周期

MainMenuToolbar = MakeShareable(new FMainMenuToolbar);
BlueprintToolbar = MakeShareable(new FBlueprintToolbar);
AnimationBlueprintToolbar = MakeShareable(new FAnimationBlueprintToolbar);
UUnLuaEditorFunctionLibrary::WatchScriptDirectory();                 // ★ UL：owner 在现有 asset editor + watcher + save hooks

auto& AnimationBlueprintEditorModule = FModuleManager::LoadModuleChecked<FAnimationBlueprintEditorModule>("AnimationBlueprintEditor");
ToolbarExtenders.Add(...);                                           // ★ UL：甚至显式进入 AnimationBlueprintEditor

FKismetCompilerContext::RegisterCompilerForBP(UTypeScriptBlueprint::StaticClass(), &MakeCompiler);
JsEnv = MakeShared<PUERTS_NAMESPACE::FJsEnv>(..., TEXT("--max-old-space-size=2048"));
JsEnv->Start("PuertsEditor/CodeAnalyze");                            // ★ PU：Blueprint compiler hook + resident JS analyzer

FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
FGlobalTabmanager::Get()->RegisterNomadTabSpawner(slua_profileTabName, ...);
TickDelegateHandle = FTSTicker::GetCoreTicker().AddTicker(TickDelegate); // ★ SL：editor 面基本就是 profiler tab + tick
```

#### 对比矩阵

| Hook point | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| `ContentBrowserDataSource` 级虚拟脚本视图 | Full | Full | None | None | None | Full |
| `ContentBrowser/LevelEditor` 菜单或 AddNew surface | Full | Full | Partial | None | Partial | Full |
| Blueprint-specific native hook | None | Full | Full | Partial | None | None |
| `PropertyEditor` / details customization | None | Full | None | None | None | None |
| editor-owned watcher / asset-change service | Full | Full | Full | Full | None | Full |
| resident analyzer / tab / ticker | None | Partial | None | Full | Full | None |

#### 小结与建议

- `当前 Angelscript` 在 D7 上真正接近 `Hazelight` 的地方不是“菜单多”，而是**先把脚本资产投影进 `ContentBrowserData`，再把 authoring 动作挂到广覆盖 extender**。这条资产中心路线应继续保持，优先级 `P0`。
- 值得吸收的不是“把所有 editor 都挂一遍”，而是更精准的挂点选择。最值得借鉴的是 `UnrealCSharp` 的 `PropertyEditor + BlueprintEditor` 高频 authoring surface，以及 `UnLua` 对 `AnimationBlueprintEditor` 的专用入口，优先级 `P1`。
- `puerts` 证明 native toolbar 不是唯一选择。若 future AS 真的引入更重的 analyzer / language service，应该像 `puerts` 一样把 resident runtime 的 owner 和生命周期写清楚，而不是把分析逻辑散在多个按钮回调里，优先级 `P1`。

### [D3/D7] Blueprint 图本体写入 contract：谁真正改 `UEdGraph`、变量表和子 Blueprint

前文已经比较过“脚本行为挂在哪个载体”与“script class 能不能长出 UE reflection schema”。本轮更进一步，只盯一个更具体的问题：**当脚本世界变化时，到底是谁在改 `UBlueprint` 本体**。这条线把六个方案分成了三类：child blueprint 路线、reload-time graph repair 路线，以及 analyzer-driven graph synthesis 路线。

#### 各插件实现概览

```
[D3/D7-BlueprintWrite] Blueprint Body Mutation
HZ/AS
├─ script class -> CreateBlueprint popup          // 主 authoring 路径是创建子 Blueprint
└─ reload helper -> refresh dependent BP nodes    // 图写入主要发生在 reload 修复阶段

UC
└─ dynamic generator -> patch pin types / refresh nodes
   -> MarkBlueprintAsModified -> CompileBlueprint

PU
└─ TS CodeAnalyze -> UPEBlueprintAsset
   ├─ LoadOrCreate Blueprint
   ├─ AddFunctionGraph / AddMemberVariable
   └─ Save -> CompileBlueprint

UL
└─ runtime override object
   └─ SuspendOverrides / ResumeOverrides          // 不直接写 Blueprint 图

SL
└─ no blueprint writer located in inspected editor modules
```

#### 详细对比

##### 子维度 1：主 authoring 路径是“创建子 Blueprint”，还是“直接改 Blueprint 图”

- `当前 Angelscript` 与 `Hazelight` 的 authoring 主线仍然是 **script class -> child blueprint asset**。`ShowCreateBlueprintPopup()` 会定位默认目录、弹出 `CreateModalSaveAssetDialog()`，随后调用 `FKismetEditorUtilities::CreateBlueprint()`。这意味着它们默认不会像 `puerts` 那样直接把 TS/AS 分析结果写成多个 `UEdGraph`；作者先得到的是一个正常的 Blueprint 资产。
- 但 current AS / HZ 并不是完全不写 Blueprint 图。真正的图写入 owner 在 `ClassReloadHelper`。热重载后，它们会扫描依赖脚本 struct/enum/class 的 Blueprint，`ReconstructNode()` 或刷新 pin type，再 `QueueForCompilation()` 并 `FlushCompilationQueueAndReinstance()`。也就是说，图写入主要用于**修复依赖图**，不是用于从脚本 AST 直接合成 Blueprint graph。
- `UnrealCSharp` 更偏 reload/regeneration owner。`FDynamicClassGenerator` 在旧类换新类后，会替换 pin 的 `PinSubCategoryObject`、`RefreshAllNodes()`、`MarkBlueprintAsModified()`，最后 `CompileBlueprint()`。它与 current AS 的共同点是“图写入服务于 generated type 切换”；差异在于 UC 的 owner 是 dynamic generator，不是 class reload helper。
- `puerts` 是六个方案里图写入最深的一种。`UPEBlueprintAsset::LoadOrCreate()` 先拿到/创建目标 Blueprint，随后 `AddFunction()` 可以 `CreateNewGraph()` + `AddFunctionGraph()`，`AddMemberVariable()` 还能走 `AddMemberVariable()`、`ChangeMemberVariableType()`，最后 `Save()` 里 `MarkBlueprintAsModified()` + `CompileBlueprint()`。这条路不是修复，而是**直接把 TypeScript 分析结果物化为 Blueprint 本体**。
- `UnLua` 在本轮 inspected 代码里没有与 `PEBlueprintAsset` 或 `FDynamicClassGenerator` 对等的 Blueprint writer。它的核心动作仍是 `ULuaFunction::Override()` 与保存期的 `SuspendOverrides()/ResumeOverrides()`。这里应判为“实现方式不同”，不是“没有 Blueprint 交互”。
- `sluaunreal` 这轮只定位到 profiler/editor menu 面；结合 `slua_profile.cpp:60-79` 与源码检索结果，可以推断 inspected editor 模块里没有对等的 Blueprint body writer。这个 `None` 带检索推断性质。

[1] `当前 Angelscript / Hazelight` 的 Blueprint body 写入分成两段：主路径先创建 child Blueprint，热重载时再修复依赖图并重新编译。

```cpp
// ============================================================================
// [1] 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptEditor\Private\AngelscriptEditorModule.cpp
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptEditor\Private/ClassReloadHelper.cpp
// 位置: current AS EditorModule.cpp:471-517; current AS ClassReloadHelper.cpp:201-206,291-299;
//       HZ EditorModule.cpp:495-505,538-540; HZ ClassReloadHelper.cpp:208-213,197-205
// 说明: HZ/AS 的 Blueprint 写入主要分为“创建 child blueprint”和“reload-time repair”两段
// ============================================================================
FString SaveObjectPath = ContentBrowserModule.Get().CreateModalSaveAssetDialog(SaveAssetDialogConfig);
...
Asset = FKismetEditorUtilities::CreateBlueprint(
	Class, Package, AssetName, BPTYPE_Normal,
	BlueprintClass, BlueprintGeneratedClass, FName("AngelscriptCreateBlueprint")
);                                                                   // ★ 主 authoring 路径是创建 child Blueprint 资产

auto RefreshRelevantNodesInBP = [&](UBlueprint* BP)
{
	FBlueprintEditorUtils::GetAllNodesOfClass(BP, AllNodes);
	...                                                                // ★ 只对依赖被替换类型的图节点做修复
};

for (UBlueprint* BP : DependencyBPs)
{
	RefreshRelevantNodesInBP(BP);
	FBlueprintCompilationManager::QueueForCompilation(BP);            // ★ 图修复后统一进入编译队列
}
FBlueprintCompilationManager::FlushCompilationQueueAndReinstance();
```

[2] `UnrealCSharp / puerts / UnLua` 的 owner 进一步分化：UC 偏 generated-type patch，PU 偏 analyzer-driven graph synthesis，UL 偏 runtime override suspend/resume。

```cpp
// ============================================================================
// [2] 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicClassGenerator.cpp
// 文件: Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp
// 文件: Reference/puerts/unreal/Puerts/PuertsEditor/CodeAnalyze.ts
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaFunction.cpp
// 位置: UC FDynamicClassGenerator.cpp:500-520;
//       PU PEBlueprintAsset.cpp:87-166,479-487,1050-1110,1345-1358;
//       PU CodeAnalyze.ts:704-709,936-965;
//       UL LuaFunction.cpp:81-99
// 说明: UC/PU/UL 分别代表三种不同的 Blueprint body owner
// ============================================================================
if (Pin->PinType.PinSubCategoryObject == InOldClass)
{
	Pin->PinType.PinSubCategoryObject = InNewClass;
}
FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
FKismetEditorUtilities::CompileBlueprint(Blueprint, BlueprintCompileOptions); // ★ UC：generated class 切换后修图并编译

Blueprint = FKismetEditorUtilities::CreateBlueprint(
	ParentClass, Package, *InName, BlueprintType, BlueprintClass, BlueprintGeneratedClass, FName("PuertsAutoGen"));
...                                                                  // ★ PU：先拿到/创建真正的 Blueprint

const FScopedTransaction Transaction(LOCTEXT("CreateOverrideFunctionGraph", "Create Override Function Graph"));
UEdGraph* const NewGraph = FBlueprintEditorUtils::CreateNewGraph(...);
FBlueprintEditorUtils::AddFunctionGraph(Blueprint, NewGraph, /*bIsUserCreated=*/false, OverrideFuncClass);
                                                                     // ★ PU：直接创建 override function graph

else if (FBlueprintEditorUtils::AddMemberVariable(Blueprint, NewVarName, PinType))
{
	NeedSave = true;
}                                                                    // ★ PU：直接写 Blueprint 变量表

FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
FKismetEditorUtilities::CompileBlueprint(Blueprint);                 // ★ PU：graph/var 同步后立即编译

bp.LoadOrCreate(type.getSymbol().getName(), modulePath, baseTypeUClass as UE.Class, 0, 0);
bp.Save();                                                           // ★ PU：TS analyzer 是 Blueprint writer 的真正上游

UnLua::FLuaOverrides::Get().Override(Function, Outer, NewName);
UnLua::FLuaOverrides::Get().Suspend(Class);
UnLua::FLuaOverrides::Get().Resume(Class);                           // ★ UL：owner 在 runtime override，而不是 Blueprint graph
```

#### 对比矩阵

| 功能点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 从脚本定义直接创建 child Blueprint asset | Full | None | None | Full | None | Full |
| 直接合成/插入 `UEdGraph` 函数图 | None | None | None | Full | None | None |
| 直接合成/更新 Blueprint 成员变量/组件 | None | None | None | Full | None | None |
| reload-time 刷新依赖 Blueprint 节点/引脚 | Full | Full | None | Partial | None | Full |
| 保存期 suspend/resume runtime override | None | None | Full | None | None | None |
| 图/类型同步后显式触发 Blueprint 编译 | Full | Full | None | Full | None | Full |

#### 小结与建议

- `当前 Angelscript` 在这一维最值得保留的不是 popup，而是**child Blueprint authoring + reload-time graph repair** 这条双段式 contract。它让主 authoring 路径保持 UE 原生资产语义，又把图写入限制在确有必要的 reload 修复阶段，优先级 `P0`。
- `puerts` 的 `PEBlueprintAsset` 很强，但不适合整体照搬。真正值得吸收的是它“把 graph writer 单独封装成一个明确对象”的思路，而不是让主脚本 authoring 路径退化成 analyzer 直接改图。若 current AS 以后需要自动生成 proxy Blueprint 或临时桥接资产，可以参考这种隔离式 writer，优先级 `P1`。
- `UnLua` 的保存期 `SuspendOverrides()/ResumeOverrides()` 值得 current AS 关注。只要 future AS 在 PIE/保存态下让 Blueprint 与脚本类耦合更深，就应考虑显式的 save fence，而不是只依赖 reload helper，优先级 `P1`。

### [D4/D7] 常驻编辑器控制环：谁在后台一直盯着源码/资产变化

前面的 D4 已经比较过“事件语义”和“reload safe point”。这一轮补的是更具体的 **resident control loop**：在 editor 打开后，究竟是谁常驻着，监听文件、资产、保存事件或 analyzer runtime，并把这些变化回流到脚本系统。

#### 各插件实现概览

```
[D4/D7-ResidentLoop] Editor Control Loop Owner
HZ/AS
├─ DirectoryWatcher on all script roots
├─ normalize file/folder changes into reload queues
└─ ClassReloadHelper repairs dependent Blueprint graphs

UC
├─ FEditorListener owns DirectoryWatcher + AssetRegistry + MainFrame
├─ file change -> CodeAnalysis
└─ asset change -> Generator / DynamicDataSource refresh

UL
├─ DirectoryWatcher on script root
├─ package save hooks suspend/resume overrides
└─ IntelliSense generator initialized after engine init

PU
├─ resident JsEnv starts CodeAnalyze
├─ PEDirectoryWatcher batches ts/js changes
└─ analyzer drives Blueprint refresh jobs

SL
└─ FTicker + profiler queues + NomadTab
```

#### 详细对比

##### 子维度 1：source change 是进 reload queue，还是直接进 analyzer / generator

- `当前 Angelscript` 与 `Hazelight` 的 loop 仍是同一谱系：editor 启动时把所有 script roots 注册给 `DirectoryWatcher`，回调统一进 `OnScriptFileChanges()`，再由 `QueueScriptFileChanges()` 把变化标准化成 `FileChangesDetectedForReload` 与 `FileDeletionsDetectedForReload` 两个队列。这里的 owner 仍是脚本引擎自身，而不是额外 analyzer runtime。
- current AS 的新价值在于**这条队列语义已经被 productized 成自动化测试**。`AngelscriptDirectoryWatcherTests.cpp` 直接覆盖 script add/remove、folder add/remove、rename window 五种场景。也就是说，watcher contract 在 current AS 里已经不是“肉眼调试看起来对”，而是正式 regression surface。
- `UnrealCSharp` 的常驻 loop 更复杂。`FEditorListener` 同时持有 `DirectoryWatcher`、`AssetRegistry`、`MainFrame`、compile/generator delegate；目录变更会进 `FCodeAnalysis::Analysis(File)`，asset 变更又会走 `FAssetGenerator::Generator(InAssetData)`。它的 owner 是 editor listener，而不是 runtime VM。
- `UnLua` 的 loop 更偏 editor operator。目录变更由 `WatchScriptDirectory()` 监听，保存 package 时又对 PIE 中的 class override 做 `SuspendOverrides()/ResumeOverrides()`，并在 engine init 后初始化 `IntelliSenseGenerator`。它不像 UC/PU 那样有一个“永远在跑的 analyzer”，更像 watcher + save-fence + on-demand generator 的组合。
- `puerts` 把 resident loop 做得最像 IDE sidecar。editor 模块先起 `SourceFileWatcher` 与 resident `JsEnv`，随后 `JsEnv->Start("PuertsEditor/CodeAnalyze")`；TypeScript 侧再 new 一个 `UE.PEDirectoryWatcher()`，把 add/modify/remove 事件批处理后驱动 `onSourceFileAddOrChange()` 与 `refreshBlueprints()`。这里的 loop owner 已经不是 UE C++ 模块单独持有，而是 `C++ shell + JS analyzer` 共同持有。
- `sluaunreal` 的常驻 loop 与 authoring 无关，主要是 profiler。ticker 从队列里取 `FunctionProfileNode` / `MemoryFrame` 刷 inspector。它证明 editor 里也可以有 resident loop，但 owner 完全不在源码 authoring。

[1] `当前 Angelscript / Hazelight` 的 resident loop 都是 watcher -> queue -> reload helper，但只有 current AS 把 watcher 语义锁成了显式自动化测试。

```cpp
// ============================================================================
// [1] 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptEditor\Private\AngelscriptEditorModule.cpp
// 位置: current AS EditorModule.cpp:78-94,366-381; current AS DirectoryWatcherInternal.cpp:43-89;
//       current AS Tests.cpp:15-37,194-222; HZ EditorModule.cpp:47-82,376-389
// 说明: HZ/AS 都是 watcher + queue 架构，但 current AS 额外把 watcher 语义做成正式 regression contract
// ============================================================================
void OnScriptFileChanges(const TArray<FFileChangeData>& Changes)
{
	FAngelscriptEngine& AngelscriptManager = FAngelscriptEngine::Get();
	AngelscriptEditor::Private::QueueScriptFileChanges(
		Changes, AngelscriptManager.AllRootPaths, AngelscriptManager, IFileManager::Get(), ...);
}                                                                    // ★ current AS：所有文件事件先进统一 queue 归一化

DirectoryWatcher->RegisterDirectoryChangedCallback_Handle(
	*RootPath,
	IDirectoryWatcher::FDirectoryChanged::CreateStatic(&OnScriptFileChanges),
	WatchHandle,
	IDirectoryWatcher::IncludeDirectoryChanges);                       // ★ current AS / HZ：对全部 script root 常驻监听

if (AbsolutePath.EndsWith(TEXT(".as")))
{
	Engine.FileChangesDetectedForReload.AddUnique(...);
	Engine.FileDeletionsDetectedForReload.AddUnique(...);
}                                                                    // ★ current AS：文件/删除分成两类 reload 队列

IMPLEMENT_SIMPLE_AUTOMATION_TEST(..., "Angelscript.Editor.DirectoryWatcher.Queue.ScriptAddAndRemove", ...)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(..., "Angelscript.Editor.DirectoryWatcher.Queue.RenameWindowTracksRemoveAndAdd", ...)
                                                                     // ★ current AS：watcher 语义本身就是自动化测试对象
```

[2] `UnrealCSharp / UnLua / puerts / sluaunreal` 的 resident loop owner 分别落在 editor listener、save-fence helper、resident analyzer、和 profiler ticker。

```cpp
// ============================================================================
// [2] 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorFunctionLibrary.cpp
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp
// 文件: Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp
// 文件: Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEDirectoryWatcher.cpp
// 文件: Reference/puerts/unreal/Puerts/PuertsEditor/CodeAnalyze.ts
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_profile/Private/slua_profile.cpp
// 位置: UC FEditorListener.cpp:36-65,239-247,251-261; UL EditorFunctionLibrary.cpp:27-36;
//       UL EditorModule.cpp:155-182; PU PuertsEditorModule.cpp:122-150;
//       PU PEDirectoryWatcher.cpp:14-32,64-68; PU CodeAnalyze.ts:465-475;
//       SL slua_profile.cpp:70-79,116-123
// 说明: 四个参考实现把后台控制环分配给了完全不同的 owner
// ============================================================================
AssetRegistryModule.Get().OnFilesLoaded().AddRaw(this, &FEditorListener::OnFilesLoaded);
DirectoryWatcherModule.Get()->RegisterDirectoryChangedCallback_Handle(..., &FEditorListener::OnDirectoryChanged, ...);
FCodeAnalysis::Analysis(File);
FAssetGenerator::Generator(InAssetData);                             // ★ UC：listener 同时消费目录变化和资产变化

DirectoryWatcher->RegisterDirectoryChangedCallback_Handle(ScriptRootPath, Delegate, DirectoryWatcherHandle);
ULuaFunction::SuspendOverrides(Pair.Value);
ULuaFunction::ResumeOverrides(SuspendedPackages[Package]);           // ★ UL：watcher + save fence 组合成 control loop

SourceFileWatcher = MakeShared<PUERTS_NAMESPACE::FSourceFileWatcher>(...);
JsEnv = MakeShared<PUERTS_NAMESPACE::FJsEnv>(...);
JsEnv->Start("PuertsEditor/CodeAnalyze");                            // ★ PU：editor 里常驻一个 analyzer runtime

var dirWatcher = new UE.PEDirectoryWatcher();
dirWatcher.OnChanged.Add((added, modified, removed) => {
	setTimeout(() => { ... refreshBlueprints(); }, 1000);
});                                                                  // ★ PU：TypeScript 侧继续接管批处理和刷新节流

TickDelegateHandle = FTSTicker::GetCoreTicker().AddTicker(TickDelegate);
sluaProfilerInspector->Refresh(funcProfilerNode, memoryInfo, memoryFrame); // ★ SL：resident loop 只服务 profiler，不服务源码 authoring
```

#### 对比矩阵

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| editor 启动时注册源码目录监听 | Full | Full | Full | Full | None | Full |
| 文件事件先归一化为插件私有队列/批次 | Full | Partial | Partial | Full | None | Full |
| `AssetRegistry` 作为第二信号源参与闭环 | None | Full | None | None | None | None |
| resident analyzer / VM / ticker 持续运行 | None | Partial | None | Full | Full | None |
| 保存期有显式安全栅栏 | None | None | Full | None | None | None |
| watcher 语义有公开自动化测试证据 | None | None | None | None | None | Full |

#### 小结与建议

- `当前 Angelscript` 这一轮最突出的优势不是 watcher 本身，而是**watcher queue semantics 已经被测试化**。这比 HZ/UC/UL/PU/SL 本轮 inspected 到的公开 surface 都更进一步，优先级 `P0`。
- 如果 future AS 引入更重的 analyzer / language service，最值得借鉴的是 `UnrealCSharp` 与 `puerts` 的 owner 分离方式：一个显式 listener / runtime 持有所有 delegate、watcher、shutdown 责任，而不是把常驻逻辑分散到菜单回调里，优先级 `P1`。
- `UnLua` 的 save fence 提醒 current AS：并不是所有 control loop 都该落到 watcher。只要某个阶段天然比文件事件更接近“危险时刻”，就应像 `UnLua` 一样把安全栅栏挂在 save/package 生命周期上，优先级 `P1`。

---

## 深化分析 (2026-04-09 08:31:32)

### [D4/D5] breakpoint continuity 的 owner：reload 之后，谁还能认出“这是同一个停点”

前文已经比较过 `transport`、`source identity` 和 `reload safe point`。这一轮只补一个更细的 contract：**源码变了、模块换了、生成产物刷新了之后，断点 continuity 依赖什么 identity artifact**。这条线把 6 个方案分成 5 类：`Hazelight / 当前 Angelscript` 是插件自持的 `file/module ledger`；`puerts` 是 `Inspector scriptId/url continuity`；`UnrealCSharp` 是 `CLR debugger + embedded symbols`；`UnLua` 是 `Lua VM debug info`；`sluaunreal` 当前公开面仍停在 profiler transport。

#### 各插件实现概览

```
[D4/D5-BreakpointContinuity] Identity Owner After Reload
HZ/AS
├─ SetBreakpoint(Filename, Line, Id)              // 入口是插件自有消息
├─ CanonizeFilename -> ModuleName ledger          // 先做路径/模块归一化
├─ relocate to next code line                     // 断点可以被重定位
├─ echo changed line back to client               // 客户端收到真实可停行
└─ compile done -> ReapplyBreakpoints()           // reload 后显式重挂

PU
├─ Debugger.scriptParsed -> parsedScript map      // continuity 依赖 inspector scriptId
├─ url normalization on Windows                   // 先修 URL 形状
└─ Debugger.setScriptSource(scriptId, source)     // 在旧 scriptId 上换源码

UC
├─ Mono dt_socket debugger agent                  // transport 直接交给 CLR
├─ build emits embedded debug symbols             // symbol 跟编译产物走
└─ plugin does not own breakpoint remap ledger    // 本轮未见插件侧重挂逻辑

UL
├─ lua_getstack / lua_getinfo                     // 以当前 frame 读取 debug info
└─ no plugin-owned persisted breakpoint ledger    // 本轮未见独立账本

SL
└─ profiler TCP port only                         // 当前公开链路不是 breakpoint debugger
```

#### 详细对比

##### 子维度 1：continuity 的 identity 锚点到底是什么

- `Hazelight` 与 `当前 Angelscript` 的锚点都还是**插件自持 ledger**。`SetBreakpoint` 先对 `Filename` 做 `CanonizeFilename()`，再用 `GetModuleByFilenameOrModuleName()` 找脚本模块；如果模块存在，就把断点挂到 `FFileBreakpoints` 和 `ModuleName` 上；如果找不到模块，仍保留一个以 canonized filename 为 key 的账本位。也就是说，断点 continuity 不是委托给底层 VM 的匿名行号，而是插件自己拥有的一层“文件/模块到断点集合”的中介。
- `puerts` 的锚点是 `Inspector` 的 `scriptId`。`hot_reload.js` 先监听 `Debugger.scriptParsed`，在 `parsedScript` 里双向记住 `scriptId <-> url`；后续 hot reload 不是重新注册断点，而是尽量复用旧 `scriptId` 去 `Debugger.setScriptSource()`。这说明 continuity owner 不在插件私有协议，而在 V8 Inspector 的脚本身份模型。
- `UnrealCSharp` 的锚点进一步外移到 `CLR debugger + build artifact`。`FMonoDomain.cpp:96-103` 只负责起 `--debugger-agent=transport=dt_socket`；`Template/Shared.props:8-10` 则把 Debug 配置下的符号交付形态定成 `Embedded`。也就是说 continuity 主要跟着程序集和符号走，而不是跟着 UE 插件自己的 breakpoint ledger 走。
- `UnLua` 的锚点更接近**瞬时 frame**。`UnLuaDebugBase.cpp:621-684` 通过 `lua_getstack()`、`lua_getinfo()`、`lua_getlocal()`、`lua_getupvalue()` 读取当前位置和变量；`LuaEnv.cpp:43-51` 的 hook 也是先取 `nSl`。本轮 inspected 源码里没有与 `FFileBreakpoints` / `scriptId` 对等的持久断点账本，因此 continuity 若存在，也更像 Lua VM 自己的 debug info 连续性，而不是插件自持 contract。
- `sluaunreal` 当前公开的 identity 仍不是 breakpoint。`slua_remote_profile.cpp:22-58` 只看到 `slua.ProfilerPort` 与 `FTcpListener`；这说明 transport owner 在 profiler，不在 source-level breakpoint continuity。

[1] `Hazelight / 当前 Angelscript` 的 continuity 关键，不只是“支持断点”，而是**断点位置可被修正、可被回写、可在 reload 后重新挂回新模块**。

```cpp
// ============================================================================
// [1] 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Debugging/AngelscriptDebugServer.cpp
// 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/AngelscriptManager.cpp
// 位置: current AS DebugServer.cpp:955-1057,1197-1217; Engine.cpp:2499-2502;
//       HZ DebugServer.cpp:845-948,1102-1122; Manager.cpp:1252-1254
// 说明: HZ/AS 都把 breakpoint continuity 做成“路径归一化 + 行号重定位 + reload 后重挂”
// ============================================================================
BP.Filename = CanonizeFilename(BP.Filename);
auto ModuleDesc = Manager.GetModuleByFilenameOrModuleName(BP.Filename, BP.ModuleName);
...
if (CodeLine != WantedLine && BP.Id != -1)
{
	ChangedBP.LineNumber = CodeLine;
	SendMessageToClient(Client, EDebugMessageType::SetBreakpoint, ChangedBP); // ★ 回写真正可停住的代码行
}
...
if (DebugServer != nullptr)
	DebugServer->ReapplyBreakpoints();                                        // ★ compile/reload 结束后把旧账本重新挂回新模块
```

##### 子维度 2：reload / regenerate 之后，谁真的负责 continuity

- `Hazelight / 当前 Angelscript` 是最显式的。`AngelscriptEngine.cpp:2499-2502` 与 `AngelscriptManager.cpp:1252-1254` 都在 compile 结束后直接调 `ReapplyBreakpoints()`。也就是说 reload closure 不只是“模块可执行”，还包含“旧断点继续有效”。
- `puerts` 是**scriptId 复用**路线。`hot_reload.js:67-94` 先用 `url` 或 Windows 斜杠转换后的 `win_url` 找 `scriptId`；找到了才 `Debugger.setScriptSource()`，找不到就警告 `can not find scriptId`。这说明 continuity 是条件式的：只要 inspector 还认得那份脚本身份，就能保住已有断点；一旦 URL 漂移或未经过 `scriptParsed`，continuity 就会断掉。
- `UnrealCSharp` 不在插件层重挂断点，而是让外部 CLR 调试器继续解释程序集符号。这里不是“没有 debug”，而是 **continuity owner 明确不在 UE 插件层**。
- `UnLua` 的 continuity 这轮只确认到**frame introspection**与**save-fence**，没有确认到 source-level breakpoint reapply。它更像在保证 override/runtime 状态安全，而不是在插件层维护 IDE 断点账本。
- `sluaunreal` 这一维应判 `N/A` 于 source-level breakpoint continuity；当前 transport 本身就不是断点调试器。

[2] `puerts / UnrealCSharp / UnLua / sluaunreal` 四个方案，分别把 continuity 交给 `Inspector`、`CLR symbols`、`Lua VM debug info` 和 `profiler transport`。

```cpp
// ============================================================================
// [2] 文件: Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp
// 文件: Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp
// 文件: Reference/UnrealCSharp/Template/Shared.props
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaDebugBase.cpp
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_profile/Private/slua_remote_profile.cpp
// 位置: PU hot_reload.js:21-23,67-94; JsEnvImpl.cpp:3890-3896; PuertsModule.cpp:236-241;
//       UC FMonoDomain.cpp:96-103; Shared.props:8-10;
//       UL UnLuaDebugBase.cpp:621-685; LuaEnv.cpp:43-48;
//       SL slua_remote_profile.cpp:22-25,52-59
// 说明: 另外四个参考实现的 continuity owner 都不在插件私有 breakpoint ledger
// ============================================================================
parsedScript.set(msg.params.scriptId, msg.params.url);
parsedScript.set(msg.params.url, msg.params.scriptId);
...
let res = await sendCommand("Debugger.setScriptSource", {scriptId:"" + scriptId, scriptSource:source}); // ★ PU：复用 inspector scriptId

FString::Printf(TEXT("--debugger-agent=transport=dt_socket,server=y,suspend=n,address=%s:%d"), ...);
<DebugType>Embedded</DebugType>                                                                            // ★ UC：把 symbol continuity 交给 CLR + 编译产物

if (!lua_getstack(L, StackLevel, &ar)) return false;
if (!lua_getinfo(L, "nSlu", &ar)) return false;                                                            // ★ UL：读取当前 frame，而不是插件自持断点账本

Listener = new FTcpListener(ListenEndpoint);                                                               // ★ SL：当前公开 transport 仍是 profiler server
```

##### 子维度 3：谁拥有“可校正的符号真值”

- `Hazelight / 当前 Angelscript` 还有一层别人没有的强化：`SendDebugDatabase()` 不只发 transport，还发 `DebugDatabaseSettings + DebugDatabase`。`当前 Angelscript` 在 `AngelscriptDebugServer.cpp:1493-1515`，`Hazelight` 在 `.../AngelscriptDebugServer.cpp:1368-1393`，都说明 IDE 看到的不只是停/继续命令，还包括**语言模式 + 已注册脚本表面**。这让 breakpoint continuity 不只是“老断点还在”，还意味着客户端可用插件自家符号真值重新解释断点。
- `puerts` 的符号真值来自 inspector runtime 当前已解析的脚本集合；好处是直接复用成熟协议，代价是 continuity 高度依赖 URL 形状和 `scriptParsed` 生命周期。
- `UnrealCSharp` 的真值则是构建物。它对离线 IDE 很友好，但插件自身不拥有 line remap 或 “changed line echo” 这种脚本域特有行为。
- `UnLua` 与 `sluaunreal` 本轮没有定位到对等的独立 symbol DB。这里应判为 `实现方式不同` 或 `未定位到对应实现`，不是简单质量高低。

#### 对比矩阵

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 插件自持 breakpoint ledger（而非完全委托外部 runtime） | Full | None | None | Partial | None | Full |
| 设置断点前先做路径/脚本身份归一化 | Full | None | None | Full | None | Full |
| 断点行号可被重定位并显式回写给客户端 | Full | None | None | Partial | None | Full |
| reload / compile 后有显式 reapply 步骤 | Full | None | None | Partial | None | Full |
| transport 外还有专门的 symbol database / language-mode artifact | Full | None | None | None | None | Full |

#### 小结与建议

- `当前 Angelscript` 在这个观察点上最值得保持的不是 “自定义协议” 四个字，而是**`CanonizeFilename + changed-line echo + ReapplyBreakpoints + DebugDatabase` 形成了一整套 continuity contract**。这条链建议按 `P0` 保持。
- 最值得吸收的参考点来自 `puerts`，但不是把 continuity owner 交给 inspector，而是**把 URL/path 规范化视为 breakpoint contract 本身**。current AS 若继续演进多 IDE / 多平台路径格式，建议把 canonical path 规则前移并固定到 `DebugDatabase` 侧，优先级 `P1`。
- `UnrealCSharp` 提醒的是另一个方向：如果未来 current AS 需要更强的离线 IDE 支持，可以研究额外导出 build-carried symbol snapshot；但它只能是 `DebugDatabase` 的补充层，不能取代当前插件自持 continuity 的主 authority，优先级 `P2`。

### [D7] editor mutation accountability：transaction / undo / compile 谁真的为“这次改动”负责

前文已经比较过 `editor integration`、`authoring truth location` 和 `Blueprint body writer`。这一轮只补一个更底层的问题：**插件一旦真的改了项目内容，这个改动有没有进入明确的 `transaction / undo / compile` contract**。这会把“能改内容”和“对改动负责”清楚分开。

#### 各插件实现概览

```
[D7-MutationAccountability] Who Owns The Edit
HZ/AS
├─ explicit menu action -> FScopedTransaction      // 显式菜单动作进 transaction
├─ selection call under script guard               // 执行上下文受控
└─ create asset via popup/CreateBlueprint          // 不靠后台 graph writer

UC
├─ code file acts like content item                // 代码文件是 editor item
├─ delete -> FScopedTransaction + GUndo            // 真正进入 undo 栈
└─ Apply/Revert -> ImmediatelyCompile()            // 修改和编译同 owner

PU
├─ analyzer decides blueprint diff                 // owner 先落在 analyzer
├─ some graph inserts use FScopedTransaction       // 局部有 transaction
├─ bulk graph/node remove via diff-save            // 不是每步都包事务
└─ Save() -> MarkBlueprintAsModified -> Compile    // 最终收口明确

UL
├─ package save fence -> Suspend/ResumeOverrides   // 更像保存期安全栅栏
└─ no explicit transaction-wrapped asset mutation  // 本轮未见对等写操作事务

SL
├─ profiler tab / wrapper tool button             // 公开面主要是工具入口
└─ no inspected authoring mutation path            // 本轮未见项目内容写回链
```

#### 详细对比

##### 子维度 1：mutation owner 是显式用户动作，还是后台系统

- `Hazelight / 当前 Angelscript` 在这一维都非常保守。`ScriptEditorMenuExtension.cpp` 的路径是“用户点菜单 -> `FScopedTransaction` -> `CallFunctionOnSelection()`”；`AngelscriptEditorModule.cpp` 的新建脚本类型路径则是 popup 中显式选择目标包，再调用 `NewObject(... RF_Transactional ...)` 或 `FKismetEditorUtilities::CreateBlueprint()`。也就是说，**插件允许改内容，但把改动 owner 留在显式 operator surface**。
- `UnrealCSharp` 把 owner 再往 editor 内核里推进了一步。`DynamicDataSource.cpp:574-582` 显示删除代码文件本身就被包进 `FScopedTransaction + GUndo`；`FDeleteFileChange::Apply/Revert()` 又在 `Apply()` 和 `Revert()` 里各自 `ImmediatelyCompile()`。这不是“旁路脚本文件系统”，而是 editor 正式拥有这次 mutation。
- `puerts` 则把 owner 一分为二。图写入的局部动作，例如 `CreateOverrideFunctionGraph`，会单独包 `FScopedTransaction`；但真正决定 diff 的是 analyzer 产出的 `FunctionAdded` 集合，后续还会直接 `RemoveAll()` function graph 与 event node。也就是说，**operator 看得见的 transaction 只覆盖了一部分，真正的 mutation authority 已经前移到 analyzer**。
- `UnLua` 更像 save fence。`UnLuaEditorModule.cpp:160-182` 只在保存 package 前后 suspend/resume overrides；它在保护运行时一致性，但 inspected source 里没有与 `FScopedTransaction` 对等的 editor 写操作 wrapper。
- `sluaunreal` 当前公开 surface 主要是 profiler tab 与 `lua_wrapper` 工具按钮。它能起工具，但本轮没看到对等的项目内容 mutation path。

[1] `Hazelight / 当前 Angelscript` 的共同点，是**只给显式动作加 transaction，不让后台 authoring loop 直接接管 Blueprint 本体**。

```cpp
// ============================================================================
// [1] 文件: Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp
// 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 位置: current AS MenuExtension.cpp:268-279,367-375; EditorModule.cpp:501-517;
//       HZ MenuExtension.cpp:244-255,350-358; EditorModule.cpp:525-541
// 说明: HZ/AS 都是“显式动作 -> transaction / transactional asset”，而不是 resident graph writer
// ============================================================================
FScopedTransaction Transaction(FText::FromString(Function->GetDisplayNameText().ToString()));
TempObject->CallFunctionOnSelection(Function, Selection);            // ★ 选中对象上的脚本动作进入 UE transaction

Asset = NewObject<UDataAsset>(Package, Class, AssetName, RF_Public | RF_Transactional | RF_Standalone);
Asset = FKismetEditorUtilities::CreateBlueprint(
	Class, Package, AssetName, BPTYPE_Normal, BlueprintClass, BlueprintGeneratedClass, FName("AngelscriptCreateBlueprint")
);                                                                   // ★ 新资产通过显式 popup/create 路径创建，不由后台分析器偷偷改图
```

##### 子维度 2：undo / compile closure 是否和 mutation owner 同步

- `UnrealCSharp` 是最彻底的同步 closure。`FDeleteFileChange::Apply()` / `Revert()` 在真正改文件后立刻 `ImmediatelyCompile()`；而 `DeleteItem()` 本身再把 `FDeleteFileChange` 塞进 `GUndo`。这意味着**改动、撤销、重新编译**三件事由同一条 editor contract 收口。
- `puerts` 是局部同步、整体偏 analyzer。`CreateOverrideFunctionGraph` 会 `Blueprint->Modify()` 并包 `FScopedTransaction`；但 `Blueprint->FunctionGraphs.RemoveAll()`、`EventGraph->Nodes.RemoveAll()` 这类 bulk diff 没有一一包事务，最终统一在 `NeedSave` 成立时 `MarkBlueprintAsModified()` + `CompileBlueprint()`。因此它不是没有 accountability，而是 accountability 更多落在 `Save()/Compile` 这一层，而不是每一个 graph diff 点。
- `Hazelight / 当前 Angelscript` 当前 inspected 路径下更像**transactional command + asset creation**，而不是 `graph diff + compile` pipeline。它们在 editor mutation 的风险面更小，但也意味着如果未来要引入更深的自动图写入，就必须补齐更强的 closure contract。

[2] `UnrealCSharp` 把 mutation 直接做成可撤销文件变更；`puerts` 则把局部 transaction 与最终 compile 收口拼在一起。

```cpp
// ============================================================================
// [2] 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp
// 文件: Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp
// 位置: UC DynamicDataSource.cpp:37-51,574-582;
//       PU PEBlueprintAsset.cpp:479-487,1319-1335,1354-1357
// 说明: UC 的 undo/compile 同 owner；PU 的 transaction 只覆盖部分写入，最终靠 Save/Compile 收口
// ============================================================================
FScopedTransaction Transaction(LOCTEXT(LOCTEXT_NAMESPACE, "Delete File"));
DeleteFileChange->Apply(nullptr);
GUndo->StoreUndo(this, MoveTemp(DeleteFileChange));                  // ★ UC：删除代码文件本身就是可撤销 mutation

FCSharpCompiler::Get().ImmediatelyCompile();                         // ★ UC：Apply / Revert 后立刻重新编译

const FScopedTransaction Transaction(LOCTEXT("CreateOverrideFunctionGraph", "Create Override Function Graph"));
Blueprint->Modify();
FBlueprintEditorUtils::AddFunctionGraph(Blueprint, NewGraph, /*bIsUserCreated=*/false, OverrideFuncClass); // ★ PU：局部 graph insert 有 transaction

Blueprint->FunctionGraphs.RemoveAll([&](UEdGraph* Graph) { return !FunctionAdded.Contains(Graph->GetFName()); });
EventGraph->Nodes.RemoveAll(...);                                    // ★ PU：bulk diff 不逐项包 transaction
FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
FKismetEditorUtilities::CompileBlueprint(Blueprint);                 // ★ 最终 consistency closure 在 Save/Compile
```

##### 子维度 3：save fence / tool UI 能不能等价替代 transaction

- `UnLua` 的答案是不能完全等价。`SuspendOverrides()` / `ResumeOverrides()` 解决的是 package save 期间脚本 override 的安全窗口，不是“这次 editor mutation 可以撤销、可追踪、会立即同步”的 contract。它非常重要，但职责不同。
- `sluaunreal` 则提醒另一端：只有 tab、menu、外部按钮，不代表就拥有 mutation accountability。`slua_profile.cpp:48-83` 与 `lua_wrapper.cpp:58-71` 说明 inspected surface 主要是打开 profiler tab 或 wrapper 工具，而不是项目内容写回。

[3] `UnLua / sluaunreal` 当前 inspected source 更像“安全栅栏 / 工具入口”，而不是 transaction-owned authoring mutation。

```cpp
// ============================================================================
// [3] 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_profile/Private/slua_profile.cpp
// 文件: Reference/sluaunreal/Plugins/lua_wrapper/Source/lua_wrapper/Private/lua_wrapper.cpp
// 位置: UL UnLuaEditorModule.cpp:160-182;
//       SL slua_profile.cpp:48-83; lua_wrapper.cpp:58-71
// 说明: UL 的 owner 是保存期安全栅栏；SL 的公开面还是工具菜单/Tab
// ============================================================================
ULuaFunction::SuspendOverrides(Pair.Value);
...
ULuaFunction::ResumeOverrides(SuspendedPackages[Package]);           // ★ UL：save fence 保护 override，不是 undo transaction

MenuExtender->AddMenuExtension("WindowLayout", ...);
FGlobalTabmanager::Get()->RegisterNomadTabSpawner(slua_profileTabName, ...);
FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(lua_wrapperTabName); // ★ SL：公开 surface 主要是 tool UI，不是内容写回链
```

#### 对比矩阵

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| inspected editor mutation 由显式 `FScopedTransaction` 包裹 | Full | Full | None | Partial | None | Full |
| mutation 明确进入 UE undo / reversible contract | Full | Full | None | Partial | None | Full |
| inspected mutation owner 主要是显式用户动作 | Full | Full | None | Partial | None | Full |
| inspected 主路径里存在后台 analyzer / save hook 驱动的静默变更 | None | None | Partial | Full | None | None |
| 修改后插件自己承担 compile / sync closure | Partial | Full | Partial | Full | None | Partial |

#### 小结与建议

- `当前 Angelscript` 在这个观察点上的关键优点，是**把 mutation 权限控制在显式 operator surface**。这不是“功能少”，而是风险预算更清楚，建议按 `P0` 保持。
- 真正值得吸收的不是 `puerts` 的“后台自动改图”本身，而是它**至少让最终 `Save()/Compile()` 成为明确 closure owner**。如果 future AS 真要增加 graph writer 或 asset patcher，至少要把 `transaction / compile / diagnostics` 收回同一 owner，优先级 `P1`。
- `UnrealCSharp` 提供的是另一条高质量路径：**一旦 editor 真把代码文件当一等内容项，就必须同时给 undo 和 compile contract**。current AS 若以后补 source-first authoring，不应只给“打开文件”按钮，而应同步考虑这两层责任，优先级 `P1`。

### [D1/D7] 长驻服务的退出合同：谁会对 watcher / runtime / tab / settings 做对称 teardown

前文已经比较过 `bootstrap`、`resident loop` 和 `tool surface`。这一轮只补 **teardown symmetry**：插件把多少长期状态拉进 editor/runtime，就有没有把同等强度的退出清理写回模块与 owner 对象。这里的差异直接关系到多次启停、模块重载和测试隔离。

#### 各插件实现概览

```
[D1/D7-TeardownSymmetry] Who Cleans Long-Lived State
AS(now)
├─ RuntimeModule::Shutdown -> RemoveTicker -> Pop engine context -> Reset engine
├─ EditorModule::Shutdown -> remove pre-save handle -> unregister state dump -> unregister tool menus
└─ ScriptEditorMenuExtension unregisters extender handles explicitly

HZ
├─ CodeModule::Shutdown -> no-op
└─ EditorModule::Shutdown -> unregister tool menus only

UC
├─ FEditorListener dtor -> unregister DirectoryWatcher callbacks
├─ EditorModule::Shutdown -> deinit toolbars, remove ticker, unregister settings
└─ MonoDomain::Deinitialize -> unload assemblies / reflection registry

UL
├─ EditorModule::Shutdown -> remove init/save delegates, unregister settings
└─ RuntimeModule::Shutdown -> SetActive(false)

PU
├─ EditorModule::Shutdown -> reset JsEnv + SourceFileWatcher
├─ SourceFileWatcher dtor -> unregister watched dirs
└─ PuertsModule::Shutdown -> unregister settings / map delegates

SL
├─ profiler shutdown -> unregister commands/tab
└─ teardown mostly stops at UI/tool layer
```

#### 详细对比

##### 子维度 1：editor 侧 delegate / menu / handle 有没有对称清理

- `当前 Angelscript` 的 editor teardown 已经明显比同谱系的 `Hazelight` 更厚。`AngelscriptEditorModule.cpp:676-689` 会移除 `OnObjectPreSave` handle、注销 state dump extension、再注销 tool menu owner；`ScriptEditorMenuExtension.cpp:676-703` 还会按 `DelegateHandle` 把之前注册进 `LevelEditor` 的扩展逐个 `RemoveAll()`。这说明 current AS 已经把 editor 常驻面当成正式生命周期对象治理。
- `Hazelight` 的 `AngelscriptEditorModule.cpp:699-704` 只保留了 `UToolMenus` 的注销，而 `AngelscriptCodeModule.cpp:12-14` 的 `ShutdownModule()` 还是空实现。这里不应写成“没有清理”，更准确的说法是：**module-level teardown 明显更薄，很多状态仍假设由 manager/singleton 常驻持有**。
- `UnrealCSharp` 的 editor 清理最系统。`FEditorListener::~FEditorListener()` 显式 `UnregisterDirectoryChangedCallback_Handle()`；`UnrealCSharpEditor.cpp:127-170` 再 deinit toolbar、`RemoveTicker()`、注销 settings、释放 `DynamicDataSource`。这说明它把 editor resident services 明确拆给 owner object 和 module 两层。
- `UnLua` 的 editor 清理也完整，但职责更窄。`UnLuaEditorModule.cpp:72-83` 主要是 `OnPostEngineInit`、`PreSave/PackageSaved` 和 settings 的移除；即它清的是 save fence 与 UI hook，而不是像 UC/PU 那样的一整套 analyzer runtime。
- `puerts` 则是典型的 owner-dtor 组合。`PuertsEditorModule.cpp:154-163` 只需要 `Reset()` 掉 `JsEnv` 和 `SourceFileWatcher`；真正 watcher handle 的注销在 `SourceFileWatcher::~FSourceFileWatcher()` 里完成。
- `sluaunreal` 的 teardown 最轻。`slua_profile.cpp:88-97` 只清 profiler inspector、commands 和 tab；这和它的 editor surface 本来就主要停在 profiler UI 一致。

[1] `当前 Angelscript` 相比 `Hazelight` 的新增点，不是“终于有 shutdown”，而是**runtime/editor 两边都开始把长期状态视为必须对称收口的 first-class contract**。

```cpp
// ============================================================================
// [1] 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp
// 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/AngelscriptCodeModule.cpp
// 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 位置: current AS RuntimeModule.cpp:27-39; EditorModule.cpp:676-689; MenuExtension.cpp:676-703;
//       HZ CodeModule.cpp:12-14; EditorModule.cpp:699-704
// 说明: current AS 已经把 runtime/editor 常驻状态写回 shutdown；HZ 的 module-level teardown 仍更薄
// ============================================================================
if (FallbackTickHandle.IsValid())
{
	FTSTicker::GetCoreTicker().RemoveTicker(FallbackTickHandle);
	FallbackTickHandle.Reset();
}
if (OwnedPrimaryEngine.IsValid())
{
	FAngelscriptEngineContextStack::Pop(OwnedPrimaryEngine.Get());
	OwnedPrimaryEngine.Reset();                                         // ★ current AS：runtime owner 真正退场
}

FCoreUObjectDelegates::OnObjectPreSave.Remove(GLiteralAssetPreSaveHandle);
AngelscriptEditor::Private::UnregisterStateDumpExtension(StateDumpExtensionHandle);
UToolMenus::UnregisterOwner(this);                                    // ★ current AS：editor handles 也跟着收口

void FAngelscriptCodeModule::ShutdownModule() {}                      // ★ HZ：runtime code module 在 module plane 仍是 no-op
UToolMenus::UnregisterOwner(this);                                    // ★ HZ：editor 只做最薄的 UI cleanup
```

##### 子维度 2：runtime / VM / domain 的退出深度

- `当前 Angelscript` 的 runtime 退出深度在 6 个方案里最明确之一。除了删 ticker，还会 `Pop(OwnedPrimaryEngine)`，这意味着它把**engine-context ownership** 也写成了 teardown contract。结合 `AngelscriptSubsystemTests.cpp:372-385` 里直接构造 `FAngelscriptRuntimeModule`、调用 `StartupModule()/ShutdownModule()` 并断言 ticker 被清掉，可以看出这不是“顺手写了个 reset”，而是已经进入回归面。
- `UnrealCSharp` 的 runtime 退出深度落在 CLR domain。`FMonoDomain::Deinitialize():135-145` 先 `FReflectionRegistry::Deinitialize()`，再 `UnloadAssembly()` 与 `DeinitializeAssembly()`；也就是说，plugin 不只停 editor listener，还明确释放 managed 运行时载荷。
- `puerts` 的 runtime owner 则是 `JsEnv`。`PuertsEditorModule.cpp:154-163` / `PuertsModule.cpp:501-507` 的 `Reset()` 与 settings/map delegate 清理，配合 `SourceFileWatcher::~FSourceFileWatcher():92-102` 的 watcher 注销，说明它依赖 owner object 析构去冲洗长期状态。
- `UnLua` 的 runtime 退出更保守。`UnLuaModule.cpp:80-84` 只 `UnregisterSettings()` 并 `SetActive(false)`；这更像把 Lua runtime 退回 inactive 状态，而不是彻底释放一整套 editor/runtime service graph。
- `sluaunreal` 这一维在 inspected plugin modules 上应判 `N/A/Partial`。当前公开 teardown 基本收束在 profiler / tool UI 层，没有与 `MonoDomain`、`JsEnv` 或 `OwnedPrimaryEngine` 对等的长期 runtime owner。

[2] `UnrealCSharp / UnLua / puerts / sluaunreal` 各自把 teardown 重心放在 CLR domain、save fence runtime、owner object 析构和 UI/tool 层。

```cpp
// ============================================================================
// [2] 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaModule.cpp
// 文件: Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/SourceFileWatcher.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_profile/Private/slua_profile.cpp
// 位置: UC FEditorListener.cpp:69-83; UnrealCSharpEditor.cpp:127-170; FMonoDomain.cpp:135-145;
//       UL UnLuaEditorModule.cpp:72-83; UnLuaModule.cpp:80-84;
//       PU PuertsEditorModule.cpp:154-163; SourceFileWatcher.cpp:92-102;
//       SL slua_profile.cpp:88-97
// 说明: 其它 4 个参考实现分别把 teardown 放在 dtor、module、domain 或 UI 层
// ============================================================================
DirectoryWatcherModule.Get()->UnregisterDirectoryChangedCallback_Handle(Directory, OnDirectoryChangedDelegateHandle);
DynamicDataSource.Reset();
FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
UUnrealCSharpEditorSetting::UnregisterSettings();
UnloadAssembly();
DeinitializeAssembly();                                               // ★ UC：editor + CLR domain 双层 teardown

FCoreDelegates::OnPostEngineInit.RemoveAll(this);
UPackage::PreSavePackageWithContextEvent.RemoveAll(this);
SetActive(false);                                                     // ★ UL：把 editor/save hooks 和 runtime active 状态关掉

JsEnv.Reset();
SourceFileWatcher.Reset();
DirectoryWatcher->UnregisterDirectoryChangedCallback_Handle(KV.Key, KV.Value); // ★ PU：owner dtor 负责 watcher cleanup

Flua_profileCommands::Unregister();
FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(slua_profileTabName);       // ★ SL：teardown 基本停在 UI/tool 层
```

##### 子维度 3：cleanup 责任有没有真正回到 owner 对象

- `当前 Angelscript` 与 `UnrealCSharp / puerts` 的共同点，是都在往“**谁创建，谁清理**”的 owner 模式靠。区别在于 current AS 目前仍有一部分清理直接写在 module 和 menu extension 里，而 UC/PU 已经更多借助 listener / watcher / domain 这类 owner object 的析构函数完成。
- `Hazelight` 的对比价值在这里最清楚：不是它“做错了”，而是它的生命周期假设还更偏 singleton/manager 常驻，module 级对称性较弱。对于当前已经扩展出 `StateDump`、更多 tests、更多 tool surface 的 AS 来说，这种薄 shutdown 预算已经不够。
- `sluaunreal` 则提供一个反例提醒：如果插件表面主要是 UI/tool，而不是 authoring runtime，自然不需要像 UC/PU/AS 一样有很重的 teardown graph。这里应判为 `实现范围不同`，不是质量差异。

#### 对比矩阵

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| editor shutdown 显式注销 menu / settings / delegates | Partial | Full | Full | Full | Full | Full |
| runtime / VM / domain 在 shutdown 时显式 reset / unload | Partial | Full | Partial | Full | N/A | Full |
| cleanup 由 owner object / dtor 承担显式责任 | Partial | Full | Partial | Full | Partial | Full |
| 插件自有 engine/context stack 在 shutdown 时被显式 unwind | None | None | None | None | N/A | Full |
| inspected runtime core 的 `ShutdownModule()` 仍接近 no-op | Full | None | None | None | None | None |

#### 小结与建议

- `当前 Angelscript` 在这一维新增确认的真实优势，是**module-level teardown 已经和它当前的架构复杂度基本匹配**，不再停留在 Hazelight 那种更薄的假设上。这条线建议按 `P0` 保持。
- 真正值得吸收的是 `UnrealCSharp / puerts` 的 owner 化清理模式。future AS 若继续增加 resident analyzer、debug client、language service，一开始就应把 cleanup 写进 owner object 的析构或明确 `Shutdown()`，而不是等 module 里堆一串 `RemoveAll()` 之后再收拾，优先级 `P1`。
- `Hazelight` 给 current AS 的启发不是“补更多 shutdown 代码”，而是**只要架构已经从 manager singleton 扩成 runtime/editor/test/tool 多平面，就不能再容忍 runtime core 的 no-op shutdown**。current AS 现在已经迈过这一步，下一步更应该补的是对称生命周期回归测试，而不是回退到更薄的假设，优先级 `P1`。

---

## 深化分析 (2026-04-09 08:50:16)

### [D2] `UInterface` 的真实落点：正式 schema，还是只在 `FScriptInterface` 边界上短暂出现

前文只把 `UInterface` 记成 current Angelscript 的一个“未完全统一的 family”。这一轮继续压源码后，可以把差异说得更准：当前 `Angelscript` 已经不只是“能判断对象是否实现接口”，而是把 interface 语义前移到了 `preprocessor -> class generator -> generic cast` 三层；`UnrealCSharp` 也能生成真正的 `CLASS_Interface`，但运行时 property bridge 仍通过托管对象回填 `FScriptInterface`；`UnLua / puerts / sluaunreal` 则主要把 interface 当成 `property/function translator` 的编组问题处理，脚本侧主视图依旧更接近底层 `UObject` 代理。

#### 各插件实现概览

```
[D2-InterfaceContract] Where Interface Semantics Actually Live
HZ
└─ UObject opCast -> Object->IsA(AssociatedClass)                // 插件路径里仍是类继承 cast

AS(now)
├─ parse `class X : IMyInterface`
├─ RegisterInterfaceMethodSignature + CallInterfaceMethod
├─ ClassGenerator -> CLASS_Interface + PointerOffset=0
└─ UObject::ImplementsInterface / interface-aware opCast

UC
├─ DynamicInterfaceGenerator -> CLASS_Interface
├─ FCSharpBind scans IncludeInterfaces
└─ InterfacePropertyDescriptor rebuilds FScriptInterface

UL / PU / SL
└─ FInterfaceProperty bridge
   ├─ GetObject() / PushUObject() / JS object proxy
   └─ GetInterfaceAddress() only on write-back
```

#### 详细对比

##### 子维度 1：谁在“类型系统层”承认 interface

- `当前 Angelscript` 已经把 interface 从“对象能不能 cast 成某种类型”提升成了**正式 schema**。`Preprocessor` 会为 interface method 建 `RegisterInterfaceMethodSignature()` 并把 `CallInterfaceMethod` 注册到 AS type；`ClassGenerator` 再把新类标成 `CLASS_Interface | CLASS_Abstract`，并用 `PointerOffset = 0` 把实现类与 `UInterface` 元数据缝合起来。这里的 owner 已经不是 property marshaller，而是脚本类生成主链。
- `Hazelight` 在本轮可见的插件路径里还停在更早的层次。`Bind_UObject.cpp` 的泛型 cast 仍只有 `Object->IsA(AssociatedClass)` 分支，没有 current 新增的 `ImplementsInterface` 分支；本轮也没有在 `AngelscriptCode` 插件路径内定位到 current 这套 `RegisterInterfaceMethodSignature / ImplementedInterfaces / PointerOffset=0` 对等块。因此这一维更准确的判断是：**HZ 在插件可见路径里仍偏 cast-level 支持，而 current 已经前移到 schema-level 支持**。
- `UnrealCSharp` 也有 formal schema。`FDynamicInterfaceGenerator` 明确把动态生成类打成 `CLASS_Abstract | CLASS_Native | CLASS_Interface`；`FCSharpBind` 遍历 `IncludeInterfaces` 参与 override/bind。也就是说，UC 并不满足于“property 能写回接口”，它也把 interface 当成动态 reflection schema 的组成部分。
- `UnLua / puerts / sluaunreal` 则都更像 **marshaller-first**。它们会识别 interface function 或 interface property，但在脚本值层看到的主对象仍然是 `UObject` / JS object proxy / Lua userdata，本体语义主要在写回 `FScriptInterface` 时才被重新组装。

[1] `当前 Angelscript` 已把 interface 拉进 schema 与 cast 主链；`Hazelight` 在插件路径里仍停在类继承 cast：

```cpp
// ============================================================================
// [1] 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp
// 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_UObject.cpp
// 位置: current Preprocessor.cpp:1106-1139; current ClassGenerator.cpp:3360-3364, 5135-5183;
//       current Bind_UObject.cpp:100-106, 188-213; HZ Bind_UObject.cpp:124-151
// 说明: current AS 已把 interface 变成正式 schema；HZ 插件路径里仍主要是 `IsA` cast
// ============================================================================
// current AS: interface method 先注册到 AS type，后续 interface 引用可直接调
auto* Sig = Engine.RegisterInterfaceMethodSignature(FName(*MethodName));
int32 FuncId = Engine.Engine->RegisterObjectMethod(
    TCHAR_TO_ANSI(*InterfaceName),
    TCHAR_TO_ANSI(*ASDecl),
    asFUNCTION(CallInterfaceMethod),
    asCALL_GENERIC,
    nullptr);

// current AS: 类生成阶段直接落成真正的 UInterface schema
NewClass->ClassFlags |= CLASS_Interface | CLASS_Abstract;
// Do NOT set CLASS_Native — this makes GetInterfaceAddress() return this (PointerOffset=0)
ImplementedInterface.PointerOffset = 0;
...
if (ImplFunc == nullptr || bResolvedToInterfaceStub)
{
    FAngelscriptEngine::Get().ScriptCompileError(...); // ★ 实现类缺接口函数时，swap-in 前直接报错
}

// current AS: generic cast 显式理解 interface
UObject_.Method("bool ImplementsInterface(const UClass InterfaceClass) const", ...);
const bool bAssociatedClassIsInterface = AssociatedClass->HasAnyClassFlags(CLASS_Interface);
const bool bImplementsInterface = bAssociatedClassIsInterface && Object->GetClass()->ImplementsInterface(AssociatedClass);
...
else if (bImplementsInterface)
{
    *(UObject**)OutAddress = Object;
}

// HZ: 插件路径里泛型 cast 仍只看 IsA，没有 current 这条 interface-aware 分支
if (Object->IsA(AssociatedClass))
{
    *(UObject**)OutAddress = Object;
}
else
{
    *(UObject**)OutAddress = nullptr;
}
```

##### 子维度 2：property / function 边界上，interface 是“独立类型”还是“临时重建”

- `UnrealCSharp` 虽然能生成 `CLASS_Interface`，但 property bridge 仍清楚地把 interface 当成**回填型 carrier**。`FInterfacePropertyDescriptor::Set()` 是先取托管对象，再在 native 边界上执行 `Interface->SetObject()` + `SetInterface(Object ? GetInterfaceAddress(...) : nullptr)`。也就是说，formal schema 与 marshaller 双层都在，只是值语义最终还是通过 `FScriptInterface` 回写。
- `UnLua` 更明显是 marshaller-first。`FInterfacePropertyDesc::GetValueInternal()` 直接 `PushUObject(L, Interface.GetObject())`；写回时再 `SetInterface(Value ? Value->GetInterfaceAddress(...) : nullptr)`，运行时还会在 `CheckPropertyType()` 上做 `ImplementsInterface()` 校验。脚本侧拿到的首先是对象，不是独立 interface 值对象。
- `puerts` 与 `sluaunreal` 基本同类。前者 `FInterfacePropertyTranslator::UEToJs()` 直接把 `Interface.GetObject()` 映射成 JS object proxy，`JsToUE()` 写回时才 `GetInterfaceAddress()`；后者 `pushUInterfaceProperty()` 直接把 `FScriptInterface.GetObject()` 压成 Lua userdata，`checkUInterfaceProperty()` 再做 `GetInterfaceAddress()` 和错误报告。

[2] `UnrealCSharp` 是 “formal schema + marshaller 回填” 双层；`UnLua / puerts / sluaunreal` 更偏 `marshaller-first`：

```cpp
// ============================================================================
// [2] 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicInterfaceGenerator.cpp
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FCSharpBind.cpp
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/ObjectProperty/FInterfacePropertyDescriptor.cpp
// 位置: UC FDynamicInterfaceGenerator.cpp:162-170; FCSharpBind.cpp:234-240;
//       FInterfacePropertyDescriptor.cpp:29-45
// 说明: UC 既生成 `CLASS_Interface`，也在 property bridge 边界回填 `FScriptInterface`
// ============================================================================
InClass->SetSuperStruct(InParentClass);
InClass->ClassFlags |= CLASS_Abstract | CLASS_Native | CLASS_Interface; // ★ 动态 interface 本身就是正式 reflection schema

for (TFieldIteratorExt<UFunction> It(FoundClass, EFieldIteratorFlags::IncludeSuper,
                                     EFieldIteratorFlags::ExcludeDeprecated,
                                     EFieldIteratorFlags::IncludeInterfaces); It; ++It)
{
    ... // ★ bind 时把 interface 函数也纳入遍历面
}

Interface->SetObject(Object);
Interface->SetInterface(Object ? Object->GetInterfaceAddress(Property->InterfaceClass) : nullptr);
// ★ 值写回阶段仍通过 FScriptInterface 重建 native carrier
```

```cpp
// ============================================================================
// [3] 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaObject.cpp
// 位置: UL PropertyDesc.cpp:541-560, 568-576;
//       PU PropertyTranslator.cpp:605-630; FunctionTranslator.cpp:113-115;
//       SL LuaObject.cpp:1869-1875, 2596-2611
// 说明: UL / PU / SL 的 interface 主体都更靠近 marshaller，而不是独立脚本类型
// ============================================================================
// UnLua: 读时直接退化成 UObject，写回时才恢复 interface 指针
const FScriptInterface &Interface = InterfaceProperty->GetPropertyValue(ValuePtr);
UnLua::PushUObject(L, Interface.GetObject());
...
Interface->SetObject(Value);
Interface->SetInterface(Value ? Value->GetInterfaceAddress(InterfaceProperty->InterfaceClass) : nullptr);
if ((Class) && (!Class->ImplementsInterface(InterfaceProperty->InterfaceClass)))
{
    ... // ★ 约束发生在运行时类型检查
}

// puerts: JS 侧拿到的仍是 Object proxy；写回时再补 FScriptInterface
UObject* Object = Interface.GetObject();
return FV8Utils::IsolateData<IObjectMapper>(Isolate)->FindOrAdd(Isolate, Context, Object->GetClass(), Object);
...
Interface->SetInterface(Object ? Object->GetInterfaceAddress(InterfaceProperty->InterfaceClass) : nullptr);
IsInterfaceFunction = (OuterClass->HasAnyClassFlags(CLASS_Interface) && OuterClass != UInterface::StaticClass());

// slua: push 时只压 UObject；check 时再按 InterfaceClass 校验并重建
auto &scriptInterface = p->GetPropertyValue(parms);
UObject *obj = scriptInterface.GetObject();
return LuaObject::push(L, obj, ref);
...
void* interfacePtr = obj ? obj->GetInterfaceAddress(p->InterfaceClass) : nullptr;
p->SetPropertyValue(parms, FScriptInterface(obj, interfacePtr));
```

#### 对比矩阵

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| interface 在脚本工具链里被生成为正式 `CLASS_Interface` schema | None | Full | None | None | None | Full |
| implementing class 在激活前会校验必需 interface method | None | Partial | None | None | None | Full |
| interface 语义主要落在 property / function marshaller 边界 | None | Partial | Full | Full | Full | Partial |
| generic cast 路径显式分支 `ImplementsInterface` | None | Partial | None | Partial | None | Full |

#### 小结与建议

- `当前 Angelscript` 在这一维最值得保持的，不是“能 cast interface”，而是**已经把 interface 前移成 formal schema + pre-activation validation**。这条线建议按 `P0` 保持，不要退回 marshaller-only。
- `Hazelight` 对 current 的最大启发，不是补一个 `ImplementsInterface()` helper，而是说明**插件血缘里原本只有 cast-level 语义，current 这轮 formalization 是真实架构增量**。如果后续继续补 interface 相关 bind/doc/debugger，应沿 current 路线统一。
- 可吸收点主要来自 `UnrealCSharp`：formal schema 与 marshaller 回填可以并存。若 current 未来要补 interface property 的更完整桥接，优先级 `P1`。
- `UnLua / puerts / sluaunreal` 的做法更适合“桥接即可”的 VM 模型，不适合拿来替换 current 已经建立起来的 interface schema 路线，优先级 `P2`。

### [D3] override 之后，谁真的替你保住 `super`

此前已经比较过 override 的 owner 与 patch 深度。这一轮只继续压一条更具体的链：**脚本覆写之后，父函数是被“保存在 UFunction 继承链里”，还是仅仅靠脚本表或 VM 缓存侧写住**。这条差异直接决定 override 可回滚性，以及 reload/rebind 后 `super` 还能不能稳定存在。

#### 各插件实现概览

```
[D3-SuperContract] Who Preserves Parent Behavior
HZ / AS(now)
├─ allocate new UASFunction
└─ NewFunction->SetSuperStruct(ParentFunction)          // super 进入 UFunction 继承链

UC
├─ DuplicateFunction(original, class, newName)
├─ store original/native func in FCSharpFunctionRegister
└─ patch callable entry to execCallCSharp

UL
├─ duplicate `__Overridden` UFunction
├─ SetSuperStruct(Function or Function->GetSuperStruct())
└─ Restore() replays old native/script state

PU
├─ rename old method if same outer
├─ duplicate UJSGeneratedFunction
├─ SetSuperStruct(Super or Super->GetSuperStruct())
└─ CancelFunctionRedirection() rolls patch back

SL
├─ duplicate newFunc and SetSuperStruct(templateFunction)
├─ inject `SUPER_NAME`
└─ LuaSuperCall resolves and caches the real super UFunction
```

#### 详细对比

##### 子维度 1：override 主路径是“新 callable”，还是“原位打补丁”

- `Hazelight / 当前 Angelscript` 都是 **new UFunction first**。类生成阶段直接 `AllocateFunctionFor()`，然后 `NewFunction->SetSuperStruct(ParentFunction)`。这说明它们的 super 语义主要托管在 UE 原生函数继承链，而不是额外的 script-side helper 表。
- `UnrealCSharp` 虽然最终会把原函数入口换成 `execCallCSharp`，但第一步仍是 `DuplicateFunction()`，并把 `OriginalFunction / OverrideFunction / 原始 flags / 原始 NativeFunc` 一起放进 `FCSharpFunctionRegister`。这说明 UC 的 super/restore owner 更像“duplicated callable + register ledger”。
- `UnLua / puerts / sluaunreal` 都会复制旧 `UFunction`。差别在于：`UnLua` 把旧函数保成 `__Overridden` 并提供 `Restore()`；`puerts` 会在同 `Outer` 下 rename 旧函数，再 duplicate 成 `UJSGeneratedFunction`；`sluaunreal` 除了 duplicate 新函数，还额外暴露 `LuaSuperCall` 作为显式 super helper。

[1] `Hazelight / 当前 Angelscript / UnrealCSharp` 都把 parent callable 留在 native callable graph 里，只是 owner 不同：

```cpp
// ============================================================================
// [1] 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/ClassGenerator/AngelscriptClassGenerator.cpp
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FCSharpBind.cpp
// 位置: current AS ClassGenerator.cpp:3410-3412; HZ ClassGenerator.cpp:3258-3260;
//       UC FCSharpBind.cpp:333-366
// 说明: HZ/AS 把 super 直接挂进新 UFunction；UC 则用 duplicate + register ledger 记住旧 callable
// ============================================================================
// current AS
auto* NewFunction = UASFunction::AllocateFunctionFor(NewClass, FunctionName, FunctionDesc);
NewFunction->SetSuperStruct(ParentFunction); // ★ super 直接进 UFunction 继承链

// HZ
auto* NewFunction = UASFunction::AllocateFunctionFor(NewClass, FunctionName, FunctionDesc);
NewFunction->SetSuperStruct(ParentFunction); // ★ upstream 同样依赖 UFunction super 链

// UnrealCSharp
const auto OverrideFunction = DuplicateFunction(OriginalFunction, InClass, *NewFunctionName);
FCSharpEnvironment::GetEnvironment().AddFunctionHash<FCSharpFunctionDescriptor>(
    FunctionHash, InClassDescriptor, OriginalFunction,
    FCSharpFunctionRegister(OriginalFunction, OverrideFunction,
                            OriginalFunction->FunctionFlags, OriginalFunction->GetNativeFunc()));
OriginalFunction->SetNativeFunc(UCSharpFunction::execCallCSharp);
OriginalFunction->FunctionFlags |= FUNC_Native;
// ★ UC 把旧 callable 和原始 native 指针一起记进 register，再 patch 当前入口
```

##### 子维度 2：super 是自动沿 `SuperStruct` 走，还是显式 helper object

- `当前 Angelscript / Hazelight / UnrealCSharp / UnLua / puerts` 的共同点，是都让 parent callable 仍然以 `UFunction` 或 original callable 的形式存在；其中 `UnLua` 和 `puerts` 也都会显式 `SetSuperStruct(...)`。
- `sluaunreal` 走得更极端。它不仅让新函数 `SetSuperStruct(templateFunction)`，还在 Lua `self table` 里塞了 `SUPER_NAME -> LuaSuperCall`；`LuaSuperCall::__superIndex()` 会动态找 super `UFunction`，再用 `LuaFunctionAccelerator` 缓存。这意味着 slua 的 super contract 有两层 owner：一层是 duplicated `UFunction`，一层是显式 Lua helper。

[2] `UnLua / puerts / sluaunreal` 都复制旧 callable；slua 额外把 `super` 产品化成 Lua helper：

```cpp
// ============================================================================
// [2] 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaFunction.cpp
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaOverrides.cpp
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JSGeneratedClass.cpp
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/TypeScriptGeneratedClass.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaOverrider.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaOverriderSuper.cpp
// 位置: UL LuaFunction.cpp:160-167, 172-188, 214-227; LuaOverrides.cpp:61-82;
//       PU JSGeneratedClass.cpp:104-145; TypeScriptGeneratedClass.cpp:245-264;
//       SL LuaOverrider.cpp:1151-1158, 1328-1330; LuaOverriderSuper.cpp:69-88, 95-110
// 说明: UL / PU / SL 都保存旧 callable；SL 还把 `super` 做成显式 LuaSuperCall
// ============================================================================
// UnLua: 复制旧函数，保成 __Overridden，必要时完整 Restore
Overridden = static_cast<UFunction*>(StaticDuplicateObject(Function, GetOuter(), *DestName));
Overridden->SetNativeFunc(Function->GetNativeFunc());
...
Old->Script = Script;
Old->SetNativeFunc(Overridden->GetNativeFunc());
Old->FunctionFlags = Overridden->FunctionFlags;
...
SetSuperStruct(Function);
...
SetSuperStruct(Function->GetSuperStruct());

// puerts: 同 Outer 先 rename 旧函数，再 duplicate 新 JSGeneratedFunction
Super->Rename(*FString::Printf(TEXT("%s%s"), TEXT(OLD_METHOD_PREFIX), *Super->GetName()), Class, ...);
UJSGeneratedFunction* Function = Cast<UJSGeneratedFunction>(
    StaticDuplicateObject(Super, Class, FunctionName, RF_Transient, UJSGeneratedFunction::StaticClass()));
Function->SetSuperStruct(Super->GetSuperStruct());
Function->SetNativeFunc(&UJSGeneratedFunction::execCallJS);
...
CancelFunctionRedirection(Function); // ★ 需要时显式撤销 redirection

// slua: duplicated UFunction 之外，再显式暴露 SUPER_NAME -> LuaSuperCall
newFunc->SetSuperStruct(templateFunction);
...
lua_pushstring(L, SUPER_NAME);
LuaObject::pushType(L, new LuaSuperCall((UObject*)objPtr), "LuaSuperCall", ...);
...
UFunction* func = getSuperFunction<LuaSuperCall>(L);
lua_pushlightuserdata(L, LuaFunctionAccelerator::findOrAdd(func));
// ★ super 既保存在 UFunction 链里，也被包装成脚本可见 helper
```

#### 对比矩阵

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| override 主路径会保留一份独立 parent/original callable | Full | Full | Full | Full | Full | Full |
| parent linkage 明确挂在 `SetSuperStruct` 或等价 register 上 | Full | Full | Full | Full | Full | Full |
| override 撤销有显式 `Restore` / `Cancel` API | None | Partial | Full | Full | Full | None |
| `super` 语义主要依赖 UE callable graph，而不是纯脚本表缓存 | Full | Full | Partial | Full | Partial | Full |

#### 小结与建议

- `当前 Angelscript` 在这一维的关键优点，是**super 语义直接依托 UE 的 `UFunction::SuperStruct` 链**。这让 override 不需要额外 Lua/JS 式 helper 才能成立，建议按 `P0` 保持。
- 真正值得吸收的是 `UnLua / puerts` 的显式 rollback contract，而不是它们的 patch 深度本身。current AS 若未来要支持更细粒度的 runtime override/unoverride，优先级 `P1`。
- `sluaunreal` 的 `LuaSuperCall` 说明另一条路：当语言层本身缺少自然的 super surface 时，可以把 super 显式产品化。但对 current AS 来说，它应是**附加 authoring sugar**，不应替代现有 `SuperStruct` 主链，优先级 `P2`。

### [D11] shipped runtime 的自扩展权：交付后还能不能长出新模块 / 新 native surface

此前 D11 已经比较过 artifact、路径 contract 和 self-verification gate。这一轮只收窄到一个更具体的问题：**交付之后，运行时还有没有“自己长出新东西”的权力**。这里的差异很实在：`Hazelight / 当前 Angelscript` 倾向 sealed snapshot；`UnrealCSharp` 倾向 published assembly set；`UnLua` 倾向 loose script root；`puerts` 甚至把 native addon loader 暴露给脚本层；`sluaunreal` 则把脚本装载权直接交给宿主 `GameInstance`。

#### 各插件实现概览

```
[D11-SelfExtension] Who Can Grow After Shipping
HZ / AS(now)
├─ load Binds.Cache / PrecompiledScript*.Cache
├─ AS(now) optionally loads BindModules.Cache at startup
└─ precompiled mode => hot reload disabled

UC
├─ Build -> Publish -> copy DLLs into Content/<PublishDir>
└─ runtime Domain loads published assemblies only

UL
├─ ScriptRoot = ProjectContent/Script/
├─ LoadFile() reads loose bytes from disk
└─ require(module) binds Lua table at runtime

PU
├─ staged VM DLLs / NonUFS runtime deps
├─ JS `puerts.load(path)` -> LoadPesapiDll()
└─ `puerts.loadCPPType()` lazily exposes native classes

SL
├─ host GameInstance injects loadFileDelegate
└─ requireModule() only consumes bytes provided by host
```

#### 详细对比

##### 子维度 1：交付物是 sealed snapshot，还是运行时仍直接吃 loose source

- `Hazelight / 当前 Angelscript` 的交付边界都偏 sealed。启动时先读 `Binds.Cache`、再找 `PrecompiledScript_{Cfg}.Cache` / `PrecompiledScript.Cache`；一旦真的走 fully precompiled path，就明确打印 “Hot reloading is disabled for this run”。current 虽然比 HZ 多了一层 `BindModules.Cache`，但它仍是**启动期装载**，不是 script-callable live extension。
- `UnrealCSharp` 的 primary input 也是 sealed publish set，只是形式换成 DLL。`Game.props` 在 `Build` 后强制跑 `Publish` 并复制 `$(PublishDir)*.dll`；运行时 `FCSharpEnvironment` 再按 `GetFullAssemblyPublishPath()` 建 domain。也就是说，UC 的 runtime 只认已发布程序集，不认 loose C# source。
- `UnLua` 明显相反。`GetScriptRootPath()` 固定给出 `ProjectContentDir()/Script/`；`LoadFile()` 直接从磁盘读字节，`UUnLuaManager::Bind()` 再执行 `require(InModuleName)`。这意味着 shipped runtime 仍然可以直接吃 loose Lua module。
- `sluaunreal` 更进一步把路径 authority 外包。插件只保留 `setLoadFileDelegate()` 与 `requireModule()`；真正的脚本根目录、扩展名顺序和字节来源，都在宿主 `MyGameInstance` 里决定。

[1] `Hazelight / 当前 Angelscript` 都偏 sealed snapshot；fully precompiled 时会主动宣布“不要再指望 live mutation”：

```cpp
// ============================================================================
// [1] 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp
// 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/AngelscriptManager.cpp
// 位置: current AS Engine.cpp:1468-1488, 1521-1555, 1583-1587, 2054-2056;
//       current Binds.cpp:53-56; HZ Manager.cpp:407-451, 507-509, 913-915
// 说明: HZ / AS 都把交付物收口成 cache/snapshot；current AS 只是在启动期多了一层 bind-module 装载
// ============================================================================
FAngelscriptBindDatabase::Get().Load(GetScriptRootDirectory() / TEXT("Binds.Cache"), bGeneratePrecompiledData);
FAngelscriptBinds::LoadBindModules(plugin->GetBaseDir() / "BindModules.Cache");
for (FString ModuleName : FAngelscriptBinds::GetBindModuleNames())
{
    FModuleManager::Get().LoadModule(FName(ModuleName), ELoadModuleFlags::LogFailures);
} // ★ current AS 可扩展点仍在 startup，而不在 script runtime

Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript_Development.Cache");
if (!IFileManager::Get().FileExists(*Filename))
    Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript.Cache");
...
PrecompiledData->Save(Filename); // ★ 正式交付物是 cache/snapshot

UE_LOG(Angelscript, Warning, TEXT("Using fully precompiled scripts. Hot reloading is disabled for this run."));
UE_LOG(Angelscript, Warning, TEXT("Delete PrecompiledScript.Cache or run with -as-development-mode flag to enable hot reload."));
// ★ fully precompiled 路径明确否决 live mutation

// HZ：同样读 Binds.Cache / PrecompiledScript*.Cache，且同样在 fully precompiled 时禁用热重载
FAngelscriptBindDatabase::Get().Load(GetScriptRootDirectory() / TEXT("Binds.Cache"), bGeneratePrecompiledData);
Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript_Development.Cache");
...
PrecompiledData->Save(Filename);
UE_LOG(Angelscript, Warning, TEXT("Using fully precompiled scripts. Hot reloading is disabled for this run."));
```

##### 子维度 2：binary publish 是“构建物”，还是脚本层可继续请求的新载荷

- `UnrealCSharp` 的 binary publish 仍是 build-owned。`Game.props` 的 `AfterBuildPublish` / `CopyDllsAfterPublish` 会把 DLL 复制进 `ScriptOutputPath`；运行时 `FCSharpEnvironment` 只按 `GetFullAssemblyPublishPath()` 建立 domain。也就是说，它可以消费**新的发布 DLL**，但这件事仍是外部构建步骤，不是脚本语言自己发起。
- `puerts` 是本轮最激进的。宿主在 `JsEnvImpl` 里把 `loadCPPType`、`load` 和 `dll_ext` 直接挂给 JS；`LoadPesapiDll()` 再在运行时 `GetDllHandle()` / `GetDllExport()` / 校验 `PESAPI_VERSION`；`pesaddon.js` 继续把 `global.puerts.load()` 包成 `Proxy`，访问属性时再 `loadCPPType(module.class)`。这意味着 shipped runtime 不只是能读 JS source，而是真的允许**脚本层动态拉起新的 native addon surface**。

[2] `UnrealCSharp` 的 DLL 是 build-owned publish set；`puerts` 则把 native addon loader 直接暴露给脚本层：

```cpp
// ============================================================================
// [2] 文件: Reference/UnrealCSharp/Template/Game.props
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PesapiAddonLoad.cpp
// 文件: Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/pesaddon.js
// 位置: UC Game.props:16-27; FUnrealCSharpFunctionLibrary.cpp:1005-1041; FCSharpEnvironment.cpp:54-59;
//       PU JsEnvImpl.cpp:499, 576-591; PesapiAddonLoad.cpp:76-126; pesaddon.js:24-49
// 说明: UC 只消费 publish DLL；PU 则让脚本侧自己发起 native addon 装载
// ============================================================================
// UnrealCSharp: publish 是 build contract，不是脚本 runtime API
<Target Name="AfterBuildPublish" AfterTargets="Build">
    <MSBuild Projects="$(ProjectPath)" Targets="Publish" Properties="Configuration=$(Configuration)" />
</Target>
<Target Name="CopyDllsAfterPublish" AfterTargets="Publish">
    <PublishFiles Include="$(PublishDir)*.dll" />
    <Copy SourceFiles="@(PublishFiles)" DestinationFolder="$(ScriptOutputPath)" />
</Target>

return FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / GetPublishDirectory());
return TArrayBuilder<FString>().
       Add(GetFullUEPublishPath()).
       Add(GetFullGamePublishPath()).
       Append(GetFullCustomProjectsPublishPath()).
       Build();
Domain = new FDomain({ "", FUnrealCSharpFunctionLibrary::GetFullAssemblyPublishPath() });
// ★ runtime 只认已 publish 的 assembly set

// puerts: 宿主直接把 native addon loader 暴露给 JS
MethodBindingHelper<&FJsEnvImpl::LoadCppType>::Bind(Isolate, Context, PuertsObj, "loadCPPType", This);
PuertsObj->Set(Context, FV8Utils::ToV8String(Isolate, "load"),
    v8::FunctionTemplate::New(Isolate, LoadPesapiDll)->GetFunction(Context).ToLocalChecked()).Check();
PuertsObj->Set(Context, FV8Utils::ToV8String(Isolate, "dll_ext"), ...).Check();

void* DllHandle = FPlatformProcess::GetDllHandle(*Path);
auto Init = (const char* (*) (pesapi_func_ptr*) )(uintptr_t) FPlatformProcess::GetDllExport(DllHandle, *EntryName);
...
FV8Utils::ThrowException(Info.GetIsolate(),
    FString::Printf(TEXT("pesapi version mismatch, expect: %d, but got %d"), PESAPI_VERSION, PesapiVersion));
// ★ DLL ABI 校验也是 runtime 当场做，不等外部构建系统兜底

const module_name = org_load(filepath);
moduleCache[filepath] = new Proxy({__name : module_name}, {
    get: function(classCache, className) {
        if (!(className in classCache)) {
            classCache[className] = puerts.loadCPPType(`${module_name}.${className}`);
        }
        return classCache[className];
    }
});
global.puerts.load = load;
// ★ JS 自己就能把新 native surface 长出来
```

##### 子维度 3：路径 authority 在插件自己，还是在宿主应用

- `UnLua` 的 authority 还在插件自己。`GetScriptRootPath()` 固定 `ProjectContentDir()/Script/`，`LoadFile()` 直接读取该根下字节，`Bind()` 再执行 `require()`。它给 shipped runtime 保留了 loose script 权利，但 canonical root 仍是插件定义的。
- `sluaunreal` 则把 authority 直接交给宿主。`LuaState` 只有 `setLoadFileDelegate()` / `requireModule()`；demo host `MyGameInstance` 决定路径是 `ProjectContentDir()/Lua`，再按 `.lua/.luac` 顺序找文件。插件本体不拥有 canonical root，也不拥有 packaging receipt 里的 script source contract。

[3] `UnLua` 保留 loose script root，但 root 由插件规定；`sluaunreal` 直接把 loader authority 外包给宿主：

```cpp
// ============================================================================
// [3] 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaFunctionLibrary.h
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaFunctionLibrary.cpp
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaBase.cpp
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaManager.cpp
// 文件: Reference/sluaunreal/Source/democpp/MyGameInstance.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaState.cpp
// 位置: UL UnLuaFunctionLibrary.h:27-34; UnLuaFunctionLibrary.cpp:20-33;
//       UnLuaBase.cpp:66-89; UnLuaManager.cpp:63-71;
//       SL MyGameInstance.cpp:41-59; LuaState.cpp:755-783
// 说明: UL 的 loose script root 仍是 plugin-owned；SL 把 loader authority 交给 host
// ============================================================================
// UnLua: 插件自己定义 canonical script root，并直接从磁盘读字节再 require
UFUNCTION(BlueprintCallable)
static FString GetScriptRootPath();
...
return FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() + TEXT("Script/"));
...
FString FullFilePath = GetFullPathFromRelativePath(RelativeFilePath);
TArray<uint8> Data;
bool bSuccess = FFileHelper::LoadFileToArray(Data, *FullFilePath, 0);
return LoadChunk(L, (const char*)(Data.GetData() + SkipLen), Data.Num() - SkipLen, ...);
...
UnLua::FLuaRetValues RetValues = UnLua::Call(L, "require", TCHAR_TO_UTF8(InModuleName));

// slua: 插件只保存 delegate；真正路径与字节来源由 host 决定
state->setLoadFileDelegate([](const char* fn, FString& filepath)->TArray<uint8> {
    FString path = FPaths::ProjectContentDir();
    path /= "Lua";
    ...
    FFileHelper::LoadFileToArray(Content, *fullPath);
    return MoveTemp(Content);
});
...
LuaVar LuaState::requireModule(const char* fn, LuaVar* pEnv) {
    lua_getglobal(L, "require");
    lua_pushstring(L, fn);
    if (lua_pcall(L, 1, 1, top))
        return LuaVar();
}
```

#### 对比矩阵

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| runtime 可以直接消费 loose script/module，而不必先 publish 新二进制 | Partial | None | Full | Full | Full | Partial |
| 脚本语言表面可以主动请求装载新的 native addon / binary surface | None | None | None | Full | None | None |
| startup 明显受 sealed publish/cache artifact 集约束 | Full | Full | None | Partial | None | Full |
| script loader 的 source-of-truth 明确由宿主应用而不是插件自己掌握 | None | None | None | None | Full | None |

#### 小结与建议

- `当前 Angelscript` 在这一维的正确定位不是“不能扩展”，而是**扩展权大多停在 startup artifact 与 build-time bind module**。`BindModules.Cache` 让它比 HZ 多一点启动期弹性，但 fully precompiled 之后仍是 sealed runtime，这条边界建议按 `P0` 保持。
- 真正值得吸收的参考点来自两端：`puerts` 说明如果要开放 runtime native addon，必须把 ABI/version check 也一起产品化；`sluaunreal` 则说明如果不想让插件拥有 loader authority，可以明确把脚本字节来源外包给宿主。两条路都比“半开放半封闭”更清楚。
- `UnLua` 的 loose script root 可以借鉴，但只适合 `development/operator tool` 层。若 current AS 继续以 `Binds.Cache + PrecompiledScript*.Cache` 为正式交付协议，就不应把 loose-source 模式重新抬成 shipping 主路径，优先级 `P2`。

---
## 深化分析 (2026-04-09 09:03:59)

### [D1/D2] 绑定图谱的可审计性：注册顺序是正式账本，还是隐式初始化副作用

前文已经比较过绑定生命周期、快路径与 fallback。这一轮只补一个更偏长期维护的问题：**当你想回答“到底注册了什么、按什么顺序、能不能禁掉某一组、出了问题去哪查”时，插件内部有没有一张正式账本**。这条线把 6 个方案分成了三类：`Hazelight / 当前 Angelscript` 是静态 bind graph；`UnrealCSharp / UnLua` 是 registry-owner；`puerts / sluaunreal` 更接近 runtime cache / command sequence。

#### 各插件实现概览

```
[D1/D2-Auditability] Binding Graph Record
HZ      : static FBind(order) -> sort -> execute
AS(now) : static FBind(name+order) -> disable set + observation + CSV dump
UC      : Environment::Initialize -> registries -> Domain.RegisterBinding()
UL      : LuaEnv registries -> RegisterReflectedType on first metatable push
PU      : ForeachRegisterClass list + StructWrapper lazy translator maps
SL      : LuaState::init() command sequence + cache tables
```

#### 详细对比

##### 子维度 1：绑定单元有没有稳定、可查询的名字和顺序

- `当前 Angelscript` 已经把 bind graph 做成了正式元数据。`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:431-474` 明确声明 `FBindInfo{BindName, BindOrder, bEnabled}`；`.../AngelscriptBinds.cpp:151-219` 则把静态 bind 收进 `FBindFunction` 数组，排序后执行，并在 dev automation 下记录 observation。它不只是“有顺序”，而是**名字、顺序、启停状态都能回读**。
- `Hazelight` 仍是同谱系，但账本明显更薄。`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/AngelscriptBinds.h:507-525` 的 `FBind` 只有 `BindOrder`；`.../Private/AngelscriptBinds.cpp:17-43` 也只是 `GetBindArray().Sort();` 后逐个执行。这里应判为 `实现质量差异`，不是路线不同。
- `UnrealCSharp` 的 owner 在 registry graph，而不在排序表。`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp:54-89` 启动时成批创建 `ClassRegistry / StructRegistry / DelegateRegistry / BindingRegistry`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp:123-132,783-790` 再把 `FBinding::Get().Register().GetClasses()` 遍历成 `mono_add_internal_call`。它能回答“有哪些 registry 在场”，但不像 current AS 那样有一张直接对应 bind pass 的 ordered ledger。
- `UnLua` 更像按需 registry。`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:98-112` 初始化若干 registry；真正碰到类型时，`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:59-138` 才 `RegisterReflectedType()` / `PushMetatable()`。它能缓存“当前 LuaEnv 已见过的 reflected type”，但**这份表是访问结果，不是启动期绑定账本**。
- `puerts` 介于“可枚举静态类表”和“运行时懒缓存”之间。`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/TemplateBindingGenerator.cpp:193-216` 的 `ForeachRegisterClass` 说明它确实有一张可遍历的注册类表；但 `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:38-67,282-327` 又把 method/function translator 懒建到 `MethodsMap / FunctionsMap`，最终 runtime 行为仍主要落在 wrapper cache。
- `sluaunreal` 的绑定可运行，但本轮没找到对等 ledger。`Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaState.cpp:526-583,621-638` 展示的是命令式 `init()`：建立 cache table、再 `LuaObject::init / LuaClass::reg / LuaArray::reg / LuaMap::reg / LuaSet::reg`。顺序当然存在，但顺序知识只活在代码里，没有被提升成可查询账本。

[1] `当前 Angelscript` 与 `Hazelight` 的差异，不在“有没有 static bind”，而在 current AS 已经把它变成了可观察账本：

```cpp
// ============================================================================
// [1] 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp
// 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/AngelscriptBinds.h
// 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/AngelscriptBinds.cpp
// 位置: current H:431-474,583-601; current CPP:120-219; current Dump:847-872;
//       HZ H:507-525; HZ CPP:17-43
// 说明: current AS 有名字/顺序/启停状态/CSV 导出；HZ 只有 order-only 数组
// ============================================================================
struct FBindInfo
{
    FName BindName;
    int32 BindOrder = 0;
    bool bEnabled = true;
};

FBind(FName BindName, int32 BindOrder, TFunction<void()> Function)
{
    FAngelscriptBinds::RegisterBinds(BindName, BindOrder, MoveTemp(Function));
}

TArray<FBindInfo> FAngelscriptBinds::GetBindInfoList(const TSet<FName>& DisabledBindNames)
{
    for (const FBindFunction& Bind : GetSortedBindArray())
        BindInfos.Add({Bind.BindName, Bind.BindOrder, !DisabledBindNames.Contains(Bind.BindName)});
}

for (const FBindFunction& Bind : GetSortedBindArray())
{
    if (DisabledBindNames.Contains(Bind.BindName))
        continue;                                            // ★ current AS 可以按名字跳过整组 bind
    FAngelscriptBindExecutionObservation::RecordExecutedBind(Bind.BindName);
    Bind.Function();
}

Writer.AddHeader({ TEXT("BindName"), TEXT("BindModule"), TEXT("bIsSkipped"), TEXT("SkipReason") });
Writer.AddRow({ BindInfo.BindName.ToString(), FString(), BoolToString(bIsSkipped), ... });
// ★ bind pass 结果还能直接外显成 CSV

// HZ：同一路线但只剩 order，没有名字、禁用面和状态导出
struct FBindFunction
{
    int32 BindOrder;
    TFunction<void()> Function;
};

void FAngelscriptBinds::RegisterBinds(int32 BindOrder, TFunction<void()> Function)
{
    GetBindArray().Add({BindOrder, Function});
}

void FAngelscriptBinds::CallBinds()
{
    GetBindArray().Sort();
    for (auto& Function : GetBindArray())
        Function.Function();
}
```

##### 子维度 2：registry 的失效与清理有没有明确 owner

- `UnrealCSharp` 的优点是 owner 很清楚。`FCSharpEnvironment` 负责生出各类 registry；`FMonoDomain` 负责把 binding 注册进 CLR。虽然它缺少 current AS 那种 bind-pass ledger，但“谁拥有注册状态”至少是明示的。
- `UnLua` 的强点则在**失效路径真实存在**。`LuaEnv.cpp:265-274` 在 `NotifyUObjectDeleted()` 里把 `PropertyRegistry / FunctionRegistry / ObjectRegistry / ClassRegistry / EnumRegistry` 一起通知；`ClassRegistry.cpp:108-138,275-278` 又会在 metatable 失效时 `Unregister()`。所以它不是“只有缓存，没有治理”，而是治理发生在 `LuaEnv` 生命周期内。
- `puerts` 和 `sluaunreal` 在这条线上更偏热路径 cache。`StructWrapper.cpp:38-67` 的 translator maps 与 `LuaState.cpp:570-583` 的 cache tables 都能显著降成本，但它们主要回答“下次怎么更快”，不直接回答“这轮绑定清单到底长什么样”。

[2] `UnrealCSharp` 与 `UnLua` 都有 owner，但 owner 是 registry 生命周期，不是 bind pass 账本：

```cpp
// ============================================================================
// [2] 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp
// 位置: UC Env:54-89; UC Domain:123-132,783-790; UL Env:98-112,265-274;
//       UL ClassRegistry:59-138,275-278
// 说明: UC/UL 都有显式 owner，但它们关心的是 registry 生命周期，而不是导出一张 ordered bind 表
// ============================================================================
void FCSharpEnvironment::Initialize()
{
    Domain = new FDomain({ "", FUnrealCSharpFunctionLibrary::GetFullAssemblyPublishPath() });
    ClassRegistry = new FClassRegistry();
    StructRegistry = new FStructRegistry();
    DelegateRegistry = new FDelegateRegistry();
    BindingRegistry = new FBindingRegistry();
}   // ★ UC：先建立 owner graph

if (bLoadSucceed)
    FReflectionRegistry::Get().Initialize();
RegisterBinding();

for (const auto& Class : FBinding::Get().Register().GetClasses())
    for (const auto& Method : Class->GetMethods())
        mono_add_internal_call(TCHAR_TO_ANSI(*Method.GetMethod()), Method.GetFunction());
// ★ UC：binding 最终注册进 CLR runtime

luaL_openlibs(L);
AddSearcher(LoadFromCustomLoader, 2);
AddSearcher(LoadFromFileSystem, 3);
ObjectRegistry = new FObjectRegistry(this);
ClassRegistry = new FClassRegistry(this);
ClassRegistry->Initialize();
FunctionRegistry = new FFunctionRegistry(this);
// ★ UL：registry 是 LuaEnv 的一部分

if (Ret && Ret->IsClass() && !Ret->IsStructValid())
{
    Unregister(Ret, true);                           // ★ metatable 失效时会主动注销
}
FClassDesc* ClassDesc = RegisterReflectedType(MetatableName);

void FLuaEnv::NotifyUObjectDeleted(const UObjectBase* ObjectBase, int32 Index)
{
    PropertyRegistry->NotifyUObjectDeleted(Object);
    FunctionRegistry->NotifyUObjectDeleted(Object);
    ClassRegistry->NotifyUObjectDeleted(Object);
}   // ★ 失效 owner 明确在 LuaEnv
```

##### 子维度 3：生成器或运行时 cache 能不能替代正式 bind ledger

- `puerts` 的 `ForeachRegisterClass` 很有价值，但它更多服务 `d.ts` 生成和 wrapper 展开，不是拿来回答“本次 runtime 到底按什么顺序装配了什么”。`JsEnvImpl.cpp:931-983` 里的 extension method map 也是首次访问时才初始化。
- `sluaunreal` 的 `LuaState::init()` 更像一次命令脚本：cache 表、对象桥、容器桥、profiler 依次挂上。能跑、也快，但 operator 很难像 current AS 那样直接得到“BindName -> Enabled -> SkipReason”。

[3] `puerts / sluaunreal` 的强项在运行时快路径，不在可审计 bind ledger：

```cpp
// ============================================================================
// [3] 文件: Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/TemplateBindingGenerator.cpp
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaState.cpp
// 位置: PU Gen:193-216; PU StructWrapper:38-67,282-327; PU JsEnvImpl:931-983;
//       SL LuaState:526-583,621-638
// 说明: PU/SL 都有“可运行、可缓存”的装配逻辑，但没有 current AS 那种 bind-pass ledger
// ============================================================================
PUERTS_NAMESPACE::ForeachRegisterClass(
    [&](const PUERTS_NAMESPACE::JSClassDefinition* ClassDefinition)
    {
        if (ClassDefinition->TypeId && ClassDefinition->ScriptName)
            Gen.GenClass(ClassDefinition);
    });
// ★ PU：有可遍历类表，但主要用于生成产物

auto Iter = MethodsMap.Find(InFunction->GetFName());
if (!Iter)
{
    auto FunctionTranslator = std::make_shared<FFunctionTranslator>(InFunction, false);
    MethodsMap.Add(InFunction->GetFName(), FunctionTranslator);
}
// ★ PU：method/function translator 是 lazy map，不是启动期 bind 清单

for (TObjectIterator<UClass> It; It; ++It)
{
    ...
    ExtensionMethodsMap[Struct].push_back(Function);
}
// ★ extension method 也是运行时扫描后才成图

cacheObjRef = newCacheTable(L);
cacheEnumRef = luaL_ref(L, LUA_REGISTRYINDEX);
cacheClassPropRef = luaL_ref(L, LUA_REGISTRYINDEX);
cacheClassFuncRef = luaL_ref(L, LUA_REGISTRYINDEX);
LuaObject::init(L);
LuaClass::reg(L);
LuaArray::reg(L);
LuaMap::reg(L);
LuaSet::reg(L);
// ★ SL：初始化顺序清楚，但元数据没有被提升成可导出的 bind 账本
```

#### 对比矩阵

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 绑定单元有稳定 `name + order` 元数据 | None | Partial | Partial | Partial | None | Full |
| 可直接导出 / 查看绑定账本，而不必重走初始化 | None | Partial | Partial | Partial | None | Full |
| 可按组禁用或跳过部分绑定 | None | Partial | None | None | None | Full |
| 绑定失效 / 清理有明确 owner | None | Full | Full | Partial | Partial | Full |
| runtime 热路径主要依赖 lazy cache，而不是启动期 bind pass | None | Partial | Full | Full | Full | Partial |

#### 小结与建议

- `当前 Angelscript` 在这一维最难得的地方，不只是 static bind，而是**static bind 已经被产品化成 named / disable-able / observable ledger**。这条路线建议按 `P0` 保持。
- `Hazelight` 的启发很具体：current AS 不该回退到 order-only。相反，应该继续把 `BindModules.Cache`、`BindRegistrations.csv` 这类产物补齐 module/source 信息，优先级 `P1`。
- `UnrealCSharp / UnLua` 说明 registry owner 也很重要。current AS 如果未来继续扩 bind graph，建议保留账本主线，同时补更明确的 owner-to-teardown 映射，而不是把治理退回到隐式 cache，优先级 `P1`。

### [D11] loose-file patch seam 的 owner：默认 overlay、delegate 注入，还是 sealed cache

前文已经比较过 artifact、self-verification 和 shipped runtime 自扩展权。这一轮只收窄到一个更实际的问题：**线上想覆盖旧脚本时，补丁入口到底在哪一层**。这里的差异非常大：`Hazelight / 当前 Angelscript` 倾向 sealed cache；`UnrealCSharp` 倾向 republish binary；`UnLua` 把 overlay 写进默认 loader 顺序；`puerts` 给了宽松的 source module 搜索；`sluaunreal` 则把 byte-source authority 交给宿主。

#### 各插件实现概览

```
[D11-Overlay] Post-Ship Patch Seam
HZ / AS(now) : Binds.Cache + PrecompiledScript*.Cache -> no built-in loose overlay
UC           : Build/Publish DLLs -> runtime only reads publish set
UL           : CustomLoadLuaFile -> CustomLoaders -> PersistentDownloadDir -> ProjectDir
PU           : require dir -> parent dirs -> ProjectContent/<ScriptRoot> -> JavaScript fallback
SL           : host LoadFileDelegate -> .lua/.luac bytes
```

#### 详细对比

##### 子维度 1：默认 overlay 是插件内建，还是根本没有

- `当前 Angelscript` 与 `Hazelight` 的 shipping 主路径都没有 built-in loose overlay。`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1425-1532,2046-2056` 说明 current AS 会先定 `bUsePrecompiledData`，再读 `Binds.Cache`、`BindModules.Cache` 与 `PrecompiledScript_{Build}.Cache`，并在 fully precompiled 路径明确宣布 hot reload 关闭。`J:/UnrealEngine/UEAS2/.../AngelscriptManager.cpp:407-451,507-509` 的 HZ 同样是 cache-first。
- `UnrealCSharp` 也没有 loose overlay，只是 artifact 形式换成 DLL。`Template/Game.props:16-27` 先 `Publish` 再复制 `*.dll`；`AssemblyLoader.cpp:6-20` 运行时只在 assembly path 里找二进制。这里不是“更灵活”，而是 **patch 必须重新 publish**。
- `UnLua` 则把 loose overlay 做成默认 loader 顺序的一部分。`LuaEnv.cpp:560-638` 先允许 `CustomLoadLuaFile` / `CustomLoaders` 抢占；进入文件系统后，又明确先查 `ProjectPersistentDownloadDir()`，再查 `ProjectDir()`。这不是样例工程技巧，而是 runtime 主路径。
- `puerts` 的 overlay 更偏“宽搜索面”，不是“明确补丁目录优先级”。`DefaultJSModuleLoader.cpp:67-143` 会从 require 目录、父目录、`ProjectContent/<ScriptRoot>` 和 `ProjectContent/JavaScript` 找 source / bytecode；`modular.js:134-154` 还处理 `Pak:` debugPath fallback 与 `.mbc/.cbc`。它给了很多入口，但补丁 seam 更像搜索算法，而不是正式 overlay policy。
- `sluaunreal` 则把 seam 全部外包。`LuaState.h:110,169-189` 公开的只有 `LoadFileDelegate`；`LuaState.cpp:131-155,651-653` 只是把 delegate 调起来；真实优先级在宿主 `democpp/MyGameInstance.cpp:42-60` 里自己决定 `.lua -> .luac` 顺序。

[1] `当前 Angelscript / Hazelight` 的正式交付 seam 都偏 sealed cache，不内建 loose-file overlay：

```cpp
// ============================================================================
// [1] 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/AngelscriptManager.cpp
// 位置: current:1425-1532,2046-2056; HZ:407-451,507-509
// 说明: 两边都把 shipping 主路径收敛到 cache artifact，不把 loose overlay 写成默认策略
// ============================================================================
bUsePrecompiledData = !bGeneratePrecompiledData && !RuntimeConfig.bIgnorePrecompiledData
    && !RuntimeConfig.bRunningCommandlet && !WITH_EDITOR && !bScriptDevelopmentMode;

FAngelscriptBindDatabase::Get().Load(GetScriptRootDirectory() / TEXT("Binds.Cache"), bGeneratePrecompiledData);
FAngelscriptBinds::LoadBindModules(plugin->GetBaseDir() / "BindModules.Cache");

Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript_Shipping.Cache");
...
if (!IFileManager::Get().FileExists(*Filename))
    Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript.Cache");

UE_LOG(Angelscript, Warning, TEXT("Using fully precompiled scripts. Hot reloading is disabled for this run."));
UE_LOG(Angelscript, Warning, TEXT("Delete PrecompiledScript.Cache or run with -as-development-mode flag to enable hot reload."));
// ★ current AS：想切回 loose/dev path，需要显式退出 sealed cache 模式

// HZ：同样先吃 Binds.Cache / PrecompiledScript*.Cache，再把新快照写回正式文件名
FAngelscriptBindDatabase::Get().Load(GetScriptRootDirectory() / TEXT("Binds.Cache"), bGeneratePrecompiledData);
...
if (!IFileManager::Get().FileExists(*Filename))
    Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript.Cache");
PrecompiledData->InitFromActiveScript();
PrecompiledData->Save(Filename);
```

##### 子维度 2：谁有权在 runtime 改写“脚本字节从哪来”

- `UnLua` 是六家里最明确把 patch seam 产品化到插件 runtime 的。先是 `CustomLoadLuaFile` / `CustomLoaders`，然后才是 file system fallback；而 file system fallback 内部又是 `PersistentDownloadDir -> ProjectDir` 两级。也就是说，**overlay 和 host hook 都在**。
- `sluaunreal` 更极端，插件根本不拥有 source root。`loadFile()` 没有 delegate 就直接返回空数组；官方 demo 只是去 `Content/Lua/` 尝试 `.lua/.luac`。它非常适合接 CDN、热更包或自定义解密，但插件自己不替你定义 policy。
- `puerts` 把灵活性放在 module resolution 上。它支持多后缀、父目录回溯、默认 `JavaScript` fallback 和 debugPath 修正，但不像 UnLua 那样有一个专门写明“下载目录优先”的 overlay contract。

[2] `UnLua` 的 overlay 是 runtime 默认链路的一部分，不是外部约定：

```cpp
// ============================================================================
// [2] 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp
// 位置: 560-638
// 说明: UL 先给宿主自定义 loader 抢占权，再把 PersistentDownloadDir 作为默认 overlay 层
// ============================================================================
if (FUnLuaDelegates::CustomLoadLuaFile.IsBound())
{
    if (FUnLuaDelegates::CustomLoadLuaFile.Execute(Env, FileName, Data, ChunkName))
    {
        if (Env.LoadString(L, Data, ChunkName))
            return 1;
        return luaL_error(L, "file loading from custom loader error");
    }
    return 0;
}

for (auto& Loader : Env.CustomLoaders)
{
    if (!Loader.Execute(Env, FileName, Data, ChunkName))
        continue;
    if (Env.LoadString(L, Data, ChunkName))
        break;
}

const auto PackagePath = UnLuaLib::GetPackagePath(L);
...
for (auto& Pattern : Patterns)
{
    const auto PathWithPersistentDir = FPaths::Combine(FPaths::ProjectPersistentDownloadDir(), Pattern);
    if (FFileHelper::LoadFileToArray(Data, *FullPath, FILEREAD_Silent))
        return LoadIt();                              // ★ 默认先查下载/补丁目录
}

for (auto& Pattern : Patterns)
{
    const auto PathWithProjectDir = FPaths::Combine(FPaths::ProjectDir(), Pattern);
    if (FFileHelper::LoadFileToArray(Data, *FullPath, FILEREAD_Silent))
        return LoadIt();                              // ★ 再回退项目目录
}
```

##### 子维度 3：patch seam 是 binary republish、宽搜索，还是 host delegate

- `UnrealCSharp` 选择的是 binary republish。优点是路径简单、交付边界清晰；代价是线上 patch 不能像 UnLua/slua 那样通过 loose script 或 delegate 快速换字节。
- `puerts` 的宽搜索很灵活，但 operator 需要自己理解 `require` 目录、父目录、`ScriptRoot`、默认 `JavaScript` 和 `.mbc/.cbc` 的组合关系。它更像“Node 风格 resolution 被移植进 Unreal”，不是显式补丁层。
- `sluaunreal` 的 host delegate 让宿主拿到最大权力，但也意味着**插件自身无法回答 active roots 是什么**。这类方案适合强宿主团队，不适合希望插件自己解释部署状态的场景。

[3] `UnrealCSharp / puerts / sluaunreal` 分别代表 republish、宽搜索与 host-delegate 三条 seam：

```cpp
// ============================================================================
// [3] 文件: Reference/UnrealCSharp/Template/Game.props
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/AssemblyLoader.cpp
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DefaultJSModuleLoader.cpp
// 文件: Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaState.h
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaState.cpp
// 文件: Reference/sluaunreal/Source/democpp/MyGameInstance.cpp
// 位置: UC props:16-27; UC loader:6-20; PU loader:67-143; PU modular.js:134-154;
//       SL LuaState.h:110,169-189; SL LuaState.cpp:131-155,651-653; SL demo:42-60
// 说明: UC=republish binary，PU=宽搜索 source/bytecode，SL=宿主完全掌握 bytes provider
// ============================================================================
<Target Name="AfterBuildPublish" AfterTargets="Build">
    <MSBuild Projects="$(ProjectPath)" Targets="Publish" Properties="Configuration=$(Configuration)" />
</Target>
<PublishFiles Include="$(PublishDir)*.dll" />
<Copy SourceFiles="@(PublishFiles)" DestinationFolder="$(ScriptOutputPath)" />
// ★ UC：补丁要重新 publish DLL

for (const auto& AssemblyPath : FUnrealCSharpFunctionLibrary::GetAssemblyPath())
{
    if (IFileManager::Get().FileExists(*File))
        FFileHelper::LoadFileToArray(Data, *File);
}
// ★ runtime 只消费 publish 后的 binary set

return SearchModuleInDir(FPaths::ProjectContentDir() / ScriptRoot, RequiredModule, Path, AbsolutePath) ||
       (ScriptRoot != TEXT("JavaScript") &&
           SearchModuleInDir(FPaths::ProjectContentDir() / TEXT("JavaScript"), RequiredModule, Path, AbsolutePath));
// ★ PU：source root 宽搜索，外加多后缀 `.js/.mjs/.cjs/.mbc/.cbc`

if(debugPath.startsWith("Pak: ")){
    debugPath = fullPath;
}
if (fullPath.endsWith(".mbc") || fullPath.endsWith(".cbc")) {
    bytecode = script;
}
// ★ PU：debug/source identity 与 bytecode 也走同一套 module loader

typedef TArray<uint8> (*LoadFileDelegate) (const char* fn, FString& filepath);
void setLoadFileDelegate(LoadFileDelegate func);

TArray<uint8> LuaState::loadFile(const char* fn, FString& filepath) {
    if(loadFileDelegate) return loadFileDelegate(fn,filepath);
    return TArray<uint8>();
}
// ★ SL：插件只消费字节，不定义来源

state->setLoadFileDelegate([](const char* fn, FString& filepath)->TArray<uint8> {
    FString path = FPaths::ProjectContentDir();
    path /= "Lua";
    TArray<FString> luaExts = { ".lua", ".luac" };
    ...
});
// ★ 官方 demo 的 seam 也明确在 host project
```

#### 对比矩阵

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| 默认 runtime 内建 loose-file overlay 优先级 | None | None | Full | Partial | None | None |
| runtime 暴露正式 custom loader / bytes-provider seam | None | None | Full | Partial | Full | None |
| shipped 主路径默认消费 publish/cache artifact，而不是 loose source | Full | Full | None | Partial | None | Full |
| 插件自己定义 build-sealed artifact contract | Full | Partial | None | None | None | Full |
| 宿主应用而不是插件拥有最终字节来源 authority | None | None | Partial | None | Full | None |

#### 小结与建议

- `当前 Angelscript` 在这一维最该保持的是 **shipping 主路径继续 sealed**。`Binds.Cache + PrecompiledScript*.Cache` 已经形成正式制品协议，不应因为参考项目有 loose overlay 就把主路径重新打散，优先级 `P0`。
- 真正值得吸收的是 `UnLua` 的“显式 overlay layer”，但只能作为**可选 operator seam**。如果 future AS 需要线上 patch，建议单独设计类似 `PersistentDownloadDir -> ProjectDir` 的 overlay 层，并明确与 precompiled/cache 路径互斥或协同，优先级 `P1`。
- `sluaunreal` 提醒 current AS：如果不想插件自己背 loader policy，可以把 byte-source authority 明确外包给宿主；但一旦走这条路，就必须同步补状态查询与诊断接口，否则 operator 无法回答“现在到底吃了哪份脚本”，优先级 `P2`。

### [D4/D7/D11] root contract 的单一真值：加载、监听与交付是否共用同一张路径表

前文分别谈过 watcher、tool loop 和 deployment path。这一轮把三者合起来问一个更基础的问题：**runtime load roots、editor watch roots、以及 staged/published roots，是否都来自同一份定义**。这直接决定“路径漂移”会不会变成隐性 bug。

#### 各插件实现概览

```
[D4/D7/D11-RootTruth] Path Table Alignment
HZ / AS(now)
├─ MakeAll/DiscoverScriptRoots()
├─ runtime AllRootPaths
└─ DirectoryWatcher registers same list

UC
├─ GetPublishDirectory()
├─ staging adds PublishDirectory
└─ AssemblyLoader reads GetAssemblyPath()

UL
├─ WatchScriptDirectory() -> ProjectContent/Script only
├─ SetupPackagingSettings() -> Script + Plugin/UnLua/Content/Script + UnLuaExtensions/*
└─ LoadFromFileSystem() -> PackagePath + PersistentDownloadDir + ProjectDir

PU
├─ C++ loader root = "JavaScript"
└─ TS analyzer watcher root = currentDirectory()

SL
└─ host setLoadFileDelegate() decides runtime root; plugin has no shared root table
```

#### 详细对比

##### 子维度 1：是否存在一张可复用的 roots API / table

- `当前 Angelscript` 与 `Hazelight` 在这条线上最整齐。`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1326-1369` 的 `DiscoverScriptRoots / MakeAllScriptRoots` 先收敛 `Project/Script + enabled plugin Script`，保证 project root first；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:372-381` 与 HZ 对应 `AngelscriptEditorModule.cpp:381-388` 再把同一批 `AllRootPaths` 注册给 `DirectoryWatcher`。current AS 进一步在 `AngelscriptDirectoryWatcherInternal.cpp:43-89` 里把 `RootPaths` 直接用于 `TryMakeRelativeScriptPath()`，所以 watcher 和 runtime queue 也共享同一张表。
- `UnrealCSharp` 的 roots contract 虽然不是 script tree，但同样是单一真值。`FUnrealCSharpFunctionLibrary.cpp:995-1049` 用 `GetPublishDirectory()` 推导 `GetFullPublishDirectory / GetFullAssemblyPublishPath / GetAssemblyPath`；`UnrealCSharpEditor.cpp:211-231` 又把同一个 `PublishDirectory` 推进 `DirectoriesToAlwaysStageAsUFS`。换句话说，**staging path 和 runtime probe path 共源**。
- `UnLua` 则明显分裂。`UUnLuaFunctionLibrary::GetScriptRootPath()` 固定只有 `ProjectContent/Script/`；`UUnLuaEditorFunctionLibrary::WatchScriptDirectory()` 也只监听这一条；但 `SetupPackagingSettings()` 还会把 `../Plugins/UnLua/Content/Script` 和 `UnLuaExtensions/*/Content/Script` 加入 staged UFS；运行时 `LoadFromFileSystem()` 又再叠加 `PackagePath` 与 `PersistentDownloadDir -> ProjectDir`。这里不是“更灵活就更好”，而是 **watch/load/stage 不再共用一张路径表**。
- `puerts` 也分裂，但方式不同。C++ editor 壳里 `PuertsEditorModule.cpp:138-150` 固定用 `DefaultJSModuleLoader("JavaScript")` 起 `JsEnv`；而 `CodeAnalyze.ts:465-515` 的 `PEDirectoryWatcher` 却直接 watch `customSystem.getCurrentDirectory()`。也就是说，**loader root 在 C++，watch root 在 TS analyzer**。
- `sluaunreal` 几乎没有插件级 root table。`LuaState` 只认 `LoadFileDelegate`；demo host 决定 `ProjectContent/Lua` 与 `.lua/.luac`。这不是 bug，而是架构刻意把路径真值留给宿主。

[1] `当前 Angelscript / Hazelight` 都把 roots 做成可复用表；current AS 还把这张表继续喂给 watcher queue：

```cpp
// ============================================================================
// [1] 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp
// 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/AngelscriptManager.cpp
// 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 位置: current Engine:1326-1369,2062-2067; current Editor:372-381; current Queue:43-89;
//       HZ Manager:269-304,921-924; HZ Editor:381-388
// 说明: HZ/current 都有 roots API；current AS 进一步把它贯穿到 watcher queue
// ============================================================================
FString RootPath = Dependencies.ConvertRelativePathToFull(Dependencies.GetProjectDir() / TEXT("Script"));
...
for (const FString& PluginScriptRoot : Dependencies.GetEnabledPluginScriptRoots())
{
    const FString ScriptPath = Dependencies.ConvertRelativePathToFull(PluginScriptRoot);
    if (Dependencies.DirectoryExists(ScriptPath) && ScriptPath != RootPath)
        DiscoveredRootPaths.Add(ScriptPath);
}
DiscoveredRootPaths.Sort();
DiscoveredRootPaths.Insert(RootPath, 0);            // ★ current AS：project root first 的 roots table

TArray<FString> AllRootPaths = FAngelscriptEngine::MakeAllScriptRoots();
for (const auto& RootPath : AllRootPaths)
    DirectoryWatcher->RegisterDirectoryChangedCallback_Handle(*RootPath, ...);
// ★ watcher 直接复用同一张表

if (!TryMakeRelativeScriptPath(AbsolutePath, RootPaths, RelativePath))
    continue;
// ★ queue 也直接消费 roots table，不另发明路径规则

// HZ：同样先 MakeAllScriptRoots()，再把 AllRootPaths 喂给 DirectoryWatcher
TArray<FString> AllRootPaths = FAngelscriptManager::MakeAllScriptRoots();
for (const auto& RootPath : AllRootPaths)
    DirectoryWatcher->RegisterDirectoryChangedCallback_Handle(*RootPath, ...);
```

##### 子维度 2：watch / load / stage 漂移风险在哪里

- `UnLua` 的漂移风险最大，因为三条路径各管一段。watcher 只看 `ProjectContent/Script/`；packaging 还会追加插件与扩展目录；runtime loader 又允许 `PackagePath` 与 `PersistentDownloadDir`。这意味着一个脚本路径**可能能被打包、能被加载，但 editor auto watch 永远看不到**。
- `puerts` 的风险更偏工具链分层。运行时 loader 明确从 `ScriptRoot` / `JavaScript` 解析模块；TS analyzer watcher 则盯当前工作目录。它适合 IDE sidecar，但“当前哪棵树才是官方 root”必须靠 C++ + TS 两边一起读。
- `sluaunreal` 因为把 authority 外包给宿主，插件内部根本不会出现“插件自己 watch A、load B、stage C”的不一致；代价是插件自己也无法回答 active root 是什么。

[2] `UnLua` 的 root truth 已经拆成 watcher-root、stage-roots、loader-pattern 三段：

```cpp
// ============================================================================
// [2] 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaFunctionLibrary.cpp
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorFunctionLibrary.cpp
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp
// 位置: UnLuaFunctionLibrary:20-23; EditorFunctionLibrary:27-35;
//       EditorModule:186-227; LuaEnv:614-638
// 说明: UL 的 watch / stage / load 不再共用同一张路径表
// ============================================================================
FString UUnLuaFunctionLibrary::GetScriptRootPath()
{
    return FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() + TEXT("Script/"));
}

const auto& ScriptRootPath = UUnLuaFunctionLibrary::GetScriptRootPath();
DirectoryWatcher->RegisterDirectoryChangedCallback_Handle(ScriptRootPath, Delegate, DirectoryWatcherHandle);
// ★ watcher 只盯 ProjectContent/Script

auto ScriptPaths = TArray<FString>{TEXT("Script"), TEXT("../Plugins/UnLua/Content/Script")};
...
if (ContentDir.Contains("UnLuaExtensions"))
    ScriptPaths.Add(ScriptPath);
PackagingSettings->DirectoriesToAlwaysStageAsUFS.Add(DirectoryPath);
// ★ stage roots 明显比 watcher root 更宽

const auto PackagePath = UnLuaLib::GetPackagePath(L);
for (auto& Pattern : Patterns)
{
    const auto PathWithPersistentDir = FPaths::Combine(FPaths::ProjectPersistentDownloadDir(), Pattern);
    ...
}
for (auto& Pattern : Patterns)
{
    const auto PathWithProjectDir = FPaths::Combine(FPaths::ProjectDir(), Pattern);
    ...
}
// ★ runtime loader 又比 watcher/stage 多了一层 PackagePath + overlay 语义
```

##### 子维度 3：如果不是 script tree，单一真值还能成立吗

- `UnrealCSharp` 证明可以。它不是 file-backed script root，但 `PublishDirectory` 同时驱动 publish 产物位置、staging 位置和 assembly probing。根表换成 binary publish table 以后，单一真值仍然成立。
- `puerts` 说明“工具进程根”和“runtime 模块根”可以不同，但必须承认这是两张表。当前快照里，C++ loader 默认 `"JavaScript"`，而 TS analyzer 自己 watch 当前目录；这更适合 IDE 服务，而不适合声称“所有路径都来自同一配置”。
- `sluaunreal` 则是另一端：没有 plugin-owned root truth，所以也无从谈对齐。它适合宿主强控，不适合插件自解释。

[3] `UnrealCSharp / puerts / sluaunreal` 分别代表“单一 publish root”“C++ root + TS root 双表”“无插件级 root truth”：

```cpp
// ============================================================================
// [3] 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp
// 文件: Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp
// 文件: Reference/puerts/unreal/Puerts/PuertsEditor/CodeAnalyze.ts
// 文件: Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEDirectoryWatcher.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaState.cpp
// 文件: Reference/sluaunreal/Source/democpp/MyGameInstance.cpp
// 位置: UC FunctionLibrary:995-1049; UC Editor:211-231; PU Editor:138-150;
//       PU CodeAnalyze.ts:465-515; PU Watcher:14-24,64-74; SL LuaState:651-653; SL demo:42-60
// 说明: UC 是 publish-root 单一真值；PU 是 C++/TS 双表；SL 没有 plugin-owned root table
// ============================================================================
FString FUnrealCSharpFunctionLibrary::GetPublishDirectory() { ... }
FString FUnrealCSharpFunctionLibrary::GetFullPublishDirectory() { ... }
TArray<FString> FUnrealCSharpFunctionLibrary::GetAssemblyPath()
{
    return { FPaths::ProjectContentDir() / GetPublishDirectory(), FMonoFunctionLibrary::GetNetDirectory() };
}
ProjectPackagingSettings->DirectoriesToAlwaysStageAsUFS.Add({PublishDirectory});
// ★ UC：publish root 同时驱动 stage 与 load

JsEnv = MakeShared<FJsEnv>(
    std::make_shared<DefaultJSModuleLoader>(TEXT("JavaScript")), ...);
JsEnv->Start("PuertsEditor/CodeAnalyze");
// ★ PU：C++ loader root 固定为 JavaScript

var dirWatcher = new UE.PEDirectoryWatcher();
dirWatcher.Watch(customSystem.getCurrentDirectory());
// ★ 但 TS analyzer watcher root 是 currentDirectory()，不是同一张 C++ 表

bool UPEDirectoryWatcher::Watch(const FString& InDirectory)
{
    Directory = FPaths::IsRelative(InDirectory) ? FPaths::ConvertRelativePathToFull(InDirectory) : InDirectory;
    DirectoryWatcher->RegisterDirectoryChangedCallback_Handle(Directory, Changed, DelegateHandle, ...);
}

void LuaState::setLoadFileDelegate(LoadFileDelegate func) { loadFileDelegate = func; }
state->setLoadFileDelegate([](const char* fn, FString& filepath)->TArray<uint8> {
    FString path = FPaths::ProjectContentDir();
    path /= "Lua";
    ...
});
// ★ SL：runtime root 由 host delegate 临时定义，插件自己没有统一根表
```

#### 对比矩阵

| 观察点 | Hazelight | UnrealCSharp | UnLua | puerts | sluaunreal | 当前 Angelscript |
| --- | --- | --- | --- | --- | --- | --- |
| load roots / publish roots 来自单一 API 或 table | Full | Full | Partial | Partial | None | Full |
| editor watcher 复用同一张根表 | Full | N/A | None | None | N/A | Full |
| staged/published delivery paths 与 runtime 读取路径共源 | Partial | Full | Partial | Partial | None | Partial |
| runtime 默认会额外引入 watcher 不知道的 overlay root | None | None | Full | Partial | N/A | None |
| 只读插件源码就能回答“当前活跃 roots 是哪些” | Full | Full | Partial | Partial | None | Full |

#### 小结与建议

- `当前 Angelscript` 在这条线上的强项，是 **`DiscoverScriptRoots / MakeAllScriptRoots` 已经成为 shared root truth**。watcher、queue 和 runtime path 都围绕这张表工作，这条设计建议按 `P0` 保持。
- `UnLua` 给出的不是“更灵活就更好”，而是一个很具体的反例：一旦 watcher-root、stage-roots、loader overlay 各写一段，路径漂移就会变成常态。current AS 若 future 增加 overlay / external roots，必须让同一张 roots table 同时驱动 watcher、dump 与 operator 查询，优先级 `P1`。
- `UnrealCSharp` 说明“单一真值”并不要求是源码树，也可以是 publish root。current AS 如果未来想把交付物显式化到独立目录，也应像 UC 一样让 load/stage/query 全部回到同一个 path API，而不是散落在多个工具入口里，优先级 `P1`。
