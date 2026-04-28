# sluaunreal 源码分析

> **分析对象**: sluaunreal (Tencent)
> **源码路径**: `Reference/sluaunreal/`
> **对比基准**: `Plugins/Angelscript/`
> **分析日期**: 2026-04-08
> **本轮覆盖维度**: D1 / D2 / D3 / D4 / D8 / D11

sluaunreal 的核心特征不是“纯反射 Lua 桥”，而是三层叠加：一层是 `Tools/config.json` 驱动的静态 wrapper 生成，一层是 `LuaCppBinding` 宏模板，一层是 `UFunction` 反射调用上的缓存加速器。它的工程重点明显偏向“线上可替换脚本字节流 + 运行时 bridge 开销压缩 + 独立 profiler 工具”。

需要先纠正一个对比前提：当前仓库里的 Angelscript 也不是“纯手写绑定”。`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:115-139, 174-215, 302-324` 会分片生成 `AS_FunctionTable_*.cpp`，并输出 direct/stub 统计摘要；运行时再通过 `Bind_BlueprintCallable.cpp:72-91` 和 `BlueprintCallableReflectiveFallback.cpp:290-371` 在 direct bind 与 reflective fallback 之间切换。因此本篇把基准视为“手写 bind + UHT 生成表 + reflective fallback + StaticJIT 元数据”的混合方案。

## 插件架构总览

```
[sluaunreal] Repository Layout
├─ Plugins/slua_unreal/                             // 真正插件主体
│  ├─ Source/slua_unreal/                           // Runtime: Lua VM bridge / overrider / wrapper
│  ├─ Source/slua_profile/                          // Editor: profiler UI + remote profile server
│  ├─ External/                                     // Lua headers / external source
│  ├─ Library/                                      // 多平台 Lua 静态库产物
│  └─ make_*.{bat,sh}                               // 构建 Lua 库脚本
├─ Tools/
│  └─ config.json                                   // wrapper 生成配置
└─ Source/democpp/                                  // 项目侧示例：loader、测试与样例绑定
```

从目录边界看，sluaunreal 把“运行时桥接”和“Profiler 编辑器工具”拆成两个模块，代码生成工具链则完全放在插件目录外的 `Tools/`。这和 Angelscript 把 `Runtime / Editor / Test` 模块及 `AngelscriptUHTTool` 一起维护的做法不同，意味着 slua 更强调“项目可接入的运行时能力”，而 Angelscript 更强调“引擎内完整开发闭环”。

## [维度 D1] 插件架构与模块划分

sluaunreal 的模块边界非常明确：`slua_unreal` 负责脚本运行时、桥接和 Lua VM 生命周期；`slua_profile` 只在 Editor 侧加载，负责面板、TCP profile server 和录制展示。`slua_unreal.Build.cs` 直接按平台链接 `Library/` 里的 Lua 静态库，说明第三方 Lua 运行时被当作“预制 runtime 资产”管理，而不是像 Angelscript 那样把第三方源码深嵌在 Runtime 模块里统一编译。

另一个关键点是工具链位置。`Tools/config.json` 把生成输出直接指向 `Plugins/slua_unreal/Source/slua_unreal/Private/`，并附带非常长的 UE include/preprocess 配置。这说明 slua 的静态导出工具不是 UHT 内嵌阶段，而是一个仓库外置、依赖宿主工程上下文的代码生成链。对比之下，Angelscript 的 `AngelscriptUHTTool` 更接近“绑定生成属于编译系统本身”的思路。

```
[D1] Module Topology
sluaunreal
├─ slua_unreal (Runtime)                            // LuaState / LuaObject / LuaOverrider / LuaWrapper
├─ slua_profile (Editor)                            // Profiler tab / TCP server / stat viewer
├─ External + Library                               // 第三方 Lua 头文件与静态库
└─ Tools/config.json                                // 外部 wrapper 生成器配置

Angelscript
├─ AngelscriptRuntime                               // 绑定、编译、StaticJIT、debug server
├─ AngelscriptEditor                                // 目录监听、Blueprint 修复、编辑器整合
├─ AngelscriptTest                                  // 自动化测试与学习样例
└─ AngelscriptUHTTool                               // UHT 导出与 function table 生成
```

[1] 关键源码：sluaunreal 的模块声明与第三方库接入

```json
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/slua_unreal.uplugin
// 函数: plugin descriptor
// 位置: 16-26，声明 runtime 与 editor profiler 两个模块
// ============================================================================
"Modules": [
  {
    "Name": "slua_unreal",
    "Type": "Runtime",
    "LoadingPhase": "PreLoadingScreen"
  }, // ★ runtime 模块很早加载，保证 Lua 状态与桥接尽早可用
  {
    "Name": "slua_profile",
    "Type": "Editor",
    "LoadingPhase": "PreDefault"
  } // ★ profiler 明确放在 Editor 侧，而不是 runtime 常驻
]
```

```csharp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/slua_unreal.Build.cs
// 函数: slua_unreal constructor
// 位置: 31-76, 92-112，接入 External/Library 并开启 profiler define
// ============================================================================
var externalSource = Path.Combine(PluginDirectory, "External");
var externalLib = Path.Combine(PluginDirectory, "Library");

PublicIncludePaths.AddRange(new string[] {
    externalSource,
    Path.Combine(externalSource, "lua"),
});

else if (Target.Platform == UnrealTargetPlatform.Win64)
{
    PublicAdditionalLibraries.Add(Path.Combine(externalLib, "Win64/lua.lib"));
} // ★ 按平台直接链接预编译 Lua 库，而不是在 UBT 内编 Lua 源码

PrivateDependencyModuleNames.AddRange(new string[]
{
    "CoreUObject",
    "Engine",
    "Slate",
    "SlateCore",
    "UMG",
    "InputCore",
    "NetCore",
});

PublicDefinitions.Add("ENABLE_PROFILER");
PublicDefinitions.Add("NS_SLUA=slua"); // ★ runtime 与 profiler 开关在模块边界上统一打开
```

```json
// ============================================================================
// 文件: Reference/sluaunreal/Tools/config.json
// 函数: codegen config
// 位置: 65-77，外部工具生成物回写到 runtime 私有目录
// ============================================================================
"output_dir": "{solution_dir}/Plugins/slua_unreal/Source/slua_unreal/Private/",
"win": {
  "solution_dir": "../",
  "ue4_dir": "C:/Program Files/Epic Games/UE_5.2",
  "ue_vcproj": "{solution_dir}/Intermediate/ProjectFiles/UE5.vcxproj",
  "include_path": "/* 省略超长 include 列表 */",
  "preprocess": "/* 省略超长宏配置 */"
} // ★ 生成器需要完整 UE include/preprocess 上下文，说明它是插件外工具链
```

### 设计取舍

- 把 profiler UI 拆成独立 `slua_profile` 模块，使 runtime 可在非编辑器场景保持更小表面积；代价是 profiler 功能天然依赖 Editor。
- Lua 第三方库以预编译产物形式接入，降低了接入成本和跨平台构建复杂度；代价是库升级、ABI 和平台覆盖要靠额外脚本维护。
- 外部代码生成工具放在 `Tools/`，让 wrapper 生成不依赖 UE 编译阶段；代价是工具链与宿主工程路径强耦合，配置漂移风险高。

### 与 Angelscript 对比

| 观察点 | sluaunreal | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 运行时/编辑器拆分 | `slua_unreal` + `slua_profile`（`slua_unreal.uplugin:16-26`） | `AngelscriptRuntime` + `AngelscriptEditor`（`Plugins/Angelscript/Angelscript.uplugin:18-28`） | 实现方式不同 |
| 测试模块 | 插件源码内未见独立测试模块，主要是 `Source/democpp/` 示例 | `AngelscriptTest` 是独立模块（`Plugins/Angelscript/Angelscript.uplugin:29-33`, `AngelscriptTest.Build.cs:23-50`） | 没有实现 |
| 第三方脚本运行时集成 | 预编译 Lua 静态库（`slua_unreal.Build.cs:42-76`） | ThirdParty 源码随 Runtime 编译（`AngelscriptRuntime.Build.cs:20-22`） | 实现方式不同 |
| 代码生成工具位置 | 仓库外置 `Tools/config.json:65-77` | UHT Tool 内置生成 `AS_FunctionTable_*.cpp`（`AngelscriptFunctionTableCodeGenerator.cs:115-139, 302-324`） | 实现方式不同 |
| 编辑器依赖面 | profiler 模块只依赖 `UnrealEd / EditorStyle / LevelEditor / Networking / Sockets`（`slua_profile.Build.cs:50-67`） | Editor 模块覆盖 `BlueprintGraph / Kismet / DirectoryWatcher / AssetTools / ContentBrowser`（`AngelscriptEditor.Build.cs:12-40`） | 实现质量差异：Angelscript 的编辑器闭环更完整 |

## [维度 D2] 反射绑定机制

sluaunreal 的反射绑定不是单一路径。`LuaCppBinding.h:404-511` 明确把“非 `UObject` 类型”和“`UObject` 扩展”分流：前者用模板宏 `DefLuaMethod` / `DefLuaProperty` 静态展开，后者禁止直接走 `LuaCppBinding`，必须用 `REG_EXTENSION_METHOD` / `REG_EXTENSION_PROPERTY` 注册到 `LuaObject` 的 extension map。再往下，`LuaWrapper.cpp:55-67,184-188` 根据 UE 版本包含 `LuaWrapper*.inc`，把大量 engine wrapper 静态编译进 runtime。

但 slua 又没有把 `UObject` 调用完全静态化。`LuaObject::push(UFunction*, UClass*)` 仍然会把 `UFunction` 包成闭包，然后交给 `LuaFunctionAccelerator`。这个加速器在构造时预分析参数布局、引用/out param、latent 参数和返回值，并把结果缓存到 `cache`，调用时再复用。也就是说，slua 的实际策略是“静态 wrapper 负责覆盖面与基础类型，反射路径负责通用 UObject/UFunction 调用，但用缓存把裸反射的重复成本摊薄”。

```
[D2] Binding Flow
[sluaunreal]
├─ [1] Tools/config.json -> LuaWrapper*.inc         // 外部工具生成 engine wrapper
├─ [2] LuaWrapper::initExt()                        // 载入生成 wrapper
├─ [3] DefLuaMethod / DefLuaProperty                // 非 UObject 模板静态绑定
├─ [4] REG_EXTENSION_METHOD                         // UObject 走 extension map
├─ [5] FindFunctionByName + push(UFunction*)        // 运行时发现 UFunction
└─ [6] LuaFunctionAccelerator::findOrAdd()          // 缓存参数布局与调用信息

[Angelscript]
├─ [1] Bind_*.cpp 手写入口                          // 显式绑定类型与规则
├─ [2] UHTTool 生成 AS_FunctionTable_*.cpp          // 批量生成 BlueprintCallable 表
├─ [3] Direct native pointer if available           // 直连 native entry
└─ [4] Reflective fallback if not available         // 退回通用 ProcessEvent 路径
```

[1] 关键源码：slua 明确区分 `UObject` 与非 `UObject` 的绑定通道

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaCppBinding.h
// 函数: binding macros
// 位置: 404-511，模板静态绑定与 UObject extension 注册的分流点
// ============================================================================
static_assert(!std::is_base_of<UObject, CLS>::value,
    "UObject class shouldn't use LuaCppBinding. Use REG_EXTENSION instead.");
// ★ 这里直接写死：UObject 不走普通模板绑定，而是走 extension 注册

#define DefLuaMethod(NAME,M) { \
    lua_CFunction x=LuaCppBinding<decltype(M),M>::LuaCFunction; \
    constexpr bool inst=std::is_member_function_pointer<decltype(M)>::value; \
    LuaObject::addMethod(L, #NAME, x, inst); \
} // ★ 非 UObject/普通 C++ 类型：在编译期展开 LuaCFunction

#define REG_EXTENSION_METHOD(U,N,M) { \
    using BindType = LuaCppBinding<decltype(M),M>; \
    LuaObject::addExtensionMethod(U::StaticClass(),N,BindType::LuaCFunction, BindType::IsStatic); \
} // ★ UObject：注册到运行时 extension map，等待按 UClass 分派

#define REG_EXTENSION_PROPERTY(U,N,GETTER,SETTER) { \
    using GetType = LuaCppBinding<decltype(GETTER),GETTER>; \
    using SetType = LuaCppBinding<decltype(SETTER),SETTER>; \
    LuaObject::addExtensionProperty(U::StaticClass(),N,GetType::LuaCFunction,SetType::LuaCFunction,GetType::IsStatic); \
}
```

[2] 关键源码：slua 的反射路径并非裸 `ProcessEvent`，而是先做缓存预分析

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaFunctionAccelerator.cpp
// 函数: LuaFunctionAccelerator::LuaFunctionAccelerator / findOrAdd / call
// 位置: 33-156, 181-260，缓存参数布局、out 参数和 latent 信息
// ============================================================================
LuaFunctionAccelerator::LuaFunctionAccelerator(UFunction* inFunc)
    : func(inFunc)
    , bLuaOverride(ULuaOverrider::isUFunctionHooked(inFunc))
{
    for (TFieldIterator<FProperty> it(func); it && (it->PropertyFlags & CPF_Parm); ++it)
    {
        FProperty* prop = *it;
        if (prop->HasAnyPropertyFlags(CPF_OutParm))
        {
            outParmRecProps.Add(prop); // ★ 预收集 out 参数，避免每次调用重新扫反射元数据
        }

        if (isLatentProperty(prop))
        {
            checkerRef->bLatent = true; // ★ latent 参数在构造阶段就标出来
        }
        else
        {
            checkerRef->checker = LuaObject::getChecker(prop); // ★ 参数校验器也做缓存
        }
    }
}

LuaFunctionAccelerator* LuaFunctionAccelerator::findOrAdd(UFunction* inFunc)
{
    auto ret = cache.Find(inFunc);
    if (ret)
    {
        return *ret; // ★ 热路径优先复用缓存，不重复解析 UFunction
    }
    auto value = new LuaFunctionAccelerator(inFunc);
    cache.Emplace(inFunc, value);
    return value;
}

int LuaFunctionAccelerator::call(lua_State* L, int offset, UObject* obj, bool& isLatentFunction, NewObjectRecorder* objRecorder)
{
    uint8* params = (uint8*)FMemory_Alloca(func->PropertiesSize);
    FProperty** propertyList = (FProperty**)FMemory_Alloca(func->NumParms * sizeof(void*));
    // ★ 调用阶段直接使用已缓存的 checker/outParm 描述，减少每次反射遍历成本
    for (auto& checkerInfo : paramsChecker)
    {
        /* 省略参数校验与写入细节 */
    }
}
```

[3] 对照源码：当前 Angelscript 并非纯手写绑定，而是有 UHT function table 生成链

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs
// 函数: BuildModule / WriteGenerationSummary / BuildShard
// 位置: 115-139, 174-215, 302-324
// ============================================================================
int shardCount = (entries.Count + MaxEntriesPerShard - 1) / MaxEntriesPerShard;
string outputPath = factory.MakePath($"AS_FunctionTable_{module.ShortName}_{shardIndex:D3}", ".cpp");
factory.CommitOutput(outputPath, BuildShard(/* 省略其余参数 */));
// ★ UHT 导出阶段分片生成 AS_FunctionTable_*.cpp，而不是全部靠人工写 bind 文件

string summaryPath = factory.MakePath("AS_FunctionTable_Summary", ".json");
File.WriteAllText(summaryPath, summaryJson, Encoding.UTF8);
// ★ 还会输出 direct/stub 覆盖率摘要，方便追踪绑定质量

builder.Append("AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_AS_FunctionTable_")
    .Append(moduleShortName)
    .Append('_')
    .Append(shardIndex.ToString("D3"));
// ★ 生成物最终仍然落回 FAngelscriptBinds 注册体系
```

### 设计取舍

- slua 的优势是把“常见 wrapper”和“通用反射”叠加，而不是强行选一边；这样 engine 大量基础类型可以静态化，`UObject` 仍保留动态覆盖面。
- 代价是绑定体系分层较多：外部 generator、宏模板、extension map、`LuaFunctionAccelerator`、`LuaWrapper*.inc` 同时存在，新人维护成本不低。
- `LuaFunctionAccelerator` 说明 slua 团队清楚裸反射成本不可接受，所以把优化点放在“桥接层参数拆装和反射元数据遍历”。

### 与 Angelscript 对比

| 观察点 | sluaunreal | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 类型绑定入口 | `LuaCppBinding` + `LuaWrapper*.inc` + extension map（`LuaCppBinding.h:404-511`, `LuaWrapper.cpp:55-67,184-188`） | 手写 `Bind_*.cpp` + UHT `AS_FunctionTable_*.cpp`（`AngelscriptFunctionTableCodeGenerator.cs:115-139,302-324`） | 实现方式不同 |
| `UObject` 调用路径 | `FindFunctionByName` 后交给 `LuaFunctionAccelerator`（`LuaObject.cpp:793-801,3062-3071`; `LuaFunctionAccelerator.cpp:33-156,181-260`） | direct native pointer 优先，缺失时 reflective fallback（`Bind_BlueprintCallable.cpp:72-91`; `BlueprintCallableReflectiveFallback.cpp:290-371`） | 实现方式不同 |
| 反射 fallback 优化 | 用缓存提前解析 `FProperty`/out param/latent 信息 | 也保留 fallback，但 direct bind 与 StaticJIT native form 更重（`Bind_BlueprintCallable.cpp:95-151`; `StaticJITBinds.cpp:878-1026`） | 实现方式不同 |
| 代码生成可观测性 | 当前可见的是 `Tools/config.json` 输出路径与编译上下文（`Tools/config.json:65-77`），未见统计摘要 | UHT tool 会输出 direct/stub JSON/CSV 摘要（`AngelscriptFunctionTableCodeGenerator.cs:166-215`） | 实现质量差异：Angelscript 的生成反馈更可审计 |

## [维度 D3] Blueprint 交互

sluaunreal 的 Blueprint 交互主轴是“运行时 hook UObject/UFunction”。`LuaOverrider::bindOverrideFuncs` 会根据对象或类推导 Lua 模块路径，`requireModule()` 后扫描 Lua table 中的函数名，把匹配到的 `UFunction` 通过 `hookBpScript()` 改写成 Lua override 入口；同时还会复制一份 `Super_` 版本的 `UFunction`，让 Lua 侧保留 super call。这个方案的重点是“不改变 Blueprint 资产的作者模型”，而是在对象实例/类层面把调用重定向到 Lua。

slua 还额外提供 `ULuaBlueprintLibrary`，允许 Blueprint 节点直接 `CallToLua*`，以及用 `FLuaBPVar` 在 BP 与 Lua 之间搬运参数。换句话说，slua 的 Blueprint 互通并不是把 Blueprint API 大规模编译进 Lua 类型系统，而是提供“hook + function library 两套桥”。

```
[D3] Blueprint Interop
[sluaunreal]
Blueprint Event / UObject
├─ [1] bindOverrideFuncs()                          // 解析 Lua 模块并创建 self table
├─ [2] hookBpScript()                               // duplicate UFunction + 替换 native/script
├─ [3] setmetatable(self, Object, Super)            // 注入 Lua 侧继承链
└─ [4] ULuaBlueprintLibrary::CallToLua*             // Blueprint 节点主动调用 Lua

[Angelscript]
BlueprintType scan
├─ [1] ShouldBindEngineType()                       // 识别 BlueprintType/BlueprintCallable 类
├─ [2] BindBlueprintEvent()                         // 事件注册成脚本方法
├─ [3] BindBlueprintCallable()                      // 函数注册成脚本方法
└─ [4] BlueprintGetter/Setter remap                 // 自动补出访问器名字
```

[1] 关键源码：slua 用 `duplicateUFunction + hook` 改写 Blueprint 事件入口

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaOverrider.cpp
// 函数: LuaOverrider::bindOverrideFuncs / hookBpScript
// 位置: 1174-1310, 1381-1450
// ============================================================================
NS_SLUA::LuaVar luaModule = sluaState->requireModule(TCHAR_TO_UTF8(*luaFilePath));
if (!luaModule.isValid())
{
    return false; // ★ 先按对象/类路径装入 Lua 模块
}

TSet<FName> funcNames;
getLuaFunctions(L, funcNames, luaModule);
for (auto& funcName : funcNames)
{
    UFunction* func = cls->FindFunctionByName(funcName, EIncludeSuperFlag::IncludeSuper);
    if (func && (func->FunctionFlags & OverrideFuncFlags))
    {
        if (hookBpScript(func, cls, (FNativeFuncPtr)&ULuaOverrider::luaOverrideFunc))
        {
            hookCounter++;
        }
    }
} // ★ Lua table 里的函数名直接驱动 UFunction hook

auto supercallFunc = duplicateUFunction(func, cls,
    FName(*(SUPER_CALL_FUNC_NAME_PREFIX + func->GetName())), func->GetNativeFunc());
// ★ 先复制一份 super call 版本，保住原始 Blueprint/C++ 入口

if (overrideFunc == func)
{
    overrideFunc->SetNativeFunc(hookFunc);
    overrideFunc->Script.Insert(Code, CodeSize, 0);
    hooked = true;
}
else if (!overrideFunc)
{
    overrideFunc = duplicateUFunction(func, cls, func->GetFName(),
        (FNativeFuncPtr)&ULuaOverrider::luaOverrideFunc);
    /* 省略 Script/Net 相关补丁分支 */
} // ★ 没有 overrideFunc 就复制出新的 UFunction；有的话直接改 native/script
```

[2] 关键源码：Blueprint 节点主动调 Lua 的桥接库

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaBlueprintLibrary.h
// 函数: ULuaBlueprintLibrary
// 位置: 34-77，暴露 BlueprintCallable 节点
// ============================================================================
UCLASS()
class SLUA_UNREAL_API ULuaBlueprintLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_UCLASS_BODY()

    UFUNCTION(BlueprintCallable, meta=(DisplayName="Call To Lua With Arguments",
        WorldContext = "WorldContextObject"), Category="slua")
    static FLuaBPVar CallToLuaWithArgs(UObject* WorldContextObject,
        FString FunctionName, const TArray<FLuaBPVar>& Args, FString StateName);

    UFUNCTION(BlueprintCallable, meta=(DisplayName="Call To Lua",
        WorldContext = "WorldContextObject"), Category="slua")
    static FLuaBPVar CallToLua(UObject* WorldContextObject,
        FString FunctionName, FString StateName);
    // ★ Blueprint 不需要继承 LuaActor 也能直接通过节点调用 Lua 全局函数
};
```

### 设计取舍

- slua 的好处是继承模型对 Blueprint 作者透明。只要类或对象能定位到 Lua 模块，就可以在不重建脚本类型系统的前提下 hook 现有 `UFunction`。
- 代价是 override 行为高度依赖运行时状态：模块路径、对象 self table、duplicate 出来的 `UFunction`、metatable 约定都要保持一致。
- `ULuaBlueprintLibrary` 是一种低耦合补丁式桥接，但也意味着 BP 与 Lua 的互通语义被分散在“事件 hook”和“节点调用”两套路径里。

### 与 Angelscript 对比

| 观察点 | sluaunreal | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| Blueprint 事件覆写 | 运行时 `duplicateUFunction + hook`（`LuaOverrider.cpp:1275-1283, 1398-1449`） | `BindBlueprintEvent()` 把事件注册成脚本方法，并保存 `UFunction` 签名（`Bind_BlueprintEvent.cpp:553-640`） | 实现方式不同 |
| Blueprint 可调用函数暴露 | 以 `ULuaBlueprintLibrary::CallToLua*` 额外提供 BP -> Lua 节点（`LuaBlueprintLibrary.h:41-77`, `LuaBlueprintLibrary.cpp:51-97`） | `Bind_BlueprintType.cpp:1374-1409` 自动扫描 `BlueprintCallable/Pure/ScriptCallable` 挂进类型系统 | 实现方式不同 |
| Blueprint 类型接入 | slua 重点是对象 hook 和 wrapper，不是统一脚本类型系统 | `ShouldBindEngineType()` 对 `BlueprintType` 与含 BP callable 的类自动建脚本类型（`Bind_BlueprintType.cpp:962-1048`） | 实现方式不同 |
| Getter/Setter 元数据映射 | 本次重点路径未见针对 `BlueprintGetter/BlueprintSetter` 的专门 remap 分支 | `Bind_BlueprintType.cpp:1413-1465` 显式为属性补 `GetX/SetX` 方法名 | 实现质量差异：Angelscript 的 Blueprint 访问器整合更系统 |

## [维度 D4] 热重载 / 热更新

sluaunreal 在仓库内能直接证明的不是“完整线上热更新系统”，而是“可替换的脚本字节加载管线”。`LuaState` 定义了 `LoadFileDelegate`，`loader()` 和 `doFile()` 只负责把 delegate 返回的字节交给 `luaL_loadbuffer`；demo 工程 `UMyGameInstance::CreateLuaState()` 则把 delegate 指到 `Content/Lua`，按顺序尝试 `.lua` 和 `.luac`。所以更准确的描述应该是：slua runtime 提供一个项目可注入的 script bytes provider，项目侧可以在这个 provider 后面接本地文件、patch 包、网络下载或自定义解密逻辑，但这些“线上工作流”本身不在插件 runtime 内实现。

Angelscript 的 hot reload 则是另一类问题。`AngelscriptDirectoryWatcherInternal.cpp:43-89` 和 `AngelscriptEngine.cpp:1615-1700, 2729-2905` 明确实现了变更检测、后台线程/目录监听、文件队列，以及 `SoftReload / FullReloadSuggested / FullReloadRequired` 的决策链。它解决的是“开发期脚本与反射类型热替换”，不是“运行时热更包分发”。

```
[D4] Reload Path Difference
[sluaunreal]
GameInstance
├─ setLoadFileDelegate()                            // 项目注入 bytes provider
├─ loadFile()                                       // runtime 只拿字节
├─ doFile()/requireModule()                         // 交给 Lua require / loadbuffer
└─ object hook / module table                       // 状态保持由 Lua 模块和对象表承担

[Angelscript]
DirectoryWatcher / checker thread
├─ queue changed .as files                          // 收集变更文件
├─ CheckForHotReload()                              // 组装 reload 批次
├─ compile + class generation                       // 重新编译并做 class reload 决策
└─ keep old code on failure                         // 失败时保留旧代码
```

[1] 关键源码：slua 的热更新基础其实是“可插拔 loader”

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaState.h
// 函数: LuaState API
// 位置: 110, 167-189，定义加载委托与 doFile/requireModule
// ============================================================================
typedef TArray<uint8> (*LoadFileDelegate) (const char* fn, FString& filepath);

LuaVar doFile(const char* fn, LuaVar* pEnv = nullptr);
LuaVar requireModule(const char* fn, LuaVar* pEnv = nullptr);
void setLoadFileDelegate(LoadFileDelegate func);
// ★ runtime 只要求“给我模块字节和文件路径”，并不限定来源必须是磁盘
```

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaState.cpp
// 函数: LuaState::loader / loadFile / doFile
// 位置: 131-156, 651-780
// ============================================================================
TArray<uint8> buf = state->loadFile(fn, filepath);
if (buf.Num() > 0)
{
    if (luaL_loadbuffer(L, (const char*)buf.GetData(), buf.Num(), chunk) == 0)
    {
        return 1;
    }
}

TArray<uint8> LuaState::loadFile(const char* fn, FString& filepath)
{
    if (loadFileDelegate) return loadFileDelegate(fn, filepath);
    return TArray<uint8>();
} // ★ runtime 完全把字节来源委托给项目侧

void LuaState::setLoadFileDelegate(LoadFileDelegate func)
{
    loadFileDelegate = func;
}
```

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Source/democpp/MyGameInstance.cpp
// 函数: UMyGameInstance::CreateLuaState
// 位置: 42-63，demo 仅演示本地文件系统加载 .lua/.luac
// ============================================================================
state->setLoadFileDelegate([](const char* fn, FString& filepath)->TArray<uint8> {
    FString path = FPaths::ProjectContentDir();
    path /= "Lua";
    path /= filename.Replace(TEXT("."), TEXT("/"));

    TArray<FString> luaExts = { TEXT(".lua"), TEXT(".luac") };
    for (auto& it : luaExts)
    {
        auto fullPath = path + *it;
        FFileHelper::LoadFileToArray(Content, *fullPath);
        if (Content.Num() > 0)
        {
            filepath = fullPath;
            return MoveTemp(Content);
        }
    }
    return MoveTemp(Content);
}); // ★ 仓库内示例只证明“本地源码/字节码切换”，不证明内置网络分发
```

[2] 对照源码：Angelscript 把重点放在开发期文件监听与失败回退

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: CheckForHotReload / PerformHotReload 内部决策
// 位置: 2729-2779, 3878-4005
// ============================================================================
FileList.Append(FileChangesDetectedForReload);
/* 省略删除文件与 queued full reload 的组装逻辑 */
if (FileList.Num() != 0)
{
    PerformHotReload(CompileType, FileList); // ★ 先消费脚本变更队列，再进入 reload 流程
}

if (bHadCompileErrors)
{
    UE_LOG(Angelscript, Error,
        TEXT("Hot reload failed due to script compile errors. Keeping all old script code."));
    bShouldSwapInModules = false;
} // ★ 编译失败时明确保留旧脚本代码

switch (ReloadReq)
{
    case FAngelscriptClassGenerator::EReloadRequirement::SoftReload:
        SwapInModules(CompiledModules, DiscardedModules);
        ClassGenerator.PerformSoftReload();
        break;
    case FAngelscriptClassGenerator::EReloadRequirement::FullReloadRequired:
        /* 省略必须全量重载时的告警与错误标记 */
        bShouldSwapInModules = false;
        bFullReloadRequired = true;
        break;
} // ★ reload 粒度是软重载/建议全量/必须全量三档
```

### 设计取舍

- slua 的 runtime loader 抽象非常适合线上补丁思维，因为脚本源被抽象成“字节流提供者”；但状态保持、失败回滚、版本校验都被留给项目层。
- Angelscript 的热重载设计明显是开发工具链：文件监听、类生成、Blueprint 修复、失败后保留旧代码，都围绕编辑期迭代效率。
- 两者不是同一赛道。前者更像 runtime hot update capability，后者更像 dev-time hot reload pipeline。

### 与 Angelscript 对比

| 观察点 | sluaunreal | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 变更检测 | 在 inspected runtime 内未见目录监听或线程扫描；只有 `LoadFileDelegate`（`LuaState.h:110,167-189`） | Editor watcher + checker thread（`AngelscriptDirectoryWatcherInternal.cpp:43-89`, `AngelscriptEngine.cpp:1615-1700, 2729-2905`） | 没有实现 |
| 重载粒度 | 由项目 loader 和 Lua `require`/对象表控制，插件本身不定义软/全量档位 | `SoftReload / FullReloadSuggested / FullReloadRequired`（`AngelscriptEngine.cpp:3938-3999`） | 实现方式不同 |
| 失败恢复 | demo 路径里没有统一回滚框架；加载失败只是返回空 `LuaVar`（`LuaState.cpp:768-779`） | 编译/类生成失败时保留旧代码（`AngelscriptEngine.cpp:3881-3885, 3972-4005`） | 没有实现 |
| 场景定位 | 运行时热更友好基础设施 | 开发期热重载与类修复链路 | 实现方式不同 |

## [维度 D8] 性能与优化

sluaunreal 的性能优化点主要落在 bridge 层，而不是 Lua VM 本体。前面 D2 已经证明 `LuaFunctionAccelerator` 会缓存 `UFunction` 的参数描述、out 参数链和 latent 参数信息，这能直接减少“Lua -> UObject/UFunction”这条桥的反射开销。与此同时，slua 把 profiler 做成了完整子系统：runtime 模块启动时创建 `SluaProfilerDataManager`，Editor 模块启动 profile tab 和 `FProfileServer`，Lua 侧脚本 `LuaProfiler.inl` 还能通过 `socket.core` 连到远端端口 `8081` 做远程采集，或本地录制。

Angelscript 的优化焦点更靠近“脚本执行体”。`AngelscriptEngine.cpp:1425-1589` 里 `bGeneratePrecompiledData`、`bUsePrecompiledData`、`StaticJIT` 和 `PrecompiledScript*.Cache` 一起出现；`StaticJITBinds.cpp:878-1026` 又会在生成 precompiled data 时，把 `PushArg/DelegateExecute/EventFunctionExecute` 等调用形态登记成 native form，供 StaticJIT 生成更直接的调用代码。也就是说，Angelscript 想优化的是“AS VM 执行 + 绑定调用一体化”的整条链，而 slua 更像是“Lua VM 保持不变，但把桥和观测工具做到足够轻”。

```
[D8] Optimization Focus
sluaunreal
├─ LuaFunctionAccelerator                          // 缓存 UFunction 参数布局
├─ generated wrappers                              // 常用 engine 类型静态导出
├─ SluaProfilerDataManager                         // runtime 采集管理
└─ slua_profile + LuaProfiler.inl                  // UI / TCP server / remote attach

Angelscript
├─ direct bind / reflective fallback               // 运行时调用策略双轨
├─ PrecompiledScript*.Cache                        // 预编译数据
├─ StaticJIT native forms                          // 生成 native form / output code
└─ build-config / GUID validation                  // 产物一致性校验
```

[1] 关键源码：slua 的 profiler 是一等公民，而不是调试附属品

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/slua_unreal.cpp
// 函数: Fslua_unrealModule::StartupModule / ShutdownModule
// 位置: 20-37，runtime 模块启动即开启 profiler manager
// ============================================================================
void Fslua_unrealModule::StartupModule()
{
    SluaProfilerDataManager::StartManager(); // ★ profiler manager 跟随 runtime 生命周期启动
}

void Fslua_unrealModule::ShutdownModule()
{
    SluaProfilerDataManager::StopManager();
}
```

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_profile/Private/slua_remote_profile.cpp
// 函数: FProfileServer::Init / Run
// 位置: 22-30, 52-60, 67-110
// ============================================================================
FAutoConsoleVariableRef CVarSluaProfilerPort(
    TEXT("slua.ProfilerPort"),
    NS_SLUA::FProfileServer::Port,
    TEXT("Slua profiler server port.\n"),
    ECVF_Default);

int32 FProfileServer::Port = 8081;

bool FProfileServer::Init()
{
    ListenEndpoint.Address = FIPv4Address(0, 0, 0, 0);
    ListenEndpoint.Port = (std::numeric_limits<uint16>::min() < Port)
        && (Port < std::numeric_limits<uint16>::max()) ? Port : 8081;
    Listener = new FTcpListener(ListenEndpoint);
    Listener->OnConnectionAccepted().BindRaw(this, &FProfileServer::HandleConnectionAccepted);
    return true;
} // ★ profiler server 是正式 TCP listener，而不是仅本地 UI

while (conn->ReceiveData(Message))
{
    (void)OnProfileMessageDelegate.ExecuteIfBound(Message);
} // ★ 采样数据通过网络消息流进入编辑器侧 UI
```

```lua
-- ============================================================================
-- 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaProfiler.inl
-- 函数: this.start / startLocalRecord / stopLocalRecord
-- 位置: 39-63, 85-88, 172-183
-- ============================================================================
function this.start(host, port)
    host = tostring(host or "127.0.0.1")
    port = tonumber(port) or 8081
    this.printToConsole("Profile start. connect host:" .. host .. " port:" .. tostring(port), 1)
    this.setSocket(nil)
    local sockSuccess = sock and sock:connect(connectHost, connectPort)
end -- ★ Lua 侧可以主动连远端 profiler server

if pcall(function() sock = require("socket.core").tcp() end) then
    sock:settimeout(ConnectTimeoutSec)
end -- ★ 远程 profiler 依赖 Lua socket，而不是 UE 专有协议

function this.startLocalRecord()
    this.onChangeRecordState(true)
    this.changeHookState(HookState.HOOK)
end
```

[2] 对照源码：Angelscript 的性能优化重心在 precompiled data 与 StaticJIT

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: FAngelscriptEngine::Initialize
// 位置: 1425-1589，决定是否生成/加载 PrecompiledScript 与 StaticJIT
// ============================================================================
bGeneratePrecompiledData = RuntimeConfig.bGeneratePrecompiledData;
bUsePrecompiledData = !bGeneratePrecompiledData && !RuntimeConfig.bIgnorePrecompiledData
    && !RuntimeConfig.bRunningCommandlet && !WITH_EDITOR && !bScriptDevelopmentMode;

if (bGeneratePrecompiledData)
{
    PrecompiledData = new FAngelscriptPrecompiledData(Engine);
    StaticJIT = new FAngelscriptStaticJIT();
    StaticJIT->PrecompiledData = PrecompiledData;
    Engine->SetJITCompiler(StaticJIT);
} // ★ 生成阶段把预编译数据和 StaticJIT 绑在一起

if (bUsePrecompiledData)
{
    Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript_Development.Cache");
    /* 省略按 BuildConfig 选择 cache 文件名的分支 */
    PrecompiledData->Load(Filename);
    if (!PrecompiledData->IsValidForCurrentBuild())
    {
        /* 省略丢弃无效 cache 的清理代码 */
    }
} // ★ 运行阶段优先消费 build-specific cache，而不是重新解释全部脚本
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.cpp
// 函数: FScriptFunctionNativeForm::BindPushArg / BindPushArgRef / BindEventFunctionExecute
// 位置: 878-1026
// ============================================================================
void FScriptFunctionNativeForm::BindPushArg()
{
    if (!FAngelscriptEngine::bGeneratePrecompiledData)
        return;
    GScriptNativeForms.Add(FAngelscriptBinds::GetPreviousBind(), new FScriptNativePushArg());
} // ★ 只有在生成 precompiled data 时才登记 native form

void FScriptFunctionNativeForm::BindPushArgRef()
{
    if (!FAngelscriptEngine::bGeneratePrecompiledData)
        return;
    GScriptNativeForms.Add(FAngelscriptBinds::GetPreviousBind(), new FScriptNativePushArgRef());
}

void FScriptFunctionNativeForm::BindEventFunctionExecute()
{
    if (!FAngelscriptEngine::bGeneratePrecompiledData)
        return;
    GScriptNativeForms.Add(FAngelscriptBinds::GetPreviousBind(), new FScriptNativeEventFunctionExecute());
} // ★ StaticJIT 优化的是脚本执行时的 native 调用形态
```

### 设计取舍

- slua 把优化重点放在桥接层和工具观测层，因此对已有 Lua VM 生态侵入较小，也更适合线上定位性能问题。
- Angelscript 把优化重点放在 precompiled data + StaticJIT，本质上是在优化脚本系统整体执行栈，而不是单一 bridge。
- 两者优化层级不同，现有源码证据不足以得出“谁更快”的绝对结论；只能说 slua 强在 bridge 与 profiler，Angelscript 强在编译产物与执行期特化。

### 与 Angelscript 对比

| 观察点 | sluaunreal | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 反射桥接优化 | `LuaFunctionAccelerator` 缓存参数布局（`LuaFunctionAccelerator.cpp:33-156,181-260`） | direct bind + reflective fallback + native form（`Bind_BlueprintCallable.cpp:72-151`, `StaticJITBinds.cpp:878-1026`） | 实现方式不同 |
| profiler 工具链 | runtime manager + Editor UI + TCP server + Lua attach 脚本（`slua_unreal.cpp:20-37`, `slua_remote_profile.cpp:22-110`, `LuaProfiler.inl:39-63,85-88,172-183`） | 当前仓库未见与 `slua_profile` 对等的独立 profiler UI 模块；重点在 debug server / coverage / StaticJIT | 没有实现 |
| VM 级预编译/JIT | inspected slua runtime 未见 Lua VM native transpile/JIT 链路 | `PrecompiledScript*.Cache` + `StaticJIT`（`AngelscriptEngine.cpp:1425-1589`） | 没有实现 |

## [维度 D11] 部署与打包

sluaunreal 在部署层的实际代码证据集中在两点：一是脚本来源可注入，二是 demo 明确接受 `.lua` 与 `.luac`。这说明 slua 的打包哲学更偏“项目自己决定脚本以源码、字节码还是热更包形式交给 runtime”，插件只要求最终能拿到字节流。`make_win.bat` 等脚本则说明第三方 Lua 运行时本身被预先打成多平台静态库产物，方便随项目一起分发。

但在当前仓库范围内，没有找到脚本加密、签名校验、manifest 管理、补丁下载器或 CDN 分发等完整实现。也就是说，slua 对“线上热更新工作流”的支持更像是“给你一个可插拔装载点”，而不是“把完整热更发布系统做进插件”。

Angelscript 的部署策略完全不同。`FAngelscriptEngineConfig` 里存在 `bSimulateCooked / bGeneratePrecompiledData / bIgnorePrecompiledData`，引擎启动时会读取 `PrecompiledScript_*.Cache` 或 `PrecompiledScript.Cache`，并校验 build configuration 与 StaticJIT 产物 GUID 是否匹配。这是一条围绕 cooked/precompiled 产物构建的打包路径，而不是动态脚本 byte stream 分发路径。

```
[D11] Packaging Strategy
sluaunreal
├─ Lua source (.lua)                                // 项目可直接分发源码
├─ Lua bytecode (.luac)                             // demo 明确支持
├─ LoadFileDelegate                                 // 项目注入脚本来源
└─ prebuilt liblua.{a/lib}                          // Lua runtime 作为第三方产物打包

Angelscript
├─ script source roots                              // 编译输入
├─ PrecompiledScript*.Cache                         // cooked/precompiled 产物
├─ StaticJIT transpiled code                        // 可选的 native 输出
└─ build/GUID validation                            // 运行时校验产物兼容性
```

[1] 关键源码：slua 的“打包策略”本质上是项目自定义脚本字节提供

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Source/democpp/MyGameInstance.cpp
// 函数: UMyGameInstance::CreateLuaState
// 位置: 42-63，demo 明确同时接受 .lua 与 .luac
// ============================================================================
TArray<FString> luaExts = { TEXT(".lua"), TEXT(".luac") };
for (auto& it : luaExts)
{
    auto fullPath = path + *it;
    FFileHelper::LoadFileToArray(Content, *fullPath);
    if (Content.Num() > 0)
    {
        filepath = fullPath;
        return MoveTemp(Content);
    }
}
// ★ 插件不强制脚本必须是源码，字节码同样是合法部署形态
```

```bat
:: ============================================================================
:: 文件: Reference/sluaunreal/Plugins/slua_unreal/make_win.bat
:: 函数: build lua static library
:: 位置: 1-15，多平台 Lua runtime 先构建为可分发静态库
:: ============================================================================
cmake -G "Visual Studio 16 2019" ..
cmake --build build_win32 --config RelWithDebInfo
copy /Y build_win32\RelWithDebInfo\lua.lib Library\Win32\lua.lib

cmake -G "Visual Studio 16 2019" -A x64 ..
cmake --build build_win64 --config RelWithDebInfo
copy /Y build_win64\RelWithDebInfo\lua.lib Library\Win64\lua.lib
:: ★ 打包时不需要现场编译 Lua，直接携带预制 liblua 产物即可
```

[2] 对照源码：Angelscript 部署更偏 precompiled cache 与 build 一致性校验

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h
// 函数: FAngelscriptEngineConfig
// 位置: 64-83，部署期开关直接进入运行时配置
// ============================================================================
struct FAngelscriptEngineConfig
{
    bool bSimulateCooked = false;
    bool bGeneratePrecompiledData = false;
    bool bDevelopmentMode = false;
    bool bIgnorePrecompiledData = false;
    bool bIsEditor = false;
    bool bRunningCommandlet = false;
}; // ★ 配置项直接区分 cooked / generate / ignore precompiled data
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: FAngelscriptEngine::Initialize
// 位置: 1513-1557, 1582-1589
// ============================================================================
if (bUsePrecompiledData)
{
    Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript_Development.Cache");
    if (!IFileManager::Get().FileExists(*Filename))
        Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript.Cache");

    PrecompiledData->Load(Filename);
    if (!PrecompiledData->IsValidForCurrentBuild())
    {
        /* 省略丢弃不匹配 cache 的清理代码 */ // ★ build 配置不匹配就丢弃 cache
    }

    const FStaticJITCompiledInfo* CompiledInfo = FStaticJITCompiledInfo::Get();
    if (CompiledInfo != nullptr && CompiledInfo->PrecompiledDataGuid != PrecompiledData->DataGuid)
    {
        FJITDatabase::Get().Clear(); // ★ transpiled C++ 与 cache GUID 不一致时禁用 JIT 产物
    }
}

if (bGeneratePrecompiledData)
{
    FString Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript.Cache");
    PrecompiledData->InitFromActiveScript();
    PrecompiledData->Save(Filename);
} // ★ 打包产物是引擎自管的 precompiled cache
```

### 设计取舍

- slua 的部署接口非常灵活，适合项目接自己的 patch/资源系统；但插件层对安全、版本一致性和增量包管理没有统一约束。
- Angelscript 的 precompiled cache 策略更稳健，能做构建配置与 JIT 产物一致性检查；但它解决的是“如何稳定发布编译产物”，不是“如何在线下发脚本补丁”。
- 如果要把 slua 视为线上热更方案，真正决定上限的不是 runtime 本身，而是项目侧如何实现 `LoadFileDelegate` 背后的文件系统/网络/加密链。

### 与 Angelscript 对比

| 观察点 | sluaunreal | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 脚本部署形态 | `.lua`/`.luac` + 自定义 loader（`MyGameInstance.cpp:42-63`, `LuaState.h:110,167-189`） | `PrecompiledScript*.Cache` + StaticJIT 产物（`AngelscriptEngine.h:64-83`, `AngelscriptEngine.cpp:1513-1589`） | 实现方式不同 |
| 产物一致性校验 | inspected runtime 未见 build/GUID 级别校验 | build 配置和 `PrecompiledDataGuid` 校验（`AngelscriptEngine.cpp:1536-1556`） | 没有实现 |
| 加密 / 签名 / manifest / 分发 | 本次核查范围内未见完整实现 | 本次核查范围内同样未见完整实现 | 都没有实现 |

## 小结

- sluaunreal 的最强特征不是“纯静态绑定”，而是“静态 wrapper + 运行时反射缓存 + profiler 工具链 + 项目可注入 loader”的组合。
- 当前 Angelscript 的真实基准也不是“纯手写绑定”，而是“手写规则 + UHT function table + reflective fallback + StaticJIT”的混合体系；这一点在 D2 比用户预设更接近 slua 的混合思路。
- D4 是两者差异最大的维度：slua 更像给线上热更系统留钩子，Angelscript 更像自己实现了一套开发期热重载编译链。
- D8/D11 也体现出同样分工：slua 把 bridge 与 profiler 做深，Angelscript 把预编译产物与执行期优化做深。

## 与 Angelscript 差异速查

| 维度 | sluaunreal 结论 | Angelscript 现状 | 差距判断 | 值得吸收点 | 优先级 |
| --- | --- | --- | --- | --- | --- |
| D1 架构 | runtime + profiler 双模块，外置 codegen 工具 | runtime + editor + test + UHTTool 更完整 | 实现方式不同 | 如果要补线上观测，可单独加 profiler 模块，不必塞进 runtime | 中 |
| D2 绑定 | 静态 wrapper + extension map + `LuaFunctionAccelerator` | 手写 bind + UHT function table + fallback + StaticJIT metadata | 实现方式不同 | Angelscript 已有 function table，后续更值得吸收的是 slua 对 bridge 热路径的缓存思路 | 高 |
| D3 Blueprint | 运行时 hook `UFunction` + `BlueprintFunctionLibrary` | BlueprintType/Callable/Event/Getter/Setter 进统一脚本类型系统 | 实现方式不同 | 若 Angelscript 需要更“无侵入”的对象级 override，可研究 slua 的 `duplicateUFunction` 方案 | 中 |
| D4 热更新 | 提供项目可注入 loader，仓库内未内建 watcher/回滚 | 目录监听、线程扫描、软/全量 reload、失败保留旧代码 | 实现方式不同 | 如果以后做线上热补丁，不应直接复用现有 dev hot reload，而应独立设计 loader/版本控制层 | 高 |
| D8 性能 | bridge 缓存 + 完整 profiler UI/TCP 采集 | precompiled data + StaticJIT + debug/coverage | 实现方式不同 | 为 Angelscript 增加独立运行时 profiler/可视化工具，价值很高 | 高 |
| D11 打包 | `.lua/.luac` + 自定义 loader + 预制 `liblua` | `PrecompiledScript*.Cache` + build/GUID 校验 | 实现方式不同 | 若要增强发布稳健性，可给脚本产物增加版本校验/manifest；若要做线上热更，则需新增而非改造现有 cache 流程 | 高 |

---

## 深化分析 (2026-04-08 18:26:13)

### [维度 D1 / D2] 静态导出的真实触发链：Editor 按钮驱动，而不是编译系统内生步骤

前文已经确认 sluaunreal 有 `Tools/config.json -> LuaWrapper*.inc` 这条静态导出链，但本轮继续往下看，发现“触发 codegen 的入口”并不在 runtime 模块或编译系统里，而是在另一个独立的 Editor 插件 `Plugins/lua_wrapper/`。`lua_wrapper` 模块把按钮挂进 `LevelEditor` 的菜单和工具栏，真正执行时直接 `system()` 调 `Tools/lua-wrapper.exe`。这意味着 slua 的静态导出不是“每次构建天然会跑”，而是“开发者在编辑器里显式触发的外部工具”。

运行时这一侧则是另外一种思路：`LuaWrapper.cpp` 按 UE minor version 在编译期挑选 `LuaWrapper4.18.inc / 4.25.inc / 5.1.inc / 5.2.inc / 5.3.inc / 5.4.inc`，然后在 `LuaObject::init()` 里先 `LuaWrapper::initExt(L)`，再初始化 `ExtensionMethod::init()`。也就是说，slua 的“静态导出”并不是单一策略，而是“Editor 手工触发生成 + 运行时编译进版本分片 wrapper + UObject 仍走 extension map 动态查找”的三段式组合。

```
[D1/D2] Static Export Trigger Chain
sluaunreal
├─ lua_wrapper (Editor plugin)                     // 编辑器按钮触发 codegen
│  ├─ LevelEditor menu / toolbar
│  └─ system(".../Tools/lua-wrapper.exe")
├─ Tools/config.json                               // 输出写回 runtime Private
├─ LuaWrapper5.x.inc                               // 按 UE minor version 编译进模块
└─ LuaObject::init()                               // initExt + ExtensionMethod::init

Angelscript
├─ UHT exporter attribute                          // 编译流程自动参与
├─ scan BlueprintCallable/Pure in UHT session
├─ emit AS_FunctionTable_*.cpp
└─ emit skipped csv / generation artifacts
```

[1] 关键源码：slua 的 codegen 由独立 Editor 插件按钮触发

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/lua_wrapper/Source/lua_wrapper/Private/lua_wrapper.cpp
// 函数: Flua_wrapperModule::StartupModule / PluginButtonClicked
// 位置: 26-60, 122-135，LevelEditor 菜单注册与外部工具执行
// ============================================================================
void Flua_wrapperModule::StartupModule()
{
    Flua_wrapperStyle::Initialize();
    Flua_wrapperCommands::Register();

    PluginCommands->MapAction(
        Flua_wrapperCommands::Get().OpenPluginWindow,
        FExecuteAction::CreateRaw(this, &Flua_wrapperModule::PluginButtonClicked),
        FCanExecuteAction());

    FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
    MenuExtender->AddMenuExtension("WindowLayout", EExtensionHook::After, PluginCommands,
        FMenuExtensionDelegate::CreateRaw(this, &Flua_wrapperModule::AddMenuExtension));
    ToolbarExtender->AddToolBarExtension("Settings", EExtensionHook::After, PluginCommands,
        FToolBarExtensionDelegate::CreateRaw(this, &Flua_wrapperModule::AddToolbarExtension));
    // ★ codegen 入口挂在编辑器 UI，而不是 UBT/UHT 钩子
}

void Flua_wrapperModule::PluginButtonClicked()
{
#ifdef _MSC_VER
    auto contentDir = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*FPaths::ProjectContentDir());
    auto cmd = contentDir + TEXT("/../Tools/lua-wrapper.exe");
    system(TCHAR_TO_UTF8(*cmd));
    // ★ 直接起外部 exe；是否生成、何时生成，取决于操作者是否点按钮
#else
    auto ret = exec("/Library/Frameworks/Mono.framework/Versions/Current/Commands/mono lua-wrapper.exe");
#endif
}
```

[2] 关键源码：运行时按引擎版本装配 wrapper，并把 `UObject` 扩展留给动态查找

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaWrapper.cpp
// 函数: LuaWrapper::initExt
// 位置: 55-67, 184-188，按 UE 版本编译进不同 wrapper 分片
// ============================================================================
#if ((ENGINE_MINOR_VERSION<25) && (ENGINE_MAJOR_VERSION==4))
    #include "LuaWrapper4.18.inc"
#elif ((ENGINE_MINOR_VERSION>=25) && (ENGINE_MAJOR_VERSION==4))
    #include "LuaWrapper4.25.inc"
#elif ((ENGINE_MINOR_VERSION==1) && (ENGINE_MAJOR_VERSION==5))
    #include "LuaWrapper5.1.inc"
#elif ((ENGINE_MINOR_VERSION==2) && (ENGINE_MAJOR_VERSION==5))
    #include "LuaWrapper5.2.inc"
#elif ((ENGINE_MINOR_VERSION==3) && (ENGINE_MAJOR_VERSION==5))
    #include "LuaWrapper5.3.inc"
#elif ((ENGINE_MINOR_VERSION==4) && (ENGINE_MAJOR_VERSION==5))
    #include "LuaWrapper5.4.inc"
#endif

void LuaWrapper::initExt(lua_State* L)
{
    init(L);
    FSoftObjectPtrWrapper::bind(L);
    // ★ 生成的 wrapper 不是运行时读取文本，而是编译期挑选 .inc 后直接参与链接
}
```

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaObject.cpp
// 函数: LuaObject::init / searchExtensionMethod
// 位置: 741-776, 3054-3059，wrapper 初始化后仍通过 extension map 搜索 UObject 扩展
// ============================================================================
int searchExtensionMethod(lua_State* L, UClass* cls, const char* name, bool isStatic=false) {
    while (cls != nullptr) {
        TMap<FString, ExtensionField>* mapptr = isStatic ? extensionMMap_static.Find(cls) : extensionMMap.Find(cls);
        if (mapptr != nullptr) {
            auto fieldptr = mapptr->Find(name);
            if (fieldptr != nullptr) {
                if (fieldptr->isFunction) {
                    lua_pushcfunction(L, fieldptr->func);
                    break;
                }
                // ★ 属性 getter/setter 也走同一张 extension map
            }
        }
        cls = cls->GetSuperClass();
        // ★ 从当前类向父类爬链，说明 UObject 扩展仍保留动态分派能力
    }
}

void LuaObject::init(lua_State* L)
{
    LuaWrapper::initExt(L);
    ExtensionMethod::init();
    // ★ 先装静态 wrapper，再补 extension method，体现“静态导出 + 动态扩展”叠加
}
```

[3] 对照源码：Angelscript 的 function table 生成是 UHT exporter，而不是编辑器按钮

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs
// 函数: Export
// 位置: 21-45，UHT exporter 自动接入编译流程并输出审计产物
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
        CountBlueprintCallableFunctions(module.ShortName, module.ScriptPackage, skippedEntries,
            ref classCount, ref functionCount, ref reconstructedCount, ref skippedCount);
    }
    WriteSkippedEntriesCsv(factory, skippedEntries);
    WriteSkippedReasonSummaryCsv(factory, skippedEntries);
    // ★ 生成和审计都在 UHT session 内完成，构建可重复性比手工按钮更强
}
```

这会把 D1/D2 的对比结论再往前推一步：

- sluaunreal 不是“没有自动化”，而是把自动化放在 Editor 工具层。优点是对宿主工程上下文更自由，坏处是 codegen 是否新鲜依赖操作者习惯。
- Angelscript 不是“只有手写 bind”，而是把可生成部分塞进 UHT exporter。优点是生成时机稳定、可输出 skipped 报表；代价是和 UHT 生命周期更强耦合。
- 差距判断这里应归为“实现方式不同 + 生成链可审计性存在质量差异”，不是简单的“slua 有静态导出、Angelscript 没有”。

### [维度 D3] `FLuaBPVar` 不是辅助壳，而是 slua 的 Blueprint 动态值总线

前文提到 slua 有 `ULuaBlueprintLibrary`，但本轮继续追踪后可以确认：`FLuaBPVar` 并不是一个普通 helper struct，而是 Blueprint <-> Lua 桥接的“总线类型”。它既是 `BlueprintType`，又直接包裹 `LuaVar`；`CallToLuaWithArgs()` 只是把每个 `FLuaBPVar.value` 直接压入 Lua 栈。更关键的是，`LuaObject` 的 `UStruct` marshalling 路径也专门给 `FLuaBPVar` 开了旁路，这让它能参与普通 `UFunction` 参数收发，而不是只存在于 function library API 里。

这和 Angelscript 的 Blueprint 交互取向完全不同。Angelscript 会先判断 `UClass` 是否值得暴露，再从 `UFunction` 重建 `FAngelscriptFunctionSignature`，把参数和返回类型编进脚本签名。slua 选择的是“把动态值容器送进 Blueprint 图”，Angelscript 选择的是“把 Blueprint API 变成脚本语言里的静态函数签名”。

```
[D3] Blueprint Value Bridge
sluaunreal
Blueprint Pin
├─ FLuaBPVar (LuaVar payload)                      // 动态值容器
├─ CallToLuaWithArgs()                             // 逐个 push 到 Lua 栈
├─ LuaObject special-case for FLuaBPVar            // 进入通用 UFunction marshalling
└─ Get*FromVar runtime warnings                    // 类型/索引错误推迟到运行期

Angelscript
UClass/UFunction scan
├─ ShouldBindEngineType()                          // 先决定哪些类型进系统
├─ Build FAngelscriptFunctionSignature             // 重建参数/返回类型
└─ BindBlueprintEvent() / BindBlueprintCallable()  // 绑定成 typed script API
```

[1] 关键源码：`FLuaBPVar` 直接暴露给 Blueprint，并携带 `LuaVar`

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaBlueprintLibrary.h
// 函数: FLuaBPVar / ULuaBlueprintLibrary
// 位置: 21-31, 41-76，Blueprint 图里实际流动的是 LuaVar 包装
// ============================================================================
USTRUCT(BlueprintType)
struct SLUA_UNREAL_API FLuaBPVar {
    GENERATED_USTRUCT_BODY()
public:
    FLuaBPVar(const NS_SLUA::LuaVar& v) : value(v) {}
    FLuaBPVar(NS_SLUA::LuaVar&& v) : value(MoveTemp(v)) {}
    FLuaBPVar() {}

    NS_SLUA::LuaVar value;
    // ★ Blueprint 层拿到的不是静态 pin 类型，而是一个动态 payload 容器
};

UFUNCTION(BlueprintCallable, meta=(DisplayName="Call To Lua With Arguments", WorldContext="WorldContextObject"), Category="slua")
static FLuaBPVar CallToLuaWithArgs(UObject* WorldContextObject, FString FunctionName, const TArray<FLuaBPVar>& Args, FString StateName);

UFUNCTION(BlueprintCallable, Category="slua")
static int GetIntFromVar(FLuaBPVar Value, int Index=1);
// ★ 取值 API 也是“从动态容器按索引取”，不是 Blueprint 编译期静态 pin
```

[2] 关键源码：`FLuaBPVar` 被插进通用 marshalling 分支，错误在运行期发 Kismet 警告

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaBlueprintLibrary.cpp
// 函数: ULuaBlueprintLibrary::CallToLuaWithArgs / FLuaBPVar::checkValue / getValueFromVar
// 位置: 51-77, 140-145, 217-233
// ============================================================================
FLuaBPVar ULuaBlueprintLibrary::CallToLuaWithArgs(UObject* WorldContextObject, FString funcname, const TArray<FLuaBPVar>& args, FString StateName) {
    auto ls = LuaState::get(gameInstance);
    LuaVar f = ls->get(TCHAR_TO_UTF8(*funcname));
    auto fillParam = [&]
    {
        for (auto& arg : args) {
            arg.value.push(ls->getLuaState());
            // ★ Blueprint 侧参数不做静态展开，直接把 LuaVar 压栈
        }
        return args.Num();
    };
    return f.callWithNArg(fillParam);
}

void* FLuaBPVar::checkValue(NS_SLUA::lua_State* L, NS_SLUA::FStructProperty* p, uint8* params, int i)
{
    FLuaBPVar ret;
    ret.value.set(L, i);
    p->CopyCompleteValue(params, &ret);
    // ★ 从 Lua 栈回填 Blueprint struct 时，也是直接保存 LuaVar
    return nullptr;
}

template<class T>
T getValueFromVar(const FLuaBPVar& Value, int Index) {
    const LuaVar& lv = Value.value;
    if (Index <= lv.count()) {
        if (getValue(lv, Index, v))
            return v;
        else
            FFrame::KismetExecutionMessage(TEXT("Attempted to index an item from an invalid type!"),
                ELogVerbosity::Warning, GetVarTypeErrorWarning);
        // ★ 类型错误不会在编译期暴露，而是运行时发 Kismet warning
    }
    return T();
}
```

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaObject.cpp
// 函数: pushUStructProperty / checkUStructProperty
// 位置: 2244-2246, 2438-2439，FLuaBPVar 在通用 struct 桥里被特殊照顾
// ============================================================================
if (uss == FLuaBPVar::StaticStruct()) {
    ((FLuaBPVar*)parms)->value.push(L);
    return 1;
}

if (uss == FLuaBPVar::StaticStruct())
    return FLuaBPVar::checkValue(L, p, parms, i);
// ★ 这两处让 FLuaBPVar 不只是 BlueprintFunctionLibrary 的 helper，而是进入 UFunction 参数桥
```

[3] 对照源码：Angelscript 把 Blueprint API 编成 typed signature，而不是 variant payload

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp
// 函数: ShouldBindEngineType
// 位置: 962-999，先决定哪些 UClass 进入脚本类型系统
// ============================================================================
bool ShouldBindEngineType(UClass* Class)
{
    if (Class == UObject::StaticClass())
        return true;
    if (!Class->HasAnyClassFlags(CLASS_Native))
        return false;
    if (Class->HasMetaData(NAME_NotInAngelscript))
        return false;
    if (Class->GetBoolMetaData(NAME_BlueprintType))
        return true;
    // ★ 先做类型准入，再谈函数绑定
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp
// 函数: BindBlueprintEvent
// 位置: 583-633，Blueprint event 在绑定阶段重建 typed signature
// ============================================================================
if (!Signature.bAllTypesValid)
    return;
if (Signature.ArgumentTypes.Num() > AS_EVENT_MAX_ARGS)
    return;

auto* Sig = new FBlueprintEventSignature;
Sig->FunctionName = Function->GetFName();
Sig->UnrealFunction = Function;
Sig->ArgCount = Signature.ArgumentTypes.Num();
Sig->ReturnType = Signature.ReturnType;
for (int32 i = 0; i < Sig->ArgCount; ++i)
    Sig->Arguments[i] = Signature.ArgumentTypes[i];

int32 FunctionId = FAngelscriptBinds::BindMethodDirect(
    InType->GetAngelscriptTypeName(),
    Signature.Declaration,
    asFUNCTION(CallEventWithSignature), asCALL_GENERIC, ASAutoCaller::FunctionCaller::Make(), Sig);
// ★ Blueprint event 最终变成脚本语言里的 typed method，而不是一个运行时拆箱容器
```

这里的差距判断应是“实现方式不同”：

- slua 的优势是 Blueprint 图可以快速承载 tuple/table/混合返回值，项目层做原型很快。
- 代价是 pin 语义变弱，很多错误只能在运行时通过 `KismetExecutionMessage` 暴露。
- Angelscript 的优势是类型边界前移到绑定阶段；代价是要维护更复杂的类型重建和签名系统。

### [维度 D4 / D11] 热更新边界停在“字节流装载”，没有进入“模块失效与状态迁移”

本轮继续追踪 `LoadFileDelegate` 后，能把 slua 的热更新边界说得更准确：插件层只定义“给我模块名，我返回字节流和解析后的真实路径”。demo 里这一步明确同时接受 `.lua` 和 `.luac`。`requireModule()` 则没有再包一层自定义模块系统，而是直接调用 Lua 全局 `require`。再往前走，`LuaSimulate::StartSimulateLua()` 会新建一个独立 `LuaState`，把同一个 `LoadFileDelegate` 装进去做模拟执行。

这三段合起来说明：slua 的热更新/部署接口主要解决“如何拿到脚本字节”和“如何起一个 VM 去跑”，但没有把模块 cache 失效、在线对象状态迁移、构建版本校验、失败回滚框架做进插件本身。结合本轮对 `LuaState.cpp / LuaOverrider.cpp / LuaObject.cpp` 的检索，未见插件层对 `package.loaded` 的显式失效处理；因此模块刷新策略应由项目侧 Lua/资源系统自行承担。这和 Angelscript 的策略差异非常大，后者把 precompiled cache、build identifier、JIT GUID 和 hot reload 开关都收拢到引擎层。

```
[D4/D11] Reload Boundary
sluaunreal
Patch / resource system
├─ LoadFileDelegate(fn, filepath) -> bytes         // 插件只要求字节流
├─ .lua or .luac accepted                          // demo 允许源码/字节码两种形态
├─ LuaState::requireModule() -> Lua require        // 继续使用 Lua 默认模块语义
└─ LuaSimulate scratch VM                          // 需要模拟时直接起独立状态机

Angelscript
Build output
├─ PrecompiledScript_[Config].Cache
├─ BuildIdentifier validation
├─ StaticJIT DataGuid validation
└─ dev-mode gate for hot reload
```

[1] 关键源码：slua 只定义 loader 合约，demo 自己决定 `.lua/.luac` 搜索与 VM 创建

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaState.h
// 函数: LoadFileDelegate / doFile / requireModule / setLoadFileDelegate
// 位置: 110, 167-189，热更新入口只是一份字节流委托
// ============================================================================
typedef TArray<uint8> (*LoadFileDelegate) (const char* fn, FString& filepath);

LuaVar doFile(const char* fn, LuaVar* pEnv = nullptr);
LuaVar requireModule(const char* fn, LuaVar* pEnv = nullptr);
void setLoadFileDelegate(LoadFileDelegate func);
// ★ 运行时并不要求“文件来自哪里”，只要求外部能给出模块字节和真实路径
```

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Source/democpp/MyGameInstance.cpp
// 函数: UMyGameInstance::CreateLuaState
// 位置: 41-64，项目层 loader 同时支持源码和字节码
// ============================================================================
state = new NS_SLUA::LuaState("SLuaMainState", this);
state->setLoadFileDelegate([](const char* fn, FString& filepath) -> TArray<uint8> {
    FString path = FPaths::ProjectContentDir();
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
    return MoveTemp(Content);
});
// ★ 插件没有规定部署物必须是什么；项目层自己决定优先读源码还是字节码
```

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaState.cpp
// 函数: LuaState::loadFile / requireModule
// 位置: 153-155, 768-784，插件层不包自己的模块缓存失效逻辑
// ============================================================================
TArray<uint8> LuaState::loadFile(const char* fn, FString& filepath) {
    if (loadFileDelegate) return loadFileDelegate(fn, filepath);
    return TArray<uint8>();
}

LuaVar LuaState::requireModule(const char* fn, LuaVar* pEnv) {
    lua_getglobal(L, "require");
    lua_pushstring(L, fn);
    if (lua_pcall(L, 1, 1, top)) {
        lua_pop(L, 2);
        return LuaVar();
    }
    // ★ 这里直接代理给 Lua 标准 require；插件层没有自建模块装载/失效表
    LuaVar luaModule(L, -retCount);
    return MoveTemp(luaModule);
}
```

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaSimulate.cpp
// 函数: LuaSimulate::StartSimulateLua
// 位置: 98-109，开发态模拟是“另起一个独立 LuaState”
// ============================================================================
void LuaSimulate::StartSimulateLua()
{
    if (Delegate == nullptr)
    {
        Log::Error("lua Simulation Error. LoadFileDelegate not set.");
        return;
    }
    StopSimulateLua();
    SluaState = new NS_SLUA::LuaState("", nullptr);
    SluaState->setLoadFileDelegate(Delegate);
    SluaState->init();
    // ★ 模拟执行通过新建状态机完成，不是对现有运行态对象图做就地热替换
}
```

[2] 对照源码：Angelscript 把打包一致性和热重载开关都收进引擎层

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp
// 函数: FAngelscriptPrecompiledData::IsValidForCurrentBuild
// 位置: 2642-2645，cache 至少受 BuildIdentifier 约束
// ============================================================================
bool FAngelscriptPrecompiledData::IsValidForCurrentBuild()
{
    return BuildIdentifier == GetCurrentBuildIdentifier() && BuildIdentifier != -1;
    // ★ 只要当前 build 标识不同，预编译脚本就整体失效
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: FAngelscriptEngine::Initialize / CompileModules
// 位置: 1519-1556, 2054-2056, 3878-4005
// ============================================================================
Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript_Development.Cache");
if (!IFileManager::Get().FileExists(*Filename))
    Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript.Cache");

PrecompiledData->Load(Filename);
if (!PrecompiledData->IsValidForCurrentBuild())
{
    delete PrecompiledData;
    PrecompiledData = nullptr;
    // ★ build 不匹配就丢弃 cache，而不是尝试带病运行
}

const FStaticJITCompiledInfo* CompiledInfo = FStaticJITCompiledInfo::Get();
if (CompiledInfo != nullptr && CompiledInfo->PrecompiledDataGuid != PrecompiledData->DataGuid)
{
    FJITDatabase::Get().Clear();
    // ★ cache 与 transpiled C++ GUID 不一致时，JIT 产物也要一起失效
}

UE_LOG(Angelscript, Warning, TEXT("Using fully precompiled scripts. Hot reloading is disabled for this run."));
UE_LOG(Angelscript, Warning, TEXT("Delete PrecompiledScript.Cache or run with -as-development-mode flag to enable hot reload."));

if (bHadCompileErrors)
{
    UE_LOG(Angelscript, Error, TEXT("Hot reload failed due to script compile errors. Keeping all old script code."));
    bShouldSwapInModules = false;
}
// ★ 热重载失败时显式保留旧代码，这和 slua 的“给字节流就加载”是完全不同的安全边界
```

因此，D4/D11 这里更精确的差距判断应是：

- sluaunreal：实现的是“可插拔脚本装载点”，不是“插件自带热补丁发布系统”。
- Angelscript：实现的是“构建产物一致性 + 开发期热重载安全阀”，不是“在线脚本下发系统”。
- 两者都不是缺失，而是面向场景根本不同；如果 Angelscript 未来要补 runtime patch，最可借鉴的是 slua 的 loader contract，而不是照搬其现有开发期 hot reload 代码。

### [维度 D8] slua 的 profiler 本质是自动 VM instrumentation；Angelscript 更像接入 Unreal Trace

前文已覆盖 slua 有 remote profiler，但本轮继续顺着实现往下看，会发现它的重心其实不是 UI，而是对 Lua VM 的“自动插桩”。`LuaState` 在 `ENABLE_PROFILER && !UE_BUILD_SHIPPING` 下直接用 `LuaMemoryProfile::alloc` 作为 Lua allocator；`LuaProfiler.cpp` 再通过 `lua_sethook(L, debug_hook, LUA_MASKRET | LUA_MASKCALL, 0)` 监听调用/返回，用 `profileTotalCost` 扣掉 profiler 自己带来的开销，并额外识别 `coroutine.resume` / `yield` 来补协程边界。

更关键的是，slua 的 profiler 有两个后端：如果没在本地录制，就把采样包序列化后经 socket 发给 Editor；如果在本地录制，就把样本送给 `SluaProfilerDataManager`，后者会维护调用树、处理协程 begin/end 不对称，并支持保存/加载 `.sluastat` 记录。这说明 slua 的 profiler 是一个完整的脚本运行时子系统，而不是只“给 Unreal Trace 打点”。

Angelscript 这边的设计哲学明显不同。当前仓库里能看到的是 `FCpuProfilerTraceScoped`：脚本代码可以构造它，从而向 Unreal 的 CPU profiler trace 发 begin/end event；再配合 `AS_LLM_SCOPE` 把内存计入 `CsvProfiler`。这更像“把脚本纳入 UE 自带 profiling 生态”，而不是自建一个远程 Lua 调用树系统。

```
[D8] Profiling Philosophy
sluaunreal
Lua VM
├─ custom alloc for memory profile                 // 分配器级内存采样
├─ lua_sethook(call/ret)                           // 自动调用链采样
├─ coroutine compensation                          // 协程边界修正
├─ socket streaming OR local record                // 双后端输出
└─ SluaProfilerDataManager call tree               // 维护脚本调用树

Angelscript
Script-visible trace helpers
├─ FCpuProfilerTraceScoped(EventID)                // 手动发 UE trace event
└─ AS_LLM_SCOPE                                    // 复用 UE CsvProfiler/LLM
```

[1] 关键源码：slua 既劫持 Lua allocator，也自动装 call/ret hook

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaState.cpp
// 函数: LuaState::init
// 位置: 558-563，Lua VM allocator 直接切到 profile allocator
// ============================================================================
#if ENABLE_PROFILER && !UE_BUILD_SHIPPING
    L = lua_newstate(LuaMemoryProfile::alloc, this);
#else
    L = luaL_newstate();
#endif
// ★ profiler 不是外部旁路采样，而是从 VM 分配器这一层就介入
```

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaProfiler.cpp
// 函数: takeSample / debug_hook / changeHookState
// 位置: 224-252, 256-325, 329-358
// ============================================================================
if (!SluaProfilerDataManager::IsRecording())
{
    makeProfilePackage(s_messageWriter, event, startTime - profileTotalCost, line, funcname, shortsrc);
    sendMessage(s_messageWriter, L);
}
else
{
    SluaProfilerDataManager::ReceiveProfileData(event, startTime - profileTotalCost, line, funcname, shortsrc);
}
// ★ 同一份采样可以走远程 socket 或本地 record，两种后端共用同一套 hook 数据

void debug_hook(lua_State* L, lua_Debug* ar) {
    int64 start = getTime();
    lua_getinfo(L, "nSl", ar);
    if (ar->event > 1) return;
    if (strstr(ar->short_src, LuaProfiler::ChunkName)) return;

    if (ttislcf(s2v(o)) && fvalue(s2v(o)) == LuaProfiler::resumeFunc) {
        event += PHE_ENTER_COROUTINE;
        lua_sethook(co, debug_hook, LUA_MASKRET | LUA_MASKCALL, 0);
        // ★ 协程进入时给 coroutine 单独挂 hook，避免主线程视角丢样本
    }

    takeSample(event, ar->linedefined, ar->name ? ar->name : "", ar->short_src, start, L);
    profileTotalCost = profileTotalCost + (getTime() - start);
    // ★ 累加 profiler 自己的成本，后续样本会用它做扣减
}

if (state == HookState::HOOKED) {
    profileTotalCost = 0;
    LuaMemoryProfile::onStart(LS);
    takeMemorySample(PHE_MEMORY_TICK, memoryInfoList, L);
    lua_sethook(L, debug_hook, LUA_MASKRET | LUA_MASKCALL, 0);
}
```

[2] 关键源码：slua 的数据管理层会修正协程不对称并维护调用树

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/SluaProfilerDataManager.cpp
// 函数: WatchBegin / WatchEnd / CoroutineBegin / CoroutineEnd
// 位置: 143-179, 183-213
// ============================================================================
void SluaProfilerDataManager::WatchBegin(const FString& fileName, int32 lineDefined, const FString& funcName,
    double nanoseconds, ProfileNodePtr funcProfilerRoot, ProfileCallInfoArray& profilerStack)
{
    TSharedPtr<FunctionProfileCallInfo> funcInfo = MakeShared<FunctionProfileCallInfo>();
    funcInfo->functionDefine = FLuaFunctionDefine::MakeLuaFunctionDefine(fileName, funcName, lineDefined);
    AddToParentNode(funcInfoNode, funcInfo);
    profilerStack.Add(funcInfo);
    // ★ 每次调用进入都显式挂到调用树节点上
}

void SluaProfilerDataManager::WatchEnd(const FString& fileName, int32 lineDefined, const FString& functionName,
    double nanoseconds, ProfileCallInfoArray& profilerStack)
{
    if (callInfo->bIsCoroutineBegin)
    {
        funcNode->costTime = nanoseconds - callInfo->begTime;
        callInfo->ProfileNode->childNode->Add(funcNode->functionDefine, funcNode);
        return;
        // ★ 协程 return 不对称时，直接在树中插补一个节点修正统计
    }
    funcInfoNode->costTime += nanoseconds - callInfo->begTime;
    funcInfoNode->countOfCalls++;
}
```

[3] 对照源码：Angelscript 暴露的是 UE Trace helper，不是独立 remote profiler 管线

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/FCpuProfilerTraceScoped.h
// 函数: FCpuProfilerTraceScoped ctor/dtor
// 位置: 14-27，脚本手动构造后向 Unreal Trace 发 begin/end event
// ============================================================================
#if CPUPROFILERTRACE_ENABLED
FCpuProfilerTraceScoped(const FName& EventID)
{
    FCpuProfilerTrace::OutputBeginDynamicEvent(EventID);
}

~FCpuProfilerTraceScoped()
{
    FCpuProfilerTrace::OutputEndEvent();
}
#endif
// ★ 这里是 opt-in trace scope，不是自动 VM call graph hook
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FCpuProfilerTraceScoped.cpp
// 函数: Bind_TraceCPUProfilerEventScoped
// 位置: 4-13，把 trace scope 作为脚本可见类型暴露
// ============================================================================
auto FCpuProfilerTraceScoped_ = FAngelscriptBinds::ExistingClass("FCpuProfilerTraceScoped");
FCpuProfilerTraceScoped_.Constructor("void f(const FName& EventID)", [](FCpuProfilerTraceScoped* Address, const FName& EventID)
{
    new(Address) FCpuProfilerTraceScoped(EventID);
});
// ★ Angelscript 选择复用 Unreal Trace 基础设施，而不是自己维护一套脚本 profiler 协议
```

这一层的结论不能简单写成“slua profiler 更强”：

- slua 强在“自动采样 + 远程协议 + 本地 record + 协程修正”，适合把 Lua VM 当成独立运行时去观察。
- Angelscript 强在“直接接 UE Trace/LLM 生态”，脚本热点可以进入现有 Unreal profiling 工具链。
- 差距判断应是“实现方式不同”；如果 Angelscript 未来要补更强的脚本级 profiler，可直接参考 slua 的 `debug_hook + dual backend + call tree repair` 这一组实现，而不是只看 `slua_profile` UI。

---

## 深化分析 (2026-04-08 18:34:13)

### [维度 D1 / D2] `lua_wrapper` 说明 slua 的静态导出是“手工触发的 Editor 工具链”，不是编译系统内建阶段

前文已经说明 slua 有 `Tools/config.json` 和 `LuaWrapper5.x.inc`。本轮再往前追一层，能看到“谁触发生成”这个关键事实：`Plugins/lua_wrapper/` 是一个单独的 Editor 插件，按钮直接执行 `../Tools/lua-wrapper.exe`。也就是说，slua 的静态导出不是 UBT/UHT 自动阶段，而是项目作者在编辑器里主动按一次工具按钮后，把生成物写回 `slua_unreal/Private/`。

这个差异直接影响维护边界。运行时模块只消费已经落地的 `.inc` 生成物，生成器自身则依赖 `config.json` 里整套宿主工程 include / preprocess 环境。对比之下，Angelscript 的 `AngelscriptFunctionTableExporter` 是 UHT exporter，生成、清理陈旧文件、输出覆盖率摘要都发生在编译链内，因此“生成是否完整”是可审计的。

```
[D1/D2] Codegen Trigger Path
sluaunreal
├─ lua_wrapper Editor plugin                         // 单独 Editor 插件
│  └─ PluginButtonClicked()
│     └─ ../Tools/lua-wrapper.exe                   // 外调 exe，不走 UHT/UBT
├─ Tools/config.json                                 // 宿主工程 include/preprocess/filter
├─ LuaWrapper5.x.inc                                 // 生成到 runtime 私有目录
└─ slua_unreal Runtime
   └─ LuaWrapper.cpp include selected .inc          // 运行时只消费生成产物

Angelscript
├─ UhtExporter(AngelscriptFunctionTable)            // UHT exporter
├─ GenerateModule() -> AS_FunctionTable_*.cpp       // 编译期 shard 生成
├─ WriteSkippedEntriesCsv / Summary.json            // 诊断产物
└─ Runtime bind table                               // 与编译链闭环
```

[1] 关键源码：slua 的生成器确实是独立 Editor 插件，按钮直接 shell 出外部 exe

```json
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/lua_wrapper/lua_wrapper.uplugin
// 函数: plugin descriptor
// 位置: 16-20，声明 lua_wrapper 只在 Editor 侧加载
// ============================================================================
{
  "Name": "lua_wrapper",
  "Type": "Editor",
  "LoadingPhase": "Default"
} // ★ 生成器不是 runtime 依赖，而是单独 Editor 插件
```

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/lua_wrapper/Source/lua_wrapper/Private/lua_wrapper.cpp
// 函数: Flua_wrapperModule::PluginButtonClicked
// 位置: 122-135，点击工具栏按钮后直接执行 ../Tools/lua-wrapper.exe
// ============================================================================
void Flua_wrapperModule::PluginButtonClicked()
{
#ifdef _MSC_VER
    auto contentDir = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*FPaths::ProjectContentDir());
    auto cmd = contentDir + TEXT("/../Tools/lua-wrapper.exe");
    system(TCHAR_TO_UTF8(*cmd));
    // ★ 这里是裸 system() 调外部 exe，不是 UHT/UBT 生命周期里的生成步骤
#else
    auto contentDir = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*FPaths::ProjectContentDir());
    auto toolsDir = contentDir + TEXT("../Tools/");
    chdir(TCHAR_TO_UTF8(*toolsDir));
    auto ret = exec("/Library/Frameworks/Mono.framework/Versions/Current/Commands/mono lua-wrapper.exe");
#endif
}
```

[2] 关键源码：slua runtime 只 include 生成物；Angelscript 则把生成器注册进 UHT 并输出审计摘要

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaWrapper.cpp
// 函数: generated include switch / LuaWrapper::initExt
// 位置: 55-67, 184-188，runtime 只按 UE 版本包含预先生成的 .inc
// ============================================================================
#if ((ENGINE_MINOR_VERSION==2) && (ENGINE_MAJOR_VERSION==5))
    #include "LuaWrapper5.2.inc"
#elif ((ENGINE_MINOR_VERSION==3) && (ENGINE_MAJOR_VERSION==5))
    #include "LuaWrapper5.3.inc"
#elif ((ENGINE_MINOR_VERSION==4) && (ENGINE_MAJOR_VERSION==5))
    #include "LuaWrapper5.4.inc"
#endif
// ★ 运行时这里只是消费生成物，完全看不到“生成动作”本身

void LuaWrapper::initExt(lua_State* L)
{
    init(L);
    FSoftObjectPtrWrapper::bind(L);
    // ★ 生成出的 wrapper 和少量手写 wrapper 一起注册进 Lua
}
```

```json
// ============================================================================
// 文件: Reference/sluaunreal/Tools/config.json
// 函数: codegen config
// 位置: 11-18, 59-70，生成器需要宿主工程的 filter + include/preprocess 上下文
// ============================================================================
"filter": [
  {
    "type": "FTransform",
    "ctors": [ 3 ],
    "methods": [ "Accumulate", "LerpTranslationScale3D" ]
  }
],
"filter_class": [
  "FPolyglotTextData",
  "FAssetBundleData"
],
"output_dir": "{solution_dir}/Plugins/slua_unreal/Source/slua_unreal/Private/",
"win": {
  "solution_dir": "../",
  "ue4_dir": "C:/Program Files/Epic Games/UE_5.2",
  "ue_vcproj": "{solution_dir}/Intermediate/ProjectFiles/UE5.vcxproj",
  "include_path": "...",
  "preprocess": "..."
}
// ★ 这说明 slua 的静态导出强依赖宿主工程编译上下文，而不是插件自洽的 UHT 阶段
```

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs
// 函数: Export
// 位置: 21-27, 35-47，UHT exporter 直接挂进编译链
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
    // ★ 生成器是编译系统的一部分，失败与覆盖率都可回写产物
}
```

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs
// 函数: Generate / WriteGenerationSummary
// 位置: 74-77, 166-215，生成后会清理陈旧文件并写 summary/csv
// ============================================================================
DeleteStaleOutputs(factory, generatedPaths);
WriteGenerationSummary(factory, moduleSummaries, csvEntries, generatedFileCount);
WriteCoverageDiagnostics(moduleSummaries);
// ★ 生成物生命周期由工具维护，旧 shard 也会被清理

string summaryPath = factory.MakePath("AS_FunctionTable_Summary", ".json");
File.WriteAllText(summaryPath, summaryJson, Encoding.UTF8);
WriteModuleSummaryCsv(factory, moduleSummaries);
WriteEntryCsv(factory, csvEntries);
// ★ 不是只吐 C++ 文件，还会吐 direct/stub/模块覆盖率摘要
```

差距判断：

- 生成触发位置：`实现方式不同`。slua 是 Editor 按钮外调工具；Angelscript 是 UHT exporter。
- 生成物可审计性：`实现质量差异`。Angelscript 的 skipped/direct/stub 统计更容易持续追踪；slua 当前源码里没有同级别的生成摘要链。
- 宿主耦合度：`实现方式不同`。slua 的 `config.json` 直接绑定项目路径和本地 UE 安装；Angelscript 的 exporter 绑定 UHT session，而不是项目目录字符串。

### [维度 D3] slua 的 Blueprint 互通核心其实是“实例级 Lua self table + lazy Super cache”

前文已经证明 slua 会 hook `UFunction`。本轮继续往里追，能看到它并不是简单把函数重定向到 Lua，而是先构造一个实例级 `luaSelfTable`：如果模块 table 有 `__call`，就先执行一次拿到 self table；随后 `setmetatable()` 把 `__cppinst`、`Super`、`__index/__newindex` 全部接进去。此后 Blueprint / C++ / Lua 三边的成员查找都汇聚到这个表和它的 metatable 上。

更细的一层在 `LuaOverriderSuper.cpp`：`Super.xxx` 第一次访问时才查找 `__overrider_xxx`，并把 `LuaFunctionAccelerator` 闭包缓存进 uservalue。这样 super call 的后续命中不必重新走 `FindFunctionByName + 反射解析`。这说明 slua 的 Blueprint 互通不是“类型系统接起来”，而是“对象实例在运行时被接成一个 Lua 代理层”。

```
[D3] Override / Super Chain
Lua module
├─ requireModule(luaFilePath)                       // 先拿模块
├─ module.__call(...) -> luaSelfTable               // 模块可构造成实例 self table
├─ setmetatable(luaSelfTable)
│  ├─ __cppinst = UObject*                          // C++ 对象回链
│  ├─ Super = LuaSuperCall                          // super 入口
│  └─ __index/__newindex                            // 函数/属性回退到 UObject/LuaNet
└─ hookBpScript(UFunction)
   ├─ duplicate -> __overrider_Func                // 保留 super 原入口
   └─ install luaOverrideFunc                      // Blueprint/native 统一改挂到 Lua

LuaSuperCall
├─ __superIndex(name)                              // 首次访问时解析 super UFunction
├─ cache LuaFunctionAccelerator in uservalue       // 缓存闭包与参数加速器
└─ superCall() -> funcAcc->call()                  // 之后直接走加速调用
```

[1] 关键源码：实例 self table 不是固定 table，而是运行时构造并挂 metatable

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaOverrider.cpp
// 函数: LuaOverrider::bindOverrideFuncs / setmetatable
// 位置: 1174-1256, 1295-1357
// ============================================================================
NS_SLUA::LuaVar luaModule = sluaState->requireModule(TCHAR_TO_UTF8(*luaFilePath));
if (luaModule.isFunction()) {
    luaModule = luaModule.call();
    // ★ 如果模块本身可调用，会先执行一次，得到实例 self table
}

if (luaModule.isTable()) {
    luaModule.push(L);
    if (lua_getmetatable(L, -1)) {
        if (lua_getfield(L, -1, "__call") != LUA_TNIL) {
            lua_pushvalue(L, -4);
            if (lua_pcall(L, 1, 1, top))
                lua_pop(L, 1);
            luaSelfTable = LuaVar(L, -retCount);
            // ★ table.__call(...) 也会被当成 self ctor
        }
    }
}

setmetatable(luaSelfTable, (void*)obj, bNetReplicated);
ULuaOverrider::addObjectTable(L, obj, luaSelfTable, bHookInstancedObj);
// ★ self table 最终和 UObject 实例一一绑定

lua_pushstring(L, SLUA_CPPINST);
lua_pushlightuserdata(L, objPtr);
lua_rawset(L, -3);
// ★ 把 UObject* 反向塞回 luaSelfTable，后续 __index/__newindex 都靠它回到 C++

lua_pushstring(L, SUPER_NAME);
LuaObject::pushType(L, new LuaSuperCall((UObject*)objPtr), "LuaSuperCall", LuaSuperCall::setupMetatable, LuaSuperCall::genericGC);
lua_rawset(L, -3);
// ★ self table 里显式挂一个 Super 代理对象

if (lua_getmetatable(L, -1)) {
    lua_newtable(L);
    lua_getfield(L, -2, "__index");
    lua_pushcclosure(L, classIndex, 1);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, classNewindex);
    lua_setfield(L, -2, "__newindex");
}
// ★ 后续成员查询不是只看 Lua table，而是 Lua table + UObject + 网络属性三层回退
```

[2] 关键源码：super 调用采用“克隆 UFunction + lazy cache”而不是每次反射查找

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaOverrider.cpp
// 函数: LuaOverrider::hookBpScript
// 位置: 1381-1449，为每个被覆写函数复制一个 __overrider_* 版本
// ============================================================================
auto supercallFunc = duplicateUFunction(func, cls, FName(*(SUPER_CALL_FUNC_NAME_PREFIX + func->GetName())), func->GetNativeFunc());
// ★ 原始函数先复制一份 __overrider_Func，专门保留 super 入口

if (overrideFunc == func)
{
    if (overrideFunc->HasAnyFunctionFlags(FUNC_Net) || overrideFunc->HasAnyFunctionFlags(FUNC_Native))
    {
        overrideFunc->SetNativeFunc(hookFunc);
        // ★ 原 overrideFunc 的 native 指针改挂到 luaOverrideFunc
    }
    overrideFunc->Script.Insert(Code, CodeSize, 0);
}
else if (!overrideFunc)
{
    overrideFunc = duplicateUFunction(func, cls, func->GetFName(), (FNativeFuncPtr)&ULuaOverrider::luaOverrideFunc);
    // ★ 如果子类里还没有 override，就复制出一个同名函数作为 Lua override 入口
}
```

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaOverriderSuper.cpp
// 函数: LuaSuperCall::__superIndex / __superCall / superCall
// 位置: 57-130，首次访问时解析并缓存 super 函数加速器
// ============================================================================
UFunction* func = getSuperFunction<LuaSuperCall>(L);
lua_pushlightuserdata(L, LuaFunctionAccelerator::findOrAdd(func));
lua_pushcclosure(L, __superCall, 1);
// ★ 第一次取 Super.xxx 时就把 LuaFunctionAccelerator 塞进闭包 upvalue

if (lua_getuservalue(L, 1) == LUA_TNIL)
{
    lua_newtable(L);
    lua_setuservalue(L, 1);
}
lua_pushvalue(L, 2);
lua_pushvalue(L, -3);
lua_rawset(L, -3);
// ★ 闭包会缓存在 uservalue 里，后续 Super.xxx 直接命中缓存

auto* funcAcc = (LuaFunctionAccelerator*)lua_touserdata(L, -1);
return UD->superCall(L, funcAcc);
// ★ 真正 super 调用已经切成 funcAcc->call() 快路径
```

[3] 对照源码：Angelscript 把 Blueprint 互通固定为 typed signature + 参数缓冲验证，而不是实例级 metatable 代理

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp
// 函数: FScriptCall::ValidateAgainstFunction
// 位置: 125-163，运行时首先校验参数类型与 buffer 大小
// ============================================================================
for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
{
    if (PropertyIndex >= ArgumentIndex)
    {
        OutErrorMessage = FString::Printf(TEXT("Signature mismatch while executing '%s': too few arguments were pushed."), *Function->GetName());
        return false;
    }

    if (!DoesCallArgumentMatchProperty(ArgumentTypes[PropertyIndex].Type, *It))
    {
        OutErrorMessage = FString::Printf(TEXT("Signature mismatch while executing '%s' at parameter '%s'."), *Function->GetName(), *It->GetName());
        return false;
    }
}

if (Function->ParmsSize != ArgumentOffset)
{
    OutErrorMessage = FString::Printf(TEXT("Signature mismatch while executing '%s': argument buffer size %d does not match expected parameter size %d."), *Function->GetName(), static_cast<int32>(ArgumentOffset), Function->ParmsSize);
    return false;
}
// ★ Angelscript 在调用期继续坚持 typed buffer 校验，不把实例表当作调度中心
```

差距判断：

- 混合继承链实现：`实现方式不同`。slua 把 UObject 实例接成 Lua proxy；Angelscript 把 Blueprint API 编成 typed signature。
- `Super` 调用成本控制：`实现方式不同`。slua 用 `LuaFunctionAccelerator` lazy cache；Angelscript 靠绑定期签名重建与 direct/fallback 调度。
- 运行时错误暴露位置：不适合简单判优。slua 更灵活，但更多问题在运行时 metatable/proxy 层暴露；Angelscript 更早在签名和 buffer 校验期失败。

### [维度 D4 / D11] slua 插件本体把“热更部署”严格收缩到“你给我脚本字节”，加密/签名不在插件边界内

前文已经说明 slua 的 loader contract 很轻。本轮再看打包与插件文件，能进一步确认：`FilterPlugin.ini` 为空，`make_win.bat` 只负责构建 `lua.lib / pdb`，demo loader 则直接依次读取 `.lua` 和 `.luac`。这三个证据拼起来，结论比“支持线上热更”更准确：slua 插件负责 VM、桥接和字节流执行，但并不负责热更包的签名、校验、版本清单和失败回滚协议。

因此在 D11 上不能写成“slua 有完整线上热更新系统”。更严谨的说法是：slua 暴露了足够轻的脚本装载面，方便项目侧接 CDN / 补丁系统；而 Angelscript 把重点放在自身构建产物一致性、缓存失效和 dump 审计。两者面对的是不同发布责任边界。

```
[D4/D11] Shipping Boundary
sluaunreal
├─ make_win.bat -> build/copy lua.lib               // 插件打包重点是 Lua runtime
├─ FilterPlugin.ini empty                           // 无额外脚本清单/签名产物
├─ LoadFileDelegate(fn) -> bytes                    // 外部提供字节流
└─ demo loader: .lua / .luac                        // 原始源码或字节码都直接装载

Angelscript
├─ cmdline: as-generate-precompiled-data            // 预编译产物开关
├─ cmdline: as-ignore-precompiled-data              // 缓存忽略开关
├─ load PrecompiledScript_[Config].Cache            // 约定产物命名
├─ validate build + DataGuid                        // 不匹配即丢弃
└─ DumpPrecompiledData / DumpStaticJITState         // 可审计导出
```

[1] 关键源码：slua 插件包装的是 Lua runtime，不是脚本签名/分发协议

```ini
; ============================================================================
; 文件: Reference/sluaunreal/Plugins/slua_unreal/Config/FilterPlugin.ini
; 函数: plugin packaging filter
; 位置: 1-8，未额外声明任何脚本签名、manifest 或补丁描述文件
; ============================================================================
[FilterPlugin]
; This section lists additional files which will be packaged along with your plugin.
; ★ 文件基本是空的，说明插件层没有额外定义热更产物清单
```

```bat
REM ============================================================================
REM 文件: Reference/sluaunreal/Plugins/slua_unreal/make_win.bat
REM 函数: build script
REM 位置: 1-15，脚本只负责把 Lua 静态库拷到 Library/Win32 / Win64
REM ============================================================================
cmake --build build_win32 --config RelWithDebInfo
copy /Y build_win32\RelWithDebInfo\lua.lib Library\Win32\lua.lib
copy /Y build_win32\RelWithDebInfo\lua.pdb Library\Win32\lua.pdb

cmake --build build_win64 --config RelWithDebInfo
copy /Y build_win64\RelWithDebInfo\lua.lib Library\Win64\lua.lib
copy /Y build_win32\RelWithDebInfo\lua.pdb Library\Win64\lua.pdb
REM ★ 打包脚本的关注点是 Lua runtime 二进制，不是脚本包签名/加密
```

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Source/democpp/MyGameInstance.cpp
// 函数: UMyGameInstance::CreateLuaState
// 位置: 41-63，demo loader 直接读取 .lua / .luac 原始字节
// ============================================================================
state->setLoadFileDelegate([](const char* fn, FString& filepath)->TArray<uint8> {
    FString path = FPaths::ProjectContentDir();
    path /= "Lua";
    path /= filename.Replace(TEXT("."), TEXT("/"));

    TArray<FString> luaExts = { UTF8_TO_TCHAR(".lua"), UTF8_TO_TCHAR(".luac") };
    for (auto& it : luaExts) {
        auto fullPath = path + *it;
        FFileHelper::LoadFileToArray(Content, *fullPath);
        if (Content.Num() > 0) {
            filepath = fullPath;
            return MoveTemp(Content);
        }
    }
    return MoveTemp(Content);
});
// ★ 插件接受“源码”或“字节码”两种原始文件，但这里没有任何 hash / signature / version check
```

[2] 对照源码：Angelscript 把脚本产物开关、加载和审计都收在 runtime 配置里

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: FAngelscriptEngineConfig::FromCurrentProcess / FAngelscriptEngine::Initialize
// 位置: 514-529, 1512-1556
// ============================================================================
Config.bGeneratePrecompiledData = FParse::Param(FCommandLine::Get(), TEXT("as-generate-precompiled-data"));
Config.bDevelopmentMode = FParse::Param(FCommandLine::Get(), TEXT("as-development-mode"));
Config.bIgnorePrecompiledData = FParse::Param(FCommandLine::Get(), TEXT("as-ignore-precompiled-data"));
// ★ 预编译脚本是否生成 / 忽略是 runtime 级配置，不是 demo 项目自己约定

Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript_Development.Cache");
if (!IFileManager::Get().FileExists(*Filename))
    Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript.Cache");

PrecompiledData->Load(Filename);
if (!PrecompiledData->IsValidForCurrentBuild())
{
    delete PrecompiledData;
    PrecompiledData = nullptr;
    // ★ build 不匹配立刻丢弃 cache
}

if (CompiledInfo != nullptr && CompiledInfo->PrecompiledDataGuid != PrecompiledData->DataGuid)
{
    FJITDatabase::Get().Clear();
    // ★ 连 transpiled C++ / precompiled script 的 GUID 也会交叉校验
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp
// 函数: DumpPrecompiledData / DumpStaticJITState
// 位置: 1038-1099，当前产物状态还能导出为 CSV 审计
// ============================================================================
if (Engine.PrecompiledData == nullptr)
{
    Writer.AddRow({ TEXT("NotLoaded"), TEXT("0"), TEXT("0"), TEXT("0"), FString() });
}
else
{
    Writer.AddRow({
        Engine.PrecompiledData->DataGuid.ToString(EGuidFormats::DigitsWithHyphens),
        LexToString(Engine.PrecompiledData->Modules.Num()),
        LexToString(Engine.PrecompiledData->FunctionReferences.Num()),
        LexToString(Engine.PrecompiledData->ClassesLoadedFromPrecompiledData.Num()),
        FString()
    });
}
return SaveTable(OutputDir, TEXT("PrecompiledData.csv"), Writer);
// ★ Angelscript 的部署工件不是黑盒；可以直接导出 cache / JIT 当前状态
```

差距判断：

- 脚本加密 / 签名：`没有实现`。至少在 slua 插件源码边界内，没有看到对应实现点；它把这部分责任留给项目侧分发系统。
- 线上热更装载点：`实现方式不同`。slua 提供的是轻量字节流入口，适合接外部 patch 系统；Angelscript 提供的是引擎内构建产物一致性框架。
- 发布后可审计性：`实现质量差异`。Angelscript 的 cache / JIT dump 更容易排查“线上到底加载了什么”；slua 当前插件层对这类状态没有同级产物导出。

---

## 深化分析 (2026-04-08 18:46:53)

### [维度 D5] slua 的远程通道只承载 profiler sample，不是 breakpoint / evaluate 调试协议

前两轮已经证明 slua 有 remote profiler，但继续顺着 `slua_profile` 和 `LuaProfiler` 往下看，可以更明确地划清边界：slua 的网络协议从枚举到消息体都只描述 `PHE_CALL / PHE_RETURN / PHE_TICK / memory` 这类采样事件。`FProfileServer` 收包后也只是把消息交给 `debug_hook_c()`，再落入 `WatchBegin/WatchEnd/CoroutineBegin/CoroutineEnd` 构建调用树。换句话说，slua 的“远程开发体验”核心是 attach profiler，而不是断点调试器。

这和 Angelscript 的调试面完全不同。`EDebugMessageType` 明确包含 `SetBreakpoint`、`RequestVariables`、`RequestEvaluate`、`GoToDefinition`、`CreateBlueprint` 等控制消息；`SendCallStack()` 会把 Blueprint 帧和 Angelscript 帧拼成同一条栈；`GetDebuggerValue()` 还能解析 `0:Owner.Name` 这种路径并在 Blueprint `this` 上求值。两者不能简单比较成“谁更强”，而是面向不同开发动作：slua 优先回答“哪里慢、哪里涨内存”，Angelscript 优先回答“现在停在哪、变量是什么、跳去定义在哪”。

```
[D5] Debug / Diagnose Channel
sluaunreal
Lua VM
├─ lua_sethook / memory sample                      // 采样 call/return/memory
├─ FProfileMessage {Event,Time,Line,Name,Src}      // 只传 profiler 样本
├─ FProfileServer TCP 8081                          // Editor 侧收包
└─ SProfilerInspector / console cmds               // 展示与诊断

Angelscript
Runtime Debug Server
├─ SetBreakpoint / ClearBreakpoints                // 断点控制
├─ RequestCallStack / RequestVariables             // 栈与变量查看
├─ RequestEvaluate / GoToDefinition                // 表达式求值 / 跳转定义
└─ Blueprint-aware frame resolver                  // BP + AS 混合调试
```

[1] 关键源码：slua 的事件枚举和消息结构只覆盖 profile/memory 数据

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaProfiler.h
// 函数: NS_SLUA::ProfilerHookEvent
// 位置: 19-30，远程事件类型全部是 profiler/memory/coroutine 样本
// ============================================================================
enum ProfilerHookEvent
{
    PHE_MEMORY_TICK = -2,
    PHE_TICK = -1,
    PHE_CALL = 0,
    PHE_RETURN = 1,
    PHE_LINE = 2,
    PHE_TAILRET = 4,
    PHE_MEMORY_GC = 5,
    PHE_MEMORY_INCREACE = 6,
    PHE_ENTER_COROUTINE = 7,
    PHE_EXIT_COROUTINE = 8,
};
// ★ 这里没有 breakpoint / step / evaluate / variables 一类控制语义
```

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_profile/Public/slua_remote_profile.h
// 函数: NS_SLUA::FProfileMessage
// 位置: 142-162，消息体只有时间、源码位置、函数名和内存列表
// ============================================================================
class FProfileMessage
{
public:
    int Event;
    int64 Time;

    int Linedefined;
    FString Name;
    FString ShortSrc;

    TArray<NS_SLUA::LuaMemInfo> memoryInfoList;
    TArray<NS_SLUA::LuaMemInfo> memoryIncrease;
    TArray<NS_SLUA::LuaMemInfo> memoryDecrease;
};
// ★ 远程协议承载的是“样本包”，而不是“调试命令/调试响应”
```

[2] 关键源码：slua Editor 侧收到消息后只会喂给 profiler 调用树

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_profile/Private/slua_profile.cpp
// 函数: Fslua_profileModule::OnSpawnPluginTab / debug_hook_c
// 位置: 136-140, 189-243，TCP 消息只进入 profiler inspector 管线
// ============================================================================
sluaProfilerInspector->ProfileServer = MakeShareable(new NS_SLUA::FProfileServer());
sluaProfilerInspector->ProfileServer->OnProfileMessageRecv().BindLambda([this](NS_SLUA::FProfileMessagePtr Message) {
    this->debug_hook_c(Message);
});
// ★ 收包后的唯一处理入口就是 debug_hook_c，没有调试命令分发器

if (event == NS_SLUA::ProfilerHookEvent::PHE_CALL)
{
    SluaProfilerDataManager::WatchBegin(short_src, linedefined, name, nanoseconds, funcProfilerRoot, profilerStack);
}
else if (event == NS_SLUA::ProfilerHookEvent::PHE_RETURN)
{
    SluaProfilerDataManager::WatchEnd(short_src, linedefined, name, nanoseconds, profilerStack);
}
else if (event == NS_SLUA::ProfilerHookEvent::PHE_TICK)
{
    funcProfilerNodeQueue.Enqueue(funcProfilerRoot);
    memoryQueue.Enqueue(currentMemory);
    ClearCurProfiler();
}
// ★ 所有事件最终都被折叠成调用树 / 内存帧；没有“暂停脚本执行”这类行为
```

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/SluaLib.cpp
// 函数: editor console commands
// 位置: 439-484，开发期补充诊断手段是控制台命令
// ============================================================================
static FAutoConsoleCommand CEnableRefTrace(
    TEXT("slua.EnableRefTrace"),
    TEXT("Enable [registry] tracer!"),
    FConsoleCommandDelegate::CreateStatic(toggleRefTraceEnable),
    ECVF_Cheat);

static FAutoConsoleCommand CVarDumpUObjects(
    TEXT("slua.DumpUObjects"),
    TEXT("Dump all uobject that referenced by lua in main state"),
    FConsoleCommandDelegate::CreateStatic(dumpUObjects),
    ECVF_Cheat);

static FAutoConsoleCommand CVarGC(
    TEXT("slua.GC"),
    TEXT("Collect lua garbage"),
    FConsoleCommandDelegate::CreateStatic(garbageCollect),
    ECVF_Cheat);

static FAutoConsoleCommand CVarMem(
    TEXT("slua.Mem"),
    TEXT("Print memory used"),
    FConsoleCommandDelegate::CreateStatic(memUsed),
    ECVF_Cheat);
// ★ slua 提供的是“泄漏追踪 / UObject 快照 / GC / 内存查看”诊断命令
```

[3] 对照源码：Angelscript 的 debug server 明确暴露断点、变量和求值协议

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h
// 函数: EDebugMessageType / GetDebuggerValue
// 位置: 25-80, 622-637，调试协议内建 breakpoint / variables / evaluate / goto
// ============================================================================
enum class EDebugMessageType : uint8
{
    StartDebugging,
    StopDebugging,
    Pause,
    Continue,
    RequestCallStack,
    CallStack,
    ClearBreakpoints,
    SetBreakpoint,
    RequestVariables,
    Variables,
    RequestEvaluate,
    Evaluate,
    GoToDefinition,
    CreateBlueprint,
    SetDataBreakpoints,
    ClearDataBreakpoints,
};
// ★ 这里是完整调试控制面，而不是单向 profile 事件枚举

bool GetDebuggerValue(const FString& Path, FDebuggerValue& Value, int32* InOutFrame = nullptr, TArray<FDebuggerValue>* OutInnerValues = nullptr);
bool GetDebuggerScope(const FString& Path, FDebuggerScope& Scope);
// ★ 运行时支持按路径读变量/作用域
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 函数: SendCallStack / GetDebuggerValue
// 位置: 1382-1442, 2420-2445，把 Blueprint 帧并入调用栈并支持 Blueprint this 求值
// ============================================================================
int BPFrame = Context->GetBlueprintCallstackFrame(i);
for (; BPStackIndex < BPFrame; ++BPStackIndex)
{
    UFunction* Function = StackView[BPStackIndex]->Node;
    if (Function == nullptr || IsAngelscriptGenerated(Function))
        continue;

    Frame.Name = FString::Printf(TEXT("(BP) %s"), *Function->GetDisplayNameText().ToString());
    Frame.Source = FString::Printf(TEXT("::%s"), *Function->GetOuter()->GetName());
    Stack.Frames.Insert(Frame, 0);
}
// ★ 调用栈会把 Blueprint 帧和脚本帧混在一起发给调试器

if ((Frame & FLAG_BlueprintFrame) != 0)
{
    UObject* StackFrameObject = BPStack->GetCurrentScriptStack()[BPFrame]->Object;
    auto Usage = FAngelscriptTypeUsage::FromClass(UASClass::GetFirstASOrNativeClass(StackFrameObject->GetClass()));
    if (Expr[0].Name == TEXT("this"))
    {
        if (Usage.GetDebuggerValue(Address, CurrentValue))
            bValidValue = true;
    }
    else
    {
        if (Usage.GetDebuggerMember(Address, Expr[0].Name, CurrentValue))
            bValidValue = true;
    }
}
// ★ 变量查看不是只限脚本栈；Blueprint frame 的 this/member 也能被调试器读取
```

差距判断：

- 远程协议能力：`实现方式不同`。slua 的 TCP 通道是 profiler streaming；Angelscript 的 socket 通道是调试协议。
- 断点 / 单步 / 变量 / 表达式求值：`没有实现`。至少在 slua 当前插件源码边界内，没有看到与 `SetBreakpoint` / `GetDebuggerValue` 对等的实现。
- Blueprint 混合调试：`没有实现`。slua 的 remote channel 不处理 Blueprint stack frame；Angelscript 明确把 Blueprint 帧并入调试栈。

### [维度 D7] slua 的 Editor 接入停在“两个工具壳”，Angelscript 把编辑器纳入脚本作者工作流

slua 仓库里的 Editor 接入可以分成两个小工具：`slua_profile` 负责在 `LevelEditor` 菜单里开一个 profiler tab，并在 tab 打开时启动 `FProfileServer`；`lua_wrapper` 负责在工具栏上挂一个“Generate Lua Interface”按钮，点击后直接 `system()` 调外部 `lua-wrapper.exe`。特别是 `lua_wrapper` 的 `NomadTab` 仍然是模板占位文本，说明这个插件在源码里本质上只是“外部 generator 启动器”。

Angelscript 的 Editor 模块则明显更深地插进了日常作者工作流。启动时它会注册源码导航、状态 dump 扩展、所有脚本根目录的 `DirectoryWatcher`、项目设置页、Content Browser data source，以及 debug server 到 editor 的回调桥。也就是说，Angelscript 不是“把工具挂进编辑器”，而是“让编辑器本身参与脚本 reload、资产浏览、Blueprint 创建和状态审计”。

```
[D7] Editor Integration Surface
sluaunreal
LevelEditor
├─ Window > slua Profile                           // profiler tab
├─ Toolbar > LuaWrapper                            // 手工触发外部 generator
└─ External exe / TCP listener                     // 编辑器主要扮演壳层

Angelscript
Editor Startup
├─ DirectoryWatcher on script roots                // 文件改动进入 reload 队列
├─ ContentBrowserDataSource                        // 脚本资源接入浏览器
├─ Settings / ToolMenus                            // 配置与命令入口
├─ DebugServer -> asset popup / create blueprint   // 运行时消息驱动编辑器动作
└─ StateDump extension                             // 编辑器状态可审计
```

[1] 关键源码：slua 的 editor 模块主要是开 tab 和转调外部工具

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_profile/Private/slua_profile.cpp
// 函数: Fslua_profileModule::StartupModule / AddMenuExtension
// 位置: 48-83, 177-180，只向 LevelEditor 注入菜单项和隐藏 tab
// ============================================================================
PluginCommands->MapAction(
    Flua_profileCommands::Get().OpenPluginWindow,
    FExecuteAction::CreateRaw(this, &Fslua_profileModule::PluginButtonClicked),
    FCanExecuteAction());

TSharedPtr<FExtender> MenuExtender = MakeShareable(new FExtender());
MenuExtender->AddMenuExtension("WindowLayout", EExtensionHook::After, PluginCommands, FMenuExtensionDelegate::CreateRaw(this, &Fslua_profileModule::AddMenuExtension));
LevelEditorModule.GetMenuExtensibilityManager()->AddExtender(MenuExtender);

FGlobalTabmanager::Get()->RegisterNomadTabSpawner(slua_profileTabName,
    FOnSpawnTab::CreateRaw(this, &Fslua_profileModule::OnSpawnPluginTab))
    .SetDisplayName(LOCTEXT("Flua_wrapperTabTitle", "slua Profiler"))
    .SetMenuType(ETabSpawnerMenuType::Hidden);
// ★ profiler 插件的编辑器接入点就是一个菜单入口 + 一个 tab spawner

Builder.AddMenuEntry(Flua_profileCommands::Get().OpenPluginWindow);
// ★ 没有脚本目录监听、资产浏览器或 Blueprint 工作流接线
```

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/lua_wrapper/Source/lua_wrapper/Private/lua_wrapper.cpp
// 函数: Flua_wrapperModule::OnSpawnPluginTab / PluginButtonClicked
// 位置: 74-96, 122-145，UI 是占位页，真正动作是执行外部 exe
// ============================================================================
return SNew(SDockTab)
    .TabRole(ETabRole::NomadTab)
    [
        SNew(SBox)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot()[
                SNew(STextBlock)
                .Text(WidgetText)
            ]
        ]
    ];
// ★ tab 内容仍然是模板文本，说明插件自身没有复杂编辑器面板逻辑

auto cmd = contentDir + TEXT("/../Tools/lua-wrapper.exe");
system(TCHAR_TO_UTF8(*cmd));
// ★ Windows 下直接启动外部生成器，而不是在编辑器内实现 codegen 流程

Builder.AddToolBarButton(Flua_wrapperCommands::Get().OpenPluginWindow);
// ★ 工具栏按钮只是 generator launcher
```

[2] 对照源码：Angelscript Editor 启动时会把 reload / 内容浏览 / UI 命令全挂上

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 函数: OnEngineInitDone / FAngelscriptEditorModule::StartupModule
// 位置: 111-119, 351-415，编辑器模块直接接管脚本工作流的多个入口
// ============================================================================
auto* DataSource = NewObject<UAngelscriptContentBrowserDataSource>(GetTransientPackage(), "AngelscriptData", RF_MarkAsRootSet | RF_Transient);
DataSource->Initialize();
UContentBrowserDataSubsystem* ContentBrowserData = IContentBrowserDataModule::Get().GetSubsystem();
ContentBrowserData->ActivateDataSource("AngelscriptData");
// ★ 编辑器启动完成后把脚本内容源注册进 Content Browser

RegisterAngelscriptSourceNavigation();
UScriptEditorMenuExtension::InitializeExtensions();
AngelscriptEditor::Private::RegisterStateDumpExtension(StateDumpExtensionHandle);
// ★ 导航、菜单扩展、状态 dump 都在 StartupModule 里就绪

for (const auto& RootPath : AllRootPaths)
{
    DirectoryWatcher->RegisterDirectoryChangedCallback_Handle(
        *RootPath,
        IDirectoryWatcher::FDirectoryChanged::CreateStatic(&OnScriptFileChanges),
        WatchHandle,
        IDirectoryWatcher::IncludeDirectoryChanges);
}
// ★ 所有脚本根目录都被目录监听，文件变化会进入 reload 队列

UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FAngelscriptEditorModule::RegisterToolsMenuEntries));
// ★ 编辑器主菜单也会被扩展成脚本工作流入口
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 函数: StartupModule / ShowAssetListPopup
// 位置: 396-409, 541-672，运行时调试消息还能驱动资产列表和 Blueprint 创建弹窗
// ============================================================================
FAngelscriptRuntimeModule::GetDebugListAssets().AddLambda(
    [](TArray<FString> AssetPaths, UASClass* BaseClass)
    {
        FAngelscriptEditorModule::ShowAssetListPopup(AssetPaths, BaseClass);
    }
);

FAngelscriptRuntimeModule::GetEditorCreateBlueprint().AddLambda(
    [](UASClass* ScriptClass)
    {
        FAngelscriptEditorModule::ShowCreateBlueprintPopup(ScriptClass);
    }
);
// ★ runtime debug server 发来的消息，可以直接驱动 editor 侧资产/Blueprint 动作
```

差距判断：

- 编辑器接入深度：`实现方式不同`。slua 以 profiler 和 codegen launcher 两个工具壳为主；Angelscript 把编辑器本身纳入脚本作者闭环。
- 文件变更监听与自动 reload 接线：`没有实现`。在 slua 当前 editor 模块源码里未见与 `DirectoryWatcher` 对等的实现。
- Content Browser / Blueprint 资产工作流：`没有实现`。slua 现有 editor 代码没有内容源注册和 Blueprint 创建桥；Angelscript 已形成成体系接线。
- codegen 工具集成质量：`实现质量差异`。slua 的 `lua_wrapper` 更像外部 exe 启动器；Angelscript 的 editor 扩展和 runtime/editor 回调桥耦合更紧。

### [维度 D6] slua 的 codegen 面向 wrapper 产物，Angelscript 还把“跳转定义”与 IDE 工作区串进去了

把 `lua_wrapper` 单独当 D6 看，能看到 slua 的代码生成与 IDE 支持重点并不在同一个环上。`lua_wrapper` 模块的命令文案就是 `Generate Lua Interface (Windows only)`，点击后直接执行 `../Tools/lua-wrapper.exe`；运行时再去消费生成好的 `LuaWrapper5.x.inc`。这说明 slua 的 codegen 目标是“吐出可编译的桥接代码”，而不是给 IDE 提供类型声明、跳转定义或符号数据库。

Angelscript 在这一维多走了一步。Editor 启动时注册 `FAngelscriptSourceCodeNavigation`，它能把 `UASClass / UASFunction / UASStruct` 直接 `code --goto` 到脚本文件；`RegisterToolsMenuEntries()` 还会在 `Tools` 菜单里加一个 “Open Angelscript workspace (VS Code)”；更底层的 `FAngelscriptBindDatabase::Save()` 会把绑定类型对应的 header 路径序列化到 `.Headers`，让 native symbol 也有稳定可追踪的源文件路径。再加上 debug server 的 `GoToDefinition()`，Angelscript 已经形成“脚本符号 -> UE 类型 -> 源文件”的 IDE 闭环。

```
[D6] Codegen and IDE Loop
sluaunreal
Editor button
├─ LuaWrapper command                              // 手工触发
├─ system("../Tools/lua-wrapper.exe")             // 外部进程生成
└─ LuaWrapper5.x.inc consumed by runtime          // 目标是 wrapper 产物

Angelscript
Editor/runtime metadata
├─ RegisterAngelscriptSourceNavigation            // IDE 跳转处理器
├─ Open Angelscript workspace (VS Code)           // 工作区入口
├─ Binds.Cache.Headers                            // 绑定符号 -> 头文件
└─ DebugServer::GoToDefinition                    // 调试器内跳转定义
```

[1] 关键源码：slua 的 D6 入口是一个 editor-only generator launcher

```json
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/lua_wrapper/lua_wrapper.uplugin
// 函数: plugin descriptor
// 位置: 16-21，lua_wrapper 本身就是独立的 Editor-only 插件
// ============================================================================
"Modules": [
  {
    "Name": "lua_wrapper",
    "Type": "Editor",
    "LoadingPhase": "Default"
  }
]
// ★ 这个模块不参与 runtime，只负责编辑器侧生成动作
```

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/lua_wrapper/Source/lua_wrapper/Private/lua_wrapperCommands.cpp
// 函数: Flua_wrapperCommands::RegisterCommands
// 位置: 7-10，命令本身就是“生成 Lua 接口”
// ============================================================================
UI_COMMAND(OpenPluginWindow, "LuaWrapper", "Generate Lua Interface (Windows only)", EUserInterfaceActionType::Button, FInputGesture());
// ★ 入口语义是 codegen，不是 IDE 导航或补全
```

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/lua_wrapper/Source/lua_wrapper/Private/lua_wrapper.cpp
// 函数: Flua_wrapperModule::PluginButtonClicked
// 位置: 122-135，点击后直接执行外部生成器
// ============================================================================
#ifdef _MSC_VER
auto contentDir = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*FPaths::ProjectContentDir());
auto cmd = contentDir + TEXT("/../Tools/lua-wrapper.exe");
system(TCHAR_TO_UTF8(*cmd));
#else
auto ret = exec("/Library/Frameworks/Mono.framework/Versions/Current/Commands/mono lua-wrapper.exe");
GEngine->AddOnScreenDebugMessage(-1, 30.0f, FColor::Red, ret.c_str());
#endif
// ★ codegen 通过外部进程完成；源码里看不到同级别的 symbol navigation / declaration export
```

[2] 对照源码：Angelscript 显式注册 IDE 导航和工作区入口

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp
// 函数: FAngelscriptSourceCodeNavigation / RegisterAngelscriptSourceNavigation
// 位置: 6-24, 34-44, 53-65, 96-116, 136-138
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
// ★ Script function 可以直接带行号跳到脚本文件

void OpenModule(TSharedPtr<FAngelscriptModuleDesc> Module, int LineNo = -1)
{
    FString Path = Module->Code[0].AbsoluteFilename;
    FPlatformMisc::OsExecute(nullptr, TEXT("code"), *FString::Printf(TEXT("--goto \"%s:%d\""), *Path, LineNo));
}
// ★ 具体实现就是把 UE 导航请求转成 VS Code 的 --goto

void RegisterAngelscriptSourceNavigation()
{
    FSourceCodeNavigation::AddNavigationHandler(new FAngelscriptSourceCodeNavigation);
}
// ★ 这是 slua 现有 editor 模块里没有看到的 IDE 导航挂载点
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 函数: RegisterToolsMenuEntries
// 位置: 700-720，把 VS Code 工作区入口放进主菜单
// ============================================================================
UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("MainFrame.MainMenu.Tools");
FToolMenuSection& Section = Menu->FindOrAddSection("Programming");

FToolUIActionChoice Action(FExecuteAction::CreateLambda([]()
{
    const FString ScriptPath = FPaths::ProjectDir() / TEXT("Script");
    FPlatformMisc::OsExecute(nullptr, TEXT("code"), *FString::Printf(TEXT("\"%s\""), *ScriptPath));
}));

Section.AddMenuEntry(
    "ASOpenCode",
    NSLOCTEXT("Angelscript", "OpenCode.Label", "Open Angelscript workspace (VS Code)"),
    NSLOCTEXT("Angelscript", "OpenCode.ToolTip", "Opens Visual Studio Code in this project's Angelscript workspace"),
    FSourceCodeNavigation::GetOpenSourceCodeIDEIcon(),
    Action
);
// ★ 不只支持跳转定义，还提供脚本工作区入口
```

[3] 对照源码：Angelscript 还维护 native 符号到头文件的可追踪映射，并把它接到 GoToDefinition

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp
// 函数: FAngelscriptBindDatabase::Save
// 位置: 60-99，保存 Binds.Cache.Headers 记录绑定符号到头文件路径
// ============================================================================
for (auto& Bind : Classes)
{
    UClass* Class = FindObject<UClass>(nullptr, *Bind.UnrealPath);
    FString HeaderPath;
    if (FSourceCodeNavigation::FindClassHeaderPath(Class, HeaderPath) && IFileManager::Get().FileSize(*HeaderPath) != INDEX_NONE)
        Headers.Add(FAngelscriptClassHeader{Bind.UnrealPath, HeaderPath});
}

for (UEnum* Enum : BoundEnums)
{
    FString HeaderPath;
    if (FSourceCodeNavigation::FindClassHeaderPath(Enum, HeaderPath) && IFileManager::Get().FileSize(*HeaderPath) != INDEX_NONE)
        Headers.Add(FAngelscriptClassHeader{Enum->GetPathName(), HeaderPath});
}

FFileHelper::SaveArrayToFile(HeaderData, *(Path + TEXT(".Headers")));
// ★ 脚本绑定数据库额外保存了一份“符号 -> 头文件”映射，供后续导航/预编译链使用
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 函数: FAngelscriptDebugServer::GoToDefinition
// 位置: 1288-1369，把调试器请求继续映射到函数/属性/类型导航
// ============================================================================
if (ScriptFunction != nullptr)
{
    UFunction* UnrealFunction = FAngelscriptDocs::LookupAngelscriptFunction(ScriptFunction->GetId());
    if (UnrealFunction != nullptr)
    {
        FSourceCodeNavigation::NavigateToFunction(UnrealFunction);
        return;
    }
}

if (Property != nullptr)
{
    FSourceCodeNavigation::NavigateToProperty(Property);
    return;
}

if (AssociatedClass != nullptr)
{
    FSourceCodeNavigation::NavigateToClass(AssociatedClass);
    return;
}
// ★ “跳转定义”不是单独功能，而是绑定数据库、类型系统和 IDE 导航共同完成的闭环
```

差距判断：

- 代码生成触发点：`实现方式不同`。slua 用 editor 按钮手工拉起外部 exe；Angelscript 的主要生成链已收进 UHT tool 与 editor/runtime 元数据体系。
- IDE 跳转定义 / 工作区接入：`没有实现`。在本轮检查到的 slua editor 源码里，没有发现与 `RegisterAngelscriptSourceNavigation` / `GoToDefinition` 对等的实现。
- native 符号可追踪性：`实现质量差异`。Angelscript 会把绑定符号额外落成 `.Headers` 映射，方便导航与预编译使用；slua 当前 codegen 主要产出 wrapper 代码本身。

---

## 深化分析 (2026-04-08 18:57:24)

### [维度 D2 / D8] slua 把首轮反射成本缓存进 Lua 堆；Angelscript 把成本前移到 bind / JIT 阶段

前文已经证明 slua 不是“纯动态反射”。继续往热路径里钻，会看到它的核心优化并不只在 `LuaFunctionAccelerator`，还在 Lua 堆里的二级缓存。`LuaClass.inl` 的 `class_index` 找到继承链成员后会直接 `rawset(t, k, ret)`；`LuaObject::push(UFunction*)` 创建闭包时会把 `LuaFunctionAccelerator*` 塞进 upvalue，并立刻把闭包写回 userdata 的 uservalue 或 metatable；`fastIndex()` 下次命中缓存后，甚至可以直接从 C closure 的 upvalue 里取出 `FProperty*` 和 pusher，跳过再次查 `classMap.findProp()`。

这说明 slua 的真实性能策略是三段式：静态 `LuaWrapper*.inc` 先压掉一批基础 wrapper 成本，第一次运行时查找再把 `UFunction` / `FProperty` 解析结果缓存进 Lua 侧对象表，之后热路径主要变成“闭包复用 + 参数 checker 复用”。Angelscript 则把更多成本前移到绑定期和 JIT 判定期。`BindBlueprintCallable()` 直接读取 `Entry->FuncPtr` 并注册 `BindMethodDirect`；`Bind_UStruct.cpp` 又只在 `StaticJIT` 能证明脚本 POD 类型“不会变形”时生成 `CppForm`。两者都在追求热路径更短，但 slua 偏向“首次 miss 后 memoize”，Angelscript 偏向“尽量在运行前决定直接调用形态”。

```
[D2/D8] Lookup Cost Placement
sluaunreal
├─ class_index() -> rawset(t, k, ret)             // Lua 类成员查找后写回表缓存
├─ push(UFunction*) -> LuaFunctionAccelerator     // UFunction 参数布局只解析一次
├─ cacheFunction()/uservalue                       // 闭包与 property pusher 缓存进 Lua 对象
└─ fastIndex() reads cached C closure              // 二次访问绕过大部分反射扫描

Angelscript
├─ FunctionTable -> Entry->FuncPtr                // 绑定前已拿到 native pointer
├─ BindMethodDirect / BindGlobalFunction          // 注册期确定调用方式
└─ StaticJIT CppForm guard                        // 只对“类型稳定”场景生成更直接本地形态
```

[1] 关键源码：slua 的 Lua 类查找与 `UFunction` 闭包都带缓存回写

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaClass.inl
// 函数: LuaClassSource::class_index
// 位置: 21-47，继承链查找命中后直接写回 Lua table
// ============================================================================
local class_index = function (t, k, cache)
    local impl = classImplement
    local ret
    while impl do
        ret = impl[k]
        if ret ~= nil then
            if cache ~= false then
                rawset(t, k, ret)
            end
            return ret
        end
        impl = impl.__super_impl
    end
    return nil
end
// ★ 第一次查到成员后就写回 table；后续访问不再重走 super 链
```

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaObject.cpp
// 函数: cacheFunction / classIndex / LuaObject::push
// 位置: 671-709, 784-801, 3062-3071
// ============================================================================
void cacheFunction(lua_State* L, UClass* cls)
{
    if (lua_getuservalue(L, 1) == LUA_TNIL) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_setuservalue(L, 1);
    }
    lua_pushvalue(L, 2);
    lua_pushvalue(L, -3);
    lua_rawset(L, -3);
    // ★ 把刚生成的函数闭包写回 userdata/uservalue；下次同名访问直接命中
}

int classIndex(lua_State* L) {
    if (lua_getuservalue(L, 1) != LUA_TNIL) {
        lua_pushvalue(L, 2);
        if (lua_rawget(L, -2)) {
            return 1;
        }
    }

    UFunction* func = cls->FindFunctionByName(ANSI_TO_TCHAR(name));
    if (func) {
        return LuaObject::push(L, func, cls);
    }
    return searchExtensionMethod(L, cls, name, true);
    // ★ 只有缓存 miss 时才去 FindFunctionByName / extension map
}

int LuaObject::push(lua_State* L, UFunction* func, UClass* cls)  {
    lua_pushlightuserdata(L, LuaFunctionAccelerator::findOrAdd(func));
    lua_pushlightuserdata(L, cls);
    lua_pushcclosure(L, ufuncClosure, 2);
    cacheFunction(L, cls);
    return 1;
    // ★ 闭包 upvalue 里直接挂加速器指针，随后马上把闭包缓存回 Lua 对象
}
```

[2] 关键源码：slua 的 property fast path 会直接复用缓存闭包里的 `FProperty*`

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaObject.cpp
// 函数: LuaObject::fastIndex
// 位置: 1001-1045，缓存命中后直接从 C closure upvalue 取 property/pusher
// ============================================================================
if (lua_getmetatable(L, -1)) 
{
    lua_pushvalue(L, 2);
    int cacheType = lua_rawget(L, -2);
    if (cacheType == LUA_TFUNCTION) {
        CClosure* f = clCvalue(v);
        auto prop = (FProperty*)pvalue(&f->upvalue[0]);
        void* pusher = pvalue(&f->upvalue[1]);
        bool bReferencePusher = !l_isfalse(&f->upvalue[3]);
        if (bReferencePusher)
        {
            return pushReferenceAndCache((ReferencePusherPropertyFunction)pusher, L,
                                         prop->GetOwnerClass(), prop,
                                         parent + prop->GetOffset_ForInternal(), parentAddress, InvalidReplicatedIndex);
        }
        else
        {
            return ((PushPropertyFunction)pusher)(L, prop, parent + prop->GetOffset_ForInternal(), 0, nullptr);
        }
    }
}
// ★ 命中缓存后不再重新解析 property 类型，只执行已缓存的 pusher
```

[3] 对照源码：Angelscript 直接在绑定期消费 native pointer，并在 JIT 前先判定类型稳定性

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp
// 函数: BindBlueprintCallable
// 位置: 72-146
// ============================================================================
auto* DirectNativePointer = &Entry->FuncPtr;
const bool bHasDirectNativePointer = DirectNativePointer != nullptr && DirectNativePointer->IsBound();
if (!bHasDirectNativePointer)
{
    if (!BindBlueprintCallableReflectiveFallback(InType, Function, Signature, *Entry))
        return;
    return;
}

FMemory::Memcpy(&ASFuncPtr, DirectNativePointer, sizeof(asSFuncPtr));

int FunctionId = FAngelscriptBinds::BindMethodDirect
(
    InType->GetAngelscriptTypeName(),
    Signature.Declaration, ASFuncPtr, asCALL_THISCALL, Entry->Caller
);
// ★ native pointer 在绑定阶段就已经确定；运行时不需要再做 Lua 式闭包回写缓存
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UStruct.cpp
// 函数: FUStructType::GetCppForm
// 位置: 623-642，仅在类型形态稳定时才给出 StaticJIT 可消费的 C++ 形态
// ============================================================================
if (ObjectType != nullptr && (ObjectType->flags & asOBJ_POD) != 0
    && FAngelscriptEngine::Get().StaticJIT != nullptr
    && !FAngelscriptEngine::Get().StaticJIT->IsTypePotentiallyDifferent(ObjectType)
    && ObjectType->GetFirstMethod("opAssign") == nullptr
)
{
    OutCppForm.CppType = FString::Printf(TEXT("TScriptPODStruct<%d,%d>"), GetValueSize(Usage), GetValueAlignment(Usage));
    return true;
}
// ★ Angelscript 宁可放弃部分 JIT 直达，也要先确认脚本类型不会在 reload 后变形
```

差距判断：

- 热路径降本策略：`实现方式不同`。slua 更依赖“首次命中后缓存到 Lua 堆”，Angelscript 更依赖“绑定期 direct pointer + JIT 前置判定”。
- 运行时分支稳定性：`实现质量差异`。有 direct pointer 的 Angelscript 热路径更可预测；slua 则保留更多“首次 miss -> 缓存”的运行时分支，换来更动态的扩展表面积。
- 类型变化容忍方式：`实现方式不同`。slua 倾向继续走运行时桥接并缓存；Angelscript 在 `IsTypePotentiallyDifferent()` 场景下主动放弃更激进的 JIT 本地形态。

### [维度 D3 / D4] slua 的“热”更多是对象后加载接管，不是模块事务式重载

如果把视角从“脚本文件变了没有”转到“对象何时真正被 Lua 接管”，slua 的机制会更清楚。`LuaOverrider` 构造时就给 `GUObjectArray` 注册 create/delete listener，并挂上 `FCoreDelegates::OnAsyncLoadingFlushUpdate`。对象一创建就会先走 `tryHook()`；如果对象还在 `RF_NeedPostLoad` / `RF_NeedInitialization` 阶段，slua 不会硬绑，而是先塞进 `asyncLoadedObjects`，等异步加载 flush 时再 `bindOverrideFuncs()`。

这条链路回答了一个经常被混淆的问题：slua 的 Blueprint/Lua 交互确实是“动态接管”，但接管单位是 `UObject` 生命周期，不是脚本模块事务。它解决的是“对象成熟后何时安全挂 Lua override”，不是“改了脚本后整批类和函数如何原子替换”。Angelscript 正好相反。`CompileModules()` 在真正 swap 前要先过 `ClassGenerator.Setup()`，然后根据 `SoftReload / FullReloadSuggested / FullReloadRequired` 决定是否替换、是否延后、是否保留旧代码。

```
[D3/D4] Reload Scope
sluaunreal
UObject created
├─ tryHook(obj, false)                            // 创建时先尝试挂钩
├─ if RF_NeedPostLoad -> asyncLoadedObjects       // 未完成后加载就先排队
├─ OnAsyncLoadingFlushUpdate                      // 等引擎确认对象成熟
└─ bindOverrideFuncs(obj, cls)                    // 再把 Blueprint/Lua override 接上

Angelscript
Changed script files
├─ CheckForHotReload()                            // 消费变更队列
├─ CompileModules()                               // 先整体编译
├─ ClassGenerator::Setup()                        // 决定 soft/full reload 档位
└─ swap or keep old modules                       // 以模块事务为单位提交/回退
```

[1] 关键源码：slua 把 override 绑定挂在对象创建与后加载完成两个生命周期点上

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaOverrider.cpp
// 函数: LuaOverrider ctor / NotifyUObjectCreated / tryHook / onAsyncLoadingFlushUpdate
// 位置: 533-548, 595-603, 662-703, 976-1013
// ============================================================================
LuaOverrider::LuaOverrider(NS_SLUA::LuaState* luaState)
{
    GUObjectArray.AddUObjectDeleteListener(this);
    GUObjectArray.AddUObjectCreateListener(this);
    asyncLoadingFlushUpdateHandle = FCoreDelegates::OnAsyncLoadingFlushUpdate.AddRaw(this, &LuaOverrider::onAsyncLoadingFlushUpdate);
    // ★ 运行时一启动就盯住 UObject 创建/销毁和异步加载 flush
}

void LuaOverrider::NotifyUObjectCreated(const UObjectBase* Object, int32 Index)
{
    if (!obj->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
    {
        tryHook(obj, false);
    }
    // ★ 新对象出现时先尝试接管，而不是等脚本侧主动轮询
}

bool LuaOverrider::tryHook(const UObjectBaseUtility* obj, bool bHookImmediate, bool bPostLoad)
{
    if (IsInGameThread() && !bPostLoad)
    {
        if (!obj->HasAnyFlags(RF_NeedPostLoad) || bHookImmediate)
        {
            bindOverrideFuncs(obj, cls);
            return true;
        }
    }

    FScopeLock lock(&asyncLoadedObjectCS);
    asyncLoadedObjects.Add(AsyncLoadedObject{ (UObject*)obj });
    return false;
    // ★ 还没 post-load 完成的对象先排队，避免在对象未成熟时绑定 Lua override
}

void LuaOverrider::onAsyncLoadingFlushUpdate()
{
    if (obj && !obj->HasAnyFlags(RF_NeedPostLoad) && !obj->HasAnyFlags(RF_NeedInitialization))
    {
        bindOverrideFuncs(obj, cls);
    }
    else if (obj)
    {
        asyncLoadedObjects[newIndex] = objInfo;
        newIndex++;
    }
    // ★ flush 时只处理“已经可安全访问”的对象，其余对象继续留队
}
```

[2] 对照源码：Angelscript 把 reload 做成“编译成功后再整体提交”的事务

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h
// 函数: FAngelscriptClassGenerator declarations
// 位置: 21-58
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

    EReloadRequirement Setup();
    void PerformFullReload();
    void PerformSoftReload();
    bool WantsFullReload(TSharedRef<FAngelscriptModuleDesc> Module);
    bool NeedsFullReload(TSharedRef<FAngelscriptModuleDesc> Module);
    // ★ reload 档位是显式状态机，不是对象成熟后就地接管
};
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: FAngelscriptEngine::CompileModules
// 位置: 3878-4005
// ============================================================================
if (bHadCompileErrors)
{
    UE_LOG(Angelscript, Error, TEXT("Hot reload failed due to script compile errors. Keeping all old script code."));
    bShouldSwapInModules = false;
}

auto ReloadReq = ClassGenerator.Setup();
switch (ReloadReq)
{
    case FAngelscriptClassGenerator::EReloadRequirement::SoftReload:
        SwapInModules(CompiledModules, DiscardedModules);
        ClassGenerator.PerformSoftReload();
        break;
    case FAngelscriptClassGenerator::EReloadRequirement::FullReloadRequired:
        if (CompileType == ECompileType::SoftReloadOnly)
        {
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
// ★ 只有整批模块与类生成都满足条件时才提交；否则明确保留旧代码
```

差距判断：

- Lua / Blueprint 接管时机：`实现方式不同`。slua 的核心是对象生命周期挂钩；Angelscript 的核心是脚本模块与类图事务式替换。
- 运行中对象安全性：`实现质量差异`。slua 通过 `RF_NeedPostLoad` / `RF_NeedInitialization` 检查避开了“对象未成熟就绑定”的风险；Angelscript 则更进一步，把整个 reload 成败和对象替换都纳入统一提交边界。
- 模块级失败回退：`没有实现`。在 slua 当前挂钩链路中，没看到与 `Keeping all old script code` 对等的模块事务回退语义，因为它处理的本来就不是同一层问题。

### [维度 D4 / D11] slua 的线上入口真正插在 `package.searchers[2]`，补丁系统只需替换模块字节来源

前文已经说过 slua 暴露 `LoadFileDelegate`。更细一层的关键点在 `LuaState::init()`：它不是等项目代码显式调用 `doFile()`，而是直接把自定义 `loader` 插进 `package.searchers[2]`。这意味着从脚本作者视角看，仍然是普通 `require("Foo.Bar")`；但从运行时解析顺序看，slua 会优先询问项目侧的 bytes provider，再决定是否落回默认 Lua loader。

这个设计对线上补丁很重要，因为它把“部署入口”从调用点转移到了模块解析器。项目侧如果要接加密、CDN、差分包，理论上只要替换 `LoadFileDelegate` 背后的数据源，而不必重写所有脚本调用点。与此同时，slua 仍然把模块缓存交给 Lua 标准 `require` 处理，因此版本淘汰、`package.loaded` 失效、签名校验依旧在插件边界之外。Angelscript 则没有这种 searcher 层插桩；它的热更新入口是“文件变更队列 -> 依赖扩散 -> 编译事务”，部署粒度天然是显式文件集和模块图。

```
[D4/D11] Module Resolution Entry
sluaunreal
require("Foo.Bar")
├─ package.searchers[2] = LuaState::loader        // 优先接管模块解析
├─ LoadFileDelegate("Foo.Bar") -> bytes           // 项目侧决定字节来源
├─ luaL_loadbuffer("@real/filepath")              // 以真实路径标记 chunk
└─ standard Lua require cache                     // 仍复用 Lua 原生模块缓存

Angelscript
Queued file changes
├─ CheckForHotReload()                            // 消费变更队列
├─ dependency expansion                           // 把依赖模块一起找出来
├─ CompileModules()                               // 重新编译受影响模块
└─ swap modules on success                        // 成功后才提交新模块图
```

[1] 关键源码：slua 直接改写 `package.searchers`，把项目 loader 放到标准 `require` 前面

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaState.cpp
// 函数: LuaState::loader / LuaState::init
// 位置: 131-155, 603-616
// ============================================================================
int LuaState::loader(lua_State* L) {
    const char* fn = lua_tostring(L, 1);
    FString filepath;
    TArray<uint8> buf = state->loadFile(fn, filepath);
    if (buf.Num() > 0) {
        snprintf(chunk, 256, "@%s", TCHAR_TO_UTF8(*filepath));
        if (luaL_loadbuffer(L, (const char*)buf.GetData(), buf.Num(), chunk) == 0) {
            return 1;
        }
    }
    return 0;
}

lua_pushcfunction(L, loader);
lua_getglobal(L, "package");
lua_getfield(L, -1, "searchers");
for (int i = lua_rawlen(L, loaderTable) + 1; i > 2; i--) {
    lua_rawgeti(L, loaderTable, i - 1);
    lua_rawseti(L, loaderTable, i);
}
lua_pushvalue(L, loaderFunc);
lua_rawseti(L, loaderTable, 2);
// ★ 自定义 loader 被插到 searchers[2]；以后 require() 会先走项目注入的字节源
```

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Source/democpp/MyGameInstance.cpp
// 函数: UMyGameInstance::CreateLuaState
// 位置: 41-63
// ============================================================================
state = new NS_SLUA::LuaState("SLuaMainState", this);
state->setLoadFileDelegate([](const char* fn, FString& filepath)->TArray<uint8> {
    FString path = FPaths::ProjectContentDir();
    path /= "Lua";
    path /= filename.Replace(TEXT("."), TEXT("/"));

    TArray<FString> luaExts = { UTF8_TO_TCHAR(".lua"), UTF8_TO_TCHAR(".luac") };
    for (auto& it : luaExts) {
        auto fullPath = path + *it;
        FFileHelper::LoadFileToArray(Content, *fullPath);
        if (Content.Num() > 0) {
            filepath = fullPath;
            return MoveTemp(Content);
        }
    }
    return MoveTemp(Content);
});
// ★ demo 证明 searcher 前置后，项目只需定义“模块名 -> 字节数组”的映射
```

[2] 对照源码：Angelscript 的入口是变更文件队列和模块依赖图，不是模块 searcher

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: PerformHotReload / CheckForHotReload
// 位置: 2253-2368, 2729-2770
// ============================================================================
TArray<FFilenamePair> FileList;
FileList.Append(InReloadFiles);

TSet<FFilenamePair> FilesToHotReload;
if (FileList.Num() > 0)
{
    TMap<FString, FAngelscriptModuleDesc*> RelativeFileToModule;
    for (auto& Module : ActiveModules)
    {
        for (const auto& Section : Module.Value->Code)
            RelativeFileToModule.Add(Section.RelativeFilename, &(Module.Value.Get()));
    }

    for (auto& File : FileList)
    {
        if (auto* ModulePtr = RelativeFileToModule.Find(File.RelativePath))
        {
            MarkedModules.Add((asCModule*)((*ModulePtr)->ScriptModule));
        }
        else
        {
            FilesToHotReload.Add(File);
        }
    }
    // ★ reload 入口是“变更文件 -> 受影响模块”，不是运行时接管脚本解析顺序
}

FileList.Append(FileChangesDetectedForReload);
if (FileList.Num() != 0)
{
    PerformHotReload(CompileType, FileList);
}
// ★ 由 watcher/checker thread 产生显式文件队列，再进入编译事务
```

差距判断：

- 线上补丁插入点：`实现方式不同`。slua 把入口插在 `require()` 的 searcher 链；Angelscript 把入口放在脚本文件变更与编译事务上。
- 模块缓存失效策略：`没有实现`。slua 当前插件层没有对 `package.loaded` 做同级别治理；项目侧若做 runtime patch，仍需自己决定如何逐模块失效与回滚。
- 部署责任边界：`实现方式不同`。slua 更适合被外部 patch 系统“喂字节”；Angelscript 更适合在引擎内维护开发期脚本资产的一致性与依赖图。

---

## 深化分析 (2026-04-08 19:16:17)

### [维度 D2] `LuaWrapper*.inc` 的版本化静态导出，本质是“编译期分叉 + struct 白名单快路径”

前文已经说明 slua 采用“静态导出 + 动态反射”的混合绑定。本轮继续下钻后可以看到，这个“混合”并不是均匀落在全部类型上，而是先把引擎版本差异前移到 C++ 编译阶段，再把运行时快路径集中到少量白名单 `UScriptStruct`。`LuaWrapper.cpp` 直接按 UE 次版本选择不同 `.inc`，而 `initExt()` 只是 `init(L)` 加一个 `FSoftObjectPtrWrapper::bind(L)`；真正的 struct 快路径由生成文件内的 `_pushStructMap / _checkStructMap` 决定。

更关键的是，`LuaWrapper5.4.inc` 里登记进 map 的只有 `FIntPoint`、`FIntVector`、`FLinearColor`、`FRandomStream`、`FGuid`、`FFallbackStruct` 等少数基础类型。`pushValue()` / `checkValue()` 如果查不到条目会直接返回 `0` 或 `nullptr`；而 `FFallbackStructWrapper::__ctor()` 又会直接 `luaL_error`。这说明 slua 的静态导出重点是“把高频基础 struct 做成专用快路径”，不是“给任意 struct 提供同等级的生成式桥接层”。

Angelscript 的策略正好相反。`Bind_UStruct.cpp` 先统一把 struct 注册成 `FUStructType`，然后只在 `GetCppForm()` 判断为 `POD + StaticJIT 认为布局稳定 + 没有自定义 opAssign` 时，才把脚本值升级为 `TScriptPODStruct<>` 之类的 native C++ form。换句话说，slua 是“先分版本和白名单，再享受零散快路径”；Angelscript 是“先反射全覆盖，再对稳定类型做 opt-in 原生布局”。

```text
[D2] Binding Shape
sluaunreal
UE minor version
├─ LuaWrapper.cpp 选择 LuaWrapper5.x.inc
├─ initExt() -> init(L) + 个别扩展 bind
├─ _pushStructMap/_checkStructMap
│  ├─ 命中白名单 struct -> 专用 push/check 函数
│  └─ 未命中 -> 返回 0/nullptr
└─ FFallbackStructWrapper::__ctor -> 直接报错

Angelscript
遍历 U/Script struct
├─ BindStructType(TypeName, Struct, BindFlags)
├─ Register(FUStructType)
├─ 默认走统一反射表示
└─ GetCppForm() 满足稳定布局条件时
   └─ StaticJIT 使用 TScriptPODStruct<> 原生形态
```

[1] 关键源码：slua 把版本差异硬编码到 `.inc` 选择，运行时扩展层很薄

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaWrapper.cpp
// 函数: include selection / LuaWrapper::initExt
// 位置: 55-66, 184-188
// ============================================================================
#if ((ENGINE_MINOR_VERSION<25) && (ENGINE_MAJOR_VERSION==4))
    #include "LuaWrapper4.18.inc"
#elif ((ENGINE_MINOR_VERSION>=25) && (ENGINE_MAJOR_VERSION==4))
    #include "LuaWrapper4.25.inc"
#elif ((ENGINE_MINOR_VERSION==1) && (ENGINE_MAJOR_VERSION==5))
    #include "LuaWrapper5.1.inc"
#elif ((ENGINE_MINOR_VERSION==2) && (ENGINE_MAJOR_VERSION==5))
    #include "LuaWrapper5.2.inc"
#elif ((ENGINE_MINOR_VERSION==3) && (ENGINE_MAJOR_VERSION==5))
    #include "LuaWrapper5.3.inc"
#elif ((ENGINE_MINOR_VERSION==4) && (ENGINE_MAJOR_VERSION==5))
    #include "LuaWrapper5.4.inc"
#endif
// ★ 绑定代码先按 UE 次版本分叉；不是单一运行时反射层跨版本消化差异

void LuaWrapper::initExt(lua_State* L)
{
    init(L);
    FSoftObjectPtrWrapper::bind(L);
}
// ★ 扩展层只追加少量 wrapper，主绑定逻辑仍在生成的 .inc 内
```

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaWrapper5.4.inc
// 函数: FFallbackStructWrapper::__ctor / LuaWrapper::pushValue / LuaWrapper::checkValue / LuaWrapper::init
// 位置: 4166-4172, 16873-16947
// ============================================================================
static int __ctor(lua_State* L) {
    auto argc = lua_gettop(L);
    luaL_error(L, "call FFallbackStruct() error, argc=%d", argc);
    return 0;
}
// ★ fallback wrapper 不是“通用 struct 兜底实现”，而是显式拒绝构造

int LuaWrapper::pushValue(lua_State* L, FStructProperty* p, UScriptStruct* uss, uint8* parms, int i) {
    auto vptr = _pushStructMap.Find(uss);
    if (vptr != nullptr) {
        (*vptr)(L, p, parms, i);
        return 1;
    } else {
        return 0;
    }
}

void* LuaWrapper::checkValue(lua_State* L, FStructProperty* p, UScriptStruct* uss, uint8* parms, int i) {
    auto vptr = _checkStructMap.Find(uss);
    if (vptr != nullptr) {
        return (*vptr)(L, p, parms, i);
    } else {
        return nullptr;
    }
}
// ★ 快路径只有 map 命中时才成立；未命中时直接交还上层处理

FIntPointStruct = StaticGetBaseStructureInternal(TEXT("IntPoint"));
_pushStructMap.Add(FIntPointStruct, __pushFIntPoint);
_checkStructMap.Add(FIntPointStruct, __checkFIntPoint);

FLinearColorStruct = StaticGetBaseStructureInternal(TEXT("LinearColor"));
_pushStructMap.Add(FLinearColorStruct, __pushFLinearColor);
_checkStructMap.Add(FLinearColorStruct, __checkFLinearColor);

FFallbackStructStruct = StaticGetBaseStructureInternal(TEXT("FallbackStruct"));
_pushStructMap.Add(FFallbackStructStruct, __pushFFallbackStruct);
_checkStructMap.Add(FFallbackStructStruct, __checkFFallbackStruct);
// ★ 本轮证据能看到的是“白名单基础 struct 集合”，不是任意 struct 的自动静态导出
```

[2] 对照源码：Angelscript 先统一注册 struct，再条件性给 StaticJIT 暴露 native form

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UStruct.cpp
// 函数: GetCppForm / BindStructType / BindStructTypes
// 位置: 625-642, 820-827, 1139-1144
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
// ★ 只有“布局稳定”的 POD 类型才被提升为原生 C++ 形态

auto Type = MakeShared<FUStructType>(Struct, TypeName);
Type->ScriptTypeInfo = Binds.GetTypeInfo();
Type->ScriptTypeInfo->SetUserData(Struct);
FAngelscriptType::Register(Type);
// ★ 先统一注册 FUStructType，保证 struct 基础覆盖面

if (Struct->StructFlags & STRUCT_IsPlainOldData)
    BindFlags.ExtraFlags |= asOBJ_POD;
BindStructType(TypeName, Struct, BindFlags);
BindStructTypeLookups();
// ★ 优化是建立在统一 bind 完成之后，而不是先做白名单生成
```

差距判断：

- 非白名单 struct 的静态快路径：`没有实现`。本轮在 slua 生成文件里只看到了基础 struct map，未看到与 Angelscript `BindStructType()` 等价的“所有 struct 都有生成式快路径”。
- 绑定覆盖面的建立顺序：`实现方式不同`。slua 先按版本与白名单生成专用代码；Angelscript 先做统一反射注册，再局部开启 native form。
- 跨 UE 次版本的维护成本：`实现质量差异`。slua 直接分叉 `LuaWrapper4.18/4.25/5.1/5.2/5.3/5.4.inc`，版本扩展面更宽；Angelscript 本轮证据主要把差异集中在 `GetCppForm()` 的条件判断和 `StaticJIT` 上，维护集中度更高。

### [维度 D3] `FLuaBPVar` 不是普通 `UStruct` 绑定，而是 Blueprint 与 Lua 之间的“动态值信封”

前文已经比较过 slua 与 Angelscript 的 Blueprint 交互能力。本轮新增发现是：slua 并没有把 Blueprint 侧参数传递完全建立在逐个 `UFunction` 签名绑定上，而是额外定义了一个 `FLuaBPVar` 信封，把 `LuaVar` 包成 `USTRUCT`，再通过 `ULuaBlueprintLibrary` 暴露一组“泛型入口 + 取值节点”。因此 Blueprint 图里看到的并不是一串强类型 pin，而是 `FLuaBPVar` 数组进、`FLuaBPVar` 出，随后再用 `GetIntFromVar`、`GetStringFromVar`、`GetObjectFromVar` 做二次拆包。

这层信封不是普通 struct property 的自然结果，而是 marshaller 明确留出的旁路。`LuaObject::getReferencer()` 和 `getReferencePusher()` 一遇到 `FLuaBPVar::StaticStruct()` 就返回 `nullptr`，让默认的 struct 引用逻辑失效；到了真正 push/check 的时候，又在 `LuaObject.cpp` 里单独判断 `uss == FLuaBPVar::StaticStruct()`，直接执行 `value.push(L)` 或 `FLuaBPVar::checkValue(...)`。也就是说，`FLuaBPVar` 更像 Blueprint 图里的“动态值邮包”，而不是一个参与常规反射绑定的业务 struct。

Angelscript 的 Blueprint 交互路线则更接近 UE 原生调用面。`Bind_BlueprintCallable.cpp` 先检查 `DirectNativePointer`，没有直接指针时退回 `BindBlueprintCallableReflectiveFallback`，有直接指针时则把 `Signature.Declaration` 直接喂给 `BindMethodDirect(...)`。它的核心对象是“已知函数签名”，不是一个统一的动态值容器。因此两者虽然都能让 Blueprint 触达脚本，但 slua 更像“动态消息桥”，Angelscript 更像“按函数原型做静态暴露”。

```text
[D3] Blueprint Bridge Shape
sluaunreal
Blueprint Graph
├─ FLuaBPVar[] Args
├─ CallToLuaWithArgs(FunctionName, Args)
├─ for arg in Args -> arg.value.push(lua_State)
├─ Lua function returns LuaVar
└─ GetInt/GetString/GetObject(FLuaBPVar, Index)
   └─ 运行时再按索引和类型拆包

Angelscript
BlueprintCallable UFunction
├─ 读取 Signature.Declaration
├─ 有 direct native pointer -> BindMethodDirect(...)
└─ 无 direct pointer -> ReflectiveFallback
   └─ 仍围绕具体函数签名工作，而非统一 variant 信封
```

[1] 关键源码：slua 明确把 `LuaVar` 包进 `FLuaBPVar`，Blueprint 节点只暴露动态信封

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaBlueprintLibrary.h
// 函数: FLuaBPVar / ULuaBlueprintLibrary declarations
// 位置: 22-43, 61-76
// ============================================================================
struct SLUA_UNREAL_API FLuaBPVar {
    GENERATED_USTRUCT_BODY()
public:
    FLuaBPVar(const NS_SLUA::LuaVar& v) :value(v) {}
    FLuaBPVar(NS_SLUA::LuaVar&& v) :value(MoveTemp(v)) {}
    FLuaBPVar() {}

    NS_SLUA::LuaVar value;

    static void* checkValue(NS_SLUA::lua_State* L, NS_SLUA::FStructProperty* p, uint8* params, int i);
};
// ★ Blueprint 可见的数据壳体里真正承载的是 LuaVar，而不是强类型 UE 字段

UFUNCTION(BlueprintCallable, meta=(DisplayName="Call To Lua With Arguments"), Category="slua")
static FLuaBPVar CallToLuaWithArgs(UObject* WorldContextObject, FString FunctionName, const TArray<FLuaBPVar>& Args, FString StateName);

UFUNCTION(BlueprintCallable, Category="slua")
static int GetIntFromVar(FLuaBPVar Value,int Index=1);
UFUNCTION(BlueprintCallable, Category="slua")
static FString GetStringFromVar(FLuaBPVar Value,int Index=1);
UFUNCTION(BlueprintCallable, Category="slua")
static UObject* GetObjectFromVar(FLuaBPVar Value,int Index=1);
// ★ Blueprint 端通过“泛型 call + 类型化拆包节点”与 Lua 交互
```

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaBlueprintLibrary.cpp
// 函数: ULuaBlueprintLibrary::CallToLuaWithArgs / FLuaBPVar::checkValue
// 位置: 36-76, 124-145
// ============================================================================
FBlueprintSupport::RegisterBlueprintWarning(
    FBlueprintWarningDeclaration(GetVarOutOfBoundsWarning, LOCTEXT("GetOutOfBoundsWarning", "BpVar read access out of bounds")));
FBlueprintSupport::RegisterBlueprintWarning(
    FBlueprintWarningDeclaration(GetVarTypeErrorWarning, LOCTEXT("GetVarTypeErrorWarning", "BpVar is not speicified type")));
// ★ 错误模型落在“运行时索引越界 / 运行时类型不匹配”上，而不是 Blueprint 编译期 pin 校验

auto fillParam = [&]
{
    for (auto& arg : args) {
        arg.value.push(ls->getLuaState());
    }
    return args.Num();
};
return f.callWithNArg(fillParam);
// ★ 入参逐个把 LuaVar 压栈，再调用目标 Lua 函数

void* FLuaBPVar::checkValue(NS_SLUA::lua_State* L, NS_SLUA::FStructProperty* p, uint8* params, int i)
{
    FLuaBPVar ret;
    ret.value.set(L, i);
    p->CopyCompleteValue(params, &ret);
    return nullptr;
}
// ★ 回传值同样先封成 FLuaBPVar，再复制回 Blueprint 可见的 struct 壳体
```

[2] 关键源码：`FLuaBPVar` 在 slua marshaller 里走专门旁路；Angelscript 则围绕签名直接绑定

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaObject.cpp
// 函数: getReferencer / getReferencePusher / push struct / check struct
// 位置: 592-625, 2240-2245, 2438-2439
// ============================================================================
auto sp = CastField<FStructProperty>(prop);
if (sp && sp->Struct == FLuaBPVar::StaticStruct())
    return nullptr;
// ★ 不让 FLuaBPVar 走默认 struct 引用流程

auto sp = static_cast<FStructProperty*>(prop);
if (sp && sp->Struct == FLuaBPVar::StaticStruct())
    return nullptr;
// ★ reference pusher 同样跳过，说明它不是常规 struct marshalling

if (uss == FLuaBPVar::StaticStruct()) {
    ((FLuaBPVar*)parms)->value.push(L);
}
// ★ 真正 push 时直接把信封里的 LuaVar 送回 Lua 栈

if (uss == FLuaBPVar::StaticStruct())
    return FLuaBPVar::checkValue(L, p, parms, i);
// ★ check 时也绕过通用 struct 路线，直接恢复 LuaVar
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp
// 函数: BindBlueprintCallable
// 位置: 72-76, 120-137
// ============================================================================
auto* DirectNativePointer = &Entry->FuncPtr;
const bool bHasDirectNativePointer = DirectNativePointer != nullptr && DirectNativePointer->IsBound();
if (!bHasDirectNativePointer)
{
    if (!BindBlueprintCallableReflectiveFallback(InType, Function, Signature, *Entry))
        return;
}
// ★ 回退路径仍然是“按 UFunction/Signature 反射绑定”，不是统一动态值容器

int FunctionId = FAngelscriptBinds::BindMethodDirect
(
    Signature.ClassName,
    Signature.Declaration, ASFuncPtr,
    asCALL_CDECL_OBJFIRST, Entry->Caller
);
// ★ 直接绑定时使用完整 Declaration，Blueprint 交互仍以强类型函数签名为中心
```

差距判断：

- Blueprint 侧统一 variant 容器：`没有实现`。本轮在 Angelscript 侧未看到与 `FLuaBPVar` 对等的通用动态值信封。
- Blueprint 与脚本的接口形态：`实现方式不同`。slua 用 `FLuaBPVar` 把参数和返回值包装成动态消息；Angelscript 用 `Signature.Declaration` 把具体函数签名直接暴露给脚本层。
- 类型安全边界：`实现质量差异`。slua 通过 `BpVar read access out of bounds` / `BpVar is not speicified type` 把很多错误延后到运行时；Angelscript 因为直接围绕声明式签名绑定，更多约束前移到绑定阶段和调用签名本身。

### [维度 D8] slua 的 profiler 是一条完整的“采样运输链”；Angelscript 更把性能工作压在调用降本与引擎 trace 上

前文提过 slua 有 profiler。本轮往代码路径继续下钻后可以确认，它不是一个零散的 debug 开关，而是一条完整的数据链。入口首先放在 `LuaState`：当 `ENABLE_PROFILER && !UE_BUILD_SHIPPING` 时，Lua VM 直接使用 `LuaMemoryProfile::alloc` 创建，随后执行 `LuaProfiler::init(this)`。这意味着 slua 一开始就把“内存统计能力”接进解释器 allocator，而不是事后从外部采样。

链路第二段是传输与消费。`LuaProfiler.cpp` 里维护 `tcpSocket` 并主动检查 socket 是否可读；`slua_profile` 编辑器模块创建 `FProfileServer`，收到 profile message 后在 `PHE_TICK` 时把函数树根节点和内存快照分别入队；后台 `SluaProfilerDataManager` 线程再把 CPU / memory 命令流预处理、按帧写入 `Saved/Profiling/Sluastats/*.sluastat`，并依据 UE 版本用 `Zlib` 或 `Oodle` 压缩。换句话说，slua profiler 不是“脚本里打几个 trace point”，而是“解释器采样 -> socket 传输 -> 编辑器面板消费 -> 压缩落盘”的完整产品链。

Angelscript 在 D8 上的发力点则明显不同。`Bind_UStruct.cpp` 与 `AngelscriptStaticJIT.cpp` 的组合表明，它优先把成本压到调用面本身：一旦 `GetCppForm()` 判断类型具备稳定 native form，JIT 生成代码就直接使用 `alignof(...)` / `sizeof(...)` 布参数布局，并借助 `PrecompiledData->ReferenceFunction(...)` / `CreateFunctionId(...)` 保持脚本函数与生成代码之间的稳定索引。与此同时，Angelscript 也有调试和分析配套，但更偏 UE 原生工具链风格，例如 `FCpuProfilerTraceScoped` 直接输出 `FCpuProfilerTrace` 事件，`DebugServer` 和 `CodeCoverage` 分别服务调试协议和覆盖率统计；本轮没有看到与 slua `slua_profile + .sluastat` 对等的独立 profiler 采样产线。

```text
[D8] Performance Strategy
sluaunreal
LuaState
├─ lua_newstate(LuaMemoryProfile::alloc)
├─ LuaProfiler::init(this)
├─ tcp/socket message flow
├─ slua_profile editor server
├─ queue CPU/memory frames
└─ Saved/Profiling/Sluastats/*.sluastat
   └─ Zlib/Oodle compression

Angelscript
Bind / StaticJIT
├─ GetCppForm() 判断 native C++ form
├─ 生成代码直接使用 sizeof/alignof
├─ PrecompiledData 维护函数引用与 FunctionId
└─ 可选 FCpuProfilerTraceScoped / DebugServer / CodeCoverage
   └─ 更偏执行路径降本和接入 UE 原生分析设施
```

[1] 关键源码：slua 从 Lua VM allocator 开始接 profiler，并自带远端 server 与消息泵

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaState.cpp
// 函数: LuaState ctor/init
// 位置: 558-560, 630
// ============================================================================
#if ENABLE_PROFILER && !UE_BUILD_SHIPPING
    L = lua_newstate(LuaMemoryProfile::alloc,this);
#endif
// ★ profiler 打开时，Lua VM 一开始就改用可统计内存的 allocator

LuaProfiler::init(this);
// ★ state 初始化阶段直接挂 profiler，而不是外部事后旁路采样
```

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaProfiler.cpp
// 函数: profiler socket state / checkSocketRead
// 位置: 65-69, 141-148
// ============================================================================
TMap<LuaState*, LuaVar> selfProfiler;
bool ignoreHook = false;
HookState currentHookState = HookState::UNHOOK;
int64 profileTotalCost = 0;
p_tcp tcpSocket = nullptr;
// ★ profiler 自己维护传输 socket，而不是完全依赖 UE trace channel

bool checkSocketRead() {
    int result;
    u_long nread = 0;
    t_socket fd = tcpSocket->sock;
#if PLATFORM_WINDOWS
    result = ioctlsocket(fd, FIONREAD, &nread);
#endif
}
// ★ runtime 会主动轮询网络侧是否有 profiler 指令/数据
```

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_profile/Private/slua_profile.cpp
// 函数: inspector setup / tick event handling
// 位置: 137-140, 219-226
// ============================================================================
sluaProfilerInspector->ProfileServer = MakeShareable(new NS_SLUA::FProfileServer());
sluaProfilerInspector->ProfileServer->OnProfileMessageRecv().BindLambda([this](NS_SLUA::FProfileMessagePtr Message) {
    this->debug_hook_c(Message);
});
// ★ 编辑器模块里有专门的 ProfileServer 接 profile 消息

funcProfilerNodeQueue.Enqueue(funcProfilerRoot);
memoryQueue.Enqueue(currentMemory);
ClearCurProfiler();
// ★ 收到 PHE_TICK 后，把函数树和内存快照都推进后续处理队列
```

[2] 关键源码：slua 后台线程负责把 profiler 数据整理成 `.sluastat` 制品

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/SluaProfilerDataManager.cpp
// 函数: ReceiveProfileData / Run / GenerateStatFilePath / SerializeCompreesedDataToFile
// 位置: 44-56, 258-281, 344-350, 644-650, 927-947
// ============================================================================
if (ProcessRunnable)
{
    ProcessRunnable->ReceiveProfileData(hookEvent, time, lineDefined, funcName, shortSrc);
}
// ★ CPU 事件先汇入后台处理线程

while (bCanStartFrameRecord && frameArchive && !funcProfilerNodeQueue.IsEmpty())
{
    memoryQueue.Dequeue(memoryFrame);
    funcProfilerNodeQueue.Dequeue(funcProfilerNode);
    PreProcessData(funcProfilerNode, memoryInfo, memoryFrame);
}
// ★ 数据会先重建成按帧组织的 profiler 结构，再决定何时落盘

FString filePath = FPaths::ProfilingDir() + "/Sluastats/" + ... + ".sluastat";
frameArchive = IFileManager::Get().CreateFileWriter(*filePath);
// ★ profiler 输出是独立的统计文件格式，不是临时日志流

#if (ENGINE_MINOR_VERSION<=26) && (ENGINE_MAJOR_VERSION==4)
    FCompression::CompressMemory(NAME_Zlib, compressedBuffer, compressedSize, dataToCompress.GetData(), uncompressedSize);
#else
    FCompression::CompressMemory(NAME_Oodle, compressedBuffer, compressedSize, dataToCompress.GetData(), uncompressedSize);
#endif
// ★ slua 明确维护了 profiler 文件压缩策略
```

[3] 对照源码：Angelscript 把“性能”更多体现在 native layout/JIT 与 UE trace 接入

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp
// 函数: native cpp form / parameter layout / function reference
// 位置: 443-456, 522-530, 1607-1610, 3206-3212
// ============================================================================
Param.Type.GetCppForm(CppForm);
if (CppForm.CppType.Len() != 0)
{
    Param.LiteralType = CppForm.CppType;
    bHasNativeCppForm = true;
}
// ★ 先拿到可直接落到 C++ 的类型形态

ParmsEntryHeader += FString::Printf(TEXT("\tParmsOffset = Align(ParmsOffset, alignof(%s));\n"), *Param.LiteralType);
ParmsEntryHeader += FString::Printf(TEXT("\tParmsOffset += sizeof(%s);\n"), *Param.LiteralType);
// ★ 生成代码直接用 native sizeof/alignof，目标是减少脚本调用桥接成本

auto Ref = JIT->PrecompiledData->ReferenceFunction(Function);
uint32 FunctionId = PrecompiledData->CreateFunctionId(ScriptFunction);
// ★ 函数引用和 FunctionId 都为生成代码提供稳定映射
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FCpuProfilerTraceScoped.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/FCpuProfilerTraceScoped.h
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 4-13, 14-27, 1455-1462, 2163-2164, 5535-5549
// ============================================================================
FCpuProfilerTraceScoped_.Constructor("void f(const FName& EventID)", [](FCpuProfilerTraceScoped* Address, const FName& EventID)
{
    new(Address) FCpuProfilerTraceScoped(EventID);
});
// ★ Angelscript 允许脚本侧触发 UE CPU trace scope

FCpuProfilerTrace::OutputBeginDynamicEvent(EventID);
...
FCpuProfilerTrace::OutputEndEvent();
// ★ 这里接的是 Unreal 自己的 trace 通道，不是 slua 式独立 profiler 文件协议

DebugServer = new FAngelscriptDebugServer(this, RuntimeConfig.DebugServerPort);
CodeCoverage = new FAngelscriptCodeCoverage;
...
if (DebugServer != nullptr)
    DebugServer->ProcessMessages();
...
AngelscriptManager.CodeCoverage->HitLine(*Module, Line);
// ★ Angelscript 在工具链上更偏调试 server + 覆盖率，而不是单独的 sampling pipeline
```

差距判断：

- 与 `slua_profile + .sluastat` 对等的独立 profiler 采样/落盘链：`没有实现`。本轮在 Angelscript 侧看到了 `DebugServer`、`CodeCoverage`、`FCpuProfilerTraceScoped`，但未见等价的远端采样文件管线。
- 性能优化的主战场：`实现方式不同`。slua 强在可观测性链路完整；Angelscript 强在 `GetCppForm()` + `StaticJIT` 直接降低绑定与调用开销。
- 性能工具产品化完整度：`实现质量差异`。slua 已经把 allocator、socket、编辑器 server、后台线程、压缩落盘串成闭环；Angelscript 当前更像“把 profiling 挂到 UE 既有 trace/调试设施里”，独立 profiler 成品度不在同一侧重点上。

### [维度 D11] slua 插件只定义“模块字节入口”；Angelscript 维护正式的 `PrecompiledScript.Cache` 制品账本

前文已经分析过 slua 把线上入口插进 `package.searchers[2]`。本轮继续往部署制品层深挖后，差异变得更清楚：slua 插件真正承诺给项目方的接口，只有“模块名 -> 字节数组 + filepath”。`LuaState.h` 里 `LoadFileDelegate` 的签名就是 `TArray<uint8>` 返回值；`LuaState.cpp` 的 `loader()` 和 `doFile()` 只负责把这段字节送进 `luaL_loadbuffer` / `doBuffer`。也就是说，插件本身并不定义包清单、版本戳、构建标识、签名、哈希或热更 manifest。

这一点被 demo 代码进一步坐实。`Reference/sluaunreal/Source/democpp/MyGameInstance.cpp` 只是把模块名映射到 `Content/Lua/...` 下的 `.lua` / `.luac` 文件，再把内容读成 `TArray<uint8>` 返回。对于线上热更常见的加密、签名、CDN 分发、补丁包版本管理，slua 的做法是“把入口留给项目侧接管”，而不是插件内建一套资产制品模型。

Angelscript 则在插件层维护了明确的预编译制品账本。`FAngelscriptPrecompiledData` 不只保存模块文本缓存，而是把 `DataGuid`、`Modules`、类型/函数/全局/属性引用表都持久化下来；`InitFromActiveScript()` 会从当前激活脚本图构造这些数据，`GetModulesToCompile()` 再把预编译账本还原成可编译的 `FAngelscriptModuleDesc`，并包含 `ImportedModules`。引擎启动时会优先加载 `PrecompiledScript_[Config].Cache`，校验 `IsValidForCurrentBuild()`；生成阶段则重新写出 `PrecompiledScript.Cache`；运行阶段若启用了 fully precompiled scripts，还会显式关闭 hot reload，并且用 `CodeHash` 判断模块缓存是否可复用。它已经是“正式制品 + 构建一致性校验”的形态，而不是单纯字节入口。

```text
[D11] Artifact Boundary
sluaunreal
项目侧 patch/CDN/加密系统
└─ LoadFileDelegate(moduleName) -> TArray<uint8>
   ├─ loader()/doFile() -> luaL_loadbuffer / doBuffer
   ├─ filepath 仅用于 chunk name
   └─ 包版本、签名、哈希、回滚策略都留在插件外

Angelscript
Active script graph
└─ FAngelscriptPrecompiledData
   ├─ DataGuid / Modules / FunctionReferences / ...
   ├─ Save("PrecompiledScript[_Config].Cache")
   ├─ Load + IsValidForCurrentBuild()
   ├─ GetModulesToCompile() 还原 ImportedModules
   └─ CodeHash 命中时直接复用
      └─ fully precompiled 模式下禁用 hot reload
```

[1] 关键源码：slua 插件接口停在“给我字节”，demo 也只示范 `.lua/.luac` 文件映射

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaState.h
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaState.cpp
// 位置: 110, 131-155, 651-652, 757-762
// ============================================================================
typedef TArray<uint8> (*LoadFileDelegate) (const char* fn, FString& filepath);
// ★ 插件对外的部署契约就是“模块名 -> 字节数组”

int LuaState::loader(lua_State* L) {
    const char* fn = lua_tostring(L,1);
    FString filepath;
    TArray<uint8> buf = state->loadFile(fn, filepath);
    if(buf.Num() > 0) {
        snprintf(chunk,256,"@%s",TCHAR_TO_UTF8(*filepath));
        if(luaL_loadbuffer(L,(const char*)buf.GetData(),buf.Num(),chunk)==0) {
            return 1;
        }
    }
    return 0;
}
// ★ loader 只消费字节并编译 chunk，没有包元数据校验步骤

void LuaState::setLoadFileDelegate(LoadFileDelegate func) {
    loadFileDelegate = func;
}

LuaVar r = doBuffer(buf.GetData(),buf.Num(),chunk,pEnv);
// ★ doFile 也只是把 bytes 交给解释器，制品治理完全交给 delegate 背后系统
```

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Source/democpp/MyGameInstance.cpp
// 函数: UMyGameInstance::CreateLuaState
// 位置: 36-64
// ============================================================================
state = new NS_SLUA::LuaState("SLuaMainState", this);
state->setLoadFileDelegate([](const char* fn, FString& filepath)->TArray<uint8> {
    FString path = FPaths::ProjectContentDir();
    FString filename = UTF8_TO_TCHAR(fn);
    path /= "Lua";
    path /= filename.Replace(TEXT("."), TEXT("/"));

    TArray<uint8> Content;
    TArray<FString> luaExts = { UTF8_TO_TCHAR(".lua"), UTF8_TO_TCHAR(".luac") };
    for (auto& it : luaExts) {
        auto fullPath = path + *it;
        FFileHelper::LoadFileToArray(Content, *fullPath);
        if (Content.Num() > 0) {
            filepath = fullPath;
            return MoveTemp(Content);
        }
    }
    return MoveTemp(Content);
});
// ★ demo 只证明“从磁盘取 lua/luac 字节”；加密、签名、补丁分发都不在插件内建路径里
```

[2] 对照源码：Angelscript 把预编译脚本做成可校验、可重建、可复用的正式制品

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp
// 位置: 569-577, 617-630, 650-659, 2620, 2649-2659, 2752-2779
// ============================================================================
FGuid DataGuid;
TMap<FString, FAngelscriptPrecompiledModule> Modules;
TMapAsPtr<int64, FAngelscriptTypeReference> TypeReferences;
TMapAsPtr<int64, FAngelscriptFunctionReference> FunctionReferences;
TMapAsPtr<int64, FAngelscriptGlobalReference> GlobalReferences;
TMapAsPtr<int64, FAngelscriptPropertyReference> PropertyReferences;
// ★ 制品不仅记模块，还记类型/函数/全局/属性引用账本

TArray<TSharedRef<FAngelscriptModuleDesc>> GetModulesToCompile();
uint32 CreateFunctionId(asIScriptFunction* Function);
FAngelscriptPrecompiledReference ReferenceFunction(class asIScriptFunction* Function);
// ★ 预编译数据不仅可存，还能反向驱动编译和函数映射

DataGuid = FGuid::NewGuid();
...
Modules.FindOrAdd(ModuleName).InitFrom(*this, Module);
// ★ 每次生成预编译数据时都会形成新的制品身份和模块快照

ModuleDesc->ModuleName = Module.ModuleName.UnrealString();
ModuleDesc->CodeHash = Module.CodeHash;
for (FStringInArchive& ImportStr : Module.ImportedModules)
    ModuleDesc->ImportedModules.Add(ImportStr.UnrealString());
// ★ 还原出来的不是“裸字节”，而是带依赖信息和 CodeHash 的模块描述
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: precompiled data load/save/use
// 位置: 1433-1442, 1513-1539, 1583-1587, 2046-2057, 4284-4290
// ============================================================================
if (bGeneratePrecompiledData)
    PrecompiledData = new FAngelscriptPrecompiledData(Engine);
...
StaticJIT->PrecompiledData = PrecompiledData;
// ★ 预编译制品和 StaticJIT 在引擎生命周期里是正式对象

Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript_Development.Cache");
...
PrecompiledData->Load(Filename);
if (!PrecompiledData->IsValidForCurrentBuild())
{
    delete PrecompiledData;
    PrecompiledData = nullptr;
}
// ★ 运行前会检查 cache 是否对当前 build 仍然有效

PrecompiledData->InitFromActiveScript();
PrecompiledData->Save(Filename);
// ★ 生成阶段会把当前激活脚本图重新固化成 cache

ModulesToCompile = PrecompiledData->GetModulesToCompile();
UE_LOG(Angelscript, Warning, TEXT("Using fully precompiled scripts. Hot reloading is disabled for this run."));
// ★ fully precompiled 模式是明确运行档位，不与 hot reload 混用

if (CompiledModule->CodeHash == Module->CodeHash)
{
    ...
}
// ★ module 级复用建立在 CodeHash 一致性之上
```

差距判断：

- 插件层内建的签名/加密/版本 manifest：`没有实现`。本轮在 slua 插件入口只看到 `LoadFileDelegate` 与 `.lua/.luac` 字节加载，未看到正式制品元数据和完整性校验结构。
- 部署制品的抽象层次：`实现方式不同`。slua 提供的是可替换字节入口；Angelscript 提供的是带 `DataGuid`、`ImportedModules`、`FunctionId`、`CodeHash` 的 cache 制品模型。
- 构建一致性与可复用性：`实现质量差异`。Angelscript 明确校验 `IsValidForCurrentBuild()` 并按 `CodeHash` 复用模块；slua 当前插件层没有同级别的 build-validity / artifact-reuse 机制。

---

## 深化分析 (2026-04-08 19:26:44)

### [维度 D2 / D8] slua 的属性桥不是“每次都裸反射”，而是 `FProperty` 类分发表 + 首次访问闭包缓存

前文已经拆过 slua 的 `UFunction` 调用缓存；这一轮补的是**属性访问**路径。继续下钻 `LuaObject.cpp` 后，可以确认 slua 的动态桥接并不是每次都从 `FProperty` 开始做 if/else 反射分派，而是先在 `LuaObject::init()` 里把 `FIntProperty / FArrayProperty / FMapProperty / FStructProperty / FDelegateProperty` 等 UE 属性类注册进 `pusherMap / checkerMap / referencePusherMap`，再在对象第一次访问某个成员名时，把 `FProperty*` 和对应的函数指针塞进 Lua closure upvalue。后续同一对象上的同名访问，会优先命中这层闭包缓存。

更关键的是，slua 连容器引用都没有退回“复制后修改再写回”的通道。`referencePusherUArrayProperty / referencePusherUMapProperty / referencePusherUSetProperty` 会直接构造 `LuaArray / LuaMap / LuaSet` 引用代理；如果属性还是网络复制字段，还会把 `LuaNetSerializationProxy` 带进去。也就是说，slua 的属性桥接实际是“**按 `FProperty` 类预注册 dispatch function + 按成员名延迟生成 accessor closure + 容器走引用代理**”。

Angelscript 的属性路径则明显前移。`GetPropertyBindParams()` 先在 bind 阶段决定读写暴露边界，`Bind_UStruct.cpp` 把 `FProperty` 直接转成 `FAngelscriptTypeUsage` 后注册到类型系统里；如果类型还能拿到稳定 native form，`StaticJIT` 会继续把参数布局降到 `sizeof/alignof`。所以两者都不是“纯反射”，但 slua 把优化点放在**运行时桥接层缓存**，Angelscript 则把优化点放在**绑定期类型化 + JIT layout**。

```text
[D2/D8] Property Bridge Fast Path
sluaunreal
├─ LuaObject::init()                               // 启动时注册各类 FProperty handler
│  ├─ regPusher / regChecker
│  ├─ regReferencePusher
│  └─ LuaWrapper::initExt(L)
├─ first access: objectIndex(obj, "Field")         // 首次访问某个成员名
│  ├─ findCacheProperty()
│  ├─ getPusher/getChecker by FFieldClass*
│  └─ cachePropertyOperator()
│     └─ closure(instanceIndex, FProperty*, fn ptrs)
└─ next access
   ├─ 命中 Lua uservalue/cache table               // 不再重新挑选 handler
   └─ Array/Map/Set/Struct 可走 reference proxy
      └─ 可选挂接 LuaNetSerializationProxy

Angelscript
├─ GetPropertyBindParams(Property)                 // 绑定期决定可读/可写/可编辑
├─ FAngelscriptTypeUsage::FromProperty(Property)   // 转成强类型 usage
├─ Bind_UStruct / Bind_UClass 注册声明             // 直接挂进类型系统
└─ StaticJIT::GetCppForm()                         // 需要时下沉到 native layout
   └─ sizeof/alignof 生成参数布局
```

[1] 关键源码：slua 先注册 `FProperty` 类分发表，再把成员访问缓存成 Lua 闭包

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaObject.h
// 函数: type definitions / bridge entry
// 位置: 345-361，属性桥的统一函数签名
// ============================================================================
typedef int (*PushPropertyFunction)(lua_State* L,FProperty* prop,uint8* parms,int i,NewObjectRecorder* objRecorder);
typedef void* (*CheckPropertyFunction)(lua_State* L,FProperty* prop,uint8* parms,int i,bool bForceCopy);
typedef void (*ReferencePropertyFunction)(lua_State* L, FProperty* prop, uint8* src, void* dst);
typedef int (*ReferencePusherPropertyFunction)(lua_State* L,FProperty* prop,uint8* parms,void* parentAdrres,uint16 replicateIndex);
// ★ slua 先把“怎么 push / 怎么 check / 怎么按引用暴露”抽象成函数表，再按属性类选择实现
```

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaObject.cpp
// 函数: LuaObject::init / cachePropertyOperator / pushReferenceAndCache
// 位置: 66-69, 475-500, 529-575, 1253-1303, 2971-3054
// ============================================================================
TMap<FFieldClass*,LuaObject::PushPropertyFunction> pusherMap;
TMap<FFieldClass*,LuaObject::CheckPropertyFunction> checkerMap;
TMap<FFieldClass*,LuaObject::ReferencePusherPropertyFunction> referencePusherMap;
// ★ 第一层是按 FFieldClass* 建立分发表，不在热路径上逐次分支判断类型

LuaObject::PushPropertyFunction LuaObject::getPusher(FFieldClass* cls) {
    auto it = pusherMap.Find(cls);
    if(it!=nullptr)
        return *it;
    return nullptr;
}
// ★ 运行时查询已经退化成 map lookup

bool cachePropertyOperator(lua_State* L, FProperty* prop, UStruct* cls, void* pusher, void* checker, bool bReferencePusher)
{
    ...
    lua_pushlightuserdata(L, prop);
    lua_pushlightuserdata(L, pusher);
    lua_pushlightuserdata(L, checker);
    lua_pushboolean(L, !!bReferencePusher);
    lua_pushcclosure(L, instanceIndex, 4);
    lua_rawset(L, -3);
    ...
}
// ★ 第一次访问成员时，把 FProperty* 和 handler 指针直接固化到 closure upvalue 里

int LuaObject::pushReferenceAndCache(const ReferencePusherPropertyFunction& pusher, lua_State* L, UStruct* cls,
                                     FProperty* prop, uint8* parms, void* parentAdrres, uint16 replicateIndex)
{
    int ret = pusher(L, prop, parms, parentAdrres, replicateIndex);
    if (ret) {
        ...
        lua_pushvalue(L, 2);   // push key
        lua_pushvalue(L, -3);  // push reference value
        lua_rawset(L, -3);
        ...
    }
}
// ★ 引用代理对象也会被塞进 per-object cache，避免下一次再重建 LuaArray/LuaMap/LuaSet

void LuaObject::init(lua_State* L) {
    regPusher<FIntProperty>();
    regPusher<FFloatProperty>();
    ...
    regPusher(FArrayProperty::StaticClass(),pushUArrayProperty);
    regPusher(FMapProperty::StaticClass(),pushUMapProperty);
    regPusher(FSetProperty::StaticClass(), pushUSetProperty);
    regPusher(FStructProperty::StaticClass(),pushUStructProperty);
    ...
    regChecker(FArrayProperty::StaticClass(),checkUArrayProperty);
    regChecker(FMapProperty::StaticClass(),checkUMapProperty);
    regChecker(FSetProperty::StaticClass(), checkUSetProperty);
    ...
    regReferencePusher(FArrayProperty::StaticClass(), referencePusherUArrayProperty);
    regReferencePusher(FMapProperty::StaticClass(), referencePusherUMapProperty);
    regReferencePusher(FSetProperty::StaticClass(), referencePusherUSetProperty);
    regReferencePusher(FStructProperty::StaticClass(), referencePusherUStructProperty);
    ...
    LuaWrapper::initExt(L);
}
// ★ 启动时先把 property bridge 装好，再接上静态 wrapper 扩展
```

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaObject.cpp
// 函数: referencePusherUArrayProperty / referencePusherUMapProperty / referencePusherUSetProperty
// 位置: 2068-2143，容器字段按引用暴露，且可带复制代理
// ============================================================================
bool isNetReplicateType = replicateIndex != InvalidReplicatedIndex;
FLuaNetSerializationProxy* proxy = nullptr;
if (isNetReplicateType)
{
    ...
    proxy = LuaNet::getLuaNetSerializationProxy(luaNetSerialization);
}

LuaArray* luaArrray = new LuaArray(p, scriptArray, true, proxy, replicateIndex);
return LuaObject::pushReference<LuaArray*>(L, luaArrray, parentAddress);
// ★ Array/Map/Set 不是复制值，而是包装成引用代理；复制字段还能顺路接入 LuaNet proxy
```

[2] 对照源码：Angelscript 在 bind 阶段先裁剪属性暴露面，再在 JIT 阶段尝试拿 native C++ form

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_PropertyBind.h
// 函数: GetPropertyBindParams
// 位置: 9-30, 65-80，绑定期就决定读写/编辑边界
// ============================================================================
FAngelscriptType::FBindParams Params;
...
if (bHasNotInAngelscript)
{
    Params.bCanRead = false;
    Params.bCanWrite = false;
    Params.bCanEdit = false;
}
...
else if (Property->HasAnyPropertyFlags(CPF_BlueprintVisible))
{
    Params.bCanRead = true;
    ...
}
// ★ Angelscript 在“生成成员声明”之前就先算出属性权限，不把这一步留到运行时 getter/setter
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UStruct.cpp
// 函数: struct property binding loop
// 位置: 1180-1196
// ============================================================================
for (TFieldIterator<FProperty> It(Struct); It; ++It)
{
    FProperty* Property = *It;

    FAngelscriptType::FBindParams Params = GetPropertyBindParams(Property);
    Params.BindClass = &Binds;

    if (!Params.bCanRead && !Params.bCanWrite && !Params.bCanEdit)
        continue;

    FAngelscriptTypeUsage Usage = FAngelscriptTypeUsage::FromProperty(Property);
    if (!Usage.IsValid())
        continue;
}
// ★ 属性一旦进 bind 阶段，就被转成类型系统里的 Usage，而不是保留到运行时再靠动态 handler 决策
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp
// 函数: parameter native layout generation
// 位置: 443-455, 522-529
// ============================================================================
FAngelscriptType::FCppForm CppForm;
Param.Type.GetCppForm(CppForm);
if (CppForm.CppType.Len() != 0)
{
    Param.LiteralType = CppForm.CppType;
    bHasNativeCppForm = true;
}
// ★ 先问类型系统能不能给出稳定 C++ form

ParmsEntryHeader += FString::Printf(TEXT("\tParmsOffset = Align(ParmsOffset, alignof(%s));\n"), *Param.LiteralType);
ParmsEntryHeader += FString::Printf(TEXT("\tParmsOffset += sizeof(%s);\n"), *Param.LiteralType);
// ★ 成功后直接生成 native layout，而不是保留动态 property bridge 到最终调用现场
```

差距判断：

- 动态属性桥接快路径：`实现方式不同`。slua 用 `FFieldClass* -> handler` 分发表和 per-property 闭包缓存压运行时成本；Angelscript 用 bind-time `TypeUsage` + `GetCppForm()` 把成本前移。
- 容器按引用暴露：`实现方式不同`。slua 明确存在 `LuaArray / LuaMap / LuaSet` 引用代理与复制代理挂点；Angelscript 当前更偏强类型成员绑定，不走这套动态容器代理模型。
- 属性桥接的性能哲学：`实现质量差异`。slua 的优势是动态覆盖面大且首访后会越来越快；Angelscript 的优势是绑定后路径更稳定、更接近原生 layout。

### [维度 D4] slua 其实有“对象/函数级撤销”，只是没有提升成模块级事务

前文已经明确 slua 没有 `package.loaded` 失效管理，也没有像 Angelscript 那样的 compile transaction。本轮补一个更细的边界判断：**slua 并不是完全没有回滚/清理能力，它只是把清理粒度压在 UObject / UFunction / UClass hook 上。**

`LuaState::NotifyUObjectDeleted()` 会在 UObject 销毁时清掉 `cachePropMap`、类属性缓存、类函数缓存和 `LuaFunctionAccelerator` 条目；`LuaOverrider::NotifyUObjectDeleted()` 则会移除对象 Lua table、撤掉类 override 记录和 constructor cache。更深入一点，`removeOneOverride()` 在编辑器路径下不仅会撤掉插入到 `UFunction::Script` 头部的 override bytecode，还会把被 `SetNativeFunc()` 改写过的 native pointer 复原；若类上新增过 Lua RPC `UFunction`，还会从 `Children / NetFields / FunctionMap` 里卸掉并 `ConditionalBeginDestroy()`。这说明 slua 的安全边界其实是“**对象级 hook 的清理必须做完整**”。

但它没有进一步提供“模块编译成功后整体提交、失败则完全保留旧模块”的事务边界。这个差异正好能用 Angelscript 的 hot reload 测试语义反衬出来：在 soft reload 测试里，Angelscript 明确要求**同一个生成类实例**和**运行中属性值**都被保留；在 full reload 测试里，又明确要求**新属性默认值**出现。这说明 Angelscript 把“保留什么、替换什么”提升成了可验证 contract；slua 当前更多是“对象死了我清缓存，Editor 关了我撤 hook”。

```text
[D4] Teardown Granularity
sluaunreal
UObject/UFunction deleted
├─ LuaState::NotifyUObjectDeleted()
│  ├─ remove class/func/property caches
│  ├─ LuaFunctionAccelerator::remove(UFunction)
│  └─ unlink UObject references
├─ LuaOverrider::NotifyUObjectDeleted()
│  ├─ removeObjectTable(obj)
│  ├─ removeOneOverride(cls, true)
│  └─ remove class constructor cache
└─ Editor close / state close
   └─ removeOverrides()
      ├─ revert injected bytecode/native func
      ├─ remove added RPC UFunction
      └─ clear super function caches

Angelscript
SoftReload contract
├─ same generated UClass instance kept
├─ live property value preserved
└─ same actor instance executes new function body

FullReload contract
├─ new generated class accepted
└─ newly added property default becomes visible
```

[1] 关键源码：slua 的清理很细，但粒度停在对象/函数/类 hook

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaState.cpp
// 函数: LuaState::close / LuaState::NotifyUObjectDeleted
// 位置: 480-488, 796-815
// ============================================================================
void LuaState::close() {
    ...
#if WITH_EDITOR
    if (!mainState && overrider)
        overrider->removeOverrides();
#endif
    ...
}
// ★ 关闭主状态时，会先撤掉所有编辑器期 override，避免把 hook 留在 UFunction 上

void LuaState::NotifyUObjectDeleted(const UObjectBase * Object, int32 Index)
{
    classMap.cachePropMap.Remove((UStruct*)Object);
    LuaObject::removeCache(L, Object, cacheEnumRef);
    LuaObject::removeCache(L, Object, cacheClassPropRef);
    LuaObject::removeCache(L, Object, cacheClassFuncRef);
    LuaFunctionAccelerator::remove((UFunction*)Object);
    releaseLink((void*)Object);
    unlinkUObject((const UObject*)Object);
    ...
}
// ★ UObject / UFunction 死亡时，会把 property/function cache 和 accelerator 一起摘掉
```

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaFunctionAccelerator.cpp
// 函数: LuaFunctionAccelerator::remove / clear
// 位置: 158-178
// ============================================================================
bool LuaFunctionAccelerator::remove(UFunction* inFunc)
{
    auto funcPtr = cache.Find(inFunc);
    if (funcPtr)
    {
        delete *funcPtr;
        cache.Remove(inFunc);
        return true;
    }
    return false;
}
// ★ 函数级缓存是可拆除的，不会一直持有已经无效的 UFunction
```

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaOverrider.cpp
// 函数: ULuaOverrider::removeObjectTable / LuaOverrider::NotifyUObjectDeleted / removeOneOverride / removeOverrides
// 位置: 423-447, 643-659, 826-974
// ============================================================================
void ULuaOverrider::removeObjectTable(UObject* obj)
{
    ...
    lua_pushstring(L, SLUA_CPPINST);
    lua_pushnil(L);
    lua_rawset(L, -3);
    ...
    tableMap.Remove(obj);
    NS_SLUA::LuaNet::removeObjectTable(obj);
}
// ★ 先把 Lua table 和 C++ instance 的反向链接断开，再清网络侧对象表

void LuaOverrider::NotifyUObjectDeleted(const UObjectBase* Object, int32 Index)
{
    UObject* obj = (UObject*)Object;
    ULuaOverrider::removeObjectTable(obj);
    ...
    if (overridedClasses.Contains(cls))
    {
        removeOneOverride(cls, true);
        overridedClasses.Remove(cls);
    }
    classConstructors.Remove(cls);
}
// ★ 类对象销毁时，还会同步撤掉该类的 override 记录与构造缓存

if (scriptNum >= CodeSize && script[0] == Ex_LuaOverride)
{
    script.RemoveAt(0, CodeSize, false);
    ...
}
...
if (func->GetNativeFunc() == (FNativeFuncPtr)&ULuaOverrider::luaOverrideFunc)
{
    ...
    func->SetNativeFunc(*nativeFunc);
}
// ★ 被插桩过的字节码和 native func ptr 都会恢复原状

cls->RemoveFunctionFromFunctionMap(func);
func->RemoveFromRoot();
func->ConditionalBeginDestroy();
// ★ 新增进类里的 Lua RPC UFunction 也会从反射结构里拆出来并销毁
```

[2] 对照源码：Angelscript 把 soft/full reload 行为写成测试契约

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp
// 函数: property-preserved soft reload / add-property full reload tests
// 位置: 110-146, 196-233
// ============================================================================
if (!TestTrue(TEXT("Scenario hot-reload property-preserved compile should succeed on the soft reload path"),
    CompileModuleWithResult(&Engine, ECompileType::SoftReloadOnly, ModuleName, TEXT("ScenarioHotReloadPropertyPreserved.as"), ScriptV2, ReloadResult)))
{
    return false;
}
...
TestEqual(TEXT("Scenario hot-reload property-preserved should keep the generated actor class instance"), ClassAfterReload, ClassV1);
...
TestEqual(TEXT("Scenario hot-reload property-preserved should keep the actor property value after soft reload"), CounterValue, 42);
...
TestEqual(TEXT("Scenario hot-reload property-preserved function should observe the preserved property value after reload"), Result, 142);
// ★ soft reload 的语义被写死成“类实例继续用、属性值保留、函数体更新”

if (!TestTrue(TEXT("Scenario hot-reload add-property compile should succeed on the full reload path"),
    CompileModuleWithResult(&Engine, ECompileType::FullReload, ModuleName, TEXT("ScenarioHotReloadAddProperty.as"), ScriptV2, ReloadResult)))
{
    return false;
}
...
TestEqual(TEXT("Scenario hot-reload add-property should preserve the original property default"), ExistingValue, 1);
TestEqual(TEXT("Scenario hot-reload add-property should expose the newly added property with its default value"), NewValue, 99);
// ★ full reload 的语义则是允许结构变化，并验证新属性默认值已经生效
```

差距判断：

- 低层缓存/hook 清理：`实现方式不同`。slua 在 UObject/UFunction/UClass 粒度有完整撤销路径；Angelscript 更强调模块/类重编译语义。
- 模块级提交边界：`没有实现`。本轮在 slua 侧仍未见 compile result、soft/full reload 状态机或失败保留旧模块的事务框架。
- 热重载行为可验证性：`实现质量差异`。Angelscript 把 soft/full reload 的保留语义写成自动化测试；slua 当前源码展示的是清理能力，而不是同级别的 reload contract。

### [维度 D1 / D2] bind 初始化的可审计性差异：slua 是命令式单入口，Angelscript 是有序注册表

这一点在前面几轮里还没被单独拎出来。继续看 `LuaObject::init()` 和 Angelscript 的 `FAngelscriptBinds` 后，可以更明确地区分两者的“绑定编排能力”。

slua 这边，property handler 注册、reference proxy 注册和 `LuaWrapper::initExt(L)` 全塞在一个命令式初始化函数里。调用顺序当然存在，但顺序信息没有被提升成可枚举、可导出、可禁用的 registry 元数据。在当前仓库可见源码里，也没有与 “bind 名称 + 顺序 + 启停状态” 对等的公共账本。

Angelscript 则把 bind 编排做成了正式基础设施。`FAngelscriptBinds::FBind` 的静态对象构造函数会在 translation unit 初始化期调用 `RegisterBinds()`，并记录 `BindName / BindOrder`；`GetBindInfoList()` 可以回收这些元数据，`AngelscriptStateDump.cpp` 还能把它们导出成 CSV。更重要的是，同一套 bind 还能拆成 `Early / Late` 两阶段，例如 `Bind_BlueprintType_Declarations` 先注册类声明，`Bind_Defaults` 再做默认值与继承链细节绑定。也就是说，Angelscript 把“绑定初始化顺序”从隐式代码顺序提升成了**显式可审计的编排层**。

```text
[D1/D2] Bind Registration Topology
sluaunreal
LuaObject::init(L)
├─ regPusher / regChecker / regReferencePusher
├─ LuaWrapper::initExt(L)
└─ runtime ready
   └─ 顺序由代码写死，但未见可导出的 bind 清单

Angelscript
translation unit static init
├─ AS_FORCE_LINK const FBind(name, order, lambda)
├─ RegisterBinds(name, order)
├─ GetSortedBindArray()
├─ GetBindInfoList()
└─ StateDump CSV
   └─ 可按 Early / Normal / Late 组织同一子系统的多阶段绑定
```

[1] 关键源码：slua 的绑定装配入口是单函数命令式初始化

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaObject.cpp
// 函数: LuaObject::init
// 位置: 2971-3054
// ============================================================================
void LuaObject::init(lua_State* L) {
    regPusher<FIntProperty>();
    regPusher<FFloatProperty>();
    ...
    regChecker<FIntProperty>();
    regChecker<FFloatProperty>();
    ...
    regReferencePusher(FArrayProperty::StaticClass(), referencePusherUArrayProperty);
    regReferencePusher(FMapProperty::StaticClass(), referencePusherUMapProperty);
    regReferencePusher(FSetProperty::StaticClass(), referencePusherUSetProperty);
    ...
    LuaWrapper::initExt(L);
}
// ★ 所有桥接装配都聚合在这里，顺序是存在的，但没有被抽象成可查询 registry
```

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaWrapper.cpp
// 函数: LuaWrapper::initExt
// 位置: 184-188
// ============================================================================
void LuaWrapper::initExt(lua_State* L)
{
    init(L);
    FSoftObjectPtrWrapper::bind(L);
}
// ★ 静态 wrapper 也是被直接调用进来，而不是作为命名 bind 单元登记到统一编排表
```

[2] 对照源码：Angelscript 的 bind 顺序、名字和启停状态都是可观测元数据

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h
// 函数: FAngelscriptBinds::FBind / EOrder / FBindInfo
// 位置: 424-474
// ============================================================================
enum class EOrder : int32
{
    Early = -100,
    Normal = 0,
    Late = 100,
};

struct FBindInfo
{
    FName BindName;
    int32 BindOrder = 0;
    bool bEnabled = true;
};

struct ANGELSCRIPTRUNTIME_API FBind
{
    FBind(FName BindName, EOrder BindOrder, TFunction<void()> Function)
    {
        FAngelscriptBinds::RegisterBinds(BindName, (int32)BindOrder, MoveTemp(Function));
    }
    ...
};
// ★ 每个 bind 都有名字、顺序和函数体，顺序从隐式代码先后提升成显式元数据
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp
// 位置: 151-184, 850-868
// ============================================================================
void FAngelscriptBinds::RegisterBinds(FName BindName, int32 BindOrder, TFunction<void()> Function)
{
    GetBindArray().Add({BindName.IsNone() ? MakeUnnamedBindName() : BindName, BindOrder, MoveTemp(Function)});
}

TArray<FAngelscriptBinds::FBindInfo> FAngelscriptBinds::GetBindInfoList(const TSet<FName>& DisabledBindNames)
{
    TArray<FBindInfo> BindInfos;
    for (const FBindFunction& Bind : GetSortedBindArray())
    {
        BindInfos.Add({Bind.BindName, Bind.BindOrder, !DisabledBindNames.Contains(Bind.BindName)});
    }
    return BindInfos;
}
// ★ 注册表不仅能执行，还能回收“名字/顺序/是否启用”的状态快照

const TArray<FAngelscriptBinds::FBindInfo> BindInfos = FAngelscriptBinds::GetBindInfoList(DisabledBindNames);
...
Writer.AddRow({
    BindInfo.BindName.ToString(),
    FString(),
    BoolToString(bIsSkipped),
    ...
});
// ★ 状态 dump 会把 bind 元数据导出，便于审计和排查跳过项
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp
// 函数: Bind_BlueprintType_Declarations / Bind_Defaults
// 位置: 712-739, 1317-1345
// ============================================================================
AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_BlueprintType_Declarations(FAngelscriptBinds::EOrder::Early, []
{
    ...
    BindUClass(Class, DBBind.TypeName);
    ...
});

AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_Defaults((int32)FAngelscriptBinds::EOrder::Late + 100, []
{
    ...
    struct FBindOrder
    {
        UClass* Class = nullptr;
        ...
    };
    ...
});
// ★ 同一子系统可以拆成 Early/Late 两阶段，先声明再补默认值和继承顺序
```

差距判断：

- bind 编排模型：`实现方式不同`。slua 把装配逻辑写死在 `LuaObject::init()`；Angelscript 把 bind 单元注册成有序 registry。
- bind 元数据可审计性：`实现质量差异`。Angelscript 能导出 `BindName / BindOrder / bEnabled`；当前 slua 可见源码里没有同级别的 registry 账本与 dump 通道。
- 分阶段绑定能力：`实现质量差异`。Angelscript 能显式区分 `Early / Late` 乃至同一子系统的多阶段绑定；slua 当前更多依赖命令式初始化顺序和 `LuaWrapper::initExt()` 的单入口装配。

---

## 深化分析 (2026-04-08 19:36:50)

### [维度 D3 / D4] slua 的 Blueprint override 会直接改写 RPC 拓扑，不只是替换事件入口

前文已经分析过 slua 的 `duplicateUFunction + hookBpScript()`。本轮继续追 `LuaNet` 之后，可以把它说得更具体一些：slua 的 Blueprint 接管不是“只把已有 `UFunction` 改到 Lua”，而是会继续扫描 Lua 模块里的 `MulticastRPC / ServerRPC / ClientRPC` 表，动态创建新的 `UFunction`，再把这些函数塞进 `UClass::FunctionMap` 与 `NetFields`。换句话说，slua 的对象级接管会顺手改写类的网络函数拓扑。

这与 Angelscript 的路径明显不同。Angelscript 把网络语义先记录在 `FAngelscriptFunctionDesc`，再由 class generator 一次性生成 `UASFunction`，同时在生成阶段校验 `_Validate` 返回值和参数签名。两者都能把脚本函数接入 UE 网络调用面，但 slua 的切入点是“运行时扩张现有 `UClass`”，Angelscript 的切入点是“编译期声明完整 class schema”。

```
[D3/D4] RPC Injection Timing
sluaunreal
Lua module table
├─ hookBpScript(existing UFunction)                // 先接管已有 BlueprintEvent / Net 函数
├─ addClassRPC()
│  ├─ scan MulticastRPC / ServerRPC / ClientRPC    // 再扫 Lua table 里的 RPC 描述
│  ├─ NewObject<UFunction>                         // 运行时新建 UFunction
│  ├─ AddFunctionToFunctionMap                     // 追加进类函数表
│  └─ NetFields.Add                               // 追加进网络字段表
└─ removeOneOverride()
   └─ RemoveFunctionFromFunctionMap / NetFields.Remove

Angelscript
script parse / class generation
├─ FAngelscriptFunctionDesc::bNet*                 // 先把网络语义写进描述结构
├─ validate _Validate signature                    // 生成前验证 RPC contract
├─ Create UASFunction
│  └─ set FUNC_Net* / FUNC_NetValidate             // 一次性生成最终 UFunction flags
└─ cache ValidateFunction                          // 缓存 _Validate 指针供运行时调用
```

[1] 关键源码：slua 的 override 绑定会把复制出的函数写回 `FunctionMap / NetFields`

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaOverrider.cpp
// 函数: duplicateUFunction / bindOverrideFuncs / hookBpScript
// 位置: 1110-1157, 1276-1287, 1398-1433
// ============================================================================
UFunction* duplicateUFunction(UFunction* templateFunction, UClass* outerClass, FName newFuncName, FNativeFuncPtr nativeFunc)
{
    ...
    UFunction* newFunc = Cast<UFunction>(StaticDuplicateObjectEx(duplicationParams));
    ...
    outerClass->AddFunctionToFunctionMap(newFunc, newFuncName); // ★ 复制出来的函数立即进入类函数表

    if (newFunc->HasAnyFunctionFlags(FUNC_NetMulticast))
    {
        outerClass->NetFields.Add(newFunc); // ★ Multicast override 还要进入 NetFields
    }
    else
    {
        newFunc->SetSuperStruct(templateFunction); // ★ 非 Multicast 保留 super 链
    }
}

UFunction* func = cls->FindFunctionByName(funcName, EIncludeSuperFlag::IncludeSuper);
if (func && (func->FunctionFlags & OverrideFuncFlags)) {
    if (hookBpScript(func, cls, (FNativeFuncPtr)&ULuaOverrider::luaOverrideFunc)) {
        hookCounter++;
    }
}
luaNet->addClassRPC(L, cls, luaFilePath); // ★ 接管 Blueprint 函数之后，继续补 Lua 侧 RPC

auto supercallFunc = duplicateUFunction(func, cls, FName(*(SUPER_CALL_FUNC_NAME_PREFIX + func->GetName())), func->GetNativeFunc());
...
overrideFunc = duplicateUFunction(func, cls, func->GetFName(), (FNativeFuncPtr)&ULuaOverrider::luaOverrideFunc);
// ★ super call 和 override call 都是动态复制 UFunction，不是预先生成好的类定义
```

[2] 关键源码：slua 的 RPC 是运行时从 Lua table 动态生长出来的

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaNet.cpp
// 函数: luaRPCTypeMap / addClassRPC / addClassRPCByType
// 位置: 34-37, 471-479, 642-645, 649-788
// ============================================================================
TMap<FString, EFunctionFlags> LuaNet::luaRPCTypeMap = {
    {TEXT("MulticastRPC"), FUNC_NetMulticast},
    {TEXT("ServerRPC"), FUNC_NetServer},
    {TEXT("ClientRPC"), FUNC_NetClient}
}; // ★ Lua 侧只声明三类 RPC 关键字，没有单独的 _Validate 语义入口

void LuaNet::addClassRPC(lua_State* L, UClass* cls, const FString& luaFilePath)
{
    if (!L || !cls) { return; }
    if (addedRPCClasses.Contains(cls)) { return; }

    LuaVar cppSuperModule;
    addClassRPCRecursive(L, cls, luaFilePath, cppSuperModule); // ★ 类首次被 Lua 接管时，递归补齐 RPC
}

for (TMap<FString, EFunctionFlags>::TConstIterator iter(LuaNet::luaRPCTypeMap); iter; ++iter)
{
    bAdded |= addClassRPCByType(L, cls, luaModule, iter.Key(), iter.Value());
}

UFunction* func = NewObject<UFunction>(cls, *rpcName, RF_Public);
func->FunctionFlags = FUNC_Public | FUNC_Net | netFlag;
if (bReliable)
{
    func->FunctionFlags |= FUNC_NetReliable; // ★ Reliable 也是从 Lua table 里读出来的
}
...
func->StaticLink(true);

cls->AddFunctionToFunctionMap(func, *rpcName);
cls->NetFields.Add(func);
func->SetNativeFunc((FNativeFuncPtr)&ULuaOverrider::luaOverrideFunc);
// ★ 运行时新建出的 RPC 直接挂到 UE 的函数查找和网络字段结构上
```

[3] 对照源码：Angelscript 把网络语义和 `_Validate` 合同固定在 class generation 阶段

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 函数: FAngelscriptFunctionDesc / validation analysis / generated UFunction flags
// 位置: AngelscriptEngine.h:972-985; AngelscriptClassGenerator.cpp:1019-1037, 3416-3483, 3660-3663
// ============================================================================
bool bNetFunction = false;
bool bNetMulticast = false;
bool bNetClient = false;
bool bNetServer = false;
bool bNetValidate = false;
// ★ 先把网络语义写进函数描述，而不是运行时扫描脚本 table 再猜

if (FunctionDesc->bNetValidate)
{
    auto ValidateFunction = ClassData.NewClass->GetMethod(FunctionDesc->FunctionName + "_Validate");
    if (ValidateFunction)
    {
        if (ValidateFunction->ScriptFunction->GetReturnTypeId() != asTYPEID_BOOL)
        {
            ... // ★ _Validate 返回值不是 bool，直接报编译错误
        }
        else if (!FunctionDesc->ParametersMatches(ValidateFunction))
        {
            ... // ★ 参数不匹配，同样在生成阶段拦截
        }
    }
}

NewFunction->ScriptFunction = FunctionDesc->ScriptFunction;
NewFunction->FunctionFlags |= FUNC_Native;
NewFunction->SetNativeFunc(&UASFunctionNativeThunk);
...
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
}
// ★ 最终 UFunction flags 一次性烙在生成类上

for (auto* Function : FunctionsWithValidate)
{
    Function->ValidateFunction = NewClass->FindFunctionByName(FName(*(Function->GetName() + TEXT("_Validate"))));
}
// ★ _Validate 指针也被缓存成生成类的一部分
```

差距判断：

- `RPC` 暴露时机：`实现方式不同`。slua 在 `bindOverrideFuncs()` 之后继续用 `LuaNet::addClassRPC()` 改写 `FunctionMap / NetFields`；Angelscript 在 class generation 阶段一次性生成完整网络函数。
- `_Validate` 合同检查：`没有实现`。本轮可见的 slua RPC 入口只有 `MulticastRPC / ServerRPC / ClientRPC` 三类标志（`LuaNet.cpp:34-37`），未见与 Angelscript `bNetValidate + ParametersMatches()` 对等的校验路径。
- 网络语义可审计性：`实现质量差异`。Angelscript 的网络 flags 先进入 `FAngelscriptFunctionDesc`，随后在生成阶段集中检查；slua 更灵活，但网络拓扑变化要靠运行时 hook/清理路径保持对称。

### [维度 D11] slua 对外暴露的是“字节流提供器 ABI”，不是完整热更制品协议

前文已经指出 slua 把热更边界收缩到“给我脚本字节”。本轮新增的证据是：这个边界不是只服务 `requireModule()`，而是贯穿了 runtime loader、Editor simulate，甚至 `luaprotobuf` 的文件读取。也就是说，slua 真正稳定下来的不是某个包格式，而是一种非常薄的 ABI: `name/path -> TArray<uint8>`。

这点很关键，因为它直接解释了为什么在插件内看不到“Lua 包签名、manifest、版本账本”。这不是漏写几行代码的问题，而是架构边界刻意选在更外层。相对地，Angelscript 把 `BindModules.Cache`、`Binds.Cache`、`PrecompiledScript*.Cache` 做成插件内部定义的正式制品协议，运行时按固定文件名和固定生命周期加载。

```
[D11] Delivery Contract Shape
sluaunreal
external updater / file system / CDN
├─ LoadFileDelegate(fn, filepath) -> bytes         // 外部系统决定明文、加密、压缩还是网络下载
├─ package.searchers[2]                            // 插件只替换模块字节来源
├─ doFile / requireModule -> luaL_loadbuffer       // 最终直接喂给 Lua VM
├─ LuaSimulate reuses same delegate                // 编辑器模拟复用同一 ABI
└─ ProtobufUtil has another byte delegate          // protobuf 也沿用“路径 -> bytes”

Angelscript
editor / cook
├─ SaveBindModules("BindModules.Cache")
├─ Save("Binds.Cache")
└─ Save("PrecompiledScript*.Cache")

runtime
├─ LoadBindModules(...)
├─ Load(Binds.Cache)
└─ Load(PrecompiledScript*.Cache)
```

[1] 关键源码：slua 的公开装载合同只有 `LoadFileDelegate`

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaState.h
// 函数: LoadFileDelegate / doFile / requireModule / setLoadFileDelegate
// 位置: 107-110, 166-189, 264-267
// ============================================================================
typedef TArray<uint8> (*LoadFileDelegate) (const char* fn, FString& filepath);
// ★ ABI 只有“模块名 -> 字节数组 + 实际路径”，没有版本号、签名或 manifest 参数

LuaVar doBuffer(const uint8* buf,uint32 len, const char* chunk, LuaVar* pEnv = nullptr);
LuaVar doFile(const char* fn, LuaVar* pEnv = nullptr);
LuaVar requireModule(const char* fn, LuaVar* pEnv = nullptr);
...
void setLoadFileDelegate(LoadFileDelegate func);
// ★ 插件只约定入口，不约定字节来自 pak、CDN、AES 解密还是本地文件

LoadFileDelegate loadFileDelegate;
TArray<uint8> loadFile(const char* fn,FString& filepath);
static int loader(lua_State* L);
```

[2] 关键源码：runtime loader 只是把外部字节源插进 `package.searchers[2]`，然后直接 `luaL_loadbuffer`

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaState.cpp
// 函数: LuaState::loader / setLoadFileDelegate / doBuffer / doFile
// 位置: 131-145, 603-616, 651-652, 725-762
// ============================================================================
TArray<uint8> buf = state->loadFile(fn, filepath);
if(buf.Num() > 0) {
    char chunk[256];
    snprintf(chunk,256,"@%s",TCHAR_TO_UTF8(*filepath));
    if(luaL_loadbuffer(L,(const char*)buf.GetData(),buf.Num(),chunk)==0) {
        return 1;
    }
}
// ★ delegate 给什么 bytes，Lua VM 就直接加载什么 bytes；插件内不再追加制品协议层

lua_pushcfunction(L,loader);
...
lua_getfield(L,-1,"searchers");
...
lua_pushvalue(L,loaderFunc);
lua_rawseti(L,loaderTable,2);
// ★ 自定义 loader 被塞进 package.searchers[2]，职责只是替换模块字节来源

void LuaState::setLoadFileDelegate(LoadFileDelegate func) {
    loadFileDelegate = func;
}

LuaVar LuaState::doBuffer(const uint8* buf,uint32 len, const char* chunk, LuaVar* pEnv) {
    if(luaL_loadbuffer(L, (const char *)buf, len, chunk)) {
        ...
    }
}
// ★ 最终消费形态仍然只是 buffer，不区分明文脚本、加密脚本还是线上补丁产物
```

[3] 关键源码：相同的“字节提供器”模式被复用到 simulate 与 protobuf

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaSimulate.cpp
// 文件: Reference/sluaunreal/Source/democpp/MyGameInstance.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/luaprotobuf/lpb.h
// 位置: LuaSimulate.cpp:22-32, 99-108; MyGameInstance.cpp:41-56; lpb.h:15-20
// ============================================================================
LuaState::LoadFileDelegate LuaSimulate::Delegate = nullptr;

void LuaSimulate::SetLuaFileLoader(LuaState::LoadFileDelegate InDelegate)
{
    Delegate = InDelegate; // ★ 编辑器模拟和 runtime 共用同一种文件提供 ABI
}

if (Delegate == nullptr)
{
    Log::Error("lua Simulation Error. LoadFileDelegate not set.");
    return;
}
SluaState = new NS_SLUA::LuaState("", nullptr);
SluaState->setLoadFileDelegate(Delegate);
// ★ simulate 模式没有另一套资源协议，仍然复用 LoadFileDelegate

state->setLoadFileDelegate([](const char* fn, FString& filepath)->TArray<uint8> {
    ...
    TArray<FString> luaExts = { UTF8_TO_TCHAR(".lua"), UTF8_TO_TCHAR(".luac") };
    ...
});
// ★ demo 工程自己决定读 .lua 还是 .luac，插件本身不做格式仲裁

typedef TArray<uint8>(*LoadFileDelegate) (const FString& filepath);
static LoadFileDelegate loadFileDelegate;
// ★ protobuf schema 读取也沿用“路径 -> bytes”的同类委托
```

[4] 对照源码：Angelscript 把运行时要加载的脚本/绑定制品命名和保存流程固化在插件内部

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: SaveBindModules / runtime cache loading / precompiled save
// 位置: AngelscriptEditorModule.cpp:1077; AngelscriptEngine.cpp:1469-1477, 1513-1534, 1583-1587, 2055-2056
// ============================================================================
FAngelscriptBinds::SaveBindModules(FString(FAngelscriptEngine::GetScriptRootDirectory() / "BindModules.Cache"));
// ★ 编辑器先把 bind module 名单固化成制品

FAngelscriptBindDatabase::Get().Load(GetScriptRootDirectory() / TEXT("Binds.Cache"), bGeneratePrecompiledData);
FAngelscriptBinds::LoadBindModules(plugin->GetBaseDir() / "BindModules.Cache");
// ★ 运行时按固定文件名回收绑定账本

if (bUsePrecompiledData)
{
    Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript_Shipping.Cache");
    ...
    if (!IFileManager::Get().FileExists(*Filename))
        Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript.Cache");
    PrecompiledData->Load(Filename);
}

if (bGeneratePrecompiledData)
{
    FString Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript.Cache");
    PrecompiledData->InitFromActiveScript();
    PrecompiledData->Save(Filename);
}
// ★ 插件自己定义了脚本制品的命名、生成和回收协议

UE_LOG(Angelscript, Warning, TEXT("Using fully precompiled scripts. Hot reloading is disabled for this run."));
// ★ precompiled 制品和 hot reload 还是显式互斥模式
```

差距判断：

- 脚本字节来源扩展点：`实现方式不同`。slua 把边界定成 `LoadFileDelegate`；Angelscript 把边界定成若干固定 cache 制品。
- 插件内建签名 / manifest / 版本账本：`没有实现`。当前 slua 可见装载 ABI 只有 `fn/path -> bytes`，没有与脚本部署制品同级的内建签名或 manifest 合同；这不等于项目做不了，而是插件边界没有接手。
- 制品可审计性：`实现质量差异`。Angelscript 至少把 `BindModules.Cache / Binds.Cache / PrecompiledScript*.Cache` 做成正式账本；slua 更灵活，但部署协议是否可审计完全取决于外部接入层。

### [维度 D8 / D11] slua 的 profiler 是开发期观测设施，Angelscript 的 `PrecompiledScript.Cache` 是运行期执行制品

前文已经把 slua profiler 的采样运输链讲清楚了。本轮再补一个容易混淆、但很关键的边界：slua 虽然在 `Build.cs` 里总是定义 `ENABLE_PROFILER`，运行时模块也总会启动 `SluaProfilerDataManager`，但真正替换 Lua allocator、注入 `LuaProfiler`、停止内存采样的逻辑都被 `!UE_BUILD_SHIPPING` 包住了。也就是说，slua 的 profiler 产品化很完整，但它仍然首先是一套开发期观测设施。

Angelscript 刚好反过来。它会在 non-editor / non-development 运行中主动切到 `bUsePrecompiledData`，加载 `PrecompiledScript_Shipping.Cache` 或通用 `PrecompiledScript.Cache`，必要时裁剪运行期数据、减少 debug 面，并明确声明 hot reload 在这一路径上关闭。它的重点不是把 profiler 做进 shipping，而是把“运行期吃什么制品、留下多少元数据”定义成正式策略。

```
[D8/D11] Build-Mode Split
sluaunreal
Build.cs -> ENABLE_PROFILER
├─ Dev / Editor
│  ├─ lua_newstate(LuaMemoryProfile::alloc)        // allocator 级内存观测
│  ├─ LuaProfiler::init                            // VM hook 注入
│  └─ save .sluastat                              // 版本化统计文件
└─ Shipping
   ├─ luaL_newstate                                // 回到普通 Lua allocator
   └─ profiler hooks compiled out                  // 观测链被剥离

Angelscript
generate mode
├─ bGeneratePrecompiledData
├─ StaticJIT->WriteOutputCode
└─ Save PrecompiledScript.Cache

cooked runtime mode
├─ bUsePrecompiledData = true
├─ Load PrecompiledScript_Shipping.Cache
├─ ClearUnneededRuntimeData
└─ hot reload / debug surface reduced
```

[1] 关键源码：slua 的 profiler 宏是全局开的，但 VM instrumentation 只在非 Shipping 生效

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/slua_unreal.Build.cs
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaState.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaProfiler.cpp
// 函数: build definitions / LuaState::init / LuaProfiler::init / close cleanup
// 位置: slua_unreal.Build.cs:106-111; LuaState.cpp:526-563, 628-630, 497-503; LuaProfiler.cpp:441-449
// ============================================================================
PublicDefinitions.Add("ENABLE_PROFILER");
PublicDefinitions.Add("NS_SLUA=slua");
// ★ 编译宏默认打开，说明 profiler 是官方支持路径

bool LuaState::init() {
    ...
#if ENABLE_PROFILER && !UE_BUILD_SHIPPING
    L = lua_newstate(LuaMemoryProfile::alloc,this);
#else
    L = luaL_newstate();
#endif
    // ★ 真正替换 allocator 只在非 Shipping 构建发生
    ...
#ifdef ENABLE_PROFILER
#if !UE_BUILD_SHIPPING
    LuaProfiler::init(this);
#endif
#endif
    // ★ profiler hook 注入同样只在非 Shipping 生效
}

if(L) {
#ifdef ENABLE_PROFILER
#if !UE_BUILD_SHIPPING
    LuaMemoryProfile::stop(L);
    LuaProfiler::clean(this);
#endif
#endif
}
// ★ 收尾和清理也被 Shipping 路径裁掉

void LuaProfiler::init(LuaState* LS)
{
    ...
    profiler = LS->doBuffer((const uint8*)ProfilerScript,strlen(ProfilerScript), ChunkName);
}
// ★ profiler 本身还是靠一段注入 Lua VM 的脚本启动
```

[2] 关键源码：slua 的 profiler 制品是 versioned 的 `.sluastat` 开发统计文件

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/slua_unreal.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/SluaProfilerDataManager.cpp
// 函数: Fslua_unrealModule::StartupModule / ShutdownModule / GenerateStatFilePath / LoadData
// 位置: slua_unreal.cpp:20-36; SluaProfilerDataManager.cpp:646-700
// ============================================================================
void Fslua_unrealModule::StartupModule()
{
    ...
    SluaProfilerDataManager::StartManager(); // ★ manager 跟 runtime 模块生命周期一起启动
}

void Fslua_unrealModule::ShutdownModule()
{
    ...
    SluaProfilerDataManager::StopManager();
}

FString filePath = FPaths::ProfilingDir() + "/Sluastats/"
    ...
    + ".sluastat";
// ★ 输出是专用 profiler 统计文件，不是脚本部署包

int32 version;
*ar << version;
if (version != ProfileVersion)
{
    UE_LOG(Slua, Warning, TEXT("sluastat file version mismatch: %d, %d"), version, ProfileVersion);
    return;
}
...
FCompression::UncompressMemory(NAME_Oodle, uncompressedBuffer.GetData(), uncompressedSize, compressedBuffer, compressedSize);
// ★ 制品自带版本号与压缩格式，但用途是离线分析，不是运行时脚本装载
```

[3] 对照源码：Angelscript 在运行期更强调预编译制品、内存裁剪与热重载互斥

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: runtime mode selection / precompiled load-save / debug server gate
// 位置: 1425-1455, 1513-1598, 2052-2056
// ============================================================================
bGeneratePrecompiledData = RuntimeConfig.bGeneratePrecompiledData;
bScriptDevelopmentMode = RuntimeConfig.bIsEditor || RuntimeConfig.bDevelopmentMode;
bUsePrecompiledData = !bGeneratePrecompiledData && !RuntimeConfig.bIgnorePrecompiledData
    && !RuntimeConfig.bRunningCommandlet && !WITH_EDITOR && !bScriptDevelopmentMode;
// ★ precompiled 路径明确瞄准 non-editor / non-development runtime

if (bGeneratePrecompiledData)
{
    PrecompiledData = new FAngelscriptPrecompiledData(Engine);
    StaticJIT = new FAngelscriptStaticJIT();
    StaticJIT->PrecompiledData = PrecompiledData;
    Engine->SetJITCompiler(StaticJIT);
}
// ★ 编辑器/生成模式把 precompiled data 和 StaticJIT 输出一起产出

if ((!bUsePrecompiledData || bScriptDevelopmentMode) && FApp::HasProjectName())
{
    DebugServer = new FAngelscriptDebugServer(this, RuntimeConfig.DebugServerPort);
}
// ★ 用 precompiled data 跑正式运行时，会主动收缩调试面

if (bUsePrecompiledData)
{
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
    if (!bScriptDevelopmentMode)
        PrecompiledData->bMinimizeMemoryUsage = true;
}

if (bGeneratePrecompiledData)
{
    FString Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript.Cache");
    PrecompiledData->InitFromActiveScript();
    PrecompiledData->Save(Filename);
}

if (!bScriptDevelopmentMode && !bGeneratePrecompiledData)
    PrecompiledData->ClearUnneededRuntimeData();
// ★ 运行期优先保留可执行所需数据，开发态附加信息会被裁掉

UE_LOG(Angelscript, Warning, TEXT("Using fully precompiled scripts. Hot reloading is disabled for this run."));
// ★ precompiled runtime 和 hot reload 明确互斥
```

差距判断：

- 性能工作重心：`实现方式不同`。slua 把大量精力放在开发期观测链；Angelscript 把精力放在预编译执行制品和运行期路径裁剪。
- Shipping 内建 profiler hook：`没有实现`。slua 的 allocator 替换、`LuaProfiler::init/clean` 都被 `!UE_BUILD_SHIPPING` 包住，正式发布包不保留同级别 VM profiling hook。
- 运行期制品治理：`实现质量差异`。Angelscript 明确区分生成态与运行态，按 `PrecompiledScript_*` 文件名、`bUsePrecompiledData`、`ClearUnneededRuntimeData()` 收缩运行期表面积；slua 当前更像“开发期 profiler 很完整，但部署态性能策略主要留给外部脚本分发系统”。

---

## 深化分析 (2026-04-08 19:48:32)

### [维度 D3 / D2] `ULuaBlueprintLibrary` 提供的是“名字驱动的动态调用口”，而 Angelscript 维持“逐 `UFunction` 的静态签名面”

前几轮已经证明 `FLuaBPVar` 是 slua 的动态值信封。本轮补的是 Blueprint 调用面的抽象层级：`ULuaBlueprintLibrary` 只暴露 `CallToLuaWithArgs()` / `CallToLua()` 两个通用节点，调用时先按 `FString` 去 `LuaState` 里找函数，再把 `TArray<FLuaBPVar>` 逐个压栈。这不是“类型系统自动投影到 Blueprint”，而是“Blueprint 侧只保留一个动态入口，类型在运行时自行解释”。

Angelscript 的策略恰好相反。`Bind_BlueprintCallable.cpp` 先尝试拿 `UFunction` 的 direct native pointer，把每个 `UFunction` 绑定成独立静态声明；只有 direct pointer 缺失时才退到 reflective fallback。更关键的是，fallback 还会拒绝 `CustomThunk` 和超参数个数的函数。这说明 Angelscript 的目标是保住“逐函数签名面”，而不是把 Blueprint 调用统一折叠成一个动态节点。

```
[D3/D2] Blueprint Call Surface
sluaunreal
Blueprint Graph
├─ CallToLuaWithArgs(FunctionName, Args[])
│  ├─ FString -> LuaState::get(...)               // 名字查找
│  ├─ FLuaBPVar.value.push(...)                   // 参数统一压栈
│  └─ LuaVar::callWithNArg(...)                   // 运行时调用
└─ one generic node                               // 一个节点覆盖任意 Lua 函数

Angelscript
UFunction scan
├─ Bind_BlueprintCallable()
│  ├─ direct native pointer -> typed declaration  // 优先静态签名
│  └─ no pointer -> ReflectiveFallback            // 反射只做补位
│     ├─ reject CustomThunk
│     └─ limit arg count
└─ many typed methods                             // 每个 UFunction 一条签名
```

[1] 关键源码：slua 的 Blueprint 调用库把“函数名”和“动态值信封”当作正式 API

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaBlueprintLibrary.h
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaBlueprintLibrary.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaObject.cpp
// 函数: FLuaBPVar / ULuaBlueprintLibrary::CallToLuaWithArgs / LuaObject::getReferencer
// 位置: LuaBlueprintLibrary.h:21-31,42-46; LuaBlueprintLibrary.cpp:51-77,140-145; LuaObject.cpp:591-595,621-625
// ============================================================================
USTRUCT(BlueprintType)
struct SLUA_UNREAL_API FLuaBPVar {
    GENERATED_USTRUCT_BODY()
public:
    NS_SLUA::LuaVar value;
    static void* checkValue(NS_SLUA::lua_State* L, NS_SLUA::FStructProperty* p, uint8* params, int i);
}; // ★ Blueprint 可见的壳体里直接包着 LuaVar，而不是静态字段集合

UFUNCTION(BlueprintCallable, meta=( DisplayName="Call To Lua With Arguments", WorldContext = "WorldContextObject"), Category="slua")
static FLuaBPVar CallToLuaWithArgs(UObject* WorldContextObject, FString FunctionName,const TArray<FLuaBPVar>& Args,FString StateName);
// ★ API 只暴露“函数名 + 动态参数数组”，没有为每个 Lua 函数展开专用 pins

FLuaBPVar ULuaBlueprintLibrary::CallToLuaWithArgs(UObject* WorldContextObject, FString funcname,const TArray<FLuaBPVar>& args,FString StateName) {
    auto ls = LuaState::get(gameInstance);
    if (StateName.Len() != 0) ls = LuaState::get(StateName);
    if (!ls) return FLuaBPVar();
    LuaVar f = ls->get(TCHAR_TO_UTF8(*funcname));
    if (!f.isFunction()) {
        Log::Error("Can't find lua member function named %s to call", TCHAR_TO_UTF8(*funcname));
        return LuaVar();
    }

    auto fillParam = [&]
    {
        for (auto& arg : args) {
            arg.value.push(ls->getLuaState()); // ★ 每个参数都作为 LuaVar 动态压栈
        }
        return args.Num();
    };
    return f.callWithNArg(fillParam);
}

void* FLuaBPVar::checkValue(NS_SLUA::lua_State* L, NS_SLUA::FStructProperty* p, uint8* params, int i)
{
    FLuaBPVar ret;
    ret.value.set(L, i); // ★ 反向桥接时也只是把 Lua 栈位封进 FLuaBPVar
    p->CopyCompleteValue(params, &ret);
    return nullptr;
}

LuaObject::ReferencePropertyFunction LuaObject::getReferencer(FProperty* prop) {
    auto sp = CastField<FStructProperty>(prop);
    if (sp && sp->Struct == FLuaBPVar::StaticStruct())
        return nullptr; // ★ FLuaBPVar 被显式踢出普通 by-ref UStruct 路径
    return getReferencer(prop->GetClass());
}
```

[2] 关键源码：Angelscript 仍然优先保住逐 `UFunction` 的静态声明；反射只是后备路径

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp
// 函数: direct bind / reflective fallback eligibility / invocation
// 位置: Bind_BlueprintCallable.cpp:72-90,95-151; BlueprintCallableReflectiveFallback.cpp:254-287,290-419
// ============================================================================
auto* DirectNativePointer = &Entry->FuncPtr;
const bool bHasDirectNativePointer = DirectNativePointer != nullptr && DirectNativePointer->IsBound();
if (!bHasDirectNativePointer)
{
    if (!BindBlueprintCallableReflectiveFallback(InType, Function, Signature, *Entry))
        return;
    return;
}
// ★ 先问“能不能做 direct bind”，只有不行才进入 fallback

if (Signature.bStaticInScript)
{
    int GlobalFunctionId = FAngelscriptBinds::BindGlobalFunction(Signature.Declaration, ASFuncPtr, Entry->Caller);
    Signature.ModifyScriptFunction(GlobalFunctionId);
}
else
{
    int FunctionId = FAngelscriptBinds::BindMethodDirect(
        InType->GetAngelscriptTypeName(),
        Signature.Declaration, ASFuncPtr, asCALL_THISCALL, Entry->Caller);
    Signature.ModifyScriptFunction(FunctionId);
}
// ★ direct bind 成功后，脚本侧拿到的是逐函数静态声明

if (Function->HasMetaData(NAME_BlueprintCallableReflectiveFallback_CustomThunk))
{
    return EAngelscriptReflectiveFallbackEligibility::RejectedCustomThunk;
}

if (GetNonReturnParameterCount(Function) > BlueprintCallableReflectiveFallbackMaxArgs)
{
    return EAngelscriptReflectiveFallbackEligibility::RejectedTooManyArguments;
}
// ★ fallback 不是兜底一切；CustomThunk 和超长参数函数会被主动拒绝

uint8* ParameterBuffer = static_cast<uint8*>(FMemory_Alloca(Function->ParmsSize));
InitializeParameterBuffer(Function, ParameterBuffer);
...
TargetObject->ProcessEvent(Function, ParameterBuffer);
...
if (!BindReflectiveFunction(InType, Signature, ReflectiveSignature))
{
    delete ReflectiveSignature;
    return false;
}
// ★ 即使退到反射路径，仍然先构造 Signature，再把 ProcessEvent 包成受限的 typed bridge
```

差距判断：

- Blueprint 调用入口模型：`实现方式不同`。slua 把 Blueprint -> Lua 收敛成“函数名 + `FLuaBPVar[]`”；Angelscript 把大多数 BlueprintCallable 维持为逐函数静态声明。
- `CustomThunk` 兼容策略：`实现方式不同`。Angelscript 明确把 `CustomThunk` 视为 reflective fallback 的拒绝条件；slua 的 `CallToLuaWithArgs()` 没有同级别的 `UFunction` 形状筛选，因为它根本不以 `UFunction` 作为 Blueprint -> Lua 的入口单位。
- Blueprint 作者期的可发现性：`实现质量差异`。slua 的动态节点更统一，但无法提供与每个 `UFunction` 一一对应的 pins/签名；Angelscript 的脚本侧 API 面更可枚举、更适合静态审计。

### [维度 D2 / D8] `LuaWrapper*.inc` 生成的是“每个 UE API 一段专用 thunk”；Angelscript `StaticJIT` 编译的是“每个脚本函数的执行路径”

前几轮已经确认 slua 有静态导出。本轮补的是“导出产物到底长什么样”。`LuaWrapper5.2.inc` 里每个 wrapper 都是完整的 `lua_CFunction` thunk：显式 `CheckSelf()`、显式 `checkValue<T>()`、显式重载分支、显式返回值构造，最后在 `bind()` 里逐个挂进类型表。它优化的是“已知 UE API 被 Lua 调用时的桥接开销”。

Angelscript 的 `StaticJIT` 不是这类 API wrapper 生成器。`asCScriptFunction::JITCompile()` 把每个脚本函数交给 JIT 编译器；`MakeNativeCall()` 再根据脚本栈布局、对象指针、返回值位置和参数类型，生成原生调用表达式。它优化的是“脚本函数本身如何执行与调用 native”。因此，slua 的静态导出和 Angelscript 的 `StaticJIT` 都在降本，但不在同一层上。

```
[D2/D8] Optimization Target
sluaunreal
wrapper generator
├─ emit LuaWrapper5.2.inc
├─ static int RandRange(lua_State* L)
│  ├─ CheckSelf(FRandomStream)                     // 自对象检查
│  ├─ checkValue<int>(L, 2/3)                     // 参数拆箱
│  └─ LuaObject::push(ret)                        // 返回值封箱
└─ bind() -> addMethod("RandRange", RandRange)    // 注册到 Lua 类型表

Angelscript
script compile
├─ asCScriptFunction::JITCompile()
├─ FStaticJITContext::MakeNativeCall()
│  ├─ decode script stack slots                   // 读取脚本栈布局
│  ├─ NativeForm->GenerateCall(...)               // 生成原生调用表达式
│  └─ emit C++ native path                        // 输出 native 风格代码
└─ compiled function body runs native-style       // 优化对象是脚本函数执行
```

[1] 关键源码：slua 的静态导出产物把参数拆装、重载分派和注册都写死在 wrapper 里

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaWrapper5.2.inc
// 函数: FRandomStream wrapper thunks / bind
// 位置: 3752-3905
// ============================================================================
static int GetUnsignedInt(lua_State* L) {
    {
        CheckSelf(FRandomStream);
        auto ret = self->GetUnsignedInt();
        LuaObject::push(L, ret);
        return 1;
    }
}

static int RandRange(lua_State* L) {
    {
        CheckSelf(FRandomStream);
        auto Min = LuaObject::checkValue<int>(L, 2);
        auto Max = LuaObject::checkValue<int>(L, 3);
        auto ret = self->RandRange(Min, Max);
        LuaObject::push(L, ret);
        return 1;
    }
}

static int VRandCone(lua_State* L) {
    auto argc = lua_gettop(L);
    if (argc == 3) {
        CheckSelf(FRandomStream);
        auto Dir = LuaObject::checkValue<FVector*>(L, 2);
        ...
        *ret = self->VRandCone(DirRef, ConeHalfAngleRad);
        LuaObject::push<FVector>(L, "FVector", ret, UD_AUTOGC | UD_VALUETYPE);
        return 1;
    }
    if (argc == 4 || argc == 5) {
        CheckSelf(FRandomStream);
        ...
        *ret = self->VRandCone(DirRef, HorizontalConeHalfAngleRad, VerticalConeHalfAngleRad);
        ...
        return 1;
    }
    luaL_error(L, "call FRandomStream::VRandCone error, argc=%d", argc);
    return 0;
}
// ★ 重载分派、参数检查和返回值构造都在生成物里展开，调用期不再查询 UFunction 元数据

static void bind(lua_State* L) {
    AutoStack autoStack(L);
    LuaObject::newType(L, "FRandomStream");
    LuaObject::addMethod(L, "Initialize", Initialize, true);
    ...
    LuaObject::addMethod(L, "GetUnsignedInt", GetUnsignedInt, true);
    ...
    LuaObject::addMethod(L, "RandRange", RandRange, true);
    ...
    LuaObject::addMethod(L, "VRandCone", VRandCone, true);
}
// ★ bind() 最终把这些 thunk 作为类型方法逐个注册
```

[2] 关键源码：Angelscript `StaticJIT` 的工作单元是脚本函数，不是 UE API wrapper

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_scriptfunction.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp
// 函数: asCScriptFunction::JITCompile / MakeNativeCall
// 位置: as_scriptfunction.cpp:1548-1559; AngelscriptBytecodes.cpp:305-473
// ============================================================================
void asCScriptFunction::JITCompile()
{
    if( funcType != asFUNC_SCRIPT )
        return;
    asIJITCompiler *jit = engine->GetJITCompiler();
    if( !jit )
        return;
    int r = jit->CompileFunction(this, &jitFunction);
}
// ★ 进入 JIT 的前提是“脚本函数”，不是某个 UFunction wrapper

void MakeNativeCall(FStaticJITContext& Context, FNativeFunctionContext& NativeContext)
{
    AddNativeHeaders(Context);
    ...
    if (HasObjectPointer())
    {
        Context.Line("  {0} Object = ({0}){1};", NativeObjectType, Context.StackValue_At(StackOffset, 2));
        ...
        NativeContext.ObjectAddress = TEXT("Object");
    }
    ...
    for (int32 i = 0, Count = ArgumentTypes.Num(); i < Count; ++i)
    {
        ...
        NativeContext.ArgumentValues.Add(
            FString::Printf(
                TEXT("value_as<%s>(%s)"),
                *ArgumentTypes[i].GetNativeForm(),
                *Context.StackValue_At(StackOffset, ScriptParam.GetSizeOnStackDWords())
            )
        );
        StackOffset += ScriptFunction->parameterTypes[i].GetSizeOnStackDWords();
    }

    FNativeFunctionCall Call = NativeForm->GenerateCall(NativeContext);
    if (Call.Header.Len() != 0)
        Context.AddHeader(Call.Header);
}
// ★ StaticJIT 先读取脚本栈和 script signature，再拼出原生调用表达式
```

差距判断：

- 优化对象：`实现方式不同`。slua 的静态导出优化“Lua 调 UE API 的桥”；Angelscript 的 `StaticJIT` 优化“脚本函数执行和 native call lowering”。
- 生成物形态：`实现方式不同`。slua 生成海量 `lua_CFunction` thunk；Angelscript 生成的是脚本函数级 JIT/native form 代码路径。
- 维护成本结构：`实现质量差异`。slua 把复杂度堆到 wrapper 数量和版本分支上；Angelscript 把复杂度堆到 JIT 编译器、栈布局和 `NativeForm` lowering 规则上。两者都复杂，但故障面完全不同。

### [维度 D4 / D11] slua 真正拥有的是“对象级 hook 退场”，而 Angelscript 真正拥有的是“文件级 hot reload 事务”

前几轮已经谈过 slua 的 `LoadFileDelegate` 边界。本轮补的是“谁来宣布一次 reload 事务开始和结束”。示例工程 `UMyGameInstance::CreateLuaState()` 只负责把模块名映射到本地 `.lua/.luac` 字节；插件运行时这边，`LuaOverrider` 构造时注册的是 `UObject` 创建/删除监听、async loading flush 和 post-GC 回调。也就是说，slua 插件内真正能保证一致性的单元，是“某个对象/类什么时候接管、什么时候退场”，而不是“哪些文件属于同一补丁批次”。

Angelscript 则把文件集合当成正式事务。Editor 模块先在所有 script root 上注册 `DirectoryWatcher`；Runtime 的 `CheckForHotReload()` 消费新增/删除队列；`PerformHotReload()` 再扩散依赖闭包、做预处理、编译，并把受影响模块送给 `HotReloadTestRunner`。这条链路回答的是“哪些文件需要一起重载、失败时保留旧代码、重载后怎么验证”，而不只是“对象被删了怎么清理 hook”。

```
[D4/D11] Reload Ownership Unit
sluaunreal
project loader
├─ LoadFileDelegate(fn) -> .lua/.luac bytes       // 项目自己定义字节来源
runtime hook layer
├─ AddUObjectCreate/DeleteListener                // 监听对象生命周期
├─ OnAsyncLoadingFlushUpdate                      // 等待异步加载稳定
├─ PostGarbageCollect callback                    // GC 后清理
└─ NotifyUObjectDeleted()
   ├─ removeObjectTable(obj)
   ├─ removeOneOverride(cls)
   └─ drop class constructor cache

Angelscript
editor/runtime transaction
├─ DirectoryWatcher -> queue file changes
├─ CheckForHotReload()
├─ PerformHotReload()
│  ├─ expand dependent modules
│  ├─ preprocess changed files
│  ├─ compile modules
│  └─ queue tests after reload
└─ transaction unit = file/module set
```

[1] 关键源码：slua 的 sample loader 只喂字节；runtime 核心只维护对象级接管与退场

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Source/democpp/MyGameInstance.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaOverrider.cpp
// 函数: UMyGameInstance::CreateLuaState / LuaOverrider ctor / NotifyUObjectDeleted
// 位置: MyGameInstance.cpp:36-64; LuaOverrider.cpp:533-553,577-580,643-659
// ============================================================================
void UMyGameInstance::CreateLuaState()
{
    ...
    state = new NS_SLUA::LuaState("SLuaMainState", this);
    state->setLoadFileDelegate([](const char* fn, FString& filepath)->TArray<uint8> {
        FString path = FPaths::ProjectContentDir();
        ...
        TArray<FString> luaExts = { UTF8_TO_TCHAR(".lua"), UTF8_TO_TCHAR(".luac") };
        for (auto& it : luaExts) {
            auto fullPath = path + *it;
            FFileHelper::LoadFileToArray(Content, *fullPath);
            if (Content.Num() > 0) {
                filepath = fullPath;
                return MoveTemp(Content);
            }
        }
        return MoveTemp(Content);
    });
    state->init();
}
// ★ sample 工程只回答“从哪里拿脚本字节”，不回答“哪些文件组成一次事务”

LuaOverrider::LuaOverrider(NS_SLUA::LuaState* luaState)
    : sluaState(luaState)
{
    ...
    GUObjectArray.AddUObjectDeleteListener(this);
    GUObjectArray.AddUObjectCreateListener(this);
    asyncLoadingFlushUpdateHandle = FCoreDelegates::OnAsyncLoadingFlushUpdate.AddRaw(this, &LuaOverrider::onAsyncLoadingFlushUpdate);
    gcHandler = FCoreUObjectDelegates::GetPostGarbageCollect().AddRaw(this, &LuaOverrider::onEngineGC);
    ...
}
// ★ runtime 自己拥有的是 UObject 生命周期和加载稳定点

void LuaOverrider::NotifyUObjectDeleted(const UObjectBase* Object, int32 Index)
{
    UObject* obj = (UObject*)Object;
    ULuaOverrider::removeObjectTable(obj);

    UClass* cls = (UClass*)Object;
    if (cls)
    {
        if (overridedClasses.Contains(cls))
        {
            removeOneOverride(cls, true);
            overridedClasses.Remove(cls);
        }

        FRWScopeLock lock(classHookMutex, SLT_Write);
        classConstructors.Remove(cls);
    }
}
// ★ 回滚单元是对象/类：删对象就删 table，删类就撤 override 和 constructor cache
```

[2] 关键源码：Angelscript 把文件变化、依赖扩散和热重载后验证串成同一条事务链

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: DirectoryWatcher registration / CheckForHotReload / PerformHotReload
// 位置: AngelscriptEditorModule.cpp:366-381; AngelscriptEngine.cpp:2253-2490,2729-2828
// ============================================================================
FDirectoryWatcherModule& DirectoryWatcherModule = FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>("DirectoryWatcher");
IDirectoryWatcher* DirectoryWatcher = DirectoryWatcherModule.Get();
...
TArray<FString> AllRootPaths = FAngelscriptEngine::MakeAllScriptRoots();
for (const auto& RootPath : AllRootPaths)
{
    DirectoryWatcher->RegisterDirectoryChangedCallback_Handle(
        *RootPath,
        IDirectoryWatcher::FDirectoryChanged::CreateStatic(&OnScriptFileChanges),
        WatchHandle,
        IDirectoryWatcher::IncludeDirectoryChanges);
}
// ★ 先把文件变化收敛成正式队列

void FAngelscriptEngine::CheckForHotReload(ECompileType CompileType)
{
    TArray<FFilenamePair> FileList;
    FileList.Append(FileChangesDetectedForReload);
    FileChangesDetectedForReload.Empty();
    ...
    if (FileList.Num() != 0)
    {
        PerformHotReload(CompileType, FileList);
    }
}
// ★ Runtime 明确消费“新增/删除文件队列”，不是只等对象事件

bool FAngelscriptEngine::PerformHotReload(ECompileType CompileType, const TArray<FFilenamePair>& InReloadFiles)
{
    ...
    TSet<FFilenamePair> FilesToHotReload;
    ...
    // Build a set of all files which are dependent on any of the modified files
    ...
    for (const auto& PathPair : FilesToHotReload)
    {
        Preprocessor.AddFile(PathPair.RelativePath, PathPair.AbsolutePath, bTreatAsDeleted);
    }

    bool bPreprocessSuccess = Preprocessor.Preprocess();
    ...
    ECompileResult Result = CompileModules(CompileType, Preprocessor.GetModulesToCompile(), CompiledModules);
    ...
    if (GEngine && bCompletedAssetScan && HotReloadTestRunner != nullptr && HotReloadTestRunner->ShouldRunUnitTestsOnHotReload())
    {
        ...
        HotReloadTestRunner->PrepareTests(GetActiveModules(), CompiledModules, RelativeFileList, ShouldUseAutomaticImportMethod());
    }
}
// ★ 一次 hot reload 事务里同时包含依赖扩散、预处理、编译和重载后测试准备
```

差距判断：

- 事务拥有者：`实现方式不同`。slua 核心拥有对象级 hook 生命周期；Angelscript 核心拥有文件/模块级 hot reload 事务。
- 插件内建文件监听与依赖扩散：`没有实现`。本轮所读 slua 插件源码里，热更入口仍然只是 `LoadFileDelegate` + 项目侧 loader；未见与 `DirectoryWatcher + FilesToHotReload` 对等的插件内事务层。
- 热重载后自动验证：`没有实现`。Angelscript 会把受影响模块送进 `HotReloadTestRunner`；slua 当前可见路径没有同级别的 reload 后测试门禁。

---

## 深化分析 (2026-04-08 19:56:58)

本轮不重述前文已经展开的 `LuaFunctionAccelerator`、`hookBpScript()` 和 `PerformHotReload()` 主链，只补三个此前没有拆开的实现边界：`lua_wrapper` 的触发方式、Profiler 的完整数据通路、以及脚本交付/打包真正落在哪一层。

### [维度 D1 / D2] `lua_wrapper` 实际是独立 Editor 插件壳，静态导出并不属于 `slua_unreal` 的编译期

前文已经指出 `Tools/config.json` 是外置配置。本轮补充的关键事实是：slua 把“静态导出”进一步拆成了一个单独的 `Plugins/lua_wrapper/` Editor 插件。这个插件在 `LevelEditor` 菜单和 Toolbar 上挂按钮，点击后直接执行 `Tools/lua-wrapper.exe` 或 `mono lua-wrapper.exe`；真正的 runtime 侧只负责 `#include "LuaWrapper5.x.inc"` 并调用生成好的注册函数。换句话说，slua 的静态导出是“编辑器壳触发的离线生成步骤”，不是 `slua_unreal` 模块在 UBT/UHT 周期里的强绑定环节。

这和 Angelscript 的 function table 生成边界明显不同。`AngelscriptFunctionTableExporter` 用 `[UhtExporter(... CompileOutput ...)]` 把 `AS_FunctionTable_*.cpp` 声明为 UHT 编译产物，意味着生成失败会直接回到构建系统；slua 的失败面则更多落在“按钮有没有点、路径对不对、外部 exe/mono 能不能起、生成文件是否与当前引擎版本匹配”。

```
[D1/D2] Static Export Ownership
sluaunreal
├─ lua_wrapper (Editor plugin)                      // 编辑器里的工具壳
│  ├─ LevelEditor menu / toolbar                   // 人工触发入口
│  └─ system(".../Tools/lua-wrapper.exe")          // 启动外部生成器
├─ Tools/config.json                               // 生成规则与 include/preprocess
├─ LuaWrapper5.x.inc                               // 已生成的 wrapper 产物
└─ slua_unreal runtime
   ├─ #include versioned .inc                      // 编译期吃现成产物
   └─ LuaClass::reg()                              // 运行时枚举 setup 函数

Angelscript
├─ UhtExporter(CompileOutput)                      // UHT 编译阶段内建
├─ generate AS_FunctionTable_*.cpp                 // 生成物是正式编译输出
└─ AngelscriptRuntime bind registration            // 同一条构建链闭环
```

[1] 关键源码：`lua_wrapper` 只是 Editor 壳，按钮动作是直接拉起外部生成器

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/lua_wrapper/lua_wrapper.uplugin
// 文件: Reference/sluaunreal/Plugins/lua_wrapper/Source/lua_wrapper/Private/lua_wrapper.cpp
// 函数: plugin descriptor / Flua_wrapperModule::StartupModule / PluginButtonClicked
// 位置: lua_wrapper.uplugin:16-21; lua_wrapper.cpp:42-59,122-133
// ============================================================================
{
  "Name": "lua_wrapper",
  "Type": "Editor",
  "LoadingPhase": "Default"
} // ★ 这是独立 Editor 插件，不属于 slua runtime 模块本体

FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
...
LevelEditorModule.GetToolBarExtensibilityManager()->AddExtender(ToolbarExtender);
FGlobalTabmanager::Get()->RegisterNomadTabSpawner(lua_wrapperTabName, ...);
// ★ 生成入口挂到 LevelEditor 菜单/工具栏，触发方式是编辑器操作，不是 UBT/UHT 回调

auto contentDir = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*FPaths::ProjectContentDir());
auto cmd = contentDir + TEXT("/../Tools/lua-wrapper.exe");
system(TCHAR_TO_UTF8(*cmd));
// ★ Windows 下直接 system() 拉起 Tools/lua-wrapper.exe

auto toolsDir = contentDir + TEXT("../Tools/");
chdir(TCHAR_TO_UTF8(*toolsDir));
auto ret = exec("/Library/Frameworks/Mono.framework/Versions/Current/Commands/mono lua-wrapper.exe");
// ★ macOS 下则切目录后走 mono，说明生成器完全是外部进程
```

[2] 关键源码：runtime 侧只消费已经生成好的 `.inc` 与 setup 函数列表

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaWrapper.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaCppBinding.cpp
// 函数: versioned wrapper include / LuaWrapper::initExt / LuaClass::reg
// 位置: LuaWrapper.cpp:55-67,184-188; LuaCppBinding.cpp:18-30
// ============================================================================
#if ((ENGINE_MINOR_VERSION==2) && (ENGINE_MAJOR_VERSION==5))
    #include "LuaWrapper5.2.inc"
#elif ((ENGINE_MINOR_VERSION==3) && (ENGINE_MAJOR_VERSION==5))
    #include "LuaWrapper5.3.inc"
#elif ((ENGINE_MINOR_VERSION==4) && (ENGINE_MAJOR_VERSION==5))
    #include "LuaWrapper5.4.inc"
#endif
// ★ runtime 只按 UE 版本选中已经生成好的 wrapper 产物

void LuaWrapper::initExt(lua_State* L)
{
    init(L);
    FSoftObjectPtrWrapper::bind(L);
}
// ★ 这里没有生成行为，只有“把生成物注册进 Lua”的消费行为

TArray<lua_CFunction> *luaclasses = nullptr;
LuaClass::LuaClass(lua_CFunction setup) {
    if(!luaclasses) luaclasses = new TArray<lua_CFunction>();
    luaclasses->Add(setup);
}
void LuaClass::reg(lua_State* L) {
    if(!luaclasses)
        return;
    for(auto it:*luaclasses)
        it(L);
}
// ★ LuaCppBinding 也是把 setup 函数收集起来，运行时逐个执行，而不是在此处生成代码
```

[3] 对照源码：Angelscript 把 function table 生成直接挂进 UHT `CompileOutput`

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs
// 函数: Export
// 位置: 21-26
// ============================================================================
[UhtExporter(
    Name = "AngelscriptFunctionTable",
    Description = "Exports Angelscript function table data",
    Options = UhtExporterOptions.Default | UhtExporterOptions.CompileOutput,
    CppFilters = ["AS_FunctionTable_*.cpp"],
    ModuleName = "AngelscriptRuntime")]
// ★ 生成物被声明为正式编译输出，生成和编译属于同一条工具链
```

与 Angelscript 对比：

| 观察点 | sluaunreal | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 代码生成触发者 | `lua_wrapper` Editor 插件按钮触发外部 exe（`lua_wrapper.cpp:42-59,122-133`） | UHT exporter 自动生成 compile output（`AngelscriptFunctionTableExporter.cs:21-26`） | 实现方式不同 |
| runtime 对生成物的关系 | 只 `#include` 版本化 `.inc` 并执行 setup（`LuaWrapper.cpp:55-67,184-188`; `LuaCppBinding.cpp:18-30`） | 生成物本身就是 Runtime 编译单元 | 实现方式不同 |
| 失败暴露面 | 容易出现路径、Mono、按钮触发、生成物陈旧等工具链漂移 | 生成失败更早在构建阶段暴露 | 实现质量差异：Angelscript 的自动化闭环更强 |

### [维度 D8] slua 的 Profiler 不是单点 API，而是 “Lua VM hook -> 后台线程聚合 -> TCP/Editor UI” 的整链实现

新增发现是：slua 的 profiler 不只是 `LuaProfiler.cpp` 里的 hook。`slua_unreal` 模块启动时先建 `SluaProfilerDataManager`；`LuaState` 在非 shipping 配置下改用 `LuaMemoryProfile::alloc` 分配器并初始化 profiler；`LuaProfiler::init()` 再把内嵌 `ProfilerScript` 注入 Lua 全局 `slua_profile`。之后每帧 `tick()` 会根据连接状态决定走“远端 socket 实时采样”还是“本地录制到 `SluaProfilerDataManager`”。数据管理器内部再起 `FProfileDataProcessRunnable` 线程，把 call tree 和内存帧按 `PHE_TICK` 合并、压缩、落盘；Editor 侧 `slua_profile` 模块注册 Nomad Tab，通过 `FTicker` 消费队列并展示；`slua_remote_profile` 还自带 TCP listener，默认端口 `8081`。

也就是说，slua 的 profiler 设计目标不是“给脚本暴露一个 CPU trace scope”，而是“提供 Lua 专属的采样、录制、回放和远端查看链路”。这与 Angelscript 当前更偏“借用 UE profiling primitive + dump StaticJIT/PrecompiledData 状态”的路线明显不同。

```
[D8] Profiler Pipeline
slua_unreal runtime
├─ StartupModule -> StartManager()                 // 启动共享数据管理器
├─ LuaState init (non-shipping)                    // profiler allocator + hook 初始化
├─ LuaProfiler::init()                             // 注入 slua_profile 全局
├─ LuaProfiler::tick()
│  ├─ CONNECTED -> socket / remote sample         // 远端实时模式
│  └─ offline -> SluaProfilerDataManager          // 本地录制模式
└─ FProfileDataProcessRunnable                     // 后台线程聚合 call tree / memory frame

slua_profile editor
├─ FTicker tick                                    // UI 帧更新
├─ SProfilerInspector                              // 录制/读取/图表展示
└─ FProfileServer(0.0.0.0:8081)                    // 远端 profile server

Angelscript
├─ FCpuProfilerTraceScoped                         // 绑定 UE CPU trace scope
└─ AngelscriptStateDump                            // 导出 PrecompiledData/StaticJIT 状态表
```

[1] 关键源码：runtime 侧把 profiler 挂进 LuaState 生命周期，而不是单独的调试辅助函数

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/slua_unreal.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaState.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaProfiler.cpp
// 函数: Fslua_unrealModule::StartupModule / LuaState init-tick-clean / LuaProfiler::init-tick-clean
// 位置: slua_unreal.cpp:20-27; LuaState.cpp:328-332,499-503,559-563,628-630;
//       LuaProfiler.cpp:441-499,506-517
// ============================================================================
void Fslua_unrealModule::StartupModule()
{
    ...
    SluaProfilerDataManager::StartManager();
}
// ★ runtime 模块启动时就创建 profiler 共享管理器

#if ENABLE_PROFILER && !UE_BUILD_SHIPPING
    L = lua_newstate(LuaMemoryProfile::alloc,this);
#else
    L = luaL_newstate();
#endif
// ★ 非 shipping 模式直接切换到 profiler-aware allocator

#ifdef ENABLE_PROFILER
#if !UE_BUILD_SHIPPING
        LuaProfiler::init(this);
#endif
#endif
// ★ LuaState 初始化阶段就注入 profiler

#ifdef ENABLE_PROFILER
#if !UE_BUILD_SHIPPING
        LuaProfiler::tick(this);
#endif
#endif
// ★ profiler 每帧跟随 LuaState::tick() 驱动

auto& profiler = selfProfiler.Add(LS);
profiler = LS->doBuffer((const uint8*)ProfilerScript,strlen(ProfilerScript), ChunkName);
...
lua_setglobal(L, "slua_profile");
// ★ 把内嵌脚本注册成全局 slua_profile，Lua 侧与 C++ hook 共用一套状态

RunState currentRunState = (RunState)profiler.getFromTable<int>("currentRunState");
if (currentRunState == RunState::CONNECTED) {
    if(checkSocketRead()) memoryGC(L);
    takeMemorySample(PHE_MEMORY_INCREACE, LuaMemoryProfile::memIncreaceThisFrame(LS), L);
    takeSample(PHE_TICK, -1, "", "", getTime(), L);
}
else
{
    SluaProfilerDataManager::ReceiveMemoryData(PHE_MEMORY_INCREACE, LuaMemoryProfile::memIncreaceThisFrame(LS));
    SluaProfilerDataManager::ReceiveProfileData(PHE_TICK, getTime() - profileTotalCost, -1, "", "");
}
// ★ 同一套 hook 可以切到远端实时模式，也可以退回本地录制模式

void LuaProfiler::clean(LuaState* LS)
{
    ...
    SluaProfilerDataManager::EndRecord();
}
// ★ LuaState 销毁时同步结束录制
```

[2] 关键源码：Profiler 数据不会直接在游戏线程上整形，而是交给后台线程、TCP server 和 Editor Tab

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/SluaProfilerDataManager.h
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/SluaProfilerDataManager.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_profile/Private/slua_profile.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_profile/Private/slua_remote_profile.cpp
// 函数: SluaProfilerDataManager API / FProfileDataProcessRunnable::Run /
//       Fslua_profileModule::StartupModule / FProfileServer::Init
// 位置: SluaProfilerDataManager.h:118-147; SluaProfilerDataManager.cpp:89-98,237-284,401-447;
//       slua_profile.cpp:48-83,109-120; slua_remote_profile.cpp:22-30,52-60,139-159
// ============================================================================
static void BeginRecord();
static void EndRecord();
static bool IsRecording();
static void ReceiveProfileData(int hookEvent, int64 time, int lineDefined, const FString& funcName, const FString& shortSrc);
static void ReceiveMemoryData(int hookEvent, const TArray<NS_SLUA::LuaMemInfo>& memInfoList);
// ★ 对外 API 已经是“录制器/流处理器”语义，不是单次统计函数

if (!ProcessRunnable)
{
    ProcessRunnable = new FProfileDataProcessRunnable();
}
if (ProcessRunnable)
{
    ProcessRunnable->StartRecord();
}
// ★ 录制开始时才真正拉起后台 runnable

SluaProfilerDataManager::InitProfileNode(funcProfilerRoot, *FLuaFunctionDefine::Root, 0);
WorkerThread = FRunnableThread::Create(this, TEXT("FProfileDataProcessRunnable"));
...
while (bCanStartFrameRecord && frameArchive && !funcProfilerNodeQueue.IsEmpty())
{
    ...
    PreProcessData(funcProfilerNode, memoryInfo, memoryFrame);
}
// ★ 每帧在后台线程里把 call tree 和 memory frame 合并、预处理、压缩

else if (hookEvent == NS_SLUA::ProfilerHookEvent::PHE_TICK)
{
    funcProfilerNodeQueue.Enqueue(funcProfilerRoot);
    memoryQueue.Enqueue(currentMemory);
    ClearCurProfiler();
}
// ★ tick 事件是“把一帧样本封包入队”的切分点

if (GIsEditor && !IsRunningCommandlet())
{
    sluaProfilerInspector = MakeShareable(new SProfilerInspector);
    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(slua_profileTabName, ...);
    TickDelegateHandle = FTSTicker::GetCoreTicker().AddTicker(TickDelegate);
}
// ★ Editor 侧自带可视化面板与 ticker，而不是只输出日志

int32 FProfileServer::Port = 8081;
ListenEndpoint.Address = FIPv4Address(0, 0, 0, 0);
Listener = new FTcpListener(ListenEndpoint);
Listener->OnConnectionAccepted().BindRaw(this, &FProfileServer::HandleConnectionAccepted);
// ★ 远端 profile server 直接内建在插件里，默认监听 8081

Socket->SetReceiveBufferSize(2 * 1024 * 1024, NewSize);
Socket->SetSendBufferSize(2 * 1024 * 1024, NewSize);
// ★ 连接建立后立即调大收发 buffer，目标就是承载连续 profiler 流量
```

[3] 对照源码：Angelscript 当前更像“暴露 UE profiler primitive + dump 状态表”，而不是提供专用远端采样链

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/FCpuProfilerTraceScoped.h
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FCpuProfilerTraceScoped.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp
// 函数: FCpuProfilerTraceScoped / bind / DumpPrecompiledData
// 位置: FCpuProfilerTraceScoped.h:8-26; Bind_FCpuProfilerTraceScoped.cpp:4-13;
//       AngelscriptStateDump.cpp:180-181,1038-1064
// ============================================================================
FCpuProfilerTraceScoped(const FName& EventID)
{
    FCpuProfilerTrace::OutputBeginDynamicEvent(EventID);
}
~FCpuProfilerTraceScoped()
{
    FCpuProfilerTrace::OutputEndEvent();
}
// ★ 这是对 UE CPU trace primitive 的薄封装

auto FCpuProfilerTraceScoped_ = FAngelscriptBinds::ExistingClass("FCpuProfilerTraceScoped");
FCpuProfilerTraceScoped_.Constructor("void f(const FName& EventID)", [](FCpuProfilerTraceScoped* Address, const FName& EventID)
{
    new(Address) FCpuProfilerTraceScoped(EventID);
});
// ★ Angelscript 目前把这个 primitive 绑定给脚本使用，但没有形成 slua 那种专用 profile pipeline

TableResults.Add(DumpPrecompiledData(Engine, ResolvedOutputDir));
TableResults.Add(DumpStaticJITState(Engine, ResolvedOutputDir));
...
return SaveTable(OutputDir, TEXT("PrecompiledData.csv"), Writer);
// ★ 更强的是状态导出与离线审计，而不是 Lua 那种 TCP 实时采样面板
```

与 Angelscript 对比：

| 观察点 | sluaunreal | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| profiler 入口 | `LuaState` 生命周期内建 profiler init/tick/clean（`LuaState.cpp:328-332,499-503,628-630`） | 脚本可手动构造 `FCpuProfilerTraceScoped`（`FCpuProfilerTraceScoped.h:14-23`; `Bind_FCpuProfilerTraceScoped.cpp:4-13`） | 实现方式不同 |
| 数据处理 | 后台线程聚合 call tree + memory frame（`SluaProfilerDataManager.cpp:237-284,401-447`） | 当前可见重点是状态 dump 和 trace primitive | 实现方式不同 |
| 远端/可视化 | 内建 `FTcpListener` + `SProfilerInspector`（`slua_remote_profile.cpp:52-60`; `slua_profile.cpp:70-83`） | 本轮所读源码未见对等的专用远端 profiler UI | 没有实现 |

### [维度 D11] slua 的“线上热更新”真正落在 `LoadFileDelegate`，插件本身不拥有加密/签名/包清单

本轮把部署链拆得更细之后，可以更准确地区分“能力边界”和“实现缺口”。slua runtime 真正要求的只是一个 `LoadFileDelegate`，返回 `TArray<uint8>` 和最终路径；示例工程里这个 delegate 先找 `.lua`，再找 `.luac`。拿到字节后，`LuaState::doFile()` 直接喂给 `luaL_loadbuffer()`，`requireModule()` 也只是调用 Lua 自带 `require`。因此，slua 支持线上热更的关键不是插件内置了包管理器，而是它把“字节从哪来”完全外包给宿主工程或上层分发系统。

这也解释了为什么前文 D4 里 slua 没有文件事务层。因为它的部署语义在 bytes 级 callback，而不是脚本文件集合级 transaction。继续往下看，vendored Lua 明明暴露了 `lua_setonlyluac()` 开关，但仓库内检索 `lua_setonlyluac(` 只有 `External/lua/lstate.cpp:239-241` 这一处定义，`Source/slua_unreal/` 没有调用点；本轮也未见脚本签名校验、解密入口或补丁清单解析代码。也就是说，slua 具备“上层可以喂 `.luac`/网络字节包”的接口，但“字节是否加密、是否签名、如何校验版本”都不由插件核心承担。

Angelscript 在部署上的差异不是“更会做加密”，而是“把交付物收敛成 build-aware cache”。启动期会决定 `bGeneratePrecompiledData` / `bUsePrecompiledData`，然后加载 `Binds.Cache` 与 `PrecompiledScript*.Cache`，并用 `IsValidForCurrentBuild()` 做构建配置校验。这使它更像“随构建产物分发的编译缓存系统”，而不是“等待运行时下发脚本字节”的宿主。

```
[D11] Delivery Boundary
sluaunreal
├─ project code
│  └─ setLoadFileDelegate(fn -> bytes, filepath)  // 宿主决定文件/网络/CDN来源
├─ sample loader
│  ├─ try *.lua
│  └─ try *.luac
├─ LuaState::doFile() -> luaL_loadbuffer()        // runtime 只消费字节
└─ requireModule() -> Lua require                 // 模块解析仍交给 Lua VM

Angelscript
├─ engine boot decides precompiled mode           // 先选生成还是消费 cache
├─ load Binds.Cache                               // 绑定元数据缓存
├─ load PrecompiledScript*.Cache                  // 脚本预编译产物
└─ IsValidForCurrentBuild()                       // 构建配置不匹配就丢弃
```

[1] 关键源码：slua 的部署边界就是“任意来源字节 -> `luaL_loadbuffer()`”

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Source/democpp/MyGameInstance.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaState.h
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaState.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/External/lua/lstate.cpp
// 函数: UMyGameInstance::CreateLuaState / LoadFileDelegate / loadFile-doFile-requireModule / lua_setonlyluac
// 位置: MyGameInstance.cpp:41-64; LuaState.h:108-110; LuaState.cpp:139-155,729-785;
//       lstate.cpp:236-241
// ============================================================================
typedef TArray<uint8> (*LoadFileDelegate) (const char* fn, FString& filepath);
// ★ runtime 对脚本来源的唯一抽象就是“给我一段字节和最终路径”

state->setLoadFileDelegate([](const char* fn, FString& filepath)->TArray<uint8> {
    ...
    TArray<FString> luaExts = { UTF8_TO_TCHAR(".lua"), UTF8_TO_TCHAR(".luac") };
    for (auto& it : luaExts) {
        auto fullPath = path + *it;
        FFileHelper::LoadFileToArray(Content, *fullPath);
        if (Content.Num() > 0) {
            filepath = fullPath;
            return MoveTemp(Content);
        }
    }
    return MoveTemp(Content);
});
// ★ sample 先探测源码，再探测字节码，但没有包签名或版本清单参与

TArray<uint8> LuaState::loadFile(const char* fn,FString& filepath) {
    if(loadFileDelegate) return loadFileDelegate(fn,filepath);
    return TArray<uint8>();
}
// ★ runtime 只向 delegate 取原始 bytes

if(luaL_loadbuffer(L, (const char *)buf, len, chunk)) {
    ...
}
// ★ doBuffer() 直接把 bytes 喂给 Lua VM，没有中间解密/验签层

LuaVar LuaState::doFile(const char* fn, LuaVar* pEnv) {
    FString filepath;
    TArray<uint8> buf = loadFile(fn, filepath);
    if (buf.Num() > 0) {
        ...
        LuaVar r = doBuffer(buf.GetData(),buf.Num(),chunk,pEnv );
        return r;
    }
    return LuaVar();
}

LuaVar LuaState::requireModule(const char* fn, LuaVar* pEnv) {
    lua_getglobal(L, "require");
    lua_pushstring(L, fn);
    if (lua_pcall(L, 1, 1, top))
    {
        ...
        return LuaVar();
    }
    ...
}
// ★ 模块装载最终仍回到 Lua require，而不是插件内自定义包格式

LUA_API void lua_setonlyluac(lua_State *L, int v) {
    L->onlyluac = v;
}
// ★ vendored Lua 提供了 bytecode-only 开关；但本仓库检索 `lua_setonlyluac(` 仅命中这一定义，slua runtime 并未启用它
```

[2] 对照源码：Angelscript 的交付物是 build-aware cache，而不是运行时任意字节 callback

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp
// 函数: engine startup / FAngelscriptBindDatabase::Load
// 位置: AngelscriptEngine.cpp:1425-1470,1512-1548;
//       AngelscriptBindDatabase.cpp:103-115
// ============================================================================
bGeneratePrecompiledData = RuntimeConfig.bGeneratePrecompiledData;
bUsePrecompiledData = !bGeneratePrecompiledData && !RuntimeConfig.bIgnorePrecompiledData
    && !RuntimeConfig.bRunningCommandlet && !WITH_EDITOR && !bScriptDevelopmentMode;
...
if (bGeneratePrecompiledData)
    PrecompiledData = new FAngelscriptPrecompiledData(Engine);
...
FAngelscriptBindDatabase::Get().Load(GetScriptRootDirectory() / TEXT("Binds.Cache"), bGeneratePrecompiledData);
// ★ 启动时先决定“生成 cache 还是消费 cache”，再加载绑定数据库

if (bUsePrecompiledData)
{
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
            ...
            PrecompiledData->bMinimizeMemoryUsage = true;
        }
    }
}
// ★ 预编译脚本是构建配置敏感的正式产物，配置不匹配会直接丢弃

FFileHelper::LoadFileToArray(Data, *Path);
...
if (Classes.Num() == 0 && Structs.Num() == 0)
{
    UE_LOG(Angelscript, Fatal, TEXT("Unable to load script bind database, Script/Binds.Cache file is missing or old. This will cause script compilation and execution to fail."));
}
// ★ 连绑定元数据 cache 都是正式启动前置条件，而不是“能跑就跑”
```

与 Angelscript 对比：

| 观察点 | sluaunreal | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 交付物抽象 | `LoadFileDelegate` 返回 bytes，宿主决定来源（`LuaState.h:108-110`; `LuaState.cpp:153-155`） | `Binds.Cache` + `PrecompiledScript*.Cache` 作为正式产物（`AngelscriptEngine.cpp:1425-1470,1512-1548`; `AngelscriptBindDatabase.cpp:103-115`） | 实现方式不同 |
| `.luac` / 预编译脚本 | sample 明确支持 `.luac`（`MyGameInstance.cpp:51-58`） | 启动期直接消费 `PrecompiledScript*.Cache` 并校验 build（`AngelscriptEngine.cpp:1512-1548`） | 实现方式不同 |
| 加密/签名 | 本轮所读源码未见验签/解密入口；`lua_setonlyluac` 也未被 runtime 启用 | 本轮所读源码同样未见脚本签名或加密；重点是 cache 校验而不是安全包装 | 都没有实现 |
| 热更包事务层 | 运行时只拿 bytes，不拥有补丁清单和批次语义 | 文件/模块级 compile cache 与 hot reload 事务内聚在引擎侧 | 实现质量差异：Angelscript 的部署与重载边界更一致 |

---

## 深化分析 (2026-04-08 23:23:08)

### [维度 D2] `LuaWrapper` 的静态导出边界比名字更窄：它优先优化 `UStruct` 编解码，`UObject` 调用分发仍然是动态桥

前文已经确认 slua 是“静态导出 + 动态反射”的混合方案；本轮继续拆边界后可以更精确地说：`LuaWrapper*.inc` 的主要收益点不是“把全部 `UObject` 调用都静态化”，而是把一批稳定 `UStruct` 的 `push/check` 路径预先生成为 C++ 函数表。`LuaObject::pushUStructProperty()` 和 `checkUStructProperty()` 会先询问 `LuaWrapper::pushValue()` / `checkValue()`，命中则直接走 `_pushStructMap` / `_checkStructMap`；未命中才退回通用 `LuaStruct` 拷贝与校验路径。

这意味着 slua 的“静态”主要落在“值类型封送（marshalling）”层，而不是“`UFunction` 调用分发”层。`LuaObject::push(UFunction*, UClass*)` 仍然把调用包装成闭包，再交给 `LuaFunctionAccelerator` 缓存参数布局；类静态查找失败后，还会沿 `UClass` 继承链搜索 extension method。换句话说，slua 的静态导出是给动态桥加一层“命中即快、未命中仍可跑”的前置捷径。

```
[D2] Static Export Boundary
sluaunreal
├─ LuaState init
│  └─ LuaClass::reg + LuaWrapper::initExt          // 注册静态 wrapper 与扩展方法
├─ FStructProperty marshal
│  ├─ _pushStructMap / _checkStructMap             // 命中已生成 struct wrapper
│  └─ generic LuaStruct copy                       // 未命中退回通用桥
├─ UClass member lookup
│  ├─ FindFunctionByName                           // 仍按运行时 UFunction 查找
│  └─ searchExtensionMethod(super chain)           // extension 也在运行时沿继承链搜
└─ UFunction call
   └─ LuaFunctionAccelerator                       // 缓存参数布局，不是静态生成 call stub

Angelscript
├─ Blueprint/UObject bind generation              // 当前构建期生成 bind / function table
├─ direct native / StaticJIT call path            // 直接优化 native call 执行
└─ reflective fallback                            // 仅在直连不可用时按条件兜底
```

[1] 关键源码：slua 的生成物按 UE 版本切片，并在 `LuaState` 启动时整体注册

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaWrapper.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaState.cpp
// 函数: LuaWrapper::initExt / LuaState::init
// 位置: LuaWrapper.cpp:55-67, 184-188; LuaState.cpp:619-627
// ============================================================================
#if ((ENGINE_MINOR_VERSION==2) && (ENGINE_MAJOR_VERSION==5))
    #include "LuaWrapper5.2.inc"
#elif ((ENGINE_MINOR_VERSION==3) && (ENGINE_MAJOR_VERSION==5))
    #include "LuaWrapper5.3.inc"
#elif ((ENGINE_MINOR_VERSION==4) && (ENGINE_MAJOR_VERSION==5))
    #include "LuaWrapper5.4.inc"
#endif
// ★ 生成物不是一个“与宿主工程同步变化”的 bind database，而是按引擎小版本固化的 .inc 快照

void LuaWrapper::initExt(lua_State* L)
{
    init(L);
    FSoftObjectPtrWrapper::bind(L);
}
// ★ 真正的生成入口是 init(L)，额外只补了少量手写 wrapper

InitExtLib(L);
LuaObject::init(L);
LuaProtobuf::init(L);
SluaUtil::openLib(L);
LuaClass::reg(L);
LuaArray::reg(L);
LuaMap::reg(L);
LuaSet::reg(L);
// ★ VM 初始化时统一注册；静态导出是 runtime 启动时加载，而不是像 UHT 一样嵌进编译产物协议
```

[2] 关键源码：`LuaWrapper` 只在 `FStructProperty` 路径上拦截，未命中立刻退回通用桥

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaWrapper5.2.inc
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaObject.cpp
// 函数: LuaWrapper::pushValue / checkValue / init / pushUStructProperty / checkUStructProperty
// 位置: LuaWrapper5.2.inc:16763-16809; LuaObject.cpp:2235-2247, 2475-2477
// ============================================================================
int LuaWrapper::pushValue(lua_State* L, FStructProperty* p, UScriptStruct* uss, uint8* parms, int i) {
    auto vptr = _pushStructMap.Find(uss);
    if (vptr != nullptr) {
        (*vptr)(L, p, parms, i);
        return 1; // ★ 命中静态 struct wrapper，直接走已生成的 push 函数
    } else {
        return 0; // ★ 未命中则明确放弃静态快路径
    }
}

void LuaWrapper::init(lua_State* L) {
    FIntPointStruct = StaticGetBaseStructureInternal(TEXT("IntPoint"));
    if (FIntPointStruct) {
        _pushStructMap.Add(FIntPointStruct, __pushFIntPoint);
        _checkStructMap.Add(FIntPointStruct, __checkFIntPoint);
        FIntPointWrapper::bind(L);
    }
    // ★ 这里是“一个 struct 一个 wrapper”的显式白名单，不是通用 UProperty 生成器
}

if (LuaWrapper::pushValue(L, p, uss, parms, i))
    return 1;
// ★ 只有 FStructProperty 先尝试静态 wrapper

auto buf = LuaWrapper::checkValue(L, p, uss, parms, i);
if (buf)
    return buf;
// ★ check 失败同样退回通用 LuaStruct 校验/拷贝
```

[3] 对照源码：Angelscript 的“静态优化”更靠近 native call 执行层，反射兜底也有显式门槛

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp
// 函数: FStaticJITFunction::ScriptCallNative / BindBlueprintCallableReflectiveFallback
// 位置: StaticJITHeader.cpp:169-208; BlueprintCallableReflectiveFallback.cpp:382-420
// ============================================================================
void FStaticJITFunction::ScriptCallNative(FScriptExecution& Execution, asCScriptFunction* Function, asBYTE* l_sp, asQWORD* valueRegister, void** objectRegister)
{
    asSSystemFunctionInterface *sysFunc = Function->sysFuncIntf;
    int callConv = sysFunc->callConv;
    if (callConv == ICC_GENERIC_FUNC || callConv == ICC_GENERIC_METHOD)
    {
        void (*func)(asIScriptGeneric*) = (void (*)(asIScriptGeneric*))sysFunc->func;
        ...
        func(&gen);
        *valueRegister = gen.returnVal;
        *objectRegister = (void*)gen.objectRegister;
        return;
    }
}
// ★ StaticJIT 优化的是“脚本执行器如何调用 native”，不是只优化某类 struct 的装箱/拆箱

if (!ShouldBindBlueprintCallableReflectiveFallback(Function))
{
    return false;
}

if (!Signature.bAllTypesValid || Signature.ArgumentTypes.Num() > BlueprintCallableReflectiveFallbackMaxArgs)
{
    return false;
}
// ★ reflective fallback 也不是无条件后门，而是对签名有效性和参数数量有硬门槛
```

### 设计取舍

- slua 把代码生成成本集中在“高频、稳定、跨项目通用”的 engine struct 上，收益是这些值类型的桥接分支更短；代价是每个 UE 小版本都要维护一套 `LuaWrapper5.x.inc` 快照。
- `UObject` 侧继续保留 `FindFunctionByName + extension method + LuaFunctionAccelerator`，覆盖面高且热更友好；代价是调用分发本身仍依赖运行时反射。
- Angelscript 的 StaticJIT/direct bind 更像“优化 native 调用执行路径”，维护成本更依赖当前构建与生成链，而不是维护多套版本化 wrapper 白名单。

### 与 Angelscript 对比

| 观察点 | sluaunreal | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 静态导出的优化层 | 主要优化 `FStructProperty` 的 `push/check`（`LuaWrapper5.2.inc:16763-16809`; `LuaObject.cpp:2235-2247,2475-2477`） | 主要优化 native call 执行与直连 bind（`StaticJITHeader.cpp:169-208`; `Bind_BlueprintCallable.cpp:72-91`） | 实现方式不同 |
| 生成物粒度 | 按 UE 小版本固化 `.inc` 快照（`LuaWrapper.cpp:55-67`） | 跟随当前构建与 UHT/function-table/StaticJIT 数据生成 | 实现方式不同 |
| `UObject` 动态桥边界 | `FindFunctionByName`、extension super-chain、`LuaFunctionAccelerator` 仍是常态（`LuaObject.cpp:741-775,793-801,3062-3066`） | reflective fallback 只在直连失败且签名满足条件时绑定（`BlueprintCallableReflectiveFallback.cpp:382-420`） | 实现质量差异：Angelscript 的 fallback 边界更显式、更可审计 |

### [维度 D3] slua 的 Blueprint 互通是“字符串调度 + `FLuaBPVar` 装箱”，不是把 Blueprint API 并入脚本类型系统

本轮把 `ULuaBlueprintLibrary` 和 Angelscript 的 Blueprint 绑定链一起看之后，可以确认两边在“Blueprint 交互”上瞄准的是不同层。slua 暴露给 Blueprint 的核心入口只有 `CallToLua` / `CallToLuaWithArgs` 加上一组 `CreateVarFrom*` / `Get*FromVar`。Blueprint 节点实际做的是：把参数装成 `FLuaBPVar`，按字符串查找 Lua 全局函数名，再把 `LuaVar` 返回值按索引拆回 `int / float / bool / FString / UObject*`。

这和 Angelscript 的方案不同。Angelscript 会在 `Bind_BlueprintType.cpp` 里扫描 `BlueprintType` 类与属性，把 `ScriptName`、`BlueprintGetter`、`BlueprintSetter`、`K2_`/`BP_`/`Receive` 前缀清洗规则一起纳入绑定阶段，最终把 Blueprint API 映射成脚本类型系统里的稳定符号。差异不在“能不能互通”，而在“互通入口是动态消息总线，还是脚本侧的静态可发现 API”。

```
[D3] Blueprint Interop Shape
sluaunreal
├─ Blueprint node
│  ├─ CallToLua / CallToLuaWithArgs               // 节点只知道函数名字符串
│  ├─ FLuaBPVar                                   // 参数与返回值统一装箱
│  └─ GetInt/Number/String/Bool/ObjectFromVar     // 按索引拆箱
└─ LuaState::get("FunctionName")                  // 运行时查 Lua 全局

Angelscript
├─ scan BlueprintType class / property / function // 扫描 BlueprintType 元数据
├─ ScriptName normalization                       // 清洗 ScriptName/K2_/BP_/Receive 前缀
├─ BindBlueprintCallable / BindBlueprintEvent     // 生成脚本侧函数声明
└─ auto getter/setter synthesis                   // 生成 GetX/SetX 访问器
```

[1] 关键源码：slua 的 Blueprint Library 本质是“函数名字符串 + `LuaVar` 装箱”

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaBlueprintLibrary.h
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaBlueprintLibrary.cpp
// 函数: ULuaBlueprintLibrary API / CallToLuaWithArgs / CallToLua
// 位置: LuaBlueprintLibrary.h:21-31,42-76; LuaBlueprintLibrary.cpp:51-96
// ============================================================================
USTRUCT(BlueprintType)
struct SLUA_UNREAL_API FLuaBPVar {
    GENERATED_USTRUCT_BODY()
public:
    NS_SLUA::LuaVar value;
    static void* checkValue(NS_SLUA::lua_State* L, NS_SLUA::FStructProperty* p, uint8* params, int i);
};
// ★ Blueprint 侧看到的不是具体 UE 类型签名，而是一个通用盒子

UFUNCTION(BlueprintCallable, meta=( DisplayName="Call To Lua With Arguments", WorldContext = "WorldContextObject"), Category="slua")
static FLuaBPVar CallToLuaWithArgs(UObject* WorldContextObject, FString FunctionName,const TArray<FLuaBPVar>& Args,FString StateName);

LuaVar f = ls->get(TCHAR_TO_UTF8(*funcname));
if (!f.isFunction()) {
    Log::Error("Can't find lua member function named %s to call", TCHAR_TO_UTF8(*funcname));
    return LuaVar();
}

auto fillParam = [&]
{
    for (auto& arg : args) {
        arg.value.push(ls->getLuaState());
    }
    return args.Num();
};
return f.callWithNArg(fillParam);
// ★ Blueprint -> Lua 的桥接核心是“字符串找函数 + 把 FLuaBPVar 里的 LuaVar 重新 push 回栈”
```

[2] 关键源码：`FLuaBPVar` 的拆箱能力是有限白名单，多返回值靠 table/tuple 索引访问

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaBlueprintLibrary.cpp
// 函数: getValue / getValueFromVar / Get*FromVar
// 位置: 152-257
// ============================================================================
bool getValue(const LuaVar& lv,int index,int& value) { ... }
bool getValue(const LuaVar& lv,int index,bool& value) { ... }
bool getValue(const LuaVar& lv,int index,float& value) { ... }
bool getValue(const LuaVar& lv,int index,FString& value) { ... }
bool getValue(const LuaVar& lv,int index,UObject*& value) { ... }
// ★ 可直接拆出的 Blueprint 类型只有 int/bool/float/FString/UObject*

else if(lv.isTable() || lv.isTuple()) {
    LuaVar v = lv.getAt(index);
    return getValue(v,1,value);
}
// ★ 多返回值不是静态签名，而是把 table/tuple 当作“按下标取值”的运行时容器

if(getValue(lv,Index,v))
    return v;
else
    FFrame::KismetExecutionMessage(*FString::Printf(TEXT("Attempted to index an item from an invalid type!")),
        ELogVerbosity::Warning, GetVarTypeErrorWarning);
// ★ 类型错误只在运行时告警，Blueprint 图上没有更强的编译期约束
```

[3] 对照源码：Angelscript 在绑定期就规范 Blueprint 名字并补访问器

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp
// 函数: GetScriptNameForFunction / BindProperties / BlueprintGetterSetter binding
// 位置: Helper_FunctionSignature.h:85-120; Bind_BlueprintType.cpp:1067-1093, 1413-1470
// ============================================================================
static FString GetScriptNameForFunction(UFunction* InFunction)
{
    FString OutScriptName = InFunction->GetName();
    if (InFunction->HasMetaData(NAME_Signature_ScriptName))
    {
        OutScriptName = GetPrimaryScriptName(InFunction->GetMetaData(NAME_Signature_ScriptName));
    }
    else
    {
        bool bChangedName = false;
        bChangedName |= OutScriptName.RemoveFromStart(TEXT("K2_"));
        bChangedName |= OutScriptName.RemoveFromStart(TEXT("BP_"));
        bChangedName |= OutScriptName.RemoveFromStart(TEXT("AS_"));
        if (InFunction->HasAnyFunctionFlags(FUNC_BlueprintEvent))
        {
            bChangedName |= OutScriptName.RemoveFromStart(TEXT("Received_"));
            bChangedName |= OutScriptName.RemoveFromStart(TEXT("Receive"));
        }
    }
    return OutScriptName;
}
// ★ 脚本名在绑定期就被规范化，脚本侧不需要手写字符串约定

const FString& ScriptName = Property->GetMetaData(NAME_Property_ScriptName);
if (ScriptName.Len() != 0)
    PropertyName = FAngelscriptFunctionSignature::GetPrimaryScriptName(ScriptName);
// ★ 属性同样支持 ScriptName 重命名

FString BlueprintGetterName = Property->GetMetaData(NAME_Property_BlueprintGetter);
...
BindFunctionWithAdditionalName(ClassType.ToSharedRef(), GetterFunc, TargetGetterName, DBMethod);

FString BlueprintSetterName = Property->GetMetaData(NAME_Property_BlueprintSetter);
...
BindFunctionWithAdditionalName(ClassType.ToSharedRef(), SetterFunc, TargetSetterName, DBMethod);
// ★ BlueprintGetter / Setter 会被补成脚本侧可发现的 GetX / SetX
```

### 设计取舍

- slua 的好处是 Blueprint 侧入口非常少，任何 Lua 函数都能通过字符串调用，热更时不需要同步刷新大批声明；代价是参数/返回值缺少脚本侧可发现性，类型错误主要靠运行时告警。
- `FLuaBPVar` 的设计非常适合“把 Lua 当消息脚本层”而不是“把 Lua 当 Blueprint API 的静态镜像层”。
- Angelscript 把更多工作放到绑定期，收益是脚本 API 可搜索、可重命名、可补访问器；代价是绑定链更复杂，依赖更强的元数据与生成逻辑。

### 与 Angelscript 对比

| 观察点 | sluaunreal | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| Blueprint -> script 入口 | `CallToLua*` 用函数名字符串查 Lua 全局（`LuaBlueprintLibrary.cpp:51-96`） | 绑定期生成脚本函数声明与命名空间（`Helper_FunctionSignature.h:85-120`; `Bind_BlueprintType.cpp:1029-1048`） | 实现方式不同 |
| 参数/返回值模型 | `FLuaBPVar` 统一装箱，拆箱白名单是 `int/float/bool/FString/UObject*`（`LuaBlueprintLibrary.h:21-31`; `LuaBlueprintLibrary.cpp:152-257`） | 按具体 `UFunction`/`FProperty` 签名生成 script-visible declaration | 实现方式不同 |
| 属性访问器 | 本轮所读 slua Blueprint Library 未见自动 getter/setter 生成 | 自动补 `GetX` / `SetX` 访问器（`Bind_BlueprintType.cpp:1413-1470`） | 没有实现 |
| 脚本名规范 | 依赖 Blueprint 节点手填字符串 | 统一处理 `ScriptName` 与 `K2_`/`BP_`/`Receive` 前缀（`Helper_FunctionSignature.h:85-120`） | 实现质量差异：Angelscript 的脚本名稳定性更强 |

### [维度 D4] slua 的“热更新恢复”是 `UFunction` 原位打补丁与回滚，不是基于源码 diff 的 reload 决策系统

前文已经区分过 slua 的线上热更新与 Angelscript 的开发期热重载；本轮继续往下拆，能看到两者的“失败恢复边界”完全不同。slua 的核心动作是：在 `hookBpScript()` 里给 `UFunction` 打补丁，方式包括 `GRegisterNative(Ex_LuaOverride, hookFunc)`、复制一个 `Super_` 版本的 `UFunction`、对 native 函数调用 `SetNativeFunc()`、对非 native 函数把 `Ex_LuaOverride` 字节码直接插到 `Script` 开头。对应的恢复动作也很原子化：`removeOneOverride()` 负责删掉插入的 `Code`、从 `cacheNativeFuncs` 里还原原始 native 指针、把运行时新增的 RPC `UFunction` 从 `FunctionMap` / `Children` / `NetFields` 上摘掉。

这条链证明 slua 的 hotfix 模型并不关心“脚本改动属于软重载还是全量重载”，它关心的是“当前 Lua 模块是否还能把 UObject 调到新的 Lua 实现”。因此 `LuaState::close()` 只在 Editor 主状态关闭时统一 `removeOverrides()`；运行时重绑更多依赖 `bindOverrideFuncs()` 与异步加载完成后的再次尝试。它是一个“补丁式 redirect/restore”系统，而不是一个“有 reload 等级、依赖传播和队列”的编译事务系统。

```
[D4] Reload Recovery Boundary
sluaunreal
├─ Lua module ready
│  └─ bindOverrideFuncs()
│     ├─ duplicate Super_ UFunction               // 保留 super call
│     ├─ SetNativeFunc(luaOverrideFunc)           // native 函数改入口
│     └─ Script.Insert(Ex_LuaOverride)            // 非 native 直接改脚本字节码
├─ async object ready
│  └─ onAsyncLoadingFlushUpdate() -> rebind       // 只在对象完成加载后重试
└─ editor/main-state close
   └─ removeOverrides()                           // 删除补丁并恢复原 native/script

Angelscript
├─ DirectoryWatcher queues changed/deleted files  // 先形成待重载文件队列
├─ ClassGenerator diff old/new declarations       // 再计算 Soft/Full reload requirement
└─ compile pipeline decides reload strategy       // 最后进入软重载或全量重载流程
```

[1] 关键源码：slua 的“安装补丁”与“撤销补丁”都直接操作 `UFunction`

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaOverrider.cpp
// 函数: hookBpScript / removeOneOverride
// 位置: 831-922, 1381-1449
// ============================================================================
if (scriptNum >= CodeSize && script[0] == Ex_LuaOverride)
{
    script.RemoveAt(0, CodeSize, false);
    if (script.Num() == 0)
    {
        script.~TArray();
        new (&script) TArray<uint8>();
    }
}
// ★ 回滚脚本覆写时，直接把插到 Script 开头的 Ex_LuaOverride 字节码删掉

if (func->GetNativeFunc() == (FNativeFuncPtr)&ULuaOverrider::luaOverrideFunc)
{
    auto nativeMap = cacheNativeFuncs.Find(cls);
    auto nativeFunc = nativeMap->Find(func->GetName());
    func->SetNativeFunc(*nativeFunc);
}
// ★ native hook 的恢复方式是把原始 FNativeFuncPtr 填回去

auto supercallFunc = duplicateUFunction(func, cls, FName(*(SUPER_CALL_FUNC_NAME_PREFIX + func->GetName())), func->GetNativeFunc());
// ★ 先复制一个 Super_ 版本保住 super call

if (overrideFunc->HasAnyFunctionFlags(FUNC_Net) || overrideFunc->HasAnyFunctionFlags(FUNC_Native))
{
    auto& funcMap = cacheNativeFuncs.FindOrAdd(cls);
    funcMap.Add(overrideFunc->GetName(), overrideFunc->GetNativeFunc());
    overrideFunc->SetNativeFunc(hookFunc);
}
overrideFunc->Script.Insert(Code, CodeSize, 0);
// ★ 安装补丁时，native 函数改函数指针，Blueprint/script 函数改字节码头
```

[2] 关键源码：slua 的恢复触发点是状态关闭与异步对象完成加载，不存在“改动等级分类”

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaState.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaOverrider.cpp
// 函数: LuaState::close / LuaOverrider::removeOverrides / onAsyncLoadingFlushUpdate
// 位置: LuaState.cpp:480-505; LuaOverrider.cpp:949-973, 976-1013
// ============================================================================
#if WITH_EDITOR
    if (!mainState && overrider)
        overrider->removeOverrides();
#endif
// ★ 统一回滚点挂在主 LuaState 关闭时，而不是“某个脚本文件 diff 后决定 soft/full reload”

void LuaOverrider::removeOverrides()
{
    classHookedFuncs.GetKeys(classArray);
    for (auto cls : classArray)
    {
        removeOneOverride(cls, false);
        clearSuperFuncCache(cls);
    }
    ...
    overridedClasses.Empty();
    LuaNet::addedRPCClasses.Empty();
}
// ★ removeOverrides 只是批量撤销已经打上的补丁

if (obj && !obj->HasAnyFlags(RF_NeedPostLoad) && !obj->HasAnyFlags(RF_NeedInitialization))
{
    UClass* cls = obj->GetClass();
    bindOverrideFuncs(obj, cls);
}
else if (obj)
{
    asyncLoadedObjects[newIndex] = objInfo;
    newIndex++;
}
// ★ 异步加载对象只是“等可绑定了再重试”，并没有源码级事务回滚或冲突分类
```

[3] 对照源码：Angelscript 先排队，再做 diff，再决定 soft/full reload

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp
// 函数: QueueFileChanges / class diff reload requirement / DumpHotReloadState
// 位置: AngelscriptDirectoryWatcherInternal.cpp:57-87;
//       AngelscriptClassGenerator.cpp:1077-1258;
//       AngelscriptStateDump.cpp:981-1014
// ============================================================================
if (AbsolutePath.EndsWith(TEXT(".as")))
{
    if (Change.Action == FFileChangeData::EFileChangeAction::FCA_Removed)
        Engine.FileDeletionsDetectedForReload.AddUnique({ AbsolutePath, RelativePath });
    else
        Engine.FileChangesDetectedForReload.AddUnique({ AbsolutePath, RelativePath });
}
// ★ 第一步先形成“待重载 / 待删除”的文件队列

if (ClassData.OldClass->SuperClass != ClassData.NewClass->SuperClass)
{
    if (ClassData.ReloadReq < EReloadRequirement::FullReloadRequired)
        ClassData.ReloadReq = EReloadRequirement::FullReloadRequired;
}

if (PropertyDesc->PropertyType != OldPropertyDesc->PropertyType)
{
    ClassData.ReloadReq = EReloadRequirement::FullReloadRequired;
    ClassData.ReloadReqLines.AddUnique(PropertyDesc->LineNumber);
}

if (!NewFunctionDesc->Meta.OrderIndependentCompareEqual(OldFunctionDesc->Meta))
{
    ClassData.ReloadReq = EReloadRequirement::FullReloadSuggested;
    ClassData.ReloadReqLines.AddUnique(NewFunctionDesc->LineNumber);
}
// ★ 第二步按“父类变化 / 属性类型变化 / 元数据变化”等条件给出 reload 等级

Writer.AddRow({
    GetFilenamePairPath(FilenamePair),
    TEXT("PendingReload"),
    FString()
});
// ★ 当前待重载队列还能通过 state dump 导出，可观察性比“只撤销函数补丁”更强
```

### 设计取舍

- slua 的优势是运行时补丁链非常短，适合“线上下发 Lua 后立刻接管行为”的 hotfix 场景；代价是恢复语义停留在 `UFunction` 粒度，没有“源码变化类别 -> reload 策略”这一层。
- 直接改写 `Script` 字节码和 native 函数指针的方案，对 Blueprint 覆写和 RPC 都很有效；代价是补丁正确性依赖运行时缓存、类函数表清理和对象生命周期时机。
- Angelscript 的热重载更像编译系统的一部分，能区分 `SoftReload / FullReloadSuggested / FullReloadRequired`；代价是整条链更重，也更偏开发期。

### 与 Angelscript 对比

| 观察点 | sluaunreal | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 热更动作核心 | 直接修改 `UFunction::Script` / `SetNativeFunc()`，并在关闭时回滚（`LuaOverrider.cpp:831-922,1381-1449`; `LuaState.cpp:480-488`） | 先排队文件变化，再按 old/new 声明 diff 计算 reload requirement（`AngelscriptDirectoryWatcherInternal.cpp:57-87`; `AngelscriptClassGenerator.cpp:1077-1258`） | 实现方式不同 |
| 恢复粒度 | `UFunction` 补丁撤销、RPC 入口删除、super cache 清理（`LuaOverrider.cpp:919-973`） | 类/属性/方法级 diff 决定 soft/full reload，并可记录行号（`AngelscriptClassGenerator.cpp:1100-1258`） | 实现质量差异：Angelscript 的恢复决策信息更丰富 |
| 变更可观测性 | 本轮所读 slua runtime 未见待重载文件队列导出 | `HotReloadState.csv` 可导出 public reload queues（`AngelscriptStateDump.cpp:981-1014`） | 没有实现 |
| 目标场景 | 运行时 hotfix / 网络下发脚本后接管 UObject 行为 | 开发期脚本改动、类结构变化与增量编译 | 实现方式不同 |

---

## 深化分析 (2026-04-08 23:33:33)

### [维度 D2 / D8] slua 的复制属性不是直接生成到 `UClass`，而是 `LuaNetSerialization` 背后的影子 `UStruct + proxy` 子系统

前文已经拆过 slua 的普通属性桥和 RPC 注入链，但复制属性这条线还有一个关键边界此前没有展开：slua 并没有把 Lua 复制字段直接生成为宿主 `UClass` 的真实 `FProperty`。`LuaNet::addClassReplicatedProps()` 会先要求类里存在一个名为 `LuaNetSerialization` 的承载属性，再调用 Lua 模块里的 `GetLifetimeReplicatedProps()` 返回描述表，随后**临时创建一个影子 `UStruct`**，把字段名、复制条件、`RepNotifyCondition` 和 `OnRep_` 命名约定都挂到 `ClassLuaReplicated` 上。

运行期真正持有状态的也不是 `UClass` 本身，而是 `FLuaNetSerializationProxy`。这个 proxy 维护 `values / oldValues` 双缓冲、`dirtyMark / flatDirtyMark / arrayDirtyMark`、`assignTimes`、64 段 change history，以及 `sharedSerialization` 位流缓存。Lua 侧写入复制字段时，`__newindex` 和 wrapper 宏都会先打脏位再递增 `assignTimes`；`NetDeltaSerialize()` 才在 UE 复制帧里做差异比对、生命周期条件过滤、共享序列化构建，并在接收端按 `OnRep_<PropName>` 回调 Lua。换句话说，slua 的复制支持不是“把属性标成 replicated”这么简单，而是插件内自带了一套影子 schema + delta serializer。

Angelscript 走的是另一条路。复制语义先写进 `FAngelscriptPropertyDesc`，再由 class generator 直接把真实 `FProperty` 标成 `CPF_Net` / `CPF_RepNotify`，同时在编译阶段校验 `ReplicatedUsing` 回调是否存在、参数是否匹配。也就是说，slua 选择“在桥接层维护一套动态复制账本”，Angelscript 选择“把脚本属性尽量还原回 UE 原生反射与复制元数据”。

```text
[D2/D8] Replicated Property Pipeline
sluaunreal
Lua module:GetLifetimeReplicatedProps()
├─ build shadow UStruct at runtime                  // 不改真实 UClass 属性表
├─ ClassLuaReplicated
│  ├─ name/index map
│  ├─ lifetimeConditions
│  ├─ lifetimeRepNotifyConditions
│  └─ repNotifies via OnRep_<PropName>
├─ FLuaNetSerializationProxy
│  ├─ values / oldValues
│  ├─ dirtyMark / flatDirtyMark / arrayDirtyMark
│  ├─ changeHistorys[64]
│  └─ sharedSerialization
├─ __newindex / wrapper write -> assignTimes++
└─ NetDeltaSerialize()
   ├─ CompareProperties()
   ├─ BuildSharedSerialization()
   └─ CallOnRep("OnRep_<PropName>")

Angelscript
script property desc
├─ bReplicated / ReplicationCondition / bRepNotify
├─ VerifyRepFunc(ReplicatedUsing)                   // 编译期校验回调合同
├─ generated FProperty
│  ├─ CPF_Net
│  └─ CPF_RepNotify + RepNotifyFunc
└─ UE native replication metadata                  // 回到标准反射属性流
```

[1] 关键源码：slua 先用 Lua 表构造影子 `UStruct`，再按命名约定搜 `OnRep_`

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaNet.cpp
// 函数: LuaNet::addClassReplicatedProps
// 位置: 120-170, 183-246, 252-268
// ============================================================================
FProperty* prop = cls->FindPropertyByName(TEXT("LuaNetSerialization"));
if (prop)
{
    NS_SLUA::LuaVar getLifetimeFunc = luaModule.getFromTable<NS_SLUA::LuaVar>("GetLifetimeReplicatedProps");
    if (getLifetimeFunc.isFunction())
    {
        auto luaReplicatedTable = getLifetimeFunc.call();
        if (luaReplicatedTable.isTable())
        {
            auto& classReplicated = *classLuaReplicatedMap.Add(cls, new ClassLuaReplicated());
            classReplicated.ownerProperty = prop;
            // ★ 真实类上只要求有一个承载属性，Lua 复制字段本体随后会被塞进影子结构

            auto ustruct = NewObject<UStruct>();
            ustruct->AddToRoot();
            classReplicated.ustruct = ustruct;
            // ★ 这里新建的不是 UClass 成员，而是一份运行时影子 UStruct

            luaReplicatedTable.push(L);
            ReplicateIndexType index = 0;
            lua_pushnil(L);
            while (lua_next(L, -2))
            {
                ...
                childProperty = NS_SLUA::PropertyProto::createProperty(..., ustruct);
                ...
                FString keyName = UTF8_TO_TCHAR(name);
                replicatedIndexToNameMap.Add(keyName);
                replicatedNameToIndexMap.Add(keyName, index++);
                properties.Add(childProperty);

                lifetimeConditions.Add(lifeCond);

                ELifetimeRepNotifyCondition repNotifyCondition = ELifetimeRepNotifyCondition::REPNOTIFY_OnChanged;
                lua_getfield(L, -1, "RepNotifyCondition");
                if (lua_isinteger(L, -1))
                {
                    repNotifyCondition = (ELifetimeRepNotifyCondition)lua_tointeger(L, -1);
                }
                lifetimeRepNotifyConditions.Add(repNotifyCondition);
                // ★ 复制条件和 RepNotifyCondition 都来自 Lua 表，不是宿主类静态元数据
            }

            ustruct->StaticLink(true);
            initFlatReplicatedProps(classReplicated, classReplicated.propertyOffsetToMarkIndex, ustruct, flatIndex, 0, 0, nullptr);

            luaModule.push(L);
            for (ReplicateIndexType i = 0, n = replicatedIndexToNameMap.Num(); i < n; ++i)
            {
                auto& propName = replicatedIndexToNameMap[i];
                if (lua_getfield(L, -1, TCHAR_TO_UTF8(*(TEXT("OnRep_") + propName))) != LUA_TNIL)
                {
                    repNotifies.Add(i);
                }
                lua_pop(L, 1);
            }
            // ★ 是否存在 OnRep_ 回调同样是运行时命名约定检查
        }
    }
}
```

[2] 关键源码：slua 的复制热路径由 proxy 维护 dirty/history/shared-serialization，而不是直接依赖宿主属性表

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaNetSerialization.h
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaWrapper.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaNet.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaNetSerialization.cpp
// 函数: FLuaNetSerializationProxy / SLUA_MARK_NETPROP / __newindex / Write / CompareProperties / CallOnRep
// 位置: LuaNetSerialization.h:145-176,239-245;
//       LuaWrapper.cpp:29-37;
//       LuaNet.cpp:383-400,844-875;
//       LuaNetSerialization.cpp:571-708,723-906,911-999,1080-1105
// ============================================================================
struct FLuaNetSerializationProxy : public FGCObject
{
    TArray<uint8, TAlignedHeapAllocator<16>> values;
    TArray<uint8, TAlignedHeapAllocator<16>> oldValues;

    int32 assignTimes;
    LuaBitArray dirtyMark;
    LuaBitArray flatDirtyMark;
    TMap<int32, LuaBitArray> arrayDirtyMark;

    int32 historyStart = 0;
    int32 historyEnd = 0;
    LuaBitArray changeHistorys[MAX_CHANGE_HISTORY];
    FLuaRepSerializationSharedInfo sharedSerialization;
    ...
};
// ★ proxy 自己保存双缓冲、脏位图、history 和共享序列化缓存

enum
{
    WithNetDeltaSerializer = true,
};
// ★ 这个承载 struct 明确接管 UE 的 NetDeltaSerialize 路径

#define SLUA_MARK_NETPROP if (udptr->flag & UD_NETTYPE) \
{ \
    auto proxy = udptr->proxy; \
    if (proxy) \
    { \
        auto luaReplicatedIndex = udptr->luaReplicatedIndex; \
        proxy->dirtyMark.Add(luaReplicatedIndex); \
        proxy->assignTimes++; \
    } \
}
// ★ 引用代理写回时也会直接打脏并增加 assignTimes

FLuaNetSerializationProxy &proxy = *luaNetSerializationMap.Add(luaNetSerialization, newProxy);
proxy.owner = obj;
proxy.contentStruct = classReplicated.ustruct;
proxy.dirtyMark = LuaBitArray(classReplicated.properties.Num());
proxy.flatDirtyMark = LuaBitArray(classReplicated.flatProperties.Num());
content.SetNumZeroed(propertiesSize);
oldContent.SetNumZeroed(propertiesSize);
// ★ 每个对象实例都有自己的一份 proxy 状态，不复用类级静态缓存

checker(L, p, data + propOffset, 3, true);
proxy->dirtyMark.Add(luaReplicatedIndex);
proxy->assignTimes++;
onPropModify(L, proxy, luaReplicatedIndex, nullptr);
// ★ Lua 直接写字段时，先改 proxy 里的值，再进入脏标记和监听器分发

if (!oldState || proxy->bDirtyThisFrame || (proxy->assignTimes != oldState->assignTimes))
{
    ...
    for (int32 index = historyStart; index < proxy->historyEnd; ++index)
    {
        changes |= changeHistorys[index % NS_SLUA::FLuaNetSerializationProxy::MAX_CHANGE_HISTORY];
        ...
    }

    if (!changes.IsEmpty() && !proxy->sharedSerialization.IsValid())
    {
        BuildSharedSerialization(deltaParms.Map, classLuaReplicated, proxy, changes, arrayChanges);
    }
    // ★ 发送端不是逐属性裸写，而是先归并 change history，再按需构建 shared bitstream
}

if (bAlwaysNotify || !flatProp->Identical(oldData + flatOffset, data + flatOffset))
{
    proxy.flatDirtyMark.Add(flatIndex);
    flatProp->CopyCompleteValue(oldData + flatOffset, data + flatOffset);
}
// ★ 接收端/比较端也维护 oldValues，避免每帧重扫所有字段

const FString funcName = "OnRep_" + propName;
if (!RepFuncMap.Contains(funcName))
{
    RepFuncMap.Add(funcName, luaTable.getFromTable<NS_SLUA::LuaVar>(funcName));
}
...
NS_SLUA::LuaObject::push(L, prop, oldData, nullptr);
if (lua_pcall(L, 2, 0, errorHandle))
    lua_pop(L, 1);
// ★ OnRep 回调不是 UFunction，而是 Lua table 上按命名约定查找并缓存的函数
```

[3] 对照源码：Angelscript 在生成期就把复制语义落到真实 `FProperty`，并校验 `ReplicatedUsing`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 函数: FAngelscriptPropertyDesc / VerifyRepFunc / generated property flags
// 位置: AngelscriptEngine.h:834-850;
//       AngelscriptEngine.cpp:2532-2578;
//       AngelscriptClassGenerator.cpp:2945-2958
// ============================================================================
bool bReplicated = false;
TEnumAsByte<ELifetimeCondition> ReplicationCondition = COND_None;
bool bRepNotify = false;
// ★ 复制元数据先写进 property 描述，不依赖运行时 Lua 表临时生成

bool FAngelscriptEngine::VerifyRepFunc(FString* FuncName, const TSharedRef<FAngelscriptPropertyDesc>& Property,
    const TSharedRef<FAngelscriptClassDesc>& Class, const TSharedRef<FAngelscriptModuleDesc>& Module)
{
    if (FuncName != nullptr)
    {
        auto FuncDesc = Class->GetMethod(*FuncName);
        if (!FuncDesc.IsValid())
        {
            ScriptCompileError(...);
            return false;
        }

        if (FuncDesc->Arguments.Num() > 1)
        {
            ScriptCompileError(...);
            return false;
        }

        const FAngelscriptTypeUsage& FuncArgType = FuncDesc->Arguments[0].Type;
        const FAngelscriptTypeUsage& PropType = Property->PropertyType;
        if (!FuncArgType.EqualsUnqualified(PropType))
        {
            ScriptCompileError(...);
            return false;
        }
    }
    return true;
}
// ★ ReplicatedUsing 的存在性和签名在编译阶段就被验证

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
// ★ 生成出的是真实 FProperty，后续复制流直接复用 UE 原生属性元数据
```

### 设计取舍

- slua 的好处是 Lua 侧可以在不重建宿主 `UClass` 属性表的前提下声明复制字段，还能把 `RepNotifyCondition`、监听器和 `OnRep_` 约定都留在脚本层；代价是插件内部必须维护一套影子 schema、dirty history 和 shared-serialization 缓存。
- `assignTimes + dirtyMark + historyEnd` 这套账本说明 slua 优化的不是“声明多几个 replicated 字段”，而是“Lua 动态字段如何在 UE 复制帧里增量发送”，这是桥接层特有成本。
- Angelscript 的优势是复制属性最终变成标准 `FProperty`，编译期就能验证 `ReplicatedUsing` 回调合同；代价是结构变化必须经过 class generation / reload，动态插入属性的自由度更低。

### 与 Angelscript 对比

| 观察点 | sluaunreal | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 复制字段承载形态 | `LuaNetSerialization` 承载属性 + 运行时影子 `UStruct`（`LuaNet.cpp:137-170,252-268`） | 生成真实 `FProperty` 并打 `CPF_Net`（`AngelscriptClassGenerator.cpp:2945-2958`） | 实现方式不同 |
| 脏标记与增量账本 | `dirtyMark / flatDirtyMark / arrayDirtyMark / changeHistorys / assignTimes`（`LuaNetSerialization.h:145-176`; `LuaNetSerialization.cpp:571-708,845-906`） | 复制条件和回调合同在 property desc + 生成属性元数据里固定（`AngelscriptEngine.h:834-850`; `AngelscriptClassGenerator.cpp:2945-2958`） | 实现方式不同 |
| `RepNotify` 合同校验 | 运行时按 `OnRep_<PropName>` 命名约定查 Lua 函数（`LuaNet.cpp:258-268`; `LuaNetSerialization.cpp:1080-1105`） | `VerifyRepFunc()` 校验 `ReplicatedUsing` 函数是否存在且参数匹配（`AngelscriptEngine.cpp:2532-2578`） | 实现质量差异：Angelscript 的回调合同更可审计 |
| 共享序列化优化 | `BuildSharedSerialization()` 对支持共享序列化的字段和数组预打 bitstream（`LuaNetSerialization.cpp:603-706,911-999,1054-1076`） | 本轮所读 Angelscript 复制链未见插件侧影子 shared-serialization 缓存，重点是生成原生复制属性 | 实现方式不同 |
| 复制字段动态插入 | Lua 表可在运行时声明复制字段和条件（`LuaNet.cpp:140-246`） | 复制字段需先进入脚本描述并经 class generation 落成 `FProperty`（`AngelscriptEngine.h:834-850`; `AngelscriptClassGenerator.cpp:2945-2958`） | 实现方式不同 |

---

## 深化分析 (2026-04-08 23:42:56)

### [维度 D5 / D8] slua 的防死循环是“独立 watchdog + 迟到 line hook”，Angelscript 是“VM loop detection + debugger 协调”

前文已经把 profiler 和 debug/profiler 通道拆开了，但还有一个此前没有展开的“开发期安全网”边界：slua 与 Angelscript 都在编辑器里防脚本跑飞，只是切入点不同。slua 在 `LuaState::init()` 里额外创建 `FDeadLoopCheck` 线程，这个线程每秒轮询一次是否仍有脚本帧在执行；真正触发超时时，它并不持续给 Lua VM 装 line hook，而是**等超时发生后**，才在 `LuaScriptCallGuard::onTimeout()` 里检查当前是否已有 debugger hook，若没有才注入 `lua_sethook(..., LUA_MASKLINE)`，下一行执行时抛 `"script exec timeout"`。

Angelscript 则把超时逻辑收进 VM 上下文本身。`AngelscriptLoopDetectionCallback()` 直接读 `EditorMaximumScriptExecutionTime`，每次 loop-detection callback 触发时检查 `m_loopDetectionTimer`；超时就抛脚本异常。更重要的是，它把调试器协作和“排除长原生作用域”都建模成一等机制：`FAngelscriptExcludeScopeFromLoopTimeout` 可以临时扣除原生耗时，`AngelscriptDebugServer.cpp` 在断点暂停时还会显式重置 `m_loopDetectionTimer`，避免调试停顿被误判成死循环。

```
[D5/D8] Runaway Script Guard
sluaunreal
Script call
├─ LuaScriptCallGuard ctor                         // 进入脚本帧时登记
├─ FDeadLoopCheck thread (Sleep 1s)               // 独立线程粗粒度计时
├─ timeout -> if no debugger hook
│  └─ lua_sethook(..., LUA_MASKLINE)              // 超时后才安装 line hook 抛错
└─ dtor -> scriptLeave                            // 正常返回时递减计数

Angelscript
Script context
├─ AngelscriptLoopDetectionCallback               // VM 回调周期检查
├─ EditorMaximumScriptExecutionTime               // 配置驱动阈值
├─ DebugServer pause -> reset timer               // 断点暂停时重置
└─ ExcludeScopeFromLoopTimeout                    // 长原生作用域显式排除
```

[1] 关键源码：slua 平时不挂 line hook，超时后才升级为“下一行即报错”

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaState.cpp
// 函数: LuaState::init / FDeadLoopCheck::Run / LuaScriptCallGuard::onTimeout / scriptTimeout
// 位置: 548-556, 1191-1199, 1252-1266
// ============================================================================
#if WITH_EDITOR
if(!IsRunningCommandlet())
{
    deadLoopCheck = new FDeadLoopCheck();
}
#endif
// ★ watchdog 只在 editor 非 commandlet 场景创建，正常运行不改 Lua line hook

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
    return 0;
}
// ★ 轮询粒度是 1 秒；它盯的是“脚本是否还没退出”，不是每行解释器事件

void LuaScriptCallGuard::onTimeout()
{
    auto hook = lua_gethook(L);
    if (hook == nullptr) {
        lua_sethook(L, scriptTimeout, LUA_MASKLINE, 0);
    }
}
// ★ 如果 debugger 已经占用了 hook，slua 不会强行覆盖；只在没有 hook 时注入超时报错逻辑

void LuaScriptCallGuard::scriptTimeout(lua_State *L, lua_Debug *ar)
{
    lua_sethook(L, nullptr, 0, 0);
    luaL_error(L, "script exec timeout");
}
// ★ 真正异常在下一次 line event 发生时抛出，因此 steady-state 几乎没有 line-hook 成本
```

[2] 关键源码：Angelscript 把超时、断点暂停和排除作用域收成同一套上下文语义

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 函数: EditorMaximumScriptExecutionTime / AngelscriptLoopDetectionCallback /
//       FAngelscriptExcludeScopeFromLoopTimeout / breakpoint pause handling
// 位置: AngelscriptSettings.h:132-138; AngelscriptEngine.cpp:5566-5615;
//       AngelscriptEngine.h:770-780; AngelscriptDebugServer.cpp:683-688
// ============================================================================
UPROPERTY(Config, EditDefaultsOnly, Category = "Angelscript")
float EditorMaximumScriptExecutionTime = 1.f;
// ★ 超时阈值显式可配置，而且注释直接说明“只在 editor 生效，cooked game 不工作”

void AngelscriptLoopDetectionCallback(asCContext* Context)
{
    float MaximumScriptExecutionTime = UAngelscriptSettings::Get().EditorMaximumScriptExecutionTime;
    if (MaximumScriptExecutionTime > 0)
    {
        if (Context->m_loopDetectionExclusionCounter != 0)
            return;

        double CurrentTime = FPlatformTime::Seconds();
        if (Context->m_loopDetectionTimer == -1.0)
        {
            Context->m_loopDetectionTimer = CurrentTime;
            return;
        }

        if (Context->m_loopDetectionTimer < CurrentTime - MaximumScriptExecutionTime)
        {
            Context->SetException("Script function took too long to execute. Potentially an infinite loop? (timeout controlled by EditorMaximumScriptExecutionTime setting)");
            return;
        }
    }
}
// ★ 检测逻辑直接挂在脚本上下文上，不依赖额外线程，也不需要事后再补 line hook

struct ANGELSCRIPTRUNTIME_API FAngelscriptExcludeScopeFromLoopTimeout
{
#if WITH_EDITOR
    class asCContext* Context;
    double StartTime;
    FAngelscriptExcludeScopeFromLoopTimeout();
    ~FAngelscriptExcludeScopeFromLoopTimeout();
#endif
};
// ★ 原生长操作可以显式排除，避免把合法的 editor-side heavy work 误算进脚本超时

// Reset loop detection on context so we don't trigger timeouts during breakpoints
auto* Context = (asCContext*)asGetActiveContext();
if (Context != nullptr)
{
    Context->m_loopDetectionTimer = -1.0;
}
// ★ breakpoint pause 时明确重置计时器，调试器与超时系统是显式协同关系
```

### 设计取舍

- slua 的 steady-state 成本更低，因为平时不挂 line hook；代价是检测粒度较粗，超时恢复依赖“watchdog 线程 + 下一次 line event”两段式触发。
- Angelscript 的 loop detection 更像 VM 级策略，超时阈值、断点暂停、排除作用域都能统一建模；代价是它天然更偏 editor 语义，而不是运行时热更链的附带能力。
- 两边都没有把同级别保护带进 cooked game：slua 的 watchdog 创建被 `WITH_EDITOR` 包住，Angelscript 设置注释也明确写了 cooked 不工作，因此这里不应判断为“谁实现了正式线上运行时保护”，而应判断为“开发期保护策略不同”。

### 与 Angelscript 对比

| 观察点 | sluaunreal | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 超时检测载体 | 独立 `FDeadLoopCheck` 线程轮询脚本帧（`LuaState.cpp:548-556,1191-1199`） | `asCContext` loop detection callback（`AngelscriptEngine.cpp:5566-5589`） | 实现方式不同 |
| 正常路径开销控制 | 超时前不装 line hook，超时后才 `lua_sethook`（`LuaState.cpp:1252-1266`） | VM callback 周期检查计时器（`AngelscriptEngine.cpp:5574-5588`） | 实现方式不同 |
| debugger 协作 | 只在已有 hook 时避免覆盖 debugger hook（`LuaState.cpp:1254-1258`） | 断点暂停时显式重置 loop timer（`AngelscriptDebugServer.cpp:683-688`） | 实现质量差异：Angelscript 的调试协作语义更明确 |
| 长原生作用域排除 | 本轮所读 slua runtime 未见与 `FAngelscriptExcludeScopeFromLoopTimeout` 对等的排除作用域 | 原生长操作可用 exclusion scope 扣除耗时（`AngelscriptEngine.h:770-780`; `AngelscriptEngine.cpp:5593-5615`） | 没有实现 |
| cooked game 保护 | `deadLoopCheck` 创建被 `WITH_EDITOR` 包住（`LuaState.cpp:548-556`） | 设置注释明确“不 work in cooked games”（`AngelscriptSettings.h:132-138`） | 两边都没有实现正式线上保护 |

### [维度 D11] `democpp` 暴露了 slua 的真实交付 ABI：模块名 -> 原始字节；Angelscript 则把交付物收敛成 build-aware cache

前文已经说明 slua 把入口插在 `package.searchers[2]`。本轮再往项目侧样例看，边界会更清楚：`LuaState.h` 暴露的 `LoadFileDelegate` 签名只有 `const char* fn`、`FString& filepath` 和返回值 `TArray<uint8>`；`Source/democpp/MyGameInstance.cpp` 的示例实现也只是把模块名映射到 `Content/Lua/<ModulePath>.lua`，找不到再试 `.luac`，随后把原始字节直接交给 `state->init()` 之后的 loader 链。也就是说，**slua 插件和样例工程共同定义的正式契约只有“模块名 -> 原始字节数组”**，并没有再往上提升到加密、签名、版本戳或 manifest。

Angelscript 这里的交付物约束明显更强。引擎启动时会优先寻找 `PrecompiledScript_<Config>.Cache`，不存在才回退到 `PrecompiledScript.Cache`；载入后先用 `BuildIdentifier` 检查当前构建配置，再用 `DataGuid` 对齐编译进二进制的 StaticJIT 产物，不匹配就丢弃 cache 或停用 transpiled code。这里依然不是“密码学签名”，但它至少是**正式的制品一致性治理**，不是单纯把某段脚本字节塞给解释器。

```
[D11] Delivery Artifact Contract
sluaunreal
require("Foo.Bar")
├─ LoadFileDelegate(fn, filepath) -> bytes         // 插件只要求原始字节
├─ democpp: Content/Lua/Foo/Bar.lua               // 示例优先源码
├─ fallback: Content/Lua/Foo/Bar.luac             // 其次字节码
└─ luaL_loadbuffer()                               // 无 build/hash/sign gate

Angelscript
Startup
├─ pick PrecompiledScript_<Config>.Cache          // 先找按配置分开的制品
├─ fallback PrecompiledScript.Cache
├─ Load() -> BuildIdentifier/DataGuid             // 读制品元数据
├─ reject mismatched build / JIT guid
└─ ApplyToModule_Stage1()                          // 命中后直接实例化预编译模块
```

[1] 关键源码：slua 样例工程实际交付的是“源码或字节码文件内容”，而不是更高层的包协议

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaState.h
// 文件: Reference/sluaunreal/Source/democpp/MyGameInstance.cpp
// 函数: LuaState::LoadFileDelegate / UMyGameInstance::CreateLuaState
// 位置: LuaState.h:106-110; MyGameInstance.cpp:36-64
// ============================================================================
/*
 * fn, lua file to load, fn may be a short filename
 * if find fn to load, return file size to len and file full path fo filepath arguments.
 */
typedef TArray<uint8> (*LoadFileDelegate) (const char* fn, FString& filepath);
// ★ ABI 只有“给我模块名，我回你字节数组和 filepath”

state->setLoadFileDelegate([](const char* fn, FString& filepath)->TArray<uint8> {
    FString path = FPaths::ProjectContentDir();
    FString filename = UTF8_TO_TCHAR(fn);
    path /= "Lua";
    path /= filename.Replace(TEXT("."), TEXT("/"));

    TArray<uint8> Content;
    TArray<FString> luaExts = { UTF8_TO_TCHAR(".lua"), UTF8_TO_TCHAR(".luac") };
    for (auto& it : luaExts) {
        auto fullPath = path + *it;

        FFileHelper::LoadFileToArray(Content, *fullPath);
        if (Content.Num() > 0) {
            filepath = fullPath;
            return MoveTemp(Content);
        }
    }

    return MoveTemp(Content);
});
// ★ 样例工程明确是“先源码 .lua，后字节码 .luac”；没有 hash、signature、版本校验
```

[2] 关键源码：Angelscript 的 cache 制品有配置分桶、build 校验和 JIT GUID 对齐

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp
// 函数: precompiled cache load / BuildIdentifier / Save / Load
// 位置: AngelscriptEngine.cpp:1519-1555, 2046-2056, 4284-4299;
//       PrecompiledData.h:568-612; PrecompiledData.cpp:2627-2689
// ============================================================================
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
}
// ★ 先按构建配置分桶找 cache，再检查它是否属于当前 build

FGuid DataGuid;
int32 BuildIdentifier = -1;
// ★ cache 自带 DataGuid 与 BuildIdentifier，不是纯脚本文本/字节流

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
// ★ build 配置不匹配时直接废弃制品

if (CompiledInfo != nullptr && CompiledInfo->PrecompiledDataGuid != PrecompiledData->DataGuid)
{
    UE_LOG(Angelscript, Warning, TEXT("Loaded angelscript precompiled data does not match the transpiled C++ in the game binary. Transpiled code will not be used!"));
    FJITDatabase::Get().Clear();
}
// ★ 即便 cache 能读，若和编译进二进制的 JIT guid 不一致，也会停用 transpiled code
```

### 设计取舍

- slua 的好处是接入成本低，项目侧可以自由把 bytes provider 接到本地文件、CDN、Patch 包甚至自定义解密器上；代价是插件本身不拥有统一制品治理，连“源码优先还是字节码优先”都可以被项目样例随手决定。
- Angelscript 的优势是把交付物收敛为正式 cache 制品，并显式关联 build 配置和 StaticJIT `DataGuid`；代价是热更新自由度下降，使用 fully precompiled scripts 时还会主动关闭 hot reload。
- 这里不能简单写成“Angelscript 有签名而 slua 没有”。源码证据显示 Angelscript 做的是 build/config 一致性校验，不是密码学签名；slua 则连这一级别的统一制品账本都没有。

### 与 Angelscript 对比

| 观察点 | sluaunreal | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 交付 ABI | `LoadFileDelegate` 只要求 `TArray<uint8>` + filepath（`LuaState.h:106-110`） | cache 制品携带 `DataGuid` / `BuildIdentifier`（`PrecompiledData.h:568-612`） | 实现方式不同 |
| 样例默认装载顺序 | `democpp` 先找 `.lua`，再找 `.luac`（`MyGameInstance.cpp:42-63`） | 优先按 build 配置找 `PrecompiledScript_<Config>.Cache`（`AngelscriptEngine.cpp:1519-1529`） | 实现方式不同 |
| build 配置校验 | 本轮所读 slua 样例与 runtime loader 未见对等 build gate | `IsValidForCurrentBuild()` 校验 `BuildIdentifier`（`PrecompiledData.cpp:2627-2645`） | 没有实现 |
| JIT / 产物一致性 | 本轮所读 slua 线上入口未见对等 `DataGuid` 对齐机制 | `PrecompiledDataGuid` 不匹配时清空 `FJITDatabase`（`AngelscriptEngine.cpp:1550-1555`） | 没有实现 |
| 密码学签名 | 本轮所读 slua 插件和样例都未实现 | 本轮所读 Angelscript cache 也未实现密码学签名，只做 build/guid 一致性检查 | 两边都没有实现正式签名，但 Angelscript 有更强制品一致性治理 |

### [维度 D9 / D1] slua 仓库把验证责任放在 demo game；Angelscript 把测试当成独立模块和工程能力

此前 D1 只从模块划分看到了 slua 的 runtime/editor 拆分，本轮继续沿仓库边界看，会发现它在“怎么验证自己”上也和 Angelscript 很不一样。`Reference/sluaunreal/Source/democpp/democpp.Build.cs` 直接把 `slua_unreal`、`slua_profile`、`HTTP`、`Slate` 一起拉进 demo 模块，而 `MyGameInstance.cpp` 在构造时立即创建 `LuaState`、注册 loader 和全局函数。这说明 slua 仓库默认的验证载体是**一个可运行的示例工程**，不是插件内置的自动化测试模块。

Angelscript 则明确把测试做成插件的第三模块。`Angelscript.uplugin` 直接声明 `AngelscriptTest`，`AngelscriptTest.Build.cs` 还把 `CQTest / Networking / Sockets / AngelscriptEditor` 等 editor-test 依赖收进测试模块。到具体实现层，像 `AngelscriptDirectoryWatcherTests.cpp` 这样的测试会创建临时目录、造 `FFileChangeData`、再断言 `FileChangesDetectedForReload` 队列内容，已经是标准 regression harness，而不是示例项目里的人工回归。

```
[D9/D1] Verification Topology
sluaunreal
Repository
├─ slua_unreal runtime
├─ slua_profile editor tool
└─ democpp sample module
   ├─ boot LuaState
   ├─ wire file loader
   └─ manual/runtime verification

Angelscript
Plugin
├─ AngelscriptRuntime
├─ AngelscriptEditor
└─ AngelscriptTest
   ├─ automation tests
   ├─ debugger/hotreload fixtures
   └─ engine helper macros
```

[1] 关键源码：slua 把验证入口放在 demo 模块启动路径里

```csharp
// ============================================================================
// 文件: Reference/sluaunreal/Source/democpp/democpp.Build.cs
// 文件: Reference/sluaunreal/Source/democpp/MyGameInstance.cpp
// 函数: democpp module deps / UMyGameInstance::CreateLuaState
// 位置: democpp.Build.cs:11-20; MyGameInstance.cpp:36-64
// ============================================================================
PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "HTTP" });
PrivateDependencyModuleNames.AddRange(new string[] { "slua_unreal", "slua_profile", "Slate", "SlateCore", "UMG", "HTTP" });
PublicDefinitions.Add("ENABLE_PROFILER");
// ★ demo 模块直接把 runtime、profiler 和 HTTP 都接进来，定位更像“集成样例工程”而不是测试壳
```

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Source/democpp/MyGameInstance.cpp
// 函数: UMyGameInstance ctor / CreateLuaState
// 位置: 28-33, 36-64
// ============================================================================
UMyGameInstance::UMyGameInstance() : state(nullptr)
{
    if (!HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
    {
        CreateLuaState();
    }
}

void UMyGameInstance::CreateLuaState()
{
    NS_SLUA::LuaState::onInitEvent.AddUObject(this, &UMyGameInstance::LuaStateInitCallback);

    CloseLuaState();
    state = new NS_SLUA::LuaState("SLuaMainState", this);
    state->setLoadFileDelegate(/* 省略，见上文 D11 代码 */);
    state->init();
}
// ★ 示例工程在 GameInstance 构造期就拉起 LuaState，说明仓库默认验证路径是“跑起来看行为”
```

[2] 关键源码：Angelscript 有显式测试模块和可回归的自动化断言

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Angelscript.uplugin
// 文件: Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs
// 函数: plugin modules / AngelscriptTest constructor
// 位置: Angelscript.uplugin:18-33; AngelscriptTest.Build.cs:6-50
// ============================================================================
{
    "Name": "AngelscriptTest",
    "Type": "Editor",
    "LoadingPhase": "PostDefault"
}
// ★ 测试不是仓库外 demo，而是插件正式模块的一部分

public class AngelscriptTest : ModuleRules
{
    public AngelscriptTest(ReadOnlyTargetRules Target) : base(Target)
    {
        PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Debugger"));
        PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Dump"));
        PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Internals"));

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
    }
}
// ★ 测试模块直接依赖 editor/runtime/socket fixture，说明它承载的是系统级回归测试
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp
// 函数: FAngelscriptDirectoryWatcherScriptQueueTest::RunTest
// 位置: 15-18, 75-102
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAngelscriptDirectoryWatcherScriptQueueTest,
    "Angelscript.Editor.DirectoryWatcher.Queue.ScriptAddAndRemove",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
// ★ 这是标准 automation test，而不是示例脚本或手工操作说明

bool FAngelscriptDirectoryWatcherScriptQueueTest::RunTest(const FString& Parameters)
{
    IFileManager& FileManager = IFileManager::Get();
    const FString RootPath = MakeTempWatcherRoot(TEXT("ScriptQueue"));
    ...
    const TArray<FFileChangeData> Changes = {
        MakeFileChange(AddedAbsolutePath, FFileChangeData::FCA_Added),
        MakeFileChange(RemovedAbsolutePath, FFileChangeData::FCA_Removed)
    };

    AngelscriptEditor::Private::QueueScriptFileChanges(Changes, Engine->AllRootPaths, *Engine, FileManager, [](const FString&)
    {
        return TArray<FAngelscriptEngine::FFilenamePair>();
    });

    TestEqual(TEXT("DirectoryWatcher.Queue.ScriptAddAndRemove should queue one added script"), Engine->FileChangesDetectedForReload.Num(), 1);
    TestEqual(TEXT("DirectoryWatcher.Queue.ScriptAddAndRemove should queue one removed script"), Engine->FileDeletionsDetectedForReload.Num(), 1);
    ...
}
// ★ 测试直接断言 reload 队列行为，能稳定回归 editor 文件监听逻辑
```

### 设计取舍

- slua 的 demo-first 方式对接入者很友好，因为示例工程天然能展示 loader、profiler、蓝图互通的真实集成方式；代价是行为回归更依赖人工验证或项目侧二次补测试。
- Angelscript 的 test-module 方式让 hot reload、debugger、Blueprint、bindings 都能做回归；代价是工程维护成本更高，测试夹具本身也是长期资产。
- 因此这里的判断不是“slua 不重视质量”，而是“质量保证手段不同”：slua 偏示例集成验证，Angelscript 偏插件内自动化回归。

### 与 Angelscript 对比

| 观察点 | sluaunreal | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 正式测试模块 | `slua_unreal.uplugin` 只声明 runtime/editor 两模块（`slua_unreal.uplugin:16-27`） | `Angelscript.uplugin` 声明独立 `AngelscriptTest`（`Angelscript.uplugin:18-33`） | 没有实现 |
| 默认验证载体 | `democpp` 示例模块直接启动 `LuaState`（`democpp.Build.cs:11-20`; `MyGameInstance.cpp:28-64`） | `AngelscriptTest` 独立模块承载 regression harness（`AngelscriptTest.Build.cs:6-50`） | 实现方式不同 |
| 自动化回归 | 本轮检索 `Reference/sluaunreal` 未见插件内 `IMPLEMENT_SIMPLE_AUTOMATION_TEST` 等测试实现 | 多个 automation test 覆盖 DirectoryWatcher、Blueprint、Bindings、HotReload 等（例如 `AngelscriptDirectoryWatcherTests.cpp:15-102`） | 没有实现 |
| editor/runtime fixture 复用 | 示例工程直接依赖 `slua_profile` 与 `HTTP`（`democpp.Build.cs:11-20`） | 测试模块显式依赖 `CQTest`、`Networking`、`Sockets`、`AngelscriptEditor`（`AngelscriptTest.Build.cs:40-49`） | 实现质量差异：Angelscript 的测试基建更系统化 |

---

## 深化分析 (2026-04-08 23:51:40)

本轮不重述前文已经展开的 `LuaFunctionAccelerator`、`hookBpScript()` 和基础 loader 主链，只补三个此前没有拆开的边界：`delegate property` 在 slua 里究竟落在哪一层、slua 如何把 Lua 调用栈映射进 UE Stats、以及 slua 自己改过的 Lua VM binary-only 能力为何没有上升为插件级打包策略。

### [维度 D2] slua 的 delegate 支持是“属性编组表上的运行时桥”，Angelscript 的 delegate 支持是“脚本类型系统里的正式类型”

前文已经说明 slua 的普通 `UFunction` 调用依赖 `LuaFunctionAccelerator`，但本轮补充后可以看得更清楚：delegate 在 slua 里并不是“额外加几个 helper API”，而是直接并入 `FProperty` 编组表。`LuaObject.cpp:2280-2310, 2987-3052` 把 `FDelegateProperty`、`FMulticastDelegateProperty`、`FMulticastInlineDelegateProperty`、`FMulticastSparseDelegateProperty` 都注册进 `regPusher / regChecker / regReferencePusher`；真正执行时，`LuaDelegate.cpp:148-176, 479-499, 523-530` 会为 Lua function 临时创建 `ULuaDelegate`，再把 `EventTrigger` 绑回 UE delegate，同时继续复用 `LuaFunctionAccelerator` 负责参数填充和返回值拆箱。

这意味着 slua 的 delegate 哲学仍然是“运行时桥接”：脚本层拿到的是 `LuaDelegateWrap` / `LuaMultiDelegateWrap` 包装，重点是把 UE 现有 delegate property 安全地接进 Lua，而不是在 Lua 语言层重新声明一个稳定的 delegate 类型。Angelscript 则相反。`AngelscriptPreprocessor.cpp:600-705` 会先把 `delegate/event` 语法生成成带 `_Inner`、`Execute/Broadcast` 的 struct；`Bind_Delegates.cpp:128-190` 再把它变成真正能创建 `FDelegateProperty`、收参数、匹配签名的脚本类型；`AngelscriptClassGenerator.cpp:1435-1515, 1770-1807, 2170-2229` 还会把 delegate 当成模块 reload 单元分析和重建。换句话说，Angelscript 的 delegate 不是“桥”，而是“类型系统的一等成员”。

```
[D2] Delegate Binding Layer
sluaunreal
FDelegateProperty / FMulticast*Property
├─ regPusher / regChecker / regReferencePusher     // 先挂进通用属性编组表
├─ LuaDelegateWrap / LuaMultiDelegateWrap          // Lua 侧只看到运行时包装
├─ ULuaDelegate transient UObject                  // 临时对象承接 EventTrigger
└─ LuaFunctionAccelerator                          // 复用参数填充与返回值拆箱

Angelscript
delegate / event declaration
├─ Preprocessor emits struct { _Inner; Execute... } // 先生成脚本侧 delegate 类型
├─ Bind_Delegates creates FDelegateProperty        // 类型系统能自己建属性/签名
├─ ClassGenerator analyzes reload requirement      // delegate 进入编译/重载模型
└─ StaticJIT native form for Execute/Broadcast     // 预编译时还能专门优化调用
```

[1] 关键源码：slua 把 delegate 并入 `FProperty` 编组表，再用临时 `ULuaDelegate` 承接执行

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaObject.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaDelegate.cpp
// 函数: pushUDelegateProperty / pushUMulticastSparseDelegateProperty / regPusher /
//       LuaMultiDelegate::Add / LuaDelegate::Execute
// 位置: LuaObject.cpp:2280-2310, 2987-3052; LuaDelegate.cpp:148-176, 479-499, 523-530
// ============================================================================
int pushUDelegateProperty(lua_State* L, FProperty* prop, uint8* parms, int i, NewObjectRecorder* objRecorder) {
    auto p = CastField<FDelegateProperty>(prop);
    FScriptDelegate* delegate = p->GetPropertyValuePtr(parms);
    return LuaDelegate::push(L, delegate, p->SignatureFunction, prop->GetNameCPP());
}

int pushUMulticastSparseDelegateProperty(lua_State* L, FProperty* prop, uint8* parms, int i, NewObjectRecorder* objRecorder) {
    auto p = CastField<FMulticastSparseDelegateProperty>(prop);
    FSparseDelegate* SparseDelegate = (FSparseDelegate*)parms;
    return LuaMultiDelegate::push(L, p, SparseDelegate, p->SignatureFunction, prop->GetNameCPP());
}

regPusher(FDelegateProperty::StaticClass(), pushUDelegateProperty);
regPusher(FMulticastDelegateProperty::StaticClass(), pushUMulticastDelegateProperty);
regPusher(FMulticastInlineDelegateProperty::StaticClass(), pushUMulticastInlineDelegateProperty);
regPusher(FMulticastSparseDelegateProperty::StaticClass(), pushUMulticastSparseDelegateProperty);
regChecker(FDelegateProperty::StaticClass(), checkUDelegateProperty);
regReferencePusher(FDelegateProperty::StaticClass(), referencePusherUDelegateProperty);
// ★ delegate 不是额外插件 API，而是通用属性编组器的正式分支

auto obj = NewObject<ULuaDelegate>((UObject*)GetTransientPackage(), ULuaDelegate::StaticClass());
obj->bindFunction(L, 2, UD->funcAcc->func);
FScriptDelegate Delegate;
Delegate.BindUFunction(obj, TEXT("EventTrigger"));
UD->delegate->AddUnique(Delegate);
// ★ Lua function 被包成临时 UObject，再回绑成 UE delegate

auto callback = [L, UD, funcAcc, &outParamCount](uint8* params, PTRINT* outParams, NewObjectRecorder* objectRecorder)
{
    UD->delegate->ProcessDelegate<UObject>(params);
    outParamCount = funcAcc->returnValue(L, 2, params, outParams, objectRecorder);
};
funcAcc->fillParam(L, 2, nullptr, callback, isLatentFunction);
// ★ delegate 执行仍然复用 LuaFunctionAccelerator，而不是另写一套参数桥
```

[2] 关键源码：Angelscript 先生成 delegate 类型，再让类型系统与 reload 系统消费它

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 函数: delegate generated struct / FScriptDelegateType::CreateProperty / Analyze
// 位置: AngelscriptPreprocessor.cpp:600-705; Bind_Delegates.cpp:133-190;
//       AngelscriptClassGenerator.cpp:1450-1515, 1770-1807, 2170-2229
// ============================================================================
GeneratedCode += FString::Printf(TEXT("struct %s {"), *DelegateName);
GeneratedCode += TEXT("_FScriptDelegate _Inner;");
GeneratedCode += FString::Printf(TEXT("%s Execute(%s) const allow_discard __generated {"), *QualifiedReturnType, *Arguments);
GeneratedCode += TEXT("if (!_Inner.IsBound()) { Throw(\"Executing unbound delegate.\"); return; }");
GeneratedCode += TEXT(" __Evt_ExecuteDelegate(_Inner);");
// ★ 预处理阶段先把 delegate 写成脚本语言里的显式 struct，而不是运行时包装壳

FProperty* CreateProperty(const FAngelscriptTypeUsage& Usage, const FPropertyParams& Params) const override
{
    auto* Prop = new FDelegateProperty(Params.Outer, Params.PropertyName, RF_Public);
    Prop->SignatureFunction = GetSignature(Usage);
    return Prop;
}

void SetArgument(const FAngelscriptTypeUsage& Usage, int32 ArgumentIndex, class asIScriptContext* Context, struct FFrame& Stack, const FArgData& Data) const override
{
    FScriptDelegate* ValuePtr = (FScriptDelegate*)Data.StackPtr;
    new(ValuePtr) FScriptDelegate();
    Stack.StepCompiledIn<FDelegateProperty>(ValuePtr);
    Context->SetArgObject(ArgumentIndex, ValuePtr);
}
// ★ delegate 类型自己知道怎样创建 FProperty、收参数、匹配签名

if (DelegateDesc->bIsMulticast)
    ScriptSignature = ScriptType->GetMethodByName("Broadcast");
else
    ScriptSignature = ScriptType->GetMethodByName("Execute");

if (DelegateDesc->bIsMulticast)
    ScriptType->SetUserData(FAngelscriptType::TAG_UserData_Multicast_Delegate);
else
    ScriptType->SetUserData(FAngelscriptType::TAG_UserData_Delegate);

for (auto& DelegateData : ModuleData.Delegates)
{
    if (ShouldFullReload(DelegateData))
        DoFullReload(ModuleData, DelegateData);
}
// ★ delegate 还是类生成器里的正式 reload 单元，不只是运行时临时桥
```

### 设计取舍

- slua 的好处是能直接覆盖 UE 已有 delegate property 体系，单播、多播、inline、sparse 四类路径都走同一套 marshaller；代价是脚本侧可见物是 runtime wrapper，类型信息主要存在于执行时。
- Angelscript 的好处是 delegate 从语法、类型系统、reload 到 precompiled/JIT 都是一条连续链；代价是实现面更宽，delegate 变化会进入编译与 reload 决策链。

### 与 Angelscript 对比

| 观察点 | sluaunreal | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| delegate 接入层级 | 挂在 `FProperty` 编组表上，脚本侧拿 `LuaDelegateWrap` / `LuaMultiDelegateWrap`（`LuaObject.cpp:2280-2310, 2987-3052`） | 先生成 delegate struct，再由 `FScriptDelegateType` 创建 `FDelegateProperty`（`AngelscriptPreprocessor.cpp:600-705`; `Bind_Delegates.cpp:133-190`） | 实现方式不同 |
| 稀疏委托支持形态 | 运行时显式分支 `FMulticastSparseDelegateProperty`（`LuaObject.cpp:2306-2310`; `LuaDelegate.cpp:166-170, 239-243, 305-313`） | 本轮所读 delegate 主链重点是脚本 delegate 类型与 reload；未见与 slua 对等的 runtime sparse-wrapper 分支 | 实现方式不同 |
| delegate 的 reload 粒度 | delegate 绑定留在运行时桥接层，本轮所读 slua 主链未见“delegate 自身是 reload 单元”的实现 | `ModuleData.Delegates` 独立分析并参与 soft/full reload（`AngelscriptClassGenerator.cpp:1770-1807, 2170-2229`） | 实现质量差异：Angelscript 的 delegate 生命周期更可审计 |
| delegate 调用热路径优化 | 复用 `LuaFunctionAccelerator` 做参数填充与回收（`LuaDelegate.cpp:489-498, 523-530`） | 预编译时可给 `Execute/Broadcast` 绑定 native form（`StaticJITBinds.cpp:963-996`） | 实现方式不同 |

### [维度 D8] slua 除了远端 Profiler，还内建了一条 “Lua call hook -> UE Stats” 的函数级采样链

前文已经拆过 slua 的远端 profiler 数据通路，但这还不是全部。继续追踪 `LuaProfiler.cpp` 后能看到，slua 还保留了一条完全不同的 profiling 路径：`setHook()` 并不走 `SluaProfilerDataManager` 的录制协议，而是直接切到 `LuaStatProfile::setHook()`，在 Lua VM 上安装 `lua_sethook`，随后由 `LuaStatProfile::profile_hook()` / `profile_coroutine_hook()` 在 call / return / tailcall / coroutine enter-exit 时驱动 `FLuaCycleCounter::counterStart/Stop()`。`FLuaCycleCounter.cpp:25-39` 会按函数名动态创建并缓存 `TStatId`，把 Lua 函数直接挂进 UE Stats 系统；`LuaStatProfile.cpp:354-396` 还支持 `LuaStatProfileBlackList.ini` 黑名单和 hook level 过滤。

这个补充很关键，因为它说明 slua 的“性能工具”并不只有远端 profile server。一条链是前文已经分析过的“录制/回放/远端查看”；另一条链是“把 Lua 函数栈映射成 UE Stats 事件”，更适合在引擎现有性能面板里看热函数。Angelscript 当前仓库里的 profiling primitive 更偏手动埋点：`Bind_Stats.cpp:4-87` 暴露 `FStatID` 与 `FScopeCycleCounter`，`FCpuProfilerTraceScoped.h:14-27` 则暴露显式 CPU trace scope。也就是说，Angelscript 让脚本作者主动创建 scope；slua 则额外提供“自动把 Lua 调用栈灌进 Stats”的钩子模式。

```
[D8] Profiling Entry Shape
sluaunreal
Lua function call / return
├─ LuaProfiler::setHook()                          // 切换到 LuaStatProfile 模式
├─ lua_sethook(profile_hook / profile_coroutine_hook)
├─ FLuaCycleCounter::CreateStatId(funcName)        // 按函数名动态建 UE Stat
├─ blacklist + hook level                          // 配置文件过滤
└─ UE Stats timeline / log                         // 不依赖远端 profiler server

Angelscript
Script code
├─ FStatID("Name") + FScopeCycleCounter            // 手动创建 stats scope
└─ FCpuProfilerTraceScoped("Event")                // 手动创建 CPU trace scope
```

[1] 关键源码：slua 的自动函数级 Stats 采样链

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaProfiler.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaStatProfile.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/FLuaCycleCounter.cpp
// 函数: setHook / profile_hook / profile_coroutine_hook / counterStart
// 位置: LuaProfiler.cpp:387-405; LuaStatProfile.cpp:42-170, 184-279, 354-396;
//       FLuaCycleCounter.cpp:25-39, 50-66
// ============================================================================
int setHook(lua_State* L)
{
    const bool enable = !!lua_toboolean(L, 1);
    LuaStatProfile::setHook(L, enable);
    if (!lua_isnil(L, 2))
    {
        const bool openLog = !!lua_toboolean(L, 2);
        FLuaCycleCounter::setLogSwitcher(openLog);
    }
    return 0;
}
// ★ profiler Lua API 可以直接切到 Stats hook 模式，而不是只能走远端 profiler

void LuaStatProfile::setHook(lua_State* L, bool enable)
{
    blackScripts.Empty();
    const FString BlackListFileName = FPaths::ProjectConfigDir() / TEXT("LuaStatProfileBlackList.ini");
    GConfig->GetArray(TEXT("ScriptBlackList"), TEXT("+BlackList"), blackScripts, BlackListFileName);
    switcher = enable;
    if (enable) {
        lua_sethook(L, profile_hook, LUA_MASKRET | LUA_MASKCALL, 0);
        ...
        luaStateStack.Add(LuaStackInfo(L, 0));
    }
}
// ★ 可以按 ini 黑名单过滤脚本，并且 hook level 是运行时可调的

if (ar->event == LUA_HOOKCALL)
{
    FLuaCycleCounter::counterStart(counterName, 4, luaStateStack.Num());
}
else if (ar->event == LUA_HOOKRET)
{
    FLuaCycleCounter::counterStop(5, luaStateStack.Num(), counterName);
}
// ★ 普通调用、返回、尾调用、协程切换都被映射成 UE Stats 事件

TStatId statId = FDynamicStats::CreateStatId<FStatGroup_STATGROUP_LuaCouter>(CounterName);
counter->Start(statId);
luaFunctionStatIdMap.Add(CounterName, statId);
// ★ 每个 Lua 函数名第一次出现时动态生成 TStatId，之后直接复用
```

[2] 关键源码：Angelscript 暴露的是手动 profiling primitive，而不是自动脚本栈 hook

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Stats.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/FCpuProfilerTraceScoped.h
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FCpuProfilerTraceScoped.cpp
// 函数: Bind_Stats / FCpuProfilerTraceScoped / Bind_TraceCPUProfilerEventScoped
// 位置: Bind_Stats.cpp:4-87; FCpuProfilerTraceScoped.h:14-27;
//       Bind_FCpuProfilerTraceScoped.cpp:4-13
// ============================================================================
DECLARE_STATS_GROUP(TEXT("Angelscript"), STATGROUP_Angelscript, STATCAT_Advanced);

FScriptStatID(const FName& Name)
{
    FString NameStr = Name.ToString();
    StatID = FDynamicStats::CreateStatId<FStatGroup_STATGROUP_Angelscript>(NameStr);
}

FScriptScopeCycleCounter(const FScriptStatID& StatID)
    : Counter(StatID.StatID)
{
}
// ★ Angelscript 侧也能接 UE Stats，但入口是“显式构造 scope”

FCpuProfilerTraceScoped(const FName& EventID)
{
    FCpuProfilerTrace::OutputBeginDynamicEvent(EventID);
}

~FCpuProfilerTraceScoped()
{
    FCpuProfilerTrace::OutputEndEvent();
}
// ★ CPU trace 也是显式 RAII scope，没有自动按脚本函数装 hook

FCpuProfilerTraceScoped_.Constructor("void f(const FName& EventID)", [](FCpuProfilerTraceScoped* Address, const FName& EventID)
{
    new(Address) FCpuProfilerTraceScoped(EventID);
});
// ★ 绑定层只是把原生 scope 暴露给脚本
```

### 设计取舍

- slua 的优势是即便不开远端 profiler，也能把 Lua 函数热度直接投进 UE Stats；代价是 call hook 会带来额外运行时开销，并且还要处理 profiler hook 与 stats hook 的互斥关系（`LuaProfiler.cpp:347-352`）。
- Angelscript 的优势是 profiling primitive 很薄，只有脚本显式创建 scope 才有成本；代价是没有 slua 这种“自动脚本函数级时间线”。

### 与 Angelscript 对比

| 观察点 | sluaunreal | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 自动函数级采样 | `lua_sethook` + `LuaStatProfile` 自动记录 call/ret/tailcall/coroutine（`LuaStatProfile.cpp:42-170, 184-279, 354-396`） | 本轮所读 profiling primitive 未见对等自动脚本栈 hook | 没有实现 |
| UE Stats 集成方式 | `FLuaCycleCounter` 按函数名动态建 `TStatId`（`FLuaCycleCounter.cpp:25-39`） | `FStatID` / `FScopeCycleCounter` 由脚本显式创建（`Bind_Stats.cpp:6-87`） | 实现方式不同 |
| CPU trace 入口 | 本轮所读 slua Stats 路径未见与 `FCpuProfilerTraceScoped` 对等的通用显式 trace scope | `FCpuProfilerTraceScoped` 是显式 RAII trace primitive（`FCpuProfilerTraceScoped.h:14-27`; `Bind_FCpuProfilerTraceScoped.cpp:4-13`） | 实现方式不同 |
| 过滤控制 | `LuaStatProfileBlackList.ini` + hook level（`LuaStatProfile.cpp:354-396`） | 本轮所读 profiling primitive 未见对等 blacklist/level 过滤 | 没有实现 |

### [维度 D11] slua 的 Lua VM 已经补了 `onlyluac`，但插件 runtime 没把它升级成正式部署策略

前文已经说明 slua 的打包边界主要是 `LoadFileDelegate`，本轮补到更细后可以确认一个容易误判的点：slua 的确改了 Lua VM 本体。`External/lua/lstate.cpp:236-240` 给 `lua_State` 增加了 `onlyluac` 开关，`External/lua/ldo.cpp:763-775` 在 parser 里也明确写成“如果 `onlyluac != 0` 就只走 `luaU_undump` 的 binary 路径”。但顺着插件 runtime 主链往上追，`LuaState.cpp:131-155` 的 `loader()` 仍然只是拿 `LoadFileDelegate` 返回的 bytes 直接 `luaL_loadbuffer()`，而 `Source/sluaunreal/Source/democpp/MyGameInstance.cpp:42-58` 的样例 loader 仍然按 `.lua -> .luac` 顺序尝试。按本轮对 `Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal` 的检索，未见 `lua_setonlyluac` 调用点。

这说明 slua 的 binary-only 能力目前停在“定制过第三方 Lua runtime”这一层，还没有上升为插件运行时的统一打包策略。换言之，slua 并不是没有“只加载字节码”的底层能力，而是没有把这件事做成插件级配置、默认行为或制品协议。Angelscript 在这点上更完整：`FAngelscriptEngineConfig` 明确暴露 `bGeneratePrecompiledData / bIgnorePrecompiledData`，`AngelscriptEngine.cpp:1425-1456, 1513-1558, 2046-2056` 会根据运行模式决定是否只用 `PrecompiledScript_*.Cache`、校验 build/config/JIT GUID，并在 fully precompiled 模式下直接关闭 hot reload。差距不在“底层有没有能力”，而在“策略是否已经被产品化”。

```
[D11] Packaging Gate Placement
sluaunreal
External/lua
├─ onlyluac flag in lua_State                      // 底层 VM 已支持 binary-only
└─ lua_setonlyluac()                              // 但本轮未见 runtime 调用点

slua_unreal runtime
├─ LoadFileDelegate(fn) -> bytes                  // 宿主决定给什么字节
├─ LuaState::loader() -> luaL_loadbuffer()        // 插件不判断 text/binary 策略
└─ democpp tries .lua then .luac                  // 样例仍默认源码优先

Angelscript
FAngelscriptEngineConfig
├─ bGeneratePrecompiledData / bIgnorePrecompiledData
├─ choose PrecompiledScript_<Config>.Cache
├─ validate BuildIdentifier + DataGuid
└─ disable hot reload when fully precompiled
```

[1] 关键源码：slua 的 binary-only 能力存在于外部 Lua patch，但 runtime 主链没有接线

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/External/lua/lstate.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/External/lua/ldo.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaState.cpp
// 文件: Reference/sluaunreal/Source/democpp/MyGameInstance.cpp
// 函数: lua_setonlyluac / f_parser / LuaState::loader / UMyGameInstance::CreateLuaState
// 位置: lstate.cpp:236-240; ldo.cpp:763-775; LuaState.cpp:131-155;
//       MyGameInstance.cpp:42-58
// ============================================================================
L->onlyluac = 0;

LUA_API void lua_setonlyluac(lua_State *L, int v) {
    L->onlyluac = v;
}
// ★ slua 自带的 Lua runtime 确实扩了 binary-only 开关

if (L->onlyluac == 0) {
    int c = zgetc(p->z);
    if (c == LUA_SIGNATURE[0]) {
        cl = luaU_undump(L, p->z, p->name);
    } else {
        cl = luaY_parser(L, p->z, &p->buff, &p->dyd, p->name, c);
    }
} else {
    cl = luaU_undump(L, p->z, p->name);
}
// ★ 打开 onlyluac 后，parser 会拒绝 text chunk，只接 binary chunk

TArray<uint8> LuaState::loadFile(const char* fn, FString& filepath) {
    if (loadFileDelegate) return loadFileDelegate(fn, filepath);
    return TArray<uint8>();
}

int LuaState::loader(lua_State* L) {
    TArray<uint8> buf = state->loadFile(fn, filepath);
    if (luaL_loadbuffer(L, (const char*)buf.GetData(), buf.Num(), chunk) == 0) {
        return 1;
    }
    return 0;
}
// ★ runtime 主入口只认“给我 bytes”，没有在这里把 onlyluac 做成正式策略

TArray<FString> luaExts = { UTF8_TO_TCHAR(".lua"), UTF8_TO_TCHAR(".luac") };
for (auto& it : luaExts) {
    auto fullPath = path + *it;
    FFileHelper::LoadFileToArray(Content, *fullPath);
    if (Content.Num() > 0) {
        filepath = fullPath;
        return MoveTemp(Content);
    }
}
// ★ 样例工程默认仍然是先源码、后字节码
```

[2] 关键源码：Angelscript 把部署策略显式提升为运行时配置与制品校验

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp
// 函数: FAngelscriptEngineConfig / precompiled load / IsValidForCurrentBuild
// 位置: AngelscriptEngine.h:64-79; AngelscriptEngine.cpp:1425-1456, 1513-1558, 2046-2056;
//       PrecompiledData.cpp:2627-2649
// ============================================================================
struct ANGELSCRIPTRUNTIME_API FAngelscriptEngineConfig
{
    bool bSimulateCooked = false;
    bool bGeneratePrecompiledData = false;
    bool bDevelopmentMode = false;
    bool bIgnorePrecompiledData = false;
    ...
};
// ★ 打包/部署模式是引擎配置的一部分，而不是藏在第三方 runtime patch 里

bUsePrecompiledData = !bGeneratePrecompiledData && !RuntimeConfig.bIgnorePrecompiledData
    && !RuntimeConfig.bRunningCommandlet && !WITH_EDITOR && !bScriptDevelopmentMode;

if (bUsePrecompiledData)
{
    Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript_Development.Cache");
    ...
    PrecompiledData->Load(Filename);
    if (!PrecompiledData->IsValidForCurrentBuild())
    {
        delete PrecompiledData;
        PrecompiledData = nullptr;
    }
    if (CompiledInfo != nullptr && CompiledInfo->PrecompiledDataGuid != PrecompiledData->DataGuid)
    {
        FJITDatabase::Get().Clear();
    }
}
// ★ 这里把 build 配置、cache 与 JIT GUID 都做成正式 gate

if (PrecompiledData != nullptr && bUsePrecompiledData && !bScriptDevelopmentMode)
{
    bUsedPrecompiledDataForPreprocessor = true;
    ModulesToCompile = PrecompiledData->GetModulesToCompile();
    UE_LOG(Angelscript, Warning, TEXT("Using fully precompiled scripts. Hot reloading is disabled for this run."));
}
// ★ fully precompiled 模式下，hot reload 行为也被一并定义清楚

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
#endif
}
// ★ build gate 不是隐式约定，而是制品格式的一部分
```

### 设计取舍

- slua 的好处是保留了很大的项目侧自由度。宿主工程完全可以自己决定是否只下发 `.luac`、是否加密、是否从 CDN 拉字节；代价是这些策略没有被插件产品化，默认样例甚至仍是源码优先。
- Angelscript 的好处是部署策略、制品校验和 hot reload 约束都已经显式化；代价是运行模式切换不如 slua 的 bytes-loader 自由。

### 与 Angelscript 对比

| 观察点 | sluaunreal | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 底层 binary-only 能力 | Lua VM 已补 `onlyluac`（`lstate.cpp:236-240`; `ldo.cpp:763-775`） | 不是 Lua 方案，无此层能力对照 | 实现方式不同 |
| 插件层是否接线 binary-only | 本轮检索 `Source/slua_unreal` 未见 `lua_setonlyluac` 调用；runtime 主链仍是 `LoadFileDelegate -> luaL_loadbuffer`（`LuaState.cpp:131-155`） | precompiled/cooked 策略直接受 `FAngelscriptEngineConfig` 控制（`AngelscriptEngine.h:64-79`; `AngelscriptEngine.cpp:1425-1456`） | 实现质量差异：Angelscript 的部署策略更显式 |
| 样例默认脚本形态 | `democpp` 先找 `.lua` 再找 `.luac`（`MyGameInstance.cpp:42-58`） | 非开发模式优先 `PrecompiledScript_*.Cache`（`AngelscriptEngine.cpp:1513-1529`） | 实现方式不同 |
| build / 制品校验 | 本轮所读 slua runtime 未见对等 build/config gate | `BuildIdentifier` + `DataGuid` 双重校验（`AngelscriptEngine.cpp:1533-1555`; `PrecompiledData.cpp:2627-2649`） | 没有实现 |
| 预编译模式行为约束 | 本轮所读 slua loader 未见“只用字节码时关闭某些开发能力”的统一策略 | fully precompiled 时显式禁用 hot reload（`AngelscriptEngine.cpp:2046-2056`） | 没有实现 |

---

## 深化分析 (2026-04-09 00:02:33)

### [维度 D2] `Tools/README` 首次把 slua 三层导出策略的“优先级”写成显式规则

前面的轮次已经从源码侧证明 `sluaunreal` 是 `Reflection + LuaCppBinding + lua-wrapper` 的混合方案，但这一轮新增的关键证据来自 `Reference/sluaunreal/Tools/README.md:21-31`：文档直接把三者排了优先级，而且明确写成“先 `Reflection`，再 `LuaCppBinding`，最后才考虑 `lua-wrapper`”，并限制 `lua-wrapper` 只服务于“不支持反射、也不适合前两层”的引擎 `USTRUCT`。这比单看 `LuaWrapper*.inc` 更进一步，因为它说明 slua 团队自己也把静态 wrapper 定义为“最后补位工具”，而不是绑定体系的主平面。

对照 Angelscript，这里最值得补的一点不是“也有生成器”，而是“生成器是否承担正式覆盖账本”。`AngelscriptFunctionTableExporter.cs:21-53,56-95` 在 UHT exporter 里扫描整个 session，失败项会写进 `AS_FunctionTable_SkippedEntries` / `AS_FunctionTable_SkippedReasonSummary`。因此两个项目虽然都存在 codegen，但 slua 的显式规则强调“wrapper 只做最后补位”，Angelscript 的显式规则则更像“function table 是 BlueprintCallable 覆盖账本的一部分”。

```
[D2] Generator Contract
sluaunreal
├─ Reflection first                               // 首选反射
├─ LuaCppBinding second                           // 其次模板推导
└─ lua-wrapper last                               // 文档明示最后补位
   └─ engine USTRUCT gap only                     // 只补指定缺口

Angelscript
├─ UHT exporter scan                              // 编译期扫描
├─ function signature reconstruction              // 重建 BlueprintCallable 签名
└─ skipped CSV / reason summary                   // 失败项进入正式账本
```

[1] slua 的工具文档把 `lua-wrapper` 定义成第三顺位补位层

```md
<!-- ====================================================================== -->
<!-- 文件: Reference/sluaunreal/Tools/README.md                              -->
<!-- 位置: 21-31                                                             -->
<!-- ====================================================================== -->
lua-wrapper is a supplement to the lua export interface in slua-unreal, which supports three types of interface export:
1. Reflection, any type that supports blueprint can be accessed directly in lua by reflection
2. LuaCppbinding, exporting lua interface through automatic derivation of C++ template
3. lua-wrapper, through the static code generation to export the interface that is not supported by the above two methods

so, the scope of lua-wrapper is:
1. Exporting custom types is not supported
2. Exporting reflective types is not supported
3. The export type is limited to the USTRUCT type in the engine
4. Use reflection or LuaCppBinding to export the type first, and finally consider using lua-wrapper
<!-- ★ 新增证据点不在“有 wrapper”，而在官方文档把它定性成第三顺位补位层 -->
```

[2] Angelscript 的 UHT exporter 把失败条目纳入正式审计产物

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs
// 函数: Export / CountBlueprintCallableFunctions
// 位置: 21-53, 65-95
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
        CountBlueprintCallableFunctions(module.ShortName, module.ScriptPackage, skippedEntries,
            ref classCount, ref functionCount, ref reconstructedCount, ref skippedCount);
    }
    WriteSkippedEntriesCsv(factory, skippedEntries);
    WriteSkippedReasonSummaryCsv(factory, skippedEntries);
}
// ★ 失败原因会被正式落盘，因此导出链本身就是覆盖账本
```

### 与 Angelscript 对比

| 观察点 | sluaunreal | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 显式优先级规则 | `Tools/README.md:21-31` 明文规定 `Reflection -> LuaCppBinding -> lua-wrapper` | 本轮所读导出链更强调统一 UHT 覆盖账本 | 实现方式不同 |
| 生成器职责定义 | wrapper 只补 engine `USTRUCT` 缺口（`Tools/README.md:21-31`） | function table 面向 `BlueprintCallable/Pure` UFunction（`AngelscriptFunctionTableExporter.cs:21-95`） | 实现方式不同 |
| 失败项可审计性 | 本轮新增证据仍停在“职责规则已写清”，未见同级 skipped ledger | `WriteSkippedEntriesCsv()` / `WriteSkippedReasonSummaryCsv()` 明确落盘（`AngelscriptFunctionTableExporter.cs:43-45`） | 实现质量差异：Angelscript 的生成覆盖更可审计 |

### [维度 D3] Blueprint 主动调用桥的“名字解析规则”差异，比 `FLuaBPVar` 本身更关键

前面的轮次已经展开过 `FLuaBPVar` 的装箱语义；这一轮真正新增的点是“Blueprint 作者怎样知道该填什么函数名”。`LuaBlueprintLibrary.cpp:60-66,88-94` 直接 `LuaState::get(TCHAR_TO_UTF8(*funcname))`，失败就打印 `"Can't find lua member function named %s to call"`。也就是说，slua 的 Blueprint 主动调用桥并没有在这一层引入 `ScriptName`、前后缀裁剪或 namespace 规则，作者必须知道 Lua 全局符号的原始名字。

Angelscript 则把这件事前移到绑定期。`Helper_FunctionSignature.h:85-175` 会先处理 `ScriptName`，再裁掉 `K2_ / BP_ / AS_ / Received_ / Receive` 前缀，并对 Blueprint library namespace 做 prefix/suffix strip。新增结论不是“Angelscript 更静态”这种泛论，而是：在 Blueprint 主动调用这条作者路径上，slua 依赖运行时字符串约定，Angelscript 依赖绑定期名字规范化，因此 discoverability（可发现性）与出错点天然不同。

```
[D3] Name Resolution Boundary
sluaunreal
Blueprint string
├─ raw FunctionName                               // 作者手填原始符号名
└─ LuaState::get(name)
   └─ runtime log if missing                      // 名字错了到运行时才暴露

Angelscript
UFunction metadata
├─ ScriptName / prefix strip                      // 绑定期规范化
├─ namespace strip for libraries                  // 绑定期整理命名空间
└─ typed declaration                              // 脚本侧看到稳定符号
```

[1] slua 的 Blueprint 主动调用桥直接使用原始字符串查找 Lua 函数

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaBlueprintLibrary.cpp
// 函数: ULuaBlueprintLibrary::CallToLuaWithArgs / CallToLua
// 位置: 60-66, 88-94
// ============================================================================
auto ls = LuaState::get(gameInstance);
if (StateName.Len() != 0) ls = LuaState::get(StateName);
if (!ls) return FLuaBPVar();
LuaVar f = ls->get(TCHAR_TO_UTF8(*funcname));
if (!f.isFunction()) {
    Log::Error("Can't find lua member function named %s to call", TCHAR_TO_UTF8(*funcname));
    return LuaVar();
}
// ★ Blueprint 主动调用面没有额外命名规约层，直接按原始字符串查 Lua 全局
```

[2] Angelscript 在绑定期就规范 Blueprint 函数名和 library 命名空间

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h
// 函数: GetScriptNameForFunction / GetScriptNamespaceForClass
// 位置: 85-175
// ============================================================================
if (InFunction->HasMetaData(NAME_Signature_ScriptName))
{
    OutScriptName = GetPrimaryScriptName(InFunction->GetMetaData(NAME_Signature_ScriptName));
}
else
{
    bool bChangedName = false;
    bChangedName |= OutScriptName.RemoveFromStart(TEXT("K2_"));
    bChangedName |= OutScriptName.RemoveFromStart(TEXT("BP_"));
    bChangedName |= OutScriptName.RemoveFromStart(TEXT("AS_"));
    if (InFunction->HasAnyFunctionFlags(FUNC_BlueprintEvent))
    {
        bChangedName |= OutScriptName.RemoveFromStart(TEXT("Received_"));
        bChangedName |= OutScriptName.RemoveFromStart(TEXT("Receive"));
    }
}
// ★ 同一个 Blueprint 函数在绑定期就被整理成脚本侧最终名字

for(const auto& Prefix : FAngelscriptEngine::BlueprintLibraryNamespacePrefixesToStrip)
{
    if(Namespace.RemoveFromStart(Prefix))
    {
        bFoundPrefix = true;
        break;
    }
}
for(const auto& Suffix : FAngelscriptEngine::BlueprintLibraryNamespaceSuffixesToStrip)
{
    if(Namespace.RemoveFromEnd(Suffix))
    {
        bFoundSuffix = true;
        break;
    }
}
// ★ Blueprint library 命名空间也在绑定期做统一清洗
```

### 与 Angelscript 对比

| 观察点 | sluaunreal | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| Blueprint 主动调用的名字来源 | Blueprint 节点传入原始 `FunctionName` 字符串（`LuaBlueprintLibrary.cpp:60-66,88-94`） | 绑定期先做 `ScriptName`/prefix/namespace 规范化（`Helper_FunctionSignature.h:85-175`） | 实现方式不同 |
| 名字错误的暴露时机 | 运行时 `Log::Error("Can't find lua member function named ...")` | 绑定期形成稳定 declaration，脚本侧不用再猜 Blueprint 原始命名 | 实现质量差异：Angelscript 的作者路径更可发现 |
| Blueprint library 命名空间处理 | 本轮所读 slua 主动调用桥未见对等规则 | prefix/suffix strip 明确存在（`Helper_FunctionSignature.h:140-175`） | 没有实现 |

### [维度 D4] slua 新增暴露出的不是另一个 loader，而是“对象级 Lua 函数缓存的失效边界”

关于 `package.searchers[2]` 的入口，前面轮次已经分析过；这一轮新增的重点是对象级缓存何时失效。`ILuaOverriderInterface::GetCachedLuaFunc()` 会把 `FunctionName -> LuaVar` 缓存在 `FuncMap` 里（`LuaOverriderInterface.h:30-46`），而 `ULuaOverrider::onLuaStateClose()` 会在 state 关闭时遍历对象表并执行 `overrideInterface->FuncMap.Empty()`（`LuaOverrider.cpp:344-360`）。这条证据链说明：对 slua 来说，脚本函数缓存的正式失效点是 `LuaState` 生命周期，而不是“脚本文件通知事件”。

把这点和 Angelscript 摆在一起看，差异会更清楚。`AngelscriptDirectoryWatcherInternal.cpp:43-89` 把文件/目录改动排进 reload 队列，`AngelscriptEngine.cpp:2743-2764` 统一消费队列。这意味着 Angelscript 的失效源头是“文件事件进入 planner”；slua 的失效源头是“VM/state 关闭或对象重绑”。它们解决的是两类不同 ownership 问题：一个是 editor reload transaction，一个是 runtime state replacement。

```
[D4] Cache Invalidation Owner
sluaunreal
Object call
├─ FuncMap[FunctionName]                           // 对象级 Lua 函数缓存
└─ onLuaStateClose()
   └─ FuncMap.Empty()                              // state 关闭时统一失效

Angelscript
File change
├─ FileChangesDetectedForReload                    // 文件事件先入队
└─ AngelscriptEngine gather
   └─ queue-driven reload                          // 由 reload planner 统一消费
```

[1] slua 的对象级 Lua 函数缓存以 `LuaState` 关闭作为正式失效点

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaOverriderInterface.h
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaOverrider.cpp
// 函数: GetCachedLuaFunc / onLuaStateClose
// 位置: LuaOverriderInterface.h:30-46,159; LuaOverrider.cpp:344-360
// ============================================================================
auto luaFuncPtr = FuncMap.Find(FunctionName);
if (!luaFuncPtr)
{
    return FuncMap.Add(FunctionName, getFromTableIndex<NS_SLUA::LuaVar>(L, selfTable, FunctionName));
}
if (L != nullptr && luaFuncPtr->getState() != L)
{
    return getFromTableIndex<NS_SLUA::LuaVar>(L, selfTable, FunctionName);
}
return *luaFuncPtr;
// ★ 对象级缓存默认持续存在，直到明确遇到 state 变更

for (auto iter : *tableMap)
{
    UObject* obj = iter.Key.Get();
    if (!obj) continue;
    ILuaOverriderInterface* overrideInterface = Cast<ILuaOverriderInterface>(obj);
    if (!overrideInterface) continue;
    overrideInterface->FuncMap.Empty();
}
// ★ 新增证据点：正式清空点挂在 onLuaStateClose，而不是脚本文件变化通知
```

[2] Angelscript 的失效源头是文件事件队列，而不是 VM 关闭

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: QueueScriptFileChanges / hot-reload gather
// 位置: AngelscriptDirectoryWatcherInternal.cpp:43-89;
//       AngelscriptEngine.cpp:2743-2764
// ============================================================================
if (AbsolutePath.EndsWith(TEXT(".as")))
{
    if (Change.Action == FFileChangeData::EFileChangeAction::FCA_Removed)
        Engine.FileDeletionsDetectedForReload.AddUnique({ AbsolutePath, RelativePath });
    else
        Engine.FileChangesDetectedForReload.AddUnique({ AbsolutePath, RelativePath });
}
// ★ 先把文件变化落成引擎队列

FileList.Append(FileChangesDetectedForReload);
FileChangesDetectedForReload.Empty();
for (const auto& DeletedFile : FileDeletionsDetectedForReload)
    FileList.AddUnique(DeletedFile);
FileDeletionsDetectedForReload.Empty();
// ★ 失效传播由 queue 驱动，而不是某个 VM/state 被关闭
```

### 与 Angelscript 对比

| 观察点 | sluaunreal | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 对象级脚本函数缓存 | `FuncMap` 缓存 `FunctionName -> LuaVar`（`LuaOverriderInterface.h:30-46,159`） | 本轮所读热重载主链未见对等的对象级 Lua-style 函数缓存账本 | 实现方式不同 |
| 正式失效源头 | `onLuaStateClose()` 统一 `FuncMap.Empty()`（`LuaOverrider.cpp:344-360`） | 文件改动先进入 reload 队列（`AngelscriptDirectoryWatcherInternal.cpp:43-89`; `AngelscriptEngine.cpp:2743-2764`） | 实现方式不同 |
| 所有权边界 | 更接近 runtime state replacement | 更接近 editor reload transaction | 实现方式不同 |

本轮只新增三点，不重复前文的宏观结论：第一，slua 官方工具文档已经把 `lua-wrapper` 定位成第三顺位补位层；第二，Blueprint 主动调用桥的关键差异其实在“名字解析规则”而不只在 `FLuaBPVar` 装箱；第三，slua 对象级 Lua 函数缓存的正式失效点是 `LuaState` 生命周期，这和 Angelscript 由文件事件驱动的 reload 队列是两种不同的所有权模型。

---

## 深化分析 (2026-04-09 00:22:13)

### [维度 D1] slua 的 Profiler 实际上横跨 `Runtime` 与 `Editor` 两个模块，而不是单独的编辑器附加件

这一轮新增的关键点不是“slua 有 profiler 面板”，而是 profiler 的所有权边界。`slua_unreal` 运行时模块在 `StartupModule()` 就启动 `SluaProfilerDataManager`，说明采样数据模型和录制状态属于 runtime 常驻能力；`slua_profile` 编辑器模块只在面板真正打开时创建 `FProfileServer`，接管 TCP 收包和 Slate 刷新。这意味着 slua 的 profiler 不是典型的“Editor-only 工具”，而是“Runtime 负责产出采样，Editor 负责 transport 与可视化”的跨模块流水线。

把这点和 Angelscript 摆在一起看，差异会更明确。当前仓库里可见的 `CodeCoverage` 由 `AngelscriptRuntime` 直接向 `AutomationController` 挂测试生命周期回调，报告也由 runtime 自己写出到 `Saved/CodeCoverage`。也就是说，Angelscript 当前的观测型基础设施更偏向“runtime 自成闭环，editor 只补工作流入口”，而 slua 的 profiler 从一开始就把 transport/UI 拆到单独 editor 模块。

```
[D1] Profiler Ownership Split
slua_unreal (Runtime)
├─ StartupModule -> StartManager                   // 建共享 profiler 数据模型
├─ LuaProfiler::takeSample                         // 运行时采样入口
│  ├─ sendMessage(socket)                          // 真机/远端流式发送
│  └─ SluaProfilerDataManager::Receive*            // 本地录制缓存
└─ ProfileDataDefine / SluaProfilerDataManager     // 运行时持有数据结构

slua_profile (Editor)
├─ RegisterNomadTabSpawner                         // 编辑器面板入口
├─ FProfileServer(8081)                            // TCP 接收 profiler 消息
└─ Tick -> SProfilerInspector::Refresh             // UI 刷新

Angelscript
├─ AngelscriptRuntime::CodeCoverage                // runtime 自己挂测试钩子并写报告
└─ AngelscriptEditor                               // 目录监听与工作流，不承载 coverage 主链
```

[1] slua 的 profiler 主链跨 `slua_unreal` 与 `slua_profile`

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/slua_unreal.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_profile/Private/slua_profile.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_profile/Private/slua_remote_profile.cpp
// 函数: Fslua_unrealModule::StartupModule / Fslua_profileModule::OnSpawnPluginTab / FProfileServer::Init
// 位置: slua_unreal.cpp:20-27; slua_profile.cpp:127-140; slua_remote_profile.cpp:52-60,67-110
// ============================================================================
void Fslua_unrealModule::StartupModule()
{
#if WITH_EDITOR
    Simulate.OnStartupModule();
#endif
    SluaProfilerDataManager::StartManager(); // ★ runtime 模块启动时就先建 profiler 共享状态
}

sluaProfilerInspector->ProfileServer = MakeShareable(new NS_SLUA::FProfileServer());
sluaProfilerInspector->ProfileServer->OnProfileMessageRecv().BindLambda([this](NS_SLUA::FProfileMessagePtr Message) {
    this->debug_hook_c(Message); // ★ editor tab 打开后才把 transport 和 UI 接上
});

ListenEndpoint.Address = FIPv4Address(0, 0, 0, 0);
ListenEndpoint.Port = (std::numeric_limits<uint16>::min() < Port) && (Port < std::numeric_limits<uint16>::max()) ? Port : 8081;
Listener = new FTcpListener(ListenEndpoint);

while (!bStop)
{
    ...
    while (conn->ReceiveData(Message))
    {
        (void)OnProfileMessageDelegate.ExecuteIfBound(Message); // ★ socket 收到的 profiler 消息在 editor 线程侧转发
    }
}
```

[2] Angelscript 的可见观测链当前更偏 runtime 自闭环

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/AngelscriptCodeCoverage.cpp
// 函数: FAngelscriptCodeCoverage::AddTestFrameworkHooks / OnTestsStopping
// 位置: 22-42, 45-64
// ============================================================================
void FAngelscriptCodeCoverage::AddTestFrameworkHooks()
{
    IAutomationControllerModule& AutomationModule =
        FModuleManager::LoadModuleChecked<IAutomationControllerModule>("AutomationController");
    IAutomationControllerManagerRef AutomationController = AutomationModule.GetAutomationController();
    AutomationController->OnTestsAvailable().AddRaw(this, &FAngelscriptCodeCoverage::OnTestsStarting);
    AutomationController->OnTestsComplete().AddRaw(this, &FAngelscriptCodeCoverage::OnTestsStopping);
}
// ★ runtime 直接接测试生命周期，不需要 editor 模块转运数据

void FAngelscriptCodeCoverage::OnTestsStopping()
{
    FString OutputDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("CodeCoverage"));
    StopRecordingAndWriteReport(OutputDir); // ★ 报告也由 runtime 自己写出
}
```

### 与 Angelscript 对比

| 观察点 | sluaunreal | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 运行时模块职责 | `slua_unreal` 启动时就初始化 profiler 数据管理器（`slua_unreal.cpp:20-27`） | `AngelscriptRuntime` 直接承载 coverage 钩子与报告输出（`AngelscriptCodeCoverage.cpp:22-64`） | 实现方式不同 |
| 编辑器模块职责 | `slua_profile` 负责 tab、TCP server、UI 刷新（`slua_profile.cpp:48-149`; `slua_remote_profile.cpp:52-110`） | 当前 editor 侧更偏工作流与热重载入口，本轮新证据未见对等 coverage transport 模块 | 实现方式不同 |
| 观测数据传输 | profiler 数据通过 `FProfileServer` 从 socket 收包后再喂给 UI（`slua_remote_profile.cpp:67-110`） | code coverage 直接在 runtime 内存中累计并写文件（`AngelscriptCodeCoverage.cpp:52-64,107-199`） | 实现方式不同 |

### [维度 D8] slua 的性能优化落点是“桥接边界微优化”，Angelscript StaticJIT 的落点是“预编译期 native form”

前文已经说明 slua 有 `Reflection / LuaCppBinding / lua-wrapper` 三层导出；这一轮新增的是“优化发生在什么边界”。`LuaWrapper.cpp:55-67` 按 UE 小版本直接包含 `LuaWrapper5.1/5.2/5.3/5.4.inc`，生成物里像 `FVector2D::GetSignVector()` 这样的调用已经被写成固定的 `lua_CFunction`，内部直接做 `self->GetSignVector()`，完全不经过 `UFunction` 查找或参数元数据扫描。换句话说，slua 的静态导出优化的是“每次跨语言 API 调用的桥接层”。

但 slua 并没有只盯着“大调用”。`LuaArray::PairsLessGC()` 还暴露了一个作者可显式选择的低 GC 遍历器：第一次迭代创建 userdata，后续迭代复用 upvalue 里的同一块 userdata，只做 `CopyCompleteValue`/`CopyScriptStruct`。这说明 slua 的性能观念是“把脚本作者能感知到的桥接热点都做细粒度微优化”。

Angelscript 的 `StaticJIT` 思路不同。`FScriptFunctionNativeForm::BindNative*()` 全部挂了 `if (!FAngelscriptEngine::bGeneratePrecompiledData) return;`，只有在生成预编译制品时才登记 native form；登记后再由 `GenerateCall()` 把脚本调用改写成 C++ `CallCode`。因此它优化的重点不是“给每个容器 API 再发一个 LessGC 版本”，而是“把脚本执行路径整体迁移到预编译/native form”。

```
[D8] Optimization Boundary
sluaunreal
├─ LuaWrapper*.inc                                // 预生成 Lua CFunction 桥
│  └─ self->Method()                              // 直接调用 native 方法
├─ LuaArray::PairsLessGC                          // 作者显式选择低 GC 遍历器
└─ LuaProfiler                                    // 采样直接走 socket 或本地记录

Angelscript
├─ BindNative* gated by bGeneratePrecompiledData  // 仅预编译模式记录 native form
├─ GenerateCall() -> C++ CallCode                 // 生成 native 调用表达式
└─ PrecompiledData + StaticJIT                    // 优化脚本执行主路径
```

[1] slua 的静态 wrapper 直接把桥接函数编进 runtime，并按 UE 版本分片

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaWrapper.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaWrapper5.2.inc
// 函数: version include / FVector2D wrapper
// 位置: LuaWrapper.cpp:55-67; LuaWrapper5.2.inc:8311-8352
// ============================================================================
#elif ((ENGINE_MINOR_VERSION==2) && (ENGINE_MAJOR_VERSION==5))
    #include "LuaWrapper5.2.inc"
// ★ 不同 UE 版本直接切不同生成物，wrapper 在编译期就被烘进 runtime

static int GetSignVector(lua_State* L) {
    CheckSelf(FVector2D);
    if (lua_isnoneornil(L, 2)) {
        auto ret = __newFVector2D();
        *ret = self->GetSignVector();
        LuaObject::push<FVector2D>(L, "FVector2D", ret, UD_AUTOGC | UD_VALUETYPE);
    } else {
        auto ret = LuaObject::checkValue<FVector2D*>(L, 2);
        if (!ret)
            luaL_error(L, "arg %d expect FVector2D type.", 2);
        *ret = self->GetSignVector();
        lua_pushvalue(L, 2);
    }
    return 1;
}
// ★ 这里直接调用 native method，没有 UFunction lookup，也没有反射参数遍历
```

[2] slua 甚至把“少分配一次 userdata”做成了脚本层显式 API

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaArray.cpp
// 函数: LuaArray::PairsLessGC / PushElementLessGC
// 位置: 498-535, 581-633
// ============================================================================
int LuaArray::PairsLessGC(lua_State* L)
{
    ...
    if (innerClass != structProp) {
        luaL_error(L, "%s arrays do not support LessGC enumeration! Only struct type arrays are supported!", TCHAR_TO_UTF8(*innerClass->GetName()));
    }
    ...
    lua_pushnil(L);
    lua_pushcclosure(L, IterateLessGC, 1);
    lua_pushvalue(L, 1);
    lua_pushinteger(L, -1);
    return 3;
}
// ★ `LessGC` 不是内部开关，而是作者可以主动选择的遍历入口

int LuaArray::PushElementLessGC(lua_State* L, LuaArray* UD, int32 index)
{
    ...
    auto genUD = (GenericUserData*)lua_touserdata(L, lua_upvalueindex(1));
    if (!genUD)
    {
        LuaObject::push(L, element, parms);
        ...
        lua_setupvalue(L, -2, 1); // ★ 第一次迭代创建 userdata，并缓存进 upvalue
    }
    else
    {
        if (genUD->flag & UD_VALUETYPE)
            inner->CopyCompleteValue(genUD->ud, parms);
        else
            uss->CopyScriptStruct(buf, parms);

        lua_pushvalue(L, lua_upvalueindex(1)); // ★ 后续复用同一份 userdata，减少 GC 压力
    }
    return 2;
}
```

[3] Angelscript `StaticJIT` 的 native form 只在预编译模式下登记，并生成 C++ 调用表达式

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.cpp
// 函数: FScriptNativeConstructor::GenerateCall / BindNativeConstructor / FScriptNativeAssignment::GenerateCall
// 位置: 96-117, 183-203
// ============================================================================
FNativeFunctionCall GenerateCall(FNativeFunctionContext& Context) const override
{
    FNativeFunctionCall Call;
    Call.CallCode = FString::Printf(TEXT("new (%s) %s("), *Context.ObjectAddress, ANSI_TO_TCHAR(Name));
    Context.AppendArgumentsTo(Call.CallCode);
    Call.CallCode += TEXT(")");
    return Call;
}
// ★ native form 的结果不是立即执行，而是生成一段后续可编译的 C++ 调用表达式

void FScriptFunctionNativeForm::BindNativeConstructor(FAngelscriptBinds& Binds, const ANSICHAR* Name, bool bTrivial, const ANSICHAR* CustomForm)
{
    if (!FAngelscriptEngine::bGeneratePrecompiledData)
        return;
    GScriptNativeForms.Add(FAngelscriptBinds::GetPreviousBind(), new FScriptNativeConstructor(Name, bTrivial, CustomForm));
}
// ★ 只有生成 precompiled data 时才登记 native form；不是所有运行模式都启用

FNativeFunctionCall GenerateCall(FNativeFunctionContext& Context) const override
{
    FNativeFunctionCall Call;
    Call.CallCode = FString::Printf(TEXT("(*((%s*)%s) = %s)"),
        ANSI_TO_TCHAR(Name),
        *Context.ObjectAddress,
        *Context.ArgumentValues[0]);
    return Call;
}
// ★ StaticJIT 优化的是脚本执行路径上的 native call 形态
```

### 与 Angelscript 对比

| 观察点 | sluaunreal | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 静态优化落点 | `LuaWrapper*.inc` 直接生成 Lua 侧桥接函数（`LuaWrapper.cpp:55-67`; `LuaWrapper5.2.inc:8311-8352`） | `StaticJIT` 生成 native form / CallCode（`StaticJITBinds.cpp:96-117,183-203`） | 实现方式不同 |
| 激活条件 | wrapper 只要命中 UE 版本分支就随 runtime 编进二进制 | `BindNative*` 只有 `bGeneratePrecompiledData` 为真时才登记（`StaticJITBinds.cpp:112-117,199-203`） | 实现方式不同 |
| 脚本作者可见的微优化 API | `PairsLessGC()` / `IterateLessGC()` 直接暴露给脚本层（`LuaArray.cpp:498-633`） | 本轮新增证据里未见对等的脚本层 `LessGC` 容器遍历 API，当前可见优化重心更偏 `StaticJIT + PrecompiledData` | 实现方式不同 |

### [维度 D11] slua 在部署侧真正产品化的是“字节加载接口”，而不是“脚本包策略”

前文已经指出 slua 的 Lua VM 有 `onlyluac` 改动；这一轮新增的关键点是：插件层真正承诺给宿主工程的能力只有 `LoadFileDelegate`。`LuaState::loader()` 只是调用 `loadFileDelegate` 取一段 `TArray<uint8>`，然后直接 `luaL_loadbuffer()`；示例工程 `MyGameInstance.cpp` 自己决定先找 `.lua`，找不到再找 `.luac`。这说明 slua 插件本体在部署侧的产品化边界非常克制: “我负责执行你给我的 bytes”，至于这些 bytes 来自源码、字节码、CDN、加密包还是热更补丁，默认都交给宿主项目。

再往下看 Lua VM patch，`lua_setonlyluac()` 确实存在，而且 parser 在 `L->onlyluac != 0` 时强制走 `luaU_undump()`。但本轮所读插件层源码没有看到对 `lua_setonlyluac()` 的调用；也就是说 binary-only 能力停留在 VM 级开关，尚未被 slua 插件包装成“正式部署模式”。这一点和 Angelscript 很不同：后者把 `bGeneratePrecompiledData / bIgnorePrecompiledData / bDevelopmentMode` 提升到 `FAngelscriptEngineConfig`，并在装载时明确选择 `PrecompiledScript_{Build}.Cache`、校验 `BuildIdentifier / DataGuid`，同时在 fully precompiled 模式下显式关闭 hot reload。

```
[D11] Packaging Ownership Boundary
sluaunreal
├─ Host Project -> setLoadFileDelegate()          // 宿主决定 bytes 从哪来
├─ LuaState::loader -> luaL_loadbuffer()          // 插件只负责执行 bytes
└─ Lua VM onlyluac flag                           // VM 级 binary-only 开关，插件未正式接线

Angelscript
├─ FAngelscriptEngineConfig                       // 部署模式进入 runtime 配置
├─ PrecompiledScript_{Build}.Cache                // 插件决定加载哪份制品
├─ BuildIdentifier + DataGuid gate                // 校验制品与当前构建
└─ fully precompiled => disable hot reload        // 运行行为一起被定义
```

[1] slua 插件层的部署边界就是 `LoadFileDelegate`

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaState.h
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaState.cpp
// 文件: Reference/sluaunreal/Source/democpp/MyGameInstance.cpp
// 函数: LoadFileDelegate / loader / setLoadFileDelegate / sample host loader
// 位置: LuaState.h:106-110, 167-189, 263-266; LuaState.cpp:131-155, 651-652;
//       MyGameInstance.cpp:41-63
// ============================================================================
typedef TArray<uint8> (*LoadFileDelegate) (const char* fn, FString& filepath);
...
// file how to loading depend on load delegation
// see setLoadFileDelegate function
LuaVar doFile(const char* fn, LuaVar* pEnv = nullptr);
void setLoadFileDelegate(LoadFileDelegate func);
...
LoadFileDelegate loadFileDelegate;
TArray<uint8> loadFile(const char* fn,FString& filepath);
// ★ 插件层公开的只是“给我 bytes”的契约

int LuaState::loader(lua_State* L) {
    LuaState* state = LuaState::get(L);
    const char* fn = lua_tostring(L,1);
    FString filepath;
    TArray<uint8> buf = state->loadFile(fn, filepath);
    if (buf.Num() > 0) {
        if (luaL_loadbuffer(L,(const char*)buf.GetData(),buf.Num(),chunk)==0) {
            return 1;
        }
    }
    return 0;
}
// ★ loader 不关心 bytes 来自源码、字节码还是热更包，它只负责把 bytes 交给 Lua VM

void LuaState::setLoadFileDelegate(LoadFileDelegate func) {
    loadFileDelegate = func;
}

state->setLoadFileDelegate([](const char* fn, FString& filepath)->TArray<uint8> {
    ...
    TArray<FString> luaExts = { UTF8_TO_TCHAR(".lua"), UTF8_TO_TCHAR(".luac") };
    for (auto& it : luaExts) {
        auto fullPath = path + *it;
        FFileHelper::LoadFileToArray(Content, *fullPath);
        if (Content.Num() > 0) {
            filepath = fullPath;
            return MoveTemp(Content);
        }
    }
    return MoveTemp(Content);
});
// ★ 样例工程自己决定“先源码、后字节码”；这个策略不在插件内部
```

[2] `onlyluac` 已经进了 VM，但插件层没有把它做成正式部署模式

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/External/lua/lstate.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/External/lua/ldo.cpp
// 函数: lua_setonlyluac / f_parser
// 位置: lstate.cpp:236-240; ldo.cpp:763-775
// ============================================================================
L->onlyluac = 0;

LUA_API void lua_setonlyluac(lua_State *L, int v) {
    L->onlyluac = v; // ★ VM 已支持 binary-only 开关
}

if (L->onlyluac == 0) {
    int c = zgetc(p->z);
    if (c == LUA_SIGNATURE[0]) {
        cl = luaU_undump(L, p->z, p->name);
    } else {
        cl = luaY_parser(L, p->z, &p->buff, &p->dyd, p->name, c);
    }
} else {
    checkmode(L, p->mode, "binary");
    cl = luaU_undump(L, p->z, p->name); // ★ 一旦开关被置位，只接受字节码
}
```

[3] Angelscript 把部署模式、制品选择与行为约束都拉进 runtime 正式配置

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp
// 函数: FAngelscriptEngineConfig / precompiled load / IsValidForCurrentBuild
// 位置: AngelscriptEngine.h:64-79; AngelscriptEngine.cpp:1425-1456, 1513-1558, 2046-2056;
//       PrecompiledData.cpp:2627-2645
// ============================================================================
struct ANGELSCRIPTRUNTIME_API FAngelscriptEngineConfig
{
    bool bGeneratePrecompiledData = false;
    bool bDevelopmentMode = false;
    bool bIgnorePrecompiledData = false;
    ...
};
// ★ 部署模式先进入 runtime config，而不是只留一个底层 VM 开关

bGeneratePrecompiledData = RuntimeConfig.bGeneratePrecompiledData;
bScriptDevelopmentMode = RuntimeConfig.bIsEditor || RuntimeConfig.bDevelopmentMode;
bUsePrecompiledData = !bGeneratePrecompiledData && !RuntimeConfig.bIgnorePrecompiledData
    && !RuntimeConfig.bRunningCommandlet && !WITH_EDITOR && !bScriptDevelopmentMode;

if (bUsePrecompiledData)
{
    Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript_Development.Cache");
    if (!IFileManager::Get().FileExists(*Filename))
        Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript.Cache");
    ...
    if (!PrecompiledData->IsValidForCurrentBuild())
    {
        delete PrecompiledData;
        PrecompiledData = nullptr;
    }
    if (CompiledInfo != nullptr && CompiledInfo->PrecompiledDataGuid != PrecompiledData->DataGuid)
    {
        FJITDatabase::Get().Clear();
    }
}
// ★ 选择哪份制品、校验 build 和 JIT GUID，都是插件主链的一部分

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
#endif
}

bool FAngelscriptPrecompiledData::IsValidForCurrentBuild()
{
    return BuildIdentifier == GetCurrentBuildIdentifier() && BuildIdentifier != -1;
}

if (PrecompiledData != nullptr && bUsePrecompiledData && !bScriptDevelopmentMode)
{
    ModulesToCompile = PrecompiledData->GetModulesToCompile();
    UE_LOG(Angelscript, Warning, TEXT("Using fully precompiled scripts. Hot reloading is disabled for this run."));
}
// ★ fully precompiled 模式的行为约束也被显式产品化
```

### 与 Angelscript 对比

| 观察点 | sluaunreal | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 部署入口 ownership | 宿主项目通过 `setLoadFileDelegate()` 决定 bytes 来源（`LuaState.h:106-110,167-189`; `LuaState.cpp:131-155,651-652`） | `FAngelscriptEngineConfig` 直接控制 precompiled/cooked 模式（`AngelscriptEngine.h:64-79`; `AngelscriptEngine.cpp:1425-1428`） | 实现方式不同 |
| 制品选择位置 | 示例工程自己决定 `.lua -> .luac` 回退顺序（`MyGameInstance.cpp:42-63`） | 插件主链自己选择 `PrecompiledScript_{Build}.Cache`（`AngelscriptEngine.cpp:1513-1529`） | 实现方式不同 |
| binary-only / build gate | VM 有 `onlyluac` 开关，但本轮新证据未见插件层正式接线，也未见 build/GUID 校验（`lstate.cpp:236-240`; `ldo.cpp:763-775`） | `BuildIdentifier` + `DataGuid` 双重校验进入正式加载链（`AngelscriptEngine.cpp:1533-1555`; `PrecompiledData.cpp:2627-2645`） | 没有实现（插件层） |
| fully precompiled 的行为定义 | 当前 loader 合同不规定“字节码模式下禁用哪些开发能力” | fully precompiled 时显式禁用 hot reload（`AngelscriptEngine.cpp:2046-2056`） | 没有实现 |

本轮新增的三点都不是前文的重复表述：第一，slua 的 profiler 真正的模块边界是“runtime 产出 + editor 转运/可视化”；第二，slua 的性能优化落点更靠近桥接层微优化，而 Angelscript StaticJIT 更靠近预编译执行路径；第三，slua 在部署侧真正产品化的是 loader 合同而不是脚本包策略，宿主工程承担了更大的发布责任边界。

---

## 深化分析 (2026-04-09 00:33:44)

### [维度 D3] slua 的 Blueprint/Lua 路由单位可以下沉到“对象实例”，而不是固定在 `UClass`

前文已经展开过 `hookBpScript()` 和 `FLuaBPVar`。本轮新增的关键点是：slua 的 Blueprint 资产并不只是在“类级别声明可被 Lua 覆写”，它还允许对象实例自己决定脚本模块。`ALuaActor` / `ULuaUserWidget` 都把 `LuaFilePath` 暴露成 `EditAnywhere` 属性；`LuaOverrider::getLuaFilePath()` 先向 CDO 取默认路径，再向实例取覆盖路径，只要两者不同就把 `bHookInstancedObj` 置为真。后续 `bindOverrideFuncs()` 不会重新走整类全量 hook，而是只补“实例 Lua 模块额外声明的函数差量”，最后再把 `luaSelfTable` 挂进 `objectTableMap[obj]`。

这和 Angelscript 的 Blueprint 互通边界明显不同。`FAngelscriptFunctionSignature` 在 bind 阶段就把 `UFunction` 规约成 `ScriptName + Namespace + Declaration`，`BindBlueprintEvent()` 再把 `Signature.ScriptName -> UFunction` 登记到 `GBlueprintEventsByScriptName[UClass*]`。换句话说，Angelscript 的可调用面是“类/函数签名级”，slua 则多了一层“对象实例选择脚本模块”的路由能力。

```
[D3] Blueprint Script Routing Unit
sluaunreal
├─ Blueprint asset exposes LuaFilePath               // 资产可配默认脚本
├─ CDO.GetLuaFilePath() -> class default module      // 类默认模块
├─ Instance.GetLuaFilePath() -> optional override    // 实例可改模块
├─ if different => bHookInstancedObj = true          // 转入按对象路由
└─ objectTableMap[obj] = luaSelfTable                // 最终绑定到具体对象

Angelscript
├─ UFunction -> ScriptName / Namespace               // 先做名字规约
├─ Signature.Declaration                             // 固化脚本声明
├─ GBlueprintEventsByScriptName[UClass]              // 以类为键登记事件
└─ call path resolved by class + function signature  // 没有对象级脚本文件路由
```

[1] slua 允许 Blueprint 资产和实例共同决定脚本模块

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaActor.h
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaUserWidget.h
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaOverrider.cpp
// 函数: LuaFilePath property / LuaOverrider::getLuaFilePath / bindOverrideFuncs
// 位置: LuaActor.h:35-37; LuaUserWidget.h:28-34;
//       LuaOverrider.cpp:1027-1049, 1257-1303
// ============================================================================
UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "slua")
FString LuaFilePath;
// ★ Blueprint 资产直接携带脚本模块路径，作者可以在资产或实例上编辑

if (func->GetNativeFunc())
{
    UObject* defaultObject = cls->GetDefaultObject();
    defaultObject->ProcessEvent(func, &luaFilePath);

    if (!bCDOLua)
    {
        FString instanceFilePath;
        obj->UObject::ProcessEvent(func, &instanceFilePath);
        if (!instanceFilePath.IsEmpty() && instanceFilePath != luaFilePath)
        {
            bHookInstancedObj = true;
            luaFilePath = MoveTemp(instanceFilePath);
        }
    }
}
// ★ 先读 CDO，再读实例；实例路径不同就切到“按对象路由”模式

if (!overridedClasses.Contains(cls) || bHookInstancedObj) {
    ...
    if (bHookInstancedObj)
    {
        auto hookedFuncsPtr = classHookedFuncNames.Find(cls);
        if (hookedFuncsPtr)
        {
            funcNames = hookedFuncsPtr->Difference(funcNames);
        }
    }
    ...
}

ULuaOverrider::addObjectTable(L, obj, luaSelfTable, bHookInstancedObj);
// ★ 类默认 hook 只做一次；实例模块只补差异函数，并把 self table 绑到具体对象
```

[2] Angelscript 的 Blueprint 入口在 bind 阶段就按 `UClass/UFunction` 固化

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp
// 函数: FAngelscriptFunctionSignature::GetScriptNameForFunction /
//       FAngelscriptFunctionSignature::GetScriptNamespaceForClass / BindBlueprintEvent
// 位置: Helper_FunctionSignature.h:85-120, 123-176;
//       Bind_BlueprintEvent.cpp:605-640
// ============================================================================
FString OutScriptName = InFunction->GetName();

if (InFunction->HasMetaData(NAME_Signature_ScriptName))
{
    OutScriptName = GetPrimaryScriptName(InFunction->GetMetaData(NAME_Signature_ScriptName));
}
...
Namespace = InType->GetAngelscriptTypeName();
...

if (Signature.bStaticInScript)
{
    Sig->StaticObject = InType->GetClass(FAngelscriptTypeUsage::DefaultUsage)->GetDefaultObject();
    ...
}
else
{
    int32 FunctionId = FAngelscriptBinds::BindMethodDirect(InType->GetAngelscriptTypeName(),
        Signature.Declaration,
        asFUNCTION(CallEventWithSignature), asCALL_GENERIC, ASAutoCaller::FunctionCaller::Make(), Sig);
    Signature.ModifyScriptFunction(FunctionId);
}

GBlueprintEventsByScriptName.FindOrAdd(CastChecked<UClass>(Function->GetOuter())).Add(Signature.ScriptName, Function);
// ★ 事件名、命名空间和回调入口都在 bind 阶段按类/函数落账，没有“实例决定脚本模块”的分支
```

### 与 Angelscript 对比

| 观察点 | sluaunreal | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| Blueprint 资产到脚本的绑定单位 | `LuaFilePath` 可在资产和实例上覆盖，`objectTableMap` 最终以对象为键存 `luaSelfTable`（`LuaActor.h:35-37`; `LuaUserWidget.h:28-34`; `LuaOverrider.cpp:414-421,1027-1049,1302-1303`） | 事件和可调用函数在 bind 阶段按 `UClass/UFunction` 固化（`Helper_FunctionSignature.h:85-176`; `Bind_BlueprintEvent.cpp:605-640`） | 实现方式不同 |
| 同类不同实例挂不同脚本模块 | 实例路径与 CDO 不同即 `bHookInstancedObj = true`，只补差异函数（`LuaOverrider.cpp:1040-1046,1265-1272`） | 本轮所读 Blueprint 绑定链未见对等“按对象实例选脚本模块”机制 | 没有实现（按对象脚本路由） |
| 路由失败的反馈位置 | `requireModule()` 失败时直接报对象名与 `LuaFilePath`（`LuaOverrider.cpp:1194-1198`） | 绑定失败主要在 `Signature` 构造或 bind 阶段退出；运行时按已登记 `UFunction` 调度 | 实现方式不同 |

### [维度 D4] slua 的“安全点”是对象构造与 `PostLoad` 完成，而不是文件事务边界

前文已经证明 slua 没有 `DirectoryWatcher + 文件 diff + ReloadReq` 这一整套事务层。本轮继续往对象生命周期里追，能更清楚地看到它真正解决的问题：**不是脚本文件怎样原子替换，而是 UObject 什么时候已经安全到足以挂 Lua override**。`LuaOverrider` 本身就是 `FUObjectCreateListener / FUObjectDeleteListener`；`tryHook()` 在对象创建时先看 `RF_NeedPostLoad / RF_NeedInitialization`，还没稳定的对象先进 `asyncLoadedObjects`，需要延迟构造的类则把 `UClass::ClassConstructor` 替换成 `CustomClassConstructor`，等原始构造完成后再 `bindOverrideFuncs()`。

这意味着 slua 的“热附着”粒度更像对象生命周期管理。它确保 Lua table、super call、RPC hook 都在对象可安全访问的时候接上，但并不回答“改了哪些脚本文件、哪些类需要重新编译、失败后应该保留哪份旧代码”。Angelscript 则正好反过来：`AngelscriptClassGenerator` 对 old/new 描述做结构 diff，给出 `SoftReload / FullReloadSuggested / FullReloadRequired` 和 `ReloadReqLines`，再由 `HotReloadTestRunner` 在 reload 后跑测试批次。

```
[D4] Safe Point Model
sluaunreal
├─ NotifyUObjectCreated                              // 监听对象创建
├─ tryHook(obj)
│  ├─ ready now -> bindOverrideFuncs()               // 已完成 PostLoad 立即接 Lua
│  ├─ need ctor deferring -> CustomClassConstructor  // 构造后再接 Lua
│  └─ need postload -> asyncLoadedObjects queue      // 等 AsyncLoadingFlush
└─ onAsyncLoadingFlushUpdate()                       // 条件满足后补挂 override

Angelscript
├─ FileChangesDetectedForReload                      // 先收文件事务
├─ old/new descriptor diff                           // 比较类/属性/函数变化
├─ ReloadReq + ReloadReqLines                        // 给出重载等级与行号
├─ PerformSoftReload / PerformFullReload             // 执行模块替换
└─ HotReloadTestRunner                               // 重载后跑测试门禁
```

[1] slua 的 hook 入口挂在对象创建、构造完成与异步加载刷新的安全点上

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaOverrider.h
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaOverrider.cpp
// 函数: LuaOverrider listener declarations / tryHook / CustomClassConstructor /
//       onAsyncLoadingFlushUpdate
// 位置: LuaOverrider.h:77-126;
//       LuaOverrider.cpp:662-706, 716-820, 976-1013
// ============================================================================
class SLUA_UNREAL_API LuaOverrider
    : public FUObjectArray::FUObjectCreateListener
    , public FUObjectArray::FUObjectDeleteListener
{
    ...
    bool tryHook(const UObjectBaseUtility* obj, bool bHookImmediate = true, bool bPostLoad = false);
    ...
    static void CustomClassConstructor(const FObjectInitializer& ObjectInitializer);
};
// ★ slua 先把自己接到 UObject 生命周期监听器上

bool LuaOverrider::tryHook(const UObjectBaseUtility* obj, bool bHookImmediate, bool bPostLoad)
{
    if (isHookable(obj))
    {
        if (IsInGameThread() && !bPostLoad)
        {
            if (!obj->HasAnyFlags(RF_NeedPostLoad) || bHookImmediate)
            {
                ...
                if (bHookImmediate)
                {
                    bindOverrideFuncs(obj, cls);
                    return true;
                }

                if (cls->ClassConstructor != CustomClassConstructor)
                {
                    classConstructors.Add(cls, cls->ClassConstructor);
                    cls->ClassConstructor = CustomClassConstructor;
                }
                ...
                return true;
            }
        }

        asyncLoadedObjects.Add(AsyncLoadedObject{ (UObject*)obj });
    }
    return false;
}
// ★ 没完成 PostLoad 的对象不立刻 hook；需要延迟构造的类则劫持 ClassConstructor

void LuaOverrider::CustomClassConstructor(const FObjectInitializer& ObjectInitializer)
{
    ...
    clsConstructor(ObjectInitializer);
    ...
    for (auto overrider : overriderList)
    {
        overrider->bindOverrideFuncs(obj, cls);
    }
}
// ★ 原始 C++ 构造完成后才补挂 Lua override，避免抢在对象半初始化状态改函数入口

void LuaOverrider::onAsyncLoadingFlushUpdate()
{
    ...
    if (obj && !obj->HasAnyFlags(RF_NeedPostLoad) && !obj->HasAnyFlags(RF_NeedInitialization))
    {
        ...
        bindOverrideFuncs(obj, cls);
    }
}
// ★ Async loading 刷新后再补挂，真正的安全点是“对象稳定可用”
```

[2] Angelscript 的安全点在“编译事务 + 重载决策 + 测试门禁”

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Testing/UnitTest.cpp
// 函数: reload diff analysis / WantsFullReload / NeedsFullReload /
//       FHotReloadTestRunner::PrepareTests / RunTests
// 位置: AngelscriptClassGenerator.cpp:1097-1258, 5865-5884;
//       UnitTest.cpp:531-655
// ============================================================================
if (PropertyDesc->PropertyType != OldPropertyDesc->PropertyType)
{
    if (ClassData.ReloadReq < EReloadRequirement::FullReloadRequired)
    {
        ClassData.ReloadReq = EReloadRequirement::FullReloadRequired;
        ClassData.ReloadReqLines.AddUnique(PropertyDesc->LineNumber);
    }
}
...
if (!OldFunctionDesc->SignatureMatches(NewFunctionDesc))
{
    if (ClassData.ReloadReq < EReloadRequirement::FullReloadRequired)
    {
        ClassData.ReloadReq = EReloadRequirement::FullReloadRequired;
        ClassData.ReloadReqLines.AddUnique(NewFunctionDesc->LineNumber);
    }
}
...
bool FAngelscriptClassGenerator::WantsFullReload(TSharedRef<FAngelscriptModuleDesc> Module)
{
    ...
    return ModuleData.ReloadReq >= EReloadRequirement::FullReloadSuggested;
}

bool FAngelscriptClassGenerator::NeedsFullReload(TSharedRef<FAngelscriptModuleDesc> Module)
{
    ...
    return ModuleData.ReloadReq >= EReloadRequirement::FullReloadRequired;
}
// ★ 先对结构变化做显式分类，再决定 soft/full reload，而不是把时机绑到对象构造

void FHotReloadTestRunner::PrepareTests(...)
{
    if (!ShouldRunUnitTestsOnHotReload())
    {
        return;
    }
    ...
    TestAfterHotReload.Add(*Module);
}

bool FHotReloadTestRunner::RunTests(FAngelscriptEngine* AngelscriptManager)
{
    if (!ShouldRunUnitTestsOnHotReload())
        return true;
    ...
    AllUnitTestsPass = RunAngelscriptUnitTests(TestBatch, AngelscriptManager, CurrentBatchOnHotReload, TotalBatchesOnHotReload);
    return AllUnitTestsPass;
}
// ★ 热重载后还可以接测试批次门禁，safe point 是“事务完成并验证通过”
```

### 与 Angelscript 对比

| 观察点 | sluaunreal | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| safe point 定义 | `NotifyUObjectCreated -> tryHook -> CustomClassConstructor / onAsyncLoadingFlushUpdate`，围绕对象可用时机（`LuaOverrider.h:77-126`; `LuaOverrider.cpp:662-706,716-820,976-1013`） | `ReloadReq`/`ReloadReqLines` 围绕 old/new 描述差异给出 reload 级别（`AngelscriptClassGenerator.cpp:1097-1258,5865-5884`） | 实现方式不同 |
| 失败/风险表达方式 | 主要靠延迟到构造完成、PostLoad 完成后再挂 hook；未见对等文件差异账本 | 属性类型、方法签名、默认值、metadata 变化都会写入 `ReloadReqLines` | 实现质量差异：Angelscript 的决策信息更可审计 |
| reload 后门禁 | 本轮所读 slua runtime 未见对等“重载后自动跑测试”路径 | `HotReloadTestRunner::PrepareTests/RunTests` 支持热重载后批量执行测试（`UnitTest.cpp:531-655`） | 没有实现 |

### [维度 D2] slua 的 extension binding 是“Lua metatable 变异 + 首次访问缓存”，不是类型签名账本

前文已经讲过 slua 的 `LuaCppBinding + LuaWrapper + reflection` 三层结构。本轮只补 extension 层真正怎么落地。`REG_EXTENSION_METHOD / REG_EXTENSION_PROPERTY` 宏最终只是把 `lua_CFunction` 或 getter/setter 塞进 `extensionMMap[UClass][Name]`。运行时 `LuaObject::newType()` 给每个 Lua 类型建一套 `.get/.set + __index/__newindex` 元方法，`searchExtensionMethod()` 再沿 `UClass -> SuperClass` 继承链查名字；如果命中的是只读静态值，`classIndex()` 还会把 getter 结果直接写回 Lua 类型表，后续访问不再经过 getter。

这套设计的优点是轻量、灵活、对 Lua 侧非常自然；代价是审计入口是“名字查表 + metatable side effect”，而不是一个统一的可枚举签名数据库。Angelscript 则相反：`FAngelscriptFunctionSignature` 先把 `UFunction` 全部参数映射成 `FAngelscriptTypeUsage`，`GetPropertyBindParams()` 再在 bind 阶段明确读写权限，最后 `BindBlueprintCallableReflectiveFallback()` 还要再过类型合法性、fallback eligibility 和重复声明检查。

```
[D2] Extension Binding Mechanics
sluaunreal
├─ REG_EXTENSION_METHOD / PROPERTY                  // 宏展开为 addExtension*
├─ extensionMMap[UClass][Name]                      // 名字到 lua_CFunction 的 map
├─ newType() -> .get / .set / __index              // 直接改 Lua 类型元表
├─ searchExtensionMethod() walk super classes       // 运行时沿继承链查找
└─ classIndex() memoizes const static values        // 首次访问后缓存到类型表

Angelscript
├─ FAngelscriptFunctionSignature::InitFromFunction  // 参数全部类型化
├─ GetPropertyBindParams()                          // 绑定期确定访问权限
├─ ShouldBind... / IsScriptDeclarationAlreadyBound  // eligibility + dedupe
└─ BindMethodDirect / reflective fallback           // 最终登记到脚本引擎
```

[1] slua 的 extension 宏最终只是把名字登记到 `UClass -> FString -> ExtensionField`

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaCppBinding.h
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaObject.cpp
// 函数: REG_EXTENSION_METHOD / REG_EXTENSION_PROPERTY /
//       LuaObject::addExtensionMethod / addExtensionProperty
// 位置: LuaCppBinding.h:487-511; LuaObject.cpp:136-155
// ============================================================================
#define REG_EXTENSION_METHOD(U,N,M) { \
    using BindType = LuaCppBinding<decltype(M),M>; \
    LuaObject::addExtensionMethod(U::StaticClass(),N,BindType::LuaCFunction, BindType::IsStatic); }

#define REG_EXTENSION_PROPERTY(U,N,GETTER,SETTER) { \
    using GetType = LuaCppBinding<decltype(GETTER),GETTER>; \
    using SetType = LuaCppBinding<decltype(SETTER),SETTER>; \
    LuaObject::addExtensionProperty(U::StaticClass(),N,GetType::LuaCFunction,SetType::LuaCFunction,GetType::IsStatic); }
// ★ 宏侧没有生成统一签名对象，只是把名字和 lua_CFunction 交给 LuaObject

void LuaObject::addExtensionMethod(UClass* cls,const char* n,lua_CFunction func,bool isStatic) {
    if(isStatic) {
        auto& extmap = extensionMMap_static.FindOrAdd(cls);
        extmap.Add(n, ExtensionField(func));
    }
    else {
        auto& extmap = extensionMMap.FindOrAdd(cls);
        extmap.Add(n, ExtensionField(func));
    }
}

void LuaObject::addExtensionProperty(UClass * cls, const char * n, lua_CFunction getter, lua_CFunction setter, bool isStatic)
{
    ...
    extmap.Add(n, ExtensionField(getter, setter));
}
// ★ extension 层的正式账本是两个 runtime map，而不是生成出来的声明数据库
```

[2] slua 在 Lua 元表里安放 `.get/.set`，并把只读静态值做首次访问缓存

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaObject.cpp
// 函数: setMetaMethods / newType / addField / searchExtensionMethod / classIndex
// 位置: LuaObject.cpp:295-322, 369-395, 741-766, 237-266
// ============================================================================
static void setMetaMethods(lua_State* L) {
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setfield(L, -3, ".get");
    lua_pushcclosure(L, LuaObject::classIndex, 1);
    lua_setfield(L, -2, "__index");
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setfield(L, -3, ".set");
    lua_pushcclosure(L, LuaObject::classNewindex, 1);
    lua_setfield(L, -2, "__newindex");
}
// ★ 每个 Lua 类型都直接带一套 getter/setter 元表

void LuaObject::addField(lua_State* L, const char* name, lua_CFunction getter, lua_CFunction setter, bool isInstance) {
    if (getter)
    {
        lua_getfield(L, isInstance ? -1 : -2, ".get");
        lua_pushcfunction(L, getter);
        lua_setfield(L, -2, name);
        lua_pop(L, 1);
    }
    ...
}
// ★ 字段不是登记到独立 registry，而是把 getter/setter 直接写进元表

int searchExtensionMethod(lua_State* L,UClass* cls,const char* name,bool isStatic=false) {
    while(cls!=nullptr) {
        TMap<FString,ExtensionField>* mapptr = isStatic?extensionMMap_static.Find(cls):extensionMMap.Find(cls);
        if(mapptr!=nullptr) {
            auto fieldptr = mapptr->Find(name);
            if (fieldptr != nullptr) {
                ...
            }
        }
        cls = cls->GetSuperClass();
    }
}
// ★ 查找发生在运行时，并沿 UClass 继承链逐级向上搜名字

if (lua_getfield(L, -1, ".get") != LUA_TNIL)
{
    if (lua_getfield(L, -1, name) != LUA_TNIL)
    {
        lua_pushvalue(L, 1);
        lua_call(L, 1, 1);
        ...
        if (lua_getfield(L, -2, ".set") != LUA_TNIL)
        {
            if (lua_getfield(L, -1, name) == LUA_TNIL)
            {
                lua_pushvalue(L, -1);
                lua_setfield(L, -3, name);
            }
        }
    }
}
// ★ 只读静态值第一次命中后会被回填进类型表，后续访问不再走 getter
```

[3] Angelscript 在 bind 入口先做类型化、权限判断和 fallback/dedupe 检查

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_PropertyBind.h
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp
// 函数: FAngelscriptFunctionSignature::InitFromFunction / GetPropertyBindParams /
//       BindBlueprintCallableReflectiveFallback
// 位置: Helper_FunctionSignature.h:178-219;
//       Helper_PropertyBind.h:17-92;
//       BlueprintCallableReflectiveFallback.cpp:374-420
// ============================================================================
for( TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It )
{
    FProperty* Property = *It;
    FAngelscriptTypeUsage Type = FAngelscriptTypeUsage::FromProperty(Property);

    if (!Type.IsValid())
    {
        bAllTypesValid = false;
        break;
    }
    ...
}
// ★ 先把每个参数都变成 FAngelscriptTypeUsage，不合法就整个签名退出

if (Property->HasAnyPropertyFlags(CPF_BlueprintVisible))
{
    Params.bCanRead = true;
    ...
}
if (Property->HasAnyPropertyFlags(CPF_BlueprintAssignable))
{
    Params.bCanRead = true;
    Params.bCanWrite = true;
    Params.bCanEdit = true;
}
// ★ 属性绑定权限在 bind 阶段显式决定，不留给运行时字符串查表

if (!ShouldBindBlueprintCallableReflectiveFallback(Function))
{
    return false;
}
if (!Signature.bAllTypesValid || Signature.ArgumentTypes.Num() > BlueprintCallableReflectiveFallbackMaxArgs)
{
    return false;
}
if (IsScriptDeclarationAlreadyBound(InType, Signature))
{
    return false;
}
// ★ fallback 还要额外过 eligibility、参数上限和重复声明检查，边界更可审计
```

### 与 Angelscript 对比

| 观察点 | sluaunreal | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 扩展绑定账本 | `extensionMMap / extensionMMap_static` 按 `UClass + Name` 存 `lua_CFunction`（`LuaCppBinding.h:487-511`; `LuaObject.cpp:90-92,136-155`） | `FAngelscriptFunctionSignature + FAngelscriptTypeUsage + GetPropertyBindParams` 在 bind 阶段落成显式类型/权限信息（`Helper_FunctionSignature.h:178-219`; `Helper_PropertyBind.h:17-92`） | 实现方式不同 |
| 运行时分发方式 | `__index/.get/.set` 元表 + 运行时沿继承链查名字（`LuaObject.cpp:237-305,741-766`） | 绑定前先做 eligibility、参数上限和重复声明检查，再注册到脚本引擎（`BlueprintCallableReflectiveFallback.cpp:374-420`） | 实现质量差异：Angelscript 的边界更可审计 |
| 首次访问缓存点 | `classIndex()` 会把 const static getter 的结果回填到 Lua 类型表（`LuaObject.cpp:256-266`） | 当前所读 Angelscript 绑定链未见对等“脚本侧首次访问再回填到类型表”的 lazy cache；其缓存主要发生在 bind/JIT 阶段 | 实现方式不同 |

---

## 深化分析 (2026-04-09 00:43:00)

### [维度 D8 / D5] slua 的 remote profiler 协议是单用途遥测流，不是可协商的调试消息总线

前几轮已经证明 slua 有完整 profiler 产线，但本轮继续下钻到 wire protocol 后，可以更精确地描述它的边界：`slua_profile` 收到的并不是“带 message type 的通用调试包”，而是一条非常薄的**长度前缀 + `FProfileMessage`** 流。`FProfileConnection::ReceiveData()` 先读 `uint32` 包长，再把整个 payload 交给 `FProfileMessage::Deserialize()`；这个消息体自身只有 `Event / Time / Linedefined / Name / ShortSrc` 和两类 memory 数组，没有版本握手、能力协商，也没有 pause/step/evaluate 这类反向控制语义。

这和 Angelscript 的 socket 层形成了很鲜明的对照。它虽然也是长度前缀二进制流，但 envelope 显式拆成 `MessageLength + MessageType + Body`；更关键的是，连接建立后第一批消息就会交换 `DebugAdapterVersion` / `DebugServerVersion`，随后才能进入 `Pause / Continue / StepIn / Variables / Evaluate / CallStack` 等多路消息处理。换句话说，slua 把网络层产品化在“高吞吐采样回传”，Angelscript 把网络层产品化在“可协商的交互式调试协议”。

```
[D8/D5] Transport Contract Focus
slua profiler stream
├─ uint32 MessageSize
└─ FProfileMessage
   ├─ Event
   ├─ Time / Linedefined
   ├─ Name / ShortSrc
   └─ memoryInfoList | memoryIncrease

Angelscript debug stream
├─ int32 MessageLength
├─ uint8 MessageType
└─ Body
   ├─ StartDebugging(DebugAdapterVersion)
   ├─ DebugServerVersion
   ├─ Pause / Continue / Step*
   ├─ Variables / Evaluate / CallStack
   └─ DebugDatabase / AssetDatabase
```

[1] 关键源码：slua 的 profiler socket 只承载 `FProfileMessage`

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_profile/Public/slua_remote_profile.h
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_profile/Private/slua_remote_profile.cpp
// 函数: NS_SLUA::FProfileMessage / FProfileConnection::ReceiveData / FProfileMessage::Deserialize
// 位置: slua_remote_profile.h:142-160;
//       slua_remote_profile.cpp:286-337, 365-387
// ============================================================================
class FProfileMessage
{
public:
    int Event;
    int64 Time;
    int Linedefined;
    FString Name;
    FString ShortSrc;
    TArray<NS_SLUA::LuaMemInfo> memoryInfoList;
    TArray<NS_SLUA::LuaMemInfo> memoryIncrease;
}; // ★ 消息体字段就是 profiler sample，自身不再区分 envelope/type/version

if (RecvMessageDataRemaining == 0)
{
    if (!Socket->HasPendingData(PendingDataSize) || (PendingDataSize < sizeof(uint32)))
    {
        return true;
    }

    FArrayReader MessagesizeData = FArrayReader(true);
    MessagesizeData.SetNumUninitialized(sizeof(uint32));
    Socket->Recv(MessagesizeData.GetData(), sizeof(uint32), BytesRead);
    MessagesizeData << RecvMessageDataRemaining;
    RecvMessageData = MakeShareable(new FArrayReader(true));
    RecvMessageData->SetNumUninitialized(RecvMessageDataRemaining);
}
// ★ 线协议先读一个 uint32 包长，然后整包反序列化；没有额外 message kind 字段

MessageReader << Event;
switch (Event)
{
case NS_SLUA::ProfilerHookEvent::PHE_MEMORY_TICK:
    MessageReader << memoryInfoList;
    return true;
case NS_SLUA::ProfilerHookEvent::PHE_MEMORY_INCREACE:
    MessageReader << memoryIncrease;
    return true;
default:
    break;
}

MessageReader << Time;
MessageReader << Linedefined;
MessageReader << Name;
MessageReader << ShortSrc;
// ★ 除 memory 事件外，其余样本都收敛成 call/return 风格的同一消息结构
```

[2] 对照源码：Angelscript 明确把 socket ABI 设计成“类型化 envelope + 版本握手”

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 函数: EDebugMessageType / FStartDebuggingMessage / FDebugServerVersionMessage /
//       SerializeDebugMessageEnvelope / HandleMessage
// 位置: AngelscriptDebugServer.h:52-80, 103-124, 670-685;
//       AngelscriptDebugServer.cpp:52-109, 791-807, 820-907
// ============================================================================
enum class EDebugMessageType : uint8
{
    Variables,
    RequestEvaluate,
    Evaluate,
    GoToDefinition,
    PingAlive,
    DebugServerVersion,
    CreateBlueprint,
    SetDataBreakpoints,
    ClearDataBreakpoints,
};
// ★ socket 层先定义多路消息类型，而不是只传 profiler sample

bool SerializeDebugMessageEnvelope(EDebugMessageType MessageType, const TArray<uint8>& Body, TArray<uint8>& OutBuffer)
{
    const int32 MessageLength = static_cast<int32>(sizeof(uint8)) + Body.Num();
    const uint8 MessageTypeByte = static_cast<uint8>(MessageType);
    Writer << const_cast<int32&>(MessageLength);
    Writer << const_cast<uint8&>(MessageTypeByte);
    OutBuffer.Append(Body);
    return true;
}
// ★ 调试协议显式把 MessageType 放进 envelope，可在同一 socket 上复用多类消息

else if (MessageType == EDebugMessageType::StartDebugging)
{
    FStartDebuggingMessage Msg;
    *Datagram << Msg;

    bIsDebugging = true;
    AngelscriptDebugServer::DebugAdapterVersion = Msg.DebugAdapterVersion;

    FDebugServerVersionMessage DebugServerVersionMessage;
    DebugServerVersionMessage.DebugServerVersion = DEBUG_SERVER_VERSION;
    SendMessageToClient(Client, EDebugMessageType::DebugServerVersion, DebugServerVersionMessage);
}
// ★ 一上来就交换 adapter/server version，说明协议把版本协商当成一等语义
```

### 补充判断

| 观察点 | sluaunreal | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| socket envelope | `uint32 size + FProfileMessage`（`slua_remote_profile.cpp:286-337`） | `int32 length + uint8 MessageType + Body`（`AngelscriptDebugServer.cpp:52-109`） | 实现方式不同 |
| 版本协商 | 本轮所读 profiler 协议未见 adapter/server version 交换 | `StartDebugging` 后交换 `DebugAdapterVersion / DebugServerVersion`（`AngelscriptDebugServer.cpp:897-907`） | 没有实现（profiler 通道） |
| 反向控制语义 | 只有样本接收与入队（`slua_remote_profile.cpp:334-337`） | `Pause/Continue/Step*/Variables/Evaluate` 全部走同一通道（`AngelscriptDebugServer.cpp:820-896,1105-1128`） | 实现方式不同 |

### [维度 D8 / D11] slua 真正做了版本治理的是 `.sluastat` profiler 制品，而不是脚本热更制品

这一点前文没有单独拎出来讲。本轮沿着 `SluaProfilerDataManager` 往下看后，可以确认 slua 最完整的“文件制品协议”其实不是 Lua 脚本包，而是 profiler 录制文件。录制开始时它会先写 `ProfileVersion` 和两个 begin index；后续每一段 frame 数据都落成 `uncompressedSize + compressedSize + compressedBuffer`，读回时若 `version != ProfileVersion` 就直接报 `"sluastat file version mismatch"` 并停止解析。也就是说，slua 把**分析制品**做成了有版本头和压缩块边界的正式文件格式。

这与前文 D11 的热更结论形成一个有意思的反差：slua 对“线上脚本字节”仍保持极薄 ABI，但对“开发期性能录制文件”反而给了明确版本化和压缩策略。Angelscript 则几乎反过来，它最正式的制品是 `PrecompiledScript*.Cache`，并用 `BuildIdentifier` 去 gate 当前 build。两边不是谁更“完整”，而是产品化重心不同：slua 把 rigor 放在观测文件，Angelscript 把 rigor 放在运行制品。

```
[D8/D11] Artifact Governance Focus
slua
├─ profiler samples
├─ FProfileDataProcessRunnable
└─ .sluastat
   ├─ ProfileVersion
   ├─ CpuBeginIndex / MemBeginIndex
   └─ [UncompressedSize][CompressedSize][CompressedBlock]*

Angelscript
├─ active script graph
├─ FAngelscriptPrecompiledData
└─ PrecompiledScript*.Cache
   ├─ BuildIdentifier
   └─ current-build validation
```

[1] 关键源码：slua 给 `.sluastat` 定义了明确的版本头和压缩块布局

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/SluaProfilerDataManager.cpp
// 函数: FProfileDataProcessRunnable::BeginRecord / SerializeLoad /
//       SerializeCompreesedDataToFile
// 位置: SluaProfilerDataManager.cpp:344-349, 664-699, 927-947
// ============================================================================
FString filePath = GenerateStatFilePath();
frameArchive = IFileManager::Get().CreateFileWriter(*filePath);
*frameArchive << ProfileVersion;
int beginIndex = 0;
*frameArchive << beginIndex << beginIndex;
// ★ 文件头先写 ProfileVersion 与两个视图起始索引，说明 `.sluastat` 有正式格式头

int32 version;
*ar << version;
if (version != ProfileVersion)
{
    UE_LOG(Slua, Warning, TEXT("sluastat file version mismatch: %d, %d"), version, ProfileVersion);
    return;
}
// ★ 读回时先校验版本，不匹配直接拒绝继续解析

int32 uncompressedSize = dataToCompress.Num();
int32 compressedSize = FCompression::CompressMemoryBound(NAME_Oodle, uncompressedSize);
FCompression::CompressMemory(NAME_Oodle, compressedBuffer, compressedSize, dataToCompress.GetData(), uncompressedSize);
ar << uncompressedSize;
ar << compressedSize;
ar.Serialize(compressedBuffer, compressedSize);
// ★ 每个块都带原始大小和压缩后大小，协议层面对 replay 文件做了块级治理
```

[2] 对照源码：Angelscript 把 build gate 放在 `PrecompiledScript*.Cache`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp
// 函数: FAngelscriptPrecompiledData::GetCurrentBuildIdentifier /
//       IsValidForCurrentBuild / InitFromActiveScript
// 位置: PrecompiledData.cpp:2627-2649
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

void FAngelscriptPrecompiledData::InitFromActiveScript()
{
    BuildIdentifier = GetCurrentBuildIdentifier();
}
// ★ Angelscript 的正式制品约束是“当前 build 能否消费这份 cache”，重点在运行制品而不是 profiling replay
```

### 补充判断

| 观察点 | sluaunreal | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 开发期观测制品 | `.sluastat` 有 `ProfileVersion` 与压缩块协议（`SluaProfilerDataManager.cpp:344-349,664-699,927-947`） | 本轮所读 profiling 相关源码未见对等独立 replay 文件协议 | 实现质量差异：slua 的 profiler 制品更产品化 |
| 运行期脚本制品 | 前文已证实脚本热更入口仍是 bytes provider ABI | `PrecompiledScript*.Cache` 直接受 `BuildIdentifier` gate（`PrecompiledData.cpp:2627-2649`） | 实现质量差异：Angelscript 的运行制品更产品化 |
| 产品化重心 | 观测链路 | 运行链路 | 实现方式不同 |

### [维度 D4 / D11] slua 对匿名脚本文本的身份治理停在 Editor 调试辅助，不是正式模块制品标识

这一点对“线上热更新脚本从网络下发”的讨论很关键。`LuaState::doString()` 在 `WITH_EDITOR` 下不会直接用原始 chunk 名运行，而是先做 `FMD5::HashAnsiString()`，把 `md5 -> 原始源码` 记进 `debugStringMap`，然后把这个 md5 当 chunk 名交给 `doBuffer()`。对应地，`getStringFromMD5()` 只在 editor 分支里生效，用于把匿名 chunk 反查回源码文本。这个机制说明 slua 确实考虑过“脚本不是来自磁盘文件、但调试时仍想看到源码”的场景。

但它的边界也很清楚：这只是**调试期的源码找回辅助**，不是部署期模块身份协议。md5 并没有和 build/version/manifest 绑定，也没有进入 `LoadFileDelegate` 的正式 ABI。Angelscript 这边则保持文件身份贯穿 compile 和 debug：`AddScriptSection()` 直接使用 `Section.AbsoluteFilename`，`EmitDiagnostics()` 也把 `Diag.Filename` 送给调试端，`CanonizeFilename()` 只做路径标准化，不改变“脚本以文件名作为主身份”的前提。

```
[D4/D11] Anonymous Script Identity
slua
├─ source string
├─ FMD5::HashAnsiString
├─ debugStringMap[md5] = source
└─ doBuffer(chunk = md5)
   └─ getStringFromMD5() only in editor

Angelscript
├─ Module->Code[].AbsoluteFilename
├─ AddScriptSection(filename, code)
├─ Diagnostics.Filename
└─ CanonizeFilename() -> normalize slash only
```

[1] 关键源码：slua 用 md5 给匿名脚本文本补一个“Editor 可回查”的临时身份

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaState.cpp
// 函数: LuaState::getStringFromMD5 / LuaState::doString
// 位置: LuaState.cpp:96-105, 739-749
// ============================================================================
#if WITH_EDITOR
int LuaState::getStringFromMD5(lua_State* L) {
    const char* md5String = lua_tostring(L, 1);
    LuaState* state = LuaState::get(L);
    FString md5FString = UTF8_TO_TCHAR(md5String);
    bool hasValue = state->debugStringMap.Contains(md5FString);
    if (hasValue) {
        auto value = state->debugStringMap[md5FString];
        lua_pushstring(L, TCHAR_TO_UTF8(*value));
    }
}
#endif
// ★ 这个接口只在 editor 下存在，职责是“把匿名 chunk 的 md5 反查回源码文本”

FString md5FString = FMD5::HashAnsiString(UTF8_TO_TCHAR(str));
debugStringMap.Add(md5FString, UTF8_TO_TCHAR(str));
return doBuffer((const uint8*)str,len,TCHAR_TO_UTF8(*md5FString),pEnv);
// ★ doString 并不把原文当 chunk 名，而是主动改成 md5；说明它默认接受“无正式文件名”的执行路径
```

[2] 对照源码：Angelscript 维持文件身份贯穿编译与诊断

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 函数: FAngelscriptEngine::CompileModule_Types_Stage1 / EmitDiagnostics /
//       FAngelscriptDebugServer::CanonizeFilename
// 位置: AngelscriptEngine.cpp:4342-4345, 4496-4504;
//       AngelscriptDebugServer.cpp:1377-1380
// ============================================================================
for (auto& Section : Module->Code)
{
    ScriptModule->AddScriptSection(TCHAR_TO_ANSI(*Section.AbsoluteFilename), TCHAR_TO_UTF8(*Section.Code), 0, 0);
}
// ★ 编译入口直接把绝对文件名交给 script module；主身份始终是文件路径

FAngelscriptDiagnostics Message;
Message.Filename = Diag.Filename;
// ★ 诊断继续沿用同一文件名，不需要额外 md5 -> source 的回查层

FString FAngelscriptDebugServer::CanonizeFilename(const FString& Filename)
{
    return Filename.Replace(TEXT("\\"), TEXT("/"));
}
// ★ 调试器只做路径标准化，而不是把匿名 chunk 二次映射成临时身份
```

### 补充判断

| 观察点 | sluaunreal | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 匿名脚本执行身份 | `doString()` 用 md5 代替 chunk 名，并仅在 editor 维护 `debugStringMap`（`LuaState.cpp:96-105,739-749`） | 直接使用 `AbsoluteFilename` 作为 compile/debug 主身份（`AngelscriptEngine.cpp:4342-4345,4496-4504`） | 实现方式不同 |
| 调试可追溯性 | 匿名字符串可在 editor 反查源码，但不构成部署协议 | 文件路径天然贯穿编译、诊断和 debug server | 实现质量差异：Angelscript 的身份链更稳定 |
| 对网络下发脚本的含义 | 证明 slua 考虑过“非文件来源脚本”的可调试性 | 更偏文件集编译和制品治理 | 实现方式不同 |

---

## 深化分析 (2026-04-09 00:52:24)

### [维度 D4 / D11] slua 的热更新入口本质是宿主提供的 `bytes provider ABI`，插件核心自己不持有包协议

本轮继续顺着 `LuaState` 往下读，可以把前文“线上热更新”说得更精确一些：slua 插件核心真正实现的是一个**宿主注入的加载 ABI**。`LuaState.h:106-110,166-190` 先把脚本来源抽象成 `LoadFileDelegate`；`LuaState.cpp:131-155,725-766` 的 `loader()` / `doFile()` 再把 delegate 返回的原始字节直接交给 `luaL_loadbuffer()`；`LuaSimulate.cpp:29-32,98-108` 则进一步证明，即使是编辑器模拟路径，也要求外部先把 loader delegate 填好，否则根本不启动。

这意味着 slua 对“线上热更”采取的是**入口开放、协议外置**策略。CDN 下载、pak 解包、加密、签名、灰度和版本分发都可以接在 delegate 前面，但这些治理动作不在插件 runtime 内部。这里必须把差距判断说清楚：这不是“没有热更新”，而是“热更新的 transport / package / integrity policy 不在插件核心里实现”。从本轮读到的执行链看，核心 runtime 的职责是“拿到 bytes + filepath 后执行”，而不是“验证这份 bytes 是否可信”。

与之相对，Angelscript 的入口更像**插件自己持有源码目录与重载边界**。`AngelscriptEngine.cpp:1326-1369` 会主动发现 `Project/Script` 和启用插件的 `Script` 根；`AngelscriptEditorModule.cpp:366-382` 又在 editor 启动时对这些根注册 `DirectoryWatcher`；编译时 `AngelscriptEngine.cpp:4342-4345` 把 `AbsoluteFilename` 直接交给 `AddScriptSection()`。它并不提供 slua 那种“任意 bytes provider”式的热更入口，但它把开发期文件身份、变更检测和编译入口都收回到了插件自身。

```
[D4/D11] Script Ingress Ownership
sluaunreal
├─ [1] Host sets LoadFileDelegate                    // 宿主决定脚本来自哪里
├─ [2] LuaState::loadFile(fn, filepath)             // runtime 只拿 bytes + path
├─ [3] loader()/doFile()                            // 直接进入 luaL_loadbuffer
├─ [4] requireModule / LuaSimulate reuse delegate   // 模块加载与模拟同样复用
└─ [5] package / crypto / signature stay outside    // 包协议不在插件 core

Angelscript
├─ [1] DiscoverScriptRoots()                        // 插件自己发现脚本根目录
├─ [2] DirectoryWatcher registers callbacks         // 插件自己监听文件变更
├─ [3] AddScriptSection(AbsoluteFilename, Code)     // 编译入口保留文件身份
└─ [4] reload queues stay in plugin/editor          // 开发期热重载闭环在插件内
```

[1] 关键源码：slua 把脚本来源抽象成 delegate，然后直接执行返回字节

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaState.h
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaState.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaSimulate.cpp
// 函数: LuaState::LoadFileDelegate / LuaState::loader / LuaState::doFile /
//       LuaState::setLoadFileDelegate / LuaSimulate::SetLuaFileLoader /
//       LuaSimulate::StartSimulateLua
// 位置: LuaState.h:106-110,166-190;
//       LuaState.cpp:131-155,651-652,725-766;
//       LuaSimulate.cpp:29-32,98-108
// ============================================================================
typedef TArray<uint8> (*LoadFileDelegate) (const char* fn, FString& filepath);
// ★ runtime 只定义一个“给我字节和解析后路径”的 ABI，没有规定下载/解密/验签协议

TArray<uint8> LuaState::loadFile(const char* fn,FString& filepath) {
    if(loadFileDelegate) return loadFileDelegate(fn,filepath);
    return TArray<uint8>();
}
// ★ 插件内部没有第二层默认 loader；如果宿主不提供 delegate，就没有脚本来源

int LuaState::loader(lua_State* L) {
    const char* fn = lua_tostring(L,1);
    FString filepath;
    TArray<uint8> buf = state->loadFile(fn, filepath);
    if(buf.Num() > 0) {
        char chunk[256];
        snprintf(chunk,256,"@%s",TCHAR_TO_UTF8(*filepath));
        if(luaL_loadbuffer(L,(const char*)buf.GetData(),buf.Num(),chunk)==0) {
            return 1;
        }
    }
    return 0;
}
// ★ delegate 返回的就是最终执行体；runtime 这里直接进入 luaL_loadbuffer

LuaVar LuaState::doFile(const char* fn, LuaVar* pEnv) {
    FString filepath;
    TArray<uint8> buf = loadFile(fn, filepath);
    if (buf.Num() > 0) {
        char chunk[256];
        snprintf(chunk,256,"@%s",TCHAR_TO_UTF8(*filepath));
        return doBuffer(buf.GetData(),buf.Num(),chunk,pEnv);
    }
    return LuaVar();
}
// ★ doFile 没有插入额外完整性校验；这说明“包治理”不在 runtime 主链里

void LuaSimulate::SetLuaFileLoader(LuaState::LoadFileDelegate InDelegate)
{
    Delegate = InDelegate;
}

void LuaSimulate::StartSimulateLua()
{
    if (Delegate == nullptr)
    {
        Log::Error("lua Simulation Error. LoadFileDelegate not set.");
        return;
    }
    SluaState = new NS_SLUA::LuaState("", nullptr);
    SluaState->setLoadFileDelegate(Delegate);
    SluaState->init();
}
// ★ 连 editor 模拟器也依赖宿主先注入 loader，进一步证明入口所有权在宿主
```

[2] 关键源码：这种“文件来源外置”模式不仅用于 Lua 脚本，也用于 protobuf schema

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/luaprotobuf/lpb.cpp
// 函数: Lio_read / lpb_loadfile / ProtobufUtil::loadFileDelegate
// 位置: lpb.cpp:541-549, 1178-1183, 1844
// ============================================================================
TArray<uint8> content;
if (ProtobufUtil::loadFileDelegate)
    content = ProtobufUtil::loadFileDelegate(UTF8_TO_TCHAR(fname));

if (content.Num() <= 0)
    return luaL_fileresult(L, 0, fname);
// ★ protobuf 侧也不自己打开磁盘文件，而是继续走宿主 delegate

TArray<uint8> content;
if (ProtobufUtil::loadFileDelegate)
    content = ProtobufUtil::loadFileDelegate(UTF8_TO_TCHAR(filename));

if (content.Num() <= 0)
    return luaL_fileresult(L, 0, filename);
// ★ 这不是单点设计，而是整个 runtime 的一贯“资源入口外置”策略

NS_SLUA::ProtobufUtil::LoadFileDelegate ProtobufUtil::loadFileDelegate = nullptr;
// ★ 默认就是空，说明插件 core 不假设具体资源管线
```

[3] 对照源码：Angelscript 由插件自己发现脚本根、监听变化并保留文件身份

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 函数: FAngelscriptEngine::DiscoverScriptRoots / MakeAllScriptRoots /
//       FAngelscriptEditorModule::StartupModule
// 位置: AngelscriptEngine.cpp:1326-1369, 4342-4345;
//       AngelscriptEditorModule.cpp:366-382
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
DiscoveredRootPaths.Insert(RootPath, 0);
// ★ 脚本根目录由插件自己发现并排序，不依赖宿主回调返回 bytes

DirectoryWatcher->RegisterDirectoryChangedCallback_Handle(
    *RootPath,
    IDirectoryWatcher::FDirectoryChanged::CreateStatic(&OnScriptFileChanges),
    WatchHandle,
    IDirectoryWatcher::IncludeDirectoryChanges);
// ★ editor 直接监听脚本目录，热重载闭环在插件侧成立

for (auto& Section : Module->Code)
{
    ScriptModule->AddScriptSection(TCHAR_TO_ANSI(*Section.AbsoluteFilename), TCHAR_TO_UTF8(*Section.Code), 0, 0);
}
// ★ 编译时保留绝对文件名，说明脚本主身份是“文件”，不是外部 delegate 返回的匿名 bytes
```

### 补充判断

| 观察点 | sluaunreal | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 脚本来源所有权 | `LoadFileDelegate` 由宿主注入，runtime 只消费 bytes（`LuaState.h:106-110`; `LuaState.cpp:153-155`） | 插件自己发现 `Script` 根并监听变更（`AngelscriptEngine.cpp:1326-1369`; `AngelscriptEditorModule.cpp:366-382`） | 实现方式不同 |
| 开发期热重载入口 | 可支持任意来源脚本，但入口必须由宿主先接好 delegate（`LuaSimulate.cpp:98-108`） | 直接以脚本目录和文件变化驱动 reload（`AngelscriptEditorModule.cpp:366-382`） | 实现方式不同 |
| 插件 core 内建包治理 | 当前执行链是 `delegate -> luaL_loadbuffer`，未见内建解密/验签步骤 | 当前核心也不是网络热更插件，但对脚本文件身份和 precompiled cache 更收口 | slua 在插件 core 上没有实现包治理；Angelscript 重心不同 |
| 非脚本资源入口 | protobuf 也复用外部 `loadFileDelegate`（`lpb.cpp:541-549,1178-1183,1844`） | 主要围绕脚本文件和预编译 cache 组织运行制品 | 实现方式不同 |

### [维度 D3] slua 的 Blueprint 调用桥是 `FLuaBPVar` 变体隧道，不是 `UFunction` 的精确脚本声明

前文已经覆盖过 slua 的 override/hook 路线；这一轮补的是它的另一条 Blueprint 互通通道。`LuaBlueprintLibrary.h:21-31,41-76` 定义的不是一组按 `UFunction` 自动投影出来的强类型节点，而是一个 `FLuaBPVar` 变体容器和几组 `CallToLua* / CreateVarFrom* / Get*FromVar` 工具函数。`LuaBlueprintLibrary.cpp:51-76,140-216` 进一步表明，Blueprint 调 Lua 时只传 `FunctionName + TArray<FLuaBPVar>`，每个参数在运行时 `push` 到 Lua 栈；返回值再被包装成 `LuaVar` tuple/table，随后用 `GetIntFromVar` / `GetObjectFromVar` 一类 helper 按 index 取回。

这条路径的设计目标不是“把 Blueprint API 变成脚本语言里的 first-class typed method”，而是“给 Blueprint 一条可以临时穿过去的动态隧道”。好处是桥非常薄、节点数量少、甚至能处理多返回值 tuple；代价是函数名和返回位次都退化成运行时约定。对照 Angelscript，`Helper_FunctionSignature.h:178-222` 会在绑定阶段把每个 `FProperty` 先变成 `FAngelscriptTypeUsage`，只要有一个参数类型不合法，整个绑定直接退出。两者的差异不是“能不能调 Blueprint”，而是**类型边界是在 bind 时封死，还是在运行时再解释**。

```
[D3] Blueprint Bridge Shape
sluaunreal
├─ [1] Blueprint node gives FunctionName + Args[]   // 入口是字符串 + 变体数组
├─ [2] FLuaBPVar.value.push(L)                      // 逐个压入 Lua 栈
├─ [3] LuaVar::callWithNArg()                       // 统一走 lua_pcallk
└─ [4] tuple/table -> Get*FromVar(Index)           // 返回值按位次再解释

Angelscript
├─ [1] Scan UFunction properties                    // 先枚举 FProperty
├─ [2] FAngelscriptTypeUsage::FromProperty          // 每个参数先变成精确脚本类型
├─ [3] Build declaration + defaults                 // 形成稳定 script declaration
└─ [4] Bind direct or reflective fallback           // 然后才开放给脚本调用
```

[1] 关键源码：slua 的 Blueprint 调用桥是“函数名字符串 + `FLuaBPVar` 变体数组”

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaBlueprintLibrary.h
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaBlueprintLibrary.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaVar.h
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaVar.cpp
// 函数: FLuaBPVar / ULuaBlueprintLibrary::CallToLuaWithArgs / CallToLua /
//       FLuaBPVar::checkValue / getValue / LuaVar::callWithNArg / LuaVar::docall
// 位置: LuaBlueprintLibrary.h:21-31,41-76;
//       LuaBlueprintLibrary.cpp:51-76,79-97,140-216;
//       LuaVar.h:287-292;
//       LuaVar.cpp:656-677
// ============================================================================
struct SLUA_UNREAL_API FLuaBPVar {
    GENERATED_USTRUCT_BODY()
public:
    NS_SLUA::LuaVar value;
    static void* checkValue(NS_SLUA::lua_State* L, NS_SLUA::FStructProperty* p, uint8* params, int i);
};
// ★ Blueprint 与 Lua 之间搬运的不是“某个精确 UFunction 签名”，而是一个通用变体壳

static FLuaBPVar CallToLuaWithArgs(UObject* WorldContextObject, FString FunctionName,const TArray<FLuaBPVar>& Args,FString StateName);
static int GetIntFromVar(FLuaBPVar Value,int Index=1);
static UObject* GetObjectFromVar(FLuaBPVar Value,int Index=1);
// ★ API 设计已经暴露出运行时协议：参数是变体数组，返回值靠 Index 解释

LuaVar f = ls->get(TCHAR_TO_UTF8(*funcname));
...
for (auto& arg : args) {
    arg.value.push(ls->getLuaState());
}
return f.callWithNArg(fillParam);
// ★ 调用时只按函数名取全局成员，再把每个 FLuaBPVar 压栈；没有 bind-time declaration

void* FLuaBPVar::checkValue(NS_SLUA::lua_State* L, NS_SLUA::FStructProperty* p, uint8* params, int i)
{
    FLuaBPVar ret;
    ret.value.set(L, i);
    p->CopyCompleteValue(params, &ret);
    return nullptr;
}
// ★ 返回值落回 Blueprint 时，仍然只是把当前栈项封成 LuaVar 放进 FLuaBPVar

bool getValue(const LuaVar& lv,int index,UObject*& value) {
    if(index==1 && lv.count()>=index && lv.isUserdata("UObject")) {
        value = lv.asUserdata<UObject>("UObject");
        return true;
    }
    else if(lv.isTable() || lv.isTuple()) {
        LuaVar v = lv.getAt(index);
        return getValue(v,1,value);
    }
    else
        return false;
}
// ★ Get*FromVar 的本质是“按 index 从 tuple/table 再解释一次”，而不是静态签名约束

inline LuaVar callWithNArg(const FillParamCallback& fillParam) {
    auto L = getState();
    int nret = docall(fillParam);
    auto ret = LuaVar::wrapReturn(L,nret);
    lua_pop(L,nret);
    return ret;
}

int LuaVar::docall(const FillParamCallback& fillParam) const
{
    vars[0].ref->push(L);
    int argn = fillParam ? fillParam() : 0;
    if (lua_pcallk(L, argn, LUA_MULTRET, errhandle, NULL, NULL))
        lua_pop(L, 1);
    return lua_gettop(L) - errhandle + 1;
}
// ★ 整个桥最终收敛成一次通用 lua_pcallk；参数/返回的类型语义都留到运行时解释
```

[2] 对照源码：Angelscript 先把 `UFunction` 压成精确签名，不合法类型直接拒绝绑定

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp
// 函数: FAngelscriptFunctionSignature::InitFromFunction / BindBlueprintCallable
// 位置: Helper_FunctionSignature.h:178-222;
//       Bind_BlueprintCallable.cpp:72-91, 100-151
// ============================================================================
for( TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It )
{
    FProperty* Property = *It;
    FAngelscriptTypeUsage Type = FAngelscriptTypeUsage::FromProperty(Property);

    if (!Type.IsValid())
    {
        bAllTypesValid = false;
        break;
    }
    ...
    ArgumentTypes.Add(Type);
    ArgumentNames.Add(Property->GetName());
}
// ★ 这里先把每个 FProperty 变成精确的脚本类型；只要有一个不合法，就不继续绑定

if (!bHasDirectNativePointer)
{
    if (!BindBlueprintCallableReflectiveFallback(InType, Function, Signature, *Entry))
        return;
    ...
    return;
}

int FunctionId = FAngelscriptBinds::BindMethodDirect
(
    InType->GetAngelscriptTypeName(),
    Signature.Declaration, ASFuncPtr, asCALL_THISCALL, Entry->Caller
);
Signature.ModifyScriptFunction(FunctionId);
// ★ 进入 direct bind / reflective fallback 之前，Signature.Declaration 已经是稳定声明
```

### 补充判断

| 观察点 | sluaunreal | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| Blueprint 调用入口 | `FunctionName + TArray<FLuaBPVar>`（`LuaBlueprintLibrary.h:41-76`; `LuaBlueprintLibrary.cpp:51-76`） | `UFunction -> Signature.Declaration -> bind`（`Helper_FunctionSignature.h:178-222`; `Bind_BlueprintCallable.cpp:100-151`） | 实现方式不同 |
| 参数类型边界 | `FLuaBPVar` 在运行时 `push/get`，返回值靠 index 解释（`LuaBlueprintLibrary.cpp:140-216`; `LuaVar.cpp:656-677`） | 参数类型在 bind 时先验证，不合法直接拒绝（`Helper_FunctionSignature.h:183-191`） | 实现质量差异：Angelscript 的类型边界更前置 |
| 多返回值表达 | `LuaVar` tuple/table 天然支持多返回值再拆（`LuaVar.h:287-292`） | 以明确函数签名和 out/ref 规则表达 | 实现方式不同 |
| Blueprint API 表面积 | 节点少，但函数名和位次是运行时约定 | 节点与脚本声明更多，但 API 语义可审计 | 实现质量差异：取舍不同，不是单纯优劣 |

### [维度 D8] slua 把性能优化拆成两层：`CppBinding` 压桥接开销，`Profiler` 负责观测；StaticJIT 压的是脚本 VM 调度

前文已经证明 slua 不是纯反射桥，但这轮把它的性能分层看得更清楚了。`LuaWrapper.cpp:55-67,184-188` 不是“生成一些 helper”那么简单，而是按 UE minor 版本直接编进不同的 `LuaWrapper*.inc` 快照；每个 wrapper 里的热点方法都长成同一种形状：`CheckSelf -> checkValue -> native C++ call -> LuaObject::push`。例如 `LuaWrapper5.4.inc:3752-3898` 的 `FRandomStream` 包装几乎没有额外反射逻辑，这说明 slua 想优化的是**Lua 到 UE native 的桥接边界**。

但 slua 又没有把“性能”只做成 wrapper。`LuaProfiler.inl:15-50,172-184` 和 `LuaProfiler.cpp:48-61,212-252,441-483` 证明 profiler 是另一条正交链路：控制平面是一段加载到每个 `LuaState` 里的 Lua 脚本，hook 和 sample 采集是 native，输出既可以走 socket，也可以落到本地 `.sluastat`。这说明 slua 的性能策略是“桥接开销和可观测性一起做，但分层很清楚”。

对照 Angelscript，`StaticJITBinds.cpp:401-443` 先在生成预编译数据时为 bind 记录 `native form`；`AngelscriptBytecodes.cpp:109-160` 再在每个脚本 call site 上根据 `NativeForm / ArgumentTypes / call convention` 选择 `CustomCall / NativeCall / GenericCall`；`StaticJITHeader.cpp:169-226` 最终负责按系统函数调用约定执行 native。这和 slua 的静态 wrapper 不在一个层面上竞争。slua 主要压“跨语言边界一次调用要付出的成本”，StaticJIT 主要压“脚本 VM 如何把一次调用 lower 成最便宜的执行路径”。

```
[D8] Optimization Target
sluaunreal
├─ [1] LuaWrapper*.inc per UE version               // 生成静态 wrapper 快照
├─ [2] static int Wrapper(lua_State* L)             // CheckSelf + checkValue + native call
├─ [3] LuaObject::addMethod(...)                    // 绑定到 Lua type table
└─ [4] ProfilerScript + native hook                 // 观测链路与调用链路并行

Angelscript
├─ [1] Bind phase records native form               // SCRIPT_NATIVE_* 记录 native form
├─ [2] StaticJIT MakeCall()                         // 每个 call site 选择 native/custom/generic
├─ [3] ScriptCallNative()                           // 依据 callConv 执行 native
└─ [4] Precompiled data gates reuse                 // 优化脚本 VM 调度而不是仅压桥接层
```

[1] 关键源码：slua 的 `CppBinding` 热路径是“每个方法一个 `lua_CFunction`”

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaWrapper.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaWrapper5.4.inc
// 函数: LuaWrapper::initExt / FRandomStreamWrapper::GetUnsignedInt /
//       FRandomStreamWrapper::RandRange / FRandomStreamWrapper::bind
// 位置: LuaWrapper.cpp:55-67,184-188;
//       LuaWrapper5.4.inc:3752-3815,3888-3898
// ============================================================================
#if ((ENGINE_MINOR_VERSION==4) && (ENGINE_MAJOR_VERSION==5))
    #include "LuaWrapper5.4.inc"
#endif
// ★ 生成物按 UE minor 版本分叉，运行时少分支，维护侧多快照

void LuaWrapper::initExt(lua_State* L)
{
    init(L);
    FSoftObjectPtrWrapper::bind(L);
}
// ★ 插件启动时直接把生成 wrapper 装进 Lua 运行时

static int GetUnsignedInt(lua_State* L) {
    CheckSelf(FRandomStream);
    auto ret = self->GetUnsignedInt();
    LuaObject::push(L, ret);
    return 1;
}
// ★ 最热路径只有 self 检查、native 调用和返回值压栈，没有额外反射遍历

static int RandRange(lua_State* L) {
    CheckSelf(FRandomStream);
    auto Min = LuaObject::checkValue<int>(L, 2);
    auto Max = LuaObject::checkValue<int>(L, 3);
    auto ret = self->RandRange(Min, Max);
    LuaObject::push(L, ret);
    return 1;
}
// ★ 参数解析也在 wrapper 内展开成固定代码，桥接成本基本与方法体线性相关

static void bind(lua_State* L) {
    LuaObject::newType(L, "FRandomStream");
    LuaObject::addMethod(L, "GetUnsignedInt", GetUnsignedInt, true);
    LuaObject::addMethod(L, "RandRange", RandRange, true);
}
// ★ 绑定结果最终是一张 Lua method table，而不是运行时继续找 UFunction 元数据
```

[2] 关键源码：slua 的 profiler 是“Lua 控制脚本 + native hook + socket / local record”三段式

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaProfiler.inl
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaProfiler.cpp
// 函数: ProfilerScript / LuaProfiler::init / takeSample / takeMemorySample / tick
// 位置: LuaProfiler.inl:15-50,172-184;
//       LuaProfiler.cpp:48-61,212-252,441-483
// ============================================================================
const char* ProfilerScript = R"code(
local this = {}
...
function this.start(host, port)
    host = tostring(host or "127.0.0.1")
    port = tonumber(port) or 8081
    this.printToConsole("Profile start. connect host:" .. host .. " port:".. tostring(port), 1)
end

function this.startLocalRecord()
    print("[Slua Profile] startLocalRecord")
    this.onChangeRecordState(true)
    this.changeHookState(HookState.HOOK)
end

function this.stopLocalRecord()
    print("[Slua Profile] stopLocalRecord")
    this.changeHookState(HookState.UNHOOK)
    this.onChangeRecordState(false)
end
)code";
// ★ 控制平面本身是 Lua 脚本，说明 profiler 不是外部单独程序，而是嵌在 runtime 里的 sidecar

#ifdef ENABLE_PROFILER
const char* LuaProfiler::ChunkName = "[ProfilerScript]";
// ★ profiler 通过 compile define 常驻进 runtime 构建

void takeSample(int event,int line,const char* funcname,const char* shortsrc, int64 startTime, lua_State* L) {
    if (!SluaProfilerDataManager::IsRecording())
    {
        makeProfilePackage(s_messageWriter, event, startTime - profileTotalCost, line, funcname, shortsrc);
        sendMessage(s_messageWriter, L);
    }
    else
    {
        SluaProfilerDataManager::ReceiveProfileData(event, startTime - profileTotalCost, line, funcname, shortsrc);
    }
}
// ★ 同一套 hook 既能远端流式发样本，也能本地录制；观测链路与桥接链路解耦

void LuaProfiler::init(LuaState* LS)
{
    auto& profiler = selfProfiler.Add(LS);
    profiler = LS->doBuffer((const uint8*)ProfilerScript,strlen(ProfilerScript), ChunkName);
    ...
    lua_pushcfunction(L, setHook);
    lua_setfield(L, -2, "setHook");
    ...
    lua_setglobal(L, "slua_profile");
}
// ★ 每个 LuaState 都会加载一份 profiler 控制脚本，再把 native hook 注入给它

void LuaProfiler::tick(LuaState *LS)
{
    QUICK_SCOPE_CYCLE_COUNTER(LuaProfiler_Tick)
    ...
}
// ★ profiler 自己也接受 UE 的 cycle counter 观测，说明它被当作正式运行时组件维护
```

[3] 对照源码：Angelscript StaticJIT 记录的是 `native form`，优化点在脚本调用调度

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp
// 函数: FScriptFunctionNativeForm::BindNativeFunction /
//       FNativeFunctionCall::MakeCall / FStaticJITFunction::ScriptCallNative
// 位置: StaticJITBinds.cpp:401-443;
//       AngelscriptBytecodes.cpp:109-160;
//       StaticJITHeader.cpp:169-226
// ============================================================================
void FScriptFunctionNativeForm::BindNativeFunction(const ANSICHAR* Name, bool bTrivial)
{
    if (!FAngelscriptEngine::bGeneratePrecompiledData)
        return;
    GScriptNativeForms.Add(FAngelscriptBinds::GetPreviousBind(), new FScriptNativeFunction(Name, bTrivial));
}
// ★ bind 阶段先把“这个脚本声明对应哪种 native form”记进预编译数据侧表

void MakeCall(FStaticJITContext& Context)
{
    ...
    FindNativeForm();
    AnalyzeArgumentTypes();
    ...
    bool bCanMakeNativeCall = bHaveNativeFunction && bAllTypesHaveNatives && NativeForm->CanCallNative(NativeContext);
    bool bCanMakeCustomCall = NativeForm != nullptr && NativeForm->CanCallCustom(NativeContext);
    ...
    if (bCanMakeCustomCall)
    {
        MakeCustomCall(Context, NativeContext);
    }
    else if (bCanMakeNativeCall)
    {
        MakeNativeCall(Context, NativeContext);
    }
}
// ★ 真正优化发生在每个脚本 call site：根据 native form 和参数类型决定最便宜的 lowering 路径

void FStaticJITFunction::ScriptCallNative(FScriptExecution& Execution, asCScriptFunction* Function, asBYTE* l_sp, asQWORD* valueRegister, void** objectRegister)
{
    ...
    if (callConv == ICC_GENERIC_FUNC || callConv == ICC_GENERIC_METHOD)
    {
        ...
        func(&gen);
        *valueRegister = gen.returnVal;
        *objectRegister = (void*)gen.objectRegister;
        return;
    }

    if (sysFunc->caller.IsBound())
    {
        ...
        if( sysFunc->callConv >= ICC_THISCALL )
        {
            FunctionArgs[ArgIndex] = *(void**)&StackArgs[0];
            ...
        }
    }
}
// ★ 最终执行点关注的是调用约定、object pointer、参数布局，而不是再去做 UFunction 反射拆装
```

### 补充判断

| 观察点 | sluaunreal | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 主要优化目标 | 静态 wrapper 压 Lua↔UE 桥接成本（`LuaWrapper.cpp:55-67,184-188`; `LuaWrapper5.4.inc:3752-3898`） | StaticJIT 压脚本 VM 调用调度与 lowering 成本（`StaticJITBinds.cpp:401-443`; `AngelscriptBytecodes.cpp:109-160`） | 实现方式不同 |
| 生成物粒度 | 按 UE minor 版本维护 `LuaWrapper*.inc` 快照 | 按 bind/function 记录 `native form`，与预编译数据联动 | 实现方式不同 |
| Profiler 集成 | `ProfilerScript` + native hook + remote/local record（`LuaProfiler.inl:15-50,172-184`; `LuaProfiler.cpp:212-252,441-483`） | 当前核心强项不在同类 profiler，而在 DebugServer / CodeCoverage / StaticJIT | 同类 profiler 形态当前没有实现 |
| 维护成本结构 | 生成 wrapper 代码量更大，但单次调用更直白 | 绑定与 JIT 元数据体系更复杂，但优化覆盖到脚本执行层 | 实现质量差异：优化层级不同，不能直接横向判高低 |

---

## 深化分析 (2026-04-09 01:02:22)

### [维度 D10] slua 的文档入口其实是一份“README + live demo 剧本”；Angelscript 把文档、示例与验证拆成三层契约

这一轮补看的不是运行时能力，而是“用户第一次接触仓库时，会被什么材料带进系统”。`sluaunreal` 当前的上手入口高度收敛在 `Reference/sluaunreal/README.md:30-85,147-318,365-392`：同一个 README 同时列功能清单、调试器入口、蓝图/Lua 用法、override/RPC/复制示例、性能表、wiki 和完整 demo 链接。它不是单纯 marketing 页面，而更像一份**把用户直接引到 demo 工程去运行**的操作脚本。

源码也支持这个判断。`Reference/sluaunreal/Source/democpp/MyGameInstance.cpp:36-64` 在 `GameInstance` 内直接创建 `LuaState` 并把 loader 固定到 `ProjectContentDir()/Lua`；`Reference/sluaunreal/Source/democpp/SluaTestActor.cpp:18-28` 在 `BeginPlay()` 里执行 `doFile("Test")` 并调用 `begin`；`Reference/sluaunreal/Source/democpp/SluaTestCase.h:87-215` 与 `SluaTestCase.cpp:337-465` 则把 `BlueprintCallable`、delegate、`FLuaBPVar` callback、extension method、性能测试函数都塞进一个 demo 类型里。也就是说，README 里的 `SluaTestCase.StaticFunc()`、Blueprint callback、override 叙事并不是对 API 目录的系统梳理，而是围绕 demo 类型组织的“活样例”。

Angelscript 走的是另一条路。`Documents/Guides/Build.md:3-80` 和 `Documents/Guides/Test.md:3-120` 先把 build/test 入口标准化；`Script/Examples/README.md:3-21` 明确示例目录仍处于 staged 状态，并要求每个新增示例关联自动化验证；`Documents/Guides/TestCatalog.md:955-986` 再把示例主题和 coverage 资产变成可追踪目录；最后 `Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleActorTest.cpp:9-95`、`AngelscriptScriptExampleTestSupport.cpp:16-59` 与 `AngelscriptScriptExampleCoverageTests.cpp:25-61` 把“示例能编译、磁盘资产存在、coverage 样例可加载”变成正式测试契约。两者差异不在“有没有文档”，而在**文档是否被纳入产品化验证闭环**。

还有一个很具体的信号：`sluaunreal` README 的性能段落明确说测试用例可参考 `TestPerf.lua`（`Reference/sluaunreal/README.md:365-373`），但当前 `Reference/sluaunreal/` 快照内未检索到同名文件。这说明它的文档组织对仓库外材料或历史产物有依赖。Angelscript 在同类问题上的做法更保守，`Script/Examples/README.md:7-14` 反而先承认示例仍在伴侣目录，并要求示例与自动化验证保持单一真实来源。

```
[D10] Onboarding Contract Flow
sluaunreal
├─ README.md                                       // 单文件承接功能、教程、性能、外链
│  ├─ feature list                                 // 功能总览
│  ├─ debugger link -> luapanda                    // 调试器跳到外部仓库
│  ├─ inline Lua snippets                          // 直接展示 override / RPC / BP 互通
│  ├─ performance table                            // 文档里直接给 benchmark 结论
│  └─ wiki + full demo links                       // 深入资料继续外链
└─ Source/democpp                                  // 仓库内真实接入载体
   ├─ MyGameInstance boots LuaState                // 启动 loader
   ├─ SluaTestActor runs doFile("Test")            // 驱动示例脚本
   └─ SluaTestCase exposes BP/demo APIs            // README 示例依附的演示类型

Angelscript
├─ Documents/Guides                                // build/test/目录规则
├─ Script/                                         // 示例脚本文本
│  ├─ Example_Actor.as                             // 对外最小样例
│  └─ Examples/README.md                           // 示例 staging 约束
└─ AngelscriptTest/Examples                        // 自动化契约
   ├─ RunScriptExampleCompileTest                  // 内存示例编译
   └─ CoverageTests load real .as assets           // 磁盘示例资产验证
```

[1] 关键源码：slua 把“功能说明 + 用法教程 + 性能说服 + 深入资料”聚合进一个 README

```md
<!-- ============================================================================
文件: Reference/sluaunreal/README.md
位置: 30-85, 147-318, 365-392
位置说明: README 同时承担功能清单、调试器入口、内联用法、性能表与外链导航
============================================================================ -->
## Features
* Automatic export of blueprint API to the Lua interface
* Supporting RPC (Remote Procedure Call) functions
* Overriding any blueprint function with a Lua function
* CPU profiling
* Multithread Lua GC (Garbage Collection)

# 调试器支持
我们开发了专门的vs code调试插件...
[调试器支持](https://github.com/Tencent/luapanda)

## Usage at a glance
local SluaTestCase=import('SluaTestCase');
SluaTestCase.StaticFunc()

-- override event from blueprint
function LuaActor:ReceiveBeginPlay()
    ...
end

LuaActor.ServerRPC.TestServerRPC = {
    Reliable = true,
    Params = { EPropertyClass.Int, EPropertyClass.Str, EPropertyClass.bool, }
}

### Performance
Test on MacOSX, Unreal 4.18 develop building, CPU i7 4GHz,
test cases can be found in TestPerf.lua

[使用帮助(Document in Chinese)](https://github.com/Tencent/sluaunreal/wiki)
[更完整的demo](https://github.com/IriskaDev/slua_unreal_demo)
<!-- ★ 同一入口既负责讲“有什么能力”，也负责告诉用户“去哪里继续学”和“去哪里拿完整 demo” -->
```

[2] 关键源码：README 里的教程对象，实际绑定在 `democpp` 的启动路径与演示类型上

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Source/democpp/MyGameInstance.cpp
// 文件: Reference/sluaunreal/Source/democpp/SluaTestActor.cpp
// 文件: Reference/sluaunreal/Source/democpp/SluaTestCase.h
// 文件: Reference/sluaunreal/Source/democpp/SluaTestCase.cpp
// 函数: UMyGameInstance::CreateLuaState / ASluaTestActor::BeginPlay /
//       USluaTestCase declarations / USluaTestCase::USluaTestCase / callback
// 位置: MyGameInstance.cpp:36-64; SluaTestActor.cpp:18-28;
//       SluaTestCase.h:87-215; SluaTestCase.cpp:337-465
// ============================================================================
void UMyGameInstance::CreateLuaState()
{
    state = new NS_SLUA::LuaState("SLuaMainState", this);
    state->setLoadFileDelegate([](const char* fn, FString& filepath)->TArray<uint8> {
        FString path = FPaths::ProjectContentDir();
        path /= "Lua";
        path /= filename.Replace(TEXT("."), TEXT("/"));
        ...
        FFileHelper::LoadFileToArray(Content, *fullPath);
        ...
    });
    state->init();
}
// ★ demo 工程在 GameInstance 启动时就固定脚本目录与 loader，说明仓库默认上手路径就是“跑 demo”

void ASluaTestActor::BeginPlay()
{
    NS_SLUA::LuaState* ls = NS_SLUA::LuaState::get(GetGameInstance());
    ls->set("some.field.x", 101);
    ls->doFile("Test");
    ls->call("begin",this->GetWorld(),this);
}
// ★ 示例 Actor 直接执行名为 Test 的 Lua 模块，把教程入口收敛成运行时演示

UCLASS()
class USluaTestCase : public UObject {
public:
    UFUNCTION(BlueprintCallable, Category="Lua|TestCase")
    static void StaticFunc();

    UFUNCTION(BlueprintCallable, Category = "Lua|TestCase")
    void TestUnicastDelegate(FString str);

    UFUNCTION()
    void TestLuaCallback(FLuaBPVar callback) {
        if (callback.value.isFunction())
            callback.value.call();
    }
};
// ★ README 里的 StaticFunc、Blueprint callback、delegate 叙事都依附在这个 demo 类型上

USluaTestCase::USluaTestCase(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    REG_EXTENSION_METHOD(USluaTestCase,"SetArrayStrEx",&USluaTestCase::SetArrayStrEx);
    REG_EXTENSION_METHOD(USluaTestCase, "constRetFunc", &USluaTestCase::constRetFunc);
    REG_EXTENSION_METHOD(USluaTestCase, "inlineFunc", &USluaTestCase::inlineFunc);
}

void USluaTestCase::callback() {
    s_onloaded.ExecuteIfBound(1024);
}
// ★ demo 类型同时承担扩展方法样例、delegate 样例和回调样例，不是纯净的 API catalog
```

[3] 对照源码：Angelscript 把“操作说明”“示例资产”“验证入口”拆成三层，并明确避免分叉

```md
<!-- ============================================================================
文件: Documents/Guides/Build.md
文件: Documents/Guides/Test.md
文件: Documents/Guides/TestCatalog.md
文件: Script/Examples/README.md
文件: Script/Example_Actor.as
位置: Build.md:3-80; Test.md:3-120; TestCatalog.md:955-986;
//      Script/Examples/README.md:3-21; Script/Example_Actor.as:7-74
位置说明: 文档与示例的职责分层，以及示例必须绑定验证入口的仓库约束
============================================================================ -->
# Build 指南
- 本仓库的标准构建入口只有 `Tools\RunBuild.ps1`

# Test 指南
- 本仓库的标准自动化测试入口是 `Tools\RunTests.ps1`
- `AS_FunctionTable_Entries.csv`：逐条函数明细，适合按过滤查询

# Script 示例目录
- 当前波次以 `Documents/Plans/Plan_ScriptExamplesExpansion/Coverage/` 为真实交付源，避免与测试内联字符串再次分叉。
- 每个新增示例至少要关联一个自动化验证入口或在对应 Plan 中登记验证策略。

## 14. Examples — 示例脚本编译
> 所有测试通过 `RunScriptExampleCompileTest` 将内嵌示例脚本编译为注解模块，
> 验证文档级示例脚本能完整通过编译。

class AExampleActorType : AActor
{
    UPROPERTY()
    int ExampleValue = 15;

    UFUNCTION(BlueprintOverride)
    void BeginPlay()
    {
        ScriptOnlyMethod();
        NewOverridableMethod();
    }
}
<!-- ★ Angelscript 不把“最小样例”只放在 README；它把样例文本、操作指南和验证说明拆成可维护资产 -->
```

[4] 对照源码：Angelscript 的示例不是纯展示文案，而是正式 automation contract

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleActorTest.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleTestSupport.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleCoverageTests.cpp
// 函数: FAngelscriptScriptExampleActorTest::RunTest /
//       AngelscriptScriptExamples::RunScriptExampleCompileTest /
//       CompileCoverageExample
// 位置: AngelscriptScriptExampleActorTest.cpp:9-95;
//       AngelscriptScriptExampleTestSupport.cpp:16-59;
//       AngelscriptScriptExampleCoverageTests.cpp:25-61
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAngelscriptScriptExampleActorTest,
    "Angelscript.TestModule.ScriptExamples.Actor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptScriptExampleActorTest::RunTest(const FString& Parameters)
{
    return AngelscriptScriptExamples::RunScriptExampleCompileTest(*this, GActorExample);
}
// ★ 示例主题有独立 automation test 名称，已经进入正式回归目录

bool RunScriptExampleCompileTest(FAutomationTestBase& Test, const FScriptExampleSource& Example)
{
    ...
    const FString VirtualFileName = FString::Printf(TEXT("ScriptExamples/%s"), *ExampleFileName);
    const bool bCompiled = CompileAnnotatedModuleFromMemory(&Engine, ModuleName, VirtualFileName, CombinedScriptCode);
    Test.TestTrue(*FString::Printf(TEXT("Compile example '%s' succeeds"), *ExampleFileName), bCompiled);
    return bCompiled;
}
// ★ 文档级示例先被编译成模块，再谈“可教学”；示例不是只靠人工阅读判断正确性

UClass* CompileCoverageExample(
    FAutomationTestBase& Test,
    FAngelscriptEngine& Engine,
    FName ModuleName,
    const TCHAR* RelativePath,
    FName GeneratedClassName)
{
    if (!ExpectCoverageExampleExists(Test, RelativePath))
    {
        return nullptr;
    }
    ...
    return CompileScriptModule(Test, Engine, ModuleName, RelativePath, ScriptSource, GeneratedClassName);
}
// ★ 真正落盘的 `.as` 资产也要先验证“文件存在 + 可加载 + 可编译”，避免文档资产与测试资产漂移
```

### 设计取舍

- slua 的 README-first 方式降低了首次上手门槛。用户几乎不用翻目录，就能同时看到功能、用法、性能和外链；代价是文档职责过于集中，仓库内材料和仓库外材料容易混用。
- slua 的 demo-first 组织让示例天然带着真实运行时上下文，例如 loader、`LuaState`、delegate、Blueprint callback；代价是示例更像“演示工程的一部分”，而不是可单独审计、可回归验证的知识资产。
- Angelscript 的 guide/example/test 三层拆分让维护成本更高，但它换来的是文档资产可验证、示例状态可声明、操作入口可标准化，尤其适合插件交付和长期维护。

### 与 Angelscript 对比

| 观察点 | sluaunreal | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 首次上手入口 | README 单点承接功能、调试器、用法、性能与外链（`Reference/sluaunreal/README.md:30-85,147-318,365-392`） | `Build.md` / `Test.md` / `TestCatalog.md` / `Script/Examples/README.md` 分层组织（`Documents/Guides/Build.md:3-80`; `Documents/Guides/Test.md:3-120`; `Documents/Guides/TestCatalog.md:955-986`; `Script/Examples/README.md:3-21`） | 实现方式不同 |
| 示例挂靠位置 | README 内联 Lua 片段 + `democpp` 运行时样例（`MyGameInstance.cpp:36-64`; `SluaTestActor.cpp:18-28`; `SluaTestCase.h:87-215`） | 独立 `.as` 示例 + automation harness（`Script/Example_Actor.as:7-74`; `AngelscriptScriptExampleActorTest.cpp:9-95`; `AngelscriptScriptExampleTestSupport.cpp:16-59`） | 实现方式不同 |
| 示例与验证耦合度 | demo 类型可运行，但仓库内未见对 README 片段的等价自动化回归 | 示例主题和 coverage 资产都进入测试目录并做存在性/编译验证（`TestCatalog.md:955-986`; `AngelscriptScriptExampleCoverageTests.cpp:25-61`） | 实现质量差异：Angelscript 更产品化 |
| 外部资料依赖 | 调试器、wiki、完整 demo 继续外链；README 还引用当前快照未见的 `TestPerf.lua`（`Reference/sluaunreal/README.md:74-81,365-392`） | 主要 build/test/example 入口在仓库内声明；对 staged 示例还显式写出伴侣目录与防分叉规则（`Script/Examples/README.md:7-14`） | 实现质量差异：slua 的仓库内闭环更弱，但不是“没有文档” |

---

## 深化分析 (2026-04-09 06:36:12)

### [维度 D6] slua 的静态导出可重复性依赖“手工维护的 clang 场景快照”，而不是 build graph 的一部分

这一轮补看 `Reference/sluaunreal/Tools/README.md:9-19,35-37,48-60`、`Reference/sluaunreal/Tools/config.json:1-9,65-77` 和 `Reference/sluaunreal/Plugins/lua_wrapper/Source/lua_wrapper/Private/lua_wrapper.cpp:122-135` 后，可以把 slua 的静态导出再说得更准确一些：它不是“编译时自动生成 wrapper”，而是维护者先在 `config.json` 中手工维护导出白名单、过滤表、引擎路径、`vcxproj` 路径以及整份 `include_path` / `preprocess` 字符串，再由 Editor 按钮启动 `lua-wrapper.exe` 去离线解析。也就是说，slua 的 source of truth 是一份脱离当前 UBT/UHT 会话的工具配置快照。

这和 Angelscript 的 `UHT exporter` 思路是两套完全不同的生成边界。`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:21-47` 把导出器注册成 `CompileOutput` 阶段的一部分；`AngelscriptFunctionTableCodeGenerator.cs:449-515` 直接遍历 `factory.Session.Modules` 里的 `UhtClass` / `UhtFunction`，并通过 `factory.AddExternalDependency()` 把真实 header 纳入依赖图；生成完成后还会输出 `AS_FunctionTable_Summary.json`、CSV，并回收旧 shard（`AngelscriptFunctionTableCodeGenerator.cs:166-215,440-445`）。所以 Angelscript 的生成物首先是 build artifact，其次才是开发者可读的代码。

```
[D6] Codegen Source-of-Truth
sluaunreal
├─ Tools/README.md                                // 工具规则写在文档里
├─ Tools/config.json
│  ├─ export_files / Customs                      // 手工指定导出目标
│  ├─ filter / filter_class                       // 手工排除不支持成员
│  ├─ ue4_dir / ue_vcproj                         // 固定工程路径
│  └─ include_path / preprocess                   // 固定 clang 语境快照
└─ lua_wrapper button -> lua-wrapper.exe          // Editor 手工触发，源码边界内未见正式 summary

Angelscript
├─ UhtExporter(CompileOutput)                     // 生成器注册进 UHT
├─ factory.Session.Modules                        // 直接使用本轮 UHT 类型树
├─ AddExternalDependency(header)                  // 真实头文件依赖
├─ AS_FunctionTable_*.cpp                         // 编译输出 shard
└─ Summary.json + Entries.csv + stale cleanup     // 可审计产物
```

[1] 关键源码：slua 的工具文档把 `lua-wrapper` 定义成第三层补充导出，并把配置维护责任交给使用者

```md
<!-- ============================================================================
文件: Reference/sluaunreal/Tools/README.md
位置: 9-19, 35-37, 48-60
位置说明: 工具定位、适用边界和配置要求都明确写死在 README
============================================================================ -->
lua-wrapper 是作为 slua-unreal 中 lua 导出接口的补充，slua-unreal 支持 3 种接口导出的方式：
1. 反射
2. LuaCppbinding
3. lua-wrapper

所以，lua-wrapper 的作用范围是：
1. 不支持导出自定义类型
2. 不支持导出可反射的类型
3. 导出类型限定于引擎中的 USTRUCT 类型

如果需要导出更多的类型... 请修改 Tools 目录下的 config*.json 文件，找到 "Customs" 字段...

配置文件主要配置两部分信息：
1. 编译参数相关信息：引擎和项目路径、预处理器
2. 指定导出类型信息
<!-- ★ 工具说明已经说明：生成正确与否，先取决于人工维护的 config 是否还跟得上当前工程 -->
```

[2] 关键源码：`config.json` 同时承担“导出白名单”和“clang 语义环境快照”

```json
// ============================================================================
// 文件: Reference/sluaunreal/Tools/config.json
// 位置: 1-9, 65-77
// 位置说明: 生成输入和编译语境都由 JSON 手工维护
// ============================================================================
"export_files": {
  "TBaseStructure": [
    "{ue4_dir}/Engine/Source/Runtime/CoreUObject/Private/UObject/Class.cpp",
    "{ue4_dir}/Engine/Source/Runtime/CoreUObject/Public/UObject/Class.h",
    "{ue4_dir}/Engine/Source/Runtime/CoreUObject/Public/UObject/SoftObjectPtr.h"
  ],
  "Customs": {}
}, // ★ 导出对象先靠 JSON 白名单声明，而不是从当前 build 自动发现

"output_dir": "{solution_dir}/Plugins/slua_unreal/Source/slua_unreal/Private/",
"win": {
  "solution_dir": "../",
  "ue4_dir": "C:/Program Files/Epic Games/UE_5.2",
  "ue_vcproj": "{solution_dir}/Intermediate/ProjectFiles/UE5.vcxproj",
  "include_path": "{ue4_dir}/Engine/Source/.../ContentBrowser;...",
  "preprocess": "TRACELOG_API=;IS_PROGRAM=0;UE_EDITOR=1;...;WITH_DEV_AUTOMATION_TESTS=1;..."
}
// ★ parser 需要一整份手工快照化的 include_path / preprocess 才能重建语义环境
```

[3] 关键源码：slua 的 Editor 侧只是启动器，源码边界内没有看到对等 summary / stale cleanup 入口

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/lua_wrapper/Source/lua_wrapper/Private/lua_wrapperCommands.cpp
// 文件: Reference/sluaunreal/Plugins/lua_wrapper/Source/lua_wrapper/Private/lua_wrapper.cpp
// 函数: Flua_wrapperCommands::RegisterCommands / Flua_wrapperModule::PluginButtonClicked
// 位置: lua_wrapperCommands.cpp:7-10; lua_wrapper.cpp:122-135
// ============================================================================
UI_COMMAND(OpenPluginWindow, "LuaWrapper", "Generate Lua Interface (Windows only)", EUserInterfaceActionType::Button, FInputGesture());
// ★ 暴露给编辑器的语义就是“点一下生成”，不是“参与编译图”

auto contentDir = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*FPaths::ProjectContentDir());
auto cmd = contentDir + TEXT("/../Tools/lua-wrapper.exe");
system(TCHAR_TO_UTF8(*cmd));
// ★ Windows 下直接起外部 exe；当前源码边界里看不到 summary、CSV 或 stale file 清理回传
```

[4] 对照源码：Angelscript 的生成器直接吃 UHT 会话，并把结果做成正式可审计产物

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs
// 函数: Export / CollectEntries / WriteGenerationSummary / stale cleanup
// 位置: AngelscriptFunctionTableExporter.cs:21-47;
//       AngelscriptFunctionTableCodeGenerator.cs:166-215, 440-487
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
        CountBlueprintCallableFunctions(module.ShortName, module.ScriptPackage, skippedEntries, ref classCount, ref functionCount, ref reconstructedCount, ref skippedCount);
    }
}
// ★ 生成入口直接挂在 UHT 里，输入就是“这一轮真实解析到的模块与函数”

factory.AddExternalDependency(classObj.HeaderFile.FilePath);
string includePath = factory.GetModuleShortestIncludePath(classObj.HeaderFile.Module, classObj.HeaderFile.FilePath);
includes.Add(includePath.Replace('\\', '/'));
// ★ header 依赖由当前会话提供，不需要人工抄一整份 include_path

string summaryPath = factory.MakePath("AS_FunctionTable_Summary", ".json");
File.WriteAllText(summaryPath, summaryJson, Encoding.UTF8);
// ★ 每次生成都输出 summary，后续测试和审计可以直接消费

foreach (string existingFile in Directory.EnumerateFiles(outputDirectory, "AS_FunctionTable_*.cpp"))
{
    if (!generatedPaths.Contains(existingFile))
    {
        File.Delete(existingFile);
    }
}
// ★ 旧 shard 会被回收，避免产物漂移
```

### 设计取舍

- slua 的好处是工具链独立，宿主工程可以在不改 UHT 的前提下扩充一批 `USTRUCT` wrapper；代价是生成可重复性高度依赖 `config.json` 是否仍然和当前引擎、当前项目、当前宏环境一致。
- Angelscript 的好处是生成链天然跟随本次 UHT 会话，依赖、产物和统计都在一个闭环里；代价是工具更深地耦合到了引擎构建流程，外部独立运行难度更高。
- 因此这里不能简单写成“slua 有 codegen，Angelscript 也有 codegen”。两边的关键差异是：slua 把 codegen 当外部补充工具，Angelscript 把 codegen 当正式 build stage。

### 与 Angelscript 对比

| 观察点 | sluaunreal | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 生成规则来源 | `config*.json` 的 `export_files / Customs / filter`（`Reference/sluaunreal/Tools/config.json:1-9,11-64`） | 当前 `UHT` 会话里的 `UhtClass / UhtFunction`（`AngelscriptFunctionTableExporter.cs:21-47`; `AngelscriptFunctionTableCodeGenerator.cs:449-515`） | 实现方式不同 |
| 编译语境来源 | 手工维护 `ue4_dir / ue_vcproj / include_path / preprocess`（`Reference/sluaunreal/Tools/README.md:48-60`; `Reference/sluaunreal/Tools/config.json:65-77`） | 直接消费 `factory.Session.Modules` 和 header 依赖（`AngelscriptFunctionTableExporter.cs:35-47`; `AngelscriptFunctionTableCodeGenerator.cs:457-463`） | 实现质量差异：Angelscript 更可重复 |
| 触发方式 | Editor 按钮启动外部 `lua-wrapper.exe`（`lua_wrapperCommands.cpp:7-10`; `lua_wrapper.cpp:122-135`） | `UhtExporter(... CompileOutput ...)` 自动参与编译输出（`AngelscriptFunctionTableExporter.cs:21-27`） | 实现方式不同 |
| 可审计产物 | 当前源码/文档边界只看到生成器启动入口，未见对等 `summary/csv/stale cleanup` 源码 | `AS_FunctionTable_Summary.json`、`AS_FunctionTable_Entries.csv`、旧 shard 清理（`AngelscriptFunctionTableCodeGenerator.cs:166-215,244-265,440-445`） | slua 在当前源码边界没有实现对等可审计产物 |

### [维度 D9 / D10] 纠正上一轮一处判断：`TestPerf.lua` 实际存在，但这反而证明 slua 把“文档、样例、验证”压在同一条 demo script 链上

这里补一条纠偏。上一轮把 README 对 `TestPerf.lua` 的引用写成“当前快照未见”，但这轮复查 `Reference/sluaunreal/README.md:365-373` 和 `Reference/sluaunreal/Content/Lua/TestPerf.lua:3-55` 后可以确认：文件确实在仓库里，且就是 README 性能表对应的脚本入口。这个纠偏不会推翻前面的主结论，反而把 slua 的验证形态说得更清楚了：它把文档中的用法、性能说服和功能验证，统一托管在 demo 工程能直接 `require` / `doFile` 的 Lua 脚本里。

源码调用链很直接。`Reference/sluaunreal/Source/democpp/MyGameInstance.cpp:36-64` 在 `GameInstance` 创建 `LuaState`，并把 loader 固定到 `ProjectContentDir()/Lua`；`Reference/sluaunreal/Source/democpp/SluaTestActor.cpp:18-41` 在 `BeginPlay()` 中执行 `doFile("Test")`，然后在 `Tick()` 中反复调 `update()`；`Reference/sluaunreal/Content/Lua/Test.lua:47-60` 再手工串起 `TestUI`、`TestCase`、`TestStruct`、`TestInterface`、`TestCppBinding`、`TestBlueprint`、`TestMap`、`TestArray`、`TestSet`、`TestActor` 等脚本模块。真正的判定机制主要落在脚本内的 `assert`、`print` 和 `os.clock()` 上，例如 `TestCase.lua` 会直接断言 `FVector`、`delegate`、`map`、`array` 行为，`TestPerf.lua` 则跑百万次循环后把耗时打印出来。

Angelscript 的验证链则被故意拆开。`Documents/Guides/Test.md:5-11,120-135,258-293` 把 `Tools\RunTests.ps1` / `Tools\RunTestSuite.ps1` 定义成唯一标准入口；`Tools/RunTestSuite.ps1:41-84` 把 suite 和前缀显式整理成工程能力；`Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs:6-50` 把测试模块收编成正式模块；`AngelscriptDirectoryWatcherTests.cpp:15-102` 和 `AngelscriptScriptExampleTestSupport.cpp:16-59` 再把 editor 行为和文档级脚本样例都收敛成 `IMPLEMENT_SIMPLE_AUTOMATION_TEST` 下的回归断言。两者差异不在“有没有验证”，而在**验证是否与 demo/sample 混在一起，以及能否形成独立的 automation contract**。

```
[D9/D10] Verification and Documentation Coupling
sluaunreal
├─ README.md                                      // 文档直接指向 demo script
├─ MyGameInstance loader                          // 固定从 Content/Lua 取字节
├─ SluaTestActor doFile("Test")                   // 游戏运行时驱动测试入口
├─ Test.lua -> require TestCase/TestPerf/...      // 手工串脚本波次
└─ assert / print / os.clock                      // 结果主要靠脚本自判和人工观察

Angelscript
├─ Test.md -> RunTests.ps1 / RunTestSuite.ps1     // 官方 runner
├─ AngelscriptTest module                         // 测试代码有正式归属
├─ IMPLEMENT_SIMPLE_AUTOMATION_TEST               // 回归断言形态统一
└─ report / summary / csv                         // 每次 run 都有结构化产物
```

[1] 关键源码：README 的性能段落确实指向仓库内 `TestPerf.lua`

```md
<!-- ============================================================================
文件: Reference/sluaunreal/README.md
位置: 365-373
位置说明: 性能表旁边直接声明测试脚本文件名
============================================================================ -->
100万次函数调用时间统计（秒），测试用例可以参考附带的TestPerf.lua文件。

### Performance

unit in second, 1,000,000 calls to C++ interface from Lua, compared reflection and cppbinding, (both reflection and cppbinding are supported by slua-unreal).

Test on MacOSX, Unreal 4.18 develop building, CPU i7 4GHz, test cases can be found in TestPerf.lua
<!-- ★ 这里不是外链说明，而是直接把性能验证入口绑定到仓库里的 demo script -->
```

[2] 关键源码：slua 的 demo 工程会在运行时直接装载 `Content/Lua`，并以 `Test.lua` 作为总调度脚本

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Source/democpp/MyGameInstance.cpp
// 文件: Reference/sluaunreal/Source/democpp/SluaTestActor.cpp
// 函数: UMyGameInstance::CreateLuaState / ASluaTestActor::BeginPlay / Tick
// 位置: MyGameInstance.cpp:36-64; SluaTestActor.cpp:18-41
// ============================================================================
state = new NS_SLUA::LuaState("SLuaMainState", this);
state->setLoadFileDelegate([](const char* fn, FString& filepath)->TArray<uint8> {
    FString path = FPaths::ProjectContentDir();
    path /= "Lua";
    path /= filename.Replace(TEXT("."), TEXT("/"));
    ...
    TArray<FString> luaExts = { UTF8_TO_TCHAR(".lua"), UTF8_TO_TCHAR(".luac") };
    for (auto& it : luaExts) {
        auto fullPath = path + *it;
        FFileHelper::LoadFileToArray(Content, *fullPath);
        if (Content.Num() > 0) {
            filepath = fullPath;
            return MoveTemp(Content);
        }
    }
    return MoveTemp(Content);
});
// ★ loader 天然把 Content/Lua 当作脚本仓库，demo、文档样例和“测试脚本”共享一条装载路径

ls->doFile("Test");
ls->call("begin", this->GetWorld(), this);
...
ls->call("update", DeltaTime);
// ★ `Test.lua` 被当作总入口持续驱动，而不是一次性 automation case
```

[3] 关键源码：`Test.lua` 手工拼装脚本波次，`TestCase.lua` / `TestPerf.lua` 自己承担断言与性能输出

```lua
-- ============================================================================
-- 文件: Reference/sluaunreal/Content/Lua/Test.lua
-- 文件: Reference/sluaunreal/Content/Lua/TestCase.lua
-- 文件: Reference/sluaunreal/Content/Lua/TestPerf.lua
-- 位置: Test.lua:47-60; TestCase.lua:94-110,134-144; TestPerf.lua:3-55
-- ============================================================================
function testcase()
    -- require 'TestPerf'
    require 'TestUI'
    require 'TestCase'
    require 'TestStruct'
    require 'TestInterface'
    require 'TestCppBinding'
    TestBp = require 'TestBlueprint'
    ...
    TestMap = require 'TestMap'
    TestArray = require 'TestArray'
    TestSet = require 'TestSet'
end
-- ★ 这里的“测试波次”是手工 `require` 链，不是独立 suite / report 系统

local info = t:GetUserInfo()
assert(info.name=="女战士")
assert(info.id==1001001)
assert(info.level==12)
...
local map2 = t:GetMap(map1)
print("map1 == map2", map1, map2, assert(map1 == map2))
-- ★ 断言散落在示例脚本内部，失败语义主要依赖 Lua assert 和控制台输出

local TestCount = 1000000
for i=1,TestCount do
    t:ReturnInt()
end
print("1m call ReturnInt, take time", os.clock()-start)
...
print("1m call FuncWithStr(cppbinding), take time", os.clock()-start)
-- ★ 性能验证也是脚本内微基准，结果以 print 形式输出，不形成标准 automation 报告
```

[4] 对照源码：Angelscript 把文档、runner、测试模块和断言链拆成正式工程能力

```powershell
# ============================================================================
# 文件: Documents/Guides/Test.md
# 文件: Tools/RunTestSuite.ps1
# 文件: Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs
# 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp
# 文件: Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleTestSupport.cpp
# 位置: Test.md:5-11,120-135,258-293; RunTestSuite.ps1:41-84;
#       AngelscriptTest.Build.cs:6-50; AngelscriptDirectoryWatcherTests.cpp:15-102;
#       AngelscriptScriptExampleTestSupport.cpp:16-59
# ============================================================================
- 本仓库的标准自动化测试入口是 `Tools\RunTests.ps1`
- 具名 suite 只能通过 `Tools\RunTestSuite.ps1` 调度
- `Tools\RunTests.ps1` / `Tools\RunTestSuite.ps1` 负责仓库内标准自动化测试入口、日志、摘要和超时收口
# ★ 文档先把“怎么跑测试”标准化，避免 demo/sample 自己定义验证入口

"ScenarioSamples" = @(
    @{ Prefix = "Angelscript.TestModule.Actor"; Label = "Actor" }
    @{ Prefix = "Angelscript.TestModule.Component"; Label = "Component" }
    @{ Prefix = "Angelscript.TestModule.Delegate"; Label = "Delegate" }
    @{ Prefix = "Angelscript.TestModule.Interface"; Label = "Interface" }
)
# ★ suite 是正式脚本能力，而不是靠某个 demo module 顺手 `require`

public class AngelscriptTest : ModuleRules
{
    PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "GameplayTags", "Json", "JsonUtilities", "AngelscriptRuntime", });
    if (Target.bBuildEditor)
    {
        PrivateDependencyModuleNames.AddRange(new string[] { "CQTest", "Networking", "Sockets", "UnrealEd", "AngelscriptEditor", });
    }
}
# ★ 测试代码有独立模块归属，夹具和运行时依赖是显式的

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAngelscriptDirectoryWatcherScriptQueueTest,
    "Angelscript.Editor.DirectoryWatcher.Queue.ScriptAddAndRemove",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
...
TestEqual(TEXT("DirectoryWatcher.Queue.ScriptAddAndRemove should queue one added script"), Engine->FileChangesDetectedForReload.Num(), 1);
# ★ 行为回归有明确 test name 和结构化断言

const bool bCompiled = CompileAnnotatedModuleFromMemory(&Engine, ModuleName, VirtualFileName, CombinedScriptCode);
Test.TestTrue(*FString::Printf(TEXT("Compile example '%s' succeeds"), *ExampleFileName), bCompiled);
# ★ 连文档级 script example 也要走 automation contract，而不是只在 README 里演示
```

### 设计取舍

- slua 的 demo-first 验证非常接近真实接入路径。脚本、蓝图、loader、profiler 都在同一个项目里跑起来，调试成本低；代价是回归结果更依赖脚本内部 `assert` 和人工观察控制台。
- Angelscript 的 runner-first 验证把测试入口、测试资产和示例资产拆开，维护成本更高；换来的是 suite、日志、摘要、超时和 test naming 都能统一治理。
- 因此这里的结论应区分三层：slua 不是“没有验证”；但它在当前源码边界里`没有实现`与 Angelscript 对等的独立 automation test 模块和官方 runner；它拥有的是一种 demo/sample 驱动的验证体系。

### 与 Angelscript 对比

| 观察点 | sluaunreal | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 文档与性能样例关系 | README 直接指向仓库内 `TestPerf.lua`（`Reference/sluaunreal/README.md:365-373`; `Reference/sluaunreal/Content/Lua/TestPerf.lua:3-55`） | `Test.md` 直接指向官方 runner 和 summary 产物（`Documents/Guides/Test.md:5-11,120-135`） | 实现方式不同 |
| 验证入口 | `MyGameInstance` loader + `SluaTestActor::doFile("Test")`（`MyGameInstance.cpp:36-64`; `SluaTestActor.cpp:18-41`） | `Tools\RunTests.ps1` / `Tools\RunTestSuite.ps1`（`Documents/Guides/Test.md:5-11,258-293`; `Tools/RunTestSuite.ps1:41-84`） | 实现方式不同 |
| 验证编排单位 | `Test.lua` 手工 `require` 多个脚本模块（`Reference/sluaunreal/Content/Lua/Test.lua:47-60`） | suite 前缀和 automation test name（`Tools/RunTestSuite.ps1:41-84`; `AngelscriptDirectoryWatcherTests.cpp:15-38`） | 实现方式不同 |
| 断言与报告 | `assert / print / os.clock` 脚本自判（`TestCase.lua:94-110,134-144`; `TestPerf.lua:6-55`） | `IMPLEMENT_SIMPLE_AUTOMATION_TEST` + 结构化 runner 输出（`AngelscriptDirectoryWatcherTests.cpp:15-102`; `Documents/Guides/Test.md:120-135`） | 实现质量差异：Angelscript 更适合长期回归 |
| 独立测试模块 | 当前 slua 插件源码内未见对等 automation test module，验证主要落在 `democpp` 与 `Content/Lua` | `AngelscriptTest` 是正式模块（`AngelscriptTest.Build.cs:6-50`） | 没有实现 |

---

## 深化分析 (2026-04-09 06:48:08)

### [维度 D4 / D11] slua 的模块热更首先受 `require` 缓存约束，而不是受 bytes provider 约束

前文已经确认 slua 的线上入口是 `LoadFileDelegate -> loader()`。这轮把链路继续追到 vendored Lua 后，能看到一个更硬的边界：**slua 改写的是 `package.searchers[2]`，不是 `require` 的缓存策略**。`LuaState::requireModule()` 只是直接调用全局 `require`；而 `ll_require()` 在真正找 loader 之前，会先查 `_LOADED[name]`。这意味着项目侧即使换掉了 `LoadFileDelegate` 背后的 CDN / 补丁包数据源，只要模块名不变，Lua 标准缓存仍然会先截住调用。

因此，slua 的 runtime patch 若想替换同名模块，项目侧要么手工失效 `package.loaded[name]`，要么原位修改旧 table；插件核心自身没有提供对等的模块事务、版本淘汰或失败后“保留旧模块继续执行”的治理层。和前文已经提到的 UObject 级 hook 回滚不同，这里说的是**模块身份层**没有被产品化。

```
[D4] Module Reload Identity Boundary
sluaunreal
├─ LoadFileDelegate -> LuaState::loader()          // 宿主只负责提供 bytes
├─ package.searchers[2] = slua loader             // 插件只改“如何找 loader”
├─ LuaState::requireModule() -> require(name)     // 模块入口仍走 Lua 标准 require
└─ _LOADED[name] hit -> return cached module       // 同名模块先被缓存截住

Angelscript
├─ FileChangesDetectedForReload                    // 明确文件变更队列
├─ CheckForHotReload()                            // 取队列、合并删除/全量请求
├─ ClassGenerator::Setup()                        // 分析 Soft/Full/Error
└─ swap / keep-old / full-reload                  // 事务式重载决策
```

[1] slua 只替换 searcher，不替换 `require` 语义

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaState.cpp
// 函数: LuaState::loader / LuaState::requireModule
// 位置: 131-145, 606-616, 768-782
// 位置说明: 自定义 loader 被插到 package.searchers[2]，但模块入口仍然直接调用 require
// ============================================================================
int LuaState::loader(lua_State* L) {
    LuaState* state = LuaState::get(L);
    const char* fn = lua_tostring(L,1);
    FString filepath;
    TArray<uint8> buf = state->loadFile(fn, filepath);
    if(buf.Num() > 0) {
        char chunk[256];
        snprintf(chunk,256,"@%s",TCHAR_TO_UTF8(*filepath));
        if(luaL_loadbuffer(L,(const char*)buf.GetData(),buf.Num(),chunk)==0) {
            return 1; // ★ 这里的契约只是“拿到 bytes 后编译 chunk”
        }
    }
    return 0;
}

lua_getglobal(L,"package");
lua_getfield(L,-1,"searchers");
...
lua_pushvalue(L,loaderFunc);
lua_rawseti(L,loaderTable,2);
// ★ slua 改的是 package.searchers[2]，不是 package.loaded / require 本身

LuaVar LuaState::requireModule(const char* fn, LuaVar* pEnv) {
    int top = LuaState::pushErrorHandler(L);
    lua_getglobal(L, "require");
    lua_pushstring(L, fn);
    if (lua_pcall(L, 1, 1, top))
    {
        lua_pop(L, 2);
        return LuaVar();
    }
    ...
}
```

[2] vendored Lua 明确先查 `_LOADED[name]`，再去跑 searchers

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/External/lua/loadlib.cpp
// 函数: findloader / ll_require
// 位置: 567-580, 595-614
// 位置说明: Lua 标准 require 先查 _LOADED，再遍历 package.searchers
// ============================================================================
static void findloader (lua_State *L, const char *name) {
  /* push 'package.searchers' to index 3 in the stack */
  if (lua_getfield(L, lua_upvalueindex(1), "searchers") != LUA_TTABLE)
    luaL_error(L, "'package.searchers' must be a table");
  for (i = 1; ; i++) {
    if (lua_rawgeti(L, 3, i) == LUA_TNIL) {
      ...
    }
    lua_pushstring(L, name);
    lua_call(L, 1, 2);
    if (lua_isfunction(L, -2))
      return; // ★ 只有 cache miss 后，才会走到 searchers 查 loader
  }
}

static int ll_require (lua_State *L) {
  const char *name = luaL_checkstring(L, 1);
  lua_settop(L, 1);
  lua_getfield(L, LUA_REGISTRYINDEX, LUA_LOADED_TABLE);
  lua_getfield(L, 2, name);  /* LOADED[name] */
  if (lua_toboolean(L, -1))
    return 1;  /* ★ 已加载模块直接返回，不再触发 slua 的 searcher */
  ...
}
```

[3] Angelscript 的热重载是显式事务，而不是借用脚本语言默认模块缓存

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 函数: FAngelscriptEngine::CheckForHotReload / reload switch / FAngelscriptClassGenerator::Setup
// 位置: AngelscriptEngine.cpp:2729-2762, 3934-4001;
//       AngelscriptClassGenerator.cpp:1872-1902
// ============================================================================
void FAngelscriptEngine::CheckForHotReload(ECompileType CompileType)
{
    TArray<FFilenamePair> FileList;
    FileList.Append(FileChangesDetectedForReload);
    FileChangesDetectedForReload.Empty();
    ...
} // ★ 先消费显式文件变更队列，而不是依赖脚本语言默认模块缓存

FAngelscriptClassGenerator::EReloadRequirement FAngelscriptClassGenerator::Setup()
{
    for (auto& ModuleData : Modules)
        Analyze(ModuleData);
    ...
    EReloadRequirement ReloadReq = EReloadRequirement::SoftReload;
    ...
    return ReloadReq;
} // ★ 先分析 reload requirement，再决定 Soft / Full / Error

case FAngelscriptClassGenerator::EReloadRequirement::FullReloadRequired:
    if (CompileType == ECompileType::SoftReloadOnly)
    {
        FString Msg =
            TEXT("Full Reload is required due to UPROPERTY() or UFUNCTION() changes, but cannot")
                TEXT(" perform a full reload right now. Keeping old angelscript code active.");
        ...
        bShouldSwapInModules = false;
        bFullReloadRequired = true;
    }
// ★ 失败和受限场景的“保留旧代码”是事务分支的一部分，不是脚本层偶然行为
```

### 设计取舍

- slua 的优点是宿主只要提供 bytes，就能把模块解析层换到本地文件、CDN、加密包或热更包；代价是模块身份与淘汰策略仍由 Lua 默认 `require` 语义主导。
- Angelscript 的优点是 reload 粒度、失败保留旧代码、Soft/Full 分流都在插件层显式建模；代价是体系更重，也更依赖引擎内分析阶段。
- 这里的关键差距不是“slua 不会热更”，而是**slua 没有实现插件层的模块缓存治理；Angelscript 实现的是事务式热重载**。

### 与 Angelscript 对比

| 观察点 | sluaunreal | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 模块解析入口 | 自定义 loader 插到 `package.searchers[2]`（`Reference/sluaunreal/.../LuaState.cpp:606-616`） | 文件变更进入 `FileChangesDetectedForReload`（`Plugins/Angelscript/.../AngelscriptEngine.cpp:2729-2747`） | 实现方式不同 |
| 模块身份缓存 | 仍受 `_LOADED[name]` 驱动（`Reference/sluaunreal/.../loadlib.cpp:595-614`） | 不依赖脚本语言模块缓存，直接分析模块图（`AngelscriptClassGenerator.cpp:1872-1902`） | 实现方式不同 |
| 同名模块失效治理 | 本轮源码边界未见插件层 `package.loaded` 失效 API | reload requirement、队列消费与 swap-in/swap-out 在插件层实现 | slua 没有实现 |
| 失败保持旧代码 | 主要依赖项目侧策略与 UObject hook 粒度回滚 | `FullReloadRequired/Error` 分支显式保留旧代码（`AngelscriptEngine.cpp:3972-4001`） | 实现质量差异：Angelscript 更产品化 |

### [维度 D8 / D5] slua 的 profiler 实际分裂成两套协议：在线裸包流与离线压缩档

前文已经分析过 profiler 是 slua 的重要能力，但这一轮把 transport 往下挖后，可以看到一个之前没单独拎出来的结构：**slua 的 live profiling 和 offline archive 根本不是同一套协议目标**。

在线链路里，`LuaProfiler.inl` 直接 `require("socket.core").tcp()` 连接 `8081`，runtime 侧 `makeProfilePackage()` 把 `hookEvent / time / line / name / shortSrc` 直接塞进 `FArrayWriter`，`slua_profile` 再按消息头尺寸读包并 `Deserialize()`。这里没有版本协商、没有字符串字典、也没有压缩块，目标明显是低门槛实时推送。

离线录制链路则完全不同。`ProfileVersion = 4`、`increaseString` 增量字符串表、按块压缩、load 时校验版本不匹配直接警告退出，这已经是正式文件格式设计。换句话说，slua 真正产品化的是 `.sluastat` 回放格式，而不是 live socket 流协议本身。

```
[D8] Profiler Data Planes
slua live stream
├─ LuaProfiler.inl -> require("socket.core").tcp()
├─ makeProfilePackage()                            // size + event + time + line + name + src
├─ sendMessage() over TCP
└─ FProfileMessage::Deserialize()                  // Editor 侧直接解包

slua offline archive
├─ ProfileVersion = 4
├─ increaseString delta table                      // 增量字符串字典
├─ SerializeFrameData()                            // frame + memory checkpoint
└─ CompressMemory / UncompressMemory               // 按块压缩回放

Angelscript
├─ metrics.json artifacts                          // 测试/性能观测制品
└─ PrecompiledData + PrecompiledDataGuid           // 执行制品与构建一致性
```

[1] 在线 profiler 是“裸包 + TCP”协议

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaProfiler.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_profile/Private/slua_remote_profile.cpp
// 函数: makeProfilePackage / makeMemoryProfilePackage / sendMessage / FProfileMessage::Deserialize
// 位置: LuaProfiler.cpp:155-248;
//       slua_remote_profile.cpp:22-30, 334-337, 365-384
// ============================================================================
void makeProfilePackage(FArrayWriter& messageWriter,
    int hookEvent, int64 time,
    int lineDefined, const char* funcName,
    const char* shortSrc)
{
    uint32 packageSize = 0;
    FString fname = FString(funcName);
    FString fsrc = FString(shortSrc);

    messageWriter << packageSize;
    messageWriter << hookEvent;
    messageWriter << time;
    messageWriter << lineDefined;
    messageWriter << fname;
    messageWriter << fsrc;
    ...
} // ★ live 流每条消息直接带函数名和源码短路径，没有版本头也没有字典压缩

void sendMessage(FArrayWriter& msg, lua_State* L) {
    if (!tcpSocket) return;
    ...
    profiler_sendraw(&tcpSocket->buf, (const char*)msg.GetData(), msg.Num(), &sent);
} // ★ 直接 TCP 写出

FAutoConsoleVariableRef CVarSluaProfilerPort(TEXT("slua.ProfilerPort"), FProfileServer::Port, ...);
int32 FProfileServer::Port = 8081;
...
if (DeserializedMessage->Deserialize(RecvMessageData))
{
    Inbox.Enqueue(MakeShareable(DeserializedMessage));
}

bool FProfileMessage::Deserialize(const TSharedPtr<FArrayReader, ESPMode::ThreadSafe>& Message)
{
    MessageReader << Event;
    ...
    MessageReader << Time;
    MessageReader << Linedefined;
    MessageReader << Name;
    MessageReader << ShortSrc;
    return true; // ★ Editor 侧按固定字段顺序直接解包，没有显式 schema/version handshake
}
```

[2] 离线 `.sluastat` 是有版本和增量字符串字典的正式文件格式

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/ProfileDataDefine.h
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/SluaProfilerDataManager.cpp
// 函数: FProfileNameSet / SerializeFrameData / SaveDataWithData / LoadData
// 位置: ProfileDataDefine.h:28-47, 54-70, 99-113;
//       SluaProfilerDataManager.cpp:453-481, 596-634, 664-699, 927-947
// ============================================================================
static int32 ProfileVersion = 4;
...
TMap<uint32, FString> increaseString;
...
uint32 GetOrCreateIndex(const FString& content)
{
    ...
    increaseString.Emplace(hashKey, content);
    return hashKey; // ★ 新字符串先登记到增量字典，后续按 index 写 frame
}

void FProfileDataProcessRunnable::SerializeFrameData(FArchive& ar, ...)
{
    ...
    if (bFrameFirstRecord)
    {
        increaseString = profileNameSet.indexToString;
        bFrameFirstRecord = false;
    }
    ar << increaseString;
    ...
    increaseString.Empty();
} // ★ 首帧全量字典，后续帧只写增量字典

void FProfileDataProcessRunnable::SaveDataWithData(...)
{
    *ar << ProfileVersion;
    *ar << inCpuViewBeginIndex << inMemViewBeginIndex;
    ...
    SerializeCompreesedDataToFile(*ar);
}

if (version != ProfileVersion)
{
    UE_LOG(Slua, Warning, TEXT("sluastat file version mismatch: %d, %d"), version, ProfileVersion);
    return; // ★ 离线文件格式有显式版本门槛
}

FCompression::CompressMemory(...);
FCompression::UncompressMemory(...);
// ★ `.sluastat` 明确按压缩块存取，不是把 live 包简单落盘
```

[3] Angelscript 把“观测制品”和“执行制品”拆成两条正式产物链

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptPerformanceTestUtils.h
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptPerformanceArtifactTests.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h
// 位置: AngelscriptPerformanceTestUtils.h:40-78;
//       AngelscriptPerformanceArtifactTests.cpp:21-29;
//       PrecompiledData.cpp:2620-2683;
//       StaticJITHeader.h:74-79
// ============================================================================
inline FString WritePerformanceMetricsArtifact(...)
{
    ...
    const FString MetricsPath = FPaths::Combine(MetricsDirectory, TEXT("metrics.json"));
    FFileHelper::SaveStringToFile(Output, *MetricsPath);
    return MetricsPath;
} // ★ 性能观测产物是可审计 JSON，而不是专用 socket 协议

const FString MetricsPath = WritePerformanceMetricsArtifact(...);
TestTrue(TEXT("Performance artifact generation test should write metrics.json"), PlatformFile.FileExists(*MetricsPath));
// ★ 观测制品由 automation test 回归保护

DataGuid = FGuid::NewGuid();
...
FFileHelper::SaveArrayToFile(Data, *Filename);
FFileHelper::LoadFileToArray(LoadedData, *Filename);

struct FStaticJITCompiledInfo
{
    FGuid PrecompiledDataGuid;
}; // ★ 执行制品另有 GUID 账本，不混入 profiler 传输
```

### 设计取舍

- slua live profiler 的优点是接线极薄，Lua 侧连上 socket 就能推送 sample；代价是协议稳定性、带宽效率和版本协商能力都弱。
- slua offline `.sluastat` 的优点是已经具备正式文件格式的基本要素；代价是 live 与 offline 两套协议分裂，调试和工具维护要同时照顾两边。
- Angelscript 的优点是把性能观测和执行制品拆开治理；代价是没有 slua 这种“Lua 侧脚本一连就能看实时 profile”的轻量路径。

### 与 Angelscript 对比

| 观察点 | sluaunreal | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 在线观测协议 | `TCP + FArrayWriter` 裸包（`Reference/sluaunreal/.../LuaProfiler.cpp:155-248`; `slua_remote_profile.cpp:365-384`） | 本轮所读 profiling 相关源码未见对等 runtime socket profiling 协议 | 实现方式不同 |
| 离线观测制品 | `.sluastat` 有 `ProfileVersion`、增量字典、压缩块（`ProfileDataDefine.h:28-47,54-70`; `SluaProfilerDataManager.cpp:453-481,664-699,927-947`） | `metrics.json` 由 automation test 产出并回归（`AngelscriptPerformanceTestUtils.h:40-78`; `AngelscriptPerformanceArtifactTests.cpp:21-29`） | 实现方式不同 |
| 执行制品治理 | profiler 文件格式成熟，但与执行制品无统一身份账本 | `DataGuid` / `PrecompiledDataGuid` 属于执行制品账本（`PrecompiledData.cpp:2620-2683`; `StaticJITHeader.h:74-79`） | 实现质量差异：Angelscript 的执行制品治理更强 |
| live 协议版本协商 | 当前 live 包未见显式 version handshake | Debug/执行相关能力普遍带版本或制品 GUID 校验 | slua 在 live profiler 协议上没有实现对等治理 |

### [维度 D11 / D1] slua 插件更先产品化了“runtime 网络能力”，而不是“binary-only 部署模式”

这一点和前文“只有 bytes provider ABI”并不矛盾，但更具体。`LuaState::InitExtLib()` 会在 runtime 初始化时直接把 `socket.core` 塞进 `package.preload`；`LuaProfiler.inl` 随后就能在 Lua 层 `require("socket.core").tcp()` 并尝试连接 `8081`。也就是说，**slua 把网络 client primitive 当作 runtime 默认能力 shipped 进去了**，而不是把它隔离在 editor-only tool 或宿主项目外部依赖里。

与之形成反差的是 binary-only 部署硬化。vendored Lua 明明已经加了 `onlyluac` 开关，parser 也支持只接受 binary chunk；但 slua runtime 主链仍然是 `luaL_loadbuffer()`，本轮未见 `lua_setonlyluac()` 接线点。结果就是：插件核心默认准备好了联网拿数据的能力，却没有默认打开“只吃字节码、不吃明文源码”的硬约束。

```
[D11] Deployment Surface Priority
slua runtime
├─ InitExtLib() -> package.preload["socket.core"]  // 网络能力预装到 VM
├─ LuaProfiler.inl -> require("socket.core").tcp() // Lua 层可直接建 TCP
├─ loader() -> luaL_loadbuffer(bytes)              // 任何 bytes 都能尝试编译
└─ onlyluac exists but not wired                    // binary-only 仍停在 VM 开关

Angelscript runtime
├─ PrecompiledData::Save/Load                      // 本地文件制品
├─ BuildIdentifier / DataGuid                      // 构建与制品一致性
└─ PrecompiledDataGuid handshake                   // 编译进二进制的 JIT 对齐
```

[1] `socket.core` 是 runtime 预置能力，不是宿主项目自己额外 open 的库

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaState.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaProfiler.inl
// 函数: LuaState::InitExtLib / Lua profiler reconnect
// 位置: LuaState.cpp:1319-1328; LuaProfiler.inl:41-60, 85-118
// ============================================================================
void LuaState::InitExtLib(lua_State* ls)
{
    lua_getglobal(ls, "package");
    lua_getfield(ls, -1, "preload");

    static const luaL_Reg s_lib_preload[] = {
        { "socket.core", luaopen_socket_core },
        { NULL, NULL }
    };
    ...
} // ★ runtime 初始化时就把 socket.core 放进 package.preload

port = tonumber(port) or 8081
...
if pcall(function() sock = require("socket.core").tcp() end) then
    this.printToConsole("reGetSock success")
    sock:settimeout(ConnectTimeoutSec)
end
...
local sockSuccess, status = sock:connect(connectHost, connectPort)
// ★ Lua 侧不需要宿主工程额外 open lib，就能直接创建 TCP client
```

[2] binary-only 开关只存在于 vendored Lua 层，未进入 slua runtime 主链

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/External/lua/lstate.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/External/lua/ldo.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaState.cpp
// 函数: lua_setonlyluac / f_parser / LuaState::loader
// 位置: lstate.cpp:236-240; ldo.cpp:763-773; LuaState.cpp:131-145, 729-738
// ============================================================================
L->onlyluac = 0;

LUA_API void lua_setonlyluac(lua_State *L, int v) {
    L->onlyluac = v;
} // ★ VM 已经有 binary-only 开关

if (L->onlyluac == 0) {
    ...
    checkmode(L, p->mode, "text");
    cl = luaY_parser(...); // ★ 默认仍然接受 text chunk
} else {
    checkmode(L, p->mode, "binary");
    cl = luaU_undump(...);
}

if(luaL_loadbuffer(L,(const char*)buf.GetData(),buf.Num(),chunk)==0) {
    return 1;
}
...
if(luaL_loadbuffer(L, (const char *)buf, len, chunk)) {
    const char* err = lua_tostring(L,-1);
    Log::Error("DoBuffer failed: %s",err);
}
// ★ runtime 主链始终是“收到 bytes 就 loadbuffer”；本轮所读 plugin 源码未见 lua_setonlyluac 调用
```

[3] Angelscript 把部署重点放在本地制品账本与构建一致性，不放在 VM 侧联网能力

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: PrecompiledData.cpp:2620-2683; StaticJITHeader.h:74-79;
//       AngelscriptEngine.cpp:1552-1552
// ============================================================================
DataGuid = FGuid::NewGuid();
...
FFileHelper::SaveArrayToFile(Data, *Filename);
FFileHelper::LoadFileToArray(LoadedData, *Filename);
// ★ 部署产物首先是本地 cache 文件，而不是 runtime 预装网络库

struct FStaticJITCompiledInfo
{
    FGuid PrecompiledDataGuid;
};

if (CompiledInfo != nullptr && CompiledInfo->PrecompiledDataGuid != PrecompiledData->DataGuid)
// ★ 二进制内嵌 JIT 与磁盘 cache 还要继续做 GUID 对齐
```

### 设计取舍

- slua 的优点是把网络能力直接送进 runtime，项目要接远端 profiler、远端脚本或自定义分发时门槛低。
- 代价是部署安全面和执行面优先级不对称：联网 primitive 已经预装，而 binary-only、签名、版本淘汰仍停留在插件边界之外。
- Angelscript 的方向更保守：先把本地制品一致性做实，再决定是否允许更激进的 runtime 分发路径。

### 与 Angelscript 对比

| 观察点 | sluaunreal | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| runtime 网络能力 | `socket.core` 由 `InitExtLib()` 预装到 `package.preload`（`Reference/sluaunreal/.../LuaState.cpp:1319-1328`） | 本轮所读部署/执行链未见对等 runtime 预装 socket library | 实现方式不同 |
| Lua 层联网入口 | `LuaProfiler.inl` 直接 `require("socket.core").tcp()`（`Reference/sluaunreal/.../LuaProfiler.inl:41-60,85-118`） | 性能/执行制品走本地文件（`PrecompiledData.cpp:2666-2683`; `AngelscriptPerformanceTestUtils.h:40-78`） | 实现方式不同 |
| binary-only 接线 | VM 有 `onlyluac`，但 runtime 主链未接线（`lstate.cpp:236-240`; `ldo.cpp:763-773`; `LuaState.cpp:131-145,729-738`） | 部署模式直接体现在 cache / GUID / build gate（`PrecompiledData.cpp:2620-2683`; `AngelscriptEngine.cpp:1552`） | 实现质量差异：Angelscript 更显式 |
| 加密/签名/版本包策略 | 本轮新增源码仍未见插件层正式协议；重点先落在联网与 bytes 装载 | 也未见密码学签名，但制品一致性治理更强 | slua 在部署硬化层没有实现对等能力 |

---

## 深化分析 (2026-04-09 06:57:38)

### [维度 D2] `lua-wrapper` 的真实边界是“补位型静态导出”，不是通吃式绑定生成

前文已经确认 slua 是“静态 + 反射”混合绑定，但这轮把工具链和生成物对上之后，可以更精确地下结论：`lua-wrapper` 并不是要替代反射和 `LuaCppBinding`，而是一个被 `config.json` 强约束、靠 denylist 维持稳定性的补位层。它的目标不是“自动导出全部 UE API”，而是“把反射和模板绑定都覆盖不到、且能稳定生成的那一小段 USTRUCT 面补上”。

这点在文档和生成物里是双重可证的。`Tools/README.md:9-19` 明说三条导出路径并存，且 `lua-wrapper` 只用于“前两种不支持的接口”；`config.json:11-65` 则把这件事落成了真实的工程约束，直接列出 `FTransform`、`FVector`、`FQuat` 等类型的过滤规则。再看生成结果，`LuaWrapper5.2.inc:14853-14936` 确实给 `FTransform` 生成了 wrapper，但被过滤的方法根本不会出现；而 `LuaWrapper5.2.inc:10362-10410` 给 `FVector` 展开的则是纯 `lua_CFunction` 级别的方法表。这说明 slua 的“静态导出”性能收益，建立在**人工维护可生成边界**之上，而不是建立在“自动覆盖面足够大”之上。

```
[D2] Static Export Boundary
Tools/config.json
├─ export_files:TBaseStructure + Customs          // 输入集合受配置控制
├─ filter / filter_class                          // 显式裁掉不稳定 API
└─ output_dir -> LuaWrapper5.2.inc               // 产物直接回写 Runtime/Private

slua binding stack
├─ Reflection for Blueprint-visible APIs          // 先吃可反射面
├─ LuaCppBinding for template-friendly C++        // 再吃模板可推导面
└─ lua-wrapper for residual USTRUCT surface       // 最后才补位

Angelscript binding stack
├─ Bind_*.cpp                                     // 手写规则层
├─ Bind_BlueprintType / Bind_BlueprintCallable    // Blueprint 反射面系统化接入
└─ UHT AS_FunctionTable_*                         // 生成的是函数表，不是 denylist 式 struct wrapper
```

[1] `lua-wrapper` 的定位不是主绑定层，而是受配置文件驱动的补位器

```json
// ============================================================================
// 文件: Reference/sluaunreal/Tools/README.md
// 文件: Reference/sluaunreal/Tools/config.json
// 位置: README.md:9-19,35-58; config.json:11-33,59-65
// 位置说明: 工具自述直接定义了导出边界，config 再把边界编码成 filter
// ============================================================================
// README.md
9:lua-wrapper 是作为 slua-unreal 中 lua 导出接口的补充，slua-unreal 支持 3 种接口导出的方式：
10:1. 反射，凡是支持 blueprint 的类型，都可以直接在 lua 中通过反射的形式访问
11:2. LuaCppbinding，通过 C++ 模版的自动推导导出 lua 接口
12:3. lua-wrapper，通过静态代码生成导出以上两种方式不支持的接口
...
15:1. 不支持导出自定义类型
16:2. 不支持导出可反射的类型
17:3. 导出类型限定于引擎中的 USTRUCT 类型
18:4. 优先使用反射或者 LuaCppBinding 导出类型，最后才考虑使用 lua-wrapper

// config.json
11:    "filter": [
12:        {
13:            "type": "FTransform",
14:            "ctors": [ 3 ],
15:            "methods": [ "Accumulate", "LerpTranslationScale3D", "AccumulateWithShortestRotation" ]
            // ★ 某些 API 直接进 denylist，说明生成器不是“能出就全出”
16:        },
...
31:            "type": "FVector",
32:            "methods": [ "GenerateClusterCenters" ]
            // ★ 连常见 Core Math 类型也需要人工排除不稳定成员
33:        },
59:    "filter_class": [
60:        "FPolyglotTextData",
61:        "FTestUndeclaredScriptStructObjectReferencesTest"
            // ★ 某些整类直接禁止生成
65:    "output_dir": "{solution_dir}/Plugins/slua_unreal/Source/slua_unreal/Private/"
            // ★ 生成物直接覆盖 Runtime 私有实现目录
```

[2] 生成物证明 denylist 确实生效，而且导出结果是纯静态 `lua_CFunction` 注册表

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaWrapper.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaWrapper5.2.inc
// 位置: LuaWrapper.cpp:55-67,184-188; LuaWrapper5.2.inc:10360-10410,14851-14936
// 位置说明: 版本化 include 载入生成物；FVector/FTransform 的注册表展示了“有选择的静态导出”
// ============================================================================
// LuaWrapper.cpp
55:#if ((ENGINE_MINOR_VERSION<25) && (ENGINE_MAJOR_VERSION==4))
56:    #include "LuaWrapper4.18.inc"
...
61:#elif ((ENGINE_MINOR_VERSION==2) && (ENGINE_MAJOR_VERSION==5))
62:    #include "LuaWrapper5.2.inc"
            // ★ 每个 UE 次版本都固化成独立生成物，升级成本直接映射到文件分叉
...
184:    void LuaWrapper::initExt(lua_State* L)
185:    {
186:        init(L);
187:        FSoftObjectPtrWrapper::bind(L);
            // ★ runtime 只负责装载生成结果，本身不再推导类型
188:    }

// LuaWrapper5.2.inc
10360:        static void bind(lua_State* L) {
10362:            LuaObject::newType(L, "FVector");
10363:            LuaObject::addOperator(L, "__add", __add);
10372:            LuaObject::addField(L, "X", get_X, set_X, true);
10386:            LuaObject::addMethod(L, "DiagnosticCheckNaN", DiagnosticCheckNaN, true);
10399:            LuaObject::addMethod(L, "Normalize", Normalize, true);
10401:            LuaObject::addMethod(L, "GetSafeNormal", GetSafeNormal, true);
            // ★ 这里是纯静态注册：方法名和入口函数在生成阶段就钉死

14851:        static void bind(lua_State* L) {
14853:            LuaObject::newType(L, "FTransform");
14872:            LuaObject::addMethod(L, "Blend", Blend, true);
14905:            LuaObject::addMethod(L, "Equals", Equals, true);
14919:            LuaObject::addMethod(L, "SetTranslationAndScale3D", SetTranslationAndScale3D, true);
14935:            LuaObject::addMethod(L, "clone", clone, true);
14936:            LuaObject::finishType(L, "FTransform", __ctor, __gc);
            // ★ 对照 config.json 的 denylist，可见 Accumulate 等方法没有被导出进来
```

[3] Angelscript 的生成重点是函数表和绑定决策，不是靠 struct denylist 维持可生成性

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp
// 位置: Bind_BlueprintType.cpp:741-755; Bind_BlueprintCallable.cpp:72-90
// 位置说明: Angelscript 先按类库数据库扫描函数，再决定 direct bind 还是 reflective fallback
// ============================================================================
// Bind_BlueprintType.cpp
741:        for (auto& DBFunc : DBBind.Methods)
742:        {
743:            UFunction* Function = Class->FindFunctionByName(*DBFunc.UnrealPath);
...
747:            if (Function->HasAnyFunctionFlags(FUNC_BlueprintEvent | FUNC_NetFuncFlags))
748:                BindBlueprintEvent(ClassType.ToSharedRef(), Function, DBFunc);
754:                BindBlueprintCallable(ClassType.ToSharedRef(), Function, DBFunc);
            // ★ 生成/绑定的主对象是 UFunction 集合，不是逐个 USTRUCT 的 denylist wrapper

// Bind_BlueprintCallable.cpp
72:    auto* DirectNativePointer = &Entry->FuncPtr;
73:    const bool bHasDirectNativePointer = DirectNativePointer != nullptr && DirectNativePointer->IsBound();
74:    if (!bHasDirectNativePointer)
75:    {
76:        if (!BindBlueprintCallableReflectiveFallback(InType, Function, Signature, *Entry))
77:            return;
            // ★ 没有直连入口才回退 reflective fallback
90:        return;
    }
```

### 设计取舍

- slua 的静态导出收益很实在：一旦进入 `LuaWrapper*.inc`，运行时只剩 `lua_CFunction` 分派，不再做 `UFunction` 反射遍历。
- 但代价同样直接：类型覆盖面必须被人为裁剪，UE 小版本变动会放大成 `LuaWrapper4.18/4.25/5.1/5.2/5.3/5.4` 这些分叉维护成本。
- Angelscript 也不是“完全自动”，但它把生成压力放在函数表和签名决策上，而不是把大量版本化 wrapper 文件长期留在仓库里。

### 与 Angelscript 对比

| 观察点 | sluaunreal | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 静态生成的职责 | `lua-wrapper` 只补位前两条绑定路径覆盖不到的接口（`Tools/README.md:9-19`） | UHT 与绑定层面向 `UFunction`/类型数据库批量工作（`Bind_BlueprintType.cpp:741-755`） | 实现方式不同 |
| 生成稳定性手段 | `filter`/`filter_class` denylist 是主稳定器（`Tools/config.json:11-65`） | 主要依赖 direct bind 与 fallback 选择，不靠大规模 denylist struct wrapper | 实现方式不同 |
| 版本适配方式 | `LuaWrapper4.18/4.25/5.1/5.2/5.3/5.4.inc` 分叉（`LuaWrapper.cpp:55-67`） | 绑定文件长期稳定，版本差异更多在签名和 UHT 产物处理 | 实现质量差异：Angelscript 的版本维护面更集中 |
| “自动化”含义 | 更像“半自动生成 + 人工裁剪” | 更像“数据库驱动的批量函数绑定 + fallback” | 实现方式不同 |

### [维度 D3] slua 的 Blueprint 互通其实是“双通道”：`Super` 覆写链 + `FLuaBPVar` 泛型值总线

前文已经写过 slua 用 `hookBpScript()` 改写 Blueprint 事件入口，但这轮看到 `LuaBlueprintLibrary` 和 `LuaObject` 的配套代码后，能把 Blueprint 互通结构再拆得更细：slua 不是只有“覆写事件”这一条路，它还有一条完全独立的“泛型值总线”。

这条总线的核心是 `FLuaBPVar`。它是一个 `BlueprintType`，内部只包了一个 `LuaVar`；`ULuaBlueprintLibrary::CallToLuaWithArgs()` 接收 `TArray<FLuaBPVar>`，把每个 `LuaVar` 直接压栈给 Lua；`LuaObject` 又在 `pushUStructProperty()` / `checkUStructProperty()` 里给 `FLuaBPVar` 开了硬编码特判。于是 Blueprint 不需要为每种跨语言值单独生成节点，就能把“任意 Lua 值/多返回值”用一个 `USTRUCT` 容器搬过去。

另一条通道则是事件/函数覆写链。`bindOverrideFuncs()` 找到 Lua 模块后，会枚举 Lua table 里的函数名，再对匹配到的 `UFunction` 执行 `hookBpScript()`；后者先复制一个 `Super_` 版本的 `UFunction` 保存原入口，再把当前函数改成 `luaOverrideFunc`，`setmetatable()` 再给 Lua `self` 注入 `SLUA_CPPINST` 和 `LuaSuperCall`。这意味着 slua 的 Blueprint 互通不是单一“反射调用”，而是**泛型值桥 + 覆写继承桥**同时存在。

```
[D3] Blueprint Interop Dual Path
slua Blueprint path A
Blueprint Node
├─ ULuaBlueprintLibrary::CallToLua*               // 统一蓝图入口
├─ FLuaBPVar / TArray<FLuaBPVar>                  // 泛型值容器
└─ LuaVar push/get                                // 直接进 Lua 栈

slua Blueprint path B
UObject / Blueprint Event
├─ bindOverrideFuncs()                            // 发现 Lua 模块与函数名
├─ hookBpScript()                                 // duplicate Super_ UFunction
└─ setmetatable()                                 // 注入 SLUA_CPPINST + LuaSuperCall

Angelscript
Bind_BlueprintType
├─ BindBlueprintEvent                             // 事件按签名绑定
├─ BindBlueprintCallable                          // 函数按签名绑定
└─ FAngelscriptAnyStructParameter                 // 仅有 any-struct 级别的泛型容器
```

[1] `FLuaBPVar` 不是普通辅助类型，而是 slua 的 Blueprint 泛型值总线

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaBlueprintLibrary.h
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaBlueprintLibrary.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaObject.cpp
// 位置: LuaBlueprintLibrary.h:21-31,41-76;
//       LuaBlueprintLibrary.cpp:51-77,124-145;
//       LuaObject.cpp:2240-2246,2436-2439
// 位置说明: `FLuaBPVar` 定义、蓝图入口、以及 LuaObject 对它的硬编码特判
// ============================================================================
// LuaBlueprintLibrary.h
21:USTRUCT(BlueprintType)
22:struct SLUA_UNREAL_API FLuaBPVar {
25:    FLuaBPVar(const NS_SLUA::LuaVar& v) :value(v) {}
29:    NS_SLUA::LuaVar value;
31:    static void* checkValue(NS_SLUA::lua_State* L, NS_SLUA::FStructProperty* p, uint8* params, int i);
        // ★ 一个 BlueprintType 里直接内嵌 LuaVar，本质是类型擦除总线
...
42:    static FLuaBPVar CallToLuaWithArgs(UObject* WorldContextObject, FString FunctionName,const TArray<FLuaBPVar>& Args,FString StateName);
46:    static FLuaBPVar CallToLua(UObject* WorldContextObject, FString FunctionName,FString StateName);
64:    static int GetIntFromVar(FLuaBPVar Value,int Index=1);
76:    static UObject* GetObjectFromVar(FLuaBPVar Value,int Index=1);

// LuaBlueprintLibrary.cpp
51:FLuaBPVar ULuaBlueprintLibrary::CallToLuaWithArgs(...)
69:    auto fillParam = [&]
70:    {
71:        for (auto& arg : args) {
72:            arg.value.push(ls->getLuaState());
                // ★ Blueprint 传进来的 FLuaBPVar 被原样压回 Lua 栈
73:        }
74:        return args.Num();
75:    };
76:    return f.callWithNArg(fillParam);
...
140:void* FLuaBPVar::checkValue(...)
142:    FLuaBPVar ret;
143:    ret.value.set(L, i);
144:    p->CopyCompleteValue(params, &ret);
        // ★ 反向方向同样是“把栈值直接塞进 FLuaBPVar”

// LuaObject.cpp
2240:        if (LuaWrapper::pushValue(L, p, uss, parms, i))
2241:            return 1;
2244:        if (uss == FLuaBPVar::StaticStruct()) {
2245:            ((FLuaBPVar*)parms)->value.push(L);
2246:            return 1;
                // ★ push 路径硬编码支持 FLuaBPVar
...
2437:        // if it's LuaBPVar
2438:        if (uss == FLuaBPVar::StaticStruct())
2439:            return FLuaBPVar::checkValue(L, p, parms, i);
                // ★ check 路径也硬编码支持
```

[2] slua 的覆写链通过复制 `Super_` 函数和注入 `LuaSuperCall` 维持继承语义

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaOverrider.cpp
// 位置: 1174-1303, 1313-1353, 1381-1449
// 位置说明: Lua 模块发现、self metatable 注入、以及 `Super_` UFunction 复制
// ============================================================================
1174:    bool LuaOverrider::bindOverrideFuncs(const UObjectBase* objBase, UClass* cls) {
1189:        FString luaFilePath = getLuaFilePath(obj, cls, false, bHookInstancedObj);
1194:        NS_SLUA::LuaVar luaModule = sluaState->requireModule(TCHAR_TO_UTF8(*luaFilePath));
1262:            getLuaFunctions(L, funcNames, luaModule);
1276:                UFunction* func = cls->FindFunctionByName(funcName, EIncludeSuperFlag::IncludeSuper);
1281:                    if (hookBpScript(func, cls, (FNativeFuncPtr)&ULuaOverrider::luaOverrideFunc)) {
                            // ★ 先从 Lua 模块枚举函数名，再逐个匹配 UFunction 覆写
...
1318:        luaSelfTable.push(L);
1319:        lua_pushstring(L, SLUA_CPPINST);
1320:        lua_pushlightuserdata(L, objPtr);
1328:        lua_pushstring(L, SUPER_NAME);
1329:        LuaObject::pushType(L, new LuaSuperCall((UObject*)objPtr), "LuaSuperCall", LuaSuperCall::setupMetatable, LuaSuperCall::genericGC);
1335:            lua_pushcclosure(L, classIndex, 1);
                // ★ self 表里同时塞入原生实例指针和 Super 调用对象
...
1398:        // duplicate UFunction for super call
1399:        auto supercallFunc = duplicateUFunction(func, cls, FName(*(SUPER_CALL_FUNC_NAME_PREFIX + func->GetName())), func->GetNativeFunc());
1408:        UFunction* overrideFunc = cls->FindFunctionByName(func->GetFName(), EIncludeSuperFlag::ExcludeSuper);
1417:                overrideFunc->SetNativeFunc(hookFunc);
1419:            overrideFunc->Script.Insert(Code, CodeSize, 0);
1429:            overrideFunc = duplicateUFunction(func, cls, func->GetFName(), (FNativeFuncPtr)&ULuaOverrider::luaOverrideFunc);
                // ★ 一个副本留给 Super，一个副本变成 Lua override 入口
1444:        // BlueprintImplementableEvent type of UFunction can't return correct value with c++ call
1447:            overrideFunc->FunctionFlags |= FUNC_HasOutParms;
                // ★ 还会修正返回值/out param 语义，避免 BlueprintImplementableEvent 返回值异常
```

[3] Angelscript 的 Blueprint 互通仍以“类型化签名绑定”为主，只在 `AnyStruct` 上提供有限泛型容器

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAnyStructParameter.h
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FInstancedStruct.cpp
// 位置: Bind_BlueprintType.cpp:741-755;
//       Bind_BlueprintEvent.cpp:583-632;
//       AngelscriptAnyStructParameter.h:8-15;
//       Bind_FInstancedStruct.cpp:69-103
// 位置说明: Angelscript 的 Blueprint 绑定主轴仍是类型化签名；泛型容器只看到 any-struct
// ============================================================================
// Bind_BlueprintType.cpp
741:        for (auto& DBFunc : DBBind.Methods)
747:            if (Function->HasAnyFunctionFlags(FUNC_BlueprintEvent | FUNC_NetFuncFlags))
748:                BindBlueprintEvent(ClassType.ToSharedRef(), Function, DBFunc);
754:                BindBlueprintCallable(ClassType.ToSharedRef(), Function, DBFunc);
            // ★ Blueprint 入口按事件/可调用函数分流，核心是类型签名绑定

// Bind_BlueprintEvent.cpp
583:    // Don't bind things that have types that are unknown to us
584:    if (!Signature.bAllTypesValid)
585:        return;
586:    if (Signature.ArgumentTypes.Num() > AS_EVENT_MAX_ARGS)
587:        return;
589:    auto* Sig = new FBlueprintEventSignature;
596:    for (int32 i = 0; i < Sig->ArgCount; ++i)
597:        Sig->Arguments[i] = Signature.ArgumentTypes[i];
                // ★ 事件签名被拆成显式参数数组，而不是走统一 Variant 容器

// AngelscriptAnyStructParameter.h
8:USTRUCT(BlueprintType)
9:struct ANGELSCRIPTRUNTIME_API FAngelscriptAnyStructParameter
13:    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Struct Data")
14:    FInstancedStruct InstancedStruct;
        // ★ 本轮检索到的“泛型”容器只覆盖任意 USTRUCT

// Bind_FInstancedStruct.cpp
69:void FAngelscriptInstancedStructHelpers::ImplicitConstructAnyStruct(...)
73:    const UStruct* StructDef = FAngelscriptEngine::Get().GetUnrealStructFromAngelscriptTypeId(TypeId);
87:    Self->InstancedStruct.InitializeAs(ScriptStructDef, (uint8*)Data);
100:        auto FAngelscriptAnyStructParameter_ = FAngelscriptBinds::ExistingClass("FAngelscriptAnyStructParameter");
101:        FAngelscriptAnyStructParameter_.ImplicitConstructor("void f(const ?&in Struct)", FUNC(...));
            // ★ 这是 any-struct 适配，不是 slua 风格的“任意 Lua 值”总线
```

### 设计取舍

- slua 的 `FLuaBPVar` 大幅降低了 Blueprint 侧桥接节点的数量，但代价是类型检查推迟到运行时，错误更晚暴露。
- slua 的 `Super_` UFunction 复制让 Lua 继承和 Blueprint super call 可以共存，但实现复杂度明显高于“只做一层事件转发”。
- Angelscript 的优势是 Blueprint 面保持类型化签名，编辑器和绑定数据库更容易做静态校验；代价是没有 slua 那种“一根总线搬任意 Lua 值”的便利性。

### 与 Angelscript 对比

| 观察点 | sluaunreal | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| Blueprint 泛型值桥 | `FLuaBPVar` + `LuaObject` 双向特判（`LuaBlueprintLibrary.h:21-31`; `LuaBlueprintLibrary.cpp:51-77,140-145`; `LuaObject.cpp:2244-2246,2438-2439`） | 本轮只检索到 `FAngelscriptAnyStructParameter`，仅覆盖 any-struct（`AngelscriptAnyStructParameter.h:8-15`; `Bind_FInstancedStruct.cpp:69-103`） | slua 有实现，Angelscript 没有实现等价的通用 variant 通道 |
| 覆写链保真度 | `duplicateUFunction + LuaSuperCall + metatable`（`LuaOverrider.cpp:1318-1329,1398-1429`） | `BindBlueprintEvent` 以签名桥接为主（`Bind_BlueprintEvent.cpp:589-632`） | 实现方式不同 |
| 错误暴露时机 | 运行时值检查居多 | 绑定期先验证 `bAllTypesValid`、参数上限（`Bind_BlueprintEvent.cpp:583-587`） | 实现质量差异：Angelscript 更偏静态校验 |
| Blueprint 与脚本的抽象边界 | “节点库 + 统一值容器 + override hook” 三层并存 | “类型绑定 + script class / blueprint child” 为主轴 | 实现方式不同 |

### [维度 D8] slua 的性能优化停在“桥接层压缩”，而 Angelscript StaticJIT 继续往“执行计划”推进

已有分析已经指出 slua 的 `CppBinding` 与 profiler 很重，这一轮再往调用路径里看，能把性能结论说得更准确：slua 其实有两层性能策略，而不是一层。

第一层是**静态 wrapper 路径**。像 `FVector` 这种进了 `LuaWrapper*.inc` 的类型，调用时直接落到生成出来的 `lua_CFunction`。第二层是**反射缓存路径**。对 Blueprint/`UFunction` 调用，`LuaObject::push(UFunction*, UClass*)` 会把 `LuaFunctionAccelerator*` 塞进闭包 upvalue；`LuaFunctionAccelerator` 在构造阶段缓存 `FProperty` 描述、out param、latent 信息，但真正调用时仍然每次 `Alloca` 参数区、property list、out param 链，再 `ProcessEvent` 或 native 调用。也就是说，slua 解决的是“不要每次重新扫反射元数据”，但它没有把反射路径变成原始函数入口。

Angelscript 这一侧则明显多了一层。`Bind_BlueprintCallable()` 一旦拿到 `DirectNativePointer`，直接 `BindMethodDirect()`；如果拿不到，才退回 `BlueprintCallableReflectiveFallback`，这条 fallback 路径同样会 `Alloca` 参数缓冲区。再往上，StaticJIT 不只是缓存元数据，而是把 `VMEntry / ParmsEntry / RawFunction` 三种入口一起登记到 `FJITDatabase`，`ScriptCallNative()` 能直接走系统函数调用约定。这和 slua 的优化层级不是一个平面。

```
[D8] Call Overhead Layers
slua direct path
Lua -> generated lua_CFunction                  // 纯静态 wrapper，无 UFunction 反射

slua reflective path
Lua -> ufuncClosure
    -> LuaFunctionAccelerator cache            // 缓存参数元数据
    -> Alloca params/out lists                 // 每次调用仍做栈上临时布局
    -> ProcessEvent / native

Angelscript
Script -> BindMethodDirect                     // 有直连指针就直接绑
Script -> ReflectiveFallback                   // 无直连时才 Alloca + ProcessEvent
Script VM -> StaticJIT {VMEntry,ParmsEntry,RawFunction}
                                              // 继续把执行计划压到 JIT 数据库
```

[1] slua 反射路径的优化边界：缓存元数据，但每次调用仍重建参数布局

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaObject.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaFunctionAccelerator.cpp
// 位置: LuaObject.cpp:793-800,3062-3071;
//       LuaFunctionAccelerator.cpp:33-57,70-79,145-156,181-218,239-279
// 位置说明: UFunction 被包装成闭包；accelerator 只缓存元数据，调用时仍 `Alloca`
// ============================================================================
// LuaObject.cpp
793:        UClass* cls = LuaObject::checkValue<UClass*>(L, 1);
797:        UFunction* func = cls->FindFunctionByName(ANSI_TO_TCHAR(name));
798:        if (func) {
799:            return LuaObject::push(L, func, cls);
                // ★ Blueprint 方法发现后，统一交给 UFunction 闭包
...
3062:    int LuaObject::push(lua_State* L,UFunction* func,UClass* cls)  {
3063:        lua_pushlightuserdata(L, LuaFunctionAccelerator::findOrAdd(func));
3066:            lua_pushcclosure(L, ufuncClosure, 2);
                // ★ 闭包 upvalue 里放的是 accelerator 指针，而不是 raw native pointer

// LuaFunctionAccelerator.cpp
33:    LuaFunctionAccelerator::LuaFunctionAccelerator(UFunction* inFunc)
46:            if (prop->HasAnyPropertyFlags(CPF_OutParm))
47:            {
48:                outParmRecProps.Add(prop);
                    // ★ 预缓存 out 参数元数据
56:                checkerInfo.bReference = IsReferenceParam(prop->PropertyFlags, func) && LuaObject::getReferencer(prop);
57:                paramsChecker.Add(checkerInfo);
                    // ★ 预缓存参数 checker
145:    LuaFunctionAccelerator* LuaFunctionAccelerator::findOrAdd(UFunction* inFunc)
149:        if (ret)
150:        {
151:            return *ret;
                // ★ 热路径避免重复构造 accelerator
...
191:        uint16 propertiesSize = func->PropertiesSize;
192:        uint8* params = (uint8*)FMemory_Alloca(propertiesSize);
194:        FProperty** propertyList = (FProperty**)FMemory_Alloca(paramsPointerSize);
195:        PTRINT* outParams = (PTRINT*)FMemory_Alloca(paramsPointerSize);
217:            auto out = (FOutParmRec*)FMemory_Alloca(sizeof(FOutParmRec));
239:        for (auto& checkerInfo : paramsChecker)
278:                *pointer = PTRINT(checker(L, prop, params + checkerInfo.offset, i, false));
                    // ★ 调用期仍然要重建参数区和 out 链，只是少了“重新扫反射元数据”这一步
```

[2] Angelscript 把“没有直连入口”的路径和“有直连入口”的路径彻底分叉，并继续用 StaticJIT 压低执行成本

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp
// 位置: Bind_BlueprintCallable.cpp:72-90,95-139;
//       BlueprintCallableReflectiveFallback.cpp:290-346;
//       StaticJITHeader.h:82-91;
//       StaticJITHeader.cpp:38-47,169-218
// 位置说明: direct bind / reflective fallback / StaticJIT 三层入口是显式分开的
// ============================================================================
// Bind_BlueprintCallable.cpp
72:    auto* DirectNativePointer = &Entry->FuncPtr;
73:    const bool bHasDirectNativePointer = DirectNativePointer != nullptr && DirectNativePointer->IsBound();
74:    if (!bHasDirectNativePointer)
75:    {
76:        if (!BindBlueprintCallableReflectiveFallback(InType, Function, Signature, *Entry))
77:            return;
            // ★ 没直连入口，才走 reflective fallback
...
120:        int FunctionId = FAngelscriptBinds::BindMethodDirect
133:        int FunctionId = FAngelscriptBinds::BindMethodDirect
            // ★ 一旦有直连入口，直接绑成 script method

// BlueprintCallableReflectiveFallback.cpp
302:    uint8* ParameterBuffer = static_cast<uint8*>(FMemory_Alloca(Function->ParmsSize));
303:    InitializeParameterBuffer(Function, ParameterBuffer);
345:    TargetObject->ProcessEvent(Function, ParameterBuffer);
            // ★ fallback 路径和 slua reflective path 一样，也会做临时参数布局

// StaticJITHeader.h / .cpp
84:    FStaticJITFunction(uint32 FunctionId,
85:        asJITFunction VMEntry,
86:        asJITFunction_ParmsEntry ParmsEntry,
87:        asJITFunction_Raw RawFunction
            // ★ StaticJIT 记录的不只是“函数存在”，而是多种执行入口
38:FStaticJITFunction::FStaticJITFunction(...)
41:    Funcs.VMEntry = InVMEntry;
42:    Funcs.ParmsEntry = InParmsEntry;
43:    Funcs.RawFunction = InRawFunction;
46:    JITDatabase.Functions.Add(FunctionId, Funcs);
169:void FStaticJITFunction::ScriptCallNative(...)
174:    int callConv = sysFunc->callConv;
213:    if (sysFunc->caller.IsBound())
214:    {
217:        void* FunctionArgs[32];
            // ★ 这里已经是 JIT 侧的原生调用桥，而不是“缓存过的反射遍历”
```

### 设计取舍

- slua 的优势是把“能静态化的类型调用”做得很薄，且对反射路径至少做了元数据缓存，工程收益直接。
- 但 slua reflective path 的终点仍是“构造参数布局再调 `UFunction`”，它没有像 StaticJIT 一样继续把执行计划固化成可复用入口。
- Angelscript 的复杂度更高，但收益也更明确：它把 direct bind、reflective fallback、StaticJIT 三条成本曲线分开治理了。

### 与 Angelscript 对比

| 观察点 | sluaunreal | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 直连调用的来源 | `lua-wrapper`/`LuaCppBinding` 生成 `lua_CFunction`（`LuaWrapper5.2.inc:10362-10410`） | `BindMethodDirect` 直接绑定 native pointer（`Bind_BlueprintCallable.cpp:120-137`） | 实现方式不同 |
| 反射路径优化粒度 | 缓存 `FProperty` 元数据，但每次调用仍 `Alloca` 参数区（`LuaFunctionAccelerator.cpp:191-279`） | fallback 也会 `Alloca`，但只有没有直连指针时才使用（`BlueprintCallableReflectiveFallback.cpp:302-345`） | 实现方式不同 |
| VM 执行层优化 | 本轮源码未见等价于 `VMEntry/ParmsEntry/RawFunction` 的执行计划数据库 | StaticJIT 显式登记三种入口（`StaticJITHeader.h:84-91`; `StaticJITHeader.cpp:41-46`） | slua 没有实现对等能力 |
| 性能理念 | 重点压缩 bridge 和反射元数据遍历 | 重点同时覆盖 bind、fallback、VM execution 三层 | 实现质量差异：Angelscript 的优化层级更深 |

### [维度 D4 / D11] slua 的线上热更新产品边界其实是“宿主提供 bytes”，插件自己不管理包协议

前文已经说明 slua 偏运行时热更新，这轮把入口再往上追，能看清它真正的产品边界：**slua 插件不负责定义热更包协议，它只定义“给我 bytes，我来 load”这条 ABI**。

`LuaState.h:166-170,188-189` 已经把这个边界写在接口上了。`doFile()`/`requireModule()` 都依赖 `setLoadFileDelegate()`；`LuaState::loader()` 被插进 `package.searchers[2]`，但它拿到模块名后做的第一件事仍然是 `state->loadFile(fn, filepath)`，即把 bytes 获取责任完全交给 delegate。`Source/democpp/MyGameInstance.cpp:41-63` 的示例进一步坐实了这点：官方 demo 的实现只是去 `Content/Lua/` 查 `.lua`/`.luac` 文件，然后把内容原样交回去。`LuaSimulate.cpp:98-108` 也说明编辑器预览链同样依赖这个 delegate，没有 loader 就根本起不来。

这和 Angelscript 的热重载所有权完全不同。Angelscript 从目录监听、文件变化入队、ReloadRequirement 分级，到 full reload 不可执行时“保留旧代码”，都在插件内部建模；部署侧再加上 `BuildIdentifier` 和 `DataGuid` 做本地制品/二进制一致性校验。slua 则把“从哪里拿脚本、拿什么格式、如何校验”留给宿主项目或外部热更系统。更关键的是，vendored Lua 明明已经有 `onlyluac` 开关，但插件主链没有接线，这意味着**binary-only 只是 VM 能力，不是 slua 默认部署策略**。

```
[D4/D11] Load And Reload Ownership
slua host-owned path
Host Project / Hotfix SDK
├─ setLoadFileDelegate()                         // 宿主决定从哪拿 bytes
├─ loader() inserted into package.searchers[2]  // 插件只接 require 入口
├─ doFile() / requireModule() -> luaL_loadbuffer()
└─ LuaSimulate uses the same delegate            // 编辑器预览也复用同一 ABI

slua VM capability
└─ lua_setonlyluac() exists                      // VM 支持 binary-only
   but plugin runtime does not wire it          // 插件默认不强制只吃字节码

Angelscript plugin-owned path
Editor DirectoryWatcher
├─ queue file changes / deletions
└─ CheckForHotReload()
   ├─ ReloadRequirement::SoftReload
   ├─ FullReloadSuggested
   └─ FullReloadRequired -> keep old code if blocked

Angelscript deployment gate
├─ BuildIdentifier
└─ DataGuid / PrecompiledDataGuid
```

[1] slua 的热更 ABI 就是 `LoadFileDelegate`，模块 bytes 从插件外部注入

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaState.h
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaState.cpp
// 文件: Reference/sluaunreal/Source/democpp/MyGameInstance.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaSimulate.cpp
// 位置: LuaState.h:166-170,188-189;
//       LuaState.cpp:131-155,603-616,651-652,725-770;
//       MyGameInstance.cpp:41-63;
//       LuaSimulate.cpp:98-108
// 位置说明: slua 只定义“如何接 module bytes”，不定义“bytes 从哪来”
// ============================================================================
// LuaState.h
166:        // execute bytes buffer and named buffer to chunk
167:        LuaVar doBuffer(const uint8* buf,uint32 len, const char* chunk, LuaVar* pEnv = nullptr);
168:        // load file and execute it
169:        // file how to loading depend on load delegation
170:        LuaVar doFile(const char* fn, LuaVar* pEnv = nullptr);
188:        // set load delegation function to load lua code
189:        void setLoadFileDelegate(LoadFileDelegate func);
            // ★ 头文件已经把“加载策略属于 delegate”写死

// LuaState.cpp
131:    int LuaState::loader(lua_State* L) {
135:        TArray<uint8> buf = state->loadFile(fn, filepath);
139:            if(luaL_loadbuffer(L,(const char*)buf.GetData(),buf.Num(),chunk)==0) {
                    // ★ loader 不负责拉取内容，只负责把 delegate 给的 bytes 送进 Lua VM
153:    TArray<uint8> LuaState::loadFile(const char* fn,FString& filepath) {
154:        if(loadFileDelegate) return loadFileDelegate(fn,filepath);
155:        return TArray<uint8>();
603:        lua_pushcfunction(L,loader);
606:        lua_getglobal(L,"package");
607:        lua_getfield(L,-1,"searchers");
615:        lua_pushvalue(L,loaderFunc);
616:        lua_rawseti(L,loaderTable,2);
            // ★ slua 只把自己挂到 package.searchers[2]
651:    void LuaState::setLoadFileDelegate(LoadFileDelegate func) {
652:        loadFileDelegate = func;
725:    LuaVar LuaState::doBuffer(const uint8* buf,uint32 len, const char* chunk, LuaVar* pEnv) {
729:        if(luaL_loadbuffer(L, (const char *)buf, len, chunk)) {
755:    LuaVar LuaState::doFile(const char* fn, LuaVar* pEnv) {
757:        TArray<uint8> buf = loadFile(fn, filepath);
762:            LuaVar r = doBuffer(buf.GetData(),buf.Num(),chunk,pEnv );
                // ★ 真正执行链仍是“拿到 bytes -> luaL_loadbuffer”

// MyGameInstance.cpp
41:    state = new NS_SLUA::LuaState("SLuaMainState", this);
42:    state->setLoadFileDelegate([](const char* fn, FString& filepath)->TArray<uint8> {
45:        FString path = FPaths::ProjectContentDir();
47:        path /= "Lua";
48:        path /= filename.Replace(TEXT("."), TEXT("/"));
51:        TArray<FString> luaExts = { UTF8_TO_TCHAR(".lua"), UTF8_TO_TCHAR(".luac") };
55:            FFileHelper::LoadFileToArray(Content, *fullPath);
57:                filepath = fullPath;
58:                return MoveTemp(Content);
                    // ★ 官方 demo 的“热更新实现”只是本地文件搜索，不是插件内建包协议
63:    });

// LuaSimulate.cpp
98:    void LuaSimulate::StartSimulateLua()
100:        if (Delegate == nullptr)
102:            Log::Error("lua Simulation Error. LoadFileDelegate not set.");
106:        SluaState = new NS_SLUA::LuaState("", nullptr);
107:        SluaState->setLoadFileDelegate(Delegate);
108:        SluaState->init();
            // ★ 编辑器模拟链也复用同一个加载 ABI
```

[2] binary-only 只是 vendored Lua 的能力，slua runtime 主链没有把它变成默认部署规则

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/External/lua/lstate.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/External/lua/ldo.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaState.cpp
// 位置: lstate.cpp:236-240; ldo.cpp:760-775; LuaState.cpp:131-145,729-733
// 位置说明: vendored Lua 已支持 onlyluac，但 slua 主链仍直接 `luaL_loadbuffer`
// ============================================================================
// lstate.cpp
236:  L->onlyluac = 0;
239:LUA_API void lua_setonlyluac(lua_State *L, int v) {
240:    L->onlyluac = v;
        // ★ VM 级别已经支持“只接受 binary chunk”

// ldo.cpp
760:static void f_parser (lua_State *L, void *ud) {
763:  if (L->onlyluac == 0) {
768:    } else {
769:      checkmode(L, p->mode, "text");
770:      cl = luaY_parser(...);
772:  } else {
773:    checkmode(L, p->mode, "binary");
774:    cl = luaU_undump(...);
        // ★ onlyluac 打开后，Lua VM 会强制 binary-only

// LuaState.cpp
139:            if(luaL_loadbuffer(L,(const char*)buf.GetData(),buf.Num(),chunk)==0) {
729:        if(luaL_loadbuffer(L, (const char *)buf, len, chunk)) {
730:            const char* err = lua_tostring(L,-1);
731:            Log::Error("DoBuffer failed: %s",err);
                // ★ 但 plugin runtime 主链没有先调用 lua_setonlyluac()
```

[3] Angelscript 把热重载和部署一致性都留在插件内部治理

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp
// 位置: AngelscriptEditorModule.cpp:78-93,366-380;
//       AngelscriptEngine.cpp:2729-2758,3938-4001,1550-1555;
//       PrecompiledData.cpp:2627-2645;
//       StaticJITHeader.cpp:25-30
// 位置说明: 目录监听、reload requirement、build gate、DataGuid gate 全在插件内部
// ============================================================================
// AngelscriptEditorModule.cpp
78:void OnScriptFileChanges(const TArray<FFileChangeData>& Changes)
84:    FAngelscriptEngine& AngelscriptManager = FAngelscriptEngine::Get();
85:    AngelscriptEditor::Private::QueueScriptFileChanges(...)
        // ★ 先把磁盘变化排队，不直接执行脚本装载
...
366:    // Register a directory watch on the script directory so we know when to reload
367:    FDirectoryWatcherModule& DirectoryWatcherModule = ...
376:            DirectoryWatcher->RegisterDirectoryChangedCallback_Handle(
378:                IDirectoryWatcher::FDirectoryChanged::CreateStatic(&OnScriptFileChanges),
                // ★ 监听器是插件自己的，而不是宿主工程自定义 delegate

// AngelscriptEngine.cpp
2729:void FAngelscriptEngine::CheckForHotReload(ECompileType CompileType)
2746:    FileList.Append(FileChangesDetectedForReload);
2753:        for (const auto& DeletedFile : FileDeletionsDetectedForReload)
            // ★ reload 入口消耗的是内部队列
...
3938:                case FAngelscriptClassGenerator::EReloadRequirement::SoftReload:
3942:                case FAngelscriptClassGenerator::EReloadRequirement::FullReloadSuggested:
3972:                case FAngelscriptClassGenerator::EReloadRequirement::FullReloadRequired:
3975:                        FString Msg =
3976:                            TEXT("Full Reload is required due to UPROPERTY() or UFUNCTION() changes, but cannot")
3977:                                TEXT(" perform a full reload right now. Keeping old angelscript code active.");
3990:                        bShouldSwapInModules = false;
3991:                        bFullReloadRequired = true;
                            // ★ 连“无法 reload 时保留旧代码”也是插件内部规则
...
1550:                const FStaticJITCompiledInfo* CompiledInfo = FStaticJITCompiledInfo::Get();
1552:                if (CompiledInfo != nullptr && CompiledInfo->PrecompiledDataGuid != PrecompiledData->DataGuid)
1554:                    UE_LOG(Angelscript, Warning, TEXT("Loaded angelscript precompiled data does not match ..."));
                            // ★ 运行时还会校验二进制内嵌 JIT 和磁盘 cache 是否同一份数据

// PrecompiledData.cpp / StaticJITHeader.cpp
2627:int32 FAngelscriptPrecompiledData::GetCurrentBuildIdentifier()
2642:bool FAngelscriptPrecompiledData::IsValidForCurrentBuild()
2644:    return BuildIdentifier == GetCurrentBuildIdentifier() && BuildIdentifier != -1;
        // ★ build config 不一致就判 invalid
25:FStaticJITCompiledInfo::FStaticJITCompiledInfo(FGuid Guid)
26:    : PrecompiledDataGuid(Guid)
29:    checkf(ActiveInfo == nullptr, TEXT("Only one angelscript static JIT info can be compiled in!"))
        // ★ DataGuid 还参与编译期唯一性约束
```

### 设计取舍

- slua 的强项是边界清晰：插件只关心 VM 和桥接，脚本分发、校验、落盘策略全部留给宿主项目或业务热更系统。
- 代价是插件本身不提供统一的包协议、安全策略和版本淘汰机制；不同项目会自行长出不同实现。
- Angelscript 的方向刚好相反：把文件监控、reload 事务和制品一致性都内收到插件，换来更强的确定性，但系统重量更高。

### 与 Angelscript 对比

| 观察点 | sluaunreal | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 热更入口所有权 | `LoadFileDelegate` 由宿主注入（`LuaState.h:188-189`; `MyGameInstance.cpp:42-63`） | 目录监听和 reload 队列由插件内部维护（`AngelscriptEditorModule.cpp:78-93,366-380`; `AngelscriptEngine.cpp:2729-2758`） | 实现方式不同 |
| 编辑器预览 / 开发态复用 | `LuaSimulate` 也依赖同一 delegate（`LuaSimulate.cpp:100-108`） | 统一走 `CheckForHotReload` / reload requirement 事务（`AngelscriptEngine.cpp:3938-4001`） | 实现方式不同 |
| binary-only 执行约束 | VM 支持 `onlyluac`，但 runtime 主链未接线（`lstate.cpp:239-240`; `ldo.cpp:763-775`; `LuaState.cpp:139,729-733`） | `BuildIdentifier` + `DataGuid`/`PrecompiledDataGuid` 已经进入正式执行链（`PrecompiledData.cpp:2627-2645`; `AngelscriptEngine.cpp:1550-1555`; `StaticJITHeader.cpp:25-30`） | slua 没有实现对等的部署一致性治理 |
| 失败恢复策略 | 主要取决于宿主业务的 loader / 分发层实现 | 插件自身显式区分 Soft/Full/Keep old code（`AngelscriptEngine.cpp:3938-4001`） | 实现质量差异：Angelscript 更产品化 |

---

## 深化分析 (2026-04-09 07:14:29)

本轮不重复前面已经写过的 D3 / D4 结论，只补三条新的纵深证据链：`D2 生成物治理`、`D8 profiler 数据通路`、`D11 部署边界`。三条线共同说明，sluaunreal 的核心取舍不是“功能有没有”，而是把哪些成本前移到仓库资产、哪些成本后移给宿主项目。

## [维度 D2] 反射绑定机制补充：slua 的静态导出产物是“按 UE minor 版本固化提交”的仓库资产

前文已经说明 slua 是 `LuaWrapper*.inc + LuaCppBinding + extension map + accelerator` 的混合体。本轮补的是一个更关键的维护事实：它的“静态导出”不是像 Angelscript 一样在当前构建里重新生成、清理旧产物，而是把不同 UE 小版本对应的巨型 wrapper 文件直接提交到仓库，再由预处理器在编译期选中其中一份。

实际文件统计可以直接看出这一点：`LuaWrapper5.1.inc` 为 `15328` 行，`LuaWrapper5.2.inc` 为 `15823` 行，`LuaWrapper5.4.inc` 为 `15929` 行。也就是说，slua 把“引擎版本差异”封装成多份长期存在的静态资产，而不是一次性生成物。这样做的好处是 runtime 初始化很直接，坏处是 UE minor 升级时必须同时维护多份 blob。

更细一点看，slua 的 `UObject` 路径也不是“生成后完全静态”。`REG_EXTENSION_METHOD` 只是把 `lua_CFunction` 存进 `extensionMMap`，真正访问时 `searchExtensionMethod()` 仍要沿 `UClass` 超类链查找字段。这意味着 slua 的“静态”主要落在 wrapper 函数体和基础类型元方法，不是把 `UObject` 的最终分派成本完全提前消掉。

```
[D2] Binding Artifact Lifecycle
sluaunreal
├─ Tools/config.json                               // 外部工具决定输出目录
├─ LuaWrapper5.1/5.2/5.4.inc                       // 各 UE 小版本各自提交一份 blob
├─ LuaWrapper.cpp selects one include              // 编译期选中一个版本
├─ LuaObject::init -> LuaWrapper::initExt          // 运行时一次性注册 wrapper
└─ searchExtensionMethod walks UClass chain        // UObject 访问仍做动态查找

Angelscript
├─ UHT exporter enumerates entries                 // 构建期枚举 BlueprintCallable/UHT 数据
├─ Build AS_FunctionTable_<Module>_<Shard>.cpp     // 按 shard 生成
├─ DeleteStaleOutputs()                            // 清理旧 shard
└─ Runtime chooses Direct or Fallback              // 执行期再决定直连或反射回退
```

[1] slua 的 wrapper 选择与注册链是“编译期选版本 + 启动期一次注册”

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaWrapper.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaObject.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaState.cpp
// 位置: LuaWrapper.cpp:55-67,184-188; LuaObject.cpp:3054; LuaState.cpp:619-625
// 位置说明: wrapper 不是运行时动态生成，而是编译时选版本、启动时注册
// ============================================================================
// LuaWrapper.cpp
55:#if ((ENGINE_MINOR_VERSION<25) && (ENGINE_MAJOR_VERSION==4))
56:    #include "LuaWrapper4.18.inc"
57:#elif ((ENGINE_MINOR_VERSION>=25) && (ENGINE_MAJOR_VERSION==4))
58:    #include "LuaWrapper4.25.inc"
59:#elif ((ENGINE_MINOR_VERSION==1) && (ENGINE_MAJOR_VERSION==5))
60:    #include "LuaWrapper5.1.inc"
61:#elif ((ENGINE_MINOR_VERSION==2) && (ENGINE_MAJOR_VERSION==5))
62:    #include "LuaWrapper5.2.inc"
63:#elif ((ENGINE_MINOR_VERSION==3) && (ENGINE_MAJOR_VERSION==5))
64:    #include "LuaWrapper5.3.inc"
65:#elif ((ENGINE_MINOR_VERSION==4) && (ENGINE_MAJOR_VERSION==5))
66:    #include "LuaWrapper5.4.inc"
67:#endif
    // ★ 编译时直接选中某一份已提交的 wrapper blob

184:    void LuaWrapper::initExt(lua_State* L)
185:    {
186:        init(L);
187:        FSoftObjectPtrWrapper::bind(L);
188:    }
    // ★ 初始化阶段只做注册，不做“现算现生”

// LuaObject.cpp
3054:        LuaWrapper::initExt(L);
    // ★ LuaObject 初始化时把 wrapper 接进来

// LuaState.cpp
619:        InitExtLib(L);
621:        LuaObject::init(L);
622:        LuaProtobuf::init(L);
623:        SluaUtil::openLib(L);
624:        LuaClass::reg(L);
625:        LuaArray::reg(L);
    // ★ Lua VM 启动时顺序注册对象系统、protobuf、通用库和静态 class
```

[2] 生成出来的 wrapper 确实是“可直接执行的绑定体”，但 `UObject` 扩展仍落到运行时 map 查找

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaWrapper5.2.inc
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaCppBinding.h
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaObject.cpp
// 位置: LuaWrapper5.2.inc:1603-1624;
//       LuaCppBinding.h:487-511;
//       LuaObject.cpp:741-776
// 位置说明: 基础值类型的 wrapper 已经展开成具体函数；UObject 扩展仍需动态分派
// ============================================================================
// LuaWrapper5.2.inc
1603:            LuaObject::newType(L, "FIntPoint");
1604:            LuaObject::addOperator(L, "__eq", __eq);
1613:            LuaObject::addField(L, "X", get_X, set_X, true);
1614:            LuaObject::addField(L, "Y", get_Y, set_Y, true);
1617:            LuaObject::addMethod(L, "ComponentMin", ComponentMin, true);
1618:            LuaObject::addMethod(L, "ComponentMax", ComponentMax, true);
1619:            LuaObject::addMethod(L, "ToString", ToString, true);
1624:            LuaObject::finishType(L, "FIntPoint", __ctor, __gc);
    // ★ 这里已经是展开后的最终注册语句，不再依赖 UFunction 反射解释

// LuaCppBinding.h
487:    #define REG_EXTENSION_METHOD(U,N,M) { \
488:        using BindType = LuaCppBinding<decltype(M),M>; \
489:        LuaObject::addExtensionMethod(U::StaticClass(),N,BindType::LuaCFunction, BindType::IsStatic); }
508:    #define REG_EXTENSION_PROPERTY(U,N,GETTER,SETTER) { \
509:        using GetType = LuaCppBinding<decltype(GETTER),GETTER>; \
510:        using SetType = LuaCppBinding<decltype(SETTER),SETTER>; \
511:        LuaObject::addExtensionProperty(U::StaticClass(),N,GetType::LuaCFunction,SetType::LuaCFunction,GetType::IsStatic); }
    // ★ UObject 成员不是直接塞进 metatable，而是先进入 extension map

// LuaObject.cpp
741:    int searchExtensionMethod(lua_State* L,UClass* cls,const char* name,bool isStatic=false) {
744:        while(cls!=nullptr) {
745:            TMap<FString,ExtensionField>* mapptr = isStatic?extensionMMap_static.Find(cls):extensionMMap.Find(cls);
748:                auto fieldptr = mapptr->Find(name);
751:                    if (fieldptr->isFunction) {
752:                        lua_pushcfunction(L, fieldptr->func);
753:                        ret = 1;
754:                        break;
769:            cls=cls->GetSuperClass();
772:        if (ret)
773:        {
774:            cacheFunction(L, isStatic ? cls : nullptr);
775:        }
    // ★ 最终访问仍然沿 super chain 查 map；slua 只是把查找结果做了缓存
```

[3] Angelscript 的 function table 生成链是“按构建重算 + 自动删旧文件”，产物生命周期更短

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs
// 函数: BuildModule / DeleteStaleOutputs
// 位置: 115-139,432-445
// 位置说明: 构建期按 shard 生成，并在同一步删除陈旧输出
// ============================================================================
115:		int shardCount = (entries.Count + MaxEntriesPerShard - 1) / MaxEntriesPerShard;
116:		for (int shardIndex = 0; shardIndex < shardCount; shardIndex++)
120:			string outputPath = factory.MakePath($"AS_FunctionTable_{module.ShortName}_{shardIndex:D3}", ".cpp");
121:			factory.CommitOutput(outputPath, BuildShard(module.ShortName, editorOnly, includes, entries, startIndex, entryCount, shardIndex, shardCount));
122:			generatedPaths.Add(outputPath);
133:					entry.EraseMacro == "ERASE_NO_FUNCTION()" ? "Stub" : "Direct",
139:		return new AngelscriptModuleGenerationSummary(module.ShortName, editorOnly, entries.Count, directBindEntries, stubEntries, generatedShardCount);
    // ★ 生成物天然带有 shard 粒度和 direct/stub 统计

432:	private static void DeleteStaleOutputs(IUhtExportFactory factory, HashSet<string> generatedPaths)
434:		string outputDirectory = Path.GetDirectoryName(factory.MakePath("AS_FunctionTable_Stale", ".cpp"))!;
440:		foreach (string existingFile in Directory.EnumerateFiles(outputDirectory, "AS_FunctionTable_*.cpp"))
442:			if (!generatedPaths.Contains(existingFile))
444:				File.Delete(existingFile);
    // ★ 本轮未生成的 shard 会被删除，避免仓库里长期堆积旧版本输出
```

### 设计取舍

- slua 的优点是 runtime 启动简单，基础类型 wrapper 可以非常薄，热路径上确实少了一层反射解释。
- 代价是“引擎版本漂移”直接体现在仓库资产上，升级 UE minor 时要维护多份 `LuaWrapper*.inc`，而不是只维护生成器。
- Angelscript 的优点是产物生命周期更短、陈旧文件可自动清理，构建反馈里还保留 direct/stub 统计；代价是更依赖 UHT/exporter 链的稳定性。

### 与 Angelscript 对比

| 观察点 | sluaunreal | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 静态导出产物形态 | 多份 `LuaWrapper*.inc` 长期提交在仓库中，并由 `LuaWrapper.cpp` 按 UE minor 选择（`LuaWrapper.cpp:55-67`） | `AS_FunctionTable_*.cpp` 在当前构建中分片生成（`AngelscriptFunctionTableCodeGenerator.cs:115-139`） | 实现方式不同 |
| 陈旧产物治理 | 本轮源码未见对 `LuaWrapper*.inc` 的自动删旧逻辑 | `DeleteStaleOutputs()` 会删除未再生成的旧 shard（`AngelscriptFunctionTableCodeGenerator.cs:432-445`） | 实现质量差异：Angelscript 的生成物治理更收敛 |
| `UObject` 扩展分派 | `REG_EXTENSION_METHOD` 进 map，访问时 `searchExtensionMethod()` 仍沿 `UClass` 链查找（`LuaCppBinding.h:487-511`; `LuaObject.cpp:741-776`） | `Bind_BlueprintCallable` 先尝试 direct native pointer，失败才切 fallback（`Bind_BlueprintCallable.cpp:72-139`） | 实现方式不同 |
| UE minor 升级成本 | wrapper blob 与引擎小版本显式耦合；本轮实测 `5.1/5.2/5.4` 三份文件都在万行级 | 主要维护 exporter 与运行时 binder，具体输出文件随构建重算 | 实现质量差异：Angelscript 的版本维护成本更集中在工具链 |

## [维度 D8] 性能与优化补充：slua 的 profiler 是一条完整的运行时遥测产品线，不只是一个 Editor 面板

本轮补到的关键点是：slua 的 profiler 不是“显示已有统计数据”，而是一整条从 Lua VM hook、到本地归档、到远程 TCP、再到 Editor 面板的独立链路。`ENABLE_PROFILER` 同时出现在 `slua_unreal` 和 `slua_profile` 两个模块里，runtime 模块启动时就先拉起 `SluaProfilerDataManager`，Lua VM 初始化后再在非 Shipping 构建里接入 `LuaProfiler::init()`。

真正的采样发生在 `lua_sethook()`。`changeHookState()` 会先抓一份整帧内存快照，再把 `LUA_MASKRET | LUA_MASKCALL` 挂到主线程和 coroutine 上；`debug_hook()` 不只记 `call/return`，还专门补偿 coroutine `yield/resume` 之间的时间段。每帧 tick 时，如果已经连上远端，就走 socket 发送；如果没连远端，就把事件塞给 `SluaProfilerDataManager`，后者最终写成 `.sluastat` 并压缩。

这条链和 Angelscript 的思路不同。Angelscript 当前更偏“降低执行开销 + 输出状态/覆盖率”，而不是“内建一条脚本级 remote sampling pipeline”。它有 `FCpuProfilerTraceScoped` 这种脚本可用的 UE trace primitive，也有 `StaticJIT` 和 `CodeCoverage` 的离线导出，但没有看到与 slua 对等的脚本调用采样器、TCP profile server 和 `.sluastat` 类产物。

```
[D8] Profiling And Optimization Pipeline
sluaunreal
├─ Lua VM hook state                               // lua_sethook(call/return)
├─ coroutine enter/exit compensation               // 补齐 yield/resume 区间
├─ per-frame memory delta                          // 记录 memory tick / increase
├─ TCP stream when connected                       // 远端在线时走 socket
└─ local .sluastat archive when offline            // 未连接时写本地压缩档案

Angelscript
├─ FCpuProfilerTraceScoped                         // 暴露 UE trace scope
├─ StaticJIT registers VM/Parms/Raw entries        // 重点是执行计划数据库
├─ DumpAll -> JITDatabase / StaticJIT / Coverage   // 导出状态而不是采样调用流
└─ Automation hooks drive coverage reports         // 测试结束后生成 HTML/JSON
```

[1] slua 的 profiler 在 VM 级别直接 hook `call/return`，并把 coroutine 也纳入时间线

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaProfiler.cpp
// 函数: debug_hook / changeHookState / tick path / RAII helper
// 位置: 256-347,490-499,527-534
// 位置说明: profiler 直接接在 Lua VM hook 上，而不是事后读统计表
// ============================================================================
256:        void debug_hook(lua_State* L, lua_Debug* ar) {
260:            lua_getinfo(L, "nSl", ar);
263:            if (ar->event > 1)
264:                return;
268:            int event = ar->event;
285:                        event += PHE_ENTER_COROUTINE;
288:                        lua_sethook(co, debug_hook, LUA_MASKRET | LUA_MASKCALL, 0);
314:            if (co_debug && co && event == PHE_EXIT_COROUTINE)
317:                takeSample(PHE_RETURN, co_debug->linedefined, ar->name ? ar->name : "", co_debug->short_src, start, co);
319:            takeSample(event, ar->linedefined, ar->name ? ar->name : "", ar->short_src, start, L);
320:            if (co_debug && co && event == PHE_ENTER_COROUTINE)
323:                takeSample(PHE_CALL, co_debug->linedefined, ar->name ? ar->name : "", co_debug->short_src, start, co);
    // ★ coroutine 会额外补一对 CALL/RETURN，保证 yield 两侧时间能落到树上

328:        int changeHookState(lua_State* L) {
332:            if (state == HookState::UNHOOK) {
333:                lua_sethook(L, nullptr, 0, 0);
335:            else if (state == HookState::HOOKED) {
337:                LuaMemoryProfile::onStart(LS);
345:                takeMemorySample(PHE_MEMORY_TICK, memoryInfoList, L);
347:                lua_sethook(L, debug_hook, LUA_MASKRET | LUA_MASKCALL, 0);
    // ★ 开启时先采一帧内存全量，再进入 call/return hook

490:        RunState currentRunState = (RunState)profiler.getFromTable<int>("currentRunState");
491:        if (currentRunState == RunState::CONNECTED) {
492:            if(checkSocketRead()) memoryGC(L);
493:            takeMemorySample(PHE_MEMORY_INCREACE, LuaMemoryProfile::memIncreaceThisFrame(LS), L);
494:            takeSample(PHE_TICK, -1, "", "", getTime(), L);
498:            SluaProfilerDataManager::ReceiveMemoryData(PHE_MEMORY_INCREACE, LuaMemoryProfile::memIncreaceThisFrame(LS));
499:            SluaProfilerDataManager::ReceiveProfileData(PHE_TICK, getTime() - profileTotalCost, -1, "", "");
    // ★ 连着远端就发 socket；没连远端也不会丢，而是落到本地 manager

527:    LuaProfiler::LuaProfiler(const char* funcName)
529:        takeSample(PHE_CALL, 0, funcName, "", getTime(), *LuaState::get());
532:    LuaProfiler::~LuaProfiler()
534:        takeSample(PHE_RETURN, 0, "", "", getTime(), *LuaState::get());
    // ★ 还提供 RAII helper，便于 C++ 手工给一段代码补 profiler 区间
```

[2] slua 的本地录制和远程查看是独立产品线：`.sluastat` 压缩档 + TCP server + Editor tab

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/SluaProfilerDataManager.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_profile/Private/slua_remote_profile.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_profile/Private/slua_profile.cpp
// 位置: SluaProfilerDataManager.cpp:329-353,393-447,644-651,927-947;
//       slua_remote_profile.cpp:56-60,139-186,369-377;
//       slua_profile.cpp:72-76,189-231
// 位置说明: profiler 数据既可以本地压缩落盘，也可以走 TCP + Editor 面板
// ============================================================================
// SluaProfilerDataManager.cpp
329:void FProfileDataProcessRunnable::StartRecord()
344:    FString filePath = GenerateStatFilePath();
345:    frameArchive = IFileManager::Get().CreateFileWriter(*filePath);
347:    *frameArchive << ProfileVersion;
    // ★ 本地录制会直接创建 profiling 档案

401:    if (hookEvent == NS_SLUA::ProfilerHookEvent::PHE_CALL)
408:        SluaProfilerDataManager::WatchBegin(shortSrc, lineDefined, funcName, time, funcProfilerRoot, profilerStack);
419:    else if (hookEvent == NS_SLUA::ProfilerHookEvent::PHE_TICK)
421:        funcProfilerNodeQueue.Enqueue(funcProfilerRoot);
422:        memoryQueue.Enqueue(currentMemory);
    // ★ CPU 事件和内存事件都被重新组织成树状/帧状结构

644:FString FProfileDataProcessRunnable::GenerateStatFilePath()
647:    FString filePath = FPaths::ProfilingDir() + "/Sluastats/"
650:        + FString::FromInt(now.GetMillisecond()) + ".sluastat";
    // ★ 录制结果是独立的 .sluastat 档案，不依赖 UE trace capture

927:void FProfileDataProcessRunnable::SerializeCompreesedDataToFile(FArchive& ar)
935:    int32 compressedSize = FCompression::CompressMemoryBound(NAME_Oodle, uncompressedSize);
943:    FCompression::CompressMemory(NAME_Oodle, compressedBuffer, compressedSize, dataToCompress.GetData(), uncompressedSize);
945:    ar << uncompressedSize;
946:    ar << compressedSize;
947:    ar.Serialize(compressedBuffer, compressedSize);
    // ★ 数据落盘前还会压缩，UE 新版本默认走 Oodle

// slua_remote_profile.cpp
56:        ListenEndpoint.Address = FIPv4Address(0, 0, 0, 0);
57:        ListenEndpoint.Port = (std::numeric_limits<uint16>::min() < Port) && (Port < std::numeric_limits<uint16>::max()) ? Port : 8081;
58:        Listener = new FTcpListener(ListenEndpoint);
59:        Listener->OnConnectionAccepted().BindRaw(this, &FProfileServer::HandleConnectionAccepted);
    // ★ Editor 侧 profiler 自己就是一个 TCP server

139:    bool FProfileServer::HandleConnectionAccepted(FSocket* ClientSocket, const FIPv4Endpoint& ClientEndpoint)
141:        PendingConnections.Enqueue(MakeShareable(new FProfileConnection(ClientSocket, ClientEndpoint)));
146:    FProfileConnection::FProfileConnection(FSocket* InSocket, const FIPv4Endpoint& InRemoteEndpoint)
158:        Socket->SetReceiveBufferSize(2 * 1024 * 1024, NewSize);
159:        Socket->SetSendBufferSize(2 * 1024 * 1024, NewSize);
186:        Thread = FRunnableThread::Create(this, *FString::Printf(TEXT("FProfileConnection %s"), *RemoteEndpoint.ToString()), 128 * 1024, TPri_Normal);
    // ★ 每个远端连接都有自己的 socket 和线程

369:        MessageReader << Event;
372:        case NS_SLUA::ProfilerHookEvent::PHE_MEMORY_TICK:
373:            MessageReader << memoryInfoList;
375:        case NS_SLUA::ProfilerHookEvent::PHE_MEMORY_INCREACE:
376:            MessageReader << memoryIncrease;
    // ★ 远端消息协议至少把 CPU/Memory 事件分成不同 hookEvent

// slua_profile.cpp
72:        sluaProfilerInspector = MakeShareable(new SProfilerInspector);
73:        FGlobalTabmanager::Get()->RegisterNomadTabSpawner(slua_profileTabName,
75:            .SetDisplayName(LOCTEXT("Flua_wrapperTabTitle", "slua Profiler"))
    // ★ 最终以 Editor Nomad tab 的形式展示

197:    if (event == NS_SLUA::ProfilerHookEvent::PHE_CALL)
205:        SluaProfilerDataManager::WatchBegin(short_src, linedefined, name, nanoseconds, funcProfilerRoot, profilerStack);
224:    else if (event == NS_SLUA::ProfilerHookEvent::PHE_MEMORY_TICK)
226:        currentMemory->memoryInfoList = Message->memoryInfoList;
229:    else if (event == NS_SLUA::ProfilerHookEvent::PHE_MEMORY_INCREACE)
231:        currentMemory->memoryIncrease = Message->memoryIncrease;
    // ★ Editor 面板并不是直接读 socket 字节，而是复用 runtime 那套树构建逻辑
```

[3] Angelscript 在“性能可观测性”上更偏执行计划数据库、测试覆盖率和 UE trace scope

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/FCpuProfilerTraceScoped.h
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FCpuProfilerTraceScoped.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/AngelscriptCodeCoverage.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp
// 位置: FCpuProfilerTraceScoped.h:14-23;
//       Bind_FCpuProfilerTraceScoped.cpp:4-13;
//       StaticJITHeader.h:82-91;
//       StaticJITHeader.cpp:38-46;
//       AngelscriptCodeCoverage.cpp:22-64;
//       AngelscriptStateDump.cpp:177-184,1017-1035
// 位置说明: Angelscript 的重点是把执行入口、状态和测试覆盖率做成可导出的结构
// ============================================================================
// FCpuProfilerTraceScoped.h / Bind_FCpuProfilerTraceScoped.cpp
14:#if CPUPROFILERTRACE_ENABLED
15:	FCpuProfilerTraceScoped(const FName& EventID)
17:		FCpuProfilerTrace::OutputBeginDynamicEvent(EventID);
20:	~FCpuProfilerTraceScoped()
22:		FCpuProfilerTrace::OutputEndEvent();
    // ★ 这是 UE trace scope primitive，不是脚本调用采样器

4:AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_TraceCPUProfilerEventScoped(FAngelscriptBinds::EOrder::Late, []
8:	FCpuProfilerTraceScoped_.Constructor("void f(const FName& EventID)", [](FCpuProfilerTraceScoped* Address, const FName& EventID)
10:		new(Address) FCpuProfilerTraceScoped(EventID);
    // ★ Angelscript 把 trace scope 暴露给脚本使用

// StaticJITHeader.h / .cpp
84:	FStaticJITFunction(uint32 FunctionId,
85:		asJITFunction VMEntry,
86:		asJITFunction_ParmsEntry ParmsEntry,
87:		asJITFunction_Raw RawFunction
40:	FJITDatabase::FJITFunctions Funcs;
41:	Funcs.VMEntry = InVMEntry;
42:	Funcs.ParmsEntry = InParmsEntry;
43:	Funcs.RawFunction = InRawFunction;
46:	JITDatabase.Functions.Add(FunctionId, Funcs);
    // ★ 性能侧更强调“把调用入口做成数据库”，而不是对每次脚本调用做取样

// AngelscriptCodeCoverage.cpp
22:void FAngelscriptCodeCoverage::AddTestFrameworkHooks()
27:	AutomationController->OnTestsAvailable().AddRaw(this, &FAngelscriptCodeCoverage::OnTestsStarting);
28:	AutomationController->OnTestsComplete().AddRaw(this, &FAngelscriptCodeCoverage::OnTestsStopping);
40:	FString OutputDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("CodeCoverage"));
63:	WriteReportHtml(OutputDir);
64:	WriteCoverageSummaries(OutputDir);
    // ★ 测试结束后输出 HTML/summary，属于离线可观测性

// AngelscriptStateDump.cpp
177:	TableResults.Add(DumpEngineSettings(Engine, ResolvedOutputDir));
179:	TableResults.Add(DumpJITDatabase(Engine, ResolvedOutputDir));
181:	TableResults.Add(DumpStaticJITState(Engine, ResolvedOutputDir));
184:	TableResults.Add(DumpCodeCoverage(Engine, ResolvedOutputDir));
1019:	const FJITDatabase& JITDatabase = FJITDatabase::Get();
1028:	Writer.AddRow({ TEXT("Functions"), LexToString(JITDatabase.Functions.Num()), FString() });
1033:	Writer.AddRow({ TEXT("PropertyOffsetLookups"), LexToString(JITDatabase.PropertyOffsetLookups.Num()), FString() });
    // ★ DumpAll 能直接把 JIT database 和 coverage 状态导出来做离线分析
```

### 设计取舍

- slua 的 profiler 优势是产品完整，运行时、远端传输、本地落盘和 Editor 浏览器已经接成闭环。
- 代价也很明确：它依赖 `lua_sethook` 的逐调用采样，运行时插桩侵入性高，所以默认被包在 `ENABLE_PROFILER && !UE_BUILD_SHIPPING` 之内。
- Angelscript 的重心更偏“降低执行成本、导出状态和覆盖率”，因此适合持续验证和性能工程，但不等价于 slua 这种脚本调用时间线 profiler。

### 与 Angelscript 对比

| 观察点 | sluaunreal | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 脚本级远程 profiler 通路 | `lua_sethook` + TCP server + `SProfilerInspector` + `.sluastat`（`LuaProfiler.cpp:256-347,490-499`; `slua_remote_profile.cpp:56-60,139-186`; `slua_profile.cpp:72-76`） | 当前源码可见的是 `FCpuProfilerTraceScoped`、`StaticJIT` dump、`CodeCoverage`，未见对等的脚本级 remote sampling pipeline（`FCpuProfilerTraceScoped.h:14-23`; `AngelscriptStateDump.cpp:177-184,1017-1035`; `AngelscriptCodeCoverage.cpp:22-64`; `Angelscript.uplugin:18-33`） | Angelscript 没有实现对等能力 |
| 运行时采样粒度 | 逐 `call/return` + coroutine enter/exit + memory delta（`LuaProfiler.cpp:256-347`） | 不做逐脚本调用采样；更强调 `VMEntry/ParmsEntry/RawFunction` 执行入口登记（`StaticJITHeader.h:82-91`; `StaticJITHeader.cpp:38-46`） | 实现方式不同 |
| 离线分析产物 | `.sluastat` 二进制压缩档案（`SluaProfilerDataManager.cpp:644-651,927-947`） | `JITDatabase.csv`、`StaticJITState.csv`、`CodeCoverage` HTML/CSV（`AngelscriptStateDump.cpp:177-184,1017-1035`; `AngelscriptCodeCoverage.cpp:40-64`） | 实现方式不同 |
| Shipping 边界 | profiler 编译开关明确关闭 Shipping 热路径（`LuaState.cpp:628-630`; `slua_unreal.Build.cs:106-112`; `slua_profile.Build.cs:35-37`） | 性能策略更多体现在常驻执行路径优化和测试态 coverage 开关（`StaticJITHeader.cpp:38-46`; `AngelscriptCodeCoverage.cpp:45-49`） | 实现方式不同 |

## [维度 D11] 部署与打包补充：slua 的官方插件负责打包 native Lua runtime，不负责治理 script 制品

前文已经写过 `LoadFileDelegate` 把热更入口留给宿主。本轮补充的是“部署单位”本身。slua 实际上把部署拆成了两层：第一层是插件自己携带的多平台 Lua 静态库；第二层是宿主项目自己提供的脚本 bytes。前者由插件仓库和构建脚本负责，后者则几乎不被插件约束。

这和很多人直觉里的“Lua 插件会自带脚本打包/加密/签名流程”不一样。按当前官方加载链，slua 只做三件事：接受 `LoadFileDelegate` 返回的 bytes、可同时接受 `.lua` 和 `.luac`、然后直接 `luaL_loadbuffer()`。本轮没有在正式加载链上看到解密、签名校验、版本校验或包协议解析。需要特别区分的是：这不代表业务项目不能自己在 delegate 里做这些事，只代表 slua 插件本身没有内建这套治理。

相对地，Angelscript 的“部署治理”不在 native runtime 复制，而在 script 制品一致性。它把 third-party engine 源码编进插件模块，同时对 precompiled script 数据附加 `DataGuid` 和 `BuildIdentifier`，运行时如果二进制内嵌的 JIT 信息与磁盘 precompiled data 不匹配，会直接放弃使用 transpiled code，并且还能把这些身份信息导出到 dump 表里。

```
[D11] Deployment Boundary
sluaunreal
├─ make_*.sh/.bat -> Library/<Platform>/liblua.a|lua.lib   // 插件自带多平台 Lua runtime
├─ slua_unreal.Build.cs links prebuilt libraries           // UBT 直接挑选平台库
├─ Host LoadFileDelegate returns script bytes              // 宿主决定脚本从哪来
└─ LuaState::loadFile -> luaL_loadbuffer                   // 官方链上无内建解密/签名 gate

Angelscript
├─ Runtime builds bundled third-party source               // native runtime 随模块构建
├─ PrecompiledData stores DataGuid + BuildIdentifier       // 脚本制品带身份
├─ Engine validates transpiled JIT against DataGuid        // 不匹配就禁用二进制 JIT
└─ DumpPrecompiledData exports artifact identity           // 可离线核对制品
```

[1] slua 的 native runtime 打包是插件内建能力，而且以多平台预编译库形式随仓库分发

```bash
# ============================================================================
# 文件: Reference/sluaunreal/Plugins/slua_unreal/make_android.sh
# 文件: Reference/sluaunreal/Plugins/slua_unreal/make_ios.sh
# 文件: Reference/sluaunreal/Plugins/slua_unreal/make_linux.sh
# 文件: Reference/sluaunreal/Plugins/slua_unreal/make_osx.sh
# 文件: Reference/sluaunreal/Plugins/slua_unreal/make_win.bat
# 位置: make_android.sh:6-28; make_ios.sh:1-7; make_linux.sh:1-7; make_osx.sh:1-7; make_win.bat:1-15
# 位置说明: slua 仓库自己生产并携带多平台 liblua 产物
# ============================================================================
# make_android.sh
6:mkdir -p build_android_v7a && cd build_android_v7a
10:cmake --build build_android_v7a --config Release
11:mkdir -p Library/Android/armeabi-v7a/
12:cp build_android_v7a/liblua.a Library/Android/armeabi-v7a/liblua.a
14:mkdir -p build_android_arm64 && cd build_android_arm64
19:mkdir -p Library/Android/armeabi-arm64/
20:cp build_android_arm64/liblua.a Library/Android/armeabi-arm64/liblua.a
27:mkdir -p Library/Android/x86/
28:cp build_android_x86/liblua.a Library/Android/x86/liblua.a

# make_ios.sh / make_linux.sh / make_osx.sh / make_win.bat
1:mkdir -p build_ios && cd build_ios
6:mkdir -p Library/iOS/
7:cp build_ios/Release-iphoneos/liblua.a Library/iOS/liblua.a
1:mkdir -p build_linux && cd build_linux
6:mkdir -p Library/Linux
7:cp build_linux/liblua.a Library/Linux/liblua.a
1:mkdir -p build_osx && cd build_osx
6:mkdir -p Library/Mac
7:cp build_osx/Release/liblua.a Library/Mac/liblua.a
4:cmake --build build_win32 --config RelWithDebInfo
6:copy /Y build_win32\RelWithDebInfo\lua.lib Library\Win32\lua.lib
12:cmake --build build_win64 --config RelWithDebInfo
14:copy /Y build_win64\RelWithDebInfo\lua.lib Library\Win64\lua.lib
    // ★ 所有主流平台都把 liblua 当成插件随附资产处理
```

```csharp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/slua_unreal.Build.cs
// 位置: slua_unreal.Build.cs:31-76
// 位置说明: UBT 直接挑选 `Library/` 下的预编译 Lua 库
// ============================================================================
31:        var externalSource = Path.Combine(PluginDirectory, "External");
32:        var externalLib = Path.Combine(PluginDirectory, "Library");
42:        if (Target.Platform == UnrealTargetPlatform.IOS)
44:            PublicAdditionalLibraries.Add(Path.Combine(externalLib, "iOS/liblua.a"));
46:        else if (Target.Platform == UnrealTargetPlatform.Android)
49:            PublicAdditionalLibraries.Add(Path.Combine(externalLib, "Android/armeabi-v7a/liblua.a"));
50:            PublicAdditionalLibraries.Add(Path.Combine(externalLib, "Android/armeabi-arm64/liblua.a"));
51:            PublicAdditionalLibraries.Add(Path.Combine(externalLib, "Android/x86/liblua.a"));
65:        else if (Target.Platform == UnrealTargetPlatform.Win64)
67:            PublicAdditionalLibraries.Add(Path.Combine(externalLib, "Win64/lua.lib"));
73:        else if (Target.Platform == UnrealTargetPlatform.Linux)
75:            PublicAdditionalLibraries.Add(Path.Combine(externalLib, "Linux/liblua.a"));
    // ★ 部署时优先处理的是“平台 Lua runtime 在哪”，不是“脚本包如何签名”
```

[2] slua 的官方 script 加载链只关心 bytes，未在正式通路上看到内建加密、签名或版本 gate

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Source/democpp/MyGameInstance.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaState.cpp
// 位置: MyGameInstance.cpp:41-63;
//       LuaState.cpp:131-155,725-763,739-751
// 位置说明: 官方样例和 runtime 主链都只做“取 bytes -> loadbuffer”
// ============================================================================
// MyGameInstance.cpp
41:	state = new NS_SLUA::LuaState("SLuaMainState", this);
42:	state->setLoadFileDelegate([](const char* fn, FString& filepath)->TArray<uint8> {
47:		path /= "Lua";
48:		path /= filename.Replace(TEXT("."), TEXT("/"));
51:		TArray<FString> luaExts = { UTF8_TO_TCHAR(".lua"), UTF8_TO_TCHAR(".luac") };
55:			FFileHelper::LoadFileToArray(Content, *fullPath);
57:				filepath = fullPath;
58:				return MoveTemp(Content);
    // ★ 官方 demo 同时接受明文 .lua 和 bytecode .luac，没有额外校验步骤

// LuaState.cpp
131:    int LuaState::loader(lua_State* L) {
135:        TArray<uint8> buf = state->loadFile(fn, filepath);
139:            if(luaL_loadbuffer(L,(const char*)buf.GetData(),buf.Num(),chunk)==0) {
153:    TArray<uint8> LuaState::loadFile(const char* fn,FString& filepath) {
154:        if(loadFileDelegate) return loadFileDelegate(fn,filepath);
    // ★ loader 只接收 delegate 返回的 bytes，不负责验签、解密或版本协商

725:    LuaVar LuaState::doBuffer(const uint8* buf,uint32 len, const char* chunk, LuaVar* pEnv) {
729:        if(luaL_loadbuffer(L, (const char *)buf, len, chunk)) {
755:    LuaVar LuaState::doFile(const char* fn, LuaVar* pEnv) {
757:        TArray<uint8> buf = loadFile(fn, filepath);
762:            LuaVar r = doBuffer(buf.GetData(),buf.Num(),chunk,pEnv );
    // ★ 进入 runtime 后立刻走 luaL_loadbuffer，官方链上没有看到中间安全/一致性 gate

739:    LuaVar LuaState::doString(const char* str, LuaVar* pEnv) {
747:        FString md5FString = FMD5::HashAnsiString(UTF8_TO_TCHAR(str));
748:        debugStringMap.Add(md5FString, UTF8_TO_TCHAR(str));
749:        return doBuffer((const uint8*)str,len,TCHAR_TO_UTF8(*md5FString),pEnv);
    // ★ 这里的 MD5 仅用于 editor debug string 名称，不是部署安全策略
```

[3] Angelscript 的部署治理不在“复制 native runtime”，而在“校验 script 制品身份”

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs
// 位置: 20-22
// 位置说明: third-party runtime 作为插件源码的一部分参与构建
// ============================================================================
20:			var AngelscriptThirdPartyPath = Path.Combine(ModuleDirectory, "ThirdParty", "angelscript");
21:			PublicIncludePaths.Add(Path.Combine(AngelscriptThirdPartyPath, "source"));
22:			PublicIncludePaths.Add(AngelscriptThirdPartyPath);
    // ★ third-party runtime 跟着插件源码一起编，不是像 slua 那样分发 liblua 资产
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp
// 位置: PrecompiledData.h:569-590;
//       PrecompiledData.cpp:2627-2645;
//       AngelscriptEngine.cpp:1550-1555;
//       AngelscriptStateDump.cpp:1038-1056
// 位置说明: Angelscript 运行时跟随模块构建，但 script 制品带 DataGuid/BuildIdentifier
// ============================================================================
// PrecompiledData.h / .cpp
569:	FGuid DataGuid;
578:	int32 BuildIdentifier = -1;
589:		Ar << Data.DataGuid;
590:		Ar << Data.BuildIdentifier;
2627:int32 FAngelscriptPrecompiledData::GetCurrentBuildIdentifier()
2642:bool FAngelscriptPrecompiledData::IsValidForCurrentBuild()
2644:	return BuildIdentifier == GetCurrentBuildIdentifier() && BuildIdentifier != -1;
    // ★ script 制品天然带身份，而且 build config 变化会直接让旧制品失效

// AngelscriptEngine.cpp
1550:				const FStaticJITCompiledInfo* CompiledInfo = FStaticJITCompiledInfo::Get();
1552:				if (CompiledInfo != nullptr && CompiledInfo->PrecompiledDataGuid != PrecompiledData->DataGuid)
1554:					UE_LOG(Angelscript, Warning, TEXT("Loaded angelscript precompiled data does not match the transpiled C++ in the game binary. Transpiled code will not be used!"));
1555:					FJITDatabase::Get().Clear();
    // ★ GUID 不一致就直接禁用二进制 JIT，不冒险继续执行

// AngelscriptStateDump.cpp
1038:FAngelscriptStateDump::FTableResult FAngelscriptStateDump::DumpPrecompiledData(FAngelscriptEngine& Engine, const FString& OutputDir)
1042:		TEXT("DataGuid"),
1043:		TEXT("ModuleCount"),
1044:		TEXT("FunctionMappingCount"),
1049:	if (Engine.PrecompiledData == nullptr)
1055:		Writer.AddRow({
1056:			Engine.PrecompiledData->DataGuid.ToString(EGuidFormats::DigitsWithHyphens),
    // ★ 部署身份信息还能被导出做离线核对，而不是只存在内存里
```

### 设计取舍

- slua 的强项是 native runtime 打包边界清晰，多平台 `liblua` 直接跟着插件走，集成成本低。
- 但 script 制品治理几乎全部外包给宿主项目；按官方链路，只能证明它支持 `.lua/.luac` bytes 装载，不能证明它内建了加密、签名或版本淘汰。
- Angelscript 刚好相反：native runtime 更像插件内部实现细节，真正被强约束的是 precompiled script 数据和 binary JIT 的一致性。

### 与 Angelscript 对比

| 观察点 | sluaunreal | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| native runtime 打包方式 | `make_*` 生成并随仓库携带 `Library/<Platform>/liblua.*`，UBT 按平台直接链接（`make_*.sh/.bat`; `slua_unreal.Build.cs:31-76`） | third-party runtime 随插件源码一起构建（`AngelscriptRuntime.Build.cs:20-22`） | 实现方式不同 |
| 官方 script 装载链 | `LoadFileDelegate` 返回 bytes，demo 同时接受 `.lua/.luac`，然后直接 `luaL_loadbuffer()`（`MyGameInstance.cpp:41-63`; `LuaState.cpp:131-155,725-763`） | precompiled data 带 `DataGuid/BuildIdentifier`，运行时还会校验二进制 JIT 是否匹配（`PrecompiledData.h:569-590`; `PrecompiledData.cpp:2627-2645`; `AngelscriptEngine.cpp:1550-1555`） | 实现方式不同 |
| 插件内建加密/签名治理 | 本轮在官方加载链上未见解密、签名校验或包协议解析；需要宿主自己在 delegate 层实现 | 当前可见重点不是加密，而是制品身份和 build 一致性校验 | slua 没有实现对等的插件内建制品治理 |
| 制品可核对性 | 本轮源码只看到脚本 bytes 被加载和 profiler 档案被写出，未见对 script 包身份的内建导出 | `DumpPrecompiledData()` 可直接导出 `DataGuid` 等身份信息（`AngelscriptStateDump.cpp:1038-1056`） | 实现质量差异：Angelscript 的部署可审计性更强 |

---

## 深化分析 (2026-04-09 07:33:07)

以下只补前文没有展开清楚的实现语义，不重复已有结论。

### [维度 D3] Blueprint 互通的“运行时补丁层” vs “编译期契约层”

前文已经说明 slua 会 hook Blueprint 事件。本轮补的是“hook 到什么粒度、Blueprint 主动调用 Lua 又走哪条路”。从 `ULuaOverrider::luaOverrideFunc()` 和 `LuaOverrider::hookBpScript()` 看，slua 的 Blueprint 覆写并不是生成一套新的 script-side `UFunction` 元数据，而是在现有 `UFunction` 上做运行时补丁：先复制一份 `Super_` 函数保存原 native 入口，再把原函数的 `NativeFunc` 和 `Script` 头部改成 Lua override 入口。这样做的优势是对现有 Blueprint 资产侵入很低，代价是覆写合法性、签名不匹配和返回值修补大多在运行时暴露。

与此同时，Blueprint 主动调用 Lua 也不是走统一的 `UFunction` 反射桥，而是通过 `ULuaBlueprintLibrary::CallToLua*()` 直接按字符串名取 Lua 全局函数。换句话说，slua 的 Blueprint 互通是两套桥并行：事件覆写走 `LuaOverrider`，Blueprint 主动调 Lua 走 `BlueprintFunctionLibrary`。

Angelscript 的策略几乎相反。`FAngelscriptClassGenerator` 在编译脚本类时先验证 `BlueprintOverride` 是否真的指向父类中的 `BlueprintImplementableEvent / BlueprintNativeEvent`，并检查签名、`EditorOnly` 元数据和 `ScriptName` 映射；脚本调用 BlueprintCallable 时，再通过 `BindBlueprintCallableReflectiveFallback()` 把 `UFunction` 直接挂进 script type。因此 Angelscript 的 Blueprint 互通更像“类型系统中的正式成员”，而 slua 更像“运行时补丁 + 工具函数”。

```
[D3] Blueprint Bridge Shape
sluaunreal
├─ LuaOverrider::bindOverrideFuncs()                // 扫 Lua table，找可覆写函数
├─ hookBpScript()                                   // 复制 Super_ UFunction，改写原入口
├─ luaOverrideFunc()                                // FFrame 进入 Lua self table
└─ ULuaBlueprintLibrary::CallToLua*()               // Blueprint 节点按名字主动调 Lua

Angelscript
├─ FAngelscriptClassGenerator                       // 编译期校验 BlueprintOverride 合法性
├─ Create/keep script UFunction metadata            // 覆写进入生成类元数据
└─ BindBlueprintCallableReflectiveFallback()        // 把 BlueprintCallable 挂成 script method
```

[1] slua 的 Blueprint 覆写是对已有 `UFunction` 做运行时补丁，而不是新建一套独立调度层

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaOverrider.cpp
// 函数: ULuaOverrider::luaOverrideFunc / LuaOverrider::hookBpScript
// 位置: 61-72, 83-127, 1399-1447
// 位置说明: Blueprint 事件入口被改写成 Lua override，同时保留一份 `Super_` 副本
// ============================================================================
61:void ULuaOverrider::luaOverrideFunc(UObject* Context, FFrame& Stack, RESULT_DECL)
64:    UFunction* func = Stack.Node;
69:    UObject* obj = Context;
72:    uint8* locals = Stack.Locals;
83:    if (Stack.CurrentNativeFunction)
92:            func = Stack.CurrentNativeFunction;    // ★ 遇到 ProcessContextOpcode 时，改回真实 native 节点
123:            if (func->GetNativeFunc() == (NS_SLUA::FNativeFuncPtr)&ULuaOverrider::luaOverrideFunc)
125:                Stack.SkipCode(1);                 // ★ 从 native 回调进入时，跳过前缀 override opcode

1399:        auto supercallFunc = duplicateUFunction(func, cls, FName(*(SUPER_CALL_FUNC_NAME_PREFIX + func->GetName())), func->GetNativeFunc());
1417:                overrideFunc->SetNativeFunc(hookFunc); // ★ 原 UFunction 的 native 入口直接换成 Lua override
1419:            overrideFunc->Script.Insert(Code, CodeSize, 0); // ★ 同时在 Script bytecode 头部插入 override opcode
1429:            overrideFunc = duplicateUFunction(func, cls, func->GetFName(), (FNativeFuncPtr)&ULuaOverrider::luaOverrideFunc);
1445:        if (overrideFunc->ReturnValueOffset != MAX_uint16 && !overrideFunc->HasAnyFunctionFlags(FUNC_HasOutParms | FUNC_Native))
1447:            overrideFunc->FunctionFlags |= FUNC_HasOutParms; // ★ 为返回值回写修补 flag
```

[2] slua 的 Blueprint 主动调 Lua 是字符串分派，不是静态 method binding

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaBlueprintLibrary.cpp
// 函数: ULuaBlueprintLibrary::CallToLuaWithArgs / CallToLua
// 位置: 51-97
// 位置说明: Blueprint 节点通过字符串函数名进入 Lua 全局空间
// ============================================================================
51:FLuaBPVar ULuaBlueprintLibrary::CallToLuaWithArgs(UObject* WorldContextObject, FString funcname,const TArray<FLuaBPVar>& args,FString StateName) {
54:    auto gameInstance = UGameplayStatics::GetGameInstance(WorldContextObject);
60:    auto ls = LuaState::get(gameInstance);
63:    LuaVar f = ls->get(TCHAR_TO_UTF8(*funcname)); // ★ Blueprint 侧给的是字符串名，不是编译期签名
69:    auto fillParam = [&]
71:        for (auto& arg : args) {
72:            arg.value.push(ls->getLuaState());     // ★ BP 参数先封进 FLuaBPVar，再压栈
76:    return f.callWithNArg(fillParam);
79:FLuaBPVar ULuaBlueprintLibrary::CallToLua(UObject* WorldContextObject, FString funcname,FString StateName) {
91:    LuaVar f = ls->get(TCHAR_TO_UTF8(*funcname));
96:    return f.callWithNArg(nullptr);               // ★ 无参调用同样是运行时字符串查找
```

[3] Angelscript 把 BlueprintOverride 的合法性前移到编译期；脚本调 BlueprintCallable 则挂成正式方法

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 函数: FAngelscriptClassGenerator::AnalyzeClasses 中的 BlueprintOverride 校验路径
// 位置: 732-828, 834-917
// 位置说明: 覆写父类事件前，先校验父类存在性、BlueprintEvent 属性、签名和 EditorOnly 约束
// ============================================================================
732:        if (FunctionDesc->bBlueprintOverride)
737:            auto* ParentFunction = GetBlueprintEventByScriptName(CodeSuperClass, FunctionDesc->FunctionName);
761:                    if (!SuperFunctionDesc.IsValid())
763:                        FAngelscriptEngine::Get().ScriptCompileError(... "does not exist in superclass" ...);
768:                    else if (!SuperFunctionDesc->bBlueprintEvent && !SuperFunctionDesc->bBlueprintOverride)
770:                        FAngelscriptEngine::Get().ScriptCompileError(... "is not marked BlueprintEvent" ...);
775:                    else if (!SuperFunctionDesc->SignatureMatches(FunctionDesc))
777:                        FAngelscriptEngine::Get().ScriptCompileError(... "does not match signature" ...);
782:                    else if (SuperFunctionDesc->Meta.Contains(NAME_Meta_EditorOnly) && !FunctionDesc->Meta.Contains(NAME_Meta_EditorOnly))
784:                        FAngelscriptEngine::Get().ScriptCompileError(... "editor-only parent function" ...);

835:                bool bASReturnsVoid = !FunctionDesc->ReturnType.IsValid();
836:                bool bUEReturnsVoid = ParentFunction->GetReturnProperty() == nullptr;
866:                if (FunctionDesc->Arguments.Num() != UEParmCount)
867:                    bArgCountMismatch = true;      // ★ 参数个数先验匹配
907:                        OverrideArg.ArgumentName = Property->GetName();
916:                        if (!FunctionDesc->Arguments[ArgumentIndex].Type.MatchesProperty(Property, FAngelscriptType::EPropertyMatchType::OverrideArgument))
917:                            bTypeMismatch = true;  // ★ 参数类型直接按 UE FProperty 规则验证
```

补充证据：`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp:100-163,290-370` 会把符合条件的 `UFunction` 绑定成 script method，并在调用时构造 `ParmsSize` 缓冲区后 `ProcessEvent()`；这和 slua 的 `ULuaBlueprintLibrary::CallToLua*()` 字符串分派是两种不同接入层。

新增对比结论：

- slua 的 Blueprint 覆写是“实现方式不同”，不是“没有实现”：`LuaOverrider.cpp:1399-1447` 证明它直接复制并改写了现有 `UFunction`。它的优势是迁移存量 Blueprint 资产很轻；代价是签名和覆写错误更多在运行时暴露。
- Angelscript 在 BlueprintOverride 上体现的是“实现质量差异”：`AngelscriptClassGenerator.cpp:761-917` 把父类存在性、签名和 `EditorOnly` 约束一次性前移到编译期，错误会在生成类之前终止。
- Blueprint 主动调用脚本这一点上，slua 采用 `ULuaBlueprintLibrary::CallToLua*()`（`LuaBlueprintLibrary.cpp:51-97`），而 Angelscript 把 BlueprintCallable 直接绑定进 script type（`BlueprintCallableReflectiveFallback.cpp:100-163,290-370`）。这是“实现方式不同”，不是简单的优劣高低。

### [维度 D4] 热更新工作流的边界：宿主装载钩子 vs 引擎主导热重载状态机

slua 常被说成“支持线上热更新”，但从当前仓库源码看，插件本体做的其实是把“脚本 bytes 从哪里来”这件事开放给宿主。`LuaState` 在初始化时把自定义 `loader` 插进 Lua 的 `package.searchers`，`loadFile()` 则完全转发给 `LoadFileDelegate`，后续 `requireModule()` 仍走 Lua 原生 `require`。这意味着插件负责的是“把 bytes 接进 Lua VM”，而不是“如何分发、验签、换代、回滚”。这正好解释了 slua 为什么容易接入项目自定义在线更新链，但官方插件本身又没有一套显式的热更状态机。

更重要的是失败语义。`LuaOverrider::bindOverrideFuncs()` 在 `requireModule()` 失败、Lua 返回值不是 table/function 时，直接记录错误并 `return false`；源码里没有出现与 Angelscript 类似的“旧模块继续服役、失败版本隔离、软/全量重载分级”状态机。本轮额外用 `rg -n "package\\.loaded"` 扫描 `Reference/sluaunreal/Plugins/slua_unreal/Source` 没有命中，因此“module cache 失效/换代由宿主负责”这条判断属于基于源码缺失点的推断，不是显式注释。

Angelscript 则是反过来的设计。它把开发期热重载收进引擎主循环和目录监听器：`DirectoryWatcher` 负责把 `.as` 变更排队，`CheckForHotReload()` 根据运行场景决定 `SoftReloadOnly` 或 `FullReload`，`AnalyzeReloadFromMemory()` 再把结果明确映射成 `SoftReload / FullReloadSuggested / FullReloadRequired`。更关键的是，测试明确验证了“坏 reload 不替换旧代码”，这说明 Angelscript 的热重载优先保障开发期稳定性，而不是线上换包吞吐。

```
[D4] Reload Boundary
sluaunreal
├─ Host LoadFileDelegate                             // 宿主决定 bytes 来源
├─ LuaState loader -> package.searchers             // 插件只负责接入 Lua loader
├─ requireModule()/doFile()                         // 模块装载继续沿用 Lua require 语义
└─ bindOverrideFuncs() returns false on failure     // 失败时停止当前绑定，没有内建版本换代状态机

Angelscript
├─ DirectoryWatcher / file scan                     // 目录监听或轮询扫描
├─ FileChangesDetectedForReload                     // 统一排队
├─ AnalyzeReloadFromMemory()                        // 判定 Soft/Full reload 级别
└─ broken reload keeps old code active              // 失败时保留旧版本代码
```

[1] slua 的热更新入口本质上是“宿主 bytes 装载钩子 + Lua 原生 require”

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaState.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaOverrider.cpp
// 函数: LuaState 初始化 loader / setLoadFileDelegate / requireModule / bindOverrideFuncs
// 位置: LuaState.cpp:603-617, 651-655, 755-785; LuaOverrider.cpp:1194-1205
// 位置说明: 插件开放 bytes 装载和 require 接口，但没有内建 reload 分级或 rollback 状态机
// ============================================================================
// LuaState.cpp
603:        lua_pushcfunction(L,loader);
606:        lua_getglobal(L,"package");
607:        lua_getfield(L,-1,"searchers");
611:        for(int i=lua_rawlen(L,loaderTable)+1;i>2;i--) {
615:        lua_pushvalue(L,loaderFunc);
616:        lua_rawseti(L,loaderTable,2);              // ★ 自定义 loader 被插到 package.searchers[2]

651:    void LuaState::setLoadFileDelegate(LoadFileDelegate func) {
652:        loadFileDelegate = func;                   // ★ bytes 来源完全由宿主 delegate 决定

755:    LuaVar LuaState::doFile(const char* fn, LuaVar* pEnv) {
757:        TArray<uint8> buf = loadFile(fn, filepath);
762:            LuaVar r = doBuffer(buf.GetData(),buf.Num(),chunk,pEnv );

768:    LuaVar LuaState::requireModule(const char* fn, LuaVar* pEnv) {
770:        lua_getglobal(L, "require");
771:        lua_pushstring(L, fn);
773:        if (lua_pcall(L, 1, 1, top))
776:            return LuaVar();                       // ★ 模块装载仍然沿用 Lua require 语义

// LuaOverrider.cpp
1194:        NS_SLUA::LuaVar luaModule = sluaState->requireModule(TCHAR_TO_UTF8(*luaFilePath));
1195:        if (!luaModule.isValid()) {
1196:            NS_SLUA::Log::Error("... can't find LuaFilePath ...");
1198:            return false;                         // ★ 当前绑定失败即退出，没有旧/新模块并存语义
1201:        if (!luaModule.isTable() && !luaModule.isFunction()) {
1202:            NS_SLUA::Log::Error("... not a lua table or function!");
1204:            return false;
```

[2] Angelscript 把热重载做成显式状态机，并用测试锁住失败回退语义

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp
// 函数: queue file change / CheckForHotReload / AnalyzeReloadFromMemory / failure fallback test
// 位置: DirectoryWatcherInternal.cpp:45-89; AngelscriptEngine.cpp:2743-2829;
//       AngelscriptTestEngineHelper.cpp:319-349; AngelscriptHotReloadFunctionTests.cpp:489-501
// 位置说明: 目录变更、reload 分级和失败保留旧代码是同一条正式工作流
// ============================================================================
// DirectoryWatcherInternal.cpp
55:            Engine.LastFileChangeDetectedTime = FPlatformTime::Seconds();
61:                    Engine.FileDeletionsDetectedForReload.AddUnique({ AbsolutePath, RelativePath });
65:                    Engine.FileChangesDetectedForReload.AddUnique({ AbsolutePath, RelativePath });
68:                UE_LOG(Angelscript, Log, TEXT("Queued script file change for primary engine reload: %s"), *RelativePath);

// AngelscriptEngine.cpp
2746:    FileList.Append(FileChangesDetectedForReload);
2753:        for (const auto& DeletedFile : FileDeletionsDetectedForReload)
2770:        PerformHotReload(CompileType, FileList);   // ★ 文件队列最终收敛到统一热重载入口
2822:        if (!GIsEditor || HasGameWorld())
2824:            CheckForHotReload(ECompileType::SoftReloadOnly);
2828:            CheckForHotReload(ECompileType::FullReload); // ★ 运行场景不同，reload 策略不同

// AngelscriptTestEngineHelper.cpp
331:        case ECompileResult::FullyHandled:
332:            OutReloadRequirement = FAngelscriptClassGenerator::SoftReload;
335:        case ECompileResult::PartiallyHandled:
336:            OutReloadRequirement = FAngelscriptClassGenerator::FullReloadSuggested;
340:        case ECompileResult::ErrorNeedFullReload:
341:            OutReloadRequirement = FAngelscriptClassGenerator::FullReloadRequired;
346:        case ECompileResult::Error:
348:            OutReloadRequirement = FAngelscriptClassGenerator::Error; // ★ reload 级别是正式结果，不是日志约定

// AngelscriptHotReloadFunctionTests.cpp
490:const bool bCompiled = CompileModuleWithResult(&Engine, ECompileType::SoftReloadOnly, ModuleName, TEXT("HotReloadFailureKeepsOldCode.as"), BrokenScript, ReloadResult);
491:TestFalse(TEXT("... should fail the broken hot reload compile"), bCompiled);
496:if (!TestTrue(TEXT("... should still execute the old generated function after reload failure"), ExecuteGeneratedIntEventOnGameThread(&Engine, TestObject, GetValueBeforeFailure, ResultAfterFailure)))
501:TestEqual(TEXT("... should keep the old code active after the broken reload"), ResultAfterFailure, 5); // ★ 失败后旧代码继续生效
```

新增对比结论：

- slua 在“线上热更新”上体现的是“实现边界不同”，不是插件内建了完整的线上工作流：`LuaState.cpp:603-617,651-655,755-785` 证明它只把 bytes 装载和 `require` 接入 Lua VM，分发、换代、验签和回滚都留给宿主。
- Angelscript 没有实现 slua 那种宿主可接管的线上 bytes 分发接口，但它明确实现了开发期文件热重载状态机和 reload 分级（`AngelscriptEngine.cpp:2743-2829`; `AngelscriptTestEngineHelper.cpp:319-349`）。这是“目标场景不同”，不是简单谁多谁少。
- 失败恢复上存在“实现质量差异”：Angelscript 有显式回归测试保证坏 reload 保留旧代码（`AngelscriptHotReloadFunctionTests.cpp:489-501`）；slua 当前可见错误路径主要是 `return false`（`LuaOverrider.cpp:1194-1205`），本轮未见对等的旧版本保活与回退测试。

### [维度 D8] 性能路径与 profiler：桥接层缓存 + 诊断插桩 vs 默认直连 + StaticJIT

slua 的性能设计不能只看“静态导出”四个字。更准确地说，它把性能拆成三层：第一层是 `LuaWrapper*.inc` 和 `extensionMMap`，让大量常见值类型和 `UObject` 扩展方法不必每次走裸反射；第二层是 `LuaFunctionAccelerator`，在第一次碰到 `UFunction` 时就把 `FProperty` 校验器、out param、latent 标记和返回值 pusher 全部缓存起来；第三层才是 profiler，这一层不是为了提速，而是为了让脚本热点和内存变化可观测。

这也解释了 slua 的“静态导出”和 Angelscript `StaticJIT` 不能简单一一对照。slua 的静态导出主要是减小 bridge 层的拆装和元数据遍历成本；profiler 则是可开关的诊断通道，开启时会给 `lua_sethook` 挂 `call/return` 钩子，并经 TCP server 与 `.sluastat` 档案输出形成闭环。Angelscript 的优化路径则更靠近默认执行主干：有 direct native pointer 就直接 bind，只有缺 direct pointer 才退回 reflective fallback；而 `StaticJIT` 会把 `VMEntry / ParmsEntry / RawFunction` 直接注册进 `FJITDatabase`，把优化结果做成常驻执行资产。

```
[D8] Performance Stack
sluaunreal
├─ LuaWrapper*.inc / extension map                  // 先缩短常见类型桥接路径
├─ LuaFunctionAccelerator cache                     // 首次解析 UFunction，后续复用
├─ lua_sethook profiler                             // 逐 call/return 采样
└─ TCP server + .sluastat archive                   // 远端查看与离线回放

Angelscript
├─ direct native pointer bind                       // 默认优先直连 native entry
├─ reflective fallback only when needed             // 缺 direct pointer 才回退
├─ StaticJITFunction registry                       // VMEntry/ParmsEntry/RawFunction 常驻注册
└─ FJITDatabase                                     // 优化元数据与引用解析表
```

[1] slua 的“静态导出 + 动态反射”真正降本点，在 extension map 和 `LuaFunctionAccelerator` 缓存

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaObject.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaFunctionAccelerator.cpp
// 函数: searchExtensionMethod / push(UFunction*) / LuaFunctionAccelerator::findOrAdd
// 位置: LuaObject.cpp:741-775, 797-801, 3062-3071;
//       LuaFunctionAccelerator.cpp:33-79, 145-155, 181-239
// 位置说明: 常见扩展先走静态 map，真正落到 UFunction 时再把反射拆装成本缓存起来
// ============================================================================
// LuaObject.cpp
741:    int searchExtensionMethod(lua_State* L,UClass* cls,const char* name,bool isStatic=false) {
745:            TMap<FString,ExtensionField>* mapptr = isStatic?extensionMMap_static.Find(cls):extensionMMap.Find(cls);
748:                auto fieldptr = mapptr->Find(name);
752:                        lua_pushcfunction(L, fieldptr->func); // ★ extension method 命中后直接推 C function
774:            cacheFunction(L, isStatic ? cls : nullptr);     // ★ 命中后还缓存到 metatable/uservalue

797:        UFunction* func = cls->FindFunctionByName(ANSI_TO_TCHAR(name));
799:            return LuaObject::push(L, func, cls);           // ★ 命不中 extension map 时再转 UFunction 路径

3062:    int LuaObject::push(lua_State* L,UFunction* func,UClass* cls)  {
3063:        lua_pushlightuserdata(L, LuaFunctionAccelerator::findOrAdd(func));
3066:            lua_pushcclosure(L, ufuncClosure, 2);           // ★ closure 里直接带 accelerator 指针

// LuaFunctionAccelerator.cpp
46:            if (prop->HasAnyPropertyFlags(CPF_OutParm))
48:                outParmRecProps.Add(prop);                  // ★ out parm 列表只扫描一次
76:                auto checker = LuaObject::getChecker(prop);
78:                checkerRef->checker = checker;              // ★ 参数 checker 预缓存
145:    LuaFunctionAccelerator* LuaFunctionAccelerator::findOrAdd(UFunction* inFunc)
147:        auto ret = cache.Find(inFunc);
153:        auto value = new LuaFunctionAccelerator(inFunc);
154:        cache.Emplace(inFunc, value);                       // ★ 同一个 UFunction 之后不再重复解析
181:    int LuaFunctionAccelerator::call(lua_State* L, int offset, UObject* obj, bool& isLatentFunction, NewObjectRecorder* objRecorder)
214:        for (auto prop : outParmRecProps)
239:        for (auto& checkerInfo : paramsChecker)             // ★ 调用热路径直接消费缓存结果
```

[2] slua 的 profiler 是独立诊断通道，不是默认执行主干的一部分

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaProfiler.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_profile/Private/slua_remote_profile.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/SluaProfilerDataManager.cpp
// 函数: debug_hook / changeHookState / FProfileServer / SerializeCompreesedDataToFile
// 位置: LuaProfiler.cpp:256-347, 490-499;
//       slua_remote_profile.cpp:56-60, 67-110, 139-144;
//       SluaProfilerDataManager.cpp:644-651, 927-947
// 位置说明: profiler 通过 `lua_sethook` 逐调用采样，再走 TCP 和压缩档案导出
// ============================================================================
// LuaProfiler.cpp
256:        void debug_hook(lua_State* L, lua_Debug* ar) {
263:            if (ar->event > 1)
268:            int event = ar->event;
285:                        event += PHE_ENTER_COROUTINE;       // ★ 协程 enter/exit 也被折算进 profile 事件
319:            takeSample(event, ar->linedefined, ar->name ? ar->name : "", ar->short_src, start, L);
347:                lua_sethook(L, debug_hook, LUA_MASKRET | LUA_MASKCALL, 0); // ★ 逐 call/return 采样
490:        RunState currentRunState = (RunState)profiler.getFromTable<int>("currentRunState");
493:            takeMemorySample(PHE_MEMORY_INCREACE, LuaMemoryProfile::memIncreaceThisFrame(LS), L);
494:            takeSample(PHE_TICK, -1, "", "", getTime(), L);

// slua_remote_profile.cpp
56:        ListenEndpoint.Address = FIPv4Address(0, 0, 0, 0);
57:        ListenEndpoint.Port = ... ? Port : 8081;
58:        Listener = new FTcpListener(ListenEndpoint);       // ★ profiler 自带远端 TCP server
67:    uint32 FProfileServer::Run()
102:                while (conn->ReceiveData(Message))
106:                    (void)OnProfileMessageDelegate.ExecuteIfBound(Message);
139:    bool FProfileServer::HandleConnectionAccepted(FSocket* ClientSocket, const FIPv4Endpoint& ClientEndpoint)
141:        PendingConnections.Enqueue(MakeShareable(new FProfileConnection(ClientSocket, ClientEndpoint)));

// SluaProfilerDataManager.cpp
644:FString FProfileDataProcessRunnable::GenerateStatFilePath()
647:    FString filePath = FPaths::ProfilingDir() + "/Sluastats/" ... + ".sluastat";
927:void FProfileDataProcessRunnable::SerializeCompreesedDataToFile(FArchive& ar)
935:    int32 compressedSize = FCompression::CompressMemoryBound(NAME_Oodle, uncompressedSize);
945:    ar << uncompressedSize;
946:    ar << compressedSize;
947:    ar.Serialize(compressedBuffer, compressedSize);        // ★ 采样结果最终落盘为压缩档案
```

[3] Angelscript 的优化路径更靠近默认执行主干：有直连就直连，再把 StaticJIT 入口注册进数据库

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp
// 函数: BlueprintCallable 绑定路径 / FStaticJITFunction::FStaticJITFunction
// 位置: Bind_BlueprintCallable.cpp:72-90, 95-147;
//       StaticJITHeader.h:82-99;
//       StaticJITHeader.cpp:38-47
// 位置说明: 先尝试 direct native pointer，StaticJIT 则把执行入口作为常驻资产注册
// ============================================================================
// Bind_BlueprintCallable.cpp
72:    auto* DirectNativePointer = &Entry->FuncPtr;
73:    const bool bHasDirectNativePointer = DirectNativePointer != nullptr && DirectNativePointer->IsBound();
74:    if (!bHasDirectNativePointer)
76:        if (!BindBlueprintCallableReflectiveFallback(InType, Function, Signature, *Entry))
90:        return;                                         // ★ 没有直连指针时才退回 reflective fallback
95:    asSFuncPtr ASFuncPtr;
98:    FMemory::Memcpy(&ASFuncPtr, DirectNativePointer, sizeof(asSFuncPtr));
107:            int GlobalFunctionId = FAngelscriptBinds::BindGlobalFunction(Signature.Declaration, ASFuncPtr, Entry->Caller);
133:        int FunctionId = FAngelscriptBinds::BindMethodDirect
145:    SCRIPT_NATIVE_UFUNCTION(Function, Function->GetName(), Signature.bTrivial); // ★ 直连路径还会登记 JIT 元数据

// StaticJITHeader.h / .cpp
82:struct ANGELSCRIPTRUNTIME_API FStaticJITFunction
84:    FStaticJITFunction(uint32 FunctionId,
85:        asJITFunction VMEntry,
86:        asJITFunction_ParmsEntry ParmsEntry,
87:        asJITFunction_Raw RawFunction
88:    );
38:FStaticJITFunction::FStaticJITFunction(uint32 FunctionId, asJITFunction InVMEntry, asJITFunction_ParmsEntry InParmsEntry, asJITFunction_Raw InRawFunction)
40:    FJITDatabase::FJITFunctions Funcs;
41:    Funcs.VMEntry = InVMEntry;
42:    Funcs.ParmsEntry = InParmsEntry;
43:    Funcs.RawFunction = InRawFunction;
46:    JITDatabase.Functions.Add(FunctionId, Funcs);   // ★ JIT 入口被注册到常驻数据库，而不是仅供诊断查看
```

新增对比结论：

- slua 的“静态导出”本质上是“先命中静态 wrapper/extension map，再把剩余动态路径做缓存”（`LuaObject.cpp:741-775,797-801,3062-3071`; `LuaFunctionAccelerator.cpp:33-79,145-239`），而不是完全替换掉反射调用。这与 Angelscript 的 direct bind / reflective fallback 双路由是“实现方式不同”。
- slua profiler 与 Angelscript `StaticJIT` 不属于同一层概念。前者是可开关的诊断插桩和远端观测闭环（`LuaProfiler.cpp:256-347,490-499`; `slua_remote_profile.cpp:56-60,67-144`; `SluaProfilerDataManager.cpp:644-651,927-947`），后者是默认执行路径上的优化资产注册（`Bind_BlueprintCallable.cpp:72-147`; `StaticJITHeader.cpp:38-47`）。
- 如果只比较“脚本级时间线 profiler”，Angelscript 当前源码里没有与 slua 对等的 `lua_sethook` + TCP + `.sluastat` 管线；如果比较“默认执行开销压缩”，Angelscript 的 direct bind + StaticJIT 更接近常驻优化，而 slua 的 profiler 明确不是为常驻热路径设计的。这不是简单的“谁更快”，而是优化对象不同。

---

## 深化分析 (2026-04-09 07:43:57)

### [维度 D2 / D8] slua 的 `UInterface` 支持停在“属性编组层”，Angelscript 把 interface 提升成正式脚本类型

前文已经拆过 slua 的 delegate、RPC 和 Blueprint override，但 `UInterface` 这条线此前没有单独拎出来。继续读 `LuaObject.cpp` 后，可以把结论说得更准：slua 不是“不支持接口”，而是**把接口当成 `FProperty` 编组问题处理**。`pushUInterfaceProperty()` 从 `FScriptInterface` 里拿到的只是 `GetObject()`；Lua 侧最终看到的还是普通 `UObject` userdata。反向写回时，`checkUInterfaceProperty()` 再对传入 `UObject` 做一次 `GetInterfaceAddress()`，然后临时拼成 `FScriptInterface` 塞回参数缓冲。也就是说，接口语义主要存在于“调用边界”和“参数检查”阶段，而不是 Lua 类型系统本身。

与此同时，`ILuaOverriderInterface` 虽然也是一个 `UINTERFACE`，但职责是 `GetLuaFilePath()`、`TryHook()`、`FuncMap` 缓存和 `CallLuaFunction*()`，它是 slua 自己的 hook marker，而不是面向业务脚本作者的通用 interface 声明机制。对比之下，Angelscript 在预处理阶段就把 `interface` chunk 解析成 `ImplementedInterfaces + InterfaceMethodDeclarations`，并在 AS 编译前先 `RegisterObjectType()` 和 `RegisterObjectMethod()`；类生成阶段再创建真正的 `UInterface` metadata、递归挂载父接口，并验证实现类是否补齐了所有必须方法。换句话说，Angelscript 的 interface 不是桥接细节，而是正式的脚本类型合同。

```
[D2] Interface Support Layer
sluaunreal
├─ FInterfaceProperty pusher/checker               // 只在参数/属性编组时识别接口
├─ Lua side sees UObject userdata                  // Lua 侧拿到的不是独立 interface 类型
└─ GetInterfaceAddress() on write-back             // 写回参数时再临时恢复 FScriptInterface

Angelscript
├─ parse `interface` chunk                         // 预处理阶段抽取接口声明与方法
├─ RegisterObjectType(interface)                   // 先注册脚本 interface 类型
├─ RegisterObjectMethod(CallInterfaceMethod)       // 给 interface 引用装方法表
└─ create UInterface metadata + verify methods     // 类生成期落成 UE metadata 并校验实现
```

[1] 关键源码：slua 的接口支持落点在 `FInterfaceProperty` 编组表，而不是独立脚本类型

```cpp
// ============================================================================
// [1]
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaOverriderInterface.h
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaObject.cpp
// 函数: ILuaOverriderInterface / pushUInterfaceProperty / checkUInterfaceProperty / regPusher
// 位置: LuaOverriderInterface.h:10-30;
//       LuaObject.cpp:1869-1875, 2596-2611, 3010, 3038
// 位置说明: 一个接口负责 slua 自己的 hook 合同；另一个接口支持点落在 FProperty 编组层
// ============================================================================
// LuaOverriderInterface.h
10:UINTERFACE()
11:class SLUA_UNREAL_API ULuaOverriderInterface : public UInterface
16:class SLUA_UNREAL_API ILuaOverriderInterface
21:    UFUNCTION(BlueprintNativeEvent)
22:        FString GetLuaFilePath() const;
28:    void TryHook();
30:    NS_SLUA::LuaVar GetCachedLuaFunc(NS_SLUA::lua_State* L, const NS_SLUA::LuaVar selfTable, const FString& FunctionName) {
// ★ 这里暴露的是 slua 自己的对象 hook 合同，不是业务脚本可声明的一般化接口体系

// LuaObject.cpp
1869:    int pushUInterfaceProperty(lua_State *L, FProperty* prop, uint8* parms, int i, NewObjectRecorder* objRecorder) {
1872:        auto &scriptInterface = p->GetPropertyValue(parms);
1873:        UObject *obj = scriptInterface.GetObject();
1875:        return LuaObject::push(L, obj, ref);
// ★ 推到 Lua 侧时只保留 UObject，本身没有生成独立 interface userdata

2596:    void* checkUInterfaceProperty(lua_State* L, FProperty* prop, uint8* parms, int i, bool bForceCopy) {
2599:        UObject* obj = LuaObject::checkUD<UObject>(L, i);
2600:        void* interfacePtr = obj ? obj->GetInterfaceAddress(p->InterfaceClass) : nullptr;
2608:            luaL_error(L, "arg %d expect interface class of %s, but got %s", i,
2611:        p->SetPropertyValue(parms, FScriptInterface(obj, interfacePtr));
// ★ 只有在参数写回时，才按目标 InterfaceClass 做一次运行时适配与报错

3010:        regPusher(FInterfaceProperty::StaticClass(), pushUInterfaceProperty);
3038:        regChecker(FInterfaceProperty::StaticClass(), checkUInterfaceProperty);
// ★ 接口支持被挂在通用 property dispatch table 上，而不是独立的脚本类型注册链
```

[2] 关键源码：Angelscript 把 interface 作为正式脚本类型和类生成合同处理

```cpp
// ============================================================================
// [2]
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 函数: parse interface / register interface methods / add implemented interfaces
// 位置: AngelscriptPreprocessor.cpp:780-819, 1089-1157;
//       AngelscriptClassGenerator.cpp:2764-2804, 3359-3364, 5060-5183
// 位置说明: interface 从语法解析、脚本类型注册到 UE metadata 落成与实现校验是一条正式流水线
// ============================================================================
// AngelscriptPreprocessor.cpp
780:        ClassDesc->bIsInterface = true;
790:            ClassDesc->SuperClass = TEXT("UInterface");
800:    // Parse implemented interfaces from the inheritance list
819:                    ClassDesc->ImplementedInterfaces.Add(InterfaceName);
// ★ interface 与 implemented interfaces 在预处理阶段就进入 ClassDesc 合同

1089:        // Register the interface as an AS reference type
1094:        int TypeId = Engine.Engine->RegisterObjectType(
1097:            asOBJ_REF | asOBJ_NOCOUNT | asOBJ_IMPLICIT_HANDLE);
1106:                // Register interface methods on the AS type
1123:                        auto* Sig = Engine.RegisterInterfaceMethodSignature(FName(*MethodName));
1134:                            int32 FuncId = Engine.Engine->RegisterObjectMethod(
1137:                                asFUNCTION(CallInterfaceMethod),
// ★ script 侧会先拿到正式 interface type，再挂上统一的接口调用桥

// AngelscriptClassGenerator.cpp
2797:            NewClass->SetSuperStruct(SuperClass);
2798:            NewClass->ClassFlags |= CLASS_Interface | CLASS_Abstract;
2803:            // Create UFunctions for each interface method declaration
3362:        NewClass->ClassFlags |= CLASS_Interface | CLASS_Abstract;
3363:        // Do NOT set CLASS_Native — this makes GetInterfaceAddress() return this (PointerOffset=0)
// ★ interface 最终落成真正的 UInterface metadata，并显式约束 PointerOffset 策略

5135:            ImplementedInterface.Class = InterfaceClass;
5137:            ImplementedInterface.PointerOffset = 0;
5138:            ImplementedInterface.bImplementedByK2 = true;
5160:        // Verify that the implementing class provides all methods required by its interfaces
5177:                if (ImplFunc == nullptr || bResolvedToInterfaceStub)
5179:                    FAngelscriptEngine::Get().ScriptCompileError(
5181:                        FString::Printf(TEXT("Class %s implements interface %s but is missing required method '%s'."),
// ★ 实现类如果漏方法，不等运行时调用，直接在 module swap 前失败
```

新增对比结论：

- `UInterface` 支持形态上，slua 不是“没有实现”，而是“实现方式不同”：它把接口主要放在 `FInterfaceProperty` 编组器里（`LuaObject.cpp:1869-1875, 2596-2611, 3010, 3038`），而 Angelscript 把 interface 做成正式脚本类型和类生成合同（`AngelscriptPreprocessor.cpp:1089-1157`; `AngelscriptClassGenerator.cpp:2764-2804, 5060-5183`）。
- 维护性上存在“实现质量差异”：slua 的接口错误主要在调用边界才暴露；Angelscript 会在实现类缺方法时直接 `ScriptCompileError`，更适合做静态审计和 CI 失败前置。
- 热路径上也有取舍差异：slua 每次写回接口参数都要再做一次 `GetInterfaceAddress()`（`LuaObject.cpp:2599-2611`），而 Angelscript 通过 `PointerOffset = 0` 和预注册 interface 方法把接口语义前移到了生成阶段（`AngelscriptClassGenerator.cpp:3362-3364, 5135-5138`）。这更接近“桥接层按次适配” vs “类型系统预建模”。

### [维度 D1 / D3] slua 的作者模型依赖一组预制宿主壳类；Angelscript 的作者模型是“每个脚本类生成自己的 UClass”

继续顺着 `ILuaOverriderInterface` 往外看，一个此前没有明确写出来的事实是：slua 的脚本作者入口其实是**有限宿主壳类族**。按 `Public/*.h` 检索，当前公开实现 `ILuaOverriderInterface` 的类型集中在 `ALuaActor / ULuaActorComponent / ALuaCharacter / ALuaGameMode / ALuaGameState / ALuaLevelScriptActor / ALuaPawn / ALuaPlayerController / ALuaPlayerState / ULuaObject / ULuaUserWidget`。这些壳类的共同点不是“都能被 Lua 调”，而是都显式暴露了 `LuaFilePath` 和若干生命周期钩子，让项目作者通过 Blueprint 继承这些壳类来挂载脚本模块。

这会直接影响作者模型。以 `ALuaActor`、`ULuaActorComponent`、`ULuaUserWidget` 为例，前者把 `LuaNetSerialization`、tick 和 `LuaFilePath` 都做成壳类自带能力；组件在 `InitializeComponent()` 主动 `TryHook()`；`ULuaUserWidget` 则在 `Initialize()` / `BeginDestroy()` 两个 widget 生命周期上调用 Lua。也就是说，slua 的“Blueprint 交互”并不是让任意 `UClass` 自然长出脚本实现，而是让作者选一个插件预制壳类，再把 Lua 模块路径灌进去。

Angelscript 则相反。预处理器会解析任意 `class / struct / interface` 声明和继承列表；类生成器随后为每个 `ClassDesc` 分配新的 `UASClass`，把 `ScriptType` 直接挂到这个新类上。它当然也提供了若干 base class 方便接 Unreal 生命周期，但“生成类”本身并不依赖一个有限壳类清单。差异不在于“谁能做更多类型”，而在于**slua 是 curated host shells，Angelscript 是 per-script generated class**。

```
[D1/D3] Authoring Entry Model
sluaunreal
├─ derive from ALuaActor / ULuaUserWidget / ...    // 先选插件预制宿主壳类
├─ set LuaFilePath                                 // 用属性或 BP 覆写指定模块
├─ shell lifecycle calls TryHook / CallLuaFunction // 壳类负责接入生命周期
└─ runtime patches the UObject instance            // 最终仍是对象级 hook

Angelscript
├─ parse `class MyType : Parent, IMyInterface`     // 直接解析脚本类声明
├─ allocate UASClass for this ClassDesc            // 每个脚本类各自生成 UClass
├─ attach ScriptTypePtr to generated class         // 脚本类型与 UClass 直接绑定
└─ optional base classes are helpers, not gate     // base class 是便利层，不是唯一入口
```

[1] 关键源码：slua 的公开作者入口是一组手工维护的宿主壳类

```cpp
// ============================================================================
// [1]
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/*.h
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaActor.h
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaActorComponent.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaUserWidget.cpp
// 函数: 宿主壳类声明与生命周期接入
// 位置: `rg -n --glob '*.h' "ILuaOverriderInterface"` 结果；
//       LuaActor.h:10-33; LuaActorComponent.cpp:4-42; LuaUserWidget.cpp:16-50
// 位置说明: slua 的作者入口是若干显式壳类，每个壳类自己决定如何在 UE 生命周期里接 Lua
// ============================================================================
// Public/*.h 检索结果（节选）
Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaActor.h:11:class SLUA_UNREAL_API ALuaActor : public AActor, public ILuaOverriderInterface
Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaActorComponent.h:10:class SLUA_UNREAL_API ULuaActorComponent : public UActorComponent, public ILuaOverriderInterface
Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaCharacter.h:10:class SLUA_UNREAL_API ALuaCharacter : public ACharacter, public ILuaOverriderInterface
Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaGameMode.h:10:class SLUA_UNREAL_API ALuaGameMode : public AGameMode, public ILuaOverriderInterface
Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaGameState.h:10:class SLUA_UNREAL_API ALuaGameState : public AGameState, public ILuaOverriderInterface
Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaLevelScriptActor.h:10:class SLUA_UNREAL_API ALuaLevelScriptActor : public ALevelScriptActor, public ILuaOverriderInterface
Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaPawn.h:10:class SLUA_UNREAL_API ALuaPawn : public APawn, public ILuaOverriderInterface
Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaPlayerController.h:10:class SLUA_UNREAL_API ALuaPlayerController : public APlayerController, public ILuaOverriderInterface
Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaPlayerState.h:10:class SLUA_UNREAL_API ALuaPlayerState : public APlayerState, public ILuaOverriderInterface
Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaUEObject.h:8:class SLUA_UNREAL_API ULuaObject : public UObject, public ILuaOverriderInterface
Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaUserWidget.h:22:class SLUA_UNREAL_API ULuaUserWidget : public UUserWidget, public ILuaOverriderInterface
// ★ 当前公开入口就是这些宿主壳类，作者通常从这里挑 Blueprint 父类

// LuaActor.h
10:UCLASS(BlueprintType, Blueprintable)
11:class SLUA_UNREAL_API ALuaActor : public AActor, public ILuaOverriderInterface
16:    virtual FString GetLuaFilePath_Implementation() const override;
17:    virtual void PostInitializeComponents() override;
22:    UFUNCTION(Blueprintcallable)
23:        void RegistLuaTick(float TickInterval);
30:    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
32:    UPROPERTY(Replicated)
33:        FLuaNetSerialization LuaNetSerialization;
// ★ Actor 壳类自带 tick、复制承载体和 LuaFilePath authoring surface

// LuaActorComponent.cpp
4:ULuaActorComponent::ULuaActorComponent(...)
8:    bWantsInitializeComponent = true;
20:FString ULuaActorComponent::GetLuaFilePath_Implementation() const
38:void ULuaActorComponent::InitializeComponent()
42:    TryHook();
// ★ 组件壳类直接把 hook 时机固定在 InitializeComponent

// LuaUserWidget.cpp
16:bool ULuaUserWidget::Initialize()
23:        if (GetSelfTable().isValid())
25:            CallLuaFunction(TEXT("Initialize"));
32:void ULuaUserWidget::BeginDestroy()
37:        auto OnDestroyFunc = SelfTable.getFromTable<NS_SLUA::LuaVar>("OnDestroy");
44:            NS_SLUA::Log::Error("ULuaUserWidget[%s] missing OnDestroy function [%s]!", ...)
// ★ UserWidget 壳类把 Lua 生命周期合同写死在 Initialize/BeginDestroy
```

[2] 关键源码：Angelscript 为每个脚本类单独生成 `UASClass`，不是依赖固定壳类库存

```cpp
// ============================================================================
// [2]
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 函数: parse class inheritance / allocate generated class
// 位置: AngelscriptPreprocessor.cpp:784-819;
//       AngelscriptClassGenerator.cpp:2581-2590, 3668-3682
// 位置说明: 先解析脚本类的继承与接口列表，再为该类分配独立 UASClass
// ============================================================================
// AngelscriptPreprocessor.cpp
784:    // Determine the direct superclass of this type
785:    ClassDesc->SuperClass = MatchClass.GetCaptureGroup(5);
800:    // Parse implemented interfaces from the inheritance list
801:    // Syntax: class MyClass : BaseClass, IMyInterface, IOtherInterface { ... }
819:                    ClassDesc->ImplementedInterfaces.Add(InterfaceName);
// ★ 脚本类声明本身就是类型来源，不依赖预制的宿主壳类名单

// AngelscriptClassGenerator.cpp
2581:    UASClass* NewClass = NewObject<UASClass>(
2582:        FAngelscriptEngine::GetPackage(),
2583:        UASClass::StaticClass(),
2584:        FName(*UnrealName),
2585:        RF_Public | RF_Standalone | RF_MarkAsRootSet
2586:    );
2588:    asITypeInfo* ScriptType = ClassDesc->ScriptType;
2590:        ScriptType->SetUserData(NewClass);
// ★ 每个脚本类都会生成自己的 UASClass，并与 ScriptType 双向挂接

3668:    NewClass->SetPropertiesSize(PropertiesSize);
3673:    NewClass->MinAlignment = MinAlignment;
3675:    NewClass->ScriptPropertyOffset = ParentCodeClass->GetPropertiesSize();
3676:    NewClass->ScriptTypePtr = ScriptType;
3677:    NewClass->CodeSuperClass = ParentCodeClass;
3680:    ClassDesc->ScriptType = ScriptType;
3681:    ClassDesc->Class = NewClass;
// ★ 生成后的类是正式 UClass 资产，可继续参与 Blueprint parent / reload / reflection 工作流
```

新增对比结论：

- 作者入口上是“实现方式不同”，不是简单的“一个有、一个没有”：slua 通过有限宿主壳类集合承载脚本（`Public/*.h` 中 `ILuaOverriderInterface` 的实现列表）；Angelscript 通过 `ClassDesc -> UASClass` 生成链把脚本类本身变成 `UClass`（`AngelscriptPreprocessor.cpp:784-819`; `AngelscriptClassGenerator.cpp:2581-2590, 3676-3681`）。
- slua 壳类模式的优势是常见 Actor/Widget/Component 生命周期已经帮作者接好，接入成本低；代价是新增一种“可脚本宿主类型”往往要再维护一个新的壳类。Angelscript 则把类型扩展成本更多压在 class generation 和 bind 合同上。
- 这也解释了两者 Blueprint 交互语义差异：slua 更像“Blueprint 继承插件宿主壳类，再挂 Lua 模块”；Angelscript 更像“脚本类本身就是 Blueprint parent 候选”。二者都是实现，不是简单优劣。

### [维度 D2 / D3] 合同错误暴露时机不同：slua 多在运行时发现，Angelscript 尽量在编译/生成阶段前置失败

如果只看功能表，很容易把 slua 和 Angelscript 都写成“支持接口、支持 Blueprint 互通”。但从工程化角度看，更关键的差异是**错误什么时候暴露**。slua 的大量合同是惰性的：`FuncMap` 只有第一次按名字查 Lua 函数时才填充；`IsLuaFunctionExist()` 需要运行到对象实例上才知道函数是否真的存在；`checkUInterfaceProperty()` 要等参数真正传错时才 `luaL_error`；`ULuaUserWidget::BeginDestroy()` 甚至到销毁阶段才发现 `OnDestroy` 没实现。它的好处是灵活，模块可以晚绑定、实例可以动态变路径；代价是很多错误天然更靠后。

Angelscript 则明显更偏前置失败。实现接口时，类生成器会先解析接口名、递归补齐父接口，再在 module swap 之前逐个检查 `FindFunctionByName()` 是否真的找到了对应实现；不合法接口名和缺失方法都会直接 `ScriptCompileError` 并标记 `bModuleSwapInError`。这让它在 CI、批量编译和大规模重载时更可审计，但也意味着作者更早面对“必须把合同补齐”的硬约束。

```
[D2/D3] Contract Failure Timing
sluaunreal
├─ lazy FuncMap lookup                             // 第一次按名字调用才知道 Lua 函数是否存在
├─ interface mismatch -> luaL_error               // 参数传入时才验证 InterfaceClass
└─ widget OnDestroy checked at BeginDestroy       // 生命周期末端仍可能暴露缺方法

Angelscript
├─ resolve interface class names first             // 生成前先确认 interface 是否有效
├─ verify every required UFunction                 // 缺实现时直接 ScriptCompileError
└─ block module swap on contract failure           // 错误前置到编译/生成阶段
```

[1] 关键源码：slua 的函数与接口合同主要在运行时兑现

```cpp
// ============================================================================
// [1]
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaOverriderInterface.h
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaObject.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaUserWidget.cpp
// 函数: GetCachedLuaFunc / IsLuaFunctionExist / checkUInterfaceProperty / BeginDestroy
// 位置: LuaOverriderInterface.h:30-55;
//       LuaObject.cpp:2596-2611;
//       LuaUserWidget.cpp:32-45
// 位置说明: 缺函数、错接口、缺析构回调都要等运行到具体对象和调用点才暴露
// ============================================================================
// LuaOverriderInterface.h
30:    NS_SLUA::LuaVar GetCachedLuaFunc(..., const FString& FunctionName) {
36:        auto luaFuncPtr = FuncMap.Find(FunctionName);
39:            return FuncMap.Add(FunctionName, getFromTableIndex<NS_SLUA::LuaVar>(L, selfTable, FunctionName));
48:    bool IsLuaFunctionExist(const FString& FunctionName) {
54:        return GetCachedLuaFunc(nullptr, selfTable, FunctionName).isFunction();
// ★ 函数存在性是实例级、按名字、按需求值的，不是编译期就固定

// LuaObject.cpp
2599:        UObject* obj = LuaObject::checkUD<UObject>(L, i);
2600:        void* interfacePtr = obj ? obj->GetInterfaceAddress(p->InterfaceClass) : nullptr;
2608:            luaL_error(L, "arg %d expect interface class of %s, but got %s", i,
// ★ interface 合法性在调用边界才检查；错了直接抛 Lua runtime error

// LuaUserWidget.cpp
32:void ULuaUserWidget::BeginDestroy()
37:        auto OnDestroyFunc = SelfTable.getFromTable<NS_SLUA::LuaVar>("OnDestroy");
38:        if (OnDestroyFunc.isFunction())
44:            NS_SLUA::Log::Error("ULuaUserWidget[%s] missing OnDestroy function [%s]!", ...)
// ★ 生命周期函数合同甚至可以晚到 BeginDestroy 才发现缺失
```

[2] 关键源码：Angelscript 把 interface 合同尽量前移到编译/类生成阶段

```cpp
// ============================================================================
// [2]
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 函数: resolve interface class / verify required methods
// 位置: AngelscriptClassGenerator.cpp:5060-5183
// 位置说明: 无效接口名和缺失实现都在 module swap 前被阻断
// ============================================================================
5060:    if (ClassDesc->ImplementedInterfaces.Num() > 0 && !ClassDesc->bIsInterface)
5063:        auto ResolveInterfaceClass = [this](const FString& InterfaceName) -> UClass*
5098:            FString UnrealInterfaceName = InterfaceName;
5104:                if (It->GetName() == UnrealInterfaceName && It->HasAnyClassFlags(CLASS_Interface))
5142:        for (const FString& InterfaceName : ClassDesc->ImplementedInterfaces)
5152:                FAngelscriptEngine::Get().ScriptCompileError(
5154:                    FString::Printf(TEXT("Class %s implements %s, but it is not a valid UInterface."),
5156:                ModuleData.NewModule->bModuleSwapInError = true;
// ★ 接口名解析失败，不等运行时 cast 或调用，直接中止模块换入

5160:        // Verify that the implementing class provides all methods required by its interfaces
5167:            for (TFieldIterator<UFunction> FuncIt(InterfaceClass, EFieldIteratorFlags::ExcludeSuper); FuncIt; ++FuncIt)
5173:                UFunction* ImplFunc = NewClass->FindFunctionByName(InterfaceFunc->GetFName());
5177:                if (ImplFunc == nullptr || bResolvedToInterfaceStub)
5179:                    FAngelscriptEngine::Get().ScriptCompileError(
5181:                        FString::Printf(TEXT("Class %s implements interface %s but is missing required method '%s'."),
5183:                    ModuleData.NewModule->bModuleSwapInError = true;
// ★ 实现类漏方法也在生成阶段直接失败，不把问题留到对象实例运行后
```

新增对比结论：

- 这里不是“有没有校验”的差异，而是“校验时机不同”：slua 主要在运行时调用边界发现问题（`LuaOverriderInterface.h:30-55`; `LuaObject.cpp:2596-2611`; `LuaUserWidget.cpp:32-45`），Angelscript 主要在编译/类生成阶段阻断问题（`AngelscriptClassGenerator.cpp:5060-5183`）。
- 这属于“实现质量差异”，不等于 slua 设计错误。slua 的晚绑定更适合动态换模块和实例级路由；Angelscript 的前置失败更适合大规模项目和自动化验证。
- 如果后续 Angelscript 要吸收 slua 的经验，值得吸收的是“实例级灵活路由”；如果 slua 要吸收 Angelscript 的经验，最值得补的是“把关键合同错误前移成可批处理的生成期诊断”。

---
## 深化分析 (2026-04-09 07:55:34)

### [维度 D4 / D3] `bindOverrideFuncs()` 对存量对象是“一次性建档”；模块重新取字节不等于已绑定实例自动换实现

前文已经写过 slua 的绑定单位可以下沉到对象实例。本轮补到更细后，可以把它和“热更新”明确切开：`LuaOverrider::bindOverrideFuncs()` 一旦发现当前对象已经在 `objectTableMap` 里有 `luaSelfTable`，就直接 `return true`，不会再次 `requireModule()`，也不会重跑模块的 `__call` 构造器。后续 `CallLuaFunction*()` 仍然经由 `GetSelfTable()` 和 `GetCachedLuaFunc()` 读取这个已建好的 `self table`（`LuaOverriderInterface.h:126-147`）。

这意味着，哪怕宿主项目自己去失效 `package.loaded`、重新下载模块字节，**已存在对象**也不会因为模块源变了就自动切到新实现。对 slua core 而言，真正的失效点只有两个：对象销毁时 `removeObjectTable()`，或 Lua state 关闭时 `onLuaStateClose()` 清掉 `FuncMap` 和 `objectTableMap`。所以它更像“对象接管生命周期”，不是“模块事务生命周期”。

```
[D4/D3] Existing Instance vs Module Refresh
sluaunreal
├─ object first bind
│  ├─ requireModule(luaFilePath)                   // 首次取模块
│  ├─ module.__call -> luaSelfTable                // 构造实例 self table
│  └─ objectTableMap[obj] = luaSelfTable           // 绑定到具体对象
├─ later module bytes changed
│  ├─ getObjectLuaTable(obj) hit                   // 已有表命中
│  └─ bindOverrideFuncs() returns early            // 不重建 self table
└─ actual invalidation points
   ├─ NotifyUObjectDeleted -> removeObjectTable    // 对象销毁才解绑
   └─ onLuaStateClose -> FuncMap.Empty             // VM 关闭才清缓存

Angelscript
├─ file/module change enters reload state          // 先进入 module reload 状态机
├─ structural diff raises ReloadReq                // 结构变化提升 reload 等级
└─ module swap / full reload decision              // 再决定是否交换新旧模块
```

[1] 关键源码：slua 对象一旦拥有 `luaSelfTable`，后续不会在 `bindOverrideFuncs()` 中自动重建

```cpp
// ============================================================================
// [1]
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaOverrider.cpp
// 函数: LuaOverrider::bindOverrideFuncs / ULuaOverrider::onLuaStateClose /
//       ULuaOverrider::removeObjectTable / LuaOverrider::NotifyUObjectDeleted
// 位置: LuaOverrider.cpp:1183-1185, 1302-1303, 344-359, 423-447, 643-646
// 位置说明: 首次建表后直接短路；真正的失效点只在对象删除或 LuaState 关闭
// ============================================================================
1183:        NS_SLUA::LuaVar* selfTable = ULuaOverrider::getObjectLuaTable(obj, L);
1184:        if (selfTable) {
1185:            return true;
1186:        }
// ★ 已存在 object -> selfTable 映射时，bindOverrideFuncs 直接退出，不会再次 requireModule

1302:        setmetatable(luaSelfTable, (void*)obj, bNetReplicated);
1303:        ULuaOverrider::addObjectTable(L, obj, luaSelfTable, bHookInstancedObj);
// ★ 首次绑定后，把实例 self table 固定挂进 objectTableMap

344:void ULuaOverrider::onLuaStateClose(NS_SLUA::lua_State* L)
346:    if (objectTableMap.Contains(L))
354:            ILuaOverriderInterface* overrideInterface = Cast<ILuaOverriderInterface>(obj);
356:            overrideInterface->FuncMap.Empty();
359:        objectTableMap.Remove(L);
// ★ VM 关闭时才批量清函数缓存和对象表

423:void ULuaOverrider::removeObjectTable(UObject* obj)
429:        auto objTable = tableMap.Find(obj);
433:            if (table->isValid())
437:                lua_pushstring(L, SLUA_CPPINST);
438:                lua_pushnil(L);
443:            tableMap.Remove(obj);
447:    NS_SLUA::LuaNet::removeObjectTable(obj);
// ★ 对象销毁/解绑时才把 C++ 实例引用从 Lua 表里摘掉

643:    void LuaOverrider::NotifyUObjectDeleted(const UObjectBase* Object, int32 Index)
646:        ULuaOverrider::removeObjectTable(obj);
// ★ 对象生命期结束时进入真正解绑路径
```

[2] 关键源码：Angelscript 的 reload ownership 是模块级状态机，不是对象级 self table 缓存

```cpp
// ============================================================================
// [2]
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_module.h
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 函数: asCModule reload state / structural change reload requirement
// 位置: as_module.h:294-305; AngelscriptClassGenerator.cpp:1077-1083, 1107-1114
// 位置说明: 先记录模块 reload 状态，再按结构差异提升到 full reload
// ============================================================================
// as_module.h
294:	enum class EReloadState
295:	{
296:		None,
297:		UpdateReferences,
298:		RecompiledOnlyCodeChanges,
299:		RecompiledWithStructuralChanges,
300:		QueuedForCompilation,
301:	};
303:	EReloadState ReloadState = EReloadState::None;
304:	asCModule* ReloadOldModule = nullptr;
305:	asCModule* ReloadNewModule = nullptr;
// ★ reload 所有权放在 module 层，天然可以表示 old/new module 关系

// AngelscriptClassGenerator.cpp
1079:		// If our superclass changed, we need a full reload
1080:		if (ClassData.OldClass->SuperClass != ClassData.NewClass->SuperClass)
1082:			if (ClassData.ReloadReq < EReloadRequirement::FullReloadRequired)
1083:				ClassData.ReloadReq = EReloadRequirement::FullReloadRequired;
// ★ 结构变化先被提升为 full reload requirement

1107:					// If the definition has changed, we must do a full reload
1108:					if (!PropertyDesc->IsDefinitionEquivalent(*OldPropertyDesc))
1110:						if (ClassData.ReloadReq < EReloadRequirement::FullReloadRequired)
1112:							ClassData.ReloadReq = EReloadRequirement::FullReloadRequired;
// ★ 不是“对象先跑起来再说”，而是 module/class 交换前先决定 reload 等级
```

补充判断：

- `存量实例随模块刷新自动换实现`：slua 插件 core 上当前是 `没有实现`。源码证据是 `bindOverrideFuncs()` 在 `getObjectLuaTable()` 命中后直接返回（`LuaOverrider.cpp:1183-1185`），真正失效只在对象销毁或 state 关闭（`LuaOverrider.cpp:344-359,423-447,643-646`）。
- `实例级路由能力`：这是 `实现方式不同`。slua 明确把脚本 ownership 放在对象和 `luaSelfTable` 上；Angelscript 把 ownership 放在 module/class reload 状态机上（`as_module.h:294-305`; `AngelscriptClassGenerator.cpp:1077-1083,1107-1114`）。
- `热更新可预测性`：这里存在 `实现质量差异`。slua 的优势是实例级灵活挂接，但“模块变了哪些老对象会继续跑旧实现”需要额外项目规则；Angelscript 的优势是 reload 决策更早、更可审计。

### [维度 D11 / D8] 即使项目自己启用 `.luac`，slua 的一致性校验也只到 Lua VM ABI 头，不到工程 build 身份

前文已经确认：slua runtime 主链没有把 `onlyluac` 包成正式部署策略。本轮再往 Lua VM 里钻后，可以把边界说得更精确一些：**就算宿主项目自己调用了 `lua_setonlyluac()`，`.luac` 被校验的也只是 Lua 字节码头兼容性，而不是项目构建身份。**

`lundump.cpp` 的 `checkHeader()` 明确只检查 `LUAC_VERSION`、`LUAC_FORMAT`、`LUAC_DATA`、`sizeof(int / Instruction / lua_Integer / lua_Number)`、endianness 和 float format。它不知道 UE build 配置、脚本来源模块、项目版本号，也不知道脚本内容 hash。对比之下，Angelscript 的 precompiled pipeline 至少有三层额外身份治理：按 build 选择 `PrecompiledScript_{Build}.Cache`，装载时先过 `BuildIdentifier`，启用静态 JIT 时还要让 `PrecompiledDataGuid` 对上；真正复用某个 module 之前，还会继续比较 `CodeHash`。

所以，这里不是“slua 完全没有校验”，而是**校验层级更低**：它校验的是“这段 bytes 能不能被当前 Lua VM 解开”，不是“这段 bytes 是否属于当前项目/当前构建/当前脚本版本”。

```
[D11/D8] Compiled Script Validation Granularity
slua `.luac`
├─ luaL_loadbuffer()                               // runtime 统一入口
└─ luaU_undump()
   ├─ LUAC_VERSION / LUAC_FORMAT                   // Lua 版本与格式
   ├─ sizeof(int / Instruction / number types)     // ABI 尺寸
   └─ endianness / float format                    // 平台兼容性

Angelscript `PrecompiledScript*.Cache`
├─ choose file by UE build config                  // Shipping/Test/Development
├─ IsValidForCurrentBuild()                        // BuildIdentifier gate
├─ PrecompiledDataGuid vs compiled JIT guid        // 二进制匹配 gate
└─ CompiledModule->CodeHash == Module->CodeHash    // 模块内容 gate
```

[1] 关键源码：slua `.luac` 校验停在 Lua VM 的 binary chunk header

```cpp
// ============================================================================
// [1]
// 文件: Reference/sluaunreal/Plugins/slua_unreal/External/lua/ldo.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/External/lua/lundump.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaState.cpp
// 函数: f_parser / checkHeader / LuaState::loader
// 位置: ldo.cpp:760-775; lundump.cpp:235-250; LuaState.cpp:131-145
// 位置说明: 入口始终是 loadbuffer；binary chunk 的校验只覆盖 Lua VM ABI 头
// ============================================================================
// ldo.cpp
760:static void f_parser (lua_State *L, void *ud) {
763:  if (L->onlyluac == 0) {
765:    if (c == LUA_SIGNATURE[0]) {
767:      cl = luaU_undump(L, p->z, p->name);
768:    } else {
770:      cl = luaY_parser(L, p->z, &p->buff, &p->dyd, p->name, c);
772:  } else {
773:    checkmode(L, p->mode, "binary");
774:    cl = luaU_undump(L, p->z, p->name);
775:  }
// ★ 就算打开 onlyluac，也只是强制走 luaU_undump 的 binary 路径

// lundump.cpp
235:static void checkHeader (LoadState *S) {
236:  checkliteral(S, LUA_SIGNATURE + 1, "not a");
237:  if (LoadByte(S) != LUAC_VERSION)
238:    error(S, "version mismatch in");
239:  if (LoadByte(S) != LUAC_FORMAT)
240:    error(S, "format mismatch in");
241:  checkliteral(S, LUAC_DATA, "corrupted");
242:  checksize(S, int);
244:  checksize(S, Instruction);
245:  checksize(S, lua_Integer);
246:  checksize(S, lua_Number);
247:  if (LoadInteger(S) != LUAC_INT)
248:    error(S, "endianness mismatch in");
249:  if (LoadNumber(S) != LUAC_NUM)
250:    error(S, "float format mismatch in");
// ★ 校验内容是 Lua chunk header 与 ABI，不含项目 build/hash/module 身份

// LuaState.cpp
131:    int LuaState::loader(lua_State* L) {
135:        TArray<uint8> buf = state->loadFile(fn, filepath);
139:            if(luaL_loadbuffer(L,(const char*)buf.GetData(),buf.Num(),chunk)==0) {
140:                return 1;
// ★ runtime 入口只把宿主给的 bytes 喂进 Lua VM
```

[2] 关键源码：Angelscript 对预编译制品做 build、GUID 和 module hash 三层治理

```cpp
// ============================================================================
// [2]
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp
// 函数: precompiled data identity / load gating
// 位置: PrecompiledData.h:423-470,568-609; PrecompiledData.cpp:1417-1423,2620-2649;
//       AngelscriptEngine.cpp:1519-1555,4283-4290; StaticJITHeader.cpp:25-30
// 位置说明: build 配置、DataGuid、CodeHash 和编译进二进制的 JIT GUID 都进入正式加载链
// ============================================================================
// PrecompiledData.h / PrecompiledData.cpp
423:struct FAngelscriptPrecompiledModule
432:	int64 CodeHash = 0;
455:		Ar << Data.CodeHash;
568:	FGuid DataGuid;
578:	int32 BuildIdentifier = -1;
589:		Ar << Data.DataGuid;
608:	int32 GetCurrentBuildIdentifier();
609:	bool IsValidForCurrentBuild();

1420:	CodeHash = Context.ModuleDesc->CodeHash;
2620:	DataGuid = FGuid::NewGuid();
2642:bool FAngelscriptPrecompiledData::IsValidForCurrentBuild()
2644:	return BuildIdentifier == GetCurrentBuildIdentifier() && BuildIdentifier != -1;
2649:	BuildIdentifier = GetCurrentBuildIdentifier();
// ★ precompiled data 自带 build 身份和 module 内容 hash

// AngelscriptEngine.cpp / StaticJITHeader.cpp
1521:		Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript_Shipping.Cache");
1525:		Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript_Development.Cache");
1536:			if (!PrecompiledData->IsValidForCurrentBuild())
1552:				if (CompiledInfo != nullptr && CompiledInfo->PrecompiledDataGuid != PrecompiledData->DataGuid)
// ★ 载入时先过 build gate，再过 JIT GUID gate

4286:		const FAngelscriptPrecompiledModule* CompiledModule = PrecompiledData->Modules.Find(Module->ModuleName);
4290:			if (CompiledModule->CodeHash == Module->CodeHash)
// ★ 真正复用模块前，还要确认当前源码 hash 与 cache 中一致

25:FStaticJITCompiledInfo::FStaticJITCompiledInfo(FGuid Guid)
26:	: PrecompiledDataGuid(Guid)
29:	checkf(ActiveInfo == nullptr, TEXT("Only one angelscript static JIT info can be compiled in!"))
// ★ 编进二进制的 JIT 代码也带自己的 precompiled data GUID
```

补充判断：

- `binary chunk 兼容性校验`：slua 这里不是 `没有实现`，而是 `实现方式不同`。它校验 `LUAC_VERSION / format / ABI / endianness`（`lundump.cpp:235-250`），解决的是“当前 Lua VM 能否解包”。
- `项目级 build / module 身份校验`：slua 插件 core 当前是 `没有实现`。本轮所读正式执行链里没有 `BuildIdentifier`、`DataGuid` 或 module hash 等对等治理；Angelscript 则把这些字段纳入正式加载链（`PrecompiledData.h:432,569,578`; `PrecompiledData.cpp:1420,2620-2649`; `AngelscriptEngine.cpp:1536-1555,4286-4290`）。
- `部署制品可追责性`：这里有明显 `实现质量差异`。slua 更像“VM 可接受什么字节码”，Angelscript 更像“当前项目是否应该接受这份预编译制品”。

### [维度 D2 / D3 / D8] slua 把 latent / RPC 语义留在运行时桥；Angelscript 更早把语义前移到类型与函数元数据

前文已经证明 `LuaFunctionAccelerator` 会缓存参数布局，但本轮继续下钻后可以看到一个更关键的设计分界：在 slua 里，反射桥不只负责“把 Lua 参数搬进 `FProperty`”，它还负责**把 Unreal 运行时语义临时拼出来**。遇到 `FLatentActionInfo` 参数时，它在调用现场创建 `ULatentDelegate`、分配线程引用、拼出一个新的 `FLatentActionInfo`；遇到 `FUNC_Net` 时，它在调用现场跑 `GetFunctionCallspace()`，再决定 `CallRemoteFunction()` 和本地 `Invoke()` 的先后。

Angelscript 则更倾向把这些语义前移。`Bind_FLatentActionInfo.cpp` 直接把 `FLatentActionInfo` 注册成正式脚本类型；类生成器在生成 `UFunction` 时就写入 `FUNC_NetMulticast / FUNC_NetClient / FUNC_NetServer / FUNC_NetValidate / FUNC_NetReliable`；`Bind_BlueprintType.cpp` 绑定时按这些 flags 决定走 event 还是 callable。差异不在“有没有支持 latent/RPC”，而在**语义放在哪一层建模**。

```
[D2/D3/D8] Where Unreal Semantics Are Materialized
sluaunreal
Lua call
└─ LuaFunctionAccelerator::call
   ├─ detect FLatentActionInfo property            // 调用时才识别 latent
   ├─ create ULatentDelegate + threadRef           // 现场拼回调对象
   ├─ detect FUNC_Net                              // 调用时才识别网络函数
   ├─ GetFunctionCallspace / CallRemoteFunction    // 现场分派 remote/local
   └─ Invoke()                                     // 最终仍走 UFunction

Angelscript
script type / generated UFunction
├─ Bind_FLatentActionInfo                          // 先把 latent struct 建成正式类型
├─ ClassGenerator writes FUNC_Net* flags           // 生成期写入网络语义
└─ Bind_BlueprintType chooses bind path            // 绑定期按 flags 决定调用形态
```

[1] 关键源码：slua 在 runtime bridge 里现场合成 latent 与 net 语义

```cpp
// ============================================================================
// [1]
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaFunctionAccelerator.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LatentDelegate.cpp
// 函数: LuaFunctionAccelerator::call / ULatentDelegate::getThreadRef
// 位置: LuaFunctionAccelerator.cpp:252-317; LatentDelegate.cpp:27-46
// 位置说明: latent callback 与 RPC callspace 都在调用热路径里现场拼装
// ============================================================================
// LuaFunctionAccelerator.cpp
252:            if (checkerInfo.bLatent)
253:            {
254:                // bind a callback to the latent function
255:                lua_State* mainThread = L->l_G->mainthread;
257:                ULatentDelegate* latentObj = LuaObject::getLatentDelegate(mainThread);
258:                int threadRef = latentObj->getThreadRef(L);
259:                FLatentActionInfo LatentActionInfo(threadRef, GetTypeHash(FGuid::NewGuid()),
260:                                                   *ULatentDelegate::NAME_LatentCallback, latentObj);
262:                prop->CopySingleValue(prop->ContainerPtrToValuePtr<void>(params), &LatentActionInfo);
263:                isLatentFunction = true;
264:            }
// ★ latent 不是预先建好的脚本合同，而是调用时临时合成 FLatentActionInfo

284:        if (funcFlag & FUNC_Net)
285:        {
289:            int32 functionCallspace = obj->GetFunctionCallspace(func, &newStack);
293:            if (functionCallspace & FunctionCallspace::Remote)
297:                obj->CallRemoteFunction(func, params, newStack.OutParms, &newStack);
300:            if (functionCallspace & FunctionCallspace::Local)
310:                func->Invoke(obj, newStack, returnValueAddress);
313:        }
// ★ 网络语义同样在桥接层现场分派 remote/local，不是静态 stub

// LatentDelegate.cpp
27:void ULatentDelegate::OnLatentCallback(int32 threadRef)
29:    luaState->resumeThread(threadRef);
37:int ULatentDelegate::getThreadRef(NS_SLUA::lua_State *L)
41:    int threadRef = luaState->findThread(L);
43:        threadRef = luaState->addThread(L);
// ★ latent 回调最终靠 Lua thread ref 恢复协程执行
```

[2] 关键源码：Angelscript 把 latent / net 语义前移成正式类型和 `UFunction` 元数据

```cpp
// ============================================================================
// [2]
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FLatentActionInfo.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp
// 函数: latent type bind / generated net flags / bind route selection
// 位置: Bind_FLatentActionInfo.cpp:5-24; AngelscriptClassGenerator.cpp:3455-3482;
//       Bind_BlueprintType.cpp:741-755
// 位置说明: 先定义类型和函数 flags，再由绑定层按 flags 选路径
// ============================================================================
// Bind_FLatentActionInfo.cpp
5:AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_FLatentActionInfo((int32)FAngelscriptBinds::EOrder::Late + 1, []
7:	auto FLatentActionInfo_ = FAngelscriptBinds::ExistingClass("FLatentActionInfo");
9:	FLatentActionInfo_.Constructor("void f(int32 InLinkage, int32 InUUID, const FName InFunctionName, UObject InCallbackTarget)",
21:	FLatentActionInfo_.Property("int32 Linkage", &FLatentActionInfo::Linkage);
23:	FLatentActionInfo_.Property("FName ExecutionFunction", &FLatentActionInfo::ExecutionFunction);
24:	FLatentActionInfo_.Property("UObject unresolved_object CallbackTarget", &FLatentActionInfo::CallbackTarget);
// ★ latent action info 先被建成正式脚本类型，而不是调用时临时拼一个匿名桥对象

// AngelscriptClassGenerator.cpp
3455:		if (FunctionDesc->bBlueprintCallable && !FunctionDesc->bIsPrivate)
3463:		if (FunctionDesc->bNetMulticast)
3464:			NewFunction->FunctionFlags |= FUNC_NetMulticast;
3465:		if (FunctionDesc->bNetClient)
3466:			NewFunction->FunctionFlags |= FUNC_NetClient;
3467:		if (FunctionDesc->bNetServer)
3468:			NewFunction->FunctionFlags |= FUNC_NetServer;
3469:		if (FunctionDesc->bNetValidate)
3471:			NewFunction->FunctionFlags |= FUNC_NetValidate;
3478:		if ((NewFunction->FunctionFlags & FUNC_NetFuncFlags) != 0)
3480:			NewFunction->FunctionFlags |= FUNC_Net;
3481:			if (!FunctionDesc->bUnreliable)
3482:				NewFunction->FunctionFlags |= FUNC_NetReliable;
// ★ 网络语义在生成 UFunction 时就固化成 flags

// Bind_BlueprintType.cpp
741:		for (auto& DBFunc : DBBind.Methods)
743:			UFunction* Function = Class->FindFunctionByName(*DBFunc.UnrealPath);
747:			if (Function->HasAnyFunctionFlags(FUNC_BlueprintEvent | FUNC_NetFuncFlags))
748:				BindBlueprintEvent(ClassType.ToSharedRef(), Function, DBFunc);
753:			else
754:				BindBlueprintCallable(ClassType.ToSharedRef(), Function, DBFunc);
// ★ 绑定阶段直接用 flags 决定事件路径，不把 net 语义留到每次调用时推断
```

补充判断：

- `latent 支持`：slua 这里不是 `没有实现`，而是 `实现方式不同`。它通过 `LuaFunctionAccelerator::call()` + `ULatentDelegate` 在调用时现场拼装 `FLatentActionInfo`（`LuaFunctionAccelerator.cpp:252-263`; `LatentDelegate.cpp:27-46`）；Angelscript 把 `FLatentActionInfo` 做成正式绑定类型（`Bind_FLatentActionInfo.cpp:5-24`）。
- `RPC / Net 语义落点`：同样是 `实现方式不同`。slua 在调用热路径里读 `FUNC_Net` 并执行 `GetFunctionCallspace()` / `CallRemoteFunction()`（`LuaFunctionAccelerator.cpp:284-313`）；Angelscript 在类生成阶段就把 `FUNC_Net*` flags 写入 `UFunction`（`AngelscriptClassGenerator.cpp:3463-3482`），绑定阶段再按 flags 选 event 路径（`Bind_BlueprintType.cpp:747-754`）。
- `热路径负担`：这里有 `实现质量差异`。slua 的优势是通用动态桥一处兜底，覆盖面高；代价是 latent/RPC 语义继续留在每次调用的桥接热路径。Angelscript 的优势是把更多语义前移到生成期和类型层，调用面更接近“执行已建好的合同”。

---

## 深化分析 (2026-04-09 08:04:44)

### [维度 D8 / D2] slua 的 GC 优化对象是“桥接产生的 userdata / struct”，Angelscript 更早把引用关系写进 UE GC schema

前文已经反复讨论 slua 的调用层优化，但这一轮往内存所有权继续下钻后，可以看到另一个清晰边界：slua 重点优化的不是“Lua VM 自己怎么收循环引用”，而是**桥接层制造出来的 `LuaStruct` / userdata 何时释放、每帧最多释放多少**。`LuaStruct` 本身就是 `FGCObject`；`__gc` 路径并不总是当场 `delete`，而是可以把 struct 壳对象塞进 `LuaState::deferGCStruct`，随后在 `tickGC()` 里按 `slua.GCStructTimeLimit` 预算慢慢清。也就是说，slua 把 GC 负担视为桥接热路径的延迟成本管理问题。

Angelscript 的落点不同。它在引擎初始化阶段直接 `SetEngineProperty(asEP_AUTO_GARBAGE_COLLECT, 0)`，然后在类生成阶段为“没有显式 `UPROPERTY`、但 GC 需要看见”的脚本属性补隐藏 `UPROPERTY`，再由 `DetectAngelscriptReferences()` 把剩余引用关系编进 `UASClass::ReferenceSchema`。这意味着 Angelscript 更倾向于**让 UE 的对象图在类型生成期就看见引用关系**，而不是在桥对象析构时再做预算化清理。

```
[D8/D2] Memory Ownership Strategy
sluaunreal
Lua userdata / LuaStruct
├─ __gc on userdata                                // Lua 侧触发析构入口
├─ DeferGCStruct ?                                 // 可选延迟释放
│  └─ LuaState.deferGCStruct queue                 // 进入每帧预算队列
└─ tickGC(time budget)                             // 按时间片慢慢 delete

Angelscript
generated script type / property
├─ disable AS auto GC                              // 不让脚本 VM 自己偷跑 GC
├─ add hidden UPROPERTY if needed                  // 先把 UE GC 看得见的属性补齐
└─ build UE::GC::ReferenceSchema                   // 剩余引用关系写入 schema
```

[1] 关键源码：slua 的桥接对象释放被拆成“立即删除”与“预算化延迟删除”两段

```cpp
// ============================================================================
// [1]
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaObject.h
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaObject.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaState.cpp
// 函数: LuaStruct / gcStruct / LuaState::tickGC
// 位置: LuaObject.h:96-145; LuaObject.cpp:48-53,2857-2874; LuaState.cpp:48-56,346-420
// 位置说明: slua 把桥接层结构体壳对象做成 FGCObject，并允许把真正 delete 延迟到每帧预算里
// ============================================================================
// LuaObject.h
96:    struct SLUA_UNREAL_API LuaStruct : public FGCObject {
97:        uint8* buf;
98:        uint32 size;
105:        struct FLuaNetSerializationProxy* proxy;
108:        bool isRef;
114:        virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
128:            return "LuaStruct";
131:    };
136:    #define UD_AUTOGC 1 // ★ userdata 允许 Lua 侧触发 __gc
144:    #define UD_VALUETYPE 1<<9 // ★ value type 不按指针缓存，强调桥对象所有权

// LuaObject.cpp
48:static int32 DeferGCStruct = 1;
50:    TEXT("slua.DeferGCStruct"),
52:    TEXT("Set defer gc struct is enabled.\n"),
// ★ `DeferGCStruct` 是公开开关，说明延迟释放是正式策略而不是临时 hack

2857:        QUICK_SCOPE_CYCLE_COUNTER(Stat_GCUStruct);
2860:        if (!userdata->parent && !(userdata->flag & UD_HADFREE))
2861:            releaseLink(L, ls->buf);
2866:        if (DeferGCStruct && !ls->isRef)
2868:            LuaState* luaState = LuaState::get(L);
2869:            luaState->deferGCStruct.Add(ls);
2870:        }
2871:        else
2872:        {
2873:            delete ls;
2874:        }
// ★ 非引用 struct 可以不当场释放，而是塞进 LuaState 的延迟回收队列

// LuaState.cpp
48:    const int MaxLuaGCCount = 8192;
50:    static float GCStructTimeLimit = 0.001f;
53:        TEXT("slua.GCStructTimeLimit"),
55:        TEXT("Defer gc struct time limit in one frame.\n"),
// ★ 回收策略直接暴露成“每帧时间预算”

346:    void LuaState::tickGC(float dtime) {
347:        if (stepGCTimeLimit > 0.0) {
355:                int runtimes = 0;
358:                for (double start = FPlatformTime::Seconds(), now = start; stepCount < stepGCCountLimit &&
359:                    now - start + stepCost < stepGCTimeLimit; stepCount++)
361:                    if (lua_gc(L, LUA_GCSTEP, 0)) {
362:                        lastFullGCSeconds = FPlatformTime::Seconds();
// ★ Lua VM 自己的 step GC 也按步数和时间预算驱动

403:            QUICK_SCOPE_CYCLE_COUNTER(Lua_DeferGCStruct)
405:            for (double start = FPlatformTime::Seconds(), now = start; now - start + stepCost < GCStructTimeLimit;)
407:                if (deferGCStruct.Num() == 0)
412:                auto luaStruct = deferGCStruct[0];
413:                deferGCStruct.RemoveAtSwap(0);
414:                delete luaStruct;
// ★ 延迟队列里的桥对象继续按 `GCStructTimeLimit` 慢慢出队
```

[2] 关键源码：Angelscript 把 GC 可见性前移到引擎配置和类生成阶段

```cpp
// ============================================================================
// [2]
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 函数: engine GC config / hidden property generation / DetectAngelscriptReferences
// 位置: AngelscriptEngine.cpp:884-890; AngelscriptClassGenerator.cpp:295-339,4859-4925
// 位置说明: Angelscript 关闭脚本 VM 自动 GC，并把引用可见性收敛到生成期类型元数据
// ============================================================================
// AngelscriptEngine.cpp
884:	Engine->SetEngineProperty(asEP_ALLOW_UNSAFE_REFERENCES, 1);
888:	Engine->SetEngineProperty(asEP_OPTIMIZE_BYTECODE, 1);
889:	Engine->SetEngineProperty(asEP_AUTO_GARBAGE_COLLECT, 0);
// ★ 不让 AngelScript runtime 自动 GC；后续引用治理由插件自己整合到 UE 体系

// AngelscriptClassGenerator.cpp
295:	// Some properties without a UPROPERTY() should be added as
296:	// hidden properties, for garbage collection purposes.
303:		FAngelscriptTypeUsage PropertyType = PropertyTypes[Elem.Value];
305:		if (ClassData.NewClass->bIsStruct)
306:			bShouldMakeProperty = !PropertyType.NeverRequiresGC();
308:		if (PropertyType.RequiresProperty())
309:			bShouldMakeProperty = true;
318:		if (!PropertyType.CanCreateProperty())
321:			FAngelscriptEngine::Get().ScriptCompileError(...);
328:		// Add new property
336:		if (AngelscriptSettings->bMarkNonUpropertyPropertiesAsTransient || !ClassData.NewClass->bIsStruct)
339:			PropDesc->bTransient = true;
// ★ 先补隐藏 UPROPERTY，让 UE GC / 序列化系统能看见本来只存在于脚本里的引用

4859:void FAngelscriptClassGenerator::DetectAngelscriptReferences(TSharedPtr<FAngelscriptClassDesc> ClassDesc)
4867:	UE::GC::FSchemaBuilder Schema(0);
4870:	FAngelscriptType::FGCReferenceParams RefParams;
4875:	Schema.Append(Class->ReferenceSchema.Get());
4878:	for (int32 i = 0, PropertyCount = ScriptType->GetPropertyCount(); i < PropertyCount; ++i)
4908:		if (!bAddedAsUnrealProperty)
4910:			if(PropertyType.HasReferences())
4912:				RefParams.AtOffset = PropertyOffset;
4914:				PropertyType.EmitReferenceInfo(RefParams);
4920:	const bool bOverrideReferenceSchema = Schema.NumMembers() != NumPreviousMembers || NumPreviousMembers == 0;
4923:		UE::GC::FSchemaView View(Schema.Build(GetARO(Class)), UE::GC::EOrigin::Other);
4924:		Class->ReferenceSchema.Set(View);
// ★ 没被补成 UPROPERTY 的剩余引用，也会被写进 UE::GC schema，而不是等桥对象析构时兜底
```

补充判断：

- `桥接壳对象的预算化释放`：slua 这里不是 `没有实现`，而是 `实现方式不同`。它显式管理 `LuaStruct` 等桥对象，并用 `GCStructTimeLimit` 控制每帧释放预算（`LuaObject.cpp:2866-2874`; `LuaState.cpp:403-414`）。
- `类型级引用可见性`：Angelscript 在这一点上有明显的 `实现质量差异`。隐藏 `UPROPERTY` + `UE::GC::ReferenceSchema` 让引用关系更早进入 UE 的正式对象图（`AngelscriptClassGenerator.cpp:295-339,4859-4925`）。
- `性能取舍`：slua 的好处是桥接层成本可被细粒度调参；代价是桥对象生命周期与类型合同分离。Angelscript 的好处是 GC 合同更靠近类型系统；代价是类生成器必须承担更多静态分析复杂度。

### [维度 D2 / D3] slua 的 `import()` 是运行时名字查找器，不是编译期类型账本

前文多数对比都落在 `LuaWrapper`、`UFunction` 和 override 路径上，但还有一条容易被忽略的类型暴露入口：slua 的脚本里可以直接 `import("Foo.Bar")`。本轮沿着这条链看下去，发现它并不对应某种“预先生成的声明文件”，而是**一次运行时名字查找 + 弱引用缓存**。`LuaState::import()` 先查 `cacheImportedMap`，未命中时再 `FindObject` / `LoadObject`，最后把 `UClass`、`UScriptStruct`、`UEnum` 压回 Lua；`onEngineGC()` 则在 UE GC 后清掉失效的弱引用。

Angelscript 的同类能力更像编译期账本。`FAngelscriptEngine` 长期维护 `ActiveClassesByName / ActiveEnumsByName / ActiveDelegatesByName`，`GetClass()` / `GetEnum()` / `GetDelegate()` 优先查这些由 active module 构成的名字表；即便没有热重载索引，也是在 `ActiveModules` 里遍历结构化描述，而不是去 `FindObject` 全局对象表碰运气。两者差异不在“能不能按名字拿类型”，而在**名字解析发生在运行时反射表，还是发生在插件维护的脚本类型账本**。

```
[D2/D3] Type Name Resolution
sluaunreal
Lua script
└─ import("Foo")
   ├─ cacheImportedMap hit?                        // 先查弱引用缓存
   ├─ FindObject / LoadObject                      // 再查 UE 全局对象表
   └─ pushClass / pushStruct / pushEnum            // 运行时把结果压回 Lua

Angelscript
compiler / runtime query
├─ ActiveClassesByName / ActiveEnumsByName         // 插件自持名字账本
├─ fallback scan ActiveModules                     // 再遍历 module 描述
└─ return ClassDesc / EnumDesc / DelegateDesc      // 返回结构化脚本元数据
```

[1] 关键源码：slua 的 `import()` 直接依赖 UE 反射对象表和弱缓存

```cpp
// ============================================================================
// [1]
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaState.cpp
// 函数: LuaState::import / LuaState::onEngineGC
// 位置: 158-236, 712-718
// 位置说明: slua 的类型名字解析是在运行时完成的，缓存项也跟着 UE GC 生命周期失效
// ============================================================================
158:    int LuaState::import(lua_State *L) {
159:        const char* name = LuaObject::checkValue<const char*>(L, 1);
161:            LuaState* state = LuaState::get(L);
162:            ImportedObjectCache* cacheImportedItem = state->cacheImportedMap.Find(name);
163:            if (cacheImportedItem)
165:                UObject* cacheObj = cacheImportedItem->cacheObjectPtr.Get();
167:                    switch (cacheImportedItem->importedType)
171:                        UClass* uclass = Cast<UClass>(cacheObj);
173:                            LuaObject::pushClass(L, uclass);
180:                        UScriptStruct* ustruct = Cast<UScriptStruct>(cacheObj);
182:                            LuaObject::pushStruct(L, ustruct);
189:                        UEnum* uenum = Cast<UEnum>(cacheObj);
191:                            LuaObject::pushEnum(L, uenum);
// ★ 命中缓存时，直接把弱引用对象重新压回 Lua，不经过任何脚本类型校验

207:            FString path = UTF8_TO_TCHAR(name);
208:            if (!FindObject<UObject>(AnyPackage, *path)) {
210:                LoadObject<UObject>(NULL, *path);
211:            }
213:            UClass* uclass = FindObject<UClass>(AnyPackage, *path);
216:                state->cacheImportedMap.Add(name, ImportedObjectCache {uclass, ImportedClass});
219:            UScriptStruct* ustruct = FindObject<UScriptStruct>(AnyPackage, *path);
222:                state->cacheImportedMap.Add(name, ImportedObjectCache {ustruct, ImportedStruct});
226:            UEnum* uenum = FindObject<UEnum>(AnyPackage, *path);
229:                state->cacheImportedMap.Add(name, ImportedObjectCache{ uenum, ImportedEnum });
233:            luaL_error(L, "Can't find class named %s", name);
// ★ 未命中缓存时，解析直接落到 UE 全局对象表；失败也是运行时报错

712:    void LuaState::onEngineGC()
715:        for (CacheImportedMap::TIterator it(cacheImportedMap); it; ++it)
716:            if (!it.Value().cacheObjectPtr.IsValid())
717:                it.RemoveCurrent();
// ★ 缓存一致性依赖 UE GC 之后的弱引用失效，而不是依赖脚本模块编译账本
```

[2] 关键源码：Angelscript 的名字解析优先走 active module 账本

```cpp
// ============================================================================
// [2]
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: ActiveClassesByName / GetClass / GetEnum / GetDelegate
// 位置: AngelscriptEngine.h:333-341,385-392; AngelscriptEngine.cpp:4775-4887
// 位置说明: Angelscript 把类型、枚举、委托名字绑定到 active module ledger，而不是直接查 UE 全局对象表
// ============================================================================
// AngelscriptEngine.h
333:	TSharedPtr<struct FAngelscriptClassDesc> GetClass(const FString& ClassName, TSharedPtr<struct FAngelscriptModuleDesc>* FoundInModule = nullptr);
334:	TSharedPtr<struct FAngelscriptEnumDesc> GetEnum(const FString& EnumName, TSharedPtr<struct FAngelscriptModuleDesc>* FoundInModule = nullptr);
335:	TSharedPtr<struct FAngelscriptDelegateDesc> GetDelegate(const FString& DelegateName, TSharedPtr<struct FAngelscriptModuleDesc>* FoundInModule = nullptr);
385:	TMap<FString, TSharedRef<struct FAngelscriptModuleDesc>> ActiveModules;
389:	TMap<FStringView, TPair<TSharedPtr<struct FAngelscriptModuleDesc>, TSharedPtr<struct FAngelscriptClassDesc>>> ActiveClassesByName;
390:	TMap<FStringView, TPair<TSharedPtr<struct FAngelscriptModuleDesc>, TSharedPtr<struct FAngelscriptEnumDesc>>> ActiveEnumsByName;
391:	TMap<FStringView, TPair<TSharedPtr<struct FAngelscriptModuleDesc>, TSharedPtr<struct FAngelscriptDelegateDesc>>> ActiveDelegatesByName;
// ★ 名字账本和 module ledger 是插件正式状态的一部分

// AngelscriptEngine.cpp
4775:TSharedPtr<FAngelscriptClassDesc> FAngelscriptEngine::GetClass(const FString& ClassName, TSharedPtr<FAngelscriptModuleDesc>* FoundInModule)
4778:	if (ActiveClassesByName.Num() != 0)
4780:		auto* FoundEntry = ActiveClassesByName.Find(ClassName);
4783:			if (FoundInModule != nullptr)
4784:				*FoundInModule = FoundEntry->Key;
4785:			return FoundEntry->Value;
// ★ 热重载索引存在时，名字解析直接命中插件自己的 active class map

4794:	for (auto ModulePair : ActiveModules)
4797:		for (auto Class : Module->Classes)
4799:			if(Class->ClassName == ClassName)
4802:					*FoundInModule = Module;
4803:				return Class;
// ★ 即便退回慢路径，也是在 module 描述里遍历，而不是去 UObject 全局命名空间探测

4813:TSharedPtr<FAngelscriptEnumDesc> FAngelscriptEngine::GetEnum(const FString& EnumName, TSharedPtr<FAngelscriptModuleDesc>* FoundInModule)
4816:	if (ActiveEnumsByName.Num() != 0)
4851:TSharedPtr<FAngelscriptDelegateDesc> FAngelscriptEngine::GetDelegate(const FString& DelegateName, TSharedPtr<FAngelscriptModuleDesc>* FoundInModule)
4854:	if (ActiveDelegatesByName.Num() != 0)
// ★ class / enum / delegate 三类名字都走同一套 ledger 思维
```

补充判断：

- `按名字拿类型`：slua 这里不是 `没有实现`，而是 `实现方式不同`。它通过 `import()` 动态查找 UE 反射对象并做弱缓存（`LuaState.cpp:158-236,712-718`）。
- `类型合同的可预测性`：Angelscript 在这里有 `实现质量差异`。类型名先落进 `ActiveClassesByName` 等 ledger，再关联具体 module，可追踪性更强（`AngelscriptEngine.h:385-392`; `AngelscriptEngine.cpp:4778-4805`）。
- `混合工程维护成本`：slua 的优点是接 Unreal 原生对象最直接，项目里新增 `UClass` 后脚本可晚绑定；代价是错误更靠运行时暴露，而且缓存一致性依赖 `onEngineGC()`。Angelscript 的优点是编译/热重载过程更早发现名称冲突与所属 module。

### [维度 D4 / D1] slua 的编辑器“热体验”其实是 `EditorPreview` scratch VM；Angelscript 把 reload 状态提升成可导出的正式数据

前文已经把 slua 与 Angelscript 的 loader / watcher 差异写得很清楚，这一轮补的是更底层的“开发期状态所有权”。`LuaSimulate` 并不是去接管正在运行的主 `LuaState`；它只在 `EditorPreview` world 里工作，`PreBeginPIE` 时先停掉，发现可 hook 的 preview 对象且 `BegunPlay` 后再临时创建一个新的 `LuaState`，复用外部注入的同一个 `LoadFileDelegate`，最后只对当前 preview 对象做 `hookObject(..., true)`。这本质上是**编辑器预览专用 scratch VM**。

Angelscript 这边，本轮看到一个 slua 没有直接对等的工程化点：reload 状态被当成正式可观测数据输出。`DumpHotReloadState()` 会把 `FileChangesDetectedForReload` / `FileDeletionsDetectedForReload` 导成 `HotReloadState.csv`，`AngelscriptEditorStateDump.cpp` 还会把 class / enum / struct / delegate 的 reload 结果写成 `EditorReloadState.csv`。这说明 Angelscript 不只是“能 reload”，而是**把 reload 过程的状态外露成审计面**。

```
[D4/D1] Dev-Time State Ownership
sluaunreal
EditorPreview object
├─ PreBeginPIE -> StopSimulateLua()                // 进入 PIE 前清空 scratch VM
├─ NotifyUObjectCreated(EditorPreview)             // 只看预览世界对象
├─ StartSimulateLua()                              // 新建独立 LuaState
└─ hookObject(SluaState, Obj, true)                // 只接管当前预览对象

Angelscript
script file / reload planner
├─ FileChangesDetectedForReload                    // 正式 reload 队列
├─ DumpHotReloadState.csv                          // 公开待重载文件状态
└─ EditorReloadState.csv                           // 公开 class/enum/struct reload 映射
```

[1] 关键源码：slua 的编辑器模拟使用独立 scratch VM，并且只服务 `EditorPreview`

```cpp
// ============================================================================
// [1]
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaSimulate.cpp
// 函数: LuaSimulate::OnStartupModule / NotifyUObjectCreated / StartSimulateLua / StopSimulateLua
// 位置: 24-37, 39-79, 98-118
// 位置说明: 编辑器模拟路径不是原位热替换，而是针对预览对象临时拉起一个独立 LuaState
// ============================================================================
24:    void LuaSimulate::OnStartupModule()
26:        PIEHandle = FEditorDelegates::PreBeginPIE.AddRaw(this, &LuaSimulate::OnPreBeginPIE);
34:    void LuaSimulate::OnPreBeginPIE(const bool bIsSimulating)
36:        StopSimulateLua();
// ★ 一进入 PIE，先把编辑器模拟状态整包清掉

39:    void LuaSimulate::NotifyUObjectCreated(const class UObjectBase* Object, int32 Index)
47:        UWorld* World = Outer->GetWorld();
52:        bool Preview = World->WorldType == EWorldType::EditorPreview;
53:        if (!Preview)
55:            return;
57:        if (!LuaOverrider::isHookable(Obj))
62:        bool BegunPlay = World->GetBegunPlay();
66:        if (!BegunPlay)
68:            if (SimulatingObj && SimulatingObj->GetClass() == Obj->GetClass())
70:                StopSimulateLua();
74:        if (!Obj->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
76:            StartSimulateLua();
77:            NS_SLUA::LuaState::hookObject(SluaState, Obj, true);
78:            SimulatingObj = Obj;
// ★ 只有 EditorPreview 且 BeginPlay 后的对象才会触发模拟；并且只 hook 当前预览实例

98:    void LuaSimulate::StartSimulateLua()
100:        if (Delegate == nullptr)
102:            Log::Error("lua Simulation Error. LoadFileDelegate not set.");
105:        StopSimulateLua();
106:        SluaState = new NS_SLUA::LuaState("", nullptr);
107:        SluaState->setLoadFileDelegate(Delegate);
108:        SluaState->init();
// ★ 这里不是复用主状态机，而是每次新建独立 LuaState，并复用同一个 bytes loader ABI

111:    void LuaSimulate::StopSimulateLua()
113:        if (SluaState != nullptr)
115:            SluaState->close();
116:            delete SluaState;
117:            SluaState = nullptr;
118:            SimulatingObj = nullptr;
// ★ scratch VM 的退出语义就是直接 close + delete
```

[2] 关键源码：Angelscript 把 reload 队列和重载结果都导出成正式状态表

```cpp
// ============================================================================
// [2]
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorStateDump.cpp
// 函数: DumpHotReloadState / SaveEditorReloadState
// 位置: AngelscriptStateDump.cpp:981-1014; AngelscriptEditorStateDump.cpp:21-67,105-119
// 位置说明: Angelscript 不只持有 reload planner，还把待重载文件和重载映射导出成 CSV
// ============================================================================
// AngelscriptStateDump.cpp
981:FAngelscriptStateDump::FTableResult FAngelscriptStateDump::DumpHotReloadState(FAngelscriptEngine& Engine, const FString& OutputDir)
990:	for (const FAngelscriptEngine::FFilenamePair& FilenamePair : Engine.FileChangesDetectedForReload)
993:			GetFilenamePairPath(FilenamePair),
994:			TEXT("PendingReload"),
999:	for (const FAngelscriptEngine::FFilenamePair& FilenamePair : Engine.FileDeletionsDetectedForReload)
1002:			GetFilenamePairPath(FilenamePair),
1003:			TEXT("PendingDeletion"),
1008:	FTableResult Result = SaveTable(OutputDir, TEXT("HotReloadState.csv"), Writer);
1011:		Result.Status = TEXT("PartialExport");
1012:		Result.ErrorMessage = TEXT("Private hot reload tracking data is not exported; only public reload queues are included.");
// ★ 即使不导出全部私有状态，也会把公共 reload 队列明确写成表

// AngelscriptEditorStateDump.cpp
21:	void SaveEditorReloadState(const FString& OutputDir)
30:		FClassReloadHelper::FReloadState& ReloadState = FClassReloadHelper::ReloadState();
31:		for (const TPair<UClass*, UClass*>& ReloadClass : ReloadState.ReloadClasses)
33:			Writer.AddRow({ TEXT("ReloadClass"), GetObjectName(ReloadClass.Key), GetObjectName(ReloadClass.Value) });
36:		for (UClass* NewClass : ReloadState.NewClasses)
38:			Writer.AddRow({ TEXT("NewClass"), FString(), GetObjectName(NewClass) });
41:		for (UEnum* ReloadEnum : ReloadState.ReloadEnums)
47:		for (UEnum* NewEnum : ReloadState.NewEnums)
52:		for (const TPair<UScriptStruct*, UScriptStruct*>& ReloadStruct : ReloadState.ReloadStructs)
57:		for (const TPair<UDelegateFunction*, UDelegateFunction*>& ReloadDelegate : ReloadState.ReloadDelegates)
62:		const FString Filename = FPaths::Combine(OutputDir, TEXT("EditorReloadState.csv"));
105:	void DumpEditorState(const FString& OutputDir)
107:		SaveEditorReloadState(OutputDir);
118:			OutHandle = FAngelscriptStateDump::OnDumpExtensions.AddStatic(&DumpEditorState);
// ★ class / enum / struct / delegate 的 reload 结果都能进入 state dump 扩展
```

补充判断：

- `开发期预览入口`：slua 这里不是 `没有实现`，而是 `实现方式不同`。它通过 `LuaSimulate` 为 `EditorPreview` 世界起一个独立 scratch VM（`LuaSimulate.cpp:24-37,39-79,98-118`）。
- `reload 状态可审计性`：Angelscript 在这里存在 `实现质量差异`。待重载文件和 editor reload 映射都能被正式导出（`AngelscriptStateDump.cpp:981-1014`; `AngelscriptEditorStateDump.cpp:21-67`）。
- `状态所有权差异`：slua 的 preview ownership 绑定在“当前预览对象 + 临时 LuaState”上；Angelscript 的 reload ownership 绑定在“文件队列 + 重载映射表”上。这不是简单的“一个能 hot reload、一个不能”，而是两种完全不同的开发期状态模型。

---
## 深化分析 (2026-04-09 08:13:42)

### [维度 D4 / D11] slua 把 `socket.core + pb.*` 下沉进 VM 启动链；Angelscript 当前的网络依赖主要服务调试与本地制品链

前文已经把 slua 的 `LoadFileDelegate -> package.searchers[2]` 讲清楚；这一轮补的是更靠近“线上工作流底座”的一层。继续顺着 `LuaState::init()` 往下读，可以看到 slua 在 VM 启动期不仅接入宿主提供的脚本 bytes loader，还会立刻执行 `InitExtLib(L)`、`LuaProtobuf::init(L)`、`SluaUtil::openLib(L)` 和 `LuaProfiler::init(this)`。其中 `InitExtLib()` 把 `socket.core` 直接写入 `package.preload`，`LuaProtobuf::init()` 再把 `pb / pb.slice / pb.buffer / pb.conv` 注册成可 `require` 的模块。再结合 `CMakeLists.txt` 中随 runtime 一起参与构建的 `http.lua.inc / socket.lua.inc / mime.lua.inc / url.lua.inc / ltn12.lua.inc`，可以确认 slua 官方插件至少把“联网原语 + 协议编解码原语”产品化到了 runtime 层。

这和前文“slua 没有内建完整热更发布系统”并不矛盾。更准确的结论是：slua 没有把 CDN、manifest、签名、回滚策略做进插件，但它确实把这些上层工作流经常依赖的 transport building blocks 预装进了 Lua 运行时。`LuaProfiler.inl` 直接 `require("socket.core").tcp()` 连到 `8081`，正是这条设计的直接消费方。换句话说，slua 在部署/热更新维度上已经产品化了**运行时底层原语**，只是没有产品化**发布协议**。

Angelscript 这一侧，本轮能直接确认的 `Networking / Sockets` 主要落在 `FAngelscriptDebugServer` 的 TCP listener 上；脚本正式暴露出来的交付路径则还是 `FFileHelper` 的本地文件 I/O 和 `PrecompiledScript.Cache` 的二进制制品读写。也就是说，两者的差异不是“一个支持在线、一个不支持在线”，而是**在线工作流的哪一层被插件正式拥有**。slua 更早把 transport primitive 下沉进 VM；Angelscript 更早把 build-aware cache、debug server 和本地制品一致性做成正式能力。

```
[D4/D11] Runtime Transport Ownership
sluaunreal
├─ package.searchers[2] = loader                   // 宿主提供脚本 bytes
├─ InitExtLib() -> preload["socket.core"]          // 预装 TCP 原语
├─ LuaProtobuf::init() -> pb / pb.slice / ...      // 预装协议编解码原语
├─ CMakeLists.txt ships http/socket/mime Lua code  // 仓库内随 runtime 构建
└─ LuaProfiler.inl -> require("socket.core").tcp() // Lua 层直接联网

Angelscript
├─ Bind_FFileHelper                                // 脚本侧正式入口是本地文件 I/O
├─ PrecompiledData::Save/Load                      // 正式制品是本地 cache
├─ FAngelscriptDebugServer(Port)                   // 网络依赖主要服务 debugger
└─ runtime script transport module not found       // 本轮源码范围未见对等预装模块
```

[1] 关键源码：slua 的 VM 启动序列会同时装配 loader、`socket.core`、`pb.*` 和 profiler

```cpp
// ============================================================================
// [1]
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaState.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaProtobufWrap.cpp
// 函数: LuaState::init / LuaState::InitExtLib / LuaProtobuf::init
// 位置: LuaState.cpp:603-630,1315-1332; LuaProtobufWrap.cpp:23-34
// 位置说明: slua 不只是插入宿主 loader，还把 socket 与 protobuf 模块接进同一条 VM bootstrap
// ============================================================================
// LuaState.cpp
603:        lua_pushcfunction(L,loader);
606:        lua_getglobal(L,"package");
607:        lua_getfield(L,-1,"searchers");
615:        lua_pushvalue(L,loaderFunc);
616:        lua_rawseti(L,loaderTable,2);
// ★ 先把宿主提供的 bytes loader 接进标准 require 流程

619:        InitExtLib(L);
621:        LuaObject::init(L);
622:        LuaProtobuf::init(L);
623:        SluaUtil::openLib(L);
624:        LuaClass::reg(L);
630:        LuaProfiler::init(this);
// ★ loader / socket / protobuf / utility / profiler 属于同一个启动链

1315:    void LuaState::InitExtLib(lua_State* ls)
1319:        lua_getglobal(ls, "package");
1320:        lua_getfield(ls, -1, "preload");
1322:        static const luaL_Reg s_lib_preload[] = {
1323:            { "socket.core", luaopen_socket_core },
1324:            { NULL, NULL }
1325:        };
1328:        for (lib = s_lib_preload; lib->func; lib++) {
1329:            lua_pushcfunction(ls, lib->func);
1330:            lua_setfield(ls, -2, lib->name);
1331:        }
// ★ `socket.core` 是 runtime 预置模块，不需要业务脚本自己再去 open 第三方库

// LuaProtobufWrap.cpp
23:        void init(lua_State *L) {
24:            luaL_requiref(L, "pb", luaopen_pb, 0);
27:            luaL_requiref(L, "pb.slice", luaopen_pb_slice, 0);
30:            luaL_requiref(L, "pb.buffer", luaopen_pb_buffer, 0);
33:            luaL_requiref(L, "pb.conv", luaopen_pb_conv, 0);
// ★ protobuf 相关模块也在 VM 启动时一并注册，说明 slua 对“在线数据协议原语”有显式产品化
```

[2] 关键源码：slua 的联网能力不只停留在 C++ 层，仓库还把 LuaSocket 资源和远端 profiler 消费链带进来了

```cpp
// ============================================================================
// [2]
// 文件: Reference/sluaunreal/Plugins/slua_unreal/CMakeLists.txt
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaProfiler.inl
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_profile/Private/slua_profile.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_profile/Private/slua_remote_profile.cpp
// 函数: runtime build list / profiler reconnect / profiler tab spawn / FProfileServer::Init
// 位置: CMakeLists.txt:52-98; LuaProfiler.inl:80-90,112-120; slua_profile.cpp:127-139; slua_remote_profile.cpp:22-30,52-60
// 位置说明: 仓库内明确携带 LuaSocket 资源，且 Lua 层会直接消费 `socket.core` 建立 TCP 连接
// ============================================================================
// CMakeLists.txt
52:#being lua socket
55:set (LUA_SOCKET_SRC_FILES
62:    ${LUA_SOCKET_SRC_PATH}/ftp.lua.inc
63:    ${LUA_SOCKET_SRC_PATH}/headers.lua.inc
64:    ${LUA_SOCKET_SRC_PATH}/http.lua.inc
69:    ${LUA_SOCKET_SRC_PATH}/ltn12.lua.inc
75:    ${LUA_SOCKET_SRC_PATH}/mime.lua.inc
81:    ${LUA_SOCKET_SRC_PATH}/smtp.lua.inc
83:    ${LUA_SOCKET_SRC_PATH}/socket.lua.inc
93:    ${LUA_SOCKET_SRC_PATH}/url.lua.inc
// ★ 官方 runtime 构建清单里明确带着 LuaSocket 的纯 Lua 资源，不只是 `socket.core` 的 C API

// LuaProfiler.inl
80:function this.reGetSock()
85:    if pcall(function() sock = require("socket.core").tcp() end) then
87:        sock:settimeout(ConnectTimeoutSec)
112:    local sockSuccess, status = sock:connect(connectHost, connectPort)
// ★ Lua 层脚本直接通过 runtime 预置的 `socket.core` 建立 TCP 连接

// slua_profile.cpp / slua_remote_profile.cpp
137:        sluaProfilerInspector->ProfileServer = MakeShareable(new NS_SLUA::FProfileServer());
138:        sluaProfilerInspector->ProfileServer->OnProfileMessageRecv().BindLambda([this](NS_SLUA::FProfileMessagePtr Message) {
22:FAutoConsoleVariableRef CVarSluaProfilerPort(
23:    TEXT("slua.ProfilerPort"),
24:    NS_SLUA::FProfileServer::Port,
30:    int32 FProfileServer::Port = 8081;
58:        Listener = new FTcpListener(ListenEndpoint);
59:        Listener->OnConnectionAccepted().BindRaw(this, &FProfileServer::HandleConnectionAccepted);
// ★ Editor 侧接收器与 Lua 侧 socket 连接拼成完整远端通道；这说明 transport primitive 已经被插件正式拥有
```

[3] 对照源码：Angelscript 当前把网络依赖主要用于 debugger，脚本交付仍围绕本地文件和 precompiled cache

```cpp
// ============================================================================
// [3]
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FFileHelper.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp
// 函数: module dependency / FAngelscriptDebugServer ctor / file helper bind / precompiled data save-load
// 位置: AngelscriptRuntime.Build.cs:45-53; AngelscriptDebugServer.cpp:402-409;
//      Bind_FFileHelper.cpp:32-49; PrecompiledData.cpp:2673-2683
// 位置说明: 本轮能直接确认的网络能力主要服务调试 transport；脚本正式交付链仍是本地文件与 cache 制品
// ============================================================================
// AngelscriptRuntime.Build.cs
45:			PrivateDependencyModuleNames.AddRange(new string[]
51:				"Networking",
52:				"Sockets",
// ★ runtime 确实依赖 Networking/Sockets，但下面要看这些依赖实际用在哪

// AngelscriptDebugServer.cpp
402:	FAngelscriptDebugServer::FAngelscriptDebugServer(FAngelscriptEngine* InOwnerEngine, int Port)
405:	Listener = new FTcpListener(FIPv4Endpoint(FIPv4Address::Any, Port));
406:	Listener->OnConnectionAccepted().BindRaw(this, &FAngelscriptDebugServer::HandleConnectionAccepted);
408:	UE_LOG(Angelscript, Log, TEXT("Angelscript debug server listening on %s"), *Listener->GetLocalEndpoint().ToText().ToString());
// ★ 当前直接可见的 socket 使用落在 debug server，而不是脚本侧 transport module

// Bind_FFileHelper.cpp
32:	FAngelscriptBinds::FNamespace ns("FFileHelper");
46:	FAngelscriptBinds::BindGlobalFunction("bool LoadFileToString(FString& Result, const FString& Filename, FFileHelper::EHashOptions HashOptions = FFileHelper::EHashOptions::None, uint32 ReadFlags = uint32(EFileRead::None))",
48:	FAngelscriptBinds::BindGlobalFunction("bool SaveStringToFile(const FString& String, const FString& Filename, FFileHelper::EEncodingOptions EncodingOptions = FFileHelper::EEncodingOptions::AutoDetect, uint32 WriteFlags = uint32(EFileWrite::None))",
// ★ 脚本侧正式暴露的是本地文件 I/O，而不是预置的网络下载模块

// PrecompiledData.cpp
2673:	Writer << *this;
2675:	FFileHelper::SaveArrayToFile(Data, *Filename);
2683:	FFileHelper::LoadFileToArray(LoadedData, *Filename);
// ★ 正式制品链围绕本地 `PrecompiledScript*.Cache`，和 slua 的 runtime transport primitive 不是同一层级
```

补充判断：

- `runtime-side transport primitive`：slua 这里不是 `没有实现`，而是 `实现方式不同`。它把 `socket.core` 和 `pb.*` 直接塞进 VM 启动链（`LuaState.cpp:619-623,1319-1331`; `LuaProtobufWrap.cpp:23-34`），并在仓库构建清单里携带 LuaSocket 资源（`CMakeLists.txt:55-98`）。
- `插件内建线上发布协议`：这里两边都应判为 `没有实现`。slua 虽然下沉了网络/协议原语，但当前源码仍只正式承诺 `module/path -> bytes` 的 loader 合约；Angelscript 则把交付物收敛成文件和 `PrecompiledScript*.Cache`，本轮未见对等 CDN/manifest/签名/回滚协议。
- `插件拥有的工程层级`：这是 `实现方式不同`，不是简单优劣。slua 更早产品化了“脚本运行时可直接联网/解码”的底层能力；Angelscript 更早产品化了 debugger、本地 cache 和 build-aware 制品链（`AngelscriptDebugServer.cpp:402-409`; `Bind_FFileHelper.cpp:46-49`; `PrecompiledData.cpp:2673-2683`）。
- 推断：如果 Angelscript 未来要补“线上脚本分发”能力，最值得参考的不是 slua 的 `LoadFileDelegate` 本身，而是它把 `socket/protobuf` 原语下沉进 VM 启动链的做法；但发布协议、验签和失败恢复仍然需要单独设计，而不能从 slua 当前插件源码里直接照搬。

---
## 深化分析 (2026-04-09 08:21:20)

### [维度 D2] `CppBinding` 的真正核心是“编译期签名展开器”，不是一堆注册宏

前文已经写过 `REG_EXTENSION_METHOD`、`LuaWrapper*.inc` 和 `extension map`。这一轮补的是更底层的一层：slua 的静态导出能力真正落点不在宏名字，而在 `LuaCppBinding<...>::LuaCFunction`、`FunctionBind`、`ArgOperator` 这组三段模板链。每个导出的 native 函数最终都收敛成一个 `lua_CFunction`，调用时再按编译期展开的参数包逐个 `readArg()`，最后把返回值 `push` 回 Lua 栈。

更关键的是，这条静态链不只处理普通 POD/`UObject` 值。`ArgOperator` 对 `TArray`、`TMap`、`TSet`、`enum` 和 `TFunction` 都有专门分支；其中 `TFunction` 分支会把 Lua 闭包包装成 C++ lambda。也就是说，slua 的 `CppBinding` 不是“只给 C++ 暴露几个快一点的 wrapper”，而是把 callback-bearing signature 也并入了同一条编译期绑定路径。

```
[D2] Static Binding Expansion
sluaunreal
├─ LuaCppBinding<Signature>::LuaCFunction          // 每个导出函数先变成一个 lua_CFunction
├─ FunctionBind<invoke, Offset>::Functor           // 通过 IntList 展开参数索引
├─ ArgOperator::readArg<T>()                       // 按 T 选择 checkValue/checkTArray/checkEnumValue
├─ TFunction -> makeTFunctionProxy()               // Lua 闭包再包成 C++ lambda
└─ LuaObject::push(ret)                            // 返回值重新压回 Lua 栈

Angelscript
├─ Signature.Declaration                           // 先保留正式声明字符串
├─ FGenericFuncPtr + FunctionCaller                // 指针擦除 + 调用器元数据
├─ BindGlobalFunction/BindMethodDirect             // 注册进 AS engine
└─ FunctionId / ModifyScriptFunction               // 继续挂文档、native form、后续账本
```

[1] 关键源码：slua 的 `CppBinding` 在模板层完成参数展开与返回值回推

```cpp
// ============================================================================
// [1]
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaCppBinding.h
// 函数: ArgOperator / FunctionBind / LuaCppBinding<...>::LuaCFunction
// 位置: 89-121, 183-221, 252-264
// 位置说明: 静态绑定的真实工作都在模板层完成，不依赖 UFunction 反射元数据
// ============================================================================
89:    struct ArgOperator {
92:        static typename std::enable_if<TIsTArray<T>::Value, T>::type readArg(lua_State * L, int p) {
93:            return LuaObject::checkTArray<T>(L, p);
97:        static typename std::enable_if<TIsTMap<T>::Value, T>::type readArg(lua_State * L, int p) {
98:            return LuaObject::checkTMap<T>(L, p);
107:        static typename std::enable_if<std::is_enum<T>::value, T>::type readArg(lua_State * L, int p) {
108:            return LuaObject::checkEnumValue<T>(L, p);
115:        static typename std::enable_if<TIsTFunction<T>::Value, T>::type readArg(lua_State * L, int p) {
116:            return LuaCallableBinding<T>::Prototype::makeTFunctionProxy(L, p);
117:        }
120:        static typename std::enable_if<!TIsTArray<T>::Value && !TIsTMap<T>::Value && !TIsTFunction<T>::Value && !std::is_enum<T>::value, T>::type readArg(lua_State * L, int p) {
121:            return LuaObject::checkValue<T>(L, p);
// ★ 参数类型分派是编译期完成的；运行时只按展开后的模板分支取值

183:    template <typename T,typename... Args,
184:          T (*target)(lua_State * L, void*, Args...),int Offset>
185:    struct FunctionBind<T (*)(lua_State * L, void* ,Args...), target, Offset, std::enable_if_t<!std::is_void<T>::value>> {
195:            static T invoke(lua_State * L,void* ptr) {
196:                return target(L, ptr, ArgOperator::readArg<typename remove_cr<Args>::type>(L, index + Offset)...);
197:            }
214:        static int invoke(lua_State * L,void* ptr) {
216:            using I = MakeIntList<sizeof...(Args)>;
217:            T ret = Functor<I>::invoke(L,ptr);
220:            return LuaObject::push(L,ret);
221:        }
// ★ 每次调用仍然要读 Lua 栈，但不再扫 UFunction/FProperty；参数槽位由模板展开直接决定

252:    template<typename RET,typename ...ARG,RET (*func)(ARG...),int Offset>
253:    struct LuaCppBinding< RET (*)(ARG...), func, Offset> {
257:        static RET invoke(lua_State* L,void* ptr,ARG&&... args) {
258:            return func( std::forward<ARG>(args)... );
261:        static int LuaCFunction(lua_State* L) {
262:            using f = FunctionBind<decltype(&invoke), invoke, Offset>;
263:            return f::invoke(L,nullptr);
264:        }
// ★ 导出的 free function 最终只是一个 `LuaCFunction -> FunctionBind -> 原生函数` 的薄包装
```

[2] 关键源码：`TFunction` 参数不是特例外挂，而是同一模板链内建的 callback 代理

```cpp
// ============================================================================
// [2]
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaCppBindingPost.h
// 函数: CallableExpand<...>::makeTFunctionProxy
// 位置: 29-46
// 位置说明: slua 直接把 Lua function 包装成 C++ lambda，供 CppBinding 形参消费
// ============================================================================
29:    template<typename CallableType, typename ReturnType, typename ... ArgTypes>
30:    typename CallableExpand<CallableType, ReturnType, ArgTypes...>::TFunctionType
31:    CallableExpand<CallableType, ReturnType, ArgTypes...>::makeTFunctionProxy(lua_State* L, int p)
32:    {
33:        luaL_checktype(L, p, LUA_TFUNCTION);
34:        LuaVar func(L, p);
35:        if (func.isValid() && func.isFunction())
36:        {
37:            return [=](ArgTypes&& ... args) -> ReturnType
38:            {
39:                LuaVar result = func.call(std::forward<ArgTypes>(args) ...);
40:                return resultCast<ReturnType>(MoveTemp(result));
41:            };
42:        }
45:            return nullptr;
46:    }
// ★ 这说明 callback 也走静态签名系统；只是 callback 执行本身仍然会回到 LuaVar 动态调用
```

[3] 对照源码：Angelscript 把“调用入口”做成 `declaration + erased ptr + caller` 的正式账本

```cpp
// ============================================================================
// [3]
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/FunctionCallers.h
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp
// 函数: ASAutoCaller::FunctionCaller / RedirectFunctionCaller / BindBlueprintCallable
// 位置: FunctionCallers.h:194-228,249-283,307-357;
//       Bind_BlueprintCallable.cpp:95-138
// 位置说明: Angelscript 也做模板展开，但它把调用器元数据和 FunctionId 正式留在注册账本里
// ============================================================================
// FunctionCallers.h
194:    struct FunctionCaller
196:        using FunctionCallerPtr = void(*)(TFunctionPtr Method, void** Parameters, void* ReturnValue);
197:        using MethodCallerPtr = void(*)(TMethodPtr Function, void** Parameters, void* ReturnValue);
214:        static FunctionCaller Make(FunctionCallerPtr InFunctionCaller)
217:            Caller.FuncPtr = InFunctionCaller;
222:        static FunctionCaller Make(MethodCallerPtr InMethodCaller)
225:            Caller.MethodPtr = InMethodCaller;
// ★ 这里不直接暴露给脚本 VM 原始 C++ 指针，而是再包一层 caller 元数据

249:    template<typename T>
250:    FORCEINLINE typename TEnableIf<TIsPointer<T>::Value, T>::Type PassArgument(void* Value)
252:        return *(T*)Value;
256:    FORCEINLINE typename TEnableIf<!TIsPointer<T>::Value, typename TRemoveReference<T>::Type&>::Type PassArgument(void* Value)
258:        return **(typename TRemoveReference<T>::Type**)Value;
281:    RedirectFunctionCaller(TFunctionPtr FunctionPtr, void** Arguments, void* ReturnValue)
283:        IndexedFunctionCaller<ReturnType, ParamTypes...>(FunctionPtr, Arguments, ReturnValue, TMakeIntegerSequence<int, sizeof...(ParamTypes)>());
355:    FunctionCaller MakeFunctionCaller(ReturnType(*FunctionPtr)(ParamTypes...))
357:        return FunctionCaller::Make(&RedirectFunctionCaller<ReturnType, ParamTypes...>);
// ★ 参数展开同样在模板层完成，但它发生在 `void**` 调用器模型里，便于后续 JIT/native form 继续复用

// Bind_BlueprintCallable.cpp
95:    asSFuncPtr ASFuncPtr;
98:    FMemory::Memcpy(&ASFuncPtr, DirectNativePointer, sizeof(asSFuncPtr));
107:            int GlobalFunctionId = FAngelscriptBinds::BindGlobalFunction(Signature.Declaration, ASFuncPtr, Entry->Caller);
108:            Signature.ModifyScriptFunction(GlobalFunctionId);
114:        int FunctionId = FAngelscriptBinds::BindGlobalFunction(Signature.Declaration, ASFuncPtr, Entry->Caller);
115:        Signature.ModifyScriptFunction(FunctionId);
120:        int FunctionId = FAngelscriptBinds::BindMethodDirect
124:            asCALL_CDECL_OBJFIRST, Entry->Caller
133:        int FunctionId = FAngelscriptBinds::BindMethodDirect
136:            Signature.Declaration, ASFuncPtr, asCALL_THISCALL, Entry->Caller
138:        Signature.ModifyScriptFunction(FunctionId);
// ★ Angelscript 的 direct bind 不是“绑定完就结束”，后面还有 FunctionId、文档、native form、precompiled data 可继续挂接
```

补充判断：

- `静态绑定的调用入口`：这是 `实现方式不同`。slua 直接产出 `lua_CFunction`；Angelscript 保留 `declaration + FunctionCaller + FunctionId` 账本（`LuaCppBinding.h:252-264`; `Bind_BlueprintCallable.cpp:107-138`）。
- `callback-bearing signature`：slua 在 `CppBinding` 主链里 `有实现`，而且是模板系统内建能力（`LuaCppBinding.h:115-117`; `LuaCppBindingPost.h:31-40`）。
- `绑定结果可审计性`：这里存在 `实现质量差异`。在本轮检查的 `CppBinding` 主链里，slua 更偏“宏位点 + 模板实例化”；Angelscript 则把 FunctionId、声明字符串、caller 元数据继续挂到后续体系中（`Bind_BlueprintCallable.cpp:107-138`; `FunctionCallers.h:194-228`）。

### [维度 D8] slua 的静态导出优化的是“跨语言入口”，不是 `StaticJIT` 那种 call-site lowering

前文已经比较过 `LuaFunctionAccelerator`、`Profiler` 和 `StaticJIT` 的宏观方向。这一轮补充的是热路径边界。`CppBinding` 的确能把 Lua -> native 的入口从 `UFunction` 反射改成模板展开后的直接调用，但一旦签名里出现 Lua callback，或者 native 又要反向调回 Lua，slua 还是会回到 `LuaVar::docall()`，最终统一走一次 `lua_pcallk()`。换句话说，slua 的静态导出主要减少的是“进入 native 前的桥接成本”，而不是给 Lua VM 本身增加新的 lowering 层。

Angelscript 则正好相反。`FScriptSystemCall::MakeCall()` 会先解析 `NativeForm`、`ObjectType`、`ReturnType` 和 `ArgumentTypes`，再在 `CustomCall / NativeCall / PointerCall / DynamicCall` 四条路径之间做选择。这说明 `StaticJIT` 优化的单位是“每个脚本 call site”，不是单纯把某个 native API 预先写成快一点的 wrapper。

```
[D8] Hot Path Cost Placement
sluaunreal
├─ Lua -> CppBinding -> ArgOperator::readArg       // 静态导出压这里的成本
├─ native callback wants Lua closure               // 一旦回流到 Lua
└─ LuaVar::docall -> lua_pcallk                    // 仍回到通用 Lua 调用框架

Angelscript
├─ script bytecode -> FScriptSystemCall::MakeCall
├─ FindNativeForm + AnalyzeArgumentTypes
├─ choose CustomCall / NativeCall / PointerCall
└─ fallback DynamicCall only when native lowering 不成立
```

[1] 关键源码：slua 的 callback 回流最终仍统一落到 `lua_pcallk`

```cpp
// ============================================================================
// [1]
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaCppBindingPost.h
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaVar.h
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaVar.cpp
// 函数: makeTFunctionProxy / LuaVar::callWithNArg / LuaVar::docall
// 位置: LuaCppBindingPost.h:37-40; LuaVar.h:287-292; LuaVar.cpp:656-677
// 位置说明: slua 的静态 callback 代理会重新回到 LuaVar 通用调用器，而不是生成新的 call-site lowering
// ============================================================================
// LuaCppBindingPost.h
37:            return [=](ArgTypes&& ... args) -> ReturnType
39:                LuaVar result = func.call(std::forward<ArgTypes>(args) ...);
40:                return resultCast<ReturnType>(MoveTemp(result));
// ★ 从 CppBinding 生成出来的 C++ lambda，本质仍然调用 LuaVar::call

// LuaVar.h
287:        inline LuaVar callWithNArg(const FillParamCallback& fillParam) {
288:            auto L = getState();
289:            int nret = docall(fillParam);
290:            auto ret = LuaVar::wrapReturn(L,nret);
291:            lua_pop(L,nret);
292:            return ret;
// ★ 所有“已知参数个数”的动态调用仍统一汇入 docall

// LuaVar.cpp
663:        int errhandle = LuaState::pushErrorHandler(L);
664:        vars[0].ref->push(L);
666:        if (fillParam) {
667:            argn = fillParam();
673:            if (lua_pcallk(L, argn, LUA_MULTRET, errhandle, NULL, NULL))
674:                lua_pop(L, 1);
675:            lua_remove(L, errhandle);
677:        return lua_gettop(L) - errhandle + 1;
// ★ 真正执行时仍然是一次通用 `lua_pcallk`；这不是 StaticJIT 风格的脚本侧调用降阶
```

[2] 对照源码：Angelscript `StaticJIT` 在每个 call site 上判断能否 lower 成更便宜的调用

```cpp
// ============================================================================
// [2]
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp
// 函数: FScriptSystemCall::MakeCall
// 位置: 109-168
// 位置说明: Angelscript 把优化逻辑放在“脚本系统调用点”的 lowering 选择上
// ============================================================================
109:    void MakeCall(FStaticJITContext& Context)
111:        FNativeFunctionContext NativeContext;
117:        Context.Line("// {0}", ScriptFunction->GetDeclaration(true, true, true, false));
120:        FindNativeForm();
123:        AnalyzeArgumentTypes();
126:        NativeContext.ArgumentTypes = ArgumentTypes;
127:        NativeContext.ReturnType = ReturnType;
128:        NativeContext.ObjectType = ObjectType;
131:        bool bCanMakeNativeCall = bHaveNativeFunction && bAllTypesHaveNatives && NativeForm->CanCallNative(NativeContext);
132:        bool bCanMakeCustomCall = NativeForm != nullptr && NativeForm->CanCallCustom(NativeContext);
149:        if (bCanMakeCustomCall)
152:            MakeCustomCall(Context, NativeContext);
154:        else if (bCanMakeNativeCall)
157:            MakeNativeCall(Context, NativeContext);
159:        else if (bAllTypesHaveGenerics && SupportsCallingConventionForGeneric())
162:            MakePointerCall(Context);
165:        else
167:            MakeDynamicCall(Context);
// ★ 这是一套真正的 call-site lowering 决策树；优化单位是“每一次脚本系统调用”
```

补充判断：

- `slua 对应 StaticJIT 的等价层`：在本轮阅读的 `LuaCppBinding/LuaVar/LuaWrapper` 主链里 `没有实现`。当前看到的是静态 wrapper 和动态 `lua_pcallk` 回流，没有看到与 `NativeForm->CanCallNative()` 同级的 call-site lowering。
- `性能优化层级`：这是 `实现方式不同`。slua 优化“跨语言入口的桥接成本”；Angelscript 优化“脚本调用点如何 lower 到 native/custom/pointer call”（`LuaVar.cpp:663-677`; `AngelscriptBytecodes.cpp:131-167`）。
- `callback 回流路径`：slua 这里不是 `没有实现`，而是 `实现方式不同`。它能把 Lua closure 变成 C++ lambda，但执行仍然走 `LuaVar::call/docall` 的通用动态调用器（`LuaCppBindingPost.h:37-40`; `LuaVar.h:287-292`; `LuaVar.cpp:663-677`）。

### [维度 D1 / D11] slua 把 native runtime 放在插件里，把 script staging/cook policy 留给宿主工程

前面已经证明 slua 用 `LoadFileDelegate` 读取脚本 bytes。这一轮新增的点是：这种边界不只是 runtime API 设计，而是直接刻在 `Build.cs` 和 demo host module 里。`slua_unreal.Build.cs` 明确负责接入多平台 `liblua`/`lua.lib`；但按本轮逐行检查 `slua_unreal.Build.cs:17-113`，看到的只有 `PublicAdditionalLibraries / IncludePaths / Definitions`，没有看到 `RuntimeDependencies` 或 `AdditionalPropertiesForReceipt` 一类 receipt/staging API。与此同时，真正决定脚本从哪里来的不是插件模块，而是 `Source/democpp/MyGameInstance.cpp`：它把 loader 固定到 `ProjectContentDir()/Lua`，按 `.lua/.luac` 顺序探测文件。

这意味着 slua 插件正式拥有的是“native Lua runtime + runtime loader ABI”，而不是“script receipt / cook / encryption / signing policy”。`LuaState::doFile()` 只是把 bytes 丢给 `luaL_loadbuffer`，`requireModule()` 只是调 Lua 原生 `require`。脚本如何随包体分发、是否转成密文、是否带签名、如何做 manifest，这一层故意留给宿主工程。

Angelscript 这边则更像相反的边界：插件内部自己定义 `-as-simulate-cooked` / `-as-generate-precompiled-data` / `bUsePrecompiledData`，加载 `Binds.Cache` 与 `PrecompiledScript*.Cache`，并在 cooked/simulate-cooked 下改变 editor script 与 test discovery 行为。它也没有直接实现“脚本加密/签名”，但它把“脚本交付物长什么样、什么时候读写、如何校验构建配置”产品化成了插件内部协议。

```
[D1/D11] Delivery Ownership
sluaunreal plugin
├─ Build.cs links liblua / lua.lib                // 插件拥有 native runtime
├─ defines ENABLE_PROFILER / NS_SLUA
└─ receipt-side script staging not found          // 本轮在 Build.cs 中未见 staging API

slua host project
├─ MyGameInstance::setLoadFileDelegate
├─ ProjectContentDir()/Lua
├─ probe .lua / .luac
└─ doFile()/requireModule() only consume bytes

Angelscript plugin
├─ -as-simulate-cooked / -as-generate-precompiled-data
├─ Load Binds.Cache
├─ Load or Save PrecompiledScript*.Cache
└─ cooked mode changes editor/test behavior
```

[1] 关键源码：slua 的插件只接管 native Lua 库，脚本根目录则由宿主 `GameInstance` 决定

```cpp
// ============================================================================
// [1]
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/slua_unreal.Build.cs
// 文件: Reference/sluaunreal/Source/democpp/MyGameInstance.cpp
// 函数: slua_unreal constructor / UMyGameInstance::CreateLuaState
// 位置: slua_unreal.Build.cs:31-76,106-112; MyGameInstance.cpp:41-63
// 位置说明: Build.cs 负责 native Lua 库；脚本路径策略则落在宿主工程，不落在插件 receipt
// ============================================================================
// slua_unreal.Build.cs
31:        var externalSource = Path.Combine(PluginDirectory, "External");
32:        var externalLib = Path.Combine(PluginDirectory, "Library");
42:        if (Target.Platform == UnrealTargetPlatform.IOS)
44:            PublicAdditionalLibraries.Add(Path.Combine(externalLib, "iOS/liblua.a"));
46:        else if (Target.Platform == UnrealTargetPlatform.Android)
49:            PublicAdditionalLibraries.Add(Path.Combine(externalLib, "Android/armeabi-v7a/liblua.a"));
65:        else if (Target.Platform == UnrealTargetPlatform.Win64)
67:            PublicAdditionalLibraries.Add(Path.Combine(externalLib, "Win64/lua.lib"));
73:        else if (Target.Platform == UnrealTargetPlatform.Linux)
75:            PublicAdditionalLibraries.Add(Path.Combine(externalLib, "Linux/liblua.a"));
107:        PublicDefinitions.Add("ENABLE_PROFILER");
108:        PublicDefinitions.Add("NS_SLUA=slua");
// ★ 本轮逐行检查 17-113 行，看到的是 native 库接入；未见脚本 receipt/staging API，这是基于源码缺席的推断

// MyGameInstance.cpp
41:    state = new NS_SLUA::LuaState("SLuaMainState", this);
42:    state->setLoadFileDelegate([](const char* fn, FString& filepath)->TArray<uint8> {
45:        FString path = FPaths::ProjectContentDir();
47:        path /= "Lua";
48:        path /= filename.Replace(TEXT("."), TEXT("/"));
51:        TArray<FString> luaExts = { UTF8_TO_TCHAR(".lua"), UTF8_TO_TCHAR(".luac") };
55:            FFileHelper::LoadFileToArray(Content, *fullPath);
57:                filepath = fullPath;
58:                return MoveTemp(Content);
// ★ 脚本目录、扩展名顺序、字节来源都由宿主模块决定；插件只是消费这个 ABI
```

[2] 关键源码：slua runtime 自己不定义 script 制品协议，只消费 bytes

```cpp
// ============================================================================
// [2]
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaState.cpp
// 函数: LuaState::doFile / LuaState::requireModule
// 位置: 755-783
// 位置说明: runtime 只负责读取 bytes 并交给 Lua；不拥有加密、签名、manifest 等交付协议
// ============================================================================
755:    LuaVar LuaState::doFile(const char* fn, LuaVar* pEnv) {
756:        FString filepath;
757:        TArray<uint8> buf = loadFile(fn, filepath);
758:        if (buf.Num() > 0) {
760:            snprintf(chunk,256,"@%s",TCHAR_TO_UTF8(*filepath));
762:            LuaVar r = doBuffer(buf.GetData(),buf.Num(),chunk,pEnv );
763:            return r;
765:        return LuaVar();
768:    LuaVar LuaState::requireModule(const char* fn, LuaVar* pEnv) {
770:        lua_getglobal(L, "require");
771:        lua_pushstring(L, fn);
773:        if (lua_pcall(L, 1, 1, top))
776:            return LuaVar();
782:        LuaVar luaModule(L, -retCount);
// ★ 这里正式承诺的只有“module/path -> bytes -> Lua require”；更高层部署规则不在插件里
```

[3] 对照源码：Angelscript 把 cooked/precompiled 交付规则定义成插件内部协议

```cpp
// ============================================================================
// [3]
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp
// 函数: FAngelscriptEngineConfig::FromCurrentProcess / engine init / precompiled save-load
// 位置: AngelscriptEngine.cpp:519-523,1427-1456,1513-1556,1583-1587,2239-2243;
//       PrecompiledData.cpp:2673-2689,2692-2729
// 位置说明: Angelscript 没有脚本加密/验签，但它把 cooked 模式与 cache 制品正式内建到插件里
// ============================================================================
// AngelscriptEngine.cpp
519:    Config.bSimulateCooked = FParse::Param(FCommandLine::Get(), TEXT("as-simulate-cooked"));
522:    Config.bGeneratePrecompiledData = FParse::Param(FCommandLine::Get(), TEXT("as-generate-precompiled-data"));
1427:    bUsePrecompiledData = !bGeneratePrecompiledData && !RuntimeConfig.bIgnorePrecompiledData
1428:        && !RuntimeConfig.bRunningCommandlet && !WITH_EDITOR && !bScriptDevelopmentMode;
1453:    if ((!bUsePrecompiledData || bScriptDevelopmentMode) && FApp::HasProjectName())
1455:        DebugServer = new FAngelscriptDebugServer(this, RuntimeConfig.DebugServerPort);
1469:        FAngelscriptBindDatabase::Get().Load(GetScriptRootDirectory() / TEXT("Binds.Cache"), bGeneratePrecompiledData);
1513:    if (bUsePrecompiledData)
1521:        Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript_Shipping.Cache");
1529:            Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript.Cache");
1534:            PrecompiledData->Load(Filename);
1552:                if (CompiledInfo != nullptr && CompiledInfo->PrecompiledDataGuid != PrecompiledData->DataGuid)
1585:        FString Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript.Cache");
1587:        PrecompiledData->Save(Filename);
2239:    if (bSimulateCooked || IsRunningCookCommandlet())
2243:        return;
// ★ cooked/simulate-cooked、cache 路径、构建校验、测试发现策略都在插件内部定义

// PrecompiledData.cpp
2673:    Writer << *this;
2675:    FFileHelper::SaveArrayToFile(Data, *Filename);
2683:    FFileHelper::LoadFileToArray(LoadedData, *Filename);
2692:    uint32 FAngelscriptPrecompiledData::CreateFunctionId(asIScriptFunction* Function)
2708:        // Generate a consistent Guid for the function, to improve iteration times
2712:            Id = HashCombine(Id, FCrc::StrCrc_DEPRECATED((const ANSICHAR*)ScriptModule->GetName()));
2718:            Id = HashCombine(Id, FCrc::StrCrc_DEPRECATED((const ANSICHAR*)ObjectType->GetEngine()->GetTypeDeclaration(ObjectType->GetTypeId(), true)));
2720:        Id = HashCombine(Id, FCrc::StrCrc_DEPRECATED((const ANSICHAR*)Function->GetDeclaration(true, true)));
// ★ 交付物不只是一个裸文件；它还有稳定 function id 和构建匹配校验语义
```

补充判断：

- `native runtime staging`：slua 这里 `有实现`，而且插件所有权非常清楚，直接体现在 `Build.cs` 的多平台 `liblua`/`lua.lib` 接入（`slua_unreal.Build.cs:42-76`）。
- `script receipt / staging ownership`：slua 插件在本轮检查范围内应判为 `没有实现`，因为 script root、扩展名探测和字节来源由宿主 `GameInstance` 决定（`MyGameInstance.cpp:42-63`; `LuaState.cpp:755-783`）；Angelscript 则是 `实现方式不同`，它把交付物收敛成 `Binds.Cache` 与 `PrecompiledScript*.Cache`（`AngelscriptEngine.cpp:1469,1513-1556,1583-1587`）。
- `脚本加密 / 签名`：本轮继续判为双方都 `没有实现`。补充扫描 `Reference/sluaunreal/Plugins/slua_unreal`、`Reference/sluaunreal/Source/democpp` 与 `Plugins/Angelscript/Source` 时，slua 只在 `slua_remote_profile.cpp:157` 命中一条 profiler socket 注释 `Socket->IsNotEncrypt = true`，而 Angelscript 命中的 `Signature` 主要是函数签名构建，不是脚本验签链。
- 推断：如果 Angelscript 未来要补 D11，最值得吸收的不是 slua 当前 loader 的代码细节，而是“把宿主 ABI 和插件能力边界划得很硬”这一点；但如果要做官方交付链，Angelscript 现有 `Binds.Cache / PrecompiledScript*.Cache / simulate-cooked` 体系已经更接近正式产品协议。

---

## 深化分析 (2026-04-09 23:24:23)

本轮不重复前文的模块总览，直接补 4 个更细的源码判断：`D4` 的刷新单元、`D3` 的 Blueprint 动态桥、`D8` 的 profiler/执行优化边界，以及 `D11` 的 bytecode-only 能力到底停留在哪一层。

### [维度 D4] slua 的“热更新”单元是 `Lua module + UFunction hook table`，不是文件监听驱动的结构化重编译

这一轮把 `LuaOverrider.cpp` 再往下读后，可以更明确地区分两类能力。slua 的刷新动作，本质上是重新 `requireModule()` 某个 Lua 模块，再把该模块里的函数名映射回已有 `UFunction`，通过 `SetNativeFunc()` 或插入 `Ex_LuaOverride` 字节码把调用改道到 `ULuaOverrider::luaOverrideFunc`。在 Editor 下，它还会回滚旧 hook。也就是说，它维护的是“调用入口改写表”，不是“脚本类型/反射签名的增量编译状态机”。

Angelscript 则正相反。它先由 `DirectoryWatcher` 把 `.as` 变更排队，再在 `ClassGenerator` 中检查属性/函数签名变化，最后由 `Engine` 在 `SoftReload / FullReload` 之间做决策。这意味着它的热重载单位是“脚本模块及其反射产物”，而不是某个已有 `UFunction` 的 trampoline。

```
[D4] Reload Unit Comparison
sluaunreal
├─ Host decides when bytes change                     // 宿主决定何时提供新脚本字节
├─ LuaOverrider::bindOverrideFuncs()
│  ├─ requireModule(luaFilePath)                     // 重新装入 Lua module
│  ├─ getLuaFunctions()                              // 扫描 Lua table 函数名
│  └─ hookBpScript()                                 // 改写已有 UFunction 入口
└─ removeOneOverride()                               // Editor 下回滚旧 hook

Angelscript
├─ DirectoryWatcher::QueueScriptFileChanges()        // 文件变化先入队
├─ compile changed modules                           // 重新编译受影响模块
├─ ClassGenerator compares signatures                // 判定结构变更
└─ Engine chooses SoftReload / FullReload            // 决定重载粒度
```

[1] 关键源码：slua 把热更新收敛为 `module reload + hook rewrite`

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaOverrider.cpp
// 函数: LuaOverrider::removeOneOverride / bindOverrideFuncs / hookBpScript
// 位置: 826-860, 1194-1288, 1381-1424
// 位置说明: slua 的刷新单元不是“重新生成反射类型”，而是“重新装入 Lua module 并重写 UFunction 入口”
// ============================================================================
826:    void LuaOverrider::removeOneOverride(UClass* cls, bool bObjectDeleted)
831:            auto hookedFuncsPtr = classHookedFuncs.Find(cls);
834:                // Revert hooked functions
847:                    if (scriptNum >= CodeSize && script[0] == Ex_LuaOverride)
849:                        script.RemoveAt(0, CodeSize, false);
858:                    if (func->GetNativeFunc() == (FNativeFuncPtr)&ULuaOverrider::luaOverrideFunc)
// ★ Editor 下能回滚旧 hook，说明这里维护的是“入口改写状态”，不是编译产物版本链

1194:        NS_SLUA::LuaVar luaModule = sluaState->requireModule(TCHAR_TO_UTF8(*luaFilePath));
1201:        if (!luaModule.isTable() && !luaModule.isFunction()) {
1262:            TSet<FName> funcNames;
1263:            getLuaFunctions(L, funcNames, luaModule);
1275:            for (auto& funcName : funcNames) {
1276:                UFunction* func = cls->FindFunctionByName(funcName, EIncludeSuperFlag::IncludeSuper);
1281:                    if (hookBpScript(func, cls, (FNativeFuncPtr)&ULuaOverrider::luaOverrideFunc)) {
1287:            luaNet->addClassRPC(L, cls, luaFilePath);
// ★ 刷新过程的核心数据来自 Lua module 的函数名集合，而不是脚本 AST / 反射签名差异

1381:    bool LuaOverrider::hookBpScript(UFunction* func, UClass* cls, FNativeFuncPtr hookFunc)
1388:            GRegisterNative(Ex_LuaOverride, hookFunc);
1398:        // duplicate UFunction for super call
1399:        auto supercallFunc = duplicateUFunction(func, cls, FName(*(SUPER_CALL_FUNC_NAME_PREFIX + func->GetName())), func->GetNativeFunc());
1417:                overrideFunc->SetNativeFunc(hookFunc);
1419:            overrideFunc->Script.Insert(Code, CodeSize, 0);
// ★ 这里做的是已有 UFunction 的 trampoline 改写，并额外复制一份 Super_ 函数给 Lua 调 super
```

[2] 对照源码：Angelscript 把热重载做成“文件变化 -> 结构判定 -> 软/全量重载”链

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: QueueScriptFileChanges / class diff checks / compile reload switch
// 位置: AngelscriptDirectoryWatcherInternal.cpp:43-89;
//       AngelscriptClassGenerator.cpp:1196-1218;
//       AngelscriptEngine.cpp:3938-3997
// 位置说明: Angelscript 的重载单位是脚本模块与反射产物，不是已有 UFunction 的 hook 表
// ============================================================================
// AngelscriptDirectoryWatcherInternal.cpp
43:    void QueueScriptFileChanges(const TArray<FFileChangeData>& Changes, const TArray<FString>& RootPaths, FAngelscriptEngine& Engine, IFileManager& FileManager, const FEnumerateLoadedScripts& EnumerateLoadedScripts)
55:            Engine.LastFileChangeDetectedTime = FPlatformTime::Seconds();
57:            if (AbsolutePath.EndsWith(TEXT(".as")))
61:                    Engine.FileDeletionsDetectedForReload.AddUnique({ AbsolutePath, RelativePath });
65:                    Engine.FileChangesDetectedForReload.AddUnique({ AbsolutePath, RelativePath });
68:                UE_LOG(Angelscript, Log, TEXT("Queued script file change for primary engine reload: %s"), *RelativePath);
// ★ 文件变化先排队，后续编译流程再统一处理

// AngelscriptClassGenerator.cpp
1196:        // Check if any bound methods from the old class have been removed or changed signature
1199:            auto NewFunctionDesc = ClassData.NewClass->GetMethod(OldFunctionDesc->FunctionName);
1202:                // Method was removed, need full reload
1211:                if (!OldFunctionDesc->SignatureMatches(NewFunctionDesc))
1213:                    // Method changed signature, need full reload
1216:                        ClassData.ReloadReq = EReloadRequirement::FullReloadRequired;
// ★ 是否全量重载由“签名和结构是否变化”决定，而不是由宿主是否重新下发脚本字节决定

// AngelscriptEngine.cpp
3938:                case FAngelscriptClassGenerator::EReloadRequirement::SoftReload:
3940:                    ClassGenerator.PerformSoftReload();
3942:                case FAngelscriptClassGenerator::EReloadRequirement::FullReloadSuggested:
3969:                        ClassGenerator.PerformFullReload();
3972:                case FAngelscriptClassGenerator::EReloadRequirement::FullReloadRequired:
3976:                            TEXT("Full Reload is required due to UPROPERTY() or UFUNCTION() changes, but cannot")
3995:                        ClassGenerator.PerformFullReload();
// ★ 最终由运行时正式区分 SoftReload 与 FullReload，并把结构风险显式反馈给用户
```

补充判断：

- `刷新触发所有权`：slua 这里是 `实现方式不同`。插件负责重绑和回滚，宿主负责何时提供新 bytes；Angelscript 则把文件监听和重载调度收进插件内部（`LuaOverrider.cpp:1194-1288`; `AngelscriptDirectoryWatcherInternal.cpp:43-89`）。
- `结构变更判定`：在本轮检查的 `Reference/sluaunreal/Plugins/slua_unreal/Source` 与 `Reference/sluaunreal/Source/democpp` 范围内，未见与 `SignatureMatches()` + `FullReloadRequired` 同级的判定链，应暂判 slua 这里 `没有实现`，但这是基于源码缺席的判断。
- `适用场景`：slua 更像“保持 UObject/UFunction ABI 不变前提下的线上逻辑替换”；Angelscript 更像“开发期脚本类和反射接口一起演化时的安全热重载”。两者不是强弱关系，而是面向不同变更面。

### [维度 D3] `FLuaBPVar` 是 slua 的 Blueprint 动态逃生舱，Blueprint/Lua 互通更像 `variant bridge`

前文已经提过 slua 有 `ULuaBlueprintLibrary`。这一轮新增的关键点是：`FLuaBPVar` 并不是普通 `USTRUCT` 参数，而是被 `LuaObject` 明确识别的特例。`CallToLuaWithArgs()` 把 `TArray<FLuaBPVar>` 中的 `LuaVar` 直接压栈；返回时 `GetIntFromVar / GetStringFromVar / GetObjectFromVar` 再按下标和运行时类型去拆包。`LuaObject` 还专门禁止对 `FLuaBPVar` 走常规 reference pusher / referencer。换句话说，slua 在 Blueprint 这条链路里选择的是“一个动态容器吃掉类型差异”，而不是为每个 Blueprint 函数保持完整的脚本侧静态签名。

Angelscript 的 Blueprint 接入则更偏“签名不丢失”。能直绑时，它把 `Signature.Declaration`、`FunctionId`、`Caller` 都挂到注册账本；退化到 reflective fallback 时，也仍逐个 `FProperty` 拷贝参数并维护 out reference。委托绑定还会显式检查签名兼容性。

```
[D3] Blueprint Bridge Shape
sluaunreal
├─ Blueprint pin -> FLuaBPVar                       // 所有动态值先包成统一容器
├─ CallToLuaWithArgs(FunctionName, Args)            // 运行时按字符串找 Lua 函数
├─ LuaVar.push()/set()                              // 直接压 Lua VM 栈
└─ GetXFromVar(Index)                               // 返回值再按索引/类型拆包

Angelscript
├─ Build typed Signature.Declaration                // 先保留脚本声明
├─ direct bind if native pointer exists             // 能直绑就直绑
├─ reflective fallback copies each FProperty        // 否则逐属性搬运
└─ delegate bind checks exact compatibility         // 委托绑定显式校验签名
```

[1] 关键源码：slua 用 `FLuaBPVar` 把 Blueprint 参数桥压缩成一个动态容器

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaBlueprintLibrary.h
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaBlueprintLibrary.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaObject.cpp
// 函数: FLuaBPVar / CallToLuaWithArgs / getValueFromVar / LuaObject special cases
// 位置: LuaBlueprintLibrary.h:21-31, 41-76;
//       LuaBlueprintLibrary.cpp:51-76, 140-145, 218-256;
//       LuaObject.cpp:591-595, 623-625, 2243-2246, 2437-2439
// 位置说明: Blueprint 不直接携带一份“函数签名”，而是通过 FLuaBPVar 这个统一容器与 LuaVar 往返
// ============================================================================
// LuaBlueprintLibrary.h
21:USTRUCT(BlueprintType)
22:struct SLUA_UNREAL_API FLuaBPVar {
29:    NS_SLUA::LuaVar value;
31:    static void* checkValue(NS_SLUA::lua_State* L, NS_SLUA::FStructProperty* p, uint8* params, int i);
42:    UFUNCTION(BlueprintCallable, meta=( DisplayName="Call To Lua With Arguments", WorldContext = "WorldContextObject"), Category="slua")
43:    static FLuaBPVar CallToLuaWithArgs(UObject* WorldContextObject, FString FunctionName,const TArray<FLuaBPVar>& Args,FString StateName);
// ★ Blueprint 侧暴露的是一个“字符串函数名 + FLuaBPVar 数组”的动态入口

// LuaBlueprintLibrary.cpp
63:    LuaVar f = ls->get(TCHAR_TO_UTF8(*funcname));
69:    auto fillParam = [&]
71:        for (auto& arg : args) {
72:            arg.value.push(ls->getLuaState());
76:    return f.callWithNArg(fillParam);
140:void* FLuaBPVar::checkValue(NS_SLUA::lua_State* L, NS_SLUA::FStructProperty* p, uint8* params, int i)
143:    ret.value.set(L, i);
144:    p->CopyCompleteValue(params, &ret);
218:    T getValueFromVar(const FLuaBPVar& Value,int Index) {
223:            if(getValue(lv,Index,v))
226:                FFrame::KismetExecutionMessage(*FString::Printf(TEXT("Attempted to index an item from an invalid type!")),
// ★ 入参是直接压 Lua 栈；回参是运行时按 Index/类型拆包，错误也延后到运行时才告警

// LuaObject.cpp
593:        if (sp && sp->Struct == FLuaBPVar::StaticStruct())
594:            return nullptr;
624:                if (sp && sp->Struct == FLuaBPVar::StaticStruct())
625:                    return nullptr;
2244:        if (uss == FLuaBPVar::StaticStruct()) {
2245:            ((FLuaBPVar*)parms)->value.push(L);
2437:        // if it's LuaBPVar
2438:        if (uss == FLuaBPVar::StaticStruct())
2439:            return FLuaBPVar::checkValue(L, p, parms, i);
// ★ LuaObject 明确把 FLuaBPVar 当作反射桥特例处理，不走普通属性引用路径
```

[2] 对照源码：Angelscript 保留 typed signature，fallback 也逐属性拷贝而不是用统一 variant 吞掉类型

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.cpp
// 函数: direct bind / InvokeReflectiveUFunctionFromGenericCall / BindUFunction_Signature
// 位置: Bind_BlueprintCallable.cpp:72-150;
//       BlueprintCallableReflectiveFallback.cpp:302-370, 374-405;
//       Bind_Delegates.cpp:536-566
// 位置说明: Angelscript 在 Blueprint 交互链上尽量保留静态签名，只有必要时才退回逐属性反射搬运
// ============================================================================
// Bind_BlueprintCallable.cpp
72:    auto* DirectNativePointer = &Entry->FuncPtr;
74:    if (!bHasDirectNativePointer)
76:        if (!BindBlueprintCallableReflectiveFallback(InType, Function, Signature, *Entry))
107:            int GlobalFunctionId = FAngelscriptBinds::BindGlobalFunction(Signature.Declaration, ASFuncPtr, Entry->Caller);
120:        int FunctionId = FAngelscriptBinds::BindMethodDirect
136:            Signature.Declaration, ASFuncPtr, asCALL_THISCALL, Entry->Caller
// ★ 能直绑时保留的是 `Signature.Declaration + FunctionId + Caller`，不是一个统一动态容器

// BlueprintCallableReflectiveFallback.cpp
302:    uint8* ParameterBuffer = static_cast<uint8*>(FMemory_Alloca(Function->ParmsSize));
310:    for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
333:        void* ScriptArgumentAddress = Generic->GetAddressOfArg(ScriptArgIndex);
335:        Property->CopySingleValue(Destination, SourceAddress);
337:        if (Property->HasAnyPropertyFlags(CPF_ReferenceParm) && !Property->HasAnyPropertyFlags(CPF_ConstParm))
345:    TargetObject->ProcessEvent(Function, ParameterBuffer);
347:    for (int32 OutReferenceIndex = 0; OutReferenceIndex < OutReferenceCount; ++OutReferenceIndex)
358:    if (FProperty* ReturnProperty = Function->GetReturnProperty())
// ★ 即便 fallback，也还是一项项搬运 FProperty，并维护 out/ref 语义

// Bind_Delegates.cpp
536:void FAngelscriptDelegateOperations::BindUFunction_Signature(FScriptDelegate* Delegate, UObject* InObject, const FName& InFunctionName, UDelegateFunction* Signature)
557:    if (!CheckAngelscriptDelegateCompatibility(Signature, CallFunction))
559:        FString Message = FString::Printf(TEXT("Specified function is not compatible with delegate function.\n\nDelegate: %s\n\nAttempted Bind: %s"),
565:    Delegate->BindUFunction(InObject, InFunctionName);
// ★ 委托绑定有显式签名兼容检查；Blueprint/Lua 互通不是“只要能塞进 variant 就继续”
```

补充判断：

- `Blueprint 主动调用脚本`：slua 这里是 `实现方式不同`。它通过 `FLuaBPVar` 把 Blueprint 侧互通口做成动态容器桥；Angelscript 则优先保留 typed declaration 和 delegate compatibility（`LuaBlueprintLibrary.cpp:51-76`; `Bind_BlueprintCallable.cpp:72-150`; `Bind_Delegates.cpp:536-566`）。
- `运行时风险分布`：slua 的类型错误更容易落到 `GetXFromVar()` 的运行时告警；Angelscript 更早在绑定或反射搬运阶段暴露出参数/签名不兼容。这不是“哪个一定更好”，而是“动态性 vs 契约显式性”的取舍。
- `Blueprint 事件覆写路径`：slua 仍然很强，因为它有 `ULuaOverrider::luaOverrideFunc(FFrame&)` 这种 trampoline（`LuaOverrider.h:16`; `LuaOverrider.cpp:59-120`）；但在 BP function-library 这条主动调用链上，它明显比 Angelscript 更偏动态桥。

### [维度 D8] slua 的 Profiler 是“观测平面”，Angelscript `StaticJIT` 是“执行平面”；两者不是同位替代关系

这一轮把 `LuaProfiler.cpp`、`slua_remote_profile.cpp` 和 Angelscript 的 `StaticJIT`、`CodeCoverage` 放在一起看后，边界就很清楚了。slua profiler 的核心动作是：

1. 在 Lua VM 上装 `lua_sethook(L, debug_hook, LUA_MASKRET | LUA_MASKCALL, 0)`。
2. 在每次 `CALL / RETURN / TICK / MEMORY_*` 事件发生时，序列化 `event + time + line + name + short_src`。
3. 这些样本要么直接走 socket 发给 `slua_profile` editor 模块，要么先进 `SluaProfilerDataManager` 本地队列再重建调用树。

这是一条标准“观测数据管线”。它回答的是“哪里花了时间、内存怎么涨、协程如何切换”。`StaticJIT` 则回答另一个问题：某个脚本调用点能不能 lower 成 `NativeCall / CustomCall / PointerCall`，从而减少桥接与通用调度成本。两者一个在测量，一个在降阶。

```
[D8] Observability Plane vs Execution Plane
sluaunreal
├─ lua_sethook(call/return)                         // 采集 Lua VM 事件
├─ takeSample / takeMemorySample                    // 组装 profile message
├─ socket or local queue                            // 远端/本地两条观测通路
└─ slua_profile reconstructs call tree             // 编辑器面板还原 CPU/内存树

Angelscript
├─ FindNativeForm + AnalyzeArgumentTypes            // 分析调用点
├─ choose CustomCall / NativeCall / PointerCall     // 生成更便宜的调用路径
├─ fallback DynamicCall only if needed              // 失败才回通用路径
└─ CodeCoverage hooks only test lifecycle           // 现有观测更接近测试覆盖率，不是 live profiler
```

[1] 关键源码：slua Profiler 从 Lua hook 采样，一直串到 socket 消息反序列化

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaProfiler.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_profile/Private/slua_remote_profile.cpp
// 函数: takeSample / changeHookState / init / FProfileMessage::Deserialize
// 位置: LuaProfiler.cpp:222-236, 328-359, 441-467;
//       slua_remote_profile.cpp:263-347, 365-387
// 位置说明: slua 的 profiler 是一条完整观测链：采样 -> 序列化 -> 传输 -> 反序列化
// ============================================================================
// LuaProfiler.cpp
222:        void takeSample(int event,int line,const char* funcname,const char* shortsrc, int64 startTime, lua_State* L) {
224:            if (!SluaProfilerDataManager::IsRecording())
230:                makeProfilePackage(s_messageWriter, event, startTime - profileTotalCost, line, funcname, shortsrc);
231:                sendMessage(s_messageWriter, L);
235:                SluaProfilerDataManager::ReceiveProfileData(event, startTime - profileTotalCost, line, funcname, shortsrc);
// ★ 样本不是只在本地统计；它有远端发送和本地录制两套通路

328:        int changeHookState(lua_State* L) {
332:            if (state == HookState::UNHOOK) {
333:                lua_sethook(L, nullptr, 0, 0);
335:            else if (state == HookState::HOOKED) {
345:                takeMemorySample(PHE_MEMORY_TICK, memoryInfoList, L);
347:                lua_sethook(L, debug_hook, LUA_MASKRET | LUA_MASKCALL, 0);
358:            profiler.callField("changeCoroutinesHookState", profiler);
// ★ 采样入口直接挂在 Lua VM hook 上，连协程 hook 状态也一起管理

441:    void LuaProfiler::init(LuaState* LS)
446:        profiler = LS->doBuffer((const uint8*)ProfilerScript,strlen(ProfilerScript), ChunkName);
449:        lua_pushcfunction(L, changeHookState);
453:        lua_pushcfunction(L, setSocket);
465:        // using native hook instead of lua hook for performance
467:        lua_setglobal(L, "slua_profile");
// ★ profiler 脚本通过 doBuffer 注入，再把 native 控制函数挂成全局 `slua_profile`

// slua_remote_profile.cpp
263:    bool FProfileConnection::ReceiveMessages()
299:                FArrayReader MessagesizeData = FArrayReader(true);
313:                MessagesizeData << RecvMessageDataRemaining;
334:                    FProfileMessage* DeserializedMessage = new FProfileMessage();
335:                    if (DeserializedMessage->Deserialize(RecvMessageData))
337:                        Inbox.Enqueue(MakeShareable(DeserializedMessage));
365:    bool FProfileMessage::Deserialize(const TSharedPtr<FArrayReader, ESPMode::ThreadSafe>& Message)
369:        MessageReader << Event;
382:        MessageReader << Time;
383:        MessageReader << Linedefined;
384:        MessageReader << Name;
385:        MessageReader << ShortSrc;
// ★ 消息体里存的是 profile event/time/source/name，说明这是观测协议，不是执行优化协议
```

[2] 对照源码：Angelscript `StaticJIT` 优化调用点；现有观测更接近测试覆盖率而非 live profiler

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/AngelscriptCodeCoverage.cpp
// 函数: FScriptSystemCall::MakeCall / AddTestFrameworkHooks / StartRecording
// 位置: AngelscriptBytecodes.cpp:109-168;
//       AngelscriptCodeCoverage.cpp:22-64
// 位置说明: Angelscript 现有“性能”和“观测”能力分属两层，StaticJIT 与 coverage 都不等价于 slua profiler
// ============================================================================
// AngelscriptBytecodes.cpp
109:    void MakeCall(FStaticJITContext& Context)
120:        FindNativeForm();
123:        AnalyzeArgumentTypes();
131:        bool bCanMakeNativeCall = bHaveNativeFunction && bAllTypesHaveNatives && NativeForm->CanCallNative(NativeContext);
132:        bool bCanMakeCustomCall = NativeForm != nullptr && NativeForm->CanCallCustom(NativeContext);
149:        if (bCanMakeCustomCall)
152:            MakeCustomCall(Context, NativeContext);
154:        else if (bCanMakeNativeCall)
157:            MakeNativeCall(Context, NativeContext);
159:        else if (bAllTypesHaveGenerics && SupportsCallingConventionForGeneric())
162:            MakePointerCall(Context);
167:            MakeDynamicCall(Context);
// ★ StaticJIT 优化的是“每个脚本调用点最终走哪条更便宜的执行路径”

// AngelscriptCodeCoverage.cpp
22:void FAngelscriptCodeCoverage::AddTestFrameworkHooks()
27:    AutomationController->OnTestsAvailable().AddRaw(this, &FAngelscriptCodeCoverage::OnTestsStarting);
28:    AutomationController->OnTestsComplete().AddRaw(this, &FAngelscriptCodeCoverage::OnTestsStopping);
33:    if (Type == EAutomationControllerModuleState::Type::Running) {
34:        StartRecording();
40:    FString OutputDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("CodeCoverage"));
41:    StopRecordingAndWriteReport(OutputDir);
// ★ 这里的观测能力目前绑定在测试生命周期上，不是 slua 那种常驻 live profiler
```

补充判断：

- `live profiler`：slua 当前 reference `有实现`，而且分成 runtime hook + editor 展示两条链（`LuaProfiler.cpp:222-236,328-359,441-467`; `slua_remote_profile.cpp:263-387`）。在本轮检查的 Angelscript 插件里，与它同层的 live CPU/memory profiler 应暂判 `没有实现`；最接近的是 `CodeCoverage`，但那是测试观测，不是运行期性能剖析。
- `call-site lowering`：Angelscript `有实现`，slua 在本轮阅读到的主链里 `没有实现`。slua 更注重 bridge 压缩和可视化 profiler；Angelscript 更注重脚本调用点如何 lower 到 native/custom/pointer call（`AngelscriptBytecodes.cpp:109-168`）。
- `结论修正`：如果把“slua profiler vs Angelscript StaticJIT”当成一组一对一对比项，维度其实错位了。更准确的说法是：slua 在 `D8` 上把精力压在可观测性和 bridge 成本；Angelscript 则把精力压在执行路径降阶和预编译数据。

### [维度 D11] slua 底层 Lua VM 具备 `onlyluac` 开关，但 reference 插件/示例工程并没有把它提升成正式打包策略

这一轮新增的判断是：slua reference 仓库里确实埋着 bytecode-only 能力，但它停留在 bundled Lua fork 这一层，没有被插件或 demo host 提升为正式策略。证据有两段：

1. `External/lua` 明确增加了 `lua_setonlyluac()`，parser 里也读 `L->onlyluac`。
2. 但示例 `MyGameInstance` 的 loader 仍按 `.lua -> .luac` 顺序探测，优先读源码。并且本轮对 `Reference/sluaunreal/Plugins/slua_unreal/Source` 与 `Reference/sluaunreal/Source` 检索 `lua_setonlyluac|onlyluac`，只命中 `External/lua`，没有命中插件/runtime 或 demo host 调用点。

这意味着 slua 的仓库确实为“只吃字节码”留了 VM 级能力，但 reference 工程并没有把它封装成 cook/stage/receipt 规则，更谈不上加密或签名链。Angelscript 虽然同样没有加密/验签，但它至少把 `simulate-cooked` 与 `PrecompiledScript*.Cache` 做成了插件内部协议。

```
[D11] Artifact Policy Boundary
slua Lua VM
├─ lua_setonlyluac(v)                               // VM 级开关存在
├─ parser checks L->onlyluac                        // 解析期可限制 chunk 类型
└─ plugin/demo never call setter                    // 本轮检索未见上层启用

slua host demo
├─ ProjectContentDir()/Lua
└─ probe .lua first, then .luac                     // 默认仍优先源码

Angelscript plugin
├─ -as-simulate-cooked / -as-generate-precompiled-data
├─ load PrecompiledScript*.Cache
└─ save cache as formal script artifact             // 插件正式拥有交付物协议
```

[1] 关键源码：slua 的 bytecode-only 能力停在 Lua fork，host loader 默认仍优先源码

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/External/lua/lua.h
// 文件: Reference/sluaunreal/Plugins/slua_unreal/External/lua/lstate.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/External/lua/ldo.cpp
// 文件: Reference/sluaunreal/Source/democpp/MyGameInstance.cpp
// 函数: lua_setonlyluac / f_parser / UMyGameInstance::CreateLuaState
// 位置: lua.h:152;
//       lstate.cpp:236-240;
//       ldo.cpp:763-768;
//       MyGameInstance.cpp:41-58
// 位置说明: VM 级别具备 onlyluac 开关，但 reference 工程并没有把它上升为正式加载策略
// ============================================================================
// lua.h / lstate.cpp / ldo.cpp
152:LUA_API void (lua_setonlyluac)(lua_State *L, int v);
236:  L->onlyluac = 0;
239:LUA_API void lua_setonlyluac(lua_State *L, int v) {
240:    L->onlyluac = v;
763:  if (L->onlyluac == 0) {
764:    int c = zgetc(p->z); /* read first character */
765:    if (c == LUA_SIGNATURE[0]) {
766:      checkmode(L, p->mode, "binary");
// ★ 底层 Lua fork 已经支持 onlyluac；这不是外部传闻，而是源码明确存在

// MyGameInstance.cpp
41:    state = new NS_SLUA::LuaState("SLuaMainState", this);
42:    state->setLoadFileDelegate([](const char* fn, FString& filepath)->TArray<uint8> {
45:        FString path = FPaths::ProjectContentDir();
47:        path /= "Lua";
51:        TArray<FString> luaExts = { UTF8_TO_TCHAR(".lua"), UTF8_TO_TCHAR(".luac") };
52:        for (auto& it : luaExts) {
55:            FFileHelper::LoadFileToArray(Content, *fullPath);
57:                filepath = fullPath;
58:                return MoveTemp(Content);
// ★ reference host 默认优先 `.lua`，说明 bytecode-only 并未成为正式交付策略
```

[2] 对照源码：Angelscript 把 precompiled/cooked 规则提升成插件内部协议

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: FAngelscriptEngineConfig::FromCurrentProcess / engine init / precompiled load-save
// 位置: 519-523, 1427-1469, 1513-1534, 1583-1587
// 位置说明: Angelscript 当前没有加密/验签，但它已把 script artifact 命名、加载和生成规则正式产品化
// ============================================================================
519:    Config.bSimulateCooked = FParse::Param(FCommandLine::Get(), TEXT("as-simulate-cooked"));
522:    Config.bGeneratePrecompiledData = FParse::Param(FCommandLine::Get(), TEXT("as-generate-precompiled-data"));
1427:    bUsePrecompiledData = !bGeneratePrecompiledData && !RuntimeConfig.bIgnorePrecompiledData
1433:    if (bGeneratePrecompiledData)
1438:        StaticJIT = new FAngelscriptStaticJIT();
1469:        FAngelscriptBindDatabase::Get().Load(GetScriptRootDirectory() / TEXT("Binds.Cache"), bGeneratePrecompiledData);
1513:    if (bUsePrecompiledData)
1521:        Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript_Shipping.Cache");
1529:            Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript.Cache");
1534:            PrecompiledData->Load(Filename);
1583:    if (bGeneratePrecompiledData)
1585:        FString Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript.Cache");
1586:        PrecompiledData->InitFromActiveScript();
1587:        PrecompiledData->Save(Filename);
// ★ 交付物名字、启用条件、生成时机都在插件内部固定下来，而不是完全交给宿主
```

补充判断：

- `bytecode-only enforcement`：slua 底层 `有能力`，但 reference 插件/示例工程层面当前应判为 `没有实现`。这是一个很典型的“底层 capability 存在，但产品策略没有上收”的案例。
- `artifact protocol ownership`：这里依然是 `实现方式不同`。slua 把脚本制品策略留给宿主；Angelscript 则已经把 cache/precompiled/cooked 规则写进插件核心启动链。
- `线上热更包安全链`：本轮新增证据仍然支持前文结论，即双方当前都 `没有实现` 正式的脚本加密/签名链。slua reference 更像为运营侧系统预留 ABI，而不是在插件里内建整套分发安全协议。

---
## 深化分析 (2026-04-09 23:35:36)

本轮只补三条前文没有展开透的链路：`D2` 的缓存/失效策略，`D4` 的脚本来源 ownership，`D8` 的 profiler 数据持久化与回放。重点不是重复“slua 有静态导出，Angelscript 有 StaticJIT”，而是把这些能力真正落在哪一层说清楚。

### [维度 D2] slua 把动态桥的成本压到运行时缓存生命周期，而 Angelscript 更偏向构建期可审计产物

slua 的 `static wrapper + dynamic reflection` 真正落地时，不是“生成完 wrapper 就结束”。`LuaWrapper.cpp` 先按 UE 次版本编入不同的 `LuaWrapper5.x.inc` 生成物；到了运行时，`LuaObject::push()` 还会为每个 `UClass` 构造一份 function-cache metatable，并通过 `LuaFunctionAccelerator::findOrAdd()` 挂上 `UFunction` 加速器；属性侧则由 `LuaState::ClassCache::findProp()` 做 `name -> FProperty*` 缓存。更关键的是，`NotifyUObjectDeleted()` 会同步清理属性缓存、函数缓存和 `LuaFunctionAccelerator`，说明 slua 不是简单“多缓存一点”，而是显式维护缓存生命周期。

Angelscript 这边，本轮看到的重心不同：它把主要优化压在 `UHT` 生成的 `AS_FunctionTable_*.cpp` 分片、`AS_FunctionTable_Summary.{json,csv}` 统计，以及 cooked 期的 `FAngelscriptBindDatabase`。也就是说，slua 更像“运行时把反射做便宜”，Angelscript 更像“在构建期尽量把可直绑的都变成 direct bind，让 fallback 少发生”。两者都在降桥接成本，但成本中心不在一层。

```
[D2] Runtime Lookup Ownership
sluaunreal
├─ LuaWrapper5.x.inc selected at compile time       // 静态 wrapper 按 UE 次版本切片
├─ ClassCache::findProp(UStruct, name)              // 属性名 -> FProperty 缓存
├─ cacheClassFuncRef metatable per UClass           // 类级函数元表缓存
└─ NotifyUObjectDeleted clears runtime caches       // 生命周期结束时主动失效

Angelscript
├─ UHT generates AS_FunctionTable_*.cpp             // 编译期分片生成 direct/stub 表
├─ AS_FunctionTable_Summary.{json,csv}              // 生成覆盖率反馈
├─ BindDatabase stores cooked declarations          // cooked 期绑定数据库
└─ Reflective fallback only for uncovered calls     // 未直绑时才走通用反射
```

[1] 关键源码：slua 的静态 wrapper 只是第一层，真正的热路径优化在运行时 cache + invalidation

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaWrapper.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaObject.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaState.cpp
// 函数: LuaWrapper::initExt / LuaObject::push / LuaState::ClassCache::findProp / NotifyUObjectDeleted
// 位置: LuaWrapper.cpp:55-67, 184-188;
//       LuaObject.cpp:2912-2946, 3054-3071;
//       LuaState.cpp:796-803, 1269-1296
// 位置说明: slua 把生成物、类级元表缓存、属性缓存和失效回收连成了一条链
// ============================================================================
55:#if ((ENGINE_MINOR_VERSION<25) && (ENGINE_MAJOR_VERSION==4))
56:    #include "LuaWrapper4.18.inc"
65:#elif ((ENGINE_MINOR_VERSION==4) && (ENGINE_MAJOR_VERSION==5))
66:    #include "LuaWrapper5.4.inc"
67:#endif
// ★ 同一个插件会随 UE 次版本切换不同的生成 wrapper，升级面直接落在 `.inc` 分支上

184:    void LuaWrapper::initExt(lua_State* L)
186:        init(L);
187:        FSoftObjectPtrWrapper::bind(L);
// ★ 生成 wrapper 先装基础桥，再补少量手写扩展，不是全自动到底

2912:                lua_geti(L, LUA_REGISTRYINDEX, ls->cacheClassFuncRef);
2922:                    lua_newtable(L); // function cache metatable
2939:                    lua_setmetatable(L, -2); // set metatetable of obj to "function cache metatable"
// ★ 每个 UClass 会派生一份函数缓存元表，实例共享，减少 `__index` 热路径查找

3054:        LuaWrapper::initExt(L);
3063:        lua_pushlightuserdata(L, LuaFunctionAccelerator::findOrAdd(func));
3071:        cacheFunction(L, cls);
// ★ `UFunction` 闭包不是裸包一层 closure，而是先挂 accelerator，再写入函数缓存

796:    void LuaState::NotifyUObjectDeleted(const UObjectBase * Object, int32 Index)
798:        classMap.cachePropMap.Remove((UStruct*)Object);
799:        LuaObject::removeCache(L, Object, cacheEnumRef);
800:        LuaObject::removeCache(L, Object, cacheClassPropRef);
801:        LuaObject::removeCache(L, Object, cacheClassFuncRef);
802:        LuaFunctionAccelerator::remove((UFunction*)Object);
// ★ cache 有明确失效路径；对象和函数销毁时会同步拆掉缓存

1269:    FProperty* LuaState::ClassCache::findProp(UStruct* ustruct, const char* pname)
1273:            // cache property's
1283:                    item->Add(TCHAR_TO_UTF8(*getPropertyFriendlyName(prop)), prop);
1289:                    item->Add(TCHAR_TO_UTF8(*prop->GetName()), prop);
// ★ 属性名查找也会缓存，而且 Blueprint 生成 struct 还会切 friendly name
```

[2] 对照源码：Angelscript 把 runtime fallback 维持在“少走通用路径”，并把 cooked 元数据缓存成独立数据库

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp
// 函数: FAngelscriptBindDatabase / InvokeReflectiveUFunctionFromGenericCall
// 位置: AngelscriptBindDatabase.h:2-5, 123-139;
//       BlueprintCallableReflectiveFallback.cpp:302-345
// 位置说明: Angelscript 的“缓存”更偏向可持久化的 cooked bind database；fallback 热路径里仍是逐属性搬运
// ============================================================================
2:	The bind database is a cache file generated in the editor that is used in the cooked
3:	game to correctly bind all C++ symbols to angelscript without requiring full
4:	editor metadata to be in the cooked game.
123:class FAngelscriptBindDatabase
128:	void Save(const FString& Filename);
129:	void Load(const FString& Filename, bool bGeneratingPrecompiledData);
// ★ 这里缓存的是可持久化绑定描述，目标是 cooked 运行时不依赖完整 editor metadata

302:	uint8* ParameterBuffer = static_cast<uint8*>(FMemory_Alloca(Function->ParmsSize));
310:	for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
333:		void* ScriptArgumentAddress = Generic->GetAddressOfArg(ScriptArgIndex);
345:		TargetObject->ProcessEvent(Function, ParameterBuffer);
// ★ 在本轮看到的 fallback 热路径里，Angelscript 没有 slua 那种长期 `LuaFunctionAccelerator` 对象
```

[3] 对照源码：Angelscript UHT 生成链把“直绑覆盖率”做成可审计工件

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs
// 函数: BuildModule / WriteGenerationSummary
// 位置: 115-139, 166-205
// 位置说明: Angelscript 的生成阶段不仅产出 shard，还同步产出 summary/csv，方便审计 direct/stub 覆盖率
// ============================================================================
115:		int shardCount = (entries.Count + MaxEntriesPerShard - 1) / MaxEntriesPerShard;
120:			string outputPath = factory.MakePath($"AS_FunctionTable_{module.ShortName}_{shardIndex:D3}", ".cpp");
133:					entry.EraseMacro == "ERASE_NO_FUNCTION()" ? "Stub" : "Direct",
// ★ 生成物会明确标出每条 entry 是 direct 还是 stub

174:		string summaryPath = factory.MakePath("AS_FunctionTable_Summary", ".json");
177:		string summaryJson = JsonSerializer.Serialize(
204:		File.WriteAllText(summaryPath, summaryJson, Encoding.UTF8);
205:		WriteModuleSummaryCsv(factory, moduleSummaries);
// ★ 生成阶段同时产出 shard 和 summary，优化点偏向“直绑覆盖率可审计”
```

补充判断：

- `UFunction` 热路径优化：双方都有，但 `实现方式不同`。slua 用 runtime accelerator + class/metatable cache；Angelscript 用 direct bind 覆盖率和 cooked bind database，把 fallback 变成少数路径。
- `fallback runtime accelerator`：slua `有实现`；在本轮阅读到的 Angelscript reflective fallback 主链里，同类长期对象缓存应判为 `没有实现同类机制`，但这不等于性能一定差，因为它优先策略是少走 fallback。
- `升级维护面`：slua 的 `LuaWrapper4.18/4.25/5.1/5.2/5.3/5.4.inc` 说明它把一部分版本适配成本摊在生成物分叉上；Angelscript 当前更依赖统一 generator 和 summary 输出来审计覆盖率，这里属于 `实现质量差异`，偏向“可审计性更强”。

### [维度 D4] slua 的热更新能力核心是“脚本来源可替换”，不是“插件自己负责分发与回归”；Angelscript 则把开发期闭环内建进插件

如果只看 reference 源码，slua 的关键抽象不是 `FileWatcher`，而是 `LoadFileDelegate`。`LuaState::doFile()` 会先 `loadFile()`，`requireModule()` 只负责调 Lua `require`；脚本到底来自本地目录、下载包、运营 patch 还是自定义容器，决定权都在宿主装进去的 delegate。`LuaSimulate` 在 Editor 里也沿用这套思路：先检查 `Delegate` 是否存在，再新建一个临时 `LuaState`，对 `EditorPreview` 世界里的对象做 `hookObject()`。因此 slua reference 真正内建的是“替换入口”和“对象 hook 机制”，不是整套线上热更工作流。

Angelscript 的 ownership 刚好相反。`AngelscriptEditorModule` 在模块启动时直接注册 `DirectoryWatcher`；`PerformHotReload()` 会扩展依赖、预处理、编译、重新挂断点；`HotReloadTestRunner` 还会按 batch 执行单测，目录监听本身也有独立自动化测试覆盖新增、删除、文件夹重命名等细节。换句话说，Angelscript 的热重载是插件产品化能力；slua reference 展示的是“插件给宿主留 seam，生产热更系统由宿主自己接”。

```
[D4] Script Replacement Ownership
sluaunreal
├─ host installs LoadFileDelegate                   // 宿主决定脚本从哪里来
├─ LuaState::doFile / requireModule                 // 插件只负责执行与 require
├─ LuaSimulate spins temporary LuaState             // EditorPreview 临时模拟态
└─ hookObject(obj) -> tryHook(obj)                  // 对象级事件入口接管

Angelscript
├─ DirectoryWatcher watches all script roots        // 插件自己监听脚本目录
├─ QueueScriptFileChanges -> reload queues          // 变更先进入统一队列
├─ PerformHotReload expands deps + recompiles       // 插件内部完成重编译闭环
└─ HotReloadTestRunner batches unit tests           // 热重载后自动回归验证
```

[1] 关键源码：slua 把“脚本从哪里来”的决策权显式交给宿主，Editor 模拟也复用这套 seam

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaState.h
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaState.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaSimulate.cpp
// 文件: Reference/sluaunreal/Source/democpp/MyGameInstance.cpp
// 函数: setLoadFileDelegate / doFile / requireModule / StartSimulateLua / hookObject / CreateLuaState
// 位置: LuaState.h:167-189;
//       LuaState.cpp:651-653, 755-784, 977-998;
//       LuaSimulate.cpp:22-32, 98-109;
//       MyGameInstance.cpp:41-63
// 位置说明: slua reference 插件内建的是脚本装载 seam 和对象 hook，不是完整分发系统
// ============================================================================
167:        // load file and execute it
168:        // file how to loading depend on load delegation
169:        // see setLoadFileDelegate function
188:        // set load delegation function to load lua code
189:        void setLoadFileDelegate(LoadFileDelegate func);
// ★ 头文件已经把责任划分写明：文件怎么加载，取决于宿主提供的 delegate

651:    void LuaState::setLoadFileDelegate(LoadFileDelegate func) {
652:        loadFileDelegate = func;
755:    LuaVar LuaState::doFile(const char* fn, LuaVar* pEnv) {
757:        TArray<uint8> buf = loadFile(fn, filepath);
768:    LuaVar LuaState::requireModule(const char* fn, LuaVar* pEnv) {
770:        lua_getglobal(L, "require");
// ★ 插件自己不关心来源，只关心 `loadFile -> doBuffer` 和 `require`

977:    bool LuaState::hookObject(LuaState* inState, const UObjectBaseUtility* obj, bool bHookImmediate/* = false*/, bool bPostLoad/* = false*/)
985:            return state->overrider->tryHook(obj, bHookImmediate, bPostLoad);
// ★ 真正热替换到对象层时，入口是 `hookObject -> overrider->tryHook`

22:    LuaState::LoadFileDelegate LuaSimulate::Delegate = nullptr;
29:    void LuaSimulate::SetLuaFileLoader(LuaState::LoadFileDelegate InDelegate)
30:    {
31:        Delegate = InDelegate;
98:    void LuaSimulate::StartSimulateLua()
100:        if (Delegate == nullptr)
102:            Log::Error("lua Simulation Error. LoadFileDelegate not set.");
106:        SluaState = new NS_SLUA::LuaState("", nullptr);
107:        SluaState->setLoadFileDelegate(Delegate);
// ★ Editor 模拟也不自己找文件；没有宿主 delegate 就无法工作

41:	state = new NS_SLUA::LuaState("SLuaMainState", this);
42:	state->setLoadFileDelegate([](const char* fn, FString& filepath)->TArray<uint8> {
45:		FString path = FPaths::ProjectContentDir();
47:		path /= "Lua";
51:		TArray<FString> luaExts = { UTF8_TO_TCHAR(".lua"), UTF8_TO_TCHAR(".luac") };
52:		for (auto& it : luaExts) {
// ★ demo host 只演示了“从 ProjectContentDir()/Lua 读 `.lua/.luac`”；网络分发并不在 reference 插件里
```

[2] 对照源码：Angelscript 把目录监听、依赖扩展、重编译和热重载后测试都写进插件核心

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Testing/UnitTest.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp
// 函数: StartupModule / PerformHotReload / CheckForHotReload / PrepareTests / RunTests
// 位置: AngelscriptEditorModule.cpp:366-380;
//       AngelscriptEngine.cpp:2253-2490, 2729-2818;
//       UnitTest.cpp:531-654;
//       AngelscriptDirectoryWatcherTests.cpp:75-222
// 位置说明: Angelscript 热重载是插件内部闭环，并且把 watcher 行为当作受测契约维护
// ============================================================================
366:	// Register a directory watch on the script directory so we know when to reload
372:		TArray<FString> AllRootPaths = FAngelscriptEngine::MakeAllScriptRoots();
376:			DirectoryWatcher->RegisterDirectoryChangedCallback_Handle(
378:				IDirectoryWatcher::FDirectoryChanged::CreateStatic(&OnScriptFileChanges),
// ★ 插件自己监听所有 script root，不把检测责任外包给宿主

2282:	// Build a set of all files which are dependent on any of the modified files,
2284:	TSet<FFilenamePair> FilesToHotReload;
2317:				// We will need to progressively mark all modules that depend on one of the files that should be reloaded
2368:				for (asCModule* ReloadModule : MarkedModules)
2455:	bool bPreprocessSuccess = Preprocessor.Preprocess();
2468:	ECompileResult Result = CompileModules(CompileType, Preprocessor.GetModulesToCompile(), CompiledModules);
// ★ 热重载不只是“文件变了就重载”，还有依赖扩展、预处理和编译阶段

2481:	if (GEngine && bCompletedAssetScan && HotReloadTestRunner != nullptr && HotReloadTestRunner->ShouldRunUnitTestsOnHotReload())
2489:		HotReloadTestRunner->PrepareTests(GetActiveModules(), CompiledModules, RelativeFileList, ShouldUseAutomaticImportMethod());
2801:			bool AllUnitTestsPass = HotReloadTestRunner->RunTests(this);
// ★ 编译完成后还有自动回归门禁，这一点和 slua reference 的 seam 设计不是同一个层级

94:	AngelscriptEditor::Private::QueueScriptFileChanges(Changes, Engine->AllRootPaths, *Engine, FileManager, [](const FString&)
99:	TestEqual(TEXT("DirectoryWatcher.Queue.ScriptAddAndRemove should queue one added script"), Engine->FileChangesDetectedForReload.Num(), 1);
156:	TestEqual(TEXT("DirectoryWatcher.Queue.FolderAddScansContainedScripts should queue the two script files in the new folder"), Engine->FileChangesDetectedForReload.Num(), 2);
189:	TestEqual(TEXT("DirectoryWatcher.Queue.FolderRemoveUsesLoadedScriptEnumerator should queue two removed scripts from the enumerator"), Engine->FileDeletionsDetectedForReload.Num(), 2);
219:	TestEqual(TEXT("DirectoryWatcher.Queue.RenameWindowTracksRemoveAndAdd should queue one removed script"), Engine->FileDeletionsDetectedForReload.Num(), 1);
// ★ 目录监听队列行为本身有自动化测试，说明这条链被当成正式产品行为维护
```

补充判断：

- `插件层线上热更分发`：slua reference 当前应判为 `没有实现`。源码里能坐实的是 `LoadFileDelegate` seam，而不是下载器、patch manager 或签名校验链。
- `脚本来源可替换能力`：slua `有实现`，而且是核心抽象；Angelscript 当前没有同级别“宿主自定义脚本来源” seam，更多是 `开发期文件系统` ownership。
- `开发期热重载闭环`：Angelscript `有实现`；slua reference 只实现了 `EditorPreview` 场景下的模拟 hook，没有看到同层级的 watcher/dependency/test gate，因此这里应判为 `没有实现同层能力`。
- `场景差异`：这不是简单的优劣关系。slua 的设计更适合把线上替换责任让给游戏项目或运营系统；Angelscript 的设计更适合把开发期安全性和回归验证收归插件自己负责。

### [维度 D8] slua 的 profiler 不只是“边跑边看”，而是一条带异步处理、压缩落盘和回放的观测资产链；Angelscript 当前更像“执行优化 + 分散式 instrumentation”

前文已经说明 slua profiler 通过 `lua_sethook` 和 socket/本地队列采样。本轮补到的关键点是：它并不把 profiler 只当成 live UI，而是把 profile 数据变成正式工件。`FProfileDataProcessRunnable` 会把 CPU 与 memory 事件分队列异步处理，录制时直接写 `.sluastat`，保存/加载时按 chunk 压缩，UE 新版本甚至切到 `Oodle`；`SProfilerInspector` 提供显式的 save/load 按钮。也就是说，slua profiler 产出的不是临时调试信息，而是一种可以离线共享、回放、二次分析的 profiling artifact。

Angelscript 当前在这个维度上的能力更分散。它当然不是“没有任何观测”：有 `FCpuProfilerTraceScoped` 这种把脚本区段打到 Unreal Trace 的手动埋点，也有 `FAngelscriptPrecompiledData::OutputTimingData()` 这种内部阶段 timing 输出，还有测试期 `CodeCoverage`。但这些都不是 slua 那种“一套 runtime 采样协议 + 异步处理器 + editor viewer + 离线文件格式”的产品化 profiler。所以这里最准确的判断不是“slua profiler 比 StaticJIT 更强”，而是“一个在造观测资产，一个在造更便宜的执行路径”。

```
[D8] Observation Artifact Pipeline
sluaunreal
├─ LuaProfiler::tick / lua_sethook                  // 采样 call/return/tick/memory
├─ FProfileDataProcessRunnable queues CPU/MEM       // 异步处理命令
├─ SaveDataWithData -> .sluastat chunks             // 压缩落盘成离线工件
└─ SProfilerInspector load/save + replay            // Editor 侧回放与查看

Angelscript
├─ FCpuProfilerTraceScoped                          // 借 Unreal Trace 做手动埋点
├─ FAngelscriptScopeTimer::OutputTime               // 输出内部阶段 timing
├─ CodeCoverage hooks AutomationController          // 测试期覆盖率
└─ no built-in script profiler UI/artifact chain    // 本轮源码未见同层产品化 profiler
```

[1] 关键源码：slua profiler 有完整的“录制 -> 异步处理 -> 压缩落盘 -> 回放”链路

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaProfiler.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/SluaProfilerDataManager.cpp
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_profile/Private/slua_profile_inspector.cpp
// 函数: onChangeRecordState / init / tick / ReceiveProfileData / StartRecord / SaveDataWithData / LoadData / OnSaveFileBtnClicked / OnLoadFileBtnClicked
// 位置: LuaProfiler.cpp:411-420, 441-467, 478-503;
//       SluaProfilerDataManager.cpp:77-105, 292-449, 598-705;
//       slua_profile_inspector.cpp:276-283, 354-381
// 位置说明: slua profiler 不是单纯 live socket 观察，而是完整的 profiling artifact pipeline
// ============================================================================
411:        int onChangeRecordState(lua_State* L)
414:            if(isBegin)
416:                SluaProfilerDataManager::BeginRecord();
420:                SluaProfilerDataManager::EndRecord();
// ★ 录制状态由 native 函数切换，直接进入 data manager

446:        profiler = LS->doBuffer((const uint8*)ProfilerScript,strlen(ProfilerScript), ChunkName);
449:        lua_pushcfunction(L, changeHookState);
453:        lua_pushcfunction(L, setSocket);
459:        lua_pushcfunction(L, onChangeRecordState);
467:        lua_setglobal(L, "slua_profile");
// ★ profiler 脚本不是外部工具孤立运行，而是被注入 Lua VM 成为全局 `slua_profile`

492:            if(checkSocketRead()) memoryGC(L);
493:            takeMemorySample(PHE_MEMORY_INCREACE, LuaMemoryProfile::memIncreaceThisFrame(LS), L);
494:            takeSample(PHE_TICK, -1, "", "", getTime(), L);
498:            SluaProfilerDataManager::ReceiveMemoryData(PHE_MEMORY_INCREACE, LuaMemoryProfile::memIncreaceThisFrame(LS));
499:            SluaProfilerDataManager::ReceiveProfileData(PHE_TICK, getTime() - profileTotalCost, -1, "", "");
// ★ 连不上远端时也不会丢观测，直接切回本地 data manager

77:void SluaProfilerDataManager::SaveDataWithData(int inCpuViewBeginIndex, int inMemViewBeginIndex, ProfileNodeArrayArray& inProfileData, const MemNodeInfoList& inLuaMemNodeList)
81:        ProcessRunnable = new FProfileDataProcessRunnable();
89:    void SluaProfilerDataManager::BeginRecord()
97:        ProcessRunnable->StartRecord();
// ★ manager 按需拉起 runnable，不要求 profiler UI 先存在

298:    cpuCommandQueue.Enqueue({hookEvent, time, lineDefined, funcName, shortSrc});
299:    commandTypeQueue.Enqueue(FCommandType::ECPU);
308:    memoryCommandQueue.Enqueue({hookEvent, memInfoList});
309:    commandTypeQueue.Enqueue(FCommandType::EMemory);
// ★ CPU 与 memory 事件先入队列，采样线程和处理线程被解耦

344:    FString filePath = GenerateStatFilePath();
345:    frameArchive = IFileManager::Get().CreateFileWriter(*filePath);
347:    *frameArchive << ProfileVersion;
// ★ 开始录制时就直接打开 `.sluastat` 文件，profiling output 是正式工件

622:        if (dataToCompress.Num() > CompressedSize)
624:            SerializeCompreesedDataToFile(*ar);
647:    FString filePath = FPaths::ProfilingDir() + "/Sluastats/"
648:        / FString::FromInt(now.GetYear()) + FString::FromInt(now.GetMonth()) + FString::FromInt(now.GetDay())
650:        + FString::FromInt(now.GetMillisecond()) + ".sluastat";
695:            FCompression::UncompressMemory(COMPRESS_ZLIB, uncompressedBuffer.GetData(), uncompressedSize, compressedBuffer, compressedSize);
697:            FCompression::UncompressMemory(NAME_Zlib, uncompressedBuffer.GetData(), uncompressedSize, compressedBuffer, compressedSize);
699:            FCompression::UncompressMemory(NAME_Oodle, uncompressedBuffer.GetData(), uncompressedSize, compressedBuffer, compressedSize);
// ★ 保存/加载都走分块压缩，而且会随 UE 版本切压缩后端

276:    void SProfilerInspector::OnSaveFileBtnClicked()
283:    SluaProfilerDataManager::SaveDataWithData(cpuViewBeginIndex, memViewBeginIndex, allProfileData, allLuaMemNodeList);
354:    void SProfilerInspector::OnLoadFileBtnClicked()
381:    SluaProfilerDataManager::LoadData(loadPath, cpuViewBeginIndex, memViewBeginIndex, allProfileData, allLuaMemNodeList);
// ★ Editor inspector 明确支持保存与回放，说明这不是一次性的 live 调试窗口
```

[2] 对照源码：Angelscript 有 instrumentation 和 timing，但当前未见 slua 这类产品化 script profiler 资产链

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/FCpuProfilerTraceScoped.h
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FCpuProfilerTraceScoped.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/AngelscriptCodeCoverage.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp
// 函数: FCpuProfilerTraceScoped / Bind_TraceCPUProfilerEventScoped / AddTestFrameworkHooks / OutputTimingData
// 位置: FCpuProfilerTraceScoped.h:14-22;
//       Bind_FCpuProfilerTraceScoped.cpp:4-13;
//       AngelscriptCodeCoverage.cpp:22-64;
//       PrecompiledData.cpp:2973-3001
// 位置说明: Angelscript 当前的观测能力分散在 Unreal Trace、测试覆盖率和内部阶段 timing，上层没有看到 slua 式统一 profiler 工件链
// ============================================================================
15:	FCpuProfilerTraceScoped(const FName& EventID)
17:		FCpuProfilerTrace::OutputBeginDynamicEvent(EventID);
20:	~FCpuProfilerTraceScoped()
22:		FCpuProfilerTrace::OutputEndEvent();
// ★ 这是把区段打到 Unreal Trace 的手动 instrumentation，不是脚本 VM 自带 profiler 协议

4:AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_TraceCPUProfilerEventScoped(FAngelscriptBinds::EOrder::Late, []
8:	FCpuProfilerTraceScoped_.Constructor("void f(const FName& EventID)", [](FCpuProfilerTraceScoped* Address, const FName& EventID)
// ★ 插件把这类 trace scope 暴露给脚本，但没有附带 viewer/protocol/file format

22:void FAngelscriptCodeCoverage::AddTestFrameworkHooks()
27:	AutomationController->OnTestsAvailable().AddRaw(this, &FAngelscriptCodeCoverage::OnTestsStarting);
28:	AutomationController->OnTestsComplete().AddRaw(this, &FAngelscriptCodeCoverage::OnTestsStopping);
59:void FAngelscriptCodeCoverage::StopRecordingAndWriteReport(const FString& OutputDir)
63:	WriteReportHtml(OutputDir);
64:	WriteCoverageSummaries(OutputDir);
// ★ 覆盖率是测试生命周期工件，不是运行时 CPU/memory profiler

2973:void FAngelscriptPrecompiledData::OutputTimingData()
2975:	FAngelscriptScopeTimer::OutputTime(TEXT("ProcessBytecode"), TIMER_ProcessBytecode);
2998:	FAngelscriptScopeTimer::OutputTime(TEXT("FunctionLookup"), TIMER_FunctionLookup);
3000:	FAngelscriptScopeTimer::OutputTime(TEXT("PropertyLookup"), TIMER_PropertyLookup);
// ★ 这里输出的是引擎内部阶段 timing，帮助分析 StaticJIT/precompiled pipeline，本质上仍是内部 instrumentation
```

补充判断：

- `runtime script profiler + offline artifact`：slua `有实现`，而且是完整链；Angelscript 在本轮阅读范围内应判为 `没有实现同层能力`。
- `手动接入 Unreal Trace`：Angelscript `有实现`，slua reference 这轮没有看到等价的脚本侧 trace scope 暴露；但这和 slua profiler 不同层，属于 `实现方式不同`。
- `性能优化主轴`：slua profiler 解决的是“怎么观察瓶颈”；Angelscript `StaticJIT` 解决的是“怎么减少调用与加载成本”。把两者直接做强弱排名并不准确，它们在 `D8` 上回答的是不同问题。

---

## 深化分析 (2026-04-09 23:47:24)

### [维度 D3 / D4 / D8] slua 的 Blueprint override 真正落点是 `FFrame` trampoline；Angelscript 把 override 合同前移到类生成阶段

前文已经证明 slua 能覆写 Blueprint 事件，但这一轮把 `ULuaOverrider::luaOverrideFunc()` 和 Angelscript `ClassGenerator` 并排读之后，可以把差异说得更精确：slua 的 override 不是“先生成一批最终 `UFunction`，再按普通函数调用”，而是**调用发生时**才进入一个 native trampoline，现场检查 `Stack.CurrentNativeFunction`、必要时重建 `locals`、再按对象实例查 Lua 实现。如果 Lua 没接住，它还会继续改写 `Stack.PropertyChainForCompiledIn`、修补 `FOutParmRec::Property`，然后跳回隐藏的 `__overrider_*` super 函数。换句话说，slua 把 override 的主要复杂度放在了**调用期 FFrame 改写**。

Angelscript 的路径几乎反过来。`FAngelscriptClassGenerator` 会在分析期就验证 `BlueprintOverride` 是否真的存在于父类、是否 `BlueprintEvent`、签名是否一致、`EditorOnly` 约束是否匹配；通过之后再创建新的 `UFunction`、挂 flags、补 return/arg property、`StaticLink()` 并塞进 generated class 的 function map。也就是说，Angelscript 把 override 的主要复杂度放在**生成期合同校验 + 类元数据实体化**，而不是每次调用都去改写 Blueprint VM 的现场状态。

```
[D3/D4/D8] Override Dispatch Layer
sluaunreal
├─ native UFunction entry -> luaOverrideFunc()      // 先进入 trampoline
├─ maybe rebuild locals from FFrame::Code           // ProcessContextOpcode 特判
├─ objectTableMap[obj] -> getLuaFunction()          // 运行时查实例 Lua 实现
├─ luaFunc.callByUFunction(...)                     // 依当前 UFunction 形状编组
└─ fallback rewrites OutParms / PropertyChain       // 再跳 hidden super

Angelscript
├─ ClassGenerator validates BlueprintOverride       // 分析期校验父类/签名/EditorOnly
├─ Generate UFunction + return/arg properties       // 生成期把签名实体化
├─ AddFunctionToFunctionMap + StaticLink            // 类表注册完成
└─ runtime call uses generated function contract    // 调用期不再改写 FFrame
```

[1] 关键源码：slua 的 override 入口会在调用期直接改写 `FFrame`

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaOverrider.cpp
// 函数: ULuaOverrider::luaOverrideFunc
// 位置: 63-118, 155-177, 183-243, 251-265
// 位置说明: slua 的 Blueprint override 不是静态生成函数体，而是调用期 trampoline
// ============================================================================
63:{
64:    UFunction* func = Stack.Node;
72:    uint8* locals = Stack.Locals;
83:    if (Stack.CurrentNativeFunction)
86:        if (Stack.CurrentNativeFunction != func)
91:            bContextOp = true;
92:            func = Stack.CurrentNativeFunction;
97:                locals = (uint8*)FMemory_Alloca(func->PropertiesSize);
98:                FMemory::Memzero(locals, func->PropertiesSize);
103:                for (auto it = CastField<FProperty>(func->ChildProperties); *Stack.Code != EX_EndFunctionParms; it = CastField<FProperty>(it->Next))
106:                    it->InitializeValue_InContainer(locals);
107:                    Stack.Step(Stack.Object, it->ContainerPtrToValuePtr<uint8>(locals));
118:                Stack.SkipCode(1);
// ★ 这里直接在调用期重建参数缓冲区；override 逻辑依赖 FFrame 当前现场

156:    FString functionName = func->GetName();
167:        auto& tableMap = objectTableMap.FindChecked(L);
168:        FObjectTable* objTable = tableMap.Find(obj);
173:            NS_SLUA::LuaVar luaFunc = getLuaFunction(L, obj, luaSelfTable, functionName);
177:                luaFunc.callByUFunction(func, locals, bContextOp ? nullptr : Stack.OutParms, luaSelfTable);
178:                bCallSuper = false;
// ★ Lua 端调用依然按当前 UFunction 的反射形状编组，而不是提前生成专用 stub

189:            uint8* savedCode = Stack.Code;
191:            auto savedPropertyChain = Stack.PropertyChainForCompiledIn;
204:                    if (prop->HasAnyPropertyFlags(CPF_OutParm))
207:                        lastOut->Property = prop;
215:            Stack.PropertyChainForCompiledIn = superFunction->ChildProperties;
229:                    Stack.Code = newCode;
230:                    Stack.Node = superFunction;
231:                    superFunction->Invoke(obj, Stack, RESULT_PARAM);
241:            Stack.PropertyChainForCompiledIn = savedPropertyChain;
243:            Stack.Code = savedCode;
// ★ fallback 不是普通 `Super::` 调用，而是现场改写 `PropertyChain/Code/Node` 后再 Invoke

251:    if (bCallFromNative || !Stack.OutParms || bContextOp)
253:        if (!bCallSuper && (RESULT_PARAM && func->ReturnValueOffset != MAX_uint16))
261:            uint8* outParam = out ? out->PropAddr : locals + prop->GetOffset_ForInternal();
264:                prop->CopyCompleteValueToScriptVM(RESULT_PARAM, outParam);
// ★ 返回值和 out 参数也在 trampoline 末尾手动回填到 ScriptVM
```

[2] 对照源码：Angelscript 在生成期先验校验，再把 override 变成正式 `UFunction`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 函数: FAngelscriptClassGenerator::AnalyzeClass / 生成函数阶段
// 位置: 732-788, 3473-3640
// 位置说明: Angelscript 把 BlueprintOverride 的合法性和函数形状前移到类生成阶段
// ============================================================================
732:		// Check that BlueprintOverride actually overrides something from a superclass
733:		if (FunctionDesc->bBlueprintOverride)
741:			if (ParentFunction == nullptr)
761:					if (!SuperFunctionDesc.IsValid())
763:						FAngelscriptEngine::Get().ScriptCompileError(... "does not exist in superclass" ...);
768:					else if (!SuperFunctionDesc->bBlueprintEvent && !SuperFunctionDesc->bBlueprintOverride)
770:						FAngelscriptEngine::Get().ScriptCompileError(... "is not marked BlueprintEvent" ...);
775:					else if (!SuperFunctionDesc->SignatureMatches(FunctionDesc))
777:						FAngelscriptEngine::Get().ScriptCompileError(... "does not match signature" ...);
782:					else if (SuperFunctionDesc->Meta.Contains(NAME_Meta_EditorOnly) && !FunctionDesc->Meta.Contains(NAME_Meta_EditorOnly))
784:						FAngelscriptEngine::Get().ScriptCompileError(... "editor-only parent function" ...);
// ★ 合法性错误在分析期就报成 compile error，而不是等到调用期才兜底

3473:		if ((FunctionDesc->bBlueprintEvent && FunctionDesc->bCanOverrideEvent) || FunctionDesc->bBlueprintOverride)
3474:			NewFunction->FunctionFlags |= FUNC_BlueprintEvent;
3529:		FProperty* ReturnProperty = AddFunctionReturnType(NewFunction, FunctionDesc->ReturnType);
3584:			FProperty* Prop = AddFunctionArgument(NewFunction, ArgDesc);
3603:			NewProperty->Next = NewFunction->ChildProperties;
3604:			NewFunction->ChildProperties = NewProperty;
3632:		NewFunction->Next = NewClass->Children;
3633:		NewClass->Children = NewFunction;
3636:		NewClass->AddFunctionToFunctionMap(NewFunction, NewFunction->GetFName());
3639:		NewFunction->StaticLink(true);
3640:		NewFunction->FinalizeArguments();
// ★ 通过校验后，override 会变成 generated class 上的正式 UFunction 与参数链
```

补充判断：

- `Blueprint override 合法性检查`：两边都 `有实现`，但 `实现方式不同`。slua 主要靠调用期 `getLuaFunction()` / fallback 和日志兜底；Angelscript 在分析期就用 `SignatureMatches()` 与父类查找强约束。
- `调用期 VM 现场稳定性`：这里存在 `实现质量差异`。slua 需要在热路径上重写 `FFrame`、`OutParms` 与 `PropertyChain`；Angelscript 把大部分复杂度前移到 generated class 构建。
- `对象实例级动态换绑`：slua 这里不是 `没有实现`，而是 `实现方式不同`。它通过 `objectTableMap[obj]` 走实例粒度；Angelscript 更偏 `class/module` 粒度的生成与重载。

### [维度 D3 / D2] Blueprint 主动调用链的错误暴露时机：slua 是 `FLuaBPVar` 动态隧道，Angelscript 是签名化 reflective fallback

前文已经多次说明 `FLuaBPVar` 是 slua 的动态值总线。本轮新增的不是“它很动态”这件事，而是**动态性具体落在哪个阶段**。`ULuaBlueprintLibrary::CallToLuaWithArgs()` 在 Blueprint 节点触发时只检查 `GameInstance / LuaState / FunctionName`，然后把每个 `FLuaBPVar.value` 直接压进 Lua 栈。真正的类型/索引问题，并不会在调用前暴露，而是延迟到 Blueprint 侧再通过 `GetIntFromVar()` / `GetObjectFromVar()` 这类 helper 读回时，才用 `FFrame::KismetExecutionMessage()` 发 warning。

Angelscript 的 reflective fallback 刚好把边界放得更早。它在绑定时先拒绝 `CustomThunk` 与超参数函数；运行时调用再严格按 `UFunction->ParmsSize` 分配参数缓冲区、逐个 `FProperty::CopySingleValue()` 拷贝、跟踪可写引用参数、`ProcessEvent()` 后再把 `out/ref` 与 return 拷回脚本调用栈。也就是说，Angelscript 在 Blueprint 交互上虽然也有反射 fallback，但它保住了**每个参数的静态 property 形状**，而不是把整条链压成 `FunctionName + TArray<FLuaBPVar>`。

```
[D3/D2] Blueprint -> Script Call Shape
sluaunreal
Blueprint Node
├─ CallToLuaWithArgs(FunctionName, Args[])         // 只传字符串名和变体数组
├─ each FLuaBPVar.value.push(L)                    // 不检查 UFunction 形状
├─ Lua global lookup by string
└─ Get*FromVar(Index) on readback
   └─ invalid type/index -> KismetExecutionMessage // 错误暴露更晚

Angelscript
Bind phase
├─ reject CustomThunk / arg>16
└─ keep per-argument FProperty shape

Call phase
├─ alloc ParameterBuffer(ParmsSize)
├─ CopySingleValue each arg by property
├─ ProcessEvent(Function, Buffer)
└─ copy back out/ref + return
```

[1] 关键源码：slua 的 Blueprint 节点在入口处并不校验函数签名，错误更多延迟到读回阶段

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaBlueprintLibrary.cpp
// 函数: ULuaBlueprintLibrary::CallToLuaWithArgs / getValueFromVar
// 位置: 33-76, 217-233
// 位置说明: Blueprint 主动调 Lua 的入口是“函数名 + 变体数组”，而不是 UFunction 级签名
// ============================================================================
33:ULuaBlueprintLibrary::ULuaBlueprintLibrary(const FObjectInitializer& ObjectInitializer)
36:    FBlueprintSupport::RegisterBlueprintWarning(
42:    FBlueprintSupport::RegisterBlueprintWarning(
// ★ 节点层真正预注册的是 warning 类型，而不是静态函数签名约束

51:FLuaBPVar ULuaBlueprintLibrary::CallToLuaWithArgs(UObject* WorldContextObject, FString funcname,const TArray<FLuaBPVar>& args,FString StateName) {
60:    auto ls = LuaState::get(gameInstance);
63:    LuaVar f = ls->get(TCHAR_TO_UTF8(*funcname));
64:    if (!f.isFunction()) {
65:        Log::Error("Can't find lua member function named %s to call", TCHAR_TO_UTF8(*funcname));
69:    auto fillParam = [&]
71:        for (auto& arg : args) {
72:            arg.value.push(ls->getLuaState());
74:        return args.Num();
76:    return f.callWithNArg(fillParam);
// ★ 调用入口只知道“函数名 + FLuaBPVar 数组”；每个参数直接 push 到 Lua 栈

217:    template<class T>
218:    T getValueFromVar(const FLuaBPVar& Value,int Index) {
221:        if(Index<=lv.count()) {
223:            if(getValue(lv,Index,v))
224:                return v;
226:                FFrame::KismetExecutionMessage(*FString::Printf(TEXT("Attempted to index an item from an invalid type!")),
230:            FFrame::KismetExecutionMessage(*FString::Printf(TEXT("Attempted to index an item from an invalid index from BpVar [%d/%d]!"),
233:        return T();
// ★ 类型/索引错误主要在读回时才被 Blueprint warning 暴露
```

[2] 对照源码：Angelscript 的 fallback 会保留 `UFunction` 参数形状，并在绑定期就筛掉不兼容函数

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp
// 函数: EvaluateReflectiveFallbackEligibility / InvokeReflectiveUFunctionFromGenericCall
// 位置: 18-57, 272-390
// 位置说明: fallback 虽然是反射调用，但入口单位仍是具体 UFunction 签名
// ============================================================================
18:	const FName NAME_BlueprintCallableReflectiveFallback_CustomThunk(TEXT("CustomThunk"));
26:	struct FBlueprintCallableReflectiveSignature
28:		FAngelscriptTypeUsage ReturnType;
29:		FAngelscriptTypeUsage Arguments[BlueprintCallableReflectiveFallbackMaxArgs];
31:		UFunction* UnrealFunction = nullptr;
// ★ fallback 自己维护的是“签名对象”，不是通用 variant 包

51:	void InitializeParameterBuffer(const UFunction* Function, uint8* Buffer)
53:		FMemory::Memzero(Buffer, Function->ParmsSize);
54:		for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
56:			It->InitializeValue_InContainer(Buffer);
// ★ 调用前先按 `ParmsSize` 和 `FProperty` 形状初始化参数缓冲区

272:	if (Function->HasMetaData(NAME_BlueprintCallableReflectiveFallback_CustomThunk))
274:		return EAngelscriptReflectiveFallbackEligibility::RejectedCustomThunk;
277:	if (GetNonReturnParameterCount(Function) > BlueprintCallableReflectiveFallbackMaxArgs)
279:		return EAngelscriptReflectiveFallbackEligibility::RejectedTooManyArguments;
// ★ 不兼容函数在绑定期直接拒绝，不进入运行时兜底

302:	uint8* ParameterBuffer = static_cast<uint8*>(FMemory_Alloca(Function->ParmsSize));
303:	InitializeParameterBuffer(Function, ParameterBuffer);
310:	for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
318:		void* Destination = Property->ContainerPtrToValuePtr<void>(ParameterBuffer);
333:		void* ScriptArgumentAddress = Generic->GetAddressOfArg(ScriptArgIndex);
335:		Property->CopySingleValue(Destination, SourceAddress);
337:		if (Property->HasAnyPropertyFlags(CPF_ReferenceParm) && !Property->HasAnyPropertyFlags(CPF_ConstParm))
339:			OutReferences[OutReferenceCount++] = { Property, SourceAddress };
345:	TargetObject->ProcessEvent(Function, ParameterBuffer);
352:			OutReference.Property->CopySingleValue(
358:	if (FProperty* ReturnProperty = Function->GetReturnProperty())
364:			ReturnProperty->CopySingleValue(
370:	DestroyParameterBuffer(Function, ParameterBuffer);
// ★ 参数、out/ref 和返回值都按具体 FProperty 形状搬运，不靠通用变体隧道
```

补充判断：

- `Blueprint 主动调用脚本`：两边都 `有实现`，但 `实现方式不同`。slua 是 `FunctionName + FLuaBPVar[]` 动态隧道；Angelscript 是逐 `UFunction` 的签名化 fallback。
- `错误暴露时机`：这里有明显 `实现质量差异`。slua 更多在 readback/helper 阶段发 warning；Angelscript 会先做 eligibility 筛选，再按具体 `FProperty` 调用。
- `CustomThunk 处理`：不要误判成 slua “支持更多”。更准确的说法是 `实现方式不同`：Angelscript 明确拒绝 `CustomThunk` fallback；slua 这条链根本不以 `CustomThunk UFunction` 为调用单位。

### [维度 D1 / D6] slua 的 codegen 所有权实际落在“Editor 壳 + 外部二进制工具”，而 Angelscript 的 exporter 是仓库内可审计源码

前文已经说明 slua 的 `lua-wrapper` 是外置工具。本轮新增的关键点是：从当前 reference 仓库可见范围看，插件源码真正拥有的不是“生成器实现”，而是**生成器启动壳和 README 合同**。`Tools/README.md` 明确写它是 C#/.NET 4.6.2 工具，依赖 `Newtonsoft.Json` 和 `libclang 5.0.0 (32 bit)`；`Plugins/lua_wrapper` 模块则只负责把按钮挂到 `LevelEditor`，并在点击时 `system("../Tools/lua-wrapper.exe")` 或 `mono lua-wrapper.exe`。连 `NomadTab` 内容都还是模板占位文本。这意味着 slua 的 `Source/` 与 `Tools/` 分工并不是“源码内置生成器 + runtime 消费者”，而更像“runtime 消费生成物，Editor 只是二进制 generator 的 launcher”。

Angelscript 在这点上更接近“源码内工具链”。`AngelscriptFunctionTableExporter` 直接作为 `UhtExporter(... CompileOutput ...)` 注册在仓库源码里，运行时读取当前 `factory.Session.Modules`，生成 `AS_FunctionTable_*.cpp`，并额外落出 skipped CSV 与统计摘要。也就是说，Angelscript 的 `Tools`/生成逻辑对当前仓库是**可审计、可 diff、可随 UHT session 一起演化**的；slua 在当前 reference 范围内暴露给读者的则主要是 launcher 边界和配置/依赖契约。

```
[D1/D6] Codegen Ownership
sluaunreal
├─ Tools/README.md                                 // 说明 C# + libclang tool 契约
├─ lua_wrapper Editor module                       // 菜单/按钮/占位 tab
└─ PluginButtonClicked() -> lua-wrapper.exe        // 生成逻辑在外部工具边界

Angelscript
├─ AngelscriptFunctionTableExporter.cs             // UHT exporter 源码就在仓库内
├─ factory.Session.Modules                         // 直接消费当前 UHT session
├─ Generate(factory)                               // 输出 CompileOutput
└─ WriteSkipped*.csv + summary                     // 结果链可审计
```

[1] 关键源码：slua 插件源码拥有的是 tool contract 与 launcher，不是仓库内生成实现

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Tools/README.md
// 文件: Reference/sluaunreal/Plugins/lua_wrapper/Source/lua_wrapper/Private/lua_wrapper.cpp
// 函数: 文档说明 / Flua_wrapperModule::StartupModule / OnSpawnPluginTab / PluginButtonClicked
// 位置: README.md:1-5, 9-19, 35-42; lua_wrapper.cpp:26-60, 74-86, 122-135
// 位置说明: 当前 reference 可见范围里，slua 暴露的是外部工具契约和编辑器启动壳
// ============================================================================
1:## lua-wrapper 是什么？
3:lua-wrapper 是 slua-unreal 的静态代码导出工具，... 采用 c# 开发，.Net framework 是 4.6.2，依赖两个库，Newtonsoft.Json 11.0.2 和 libclang 5.0.0（32位）...
9:lua-wrapper 是作为 slua-unreal 中 lua 导出接口的补充，slua-unreal 支持 3 种接口导出的方式：
17:3. 导出类型限定于引擎中的 USTRUCT 类型
18:4. 优先使用反射或者 LuaCppBinding 导出类型，最后才考虑使用 lua-wrapper
35:... 请修改 Tools 目录下的 config*.json 文件 ...
41:Newtonsoft.Json 11.0.02 (.net framework 4.6.2)
42:libclang 5.0.0 (32 bit)
// ★ README 公开的是工具依赖、适用范围和配置约束，而不是生成器内部实现

26:void Flua_wrapperModule::StartupModule()
37:	PluginCommands->MapAction(
42:	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
58:	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(lua_wrapperTabName, ...)
59:		.SetDisplayName(LOCTEXT("Flua_wrapperTabTitle", "lua_wrapper"))
// ★ `Source/lua_wrapper` 主要做的是 LevelEditor 菜单/按钮接入

74:TSharedRef<SDockTab> Flua_wrapperModule::OnSpawnPluginTab(...)
77:		LOCTEXT("WindowWidgetText", "Add code to {0} in {1} to override this window's contents"),
85:			// Put your tab content here!
// ★ 连 tab 内容都还是模板占位，说明这个模块不是生成器主体

122:void Flua_wrapperModule::PluginButtonClicked()
127:	auto cmd = contentDir + TEXT("/../Tools/lua-wrapper.exe");
128:	system(TCHAR_TO_UTF8(*cmd));
133:    auto ret = exec("/Library/Frameworks/Mono.framework/Versions/Current/Commands/mono lua-wrapper.exe");
// ★ 真正生成逻辑被收口到外部 `lua-wrapper.exe` / `mono lua-wrapper.exe`
```

[2] 对照源码：Angelscript 的 function-table exporter 直接是仓库内 UHT 工具源码

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs
// 函数: Export / IsBlueprintCallable / CountBlueprintCallableFunctions
// 位置: 21-53, 56-96
// 位置说明: Angelscript 的生成器逻辑在仓库源码内直接注册为 UHT exporter
// ============================================================================
21:	[UhtExporter(
22:		Name = "AngelscriptFunctionTable",
23:		Description = "Exports Angelscript function table data",
24:		Options = UhtExporterOptions.Default | UhtExporterOptions.CompileOutput,
25:		CppFilters = ["AS_FunctionTable_*.cpp"],
26:		ModuleName = "AngelscriptRuntime")]
27:	private static void Export(IUhtExportFactory factory)
35:		int generatedFileCount = AngelscriptFunctionTableCodeGenerator.Generate(factory);
37:		foreach (UhtModule module in factory.Session.Modules)
40:			CountBlueprintCallableFunctions(module.ShortName, module.ScriptPackage, skippedEntries, ref classCount, ref functionCount, ref reconstructedCount, ref skippedCount);
43:		WriteSkippedEntriesCsv(factory, skippedEntries);
44:		WriteSkippedReasonSummaryCsv(factory, skippedEntries);
46:		Console.WriteLine("AngelscriptUHTTool exporter visited {0} packages, ... wrote {5} module files.", ...);
// ★ exporter 自己就是源码级 UHT 节点，直接挂进当前编译会话并输出审计摘要

56:	internal static bool IsBlueprintCallable(UhtFunction function)
60:		return function.FunctionType == UhtFunctionType.Function &&
65:	private static void CountBlueprintCallableFunctions(string moduleName, UhtType type, List<AngelscriptSkippedFunctionEntry> skippedEntries, ...)
67:		if (type is UhtClass classObj)
72:				if (child is UhtFunction function && IsBlueprintCallable(function))
75:					if (AngelscriptFunctionSignatureBuilder.TryBuild(classObj, function, out AngelscriptFunctionSignature? signature, out string? failureReason))
82:						skippedCount++;
83:						skippedEntries.Add(new AngelscriptSkippedFunctionEntry(...));
// ★ 哪些函数被生成、哪些被跳过、为什么跳过，都在源码内有明确统计链
```

补充判断：

- `Source/ 与 Tools/ 的责任边界`：这里是 `实现方式不同`。slua 的 `Source/lua_wrapper` 更像外部 generator 的 Editor launcher；Angelscript 的 exporter 直接属于仓库内编译工具链。
- `生成器可审计性`：这里有 `实现质量差异`。在当前 reference 可见范围内，slua 读得到的是 README + launcher + config 合同；Angelscript 读得到的是完整 exporter 源码和 skipped summary 输出链。
- `生成器是否“没有实现”`：不能这么判。slua 明显 `有实现`，只是 `实现边界在外部二进制工具`，不在当前插件源码主干里。
