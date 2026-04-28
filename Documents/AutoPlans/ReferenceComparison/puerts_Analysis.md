# puerts 源码分析

> **分析对象**: puerts (Tencent) UE 插件部分
> **源码路径**: `Reference/puerts/unreal/Puerts/`
> **对比基准**: `Plugins/Angelscript/`
> **分析日期**: 2026-04-08

本分析只覆盖 `Reference/puerts/unreal/Puerts/` 这一层的 UE 插件实现，不扩展到仓库其余平台或外部文档。puerts 的核心特征是多后端脚本虚拟机抽象、TypeScript 声明生成、以及基于 Inspector 的模块级热更；与之相比，Angelscript 更像是把单一脚本引擎深嵌进 UE 运行时，并把编辑器工具、测试、StaticJIT 与预编译数据一起做成一套内聚方案。

## 插件架构总览

```
[puerts] Plugin Architecture
├─ WasmCore                               // wasm 能力与静态绑定样例
├─ JsEnv                                  // JS VM 抽象层
│  ├─ V8                                  // 默认高性能后端
│  ├─ QuickJS                             // 轻量后端
│  └─ Node.js                             // Node runtime
├─ Puerts                                 // UE 生命周期、设置、自动启动
├─ DeclarationGenerator                   // ue.d.ts / ue_bp.d.ts / cpp.d.ts
├─ PuertsEditor                           // TS LanguageService / Blueprint asset
└─ ParamDefaultValueMetas                 // 程序侧元数据提取

[Angelscript] Plugin Architecture
├─ AngelscriptRuntime                     // 单引擎嵌入、绑定、JIT、预编译
├─ AngelscriptEditor                      // 编辑器菜单、热重载、资产工作流
└─ AngelscriptTest                        // 独立测试模块
```

从模块切法看，puerts 把“脚本 VM 抽象”“声明生成”“编辑器分析器”拆成多个显式模块，便于替换后端和单独发布工具链；Angelscript 则把大部分脚本能力压缩在 `AngelscriptRuntime` 内，再由 `AngelscriptEditor` 和 `AngelscriptTest` 向外分层。这不是谁“有没有模块化”的问题，而是“后端解耦优先”与“运行时内聚优先”的工程取舍不同。

---

## 深化分析 (2026-04-08 18:03:00)

## [维度 D1] 插件架构与模块划分

puerts 在插件描述层就显式声明了 6 个模块，其中 `JsEnv` 负责脚本虚拟机封装，`DeclarationGenerator` 与 `PuertsEditor` 负责工具链，`Puerts` 自己更多承担 UE 生命周期与配置读入职责。`JsEnv.Build.cs` 又把 Node.js / QuickJS / V8 的切换逻辑集中到一个模块规则里，说明它的“多后端”不是多个平行插件，而是一个统一 API 下的多实现选择。

Angelscript 的模块数量更少，但 `AngelscriptRuntime.Build.cs` 直接把 `ThirdParty/angelscript/source` 编进运行时，同时把大量绑定依赖也挂在 Runtime 模块上。这意味着 Angelscript 的扩展点更集中，切换脚本后端的自由度更低，但也减少了运行时跨模块边界。

```
[puerts] Build-Time Boundary
Puerts.uplugin
├─ JsEnv -> backend switch in JsEnv.Build.cs
├─ Puerts -> settings / startup / auto mode
├─ DeclarationGenerator -> editor-only type generation
└─ PuertsEditor -> JS analyzer hosted inside UE

[Angelscript] Build-Time Boundary
Angelscript.uplugin
├─ AngelscriptRuntime -> embeds AngelScript directly
├─ AngelscriptEditor -> editor tools
└─ AngelscriptTest -> test-only surface
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Puerts.uplugin:15-48`

```jsonc
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Puerts.uplugin
// 位置: 15-48，插件模块清单
// ============================================================================
"Modules": [
  {
    "Name": "WasmCore",
    "Type": "Runtime"
  },
  {
    "Name": "JsEnv",
    "Type": "Runtime"
  },
  {
    "Name": "DeclarationGenerator",
    "Type": "Editor" // ★ 声明生成被拆成独立编辑器模块
  },
  {
    "Name": "Puerts",
    "Type": "Runtime"
  },
  {
    "Name": "PuertsEditor",
    "Type": "Editor" // ★ 编辑器分析器再拆一层
  }
]
```

[2] `Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs:149-166,360-367,502-523,627-663`

```csharp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs
// 位置: 149-166, 360-367, 502-523, 627-663，多后端选择与运行时依赖打包
// ============================================================================
if (UseNodejs)
{
    ThirdPartyNodejs(Target);            // ★ Node.js 后端
}
else if (UseQuickjs)
{
    ForceStaticLibInEditor = true;
    ThirdPartyQJS(Target);               // ★ QuickJS 后端
}
else if (UseV8Version > SupportedV8Versions.VDeprecated)
{
    ThirdParty(Target);                  // ★ V8 后端
}

var DestDllPath = Path.Combine("$(BinaryOutputDir)", DllName);
RuntimeDependencies.Add(DestDllPath, DllPath, StagedFileType.NonUFS);

PrivateDefinitions.Add("WITH_NODEJS");
RuntimeDependencies.Add("$(TargetOutputDir)/libnode.dll", Path.Combine(V8LibraryPath, "libnode.dll"));

PrivateDefinitions.Add("WITH_QUICKJS");
AddRuntimeDependencies(new string[] { "msys-quickjs.dll" }, V8LibraryPath, false);
```

[3] `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs:20-22,29-79`

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs
// 位置: 20-22, 29-79，单引擎嵌入与运行时依赖集中在 Runtime 模块
// ============================================================================
var AngelscriptThirdPartyPath = Path.Combine(ModuleDirectory, "ThirdParty", "angelscript");
PublicIncludePaths.Add(Path.Combine(AngelscriptThirdPartyPath, "source")); // ★ 直接嵌入 AngelScript 源码

PublicDependencyModuleNames.AddRange(new string[]
{
    "Core", "CoreUObject", "Engine", "Json", "GameplayTags"
});

PrivateDependencyModuleNames.AddRange(new string[]
{
    "AIModule", "Networking", "Sockets", "UMG", "GameplayAbilities"
});
```

设计取舍：

- puerts 通过模块拆分把“脚本后端”“工具链”“宿主生命周期”分开，适合多后端与多平台组合发布，但 Build.cs 复杂度明显更高。
- Angelscript 把第三方引擎和大部分绑定能力压在 Runtime 内，依赖路径直，跨模块切换少，但后端可替换性基本没有暴露出来。

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 脚本后端组织 | `JsEnv.Build.cs` 在同一 API 下切 V8 / QuickJS / Node.js | `AngelscriptRuntime.Build.cs` 直接嵌入 AngelScript 源码 | 实现方式不同 |
| 工具链模块化 | `DeclarationGenerator`、`PuertsEditor` 单独成模块 | 编辑器能力集中在 `AngelscriptEditor` | 实现方式不同 |
| 测试模块暴露 | 插件模块清单中未见专门 Test module | `Angelscript.uplugin` 明确有 `AngelscriptTest` | puerts 在插件层没有实现同等级独立测试模块 |

## [维度 D2] 反射绑定机制

puerts 的绑定入口是模板式 `DefineClass<T>()`，后续由 `pesapi_define_class(...)` 把类型、构造、属性与父类关系提交给 backend。真正的类型落地发生在运行时：`JSClassRegister` 负责按 `TypeId` 延迟加载类定义，`CppObjectMapper` 再把 `JSClassDefinition` 变成 V8 `FunctionTemplate`，并把继承关系、`FastCallInfo` 等附着进去。这个设计让“类型描述”与“后端具体注册”解耦。

Angelscript 的主干则是 `FAngelscriptBinds` 对 `asIScriptEngine` 的直接注册。它维护 `ClassFuncMaps`，在大量 `Bind_*.cpp` 手写文件里调用 `RegisterObjectType`、`RegisterObjectMethod`、`BindGlobalFunction` 等 API；UHT 工具 `AngelscriptFunctionTableCodeGenerator.cs` 只负责生成 `AddFunctionEntry(...)` 的函数表补充，并不生成完整绑定层。遇到 BlueprintCallable 场景时，又通过 `BlueprintCallableReflectiveFallback.cpp` 做运行时兜底。

```
[puerts] Reflection Binding Flow
├─ [1] DefineClass<T>()                  // 统一类型描述 DSL
├─ [2] pesapi_define_class(...)          // backend 注册
├─ [3] JSClassRegister::LoadClassByID    // 按需加载
└─ [4] CppObjectMapper builds template    // 继承链 / FastCall / 方法挂接

[Angelscript] Reflection Binding Flow
├─ [1] Bind_*.cpp static registration    // 手写绑定入口
├─ [2] AddFunctionEntry(...)             // UHT 只补函数表
├─ [3] RegisterObjectType/Method         // 直接注册进 AngelScript engine
└─ [4] ReflectiveFallback                // BlueprintCallable 运行时兜底
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/ScriptBackend.hpp:155-168` 与 `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/PesapiBackend.hpp:203-215`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Public/ScriptBackend.hpp
// 函数: DefineClass<T>()
// 位置: 155-168，绑定 DSL 的统一入口
// ============================================================================
template <typename T>
ClassDefineBuilder<T, PUERTS_BINDING_IMPL::API, PUERTS_BINDING_IMPL::API> DefineClass()
{
    static auto NameLiteral = ScriptTypeName<T>::value();
    // ★ 先拿类型名，再把后续方法/属性定义交给 builder
    return ClassDefineBuilder<T, PUERTS_BINDING_IMPL::API, PUERTS_BINDING_IMPL::API>(NameLiteral.Data());
}

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Public/PesapiBackend.hpp
// 位置: 203-215，backend 最终注册点
// ============================================================================
for (const auto& prop : Cdb.variables_)
{
    pesapi_set_property_info(properties, pos++, prop.Name, true, prop.Getter, prop.Setter, nullptr, nullptr, nullptr);
}

// ★ 把 TypeId、父类、构造函数、属性表一次性交给 backend
pesapi_define_class(StaticTypeId<T>::get(), Cdb.superTypeId_, Cdb.className_,
    reinterpret_cast<InitializeFuncType>(Cdb.constructor_), finalize, properties_count, properties, nullptr);
```

[2] `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/CppObjectMapper.cpp:49-57,208-246,263-268`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/CppObjectMapper.cpp
// 位置: 49-57, 208-246, 263-268，运行时把类定义变成具体 JS 模板
// ============================================================================
auto ClassDef = puerts::LoadClassByID(TypeId);
if (!ClassDef)
{
    return v8::MaybeLocal<v8::Function>();
}

auto FastCallInfo = FunctionInfo->ReflectionInfo ? FunctionInfo->ReflectionInfo->FastCallInfo() : nullptr;
if (FastCallInfo)
{
    // ★ V8 路径可直接挂 FastCallInfo，减少桥接开销
    Template->PrototypeTemplate()->Set(
        v8::String::NewFromUtf8(Isolate, FunctionInfo->Name, v8::NewStringType::kNormal).ToLocalChecked(),
        v8::FunctionTemplate::New(Isolate, (v8::FunctionCallback) FunctionInfo->Callback,
            v8::External::New(Isolate, &FunctionInfo->Data), v8::Local<v8::Signature>(), 0,
            v8::ConstructorBehavior::kThrow, v8::SideEffectType::kHasSideEffect, FastCallInfo));
}

if (auto SuperDefinition = LoadClassByID(ClassDefinition->SuperTypeId))
{
    Template->Inherit(GetTemplateOfClass(Isolate, SuperDefinition)); // ★ 继承链在模板层拼接
}
```

[3] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:260-289,456-460,588-608`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp
// 位置: 260-289, 456-460, 588-608，直接面向 asIScriptEngine 注册
// ============================================================================
int TypeId = Manager.Engine->RegisterObjectType(ClassName.ToCString(), Size, Flags); // ★ 直接注册脚本类型

int FunctionId = Manager.Engine->RegisterObjectMethod(
    ClassName.ToCString(), Signature.ToCString(), Function, CallConv, *(asFunctionCaller*)&Caller);

int FunctionId = Manager.Engine->RegisterGlobalFunction(
    Signature.ToCString(), Function, asCALL_CDECL, *(asFunctionCaller*)&Caller, nullptr);
```

设计取舍：

- puerts 把“类型描述”做成后端无关 DSL，后端替换成本低，适合多 VM 共用同一套绑定描述。
- Angelscript 直接贴近 `asIScriptEngine`，调用链短，但维护成本落在大量 `Bind_*.cpp` 与注册顺序管理上。
- Angelscript 并非没有自动化，只是自动化集中在“函数表补充”和“反射兜底”，没有形成 puerts 这种统一声明 DSL。

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 类型定义入口 | `DefineClass<T>()` + `pesapi_define_class(...)` | `FAngelscriptBinds` + `Bind_*.cpp` | 实现方式不同 |
| 类加载时机 | `LoadClassByID()` 按 `TypeId` 延迟装配 | 多数绑定在启动期静态注册 | 实现方式不同 |
| UHT 参与度 | 绑定核心不依赖 UHT 函数表 | UHT 仅补 `AddFunctionEntry(...)` | Angelscript 没有实现同等级统一绑定 DSL |
| BlueprintCallable 兜底 | 主要仍走绑定描述 + runtime mapper | `BlueprintCallableReflectiveFallback.cpp` 单独兜底 | 实现方式不同 |

## [维度 D3] Blueprint 交互

puerts 通过 `UTypeScriptGeneratedClass` 把需要由 TS 实现的 `UFunction` 标记为 `FUNC_BlueprintCallable | FUNC_BlueprintEvent`，再把 `NativeFunc` 重定向到 `execCallJS` 或惰性装载的 `execLazyLoadCallJS`。这说明它的 Blueprint 交互核心不是“在 Blueprint 图里嵌 JS 节点”，而是把 TS 生成类伪装成 UE 可调用的 `UClass/UFunction`。

Angelscript 则把“脚本类作为 Blueprint 父类”做成显式编辑器工作流。`FAngelscriptEditorModule::ShowCreateBlueprintPopup` 会为 `UASClass` 调起保存对话框，再调用 `FKismetEditorUtilities::CreateBlueprint(...)` 真正创建资产。两者都支持混合继承链，但 puerts 更像“代码分析器驱动的类重定向”，Angelscript 更像“资产工作流驱动的 Blueprint 子类化”。

```
[puerts] Blueprint Interop
TypeScript class
 -> UTypeScriptGeneratedClass
 -> mark UFunction as BlueprintCallable/Event
 -> execCallJS / execLazyLoadCallJS
 -> Blueprint calls into JS

[Angelscript] Blueprint Interop
Angelscript class
 -> UASClass
 -> CreateBlueprint popup
 -> Blueprint child asset
 -> Blueprint instances call generated script class
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/TypeScriptGeneratedClass.cpp:57-118,221-229`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/TypeScriptGeneratedClass.cpp
// 位置: 57-118, 221-229，把 UFunction 重定向到 TS 实现
// ============================================================================
if (PinedDynamicInvoker)
{
    PinedDynamicInvoker->NotifyReBind(Class); // ★ 首次调用前可触发重绑
}

Function->FunctionFlags |= FUNC_BlueprintCallable | FUNC_BlueprintEvent | FUNC_Public | FUNC_Native;
Function->SetNativeFunc(&UTypeScriptGeneratedClass::execLazyLoadCallJS);
AddNativeFunction(*Function->GetName(), &UTypeScriptGeneratedClass::execLazyLoadCallJS);

InFunction->FunctionFlags |= FUNC_BlueprintCallable | FUNC_BlueprintEvent | FUNC_Public;
InFunction->SetNativeFunc(&UTypeScriptGeneratedClass::execCallJS); // ★ 正式调用直接进 JS
AddNativeFunction(*InFunction->GetName(), &UTypeScriptGeneratedClass::execCallJS);
```

[2] `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:418-517`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 位置: 418-517，从脚本类创建 Blueprint 资产
// ============================================================================
if (bIsDataAsset)
{
    Asset = NewObject<UDataAsset>(Package, Class, AssetName, RF_Public | RF_Transactional | RF_Standalone);
}
else
{
    IKismetCompilerInterface& KismetCompilerModule = FModuleManager::LoadModuleChecked<IKismetCompilerInterface>("KismetCompiler");
    KismetCompilerModule.GetBlueprintTypesForClass(Class, BlueprintClass, BlueprintGeneratedClass);

    // ★ 直接以脚本生成类为父类创建 Blueprint
    Asset = FKismetEditorUtilities::CreateBlueprint(
        Class, Package, AssetName, BPTYPE_Normal,
        BlueprintClass, BlueprintGeneratedClass, FName("AngelscriptCreateBlueprint")
    );
}
```

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| Blueprint 调脚本函数 | `UFunction` 重定向到 `execCallJS` | 通过脚本生成类与 Blueprint 子类关系执行 | 实现方式不同 |
| Blueprint 子类创建入口 | `CodeAnalyze.ts` 可用 `PEBlueprintAsset` 自动生成 | `ShowCreateBlueprintPopup` 显式创建资产 | 实现方式不同 |
| 继承链暴露方式 | TS 生成类伪装成 `UTypeScriptGeneratedClass` | `UASClass` 直接进入 Kismet 创建流程 | 实现方式不同 |
| 资产工作流显式性 | 更多依赖分析器与生成 | 编辑器入口更直接、可见 | Angelscript 在编辑器工作流上实现质量更完整 |

## [维度 D4] 热重载

puerts 的热重载路径是“文件监听 + Inspector 改脚本源码 + TS 生成类重绑”。`FSourceFileWatcher` 只盯 `.js` 文件，先用 MD5 去抖；`hot_reload.js` 则借助 `Debugger.getScriptSource` / `Debugger.setScriptSource` 做模块级 HMR，并在改源码前后触发 `HMR.prepare` / `HMR.finish`。如果类来自 `UTypeScriptGeneratedClass`，`RebindJs()` 与 `NotifyReBind()` 会负责把对象重定向到新实现。

Angelscript 的热重载粒度更偏“脚本文件/模块编译管线”。目录监听器会把 `.as` 变更、目录新增删除都排队，`FAngelscriptClassGenerator` 先判断是 `SoftReload`、`FullReloadSuggested` 还是 `FullReloadRequired`，`FAngelscriptEngine` 再决定是否立即软重载、升级为全量重载，或者保留旧代码并记录待重试文件。这个失败恢复链明显比 puerts 更重。

```
[puerts] HMR
.js file change
 -> MD5 watcher
 -> Debugger.setScriptSource
 -> HMR.prepare / HMR.finish
 -> RebindJs / NotifyReBind for TS classes

[Angelscript] Reload Pipeline
.as file change
 -> queue changed / deleted files
 -> classify SoftReload / FullReloadRequired
 -> swap modules or keep old code
 -> queue retry / full reload later
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/SourceFileWatcher.cpp:22-80`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/SourceFileWatcher.cpp
// 位置: 22-80，监听 .js 变更并用 MD5 去抖
// ============================================================================
DirectoryWatcher->RegisterDirectoryChangedCallback_Handle(
    Dir,
    IDirectoryWatcher::FDirectoryChanged::CreateRaw(this, &FSourceFileWatcher::OnDirectoryChanged),
    DelegateHandle,
    IDirectoryWatcher::IgnoreChangesInSubtree);

if (Change.Action == FFileChangeData::FCA_Modified && Change.Filename.EndsWith(TEXT(".js")))
{
    FMD5Hash Hash = FMD5Hash::HashFile(*NotifyPath);
    if (WatchedFiles[Dir][FileName] != Hash)
    {
        OnWatchedFileChanged(NotifyPath); // ★ 只有内容真的变化才触发 HMR
        WatchedFiles[Dir][FileName] = Hash;
    }
}
```

[2] `Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js:53-90`

```javascript
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js
// 位置: 53-90，通过 Inspector 协议直接替换脚本源码
// ============================================================================
async function enableDebugger() {
    if (inited) return;
    inited = true;
    setInspectorCallback(messageHandler);
    await sendCommand("Runtime.enable", {});
    await sendCommand("Debugger.enable", {"maxScriptsCacheSize":10000000});
}

async function reload(moduleName, url, source) {
    await enableDebugger();
    let orgSourceInfo = await sendCommand("Debugger.getScriptSource", {scriptId:"" + scriptId});
    source = ("(function (exports, require, module, __filename, __dirname) { " + source + "\n});");
    if (orgSourceInfo.scriptSource == source) {
        return;
    }
    puerts.emit('HMR.prepare', moduleName, m, url);
    let res = await sendCommand("Debugger.setScriptSource", {scriptId:"" + scriptId,scriptSource:source});
    puerts.emit('HMR.finish', moduleName, m, url); // ★ 真正的模块级 HMR
}
```

[3] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3951-3997,4168-4187`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 3951-3997, 4168-4187，热重载分级决策与失败恢复
// ============================================================================
switch (ClassGenerator.Setup())
{
    case FAngelscriptClassGenerator::EReloadRequirement::SoftReload:
        SwapInModules(CompiledModules, DiscardedModules);
        ClassGenerator.PerformSoftReload(); // ★ 结构未变时走软重载
        break;

    case FAngelscriptClassGenerator::EReloadRequirement::FullReloadRequired:
        if (CompileType == ECompileType::SoftReloadOnly)
        {
            bShouldSwapInModules = false;
            bFullReloadRequired = true;    // ★ 当前时机不允许全量重载，则保留旧代码
        }
        else
        {
            SwapInModules(CompiledModules, DiscardedModules);
            ClassGenerator.PerformFullReload();
        }
        break;
}

QueuedFullReloadFiles.Add(RepeatFile);      // ★ 失败文件排队，下轮自动重试
PreviouslyFailedReloadFiles.Append(AllCompiledFiles);
```

设计取舍：

- puerts 选择“脚本模块级就地换源码”，对行为层改动响应快，但结构性变更最终还是要靠重绑类对象来兜底。
- Angelscript 选择“先分析结构风险再决定软/全量重载”，复杂但对 UObject 布局变化更谨慎，也更明确地保留旧版本作为失败回退。
- `JsEnv.Build.cs:104-107` 把 C++ 模块层的 `bCanHotReload` 设为 `false`，说明 puerts 的热更重点是脚本 HMR，不是 UE C++ 模块热替换。

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 变更检测 | 监听 `.js`，MD5 去抖 | 监听 `.as` 与目录变化，支持删除/新增目录扩散 | Angelscript 实现质量更完整 |
| 重载粒度 | `Debugger.setScriptSource` 做模块级 HMR | Soft / Full reload 分级 | 实现方式不同 |
| 状态保持 | `NotifyReBind` 重绑 TS 生成类与对象 | 保留旧模块、排队全量重载、失败文件自动重试 | Angelscript 实现质量更完整 |
| 失败恢复 | 主要依赖 Inspector 返回与类重绑 | 明确有 `QueuedFullReloadFiles` / `PreviouslyFailedReloadFiles` | puerts 没有实现同等级失败恢复队列 |

## [维度 D5] 调试与开发体验

puerts 的调试方案是 V8 Inspector。`JsEnvImpl` 把 `__tgjsSetInspectorCallback` 与 `__tgjsDispatchProtocolMessage` 绑进 JS 全局对象，`V8InspectorImpl.cpp` 自己起 WebSocket/HTTP 服务并输出 Chrome DevTools URL，因此调试前端基本复用浏览器生态。再配合 `UPuertsSetting` 的 `DebugEnable`、`DebugPort`、`WaitDebugger`，开发者可以直接用现成 Inspector 客户端接入。

Angelscript 走的是自定义调试协议。`AngelscriptDebugServer.h` 定义了成套 `EDebugMessageType`、版本消息、断点、调用栈、`GoToDefinition` 等消息，并在 `AngelscriptDebugProtocolTests.cpp` 用自动化测试验证序列化回环。它复用性不如 Inspector，但 UE 语义更强，协议也有仓内测试保证。

```
[puerts] Debugging
UE -> V8Inspector server -> websocket / HTTP
 -> Chrome DevTools style protocol
 -> JS callback bridge in hot_reload.js

[Angelscript] Debugging
UE -> FAngelscriptDebugServer
 -> custom binary messages
 -> call stack / breakpoints / go-to-definition
 -> protocol round-trip tests
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:526-530,619,4522-4581`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp
// 位置: 526-530, 619, 4522-4581，把 Inspector 协议桥接进 JS 运行时
// ============================================================================
MethodBindingHelper<&FJsEnvImpl::SetInspectorCallback>::Bind(Isolate, Context, Global, "__tgjsSetInspectorCallback", This);
MethodBindingHelper<&FJsEnvImpl::DispatchProtocolMessage>::Bind(Isolate, Context, Global, "__tgjsDispatchProtocolMessage", This);

Inspector = CreateV8Inspector(InDebugPort, &Context); // ★ UE 内直接启动 Inspector

if (!InspectorChannel)
{
    InspectorChannel = Inspector->CreateV8InspectorChannel();
    InspectorChannel->OnMessage([this](std::string Message)
    {
        auto Handler = InspectorMessageHandler.Get(MainIsolate);
        v8::Local<v8::Value> Args[] = {FV8Utils::ToV8String(MainIsolate, Message.c_str())};
        __USE(Handler->Call(ContextInner, ContextInner->Global(), 1, Args)); // ★ 协议消息回注到 JS
    });
}

InspectorChannel->DispatchProtocolMessage(TCHAR_TO_UTF8(*Message)); // ★ JS 侧也能主动发 Inspector 消息
```

[2] `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h:25-50,581-691`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h
// 位置: 25-50, 581-691，自定义调试协议与服务器能力
// ============================================================================
enum class EDebugMessageType : uint8
{
    Diagnostics,
    RequestDebugDatabase,
    StartDebugging,
    RequestCallStack,
    SetBreakpoint,
    StepOver,
    StepIn,
    StepOut,
    EngineBreak
};

class FAngelscriptDebugServer
{
public:
    void ProcessScriptLine(class asCContext* Context);
    void ProcessException(class asIScriptContext* Context);
    void GoToDefinition(const FAngelscriptGoToDefinition GoTo); // ★ 协议直接带 UE 语义
    void BroadcastDebugDatabase();
};
```

[3] `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp:25-35,79-101`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp
// 位置: 25-35, 79-101，协议消息有自动化 round-trip 校验
// ============================================================================
template <typename T>
T RoundTripMessage(T Message)
{
    TArray<uint8> Buffer;
    FMemoryWriter Writer(Buffer);
    Writer << Message;      // ★ 先序列化

    T Result;
    FMemoryReader Reader(Buffer);
    Reader << Result;       // ★ 再反序列化，验证协议兼容性
    return Result;
}
```

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 调试协议 | 复用 V8 Inspector | 自定义二进制消息协议 | 实现方式不同 |
| 前端生态 | 可直接接 Chrome DevTools | 需要配套适配器/客户端 | 实现方式不同 |
| UE 语义扩展 | 主要借助 JS/Inspector 能力 | `GoToDefinition`、DebugDatabase、Blueprint 创建等都在协议内 | Angelscript 在 UE 定制语义上实现质量更高 |
| 协议测试 | 当前插件子树未见同等级协议自动化测试 | `AngelscriptDebugProtocolTests.cpp` 明确覆盖 round-trip | puerts 在插件层没有实现同等级协议测试 |

## [维度 D6] 代码生成与 IDE 支持

这是 puerts 相对 Angelscript 最显著的强项之一。`DeclarationGenerator.cpp` 会遍历 `UClass/UBlueprint/UUserDefinedEnum/UUserDefinedStruct`，生成 `Typing/ue/ue.d.ts` 与 `Typing/ue/ue_bp.d.ts`，`TemplateBindingGenerator.cpp` 再生成 `Typing/cpp/index.d.ts`。`PuertsEditor/tsconfig.json` 把 `../../../Typing` 设为 `typeRoots`，`CodeAnalyze.ts` 通过自定义 `ts.System` 和 `ts.createLanguageService(...)` 把 UE 文件系统直接挂进 TypeScript 语言服务。也就是说，puerts 的 IDE 支持不是“附加脚本”，而是完整的类型声明生产线。

Angelscript 在 IDE 侧也不是空白，但路线不同。`AngelscriptSourceCodeNavigation.cpp` 只解决“从 UE 反射对象跳到脚本源文件”的导航问题，配合调试服务器可做更多 IDE 协同；然而在当前源码里，没有看到类似 `ue.d.ts` 这种面向编辑器的完整类型声明输出物。因此在“类型安全 + 智能提示”这个维度，puerts 的投入明显更深。

```
[puerts] IDE Pipeline
UClass / Blueprint scan
 -> GenTypeScriptDeclaration()
 -> Typing/ue/ue.d.ts + ue_bp.d.ts
 -> Typing/cpp/index.d.ts
 -> tsconfig typeRoots
 -> TypeScript LanguageService

[Angelscript] IDE Pipeline
UE reflection object
 -> SourceCodeNavigation handler
 -> open source file in editor
 -> debug server / debug database assists
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/PuertsEditor/CodeAnalyze.ts:8-24,343-353`

```ts
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/PuertsEditor/CodeAnalyze.ts
// 位置: 8-24, 343-353，把 UE 文件系统挂进 TypeScript LanguageService
// ============================================================================
const customSystem: ts.System = {
    args: [],
    readFile,
    writeFile,
    resolvePath: tsi.resolvePath,
    fileExists,
    directoryExists: tsi.directoryExists,
    createDirectory: tsi.createDirectory,
    getExecutingFilePath,
    getCurrentDirectory,
    getDirectories,
    readDirectory,
    exit,
}

let service = ts.createLanguageService(servicesHost, ts.createDocumentRegistry()); // ★ 直接在 UE 里跑 TS 语言服务

function getProgramFromService() {
    while(true) {
        try {
            return service.getProgram();
        } catch (e) {
            service = ts.createLanguageService(servicesHost, ts.createDocumentRegistry()); // ★ 失败后重建服务
        }
    }
}
```

[2] `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp:6-139`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp
// 位置: 6-139，IDE 侧主要提供源码跳转
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
        OpenFile(Path, ASFunc->GetSourceLineNumber()); // ★ 用反射对象定位脚本源码
        return true;
    }
};
```

设计取舍：

- puerts 通过 `.d.ts` 把 UE API 投射进 TypeScript 类型系统，IDE 能力直接复用 TS 生态。
- Angelscript 选择“源码导航 + 调试数据库”这类宿主集成，而不是维护另一套声明语言。
- 这不是 Angelscript 没有 IDE 支持，而是它没有实现 puerts 这种“声明文件作为第一产物”的策略。

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 类型声明产物 | `Typing/ue/ue.d.ts`、`ue_bp.d.ts`、`Typing/cpp/index.d.ts` | 当前源码未见同等级声明文件生成链 | Angelscript 没有实现 |
| IDE 引擎 | `ts.createLanguageService(...)` | `ISourceCodeNavigationHandler` + debug server | 实现方式不同 |
| 类型安全提示 | 直接走 TypeScript 类型系统 | 主要依赖脚本编译器与导航能力 | puerts 实现质量更完整 |
| Blueprint API 暴露到 IDE | `ue_bp.d.ts` 专门输出 | 当前未见等价 Blueprint 声明产物 | Angelscript 没有实现 |

## [维度 D7] 编辑器集成

puerts 的编辑器集成集中在“生成”和“分析”。`DeclarationGenerator` 在启动时注册菜单/命令与 `Puerts.Gen` 控制台命令；`PuertsEditorModule` 则起一个共享 `FJsEnv`，直接运行 `PuertsEditor/CodeAnalyze`。也就是说，puerts 的编辑器扩展偏向代码工作流。

Angelscript 的编辑器集成更偏资产与诊断工作流。它不仅注册目录监听，还注册 `StateDump` 扩展、Blueprint 创建入口、内容浏览器弹窗等，覆盖面更像一个完整的编辑器子系统。

```
[puerts] Editor Integration
Toolbar / ToolMenus
 -> Puerts.Gen
 -> declaration generation
 -> shared JsEnv starts CodeAnalyze

[Angelscript] Editor Integration
ToolMenus / StateDump
 -> script watcher
 -> create Blueprint popup
 -> content browser asset workflow
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1640-1687`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp
// 位置: 1640-1687，编辑器菜单与控制台命令
// ============================================================================
FGenDTSCommands::Register();
PluginCommands->MapAction(FGenDTSCommands::Get().PluginAction,
    FExecuteAction::CreateRaw(this, &FDeclarationGenerator::GenUeDtsCallback), FCanExecuteAction());

UToolMenus::RegisterStartupCallback(
    FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FDeclarationGenerator::RegisterMenus));

ConsoleCommand = MakeUnique<FAutoConsoleCommand>(TEXT("Puerts.Gen"), TEXT("Execute GenDTS action"),
    FConsoleCommandWithArgsDelegate::CreateLambda([this](const TArray<FString>& Args)
    {
        this->GenUeDts(GenFull, SearchPath); // ★ 编辑器内可直接触发声明生成
    }));
```

[2] `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:405-415,418-517`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 位置: 405-415, 418-517，编辑器工作流直接面向资产操作
// ============================================================================
AngelscriptEditor::Private::RegisterStateDumpExtension(StateDumpExtensionHandle); // ★ 编辑器状态导出挂钩

FAngelscriptRuntimeModule::GetEditorCreateBlueprint().AddLambda(
    [](UASClass* ScriptClass)
    {
        FAngelscriptEditorModule::ShowCreateBlueprintPopup(ScriptClass); // ★ 编辑器显式暴露 Blueprint 创建入口
    }
);

UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FAngelscriptEditorModule::RegisterToolsMenuEntries));
```

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 菜单/命令核心 | 生成声明与分析器启动 | 工具菜单、StateDump、Blueprint 创建 | 实现方式不同 |
| 编辑器运行时 | `PuertsEditorModule` 启动共享 `JsEnv` 跑 `CodeAnalyze` | `AngelscriptEditor` 深度接 UE 内容浏览器与资产工具 | Angelscript 在资产工作流上实现质量更完整 |
| Commandlet / Dump 扩展 | 当前检视范围内未见同等级状态导出体系 | `AngelscriptStateDump` 与编辑器扩展协同 | puerts 在插件内没有实现同等级状态导出 |

## [维度 D8] 性能与优化

puerts 在当前源码里能看到两类性能优化抓手。第一类是 VM 后端层面的选择：V8、QuickJS、Node.js 让项目可以在吞吐、体积、生态之间折中。第二类是桥接层面的 `FastCallInfo`，它只在非 QuickJS 路径下启用，说明 puerts 很清楚瓶颈主要落在 JS/C++ 边界上，而不是单纯的脚本执行。

Angelscript 的优化焦点则完全不同。`AngelscriptEngine` 会在非编辑器、非开发模式下考虑 `bUsePrecompiledData`，并在生成阶段拉起 `FAngelscriptPrecompiledData` 与 `FAngelscriptStaticJIT`。`PrecompiledData.cpp` 甚至直接操作 AngelScript 内部注册表和模块数组，说明它是在“引擎内部态”做序列化和恢复，而不是 VM 外围的桥接加速。

```
[puerts] Perf Focus
JS call
 -> ReflectionInfo::FastCallInfo()
 -> V8 FunctionTemplate fast path
 -> backend choice (V8 / QuickJS / Node.js)

[Angelscript] Perf Focus
Script compile / load
 -> PrecompiledData
 -> StaticJIT
 -> restore engine internal registries
 -> run inside embedded AngelScript engine
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/CppObjectMapper.cpp:208-246`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/CppObjectMapper.cpp
// 位置: 208-246，V8 路径的 FastCall 优化
// ============================================================================
auto FastCallInfo = FunctionInfo->ReflectionInfo ? FunctionInfo->ReflectionInfo->FastCallInfo() : nullptr;
if (FastCallInfo)
{
    Template->PrototypeTemplate()->Set(
        v8::String::NewFromUtf8(Isolate, FunctionInfo->Name, v8::NewStringType::kNormal).ToLocalChecked(),
        v8::FunctionTemplate::New(Isolate, (v8::FunctionCallback) FunctionInfo->Callback,
            v8::External::New(Isolate, &FunctionInfo->Data), v8::Local<v8::Signature>(), 0,
            v8::ConstructorBehavior::kThrow, v8::SideEffectType::kHasSideEffect, FastCallInfo)); // ★ 直接走 FastCallInfo
}
else
{
    Template->PrototypeTemplate()->Set(
        v8::String::NewFromUtf8(Isolate, FunctionInfo->Name, v8::NewStringType::kNormal).ToLocalChecked(),
        v8::FunctionTemplate::New(Isolate, (v8::FunctionCallback) FunctionInfo->Callback,
            v8::External::New(Isolate, &FunctionInfo->Data))); // QuickJS 等路径没有这条优化链
}
```

[2] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1427-1442`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 1427-1442，预编译数据与 StaticJIT 初始化
// ============================================================================
bUsePrecompiledData = !bGeneratePrecompiledData && !RuntimeConfig.bIgnorePrecompiledData
    && !RuntimeConfig.bRunningCommandlet && !WITH_EDITOR && !bScriptDevelopmentMode;

if (bGeneratePrecompiledData)
    PrecompiledData = new FAngelscriptPrecompiledData(Engine);

if (bGeneratePrecompiledData)
{
    StaticJIT = new FAngelscriptStaticJIT();
    StaticJIT->PrecompiledData = PrecompiledData; // ★ JIT 与预编译数据绑定在一起
}
```

设计取舍：

- puerts 的优化目标是降低“跨 VM 桥接”成本，尤其是 V8 路径。
- Angelscript 的优化目标是缩短“编译/加载/恢复”成本，并尽量把脚本状态压缩成可复用数据。
- 因此不能简单说 “V8 JIT 一定更快” 或 “Angelscript 一定更轻”；源码只能明确看出双方优化着力点不同，而非仓内基准结论。

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 主要优化对象 | JS/C++ 调用桥接 | 脚本编译、装载、恢复 | 实现方式不同 |
| JIT 路线 | 依赖 V8/Node.js 后端能力；QuickJS 无 V8 FastCall | `StaticJIT` + `PrecompiledData` 内建 | 实现方式不同 |
| 调用快路径证据 | `FastCallInfo()` -> `FunctionTemplate::New(..., FastCallInfo)` | 当前主证据在预编译/JIT，而非外部 VM 快调 | 实现方式不同 |
| 非 V8 后端优化一致性 | QuickJS 路径不走 `FastCallInfo` | 单引擎内优化路径更统一 | Angelscript 在引擎内一致性上更强 |

## [维度 D9] 测试基础设施

在当前检视的 puerts UE 插件子树里，没有看到像 `AngelscriptTest` 这样的独立测试模块。`Puerts.uplugin` 只声明 Runtime / Editor / Program 模块，而 `Source/JsEnv/Private/PuertsWasm/WasmTestForStaticBinding.cpp` 更像一个编译期静态绑定样例，不是成体系的 UE 自动化测试入口。这意味着至少在该子树范围内，puerts 的测试基础设施没有像 Angelscript 那样直接暴露出来。

Angelscript 则完全相反。它既有 `AngelscriptTest` 模块，也有 Runtime 侧 `Tests/`、Editor 私有测试、以及中文/英文测试指南，测试面覆盖调试协议、热重载、绑定、Blueprint、性能与学习追踪。

```
[puerts] Test Surface
Puerts.uplugin
 -> Runtime / Editor / Program modules
 -> no dedicated Test module
 -> ad-hoc wasm static binding sample

[Angelscript] Test Surface
Angelscript.uplugin
 -> AngelscriptTest module
 -> Runtime Tests/
 -> Editor private tests
 -> TESTING_GUIDE.md / TESTING_GUIDE_ZH.md
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PuertsWasm/WasmTestForStaticBinding.cpp:9-28`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PuertsWasm/WasmTestForStaticBinding.cpp
// 位置: 9-28，当前插件子树里少见的 test-like 源文件
// ============================================================================
#if USE_WASM3
float atan2_ue_bind(float X, float Y)
{
    return UKismetMathLibrary::Atan2(X, Y);
}

WASM_BEGIN_LINK_GLOBAL(TestMath, 0)
WASM_LINK_GLOBAL(atan2_ue_bind) // ★ 更像静态绑定样例，而非 UE 自动化测试
WASM_END_LINK_GLOBAL(TestMath, 0)
#endif
```

[2] `Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs:6-49`

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs
// 位置: 6-49，独立测试模块
// ============================================================================
public class AngelscriptTest : ModuleRules
{
    PublicDependencyModuleNames.AddRange(new string[]
    {
        "Core", "CoreUObject", "Engine", "AngelscriptRuntime"
    });

    if (Target.bBuildEditor)
    {
        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "CQTest", "Networking", "Sockets", "UnrealEd", "AngelscriptEditor"
        }); // ★ 测试模块直接依赖编辑器与 CQTest
    }
}
```

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 独立测试模块 | 当前插件子树未见 | `AngelscriptTest` 明确存在 | puerts 没有实现 |
| 测试组织 | 仅见零散 sample/test-like 文件 | Runtime / Editor / Test 多层测试 | Angelscript 实现质量更完整 |
| 测试指南 | 当前插件子树未见同等级测试指南 | `TESTING_GUIDE.md` / `TESTING_GUIDE_ZH.md` | puerts 没有实现 |

## [维度 D10] 文档与示例组织

puerts 在 UE 插件子树里的“文档主产物”更偏机器可消费形态，也就是 `Typing/ue/*.d.ts`、`Typing/cpp/index.d.ts` 以及 `CodeAnalyze` 维护的类型与 Blueprint 资产信息。换言之，它把 IDE 智能提示本身当作文档出口，而不是在插件子树里维护大量说明性 markdown。

Angelscript 在这一维的策略不同。`FAngelscriptDocs` 会把 Unreal tooltip、分类、属性文档汇总进脚本层，再在 `DumpDocumentation(...)` 里导出完整文档；测试目录还附带中英文测试与宏说明文档。因此 Angelscript 的文档产出兼顾“开发者阅读”与“运行时导出”，而 puerts 更偏“IDE 内即文档”。

```
[puerts] Documentation Surface
reflection scan
 -> ue.d.ts / ue_bp.d.ts / cpp index.d.ts
 -> TypeScript IDE becomes the main doc reader

[Angelscript] Documentation Surface
Unreal metadata + script metadata
 -> FAngelscriptDocs cache
 -> DumpDocumentation()
 -> TESTING_GUIDE / macro guides
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:417-457`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp
// 位置: 417-457，d.ts 文件就是文档/类型产物
// ============================================================================
PlatformFile.DeleteFile(*(PuertsBaseDir / TEXT("Typing/ue/ue.d.ts")));
PlatformFile.DeleteFile(*(PuertsBaseDir / TEXT("Typing/ue/ue_bp.d.ts")));

const FString UEDeclarationFilePath = FPaths::ProjectDir() / TEXT("Typing/ue/ue.d.ts");
FFileHelper::SaveStringToFile(ToString(), *UEDeclarationFilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

const FString BPDeclarationFilePath = FPaths::ProjectDir() / TEXT("Typing/ue/ue_bp.d.ts");
FFileHelper::SaveStringToFile(ToString(), *BPDeclarationFilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM); // ★ 文档以 IDE 类型文件落地
```

[2] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp:31-53,407-520`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp
// 位置: 31-53, 407-520，把 Unreal 文档聚合并可导出
// ============================================================================
void FAngelscriptDocs::AddUnrealDocumentation(int FunctionId, FStringView Documentation, FStringView Category, UFunction* Function)
{
    FPassedDoc Doc;
    Doc.Tooltip = Documentation;
    Doc.Category = Category;
    Doc.Function = Function;
    UnrealDocumentation.Add(FunctionId, Doc); // ★ 先聚合文档
}

void FAngelscriptDocs::DumpDocumentation(asIScriptEngine* Engine)
{
    TMap<FString, FDocClass> Classes;
    auto* ScriptEngine = FAngelscriptEngine::Get().Engine;
    // ★ 再把类型、属性、函数文档统一导出
}
```

另见 `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE_ZH.md:1-20,43-47`，测试子树内还提供了中文指南与入口索引，这部分在 puerts 当前 UE 插件子树中未见对应物。

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 文档主产物 | `.d.ts` 与 IDE 类型系统 | 运行时文档缓存 + 导出 + 指南文档 | 实现方式不同 |
| 面向读者 | IDE 使用者优先 | 仓内开发者与调试工具优先 | 实现方式不同 |
| 显式文档导出 | 当前检视范围内主要是类型文件生成 | `DumpDocumentation(...)` 明确支持 | puerts 在插件内没有实现同等级文档导出 |

## [维度 D11] 部署与打包

puerts 的部署重点在“后端选择 + DLL 跟包 + 运行时配置加载”。`JsEnv.Build.cs` 会把后端 DLL 通过 `RuntimeDependencies.Add(..., StagedFileType.NonUFS)` 显式带进输出，同时 `PuertsModule.cpp` 说明在 `NonPak Game` 下需要手动读 ini，确保 `RootPath`、`DebugPort`、`WatchDisable` 等设置在模块加载后仍然正确。它对多平台、多后端部署的关注点非常明确。

Angelscript 的部署重点则是“Cooked / 非编辑器环境的脚本预编译”。`FAngelscriptEngineConfig` 暴露 `bSimulateCooked`、`bGeneratePrecompiledData`、`bIgnorePrecompiledData`，运行时再决定是否启用 `bUsePrecompiledData` 并初始化 `StaticJIT`。在当前检视文件里，两边都没有看到脚本加密或签名实现；差异主要不在安全封装，而在“外部 VM 二进制打包”与“内部脚本字节码预编译”。

```
[puerts] Packaging
Build.cs backend choice
 -> stage backend DLLs as NonUFS
 -> load RootPath / DebugPort / WatchDisable from ini
 -> run external JS assets from configured root

[Angelscript] Packaging
command line flags
 -> generate / ignore precompiled data
 -> decide bUsePrecompiledData in cooked runtime
 -> StaticJIT + PrecompiledData
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs:360-367,502-523,627-663`

```csharp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs
// 位置: 360-367, 502-523, 627-663，后端 DLL 打包
// ============================================================================
var DestDllPath = Path.Combine("$(BinaryOutputDir)", DllName);
RuntimeDependencies.Add(DestDllPath, DllPath, StagedFileType.NonUFS); // ★ 非 UFS 资源显式跟包

RuntimeDependencies.Add("$(TargetOutputDir)/libnode.dll", Path.Combine(V8LibraryPath, "libnode.dll"));
AddRuntimeDependencies(new string[] { "msys-quickjs.dll" }, V8LibraryPath, false);
```

[2] `Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:224-238,407-446`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp
// 位置: 224-238, 407-446，运行时配置与 NonPak 兼容
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
    JsEnv->WaitDebugger(Settings.WaitDebuggerTimeout);
}

// ★ NonPak Game 下显式再读一遍 ini，保证 CDO 配置正确
RegisterSettings();
WatchEnabled = !Settings.WatchDisable;
if (Settings.AutoModeEnable)
{
    Enable();
}
```

[3] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:519-524,1427-1442`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 519-524, 1427-1442，Cooked / 预编译数据开关
// ============================================================================
Config.bSimulateCooked = FParse::Param(FCommandLine::Get(), TEXT("as-simulate-cooked"));
Config.bGeneratePrecompiledData = FParse::Param(FCommandLine::Get(), TEXT("as-generate-precompiled-data"));
Config.bIgnorePrecompiledData = FParse::Param(FCommandLine::Get(), TEXT("as-ignore-precompiled-data"));

bUsePrecompiledData = !bGeneratePrecompiledData && !RuntimeConfig.bIgnorePrecompiledData
    && !RuntimeConfig.bRunningCommandlet && !WITH_EDITOR && !bScriptDevelopmentMode;

if (bGeneratePrecompiledData)
{
    StaticJIT = new FAngelscriptStaticJIT();
    StaticJIT->PrecompiledData = PrecompiledData; // ★ 部署优化核心是预编译与 JIT
}
```

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 多后端部署 | Node.js / QuickJS / V8 构建时切换 | 单引擎，无多后端切换 | Angelscript 没有实现 |
| 跟包策略 | `RuntimeDependencies` + `StagedFileType.NonUFS` | 重点不在 DLL 跟包，而在脚本预编译 | 实现方式不同 |
| Cooked 优化 | 当前主要是配置加载与后端资源打包 | `bUsePrecompiledData` + `StaticJIT` | 实现方式不同 |
| 加密/签名 | 当前检视文件未见实现 | 当前检视文件未见实现 | 双方都没有在当前范围内实现 |

## 小结

- puerts 最值得 Angelscript 参考的不是“换成 JS”，而是三条工程策略：统一 backend abstraction、把类型声明当第一产物、让 IDE 支持直接建立在正式产物上。
- Angelscript 在热重载失败恢复、测试基础设施、编辑器资产工作流这三块反而更厚，说明它并不落后于 puerts，只是把资源投入在不同层面。
- 若只从源码证据判断，Angelscript 当前最明显的缺口是 D6 类型声明生成链与 D9 独立测试分层；D2 不是“没有绑定”，而是“绑定 DSL 自动化程度低于 puerts”；D4 也不是“没有热重载”，而是两边重载粒度与失败恢复模型不同。
- D11 中关于“加密/签名”的结论必须克制：在本轮检视文件里，两边都没有看到插件内脚本签名/加密实现，不能凭项目介绍替代源码证据。

## 与 Angelscript 差异速查

| 维度 | puerts 结论 | Angelscript 现状 | 差距判断 |
| --- | --- | --- | --- |
| D1 架构 | 多模块 + 多后端抽象 | 三模块，单引擎嵌入 | 实现方式不同 |
| D2 绑定 | `DefineClass` + `pesapi_define_class` + lazy mapper | `FAngelscriptBinds` + `Bind_*.cpp` + UHT 函数表 | Angelscript 在统一 DSL 上没有实现 |
| D3 Blueprint | TS 生成类重定向到 JS | 脚本类显式创建 Blueprint 资产 | 实现方式不同 |
| D4 热重载 | Inspector HMR + 类重绑 | Soft/Full reload + 失败回退队列 | Angelscript 在失败恢复上更完整 |
| D5 调试 | V8 Inspector / DevTools 路线 | 自定义调试协议 + 协议测试 | 实现方式不同 |
| D6 IDE | `.d.ts` 生成链 + TS LanguageService | 源码导航 + 调试数据库 | Angelscript 没有实现同等级声明链 |
| D7 编辑器 | 菜单与分析器偏代码工作流 | 资产、菜单、StateDump 更完整 | 实现方式不同 |
| D8 性能 | FastCall + 多 VM 后端选择 | StaticJIT + PrecompiledData | 实现方式不同 |
| D9 测试 | 当前插件子树未见独立测试模块 | `AngelscriptTest` + 大量自动化测试 | puerts 在当前范围内没有实现 |
| D10 文档 | IDE 类型文件即文档主产物 | 文档缓存导出 + 测试指南 | 实现方式不同 |
| D11 部署 | NonUFS 跟包 + 多后端部署 | Cooked 预编译/JIT 优化 | 实现方式不同 |

---

## 深化分析 (2026-04-08 18:22:15)

### [维度 D6] 代码生成与 IDE 支持：`ue.d.ts` 是正式产物，`DebugDatabase` 是运行时元数据流

这一轮往下看，puerts 的 `.d.ts` 不是“额外导出一份提示文件”这么简单，而是把 Blueprint 增量信息、模板绑定扩展方法、以及 Unreal tooltip 一起折进类型产物。`DeclarationGenerator` 先恢复旧的 `ue_bp.d.ts` 缓存，再重新扫 AssetRegistry 与 Widget Blueprint，最后把 `Typing/ue/ue.d.ts` / `Typing/ue/ue_bp.d.ts` 写回项目目录；`PuertsEditor/CodeAnalyze.ts` 又直接把这些文件作为 `ts.LanguageService` 的输入，所以“生成链”和“IDE 消费链”共享同一份事实来源。

Angelscript 的 IDE 路线则是另一种哲学：它没有在当前仓库里产出一套等价的离线声明文件，而是把函数、类型、文档、设置打包成 `DebugDatabase` JSON，通过调试服务器推给外部工具，再把“跳定义”回落到 `FSourceCodeNavigation` 与脚本源文件行号。也就是说，puerts 偏“静态声明驱动”，Angelscript 偏“运行时数据库驱动”。

```
[puerts] Type Intelligence Pipeline
UE reflection / AssetRegistry
 -> RestoreBlueprintTypeDeclInfos()      // 恢复旧蓝图声明片段
 -> LoadAllWidgetBlueprint()             // 重新扫描蓝图资产
 -> GatherExtensions()                   // 合并模板绑定/扩展方法
 -> GenResolvedFunctions()               // 写入 overload + tooltip
 -> Typing/ue/ue.d.ts / ue_bp.d.ts
 -> CodeAnalyze.ts LanguageService

[Angelscript] IDE Metadata Pipeline
script engine / docs cache
 -> SendDebugDatabase()                  // 发送 JSON 元数据
 -> FAngelscriptDocs                     // 注入函数/类型/属性文档
 -> GoToDefinition                       // 解析符号到 UE / 脚本对象
 -> FSourceCodeNavigation                // 跳到脚本文件行号
 -> external IDE / editor action
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:417-457,568-616,1110-1205`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp
// 函数: FTypeScriptDeclarationGenerator::GenTypeScriptDeclaration /
//       RestoreBlueprintTypeDeclInfos / GatherExtensions / GenResolvedFunctions
// 位置: 417-457, 568-616, 1110-1205
// ============================================================================
PlatformFile.DeleteFile(*(PuertsBaseDir / TEXT("Typing/ue/ue.d.ts")));
PlatformFile.DeleteFile(*(PuertsBaseDir / TEXT("Typing/ue/ue_bp.d.ts")));

// ★ 先把项目侧 Typing/JavaScript 内容拷回插件目录，保持编辑器工具链与产物同步
PlatformFile.CopyDirectoryTree(*ProjectTypingDir, *(PuertsBaseDir / TEXT("Typing")), false);
PlatformFile.CopyDirectoryTree(
    *(FPaths::ProjectContentDir() / TEXT("JavaScript")), *(PuertsBaseDir / TEXT("Content") / TEXT("JavaScript")), true);

FFileHelper::SaveStringToFile(ToString(), *UEDeclarationFilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
FFileHelper::SaveStringToFile(ToString(), *BPDeclarationFilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

// ★ 从旧 ue_bp.d.ts 中恢复 Blueprint 类型片段，避免每次全量丢失资产侧声明
FString TypeDecl = FileContent.Mid(VersionInfoEnd + 1, DeclEnd - VersionInfoEnd - 1);
BlueprintTypeDeclInfoCache.Add(
    FName(*PackageName), {NameToDecl, FileVersionString, bIsExist, true, bIsAssociation});

// ★ 把模板绑定函数、方法、属性、静态变量都并入声明输出，而不是只导出 UClass/UFunction
PUERTS_NAMESPACE::NamedFunctionInfo* FunctionInfo = ClassDefinition->FunctionInfos;
GenTemplateBindingFunction(Tmp, FunctionInfo, true);
TryToAddOverload(Outputs, FunctionInfo->Name, true, Tmp.Buffer);

// ★ 将 Unreal tooltip 写进 overload 前面的注释，IDE hover 直接复用
FString DocString = Function->GetMetaData(TEXT("ToolTip"));
if (!DocString.IsEmpty())
{
    Buff << "    /*\n";
    Buff << tmp;
    Buff << "     */\n";
}
Buff << "    " << *OverloadIter << ";\n";
```

[2] `Reference/puerts/unreal/Puerts/PuertsEditor/CodeAnalyze.ts:285-354,465-560`

```ts
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/PuertsEditor/CodeAnalyze.ts
// 位置: 285-354, 465-560，编辑器内直接托管 TypeScript LanguageService
// ============================================================================
const servicesHost: ts.LanguageServiceHost = {
  getScriptVersion: fileName => {
      // ★ 版本号直接取文件 MD5，避免靠时间戳导致增量编译误判
      let md5 = UE.FileSystemOperation.FileMD5Hash(fileName);
      fileVersions[fileName] = { version: md5, processed: false};
      return md5;
  },
  getScriptSnapshot: fileName => {
      // ★ snapshot 缓存与 version 绑定，声明文件改动会进入同一条增量链
      scriptSnapshotsCache.set(fileName, {
          version:fileVersions[fileName].version,
          scriptSnapshot: ts.ScriptSnapshot.fromString(sourceFile)
      });
  },
};

let service = ts.createLanguageService(servicesHost, ts.createDocumentRegistry());

// ★ UE 文件读取偶发失败时，直接重建 LanguageService，避免 TS 增量状态损坏
function getProgramFromService() {
    while(true) {
        try { return service.getProgram(); }
        catch (e) { service = ts.createLanguageService(servicesHost, ts.createDocumentRegistry()); }
    }
}

dirWatcher.OnChanged.Add((added, modified, removed) => {
    // ★ 目录监听命中后只增量编译变更文件，再刷新 Blueprint 资产
    onSourceFileAddOrChange(fileName, true);
    refreshBlueprints();
});

const diagnostics = [
    ...program.getSyntacticDiagnostics(sourceFile),
    ...program.getSemanticDiagnostics(sourceFile)
];
```

[3] `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:1493-1578`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp:6-44,95-139`、`Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSourceNavigationTests.cpp:69-78`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 函数: FAngelscriptDebugServer::SendDebugDatabase
// 位置: 1493-1578，向外部工具发送 IDE 元数据
// ============================================================================
FAngelscriptDebugDatabaseSettings DebugSettings;
DebugSettings.bAutomaticImports = FAngelscriptEngine::Get().ShouldUseAutomaticImportMethod();
SendMessageToClient(Client, EDebugMessageType::DebugDatabaseSettings, DebugSettings);

// ★ 这里不是生成 .d.ts，而是把类型/函数/文档拼成 JSON 调试数据库
auto MakeFuncDesc = [&](asCScriptFunction* ScriptFunction) -> TSharedPtr<FJsonObject>
{
    FuncDesc->SetStringField(TEXT("name"), Name);
    FuncDesc->SetStringField(TEXT("return"), ReturnType);

    const FString& Doc = FAngelscriptDocs::GetUnrealDocumentation(ScriptFunction->GetId());
    if (Doc.Len() != 0)
        FuncDesc->SetStringField(TEXT("doc"), Doc);
};

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp
// 位置: 6-44, 95-139，把脚本类/函数映射到源文件与行号
// ============================================================================
virtual bool NavigateToFunction(const UFunction* InFunction) override
{
    auto* ASFunc = Cast<const UASFunction>(InFunction);
    FString Path = ASFunc->GetSourceFilePath();
    OpenFile(Path, ASFunc->GetSourceLineNumber());   // ★ 直接跳脚本源文件
    return true;
}

void RegisterAngelscriptSourceNavigation()
{
    FSourceCodeNavigation::AddNavigationHandler(new FAngelscriptSourceCodeNavigation);
}

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSourceNavigationTests.cpp
// 位置: 69-78，源码导航具备自动化回归保护
// ============================================================================
UASFunction* RuntimeASFunction = Cast<UASFunction>(RuntimeFunction);
TestEqual(TEXT("Generated function should preserve source file path"), RuntimeASFunction->GetSourceFilePath(), ScriptPath);
TestEqual(TEXT("Generated function should preserve source line number"), RuntimeASFunction->GetSourceLineNumber(), 6);
TestTrue(TEXT("Source navigation should recognize generated script class"), FSourceCodeNavigation::CanNavigateToClass(RuntimeClass));
TestTrue(TEXT("Source navigation should recognize generated script function"), FSourceCodeNavigation::CanNavigateToFunction(RuntimeFunction));
```

与 Angelscript 对比：

| 对比点 | puerts 新证据 | Angelscript 新证据 | 差距判断 |
| --- | --- | --- | --- |
| IDE 主产物 | `ue.d.ts` / `ue_bp.d.ts` 是正式产物，包含 Blueprint 缓存、扩展方法、tooltip | `DebugDatabase` 是运行时 JSON 元数据流 | 实现方式不同 |
| Blueprint 与扩展方法并入 | `RestoreBlueprintTypeDeclInfos()` + `GatherExtensions()` 明确合并 | 主要在运行时数据库中表达，不生成静态声明文件 | Angelscript 没有实现同等级离线声明产物 |
| IDE 消费链 | `CodeAnalyze.ts` 内嵌 `ts.LanguageService`，用 MD5 驱动增量编译 | 调试服务器按需发送数据库，外加源码导航处理器 | 实现方式不同 |
| 跳定义保障 | 当前检视范围内主要看到 TS 语言服务托管 | `GoToDefinition` + `FSourceCodeNavigation` + 自动化测试 | Angelscript 在“活体导航回归保护”上实现质量更完整 |

### [维度 D4] 热重载：puerts 依赖 Inspector 改源码，Angelscript 维护显式的 reload 等级与失败队列

已有分析说 puerts 是模块级 HMR、Angelscript 是软/全量重载；这一轮更细一点，差异其实在“失败模型”。puerts 的 UE 插件层只监听**已经被加载过**的 `.js` 文件：`OnSourceLoaded()` 才会把目录和文件放入 watcher，后续 `OnDirectoryChanged()` 再用 MD5 去抖。真正的替换动作不是重新建 VM，而是通过 `hot_reload.js` 里的 `Debugger.getScriptSource` / `Debugger.setScriptSource` 原地改脚本，再由 `RebindJs()` 给 `UTypeScriptGeneratedClass` 打上 `NeedReBind`，第一次命中 `execLazyLoadCallJS` 时再触发 `NotifyReBind()`。

Angelscript 反过来把“何时可以软重载、何时必须全量重载、何时只能先保留旧代码”编码成显式状态机。目录变化先进入 `FileChangesDetectedForReload` / `FileDeletionsDetectedForReload` 队列，`CheckForHotReload()` 根据 PIE / GameWorld 选择 `SoftReloadOnly` 或 `FullReload`；`ClassGenerator` 再按属性、函数签名、metadata、脚本对象尺寸决定 `SoftReload`、`FullReloadSuggested`、`FullReloadRequired`。如果当前环境不允许 full reload，它会保留旧代码并把文件塞进 `QueuedFullReloadFiles`，下次自动重试。

```
[puerts] HMR Execution Path
loaded script
 -> OnSourceLoaded()                    // 仅已加载文件进入监听
 -> OnDirectoryChanged() + MD5
 -> ReloadSource(Path, Source)
 -> __reload(url, source)
 -> Debugger.setScriptSource
 -> RebindJs() marks NeedReBind
 -> execLazyLoadCallJS -> NotifyReBind()

[Angelscript] Reload State Machine
.as add/remove/change
 -> QueueScriptFileChanges()
 -> CheckForHotReload(SoftReloadOnly | FullReload)
 -> ClassGenerator computes ReloadReq
    -> SoftReload
    -> FullReloadSuggested
    -> FullReloadRequired
 -> keep old code if needed
 -> QueuedFullReloadFiles / PreviouslyFailedReloadFiles
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/SourceFileWatcher.cpp:22-49,52-80`、`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js:67-95`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/SourceFileWatcher.cpp
// 函数: FSourceFileWatcher::OnSourceLoaded / OnDirectoryChanged
// 位置: 22-49, 52-80
// ============================================================================
if (!WatchedDirs.Contains(Dir))
{
    // ★ 只有“已经加载过源码”的目录才会进入监听，不做全盘扫描
    DirectoryWatcher->RegisterDirectoryChangedCallback_Handle(
        Dir,
        IDirectoryWatcher::FDirectoryChanged::CreateRaw(this, &FSourceFileWatcher::OnDirectoryChanged),
        DelegateHandle,
        IDirectoryWatcher::IgnoreChangesInSubtree);
}

if (Change.Action == FFileChangeData::FCA_Modified && Change.Filename.EndsWith(TEXT(".js")))
{
    FMD5Hash Hash = FMD5Hash::HashFile(*NotifyPath);
    if (WatchedFiles[Dir][FileName] != Hash)
    {
        OnWatchedFileChanged(NotifyPath);  // ★ 变更后直接回调 reload
        WatchedFiles[Dir][FileName] = Hash;
    }
}
```

```js
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js
// 位置: 67-95，模块热替换依赖 Inspector 改脚本源码
// ============================================================================
async function reload(moduleName, url, source) {
    let scriptId = parsedScript.get(url);
    if (scriptId) {
        let orgSourceInfo = await sendCommand("Debugger.getScriptSource", {scriptId:"" + scriptId});
        if (orgSourceInfo.scriptSource == source) {
            console.log(`source not changed, skip ${url}`);
            return;
        }
        let m = puerts.getModuleByUrl(url);
        puerts.emit('HMR.prepare', moduleName, m, url);         // ★ 替换前通知模块
        let res = await sendCommand("Debugger.setScriptSource", {scriptId:"" + scriptId,scriptSource:source});
        puerts.emit('HMR.finish', moduleName, m, url);          // ★ 替换后通知模块
    } else {
        console.warn(`can not find scriptId for ${url}`);       // ★ 当前插件层没有进一步排队重试
    }
}
```

[2] `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:1618-1685`、`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/TypeScriptGeneratedClass.cpp:57-99`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp
// 函数: FJsEnvImpl::RebindJs
// 位置: 1618-1685，一次性标记所有待重绑的 TS 类与对象
// ============================================================================
if (auto TsClass = Cast<UTypeScriptGeneratedClass>(Class))
{
    TsClass->DynamicInvoker = TsDynamicInvoker;
    TsClass->ClassConstructor = &UTypeScriptGeneratedClass::StaticConstructor;
    TsClass->NeedReBind = true;                 // ★ 这里只打标，不立刻逐对象重绑
    TsClass->GeneratedObjects.Empty(TsClass->GeneratedObjects.Num());
    TsClass->LazyLoadRedirect();
}

// ★ 注释已经说明：把全量遍历前置到 RebindJs，避免 NotifyReBind 多次扫描对象
for (TObjectIterator<UObject> It; It; ++It)
{
    if (It->GetClass()->ClassConstructor == UTypeScriptGeneratedClass::StaticConstructor)
    {
        ...
    }
}

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/TypeScriptGeneratedClass.cpp
// 位置: 57-99，第一次命中时再真正触发动态重绑
// ============================================================================
DEFINE_FUNCTION(UTypeScriptGeneratedClass::execLazyLoadCallJS)
{
    NotifyRebind(Context ? Context->GetClass() : Class);  // ★ 惰性触发重绑
    Class->RestoreNativeFunc();
    execCallJS(Context, Stack, RESULT_PARAM);
}

if (TsClass->NeedReBind && TsClass->DynamicInvoker.IsValid())
{
    TsClass->NeedReBind = false;
    CachedClass->DynamicInvoker.Pin()->NotifyReBind(CachedClass);
}
```

[3] `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp:43-89`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:1079-1315`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2729-2771,3938-3992,4168-4186`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp
// 位置: 43-89，目录变化先进入 reload 队列
// ============================================================================
if (AbsolutePath.EndsWith(TEXT(".as")))
{
    if (Change.Action == FFileChangeData::EFileChangeAction::FCA_Removed)
        Engine.FileDeletionsDetectedForReload.AddUnique({ AbsolutePath, RelativePath });
    else
        Engine.FileChangesDetectedForReload.AddUnique({ AbsolutePath, RelativePath });
}
else if (Change.Action == FFileChangeData::EFileChangeAction::FCA_Added && FileManager.DirectoryExists(*AbsolutePath))
{
    // ★ 新目录会递归收集其中脚本文件，不只是盯单文件
    FAngelscriptEngine::FindScriptFiles(FileManager, RelativePath, AbsolutePath, TEXT("*.as"), ContainedScriptFiles, false, false);
}

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 位置: 1079-1315，按结构变化计算 reload 等级
// ============================================================================
if (ClassData.OldClass->SuperClass != ClassData.NewClass->SuperClass)
    ClassData.ReloadReq = EReloadRequirement::FullReloadRequired;

if (PropertyDesc->PropertyType != OldPropertyDesc->PropertyType)
    ClassData.ReloadReq = EReloadRequirement::FullReloadRequired;

if (!OldFunctionDesc->SignatureMatches(NewFunctionDesc))
    ClassData.ReloadReq = EReloadRequirement::FullReloadRequired;

if (!NewFunctionDesc->Meta.OrderIndependentCompareEqual(OldFunctionDesc->Meta))
    ClassData.ReloadReq = EReloadRequirement::FullReloadSuggested;
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 2729-2771, 3938-3992, 4168-4186，失败恢复与延后 full reload
// ============================================================================
if (CompileType != ECompileType::SoftReloadOnly)
{
    for (const auto& QueuedFile : QueuedFullReloadFiles)
        FileList.AddUnique(QueuedFile);   // ★ 之前欠下的 full reload 本轮一并吃掉
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
            // ★ PIE 中不能 full reload 时，明确保留旧代码继续跑
            bShouldSwapInModules = false;
            bFullReloadRequired = true;
        }
        break;
}

if (Result == ECompileResult::ErrorNeedFullReload)
{
    for (const auto& RepeatFile : AllCompiledFiles)
        QueuedFullReloadFiles.Add(RepeatFile);      // ★ 下次自动重试
}
else if (Result == ECompileResult::PartiallyHandled)
{
    for (const auto& RepeatFile : AllCompiledFiles)
        QueuedFullReloadFiles.Add(RepeatFile);      // ★ 软重载后补一次 full reload
}
```

与 Angelscript 对比：

| 对比点 | puerts 新证据 | Angelscript 新证据 | 差距判断 |
| --- | --- | --- | --- |
| 监听范围 | `OnSourceLoaded()` 后才监听对应目录/文件 | 目录变化进入统一队列，新目录会递归扫描 | 实现方式不同 |
| 替换粒度 | `Debugger.setScriptSource` 原地替换单模块源码 | 先分类结构变化，再执行 soft/full reload | 实现方式不同 |
| 重绑策略 | `NeedReBind` + `execLazyLoadCallJS` 惰性重绑 | `PerformSoftReload()` / `PerformFullReload()` 显式分支 | 实现方式不同 |
| 失败恢复 | 当前插件层主要是 `warn/error`，未见同等级待重试队列 | `QueuedFullReloadFiles` / `PreviouslyFailedReloadFiles` 明确存在 | Angelscript 在失败恢复模型上实现质量更完整 |

### [维度 D11] 部署与打包：puerts 打包的是 VM 二进制与脚本文件边界，Angelscript 打包的是预编译缓存边界

已有结论提到 puerts 会跟包 DLL、Angelscript 会使用预编译数据；本轮更进一步，能看到两边“部署边界”的对象根本不同。puerts 的 `JsEnv.Build.cs` 在构建期就决定 Node.js / QuickJS / V8 路线，并把运行库以 `NonUFS` 方式塞进输出目录；`DefaultJSModuleLoader` 运行时仍然按普通文件查找 `.js/.mjs/.cjs/.json`，可选再查 `.mbc/.cbc`，最后直接 `OpenRead` 原始文件。也就是说，在当前插件层里，puerts 的部署优化主要是“后端二进制 staging + 可选 bytecode 后缀”，不是脚本内容加密。

Angelscript 的部署边界则落在 `PrecompiledScript*.Cache` 与 `Binds.Cache`。启动时先根据命令行/运行配置判断是否生成或使用预编译数据，再按 build configuration 选取 cache 文件，校验 `BuildIdentifier` 与 `StaticJIT` GUID，之后分三阶段把模块、类、函数、全局变量恢复回 AngelScript 内部结构，最后在非开发模式清理多余运行时数据以减内存。它打包的是“引擎内部可恢复状态”，不是外部 VM 动态库。

```
[puerts] Packaging Boundary
JsEnv.Build.cs
 -> choose V8 / Node.js / QuickJS
 -> stage runtime DLLs as NonUFS
 -> DefaultJSModuleLoader search Content/<RootPath>
 -> load .js / .mjs / .cjs / .json
 -> optional .mbc / .cbc when WITH_V8_BYTECODE

[Angelscript] Packaging Boundary
engine startup
 -> load Binds.Cache
 -> choose PrecompiledScript[_Config].Cache
 -> validate BuildIdentifier / JIT GUID
 -> ApplyToModule Stage1 / Stage2 / Stage3
 -> ClearUnneededRuntimeData()
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs:23-34,53,154-166,360-367,406-409,502-523,624-667`

```csharp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs
// 位置: 23-34, 53, 154-166, 360-367, 406-409, 502-523, 624-667
// ============================================================================
private bool UseNodejs = false;
private bool UseQuickjs = false;
public bool WithByteCode = false;   // ★ 当前默认不开启 bytecode 路线

if (UseNodejs)
{
    ThirdPartyNodejs(Target);
}
else if (UseQuickjs)
{
    ThirdPartyQJS(Target);
}
else if (UseV8Version > SupportedV8Versions.VDeprecated)
{
    ThirdParty(Target);
}

void AddRuntimeDependencies(string[] DllNames, string LibraryPath, bool Delay)
{
    var DestDllPath = Path.Combine("$(BinaryOutputDir)", DllName);
    RuntimeDependencies.Add(DestDllPath, DllPath, StagedFileType.NonUFS);  // ★ 运行库按 NonUFS 跟包
}

if (WithByteCode)
{
    PrivateDefinitions.Add("WITH_V8_BYTECODE");
}

RuntimeDependencies.Add("$(TargetOutputDir)/libnode.dll", Path.Combine(V8LibraryPath, "libnode.dll"));
AddRuntimeDependencies(new string[] { "msys-quickjs.dll" }, V8LibraryPath, false);
AddRuntimeDependencies(new string[] { "libgcc_s_seh-1.dll", "libwinpthread-1.dll" }, V8LibraryPath, true);
```

[2] `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DefaultJSModuleLoader.cpp:67-83,92-139`、`Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:374-389,405-446`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DefaultJSModuleLoader.cpp
// 位置: 67-83, 92-139，脚本按文件系统与 node_modules 规则装载
// ============================================================================
bool IsJs = Extension == TEXT("js") || Extension == TEXT("mjs") || Extension == TEXT("cjs") || Extension == TEXT("json");
return SearchModuleWithExtInDir(Dir, RequiredModule + ".js", Path, AbsolutePath) ||
       SearchModuleWithExtInDir(Dir, RequiredModule + ".mjs", Path, AbsolutePath) ||
       SearchModuleWithExtInDir(Dir, RequiredModule + ".cjs", Path, AbsolutePath) ||
#if defined(WITH_V8_BYTECODE)
       SearchModuleWithExtInDir(Dir, RequiredModule + ".mbc", Path, AbsolutePath) ||
       SearchModuleWithExtInDir(Dir, RequiredModule + ".cbc", Path, AbsolutePath) ||
#endif
       SearchModuleWithExtInDir(Dir, RequiredModule / "package.json", Path, AbsolutePath);

return SearchModuleInDir(FPaths::ProjectContentDir() / ScriptRoot, RequiredModule, Path, AbsolutePath);

IFileHandle* FileHandle = PlatformFile.OpenRead(*Path);
if (FileHandle)
{
    // ★ 直接读原始脚本文件，没有看到插件层解密/验签步骤
    Content.AddUninitialized(len);
    const bool Success = FileHandle->Read(Content.GetData(), len);
    return Success;
}

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp
// 位置: 374-389, 405-446，部署行为依赖 ini 与 RootPath
// ============================================================================
GConfig->GetBool(SectionName, TEXT("AutoModeEnable"), Settings.AutoModeEnable, PuertsConfigIniPath);
GConfig->GetInt(SectionName, TEXT("DebugPort"), Settings.DebugPort, PuertsConfigIniPath);
GConfig->GetBool(SectionName, TEXT("WatchDisable"), Settings.WatchDisable, PuertsConfigIniPath);

// ★ 注释明确指出 NonPak Game 下仍要手动再读 ini，保证打包运行时配置生效
RegisterSettings();
if (Settings.AutoModeEnable)
{
    Enable();
}
WatchEnabled = !Settings.WatchDisable;
```

[3] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1425-1599`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp:1535-1618,2481-2489,2642-2690`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 1425-1599，启动时决定是否加载/生成预编译脚本
// ============================================================================
bUsePrecompiledData = !bGeneratePrecompiledData && !RuntimeConfig.bIgnorePrecompiledData
    && !RuntimeConfig.bRunningCommandlet && !WITH_EDITOR && !bScriptDevelopmentMode;

if (bGeneratePrecompiledData)
{
    PrecompiledData = new FAngelscriptPrecompiledData(Engine);
    StaticJIT = new FAngelscriptStaticJIT();
    StaticJIT->PrecompiledData = PrecompiledData;
    Engine->SetJITCompiler(StaticJIT);           // ★ 部署优化落在引擎内部 JIT/Cache
}

FAngelscriptBindDatabase::Get().Load(GetScriptRootDirectory() / TEXT("Binds.Cache"), bGeneratePrecompiledData);

PrecompiledData->Load(Filename);
if (!PrecompiledData->IsValidForCurrentBuild())
{
    delete PrecompiledData;                      // ★ build 不匹配直接丢弃
}

PrecompiledData->InitFromActiveScript();
PrecompiledData->Save(Filename);                // ★ 生成 PrecompiledScript.Cache

if (!bScriptDevelopmentMode && !bGeneratePrecompiledData)
    PrecompiledData->ClearUnneededRuntimeData();

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp
// 位置: 1535-1618, 2481-2489, 2642-2690，预编译缓存是“可恢复的引擎内部态”
// ============================================================================
void FAngelscriptPrecompiledModule::ApplyToModule_Stage1(...)
{
    // ★ 先恢复类型与枚举骨架
    asCObjectType* Type = Classes[i].Create(Context, Module);
}

void FAngelscriptPrecompiledModule::ApplyToModule_Stage2(...)
{
    // ★ 再恢复属性、全局变量、函数对象
    GlobalProperties[i] = GlobalVariables[i].Create(Context, Module);
    asCScriptFunction* Function = Functions[i].Create(Context, Module);
}

void FAngelscriptPrecompiledData::ClearUnneededRuntimeData()
{
    objType->propertyTable.EraseAll();
    objType->methodTable.EraseAll();             // ★ 初始编译后主动瘦身
}

bool FAngelscriptPrecompiledData::IsValidForCurrentBuild()
{
    return BuildIdentifier == GetCurrentBuildIdentifier() && BuildIdentifier != -1;
}

void FAngelscriptPrecompiledData::Save(const FString& Filename)
{
    Writer << *this;
    FFileHelper::SaveArrayToFile(Data, *Filename);
}

void FAngelscriptPrecompiledData::Load(const FString& Filename)
{
    FFileHelper::LoadFileToArray(LoadedData, *Filename);
    Reader << *this;
}
```

与 Angelscript 对比：

| 对比点 | puerts 新证据 | Angelscript 新证据 | 差距判断 |
| --- | --- | --- | --- |
| 打包对象 | 外部 VM 动态库 + 文件系统脚本 | `Binds.Cache` + `PrecompiledScript*.Cache` + `StaticJIT` | 实现方式不同 |
| 多后端选择 | `UseNodejs` / `UseQuickjs` / `UseV8Version` 构建期切换 | 单引擎，无等价后端切换面 | Angelscript 没有实现 |
| 脚本载入形式 | `OpenRead()` 原始文件，支持 `.js/.mjs/.cjs/.json`，可选 `.mbc/.cbc` | 反序列化内部 cache，再三阶段恢复模块 | 实现方式不同 |
| 构建兼容性校验 | 当前主要看到运行库 staging 与 ini 配置 | `IsValidForCurrentBuild()` + JIT GUID 校验 | Angelscript 在缓存版本校验上实现质量更完整 |
| 加密 / 签名 | 当前插件层未见脚本解密或签名验证 | 当前检视范围内也未见 cache 加密或签名验证 | 双方都没有在当前范围内实现 |

---

## 深化分析 (2026-04-08 18:32:46)

### [维度 D2] 反射绑定机制：`puerts` 自动生成的是最终绑定代码，`Angelscript` UHT 自动生成的是函数表补丁

上一轮已经确认 puerts 有统一的 `DefineClass<T>()` DSL、Angelscript 仍以 `Bind_*.cpp` 为主。再往下看，两边“自动化”落点其实不同。puerts 的 `Source/JsEnv/Private/Gen/*_Wrap.cpp` 不是声明文件，也不是中间元数据，而是会在静态初始化阶段直接执行的**最终绑定代码**；它把构造、属性、方法都编码成 `DefineClass -> Register()` 链，再由 `pesapi_define_class(...)` 真正落到运行时后端。

Angelscript 新增的 `AngelscriptUHTTool` 则更像“函数表补丁器”。它扫描 `BlueprintCallable/BlueprintPure`，输出 `AS_FunctionTable_*.cpp`，内部只是在 `ClassFuncMaps` 里补 `AddFunctionEntry(...)`，并统计 `direct bind` / `stub` 比率。也就是说，Angelscript 不是完全没有自动化，而是自动化集中在“恢复可直绑的函数指针”和“给反射兜底提供索引”；真正的 `RegisterObjectMethod`、类型注册、属性注册仍然主要由手写绑定层完成。

```
[puerts] Generated Binding Stack
gen tool
 -> *_Wrap.cpp static initializer         // 生成的最终绑定代码
 -> DefineClass<T>()                      // 统一 DSL
 -> ClassDefineBuilder collects members   // 收集构造/方法/属性
 -> Register()
 -> pesapi_define_class(...)
 -> runtime mapper / backend

[Angelscript] Generated Function Table Stack
UHT exporter
 -> BuildEraseMacro()                     // 恢复可直绑函数指针
 -> AS_FunctionTable_*.cpp
 -> AddFunctionEntry(UClass, Name, Entry) // 只写函数表
 -> handwritten Bind_*.cpp / fallback     // 真正绑定仍在这里发生
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/Gen/FVector_Wrap.cpp:14-25,150-154`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/Gen/FVector_Wrap.cpp
// 位置: 14-25, 150-154，生成文件本身就是最终绑定代码
// ============================================================================
struct AutoRegisterForFVector
{
    AutoRegisterForFVector()
    {
        puerts::DefineClass<FVector>()
            .Constructor(CombineConstructors(MakeConstructor(FVector), MakeConstructor(FVector, float),
                MakeConstructor(FVector, float, float, float)))
            .Property("X", MakeProperty(&FVector::X))
            .Property("Y", MakeProperty(&FVector::Y))
            .Property("Z", MakeProperty(&FVector::Z))
            .Method("op_ExclusiveOr", MakeFunction(&FVector::operator^))
            .Function("CrossProduct", MakeFunction(&FVector::CrossProduct))
            // ...
            .Register();  // ★ 生成文件在静态初始化时直接完成注册
    }
};

AutoRegisterForFVector _AutoRegisterForFVector_;
```

[2] `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/StaticCall.hpp:1711-1839` 与 `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/PesapiBackend.hpp:187-239`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Public/StaticCall.hpp
// 位置: 1711-1839，ClassDefineBuilder 收集成员并把注册委托给后端
// ============================================================================
ClassDefineBuilder<T, API, RegisterAPI>& Method(
    const char* name, typename API::FunctionCallbackType func, const CFunctionInfo* info)
{
    if (info)
    {
        methodInfos_.push_back(typename API::GeneralFunctionReflectionInfo{name, info});
    }
    methods_.push_back(typename API::GeneralFunctionInfo(name, func, nullptr, info));
    return *this;
}

ClassDefineBuilder<T, API, RegisterAPI>& Property(const char* name, typename API::FunctionCallbackType getter,
    typename API::FunctionCallbackType setter = nullptr, const CTypeInfo* type = nullptr)
{
    if (type)
    {
        propertyInfos_.push_back(typename API::GeneralPropertyReflectionInfo{name, type});
    }
    properties_.push_back(typename API::GeneralPropertyInfo(name, getter, setter, nullptr, nullptr));
    return *this;
}

void Register(FinalizeFuncType Finalize)
{
    RegisterAPI::template Register<T>(Finalize, *this); // ★ DSL 只负责组装，真正注册交给后端
}
```

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Public/PesapiBackend.hpp
// 位置: 187-239，后端把 DSL 内容一次性提交给 runtime
// ============================================================================
template <typename T, typename CDB>
static void Register(FinalizeFuncType Finalize, const CDB& Cdb)
{
    size_t properties_count = Cdb.functions_.size() + Cdb.methods_.size() + Cdb.properties_.size() + Cdb.variables_.size();
    auto properties = pesapi_alloc_property_descriptors(properties_count);

    for (const auto& func : Cdb.functions_)
    {
        pesapi_set_method_info(properties, pos++, func.Name, true, func.Callback, nullptr, nullptr);
    }
    for (const auto& method : Cdb.methods_)
    {
        pesapi_set_method_info(properties, pos++, method.Name, false, method.Callback, nullptr, nullptr);
    }

    // ★ 类信息、父类、构造、属性表在这里一次性定义
    pesapi_define_class(StaticTypeId<T>::get(), Cdb.superTypeId_, Cdb.className_,
        reinterpret_cast<InitializeFuncType>(Cdb.constructor_), finalize, properties_count, properties, nullptr);

    // ★ 同时把 reflection info 保留下来，供后续 runtime mapper / FastCall 使用
    pesapi_class_type_info(PUERTS_BINDING_PROTO_ID(), StaticTypeId<T>::get(), s_constructorInfos_.data(), s_methodInfos_.data(),
        s_functionInfos_.data(), s_propertyInfos_.data(), s_variableInfos_.data());
}
```

[3] `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:21-53` 与 `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:14-22,103-139,166-215`

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs
// 位置: 21-53，UHT exporter 的职责是生成函数表与覆盖率统计
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

    Console.WriteLine(
        "AngelscriptUHTTool exporter visited {0} packages, {1} classes, {2} BlueprintCallable/Pure functions, reconstructed {3}, skipped {4}, wrote {5} module files.",
        packageCount, classCount, functionCount, reconstructedCount, skippedCount, generatedFileCount);
}
```

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs
// 位置: 14-22, 103-139, 166-215，生成产物是 AddFunctionEntry 行和 direct/stub 统计
// ============================================================================
internal sealed record AngelscriptGeneratedFunctionEntry(
    string ClassName,
    string FunctionName,
    string EraseMacro)
{
    public string BuildRegistrationLine()
    {
        return $"\tFAngelscriptBinds::AddFunctionEntry({ClassName}::StaticClass(), \"{FunctionName}\", {{ {EraseMacro} }});";
    }
}

foreach (AngelscriptGeneratedFunctionEntry entry in entries)
{
    if (entry.EraseMacro == "ERASE_NO_FUNCTION()")
        stubEntries++;
    else
        directBindEntries++;
}

Console.WriteLine(
    "AngelscriptUHTTool generated {0} binding entries ({1} direct, {2} stubs) across {3} modules and {4} shard files. Summary: {5}",
    totalGeneratedEntries, totalDirectBindEntries, totalStubEntries, moduleSummaries.Count, generatedFileCount, summaryPath);
```

[4] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:497-511`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h
// 位置: 497-511，自动生成文件写入的只是函数表索引
// ============================================================================
static void AddFunctionEntry(UClass* Class, FString Name, FFuncEntry Entry)
{
    auto& ClassFuncMaps = GetClassFuncMaps();
    if (ClassFuncMaps.Contains(Class))
    {
        if (!ClassFuncMaps[Class].Contains(Name))
        {
            ClassFuncMaps[Class].Add(Name, Entry);   // ★ 这里只是记住函数入口，不直接注册脚本类型/属性
        }
    }
    else
    {
        ClassFuncMaps.Add(Class, TMap<FString, FFuncEntry>()).Add(Name, Entry);
    }
}
```

与 Angelscript 对比：

| 对比点 | puerts 新证据 | Angelscript 新证据 | 差距判断 |
| --- | --- | --- | --- |
| 自动生成产物的性质 | `*_Wrap.cpp` 直接执行 `DefineClass<T>()...Register()` | `AS_FunctionTable_*.cpp` 只调用 `AddFunctionEntry(...)` | 实现方式不同 |
| 自动化覆盖范围 | 构造、属性、方法、静态变量都进入最终绑定代码 | 主要覆盖 `BlueprintCallable/Pure` 的函数指针恢复与索引 | Angelscript 没有实现同等级最终绑定代码生成 |
| runtime 终点 | `pesapi_define_class(...)` 真正定义类 | `ClassFuncMaps` 只是给后续绑定/反射查询提供表 | 实现方式不同 |
| 自动化质量诊断 | 当前检视范围内未见同等级 direct/stub 覆盖率统计 | UHT tool 明确输出 `direct/stub/skipped` 统计 | Angelscript 在“自动化覆盖率可观测性”上实现质量更完整 |

### [维度 D5] 调试与开发体验：`puerts` 的调试能力受后端与运行模式约束，`Angelscript` 的协议语义更 UE 化

前面的分析已经说明 puerts 复用 V8 Inspector、Angelscript 使用自定义协议。这一轮更关键的发现是：puerts 的“多后端”并没有带来统一调试面。`ThirdPartyQJS()` 在 Build.cs 里直接定义了 `WITHOUT_INSPECTOR`，`SetInspectorCallback` / `DispatchProtocolMessage` 也在 `#ifndef WITH_QUICKJS` 下编译；同时 `PuertsModule` 在 `JsEnvGroup` 模式下明确拒绝 `WaitDebugger`。所以 puerts 的调试体验并不是“所有后端统一共享一套 DevTools 工作流”，而是明显偏向 V8/Node.js 路线。

Angelscript 则相反。它虽然没有浏览器生态，但调试协议直接编码了 UE 语义：请求 `DebugDatabase`、把断点挪到下一条可执行代码行、拉变量树、求值、设置 break filter、查资产、创建 Blueprint，全都在 `HandleMessage()` 里完成。也就是说，Angelscript 的调试体验不是借用通用 JS 协议，而是把“脚本调试 + 编辑器操作 + 资源语义”做成一条统一协议栈。

```
[puerts] Debug Capability Matrix
JsEnv.Build.cs
├─ V8 / Node.js -> Inspector transport available
├─ QuickJS -> WITHOUT_INSPECTOR
└─ Group Mode -> WaitDebugger unsupported

runtime
 -> __tgjsSetInspectorCallback
 -> websocket /json/list
 -> Chrome DevTools URL

[Angelscript] Debug Semantics Pipeline
client message
 -> HandleMessage()
 -> RequestDebugDatabase / StartDebugging
 -> relocate breakpoint to next code line
 -> Variables / Evaluate / BreakFilters
 -> FindAssets / CreateBlueprint
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs:502-505,624-627` 与 `Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:194-238`

```csharp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs
// 位置: 502-505, 624-627，调试能力随 backend 变化
// ============================================================================
void ThirdPartyNodejs(ReadOnlyTargetRules Target)
{
    PrivateDefinitions.Add("WITH_NODEJS");   // ★ Node.js 仍走 Inspector 体系
}

void ThirdPartyQJS(ReadOnlyTargetRules Target)
{
    PrivateDefinitions.Add("WITHOUT_INSPECTOR"); // ★ QuickJS 明确关闭 Inspector
    PrivateDefinitions.Add("WITH_QUICKJS");
}
```

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp
// 位置: 194-238，运行模式也影响调试能力
// ============================================================================
if (NumberOfJsEnv > 1)
{
    if (Settings.DebugEnable)
    {
        JsEnvGroup = MakeShared<PUERTS_NAMESPACE::FJsEnvGroup>(NumberOfJsEnv, ..., DebugPort);
    }

    if (Settings.WaitDebugger)
    {
        UE_LOG(PuertsModule, Warning, TEXT("Do not support WaitDebugger in Group Mode!"));
    }
}
else
{
    if (Settings.DebugEnable)
    {
        JsEnv = MakeShared<PUERTS_NAMESPACE::FJsEnv>(..., DebugPort);
    }

    if (Settings.WaitDebugger)
    {
        JsEnv->WaitDebugger(Settings.WaitDebuggerTimeout); // ★ 只有单 JsEnv 模式支持等待调试器
    }
}
```

[2] `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:4522-4583` 与 `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:319-355`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp
// 位置: 4522-4583，QuickJS 下整个 Inspector JS 桥接会被裁掉
// ============================================================================
void FJsEnvImpl::SetInspectorCallback(const v8::FunctionCallbackInfo<v8::Value>& Info)
{
#ifndef WITH_QUICKJS
    if (!Inspector)
        return;

    if (!InspectorChannel)
    {
        InspectorChannel = Inspector->CreateV8InspectorChannel();
        InspectorChannel->OnMessage([this](std::string Message) { /* ★ 回注协议消息到 JS */ });
    }

    InspectorMessageHandler.Reset(Isolate, v8::Local<v8::Function>::Cast(Info[0]));
#endif    // !WITH_QUICKJS
}

void FJsEnvImpl::DispatchProtocolMessage(const v8::FunctionCallbackInfo<v8::Value>& Info)
{
#ifndef WITH_QUICKJS
    if (InspectorChannel)
    {
        InspectorChannel->DispatchProtocolMessage(TCHAR_TO_UTF8(*Message));
    }
#endif    // !WITH_QUICKJS
}
```

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp
// 位置: 319-355，前端工作流直接暴露为 Chrome DevTools URL
// ============================================================================
Server.set_http_handler(std::bind(&V8InspectorClientImpl::OnHTTP, this, std::placeholders::_1));
Server.set_open_handler(std::bind(&V8InspectorClientImpl::OnOpen, this, std::placeholders::_1));
Server.set_message_handler(
    std::bind(&V8InspectorClientImpl::OnReceiveMessage, this, std::placeholders::_1, std::placeholders::_2));

JSONList = R"([
    {
        "description": "Puerts Inspector",
        "title": "Puerts Inspector",
        "type": "node",
        "webSocketDebuggerUrl": "ws://127.0.0.1:)" + std::to_string(Port) + R"("
    }
])";

FString InspectorUrl =
    FString::Printf(TEXT("devtools://devtools/bundled/inspector.html?v8only=true&ws=127.0.0.1:%d"), Port);
UE_LOG(LogV8Inspector, Log,
    TEXT("Startup Inspector Successfully!\n"
         "Please Open This URL in Debugger Front-End(e.g. Chrome DevTool):\n \n"
         "\t%s\n \n"),
    *InspectorUrl);
```

[3] `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:822-827,955-1057,1081-1180`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 位置: 822-827, 955-1057, 1081-1180，协议直接承载 UE 语义
// ============================================================================
if (MessageType == EDebugMessageType::RequestDebugDatabase)
{
    ClientsThatWantDebugDatabase.Add(Client);
    SendDebugDatabase(Client);
    FAngelscriptEngine::Get().EmitDiagnostics(Client);   // ★ 调试数据库与诊断一起发
}
else if (MessageType == EDebugMessageType::SetBreakpoint)
{
    // ★ 把断点自动挪到“下一条真正有代码的行”
    int32 WantedLine = BP.LineNumber;
    int32 CodeLine = -1;
    for (int32 i = 0, Count = FoundModule->scriptFunctions.GetLength(); i < Count; ++i)
    {
        asCScriptFunction* Func = FoundModule->scriptFunctions[i];
        int32 LineInFunc = Func->FindNextLineWithCode(WantedLine);
        if (LineInFunc >= WantedLine && (BestLine == -1 || (LineInFunc - WantedLine) < (BestLine - WantedLine)))
            BestLine = LineInFunc;
    }
    if (CodeLine != WantedLine && BP.Id != -1)
    {
        SendMessageToClient(Client, EDebugMessageType::SetBreakpoint, ChangedBP);
    }
}
else if (MessageType == EDebugMessageType::RequestVariables)
{
    // ★ 变量树、值地址、成员信息都在协议里
    Var.Name = Value.Name;
    Var.Value = Value.Value;
    Var.Type = Value.Type;
    Var.ValueAddress = reinterpret_cast<uint64>(Value.GetAddressToMonitor());
    SendMessageToClient(Client, EDebugMessageType::Variables, Vars);
}
else if (MessageType == EDebugMessageType::BreakOptions)
{
    for (auto Filter : Options.Filters)
        BreakOptions.Add(FName(*Filter));
    BreakOptions.Add(FName("break:any"));       // ★ break filter 也是协议能力
}
else if (MessageType == EDebugMessageType::CreateBlueprint)
{
    auto ClassDesc = FAngelscriptEngine::Get().GetClass(CreateBlueprint.ClassName);
    if (ClassDesc.IsValid() && Cast<UASClass>(ClassDesc->Class) != nullptr)
    {
        FAngelscriptRuntimeModule::GetEditorCreateBlueprint().Broadcast(Cast<UASClass>(ClassDesc->Class));
    }
}
```

与 Angelscript 对比：

| 对比点 | puerts 新证据 | Angelscript 新证据 | 差距判断 |
| --- | --- | --- | --- |
| 多后端调试一致性 | `WITH_QUICKJS` 同时定义 `WITHOUT_INSPECTOR`，Group Mode 不支持 `WaitDebugger` | 单后端上统一处理断点、变量、评估、资源操作 | puerts 在自身多后端调试一致性上实现质量更弱 |
| 调试前端语义 | 主要暴露 `/json/list` 和 DevTools URL | 协议内直接编码 `BreakOptions`、`FindAssets`、`CreateBlueprint` | Angelscript 在 UE 语义集成上实现质量更完整 |
| 断点落点修正 | 当前插件层主要依赖 Inspector/脚本 URL 规则 | `FindNextLineWithCode()` 明确把断点对齐到可执行行 | Angelscript 实现质量更完整 |
| 调试数据库触发方式 | 当前主链是 Inspector 消息桥接 | `RequestDebugDatabase` 会连同诊断信息一起发送 | 实现方式不同 |

### [维度 D8] 性能与优化：`puerts` 的 FastCall 只缩短 V8 边界，`Angelscript` 的 direct bind / StaticJIT 分别优化 native call 与 script bytecode

前面的结论说 puerts 偏桥接优化、Angelscript 偏引擎内部优化；这一轮可以把“桥接”拆得更细。puerts 的 `V8FastCall` 其实有两层门槛。第一层是类型门槛，只有满足 `FastCallArgument<T>` / `IsReturnSupportedHelper<T>` 的参数和返回值才能生成 `v8::CFunction`。第二层是调用路径门槛，即便 `CppObjectMapper` 成功把 `FastCallInfo` 挂进 `FunctionTemplate`，很多 `UFunction` 调用仍然会落到 `FFunctionTranslator::FastCall()`，在那里它依旧要清零参数缓冲、遍历 `TFieldIterator<PropertyMacro>`、建立 `FFrame`、处理 `OutParm` 链，再调用 `UFunction::Invoke()`。

Angelscript 的 native call 则更接近“类型擦除后的直调”。`ASAutoCaller::FunctionCaller` 在编译期生成 `RedirectFunctionCaller` / `RedirectMethodCaller`，运行时只需要把 `void**` 参数按模板展开后调用目标函数；注册时这个 caller 被直接塞进 `RegisterObjectMethod(..., *(asFunctionCaller*)&Caller)`。另一方面，脚本函数本身又可以继续走 `StaticJIT::GenerateCppCode()`，把 bytecode 扫描、预处理、栈布局分析写成 C++ 输出。因此 Angelscript 在 D8 里其实有两条优化链，一条优化 native bind，一条优化 script bytecode。

```
[puerts] Native Call Path
JS call
 -> v8::FunctionTemplate (optional FastCallInfo)
 -> FFunctionTranslator::FastCall
 -> zero params / iterate UProperty
 -> build FFrame + OutParm list
 -> UFunction::Invoke / ProcessEvent
 -> translate return & out params

[Angelscript] Native Call Path
script call
 -> RegisterObjectMethod(... asFunctionCaller ...)
 -> RedirectMethodCaller / RedirectFunctionCaller
 -> unpack void** arguments
 -> typed C++ function call
 -> write return value

[Angelscript] Script JIT Path
script bytecode
 -> GenerateCppCode()
 -> instruction scan / prepass / stack analysis
 -> output compiled function
 -> OutputTimingData() for restore phases
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/V8FastCall.hpp:18-19,23-39,47-67,138-177` 与 `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/CppObjectMapper.cpp:208-246`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Public/V8FastCall.hpp
// 位置: 18-19, 23-39, 47-67, 138-177，FastCall 先受类型系统约束
// ============================================================================
template <typename T, typename Enable = void>
struct FastCallArgument
{
};

template <typename T>
struct FastCallArgument<T, typename std::enable_if<std::is_pointer<T>::value ...>::type>
{
    using DeclType = v8::Local<v8::Value>;
    static T Get(v8::Local<v8::Value> v)
    {
        if (V8_LIKELY(v->IsObject()))
        {
            return static_cast<T>(DataTransfer::GetPointerFast<void>(v.As<v8::Object>()));
        }
        return nullptr;
    }
};

template <typename T>
struct FastCallArgument<T, typename std::enable_if<std::is_enum<T>::value>::type>
{
    using DeclType = int;     // ★ enum、整数、浮点等才有专门 FastCall 映射
};

template <typename Ret, typename... Args, Ret (*func)(Args...)>
struct V8FastCall<Ret (*)(Args...), func,
    typename std::enable_if<IsReturnSupportedHelper<Ret>::value && IsArgsSupportedHelper<std::tuple<Args...>>::value &&
                            (sizeof...(Args) > 0)>::type>
{
    static const v8::CFunction* info()
    {
        static v8::CFunction _info = v8::CFunction::Make(Wrap); // ★ 只有“支持的签名”才有 CFunction fast path
        return &_info;
    }
};
```

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/CppObjectMapper.cpp
// 位置: 208-246，把 FastCallInfo 只挂到满足条件的方法/函数上
// ============================================================================
auto FastCallInfo = FunctionInfo->ReflectionInfo ? FunctionInfo->ReflectionInfo->FastCallInfo() : nullptr;
if (FastCallInfo)
{
    Template->PrototypeTemplate()->Set(
        v8::String::NewFromUtf8(Isolate, FunctionInfo->Name, v8::NewStringType::kNormal).ToLocalChecked(),
        v8::FunctionTemplate::New(Isolate, (v8::FunctionCallback) FunctionInfo->Callback,
            v8::External::New(Isolate, &FunctionInfo->Data), v8::Local<v8::Signature>(), 0,
            v8::ConstructorBehavior::kThrow, v8::SideEffectType::kHasSideEffect, FastCallInfo));
}
else
{
    Template->PrototypeTemplate()->Set(
        v8::String::NewFromUtf8(Isolate, FunctionInfo->Name, v8::NewStringType::kNormal).ToLocalChecked(),
        v8::FunctionTemplate::New(Isolate, (v8::FunctionCallback) FunctionInfo->Callback, v8::External::New(Isolate, &FunctionInfo->Data)));
}
```

[2] `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp:93-136,265-290,295-417`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp
// 位置: 93-136, 265-290, 295-417，UFunction 快路径仍然保留 UE 反射编组成本
// ============================================================================
ParamsBufferSize = InFunction->PropertiesSize > InFunction->ParmsSize ? InFunction->PropertiesSize : InFunction->ParmsSize;
for (TFieldIterator<PropertyMacro> It(InFunction); It && (It->PropertyFlags & CPF_Parm); ++It)
{
    PropertyMacro* Property = *It;
    if (Property->HasAnyPropertyFlags(CPF_ReturnParm))
        Return = FPropertyTranslator::Create(Property);
    else
        Arguments.push_back(FPropertyTranslator::Create(Property)); // ★ translator 预构建，但每次调用仍要逐参处理
}

if ((Function->FunctionFlags & FUNC_Native) && !(Function->FunctionFlags & FUNC_Net) &&
    !CallFunctionPtr->HasAnyFunctionFlags(FUNC_UbergraphFunction))
{
    FastCall(Isolate, Context, Info, CallObject, CallFunctionPtr, Params);
}
else
{
    SlowCall(Isolate, Context, Info, CallObject, CallFunctionPtr, Params);
}

FMemory::Memzero(Params, ParamsBufferSize);
FFrame NewStack(CallObject, CallFunction, Params, nullptr, Function->ChildProperties);
for (TFieldIterator<PropertyMacro> It(CallFunction); It && (It->PropertyFlags & CPF_Parm); ++It)
{
    PropertyMacro* Property = *It;
    if (Property->HasAnyPropertyFlags(CPF_OutParm))
    {
        // ★ 仍要构建 OutParm 链和容器地址
    }

    if (Property->HasAnyPropertyFlags(CPF_OutParm))
    {
        Arguments[Index]->JsToUEFastInContainer(Isolate, Context, Info[Index], Params, reinterpret_cast<void**>(&(Out->PropAddr)));
    }
    else
    {
        Arguments[Index]->JsToUEInContainer(Isolate, Context, Info[Index], Params, false);
    }
}
CallFunction->Invoke(CallObject, NewStack, ReturnValueAddress); // ★ 最终仍走 UFunction/FFrame 调用模型
Info.GetReturnValue().Set(Return->UEToJsInContainer(Isolate, Context, Params));
```

[3] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/FunctionCallers.h:194-375` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:449-459`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/FunctionCallers.h
// 位置: 194-375，native bind 的快路径是模板展开后的直调
// ============================================================================
struct FunctionCaller
{
    using FunctionCallerPtr = void(*)(TFunctionPtr Method, void** Parameters, void* ReturnValue);
    using MethodCallerPtr = void(*)(TMethodPtr Function, void** Parameters, void* ReturnValue);
    // ★ 运行时只携带一个 type-erased caller 指针
};

template<typename ReturnType, typename... ParamTypes, int... TIndices>
FORCEINLINE void IndexedFunctionCaller(TFunctionPtr FunctionPtr, void** Arguments, void* ReturnValue, TIntegerSequence<int, TIndices...>)
{
    new(ReturnValue) (typename TReferenceToPtr<ReturnType>::Type)(
        GetAddressIfReference<ReturnType>(
            (CastedFunctionPtr.Casted)(
                (PassArgument<ParamTypes>(Arguments + TIndices))...))); // ★ 直接按模板展开参数
}

template<typename ReturnType, typename ObjectType, typename... ParamTypes, int... TIndices>
FORCEINLINE void IndexedMethodCaller(TMethodPtr MethodPtr, void** Arguments, void* ReturnValue, TIntegerSequence<int, TIndices...>)
{
    new(ReturnValue) (typename TReferenceToPtr<ReturnType>::Type)(
        GetAddressIfReference<ReturnType>(
            (((ObjectType*)Arguments[0])->*CastedMethodPtr.Casted)(
                (PassArgument<ParamTypes>(Arguments + TIndices + 1))...))); // ★ 没有 UProperty 级别的循环
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp
// 位置: 449-459，注册时直接把 caller 塞给 AngelScript
// ============================================================================
void FAngelscriptBinds::BindMethod(FBindString Signature, asSFuncPtr Ptr, ASAutoCaller::FunctionCaller Caller, void* UserData)
{
    int FunctionId = Manager.Engine->RegisterObjectMethod(
        ClassName.ToCString(), Signature.ToCString(), Ptr, asCALL_THISCALL, *(asFunctionCaller*)&Caller, nullptr);
    OnBind(FunctionId, UserData, nullptr);
}

int FAngelscriptBinds::BindMethodDirect(FBindString ClassName, FBindString Signature, asSFuncPtr Function, asECallConvTypes CallConv, ASAutoCaller::FunctionCaller Caller, void* UserData)
{
    int32 FunctionId = Manager.Engine->RegisterObjectMethod(ClassName.ToCString(), Signature.ToCString(), Function, CallConv, *(asFunctionCaller*)&Caller);
    OnBind(FunctionId, UserData, nullptr);
    return FunctionId;
}
```

[4] `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp:3204-3248` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp:2973-3002`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp
// 位置: 3204-3248，脚本函数本身还能继续被转成 C++ 输出
// ============================================================================
void FAngelscriptStaticJIT::GenerateCppCode(asIScriptFunction* ScriptFunction, FGenerateFunction& Generate)
{
    uint32 FunctionId = PrecompiledData->CreateFunctionId(ScriptFunction);
    Context.BC = ScriptFunction->GetByteCode(&BytecodeLength);
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
        Context.Instructions[i].Bytecode->PrePass(Context); // ★ 先做 pre-pass / 栈布局分析，再生成输出
    }
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp
// 位置: 2973-3002，恢复链路还有显式 timing 输出
// ============================================================================
void FAngelscriptPrecompiledData::OutputTimingData()
{
    FAngelscriptScopeTimer::OutputTime(TEXT("ProcessBytecode"), TIMER_ProcessBytecode);
    FAngelscriptScopeTimer::OutputTime(TEXT("CreateFunction"), TIMER_CreateFunction);
    FAngelscriptScopeTimer::OutputTime(TEXT("CreateClass"), TIMER_CreateClass);
    FAngelscriptScopeTimer::OutputTime(TEXT("ProcessFunctions"), TIMER_ProcessFunctions);
    FAngelscriptScopeTimer::OutputTime(TEXT("FunctionLookup"), TIMER_FunctionLookup);
    FAngelscriptScopeTimer::OutputTime(TEXT("PropertyLookup"), TIMER_PropertyLookup); // ★ 预编译恢复阶段有细粒度时序观测
}
```

与 Angelscript 对比：

| 对比点 | puerts 新证据 | Angelscript 新证据 | 差距判断 |
| --- | --- | --- | --- |
| fast path 适用范围 | `V8FastCall` 先受类型门槛限制，且 QuickJS 不支持 | direct bind 由 `FunctionCaller` 模板统一展开，单引擎路径一致 | 实现方式不同 |
| native 调用桥接成本 | `FFunctionTranslator::FastCall()` 仍要遍历 `UProperty`、构建 `FFrame`、处理 `OutParm` | `RedirectMethodCaller` / `RedirectFunctionCaller` 直接展开 `void**` 参数 | Angelscript 在 native bridge 路径上实现质量更完整 |
| script JIT 路线 | V8/Node.js 借后端 JIT，QuickJS 无等价 V8 fast path | `StaticJIT::GenerateCppCode()` 自己分析 bytecode 并生成 C++ | 实现方式不同 |
| 阶段观测能力 | 当前范围主要见到 `dumpStatisticsLog` 这类 V8 heap 统计 | `OutputTimingData()` 明确输出恢复/JIT 各阶段耗时 | Angelscript 在性能可观测性上实现质量更完整 |

---

## 深化分析 (2026-04-08 18:42:56)

### [维度 D1] 插件架构与模块划分：puerts 抽象的是“模块加载协议”，Angelscript 抽象的是“脚本根发现依赖”

前面的 D1 更偏模块数量；继续下钻后，能看到两边真正的解耦点并不在同一层。puerts 在运行时把“脚本从哪里找、怎么读”抽成 `IJSModuleLoader`，`FJsEnv` 和 `FJsEnvGroup` 都接受这个接口，`PuertsModule` 只负责把 `RootPath` 配成 `DefaultJSModuleLoader` 注入进去。也就是说，`Puerts` 模块本身不关心后续 `require` 解析细节，它只装配 loader 与 VM。

Angelscript 也不是没有抽象。`FAngelscriptEngineDependencies` 把 `ProjectDir`、路径归一化、目录存在性、插件脚本根发现都做成可注入依赖，`DiscoverScriptRoots()` 再基于这些依赖生成根目录列表。但它没有再向下抽成“模块解析/读取接口”；真正的文件枚举、预处理、编译仍然由 `FAngelscriptEngine` 自己串起来。因此这不是“有无解耦”的差别，而是“抽象颗粒度”不同：puerts 的 seam 落在 per-module load API，Angelscript 的 seam 落在 filesystem/provider 层。

```
[puerts] Runtime Loader Seam
PuertsModule
 -> DefaultJSModuleLoader(Settings.RootPath)
 -> IJSModuleLoader
 -> FJsEnv / FJsEnvGroup(shared loader)
 -> backend runtime

[Angelscript] Root Discovery Seam
EngineDependencies
 -> GetProjectDir / GetEnabledPluginScriptRoots
 -> DiscoverScriptRoots()
 -> FindAllScriptFilenames()
 -> Preprocessor / CompileModules()
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSModuleLoader.h:17-50`、`Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JsEnv.h:64-68`、`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvGroup.cpp:105-115`、`Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:194-205,224-229`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSModuleLoader.h
// 位置: 17-50，模块解析/装载先抽成独立接口
// ============================================================================
class IJSModuleLoader
{
public:
    virtual bool Search(const FString& RequiredDir, const FString& RequiredModule, FString& Path, FString& AbsolutePath) = 0;
    virtual bool Load(const FString& Path, TArray<uint8>& Content) = 0;
    virtual FString& GetScriptRoot() = 0;
};

class JSENV_API DefaultJSModuleLoader : public IJSModuleLoader
{
public:
    explicit DefaultJSModuleLoader(const FString& InScriptRoot) : ScriptRoot(InScriptRoot) {}
    FString ScriptRoot;
};

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JsEnv.h
// 位置: 64-68，JsEnv 直接接受 loader 注入
// ============================================================================
explicit FJsEnv(const FString& ScriptRoot = TEXT("JavaScript"));

FJsEnv(std::shared_ptr<IJSModuleLoader> InModuleLoader, std::shared_ptr<ILogger> InLogger, int InDebugPort,
    std::function<void(const FString&)> InOnSourceLoadedCallback = nullptr, const FString InFlags = FString(),
    void* InExternalRuntime = nullptr, void* InExternalContext = nullptr);

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvGroup.cpp
// 位置: 105-115，多 JsEnv 共享同一份 loader，而不是每个 env 自己找脚本
// ============================================================================
std::shared_ptr<IJSModuleLoader> SharedModuleLoader = std::move(InModuleLoader);
for (int i = 0; i < Size; i++)
{
    JsEnvList.push_back(std::make_shared<FJsEnvImpl>(SharedModuleLoader, InLogger, InDebugStartPort + i,
        InOnSourceLoadedCallback, InFlags, InExternalRuntime, InExternalContext));
}

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp
// 位置: 194-205, 224-229，宿主模块只负责把 RootPath 注入到 loader
// ============================================================================
JsEnvGroup = MakeShared<PUERTS_NAMESPACE::FJsEnvGroup>(NumberOfJsEnv,
    std::make_shared<PUERTS_NAMESPACE::DefaultJSModuleLoader>(Settings.RootPath),
    std::make_shared<PUERTS_NAMESPACE::FDefaultLogger>(),
    DebuggerPortFromCommandLine < 0 ? Settings.DebugPort : DebuggerPortFromCommandLine);

JsEnv = MakeShared<PUERTS_NAMESPACE::FJsEnv>(
    std::make_shared<PUERTS_NAMESPACE::DefaultJSModuleLoader>(Settings.RootPath),
    std::make_shared<PUERTS_NAMESPACE::FDefaultLogger>(),
    DebuggerPortFromCommandLine < 0 ? Settings.DebugPort : DebuggerPortFromCommandLine);
```

[2] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:86-95,167`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:539-567,1326-1363,2061-2079`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h
// 位置: 86-95, 167，抽象点落在文件系统依赖与脚本根发现
// ============================================================================
struct ANGELSCRIPTRUNTIME_API FAngelscriptEngineDependencies
{
    TFunction<FString()> GetProjectDir;
    TFunction<FString(const FString&)> ConvertRelativePathToFull;
    TFunction<bool(const FString&)> DirectoryExists;
    TFunction<bool(const FString&, bool)> MakeDirectory;
    TFunction<TArray<FString>()> GetEnabledPluginScriptRoots;
};

TArray<FString> DiscoverScriptRoots(bool bOnlyProjectRoot = false) const;

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 539-567, 1326-1363, 2061-2079，根目录发现与后续读取仍在引擎主流程中
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

FString RootPath = Dependencies.ConvertRelativePathToFull(Dependencies.GetProjectDir() / TEXT("Script"));
for (const FString& PluginScriptRoot : Dependencies.GetEnabledPluginScriptRoots())
{
    const FString ScriptPath = Dependencies.ConvertRelativePathToFull(PluginScriptRoot);
    if (Dependencies.DirectoryExists(ScriptPath) && ScriptPath != RootPath)
    {
        DiscoveredRootPaths.Add(ScriptPath);
    }
}

AllRootPaths = MakeAllScriptRoots();
FindAllScriptFilenames(Filenames);            // ★ 真正的枚举/预处理仍在引擎里
for (FFilenamePair& Filename : Filenames)
    Preprocessor.AddFile(Filename.RelativePath, Filename.AbsolutePath);
```

与 Angelscript 对比：

| 对比点 | puerts 新证据 | Angelscript 新证据 | 差距判断 |
| --- | --- | --- | --- |
| 抽象边界 | `IJSModuleLoader` 直接定义模块查找/读取协议 | `FAngelscriptEngineDependencies` 只注入路径与目录依赖 | 实现方式不同 |
| 多实例复用 | `FJsEnvGroup` 共享同一份 loader | `DiscoverScriptRoots()` 每次生成根目录列表 | 实现方式不同 |
| 宿主模块职责 | `PuertsModule` 只做 loader 注入与 VM 装配 | `FAngelscriptEngine` 后续继续负责枚举、预处理、编译 | puerts 在运行时装载边界上的解耦颗粒度更细 |

### [维度 D6] 代码生成与 IDE 支持：puerts 为声明精度额外维护默认参数与模板绑定元数据，Angelscript 把默认值用于脚本签名而不是离线声明

现有 D6 已经说明 puerts 有 `.d.ts` 流水线；这一轮更关键的新发现是，puerts 为了把声明做“准”，专门补了默认参数和模板绑定两条侧链。`DeclarationGenerator` 读取 `CPP_Default_*` metadata，把参数转成可选参数 `?`，并把默认值写成注释保留在 `.d.ts` 中；`TemplateBindingGenerator` 又遍历注册过的 `JSClassDefinition` 输出 `Typing/cpp/index.d.ts`，把运行时模板绑定也变成 IDE 可见符号。再往前一层，`JsEnv.Build.cs` 每次 build 会把手写 `Typing/` 同步到项目根，而 `CSharpParamDefaultValueMetas` / `ParamDefaultValueMetas` 负责把 UHT 看到的默认参数元数据生成为 `InitParamDefaultMetas.inl`。这说明 puerts 的 IDE 产物不是单模块生成器，而是 build + UHT + editor 三段协同。

Angelscript 这边不能简单写成“没有默认参数元数据”。它在 `Helper_FunctionSignature.h` 里同样读取 `CPP_Default_*`，`AngelscriptType.cpp` 会把 Unreal 默认值转成 Angelscript 默认值，必要时再给每种类型补 fallback。区别在于这些默认值被消费到脚本编译签名里，而不是落成一个外部 IDE/离线声明产物。所以差距点不是“能不能理解默认值”，而是“默认值有没有变成编辑器第一产物”。

```
[puerts] Declaration Precision Pipeline
UHT metadata / hand-written Typing / registered template bindings
 -> InitParamDefaultMetas.inl
 -> DeclarationGenerator reads CPP_Default_*
 -> optional params + default comments in ue.d.ts
 -> TemplateBindingGenerator => Typing/cpp/index.d.ts
 -> tsconfig/typeRoots -> LanguageService

[Angelscript] Signature Precision Pipeline
UFunction metadata
 -> Helper_FunctionSignature reads CPP_Default_*
 -> AngelscriptType converts Unreal defaults
 -> function declaration string
 -> compiler/runtime consumes defaults
 -> no equivalent offline .d.ts artifact
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs:178-181`、`Reference/puerts/unreal/Puerts/Source/CSharpParamDefaultValueMetas/CSharpParamDefaultValueMetas.cs:23-38,112-169`、`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:996-1055`、`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/TemplateBindingGenerator.cpp:193-216`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs
// 位置: 178-181，手写 Typing 不是静态资源，而是每次 build 都同步
// ============================================================================
// 每次build时拷贝一些手写的.d.ts到Typing目录以同步更新
string srcDtsDirName  = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", "Typing"));
string dstDtsDirName = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", "..", "..", "Typing"));
DirectoryCopy(srcDtsDirName, dstDtsDirName, true);
```

```csharp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/CSharpParamDefaultValueMetas/CSharpParamDefaultValueMetas.cs
// 位置: 23-38, 112-169，UHT/UBT 额外收集默认参数元数据
// ============================================================================
[UnrealHeaderTool]
class CSharpParamDefaultValueMetas
{
    [UhtExporter(Name = "Puerts", Description = "Puerts Default Values Collector", Options = UhtExporterOptions.Default, ModuleName = "JsEnv")]
    private static void UnLuaDefaultParamCollectorUbtPluginExporter(IUhtExportFactory factory)
    {
        var paramDefaultValueMetas = new CSharpParamDefaultValueMetas(factory);
        paramDefaultValueMetas.Generate();
        paramDefaultValueMetas.OutputIfNeeded(bHasGameRuntime); // ★ 产出 InitParamDefaultMetas.inl
    }
}

private static bool TryGetDefaultValue(UhtMetaData metaData, UhtProperty property, out string value)
{
    var cppKey = "CPP_Default_" + property.SourceName; // ★ 直接从 UHT metadata 抽默认值
    hasValue = metaData.TryGetValue(cppKey, out tempValue);
}
```

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp
// 位置: 996-1055，把默认参数折进 .d.ts 的可选参数与注释
// ============================================================================
const FName MetadataCppDefaultValueKey(*(FString(TEXT("CPP_Default_")) + Property->GetName()));
FString* DefaultValuePtr = nullptr;
if (MetaMap)
{
    DefaultValuePtr = MetaMap->Find(MetadataCppDefaultValueKey);
}

TmpBuf << DeDup(SafeParamName(Property->GetName()));
if (DefaultValuePtr)
{
    TmpBuf << "?";                     // ★ 有默认值的参数变成可选
}
TmpBuf << ": ";

if (DefaultValuePtr)
{
    TmpBuf << " /* = " << *DefaultValuePtr << " */"; // ★ 默认值保留给 IDE / hover
}

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/TemplateBindingGenerator.cpp
// 位置: 193-216，把运行时模板绑定也输出为 Typing/cpp/index.d.ts
// ============================================================================
PUERTS_NAMESPACE::ForeachRegisterClass(
    [&](const PUERTS_NAMESPACE::JSClassDefinition* ClassDefinition)
    {
        if (ClassDefinition->TypeId && ClassDefinition->ScriptName)
        {
            Gen.GenClass(ClassDefinition);    // ★ 不是只导出 UE 反射类型，模板绑定也导出
        }
    });

const FString FilePath = FPaths::ProjectDir() / TEXT("Typing/cpp/index.d.ts");
FFileHelper::SaveStringToFile(Gen.Output.Buffer, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
```

[2] `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h:200-230`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:591-606`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h
// 位置: 200-230，Angelscript 也会读取 CPP_Default_*，但用于脚本签名生成
// ============================================================================
FString DefaultMeta = TEXT("CPP_Default_");
DefaultMeta += Property->GetName();

FName MetaName = *DefaultMeta;
if (Function->HasMetaData(MetaName))
{
    FString MetaStr = Function->GetMetaData(MetaName);
    if (MetaStr == TEXT("None"))
        MetaStr = TEXT("");
    ArgumentDefaults.Add(MetaStr);          // ★ 默认值进签名构造器
}

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp
// 位置: 591-606，进一步把 Unreal 默认值转成 Angelscript 默认值
// ============================================================================
if (ArgumentDefaults.IsValidIndex(i)
    && ArgumentDefaults[i] != TEXT("-"))
{
    if (ArgumentTypes[i].DefaultValue_UnrealToAngelscript(ArgumentDefaults[i], AngelscriptDefaultValue))
        bValidDefault = true;
}
else
{
    if (ArgumentTypes[i].DefaultValue_AngelscriptFallback(AngelscriptDefaultValue))
        bValidDefault = true;              // ★ 无默认值时按类型兜底
}
```

与 Angelscript 对比：

| 对比点 | puerts 新证据 | Angelscript 新证据 | 差距判断 |
| --- | --- | --- | --- |
| 默认参数采集链 | UHT/UBT 额外产出 `InitParamDefaultMetas.inl`，生成器再读 `CPP_Default_*` | `Helper_FunctionSignature` + `AngelscriptType` 直接把默认值写进脚本签名 | 实现方式不同 |
| 默认参数对 IDE 的可见性 | `.d.ts` 里是可选参数并保留默认值注释 | 默认值主要服务脚本编译器/运行时，当前未见等价离线声明输出 | puerts 在 IDE 精度上实现更完整 |
| 模板/手写绑定补全 | `TemplateBindingGenerator` 输出 `Typing/cpp/index.d.ts`，Build.cs 同步手写 `Typing/` | 当前源码未见同等级离线声明产物覆盖 bind helper | Angelscript 没有实现同等级离线声明产物 |

### [维度 D11] 部署与打包：puerts 保留可读脚本文件作为运行时真源，Angelscript 预编译模式会主动牺牲热重载换缓存确定性

前面的 D11 已经覆盖 staging 和 cache；这一轮再往运行时后果看，差异会更清楚。puerts 的 `DefaultJSModuleLoader::Load()` 直接 `OpenRead` 磁盘文件，搜索顺序是 `.js/.mjs/.cjs/.json`，仅在定义 `WITH_V8_BYTECODE` 时额外接受 `.mbc/.cbc`。也就是说，在当前插件层，部署后的“真源”仍然是可读脚本文件；`RootPath` 还可以在 `DefaultPuerts.ini` 里配置，`PuertsModule` 为了兼容 `NonPak Game` 会在启动时手动再读一遍 ini。它更像“带着外部 JS 资产和 VM 二进制一起出包”。

Angelscript 的预编译模式则明显更封闭。启动时先加载 `Binds.Cache`，再按 `Shipping/Test/Development` 选取 `PrecompiledScript_*.Cache`，校验 `BuildIdentifier` 与 `StaticJIT` GUID；如果有效，`InitialCompile()` 会直接用 `PrecompiledData->GetModulesToCompile()` 代替预处理器读盘，并明确打日志禁用 hot reload。后续模块恢复分 `ApplyToModule_Stage1/2/3` 三段完成，最后 `ClearUnneededRuntimeData()` 主动裁掉 property/method table 等运行时数据来减内存。这里的部署产物已经不是源码文件本身，而是“可恢复的引擎内部状态快照”。

```
[puerts] Runtime Source of Truth
DefaultPuerts.ini
 -> RootPath
 -> DefaultJSModuleLoader.Search/Load
 -> raw .js/.mjs/.cjs/.json file
 -> optional .mbc/.cbc
 -> JS VM executes file content

[Angelscript] Precompiled Deployment
Binds.Cache + PrecompiledScript_<Config>.Cache
 -> Load + IsValidForCurrentBuild()
 -> GetModulesToCompile() instead of preprocessor
 -> ApplyToModule_Stage1 / Stage2 / Stage3
 -> hot reload disabled
 -> ClearUnneededRuntimeData()
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DefaultJSModuleLoader.cpp:67-83,92-139`、`Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:366-390,405-446`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DefaultJSModuleLoader.cpp
// 位置: 67-83, 92-139，部署态仍然按文件系统加载脚本
// ============================================================================
bool IsJs = Extension == TEXT("js") || Extension == TEXT("mjs") || Extension == TEXT("cjs") || Extension == TEXT("json");
return SearchModuleWithExtInDir(Dir, RequiredModule + ".js", Path, AbsolutePath) ||
       SearchModuleWithExtInDir(Dir, RequiredModule + ".mjs", Path, AbsolutePath) ||
       SearchModuleWithExtInDir(Dir, RequiredModule + ".cjs", Path, AbsolutePath) ||
#if defined(WITH_V8_BYTECODE)
       SearchModuleWithExtInDir(Dir, RequiredModule + ".mbc", Path, AbsolutePath) ||
       SearchModuleWithExtInDir(Dir, RequiredModule + ".cbc", Path, AbsolutePath) ||
#endif
       SearchModuleWithExtInDir(Dir, RequiredModule / "package.json", Path, AbsolutePath);

return SearchModuleInDir(FPaths::ProjectContentDir() / ScriptRoot, RequiredModule, Path, AbsolutePath);

IFileHandle* FileHandle = PlatformFile.OpenRead(*Path);
if (FileHandle)
{
    Content.AddUninitialized(len);
    const bool Success = FileHandle->Read(Content.GetData(), len); // ★ 直接读原始脚本内容
    return Success;
}

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp
// 位置: 366-390, 405-446，部署时 root path / debug / watch 等都由 ini 决定
// ============================================================================
if (GConfig->DoesSectionExist(SectionName, PuertsConfigIniPath))
{
    GConfig->GetBool(SectionName, TEXT("AutoModeEnable"), Settings.AutoModeEnable, PuertsConfigIniPath);
    GConfig->GetInt(SectionName, TEXT("DebugPort"), Settings.DebugPort, PuertsConfigIniPath);
    GConfig->GetBool(SectionName, TEXT("WatchDisable"), Settings.WatchDisable, PuertsConfigIniPath);
}

// ★ NonPak Game 打包下仍然显式再读 ini，避免模块加载先于配置生效
RegisterSettings();
if (Settings.AutoModeEnable)
{
    Enable();
}
WatchEnabled = !Settings.WatchDisable;
```

[2] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1466-1556,1582-1601,2046-2056,4283-4390`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp:1535-1625,2481-2489,2642-2690`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 1466-1556, 1582-1601, 2046-2056, 4283-4390
// ============================================================================
FAngelscriptBindDatabase::Get().Load(GetScriptRootDirectory() / TEXT("Binds.Cache"), bGeneratePrecompiledData);

Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript_Development.Cache");
if (!IFileManager::Get().FileExists(*Filename))
    Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript.Cache");

PrecompiledData = new FAngelscriptPrecompiledData(Engine);
PrecompiledData->Load(Filename);
if (!PrecompiledData->IsValidForCurrentBuild())
{
    delete PrecompiledData;
    PrecompiledData = nullptr;   // ★ build 配置不匹配直接丢弃 cache
}

const FStaticJITCompiledInfo* CompiledInfo = FStaticJITCompiledInfo::Get();
if (CompiledInfo != nullptr && CompiledInfo->PrecompiledDataGuid != PrecompiledData->DataGuid)
{
    FJITDatabase::Get().Clear(); // ★ transpiled C++ 与 cache GUID 不匹配也禁用
}

if (bGeneratePrecompiledData)
{
    PrecompiledData->InitFromActiveScript();
    PrecompiledData->Save(Filename);       // ★ 生成部署产物
}

if (PrecompiledData != nullptr)
{
    if (!bScriptDevelopmentMode && !bGeneratePrecompiledData)
        PrecompiledData->ClearUnneededRuntimeData(); // ★ 用完后裁掉多余运行时数据
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
    return;
}

if (Module->bLoadedPrecompiledCode)
{
    Module->PrecompiledData->ApplyToModule_Stage2(*PrecompiledData, ScriptModule);
    return;
}

if (Module->bLoadedPrecompiledCode)
{
    Module->PrecompiledData->ApplyToModule_Stage3(*PrecompiledData, ScriptModule);
    return;
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp
// 位置: 1535-1625, 2481-2489, 2642-2690，预编译数据是分阶段恢复的引擎内部状态
// ============================================================================
void FAngelscriptPrecompiledModule::ApplyToModule_Stage1(...)
{
    // ★ 第 1 阶段：先创建类、事件、枚举
    asCObjectType* Type = Classes[i].Create(Context, Module);
}

void FAngelscriptPrecompiledModule::ApplyToModule_Stage2(...)
{
    // ★ 第 2 阶段：补属性、导入函数、全局变量、全局函数
    Classes[i].ProcessProperties(Context, ClassTypes[i]);
    GlobalProperties[i] = GlobalVariables[i].Create(Context, Module);
    asCScriptFunction* Function = Functions[i].Create(Context, Module);
}

void FAngelscriptPrecompiledModule::ApplyToModule_Stage3(...)
{
    // ★ 第 3 阶段：处理函数体与后处理
    Functions[i].Process(Context, GlobalFunctions[i]);
}

bool FAngelscriptPrecompiledData::IsValidForCurrentBuild()
{
    return BuildIdentifier == GetCurrentBuildIdentifier() && BuildIdentifier != -1;
}

void FAngelscriptPrecompiledData::ClearUnneededRuntimeData()
{
    objType->propertyTable.EraseAll();
    objType->methodTable.EraseAll(); // ★ 明确回收不再需要的运行时索引
}
```

与 Angelscript 对比：

| 对比点 | puerts 新证据 | Angelscript 新证据 | 差距判断 |
| --- | --- | --- | --- |
| 运行时真源 | `DefaultJSModuleLoader` 直接读取磁盘脚本文件 | `Binds.Cache` + `PrecompiledScript*.Cache` 恢复内部状态 | 实现方式不同 |
| 部署一致性校验 | 当前插件层未见脚本内容 build/GUID 校验 | `IsValidForCurrentBuild()` + `PrecompiledDataGuid` + `CodeHash` 三层校验 | Angelscript 在部署一致性校验上实现质量更完整 |
| 部署态热重载关系 | 文件仍是运行时真源，保留 watcher/HMR 路径 | `bUsedPrecompiledDataForPreprocessor` 时显式禁用 hot reload | 实现方式不同 |
| 加密/签名 | 当前新增检视范围内仍未见 | 当前新增检视范围内仍未见 | 双方都没有实现 |

---

## 深化分析 (2026-04-08 18:52:02)

### [维度 D1] 插件架构与模块划分：`puerts` 的多后端解耦落到了绑定 ABI，而不只是 `Build.cs` 开关

前面的 D1 已经覆盖了模块数量和 `Build.cs` 里的 backend 切换；这一轮继续往下压，能看到 puerts 真正解耦的不是“模块名字”，而是“绑定代码最终看见的 ABI”。`PesapiAddonLoad.cpp` 维护了一整张 `pesapi_func_ptr` 表，把 `create/get/call/define_class/find_type_id` 等能力一次性注入 addon；`PesapiBackend.hpp` 再把 `DefineClass<T>()` builder 统一落到 `pesapi_define_class(...)`。结果是自动生成的 `*_Wrap.cpp` 只写 `DefineClass / Constructor / Property / Method / Register`，不直接依赖 `v8::FunctionTemplate`、QuickJS C API 或 Node.js 入口。

Angelscript 这边的抽象边界更靠上层。`Bind_FVector.cpp` 这类最终绑定文件仍然直接写 `FAngelscriptBinds::ValueClass/Method/Property`，底层再进入单一 `asIScriptEngine` 注册链；UHT 自动生成的 `AS_FunctionTable_*.cpp` 只补 `AddFunctionEntry(...)`，并没有把最终绑定语法再抽成一层后端无关 ABI。也就是说，puerts 的“多后端”在源码形态上表现为“绑定生成物对具体 VM 符号零感知”，而 Angelscript 的工程策略是“接受单引擎前提，换取更短的注册链路”。

```
[puerts] Binding ABI Boundary
generated *_Wrap.cpp
 -> DefineClass / Method / Property
 -> PesapiBackend::API
 -> pesapi_func_ptr table
 -> V8 / QuickJS / Node backend

[Angelscript] Binding ABI Boundary
Bind_*.cpp / AS_FunctionTable_*.cpp
 -> FAngelscriptBinds DSL
 -> RegisterObjectType / RegisterObjectMethod
 -> asIScriptEngine
 -> AngelScript only
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PesapiAddonLoad.cpp:31-67,99-107,143-149`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PesapiAddonLoad.cpp
// 位置: 31-67, 99-107, 143-149，把 backend 能力压成统一函数表传给 addon
// ============================================================================
static pesapi_func_ptr funcs[] = {
    (pesapi_func_ptr) &pesapi_create_function,
    (pesapi_func_ptr) &pesapi_create_class,
    (pesapi_func_ptr) &pesapi_call_function,
    (pesapi_func_ptr) &pesapi_define_class,
    (pesapi_func_ptr) &pesapi_get_class_data,
    (pesapi_func_ptr) &pesapi_trace_native_object_lifecycle,
    (pesapi_func_ptr) &pesapi_find_type_id
    // ★ 实际还有大量 create/get/set API，一并作为 backend-neutral ABI 暴露
};

auto Init = (const char* (*)(pesapi_func_ptr*))(uintptr_t)FPlatformProcess::GetDllExport(DllHandle, *EntryName);
const char* ModuleName = Init(nullptr);
GPesapiModuleName = ModuleName;
Init(funcs);                        // ★ addon 初始化时只拿到 pesapi 函数表，不碰具体 VM 私有 API
GPesapiModuleName = nullptr;

const char* ModuleName = Init(nullptr);
GPesapiModuleName = ModuleName;
Init(funcs);                        // ★ 非 iOS 路径同样走统一 ABI
GPesapiModuleName = nullptr;
```

[2] `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/PesapiBackend.hpp:89-96,187-215` 与 `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/Gen/FVector_Wrap.cpp:14-25,150`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Public/PesapiBackend.hpp
// 位置: 89-96, 187-215，builder 终点是 pesapi，而不是 V8/QuickJS 专有类型
// ============================================================================
struct API
{
    typedef pesapi_callback_info CallbackInfoType;
    typedef pesapi_env ContextType;
    typedef pesapi_value ValueType;         // ★ 绑定层只认识 pesapi 基础类型
    typedef void (*FunctionCallbackType)(pesapi_callback_info info);
};

template <typename T, typename CDB>
static void Register(FinalizeFuncType Finalize, const CDB& Cdb)
{
    auto properties = pesapi_alloc_property_descriptors(properties_count);
    pesapi_set_method_info(properties, pos++, func.Name, true, func.Callback, nullptr, nullptr);
    pesapi_set_property_info(properties, pos++, prop.Name, false, prop.Getter, prop.Setter, nullptr, nullptr, nullptr);
    pesapi_define_class(StaticTypeId<T>::get(), Cdb.superTypeId_, Cdb.className_,
        reinterpret_cast<InitializeFuncType>(Cdb.constructor_), finalize, properties_count, properties, nullptr);
}
```

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/Gen/FVector_Wrap.cpp
// 位置: 14-25, 150，生成物不直接出现 backend 私有符号
// ============================================================================
struct AutoRegisterForFVector
{
    AutoRegisterForFVector()
    {
        puerts::DefineClass<FVector>()
            .Constructor(CombineConstructors(MakeConstructor(FVector), MakeConstructor(FVector, float)))
            .Property("X", MakeProperty(&FVector::X))
            .Property("Y", MakeProperty(&FVector::Y))
            .Property("Z", MakeProperty(&FVector::Z))
            .Register();              // ★ 生成代码只描述“绑定内容”，不描述“具体 VM 怎么挂”
    }
};
```

[3] `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FVector.cpp:114-154` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:264-288,449-459`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FVector.cpp
// 位置: 114-154，最终绑定文件直接面向 Angelscript DSL
// ============================================================================
AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_FVector(FAngelscriptBinds::EOrder::Early, []
{
    auto FVector_ = FAngelscriptBinds::ValueClass<FVector>("FVector", Flags);
    FVector_.Constructor("void f(float64 X, float64 Y, float64 Z)", [](FVector* Address, double X, double Y, double Z)
    {
        new(Address) FVector(X, Y, Z);
    });
    FVector_.Property("float64 X", &FVector::X);
    FVector_.Property("float64 Y", &FVector::Y);
    FVector_.Property("float64 Z", &FVector::Z);
    FVector_.Method("FVector& opAssign(const FVector& Other)", METHODPR_TRIVIAL(FVector&, FVector, operator=, (const FVector&)));
});

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp
// 位置: 264-288, 449-459，最终仍直接注册进单一 asIScriptEngine
// ============================================================================
int TypeId = Manager.Engine->RegisterObjectType(ClassName.ToCString(), Size, Flags);

int FunctionId = Manager.Engine->RegisterObjectMethod(
    ClassName.ToCString(), Signature.ToCString(), asFUNCTION(Fun), asCALL_GENERIC, nullptr);

int FunctionId = Manager.Engine->RegisterObjectMethod(
    ClassName.ToCString(), Signature.ToCString(), Ptr, asCALL_THISCALL, *(asFunctionCaller*)&Caller, nullptr);
```

与 Angelscript 对比：

| 对比点 | puerts 新证据 | Angelscript 新证据 | 差距判断 |
| --- | --- | --- | --- |
| 绑定 ABI 边界 | `PesapiAddonLoad.cpp` 把 backend 能力压成 `pesapi_func_ptr` 表 | 最终绑定直接调用 `RegisterObjectType / RegisterObjectMethod` | Angelscript 当前没有实现同等级后端无关绑定 ABI |
| 生成绑定代码对 VM 的感知 | `FVector_Wrap.cpp` 只写 `DefineClass/Property/Method`，不出现 backend 私有符号 | `Bind_FVector.cpp` 直接编码 Angelscript DSL 与签名字符串 | 实现方式不同 |
| 扩展模块契约 | `Init(funcs)` + version check 形成统一 addon 契约 | 当前新增检视路径未见等价 cross-backend addon 契约 | puerts 在多后端扩展边界上实现更细 |

### [维度 D4] 热重载：`puerts` 主要替换模块源码并懒触发对象重绑，`Angelscript` 会显式重连类布局与默认值基线

前面的 D4 已经说明 puerts 走 Inspector HMR、Angelscript 走 reload 等级判定；这一轮继续看“旧实例和旧类在 reload 后怎么活下去”。puerts 的 `ReloadModule()` 本身很轻，只是把 `(ModuleName, JsSource)` 交给 `JsHotReload()`；`hot_reload.js` 的核心动作是对已经解析过的 `scriptId` 调 `Debugger.setScriptSource`。对 `UTypeScriptGeneratedClass` 来说，真正的修补发生在 `execLazyLoadCallJS()` 和 `NotifyReBind()`：首次调用先触发 `NotifyReBind(Class)`，随后遍历 `GeneratedObjects` 重新 `FindOrAdd()` JS wrapper，并按需 `MakeSureInject()` 子类。

Angelscript 的 soft reload 明显更像“类重连”而不是“模块 patch”。`PrepareSoftReload()` 会先造一个不带 defaults 的临时 CDO，作为后续 diff 的基线；`DoSoftReload()` 再 relink property offset、刷新 class flags、替换 `ScriptTypePtr`，并把旧 `UASFunction` 重新挂到新的 `ScriptFunction`，最后递归修正参数和返回值里的 `ScriptClass` 指针。`AngelscriptEngine.cpp` 还会在 `SoftReloadOnly` 下区分 `FullReloadSuggested` 与 `FullReloadRequired`，必要时保留旧模块继续运行并把 full reload 推迟到之后执行。

```
[puerts] Rebind After HMR
ReloadModule()
 -> JsHotReload()
 -> Debugger.setScriptSource(scriptId)
 -> execLazyLoadCallJS
 -> NotifyReBind()
 -> FindOrAdd wrapper for GeneratedObjects

[Angelscript] Soft Reload State Path
PerformSoftReload()
 -> PrepareSoftReload(CDONoDefaults)
 -> DoSoftReload()
 -> relink properties / flags / ScriptTypePtr
 -> remap old UASFunction to new ScriptFunction
 -> keep old code or queue full reload when needed
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js:68-90` 与 `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:1504-1537`

```js
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js
// 位置: 68-90，HMR 真正做的是替换某个 scriptId 的源码
// ============================================================================
await enableDebugger();
if (scriptId) {
    let orgSourceInfo = await sendCommand("Debugger.getScriptSource", {scriptId:"" + scriptId});
    source = ("(function (exports, require, module, __filename, __dirname) { " + source + "\n});");
    if (orgSourceInfo.scriptSource == source) {
        console.log(`source not changed, skip ${url}`);
        return;
    }
    let m = puerts.getModuleByUrl(url);
    puerts.emit('HMR.prepare', moduleName, m, url);
    let res = await sendCommand("Debugger.setScriptSource", {scriptId:"" + scriptId,scriptSource:source});
    puerts.emit('HMR.finish', moduleName, m, url);   // ★ 模块级 patch 结束
}
```

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp
// 位置: 1504-1537，C++ 侧 ReloadModule 本身并不处理类布局，只转交 JS 热更入口
// ============================================================================
void FJsEnvImpl::ReloadModule(FName ModuleName, const FString& JsSource)
{
    JsHotReload(ModuleName, JsSource);    // ★ 直接委托给 JS 侧热更脚本
}

void FJsEnvImpl::ReloadSource(const FString& Path, const PString& JsSource)
{
    auto LocalReloadJs = ReloadJs.Get(Isolate);
    v8::Handle<v8::Value> Args[] = {
        v8::Undefined(Isolate), FV8Utils::ToV8String(Isolate, Path), FV8Utils::ToV8String(Isolate, JsSource.c_str())};
    (void)(LocalReloadJs->Call(Context, v8::Undefined(Isolate), 3, Args));
    // ★ 这里没有 property relink / defaults diff，职责仍是“把新源码送进 VM”
}
```

[2] `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/TypeScriptGeneratedClass.cpp:57-73` 与 `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:2325-2365`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/TypeScriptGeneratedClass.cpp
// 位置: 57-73，首次命中时通过 lazy redirect 触发 rebind
// ============================================================================
DEFINE_FUNCTION(UTypeScriptGeneratedClass::execLazyLoadCallJS)
{
    auto Class = Cast<UTypeScriptGeneratedClass>(Function->GetOuterUClassUnchecked());
    auto PinedDynamicInvoker = Class->DynamicInvoker.Pin();
    if (PinedDynamicInvoker)
    {
        PinedDynamicInvoker->NotifyReBind(Class); // ★ 先重绑，再切回真正 execCallJS
    }
    Class->RestoreNativeFunc();
    execCallJS(Context, Stack, RESULT_PARAM);
}

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp
// 位置: 2325-2365，rebind 主要重新挂 wrapper 与注入 TypeScript 类
// ============================================================================
void FJsEnvImpl::NotifyReBind(UTypeScriptGeneratedClass* Class)
{
    MakeSureInject(Class, false, false);
    FinishInjection(Class);

    for (TWeakObjectPtr<UObject>& Iter : Class->GeneratedObjects)
    {
        auto Object = Iter.Get();
        if (!Object || ObjectMap.Find(Object))
            continue;
        __USE(FindOrAdd(Isolate, Context, Object->GetClass(), Object, true)); // ★ 重新把旧对象接回 JS wrapper

        if (ClassMayNeedReBind)
        {
            MakeSureInject(ClassMayNeedReBind, false, false); // ★ 子类按需二次注入
        }
    }
}
```

[3] `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2126-2279,4085-4260,4779-4804` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3936-3997`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 位置: 2126-2279, 4085-4260, 4779-4804，soft reload 显式维护类/函数/默认值状态
// ============================================================================
void FAngelscriptClassGenerator::PerformReload(bool bFullReload)
{
    for (auto& ModuleData : Modules)
    {
        if (ShouldFullReload(ClassData))
            CreateFullReloadClass(ModuleData, ClassData);
        else
            LinkSoftReloadClasses(ModuleData, ClassData); // ★ 先决定是 link soft reload 还是 full reload
    }

    PrepareSoftReload(ModuleData, ClassData);  // ★ 先准备 soft reload 基线
    DoSoftReload(ModuleData, ClassData);       // ★ 再真正重连旧类
}

void FAngelscriptClassGenerator::PrepareSoftReload(FModuleData& ModuleData, FClassData& ClassData)
{
    GConstructASObjectWithoutDefaults = true;
    UObject* CDONoDefaults = NewObject<UObject>(GetTransientPackage(), Class, ... , RF_ArchetypeObject);
    DestructScriptObject((asCScriptObject*)CDONoDefaults, Class, (asCObjectType*)ClassData.OldClass->ScriptType);
    ReinitializeScriptObject((asCScriptObject*)CDONoDefaults, Class, (asCObjectType*)ClassData.OldClass->ScriptType);
    ClassData.CDONoDefaults = CDONoDefaults;   // ★ 明确保存“不带 defaults”的基线
}

void FAngelscriptClassGenerator::DoSoftReload(FModuleData& ModuleData, FClassData& ClassData)
{
    ClassDesc->DefaultsCode = ClassData.OldClass->DefaultsCode; // ★ soft reload 保留旧 defaults code
    Property->Link(ArDummy);                                    // ★ 逐个 property relink offset
    Class->ScriptTypePtr = ScriptType;                          // ★ 更新脚本类型指针
    ((UASFunction*)FuncDesc->Function)->ScriptFunction = FuncDesc->ScriptFunction;
    SoftReloadFunction(OldFuncDesc->Function);                  // ★ 参数/返回类型里嵌套的脚本类型也递归修正
}

void FAngelscriptClassGenerator::SoftReloadType(FAngelscriptTypeUsage& Usage)
{
    if (Usage.ScriptClass != nullptr)
    {
        asITypeInfo** NewType = UpdatedScriptTypeMap.Find(Usage.ScriptClass);
        if (NewType != nullptr)
            Usage.ScriptClass = *NewType;
    }
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 3936-3997，reload 决策层会保留旧代码并区分 suggested / required
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
            ClassGenerator.PerformSoftReload();   // ★ 先软重载，full reload 延后
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
            bShouldSwapInModules = false;         // ★ 当前不能 full reload 时，保留旧代码继续跑
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

与 Angelscript 对比：

| 对比点 | puerts 新证据 | Angelscript 新证据 | 差距判断 |
| --- | --- | --- | --- |
| reload 落点 | `Debugger.setScriptSource` 直接替换模块源码 | `PrepareSoftReload/DoSoftReload` 直接重连类、属性、函数 | 实现方式不同 |
| 旧实例重绑方式 | `NotifyReBind()` 遍历 `GeneratedObjects` 重新挂 wrapper / 注入 | `DoSoftReload()` relink property offset、`ScriptTypePtr`、`UASFunction` | Angelscript 在类级状态保持上实现更完整 |
| 默认值基线 | 当前新增检视路径里未见等价 `CDONoDefaults` diff 基线 | 明确构造 `CDONoDefaults` 并保留旧 `DefaultsCode` | Angelscript 在默认值保持策略上实现更完整 |
| 失败恢复 | 当前 HMR 路径主要记录异常并终止该次 patch | `FullReloadSuggested/Required` 下可保留旧代码并排队 full reload | Angelscript 在失败恢复链路上实现更完整 |

### [维度 D8] 性能与优化：`puerts` 把成本花在双 GC 协调，`Angelscript` 把引用语义塞回 UE GC 与容器失效规则

前面的 D8 已经覆盖了 FastCall、direct bind 和 StaticJIT；这一轮补上“生命周期管理本身也是性能路径”的证据。puerts 运行时同时面对 UE GC 和 JS VM GC，所以它需要维护 `ObjectMap`、`FObjectRetainer`、weak persistent callback 和 `Finalize/OnEnter/OnExit` 四套钩子。`FObjectRetainer` 作为 `FGCObject` 把被 JS 持有的 `UObject` 继续挂到 UE 引用收集器；`FCppObjectMapper` 再按 `Ptr + TypeId` 缓存 JS wrapper，用 weak handle 决定何时 `Finalize`，并允许同一 native pointer 派生多个 type view。这个设计的收益是跨 VM 对象身份稳定，代价则是每次绑定/解绑都要做一次双向生命周期对账。

Angelscript 的路线几乎相反。脚本类绑定完成后直接 `AssembleReferenceTokenStream()`，随后 `DetectAngelscriptReferences()` 用 `UE::GC::FSchemaBuilder` 生成 `ReferenceSchema`，把脚本属性中的引用语义并进 UE GC；容器绑定如 `Bind_TArray.cpp` 在底层内存重排时，还会主动调用 `InvalidateReferencesToMemoryBlock(...)`，让运行中的 AngelScript 引用立刻失效。换句话说，puerts 优先解决“双运行时对象身份同步”，Angelscript 优先解决“脚本对象在 UE 自身 GC/容器语义里的原生化”。

```
[puerts] Dual-GC Lifetime Path
UObject* / native ptr
 -> ObjectMap + FObjectRetainer
 -> v8::Persistent / weak callback
 -> CppObjectMapper cache by TypeId
 -> Finalize / OnEnter / OnExit
 -> JS GC and UE GC handshake

[Angelscript] UE-GC-Integrated Path
script class / container
 -> AssembleReferenceTokenStream()
 -> DetectAngelscriptReferences()
 -> ReferenceSchema
 -> UE GC scan
 -> container mutation invalidates active refs
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/ObjectRetainer.cpp:22-58` 与 `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:1747-1782`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/ObjectRetainer.cpp
// 位置: 22-58，JS 持有的 UObject 继续通过 FGCObject 暴露给 UE GC
// ============================================================================
void FObjectRetainer::Retain(UObject* Object)
{
    if (!RetainedObjects.Contains(Object))
    {
        RetainedObjects.Add(Object);
    }
}

void FObjectRetainer::AddReferencedObjects(FReferenceCollector& Collector)
{
    Collector.AddReferencedObjects(RetainedObjects);   // ★ 让 UE GC 继续看到这些对象
}

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp
// 位置: 1747-1782，UEObject 与 JS wrapper 双向绑定后要显式 retain / release
// ============================================================================
DataTransfer::SetPointer(MainIsolate, JSObject, UEObject, 0);
ObjectMap.Emplace(UEObject, v8::UniquePersistent<v8::Value>(MainIsolate, JSObject));

void FJsEnvImpl::SetJsTakeRef(UObject* UEObject, FClassWrapper* ClassWrapper)
{
    UserObjectRetainer.Retain(UEObject);
    ObjectMap[UEObject].SetWeak<UClass>(
        Cast<UClass>(ClassWrapper->Struct.Get()), FClassWrapper::OnGarbageCollected, v8::WeakCallbackType::kInternalFields);
}

void FJsEnvImpl::UnBind(UClass* Class, UObject* UEObject, bool ResetPointer)
{
    ObjectMap.Remove(UEObject);
    UserObjectRetainer.Release(UEObject);  // ★ JS wrapper 解绑后再释放 UE GC 保留
}
```

[2] `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/CppObjectMapper.cpp:299-332,370-409` 与 `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/ObjectCacheNode.h:21-128`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/CppObjectMapper.cpp
// 位置: 299-332, 370-409，一个 native ptr 可以挂多个 TypeId 视图，并带 finalize/on-exit 钩子
// ============================================================================
auto Iter = CDataCache.find(Ptr);
FObjectCacheNode* CacheNodePtr;
if (Iter != CDataCache.end())
{
    auto Temp = Iter->second.Find(ClassDefinition->TypeId);
    CacheNodePtr = Temp ? Temp : Iter->second.Add(ClassDefinition->TypeId); // ★ 同一指针下按 TypeId 链式缓存
}

CacheNodePtr->Value.Reset(Isolate, JSObject);
if (!PassByPointer)
{
    CacheNodePtr->MustCallFinalize = true;
    CacheNodePtr->Value.SetWeak<JSClassDefinition>(
        ClassDefinition, CDataGarbageCollectedWithFree, v8::WeakCallbackType::kInternalFields);
}

if (ClassDefinition->OnEnter)
{
    CacheNodePtr->UserData = ClassDefinition->OnEnter(Ptr, ClassDefinition->Data, DataTransfer::GetIsolatePrivateData(Isolate));
}

if (ClassDefinition->OnExit)
{
    ClassDefinition->OnExit(
        Ptr, ClassDefinition->Data, DataTransfer::GetIsolatePrivateData(Isolate), Iter->second.UserData);
}
```

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/ObjectCacheNode.h
// 位置: 21-128，缓存结构本身就是“一个指针对应多脚本视图”的链表
// ============================================================================
class FObjectCacheNode
{
public:
    const void* TypeId;
    void* UserData;
    FObjectCacheNode* Next;
    v8::UniquePersistent<v8::Value> Value;
    bool MustCallFinalize;

    FObjectCacheNode* Find(const void* TypeId_)
    {
        if (TypeId_ == TypeId)
            return this;
        return Next ? Next->Find(TypeId_) : nullptr;
    }

    FObjectCacheNode* Add(const void* TypeId_)
    {
        Next = new FObjectCacheNode(TypeId_, Next);  // ★ 追加新的 TypeId 视图
        return Next;
    }
};
```

[3] `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h:34,77`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2833-2835,4852-4924`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp:43-85`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h
// 位置: 34, 77，脚本类本身持有 UE GC schema，并可扩展 AddReferencedObjects
// ============================================================================
UE::GC::FSchemaOwner ReferenceSchema;
virtual void RuntimeAddReferencedObjects(UObject* InThis, FReferenceCollector& Collector) {}

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 位置: 2833-2835, 4852-4924，类完成绑定后直接进入 UE GC token stream / schema 流程
// ============================================================================
NewClass->Bind();
NewClass->StaticLink(true);
NewClass->AssembleReferenceTokenStream();   // ★ 先让 UE 的 token stream 成型

UE::GC::FSchemaBuilder Schema(0);
Schema.Append(Class->ReferenceSchema.Get());
if (PropertyType.HasReferences())
{
    RefParams.AtOffset = PropertyOffset;
    PropertyType.EmitReferenceInfo(RefParams); // ★ 脚本属性引用被编进 UE::GC schema
}

UE::GC::FSchemaView View(Schema.Build(GetARO(Class)), UE::GC::EOrigin::Other);
Class->ReferenceSchema.Set(View);
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp
// 位置: 43-85，容器扩容/搬移时主动失效旧引用
// ============================================================================
static void InvalidateReferencesToArray(FScriptArray& Array, FArrayOperations* Ops)
{
    asCContext* Context = (asCContext*)asGetActiveContext();
    if (Context != nullptr)
    {
        Context->InvalidateReferencesToMemoryBlock(Array.GetData(), Array.GetAllocatedSize(Ops->NumBytesPerElement));
        // ★ 容器底层内存变化时，脚本侧引用立即失效，避免悬挂引用继续读旧地址
    }
}

void FAngelscriptArrayType::EmitReferenceInfo(const FAngelscriptTypeUsage& Usage, FGCReferenceParams& Params) const
{
    if (Usage.SubTypes[0].Type->IsObjectPointer())
    {
        Params.Schema->Add(UE::GC::DeclareMember(Params.Names.Top(), Params.AtOffset, UE::GC::EMemberType::ReferenceArray, InnerSchema.Build()));
    }
    else
    {
        Usage.SubTypes[0].EmitReferenceInfo(InnerParams);
        Params.Schema->Add(UE::GC::DeclareMember(Params.Names.Top(), Params.AtOffset, UE::GC::EMemberType::StructArray, InnerSchema.Build()));
    }
}
```

与 Angelscript 对比：

| 对比点 | puerts 新证据 | Angelscript 新证据 | 差距判断 |
| --- | --- | --- | --- |
| 生命周期模型 | `FObjectRetainer` + `ObjectMap` + V8 weak handle + `Finalize/OnExit` 四层协调 | `ReferenceSchema` + `AssembleReferenceTokenStream()` 直接并入 UE GC | 实现方式不同 |
| 对象身份缓存 | `FObjectCacheNode` 允许同一 native ptr 挂多个 `TypeId` 视图 | 当前新增检视路径主要把脚本引用收束到类/属性 schema，而非跨 VM pointer cache | 实现方式不同 |
| 容器引用失效保护 | 当前新增检视路径未见等价 `InvalidateReferencesToMemoryBlock(...)` 钩子 | `Bind_TArray.cpp` 明确在容器搬移时失效活动引用 | Angelscript 在容器引用失效保护上实现更完整 |

---

## 深化分析 (2026-04-08 19:01:08)

### [维度 D1] 插件架构与模块划分：`puerts` 把编辑器工具链直接建在运行时 VM 之上，`Angelscript` 则让编辑器工具链留在原生 C++ 层

前面的 D1 已经说明 puerts 的 backend 抽象会落到 `pesapi` ABI；这一轮补的是更上层的 build graph。`Puerts.Build.cs` 本身很薄，只把 `Puerts` runtime 建在 `JsEnv` 之上；而 `PuertsEditor.Build.cs` 则同时依赖 `JsEnv`、`Puerts`、`DirectoryWatcher`、`KismetCompiler`、`BlueprintGraph`、`AssetTools`。`FPuertsEditorModule::OnPostEngineInit()` 启动时不只注册菜单，而是先给 `UTypeScriptBlueprint` 挂自定义 `FKismetCompilerContext`，再新建一份共享 `FJsEnv` 去跑 `PuertsEditor/CodeAnalyze`。从这些入口可以推断，puerts 的编辑器工具链不是“附着在 VM 旁边”的原生插件，而是“复用同一套脚本宿主能力去执行工具本身”。

Angelscript 的 build graph 则更像传统 UE 编辑器模块。`AngelscriptEditor.Build.cs` 依赖 `AngelscriptRuntime` 与一批 Editor 模块，但当前检视到的工具能力仍主要用 C++ 直接完成：菜单里明确把 `GenerateNativeBinds()` 标成 legacy debug path，源码导航则由 `FSourceCodeNavigation` 直接 `code --goto` 打开脚本文件。也就是说，Angelscript 的运行时和编辑器层有共享类型系统，但编辑器工具本身并不“自托管”在脚本 VM 里。

```
[puerts] Editor Tool Hosting
PuertsEditor.Build.cs
 -> JsEnv + Puerts + KismetCompiler + AssetTools
 -> RegisterCompilerForBP(UTypeScriptBlueprint)
 -> shared FJsEnv
 -> Start("PuertsEditor/CodeAnalyze")
 -> tool logic runs inside JS VM

[Angelscript] Editor Tool Hosting
AngelscriptEditor.Build.cs
 -> AngelscriptRuntime + UE editor modules
 -> native C++ menu / watcher / source navigation
 -> UHT exporter for bind data
 -> tool logic stays in native editor module
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Source/Puerts/Puerts.Build.cs:16-25`、`Reference/puerts/unreal/Puerts/Source/PuertsEditor/PuertsEditor.Build.cs:16-45`、`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:110-150`、`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/TypeScriptCompilerContext.cpp:19-40`

```csharp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/Puerts/Puerts.Build.cs
// 位置: 16-25，runtime 模块本身很薄，核心能力来自 JsEnv
// ============================================================================
PublicDependencyModuleNames.AddRange(
    new string[]
    {
        "Core", "CoreUObject", "Engine", "InputCore", "Serialization", "OpenSSL","UMG","JsEnv",
    }
);

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/PuertsEditor/PuertsEditor.Build.cs
// 位置: 16-45，editor 模块把 VM、编译器和资产工具全部拉到同一层
// ============================================================================
PublicDependencyModuleNames.AddRange(
    new string[]
    {
        "Core", "CoreUObject", "UMG", "UnrealEd", "LevelEditor", "Engine",
        "Slate", "SlateCore", "EditorStyle", "InputCore", "Projects",
        "JsEnv", "Puerts", "DirectoryWatcher", "AssetRegistry",
        "KismetCompiler", "BlueprintGraph", "AssetTools"
    }
);
```

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp
// 位置: 110-150，editor 启动时直接拉起脚本 VM 跑分析器
// ============================================================================
TSharedPtr<FKismetCompilerContext> MakeCompiler(
    UBlueprint* InBlueprint, FCompilerResultsLog& InMessageLog, const FKismetCompilerOptions& InCompileOptions)
{
    return MakeShared<FTypeScriptCompilerContext>(CastChecked<UTypeScriptBlueprint>(InBlueprint), InMessageLog, InCompileOptions);
}

if (Enabled)
{
    FKismetCompilerContext::RegisterCompilerForBP(UTypeScriptBlueprint::StaticClass(), &MakeCompiler);

    JsEnv = MakeShared<PUERTS_NAMESPACE::FJsEnv>(
        std::make_shared<PUERTS_NAMESPACE::DefaultJSModuleLoader>(TEXT("JavaScript")),
        std::make_shared<PUERTS_NAMESPACE::FDefaultLogger>(), -1, ...);

    JsEnv->Start("PuertsEditor/CodeAnalyze");   // ★ 工具逻辑本身跑在 VM 里
}

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/TypeScriptCompilerContext.cpp
// 位置: 19-40，自定义 Blueprint 编译器直接产出 UTypeScriptGeneratedClass
// ============================================================================
if (NewClass == NULL)
{
    NewClass =
        NewObject<UTypeScriptGeneratedClass>(Blueprint->GetOutermost(), FName(*NewClassName), RF_Public | RF_Transactional);
}
else
{
    NewClass->ClassGeneratedBy = Blueprint;
    FBlueprintCompileReinstancer::Create(NewClass); // ★ 已有类重入编译时仍走 TS generated class
}
```

[2] `Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs:12-40`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:726-733`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp:95-115`

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs
// 位置: 12-40，editor 依赖的是 runtime 和 UE 编辑器模块，而不是第二套脚本宿主
// ============================================================================
PublicDependencyModuleNames.AddRange(new string[]
{
    "Core",
    "CoreUObject",
    "Engine",
    "UnrealEd",
    "EditorSubsystem",
    "AngelscriptRuntime",
    "BlueprintGraph",
    "Kismet",
    "DirectoryWatcher",
    "Slate",
    "SlateCore",
    "AssetTools",
});
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 位置: 726-733，菜单直接说明 editor-side generator 只是 legacy debug 路径
// ============================================================================
BindSection.AddMenuEntry
(
    "ASGenerateBindings",
    NSLOCTEXT("Angelscript", "GenerateBind.Label", "Legacy Native Bind Generator (Debug Only)"),
    NSLOCTEXT("Angelscript", "GenerateBind.ToolTip",
        "Legacy editor-side generator retained only for debugging old FunctionCallers output. The UHT-based AngelscriptUHTTool pipeline is the primary path."),
    FSourceCodeNavigation::GetOpenSourceCodeIDEIcon(),
    GenerateAction
);

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp
// 位置: 95-115，导航直接在 native editor 层调用外部 IDE
// ============================================================================
void OpenFile(const FString& Path, int LineNo = -1)
{
    if (LineNo != -1)
        FPlatformMisc::OsExecute(nullptr, TEXT("code"), *FString::Printf(TEXT("--goto \"%s:%d\""), *Path, LineNo));
    else
        FPlatformMisc::OsExecute(nullptr, TEXT("code"), *FString::Printf(TEXT("\"%s\""), *Path));
}
```

与 Angelscript 对比：

| 对比点 | puerts 新证据 | Angelscript 新证据 | 差距判断 |
| --- | --- | --- | --- |
| 编辑器工具宿主 | `PuertsEditor` 启动共享 `FJsEnv` 执行 `CodeAnalyze` | `AngelscriptEditor` 工具逻辑留在 native module | 实现方式不同 |
| Blueprint 编译扩展点 | `RegisterCompilerForBP(UTypeScriptBlueprint)` + `UTypeScriptGeneratedClass` | 当前检视入口主要是 `Kismet`/资产工具/源码导航 | 实现方式不同 |
| build graph 复用方式 | 运行时 `JsEnv` 被 editor 工具链直接复用 | editor 通过 `AngelscriptRuntime` 共享类型系统，但工具不自托管在脚本 VM | puerts 在工具/运行时代码复用上实现更激进 |

### [维度 D6] 代码生成与 IDE 支持：`puerts` 的 IDE 管线本质上是一个带 Blueprint 回写的增量状态机

前面的 D6 已经讲过 `.d.ts`、默认参数和 `DebugDatabase`；这一轮真正新的点是，puerts 的 IDE 管线并不是“一次生成，IDE 只读消费”。`PuertsEditor/package.json` 把 TypeScript 版本钉在 `4.7.4`；`CodeAnalyze.js` 启动后会创建自定义 `ts.System` 和 `ts.LanguageServiceHost`，用 MD5 维护 `fileVersions`，在 `UE` 文件读取失败时重建 `LanguageService`，并把状态持久化到 `ts_file_versions_info.json`。更关键的是，它不是只报诊断，还会按继承关系对待刷新的 Blueprint 任务做拓扑排序，调用 `PEBlueprintAsset` 检查/更新 Blueprint 资产。再往下一层，`FTypeScriptCompilerContext` 保证这些 Blueprint 最终编译成 `UTypeScriptGeneratedClass`。这说明 puerts 的 IDE 支持是“类型服务 + 资产同步 + Blueprint 编译”的闭环。

Angelscript 当前检视到的 IDE 支持则是另一条闭环。`AngelscriptFunctionTableExporter` 在 UHT 阶段统计 `BlueprintCallable/Pure`，生成 C++ 函数表与 skipped CSV；`FAngelscriptSourceCodeNavigation` 负责从 UE 反射对象跳回脚本文件行号。这里也有自动化和 editor 集成，但“主产物”是绑定生成物和导航入口，而不是一个常驻的脚本语言服务。因此差距不应写成“Angelscript 没 IDE”，而应写成“IDE 支持重心不同”。

```
[puerts] IDE State Machine
tsconfig / Typing / source files
 -> file MD5 ledger
 -> ts.createLanguageService()
 -> changed file analysis
 -> topological Blueprint refresh jobs
 -> PEBlueprintAsset check/update
 -> UTypeScriptGeneratedClass compile

[Angelscript] IDE Support Loop
UHT session
 -> AngelscriptFunctionTableExporter
 -> AS_FunctionTable_*.cpp + skipped CSV
 -> runtime reflection object
 -> FSourceCodeNavigation
 -> open script file / line
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Content/JavaScript/PuertsEditor/package.json:1-8`、`Reference/puerts/unreal/Puerts/Content/JavaScript/PuertsEditor/CodeAnalyze.js:208-280,314-387`、`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/TypeScriptCompilerContext.cpp:19-40`

```jsonc
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Content/JavaScript/PuertsEditor/package.json
// 位置: 1-8，编辑器分析器把 TS 版本固定在插件内
// ============================================================================
{
  "name": "PuertsEditor",
  "version": "1.0.0",
  "dependencies": {
    "typescript": "4.7.4" // ★ 语言服务版本跟插件一起发布
  }
}
```

```javascript
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Content/JavaScript/PuertsEditor/CodeAnalyze.js
// 位置: 208-280, 314-387，IDE 管线不是一次性导出，而是带持久状态的增量分析器
// ============================================================================
const versionsFilePath = tsi.getDirectoryPath(configFilePath) + "/ts_file_versions_info.json";
const fileVersions = {};
fileNames.forEach(fileName => {
    fileVersions[fileName] = { version: UE.FileSystemOperation.FileMD5Hash(fileName), processed: false, isBP: false };
});

const servicesHost = {
    getScriptFileNames: () => fileNames,
    getScriptVersion: fileName => { ... },
    getScriptSnapshot: fileName => { ... },
    getCompilationSettings: () => options,
};
let service = ts.createLanguageService(servicesHost, ts.createDocumentRegistry()); // ★ 常驻语言服务

function getProgramFromService() {
    while (true) {
        try {
            return service.getProgram();
        } catch (e) {
        }
        service = ts.createLanguageService(servicesHost, ts.createDocumentRegistry()); // ★ 失败后重建
    }
}

function refreshBlueprints() {
    if (pendingBlueprintRefleshJobs.length > 0) {
        pendingBlueprintRefleshJobs = topologicalSort(pendingBlueprintRefleshJobs);
        pendingBlueprintRefleshJobs.forEach(job => {
            job.op(); // ★ 按继承顺序刷新 Blueprint 资产
        });
        pendingBlueprintRefleshJobs = [];
    }
}

if (!versionsFileExisted || restoredFileVersions[fileName].version != fileVersions[fileName].version || !restoredFileVersions[fileName].processed || !BPExisted) {
    onSourceFileAddOrChange(fileName, false, program, false);
    changed = true;
}

if (changed) {
    UE.FileSystemOperation.WriteFile(versionsFilePath, JSON.stringify(fileVersions, null, 4)); // ★ 版本账本持久化
}
```

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/TypeScriptCompilerContext.cpp
// 位置: 19-40，Blueprint 编译阶段继续沿用 TS generated class
// ============================================================================
if (NewClass == NULL)
{
    NewClass =
        NewObject<UTypeScriptGeneratedClass>(Blueprint->GetOutermost(), FName(*NewClassName), RF_Public | RF_Transactional);
}
else
{
    NewClass->ClassGeneratedBy = Blueprint;
    FBlueprintCompileReinstancer::Create(NewClass); // ★ 增量刷新后的 Blueprint 仍回到同一类体系
}
```

[2] `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:21-53,65-96`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp:34-45`

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs
// 位置: 21-53, 65-96，自动化主产物是函数表与 skipped 统计
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
        CountBlueprintCallableFunctions(...); // ★ 统计/重建 BlueprintCallable 签名
    }
    WriteSkippedEntriesCsv(factory, skippedEntries);
    WriteSkippedReasonSummaryCsv(factory, skippedEntries); // ★ 额外输出 skipped 报表
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp
// 位置: 34-45，IDE 交互核心是从反射对象跳到脚本文件行号
// ============================================================================
virtual bool NavigateToFunction(const UFunction* InFunction) override
{
    auto* ASFunc = Cast<const UASFunction>(InFunction);
    if (ASFunc == nullptr)
        return false;

    FString Path = ASFunc->GetSourceFilePath();
    if (Path.Len() == 0)
        return false;

    OpenFile(Path, ASFunc->GetSourceLineNumber()); // ★ 导航回脚本源文件
    return true;
};
```

与 Angelscript 对比：

| 对比点 | puerts 新证据 | Angelscript 新证据 | 差距判断 |
| --- | --- | --- | --- |
| IDE 状态持久化 | `ts_file_versions_info.json` + MD5 版本账本 | UHT exporter 主要输出函数表与 skipped CSV | 实现方式不同 |
| IDE 主引擎 | 插件内固定 `typescript 4.7.4`，常驻 `ts.createLanguageService()` | 当前检视入口主要是 UHT 导出器 + `FSourceCodeNavigation` | 当前检视范围内 Angelscript 未见同等级内嵌语言服务 |
| 与 Blueprint 资产的闭环 | `CodeAnalyze.js` 拓扑刷新 Blueprint，`FTypeScriptCompilerContext` 回写 `UTypeScriptGeneratedClass` | 自动化主链更偏绑定导出与跳定义 | puerts 在“IDE 分析直连资产刷新”上实现更完整 |

### [维度 D11] 部署与打包：`puerts` 的部署配置面围绕 VM 和脚本根，`Angelscript` 的部署配置面围绕脚本包与 editor-only 边界

前面的 D11 已经说明了 loose source 与 precompiled cache 的差异；这一轮补的是“配置面”本身。`UPuertsSetting` 暴露的部署相关选项主要是 `RootPath`、`DebugEnable`、`DebugPort`、`NumberOfJsEnv`、`WatchDisable`；`DefaultJSModuleLoader` 再按这个根目录去 `ProjectContentDir()/RootPath` 和默认 `JavaScript` 目录里找 `.js/.mjs/.cjs/.json`；`JsEnv.Build.cs` 会把 backend DLL 作为 `StagedFileType.NonUFS` 直接 stage 到输出目录。也就是说，在当前 UE 插件层里，puerts 的部署旋钮主要服务“选哪类 VM / 从哪找脚本 / 把哪些 backend binary 带出去”，而不是“哪些脚本包在 cooked runtime 可见、哪些只在 editor 可见”。

Angelscript 这边则把“脚本内容边界”做成了一等配置。`DiscoverScriptRoots()` 会枚举项目根和启用插件的 `Script/` 根目录；`UAngelscriptSettings` 暴露 `AdditionalEditorOnlyScriptPackageNames`；`IsEditorOnlyClass()` 在运行时会同时检查 package flags、这组配置名，以及 header path 是否落在 editor 目录。再叠加 `PrecompiledScript_Development/Test/Shipping.Cache` 的 config 分流，Angelscript 的部署配置更像“先划定脚本包边界，再决定是否预编译”。这一点和 puerts 的 VM/backend 导向部署面是两个维度。

```
[puerts] Deployment Surface
DefaultPuerts.ini / UPuertsSetting
 -> RootPath / Debug / EnvCount / WatchDisable
 -> DefaultJSModuleLoader search raw files
 -> stage backend dll as NonUFS
 -> runtime decides VM + script root

[Angelscript] Deployment Surface
UAngelscriptSettings + package flags
 -> project Script root + plugin Script roots
 -> AdditionalEditorOnlyScriptPackageNames
 -> IsEditorOnlyClass()
 -> PrecompiledScript_<Config>.Cache
 -> runtime decides package visibility + cache selection
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsSetting.h:20-57`、`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DefaultJSModuleLoader.cpp:92-120`、`Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs:360-367`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsSetting.h
// 位置: 20-57，部署相关配置集中在脚本根和 VM 调试/实例数
// ============================================================================
UPROPERTY(config, EditAnywhere, Category = "Default JavaScript Environment",
    meta = (defaultValue = "JavaScript", Tooltip = "JavaScript Source Code Root Path", DisplayName = "JavaScript Source Root"))
FString RootPath = "JavaScript";

bool DebugEnable = false;
int32 DebugPort = 8080;
int32 NumberOfJsEnv = 1;
bool WatchDisable = false; // ★ 当前暴露的是 VM/调试/脚本根相关旋钮
```

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DefaultJSModuleLoader.cpp
// 位置: 92-120，loader 只围绕脚本根和原始文件扩展名做搜索
// ============================================================================
if (SearchModuleInDir(RequiredDir, RequiredModule, Path, AbsolutePath))
{
    return true;
}

return SearchModuleInDir(FPaths::ProjectContentDir() / ScriptRoot, RequiredModule, Path, AbsolutePath) ||
       (ScriptRoot != TEXT("JavaScript") &&
           SearchModuleInDir(FPaths::ProjectContentDir() / TEXT("JavaScript"), RequiredModule, Path, AbsolutePath));
// ★ 没有“editor-only script package”这一层显式概念，核心是根目录与模块路径
```

```csharp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs
// 位置: 360-367，backend 动态库直接以 NonUFS 方式跟随产物输出
// ============================================================================
void AddRuntimeDependencies(string[] DllNames, string LibraryPath, bool Delay)
{
    foreach (var DllName in DllNames)
    {
        var DllPath = Path.Combine(LibraryPath, DllName);
        var DestDllPath = Path.Combine("$(BinaryOutputDir)", DllName);
        RuntimeDependencies.Add(DestDllPath, DllPath, StagedFileType.NonUFS); // ★ 直接 stage loose binary
    }
}
```

[2] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h:197-201`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1326-1363,1519-1529`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:901-930`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h
// 位置: 197-201，把 editor-only script package 显式暴露成配置
// ============================================================================
/**
 * Script package names (/Script/ModuleName) that should be considered editor-only for the purposes of checking for incorrect usage.
 */
UPROPERTY(Config)
TArray<FName> AdditionalEditorOnlyScriptPackageNames;
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 1326-1363, 1519-1529，部署时先划脚本根，再按 build config 选缓存
// ============================================================================
FString RootPath = Dependencies.ConvertRelativePathToFull(Dependencies.GetProjectDir() / TEXT("Script"));

for (const FString& PluginScriptRoot : Dependencies.GetEnabledPluginScriptRoots())
{
    const FString ScriptPath = Dependencies.ConvertRelativePathToFull(PluginScriptRoot);
    if (Dependencies.DirectoryExists(ScriptPath) && ScriptPath != RootPath)
    {
        DiscoveredRootPaths.Add(ScriptPath); // ★ 插件脚本根也进入部署面
    }
}

#if UE_BUILD_SHIPPING
Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript_Shipping.Cache");
#elif UE_BUILD_TEST
Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript_Test.Cache");
#elif UE_BUILD_DEVELOPMENT
Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript_Development.Cache");
#endif

if (!IFileManager::Get().FileExists(*Filename))
    Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript.Cache");
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp
// 位置: 901-930，editor-only 判定是 package/config/path 三重检查
// ============================================================================
if (Class->GetOutermost()->HasAnyPackageFlags(PKG_EditorOnly | PKG_UncookedOnly))
{
    bIsEditor = true;
}

if (!bIsEditor && FAngelscriptEngine::Get().ConfigSettings->AdditionalEditorOnlyScriptPackageNames.Contains(Class->GetOutermost()->GetFName()))
{
    bIsEditor = true;
}

if (!bIsEditor && FSourceCodeNavigation::FindClassHeaderPath(Class, ClassHeaderPath))
{
    if (ClassHeaderPath.Contains(TEXT("/Source/Editor/"))
        || ClassHeaderPath.Contains(TEXT("\\Source\\Editor\\"))
        || ClassHeaderPath.Contains(TEXT("/Plugins/Editor/"))
        || ClassHeaderPath.Contains(TEXT("\\Plugins\\Editor\\")))
    {
        bIsEditor = true; // ★ 路径启发式继续收紧 cooked 可见面
    }
}
```

与 Angelscript 对比：

| 对比点 | puerts 新证据 | Angelscript 新证据 | 差距判断 |
| --- | --- | --- | --- |
| 部署配置重心 | `RootPath / Debug / NumberOfJsEnv / WatchDisable` + `NonUFS` backend stage | 脚本根发现 + `AdditionalEditorOnlyScriptPackageNames` + config cache 选择 | 实现方式不同 |
| editor-only 脚本边界 | 当前检视到的 `UPuertsSetting` 未见等价脚本包级 editor-only 配置 | `IsEditorOnlyClass()` 明确检查 package/config/path | puerts 没有实现同等级脚本包边界控制 |
| 多插件脚本面 | loader 主要围绕单个 `ScriptRoot` 与默认 `JavaScript` 回退 | `DiscoverScriptRoots()` 显式枚举项目与插件 `Script/` 根目录 | Angelscript 在多脚本根部署建模上实现更完整 |

---

## 深化分析 (2026-04-08 19:11:22)

### [维度 D2] 反射绑定机制：`puerts` 的“自动绑定”落在惰性 translator 工厂，`Angelscript` 的自动化更像受限 reflective fallback

前文已经说明 puerts 走 runtime binding；这一轮补的是“自动”到底自动到哪一层。新的证据表明，puerts 并不会先为所有 `UFunction/UProperty` 生成固定胶水，而是在 `MakeSureInject` 命中 JS override 时，才为对应 `UFunction` 建立 `FFunctionTranslator`。这个 translator 再按 `CPF_Parm` 逐个创建 `FPropertyTranslator`，最终依赖 `JSClassRegister` 里存下来的 `MethodInfos/PropertyInfos` 与 `LoadClassByID()` 做按需类定义装配。也就是说，puerts 的自动化主路径是“运行时惰性建模”。

Angelscript 的自动化边界更窄。常规能力仍然要落回 `RegisterObjectMethod` / `RegisterGlobalFunction` 这样的直接注册；`BlueprintCallableReflectiveFallback` 只是给一部分 `BlueprintCallable/Pure` 函数兜底，而且显式拒绝 `CustomThunk` 与超过 16 个非返回参数的函数，执行时本质上还是分配参数缓冲后 `ProcessEvent`。因此两边不是“有没有自动化”的差别，而是“runtime translator 主路径”与“手写 direct bind 主路径 + 反射兜底支线”的工程分工不同。

```
[puerts] Lazy Reflection Binding
JS override detected
 -> TsFunctionMap caches FFunctionTranslator
 -> FFunctionTranslator::Init() walks CPF_Parm
 -> FPropertyTranslator::Create() per property
 -> JSClassRegister::LoadClassByID() resolves class on demand

[Angelscript] Direct Bind + Reflective Fallback
Bind_*.cpp / FAngelscriptBinds
 -> RegisterObjectMethod / RegisterGlobalFunction
 -> optional BlueprintCallableReflectiveFallback
    -> reject CustomThunk / >16 args
    -> copy parms
    -> ProcessEvent()
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:1364-1375`、`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp:93-136`、`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:159-180`、`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JSClassRegister.cpp:85-100,171-213`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp
// 位置: 1364-1375，只有在发现 JS override 时才惰性创建 translator
// ============================================================================
auto FuncInfo = TsFunctionMap.Find(Function);
if (!FuncInfo)
{
    TsFunctionMap.Add(
        Function, {v8::UniquePersistent<v8::Function>(
                       Isolate, v8::Local<v8::Function>::Cast(MaybeValue.ToLocalChecked())),
                   std::make_unique<FFunctionTranslator>(Function, false)}); // ★ 命中 override 才建 translator
}
else
{
    FuncInfo->FunctionTranslator->Init(Function, false); // ★ 重绑时复用 translator 壳体，刷新参数模型
}

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp
// 位置: 93-136，translator 再按 UFunction 参数表逐参展开
// ============================================================================
ParamsBufferSize = InFunction->PropertiesSize > InFunction->ParmsSize ? InFunction->PropertiesSize : InFunction->ParmsSize;
for (TFieldIterator<PropertyMacro> It(InFunction); It && (It->PropertyFlags & CPF_Parm); ++It)
{
    PropertyMacro* Property = *It;
    if (Property->HasAnyPropertyFlags(CPF_ReturnParm))
    {
        Return = FPropertyTranslator::Create(Property);    // ★ 返回值也走统一 property translator
    }
    else
    {
        Arguments.push_back(FPropertyTranslator::Create(Property)); // ★ 参数 translator 运行时生成
    }
}

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp
// 位置: 159-180，属性访问器同样按 Property 类型动态挂接
// ============================================================================
auto Self = v8::External::New(Isolate, this);
auto GetterTemplate = v8::FunctionTemplate::New(Isolate, Getter, Self);
auto SetterTemplate = v8::FunctionTemplate::New(Isolate, Setter, Self);
Template->PrototypeTemplate()->SetAccessorProperty(
    FV8Utils::InternalString(Isolate, Property->GetName()),
    GetterTemplate,
    SetterTemplate); // ★ 每个 Property 自带 getter/setter translator

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JSClassRegister.cpp
// 位置: 85-100, 171-213，类信息与 reflection info 也按需加载
// ============================================================================
auto clsDef = FindClassByID(TypeId);
if (!clsDef && ClassNotFoundCallback)
{
    if (!ClassNotFoundCallback(TypeId))
    {
        return nullptr;
    }
    clsDef = FindClassByID(TypeId); // ★ 缺类时再触发回调补注册
}

ClassDef->ConstructorInfos = PropertyInfoDuplicate(const_cast<NamedFunctionInfo*>(ConstructorInfos));
ClassDef->MethodInfos = PropertyInfoDuplicate(const_cast<NamedFunctionInfo*>(MethodInfos));
ClassDef->FunctionInfos = PropertyInfoDuplicate(const_cast<NamedFunctionInfo*>(FunctionInfos));
ClassDef->PropertyInfos = PropertyInfoDuplicate(const_cast<NamedPropertyInfo*>(PropertyInfos));
ClassDef->VariableInfos = PropertyInfoDuplicate(const_cast<NamedPropertyInfo*>(VariableInfos));
SetReflectoinInfo(ClassDef->Methods, ClassDef->MethodInfos); // ★ 再把唯一匹配的方法补上 reflection info
```

[2] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:285-296,588-608`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp:272-288,290-370`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp
// 位置: 285-296, 588-608，常规路径仍然是 direct bind
// ============================================================================
int FunctionId = Manager.Engine->RegisterObjectMethod(
    ClassName.ToCString(), Signature.ToCString(), asFUNCTION(Fun), asCALL_GENERIC, nullptr);

int FunctionId = Manager.Engine->RegisterGlobalFunction(
    Signature.ToCString(), Function, asCALL_CDECL, *(asFunctionCaller*)&Caller, nullptr);
// ★ 正常能力面依旧围绕 Register* API 直接注册

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp
// 位置: 272-288, 290-370，reflective fallback 是受限兜底，不是主绑定层
// ============================================================================
if (Function->HasMetaData(NAME_BlueprintCallableReflectiveFallback_CustomThunk))
{
    return EAngelscriptReflectiveFallbackEligibility::RejectedCustomThunk;
}

if (GetNonReturnParameterCount(Function) > BlueprintCallableReflectiveFallbackMaxArgs)
{
    return EAngelscriptReflectiveFallbackEligibility::RejectedTooManyArguments; // ★ 超过 16 个参数直接拒绝
}

uint8* ParameterBuffer = static_cast<uint8*>(FMemory_Alloca(Function->ParmsSize));
InitializeParameterBuffer(Function, ParameterBuffer);

for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
{
    FProperty* Property = *It;
    void* Destination = Property->ContainerPtrToValuePtr<void>(ParameterBuffer);
    void* SourceAddress = ResolveScriptArgumentAddress(Property, ScriptArgumentAddress);
    Property->CopySingleValue(Destination, SourceAddress); // ★ 这里只是把脚本参数复制回 UFunction 参数缓冲
}

TargetObject->ProcessEvent(Function, ParameterBuffer); // ★ 最终仍借 UE 反射调用
```

与 Angelscript 对比：

| 对比点 | puerts 新证据 | Angelscript 新证据 | 差距判断 |
| --- | --- | --- | --- |
| 自动化主路径 | `TsFunctionMap` 命中后惰性创建 `FFunctionTranslator`，再逐参建 `FPropertyTranslator` | 常规路径仍是 `RegisterObjectMethod/RegisterGlobalFunction` | 实现方式不同 |
| reflective fallback 范围 | `LoadClassByID()` + `SetClassTypeInfo()` 未见固定参数上限 | 拒绝 `CustomThunk`，且 `BlueprintCallableReflectiveFallbackMaxArgs = 16` | Angelscript 的 reflective fallback 能力范围更窄 |
| 自动化颗粒度 | 属性 getter/setter 与参数编组都进入 translator 层 | fallback 只做一次性参数复制与 `ProcessEvent` | puerts 在反射 translator 颗粒度上实现更完整 |

### [维度 D6] 代码生成与 IDE 支持：`puerts` 的 `cpp/index.d.ts` 直接吃运行时类注册表，`Angelscript` 的生成链仍与 live binder 分离

前文已经说明 puerts 能产出 `ue.d.ts` / `cpp/index.d.ts`；这一轮补的是“声明和运行时绑定是否同源”。`JSClassRegister.h` 把 `SetClassTypeInfo()`、`ForeachRegisterClass()`、`LoadClassByID()` 暴露在同一套注册表 API 上；`JSClassRegister.cpp` 会把 constructor / method / property / variable infos 回填到 `JSClassDefinition`；`UTemplateBindingGenerator::Gen_Implementation()` 再直接遍历这张注册表生成 `Typing/cpp/index.d.ts`。这意味着 `cpp` 侧 IDE 声明不是另起一份 schema，而是运行时绑定数据的派生产物，天然更不容易漂移。

Angelscript 的自动化产物则是另一条构建链。`AngelscriptFunctionTableExporter` 遍历 `factory.Session.Modules`，统计 BlueprintCallable/Pure 覆盖率、写 skipped CSV；`AngelscriptFunctionTableCodeGenerator` 再分片生成 `AS_FunctionTable_*.cpp` 与 summary JSON。它当然不是没有生成链，但这条链和运行时 `FAngelscriptBinds::Register*` 仍是前后两段，而不是像 puerts 那样由同一份 live bind registry 直接喂给声明生成器。

```
[puerts] Runtime Registry -> IDE Schema
DefineClass / SetClassTypeInfo
 -> JSClassDefinition registry
 -> ForeachRegisterClass()
 -> TemplateBindingGenerator
 -> Typing/cpp/index.d.ts

[Angelscript] UHT Export -> Runtime Bind
factory.Session.Modules
 -> FunctionTableExporter / CodeGenerator
 -> AS_FunctionTable_*.cpp + CSV/JSON diagnostics
 -> runtime still calls FAngelscriptBinds::Register*
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSClassRegister.h:137-148`、`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JSClassRegister.cpp:201-213`、`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/TemplateBindingGenerator.cpp:82-100,193-216`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSClassRegister.h
// 位置: 137-148，runtime bind registry 同时暴露“写入类型信息”和“枚举所有类”
// ============================================================================
void JSENV_API SetClassTypeInfo(const void* TypeId, const NamedFunctionInfo* ConstructorInfos, const NamedFunctionInfo* MethodInfos,
    const NamedFunctionInfo* FunctionInfos, const NamedPropertyInfo* PropertyInfos, const NamedPropertyInfo* VariableInfos);

void JSENV_API ForeachRegisterClass(std::function<void(const JSClassDefinition* ClassDefinition)>);
JSENV_API const JSClassDefinition* LoadClassByID(const void* TypeId);

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JSClassRegister.cpp
// 位置: 201-213，同一张 JSClassDefinition 表既服务 runtime，也保存 IDE 需要的 schema
// ============================================================================
ClassDef->ConstructorInfos = PropertyInfoDuplicate(const_cast<NamedFunctionInfo*>(ConstructorInfos));
ClassDef->MethodInfos = PropertyInfoDuplicate(const_cast<NamedFunctionInfo*>(MethodInfos));
ClassDef->FunctionInfos = PropertyInfoDuplicate(const_cast<NamedFunctionInfo*>(FunctionInfos));
ClassDef->PropertyInfos = PropertyInfoDuplicate(const_cast<NamedPropertyInfo*>(PropertyInfos));
ClassDef->VariableInfos = PropertyInfoDuplicate(const_cast<NamedPropertyInfo*>(VariableInfos)); // ★ IDE 要的签名直接回填到 runtime registry

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/TemplateBindingGenerator.cpp
// 位置: 82-100, 193-216，声明生成器直接枚举 runtime registry
// ============================================================================
Output << "class " << ClassDefinition->ScriptName;
if (ClassDefinition->SuperTypeId)
{
    Output << " extends " << PUERTS_NAMESPACE::FindClassByID(ClassDefinition->SuperTypeId)->ScriptName;
}

PUERTS_NAMESPACE::ForeachRegisterClass(
    [&](const PUERTS_NAMESPACE::JSClassDefinition* ClassDefinition)
    {
        if (ClassDefinition->TypeId && ClassDefinition->ScriptName)
        {
            Gen.GenClass(ClassDefinition); // ★ 直接消费 live registry，而不是单独维护 schema
        }
    });

FFileHelper::SaveStringToFile(Gen.Output.Buffer, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
```

[2] `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:21-53,65-96`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:100-139,166-215`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:588-608`

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs
// 位置: 21-53, 65-96，生成链从 UHT session 出发，而不是 runtime bind registry
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
    WriteSkippedEntriesCsv(factory, skippedEntries); // ★ 输出 coverage 诊断
}

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs
// 位置: 100-139, 166-215，产物是 shard cpp + summary，而不是 IDE schema
// ============================================================================
foreach (AngelscriptGeneratedFunctionEntry entry in entries)
{
    if (entry.EraseMacro == "ERASE_NO_FUNCTION()")
    {
        stubEntries++;
    }
    else
    {
        directBindEntries++;
    }
}

string outputPath = factory.MakePath($"AS_FunctionTable_{module.ShortName}_{shardIndex:D3}", ".cpp");
factory.CommitOutput(outputPath, BuildShard(module.ShortName, editorOnly, includes, entries, startIndex, entryCount, shardIndex, shardCount));

string summaryPath = factory.MakePath("AS_FunctionTable_Summary", ".json");
File.WriteAllText(summaryPath, summaryJson, Encoding.UTF8); // ★ 自动化主产物是函数表和覆盖率报告
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp
// 位置: 588-608，runtime 真正生效仍要回到 RegisterGlobalFunction
// ============================================================================
int FunctionId = Manager.Engine->RegisterGlobalFunction(
    Signature.ToCString(), Function, asCALL_CDECL, *(asFunctionCaller*)&Caller, nullptr);
OnBind(FunctionId, UserData, nullptr); // ★ 运行时 binder 与 UHT exporter 不是同一张注册表
```

与 Angelscript 对比：

| 对比点 | puerts 新证据 | Angelscript 新证据 | 差距判断 |
| --- | --- | --- | --- |
| schema 来源 | `SetClassTypeInfo()` 与 `ForeachRegisterClass()` 共用 `JSClassDefinition` 注册表 | UHT exporter 直接遍历 `factory.Session.Modules` | 实现方式不同 |
| IDE/runtime 漂移控制 | `cpp/index.d.ts` 直接从 runtime registry 派生 | 生成链与 runtime binder 分离 | puerts 在 schema 同源性上实现更完整 |
| 覆盖率诊断 | 当前检视到的 `cpp/index.d.ts` 生成链未见等价 skipped CSV/summary | `SkippedEntries.csv`、`Summary.json`、module diagnostics 明确存在 | Angelscript 在生成覆盖率诊断上实现更完整 |

### [维度 D8] 性能与优化：`puerts` 在脚本回调热路径上仍保留逐参反射编组，`Angelscript` 把调用策略前移到 `UASFunction`

前面的 D8 已经拆过 `FastCall`、direct bind 和 `StaticJIT`；这一轮只看“UE/Blueprint 调脚本覆写函数”这条热路径。puerts 在发现 JS override 时会缓存 `FFunctionTranslator`，但真正执行 `CallJs()` 时仍要 `Alloca` V8 参数数组、逐参 `UEToJsInContainer`，返回后再逐参 `JsToUEOutInContainer`。即便走 `FastCall()`，它也仍要 `Memzero` 参数缓冲、枚举 `CPF_Parm`、拼 `FFrame` 与 `OutParm` 链，所以优化重点是缩短 V8 call stub，而不是把 UE 反射编组整体拿掉。

Angelscript 则把这些决定提前到 `UASFunction::FinalizeArguments()`。参数对齐、`PosInParmStruct`、`ArgStackSize`、`ParmBehavior`、`VMBehavior` 都在类生成阶段算好；执行时 `AngelscriptCallFromBPVM()` / `AngelscriptCallFromParms()` 只按这些枚举分支搬值，并在可用时直接跳 `JitFunction` / `JitFunction_ParmsEntry`。因此这不是“有没有桥接成本”的问题，而是“per-call reflection walk”与“one-time thunk specialization”的取舍差异。

```
[puerts] Script Override Call Path
cached FFunctionTranslator
 -> Alloca Args[]
 -> UEToJsInContainer per arg
 -> JsFunction->Call()
 -> JsToUEOutInContainer per arg
 -> keep UE reflection marshalling in path

[Angelscript] Pre-Specialized Script Call Path
UASFunction::FinalizeArguments()
 -> precompute offsets / VMBehavior / ParmBehavior
 -> AngelscriptCallFromBPVM / AngelscriptCallFromParms
 -> JitFunction or Context->SetArg*
 -> no per-call TFieldIterator on hot path
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:1364-1375`、`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp:295-365,439-470`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp
// 位置: 1364-1375，translator 会缓存，但只是把“建模成本”前移一次
// ============================================================================
if (!FuncInfo)
{
    TsFunctionMap.Add(
        Function, {v8::UniquePersistent<v8::Function>(
                       Isolate, v8::Local<v8::Function>::Cast(MaybeValue.ToLocalChecked())),
                   std::make_unique<FFunctionTranslator>(Function, false)});
}

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp
// 位置: 295-365, 439-470，真正调用时仍有逐参编组成本
// ============================================================================
FMemory::Memzero(Params, ParamsBufferSize);
FFrame NewStack(CallObject, CallFunction, Params, nullptr, Function->ChildProperties);
for (TFieldIterator<PropertyMacro> It(CallFunction); It && (It->PropertyFlags & CPF_Parm); ++It)
{
    PropertyMacro* Property = *It;
    if (Property->HasAnyPropertyFlags(CPF_OutParm))
    {
        Out = (FOutParmRec*) FMemory_Alloca(sizeof(FOutParmRec)); // ★ 每次调用仍要构建 OutParm 链
    }

    if (Property->HasAnyPropertyFlags(CPF_OutParm))
    {
        Arguments[Index]->JsToUEFastInContainer(Isolate, Context, Info[Index], Params, reinterpret_cast<void**>(&(Out->PropAddr)));
    }
    else
    {
        Arguments[Index]->JsToUEInContainer(Isolate, Context, Info[Index], Params, false);
    }
}

v8::Local<v8::Value>* Args =
    static_cast<v8::Local<v8::Value>*>(FMemory_Alloca(sizeof(v8::Local<v8::Value>) * Arguments.size()));
for (int i = 0; i < Arguments.size(); ++i)
{
    Args[i] = Arguments[i]->UEToJsInContainer(Isolate, Context, Params, false); // ★ UE -> JS 逐参转换
}

Result = JsFunction->Call(Context, This, Arguments.size(), Args);

for (int i = 0; i < Arguments.size(); ++i)
{
    Arguments[i]->JsToUEOutInContainer(Isolate, Context, Args[i], Params, true); // ★ 返回后再逐参回写 out/ref
}
```

[2] `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:149-176,181-307,475-520,534-663,736-849`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp
// 位置: 736-849，先在 UASFunction 上预计算调用策略
// ============================================================================
Arg.ValueBytes = ArgSize;
Arg.StackOffset = ArgStackSize;
Arg.PosInParmStruct = Arg.Property->GetOffset_ForUFunction();

if (Arg.Type.bIsReference)
{
    Arg.ParmBehavior = EArgumentParmBehavior::Reference;
    Arg.VMBehavior = Arg.Type.NeedConstruct()
        ? EArgumentVMBehavior::Reference
        : EArgumentVMBehavior::ReferencePOD;
}
else if (Arg.Type.IsObjectPointer())
{
    Arg.ParmBehavior = EArgumentParmBehavior::Value8Byte;
    Arg.VMBehavior = (WorldContextIndex == i)
        ? EArgumentVMBehavior::WorldContextObject
        : EArgumentVMBehavior::ObjectPointer;
}
else
{
    switch (Arg.Type.GetValueSize())
    {
        case 1: Arg.ParmBehavior = EArgumentParmBehavior::Value1Byte; Arg.VMBehavior = EArgumentVMBehavior::Value1Byte; break;
        case 2: Arg.ParmBehavior = EArgumentParmBehavior::Value2Byte; Arg.VMBehavior = EArgumentVMBehavior::Value2Byte; break;
        case 4: Arg.ParmBehavior = EArgumentParmBehavior::Value4Byte; Arg.VMBehavior = EArgumentVMBehavior::Value4Byte; break;
        case 8: Arg.ParmBehavior = EArgumentParmBehavior::Value8Byte; Arg.VMBehavior = EArgumentVMBehavior::Value8Byte; break;
    }
}

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp
// 位置: 149-176, 181-307, 475-520, 534-663，执行期按预编码策略走 JIT 或 typed context
// ============================================================================
asCScriptFunction* ScriptFunction = (asCScriptFunction*)ASFunction->ScriptFunction;
asJITFunction JitFunction = nullptr;
if constexpr (TNonVirtual)
{
    JitFunction = ASFunction->JitFunction;
}
else
{
    ScriptFunction = ResolveScriptVirtual(ASFunction, Object);
    JitFunction = ScriptFunction->jitFunction; // ★ 优先直接跳 JIT 入口
}

for (int32 i = 0; i < ArgumentCount; ++i)
{
    auto& Arg = ASFunction->Arguments[i];
    switch (Arg.VMBehavior)
    {
        case UASFunction::EArgumentVMBehavior::Value4Byte:
            Stack.StepCompiledIn<FProperty>(&Value);
            *(asDWORD*)VMArgs = Value;
            VMArgs += 1;
        break;
        case UASFunction::EArgumentVMBehavior::Reference:
            Arg.Type.ConstructValue(StackPtr);
            uint8& RefValue = Stack.StepCompiledInRef<FProperty, uint8>(StackPtr);
            *(void**)VMArgs = &RefValue;
            VMArgs += 2;
        break;
    }
}

if (JitFunction != nullptr)
{
    (JitFunction)(Execution, VMArgStart, &OutValue); // ★ 调用时不再重新枚举 UFunction 属性
}

switch (Arg.ParmBehavior)
{
    case UASFunction::EArgumentParmBehavior::Value4Byte:
        Context->SetArgDWord(i, *(asDWORD*)ValuePtr);
    break;
    case UASFunction::EArgumentParmBehavior::Reference:
        Context->SetArgAddress(i, ValuePtr);
    break;
} // ★ 非 JIT 路径也按预计算的 ParmBehavior 直接设置上下文参数
```

与 Angelscript 对比：

| 对比点 | puerts 新证据 | Angelscript 新证据 | 差距判断 |
| --- | --- | --- | --- |
| 调用特化时机 | `FFunctionTranslator` 只缓存 translator；执行期仍逐参转换 | `FinalizeArguments()` 预先计算 offset / `ParmBehavior` / `VMBehavior` | Angelscript 在 call-site specialization 上实现更完整 |
| 热路径是否重走反射 | `FastCall()` 仍要遍历 `CPF_Parm`、构建 `FFrame` / `OutParm` | `AngelscriptCallFromBPVM/Parms` 按预编码枚举分支执行 | puerts 没有实现同等级执行期去反射化 |
| JIT 接入位置 | 优化重点在 V8 callback stub，与参数编组仍耦合 | 可直接落到 `JitFunction` / `JitFunction_ParmsEntry` | 实现方式不同 |

---

## 深化分析 (2026-04-08 19:23:13)

### [维度 D4] 热重载：`puerts` 的模块加载面大于默认 HMR 覆盖面，`Angelscript` 的 reload 面与源码面基本一致

前面的 D4 已经说明 puerts 通过 Inspector 做模块级 patch；这一轮补的是“哪些模块能被加载”和“哪些模块会被默认热更”并不是一回事。`DefaultJSModuleLoader` 允许 `.js/.mjs/.cjs/.json`、可选 `.mbc/.cbc`、`package.json` 与 `index.js`，还会沿目录向上回溯 `node_modules`；但 `FSourceFileWatcher` 只在 `OnSourceLoaded()` 之后登记已加载目录，并且只对 `FCA_Modified && *.js` 触发回调。`hot_reload.js` 再要求目标 URL 已经拿到 `scriptId` 且新内容是字符串，最终只调用 `Debugger.setScriptSource(...)` 替换源码。结果是 `mjs/cjs/json/package.json/mbc/cbc` 虽然属于 runtime 可解析模块，却不在默认 watcher + HMR 路径里。

Angelscript 这边没有出现同类“加载器支持集合 > reload 支持集合”的裂缝。预处理器把输入文件直接归一化成 `FAngelscriptModuleDesc`，类生成器随后按属性类型、方法签名、默认值、metadata 与 `BlueprintEvent` 变化升降级 `FullReloadSuggested/FullReloadRequired`。因为它只有 `.as` 这一类源码输入，reload 能力边界和源码边界基本是同一条线。

```
[puerts] Loadable Modules vs HMR Modules
module resolution
 -> .js / .mjs / .cjs / .json                // loader 可解析
 -> package.json / index.js / .mbc / .cbc
 -> execute / parse module
 -> OnSourceLoaded() registers watched dir
 -> only modified *.js enters watcher
 -> Debugger.setScriptSource(scriptId, source)
 -> HMR covers only a subset of loadable modules

[Angelscript] Reload Envelope
changed .as file
 -> Preprocessor.AddFile()
 -> GetModulesToCompile()
 -> compare property / function / default / meta diffs
 -> SoftReload / FullReloadSuggested / FullReloadRequired
 -> reload envelope matches source envelope
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DefaultJSModuleLoader.cpp:67-120`、`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/SourceFileWatcher.cpp:22-80`、`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js:53-90`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DefaultJSModuleLoader.cpp
// 位置: 67-120，loader 支持的模块格式明显多于默认 watcher 监听格式
// ============================================================================
bool IsJs = Extension == TEXT("js") || Extension == TEXT("mjs") || Extension == TEXT("cjs") || Extension == TEXT("json");
return SearchModuleWithExtInDir(Dir, RequiredModule + ".js", Path, AbsolutePath) ||
       SearchModuleWithExtInDir(Dir, RequiredModule + ".mjs", Path, AbsolutePath) ||
       SearchModuleWithExtInDir(Dir, RequiredModule + ".cjs", Path, AbsolutePath) ||
#if defined(WITH_V8_BYTECODE)
       SearchModuleWithExtInDir(Dir, RequiredModule + ".mbc", Path, AbsolutePath) ||
       SearchModuleWithExtInDir(Dir, RequiredModule + ".cbc", Path, AbsolutePath) ||
#endif
       SearchModuleWithExtInDir(Dir, RequiredModule / "package.json", Path, AbsolutePath) ||
       SearchModuleWithExtInDir(Dir, RequiredModule / "index.js", Path, AbsolutePath);

if (RequiredDir != TEXT("") && !RequiredModule.GetCharArray().Contains('/') && !RequiredModule.EndsWith(TEXT(".js")) &&
    !RequiredModule.EndsWith(TEXT(".mjs")))
{
    // ★ 继续向上找父目录和 node_modules，保留 npm 风格解析语义
    if (SearchModuleInDir(FString::Join(pathFrags, TEXT("/")), RequiredModule, Path, AbsolutePath))
    {
        return true;
    }
}

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/SourceFileWatcher.cpp
// 位置: 22-80，默认 watcher 只登记已加载目录中的 *.js 修改
// ============================================================================
if (!WatchedDirs.Contains(Dir))
{
    DirectoryWatcher->RegisterDirectoryChangedCallback_Handle(
        Dir,
        IDirectoryWatcher::FDirectoryChanged::CreateRaw(this, &FSourceFileWatcher::OnDirectoryChanged),
        DelegateHandle,
        IDirectoryWatcher::IgnoreChangesInSubtree);
}

if (Change.Action == FFileChangeData::FCA_Modified && Change.Filename.EndsWith(TEXT(".js")))
{
    if (WatchedFiles[Dir].Contains(FileName))
    {
        FMD5Hash Hash = FMD5Hash::HashFile(*NotifyPath);
        if (WatchedFiles[Dir][FileName] != Hash)
        {
            OnWatchedFileChanged(NotifyPath);   // ★ 这里只有 *.js 会进入热更入口
        }
    }
}
```

```js
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js
// 位置: 53-90，HMR 最终要求 scriptId + string source，走 Inspector 改源码
// ============================================================================
async function enableDebugger() {
    setInspectorCallback(messageHandler);
    await sendCommand("Runtime.enable", {});
    await sendCommand("Debugger.enable", {"maxScriptsCacheSize":10000000});
}

if (scriptId) {
    if (typeof source === "string") {
        let orgSourceInfo = await sendCommand("Debugger.getScriptSource", {scriptId:"" + scriptId});
        source = ("(function (exports, require, module, __filename, __dirname) { " + source + "\n});");
        let res = await sendCommand("Debugger.setScriptSource", {scriptId:"" + scriptId,scriptSource:source});
        puerts.emit('HMR.finish', moduleName, m, url); // ★ 改的是源码文本，不是 json/bytecode 载荷
    }
}
```

[2] `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:75-123`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:1097-1294`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp
// 位置: 75-123，reload 输入和编译输入共用同一份 .as 文件列表
// ============================================================================
TArray<TSharedRef<FAngelscriptModuleDesc>> FAngelscriptPreprocessor::GetModulesToCompile()
{
    for (auto& File : Files)
        OutArray.AddUnique(File.Module.ToSharedRef()); // ★ 预处理器直接产出后续 reload/compile 的模块集合
}

void FAngelscriptPreprocessor::AddFile(const FString& RelativeFilename, const FString& AbsoluteFilename, bool bLoadAsynchronous, bool bTreatAsDeleted)
{
    Module->ModuleName = FilenameToModuleName(RelativeFilename);
    if (!bTreatAsDeleted)
    {
        FFileHelper::LoadFileToString(File.RawCode, *AbsoluteFilename); // ★ 输入就是最终参与比较/编译的 .as 源
    }
}

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 位置: 1097-1294，reload 等级由源码差异直接决定
// ============================================================================
if (PropertyDesc->PropertyType != OldPropertyDesc->PropertyType)
{
    ClassData.ReloadReq = EReloadRequirement::FullReloadRequired; // ★ 属性类型变了，必须 full reload
}

if (!OldFunctionDesc->SignatureMatches(NewFunctionDesc))
{
    ClassData.ReloadReq = EReloadRequirement::FullReloadRequired; // ★ 方法签名变了，必须 full reload
}

if (NewArgument.DefaultValue != OldArgument.DefaultValue || NewArgument.ArgumentName != OldArgument.ArgumentName)
{
    ClassData.ReloadReq = EReloadRequirement::FullReloadSuggested; // ★ 默认值/参数名变化至少建议 full reload
}

if (NewFunctionDesc->bBlueprintEvent)
{
    ClassData.ReloadReq = EReloadRequirement::FullReloadRequired; // ★ 新增 BlueprintEvent 直接升级到 required
}
```

设计取舍：

- puerts 把模块解析面做得更宽，兼容 JS 生态更容易，但默认 HMR 只覆盖文本 `*.js`，多格式模块的一致热更体验要靠额外机制补齐。
- Angelscript 牺牲了模块格式多样性，换来“进入编译的源码就进入 reload 判定”的一致性，失败边界更可预测。

与 Angelscript 对比：

| 对比点 | puerts 新证据 | Angelscript 新证据 | 差距判断 |
| --- | --- | --- | --- |
| 可加载模块集合 | `.js/.mjs/.cjs/.json/package.json/.mbc/.cbc` 都可解析 | 预处理与编译输入围绕 `.as` 模块描述 | 实现方式不同 |
| 默认 watcher 覆盖面 | `SourceFileWatcher` 只监听已加载目录中的 `*.js` 修改 | `AddFile()` 与 `GetModulesToCompile()` 直接喂给 reload/compile | puerts 没有实现同等级“加载面=热更面”一致性 |
| reload 执行形态 | `Debugger.setScriptSource()` 替换脚本文本 | 按属性/签名/默认值差异升降级 reload 等级 | 实现方式不同 |

### [维度 D6] 代码生成与 IDE 支持：`puerts` 把 TypeScript 语义图直接当作 Blueprint schema 编译器，`Angelscript` 当前更偏源码导航

前面的 D6 已经覆盖声明生成与 LanguageService；这一轮补的是“IDE 语义最终落到了哪里”。`PuertsEditorModule` 在编辑器里直接启动一个 `JsEnv` 执行 `PuertsEditor/CodeAnalyze`；`CodeAnalyze.ts` 使用 TypeScript `checker` 把 `ts.Type` 转成 `UE.PEGraphPinType`，再从 decorator / annotation 编译 `uclass`、`ufunction`、`uparam`、`uproperty` metadata，最后直接调用 `UPEBlueprintAsset` 增量写 Blueprint 参数、函数和成员变量。换句话说，`.d.ts` 只是结果之一，更关键的是 TS 语义图本身已经成为 Blueprint 资产签名的权威来源。

Angelscript 当前检视范围内的 IDE 支持策略更像“保证原生工具能回到脚本源”。`UASFunction::GetSourceFilePath()` / `GetSourceLineNumber()` 从运行时模块和 `scriptData` 还原源码定位，`FAngelscriptSourceCodeNavigation` 再把 UE 里的 `UClass/UFunction/FProperty` 导航回脚本文件。它并不是没有 IDE 支持，而是当前源码里未见等价的内嵌语义服务，去反向推导 Blueprint pin 类型和 metadata。

```
[puerts] TS Semantic -> Blueprint Asset
PuertsEditorModule
 -> JsEnv.Start("PuertsEditor/CodeAnalyze")
 -> TypeScript checker
 -> tsTypeToPinType / compile*MetaData
 -> UPEBlueprintAsset Add*WithMetaData
 -> CompileBlueprint()

[Angelscript] Runtime Metadata -> Source Navigation
script module + scriptData
 -> UASFunction::GetSourceFilePath / GetSourceLineNumber
 -> FAngelscriptSourceCodeNavigation
 -> OpenFile / OpenModule
 -> editor jumps back to script source
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:138-150`、`Reference/puerts/unreal/Puerts/PuertsEditor/CodeAnalyze.ts:725-816,875-918,936-1055`、`Reference/puerts/unreal/Puerts/PuertsEditor/UEMeta.ts:423-438,1884-1938`、`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:1345-1357`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp
// 位置: 138-150，编辑器直接起一个 JS 环境跑 CodeAnalyze
// ============================================================================
JsEnv = MakeShared<PUERTS_NAMESPACE::FJsEnv>(
    std::make_shared<PUERTS_NAMESPACE::DefaultJSModuleLoader>(TEXT("JavaScript")),
    std::make_shared<PUERTS_NAMESPACE::FDefaultLogger>(), -1,
    [this](const FString& InPath)
    {
        if (SourceFileWatcher.IsValid())
        {
            SourceFileWatcher->OnSourceLoaded(InPath);
        }
    },
    TEXT("--max-old-space-size=2048"));

JsEnv->Start("PuertsEditor/CodeAnalyze"); // ★ TS 语义分析器直接跑在插件内
```

```ts
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/PuertsEditor/CodeAnalyze.ts
// 位置: 725-816, 875-918, 936-1055，TypeScript 类型系统直接驱动 Blueprint schema
// ============================================================================
function tsTypeToPinType(type: ts.Type, node: ts.Node) {
    let typeNode = checker.typeToTypeNode(type, undefined, undefined);
    if (typeName == 'TArray' || typeName == 'TSet') {
        result.pinType.PinContainerType = typeName == 'TArray' ? UE.EPinContainerType.Array : UE.EPinContainerType.Set;
        return result;
    } else if (typeName == 'TMap') {
        result.pinType.PinContainerType = UE.EPinContainerType.Map;
        result.pinValueType = new UE.PEGraphTerminalType(valuePinType.pinType.PinCategory, valuePinType.pinType.PinSubCategoryObject);
        return result; // ★ 容器类型、值类型都直接从 TS 泛型推导
    }
}

function getDecoratorFlagsValue(valueDeclaration:ts.Node, posfix: string, flagsDef:object): bigint {
    if (expression.expression.getFullText() == posfix|| expression.expression.getFullText().endsWith('.' + posfix)) {
        ret = ret | e; // ★ decorator 直接改 UE flags
    }
}

bp.LoadOrCreateWithMetaData(type.getSymbol().getName(), modulePath, baseTypeUClass, 0, 0, uemeta.compileClassMetaData(type));
bp.AddParameterWithMetaData(signature.parameters[i].getName(), paramPinType.pinType, paramPinType.pinValueType, uemeta.compileParamMetaData(signature.parameters[i]));
bp.AddFunctionWithMetaData(symbol.getName(), false, resultPinType.pinType, resultPinType.pinValueType, flags, clearFlags, uemeta.compileFunctionMetaData(symbol));
// ★ 参数 pin、返回 pin、UFUNCTION flags、metadata 都从同一份 TS 语义图写回 Blueprint asset
```

```ts
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/PuertsEditor/UEMeta.ts
// 位置: 423-438, 1884-1938，把 decorator 统一编译成 UE metadata 对象
// ============================================================================
function getMetaDataFromDecorators(decorators:ts.NodeArray<ts.Decorator> | null, prefix: string)
{
    decorators.forEach((value)=>
    {
        collectMetaDataFromDecorator(value, prefix, specifiers, metaData); // ★ 统一收集 uclass/ufunction/uparam/uproperty
    });
}

let [specifiers, metaData] = getMetaDataFromDecorators(decorators as ts.NodeArray<ts.Decorator>, 'ufunction');
return processFunctionMetaData(specifiers, metaData);

let [specifiers, metaData] = getMetaDataFromDecorators(decorators as ts.NodeArray<ts.Decorator>, 'uproperty');
const result = processPropertyMetaData(specifiers, metaData);
```

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp
// 位置: 1345-1357，分析结果最终落到 Blueprint 编译
// ============================================================================
if (NeedSave)
{
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint); // ★ TypeScript 语义结果最终驱动 Blueprint 重编译
}
```

[2] `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp:15-44`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:1497-1507,1535-1558`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp
// 位置: 15-44，IDE 支持核心是从 UE 反射对象跳回脚本源
// ============================================================================
virtual bool NavigateToFunction(const UFunction* InFunction) override
{
    auto* ASFunc = Cast<const UASFunction>(InFunction);
    FString Path = ASFunc->GetSourceFilePath();
    if (Path.Len() == 0)
        return false;

    OpenFile(Path, ASFunc->GetSourceLineNumber()); // ★ 用运行时保存的源码定位做导航
    return true;
}

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp
// 位置: 1497-1507, 1535-1558，源码路径与行号来自运行时模块/脚本数据
// ============================================================================
FString UASFunction::GetSourceFilePath() const
{
    auto Module = Manager.GetModule(ScriptFunction->GetModule());
    return Module->Code[0].AbsoluteFilename; // ★ 直接回到模块记录的源文件
}

int UASFunction::GetSourceLineNumber() const
{
    auto* RealFunc = ((asCScriptFunction*)ScriptFunction);
    auto* scriptData = RealFunc->scriptData;
    return (scriptData->declaredAt & 0xFFFFF) + 1; // ★ 行号来自脚本函数调试数据
}
```

设计取舍：

- puerts 把 IDE 语义系统直接变成资产 schema 编译器，类型安全和 Blueprint 资产一致性更强，但编辑器工具链明显依赖 TypeScript 语义服务常驻。
- Angelscript 的策略更轻，靠运行时保留的源码路径/行号实现导航与定位；代价是当前检视范围内未见等价“语义图 -> 资产 schema”闭环。

与 Angelscript 对比：

| 对比点 | puerts 新证据 | Angelscript 新证据 | 差距判断 |
| --- | --- | --- | --- |
| IDE 语义权威源 | `checker.typeToTypeNode()` + `tsTypeToPinType()` 直接生成 pin/schema | `UASFunction::GetSourceFilePath/GetSourceLineNumber` 提供源码定位 | 实现方式不同 |
| metadata 来源 | decorator 经 `compile*MetaData()` 直接生成 UE metadata | 当前检视范围内 IDE 侧主要消费运行时源码定位 | puerts 在“语义信息直连资产 metadata”上实现更完整 |
| Blueprint 资产闭环 | `UPEBlueprintAsset` 写回并 `CompileBlueprint()` | 当前检视范围内未见等价 IDE 语义服务驱动 pin 生成 | Angelscript 当前没有实现同等级 asset-schema 语义编译链 |

### [维度 D11] 部署与打包：`puerts` 运行时部署契约包含 npm 风格目录语义，`Angelscript` 的契约是 script root 与 package 可见性

前面的 D11 已经覆盖了 loose source、cache 与 editor-only 边界；这一轮补的是“部署后的目录结构本身是不是运行时 API”。从 `JsEnvImpl`、`DefaultJSModuleLoader` 与 `modular.js` 连起来看，答案对 puerts 是肯定的：Node 路径下会先把 `require('module').createRequire(process.cwd() + '/')` 挂到全局；默认 loader 会查当前目录、父目录、`node_modules`、`package.json` 与 `index.js`；`modular.js` 再解析 `package.json` 的 `type/main/exports["."]` 来决定 ESM/CJS 入口。也就是说，部署给 puerts 的不只是若干脚本文件，而是一套带目录约定的 JS package layout。

Angelscript 的部署契约则更接近 UE 原生脚本包系统。启动时只收集项目 `Script/` 和启用插件的 `Script/` 根；预处理器把相对文件名直接转成模块名；`AdditionalEditorOnlyScriptPackageNames` 与 `SetEditorOnlyBlockLinePositions()` 再把 package 可见性与 editor-only 线位编码进去。它关心的是“这个文件属于哪个 script package / compile unit”，而不是 `node_modules/package.json` 这样的运行时目录协议。

```
[puerts] Runtime Package Contract
project Content/<ScriptRoot>
 -> current dir / parent dir / node_modules lookup
 -> package.json main / exports / type
 -> genRequire()
 -> ESM / CJS / JSON / bytecode module

[Angelscript] Runtime Package Contract
Project Script/ + Plugin Script/
 -> RelativeFilename -> ModuleName
 -> AdditionalEditorOnlyScriptPackageNames
 -> EditorOnlyBlockLinePositions
 -> script package visibility + compile unit identity
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:469-472,621-640,3778-3806`、`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DefaultJSModuleLoader.cpp:67-120`、`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:74-77,105-176`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp
// 位置: 469-472, 621-640, 3778-3806，把 Node/package 语义正式带进 runtime
// ============================================================================
v8::MaybeLocal<v8::Value> LoadenvRet = node::LoadEnvironment(NodeEnv,
    "const publicRequire ="
    "  require('module').createRequire(process.cwd() + '/');"
    "globalThis.require = publicRequire;"); // ★ Node backend 主动暴露公共 require

ExecuteModule("puerts/modular.js");
GetESMMain.Reset(
    Isolate, PuertsObj->Get(Context, FV8Utils::ToV8String(Isolate, "getESMMain")).ToLocalChecked().As<v8::Function>());
// ★ modular.js 成为 package 解析协议的一部分

if (OutPath.EndsWith(TEXT("package.json")))
{
    auto MaybeRet = GetESMMain.Get(Isolate)->Call(Context, v8::Undefined(Isolate), 1, Args);
    if (MaybeRet.ToLocal(&ESMMainValue) && ESMMainValue->IsString())
    {
        if (ModuleLoader->Search(FPaths::GetPath(OutPath), ESMMain, ESMMainOutPath, ESMMainOutDebugPath))
        {
            OutPath = ESMMainOutPath; // ★ package.json 的 main/type 会参与最终入口决议
        }
    }
}

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DefaultJSModuleLoader.cpp
// 位置: 67-120，部署目录结构必须满足 node_modules/package.json 解析约定
// ============================================================================
return CheckExists(Dir / RequiredModule, Path, AbsolutePath) ||
       (!Dir.EndsWith(TEXT("node_modules")) && CheckExists(Dir / TEXT("node_modules") / RequiredModule, Path, AbsolutePath));

if (RequiredDir != TEXT("") && !RequiredModule.GetCharArray().Contains('/') && !RequiredModule.EndsWith(TEXT(".js")) &&
    !RequiredModule.EndsWith(TEXT(".mjs")))
{
    // ★ 沿父目录向上继续查包，和 Node 风格一致
    if (SearchModuleInDir(FString::Join(pathFrags, TEXT("/")), RequiredModule, Path, AbsolutePath))
    {
        return true;
    }
}
```

```js
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js
// 位置: 74-77, 105-176，package.json 不是旁路文件，而是 require 协议的一部分
// ============================================================================
function getESMMain(script) {
    let packageConfigure = JSON.parse(script);
    return (packageConfigure && packageConfigure.type === "module") ? packageConfigure.main : undefined;
}

function genRequire(requiringDir, outerIsESM) {
    let moduleInfo = searchModule(moduleName, requiringDir);
    let [fullPath, debugPath] = moduleInfo;

    if (fullPath.endsWith(".json")) {
        let packageConfigure = JSON.parse(script);
        if (fullPath.endsWith("package.json")) {
            isESM = packageConfigure.type === "module";
            let url = packageConfigure.main || "index.js";
            if (isESM) {
                let packageExports = packageConfigure.exports && packageConfigure.exports["."];
                url =
                    (packageExports["default"] && packageExports["default"]["require"]) ||
                    (packageExports["require"] && packageExports["require"]["default"]) ||
                    packageExports["require"];
            }
            let tmpRequire = genRequire(fullDirInJs, isESM);
            let r = tmpRequire(url);
            m.exports = r; // ★ package.json 决定真正执行哪个入口
        }
    }
}
```

[2] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1326-1363,4353-4356`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:86-103`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h:197-201`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 1326-1363, 4353-4356，部署契约围绕 script root 与 editor-only 可见性
// ============================================================================
FString RootPath = Dependencies.ConvertRelativePathToFull(Dependencies.GetProjectDir() / TEXT("Script"));
for (const FString& PluginScriptRoot : Dependencies.GetEnabledPluginScriptRoots())
{
    const FString ScriptPath = Dependencies.ConvertRelativePathToFull(PluginScriptRoot);
    if (Dependencies.DirectoryExists(ScriptPath) && ScriptPath != RootPath)
    {
        DiscoveredRootPaths.Add(ScriptPath); // ★ 运行时先确定哪些 Script 根参与发现
    }
}

DiscoveredRootPaths.Insert(RootPath, 0); // ★ 项目根优先

ScriptModule->builder->SetEditorOnlyBlockLinePositions(Module->EditorOnlyBlockLines);
ScriptModule->builder->isEditorOnlyModule = Module->ModuleName.StartsWith(TEXT("Editor.")) || Module->ModuleName.Contains(TEXT(".Editor."));
// ★ editor-only 边界是 package / line-range 级别的编译可见性

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp
// 位置: 86-103，文件名会直接被归一化为模块名
// ============================================================================
FString FAngelscriptPreprocessor::FilenameToModuleName(const FString& Filename)
{
    return Filename.Replace(TEXT(".as"), TEXT("")).Replace(TEXT("/"), TEXT("."));
}

Module->ModuleName = FilenameToModuleName(RelativeFilename); // ★ 模块身份来自相对路径，不依赖 package.json 协议
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h
// 位置: 197-201，部署配置直接暴露 script package 可见性
// ============================================================================
UPROPERTY(Config)
TArray<FName> AdditionalEditorOnlyScriptPackageNames;
// ★ 明确允许把 /Script/ModuleName 级别的脚本包标记为 editor-only
```

设计取舍：

- puerts 保留了 JS 生态熟悉的 package layout，第三方包迁移和多入口模块组织更自然，但部署目录结构本身就成为运行时契约。
- Angelscript 让部署契约收敛到 script root、模块名和 package 可见性，Cook/预编译边界更可控，但没有把 npm 风格目录协议带进 runtime。

与 Angelscript 对比：

| 对比点 | puerts 新证据 | Angelscript 新证据 | 差距判断 |
| --- | --- | --- | --- |
| 运行时目录协议 | `node_modules`、`package.json`、`exports/main/type` 都参与解析 | `DiscoverScriptRoots()` + `FilenameToModuleName()` 决定模块身份 | 实现方式不同 |
| 模块入口决议 | `modular.js` 会根据 `package.json` 二次跳转真实入口 | 模块入口就是相对脚本文件归一化后的模块名 | 实现方式不同 |
| editor-only 可见性 | 当前新增检视路径未见等价 package 级 editor-only 配置 | `AdditionalEditorOnlyScriptPackageNames` + `EditorOnlyBlockLines` 明确存在 | puerts 在部署可见性边界上没有实现同等级显式配置 |

---

## 深化分析 (2026-04-08 19:34:06)

### [维度 D1] 插件架构与模块划分：`puerts` 把多后端 ABI 继续外推成可分发 addon SDK，`Angelscript` 的自动化边界仍留在仓库内构建图

前面的 D1 已经说明 puerts 的 backend-neutral 绑定 ABI 存在；这一轮继续往发布边界看，会发现它不是只给仓库内部代码使用。`make_puerts_libs.js` 会主动挑出 `Binding.hpp`、`PesapiBackend.hpp`、`pesapi.h` 等公共头文件，组装 `puerts_libs/include`，再从 `pesapi.h` 扫描所有 `PESAPI_EXTERN` 符号，生成 `puerts_libs/src/pesapi_adpt.c`。配合 `PESAPI_MODULE(...)` 宏和 `LoadPesapiDll()` 的函数表注入，puerts 实际上把“宿主提供能力、扩展只实现初始化”的合作边界做成了独立 SDK。

Angelscript 的自动化重点则还是“减少仓库内手写绑定样板”。`AngelscriptFunctionTableCodeGenerator` 先从 UHT session 里反推 `AngelscriptRuntime.Build.cs`，再遍历 `module.ScriptPackage` 收集条目，输出 `AS_FunctionTable_*.cpp` 并注册到 `FAngelscriptBinds::AddFunctionEntry(...)`。这说明它的工程策略是让当前插件自己的构建链更顺，而不是把绑定边界发布成外部二进制 ABI。这里不是“有没有自动化”的差别，而是“面向插件内协作”与“面向外部扩展生态”的差别。

```
[puerts] External Addon SDK Flow
Source/JsEnv/Public/*.hpp
 -> make_puerts_libs.js
 -> puerts_libs/include + src/pesapi_adpt.c
 -> addon exports PESAPI_MODULE(...)
 -> LoadPesapiDll / Init(funcs)
 -> backend-neutral pesapi ABI

[Angelscript] In-Tree Generation Flow
UHT session modules
 -> ResolveRuntimeBuildCsPath()
 -> CollectEntries(module.ScriptPackage)
 -> AS_FunctionTable_*.cpp
 -> FAngelscriptBinds::AddFunctionEntry(...)
 -> same build graph / same runtime module
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/make_puerts_libs.js:4-18,45-68` 与 `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/pesapi.h:82-105`

```js
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/make_puerts_libs.js
// 位置: 4-18, 45-68，把公共头文件和 pesapi 适配层打成独立 SDK 目录
// ============================================================================
const headers = [
  'Binding.hpp', 'PesapiObject.hpp', 'TypeInfo.hpp',
  'DataTransfer.h', 'PuertsNamespaceDef.h', 'V8Backend.hpp',
  'JSClassRegister.h', 'ScriptBackend.hpp', 'V8FastCall.hpp',
  'Object.hpp', 'StaticCall.hpp', 'V8Object.hpp',
  'PesapiBackend.hpp', 'StdFunctionConverter.hpp', 'pesapi.h'
];

const lib_path = path.resolve(__dirname, 'puerts_libs');
fs.mkdirSync(path.join(lib_path, 'include'), { recursive: true });
fs.mkdirSync(path.join(lib_path, 'src'), { recursive: true });
headers.forEach((name) =>
    fs.writeFileSync(path.join(lib_path, 'include', name), fs.readFileSync(path.join('Source/JsEnv/Public', name))));

let m = line.match(/^PESAPI_EXTERN\s+([\w\* ]+)\s+(pesapi_[^\(]+)(.+);/);
if (functionName != "pesapi_init") {
    ptrSetter += `    ${functionName}_ptr = (${functionName}Type)func_array[${funcIndex++}];\n`;
    ptrGetter.push(`(pesapi_func_ptr)&${functionName}`);
}

var pesapi_adpt =
    '#define PESAPI_ADPT_C\n\n#include "pesapi.h"\n\n#if IL2CPP_TARGET_IOS\n#define WITHOUT_PESAPI_WRAPPER\n#endif\n\n'
  + '#if !defined(WITHOUT_PESAPI_WRAPPER)\n\nEXTERN_C_START\n\n' + apiImpl
  + '\n#endif\n\nvoid pesapi_init(pesapi_func_ptr* func_array){\n#if !defined(WITHOUT_PESAPI_WRAPPER)\n'
  + ptrSetter + '\n#endif\n}\n\nEXTERN_C_END\n';
// ★ 生成物不是业务绑定代码，而是“把宿主函数表转成稳定 C ABI 包装”的适配层
```

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Public/pesapi.h
// 位置: 82-105，扩展模块的正式入口约定
// ============================================================================
#define PESAPI_MODULE(modname, initfunc)                                                                   \
    EXTERN_C_START                                                                                         \
    PESAPI_MODULE_EXPORT void PESAPI_MODULE_INITIALIZER(modname)(pesapi_func_ptr * func_ptr_array);        \
    PESAPI_MODULE_EXPORT const char* PESAPI_MODULE_INITIALIZER(dynamic)(pesapi_func_ptr * func_ptr_array); \
    PESAPI_MODULE_EXPORT int PESAPI_MODULE_VERSION()();                                                    \
    EXTERN_C_END                                                                                           \
    PESAPI_MODULE_EXPORT void PESAPI_MODULE_INITIALIZER(modname)(pesapi_func_ptr * func_ptr_array)         \
    {                                                                                                      \
        pesapi_init(func_ptr_array);                                                                       \
        initfunc();                                                                                        \
    }                                                                                                      \
    PESAPI_MODULE_EXPORT const char* PESAPI_MODULE_INITIALIZER(dynamic)(pesapi_func_ptr * func_ptr_array)  \
    {                                                                                                      \
        if (func_ptr_array)                                                                                \
        {                                                                                                  \
            pesapi_init(func_ptr_array);                                                                   \
            initfunc();                                                                                    \
        }                                                                                                  \
        return #modname;                                                                                   \
    }
// ★ addon 只需遵守这一套入口签名，不需要直接链接 V8 / QuickJS / Node 私有 API
```

[2] `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PesapiAddonLoad.cpp:31-67,99-107,143-149`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PesapiAddonLoad.cpp
// 位置: 31-67, 99-107, 143-149，宿主在运行时把 backend 能力打包成函数表注入扩展
// ============================================================================
static pesapi_func_ptr funcs[] = {
    (pesapi_func_ptr) &pesapi_create_function,
    (pesapi_func_ptr) &pesapi_create_class,
    (pesapi_func_ptr) &pesapi_call_function,
    (pesapi_func_ptr) &pesapi_define_class,
    (pesapi_func_ptr) &pesapi_get_class_data,
    (pesapi_func_ptr) &pesapi_trace_native_object_lifecycle,
    (pesapi_func_ptr) &pesapi_find_type_id
    // ★ 实际还有大量 create/get/set API，一次性作为宿主 ABI 注入
};

auto Init = (const char* (*)(pesapi_func_ptr*))(uintptr_t)FPlatformProcess::GetDllExport(DllHandle, *EntryName);
const char* ModuleName = Init(nullptr);
GPesapiModuleName = ModuleName;
Init(funcs);     // ★ 真正初始化 addon 时只传函数表，不传具体 VM 对象
GPesapiModuleName = nullptr;

const char* ModuleName = Init(nullptr);
GPesapiModuleName = ModuleName;
Init(funcs);     // ★ 普通 dll/so 路径与 JS 侧 load() 路径走同一契约
GPesapiModuleName = nullptr;
```

[3] `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:51-86,302-312,334-409`

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs
// 位置: 51-86, 302-312, 334-409，自动化绑定条目仍然留在当前仓库的 UHT / Build.cs 图里
// ============================================================================
public static int Generate(IUhtExportFactory factory)
{
    AngelscriptSupportedModules supportedModules = LoadSupportedModules(factory);
    foreach (UhtModule module in factory.Session.Modules)
    {
        if (!supportedModules.All.Contains(module.ShortName))
        {
            continue;
        }

        AngelscriptModuleGenerationSummary? moduleSummary =
            GenerateModule(factory, module, supportedModules.EditorOnly.Contains(module.ShortName), generatedPaths, csvEntries);
    }
}

builder.Append("AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_AS_FunctionTable_")
    .Append(moduleShortName)
    .Append('_')
    .Append(shardIndex.ToString("D3"))
    .AppendLine("((int32)FAngelscriptBinds::EOrder::Late + 50, []()");
builder.AppendLine("{");
builder.AppendLine(entries[entryIndex].BuildRegistrationLine());
// ★ 生成物最终还是仓库内的 C++ 绑定文件

string buildCsPath = ResolveRuntimeBuildCsPath(factory);
foreach (UhtModule module in factory.Session.Modules)
{
    if (!module.ShortName.Equals("AngelscriptRuntime", StringComparison.Ordinal))
    {
        continue;
    }

    if (TryFindFirstHeaderPath(module.ScriptPackage, out string? headerPath) && !string.IsNullOrEmpty(headerPath))
    {
        return Path.Combine(moduleRoot, "AngelscriptRuntime.Build.cs");
    }
}
// ★ 生成器需要显式回到当前插件的 Build.cs，说明自动化边界仍绑在同一构建图
```

设计取舍：

- puerts 不只把 backend 差异藏进内部封装，还额外把 ABI 边界整理成对外可复用的 SDK 目录，适合扩展作者在插件外单独编译 addon。
- Angelscript 的自动化更强调“仓库内增量生成”和“当前 Runtime 模块可见性”，可以降低本插件自己的维护成本，但不会自然长出一个独立 addon 发行面。

与 Angelscript 对比：

| 对比点 | puerts 新证据 | Angelscript 新证据 | 差距判断 |
| --- | --- | --- | --- |
| 外部 addon SDK 打包 | `make_puerts_libs.js` 生成 `puerts_libs/include` 与 `src/pesapi_adpt.c` | UHT 生成物直接落成 `AS_FunctionTable_*.cpp` | Angelscript 当前没有实现同等级独立 addon SDK 打包层 |
| 扩展初始化契约 | `PESAPI_MODULE(...)` + `pesapi_init(func_array)` | `FAngelscriptBinds::AddFunctionEntry(...)` 留在同一构建图 | 实现方式不同 |
| 对 Build.cs 的依赖方式 | addon 运行时通过 `LoadPesapiDll()` 动态接宿主能力 | 生成器要先反推 `AngelscriptRuntime.Build.cs` 再生成条目 | puerts 在宿主/扩展解耦上实现更细 |

### [维度 D8] 性能与优化：`puerts` 的性能模型受 backend scheduler 影响，`Angelscript` 则把 runtime 推进收敛到 UE tick 与 context pool

前面的 D8 已经比较过 FastCall、StaticJIT 和 GC；这一轮补的是“谁负责把脚本运行时往前推进”。在 Node.js 后端下，puerts 不只是换了个 VM，而是多出一套 `uv_loop + polling thread + game-thread UvRunOnce()` 的调度链：后台线程等 OS 事件，真正的 `uv_run(UV_RUN_NOWAIT)` 与 `DrainTasks(Isolate)` 再回到 GameThread 执行。与此同时，QuickJS 路径虽然共用 `FJsEnvImpl` 接口，但 `IdleNotificationDeadline()` 会直接返回 `true`，`Request*GarbageCollectionForTesting()` 也会被编译成 no-op。也就是说，backend 选择会直接改变运行时推进方式和可用调参面。

Angelscript 的路径更单一。`FAngelscriptRuntimeModule` 只在编辑器挂一个 fallback ticker，正常运行时优先让 `UAngelscriptGameInstanceSubsystem` 成为 tick owner；真正执行层面再通过 `TryTakeContextFromPool()` / `ResetContextForPooling()` 复用 `asCContext`。这让它的性能模型更像 UE 原生子系统加上下文池，而不是一个外部异步 runtime 嵌进来。

```
[puerts] Backend-Dependent Runtime Drive
Node backend
 -> uv_loop_init + CreateEnvironment
 -> polling thread waits OS events
 -> dispatch UvRunOnce() to GameThread
 -> uv_run(UV_RUN_NOWAIT) + DrainTasks(Isolate)

QuickJS/V8 shared API
 -> IdleNotification / RequestGC exposed by interface
 -> actual semantics depend on compile-time backend

[Angelscript] UE-Owned Runtime Drive
GameInstanceSubsystem or editor fallback ticker
 -> PrimaryEngine->Tick(DeltaTime)
 -> TryTakeContextFromPool()
 -> execute / unprepare
 -> ResetContextForPooling()
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:165-247,418-482,1147-1187`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp
// 位置: 165-247, 418-482，Node backend 会额外引入 libuv 轮询线程与 game-thread 回调
// ============================================================================
void FJsEnvImpl::StartPolling()
{
    uv_sem_init(&PollingSem, 0);
    uv_thread_create(
        &PollingThread,
        [](void* arg)
        {
            auto* self = static_cast<FJsEnvImpl*>(arg);
            while (true)
            {
                uv_sem_wait(&self->PollingSem);
                if (self->PollingClosed)
                    break;

                self->PollEvents();
                self->LastJob = FFunctionGraphTask::CreateAndDispatchWhenReady(
                    [self]() { self->UvRunOnce(); }, TStatId{}, nullptr, ENamedThreads::GameThread);
            }
        },
        this);
    UvRunOnce();
}

void FJsEnvImpl::UvRunOnce()
{
    v8::TryCatch TryCatch(Isolate);
    uv_run(&NodeUVLoop, UV_RUN_NOWAIT);  // ★ 真正推进 Node/libuv 的执行点在这里
    if (!TryCatch.HasCaught())
    {
        static_cast<node::MultiIsolatePlatform*>(IJsEnvModule::Get().GetV8Platform())->DrainTasks(Isolate);
        // ★ 还要把 V8 平台任务手动 drain 掉
    }
    uv_sem_post(&PollingSem);
}

NodeEnv = CreateEnvironment(NodeIsolateData, Context, Args, ExecArgs, node::EnvironmentFlags::kOwnsProcessState);
Isolate->SetMicrotasksPolicy(v8::MicrotasksPolicy::kAuto);
StartPolling(); // ★ 选择 Node backend 后，运行时推进模型已经和纯 V8/QuickJS 不同
```

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp
// 位置: 1147-1187，同一个 IJsEnv 接口在不同 backend 下的调参语义并不一致
// ============================================================================
bool FJsEnvImpl::IdleNotificationDeadline(double DeadlineInSeconds)
{
#ifndef WITH_QUICKJS
    return MainIsolate->IdleNotificationDeadline(DeadlineInSeconds);
#else
    return true;  // ★ QuickJS 路径直接返回成功，不执行 V8 idle GC 语义
#endif
}

void FJsEnvImpl::RequestMinorGarbageCollectionForTesting()
{
#ifndef WITH_QUICKJS
    MainIsolate->RequestGarbageCollectionForTesting(v8::Isolate::kMinorGarbageCollection);
#endif
}

void FJsEnvImpl::RequestFullGarbageCollectionForTesting()
{
#ifndef WITH_QUICKJS
    MainIsolate->RequestGarbageCollectionForTesting(v8::Isolate::kFullGarbageCollection);
#endif
}
// ★ 说明 backend 切换不仅影响速度，也影响“你还能调哪些性能旋钮”
```

[2] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp:13-24,186-199`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp:12-29,81-87`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:170-193,260-266`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp
// 位置: 13-24, 186-199，运行时推进基本收敛到 UE ticker / subsystem
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
                CurrentEngine->Tick(DeltaTime); // ★ 没有外部 event loop，直接走 UE tick
            }
        }
    }
    return true;
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp
// 位置: 12-29, 81-87，把 PrimaryEngine 的 tick owner 明确挂到 GameInstanceSubsystem
// ============================================================================
void UAngelscriptGameInstanceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    PrimaryEngine = FAngelscriptEngine::TryGetCurrentEngine();
    if (PrimaryEngine == nullptr)
    {
        PrimaryEngine = &OwnedEngine;
        FAngelscriptEngineContextStack::Push(PrimaryEngine);
        OwnedEngine.Initialize();
        bOwnsPrimaryEngine = true;
    }
    if (PrimaryEngine != nullptr)
    {
        ++ActiveTickOwners;
    }
}

void UAngelscriptGameInstanceSubsystem::Tick(float DeltaTime)
{
    if (PrimaryEngine != nullptr && PrimaryEngine->ShouldTick())
    {
        PrimaryEngine->Tick(DeltaTime); // ★ 正常路径由子系统直接驱动脚本引擎前进
    }
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 170-193, 260-266，执行上下文通过池复用，而不是让 backend 再引入额外 runtime
// ============================================================================
static asCContext* TryTakeContextFromPool(TArray<asCContext*>& Pool, asIScriptEngine* DesiredScriptEngine)
{
    if (DesiredScriptEngine == nullptr)
    {
        return Pool.Pop(false);
    }

    for (int32 Index = Pool.Num() - 1; Index >= 0; --Index)
    {
        asCContext* Candidate = Pool[Index];
        if (Candidate != nullptr && Candidate->GetEngine() == DesiredScriptEngine)
        {
            Pool.RemoveAtSwap(Index, 1, EAllowShrinking::No);
            return Candidate; // ★ 直接从池里拿匹配 engine 的 context
        }
    }
    return nullptr;
}

static void ResetContextForPooling(asCContext* Context)
{
    check(Context->Unprepare() >= 0); // ★ 复用前只做上下文重置，不引入第二套事件循环
}
```

设计取舍：

- puerts 选择 Node backend 时能换来 JS 生态里的异步 I/O 与任务模型，但成本是多线程轮询链路和 backend-specific 的调参行为。
- Angelscript 把运行时推进权留在 UE 自己的 tick / subsystem / context pool 里，模型更统一，但也没有把 Node/libuv 这类宿主外 runtime 一并引进来。

与 Angelscript 对比：

| 对比点 | puerts 新证据 | Angelscript 新证据 | 差距判断 |
| --- | --- | --- | --- |
| runtime 推进者 | Node 路径是 `polling thread -> GameThread UvRunOnce()` | `GameInstanceSubsystem` 或 editor fallback 直接 `PrimaryEngine->Tick()` | 实现方式不同 |
| GC / idle 调参统一性 | `WITH_QUICKJS` 下 `IdleNotificationDeadline` 与 `Request*GC` 语义退化 | 当前检视路径下单引擎没有等价 backend 分叉 | puerts 在 backend 一致性能调参面上没有实现统一语义 |
| 执行上下文复用 | 当前新增检视路径以长期存活 isolate/context 为主 | `TryTakeContextFromPool()` + `ResetContextForPooling()` 明确存在 | 实现方式不同 |

### [维度 D11] 部署与打包：`puerts` 的 `.mbc/.cbc` 更像“可适配 V8 code cache”，`Angelscript` 的 `PrecompiledScript.Cache` 更像“严格失效的构建缓存”

已有 D11 已经说明 puerts 支持 `.mbc/.cbc`，但这一轮能把“它到底是什么”说得更具体。从 `modular.js` 与 `JsEnvImpl` 连起来看，`.mbc/.cbc` 的核心不是独立加密容器，而是 V8 code cache 的加载协议：loader 先从 bytecode header 里取 `SourceHash` 的长度位，生成一段同长度空白源码，再把 `CachedData` 交给 V8 `kConsumeCodeCache`。同时，宿主会先记录当前运行时的 `FlagHash` / `ReadOnlySnapshotChecksum`，加载时如果 header 不匹配还会就地修正。这说明当前 bytecode 路径首先追求的是“和当前 V8 快照兼容并保持调试/源码长度对齐”。“不是加密容器”这一点是基于源码行为的推断。

Angelscript 的 `PrecompiledScript.Cache` 目标更偏缓存一致性。`FAngelscriptPrecompiledData` 自己做二进制序列化，加载后先用 `BuildIdentifier` 判断是不是当前 build 配置，再用 `PrecompiledDataGuid` 对比编进二进制的 StaticJIT 信息；如果不匹配，就直接丢弃预编译数据或禁用 transpiled C++。它不去修补 cache header，而是明确走失效策略。

```
[puerts] Bytecode Load Contract
.mbc / .cbc
 -> read SourceHash length
 -> generate empty source with same length
 -> patch FlagHash / ReadOnlySnapshotChecksum
 -> V8 kConsumeCodeCache

[Angelscript] Precompiled Cache Contract
PrecompiledScript.Cache
 -> binary load into FAngelscriptPrecompiledData
 -> validate BuildIdentifier
 -> validate PrecompiledDataGuid against StaticJIT
 -> use or discard cache
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DefaultJSModuleLoader.cpp:67-82`、`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:79-155`、`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:674-680,3708-3738`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DefaultJSModuleLoader.cpp
// 位置: 67-82，bytecode 只是 loader 支持的一种 module 扩展名
// ============================================================================
return SearchModuleWithExtInDir(Dir, RequiredModule + ".js", Path, AbsolutePath) ||
       SearchModuleWithExtInDir(Dir, RequiredModule + ".mjs", Path, AbsolutePath) ||
       SearchModuleWithExtInDir(Dir, RequiredModule + ".cjs", Path, AbsolutePath) ||
#if defined(WITH_V8_BYTECODE)
       SearchModuleWithExtInDir(Dir, RequiredModule + ".mbc", Path, AbsolutePath) ||
       SearchModuleWithExtInDir(Dir, RequiredModule + ".cbc", Path, AbsolutePath) ||
#endif
       SearchModuleWithExtInDir(Dir, RequiredModule / "package.json", Path, AbsolutePath) ||
       SearchModuleWithExtInDir(Dir, RequiredModule / "index.js", Path, AbsolutePath);
// ★ loader 只是把 bytecode 当作 module 变体，不见额外签名校验入口
```

```js
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js
// 位置: 79-155，bytecode 加载时会重建“同长度空白源码”
// ============================================================================
function getSourceLengthFromBytecode(buf, isESM) {
    let sourceHash = (new Uint32Array(buf))[2];
    const kModuleFlagMask = (1 << 31);
    const mask = isESM ? kModuleFlagMask : 0;
    const length = sourceHash & ~mask;
    return length; // ★ 直接从 bytecode header 里恢复源码长度
}

function generateEmptyCode(length) {
    if (baseString === undefined) {
        baseString = " ".repeat(128*1024);
    }
    if (length <= baseString.length) {
        return baseString.slice(0, length);
    } else {
        const fullString = baseString.repeat(Math.floor(length / baseString.length));
        const remainingLength = length % baseString.length;
        return fullString.concat(baseString.slice(0, remainingLength));
    }
}

if (fullPath.endsWith(".mbc") || fullPath.endsWith(".cbc")) {
    bytecode = script;
    script = generateEmptyCode(getSourceLengthFromBytecode(bytecode));
    // ★ 不是解密源码，而是构造同长度占位源码给 V8 / 调试链路使用
}
```

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp
// 位置: 674-680, 3708-3738，宿主会把 bytecode header 调整到当前 V8 运行时期望值
// ============================================================================
const FCodeCacheHeader* CodeCacheHeader = (const FCodeCacheHeader*) CachedCode->data;
Expect_FlagHash = CodeCacheHeader->FlagHash;
#if V8_MAJOR_VERSION >= 11
Expect_ReadOnlySnapshotChecksum = CodeCacheHeader->ReadOnlySnapshotChecksum;
#endif

v8::ScriptCompiler::CachedData* CachedCode = nullptr;
if (FileName.EndsWith(TEXT(".mbc")))
{
    FCodeCacheHeader* CodeCacheHeader = (FCodeCacheHeader*) Data.GetData();
    if (CodeCacheHeader->FlagHash != Expect_FlagHash)
    {
        CodeCacheHeader->FlagHash = Expect_FlagHash; // ★ header 不匹配时直接修正
    }
#if V8_MAJOR_VERSION >= 11
    if (CodeCacheHeader->ReadOnlySnapshotChecksum != Expect_ReadOnlySnapshotChecksum)
    {
        CodeCacheHeader->ReadOnlySnapshotChecksum = Expect_ReadOnlySnapshotChecksum;
    }
#endif
    uint32_t Len = CodeCacheHeader->SourceHash & ~kModuleFlagMask;
    CachedCode = new v8::ScriptCompiler::CachedData(Data.GetData(), Data.Num());
    Options = v8::ScriptCompiler::CompileOptions::kConsumeCodeCache;
    Source = Ret.As<v8::String>();
}
// ★ 这里看到的重点是 code cache 兼容和调试占位，不是额外加密校验
```

[2] `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp:2642-2690` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1529-1556,1583-1603`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp
// 位置: 2642-2690，预编译缓存先按 build 配置验证，再做二进制读写
// ============================================================================
bool FAngelscriptPrecompiledData::IsValidForCurrentBuild()
{
    return BuildIdentifier == GetCurrentBuildIdentifier() && BuildIdentifier != -1;
}

void FAngelscriptPrecompiledData::Save(const FString& Filename)
{
    FMemoryWriter Writer(Data, true);
    Writer.SetIsPersistent(true);
    Writer.SetWantBinaryPropertySerialization(true);
    Writer << *this;
    FFileHelper::SaveArrayToFile(Data, *Filename);
}

void FAngelscriptPrecompiledData::Load(const FString& Filename)
{
    FFileHelper::LoadFileToArray(LoadedData, *Filename);
    FMemoryReaderWithPtr Reader(LoadedData);
    Reader.SetIsPersistent(true);
    Reader.SetWantBinaryPropertySerialization(true);
    Reader << *this;
}
// ★ 缓存契约首先是“这个二进制是不是当前 build 能吃”，而不是加载时修补 header
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 1529-1556, 1583-1603，build/guid 不匹配时直接废弃缓存或禁用 transpiled code
// ============================================================================
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
        FJITDatabase::Get().Clear(); // ★ 不修补，直接禁用不匹配的 transpiled code
    }
}

if (bGeneratePrecompiledData)
{
    PrecompiledData->InitFromActiveScript();
    PrecompiledData->Save(Filename);
}

if (!bScriptDevelopmentMode && !bGeneratePrecompiledData)
    PrecompiledData->ClearUnneededRuntimeData();
// ★ 运行期会主动裁掉不再需要的数据，目标是确定性缓存与内存回收
```

设计取舍：

- puerts 的 bytecode 路径更像“和当前 V8 快照协商后再消费 code cache”，便于保留调试映射和多模块加载语义，但也更依赖 loader 端的兼容修补。
- Angelscript 的预编译缓存更像“版本不对就直接失效”，配合 `PrecompiledDataGuid` 约束 transpiled C++，换来的是更强的构建确定性。

与 Angelscript 对比：

| 对比点 | puerts 新证据 | Angelscript 新证据 | 差距判断 |
| --- | --- | --- | --- |
| bytecode / cache 的本体 | `.mbc/.cbc` 进入 `CachedData`，并重建同长度空白源码 | `PrecompiledScript.Cache` 进入 `FAngelscriptPrecompiledData` 二进制结构 | 实现方式不同 |
| 不兼容时的策略 | 修补 `FlagHash` / `ReadOnlySnapshotChecksum` 后继续尝试消费 | `BuildIdentifier` / `PrecompiledDataGuid` 不匹配就丢弃或禁用 | 实现方式不同 |
| 运行期目标 | 保留 code cache 兼容与调试占位 | 强调构建配置一致性和缓存裁剪 | 实现方式不同 |

---

## 深化分析 (2026-04-08 19:45:13)

### [维度 D1] 插件架构与模块划分：`puerts` 的运行时拥有者是“场景化重建”的，`JsEnvGroup` 只完整覆盖 `TypeScriptGeneratedClass` 主链

前面的 D1 已经分析过模块数量、`pesapi` ABI 和 addon SDK；这一轮补的是“谁拥有脚本运行时，以及这个拥有权在什么时机切换”。`puerts` 的 UE 插件层并没有常驻唯一 `JsEnv` 的承诺。`StartupModule()` 先补读 `DefaultPuerts.ini`，再根据 `AutoModeEnable` 决定是否 `Enable()`；进入 PIE、UE C++ Hot Reload 完成、Standalone 运行这些节点都可能触发 `MakeSharedJsEnv()`。对应地，退出 PIE 时它会显式 `JsEnv.Reset()`，并遍历所有 `UTypeScriptGeneratedClass` 取消重定向、恢复 `ClassConstructor`。

更细一点看，`MakeSharedJsEnv()` 不只是“开一个或多个 VM”这么简单。它会根据 `NumberOfJsEnv` 在单实例 `FJsEnv` 与多实例 `FJsEnvGroup` 之间切换；但 `FGroupDynamicInvoker` 只对 `TsConstruct / InvokeTsMethod / NotifyReBind / InvokeMixinMethod` 提供真实实现，`InvokeDelegateCallback / JsConstruct / InvokeJsMethod` 直接 `ensureMsgf(false)`。这说明 `puerts` 的多 `JsEnv` 模式本质上是给 `UTypeScriptGeneratedClass` 这条 TypeScript 生成类路径做对象分片，而不是把所有 JS 运行时能力无损横向扩成 N 份。

Angelscript 的拥有权模型更稳定。`FAngelscriptRuntimeModule::InitializeAngelscript()` 会把主引擎压到 `FAngelscriptEngineContextStack`，没有现成 engine 就创建 `OwnedPrimaryEngine`；真正的运行时 tick owner 再由 `UAngelscriptGameInstanceSubsystem` 接管，只有在没有任何 subsystem owner 时才回落到 editor fallback ticker。和 puerts 相比，它牺牲了多环境分片能力，但换来了“主引擎始终是谁拥有”的确定性。

```
[puerts] Runtime Ownership And Sharding
StartupModule
├─ RegisterSettings()                     // 先补读 DefaultPuerts.ini
├─ AutoModeEnable ? Enable()             // 可选自动启动
├─ PIE begin / HotReload complete
│  └─ MakeSharedJsEnv()                  // 按场景重建运行时
└─ PIE end
   └─ Reset JsEnv + cancel TS redirection

MakeSharedJsEnv()
├─ NumberOfJsEnv == 1
│  └─ FJsEnv                             // 单实例
└─ NumberOfJsEnv > 1
   └─ FJsEnvGroup                        // 多实例分片
      ├─ selector -> InvokeTsMethod      // 只分发 TS 生成类主链
      ├─ CDO -> TsConstruct on all envs  // CDO 广播到所有 env
      └─ delegate/js construct => unsupported

[Angelscript] Primary Engine Ownership
StartupModule
├─ InitializeAngelscript()               // 初始化或复用主引擎
├─ GameInstanceSubsystem::Initialize()   // 建立 tick owner
└─ TickFallbackPrimaryEngine()           // 仅在无 owner 时兜底
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:185-243,405-466`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp
// 函数: FPuertsModule::MakeSharedJsEnv / StartupModule / Enable
// 位置: 185-243, 405-466，按场景决定创建单实例还是 Group 模式，并在 PIE / AutoMode 下反复重建
// ============================================================================
NumberOfJsEnv = (Settings.NumberOfJsEnv > 1 && Settings.NumberOfJsEnv < 10) ? Settings.NumberOfJsEnv : 1;

if (NumberOfJsEnv > 1)
{
    JsEnvGroup = MakeShared<PUERTS_NAMESPACE::FJsEnvGroup>(NumberOfJsEnv, Settings.RootPath);
    if (Selector)
    {
        JsEnvGroup->SetJsEnvSelector(Selector); // ★ 多 env 时才有对象分片 selector
    }

    if (Settings.WaitDebugger)
    {
        UE_LOG(PuertsModule, Warning, TEXT("Do not support WaitDebugger in Group Mode!"));
    }

    JsEnvGroup->RebindJs();
}
else
{
    JsEnv = MakeShared<PUERTS_NAMESPACE::FJsEnv>(Settings.RootPath);
    if (Settings.WaitDebugger)
    {
        JsEnv->WaitDebugger(Settings.WaitDebuggerTimeout);
    }
    JsEnv->RebindJs();
}

if (Settings.AutoModeEnable)
{
    Enable(); // ★ 是否自动拥有运行时由配置决定，不是模块加载即必启
}

GUObjectArray.AddUObjectCreateListener(static_cast<FUObjectArray::FUObjectCreateListener*>(this));
// ★ Listener 要早于 Rebind 触发对象加载，否则会漏掉对象注入
```

[2] `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvGroup.cpp:18-80,119-181`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvGroup.cpp
// 类: FGroupDynamicInvoker
// 位置: 18-80, 119-181，多 JsEnv 分片的真实能力边界
// ============================================================================
void InvokeTsMethod(UObject* ContextObject, UFunction* Function, FFrame& Stack, void* RESULT_PARAM) override
{
    JsEnvs[GetSelectIndex(ContextObject)]->InvokeTsMethod(ContextObject, Function, Stack, RESULT_PARAM);
}

void NotifyReBind(UTypeScriptGeneratedClass* Class) override
{
    for (int i = 0; i < JsEnvs.size(); i++)
    {
        JsEnvs[i]->NotifyReBind(Class); // ★ TS 类重绑会广播到所有 env
    }
}

virtual void InvokeDelegateCallback(UDynamicDelegateProxy* Proxy, void* Params) override
{
    ensureMsgf(false, TEXT("InvokeDelegateCallback in GroupDynamicInvoker"));
}

virtual void JsConstruct(UClass* Class, UObject* Object, const v8::UniquePersistent<v8::Function>& Constructor,
    const v8::UniquePersistent<v8::Object>& Prototype) override
{
    ensureMsgf(false, TEXT("JsConstruct in GroupDynamicInvoker"));
}

virtual void InvokeJsMethod(UObject* ContextObject, UJSGeneratedFunction* Function, FFrame& Stack, void* RESULT_PARAM) override
{
    ensureMsgf(false, TEXT("InvokeJsMethod in GroupDynamicInvoker"));
}

JsEnvs[i]->TsDynamicInvoker = GroupDynamicInvoker;
JsEnvs[i]->MixinInvoker = GroupDynamicInvoker;
// ★ 这里只给 Ts/Mixin 路径挂 group invoker，没有把所有 JS 入口都做成分片安全
```

[3] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp:13-25,138-165` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp:12-47,81-87`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp
// 函数: FAngelscriptRuntimeModule::StartupModule / InitializeAngelscript
// 位置: 13-25, 138-165，主引擎初始化与 editor fallback ticker
// ============================================================================
void FAngelscriptRuntimeModule::StartupModule()
{
    if (GIsEditor || IsRunningCommandlet())
    {
        InitializeAngelscript(); // ★ 模块启动期先确保有 primary engine
    }

    if (GIsEditor)
    {
        FallbackTickHandle = FTSTicker::GetCoreTicker().AddTicker(
            FTickerDelegate::CreateRaw(this, &FAngelscriptRuntimeModule::TickFallbackPrimaryEngine));
    }
}

if (FAngelscriptEngine* CurrentEngine = FAngelscriptEngine::TryGetCurrentEngine())
{
    CurrentEngine->Initialize();
}
else
{
    OwnedPrimaryEngine = MakeUnique<FAngelscriptEngine>();
    FAngelscriptEngineContextStack::Push(OwnedPrimaryEngine.Get());
    OwnedPrimaryEngine->Initialize(); // ★ 没有现成 engine 时由 RuntimeModule 持有一个
}

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp
// 函数: UAngelscriptGameInstanceSubsystem::Initialize / Deinitialize / Tick
// 位置: 12-47, 81-87，把运行时推进权稳定挂到 subsystem owner 上
// ============================================================================
PrimaryEngine = FAngelscriptEngine::TryGetCurrentEngine();
if (PrimaryEngine == nullptr)
{
    PrimaryEngine = &OwnedEngine;
    FAngelscriptEngineContextStack::Push(PrimaryEngine);
    OwnedEngine.Initialize();
    bOwnsPrimaryEngine = true;
}
if (PrimaryEngine != nullptr)
{
    ++ActiveTickOwners; // ★ 用 owner 计数控制谁来推进主引擎
}

if (bOwnsPrimaryEngine)
{
    FAngelscriptEngineContextStack::Pop(PrimaryEngine);
    PrimaryEngine->Shutdown();
}

if (PrimaryEngine != nullptr && PrimaryEngine->ShouldTick())
{
    PrimaryEngine->Tick(DeltaTime);
}
```

设计取舍：

- `puerts` 把运行时拥有权做成“按 AutoMode / PIE / HotReload 重建”的可切换模型，适合调试与多 `JsEnv` 分片，但对象生命周期和 redirection 清理成本更高。
- `JsEnvGroup` 给 `TypeScriptGeneratedClass` 提供了分片能力，却没有把 delegate callback、普通 `JSGeneratedClass` 构造和 `InvokeJsMethod` 一并做成 group-safe，这说明它追求的是特定主链的扩展，而不是全栈一致性。
- Angelscript 把主引擎 ownership 固定在 `RuntimeModule + EngineContextStack + GameInstanceSubsystem` 三层里，扩展性不如 `JsEnvGroup`，但运行时边界更稳。

与 Angelscript 对比：

| 对比点 | puerts 新证据 | Angelscript 新证据 | 差距判断 |
| --- | --- | --- | --- |
| 运行时拥有者 | `StartupModule / PIE / HotReload` 均可能触发 `MakeSharedJsEnv()` | `InitializeAngelscript()` 建立 primary engine，`GameInstanceSubsystem` 接管 tick | 实现方式不同 |
| 多运行时分片 | `NumberOfJsEnv` + `SetJsEnvSelector()` 可做对象分片 | 当前检视路径无等价 multi-engine sharding | Angelscript 没有实现 |
| 分片模式能力一致性 | `InvokeDelegateCallback / JsConstruct / InvokeJsMethod` 在 group mode 明确不支持 | 单主引擎路径没有第二套能力矩阵 | 实现质量差异 |

### [维度 D2] 反射绑定机制：`puerts` 的 Delegate 暴露是动态桥接对象，`Angelscript` 的 Delegate 暴露是签名化脚本类型

前面的 D2 主要分析了类/函数绑定 DSL；这一轮往下钻到 Delegate。`puerts` 对 Delegate 的处理更像“把 UE 原生委托包装成 JS 对象”。`FDelegateWrapper` / `FMulticastDelegateWrapper` 暴露 `Bind / Add / Execute / Broadcast`；`FDelegatePropertyTranslator` 又允许把 JS `function`、已有 delegate wrapper，甚至 `[UObject, string/function]` 这样的数组形态写回 `FScriptDelegate`。真正的 JS 回调落地则依赖 `UDynamicDelegateProxy::ProcessEvent()`，它把 UE delegate 调用转回 `InvokeDelegateCallback()`，再由 `FFunctionTranslator(SignatureFunction, true)` 完成参数编组。

这条链的一个关键特征是“运行时动态性优先”。在 `Bind(object, "Func")` / `Add(object, "Func")` 这条路径里，插件层直接调用 `Delegate.BindUFunction(...)` 或 `AddUnique(...)`，当前检视代码没有 Angelscript 那种显式的签名兼容性前置校验。另一边，如果 JS `function` 被复用到不同 `SignatureFunction`，`NewDelegate()` 会检测到旧 proxy 的签名不一致并拒绝继续绑定。这说明 puerts 的安全边界主要压在 proxy + translator 运行时，而不是绑定入口的静态契约上。

Angelscript 则相反。它先把每个 `UDelegateFunction` 注册成 `FScriptDelegateType` / `FMulticastScriptDelegateType`，把签名塞进类型系统；`BindUFunction()` 会先找目标 `UFunction`，再调用 `CheckAngelscriptDelegateCompatibility(...)` 做兼容性检查，不通过就直接抛出带签名文本的错误。即便绑定成功，`Bind_BlueprintEvent.cpp` 在真正 `ExecuteDelegate` / `ExecuteMulticastDelegate` 前还会再次 `ValidateAgainstFunction(...)`。也就是说，Angelscript 把 delegate 视为“有签名契约的脚本类型”，而不只是一个可晚绑定的宿主句柄。

```
[puerts] Delegate Runtime Bridge
FDelegateProperty / FMulticastDelegateProperty
├─ DelegateWrapper.Bind/Add               // JS 侧入口
├─ PropertyTranslator.JsToUE              // function / [object, name] / wrapper
├─ UDynamicDelegateProxy::ProcessEvent    // UE 回调转发点
└─ FFunctionTranslator(SignatureFunction) // 运行时按签名编组

[Angelscript] Delegate Typed Contract
UDelegateFunction
├─ DeclareDelegate()                      // 注册为脚本类型
├─ FScriptDelegateType / FMulticast...    // 类型上携带 Signature
├─ BindUFunction()                        // 绑定前兼容性检查
└─ __Evt_ExecuteDelegate                  // 执行前再次验证
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DelegateWrapper.cpp:45-76,120-165` 与 `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1063-1125`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DelegateWrapper.cpp
// 函数: FDelegateWrapper::Bind / FMulticastDelegateWrapper::Add
// 位置: 45-76, 120-165，Delegate 的 JS 侧入口
// ============================================================================
if (Info.Length() == 1 && Info[0]->IsFunction())
{
    FV8Utils::IsolateData<IObjectMapper>(Isolate)->AddToDelegate(
        Isolate, Context, DelegatePtr, v8::Local<v8::Function>::Cast(Info[0]));
    return; // ★ 直接接 JS function，后续靠 proxy + translator 处理
}
if (Info.Length() == 2 && Info[0]->IsObject() && Info[1]->IsString())
{
    auto DelegatePtr = FV8Utils::GetPointerFast<FScriptDelegate>(Info.Holder(), 0);
    FScriptDelegate Delegate;
    Delegate.BindUFunction(Object, FName(*FV8Utils::ToFString(Isolate, Info[1])));
    *DelegatePtr = Delegate; // ★ 这里直接写入 UObject + FName，没有插件侧签名校验
    return;
}

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp
// 类: FDelegatePropertyTranslator
// 位置: 1063-1125，JS -> UE 的委托赋值支持多种动态输入形态
// ============================================================================
if (Value->IsFunction())
{
    *Des = FV8Utils::IsolateData<IObjectMapper>(Isolate)->NewDelegate(
        Isolate, Context, nullptr, Value.As<v8::Function>(), DelegateProperty->SignatureFunction);
}
else if (Des && Value->IsArray())
{
    auto Obj = FV8Utils::GetUObject(Context, Array->Get(Context, 0).ToLocalChecked());
    auto Func = Array->Get(Context, 1).ToLocalChecked();
    if (Func->IsString())
    {
        FScriptDelegate Delegate;
        Delegate.BindUFunction(Obj, *FV8Utils::ToFString(Isolate, Func)); // ★ [UObject, "Func"] 也合法
        *Des = Delegate;
    }
    else if (Func->IsFunction())
    {
        *Des = FV8Utils::IsolateData<IObjectMapper>(Isolate)->NewDelegate(
            Isolate, Context, Obj, Func.As<v8::Function>(), DelegateProperty->SignatureFunction);
    }
}
```

[2] `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:1927-1957,2402-2531,2545-2602` 与 `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DynamicDelegateProxy.cpp:25-36`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp
// 函数: InvokeDelegateCallback / AddToDelegate / NewDelegate
// 位置: 1927-1957, 2402-2531, 2545-2602，JS function 最终通过 proxy 回进 UE delegate
// ============================================================================
JsCallbackPrototypeMap[SignatureFunction.Get()] = std::make_unique<FFunctionTranslator>(SignatureFunction.Get(), true);
// ★ 回调参数编组完全依赖 SignatureFunction 对应的 translator

DelegateProxy = NewObject<UDynamicDelegateProxy>();
DelegateProxy->Owner = Iter->second.Owner;
DelegateProxy->SignatureFunction = Iter->second.SignatureFunction;
DelegateProxy->DynamicInvoker = DynamicInvoker;

FScriptDelegate Delegate;
Delegate.BindUFunction(DelegateProxy, NAME_Fire);
*(static_cast<FScriptDelegate*>(DelegatePtr)) = Delegate;
// ★ 真正挂进 UE 的不是原 JS function，而是代理 UObject 上的 Fire

if (DelegateProxy->SignatureFunction.Get() != SignatureFunction)
{
    Logger->Error(TEXT("aleady bind to another delegate pleace release first!"));
    DelegateProxy = nullptr; // ★ 同一 JS function 不能复用到另一种签名
}

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DynamicDelegateProxy.cpp
// 函数: UDynamicDelegateProxy::ProcessEvent
// 位置: 25-36，UE delegate 触发时把参数反向送回 JS
// ============================================================================
auto PinedDynamicInvoker = DynamicInvoker.Pin();
if (PinedDynamicInvoker && Owner.IsValid())
{
    if (ensureAlwaysMsgf(!JsFunction.IsEmpty(), TEXT("Invalid JS Function")))
    {
        PinedDynamicInvoker->InvokeDelegateCallback(this, Params); // ★ 统一回到 translator + JS function
    }
}
```

[3] `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.cpp:57-113,432-631` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp:287-329,475-486`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.cpp
// 函数: DeclareDelegate / FAngelscriptDelegateOperations::BindUFunction
// 位置: 57-113, 432-631，先把 delegate 做成有签名的脚本类型，再校验绑定兼容性
// ============================================================================
struct FScriptDelegateType : TAngelscriptCppType<FScriptDelegate>
{
    FORCEINLINE UDelegateFunction* GetSignature(const FAngelscriptTypeUsage& Usage) const
    {
        return Function != nullptr ? Function : (UDelegateFunction*) Usage.ScriptClass->GetUserData();
    }
    // ★ delegate type 自身就携带 signature
};

FAngelscriptType::Register(MakeShared<FScriptDelegateType>(Decl, Function));
auto Delegate_ = FAngelscriptBinds::ValueClass<FScriptDelegate>(Decl, BindFlags);

UFunction* CallFunction = InObject->FindFunction(InFunctionName);
if (!CheckAngelscriptDelegateCompatibility(Ops->SignatureFunction, CallFunction))
{
    FString Message = FString::Printf(TEXT("Specified function is not compatible with delegate function.\n\nDelegate: %s\n\nAttempted Bind: %s"),
        *GetSignatureStringForFunction(Ops->SignatureFunction), *GetSignatureStringForFunction(CallFunction));
    FAngelscriptEngine::Throw(TCHAR_TO_ANSI(*Message)); // ★ 绑定时就抛出签名不兼容错误
    return;
}

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp
// 函数: FScriptCall::ExecuteDelegate / ExecuteMulticastDelegate
// 位置: 287-329, 475-486，执行前再次验证每个已绑定函数的签名
// ============================================================================
UFunction* BoundFunction = BoundObject != nullptr ? BoundObject->FindFunction(Delegate.GetFunctionName()) : nullptr;
if (!ValidateAgainstFunction(BoundFunction, ValidationError))
{
    AbortExecution(ValidationError);
    return; // ★ 不是盲目 ProcessDelegate，而是先核对签名
}

if (!TryExtractMulticastFunctionNames(Delegate, BoundFunctionNames) || BoundObjects.Num() != BoundFunctionNames.Num())
{
    AbortExecution(TEXT("Signature mismatch while executing multicast delegate: failed to resolve bound functions."));
    return;
}

FAngelscriptBinds::BindGlobalFunction("void __Evt_ExecuteDelegate(const _FScriptDelegate& Delegate)",
[](FScriptDelegate& Delegate)
{
    CurrentCall().ExecuteDelegate(Delegate); // ★ 最终执行也走统一验证入口
});
```

设计取舍：

- `puerts` 的委托桥接更灵活，JS `function`、`[UObject, "Func"]`、现有 delegate wrapper 都能在运行时塞进 `FScriptDelegate`。
- 这种灵活性换来的代价是：在当前检视路径里，`UObject + FName` 绑定缺少 Angelscript 那种显式签名预检，错误更容易推迟到回调或执行期。
- Angelscript 把 delegate 变成带 `SignatureFunction` 的正式脚本类型，并在“绑定前”和“执行前”各做一次验证，诊断链更前置、更强约束。

与 Angelscript 对比：

| 对比点 | puerts 新证据 | Angelscript 新证据 | 差距判断 |
| --- | --- | --- | --- |
| Delegate 暴露形态 | `DelegateWrapper` / `PropertyTranslator` 以运行时 wrapper 为主 | `DeclareDelegate()` 注册 `FScriptDelegateType` / `FMulticastScriptDelegateType` | 实现方式不同 |
| 绑定前兼容性检查 | `Bind(object, "Func")` / `Add(object, "Func")` 直接 `BindUFunction` / `AddUnique` | `CheckAngelscriptDelegateCompatibility(...)` 不通过即抛错 | 实现质量差异 |
| JS/脚本回调落地 | `UDynamicDelegateProxy -> InvokeDelegateCallback -> FFunctionTranslator` | `__Evt_ExecuteDelegate -> ValidateAgainstFunction -> ProcessDelegate` | 实现方式不同 |

### [维度 D6] 代码生成与 IDE 支持：`puerts` 用声明层保留“超集类型形状”，`Angelscript` 用签名重写得到最终脚本表面

前面的 D6 已经覆盖 `.d.ts` 产物、LanguageService 和 Blueprint 回写；这一轮补的是“当类型表面和真实绑定表面不完全重合时，两边如何处理”。`puerts` 的 `DeclarationGenerator` 并不是简单遍历反射对象后直接写文件。它先把运行时模板绑定 (`FindClassByType`) 和 `ExtensionMethodsMap` 收集到 `FunctionOutputs`，再通过 `TryToAddOverload()` 去重。真正落盘时，`GenResolvedFunctions()` 不只写当前类自己的 overload，还会继续向上遍历父类；只要父类有而当前类没有，它就以 `@deprecated Unsupported super overloads.` 的形式保留在 `.d.ts` 里。

这个细节很关键。它说明 puerts 的声明层目标不是“只输出当前真正可调的最小集合”，而是尽量保留继承树上的完整类型形状，再用 `deprecated` 明确告诉 IDE 和用户哪些是看得见但不建议依赖的父类 overload。换句话说，puerts 在 IDE 侧选择的是“可见性优先，风险靠注释标识”。

Angelscript 的策略更像“在进入脚本世界之前就把表面改写成最终形状”。`Helper_FunctionSignature.h` 在分析静态 `UFUNCTION` 时，如果宿主类带 `ScriptMixin` metadata 且首参数类型匹配，它会直接删掉首参数，把 `bStaticInScript` 改成 `false`，必要时同步修正 `WorldContextArgument` 和 `DeterminesOutputTypeArgument`，最后重新 `BuildFunctionDeclaration(...)`。这意味着脚本编译器和调试数据库看到的是已经重写过的最终成员函数签名，而不是另起一层“保留父类/保留原型”的离线声明超集。

```
[puerts] Declaration Shape Preservation
runtime class registry
├─ GatherExtensions()                     // 收模板绑定 / 扩展方法 / 变量
├─ TryToAddOverload()                     // 文本级去重
└─ GenResolvedFunctions()
   ├─ emit tooltip
   ├─ emit current overloads
   └─ emit missing super overloads as deprecated

[Angelscript] Signature Rewriting
UFUNCTION metadata
└─ Helper_FunctionSignature
   ├─ detect ScriptMixin
   ├─ strip first mixin argument
   ├─ static -> member
   └─ BuildFunctionDeclaration()          // 直接得到最终脚本签名
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1100-1169`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp
// 函数: TryToAddOverload / GatherExtensions
// 位置: 1100-1169，声明生成先聚合模板绑定与扩展方法，再做文本级 overload 去重
// ============================================================================
void FTypeScriptDeclarationGenerator::TryToAddOverload(
    FunctionOutputs& Outputs, const FString& FunctionName, bool IsStatic, const FString& Overload)
{
    FunctionOverloads& Overloads = GetFunctionOverloads(Outputs, FunctionName, IsStatic);
    if (!Overloads.Contains(Overload))
    {
        Overloads.Add(Overload); // ★ 同一个文本签名只保留一份
    }
}

auto ClassDefinition = PUERTS_NAMESPACE::FindClassByType(Struct);
if (ClassDefinition)
{
    GenTemplateBindingFunction(Tmp, FunctionInfo, true);
    TryToAddOverload(Outputs, FunctionInfo->Name, true, Tmp.Buffer);
    // ★ runtime class registry 里的 template binding 会直接进入 .d.ts 形状

    Buff << "    " << PropertyInfo->Name << ": " << GetNamePrefix(PropertyInfo->Type) << PropertyInfo->Type->Name() << ";\n";
    Buff << "    static " << (ClassDefinition->Variables[Pos].Setter ? "" : "readonly ") << VariableInfo->Name << ": "
         << GetNamePrefix(VariableInfo->Type) << VariableInfo->Type->Name() << ";\n";
}

if (!GenFunction(Tmp, Function, true, false, false, true))
{
    continue;
}
TryToAddOverload(Outputs, Function->GetName(), false, Tmp.Buffer);
// ★ 扩展方法也不是单独文件，而是并入同一份 overload 表
```

[2] `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1174-1224`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp
// 函数: GenResolvedFunctions
// 位置: 1174-1224，当前类 overload 写完后，还会补写“父类存在但当前类未支持”的签名
// ============================================================================
for (FunctionOverloads::RangedForIteratorType OverloadIter = Overloads.begin(); OverloadIter != Overloads.end(); ++OverloadIter)
{
    Buff << "    " << *OverloadIter << ";\n";
}

UStruct* SuperStruct = Struct->GetSuperStruct();
while (SuperStruct != nullptr)
{
    FunctionOutputs& SuperOutputs = GetFunctionOutputs(SuperStruct);
    FunctionOutputs::iterator SuperOutputsIter = SuperOutputs.find(FunctionKey);
    if (SuperOutputsIter != SuperOutputs.end())
    {
        for (FunctionOverloads::RangedForIteratorType SuperOverloadIter = SuperOverloads.begin();
             SuperOverloadIter != SuperOverloads.end(); ++SuperOverloadIter)
        {
            if (!Overloads.Contains(*SuperOverloadIter))
            {
                Buff << "    /**\n";
                Buff << "     * @deprecated Unsupported super overloads.\n";
                Buff << "     */\n";
                Buff << "    " << *SuperOverloadIter << ";\n";
                // ★ 不静默丢弃父类签名，而是保留为“可见但不建议依赖”的 IDE 形状
            }
        }
    }
    SuperStruct = SuperStruct->GetSuperStruct();
}
```

[3] `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h:273-323`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h
// 位置: 273-323，ScriptMixin 会在签名构建阶段被直接改写成实例方法
// ============================================================================
bool bFoundMixin = false;
const FString& MixinClasses = Function->GetOuterUClass()->GetMetaData(NAME_Signature_ScriptMixin);
if (MixinClasses.Len() != 0 && ArgumentTypes.Num() > 0
    && (ArgumentTypes[0].IsObjectPointer() || ArgumentTypes[0].bIsReference))
{
    FString FirstParamType = ArgumentTypes[0].Type->GetAngelscriptTypeName();
    for (const FString& Mixin : MixinList)
    {
        if (FirstParamType == Mixin)
        {
            ArgumentTypes.RemoveAt(0);
            ArgumentNames.RemoveAt(0);
            ArgumentDefaults.RemoveAt(0);
            ClassName = Mixin;

            bStaticInScript = false;
            bFoundMixin = true;
            // ★ 把“静态函数库 + 首参 self”改写成脚本侧实例成员函数

            if (WorldContextArgument >= 0)
                WorldContextArgument -= 1;
            if (DeterminesOutputTypeArgument >= 0)
                DeterminesOutputTypeArgument -= 1;
            break;
        }
    }
}

Declaration = FAngelscriptType::BuildFunctionDeclaration(ReturnType, ScriptName, ArgumentTypes, ArgumentNames, ArgumentDefaults,
    (Function->HasAnyFunctionFlags(FUNC_Const) && !bStaticInScript) || bForceConst);
// ★ 脚本编译器看到的已经是最终形状，不再保留一层“父类超集声明”
```

设计取舍：

- `puerts` 的声明层会保留模板绑定、扩展方法和父类未完全支持的 overload，优先保证 IDE 看见的类型形状尽量完整。
- 这种策略对智能提示和迁移友好，但声明文件里会存在“可见但受限”的 API，理解成本靠 `deprecated` 注释承担。
- Angelscript 选择在签名构建阶段就把 `ScriptMixin` 等语义折叠成最终脚本表面，不额外维护一层超集声明，因此脚本表面更确定，但 IDE 侧少了一个显式的“保留原型/父类 overload”缓冲层。

与 Angelscript 对比：

| 对比点 | puerts 新证据 | Angelscript 新证据 | 差距判断 |
| --- | --- | --- | --- |
| 扩展能力进入类型表面的方式 | `FindClassByType()` + `ExtensionMethodsMap` 聚合后写入 overload 表 | `ScriptMixin` 在签名分析阶段直接改写成实例成员 | 实现方式不同 |
| 父类 overload 保留策略 | `@deprecated Unsupported super overloads.` 明确保留不可完全支持的父类签名 | 当前检视路径未见等价离线声明保留层，直接产出最终脚本声明 | 实现方式不同 |
| IDE 事实源的目标 | 倾向保留“超集类型形状”供 IDE 连续感知 | 倾向直接暴露脚本编译器的最终表面 | 实现方式不同 |

---

## 深化分析 (2026-04-08 23:16:30)

### [维度 D5] 调试与开发体验：`puerts` 把 DevTools 协议桥接进脚本运行时，`Angelscript` 把调试协议做成插件自有二进制契约

前面的 D5 已经覆盖了“复用 V8 Inspector”与“自定义调试协议”这层表面差异；这一轮继续往协议边界里看，能看到两边真正拥有的东西完全不同。`puerts` 在 UE 插件层拥有的是“把 Inspector 流量送进 JS VM”的桥，而不是一套由插件自己定义的调试消息模型。`QuickJS` 路径在 `Build.cs` 里直接定义 `WITHOUT_INSPECTOR`，`Group Mode` 也明确不支持 `WaitDebugger`，所以它的调试能力天然跟 backend 绑定。

`Angelscript` 则把调试面做成插件自己的 TCP 协议。`EDebugMessageType` 枚举直接定义了 `SetBreakpoint`、`RequestVariables`、`RequestEvaluate`、`CreateBlueprint`、`SetDataBreakpoints` 等消息，`SerializeDebugMessageEnvelope()` 负责统一封包，`FAngelscriptDebugServer` 再把这些消息落到断点、数据断点、资产数据库、定义跳转等运行时动作上。它不是接 DevTools，而是在维护一份自己的调试契约。

更重要的是，Angelscript 还把这份契约纳入自动化测试。`AngelscriptDebugProtocolTests.cpp` 对 `StartDebugging`、`DebugServerVersion`、`Breakpoint`、`Variables`、`DataBreakpoints` 等消息做 round-trip 校验，说明它不仅“有协议”，而且把协议兼容性也作为插件能力的一部分来维护。`puerts` 当前插件层则更像是“借 V8/Node 的前端生态”，协议语义主要由 Inspector 决定，插件只负责接线。

```
[puerts] Debug Transport Ownership
Chrome DevTools
 -> /json/list + websocket                 // 使用 Inspector 发现与连接
 -> V8InspectorChannel
 -> __tgjsSetInspectorCallback / __tgjsDispatchProtocolMessage
 -> JS VM backend
    ├─ V8 / Node.js                        // 支持 Inspector
    └─ QuickJS                             // 编译期去掉 Inspector

[Angelscript] Plugin-Owned Debug Protocol
Debug Client
 -> TCP listener
 -> SerializeDebugMessageEnvelope          // 插件自定义封包
 -> HandleMessage()
    ├─ breakpoints / stepping
    ├─ variables / evaluate
    ├─ create blueprint / asset database
    └─ data breakpoints
 -> automation round-trip tests
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs:624-632` 与 `Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:194-239`

```csharp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs
// 位置: 624-632，QuickJS 路径直接编译掉 Inspector
// ============================================================================
void ThirdPartyQJS(ReadOnlyTargetRules Target)
{
    PrivateDefinitions.Add("WITHOUT_INSPECTOR");
    PrivateDefinitions.Add("WITH_QUICKJS");   // ★ backend 切到 QuickJS 时，Inspector 能力一起裁掉
    if (QjsNamespaceSuffix)
    {
        PublicDefinitions.Add("QJSV8NAMESPACE=v8_qjs");
    }
}
```

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp
// 函数: FPuertsModule::MakeSharedJsEnv
// 位置: 194-239，调试器等待能力还受运行时模式约束
// ============================================================================
if (NumberOfJsEnv > 1)
{
    if (Settings.DebugEnable)
    {
        JsEnvGroup = MakeShared<PUERTS_NAMESPACE::FJsEnvGroup>(NumberOfJsEnv,
            std::make_shared<PUERTS_NAMESPACE::DefaultJSModuleLoader>(Settings.RootPath),
            std::make_shared<PUERTS_NAMESPACE::FDefaultLogger>(),
            DebuggerPortFromCommandLine < 0 ? Settings.DebugPort : DebuggerPortFromCommandLine);
    }

    if (Settings.WaitDebugger)
    {
        UE_LOG(PuertsModule, Warning, TEXT("Do not support WaitDebugger in Group Mode!"));
        // ★ 多 JsEnv 模式不支持等待调试器接入
    }
}
else
{
    if (Settings.DebugEnable)
    {
        JsEnv = MakeShared<PUERTS_NAMESPACE::FJsEnv>(
            std::make_shared<PUERTS_NAMESPACE::DefaultJSModuleLoader>(Settings.RootPath),
            std::make_shared<PUERTS_NAMESPACE::FDefaultLogger>(),
            DebuggerPortFromCommandLine < 0 ? Settings.DebugPort : DebuggerPortFromCommandLine);
    }

    if (Settings.WaitDebugger)
    {
        JsEnv->WaitDebugger(Settings.WaitDebuggerTimeout); // ★ 只有单实例模式才能阻塞等待调试器
    }
}
```

[2] `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:4522-4584` 与 `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:319-355`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp
// 函数: FJsEnvImpl::SetInspectorCallback / DispatchProtocolMessage
// 位置: 4522-4584，插件层只做协议消息转发
// ============================================================================
if (!InspectorChannel)
{
    InspectorChannel = Inspector->CreateV8InspectorChannel();
    InspectorChannel->OnMessage(
        [this](std::string Message)
        {
            auto Handler = InspectorMessageHandler.Get(MainIsolate);
            v8::Local<v8::Value> Args[] = {FV8Utils::ToV8String(MainIsolate, Message.c_str())};
            __USE(Handler->Call(ContextInner, ContextInner->Global(), 1, Args));
            // ★ 来自 Inspector 的消息被重新包装成 JS 回调
        });
}

InspectorMessageHandler.Reset(Isolate, v8::Local<v8::Function>::Cast(Info[0]));

if (InspectorChannel)
{
    FString Message = FV8Utils::ToFString(Isolate, Info[0]);
    InspectorChannel->DispatchProtocolMessage(TCHAR_TO_UTF8(*Message));
    // ★ 反向同样只是把前端消息交回 InspectorChannel
}
```

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp
// 位置: 319-355，直接对外暴露 DevTools 发现接口
// ============================================================================
Server.set_http_handler(std::bind(&V8InspectorClientImpl::OnHTTP, this, std::placeholders::_1));
Server.set_open_handler(std::bind(&V8InspectorClientImpl::OnOpen, this, std::placeholders::_1));
Server.set_message_handler(
    std::bind(&V8InspectorClientImpl::OnReceiveMessage, this, std::placeholders::_1, std::placeholders::_2));
Server.listen(Port);
Server.start_accept();                  // ★ 直接起 websocket/http server

JSONList = R"([
    {
        "description": "Puerts Inspector",
        "title": "Puerts Inspector",
        "type": "node",
        "webSocketDebuggerUrl": "ws://127.0.0.1:)" + std::to_string(Port) + R"("
    }
])";
// ★ 返回的是 DevTools 约定的 /json/list 信息，而不是插件自定义 schema
```

[3] `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h:31-80,643-687`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:52-109,402-417,479-544` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp:39-101`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h
// 位置: 31-80, 643-687，调试消息类型和统一封包发送接口
// ============================================================================
enum class EDebugMessageType : uint8
{
    StartDebugging,
    SetBreakpoint,
    StepOver,
    StepIn,
    StepOut,
    RequestVariables,
    Variables,
    RequestEvaluate,
    Evaluate,
    GoToDefinition,
    DebugServerVersion,
    CreateBlueprint,
    SetDataBreakpoints,
    ClearDataBreakpoints,
};
// ★ 协议能力由插件自己枚举，不依赖外部 DevTools schema

template<typename T>
void SendMessageToClient(FSocket* Client, EDebugMessageType MessageType, T& Message)
{
    TArray<uint8> Body;
    FMemoryWriter BodyWriter(Body);
    BodyWriter << Message;

    TArray<uint8> Buffer;
    if (!SerializeDebugMessageEnvelope(MessageType, Body, Buffer))
    {
        return;
    }
    // ★ 所有消息都走同一套 envelope 序列化
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 位置: 52-109, 402-417, 479-544，协议封包、服务端启动与数据断点都在插件内实现
// ============================================================================
bool SerializeDebugMessageEnvelope(EDebugMessageType MessageType, const TArray<uint8>& Body, TArray<uint8>& OutBuffer)
{
    const int32 MessageLength = static_cast<int32>(sizeof(uint8)) + Body.Num();
    const uint8 MessageTypeByte = static_cast<uint8>(MessageType);
    Writer << const_cast<int32&>(MessageLength);
    Writer << const_cast<uint8&>(MessageTypeByte);
    OutBuffer.Append(Body);
    return true;                        // ★ 自定义 envelope：长度 + 类型 + 负载
}

FAngelscriptDebugServer::FAngelscriptDebugServer(FAngelscriptEngine* InOwnerEngine, int Port)
{
    Listener = new FTcpListener(FIPv4Endpoint(FIPv4Address::Any, Port));
    Listener->OnConnectionAccepted().BindRaw(this, &FAngelscriptDebugServer::HandleConnectionAccepted);
    // ★ 自己起 TCP listener，而不是接入现成 Inspector server
}

if (DataBreakpoints.Num() > 0 && bBreakNextScriptLine)
{
    SyncActiveDataBreakpointsToAuthoritativeState();
    FAngelscriptClearDataBreakpoints ClearMessage;
    TArray<FString> TriggeredBreakpoints;
    SendMessageToAll(EDebugMessageType::ClearDataBreakpoints, ClearMessage);
    UpdateDataBreakpoints();
    // ★ 数据断点状态流也是插件协议的一部分
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp
// 位置: 39-101，协议兼容性本身有自动化回归测试
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAngelscriptDebugProtocolStartDebuggingRoundTripTest,
    "Angelscript.CppTests.Debug.Protocol.StartDebugging.RoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAngelscriptDebugProtocolDebugServerVersionRoundTripTest,
    "Angelscript.CppTests.Debug.Protocol.DebugServerVersion.RoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

const FStartDebuggingMessage RoundTripped = RoundTripMessage(Message);
TestEqual(TEXT("Debug.Protocol.StartDebugging.RoundTrip should preserve the adapter version"), RoundTripped.DebugAdapterVersion, 2);

const FDebugServerVersionMessage RoundTripped = RoundTripMessage(Message);
TestEqual(TEXT("Debug.Protocol.DebugServerVersion.RoundTrip should preserve the server version"), RoundTripped.DebugServerVersion, DEBUG_SERVER_VERSION);
// ★ 协议字段变化会直接打到自动化测试，而不是等到前端连不上再发现
```

设计取舍：

- `puerts` 复用 DevTools/Inspector，能直接吃现成前端生态，插件自身协议维护成本低。
- 这种路径的代价是 backend 一致性较弱：`WITH_QUICKJS` 直接去掉 Inspector，`JsEnvGroup` 也不能完整覆盖等待调试器这类流程。
- `Angelscript` 维护自定义调试协议，开发成本更高，但插件能稳定拥有变量查看、表达式求值、数据断点、蓝图创建等扩展消息面，而且可以对协议做版本化测试。

与 Angelscript 对比：

| 对比点 | puerts 新证据 | Angelscript 新证据 | 差距判断 |
| --- | --- | --- | --- |
| 协议所有权 | Inspector 流量经 `InspectorChannel` 桥接进 JS 回调 | `EDebugMessageType` + `SerializeDebugMessageEnvelope()` 完全由插件定义 | 实现方式不同 |
| 多后端调试一致性 | `WITH_QUICKJS` 直接 `WITHOUT_INSPECTOR`，`JsEnvGroup` 不支持 `WaitDebugger` | 单引擎统一走 `FAngelscriptDebugServer` | puerts 在多后端调试一致性上实现质量更弱 |
| 高阶调试能力归属 | 高阶语义主要跟随 DevTools/Inspector | 数据断点、资产数据库、`CreateBlueprint` 等都在插件协议内 | Angelscript 实现了更宽的插件自有调试面 |
| 协议回归保障 | 当前检视路径未见等价协议 round-trip 自动化测试 | `AngelscriptDebugProtocolTests.cpp` 明确覆盖协议序列化 | puerts 在插件层没有实现同等级协议测试 |

### [维度 D7] 编辑器集成：`puerts` 把编辑器能力寄托在 editor-side `JsEnv` 与 Blueprint 编译钩子上，`Angelscript` 把编辑器表面做成原生扩展点

前面的 D7 主要覆盖“有无编辑器能力”；这一轮补的是“这些能力到底挂在哪一层”。`puerts` 的 `PuertsEditor` 模块在 `OnPostEngineInit()` 里同时做三件事：给 `UTypeScriptBlueprint` 注册专用 `FKismetCompilerContext`、创建 `FSourceFileWatcher`、再起一个 editor-side `FJsEnv` 并直接 `Start("PuertsEditor/CodeAnalyze")`。也就是说，它的编辑器工作流核心不是一个纯 C++ 面板体系，而是“UE 编译器钩子 + 常驻 JS 分析器”。

`UPEBlueprintAsset` 则把这种设计再往前推一步。它不是简单创建资产，而是把 `CreateBlueprint`、函数图生成、事件节点覆写、成员变量增删都做成可从 JS 分析器驱动的 UObject API。换句话说，puerts 的编辑器层更像“给 TypeScript 分析管线提供资产写回能力”。

Angelscript 的编辑器集成重心不同。`AngelscriptEditor.Build.cs` 明确拉入 `ContentBrowser`、`ContentBrowserData`、`ToolMenus` 等模块；`UAngelscriptContentBrowserDataSource` 把 `AssetsPackage` 里的脚本资产映射到 `/All/Angelscript/...` 虚拟路径；`UScriptEditorMenuExtension` 则把 ToolMenu、LevelViewport、ContentBrowser 各类菜单位置暴露成脚本可配置的扩展点。这说明 Angelscript 更强调“脚本对象在编辑器中是一级公民”，而不是只服务某条特定编译链。

```
[puerts] Editor Toolchain
PuertsEditor module
├─ RegisterCompilerForBP(UTypeScriptBlueprint)  // 接管 TS Blueprint 编译
├─ FSourceFileWatcher                           // 监听源码变化
├─ editor-side FJsEnv
│  └─ Start("PuertsEditor/CodeAnalyze")         // 常驻 JS 分析器
└─ UPEBlueprintAsset                            // JS 驱动 Blueprint 资产写回

[Angelscript] Native Editor Surface
AngelscriptEditor module
├─ ContentBrowserData / ToolMenus               // 原生编辑器扩展模块
├─ UAngelscriptContentBrowserDataSource         // 脚本资产进入 Content Browser
└─ UScriptEditorMenuExtension                   // 脚本配置菜单扩展位置与行为
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Source/PuertsEditor/PuertsEditor.Build.cs:21-43` 与 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:110-150`

```csharp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/PuertsEditor/PuertsEditor.Build.cs
// 位置: 21-43，编辑器模块依赖直接围绕 Blueprint 编译与资产写回展开
// ============================================================================
PrivateDependencyModuleNames.AddRange(
    new string[]
    {
        "JsEnv",
        "Puerts",
        "DirectoryWatcher",
        "AssetRegistry",
        "KismetCompiler",
        "BlueprintGraph",
        "AssetTools"
        // ★ 没有独立内容浏览器数据源层，核心依赖集中在编译与资产修改
    }
);
```

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp
// 位置: 110-150，编辑器模块直接起 JS 分析器并注册 Blueprint 编译器
// ============================================================================
TSharedPtr<FKismetCompilerContext> MakeCompiler(
    UBlueprint* InBlueprint, FCompilerResultsLog& InMessageLog, const FKismetCompilerOptions& InCompileOptions)
{
    return MakeShared<FTypeScriptCompilerContext>(CastChecked<UTypeScriptBlueprint>(InBlueprint), InMessageLog, InCompileOptions);
}

FKismetCompilerContext::RegisterCompilerForBP(UTypeScriptBlueprint::StaticClass(), &MakeCompiler);
// ★ 只对 TypeScriptBlueprint 挂专用编译上下文

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

JsEnv = MakeShared<PUERTS_NAMESPACE::FJsEnv>(
    std::make_shared<PUERTS_NAMESPACE::DefaultJSModuleLoader>(TEXT("JavaScript")),
    std::make_shared<PUERTS_NAMESPACE::FDefaultLogger>(), -1,
    [this](const FString& InPath)
    {
        if (SourceFileWatcher.IsValid())
        {
            SourceFileWatcher->OnSourceLoaded(InPath);
        }
    },
    TEXT("--max-old-space-size=2048"));

JsEnv->Start("PuertsEditor/CodeAnalyze");
// ★ 编辑器分析主逻辑实际跑在一套常驻 editor-side JS VM 里
```

[2] `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:87-165,394-460`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp
// 函数: UPEBlueprintAsset::LoadOrCreate / AddFunction
// 位置: 87-165, 394-460，Blueprint 资产由分析器驱动创建和增量改写
// ============================================================================
Blueprint = LoadObject<UBlueprint>(nullptr, *PackageName, nullptr, LOAD_NoWarn | LOAD_NoRedirects);
if (!Blueprint)
{
    Blueprint = FKismetEditorUtilities::CreateBlueprint(
        ParentClass, Package, *InName, BlueprintType, BlueprintClass, BlueprintGeneratedClass, FName("PuertsAutoGen"));
    FAssetRegistryModule::AssetCreated(Blueprint);
    // ★ 资产创建本身就是 TypeScript 工具链的一部分
}

UFunction* ParentFunction = SuperClass->FindFunctionByName(InName);
UFunction* Function = GeneratedClass->FindFunctionByName(InName, EIncludeSuperFlag::ExcludeSuper);

if (ParentFunction)
{
    UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
    if (UEdGraphSchema_K2::FunctionCanBePlacedAsEvent(OverrideFunc) &&
        !IsImplementationDesiredAsFunction(Blueprint, OverrideFunc) && EventGraph)
    {
        UK2Node_Event* NewEventNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_Event>(EventGraph,
            EventGraph->GetGoodPlaceForNewNode(), EK2NewNodeFlags::SelectNewNode,
            [EventName, OverrideFuncClass](UK2Node_Event* NewInstance)
            {
                NewInstance->EventReference.SetExternalMember(EventName, OverrideFuncClass);
                NewInstance->bOverrideFunction = true;
            });
        // ★ JS 分析结果最终会反映为 Blueprint graph 上的事件节点与函数图
    }
}
```

[3] `Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs:16-39`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:17-29,65-120` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h:9-32,42-80,122-131`

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs
// 位置: 16-39，编辑器模块直接声明 ContentBrowser / ToolMenus 依赖
// ============================================================================
PublicDependencyModuleNames.AddRange(new string[]
{
    "Engine",
    "UnrealEd",
    "EditorSubsystem",
    "AngelscriptRuntime",
    "BlueprintGraph",
    "DirectoryWatcher",
    "AssetTools",
});

PrivateDependencyModuleNames.AddRange(new string[]
{
    "ContentBrowser",
    "ContentBrowserData",
    "ToolMenus",
    "ToolWidgets",
    // ★ 编辑器表面就是插件模块设计的一部分
});
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp
// 位置: 17-29, 65-120，把脚本资产放进 Content Browser 的虚拟路径
// ============================================================================
return FContentBrowserItemData(
    this,
    EContentBrowserItemFlags::Type_File | EContentBrowserItemFlags::Category_Asset,
    *(TEXT("/All/Angelscript/") + Asset->GetName()), Asset->GetFName(), FText::FromString(DisplayName), Payload, *Payload->Path);
// ★ Angelscript 资产有自己的浏览器命名空间，而不是只在某个分析器里临时可见

TArray<UObject*> Assets;
GetObjectsWithOuter(FAngelscriptEngine::Get().AssetsPackage, Assets);
for (UObject* Object : Assets)
{
    if (!InCallback(CreateAssetItem(Object)))
    {
        return;
    }
}
// ★ 由数据源主动枚举脚本资产，交给 Content Browser 过滤与展示
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h
// 位置: 9-32, 42-80, 122-131，菜单扩展位置本身就是脚本可配置对象
// ============================================================================
enum class EScriptEditorMenuExtensionLocation : uint8
{
    ToolMenu,
    LevelViewport_ContextMenu,
    ContentBrowser_AssetContextMenu,
    ContentBrowser_AssetViewContextMenu,
    ContentBrowser_AssetViewViewMenu,
    // ★ 菜单挂点被明确定义成脚本可选位置
};

UCLASS(BlueprintType)
class UScriptEditorMenuExtension : public UObject
{
    GENERATED_BODY()

    UPROPERTY(EditDefaultsOnly, Category = "Menu Extension")
    EScriptEditorMenuExtensionLocation ExtensionMenu = EScriptEditorMenuExtensionLocation::ToolMenu;

    UPROPERTY(EditDefaultsOnly, Category = "Menu Extension")
    FName ExtensionPoint = "MainFrame.MainMenu.Tools";

    UFUNCTION(BlueprintNativeEvent)
    bool ShouldExtend() const;
    // ★ 扩展入口是原生 UObject/Blueprint 能理解的编辑器对象
};

static TArray<FRegisteredExtender> RegisteredExtensions;
void BuildToolMenuSection(struct FToolMenuSection& MenuSection, FExtenderSelection Selection, bool bIsMenu) const;
void AddToolMenuEntry(struct FToolMenuSection& MenuSection, UFunction* Function, FExtenderSelection Selection) const;
// ★ 菜单扩展不是单次脚本操作，而是一套可注册、可复用的编辑器框架
```

设计取舍：

- `puerts` 的编辑器层把“源码分析、BP 编译、资产写回”绑成一条 TypeScript 工具链，适合围绕 TS 开发体验做闭环。
- 这种设计的代价是编辑器表面偏窄，当前检视路径里没有看到 Angelscript 那种显式的 Content Browser 数据源或通用菜单扩展框架。
- `Angelscript` 更强调脚本资产和脚本工具在编辑器里的原生存在感，代价是需要维护更多 editor-only C++ 集成面。

与 Angelscript 对比：

| 对比点 | puerts 新证据 | Angelscript 新证据 | 差距判断 |
| --- | --- | --- | --- |
| 编辑器主控层 | `PuertsEditorModule` 起 editor-side `FJsEnv` 并运行 `PuertsEditor/CodeAnalyze` | 主要由 `ContentBrowserData` / `ToolMenus` / UObject 扩展点承载 | 实现方式不同 |
| Blueprint 编译接入 | `RegisterCompilerForBP(UTypeScriptBlueprint)` 明确存在 | 当前新增检视路径未见等价 TS-style Blueprint 编译器替换点 | 实现方式不同 |
| 资产浏览器集成 | 当前主要是 `UPEBlueprintAsset` 驱动生成/更新 Blueprint | `UAngelscriptContentBrowserDataSource` 明确把脚本资产挂到 `/All/Angelscript/` | puerts 在显式 Content Browser 集成上没有实现同等级数据源 |
| 菜单扩展框架 | 当前新增检视路径未见通用菜单扩展 UObject 框架 | `UScriptEditorMenuExtension` 明确暴露多种编辑器菜单挂点 | puerts 在通用编辑器扩展框架上没有实现 |

### [维度 D9] 测试基础设施：`puerts` 当前 UE 插件层没有把测试做成一级模块，`Angelscript` 则把测试夹在运行时与编辑器之间持续演进

前面的 D9 只把有无测试简单对比了一次；这一轮补的是“测试是否已经成为插件架构的一部分”。就当前分析范围 `Reference/puerts/unreal/Puerts/` 而言，`Puerts.uplugin` 只声明了 `WasmCore`、`JsEnv`、`DeclarationGenerator`、`ParamDefaultValueMetas`、`Puerts`、`PuertsEditor` 六个模块，没有测试模块入口。更进一步，本轮对该路径全文检索 `IMPLEMENT_SIMPLE_AUTOMATION_TEST|FAutomationTestBase|CQTest|TESTING_GUIDE|RunTest(` 返回 `NO_MATCH`。这不能推出整个 puerts 仓库没有测试，但至少说明“当前 UE 插件层没有把测试暴露成可见模块/指南/fixture 体系”。

Angelscript 则相反。`AngelscriptTest.Build.cs` 单独声明了测试模块，并在 editor 构建下依赖 `CQTest`、`Networking`、`Sockets`、`UnrealEd`、`AngelscriptEditor`；`TESTING_GUIDE_ZH.md` 继续规定宏生命周期与作用域约束；`AngelscriptTestUtilities.h` 则提供 `BuildModule()`、`FAngelscriptTestFixture` 等公共夹具，能写临时 `.as` 文件、跑预处理、编译模块、切换 shared clone / isolated full / production-like engine 模式。最后，`AngelscriptMultiEngineTests.cpp` 这类文件直接把多引擎共享状态、克隆生命周期、隔离性做成自动化测试套件。

这意味着两边差的不是“有没有办法手工验证”，而是“测试是不是被插件自己当成一条受支持的产品线”。在当前 UE 插件范围里，puerts 更像交付运行时与编辑器能力本体；Angelscript 已经把测试、学习样例、协议回归都做成并列维护面。

```
[puerts] Current Plugin Test Surface
Puerts.uplugin
├─ WasmCore
├─ JsEnv
├─ DeclarationGenerator
├─ ParamDefaultValueMetas
├─ Puerts
└─ PuertsEditor
   // 当前插件层未声明 Test module

[Angelscript] In-Tree Test Infrastructure
AngelscriptTest module
├─ TESTING_GUIDE.md / TESTING_GUIDE_ZH.md
├─ AngelscriptTestUtilities.h            // 公共 fixture / engine scope / build helpers
├─ Runtime tests                         // protocol / multi-engine / coverage / subsystem
└─ Editor tests                          // directory watcher / blueprint impact / debugger fixtures
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Puerts.uplugin:15-48`

```jsonc
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Puerts.uplugin
// 位置: 15-48，当前 UE 插件层只声明功能模块，没有测试模块入口
// ============================================================================
"Modules": [
  { "Name": "WasmCore", "Type": "Runtime" },
  { "Name": "JsEnv", "Type": "Runtime" },
  { "Name": "DeclarationGenerator", "Type": "Editor" },
  { "Name": "ParamDefaultValueMetas", "Type": "Program" },
  { "Name": "Puerts", "Type": "Runtime" },
  { "Name": "PuertsEditor", "Type": "Editor" }
]
// ★ 模块图里没有与 `AngelscriptTest` 对应的测试层
```

[2] `Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs:6-50` 与 `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE_ZH.md:1-47`

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs
// 位置: 6-50，测试模块在构建图中是独立一层
// ============================================================================
public class AngelscriptTest : ModuleRules
{
    public AngelscriptTest(ReadOnlyTargetRules Target) : base(Target)
    {
        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
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
        // ★ 测试基础设施不是散落在运行时里，而是单独可编译、可依赖的模块
    }
}
```

```markdown
<!-- =========================================================================
文件: Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE_ZH.md
位置: 1-47，测试宏与生命周期规则有单独文档约束
============================================================================ -->
# Angelscript 测试约定与宏指南（中文补充）

- `ASTEST_BEGIN_*` 会展开出新的 C++ 作用域，并在其中创建 `FAngelscriptEngineScope`。
- `FULL` / `CLONE` / `NATIVE` 这几类还会在 `BEGIN` 里注册 `ON_SCOPE_EXIT`。
- 终结性的成功/失败 `return` 放在 `ASTEST_END_*` 之后。
// ★ 测试写法本身有专门规范，不是临时约定
```

[3] `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h:535-594,734-786` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineTests.cpp:94-145`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h
// 位置: 535-594, 734-786，公共测试夹具直接封装脚本编译与 engine 生命周期
// ============================================================================
inline asIScriptModule* BuildModule(FAutomationTestBase& Test, FAngelscriptEngine& Engine, const char* ModuleName, const FString& Source)
{
    const FString RequestedModuleName = ANSI_TO_TCHAR(ModuleName);
    const FString UniqueFilename = FString::Printf(TEXT("%s_%s.as"), *RequestedModuleName, *FGuid::NewGuid().ToString(EGuidFormats::Digits));
    const FString RelativeFilename = FString::Printf(TEXT("%s.as"), *RequestedModuleName);
    const FString AbsoluteFilename = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), UniqueFilename);
    if (!FFileHelper::SaveStringToFile(Source, *AbsoluteFilename))
    {
        Test.AddError(FString::Printf(TEXT("Failed to write script module '%s' to '%s'"), *RequestedModuleName, *AbsoluteFilename));
        return nullptr;
    }

    FAngelscriptPreprocessor Preprocessor;
    Preprocessor.AddFile(RelativeFilename, AbsoluteFilename);
    if (!Preprocessor.Preprocess())
    {
        ReportCompileDiagnostics(Test, Engine, AbsoluteFilename);
        Test.AddError(FString::Printf(TEXT("Failed to preprocess script module '%s'"), *RequestedModuleName));
        return nullptr;
    }

    TArray<TSharedRef<FAngelscriptModuleDesc>> ModulesToCompile = Preprocessor.GetModulesToCompile();
    TArray<TSharedRef<FAngelscriptModuleDesc>> CompiledModules;
    const ECompileResult CompileResult = Engine.CompileModules(ECompileType::Initial, ModulesToCompile, CompiledModules);
    // ★ fixture 直接覆盖“写文件 -> 预处理 -> 编译 -> 取回模块”整条链
}

struct FAngelscriptTestFixture
{
    FAngelscriptTestFixture(FAutomationTestBase& InTest, ETestEngineMode InMode = ETestEngineMode::SharedClone)
    {
        switch (Mode)
        {
        case ETestEngineMode::SharedClone:
            Engine = &AcquireCleanSharedCloneEngine();
            EngineScope = MakeUnique<FAngelscriptEngineScope>(*Engine);
            break;
        case ETestEngineMode::IsolatedFull:
        {
            OwnedEngine = CreateIsolatedFullEngine();
            if (OwnedEngine.IsValid())
            {
                Engine = OwnedEngine.Get();
                EngineScope = MakeUnique<FAngelscriptEngineScope>(*Engine);
            }
            break;
        }
        }
    }
    // ★ 测试层把引擎模式切换抽成统一 fixture，而不是每个用例手写
};
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineTests.cpp
// 位置: 94-145，多引擎共享状态被做成系统化自动化用例
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAngelscriptEngineCreateFullModeTest,
    "Angelscript.CppTests.MultiEngine.Create.Full",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAngelscriptEngineCreateCloneModeTest,
    "Angelscript.CppTests.MultiEngine.Create.Clone",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAngelscriptCloneModuleIsolationTest,
    "Angelscript.CppTests.MultiEngine.CloneModuleIsolation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAngelscriptDestroyingSourceWhileCloneAliveIsRejectedTest,
    "Angelscript.CppTests.MultiEngine.DestroyingSourceWhileCloneAliveIsRejected",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
// ★ 测试用例直接覆盖前面分析过的引擎克隆/共享状态这些架构性问题
```

设计取舍：

- `puerts` 当前 UE 插件层更专注运行时、声明生成和编辑器工具本体，没有把测试做成并列模块；这不是“测试做得差”，而是“当前分析范围内没有实现等价的插件级测试架构”。
- `Angelscript` 把测试模块、测试指南、fixture 和学习轨迹都纳入源码树，意味着架构变化会同步要求测试面演进。
- 这种投入会增加维护成本，但也让多引擎、调试协议、预编译缓存等复杂能力有了稳定回归入口。

与 Angelscript 对比：

| 对比点 | puerts 新证据 | Angelscript 新证据 | 差距判断 |
| --- | --- | --- | --- |
| 测试模块是否一级化 | `Puerts.uplugin` 仅声明 runtime/editor/program 模块 | `AngelscriptTest.Build.cs` 单独声明测试模块 | puerts 在当前插件层没有实现独立测试模块 |
| 公共 fixture | 当前分析范围内未见等价 `BuildModule`/engine fixture 文件 | `AngelscriptTestUtilities.h` 直接封装编译、预处理、引擎模式切换 | puerts 没有实现同等级公共测试夹具 |
| 架构回归用例 | 本轮全文搜索常见自动化测试入口返回 `NO_MATCH` | `AngelscriptMultiEngineTests.cpp` 等明确覆盖多引擎/协议/覆盖率等主题 | Angelscript 的测试基础设施更完整 |

---

## 深化分析 (2026-04-08 23:29:39)

### [维度 D6] 代码生成与 IDE 支持：`puerts` 把 TypeScript semantic host 内嵌进 UE，`Angelscript` 的 IDE 支撑更像“导航层”而不是“语义层”

这一轮补的不是 `ue.d.ts` 有没有，而是“谁拥有 IDE 的语义图”。`puerts` 的 `CodeAnalyze.ts` 直接在 UE editor-side `JsEnv` 里起一个 `ts.LanguageService`，文件系统读写、目录遍历、版本号、快照缓存都走 `UE.FileSystemOperation`。它不是把 `.d.ts` 导出给外部 IDE 就结束，而是把 TypeScript compiler host 直接塞进插件运行时，再用目录监听与 MD5 版本号做增量语义刷新。`DeclarationGenerator.cpp` 也不是只写一个声明文件，而是把 `Typing/` 和 `Content/JavaScript/` 一起拷回插件目录，说明声明生成、编辑器分析器和运行时脚本根本来就是一个闭环。

`Angelscript` 的新增证据则落在另一个方向。`FAngelscriptSourceCodeNavigation` 向 UE 注册 `ISourceCodeNavigationHandler`，把 `UASClass`、`UASFunction`、`UASStruct` 映射回 `.as` 文件与源码行号；原生函数定位则继续借 `FPlatformStackWalk::GetFunctionDefinitionLocation(...)` 之类的符号查询。也就是说，Angelscript 当前 IDE 体验的主轴是“从 UE 对象跳回源码”，而不是在插件内部维护一份像 TypeScript compiler 那样的完整语义图。

```
[puerts] IDE Semantic Loop
DeclarationGenerator
 -> write Typing/ue.d.ts
 -> copy Typing + JavaScript into plugin
 -> editor-side CodeAnalyze.ts boots tsc.js
 -> custom ts.LanguageServiceHost over UE file APIs
 -> watch MD5 changes
 -> refresh semantic graph / emit JS / update Blueprint

[Angelscript] IDE Navigation Loop
Script reload metadata
 -> UASClass / UASFunction / UASStruct
 -> SourceCodeNavigation handler
 -> open .as file or native source location
 -> developer edits source in external IDE
```

[1] `Reference/puerts/unreal/Puerts/PuertsEditor/CodeAnalyze.ts:8-49,65-67,282-355`

```typescript
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/PuertsEditor/CodeAnalyze.ts
// 位置: 8-49, 65-67, 282-355，UE 内嵌 TypeScript compiler host
// ============================================================================
const customSystem: ts.System = {
    readFile,
    writeFile,
    resolvePath: tsi.resolvePath,
    fileExists,
    readDirectory,
    getExecutingFilePath,
    getCurrentDirectory,
    // ★ 这里不是用 Node.js fs，而是把 ts.System 全部映射到 UE 暴露的文件 API
}

function readFile(path: string, encoding?: string): string | undefined {
    let data = $ref<string>(undefined);
    const res = UE.FileSystemOperation.ReadFile(path, data);
    if (res) {
        return $unref(data);
    } else {
        console.warn("readFile: read file fail! path=" + path + ", stack:" + new Error().stack);
        return undefined;
    }
}

function writeFile(path: string, data: string, writeByteOrderMark?: boolean): void {
    throw new Error("forbiden!")
    // ★ semantic host 自己不允许随意写文件，写盘行为由更上层流程统一控制
}

function getExecutingFilePath(): string {
    return getCurrentDirectory() + "Content/JavaScript/PuertsEditor/node_modules/typescript/lib/tsc.js";
    // ★ 插件直接在 UE 内部加载打包好的 TypeScript 编译器
}

const scriptSnapshotsCache = new Map<string, {version: string, scriptSnapshot:ts.IScriptSnapshot}>();
const servicesHost: ts.LanguageServiceHost = {
  getScriptFileNames: () => fileNames,
  getScriptVersion: fileName => {
      let md5 = UE.FileSystemOperation.FileMD5Hash(fileName);
      fileVersions[fileName] = { version: md5, processed: false};
      return md5;
      // ★ 版本号不是时间戳，而是内容 MD5
  },
  getScriptSnapshot: fileName => {
    if (!scriptSnapshotsCache.has(fileName)) {
        const sourceFile = customSystem.readFile(fileName);
        scriptSnapshotsCache.set(fileName, {
            version:fileVersions[fileName].version,
            scriptSnapshot: ts.ScriptSnapshot.fromString(sourceFile)
        });
    }

    let scriptSnapshotsInfo = scriptSnapshotsCache.get(fileName);
    if (scriptSnapshotsInfo.version != fileVersions[fileName].version) {
        const sourceFile = customSystem.readFile(fileName);
        scriptSnapshotsInfo.version = fileVersions[fileName].version;
        scriptSnapshotsInfo.scriptSnapshot = ts.ScriptSnapshot.fromString(sourceFile);
    }
    return scriptSnapshotsInfo.scriptSnapshot;
  },
};

let service = ts.createLanguageService(servicesHost, ts.createDocumentRegistry());
function getProgramFromService() {
    while(true) {
        try {
            return service.getProgram();
        } catch (e) {
            console.error(e);
        }
        service = ts.createLanguageService(servicesHost, ts.createDocumentRegistry());
        // ★ UE 文件读取偶发失败时，直接重建整个 LanguageService，优先保证 editor 工具继续可用
    }
}
```

[2] `Reference/puerts/unreal/Puerts/PuertsEditor/CodeAnalyze.ts:465-515` 与 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEDirectoryWatcher.cpp:14-68`

```typescript
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/PuertsEditor/CodeAnalyze.ts
// 位置: 465-515，TS/JS/JSON 变更进入 semantic host 的增量刷新队列
// ============================================================================
var dirWatcher = new UE.PEDirectoryWatcher();
global.__dirWatcher = dirWatcher; // ★ 防止 watcher 被 GC 回收

dirWatcher.OnChanged.Add((added, modified, removed) => {
    setTimeout(() =>{
        modifiedFiles.forEach(fileName => {
            let md5 = UE.FileSystemOperation.FileMD5Hash(fileName);
            if (md5 === fileVersions[fileName].version) {
                console.log(fileName + " md5 not changed, so skiped!");
            } else {
                fileVersions[fileName].version = md5;
                onSourceFileAddOrChange(fileName, true);
            }
        });

        refreshBlueprints();
        if (changed) {
            UE.FileSystemOperation.WriteFile(versionsFilePath, JSON.stringify(fileVersions, null, 4));
        }
    }, 100); // ★ 延时 100ms，避免 UE 与外部编辑器同时读文件产生冲突
});
```

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEDirectoryWatcher.cpp
// 位置: 14-68，底层 watcher 只把语义相关扩展名送上来
// ============================================================================
if (!Change.Filename.EndsWith(TEXT(".ts")) && !Change.Filename.EndsWith(TEXT(".mts")) &&
    !Change.Filename.EndsWith(TEXT(".tsx")) && !Change.Filename.EndsWith(TEXT(".json")) &&
    !Change.Filename.EndsWith(TEXT(".js")))
{
    continue;
    // ★ 过滤掉无关文件，说明这条 watcher 是给 semantic host 用的，不是通用热更总线
}

DirectoryWatcher->RegisterDirectoryChangedCallback_Handle(
    Directory, Changed, DelegateHandle, IDirectoryWatcher::IncludeDirectoryChanges);
```

[3] `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:417-426,1615-1631`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp:6-44,95-139` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:3148-3163`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp
// 位置: 417-426, 1615-1631，声明生成不是独立脚本，而是插件工作流的一部分
// ============================================================================
IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
FString PuertsBaseDir = IPluginManager::Get().FindPlugin("Puerts")->GetBaseDir();
PlatformFile.DeleteFile(*(PuertsBaseDir / TEXT("Typing/ue/ue.d.ts")));
PlatformFile.DeleteFile(*(PuertsBaseDir / TEXT("Typing/ue/ue_bp.d.ts")));

PlatformFile.CopyDirectoryTree(*ProjectTypingDir, *(PuertsBaseDir / TEXT("Typing")), false);
PlatformFile.CopyDirectoryTree(
    *(FPaths::ProjectContentDir() / TEXT("JavaScript")), *(PuertsBaseDir / TEXT("Content") / TEXT("JavaScript")), true);
// ★ 生成声明后，连 Typing 和 JavaScript 根都同步回插件目录，保证 editor/runtime 看到同一套产物

void GenUeDts(bool InGenFull, FName InSearchPath)
{
    GenTypeScriptDeclaration(InGenFull, InSearchPath);
    GenTypeScriptCppDeclaration();
    // ★ `ue.d.ts` 与 `cpp/index.d.ts` 属于同一条 editor 生成命令
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp
// 位置: 6-44, 95-139，Angelscript 侧更像“源码导航处理器”而不是语义主机
// ============================================================================
class FAngelscriptSourceCodeNavigation : public ISourceCodeNavigationHandler
{
public:
    virtual bool NavigateToClass(const UClass* InClass) override
    {
        auto ClassDesc = GetClassDesc(InClass, &Module);
        OpenModule(Module, ClassDesc->LineNumber);
        return true;
        // ★ 通过脚本模块描述里的行号直接跳回 `.as`
    }

    virtual bool NavigateToFunction(const UFunction* InFunction) override
    {
        auto* ASFunc = Cast<const UASFunction>(InFunction);
        FString Path = ASFunc->GetSourceFilePath();
        OpenFile(Path, ASFunc->GetSourceLineNumber());
        return true;
        // ★ 函数导航依赖 UASFunction 记录的源码位置
    }
};

void RegisterAngelscriptSourceNavigation()
{
    FSourceCodeNavigation::AddNavigationHandler(new FAngelscriptSourceCodeNavigation);
    // ★ 挂的是 UE 的 SourceCodeNavigation 扩展点
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 位置: 3148-3163，原生函数定位继续依赖平台符号查询
// ============================================================================
bool FORCENOINLINE FAngelscriptEditorModule::FindFunctionDefinitionLine(
    const FString& FunctionSymbolName, const FString& FunctionModuleName, uint32& OutLineNumber, FString& OutSourceFile)
{
    if (FPlatformStackWalk::GetFunctionDefinitionLocation(
            FunctionSymbolName, FunctionModuleName, OutSourceFile, OutLineNumber, SourceColumnNumber))
    {
        return true;
    }
    // ★ 原生函数跳转靠 PDB / dSYM，仍然是“导航”而不是“语言服务”
}
```

设计取舍：

- `puerts` 把 IDE semantic graph 放进插件内部，因此能直接拿 TypeScript 类型系统驱动 Blueprint 回写；代价是 editor 内要长期维护一个带文件容错、快照缓存和 watcher 的 compiler host。
- `Angelscript` 目前更偏“源码位置可回溯”：脚本对象、原生符号、Blueprint 影响面都能回到文件和行号，维护成本更低，但没有在插件内实现一套等价的通用语义服务。
- 这里的差距不是“有没有 IDE 支持”，而是“IDE 支持落在 semantic analysis 还是 source navigation”。

与 Angelscript 对比：

| 对比点 | puerts 新证据 | Angelscript 新证据 | 差距判断 |
| --- | --- | --- | --- |
| 语义主机归属 | `CodeAnalyze.ts` 在 UE 进程里创建 `ts.LanguageService` | `FAngelscriptSourceCodeNavigation` 只向 UE 注册导航处理器 | 实现方式不同 |
| 增量模型 | `FileMD5Hash` + `ScriptSnapshot` cache + 读失败重建 service | 当前新增证据主要是对象到源码位置映射 | puerts 实现了更完整的内嵌语义增量主机 |
| 声明生成与运行时闭环 | `GenUeDts()` 后同步 `Typing/` 与 `Content/JavaScript/` 到插件目录 | 当前新增证据未见等价“声明产物驱动 editor-side compiler host”链路 | Angelscript 在当前路径没有实现同等级闭环 |
| 原生函数 IDE 跳转 | 当前新增证据主轴不是 native symbol lookup | `FindFunctionDefinitionLine()` 依赖平台符号定位 | 实现方式不同 |

### [维度 D8] 性能与优化：`puerts` 把调用形状决定留到 `FFunctionTranslator` 执行期，`Angelscript` 在 `UASFunction` 生成期就把热路径专门化

前面的 D8 已经覆盖过 `FastCall`、GC 和 scheduler；这一轮补的是“调用策略何时决定”。`puerts` 的 TS/JS override 路径不管入口是 `UJSGeneratedFunction` 还是 `UTypeScriptGeneratedClass`，最终都汇到 `IDynamicInvoker -> FJsEnvImpl -> FFunctionTranslator::CallJs(...)`。`FFunctionTranslator` 内部保存 `std::vector<std::unique_ptr<FPropertyTranslator>>`，每次调用都要为 `Arguments.size()` 临时分配 `v8::Local<v8::Value>` 数组、逐参 `UEToJs`、必要时扫描 `FOutParmRec`，Blueprint 发起调用时还要先从 `FFrame` 重建 `Params` buffer。这意味着 `puerts` 的热路径优化重点是“让同一个 translator 适配尽可能多的函数形状”。

`Angelscript` 走的是反方向。类生成时 `UASFunction::AllocateFunctionFor(...)` 会根据“是否 thread-safe、是否有 raw JIT、参数个数、是否 primitive、是否 reference、返回值形状”直接选出 `UASFunction_NoParams`、`UASFunction_DWordArg_JIT`、`UASFunction_ObjectReturn_JIT` 之类专用 thunk。真正运行时，很多路径只剩下固定的 `Stack.StepCompiledIn<FProperty>(&ArgumentValue)` 和 `MakeRawJITCall_*`，只有不满足专门化条件时才退回通用 `AngelscriptCallFromBPVM` 或 `Context->Execute()`。两边都不是“没有 generic path”，但 generic path 介入的时机完全不同。

```
[puerts] JS/TS Override Call Path
UE thunk
 -> IDynamicInvoker
 -> FJsEnvImpl::InvokeJsMethod / InvokeTsMethod
 -> FFunctionTranslator::CallJs
    -> allocate Args[]
    -> loop PropertyTranslator[]
    -> scan out parms / maybe rebuild Params
 -> v8::Function::Call

[Angelscript] Script Function Call Path
class generation
 -> UASFunction::AllocateFunctionFor
    -> choose specialized thunk subclass once
runtime
 -> UASFunction_*::RuntimeCallFunction
    -> fixed Stack.StepCompiledIn shape
    -> raw JIT call or prepared context execute
```

[1] `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JSGeneratedFunction.cpp:11-24`、`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:2184-2216,2274-2315`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JSGeneratedFunction.cpp
// 位置: 11-24，JS override 的 UE thunk 只负责把调用丢给 dynamic invoker
// ============================================================================
DEFINE_FUNCTION(UJSGeneratedFunction::execCallJS)
{
    UJSGeneratedFunction* Func = Cast<UJSGeneratedFunction>(Stack.CurrentNativeFunction ? Stack.CurrentNativeFunction : Stack.Node);
    auto PinedDynamicInvoker = Func->DynamicInvoker.Pin();
    if (PinedDynamicInvoker)
    {
        PinedDynamicInvoker->InvokeJsMethod(Context, Func, Stack, RESULT_PARAM);
        // ★ UE thunk 本身不区分参数形状，真正调用策略在后面才决定
    }
}
```

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp
// 位置: 2184-2216, 2274-2315，JS/TS 方法最终统一落到 FunctionTranslator
// ============================================================================
void FJsEnvImpl::InvokeJsMethod(UObject* ContextObject, UJSGeneratedFunction* Function, FFrame& Stack, void* RESULT_PARAM)
{
    v8::Local<v8::Value> Self;
    auto GeneratedObjectPtr = ObjectMap.Find(ContextObject);
    if (GeneratedObjectPtr)
    {
        Self = GeneratedObjectPtr->Get(Isolate);
    }

    Function->FunctionTranslator->CallJs(
        Isolate, Context, Function->JsFunction.Get(Isolate), Self, ContextObject, Stack, RESULT_PARAM);
    // ★ 无论具体签名如何，统一走 translator
}

void FJsEnvImpl::InvokeTsMethod(UObject* ContextObject, UFunction* Function, FFrame& Stack, void* RESULT_PARAM)
{
    auto FuncInfo = TsFunctionMap.Find(Function);
    if (!FuncInfo)
    {
        MakeSureInject(Class, true, false);
        FinishInjection(Class);
    }

    FuncInfo->FunctionTranslator->CallJs(
        Isolate, Context, FuncInfo->JsFunction.Get(Isolate), ThisObj, ContextObject, Stack, RESULT_PARAM);
    // ★ TS override 也没有专门的轻量 thunk，仍回到同一个 translator 框架
}
```

[2] `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.h:45-49,57-100` 与 `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp:488-618`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.h
// 位置: 45-49, 57-100，translator 的核心数据结构就是一组 PropertyTranslator
// ============================================================================
void CallJs(v8::Isolate* Isolate, v8::Local<v8::Context>& Context, v8::Local<v8::Function> JsFunction,
    v8::Local<v8::Value> This, UObject* ContextObject, FFrame& Stack, void* RESULT_PARAM);

std::vector<std::unique_ptr<FPropertyTranslator>> Arguments;
std::unique_ptr<FPropertyTranslator> Return;
uint32 ParamsBufferSize;
void* ArgumentDefaultValues;
// ★ 调用期需要靠这一组 translator 解释参数、返回值和默认值
```

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp
// 位置: 488-618，Blueprint/JS 边界每次调用都要逐参编组
// ============================================================================
void FFunctionTranslator::CallJs(v8::Isolate* Isolate, v8::Local<v8::Context>& Context, v8::Local<v8::Function> JsFunction,
    v8::Local<v8::Value> This, UObject* ContextObject, FFrame& Stack, void* RESULT_PARAM)
{
    void* Params = Stack.Locals;
    FOutParmRec* NewOutParms = nullptr;
    const bool CallByBP = Stack.Node != Stack.CurrentNativeFunction;

    if (CallByBP)
    {
        Params = ParamsBufferSize > 0 ? FMemory_Alloca(ParamsBufferSize) : nullptr;
        FMemory::Memzero(Params, ParamsBufferSize);
        for (PropertyMacro* Property = (PropertyMacro*) Function->ChildProperties;
             *Stack.Code != EX_EndFunctionParms; Property = (PropertyMacro*) (Property->Next))
        {
            Property->InitializeValue_InContainer(Params);
            Stack.Step(Stack.Object, Property->ContainerPtrToValuePtr<uint8>(Params));
            // ★ Blueprint 调用时要先把 FFrame 里的参数重新摊平成 Params buffer
        }
    }

    v8::Local<v8::Value>* Args =
        static_cast<v8::Local<v8::Value>*>(FMemory_Alloca(sizeof(v8::Local<v8::Value>) * Arguments.size()));
    for (int i = 0; i < Arguments.size(); ++i)
    {
        if (!CallByBP && (Arguments[i]->Property->PropertyFlags & CPF_OutParm) && Stack.OutParms)
        {
            FOutParmRec* Out = Stack.OutParms;
            while (Out->Property != Arguments[i]->Property)
            {
                Out = Out->NextOutParm;
            }
            Args[i] = Arguments[i]->UEToJs(Isolate, Context, Out->PropAddr, false);
            continue;
        }
        Args[i] = Arguments[i]->UEToJsInContainer(Isolate, Context, Params, false);
        // ★ 每次调用都逐参做 UE -> JS 转换
    }

    Result = JsFunction->Call(Context, This, Arguments.size(), Args);

    if (!Result.IsEmpty())
    {
        if (Return)
        {
            Return->JsToUE(Isolate, Context, Result.ToLocalChecked(), RESULT_PARAM, true);
        }

        for (int i = 0; i < Arguments.size(); ++i)
        {
            if (Arguments[i]->IsOut())
            {
                auto OutParmRec = GetMatchOutParmRec(NewOutParms ? NewOutParms : Stack.OutParms, Arguments[i]->Property);
                if (OutParmRec)
                {
                    Arguments[i]->JsToUEOut(Isolate, Context, Args[i], OutParmRec->PropAddr, true);
                }
            }
        }
        // ★ 返回值和 out param 也在调用结束后再回写
    }
}
```

[3] `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:1762-1929,1971-1997,2025-2043`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp
// 位置: 1762-1929, 1971-1997, 2025-2043，调用策略在类生成时就固化成专门 thunk
// ============================================================================
UASFunction* UASFunction::AllocateFunctionFor(UClass* InClass, FName ObjectName, TSharedPtr<FAngelscriptFunctionDesc> FunctionDesc)
{
    const bool bHasNonVirtualJitFunction = ScriptFunction != nullptr
        && ScriptFunction->jitFunction != nullptr
        && ScriptFunction->jitFunction_Raw != nullptr
        && ScriptFunction->jitFunction_ParmsEntry != nullptr
        && ScriptFunction->traits.GetTrait(asTRAIT_FINAL);

    if (FunctionDesc->bThreadSafe)
    {
        return bHasNonVirtualJitFunction
            ? NewObject<UASFunction_JIT>(InClass, ObjectName, RF_Public)
            : NewObject<UASFunction>(InClass, ObjectName, RF_Public);
    }

    if (!FunctionDesc->ReturnType.IsValid() && FunctionDesc->Arguments.Num() == 0)
    {
        return bHasNonVirtualJitFunction
            ? NewObject<UASFunction_NoParams_JIT>(InClass, ObjectName, RF_Public)
            : NewObject<UASFunction_NoParams>(InClass, ObjectName, RF_Public);
    }

    if (!FunctionDesc->ReturnType.IsValid()
        && FunctionDesc->Arguments.Num() == 1
        && !FunctionDesc->Arguments[0].Type.bIsReference
        && FunctionDesc->Arguments[0].Type.IsPrimitive())
    {
        // ★ 参数形状在这里一次性分类成 Byte / DWord / QWord / Float / Double 等专门 thunk
    }

    return bHasNonVirtualJitFunction
        ? NewObject<UASFunction_NotThreadSafe_JIT>(InClass, ObjectName, RF_Public)
        : NewObject<UASFunction_NotThreadSafe>(InClass, ObjectName, RF_Public);
}

void UASFunction_NoParams::RuntimeCallFunction(UObject* Object, FFrame& Stack, RESULT_DECL)
{
    asCScriptFunction* RealFunction = ResolveScriptVirtual(this, Object);
    if (auto* JitFunc = RealFunction->jitFunction_Raw)
    {
        P_FINISH;
        MakeRawJITCall_NoParam(Object, JitFunc);
        // ★ 无参路径直接进入 raw JIT call
    }
    else
    {
        AS_PREPARE_CONTEXT_OR_RETURN(Context, RealFunction);
        Context->SetObject(Object);
        P_FINISH;
        Context->Execute();
    }
}

void UASFunction_DWordArg::RuntimeCallFunction(UObject* Object, FFrame& Stack, RESULT_DECL)
{
    asCScriptFunction* RealFunction = ResolveScriptVirtual(this, Object);
    if (auto* JitFunc = RealFunction->jitFunction_Raw)
    {
        asDWORD ArgumentValue;
        Stack.StepCompiledIn<FProperty>(&ArgumentValue);
        P_FINISH;
        MakeRawJITCall_Arg<asDWORD>(Object, JitFunc, ArgumentValue);
        // ★ 只有一个固定宽度参数时，运行时不再走反射数组循环
    }
}
```

设计取舍：

- `puerts` 的 translator 架构统一、后端无关、对 UFunction 形状变化更稳，但热路径始终保留“逐参解释”成本。
- `Angelscript` 把复杂度前移到 class generation 阶段，换来运行时更短的 thunk；代价是要维护大量 `UASFunction_*` 变体，并处理 JIT / non-JIT / thread-safe 分叉。
- 所以这里不是简单的 “V8 JIT vs AngelScript JIT”，而是 “generic bridge 在执行期发生” 对比 “call shape specialization 在生成期发生”。

与 Angelscript 对比：

| 对比点 | puerts 新证据 | Angelscript 新证据 | 差距判断 |
| --- | --- | --- | --- |
| 调用策略决策时机 | `InvokeJsMethod()` / `InvokeTsMethod()` 调用时才进入 `FunctionTranslator` | `AllocateFunctionFor()` 生成类时就选好 `UASFunction_*` 子类 | 实现方式不同 |
| 热路径参数处理 | 每次调用都要构造 `Args[]` 并循环 `PropertyTranslator[]` | 专门 thunk 只取固定形状参数并直接 raw JIT call | Angelscript 在 BPVM->script 热路径上实现更激进的专门化 |
| Blueprint 调用桥 | `CallByBP` 时先重建 `ParamsBuffer` 再转 JS | `RuntimeCallFunction()` 直接 `StepCompiledIn` 到目标类型 | Angelscript 在这一路径上实现质量更偏性能 |
| 通用回退路径 | translator 本身就是统一主路径 | thread-safe / 非专门化场景才回退通用路径 | 实现方式不同 |

### [维度 D11] 部署与打包：`puerts` 的部署产物更像“runtime-compatible script package”，`Angelscript` 的部署产物更像“build-bound precompiled snapshot”

这一轮补的是“产物信任模型”。`puerts` 的 `DefaultJSModuleLoader` 在部署期默认承认 `.js`、`.mjs`、`.cjs`，可选承认 `.mbc`、`.cbc`，同时支持 `package.json`、`index.js` 和向上查找 `node_modules`；`UPuertsSetting::RootPath` 又把脚本根显式暴露成配置项。这说明它部署时相信的是“模块解析语义”和“backend runtime 可用性”。进一步看 `JsEnvImpl.cpp`，V8 bytecode 头里的 `FlagHash` 与 `ReadOnlySnapshotChecksum` 不匹配时会被直接改写成当前 runtime 预期值，然后继续 `kConsumeCodeCache`。这不是严格的构建缓存失效协议，而是一种偏“尽量兼容当前 runtime”的软校正。

`Angelscript` 则相反。`bUsePrecompiledData` 只有在非 editor、非 development mode、非 commandlet 场景下才开启；加载时优先吃 `PrecompiledScript_[Config].Cache`，然后校验 `BuildIdentifier`、`PrecompiledDataGuid`，如果不匹配就直接丢弃预编译数据或清空 JIT 数据库；一旦真的走 `PrecompiledData->GetModulesToCompile()`，热重载会被完全禁掉。这套机制更像“把脚本当成跟二进制同版本的构建产物”，而不是“运行时再协商兼容”。

在“脚本保护”方面，本轮继续只看到 `puerts` 把重点放在 backend 二进制分发、module resolution 和 code cache 兼容；对 `Reference/puerts/unreal/Puerts/Source/JsEnv/`、`Source/Puerts/`、`Content/JavaScript/puerts/` 的部署链路文件检视中，未见独立加密/签名入口。这里的判断应归类为“当前分析范围内没有实现”，不是“实现差”。

```
[puerts] Deployment Contract
settings RootPath
 -> module loader resolves js/mjs/cjs/mbc/cbc/package.json
 -> stage backend runtime libs (Node.js / QuickJS / V8)
 -> optional consume code cache
 -> if cache header mismatch, patch to current runtime
 -> keep module/package semantics as source of truth

[Angelscript] Deployment Contract
runtime config chooses precompiled mode
 -> load PrecompiledScript_[Config].Cache
 -> validate build id + PrecompiledDataGuid
 -> instantiate module descriptors from cache
 -> disable hot reload for this run
 -> treat cache as build-bound snapshot
```

[1] `Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsSetting.h:20-22,48-57` 与 `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DefaultJSModuleLoader.cpp:67-120`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsSetting.h
// 位置: 20-22, 48-57，脚本根和声明生成忽略项都是部署配置面的一部分
// ============================================================================
UPROPERTY(config, EditAnywhere, Category = "Default JavaScript Environment",
    meta = (defaultValue = "JavaScript", Tooltip = "JavaScript Source Code Root Path", DisplayName = "JavaScript Source Root"))
FString RootPath = "JavaScript";
// ★ 默认部署契约先从 `Content/JavaScript` 这一层开始
```

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DefaultJSModuleLoader.cpp
// 位置: 67-120，module resolver 直接承认 npm 风格目录语义和 bytecode 扩展
// ============================================================================
return SearchModuleWithExtInDir(Dir, RequiredModule + ".js", Path, AbsolutePath) ||
       SearchModuleWithExtInDir(Dir, RequiredModule + ".mjs", Path, AbsolutePath) ||
       SearchModuleWithExtInDir(Dir, RequiredModule + ".cjs", Path, AbsolutePath) ||
#if defined(WITH_V8_BYTECODE)
       SearchModuleWithExtInDir(Dir, RequiredModule + ".mbc", Path, AbsolutePath) ||
       SearchModuleWithExtInDir(Dir, RequiredModule + ".cbc", Path, AbsolutePath) ||
#endif
       SearchModuleWithExtInDir(Dir, RequiredModule / "package.json", Path, AbsolutePath) ||
       SearchModuleWithExtInDir(Dir, RequiredModule / "index.js", Path, AbsolutePath);

while (pathFrags.Num() > 0)
{
    if (!pathFrags.Last().Equals(TEXT("node_modules")))
    {
        if (SearchModuleInDir(FString::Join(pathFrags, TEXT("/")), RequiredModule, Path, AbsolutePath))
        {
            return true;
        }
    }
    pathFrags.Pop();
}

return SearchModuleInDir(FPaths::ProjectContentDir() / ScriptRoot, RequiredModule, Path, AbsolutePath) ||
       (ScriptRoot != TEXT("JavaScript") &&
           SearchModuleInDir(FPaths::ProjectContentDir() / TEXT("JavaScript"), RequiredModule, Path, AbsolutePath));
// ★ 解析语义是部署契约的一部分，不只是“把几个脚本文件拷过去”
```

[2] `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:333-344,674-680,3711-3737,4117-4124` 与 `Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs:154-165,502-523,624-663`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp
// 位置: 333-344, 674-680, 3711-3737, 4117-4124，V8 code cache 更像软兼容层
// ============================================================================
struct FCodeCacheHeader
{
    uint32_t MagicNumber;
    uint32_t VersionHash;
    uint32_t SourceHash;
    uint32_t FlagHash;
    uint32_t ReadOnlySnapshotChecksum;
    uint32_t PayloadLength;
    uint32_t Checksum;
};

auto CachedCode = v8::ScriptCompiler::CreateCodeCache(Script->GetUnboundScript());
const FCodeCacheHeader* CodeCacheHeader = (const FCodeCacheHeader*) CachedCode->data;
Expect_FlagHash = CodeCacheHeader->FlagHash;
Expect_ReadOnlySnapshotChecksum = CodeCacheHeader->ReadOnlySnapshotChecksum;
// ★ 运行时启动时先记住“本机 V8 期望的 cache header”

if (FileName.EndsWith(TEXT(".mbc")))
{
    FCodeCacheHeader* CodeCacheHeader = (FCodeCacheHeader*) Data.GetData();
    if (CodeCacheHeader->FlagHash != Expect_FlagHash)
    {
        CodeCacheHeader->FlagHash = Expect_FlagHash;
    }
    if (CodeCacheHeader->ReadOnlySnapshotChecksum != Expect_ReadOnlySnapshotChecksum)
    {
        CodeCacheHeader->ReadOnlySnapshotChecksum = Expect_ReadOnlySnapshotChecksum;
    }
    CachedCode = new v8::ScriptCompiler::CachedData(Data.GetData(), Data.Num());
    Options = v8::ScriptCompiler::CompileOptions::kConsumeCodeCache;
    // ★ header 不匹配先改头再吃 cache，说明 `.mbc` 不是严格只读构建产物
}

if (Path.EndsWith(TEXT(".cbc")) || Path.EndsWith(TEXT(".mbc")))
{
    v8::Local<v8::ArrayBuffer> Ab = v8::ArrayBuffer::New(Info.GetIsolate(), Data.Num());
    ::memcpy(Buff, Data.GetData(), Data.Num());
    Info.GetReturnValue().Set(Ab);
    // ★ JS 侧拿到的是原始 bytecode buffer，而不是被封装过的受保护资源
}
```

```csharp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs
// 位置: 154-165, 502-523, 624-663，多后端部署的重点在 native runtime staging
// ============================================================================
if (UseNodejs)
{
    ThirdPartyNodejs(Target);
}
else if (UseQuickjs)
{
    ThirdPartyQJS(Target);
}
else if (UseV8Version > SupportedV8Versions.VDeprecated)
{
    ThirdParty(Target);
}
// ★ backend 选择先发生在构建图

PrivateDefinitions.Add("WITH_NODEJS");
PublicAdditionalLibraries.Add(Path.Combine(V8LibraryPath, "libnode.lib"));
RuntimeDependencies.Add("$(TargetOutputDir)/libnode.dll", Path.Combine(V8LibraryPath, "libnode.dll"));
// ★ Node.js 路径显式 stage `libnode.dll`

PrivateDefinitions.Add("WITHOUT_INSPECTOR");
PrivateDefinitions.Add("WITH_QUICKJS");
PublicAdditionalLibraries.Add(Path.Combine(V8LibraryPath, "quickjs.dll.lib"));
AddRuntimeDependencies(new string[] { "msys-quickjs.dll" }, V8LibraryPath, false);
// ★ QuickJS 路径显式 stage `quickjs` 运行库
```

[3] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1425-1557,2046-2056,2731-2733` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp:2627-2675`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 1425-1557, 2046-2056, 2731-2733，预编译缓存与运行时绑定得更死
// ============================================================================
bGeneratePrecompiledData = RuntimeConfig.bGeneratePrecompiledData;
bScriptDevelopmentMode = RuntimeConfig.bIsEditor || RuntimeConfig.bDevelopmentMode;
bUsePrecompiledData = !bGeneratePrecompiledData && !RuntimeConfig.bIgnorePrecompiledData
    && !RuntimeConfig.bRunningCommandlet && !WITH_EDITOR && !bScriptDevelopmentMode;
// ★ 只有非 editor / 非 development / 非 commandlet 才会真的吃预编译缓存

if (IFileManager::Get().FileExists(*Filename))
{
    PrecompiledData = new FAngelscriptPrecompiledData(Engine);
    PrecompiledData->Load(Filename);

    if (!PrecompiledData->IsValidForCurrentBuild())
    {
        delete PrecompiledData;
        PrecompiledData = nullptr;
        UE_LOG(Angelscript, Warning, TEXT("Loaded angelscript precompiled data was for a different build configuration. Discarding all precompiled data."));
        // ★ build 配置不匹配直接丢弃
    }
    else
    {
        if (CompiledInfo != nullptr && CompiledInfo->PrecompiledDataGuid != PrecompiledData->DataGuid)
        {
            UE_LOG(Angelscript, Warning, TEXT("Loaded angelscript precompiled data does not match the transpiled C++ in the game binary. Transpiled code will not be used!"));
            FJITDatabase::Get().Clear();
            // ★ 连编进二进制的 JIT 代码都要和 cache GUID 对齐
        }
    }
}

if (PrecompiledData != nullptr && bUsePrecompiledData && !bScriptDevelopmentMode)
{
    bUsedPrecompiledDataForPreprocessor = true;
    ModulesToCompile = PrecompiledData->GetModulesToCompile();
    UE_LOG(Angelscript, Warning, TEXT("Using fully precompiled scripts. Hot reloading is disabled for this run."));
}

if (bUsedPrecompiledDataForPreprocessor)
    return;
// ★ 一旦走预编译描述符，热重载主循环直接退出
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp
// 位置: 2627-2675，cache 自己带 build identifier
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
    // ★ build id 不同就视为无效
}

void FAngelscriptPrecompiledData::Save(const FString& Filename)
{
    Writer << *this;
    FFileHelper::SaveArrayToFile(Data, *Filename);
}
// ★ 这是严格序列化的 build-bound snapshot，不是运行时可随意协商的 cache
```

设计取舍：

- `puerts` 的部署链更灵活：脚本根可配置、模块解析兼容 npm 语义、多 backend 可切、bytecode header 会尽量向当前 runtime 对齐。
- 这种灵活性的代价是部署成功不仅取决于“脚本文件在不在”，还取决于 backend 二进制、cache header、module resolver 语义是否一起成立。
- `Angelscript` 的预编译链更刚性：build id、JIT GUID、运行模式都要对齐，换来的是启动时更确定的模块描述符恢复路径。
- 对“脚本加密/签名”这一项，本轮只能得出“`puerts` 当前分析范围内没有实现独立入口”，不能升级成“实现质量差”。

与 Angelscript 对比：

| 对比点 | puerts 新证据 | Angelscript 新证据 | 差距判断 |
| --- | --- | --- | --- |
| 运行时主契约 | `RootPath` + module resolver + backend runtime libs | `PrecompiledScript_[Config].Cache` + build id + `PrecompiledDataGuid` | 实现方式不同 |
| cache 信任模型 | `.mbc` header 不匹配时会被改写后继续消费 | `IsValidForCurrentBuild()` 失败直接丢弃 | 实现质量差异，Angelscript 更偏严格构建一致性 |
| 热重载与部署关系 | code cache 不直接禁止模块热替换 | 使用 precompiled descriptors 时直接禁 hot reload | 实现方式不同 |
| 多平台后端分发 | `JsEnv.Build.cs` 显式 stage `libnode.dll` / `msys-quickjs.dll` 等 | 当前主轴不是切换 VM，而是切换 precompiled mode | 实现方式不同 |
| 脚本保护 | 当前分析范围内未见独立加密/签名 hook | 当前新增证据主轴也不是加密，而是 precompiled cache 严格校验 | puerts 在当前路径没有实现独立脚本保护入口 |

---

## 深化分析 (2026-04-08 23:47:31)

### [维度 D4] 热重载：`puerts` 当前改的是已解析脚本文本，不会默认重跑 `module factory`；`Angelscript` 则显式重建 script object / CDO / Blueprint 依赖

这一轮补的是“被替换的到底是什么”。前面的 D4 已经说明 `puerts` 走 Inspector HMR；更细看 `hot_reload.js` 与 `modular.js`，它当前替换的是 **已解析 scriptId 对应的源码文本**，而不是 CommonJS `module` 实例。`modular.js` 给 `module` 对象只挂了 `exports`，缓存也分成全局 `moduleCache` 与每个 `require` 闭包自己的 `localModuleCache`；`hot_reload.js` 在 `Debugger.setScriptSource(...)` 之后把 `puerts.forceReload(url)` 留成了注释。换句话说，当前插件层没有内建“清模块缓存并重新执行工厂函数”的主链，HMR 更像“把下一次函数执行指向新源码”。

`Angelscript` 的 soft reload 则是另一层语义。`PrepareSoftReload()` 会先构造一个无 defaults 的临时 CDO 作为差分基线；`DoSoftReload()` 再重链属性 offset、更新 `ScriptTypePtr`、析构并重建现有 script object 与 CDO，最后复制需要保留的属性值。编辑器侧 `ClassReloadHelper` 还会扫描所有 Blueprint，把受影响 pin、变量和 wildcard type 替换到新类型，再把依赖 Blueprint 收集出来走后续重编译。这不是“模块 patch”与“全量 reload”谁更先进的问题，而是 **文本级 patch** 对比 **对象图级 reinstance**。

```
[puerts] Text Patch HMR
loaded moduleCache[url]
 -> Debugger.getScriptSource(scriptId)
 -> Debugger.setScriptSource(new wrapped source)
 -> emit HMR.prepare / HMR.finish
 -> existing module object stays alive
 -> next call hits new function body

[Angelscript] Object Graph Soft Reload
old class desc
 -> PrepareSoftReload() creates CDO_NoDefaults
 -> DoSoftReload() relinks properties / ScriptTypePtr
 -> destruct + reinitialize script objects and CDOs
 -> ClassReloadHelper refreshes Blueprint pins
 -> dependency Blueprints recompile
```

[1] `Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:54-71,105-146,205-226` 与 `Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js:67-92`

```javascript
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js
// 位置: 54-71, 105-146, 205-226，module 实例与 cache 的真实形状
// ============================================================================
let moduleCache = Object.create(null);

function executeModule(fullPath, script, debugPath, sid, isESM, bytecode) {
    let exports = {};
    let module = puerts.getModuleBySID(sid);
    module.exports = exports; // ★ 当前 module 实例只显式挂了 exports，没有看到 hot/dispose 契约
    let wrapped = evalScript(
        (isESM || bytecode) ? script : "(function (exports, require, module, __filename, __dirname) { " + script + "\n});",
        debugPath, isESM, fullPath, bytecode
    );
    if (isESM) return wrapped;
    wrapped(exports, puerts.genRequire(fullDirInJs), module, fullPathInJs, fullDirInJs);
    return module.exports;
}

function require(moduleName) {
    let forceReload = false;
    if ((moduleName in localModuleCache)) {
        let m = localModuleCache[moduleName];
        if (!m.__forceReload) {
            return localModuleCache[moduleName].exports; // ★ 命中 local cache 时直接返回旧 module.exports
        } else {
            forceReload = true;
        }
    }

    let key = fullPath;
    if ((key in moduleCache) && !forceReload) {
        localModuleCache[moduleName] = moduleCache[key];
        return localModuleCache[moduleName].exports;   // ★ 全局 cache 也直接复用旧 module 对象
    }

    let m = {"exports":{}};
    localModuleCache[moduleName] = m;
    moduleCache[key] = m; // ★ 重新执行工厂函数只发生在 cache miss 或显式 __forceReload
}

function forceReload(reloadModuleKey) {
    for (var moduleKey in moduleCache) {
        if (!reloadModuleKey || (reloadModuleKey == moduleKey)) {
            moduleCache[moduleKey].__forceReload = true; // ★ cache invalidation 有接口，但不是当前 HMR 主链默认动作
        }
    }
}
```

```javascript
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js
// 位置: 67-92，Inspector HMR 实际只做 script source patch
// ============================================================================
async function reload(moduleName, url, source) {
    await enableDebugger();
    let scriptId = parsedScript.get(url);

    if (scriptId && typeof source === "string") {
        let orgSourceInfo = await sendCommand("Debugger.getScriptSource", {scriptId:"" + scriptId});
        source = ("(function (exports, require, module, __filename, __dirname) { " + source + "\n});");
        if (orgSourceInfo.scriptSource == source) {
            return; // ★ 源码文本没变就直接跳过
        }
        let m = puerts.getModuleByUrl(url);
        puerts.emit('HMR.prepare', moduleName, m, url);
        await sendCommand("Debugger.setScriptSource", {scriptId:"" + scriptId, scriptSource:source});
        puerts.emit('HMR.finish', moduleName, m, url);
        //puerts.forceReload(url); // ★ 当前实现没有把 module cache invalidation 接进默认 HMR 路径
    }
}
```

[2] `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4085-4108,4113-4202,4604-4765`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 函数: FAngelscriptClassGenerator::PrepareSoftReload / DoSoftReload
// 位置: 4085-4108, 4113-4202, 4604-4765，soft reload 会重建对象与 CDO
// ============================================================================
void FAngelscriptClassGenerator::PrepareSoftReload(FModuleData& ModuleData, FClassData& ClassData)
{
    GConstructASObjectWithoutDefaults = true;
    UObject* CDONoDefaults = NewObject<UObject>(GetTransientPackage(), Class,
        MakeUniqueObjectName(GetTransientPackage(), Class, *(Class->GetDefaultObjectName().ToString() + TEXT("_NoDefaults"))),
        RF_ArchetypeObject);

    // ★ 先构造一个“不带 defaults”的临时 CDO，用作后续差分复制的基线
    DestructScriptObject((asCScriptObject*)CDONoDefaults, Class, (asCObjectType*)ClassData.OldClass->ScriptType);
    ReinitializeScriptObject((asCScriptObject*)CDONoDefaults, Class, (asCObjectType*)ClassData.OldClass->ScriptType);
    ClassData.CDONoDefaults = CDONoDefaults;
}

void FAngelscriptClassGenerator::DoSoftReload(FModuleData& ModuleData, FClassData& ClassData)
{
    GetObjectsOfClass(Class, Instances, true, RF_NoFlags);
    ClassDesc->DefaultsCode = ClassData.OldClass->DefaultsCode; // ★ soft reload 先保留旧 defaults code

    for (auto PropDesc : ClassDesc->Properties)
    {
        FProperty* Property = Class->FindPropertyByName(*PropDesc->PropertyName);
        if (Property->GetOwnerClass() != Class)
            continue;

        Class->SetPropertiesSize(PropDesc->ScriptPropertyOffset);
        Property->Link(ArDummy); // ★ 先把属性 offset / link 关系重新接好
    }

    DestroyAngelscriptUnversionedSchema(Class);
    asITypeInfo* ScriptType = GetNamespacedTypeInfoForClass(ClassData.NewClass, ModuleData.NewModule);
    Class->ScriptTypePtr = ScriptType; // ★ 类的 script type 会被换到新类型
}

DestructScriptObject(ScriptObject, Class, (asCObjectType*)OldScriptType);
ReinitializeScriptObject(ScriptObject, Class, (asCObjectType*)ScriptType);
// ★ 旧 script object 会真的析构再重建，然后把需要保留的属性从临时 buffer 复制回去

DestructScriptObject(ScriptObject, Class, (asCObjectType*)OldScriptType);
ReinitializeScriptObject(ScriptObject, Class, (asCObjectType*)ScriptType);
// ★ CDO 也走同样的 reinstance 路线，而不是只换函数入口
```

[3] `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:27-145`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp
// 函数: FClassReloadHelper::FReloadState::PerformReinstance
// 位置: 27-145，编辑器侧还会刷新 Blueprint 依赖
// ============================================================================
void FClassReloadHelper::FReloadState::PerformReinstance()
{
    AngelscriptEditor::BlueprintImpact::FBlueprintImpactSymbols ImpactSymbols;
    for (const auto& ReloadClass : ReloadClasses)
    {
        ImpactSymbols.Classes.Add(ReloadClass.Key);
        ImpactSymbols.ReplacementObjects.Add(ReloadClass.Key, ReloadClass.Value);
    }

    for (TObjectIterator<UBlueprint> BlueprintIt; BlueprintIt; ++BlueprintIt)
    {
        UBlueprint* BP = *BlueprintIt;
        const bool bHasDependency = AngelscriptEditor::BlueprintImpact::AnalyzeLoadedBlueprint(*BP, ImpactSymbols, ImpactReasons);

        FBlueprintEditorUtils::GetAllNodesOfClass(BP, AllNodes);
        for (UK2Node* Node : AllNodes)
        {
            for (auto* Pin : Node->Pins)
            {
                ReplacePinType(Pin->PinType); // ★ 直接把受影响 pin 替到新 struct / enum / delegate
            }
        }

        if (bHasDependency)
        {
            DependencyBPs.Add(BP); // ★ 依赖 Blueprint 会被收集出来做后续重编译
        }
    }
}
```

设计取舍：

- `puerts` 的收益是 patch 很轻，不必重建 module graph，也不用遍历整个 UObject 世界。
- 代价是当前主链不会默认重跑 `module factory`，模块级状态只能靠 `HMR.prepare/HMR.finish` 事件或脚本自约定处理，插件层没有看到显式 `module.hot.accept/dispose` 契约。
- `Angelscript` 的代价是 reload 链更重，需要碰类布局、实例、CDO 和编辑器资产；收益是 UE 对象图与 Blueprint 依赖会跟着一起收敛到新状态。

与 Angelscript 对比：

| 对比点 | puerts 新证据 | Angelscript 新证据 | 差距判断 |
| --- | --- | --- | --- |
| HMR 默认替换目标 | `Debugger.setScriptSource(...)` 改已解析脚本文本 | `DestructScriptObject` + `ReinitializeScriptObject` 重建 script object / CDO | 实现方式不同 |
| 模块 cache 处理 | `puerts.forceReload(url)` 存在但在默认 HMR 主链里被注释掉 | 不依赖 module cache，而是直接 reinstance 对象图 | 实现方式不同 |
| 脚本状态保持层级 | `module.exports` 与闭包状态默认延续 | 通过临时 buffer 复制实例/默认值、刷新 `ScriptTypePtr` | Angelscript 在 UE 对象级状态收敛上实现更完整 |
| 编辑器侧联动 | 当前片段只看到 `HMR.prepare/HMR.finish` 事件 | `ClassReloadHelper` 会刷新 Blueprint pin 与依赖 Blueprint | puerts 在当前路径没有实现同等级 Blueprint reload 收敛链 |

### [维度 D2] 反射绑定机制：`puerts` 用可重建 translator 缓存吸收反射形状变化，`Angelscript` 先判定 reload 等级再决定哪些绑定允许原位修补

前面的 D2 已经说明 puerts 有 `DefineClass<T>()` DSL；这轮补的是 **绑定对象在变化后怎么维护**。`StructWrapper` 并不会每次都重建一整套 wrapper：属性走 `PropertiesMap`，已有条目时直接 `FPropertyTranslator::CreateOn(...)` 在旧内存上 placement-new 新 translator；函数走 `MethodsMap/FunctionsMap`，已有条目时只 `Init(...)` 重新读取新的 `UFunction`。同时，EditorOnly 方法还会额外挂一个 `_EditorOnly` 别名，兼容 `PuertsEditor` 侧旧代码继续按后缀名访问。

`Angelscript` 则更保守。`AngelscriptClassGenerator` 会先把“超类变化、属性类型变化、定义变化、默认值变化、metadata 变化、脚本大小变化”等全部折算成 `SoftReload / FullReloadSuggested / FullReloadRequired`；只有落在软重载安全区里的部分，`SoftReloadFunction()` 才会去把 `Usage.ScriptClass` 通过 `UpdatedScriptTypeMap` 指向新类型。也就是说，puerts 的增量维护更像“局部热修 translator”，Angelscript 的增量维护更像“先做一致性证明，再允许局部修补”。

```
[puerts] Translator Refresh
UE reflected property/function changes
 -> StructWrapper PropertiesMap / MethodsMap hit
 -> PropertyTranslator::CreateOn(old translator memory)
 -> FFunctionTranslator::Init(new UFunction)
 -> existing wrapper/template keeps identity
 -> optional _EditorOnly alias keeps old callers alive

[Angelscript] Reload Gating
old desc vs new desc
 -> compare super/property/method/default/meta/script size
 -> classify SoftReload / FullReloadSuggested / FullReloadRequired
 -> only safe cases call SoftReloadFunction()
 -> UpdatedScriptTypeMap repoints type usages
```

[1] `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:21-35,38-69,267-306` 与 `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1390-1424`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp
// 位置: 21-35, 38-69, 267-306，wrapper 里的 translator 会被原位刷新
// ============================================================================
std::shared_ptr<FPropertyTranslator> FStructWrapper::GetPropertyTranslator(PropertyMacro* InProperty)
{
    auto Iter = PropertiesMap.Find(InProperty->GetFName());
    if (!Iter)
    {
        std::shared_ptr<FPropertyTranslator> PropertyTranslator = FPropertyTranslator::Create(InProperty);
        PropertiesMap.Add(InProperty->GetFName(), PropertyTranslator);
        Properties.push_back(PropertyTranslator);
        return PropertyTranslator;
    }
    FPropertyTranslator::CreateOn(InProperty, Iter->get()); // ★ 命中缓存后不换 map key，直接在旧对象内存上重建 translator
    return *Iter;
}

std::shared_ptr<FFunctionTranslator> FStructWrapper::GetMethodTranslator(UFunction* InFunction, bool IsExtension)
{
    auto Iter = MethodsMap.Find(InFunction->GetFName());
    if (!Iter)
    {
        auto FunctionTranslator = std::make_shared<FFunctionTranslator>(InFunction, false);
        MethodsMap.Add(InFunction->GetFName(), FunctionTranslator);
        return FunctionTranslator;
    }
    Iter->get()->Init(InFunction, false); // ★ 旧 translator 复用，内部重新读取 UFunction 形状
    return *Iter;
}

if (puerts::IsEditorOnlyUFunction(Function))
{
    FString SuffixFuncName = FuncName + EditorOnlyPropertySuffix.GetData();
    AdditionalKey = FV8Utils::InternalString(Isolate, SuffixFuncName);
}
Result->PrototypeTemplate()->Set(Key, FunctionTranslator->ToFunctionTemplate(Isolate));
Result->PrototypeTemplate()->Set(AdditionalKey, FunctionTranslator->ToFunctionTemplate(Isolate));
// ★ EditorOnly 方法会同时保留原名和 `_EditorOnly` 别名，兼容旧调用点
```

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp
// 位置: 1390-1424，原位重建 translator 的关键入口
// ============================================================================
std::unique_ptr<FPropertyTranslator> FPropertyTranslator::Create(PropertyMacro* InProperty, bool IgnoreOut)
{
    return PropertyTranslatorCreator<UniquePtrCreator, std::unique_ptr<FPropertyTranslator>>::Do(InProperty, IgnoreOut, nullptr);
}

void FPropertyTranslator::CreateOn(PropertyMacro* InProperty, FPropertyTranslator* InOldProperty)
{
    check(InOldProperty);
    PropertyTranslatorCreator<PlacementNewCreator, FPropertyTranslator*>::Do(InProperty, true, InOldProperty);
    // ★ 这里不是 update field，而是直接 placement-new 成新的具体 translator 子类
}
```

[2] `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:1077-1324,4779-4808`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 位置: 1077-1324, 4779-4808，先判定 reload 等级，再做局部类型修补
// ============================================================================
if (ClassData.OldClass->SuperClass != ClassData.NewClass->SuperClass)
{
    ClassData.ReloadReq = EReloadRequirement::FullReloadRequired; // ★ 超类变了，直接禁止原位修补
}

if (PropertyDesc->PropertyType != OldPropertyDesc->PropertyType)
{
    ClassData.ReloadReq = EReloadRequirement::FullReloadRequired; // ★ 属性类型变化也升级为 full reload
}

if (!OldFunctionDesc->SignatureMatches(NewFunctionDesc))
{
    ClassData.ReloadReq = EReloadRequirement::FullReloadRequired; // ★ 方法签名变化不能只换 translator
}

if (NewArgument.DefaultValue != OldArgument.DefaultValue
    || NewArgument.ArgumentName != OldArgument.ArgumentName)
{
    ClassData.ReloadReq = EReloadRequirement::FullReloadSuggested; // ★ 默认值/参数名变化先建议 full reload
}

void FAngelscriptClassGenerator::SoftReloadFunction(UFunction* Function)
{
    auto* ASFunction = Cast<UASFunction>(Function);
    for (int32 i = 0, Num = ASFunction->Arguments.Num(); i < Num; ++i)
        SoftReloadType(ASFunction->Arguments[i].Type);
    SoftReloadType(ASFunction->ReturnArgument.Type);
}

void FAngelscriptClassGenerator::SoftReloadType(FAngelscriptTypeUsage& Usage)
{
    if (Usage.ScriptClass != nullptr)
    {
        asITypeInfo** NewType = UpdatedScriptTypeMap.Find(Usage.ScriptClass);
        if (NewType != nullptr)
            Usage.ScriptClass = *NewType; // ★ 只有安全区里的软重载，才做这种局部 type repoint
    }
}
```

设计取舍：

- `puerts` 的局部 translator 刷新非常灵活，运行时对象身份更稳定，适合跟 UE 反射对象做增量同步。
- 代价是“哪些变化仍然安全”更多由 translator 自己承担，插件层没有像 Angelscript 那样先把修改分类成 reload 等级。
- `Angelscript` 的保守 gating 会让更多变化升级成 full reload，但好处是对象布局、默认值和 Blueprint 事件这类高风险变更不会悄悄滑进原位更新路径。

与 Angelscript 对比：

| 对比点 | puerts 新证据 | Angelscript 新证据 | 差距判断 |
| --- | --- | --- | --- |
| 增量维护载体 | `PropertiesMap/MethodsMap` 里的 translator 原位刷新 | 先算 `ReloadReq`，再决定是否 `SoftReloadFunction()` | 实现方式不同 |
| 变化判定策略 | 命中缓存就 `CreateOn()` / `Init()` | 超类、属性类型、函数签名、script size 等先做一致性判定 | Angelscript 在安全门控上实现更严格 |
| 符号兼容层 | EditorOnly 方法额外挂 `_EditorOnly` 别名 | 当前路径未见等价的双名字兼容层 | puerts 在 editor-side API 兼容层上实现更细 |
| 软更新范围 | 更偏 wrapper / translator 级局部修补 | 更偏 class/function/type usage 级受控修补 | 实现方式不同 |

### [维度 D11] 部署与打包：`puerts` 的可选 FFI 会把发布物扩成“VM + JS + libffi ABI shim”，`Angelscript` 当前发布边界仍是编译期绑定表

这一轮补的是一个前文没展开的部署边界：`puerts` 在 `JsEnv.Build.cs` 里还有 `WithFFI` 开关。它默认关掉，但一旦打开，就会按平台把 `ffi.lib` / `libffi.a` 加进构建，同时继续把 `Content/` 整体拷到目标内容目录。运行时侧 `FFIBinding.cpp` 会把 `ffi_prep_cif`、`ffi_call`、closure、`FFI_TYPES` 和各种 ABI 常量注册成 `ffi_bindings` native module；JS 侧 `ffi/binding.js` 再把原始指针和 `ffi_cif` 封装成 `binding()` / `allocClosure()`。这说明 puerts 的部署边界不只是“带上选中的 JS VM 和脚本根”，而是 **可选地再带上一层 raw C ABI bridge**。

Angelscript 当前路径则仍停在编译期 native bind。UHT 工具生成的是 `FAngelscriptBinds::AddFunctionEntry(...)` 注册语句，运行时再通过 `RegisterObjectType(...)`、`RegisterGlobalFunction(...)` 把这些条目编进 AngelScript 引擎。也就是说，它的发布物里当然包含 native 代码，但没有看到与 puerts `ffi_bindings` 等价的“脚本侧自由拼 `ffi_cif` / closure”的 runtime surface。这里应该判成“当前路径没有实现”，不是“实现差”。

```
[puerts] Optional FFI Shipping
JsEnv.Build.cs (WithFFI)
 -> add libffi headers/libs per platform
 -> copy Content/JavaScript/ffi/*
 -> native module ffi_bindings exports cif/call/closure/types
 -> JS binding.js builds ABI wrappers at runtime

[Angelscript] Compile-Time Native Bind Shipping
UHT tool
 -> AddFunctionEntry(...)
 -> AngelscriptBinds RegisterObjectType/RegisterGlobalFunction
 -> native functions become fixed script surface
 -> no equivalent runtime ffi_bindings surface in current path
```

[1] `Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs:38,172-176,331-357`

```csharp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs
// 位置: 38, 172-176, 331-357，FFI 是一个可选的构建/部署面
// ============================================================================
private bool WithFFI = false;

if (WithFFI) AddFFI(Target);
string coreJSPath = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", "Content"));
string destDirName = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", "..", "..", "Content"));
DirectoryCopy(coreJSPath, destDirName, true);
// ★ 一旦开启，对应 JS 资产也会跟着被同步到 Content 目录

void AddFFI(ReadOnlyTargetRules Target)
{
    if (Target.Platform == UnrealTargetPlatform.Win64)
    {
        PublicIncludePaths.AddRange(new string[] {Path.Combine(HeaderPath, "ffi", "Win64")});
        PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "ffi", "Win64", "ffi.lib"));
    }
    else if (Target.Platform == UnrealTargetPlatform.Mac)
    {
        PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "ffi", "macOS", "libffi.a"));
    }
    else if (Target.Platform == UnrealTargetPlatform.IOS)
    {
        PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "ffi", "iOS", "libffi.a"));
    }
    else if (Target.Platform == UnrealTargetPlatform.Android)
    {
        PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "ffi", "Android", "armeabi-v7a", "libffi.a"));
        PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "ffi", "Android", "arm64-v8a", "libffi.a"));
    }

    PrivateDefinitions.Add("WITH_FFI"); // ★ FFI 是明确的条件编译面
}
```

[2] `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FFIBinding.cpp:258-297,449-532,593-601` 与 `Reference/puerts/unreal/Puerts/Content/JavaScript/ffi/binding.js:8-109`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FFIBinding.cpp
// 位置: 258-297, 449-532, 593-601，把 raw C ABI 能力暴露给 JS
// ============================================================================
static void FFICall(const v8::FunctionCallbackInfo<v8::Value>& Info)
{
    char* Cif = ArrayBufferData(Info[0]);
    char* FuncPtr = Info[1]->IsNumber() ? reinterpret_cast<char*>(GFuncArray[Info[1]->Uint32Value(Context).ToChecked()])
                                        : ArrayBufferData(Info[1]);
    char* ReturnPtr = ArrayBufferData(Info[2]);
    char* ArgsPtr = ArrayBufferData(Info[3]);

    ffi_call(reinterpret_cast<ffi_cif*>(Cif), FFI_FN(FuncPtr),
        reinterpret_cast<void*>(ReturnPtr), reinterpret_cast<void**>(ArgsPtr));
    // ★ JS 侧最终可以拿到一个原始 `ffi_call` 面，而不是只能走 UE 反射绑定
}

Exports->Set(Context, "ffi_prep_cif", v8::FunctionTemplate::New(Isolate, FFIPrepCif)->GetFunction(Context).ToLocalChecked()).Check();
Exports->Set(Context, "ffi_call", v8::FunctionTemplate::New(Isolate, FFICall)->GetFunction(Context).ToLocalChecked()).Check();
Exports->Set(Context, "ffi_alloc_closure", v8::FunctionTemplate::New(Isolate, FFIAllocClosure)->GetFunction(Context).ToLocalChecked()).Check();
Exports->Set(Context, "ffi_free_closure", v8::FunctionTemplate::New(Isolate, FFIFreeClosure)->GetFunction(Context).ToLocalChecked()).Check();
// ★ `ffi_bindings` 直接把 CIF / 调用 / closure 生命周期全部暴露到 JS

SET_FFI_TYPE("int32", ffi_type_sint32);
SET_FFI_TYPE("double", ffi_type_double);
SET_FFI_TYPE("pointer", ffi_type_pointer);
Exports->Set(Context, "FFI_TYPES", Types).Check();
SET_Property("FFI_CIF_SIZE", sizeof(ffi_cif));
// ★ 类型表和 ABI 常量也作为运行时公开数据导出

PUERTS_MODULE(ffi_bindings, Init); // ★ 最终作为一个可 require 的 native module 发布
```

```javascript
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Content/JavaScript/ffi/binding.js
// 位置: 8-109，JS 侧把原始 ABI 面封装成 binding/closure
// ============================================================================
const ffi_bindings = require('ffi_bindings');

function allocCif(returnType, parameterTypes, abi, fixArgNum) {
    let cifPtr = new Uint8Array(ffi_bindings.FFI_CIF_SIZE);
    let status = ffi_bindings.ffi_prep_cif(cifPtr, abi, parameterTypes.length, returnType.ffi_type, pointer.alloc(...param_ffi_types));
    if (status != 0) {
        throw new Error(`call ffi_prep_cif fail, status=${status}`);
    }
    return cifPtr;
}

function binding(func, abi, returnType, parameterTypes, fixArgNum) {
    const cifPtr = allocCif(returnType, parameterTypes, abi, fixArgNum);
    const ffi_call_args = pointer.alloc(...argPtrs);
    function wrap(...args) {
        for (var i = 0; i < expectArgNum; i++) {
            parameterTypes[i].write(argPtrs[i], args[i]);
        }
        ffi_call(cifPtr, func, retPtr, ffi_call_args);
        return resultProcesser(returnType.read(retPtr));
    }
    return wrap; // ★ JS 层可以在运行时拼出新的 native call wrapper
}

function allocClosure(func, abi, returnType, parameterTypes) {
    const cifPtr = allocCif(returnType, parameterTypes, abi);
    return ffi_bindings.ffi_alloc_closure(cifPtr, function(retPtr, argPtrs) {
        var result = func.apply(null, args);
        returnType.write(retPtr, result);
    }, returnType.size);
}
```

[3] `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:14-22` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:260-289,588-608`

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs
// 位置: 14-22，UHT 生成的是编译期注册语句
// ============================================================================
internal sealed record AngelscriptGeneratedFunctionEntry(
    string ClassName,
    string FunctionName,
    string EraseMacro)
{
    public string BuildRegistrationLine()
    {
        return $"\tFAngelscriptBinds::AddFunctionEntry({ClassName}::StaticClass(), \"{FunctionName}\", {{ {EraseMacro} }});";
        // ★ 这里产出的是“把某个 native 函数编进绑定表”的代码，不是 runtime FFI surface
    }
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp
// 位置: 260-289, 588-608，运行时继续把固定条目注册进 AngelScript 引擎
// ============================================================================
int TypeId = Manager.Engine->RegisterObjectType(ClassName.ToCString(), Size, Flags);
int FunctionId = Manager.Engine->RegisterObjectMethod(ClassName.ToCString(), Signature.ToCString(), asFUNCTION(Fun), asCALL_GENERIC, nullptr);
int FunctionId = Manager.Engine->RegisterGlobalFunction(Signature.ToCString(), Function, asCALL_CDECL, *(asFunctionCaller*)&Caller, nullptr);
// ★ 当前路径看到的是“固定注册表 -> 引擎 API”，没有等价于 `ffi_bindings` 的脚本侧自由 ABI 组装面
```

设计取舍：

- `puerts` 一旦打开 `WithFFI`，发布物需要一起携带平台对应的 `libffi` 与 JS wrapper，部署复杂度上升，但脚本侧获得了更宽的 native ABI 组合能力。
- `Angelscript` 的 native 面更收敛，功能基本都在编译期绑定表里定义，发布物边界更稳定，但脚本侧没有看到等价的 runtime FFI 拼装能力。
- 这里的差异不是“哪边支持 native”，而是“native 能力是在编译期定表，还是在运行时继续向脚本开放 ABI 组合层”。

与 Angelscript 对比：

| 对比点 | puerts 新证据 | Angelscript 新证据 | 差距判断 |
| --- | --- | --- | --- |
| 可选部署组件 | `WithFFI` 会额外带入 `ffi.lib/libffi.a` 与 `Content/JavaScript/ffi/*` | 当前主链是 UHT 生成的 `AddFunctionEntry(...)` 与 runtime 注册 | 实现方式不同 |
| 脚本侧 ABI 面 | `ffi_bindings` 暴露 `ffi_prep_cif` / `ffi_call` / closure / `FFI_TYPES` | 当前路径只看到固定 `RegisterObjectType/RegisterGlobalFunction` | Angelscript 在当前路径没有实现等价 runtime FFI surface |
| 平台适配方式 | Win64 / macOS / iOS / Android 分别选 `libffi` 产物 | 无等价 `WithFFI` 多平台原始 ABI shim | puerts 有实现，Angelscript 当前路径没有实现 |
| 发布物复杂度 | VM 之外还可叠加 raw ABI bridge | 发布边界主要是绑定表与脚本/缓存 | 实现方式不同 |

---

## 深化分析 (2026-04-08 23:57:31)

### [维度 D1] 插件架构与模块划分：`puerts` 的多后端抽象是“源码级 faux-V8 façade”，`Angelscript` 则直接把脚本引擎 API 当作架构锚点

前面的 D1 已经覆盖模块数量和 `Build.cs` 切换；这一轮补的是 **同一套 bridge C++ 到底如何跨 backend 复用**。`puerts` 在 `ThirdPartyQJS()` 里不仅切到 `WITH_QUICKJS`，还显式定义 `QJSV8NAMESPACE=v8_qjs`；随后 `NamespaceDef.h` 直接做 `namespace v8 = v8_qjs;`。于是 `CppObjectMapper.cpp` 这种桥接核心文件仍然大面积写成 `v8::FunctionTemplate`、`v8::UniquePersistent`、`v8::Symbol` 风格，只有极少数 QuickJS 无法仿真的点才落成 `#ifndef WITH_QUICKJS`。这说明 puerts 的多后端抽象不只是运行时切换，而是 **把大多数 backend 差异压进一个“V8 形状兼容层”**。

`Angelscript` 的路径正好相反。`AngelscriptBinds.cpp` 直接包含 `source/as_scriptengine.h`、`source/as_scriptfunction.h` 等内部头，`FAngelscriptBinds` 构造与方法里直接调用 `RegisterObjectType()`、`RegisterObjectMethod()`、`RegisterObjectBehaviour()`。也就是说，它没有设计“可替换脚本引擎 façade”，而是把 AngelScript 自身 API 视为稳定内核。对单引擎方案来说，这降低了中间抽象成本；但“切 backend”也因此不是它的架构目标。

```
[puerts] Source-Level Backend Façade
JsEnv.Build.cs
 -> WITH_QUICKJS + QJSV8NAMESPACE=v8_qjs
 -> NamespaceDef.h aliases v8 -> v8_qjs
 -> CppObjectMapper / StructWrapper / JsEnvImpl keep using v8::*
 -> only non-emulatable features fall out as #ifdef

[Angelscript] Direct Engine Coupling
source/as_scriptengine.h
 -> FAngelscriptBinds
 -> RegisterObjectType / Method / Behaviour
 -> engine API is the architectural center
```

[1] `Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs:624-632`

```csharp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs
// 位置: 624-632，QuickJS 路径不仅切库，还切“伪 V8 命名空间”
// ============================================================================
void ThirdPartyQJS(ReadOnlyTargetRules Target)
{
    PrivateDefinitions.Add("WITHOUT_INSPECTOR");
    PrivateDefinitions.Add("WITH_QUICKJS");
    if (QjsNamespaceSuffix)
    {
        PublicDefinitions.Add("WITH_QJS_NAMESPACE_SUFFIX=1");
        PublicDefinitions.Add("QJSV8NAMESPACE=v8_qjs"); // ★ 让后续 bridge 代码继续写成 `v8::*`
    }
}
```

[2] `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/NamespaceDef.h:13-15` 与 `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/CppObjectMapper.cpp:49-72,97-102`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Public/NamespaceDef.h
// 位置: 13-15，把 QuickJS 后端伪装成 `v8` 命名空间
// ============================================================================
#if defined(WITH_QJS_NAMESPACE_SUFFIX)
namespace v8 = v8_qjs; // ★ bridge 侧大部分源码不需要改函数签名
#endif
```

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/CppObjectMapper.cpp
// 位置: 49-72, 97-102，同一份 mapper 代码仍然直接面向 `v8::*`
// ============================================================================
v8::MaybeLocal<v8::Function> FCppObjectMapper::LoadTypeById(v8::Local<v8::Context> Context, const void* TypeId)
{
    auto ClassDef = puerts::LoadClassByID(TypeId);
    if (!ClassDef)
    {
        return v8::MaybeLocal<v8::Function>();
    }
    auto Template = GetTemplateOfClass(Context->GetIsolate(), ClassDef);
    return Template->GetFunction(Context);
}

void FCppObjectMapper::Initialize(v8::Isolate* InIsolate, v8::Local<v8::Context> InContext)
{
    auto LocalTemplate = v8::FunctionTemplate::New(InIsolate, PointerNew);
    PointerTemplate = v8::UniquePersistent<v8::FunctionTemplate>(InIsolate, LocalTemplate);
#ifndef WITH_QUICKJS
    PrivateKey.Reset(InIsolate, v8::Symbol::New(InIsolate)); // ★ 只有无法等价映射的点才漏出 backend 分支
#endif
}

auto ClassDefinition = LoadClassByID(TypeId);
if (ClassDefinition)
{
    auto Result = GetTemplateOfClass(Isolate, ClassDefinition)->InstanceTemplate()->NewInstance(Context).ToLocalChecked();
    BindCppObject(Isolate, const_cast<JSClassDefinition*>(ClassDefinition), Ptr, Result, PassByPointer);
    return Result;
}
```

[3] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:11-20,260-296`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp
// 位置: 11-20, 260-296，绑定核心直接贴着 AngelScript 引擎 API 写
// ============================================================================
#include "source/as_property.h"
#include "source/as_scriptfunction.h"
#include "source/as_objecttype.h"
#include "source/as_scriptengine.h"
// ★ 这里没有“脚本引擎兼容层”头文件，直接拉 AngelScript 内部 API

FAngelscriptBinds::FAngelscriptBinds(FBindString Name, asQWORD Flags, int32 Size)
    : ClassName(Name)
{
    auto& Manager = FAngelscriptEngine::Get();
    int TypeId = Manager.Engine->RegisterObjectType(ClassName.ToCString(), Size, Flags); // ★ 直接注册到 asIScriptEngine
}

void FAngelscriptBinds::GenericMethod(FBindString Signature, void(CDECL *Fun)(asIScriptGeneric*), void* UserData)
{
    auto& Manager = FAngelscriptEngine::Get();
    int FunctionId = Manager.Engine->RegisterObjectMethod(ClassName.ToCString(), Signature.ToCString(), asFUNCTION(Fun), asCALL_GENERIC, nullptr);
    OnBind(FunctionId, UserData, nullptr);
}

void FAngelscriptBinds::BindBehaviour(asEBehaviours Beh, FBindString Signature, asSFuncPtr Ptr, ASAutoCaller::FunctionCaller Caller)
{
    auto& Manager = FAngelscriptEngine::Get();
    int FunctionId = Manager.Engine->RegisterObjectBehaviour(ClassName.ToCString(), Beh, Signature.ToCString(), Ptr, asCALL_THISCALL, *(asFunctionCaller*)&Caller);
    OnBind(FunctionId, nullptr, nullptr);
}
```

设计取舍：

- `puerts` 通过 faux-V8 façade 把多数 bridge 文件维持成单份源码，降低了多 backend 下的文件分叉成本。
- 代价是抽象上限受 `v8_qjs` 可仿真能力约束，像 Inspector、`Symbol` 这类能力最终还是会漏成条件编译。
- `Angelscript` 不为“换引擎”支付抽象税，运行时和绑定器都能直接贴近 `asIScriptEngine`；这不是“少做一层”，而是明确接受单引擎前提。

与 Angelscript 对比：

| 对比点 | puerts 新证据 | Angelscript 新证据 | 差距判断 |
| --- | --- | --- | --- |
| 后端抽象落点 | `QJSV8NAMESPACE=v8_qjs` + `namespace v8 = v8_qjs` | 直接 `#include "source/as_scriptengine.h"` 并调用 `RegisterObjectType()` | 实现方式不同 |
| bridge 源码复用策略 | 大量核心文件保持 `v8::*` 形状，仅局部 `#ifdef` | 绑定代码直接以 AngelScript API 为稳定中心 | 实现方式不同 |
| 可替换引擎预留 | 有明确源码级兼容层 | 当前路径没有等价脚本引擎 façade | Angelscript 当前路径没有实现该类多后端 façade |
| 抽象泄漏位置 | 主要在 Inspector / 特定对象语义等不可仿真点 | 无后端抽象，自然也无此类泄漏面 | 实现方式不同 |

### [维度 D6] 代码生成与 IDE 支持：`puerts` 生成的是“可写代码表面”，`Angelscript` 生成的是“稳定绑定键”

前面的 D6 已经说明 puerts 有 `ue.d.ts`、`cpp/index.d.ts` 和 `LanguageService`；这一轮补的是 **这些生成产物优先服务谁**。从 `DeclarationGenerator.cpp` 看，puerts 的声明生成不是“把反射类型原样抄一份”。它会先用 `SafeName()` / `SafeParamName()` 把空格、`-`、`/`、`?`、前导数字和 TypeScript 关键字整理成可写标识符；再把 Blueprint 所在 package path 映射成 TS `namespace`；生成函数签名时，如果参数有 `CPP_Default_*`，就把参数标成 optional，并把默认值以注释保留下来；对象参数可空时会包 `$Nullable<>`，`out` 引用会包 `$Ref<>`；甚至父类存在但当前类不支持的 overload，也会以 `@deprecated Unsupported super overloads.` 形式继续暴露。这一整套动作说明 puerts 追求的是 **IDE 可写性与类型连续性优先的 authoring surface**，而不是 1:1 还原 runtime bind table。

`Angelscript` UHT 工具的目标不同。`AngelscriptHeaderSignatureResolver` 会先把 `UPARAM(...)`、默认值、参数末尾标识符都剥掉，再交给 `BuildRegistrationLine()` 生成 `FAngelscriptBinds::AddFunctionEntry(...)`。也就是说，它生成的是“稳定的 native 绑定键”，不是给用户书写脚本时直接消费的声明表面。这解释了为什么当前 Angelscript 的 IDE 支撑更偏导航、调试数据库和真实编译器反馈，而不是像 puerts 那样维护一份很厚的离线 authoring contract。

```
[puerts] Authoring Surface Normalization
UE names / package path / metadata / defaults
 -> SafeName / SafeParamName
 -> package path -> namespace
 -> optional param + default comment + $Nullable/$Ref
 -> keep unsupported super overloads as deprecated
 -> ue.d.ts becomes IDE-safe writable surface

[Angelscript] Binding Signature Reduction
UHT raw header text
 -> StripLeadingUparam
 -> StripDefaultValue
 -> StripTrailingIdentifier
 -> BuildRegistrationLine(AddFunctionEntry)
 -> runtime bind table key
```

[1] `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:68-105,469-519,1000-1052,1174-1224`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp
// 位置: 68-105, 469-519, 1000-1052, 1174-1224，声明生成主动为 IDE 整理“可写表面”
// ============================================================================
static FString SafeName(const FString& Name)
{
    auto Ret = Name.Replace(TEXT(" "), TEXT(""))
                   .Replace(TEXT("-"), TEXT("_"))
                   .Replace(TEXT("/"), TEXT("_"))
                   .Replace(TEXT("?"), TEXT("$"));
    if (Ret.Len() > 0 && (TCHAR)'0' <= Ret[0] && Ret[0] <= (TCHAR)'9')
    {
        return TEXT("_") + Ret; // ★ 前导数字也会被修成合法标识符
    }
    return Ret;
}

static FString SafeParamName(const FString& Name)
{
    auto Ret = SafeName(Name);
    return IsTypeScriptKeyword(Ret) ? (TEXT("_") + Ret) : Ret; // ★ TS 关键字再额外避让一次
}

Pkg->GetName().ParseIntoArray(PathFrags, TEXT("/"));
for (int i = 0; i < PathFrags.Num(); i++)
{
    PathFrags[i] = PUERTS_NAMESPACE::FilenameToTypeScriptVariableName(PathFrags[i]);
}
NamespaceMap[Obj] = FString::Join(PathFrags, TEXT(".")); // ★ Blueprint 路径会变成 TS namespace

TmpBuf << DeDup(SafeParamName(Property->GetName()));
if (DefaultValuePtr)
{
    TmpBuf << "?"; // ★ 有默认值时直接把参数变成 optional
}
TmpBuf << ": ";

if (IsNullable)
{
    TmpBuf << "$Nullable<"; // ★ 对象空值语义显式进声明
}
if (IsReference)
{
    TmpBuf << "$Ref<"; // ★ out/ref 语义也显式进声明
}

if (DefaultValuePtr)
{
    TmpBuf << " /* = " << *DefaultValuePtr << " */"; // ★ 默认值保留为注释，而不是完全丢掉
}

if (!Overloads.Contains(*SuperOverloadIter))
{
    Buff << "    /**\n";
    Buff << "     * @deprecated Unsupported super overloads.\n";
    Buff << "     */\n";
    Buff << "    " << *SuperOverloadIter << ";\n"; // ★ 不支持的父类 overload 也先保留给 IDE 看见
}
```

[2] `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs:558-569,638-675` 与 `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:14-22`

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs
// 位置: 558-569, 638-675，UHT 先把“书写层噪音”剥掉，再进入绑定签名
// ============================================================================
foreach (string rawParameter in rawParameters)
{
    string parameter = StripDefaultValue(StripLeadingUparam(rawParameter.Trim()));
    results.Add(StripTrailingIdentifier(parameter)); // ★ 默认值、UPARAM 包装、参数名都不是最终目标
}

private static string StripDefaultValue(string parameter)
{
    for (int index = 0; index < parameter.Length; index++)
    {
        switch (parameter[index])
        {
            case '=':
                if (angleDepth == 0 && parenDepth == 0 && braceDepth == 0)
                {
                    return parameter.Substring(0, index).Trim(); // ★ 直接裁掉默认值
                }
                break;
        }
    }
    return parameter.Trim();
}

private static string StripLeadingUparam(string parameter)
{
    while (parameter.StartsWith("UPARAM(", StringComparison.Ordinal))
    {
        int closeParen = FindMatchingParen(parameter, parameter.IndexOf('('));
        parameter = parameter.Substring(closeParen + 1).Trim(); // ★ `UPARAM(...)` 也不会进最终签名
    }
    return parameter;
}
```

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs
// 位置: 14-22，生成目标是绑定注册语句，不是 IDE 声明
// ============================================================================
internal sealed record AngelscriptGeneratedFunctionEntry(
    string ClassName,
    string FunctionName,
    string EraseMacro)
{
    public string BuildRegistrationLine()
    {
        return $"\tFAngelscriptBinds::AddFunctionEntry({ClassName}::StaticClass(), \"{FunctionName}\", {{ {EraseMacro} }});";
        // ★ 这里追求的是稳定绑定键，而不是把默认值/注释/可空性保留给 IDE
    }
}
```

设计取舍：

- `puerts` 的 `.d.ts` 更像“作者看到什么就能写什么”的契约，因此主动保留默认值、可空性、继承残影和名字合法化信息。
- 这种 authoring-first 路线会让声明面成为“安全超集”，与真实 runtime surface 不完全同构，但 IDE 体验更连续。
- `Angelscript` 当前生成链更偏运行时稳定性和 UHT 一致性，适合驱动绑定表，却不会自然长成 puerts 那样厚的 IDE 声明层。

与 Angelscript 对比：

| 对比点 | puerts 新证据 | Angelscript 新证据 | 差距判断 |
| --- | --- | --- | --- |
| 标识符合法化 | `SafeName()` / `SafeParamName()` 主动修名字 | 当前看到的是 `StripDefaultValue()` / `StripLeadingUparam()` 等“降噪”逻辑 | 实现方式不同 |
| 默认值暴露策略 | 参数 optional，并以注释保留 `CPP_Default_*` | 默认值在签名解析阶段被剥离 | Angelscript 当前路径没有实现同等级默认值声明输出 |
| 继承 overload 呈现 | 用 `@deprecated Unsupported super overloads.` 保留 authoring 可见性 | 当前生成链没有等价“声明超集”层 | Angelscript 当前路径没有实现等价声明兼容层 |
| 生成物服务对象 | IDE authoring surface | runtime bind table key | 实现方式不同 |

### [维度 D8] 性能与优化：`puerts` 把一部分绑定成本后移到“首次触达”，`Angelscript` 则在启动期一次性付清

前面的 D8 已经覆盖 `FastCall`、GC 协调和 scheduler；这一轮补的是 **绑定成本到底在什么时候支付**。`puerts` 这边，`pesapi_get_class_data(type_id, force_load)` 会在 `force_load=true` 时走 `LoadClassByID()`；`LoadClassByID()` 如果命中不到类型，就立刻触发 `ClassNotFoundCallback`，让外部补注册后再查一次。`FCppObjectMapper::LoadTypeById()` 和 `FindOrAddCppObject()` 又都在第一次真正需要某个类型/对象 wrapper 时才调用 `LoadClassByID()`。这意味着 puerts 可以把大量未被脚本触达的类型保持在“冷状态”，启动期不必全量建模板；代价是第一次触达某个冷类型时，运行时会额外承担注册和模板构造延迟。

`Angelscript` 的成本分布更前置。每个 `Bind_*.cpp` 里的静态 `FBind` 对象在模块加载时就把 lambda 塞进全局 bind 数组，`FAngelscriptEngine::BindScriptTypes()` 再统一调用 `FAngelscriptBinds::CallBinds(CollectDisabledBindNames())` 做有序注册，`WITH_DEV_AUTOMATION_TESTS` 下还会显式量测这一阶段。也就是说，Angelscript 倾向于在启动期把“有哪些类型、哪些方法可用”一次性收敛完，换来游戏期没有 class-not-found trampoline，类型可用性也更确定。

```
[puerts] Deferred Type Materialization
script wants class/object
 -> pesapi_get_class_data(force_load=true)
 -> LoadClassByID
 -> miss => ClassNotFoundCallback
 -> RegisterJSClass / SetClassTypeInfo
 -> mapper builds template / wrapper on first touch

[Angelscript] Upfront Bind Pass
static FBind in Bind_*.cpp
 -> RegisterBinds(bind order)
 -> BindScriptTypes()
 -> CallBinds(sorted lambdas)
 -> RegisterObjectType / Method before gameplay
```

[1] `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JSClassRegister.cpp:80-100`、`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PesapiV8Impl.cpp:1011-1025` 与 `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/CppObjectMapper.cpp:49-57,96-102`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JSClassRegister.cpp
// 位置: 80-100，类型真正缺失时才回调外部补注册
// ============================================================================
void OnClassNotFound(pesapi_class_not_found_callback InCallback)
{
    ClassNotFoundCallback = InCallback;
}

const JSClassDefinition* LoadClassByID(const void* TypeId)
{
    auto clsDef = FindClassByID(TypeId);
    if (!clsDef && ClassNotFoundCallback)
    {
        if (!ClassNotFoundCallback(TypeId))
        {
            return nullptr;
        }
        clsDef = FindClassByID(TypeId); // ★ miss 时才尝试补注册，而不是启动期全量铺开
    }
    return clsDef;
}
```

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PesapiV8Impl.cpp
// 位置: 1011-1025，是否强制装载由调用点显式决定
// ============================================================================
void* pesapi_get_class_data(const void* type_id, bool force_load)
{
    auto clsDef = force_load ? puerts::LoadClassByID(type_id) : puerts::FindClassByID(type_id);
    return clsDef ? clsDef->Data : nullptr; // ★ “查缓存”还是“触发补注册”是可选路径
}

void pesapi_on_class_not_found(pesapi_class_not_found_callback callback)
{
    puerts::OnClassNotFound(callback);
}
```

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/CppObjectMapper.cpp
// 位置: 49-57, 96-102，模板和 wrapper 都在首次实际需要时才构建
// ============================================================================
auto ClassDef = puerts::LoadClassByID(TypeId);
if (!ClassDef)
{
    return v8::MaybeLocal<v8::Function>();
}

auto ClassDefinition = LoadClassByID(TypeId);
if (ClassDefinition)
{
    auto Result = GetTemplateOfClass(Isolate, ClassDefinition)->InstanceTemplate()->NewInstance(Context).ToLocalChecked();
    BindCppObject(Isolate, const_cast<JSClassDefinition*>(ClassDefinition), Ptr, Result, PassByPointer);
    return Result; // ★ 第一次真正绑对象时，才把类型模板和实例 wrapper 都补齐
}
```

[2] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:438-467`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:151-154,195-214` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1915-1921`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h
// 位置: 438-467，静态 `FBind` 在模块装载期就把工作排进 bind 队列
// ============================================================================
struct ANGELSCRIPTRUNTIME_API FBind
{
    FBind(FName BindName, int32 BindOrder, TFunction<void()> Function)
    {
        FAngelscriptBinds::RegisterBinds(BindName, BindOrder, MoveTemp(Function));
    }

    FBind(EOrder BindOrder, TFunction<void()> Function)
    {
        FAngelscriptBinds::RegisterBinds((int32)BindOrder, MoveTemp(Function));
    }

    FBind(TFunction<void()> Function)
    {
        FAngelscriptBinds::RegisterBinds(0, MoveTemp(Function));
    }
};
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp
// 位置: 151-154, 195-214，启动时统一跑完整个 bind 队列
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
            continue;
        }
        Bind.Function(); // ★ 启动期直接把所有已登记 bind 依序执行
    }
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 1915-1921，绑定阶段是引擎启动管线里的显式步骤
// ============================================================================
void FAngelscriptEngine::BindScriptTypes()
{
    FAngelscriptBinds::CallBinds(CollectDisabledBindNames()); // ★ 启动时一次性付清 bind 成本
}
```

设计取舍：

- `puerts` 的优势是未触达类型可以一直保持冷状态，启动期与编辑器常驻成本更可控。
- 代价是首次触达冷类型的路径会掺入注册、模板创建和 wrapper 绑定，性能尖峰可能出现在运行中而不是启动中。
- `Angelscript` 选择前置成本，启动更重，但类型可用性和 steady-state 行为更确定，也更容易做启动阶段的统一观察与测试。

与 Angelscript 对比：

| 对比点 | puerts 新证据 | Angelscript 新证据 | 差距判断 |
| --- | --- | --- | --- |
| 绑定成本支付时机 | `LoadClassByID()` miss 时才触发 `ClassNotFoundCallback` | `BindScriptTypes()` 启动期统一 `CallBinds()` | 实现方式不同 |
| 冷类型策略 | 未触达类型可一直不注册 | 只要 bind 已登记，启动期就进入执行队列 | 实现方式不同 |
| 首次触达延迟 | 可能落在游戏期第一次 `LoadTypeById/FindOrAddCppObject` | 启动后无等价 class-not-found trampoline | Angelscript 在 steady-state 确定性上更强 |
| 启动期可观测性 | 当前主链更偏按需补注册 | `BindScriptTypes()` 是显式阶段，且可挂 bind timing 观察 | Angelscript 在启动期绑定可观测性上实现更完整 |

---

## 深化分析 (2026-04-09 00:05:12)

### [维度 D1] 插件架构与模块划分：`puerts` 用“多 `JsEnv` 分片”扩展运行时，`Angelscript` 用 `Clone` 共享同一脚本引擎

前面多轮已经把 `puerts` 的多后端抽象、`Build.cs` 开关和 faux-V8 façade 拆过了；这一轮补的是 **运行时拥有者到底是谁**。`puerts` 在 `FPuertsModule::MakeSharedJsEnv()` 里根据 `NumberOfJsEnv` 在“单 `FJsEnv`”和“`FJsEnvGroup` 里装多个 `FJsEnvImpl`”之间切换。组模式下并不是共享一个 VM 再做多上下文，而是把多个完整 `IJsEnv` 实例放进 `JsEnvList`，再用 `Selector(UObject*, Size)` 决定对象路由；只有 `CDO` / `Archetype` / `WasLoaded` 这种没有明确归属的对象，才广播到所有 env。

`Angelscript` 的扩展方向完全不同。`CreateCloneFrom()` 并不会复制一套新的 AngelScript engine，而是让 clone 直接拿源 engine 的 `SharedState`，其中包含 `ScriptEngine`、`PrimaryContext`、`PrecompiledData`、`StaticJIT`、`DebugServer` 与 bind/type database；clone 只增加 `ActiveParticipants` / `ActiveCloneCount` 计数，并在 owner 关闭时延迟真正释放。这说明两者都支持“多实例语义”，但 puerts 是 **多 VM 分片**，Angelscript 是 **单引擎多视图**。

```
[puerts] Runtime Ownership
UPuertsSetting.NumberOfJsEnv
 -> FPuertsModule::MakeSharedJsEnv
    ├─ 1 -> FJsEnv
    └─ N -> FJsEnvGroup
         ├─ shared IJSModuleLoader / ILogger
         ├─ JsEnv[0..N-1] with debugPort+i
         └─ Selector(UObject,size) routes calls

[Angelscript] Runtime Ownership
CreateCloneFrom(Source)
 -> SharedState{ScriptEngine, PrimaryContext, PrecompiledData, StaticJIT, DebugServer}
 -> ActiveParticipants++, ActiveCloneCount++
 -> clone shares bind/type database
 -> owner release deferred until clones leave
```

[1] `Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:192-241` 与 `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvGroup.cpp:105-131,176-182`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp
// 位置: 192-241，根据配置决定是单 env 还是 group env
// ============================================================================
NumberOfJsEnv = (Settings.NumberOfJsEnv > 1 && Settings.NumberOfJsEnv < 10) ? Settings.NumberOfJsEnv : 1;

if (NumberOfJsEnv > 1)
{
    JsEnvGroup = MakeShared<PUERTS_NAMESPACE::FJsEnvGroup>(NumberOfJsEnv,
        std::make_shared<PUERTS_NAMESPACE::DefaultJSModuleLoader>(Settings.RootPath),
        std::make_shared<PUERTS_NAMESPACE::FDefaultLogger>(),
        DebuggerPortFromCommandLine < 0 ? Settings.DebugPort : DebuggerPortFromCommandLine);

    if (Selector)
    {
        JsEnvGroup->SetJsEnvSelector(Selector); // ★ 运行时路由不是“共享一个 env”，而是对象级分流
    }

    JsEnvGroup->RebindJs();
}
else
{
    JsEnv = MakeShared<PUERTS_NAMESPACE::FJsEnv>(Settings.RootPath);
    JsEnv->RebindJs(); // ★ 单 env 模式仍是完整 FJsEnv 实例
}
```

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvGroup.cpp
// 位置: 105-131, 176-182，组模式实际维护多个 FJsEnvImpl
// ============================================================================
for (int i = 0; i < Size; i++)
{
    JsEnvList.push_back(std::make_shared<FJsEnvImpl>(SharedModuleLoader, InLogger, InDebugStartPort + i,
        InOnSourceLoadedCallback, InFlags, InExternalRuntime, InExternalContext));
    // ★ 每个槽位都是独立 FJsEnvImpl，只是共用 module loader / logger
}

auto GroupDynamicInvoker = MakeShared<FGroupDynamicInvoker, ESPMode::ThreadSafe>(JsEnvs);
for (int i = 0; i < JsEnvs.size(); i++)
{
    JsEnvs[i]->TsDynamicInvoker = GroupDynamicInvoker;
    JsEnvs[i]->MixinInvoker = GroupDynamicInvoker;
}

void FJsEnvGroup::SetJsEnvSelector(std::function<int(UObject*, int)> InSelector)
{
    auto DynamicInvoker = static_cast<FJsEnvImpl*>(JsEnvList[0].get())->TsDynamicInvoker;
    if (DynamicInvoker.IsValid())
    {
        static_cast<FGroupDynamicInvoker*>(DynamicInvoker.Get())->Selector = InSelector; // ★ 路由点挂在 invoker，而不是宿主模块外层
    }
}
```

[2] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:628-650,922-942,1135-1226`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 628-650, 922-942, 1135-1226，clone 共享同一个 SharedState
// ============================================================================
TUniquePtr<FAngelscriptEngine> FAngelscriptEngine::CreateCloneFrom(FAngelscriptEngine& Source, const FAngelscriptEngineConfig& InConfig, const FAngelscriptEngineDependencies& InDependencies)
{
    if (Source.SharedState.IsValid() == false && Source.OwnsEngine() && Source.GetScriptEngine() != nullptr)
    {
        Source.InitializeOwnedSharedState(); // ★ owner 先把 engine / JIT / debug server 封进 SharedState
    }

    TUniquePtr<FAngelscriptEngine> EngineInstance = MakeUnique<FAngelscriptEngine>(InConfig, InDependencies);
    EngineInstance->CreationMode = EAngelscriptEngineCreationMode::Clone;
    EngineInstance->bOwnsEngine = false;
    EngineInstance->SharedState = Source.SharedState;
    if (EngineInstance->SharedState.IsValid())
    {
        ++EngineInstance->SharedState->ActiveParticipants;
        ++EngineInstance->SharedState->ActiveCloneCount; // ★ clone 只增引用计数，不新建脚本引擎
    }
    EngineInstance->AdoptSharedStateFrom(Source);
    return EngineInstance;
}

void FAngelscriptEngine::InitializeOwnedSharedState()
{
    if (!SharedState.IsValid())
    {
        SharedState = MakeShared<FAngelscriptOwnedSharedState>();
    }

    SharedState->ScriptEngine = Engine;
    SharedState->PrimaryContext = GameThreadTLD != nullptr ? static_cast<asCContext*>(GameThreadTLD->primaryContext) : nullptr;
    SharedState->PrecompiledData = PrecompiledData;
    SharedState->StaticJIT = StaticJIT;
    SharedState->ActiveParticipants = FMath::Max(SharedState->ActiveParticipants, 1);
}

if (bHasDeferredCloneDependents)
{
    LocalSharedState->bPendingOwnerRelease = true; // ★ owner 先挂起释放，等所有 clone 退出再真正回收
}

if (!bOwnsEngine && LocalSharedState.IsValid() && LocalSharedState->bPendingOwnerRelease && LocalSharedState->ActiveParticipants == 0)
{
    ReleaseOwnedSharedStateResources(LocalSharedState); // ★ clone 是最后一个离开者时，负责触发延迟回收
}
```

设计取舍：

- `puerts` 把并发扩展点放在完整 VM 实例级别，适合按对象分片、按端口分调试、按 env 隔离状态。
- 代价是组模式里的 `RebindJs()`、`ReloadModule()`、`TryBindJs()` 都要广播到全部 env，冷启动和重绑成本随 env 数线性放大。
- `Angelscript` clone 路线把引擎、JIT、预编译和调试数据库都收敛成共享资源，更像“多个观察/执行视图”而不是多个独立 VM，资源效率高，但天然不提供 puerts 那种每对象路由到不同解释器的隔离面。

与 Angelscript 对比：

| 对比点 | puerts 新证据 | Angelscript 新证据 | 差距判断 |
| --- | --- | --- | --- |
| 多实例单位 | `FJsEnvGroup` 里是多个 `FJsEnvImpl` | `CreateCloneFrom()` 共享 `SharedState` | 实现方式不同 |
| 对象路由 | `Selector(UObject*, int)` 决定落到哪个 env | clone 不做对象级分流 | Angelscript 当前路径没有实现等价对象分片执行 |
| 调试端口策略 | `InDebugStartPort + i` 给每个 env 单独端口 | clone 共享同一 `DebugServer` 指针 | 实现方式不同 |
| 生命周期回收 | 组内 env 独立销毁 | owner / clone 通过 `ActiveCloneCount` 延迟释放 | 实现方式不同 |

### [维度 D4] 热重载：`puerts` 默认替换的是“已解析脚本文本”，`module cache` 失效需要额外动作；`Angelscript` 显式维护依赖闭包与失败队列

前文已经分析过 puerts 依赖 Inspector 做 HMR，这一轮补的是 **默认 HMR 实际改到了哪一层**。`SourceFileWatcher` 只要发现 `.js` 内容 MD5 变化就回调；`hot_reload.js` 进入 `Debugger.setScriptSource` 之前会发 `HMR.prepare`，结束后发 `HMR.finish`，但默认并不会调用 `puerts.forceReload(url)`，那一行被注释掉了。与此同时，`modular.js` 的 `require()` 只有在模块对象上被打了 `__forceReload` 时才会重新执行工厂函数，否则直接复用 `moduleCache[key].exports`。也就是说，puerts 默认 HMR 更接近 **脚本文本热补丁**，而不是 **模块图级别的强一致重建**。

`Angelscript` 则明显把失败恢复做成了主链的一部分。`PerformHotReload()` 会先把 `PreviouslyFailedReloadFiles` 合并回本轮文件集，再按 `moduleDependencies` 递推依赖闭包；随后根据 `ReloadRequirement` 决定 `SoftReload / FullReload / 保留旧代码`。如果本轮只能软重载但其实需要 full reload，或者直接编译失败，相关文件会被分别放进 `QueuedFullReloadFiles` 与 `PreviouslyFailedReloadFiles`，下次自动继续尝试。

```
[puerts] Text-Patch HMR
.js modified
 -> SourceFileWatcher (MD5 dedupe)
 -> hot_reload.js reload()
 -> Debugger.getScriptSource
 -> Debugger.setScriptSource
 -> HMR.prepare / HMR.finish
 -> moduleCache stays unless forceReload()

[Angelscript] Recovery-Oriented Reload
changed files + PreviouslyFailedReloadFiles
 -> dependency closure via moduleDependencies
 -> ReloadRequirement {Soft, FullSuggested, FullRequired}
 -> swap or keep old code
 -> queue QueuedFullReloadFiles / PreviouslyFailedReloadFiles
```

[1] `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/SourceFileWatcher.cpp:52-80` 与 `Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js:67-91`、`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:114-145,205-219`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/SourceFileWatcher.cpp
// 位置: 52-80，只要 .js 内容 hash 真变化就触发 reload 回调
// ============================================================================
if (Change.Action == FFileChangeData::FCA_Modified && Change.Filename.EndsWith(TEXT(".js")))
{
    FMD5Hash Hash = FMD5Hash::HashFile(*NotifyPath);
    if (WatchedFiles[Dir][FileName] != Hash)
    {
        OnWatchedFileChanged(NotifyPath); // ★ 观察层只负责把“文件变了”上报出去
        WatchedFiles[Dir][FileName] = Hash;
    }
}
```

```javascript
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js
// 位置: 67-91，默认 HMR 改的是已解析脚本文本
// ============================================================================
if (scriptId) {
    let orgSourceInfo = await sendCommand("Debugger.getScriptSource", {scriptId:"" + scriptId});
    source = ("(function (exports, require, module, __filename, __dirname) { " + source + "\n});");
    if (orgSourceInfo.scriptSource == source) {
        console.log(`source not changed, skip ${url}`);
        return;
    }
    let m = puerts.getModuleByUrl(url);
    puerts.emit('HMR.prepare', moduleName, m, url);
    let res = await sendCommand("Debugger.setScriptSource", {scriptId:"" + scriptId,scriptSource:source});
    puerts.emit('HMR.finish', moduleName, m, url);
    //puerts.forceReload(url); // ★ 默认没有失效 module cache
}
```

```javascript
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js
// 位置: 114-145, 205-219，只有打上 `__forceReload` 才会重跑模块工厂
// ============================================================================
if ((moduleName in localModuleCache)) {
    let m = localModuleCache[moduleName];
    if (!m.__forceReload) {
        return localModuleCache[moduleName].exports; // ★ 默认直接复用 cache
    } else {
        forceReload = true;
    }
}

if ((key in moduleCache) && !forceReload) {
    localModuleCache[moduleName] = moduleCache[key];
    return localModuleCache[moduleName].exports;
}

function forceReload(reloadModuleKey) {
    for(var moduleKey in moduleCache) {
        if (!reloadModuleKey || (reloadModuleKey == moduleKey)) {
            moduleCache[moduleKey].__forceReload = true; // ★ cache 失效是独立机制，不在默认 HMR 主链里
        }
    }
}
```

[2] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2270-2368,3938-3991,4168-4186`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 2270-2368, 3938-3991, 4168-4186，热重载把依赖扩展和失败恢复放进主链
// ============================================================================
for (auto& FailedFile : PreviouslyFailedReloadFiles)
{
    FileList.AddUnique(FailedFile); // ★ 上轮失败文件会直接并回本轮
}
PreviouslyFailedReloadFiles.Empty();

if (ShouldUseAutomaticImportMethod())
{
    TSet<asCModule*> MarkedModules;
    for (auto& File : FileList)
    {
        if (auto* ModulePtr = RelativeFileToModule.Find(File.RelativePath))
        {
            if ((*ModulePtr)->ScriptModule != nullptr)
                MarkedModules.Add((asCModule*)((*ModulePtr)->ScriptModule));
        }
    }

    while (bDidMarkModules)
    {
        for (auto& DependencyElem : ScriptModule->moduleDependencies)
        {
            if (MarkedModules.Contains(DependencyElem.Key))
            {
                bIsDependent = true; // ★ 依赖模块会被递推拉进同一轮 reload
            }
        }
    }
}

case FAngelscriptClassGenerator::EReloadRequirement::FullReloadRequired:
    if (CompileType == ECompileType::SoftReloadOnly)
    {
        UE_LOG(Angelscript, Error, TEXT("Full Reload is required due to UPROPERTY() or UFUNCTION() changes, but cannot perform a full reload right now. Keeping old angelscript code active."));
        bShouldSwapInModules = false;
        bFullReloadRequired = true; // ★ 无法 full reload 时，明确保留旧代码
    }
    break;

if (Result == ECompileResult::ErrorNeedFullReload)
{
    for (const auto& RepeatFile : AllCompiledFiles)
        QueuedFullReloadFiles.Add(RepeatFile); // ★ 下次有机会时继续 full reload
    PreviouslyFailedReloadFiles.Append(AllCompiledFiles);
}
else if (Result == ECompileResult::Error)
{
    PreviouslyFailedReloadFiles.Append(AllCompiledFiles); // ★ 普通失败也会自动重试
}
else if (Result == ECompileResult::PartiallyHandled)
{
    for (const auto& RepeatFile : AllCompiledFiles)
        QueuedFullReloadFiles.Add(RepeatFile); // ★ 软重载兜住了本轮，但仍排队等 full reload
}
```

设计取舍：

- `puerts` 的默认 HMR 路径更轻，优势是改动小、对正在运行的模块对象侵入低。
- 代价是模块工厂是否重执行、`exports` 是否重新计算，不由 `hot_reload.js` 默认保障；要做到图级一致性，需要业务再配 `forceReload()` 或自定义 `HMR.prepare/finish`。
- `Angelscript` 负担更重，但把“依赖扩展、失败重试、full reload 延后补偿”都做成了框架职责，出错时状态边界更明确。

与 Angelscript 对比：

| 对比点 | puerts 新证据 | Angelscript 新证据 | 差距判断 |
| --- | --- | --- | --- |
| 默认 HMR 作用层 | `Debugger.setScriptSource()` 改已解析文本 | `PerformHotReload()` 处理文件集合与模块闭包 | 实现方式不同 |
| 模块 cache 失效 | `forceReload()` 是独立机制，默认未调用 | reload 主链自带失败队列和 full reload 排队 | Angelscript 在失败恢复链上实现更完整 |
| 依赖扩展 | 默认 HMR 不显式重建模块依赖闭包 | 通过 `moduleDependencies` 递推受影响模块 | Angelscript 当前实现质量更高 |
| 旧代码保留策略 | 主要交给业务侧 HMR hook 决定 | 无法 full reload 时显式“Keeping old angelscript code active” | 实现方式不同 |

### [维度 D11] 部署与打包：`puerts` 的发布契约是“脚本目录语义 + backend native libs”，`Angelscript` 的发布契约是“脚本根顺序 + build-bound cache”

本轮 D11 不再重复前文关于 `.mbc/.cbc` 与 `PrecompiledScript.Cache` 的大框架，而是补 **运行时到底如何找到脚本和依赖**。`puerts` 在设置层把 `RootPath` 默认定为 `JavaScript`，`DefaultJSModuleLoader` 会从 `ProjectContentDir()/ScriptRoot` 查找模块，并额外支持沿目录向上找 `node_modules`、`package.json`、`.mjs/.cjs/.json/.mbc/.cbc`；`modular.js` 还会读取 `package.json.main` 与 `exports["."]`。与此同时，`JsEnv.Build.cs` 把 `libnode.dll`、`v8qjs.dll`、`msys-quickjs.dll` 等 runtime backend 二进制显式加进 `RuntimeDependencies` 并标成 `NonUFS`。这说明 puerts 的部署单元不是“只带脚本”，而是 **脚本包目录语义 + 对应 VM 动态库**。

`Angelscript` 这边的契约更接近 UE 原生脚本源树。默认项目根是 `Project/Script`，启用的插件如果存在 `Script/` 目录也会被纳入 `AllRootPaths`，但排序时项目根固定插在第一个。进入预编译路径后，engine 会优先尝试 `PrecompiledScript_<Config>.Cache`，找不到再回退 `PrecompiledScript.Cache`；一旦真用了 fully precompiled data，就会明确打印“Hot reloading is disabled for this run.”。所以 Angelscript 的发布物核心不是 npm 风格包解析，而是 **脚本根搜索顺序 + 与当前 build 严格匹配的 cache**。

```
[puerts] Shipping Contract
DefaultPuerts.ini RootPath
 -> DefaultJSModuleLoader(ProjectContentDir/RootPath)
 -> node_modules / package.json / .mjs / .mbc
 -> JsEnv.Build.cs stages backend dll/so/dylib as NonUFS
 -> runtime package = scripts + VM binaries

[Angelscript] Shipping Contract
Project/Script first
 + EnabledPlugin/Script roots (sorted)
 -> compile/load .as
 -> optional PrecompiledScript_{Config}.Cache
 -> fully precompiled run disables hot reload
 -> runtime package = roots + cache compatibility
```

[1] `Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsSetting.h:20-23`、`Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSModuleLoader.h:31-50`、`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DefaultJSModuleLoader.cpp:67-120`、`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js:148-180`、`Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs:360-367,502-523,624-667`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsSetting.h
// 位置: 20-23，脚本根本身就是部署契约的一部分
// ============================================================================
UPROPERTY(config, EditAnywhere, Category = "Default JavaScript Environment",
    meta = (defaultValue = "JavaScript", Tooltip = "JavaScript Source Code Root Path", DisplayName = "JavaScript Source Root"))
FString RootPath = "JavaScript";
```

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DefaultJSModuleLoader.cpp
// 位置: 67-120，加载器内建 node 风格目录搜索
// ============================================================================
return SearchModuleWithExtInDir(Dir, RequiredModule + ".js", Path, AbsolutePath) ||
       SearchModuleWithExtInDir(Dir, RequiredModule + ".mjs", Path, AbsolutePath) ||
       SearchModuleWithExtInDir(Dir, RequiredModule + ".cjs", Path, AbsolutePath) ||
#if defined(WITH_V8_BYTECODE)
       SearchModuleWithExtInDir(Dir, RequiredModule + ".mbc", Path, AbsolutePath) ||
       SearchModuleWithExtInDir(Dir, RequiredModule + ".cbc", Path, AbsolutePath) ||
#endif
       SearchModuleWithExtInDir(Dir, RequiredModule / "package.json", Path, AbsolutePath) ||
       SearchModuleWithExtInDir(Dir, RequiredModule / "index.js", Path, AbsolutePath);

return CheckExists(Dir / RequiredModule, Path, AbsolutePath) ||
       (!Dir.EndsWith(TEXT("node_modules")) && CheckExists(Dir / TEXT("node_modules") / RequiredModule, Path, AbsolutePath));

return SearchModuleInDir(FPaths::ProjectContentDir() / ScriptRoot, RequiredModule, Path, AbsolutePath) ||
       (ScriptRoot != TEXT("JavaScript") &&
           SearchModuleInDir(FPaths::ProjectContentDir() / TEXT("JavaScript"), RequiredModule, Path, AbsolutePath));
// ★ 发布后的目录布局需要保持这些 node/package 语义，加载器并不是简单拼一个文件名
```

```javascript
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js
// 位置: 148-180，package.json / exports 也是运行时解析的一部分
// ============================================================================
let isESM = outerIsESM === true || fullPath.endsWith(".mjs") || fullPath.endsWith(".mbc");
if (fullPath.endsWith(".cjs") || fullPath.endsWith(".cbc")) isESM = false;

if (fullPath.endsWith(".json")) {
    let packageConfigure = JSON.parse(script);

    if (fullPath.endsWith("package.json")) {
        isESM = packageConfigure.type === "module"
        let url = packageConfigure.main || "index.js";
        if (isESM) {
            let packageExports = packageConfigure.exports && packageConfigure.exports["."];
            if (packageExports)
                url =
                    (packageExports["default"] && packageExports["default"]["require"]) ||
                    (packageExports["require"] && packageExports["require"]["default"]) ||
                    packageExports["require"];
        }
        let tmpRequire = genRequire(fullDirInJs, isESM);
        let r = tmpRequire(url);
        m.exports = r; // ★ `package.json` 不是只给编辑器看，而是运行时入口解析规则
    }
}
```

```csharp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs
// 位置: 360-367, 502-523, 624-667，后端动态库被显式 staging 到发布物
// ============================================================================
void AddRuntimeDependencies(string[] DllNames, string LibraryPath, bool Delay)
{
    foreach (var DllName in DllNames)
    {
        var DllPath = Path.Combine(LibraryPath, DllName);
        var DestDllPath = Path.Combine("$(BinaryOutputDir)", DllName);
        RuntimeDependencies.Add(DestDllPath, DllPath, StagedFileType.NonUFS); // ★ 运行时二进制不是附带品，而是正式发布物
    }
}

PrivateDefinitions.Add("WITH_NODEJS");
RuntimeDependencies.Add("$(TargetOutputDir)/libnode.dll", Path.Combine(V8LibraryPath, "libnode.dll"));
AddRuntimeDependencies(new string[] { "libnode.dll" }, V8LibraryPath, false);

PrivateDefinitions.Add("WITH_QUICKJS");
AddRuntimeDependencies(new string[] { "v8qjs.dll" }, V8LibraryPath, false);
AddRuntimeDependencies(new string[] { "msys-quickjs.dll", "libgcc_s_seh-1.dll", "libwinpthread-1.dll" }, V8LibraryPath, true);
```

[2] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:558-566,1326-1363,1431,1512-1537,1583-1587,2054-2056`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 558-566, 1326-1363, 1431, 1512-1537, 1583-1587, 2054-2056，脚本根顺序与 cache 是部署核心
// ============================================================================
Dependencies.GetEnabledPluginScriptRoots = []()
{
    TArray<FString> ScriptRoots;
    for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetEnabledPluginsWithContent())
    {
        ScriptRoots.Add(Plugin->GetBaseDir() / TEXT("Script")); // ★ 插件也能带 Script 根，但不是 node 风格包解析
    }
    return ScriptRoots;
};

FString RootPath = Dependencies.ConvertRelativePathToFull(Dependencies.GetProjectDir() / TEXT("Script"));
for (const FString& PluginScriptRoot : Dependencies.GetEnabledPluginScriptRoots())
{
    const FString ScriptPath = Dependencies.ConvertRelativePathToFull(PluginScriptRoot);
    if (Dependencies.DirectoryExists(ScriptPath) && ScriptPath != RootPath)
    {
        DiscoveredRootPaths.Add(ScriptPath);
    }
}
DiscoveredRootPaths.Sort();
DiscoveredRootPaths.Insert(RootPath, 0); // ★ 项目根固定优先

AllRootPaths = DiscoverScriptRoots(/*bOnlyProjectRoot =*/ true);

if (bUsePrecompiledData)
{
#if UE_BUILD_DEVELOPMENT
    Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript_Development.Cache");
#endif
    if (!IFileManager::Get().FileExists(*Filename))
        Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript.Cache");
    if (IFileManager::Get().FileExists(*Filename))
    {
        PrecompiledData->Load(Filename); // ★ cache 名称和 build 配置强绑定
    }
}

if (bGeneratePrecompiledData)
{
    FString Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript.Cache");
    PrecompiledData->InitFromActiveScript();
    PrecompiledData->Save(Filename); // ★ 生成产物直接落在 script root 下
}

UE_LOG(Angelscript, Warning, TEXT("Using fully precompiled scripts. Hot reloading is disabled for this run."));
UE_LOG(Angelscript, Warning, TEXT("Delete PrecompiledScript.Cache or run with -as-development-mode flag to enable hot reload."));
```

设计取舍：

- `puerts` 发布物更像“可执行脚本包”，目录结构、`package.json`、字节码扩展名和 backend 动态库都属于契约。
- 这让它天然适合引入 npm 风格布局、多 backend 选择和脚本包替换，但部署检查面更宽，漏一个动态库或目录语义就会直接失效。
- `Angelscript` 更像“受 UE 工程和 build 配置约束的脚本源树”，好处是根顺序明确、cache 生命周期更可控，代价是发布物灵活性不如 puerts 的 package 风格生态。

与 Angelscript 对比：

| 对比点 | puerts 新证据 | Angelscript 新证据 | 差距判断 |
| --- | --- | --- | --- |
| 脚本根语义 | `RootPath` + `ProjectContentDir()/ScriptRoot` + `node_modules` 向上搜索 | `Project/Script` 优先，再拼启用插件的 `Script/` 根 | 实现方式不同 |
| 运行时入口解析 | `package.json.main/exports`、`.mjs/.cjs/.mbc/.cbc` 都是正式入口 | `.as` 根目录扫描 + cache 回退 | 实现方式不同 |
| Native backend staging | `RuntimeDependencies.Add(..., StagedFileType.NonUFS)` 显式带上 Node/QuickJS 二进制 | 当前主链核心发布物是 `PrecompiledScript*.Cache` 与 bind/cache 文件 | 实现方式不同 |
| Hot reload 与发布关系 | 后端二进制和目录语义齐全即可继续热替换脚本 | fully precompiled run 明确关闭 hot reload | Angelscript 在缓存确定性上更强，但灵活脚本分发面较窄 |

---

## 深化分析 (2026-04-09 00:16:43)

### [维度 D6] 代码生成与 IDE 支持：`puerts` 的符号身份围绕“资产路径 -> TS namespace”，`Angelscript` 的生成身份围绕“module/class/function -> erase macro”

前面的 D6 已经覆盖 `.d.ts`、`LanguageService` 与 Blueprint 回写；这一轮补的是 **这些产物到底把谁当成“稳定符号主键”**。`puerts` 在 `DeclarationGenerator` 里不是简单拿 `UClass::GetName()` 当声明名，而是先把 `UPackage` 路径拆成 path fragments，经 `FilenameToTypeScriptVariableName()` 清洗后拼成 `namespace`，再把非 native 资产声明缓存到 `BlueprintTypeDeclInfoCache.NameToDecl`。这意味着它的 IDE 表面与 Blueprint 资产路径天然同构，重点是“给作者一个可持续书写的类型坐标系”。

`Angelscript` 的代码生成则明显服务于另一件事。`AngelscriptFunctionTableCodeGenerator` 会按 `module/class/function` 排序并切 shard，核心产物是 `AS_FunctionTable_<module>_<index>.cpp`；单个函数入口再由 `AngelscriptFunctionSignature.BuildEraseMacro()` 还原成 `ERASE_AUTO_*` 或显式 `ERASE_*` 宏。它追求的是 **绑定恢复时的稳定函数指针身份**。IDE 侧当前更依赖 `FAngelscriptSourceCodeNavigation` 直接跳到 `.as` 文件行号，而不是维护一套与运行时并行的离线声明命名空间。

```
[puerts] IDE Symbol Identity
UPackage path (/Game/UI/HUD)
 -> FilenameToTypeScriptVariableName()
 -> UE.Game.UI.HUD namespace
 -> NameToDecl cache / ue_bp.d.ts
 -> ts.LanguageService symbol

[Angelscript] IDE Symbol Identity
UHT function
 -> HeaderSignatureResolver
 -> BuildEraseMacro()
 -> AS_FunctionTable_<module>_<shard>.cpp
 -> runtime bind identity
 -> SourceCodeNavigation opens .as source
```

[1] `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:469-557`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp
// 函数: FTypeScriptDeclarationGenerator::GetNamespace / GetNameWithNamespace / WriteOutput
// 位置: 469-557，声明身份直接绑定到 package path 与 Blueprint 缓存
// ============================================================================
const FString& FTypeScriptDeclarationGenerator::GetNamespace(UObject* Obj)
{
    UPackage* Pkg = GetPackage(Obj);
    Pkg->GetName().ParseIntoArray(PathFrags, TEXT("/"));
    for (int i = 0; i < PathFrags.Num(); i++)
    {
        PathFrags[i] = PUERTS_NAMESPACE::FilenameToTypeScriptVariableName(PathFrags[i]);
    }
    NamespaceMap[Obj] = FString::Join(PathFrags, TEXT(".")); // ★ 用 package path 构造 TS namespace
}

FString FTypeScriptDeclarationGenerator::GetNameWithNamespace(UObject* Obj)
{
    if (!Obj->IsNative())
    {
        return (RefFromOuter ? TEXT("") : TEXT("UE.")) + GetNamespace(Obj) + TEXT(".") +
               PUERTS_NAMESPACE::FilenameToTypeScriptVariableName(Obj->GetName());
        // ★ 非 native 资产不走平铺类名，而走 `UE.<namespace>.<Type>`
    }
    return (RefFromOuter ? TEXT("") : TEXT("UE.")) + SafeName(Obj->GetName());
}

void FTypeScriptDeclarationGenerator::WriteOutput(UObject* Obj, const FStringBuffer& Buff)
{
    const UPackage* Pkg = GetPackage(Obj);
    if (Pkg && !Obj->IsNative())
    {
        FStringBuffer Temp;
        NamespaceBegin(Obj, Temp);
        Temp << Buff;
        NamespaceEnd(Obj, Temp);

        BlueprintTypeDeclInfo->NameToDecl.Add(Obj->GetFName(), Temp.Buffer);
        // ★ 同一个 namespace 文本既是写盘产物，也是 Blueprint 声明缓存主键
    }
    else
    {
        NamespaceBegin(Obj, Output);
        Output << Buff;
        NamespaceEnd(Obj, Output);
    }
}
```

[2] `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs:8-38` 与 `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:81-139`

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs
// 函数: AngelscriptFunctionSignature.BuildEraseMacro
// 位置: 8-38，把函数身份收敛成可恢复的 erase macro
// ============================================================================
internal sealed record AngelscriptFunctionSignature(
    string OwningType,
    string FunctionName,
    string ReturnType,
    IReadOnlyList<string> ParameterTypes,
    bool IsStatic,
    bool IsConst,
    bool UseExplicitSignature)
{
    public string BuildEraseMacro()
    {
        if (UseExplicitSignature)
        {
            return IsStatic
                ? $"ERASE_FUNCTION_PTR({OwningType}::{FunctionName}, {parameterPack}, ERASE_ARGUMENT_PACK({ReturnType}))"
                : $"ERASE_METHOD_PTR({OwningType}, {FunctionName}, {parameterPack}, ERASE_ARGUMENT_PACK({ReturnType}))";
            // ★ 显式签名模式关心的是“如何准确拿回 C++ 指针”，不是导出 IDE 友好名字
        }

        return IsStatic
            ? $"ERASE_AUTO_FUNCTION_PTR({OwningType}::{FunctionName})"
            : $"ERASE_AUTO_METHOD_PTR({OwningType}, {FunctionName})";
    }
}

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs
// 函数: GenerateModule
// 位置: 81-139，生成物按 module/class/function 排序并分 shard
// ============================================================================
CollectEntries(factory, module.ScriptPackage, includes, entries);

entries.Sort(static (left, right) =>
{
    int classComparison = StringComparer.Ordinal.Compare(left.ClassName, right.ClassName);
    return classComparison != 0
        ? classComparison
        : StringComparer.Ordinal.Compare(left.FunctionName, right.FunctionName);
});

string outputPath = factory.MakePath($"AS_FunctionTable_{module.ShortName}_{shardIndex:D3}", ".cpp");
factory.CommitOutput(outputPath, BuildShard(module.ShortName, editorOnly, includes, entries, startIndex, entryCount, shardIndex, shardCount));
// ★ 产物的稳定坐标是 module + shard，不是给 IDE 暴露一层 package namespace
```

[3] `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp:26-45,118-138`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp
// 函数: FAngelscriptSourceCodeNavigation::NavigateToFunction / GetClassDesc
// 位置: 26-45, 118-138，IDE 支撑重点是源码导航，不是离线类型命名空间
// ============================================================================
virtual bool NavigateToFunction(const UFunction* InFunction) override
{
    auto* ASFunc = Cast<const UASFunction>(InFunction);
    if (ASFunc == nullptr)
        return false;

    FString Path = ASFunc->GetSourceFilePath();
    if (Path.Len() == 0)
        return false;

    OpenFile(Path, ASFunc->GetSourceLineNumber()); // ★ 直接跳到脚本源文件
    return true;
}

void RegisterAngelscriptSourceNavigation()
{
    FSourceCodeNavigation::AddNavigationHandler(new FAngelscriptSourceCodeNavigation);
}
```

设计取舍：

- `puerts` 用 package path 做 namespace，使 Blueprint/资产类型天然带上项目层级语义，利于 `TypeScript` 智能提示、重构和多资产共存。
- 代价是声明层需要维护额外的名字清洗、namespace 缓存和 Blueprint 文本缓存，IDE 表面与 runtime bind table 不是同一层数据结构。
- `Angelscript` 选择让生成链直接服务绑定恢复和函数指针稳定性，IDE 体验更多依赖源码导航与运行时元数据，而不是再维护一套 authoring-first 离线声明空间。

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 符号主键 | `UPackage path -> TS namespace -> NameToDecl` | `module/class/function/signature -> erase macro/shard` | 实现方式不同 |
| 离线 IDE 命名空间 | `ue.d.ts / ue_bp.d.ts` 中显式保留资产 namespace | 当前检视路径未见等价离线 namespace 声明产物 | Angelscript 当前没有实现同等级离线声明命名空间层 |
| IDE 主回路 | `TypeScript` 语义符号本身就是主要 IDE surface | `SourceCodeNavigation` 直接打开 `.as` 源码 | 实现方式不同 |

### [维度 D8] 性能与优化：`puerts` 的快路径是 backend-conditional `FastCall`，`Angelscript` 的快路径是带校验与调试支架的 `StaticJIT`

前面的 D8 已经讲过 `FastCall`、`StaticJIT` 和调用边界；这一轮补的是 **快路径什么时候成立，以及快路径失效时系统怎样收口**。`puerts` 的 `CFunctionInfoImpl` 默认 `FastCallInfo()` 直接返回 `nullptr`，只有在 `WITH_V8_FAST_CALL` 的模板特化里才会返回 `V8FastCall<...>::info()`；`CppObjectMapper` 又只有在 `!WITH_QUICKJS` 时才会把这个 `FastCallInfo` 塞进 `v8::FunctionTemplate::New(...)`。`JsEnvModule.cpp` 也只有在 `WITH_V8_FAST_CALL` 下才给 V8 加 `--turbo-fast-api-calls`。也就是说，同一套绑定描述在不同 backend / build flag 下，热路径形态并不相同。

`Angelscript` 的 `StaticJIT` 则不是“开了就冲”。生成预编译数据时会同时生成 `PrecompiledData` 与 `StaticJIT` 输出；运行时如果读到了 `PrecompiledData`，还会把 `FStaticJITCompiledInfo::PrecompiledDataGuid` 与 cache 的 `DataGuid` 对比，不匹配就直接 `FJITDatabase::Get().Clear()` 禁用 transpiled code。更关键的是，`StaticJITHeader` 在非 shipping 下保留 `FScopeJITDebugCallstack`，并可记录 property offset / type size 验证数据；异常也统一回流到 `FAngelscriptEngine::HandleExceptionFromJIT(...)`。这说明它把快路径当成“经过验证后才能启用的执行层”，不是单纯的 API 加速开关。

```
[puerts] Fast Path Gate
binding metadata
 -> WITH_V8_FAST_CALL ? FastCallInfo() : nullptr
 -> !WITH_QUICKJS ? FunctionTemplate(..., FastCallInfo) : generic callback
 -> optional --turbo-fast-api-calls
 -> backend-specific hot path

[Angelscript] StaticJIT Gate
bGeneratePrecompiledData
 -> StaticJIT + PrecompiledData
 -> load PrecompiledScript cache
 -> compare CompiledInfo.PrecompiledDataGuid with DataGuid
 -> mismatch => FJITDatabase::Clear()
 -> match => use transpiled code with debug/verify scaffolding
```

[1] `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/TypeInfo.hpp:408-438,457-483`、`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/CppObjectMapper.cpp:208-246`、`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvModule.cpp:208-217`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Public/TypeInfo.hpp
// 函数: CFunctionInfoImpl::FastCallInfo / CFunctionInfoByPtrImpl::FastCallInfo
// 位置: 408-438, 457-483，FastCall 默认不存在，只有特定编译开关才生成
// ============================================================================
virtual const class v8::CFunction* FastCallInfo() const override
{
    return nullptr; // ★ 默认路径没有 FastCall
};

#ifdef WITH_V8_FAST_CALL
virtual const class v8::CFunction* FastCallInfo() const override
{
    return V8FastCall<Ret (*)(Args...), func>::info(); // ★ 只有 V8 fast api 开关打开才有快路径
};
#endif

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/CppObjectMapper.cpp
// 位置: 208-246，mapper 还要再看 backend 是否允许塞 FastCallInfo
// ============================================================================
#ifndef WITH_QUICKJS
auto FastCallInfo = FunctionInfo->ReflectionInfo ? FunctionInfo->ReflectionInfo->FastCallInfo() : nullptr;
if (FastCallInfo)
{
    Template->PrototypeTemplate()->Set(
        ...,
        v8::FunctionTemplate::New(Isolate, ..., v8::SideEffectType::kHasSideEffect, FastCallInfo));
        // ★ V8 路径走 FastCall
}
else
#endif
{
    Template->PrototypeTemplate()->Set(
        ...,
        v8::FunctionTemplate::New(Isolate, ...)); // ★ QuickJS / 无 FastCall 时退回通用 callback
}

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvModule.cpp
// 位置: 208-217，V8 全局 flag 也只在特定条件下开启
// ============================================================================
#ifdef WITH_V8_FAST_CALL
    v8::V8::SetFlagsFromString("--turbo-fast-api-calls");
#endif

#if defined(WITH_V8_BYTECODE)
    v8::V8::SetFlagsFromString("--no-lazy --no-flush-bytecode --no-enable-lazy-source-positions");
#endif
```

[2] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1433-1556,1573-1602`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h:74-99,146-214`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp:89-119`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 1433-1556, 1573-1602，StaticJIT 先生成，再按 GUID 验证是否可用
// ============================================================================
if (bGeneratePrecompiledData)
{
    PrecompiledData = new FAngelscriptPrecompiledData(Engine);
    StaticJIT = new FAngelscriptStaticJIT();
    StaticJIT->PrecompiledData = PrecompiledData;
    Engine->SetJITCompiler(StaticJIT); // ★ 生成阶段把 JIT 编译器接进 AngelScript 引擎
}

if (IFileManager::Get().FileExists(*Filename))
{
    PrecompiledData = new FAngelscriptPrecompiledData(Engine);
    PrecompiledData->Load(Filename);

    const FStaticJITCompiledInfo* CompiledInfo = FStaticJITCompiledInfo::Get();
    if (CompiledInfo != nullptr && CompiledInfo->PrecompiledDataGuid != PrecompiledData->DataGuid)
    {
        UE_LOG(Angelscript, Warning, TEXT("Loaded angelscript precompiled data does not match the transpiled C++ in the game binary. Transpiled code will not be used!"));
        FJITDatabase::Get().Clear(); // ★ 一旦 GUID 不匹配，直接放弃 transpiled code
    }
}

if (PrecompiledData != nullptr)
{
    bStaticJITTranspiledCodeLoaded = FJITDatabase::Get().Functions.Num() > 0;
    if (!bScriptDevelopmentMode && !bGeneratePrecompiledData)
        PrecompiledData->ClearUnneededRuntimeData(); // ★ 用完就裁掉多余运行时数据
    FJITDatabase::Clear();
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h
// 位置: 74-99, 146-214，JIT 产物自带验证与调试帧支架
// ============================================================================
struct FStaticJITCompiledInfo
{
    FGuid PrecompiledDataGuid; // ★ 用来和 cache 做一致性校验
    FStaticJITCompiledInfo(FGuid Guid);
    static const FStaticJITCompiledInfo* Get();
};

struct FJitVerifyPropertyOffset
{
#if AS_JIT_VERIFY_PROPERTY_OFFSETS
    FJitVerifyPropertyOffset(uint64 PropertyRef, SIZE_T ComputedOffset);
#endif
};

#if AS_JIT_DEBUG_CALLSTACKS
struct FScopeJITDebugCallstack
{
    FScopeJITDebugCallstack(FScriptExecution& InExecution, const char* InFilename, const char* InFunctionName, int32 InLineNumber, void* InThisObject)
    {
        PrevFrame = (FScopeJITDebugCallstack*)Execution.debugCallStack;
        Execution.debugCallStack = this; // ★ 非 shipping 下仍保留 JIT 帧，便于调试定位
    }
};
#endif

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp
// 位置: 89-119，JIT 异常统一回流到引擎异常处理
// ============================================================================
FJitVerifyPropertyOffset::FJitVerifyPropertyOffset(uint64 PropertyRef, SIZE_T ComputedOffset)
{
    FJITDatabase::Get().VerifyPropertyOffsets.Add(TPair<uint64,SIZE_T>(PropertyRef, ComputedOffset));
}

void FStaticJITFunction::SetException(FScriptExecution& Execution, EJITException Exception)
{
    Execution.bExceptionThrown = true;
    if (Exception == EJITException::NullPointer)
        FAngelscriptEngine::HandleExceptionFromJIT(TXT_NULL_POINTER_ACCESS);
    else if (Exception == EJITException::Div0)
        FAngelscriptEngine::HandleExceptionFromJIT(TXT_DIVIDE_BY_ZERO);
    else
        FAngelscriptEngine::HandleExceptionFromJIT("Unknown exception.");
    // ★ JIT 路径不是“出错即黑盒”，而是显式接回统一异常通道
}
```

设计取舍：

- `puerts` 的优势是快路径嵌入成本低，绑定描述一份即可在支持的 V8 路径上自动吃到 `FastCall`，不支持时自然回退。
- 代价是性能语义受 backend 和编译开关影响较大，同一 API 在 `V8 / QuickJS / Node.js` 下并不保证同样的热路径形态。
- `Angelscript` 的 `StaticJIT` 更重，但它把 GUID 校验、异常桥接、调试帧和可选的 offset/type 校验都绑在快路径上，强调的是“可验证的快”，不是“尽量快”。

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 快路径启用条件 | `WITH_V8_FAST_CALL` + `!WITH_QUICKJS` + 函数级 `FastCallInfo()` | 预编译/JIT 产物与 `PrecompiledDataGuid` 一致后才接受 transpiled code | 实现方式不同 |
| 快路径失效处理 | 退回普通 `FunctionTemplate` callback | `FJITDatabase::Clear()`，显式禁用不匹配的 JIT 产物 | Angelscript 当前实现更完整 |
| 调试/验证支架 | 当前检视路径未见与 `FastCall` 并列的专用调试帧设施 | `FScopeJITDebugCallstack` + offset/type 校验 + 统一异常回流 | Angelscript 当前实现质量更高 |

### [维度 D7] 编辑器集成：`puerts` 把安全边界放在“拒绝 PIE 内 schema 写回”，`Angelscript` 把安全边界放在“reload 后替换打开中的资产引用”

前面的 D7 / D4 已经讲过 editor-side `JsEnv` 和 `ClassReloadHelper`；这一轮补的是 **编辑器 live session 中谁来兜住一致性风险**。`puerts` 在 `FPuertsEditorModule::OnPostEngineInit()` 里启动常驻 `FJsEnv` 和 `FSourceFileWatcher`，允许 `CodeAnalyze` 持续跑；但真正会修改 Blueprint schema 的 `UPEBlueprintAsset` 在 `IsPlaying()` 时直接拒绝变更，`LoadOrCreate()` 和类布局更新都明确报错 `change the layout ... in PIE mode is forbiden!`。也就是说，分析可以常驻，但写回边界收得很死。

`Angelscript` 则走另一条路。`FClassReloadHelper::FReloadState::PerformReinstance()` 在 reload 后显式进入对象图修复流程；`UAngelscriptReferenceReplacementHelper::Serialize()` 会枚举所有正在编辑的资产，把旧对象引用通过归档替换成新对象，再调用 `NotifyAssetClosed/NotifyAssetOpened` 让 asset editor subsystem 接受替换结果。这里的重点不是“禁止用户在某个时刻修改”，而是 **允许类型被替换后，尽量把编辑器状态修复回来**。

```
[puerts] Editor Mutation Safety
SourceFileWatcher
 -> editor-side JsEnv / CodeAnalyze
 -> UPEBlueprintAsset mutation API
 -> if PIE => reject layout/create change

[Angelscript] Editor Reload Safety
script/class reload
 -> PerformReinstance()
 -> ReplaceHelper + replacement maps
 -> patch open asset refs in AssetEditorSubsystem
 -> editors continue on replaced assets
```

[1] `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:76-151` 与 `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:57-73,87-123`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp
// 函数: FPuertsEditorModule::StartupModule / OnPostEngineInit
// 位置: 76-151，分析器常驻，但真正的写回由专门资产 API 承担
// ============================================================================
void FPuertsEditorModule::StartupModule()
{
    Enabled = IPuertsModule::Get().IsWatchEnabled() && !IsRunningCommandlet();
    FEditorDelegates::PreBeginPIE.AddRaw(this, &FPuertsEditorModule::PreBeginPIE);
    FEditorDelegates::EndPIE.AddRaw(this, &FPuertsEditorModule::EndPIE);
    this->OnPostEngineInit();
}

void FPuertsEditorModule::OnPostEngineInit()
{
    SourceFileWatcher = MakeShared<PUERTS_NAMESPACE::FSourceFileWatcher>(...);
    JsEnv = MakeShared<PUERTS_NAMESPACE::FJsEnv>(..., TEXT("--max-old-space-size=2048"));
    JsEnv->Start("PuertsEditor/CodeAnalyze"); // ★ 分析器可以常驻跑
}

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp
// 函数: UPEBlueprintAsset::LoadOrCreate
// 位置: 57-73, 87-123，真正写 Blueprint schema 时，PIE 直接拒绝
// ============================================================================
#define CanChangeCheckWithBoolRet()                                                                        \
    if (IsPlaying())                                                                                       \
    {                                                                                                      \
        UE_LOG(PuertsEditorModule, Error, TEXT("change the layout of class[%s] in PIE mode is forbiden!"), \
            *GeneratedClass->GetName());                                                                   \
        NeedSave = false;                                                                                  \
        return false;                                                                                      \
    }

bool UPEBlueprintAsset::LoadOrCreate(const FString& InName, const FString& InPath, UClass* ParentClass, int32 InSetFlags, int32 InClearFlags)
{
    if (Blueprint->ParentClass != ParentClass)
    {
        CanChangeCheckWithBoolRet(); // ★ 运行中不允许改 schema
        Blueprint->ParentClass = ParentClass;
    }

    if (IsPlaying())
    {
        UE_LOG(PuertsEditorModule, Error, TEXT("create class[%s] in PIE mode is forbiden!"), *InName);
        return false; // ★ 创建 Blueprint 也同样拒绝
    }
}
```

[2] `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:27-48,409-438`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp
// 函数: FClassReloadHelper::FReloadState::PerformReinstance / UAngelscriptReferenceReplacementHelper::Serialize
// 位置: 27-48, 409-438，reload 后主动修补对象图与打开中的编辑器资产
// ============================================================================
void FClassReloadHelper::FReloadState::PerformReinstance()
{
    if (!FAngelscriptEngine::Get().bIsInitialCompileFinished)
        return;

    if (ReplaceHelper == nullptr)
    {
        ReplaceHelper = NewObject<UAngelscriptReferenceReplacementHelper>(GetTransientPackage());
        ReplaceHelper->AddToRoot(); // ★ 进入显式 replacement helper 路径，而不是简单拒绝
    }

    TArray<UBlueprint*> DependencyBPs;
    TArray<UK2Node*> AllNodes;
    // ★ 后续会基于 replacement map 修补 Blueprint pin / struct / delegate 引用
}

void UAngelscriptReferenceReplacementHelper::Serialize(FStructuredArchive::FRecord Record)
{
    if (UnderlyingArchive.IsObjectReferenceCollector())
    {
        UAssetEditorSubsystem* SubSystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
        TArray<UObject*> OpenAssets = SubSystem->GetAllEditedAssets();
        for (UObject* OriginalAsset : OpenAssets)
        {
            UObject* ReplacedAsset = OriginalAsset;
            UnderlyingArchive << ReplacedAsset;

            if (ReplacedAsset != OriginalAsset)
            {
                auto Editors = SubSystem->FindEditorsForAsset(OriginalAsset);
                for (auto* EditorInstance : Editors)
                {
                    SubSystem->NotifyAssetClosed(OriginalAsset, EditorInstance);
                    SubSystem->NotifyAssetOpened(ReplacedAsset, EditorInstance);
                    // ★ 不只是替换对象引用，还把打开中的 asset editor 状态一起切过去
                }
            }
        }
    }
}
```

设计取舍：

- `puerts` 的边界更保守，优点是编辑器运行中不会出现“分析器半改了一半 schema”的中间态。
- 代价是 live authoring 粒度受限，PIE 期间 Blueprint schema 侧的自动更新能力明显收缩。
- `Angelscript` 选择承受更高实现复杂度，把 reload 后的 open asset、pin、引用替换都纳入框架职责，换来更强的连续编辑能力。

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| PIE 内 schema 变更策略 | `UPEBlueprintAsset` 直接拒绝布局变化与创建 | reload 路径继续执行，并用 replacement helper 修补引用 | 实现方式不同 |
| 打开中的资产编辑器连续性 | 当前检视路径未见等价 `NotifyAssetClosed/Open` 替换链 | 显式遍历 `GetAllEditedAssets()` 并切换 editor 绑定对象 | Angelscript 当前实现更完整 |
| 安全边界位置 | 写回 API 层硬拒绝 | reload / reinstance 层主动修复 | 实现方式不同 |

---

## 深化分析 (2026-04-09 00:25:53)

### [维度 D5] 调试与开发体验：`puerts` 的调用栈更像 backend 条件成立时的即时字符串，`Angelscript` 的调用栈是持续维护的结构化帧模型

前面几轮已经说明两边一个偏 Inspector、一个偏自定义协议；这一轮继续往“断下来之后你到底拿到什么”去看，差异会更具体。`puerts` 在 UE 插件层暴露的调用栈主入口是 `FJsEnvImpl::CurrentStackTrace()`，它直接向 VM 取当前栈，再把结果格式化成一段 `FString`；`QuickJS` 路径则直接返回空串。Promise reject 的错误定位也是同一路线，仍然是把 `v8::StackFrame` 逐帧拼成文本。这说明 `puerts` 的调试来源主要是“当前 backend 还能不能即时给我一份栈文本”，而不是插件自己维护一份可重放、可序列化的帧状态。

`Angelscript` 这里相反。行回调每次命中脚本行时，都会把 `Function/Class/File/Line/This/Variables` 写回 `FAngelscriptDebugStack`；真正对外发协议时，`SendCallStack()` 再把脚本帧、Blueprint 帧、系统函数帧拼成 `FAngelscriptCallStack` 消息。连 JIT 路径也额外挂了 `FScopeJITDebugCallstack` 兜底。这意味着它的“调试体验”并不只来自前端协议设计，而是来自一套持续更新的运行时帧模型。`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp:239-247,423-427` 还会主动请求 `RequestCallStack` 并断言返回至少一帧，说明这条链路已经被当成稳定契约在测试。

```
[puerts] Stack Provenance
JS runtime state
 -> v8::StackTrace::CurrentStackTrace()
 -> StackTraceToString()
 -> FString / promise reject text
 -> frontend consumes text
QuickJS
 -> return ""

[Angelscript] Stack Provenance
script line callback
 -> GetStack(Context)
 -> FAngelscriptDebugStack.Frames[]
 -> DebugServer::SendCallStack()
 -> FAngelscriptCallStack payload
 -> automation tests validate payload
```

[1] `puerts` 当前的调用栈入口本质上是“把 VM 当前帧转成字符串”，并且 `QuickJS` 路径没有等价返回值。

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp
// 函数: FJsEnvImpl::CurrentStackTrace
// 位置: 1704-1717，直接向 backend 要当前栈，再格式化成 FString
// ============================================================================
FString FJsEnvImpl::CurrentStackTrace()
{
#ifndef WITH_QUICKJS
    v8::Isolate* Isolate = MainIsolate;
    v8::Isolate::Scope IsolateScope(Isolate);
    v8::HandleScope HandleScope(Isolate);

    PString StackTrace = StackTraceToString(
        Isolate,
        v8::StackTrace::CurrentStackTrace(Isolate, 10, v8::StackTrace::kDetailed));
    return UTF8_TO_TCHAR(StackTrace.c_str()); // ★ 返回的是文本快照，不是结构化帧对象
#else
    return TEXT(""); // ★ QuickJS 路径没有等价调用栈产物
#endif
}
```

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PromiseRejectCallback.hpp
// 函数: StackTraceToString
// 位置: 75-101，异常链路同样走逐帧字符串化
// ============================================================================
PString StackTraceToString(v8::Isolate* InIsolate, v8::Local<v8::StackTrace> InStack)
{
    std::ostringstream stm;
    for (int i = 0; i < InStack->GetFrameCount(); i++)
    {
        v8::Local<v8::StackFrame> StackFrame = InStack->GetFrame(InIsolate, i);
        const int LineNumber = StackFrame->GetLineNumber();
        const int Column = StackFrame->GetColumn();

        if (StackFrame->IsEval())
        {
            if (StackFrame->GetScriptId() == v8::Message::kNoScriptIdInfo)
            {
                stm << "    at [eval]:" << LineNumber << ":" << Column << std::endl;
            }
            else
            {
                stm << "    at [eval] (" << (*ScriptName ? *ScriptName : "anonymous") << ":" << LineNumber << ":" << Column << ")"
                    << std::endl;
            }
            break; // ★ 这里的职责仍然是“拼文本”，不是保留可查询的 frame graph
        }
    }
}
```

[2] `Angelscript` 在运行时主动维护调试帧，并把它们序列化成协议消息；Blueprint 帧也被并入同一条调用栈。

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 5478-5536，行回调持续刷新结构化调试栈
// ============================================================================
auto& Stack = GetStack(Context);
GAngelscriptStack = &Stack;

int32 StackSize = Context->GetCallstackSize();
if (StackSize != Stack.Frames.Num()
    || (StackSize != 0 && Stack.Frames[0].ScriptFunction != Context->GetFunction(0)))
{
    Stack.Frames.SetNum(StackSize, false);

    for (int32 i = 0; i < StackSize; ++i)
    {
        auto& Frame = Stack.Frames[i];
        Frame.ScriptFunction = Context->GetFunction(i);
        Frame.LineNumber = Context->GetLineNumber(i, nullptr, &Frame.File);
        Frame.Function = Frame.ScriptFunction ? Frame.ScriptFunction->GetName() : nullptr;
        Frame.Class = Frame.ScriptFunction && Frame.ScriptFunction->GetObjectType()
            ? Frame.ScriptFunction->GetObjectType()->GetName()
            : nullptr;
        Frame.This = (UObject*)Context->GetThisPointer(i); // ★ this 指针也进入调试帧
    }
}

if (auto* DebugServer = AngelscriptManager.DebugServer)
    DebugServer->ProcessScriptLine(Context); // ★ 协议层读取的是这份持续维护的帧状态
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 函数: FAngelscriptDebugServer::SendCallStack
// 位置: 1382-1482，把脚本帧、Blueprint 帧、系统函数帧拼成结构化消息
// ============================================================================
void FAngelscriptDebugServer::SendCallStack(FSocket* Client)
{
    FAngelscriptCallStack Stack;

    auto* Context = (asCContext*)asGetActiveContext();
    if (Context != nullptr)
    {
        int32 StackSize = Context->GetCallstackSize();

        for (int32 i = StackSize-1; i >= 0; --i)
        {
            auto* ScriptFunction = (asCScriptFunction*)Context->GetFunction(i);
            FAngelscriptCallFrame Frame;

#if DO_BLUEPRINT_GUARD
            // ★ 先把 Blueprint 栈拼进同一条调用链
            int BPFrame = Context->GetBlueprintCallstackFrame(i);
            for (; BPStackIndex < BPFrame; ++BPStackIndex)
            {
                if (BPStack == nullptr)
                    continue;

                auto StackView = BPStack->GetCurrentScriptStack();
                if (!StackView.IsValidIndex(BPStackIndex))
                    continue;

                UFunction* Function = StackView[BPStackIndex]->Node;
                if (Function == nullptr || IsAngelscriptGenerated(Function))
                    continue;
            }
#endif

            if (ScriptFunction != nullptr)
            {
                if (ScriptFunction->GetFuncType() == asEFuncType::asFUNC_SYSTEM)
                {
                    Frame.Name = FString::Printf(TEXT("(C++) %s"), *FunctionName);
                    Frame.Source = FString::Printf(TEXT("::%s"), ANSI_TO_TCHAR(ScriptFunction->GetNamespace()));
                    Stack.Frames.Insert(Frame, 0);
                }
                else
                {
                    Frame.Name = ANSI_TO_TCHAR(ScriptFunction->GetName());
                    Frame.LineNumber = Context->GetLineNumber(i, nullptr, &SectionName);
                    Frame.Source = SectionName ? ANSI_TO_TCHAR(SectionName) : TEXT("");
                    Frame.ModuleName = ScriptFunction->GetModuleName() ? ANSI_TO_TCHAR(ScriptFunction->GetModuleName()) : TEXT("");
                    Stack.Frames.Insert(Frame, 0); // ★ 最终发的是结构化 frame，而不是预格式化文本
                }
            }
        }
    }

    SendMessageToClient(Client, EDebugMessageType::CallStack, Stack);
}
```

设计取舍：

- `puerts` 复用 VM 原生栈，接入成本低，适合快速接 Chrome DevTools，但调用栈保真度会随 backend 能力变化。
- `Angelscript` 维护帧模型的成本更高，但换来的是协议、测试和 JIT/Blueprint 混合栈都能共享同一套数据源。

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 调用栈载体 | `FString` 文本快照，`QuickJS` 路径直接空串 | `FAngelscriptDebugStack` + `FAngelscriptCallStack` 结构化消息 | 实现方式不同 |
| JIT / 非解释路径来源 | 依赖 backend 当前能否给出 stack trace | `FScopeJITDebugCallstack` 与普通 `asIScriptContext` 共存 | Angelscript 当前实现更完整 |
| Blueprint 栈并入 | 当前检视路径未见等价 Blueprint frame 合并链 | `SendCallStack()` 显式插入 Blueprint 帧 | puerts 当前没有实现同等级集成 |

### [维度 D9] 测试基础设施：`puerts` 暴露的是 VM testing hook，`Angelscript` 把测试引擎模式做成公共基础设施

前面的 D9 已经说过 `puerts` 没有像 `AngelscriptTest` 这样的一等测试模块；这一轮继续下钻后，可以更准确地区分成“没有测试能力”与“测试能力停留在哪一层”。在 `puerts` 当前 UE 插件源码里，`IJsEnv` 明确暴露了 `RequestMinorGarbageCollectionForTesting()` / `RequestFullGarbageCollectionForTesting()`，实现也直接调用 `v8::Isolate::RequestGarbageCollectionForTesting(...)`。这说明它不是完全没考虑可测试性，而是把“可测试性”主要做成 VM 级调试/诊断钩子。再结合 `Reference/puerts/unreal/Puerts/Source/WasmCore/Private/WasmCore.cpp:42` 这种直接调用 `TestVector` 的样例代码，可以看出当前检视范围里的测试更多是 hook/sample 形态，而不是插件层统一自动化入口。

`Angelscript` 则把测试基础设施明确产品化了。运行时里有 `FAngelscriptUnitTests` 把脚本单元测试暴露进 UE Automation Tool；测试公共库 `AngelscriptTestUtilities.h` 会把脚本源码写到 `Saved/Automation`、预处理、编译、执行；同一份夹具还支持 `SharedClone / IsolatedFull / ProductionLike` 三种引擎模式。也就是说，Angelscript 不是只有“很多测试文件”，而是把“如何创建可重复的测试运行态”也抽成了公共 API。

```
[puerts] Test Surface
IJsEnv testing hook
 -> RequestMinor/FullGarbageCollectionForTesting()
 -> backend isolate call
 -> manual / ad hoc verification

[Angelscript] Test Surface
Automation registration
 -> FAngelscriptTestFixture
 -> SharedClone / IsolatedFull / ProductionLike
 -> BuildModule(temp .as)
 -> runtime / debugger / editor tests
```

[1] `puerts` 当前能看到的是“为了测试而开放的 VM 钩子”，不是插件层自动化测试框架。

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JsEnv.h
// 位置: 34-38, 76-83，testing surface 暴露在 IJsEnv 接口层
// ============================================================================
virtual void RequestMinorGarbageCollectionForTesting() = 0;
virtual void RequestFullGarbageCollectionForTesting() = 0;
virtual void WaitDebugger(double Timeout) = 0;

// equivalent to Isolate->RequestGarbageCollectionForTesting(v8::Isolate::kMinorGarbageCollection)
// It is only valid to call this function if --expose_gc was specified
void RequestMinorGarbageCollectionForTesting();

// equivalent to Isolate->RequestGarbageCollectionForTesting(v8::Isolate::kFullGarbageCollection)
void RequestFullGarbageCollectionForTesting();
```

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp
// 位置: 1170-1187，具体实现仍然是直接向 V8 isolate 下发 testing GC 请求
// ============================================================================
void FJsEnvImpl::RequestMinorGarbageCollectionForTesting()
{
#ifndef WITH_QUICKJS
    MainIsolate->RequestGarbageCollectionForTesting(v8::Isolate::kMinorGarbageCollection);
    // ★ 这是底层 VM hook，不是 UE automation fixture
#endif
}

void FJsEnvImpl::RequestFullGarbageCollectionForTesting()
{
#ifndef WITH_QUICKJS
    MainIsolate->RequestGarbageCollectionForTesting(v8::Isolate::kFullGarbageCollection);
#endif
}
```

[2] `Angelscript` 不只注册自动化测试，还把测试运行态和脚本编译入口抽成公共夹具。

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Testing/UnitTest.cpp
// 位置: 658-664，把脚本单元测试正式挂到 UE Automation Tool
// ============================================================================
#if WITH_DEV_AUTOMATION_TESTS
// This test exposes the tests to the Test Automation tool in the editor, so that
// devs can run the tests there as well.
IMPLEMENT_COMPLEX_AUTOMATION_TEST(
    FAngelscriptUnitTests, "Angelscript.UnitTests",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter);
#endif
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h
// 位置: 535-552, 727-759，测试公共层负责写临时脚本、编译模块和切换引擎模式
// ============================================================================
inline asIScriptModule* BuildModule(FAutomationTestBase& Test, FAngelscriptEngine& Engine, const char* ModuleName, const FString& Source)
{
    const FString AbsoluteFilename = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), UniqueFilename);
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(AbsoluteFilename), true);
    if (!FFileHelper::SaveStringToFile(Source, *AbsoluteFilename))
    {
        Test.AddError(FString::Printf(TEXT("Failed to write script module '%s' to '%s'"), *RequestedModuleName, *AbsoluteFilename));
        return nullptr;
    }
    // ★ 这里之后还会走预处理、编译和诊断回传
}

enum class ETestEngineMode : uint8
{
    SharedClone,
    IsolatedFull,
    ProductionLike,
};

switch (Mode)
{
case ETestEngineMode::SharedClone:
    FAngelscriptEngine& SharedEngine = AcquireCleanSharedCloneEngine(); // ★ 共享克隆态
    Engine = &SharedEngine;
    EngineScope = MakeUnique<FAngelscriptEngineScope>(SharedEngine);
case ETestEngineMode::IsolatedFull:
    OwnedEngine = CreateIsolatedFullEngine(); // ★ 全隔离全量引擎态
    if (OwnedEngine.IsValid())
    {
        Engine = OwnedEngine.Get();
        EngineScope = MakeUnique<FAngelscriptEngineScope>(*Engine);
    }
case ETestEngineMode::ProductionLike:
    FResolvedProductionLikeEngine Resolved;
    if (AcquireProductionLikeEngine(InTest, TEXT("FAngelscriptTestFixture failed to acquire production-like engine"), Resolved))
    {
        OwnedEngine = MoveTemp(Resolved.OwnedEngine);
        EngineScope = MoveTemp(Resolved.EngineScope);
        Engine = Resolved.Engine;
    }
}
```

设计取舍：

- `puerts` 把测试能力放在 VM hook 层，维护成本低，也方便手工诊断，但难以自然演进成插件级回归体系。
- `Angelscript` 为了可回归性承担了更重的夹具成本，不过换来的是运行时、调试器、编辑器工作流都能复用同一套测试地基。

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| VM 级 testing hook | 有，`RequestGarbageCollectionForTesting()` 直接下发到 isolate | 也有，但不是测试体系的终点 | 实现质量差异 |
| 一级自动化入口 | 当前检视范围未见等价 `IMPLEMENT_*AUTOMATION_TEST` 入口 | `FAngelscriptUnitTests` 正式挂进 Automation Tool | puerts 当前没有实现同等级自动化入口 |
| 测试运行态抽象 | 当前检视路径未见等价 `SharedClone / IsolatedFull / ProductionLike` 夹具层 | `FAngelscriptTestFixture` 明确提供三种模式 | puerts 当前没有实现同等级测试基础设施 |

### [维度 D11] 部署与打包：`puerts` 的部署配置会直接改变运行时拓扑，`Angelscript` 的部署配置主要改变编译/可见性策略

前面的 D11 已经讲过脚本根、`NonUFS` 二进制和 cache；这一轮补的是“部署配置到底只影响内容，还是会改运行时拓扑”。`puerts` 的答案是后者。`UPuertsSetting` 不只暴露 `RootPath`、`DebugPort`、`WatchDisable`，还把 `NumberOfJsEnv` 做成配置；`FPuertsModule::MakeSharedJsEnv()` 启动时先读 `DefaultPuerts.ini`，再允许 `-JsEnvDebugPort` 覆盖端口，最后根据 `NumberOfJsEnv` 在 `FJsEnv` 与 `FJsEnvGroup` 之间二选一。更关键的是，组模式一旦成立，`WaitDebugger` 就不再可用。也就是说，同一份已打包产物，只改配置就能把运行时从“单 VM”切到“多 VM 分片”，而且功能面随之变化。

`Angelscript` 这边的部署开关更像“行为模式”而不是“拓扑模式”。`FAngelscriptEngineConfig::FromCurrentProcess()` 会解析 `as-simulate-cooked`、`as-generate-precompiled-data`、`as-ignore-precompiled-data` 等命令行，再在引擎初始化时决定是否使用 editor scripts、是否生成/读取 precompiled data。但 `AngelscriptRuntimeModule` 自身仍然只创建一个 `OwnedPrimaryEngine`。这说明它的部署期变化主要是“脚本内容怎样被编译、哪些内容可见”，而不是“宿主里到底起几个脚本引擎实例”。

```
[puerts] Deployment-Time Topology
DefaultPuerts.ini / -JsEnvDebugPort
 -> RootPath / DebugPort / WaitDebugger / NumberOfJsEnv / WatchDisable
 -> MakeSharedJsEnv()
    -> FJsEnvGroup (N envs, WaitDebugger disabled)
    -> or FJsEnv (single env)
 -> runtime topology changes after packaging

[Angelscript] Deployment-Time Behavior
command line
 -> FAngelscriptEngineConfig
 -> bSimulateCooked / bGeneratePrecompiledData / bIgnorePrecompiledData
 -> OwnedPrimaryEngine.Initialize()
 -> same primary-engine topology, different compile/visibility mode
```

[1] `puerts` 把运行时拓扑本身暴露成部署配置的一部分。

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsSetting.h
// 位置: 20-22, 32-50，部署配置不只描述脚本根，也描述运行时实例数量
// ============================================================================
FString RootPath = "JavaScript";
int32 DebugPort = 8080;
bool WaitDebugger = false;
double WaitDebuggerTimeout = 0;
int32 NumberOfJsEnv = 1; // ★ 直接把运行时拓扑暴露成配置
bool WatchDisable = false;
```

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp
// 位置: 174-242, 366-392，启动时读 ini / 命令行，再决定单实例还是组模式
// ============================================================================
// we can also specify the debug port via command line, -JsEnvDebugPort
static const FString DebugPortParam{TEXT("JsEnvDebugPort")};
if (OutParams.Contains(DebugPortParam))
{
    Result = FCString::Atoi(*OutParams[DebugPortParam]); // ★ 命令行可覆盖部署端口
}

NumberOfJsEnv = (Settings.NumberOfJsEnv > 1 && Settings.NumberOfJsEnv < 10) ? Settings.NumberOfJsEnv : 1;
if (NumberOfJsEnv > 1)
{
    JsEnvGroup = MakeShared<PUERTS_NAMESPACE::FJsEnvGroup>(
        NumberOfJsEnv,
        std::make_shared<PUERTS_NAMESPACE::DefaultJSModuleLoader>(Settings.RootPath),
        std::make_shared<PUERTS_NAMESPACE::FDefaultLogger>(),
        DebuggerPortFromCommandLine < 0 ? Settings.DebugPort : DebuggerPortFromCommandLine);
    if (Settings.WaitDebugger)
    {
        UE_LOG(PuertsModule, Warning, TEXT("Do not support WaitDebugger in Group Mode!"));
        // ★ 组模式不仅是“多几个 env”，功能矩阵也随之改变
    }
    JsEnvGroup->RebindJs();
}
else
{
    JsEnv = MakeShared<PUERTS_NAMESPACE::FJsEnv>(
        std::make_shared<PUERTS_NAMESPACE::DefaultJSModuleLoader>(Settings.RootPath),
        std::make_shared<PUERTS_NAMESPACE::FDefaultLogger>(),
        DebuggerPortFromCommandLine < 0 ? Settings.DebugPort : DebuggerPortFromCommandLine);
    if (Settings.WaitDebugger)
    {
        JsEnv->WaitDebugger(Settings.WaitDebuggerTimeout);
    }
    JsEnv->RebindJs();
}

const FString PuertsConfigIniPath =
    FConfigCacheIni::NormalizeConfigIniPath(FPaths::SourceConfigDir().Append(TEXT("DefaultPuerts.ini")));
GConfig->GetBool(SectionName, TEXT("WaitDebugger"), Settings.WaitDebugger, PuertsConfigIniPath);
GConfig->GetInt(SectionName, TEXT("NumberOfJsEnv"), Settings.NumberOfJsEnv, PuertsConfigIniPath);
GConfig->GetBool(SectionName, TEXT("WatchDisable"), Settings.WatchDisable, PuertsConfigIniPath);
DebuggerPortFromCommandLine = GetDebuggerPortFromCommandLine();
```

[2] `Angelscript` 也有部署期命令行开关，但这些开关进入的是单个 primary engine 的行为配置，而不是“单/多实例切换”。

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 514-529, 866-875, 1425-1433，命令行只决定行为模式
// ============================================================================
FAngelscriptEngineConfig FAngelscriptEngineConfig::FromCurrentProcess()
{
    FAngelscriptEngineConfig Config;
    Config.bSimulateCooked = FParse::Param(FCommandLine::Get(), TEXT("as-simulate-cooked"));
    Config.bGeneratePrecompiledData = FParse::Param(FCommandLine::Get(), TEXT("as-generate-precompiled-data"));
    Config.bIgnorePrecompiledData = FParse::Param(FCommandLine::Get(), TEXT("as-ignore-precompiled-data"));
    FParse::Value(FCommandLine::Get(), TEXT("-asdebugport="), Config.DebugServerPort);
    return Config;
}

bSimulateCooked = RuntimeConfig.bSimulateCooked;
bUseEditorScripts = WITH_EDITOR
    && ((RuntimeConfig.bIsEditor && !RuntimeConfig.bRunningCommandlet) || bForcePreprocessEditorCode)
    && !bSimulateCooked;
bGeneratePrecompiledData = RuntimeConfig.bGeneratePrecompiledData;
bUsePrecompiledData = !bGeneratePrecompiledData && !RuntimeConfig.bIgnorePrecompiledData
    && !RuntimeConfig.bRunningCommandlet && !WITH_EDITOR && !bScriptDevelopmentMode;
// ★ 这里改变的是脚本可见性和 precompiled 策略，不是引擎实例拓扑
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp
// 位置: 160-164，运行时主链仍然只创建一个 OwnedPrimaryEngine
// ============================================================================
else
{
    OwnedPrimaryEngine = MakeUnique<FAngelscriptEngine>();
    FAngelscriptEngineContextStack::Push(OwnedPrimaryEngine.Get());
    OwnedPrimaryEngine->Initialize(); // ★ 部署态主链没有 ini 驱动的单/多引擎切换
}
```

设计取舍：

- `puerts` 的部署面更动态，项目可以用配置切 runtime topology；代价是部署配置也会改变能力边界，运维和排障需要知道当前到底跑在单实例还是组模式。
- `Angelscript` 的部署面更收敛，主引擎拥有者稳定；代价是它没有把“多引擎部署拓扑”做成生产配置项，而是把变化收束到脚本可见性与 precompiled 策略。

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 部署配置是否改变运行时拓扑 | `NumberOfJsEnv` 直接切 `FJsEnv` / `FJsEnvGroup` | 主链始终 `OwnedPrimaryEngine->Initialize()` | 实现方式不同 |
| 部署期覆盖面 | `DefaultPuerts.ini` + `-JsEnvDebugPort` | 命令行 `as-*` flags | 实现方式不同 |
| 配置变更后的能力矩阵 | 组模式下 `WaitDebugger` 不再可用 | 改的是 `simulate cooked` / `precompiled` / editor 可见性 | puerts 的部署配置面更动态，Angelscript 当前没有实现同等级部署期拓扑切换 |

---

## 深化分析 (2026-04-09 00:39:36)

### [维度 D2] 反射绑定机制：`puerts` 的 extension methods 是“共享扫描 + 延迟注入”的 synthetic member，`Angelscript` 的 `ScriptMixin` 是“签名重写 + 调用时回填”

前面的 D2 已经讲过 `DefineClass<T>()` 和 `Bind_*.cpp`。这一轮只补“现有 UE 类型如何长出额外成员”这一层。`puerts` 不是在每次调用时临时猜测 extension method，而是先扫描 `UExtensionMethods` 原生类，把静态函数按“首个参数类型”归到 `ExtensionMethodsMap`。这张表随后被运行时 `GetTemplateInfoOfType()` 消耗，挂进 `StructWrapper`；也被 `DeclarationGenerator::GatherExtensions()` 消耗，写进 `.d.ts`。结果是 runtime surface 与 IDE surface 共用同一份 synthetic-member 规则。

`Angelscript` 的 mixin 方案则更接近“签名改写”。`UCLASS(meta = (ScriptMixin = "..."))` 先声明目标类型；`Helper_FunctionSignature` 在构造脚本签名时检查首参是否命中 mixin 目标，命中后直接删掉首参并把函数变成实例成员形态；如果最终走 reflective fallback，调用桥还会把 `this` 再注回隐藏首参。两边都能把静态 helper 暴露成成员方法，但 `puerts` 共享的是“扫描结果”，`Angelscript` 共享的是“签名规则”。

```
[puerts] Extension Method Surface
UExtensionMethods subclass
 -> scan first parameter type
 -> ExtensionMethodsMap[UStruct]
 -> GetTemplateInfoOfType()
 -> AddExtensionMethods()
 -> FExtensionMethodTranslator
 -> runtime prototype + .d.ts overload

[Angelscript] ScriptMixin Surface
UCLASS(meta = ScriptMixin)
 -> Helper_FunctionSignature strips first arg
 -> member-style script declaration
 -> reflective fallback sets bInjectMixinObject
 -> call bridge reinserts receiver
```

[1] `puerts` 运行时把 extension method 扫进类型索引，并在模板首次建立时注入。

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp
// 位置: 931-983, 3052-3065，先扫描 extension methods，再在类型模板首次建立时注入
// ============================================================================
void FJsEnvImpl::InitExtensionMethodsMap()
{
    for (TObjectIterator<UClass> It; It; ++It)
    {
        UClass* Class = *It;
        if (Class->IsChildOf<UExtensionMethods>() && Class->IsNative())
        {
            for (TFieldIterator<UFunction> FuncIt(Class, EFieldIteratorFlags::ExcludeSuper); FuncIt; ++FuncIt)
            {
                UFunction* Function = *FuncIt;
                if (Function->HasAnyFunctionFlags(FUNC_Static))
                {
                    TFieldIterator<PropertyMacro> ParamIt(Function);
                    if (ParamIt && ((ParamIt->PropertyFlags & (CPF_Parm | CPF_ReturnParm)) == CPF_Parm))
                    {
                        UStruct* Struct = nullptr;   // ★ 用首参类型决定挂到哪个 UE 类型上
                        if (auto ObjectPropertyBase = CastFieldMacro<ObjectPropertyBaseMacro>(*ParamIt))
                            Struct = ObjectPropertyBase->PropertyClass;
                        else if (auto StructProperty = CastFieldMacro<StructPropertyMacro>(*ParamIt))
                            Struct = StructProperty->Struct;

                        if (Struct)
                            ExtensionMethodsMap[Struct].push_back(Function);
                    }
                }
            }
        }
    }
    ExtensionMethodsMapInited = true;
}

if (!ExtensionMethodsMapInited)
{
    InitExtensionMethodsMap();
}
auto ExtensionMethodsIter = ExtensionMethodsMap.find(InStruct);
if (ExtensionMethodsIter != ExtensionMethodsMap.end())
{
    StructWrapper->AddExtensionMethods(ExtensionMethodsIter->second); // ★ 首次触达类型时才真正注入 wrapper
    ExtensionMethodsMap.erase(ExtensionMethodsIter);
}
```

[2] `puerts` 的声明生成复用了同样的 extension 语义，而不是单独维护另一套 IDE 规则。

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp
// 位置: 315-358, 1110-1170，声明生成独立扫描 extension methods，并把它们并入 overload 集
// ============================================================================
void FTypeScriptDeclarationGenerator::InitExtensionMethodsMap()
{
    TArray<UObject*> SortedClasses(GetSortedClasses());
    for (int i = 0; i < SortedClasses.Num(); ++i)
    {
        UClass* Class = Cast<UClass>(SortedClasses[i]);
        if (!Class)
            continue;
        bool IsExtensionMethod = IsChildOf(Class, "ExtensionMethods");
        if (IsExtensionMethod)
        {
            for (TFieldIterator<UFunction> FuncIt(Class, EFieldIteratorFlags::ExcludeSuper); FuncIt; ++FuncIt)
            {
                UFunction* Function = *FuncIt;
                if (Function->HasAnyFunctionFlags(FUNC_Static))
                {
                    TFieldIterator<PropertyMacro> ParamIt(Function);
                    if (ParamIt && ((ParamIt->PropertyFlags & (CPF_Parm | CPF_ReturnParm)) == CPF_Parm))
                    {
                        UStruct* Struct = nullptr;
                        if (auto ObjectPropertyBase = CastFieldMacro<ObjectPropertyBaseMacro>(*ParamIt))
                            Struct = ObjectPropertyBase->PropertyClass;
                        else if (auto StructProperty = CastFieldMacro<StructPropertyMacro>(*ParamIt))
                            Struct = StructProperty->Struct;
                        if (Struct)
                            ExtensionMethodsMap[Struct].push_back(Function); // ★ IDE surface 共享首参分发规则
                    }
                }
            }
        }
    }
}

void FTypeScriptDeclarationGenerator::GatherExtensions(UStruct* Struct, FStringBuffer& Buff)
{
    auto ExtensionMethodsIter = ExtensionMethodsMap.find(Struct);
    if (ExtensionMethodsIter != ExtensionMethodsMap.end())
    {
        for (auto Iter = ExtensionMethodsIter->second.begin(); Iter != ExtensionMethodsIter->second.end(); ++Iter)
        {
            UFunction* Function = *Iter;
            FStringBuffer Tmp;
            if (!GenFunction(Tmp, Function, true, false, false, true))
                continue;
            TryToAddOverload(Outputs, Function->GetName(), false, Tmp.Buffer); // ★ 直接并入实例方法 overload
        }
    }
}
```

[3] `puerts` 的 extension method 在运行时不是普通 `FFunctionTranslator`，而是专门走 `FExtensionMethodTranslator`。

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp
// 位置: 16-18, 38-56，把 extension method 单独翻译成可挂到 prototype 的 method translator
// ============================================================================
void FStructWrapper::AddExtensionMethods(const std::vector<UFunction*>& InExtensionMethods)
{
    ExtensionMethods.insert(ExtensionMethods.end(), InExtensionMethods.begin(), InExtensionMethods.end());
}

std::shared_ptr<FFunctionTranslator> FStructWrapper::GetMethodTranslator(UFunction* InFunction, bool IsExtension)
{
    auto Iter = MethodsMap.Find(InFunction->GetFName());
    if (!Iter)
    {
        std::shared_ptr<FFunctionTranslator> FunctionTranslator;
        if (IsExtension)
            FunctionTranslator = std::make_shared<FExtensionMethodTranslator>(InFunction); // ★ 扩展方法走独立 translator
        else
            FunctionTranslator = std::make_shared<FFunctionTranslator>(InFunction, false);
        MethodsMap.Add(InFunction->GetFName(), FunctionTranslator);
        return FunctionTranslator;
    }
    Iter->get()->Init(InFunction, false);
    return *Iter;
}
```

[4] `Angelscript` 的 mixin 入口来自 `ScriptMixin` 元数据；签名阶段删掉首参，运行时 fallback 再把对象回填。

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/RuntimeCurveLinearColorMixinLibrary.h
// 位置: 8-22，mixin 目标先写在 UCLASS 元数据里
// ============================================================================
UCLASS(meta = (ScriptMixin = "FRuntimeCurveLinearColor"))
class ANGELSCRIPTRUNTIME_API URuntimeCurveLinearColorMixinLibrary : public UObject
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category = "Math|Curves")
    static void AddDefaultKey(FRuntimeCurveLinearColor& Target, float InTime, FLinearColor InColor)
    {
        Target.ColorCurves[0].AddKey(InTime, InColor.R); // ★ 首参 Target 就是未来要被吃掉的 receiver
        Target.ColorCurves[1].AddKey(InTime, InColor.G);
        Target.ColorCurves[2].AddKey(InTime, InColor.B);
        Target.ColorCurves[3].AddKey(InTime, InColor.A);
    }
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h
// 位置: 276-314，命中 ScriptMixin 时把首参从脚本签名里删掉
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
            ArgumentTypes.RemoveAt(0);   // ★ 对脚本作者而言，首参不再出现
            ArgumentNames.RemoveAt(0);
            ArgumentDefaults.RemoveAt(0);
            ClassName = Mixin;
            bStaticInScript = false;
            break;
        }
    }
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp
// 位置: 135-151，fallback 调用桥会把 mixin receiver 再注回隐藏首参
// ============================================================================
if (Signature.bStaticInUnreal)
{
    ReflectiveSignature->StaticObject = InType->GetClass(FAngelscriptTypeUsage::DefaultUsage)->GetDefaultObject();
    if (ReflectiveSignature->StaticObject == nullptr)
        return false;

    ReflectiveSignature->bInjectMixinObject = true; // ★ 调用时把 this 回填到 Unreal 静态函数的首参
    const int32 FunctionId = FAngelscriptBinds::BindMethodDirect(
        Signature.ClassName,
        Signature.Declaration,
        asFUNCTION(CallBlueprintCallableReflectiveFallback),
        asCALL_GENERIC,
        ASAutoCaller::FunctionCaller::Make(),
        ReflectiveSignature);
    Signature.ModifyScriptFunction(FunctionId);
    return true;
}
```

设计取舍：

- `puerts` 的好处是 runtime 与 `.d.ts` 的扩展成员面天然一致；代价是 runtime 与 generator 都要维护一次“首参归属”扫描逻辑。
- `Angelscript` 的好处是 mixin 规则直接落在签名层，脚本表面更像原生成员；代价是 IDE/文档面若想完全复现这层语义，必须再次理解 `ScriptMixin` 与 fallback 注入规则。

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 扩展成员来源 | 扫描 `UExtensionMethods` 原生类，按首参类型建 `ExtensionMethodsMap` | `UCLASS(meta = (ScriptMixin = ...))` 声明 mixin 目标 | 实现方式不同 |
| runtime / IDE 是否共用同一 synthetic-member 源 | 运行时注入与 `.d.ts` 生成都复用同一 extension 语义 | 运行时绑定与文档/导航更多依赖签名构造和后续提取 | Angelscript 当前没有实现同等级“共享 synthetic-member 源” |
| receiver 处理时机 | 注入 `FExtensionMethodTranslator` 时固定 | 签名阶段删首参，调用阶段再 `bInjectMixinObject` 回填 | 实现方式不同 |

### [维度 D6] 代码生成与 IDE 支持：`puerts` 把 UE 语义规则前置到 TypeScript authoring 阶段，`Angelscript` 的 IDE 面更多是“编译/运行后可查询”

前面的 D6 已经覆盖了 `.d.ts`、`LanguageService` 和 Blueprint 回写。这里补的是“IDE 管线到底只负责补全，还是也负责守住 UE 语义边界”。`puerts` 的答案明显是后者：`CodeAnalyze.ts` 先拿语法/语义诊断，只有在无错误时才会写 `.js`、通知 reload、排队 Blueprint refresh；同时 `UEMeta.ts` 对 `BlueprintImplementableEvent`、`BlueprintNativeEvent`、`Server/Client/NetMulticast`、`ServiceRequest/ServiceResponse` 等 specifier 组合做前置校验，错误直接停留在 authoring 阶段。

`Angelscript` 并不是“没有校验”。`AngelscriptPreprocessor.cpp` 也会拦住 `BlueprintEvent` 与 `BlueprintOverride` 冲突、非法网络 specifier、`WithValidation` 与 `Server/Client` 不匹配等情况；只是它更偏编译前宏语义检查，而不是一个常驻 IDE semantic host。编辑器外部真正能消费到的 IDE 数据，更多来自 `SendDebugDatabase()` 和 `AngelscriptDocs.cpp` 这类 runtime 导出路径。换句话说，两边都在做约束，但 `puerts` 把约束放在“写代码时”，`Angelscript` 更偏“编译和运行后查询真实已注册表面”。

```
[puerts] Authoring Guardrail
.ts source
 -> TS LanguageService
 -> syntactic / semantic diagnostics
 -> UEMeta.ts specifier validation
 -> emit JS / notify reload / queue Blueprint refresh

[Angelscript] IDE Surface
.as source
 -> Preprocessor conflict checks
 -> runtime registration
 -> DebugDatabase / Docs serialization
 -> external navigation / tooltip consumers
```

[1] `puerts` 只有在 TypeScript 诊断通过后，才会真正写出 `.js`、通知热重载和刷新 Blueprint。

```typescript
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/PuertsEditor/CodeAnalyze.ts
// 位置: 537-585, 638-642，诊断先行；只有通过后才 emit / reload / refresh Blueprint
// ============================================================================
function onSourceFileAddOrChange(sourceFilePath: string, reload: boolean, program?: ts.Program, doEmitJs: boolean = true, doEmitBP:boolean = true) {
    let sourceFile = program.getSourceFile(sourceFilePath);
    if (sourceFile) {
        const diagnostics = [
            ...program.getSyntacticDiagnostics(sourceFile),
            ...program.getSemanticDiagnostics(sourceFile)
        ];

        if (diagnostics.length > 0) {
            logErrors(diagnostics);                  // ★ 有错误时直接停在 authoring 阶段
        } else {
            let emitOutput = service.getEmitOutput(sourceFilePath);
            if (!emitOutput.emitSkipped) {
                emitOutput.outputFiles.forEach(output => {
                    if (doEmitJs) {
                        UE.FileSystemOperation.WriteFile(output.name, output.text);
                    }
                });
                if (moduleFileName && reload) {
                    UE.FileSystemOperation.PuertsNotifyChange(moduleFileName, jsSource); // ★ 诊断通过后才通知 runtime reload
                }
            }
        }
    }

    if (foundType && foundBaseTypeUClass) {
        fileVersions[sourceFilePath].isBP = true;
        pendingBlueprintRefleshJobs.push({ type: foundType, op: () => onBlueprintTypeAddOrChange(foundBaseTypeUClass, foundType, modulePath) });
        // ★ Blueprint 刷新也依赖同一条“先过诊断”的 authoring 管线
    }
}
```

[2] `puerts` 在 authoring 层直接实现了一套 UE specifier 组合校验，不只是把 metadata 原样转抄。

```typescript
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/PuertsEditor/UEMeta.ts
// 位置: 894-1065，对 Blueprint / RPC / Service specifier 组合做前置语义约束
// ============================================================================
switch (value.Specifier.toLowerCase())
{
case 'BlueprintNativeEvent'.toLowerCase():
    if (FunctionFlags & BigInt(UE.FunctionFlags.FUNC_Net))
        return markInvalidSince('BlueprintNativeEvent functions cannot be replicated!');
    if ((FunctionFlags & BigInt(UE.FunctionFlags.FUNC_BlueprintEvent)) && !(FunctionFlags & BigInt(UE.FunctionFlags.FUNC_Native)))
        return markInvalidSince('A function cannot be both BlueprintNativeEvent and BlueprintImplementableEvent!');
    if (bSawPropertyAccessor)
        return markInvalidSince("A function cannot be both BlueprintNativeEvent and a Blueprint Property accessor!");
    break;

case 'Server'.toLowerCase():
    if (FunctionFlags & BigInt(UE.FunctionFlags.FUNC_BlueprintEvent))
        return markInvalidSince('BlueprintImplementableEvent or BlueprintNativeEvent functions cannot be declared as Client or Server');
    if (FunctionFlags & BigInt(UE.FunctionFlags.FUNC_Exec))
        return markInvalidSince('Exec functions cannot be replicated!');
    FunctionFlags |= BigInt(UE.FunctionFlags.FUNC_Net);
    FunctionFlags |= BigInt(UE.FunctionFlags.FUNC_NetServer);
    break;

case 'ServiceRequest'.toLowerCase():
    if (FunctionFlags & BigInt(UE.FunctionFlags.FUNC_BlueprintEvent))
        return markInvalidSince('BlueprintImplementableEvent or BlueprintNativeEvent functions cannot be declared as a ServiceRequest');
    FunctionFlags |= BigInt(UE.FunctionFlags.FUNC_NetRequest);
    parseNetServiceIdentifiers(value.Values);
    if (bValidSpecifiers && EndpointName.length == 0)
        markInvalidSince('ServiceRequest needs to specify an endpoint name'); // ★ 端点名缺失也在 authoring 层报错
    break;
}
```

[3] `Angelscript` 也会做前置校验，但范围更偏脚本宏语义。

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp
// 位置: 1575-1685，预处理阶段会阻止一部分非法 specifier 组合
// ============================================================================
else if (Spec.Name == PP_NAME_NetMulticast || Spec.Name == PP_NAME_NetServer || Spec.Name == PP_NAME_NetClient)
{
    if (FunctionDesc->bBlueprintOverride)
    {
        MacroError(File, Macro, FString::Printf(TEXT("UFUNCTION() %s cannot both be BlueprintOverride and have network specifiers"), *FunctionDesc->FunctionName));
        bHasError = true;
        continue;
    }
    if (FunctionDesc->bIsStatic)
    {
        MacroError(File, Macro, FString::Printf(TEXT("Static UFUNCTION()s cannot use network specifiers"), *FunctionDesc->FunctionName));
        bHasError = true;
        continue;
    }
}
else if (Spec.Name == PP_NAME_WithValidation)
{
    if (Specs.FindByPredicate([](FSpecifier& CurSpec) -> bool { return CurSpec.Name == PP_NAME_NetServer || CurSpec.Name == PP_NAME_NetClient; }))
        FunctionDesc->bNetValidate = true;
    else
        MacroError(File, Macro, FString::Printf(TEXT("UFUNCTION() %s has the WithValidation property without the Server or Client property!"), *FunctionDesc->FunctionName));
}
else if (Spec.Name == PP_NAME_BlueprintOverride)
{
    if (FunctionDesc->bBlueprintEvent)
    {
        MacroError(File, Macro, FString::Printf(TEXT("UFUNCTION() %s cannot be both BlueprintEvent and BlueprintOverride."), *FunctionDesc->FunctionName));
        bHasError = true;
        continue;
    }
}
```

[4] `Angelscript` 面向 IDE/外部工具的主数据，更多来自 runtime 导出的 debug database。

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 位置: 1499-1515，IDE 可消费数据更多来自 runtime 已注册表面
// ============================================================================
FAngelscriptDebugDatabaseSettings DebugSettings;
DebugSettings.bAutomaticImports = FAngelscriptEngine::Get().ShouldUseAutomaticImportMethod();
DebugSettings.bFloatIsFloat64 = GetDefault<UAngelscriptSettings>()->bScriptFloatIsFloat64;
SendMessageToClient(Client, EDebugMessageType::DebugDatabaseSettings, DebugSettings);

auto JsonWriter = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&DB.Database);
FJsonSerializer::Serialize(Root, JsonWriter);
SendMessageToClient(Client, EDebugMessageType::DebugDatabase, DB); // ★ 导出的是 runtime 已知类型数据库
```

[5] `Angelscript` 的文档文本同样是在已注册类型/属性/函数上回读 `ToolTip` 元数据。

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp
// 位置: 490-503, 533-539，文档面来自已注册类型与反射元数据
// ============================================================================
if (PropDesc != nullptr)
{
    Prop.Documentation = PropDesc->GetMetaData("ToolTip");
    Prop.Category = PropDesc->GetMetaData("Category");
}
else
{
    UFunction* FuncDesc = UnrealClass->FindFunctionByName(*(TEXT("Get") + Prop.Name));
    if (FuncDesc != nullptr)
    {
        Prop.Documentation = GetFunctionTooltip(FuncDesc->GetMetaData("ToolTip"));
        Prop.Category = FuncDesc->GetMetaData("Category");
    }
}
ClassDoc.Documentation = Class->GetMetaData("ToolTip"); // ★ 文档面来自已注册类型与反射元数据
```

设计取舍：

- `puerts` 把 UE 语义约束前移到 authoring 阶段，编辑器里更早报错；代价是 TypeScript host、metadata parser、Blueprint 回写三者耦合很重。
- `Angelscript` 的 runtime docs / debug database 更贴近真实已注册表面，导出结果更权威；代价是 authoring 期缺少一层与 `puerts` 等价的常驻语义主机。

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 语义校验时机 | `CodeAnalyze.ts` + `UEMeta.ts` 在 authoring 阶段先报错，再决定是否 emit | `AngelscriptPreprocessor.cpp` 在编译前阻止部分非法 specifier 组合 | 实现方式不同 |
| 失败后的直接效果 | 不写 `.js`、不通知 reload、不中继 Blueprint refresh | 阻止脚本预处理/编译继续，runtime IDE 数据要等已注册表面 | 实现方式不同 |
| IDE 主数据源 | `TypeScript` semantic host + `.d.ts` + Blueprint schema 同步 | `DebugDatabase` / docs 序列化 + 源码导航 | Angelscript 当前没有实现同等级 authoring-phase semantic host |

### [维度 D8] 性能与优化：`puerts` 优化的是进程内 wrapper/template 复用，`Angelscript` 优化的是跨启动的 script graph / `StaticJIT` 重建成本

前面的 D8 已经拆过 `FastCall`、GC 和 `StaticJIT`。这里补的是“缓存到底活多久”。`puerts` 的缓存主要活在当前进程内：`FStructWrapper` 持有 `CachedFunctionTemplate`、`PropertiesMap`、`MethodsMap`、`FunctionsMap`，类型首次触达时才建 `TemplateInfo`，后续复用同一批 wrapper / translator。它解决的是“同一次 Editor/Game 进程里别反复搭 V8 模板”。

`Angelscript` 的热路径缓存则更偏 build-bound warm start。`FAngelscriptPrecompiledData` 会记下当前 build id，把活动脚本模块、类型、函数和静态名序列化出来，再按 `Stage1/2/3` 重建模块；`CreateFunctionId()` 还会把模块名、类型声明和函数声明 hash 成稳定 id。`FAngelscriptStaticJIT::CompileFunction()` 在生成模式下甚至直接把脚本函数排进 C++ 输出队列。所以两边都在“减少重复工作”，但缓存边界完全不同：一个偏进程内 V8 wrapper 复用，一个偏跨启动/跨构建的脚本图恢复与原生化。

```
[puerts] In-Process Warm Cache
UStruct
 -> TypeToTemplateInfoMap
 -> FStructWrapper::CachedFunctionTemplate
 -> PropertiesMap / MethodsMap / FunctionsMap
 -> first-touch cost amortized inside current process

[Angelscript] Build-Bound Warm Cache
active script
 -> PrecompiledData::InitFromActiveScript()
 -> Save / Load binary snapshot
 -> ApplyToModule Stage1 / Stage2 / Stage3
 -> optional StaticJIT C++ generation
```

[1] `puerts` 的缓存粒度首先定义在 `FStructWrapper` 对象本身：模板、属性 translator、方法 translator 都尽量在当前进程内复用。

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.h
// 位置: 22-77；Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp
// 位置: 16-19, 21-35, 38-56，进程内缓存的是 FunctionTemplate 与 translator
// ============================================================================
#define PUERTS_REUSE_STRUCTWRAPPER_FUNCTIONTEMPLATE 1

FORCEINLINE void Init(UStruct* InStruct, bool IsReuseTemplate)
{
    Struct = InStruct;
    if (!IsReuseTemplate)
    {
        ExternalInitialize = nullptr;
        ExternalFinalize = nullptr;
        Properties.clear();
        ExtensionMethods.clear();
        CachedFunctionTemplate.Reset(); // ★ 复用失败时才清空模板缓存
    }
}

v8::UniquePersistent<v8::FunctionTemplate> CachedFunctionTemplate;
TMap<FName, std::shared_ptr<FPropertyTranslator>> PropertiesMap;
TMap<FName, std::shared_ptr<FFunctionTranslator>> FunctionsMap;
TMap<FName, std::shared_ptr<FFunctionTranslator>> MethodsMap;

std::shared_ptr<FPropertyTranslator> FStructWrapper::GetPropertyTranslator(PropertyMacro* InProperty)
{
    auto Iter = PropertiesMap.Find(InProperty->GetFName());
    if (!Iter)
    {
        std::shared_ptr<FPropertyTranslator> PropertyTranslator = FPropertyTranslator::Create(InProperty);
        PropertiesMap.Add(InProperty->GetFName(), PropertyTranslator);
        Properties.push_back(PropertyTranslator);
        return PropertyTranslator;
    }
    FPropertyTranslator::CreateOn(InProperty, Iter->get()); // ★ 命中缓存时原位刷新 translator
    return *Iter;
}

std::shared_ptr<FFunctionTranslator> FStructWrapper::GetMethodTranslator(UFunction* InFunction, bool IsExtension)
{
    auto Iter = MethodsMap.Find(InFunction->GetFName());
    if (!Iter)
    {
        auto FunctionTranslator = IsExtension
            ? std::make_shared<FExtensionMethodTranslator>(InFunction)
            : std::make_shared<FFunctionTranslator>(InFunction, false);
        MethodsMap.Add(InFunction->GetFName(), FunctionTranslator);
        return FunctionTranslator;
    }
    Iter->get()->Init(InFunction, false); // ★ 后续访问只重读函数描述，不重建整套 wrapper
    return *Iter;
}
```

[2] `puerts` 还会在类型首次触达时缓存 `TemplateInfo`，并只把 extension methods 注入一次。

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp
// 位置: 3049-3066，类型模板缓存属于当前进程内的首次触达优化
// ============================================================================
auto TemplateInfoPtr = TypeToTemplateInfoMap.Find(InStruct);
if (!TemplateInfoPtr)
{
    auto StructWrapper = GetStructWrapper(InStruct, IsReuseTemplate);
    auto ExtensionMethodsIter = ExtensionMethodsMap.find(InStruct);
    if (ExtensionMethodsIter != ExtensionMethodsMap.end())
    {
        StructWrapper->AddExtensionMethods(ExtensionMethodsIter->second); // ★ 首次建类型模板时再补扩展方法
        ExtensionMethodsMap.erase(ExtensionMethodsIter);
    }
}
```

[3] `Angelscript` 的 `StaticJIT` 明确会把脚本函数排入 C++ 输出队列，这已经超出了“进程内 wrapper 复用”的范围。

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp
// 位置: 66-75, 118-139，生成模式下会把脚本函数转成待输出的 C++ 任务
// ============================================================================
int FAngelscriptStaticJIT::CompileFunction(asIScriptFunction* ScriptFunction, asJITFunction* OutJITFunction)
{
#if AS_CAN_GENERATE_JIT
    if (bGenerateOutputCode)
    {
        FunctionsToGenerate.Add((asCScriptFunction*)ScriptFunction, FGenerateFunction()); // ★ 真正把脚本函数排进 C++ 输出队列
        *OutJITFunction = nullptr;
        return 1;
    }
#endif
    check(false);
    return 0;
}
```

[4] `Angelscript` 的 precompiled 路线则把活动脚本图和稳定函数身份落成可跨启动复用的 snapshot。

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp
// 位置: 2627-2690, 2692-2729, 1535-1625，缓存的是可落盘的脚本图与稳定函数身份
// ============================================================================
void FAngelscriptPrecompiledData::InitFromActiveScript()
{
    BuildIdentifier = GetCurrentBuildIdentifier(); // ★ 先把当前 build 身份写进缓存
    int32 ModuleCount = Engine->GetModuleCount();
    for (int32 i = 0; i < ModuleCount; ++i)
    {
        asCModule* Module = (asCModule*)Engine->GetModuleByIndex(i);
        FString ModuleName = Module->GetName();
        Modules.FindOrAdd(ModuleName).InitFrom(*this, Module); // ★ 活动脚本模块被序列化进 snapshot
    }
}

void FAngelscriptPrecompiledData::Save(const FString& Filename)
{
    TArray<uint8> Data;
    FMemoryWriter Writer(Data, true);
    Writer << *this;
    FFileHelper::SaveArrayToFile(Data, *Filename); // ★ 缓存是可落盘的二进制快照
}

uint32 FAngelscriptPrecompiledData::CreateFunctionId(asIScriptFunction* Function)
{
    uint32 Id = 0;
    auto* ScriptModule = Function->GetModule();
    if (ScriptModule != nullptr)
    {
        Id = HashCombine(Id, FCrc::StrCrc_DEPRECATED((const ANSICHAR*)ScriptModule->GetName()));
        Id = HashCombine(Id, (uint32)(size_t)ScriptModule->GetUserData());
    }
    auto* ObjectType = Function->GetObjectType();
    if (ObjectType != nullptr)
        Id = HashCombine(Id, FCrc::StrCrc_DEPRECATED((const ANSICHAR*)ObjectType->GetEngine()->GetTypeDeclaration(ObjectType->GetTypeId(), true)));
    Id = HashCombine(Id, FCrc::StrCrc_DEPRECATED((const ANSICHAR*)Function->GetDeclaration(true, true))); // ★ 函数身份来自模块 + 类型 + 声明
    while (ProcessedIdToFunction.Contains(Id))
        ++Id;
    return Id;
}

void FAngelscriptPrecompiledModule::ApplyToModule_Stage1(FAngelscriptPrecompiledData& Context, asIScriptModule* InModule) const
{
    for (int32 i = 0, Count = Classes.Num(); i < Count; ++i)
    {
        asCObjectType* Type = Classes[i].Create(Context, Module); // ★ Stage1 先重建类型壳
        Context.ClassesLoadedFromPrecompiledData.Add(Type, &Classes[i]);
    }
}

void FAngelscriptPrecompiledModule::ApplyToModule_Stage2(FAngelscriptPrecompiledData& Context, asIScriptModule* InModule) const
{
    for (int32 i = 0, Count = Classes.Num(); i < Count; ++i)
        Classes[i].ProcessProperties(Context, ClassTypes[i]);   // ★ Stage2 补属性与函数对象
    for (int32 i = 0, Count = Functions.Num(); i < Count; ++i)
        GlobalFunctions[i] = Functions[i].Create(Context, Module);
}

void FAngelscriptPrecompiledModule::ApplyToModule_Stage3(FAngelscriptPrecompiledData& Context, asIScriptModule* InModule) const
{
    for (int32 i = 0, Count = Classes.Num(); i < Count; ++i)
        Classes[i].PreProcessFunctions(Context, ClassTypes[i]); // ★ Stage3 再做函数预处理与最终 patch
    for (int32 i = 0, Count = Functions.Num(); i < Count; ++i)
        Functions[i].Process(Context, GlobalFunctions[i]);
}
```

设计取舍：

- `puerts` 的优点是冷类型不触达就不付模板和 translator 成本，进程内交互更灵活；代价是首次触达某个冷类型时仍要临场建 wrapper。
- `Angelscript` 的优点是可以把脚本图、函数 id 和 `StaticJIT` 生成结果外推到构建产物，重复启动时更确定；代价是缓存强依赖 build 身份和脚本图一致性。

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 主要缓存寿命 | 当前进程内的 `FunctionTemplate` / translator / type template | 可落盘的 precompiled snapshot + `StaticJIT` 生成队列 | 实现方式不同 |
| 冷启动成本支付时机 | 某个 `UStruct` 首次触达时建立 wrapper | 启动前或构建期先生成 snapshot / JIT 产物，运行时分阶段恢复 | 实现方式不同 |
| 失效键 | 类型模板重建与函数描述刷新 | `BuildIdentifier` + 模块/类型/函数声明 hash | 实现方式不同 |

---

## 深化分析 (2026-04-09 00:50:23)

### [维度 D1] 插件架构与模块划分：`puerts` 的核心解耦点其实是“宿主契约注入”，不只是 backend 开关

这一轮补的是 `JsEnv` 对宿主暴露出来的正式构造 ABI。`puerts` 不只是在 `Build.cs` 里切 `V8 / QuickJS / Node.js`，它还把文件系统、日志、调试端口、source-loaded 回调，以及部分 backend 下的 runtime/context 句柄都做成了构造参数。结果是 Runtime 模块与 Editor 工具链可以复用同一套 VM 入口；宿主真正替换的不是“某个脚本文件路径”，而是一整层 host contract。

`Angelscript` 也有依赖注入，但边界明显更窄。`FAngelscriptEngineDependencies` 只注入脚本根发现所需的文件系统能力，`CreateCloneFrom()` 则共享内部 `SharedState`。这说明 Angelscript 的扩展点在“脚本目录与引擎生命周期”，而不在“让外部宿主提供脚本 VM 实例”。

```
[puerts] Host Contract Injection
host
├─ IJSModuleLoader / ILogger / debug port      // 文件系统、日志、调试都可替换
├─ source-loaded callback                      // Editor watcher 复用同一入口
└─ optional external runtime/context           // 仅特定 backend 接受外部 VM 句柄
   -> FJsEnv / FJsEnvGroup
      -> FJsEnvImpl

[Angelscript] Engine Dependency Injection
host
├─ GetProjectDir / path convert / mkdir        // 只注入脚本根发现所需能力
└─ GetEnabledPluginScriptRoots
   -> FAngelscriptEngine
      -> Full / Clone shared state             // 复用的是内部 AngelScript shared state
```

关键源码引用：

[1] `puerts` 把 loader / logger / external runtime 直接暴露在 `FJsEnv` 构造面上，而且 `FJsEnvGroup` 会把同一个 loader 扇出给多个 `JsEnv`。

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JsEnv.h
// 函数: FJsEnv::FJsEnv
// 位置: 66-68，宿主注入面
// ============================================================================
FJsEnv(std::shared_ptr<IJSModuleLoader> InModuleLoader, std::shared_ptr<ILogger> InLogger, int InDebugPort,
    std::function<void(const FString&)> InOnSourceLoadedCallback = nullptr, const FString InFlags = FString(),
    void* InExternalRuntime = nullptr, void* InExternalContext = nullptr); // ★ runtime/context 也是构造参数

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp
// 函数: FJsEnvImpl::FJsEnvImpl
// 位置: 374-407，真正消费宿主传入契约
// ============================================================================
ModuleLoader = std::move(InModuleLoader);
Logger = InLogger;
OnSourceLoadedCallback = InOnSourceLoadedCallback;

#ifdef WITH_QUICKJS
MainIsolate = InExternalRuntime ? v8::Isolate::New(InExternalRuntime) : v8::Isolate::New(CreateParams);
#else
check(!InExternalRuntime && !InExternalContext); // ★ V8 路径明确拒绝外部 runtime/context
MainIsolate = v8::Isolate::New(CreateParams);
#endif

#ifdef WITH_QUICKJS
v8::Local<v8::Context> Context =
    (InExternalRuntime && InExternalContext) ? v8::Context::New(Isolate, InExternalContext) : v8::Context::New(Isolate);
#else
v8::Local<v8::Context> Context = v8::Context::New(Isolate);
#endif

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvGroup.cpp
// 函数: FJsEnvGroup::FJsEnvGroup
// 位置: 105-114，共享同一个 loader 扇出多个 JsEnv
// ============================================================================
std::shared_ptr<IJSModuleLoader> SharedModuleLoader = std::move(InModuleLoader);
for (int i = 0; i < Size; i++)
{
    JsEnvList.push_back(std::make_shared<FJsEnvImpl>(SharedModuleLoader, InLogger, InDebugStartPort + i,
        InOnSourceLoadedCallback, InFlags, InExternalRuntime, InExternalContext)); // ★ host contract 被批量复用
}
```

[2] `Angelscript` 的注入点只到脚本根发现与 clone 生命周期，没有外部脚本 runtime 句柄位。

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h
// 位置: 86-95, 129-136，依赖注入面定义
// ============================================================================
struct ANGELSCRIPTRUNTIME_API FAngelscriptEngineDependencies
{
    TFunction<FString()> GetProjectDir;
    TFunction<FString(const FString&)> ConvertRelativePathToFull;
    TFunction<bool(const FString&)> DirectoryExists;
    TFunction<bool(const FString&, bool)> MakeDirectory;
    TFunction<TArray<FString>()> GetEnabledPluginScriptRoots; // ★ 注入的是脚本根发现能力
};

static TUniquePtr<FAngelscriptEngine> CreateCloneFrom(FAngelscriptEngine& Source, const FAngelscriptEngineConfig& InConfig);
static TUniquePtr<FAngelscriptEngine> CreateCloneFrom(FAngelscriptEngine& Source, const FAngelscriptEngineConfig& InConfig, const FAngelscriptEngineDependencies& InDependencies);
static TUniquePtr<FAngelscriptEngine> CreateForTesting(const FAngelscriptEngineConfig& InConfig, const FAngelscriptEngineDependencies& InDependencies, EAngelscriptEngineCreationMode Mode = EAngelscriptEngineCreationMode::Clone);

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: FAngelscriptEngine::DiscoverScriptRoots / FAngelscriptEngine::CreateForTesting
// 位置: 1328-1349, 654-666，消费注入依赖的方式
// ============================================================================
check(Dependencies.GetProjectDir);
check(Dependencies.ConvertRelativePathToFull);
check(Dependencies.DirectoryExists);
check(Dependencies.MakeDirectory);
check(Dependencies.GetEnabledPluginScriptRoots);

FString RootPath = Dependencies.ConvertRelativePathToFull(Dependencies.GetProjectDir() / TEXT("Script"));
for (const FString& PluginScriptRoot : Dependencies.GetEnabledPluginScriptRoots())
{
    const FString ScriptPath = Dependencies.ConvertRelativePathToFull(PluginScriptRoot);
    if (Dependencies.DirectoryExists(ScriptPath) && ScriptPath != RootPath)
    {
        DiscoveredRootPaths.Add(ScriptPath); // ★ 依赖注入只决定脚本根发现
    }
}

if (FAngelscriptEngine* CurrentEngine = TryGetCurrentEngine())
{
    return CreateCloneFrom(*CurrentEngine, InConfig, InDependencies); // ★ Clone 复用内部 shared state，而不是接收外部 VM
}
```

设计取舍：

- `puerts` 的好处是 Runtime 和 Editor 都能站在同一个 `FJsEnv`/`FJsEnvGroup` host contract 上复用 VM 入口，甚至允许 QuickJS 路径接入外部 runtime/context；代价是构造 ABI 更复杂，backend 条件分支直接进入运行时初始化代码。
- `Angelscript` 的好处是引擎拥有权非常明确，`Full/Clone` 生命周期和脚本根发现都在插件自己掌控内；代价是外部宿主无法像 `puerts` 那样把一个现成脚本 runtime 直接嵌进来。

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 宿主注入面 | `IJSModuleLoader` / `ILogger` / debug callback / optional external runtime+context | project dir / path convert / directory ops / plugin script roots | 实现方式不同 |
| 外部 VM 接入 | `WITH_QUICKJS` 路径允许 external runtime/context | `Clone` 只复用内部 `SharedState` | Angelscript 当前没有实现同等级外部脚本 VM 注入点 |
| Runtime/Editor 复用方式 | `PuertsModule.cpp:198-229` 与 `PuertsEditorModule.cpp:138-146` 都直接构造 `FJsEnv` 系列 | 编辑器工具链主要停留在原生 C++ 层 | 实现方式不同 |

### [维度 D6] 代码生成与 IDE 支持：`puerts` 把 Blueprint 声明做成“带 package version 的离线缓存”，`Angelscript` 当前主要导出 live schema

这一轮补的是 `ue_bp.d.ts` 的“增量缓存协议”。`puerts` 不是每次都把所有 Blueprint 重新扫一遍，而是把每个 Blueprint 类型声明包在 `TYPE_DECL_START/END` 标记里，顺手把 `PackageSavedHash` 或 `PackageGuid` 写进去。下一轮生成时，先从旧 `ue_bp.d.ts` 恢复缓存，再用 `AssetRegistry` 的 package version 决定哪些资产真要重生成功能声明。这个设计把“IDE 可用声明”当成一个长期维护的离线数据库，而不是一次性的导出文本。

`Angelscript` 当前 IDE 面更接近 live runtime snapshot。`DebugDatabase` 直接把当前已注册的类型表序列化给客户端，`AngelscriptDocs.cpp` 也只是从运行时已绑定的 `UClass/UFunction/FProperty` 回读 `ToolTip/Category`。它能提供准确的“当前真相”，但没有 `puerts` 这种按 package version 维护的离线 Blueprint 声明缓存层。

```
[puerts] Offline Blueprint Declaration Cache
old ue_bp.d.ts
 -> parse TYPE_DECL_START / TYPE_DECL_END
 -> restore package -> decl cache
 -> AssetRegistry package version compare
 -> regenerate only Changed packages
 -> write new ue_bp.d.ts with version markers

[Angelscript] Live IDE Export
runtime bound types
 -> DebugDatabase JSON
 -> Docs from ToolTip / Category metadata
 -> client consumes current live schema
```

关键源码引用：

[1] `puerts` 先把 package version 写进 `ue_bp.d.ts`，下次再从旧文件恢复并做增量判断。

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp
// 位置: 55-57, 63-65, 437-457，声明文件里的版本标记与落盘格式
// ============================================================================
#define TYPE_DECL_START "// __TYPE_DECL_START: "
#define TYPE_DECL_END "// __TYPE_DECL_END"
#define TYPE_ASSOCIATION "ASSOCIATION"

#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION > 5
#define GET_VERSION_ID(PD) LexToString(PD->GetPackageSavedHash()) // ★ UE5.6+ 直接吃 package saved hash
#else
#define GET_VERSION_ID(PD) PD->PackageGuid.ToString()
#endif

for (auto& KV : BlueprintTypeDeclInfoCache)
{
    if (KV.Value.IsExist)
    {
        for (auto& NameToDecl : KV.Value.NameToDecl)
        {
            Output << TYPE_DECL_START << (KV.Value.IsAssociation ? TYPE_ASSOCIATION : KV.Value.FileVersionString) << "\n";
            Output << NameToDecl.Value;
            Output << TYPE_DECL_END << "\n"; // ★ 每个 Blueprint 声明块都带版本信息
        }
    }
}

FFileHelper::SaveStringToFile(ToString(), *BPDeclarationFilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
```

[2] 下一轮生成前，`puerts` 会从旧 `ue_bp.d.ts` 恢复缓存，并只对 package version 变化的资产重新 `Gen()`。

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp
// 函数: FTypeScriptDeclarationGenerator::RestoreBlueprintTypeDeclInfos / GenTypeScriptDeclaration
// 位置: 568-623, 384-397, 674-681，恢复旧缓存并按 package version 做增量
// ============================================================================
FString FileVersionString = FileContent.Mid(Pos + Start.Len(), VersionInfoEnd - Pos - Start.Len());
const bool bIsAssociation = FileVersionString == TYPE_ASSOCIATION;
FString PackageName = FString(TEXT("/")) + Namespace.Replace(TEXT("."), TEXT("/"));

if (Matcher.FindNext())
{
    FName TypeName = *Matcher.GetCaptureGroup(1);
    auto BlueprintTypeDeclInfoPtr = BlueprintTypeDeclInfoCache.Find(*PackageName);
    if (BlueprintTypeDeclInfoPtr)
    {
        BlueprintTypeDeclInfoPtr->NameToDecl.Add(TypeName, TypeDecl); // ★ 先把旧声明恢复回内存缓存
    }
    else
    {
        TMap<FName, FString> NameToDecl;
        NameToDecl.Add(TypeName, TypeDecl);
        BlueprintTypeDeclInfoCache.Add(FName(*PackageName), {NameToDecl, FileVersionString, false, true, bIsAssociation});
    }
}

auto BlueprintTypeDeclInfoPtr = BlueprintTypeDeclInfoCache.Find(AssetData.PackageName);
if (PackageData && BlueprintTypeDeclInfoPtr)
{
    auto FileVersion = GET_VERSION_ID(PackageData);
    BlueprintTypeDeclInfoPtr->IsExist = true;
    BlueprintTypeDeclInfoPtr->Changed = InGenFull || (FileVersion != BlueprintTypeDeclInfoPtr->FileVersionString); // ★ 只有版本变了才重生声明
    BlueprintTypeDeclInfoPtr->FileVersionString = FileVersion;
}

if (BlueprintTypeDeclInfoPtr && BlueprintTypeDeclInfoPtr->Changed)
{
    auto Asset = AssetData.GetAsset();
    if (auto Blueprint = Cast<UBlueprint>(Asset))
    {
        if (Blueprint->Status != BS_Error && Blueprint->GeneratedClass)
        {
            Gen(Blueprint->GeneratedClass); // ★ 增量扫描真正发生在这里
        }
    }
}
```

[3] `Angelscript` 的 IDE 主数据当前仍来自 live runtime 导出，而不是带 package version 的离线声明缓存。

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 位置: 1499-1515，调试客户端拿到的是 live DebugDatabase
// ============================================================================
FAngelscriptDebugDatabaseSettings DebugSettings;
DebugSettings.bAutomaticImports = FAngelscriptEngine::Get().ShouldUseAutomaticImportMethod();
SendMessageToClient(Client, EDebugMessageType::DebugDatabaseSettings, DebugSettings);

auto JsonWriter = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&DB.Database);
FJsonSerializer::Serialize(Root, JsonWriter);
SendMessageToClient(Client, EDebugMessageType::DebugDatabase, DB); // ★ 直接序列化当前运行时类型表

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp
// 位置: 492-503, 533-539，文档文本同样从 live 反射元数据回读
// ============================================================================
FProperty* PropDesc = UnrealClass->FindPropertyByName(*Prop.Name);
if (PropDesc != nullptr)
{
    Prop.Documentation = PropDesc->GetMetaData("ToolTip");
    Prop.Category = PropDesc->GetMetaData("Category");
}
else
{
    UFunction* FuncDesc = UnrealClass->FindFunctionByName(*(TEXT("Get") + Prop.Name));
    if (FuncDesc != nullptr)
    {
        Prop.Documentation = GetFunctionTooltip(FuncDesc->GetMetaData("ToolTip"));
    }
}

ClassDoc.Documentation = Class->GetMetaData("ToolTip"); // ★ 文档来自已注册表面，不是离线缓存文件
```

设计取舍：

- `puerts` 的好处是大型项目里不必每次全量重扫 Blueprint 才能刷新 `ue_bp.d.ts`，package version 直接决定声明是否需要重建；代价是 `ue_bp.d.ts` 不只是产物文件，而是一个要被下一轮再解析的状态文件。
- `Angelscript` 的好处是 IDE/文档导出的数据与当前运行时已注册表面严格一致；代价是当前没有 `puerts` 这种“离线声明缓存 + package version 增量更新”层。

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| Blueprint IDE 数据源 | `ue_bp.d.ts` + `TYPE_DECL_START/END` + package version cache | `DebugDatabase` JSON + docs metadata | 实现方式不同 |
| 增量更新键 | `PackageSavedHash` / `PackageGuid` | 当前导出链未见 package-version 级离线缓存 | Angelscript 当前没有实现同等级离线声明缓存 |
| 导出时机 | 生成前先恢复旧缓存，再只重建 `Changed` 资产 | 运行时按当前已注册表面序列化 | 实现方式不同 |

### [维度 D11] 部署与打包：`puerts` 的可分发脚本格式包含 `Pak + .mbc/.cbc` 调试占位协议，当前 UE 插件范围内未见独立加密链

这一轮补的是“打包后脚本到底以什么形态继续被 runtime 消费”。`puerts` 的模块解析器把 `.js/.mjs/.cjs` 和 `.mbc/.cbc` 放在同一条搜索链里，`require()` 命中的依然是一个模块文件路径；如果文件来自 `Pak`，`modular.js` 还会把 `debugPath` 退回 `fullPath`。遇到 `.mbc/.cbc` 时，runtime 不会直接丢一个黑盒 bytecode 给 V8，而是先从 header 里读出源长度、修补 `FlagHash/ReadOnlySnapshotChecksum`，再合成等长空源码做 compile wrapper。也就是说，`puerts` 的 bytecode 包仍然维持了一份“可调试、可定位、可被 module loader 理解”的文件协议。

`Angelscript` 的 `PrecompiledScript.Cache` 不是这种运行时模块格式。它更像 build-bound snapshot：先记住 `BuildIdentifier`，再把模块、类型、函数身份序列化出来，启动时反序列化恢复。它减少的是脚本图重建成本，不承担 `require()` 式模块分发协议，也不负责提供 pak-aware debug path。

```
[puerts] Packaged Module Protocol
require()
 -> Search .js / .mjs / .cjs / .mbc / .cbc / package.json
 -> Load bytes
 -> if Pak debug path: fallback to fullPath
 -> if bytecode: patch header + synthesize empty source
 -> evalScript(debugPath, bytecode)

[Angelscript] Precompiled Snapshot
startup
 -> Load PrecompiledScript.Cache
 -> validate BuildIdentifier
 -> restore modules / types / function ids
 -> runtime executes restored script graph
```

关键源码引用：

[1] `puerts` 的 loader 把 `.mbc/.cbc` 当成一等模块文件，而不是额外的打包旁路。

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DefaultJSModuleLoader.cpp
// 函数: DefaultJSModuleLoader::SearchModuleInDir / Search
// 位置: 67-83, 92-120，模块搜索顺序直接包含 bytecode 与 package.json
// ============================================================================
bool IsJs = Extension == TEXT("js") || Extension == TEXT("mjs") || Extension == TEXT("cjs") || Extension == TEXT("json");
if (IsJs && SearchModuleWithExtInDir(Dir, RequiredModule, Path, AbsolutePath))
    return true;
return SearchModuleWithExtInDir(Dir, RequiredModule + ".js", Path, AbsolutePath) ||
       SearchModuleWithExtInDir(Dir, RequiredModule + ".mjs", Path, AbsolutePath) ||
       SearchModuleWithExtInDir(Dir, RequiredModule + ".cjs", Path, AbsolutePath) ||
#if defined(WITH_V8_BYTECODE)
       SearchModuleWithExtInDir(Dir, RequiredModule + ".mbc", Path, AbsolutePath) ||
       SearchModuleWithExtInDir(Dir, RequiredModule + ".cbc", Path, AbsolutePath) || // ★ bytecode 是正式模块扩展名
#endif
       SearchModuleWithExtInDir(Dir, RequiredModule / "package.json", Path, AbsolutePath) ||
       SearchModuleWithExtInDir(Dir, RequiredModule / "index.js", Path, AbsolutePath);

return SearchModuleInDir(FPaths::ProjectContentDir() / ScriptRoot, RequiredModule, Path, AbsolutePath) ||
       (ScriptRoot != TEXT("JavaScript") &&
           SearchModuleInDir(FPaths::ProjectContentDir() / TEXT("JavaScript"), RequiredModule, Path, AbsolutePath)); // ★ 打包后仍沿用同一搜索协议
```

[2] `modular.js` 与 `JsEnvImpl.cpp` 共同定义了 bytecode 包的运行时契约：`Pak` 路径退化、源长度占位、header 自修补。

```javascript
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js
// 位置: 63-67, 79-88, 134-154，Pak 路径与 bytecode 占位源码
// ============================================================================
let wrapped = evalScript(
    (isESM || bytecode) ? script : "(function (exports, require, module, __filename, __dirname) { " + script + "\n});",
    debugPath, isESM, fullPath, bytecode // ★ bytecode 模块也走同一套 evalScript 入口
);

let sourceHash = (new Uint32Array(buf))[2];
const kModuleFlagMask = (1 << 31);
const length = sourceHash & ~kModuleFlagMask; // ★ bytecode header 里藏着原始源码长度

let [fullPath, debugPath] = moduleInfo;
if (debugPath.startsWith("Pak: ")) {
    debugPath = fullPath; // ★ Pak 内模块无法直接映射外部调试路径时回退到 fullPath
}

let isESM = outerIsESM === true || fullPath.endsWith(".mjs") || fullPath.endsWith(".mbc");
if (fullPath.endsWith(".cjs") || fullPath.endsWith(".cbc")) isESM = false;
let bytecode = undefined;
if (fullPath.endsWith(".mbc") || fullPath.endsWith(".cbc")) {
    bytecode = script;
    script = generateEmptyCode(getSourceLengthFromBytecode(bytecode)); // ★ 生成等长空源码占位，保持调试/包装语义
}
```

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp
// 函数: FJsEnvImpl::CompileModule
// 位置: 333-341, 676-680, 3711-3737，bytecode header 校验与自修补
// ============================================================================
struct FCodeCacheHeader
{
    uint32_t MagicNumber;
    uint32_t VersionHash;
    uint32_t SourceHash;
    uint32_t FlagHash;
#if V8_MAJOR_VERSION >= 11
    uint32_t ReadOnlySnapshotChecksum;
#endif
};

auto CachedCode = v8::ScriptCompiler::CreateCodeCache(Script->GetUnboundScript());
const FCodeCacheHeader* CodeCacheHeader = (const FCodeCacheHeader*) CachedCode->data;
Expect_FlagHash = CodeCacheHeader->FlagHash; // ★ 先从当前 runtime 取到期望 header

if (FileName.EndsWith(TEXT(".mbc")))
{
    FCodeCacheHeader* CodeCacheHeader = (FCodeCacheHeader*) Data.GetData();
    if (CodeCacheHeader->FlagHash != Expect_FlagHash)
    {
        UE_LOG(Puerts, Warning, TEXT("FlagHash not match expect %u, but got %u"), Expect_FlagHash, CodeCacheHeader->FlagHash);
        CodeCacheHeader->FlagHash = Expect_FlagHash; // ★ 打包产物会按当前 runtime 修补 header
    }
#if V8_MAJOR_VERSION >= 11
    if (CodeCacheHeader->ReadOnlySnapshotChecksum != Expect_ReadOnlySnapshotChecksum)
    {
        CodeCacheHeader->ReadOnlySnapshotChecksum = Expect_ReadOnlySnapshotChecksum;
    }
#endif
    uint32_t Len = CodeCacheHeader->SourceHash & ~kModuleFlagMask;
    v8::Local<v8::Value> Args[] = {v8::Integer::New(Isolate, Len)};
    if (!GenEmptyCode.Get(Isolate)->Call(Context, v8::Undefined(Isolate), 1, Args).ToLocal(&Ret) || !Ret->IsString())
    {
        FV8Utils::ThrowException(MainIsolate, FString::Printf(TEXT("generate code for bytecode [%s] fail!"), *FileName));
        return v8::MaybeLocal<v8::Module>();
    }
    CachedCode = new v8::ScriptCompiler::CachedData(Data.GetData(), Data.Num());
    Options = v8::ScriptCompiler::CompileOptions::kConsumeCodeCache;
}
```

[3] `Angelscript` 的 `PrecompiledData` 依赖 build 身份与函数声明 hash，它优化的是恢复成本，不是运行时模块打包协议。

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp
// 位置: 2642-2660, 2666-2689, 2692-2720，build-bound snapshot
// ============================================================================
bool FAngelscriptPrecompiledData::IsValidForCurrentBuild()
{
    return BuildIdentifier == GetCurrentBuildIdentifier() && BuildIdentifier != -1; // ★ 先过 build 身份校验
}

void FAngelscriptPrecompiledData::InitFromActiveScript()
{
    BuildIdentifier = GetCurrentBuildIdentifier();
    int32 ModuleCount = Engine->GetModuleCount();
    for (int32 i = 0; i < ModuleCount; ++i)
    {
        asCModule* Module = (asCModule*)Engine->GetModuleByIndex(i);
        FString ModuleName = Module->GetName();
        Modules.FindOrAdd(ModuleName).InitFrom(*this, Module); // ★ 落盘的是活动脚本图
    }
}

void FAngelscriptPrecompiledData::Save(const FString& Filename)
{
    TArray<uint8> Data;
    FMemoryWriter Writer(Data, true);
    Writer << *this;
    FFileHelper::SaveArrayToFile(Data, *Filename);
}

uint32 FAngelscriptPrecompiledData::CreateFunctionId(asIScriptFunction* Function)
{
    uint32 Id = 0;
    auto* ScriptModule = Function->GetModule();
    if (ScriptModule != nullptr)
    {
        Id = HashCombine(Id, FCrc::StrCrc_DEPRECATED((const ANSICHAR*)ScriptModule->GetName()));
        Id = HashCombine(Id, (uint32)(size_t)ScriptModule->GetUserData());
    }
    Id = HashCombine(Id, FCrc::StrCrc_DEPRECATED((const ANSICHAR*)Function->GetDeclaration(true, true))); // ★ 身份来自模块+声明，不是分发模块文件格式
    return Id;
}
```

补充观察：

- 本轮对 `Reference/puerts/unreal/Puerts/Source/JsEnv`、`Source/Puerts`、`Source/DeclarationGenerator`、`Content/JavaScript/puerts` 以 `Encrypt|Encryption|AES|Aes|Cipher|Signing|RSA` 检索无命中，因此在当前 UE 插件分析范围内，没有看到独立的脚本加密/签名链；现有保护更接近 `pak + bytecode + native libs staging`，而不是密码学保护。

设计取舍：

- `puerts` 的好处是打包后的脚本仍然服从统一 module protocol，`Pak`、`package.json`、`.mbc/.cbc`、调试路径和 bytecode 占位源码都在同一条运行时链路上；代价是字节码兼容性明显受 backend 与 V8 header 版本约束。
- `Angelscript` 的好处是 `PrecompiledScript.Cache` 更像内部构建缓存，失效条件和恢复语义都更确定；代价是它不承担 `puerts` 那种“运行时模块格式 + 调试占位协议”的职责。

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 分发脚本格式 | `.js/.mjs/.cjs/.mbc/.cbc` + `package.json` + `Pak` 路径协议 | `PrecompiledScript.Cache` + script graph restore | 实现方式不同 |
| 兼容性修补点 | runtime 会修补 V8 `FlagHash` / `ReadOnlySnapshotChecksum` | runtime 先校验 `BuildIdentifier`，再按函数声明 hash 恢复 | 实现方式不同 |
| 调试路径契约 | `Pak:` 路径会回退到 `fullPath`，bytecode 还保留等长源码占位 | 当前 precompiled 路线不承担 pak-aware debug path | Angelscript 当前没有实现同等级运行时模块调试协议 |
| 加密/签名 | 当前 UE 插件范围内未见独立加密/签名链 | 当前引用到的 `PrecompiledData` 也不是加密机制 | 两边都没有把这个维度实现为密码学保护，主要是实现方式不同 |

---

## 深化分析 (2026-04-09 01:02:05)

### [维度 D2] 反射绑定机制：`puerts` 的字段 wrapper 是“弱字段 + outer link”的自修复外壳，`Angelscript` 更依赖 reload 分级而不是访问期修补

前面的 D2 已经覆盖 `DefineClass<T>()`、translator 工厂和 extension methods。这一轮补的是 **字段/容器 binding 在底层 `FProperty` 发生漂移时如何继续存活**。`puerts` 的 `FPropertyTranslator` 并不是一次性构造后永不更新：它保存 `TWeakFieldPtr`，每次 getter / setter / container access 都先过 `IsPropertyValid()`；如果 Editor 场景里 `FProperty*` 已经换地址，但弱引用仍有效，就直接 `Init(TestP)` 在原 translator 上刷新。对返回“内部引用”的场景，它还会通过 `NeedLinkOuter` + `LinkOuterImpl()` 把外层容器/struct 塞回 JS 对象，尽量避免内部子对象脱离 owning outer。

`Angelscript` 的处理点更靠前。`Bind_UStruct.cpp` 在生成 getter / setter 时直接把 `Property->GetOffset_ForUFunction()` 或原始 `FProperty*` 烧进绑定；如果 class property 的类型、定义或元数据和旧版本不一致，`AngelscriptClassGenerator` 会把类升级成 `FullReloadRequired` / `FullReloadSuggested`，随后走类级别重建，而不是让单个 accessor 在访问时自修补。

```
[puerts] Property Wrapper Survival
UE field access
 -> IsPropertyValid()
 -> if weak field moved: Init(new field)
 -> UEToJs / JsToUE
 -> if inner ref/container: LinkOuterImpl()
 -> JS keeps same wrapper object

[Angelscript] Property Binding Survival
bind generation
 -> capture Offset / FProperty pointer
 -> runtime direct bind helper
 -> if property shape changed
 -> classify FullReloadSuggested / Required
 -> rebuild class/module boundary
```

[1] `puerts` 在 translator 本体里同时维护“字段有效性”和“inner value 挂回 outer”的语义；容器 wrapper 每次访问都先复查这套状态。

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.h
// 函数: FPropertyTranslator::Init / FPropertyTranslator::IsPropertyValid
// 位置: 110-125, 179-193
// ============================================================================
FORCEINLINE void Init(PropertyMacro* InProperty)
{
    Property = InProperty;
    PropertyWeakPtr = InProperty;       // ★ 不直接相信原始 FProperty* 永远稳定
    OwnerIsClass = InProperty->GetOwnerClass() != nullptr;
    NeedLinkOuter = false;
    if (!OwnerIsClass)
    {
        if ((InProperty->IsA<StructPropertyMacro>() && StructProperty->Struct != FArrayBuffer::StaticStruct() &&
                StructProperty->Struct != FArrayBufferValue::StaticStruct() &&
                StructProperty->Struct != FJsObject::StaticStruct()) ||
            InProperty->IsA<MapPropertyMacro>() || InProperty->IsA<ArrayPropertyMacro>() || InProperty->IsA<SetPropertyMacro>())
        {
            NeedLinkOuter = true;       // ★ 嵌套容器/struct 返回引用时，需要保住 outer 关系
        }
    }
}

bool IsPropertyValid()
{
    if (!PropertyWeakPtr.IsValid())
    {
        return false;                   // ★ 字段彻底失效，访问直接失败
    }
#if WITH_EDITOR
    FProperty* TestP = PropertyWeakPtr.Get();
    if (TestP != Property)
    {
        Init(TestP);                    // ★ Editor 下字段换地址时，原位刷新 translator
    }
#endif
    return true;
}
```

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/ContainerWrapper.cpp
// 函数: FScriptArrayWrapper::InternalGet
// 位置: 70-89
// ============================================================================
auto Inner = FV8Utils::GetPointerFast<FPropertyTranslator>(Info.Holder(), 1);
if (!Inner->IsPropertyValid())
{
    FV8Utils::ThrowException(Isolate, "item info is invalid!"); // ★ 访问前先验证字段是否仍有效
    return;
}

uint8* DataPtr = GetData(Self, GetSizeWithAlignment(Inner->Property), Index);
auto Ret = Inner->UEToJs(Isolate, Context, DataPtr, PassByPointer);
if (Inner->NeedLinkOuter && PassByPointer)
{
    LinkOuterImpl(Context, Info.Holder(), Ret); // ★ 内部引用挂回 outer，避免悬空子对象
}
Info.GetReturnValue().Set(Ret);
```

[2] `Angelscript` 侧 property bind 仍是“把 offset / FProperty 烧进绑定”，布局变化则交给 class generator 判定 reload 等级。

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UStruct.cpp
// 位置: 900-976，属性 getter/setter 直接固化 offset 或 FProperty
// ============================================================================
FProperty* Property = Struct->FindPropertyByName(*DBProp.UnrealPath);
if (Property == nullptr)
    continue;

if (DBProp.bGeneratedGetter)
{
    Binds.Method(Decl, FUNC_TRIVIAL(FAngelscriptBindHelpers::GetValueFromProperty),
        (void*)(SIZE_T)Property->GetOffset_ForUFunction()); // ★ getter 直接携带 property offset
    FAngelscriptBinds::PreviousBindPassScriptFunctionAsFirstParam();
}

if (DBProp.bGeneratedSetter)
{
    Binds.Method(Decl, FUNC_CUSTOMNATIVE(FAngelscriptBindHelpers::SetValueFromProperty,
        FAngelscriptBindHelpers::SetValueFromProperty_Native), (void*)Property); // ★ setter 可能直接带原始 FProperty*
    FAngelscriptBinds::PreviousBindPassScriptFunctionAsFirstParam();
}

if (DBProp.Declaration.Len() != 0)
{
    Binds.Property(DBProp.Declaration, (SIZE_T)Property->GetOffset_ForUFunction()); // ★ 普通属性同样注册 offset
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 位置: 1077-1136，属性变化时升级 reload 等级
// ============================================================================
if (ClassData.OldClass->SuperClass != ClassData.NewClass->SuperClass)
{
    if (ClassData.ReloadReq < EReloadRequirement::FullReloadRequired)
        ClassData.ReloadReq = EReloadRequirement::FullReloadRequired;
}

for (auto OldPropertyDesc : ClassData.OldClass->Properties)
{
    bool bFound = false;
    for (auto PropertyDesc : ClassData.NewClass->Properties)
    {
        if (PropertyDesc->PropertyName == OldPropertyDesc->PropertyName)
        {
            bFound = true;

            if (PropertyDesc->PropertyType != OldPropertyDesc->PropertyType)
            {
                ClassData.ReloadReq = EReloadRequirement::FullReloadRequired; // ★ 类型一变，不做 accessor 级修补
            }

            if (!PropertyDesc->IsDefinitionEquivalent(*OldPropertyDesc))
            {
                ClassData.ReloadReq = EReloadRequirement::FullReloadRequired; // ★ 定义一变，直接升级为全量重载
            }
        }
    }

    if (!bFound)
    {
        ClassData.ReloadReq = EReloadRequirement::FullReloadRequired;         // ★ 删除属性同样要求 full reload
    }
}
```

设计取舍：

- `puerts` 把成本放在访问期，换来的是字段指针漂移时旧 wrapper 仍有机会继续工作，适合 Editor 热重载和容器引用回传。
- `Angelscript` 把成本放在编译/重载分析期，运行期字段访问更短，但依赖 reload 分类提前把危险布局变化拦住。

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 字段漂移处理 | `TWeakFieldPtr` + `IsPropertyValid()` + `Init(TestP)` 原位刷新 | 绑定层固化 offset / `FProperty*`，变化由 reload 分级接管 | 实现方式不同 |
| 内部引用生命周期 | `NeedLinkOuter` + `LinkOuterImpl()` 把 inner value 重新挂回 outer | 当前证据集中在类级 reload / 引用失效保护，没有同等级 outer-link 机制 | Angelscript 当前没有实现同等级 inner-to-outer JS wrapper 关联 |
| 失败模式 | 访问时抛 `"Property is invalid!"` / `"item info is invalid!"` | 变更分析时升级 `FullReloadRequired` / `Suggested` | 实现方式不同 |

### [维度 D8] 性能与优化：`puerts` 把二进制桥接细分成 borrowed buffer 与 owning buffer，`Angelscript` 的快路径则围绕 JIT 调用与容器内存失效

前面的 D8 已经写过 `FastCall`、GC 和 `StaticJIT`。这一轮补的是 **“大块二进制数据跨边界时，谁负责拷贝、谁承担所有权”**。`puerts` 没把 `ArrayBuffer` 当成普通 struct：`FArrayBufferPropertyTranslator` 遇到 `bCopy == false` 时直接把 UE 侧内存暴露成 `v8::ArrayBuffer`，需要独立副本时再 `memcpy`；`FArrayBufferValuePropertyTranslator` 则始终把脚本侧 buffer 拷进 `TArray<uint8>`，形成拥有期明确的值语义。也就是说，它把“借用视图”和“拥有副本”拆成两个显式类型。

`Angelscript` 当前看到的热路径重心不一样。脚本函数侧，它通过 `UASFunction::OptimizedCall_*` 和 `jitFunction_Raw` 针对固定参数形状直走 raw JIT 或轻量 context；容器侧，它在 `TArray` / `TMap` 等操作里大量使用 `FMemory::Memcpy`，并在底层内存可能搬移时调用 `InvalidateReferencesToMemoryBlock(...)` 使旧引用失效。这里优化的是“脚本执行”和“UE 容器语义保持”，不是单独做一个跨语言二进制 buffer surface。

```
[puerts] Binary Buffer Bridge
UE memory
 -> FArrayBuffer (borrow or copy)
 -> if bCopy: memcpy
 -> else: NewArrayBuffer() shares pointer
 -> JS ArrayBuffer / ArrayBufferView

JS ArrayBufferView
 -> FArrayBufferValue
 -> copy into TArray<uint8>
 -> UE owns stable snapshot

[Angelscript] Native Hot Path
script call
 -> UASFunction::OptimizedCall_*
 -> raw JIT or prepared context

container mutation/copy
 -> FMemory::Memcpy / CopyValue
 -> InvalidateReferencesToMemoryBlock()
 -> keep UE container semantics coherent
```

[1] `puerts` 对 `ArrayBuffer` 和 `ArrayBufferValue` 分别实现“借用”与“拥有”两套编组语义。

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp
// 位置: 704-805，FArrayBuffer / FArrayBufferValue 的所有权语义
// ============================================================================
class FArrayBufferPropertyTranslator : public FPropertyWithDestructorReflection
{
    v8::Local<v8::Value> UEToJs(...) const override
    {
        FArrayBuffer* ArrayBuffer = static_cast<FArrayBuffer*>(Ptr);
        if (ArrayBuffer->bCopy)
        {
            v8::Local<v8::ArrayBuffer> Ab = v8::ArrayBuffer::New(Isolate, ArrayBuffer->Length);
            void* Buff = static_cast<char*>(DataTransfer::GetArrayBufferData(Ab));
            ::memcpy(Buff, ArrayBuffer->Data, ArrayBuffer->Length); // ★ 需要独立拥有权时才复制
            return Ab;
        }
        else
        {
            return DataTransfer::NewArrayBuffer(Context, ArrayBuffer->Data, ArrayBuffer->Length); // ★ 否则直接借用原始内存
        }
    }

    bool JsToUE(...) const override
    {
        FArrayBuffer ArrayBuffer;
        ArrayBuffer.Data = ...;
        ArrayBuffer.Length = ...;       // ★ 只记录指针和长度，不复制持有
        StructProperty->CopySingleValue(ValuePtr, &ArrayBuffer);
        return true;
    }
};

class FArrayBufferValuePropertyTranslator : public FPropertyWithDestructorReflection
{
    v8::Local<v8::Value> UEToJs(...) const override
    {
        FArrayBufferValue* ArrayBuffer = static_cast<FArrayBufferValue*>(Ptr);
        v8::Local<v8::ArrayBuffer> Ab = v8::ArrayBuffer::New(Isolate, ArrayBuffer->Data.Num());
        ::memcpy(DataTransfer::GetArrayBufferData(Ab), ArrayBuffer->Data.GetData(), ArrayBuffer->Data.Num());
        return Ab;                      // ★ 值语义始终输出副本
    }

    bool JsToUE(...) const override
    {
        if (Len > 0 && Data)
        {
            ArrayBuffer->Data.AddUninitialized(Len);
            ::memcpy(ArrayBuffer->Data.GetData(), Data, Len); // ★ 输入侧同样复制进拥有型 TArray<uint8>
        }
        else
        {
            ArrayBuffer->Data.Reset();
        }
        return true;
    }
};
```

[2] `Angelscript` 的快路径证据主要落在 `UASFunction` 与 UE 容器操作，而不是专门的 binary buffer translator。

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp
// 函数: UASFunction::OptimizedCall_ByteReturn / UASFunction::OptimizedCall_RefArg_ByteReturn
// 位置: 1561-1595, 1697-1727
// ============================================================================
if (JitFunction_Raw != nullptr)
{
    return MakeRawJITCall_ReturnValue<asBYTE>(Object, JitFunction_Raw); // ★ 命中 raw JIT 时直接走专门化调用
}

asCScriptFunction* RealFunction = ResolveScriptVirtual(this, Object);
if (RealFunction->jitFunction_Raw != nullptr)
{
    return MakeRawJITCall_ReturnValue<asBYTE>(Object, RealFunction->jitFunction_Raw);
}
else
{
    FAngelscriptGameThreadContext Context(Object, RealFunction->GetEngine());
    AS_PREPARE_CONTEXT_OR_RETURN_VALUE(Context, RealFunction, 0);
    Context->SetObject(Object);
    Context->Execute();                 // ★ 没有 JIT 时退回准备好的 AngelScript context
    return Context->GetReturnByte();
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp
// 位置: 663-700；Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp
// 位置: 1040-1065，容器路径强调 memcpy 与失效旧引用
// ============================================================================
#if AS_REFERENCE_DEBUGGING
InvalidateReferencesToArray(Arr, Ops);  // ★ 底层内存可能搬移时，先让旧引用失效
#endif

int32 AddIndex = Arr.Add(1, Ops->NumBytesPerElement, Ops->Alignment);
if (Ops->bNeedCopy)
    Ops->Type.CopyValue(Value, DestinationAddr);
else
    FMemory::Memcpy(DestinationAddr, Value, Ops->NumBytesPerElement); // ★ POD 元素直接 memcpy

#if AS_REFERENCE_DEBUGGING
InvalidateReferencesToMap(Map, Ops);    // ★ TMap 取值导出前同样先失效旧引用
#endif

if (Ops->bKeyNeedCopy)
    Ops->ValueType.CopyValue(ValuePtr, DestPtr);
else
    FMemory::Memcpy(DestPtr, ValuePtr, Ops->ValueSize);
```

设计取舍：

- `puerts` 的二进制桥接更显式，脚本作者可以在“借用原始内存”和“拿一份稳定副本”之间做选择；代价是需要额外维护 `FArrayBuffer` / `FArrayBufferValue` 两套语义。
- `Angelscript` 把性能预算更多投在脚本执行和容器一致性上，热路径更贴近 UE 原生调用，但当前证据里没有看到与 `ArrayBuffer` 对应的一等二进制缓冲 surface。

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 二进制桥接抽象 | `FArrayBuffer` 借用 + `FArrayBufferValue` 拥有 | 当前证据集中在 `UASFunction` 与 `TArray/TMap`，未见同等级专用 binary buffer 类型 | Angelscript 当前没有实现同等级一等二进制缓冲抽象 |
| 无拷贝路径 | `bCopy == false` 时 `NewArrayBuffer(Context, Data, Length)` 直接借用 | 容器路径主要是 `CopyValue` / `FMemory::Memcpy`，并用引用失效保护兜底 | 实现方式不同 |
| 主要优化焦点 | 脚本 VM 与原始 buffer 之间的所有权/复制边界 | 脚本函数执行专门化 + UE 容器内存语义 | 实现方式不同 |

### [维度 D6] 代码生成与 IDE 支持：`puerts` 用同一份“特殊互操作类型规则”同时驱动 runtime translator 与 `.d.ts`，`Angelscript` 的 authoring surface 更分散

前面的 D6 已经拆过 `ue.d.ts`、Blueprint 缓存和 semantic host。这一轮只补 **特殊互操作类型是否有统一规则源**。`puerts` 对 `ArrayBuffer` / `ArrayBufferValue` / `JsObject` 的处理非常集中：`DeclarationGenerator::Gen()` 会跳过这三个 UE struct 本体，不为它们生成独立类型声明；`GenTypeDecl()` 遇到对应 `StructProperty` 时直接把它们映射成 TS 的 `ArrayBuffer` 或 `object`；运行时 `PropertyTranslatorCreator` 又用同一组 struct 选择 `FArrayBufferPropertyTranslator`、`FArrayBufferValuePropertyTranslator`、`FJsObjectPropertyTranslator`。这意味着 authoring surface 和 runtime marshalling 共享一份“特殊类型真相”。

`Angelscript` 当前更像“分散注册 + live 回读”。`Bind_TArray.cpp` 之类的 `Bind_*` 文件各自 `Register(...)` / `RegisterTypeFinder(...)`，运行时先把类型挂进 `FAngelscriptType` 体系；`AngelscriptDocs.cpp` 再遍历脚本引擎里已经注册好的 object types，通过 `FindPropertyByName()` / `FindFunctionByName()` 把 `ToolTip` 与 `Category` 回填进文档对象。它当然能得到准确的当前类型表，但没有看到一个像 `puerts` 那样同时约束 runtime 与 IDE 表面的“特殊互操作类型白名单”。

```
[puerts] One Rule, Two Consumers
special UE structs
 -> PropertyTranslatorCreator
 -> runtime marshal semantics

special UE structs
 -> DeclarationGenerator::GenTypeDecl
 -> .d.ts names: ArrayBuffer / object

result
 -> authoring surface matches runtime surface

[Angelscript] Decentralized Authoring Surface
Bind_TArray / Bind_TMap / Bind_UStruct ...
 -> FAngelscriptType::Register / RegisterTypeFinder
 -> live script engine type table
 -> AngelscriptDocs walks live types + UE metadata
```

[1] `puerts` 的声明生成和 translator 选择都显式吃同一组特殊 struct。

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp
// 函数: FTypeScriptDeclarationGenerator::Gen / GenTypeDecl
// 位置: 691-697, 800-825
// ============================================================================
void FTypeScriptDeclarationGenerator::Gen(UObject* ToGen)
{
    if (ToGen->GetName().Equals(TEXT("ArrayBuffer")) || ToGen->GetName().Equals(TEXT("ArrayBufferValue")) ||
        ToGen->GetName().Equals(TEXT("JsObject")))
    {
        return;                         // ★ 不为特殊互操作类型单独生成 UE 声明实体
    }
}

else if (auto StructProperty = CastFieldMacro<StructPropertyMacro>(Property))
{
    if (StructProperty->Struct->GetName() == TEXT("JsObject"))
    {
        StringBuffer << "object";       // ★ authoring 面直接写成 TS object
    }
    else if (StructProperty->Struct->GetName() == TEXT("ArrayBuffer") ||
             StructProperty->Struct->GetName() == TEXT("ArrayBufferValue"))
    {
        StringBuffer << "ArrayBuffer";  // ★ 两种运行时语义在 authoring 面统一收口到 ArrayBuffer
    }
    else
    {
        StringBuffer << GetNameWithNamespace(StructProperty->Struct);
    }
}
```

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp
// 位置: 1289-1305，runtime translator 选择沿用同一组特殊类型规则
// ============================================================================
auto StructProperty = CastFieldMacro<StructPropertyMacro>(InProperty);
if (StructProperty->Struct == FArrayBuffer::StaticStruct())
{
    return Creator<FArrayBufferPropertyTranslator>::Do(InProperty, IgnoreOut, Ptr);
}
else if (StructProperty->Struct == FArrayBufferValue::StaticStruct())
{
    return Creator<FArrayBufferValuePropertyTranslator>::Do(InProperty, IgnoreOut, Ptr);
}
else if (StructProperty->Struct == FJsObject::StaticStruct())
{
    return Creator<FJsObjectPropertyTranslator>::Do(InProperty, IgnoreOut, Ptr);
}
else
{
    return Creator<FScriptStructPropertyTranslator>::Do(InProperty, IgnoreOut, Ptr);
}
```

[2] `Angelscript` 的类型表面当前来自分散注册与运行后回读，而不是一个同时供 runtime / IDE 共享的特殊类型规则表。

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp
// 位置: 1550-1565，类型发现由各个 Bind_*.cpp 分散注册
// ============================================================================
auto ArrayType = MakeShared<FAngelscriptArrayType>();
FAngelscriptType::Register(ArrayType);
FAngelscriptType::RegisterTypeFinder([ArrayType](FProperty* Property, FAngelscriptTypeUsage& Usage) -> bool
{
    FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property);
    if (ArrayProp == nullptr)
        return false;

    FAngelscriptTypeUsage InnerUsage = FAngelscriptTypeUsage::FromProperty(ArrayProp->Inner);
    if (!InnerUsage.IsValid())
        return false;

    Usage.Type = ArrayType;             // ★ 每种类型各自在 bind 文件里注册自己的发现规则
    Usage.SubTypes.Add(InnerUsage);
    return true;
});
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp
// 位置: 473-576，文档导出从 live script types 反查 UE 元数据
// ============================================================================
auto ResolveAccessors = [&](FDocClass& Class, UClass* UnrealClass = nullptr, const FString* StaticName = nullptr)
{
    for (auto& Elem : GetAccessors)
    {
        FDocProperty Prop;
        Prop.Name = Elem.Key;
        if (UnrealClass != nullptr)
        {
            FProperty* PropDesc = UnrealClass->FindPropertyByName(*Prop.Name);
            if (PropDesc != nullptr)
            {
                Prop.Documentation = PropDesc->GetMetaData("ToolTip");
                Prop.Category = PropDesc->GetMetaData("Category"); // ★ 文档来自运行后可见的 live 类型表
            }
        }
        Class.Properties.Add(Prop);
    }
};

int32 TypeCount = ScriptEngine->GetObjectTypeCount();
for (int32 TypeIndex = 0; TypeIndex < TypeCount; ++TypeIndex)
{
    auto* ScriptType = ScriptEngine->GetObjectTypeByIndex(TypeIndex); // ★ 先拿当前已注册脚本类型，再回填说明文本
}
```

设计取舍：

- `puerts` 的好处是特殊互操作类型只需要维护一份规则，就能同时保证 runtime 编组和 `.d.ts` 可写表面一致；代价是 generator 与 runtime 必须长期共享这组约定名。
- `Angelscript` 的好处是 live docs 总能反映当前真正注册成功的类型；代价是类型发现、文档生成和 bind surface 分散在多处，特殊互操作类型若要同时进 runtime 与 authoring，通常需要多点同步改动。

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 特殊互操作类型规则源 | `ArrayBuffer` / `ArrayBufferValue` / `JsObject` 同时驱动 translator 与 `.d.ts` | `RegisterTypeFinder(...)` 分散在各个 `Bind_*.cpp`，docs 再从 live types 回读 | Angelscript 当前没有实现同等级 shared special-type rule table |
| IDE 名称与 runtime 语义一致性 | `GenTypeDecl()` 与 `PropertyTranslatorCreator` 明确共用一组特殊类型分支 | 主要依赖 live script type table 与文档回填，不是同一套显式规则 | 实现方式不同 |
| 变更维护面 | 一处特殊类型规则会影响 authoring + runtime | 新类型通常要同时改 bind、type finder、docs/export 路径 | 实现质量差异 |

---

## 深化分析 (2026-04-09 06:38:54)

### [维度 D1] 插件架构与模块划分：`puerts` 把生成资产落进宿主 `/Game` package，`Angelscript` 维护插件自有 `/Script/Angelscript*` package 空间

前面几轮已经讲过模块数量、`JsEnv`/`DeclarationGenerator` 的拆分和运行时拥有者；这一轮只补 **插件与宿主工程的资产边界**。`puerts` 的 `UPEBlueprintAsset::LoadOrCreate()` 直接把 TypeScript 生成资产落到 `/Game` 路径，后续 `FTypeScriptCompilerContext::SpawnNewClass()` 也把 `UTypeScriptGeneratedClass` 放到 `Blueprint->GetOutermost()` 这个宿主 package 里。换句话说，puerts 的“脚本类落地”不是一个插件私有命名空间，而是把宿主项目 package 当正式产物空间来用。

Angelscript 则走了完全不同的边界策略。`FAngelscriptEngine::Initialize_AnyThread()` 在运行时主动创建 `/Script/Angelscript` 和 `/Script/AngelscriptAssets` 两个 `PKG_CompiledIn` package；编辑器侧 `UAngelscriptContentBrowserDataSource` 再从 `AssetsPackage` 枚举对象，并通过 payload 把它们投影给 Content Browser。也就是说，Angelscript 不是把脚本产物直接塞回 `/Game`，而是先建插件自有资产空间，再做编辑器可视化映射。

这和“有没有 Blueprint 资产”不是一回事，而是 **插件边界落在宿主 package，还是落在插件自有 package** 的架构分歧。

```
[puerts] Host-Owned Asset Boundary
TypeScript analyzer
 -> UPEBlueprintAsset::LoadOrCreate
 -> CreatePackage("/Game/...")              // 直接进入宿主内容空间
 -> FKismetEditorUtilities::CreateBlueprint
 -> UTypeScriptGeneratedClass in Blueprint->GetOutermost()

[Angelscript] Plugin-Owned Asset Boundary
FAngelscriptEngine::Initialize_AnyThread
 -> Create /Script/Angelscript              // 插件私有脚本类型包
 -> Create /Script/AngelscriptAssets        // 插件私有脚本资产包
 -> UAngelscriptContentBrowserDataSource
 -> project editor sees projected virtual items
```

[1] `puerts` 把 TS 生成 Blueprint 与 generated class 放在宿主 `/Game/...` package。

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp
// 函数: UPEBlueprintAsset::LoadOrCreate
// 位置: 87-160，生成资产直接进入宿主 package
// ============================================================================
FString PackageName = FString(TEXT("/Game" TS_BLUEPRINT_PATH)) / InPath / InName;

Blueprint = LoadObject<UBlueprint>(nullptr, *PackageName, nullptr, LOAD_NoWarn | LOAD_NoRedirects);
if (Blueprint)
{
    GeneratedClass = Blueprint->GeneratedClass;
    Package = Cast<UPackage>(Blueprint->GetOuter());   // ★ 已存在资产继续复用宿主 package
    ...
    return true;
}

if (IsPlaying())
{
    UE_LOG(PuertsEditorModule, Error, TEXT("create class[%s] in PIE mode is forbiden!"), *InName);
    return false;
}

Package = CreatePackage(*PackageName);                 // ★ 新资产同样创建在 /Game/... 路径
check(Package);

Blueprint = FKismetEditorUtilities::CreateBlueprint(
    ParentClass, Package, *InName, BlueprintType, BlueprintClass, BlueprintGeneratedClass, FName("PuertsAutoGen"));
if (Blueprint)
{
    FAssetRegistryModule::AssetCreated(Blueprint);     // ★ 按普通 UE 资产注册到宿主资产系统
}
```

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/TypeScriptCompilerContext.cpp
// 函数: FTypeScriptCompilerContext::SpawnNewClass
// 位置: 19-40，generated class 的 outer 继续跟随 Blueprint 外层 package
// ============================================================================
NewClass = FindObject<UTypeScriptGeneratedClass>(Blueprint->GetOutermost(), *NewClassName);

if (NewClass == NULL)
{
    NewClass =
        NewObject<UTypeScriptGeneratedClass>(Blueprint->GetOutermost(), FName(*NewClassName), RF_Public | RF_Transactional);
    // ★ 类对象直接生在 Blueprint 自己的 outer package 中，而不是插件私有包
}
else
{
    NewClass->ClassGeneratedBy = Blueprint;
    FBlueprintCompileReinstancer::Create(NewClass);
}
```

[2] `Angelscript` 先创建插件自有 package，再由编辑器数据源把其中对象投影到 Content Browser。

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: FAngelscriptEngine::Initialize_AnyThread
// 位置: 1372-1388，运行时先建立插件私有脚本 package
// ============================================================================
bUseEditorScripts = WITH_EDITOR
    && ((RuntimeConfig.bIsEditor && !RuntimeConfig.bRunningCommandlet) || bForcePreprocessEditorCode)
    && !bSimulateCooked;

AngelscriptPackage = NewObject<UPackage>(nullptr, FName(TEXT("/Script/Angelscript")), RF_Public | RF_Standalone | RF_MarkAsRootSet);
AngelscriptPackage->SetPackageFlags(PKG_CompiledIn);       // ★ 脚本类型空间独立于 /Game

AssetsPackage = NewObject<UPackage>(nullptr, FName(TEXT("/Script/AngelscriptAssets")), RF_Public | RF_Standalone | RF_MarkAsRootSet);
AssetsPackage->SetPackageFlags(PKG_CompiledIn);            // ★ 脚本资产空间同样独立出来
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp
// 位置: 78-115, 225-243，把插件私有资产包里的对象投影给编辑器
// ============================================================================
if (EnumHasAnyFlags(InFilter.ItemTypeFilter, EContentBrowserItemTypeFilter::IncludeFiles)
    && EnumHasAnyFlags(InFilter.ItemCategoryFilter, EContentBrowserItemCategoryFilter::IncludeAssets))
{
    TArray<UObject*> Assets;
    GetObjectsWithOuter(FAngelscriptEngine::Get().AssetsPackage, Assets); // ★ 编辑器侧从插件私有包拉对象

    for (UObject* Object : Assets)
    {
        ...
    }
}

if (InItem.GetOwnerDataSource() == this && InItem.IsFile())
{
    auto Payload = StaticCastSharedPtr<const FAngelscriptContentBrowserPayload>(InItem.GetPayload());
    OutPackagePath = *Payload->Path;                        // ★ 通过数据源 payload 把私有对象映射成可浏览项
    return true;
}
```

设计取舍：

- `puerts` 的好处是直接复用普通 UE 资产与 package 工作流，生成物天然进入项目内容树，保存/编译/资产注册都沿用现成路径。
- 代价是插件边界明显更宿主耦合，插件运行时与生成工具都默认“项目内容目录就是正式落地点”。
- `Angelscript` 的好处是插件自有命名空间更清晰，作为独立插件交付时边界更硬；代价是必须自己补 `AssetsPackage`、Content Browser data source 这类配套桥接层。

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 生成资产归属 | `CreatePackage("/Game/...")`，直接落宿主内容空间 | 先创建 `/Script/Angelscript` 与 `/Script/AngelscriptAssets` | 实现方式不同 |
| generated class outer | `Blueprint->GetOutermost()` | 插件自有 package + 运行时引擎拥有 | 实现方式不同 |
| 插件边界 | 当前路径未见等价插件私有脚本资产命名空间 | 自有 package 空间明确 | puerts 当前没有实现同等级插件私有资产边界 |
| 编辑器暴露方式 | 走普通资产注册与 Blueprint 编译链 | 走自定义 `ContentBrowserDataSource` 投影 | 实现方式不同 |

### [维度 D4] 热重载：`puerts` 的 UE C++ hot reload 钩子是“重建 env + 惰性再绑定”，`Angelscript` 的 reload 钩子是“收集变更对象图 + 显式 reinstance”

前面的 D4 主要集中在 `.js` 文本替换与 `.as` 软/全量重载；这一轮补 **原生 C++ / editor session 这一层的热重载接缝**。`puerts` 在 `StartupModule()` 里把 UE 的 `ReloadCompleteDelegate` / `OnHotReload()` 直接接到 `MakeSharedJsEnv()`。也就是说，native module reload 完成后，它的第一反应不是去修 Blueprints 或对象图，而是整块重建 `JsEnv`/`JsEnvGroup`。真正的类级修复延后到 `execLazyLoadCallJS` 第一次命中时，才通过 `NotifyRebind()` 把 `NeedReBind` 的 `UTypeScriptGeneratedClass` 真正接回去。

再往回收侧看，这条链路也明显偏向“脚本 env + generated class redirect”而不是“全对象图修复”。`EndPIE()` 会清 `JsEnv`、遍历所有 `UTypeScriptGeneratedClass` 取消函数重定向并恢复 `ClassConstructor`；但同文件里的完整停用路径 `Disable()` 才会同时 `JsEnv.Reset()` 和 `JsEnvGroup.Reset()`。这说明当前可见的 PIE teardown 主轴仍是单 `JsEnv` / `UTypeScriptGeneratedClass` 路线。

Angelscript 则把 reload 事件做成显式状态机。`FClassReloadHelper::Init()` 监听 `OnStructReload`、`OnClassReload`、`OnDelegateReload`、`OnFullReload`、`OnPostReload`；真正 full reload 时，`PerformReinstance()` 会扫描 Blueprint 依赖、替换 pin type、重构节点、排队重新编译，并通过 `UAngelscriptReferenceReplacementHelper::Serialize()` 修补已经打开的编辑器资产引用。收尾时 `OnPostReload` 再把 `ReloadState()` 整体清空。

```
[puerts] Native Hot Reload Edge
UE ReloadCompleteDelegate / OnHotReload
 -> MakeSharedJsEnv()                      // 先重建 VM 宿主
 -> JsEnv::RebindJs()
 -> generated class marks NeedReBind
 -> first execLazyLoadCallJS
 -> NotifyRebind()                        // 首次命中时才真正接回 TS 实现

PIE end
 -> JsEnv.Reset()
 -> CancelRedirection / restore ClassConstructor
 -> generated class path cleaned

[Angelscript] Reload Graph Repair
ClassGenerator On*Reload
 -> accumulate ReloadState
 -> OnFullReload -> PerformReinstance()
    -> replace BP pin types
    -> queue BP compile
    -> reinstance + fix open asset refs
 -> OnPostReload -> Reset ReloadState
```

[1] `puerts` 的 native hot reload 钩子本身只有“重建 env”，类级重绑延迟到调用边界。

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp
// 函数: FPuertsModule::StartupModule / MakeSharedJsEnv
// 位置: 185-243, 405-438，UE native hot reload 完成后直接重建 JsEnv
// ============================================================================
virtual void MakeSharedJsEnv() override
{
    const UPuertsSetting& Settings = *GetDefault<UPuertsSetting>();

    JsEnv.Reset();
    JsEnvGroup.Reset();                                  // ★ 真正重建时是整块重置 env 宿主

    NumberOfJsEnv = (Settings.NumberOfJsEnv > 1 && Settings.NumberOfJsEnv < 10) ? Settings.NumberOfJsEnv : 1;
    ...
    JsEnvGroup->RebindJs();
    ...
    JsEnv->RebindJs();
}

FCoreUObjectDelegates::ReloadCompleteDelegate.AddLambda(
    [&](EReloadCompleteReason)
    {
        if (Enabled)
        {
            MakeSharedJsEnv();                           // ★ UE C++ Hot Reload 钩子里未见 Blueprint 修复逻辑
        }
    });
```

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/TypeScriptGeneratedClass.cpp
// 函数: UTypeScriptGeneratedClass::execLazyLoadCallJS / NotifyRebind
// 位置: 57-73, 77-99，真正重绑延后到首次命中函数时发生
// ============================================================================
auto Class = Cast<UTypeScriptGeneratedClass>(Function->GetOuterUClassUnchecked());
#if !WITH_EDITOR
    auto PinedDynamicInvoker = Class->DynamicInvoker.Pin();
    if (PinedDynamicInvoker)
    {
        PinedDynamicInvoker->NotifyReBind(Class);
    }
#else
    NotifyRebind(Context ? Context->GetClass() : Class); // ★ 编辑器态也只是走惰性重绑入口
#endif
Class->RestoreNativeFunc();
execCallJS(Context, Stack, RESULT_PARAM);

if (TsClass->NeedReBind && TsClass->DynamicInvoker.IsValid())
{
    TsClass->NeedReBind = false;
    ...
    CachedClass->DynamicInvoker.Pin()->NotifyReBind(CachedClass); // ★ 首次真正调用时才刷新绑定
}
```

[2] `puerts` 的 PIE teardown 主要清 generated-class redirect 路径；`Disable()` 才是完整 env 关闭路径。

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp
// 函数: FPuertsModule::EndPIE / Disable
// 位置: 316-349, 472-477，PIE 结束与彻底停用不是同一条清理链
// ============================================================================
void FPuertsModule::EndPIE(bool bIsSimulating)
{
    bIsInPIE = false;
    if (Enabled)
    {
        JsEnv.Reset();                                   // ★ PIE 结束只显式清单实例 JsEnv 成员
        for (TObjectIterator<UClass> It; It; ++It)
        {
            if (auto TsClass = Cast<UTypeScriptGeneratedClass>(Class))
            {
                TsClass->CancelRedirection();
                TsClass->DynamicInvoker.Reset();         // ★ 重点在恢复 generated class 重定向状态
            }
            ...
        }
    }
}

void FPuertsModule::Disable()
{
    Enabled = false;
    JsEnv.Reset();
    JsEnvGroup.Reset();                                  // ★ 完整停用路径才同时回收 group mode
}
```

[3] `Angelscript` 的 reload 事件会显式修复 Blueprint 依赖、打开中的资产引用，并在后处理阶段重置累积状态。

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h
// 位置: 50-176，reload 不是单点回调，而是一组显式阶段事件
// ============================================================================
FAngelscriptClassGenerator::OnClassReload.AddLambda(
[](UClass* OldClass, UClass* NewClass)
{
    if (OldClass != nullptr)
        ReloadState().ReloadClasses.Add(OldClass, NewClass); // ★ 先累积替换关系
    else
        ReloadState().NewClasses.Add(NewClass);
    ...
});

FAngelscriptClassGenerator::OnFullReload.AddLambda(
[]()
{
    ReloadState().PerformReinstance();                   // ★ full reload 时显式进 reinstance
});

FAngelscriptClassGenerator::OnPostReload.AddLambda(
[](bool bFullReload)
{
    ...
    ReloadState() = FReloadState();                      // ★ 收尾统一清空中间状态
});
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp
// 函数: FClassReloadHelper::FReloadState::PerformReinstance / UAngelscriptReferenceReplacementHelper::Serialize
// 位置: 27-110, 181-299, 409-438，显式修复 Blueprint 图与打开中的资产引用
// ============================================================================
for (TObjectIterator<UBlueprint> BlueprintIt; BlueprintIt; ++BlueprintIt)
{
    UBlueprint* BP = *BlueprintIt;
    const bool bHasDependency = AngelscriptEditor::BlueprintImpact::AnalyzeLoadedBlueprint(*BP, ImpactSymbols, ImpactReasons);
    ...
}

FBlueprintCompilationManager::ReparentHierarchies(ReloadClasses);
CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, true);

for (UBlueprint* BP : DependencyBPs)
{
    RefreshRelevantNodesInBP(BP);                        // ★ 受影响节点先重构
    FBlueprintCompilationManager::QueueForCompilation(BP);
}

FBlueprintCompilationManager::FlushCompilationQueueAndReinstance(); // ★ 然后统一重编译并 reinstance

UAssetEditorSubsystem* SubSystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
for (UObject* OriginalAsset : OpenAssets)
{
    UObject* ReplacedAsset = OriginalAsset;
    UnderlyingArchive << ReplacedAsset;
    if (ReplacedAsset != OriginalAsset)
    {
        SubSystem->NotifyAssetClosed(OriginalAsset, EditorInstance);
        SubSystem->NotifyAssetOpened(ReplacedAsset, EditorInstance); // ★ 打开的编辑器资产也跟着替换引用
    }
}
```

设计取舍：

- `puerts` 的好处是 native hot reload 钩子非常薄，重建 `JsEnv` 后把最终修复延迟到真正调用时，避免立刻扫描整张对象图。
- 代价是当前可见钩子本身没有等价的 Blueprint 依赖收敛与打开资产替换链，editor session 的一致性更多依赖 generated-class 惰性重绑路径。
- `Angelscript` 的好处是 reload 后编辑器对象图修复更显式、更闭环；代价是 reload 管线更重，事件、状态和 Blueprint 重编译步骤明显更多。

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| native hot reload 入口 | `ReloadCompleteDelegate` / `OnHotReload()` -> `MakeSharedJsEnv()` | `On*Reload` 事件族 -> `PerformReinstance()` | 实现方式不同 |
| 真正修复时机 | `execLazyLoadCallJS` 首次命中时 `NotifyRebind()` | reload 阶段立即刷新 BP 节点、排队编译、reinstance | 实现方式不同 |
| PIE / editor 清理模型 | `EndPIE()` 主轴是 `JsEnv` + generated class redirect 清理 | `OnPostReload` 明确重置 `ReloadState`，并修复打开中的资产引用 | Angelscript 在 editor object-graph 恢复链上实现质量更完整 |
| 多 env 回收显式性 | 当前 `EndPIE()` 未见与 `Disable()` 对等的 `JsEnvGroup.Reset()` | reload 状态收尾路径显式统一 | puerts 当前在 PIE teardown 上没有实现同等级显式 group-mode 收尾链 |

### [维度 D11] 部署与打包：`puerts` 的多后端发布图是按平台展开的 receipt/staging 脚本，`Angelscript` 的 runtime 构建图仍是单引擎源码内嵌

前面的 D11 已经把 loose script、`.mbc/.cbc`、`PrecompiledScript.Cache` 讲得很细；这一轮不重复运行时文件协议，只补 **构建系统怎么把这些后端真正带到各平台**。`puerts` 的 `JsEnv.Build.cs` 不是简单切个 `WITH_NODEJS`/`WITH_QUICKJS` 宏，而是按平台展开成不同的 native runtime 物料：Win64 `libnode.dll` / `msys-quickjs.dll` 走 `RuntimeDependencies(..., StagedFileType.NonUFS)`，Android Node.js 带一串 `libnode.a/libuv.a/libv8_*.a`，macOS 还区分 `macOS_arm64`，iOS QuickJS 要额外拉 `WebKit` framework；仓库里甚至放了 `Libnode_APL.xml` 把 `libnode.so` copy 进 APK。

更关键的是，`puerts` 在同一份 `Build.cs` 里还会把插件自带 `Content/` 和 `Typing/` 直接复制到宿主工程根下的 `Content` / `Typing`。这说明它的发布单元并不止“一个 UE plugin binary”，而是 **backend native libs + JS/TS 资源 + 宿主工程侧目录物化** 的组合体。

Angelscript 这边则简单得多。`AngelscriptRuntime.Build.cs` 只把 `ThirdParty/angelscript/source` 加入 include path，再声明 UE 模块依赖；当前文件里没有 `RuntimeDependencies`、`AdditionalPropertiesForReceipt`、APL、也没有平台分支去 stage 另一个脚本 VM。它的多平台成本主要交给同一份 C++ 源码和 UE 编译系统，而不是额外分发不同 backend runtime。

```
[puerts] Multi-Backend Shipping Graph
Build.cs
├─ choose Node.js / QuickJS / V8
├─ copy Content -> host project Content
├─ copy Typing -> host project Typing
├─ stage Win64 dll as NonUFS
├─ add Android .a / APL copy rules
├─ choose macOS_arm64 / iOS frameworks
└─ output = plugin + VM binaries + JS/TS resources

[Angelscript] Embedded Runtime Graph
AngelscriptRuntime.Build.cs
├─ include ThirdParty/angelscript/source
├─ declare UE module dependencies
└─ output = module compiled with embedded engine source
```

[1] `puerts` 的 build 图会直接选择 backend，并把 `Content`/`Typing` 复制到宿主工程，再用 `RuntimeDependencies` stage runtime 二进制。

```csharp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs
// 位置: 149-176, 360-367，backend 选择、宿主目录物化与 NonUFS staging
// ============================================================================
if (UseNodejs)
{
    ThirdPartyNodejs(Target);                           // ★ Node.js 后端
}
else if (UseQuickjs)
{
    ForceStaticLibInEditor = true;
    ThirdPartyQJS(Target);                             // ★ QuickJS 后端
}
else if (UseV8Version > SupportedV8Versions.VDeprecated)
{
    ThirdParty(Target);                                // ★ V8 后端
}

if (WithFFI) AddFFI(Target);

string coreJSPath = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", "Content"));
string destDirName = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", "..", "..", "Content"));
DirectoryCopy(coreJSPath, destDirName, true);          // ★ 构建期把 JS 资源物化到宿主工程 Content

string srcDtsDirName = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", "Typing"));
string dstDtsDirName = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", "..", "..", "Typing"));
DirectoryCopy(srcDtsDirName, dstDtsDirName, true);     // ★ Typing 同样复制到宿主工程

void AddRuntimeDependencies(string[] DllNames, string LibraryPath, bool Delay)
{
    ...
    RuntimeDependencies.Add(DestDllPath, DllPath, StagedFileType.NonUFS); // ★ runtime 二进制明确走 NonUFS stage
}
```

[2] `Node.js` 与 `QuickJS` 的平台分发不是抽象描述，而是真实展开成不同平台库、framework 和 receipt 规则。

```csharp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs
// 位置: 502-621, 627-715，按平台展开 Node.js / QuickJS 物料
// ============================================================================
if (Target.Platform == UnrealTargetPlatform.Win64)
{
    PublicAdditionalLibraries.Add(Path.Combine(V8LibraryPath, "libnode.lib"));
    RuntimeDependencies.Add("$(TargetOutputDir)/libnode.dll", Path.Combine(V8LibraryPath, "libnode.dll"));
    AddRuntimeDependencies(new string[] { "libnode.dll" }, V8LibraryPath, false); // ★ Win64 带 dll
}
else if (Target.Platform == UnrealTargetPlatform.Android)
{
    foreach (var Arch in Archs)
    {
        PublicAdditionalLibraries.Add(Path.Combine(V8LibraryPath, "libnode.a"));
        PublicAdditionalLibraries.Add(Path.Combine(V8LibraryPath, "libuv.a"));
        PublicAdditionalLibraries.Add(Path.Combine(V8LibraryPath, "libv8_snapshot.a"));
        ...                                                     // ★ Android 展开成一串静态库
    }
}
else if (Target.Platform == UnrealTargetPlatform.Mac)
{
    if (Target.Architecture == UnrealArch.Arm64)
    {
        V8LibraryPath = Path.Combine(LibraryPath, "macOS_arm64"); // ★ macOS 区分 arm64
    }
    PublicAdditionalLibraries.Add(Path.Combine(V8LibraryPath, "libnode.93.dylib"));
}
else if (Target.Platform == UnrealTargetPlatform.IOS)
{
    PublicAdditionalLibraries.Add(Path.Combine(V8LibraryPath, "libnode.a"));
    PublicAdditionalLibraries.Add(Path.Combine(V8LibraryPath, "libopenssl.a"));   // ★ iOS 继续拉额外静态库
}

PrivateDefinitions.Add("WITH_QUICKJS");
...
AddRuntimeDependencies(new string[] { "msys-quickjs.dll" }, V8LibraryPath, false);
AddRuntimeDependencies(new string[]
{
    "libgcc_s_seh-1.dll",
    "libwinpthread-1.dll"
}, V8LibraryPath, true);                                       // ★ QuickJS 还要带 MinGW 运行库

PublicFrameworks.AddRange(new string[] { "WebKit" });
PublicAdditionalLibraries.Add(Path.Combine(V8LibraryPath, "libquickjs.a"));       // ★ iOS QuickJS 还依赖 framework
```

```xml
<!-- =========================================================================
文件: Reference/puerts/unreal/Puerts/ThirdParty/Libnode_APL.xml
位置: 1-15，Android receipt 额外把 libnode.so 拷进 APK
=========================================================================== -->
<resourceCopies>
    <copyFile src="$S(PluginDir)/nodejs_16/Lib/Android/$S(Architecture)/libnode.so"
        dst="$S(BuildDir)/libs/$S(Architecture)/libnode.so" />
    <!-- ★ APL 说明 Android 发布不是“编进去就完”，还需要额外 copy so -->
</resourceCopies>
```

[3] `Angelscript` 当前 runtime 构建图没有等价的 backend runtime staging / receipt 分支。

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs
// 位置: 15-79，单引擎源码内嵌 + UE 依赖声明
// ============================================================================
PublicIncludePaths.Add(ModuleDirectory);
...
var AngelscriptThirdPartyPath = Path.Combine(ModuleDirectory, "ThirdParty", "angelscript");
PublicIncludePaths.Add(Path.Combine(AngelscriptThirdPartyPath, "source"));
PublicIncludePaths.Add(AngelscriptThirdPartyPath);             // ★ 直接嵌入 AngelScript 源码

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
    ...
});
// ★ 当前文件未见 RuntimeDependencies / APL / AdditionalPropertiesForReceipt / backend 分发分支
```

设计取舍：

- `puerts` 的好处是同一插件能在 V8 / QuickJS / Node.js 与多平台之间做明确组合，发布时可以把 backend 物料、JS 资源、Typing 资源一起编排。
- 代价是构建图和交付物都更复杂，平台分支、额外 runtime 库、宿主工程目录复制、APL/receipt 都会成为部署变量。
- `Angelscript` 的好处是发布面更收敛，主 runtime 仍是一套编进模块的 C++ 源码；代价是没有 `puerts` 这种“换一个 backend 一起随包发布”的分发轴。

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 后端分发轴 | `UseNodejs` / `UseQuickjs` / `UseV8Version` + 平台分支 | 当前主链只有单一 AngelScript 引擎源码 | Angelscript 没有实现 |
| 平台 receipt / staging | `RuntimeDependencies`、APL、framework、arch-specific libs | 当前文件未见等价 receipt / staging 分支 | Angelscript 没有实现 |
| 宿主工程物化 | 构建期 `DirectoryCopy(Content/Typing -> host project)` | 当前 `Build.cs` 未见等价宿主目录复制 | 实现方式不同 |
| 交付复杂度 | plugin + VM binaries + JS/TS 资源 + receipt 规则 | plugin module + embedded engine source | 实现方式不同 |

---

## 深化分析 (2026-04-09 06:50:51)

### [维度 D1] 插件架构：`puerts` 额外拆出 `JsEnvModule` 作为进程级 VM bootstrap 层，`PuertsModule` 只保留运行时拓扑装配

前面的 D1 已经把 `FJsEnvGroup`、`pesapi` 和多 backend 开关讲得比较充分；这一轮补的是 **模块职责到底切在什么边界**。`puerts` 并不是把“多后端”全塞进 `PuertsModule`，而是先在 `JsEnvModule` 里完成进程级 VM 引导，再把“当前场景起几个 env、是否等待调试器、何时 rebind”留给 `PuertsModule`。源码证据很直接：`FJsEnvModule::StartupModule()` 负责 zero-sized allocation 探测、旧版 UE 的 `GMalloc` 兼容补丁、V8/Node platform 创建和全局 flag 设置；`FPuertsModule::MakeSharedJsEnv()` 则只读取 `UPuertsSetting` 并在 `FJsEnv` / `FJsEnvGroup` 间二选一。

Angelscript 的切法更收敛。当前主路径里没有等价的独立 “VM bootstrap module”；`FAngelscriptEngineConfig::FromCurrentProcess()` 先吸收命令行配置，`FAngelscriptEngine::Initialize()` 接着完成 package 创建、`asEP_*` property 设置、`BindScriptTypes()` 和 `InitializeOwnedSharedState()`。也就是说，`puerts` 是“两层引导”，而 `Angelscript` 更接近“单层 bootstrap”。

```
[puerts] Bootstrap Ownership Split
Process Start
├─ JsEnvModule::StartupModule
│  ├─ probe new (std::nothrow) int[0]      // 先探测底层分配器兼容性
│  ├─ patch GMalloc on old UE              // 需要时接管全局 allocator 入口
│  ├─ create V8/Node platform              // 构建 VM 平台对象
│  └─ set V8 global flags                  // 注入 fast-call / bytecode 等全局 flag
└─ PuertsModule::MakeSharedJsEnv
   ├─ read UPuertsSetting                  // 读取宿主配置
   ├─ choose FJsEnv / FJsEnvGroup          // 只决定实例拓扑
   └─ WaitDebugger / RebindJs              // 做场景级收口

[Angelscript] Unified Engine Bootstrap
Process Start
└─ FAngelscriptEngine::FromCurrentProcess + Initialize
   ├─ parse command line config            // 解析运行期行为开关
   ├─ create packages                      // 建立 /Script/Angelscript 包空间
   ├─ set asEP_* properties                // 配置 AngelScript 引擎属性
   ├─ BindScriptTypes                      // 注册脚本可见类型
   └─ InitializeOwnedSharedState           // 初始化共享状态
```

[1] `JsEnvModule` 明确承担了进程级 VM 引导责任，而不是简单暴露一个 `IModuleInterface`。

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvModule.cpp
// 函数: FJsEnvModule::StartupModule / ShutdownModule
// 位置: 172-180, 185-228, 261-268
// ============================================================================
int* Dummy = new (std::nothrow) int[0];
if (!Dummy)
{
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION > 5
    UE_LOG(JsEnvModule, Error, TEXT("new (std::nothrow) int[0] return nullptr, try fix it!"));
#else
    MallocWrapper = new FMallocWrapper(GMalloc);
    GMalloc = MallocWrapper;                           // ★ 兼容补丁发生在模块启动期，而不是某个 env 构造期
#endif
}

#if defined(WITH_NODEJS)
platform_ = node::MultiIsolatePlatform::Create(4);    // ★ Node backend 的 platform 在这里初始化
#else
platform_ = v8::platform::NewDefaultPlatform();
#endif

#ifdef WITH_V8_FAST_CALL
v8::V8::SetFlagsFromString("--turbo-fast-api-calls"); // ★ 全局 V8 flag 也归 bootstrap 层管理
#endif

#if defined(V8_HAS_WRAP_API_WITHOUT_STL)
v8::V8::InitializePlatform(platform_);
#else
v8::V8::InitializePlatform(platform_.get());
#endif
v8::V8::Initialize();

#if ENGINE_MAJOR_VERSION < 5 || ENGINE_MINOR_VERSION <= 5
if (MallocWrapper && MallocWrapper == GMalloc)
{
    GMalloc = MallocWrapper->InnerMalloc;             // ★ 模块卸载时恢复全局 allocator
    delete MallocWrapper;
    MallocWrapper = nullptr;
}
#endif
```

[2] `PuertsModule` 的职责更窄，核心是“实例拓扑选择 + 场景级重绑”。

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp
// 函数: FPuertsModule::MakeSharedJsEnv
// 位置: 187-239
// ============================================================================
const UPuertsSetting& Settings = *GetDefault<UPuertsSetting>();

JsEnv.Reset();
JsEnvGroup.Reset();                                    // ★ 先清旧实例，再重新装配当前运行时

NumberOfJsEnv = (Settings.NumberOfJsEnv > 1 && Settings.NumberOfJsEnv < 10) ? Settings.NumberOfJsEnv : 1;

if (NumberOfJsEnv > 1)
{
    JsEnvGroup = MakeShared<PUERTS_NAMESPACE::FJsEnvGroup>(NumberOfJsEnv, Settings.RootPath);
    if (Settings.WaitDebugger)
    {
        UE_LOG(PuertsModule, Warning, TEXT("Do not support WaitDebugger in Group Mode!"));
    }
    JsEnvGroup->RebindJs();                            // ★ group mode 关注的是多 env 装配与重绑
}
else
{
    JsEnv = MakeShared<PUERTS_NAMESPACE::FJsEnv>(Settings.RootPath);
    if (Settings.WaitDebugger)
    {
        JsEnv->WaitDebugger(Settings.WaitDebuggerTimeout); // ★ 单实例模式才顺带阻塞等待调试器
    }
    JsEnv->RebindJs();
}
```

[3] `Angelscript` 把配置解析、引擎属性和 shared state 初始化压进同一条 bootstrap 链。

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: FAngelscriptEngineConfig::FromCurrentProcess / FAngelscriptEngine::Initialize
// 位置: 514-529, 876-920
// ============================================================================
FAngelscriptEngineConfig Config;
Config.bSimulateCooked = FParse::Param(FCommandLine::Get(), TEXT("as-simulate-cooked"));
Config.bGeneratePrecompiledData = FParse::Param(FCommandLine::Get(), TEXT("as-generate-precompiled-data"));
Config.bIgnorePrecompiledData = FParse::Param(FCommandLine::Get(), TEXT("as-ignore-precompiled-data"));
FParse::Value(FCommandLine::Get(), TEXT("-asdebugport="), Config.DebugServerPort); // ★ 运行期配置先集中解析

PreInitialize_GameThread();

AngelscriptPackage = NewObject<UPackage>(nullptr, FName(TEXT("/Script/Angelscript")), RF_Public | RF_Standalone | RF_MarkAsRootSet);
AssetsPackage = NewObject<UPackage>(nullptr, FName(TEXT("/Script/AngelscriptAssets")), RF_Public | RF_Standalone | RF_MarkAsRootSet);

Engine->SetEngineProperty(asEP_OPTIMIZE_BYTECODE, 1);
Engine->SetEngineProperty(asEP_AUTO_GARBAGE_COLLECT, 0);
Engine->SetEngineProperty(asEP_REQUIRE_ENUM_SCOPE, 1);        // ★ 引擎属性配置与 package 初始化在同一 bootstrap 内完成

Engine->SetMessageCallback(asFUNCTION(LogAngelscriptError), 0, asCALL_CDECL);
Engine->SetContextCallbacks(&AngelscriptRequestContext, &AngelscriptReturnContext, nullptr);
EnsureSharedStateCreated();
BindScriptTypes();
GameThreadTLD->primaryContext = CreateContext();
InitializeOwnedSharedState();                                 // ★ shared state 初始化没有再拆到独立模块
```

设计取舍：

- `puerts` 的收益是把“进程级 VM 兼容层”与“UE 运行时实例层”分离，便于在 `JsEnvModule` 集中处理 V8/Node platform、全局 flag、旧版 UE allocator 兼容。
- 代价是 bootstrap 责任分散到两个模块，定位启动期副作用时必须同时读 `JsEnvModule` 和 `PuertsModule`。
- `Angelscript` 的收益是引导路径集中，配置、引擎属性、共享状态初始化都在 `FAngelscriptEngine` 一处闭合；代价是没有 `puerts` 这种单独可替换的 VM bootstrap 层。

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| bootstrap 分层 | `JsEnvModule` 处理 VM 平台与全局 flag，`PuertsModule` 处理 env topology | `FAngelscriptEngine` 统一解析配置并完成引擎初始化 | 实现方式不同 |
| 进程级兼容补丁位置 | `StartupModule()` 内探测 `new int[0]` 并可替换 `GMalloc` | 当前检视源码未见等价独立 VM bootstrap 补丁层 | 当前检视范围内 Angelscript 没有实现 |
| 运行时实例装配 | `MakeSharedJsEnv()` 在单 env / group env 间切换 | 主链集中初始化单一 `FAngelscriptEngine` | 实现方式不同 |

### [维度 D5] 调试与开发体验：`puerts` 的暂停模型本质由 Inspector 驱动，`Angelscript` 的暂停模型由插件自定义协议状态机驱动

前面的 D5 已经写过 “V8 Inspector vs 自定义协议”；这一轮继续往 **暂停时系统到底怎么停住** 里钻。`FJsEnvImpl::WaitDebugger()` 的实现非常薄，只是循环调用 `Inspector->Tick()`；`V8InspectorClientImpl::runIfWaitingForDebugger()` 也只是把 `Connected = true` 回灌给 Inspector；真正进入 breakpoint pause 时，`runMessageLoopOnPause()` 仍然只是 `while (IsPaused) Tick()`。这说明 `puerts` 在 UE 插件层拥有的是“把 Inspector 生命周期接进宿主”的桥，而不是一套自己定义的 pause protocol。

Angelscript 则相反。`EDebugMessageType` 把 `Pause / Continue / StepIn / StepOver / StepOut / HasStopped / HasContinued` 明确列成协议枚举；`PauseExecution()` 会主动广播 `HasStopped`，然后在 `while (bIsPaused)` 中持续 `ProcessMessages()`；`HandleMessage()` 再显式改变 `bIsPaused`、`bBreakNextScriptLine` 和条件断点状态。暂停状态本身就是插件协议的一部分，而不是交给外部 Inspector runtime 隐式维持。

```
[puerts] Inspector-Driven Pause Loop
Frontend Connect
├─ runIfWaitingForDebugger()               // 只把 Connected 置位
├─ WaitDebugger()
│  └─ while (!Inspector->Tick())          // 等前端连接
└─ runMessageLoopOnPause()
   └─ while (IsPaused) Tick()             // 暂停时继续泵 Inspector 事件

[Angelscript] Protocol-Driven Pause Loop
Socket Message
├─ HandleMessage(Pause/Continue/Step*)    // 显式改写调试状态
├─ PauseExecution()
│  ├─ send HasStopped                     // 插件自己广播停止事件
│  └─ while (bIsPaused) ProcessMessages() // 暂停期间继续处理协议消息
└─ ProcessMessages()
   ├─ dispatch request/reply              // 请求、回包、断开、ping
   └─ flush CallStack while paused        // 暂停态还能发栈与变量
```

[1] `puerts` 的 “等待调试器 / 暂停循环” 都直接围绕 Inspector `Tick()` 展开。

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.h
// 函数: FJsEnvImpl::WaitDebugger
// 位置: 99-116
// ============================================================================
virtual void WaitDebugger(double timeout) override
{
#ifdef THREAD_SAFE
    v8::Locker Locker(MainIsolate);
#endif
    const auto startTime = FDateTime::Now();
    while (Inspector && !Inspector->Tick())           // ★ 本质是反复 pump Inspector，直到前端连上
    {
        if (timeout > 0)
        {
            auto now = FDateTime::Now();
            if ((now - startTime).GetTotalSeconds() >= timeout)
            {
                break;
            }
        }
    }
}
```

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp
// 函数: V8InspectorClientImpl::runIfWaitingForDebugger / Tick / runMessageLoopOnPause
// 位置: 217-220, 446-450, 572-590
// ============================================================================
void runIfWaitingForDebugger(int ContextGroupId) override
{
    Connected = true;                                 // ★ 前端接入语义被压缩成一个布尔状态
}

bool V8InspectorClientImpl::Tick()
{
    Tick(0);
    return IsAlive && Connected;                      // ★ Tick 的返回值就是 “Inspector 是否已连接”
}

void V8InspectorClientImpl::runMessageLoopOnPause(int /* ContextGroupId */)
{
    if (IsPaused)
    {
        return;
    }

    IsPaused = true;
    while (IsPaused)
    {
        Tick();                                       // ★ 暂停期没有插件自有协议分发，继续跑 Inspector loop
    }
}
```

[2] `Angelscript` 的暂停期由插件自己的消息枚举和状态机驱动。

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h
// 位置: 25-47, 597-614
// ============================================================================
enum class EDebugMessageType : uint8
{
    StartDebugging,
    StopDebugging,
    Pause,
    Continue,
    ...
    HasStopped,
    HasContinued,
    StepOver,
    StepIn,
    StepOut,
};

TAtomic<bool> bBreakNextScriptLine { false };
bool bIsPaused = false;
bool bIsDebugging = false;

void ProcessScriptLine(class asCContext* Context);
void ProcessException(class asIScriptContext* Context);
void PauseExecution(FStoppedMessage* StopMessage = nullptr);   // ★ 暂停是显式协议状态，不是 Inspector 内部细节
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 函数: FAngelscriptDebugServer::PauseExecution / ProcessMessages / HandleMessage
// 位置: 667-699, 812-817, 828-895, 897-907
// ============================================================================
void FAngelscriptDebugServer::PauseExecution(FStoppedMessage* StopMessage)
{
    bIsPaused = true;
    SendMessageToAll(EDebugMessageType::HasStopped, *StopMessage); // ★ 插件主动通知前端 “已经停住”

    while (bIsPaused)
    {
        ProcessMessages();                                       // ★ 暂停期间持续处理自定义调试协议
        FPlatformProcess::Sleep(0);
    }

    FEmptyMessage ContinueMsg;
    SendMessageToAll(EDebugMessageType::HasContinued, ContinueMsg);
}

if (bIsPaused && CallstackRequests.Num() > 0)
{
    for (auto* Socket : CallstackRequests)
        SendCallStack(Socket);                                   // ★ 暂停态还能继续处理 call stack 请求
}

else if (MessageType == EDebugMessageType::Pause)
{
    bBreakNextScriptLine = true;
    FAngelscriptEngine::Get().UpdateLineCallbackState();
}
else if (MessageType == EDebugMessageType::Continue)
{
    bIsPaused = false;
    bBreakNextScriptLine = false;
}
else if (MessageType == EDebugMessageType::StepOver)
{
    bBreakNextScriptLine = true;
    bIsPaused = false;
    ConditionBreakFrame = CallstackSize - 1;                    // ★ StepOver/StepOut 也是插件自己维护条件断点状态
}
else if (MessageType == EDebugMessageType::StartDebugging)
{
    bIsDebugging = true;
    SendMessageToClient(Client, EDebugMessageType::DebugServerVersion, DebugServerVersionMessage);
}
```

设计取舍：

- `puerts` 的收益是直接复用 Inspector 生态，插件层不必重造一套完整前端协议和断点消息格式。
- 代价是暂停语义、等待连接和 message loop 都绑定在 Inspector 生命周期上，插件自己的可扩展状态面较薄。
- `Angelscript` 的收益是 pause/continue/step/栈请求全都在自定义协议里显式可见；代价是调试服务实现和协议演进成本都由插件自己承担。

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 暂停循环拥有者 | `WaitDebugger()` 与 `runMessageLoopOnPause()` 都是 Inspector `Tick()` loop | `PauseExecution()` + `HandleMessage()` 组成插件自有状态机 | 实现方式不同 |
| 前端连接握手语义 | `runIfWaitingForDebugger()` 仅设置 `Connected = true` | `StartDebugging`、`DebugServerVersion`、`HasStopped`、`HasContinued` 都有显式消息类型 | Angelscript 实现质量更完整 |
| 暂停期间可处理的插件消息面 | 代码可见路径主要继续 pump Inspector | `ProcessMessages()` 在暂停态仍可处理 `CallStack`、`Continue`、`Step*` 等消息 | Angelscript 实现质量更完整 |

### [维度 D8] 性能与优化：`puerts` 的 VM 接入成本不只在调用桥，还体现在 allocator 接口面与进程级兼容层

前面的 D8 已经比较过 `FastCall`、`StaticJIT`、GC 和 event loop；这一轮补的是 **VM 被接进 UE 时，内存入口是谁在负责**。`puerts` 的证据分成两层。第一层在 `JsEnvModule`：`FMallocWrapper` 把 `Malloc` / `TryMalloc` 的 `Size == 0` 强制改成 `1`，并在旧版 UE 路径下直接替换全局 `GMalloc`。第二层在 `FJsEnvImpl`：V8 路径用 `v8::ArrayBuffer::Allocator::NewDefaultAllocator()`，Node 路径则改走 `node::ArrayBufferAllocator::Create()`、`node::NewIsolate()` 和 `node::CreateIsolateData()`。也就是说，`puerts` 为了嵌入通用 JS VM，除了函数调用桥，还引入了 allocator 兼容与 backend-specific isolate 启动成本。

Angelscript 的资源边界更局部。当前检视源码里，`FAngelscriptOwnedSharedState` 集中持有 `ScriptEngine`、`PrimaryContext`、`PrecompiledData`、`StaticJIT`、`DebugServer` 与类型数据库；`CreateConfiguredContext()` 只给 AngelScript context 装回调；`ReleaseOwnedSharedStateResources()` 则在一个函数里统一释放 context、context pool 和 `ScriptEngine`。在目前检查到的路径中，没有看到等价的全局 allocator 替换。

```
[puerts] Memory Entry Layers
Process
├─ JsEnvModule::StartupModule
│  └─ optional GMalloc -> FMallocWrapper   // 兼容旧 UE 的全局 allocator 补丁
└─ FJsEnvImpl ctor
   ├─ V8: NewDefaultAllocator()            // 普通 V8 allocator
   └─ Node: ArrayBufferAllocator::Create()
      ├─ node::NewIsolate(...)             // Node 独立 isolate 入口
      └─ CreateIsolateData(...)            // 追加 Node runtime 元数据

[Angelscript] Shared-State Lifetime
FAngelscriptEngine
├─ FAngelscriptOwnedSharedState
│  ├─ ScriptEngine / PrimaryContext
│  ├─ PrecompiledData / StaticJIT / DebugServer
│  └─ TypeDatabase / BindState / BindDatabase
├─ CreateConfiguredContext()
└─ ReleaseOwnedSharedStateResources()
   ├─ release context
   ├─ release pooled contexts
   └─ ShutDownAndRelease script engine
```

[1] `puerts` 的 allocator 兼容层先落在模块级别，必要时直接改写 `GMalloc` 入口。

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvModule.cpp
// 位置: 26-54, 172-181, 261-268
// ============================================================================
class FMallocWrapper final : public FMalloc
{
public:
    virtual void* Malloc(SIZE_T Size, uint32 Alignment) override
    {
        if (UNLIKELY(Size == 0))
        {
            Size = 1;                                  // ★ 把 zero-sized allocation 修正为 1，规避底层分配器差异
        }
        return InnerMalloc->Malloc(Size, Alignment);
    }

    virtual void* TryMalloc(SIZE_T Size, uint32 Alignment) override
    {
        if (UNLIKELY(Size == 0))
        {
            Size = 1;
        }
        return InnerMalloc->TryMalloc(Size, Alignment);
    }
};

if (!Dummy)
{
    MallocWrapper = new FMallocWrapper(GMalloc);
    GMalloc = MallocWrapper;                           // ★ 兼容逻辑直接接到全局分配器入口
}

if (MallocWrapper && MallocWrapper == GMalloc)
{
    GMalloc = MallocWrapper->InnerMalloc;             // ★ 模块关闭时再恢复全局 allocator
    delete MallocWrapper;
}
```

[2] `FJsEnvImpl` 在不同 backend 下再分出不同的 allocator / isolate 启动路径。

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp
// 位置: 389-395, 425-429, 448-467, 907-910
// ============================================================================
CreateParams.array_buffer_allocator = v8::ArrayBuffer::Allocator::NewDefaultAllocator();
MainIsolate = v8::Isolate::New(CreateParams);         // ★ V8 路径使用默认 ArrayBuffer allocator

CreateParams.array_buffer_allocator = nullptr;
NodeArrayBufferAllocator = node::ArrayBufferAllocator::Create();
auto Platform = static_cast<node::MultiIsolatePlatform*>(IJsEnvModule::Get().GetV8Platform());
MainIsolate = node::NewIsolate(NodeArrayBufferAllocator.get(), &NodeUVLoop, Platform); // ★ Node 路径改走另一套 allocator/isolate 组合

NodeIsolateData =
    node::CreateIsolateData(Isolate, &NodeUVLoop, Platform, NodeArrayBufferAllocator.get());
NodeEnv = CreateEnvironment(NodeIsolateData, Context, Args, ExecArgs, node::EnvironmentFlags::kOwnsProcessState);

DefaultContext.Reset();
MainIsolate->Dispose();
delete CreateParams.array_buffer_allocator;           // ★ env 销毁时还要回收 V8 allocator
```

[3] `Angelscript` 当前资源生命周期主要收敛在 owned shared state，而不是进程级 allocator patch。

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 144-163, 239-257, 327-378
// ============================================================================
struct FAngelscriptOwnedSharedState
{
    asCScriptEngine* ScriptEngine = nullptr;
    asCContext* PrimaryContext = nullptr;
    FAngelscriptPrecompiledData* PrecompiledData = nullptr;
    FAngelscriptStaticJIT* StaticJIT = nullptr;
    FAngelscriptDebugServer* DebugServer = nullptr;
    TUniquePtr<FAngelscriptTypeDatabase> TypeDatabase;
    TUniquePtr<FAngelscriptBindState> BindState;
    TUniquePtr<FAngelscriptBindDatabase> BindDatabase; // ★ 资源集中挂在 shared state 上
};

auto* Context = static_cast<asCContext*>(ScriptEngine->CreateContext());
Context->SetExceptionCallback(asFUNCTION(LogAngelscriptException), 0, asCALL_CDECL);
Context->SetLineCallback(AngelscriptLineCallback);
Context->SetStackPopCallback(AngelscriptStackPopCallback);     // ★ context 配置停留在 AngelScript 层

if (SharedState->PrimaryContext != nullptr)
{
    SharedState->PrimaryContext->Release();
}
if (SharedState->ScriptEngine != nullptr)
{
    ReleaseContextsForScriptEngine(GAngelscriptContextPool.FreeContexts, SharedState->ScriptEngine);
    SharedState->ScriptEngine->ShutDownAndRelease();            // ★ 收口逻辑集中在一个 shared-state 释放函数里
}
SharedState->TypeDatabase.Reset();
SharedState->BindState.Reset();
SharedState->BindDatabase.Reset();
```

设计取舍：

- `puerts` 的收益是能把 V8 / Node 这类通用 VM 原样嵌进 UE，因此可以享受各自 runtime 的优化与生态。
- 代价是启动与关闭路径要承担更宽的 allocator 接口面，甚至在旧版 UE 兼容路径上触碰全局 `GMalloc`。
- `Angelscript` 的收益是资源边界更内聚，当前检视代码主要围绕 `OwnedSharedState` 与 context pool 收口；代价是它没有 `puerts` 这种可替换外部 VM 的优化轴。

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| allocator 接入层级 | 模块级 `FMallocWrapper` + env 级 `ArrayBufferAllocator` / `NodeArrayBufferAllocator` | 当前检视路径主要是 `OwnedSharedState` + context 生命周期管理 | 实现方式不同 |
| 进程级 allocator 副作用面 | 旧版 UE 路径可直接替换 `GMalloc` | 当前检视源码未见等价全局 allocator 替换 | 当前检视范围内 Angelscript 没有实现 |
| 资源收口边界 | 模块 shutdown 与 env destroy 分散处理 | `ReleaseOwnedSharedStateResources()` 集中释放 shared state 资源 | Angelscript 在当前资源收口边界上实现质量更收敛 |

---

## 深化分析 (2026-04-09 06:59:03)

### [维度 D6] 代码生成与 IDE 支持：`puerts` 的类型安全是“反射投影后的近似类型”，投影失败时优先跳过；`Angelscript` 则直接把非法签名卡死在生成链

前面几轮已经把 `ue.d.ts`、`ue_bp.d.ts`、`cpp/index.d.ts` 和 `LanguageService` 闭环讲得比较完整；这一轮只补 **这条类型链在遇到坏形状时如何退化**。`puerts` 的 `DeclarationGenerator` 不是“所有 UE 反射都一定能成功投影到 TypeScript”。它会先对 native type 名做全局去重，重名直接警告并跳过；遇到 `IgnoreClassListOnDTS` / `IgnoreStructListOnDTS` 或无法识别的 `FProperty`，`GenTypeDecl(...)` 直接 `return false`；函数侧则把 `out` 参数包成 `$Ref<T>`，普通对象参数包成 `$Nullable<T>`，默认值只保留成注释。这说明 `puerts` 的 IDE 类型安全依赖于“能否把 UE 形状近似映射为 TypeScript”，一旦近似失败，策略是减少声明面，而不是阻止运行时继续存在。

`Angelscript` 这边的策略更硬。`Helper_FunctionSignature.h` 在把 `UFunction` 转成 `FAngelscriptTypeUsage` 时，只要某个参数或返回类型无效，就把 `bAllTypesValid` 置为 `false`；而类生成阶段 `AngelscriptClassGenerator.cpp` 对脚本定义函数更严格，返回引用、不可返回类型、不可创建属性的参数都会立刻报 `ScriptCompileError(...)`，并把 `ReloadReq` 升为 `Error`。也就是说，Angelscript 的“类型安全”更多来自 **拒绝不合法表面进入系统**，而不是像 puerts 那样尽量生成一个可工作的近似声明。

```
[puerts] Type Projection Degrade Path
UE Reflection
├─ duplicate native name? -> warn + skip        // 原生重名直接不进 ue.d.ts
├─ ignored class/struct? -> return false        // 配置忽略名单直接裁掉
├─ unsupported property? -> return false        // 不能映射到 TS 的属性直接失败
└─ supported property
   ├─ object -> $Nullable<T>                    // 非 ref object 参数默认可空
   ├─ out parm -> $Ref<T>                       // 非 const out 参数包成引用壳
   └─ default -> optional + comment             // 默认值只进注释，不进真实求值语义

[Angelscript] Signature Enforcement Path
Native / Script Function
├─ FromProperty / FromReturn / FromParam        // 先转成 FAngelscriptTypeUsage
├─ invalid type? -> bAllTypesValid = false      // 无法表达的类型先标红
├─ invalid return/ref? -> ScriptCompileError    // 不允许的脚本表面直接报错
└─ ReloadReq = Error                            // 拒绝进入 reload 成功路径
```

[1] `puerts` 的声明生成会把坏形状直接裁掉，并把能表达的形状压成 TypeScript 近似类型。

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp
// 函数: FTypeScriptDeclarationGenerator::Gen / GenTypeDecl / GenFunction
// 位置: 691-715, 741-925, 953-1063
// ============================================================================
if (ToGen->IsNative() && ProcessedByName.Contains(SafeName(ToGen->GetName())))
{
    UE_LOG(LogTemp, Warning, TEXT("duplicate name found in ue.d.ts generate: %s"), *ToGen->GetName());
    return; // ★ native 重名直接跳过，而不是生成带命名空间歧义的声明
}

const TArray<FString>& IgnoreStructListOnDTS = IPuertsModule::Get().GetIgnoreStructListOnDTS();
if (IgnoreStructListOnDTS.Contains(Name))
{
    return false; // ★ 命中 ignore list，整个类型声明链路直接中断
}

StringBuffer << "TArray<";
bool Result = GenTypeDecl(StringBuffer, ArrayProperty->Inner, AddToGen, false, TreatAsRawFunction);

if (IsReference)
{
    TmpBuf << "$Ref<"; // ★ 非 const out 参数投影成 $Ref<T>
}
if (IsNullable)
{
    TmpBuf << "$Nullable<"; // ★ 普通 UObject* 参数默认投影成可空
}

if (DefaultValuePtr)
{
    TmpBuf << "?";
    TmpBuf << " /* = " << *DefaultValuePtr << " */"; // ★ 默认值只保留成注释，不参与真实 TS 求值
}

if (!GenTypeDecl(OwnerBuffer, ReturnValue, RefTypes))
{
    return false; // ★ 返回类型投影失败时，函数声明整体放弃
}
```

[2] `Angelscript` 则把“能不能表达”前移成签名构造阶段的硬校验。

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h
// 函数: FAngelscriptMethodDesc::InitFromFunction
// 位置: 178-223, 321-332
// ============================================================================
for( TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It )
{
    FProperty* Property = *It;
    FAngelscriptTypeUsage Type = FAngelscriptTypeUsage::FromProperty(Property);

    if (!Type.IsValid())
    {
        bAllTypesValid = false;
        break; // ★ 一旦参数类型无法映射到 Angelscript，直接标记整条签名无效
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
    (Function->HasAnyFunctionFlags(FUNC_Const) && !bStaticInScript) || bForceConst
); // ★ 只有通过类型映射的参数才会进入最终脚本声明
```

[3] 到脚本类生成阶段，`Angelscript` 会继续把不合法表面拦截成编译错误，而不是降级生成近似签名。

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 位置: 572-610
// ============================================================================
FunctionDesc->ReturnType = FAngelscriptTypeUsage::FromReturn(ScriptFunction);
if (!FunctionDesc->ReturnType.IsValid() || !FunctionDesc->ReturnType.CanCreateProperty() ||
    !FunctionDesc->ReturnType.CanBeReturned() || FunctionDesc->ReturnType.bIsReference)
{
    if (FunctionDesc->ReturnType.bIsReference)
    {
        FAngelscriptEngine::Get().ScriptCompileError(
            ModuleData.NewModule, FunctionDesc->LineNumber,
            FString::Printf(TEXT("UFUNCTIONs cannot return references, function %s in class %s"),
                *FunctionDesc->FunctionName, *ClassData.NewClass->ClassName)
        );
    }
    else
    {
        FAngelscriptEngine::Get().ScriptCompileError(
            ModuleData.NewModule, FunctionDesc->LineNumber,
            FString::Printf(TEXT("Unknown or invalid return type to function %s in class %s"),
                *FunctionDesc->FunctionName, *ClassData.NewClass->ClassName)
        );
    }

    ClassData.ReloadReq = EReloadRequirement::Error; // ★ 非法签名直接阻断类生成与 reload
    continue;
}
```

设计取舍：

- `puerts` 的收益是 IDE 声明覆盖面大，遇到复杂 UE 类型时还能通过 `$Ref<T>`、`$Nullable<T>`、容器泛型和注释默认值维持较高可用性。
- 代价是这份类型面天然是投影，不是宿主真实 ABI；遇到重名、ignore list、未知 property 时，它更倾向于裁剪声明而不是给出强一致错误。
- `Angelscript` 的收益是脚本表面更接近“已验证可执行的真实签名”；代价是对不能表达的形状更保守，直接报错而不是退化成近似 IDE 类型。

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 类型投影失败策略 | `duplicate name` / ignore list / unknown property 时直接跳过声明 | 非法类型直接 `ScriptCompileError` 并中断生成 | 实现方式不同 |
| `out/ref` 表达 | `$Ref<T>`、`$Nullable<T>`、默认值注释 | `FAngelscriptTypeUsage` + 最终脚本声明，拒绝非法引用返回 | 实现方式不同 |
| IDE 类型与运行时 ABI 一致性 | 属于“高保真投影”，但不是严格同构 | 更接近已注册脚本 ABI | Angelscript 在“声明即真实可执行表面”上实现质量更强 |

### [维度 D4] 热重载：`puerts` 的重绑前提是“可注入的 TS 生成类 + BlueprintEvent override”，`Angelscript` 的重载前提是“整个类型依赖图可传播”

前面的几轮已经把 `Inspector`、`SourceFileWatcher` 和 module cache 讲过了；这一轮补 **到底哪些类有资格进入 rebind**。`puerts` 的 `MakeSureInject(...)` 不是对所有 JS/TS 模块一视同仁。它先要求 `UTypeScriptGeneratedClass` 有合法 `UPackage`，再拒绝 `SKEL_ / REINST_ / TRASHCLASS_ / PLACEHOLDER- / HOTRELOADED_` 这些临时类；随后要求模块 `default` 导出是函数，并且 prototype 上真的覆盖了 `FUNC_BlueprintEvent`，才会把该 `UFunction` 放进 `FunctionToRedirect`。换句话说，puerts 的热更新最终落点并不是“任意 JS 模块”，而是 **能被包装成 `UTypeScriptGeneratedClass` 且命中 Blueprint event redirection 条件的那一小块 UE 表面**。

`Angelscript` 的 reload 入口则没有这种“只看某一种 class 壳”的限制。`FAngelscriptClassGenerator` 会把 super type、属性类型、返回值、参数类型全部递归塞进 `AddReloadDependency(...)`，再把 `FullReloadSuggested / FullReloadRequired / Error` 沿依赖图向后传播。也就是说，Angelscript 判断“能不能 reload”的单位是 **脚本类型图**，而不是某个特定 Blueprint generated class 的注入资格。

```
[puerts] Rebind Eligibility
UTypeScriptGeneratedClass
├─ valid package?                         // 没有 package 直接退出
├─ not SKEL_/REINST_/HOTRELOADED_?        // 临时类全部拒绝注入
├─ module default export is function?     // 必须能拿到 JS constructor/prototype
├─ own prototype overrides BlueprintEvent // 只重定向蓝图事件函数
└─ NeedReBind / LazyLoadRedirect          // 之后才进入惰性重绑

[Angelscript] Reload Eligibility
Changed Script Module
├─ analyze class / delegate diff          // 先算本节点 reload 等级
├─ AddReloadDependency(super/prop/ret/arg)
├─ propagate FullReloadSuggested/Required // 依赖类型继续抬升等级
└─ resolve pending dependees              // 最终决定 soft/full/error
```

[1] `puerts` 的注入资格被显式限制在“可注入 TS 生成类 + BlueprintEvent override”。

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/TypeScriptGeneratedClass.cpp
// 函数: UTypeScriptGeneratedClass::NotSupportInject / ObjectInitialize / LazyLoadRedirect
// 位置: 107-119, 203-219, 267-271
// ============================================================================
void UTypeScriptGeneratedClass::LazyLoadRedirect()
{
    for (TFieldIterator<UFunction> FuncIt(this, EFieldIteratorFlags::ExcludeSuper); FuncIt; ++FuncIt)
    {
        auto Function = *FuncIt;
        if (!FunctionToRedirect.Contains(Function->GetFName()))
        {
            continue;
        }
        Function->SetNativeFunc(&UTypeScriptGeneratedClass::execLazyLoadCallJS); // ★ 只对已登记函数做惰性重绑
    }
}

if (!Function->IsNative() && Function->HasAnyFunctionFlags(FUNC_BlueprintEvent))
{
    TempNativeFuncStorage.Add(Function->GetFName(), Function->GetNativeFunc());
    Function->SetNativeFunc(&UTypeScriptGeneratedClass::execLazyLoadCallJS); // ★ 默认只拦 BlueprintEvent
}

bool UTypeScriptGeneratedClass::NotSupportInject()
{
    return (GetName().StartsWith("SKEL_") || GetName().StartsWith("REINST_") ||
            GetName().StartsWith("TRASHCLASS_") || GetName().StartsWith("PLACEHOLDER-") ||
            GetName().StartsWith("HOTRELOADED_")); // ★ 临时类全部被排除出注入链
}
```

[2] `MakeSureInject(...)` 进一步要求模块 `default` 导出和 prototype 条件都满足，才会真的建立 redirection。

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp
// 函数: FJsEnvImpl::MakeSureInject
// 位置: 1222-1230, 1282-1310, 1322-1394
// ============================================================================
auto Package = Cast<UPackage>(TypeScriptGeneratedClass->GetOuter());
if (!Package || TypeScriptGeneratedClass->NotSupportInject())
{
    return; // ★ 没有 package 或命中临时类前缀时，整个 inject 直接退出
}

if (!MaybeRet.IsEmpty())
{
    auto Ret = MaybeRet.ToLocalChecked().As<v8::Object>();
    auto MaybeFunc = Ret->Get(Context, FV8Utils::ToV8String(Isolate, "default"));
    if (MaybeFunc.ToLocal(&Val) && Val->IsFunction())
    {
        auto Func = Val.As<v8::Function>(); // ★ default export 必须真的是 constructor function
        if (Proto->Get(Context, FV8Utils::ToV8String(Isolate, "Constructor")).ToLocal(&VCtor) && VCtor->IsFunction())
        {
            BindInfoMap[TypeScriptGeneratedClass].Constructor.Reset(Isolate, VCtor.As<v8::Function>());
        }

        for (TFieldIterator<UFunction> It(TypeScriptGeneratedClass, EFieldIteratorFlags::ExcludeSuper,
                 EFieldIteratorFlags::ExcludeDeprecated, EFieldIteratorFlags::ExcludeInterfaces);
             It; ++It)
        {
            UFunction* Function = *It;
            if (!TypeScriptGeneratedClass->FunctionToRedirect.Contains(FunctionFName) &&
                FuncsObj->HasOwnProperty(Context, V8Name).ToChecked() &&
                (Function->HasAnyFunctionFlags(FUNC_BlueprintEvent)))
            {
                TypeScriptGeneratedClass->FunctionToRedirect.Add(FunctionFName);
                TypeScriptGeneratedClass->RedirectToTypeScript(Function); // ★ 只有 prototype 上真实覆写的 BlueprintEvent 才重定向
            }
        }

        TypeScriptGeneratedClass->FunctionToRedirectInitialized = true;
    }
}
```

[3] `Angelscript` 的 reload 则显式把 super / property / return / argument 全部纳入依赖传播。

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h
// 位置: 23-29, 52-59
// ============================================================================
enum EReloadRequirement
{
    SoftReload,
    FullReloadSuggested,
    FullReloadRequired,
    Error,
};

struct FReloadPropagation
{
    bool bHasOutstandingDependencies = false;
    EReloadRequirement ReloadReq = EReloadRequirement::SoftReload;
    TArray<FReloadPropagation*> PendingDependees; // ★ reload 等级沿依赖图向后传播
};
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 函数: AddReloadDependency / PropagateReloadRequirements / ResolvePendingReloadDependees
// 位置: 1908-1923, 1975-2041, 2063-2077
// ============================================================================
for (const FAngelscriptTypeUsage& SubType : Type.SubTypes)
    AddReloadDependency(Source, SubType); // ★ 容器子类型也进入依赖传播

if (Type.ScriptClass == nullptr || !(Type.ScriptClass->GetFlags() & asOBJ_SCRIPT_OBJECT))
    return;

if (!ClassDesc->bSuperIsCodeClass)
{
    asITypeInfo* SuperScriptType = ClassDesc->ScriptType->GetBaseType();
    if (SuperScriptType != nullptr)
        AddReloadDependency(&ClassData, SuperScriptType); // ★ 父类变化抬升子类 reload 等级
}

for (auto Property : ClassDesc->Properties)
{
    AddReloadDependency(&ClassData, Property->PropertyType);
}

for (auto Function : ClassDesc->Methods)
{
    AddReloadDependency(&ClassData, Function->ReturnType);
    for (auto Argument : Function->Arguments)
        AddReloadDependency(&ClassData, Argument.Type); // ★ 返回值和参数类型同样参与传播
}

if (Source->ReloadReq > Dependee->ReloadReq)
{
    Dependee->ReloadReq = Source->ReloadReq;
    ResolvePendingReloadDependees(Dependee); // ★ 依赖完成后继续向后递归推送最新 reload 等级
}
```

设计取舍：

- `puerts` 的收益是只在 TS-generated Blueprint 主链上做精细 redirection，避免把整个 JS module graph 都拉进 UE 类型重建。
- 代价是 reload 资格边界明显窄于“所有已加载脚本”，并且核心命中条件依赖 `default export`、`prototype` 与 `BlueprintEvent`。
- `Angelscript` 的收益是 reload 语义和类型图一致，依赖传播规则明确；代价是分析和升级 reload 等级的成本更高。

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| reload 主体 | `UTypeScriptGeneratedClass` 注入链 | 脚本类/委托/属性/方法组成的类型图 | 实现方式不同 |
| 热重载资格限制 | 拒绝 `SKEL_ / REINST_ / HOTRELOADED_`，仅覆盖可重定向 `BlueprintEvent` | 依赖传播面覆盖 super / property / return / argument | puerts 在 reload 覆盖面上实现更窄 |
| 失败模型 | 注入前提不满足时直接不进入重绑 | `ReloadReq` 明确升级到 `Suggested/Required/Error` | Angelscript 在 reload 决策透明度上实现质量更完整 |

### [维度 D1] 插件架构与模块划分：`puerts` 的多后端层是“统一入口 + 编译期开洞的 capability façade”，`Angelscript` 的单引擎层保持 feature plane 一致

前面已经写过 `Build.cs` 里有 V8 / QuickJS / Node.js 开关；这一轮补的是 **这些后端在 API 面到底是不是同一套能力**。答案并不完全是。`IJsEnv` / `FJsEnv` 对外暴露的是统一接口，看起来有 `Start`、GC、`WaitDebugger`、`ReloadSource`、`CurrentStackTrace` 这一整套宿主 API；但 `JsEnv.Build.cs` 在 QuickJS 路径直接定义 `WITHOUT_INSPECTOR` + `WITH_QUICKJS`，而 `JsEnvImpl.cpp` 里 `__tgjsSetInspectorCallback`、`__tgjsDispatchProtocolMessage` 的实现本体都包在 `#ifndef WITH_QUICKJS` 下。也就是说，puerts 的多后端抽象更像一个 **共享入口名、但能力由 backend 和宏裁剪的 façade**。

`Angelscript` 这边则没有“换后端”这条轴。`FAngelscriptEngine` 从一开始就围绕同一个 `asIScriptEngine` 初始化一组固定 feature：`SetEngineProperty(...)`、可选 `StaticJIT`、可选 `DebugServer`、可选 `BindDatabase`；测试和分身能力也不是切引擎后端，而是 `CreateCloneFrom(...)` 共享同一份 `SharedState` 语义。也就是说，Angelscript 的模块分层没有 puerts 那种后端可替换性，但它的 feature plane 对用户更稳定。

```
[puerts] Backend Capability Façade
IJsEnv / FJsEnv
├─ Start / Reload / GC / WaitDebugger      // 统一入口名
├─ JsEnv.Build.cs
│  ├─ WITH_NODEJS                          // Node 路径保留 Inspector 体系
│  └─ WITH_QUICKJS + WITHOUT_INSPECTOR     // QuickJS 直接裁掉调试通道
└─ JsEnvImpl
   ├─ bind __tgjsSetInspectorCallback      // V8/Node 有实现
   └─ #ifndef WITH_QUICKJS bodies          // QuickJS 编译期直接开洞

[Angelscript] Single Engine Capability Plane
FAngelscriptEngine
├─ one AngelScript runtime                 // 没有多 backend 选择
├─ SetEngineProperty(...)                  // 统一引擎属性面
├─ optional StaticJIT / DebugServer        // 通过配置开关增减能力
└─ CreateCloneFrom(...)                    // 用 clone 共享语义，而不是切换后端
```

[1] `puerts` 对外先暴露统一 `IJsEnv` / `FJsEnv`，但这层并不保证所有 backend 真有相同能力。

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JsEnv.h
// 位置: 25-58, 61-100
// ============================================================================
class JSENV_API IJsEnv
{
public:
    virtual void Start(const FString& ModuleName, const TArray<TPair<FString, UObject*>>& Arguments) = 0;
    virtual bool IdleNotificationDeadline(double DeadlineInSeconds) = 0;
    virtual void LowMemoryNotification() = 0;
    virtual void RequestMinorGarbageCollectionForTesting() = 0;
    virtual void RequestFullGarbageCollectionForTesting() = 0;
    virtual void WaitDebugger(double Timeout) = 0;
    virtual void ReloadModule(FName ModuleName, const FString& JsSource) = 0;
    virtual void ReloadSource(const FString& Path, const PString& JsSource) = 0;
    virtual FString CurrentStackTrace() = 0;
}; // ★ 对外表面看起来是统一脚本运行时接口

class JSENV_API FJsEnv
{
public:
    void Start(const FString& ModuleName, const TArray<TPair<FString, UObject*>>& Arguments = TArray<TPair<FString, UObject*>>());
    bool IdleNotificationDeadline(double DeadlineInSeconds);
    void LowMemoryNotification();
    void RequestMinorGarbageCollectionForTesting();
    void RequestFullGarbageCollectionForTesting();
    void WaitDebugger(double Timeout = 0);
}; // ★ wrapper 名字统一，但具体能力是否有效要看 backend 编译结果
```

[2] QuickJS 路径在构建层和实现层都会把 Inspector 能力切掉，说明 façade 不是严格同构接口。

```csharp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs
// 位置: 624-632, 502-523
// ============================================================================
void ThirdPartyQJS(ReadOnlyTargetRules Target)
{
    PrivateDefinitions.Add("WITHOUT_INSPECTOR");
    PrivateDefinitions.Add("WITH_QUICKJS"); // ★ QuickJS 路径编译期直接声明“无 Inspector”
    if (QjsNamespaceSuffix)
    {
        PublicDefinitions.Add("QJSV8NAMESPACE=v8_qjs");
    }
}

void ThirdPartyNodejs(ReadOnlyTargetRules Target)
{
    PrivateDefinitions.Add("WITH_NODEJS");  // ★ Node.js 走另一条后端路径，但仍保留 Inspector 体系
    RuntimeDependencies.Add("$(TargetOutputDir)/libnode.dll", Path.Combine(V8LibraryPath, "libnode.dll"));
}
```

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp
// 位置: 524-529, 4522-4583
// ============================================================================
MethodBindingHelper<&FJsEnvImpl::SetInspectorCallback>::Bind(Isolate, Context, Global, "__tgjsSetInspectorCallback", This);
MethodBindingHelper<&FJsEnvImpl::DispatchProtocolMessage>::Bind(
    Isolate, Context, Global, "__tgjsDispatchProtocolMessage", This); // ★ JS 全局先统一注入调试入口名

void FJsEnvImpl::SetInspectorCallback(const v8::FunctionCallbackInfo<v8::Value>& Info)
{
#ifndef WITH_QUICKJS
    if (!Inspector)
        return;

    CHECK_V8_ARGS(EArgFunction);

    if (!InspectorChannel)
    {
        InspectorChannel = Inspector->CreateV8InspectorChannel();
        InspectorChannel->OnMessage(
            [this](std::string Message)
            {
                v8::Isolate::Scope IsolatescopeObject(MainIsolate);
                v8::HandleScope HandleScopeObject(MainIsolate);
                v8::Local<v8::Context> ContextInner = DefaultContext.Get(MainIsolate);
                v8::Context::Scope ContextScopeObject(ContextInner);
                auto Handler = InspectorMessageHandler.Get(MainIsolate);
                v8::Local<v8::Value> Args[] = {FV8Utils::ToV8String(MainIsolate, Message.c_str())};
                __USE(Handler->Call(ContextInner, ContextInner->Global(), 1, Args));
            });
    }

    InspectorMessageHandler.Reset(Isolate, v8::Local<v8::Function>::Cast(Info[0]));
#endif    // !WITH_QUICKJS
}

void FJsEnvImpl::DispatchProtocolMessage(const v8::FunctionCallbackInfo<v8::Value>& Info)
{
#ifndef WITH_QUICKJS
    if (InspectorChannel)
    {
        InspectorChannel->DispatchProtocolMessage(TCHAR_TO_UTF8(*Message));
    }
#endif    // !WITH_QUICKJS
} // ★ QuickJS 下接口名还在，但真正 Inspector 逻辑被编译期裁掉
```

[3] `Angelscript` 的 feature plane 则围绕单一引擎保持一致，通过配置启用 `StaticJIT`、`DebugServer` 和 cache，而不是切 backend。

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 1388-1469, 610-640
// ============================================================================
Engine->SetEngineProperty(asEP_ALLOW_UNSAFE_REFERENCES, 1);
Engine->SetEngineProperty(asEP_OPTIMIZE_BYTECODE, 1);
Engine->SetEngineProperty(asEP_DISALLOW_VALUE_ASSIGN_FOR_REF_TYPE, 1);
Engine->SetEngineProperty(asEP_REQUIRE_ENUM_SCOPE, 1); // ★ 单一 AngelScript 引擎统一配置语义

if (bGeneratePrecompiledData)
{
    StaticJIT = new FAngelscriptStaticJIT();
    StaticJIT->PrecompiledData = PrecompiledData;
    Engine->SetJITCompiler(StaticJIT); // ★ 同一引擎上附加 JIT，而不是切到另一套 backend
}

if ((!bUsePrecompiledData || bScriptDevelopmentMode) && FApp::HasProjectName())
{
    DebugServer = new FAngelscriptDebugServer(this, RuntimeConfig.DebugServerPort);
}

TUniquePtr<FAngelscriptEngine> FAngelscriptEngine::CreateCloneFrom(FAngelscriptEngine& Source, const FAngelscriptEngineConfig& InConfig, const FAngelscriptEngineDependencies& InDependencies)
{
    TUniquePtr<FAngelscriptEngine> EngineInstance = MakeUnique<FAngelscriptEngine>(InConfig, InDependencies);
    EngineInstance->CreationMode = EAngelscriptEngineCreationMode::Clone;
    EngineInstance->SourceEngine = Source.GetSourceEngine() != nullptr ? Source.GetSourceEngine() : &Source;
    EngineInstance->bOwnsEngine = false; // ★ clone 复用同一运行时语义，不是多 backend 切换
    return EngineInstance;
}
```

设计取舍：

- `puerts` 的收益是多后端路线清晰，能在同一插件架构下切 V8 / Node.js / QuickJS。
- 代价是“统一 API”更多是 façade 级统一，真正能力面会随 `WITH_QUICKJS`、`WITHOUT_INSPECTOR` 等宏发生裂缝。
- `Angelscript` 的收益是 feature plane 更稳定；代价是没有 puerts 这种通过 backend 切换换取生态/JIT/runtime 选择空间的能力。

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 脚本运行时扩展轴 | 多 backend (`V8/Node.js/QuickJS`) | 单一 AngelScript 引擎 | 实现方式不同 |
| 对外能力面一致性 | `IJsEnv` 名义统一，但 QuickJS 会编译期裁掉 Inspector 能力 | `SetEngineProperty` / `StaticJIT` / `DebugServer` 围绕同一引擎一致展开 | Angelscript 在能力面一致性上实现质量更稳定 |
| 横向扩展方式 | 通过 backend 选择与宏裁剪扩展 | 通过 clone / shared state / 可选子系统扩展 | 实现方式不同 |

## 深化分析 (2026-04-09 07:08:24)

### [维度 D4] 热重载：`puerts` 实际有两条 reload 入口，`module name` 与 `absolute path` 并不是同一个身份层

前面的 D4 已经把 `Inspector`、`SourceFileWatcher`、`UTypeScriptGeneratedClass` 的大框架写清了；这一轮补的是 **入口层到底有几条链**。顺着 `CodeAnalyze.ts`、`FileSystemOperation.cpp`、`PuertsModule.cpp` 再对一遍，可以看到 puerts 的 editor 工作流里至少有两条正式 reload 入口：

- 一条是 **emit-driven inline reload**。`CodeAnalyze.ts` 在语义检查和 emit 成功后，直接把内存里的 `jsSource` 通过 `UE.FileSystemOperation.PuertsNotifyChange(moduleFileName, jsSource)` 推给 runtime；C++ 再转成 `IPuertsModule::ReloadModule(FName ModuleName, const FString& JsSource)`。
- 另一条是 **load-driven path reload**。`FPuertsEditorModule::OnPostEngineInit()` 创建 editor-side `FJsEnv` 时挂了 `OnSourceLoaded` 回调；只要某个 loose `.js` 真被执行过，`FSourceFileWatcher` 就会记住该文件和目录。之后磁盘上的 `*.js` 发生 MD5 变化，就会走 `JsEnv->ReloadSource(absPath, source)`。

这意味着同一份逻辑改动，在 puerts 里可能以 `moduleFileName` 触发，也可能以 `absolute file path` 触发；两条链共享同一个 VM，但标识粒度不同。对比之下，Angelscript 当前检视到的 hot reload 入口更收敛：目录监听统一收集 `{AbsolutePath, RelativePath}`，交给 `Engine.FileChangesDetectedForReload / FileDeletionsDetectedForReload`，后续分析、编译、失败回退都围绕这同一份文件对来走。

```
[puerts] Reload Entry Split
TS source (.ts)
 -> CodeAnalyze semantic pass                   // TS 语义检查通过
 -> emit jsSource
 -> PuertsNotifyChange(moduleName, jsSource)   // 逻辑模块名
 -> IPuertsModule::ReloadModule()
 -> JsEnv->ReloadModule(moduleName, jsSource)

Loaded loose JS (.js)
 -> OnSourceLoaded(absPath)                    // 运行过的真实文件
 -> FSourceFileWatcher register dir + md5
 -> disk modify *.js
 -> JsEnv->ReloadSource(absPath, source)       // 绝对路径

[Angelscript] Unified File Queue
DirectoryWatcher
 -> QueueScriptFileChanges(absPath, relPath)
 -> FileChangesDetectedForReload / FileDeletionsDetectedForReload
 -> CheckForHotReload()
 -> compile + keep-old-code-on-failure
```

[1] `puerts` 的第一条入口来自 editor semantic host，直接把内存中的 emit 结果推给 `ReloadModule()`，中间不依赖磁盘 watcher 二次发现。

```ts
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/PuertsEditor/CodeAnalyze.ts
// 位置: 562-585
// 说明: TS 通过诊断后，直接把 emit 出来的 jsSource 推给 runtime
// ============================================================================
if (!emitOutput.emitSkipped) {
    let moduleFileName: string = undefined;
    let jsSource: string = undefined;
    emitOutput.outputFiles.forEach(output => {
        if (output.name.endsWith(".js") || output.name.endsWith(".mjs")) {
            jsSource = output.text;
            if (options.outDir && output.name.startsWith(options.outDir)) {
                moduleFileName = output.name.substr(options.outDir.length + 1);
                moduleFileName = tsi.removeExtension(moduleFileName, output.name.endsWith(".js") ? ".js" : ".mjs");
            }
        }
    });
    if (moduleFileName && reload) {
        UE.FileSystemOperation.PuertsNotifyChange(moduleFileName, jsSource); // ★ 按逻辑 module 名推送热更
    }
}
```

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/FileSystemOperation.cpp
// 函数: UFileSystemOperation::PuertsNotifyChange
// 位置: 99-102
// ============================================================================
void UFileSystemOperation::PuertsNotifyChange(FString Path, FString Source)
{
    IPuertsModule::Get().ReloadModule(*Path, Source); // ★ TS 侧通知直接转成 runtime module reload
}
```

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp
// 函数: FPuertsModule::ReloadModule
// 位置: 80-93
// ============================================================================
void ReloadModule(FName ModuleName, const FString& JsSource) override
{
    if (Enabled)
    {
        if (JsEnv.IsValid())
        {
            JsEnv->ReloadModule(ModuleName, JsSource); // ★ 单 env 路径继续按 moduleName 走
        }
        else if (NumberOfJsEnv > 1 && JsEnvGroup.IsValid())
        {
            JsEnvGroup->ReloadModule(ModuleName, JsSource); // ★ group mode 同样复用 moduleName 身份
        }
    }
}
```

[2] `puerts` 的第二条入口是“执行过的 loose `.js` 文件”专用路径，它先登记真实文件，再由目录 watcher 仅对 `*.js` 修改触发 `ReloadSource(absPath, source)`。

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp
// 函数: FPuertsEditorModule::OnPostEngineInit
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
                JsEnv->ReloadSource(InPath, puerts::PString((const char*) Source.GetData(), Source.Num())); // ★ 按绝对路径热更
            }
        }
    });

JsEnv = MakeShared<PUERTS_NAMESPACE::FJsEnv>(
    std::make_shared<PUERTS_NAMESPACE::DefaultJSModuleLoader>(TEXT("JavaScript")),
    std::make_shared<PUERTS_NAMESPACE::FDefaultLogger>(), -1,
    [this](const FString& InPath)
    {
        if (SourceFileWatcher.IsValid())
        {
            SourceFileWatcher->OnSourceLoaded(InPath); // ★ 只有真正执行过的文件才会被登记监听
        }
    },
    TEXT("--max-old-space-size=2048"));
```

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/SourceFileWatcher.cpp
// 函数: FSourceFileWatcher::OnSourceLoaded / OnDirectoryChanged
// 位置: 22-49, 52-80
// ============================================================================
void FSourceFileWatcher::OnSourceLoaded(const FString& InPath)
{
    FString Dir = FPaths::GetPath(InPath);
    FString FileName = FPaths::GetCleanFilename(InPath);
    if (!WatchedFiles[Dir].Contains(FileName))
    {
        UE_LOG(Puerts, Log, TEXT("add watched file: %s"), *InPath);
        FMD5Hash Hash = FMD5Hash::HashFile(*InPath);
        WatchedFiles[Dir].Add(FileName, Hash); // ★ 用已执行文件初始化 MD5 基线
    }
}

void FSourceFileWatcher::OnDirectoryChanged(const TArray<FFileChangeData>& FileChanges)
{
    for (auto Change : FileChanges)
    {
        if (Change.Action == FFileChangeData::FCA_Modified && Change.Filename.EndsWith(TEXT(".js")))
        {
            if (WatchedFiles[Dir][FileName] != Hash)
            {
                OnWatchedFileChanged(NotifyPath); // ★ 只对真实磁盘上的 *.js 修改回调
                WatchedFiles[Dir][FileName] = Hash;
            }
        }
    }
}
```

[3] `Angelscript` 的 watcher 路径则把入口统一成文件对，并且用自动化测试锁住去重与失败回退。

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp
// 函数: AngelscriptEditor::Private::QueueScriptFileChanges
// 位置: 43-89
// ============================================================================
void QueueScriptFileChanges(const TArray<FFileChangeData>& Changes, const TArray<FString>& RootPaths, FAngelscriptEngine& Engine, IFileManager& FileManager, const FEnumerateLoadedScripts& EnumerateLoadedScripts)
{
    for (const FFileChangeData& Change : Changes)
    {
        const FString AbsolutePath = FPaths::ConvertRelativePathToFull(Change.Filename);
        FString RelativePath;

        if (!TryMakeRelativeScriptPath(AbsolutePath, RootPaths, RelativePath))
        {
            continue;
        }

        if (AbsolutePath.EndsWith(TEXT(".as")))
        {
            if (Change.Action == FFileChangeData::EFileChangeAction::FCA_Removed)
            {
                Engine.FileDeletionsDetectedForReload.AddUnique({ AbsolutePath, RelativePath });
            }
            else
            {
                Engine.FileChangesDetectedForReload.AddUnique({ AbsolutePath, RelativePath }); // ★ 同一身份模型统一排队
            }
        }
    }
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp
// 位置: 317-326, 489-493
// 说明: watcher 去重和 reload 失败保留旧代码都有自动化回归保护
// ============================================================================
FAngelscriptHotReloadTestAccess::QueueFileChange(Engine, FilenamePair);
TestEqual(
    TEXT("QueueFileChange should add the changed file once"),
    FAngelscriptHotReloadTestAccess::GetQueuedFileChangeCount(Engine),
    1);

FAngelscriptHotReloadTestAccess::QueueFileChange(Engine, FilenamePair);
TestEqual(
    TEXT("QueueFileChange should keep the queue de-duplicated"),
    FAngelscriptHotReloadTestAccess::GetQueuedFileChangeCount(Engine),
    1); // ★ 入口统一后，去重行为有测试兜底

const bool bCompiled = CompileModuleWithResult(&Engine, ECompileType::SoftReloadOnly, ModuleName, TEXT("HotReloadFailureKeepsOldCode.as"), BrokenScript, ReloadResult);
TestFalse(TEXT("Failure fallback test should fail the broken hot reload compile"), bCompiled);
TestTrue(TEXT("Failure fallback test should report an error reload state"), ReloadResult == ECompileResult::Error || ReloadResult == ECompileResult::ErrorNeedFullReload); // ★ 失败不会静默吞掉
```

设计取舍：

- `puerts` 的收益是 editor 语义分析器可以直接把内存里的新 `jsSource` 推给 VM，不必等待 loose file 再次被加载。
- 代价是 reload 身份有两层，`moduleName` 路径与 `absolute path` 路径需要开发者自己维持一致语义；同一个模块若同时存在 emit 驱动和 watcher 驱动，行为边界更难推断。
- `Angelscript` 的收益是 watcher、reload 分析、失败回退都围绕同一份文件身份模型展开；代价是没有 puerts 这种“先拿内存 emit 结果直接推 VM”的捷径。

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| reload 入口数量 | `ReloadModule(moduleName, jsSource)` 与 `ReloadSource(absPath, source)` 并存 | watcher 统一排 `{AbsolutePath, RelativePath}` 文件对 | 实现方式不同 |
| reload 身份模型 | 逻辑模块名 + 真实文件路径两层 | 统一文件身份模型 | Angelscript 在 reload 入口一致性上实现质量更稳定 |
| watcher 覆盖对象 | 只对已执行过且是 `*.js` 的文件建 MD5 watcher | 对脚本根内 `.as` 变更与目录增删统一入队 | puerts 在默认 watcher 面上实现更窄 |
| 回归保护 | 当前检视链路未见同等级入口统一性测试 | 去重与失败保留旧代码有自动化测试 | Angelscript 在 hot reload 入口验证上实现质量更完整 |

### [维度 D6] 代码生成与 IDE 支持：`puerts` 不只是生成 `.d.ts`，它还把 `decorator` 编译成 Blueprint metadata

前面的 D6 已经写过 `ue.d.ts`、`ue_bp.d.ts`、`ts.LanguageService` 和 package-version 缓存；这一轮补的是更靠近 authoring 语义的一层：**puerts 的代码生成链会把 TypeScript `decorator` 直接编译成 UE Blueprint metadata，而不只是生成类型文本。**

这条链路是连起来的：

- `UEMeta.ts` 把 `@uclass / @ufunction / @uparam / @uproperty` 编译成 `UE.PEClassMetaData / PEFunctionMetaData / PEParamMetaData / PEPropertyMetaData`。
- `CodeAnalyze.ts` 在构建 `PEBlueprintAsset` 时，不是只塞 pin type，而是调用 `LoadOrCreateWithMetaData()`、`AddParameterWithMetaData()`、`AddFunctionWithMetaData()`、`AddMemberVariableWithMetaData()` 把 metadata 一起下发。
- `PEBlueprintMetaData.cpp` 在 UE 侧先做 metadata key remap，再 `ValidateFormat()`，再把 `CallInEditor`、`Category`、`CompactNodeTitle`、`ToolTip`、`DeprecationMessage`、property flags 等具体写到 `UClass`、`UK2Node_FunctionEntry`、`FBPVariableDescription`。
- `UPEBlueprintAsset::Save()` 最终会 `MarkBlueprintAsModified()` 并 `CompileBlueprint()`，让 TS authoring 语义立刻变成 UE 资产 schema。

所以 puerts 在 D6 上的主产物不只是 `.d.ts`，而是一个 **“TS decorator -> UE metadata -> Blueprint schema” 的 authoring-time 编译器**。对比之下，Angelscript 当前检视到的自动生成链更保守：`AngelscriptFunctionTableExporter` 的产物是函数表分片和 skipped CSV，它解决的是绑定恢复、覆盖率统计、失败可审计性，不是一个等价的 editor metadata 编译器。

```
[puerts] Authoring Metadata Compiler
TypeScript source
 -> @uclass / @ufunction / @uparam / @uproperty
 -> UEMeta.ts compile*MetaData()
 -> PEBlueprintAsset Add*WithMetaData()
 -> PEBlueprintMetaData Validate + Remap
 -> Save + CompileBlueprint()

[Angelscript] Binding-Oriented Codegen
UHT reflection graph
 -> AngelscriptFunctionSignatureBuilder
 -> AS_FunctionTable_*.cpp
 -> SkippedEntries.csv / SkippedReasonSummary.csv
 -> runtime bind recovery
```

[1] `puerts` 先在 TypeScript 语义层把 decorator 编译成具体的 UE metadata 对象。

```ts
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/PuertsEditor/UEMeta.ts
// 位置: 1871-1938
// 说明: decorator 不只是被读取，而是被编译成 UE 侧 metadata UObject
// ============================================================================
export function compileClassMetaData(type: ts.Type): UE.PEClassMetaData | null
{
    let decorators: readonly ts.Decorator[] | undefined = undefined;
    if (type.getSymbol()?.valueDeclaration != null)
    {
        decorators = ts.getDecorators(type.getSymbol()!.valueDeclaration!);
    }
    if (decorators == null || decorators.length === 0)
    {
        return null;
    }

    let [specifiers, metaData] = getMetaDataFromDecorators(decorators as ts.NodeArray<ts.Decorator>, 'uclass');
    return processClassMetaData(specifiers, metaData); // ★ 产出不是字符串，而是 UE metadata 对象
}

export function compileFunctionMetaData(func: ts.Symbol): UE.PEFunctionMetaData | null
{
    const decorators = func.valueDeclaration != null ? ts.getDecorators(func.valueDeclaration) : null;
    if (decorators == null || decorators.length === 0)
    {
        return null;
    }

    let [specifiers, metaData] = getMetaDataFromDecorators(decorators as ts.NodeArray<ts.Decorator>, 'ufunction');
    return processFunctionMetaData(specifiers, metaData);
}
```

```ts
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/PuertsEditor/CodeAnalyze.ts
// 位置: 939-940, 1002, 1018, 1030, 1074-1089
// 说明: Blueprint 资产构建时把 metadata 和 pin type 一起下发
// ============================================================================
let bp = new UE.PEBlueprintAsset();
bp.LoadOrCreateWithMetaData(type.getSymbol().getName(), modulePath, baseTypeUClass, 0, 0, uemeta.compileClassMetaData(type));

bp.AddParameterWithMetaData(signature.parameters[i].getName(), paramPinType.pinType, paramPinType.pinValueType, uemeta.compileParamMetaData(signature.parameters[i])); // ★ 参数 metadata 同步进 Blueprint
bp.AddFunctionWithMetaData(symbol.getName(), true, undefined, undefined, flags, clearFlags, uemeta.compileFunctionMetaData(symbol));
bp.AddFunctionWithMetaData(symbol.getName(), false, resultPinType.pinType, resultPinType.pinValueType, flags, clearFlags, uemeta.compileFunctionMetaData(symbol));

let propertyMetaData = uemeta.compilePropertyMetaData(symbol);
bp.AddMemberVariableWithMetaData(
    symbol.getName(),
    propPinType.pinType,
    propPinType.pinValueType,
    Number(localFlags & 0xffffffffn),
    Number(localFlags >> 32n),
    cond,
    propertyMetaData); // ★ 属性 flags + metadata 一起进入资产 schema
```

[2] `puerts` 在 UE 侧并不是原样照抄 metadata，而是先 remap、validate，再把具体字段写进 Blueprint schema，并在保存时重新编译。

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintMetaData.cpp
// 位置: 272-303, 509-562, 621-654
// 说明: metadata 进入资产前会做 key remap、格式校验和字段级应用
// ============================================================================
bool UPEClassMetaData::SetClassMetaData(UClass* InClass)
{
    for (TPair<FName, FString>& Pair : MetaData)
    {
        FName NewKey = FMetaData::GetRemappedKeyName(CurrentKey);
        if (NewKey != NAME_None)
        {
            CurrentKey = NewKey; // ★ 旧 metadata key 会被重映射，避免 authoring 面继续写旧名
        }
    }

    for (const auto& Pair : MetaData)
    {
        FString Message;
        if (!FPEMetaDataUtils::ValidateFormat(InClass, Pair.Key, Pair.Value, Message))
        {
            UE_LOG(LogTemp, Error, TEXT("failed set meta data: %s"), *Message);
            return false; // ★ 非法 metadata 在 authoring 阶段直接失败
        }
    }

    return FPEMetaDataUtils::AddMetaData(InClass, MetaData);
}

bool UPEFunctionMetaData::Apply(UK2Node_FunctionEntry* InFunctionEntry) const
{
    InFunctionEntry->SetExtraFlags(InFunctionEntry->GetExtraFlags() | FunctionFlags);
    auto& MetaDataToSet = InFunctionEntry->MetaData;

    UpdateBooleanMetaData(TEXT("CallInEditor"), MetaData, MetaDataToSet.bCallInEditor);
    UpdateTextMetaData(TEXT("Category"), MetaData, MetaDataToSet.Category);
    UpdateTextMetaData(TEXT("CompactNodeTitle"), MetaData, MetaDataToSet.CompactNodeTitle);
    UpdateTextMetaData(TEXT("ToolTip"), MetaData, MetaDataToSet.ToolTip);
    UpdateStringMetaData(TEXT("DeprecationMessage"), MetaData, MetaDataToSet.DeprecationMessage); // ★ 节点 UI 语义直接来自 TS metadata
    return bFlagsChanged || bMetaDataChanged;
}

bool UPEPropertyMetaData::Apply(FBPVariableDescription& Element) const
{
    Element.PropertyFlags |= PropertyFlags;
    for (int Index = Element.MetaDataArray.Num() - 1; Index >= 0; --Index)
    {
        if (!MetaData.Contains(Element.MetaDataArray[Index].DataKey))
        {
            Element.MetaDataArray.RemoveAt(Index); // ★ Blueprint 变量 metadata 会被增删同步
        }
    }

    for (const auto& Pair : MetaData)
    {
        if (const auto MetaDataEntryPtr = Element.MetaDataArray.FindByPredicate(
                [Key = Pair.Key](const FBPVariableMetaDataEntry& InEntry) { return InEntry.DataKey == Key; }))
        {
            bMetaDataChanged = MetaDataEntryPtr->DataValue != Pair.Value || bMetaDataChanged;
            MetaDataEntryPtr->DataValue = Pair.Value;
        }
        else
        {
            bMetaDataChanged = true;
            Element.MetaDataArray.Emplace(Pair.Key, Pair.Value);
        }
    }
}
```

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp
// 位置: 1345-1358
// 说明: metadata 变化最终会触发 Blueprint 重新编译
// ============================================================================
void UPEBlueprintAsset::Save()
{
    auto TypeScriptGeneratedClass = Cast<UTypeScriptGeneratedClass>(GeneratedClass);
    if (Blueprint && TypeScriptGeneratedClass)
    {
        NeedSave = NeedSave || (TypeScriptGeneratedClass->HasConstructor != HasConstructor);
        if (NeedSave)
            CanChangeCheck();
        TypeScriptGeneratedClass->HasConstructor = HasConstructor;
        if (NeedSave)
        {
            FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
            FKismetEditorUtilities::CompileBlueprint(Blueprint); // ★ authoring metadata 最终进入已编译的 UE 资产
        }
    }
}
```

[3] `Angelscript` 当前检视到的自动生成链则把重点放在“函数表恢复”和“失败可审计”，而不是 decorator-style metadata 编译。

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs
// 位置: 21-54, 65-97, 99-161
// 说明: 生成链主目标是函数表与 skipped CSV，不是 editor metadata 回写
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
    WriteSkippedEntriesCsv(factory, skippedEntries);
    WriteSkippedReasonSummaryCsv(factory, skippedEntries); // ★ 重点是覆盖率与失败审计
}

if (AngelscriptFunctionSignatureBuilder.TryBuild(classObj, function, out AngelscriptFunctionSignature? signature, out string? failureReason))
{
    _ = signature!.BuildEraseMacro(); // ★ 生成的是稳定函数表键，而不是 Blueprint metadata
}
else
{
    skippedEntries.Add(new AngelscriptSkippedFunctionEntry(
        moduleName,
        classObj.SourceName,
        function.SourceName,
        string.IsNullOrEmpty(failureReason) ? "unknown" : failureReason));
}
```

设计取舍：

- `puerts` 的收益是 authoring 语义直接投影到 Blueprint 节点和变量，TS 里的 decorator 能一跳变成 UE editor 行为。
- 代价是它要同时维护 metadata key remap、格式校验、Blueprint recompilation，以及一整套 `UPE*MetaData` 资产侧桥接。
- `Angelscript` 的收益是生成链更保守、更可审计，失败原因会被 CSV 固化下来；代价是在当前检视范围内，还没有同等级的 “script annotation -> Blueprint metadata” 编译器。

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 代码生成主产物 | `.d.ts` + Blueprint metadata + 资产重编译 | `AS_FunctionTable_*.cpp` + skipped CSV | 实现方式不同 |
| metadata 校验时机 | authoring 阶段 remap + `ValidateFormat()` | 当前检视链主要校验函数签名可否重建 | puerts 在“authoring metadata 闭环”上实现更完整 |
| editor 语义落点 | `CallInEditor` / `Category` / `CompactNodeTitle` / `ToolTip` 等直接写入 Blueprint schema | 当前检视范围内未见同等级 metadata 回写链 | Angelscript 当前没有实现同等级 authoring metadata compiler |
| 失败可审计性 | metadata 非法时就地报错 | skipped CSV 汇总失败原因 | 实现方式不同，Angelscript 在导出失败审计上更显式 |

### [维度 D11] 部署与打包：`puerts` 的打包格式是 script-visible ABI，`Angelscript` 的 cache 格式基本留在宿主内部

前面的 D11 已经把 `Pak`、`.mbc/.cbc`、`PrecompiledScript.Cache`、`Binds.Cache` 的大框架写过很多轮；这一轮只补一个前文没直接点透的问题：**这些部署格式究竟是谁在消费。**

从 `FJsEnvImpl::SearchModule()` / `LoadModule()` 和 `modular.js` 一起看，puerts 的部署格式不仅被宿主消费，**还被 script-side loader 直接观察和分支**：

- `SearchModule()` 把 `[OutPath, OutDebugPath]` 明文返回给 JS。
- `LoadModule()` 遇到 `.cbc/.mbc` 返回 `ArrayBuffer`，否则返回源码字符串。
- `modular.js` 再根据 `fullPath` 的后缀决定 `ESM/CJS`、`bytecode`、`package.json` 入口、`Pak:` debug path 回退，最后再决定怎么包一层 `executeModule(...)`。

也就是说，对 puerts 来说，部署格式不是“宿主私有实现细节”，而是脚本侧可见的一部分模块 ABI。反过来看 Angelscript：`PrecompiledScript_<Config>.Cache`、`PrecompiledScript.Cache`、`Binds.Cache` 的选择、加载、校验、失效和 hot reload 禁用，都发生在 `FAngelscriptEngine` 启动期内部；脚本层并不会收到“现在用了哪个 cache 文件”的回调，也不会按缓存格式自己分支。

```
[puerts] Script-Visible Packaging ABI
JS require()
 -> searchModule(name, dir) => [fullPath, debugPath]
 -> loadModule(fullPath) => string | ArrayBuffer
 -> modular.js branches on:
    .mjs / .cjs / .mbc / .cbc / package.json / Pak:
 -> executeModule(...)

[Angelscript] Host-Private Cache ABI
FAngelscriptEngine startup
 -> choose PrecompiledScript_<Config>.Cache
 -> fallback PrecompiledScript.Cache
 -> Load() + IsValidForCurrentBuild()
 -> GetModulesToCompile()
 -> disable hot reload
 -> script code stays unaware of cache format
```

[1] `puerts` 的模块格式先通过 C++ API 暴露给 JS，再由 `modular.js` 自己决定如何消费。

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp
// 函数: FJsEnvImpl::SearchModule / LoadModule
// 位置: 4079-4129
// 说明: 宿主先把模块路径和加载结果类型暴露给 JS
// ============================================================================
void FJsEnvImpl::SearchModule(const v8::FunctionCallbackInfo<v8::Value>& Info)
{
    FString ModuleName = FV8Utils::ToFString(Isolate, Info[0]);
    FString RequiringDir = FV8Utils::ToFString(Isolate, Info[1]);
    FString OutPath;
    FString OutDebugPath;

    if (ModuleLoader->Search(RequiringDir, ModuleName, OutPath, OutDebugPath))
    {
        auto Result = v8::Array::New(Isolate);
        Result->Set(Context, 0, FV8Utils::ToV8String(Isolate, OutPath)).Check();
        Result->Set(Context, 1, FV8Utils::ToV8String(Isolate, OutDebugPath)).Check();
        Info.GetReturnValue().Set(Result); // ★ fullPath/debugPath 直接暴露给 JS 侧 loader
    }
}

void FJsEnvImpl::LoadModule(const v8::FunctionCallbackInfo<v8::Value>& Info)
{
    FString Path = FV8Utils::ToFString(Isolate, Info[0]);
    TArray<uint8> Data;
    if (!ModuleLoader->Load(Path, Data))
    {
        FV8Utils::ThrowException(Isolate, "can not load module");
        return;
    }
#if defined(WITH_V8_BYTECODE)
    if (Path.EndsWith(TEXT(".cbc")) || Path.EndsWith(TEXT(".mbc")))
    {
        v8::Local<v8::ArrayBuffer> Ab = v8::ArrayBuffer::New(Info.GetIsolate(), Data.Num());
        ::memcpy(DataTransfer::GetArrayBufferData(Ab), Data.GetData(), Data.Num());
        Info.GetReturnValue().Set(Ab); // ★ bytecode 对 JS 来说是另一种可观察返回类型
    }
    else
#endif
    {
        Info.GetReturnValue().Set(FV8Utils::ToV8StringFromFileContent(Isolate, Data));
    }
}
```

```js
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/modular.js
// 位置: 129-184
// 说明: JS 侧 loader 自己按路径后缀、Pak 和 package.json 规则分支
// ============================================================================
let moduleInfo = searchModule(moduleName, requiringDir);
let [fullPath, debugPath] = moduleInfo;
if (debugPath.startsWith("Pak: ")) {
    debugPath = fullPath; // ★ 打包后 debugPath 协议会影响脚本侧执行路径
}

let isESM = outerIsESM === true || fullPath.endsWith(".mjs") || fullPath.endsWith(".mbc");
if (fullPath.endsWith(".cjs") || fullPath.endsWith(".cbc")) isESM = false;
let script = isESM ? undefined : loadModule(fullPath);
let bytecode = undefined;
if (fullPath.endsWith(".mbc") || fullPath.endsWith(".cbc")) {
    bytecode = script;
    script = generateEmptyCode(getSourceLengthFromBytecode(bytecode)); // ★ bytecode 仍要走脚本侧包装逻辑
}

if (fullPath.endsWith(".json")) {
    let packageConfigure = JSON.parse(script);
    if (fullPath.endsWith("package.json")) {
        isESM = packageConfigure.type === "module";
        let url = packageConfigure.main || "index.js";
        let packageExports = packageConfigure.exports && packageConfigure.exports["."];
        if (packageExports)
            url =
                (packageExports["default"] && packageExports["default"]["require"]) ||
                (packageExports["require"] && packageExports["require"]["default"]) ||
                packageExports["require"];
    }
} else {
    let r = executeModule(fullPath, script, debugPath, sid, isESM, bytecode);
}
```

[2] `Angelscript` 的部署格式则主要停留在宿主内部，脚本层只看到“模块能不能编译/运行”，看不到 cache 文件和 build-specific 命名。

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 1513-1557, 2046-2056
// 说明: cache 文件选择、加载、失效与 hot reload 禁用都发生在宿主内部
// ============================================================================
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
            PrecompiledData = nullptr; // ★ cache 失效在宿主内部被处理掉
        }
    }
}

if (PrecompiledData != nullptr && bUsePrecompiledData && !bScriptDevelopmentMode)
{
    ModulesToCompile = PrecompiledData->GetModulesToCompile();
    UE_LOG(Angelscript, Warning, TEXT("Using fully precompiled scripts. Hot reloading is disabled for this run."));
    UE_LOG(Angelscript, Warning, TEXT("Delete PrecompiledScript.Cache or run with -as-development-mode flag to enable hot reload.")); // ★ script 层不会自己决定 cache 协议
}
```

设计取舍：

- `puerts` 的收益是 JS 模块系统非常灵活，脚本侧可以继续复用 `package.json`、`ESM/CJS`、`Pak`、`bytecode` 这些 package layout 语义。
- 代价是部署格式本身成了 script-visible ABI；后端切换、扩展名策略、debug path 协议的变化，都可能改变脚本侧 loader 行为。
- `Angelscript` 的收益是 cache 协议对脚本作者几乎不可见，构建一致性和运行时行为边界更清楚；代价是灵活性小很多，而且启用 fully precompiled scripts 时会直接牺牲 hot reload。

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 部署格式可见性 | `fullPath/debugPath`、`string/ArrayBuffer` 都暴露给 JS loader | cache 文件选择和失效逻辑都留在 `FAngelscriptEngine` 内部 | 实现方式不同 |
| 模块入口判定位置 | script-side `modular.js` 再按 `.mjs/.cjs/.mbc/.cbc/package.json/Pak:` 分支 | host-side 直接选择 `PrecompiledScript_<Config>.Cache` / fallback | 实现方式不同 |
| 脚本层对打包协议的耦合 | 高，package layout 是正式 ABI | 低，script 看不到 cache 协议细节 | Angelscript 在部署协议封装性上实现质量更稳定 |
| 热重载与部署的关系 | loader 协议和热更协议同时暴露给脚本侧 | fully precompiled 模式直接由宿主禁用 hot reload | 实现方式不同 |

---

## 深化分析 (2026-04-09 07:21:28)

### [维度 D3] Blueprint 交互：`puerts` 把 Blueprint 当作 TS authoring 的投影层，运行时重定向只覆盖 `BlueprintEvent`

这一轮不重复“都能创建 Blueprint 子类”这件事，只补一个前文没掰开的边界：`puerts` 的 Blueprint 交互是 **authoring-time schema 编译 + runtime event redirection** 两段式，而且两段的能力边界并不相同。

- 在 authoring 阶段，`CodeAnalyze.ts` 会把 TypeScript class 的函数、属性、attachment 和 metadata 直接写进 `UPEBlueprintAsset`，再立刻 `Save()`。
- 到 runtime 阶段，`UTypeScriptGeneratedClass::ObjectInitialize()` 并不会把所有函数都接管给 JS；它只对“非 native 且带 `FUNC_BlueprintEvent`”的方法装 `execLazyLoadCallJS`。
- 更关键的是，源码注释直接写明当前链路是“蓝图继承 ts 类”，并明确说“目前不支持 ts 继承蓝图”。所以 `puerts` 的混合继承链不是双向互操作，而是 **TS class -> Blueprint child** 这一条单向 authoring 流。

```
[puerts] Blueprint Authoring and Runtime Split
TS class
 -> CodeAnalyze.ts
 -> UPEBlueprintAsset schema write
 -> Blueprint asset saved/recompiled
 -> UTypeScriptGeneratedClass::StaticConstructor
 -> redirect BlueprintEvent only

[Angelscript] Script Parent and Blueprint Child
script UASClass
 -> native CreateBlueprint flow
 -> Blueprint child asset
 -> automation test spawns child class
 -> verify parent override still executes
```

[1] `puerts` 先把 TS 语义投影成 Blueprint 资产结构，函数/属性/attachment 都来自 authoring 阶段。

```ts
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/PuertsEditor/CodeAnalyze.ts
// 位置: 936-940, 1018, 1030, 1089, 1096-1098
// 说明: TypeScript 语义分析直接驱动 Blueprint schema 写入
// ============================================================================
function onBlueprintTypeAddOrChange(baseTypeUClass: UE.Class, type: ts.Type, modulePath:string) {
    let bp = new UE.PEBlueprintAsset();
    bp.LoadOrCreateWithMetaData(type.getSymbol().getName(), modulePath, baseTypeUClass, 0, 0, uemeta.compileClassMetaData(type));

    bp.AddFunctionWithMetaData(symbol.getName(), true, undefined, undefined, flags, clearFlags, uemeta.compileFunctionMetaData(symbol));   // ★ 函数定义直接回写 Blueprint
    bp.AddFunctionWithMetaData(symbol.getName(), false, resultPinType.pinType, resultPinType.pinValueType, flags, clearFlags, uemeta.compileFunctionMetaData(symbol));
    bp.AddMemberVariableWithMetaData(symbol.getName(), propPinType.pinType, propPinType.pinValueType, Number(localFlags & 0xffffffffn), Number(localFlags >> 32n), cond, propertyMetaData); // ★ 属性 flags / metadata 一起落盘
    bp.SetupAttachments(attachments);   // ★ 组件挂接关系也来自 TS decorator / annotation
    bp.Save();                          // ★ 资产写回在 authoring 阶段完成
}
```

[2] `puerts` 到 runtime 只对 `BlueprintEvent` 走惰性重定向，而且注释明确限制了继承方向。

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/TypeScriptGeneratedClass.cpp
// 位置: 123-136, 169-218
// 说明: 运行时重定向只接管 BlueprintEvent，且明确注明“不支持 ts 继承蓝图”
// ============================================================================
void UTypeScriptGeneratedClass::StaticConstructor(const FObjectInitializer& ObjectInitializer)
{
    auto Class = ObjectInitializer.GetClass();

    // 蓝图继承ts类，既然进了这里，表明链上必然有ts类，由于目前不支持ts继承蓝图，所以顶部节点往下找的第一个UTypeScriptGeneratedClass就是本类
    while (Class)
    {
        if (auto TypeScriptGeneratedClass = Cast<UTypeScriptGeneratedClass>(Class))
        {
            TypeScriptGeneratedClass->ObjectInitialize(ObjectInitializer);
            break;
        }
        Class = Class->GetSuperClass();
    }
}

void UTypeScriptGeneratedClass::ObjectInitialize(const FObjectInitializer& ObjectInitializer)
{
    auto Object = ObjectInitializer.GetObj();
    if (auto SuperTypeScriptGeneratedClass = Cast<UTypeScriptGeneratedClass>(GetSuperClass()))
    {
        SuperTypeScriptGeneratedClass->ObjectInitialize(ObjectInitializer);
    }
    else
    {
        GetSuperClass()->ClassConstructor(ObjectInitializer);
    }

    auto PinedDynamicInvoker = DynamicInvoker.Pin();
    if (PinedDynamicInvoker)
    {
        PinedDynamicInvoker->TsConstruct(this, Object); // ★ 先完成 TS 构造，再决定哪些函数需要重定向
    }
    if (UNLIKELY(!RedirectedToTypeScript))
    {
        for (TFieldIterator<UFunction> FuncIt(this, EFieldIteratorFlags::ExcludeSuper); FuncIt; ++FuncIt)
        {
            auto Function = *FuncIt;
            // 只有蓝图可重写的方法才需要设置LazyLoadCallJS重写
            if (!Function->IsNative() && Function->HasAnyFunctionFlags(FUNC_BlueprintEvent))
            {
                TempNativeFuncStorage.Add(Function->GetFName(), Function->GetNativeFunc());
                Function->FunctionFlags |= FUNC_Native;
                Function->SetNativeFunc(&UTypeScriptGeneratedClass::execLazyLoadCallJS); // ★ 只拦截 BlueprintEvent
                AddNativeFunction(*Function->GetName(), &UTypeScriptGeneratedClass::execLazyLoadCallJS);
            }
        }
        RedirectedToTypeScript = true;
    }
}
```

[3] `Angelscript` 当前检视到的 Blueprint 交互不是用脚本语义去重写 Blueprint schema，而是把 script parent -> Blueprint child 链路当成稳定运行场景，用自动化测试守住。

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Blueprint/AngelscriptBlueprintSubclassRuntimeTests.cpp
// 位置: 181-209, 211-276
// 说明: Blueprint 子类继承 script parent override 的行为被做成显式回归测试
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAngelscriptScenarioBlueprintChildInheritsScriptBeginPlayTest,
    "Angelscript.TestModule.BlueprintChild.InheritsScriptBeginPlay",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAngelscriptScenarioBlueprintChildOverrideChainHasDeterministicCountsTest,
    "Angelscript.TestModule.BlueprintChild.OverrideChainHasDeterministicCounts",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptScenarioBlueprintChildInheritsScriptBeginPlayTest::RunTest(const FString& Parameters)
{
    UClass* ScriptParentClass = CompileScriptModule(
        *this,
        Engine,
        ModuleName,
        TEXT("ScenarioBlueprintChildInheritsScriptBeginPlay.as"),
        TEXT(R"AS(
UCLASS()
class AScenarioBlueprintChildInheritsScriptBeginPlayParent : AActor
{
    UPROPERTY()
    int BeginPlayCount = 0;

    UFUNCTION(BlueprintOverride)
    void BeginPlay()
    {
        BeginPlayCount += 1;
    }
}
)AS"),
        TEXT("AScenarioBlueprintChildInheritsScriptBeginPlayParent"));
    BlueprintSubclassRuntimeTest::FScopedTransientBlueprint Blueprint;
    if (!Blueprint.CreateAndCompile(*this, ScriptParentClass, TEXT("InheritsScriptBeginPlay")))
    {
        return false;
    }

    UClass* BlueprintClass = Blueprint.GetGeneratedClass();
    AActor* Actor = SpawnScriptActor(*this, Spawner, BlueprintClass);
    BeginPlayActor(Engine, *Actor);

    int32 BeginPlayCount = 0;
    if (!ReadPropertyValue<FIntProperty>(*this, Actor, TEXT("BeginPlayCount"), BeginPlayCount))
    {
        return false;
    }

    TestEqual(TEXT("Blueprint child should inherit and execute the script BeginPlay override"), BeginPlayCount, 1); // ★ 子 Blueprint 保留 script override
    return true;
}
```

设计取舍：

- `puerts` 的优点是 script authoring 几乎就是 Blueprint schema 的事实来源，TS 改完即可回写资产。
- 代价是 runtime 接管面被严格限制在 `BlueprintEvent`，而且继承方向由实现写死为 `TS class -> Blueprint child`。
- `Angelscript` 的优点是把 Blueprint child 当独立 UE 资产来维护，并用自动化场景验证 override 链、状态隔离和重建稳定性。
- 代价是当前检视范围内没有看到 puerts 那种“脚本语义即资产 schema”的 authoring compiler。

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| Blueprint schema 权威来源 | `CodeAnalyze.ts` 直接写 `UPEBlueprintAsset` | Blueprint 资产按原生 Kismet 路线创建，行为靠运行时/测试保证 | 实现方式不同 |
| runtime 重定向范围 | 只对非 native `FUNC_BlueprintEvent` 装 `execLazyLoadCallJS` | 前文与测试可见的主链是 script parent + Blueprint child 正常运行 | 实现方式不同 |
| 混合继承边界声明 | 源码注释显式写明“不支持 ts 继承蓝图” | 当前检视范围内同样以 script parent -> Blueprint child 为主 | 不是“没有实现”，而是都选择了单向链路 |
| Blueprint 子链回归保障 | 当前插件源码范围内未见同等级自动化回归文件 | `Angelscript.TestModule.BlueprintChild.*` 系列显式覆盖 | Angelscript 在 Blueprint 子链回归验证上实现更完整 |

### [维度 D7] 编辑器集成：`puerts` 把 editor workflow 托管给常驻 `JsEnv`，`Angelscript` 则做成原生 delegate + commandlet 工具体系

这一轮补的是“编辑器动作到底由谁拥有”。`puerts` 的答案是：**由 editor-side script runtime 自己拥有**。`FPuertsEditorModule::OnPostEngineInit()` 注册 `UTypeScriptBlueprint` 的自定义编译器，然后直接创建一个 editor-side `FJsEnv`，启动 `PuertsEditor/CodeAnalyze`。后续 Blueprint 生成、schema 更新、attachment 回写，本质上都由这套常驻 JS authoring runtime 驱动。`PEBlueprintAsset` 甚至明确禁止 PIE 中改布局或新建类，说明 editor workflow 的一致性约束被写进了这条 authoring 链。

Angelscript 则把编辑器动作拆成原生 UE 工具面：

- runtime 侧只暴露 `GetEditorCreateBlueprint()` 这类 delegate；
- debug server 收到 `CreateBlueprint` 消息时广播 delegate；
- editor module 用原生对话框和 `FKismetEditorUtilities::CreateBlueprint(...)` 执行；
- `BlueprintImpactScanCommandlet` 则把“受影响 Blueprint 扫描”做成 headless 命令行入口，返回明确 exit code。

也就是说，`puerts` 的 editor integration 更像“把一台 TS compiler 嵌进 UE 编辑器”；`Angelscript` 更像“把编辑器能力做成原生服务，并额外提供 commandlet 入口”。

```
[puerts] Editor-Owned By Script Runtime
PostEngineInit
 -> RegisterCompilerForBP(UTypeScriptBlueprint)
 -> create editor FJsEnv
 -> Start("PuertsEditor/CodeAnalyze")
 -> JS/TS analysis mutates Blueprint assets
 -> PIE guard blocks layout edits

[Angelscript] Editor-Owned By Native Tools
DebugServer / menu / commandlet
 -> FAngelscriptRuntimeModule delegates
 -> FAngelscriptEditorModule native handlers
 -> modal asset dialog / CreateBlueprint
 -> BlueprintImpactScanCommandlet for headless scan
```

[1] `puerts` 在 editor 启动时直接拉起一套常驻 `JsEnv`，并把 `UTypeScriptBlueprint` 的编译器也挂上去。

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp
// 位置: 116-151
// 说明: editor-side FJsEnv 常驻运行，PuertsEditor/CodeAnalyze 是正式工作流入口
// ============================================================================
void FPuertsEditorModule::OnPostEngineInit()
{
    if (Enabled)
    {
        FKismetCompilerContext::RegisterCompilerForBP(UTypeScriptBlueprint::StaticClass(), &MakeCompiler); // ★ 自定义 Blueprint 编译器

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
        JsEnv = MakeShared<PUERTS_NAMESPACE::FJsEnv>(
            std::make_shared<PUERTS_NAMESPACE::DefaultJSModuleLoader>(TEXT("JavaScript")),
            std::make_shared<PUERTS_NAMESPACE::FDefaultLogger>(), -1,
            [this](const FString& InPath)
            {
                if (SourceFileWatcher.IsValid())
                {
                    SourceFileWatcher->OnSourceLoaded(InPath);
                }
            },
            TEXT("--max-old-space-size=2048"));

        JsEnv->Start("PuertsEditor/CodeAnalyze"); // ★ 编辑器工作流主入口不是 C++ tool，而是 JS authoring runtime
    }
}
```

[2] `puerts` 的 Blueprint authoring 保护也写在这条 editor 工作流里，PIE 中禁止改布局或新建类。

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp
// 位置: 57-64, 119-156
// 说明: authoring 工作流对 PIE 有显式写保护
// ============================================================================
#define CanChangeCheckWithBoolRet()                                                                        \
    if (IsPlaying())                                                                                       \
    {                                                                                                      \
        UE_LOG(PuertsEditorModule, Error, TEXT("change the layout of class[%s] in PIE mode is forbiden!"), \
            *GeneratedClass->GetName());                                                                   \
        NeedSave = false;                                                                                  \
        return false;                                                                                      \
    }

if (IsPlaying())
{
    UE_LOG(PuertsEditorModule, Error, TEXT("create class[%s] in PIE mode is forbiden!"), *InName);
    return false; // ★ PIE 中直接拒绝 authoring 写入
}

Blueprint = FKismetEditorUtilities::CreateBlueprint(
    ParentClass, Package, *InName, BlueprintType, BlueprintClass, BlueprintGeneratedClass, FName("PuertsAutoGen"));
```

[3] `Angelscript` 把“创建 Blueprint”做成 runtime delegate + native editor handler，不需要 editor-side script VM 常驻接管。

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h
// 位置: 19-21, 45-47
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 位置: 1172-1181
// 说明: runtime 只广播编辑器动作，真正执行留给 editor module
// ============================================================================
DECLARE_MULTICAST_DELEGATE_TwoParams(FAngelscriptDebugListAssets, TArray<FString>, class UASClass*);
DECLARE_MULTICAST_DELEGATE_OneParam(FAngelscriptEditorCreateBlueprint, class UASClass*);
DECLARE_DELEGATE_RetVal_OneParam(FString, FAngelscriptEditorGetCreateBlueprintDefaultAssetPath, class UASClass*);

static FAngelscriptDebugListAssets& GetDebugListAssets();
static FAngelscriptEditorCreateBlueprint& GetEditorCreateBlueprint();
static FAngelscriptEditorGetCreateBlueprintDefaultAssetPath& GetEditorGetCreateBlueprintDefaultAssetPath();

else if (MessageType == EDebugMessageType::CreateBlueprint)
{
    FAngelscriptCreateBlueprint CreateBlueprint;
    *Datagram << CreateBlueprint;

    auto ClassDesc = FAngelscriptEngine::Get().GetClass(CreateBlueprint.ClassName);
    if (ClassDesc.IsValid() && Cast<UASClass>(ClassDesc->Class) != nullptr)
    {
        FAngelscriptRuntimeModule::GetEditorCreateBlueprint().Broadcast(Cast<UASClass>(ClassDesc->Class)); // ★ runtime 只发信号
    }
}
```

[4] `Angelscript` 额外把 Blueprint impact 扫描做成 commandlet，适合 headless/CI 工作流。

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp
// 位置: 55-120
// 说明: Blueprint 影响面扫描是独立命令行入口，而不是 editor-side script session
// ============================================================================
int32 UAngelscriptBlueprintImpactScanCommandlet::Main(const FString& Params)
{
    if (!FAngelscriptEngine::Get().bDidInitialCompileSucceed)
    {
        UE_LOG(Angelscript, Error, TEXT("Blueprint impact commandlet requires a successfully initialized Angelscript engine."));
        return static_cast<int32>(EBlueprintImpactCommandletExitCode::EngineNotReady);
    }

    const AngelscriptEditor::BlueprintImpact::FBlueprintImpactScanResult ScanResult =
        AngelscriptEditor::BlueprintImpact::ScanBlueprintAssets(FAngelscriptEngine::Get(), AssetRegistryModule.Get(), Request);

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
        ScanResult.FailedAssetLoads); // ★ 直接输出机器可读摘要

    return ScanResult.FailedAssetLoads > 0
        ? static_cast<int32>(EBlueprintImpactCommandletExitCode::AssetScanFailure)
        : static_cast<int32>(EBlueprintImpactCommandletExitCode::Success);
}
```

设计取舍：

- `puerts` 的收益是 editor authoring、TS 语义分析、Blueprint 生成共用一套 script runtime，authoring 体验非常一体化。
- 代价是编辑器能力依赖常驻 `JsEnv` 和 JS toolchain，PIE 安全边界也只能通过 authoring 链内部守住。
- `Angelscript` 的收益是工具入口更 UE 化，runtime/editor/commandlet 之间边界清楚，便于做 headless 自动化。
- 代价是在当前检视范围内，编辑器 authoring 不像 puerts 那样由脚本语义直接驱动 Blueprint schema。

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 编辑器工作流拥有者 | editor-side `FJsEnv` + `PuertsEditor/CodeAnalyze` | native editor module + runtime delegate | 实现方式不同 |
| Blueprint 创建执行面 | `UPEBlueprintAsset` / `UTypeScriptBlueprint` 编译链内完成 | `GetEditorCreateBlueprint()` 广播后由 editor module 原生执行 | 实现方式不同 |
| PIE 写保护 | `PEBlueprintAsset` 显式拒绝布局变更和新建类 | 当前新增证据主要显示 native tool 分层，而非同类 schema 写保护 | 不是“没有实现”，而是工具面设计不同 |
| headless/CI 入口 | 当前检视范围内未见同等级 commandlet | `BlueprintImpactScanCommandlet` 提供独立 exit code 和机器可读日志 | Angelscript 在 editor automation 入口上实现更完整 |

### [维度 D9] 测试基础设施：`puerts` 在插件内更像内联防线，`Angelscript` 把测试做成运行时能力和独立模块

这一轮补的不是“Angelscript 测试多”这种废话，而是**测试是不是产品面的一部分**。

对 `Reference/puerts/unreal/Puerts/Source` 与 `Reference/puerts/unreal/Puerts/PuertsEditor` 做源码树扫描后，我没有命中 `AutomationTest`、`IMPLEMENT_*AUTOMATION_TEST`、`Misc/AutomationTest.h`、`RunTest(...)` 这类 UE automation 标记；同时 `Puerts.uplugin` 的模块清单里也没有 test module。这个判断是**基于当前 UE 插件子树源码的推断**，不是说整个 puerts 仓库绝对没有测试。但至少在这层交付物里，能直接看到的质量防线更像是 `PEBlueprintAsset`/`CodeAnalyze.ts` 里的 authoring 保护和 runtime 日志，而不是一套正式暴露给 UE Automation / commandlet 的测试面。

Angelscript 则明显把测试纳入运行时主链：

- `Angelscript.uplugin` 直接声明 `AngelscriptTest` 模块；
- `FAngelscriptEngine` 在活跃模块上发现 unit/integration tests；
- hot reload 后会根据依赖关系挑模块、分批跑测试，还专门处理 PhysX 卡死风险；
- `FAngelscriptUnitTests` 把测试暴露给 UE Automation；
- `UAngelscriptTestCommandlet` 又给 CI/headless 执行一条正式入口。

这意味着 Angelscript 的测试不是“仓外脚本”或“开发者手工流程”，而是插件 runtime contract 的一部分。

```
[puerts] Test Surface In Inspected UE Plugin Scope
Puerts.uplugin
 -> Runtime + Editor modules only
 -> inline authoring/runtime guards
 -> source-tree scan: no AutomationTest markers   // 基于当前子树扫描的推断

[Angelscript] Test Surface As Product Capability
Angelscript.uplugin
 -> AngelscriptTest module
 -> DiscoverUnitTests / DiscoverIntegrationTests
 -> HotReloadTestRunner PrepareTests / RunTests
 -> Automation wrapper + TestCommandlet
```

[1] `puerts` 在当前 UE 插件层的模块清单里没有独立测试模块。

```jsonc
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Puerts.uplugin
// 位置: 15-49
// 说明: 当前 UE 插件层模块只有 runtime/editor/program，没有 test module
// ============================================================================
"Modules": [
  { "Name": "WasmCore", "Type": "Runtime" },
  { "Name": "JsEnv", "Type": "Runtime" },
  { "Name": "DeclarationGenerator", "Type": "Editor" },
  { "Name": "ParamDefaultValueMetas", "Type": "Program" },
  { "Name": "Puerts", "Type": "Runtime" },
  { "Name": "PuertsEditor", "Type": "Editor" }
]
```

[2] `Angelscript` 则把测试模块显式放进插件清单，而且测试模块本身就是正式 UE module。

```jsonc
// ============================================================================
// 文件: Plugins/Angelscript/Angelscript.uplugin
// 位置: 18-33
// 说明: Tests 是插件一级模块，而不是仓外附属目录
// ============================================================================
"Modules": [
    { "Name": "AngelscriptRuntime", "Type": "Runtime", "LoadingPhase": "PostDefault" },
    { "Name": "AngelscriptEditor", "Type": "Editor", "LoadingPhase": "PostDefault" },
    { "Name": "AngelscriptTest", "Type": "Editor", "LoadingPhase": "PostDefault" } // ★ 独立测试模块
]
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTestModule.cpp
// 位置: 5-16
// 说明: AngelscriptTest 是正式模块，不是零散样例代码
// ============================================================================
IMPLEMENT_MODULE(FAngelscriptTestModule, AngelscriptTest);

void FAngelscriptTestModule::StartupModule()
{
    UE_LOG(LogAngelscriptTest, Log, TEXT("AngelscriptTest module started."));
}

void FAngelscriptTestModule::ShutdownModule()
{
    UE_LOG(LogAngelscriptTest, Log, TEXT("AngelscriptTest module shut down."));
}
```

[3] `Angelscript` 的运行时会发现测试、在 hot reload 后择优排队，并提供 automation / commandlet 两种执行面。

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 2245-2248, 2481-2489
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Testing/UnitTest.cpp
// 位置: 531-654, 662-756
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptTestCommandlet.cpp
// 位置: 5-24
// 说明: 测试发现、hot reload 批调度、Automation、Commandlet 都在产品代码里
// ============================================================================
for (auto& ActiveModule : GetActiveModules())
{
    DiscoverUnitTests(*ActiveModule, ActiveModule->UnitTestFunctions);
    DiscoverIntegrationTests(*ActiveModule, ActiveModule->IntegrationTestFunctions); // ★ 运行时直接发现测试
}

if (GEngine && bCompletedAssetScan && HotReloadTestRunner != nullptr && HotReloadTestRunner->ShouldRunUnitTestsOnHotReload())
{
    HotReloadTestRunner->PrepareTests(GetActiveModules(), CompiledModules, RelativeFileList, ShouldUseAutomaticImportMethod()); // ★ 热重载后按依赖挑测试
}

void FHotReloadTestRunner::PrepareTests(
    const TArray<TSharedRef<struct FAngelscriptModuleDesc>>& ActiveModules,
    const TArray<TSharedRef<FAngelscriptModuleDesc>>& ModulesToCompile,
    const TArray<FString>& FileList,
    bool AutomaticImport)
{
    if (!ShouldRunUnitTestsOnHotReload())
    {
        return;
    }

    for (const TSharedRef<FAngelscriptModuleDesc>& Module : ModulesToCompile)
    {
        if (Module->UnitTestFunctions.Num() > 0)
        {
            TestAfterHotReload.Add(Module); // ★ 待跑测试直接挂在模块描述上
        }
    }
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(
    FAngelscriptUnitTests, "Angelscript.UnitTests",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter); // ★ 暴露给 UE Automation

int32 UAngelscriptTestCommandlet::Main(const FString& Params)
{
    if (!RunAngelscriptUnitTests(FAngelscriptEngine::Get().GetActiveModules(), &FAngelscriptEngine::Get(), 0, 0))
    {
        return 2; // ★ headless/CI 也有正式入口
    }
    return 0;
}
```

设计取舍：

- `puerts` 的收益是插件表面更轻，主交付物集中在 runtime/editor 功能，不额外暴露一层 test module。
- 代价是当前 UE 插件子树里看不到同等级 automation/commandlet 测试面，质量保障更多落在 authoring 保护和运行时日志上。
- `Angelscript` 的收益是测试发现、hot reload 回归、UE Automation、commandlet 全部进入产品代码，适合长期维护和 CI。
- 代价是 runtime 本身要承担更多测试调度职责，模块面也更重。

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 插件级测试模块 | 当前 `Puerts.uplugin` 未声明 test module | `Angelscript.uplugin` 声明 `AngelscriptTest` | puerts 当前没有实现同等级独立测试模块 |
| 运行时测试发现 | 当前检视范围内未见 `Discover*Tests` 主链 | `FAngelscriptEngine` 直接发现 unit/integration tests | Angelscript 在测试发现闭环上实现更完整 |
| hot reload 后回归执行 | 当前检视范围内未见同等级模块级批调度 | `PrepareTests()` / `RunTests()` 按依赖与批次运行 | Angelscript 在热重载回归闭环上实现更完整 |
| Automation / CI 入口 | 基于源码树扫描未见同等级 UE Automation / commandlet 入口 | `Angelscript.UnitTests` + `UAngelscriptTestCommandlet` | Angelscript 在自动化执行面上实现更完整 |

---

## 深化分析 (2026-04-09 07:32:01)

### [维度 D1] 插件架构与模块划分：`puerts` 的多后端抽象真正落在 `IJsEnv + pesapi ABI`，不只是 `Build.cs` 里切宏

前几轮已经确认 `puerts` 模块拆分更多。这一轮往下钻，能看到它的“多后端”并不是单纯编译期宏开关，而是明确做成了三层边界：

- 第一层是 `IJsEnv` 统一门面，把 `Start`、`ReloadModule`、`ReloadSource`、`TryBindJs`、`RebindJs`、`WaitDebugger`、GC 通知都收敛成同一组虚函数。
- 第二层是 `FJsEnvGroup` 扇出层，在同一份 `IJSModuleLoader` 上创建多个 `FJsEnvImpl`，再把 `GroupDynamicInvoker` 注回各个实例，说明多 VM 并发也是抽象设计的一部分，而不是宿主临时拼出来的逻辑。
- 第三层是 `pesapi` ABI。`PesapiAddonLoad.cpp` 不是简单 `LoadLibrary`；它把整张 `pesapi_func_ptr` 表交给 addon，找不到 `dynamic` 入口时还会回退查 `PESAPI_MODULE_VERSION()`，直接拒绝 ABI 版本不匹配的扩展。

Angelscript 当前插件的扩展边界明显更“内聚”。`FAngelscriptEngine` 自己同时持有配置、JIT、预编译数据、bind database 和调试状态；`FAngelscriptBindDatabase::Get()` 也是先找当前 engine，再回退到一个 legacy static database。也就是说，Angelscript 不是没有抽象，而是抽象边界放在**当前进程内的 engine shared state**，不是 `puerts` 这种版本化脚本后端 ABI。

```
[puerts] Runtime Abstraction Stack
host module
 -> IJsEnv facade
 -> FJsEnv wrapper
 -> FJsEnvImpl / FJsEnvGroup
 -> pesapi function table
 -> backend runtime / addon dll

[Angelscript] Runtime Abstraction Stack
host module
 -> FAngelscriptEngine::Get()
 -> engine-owned shared state
 -> BindDatabase / StaticJIT / PrecompiledData
 -> embedded AngelScript runtime
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JsEnv.h:25-101`、`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvGroup.cpp:95-171`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JsEnv.h
// 位置: 25-101，多后端统一门面
// ============================================================================
class JSENV_API IJsEnv
{
public:
    virtual void Start(const FString& ModuleName, const TArray<TPair<FString, UObject*>>& Arguments) = 0;
    virtual void LowMemoryNotification() = 0;
    virtual void WaitDebugger(double Timeout) = 0;
    virtual void TryBindJs(const class UObjectBase* InObject) = 0;
    virtual void RebindJs() = 0;
    virtual void ReloadModule(FName ModuleName, const FString& JsSource) = 0;
    virtual void ReloadSource(const FString& Path, const PString& JsSource) = 0;
    virtual void OnSourceLoaded(std::function<void(const FString&)> Callback) = 0;
    // ★ VM 生命周期、热更、调试、绑定全部收敛到同一接口
};

class JSENV_API FJsEnv
{
private:
    std::unique_ptr<IJsEnv> GameScript; // ★ 宿主只依赖接口实现，不直接依赖具体 backend 类
};

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvGroup.cpp
// 位置: 95-171，多环境扇出层
// ============================================================================
FJsEnvGroup::FJsEnvGroup(int Size, std::shared_ptr<IJSModuleLoader> InModuleLoader, std::shared_ptr<ILogger> InLogger,
    int InDebugStartPort, std::function<void(const FString&)> InOnSourceLoadedCallback, const FString InFlags,
    void* InExternalRuntime, void* InExternalContext)
{
    std::shared_ptr<IJSModuleLoader> SharedModuleLoader = std::move(InModuleLoader);
    for (int i = 0; i < Size; i++)
    {
        JsEnvList.push_back(std::make_shared<FJsEnvImpl>(SharedModuleLoader, InLogger, InDebugStartPort + i,
            InOnSourceLoadedCallback, InFlags, InExternalRuntime, InExternalContext));
    }
    Init();
}

void FJsEnvGroup::Init()
{
    std::vector<FJsEnvImpl*> JsEnvs;
    for (int i = 0; i < JsEnvList.size(); i++)
    {
        JsEnvs.push_back(static_cast<FJsEnvImpl*>(JsEnvList[i].get()));
    }
    auto GroupDynamicInvoker = MakeShared<FGroupDynamicInvoker, ESPMode::ThreadSafe>(JsEnvs);
    for (int i = 0; i < JsEnvs.size(); i++)
    {
        JsEnvs[i]->TsDynamicInvoker = GroupDynamicInvoker;
        JsEnvs[i]->MixinInvoker = GroupDynamicInvoker; // ★ group 层负责把统一调用器注入多个 env
    }
}
```

[2] `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/pesapi.h:18,63-104`、`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PesapiAddonLoad.cpp:99-126,141-165`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Public/pesapi.h
// 位置: 18, 63-104，addon ABI 自带版本号
// ============================================================================
#define PESAPI_VERSION 11
#define PESAPI_MODULE_INITIALIZER(modname) PESAPI_MODULE_INITIALIZER_X(PESAPI_MODULE_INITIALIZER_BASE, modname, PESAPI_VERSION)
#define PESAPI_MODULE_VERSION() PESAPI_MODULE_INITIALIZER_X(PESAPI_MODULE_INITIALIZER_BASE, version, 0)

PESAPI_MODULE_EXPORT const char* PESAPI_MODULE_INITIALIZER(dynamic)(pesapi_func_ptr * func_ptr_array)
{
    if (func_ptr_array)
    {
        pesapi_init(func_ptr_array);
        initfunc();
    }
    return #modname;
}

PESAPI_MODULE_EXPORT int PESAPI_MODULE_VERSION()()
{
    return PESAPI_VERSION; // ★ addon 自报 ABI 版本
}

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PesapiAddonLoad.cpp
// 位置: 99-126, 141-165，运行时加载 addon 并校验 ABI
// ============================================================================
auto Init = (const char* (*) (pesapi_func_ptr*) )(uintptr_t) FPlatformProcess::GetDllExport(DllHandle, *EntryName);
if (Init)
{
    const char* ModuleName = Init(nullptr);
    GPesapiModuleName = ModuleName;
    Init(funcs); // ★ 把整张 pesapi 函数表交给 addon
    GPesapiModuleName = nullptr;
}
else
{
    auto Ver = (int (*)())(uintptr_t) FPlatformProcess::GetDllExport(DllHandle, *VersionEntryName);
    int PesapiVersion = Ver();
    FV8Utils::ThrowException(Info.GetIsolate(),
        FString::Printf(TEXT("pesapi version mismatch, expect: %d, but got %d"), PESAPI_VERSION, PesapiVersion));
    // ★ 没有正确入口时不会“尽量凑合”，而是直接拒绝不兼容扩展
}
```

[3] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:119-170,541-567`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp:14-24`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h
// 位置: 119-170, 541-567，运行时共享状态集中在单一 engine 上
// ============================================================================
struct ANGELSCRIPTRUNTIME_API FAngelscriptEngine
{
    static FAngelscriptEngine& Get();
    static FString GetScriptRootDirectory();

    bool bUseAutomaticImportMethod = false;
    struct FAngelscriptPrecompiledData* PrecompiledData = nullptr;
    struct FAngelscriptStaticJIT* StaticJIT = nullptr;
    bool bUsePrecompiledData = false;

    FAngelscriptTypeDatabase* GetTypeDatabase() const;
    FAngelscriptBindState* GetBindState() const;
    FAngelscriptBindDatabase* GetBindDatabase() const;
    // ★ 配置、类型、绑定、JIT、缓存都挂在当前 engine shared state 上
};

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp
// 位置: 14-24，bind database 依赖当前 engine 上下文
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
    return LegacyBindDatabase; // ★ 回退也是进程内对象，不是外部 ABI 扩展点
}
```

设计取舍：

- `puerts` 的收益是后端、addon、单 VM/多 VM 都可以压在统一门面后面，替换性强。
- 代价是 ABI、loader、group invoker、模块加载器多层并存，理解和调试成本明显更高。
- Angelscript 的收益是 engine 内部状态集中，调试、JIT、预编译、bind database 更容易统一推进。
- 代价是当前插件没有暴露同等级的后端替换面，也没有 `puerts` 这种外部 addon ABI。

与 Angelscript 对比：

| 对比点 | puerts 新证据 | Angelscript 新证据 | 差距判断 |
| --- | --- | --- | --- |
| 运行时统一门面 | `IJsEnv` 把启动、热更、调试、GC 都做成统一接口 | `FAngelscriptEngine` 统一拥有运行时共享状态 | 实现方式不同 |
| 多环境并发抽象 | `FJsEnvGroup` 共享 loader 并批量扇出多个 `FJsEnvImpl` | 当前主链仍以当前 engine/shared state 为中心 | Angelscript 没有实现同等级多 VM 门面 |
| 扩展 ABI | `pesapi` addon 通过函数表 + 版本号接入 | 当前主链未见等价外部 ABI | Angelscript 没有实现 |
| 兼容性拒绝点 | ABI 版本不匹配直接拒绝加载 addon | 兼容性主要落在 engine 内部状态与 cache 校验 | 实现方式不同 |

### [维度 D6] 代码生成与 IDE 支持：`puerts` 把 TS compiler 变成 authoring compiler，`Angelscript` 把 diagnostics/navigation 做成结构化服务

上一轮已经确认 `puerts` 有 `.d.ts` 和 `LanguageService`。这一轮真正新增的发现是：在 `puerts` UE 插件层里，IDE 支持和资产 authoring 根本不是两条链，而是**同一次 TS compile/emit 的两个副作用**。`CodeAnalyze.ts` 先跑 `ts.getPreEmitDiagnostics()`，然后 `program.emit(...)` 同时产出 `.js`、触发 `PuertsNotifyChange` 热更通知，并把 Blueprint 刷新任务排队；后续 `onBlueprintTypeAddOrChange(...)` 直接把 TS AST 里的函数签名、返回值、泛型容器、decorator/meta 编译成 `PEBlueprintAsset` 的 pin、函数和属性。

换句话说，`puerts` 的“IDE 支持”不是停留在提示层，而是已经演变成一个 **TS authoring compiler**。但它在当前 UE 插件子树里的诊断载体仍然偏轻：`diagnostics` 主要被 `logErrors()` 打到 console，没有看到与 `.d.ts` 同等级的结构化诊断协议或持久化导出。

Angelscript 走的是另一条路。它没有做等价的 TS AST -> Blueprint schema 编译器，但把**诊断、跳定义、数据库同步**做成了正式服务面：按文件缓存 `Diagnostics`，只在 dirty 时通过 debug server 发送 `EDebugMessageType::Diagnostics`；同时 `GoToDefinition()` 能把 script/global/bound type/property 统一映射回 `SourceCodeNavigation`；甚至 `StateDump` 还能把当前 diagnostics 导出成 `Diagnostics.csv`。这不是“没有 IDE 支持”，而是**把 IDE 集成放在运行时元数据服务**上。

```
[puerts] Authoring Compiler Loop
ts.Program
 -> getPreEmitDiagnostics()
 -> emit .js / .mjs
 -> PuertsNotifyChange()
 -> queue blueprint refresh jobs
 -> TS AST to pin/function/property
 -> UPEBlueprintAsset mutation

[Angelscript] IDE Service Loop
compile / reload
 -> collect per-file diagnostics
 -> EmitDiagnostics() over debug server
 -> SendDebugDatabase / GoToDefinition
 -> FSourceCodeNavigation
 -> optional Diagnostics.csv dump
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/PuertsEditor/CodeAnalyze.ts:405-419,547-555,566-585,936-1032`

```ts
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/PuertsEditor/CodeAnalyze.ts
// 位置: 405-419, 547-555, 566-585, 936-1032
// ============================================================================
let diagnostics = ts.getPreEmitDiagnostics(program);
if (diagnostics.length > 0) {
    logErrors(diagnostics); // ★ 诊断先走 console 输出
}

const diagnostics = [
    ...program.getSyntacticDiagnostics(sourceFile),
    ...program.getSemanticDiagnostics(sourceFile)
];
if (diagnostics.length > 0) {
    logErrors(diagnostics); // ★ 增量编译同样主要是日志回显
}

emitOutput.outputFiles.forEach(output => {
    if (doEmitJs) {
        UE.FileSystemOperation.WriteFile(output.name, output.text); // ★ 同一轮 compile 直接产出 JS
    }
    if (output.name.endsWith(".js") || output.name.endsWith(".mjs")) {
        jsSource = output.text;
    }
});
if (moduleFileName && reload) {
    UE.FileSystemOperation.PuertsNotifyChange(moduleFileName, jsSource); // ★ 同一轮 compile 直接通知 runtime 热更
}

pendingBlueprintRefleshJobs.push({ type: foundType, op: () => onBlueprintTypeAddOrChange(foundBaseTypeUClass, foundType, modulePath) });

function onBlueprintTypeAddOrChange(baseTypeUClass: UE.Class, type: ts.Type, modulePath:string) {
    let bp = new UE.PEBlueprintAsset();
    bp.LoadOrCreateWithMetaData(type.getSymbol().getName(), modulePath, baseTypeUClass, 0, 0, uemeta.compileClassMetaData(type));

    // ★ 直接把 TS 签名转成 Blueprint pin / function / metadata
    bp.AddParameterWithMetaData(signature.parameters[i].getName(), paramPinType.pinType, paramPinType.pinValueType, uemeta.compileParamMetaData(signature.parameters[i]));
    bp.AddFunctionWithMetaData(symbol.getName(), false, resultPinType.pinType, resultPinType.pinValueType, flags, clearFlags, uemeta.compileFunctionMetaData(symbol));
}
```

[2] `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:87-169,394-420`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp
// 位置: 87-169, 394-420，TS authoring compiler 的资产落点
// ============================================================================
bool UPEBlueprintAsset::LoadOrCreate(
    const FString& InName, const FString& InPath, UClass* ParentClass, int32 InSetFlags, int32 InClearFlags)
{
    Blueprint = LoadObject<UBlueprint>(nullptr, *PackageName, nullptr, LOAD_NoWarn | LOAD_NoRedirects);
    if (Blueprint)
    {
        GeneratedClass = Blueprint->GeneratedClass;
        if (Blueprint->ParentClass != ParentClass)
        {
            CanChangeCheckWithBoolRet();
            Blueprint->ParentClass = ParentClass;
            NeedSave = true;
        }
        return true;
    }

    Blueprint = FKismetEditorUtilities::CreateBlueprint(
        ParentClass, Package, *InName, BlueprintType, BlueprintClass, BlueprintGeneratedClass, FName("PuertsAutoGen"));
    // ★ Blueprint 资产并不是手工附带物，而是 compile 链直接写出的产物
}

void UPEBlueprintAsset::AddFunction(FName InName, bool IsVoid, FPEGraphPinType InGraphPinType, FPEGraphTerminalType InPinValueType,
    int32 InSetFlags, int32 InClearFlags)
{
    InSetFlags &= ~InClearFlags;
    InSetFlags &= ~FUNC_Native;
    // ★ 后续逻辑直接进入 graph/function 结构修改，而不是只做类型提示
}
```

[3] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:4469-4518,4928-5083`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:1288-1370`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp:718-745`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 4469-4518, 4928-5083，结构化诊断是正式运行时状态
// ============================================================================
void FAngelscriptEngine::EmitDiagnostics(class FSocket* Client)
{
    for (auto Iterator = Diagnostics.CreateIterator(); Iterator; ++Iterator)
    {
        if (Iterator->Value.Diagnostics.Num() == 0)
        {
            if (Iterator->Value.bHasEmittedAny || Iterator->Value.bIsCompiling)
                EmitDiagnostics(Iterator->Value, Client);
            // ★ 空诊断也会发一次，通知客户端清空旧状态
        }
        else
        {
            EmitDiagnostics(Iterator->Value, Client);
            Iterator->Value.bHasEmittedAny = true;
        }
    }
    bDiagnosticsDirty = false;
}

auto& FileDiagnostics = Diagnostics.FindOrAdd(AbsoluteFilename);
FileDiagnostics.Filename = AbsoluteFilename;
FileDiagnostics.Diagnostics.Add(Diagnostic);
bDiagnosticsDirty = true; // ★ 诊断按文件入库，等 debug server 增量发送

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 位置: 1288-1370，跳定义是协议能力，不是控制台日志
// ============================================================================
if (ScriptFunction != nullptr)
{
    UFunction* UnrealFunction = FAngelscriptDocs::LookupAngelscriptFunction(ScriptFunction->GetId());
    if (UnrealFunction != nullptr)
    {
        FSourceCodeNavigation::NavigateToFunction(UnrealFunction); // ★ 绑定函数直接跳回 UE 源码
        return;
    }
}

FProperty* Property = AssociatedClass->FindPropertyByName(*GoTo.SymbolName);
if (Property != nullptr)
{
    FSourceCodeNavigation::NavigateToProperty(Property); // ★ 属性也在同一条定义跳转链里
    return;
}

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp
// 位置: 718-745，诊断还能导出为 CSV
// ============================================================================
for (const TPair<FString, FAngelscriptEngine::FDiagnostics>& DiagnosticsPair : Engine.Diagnostics)
{
    for (const FAngelscriptEngine::FDiagnostic& Diagnostic : DiagnosticsPair.Value.Diagnostics)
    {
        Writer.AddRow({
            DiagnosticsPair.Key,
            LexToString(Diagnostic.Row),
            LexToString(Diagnostic.Column),
            BoolToString(Diagnostic.bIsError),
            BoolToString(Diagnostic.bIsInfo),
            Diagnostic.Message
        });
    }
}
```

设计取舍：

- `puerts` 的收益是“提示、编译、热更、资产 authoring”共用一份 TS 语义树，工作流非常一体化。
- 代价是在当前 UE 插件层里，诊断主要停留在 console 反馈，没有看到同等级结构化诊断通道。
- Angelscript 的收益是诊断、导航、状态导出全都做成正式协议/数据结构，适合外部 IDE 或工具持续同步。
- 代价是当前仓库范围内没有 `puerts` 这种 AST 驱动的 Blueprint schema 编译器。

与 Angelscript 对比：

| 对比点 | puerts 新证据 | Angelscript 新证据 | 差距判断 |
| --- | --- | --- | --- |
| IDE 主链的终点 | 同一次 TS compile 直接产出 JS、热更通知和 Blueprint 资产 | 运行时缓存 diagnostics/debug database，再经协议同步 | 实现方式不同 |
| 诊断载体 | `logErrors()` 为主，当前范围内未见同等级结构化诊断协议 | `Diagnostics` 按文件缓存并通过 `EDebugMessageType::Diagnostics` 发送 | puerts 在结构化诊断服务上当前没有实现 |
| Blueprint schema 权威来源 | `CodeAnalyze.ts` 直接把 TS AST 编译成 `PEBlueprintAsset` | 当前主链未见等价 AST -> Blueprint compiler | Angelscript 当前没有实现 |
| 跳定义与导出 | 当前主证据仍偏 TS/console/asset 编译链 | `GoToDefinition` + `Diagnostics.csv` + `FSourceCodeNavigation` | Angelscript 在 IDE 元数据服务化上实现更完整 |

### [维度 D8] 性能与优化：`puerts` 的生命周期优化围绕 JS handle 所有权，`Angelscript` 的内存优化围绕 UE GC schema

前两轮 D8 主要看了 FastCall 和 StaticJIT。本轮补的是**内存/生命周期**这条线，因为这决定了调用桥接之外的长期运行成本。

`puerts` 的做法是典型 VM-centric 模型。`pesapi_impl::Object` 持有 `env_ref + value_ref`，拷贝时 duplicate，析构时 release；`SetWeakAndOwnBy()` 会把 JS 值挂到 native owner 上，再把 ref 置为 weak。落到 V8 层后，`FCppObjectMapper::BindCppObject()` 会把 `JSObject` 绑进缓存，并按 `PassByPointer` 决定是否在弱回调里执行 `Finalize`；同时 `TraceObjectLifecycle()` 还允许每个类型注册 `OnEnter/OnExit`，把对象进入/退出 JS 世界的时机开放出来。

Angelscript 的重心则不在“谁拥有 JS handle”，而在“脚本对象怎么被 UE GC 认识”。`UASClass` 直接带 `ReferenceSchema`；`DetectAngelscriptReferences()` 会扫描脚本属性，把有引用的非 primitive/non-inherited property 编进 `UE::GC::FSchemaBuilder`；对 `PersistentInstance`、`InstancedReference` 的容器和结构体还会向内冒泡设置引用标记。也就是说，Angelscript 选择让**UE collector** 成为脚本对象生命周期的裁判者。

```
[puerts] Lifetime Model
native object / pesapi value
 -> create env_ref + value_ref
 -> optional SetWeakAndOwnBy(owner)
 -> CppObjectMapper cache
 -> V8 weak callback
 -> Finalize / OnExit

[Angelscript] Lifetime Model
script property graph
 -> DetectAngelscriptReferences()
 -> UE::GC::FSchemaBuilder
 -> ReferenceSchema on UASClass
 -> PersistentInstance / InstancedReference flags
 -> UE collector walks script-owned references
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/PesapiObject.hpp:42-79,189-199`、`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PesapiV8Impl.cpp:639-642`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Public/PesapiObject.hpp
// 位置: 42-79, 189-199，脚本值引用与 owner 绑定
// ============================================================================
class Object
{
public:
    Object(pesapi_env env, pesapi_value value)
    {
        if (!env || !value || IsNullOrUndefined(env, value))
        {
            env_holder = nullptr;
            value_holder = nullptr;
        }
        else
        {
            env_holder = pesapi_create_env_ref(env);
            value_holder = pesapi_create_value_ref(env, value, 0);
        }
    }

    Object(const Object& InOther)
    {
        env_holder = pesapi_duplicate_env_ref(InOther.env_holder);
        value_holder = pesapi_duplicate_value_ref(InOther.value_holder);
    }

    ~Object()
    {
        pesapi_release_value_ref(value_holder);
        pesapi_release_env_ref(env_holder); // ★ 对象析构时主动释放脚本环境和值引用
    }

    void SetWeakAndOwnBy(const T* Owner)
    {
        auto owner = pesapi_native_object_to_value(env, StaticTypeId<T>::get(), Owner, false);
        auto val = pesapi_get_value_from_ref(env, value_holder);
        if (pesapi_set_owner(env, val, owner))
        {
            pesapi_set_ref_weak(env, value_holder); // ★ 让 JS handle 退化为 weak 引用
        }
    }
};

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PesapiV8Impl.cpp
// 位置: 639-642，V8 层弱引用落点
// ============================================================================
void pesapi_set_ref_weak(pesapi_env env, pesapi_value_ref value_ref)
{
    value_ref->value_persistent.SetWeak(); // ★ 最终真的落到 V8 persistent handle 的 weak 状态
}
```

[2] `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/CppObjectMapper.cpp:282-333,370-380`、`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JSClassRegister.cpp:80-100,337-344`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/CppObjectMapper.cpp
// 位置: 282-333, 370-380，弱回调 + 生命周期钩子
// ============================================================================
static void CDataGarbageCollectedWithFree(const v8::WeakCallbackInfo<JSClassDefinition>& Data)
{
    JSClassDefinition* ClassDefinition = Data.GetParameter();
    void* Ptr = DataTransfer::MakeAddressWithHighPartOfTwo(Data.GetInternalField(0), Data.GetInternalField(1));
    if (ClassDefinition->Finalize)
        ClassDefinition->Finalize(Ptr, ClassDefinition->Data, DataTransfer::GetIsolatePrivateData(Data.GetIsolate()));
    DataTransfer::IsolateData<ICppObjectMapper>(Data.GetIsolate())->UnBindCppObject(Data.GetIsolate(), ClassDefinition, Ptr);
}

void FCppObjectMapper::BindCppObject(
    v8::Isolate* Isolate, JSClassDefinition* ClassDefinition, void* Ptr, v8::Local<v8::Object> JSObject, bool PassByPointer)
{
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

    if (ClassDefinition->OnEnter)
    {
        CacheNodePtr->UserData = ClassDefinition->OnEnter(Ptr, ClassDefinition->Data, DataTransfer::GetIsolatePrivateData(Isolate));
    }
}

void FCppObjectMapper::UnBindCppObject(v8::Isolate* Isolate, JSClassDefinition* ClassDefinition, void* Ptr)
{
    if (ClassDefinition->OnExit)
    {
        ClassDefinition->OnExit(Ptr, ClassDefinition->Data, DataTransfer::GetIsolatePrivateData(Isolate), Iter->second.UserData);
    }
    // ★ 对象离开 JS 世界时，既执行 finalize，又执行 OnExit
}

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JSClassRegister.cpp
// 位置: 80-100, 337-344，生命周期钩子由类型注册表持有
// ============================================================================
const JSClassDefinition* LoadClassByID(const void* TypeId)
{
    auto clsDef = FindClassByID(TypeId);
    if (!clsDef && ClassNotFoundCallback)
    {
        if (!ClassNotFoundCallback(TypeId))
        {
            return nullptr;
        }
        clsDef = FindClassByID(TypeId);
    }
    return clsDef;
}

bool TraceObjectLifecycle(const void* TypeId, pesapi_on_native_object_enter OnEnter, pesapi_on_native_object_exit OnExit)
{
    if (auto clsDef = const_cast<JSClassDefinition*>(GetJSClassRegister()->FindClassByID(TypeId)))
    {
        clsDef->OnEnter = OnEnter;
        clsDef->OnExit = OnExit; // ★ 生命周期回调是类型级能力
        return true;
    }
    return false;
}
```

[3] `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h:27-35,74-77`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4852-4924,3004-3096`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h
// 位置: 27-35, 74-77，脚本类直接带 UE GC schema
// ============================================================================
UClass* ComposeOntoClass = nullptr;
void* ScriptTypePtr = nullptr;
bool bIsScriptClass = false;
UE::GC::FSchemaOwner ReferenceSchema; // ★ 生命周期信息直接挂在 UASClass 上

virtual UClass* GetMostUpToDateClass();
virtual void RuntimeAddReferencedObjects(UObject* InThis, FReferenceCollector& Collector) {}

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 位置: 4852-4924, 3004-3096，把 script property 编进 UE GC schema
// ============================================================================
static UE::GC::ObjectAROFn GetARO(UClass* Class)
{
    UE::GC::ObjectAROFn ARO = Class->CppClassStaticFunctions.GetAddReferencedObjects();
    return ARO != &UObject::AddReferencedObjects ? ARO : nullptr;
}

void FAngelscriptClassGenerator::DetectAngelscriptReferences(TSharedPtr<FAngelscriptClassDesc> ClassDesc)
{
    UE::GC::FSchemaBuilder Schema(0);
    FAngelscriptType::FGCReferenceParams RefParams;
    RefParams.Class = Class;
    RefParams.Schema = &Schema;

    Schema.Append(Class->ReferenceSchema.Get());
    for (int32 i = 0, PropertyCount = ScriptType->GetPropertyCount(); i < PropertyCount; ++i)
    {
        if (TypeId <= asTYPEID_LAST_PRIMITIVE || ScriptType->IsPropertyInherited(i))
            continue;

        if (!bAddedAsUnrealProperty && PropertyType.HasReferences())
        {
            RefParams.AtOffset = PropertyOffset;
            RefParams.Names.Push(Name);
            PropertyType.EmitReferenceInfo(RefParams); // ★ 把脚本引用写进 UE GC schema
            RefParams.Names.Pop();
        }
    }

    UE::GC::FSchemaView View(Schema.Build(GetARO(Class)), UE::GC::EOrigin::Other);
    Class->ReferenceSchema.Set(View);
}

if (PropDesc->bPersistentInstance)
{
    MarkUStructContainsReference(); // ★ 持久实例会被显式标记成引用持有者
}

if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(NewProperty))
{
    if(PropDesc->bPersistentInstance)
    {
        ApplyInstancedPropertyFlags(ArrayProp, ArrayProp->Inner); // ★ 容器内部也要冒泡引用/实例标记
    }
}
```

设计取舍：

- `puerts` 的收益是对象何时进入/退出脚本世界、何时执行 finalize、何时降级为 weak，都有显式钩子。
- 代价是生命周期规则分散在 `pesapi` ref、V8 weak callback、object mapper、owner 绑定多个层面。
- Angelscript 的收益是直接借 UE GC schema 追踪脚本引用，`PersistentInstance`/容器引用也能进入同一条 collector 路径。
- 代价是对象生命周期更多受 UE 反射/GC 体系约束，不像 `puerts` 那样能从 VM 侧单独钩住 enter/exit。

与 Angelscript 对比：

| 对比点 | puerts 新证据 | Angelscript 新证据 | 差距判断 |
| --- | --- | --- | --- |
| 生命周期主导方 | `env_ref/value_ref` + weak handle + finalize/OnExit | `ReferenceSchema` + UE collector + instanced flags | 实现方式不同 |
| 类型级生命周期钩子 | `TraceObjectLifecycle()` 明确支持 `OnEnter/OnExit` | 当前主链未见等价类型级 enter/exit hook | Angelscript 当前没有实现 |
| 容器/持久实例引用传播 | 当前主证据偏 JS handle 与对象缓存 | `PersistentInstance` 会对 array/map/set/struct 冒泡引用标记 | Angelscript 在 UE GC 一体化上实现更完整 |
| 跨 VM 对象复用 | `CppObjectMapper` 缓存同一 native ptr 对应 JS object | 当前优化重点不在“外部 VM handle 复用” | 实现方式不同 |

### [维度 D11] 部署与打包：`puerts` 的兼容性门槛在 `pesapi` ABI，`Angelscript` 的兼容性门槛在 precompiled cache 与 JIT GUID

前两轮 D11 主要讲了 DLL 跟包和 `PrecompiledScript.Cache`。这一轮新增的关键点是：两边其实都做了**版本/兼容性闸门**，只是闸门卡住的对象完全不同。

`puerts` 的闸门卡的是**外部扩展 ABI**。`pesapi.h` 把 `PESAPI_VERSION` 写死在导出宏里，addon 必须同时导出 `PESAPI_MODULE_INITIALIZER(dynamic)` 和 `PESAPI_MODULE_VERSION()`。`PesapiAddonLoad.cpp` 加载 DLL 后，先找 `dynamic` 入口；找不到才去查版本号并抛 `version mismatch`。这说明 puerts 的部署兼容性不只关心“DLL 有没有跟包”，还关心“扩展是否与当前 runtime ABI 对齐”。

Angelscript 的闸门则卡在**内部生成物**。`PrecompiledData` 会在生成期写入 `BuildIdentifier` 和新的 `DataGuid`；运行时如果发现 cache 的 build configuration 不匹配，直接丢弃整份 precompiled data；如果 cache 能用但静态编进游戏二进制的 `FStaticJITCompiledInfo::PrecompiledDataGuid` 对不上，就清空 JIT database，仅保留解释/非 JIT 路径继续跑。也就是说，Angelscript 防的是“旧 cache / 旧 transpiled code 混入当前包”。

```
[puerts] Compatibility Gate
addon dll
 -> find dynamic entry
 -> pass pesapi function table
 -> fallback to PESAPI_MODULE_VERSION()
 -> reject ABI mismatch

[Angelscript] Compatibility Gate
PrecompiledScript.Cache
 -> check BuildIdentifier
 -> attach StaticJIT
 -> compare PrecompiledDataGuid
 -> drop cache or clear JIT database
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/pesapi.h:18,63-104`、`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PesapiAddonLoad.cpp:99-126,141-165`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Public/pesapi.h
// 位置: 18, 63-104，导出宏把 ABI 版本编码进模块入口
// ============================================================================
#define PESAPI_VERSION 11
#define PESAPI_MODULE_INITIALIZER(modname) PESAPI_MODULE_INITIALIZER_X(PESAPI_MODULE_INITIALIZER_BASE, modname, PESAPI_VERSION)
#define PESAPI_MODULE_VERSION() PESAPI_MODULE_INITIALIZER_X(PESAPI_MODULE_INITIALIZER_BASE, version, 0)

PESAPI_MODULE_EXPORT const char* PESAPI_MODULE_INITIALIZER(dynamic)(pesapi_func_ptr * func_ptr_array)
{
    if (func_ptr_array)
    {
        pesapi_init(func_ptr_array);
        initfunc();
    }
    return #modname;
}

PESAPI_MODULE_EXPORT int PESAPI_MODULE_VERSION()()
{
    return PESAPI_VERSION; // ★ runtime 可以单独询问 addon 的 ABI 版本
}

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PesapiAddonLoad.cpp
// 位置: 99-126, 141-165，部署期真正执行 ABI 兼容性拒绝
// ============================================================================
auto Init = (const char* (*) (pesapi_func_ptr*) )(uintptr_t) FPlatformProcess::GetDllExport(DllHandle, *EntryName);
if (Init)
{
    const char* ModuleName = Init(nullptr);
    GPesapiModuleName = ModuleName;
    Init(funcs); // ★ 入口存在时才把函数表注入 addon
}
else
{
    auto Ver = (int (*)())(uintptr_t) FPlatformProcess::GetDllExport(DllHandle, *VersionEntryName);
    int PesapiVersion = Ver();
    FV8Utils::ThrowException(Info.GetIsolate(),
        FString::Printf(TEXT("pesapi version mismatch, expect: %d, but got %d"), PESAPI_VERSION, PesapiVersion));
    // ★ ABI 不匹配直接拒绝，不做降级兼容
}
```

[2] `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp:2620-2645`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h:74-79`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1529-1555`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp
// 位置: 2620-2645，cache 自带 build 配置与数据 GUID
// ============================================================================
DataGuid = FGuid::NewGuid();

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
    return BuildIdentifier == GetCurrentBuildIdentifier() && BuildIdentifier != -1; // ★ 先按 build 配置硬校验
}

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h
// 位置: 74-79，静态编进二进制的 JIT 信息也带 GUID
// ============================================================================
struct ANGELSCRIPTRUNTIME_API FStaticJITCompiledInfo
{
    FGuid PrecompiledDataGuid;
    FStaticJITCompiledInfo(FGuid Guid);
    static const FStaticJITCompiledInfo* Get();
};

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 1529-1555，cache 与 transpiled code 分层校验
// ============================================================================
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
        FJITDatabase::Get().Clear(); // ★ cache 可保留，但不兼容的 JIT 代码会被单独熔断
    }
}
```

设计取舍：

- `puerts` 的收益是外部 addon 只要满足 `pesapi` ABI 就能插入当前 runtime，扩展模型更开放。
- 代价是 ABI 版本不兼容时只能硬拒绝，部署方需要自己管理 addon/runtime 同步升级。
- Angelscript 的收益是对内部生成物做了两层闸门，旧 cache 和旧 JIT 代码不会静默混用。
- 代价是部署链路更偏自研产物管理，第三方扩展边界不像 `puerts` 那样显式开放。

与 Angelscript 对比：

| 对比点 | puerts 新证据 | Angelscript 新证据 | 差距判断 |
| --- | --- | --- | --- |
| 兼容性闸门对象 | `pesapi` addon ABI | `PrecompiledData` build id + `StaticJIT` GUID | 实现方式不同 |
| 失败行为 | ABI 不匹配直接拒绝加载 addon | build 不匹配丢 cache，GUID 不匹配仅熔断 JIT | 实现方式不同 |
| 部署边界 | 面向外部动态扩展与运行时注入 | 面向内部生成 cache / transpiled code | 实现方式不同 |
| 版本守卫能力 | 当前范围内明确有 `PESAPI_VERSION` 守卫 | 当前范围内明确有 `BuildIdentifier`/`PrecompiledDataGuid` 守卫 | 不是“谁有谁没有”，而是守卫层级不同 |

---

## 深化分析 (2026-04-09 08:18:24)

### [维度 D1] 插件架构与模块划分：`puerts` 在 UE 反射层其实维护了两套 `generated class` 家族，`Angelscript` 则让 `UASClass` 一类到底

前几轮 D1 主要集中在模块和 backend。继续往 UE 反射层里钻，能看到更细的一层架构差异：`puerts` 在类对象层面并不是一条链，而是至少拆成了两套家族。

- 一套是 **package-backed authoring family**：`UTypeScriptBlueprint -> UTypeScriptGeneratedClass`。它服务于编辑器产物和持久化资产。
- 另一套是 **transient runtime overlay family**：`UJSGeneratedClass/UJSWidgetGeneratedClass -> UJSGeneratedFunction`。它服务于运行时 JS 覆写与 mixin。

`Angelscript` 则不同。当前主链里只有 `UASClass` 这一类核心载体，同时背负代码超类、构造函数、默认值函数、GC schema、默认组件和热重载版本链。也就是说，puerts 把“资产编译产物”和“运行时函数补丁”拆开了；Angelscript 把这些责任压在同一类族上。

```
[puerts] UE Class Families
├─ Authoring Family                          // 资产化、可持久化
│  ├─ UTypeScriptBlueprint                   // Blueprint 资产载体
│  ├─ FTypeScriptCompilerContext             // 自定义编译上下文
│  └─ UTypeScriptGeneratedClass              // 落在资产 package 内
└─ Runtime Overlay Family                    // 运行时补丁、非持久化
   ├─ UJSGeneratedClass / UJSWidgetGeneratedClass
   ├─ transient UBlueprint                   // 仅为反射关系补齐
   └─ UJSGeneratedFunction                   // 函数级覆写 / mixin

[Angelscript] Single Class Family
└─ UASClass
   ├─ CodeSuperClass / ScriptTypePtr
   ├─ ConstructFunction / DefaultsFunction
   ├─ ReferenceSchema / DefaultComponents
   └─ NewerVersion                           // 热重载版本链
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/TypeScriptBlueprint.h:18-26`、`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/TypeScriptBlueprint.cpp:7-10`、`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/TypeScriptCompilerContext.cpp:17-31`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Public/TypeScriptBlueprint.h
// 位置: 18-26，专用 Blueprint 资产类型
// ============================================================================
UCLASS()
class JSENV_API UTypeScriptBlueprint : public UBlueprint
{
    GENERATED_BODY()
public:
#if WITH_EDITOR
    virtual UClass* GetBlueprintClass() const override;   // ★ 资产类型自己声明生成类类型
#endif
};

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/TypeScriptBlueprint.cpp
// 位置: 7-10，生成类固定落到 UTypeScriptGeneratedClass
// ============================================================================
UClass* UTypeScriptBlueprint::GetBlueprintClass() const
{
    return UTypeScriptGeneratedClass::StaticClass();
}

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/TypeScriptCompilerContext.cpp
// 位置: 17-31，把生成类放进 Blueprint 自己的 package
// ============================================================================
void FTypeScriptCompilerContext::SpawnNewClass(const FString& NewClassName)
{
    NewClass = FindObject<UTypeScriptGeneratedClass>(Blueprint->GetOutermost(), *NewClassName);
    if (NewClass == NULL)
    {
        NewClass = NewObject<UTypeScriptGeneratedClass>(
            Blueprint->GetOutermost(), FName(*NewClassName), RF_Public | RF_Transactional);
        // ★ authoring 家族的生成类是 package-backed 的长期资产
    }
}
```

[2] `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JSGeneratedClass.cpp:18-65,87-177`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JSGeneratedClass.cpp
// 位置: 18-65, 87-177，运行时 overlay 家族走 transient package
// ============================================================================
UClass* UJSGeneratedClass::Create(const FString& Name, UClass* Parent, ...)
{
    auto Outer = GetTransientPackage();                    // ★ 不是资产 package，而是 transient
    auto JSGeneratedClass = NewObject<UJSGeneratedClass>(Outer, *Name, RF_Public);
    JSGeneratedClass->Constructor = v8::UniquePersistent<v8::Function>(Isolate, Constructor);
    JSGeneratedClass->Prototype = v8::UniquePersistent<v8::Object>(Isolate, Prototype);
    JSGeneratedClass->ClassConstructor = &UJSGeneratedClass::StaticConstructor;

    auto Blueprint = NewObject<UBlueprint>(Outer);         // ★ 仅补一层 Blueprint 反射关系
    Blueprint->GeneratedClass = Class;
    Class->SetSuperStruct(Parent);
    Class->ClassFlags |= CLASS_CompiledFromBlueprint;
    return Class;
}

void UJSGeneratedClass::Override(v8::Isolate* Isolate, UClass* Class, UFunction* Super, ...)
{
    if (Existed)
    {
        Super->Rename(*FString::Printf(TEXT("%s%s"), TEXT("__puerts_old__"), *Super->GetName()), Class,
            REN_DontCreateRedirectors | REN_DoNotDirty | REN_ForceNoResetLoaders);
        // ★ 旧函数先改名，再复制出新的 UJSGeneratedFunction
    }

    UJSGeneratedFunction* Function = Cast<UJSGeneratedFunction>(
        StaticDuplicateObject(Super, Class, FunctionName, RF_Transient, UJSGeneratedFunction::StaticClass()));
    Function->SetNativeFunc(&UJSGeneratedFunction::execCallJS);
    Function->FunctionTranslator = std::make_unique<PUERTS_NAMESPACE::FFunctionTranslator>(Function, false);
    Class->AddFunctionToFunctionMap(Function, Function->GetFName());
}
```

[3] `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h:14-35`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:507-517`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h
// 位置: 14-35，UASClass 把运行时元数据集中在同一类里
// ============================================================================
class ANGELSCRIPTRUNTIME_API UASClass : public UClass
{
    GENERATED_BODY()
public:
    UClass* CodeSuperClass = nullptr;
    UASClass* NewerVersion = nullptr;          // ★ 热重载版本链
    class asIScriptFunction* ConstructFunction;
    class asIScriptFunction* DefaultsFunction;
    void* ScriptTypePtr = nullptr;
    UE::GC::FSchemaOwner ReferenceSchema;      // ★ GC schema 也直接挂在类上
};

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 位置: 507-517，编辑器直接以 UASClass 为父类创建标准 Blueprint
// ============================================================================
IKismetCompilerInterface& KismetCompilerModule = FModuleManager::LoadModuleChecked<IKismetCompilerInterface>("KismetCompiler");
KismetCompilerModule.GetBlueprintTypesForClass(Class, BlueprintClass, BlueprintGeneratedClass);
Asset = FKismetEditorUtilities::CreateBlueprint(
    Class, Package, AssetName, BPTYPE_Normal,
    BlueprintClass, BlueprintGeneratedClass, FName("AngelscriptCreateBlueprint")
);
// ★ 没有额外的 Blueprint subclass；资产路径直接围绕 UASClass 展开
```

设计取舍：

- `puerts` 的收益是把“编辑器资产生成”和“运行时函数补丁”拆成两条演进速度不同的链，互相污染更少。
- 代价是同一个“JS/TS 脚本类”在 UE 层并不只有一种落地形态，排查类行为时要先分清自己站在哪条家族上。
- `Angelscript` 的收益是 `UASClass` 作为单一事实来源，编辑器、构造、GC、热重载都围绕同一类对象运转。
- 代价是 `UASClass` 责任很重，单点复杂度和类对象膨胀明显更高。

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 资产化类族 | `UTypeScriptBlueprint -> UTypeScriptGeneratedClass` | 直接以 `UASClass` 为父类创建标准 Blueprint | 实现方式不同 |
| 运行时补丁类族 | `UJSGeneratedClass/UJSGeneratedFunction` 走 transient overlay | 当前主链未见等价第二套 generated-class 家族 | Angelscript 当前没有实现 |
| 类对象职责分配 | 责任分散到两套家族 | `UASClass` 单类承载构造、GC、热重载、组件 | 实现方式不同 |

### [维度 D6] 代码生成与 IDE 支持：`puerts` 的类型安全会继续下沉到 Blueprint 资产类型选择，且覆盖范围不是完全统一的

前几轮 D6 已经把 `.d.ts`、`LanguageService`、`CodeAnalyze` 写得比较细。本轮新增的关键点是：`puerts` 的 authoring contract 不只停在离线声明，还会体现在 **UE 资产本身选用哪一种 Blueprint 类型**。

从 `UPEBlueprintAsset::LoadOrCreate()` 可以看到，`puerts` 并不是对所有父类都统一使用 `UTypeScriptBlueprint`。它只在 **非 Actor 父类** 上选择 `UTypeScriptBlueprint`；Actor 父类仍然使用标准 `UBlueprint`，但生成类仍强制落到 `UTypeScriptGeneratedClass`。再配合 `FPuertsEditorModule::OnPostEngineInit()` 只给 `UTypeScriptBlueprint` 注册专用 compiler，可以推断：`puerts` 的“资产级类型安全”是**部分专门化**的，而不是所有 Blueprint 一刀切。

Angelscript 当前主链则完全走标准 Kismet 资产路径：`ShowCreateBlueprintPopup()` 只根据 `UASClass` 去问 `KismetCompiler` 要 `BlueprintClass/BlueprintGeneratedClass`，再创建普通 Blueprint 资产。也就是说，Angelscript 的 authoring contract 主要放在父类和运行时生成链里，而不是额外造一层 Blueprint 资产类型。

```
[puerts] Authoring Contract in Asset Type
TS schema / decorators
 -> UPEBlueprintAsset::LoadOrCreate
    ├─ Actor parent      -> UBlueprint
    └─ non-Actor parent  -> UTypeScriptBlueprint
 -> generated class always UTypeScriptGeneratedClass
 -> custom compiler only for UTypeScriptBlueprint

[Angelscript] Authoring Contract in Parent Class
script class (UASClass)
 -> IKismetCompilerInterface::GetBlueprintTypesForClass
 -> standard UBlueprint asset
 -> runtime/generated semantics stay on UASClass side
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:127-155`、`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/TypeScriptBlueprint.cpp:7-10`、`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:116-120`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp
// 位置: 127-155，按父类决定 Blueprint 资产类型
// ============================================================================
UClass* BlueprintClass = UBlueprint::StaticClass();
UClass* BlueprintGeneratedClass = UTypeScriptGeneratedClass::StaticClass();

if (!ParentClass->IsChildOf(AActor::StaticClass()))
{
    BlueprintClass = UTypeScriptBlueprint::StaticClass();   // ★ 只有 non-Actor 才走专用 Blueprint 资产
}

Blueprint = FKismetEditorUtilities::CreateBlueprint(
    ParentClass, Package, *InName, BlueprintType,
    BlueprintClass, BlueprintGeneratedClass, FName("PuertsAutoGen"));

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/TypeScriptBlueprint.cpp
// 位置: 7-10，专用 Blueprint 资产固定返回 UTypeScriptGeneratedClass
// ============================================================================
UClass* UTypeScriptBlueprint::GetBlueprintClass() const
{
    return UTypeScriptGeneratedClass::StaticClass();
}

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp
// 位置: 116-120，专用 compiler 只注册给 UTypeScriptBlueprint
// ============================================================================
void FPuertsEditorModule::OnPostEngineInit()
{
    if (Enabled)
    {
        FKismetCompilerContext::RegisterCompilerForBP(UTypeScriptBlueprint::StaticClass(), &MakeCompiler);
        // ★ actor 路径若仍是 UBlueprint，则不会命中这一条专用 compiler 注册
    }
}
```

[2] `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:507-517`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 位置: 507-517，直接使用 Kismet 返回的标准 Blueprint 类型
// ============================================================================
UClass* BlueprintClass = nullptr;
UClass* BlueprintGeneratedClass = nullptr;

IKismetCompilerInterface& KismetCompilerModule = FModuleManager::LoadModuleChecked<IKismetCompilerInterface>("KismetCompiler");
KismetCompilerModule.GetBlueprintTypesForClass(Class, BlueprintClass, BlueprintGeneratedClass);

Asset = FKismetEditorUtilities::CreateBlueprint(
    Class, Package, AssetName, BPTYPE_Normal,
    BlueprintClass, BlueprintGeneratedClass, FName("AngelscriptCreateBlueprint")
);
// ★ 当前主链没有额外的 Blueprint subclass / custom compiler 资产载体
```

设计取舍：

- `puerts` 的收益是可以把一部分 authoring 规则编码进资产类型和 compiler hook，自定义 Blueprint 资产拥有更强的“类型契约”。
- 代价是覆盖面并不完全统一。Actor 与 non-Actor 走的是不同 asset carrier，排查编译/刷新问题时必须先分流。
- `Angelscript` 的收益是资产路径与 UE 原生 Blueprint 体系一致，内容浏览器与 Kismet 兼容面更直接。
- 代价是 authoring contract 更多落在父类和 runtime/editor 工具，不像 `puerts` 那样能从资产类型本身读出一部分语义。

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 资产类型承载 authoring contract | non-Actor 用 `UTypeScriptBlueprint`，Actor 仍用 `UBlueprint` | 主链统一走标准 Blueprint 资产 | 实现方式不同 |
| 专用 compiler 的覆盖范围 | `RegisterCompilerForBP(UTypeScriptBlueprint)`，不是全覆盖 | 当前主链未见等价 Blueprint subclass compiler | Angelscript 当前没有实现 |
| 生成类落点 | 最终都落到 `UTypeScriptGeneratedClass` | 标准 Blueprint 生成类 + `UASClass` 父类体系 | 实现方式不同 |

### [维度 D4] 热重载：`puerts` 还保留了一条函数级 overlay / restore 链，`Angelscript` 仍然以类版本链和对象图重建为主

前几轮 D4 主要围绕 `Inspector + UTypeScriptGeneratedClass` 主线。本轮往旁边再挖，能看到 puerts 还有一套更细粒度的 **function overlay** 机制，这套机制不依赖 `CodeAnalyze` 那条 asset 编译链。

这里的推断是：`UJSGeneratedClass::Override/Mixin/Restore()` 面向的是运行时 JS 类补丁，而不是持久化资产刷新。理由是它的类 outer 固定在 `GetTransientPackage()`，生成函数是 `RF_Transient`，回滚时也只是把 `UJSGeneratedFunction` 挪到 `ORPHANED_DATA_ONLY_*` 的 transient class 下并恢复原函数指针。

Angelscript 的热重载则明显更高阶：`CreateFullReloadClass()` 先把旧 `UASClass` 改名并标记 `CLASS_NewerVersionExists`，再新建一个新的 `UASClass`；`DoFullReloadClass()` 重建属性、函数和默认对象数据，最后把 `ReplacedClass->NewerVersion = NewClass` 串成版本链。`UASClass::GetMostUpToDateClass()` 再沿链条拿最新类。这不是 `UFunction` 级别的 patch，而是类/对象图级别的替换。

```
[puerts] Function Overlay Reload
transient UJSGeneratedClass
 -> Override() rename old function to __puerts_old__
 -> duplicate UJSGeneratedFunction
 -> execCallJS / execCallMixin
 -> Restore() orphan transient functions and rename originals back

[Angelscript] Class Version Reload
CreateFullReloadClass()
 -> old UASClass renamed to *_REPLACED_*
 -> new UASClass created
 -> DoFullReloadClass() rebuild properties/functions
 -> ReplacedClass->NewerVersion = NewClass
 -> GetMostUpToDateClass() follows version chain
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JSGeneratedClass.cpp:87-177,179-260,262-355`、`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JSGeneratedFunction.cpp:11-47`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JSGeneratedClass.cpp
// 位置: 87-177, 179-260, 262-355，函数级 overlay / mixin / restore
// ============================================================================
void UJSGeneratedClass::Override(..., UFunction* Super, ...)
{
    if (Existed)
    {
        Super->Rename(*FString::Printf(TEXT("%s%s"), TEXT("__puerts_old__"), *Super->GetName()), Class,
            REN_DontCreateRedirectors | REN_DoNotDirty | REN_ForceNoResetLoaders);
        // ★ 旧函数先保留，再复制出新的 UJSGeneratedFunction
    }

    UJSGeneratedFunction* Function = Cast<UJSGeneratedFunction>(
        StaticDuplicateObject(Super, Class, FunctionName, RF_Transient, UJSGeneratedFunction::StaticClass()));
    Function->SetNativeFunc(&UJSGeneratedFunction::execCallJS);
    Function->FunctionTranslator = std::make_unique<PUERTS_NAMESPACE::FFunctionTranslator>(Function, false);
}

UFunction* UJSGeneratedClass::Mixin(..., UFunction* Super, ...)
{
    const FString FunctionName = *FString::Printf(TEXT("%s%s"), *Super->GetName(), TEXT("__puerts_mixin__"));
    UJSGeneratedFunction* Function = Cast<UJSGeneratedFunction>(
        StaticDuplicateObject(Super, Class, *FunctionName, RF_AllFlags, UJSGeneratedFunction::StaticClass()));
    Function->Original = Super;
    Function->OriginalFunc = Super->GetNativeFunc();
    Function->OriginalFunctionFlags = Super->FunctionFlags;
    Super->SetNativeFunc(&UJSGeneratedFunction::execCallMixin);   // ★ 原函数也被改写成 mixin 入口
    return Function;
}

void UJSGeneratedClass::Restore(UClass* Class)
{
    if (auto JGF = Cast<UJSGeneratedFunction>(*PP))
    {
        if (JGF->Original)
        {
            JGF->Original->SetNativeFunc(JGF->OriginalFunc);      // ★ 回滚原 native func
            JGF->Original->FunctionFlags = JGF->OriginalFunctionFlags;
        }
        JGF->Rename(nullptr, OrphanedClass, REN_DontCreateRedirectors | REN_DoNotDirty | REN_ForceNoResetLoaders);
    }

    if (Function->GetName().StartsWith(TEXT("__puerts_old__")))
    {
        Function->Rename(*Function->GetName().Mid(strlen("__puerts_old__")), Class,
            REN_DontCreateRedirectors | REN_DoNotDirty | REN_ForceNoResetLoaders);
        // ★ 旧名字再改回来
    }
}

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JSGeneratedFunction.cpp
// 位置: 11-47，最终执行入口
// ============================================================================
DEFINE_FUNCTION(UJSGeneratedFunction::execCallJS)
{
    auto PinedDynamicInvoker = Func->DynamicInvoker.Pin();
    if (PinedDynamicInvoker)
    {
        PinedDynamicInvoker->InvokeJsMethod(Context, Func, Stack, RESULT_PARAM);
        // ★ 覆写函数最终落到 JS method 调用
    }
}

DEFINE_FUNCTION(UJSGeneratedFunction::execCallMixin)
{
    auto PinedDynamicInvoker = JsFunc->DynamicInvoker.Pin();
    if (PinedDynamicInvoker)
    {
        PinedDynamicInvoker->InvokeMixinMethod(Context, JsFunc, Stack, RESULT_PARAM);
        // ★ mixin 走另一条显式入口
    }
}
```

[2] `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2563-2586,3234-3287,3695-3700`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:912-923`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 位置: 2563-2586，full reload 先创建新 UASClass
// ============================================================================
void FAngelscriptClassGenerator::CreateFullReloadClass(FModuleData& ModuleData, FClassData& ClassData)
{
    UASClass* ReplacedClass = FindObject<UASClass>(FAngelscriptEngine::GetPackage(), *UnrealName);
    if (ReplacedClass)
    {
        ReplacedClass->Rename(*OldClassName, nullptr, REN_DontCreateRedirectors);
        ReplacedClass->ClassFlags |= CLASS_NewerVersionExists;   // ★ 旧类保留为旧版本
    }

    UASClass* NewClass = NewObject<UASClass>(
        FAngelscriptEngine::GetPackage(), UASClass::StaticClass(),
        FName(*UnrealName), RF_Public | RF_Standalone | RF_MarkAsRootSet);
}

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 位置: 3234-3287, 3695-3700，真正的 full reload 是类级别重建
// ============================================================================
void FAngelscriptClassGenerator::DoFullReloadClass(FModuleData& ModuleData, FClassData& ClassData)
{
    UASClass* NewClass = (UASClass*)ClassDesc->Class;
    UASClass* ReplacedClass = ClassData.ReplacedClass;
    GetObjectsOfClass(ReplacedClass, Instances, true, RF_NoFlags); // ★ 先拿到旧类实例，再做整类替换

    // ...中间会重建属性、UASFunction、默认对象、引用信息...

    if (ReplacedClass != nullptr)
    {
        bReinstancingAny = true;
        ReplacedClass->NewerVersion = NewClass;                 // ★ 最终串成版本链
    }
}

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp
// 位置: 912-923，调用期可沿版本链取最新类
// ============================================================================
UClass* UASClass::GetMostUpToDateClass()
{
    if (NewerVersion == nullptr)
        return this;

    UASClass* NewerClass = NewerVersion;
    while (NewerClass->NewerVersion != nullptr)
        NewerClass = NewerClass->NewerVersion;
    return NewerClass;
}
```

设计取舍：

- `puerts` 的收益是有一条更细粒度的 `UFunction` overlay 逃生通道，适合快速覆写和 mixin。
- 代价是这条链路与资产刷新主线分离，且状态主要依赖 transient 对象、函数改名和手动 restore，语义面更分裂。
- `Angelscript` 的收益是所有热重载都围绕 `UASClass` 版本链和实例重建展开，模型统一。
- 代价是粒度更粗，代价也更高；当前主链未见等价的 per-function overlay API。

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| reload 粒度 | `Override/Mixin/Restore` 可到 `UFunction` 级 | `CreateFullReloadClass/DoFullReloadClass` 以类级版本链为主 | 实现方式不同 |
| 旧实现保留方式 | `__puerts_old__` 改名 + `UJSGeneratedFunction` transient 包装 | `*_REPLACED_*` 旧类 + `NewerVersion` 指向新类 | 实现方式不同 |
| 调用期追新方式 | 当前主链更依赖 wrapper/native func 改写 | `GetMostUpToDateClass()` 显式沿类版本链取最新 | Angelscript 在类版本追踪上实现更完整 |
| 函数级 overlay API | 源码可见 `Override/Mixin/Restore` | 当前主链未见等价公开路径 | Angelscript 当前没有实现 |

---
## 深化分析 (2026-04-09 07:54:52)

### [维度 D1] 插件架构与模块划分：puerts 的统一抽象更像“最小公共面”，而不是“等能力面”

前文已经证明 puerts 的主架构锚点是 `IJsEnv + pesapi`。本轮新增发现是：这层抽象解决的是“多 backend 能不能挂到同一套宿主接口上”，不是“所有 backend / mode 是否提供同等能力”。源码里至少有三处能力分叉：

- `IJsEnv` 把 `WaitDebugger()`、`IdleNotificationDeadline()`、`LowMemoryNotification()` 暴露成统一接口，但 `FJsEnvImpl` 在 QuickJS 路径把 `IdleNotificationDeadline()` 直接短路成 `true`。
- `WaitDebugger()` 依赖 `Inspector` 存在，而 `CreateV8Inspector()` 在 `WITH_EDITOR && (PLATFORM_WINDOWS || PLATFORM_MAC)` 之外直接返回 `nullptr`。
- `PuertsModule` 在 `Group Mode` 显式打印 `Do not support WaitDebugger in Group Mode!`，说明“多 env 扩展能力”与“调试阻塞能力”没有被做成同一平面。

这意味着 puerts 的工程策略是“先保住统一入口，再允许不同 backend 泄漏各自能力差异”；Angelscript 则相反，它牺牲 backend 多样性，换来 `FAngelscriptEngine -> FAngelscriptDebugServer -> Tick()` 这条运行时能力链的单形态。

```
[puerts] Capability Plane
IJsEnv
├─ Start / Reload / Rebind                    // 共通入口
├─ WaitDebugger                               // 名义上共通
│  ├─ Normal Mode + Inspector -> works        // 单 env + 有 inspector
│  ├─ Group Mode -> disabled                  // 组模式显式禁用
│  └─ unsupported platform/mode -> nullptr    // inspector 工厂直接返回空
└─ IdleNotificationDeadline
   ├─ V8 / Node.js -> real isolate call       // 真正进入 backend
   └─ QuickJS -> stub return true             // 接口保留但语义退化

[Angelscript] Capability Plane
FAngelscriptEngine
├─ Initialize / Tick                          // 单引擎统一执行面
├─ DebugServer (build-flag gated)             // 创建后所有 tick 同路径
└─ no backend split                           // 不存在后端能力分叉
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JsEnv.h:25-50`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JsEnv.h
// 位置: 25-50，puerts 对外暴露的统一 runtime 接口
// ============================================================================
class JSENV_API IJsEnv
{
public:
    virtual void Start(const FString& ModuleName, const TArray<TPair<FString, UObject*>>& Arguments) = 0;
    virtual bool IdleNotificationDeadline(double DeadlineInSeconds) = 0;   // ★ 抽象层假定所有 backend 都有 idle hook
    virtual void LowMemoryNotification() = 0;
    virtual void WaitDebugger(double Timeout) = 0;                         // ★ 抽象层也假定 debugger wait 可共用

    virtual void ReloadModule(FName ModuleName, const FString& JsSource) = 0;
    virtual void ReloadSource(const FString& Path, const PString& JsSource) = 0;
    virtual void OnSourceLoaded(std::function<void(const FString&)> Callback) = 0;
};
```

[2] `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.h:99-115`、`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:1147-1156`、`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:599-608`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.h
// 位置: 99-115，WaitDebugger 的真正实现条件
// ============================================================================
virtual void WaitDebugger(double timeout) override
{
    const auto startTime = FDateTime::Now();
    while (Inspector && !Inspector->Tick())          // ★ 没有 Inspector 时，这个等待链根本不会成立
    {
        if (timeout > 0)
        {
            auto now = FDateTime::Now();
            if ((now - startTime).GetTotalSeconds() >= timeout)
            {
                break;
            }
        }
    }
}

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp
// 位置: 1147-1156，QuickJS 路径的 idle hook 退化成空语义
// ============================================================================
bool FJsEnvImpl::IdleNotificationDeadline(double DeadlineInSeconds)
{
#ifndef WITH_QUICKJS
    return MainIsolate->IdleNotificationDeadline(DeadlineInSeconds);  // ★ V8/Node.js 真正进入 isolate
#else
    return true;                                                      // ★ QuickJS 路径直接短路
#endif
}

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp
// 位置: 599-608，不满足平台/构建条件时 inspector 工厂直接返回空
// ============================================================================
namespace PUERTS_NAMESPACE
{
V8Inspector* CreateV8Inspector(int32_t Port, void* InContextPtr)
{
    return nullptr;   // ★ 抽象接口仍然存在，但能力面在这里断开
}
};    // namespace PUERTS_NAMESPACE
```

[3] `Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:194-239`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp
// 位置: 194-239，多 env 扩展与 debugger wait 没有被统一成同一能力面
// ============================================================================
if (NumberOfJsEnv > 1)
{
    JsEnvGroup = MakeShared<PUERTS_NAMESPACE::FJsEnvGroup>(NumberOfJsEnv, Settings.RootPath);

    // 这种不支持等待
    if (Settings.WaitDebugger)
    {
        UE_LOG(PuertsModule, Warning, TEXT("Do not support WaitDebugger in Group Mode!"));
        // ★ 组模式明确拒绝 WaitDebugger
    }

    JsEnvGroup->RebindJs();
}
else
{
    JsEnv = MakeShared<PUERTS_NAMESPACE::FJsEnv>(Settings.RootPath);

    if (Settings.WaitDebugger)
    {
        JsEnv->WaitDebugger(Settings.WaitDebuggerTimeout);  // ★ 只有单 env 才会真正调用
    }

    JsEnv->RebindJs();
}
```

[4] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1452-1456,2833-2835`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:402-437`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 1452-1456, 2833-2835，单引擎路径上的调试能力没有后端分叉
// ============================================================================
#if WITH_AS_DEBUGSERVER
if ((!bUsePrecompiledData || bScriptDevelopmentMode) && FApp::HasProjectName())
{
    DebugServer = new FAngelscriptDebugServer(this, RuntimeConfig.DebugServerPort);
    // ★ 是否启用由 build/runtime config 决定，但一旦启用，所有 engine tick 都走同一条路径
}

if(DebugServer != nullptr)
    DebugServer->Tick();
#endif

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 位置: 402-437，DebugServer 本身就是固定的 runtime 服务
// ============================================================================
FAngelscriptDebugServer::FAngelscriptDebugServer(FAngelscriptEngine* InOwnerEngine, int Port)
{
    OwnerEngine = InOwnerEngine;
    Listener = new FTcpListener(FIPv4Endpoint(FIPv4Address::Any, Port));
    Listener->OnConnectionAccepted().BindRaw(this, &FAngelscriptDebugServer::HandleConnectionAccepted);
}

void FAngelscriptDebugServer::Tick()
{
    ProcessMessages();   // ★ 无“不同 backend 用不同调试 transport”的分叉
}
```

设计取舍：

- puerts 通过 `IJsEnv` 保住了多 backend 的宿主边界，收益是 Node.js / V8 / QuickJS 都能复用同一套 UE 接入口。
- 代价是“接口统一”不等于“能力统一”。调试、idle GC、group mode 等能力会在 backend/mode 上泄漏差异，调用方必须理解这些裂缝。
- Angelscript 牺牲了 backend 多样性，但换来能力面稳定，调试、tick、context 生命周期都围绕单一引擎对象展开。

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 运行时抽象目标 | 统一多 backend 宿主接口 | 单引擎能力内聚 | 实现方式不同 |
| 调试等待能力 | `WaitDebugger` 受 `Inspector`、平台、group mode 约束 | `DebugServer` 一旦创建即沿同一路径 tick | 实现质量差异 |
| idle/GC hook 一致性 | `QuickJS` 路径可退化成 stub | 不存在后端分叉 | 实现质量差异 |
| 多 env 扩展 | `FJsEnvGroup` 共享 invoker，但能力面不完全一致 | `CreateCloneFrom`/主引擎体系不引入后端裂缝 | 实现方式不同 |

### [维度 D8] 性能与优化：puerts 的运行时成本会随 backend scheduler 形态变化，Angelscript 的热路径更收敛

前文 D8 已经覆盖调用桥、`FastCall`、`StaticJIT`。本轮新增点是：puerts 的性能模型不只受“函数怎么过桥”影响，还强烈受“backend 需要怎样被驱动”影响。Node.js 路径会额外引入 `uv` 轮询线程、平台相关的 `poll`/`iocp` 适配、以及回到 UE game thread 的 task 分发；V8 / QuickJS 则没有这层 scheduler 成本。也就是说，puerts 的 runtime overhead 不是单一常数，而是 backend-dependent。

Angelscript 的对照很鲜明：它的主要运行时摊销落在 `asIScriptContext` 的 thread-local / global pool、以及统一的 `Engine->Tick()`。即使打开 DebugServer，消息处理也只是挂在同一个 tick 面上，没有再引入第二套 backend scheduler。

```
[puerts Node.js] Runtime Pump
uv thread
 -> PollEvents()
 -> FFunctionGraphTask(GameThread)
 -> UvRunOnce()
 -> JS callbacks / timers / promise jobs

[puerts V8 / QuickJS] Runtime Pump
GameThread
 -> isolate work
 -> delegate proxy ticker
 -> reload / bind callbacks

[Angelscript] Runtime Pump
GameThread tick
 -> FAngelscriptEngine::Tick()
 -> DebugServer->Tick()
 -> RequestContext / ReturnContext
 -> thread-local + global context pool reuse
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:166-200`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp
// 位置: 166-200，Node.js 后端额外引入 uv 轮询线程
// ============================================================================
void FJsEnvImpl::StartPolling()
{
    uv_async_init(&NodeUVLoop, &DummyUVHandle, nullptr);
    uv_sem_init(&PollingSem, 0);
    uv_thread_create(
        &PollingThread,
        [](void* arg)
        {
            auto* self = static_cast<FJsEnvImpl*>(arg);
            while (true)
            {
                uv_sem_wait(&self->PollingSem);
                if (self->PollingClosed)
                    break;

                self->PollEvents();   // ★ 先在 uv 线程等 IO / loop 事件
                if (self->PollingClosed)
                    break;

                self->LastJob = FFunctionGraphTask::CreateAndDispatchWhenReady(
                    [self]() { self->UvRunOnce(); }, TStatId{}, nullptr, ENamedThreads::GameThread);
                // ★ 再切回 UE game thread 执行一次 loop
            }
        },
        this);

#if PLATFORM_WINDOWS
    if (FPlatformMisc::NumberOfCores() == 1)
    {
        NodeUVLoop.iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 2);
        // ★ 源码直接承认单核下要额外修正，避免 PollEvents busy loop 把 CPU 顶满
    }
#endif
}
```

[2] `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:448-482,656-657`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp
// 位置: 448-482, 656-657，Node.js 初始化后立刻把 event loop 带进 UE 生命周期
// ============================================================================
NodeIsolateData = node::CreateIsolateData(Isolate, &NodeUVLoop, Platform, NodeArrayBufferAllocator.get());
v8::Local<v8::Context> Context = node::NewContext(Isolate);

NodeEnv = CreateEnvironment(NodeIsolateData, Context, Args, ExecArgs, node::EnvironmentFlags::kOwnsProcessState);
v8::MaybeLocal<v8::Value> LoadenvRet = node::LoadEnvironment(NodeEnv,
    "const publicRequire ="
    "  require('module').createRequire(process.cwd() + '/');"
    "globalThis.require = publicRequire;");

Isolate->SetMicrotasksPolicy(v8::MicrotasksPolicy::kAuto);
StartPolling();    // ★ backend 自己的 event loop 进入常驻调度

DelegateProxiesCheckerHandler =
    FUETicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FJsEnvImpl::CheckDelegateProxies), 1);
// ★ 即使不看函数桥，仅 scheduler 层也已经比单纯的 V8 embed 更重
```

[3] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1704-1750,1889-1902,2833-2835`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp:186-199`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 1704-1750, 1889-1902, 2833-2835，Angelscript 把主要热路径收敛在 context pool + 单 tick
// ============================================================================
asCContext* FAngelscriptEngine::CreateContext()
{
    auto* Context = (asCContext*)Engine->CreateContext();
    Context->SetExceptionCallback(asFUNCTION(LogAngelscriptException), 0, asCALL_CDECL);
    Context->SetLineCallback(AngelscriptLineCallback);
    Context->SetStackPopCallback(AngelscriptStackPopCallback);
    return Context;
}

asIScriptContext* AngelscriptRequestContext(asIScriptEngine* Engine, void* Data)
{
    auto& LocalPool = GAngelscriptContextPool;
    if (asCContext* Context = TryTakeContextFromPool(LocalPool.FreeContexts, Engine))
        return Context;   // ★ 先吃 thread-local pool

    return CreateConfiguredContext(Engine);
}

void AngelscriptReturnContext(asIScriptEngine* Engine, asIScriptContext* Context, void* Data)
{
    asCContext* ConcreteContext = static_cast<asCContext*>(Context);
    ResetContextForPooling(ConcreteContext);

    auto& LocalPool = GAngelscriptContextPool;
    if (LocalPool.FreeContexts.Num() < AS_MAX_POOLED_CONTEXTS)
    {
        LocalPool.FreeContexts.Push(ConcreteContext);  // ★ 再回收到本地池
        return;
    }

    Context->Release();
}

if(DebugServer != nullptr)
    DebugServer->Tick();   // ★ Debug 消息处理挂在同一条 engine tick 面

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp
// 位置: 186-199，没有 tick owner 时用 fallback tick 驱动主引擎
// ============================================================================
bool FAngelscriptRuntimeModule::TickFallbackPrimaryEngine(float DeltaTime)
{
    if (!UAngelscriptGameInstanceSubsystem::HasAnyTickOwner())
    {
        if (FAngelscriptEngine* CurrentEngine = FAngelscriptEngine::TryGetCurrentEngine())
        {
            if (CurrentEngine->ShouldTick())
            {
                CurrentEngine->Tick(DeltaTime);
            }
        }
    }

    return true;
}
```

设计取舍：

- puerts 的收益是可以把 Node.js 生态和 `uv` loop 真正搬进 UE 进程，脚本侧能力更强。
- 代价是 backend 拓扑直接变成性能变量。Node.js 不只是“另一个 VM”，而是“另一套 scheduler”。
- Angelscript 的收益是 tick / context / debug path 都比较统一，性能画像更可预测。
- 代价是它没有 Node.js 这类宿主外生态能力，扩展空间主要靠 `StaticJIT`、bind trivialization、context pool。

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| scheduler 成本 | `Node.js` 后端引入 uv 线程 + game thread hop | 主路径集中在 `Engine->Tick()` | 实现方式不同 |
| backend 对性能画像的影响 | 明显，Node/V8/QuickJS 成本不一 | 基本没有 backend 分叉 | 实现质量差异 |
| 热路径复用策略 | wrapper/template/ticker 复用 + backend 特化 | `asIScriptContext` thread-local/global pool | 实现方式不同 |
| 调试附加成本 | Inspector / websocket / polling 受 backend 影响 | DebugServer 挂在单引擎 tick 面 | 实现质量差异 |

### [维度 D11] 部署与打包：puerts 发布的是“带 Node-style 解析语义的脚本运行时”，Angelscript 发布的是“按 build 严格失效的脚本快照”

前文 D11 已经比较过 `.mbc/.cbc` 与 `PrecompiledScript.Cache`。本轮新增点是：两者的“发布物”不仅是静态文件集合，更直接决定运行时拓扑。

puerts 的发布契约由三层组成：

1. `DefaultPuerts.ini` 提供 `RootPath / DebugEnable / NumberOfJsEnv / WatchDisable`，甚至在 `NonPak Game` 打包下还要在模块启动时手动重读，避免配置加载时序晚于模块加载。
2. `DefaultJSModuleLoader` 实现的是一套接近 Node.js 的模块解析协议：`node_modules` 级联查找、`package.json`、`index.js`、以及 `js/mjs/cjs/mbc/cbc` 多后缀。
3. `JsEnv.Build.cs` 按 backend 和平台把 `libnode.dll`、`quickjs` 运行库、`ffi` 库作为 `NonUFS` 依赖打进目标输出目录。

Angelscript 的发布契约则更接近“脚本快照 + 构建一致性校验”：

1. 运行时从命令行读取 `as-generate-precompiled-data / as-ignore-precompiled-data / -asdebugport=`，再动态发现 project/plugin script roots。
2. 缓存文件按 build 配置区分 `PrecompiledScript_Development/Test/Shipping.Cache`，加载后还要校验 `BuildIdentifier`。
3. 如果二进制里编进了 StaticJIT，还要再对齐 `PrecompiledDataGuid`；不匹配就直接废弃转译代码。
4. `AdditionalEditorOnlyScriptPackageNames` 和 bind 侧 editor-only 检查控制的是“哪些脚本包在 cooked/runtime 可见”，不是额外的脚本加密层。

补充说明：本轮对 `Reference/puerts/unreal/Puerts/Source/` 与 `Plugins/Angelscript/Source/` 进行了 `rg -ni "\b(encrypt|decrypt|aes|rsa|cipher|crypto|signatureverify|signfile)\b"` 检索，均无命中。因此在当前 UE 插件源码范围内，能被源码证实的是“缓存格式、模块解析语义、native 依赖 staging、editor-only 可见性控制”，不是独立脚本加密/签名链。

```
[puerts] Deploy Contract
DefaultPuerts.ini
 -> RootPath / Debug / NumberOfJsEnv
 -> DefaultJSModuleLoader
    ├─ /Content/<RootPath>
    ├─ fallback /Content/JavaScript
    ├─ node_modules lookup
    └─ .js / .mjs / .cjs / .mbc / .cbc / package.json
 -> JsEnv.Build.cs stages backend native libs as NonUFS

[Angelscript] Deploy Contract
command line + enabled plugin script roots
 -> DiscoverScriptRoots()
 -> PrecompiledScript_<Config>.Cache
 -> BuildIdentifier validation
 -> PrecompiledDataGuid / StaticJIT GUID validation
 -> editor-only package filtering
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DefaultJSModuleLoader.cpp:67-120,123-139`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DefaultJSModuleLoader.cpp
// 位置: 67-120, 123-139，puerts 的发布物同时携带脚本目录语义
// ============================================================================
bool DefaultJSModuleLoader::SearchModuleInDir(
    const FString& Dir, const FString& RequiredModule, FString& Path, FString& AbsolutePath)
{
    return SearchModuleWithExtInDir(Dir, RequiredModule + ".js", Path, AbsolutePath) ||
           SearchModuleWithExtInDir(Dir, RequiredModule + ".mjs", Path, AbsolutePath) ||
           SearchModuleWithExtInDir(Dir, RequiredModule + ".cjs", Path, AbsolutePath) ||
#if defined(WITH_V8_BYTECODE)
           SearchModuleWithExtInDir(Dir, RequiredModule + ".mbc", Path, AbsolutePath) ||
           SearchModuleWithExtInDir(Dir, RequiredModule + ".cbc", Path, AbsolutePath) ||   // ★ bytecode/cache 也是一等公民
#endif
           SearchModuleWithExtInDir(Dir, RequiredModule / "package.json", Path, AbsolutePath) ||
           SearchModuleWithExtInDir(Dir, RequiredModule / "index.js", Path, AbsolutePath);
}

bool DefaultJSModuleLoader::Search(const FString& RequiredDir, const FString& RequiredModule, FString& Path, FString& AbsolutePath)
{
    // 调用 require 的文件所在目录往上找
    // ★ 这里保留了 node_modules 逐级回溯语义，而不是只认固定脚本根

    return SearchModuleInDir(FPaths::ProjectContentDir() / ScriptRoot, RequiredModule, Path, AbsolutePath) ||
           (ScriptRoot != TEXT("JavaScript") &&
               SearchModuleInDir(FPaths::ProjectContentDir() / TEXT("JavaScript"), RequiredModule, Path, AbsolutePath));
           // ★ 自定义 RootPath 之外，还保留默认 JavaScript 根回退
}

bool DefaultJSModuleLoader::Load(const FString& Path, TArray<uint8>& Content)
{
    IFileHandle* FileHandle = PlatformFile.OpenRead(*Path);
    if (FileHandle)
    {
        int len = FileHandle->Size();
        Content.AddUninitialized(len);
        const bool Success = FileHandle->Read(Content.GetData(), len);   // ★ 运行时直接读部署产物原始字节
        delete FileHandle;
        return Success;
    }
    return false;
}
```

[2] `Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsSetting.h:20-50`、`Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:372-447`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsSetting.h
// 位置: 20-50，发布配置会直接改变 runtime 拓扑
// ============================================================================
FString RootPath = "JavaScript";
bool AutoModeEnable = false;
bool DebugEnable = false;
int32 DebugPort = 8080;
bool WaitDebugger = false;
int32 NumberOfJsEnv = 1;
bool WatchDisable = false;

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp
// 位置: 372-447，模块启动期手动重读 ini，避免打包时序导致配置失真
// ============================================================================
if (GConfig->DoesSectionExist(SectionName, PuertsConfigIniPath))
{
    GConfig->GetBool(SectionName, TEXT("AutoModeEnable"), Settings.AutoModeEnable, PuertsConfigIniPath);
    GConfig->GetBool(SectionName, TEXT("DebugEnable"), Settings.DebugEnable, PuertsConfigIniPath);
    GConfig->GetInt(SectionName, TEXT("NumberOfJsEnv"), Settings.NumberOfJsEnv, PuertsConfigIniPath);
    GConfig->GetBool(SectionName, TEXT("WatchDisable"), Settings.WatchDisable, PuertsConfigIniPath);
}

void FPuertsModule::StartupModule()
{
    // NonPak Game 打包下, Puerts ini 的加载时间晚于模块加载
    RegisterSettings();   // ★ 打包模式会反向影响模块启动逻辑

    if (Settings.AutoModeEnable)
    {
        Enable();
    }

    WatchEnabled = !Settings.WatchDisable;
}
```

[3] `Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs:331-368,502-523,627-663`、`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:3708-3738,4118-4124`

```csharp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs
// 位置: 331-368, 502-523, 627-663，backend native libs 直接进入发布图
// ============================================================================
void AddRuntimeDependencies(string[] DllNames, string LibraryPath, bool Delay)
{
    foreach (var DllName in DllNames)
    {
        var DestDllPath = Path.Combine("$(BinaryOutputDir)", DllName);
        RuntimeDependencies.Add(DestDllPath, DllPath, StagedFileType.NonUFS);   // ★ 明确作为 NonUFS 运行时依赖发货
    }
}

PrivateDefinitions.Add("WITH_NODEJS");
RuntimeDependencies.Add("$(TargetOutputDir)/libnode.dll", Path.Combine(V8LibraryPath, "libnode.dll"));

PrivateDefinitions.Add("WITH_QUICKJS");
AddRuntimeDependencies(new string[] { "msys-quickjs.dll" }, V8LibraryPath, false);
AddRuntimeDependencies(new string[] { "libgcc_s_seh-1.dll", "libwinpthread-1.dll" }, V8LibraryPath, false);

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp
// 位置: 3708-3738, 4118-4124，.mbc/.cbc 是运行时协议的一部分，而不是纯构建缓存
// ============================================================================
if (FileName.EndsWith(TEXT(".mbc")))
{
    FCodeCacheHeader* CodeCacheHeader = (FCodeCacheHeader*) Data.GetData();
    if (CodeCacheHeader->FlagHash != Expect_FlagHash)
    {
        CodeCacheHeader->FlagHash = Expect_FlagHash;   // ★ 运行时会主动修补 cache header
    }

    CachedCode = new v8::ScriptCompiler::CachedData(Data.GetData(), Data.Num());
    Options = v8::ScriptCompiler::CompileOptions::kConsumeCodeCache;
}

if (Path.EndsWith(TEXT(".cbc")) || Path.EndsWith(TEXT(".mbc")))
{
    v8::Local<v8::ArrayBuffer> Ab = v8::ArrayBuffer::New(Info.GetIsolate(), Data.Num());
    ::memcpy(Buff, Data.GetData(), Data.Num());
    Info.GetReturnValue().Set(Ab);   // ★ JS runtime 本身也认这类产物
}
```

[4] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:514-567,1326-1363,1517-1556,1582-1600`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp:2481-2498,2627-2645`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp:25-35`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:910-919`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 514-567, 1326-1363，Angelscript 先发现 script roots，再决定用哪份快照
// ============================================================================
FAngelscriptEngineConfig FAngelscriptEngineConfig::FromCurrentProcess()
{
    Config.bGeneratePrecompiledData = FParse::Param(FCommandLine::Get(), TEXT("as-generate-precompiled-data"));
    Config.bIgnorePrecompiledData = FParse::Param(FCommandLine::Get(), TEXT("as-ignore-precompiled-data"));
    FParse::Value(FCommandLine::Get(), TEXT("-asdebugport="), Config.DebugServerPort);
}

Dependencies.GetEnabledPluginScriptRoots = []()
{
    for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetEnabledPluginsWithContent())
    {
        ScriptRoots.Add(Plugin->GetBaseDir() / TEXT("Script"));   // ★ 发布根来自启用插件，而不是固定 JavaScript 目录
    }
    return ScriptRoots;
};

TArray<FString> FAngelscriptEngine::DiscoverScriptRoots(bool bOnlyProjectRoot) const
{
    FString RootPath = Dependencies.ConvertRelativePathToFull(Dependencies.GetProjectDir() / TEXT("Script"));
    DiscoveredRootPaths.Insert(RootPath, 0);   // ★ 项目根优先，再拼插件脚本根
    return DiscoveredRootPaths;
}

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 1517-1556, 1582-1600，cache 按 build 维度严格失效
// ============================================================================
Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript_Development.Cache");
if (!IFileManager::Get().FileExists(*Filename))
    Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript.Cache");

PrecompiledData->Load(Filename);
if (!PrecompiledData->IsValidForCurrentBuild())
{
    delete PrecompiledData;
    PrecompiledData = nullptr;   // ★ build id 不同，整份缓存直接废弃
}
else
{
    const FStaticJITCompiledInfo* CompiledInfo = FStaticJITCompiledInfo::Get();
    if (CompiledInfo != nullptr && CompiledInfo->PrecompiledDataGuid != PrecompiledData->DataGuid)
    {
        FJITDatabase::Get().Clear();   // ★ 二进制内 JIT GUID 不匹配，也直接放弃
    }
}

if (bGeneratePrecompiledData)
{
    FString Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript.Cache");
    PrecompiledData->InitFromActiveScript();
    PrecompiledData->Save(Filename);
}

if (!bScriptDevelopmentMode && !bGeneratePrecompiledData)
    PrecompiledData->ClearUnneededRuntimeData();   // ★ 运行时会主动裁掉不再需要的数据

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp
// 位置: 2481-2498, 2627-2645，快照严格绑定当前 build
// ============================================================================
void FAngelscriptPrecompiledData::ClearUnneededRuntimeData()
{
    objType->propertyTable.EraseAll();
    objType->methodTable.EraseAll();   // ★ 加载后主动瘦身
}

int32 FAngelscriptPrecompiledData::GetCurrentBuildIdentifier()
{
#if UE_BUILD_DEVELOPMENT
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

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp
// 位置: 25-35，编进二进制的 StaticJIT 还会额外绑定 DataGuid
// ============================================================================
FStaticJITCompiledInfo::FStaticJITCompiledInfo(FGuid Guid)
    : PrecompiledDataGuid(Guid)
{
    ActiveInfo = this;   // ★ 二进制里只允许一份当前 JIT 元信息
}

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp
// 位置: 910-919，editor-only 控制的是脚本包可见性，而不是加密
// ============================================================================
if (!bIsEditor && FAngelscriptEngine::Get().ConfigSettings->AdditionalEditorOnlyScriptPackageNames.Contains(Class->GetOutermost()->GetFName()))
{
    bIsEditor = true;    // ★ cooked/runtime 可见性控制
}
```

设计取舍：

- puerts 的收益是部署产物和运行时语义高度一致，脚本目录、`node_modules`、bytecode/cache、native backend 库一起构成真正的 runtime contract。
- 代价是发布物更重，平台差异也更直接暴露到 staging 图里；同一个项目换 backend，产物边界就会变化。
- Angelscript 的收益是运行时产物更集中在 script roots 与 precompiled snapshot，且 build / JIT 一致性校验非常明确。
- 代价是它不提供多 backend 选择，也没有在当前插件源码范围内实现独立脚本加密器；主要策略是“缓存失效”和“可见性过滤”而不是“密文分发”。

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 模块解析语义 | `node_modules + package.json + index.js + .mbc/.cbc` | `project root + plugin script roots + module filename` | 实现方式不同 |
| 运行时 native 依赖 | 按 backend/platform staging `libnode` / `quickjs` / `ffi` | `AngelscriptRuntime` 直接内嵌引擎源码，无独立 VM DLL staging | 实现方式不同 |
| 缓存失效策略 | `CodeCacheHeader` 运行时修补后继续消费 | `BuildIdentifier + DataGuid` 不匹配就丢弃 | 实现质量差异 |
| editor-only / deploy boundary | 主要靠 root path、watch、backend 库和 JS 目录语义 | 额外有 `AdditionalEditorOnlyScriptPackageNames` 过滤 runtime 可见性 | 实现方式不同 |
| 独立加密/签名链 | 当前 UE 插件源码范围内未见实现 | 当前插件源码范围内同样未见实现 | 二者当前都没有实现 |

---

## 深化分析 (2026-04-09 08:05:09)

### [维度 D6] 代码生成与 IDE 支持：`puerts` 用 package-version 驱动离线声明增量失效，`Angelscript` 当前把 IDE 真源保持在 live debug database

前几轮 D6 已经把 `.d.ts`、`CodeAnalyze` 和 Blueprint 回写拆得很细；这一轮补的是 **这些 IDE 产物如何防止过期**。`puerts` 不是每次全量重写再让 IDE 自己兜底，而是把每个 Blueprint 类型声明包在 `TYPE_DECL_START/TYPE_DECL_END` 标记里，并把 `PackageSavedHash` / `PackageGuid` 作为 `FileVersionString` 写回 `ue_bp.d.ts`。下一轮生成前，它会先从旧的 `ue_bp.d.ts` 恢复 `BlueprintTypeDeclInfoCache`，再按当前 `AssetRegistry` 里的 package version 比较 `Changed` 标志，只重生成变更项；落盘前还会主动删掉插件目录内旧版 `Typing/ue/*.d.ts`，避免仓库里残留的历史声明污染编译。

Angelscript 这边，本轮重新对 `Plugins/Angelscript/Source` 做了 `rg -n "\.d\.ts|Typing/|TypeScriptDeclaration|LanguageService|ts_file_versions_info|ue_bp\.d\.ts|ue\.d\.ts"` 检索，结果是 `NO_HITS`。当前源码里能被证实的 IDE 真源，主要是 `FAngelscriptDebugServer::SendDebugDatabase()` 实时序列化当前 `asIScriptEngine` 的类型/函数声明，和 `GoToDefinition()` 在 live engine 上解析 symbol 后跳转 UE 原生定义。也就是说，Angelscript 现在更像“运行时查询服务”，而不是“带版本失效协议的离线声明缓存”。

```
[puerts] IDE Artifact Freshness
AssetRegistry package version
 -> RestoreBlueprintTypeDeclInfos()
 -> compare FileVersionString
 -> regenerate changed Blueprint decl only
 -> write Typing/ue/ue_bp.d.ts with version markers

[Angelscript] IDE Artifact Freshness
live asIScriptEngine
 -> SendDebugDatabase()
 -> serialize current type/function graph
 -> GoToDefinition()
 -> navigate UE source directly
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:55-65,418-457,561-615,674-681`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp
// 位置: 55-65, 418-457, 561-615, 674-681，离线声明的版本标记、恢复与增量失效
// ============================================================================
#define TYPE_DECL_START "// __TYPE_DECL_START: "
#define TYPE_DECL_END "// __TYPE_DECL_END"

#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION > 5
#define GET_VERSION_ID(PD) LexToString(PD->GetPackageSavedHash())
#else
#define GET_VERSION_ID(PD) PD->PackageGuid.ToString()   // ★ 用 package 版本做声明失效主键
#endif

PlatformFile.DeleteFile(*(PuertsBaseDir / TEXT("Typing/ue/ue.d.ts")));
PlatformFile.DeleteFile(*(PuertsBaseDir / TEXT("Typing/ue/ue_bp.d.ts")));
// ★ 先删插件里旧产物，避免历史版本残留污染编译

for (auto& KV : BlueprintTypeDeclInfoCache)
{
    if (KV.Value.IsExist)
    {
        Output << TYPE_DECL_START << (KV.Value.IsAssociation ? TYPE_ASSOCIATION : KV.Value.FileVersionString) << "\n";
        Output << NameToDecl.Value;
        Output << TYPE_DECL_END << "\n";   // ★ 每个声明块都携带版本信息
    }
}

FFileHelper::LoadFileToString(FileContent, *(FPaths::ProjectDir() / TEXT("Typing/ue/ue_bp.d.ts")));
RestoreBlueprintTypeDeclInfos(FileContent, InGenFull);   // ★ 下一轮先恢复旧缓存

FString FileVersionString = FileContent.Mid(Pos + Start.Len(), VersionInfoEnd - Pos - Start.Len());
const bool bIsAssociation = FileVersionString == TYPE_ASSOCIATION;
BlueprintTypeDeclInfoCache.Add(
    FName(*PackageName), {NameToDecl, FileVersionString, bIsExist, true, bIsAssociation});

auto FileVersion = GET_VERSION_ID(PackageData);
BlueprintTypeDeclInfoPtr->Changed = InGenFull || (FileVersion != BlueprintTypeDeclInfoPtr->FileVersionString);
BlueprintTypeDeclInfoPtr->FileVersionString = FileVersion;   // ★ 只有版本变化才继续重生成
```

[2] `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:1288-1370,1493-1539`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 位置: 1288-1370, 1493-1539，Angelscript 当前 IDE 真源是 live debug database + live symbol lookup
// ============================================================================
void FAngelscriptDebugServer::GoToDefinition(const FAngelscriptGoToDefinition GoTo)
{
    auto* Engine = FAngelscriptEngine::Get().Engine;

    asITypeInfo* TypeInfo = Engine->GetTypeInfoByName(TCHAR_TO_ANSI(*GoTo.TypeName));
    if (TypeInfo != nullptr && ScriptFunction == nullptr)
    {
        // ★ 直接在当前 live engine 里扫 method，而不是查离线声明缓存
        asIScriptFunction* Method = TypeInfo->GetMethodByIndex(i);
    }

    UFunction* UnrealFunction = FAngelscriptDocs::LookupAngelscriptFunction(ScriptFunction->GetId());
    if (UnrealFunction != nullptr)
    {
        FSourceCodeNavigation::NavigateToFunction(UnrealFunction);   // ★ 最终跳回 UE 原生源码
        return;
    }
}

void FAngelscriptDebugServer::SendDebugDatabase(FSocket* Client)
{
    auto* ScriptEngine = FAngelscriptEngine::Get().Engine;

    FAngelscriptDebugDatabaseSettings DebugSettings;
    SendMessageToClient(Client, EDebugMessageType::DebugDatabaseSettings, DebugSettings);

    auto GetDecl = [&](int TypeId, asDWORD* Flags = nullptr, bool bShowInRef = true) -> FString
    {
        const char* DeclRaw = ScriptEngine->GetTypeDeclaration(TypeId);
        FString Decl = ANSI_TO_TCHAR(DeclRaw);   // ★ 当前进程里的类型声明现查现发
        return Decl;
    };
}
```

设计取舍：

- `puerts` 的收益是 authoring 阶段就有一份带版本失效协议的离线类型缓存，编辑器或外部 IDE 不必依赖 live UE session 才能得到相对新鲜的类型表面。
- 代价是声明文件本身变成“需要维护的一等产物”，生成链必须负责恢复、比对、清理旧版本。
- `Angelscript` 的收益是 IDE 数据永远来自当前加载中的脚本引擎与绑定状态，live session 下新鲜度更高，也更不容易被旧文件污染。
- 代价是在当前仓库范围内，还没有看到与 `puerts` 等价的离线声明缓存入口，离线 authoring 体验主要依赖运行中的 debug/navigation 服务。

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 离线声明缓存 | `ue_bp.d.ts` 带 `TYPE_DECL_START/END + FileVersionString` | 当前源码范围内未见等价 `.d.ts/Typing` 生成入口 | Angelscript 当前没有实现同等级离线声明缓存 |
| 失效主键 | `PackageSavedHash/PackageGuid` | live engine 当前状态 | 实现方式不同 |
| IDE 真源 | 增量声明文件 + TS 工具链 | `DebugDatabase` + `GoToDefinition` + UE source navigation | 实现方式不同 |

### [维度 D2] 反射绑定机制：`puerts` 的“自动绑定”是薄生成 + 厚编组，`Angelscript` 的“手写绑定”是厚注册 + 薄执行

前几轮 D2 已经覆盖 `DefineClass<T>()`、translator 工厂和 extension methods；这一轮补的是 **复杂度到底落在绑定流水线的哪一侧**。`puerts` 的自动生成文件确实存在，但在当前 UE 插件范围内，`JsEnv/Private/Gen/*.cpp` 主要是像 `FVector_Wrap.cpp` 这种“把构造、属性、方法写成 DSL”的薄层包装。真正把大部分 UE 反射面接进 JS 的，是运行时 `FStructWrapper` 对 `UClass/UInterface` 的 `TFieldIterator` 扫描，以及 `PropertyTranslatorCreator` 按 `FProperty` 子类在运行时挑选 marshaller。`FFunctionTranslator::CallJs()` 再在每次调用时逐参 `UEToJsInContainer()` / `JsToUEOutInContainer()`。

Angelscript 则刚好反过来。绑定作者在 `Bind_*.cpp` 里先把脚本签名文本写死，再由 `ASAutoCaller::MakeFunctionCaller()` 通过模板生成 `RedirectFunctionCaller/RedirectMethodCaller`，运行时只需要把 `void**` 参数展开后直调 native 函数。换句话说，`puerts` 把 authoring 自动化做得更深，但把编组复杂度保留在运行时 translator 层；`Angelscript` 把 authoring 负担前置给手写 bind，却把调用边界压成了更窄的模板 caller。

```
[puerts] Binding Cost Placement
Gen/*.cpp thin DSL
 -> StructWrapper scans UClass/UInterface
 -> PropertyTranslatorCreator picks marshaller per FProperty
 -> FunctionTranslator converts args every call
 -> JS invocation

[Angelscript] Binding Cost Placement
Bind_*.cpp writes exact script signature
 -> ASAutoCaller generates template caller
 -> RegisterObjectMethod/RegisterGlobalFunction
 -> runtime expands void** and direct-calls native
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/Gen/FVector_Wrap.cpp:14-35`、`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:254-356`、`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1225-1392`、`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp:439-470`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/Gen/FVector_Wrap.cpp
// 位置: 14-35，自动生成层本身很薄，只描述少量强类型 struct API
// ============================================================================
struct AutoRegisterForFVector
{
    AutoRegisterForFVector()
    {
        puerts::DefineClass<FVector>()
            .Constructor(CombineConstructors(...))
            .Property("X", MakeProperty(&FVector::X))
            .Method("DiagnosticCheckNaN", CombineOverloads(...))
            .Function("CrossProduct", MakeFunction(&FVector::CrossProduct));
            // ★ 这里描述的是 DSL，不是通用 UClass 绑定器本体
    }
};

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp
// 位置: 254-356，真正的大面绑定在运行时扫描 UFunction/UInterface
// ============================================================================
for (TFieldIterator<UFunction> FuncIt(Class, EFieldIteratorFlags::ExcludeSuper); FuncIt; ++FuncIt)
{
    UFunction* Function = *FuncIt;
    if (Function->HasAnyFunctionFlags(FUNC_Static))
    {
        auto FunctionTranslator = GetFunctionTranslator(Function);
        Result->Set(Key, FunctionTranslator->ToFunctionTemplate(Isolate));
    }
    else
    {
        auto FunctionTranslator = GetMethodTranslator(Function, false);
        Result->PrototypeTemplate()->Set(Key, FunctionTranslator->ToFunctionTemplate(Isolate));
    }
}

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp
// 位置: 1225-1392，marshaller 在运行时按 FProperty 子类分派
// ============================================================================
if (InProperty->IsA<StructPropertyMacro>())
{
    auto StructProperty = CastFieldMacro<StructPropertyMacro>(InProperty);
    if (StructProperty->Struct == FArrayBuffer::StaticStruct())
        return Creator<FArrayBufferPropertyTranslator>::Do(InProperty, IgnoreOut, Ptr);
    else
        return Creator<FScriptStructPropertyTranslator>::Do(InProperty, IgnoreOut, Ptr);
}
else if (InProperty->IsA<DelegatePropertyMacro>())
{
    return Creator<FDelegatePropertyTranslator>::Do(InProperty, IgnoreOut, Ptr);
}
else
{
    return Creator<DoNothingPropertyTranslator>::Do(InProperty, IgnoreOut, Ptr);
    // ★ 不支持类型直接降级；上层 `.d.ts` 也会对应跳过
}

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp
// 位置: 439-470，每次 JS 调用都要逐参编组与回写 out 参数
// ============================================================================
for (int i = 0; i < Arguments.size(); ++i)
{
    Args[i] = Arguments[i]->UEToJsInContainer(Isolate, Context, Params, false);
}

Result = JsFunction->Call(Context, This, Arguments.size(), Args);

for (int i = 0; i < Arguments.size(); ++i)
{
    Arguments[i]->JsToUEOutInContainer(Isolate, Context, Args[i], Params, true);
}
```

[2] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/FunctionCallers.h:194-363`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:449-460`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/FunctionCallers.h
// 位置: 194-363，Angelscript 把调用桥前移到模板 caller 生成期
// ============================================================================
struct FunctionCaller
{
    using FunctionCallerPtr = void(*)(TFunctionPtr Method, void** Parameters, void* ReturnValue);
    using MethodCallerPtr = void(*)(TMethodPtr Function, void** Parameters, void* ReturnValue);
};

template<typename ReturnType, typename... ParamTypes>
FunctionCaller MakeFunctionCaller(ReturnType(*FunctionPtr)(ParamTypes...))
{
    return FunctionCaller::Make(&RedirectFunctionCaller<ReturnType, ParamTypes...>);
}

template<typename ReturnType, typename... ParamTypes, typename ObjectType>
FunctionCaller MakeFunctionCaller(ReturnType(ObjectType::* FunctionPtr)(ParamTypes...))
{
    return FunctionCaller::Make(&RedirectMethodCaller<ReturnType, ObjectType, ParamTypes...>);
}
// ★ caller 在编译期已经知道参数布局，运行时只解包 `void**`

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp
// 位置: 449-460，注册阶段直接把模板 caller 交给 AngelScript engine
// ============================================================================
void FAngelscriptBinds::BindMethod(FBindString Signature, asSFuncPtr Ptr, ASAutoCaller::FunctionCaller Caller, void* UserData)
{
    int FunctionId = Manager.Engine->RegisterObjectMethod(
        ClassName.ToCString(), Signature.ToCString(), Ptr, asCALL_THISCALL, *(asFunctionCaller*)&Caller, nullptr);
    OnBind(FunctionId, UserData, nullptr);
}
```

设计取舍：

- `puerts` 的收益是 authoring 自动化强，新增 UE 反射面时不必同步手写大量签名；同一套 translator 还能同时服务 runtime 和 `.d.ts` 投影。
- 代价是运行时编组仍然很厚，调用路径里始终保留 `FProperty -> translator -> JS value` 的逐层转换。
- `Angelscript` 的收益是热路径更早确定，模板 caller 把 native 调用形状固定下来，执行面更窄。
- 代价是签名维护成本高，缺少 `puerts` 那种“把大部分暴露面交给统一 translator 栈处理”的自动化层。

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 自动化落点 | 生成 DSL + 运行时 translator | 手写签名 + 编译期 caller | 实现方式不同 |
| 调用时编组厚度 | 每次调用逐参 `UEToJs/JsToUE` | `void**` 展开后直调 native | 实现质量差异 |
| 不支持类型处理 | `DoNothingPropertyTranslator` + `.d.ts` 跳过 | 通常不暴露或单独手写/reflective fallback | 实现方式不同 |

### [维度 D8] 性能与优化：`puerts` 把冷启动成本拆成进程级 VM bootstrap + 场景级 env 构建 + 首触达 class load，`Angelscript` 把大头压进 engine init

前几轮 D8 更多讨论调用桥与 GC；这一轮补的是 **冷启动成本在哪里支付**。从源码看，`puerts` 至少分三段付费：第一段是 `FJsEnvModule::StartupModule()` 的进程级 VM bootstrap，包括 `GMalloc` 兼容修补和 V8/Node platform 创建；第二段是 `FPuertsModule::Enable()/MakeSharedJsEnv()` 的场景级 env 构建，什么时候创建 `JsEnv`/`JsEnvGroup` 取决于 `AutoModeEnable`、PIE/Standalone、`NumberOfJsEnv` 等设置；第三段是 `LoadClassByID()` 的首触达 lazy load，只有某个 `TypeId` 真被脚本或 wrapper 访问到，才会触发 `ClassNotFoundCallback` 补注册。

Angelscript 也不是绝对“零延迟初始化”式设计，它在 `DiscoverScriptRoots(true)` 上保留了一个只先看 project root 的小懒点；但紧接着就会把 bind database、bind modules、`BindScriptTypes()`、precompiled cache、primary context 和 `InitialCompile()` 串成一条启动链。换句话说，从源码流程推断，puerts 更像把成本摊到“模块启动 / 会话启动 / 类型首触达”三次付清，Angelscript 更像在 engine init 阶段把大头一次性交掉，换取之后更稳定的首帧与首调用形态。

```
[puerts] Cold-Start Cost
process start
 -> JsEnvModule::StartupModule()
 -> Enable()/MakeSharedJsEnv()
 -> LoadClassByID() on first touched type

[Angelscript] Cold-Start Cost
engine init
 -> load Binds.Cache / bind modules
 -> BindScriptTypes()
 -> load PrecompiledScript.Cache
 -> CreateContext()
 -> InitialCompile()
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvModule.cpp:170-210`、`Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:185-220,453-469`、`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JSClassRegister.cpp:85-99`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvModule.cpp
// 位置: 170-210，进程级 VM bootstrap
// ============================================================================
void FJsEnvModule::StartupModule()
{
    int* Dummy = new (std::nothrow) int[0];
    if (!Dummy)
    {
        MallocWrapper = new FMallocWrapper(GMalloc);
        GMalloc = MallocWrapper;   // ★ 先处理 allocator 兼容层
    }

#if defined(WITH_NODEJS)
    platform_ = node::MultiIsolatePlatform::Create(4);
#else
    platform_ = v8::platform::NewDefaultPlatform();
#endif

    v8::V8::SetFlagsFromString("--turbo-fast-api-calls");
    // ★ backend platform 在模块启动时就创建，不等首个脚本调用
}

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp
// 位置: 185-220, 453-469，env 构建放到 Enable/MakeSharedJsEnv 阶段
// ============================================================================
JsEnv.Reset();
JsEnvGroup.Reset();

NumberOfJsEnv = (Settings.NumberOfJsEnv > 1 && Settings.NumberOfJsEnv < 10) ? Settings.NumberOfJsEnv : 1;
if (NumberOfJsEnv > 1)
{
    JsEnvGroup = MakeShared<PUERTS_NAMESPACE::FJsEnvGroup>(NumberOfJsEnv, Settings.RootPath);
    JsEnvGroup->RebindJs();   // ★ 会话级再真正拉起一组 VM/env
}

void FPuertsModule::Enable()
{
    GUObjectArray.AddUObjectCreateListener(...);
#if WITH_EDITOR
    if (IsRunningGame())
    {
        MakeSharedJsEnv();    // ★ Standalone/PIE 时才真正建 env
    }
#else
    MakeSharedJsEnv();
#endif
}

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JSClassRegister.cpp
// 位置: 85-99，类型注册继续按首触达 lazy load
// ============================================================================
const JSClassDefinition* LoadClassByID(const void* TypeId)
{
    auto clsDef = FindClassByID(TypeId);
    if (!clsDef && ClassNotFoundCallback)
    {
        if (!ClassNotFoundCallback(TypeId))
            return nullptr;
        clsDef = FindClassByID(TypeId);   // ★ 只有真的触达某个 TypeId 才补注册
    }
    return clsDef;
}
```

[2] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1422-1569`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 1422-1569，Angelscript 把 bind/cache/context/initial compile 串进 engine init
// ============================================================================
Engine->SetMessageCallback(asFUNCTION(LogAngelscriptError), 0, asCALL_CDECL);
Engine->SetContextCallbacks(&AngelscriptRequestContext, &AngelscriptReturnContext, nullptr);

AllRootPaths = DiscoverScriptRoots(/*bOnlyProjectRoot =*/ true);
// ★ 这里只把 plugin script roots 稍微后推，但后面的重活仍在启动期完成

FAngelscriptBindDatabase::Get().Load(GetScriptRootDirectory() / TEXT("Binds.Cache"), bGeneratePrecompiledData);

for (FString ModuleName : FAngelscriptBinds::GetBindModuleNames())
{
    FModuleManager::Get().LoadModule(FName(ModuleName), ELoadModuleFlags::LogFailures);
}

BindScriptTypes();   // ★ 启动期直接注册整套脚本类型面

if (bUsePrecompiledData)
{
    PrecompiledData = new FAngelscriptPrecompiledData(Engine);
    PrecompiledData->Load(Filename);   // ★ 启动期加载缓存
}

GameThreadTLD->primaryContext = CreateContext();
InitialCompile();    // ★ 初次编译也在同一条启动链上
```

设计取舍：

- 从源码流程推断，`puerts` 的收益是冷启动更可分摊，未触达类型不会先付模板/类注册成本。
- 代价是成本分布更分散，首触达某个冷类型或切换到 group mode 时，延迟峰值可能发生在运行中。
- `Angelscript` 的收益是启动后状态更稳定，`bind + cache + context + initial compile` 已经在 engine init 基本就绪。
- 代价是启动阶段更重；即便后续只用到少量脚本面，也先为大部分绑定和缓存链支付了初始化成本。

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 进程级 bootstrap | `StartupModule()` 建 platform / allocator shim | 无独立 VM platform bootstrap，直接嵌入 AngelScript engine | 实现方式不同 |
| 类型注册时机 | `LoadClassByID()` 首触达懒加载 | `BindScriptTypes()` 启动期整体注册 | 实现方式不同 |
| 冷启动成本分布 | 模块启动 + env 构建 + 首触达三段摊销 | bind/cache/context/initial compile 前置到 engine init | 实现质量差异 |

---

## 深化分析 (2026-04-09 08:13:29)

本轮不再重复前面已经覆盖很多次的 `Build.cs / .d.ts / Inspector / PrecompiledData` 大框架，而是补三处更靠近“源码行为边界”的细节：`puerts` 的 TS 编写期编译器如何直接收敛 Blueprint schema，`puerts` 热重载失败时到底回退到哪一层，以及 `puerts` 的 native addon 加载链为什么已经构成 script-visible 部署契约。

### [维度 D6] 代码生成与 IDE 支持：`puerts` 的 TypeScript authoring compiler 会直接删改 Blueprint schema，`Angelscript` 当前更偏“影响分析 + 节点刷新”

前面的 D6 主要讲了 `.d.ts`、`LanguageService`、decorator metadata。本轮新增点是：`puerts` 的 `CodeAnalyze.ts` 不只是“给 IDE 生成提示”，它还把 TypeScript 类视为 Blueprint 资产的**权威 schema 源**。分析器遍历 TS 成员后，不只做 `AddFunctionWithMetaData()` / `AddMemberVariableWithMetaData()`，还会在同一轮把 TS 中已经不存在的 component、member variable、function 从 Blueprint 里删除，然后立即 `Save()` 并触发 `CompileBlueprint()`。

对比 `Angelscript`，当前插件在编辑器侧已经有成熟的 Blueprint 关系处理，但职责不同。`BlueprintImpactScanner` 先从变更脚本模块里构建 `Classes / Structs / Enums / Delegates` 影响集，再分析哪些 Blueprint 受影响；`ClassReloadHelper` 随后只做 pin type 替换、节点 `ReconstructNode()`、`QueueForCompilation()`。这是一条**依赖分析与修复链**，不是一条“脚本源码直接重写 Blueprint schema”的 authoring compiler。

```
[puerts] TS Authoring Compiler
TypeScript AST + decorators
 -> CodeAnalyze.ts
 -> PEBlueprintAsset
    -> Add*WithMetaData()
    -> RemoveNotExisted*
    -> Save() -> CompileBlueprint()
 -> UTypeScriptGeneratedClass.FunctionToRedirect

[Angelscript] Blueprint Impact Assistance
changed .as modules
 -> BlueprintImpact::FindModulesForChangedScripts
 -> BuildImpactSymbols
 -> AnalyzeLoadedBlueprint
 -> ClassReloadHelper::ReconstructNode()
 -> QueueForCompilation()
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/PuertsEditor/CodeAnalyze.ts:936-940,1030-1098`

```typescript
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/PuertsEditor/CodeAnalyze.ts
// 位置: 936-940, 1030-1098，TypeScript 语义分析直接驱动 Blueprint schema 写回
// ============================================================================
let bp = new UE.PEBlueprintAsset();
bp.LoadOrCreateWithMetaData(type.getSymbol().getName(), modulePath, baseTypeUClass, 0, 0, uemeta.compileClassMetaData(type));
// ★ 先把 TS class 绑定到一个可编辑的 Blueprint asset 外壳

bp.AddFunctionWithMetaData(symbol.getName(), false, resultPinType.pinType, resultPinType.pinValueType, flags, clearFlags, uemeta.compileFunctionMetaData(symbol));
bp.AddMemberVariableWithMetaData(symbol.getName(), propPinType.pinType, propPinType.pinValueType, Number(localFlags & 0xffffffffn), Number(localFlags >> 32n), cond, propertyMetaData);

bp.RemoveNotExistedComponent();
bp.RemoveNotExistedMemberVariable();
bp.RemoveNotExistedFunction();
// ★ TS 没有的 Blueprint 成员会被主动删掉，不是“只增不减”

bp.SetupAttachments(attachments);
bp.HasConstructor = hasConstructor;
bp.Save();
// ★ 本轮分析结束后立即落盘并触发后续编译
```

[2] `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:1240-1264,1269-1289,1294-1357,1393-1395`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp
// 位置: 1240-1264, 1269-1289, 1294-1357, 1393-1395，Blueprint 资产同步器
// ============================================================================
for (auto Name : ToDelete)
{
    NeedSave = true;
    RemoveComponent(Name);                               // ★ 删除 TS 中已不存在的组件
}

for (auto Name : ToDelete)
{
    NeedSave = true;
    FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, Name); // ★ 删除过期变量
}

auto RemovedFunction =
    Blueprint->FunctionGraphs.RemoveAll([&](UEdGraph* Graph) { return !FunctionAdded.Contains(Graph->GetFName()); });
NeedSave = NeedSave || (RemovedFunction > 0);            // ★ 删除过期函数图

auto RemovedCustomEvent = EventGraph->Nodes.RemoveAll(
    [&](UEdGraphNode* GraphNode)
    {
        UK2Node_CustomEvent* CustomEvent = Cast<UK2Node_CustomEvent>(GraphNode);
        return CustomEvent && !FunctionAdded.Contains(CustomEvent->CustomFunctionName);
    });

if (NeedSave)
{
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint); // ★ 同步后立刻重编译 Blueprint
}

FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, false, false); // ★ 最终直接进入持久化
```

[3] `Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp:88-148,278-304` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:201-299`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp
// 位置: 88-148, 278-304，先做影响集构建，再扫描候选 Blueprint
// ============================================================================
Result.MatchingModules = Request.IsFullScan()
    ? ActiveModules
    : FindModulesForChangedScripts(ActiveModules, Result.NormalizedChangedScripts);

Result.Symbols = BuildImpactSymbols(Result.MatchingModules); // ★ 先构建“受影响符号集合”
Result.CandidateAssets = FindBlueprintAssets(AssetRegistry, Request.bIncludeOnlyOnDiskAssets);

if (AnalyzeLoadedBlueprint(*Blueprint, Result.Symbols, Match.Reasons))
{
    Result.Matches.Add(Match);                           // ★ 输出的是 impacted blueprint 列表
}

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp
// 位置: 201-299，受影响 Blueprint 只刷新节点并排队重新编译
// ============================================================================
if (bShouldRefresh)
{
    const UEdGraphSchema* Schema = Node->GetGraph()->GetSchema();
    Schema->ReconstructNode(*Node, true);               // ★ 刷新依赖节点，不重写整份 schema
}

for (UBlueprint* BP : DependencyBPs)
{
    RefreshRelevantNodesInBP(BP);
    FBlueprintCompilationManager::QueueForCompilation(BP); // ★ 走“影响后修复”而不是“脚本即 Blueprint 真源”
}
```

设计取舍：

- `puerts` 的收益是 TS authoring surface、Blueprint asset、运行时 `UTypeScriptGeneratedClass` 三者共享同一份 schema 真源，开发者在 TS 里改接口就能把 Blueprint 资产压到同形。
- 代价是编辑器工具链拥有更强的“删改资产”权限，安全边界更高，错误的 TS 语义会直接反映成 Blueprint 成员删除和资产重编译。
- `Angelscript` 的收益是 Blueprint 资产仍然主要由 UE/编辑器工作流掌控，脚本变更只做 dependency impact 和节点修复，侵入性更低。
- 代价是它当前没有 `puerts` 这类“脚本源码直接收敛 Blueprint schema”的统一 authoring compiler。

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| Blueprint schema 真源 | `CodeAnalyze.ts` 直接驱动 `PEBlueprintAsset` | `BlueprintImpact` 先分析影响，`ClassReloadHelper` 再刷新节点 | 实现方式不同 |
| 过期成员处理 | `RemoveNotExisted*()` 主动删除 component / variable / function | 当前主链主要重建节点与排队编译 | Angelscript 当前没有实现同等级脚本驱动 schema 收敛器 |
| 持久化时机 | 同轮 `CompileBlueprint() + PromptForCheckoutAndSave()` | 依赖分析后编译 Blueprint，不把脚本当资产唯一真源 | 实现方式不同 |

### [维度 D4] 热重载：`puerts` 的失败回退落在 `UFunction` 指针级别，`Angelscript` 的失败回退落在 module version graph

前面的 D4 已经多次写过 Inspector HMR、`moduleCache`、`ReloadSource/ReloadModule`。本轮新增的是**失败后系统回退到哪一层**。`puerts` 的回退粒度比前文描述得更细：`LazyLoadRedirect()` 先把目标 `UFunction` 改成 `execLazyLoadCallJS`，首次命中时 `execLazyLoadCallJS()` 先 `NotifyReBind()`，再 `RestoreNativeFunc()`。如果某个函数最终没有被 JS override，或者缓存到的旧 `NativeFunc` 本身就是空指针，`RestoreNativeFunc()` 会走 `CancelFunctionRedirection()`，把该函数从“脚本重定向态”撤回 UE 原生绑定态。

`Angelscript` 则是另一种哲学。它把旧模块和新模块挂成 `ReloadOldModule / ReloadNewModule` 对，并通过 `DiffForReferenceUpdate()`、`ReloadState`、`ScriptUpdateMap` 判断这次只是 code changes 还是 structural changes。若当前只能 soft reload 但其实需要 full reload，就明确记录“keeping old angelscript code active”，并把文件扔进 `QueuedFullReloadFiles / PreviouslyFailedReloadFiles`。也就是说，`Angelscript` 的失败记忆和延后恢复是在**模块图**上维护的，不是在单个 `UFunction` 指针上维护的。

```
[puerts] Function-Level Rollback
PEBlueprintAsset::Save()
 -> FunctionToRedirect
 -> RebindJs() marks NeedReBind + LazyLoadRedirect()
 -> execLazyLoadCallJS()
    -> NotifyReBind()
    -> RestoreNativeFunc()
       -> missing JS? CancelFunctionRedirection()
 -> execCallJS()

[Angelscript] Module-Level Rollback
compile changed modules
 -> DiffForReferenceUpdate(old, new)
 -> ReloadState = code-only / structural
 -> SoftReloadOnly && full needed?
    -> keep old code active
    -> QueuedFullReloadFiles / PreviouslyFailedReloadFiles
 -> later full reload / replacement templates
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/TypeScriptGeneratedClass.cpp:57-74,139-167,245-267` 与 `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:1618-1646,2325-2367`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/TypeScriptGeneratedClass.cpp
// 位置: 57-74, 139-167, 245-267，puerts 的回退发生在 UFunction 重定向层
// ============================================================================
DEFINE_FUNCTION(UTypeScriptGeneratedClass::execLazyLoadCallJS)
{
    auto Class = Cast<UTypeScriptGeneratedClass>(Function->GetOuterUClassUnchecked());
    NotifyRebind(Context ? Context->GetClass() : Class); // ★ 首次调用前先尝试重绑
    Class->RestoreNativeFunc();                          // ★ 再恢复/回退 NativeFunc
    execCallJS(Context, Stack, RESULT_PARAM);
}

bool bContains = FunctionToRedirect.Contains(KV.Key);
if (!bContains)
{
    if (InPointer == nullptr)
    {
        CancelFunctionRedirection(Function);            // ★ JS 模块失败或未 override 时，直接撤销重定向
    }
    else
    {
        Function->SetNativeFunc(InPointer);             // ★ 否则恢复原生函数指针
    }
}

void UTypeScriptGeneratedClass::CancelRedirection()
{
    for (TFieldIterator<UFunction> FuncIt(this, EFieldIteratorFlags::ExcludeSuper); FuncIt; ++FuncIt)
    {
        if (FunctionToRedirect.Contains(Function->GetFName()))
        {
            CancelFunctionRedirection(Function);        // ★ 取消脚本重定向，而不是保留一个空壳入口
        }
    }
}

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp
// 位置: 1618-1646, 2325-2367，rebind 只标记 TypeScriptGeneratedClass，再按需注入对象
// ============================================================================
TsClass->DynamicInvoker = TsDynamicInvoker;
TsClass->ClassConstructor = &UTypeScriptGeneratedClass::StaticConstructor;
TsClass->NeedReBind = true;
TsClass->GeneratedObjects.Empty(TsClass->GeneratedObjects.Num());
TsClass->LazyLoadRedirect();                             // ★ 先挂惰性跳板，不立刻全量重建

MakeSureInject(Class, false, false);
FinishInjection(Class);

for (TWeakObjectPtr<UObject>& Iter : Class->GeneratedObjects)
{
    auto Object = Iter.Get();
    if (!Object || ObjectMap.Find(Object))
        continue;
    __USE(FindOrAdd(Isolate, Context, Object->GetClass(), Object, true));
    // ★ 只把已知生成对象重新接回 JS wrapper，不做全局模块级失败队列
}
```

[2] `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3315-3386,3938-3995,4168-4187`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 3315-3386, 3938-3995, 4168-4187，Angelscript 的回退发生在模块与依赖图层
// ============================================================================
if (ScriptModule->ReloadOldModule != nullptr)
{
    ScriptModule->DiffForReferenceUpdate(
        ScriptModule->ReloadOldModule,
        OUT ScriptUpdateMap,
        OUT bHasStructuralChanges);                      // ★ 先比较 old/new module 的结构差异
}

if (bHasStructuralChanges)
    ScriptModule->ReloadState = asCModule::EReloadState::RecompiledWithStructuralChanges;
else
    ScriptModule->ReloadState = asCModule::EReloadState::RecompiledOnlyCodeChanges;

if (CompileType == ECompileType::SoftReloadOnly)
{
    FString Msg =
        TEXT("Full Reload is required due to UPROPERTY() or UFUNCTION() changes, but cannot")
            TEXT(" perform a full reload right now. Keeping old angelscript code active.");
    bShouldSwapInModules = false;                        // ★ 明确保留旧代码继续运行
    bFullReloadRequired = true;
}

if (Result == ECompileResult::ErrorNeedFullReload)
{
    for (const auto& RepeatFile : AllCompiledFiles)
        QueuedFullReloadFiles.Add(RepeatFile);           // ★ 把失败文件记入待 full reload 队列

    PreviouslyFailedReloadFiles.Append(AllCompiledFiles); // ★ 下轮自动重试
}
```

设计取舍：

- `puerts` 的收益是 rollback 非常轻，失败时可以把单个 `UFunction` 撤回原生绑定，不必立刻重建整张模块图。
- 代价是失败记忆弱，当前主链没有像 `Angelscript` 那样的显式 `QueuedFullReloadFiles` / `PreviouslyFailedReloadFiles` 持续恢复状态。
- `Angelscript` 的收益是对结构性变化和失败恢复有清晰的模块级状态机，能够跨轮维护“旧代码继续运行，待条件允许再 full reload”的语义。
- 代价是热重载实现更重，模块 diff、依赖传播、替换模板类型都要进入主链。

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 回退粒度 | `UFunction` 重定向与恢复 | `ReloadOldModule / ReloadNewModule` + `ReloadState` | 实现方式不同 |
| 失败后旧代码保留 | 通过 `RestoreNativeFunc()` / `CancelFunctionRedirection()` 回到原生函数 | `bShouldSwapInModules = false`，明确保留旧 module 继续运行 | 实现方式不同 |
| 失败记忆 | 当前主链未见等价待重试队列 | `QueuedFullReloadFiles` + `PreviouslyFailedReloadFiles` | Angelscript 在失败恢复记忆上实现质量更完整 |

### [维度 D11] 部署与打包：`puerts` 的 native addon 载入链已经是 script-visible runtime contract，`Angelscript` 的 native 扩展仍是启动期 bind-module contract

前面的 D11 多次比较过 `NonUFS DLL`、`.mbc/.cbc`、`PrecompiledScript.Cache`。本轮新增点是：`puerts` 的部署契约不只体现在“有哪些文件要跟包”，还体现在**脚本侧可以直接触达 native addon loader**。`JsEnvImpl` 在非 `QuickJS` 路径把 `puerts.load` 挂进全局对象，并在引导时执行 `puerts/pesaddon.js`；`pesaddon.js` 又把 `puerts.load(filepath)` 包成一个 `Proxy`，脚本代码访问 `addon.SomeClass` 时才进一步 `loadCPPType(module.Class)`。这意味着 DLL 文件名规则、导出入口、module name、类型名空间，都会直接变成 JS 层可观察的 ABI。

`Angelscript` 的 native 扩展边界则明显更宿主内聚。启动时 `FAngelscriptEngine` 先遍历 `FAngelscriptBinds::GetBindModuleNames()`，逐个 `LoadModule()`，然后立刻 `BindScriptTypes()`。脚本层最终只看到“类型已经注册好了”，看不到 `load dll` 这一层，也没有等价的运行时 `addon proxy` 协议。两边都不是“能不能扩展 native”，而是 **扩展是在脚本层运行时装入，还是在宿主启动期装入**。

```
[puerts] Script-Visible Native Addon Load
JS -> global.puerts.load(path)
 -> LoadPesapiDll()
 -> GetDllHandle / GetDllExport(dynamic)
 -> init pesapi function table
 -> pesaddon.js Proxy
 -> loadCPPType(module.Class)

[Angelscript] Startup-Time Native Bind
engine startup
 -> GetBindModuleNames()
 -> FModuleManager::LoadModule()
 -> BindScriptTypes()
 -> scripts see registered types only
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:576-633` 与 `Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/pesaddon.js:24-50`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp
// 位置: 576-633，宿主把 addon loader 直接暴露给 JS 侧
// ============================================================================
#if !defined(WITH_QUICKJS)
PuertsObj
    ->Set(Context, FV8Utils::ToV8String(Isolate, "load"),
        v8::FunctionTemplate::New(Isolate, LoadPesapiDll)->GetFunction(Context).ToLocalChecked())
    .Check();
#endif

PuertsObj->Set(Context, FV8Utils::ToV8String(Isolate, "dll_ext"), FV8Utils::ToV8String(Isolate, DllExt)).Check();

ExecuteModule("puerts/pesaddon.js");                    // ★ 把 JS 侧 addon 包装器装进引导主链

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/pesaddon.js
// 位置: 24-50，JS 侧把 native addon 包成惰性类型代理
// ============================================================================
const org_load = global.puerts.load;
const dll_ext = global.puerts.dll_ext;
const moduleCache = {};

const module_name = org_load(filepath);                 // ★ 先向宿主要一个 module 名
moduleCache[filepath] = new Proxy({__name : module_name}, {
    get: function(classCache, className) {
        if (!(className in classCache)) {
            classCache[className] = puerts.loadCPPType(`${module_name}.${className}`);
            // ★ 真正访问属性时才解析 C++ 类型
        }
        return classCache[className];
    }
});
```

[2] `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PesapiAddonLoad.cpp:76-126,130-181` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1480-1496`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PesapiAddonLoad.cpp
// 位置: 76-126, 130-181，native addon 真正的加载与 ABI 校验
// ============================================================================
void* DllHandle = FPlatformProcess::GetDllHandle(*Path);
const FString EntryName = UTF8_TO_TCHAR(STRINGIFY(PESAPI_MODULE_INITIALIZER(dynamic)));
auto Init = (const char* (*) (pesapi_func_ptr*) )(uintptr_t) FPlatformProcess::GetDllExport(DllHandle, *EntryName);

if (Init)
{
    const char* ModuleName = Init(nullptr);
    GPesapiModuleName = ModuleName;
    Init(funcs);                                        // ★ 宿主把整张 pesapi 函数表注入 addon
    LoadedModules[TCHAR_TO_UTF8(*Path)] = ModuleName;
}
else
{
    auto Ver = (int (*)())(uintptr_t) FPlatformProcess::GetDllExport(DllHandle, *VersionEntryName);
    FV8Utils::ThrowException(Info.GetIsolate(),
        FString::Printf(TEXT("pesapi version mismatch, expect: %d, but got %d"), PESAPI_VERSION, PesapiVersion));
    // ★ addon ABI 不匹配时直接在 runtime 拒绝装入
}

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 1480-1496，Angelscript 的 native 扩展仍是启动期装入
// ============================================================================
if (!FAngelscriptBinds::GetBindModuleNames().IsEmpty())
{
    for (FString ModuleName : FAngelscriptBinds::GetBindModuleNames())
    {
        FModuleManager::Get().LoadModule(FName(ModuleName), ELoadModuleFlags::LogFailures);
        // ★ 先把 bind module 装进宿主，再进入后续 BindScriptTypes
    }
}
BindScriptTypes();
```

设计取舍：

- `puerts` 的收益是 native addon 可以按脚本需求延迟装入，脚本层还能通过 `Proxy` 做惰性类型解析，扩展面更开放。
- 代价是部署 ABI 明显暴露到脚本层；文件名规则、module name、导出约定和 `PESAPI_VERSION` 都进入运行时契约，而且这条链在源码里还是 backend-conditional 的。
- `Angelscript` 的收益是 native 扩展边界更稳定，脚本层不需要知道 DLL/entrypoint/ABI 细节，只消费已经注册好的类型面。
- 代价是当前插件没有 `puerts` 等价的 runtime script-callable native addon loader，扩展通常要提前进入宿主构建和启动图。

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| native 扩展装入时机 | 运行时，由脚本侧 `puerts.load()` 触发 | 启动期，由宿主 `LoadModule()` 触发 | 实现方式不同 |
| 脚本层可见性 | `dll_ext`、`module_name`、`loadCPPType()` 直接进入 JS loader 协议 | 脚本层只看到已注册类型，不直接接触 native loader | 实现方式不同 |
| 运行时 addon loader | 非 `QuickJS` 路径显式存在 `LoadPesapiDll()` | 当前源码未见等价 script-callable addon loader | Angelscript 当前没有实现 |

---

## 深化分析 (2026-04-09 23:24:49)

### [维度 D2] 反射绑定机制：`puerts` 的字段桥接是“按 `FProperty` 类动态挑 marshaller + outer 反向链接”，`Angelscript` 的字段桥接是“按 `FAngelscriptTypeUsage` 预决议值语义”

前面的 D2 已经提过 `weak field + outer link` 的大框架；这一轮继续下钻后，能看到两边在**字段与容器元素**这一层的桥接哲学差别更大。`puerts` 把每个字段访问都还原成一个 `FPropertyTranslator`：初始化时先记住 `PropertyWeakPtr`，对非 `UClass` owner 的 `Struct/Array/Map/Set` 设 `NeedLinkOuter`；真正读字段或取容器元素时，如果返回的是按指针暴露的内部值，就立刻 `LinkOuterImpl(...)` 把 JS inner object 反向挂回 outer，避免内部子值脱离拥有者。Editor 下如果 `FProperty*` 地址因为 hot reload 漂移，`IsPropertyValid()` 还会在访问期重新 `Init(TestP)`。

`Angelscript` 则不在字段访问期再做一层 “按 `FProperty` 动态挑 translator”。它更早地把语义塞进 `FAngelscriptTypeUsage` / `FAngelscriptType`：`FAngelscriptArrayType` 会先看子类型的 `NeedCopy/NeedConstruct/NeedDestruct`，能 `memcpy` 就整段复制，不能才逐元素构造/析构；`Helper_StructType` 直接把 `FStructProperty` 读入 `asIScriptContext` 或从返回值拷回 native。也就是说，puerts 的复杂度落在**访问期 marshaller 工厂**，Angelscript 的复杂度落在**类型对象的能力表**。

```
[puerts] Field Access Marshalling
FProperty
 -> FPropertyTranslator::Create()          // 运行时按字段类挑 translator
 -> Getter / ContainerWrapper::GetRef      // 访问期桥接
 -> NeedLinkOuter ? LinkOuterImpl()        // 内部值反向挂回 outer
 -> JS sees nested object/value

[Angelscript] Field Access Marshalling
FProperty
 -> FAngelscriptTypeUsage::FromProperty    // 先决议脚本类型语义
 -> Type.NeedCopy/NeedConstruct            // 提前决定值语义
 -> SetArgument / GetReturnValue           // 直接写入 asIScriptContext
 -> Script sees typed value/reference
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.h:105-124,179-190`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.h
// 位置: 105-124, 179-190，字段 translator 的 owner/outer 语义与自修复入口
// ============================================================================
explicit FPropertyTranslator(PropertyMacro* InProperty)
{
    Init(InProperty);
}

FORCEINLINE void Init(PropertyMacro* InProperty)
{
    Property = InProperty;
    PropertyWeakPtr = InProperty;                      // ★ 保存弱字段引用，给 Editor 漂移自修复留入口
    OwnerIsClass = InProperty->GetOwnerClass() != nullptr;
    NeedLinkOuter = false;
    if (!OwnerIsClass)
    {
        if ((InProperty->IsA<StructPropertyMacro>() && StructProperty->Struct != FArrayBuffer::StaticStruct() &&
                StructProperty->Struct != FArrayBufferValue::StaticStruct() &&
                StructProperty->Struct != FJsObject::StaticStruct()) ||
            InProperty->IsA<MapPropertyMacro>() || InProperty->IsA<ArrayPropertyMacro>() || InProperty->IsA<SetPropertyMacro>())
        {
            NeedLinkOuter = true;                      // ★ 非类 owner 的内部值需要把 outer 关系重新挂回 JS 对象图
        }
    }
}

bool IsPropertyValid()
{
    if (!PropertyWeakPtr.IsValid())
    {
        return false;
    }
#if WITH_EDITOR
    FProperty* TestP = PropertyWeakPtr.Get();
    if (TestP != Property)
    {
        Init(TestP);                                  // ★ Editor 场景字段地址漂移时，访问期即时重绑
    }
```

[2] `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1225-1346` 与 `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/ContainerWrapper.cpp:84-89,350-354,591-595`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp
// 位置: 1225-1346，按 FProperty 子类动态挑 marshaller
// ============================================================================
if (InProperty->IsA<BytePropertyMacro>() || InProperty->IsA<Int8PropertyMacro>() || InProperty->IsA<Int16PropertyMacro>() ||
    InProperty->IsA<IntPropertyMacro>() || InProperty->IsA<UInt16PropertyMacro>())
{
    return Creator<FInt32PropertyTranslator>::Do(InProperty, IgnoreOut, Ptr);
}
else if (InProperty->IsA<StructPropertyMacro>())
{
    auto StructProperty = CastFieldMacro<StructPropertyMacro>(InProperty);
    if (StructProperty->Struct == FArrayBuffer::StaticStruct())
    {
        return Creator<FArrayBufferPropertyTranslator>::Do(InProperty, IgnoreOut, Ptr);
    }
    else if (StructProperty->Struct == FArrayBufferValue::StaticStruct())
    {
        return Creator<FArrayBufferValuePropertyTranslator>::Do(InProperty, IgnoreOut, Ptr);
    }
    else if (StructProperty->Struct == FJsObject::StaticStruct())
    {
        return Creator<FJsObjectPropertyTranslator>::Do(InProperty, IgnoreOut, Ptr);
    }
    else
    {
        return Creator<FScriptStructPropertyTranslator>::Do(InProperty, IgnoreOut, Ptr);
    }
}
else if (InProperty->IsA<ArrayPropertyMacro>())
{
    return Creator<FScriptArrayPropertyTranslator>::Do(InProperty, IgnoreOut, Ptr);
}
else if (InProperty->IsA<MapPropertyMacro>())
{
    return Creator<FScriptMapPropertyTranslator>::Do(InProperty, IgnoreOut, Ptr);
}
else if (InProperty->IsA<SetPropertyMacro>())
{
    return Creator<FScriptSetPropertyTranslator>::Do(InProperty, IgnoreOut, Ptr);
}
else
{
    return Creator<DoNothingPropertyTranslator>::Do(InProperty, IgnoreOut,
        Ptr);    // ★ 当前不支持的字段直接降级，避免加载期炸掉整条链
}

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/ContainerWrapper.cpp
// 位置: 84-89, 350-354, 591-595，容器元素返回时反向挂 outer
// ============================================================================
auto Ret = Inner->UEToJs(Isolate, Context, DataPtr, PassByPointer);
if (Inner->NeedLinkOuter && PassByPointer)
{
    LinkOuterImpl(Context, Info.Holder(), Ret);       // ★ Array 元素引用挂回 owning container
}

auto Ret = Inner->UEToJs(Isolate, Context, Data, PassByPointer);
if (Inner->NeedLinkOuter && PassByPointer)
{
    LinkOuterImpl(Context, Info.Holder(), Ret);       // ★ Set 元素同样走 outer 反链
}

auto Ret = ValuePropertyTranslator->UEToJs(Isolate, Context, ValuePtr, PassByPointer);
if (ValuePropertyTranslator->NeedLinkOuter && PassByPointer)
{
    LinkOuterImpl(Context, Info.Holder(), Ret);       // ★ Map value 引用也保持 owner 关系
}
```

[3] `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp:124-174,219-250,1769-1777` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_StructType.h:29-34,36-70`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp
// 位置: 124-174, 219-250, 1769-1777，数组值语义在类型层先决议
// ============================================================================
bool FAngelscriptArrayType::NeedCopy(const FAngelscriptTypeUsage& Usage) const  { return true; }
void FAngelscriptArrayType::CopyValue(const FAngelscriptTypeUsage& Usage, void* SourcePtr, void* DestinationPtr) const 
{
    const FAngelscriptTypeUsage& SubType = Usage.SubTypes[0];
    FScriptArray& SourceArray = *(FScriptArray*)SourcePtr;
    FScriptArray& DestinationArray = *(FScriptArray*)DestinationPtr;

    if (!SubType.NeedCopy())
    {
        // ★ 子类型是 POD 时直接 memcpy，不再按字段级别找 translator
        FMemory::Memcpy(DestinationArray.GetData(), SourceArray.GetData(), SourceNum * ElementSize);
        return;
    }

    if (SubType.NeedConstruct())
    {
        for (int32 i = DestNum; i < SourceNum; ++i)
            SubType.ConstructValue((void*)((SIZE_T)DestinationArray.GetData() + (i * ElementSize)));
    }

    for (int32 i = 0; i < SourceNum; ++i)
    {
        SubType.CopyValue(
            (void*)((SIZE_T)SourceArray.GetData() + (i * ElementSize)),
            (void*)((SIZE_T)DestinationArray.GetData() + (i * ElementSize)));
    }
}

void FAngelscriptArrayType::SetArgument(const FAngelscriptTypeUsage& Usage, int32 ArgumentIndex, class asIScriptContext* Context, struct FFrame& Stack, const FArgData& Data) const 
{
    FScriptArray* Arg = (FScriptArray*)Data.StackPtr;
    new(Arg) FScriptArray();

    if (Usage.bIsReference)
    {
        FScriptArray& Ref = Stack.StepCompiledInRef<FArrayProperty,FScriptArray>(Arg);
        Context->SetArgAddress(ArgumentIndex, &Ref);   // ★ 直接把 UE array ref 交给脚本 VM
    }
    else
    {
        Stack.StepCompiledIn<FArrayProperty>(Arg);
        Context->SetArgObject(ArgumentIndex, Arg);
    }
}

Ops->NumBytesPerElement = Type.GetValueSize();
Ops->bNeedConstruct = Type.NeedConstruct();
Ops->bNeedDestruct = Type.NeedDestruct();
Ops->bNeedCopy = Type.NeedCopy();                      // ★ 容器操作缓存来自 type usage，不来自访问期 FProperty 判断

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_StructType.h
// 位置: 29-34, 36-70，struct 参数/返回直接通过 FAngelscriptType 写入 context
// ============================================================================
FProperty* CreateProperty(const FAngelscriptTypeUsage& Usage, const FAngelscriptType::FPropertyParams& Params) const override
{
    auto* StructProp = new FStructProperty(Params.Outer, Params.PropertyName, RF_Public);
    StructProp->Struct = GetStruct(Usage);
    return StructProp;
}

void SetArgument(const FAngelscriptTypeUsage& Usage, int32 ArgumentIndex, class asIScriptContext* Context, struct FFrame& Stack, const FAngelscriptType::FArgData& Data) const override
{
    NativeType* ValuePtr = (NativeType*)Data.StackPtr;
    new(ValuePtr) NativeType();

    if (Usage.bIsReference)
    {
        NativeType& ObjRef = Stack.StepCompiledInRef<FStructProperty, NativeType>(ValuePtr);
        Context->SetArgAddress(ArgumentIndex, &ObjRef); // ★ 类型对象直接定义参数桥接方式
    }
    else
    {
        Stack.StepCompiledIn<FStructProperty>(ValuePtr);
        Context->SetArgObject(ArgumentIndex, ValuePtr);
    }
}

void GetReturnValue(const FAngelscriptTypeUsage& Usage, class asIScriptContext* Context, void* Destination) const override
{
    if(Usage.bIsReference)
    {
        *(NativeType**)Destination = (NativeType*)Context->GetReturnAddress();
    }
    else
    {
        void* ReturnedObject = Context->GetReturnObject();
        if (ReturnedObject == nullptr)
            return;
        *(NativeType*)Destination = *(NativeType*)ReturnedObject;
    }
}
```

设计取舍：

- `puerts` 的收益是字段 marshaller 对 `FProperty` 子类非常敏感，Editor 场景下还能在访问期自修复漂移字段；容器内部引用也能靠 `LinkOuterImpl()` 保持对象图一致性。
- 代价是字段访问热路径更依赖运行时工厂、弱字段检查和 outer 关系修补，复杂度落在 marshaller 层。
- `Angelscript` 的收益是 `FAngelscriptType` 把 copy/construct/destruct/arg/return 规则统一收束，容器操作一旦缓存 `Ops` 就可以重复使用。
- 代价是类型能力需要更早建模，新增一种值语义通常意味着补 `FAngelscriptType` 实现，而不是只加一个字段 translator。

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 字段 marshaller 选择时机 | 访问期按 `FProperty` 子类动态挑 `FPropertyTranslator` | 类型建立期按 `FAngelscriptTypeUsage` 决定语义 | 实现方式不同 |
| 容器内部引用 owner 关系 | `NeedLinkOuter` + `LinkOuterImpl()` 显式维护 | 当前这条主链直接按值/引用语义进 `asIScriptContext` | 实现方式不同 |
| Editor 字段漂移修补 | `PropertyWeakPtr` + `Init(TestP)` 访问期自修复 | 当前主链更依赖类型/重载链稳定性，而不是字段访问期自修复 | 实现方式不同 |

### [维度 D6] 代码生成与 IDE 支持：`puerts` 把固定版本 TypeScript compiler 随插件内嵌发布，`Angelscript` 把 IDE 面收敛为源码导航与 debug metadata

前面的 D6 已经讲过 `.d.ts`、`CodeAnalyze` 和 Blueprint metadata。本轮补的是一个更基础的问题：**IDE 工具链到底依赖谁**。从当前 UE 插件源码看，`puerts` 不是调用外部 `tsserver` 或用户自装 Node 环境，而是把 `typescript@4.7.4` 直接打进插件 `Content/JavaScript/PuertsEditor/node_modules/`，然后由 `FPuertsEditorModule::OnPostEngineInit()` 新建一份 `FJsEnv`，在 UE 编辑器内直接运行 `PuertsEditor/CodeAnalyze`。`CodeAnalyze.js` 又主动改写 `ts.sys`，把文件读取、目录扫描、MD5 版本号、增量快照、`LanguageService` 异常恢复都接到 `UE.FileSystemOperation` 上。

`Angelscript` 这边则是另一条线。`StartupModule()` 只注册源码导航 handler，不去启动一份内嵌脚本编译器；`UASFunction` 自己携带 `GetSourceFilePath()` / `GetSourceLineNumber()`，`FAngelscriptSourceCodeNavigation` 用它直接打开源码；`FAngelscriptDebugServer` 再把 `DebugDatabase` 与 `AssetDatabase` 推给外部客户端。也就是说，Angelscript 当前的 IDE 支撑主线更像“**把 live metadata 和源码坐标暴露出去**”，而不是在插件内部常驻一份语义编译器。

```
[puerts] IDE Toolchain Ownership
PuertsEditor package
 -> bundled typescript@4.7.4
 -> FJsEnv starts CodeAnalyze
 -> ts.LanguageService in UE process
 -> emit .js / refresh Blueprint / write version cache

[Angelscript] IDE Toolchain Ownership
AngelscriptEditor startup
 -> register source navigation
 -> UASFunction exposes file/line
 -> DebugServer sends DebugDatabase
 -> external IDE/client consumes metadata
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Content/JavaScript/PuertsEditor/package.json:1-10` 与 `Reference/puerts/unreal/Puerts/Content/JavaScript/PuertsEditor/CodeAnalyze.js:3-7,57-58,210-280,323-387,452-540`

```javascript
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Content/JavaScript/PuertsEditor/package.json
// 位置: 1-10，PuertsEditor 把 TypeScript 编译器随插件一起分发
// ============================================================================
{
  "name": "PuertsEditor",
  "version": "1.0.0",
  "dependencies": {
    "typescript": "4.7.4"                  // ★ 固定编译器版本，不依赖用户本机 tsserver
  }
}

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Content/JavaScript/PuertsEditor/CodeAnalyze.js
// 位置: 3-7, 57-58, 210-280, 323-387, 452-540，UE 内嵌 TS host + LanguageService
// ============================================================================
const UE = require("ue");
const puerts_1 = require("puerts");
const ts = require("typescript");           // ★ 直接加载插件自带的 TypeScript runtime
const tsi = require("./TypeScriptInternal");
const uemeta = require("./UEMeta");

function getExecutingFilePath() {
    return getCurrentDirectory() + "Content/JavaScript/PuertsEditor/node_modules/typescript/lib/tsc.js";
    // ★ 把 ts 编译器入口固定到插件目录
}

const versionsFilePath = tsi.getDirectoryPath(configFilePath) + "/ts_file_versions_info.json";
const fileVersions = {};
fileNames.forEach(fileName => {
    fileVersions[fileName] = { version: UE.FileSystemOperation.FileMD5Hash(fileName), processed: false, isBP: false };
    // ★ 增量失效依赖的是 UE 侧 MD5，不是外部 watch server
});

let service = ts.createLanguageService(servicesHost, ts.createDocumentRegistry());
function getProgramFromService() {
    while (true) {
        try {
            return service.getProgram();
        }
        catch (e) {
        }
        service = ts.createLanguageService(servicesHost, ts.createDocumentRegistry());
        // ★ UE 文件读取偶发失败时，直接在插件内重建 LanguageService
    }
}

let diagnostics = ts.getPreEmitDiagnostics(program);
if (diagnostics.length > 0) {
    logErrors(diagnostics);
}
else {
    onSourceFileAddOrChange(fileName, false, program, true, false);
    refreshBlueprints();
    UE.FileSystemOperation.WriteFile(versionsFilePath, JSON.stringify(fileVersions, null, 4));
}

if (!sourceFile.isDeclarationFile) {
    let emitOutput = service.getEmitOutput(sourceFilePath);
    if (!emitOutput.emitSkipped) {
        UE.FileSystemOperation.WriteFile(output.name, output.text);  // ★ 直接在 UE 内发射 .js/.mjs
    }
    if (moduleFileName && reload) {
        UE.FileSystemOperation.PuertsNotifyChange(moduleFileName, jsSource);
        // ★ 语义分析、代码生成、reload 通知在同一宿主里闭环
    }
    pendingBlueprintRefleshJobs.push({ type: foundType, op: () => onBlueprintTypeAddOrChange(foundBaseTypeUClass, foundType, modulePath) });
}
```

[2] `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:110-150`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp
// 位置: 110-150，编辑器启动时直接拉起一份 JS runtime 来跑工具链
// ============================================================================
TSharedPtr<FKismetCompilerContext> MakeCompiler(
    UBlueprint* InBlueprint, FCompilerResultsLog& InMessageLog, const FKismetCompilerOptions& InCompileOptions)
{
    return MakeShared<FTypeScriptCompilerContext>(CastChecked<UTypeScriptBlueprint>(InBlueprint), InMessageLog, InCompileOptions);
}

void FPuertsEditorModule::OnPostEngineInit()
{
    if (Enabled)
    {
        FKismetCompilerContext::RegisterCompilerForBP(UTypeScriptBlueprint::StaticClass(), &MakeCompiler);

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
        JsEnv = MakeShared<PUERTS_NAMESPACE::FJsEnv>(
            std::make_shared<PUERTS_NAMESPACE::DefaultJSModuleLoader>(TEXT("JavaScript")),
            std::make_shared<PUERTS_NAMESPACE::FDefaultLogger>(), -1,
            [this](const FString& InPath)
            {
                if (SourceFileWatcher.IsValid())
                {
                    SourceFileWatcher->OnSourceLoaded(InPath);
                }
            },
            TEXT("--max-old-space-size=2048"));

        JsEnv->Start("PuertsEditor/CodeAnalyze");      // ★ IDE/compiler 主链就是插件内一份常驻 JsEnv
    }
}
```

[3] `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:351-355,713-743`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp:34-44,136-138`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:1535-1559`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:1493-1515,2044-2148`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 位置: 351-355, 713-743，编辑器主链以导航入口为主
// ============================================================================
void FAngelscriptEditorModule::StartupModule()
{
    FClassReloadHelper::Init();
    RegisterAngelscriptSourceNavigation();            // ★ 注册导航 handler，而不是拉起内嵌语言运行时
}

Section.AddMenuEntry(
    "ASOpenCode",
    NSLOCTEXT("Angelscript", "OpenCode.Label", "Open Angelscript workspace (VS Code)"),
    NSLOCTEXT("Angelscript", "OpenCode.ToolTip", "Opens Visual Studio Code in this project's Angelscript workspace"),
    FSourceCodeNavigation::GetOpenSourceCodeIDEIcon(),
    Action);

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp
// 位置: 34-44, 136-138，导航直接吃脚本函数的源码坐标
// ============================================================================
virtual bool NavigateToFunction(const UFunction* InFunction) override
{
    auto* ASFunc = Cast<const UASFunction>(InFunction);
    if (ASFunc == nullptr)
        return false;
    FString Path = ASFunc->GetSourceFilePath();
    if (Path.Len() == 0)
        return false;

    OpenFile(Path, ASFunc->GetSourceLineNumber());    // ★ 直接跳源码，不经过内嵌语义服务
    return true;
};

void RegisterAngelscriptSourceNavigation()
{
    FSourceCodeNavigation::AddNavigationHandler(new FAngelscriptSourceCodeNavigation);
}

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp
// 位置: 1535-1559，脚本函数对象本身携带源码定位信息
// ============================================================================
FString UASFunction::GetSourceFilePath() const
{
    if (ScriptFunction == nullptr)
        return TEXT("");
    auto& Manager = FAngelscriptEngine::Get();
    auto Module = Manager.GetModule(ScriptFunction->GetModule());
    if (!Module.IsValid() || Module->Code.Num() == 0)
        return TEXT("");
    return Module->Code[0].AbsoluteFilename;          // ★ 直接回到脚本模块源码路径
}

int UASFunction::GetSourceLineNumber() const
{
    if (ScriptFunction == nullptr)
        return -1;
    auto* RealFunc = ((asCScriptFunction*)ScriptFunction);
    auto* scriptData = RealFunc->scriptData;
    if (scriptData == nullptr)
        return -1;
    return (scriptData->declaredAt & 0xFFFFF) + 1;    // ★ 行号来自 script function 元数据
}

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 位置: 1493-1515, 2044-2148，向外部 client 广播 debug database / asset database
// ============================================================================
void FAngelscriptDebugServer::SendDebugDatabase(FSocket* Client)
{
    FAngelscriptDebugDatabaseSettings DebugSettings;
    DebugSettings.bAutomaticImports = FAngelscriptEngine::Get().ShouldUseAutomaticImportMethod();
    SendMessageToClient(Client, EDebugMessageType::DebugDatabaseSettings, DebugSettings);

    FAngelscriptDebugDatabase DB;
    FJsonSerializer::Serialize(Root, JsonWriter);
    SendMessageToClient(Client, EDebugMessageType::DebugDatabase, DB);  // ★ IDE/client 主要消费的是 runtime 导出的 metadata
}

SendMessageToClient(Client, EDebugMessageType::DebugDatabaseFinished, Message);
SendAssetDatabase(Client);                             // ★ 资产更新继续以消息流推给外部 client
AssetRegistry.OnAssetAdded().AddLambda([this](const FAssetData& AssetData)
{
    for (auto* ConnectedClient : ClientsThatWantDebugDatabase)
        SendMessageToClient(ConnectedClient, EDebugMessageType::AssetDatabase, UpdateMessage);
});
```

设计取舍：

- `puerts` 的收益是 IDE/compiler 行为在插件里是封闭且可复现的，TypeScript 版本、文件系统抽象、增量失效逻辑都由插件自己控制。
- 代价是编辑器工具链要常驻一份脚本 VM 与 TypeScript runtime，`PuertsEditor` 本身就成了一个运行中的小型编译器宿主。
- `Angelscript` 的收益是编辑器主链保持原生 C++，把 IDE 对接面收敛成源码导航、debug database 与资产数据库，宿主更轻。
- 代价是当前检视到的源码里未见 puerts 等价的“插件内嵌语义编译器”；外部 IDE 若想拿到更深 authoring 语义，需要继续消费 debug metadata 或自行实现客户端逻辑。

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 编译器归属 | 插件内嵌 `typescript@4.7.4`，在 UE 进程内跑 `LanguageService` | 当前主链是源码导航 + `DebugDatabase` 输出 | 实现方式不同 |
| 增量失效机制 | `ts_file_versions_info.json` + `FileMD5Hash` + 重建 `LanguageService` | 当前源码主链未见等价 editor 内嵌语义服务 | Angelscript 当前源码未见等价实现 |
| IDE 对接契约 | 工具链自己发射 `.js`、通知 reload、刷新 Blueprint | 暴露源码路径、行号、debug database 给外部 IDE/client | 实现方式不同 |

### [维度 D8] 性能与优化：`puerts` 的快路径受 backend ABI 覆盖面限制，`Angelscript` 的快路径在函数分型阶段就固定下来

前面的 D8 已经多轮比较过 `FastCall`、GC、scheduler 和 `StaticJIT`。这一轮补的是**快路径覆盖边界**。`puerts` 的 `V8FastCall` 不是“所有绑定都快”，而是只有当参数和返回值都满足 `IsArgsSupportedHelper` / `IsReturnSupportedHelper` 时，模板才会实例化；而且这条链还被 `#ifndef WITH_QUICKJS` 卡住，说明 QuickJS 后端根本不会吃这套 V8 `CFunction` 快路径。运行时在 `StructWrapper` 挂方法时，也要先拿 `ReflectionInfo->FastCallInfo()`，拿不到就退回普通 `v8::FunctionTemplate::New(...)`。

`Angelscript` 的快路径则更像**在类生成时就把调用形状定死**。`UASFunction` 有一整套 `NoParams / DWordArg / QWordArg / FloatArg / ReferenceArg / ByteReturn ...` 以及对应的 `_JIT` 子类；`AllocateFunctionFor()` 根据参数个数、primitive 大小、引用形态、返回类型和 `jitFunction_*` 是否齐全，一次性挑好最终的 thunk class。再往下，`TAngelscriptPODType` 又把 `NeedCopy/NeedConstruct/NeedDestruct` 都标成 `false`，使得容器和参数桥接可以走更轻的 POD 路径。也就是说，puerts 的快路径首先受**backend ABI** 限制，Angelscript 的快路径首先受**函数形状分类** 限制。

```
[puerts] Fast Path Eligibility
C++ method
 -> V8FastCall trait check               // 参数/返回必须落入支持集合
 -> ReflectionInfo->FastCallInfo()       // 绑定时再判断是否可用
 -> !WITH_QUICKJS ? attach CFunction : fallback callback
 -> V8 fast stub or generic stub

[Angelscript] Fast Path Eligibility
FAngelscriptFunctionDesc
 -> AllocateFunctionFor()
 -> choose UASFunction_* / *_JIT class   // 生成期按形状分型
 -> RuntimeCallFunction thunk fixed
 -> POD types skip copy/construct where possible
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/V8FastCall.hpp:19-39,47-77,112-118,164-205` 与 `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:195-240`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Public/V8FastCall.hpp
// 位置: 19-39, 47-77, 112-118, 164-205，V8 FastCall 的 ABI 覆盖边界
// ============================================================================
template <typename T>
struct FastCallArgument<T, typename std::enable_if<std::is_pointer<T>::value && !std::is_same<T, const char*>::value &&
                                                   !std::is_enum<typename std::remove_pointer<T>::type>::value &&
                                                   !std::is_integral<typename std::remove_pointer<T>::type>::value &&
                                                   !std::is_floating_point<typename std::remove_pointer<T>::type>::value>::type>
{
    using DeclType = v8::Local<v8::Value>;

    static T Get(v8::Local<v8::Value> v)
    {
        if (V8_LIKELY(v->IsObject()))
        {
            return static_cast<T>(DataTransfer::GetPointerFast<void>(v.As<v8::Object>()));
        }
        return nullptr;
    }
};

template <typename T>
struct FastCallArgument<T, typename std::enable_if<std::is_enum<T>::value>::type>
{
    using DeclType = int;
};

template <typename T>
struct FastCallArgument<T, typename std::enable_if<std::is_integral<T>::value || std::is_floating_point<T>::value>::type>
{
    using DeclType = typename std::decay<T>::type;
};

template <typename T>
struct IsArgSupportedHelper<T, internal::fastcallutil::Void_t<decltype(&FastCallArgument<T>::Get)>> : std::true_type
{
};                                                  // ★ 只有定义了 FastCallArgument::Get 的类型才算“支持”

template <typename Ret, typename... Args, Ret (*func)(Args...)>
struct V8FastCall<Ret (*)(Args...), func,
    typename std::enable_if<IsReturnSupportedHelper<Ret>::value && IsArgsSupportedHelper<std::tuple<Args...>>::value &&
                            (sizeof...(Args) > 0)>::type>
{
    static Ret Wrap(v8::Local<v8::Object> receiver_obj, typename FastCallArgument<Args>::DeclType... args)
    {
        return func(FastCallArgument<Args>::Get(args)...); // ★ 只给满足 ABI 约束的方法生成 CFunction 包装
    }
};

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp
// 位置: 195-240，绑定时再看 backend 和 reflection 是否允许 FastCall
// ============================================================================
#ifndef WITH_QUICKJS
auto FastCallInfo = FunctionInfo->ReflectionInfo ? FunctionInfo->ReflectionInfo->FastCallInfo() : nullptr;
if (FastCallInfo)
{
    Result->PrototypeTemplate()->Set(FV8Utils::InternalString(Isolate, FunctionInfo->Name),
        v8::FunctionTemplate::New(Isolate, (v8::FunctionCallback) FunctionInfo->Callback,
            FunctionInfo->Data ? static_cast<v8::Local<v8::Value>>(v8::External::New(Isolate, FunctionInfo->Data))
                               : v8::Local<v8::Value>(),
            v8::Local<v8::Signature>(), 0, v8::ConstructorBehavior::kThrow, v8::SideEffectType::kHasSideEffect,
            FastCallInfo));                              // ★ 只有 V8 路径且拿到 FastCallInfo 才挂快路径
}
else
#endif
{
    Result->PrototypeTemplate()->Set(FV8Utils::InternalString(Isolate, FunctionInfo->Name),
        v8::FunctionTemplate::New(Isolate, (v8::FunctionCallback) FunctionInfo->Callback,
            FunctionInfo->Data ? static_cast<v8::Local<v8::Value>>(v8::External::New(Isolate, FunctionInfo->Data))
                               : v8::Local<v8::Value>())); // ★ 否则退回普通 callback stub
}
```

[2] `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h:230-360` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:1762-1850`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h
// 位置: 230-360，按调用形状预先准备一组专门 thunk class
// ============================================================================
class ANGELSCRIPTRUNTIME_API UASFunction_NoParams : public UASFunction
{
    GENERATED_BODY()
public:
    virtual void RuntimeCallFunction(UObject* Object, FFrame& Stack, RESULT_DECL) override;
    virtual void RuntimeCallEvent(UObject* Object, void* Parms) override;
};

class ANGELSCRIPTRUNTIME_API UASFunction_DWordArg : public UASFunction
{
    GENERATED_BODY()
public:
    virtual void RuntimeCallFunction(UObject* Object, FFrame& Stack, RESULT_DECL) override;
    virtual void RuntimeCallEvent(UObject* Object, void* Parms) override;
};

class ANGELSCRIPTRUNTIME_API UASFunction_QWordArg : public UASFunction
{
    GENERATED_BODY()
public:
    virtual void RuntimeCallFunction(UObject* Object, FFrame& Stack, RESULT_DECL) override;
    virtual void RuntimeCallEvent(UObject* Object, void* Parms) override;
};

class ANGELSCRIPTRUNTIME_API UASFunction_JIT : public UASFunction
{
    GENERATED_BODY()
public:
    virtual void RuntimeCallFunction(UObject* Object, FFrame& Stack, RESULT_DECL) override;
    virtual void RuntimeCallEvent(UObject* Object, void* Parms) override;
};                                                  // ★ 同一形状族再分出 JIT 版本

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp
// 位置: 1762-1850，函数生成期就选定最终快路径类
// ============================================================================
const bool bHasNonVirtualJitFunction = ScriptFunction != nullptr
    && ScriptFunction->jitFunction != nullptr
    && ScriptFunction->jitFunction_Raw != nullptr
    && ScriptFunction->jitFunction_ParmsEntry != nullptr
    && ScriptFunction->traits.GetTrait(asTRAIT_FINAL);

if (FunctionDesc->bThreadSafe)
{
    if (bHasNonVirtualJitFunction)
        return NewObject<UASFunction_JIT>(InClass, ObjectName, RF_Public);
    else
        return NewObject<UASFunction>(InClass, ObjectName, RF_Public);
}

if (!FunctionDesc->ReturnType.IsValid() && FunctionDesc->Arguments.Num() == 0)
{
    if (bHasNonVirtualJitFunction)
        return NewObject<UASFunction_NoParams_JIT>(InClass, ObjectName, RF_Public);
    else
        return NewObject<UASFunction_NoParams>(InClass, ObjectName, RF_Public);
}

if (!FunctionDesc->ReturnType.IsValid()
    && FunctionDesc->Arguments.Num() == 1
    && !FunctionDesc->Arguments[0].Type.bIsReference
    && FunctionDesc->Arguments[0].Type.IsPrimitive())
{
    int32 ArgSize = FunctionDesc->Arguments[0].Type.GetValueSize();
    if (ArgSize == 1)
    {
        return bHasNonVirtualJitFunction
            ? NewObject<UASFunction_ByteArg_JIT>(InClass, ObjectName, RF_Public)
            : NewObject<UASFunction_ByteArg>(InClass, ObjectName, RF_Public);
    }
    else if (ArgSize == 4)
    {
        return bHasNonVirtualJitFunction
            ? NewObject<UASFunction_DWordArg_JIT>(InClass, ObjectName, RF_Public)
            : NewObject<UASFunction_DWordArg>(InClass, ObjectName, RF_Public);
    }
    else if (ArgSize == 8)
    {
        return bHasNonVirtualJitFunction
            ? NewObject<UASFunction_QWordArg_JIT>(InClass, ObjectName, RF_Public)
            : NewObject<UASFunction_QWordArg>(InClass, ObjectName, RF_Public);
    }
}
// ★ 调用形状一旦分类完成，运行期就不再重新判断“能否走快路径”
```

[3] `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_PODType.h:15-34,95-120`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_PODType.h
// 位置: 15-34, 95-120，POD 类型把复制/构造成本显式压到最低
// ============================================================================
struct TAngelscriptPODType : FAngelscriptType
{
    bool CanCopy(const FAngelscriptTypeUsage& Usage) const override { return true; }
    bool NeedCopy(const FAngelscriptTypeUsage& Usage) const override { return false; }      // ★ POD 不需要额外 copy 语义

    bool CanConstruct(const FAngelscriptTypeUsage& Usage) const override { return true; }
    bool NeedConstruct(const FAngelscriptTypeUsage& Usage) const override { return false; } // ★ POD 不需要显式构造

    bool CanDestruct(const FAngelscriptTypeUsage& Usage) const override { return true; }
    bool NeedDestruct(const FAngelscriptTypeUsage& Usage) const override { return false; }  // ★ POD 不需要显式析构
};

void SetArgument(const FAngelscriptTypeUsage& Usage, int32 ArgumentIndex, class asIScriptContext* Context, struct FFrame& Stack, const FAngelscriptType::FArgData& Data) const override
{
    NativeType* ValuePtr = (NativeType*)Data.StackPtr;
    if (Usage.bIsReference)
    {
        NativeType& ObjRef = Stack.StepCompiledInRef<PropertyType, NativeType>(ValuePtr);
        Context->SetArgAddress(ArgumentIndex, &ObjRef);
    }
    else
    {
        Stack.StepCompiledIn<PropertyType>(ValuePtr);
        Context->SetArgObject(ArgumentIndex, ValuePtr);
    }
}

void GetReturnValue(const FAngelscriptTypeUsage& Usage, class asIScriptContext* Context, void* Destination) const override
{
    if (Usage.bIsReference)
    {
        *(NativeType**)Destination = (NativeType*)Context->GetReturnAddress();
    }
    else
    {
        *(NativeType*)Destination = (NativeType)Context->GetReturnDWord(); // ★ POD 返回也走轻量路径
    }
}
```

设计取舍：

- `puerts` 的收益是只要命中 V8 `CFunction` 支持集合，就能把 JS stub 压得很薄；而且不需要为每种函数形状维护一组 UE `UFunction` 子类。
- 代价是快路径覆盖面受 backend 和 ABI 双重约束，QuickJS 路径天然吃不到这套优化，复杂签名也会自动退回普通 callback。
- `Angelscript` 的收益是同一套单引擎 runtime 里，调用形状一旦分类完成，后续每次调用都走固定 thunk；POD 语义还被显式编码进 type system。
- 代价是为了覆盖这些热路径，需要维护一整套 `UASFunction_*` 分类和相应的生成逻辑，工程面更重。

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| 快路径依赖 | `V8FastCall` 仅对支持 ABI 且非 `QuickJS` 的绑定生效 | 单后端，不存在 backend 切换导致的快路径能力面差异 | 实现方式不同 |
| 快路径决定时机 | 绑定时根据 `FastCallInfo` 决定是否附加 `CFunction` | `AllocateFunctionFor()` 生成期一次性选定 `UASFunction_*` | 实现方式不同 |
| POD 值语义优化 | 当前这条链主要优化 call stub | `NeedCopy/NeedConstruct/NeedDestruct` 显式进入 type system | 实现方式不同 |

---

## 深化分析 (2026-04-09 23:39:31)

### [维度 D1] 插件架构与模块划分：`puerts` 的公共 schema 先注册到进程级单例，再由每个 `JsEnv` 消费；`Angelscript` 的 schema 则在引擎实例上回放

前面几轮已经把模块数量、多后端和 editor/runtime 分层写清；这一轮只补“注册表活在哪一层”。`puerts` 的 `AutoRegisterForUE`、`AutoRegisterForPEM` 都是 C++ 静态对象，模块装载时就执行 `DefineClass<>().Register()`。`Register()` 最终走 `RegisterJSClass(ClassDef)`，并落到 `GetJSClassRegister()` 的进程级静态单例里。结果是 editor-side `FJsEnv`、runtime-side `FJsEnv`、甚至后续重建出来的新 `FJsEnv`，看到的是同一张 class/addon registry；变化发生在 env 生命周期，而不是 schema 是否重建。

`Angelscript` 则把绑定条目当成 engine-instance 初始化步骤。无论是手写 `Bind_*.cpp`，还是 `AngelscriptEditorModule.cpp` 生成的 bind module，都先把 lambda 塞给 `FAngelscriptBinds::RegisterBinds(...)`；真正打到 `asIScriptEngine` 上，要等 `CallBinds()` 遍历排序后的 bind array。这样做的收益是当前引擎实例可以带自己的 `DisabledBindNames`，但代价就是每个引擎实例都要回放一次注册序列。

```
[puerts] Process-Level Schema Registry
static AutoRegisterForUE / AutoRegisterForPEM
 -> DefineClass<T>().Register()
 -> RegisterJSClass(ClassDef)
 -> GetJSClassRegister() singleton
 -> each FJsEnv consumes shared schema

[Angelscript] Engine-Level Bind Replay
generated StartupModule / Bind_*.cpp
 -> FAngelscriptBinds::RegisterBinds(lambda)
 -> FAngelscriptBinds::CallBinds()
 -> asIScriptEngine::RegisterObjectType/Function
 -> current engine instance owns live schema
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/Ext/UEExtension.cpp:97-153`、`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:62-72`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/Ext/UEExtension.cpp
// 位置: 97-153，运行时基础类型在模块装载时静态自注册
// ============================================================================
struct AutoRegisterForUE
{
    AutoRegisterForUE()
    {
        PUERTS_NAMESPACE::DefineClass<UObject>()
            .Method("GetName", SelectFunction(FString(UObjectBaseUtility::*)() const, &UObjectBaseUtility::GetName))
            .Method("GetOuter", MakeFunction(&UObject::GetOuter))
            .Method("GetClass", MakeFunction(&UObject::GetClass))
            .Register();                    // ★ 不是等 FJsEnv 创建时再注册

        PUERTS_NAMESPACE::DefineClass<UStruct>()
            .Method("IsChildOf", SelectFunction(bool (UStruct::*)(const UStruct*) const, &UStruct::IsChildOf))
            .Register();
    }
};

AutoRegisterForUE _AutoRegisterForUE__;

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp
// 位置: 62-72，editor 模块自己也走同一套静态注册 DSL
// ============================================================================
struct AutoRegisterForPEM
{
    AutoRegisterForPEM()
    {
        PUERTS_NAMESPACE::DefineClass<FPuertsEditorModule>()
            .Function("SetCmdCallback", MakeFunction(&FPuertsEditorModule::SetCmdCallback))
            .Register();                    // ★ editor tooling 同样进入全局 class registry
    }
};

AutoRegisterForPEM _AutoRegisterForPEM__;
```

[2] `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/V8Backend.hpp:228-243`、`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JSClassRegister.cpp:281-304`、`Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSClassRegister.h:137-176`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Public/V8Backend.hpp
// 位置: 228-243，Register() 最终只做“写入注册表”
// ============================================================================
s_variableInfos_ = std::move(Cdb.variableInfos_);
s_variableInfos_.push_back(NamedPropertyInfo{nullptr, nullptr});
ClassDef.VariableInfos = s_variableInfos_.data();

RegisterJSClass(ClassDef);                 // ★ 写入共享 registry，而不是直接写某个具体 FJsEnv

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JSClassRegister.cpp
// 位置: 281-304，registry 是进程级静态单例
// ============================================================================
void JSClassRegister::ForeachRegisterClass(std::function<void(const JSClassDefinition* ClassDefinition)> Callback)
{
    for (auto& KV : CDataNameToClassDefinition)
    {
        Callback(KV.second);
    }
}

JSClassRegister* GetJSClassRegister()
{
    static JSClassRegister S_JSClassRegister; // ★ 进程级单例
    return &S_JSClassRegister;
}

void RegisterJSClass(const JSClassDefinition& ClassDefinition)
{
    GetJSClassRegister()->RegisterClass(ClassDefinition);
}

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSClassRegister.h
// 位置: 137-176，addon 也复用同一套全局注册入口
// ============================================================================
void JSENV_API RegisterJSClass(const JSClassDefinition& ClassDefinition);
void JSENV_API ForeachRegisterClass(std::function<void(const JSClassDefinition* ClassDefinition)> Callback);

#define PUERTS_MODULE(Name, RegFunc)                           \
    static struct FAutoRegisterFor##Name                       \
    {                                                          \
        FAutoRegisterFor##Name()                               \
        {                                                      \
            PUERTS_NAMESPACE::RegisterAddon(#Name, (RegFunc)); \
        }                                                      \
    } _AutoRegisterFor##Name                                   // ★ native addon 名字也在静态初始化期入表
```

[3] `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1322-1328`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:151-203,264-265,588-607`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 位置: 1322-1328，editor 生成的 bind module 只是“登记一个 lambda”
// ============================================================================
ModuleCPP.Add(FString("void ") + ModuleClass + "::StartupModule()\n{\n");
ModuleCPP.Add("\tFAngelscriptBinds::RegisterBinds\n\t(");
ModuleCPP.Add("\t\t(int32)FAngelscriptBinds::EOrder::Late,");
ModuleCPP.Add("\t\t[]()");
ModuleCPP.Add("\t\t{");

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp
// 位置: 151-203, 264-265, 588-607，登记与真正回放分离
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
        Bind.Function();                   // ★ 当前引擎实例上逐个执行 bind lambda
    }
}

int TypeId = Manager.Engine->RegisterObjectType(ClassName.ToCString(), Size, Flags); // ★ 真正落到 asIScriptEngine
int FunctionId = Manager.Engine->RegisterGlobalFunction(Signature.ToCString(), Function, CallConv, *(asFunctionCaller*)&Caller);
```

设计取舍：

- `puerts` 的收益是 schema 注册只做一次，后续 `FJsEnv` 重建不会重复生成 class/addon 描述；editor/runtime/tooling 也天然共享同一套注册 DSL。
- 代价是注册表先天更偏进程级公共面，当前检视路径未见 `Angelscript` 那种 `CallBinds(DisabledBindNames)` 级别的 per-env bind 过滤。
- `Angelscript` 的收益是绑定回放发生在当前 `asIScriptEngine` 上，engine 实例可以带自己的禁用表、观测逻辑和初始化顺序。
- 代价是 schema 回放成本与 engine 生命周期绑定，不能像 `puerts` 那样把描述层完全沉到进程级静态注册表。

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| registry 生命周期 | `GetJSClassRegister()` 进程级静态单例 | `RegisterBinds()` + `CallBinds()` 在当前 engine 上回放 | 实现方式不同 |
| editor 工具注册方式 | `AutoRegisterForPEM` 让 editor 模块本身也进入 JS registry | editor 工具主要生成/调用原生 C++ bind module | 实现方式不同 |
| engine 级 bind 裁剪 | 当前检视路径未见等价 `DisabledBindNames` 过滤 | `CallBinds(const TSet<FName>&)` 明确支持 | puerts 当前路径没有实现等价能力 |

### [维度 D6] 代码生成与 IDE 支持：`puerts` 的 TypeScript 分析器是“写权限工具链”，`Angelscript` 的 BlueprintImpact 是“读权限分析器”

前面已经写过 `ue.d.ts`、`LanguageService` 和 Blueprint metadata；这一轮补的是**谁拥有 Blueprint schema 的写权限**。`puerts` 的 `onBlueprintTypeAddOrChange(...)` 会直接遍历 TS AST，把方法、参数、属性、metadata 编译成 `PEBlueprintAsset` 的函数和变量；然后立即执行 `RemoveNotExistedComponent()`、`RemoveNotExistedMemberVariable()`、`RemoveNotExistedFunction()`，最后 `Save()` 时立刻 `CompileBlueprint()` 并把需要 runtime 重定向的函数写进 `UTypeScriptGeneratedClass::FunctionToRedirect`。这说明在当前路径里，TS authoring surface 不是“提示来源”，而是 Blueprint 资产结构的权威真源。

`Angelscript` 的 editor tooling 则明显更克制。`BuildImpactSymbols()` 只是把改动脚本模块归一化成 `Classes/Structs/Enums/Delegates` 集合，`AnalyzeLoadedBlueprint()` 负责判断某个 Blueprint 是否依赖这些符号，`ScanBlueprintAssets()` 产出的是匹配资产列表。后续 `ClassReloadHelper` 最多替换 pin type、重建受影响节点并 `QueueForCompilation(BP)`；当前检视路径没有看到等价于 `puerts` 那种“从脚本 AST 直接删改 Blueprint 成员/组件/函数定义”的 authoring compiler。

```
[puerts] Authoritative Asset Compiler
TypeScript AST
 -> onBlueprintTypeAddOrChange()
 -> AddFunction / AddMemberVariable / metadata
 -> RemoveNotExisted*
 -> Save() + CompileBlueprint()
 -> UTypeScriptGeneratedClass.FunctionToRedirect

[Angelscript] Impact Analyzer
changed .as modules
 -> BuildImpactSymbols()
 -> AnalyzeLoadedBlueprint()
 -> matches / replacement objects
 -> ReconstructNode() + QueueForCompilation()
 -> Blueprint keeps UE-owned schema
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Content/JavaScript/PuertsEditor/CodeAnalyze.js:817-965`、`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:1250-1290,1345-1395`、`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/TypeScriptCompilerContext.cpp:19-40`

```javascript
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Content/JavaScript/PuertsEditor/CodeAnalyze.js
// 函数: onBlueprintTypeAddOrChange
// 位置: 817-965，TS AST 直接驱动 Blueprint schema 写回
// ============================================================================
function onBlueprintTypeAddOrChange(baseTypeUClass, type, modulePath) {
    let bp = new UE.PEBlueprintAsset();
    bp.LoadOrCreateWithMetaData(type.getSymbol().getName(), modulePath, baseTypeUClass, 0, 0, uemeta.compileClassMetaData(type));

    // ★ 先把 TS 方法和属性编译成 Blueprint 函数/变量
    bp.AddFunctionWithMetaData(symbol.getName(), true, undefined, undefined, flags, clearFlags, uemeta.compileFunctionMetaData(symbol));
    bp.AddMemberVariableWithMetaData(symbol.getName(), propPinType.pinType, propPinType.pinValueType,
        Number(localFlags & 0xffffffffn), Number(localFlags >> 32n), cond, propertyMetaData);

    // ★ 再把 TS 中已经消失的成员从 Blueprint 里删掉
    bp.RemoveNotExistedComponent();
    bp.RemoveNotExistedMemberVariable();
    bp.RemoveNotExistedFunction();
    bp.HasConstructor = hasConstructor;
    bp.Save();                              // ★ 同一轮里直接持久化并触发编译
}

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp
// 函数: UPEBlueprintAsset::RemoveNotExistedMemberVariable / RemoveNotExistedFunction / Save
// 位置: 1250-1290, 1345-1395，缺失成员直接删，保存时立刻重编译
// ============================================================================
if (!ComponentsAdded.Contains(Blueprint->SimpleConstructionScript->GetAllNodes()[i]->GetVariableName()))
{
    NeedSave = true;
    RemoveComponent(Name);                  // ★ 组件消失就删 Blueprint 节点
}

if (!MemberVariableAdded.Contains(Blueprint->NewVariables[i].VarName))
{
    NeedSave = true;
    FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, Name); // ★ 成员变量也直接删
}

if (NeedSave)
{
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);          // ★ 保存时立刻编译
    if (FunctionAdded.Contains(*FunctionName))
    {
        TypeScriptGeneratedClass->FunctionToRedirect.Add(FunctionFName); // ★ runtime redirect 名单同步更新
    }
    FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, false, false);
}

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/TypeScriptCompilerContext.cpp
// 函数: FTypeScriptCompilerContext::SpawnNewClass
// 位置: 19-40，Blueprint 编译出口固定落到 UTypeScriptGeneratedClass
// ============================================================================
NewClass = FindObject<UTypeScriptGeneratedClass>(Blueprint->GetOutermost(), *NewClassName);
if (NewClass == NULL)
{
    NewClass = NewObject<UTypeScriptGeneratedClass>(
        Blueprint->GetOutermost(), FName(*NewClassName), RF_Public | RF_Transactional);
}
else
{
    NewClass->ClassGeneratedBy = Blueprint;
    FBlueprintCompileReinstancer::Create(NewClass);
}
```

[2] `Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp:112-180,278-309`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:108-145,291-295`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp
// 函数: BuildImpactSymbols / AnalyzeLoadedBlueprint / ScanBlueprintAssets
// 位置: 112-180, 278-309，把脚本改动归一化为“受影响符号集合 + 匹配资产”
// ============================================================================
FBlueprintImpactSymbols BuildImpactSymbols(const TArray<TSharedRef<FAngelscriptModuleDesc>>& Modules)
{
    FBlueprintImpactSymbols Symbols;
    for (const TSharedRef<FAngelscriptModuleDesc>& Module : Modules)
    {
        if (ClassDesc->Class != nullptr)
            Symbols.Classes.Add(ClassDesc->Class);
        if (UScriptStruct* ScriptStruct = Cast<UScriptStruct>(ClassDesc->Struct))
            Symbols.Structs.Add(ScriptStruct);
        if (EnumDesc->Enum != nullptr)
            Symbols.Enums.Add(EnumDesc->Enum);
        if (DelegateDesc->Function != nullptr)
            Symbols.Delegates.Add(DelegateDesc->Function);
    }
    return Symbols;
}

if (AnalyzeLoadedBlueprint(*Blueprint, Result.Symbols, Match.Reasons))
{
    Result.Matches.Add(Match);              // ★ 只记录“哪个资产受影响”，不直接改 Blueprint schema
}

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp
// 位置: 108-145, 291-295，后处理是 pin 修复和排队编译，不是脚本 AST 写回
// ============================================================================
const bool bHasDependency = AngelscriptEditor::BlueprintImpact::AnalyzeLoadedBlueprint(*BP, ImpactSymbols, ImpactReasons);
for (UK2Node* Node : AllNodes)
{
    for (auto* Pin : Node->Pins)
    {
        ReplacePinType(Pin->PinType);       // ★ 修 pin type
    }
}

for (UBlueprint* BP : DependencyBPs)
{
    RefreshRelevantNodesInBP(BP);
    FBlueprintCompilationManager::QueueForCompilation(BP); // ★ 只排队重编译
}
```

设计取舍：

- `puerts` 的收益是 TS 类声明、Blueprint 资产、`UTypeScriptGeneratedClass` 三者共享一份 schema 真源，authoring 改动可以直接压成 UE 资产。
- 代价是工具链拥有真正的“删改资产结构”权限，TypeScript compiler 已经不是只读 IDE 服务，而是带持久化副作用的 authoring compiler。
- `Angelscript` 的收益是 Blueprint 仍由 UE 原生资产系统拥有，脚本工具链主要做影响分析、替换对象映射和重新编译调度。
- 代价是当前检视路径里没有 `puerts` 等价的 asset-schema authoring compiler，脚本侧不能直接当 Blueprint 成员表的唯一真源。

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| schema 真源 | `CodeAnalyze.js` 的 TS AST 直接回写 Blueprint 结构 | `BuildImpactSymbols()` + `AnalyzeLoadedBlueprint()` 只判断影响范围 | 实现方式不同 |
| 缺失成员处理 | `RemoveNotExisted*()` 直接删除组件/变量/函数 | 当前路径只见 pin 修复、节点重建、排队编译 | Angelscript 当前没有实现等价 asset-schema compiler |
| 持久化时机 | 同一轮 `Save()` 内就 `CompileBlueprint()` 并保存 package | `QueueForCompilation(BP)` 走后续编译流程 | 实现方式不同 |

### [维度 D11] 部署与打包：`puerts` 的 native 扩展平面是运行时可加载 ABI，`Angelscript` 的 native 扩展平面是编译期 bind module

前面的 D11 已经把 staging、`.mbc/.cbc` 和 `PrecompiledScript.Cache` 写过很多轮；这一轮只补**native 扩展到底在哪一层生效**。`puerts` 的 `pesapi.h` 把 ABI 版本写死在 `PESAPI_VERSION`，并要求 addon 导出 `PESAPI_MODULE_INITIALIZER(dynamic)` 与 `PESAPI_MODULE_VERSION()`。加载端 `PesapiAddonLoad.cpp` 会先找 `dynamic` 入口，找不到才回退检查版本并报 `version mismatch`。再往上，`JsEnvImpl` 把 `loadCPPType` 暴露给脚本，并在引导阶段执行 `puerts/pesaddon.js`；该 JS 包装器又把 `puerts.load(filepath)` 转成“按平台补 `lib` 前缀/动态库后缀、按 `module.Class` 懒解析 C++ 类型”的 runtime API。也就是说，`puerts` 的 native 扩展平面不仅是部署产物，还是 script-visible ABI。

`Angelscript` 当前路径里的 native 扩展平面则仍然是 build-time bind module。新增 native surface 的常规入口，是 editor 生成或手写 C++ module，在 `StartupModule()` 里 `FAngelscriptBinds::RegisterBinds(...)`，随后由当前 `asIScriptEngine` 的 `CallBinds()` 回放到 `RegisterObjectType/RegisterGlobalFunction`。脚本层看到的是已经存在的 API，不会自己 `load` 一个宿主 DLL 再按 `module.Class` 懒解析类型。这里不该判成“没有 native 扩展”，而该判成“扩展时机和 ABI 边界不同”。

```
[puerts] Runtime Native Extension Plane
addon dll / dylib / so
 -> export PESAPI_MODULE_INITIALIZER(dynamic)
 -> pesapi_load_addon(path, module)
 -> JsEnv binds loadCPPType + executes pesaddon.js
 -> JS calls puerts.load("mylib").SomeClass
 -> lazy module.Class resolution

[Angelscript] Build-Time Native Bind Plane
generated module / handwritten bind cpp
 -> StartupModule() registers lambda
 -> FAngelscriptBinds::CallBinds()
 -> RegisterObjectType / RegisterGlobalFunction
 -> script only sees pre-registered API
```

关键源码引用：

[1] `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/pesapi.h:57-104`、`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PesapiAddonLoad.cpp:99-183`、`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:494-499,628-635`、`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/pesaddon.js:24-49`、`Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSClassRegister.h:169-176`、`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FFIBinding.cpp:596-601`

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Public/pesapi.h
// 位置: 57-104，addon ABI 的导出契约
// ============================================================================
#define PESAPI_MODULE_INITIALIZER(modname) PESAPI_MODULE_INITIALIZER_X(PESAPI_MODULE_INITIALIZER_BASE, modname, PESAPI_VERSION)
#define PESAPI_MODULE_VERSION() PESAPI_MODULE_INITIALIZER_X(PESAPI_MODULE_INITIALIZER_BASE, version, 0)

#define PESAPI_MODULE(modname, initfunc)                                                                   \
    PESAPI_MODULE_EXPORT void PESAPI_MODULE_INITIALIZER(modname)(pesapi_func_ptr * func_ptr_array);        \
    PESAPI_MODULE_EXPORT const char* PESAPI_MODULE_INITIALIZER(dynamic)(pesapi_func_ptr * func_ptr_array); \
    PESAPI_MODULE_EXPORT int PESAPI_MODULE_VERSION()();                                                    \
    /* ... */                                                                                              \
    PESAPI_MODULE_EXPORT int PESAPI_MODULE_VERSION()()                                                     \
    {                                                                                                      \
        return PESAPI_VERSION;                                                                             \
    }                                                                                                      // ★ addon 必须自报 ABI 版本

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PesapiAddonLoad.cpp
// 位置: 99-183，运行时真正执行 ABI 校验并装载 addon
// ============================================================================
auto Init = (const char* (*) (pesapi_func_ptr*) )(uintptr_t) FPlatformProcess::GetDllExport(DllHandle, *EntryName);
if (Init)
{
    const char* ModuleName = Init(nullptr);
    GPesapiModuleName = ModuleName;
    Init(funcs);                             // ★ 把整张 pesapi_func_ptr 表注入 addon
}
else
{
    auto Ver = (int (*)())(uintptr_t) FPlatformProcess::GetDllExport(DllHandle, *VersionEntryName);
    int PesapiVersion = Ver();
    UE_LOG(LogTemp, Error, TEXT("pesapi version mismatch, expect: %d, but got: %d"), PESAPI_VERSION, PesapiVersion);
}

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp
// 位置: 494-499, 628-635，脚本侧正式暴露 loadCPPType / pesaddon 包装器
// ============================================================================
MethodBindingHelper<&FJsEnvImpl::LoadCppType>::Bind(Isolate, Context, PuertsObj, "loadCPPType", This);
ExecuteModule("puerts/pesaddon.js");         // ★ 把 JS 侧 loader 协议挂进启动主链

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/pesaddon.js
// 位置: 24-49，JS 侧直接消费 native addon ABI
// ============================================================================
const org_load = global.puerts.load;
function load(filepath) {
    if (filepath && typeof filepath === 'string' && filename.indexOf('.') === -1) {
        const prefix = iswin ? '' : 'lib';
        filepath = `${pathDirname(filepath)}${iswin? '\\' : '/'}${prefix}${filename}${dll_ext}`;
    }
    const module_name = org_load(filepath);
    moduleCache[filepath] = new Proxy({__name : module_name}, {
        get: function(classCache, className) {
            classCache[className] = puerts.loadCPPType(`${module_name}.${className}`); // ★ 类型名空间也进入脚本 ABI
        }
    });
}
global.puerts.load = load;

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSClassRegister.h
// 位置: 169-176，native module 名字同样在静态初始化期入表
// ============================================================================
#define PUERTS_MODULE(Name, RegFunc)                           \
    static struct FAutoRegisterFor##Name                       \
    {                                                          \
        FAutoRegisterFor##Name()                               \
        {                                                      \
            PUERTS_NAMESPACE::RegisterAddon(#Name, (RegFunc)); \
        }                                                      \
    } _AutoRegisterFor##Name

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FFIBinding.cpp
// 位置: 596-601，ffi 也作为运行时可 require 的 native module 发布
// ============================================================================
PUERTS_MODULE(ffi_bindings, Init);            // ★ 不是编译后静态写死到脚本表面，而是 runtime module
```

[2] `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1322-1328`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:151-203,588-607`

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 位置: 1322-1328，新增 native surface 的常规路径是生成 bind module
// ============================================================================
ModuleCPP.Add(FString("void ") + ModuleClass + "::StartupModule()\n{\n");
ModuleCPP.Add("\tFAngelscriptBinds::RegisterBinds\n\t(");
ModuleCPP.Add("\t\t(int32)FAngelscriptBinds::EOrder::Late,");
ModuleCPP.Add("\t\t[]()");
ModuleCPP.Add("\t\t{");

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp
// 位置: 151-203, 588-607，运行时只是把已编译进来的 bind 回放到当前 engine
// ============================================================================
void FAngelscriptBinds::RegisterBinds(FName BindName, int32 BindOrder, TFunction<void()> Function)
{
    GetBindArray().Add({BindName.IsNone() ? MakeUnnamedBindName() : BindName, BindOrder, MoveTemp(Function)});
}

void FAngelscriptBinds::CallBinds(const TSet<FName>& DisabledBindNames)
{
    for (const FBindFunction& Bind : GetSortedBindArray())
    {
        Bind.Function();                    // ★ 当前进程只能消费“已经编译进来的” native bind
    }
}

int FunctionId = Manager.Engine->RegisterGlobalFunction(Signature.ToCString(), Function, asCALL_CDECL, *(asFunctionCaller*)&Caller, nullptr);
int FunctionId = Manager.Engine->RegisterGlobalFunction(Signature.ToCString(), Function, CallConv, *(asFunctionCaller*)&Caller);
```

设计取舍：

- `puerts` 的收益是 native 扩展 ABI 对脚本层透明可见，addon 文件名、module 名、类型命名空间都能在运行时被脚本消费。
- 代价是部署契约更重，除了 VM/backend 本体，还要保证 addon 导出入口、`PESAPI_VERSION` 和脚本 loader 协议一起保持兼容。
- `Angelscript` 的收益是 native 扩展边界更收敛，脚本层不直接碰宿主 DLL loader，运行时表面更稳定。
- 代价是新增 native API 基本要回到 C++ 生成/编译链，当前检视路径未见等价 `puerts.load(...)` 的 runtime addon plane。

与 Angelscript 对比：

| 对比点 | puerts | Angelscript | 差距判断 |
| --- | --- | --- | --- |
| native 扩展生效时机 | `pesapi_load_addon()` 运行时装载 + JS `puerts.load()` 消费 | `StartupModule()` 生成/登记 bind，`CallBinds()` 启动期回放 | 实现方式不同 |
| 脚本层是否可见 loader 协议 | `loadCPPType()`、`pesaddon.js`、`module.Class` 直接进入脚本 ABI | 当前路径脚本层只看到已注册 API | Angelscript 当前没有实现等价 runtime addon plane |
| 兼容性闸门 | `PESAPI_VERSION` + 导出入口检查 | 主要靠 C++ 编译/链接边界，当前路径未见等价 runtime ABI loader | 实现方式不同 |
