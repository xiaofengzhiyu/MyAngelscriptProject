# ExtensionPoints 架构审查

---

## 架构分析 (2026-04-08 14:48)

### Arch-EP1：`bind/type` 可扩展，但扩展入口仍是隐式 C++ 约定

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 自定义全局函数、自定义类型绑定、`ini/settings` 驱动的 bind 开关 |
| 当前设计 | 基于 `AngelscriptRuntime.Build.cs` 的 `PublicIncludePaths` 暴露和 `FAngelscriptBinds` / `FAngelscriptType` 的 public static API，可以推断外部宿主 `C++ module` 无需修改插件源码就能追加 `global function` 与 `type binding`；但这条扩展面仍停留在“自己 include 内核头并在模块启动或静态初始化里注册”的隐式约定。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs:15`、`:17` 把 `ModuleDirectory` 和 `Core` 暴露到 `PublicIncludePaths`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:438`、`:471`-`:475` 公开 `FAngelscriptBinds::FBind` 与 `RegisterBinds()` / `GetBindInfoList()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:606`-`:611` 公开 `BindGlobalFunction()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:71`-`:81` 公开 `Register()` / `RegisterTypeFinder()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h:58`-`:63` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1928`-`:1941` 用 `DisabledBindNames` 合并 settings/runtime config 来关闭 named binds。 |
| 优点 | 外部项目理论上已经能在不改 `Plugins/Angelscript/` 源码的前提下增加 `global function`、自定义 `type finder`、并用 `DisabledBindNames` 做灰度关闭；`GetBindInfoList()` 也提供了最小可观测性。 |
| 不足 | 这条 seam 没有显式 `IAngelscriptExtensionModule`、没有外部样例、没有模块级来源标识。用户必须知道要依赖 `Core/AngelscriptBinds.h`、自己保证 `BindName` 稳定、自己承担静态注册时序；结果是“能扩展”成立，但“可发现、可维护、可支持”并没有产品化。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | 把“哪些内容允许导出绑定”变成显式 `settings + build define + generator delegates` 控制面。`bEnableExport`、`ExportModule`、`ClassBlacklist` 直接出现在 settings 中，生成流程再通过 `OnBeginGenerator` / `OnEndGenerator` 广播给其它模块。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpEditorSetting.h:136`-`:148`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/UnrealCSharpCore.build.cs:128`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Delegate/FUnrealCSharpCoreModuleDelegates.h:7`-`:35`；`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:237`-`:309` | 把“扩展范围”和“扩展时机”做成显式 contract，而不是要求用户去读 binder 源码推理。 |
| puerts | 用 `IPuertsModule` 暴露 `InitExtensionMethodsMap()` / `SetJsEnvSelector()`，并通过 `UExtensionMethods` 约定类扫描 `static UFunction` 的首参类型，自动把外部方法挂到目标 `Struct/Object` 上。 | `Reference/puerts/unreal/Puerts/Source/Puerts/Public/PuertsModule.h:17`-`:55`；`Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:97`-`:121`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Public/ExtensionMethods.h:18`-`:21`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:931`-`:983`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:340`-`:356` | 把“用户新增方法”的接入协议做成显式框架，用户不需要理解内部 translator 细节。 |
| UnLua | 把对象到脚本模块的映射做成 `IUnLuaInterface::GetModuleName()`，把加载器替换做成 `FUnLuaDelegates::CustomLoadLuaFile`，宿主项目可以直接在自己的 `GameInstance` 或 `BlueprintFunctionLibrary` 里介入。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaInterface.h:23`-`:39`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDelegates.h:22`-`:49`；`Reference/UnLua/Source/TPSProject/TPSGameInstance.h:22`-`:32`；`Reference/UnLua/Source/TPSProject/TutorialBlueprintFunctionLibrary.cpp:91`-`:104` | 把扩展能力收敛成宿主项目可直接实现的协议，而不是暴露一组底层静态函数后让用户自行拼装。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留现有 `FAngelscriptBinds` / `FAngelscriptType` 能力的前提下，新增一层显式 `extension registry`，把“隐式可用”提升成“正式支持”。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/` 新增 `IAngelscriptExtensionModule` 和 `FAngelscriptExtensionRegistry`，公开 `RegisterGlobalBinds()`、`RegisterTypeBindings()`、`RegisterCompileObservers()`。 2. 在 `FAngelscriptRuntimeModule::StartupModule()` 或 `FAngelscriptEngine::Initialize()` 增加扩展发现阶段，统一执行外部 contributor，再进入 `BindScriptTypes()`。 3. 扩充 `FBindInfo` 或额外 manifest，记录 `SourceModule`、`ExtensionKind`、`BindName`，让 `StateDump` 能区分内建 bind 与外部 bind。 4. 新增一个最小示例模块，演示“外部插件增加 `BindGlobalFunction()` + `RegisterTypeFinder()` 且可被 `DisabledBindNames` 管理”的路径。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptExtensionRegistry.h/.cpp` |
| 预估工作量 | M |
| 架构风险 | 当前许多 bind 依赖静态注册顺序；若 registry 初始化时机处理不当，可能改变既有 bind 顺序和 `UnnamedBind_*` 分配结果。 |
| 兼容性 | 向后兼容。保留现有 `FAngelscriptBinds::FBind` / `BindGlobalFunction()` / `RegisterTypeFinder()`，新 registry 只提供显式入口、来源标识和样例。 |
| 验证方式 | 1. 新建宿主测试模块，验证不改插件源码即可增加 `global function` 与 `custom type`。 2. 复跑 `AngelscriptBindConfigTests`，确认 `DisabledBindNames` 仍能同时过滤内建 bind 与外部 bind。 3. 用 `StateDump` 检查 manifest 中能看到新增扩展来源。 |

### Arch-EP2：编译与热重载事件已公开，但导入解析和编译策略缺少早期扩展接口

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 修改编译行为、定制 `import` 解析、订阅编译/热重载事件、`virtual` 子类化扩展点 |
| 当前设计 | 运行时已经公开 `PreCompile / PostCompile / PreGenerateClasses / reload delegates`，也公开了 `preprocessor` 的 `OnProcessChunks / OnPostProcessCode`；但 `ProcessImports()` 发生在这两个 hook 之前，唯一面向编译策略的 `virtual` 扩展点 `FAngelscriptAdditionalCompileChecks` 又只是挂在 `FAngelscriptEngine::AdditionalCompileChecks` 这张 map 上，没有显式注册接口。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h:15`-`:21`、`:37`-`:47` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp:72`-`:118` 定义 compile/literal asset delegates；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3066`-`:3068`、`:3898`-`:3899`、`:4138`-`:4140` 在编译各阶段广播事件；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:232`-`:239` 先执行 `ProcessImports()`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:265`-`:287` 才广播 `OnProcessChunks` / `OnPostProcessCode`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h:29`-`:30`、`:180` 表明 hook 和 `Files` 都是公开状态，但导入已在此前完成；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h:12`-`:38` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2347`-`:2395`、`:2469` 暴露 reload 事件；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptAdditionalCompileChecks.h:4`-`:8`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:359`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:1374`-`:1383`、`:2474`-`:2487` 表明附加编译检查只能通过 `Engine` 内部 map 注入；`Plugins/Angelscript/Source/AngelscriptRuntime/BaseClasses/ScriptEngineSubsystem.h:21`-`:99`、`Plugins/Angelscript/Source/AngelscriptRuntime/BaseClasses/ScriptGameInstanceSubsystem.h:15`-`:86`、`Plugins/Angelscript/Source/AngelscriptRuntime/BaseClasses/ScriptWorldSubsystem.h:28`-`:156` 说明 `virtual` 扩展点主要集中在 gameplay/subsystem 层，而不是 compiler/import 层。 |
| 优点 | 外部宿主模块已经可以订阅编译开始、编译结束、类生成前、类/结构体/委托 reload 等事件；`FAngelscriptAdditionalCompileChecks` 至少给了按 `CodeSuperClass` 插入校验逻辑的后门。 |
| 不足 | `custom import resolver` 当前没有一等接口，`OnProcessChunks` 为时已晚；`bAutomaticImports` 与 `PreprocessorFlags` 只是配置值，不是策略对象；`AdditionalCompileChecks` 需要直接改 `Engine` 实例状态，扩展生命周期和多引擎场景都不清晰。结果是“订阅事件”不难，“真正改变编译语义”仍然主要要改插件源码。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `CustomLoadLuaFile` 是真正位于加载路径中的 hook，`LuaEnv` 在默认 loader 失败后直接调用它决定脚本内容；`IUnLuaInterface::GetModuleName()` 进一步把脚本模块选路下放到宿主对象。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDelegates.h:33`-`:49`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:557`-`:580`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaInterface.h:29`-`:39` | 把“模块解析/文件加载”做成一等扩展接口，而不是只在后处理阶段给字符串 hook。 |
| puerts | 用 `IJSModuleLoader` 抽象模块解析与加载，并让 `FJsEnv` 构造函数直接接收自定义 loader；默认 loader 里的 `Search()` / `Load()` / `CheckExists()` 也都是 `virtual`，可继承覆盖。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSModuleLoader.h:17`-`:50`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JsEnv.h:64`-`:68`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnv.cpp:19`-`:25`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DefaultJSModuleLoader.cpp:53`-`:143` | 把 import/module resolution 变成 strategy object，而不是布尔开关。 |
| UnrealCSharp | 生成与运行时绑定被拆成明确阶段：`OnBeginGenerator` / `OnEndGenerator` / `OnCompile` / `OnCSharpEnvironmentInitialize` 都是显式广播点，方便其它模块在正确阶段接入。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Delegate/FUnrealCSharpCoreModuleDelegates.h:10`-`:35`；`Reference/UnrealCSharp/Source/UnrealCSharp/Public/Delegate/FUnrealCSharpModuleDelegates.h:3`-`:16`；`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp:133`；`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FCSharpBind.cpp:24`-`:30` | 不是所有可扩展点都要做成 `virtual`；但阶段边界至少要足够稳定、足够早、足够命名化。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 新增“早期编译策略接口”，把导入解析、附加编译检查、编译阶段广播从分散的静态 hook 整理成可注册的 compiler extension surface。 |
| 具体步骤 | 1. 在 `Preprocessor/` 新增 `IAngelscriptImportResolver`，默认实现保持现有 `explicit import` + `automatic import` 逻辑，但把 `ProcessImports()` 提升成可替换策略。 2. 在 `FAngelscriptPreprocessor::Preprocess()` 中增加 `BeforeResolveImports` 阶段，并传入 `FFile` / `FImport` 视图，让外部模块能在导入排序前介入。 3. 给 `FAngelscriptEngine` 增加显式 `RegisterAdditionalCompileChecks(UClass*, TSharedRef<FAngelscriptAdditionalCompileChecks>)`，避免外部代码直接写 `AdditionalCompileChecks` map。 4. 把现有 `GetPreCompile()`、`GetPostCompile()`、`OnProcessChunks`、`OnPostReload` 保留，但新增统一 `FAngelscriptCompileContext` / `FAngelscriptReloadContext` 参数，减少扩展者对全局静态状态的依赖。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptAdditionalCompileChecks.h`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptImportResolver.h/.cpp` |
| 预估工作量 | M |
| 架构风险 | 改动导入解析阶段会直接影响模块拓扑和初始编译顺序；如果没有保留默认 resolver，可能引入隐蔽的 `module ordering` 回归。 |
| 兼容性 | 基本向后兼容。默认 resolver 复用当前逻辑，现有 `OnProcessChunks` / `OnPostProcessCode` / compile/reload delegates 继续保留；只是新增更早、更正式的 hook。 |
| 验证方式 | 1. 增加一个自定义 resolver 测试，验证外部模块能改变 `import` 解析而无需改插件源码。 2. 复跑预处理与热重载相关测试，确认 `OnProcessChunks` 仍按旧阶段触发。 3. 在多引擎测试中验证 `AdditionalCompileChecks` 注册不会串到其它 `Engine` 实例。 |

### 场景判断

| 目标 | 当前结论 | 纯 `.as` / `ini` 是否可做 | 宿主 `C++ module` 是否可做 | 依据 |
|------|----------|---------------------------|-----------------------------|------|
| 添加自定义全局函数 | 不需要改插件源码，但需要新增宿主 `C++ module`；当前不是脚本级扩展面 | 否 | 是 | `AngelscriptRuntime.Build.cs:15`-`:18` + `AngelscriptBinds.h:438`-`:475`、`:606`-`:611` |
| 修改编译行为（如自定义 `import` 解析） | 现状通常需要改插件源码；后处理 hook 太晚，缺少早期 resolver | 否 | 部分可做，但 `import resolver` 不行 | `AngelscriptPreprocessor.cpp:232`-`:239` 先于 `OnProcessChunks`；`AngelscriptPreprocessor.cpp:265`-`:287` |
| 添加自定义类型绑定 | 不需要改插件源码，但需要新增宿主 `C++ module`；当前是隐式类型数据库接口 | 否 | 是 | `AngelscriptType.h:71`-`:81`；`AngelscriptType.cpp:54`-`:118` |
| 订阅编译/热重载事件 | 不需要改插件源码；新增宿主 `C++/Editor module` 即可绑定 delegate | 否 | 是 | `AngelscriptRuntimeModule.h:37`-`:47`、`AngelscriptRuntimeModule.cpp:72`-`:118`、`AngelscriptClassGenerator.h:12`-`:38` |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P0 | Arch-EP2 | 编译与导入解析缺少早期 strategy hook | 扩展点新增 | 高 |
| P1 | Arch-EP1 | `bind/type` 扩展 seam 仍是隐式 C++ 约定 | 扩展点显式化 | 高 |

---

## 架构分析 (2026-04-08 15:04)

### Arch-EP3：`EngineDependencies` 已可注入，但正式运行时 owner 仍被固定在 `UAngelscriptGameInstanceSubsystem`

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 宿主项目能否在不改插件源码的前提下替换 runtime owner、脚本根发现策略和多实例选择策略 |
| 当前设计 | `FAngelscriptEngineDependencies` 已经把 project dir、path normalize、plugin script root 枚举抽象成函数指针；但正式运行时仍由 concrete 的 `UAngelscriptGameInstanceSubsystem` 直接持有 `OwnedEngine` 并调用默认 `Initialize()`，宿主没有公开的 factory / host interface 可以接管这一层。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.h:11`-`:13` 把 owner 固定为 concrete `UAngelscriptGameInstanceSubsystem`，并内嵌 `FAngelscriptEngine OwnedEngine`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp:16`-`:23` 在没有 ambient engine 时直接 `OwnedEngine.Initialize()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp:107`-`:113` 用 `GetSubsystem<UAngelscriptGameInstanceSubsystem>()` 硬编码取回当前 owner；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:86`-`:95` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:539`-`:567` 定义并实现 `FAngelscriptEngineDependencies::CreateDefault()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDependencyInjectionTests.cpp:52`-`:85` 证明这套 DI seam 目前主要通过 `CreateForTesting()` 在测试中注入。 |
| 优点 | 当前 owner 模型非常直接，生命周期简单；也延续了已有对比文档里“宿主边界保持薄壳”的方向，不会像某些参考插件那样把宿主工程强绑定进主流程。 |
| 不足 | 想替换 script-root discovery、plugin root 筛选、按 `GameInstance/World` 选择不同 engine、或做项目级 runtime host 策略时，宿主只能绕开 subsystem 自己手写 `FAngelscriptEngine` 生命周期，或者直接改插件源码。换句话说，DI seam 存在，但没有产品化成正式扩展点。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 把宿主介入点前移到 settings 和接口层：`UUnLuaSettings` 允许配置 `EnvLocatorClass` / `ModuleLocatorClass` / `PreBindClasses`，`FUnLuaModule` 启动时按配置实例化 locator；宿主 `UTPSGameInstance` 只需实现 `IUnLuaInterface::GetModuleName()` 就能声明脚本入口。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaSettings.h:47`-`:56`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:77`-`:79`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaModule.cpp:103`-`:113`；`Reference/UnLua/Source/TPSProject/TPSGameInstance.h:22`-`:32` | 把“谁决定脚本环境/模块定位”交给宿主可替换对象，而不是把 owner 固定死在插件内部 subsystem。 |
| puerts | 直接把 module resolution 和 env 选路做成 public contract：`FJsEnv` 构造函数可接收自定义 `IJSModuleLoader`，`IPuertsModule` 再暴露 `SetJsEnvSelector()` 和 `InitExtensionMethodsMap()`。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSModuleLoader.h:17`-`:25`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JsEnv.h:64`-`:68`；`Reference/puerts/unreal/Puerts/Source/Puerts/Public/PuertsModule.h:37`-`:47`；`Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:97`-`:120` | 核心不是“再加一个 subsystem”，而是给宿主一个显式的 loader / selector contract，让生命周期 owner 可以保持在宿主侧。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 保持默认 `UAngelscriptGameInstanceSubsystem` 不变，但新增一个正式的 `runtime host/factory` seam，让正式运行时也能消费 `FAngelscriptEngineDependencies`。 |
| 具体步骤 | 1. 在 `Core/` 新增 `IAngelscriptRuntimeHost` 或 `UAngelscriptRuntimeHost`，公开 `BuildEngineConfig()`、`BuildEngineDependencies()`、`ResolvePrimaryEngine()`。 2. 在 `UAngelscriptSettings` 新增 `RuntimeHostClass` 或在 `FAngelscriptRuntimeModule` 新增 `SetRuntimeHostFactory()`，让宿主项目能注册自己的 host。 3. 修改 `UAngelscriptGameInstanceSubsystem::Initialize()`：先解析 host，再决定是沿用 ambient engine、创建默认 `OwnedEngine`，还是按 host 返回的 dependencies 创建自定义 engine。 4. 把现有 `AngelscriptDependencyInjectionTests` 复制一组“正式 subsystem 路径”的自动化测试，验证自定义 script roots 能在非 test-only 路径生效。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeHost.h/.cpp` |
| 预估工作量 | M |
| 架构风险 | owner 改成可替换后，要重新审视 `FAngelscriptEngineContextStack`、ambient world context 和多引擎 clone/full 模式的约束；如果 host 在错误时机创建 engine，可能破坏现有 tick owner 计数和初始化顺序。 |
| 兼容性 | 向后兼容。`RuntimeHostClass` 为空时继续走当前 `OwnedEngine.Initialize()` 路径；现有 `GetCurrent()` 和 `GetSubsystem<UAngelscriptGameInstanceSubsystem>()` 也可以先保持不变。 |
| 验证方式 | 1. 新增一个宿主测试模块，自定义 `RuntimeHostClass` 返回注入版 `FAngelscriptEngineDependencies`，验证 `DiscoverScriptRoots()` 结果与默认路径不同。 2. 复跑 `AngelscriptDependencyInjectionTests`、`AngelscriptMultiEngineTests` 和 subsystem tests，确认 clone/full engine 语义未回归。 3. 在 Editor 和非 Editor 各跑一次启动流程，确认默认 host 不改变现有编译、tick 与 shutdown 顺序。 |

### Arch-EP4：`UAngelscriptSettings` 提供的是“内建行为开关”，不是“扩展 manifest”

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `ini/settings` 是否足以声明和激活用户扩展，而不只是调节内建行为 |
| 当前设计 | 全量检查 `UAngelscriptSettings` 后，当前配置面主要分成五类：preprocessor/import (`PreprocessorFlags`、`bAutomaticImports`)；bind/name policy (`DisabledBindNames`、namespace strip 列表)；property/function 默认规则；warnings/errors；debugger blacklist。引擎启动时把这些字段直接读入运行时状态，Editor 侧只负责注册设置页。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h:46`-`:71` 定义 preprocess/import 相关配置；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h:73`-`:130` 定义默认 property/function 与命名空间策略；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h:137`-`:219` 定义执行限制、warning/error 与 debugger blacklist；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1291`-`:1296` 在初始化时把这些值直接灌入 engine 状态；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:385`-`:393` 仅把 `UAngelscriptSettings` 注册到 settings viewer。基于对 `AngelscriptSettings.h:41`-`:225` 的全文检查，可以推断当前没有 `TSubclassOf`、module list 或 contributor list 一类的扩展声明字段。 |
| 优点 | 配置语义清晰，所有开关都围绕内建编译器/绑定器行为展开；对于只想调 warning、命名空间、默认 specifier 的项目，这种设计足够简单，也避免了 settings 直接驱动复杂模块装配。 |
| 不足 | `ini` 目前只能调“插件已经支持的行为”，不能声明“项目想接入哪些额外扩展者”。这意味着外部 `global bind`、type contributor、compile observer 即使已经写好，也仍要靠宿主 `C++ module` 手写 startup glue 激活，settings 无法承担 rollout / manifest / validation 的角色。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | 把生成范围做成显式 config：`bEnableExport`、`ExportModule`、`ClassBlacklist` 直接决定导出/过滤哪些模块和类型；生成阶段再由 `OnBeginGenerator` / `OnEndGenerator` 统一广播。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpEditorSetting.h:136`-`:148`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Delegate/FUnrealCSharpCoreModuleDelegates.h:14`-`:35`；`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:237`-`:309` | 让 settings 不只是“布尔开关集合”，而是能表达“这次扩展/导出的目标集合”。 |
| UnLua | `UUnLuaSettings` 里直接暴露 `EnvLocatorClass`、`ModuleLocatorClass`、`PreBindClasses`，运行时按配置实例化 locator 并预绑定类型；同时 settings section 还挂了 `OnModified()`，保证修改会进模块处理逻辑。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaSettings.h:47`-`:56`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:77`-`:79`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaModule.cpp:245`-`:249`、`:272`-`:276` | 把 settings 提升成 typed manifest，允许用户通过配置声明“要用哪个 locator / 先绑定哪些类”。 |
| puerts | 即使不把扩展类列表直接写进 settings，模块也把 settings 注册成 active integration point：`RegisterSettings()` 后立刻绑定 `SettingsSection->OnModified()`，并在同一路径里解析 `DefaultPuerts.ini`。 | `Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:353`-`:389` | 配置面不应只是被动显示页；至少要有验证、保存回调和运行时应用路径。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 `UAngelscriptSettings` 从 passive config bag 提升为“扩展清单 + 行为开关”二层模型，但默认仍保持空清单。 |
| 具体步骤 | 1. 在 `UAngelscriptSettings` 新增一组 typed manifest 字段，例如 `ExtensionModules`、`RuntimeHostClass`、`BindingContributorClasses`、`CompileObserverModules`；保持现有 warning/import 开关不动。 2. 在 `AngelscriptEditorModule` 注册 settings 时补一个 `OnModified`/校验回调，对不存在的 module、未实现接口的 class 给出 editor 期报错。 3. 在 `FAngelscriptRuntimeModule::StartupModule()` 或 engine pre-initialize 阶段消费 manifest：按 module 名加载扩展模块，按 class 列表实例化 contributor，再把它们接到现有 bind/type/compile hook 上。 4. 增加一份启动期 dump，列出当前启用的 manifest 项，避免用户只能靠读源码确认扩展是否生效。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptExtensionManifest.h/.cpp` |
| 预估工作量 | S-M |
| 架构风险 | 一旦 settings 开始驱动 module/class 加载，就必须定义好失败策略和加载顺序；否则很容易把今天简单的启动路径变成隐式依赖图。 |
| 兼容性 | 向后兼容。新 manifest 字段默认空数组/空类，不改变现有项目；旧 `ini` 仍只控制 built-in 行为。 |
| 验证方式 | 1. 新增一个最小宿主模块，只通过 `DefaultEngine.ini` 把它加入 `ExtensionModules`，验证启动时能看到扩展被装载。 2. 故意填写一个错误的 class/module，确认 Editor settings 页能给出明确报错。 3. 复跑现有 compile/bind 回归，确认 manifest 为空时行为与当前版本完全一致。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-EP3 | 正式运行时缺少可替换的 engine owner / host seam | 扩展点新增 | 高 |
| P2 | Arch-EP4 | settings 只能调内建行为，不能声明扩展参与者 | 配置面产品化 | 中 |

---

## 架构分析 (2026-04-08 15:18)

### Arch-EP5：扩展事件面真实存在，但 public contract 被分散到三套不同 owner 里

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `DECLARE_MULTICAST_DELEGATE` / `DECLARE_DELEGATE` / `OnXxx` 搜索得到的插件级扩展事件面是否足够集中、可发现 |
| 当前设计 | 本轮对 `Plugins/Angelscript/Source/` 的全文搜索显示，插件级 hook 主要分散在三种暴露方式里：`FAngelscriptRuntimeModule` 用 `GetXxx()` accessor 返回 static delegate；`FAngelscriptClassGenerator` 与 `FAngelscriptPreprocessor` 直接暴露 public static multicast field；与此同时，`OnXxx` 搜索还会命中 `UAngelscriptAbilitySystemComponent` 这类 gameplay API 事件。结果是“事件不少”，但“哪个是插件扩展 hook、哪个只是运行时组件事件”需要靠读源码自行区分。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h:9`-`:47` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp:42`-`:135` 把 compile/debug/editor 相关 hook 挂在一组 `GetPreCompile()` / `GetPostCompile()` / `GetEditorCreateBlueprint()` accessor 上；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h:8`、`:29`-`:30` 直接公开 `OnProcessChunks` / `OnPostProcessCode` static field；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h:12`-`:38` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:70`-`:77`、`:2348`-`:2395`、`:2469`、`:3873`-`:3932` 直接公开 reload 相关 static field；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilitySystemComponent.h:141`-`:146`、`:299`-`:325` 又定义了一组 `OnInitAbilityActorInfo` / `OnAbilityGiven` / `OnOwnedTagUpdated` 等 gameplay 事件；`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:25`-`:45` 还需要手工把 `OnPostReload` 转译成 editor menu 重注册流程。 |
| 优点 | compile、reload、editor integration 的关键阶段事实上都已经有 hook；外部宿主模块理论上可以在多个阶段介入。 |
| 不足 | extension 事件面没有单一入口，也没有统一命名/owner 规则。外部用户要先知道 `RuntimeModule accessor`、`Preprocessor static`、`ClassGenerator static` 三种接法，才能判断该订阅哪里；而 `OnXxx` 关键字搜索又会混入 gameplay 组件事件，降低 discoverability。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | 统一用专门的 delegate carrier 暴露模块级生命周期。`FUnrealCSharpCoreModuleDelegates` 和 `FUnrealCSharpModuleDelegates` 分别集中承载 generator、compile、runtime activation 事件。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Delegate/FUnrealCSharpCoreModuleDelegates.h:7`-`:37`；`Reference/UnrealCSharp/Source/UnrealCSharp/Public/Delegate/FUnrealCSharpModuleDelegates.h:3`-`:17` | 让“哪些事件是给扩展者订阅的”在 header 级别一眼可见，而不是散落在多个实现 owner。 |
| UnLua | 统一用 `FUnLuaDelegates` 这一处集中暴露 `OnLuaStateCreated`、`OnPreStaticallyExport`、`CustomLoadLuaFile`、`ReportLuaCallError` 等 runtime hook。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDelegates.h:22`-`:50` | 把 lifecycle hook、替换型 hook、错误处理 hook 收束在一个 public contract 下，扩展者不需要猜测该去哪个类找事件。 |
| puerts | 不把扩展能力分散成若干 static field，而是集中挂在 `IPuertsModule` 上，例如 `ReloadModule()`、`InitExtensionMethodsMap()`、`SetJsEnvSelector()`。 | `Reference/puerts/unreal/Puerts/Source/Puerts/Public/PuertsModule.h:17`-`:56` | 即便不是 delegate，也可以用单一 module interface 作为“正式扩展入口”。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 新增一层统一的 `extension event hub`，把现有 compile/reload/preprocess/editor hook 从“能搜到”提升为“正式 contract”。 |
| 具体步骤 | 1. 在 `Core/` 新增 `FAngelscriptExtensionDelegates` 或 `IAngelscriptExtensionEvents`，把插件级 hook 收口为几组命名事件，例如 `Compiler.*`、`Reload.*`、`Editor.*`、`Debug.*`。 2. 让 `FAngelscriptRuntimeModule::GetPreCompile()`、`FAngelscriptPreprocessor::OnProcessChunks`、`FAngelscriptClassGenerator::OnPostReload` 等现有入口内部转发到新 hub，而不是立刻删除旧入口。 3. 为每组事件增加最小上下文 struct，例如 `FAngelscriptCompileEventContext`、`FAngelscriptReloadEventContext`，减少扩展者必须回读全局状态的需求。 4. 在 `StateDump` 或一份自动生成文档里列出可订阅事件、触发阶段与线程/生命周期约束，避免用户只能靠 `rg On` 搜索。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptExtensionDelegates.h/.cpp` |
| 预估工作量 | M |
| 架构风险 | 现有扩展者可能已经直接绑定到 static field 或 accessor；如果重命名或重排广播时机，会产生隐性回归。 |
| 兼容性 | 向后兼容。保留旧 accessor/static field 作为 forwarding shim；新 hub 只解决 discoverability 与上下文模型，不改变默认广播顺序。 |
| 验证方式 | 1. 写一个宿主测试模块，同时用旧入口和新 hub 订阅 compile/reload hook，确认两边都能收到同样事件。 2. 运行 editor menu extension 流程，确认 `OnPostReload` 仍能驱动菜单重注册。 3. 通过 `StateDump` 或自动文档检查事件目录是否完整列出。 |

### Arch-EP6：`virtual` 子类化点大量存在，但主要服务 gameplay/editor 外壳，而不是 core pipeline 扩展

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 全量 `virtual` 搜索后，用户真正可通过子类化介入的扩展点是否对准 bind/compiler/runtime host 等核心需求 |
| 当前设计 | 本轮对 `AngelscriptRuntime` 与 `AngelscriptEditor` 的 `virtual` 搜索显示，最成体系的子类化面集中在 `UScriptEngineSubsystem`、`UScriptGameInstanceSubsystem`、`UScriptLocalPlayerSubsystem`、`UScriptWorldSubsystem`、`UScriptEditorSubsystem` 这些 gameplay/editor 容器类，以及 `UScriptEditorMenuExtension` 这种 editor extension object。反过来，真正决定脚本根发现、module 解析、bind 策略、编译前决策的 `Core/`、`Preprocessor/`、`RuntimeModule` 并没有对应的 public strategy base class；`FAngelscriptEngineDependencies` 也只是 `TFunction` 集合，不是可配置的 subclass seam。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/BaseClasses/ScriptEngineSubsystem.h:6`-`:104`、`Plugins/Angelscript/Source/AngelscriptRuntime/BaseClasses/ScriptGameInstanceSubsystem.h:6`-`:88`、`Plugins/Angelscript/Source/AngelscriptRuntime/BaseClasses/ScriptLocalPlayerSubsystem.h:6`-`:53`、`Plugins/Angelscript/Source/AngelscriptRuntime/BaseClasses/ScriptWorldSubsystem.h:7`-`:158`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:7`-`:90` 都把 `virtual` 主要用于 `BP_ShouldCreateSubsystem`、`BP_Initialize`、`BP_Tick` 这类生命周期壳；`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h:42`-`:138` 甚至已经提供 `GatherExtensionFunctions()`、`CallFunctionOnSelection()`、`GetExtensionPointOrDefault()` 这类清晰的 typed extension object；但 `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h:10`-`:30` 没有任何 `virtual` strategy，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h:23`-`:47` 只有模块生命周期 override 与 delegate accessor，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:86`-`:95` 的 `FAngelscriptEngineDependencies` 也只是函数对象聚合。 |
| 优点 | gameplay 与 editor 层的子类化体验不错，脚本作者可以比较自然地扩展 subsystem 和 editor menu 行为。 |
| 不足 | 当需求转向“自定义脚本根定位”“替换 module/import 解析”“按项目策略注入 bind/type policy”时，现有 `virtual` 面几乎帮不上忙；用户只能直接写宿主 `C++ module glue`，或继续要求插件新增 static hook。换句话说，子类化 seam 被投入到了上层使用体验，而不是 core extensibility。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 直接把 runtime strategy 做成 settings 可配置的 subclass seam：`EnvLocatorClass` / `ModuleLocatorClass` 是 `TSubclassOf<>`，模块激活时实例化它们；对象到脚本模块的映射再通过 `IUnLuaInterface::GetModuleName()` 下放给宿主对象。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaSettings.h:23`-`:57`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaModule.cpp:91`-`:128`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaInterface.h:23`-`:39` | 把真正影响 runtime route 的点做成可子类化 contract，而不是只给 gameplay 外壳很多 `virtual`。 |
| puerts | 直接抽象 `IJSModuleLoader`，并让 `FJsEnv` 构造函数接受自定义 loader；默认 loader 的 `Search()` / `Load()` / `CheckExists()` 也都是 `virtual`。同时 `IPuertsModule::SetJsEnvSelector()` 负责多 env 路由策略。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSModuleLoader.h:17`-`:51`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JsEnv.h:61`-`:101`；`Reference/puerts/unreal/Puerts/Source/Puerts/Public/PuertsModule.h:37`-`:55`；`Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:208`-`:210` | 把 module resolution / env selection 这些核心策略放在正式接口层，用户子类化的努力落在真正有价值的位置。 |
| UnrealCSharp | 即使不强调 `virtual`，也会把可扩展阶段显式 contract 化，例如 `OnBeginGenerator` / `OnEndGenerator` / `OnCompile` 与 `ExportModule` / `ClassBlacklist` 等 settings。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Delegate/FUnrealCSharpCoreModuleDelegates.h:7`-`:37`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpEditorSetting.h:136`-`:149` | 关键不是“必须虚函数化”，而是 core pipeline 要有正式、稳定的扩展合同。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 不推翻现有 subsystem/editor 子类化面，而是在 core pipeline 补一组窄而清晰的 strategy object/interface，让用户的 subclassing 成本落在真正的架构扩展点上。 |
| 具体步骤 | 1. 先从当前已经有雏形的 seam 下手，把 `FAngelscriptEngineDependencies` 中与脚本根相关的 lambda 提升为 `UAngelscriptScriptRootLocator` 或 `IAngelscriptScriptRootLocator`。 2. 把 `Preprocessor` 中与 module/import 路由相关的决策抽成 `IAngelscriptModuleResolver`，默认实现复用现有逻辑。 3. 对 bind/type 过滤策略新增一个轻量接口，例如 `IAngelscriptBindingPolicy`，仅负责 bind enable/disable、命名空间变换和来源标签，不触碰现有 binder 实现。 4. 通过 `UAngelscriptSettings` 或 runtime registry 装配这些 strategy；保留今天的 subsystem/editor `virtual` 面不变，只补 core pipeline。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptScriptRootLocator.h/.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptModuleResolver.h/.cpp` |
| 预估工作量 | M-L |
| 架构风险 | 一旦 core pipeline 开始实例化 strategy object，就必须明确生命周期与线程约束；否则很容易引入比当前 static hook 更难排查的状态问题。 |
| 兼容性 | 向后兼容。默认 strategy 复用当前 lambda/static 逻辑，现有 subsystem/editor 子类和外部直接绑定 static hook 的代码无需改动。 |
| 验证方式 | 1. 增加一个宿主测试插件，自定义 `ScriptRootLocator` 与 `ModuleResolver`，验证无需改插件源码即可改变脚本根与模块解析策略。 2. 复跑 compile/reload/bind 回归，确认默认 strategy 下行为完全一致。 3. 在 editor menu extension 与 subsystem 示例中验证既有 `virtual` 面不受影响。 |

### Arch-EP7：`ini/settings` 可配置行为很多，但“何时生效”没有统一 apply contract

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 所有可通过 `ini/settings` 配置的行为，是否具有明确的一致生效时机、校验路径和热更新约束 |
| 当前设计 | 本轮通读 `UAngelscriptSettings` 可见当前插件已经暴露了大批 `Config` 行为开关；但这些配置的应用路径是分裂的：一部分在 `FAngelscriptEngine::Initialize()` 时复制到 engine/static state，一部分在 bind 注册或 debug server 请求时直接 `GetDefault<UAngelscriptSettings>()` 即时读取，Editor 侧则只注册了 settings 页面，没有 `OnModified`/validate/apply 回调。因此“能配置”成立，但“修改后何时生效、是否需要重编译/重启”没有统一 contract。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h:41`-`:225` 定义了整块 `Config=Engine` settings；本轮全文统计该文件含 31 个 `UPROPERTY(Config)`，其中大量字段带 `ConfigRestartRequired`；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:384`-`:393` 只是把 `UAngelscriptSettings` 注册到 settings viewer，没有保存回调；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1291`-`:1296` 在 engine 初始化时把 `bAutomaticImports`、namespace strip 列表等复制进运行时状态；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FMath.cpp:7`-`:14` 又在 bind 注册期直接读取 `MathNamespace` / `bDeprecateDoubleType`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:1499`-`:1504` 仍在请求期直接读取 `bScriptFloatIsFloat64` 与 `StaticClassDeprecation`。 |
| 优点 | 纯内建行为的可配置面已经很宽，warning、namespace、preprocessor、debugger、默认 specifier 等都能通过 `ini` 调节。 |
| 不足 | 当前没有统一回答“这是启动时锁定、下次 compile 生效，还是可以 live-safe 修改”。扩展者和项目维护者只能从调用点反推 settings 时机，导致 rollout 与排障成本偏高；这也削弱了 settings 作为正式扩展面的可信度。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `RegisterSettings()` 不只是注册页面，还立即绑定 `SettingsSection->OnModified()`，并显式解析 `DefaultPuerts.ini`。 | `Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:353`-`:393` | 把 settings 变成 active contract，而不是 passive config bag。 |
| UnLua | `RegisterSettings()` 同样绑定 `Section->OnModified()`，并在 `OnSettingsModified()` 里同步 runtime cached state；非 editor 还会显式 `ReloadConfig()`。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaModule.cpp:238`-`:276` | 至少要让“哪些配置会被重新应用”有明确入口。 |
| UnrealCSharp | settings 注册前就会补齐默认 `SupportedModule` / `SupportedAssetPath` / `SupportedAssetClass`，说明配置对象在注册期就承担 workflow normalization 责任，而不是只做展示。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Setting/UnrealCSharpEditorSetting.cpp:44`-`:115` | 可以在 settings 注册/保存期完成默认值填充、校验与归一化。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 为现有 `UAngelscriptSettings` 增加显式的 `validate + apply` 层，把“可配置”升级成“可预测生效”。 |
| 具体步骤 | 1. 新增 `FAngelscriptSettingsApplier` 或 `UAngelscriptSettings::ApplyToRuntime()`，把 settings 分成三类：`StartupOnly`、`NextCompile`、`LiveSafe`。 2. 在 `AngelscriptEditorModule` 注册 settings 时绑定 `OnModified`，先跑校验，再根据分类决定是立即应用、标记“下次 compile 生效”，还是提示需要 restart。 3. 把今天散落在 `Engine.cpp`、`Bind_FMath.cpp`、`DebugServer.cpp` 的直接读取逐步收口到 `EffectiveSettings` snapshot，减少调用点自己决定时机。 4. 在 `StateDump`/日志中输出 effective settings 与生效阶段，便于项目侧排障。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FMath.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettingsApplier.h/.cpp` |
| 预估工作量 | S-M |
| 架构风险 | 如果一口气把所有读取点都改成 snapshot，容易把今天“偶尔直接读配置”的行为静默改变；应分批迁移并先定义分类。 |
| 兼容性 | 向后兼容。现有 `DefaultEngine.ini` 字段名全部保留；新增的只是 apply/validate 路径与更清晰的生效提示。 |
| 验证方式 | 1. 增加一组自动化测试，分别覆盖 `MathNamespace`、`bAutomaticImports`、`bScriptFloatIsFloat64` 三类不同生效时机的 settings。 2. 在 Editor 修改 setting 后检查是否收到正确的“立即生效 / 下次 compile / restart”提示。 3. 复跑现有 bind、preprocess、debugger 回归，确认 effective settings 与旧行为一致。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-EP5 | 插件级事件面分散在多个 owner，discoverability 差 | 扩展面收口 | 高 |
| P1 | Arch-EP6 | `virtual` 子类化点主要落在 gameplay/editor 外壳，缺少 core strategy seam | 扩展点重分配 | 高 |
| P2 | Arch-EP7 | settings 生效时机没有统一 apply contract | 配置语义产品化 | 中 |

---

## 架构分析 (2026-04-08 15:31)

### Arch-EP8：扩展注册缺少 contributor ownership 与 teardown contract，static registry 在模块卸载时不可回收

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 外部模块注册 bind、type finder、reload/editor hook 之后，是否存在显式 owner、撤销路径和模块卸载语义 |
| 当前设计 | `bind/type/event` 三类扩展点都支持“追加注册”，但基本没有“按 contributor 撤销”。`FAngelscriptBinds::FBind` 通过静态构造直接把函数压入 process-wide `BindArray`；`FAngelscriptType::RegisterTypeFinder()` 只向 `TypeFinders` 追加 lambda；Editor 模块启动时又继续把一批 lambda 挂到 `ClassGenerator` 和 `RuntimeModule` 的 static delegate 上，而 `ShutdownModule()` 只清掉极少数 handle。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:438`-`:475` 的 `FBind` / `RegisterBinds()` 只有注册入口；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:120`-`:154` 把 bind 存进 static `BindArray`，`ResetBindState()` 在 `:186`-`:188` 只重置 `FAngelscriptBindState`，不会移除已登记 bind；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:79`-`:81` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:114`-`:117` 表明 `RegisterTypeFinder()` 只有追加，没有 handle/unregister；`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h:50`-`:152` 在 `Init()` 里连续 `AddLambda()` 到 `OnStructReload` / `OnClassReload` / `OnPostReload` 等 static delegate；`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:25`-`:38` 再次 `AddLambda()` 到 `OnPostReload` 和 `OnEnginePreExit`；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:351`-`:412` 的 `StartupModule()` 注册了 `FClassReloadHelper::Init()`、`InitializeExtensions()`、directory watcher、`GetDebugListAssets().AddLambda()`、`GetEditorCreateBlueprint().AddLambda()`，但 `ShutdownModule()` 在 `:676`-`:689` 只移除了 `OnObjectPreSave` 和 state dump extension。 |
| 优点 | 内建扩展接入成本极低，静态 `FBind` 和直接 `AddLambda()` 非常适合插件内部快速铺开能力。 |
| 不足 | 对外部扩展者和长期演进而言，这是一条“只能加、很难退”的 contract：模块卸载、Live Coding、可选子插件关闭、重复初始化、防止 stale callback 都缺正式支持。即便内部代码，也已经出现“局部有 handle，整体无 owner”的不一致状态。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | 订阅 lifecycle delegate 时显式保存 `FDelegateHandle`，并在 `Deinitialize()` / 析构里移除；delegate carrier 虽然也是 static，但 subscriber lifecycle 是 owner-scoped。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:21`-`:46`；`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FCSharpBind.cpp:14`-`:32` | 关键不是“禁止 static delegate”，而是任何 subscriber 都必须有归属对象和 teardown。 |
| UnLua | `FUnLuaModule::SetActive()` 在激活时创建 `EnvLocator`、注册系统回调和 UObject listener，在停用时成对 `Remove()` / `Reset()` / `RemoveFromRoot()`；扩展宿主对象由模块自己持有。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaModule.cpp:96`-`:133` | 把扩展贡献物收编到模块生命周期内，停用插件时可以完整回收。 |
| puerts | 把 selector 与 extension method 初始化挂在 `IPuertsModule` / `FPuertsModule` 上，状态保存在模块实例成员里，而不是散落在若干匿名 static callback 中。 | `Reference/puerts/unreal/Puerts/Source/Puerts/Public/PuertsModule.h:17`-`:47`；`Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:97`-`:120` | 对“长期存在的扩展策略”优先使用 module-owned state，而不是无 owner 的全局注册。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留现有 static API 的前提下，为 bind、type finder、runtime/editor hook 补一层 `owner-scoped registration`，让“可注册”升级成“可撤销、可卸载、可排查来源”。 |
| 具体步骤 | 1. 在 `Core/` 新增 `FAngelscriptExtensionHandle` / `FAngelscriptExtensionOwner`，统一承载 `SourceModule`、注册类型、delegate handle 或 registry slot id。 2. 给 `FAngelscriptBinds` 增加新的 contributor API，例如 `RegisterBindContributor(SourceModule, BindName, BindOrder, Function)`，内部返回 handle；旧 `FBind` 构造仍保留，但标记为 `Persistent/Legacy` contributor。 3. 给 `FAngelscriptType::RegisterTypeFinder()` 增加 handle 版本和 `UnregisterTypeFinder()`，`GetByProperty()` 遍历前先过滤已失效 owner。 4. 把 `FClassReloadHelper::Init()`、`UScriptEditorMenuExtension::InitializeExtensions()`、`FAngelscriptEditorModule::StartupModule()` 里对 Angelscript delegate 的注册改为保存 handle，并在 `ShutdownModule()` / `DeinitializeExtensions()` 成对解绑；directory watcher handle 也需要持久化到模块成员。 5. 在 `StateDump` 增加 extension contributor 列表，至少能看到 `SourceModule`、`Kind`、`RegisteredAt`、`bPersistent`。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptExtensionHandle.h/.cpp` |
| 预估工作量 | M |
| 架构风险 | 需要明确哪些注册是“引擎全程常驻”、哪些是“模块生命周期绑定”；如果一开始划分不清，容易把当前依赖静态初始化的 bind 路径打断。 |
| 兼容性 | 向后兼容。现有 `FBind`、`RegisterTypeFinder()`、static delegate 字段可继续工作；新 owner/handle API 先用于新扩展和 Editor 自身代码，旧路径逐步迁移。 |
| 验证方式 | 1. 增加一个测试插件，在 `StartupModule()` 注册 bind、type finder 和 compile/reload hook，在 `ShutdownModule()` 卸载，验证不会残留重复回调。 2. 重复执行 Editor 模块 startup/shutdown 或 Live Coding 场景，确认 `GetDebugListAssets()` / `OnPostReload` 不会累计多次触发。 3. 通过 `StateDump` 校验 contributor 列表能显示来源模块与存活状态。 |

### Arch-EP9：扩展优先级与覆盖语义是隐式的，`bind/type finder/runtime hook` 三套机制各自为政

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 当多个扩展同时介入同一阶段时，谁先执行、谁能覆盖谁、冲突如何判定，是否有统一 contract |
| 当前设计 | 当前插件至少同时存在三套 precedence 规则：`bind` 通过 `BindOrder` 排序，但 tie-break 只靠 `BindOrder` 自身；`type finder` 完全靠注册顺序，`GetByProperty()` 命中第一个返回 `true` 的 finder 就立即结束；`RuntimeModule` 里的 hook 则把 single-cast strategy 和 multicast observer 混在同一组 `GetXxx()` accessor 里，调用点只能靠 `Execute()` / `Broadcast()` 细读实现来猜“这是覆盖型还是叠加型”。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:120`-`:148` 定义 `FBindFunction::operator<()` 只比较 `BindOrder`，`GetSortedBindArray()` 也只按该键排序；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:450`-`:467` 的默认 `FBind` 构造会把大量扩展自然落到 `BindOrder = 0`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:148`-`:160` 在 `GetByProperty()` 中按 `Database.TypeFinders` 注册顺序遍历并在首个命中时直接 `return`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h:9`-`:21` 同时声明了 `DECLARE_DELEGATE` 和 `DECLARE_MULTICAST_DELEGATE`，但 `:32`-`:47` 统一以 `GetXxx()` 形式暴露；调用侧也确实混用两种语义：`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp:189`-`:190` 执行 `GetDynamicSpawnLevel().Execute()`，`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:658`-`:661` 与 `:1150` 走 single-cast `Execute/ExecuteIfBound()`，而同文件 `:1170`、`:1180` 又对 `GetDebugListAssets()` 和 `GetEditorCreateBlueprint()` 进行 `Broadcast()`；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:434`-`:435` 也把 `GetEditorGetCreateBlueprintDefaultAssetPath()` 当单一 provider 调用。 |
| 优点 | 每种扩展面都足够轻量，插件内部开发者能很快接上一个 hook，不必先设计复杂协议。 |
| 不足 | 一旦进入“多个外部模块共同扩展”的场景，规则就开始漂移：有的是 first-win，有的是 last-bind-wins，有的是 order-only 但 tie 不稳定，有的是 additive multicast。扩展者如果不通读源码，无法判断自己的自定义全局函数、type binding、debug hook 与别人的插件谁优先生效。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 在公共 contract 中直接把 lifecycle multicast 与 override-style delegate 分开定义；`CustomLoadLuaFile` 的单一 provider 语义在示例工程里通过 `Unbind()` / `BindStatic()` 明确表达；模块定位则进一步下放到 `ModuleLocatorClass`。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDelegates.h:22`-`:49`；`Reference/UnLua/Source/TPSProject/TutorialBlueprintFunctionLibrary.cpp:91`-`:103`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:74`-`:79`、`:372`-`:376` | “这是 observer，还是唯一 strategy”应该在 API 级别就被看见，而不是靠调用点推断。 |
| puerts | 把模块解析和 env 路由都做成明确 strategy：`IJSModuleLoader` 是可替换接口，`IPuertsModule::SetJsEnvSelector()` 是显式 setter，而不是若干同名 `GetXxx()` delegate。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSModuleLoader.h:17`-`:43`；`Reference/puerts/unreal/Puerts/Source/Puerts/Public/PuertsModule.h:37`-`:45`；`Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:112`-`:120` | 对覆盖型扩展直接使用 interface/setter，可天然避免“多个 multicast listener 里谁说了算”的问题。 |
| UnrealCSharp | 即便主要依赖 multicast delegate，也会把阶段顺序写成明确的编排主线：`OnBeginGenerator` 在生成链最前，`OnEndGenerator` 在末尾，阶段边界稳定且容易推断。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:237`-`:309` | 当必须保留广播模型时，至少要让 phase order 与语义边界足够明确、可预测。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把当前扩展面正式分成三类 contract：`Observer`、`Strategy`、`OrderedContributor`，并为每类提供显式 precedence 规则，而不是继续让 `bind/type finder/delegate` 各自定义。 |
| 具体步骤 | 1. 在 `Core/` 新增一份轻量约定，例如 `EAngelscriptExtensionSemantics { Observer, Strategy, OrderedContributor }`，先用于文档化和 state dump。 2. 对 `GetDynamicSpawnLevel`、`GetDebugCheckBreakOptions`、`GetDebugBreakFilters`、`GetEditorGetCreateBlueprintDefaultAssetPath` 这类单一 provider hook，补显式 `Set...Provider()` 或 interface 形式的 strategy API；旧 `GetXxx()` 继续保留为 shim。 3. 对 `FAngelscriptBinds` 增加稳定 secondary key，例如 `RegistrationSequence` / `SourceModule`，避免同 `BindOrder` 下排序不可预期；必要时再加 `Before/After` 约束。 4. 对 `RegisterTypeFinder()` 增加 `Priority` 和 `SourceModule`，并在 `GetByProperty()` 命中时输出 trace，明确是哪个 finder 截获了该 property。 5. 在 `StateDump` 或自动文档中列出每个扩展点的语义类型、当前 provider/contributor 顺序和冲突告警。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptExtensionSemantics.h` |
| 预估工作量 | S-M |
| 架构风险 | 一旦把“隐式 first-win / last-bind-wins”改成显式规则，可能暴露出今天项目里已经依赖旧顺序的行为；需要先做观测，再开启更严格约束。 |
| 兼容性 | 基本向后兼容。旧 `GetXxx()` 与默认 `BindOrder=0` 路径仍可运行，但在检测到多个 strategy provider 或同优先级冲突时应输出 warning，引导迁移到新 API。 |
| 验证方式 | 1. 新增冲突测试：两个测试模块同时注册同一 `type finder`、同一 `bind order`、同一 single-provider hook，验证日志和最终生效顺序都可预测。 2. 复跑现有 bind/type/debug/editor 回归，确认默认项目在没有冲突时行为不变。 3. 用 `StateDump` 检查每个扩展点都能看到语义类型与当前 precedence chain。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-EP8 | 扩展注册没有 owner/teardown contract，模块卸载与 Live Coding 不安全 | 生命周期补强 | 高 |
| P1 | Arch-EP9 | 扩展优先级与覆盖语义隐式，跨 `bind/type finder/runtime hook` 不一致 | contract 显式化 | 高 |

---

## 架构分析 (2026-04-08 15:48)

### Arch-EP10：插件已经支持 `multi-engine`，但大多数扩展点仍按 process scope 生效

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 外部扩展能否只作用于某个 `FAngelscriptEngine` / clone / script root，而不是污染整个进程 |
| 当前设计 | 当前仓库已经正式支持 `Full + Clone` 多引擎模型，但扩展注册大多仍停留在 process-wide static：`FAngelscriptBinds` 的 `BindArray` 是全局静态数组，`FAngelscriptRuntimeModule` 的 compile/debug/editor hook 都返回函数内 static delegate，`FAngelscriptPreprocessor` 与 `FAngelscriptClassGenerator` 也暴露 public static delegate；`FAngelscriptType` 虽然优先取 `TryGetCurrentEngine()` 的 database，但在没有 current engine 时会回落到 `LegacyDatabase`，使扩展注册的真实作用域依赖“注册发生时是否正好有 current engine”。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:132`-`:148` 把 binds 收到 static `BindArray` 并全局排序；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h:32`-`:47` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp:42`-`:126` 把各类 hook 暴露为函数内 static delegate；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h:8`、`:29`-`:30` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h:12`-`:19`、`:31`-`:38` 继续把 preprocess/reload 事件做成 static field；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:35`-`:45` 在无 current engine 时回退到 static `LegacyDatabase`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:114`-`:117` 的 `RegisterTypeFinder()` 只向当前解析到的 database 追加；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineTests.cpp:652`-`:664` 说明 clone create 不会重放 startup binds，`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineTests.cpp:759`-`:778` 说明 full owner 与 clones 已被当成正式 shared-state participant 跟踪。 |
| 优点 | static registry 让插件内部扩展很轻，startup bind 也能在 clone 路径复用，不必每个 clone 重放一轮注册；这和当前 `multi-engine` 测试里“clone 不重播 bind”的目标是一致的。 |
| 不足 | 一旦扩展来自宿主模块或第三方插件，scope 就变得不受控：扩展者无法声明“只对测试 clone 生效”“只对某个 script root 生效”“只对某个 runtime host 生效”；更微妙的是，`RegisterTypeFinder()` 在无 current engine 时会落到 `LegacyDatabase`，使注册时机本身决定作用域，隐藏了 cross-engine bleed 的风险。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 把扩展 seam 明确绑到 env 实例：`IJSModuleLoader` 是 per-env contract，`FJsEnv` 构造函数直接接收 loader，`IPuertsModule::SetJsEnvSelector()` 与 `FJsEnvGroup::SetJsEnvSelector()` 再把对象路由绑定到 env group，而不是依赖 ambient current context。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSModuleLoader.h:17`-`:48`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JsEnv.h:64`-`:97`；`Reference/puerts/unreal/Puerts/Source/Puerts/Public/PuertsModule.h:41`-`:45`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvGroup.cpp:155`-`:176` | 扩展范围先绑定到 env，再谈广播和共享；这让“多 env”不是附属优化，而是 contract 的一部分。 |
| UnLua | 用 typed settings 和宿主接口决定作用域：`EnvLocatorClass` / `ModuleLocatorClass` / `PreBindClasses` 都是 manifest，模块启动时实例化 locator；对象到脚本模块的最终选路再落到 `IUnLuaInterface::GetModuleName()`。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaSettings.h:47`-`:56`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaModule.cpp:103`-`:120`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaInterface.h:29`-`:38` | 即便保留全局模块，也先把“谁决定 env/module scope”做成显式对象，而不是把 scope 隐含在注册时机里。 |
| UnrealCSharp | 虽然也使用 static lifecycle delegates，但扩展范围至少由显式 manifest 约束，例如 `ExportModule` / `ClassBlacklist`；generator 观察者看到的是一组声明过的导出集合，而不是“当前 engine 是谁”这种隐式上下文。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Delegate/FUnrealCSharpCoreModuleDelegates.h:14`-`:30`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpEditorSetting.h:140`-`:148` | 即便不做 per-env registry，也可以先把作用域声明显式化，减少 ambient global state。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留现有 static API 的前提下，引入显式 `extension scope`，把“process-global 默认值”和“engine-local 生效面”分开。 |
| 具体步骤 | 1. 在 `Core/` 新增 `EAngelscriptExtensionScope` 与 `FAngelscriptScopedExtensionRegistry`，支持 `Global`、`PrimaryEngineOnly`、`SpecificEngine`、`ScriptRootPattern` 几类 scope。 2. 给 `FAngelscriptBinds`、`FAngelscriptType::RegisterTypeFinder()`、compile/reload/preprocess hook 增加 scoped overload；旧 API 保持为 `Global`。 3. 在 `FAngelscriptEngine` 初始化和 `CreateCloneFrom()` 路径中，把 global registry snapshot 到 engine-local effective registry，再叠加该 engine 的 scoped contributors；先不支持“运行中热附着到已有 clone”，避免时序失控。 4. 给 hook 上下文补 `FAngelscriptEngine*` / script-root 信息，让扩展者在旧 API 迁移期也能自行过滤。 5. 扩展 `AngelscriptMultiEngineTests`：验证 clone-specific type finder / compile observer 不会泄漏回 source engine，同时 legacy global bind 仍保持现有行为。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineTests.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptScopedExtensionRegistry.h/.cpp` |
| 预估工作量 | M-L |
| 架构风险 | 关键风险是 snapshot 时机定义：full engine 初始化后再注册的 scoped contributor 是否需要 retroactive apply，clone 何时继承 source engine 的 global vs local registry，都必须先定规则，否则会把今天“简单但粗粒度”的模型换成“细粒度但不可预测”的模型。 |
| 兼容性 | 向后兼容。所有现有 static 注册 API 默认映射到 `Global` scope；只有主动使用新 scoped API 的扩展才会获得 engine-local 隔离。 |
| 验证方式 | 1. 新增自动化测试，在 source engine 与两个 clones 上分别注册 scoped type finder / compile observer，验证只在目标 engine 生效。 2. 复跑现有 `StartupBindObservation` 和 `SharedState.ParticipantCountsTrackFullAndClones` 测试，确认 legacy global 行为不变。 3. 用 state dump 输出 effective contributors，人工核对 `Global` 与 engine-local registry 是否符合预期。 |

### Arch-EP11：core extensibility 仍偏“共享可变对象 + raw delegate”，缺少 typed strategy contract

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 用户在不改插件源码的情况下修改 compile/import/type binding 行为时，拿到的是正式策略接口，还是必须直接改动内部 owner 状态 |
| 当前设计 | 当前插件的 core 扩展多半不是“返回一个明确结果”，而是“拿到内部对象自己改”：`OnProcessChunks` / `OnPostProcessCode` 直接把 `FAngelscriptPreprocessor&` 整体广播出去；`FAngelscriptClassAnalyzeDelegate` 用 `FString&`、`bool&` 这种 in/out 参数改生成结果；`AdditionalCompileChecks` 则要求外部代码直接往 `FAngelscriptEngine::AdditionalCompileChecks` 这张 map 塞 `virtual` 对象；`RegisterTypeFinder()` 也只是追加 lambda，由 finder 直接改 `FAngelscriptTypeUsage` 并在第一个返回 `true` 时短路。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h:8`、`:29`-`:30` 定义 `FOnAngelscriptPreprocessHook(FAngelscriptPreprocessor&)`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:232`-`:287` 显示 `ProcessImports()` 之后直接 `OnProcessChunks.Broadcast(*this)` / `OnPostProcessCode.Broadcast(*this)`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h:13`-`:14` 定义 `FAngelscriptClassAnalyzeDelegate(FString&, TSharedPtr<FAngelscriptClassDesc>, bool&)`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:1310`-`:1311` 直接把 `GeneratedStatics` 与 `bHasStatics` 交给外部 delegate 改写；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:357`-`:359` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptAdditionalCompileChecks.h:4`-`:9` 定义 `AdditionalCompileChecks` map 和 `virtual` 检查对象，但 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:1374`-`:1383`、`:2474`-`:2487` 直接在 class generator 中查 map 并执行；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:114`-`:117`、`:147`-`:160` 说明 type finder 是“追加 lambda + 首个命中短路”的共享状态协议。 |
| 优点 | 这种模式对插件内部开发者极快，几乎不需要额外抽象；很多“再插一个钩子”的需求当天就能落地。 |
| 不足 | 对正式扩展者不友好：没有 typed request/response，就很难表达 `bHandled`、fallback、diagnostics、priority、source ownership 等关键信息；扩展者必须理解 owner 内部结构和调用时机，才能安全修改 compile/import/type 行为。结果是事件虽然多，但真正可支持的 strategy contract 仍然偏弱。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 把“决策型扩展”做成 interface method：`IJSModuleLoader::Search()` / `Load()` / `GetScriptRoot()` 是显式 request/response；`FJsEnv` 构造函数直接接收 loader；`IPuertsModule::SetJsEnvSelector()` 要求 selector 返回 env index。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSModuleLoader.h:17`-`:48`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JsEnv.h:64`-`:97`；`Reference/puerts/unreal/Puerts/Source/Puerts/Public/PuertsModule.h:41`-`:45` | 对 strategy hook 来说，typed return value 比“把 owner 整个交出去改”更容易验证、组合和记录来源。 |
| UnLua | `CustomLoadLuaFile` 明确返回 `bool` 并输出 `Data` 与 `ChunkName`，`LoadFromCustomLoader()` 再按返回值决定是否回退默认 loader；对象到脚本模块的映射则由 `IUnLuaInterface::GetModuleName()` 返回字符串结果。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDelegates.h:33`-`:49`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:557`-`:570`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaInterface.h:29`-`:38` | 观察型 hook 可以继续广播，但真正决定加载/选路的点应有明确输入和返回值。 |
| UnrealCSharp | 把“要生成什么”交给 `ExportModule` / `ClassBlacklist` 这类 manifest，而把 `OnBeginGenerator` / `OnEndGenerator` 保持为 observe-only delegate；策略与观察职责不混在同一个 raw hook 里。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpEditorSetting.h:140`-`:148`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Delegate/FUnrealCSharpCoreModuleDelegates.h:14`-`:30` | 可以同时拥有 delegate 和 extensibility，但要先把“这是 strategy 还是 observer”在 API 层分开。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 core 扩展面拆成 `Observer` 与 `Strategy` 两层 contract：广播仍保留，但真正改变 compile/import/type 决策的路径改用 typed interface / result struct。 |
| 具体步骤 | 1. 新增 `FAngelscriptStrategyResult` 基类和几类 typed result，例如 `FAngelscriptImportResolveResult`、`FAngelscriptClassAnalyzeResult`、`FAngelscriptTypeResolveResult`，统一携带 `bHandled`、`Diagnostics`、`Priority`、`SourceModule`。 2. 在 `Preprocessor/` 引入 `IAngelscriptImportResolver`，在 `Core/` 引入 `IAngelscriptClassAnalyzer` 与 `IAngelscriptTypeResolver`；默认实现直接适配当前 `OnProcessChunks`、`GetClassAnalyze()`、`RegisterTypeFinder()` 路径。 3. 把 `AdditionalCompileChecks` map 收口为 `RegisterAdditionalCompileChecks()` API，内部再适配旧 map，避免外部代码直接改 engine 成员。 4. 保留现有 raw delegate 作为 compatibility shim，但在 state dump / 日志中区分“legacy raw hook”与“typed strategy provider”。 5. 先只替换 compile/import/type 三条核心策略线，不动纯 observe-only 的 reload/debug/editor multicast，避免一次性过度抽象。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptAdditionalCompileChecks.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptStrategyContracts.h`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptImportResolver.h/.cpp` |
| 预估工作量 | M-L |
| 架构风险 | 最大风险是把今天“内部人心照不宣的可变状态协议”显式化时，会暴露出既有行为依赖短路顺序和隐式副作用；因此必须先做 adapter，而不是直接替换全部 raw hook。 |
| 兼容性 | 向后兼容。现有 delegate、`RegisterTypeFinder()` 和 `AdditionalCompileChecks` 继续可用，只是逐步迁移到 typed provider；旧扩展在未迁移前仍按 legacy 路径运行。 |
| 验证方式 | 1. 新增 import/type/class-analyze 三类测试：typed provider 返回 `bHandled=false` 时应回退旧逻辑，返回 diagnostics 时应写入编译结果。 2. 增加一个 legacy hook + typed provider 并存的测试，确认适配层不会重复执行或吞掉旧行为。 3. 复跑现有 compile/reload/preprocessor 学习测试，确认默认 adapter 下输出与当前版本一致。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-EP10 | `multi-engine` 已成立，但扩展注册 scope 仍偏 process-global | 作用域重构 | 高 |
| P1 | Arch-EP11 | core hook 依赖共享可变对象，缺少 typed strategy contract | contract 升级 | 高 |

---

## 架构分析 (2026-04-08 15:57)

### Arch-EP12：`settings` 目前只是行为开关，不是扩展实现的声明面

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `ini/settings` 是否能声明自定义扩展实现，以及 `virtual` 扩展是否能通过配置接入 |
| 当前设计 | 当前公开配置面基本分成两类：`UAngelscriptSettings` 里的 `Config` 属性用于切换内建行为，`FAngelscriptEngineConfig` / `FAngelscriptEngineDependencies` 用于进程参数和 C++ 依赖注入；真正的扩展实现体仍要靠宿主 `C++` 代码手工注册，配置文件本身不能声明“使用哪个 import resolver / type provider / extension module”。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h:41`-`:223` 的配置项全部围绕 `PreprocessorFlags`、`DisabledBindNames`、`bAutomaticImports`、warning/debug blacklist 等内建策略参数展开；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:64`-`:95` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:514`-`:567` 表明 `EngineConfig` 只读 command line，`EngineDependencies` 只接收 C++ `TFunction`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:71`-`:82`、`:120`-`:137` 虽然提供了 `RegisterTypeFinder()` 和大量 `virtual` 类型行为，但没有任何 settings 槽位去选择或实例化这些 provider。 |
| 优点 | 配置语义简单，启动路径稳定；大多数项目只需要调整内建 warning、namespace、import 模式时，维护成本很低。 |
| 不足 | 对“想扩展插件而不是只调插件”的用户，`ini` 只能调阈值和开关，不能声明扩展实现。结果是 `virtual` seam 虽然存在，但没有被产品化成“可配置的 provider contract”；扩展者仍要写 `C++ module` 并理解内部注册时机。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 直接把宿主可替换实现放进 settings：`EnvLocatorClass`、`ModuleLocatorClass`、`PreBindClasses` 都是配置项；模块启动和 `LuaEnv` 初始化时按配置实例化 locator 并预绑定类。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaSettings.h:23`-`:57`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaModule.cpp:103`-`:118`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:77`-`:79` | 把“用哪个扩展实现”做成配置声明，而不是要求宿主在若干内部静态点手工注册。 |
| UnrealCSharp | 虽然不是 class-provider 模式，但 `bEnableExport`、`ExportModule`、`ClassBlacklist` 已经把“生成哪些绑定、排除哪些类型”提升成显式 manifest，而不是散落在代码里的硬编码规则。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpEditorSetting.h:136`-`:148` | 即使不走 UObject subclass，也可以先把扩展范围做成声明式配置。 |
| puerts | 采用显式 strategy object：`IJSModuleLoader` 定义 `Search()` / `Load()` / `GetScriptRoot()`，`FJsEnv` 构造函数直接接收自定义 loader，而不是依赖 ambient static state。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSModuleLoader.h:17`-`:50`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JsEnv.h:61`-`:68` | 即使选择代码注入，也要让扩展实现成为正式 contract，而不是“知道内部头才能接”。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留现有 `UAngelscriptSettings` 开关语义的前提下，新增“扩展 manifest + provider class/module”配置层，让配置能声明扩展实现，而不仅是调内建参数。 |
| 具体步骤 | 1. 在 `Core/` 新增 `UAngelscriptExtensionSettings`，或在 `UAngelscriptSettings` 中增量加入 `ExtensionModules`、`ImportResolverClass`、`TypeProviderClasses`、`CompileObserverModules` 这类可选配置项。 2. 在 `FAngelscriptRuntimeModule::StartupModule()` 与 `FAngelscriptEngine::Initialize()` 增加 manifest 读取阶段：先加载声明的扩展模块，再实例化可配置 provider，并让它们走现有 `BindGlobalFunction()` / `RegisterTypeFinder()` / compile hook。 3. 为 import/type/bind 三条主线各定义一个最小 public provider base，例如 `UAngelscriptImportResolverBase`、`IAngelscriptTypeProvider`、`IAngelscriptBindContributor`；默认实现适配当前逻辑，空配置时行为完全不变。 4. 在 `Dump/AngelscriptStateDump.cpp` 增加一张 `ConfiguredExtensions` 表，把当前生效的 module/provider class 输出出来，避免配置生效面不可见。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptExtensionSettings.h/.cpp`、新增 provider base 头文件 |
| 预估工作量 | M |
| 架构风险 | 一旦允许配置驱动加载外部扩展，初始化顺序和错误恢复需要重新定义；尤其要避免把“配置错误”演化成“引擎初始化直接失败且无诊断”。 |
| 兼容性 | 向后兼容。所有新配置项都应为 optional，留空时完全走现有路径；已有 `DisabledBindNames`、warning/debug 设置和 command line 参数不变。 |
| 验证方式 | 1. 新建宿主测试模块，通过 `DefaultEngine.ini` 指定自定义 import/type provider，验证无需改插件源码即可生效。 2. 验证空配置项目与当前版本的 bind/type/import 输出完全一致。 3. 运行 `StateDump`，确认能看到配置声明的 extension modules/provider classes。 |

### Arch-EP13：扩展事件分散在多个 owner，上层只能靠 `grep` 发现 public hook

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | delegate / `OnXxx` 事件的可发现性、统一入口和稳定 public contract |
| 当前设计 | 当前扩展事件分散在多个 public owner：`FAngelscriptRuntimeModule` 通过 `GetPreCompile()` / `GetClassAnalyze()` 之类 getter 返回函数内 static delegate，`FAngelscriptPreprocessor` 和 `FAngelscriptClassGenerator` 直接暴露 public static delegate，`FAngelscriptStateDump` 又单独维护 `OnDumpExtensions`。扩展者要想找全 public hook，基本只能跨 `Core/Preprocessor/ClassGenerator/Dump` 逐个 grep。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h:9`-`:21`、`:32`-`:47` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp:42`-`:126` 同时定义了 single-cast 与 multicast hook，并通过 `GetXxx()` 暴露；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h:8`、`:29`-`:30` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:35`-`:36` 定义 `OnProcessChunks` / `OnPostProcessCode`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h:12`-`:19`、`:31`-`:38` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:70`-`:77` 定义 reload 系列 hook；`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.h:18`-`:22` 单独暴露 `OnDumpExtensions`。 |
| 优点 | 从插件内部实现角度，这种“谁拥有阶段，谁就顺手挂一个 delegate” 的方式非常直接，新增 hook 成本低。 |
| 不足 | 对外部扩展者不友好：public surface 没有统一索引，也没有一个稳定 header/模块来表达“这是正式支持的扩展入口”。不同 hook 还混用了 `GetXxx()`、public static field、single-cast、multicast 四种风格，导致 discoverability、文档化和版本演进都偏弱。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 把生命周期和 loader hook 集中到 `FUnLuaDelegates` 一个 public header 中，`CustomLoadLuaFile`、`OnLuaContextInitialized`、`OnObjectBinded` 等都从同一个入口暴露。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDelegates.h:22`-`:50` | 扩展者先记住一个 header，再去选择具体 hook，discoverability 明显更高。 |
| UnrealCSharp | 把 core 扩展阶段收敛到 `FUnrealCSharpCoreModuleDelegates`，`OnBeginGenerator`、`OnEndGenerator`、`OnCompile` 都集中定义，Editor/Compiler 再围绕这个中心订阅。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Delegate/FUnrealCSharpCoreModuleDelegates.h:7`-`:37` | 即使内部模块很多，也能给外部世界一个统一 delegate bus。 |
| puerts | 对关键扩展点直接给出单一 public contract：`IPuertsModule` 暴露 `InitExtensionMethodsMap()`、`SetJsEnvSelector()`、`ReloadModule()` 等入口，而不是让用户去多个子系统里找 static hook。 | `Reference/puerts/unreal/Puerts/Source/Puerts/Public/PuertsModule.h:17`-`:56` | 如果不想做统一 delegate header，至少要有一个单一 public interface 代表扩展 surface。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 新增统一的 `extension surface` 入口，把 compile/preprocess/reload/dump/debug/editor 等 public hook 收敛到一个可文档化、可演进的 façade；旧入口先保留为 shim。 |
| 具体步骤 | 1. 在 `Core/` 新增 `AngelscriptExtensionSurface.h`，集中声明 `FAngelscriptExtensionEvents` 或若干子命名空间，例如 `Compilation`、`Preprocessor`、`Reload`、`Diagnostics`。 2. 让 `FAngelscriptRuntimeModule`、`FAngelscriptPreprocessor`、`FAngelscriptClassGenerator`、`FAngelscriptStateDump` 的现有 delegate 通过 inline forwarding 或引用别名映射到新 façade，而不是直接暴露多套 owner。 3. 在统一 header 里为每个 hook 标注语义类别、运行阶段、`single-cast/multicast` 和 `Runtime/Editor` 适用域，顺手生成一份 extension surface 清单到文档或 state dump。 4. 增加一个最小宿主示例模块，只 include 统一 header 就能订阅 compile/reload/dump 三类事件，作为正式支持路径。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptExtensionSurface.h/.cpp` |
| 预估工作量 | S-M |
| 架构风险 | 风险主要在 API 迁移而非运行时行为。若过早删除旧入口，会破坏现有外部模块；因此需要至少一个过渡期，让旧 header 继续工作并输出迁移 warning。 |
| 兼容性 | 向后兼容。现有 `GetPreCompile()`、`OnProcessChunks`、`OnPostReload`、`OnDumpExtensions` 等入口继续保留，新 façade 只是提供统一入口和文档化 surface。 |
| 验证方式 | 1. 编写一个样例扩展模块，只 include 新 `AngelscriptExtensionSurface.h` 即可订阅多类 hook。 2. 复跑现有 compile/reload/state-dump 流程，确认 legacy API 与新 façade 同时可用。 3. 人工检查生成的 extension surface 清单，确认 public hook 不再需要跨多个 header 手工拼接。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-EP12 | `settings` 只能调内建行为，不能声明扩展实现 | 配置驱动扩展面 | 高 |
| P2 | Arch-EP13 | public hook 分散在多个 owner，扩展入口不可发现 | surface 收敛 | 中高 |

---

## 架构分析 (2026-04-08 16:06)

### Arch-EP14：对外扩展边界仍是 `internal include tree`，不是收口后的 public contract

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 外部宿主模块扩展插件时，依赖的是稳定 public API，还是整个 `AngelscriptRuntime` 内部目录与 editor 侧转发能力 |
| 当前设计 | 现状的“可扩展”很大程度上来自 `Build.cs` 直接把 `ModuleDirectory`、`Core` 和 `ThirdParty/angelscript/source` 暴露给外部模块；同时 `FAngelscriptRuntimeModule` 这个 runtime header 里又混入了 `GetEditorCreateBlueprint()`、`GetEditorGetCreateBlueprintDefaultAssetPath()` 等 editor 语义 hook。可以推断，当前 external extension ABI 实际上是“整个 internal tree + 一部分 editor/runtime 混合入口”，而不是经过收口的 public extension surface。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs:15`-`:22` 直接公开 `ModuleDirectory`、`Core` 和 `ThirdParty/angelscript/source`；同文件 `:67`-`:78` 在 editor 构建下把 `UnrealEd`、`EditorSubsystem` 放进 `PublicDependencyModuleNames`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:11`-`:15` 直接依赖 `AngelscriptInclude.h`、`AngelscriptType.h`、`StaticJIT/StaticJITBinds.h` 等内部头，再在 `:438`-`:475` 与 `:606`-`:611` 对外公开 `FBind` 和 `BindGlobalFunction()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:71`-`:82`、`:96`-`:342` 直接把 register/type system vtable 暴露给外部；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h:19`-`:21`、`:45`-`:47` 在 runtime 模块 public header 中暴露 `DebugListAssets` 和 blueprint 创建相关 hook。 |
| 优点 | 对插件内部开发者和同仓库模块来说非常方便，几乎不需要额外 façade 就能接入 bind、type、compile、debug 能力。 |
| 不足 | 对真正的插件使用者不友好：外部模块必须知道 `Core/*.h`、甚至 third-party source header 的内部布局；editor 语义又通过 runtime header 外溢，导致 public boundary 过宽、升级面不清晰，也不利于把 shipping-safe 的 runtime 扩展和 editor-only 的工具扩展分开演进。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 公开 contract 明确落在 `Public/` 目录：`FUnLuaDelegates` 暴露 runtime hook，`UUnLuaSettings` 暴露 typed settings，`IUnLuaInterface` 暴露对象级脚本入口；扩展者不需要 include 插件内部实现树。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDelegates.h:22`-`:50`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaSettings.h:23`-`:57`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaInterface.h:23`-`:39` | 先把“正式支持哪些扩展入口”做成收口后的 public headers，再让内部模块自由演进。 |
| puerts | 把 public contract 拆成清晰的 runtime 接口：`IPuertsModule`、`IJSModuleLoader`、`FJsEnv`、`UExtensionMethods` 都位于 `Public/`；editor 特有逻辑只在 `#if WITH_EDITOR` 下额外暴露，如 `IsInPIE()`。 | `Reference/puerts/unreal/Puerts/Source/Puerts/Public/PuertsModule.h:17`-`:56`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSModuleLoader.h:17`-`:50`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JsEnv.h:61`-`:101`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Public/ExtensionMethods.h:18`-`:20` | public surface 可以同时支持 runtime extensibility 和 editor 条件编译，但前提是边界先被模块化、命名化。 |
| UnrealCSharp | `FUnrealCSharpCoreModuleDelegates` 和 `UUnrealCSharpEditorSetting` 都通过 `Public/` 暴露；编译相关 hook 还明确放在 `#if WITH_EDITOR` 内，避免 editor 语义无条件渗出到 runtime contract。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Delegate/FUnrealCSharpCoreModuleDelegates.h:7`-`:37`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpEditorSetting.h:32`-`:45`、`:139`-`:148` | 公开面不需要很大，但要稳定、聚焦，并显式区分 runtime 与 editor domain。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在不破坏现有 `Core/*.h` 扩展路径的前提下，补一层真正的 `Public/Extension` contract，并把 editor-only hook 从 runtime surface 中拆出来。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/` 新增窄接口头，例如 `AngelscriptExtensionBinds.h`、`AngelscriptExtensionTypes.h`、`AngelscriptExtensionEvents.h`，只暴露外部确实需要的 API。 2. 将 `AngelscriptRuntime.Build.cs` 的 `PublicIncludePaths` 从 `ModuleDirectory/Core/ThirdParty` 收口到 `Public`；`Core` 与 third-party source 改为 private。 3. 对 `GetDebugListAssets()`、`GetEditorCreateBlueprint()`、`GetEditorGetCreateBlueprintDefaultAssetPath()` 这类 editor 语义入口，迁移到 `AngelscriptEditor` 的 public interface，或至少在 runtime public header 中加 `#if WITH_EDITOR` 包裹和清晰注释。 4. 保留现有 `Core/*.h` 入口一个过渡期，改成 forwarding shim 并输出 deprecation warning，避免立即打断现有宿主模块。 5. 增加一个 black-box 示例扩展模块，只 include 新 public headers 即可注册全局函数、类型贡献者和 compile observer，用来约束后续 ABI 演进。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/*` |
| 预估工作量 | M |
| 架构风险 | 最大风险是外部模块可能已经直接 include `Core/*.h` 或依赖 transitive `UnrealEd`；因此必须先做 façade + shim，再逐步收口 include path。 |
| 兼容性 | 向后兼容，但有迁移压力。旧 include 路径和旧 runtime header 先继续工作；新 public contract 作为推荐路径，后续再逐步淘汰 internal include。 |
| 验证方式 | 1. 新建一个独立宿主模块，只 include `Public/Extension` 头并完成全局函数/事件订阅注册，验证不需要再 include `Core/*.h`。 2. 在 editor target 与非 editor target 各编译一次，确认 runtime 扩展模块不会因为 public surface 牵出 `UnrealEd` 依赖。 3. 复跑现有 bind/type/compile 回归，确认 forwarding shim 不改变原有行为。 |

### Arch-EP15：`FAngelscriptType` 是 expert-mode vtable，自定义类型绑定缺少渐进式 authoring model

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 外部用户为插件添加自定义类型绑定时，拿到的是“常见场景可用的分层接口”，还是一套面向内核作者的底层 type runtime |
| 当前设计 | 当前公开的类型扩展面本质上有两层：先用 `RegisterTypeFinder()` 把 `FProperty` 映射到某个类型，再用 `Register()` 提供完整 `FAngelscriptType` 实现。但 `FAngelscriptType` 自身是一套覆盖 property 创建、GC、copy/construct/destruct、参数 marshalling、默认值、debugger、JIT、排序比较等职责的广义 vtable。结果是“理论上很强”，但对外 authoring model 偏 expert-only。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:71`-`:82` 只提供 `Register()` 与 `RegisterTypeFinder()` 两个总入口；同文件 `:96`-`:342` 暴露了大量 `virtual` 能力，包括 `CreateProperty()`、`EmitReferenceInfo()`、`SetArgument()`、`GetReturnValue()`、`GetDebuggerScope()`、`GetCppForm()` 等；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:114`-`:118` 说明 `RegisterTypeFinder()` 只是追加 finder，`:147`-`:167` 说明命中后仍依赖完整 type object 去承担后续语义；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TOptional.cpp:18`-`:242` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UStruct.cpp:34`-`:259` 展示了一个实际 type binding 往往要实现 copy、construct、property、GC、debugger 等多类职责。 |
| 优点 | 这套底层 contract 的表达力很高，复杂容器、script struct、JIT 友好类型和特殊 debugger 语义都能被精确描述。 |
| 不足 | 常见扩展场景的门槛过高。对多数宿主项目来说，需求通常只是“暴露一个 `UStruct/UClass`”“给现有类型挂几个辅助方法”“调整少数 copy/hash/debug 行为”，但当前路径要求扩展者直接进入内核级 type runtime，理解过多 GC、marshalling、`FProperty` 和 debug 细节。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 用户新增扩展方法时，只需继承 `UExtensionMethods` 并写 `static UFunction`；运行时通过扫描首参数类型建立 `ExtensionMethodsMap`，随后在 `StructWrapper` 上自动挂接到目标 `Struct/Object`。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/ExtensionMethods.h:18`-`:20`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:931`-`:983`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:340`-`:356` | 对常见“给已有类型加能力”的场景，不要求用户实现一整套底层 type vtable。 |
| UnLua | 用户选择要绑定哪些类型时，主要通过 `PreBindClasses` 配置和 `EnvLocator`/`TryBind()` 路径接入；宿主是在声明“哪些类参与绑定”，不是手写每个类型的底层内存语义。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaSettings.h:54`-`:56`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaModule.cpp:103`-`:128` | 对大多数项目，先提供高层“选择/声明/附加方法”模式，比直接暴露低层 runtime contract 更友好。 |
| UnrealCSharp | 自定义绑定范围主要通过 `bEnableExport`、`ExportModule`、`ClassBlacklist` 这类声明式 settings 控制，由 generator 负责底层桥接代码。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpEditorSetting.h:139`-`:148` | 当底层桥接足够复杂时，可以把常见用户诉求提升成 manifest/generator 层，而不是要求项目侧手写 runtime type internals。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 保留 `FAngelscriptType` 作为 expert escape hatch，但在其上补两层更窄的 authoring model：`reflected type contributor` 和 `extension methods`。 |
| 具体步骤 | 1. 在 `Core/` 新增 `FAngelscriptReflectedTypeDescriptor` / `IAngelscriptReflectedTypeContributor`，让常见 `UClass`、`UStruct`、`UEnum` 暴露只需声明目标类型、名称和少量 trait。 2. 引入可选的 `FAngelscriptTypeTraits`，只在需要特殊 copy/hash/debug/GC 语义时局部覆盖，默认由 reflection 和现有 helper 自动推导。 3. 新增类似 `UExtensionMethods` 的 `UAngelscriptExtensionMethods` 或 `IAngelscriptMethodContributor`，允许用户通过“首参数是目标类型”的 `static UFunction` 给已有绑定类型追加方法，而不必注册一个全新的 raw type。 4. 让 `RegisterTypeFinder()` 支持返回 descriptor/handle，而不是只返回 `bool + Usage`；内部再由 framework 决定走默认 reflected provider 还是 raw `FAngelscriptType`。 5. 保留现有 `Register()` / `RegisterTypeFinder()` / raw subclass 路径不动，只把新的分层接口先用于外部扩展和简单 built-in type 的增量迁移。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptTypeContributor.h/.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptExtensionMethods.h/.cpp` |
| 预估工作量 | M-L |
| 架构风险 | 默认 trait 推导很容易先覆盖简单类型、遗漏复杂容器或 script-only struct；因此必须把 raw `FAngelscriptType` 保留为 fallback，而不是试图一次性替换全部 built-in type。 |
| 兼容性 | 向后兼容。现有 `FAngelscriptType` 子类、`Register()` 和 `RegisterTypeFinder()` 继续有效；新接口只是补一个低门槛层级，让未来扩展优先走高层 contract。 |
| 验证方式 | 1. 新建一个宿主测试模块，只用新 contributor API 暴露一个 `UStruct` 和一个附加 `static UFunction`，验证无需手写 raw type subclass 也能完成绑定。 2. 再补一个需要自定义 copy/debug 语义的测试类型，验证 trait 覆盖与 raw fallback 可以共存。 3. 复跑现有 `FAngelscriptType` 内建绑定回归，确认新分层接口不改变已有 built-in type 的行为。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-EP15 | 自定义类型绑定 authoring model 过于底层，常见场景缺少渐进式接口 | 分层扩展模型 | 高 |
| P2 | Arch-EP14 | 对外扩展 ABI 仍靠 internal include tree 和 runtime/editor 混合 surface 暴露 | public contract 收口 | 中高 |

---

## 架构分析 (2026-04-08 16:24)

### Arch-EP16：自定义函数扩展并不只依赖 `BindGlobalFunction()`，当前其实存在一条隐藏的 `UFUNCTION` 反射扩展面

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 脚本开发者想添加自定义全局函数、namespaced function 或 mixin method 时，是否一定要写底层 binder / 修改插件源码 |
| 当前设计 | 重新核对 `Bind_BlueprintType.cpp` 与 `Bind_BlueprintCallable.cpp` 后可以确认：当前插件除了 `FAngelscriptBinds::BindGlobalFunction()` 这条显式 C++ 注册路径，还会在绑定阶段扫描 `TObjectRange<UClass>()`，把任意类上的 `BlueprintCallable` / `BlueprintPure` / `ScriptCallable` 函数自动送进 Angelscript；`ScriptName`、`ScriptMixin`、`ScriptGlobalScope` metadata 进一步决定它被绑定成 namespaced function、成员 mixin 还是额外的 global function。也就是说，项目侧新增一个 `UObject`/`UBlueprintFunctionLibrary` 风格的 C++ class，就已经具备“不改插件源码扩展函数面”的能力。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1366`-`:1406` 对全部 `UClass` 做扫描，并把 `FUNC_BlueprintCallable` / `FUNC_BlueprintPure` / `ScriptCallable` 函数送入 `BindBlueprintCallable()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h:270`-`:314` 读取 `ScriptGlobalScope`、`ScriptMixin`、`ScriptName` 来决定 `bGlobalScope`、`bStaticInScript` 与 mixin 注入；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp:101`-`:126` 对 static function 同时支持 namespace bind 与 global-scope bind；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp:105`-`:132`、`:374`-`:410` 说明即使没有 direct native pointer，仍可走 reflective fallback 完成绑定；`Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptActorLibrary.h:5`-`:14` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptMathLibrary.h:6`-`:15` 展示了仓内已经在使用 `ScriptMixin` 与 `ScriptName` 这套 metadata authoring model。 |
| 优点 | 对项目方来说，这条 seam 比手写 `BindGlobalFunction()` 轻得多：很多函数扩展可以直接落在普通 `UFUNCTION` authoring 模式里，尤其适合 namespaced utility 和 mixin helper。 |
| 不足 | 这条能力目前是“实现里存在、产品面上隐身”的状态。扩展者必须知道 binder 会扫描全部 `UClass`、理解 `ScriptMixin` / `ScriptGlobalScope` / `ScriptName` 的组合语义，还要自己猜函数最终走 direct bind 还是 reflective fallback。对“true global function 可否只靠 metadata 暴露”这一点，源码实现明确支持，但由于仓内没有 first-party sample 或 contract test，这部分属于基于实现的推断。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 把“给已有类型追加方法”的 authoring model 收敛成 `UExtensionMethods` 基类；`InitExtensionMethodsMap()` 只扫描 `UExtensionMethods` 的 native 子类，再按首参数类型把 `static UFunction` 挂到目标 `Struct/Object`。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/ExtensionMethods.h:18`-`:20`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:940`-`:980` | 当前插件已经有类似能力，但缺的是显式 base class / public contract，让用户不必反推 binder 内部规则。 |
| UnLua | 不把“哪些类会被纳入绑定”交给隐式全局扫描，而是通过 `PreBindClasses` 明确声明要预绑定的 class family，模块启动时再按清单执行 `TryBind()`。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaSettings.h:54`-`:56`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaSettings.cpp:19`-`:24`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaModule.cpp:110`-`:125` | 当扩展面来自 UE 反射类时，先把 author intent 显式化，比“扫到就算支持”更可维护。 |
| UnrealCSharp | 把是否导出绑定、导出哪些模块和排除哪些类做成 `bEnableExport`、`ExportModule`、`ClassBlacklist` 这类 settings manifest，再由 generator 负责具体桥接。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpEditorSetting.h:139`-`:148` | 即使继续保留自动发现，也应该有一个显式 manifest 告诉用户“哪些扩展会进入正式脚本 API”。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把现有的 metadata 反射扩展面正式产品化：保留自动绑定兼容路径，但新增明确的 extension-library contract、诊断和示例。 |
| 具体步骤 | 1. 在 `Public/Extension/` 新增 `UAngelscriptExtensionLibraryBase` 与 `UAngelscriptMixinLibraryBase`，把 `ScriptName`、`ScriptMixin`、`ScriptGlobalScope` 定义成正式支持的 metadata 组合，而不是只靠 `Helper_FunctionSignature.h` 隐式解释。 2. 在 `Bind_BlueprintType.cpp` 的扫描路径里优先识别新 base class，并为命中的函数记录 `BindSource = DirectNative / ReflectiveFallback`、`BindingKind = Global / Namespace / Mixin`。 3. 在 `Dump/AngelscriptStateDump.cpp` 增加一张 `ReflectiveExtensionLibraries` 表，输出库类、函数名、最终脚本名、是否走 fallback，便于用户确认扩展是否按预期生效。 4. 增加一个最小宿主样例和自动化测试，分别覆盖 `ScriptName` namespace、自定义 mixin，以及基于 `ScriptGlobalScope` 的 global function，避免这条 seam 继续停留在“看代码才知道能用”。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/AngelscriptExtensionLibrary.h`、新增宿主样例/测试文件 |
| 预估工作量 | M |
| 架构风险 | 如果直接把扫描规则从“任何 `UClass`”改成“只认新 base class”，会打断现有项目里依赖 ambient scan 的 BlueprintCallable 扩展；因此第一阶段只能先补 contract 和 diagnostics，不能立刻收紧兼容规则。 |
| 兼容性 | 向后兼容。现有 `BlueprintCallable` / `BlueprintPure` / `ScriptCallable` 自动绑定继续可用；新 base class、dump 和测试只是把现有隐式 seam 显式化。 |
| 验证方式 | 1. 在宿主项目新增一个 `UAngelscriptExtensionLibraryBase` 子类，验证不改插件源码即可新增 namespaced function。 2. 再新增一个 `UAngelscriptMixinLibraryBase` 子类，验证首参目标类型会被正确折叠成成员方法。 3. 新增一个 `ScriptGlobalScope` 测试函数，验证能同时在 global scope 与 namespace 下被发现，且 `StateDump` 能显示它走的是 direct bind 还是 reflective fallback。 |

### Arch-EP17：当前反射式扩展发现是“扫全局 `UClass` + 全局命名裁剪”，扩展意图和 API 边界都不显式

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 用户通过 `UFUNCTION` / metadata 扩展脚本 API 时，插件如何决定“哪些类参与发现、最终暴露成什么名字、哪些暴露是项目作者有意为之” |
| 当前设计 | 现有反射式函数扩展本质上是 ambient discovery：`Bind_BlueprintType.cpp` 直接遍历全部 `UClass`，任何类只要满足 `BlueprintCallable` / `BlueprintPure` / `ScriptCallable` 条件就可能进入脚本面；最终 namespace 又由 `ScriptName` 或 class name，再叠加 `BlueprintLibraryNamespacePrefixesToStrip` / `BlueprintLibraryNamespaceSuffixesToStrip` 这套全局规则裁剪得出。结果是扩展 API 的边界同时受“工程里有哪些类”和“全局命名规则如何配置”影响，而不是由一个显式 manifest 或 opt-in base class 控制。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1366`-`:1406` 对 `TObjectRange<UClass>()` 做全局扫描，并对每个类的函数按 flags/meta 决定是否绑定；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h:123`-`:175` 用 `ScriptName` 或类型名生成 namespace，再按 `BlueprintLibraryNamespacePrefixesToStrip` / `BlueprintLibraryNamespaceSuffixesToStrip` 做全局裁剪；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h:100`-`:129` 把这些命名裁剪规则暴露成全局配置；结合 `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptMathLibrary.h:6`-`:15` 可以看到，库类最终名字并不完全来自 class 本名，而是来自 metadata + 全局规则叠加。 |
| 优点 | 对插件内部和单项目使用来说非常省事，新增一个 BlueprintCallable library 往往就能自然进入脚本面，几乎没有额外样板。 |
| 不足 | 这套机制把“是否应该暴露给脚本”变成了推断题。项目新增一个工具类、改一个 library 名称、调整一条 prefix/suffix 配置，都可能让脚本 API surface 发生扩张或重命名；而扩展作者和项目维护者都缺少一个显式地方去声明“这些是正式支持的扩展库”。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 虽然也扫描 `UClass`，但只接受 `UExtensionMethods` 子类，而且要求函数是 static、首参数能映射到目标 `Struct/Object`；扫描边界比当前插件更窄，也更可解释。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/ExtensionMethods.h:18`-`:20`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:940`-`:977` | 发现机制可以继续自动化，但 class family 至少要是 opt-in，而不是把整个工程类集都纳入候选池。 |
| UnLua | `PreBindClasses` 不是“扫到了就绑定”，而是明确声明哪些 class family 在 startup 时会进入 `TryBind()`；默认值甚至写在 settings ctor 里，便于用户理解系统默认暴露范围。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaSettings.cpp:19`-`:24`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaModule.cpp:110`-`:125` | 默认暴露范围应当可见、可改、可解释，而不是埋在 binder 扫描逻辑里。 |
| UnrealCSharp | 不靠环境里“出现了什么类”来决定导出边界，而是通过 `ExportModule` 与 `ClassBlacklist` 把发现范围做成 generator manifest。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpEditorSetting.h:139`-`:148` | 对大型项目，脚本 API 边界最好是 manifest/allowlist 问题，而不是命名碰撞或全局扫描副作用。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 保留 legacy global scan 兼容模式，但为反射式函数扩展补一个显式 discovery contract，让“哪些类参与暴露”从隐式环境副作用变成可配置、可审计的决定。 |
| 具体步骤 | 1. 在 `UAngelscriptSettings` 中新增 `ReflectiveExtensionDiscoveryMode`，例如 `LegacyScanAllClasses`、`OnlyExtensionLibraryBase`、`AllowlistModules` 三种模式，默认先保持 legacy。 2. 新增 `AllowedExtensionModules` / `AllowedExtensionBaseClasses` / `DeniedExtensionClasses` 之类的轻量 manifest，优先只做日志和 dump，不立刻改变默认行为。 3. 在 `Bind_BlueprintType.cpp` 里把 discovery decision 拆成独立 helper，命中时记录 `WhyExposed`，例如 `BlueprintCallable+LegacyScan`、`ExtensionLibraryBase+Allowlist`、`ScriptCallable+ExplicitMetadata`。 4. 在 `StateDump` 和编译日志里输出“反射式暴露清单”和潜在风险，如“该类仅因 legacy global scan 被暴露”“该函数名在 namespace 裁剪后发生碰撞”。 5. 等 manifest/dump 跑稳后，再提供项目级迁移开关，让新项目默认走 opt-in 模式，旧项目继续保留 legacy 扫描。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/AngelscriptReflectiveDiscovery.h/.cpp` |
| 预估工作量 | M |
| 架构风险 | 任何收紧发现范围的动作都可能暴露旧项目对 ambient scan 的隐式依赖；因此必须先做 `discoverability + diagnostics`，再允许项目主动切换到 opt-in 模式，而不是直接改变默认扫描规则。 |
| 兼容性 | 向后兼容。默认仍可保持 `LegacyScanAllClasses`；新的 allowlist/base-class 模式只作为项目可选项，旧项目不需要立即迁移。 |
| 验证方式 | 1. 构造一个包含多个 `BlueprintCallable` utility class 的宿主项目，验证 `StateDump` 能清楚列出每个类为什么进入脚本面。 2. 开启 `OnlyExtensionLibraryBase` 后，确认普通 utility class 不再被意外暴露，而显式扩展库仍能工作。 3. 增加一个 namespace 冲突测试，验证 prefix/suffix 裁剪导致的同名暴露会被日志或 dump 标出来。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-EP17 | 反射式函数扩展目前依赖全局 `UClass` 扫描与全局命名裁剪，API 边界不显式 | 发现机制收口 | 高 |
| P1 | Arch-EP16 | `UFUNCTION` metadata 已构成隐藏的自定义函数扩展面，但缺少正式 contract、示例与诊断 | 扩展面显式化 | 高 |

---

## 架构分析 (2026-04-08 16:36)

### Arch-EP18：脚本层 delegate 已是一等公民，但插件生命周期事件仍停留在 `C++ only`

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 纯 `Angelscript` 作者能否不写宿主 `C++ module`，直接订阅编译、预处理、热重载这些插件生命周期事件 |
| 当前设计 | 当前插件其实已经把 UE delegate 语义完整映射到了 `Angelscript`：`Bind_Delegates.cpp` 为 `FScriptDelegate` / `FMulticastScriptDelegate` 暴露了 `BindUFunction()`、`AddUFunction()`、`Unbind()`、`Clear()` 等脚本可调用能力；但 compile/reload/preprocess 这些真正的插件扩展事件仍然只以 `DECLARE_MULTICAST_DELEGATE` / `DECLARE_DELEGATE` 的 static `C++` carrier 存在，没有被投射成脚本可见对象。结果是语言层并不缺 delegate 能力，缺的是“把插件生命周期事件桥接进语言层”的最后一步。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.cpp:615`-`:630` 为单播 delegate 暴露 `IsBound`、`Clear`、`BindUFunction`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.cpp:1113`-`:1120` 为多播 delegate 暴露 `AddUFunction`、`Unbind`、`UnbindObject`；反过来，插件生命周期 hook 仍定义在 `C++` static carrier 中：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h:9`-`:21`、`:32`-`:47` 定义 `PreCompile` / `PostCompile` / `PreGenerateClasses` / `ClassAnalyze` 等 hook，`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h:10`-`:19`、`:28`-`:38` 定义 `OnClassReload` / `OnPostReload` / `OnLiteralAssetReload`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h:8`、`:29`-`:30` 定义 `OnProcessChunks` / `OnPostProcessCode`。这些都不是 `UObject` property，也没有对应的 script-facing wrapper。 |
| 优点 | 对宿主 `C++/Editor module` 来说接入很直接，也保持了插件内部实现的低开销；同时现有 delegate 绑定系统已经证明语言层可以承载 callback。 |
| 不足 | “订阅编译/热重载事件”目前仍是 `C++` 扩展者专属能力。纯 `.as` 作者虽然能在脚本里操作 gameplay/UE delegate，却无法观察插件自己的 compile/reload/preprocess 生命周期，这让插件扩展点在作者体验上出现明显断层。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 一方面用 `FUnLuaDelegates` 集中承载 `OnLuaStateCreated`、`OnObjectBinded`、`CustomLoadLuaFile` 等运行时 hook；另一方面又在 `LuaLib_Delegate.cpp` 与 `LuaLib_MulticastDelegate.cpp` 把 `Bind` / `Unbind` / `Execute` / `Add` / `Remove` / `Clear` / `Broadcast` 直接暴露给 Lua。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDelegates.h:22`-`:49`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/BaseLib/LuaLib_Delegate.cpp:21`-`:88`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/BaseLib/LuaLib_MulticastDelegate.cpp:132`-`:142` | 参考价值不在“把所有 hook 都变成脚本 API”，而在形成“两层模型”：上层有正式 lifecycle hook，下层有稳定的语言侧 delegate bridge。 |
| puerts | `DelegateWrapper.cpp` 把 `Bind`、`Unbind`、`Execute`、`Add`、`Remove`、`Clear`、`Broadcast` 暴露给 JS/TS，对应的实际绑定与解绑由 `FJsEnvImpl::AddToDelegate()` / `RemoveFromDelegate()` / `ClearDelegate()` 维护。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DelegateWrapper.cpp:45`-`:88`、`:98`-`:205`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:2402`-`:2703` | 说明“把 UE / 插件事件桥接进脚本层”可以作为独立 runtime service 来设计，不必和编译策略或类型系统耦死。 |
| UnLua | `UnLuaSettings` 还把 `EnvLocatorClass`、`ModuleLocatorClass`、`PreBindClasses` 做成 manifest，使“哪些对象/类会进入脚本生态”是显式可配置的。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaSettings.h:49`-`:56` | 如果要把插件生命周期事件桥接到脚本层，最好也用显式 service / settings 暴露，而不是让脚本去摸 static 全局。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 保留现有 `C++` delegate 作为 source of truth，再补一层只读的 script-facing lifecycle bridge，让脚本作者可以观察而不是篡改插件生命周期。 |
| 具体步骤 | 1. 在 `AngelscriptRuntime/Public/Extension/` 新增 `UAngelscriptLifecycleEvents` 或 `FAngelscriptScriptLifecycleBridge`，以 singleton service 形式暴露 `OnPreCompile`、`OnPostCompile`、`OnPreprocess`、`OnPostReload` 等 observe-only 事件。 2. 该 bridge 内部持有对 `FAngelscriptRuntimeModule`、`FAngelscriptClassGenerator`、`FAngelscriptPreprocessor` 现有 static delegate 的转发订阅，并把 payload 收口成 `FAngelscriptCompileEventContext`、`FAngelscriptReloadEventContext` 这类只读结构。 3. 给 `Bind_Delegates.cpp` 或单独的 `Bind_LifecycleEvents.cpp` 增加对该 service 的绑定，让 `.as` 可以像操作普通 delegate 一样绑定 callback，但不能直接替换 strategy provider。 4. 第一阶段只桥接 observer 事件，不桥接 `GetDynamicSpawnLevel`、`GetDebugCheckBreakOptions` 这类 strategy hook，避免把脚本层变成新的核心策略入口。 5. 在 editor-only 环境增加一个最小示例脚本，验证脚本可以收到 compile / reload 通知并安全解绑。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/AngelscriptLifecycleEvents.h/.cpp`、可选新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_LifecycleEvents.cpp` |
| 预估工作量 | M |
| 架构风险 | 如果把 strategy 型 hook 也一并桥接到脚本层，会立刻引入时序、权限和多引擎一致性问题；因此第一阶段必须明确限制为 observe-only bridge。 |
| 兼容性 | 向后兼容。现有 `C++` 扩展模块继续直接绑定旧 delegate；新的 script bridge 只是附加能力，不改变旧广播时序。 |
| 验证方式 | 1. 新增 editor 自动化测试或样例脚本，验证 `.as` 可以订阅 `OnPreCompile` 与 `OnPostReload` 并收到一次且仅一次通知。 2. 执行解绑后再次触发编译，确认 callback 不再被调用。 3. 复跑现有 `C++` 扩展订阅路径，确认 bridge 的内部转发不会改变旧 delegate 的顺序与次数。 |

### Arch-EP19：函数扩展的符号空间依赖隐式改名与静默去重，扩展冲突难以诊断

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 多个扩展库同时追加 `global function`、namespace function 或 mixin method 时，插件如何处理重名、前缀裁剪和重复声明 |
| 当前设计 | 当前函数扩展的符号规则主要是启发式处理，不是显式 contract。`Helper_FunctionSignature.h` 会先按 metadata 或约定裁剪函数名与 namespace：自动移除 `K2_`、`BP_`、`AS_`、`Received_`、`Receive` 前缀，再按 settings 裁剪 namespace prefix/suffix；真正绑定前，`IsScriptDeclarationAlreadyBound()` 又会在 global scope 或 type method 上按“同名”或“同声明”做静默去重，命中后直接跳过绑定。结果是扩展冲突通常不会有明确诊断，扩展者只能从“为什么这个函数没出现”反推冲突来源。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h:85`-`:120` 会在无 `ScriptName` metadata 时自动裁剪 `K2_` / `BP_` / `AS_` / `Received_` / `Receive`，若裁剪后与同类 callable function 冲突又退回原名；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h:123`-`:175` 会再按 `BlueprintLibraryNamespacePrefixesToStrip` / `BlueprintLibraryNamespaceSuffixesToStrip` 与 `ScriptName` 生成 namespace；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h:273`-`:278` 把 `ScriptGlobalScope`、`ScriptMixin` 与 namespace 组合成最终绑定形态；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp:62`-`:70` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp:174`-`:240`、`:392`-`:396` 在检测到已有全局函数或方法时直接 `return`，没有来源模块、冲突类型或最终胜出者的显式记录。 |
| 优点 | 对单插件、单项目、单 author 的场景很省心，很多 BlueprintCallable library 不写任何 metadata 也能得到比较自然的脚本名。 |
| 不足 | 一旦进入“多个扩展贡献者共享同一脚本符号空间”的场景，规则就变得不透明：有的冲突通过改名规避，有的通过静默跳过处理，有的受 prefix/suffix 配置影响才发生。扩展者很难判断冲突是来自 `ScriptName`、namespace strip、`ScriptGlobalScope`，还是 reflective fallback 的重复声明检查。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 用 `UExtensionMethods` 作为 opt-in base class，仅扫描这类 native class；`InitExtensionMethodsMap()` 按首参数类型把扩展方法分组到目标 `UStruct`；`StructWrapper.cpp` 再用 `AddedMethods` 集合按目标 type 去重，冲突范围被限制在单个 wrapper 内，而不是整个工程的 ambient symbol space。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/ExtensionMethods.h:19`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:931`-`:983`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:187`-`:197`、`:261`-`:351` | 关键可借鉴点是“先缩小候选集，再在局部 wrapper 内做显式去重”，而不是先全局扫描、再靠启发式重命名。 |
| UnLua | `EnvLocatorClass`、`ModuleLocatorClass`、`PreBindClasses` 让“谁参与绑定”先通过 manifest 和 locator 决定，而不是先把所有候选函数扔进同一符号池。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaSettings.h:49`-`:56`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaModule.cpp:104`-`:112` | 即便不是专门的“重名冲突系统”，也说明先显式限定发现范围，可以显著减少后续命名碰撞与静默跳过。 |
| puerts | JS delegate / method wrapper 里对扩展和反射方法统一维护 `AddedMethods`，重复项会在构造 wrapper 时被明确拦下，而不是等运行时某个 fallback bind 路径默默失败。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:261`-`:351` | 去重应当尽可能局部、显式、可观察，而不是通过多层命名修正规则隐式达成。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把“最终暴露到脚本里的符号”先产品化成 manifest，再在 manifest 层处理冲突、告警与兼容，而不是继续让 `Helper_FunctionSignature` 和 reflective fallback 隐式做决定。 |
| 具体步骤 | 1. 在 `Core/` 或 `Dump/` 新增 `FAngelscriptSymbolManifestEntry`，记录 `SourceModule`、`SourceClass`、`UFunction`、`BindingKind(Global/Namespace/Mixin/Method)`、`FinalNamespace`、`FinalScriptName`、`CollisionPolicy`。 2. 在 `Bind_BlueprintCallable.cpp` / `BlueprintCallableReflectiveFallback.cpp` 里，先生成 manifest entry，再统一做冲突检测；旧逻辑可以继续作为默认 `LegacyAutoResolve` policy。 3. 当发生同名或同声明冲突时，至少输出 warning 和 `StateDump` 记录，说明是“被已有声明覆盖”“因 namespace strip 冲突被跳过”还是“自动回退到原始函数名”。 4. 为新扩展库提供 opt-in strict mode：要求 extension library 明确声明 `ScriptName` 或 base class，不允许完全依赖前缀裁剪自动命名。 5. 补充自动化测试，覆盖 `K2_`/`BP_` 前缀裁剪冲突、`ScriptGlobalScope` 与 namespace 重复、mixin method 与已有 member method 重名三类场景。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSymbolManifest.h/.cpp` |
| 预估工作量 | M |
| 架构风险 | 如果直接把 legacy 启发式改成 strict policy，旧项目里依赖自动裁剪命名的函数库会大量暴露 warning 甚至失去绑定；因此必须先增加 manifest 和诊断，再提供 opt-in strict mode。 |
| 兼容性 | 向后兼容。默认仍保留当前自动命名与静默去重行为，但开始输出可追踪的 warning / dump；只有主动启用 strict mode 的项目才收紧规则。 |
| 验证方式 | 1. 构造两个扩展库，分别通过 `K2_` 裁剪和 `ScriptName` 生成同一脚本名，验证日志与 `StateDump` 能明确指出冲突来源与最终胜出项。 2. 增加一个 `ScriptGlobalScope + namespace` 双暴露案例，验证重复声明不会再静默消失。 3. 复跑现有 reflective fallback 回归，确认在未启用 strict mode 时旧项目行为不变。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-EP18 | 脚本层已有 delegate 能力，但插件生命周期事件仍是 `C++ only` | script-facing observer bridge | 高 |
| P1 | Arch-EP19 | 函数扩展的符号冲突依赖隐式改名与静默去重，缺少可诊断 contract | symbol manifest + conflict diagnostics | 高 |

---

## 架构分析 (2026-04-08 16:49)

### Arch-EP20：`import` / module resolution 仍是内建分支，不是可替换的 resolver contract

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 脚本开发者是否能在不改插件源码的前提下，替换 `import` 解析、模块名映射和脚本源码加载策略 |
| 当前设计 | 当前 `import` 行为本质上只有一个 policy bit：`UAngelscriptSettings` 提供 `bAutomaticImports` 和 `bWarnOnManualImportStatements`，`FAngelscriptEngine` 在初始化时把它拷到 `bUseAutomaticImportMethod`，再决定是否打开 `asEP_AUTOMATIC_IMPORTS`。`FAngelscriptPreprocessor` 里没有独立的 resolver/provider 接口，只是在 `Preprocess()` 早期走 `ProcessImports()` 对显式 import 做拓扑排序，或在 automatic mode 下直接把 import 语句抹掉并给 warning。后续公开 hook `OnProcessChunks` / `OnPostProcessCode` 已经晚于 import 决策，`GetClassAnalyze()` 也只是对生成 statics 代码做单播修改，不能介入模块发现与解析。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h:59`-`:67` 只暴露 `DisabledBindNames`、`bAutomaticImports`、`bWarnOnManualImportStatements` 这类开关；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:64`-`:95` 显示 `FAngelscriptEngineConfig` 只有运行参数、`DisabledBindNames`，`FAngelscriptEngineDependencies` 只覆盖文件系统/插件脚本根发现；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:702`-`:709`、`:905`-`:907`、`:1291`-`:1296` 说明 automatic-import 只是当前 engine 的布尔状态和 AngelScript engine property；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:232`-`:237`、`:439`-`:497` 显示 import 解析完全写死在 `ProcessImports()`；同文件 `:265`-`:287` 的 `OnProcessChunks` / `OnPostProcessCode` 已发生在 import 排序之后，`:1310`-`:1311` 的 `GetClassAnalyze()` 只接收 `GeneratedStatics`、`ClassDesc`、`bHasStatics`。 |
| 优点 | 当前链路短、行为可预测，automatic/manual 两套历史模式都容易维护，也利于热重载和预编译缓存保持确定性。 |
| 不足 | 对项目方来说，可扩展面几乎停留在“开关现有行为”。想做 alias import、按插件/包重写模块名、远程或虚拟文件系统加载、按 world 或 engine instance 切换脚本根，今天都没有正式 contract，只能改 `Preprocessor` / `Engine` 源码。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 用 `UUnLuaSettings` 直接暴露 `EnvLocatorClass`、`ModuleLocatorClass`、`PreBindClasses`；`ULuaModuleLocator` / `ULuaEnvLocator` 是可子类化的 `UObject` strategy，`FLuaEnv` 启动时拿 settings 中配置的 locator；同时 `FUnLuaDelegates::CustomLoadLuaFile` 还允许项目接管 Lua 文件加载。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaSettings.h:47`-`:56`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/LuaEnvLocator.h:22`-`:46`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/LuaModuleLocator.h:21`-`:33`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaModule.cpp:103`-`:106`、`:112`-`:125`、`:152`-`:156`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:77`-`:79`、`:560`-`:566` | loader / locator / bind-scope 都是正式 public contract，而不是只能切换一个 bool。 |
| puerts | 把模块解析抽象成 `IJSModuleLoader::Search()` / `Load()` / `GetScriptRoot()`，`FJsEnvImpl` 构造时直接注入 loader，并在正常 load 与 reload 路径都调用它；模块级扩展再通过 `IPuertsModule` 暴露 `ReloadModule()`。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSModuleLoader.h:17`-`:41`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:347`-`:349`、`:1482`-`:1500`、`:3557`-`:3560`；`Reference/puerts/unreal/Puerts/Source/Puerts/Public/PuertsModule.h:37`-`:47` | 把“找模块”“读源码”“重载模块”做成独立接口后，项目级替换实现就不需要改核心编译器。 |
| UnrealCSharp | 虽然它不是 runtime import 系统，但编译/生成的扩展至少不是裸分支：一方面通过 `FUnrealCSharpCoreModuleDelegates` 暴露 `OnBeginGenerator` / `OnEndGenerator` / `OnCompile`，另一方面用 `bEnableExport`、`ExportModule`、`ClassBlacklist` 把导出边界做成 settings manifest。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Delegate/FUnrealCSharpCoreModuleDelegates.h:14`-`:35`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpEditorSetting.h:139`-`:148` | 即便语言链路不同，也说明“编译阶段扩展”应当有正式 contract 或 manifest，而不是只靠内部 if-branch。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 保留现有 automatic/manual import 行为作为默认 resolver，但新增正式的 `module resolver + source loader` contract，让项目能按 settings 或 engine config 注入自定义实现。 |
| 具体步骤 | 1. 在 `Public/Extension/` 新增 `IAngelscriptModuleResolver` 或 `UAngelscriptModuleLocator`，最小职责先只包含 `ResolveDeclaredImport()`、`EnumerateImplicitImports()`、`LoadModuleSource()` 三类接口。 2. 在 `FAngelscriptEngineConfig` 或 `FAngelscriptEngineDependencies` 中增加 resolver factory / provider 字段，使其和现有文件系统依赖一样可以按 engine instance 注入，而不是只能走 process-global settings。 3. 在 `FAngelscriptPreprocessor::Preprocess()` 中把 `ProcessImports()` 改造成默认 resolver 的实现，legacy automatic/manual 路径继续保留，但统一通过 resolver contract 出口返回 import graph 和源码。 4. 给 `StateDump` 或编译日志增加 resolver trace，明确记录模块是通过 `LegacyAutomaticImports`、`ExplicitImportGraph` 还是 `CustomResolver` 命中。 5. 第一阶段只开放解析与加载，不开放语法树重写；后续若有需要，再把 `OnProcessChunks` 升级为 typed `PreprocessTransform` service。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/AngelscriptModuleResolver.h/.cpp` |
| 预估工作量 | M-L |
| 架构风险 | import graph 与预编译缓存、热重载判定高度相关；如果 resolver 改变模块 identity 却没有稳定 key，会破坏依赖排序和 reload 缓存。因此第一阶段必须要求 resolver 返回稳定 `ModuleName` / `SourceFingerprint`，并默认保留 legacy resolver。 |
| 兼容性 | 向后兼容。默认 resolver 继续等价于当前 `bAutomaticImports` + `ProcessImports()` 逻辑；只有显式配置 custom resolver 的项目才进入新路径。 |
| 验证方式 | 1. 增加一个最小宿主测试模块，实现 alias import resolver，把 `Gameplay/Foo` 重写到真实脚本文件，验证无需改插件源码即可成功编译。 2. 在 automatic import 与 manual import 两种 legacy 配置下复跑现有编译回归，确认结果不变。 3. 在 state dump 或编译日志中检查每个 module 的 resolver 来源，确保调试时能看出是默认路径还是自定义路径。 |

### Arch-EP21：编译 / 热重载 hook 已公开，但 payload contract 过弱且风格混杂，外部工具很难只靠公开事件完成扩展

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 外部模块订阅 compile / preprocess / reload 事件后，是否能拿到足够稳定且高层的上下文，做 IDE、缓存、诊断或自定义增量策略 |
| 当前设计 | 当前事件面“有很多 hook”，但 payload contract 不统一：`GetPreCompile()` / `GetPostCompile()` / `GetOnInitialCompileFinished()` 是零参数广播，只能告诉订阅者“发生过一次编译”；`GetPreGenerateClasses()` / `GetPostCompileClassCollection()` 会给模块列表；`OnProcessChunks` / `OnPostProcessCode` 直接把整个可变 `FAngelscriptPreprocessor&` 抛出去；`GetClassAnalyze()` 更是 single-cast 且让外部直接改 `GeneratedStatics` 字符串与 `bHasStatics`；热重载侧又混成 `OnClassReload(OldClass, NewClass)`、`OnDelegateReload(Old, New)`、`OnPostReload(bool bIsDoingFullReload)`。结果是“能订阅”不等于“有稳定 contract”：简单观察可以，复杂工具往往还得理解内部对象布局或改插件源码。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h:14`-`:18`、`:37`-`:42` 显示 compile 侧 hook 混用了 `ClassAnalyze`、零参数 compilation delegate 与带模块集合的 delegate；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h:8`、`:29`-`:30` 只把整个 `FAngelscriptPreprocessor&` 暴露给 hook；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:265`-`:287` 广播 preprocess 事件，`:1310`-`:1311` 让 `GetClassAnalyze()` 直接改生成代码；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h:12`-`:19`、`:31`-`:38` 定义 reload 系列 hook，`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2347`-`:2395`、`:3931`-`:3932` 说明实际广播既有 pairwise old/new 对象，也有仅携带 `bool` 的 `OnPostReload`。 |
| 优点 | 低成本、直接，插件内部任何阶段想给自己加 hook 都很容易；而且 C++ 宿主模块今天已经能在不改插件源码的前提下订阅这些事件。 |
| 不足 | payload contract 对外部工具不友好。零参数 hook 不知道本次编译处理了哪些文件或模块；`FAngelscriptPreprocessor&` / `GeneratedStatics` 这类 payload 又过于贴近内部实现。结果是“订阅编译/热重载事件”今天对 C++ 扩展者虽然可行，但更像 internal hook，而不是稳定 public API；纯 `.as` 作者更仍然无法直接使用。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | 生成和编译的阶段信号集中在 `FUnrealCSharpCoreModuleDelegates`，而 `OnCompile` 还显式携带 `TArray<FFileChangeData>`，使外部订阅者知道是哪些文件变化触发了本轮流程。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Delegate/FUnrealCSharpCoreModuleDelegates.h:14`-`:35`；`Reference/UnrealCSharp/Source/Compiler/Private/FCSharpCompilerRunnable.cpp:166`-`:184` | hook 不一定要很多，但要有稳定、可消费的上下文字段。 |
| UnLua | `CustomLoadLuaFile` 不是简单的“通知一下”，而是把 `FLuaEnv&`、`FileName`、`Data`、`ChunkName` 一起交给扩展者；`EnvLocator` / `ModuleLocator` 也通过 `Locate(Object)` / `HotReload()` / `Reset()` 暴露明确职责。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDelegates.h:33`-`:49`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/LuaEnvLocator.h:27`-`:31`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/LuaModuleLocator.h:25`-`:33`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:560`-`:566` | 与其暴露庞大内部对象，不如定义面向扩展任务的窄 payload。 |
| puerts | `IJSModuleLoader::Search()` / `Load()` 和 `ReloadModule(FName ModuleName, const FString& JsSource)` 都带有明确的模块名、路径和源码参数；项目扩展做 cache、watch 或 remote load 时，不需要反向理解引擎内部状态。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSModuleLoader.h:17`-`:24`；`Reference/puerts/unreal/Puerts/Source/Puerts/Public/PuertsModule.h:41`-`:45`；`Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:80`-`:118` | event / hook 的关键不是数量，而是参数语义足够高层且稳定。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在不删除旧 hook 的前提下，补一层 typed event contract，把 compile / preprocess / reload 变成可订阅、可记录、可脚本桥接的稳定上下文对象。 |
| 具体步骤 | 1. 在 `Public/Extension/` 新增 `FAngelscriptCompileEventContext`、`FAngelscriptPreprocessEventContext`、`FAngelscriptReloadEventContext`，至少包含 `CompileType`、`ChangedModules`、`ChangedFiles`、`bAutomaticImports`、`bFullReload`、`ReloadedTypes`、`DiagnosticsOutputDir` 等字段。 2. 新增统一事件总线，例如 `FAngelscriptExtensionEvents::OnCompileStarted/OnCompileFinished/OnPreprocessFinished/OnReloadFinished`，新总线使用 typed payload；旧 `GetPreCompile()` / `OnPostReload` / `OnProcessChunks` 继续保留为 forwarding shim。 3. 对 `GetClassAnalyze()` 这类 raw internal hook，增加一个更窄的替代事件，只暴露“附加 generated statics fragment”或“声明额外分析结果”，逐步把外部修改 `GeneratedStatics` 原始字符串的用法转为显式 API。 4. 在 state dump 或 debug server 中记录每次事件的上下文摘要，方便验证订阅者看到的内容稳定且完整。 5. 第二阶段再考虑把 typed observer bridge 映射到脚本层，先解决 C++ 扩展者拿不到高质量 payload 的问题。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/AngelscriptCompileEvents.h/.cpp` |
| 预估工作量 | M |
| 架构风险 | 最大风险是新旧事件并存期间出现重复广播或顺序变化，影响现有外部模块；因此必须让 typed event 以 wrapping/forwarding 方式从旧源头生成，而不是改变旧事件触发点。 |
| 兼容性 | 向后兼容。旧 compile/reload delegate 全部保留，新 typed event 只是新增推荐入口；现有订阅者不需要立即迁移。 |
| 验证方式 | 1. 写一个样例扩展模块，分别订阅 legacy hook 和 typed hook，确认两边观察到的时序一致。 2. 在一次 soft reload 和一次 full reload 中验证 `ChangedModules`、`bFullReload`、`ReloadedTypes` 等字段与真实行为一致。 3. 复跑现有 compile / reload 测试，确认新增 typed event 不改变旧 hook 的触发次数和顺序。 |

### 本轮场景判断（增量）

| 场景 | 当前是否需要改插件源码 | 依据 | 备注 |
|------|------------------------|------|------|
| 添加自定义全局函数 | 不需要 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:362`-`:377`、`:437`-`:467`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:151`-`:214`、`:588`-`:600` | 需要宿主 `C++ module` include `AngelscriptBinds.h` 并注册 static `FBind` / `BindGlobalFunction()`；纯脚本作者做不到。 |
| 修改编译行为（如自定义 `import` 解析） | 需要 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h:63`-`:67`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1291`-`:1292`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:232`-`:237`、`:439`-`:497` | 当前只有 built-in automatic/manual import 分支，没有正式 resolver/provider 接口。 |
| 添加自定义类型绑定 | 不需要 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:72`-`:81`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:54`-`:118` | 仍需 expert-mode `C++` 扩展：实现 `FAngelscriptType` 或注册 `TypeFinder`，门槛高。 |
| 订阅编译 / 热重载事件 | 不需要 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h:37`-`:42`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h:31`-`:38`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h:29`-`:30` | 需要 `C++ module`，且现有 payload contract 偏弱；纯 `.as` 侧仍然没有正式入口。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-EP20 | `import` / module resolution 只有 built-in 分支，没有 resolver contract | 编译扩展点新增 | 高 |
| P1 | Arch-EP21 | compile / reload hook payload 过弱且风格混杂，难以支撑外部工具 | event contract 收敛 | 高 |

---

## 架构分析 (2026-04-08 17:48)

### Arch-EP22：插件已经在测试链路验证了 `class-based customization`，但 runtime 扩展设置仍停留在 flag 级别

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `ini/settings` 是否真的能声明“用哪个扩展实现”，还是只能调内建行为开关 |
| 当前设计 | 当前仓库内部其实已经有一条成熟的“settings -> user supplied class/object”路径，但它只用于测试链路：`UAngelscriptTestSettings` 允许项目指定 `UnitTestGameInstanceClass`，`IntegrationTest` 还能通过 settings 驱动网络仿真参数；反过来，正式 runtime 的 `UAngelscriptSettings` 仍然几乎全部是 `bool / enum / string list / blacklist`，`FAngelscriptEngineConfig` 也没有任何 `TSubclassOf` / `TSoftClassPtr` / provider factory 字段。换句话说，代码库已经证明自己能安全消费“项目自定义类”，只是这套 authoring model 还没进入 runtime extensibility。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Testing/AngelscriptTestSettings.h:32`-`:65` 把 `UAngelscriptTestSettings` 做成 `UDeveloperSettings`，并公开 `TSoftClassPtr<UGameInstance> UnitTestGameInstanceClass`；`Plugins/Angelscript/Source/AngelscriptRuntime/Testing/UnitTest.cpp:281`-`:295` 在单元测试启动时 `LoadSynchronous()` 该类并回退到 `UGameInstance::StaticClass()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Testing/AngelscriptTestSettings.h:98`-`:124` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Testing/IntegrationTest.cpp:323`-`:330` 说明 integration test 也会按 settings 注入 network emulation；对比 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h:41`-`:224` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:64`-`:95`，runtime 侧只有行为参数，没有任何 runtime contributor class / module / factory 槽位。 |
| 优点 | 测试链路已经给出两个重要先例：一是项目自定义类可以通过 settings 安全加载并附带 fallback；二是 settings 可以驱动一次性环境装配，而不要求用户改插件源码。 |
| 不足 | 这个先例没有被推广到正式 runtime。结果是项目方可以通过 settings 替换“测试用 `GameInstance` 与网络环境”，却不能通过同样的产品化入口声明“运行时 host / module resolver / type provider / compile observer”。运行时扩展仍然主要依赖 expert-mode `C++ module` glue。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 直接把 runtime strategy 做成 settings 可配置类：`EnvLocatorClass`、`ModuleLocatorClass`、`PreBindClasses` 都在 `UUnLuaSettings` 中声明，模块启动时按配置实例化 locator 并预绑定类。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaSettings.h:23`-`:56`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaModule.cpp:103`-`:125`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:74`-`:81` | 当前仓库不必从零发明模式，完全可以复用“settings 声明 provider class，运行时按默认对象/实例消费”的路径。 |
| UnrealCSharp | 把“用哪个 runtime 组件”和“导出哪些模块”做成正式 settings：`AssemblyLoader`、`ExportModule`、`ClassBlacklist` 都是配置项，而不是硬编码在调用点里。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpSetting.h:140`-`:144`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpEditorSetting.h:139`-`:148`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/UnrealCSharpCore.build.cs:128` | 即使具体语言链路不同，也说明“用户可替换实现”和“导出范围”应当是 manifest，而不是散落在若干内部注册点的隐式知识。 |
| puerts | 即便不走 settings，它也给出单一正式 contract：`IPuertsModule` 和 `IJSModuleLoader` 是用户扩展的显式入口。 | `Reference/puerts/unreal/Puerts/Source/Puerts/Public/PuertsModule.h:17`-`:56`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSModuleLoader.h:17`-`:45` | 如果不想一开始就把一切做成 settings，至少也应当让 runtime extensibility 有单一、被支持的 provider contract。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 复用当前测试链路已经验证过的 `UDeveloperSettings + soft/class reference + fallback` 模式，为 runtime 扩展补一个正式的 class/module manifest。 |
| 具体步骤 | 1. 新增 `UAngelscriptRuntimeExtensionSettings : UDeveloperSettings`，或在 `UAngelscriptSettings` 中增量加入 `RuntimeHostClass`、`ModuleResolverClass`、`TypeProviderClasses`、`CompileObserverClasses`、`ExtensionModules` 等可选字段；第一阶段至少先支持 1 个 host 和 1 个 resolver。 2. 在 `FAngelscriptRuntimeModule::InitializeAngelscript()` 或 `FAngelscriptEngine::Create()` 中复用 `UnitTestGameInstanceClass.LoadSynchronous()` 这类模式：若配置为空则走 legacy 默认实现，若配置存在则实例化 provider 并接到现有 `EngineConfig / Dependencies / hook` 上。 3. 在 `AngelscriptEditorModule` 注册 runtime settings 时增加 editor 期校验，检查 class 是否实现预期接口、module 是否可加载，并给出明确错误。 4. 在 `StateDump` 增加 `ConfiguredRuntimeExtensions` 表，列出最终解析到的 class/module 与 fallback 状态，避免用户只能靠日志猜测。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Testing/AngelscriptTestSettings.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/AngelscriptRuntimeExtensionSettings.h/.cpp` |
| 预估工作量 | M |
| 架构风险 | 最大风险不是实例化本身，而是 provider 生命周期和 editor/runtime 域分层。如果直接把 editor-only provider 放进 runtime settings，容易把 `WITH_EDITOR` 依赖重新渗回 runtime 模块；因此第一阶段应先区分 `Runtime` 与 `Editor` provider 类型。 |
| 兼容性 | 向后兼容。所有新字段默认留空，空值时完全沿用现有 runtime 路径；已有 `C++ module` 直接注册方式继续可用。 |
| 验证方式 | 1. 新建一个最小宿主测试模块，通过 settings 指定自定义 runtime host 或 resolver，验证无需改插件源码即可被实例化。 2. 回归 `UnitTestGameInstanceClass` 与 integration test network emulation，确认复用这套模式不会影响现有测试定制。 3. 通过 `StateDump` 检查 runtime extensions 能看到“显式配置项”和“fallback 到 legacy 实现”的状态。 |

### Arch-EP23：扩展语义相关 settings 仍以 `singleton + static globals` 生效，无法形成真正的 engine profile

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 多引擎/多 profile 场景下，扩展语义能否随 engine 实例隔离，还是仍然由进程级 settings 决定 |
| 当前设计 | 当前 `FAngelscriptEngineConfig` 只承载 command line 与极少数 runtime flag；真正会影响扩展语义的选项，例如 namespace strip、`ScriptName` 命名策略、`float`/`double` 语义、`StaticClass` 兼容模式、默认 property/function specifier，仍然要么在 engine 初始化时写入 `FAngelscriptEngine` 的 process-static 变量，要么在预处理、bind 注册、debug server 请求时直接 `GetDefault<UAngelscriptSettings>()` 读取。结果是“能 clone engine”并不等于“能有不同扩展 profile”：不同 engine 不能拥有各自的命名/类型/警告语义，扩展行为仍跟着进程单例走。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:64`-`:83` 显示 `FAngelscriptEngineConfig` 只有 command line/runtime flag 和 `DisabledBindNames`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1280`-`:1308` 在 `PreInitialize_GameThread()` 中读取 `GetMutableDefault<UAngelscriptSettings>()`，并把 `bUseScriptNameForBlueprintLibraryNamespaces`、`BlueprintLibraryNamespacePrefixesToStrip`、`BlueprintLibraryNamespaceSuffixesToStrip` 写入 `FAngelscriptEngine` 的 static 变量；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h:123`-`:160` 又直接依赖这些 static 变量计算最终 namespace；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:38`-`:57` 在构造时从 default settings 读取 `PreprocessorFlags`、`bDefaultFunctionBlueprintCallable`、默认 property specifier；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Primitives.cpp:610`-`:639` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:1499`-`:1504` 说明 `bScriptFloatIsFloat64`、`StaticClassDeprecation` 仍然通过 singleton 直接参与 bind 语义与 debug 数据。 |
| 优点 | 这种模型对 legacy 项目很便宜，所有扩展语义都围绕同一份 `Engine.ini` 和当前进程运行态展开，不需要额外处理 profile 组合问题。 |
| 不足 | 一旦项目想做 `Editor vs Cooked`、`主引擎 vs clone`、`测试扩展 vs 正式扩展` 的差异化策略，就会发现 extension profile 实际上没有 engine 级承载体。更关键的是，这类设置决定的是“函数叫什么名、类型别名是什么、哪些语法被警告/禁止”，它们直接影响外部 bind/type 扩展的最终 ABI，而不仅仅是 UI 选项。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `FJsEnv` 构造函数直接接收 `IJSModuleLoader`，`FJsEnvGroup::SetJsEnvSelector()` 决定 group 内对象如何路由到具体 env；module/selector 策略天然挂在 env/group 上，而不是若干 process-static 设置。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JsEnv.h:61`-`:68`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSModuleLoader.h:17`-`:45`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvGroup.cpp:155`-`:182` | 先把策略绑定到 env 实例，再决定是否共享默认值，这比“先写全局 static 再由当前 engine 读取”更容易支持多 profile。 |
| UnLua | `FLuaEnv` 在构造时保存自己的 `ModuleLocator`，而 `ULuaModuleLocator` 本身又是可子类化 strategy object。虽然 settings 仍是全局来源，但真正参与解析的是 env 持有的对象，而不是 process-static 命名表。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/LuaModuleLocator.h:20`-`:33`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:74`-`:81` | 即便默认配置来自全局 settings，实际执行语义也应先落到 env-owned object，避免扩展策略只存在于全局单例。 |
| UnrealCSharp | `bEnableExport` / `ExportModule` 更像是 generator/build stage 的输入：build 阶段转成 `WITH_BINDING`，editor 生成阶段再通过 `OnBeginGenerator` / `OnEndGenerator` 执行流程，而不是在若干 runtime 请求点反复 `GetDefault`。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/UnrealCSharpCore.build.cs:128`；`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:237`-`:309` | 对“影响绑定产物”的配置，最好在明确的 stage 上 snapshot，而不是任由不同调用点各自读取 singleton。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把“影响扩展语义的 settings”收口成 engine-local `effective profile`，让 `UAngelscriptSettings` 只负责提供默认值，而不是直接决定所有 engine 的即时行为。 |
| 具体步骤 | 1. 在 `Core/` 新增 `FAngelscriptEffectiveSettings`，至少覆盖 `bUseScriptNameForBlueprintLibraryNamespaces`、namespace strip 列表、`bScriptFloatIsFloat64`、`StaticClassDeprecation`、`PreprocessorFlags`、默认 property/function specifier。 2. 扩充 `FAngelscriptEngineConfig`，让 `FromCurrentProcess()` 用 `UAngelscriptSettings` 填充默认 `EffectiveSettings`；后续 runtime host / test code 可以按 engine 覆盖其中部分字段。 3. 让 `FAngelscriptPreprocessor`、`Helper_FunctionSignature`、`Bind_Primitives`、`DebugServer` 优先消费当前 engine 的 `EffectiveSettings`；对现有 `FAngelscriptEngine::bUseScriptNameForBlueprintLibraryNamespaces` 这类 static 变量，先保留 compatibility accessor，但内部改为从 `TryGetCurrentEngine()->GetEffectiveSettings()` 取值。 4. 扩展 `StateDump` 输出 `EffectiveSettings` 与其来源（`DefaultSettings` / `RuntimeOverride` / `TestOverride`），并新增 multi-engine 覆盖测试，验证两个 engine 可以拥有不同 profile 而不互相污染。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Primitives.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEffectiveSettings.h/.cpp` |
| 预估工作量 | M |
| 架构风险 | 主要风险是 legacy static accessor 与当前调用栈对 `TryGetCurrentEngine()` 的依赖。如果在没有 current engine 的阶段就读取 `EffectiveSettings`，需要定义稳定 fallback（通常回退到 `UAngelscriptSettings` 默认值），否则会引入初始化时序问题。 |
| 兼容性 | 向后兼容。默认 `EffectiveSettings` 仍由当前 `UAngelscriptSettings` 生成；旧项目不做任何配置即可得到完全相同的行为。只有主动按 engine 覆盖 profile 的项目才会进入新路径。 |
| 验证方式 | 1. 增加 multi-engine 测试：让 source engine 和 clone 使用不同 namespace strip / `bScriptFloatIsFloat64`，验证生成的 bind/type 语义不会串线。 2. 回归 `Bind_Primitives`、preprocessor 和 debug server 输出，确认在单一默认 profile 下行为不变。 3. 检查 `StateDump` 中的 `EffectiveSettings`，确认能区分 default 与 per-engine override。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-EP23 | 扩展语义相关 settings 仍按 process-global 生效，缺少 engine profile | 配置作用域收口 | 高 |
| P2 | Arch-EP22 | 测试链路已有 class-based customization，但 runtime 设置仍无同等级入口 | 扩展 manifest 产品化 | 中高 |

---

## 架构分析 (2026-04-08 18:02)

### Arch-EP24：插件内部已经有一套成熟的 `opt-in extension object` 模型，但这套做法只停留在 editor 侧

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 当前插件里，是否已经存在“正式、可发现、可卸载”的扩展对象模型，可作为 runtime 扩展点产品化的直接前例 |
| 当前设计 | 这次重查源码后可以确认：`UScriptEditorMenuExtension` 不是临时工具类，而是一套完整的 extension framework。它提供明确的 opt-in base class、`BlueprintNativeEvent` 级别的筛选点、函数发现规则、注册/反注册、reload 后重建，以及 `Asset` / `Actor` 两种 typed specialization。问题在于，这套做法目前只服务 editor 菜单扩展；到了 runtime core，插件又退回成 `static delegate + global scan + raw registry` 模式，导致最成熟的 extensibility authoring model 没有被复用到“自定义全局函数 / 类型绑定 / 编译观察器”这些更核心的扩展需求上。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h:42`-`:109` 把 `UScriptEditorMenuExtension` 做成公开 `UObject` 基类，并暴露 `ShouldExtend()`、`GatherExtensionFunctions()`、`CallFunctionOnSelection()`、`GetExtensionPointOrDefault()`；`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:25`-`:45` 在 `InitializeExtensions()` 中订阅 `FAngelscriptClassGenerator::OnPostReload` 与 `FCoreDelegates::OnEnginePreExit`，形成重载后重建与退出前清理；同文件 `:845`-`:867` 通过 `TObjectIterator<UASClass>` 扫描 opt-in script class，并以 CDO 驱动注册；`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptAssetMenuExtension.h:5`-`:25` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptActorMenuExtension.h:5`-`:26` 又提供按 `SupportedClasses` / `SupportsAsset()` / `SupportsActor()` 定制的特化版本。反过来，runtime core 仍主要暴露 raw seam：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h:8`-`:47` 公开 static delegate accessor，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:71`-`:81` 公开 `Register()` / `RegisterTypeFinder()`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h:8`、`:29`-`:30` 公开 `OnProcessChunks` / `OnPostProcessCode`。 |
| 优点 | editor 侧这套模型已经证明项目内可以稳定承载“显式 base class + 发现 + 生命周期 + teardown + typed specialization”的扩展协议，而且实现并不复杂。 |
| 不足 | runtime extensibility 没有复用这条成功路径，导致插件在不同 domain 上对用户给出的 authoring model 完全不同：editor 是 productized object model，runtime 却还是 expert-mode glue code。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 把“给已有类型加能力”明确收口到 `UExtensionMethods` 这一 opt-in base class；`InitExtensionMethodsMap()` 只扫描这类 native class，再按首参数类型建立扩展方法表。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/ExtensionMethods.h:18`-`:20`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:931`-`:983` | 当前插件不缺扫描能力，缺的是“只扫描显式声明的扩展对象”这一收口动作。 |
| UnLua | 把对象级扩展协议做成显式 interface，把 loader 替换做成显式 delegate，并且在 sample 工程里直接演示宿主如何实现。`UTPSGameInstance` 只需实现 `IUnLuaInterface::GetModuleName()`，教程库则通过 `FUnLuaDelegates::CustomLoadLuaFile.BindStatic()` 接管加载。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaInterface.h:23`-`:39`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDelegates.h:22`-`:50`；`Reference/UnLua/Source/TPSProject/TPSGameInstance.h:22`-`:32`；`Reference/UnLua/Source/TPSProject/TutorialBlueprintFunctionLibrary.cpp:91`-`:103` | 真正成熟的扩展点，不只是“内部可插”，还要有宿主侧可直接采用的对象协议和示例。 |
| UnrealCSharp | 虽然更多依赖 delegates/settings，但公开 contract 也是单独命名和收口的，例如 `FUnrealCSharpCoreModuleDelegates` 与 `ExportModule`/`ClassBlacklist`。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Delegate/FUnrealCSharpCoreModuleDelegates.h:7`-`:37`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpEditorSetting.h:136`-`:148` | 即使扩展实现形态不同，也应当先给出统一、命名化的 contract，再谈内部如何装配。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 不另起炉灶设计第四套 runtime 扩展协议，而是把 editor 侧已经验证过的 `extension object + registrar + teardown` 模型抽象出来，增量推广到 runtime core。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/` 新增 `UAngelscriptExtensionObject` 与 `FAngelscriptRuntimeExtensionRegistry`，提供 `RegisterExtension()`、`UnregisterExtension()`、`SupportsEngine()`、`GetExtensionKind()` 这类最小 contract。 2. 第一阶段只落三类 runtime object：`UAngelscriptFunctionExtension`、`UAngelscriptTypeExtension`、`UAngelscriptCompileObserver`；它们内部继续调用现有 `BindGlobalFunction()`、`RegisterTypeFinder()`、compile delegates，先复用现有机制。 3. 在 `FAngelscriptRuntimeModule::StartupModule()` 和 reload 完成点增加一次显式扫描，只识别 `UAngelscriptExtensionObject` 子类，而不是继续依赖 ambient static 注册；同时记录 `SourceClass`、`SourceModule` 和 registration handle。 4. 借用 `UScriptEditorMenuExtension` 的 teardown 思路，在 `ShutdownModule()` / engine teardown 中统一反注册这些 runtime extension object，避免 Live Coding 或模块重载时重复挂接。 5. 原有 raw API 保持不动，但把新 object model 作为推荐路径，并在 `StateDump` 中输出 `RegisteredRuntimeExtensionObjects`。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/AngelscriptExtensionObject.h/.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeExtensionRegistry.h/.cpp` |
| 预估工作量 | M |
| 架构风险 | runtime extension object 如果直接照搬 editor 侧 `TObjectIterator<UASClass>` 扫描，可能把 cook/runtime 生命周期和 editor-only class 装载混在一起；第一阶段必须把扫描边界限制在显式 base class 和正确 module domain。 |
| 兼容性 | 向后兼容。现有 `BindGlobalFunction()`、`RegisterTypeFinder()`、compile/reload delegate 继续有效；新 object model 只是新增一条更正式的 authoring path。 |
| 验证方式 | 1. 新建一个最小宿主扩展模块，只通过 `UAngelscriptFunctionExtension` 增加 namespaced/global function，验证无需改插件源码且可在 `StateDump` 中看到来源。 2. 再新建一个 `UAngelscriptCompileObserver`，验证 reload 后不会重复注册，模块卸载后不会残留回调。 3. 复跑 editor menu extension 流程，确认这次抽象没有破坏既有 `UScriptEditorMenuExtension` 行为。 |

### Arch-EP25：`virtual` 搜索结果里混入了大量 internal runtime vtable，用户真正该继承的 seam 与 accidental ABI 没有分开

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 用户按“搜索全部 `virtual` 函数”来找扩展点时，看到的是明确设计过的 subclass seam，还是一批因为 public include 过宽而外露的内部运行时 vtable |
| 当前设计 | 这轮全量 `virtual` 搜索的一个更深问题是：很多命中的并不是“建议用户继承”的扩展点，而是脚本运行时内部 dispatch/type-system vtable。`AngelscriptRuntime.Build.cs` 又把 `ModuleDirectory`、`Core` 和 third-party source 直接放进 `PublicIncludePaths`，使这些 internal class 对外天然可见。结果是用户在搜索 `virtual` 时，会同时看到真正有意暴露的 `UScriptEditorMenuExtension`/subsystem 基类，以及 `UASClass`、`UASFunction`、`FAngelscriptType` 这类内部核心对象；前者是 contract，后者更像 accidental ABI。当前架构没有把这两类 subclass seam 区分开。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs:15`-`:22` 直接公开 `ModuleDirectory`、`Core` 与 `ThirdParty/angelscript/source`，`:67`-`:73` 在 editor target 下还把 `UnrealEd` / `EditorSubsystem` 暴露进 public 依赖；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h:76`-`:90` 公开 `UASClass` 的 `GetMostUpToDateClass()`、`RuntimeAddReferencedObjects()`、`GetLifetimeScriptReplicationList()`、`RuntimeDestroyObject()` 等 virtual runtime method，`ASClass.h:205`-`:207` 与 `:221`-`:235` 又公开 `UASFunction` 的 `RuntimeCallFunction()` / `RuntimeCallEvent()` / `GetRuntimeValidateFunction()` 及其一系列派生 override；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:71`-`:81`、`:96`-`:215` 则把 `FAngelscriptType` 的大型 type-runtime vtable 直接暴露出来。与之对照，真正明显面向用户的 subclass seam 反而集中在 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h:42`-`:109` 这类清晰命名的扩展基类。 |
| 优点 | 由于 include 面宽，插件内部和同仓库模块扩展起来非常快，很多实现无需再做 façade。 |
| 不足 | 对外部使用者来说，`virtual` 搜索的噪音极高，而且容易形成对内部运行时类的直接继承与 include 依赖。一旦后续重构 `UASClass`/`UASFunction`/`FAngelscriptType` 内部实现，就会背上并非刻意承诺的 public ABI 兼容负担。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 公开给扩展者的 subclass seam 很窄而且命名明确：`IJSModuleLoader` 只负责 `Search/Load/GetScriptRoot`，`IPuertsModule` 只负责模块级扩展动作；内部 wrapper/bridge 细节并不要求用户继承。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSModuleLoader.h:17`-`:49`；`Reference/puerts/unreal/Puerts/Source/Puerts/Public/PuertsModule.h:17`-`:56` | 把“应该继承什么”压缩成少数 deliberate interface，能显著降低 accidental coupling。 |
| UnLua | 公开 contract 也很集中：`IUnLuaInterface` 明确只解决对象到脚本模块的映射，`UUnLuaSettings` 只声明可替换 locator class；用户不需要去碰 LuaEnv 内部运行时对象的 virtual 细节。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaInterface.h:23`-`:39`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaSettings.h:47`-`:56` | 用户 subclass seam 应该是“业务含义明确的接口/locator”，而不是 runtime kernel class。 |
| UnrealCSharp | 对外更多暴露 delegate/settings manifest，而不是 runtime 内部对象的继承点；`FUnrealCSharpCoreModuleDelegates` 与 `ExportModule`/`ClassBlacklist` 足以表达多数扩展意图。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Delegate/FUnrealCSharpCoreModuleDelegates.h:7`-`:37`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpEditorSetting.h:136`-`:148` | 不一定非要给用户很多 `virtual`；关键是公开 surface 要有意设计，而不是 accidental leak。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把“deliberate extension contract”和“internal runtime vtable”分层，先新增清晰的 `Public/Extension` seam，再逐步收紧 accidental public ABI。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/` 收口所有正式支持的用户继承点和接口，例如 extension object、type contributor、compile observer、script-root locator；这些类统一采用 `AngelscriptExtension*` 或 `IAngelscript*` 命名。 2. 把 `ASClass.h`、`UASFunction` 派生类、`FAngelscriptType` 的 raw vtable 视为 internal runtime header：第一阶段先补文档注释和 `deprecated for external use` 提示，第二阶段再通过 `Build.cs` 收口 `PublicIncludePaths`，让外部模块默认只能看到 `Public/Extension`。 3. 对确实还需要作为 escape hatch 保留的低层接口，例如 `FAngelscriptType`，增加更高层 public contributor wrapper，并明确标注“expert-only/internal-ABI-risk”；避免用户把它误当成日常 extension base。 4. 新增一个“public-only extension build” 自动化目标，约束一个宿主扩展模块只能 include `Public/Extension` 头而不能 include `Core/` / `ClassGenerator/` / third-party internal header。 5. 在文档或 state dump 中生成 `SupportedSubclassSeams` 清单，明确哪些类/接口是正式支持的用户继承点，哪些只是内部实现。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/*`、新增 public-only 验证模块/目标文件 |
| 预估工作量 | M-L |
| 架构风险 | 当前外部模块很可能已经直接 include `Core/` 或 `ClassGenerator/` 头；如果过快收口 include path，会出现真实编译回归。必须先给新 public contract 和迁移窗口，再收紧旧路径。 |
| 兼容性 | 需要分阶段保持向后兼容。第一阶段只新增 `Public/Extension` 并对 internal header 加 warning/注释；旧 include 继续工作。第二阶段才考虑真正收口 `PublicIncludePaths`。 |
| 验证方式 | 1. 新建一个宿主扩展模块，只 include `Public/Extension` 头完成函数扩展、类型扩展和事件订阅，验证不再需要 `ASClass.h` / `AngelscriptType.h`。 2. 在 CI 中增加一次“禁止 include internal runtime header”的编译检查，确认 deliberate surface 足够完成常见扩展场景。 3. 复跑现有内建 bind/type/runtime 测试，确认 internal header 收口不会改变插件自身行为。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-EP25 | `virtual` 搜索被 internal runtime vtable 污染，正式 subclass seam 与 accidental ABI 未分层 | public contract 收口 | 高 |
| P2 | Arch-EP24 | editor 已有成熟 extension object 模型，但 runtime core 未复用 | 扩展模型复用 | 中高 |

---

## 架构分析 (2026-04-08 18:16)

### Arch-EP26：`BindModules.Cache` 已经形成一条隐藏的 generated extension lane，但 producer/consumer contract 目前是断裂的

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 用户想通过自动生成的 bind module 扩展 `UFUNCTION` 绑定行为，而不是手改插件源码时，这条通道是否有稳定、可发现、可部署的 contract |
| 当前设计 | 重新核对 `GenerateNativeBinds()` 与 runtime 初始化后可以确认：仓内确实已经存在一条独立的 generated extension lane。editor 会把扫描得到的类分批生成 `ASRuntimeBind_*` / `ASEditorBind_*` 模块，并把模块名写入 `BindModules.Cache`；runtime 初始化时也会先读取 cache、再按模块名 `LoadModule()`、最后执行 `BindScriptTypes()`。但 producer 和 consumer 使用的 cache 路径并不一致：editor 把文件写到 `GetScriptRootDirectory()` 返回的项目 `Script/` 根，而 runtime 却从 `IPluginManager::FindPlugin("Angelscript")->GetBaseDir()` 读取插件根目录。也就是说，这条“无须改插件源码加载外部 generated bind module”的扩展面在实现里存在，但没有统一 manifest 路径，也没有正式 settings contract。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:999`-`:1077` 的 `GenerateNativeBinds()` 会生成 `ASRuntimeBind_*` / `ASEditorBind_*` 模块并调用 `FAngelscriptBinds::SaveBindModules(FAngelscriptEngine::GetScriptRootDirectory() / "BindModules.Cache")`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:781`-`:793` 说明 `GetScriptRootDirectory()` 返回当前 engine 第一条 root，也就是项目 `Script/` 根；而 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1473`-`:1488` 却从 `plugin->GetBaseDir() / "BindModules.Cache"` 读取，再在 `:1493`-`:1495` 调用 `BindScriptTypes()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:584`-`:601` 的 `SaveBindModules()` / `LoadBindModules()` 只是原样写读字符串数组，没有额外 metadata 来解释该文件归属哪个项目或哪次生成。 |
| 优点 | 当前插件并不是完全没有“项目自定义绑定模块”这条路；相反，它已经具备 `生成模块名 -> 预加载模块 -> 再执行 bind` 的骨架，说明设计方向并不需要从零开始。 |
| 不足 | 这条 lane 目前仍是隐藏且脆弱的。路径不一致会让 editor 产物无法被 runtime 按原设计消费；用户也没有 settings/dump 可以确认 cache 应放在哪里、当前实际加载了哪些 generated module。结果是存在一条扩展通道，但它既不自描述，也不可靠，出了问题只能回头读源码。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | 生成边界与消费边界是同一个 contract。`UUnrealCSharpEditorSetting` 先用 `bEnableExport` / `ExportModule` / `ClassBlacklist` 显式声明导出范围；`UnrealCSharpCore.build.cs` 再把宿主模块信息写到 `ProjectIntermediateDir()/UnrealCSharp_Modules.json`；运行时代码通过 `FUnrealCSharpFunctionLibrary` 从同一条 `Intermediate` 路径读取该 manifest。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpEditorSetting.h:136`-`:148`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/UnrealCSharpCore.build.cs:128`-`:171`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp:1265`-`:1324` | 生成产物是否被消费，不应依赖“写在哪个目录靠约定记忆”；producer 和 consumer 应共享同一个 manifest path 和 schema。 |
| UnLua | 不走隐藏 cache，而是把 runtime extension provider 明确写进 settings：`EnvLocatorClass`、`ModuleLocatorClass`、`PreBindClasses` 都是显式配置，模块启动时按配置实例化和执行。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaSettings.h:47`-`:56`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaModule.cpp:103`-`:125` | 即便扩展行为最终还是“自动化发现”，入口也应当先表现为用户可见的 manifest，而不是隐藏文件。 |
| puerts | 把模块加载 contract 直接收口成 `IJSModuleLoader`，由 `FJsEnv` 构造时注入，默认实现 `DefaultJSModuleLoader` 负责 `Search()` / `Load()`；没有额外的 sidecar cache 去传递“该加载哪些模块”。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSModuleLoader.h:17`-`:29`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JsEnv.h:19`-`:25`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DefaultJSModuleLoader.cpp:53`-`:94` | 如果扩展点的核心是“找到并加载额外脚本/模块”，优先设计显式 loader/provider，比额外再引入一份隐式 cache 更稳。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把现有 `BindModules.Cache` 升级为正式 `generated bind module manifest`：统一路径、统一 schema、统一 diagnostics，保留 legacy cache 兼容。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/` 新增 `FAngelscriptBindModuleManifest`，集中定义 `GetManifestPath()`、`Load()`、`Save()`；第一阶段建议把 canonical 路径定到 `ProjectIntermediateDir()/Angelscript/BindModules.json` 这类明确属于宿主项目的目录。 2. 让 `AngelscriptEditorModule::GenerateNativeBinds()` 和 `FAngelscriptEngine::Initialize()` 都只通过该 manifest helper 访问产物，不再各自拼路径。 3. 把当前字符串数组升级成带字段的 manifest，至少记录 `Modules`、`GeneratedAt`、`SourceProjectRoot`、`bContainsEditorModules`；runtime 读取时先过滤 editor-only module，再按顺序 `LoadModule()`. 4. 在 `UAngelscriptSettings` 或 `FAngelscriptEngineConfig` 增加 `AdditionalBindModules` / `BindModuleManifestPathOverride` 这类显式入口，让用户能不用改插件源码就声明附加模块。 5. 为兼容旧流程，runtime 在第一阶段同时回退尝试 `Project ScriptRoot/BindModules.Cache` 与 `PluginBaseDir/BindModules.Cache`，但要输出 warning，提示迁移到新 manifest。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindModuleManifest.h/.cpp` |
| 预估工作量 | M |
| 架构风险 | 如果直接替换旧 cache 路径而不做兼容回退，现有依赖 `BindModules.Cache` 的内部流程可能立即失效；因此第一阶段必须保持双读兼容，并在日志里明确实际命中的路径。 |
| 兼容性 | 向后兼容。旧 `BindModules.Cache` 继续可读；新 manifest 只是把路径和 schema 产品化。现有 generated bind module 名称也可以保持不变。 |
| 验证方式 | 1. 在 editor 生成一轮 native binds，确认 manifest 写到 canonical 路径，runtime 同一路径能读到并成功 `LoadModule()`. 2. 用 `StateDump` 输出 `LoadedBindModules` / `BindModuleManifestPath`，验证用户能直接看到本轮加载了哪些 generated module。 3. 保留一份 legacy `BindModules.Cache`，验证兼容回退仍能工作且会输出迁移 warning。 |

### Arch-EP27：`FBind` 贡献者在第一次初始化后被时序锁死，缺少 late-join / reapply contract

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 外部模块如果在 `Angelscript` 已初始化之后才加载，还能否通过 `FBind` / generated bind module 这种正式贡献者路径扩展全局函数或绑定行为 |
| 当前设计 | 当前扩展激活是典型的 one-shot 设计。`FAngelscriptRuntimeModule::StartupModule()` 在 editor/commandlet 启动时就立刻调用 `InitializeAngelscript()`；`InitializeAngelscript()` 又受 `bInitializeAngelscriptCalled` 保护，只会成功执行一次。engine 初始化过程中，bind module preload 和 `BindScriptTypes()` 只在这一轮里发生一次；而 `FAngelscriptBinds::RegisterBinds()` 本身只是把 lambda 追加到 static `BindArray`。这意味着，依赖 `FBind` 静态注册或 `BindModules.Cache` preload 的扩展，如果晚于首次初始化才进入进程，就不会自动回填到当前 engine。需要强调的是，expert-mode 直接调用 `BindGlobalFunction()` 这类 raw API 仍然可以立刻改当前 engine；被锁死的是“正式贡献者路径”的激活时机，而不是所有底层能力。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp:13`-`:18` 在 `StartupModule()` 里直接触发 `InitializeAngelscript()`，`:138`-`:165` 又用 `bInitializeAngelscriptCalled` 保证只初始化一次；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:912`-`:916` 与 `:1491`-`:1495` 都是在初始化流程中调用 `BindScriptTypes()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:151`-`:158` 的 `RegisterBinds()` 仅追加到 static `BindArray`，真正执行只发生在 `:195`-`:214` 的 `CallBinds()`；而生产代码中触发 `CallBinds()` 的入口就是 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1915`-`:1921` 的 `BindScriptTypes()`；作为对照，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:588`-`:608` 的 `BindGlobalFunction()` / `BindGlobalFunctionDirect()` 是直接对当前 engine 调 `RegisterGlobalFunction()`，说明 raw escape hatch 仍然存在。 |
| 优点 | 启动顺序简单，默认项目几乎不需要考虑 contributor 生命周期；所有内建 bind 都在 engine 初始建表阶段一次完成，行为确定性高。 |
| 不足 | 这套设计对“扩展模块晚加载”极不友好。项目插件、Live Coding、按需加载模块、乃至前一条 `generated bind module` lane 只要错过首次初始化窗口，就没有正式的再激活入口。外部作者要么被迫提高模块加载相位，要么退回更底层的直接 `BindGlobalFunction()` 调用，正式扩展路径反而最依赖隐式时序。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | 生成链是可重入的。`Generator()` 可以重复执行完整生成/编译流程，并通过 `OnBeginGenerator` / `OnEndGenerator` / `OnCompile` 广播阶段信号；扩展者不需要假设“只能在第一次启动前接入”。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Delegate/FUnrealCSharpCoreModuleDelegates.h:14`-`:35`；`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:237`-`:309` | 对扩展系统而言，关键不是一定要热插拔，而是至少要有一条正式的 re-entry path。 |
| UnLua | `ULuaEnvLocator` 把 `HotReload()` / `Reset()` 做成正式虚接口，`FUnLuaModule::HotReload()` 直接调用 locator 的 `HotReload()`；宿主可以在系统已启动后刷新 env/provider，而不是完全绑定在首次启动时机上。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/LuaEnvLocator.h:27`-`:31`、`:41`-`:45`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaModule.cpp:152`-`:157` | 即便核心运行时在启动时创建，provider/locator 也应该保留刷新契约。 |
| puerts | 模块接口本身就暴露了可在运行中调用的扩展动作：`ReloadModule()`、`InitExtensionMethodsMap()`、`SetJsEnvSelector()` 都不是只能在第一次启动前调用的初始化函数。 | `Reference/puerts/unreal/Puerts/Source/Puerts/Public/PuertsModule.h:41`-`:47`；`Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:80`-`:119` | 允许 late configuration 的关键是显式 API，而不是要求扩展作者赌模块加载相位。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留当前 one-shot 初始化的前提下，补一层正式的 `extension activation / refresh` contract，让 `FBind` 贡献者和 generated bind module 能在首次初始化后安全 late-join。 |
| 具体步骤 | 1. 在 `Core/` 新增 `FAngelscriptExtensionActivationRegistry`，统一记录 `PendingBindContributors`、`LoadedBindModules`、`AppliedGeneration`；把当前 `FBind` 静态注册包装成 contributor entry，而不只是匿名 lambda。 2. 在 `RegisterBinds()` 路径里检测当前 engine 是否已完成 `BindScriptTypes()`：若未完成则保持现状；若已完成则把新 contributor 标记为 `PendingAfterInitialization` 并输出明确 warning，而不是默默错过。 3. 新增 `FAngelscriptRuntimeModule::RefreshExtensions()` 或 `FAngelscriptEngine::ApplyPendingExtensions()`，只负责加载新增 bind module 并执行尚未应用的 contributor，不重复整个 engine 初始化。 4. 对 generated bind module manifest 的消费也走同一 registry，保证 editor 生成的新模块在 runtime 内可以显式 refresh，而不是只能等下次进程启动。 5. 增加 idempotent guard，避免二次 refresh 导致同一 contributor 重复注册；必要时给 `FBindInfo` 扩展 `SourceModule` / `GenerationId` / `bApplied` 字段。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptExtensionActivationRegistry.h/.cpp` |
| 预估工作量 | M-L |
| 架构风险 | 最大风险是 refresh 后重复注册全局函数、对象方法或 metadata，导致引擎内部声明冲突；因此 registry 必须先做 contributor 去重和 generation tracking，再开放 public refresh API。 |
| 兼容性 | 向后兼容。原有“启动前静态 `FBind` 注册”路径完全保留；只有晚于初始化发生的注册才会进入新 registry/refresh 逻辑。expert-mode 直接 `BindGlobalFunction()` 的现有用法也无需改变。 |
| 验证方式 | 1. 写一个测试模块，在 `InitializeAngelscript()` 之后才加载并注册 `FBind`，验证调用 `RefreshExtensions()` 前函数不可见、调用后函数可见。 2. 连续执行两次 refresh，确认同一 contributor 不会重复注册。 3. 复跑现有启动阶段 bind 回归，确认未使用 refresh 的项目行为完全不变。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P0 | Arch-EP26 | generated bind module 的 manifest 路径与消费 contract 断裂 | 扩展产物合同修复 | 高 |
| P1 | Arch-EP27 | `FBind` 贡献者缺少 late-join / reapply 机制，正式扩展路径被初始化时序锁死 | 扩展激活协议新增 | 高 |

---

## 架构分析 (2026-04-08 18:26)

### Arch-EP28：debug / tooling 自定义点已经进入运行时热路径，但仍停留在 process-global 单一 provider

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 调试器行为定制、debug UI 资产查询、蓝图创建辅助、spawn/component 生命周期介入 |
| 当前设计 | 这组能力不是没有扩展点，而是以 `FAngelscriptRuntimeModule` 上的 `DECLARE_DELEGATE` / `DECLARE_MULTICAST_DELEGATE` 形式散落在 runtime public header 中；其中一部分还是运行时热路径上的 strategy hook，但 provider 仍是 process-global singleton，而不是 engine-scoped service。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h:7`-`:21` 定义 `GetDynamicSpawnLevel`、`GetDebugCheckBreakOptions`、`GetDebugBreakFilters`、`GetDebugObjectSuffix`、`GetComponentCreated`、`GetDebugListAssets`、`GetEditorCreateBlueprint`、`GetEditorGetCreateBlueprintDefaultAssetPath`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp:42`-`:69` 把前五类 strategy hook 全部实现成函数内 static delegate；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp:187`-`:223` 在 `SpawnActor` 热路径里直接 `Execute()` `GetDynamicSpawnLevel()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UActorComponent.cpp:102`-`:103` 在组件创建后立即 `ExecuteIfBound()` `GetComponentCreated()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:658`-`:661`、`:1147`-`:1180` 分别在断点判定、break filter 枚举、asset 查询和 blueprint 创建消息处理中调用这些 hook；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:397`-`:405`、`:434`-`:435` 说明默认 editor 体验又是通过 editor module 在启动时临时挂 lambda 进去的。 |
| 优点 | 这套 seam 的改造成本很低，宿主 `C++ module` 理论上可以在不改插件源码的前提下改 spawn level 选择、调试器 break filter、object label suffix，或接入自定义 asset picker / blueprint 创建流程。 |
| 不足 | 这些 hook 没有 `SourceModule`、没有 per-engine owner、没有 settings manifest、也没有 `StateDump` 可视化当前 provider。更关键的是，`SpawnActor` / debugger 这种运行时关键路径被 process-global single-provider 决定，导致多引擎、多世界、PIE 与工具 world 并存时无法按上下文切换策略；脚本开发者若想改这类行为，仍然只能写一个知道内部 hook 名称的宿主 `C++ module`。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 把 runtime/tooling 能力做成显式 module / env service：`IPuertsModule` 公开 `ReloadModule()`、`InitExtensionMethodsMap()`、`SetJsEnvSelector()`，`IJSModuleLoader` 公开 `Search()` / `Load()` / `GetScriptRoot()`，`FJsEnv` 还直接暴露 `WaitDebugger()` / `ReloadModule()` 包装。 | `Reference/puerts/unreal/Puerts/Source/Puerts/Public/PuertsModule.h:17`-`:56`；`Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:80`-`:120`、`:236`-`:241`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSModuleLoader.h:17`-`:24`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JsEnv.h:38`-`:54`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnv.cpp:52`-`:82` | 关键不是“hook 多不多”，而是 debug / reload / module resolution 都有明确 service owner，且接口语义与实例边界清晰。 |
| UnLua | 把环境选择和加载策略前移到显式配置与命名 hook：`UUnLuaSettings` 允许项目配置 `EnvLocatorClass` / `ModuleLocatorClass`，`UnLuaModule` 启动时按 settings 实例化 locator；错误报告与自定义加载器则以 `FUnLuaDelegates::ReportLuaCallError` / `CustomLoadLuaFile` 明确命名。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaSettings.h:23`-`:56`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/LuaEnvLocator.h:23`-`:49`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaModule.cpp:103`-`:107`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDelegates.h:22`-`:49`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaBase.cpp:151`-`:156`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:557`-`:567` | 即使继续使用 delegate，也要把“谁来提供策略、何时安装、能改哪一段链路”产品化成 settings + 命名 contract。 |
| UnrealCSharp | 把扩展事件收口到 `FUnrealCSharpCoreModuleDelegates`，订阅者统一保存并移除 `FDelegateHandle`；listener 生命周期在析构里显式解绑。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Delegate/FUnrealCSharpCoreModuleDelegates.h:7`-`:37`；`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:33`-`:40`、`:100`-`:112` | 哪怕仍是 process-level delegate，也应至少有集中 owner、命名化 surface 和成对 teardown，而不是让 runtime hot-path hook 隐藏在 `GetXxx()` 访问器里。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把现有 debug / tooling hook 从“散落 delegate”提升成 engine-scoped `tooling service` contract，旧 delegate 保留为兼容层。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/` 新增 `IAngelscriptToolingServices`，按职责拆出 `IAngelscriptSpawnPolicy`、`IAngelscriptDebugPolicy`、`IAngelscriptEditorAssistService` 三类最小接口。 2. 在 `FAngelscriptEngineConfig` 或新的 runtime extension settings 中加入 service factory / service class 配置；`FAngelscriptEngine::Initialize()` 为每个 engine 实例创建默认 service，使 spawn/debug 决策不再天然是 process-global。 3. 让 `Bind_AActor.cpp`、`Bind_UActorComponent.cpp`、`AngelscriptDebugServer.cpp` 优先调用当前 engine 的 tooling service；现有 `GetDynamicSpawnLevel()`、`GetDebugCheckBreakOptions()`、`GetDebugBreakFilters()`、`GetComponentCreated()`、`GetEditorGetCreateBlueprintDefaultAssetPath()` 内部转发到默认 service，作为 legacy shim。 4. editor 侧把 `GetDebugListAssets()` / `GetEditorCreateBlueprint()` 迁移为 `IAngelscriptEditorAssistService` 默认实现，而不是在 `StartupModule()` 里匿名 `AddLambda()`；同时记录 `SourceModule` 和 `OwnerDomain(Runtime/Editor)`。 5. 在 `StateDump` 新增 `ToolingServices.csv`，输出当前 engine 正在使用的 spawn/debug/editor-assist provider，便于项目侧确认哪套策略正在生效。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UActorComponent.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/AngelscriptToolingServices.h/.cpp` |
| 预估工作量 | M |
| 架构风险 | 若直接把 process-global hook 替换为 engine-scoped service，可能影响 editor fallback path 与现有单例假设，尤其是 debug server 和 PIE 多实例场景；因此第一阶段必须让旧 delegate 继续工作并默认转发到 service。 |
| 兼容性 | 向后兼容。已有宿主模块继续可以绑定 `GetDynamicSpawnLevel()` / `GetDebugCheckBreakOptions()` 等旧入口；只是新路径允许更明确的 provider 安装与 per-engine 定制。 |
| 验证方式 | 1. 增加一个宿主测试模块，为两个不同 engine 安装不同的 debug / spawn service，验证 `SpawnActor` 与 break filter 返回值不会串线。 2. 复跑 debug server 与 editor create-blueprint 流程，确认默认项目在未配置 service 时行为不变。 3. 运行 `StateDump`，确认 `ToolingServices.csv` 能准确列出当前 provider 与来源模块。 |

### Arch-EP29：`StateDump` 已经形成一条成熟的 artifact 扩展 contract，但目前只服务第一方 editor 表

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 扩展点可观测性、扩展 provider 自检、第三方扩展的诊断/manifest 输出 |
| 当前设计 | `FAngelscriptStateDump::OnDumpExtensions` 是当前插件里少数已经具备“输入 contract + 生命周期管理 + 测试覆盖”的扩展点：runtime 只负责给出 `OutputDir` 并汇总结果，editor module 成对注册/解绑 dump 回调，测试也会验证扩展表是否写入摘要。但这条 lane 目前仍被 runtime 端硬编码成两张 editor CSV。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.h:18`-`:22` 定义 `FDumpExtensionsDelegate` 与 `OnDumpExtensions`；`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp:186`-`:224` 在 `DumpAll()` 中 `Broadcast(ResolvedOutputDir)`，随后硬编码检查 `EditorReloadState.csv` 与 `EditorMenuExtensions.csv` 是否存在，并把缺失视为错误；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:363`-`:364`、`:684` 在 editor module startup/shutdown 中成对注册/注销 dump extension；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorStateDump.cpp:114`-`:127` 通过 `FDelegateHandle` 管理 `AddStatic()` / `Remove()`；`Plugins/Angelscript/Source/AngelscriptTest/Dump/AngelscriptDumpTests.cpp:61`-`:74`、`:219`-`:250` 又把这两张扩展表写进期望文件列表和 `DumpSummary.csv` 校验。 |
| 优点 | 这条 seam 已经比大多数扩展点成熟得多：输出目录是显式输入、扩展结果会被 summary 汇总、注册与解绑成对、而且有自动化测试约束。也就是说，插件内部已经存在一套“可扩展且可验证的 artifact contract”。 |
| 不足 | 这条能力仍然没有产品化成通用 registry。runtime 只认识两张固定文件名，第三方扩展无法声明“我会产出哪张表、是否必需、属于哪个模块”；现有 dump 也没有拿这条 seam 去输出 `ConfiguredExtensions`、active provider、tooling service 之类更关键的扩展可观测信息。结果是最成熟的一条 extension contract 仍然被困在第一方 editor dump。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | 生成端和消费端共享同一份 manifest contract：`UnrealCSharpCore.build.cs` 把模块索引写到 `Project/Intermediate/UnrealCSharp_Modules.json`，运行时代码再从同一路径读回；围绕生成流程的事件也集中在 `FUnrealCSharpCoreModuleDelegates` 上。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/UnrealCSharpCore.build.cs:140`-`:155`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp:1265`-`:1289`、`:1302`-`:1320`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Delegate/FUnrealCSharpCoreModuleDelegates.h:14`-`:35`；`Reference/UnrealCSharp/Source/Compiler/Private/FCSharpCompilerRunnable.cpp:22`-`:39`、`:182`-`:184` | artifact/phase contract 一旦存在，就应让 producer、consumer、observer 三方都围绕同一 schema 工作，而不是 consumer 侧再硬编码固定文件名。 |
| puerts | 模块定位不靠 sidecar 文件名猜测，而是把 `Search()` / `Load()` / `GetScriptRoot()` 做成 `IJSModuleLoader` 接口，再由 `IPuertsModule` / `FJsEnv` 显式持有和调用。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSModuleLoader.h:17`-`:24`；`Reference/puerts/unreal/Puerts/Source/Puerts/Public/PuertsModule.h:41`-`:45`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnv.cpp:79`-`:86` | 与其让扩展者“写一个文件然后希望 core 知道它存在”，不如把 artifact/provider 描述注册成显式接口或 descriptor。 |
| UnLua | 宿主自定义入口由 settings 和 locator class 明确声明，`UnLuaModule` 启动时按配置实例化 `EnvLocator`；扩展者不需要猜 runtime 是否会额外寻找某个硬编码输出文件。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaSettings.h:23`-`:56`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/LuaEnvLocator.h:23`-`:49`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaModule.cpp:103`-`:107` | 如果插件已经允许外部参与某条关键流程，最好再给出显式 descriptor/class，而不是只约定“回调里自己写点文件”。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留 `OnDumpExtensions` 的前提下，引入通用 `dump extension descriptor registry`，把当前 editor 专用 dump lane 升级成所有扩展点共用的 introspection/manifest surface。 |
| 具体步骤 | 1. 在 `Dump/` 或 `Core/` 新增 `FAngelscriptDumpExtensionDescriptor` 与 `FAngelscriptDumpExtensionRegistry`，最少记录 `SourceModule`、`TableName`、`bRequired`、`Domain(Runtime/Editor/Test)`、`DumpCallback`。 2. 让 `AngelscriptEditorStateDump.cpp` 不再只是裸 `AddStatic(&DumpEditorState)`，而是在注册时同时声明 `EditorReloadState.csv` 与 `EditorMenuExtensions.csv` 的 descriptor；legacy `OnDumpExtensions` 继续保留，用于兼容旧调用。 3. 将 `AngelscriptStateDump.cpp:221`-`:222` 那种硬编码 `AddExtensionTableResult()` 替换为遍历 descriptor registry，summary 自动根据 descriptor 校验表是否生成。 4. 借这条 seam 新增 `ConfiguredExtensions.csv`、`ToolingServices.csv`、`ExtensionContributors.csv` 等表，把前面多轮审查中反复提到但尚不可见的 provider/handle/source-module 信息产品化输出。 5. 增加一个最小宿主测试扩展，注册自定义 dump table 并在测试中断言 `DumpSummary.csv` 能看到该表，从而把第三方扩展的 introspection contract 也锁进自动化。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorStateDump.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Dump/AngelscriptDumpTests.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDumpExtensionRegistry.h/.cpp` |
| 预估工作量 | M |
| 架构风险 | 主要风险是改 summary 生成逻辑时打破现有 dump tests 或既有 CSV 命名约定；因此第一阶段必须保持旧表名、旧 `OnDumpExtensions` 回调签名和当前 editor 表全部兼容。 |
| 兼容性 | 向后兼容。现有 `OnDumpExtensions`、`EditorReloadState.csv`、`EditorMenuExtensions.csv` 和 dump tests 都可以继续工作；registry 只是把这条 lane 泛化，让更多扩展点能复用。 |
| 验证方式 | 1. 复跑现有 `AngelscriptDumpTests`，确认旧表名和 summary 状态不变。 2. 新增一个第三方 dump extension 测试，验证注册自定义 descriptor 后 summary 会自动出现对应表项。 3. 运行一次完整 dump，确认新增的 `ConfiguredExtensions.csv` / `ToolingServices.csv` 能直接反映当前扩展 provider 状态。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-EP28 | debug / tooling hook 已进入运行时热路径，但仍是 process-global 单一 provider | service contract 新增 | 高 |
| P2 | Arch-EP29 | `StateDump` 已有成熟 artifact seam，但只服务第一方 editor 表 | introspection contract 泛化 | 中高 |

---

## 架构分析 (2026-04-08 18:38)

### Arch-EP30：插件型扩展包的代码载体与脚本载体被拆成两种互不兼容的插件形态

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 第三方想把 `C++ bind module`、`Script/` 扩展脚本和自动生成产物一起打成一个可复用插件时，当前插件是否提供统一的扩展包 contract |
| 当前设计 | 重新核对 `Script root` 发现和生成插件模板后可以确认：runtime 只把 `CanContainContent` 的已启用插件视为 `Script/` 提供者，但 editor 生成的 bind 插件模板又固定写成 `CanContainContent = false`。同时，生成器把 `BindModules.Cache` 写到项目 `Script/` 根，而不是写回生成插件自身。这导致“能被自动发现脚本”的插件形态和“能承载自动生成 bind module”的插件形态被拆开了。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:558`-`:565` 通过 `IPluginManager::Get().GetEnabledPluginsWithContent()` 收集 `Plugin->GetBaseDir() / "Script"`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1347`-`:1360` 说明 `DiscoverScriptRoots()` 只把这些 content plugin root 加入搜索列表；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1077` 把 `BindModules.Cache` 保存到 `FAngelscriptEngine::GetScriptRootDirectory() / "BindModules.Cache"`，也就是项目 `Script/` 根；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:2316` 与 `Plugins/Angelscript/Angelscript.uplugin:14` 又分别把生成插件模板和主插件都固定为 `"CanContainContent": false`。 |
| 优点 | 当前实现简单直接：项目主 `Script/` 根始终是中心，generated bind module 也不会意外把内容资产带进自动生成插件。 |
| 不足 | 对扩展作者来说，这意味着“脚本扩展包”和“bind 扩展包”不是同一种产品形态。想发布一个同时含 `Script/` 与 bind module 的插件时，现有生成模板不能直接用，runtime 也没有显式 descriptor 告诉它应该从哪里读脚本、从哪里读 bind module manifest。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 明确把可复用扩展包建模成 `UnLuaExtensions` 内容插件。editor 启动时扫描 `CanContainContent` 且路径含 `UnLuaExtensions` 的插件，把它们的 `Content/Script` 自动加入 `DirectoriesToAlwaysStageAsUFS`；扩展模块启动时再把自己的脚本路径追加进 `UnLua.PackagePath`。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:188`-`:227`；`Reference/UnLua/Plugins/UnLuaExtensions/LuaSocket/LuaSocket.uplugin:13`；`Reference/UnLua/Plugins/UnLuaExtensions/LuaSocket/Source/Private/LuaSocketModule.cpp:32`-`:36` | “扩展插件长什么样”是显式产品 contract，而不是让作者自行猜 `uplugin` 和脚本根应该怎样组合。 |
| puerts | 不把脚本根写死在插件形态里，而是通过 `IJSModuleLoader` / `DefaultJSModuleLoader` 把 `ScriptRoot` 变成正式接口；默认实现也明确从 `ProjectContentDir()/ScriptRoot` 搜索模块。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSModuleLoader.h:17`-`:42`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DefaultJSModuleLoader.cpp:118`-`:143` | 即使继续允许多种扩展包形态，也应该先给出显式 loader/root contract，而不是把发现逻辑散落在插件内容标记和若干缓存文件里。 |
| UnrealCSharp | 外部模块参与范围通过 `ExportModule` / `ClassBlacklist` 和 `UnrealCSharp_Modules.json` manifest 显式声明，producer 与 consumer 围绕同一份模块清单工作。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpEditorSetting.h:144`-`:148`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/UnrealCSharpCore.build.cs:142` | 当前插件未必需要照搬其生成链，但“扩展包包含哪些模块/产物”至少应落到统一 manifest，而不是拆成互不相认的两套约定。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 为 `Angelscript` 扩展插件补一个正式的 `extension package descriptor`，统一声明脚本根、bind module manifest 和内容承载能力，保留现有 project-root fallback 兼容。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/` 新增 `FAngelscriptExtensionPackageDescriptor` 或等价 manifest helper，至少记录 `PackageName`、`ScriptRoots`、`BindModulesManifest`、`bCanContainScripts`、`bContainsEditorOnlyModules`。 2. 让 `FAngelscriptEngineDependencies::CreateDefault()` 不再只依赖 `GetEnabledPluginsWithContent()`；优先读取 descriptor 中声明的脚本根，旧的 content-plugin 扫描作为 legacy fallback。 3. 给 `GeneratePluginDirectory()` 增加显式选项，支持生成 `CanContainContent = true` 的扩展插件模板，并同步写出 bind module manifest 路径，而不是只把 `BindModules.Cache` 落到项目 `Script/` 根。 4. 在 editor 侧增加脚本目录 staging helper：若扩展插件声明了 `ScriptRoots`，则自动把对应目录加入打包设置或生成明确 warning。 5. 在 `StateDump` 新增 `ExtensionPackages.csv`，输出每个扩展插件的 `ScriptRoots`、`BindModulesManifest`、`CanContainContent` 与实际命中的发现来源。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`、`Plugins/Angelscript/Angelscript.uplugin`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptExtensionPackageDescriptor.h/.cpp` |
| 预估工作量 | M-L |
| 架构风险 | 最大风险是把现有“项目 `Script/` 根为中心”的假设改得过快，导致老项目、generated bind module 和 cook/stage 规则出现路径回归。因此第一阶段必须保留 project-root 与 `GetEnabledPluginsWithContent()` 的 legacy fallback，并把实际命中的路径写进 dump/log。 |
| 兼容性 | 向后兼容。现有项目继续可以只用项目 `Script/` 根和旧 `BindModules.Cache`；新 descriptor 只是为扩展插件补齐正式 contract。 |
| 验证方式 | 1. 新建一个同时包含 `Script/` 与 generated bind module 的宿主扩展插件，验证无需改插件源码即可被 runtime 发现并加载。 2. 在 editor/cooked 场景分别验证脚本根和 bind module manifest 都能命中，且 `StateDump` 能看到来源插件。 3. 保留一个 legacy content-plugin + project-root `BindModules.Cache` 案例，确认旧路径仍正常工作。 |

### Arch-EP31：扩展 contract 缺少 public-boundary 回归与宿主演示，当前 hook 主要靠插件自用验证

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 当前公开的扩展点是否真的被“插件外部消费者”持续验证，还是主要由同仓库内部模块和白盒测试自用 |
| 当前设计 | 这轮重查后可以确认：仓内对扩展 hook 的主要消费者仍是插件自己。宿主工程 `Source/AngelscriptProject` 并没有依赖 `AngelscriptRuntime`/`AngelscriptEditor`，因此没有第一方宿主侧样例；现有 hook 的使用主要出现在插件内部模块和 learning test 里，例如 preprocessor hook 只在 `AngelscriptLearningPreprocessorTraceTests.cpp` 白盒绑定，dump/reload/compile hook 也主要被 editor/runtime 自己消费。换句话说，当前 extensibility 更像“内部可用的工程缝”，还没有形成经过公共边界回归的产品 contract。 |
| 源码证据 | `Source/AngelscriptProject/AngelscriptProject.Build.cs:11`-`:13` 只依赖 `Core/CoreUObject/Engine`，没有把 `AngelscriptRuntime` 拉进宿主模块；`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp:123`-`:134` 通过 `OnProcessChunks` / `OnPostProcessCode` 做白盒 hook 测试；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorStateDump.cpp:118`-`:126` 由 editor 模块自己注册/注销 `OnDumpExtensions`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Console.h:24`-`:44` 用 `GetOnInitialCompileFinished()` 做 runtime 内部延迟初始化；`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h:52`-`:139` 由 editor helper 直接消费 `OnClassReload` / `OnPostReload`；`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorPrompts.cpp:402`-`:460` 对 `BindGlobalFunction()` 的第一方使用也仍在插件内部。 |
| 优点 | 这说明扩展点并非纸面 API，插件内部确实在依赖它们做真实功能；短期内改动也很快，因为调用方和被调用方在同一仓库里一起演化。 |
| 不足 | 只靠同仓库自用，无法证明“外部模块只 include 正式 public header 就能稳定扩展”。一旦 public include tree、module dependency 或初始化时序回归，内部模块可能仍能编译通过，而真实第三方扩展者会先踩坑。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 直接在宿主示例工程里演示 public contract：`UTPSGameInstance` 通过实现 `IUnLuaInterface::GetModuleName()` 接入脚本，`UTutorialBlueprintFunctionLibrary::SetupCustomLoader()` 则在 sample 工程里绑定/解绑 `FUnLuaDelegates::CustomLoadLuaFile`。 | `Reference/UnLua/Source/TPSProject/TPSGameInstance.h:23`-`:31`；`Reference/UnLua/Source/TPSProject/TutorialBlueprintFunctionLibrary.cpp:91`-`:103` | 最可靠的扩展 contract，不只是核心模块里“理论上可用”，还要有宿主侧样例持续证明“外部项目这样接就能跑”。 |
| UnrealCSharp | public delegate 不只是声明出来，而是被不同模块的真实 consumer 持续订阅和 teardown。`FEditorListener` 跨模块绑定 `OnBeginGenerator` / `OnEndGenerator` / `OnCompile`，析构时再显式 `Remove()` 这些 handle。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:33`-`:39`、`:102`-`:112` | cross-module consumer 本身就是 contract test；只要 consumer 和 provider 不在同一实现单元，API 漂移更容易被及时发现。 |
| puerts | `UExtensionMethods` 作为公开 base class，被 runtime 的 `JsEnvImpl` 和 editor 侧 `DeclarationGenerator` 同时消费：一边建立运行时扩展方法表，一边生成声明。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/ExtensionMethods.h:19`；`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:323`-`:351` | 同一条 public extension contract 同时服务多个模块，能自然逼出更稳定的边界和更少的 accidental include。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 为扩展点新增 `public-only` 的宿主样例与黑盒回归，把“插件自己会用”和“外部用户真的能用”拆成两条独立验证链。 |
| 具体步骤 | 1. 在仓库中新增一个独立的宿主扩展示例模块或插件，例如 `Samples/AngelscriptExtensionSample`，只允许 include 正式 public header，并覆盖四个关键场景：自定义全局函数、自定义类型 finder、编译/热重载 observer、脚本根/扩展包声明。 2. 新增一组 black-box 自动化测试，直接加载这个示例模块，验证它不修改 `Plugins/Angelscript/` 源码也能完成上述扩展；不要再只在 `AngelscriptTest` 内部直接摸 static delegate。 3. 把 `Bind_Console.h`、`ClassReloadHelper.h`、`AngelscriptEditorStateDump.cpp` 这类第一方 self-consumer 提炼成文档化示例，明确哪些是“内部参考实现”，哪些是“推荐外部做法”。 4. 在 CI 中增加 `public-only extension build` 目标，要求示例模块不能 include `Core/`、`ClassGenerator/` 或 third-party internal header；一旦 public surface 不足，就让 CI 直接失败。 5. 在 `StateDump` 新增 `ExternalExtensionConsumers.csv` 或等价表，列出通过正式 public contract 注册的外部 contributor，便于样例、测试和真实项目共享同一观察面。 |
| 涉及文件 | `Source/AngelscriptProject/AngelscriptProject.Build.cs`、`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorStateDump.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Console.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h`、新增 `Samples/AngelscriptExtensionSample/*`、新增 public-only CI/验证目标文件 |
| 预估工作量 | M |
| 架构风险 | 第一阶段最大的“风险”其实是把当前 public surface 的缺口暴露出来，例如需要 internal header 才能完成常见扩展，或初始化时序仍依赖插件私有知识。这会带来短期修补成本，但正是这条回归链的价值所在。 |
| 兼容性 | 向后兼容。新增的是样例、测试和 CI 约束，不会改变现有扩展行为；只是未来任何 API 调整都要同时通过这条 public-boundary 回归。 |
| 验证方式 | 1. 让 `Samples/AngelscriptExtensionSample` 在空白宿主工程中编译通过，并确认只使用正式 public 头。 2. 运行 black-box 测试，验证 sample 能成功注册全局函数、类型 finder 和 compile observer。 3. 故意移除一条 public include 或改变 delegate 签名，确认新 CI 目标会优先失败，而不是等真实用户反馈。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-EP30 | 插件型扩展包被拆成 code-only bind plugin 与 content-bearing script plugin 两种形态 | 扩展包 contract 收口 | 高 |
| P2 | Arch-EP31 | 扩展 contract 缺少 public-boundary 回归与宿主演示，现有 hook 主要靠插件自用验证 | 样例与黑盒验证补齐 | 中高 |

---

## 架构分析 (2026-04-08 18:50)

### Arch-EP32：脚本 `specifier / metadata` 语言仍是 closed-world parser，第三方无法扩展新的注解 contract

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 用户是否能在不改插件源码的前提下，为 `UCLASS / UFUNCTION / UPROPERTY` 新增自定义 `specifier` 或把自定义 metadata 接入编译语义 |
| 当前设计 | 本轮回看 preprocessor 后可以确认，`UAngelscriptSettings` 只负责提供默认值和 flag，真正的脚本注解语义仍由 `FAngelscriptPreprocessor` 内部硬编码的 `PP_NAME_*` 表和 `if/else` 分派决定。对外公开的 only hook 仍是整轮级别的 `OnProcessChunks / OnPostProcessCode`，它们发生在 `ProcessMacros()` 和 `ProcessDelegates()` 之后，无法把“新增一种注解语义”产品化为外部扩展。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h:29`-`:30` 只公开 `OnProcessChunks` 与 `OnPostProcessCode`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:40`-`:56` 只是从 `UAngelscriptSettings` 拷贝 `PreprocessorFlags`、默认 property/function specifier；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:2250`-`:2338` 把 `Config`、`DefaultConfig`、`Blueprintable` 等 class specifier 写死在 `PP_NAME_*` 与 `ProcessClassMacro()` 分支里；同文件 `:2411`-`:2728` 又以同样方式硬编码 property specifier 语义；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:265`-`:287` 表明公开 hook 发生在宏和 delegate 处理之后。 |
| 优点 | 语义集中在一个 parser/translator 内，编译结果稳定，可预测性强；默认 specifier 与 warning 行为也能通过 `UAngelscriptSettings` 统一收敛。 |
| 不足 | 想给脚本语言新增项目级注解，例如自定义 `UFUNCTION(...)` 语义、额外 class/property trait、或对 metadata 做定制翻译，当前仍基本等价于修改插件源码。换句话说，settings 能改“参数值”，不能改“注解语法表”。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 不把“扩展语义”做成 parser keyword，而是把用户 authoring model 收口为 `UExtensionMethods` 基类。运行时 `JsEnvImpl` 和 editor `DeclarationGenerator` 都扫描同一类 family，并按首参数类型建立扩展方法表。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/ExtensionMethods.h:18`-`:20`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:940`-`:983`；`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:323`-`:358` | 扩展 contract 建立在显式 base class 和共享 discovery 规则上，而不是把新语义塞进核心 parser。 |
| UnLua | 宿主扩展点前移到 typed class contract：`UUnLuaSettings` 声明 `EnvLocatorClass`、`ModuleLocatorClass`、`PreBindClasses`，模块启动时按配置实例化 locator 并驱动预绑定，而不是要求宿主去扩展 Lua parser。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaSettings.h:47`-`:56`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaModule.cpp:103`-`:128` | 优先把“新行为”建模为可替换对象，再由核心流程消费，而不是继续扩写硬编码 specifier 表。 |
| UnrealCSharp | 通过 `bEnableExport`、`ExportModule`、`ClassBlacklist` 和 `OnBeginGenerator / OnEndGenerator / OnCompile` 把生成期扩展做成 settings + phase delegates，允许其它模块在稳定阶段接入。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpEditorSetting.h:136`-`:148`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/UnrealCSharpCore.build.cs:128`-`:149`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Delegate/FUnrealCSharpCoreModuleDelegates.h:7`-`:35` | 即便仍由核心负责主流程，也可以把“阶段”和“过滤条件”开放为正式 contract，避免每次扩展都回到核心源码里加分支。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留现有 `PP_NAME_*` 兼容语义的前提下，为脚本注解翻译新增一层 `specifier / metadata handler registry`，先开放增量式扩展，再考虑覆盖式策略。 |
| 具体步骤 | 1. 在 `Preprocessor/` 或新的 `Public/Extension/` 下新增 `IAngelscriptSpecifierExtension`，最小接口先包含 `HandleClassSpecifier()`、`HandleFunctionSpecifier()`、`HandlePropertySpecifier()`、`HandleMetadata()` 和 `GetPriority()`。 2. 让 `FAngelscriptPreprocessor` 先执行现有内建 handler；当遇到未知 specifier 或 extension-only metadata 时，再把 `FSpecifier`、`ClassDesc/FunctionDesc/PropertyDesc` 视图交给 registry。第一阶段只允许“消费未知 specifier + 追加 meta/trait”，不允许改写现有 built-in 语义。 3. 在 `FAngelscriptRuntimeModule` 或新的 extension registry 中增加 `RegisterSpecifierExtension()`，并支持通过 settings/module manifest 声明要加载哪些 specifier extension module。 4. 在 `StateDump` 新增 `SupportedSpecifiers.csv` 或 `SpecifierExtensions.csv`，记录每个 specifier 由哪个 handler 消费，避免用户继续靠 `grep PP_NAME_` 理解语言能力边界。 5. 增加一个最小宿主扩展测试，验证外部模块注册新的 property/function annotation 后，`ClassDesc`/`PropertyDesc` 会得到预期 trait，且 legacy 脚本不受影响。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/AngelscriptSpecifierExtension.h`、新增对应测试/样例文件 |
| 预估工作量 | M |
| 架构风险 | 最大风险是 extension handler 与内建 `PP_NAME_*` 语义发生优先级竞争，导致老脚本行为漂移。因此第一阶段必须限定为 additive-only，并在 dump/log 中输出实际 consumer。 |
| 兼容性 | 向后兼容。现有所有 built-in specifier 和 metadata 解释规则保持不变；新 registry 只为外部模块补一个正式入口。 |
| 验证方式 | 1. 新增宿主扩展模块，为自定义 class/property/function annotation 注册 handler，验证无需改插件源码即可生效。 2. 复跑已有 preprocessor/class generator 测试，确认 legacy specifier 行为不变。 3. 运行 `StateDump`，确认新表能清楚列出哪些 specifier 由 core 消费、哪些由外部 extension 消费。 |

### Arch-EP33：脚本类已经支持 `Config / DefaultConfig` 自定义，但这条配置能力没有上升为扩展 provider contract

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 用户通过 `ini/settings` 自定义脚本行为时，当前插件是否把“脚本配置类”和“扩展 provider 配置”统一成一条正式 contract |
| 当前设计 | 本轮补查后可以确认，脚本作者其实已经拥有一条比 `UAngelscriptSettings` 更贴近业务的可定制路径：preprocessor 支持 `Config`、`DefaultConfig` 和 property 级 `Config`，class generator 会把它们落成 `CLASS_Config`、`CLASS_DefaultConfig`、`CPF_Config`，运行时还额外绑定了 `SaveConfig()` / `LoadConfig()` / `ReloadConfig()`。这意味着脚本类本身可以成为 `ini` 驱动对象。但这条 lane 仍只服务“脚本 UObject/UStruct 的持久化配置”，并没有延伸到 bind contributor、type provider、import resolver、compile observer 这类扩展 provider。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:2259`-`:2260` 定义 `PP_NAME_Config` 与 `PP_NAME_DefaultConfig`；同文件 `:2320`-`:2330` 把 class-level `Config` / `DefaultConfig` 写入 `ClassDesc`，`:2623`-`:2625` 把 property-level `Config` 写入 `PropDesc->bConfig`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3020`-`:3021`、`:3294`-`:3307` 再把这些描述翻译成 `CPF_Config`、`CLASS_Config`、`CLASS_DefaultConfig`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp:109`-`:124` 公开了 `SaveConfig()` / `LoadConfig()` / `ReloadConfig()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp:371`-`:403` 只输出 engine-wide `RuntimeConfig.csv`，而 `:517`-`:552` 仅在属性表里输出 `bConfig`，没有“哪个扩展 provider 使用哪份设置”的对应关系。 |
| 优点 | 对脚本业务层来说，这条能力已经相当实用：脚本类能直接接入 UE 原生 config 系统，不需要额外桥接层；同时现有 dump 也至少能看到 runtime flag 和 property-level `bConfig`。 |
| 不足 | 这导致当前仓库出现两条互不连通的配置面：一条是脚本类的 `Config/DefaultConfig`，另一条是插件扩展者仍需手写 `C++` 注册与全局 settings 读取。脚本作者可以配置脚本对象，却不能用同样的 contract 去配置“哪类 extension provider 被启用、provider 用什么参数、provider 对应哪个 config section”。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 把 runtime 可替换对象直接声明到 `UUnLuaSettings`：`EnvLocatorClass`、`ModuleLocatorClass`、`PreBindClasses` 都是 typed settings，模块启动时按配置实例化 locator 并执行预绑定。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaSettings.h:47`-`:56`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaModule.cpp:103`-`:128` | “用户配置什么 provider”与“runtime 怎样消费它”属于同一条正式配置面，而不是一边是脚本 config class，一边是手写注册。 |
| UnrealCSharp | 生成导出范围同样由 typed settings 驱动：`bEnableExport`、`ExportModule`、`ClassBlacklist` 出现在 `UUnrealCSharpEditorSetting`，`Build.cs` 直接读取配置决定 `WITH_BINDING` 和模块清单生成。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpEditorSetting.h:136`-`:148`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/UnrealCSharpCore.build.cs:128`-`:149` | provider/filter config 不一定非得等到运行时才生效，但至少 producer 和 consumer 应共享同一份 settings contract。 |
| puerts | 把 provider 自身收口成显式对象接口：`IPuertsModule` 公开 `SetJsEnvSelector()` / `InitExtensionMethodsMap()`，`IJSModuleLoader` 公开 `Search()` / `Load()` / `GetScriptRoot()`。自定义点不依赖脚本类的 `Config` flag，而依赖正式 provider object。 | `Reference/puerts/unreal/Puerts/Source/Puerts/Public/PuertsModule.h:17`-`:49`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSModuleLoader.h:17`-`:50` | 与其让扩展配置散落在脚本 class flag 和若干全局变量里，不如让 provider 自己有明确的 settings/object 身份。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 保留现有脚本 `Config/DefaultConfig` 完全兼容，同时补一条“extension provider 也可声明/消费 typed settings”的正式 contract，把脚本配置能力延伸到扩展层。 |
| 具体步骤 | 1. 在 `Public/Extension/` 新增 `UAngelscriptExtensionSettingsBase` 或 `IAngelscriptConfiguredExtension`，允许 bind contributor、type provider、import resolver、compile observer 声明自己的 settings class 或 config section。 2. 在 runtime extension registry 中增加 `GetSettingsObject()` 或等价注入路径：若 provider 声明 settings，则在 `FAngelscriptEngine::Initialize()` / `FAngelscriptRuntimeModule::StartupModule()` 里加载并传入；若未声明，则继续走 legacy 无配置路径。 3. 复用现有 dump 能力，新增 `ScriptConfigSchemas.csv` 和 `ConfiguredExtensionProviders.csv`，分别列出脚本类 `ConfigName / DefaultConfig / bConfig properties`，以及每个 provider 对应的 settings class、config section、source module。 4. 在 editor 侧增加校验：当 provider 声明了 settings class 但模块未加载、class 不实现预期接口或 config section 缺失时，给出明确 warning，而不是静默 fallback。 5. 增加一个最小宿主样例，让外部模块用独立 settings class 控制一个自定义 global bind 或 type provider 的启停，证明“扩展 provider 配置”不再需要改插件源码。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/AngelscriptExtensionSettings.h`、新增样例/测试文件 |
| 预估工作量 | M |
| 架构风险 | 主要风险是把 runtime provider 的配置加载时机做得过早或过晚，导致 engine 初始化顺序与 editor settings 生命周期打架；第一阶段应限定为 optional settings object，不改变既有 provider 初始化顺序。 |
| 兼容性 | 向后兼容。现有 `Config/DefaultConfig` 脚本类、`UAngelscriptSettings`、`FAngelscriptEngineConfig` 和所有无配置 provider 均可继续工作；新 contract 只是给扩展 provider 增量补一个正式设置面。 |
| 验证方式 | 1. 新建一个声明 settings class 的宿主扩展 provider，验证无需改插件源码即可按 `ini` 开关全局函数或类型绑定。 2. 复跑已有脚本 config 相关流程，确认 `Config/DefaultConfig`、`SaveConfig()` / `LoadConfig()` / `ReloadConfig()` 行为不变。 3. 运行 `StateDump`，确认脚本配置 schema 与扩展 provider 配置都能被独立观察到。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-EP32 | 脚本 `specifier / metadata` 语法缺少正式扩展入口 | compiler extension contract 新增 | 高 |
| P2 | Arch-EP33 | `Config / DefaultConfig` 能配置脚本类，但没有上升为扩展 provider 设置面 | settings contract 收口 | 中高 |

---

## 架构分析 (2026-04-08 18:58)

### Arch-EP34：扩展总线主要挂在 concrete module / global static 上，缺少稳定的 `IModule` contract

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 外部插件在不依赖 concrete implementation 细节的前提下，能否稳定访问 runtime/editor 扩展总线 |
| 当前设计 | 当前扩展面主要通过 concrete 的 `FAngelscriptRuntimeModule`、`FAngelscriptClassGenerator`、`FAngelscriptStateDump` 暴露：前者是 `FDefaultModuleImpl` 子类，公开一组 static getter；后两者直接公开 public static delegate。扩展消费者实际上是在直接耦合这些 concrete/global 类型，而不是通过一个稳定 `IAngelscript...` interface。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h:25`-`:49` 将扩展入口定义在 concrete `FAngelscriptRuntimeModule` 上；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp:42`-`:136` 用函数内 static delegate 保存这些 hook；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h:21`-`:38` 公开 `OnClassReload` / `OnPostReload` 等 public static delegate；`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.h:18`-`:23` 公开 `OnDumpExtensions`；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorStateDump.cpp:114`-`:127` 直接对这个 global delegate 做 `AddStatic` / `Remove`。 |
| 优点 | 访问成本低，内部模块几乎不需要额外 plumbing；函数内 static delegate 也避免了 module instance 生命周期传递。 |
| 不足 | 对外 contract 很难演进。新增或重排 hook 时，外部插件只能继续 include concrete header 并假设这些 global/static 入口长期存在；同时 runtime、reload、state dump 三套扩展面各自暴露在不同 concrete/global owner 上，缺少统一 discoverability 和 versioning 边界。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 先定义 `IPuertsModule` interface，再由模块实现它；外部只依赖 `Get()` / `IsAvailable()` 和 virtual API，如 `ReloadModule()`、`InitExtensionMethodsMap()`、`SetJsEnvSelector()`。 | `Reference/puerts/unreal/Puerts/Source/Puerts/Public/PuertsModule.h:17`-`:55` | 把 public contract 固定在 interface，而不是 concrete module class。 |
| UnLua | 同样通过 `IUnLuaModule` 暴露 `GetEnv()`、`SetActive()`、`HotReload()`；扩展 delegate 虽然也是 static，但 VM 生命周期和宿主访问入口都先收口到 module interface。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaModule.h:18`-`:32`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDelegates.h:22`-`:49` | 即便保留 global delegates，也可以先把主入口收口到 `IModule` 边界。 |
| UnrealCSharp | 不要求外部碰 concrete module，而是把 phase hook 收敛到独立的 `FUnrealCSharpCoreModuleDelegates`；消费者通过 handle 订阅/解绑，`FEditorListener` 不需要感知 core module 的具体实现类。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Delegate/FUnrealCSharpCoreModuleDelegates.h:7`-`:35`；`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:33`-`:40`、`:102`-`:112` | 可以把“模块实现”与“公共 delegate 总线”拆开，减少 ABI 漂移对扩展者的影响。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 保留现有 static API 兼容层，但新增正式 `IAngelscriptRuntimeModule` / `IAngelscriptEditorExtensibility` contract，把 concrete/global 扩展面收口成可版本化接口。 |
| 具体步骤 | 1. 在 `AngelscriptRuntime/Public/` 新增 `IAngelscriptRuntimeModule`，提供 `Get()` / `IsAvailable()` 以及 `GetPreCompileDelegate()`、`GetPostCompileDelegate()`、`GetClassAnalyzeHook()`、`GetReloadEvents()` 等 accessor。 2. 让 `FAngelscriptRuntimeModule` 实现该 interface；现有 `FAngelscriptRuntimeModule::GetXxx()` 保留，但内部只做 forward，避免直接暴露更多 concrete/global 细节。 3. 为 `FAngelscriptClassGenerator` 和 `FAngelscriptStateDump` 新增轻量 facade，例如 `IAngelscriptReloadService` / `IAngelscriptDiagnosticsService`，第一阶段只包已有 delegate，不改触发时机。 4. 迁移一个第一方消费者作为样板，例如把 `AngelscriptEditorStateDump.cpp` 从直接访问 `FAngelscriptStateDump::OnDumpExtensions` 改成通过 facade 注册，验证接口足够。 5. 新增一个 public-only 宿主样例和编译测试，要求外部模块只 include interface header 即可订阅 compile/reload/state-dump 扩展面。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorStateDump.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/IAngelscriptRuntimeModule.h`、新增 facade 头文件 |
| 预估工作量 | M |
| 架构风险 | 风险主要在接口拆分时把现有 static/global 生命周期包错层，导致 editor 模块在 runtime 模块尚未完成初始化时提前取用服务。第一阶段应只做 facade，不改变 delegate 实际存储位置。 |
| 兼容性 | 向后兼容。旧的 `FAngelscriptRuntimeModule::GetXxx()`、`FAngelscriptClassGenerator::OnXxx`、`FAngelscriptStateDump::OnDumpExtensions` 可以继续保留一个版本周期，并标注推荐迁移路径。 |
| 验证方式 | 1. 新建外部样例模块，只 include 新 interface 头并订阅 compile/reload hook，验证无需 include concrete implementation header。 2. 复跑 editor state dump 与 reload 相关测试，确认 facade 不改变触发时机。 3. 卸载/重载 `AngelscriptEditor` 时检查 delegate handle 能正常解绑，没有悬空 static consumer。 |

### Arch-EP35：真正改变行为的 hook 仍是单占式 `DECLARE_DELEGATE`，多个扩展插件无法组合

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 多个外部插件是否能同时接管 `spawn level`、`debug filter`、`class analyze`、`default asset path` 这类行为型扩展点 |
| 当前设计 | 当前 repo 里真正会改变核心行为的 hook，大多不是 multicast，而是 `DECLARE_DELEGATE` 单播接口。按这类 delegate 的语义推断，同一时刻只有一个 bound callable，因此这些 hook 天然是“单 owner”而不是“可组合 provider”。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h:9`-`:14`、`:21` 将 `GetDynamicSpawnLevel`、`DebugCheckBreakOptions`、`GetDebugBreakFilters`、`DebugObjectSuffix`、`ClassAnalyze`、`EditorGetCreateBlueprintDefaultAssetPath` 都定义为 `DECLARE_DELEGATE...`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp:187`-`:190`、`:220`-`:223` 直接用 `GetDynamicSpawnLevel().Execute()` 决定 `SpawnActor` 的 `OverrideLevel`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:658`-`:664` 用 `GetDebugCheckBreakOptions().Execute()` 决定断点是否生效；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:433`-`:435` 用 `GetEditorGetCreateBlueprintDefaultAssetPath().Execute()` 决定默认资源路径；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:1310`-`:1311` 用 `GetClassAnalyze().Execute()` 改写 class 分析结果；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FString.cpp:439`-`:443` 用 `GetDebugObjectSuffix().Execute()` 追加调试后缀。 |
| 优点 | 决策路径直接，优先级没有歧义；对当前单宿主项目来说，实现和调试都很省心。 |
| 不足 | 一旦项目里出现两个以上扩展插件，这些点就无法自然组合。例如一个插件想改 `spawn level`，另一个插件想给 debug object 补 suffix，问题不大；但两个插件都想参与 `class analyze` 或 `default asset path` 时，当前 contract 没有链式仲裁、优先级或 merge 语义，扩展共存能力明显弱于 compile/reload 这类 multicast observer。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 虽保留 legacy 的 `CustomLoadLuaFile` 单播 delegate，但同时在 `FLuaEnv` 上正式支持 `AddLoader()`，并在加载阶段遍历 `CustomLoaders` 数组，允许多个 loader 逐个尝试。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDelegates.h:33`-`:49`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/LuaEnv.h:115`-`:128`、`:163`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:526`-`:528`、`:576`-`:594` | 即便历史上已有单播 hook，也可以增量补一条 composable loader/provider 链。 |
| puerts | 把模块解析收口成 `IJSModuleLoader` strategy object，并把 `Search()` / `Load()` / `CheckExists()` 做成 virtual；默认 loader 内部还拆出 `SearchModuleInDir()` 等层次，便于宿主包装或替换。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSModuleLoader.h:17`-`:50`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnv.cpp:19`-`:25`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DefaultJSModuleLoader.cpp:53`-`:115` | 对行为型扩展，与其暴露一个全局单播 callback，不如暴露 strategy/provider object。 |
| UnrealCSharp | 对非决策型阶段 hook，统一使用 multicast delegate，并要求 consumer 保存/移除 handle；这让多个 editor/runtime consumer 能稳定共存。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Delegate/FUnrealCSharpCoreModuleDelegates.h:10`-`:35`；`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:33`-`:40`、`:102`-`:112` | 如果某个扩展点本质上是“多人观察”而不是“单人决策”，就不该继续用单播 delegate 伪装成扩展面。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把行为型 hook 按“可组合 provider”与“必须单选 strategy”两类拆开；前者改成有优先级/返回 `Handled` 的 provider registry，后者改成显式 strategy object，而不是继续复用单播 delegate。 |
| 具体步骤 | 1. 先挑两个高价值点试点：`GetClassAnalyze()` 和 `GetEditorGetCreateBlueprintDefaultAssetPath()`。新增 `RegisterClassAnalyzeProvider()` / `RegisterBlueprintAssetPathProvider()`，返回 `FDelegateHandle`，支持 priority 和 `bool bHandled` 语义。 2. 对 `GetDynamicSpawnLevel()`、`GetDebugCheckBreakOptions()` 这类更接近“唯一策略”的点，新增 `IAngelscriptSpawnPolicy` / `IAngelscriptDebugBreakPolicy`，由 settings 或 module registry 指定当前策略对象，避免“看似可扩展、实则只能绑定一次”的误导。 3. 对 `GetDebugBreakFilters()`、`GetDebugObjectSuffix()` 这类天然可 merge 的点，改成 reducer：前者合并 `TMap/FName` 结果，后者按 provider 顺序追加字符串。 4. 复用当前代码库已存在的 handle 化实践，把注册/解绑生命周期做成和 `FAngelscriptStateDump::OnDumpExtensions` 一样的 `Add/Remove` 模式，降低改造心智负担。 5. 在 `StateDump` 新增 `ActivePolicies.csv` 与 `BehaviorProviders.csv`，列出每个行为点当前绑定的是单一 strategy 还是 provider 链，以及 provider 的 priority/source module。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`、新增 `Public/Extension/AngelscriptBehaviorPolicy.h` 或等价文件 |
| 预估工作量 | M |
| 架构风险 | 最大风险是从“只有一个 owner”迁移到“多个 provider”后，默认执行顺序和冲突诊断会成为新复杂度来源。因此第一阶段应只试点 1-2 个 hook，并在 log/dump 中打印最终命中的 provider。 |
| 兼容性 | 向后兼容。现有单播 getter 可暂时保留，并在内部桥接为 priority 最高或默认 policy；旧扩展者无需立刻改代码。 |
| 验证方式 | 1. 编写两个独立宿主扩展模块，同时注册 `class analyze` 或 blueprint asset path provider，验证两者能按 priority/`Handled` 规则共存。 2. 复跑 `SpawnActor`、debug break、preprocessor 分析相关测试，确认默认无 provider 时行为完全不变。 3. 运行 `StateDump`，确认能看到哪些行为点仍是 legacy single-policy，哪些已经迁移为 provider 链。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-EP35 | 行为型 hook 仍是单占式 `DECLARE_DELEGATE`，多扩展插件无法组合 | 扩展点语义重构 | 高 |
| P2 | Arch-EP34 | 扩展总线主要挂在 concrete module / global static 上，缺少稳定 `IModule` contract | 公共接口收口 | 中高 |

---

## 架构分析 (2026-04-08 19:06)

### Arch-EP36：editor 扩展能力通过 `AngelscriptRuntime` 公共总线侧向暴露，`Runtime/Editor` 能力域没有真正分层

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 外部扩展者能否清晰区分 runtime-safe 扩展点与 editor-only 扩展点，并在不引入 editor 依赖的情况下稳定复用公共扩展面 |
| 当前设计 | 本轮重新跑 `DECLARE_DELEGATE / DECLARE_MULTICAST_DELEGATE / OnXxx` 与 settings 搜索后可以确认：插件把 compile/reload/debug 这类 runtime hook，与 `DebugListAssets`、`EditorCreateBlueprint`、`EditorGetCreateBlueprintDefaultAssetPath` 这类 editor 工作流动作，一起暴露在 `FAngelscriptRuntimeModule` 的 public contract 中；`AngelscriptEditor` 只是启动时再把 editor lambda 反向挂回这条 runtime bus，settings 侧也只注册 `UAngelscriptSettings` 这个 `Config=Engine` 对象。换句话说，editor extensibility 不是独立能力域，而是 runtime 模块上的侧向扩展。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h:15`-`:21`、`:37`-`:47` 在 runtime 公共头里同时声明 `PreGenerateClasses`、`OnInitialCompileFinished` 与 `DebugListAssets` / `EditorCreateBlueprint` / `EditorGetCreateBlueprintDefaultAssetPath`；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:397`-`:409`、`:433`-`:435` 由 editor 模块把 `ShowAssetListPopup()` / `ShowCreateBlueprintPopup()` 和默认资源路径逻辑绑定回这些 runtime delegate；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:1163`-`:1180` 再从 runtime debug server 直接广播资产列表和蓝图创建请求；`Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs:67`-`:73` 在 editor target 下把 `UnrealEd` / `EditorSubsystem` 暴露为 runtime 模块 public dependency；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:388`-`:393` 只向 Settings 面板注册 `GetMutableDefault<UAngelscriptSettings>()`，而 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h:41`-`:63` 表明该对象本质上是 `Config=Engine` 的编译器/运行时设置。 |
| 优点 | 一条总线就能把远程 debug、编译事件和 editor 弹窗串起来，第一方实现成本低，`CreateBlueprint` 之类功能也不需要额外 bridge 层。 |
| 不足 | 公共扩展面对消费者不自描述：看 `AngelscriptRuntimeModule.h` 无法直接判断哪些 hook 在非 editor 目标不可用、哪些会把你带进 editor 工作流。结果是 runtime contract 混入 editor 语义，外部扩展模块难以做 capability-scoped 依赖管理，后续想把正式 public API 收口到 runtime-only / editor-only 两层时也会很痛。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | runtime 模块只在 `WITH_EDITOR` 下显式加载 `UnLuaEditor`，运行时与编辑器配置也拆成两类：`UUnLuaSettings` 负责 `EnvLocatorClass` / `ModuleLocatorClass` / `PreBindClasses` 这类 runtime seam，`UnLuaEditorModule` 再单独注册 `UUnLuaEditorSettings`。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaModule.cpp:48`-`:54`、`:238`-`:249`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaSettings.h:47`-`:56`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:114`-`:123`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorSettings.h:39`-`:55` | editor workflow 可以依赖 runtime，但 editor 配置与 editor hook 不必反向塞回 runtime 公共头。 |
| UnrealCSharp | 模块边界显式分层：`UnrealCSharpEditor` 是单独的 editor 模块；core delegate 里凡是 editor-only 的 `OnDynamicClassUpdated` / `OnCompile` 都放在 `#if WITH_EDITOR` 内；同时 editor 定制项集中在 `UUnrealCSharpEditorSetting`，注册路径也是 `"Editor/Plugins"`。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/UnrealCSharpEditor.Build.cs:25`-`:63`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Delegate/FUnrealCSharpCoreModuleDelegates.h:18`-`:35`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpEditorSetting.h:32`-`:45`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Setting/UnrealCSharpEditorSetting.cpp:44`-`:114` | 即便 core module 也承载一部分阶段事件，editor-only 能力仍然通过编译条件和独立 settings class 明确隔离。 |
| puerts | public runtime contract 聚焦运行时能力：`IPuertsModule` 公开 `InitExtensionMethodsMap()`、`SetJsEnvSelector()`、`ReloadModule()`，`IJSModuleLoader` 只关心 `Search()` / `Load()` / `GetScriptRoot()`；settings 保存回调也只是 module 自己处理，没有把 editor UI 动作定义进 runtime interface。 | `Reference/puerts/unreal/Puerts/Source/Puerts/Public/PuertsModule.h:17`-`:55`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSModuleLoader.h:17`-`:50`；`Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:353`-`:363` | runtime extensibility 的 public API 应优先表达“脚本环境/模块解析/扩展方法”这类核心能力，而不是顺手承载 editor 行为。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 editor 侧扩展动作从 `FAngelscriptRuntimeModule` 的公共 contract 中剥离出来，形成 `runtime extensibility` 与 `editor tooling extensibility` 两条并行但解耦的正式边界。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/` 新增 `IAngelscriptRuntimeExtensibility` 或等价 facade，只保留 compile/reload/type/bind/debug runtime 相关 accessor；把 `DebugListAssets`、`EditorCreateBlueprint`、`EditorGetCreateBlueprintDefaultAssetPath` 从 runtime 公共头迁出。 2. 在 `Plugins/Angelscript/Source/AngelscriptEditor/Public/` 新增 `IAngelscriptEditorExtensibility` 或 `FAngelscriptEditorModuleDelegates`，由 `AngelscriptEditor` 提供资产列表弹窗、蓝图创建、默认路径等 editor-only 行为。 3. 修改 `AngelscriptDebugServer.cpp`：runtime 侧不再直接广播 editor delegate，而是在 `WITH_EDITOR` 条件下查询 editor bridge；非 editor 目标明确返回“editor integration unavailable”，避免 runtime bus 暗含 editor 语义。 4. 新增 `UAngelscriptEditorSettings`（建议 `Config=Editor` 或 `EditorPerProjectUserSettings`），承载蓝图创建默认路径、debug 弹窗策略、未来 editor tooling provider 清单；`UAngelscriptSettings` 继续只负责编译器/运行时语义。 5. 为兼容旧扩展者，可在一个迁移周期内保留 `FAngelscriptRuntimeModule::GetEditorCreateBlueprint()` 等旧入口，但内部只做 forward，并标注 deprecated，要求新扩展改为依赖 editor contract。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h`、新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/IAngelscriptEditorExtensibility.h`、新增 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorExtensibility.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/AngelscriptEditorSettings.h/.cpp` |
| 预估工作量 | M |
| 架构风险 | 最大风险是已有 editor 功能和外部扩展可能直接 include `AngelscriptRuntimeModule.h` 并绑定这些 editor getter；如果一次性删除会造成编译回归。第一阶段必须先提供 editor facade，再把旧入口桥接过去。 |
| 兼容性 | 可增量、基本向后兼容。runtime compile/reload/debug 扩展者不受影响；旧的 editor getter 在迁移期继续存在，只是推荐迁移到新 editor contract。新增 `UAngelscriptEditorSettings` 不改变现有 `DefaultEngine.ini` 字段语义。 |
| 验证方式 | 1. 新建一个纯 runtime 宿主扩展模块，只 include runtime facade，确认在非 editor target 下不再暴露 editor-only getter。 2. 在 editor target 下验证 debug server 的 `ListAssets` / `CreateBlueprint` 仍能走通，但实现路径已经通过 `IAngelscriptEditorExtensibility`。 3. 新建一个 editor 扩展模块，仅依赖新 editor contract 和 `UAngelscriptEditorSettings` 改写默认蓝图路径，确认无需 include `AngelscriptRuntimeModule.h`。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-EP36 | editor 扩展动作通过 runtime 公共总线侧向暴露，`Runtime/Editor` 能力域未分层 | 模块边界与公共 contract 收口 | 高 |

---

## 架构分析 (2026-04-08 19:17)

### Arch-EP37：raw 扩展 API 能改 runtime，但不会进入 `Binds.Cache` / bind database 这条统一事实来源

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 外部扩展者通过 `BindGlobalFunction()`、`BindGlobalFunctionDirect()` 或未来的自定义 contributor 增加脚本 API 时，这些扩展是否会像反射绑定一样进入持久化缓存、state dump 和工具链审计面 |
| 当前设计 | 本轮补查 `bind database` 后可以确认：当前插件已经有一条正式的持久化元数据通路，但这条通路只完整覆盖 `UClass/UStruct` 反射绑定。`FAngelscriptBindDatabase` 的 schema 只有 `Structs` 和 `Classes`，运行时会加载/保存 `Binds.Cache`，`StateDump` 也会单独导出 `BindDatabase_Classes.csv`；但 raw `BindGlobalFunction()` / `BindGlobalFunctionDirect()` 只是直接向 AngelScript engine 注册函数，然后走 `OnBind()`，没有把贡献写回 bind database。结果是“扩展已生效”和“工具链知道这条扩展存在”是两条不同通路。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h:56`-`:79` 定义 `FAngelscriptMethodBind` 只描述 class method 级绑定，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h:123`、`:132`-`:133` 的 `FAngelscriptBindDatabase` 只持有 `Structs` / `Classes`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1469`、`:1505` 在启动/写回时读写 `GetScriptRootDirectory() / "Binds.Cache"`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1505` 把反射得到的 `DBBind` 写入 `FAngelscriptBindDatabase::Get().Classes`；对照 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:588`-`:608`，`BindGlobalFunction()` 与 `BindGlobalFunctionDirect()` 只做 `RegisterGlobalFunction()` + `OnBind()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp:847`-`:871` 的 `BindRegistrations.csv` 虽然记录了 `BindName`，但 `BindModule` 列实际写的是空字符串，`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp:899`-`:919` 的 `BindDatabase_Classes.csv` 又只统计 class 级条目。 |
| 优点 | 现有反射绑定与 cook 缓存链路已经很成熟，`Binds.Cache`、`BindDatabase_*` 和文档/tooltip 体系能稳定服务第一方 `UClass/UStruct` 暴露。 |
| 不足 | 外部用户最容易走的 raw 扩展 seam，恰好绕开了这条工具链事实来源。这样一来，自定义全局函数即使不改插件源码就能工作，也无法自然进入 bind database、持久化审计、来源追踪和后续 tooling 复用；而 `BindRegistrations.csv` 又只知道“有个 bind 名字”，不知道它具体暴露了什么脚本函数、来自哪个模块、是否应该进缓存。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | 把绑定描述先收敛进单一 `FBinding` registry，再由不同 phase 共用：`FBindingClassGenerator::Generator()` 从 `FBinding::Get().GetClasses()` 生成绑定代码，`FMonoDomain::RegisterBinding()` 又从同一 registry 取 `GetMethods()` 注册 internal call。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Binding/FBinding.h:8`-`:29`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Binding/FBinding.cpp:10`-`:28`、`:78`；`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FBindingClassGenerator.cpp:10`-`:14`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp:783`-`:789` | runtime 注册与 generator/tooling 不该各自维护半套事实，应该共享同一份 binding manifest/registry。 |
| puerts | `UExtensionMethods` 不是只给 runtime 用的临时约定；`FJsEnvImpl::InitExtensionMethodsMap()` 和 `FTypeScriptDeclarationGenerator::InitExtensionMethodsMap()` 都扫描同一类 family，并把结果落到各自的扩展方法表里。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/ExtensionMethods.h:19`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:931`-`:972`；`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:228`、`:315`-`:351` | 扩展 authoring model 一旦公开，就应同时进入 runtime 和声明/工具链消费面，而不是只在其中一侧可见。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把现有 `bind database + raw bind` 两条平行通路收口成统一 `extension manifest`：反射绑定、raw 全局函数和未来 provider 都写入同一份可持久化描述，再由 runtime、state dump 和 tooling 共用。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/` 新增 `FAngelscriptExtensionManifest`，或扩展现有 `FAngelscriptBindDatabase` schema，至少补上 `GlobalFunctions`、`ExtensionContributors`、`SourceModule`、`SourceHeader`、`BindingKind(Global/Namespace/Mixin/Method)`、`PersistencePolicy`。 2. 让 `FAngelscriptBinds::BindGlobalFunction()`、`BindGlobalFunctionDirect()`、`BindGlobalGenericFunction()` 在成功注册后可选写入 manifest entry；第一阶段先由新重载或 builder API 提供 metadata，旧 API 保持不写 manifest 的 legacy 行为。 3. 把 `DumpBindRegistrations()` 与 `DumpBindDatabaseClasses()` 合并为统一 `ExtensionManifest.csv` / `ScriptApiManifest.csv` 输出，明确展示脚本名、声明、来源模块、是否来自 `Binds.Cache`、是否为 legacy raw bind。 4. 让 `Binds.Cache` 或新 manifest 文件承载这份统一 schema；cook/runtime 继续从磁盘读取，editor 与 `StateDump` 也只读这一份事实来源。 5. 为自定义全局函数补一个最小宿主测试模块，验证“不改插件源码新增函数”后，既能在 runtime 调用，也能在 manifest/dump 中被准确看到。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptExtensionManifest.h/.cpp` |
| 预估工作量 | M |
| 架构风险 | 最大风险是把“运行时即时注册”与“持久化元数据”绑得过死，导致某些只应在当前进程存在的临时 bind 被错误缓存。第一阶段必须给 manifest entry 增加 `PersistencePolicy`，并把 legacy raw bind 默认标成 non-persistent。 |
| 兼容性 | 向后兼容。现有 raw API 继续能直接改 runtime；新 manifest 只是给愿意进入 tooling/cook 审计链的扩展补正式描述。旧 `Binds.Cache` 也可以先作为新 schema 的退化读取路径。 |
| 验证方式 | 1. 新建一个宿主扩展模块，只用 `BindGlobalFunction()` 增加全局函数，验证函数可调用且 `StateDump` 能看到对应 manifest entry。 2. 复跑 `Binds.Cache` 读写与 bind database 相关回归，确认原有 `UClass/UStruct` 反射绑定条目不变。 3. 在 cooked/commandlet 路径下验证 non-persistent raw bind 不会被误写进缓存，而显式声明 persistent 的扩展会被正确保存。 |

### Arch-EP38：`ScriptName/ScriptMixin` 规则只被局部 helper 复用，尚未上升为 runtime / class generator / editor 共用的 discovery service

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 用户通过 `ScriptName`、`ScriptMixin`、`ScriptGlobalScope` 等 metadata 扩展脚本 API 时，这套 authoring 规则是否已经成为所有 phase 共用的正式 contract，还是仍由多个子系统各自推导 |
| 当前设计 | 本轮补查后可以确认，当前仓库已经有一部分“共享 helper”，但还没有共享的 discovery service。`Helper_FunctionSignature.h` 集中定义了脚本名、namespace、global-scope 与 mixin 判定规则；`Bind_BlueprintType.cpp` 在 runtime 绑定阶段消费这些规则并把结果写入 bind database；而 `AngelscriptClassGenerator.cpp` 为了处理 override/事件名冲突，又要单独调用 `GetScriptNameForFunction()` 和 `GetBlueprintEventByScriptName()` 重新推导一遍。也就是说，规则文本是共享的，扩展发现结果却不是共享 artifact。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h:85`-`:120` 定义 `GetScriptNameForFunction()`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h:123`-`:175` 定义 `GetScriptNamespaceForClass()`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h:273`-`:278` 决定 `ScriptGlobalScope` 与 `ScriptMixin` 语义；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1404`-`:1406` 在 runtime 路径里按这些规则做 `BindBlueprintCallable()`，并在 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1505` 把结果写进 `BindDatabase`；对照 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:99`、`:737`、`:796`-`:801`，class generator 仍要自己通过 `GetBlueprintEventByScriptName()` 与 `GetScriptNameForFunction()` 重新推断“父类事件真正该叫什么”。 |
| 优点 | 关键命名规则至少没有完全复制粘贴，`Helper_FunctionSignature` 已经避免了最糟糕的文本级分叉；runtime 绑定和 class generator 在部分边界上还能共享同一套 naming helper。 |
| 不足 | 但“共享 helper”还不等于“共享 contract”。一旦未来引入新的 extension base class、provider allowlist、strict discovery mode，或把 `ScriptMixin` 正式产品化成 public API，当前架构仍需要同时改 `Bind_BlueprintType`、class generator、bind database、state dump，且很难证明这些 phase 看到的是同一份扩展集合。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `UExtensionMethods` 是真正的共享 discovery contract：runtime 里的 `FJsEnvImpl::InitExtensionMethodsMap()` 与 editor/generator 里的 `FTypeScriptDeclarationGenerator::InitExtensionMethodsMap()` 都扫描 `UExtensionMethods` 子类，并按首参数类型生成同构的扩展方法表。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/ExtensionMethods.h:19`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:931`-`:972`；`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:228`、`:315`-`:351` | 不同 phase 可以各自消费结果，但发现规则本身应只有一份正式 contract。 |
| UnrealCSharp | 绑定 authoring 不是散落在 runtime/generator 各处，而是先经 `FBinding` / `FBindingClassRegister` 建模，再由 generator 和 runtime domain 共用同一 registry。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Binding/FBinding.h:16`-`:29`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Binding/Class/FBindingClassRegister.h:31`-`:41`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Binding/FBinding.cpp:10`-`:28`；`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FBindingClassGenerator.cpp:10`-`:14`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp:783`-`:789` | 先把“发现与描述”做成 registry/object model，后面的 runtime、generator、tooling 才能稳定复用。 |
| UnLua | `ModuleLocatorClass` 这类 typed contract 也不是 runtime 私有知识；`FLuaEnv` 在运行时按 settings 取 locator，`UnLuaEditorCore` 在 editor 里判断绑定状态时仍然读取同一份 `ModuleLocatorClass`。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaSettings.h:49`-`:56`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:78`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorCore.cpp:39`-`:48` | 即使 editor 与 runtime 做的事不同，也可以围绕同一 typed contract，而不是各自猜规则。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把今天散落在 `Helper_FunctionSignature`、`Bind_BlueprintType`、`ClassGenerator` 里的扩展发现规则抽成正式 `discovery service`，让 runtime、generator、state dump、editor 只消费描述结果，不再各自重算。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/` 或 `Public/Extension/` 新增 `FAngelscriptExtensionDiscoveryService` 与 `FAngelscriptExtensionDescriptor`，descriptor 至少记录 `SourceClass`、`SourceFunction`、`BindingKind(Global/Namespace/Mixin/EventOverride)`、`FinalScriptName`、`Namespace`、`MixinTargets`、`bEditorOnly`、`WhyExposed`。 2. 让 `Bind_BlueprintType.cpp` 先调用 discovery service 获取 descriptor，再做 `BindBlueprintCallable()` / `BindBlueprintEvent()` 和 bind database 写入，而不是自己边扫边推导。 3. 让 `AngelscriptClassGenerator.cpp` 不再直接拼 `GetScriptNameForFunction()` + `GetBlueprintEventByScriptName()` 组合，而是查询 descriptor view，统一拿到“父类事件的最终脚本名”和“该名称是因 `ScriptName` 还是前缀裁剪得来”。 4. 让 `StateDump`、文档与未来 editor tooling 都只消费这份 descriptor 列表，避免每增加一种 discovery mode 就多维护一套 side logic。 5. 第一阶段保留 `Helper_FunctionSignature` 作为 discovery service 的内部实现细节和 compatibility shim；先收口调用点，不立即更改任何现有命名规则。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptExtensionDiscoveryService.h/.cpp` |
| 预估工作量 | M |
| 架构风险 | discovery service 一旦抽象错误，容易把今天依赖“运行时即时上下文”的判断硬冻结成静态 descriptor，反而引入 editor-only / cook / legacy scan 差异。因此第一阶段应只抽取现有规则，不新增新语义，并保留逐 phase 的少量后处理 hook。 |
| 兼容性 | 向后兼容。现有 `ScriptName`、`ScriptMixin`、`ScriptGlobalScope`、前缀/后缀裁剪和 override 诊断规则都保持不变；变化只是这些规则从“多个消费者分别推导”变成“由 discovery service 统一产出”。 |
| 验证方式 | 1. 用包含 `ScriptName`、`ScriptMixin`、`ScriptGlobalScope` 的宿主扩展库做回归，确认 runtime 绑定结果与当前版本完全一致。 2. 为 class generator 增加一条 override 名称测试，验证错误信息中的建议名称与 runtime descriptor 中的 `FinalScriptName` 一致。 3. 运行 `StateDump` 或新的 descriptor dump，确认同一个扩展函数只出现一条描述记录，而不是 runtime 和 tooling 各自推导出不同结果。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-EP37 | raw 全局函数/外部扩展未进入 `Binds.Cache` 与 bind database 统一事实来源 | manifest 收口 | 高 |
| P2 | Arch-EP38 | `ScriptName/ScriptMixin` 规则只被局部 helper 复用，未形成跨 runtime/tooling 的 discovery service | 共享发现层收口 | 中高 |

---

## 架构分析 (2026-04-08 19:28)

### Arch-EP39：`OnInitialCompileFinished` 被过载成 engine-ready 信号，但插件没有对称的 active/inactive 生命周期 contract

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 外部扩展者如何拿到“engine 已可用 / 即将停用”的正式生命周期边界，而不是把编译事件当初始化替身 |
| 当前设计 | 现有 public hook 里，真正接近“runtime 已就绪”的只有 `GetOnInitialCompileFinished()`；它在 `FAngelscriptEngine::PostInitialize_GameThread()` 里被广播。相对地，`FAngelscriptRuntimeModule::ShutdownModule()` 与 `FAngelscriptEngine::Shutdown()` 会释放 ticker、owned engine、shared state、`DebugServer`、`StaticJIT` 和 context，但没有任何对外的 `OnEngineDeactivating / OnEngineDeactivated` 对称事件。仓内第一方代码也因此把 `OnInitialCompileFinished` 当作“现在终于可以安全做点初始化”的通用替身。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h:37`-`:49` 公开的生命周期相关入口只有 compile 系列 delegate 与 `InitializeAngelscript()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp:13`-`:39` 在 startup/shutdown 中只做 initialize、fallback ticker 和 owned engine 清理，没有 broadcast deactivate 事件；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp:138`-`:166` 显示初始化受 `bInitializeAngelscriptCalled` 保护、只走一轮；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1653`-`:1655` 在 `PostInitialize_GameThread()` 里广播 `GetOnInitialCompileFinished()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1132`-`:1252` 执行完整 shutdown / shared-state release，却没有对应 public lifecycle event；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Console.h:18`-`:29` 则把 `GetOnInitialCompileFinished()` 用作 late initialize hook。 |
| 优点 | 当前状态机很少，默认项目几乎不需要关心 activation graph；只要插件自己控制 owner，启动和销毁路径都比较直接。 |
| 不足 | 对扩展者而言，“compile finished”“engine ready”“shared state adopted”“engine shutting down”是四种不同语义，但现在只有第一种被显式命名。结果是外部 provider 很难安全挂接 per-engine 资源、成对解绑、区分初次初始化与 clone/shared-state 参与，也容易把编译完成事件误用成一般性 runtime activation 事件。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `IUnLuaModule` 明确公开 `IsActive()` / `SetActive()` / `GetEnv()` / `HotReload()`；`FUnLuaModule::StartupModule()`、`ShutdownModule()` 和 `SetActive()` 把激活、停用、listener 注册、`EnvLocator` 创建与回收都收口到统一 lifecycle。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaModule.h:18`-`:32`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaModule.cpp:48`-`:84`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaModule.cpp:91`-`:143`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaModule.cpp:152`-`:157` | 即便不暴露很多细粒度事件，也要先有“active / inactive / hot reload”这条正式宿主 contract。 |
| UnrealCSharp | 把 lifecycle 信号拆成 `OnUnrealCSharpModuleActive`、`OnUnrealCSharpModuleInActive`、`OnCSharpEnvironmentInitialize`；`FCSharpEnvironment` 订阅 active/inactive，并在真正完成环境对象创建后单独广播 `OnCSharpEnvironmentInitialize`。 | `Reference/UnrealCSharp/Source/UnrealCSharp/Public/Delegate/FUnrealCSharpModuleDelegates.h:3`-`:16`；`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp:32`-`:39`；`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp:54`-`:134` | “模块已激活”和“语言环境已可用”是两层不同语义，应该被显式区分。 |
| puerts | `FPuertsModule` 用 `Enable()` / `Disable()` 管理 `JsEnv`、listener 与 settings 驱动的启停；settings 保存回调也会切换 enable state，而不是让外部自己猜模块何时可用。 | `Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:356`-`:363`；`Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:405`-`:451`；`Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:453`-`:479`；`Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:482`-`:498`；`Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:501`-`:512` | 扩展可用性的开关与 teardown 最好由模块自己持有，而不是让扩展者把 compile event 当作 activation proxy。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留现有 compile/reload delegate 的前提下，补一条更上层的 `runtime lifecycle` contract，把“engine ready / deactivate”从 compile 事件中解耦出来。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/` 新增 `IAngelscriptRuntimeLifecycle` 或 `FAngelscriptRuntimeLifecycleDelegates`，第一阶段至少提供 `OnEngineActivating`、`OnEngineActivated`、`OnEngineDeactivating`、`OnEngineDeactivated`、`OnSharedStateAdopted`。 2. 让 `FAngelscriptRuntimeModule::InitializeAngelscript()`、`FAngelscriptEngine::PostInitialize_GameThread()`、`FAngelscriptEngine::Shutdown()` 分别广播对应生命周期事件；`GetOnInitialCompileFinished()` 继续保留，但重新定义为 compile-specific signal，而不是默认 engine-ready 信号。 3. 为 lifecycle payload 增加最小上下文，例如 `FAngelscriptEngine*`、`InstanceId`、`bOwnsEngine`、`SharedStateParticipantCount`、`bIsClone`，避免外部 provider 必须回读全局单例。 4. 迁移一处第一方使用作为样板，例如把 `Bind_Console.h` 的延迟初始化从直接监听 `OnInitialCompileFinished` 改为监听 `OnEngineActivated`，验证语义更准确。 5. 在 `StateDump` 增加 `RuntimeLifecycle.csv` 或等价表，记录最近一次 activation/deactivation 的时间、engine id 与 shared-state 信息。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Console.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/IAngelscriptRuntimeLifecycle.h` 或 `AngelscriptRuntimeLifecycleDelegates.h/.cpp` |
| 预估工作量 | M |
| 架构风险 | 生命周期事件如果插得太早或太晚，容易和现有 editor fallback tick、clone/shared-state 释放顺序打架。第一阶段必须只做附加广播，不改变初始化与释放次序。 |
| 兼容性 | 向后兼容。现有 `GetOnInitialCompileFinished()`、`GetPreCompile()`、`GetPostCompile()` 保持不变；新 lifecycle contract 只是把过去隐含在 compile 事件里的“engine ready / deactivate”语义显式化。 |
| 验证方式 | 1. 增加一个宿主测试模块，在 `OnEngineActivated` 分配资源、`OnEngineDeactivated` 回收资源，验证 editor 启动、PIE、shutdown 都能成对触发。 2. 复跑 multi-engine / clone tests，确认 `OnSharedStateAdopted` 与 clone 参与者数量匹配。 3. 把 `Bind_Console.h` 迁移到新事件后，验证初次启动和二次 full reload 都不会早于 engine-ready 时机注册 CVar。 |

### Arch-EP40：`UClass` 扫描式 reflective 扩展库已经能自动暴露函数，但没有显式 refresh / rebind contract

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 基于 `BlueprintCallable` / `ScriptCallable` / `ScriptMixin` / `ScriptGlobalScope` 的 declarative 扩展库，是否能在不重启插件或不重走整轮 bind 的情况下被显式刷新 |
| 当前设计 | 当前 reflective extension lane 确实存在，而且很强：`Bind_BlueprintType.cpp` 在绑定阶段直接扫描 `TObjectRange<UClass>()`，把 callable function 绑定成 method、namespace function、mixin 或 global function。但这条能力仍然被锁在 `BindScriptTypes()` 这轮 one-shot bind 里；public runtime contract 只暴露 compile observer 和 `InitializeAngelscript()`，没有等价于“重新扫描 extension libraries / 重新建立 reflective function map”的显式 API。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1366`-`:1406` 在 bind 阶段对 `TObjectRange<UClass>()` 全量扫描，并对 `FUNC_BlueprintCallable` / `FUNC_BlueprintPure` / `ScriptCallable` 调用 `BindBlueprintCallable()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1915`-`:1926` 显示 `BindScriptTypes()` 本质上只是在当前时点执行 `FAngelscriptBinds::CallBinds()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp:138`-`:166` 说明初始化受 one-shot `bInitializeAngelscriptCalled` 控制；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h:37`-`:49` 的 public surface 只有 compile delegate 与 `InitializeAngelscript()`，没有任何 `RefreshReflectiveExtensions()` / `RebindExtensionLibraries()` 之类的入口。 |
| 优点 | 对静态项目来说，这条路径非常省心：作者只要写普通 `UClass` / `UBlueprintFunctionLibrary` 风格的 native class 和 metadata，就能被自动发现，无需手写 raw binder。 |
| 不足 | 一旦扩展库在 engine 初始化后才进入进程，或作者希望在 editor 中显式刷新一批新加的 extension library，这条 seam 就只能依赖“再来一轮完整 bind / compile / restart”。这让 declarative authoring model 在体验上仍然不像正式扩展协议，而更像当前 bind pass 的副产品。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 公开 `IPuertsModule::ReloadModule()` 与 `InitExtensionMethodsMap()`；模块实例再把请求转发给 `JsEnv` 或 `JsEnvGroup`。`FJsEnvImpl::InitExtensionMethodsMap()` 虽然同样做 `UClass` 扫描，但它是一个可显式调用的刷新动作，而不是只能依赖初始化时机。 | `Reference/puerts/unreal/Puerts/Source/Puerts/Public/PuertsModule.h:37`-`:47`；`Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:80`-`:109`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JsEnv.h:46`-`:55`、`:83`-`:98`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:931`-`:983` | 即便发现机制仍靠扫描，也要给宿主一个“现在请刷新这批扩展”的正式 API。 |
| UnLua | `IUnLuaModule` 明确公开 `HotReload()`；模块 active 状态与 `EnvLocator` 也由 `SetActive()` 统一管理，宿主可显式请求重新绑定，而不是只能等待下一次完整 startup。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaModule.h:26`-`:32`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaModule.cpp:91`-`:143`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaModule.cpp:152`-`:157` | 对 discovery 型扩展，public API 最好同时回答“如何第一次发现”和“如何刷新”。 |
| UnrealCSharp | runtime activation 与环境初始化被显式拆开，扩展者可以依赖 `OnUnrealCSharpModuleActive` / `OnCSharpEnvironmentInitialize` 在正确阶段装配自己的 registry，而不是去猜初始化 pass 是否已经结束。 | `Reference/UnrealCSharp/Source/UnrealCSharp/Public/Delegate/FUnrealCSharpModuleDelegates.h:6`-`:16`；`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp:32`-`:39`、`:54`-`:134` | 即使不提供专门的 rescan API，也要把“何时能安全装配 declarative extension”作为正式阶段暴露出来。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留现有全量 bind 流程的前提下，给 reflective extension library 补一个显式 refresh / diff-apply lane，并把扫描范围收敛到 opt-in base class。 |
| 具体步骤 | 1. 在 `Public/Extension/` 新增 `UAngelscriptExtensionLibraryBase` / `UAngelscriptMixinLibraryBase`（若已按前述方案引入，则直接复用），并在 discovery service 中只把这两类 class 视为“支持显式 refresh 的 reflective provider”。 2. 在 `FAngelscriptRuntimeModule` 或 `FAngelscriptEngine` 新增 `RefreshReflectiveExtensions()` / `RebindReflectiveExtensions()`，内部基于 discovery service 重新扫描 opt-in library、生成新一轮 descriptor，并按 diff 把新增符号应用到当前 engine。 3. 第一阶段不要对整个 `TObjectRange<UClass>()` 重跑全量 bind，而是只刷新显式 extension library，避免 editor 热刷新时把普通 gameplay class 全部再扫一遍。 4. 对 diff-apply 结果记录 `GenerationId`、`SourceClass`、`BindingKind` 与 `AppliedAtRuntime`，并在 `StateDump` 新增 `ReflectiveExtensionRefresh.csv` / `ReflectiveExtensionLibraries.csv`。 5. 在 editor module / host module 侧增加一个手动入口，例如 commandlet、console command 或 `OnModulesChanged`/hot reload 之后的显式调用，让扩展作者能在不重启进程的情况下刷新 declarative extension library。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/AngelscriptExtensionLibraryBase.h`、新增 refresh service 头文件 |
| 预估工作量 | M |
| 架构风险 | 最大风险是 diff-apply 不严谨导致重复注册、旧声明残留或 reflective fallback 与 direct bind 混用后状态不一致。第一阶段必须把 refresh 范围限定在 opt-in extension library，并增加 generation/idempotent guard。 |
| 兼容性 | 向后兼容。现有全量 `BindScriptTypes()`、初次启动扫描和 legacy library 行为不变；新 refresh lane 只是给显式 extension library 补一个增量重扫入口。 |
| 验证方式 | 1. 在 engine 已初始化后动态加载一个宿主扩展模块，新增 `UAngelscriptExtensionLibraryBase` 子类并调用 `RefreshReflectiveExtensions()`，验证新 global function / mixin method 能出现而无需重启。 2. 连续调用两次 refresh，确认不会重复注册同一脚本符号。 3. 复跑现有 reflective fallback 与 `ScriptName` / `ScriptMixin` 回归，确认未调用 refresh 时默认行为与当前版本完全一致。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-EP39 | `OnInitialCompileFinished` 被过载成 engine-ready 信号，缺少 active/inactive 生命周期 contract | 生命周期 contract 新增 | 高 |
| P2 | Arch-EP40 | declarative reflective extension library 没有显式 refresh / rebind lane | 增量刷新能力 | 中高 |

---

## 架构分析 (2026-04-08 23:35)

### Arch-EP41：reload 扩展面只有结果通知，没有 `prepare/commit/abort` 事务 contract

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 外部扩展若维护 `UClass / UStruct / UDelegateFunction` 缓存、菜单映射或其它派生索引，能否在 hot reload 前后拿到完整事务边界，而不是只收到事后通知 |
| 当前设计 | `FAngelscriptClassGenerator` 当前只暴露 `OnStructReload`、`OnClassReload`、`OnDelegateReload`、`OnEnumCreated`、`OnEnumChanged`、`OnFullReload`、`OnPostReload` 这些结果型事件。它们都发生在新旧 reflection shell 已经确定、或者 full reload 已经提交之后；没有 `OnReloadStarting`、没有 `OnReloadAborted`、也没有统一 transaction id。第一方 editor 代码因此只能一边在若干 post-event 里累计状态，一边在 `OnPostReload` 再做收尾重建。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h:12`-`:19`、`:31`-`:38` 只声明 post-reload 相关 delegate；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2347`-`:2395` 在 old/new `Struct/Class` 已生成后才 `Broadcast()`，并在 full reload 收尾时才 `OnPostReload.Broadcast(bIsDoingFullReload)`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2468`-`:2469` 的 soft reload 也只有事后 `OnPostReload`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3931`-`:3932` 的 delegate reload 同样是“完成替换后再通知”；`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h:52`-`:145` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:25`-`:39` 说明第一方消费者只能靠这些 post-event 累加 delta、再在 `OnPostReload` 做统一刷新。 |
| 优点 | 事件模型简单，第一方 editor 刷新逻辑足够工作；对只需要“reload 结束后重建一次”的消费者来说，接入门槛低。 |
| 不足 | 对第三方扩展者来说，这条 contract 太晚了。若扩展维护自己的 symbol cache、wrapper map、UI selection 或旧对象引用，它拿不到“即将失效”的准备时机，也拿不到失败/回滚信号，只能把 reload 当作不可分解的黑箱提交。结果是扩展很难安全地做 pre-invalidation、双缓冲切换和失败恢复。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | 生成流程被显式 bracket 成 `OnBeginGenerator` / `OnEndGenerator` 两段，`FUnrealCSharpEditorModule::Generator()` 在整个生成流水线前后广播 begin/end。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Delegate/FUnrealCSharpCoreModuleDelegates.h:10`-`:30`；`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:237`-`:309` | 即使中间仍有多个内部步骤，外部扩展至少能拿到“事务开始/事务结束”的正式边界。 |
| UnLua | `IUnLuaModule` 把 `HotReload()` 做成显式模块动作，`FUnLuaModule::HotReload()` 再转发给 `EnvLocator->HotReload()`；reload 是 owner 驱动的正式操作，而不是纯粹被动事件。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaModule.h:18`-`:32`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaModule.cpp:152`-`:156` | 把 reload 先定义成可调用 contract，扩展者才有机会在入口前后挂自己的 prepare/cleanup 逻辑。 |
| puerts | `IPuertsModule` 直接公开 `ReloadModule(FName ModuleName, const FString& JsSource)`，`FPuertsModule` 再把请求转发到 `JsEnv` 或 `JsEnvGroup`；editor hot reload support 也通过模块入口集中驱动。 | `Reference/puerts/unreal/Puerts/Source/Puerts/Public/PuertsModule.h:37`-`:45`；`Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:80`-`:93`、`:430`-`:438` | reload 不是“等事情发生再告诉别人”，而是有明确 owner 和调用边界，便于扩展围绕同一 transaction 编排。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留现有 `OnClassReload / OnPostReload` 事件的前提下，补一条更高层的 reload transaction contract，把“开始、提交、失败”三种状态显式化。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/` 新增 `FAngelscriptReloadTransactionContext`，至少包含 `TransactionId`、`CompileType`、`bFullReload`、`AffectedModules`、`PredictedChangedSymbols`。 2. 在 `FAngelscriptClassGenerator` 或新的 `IAngelscriptReloadService` 上增加 `OnReloadStarting`、`OnReloadCompleted`、`OnReloadAborted`；第一阶段不替换旧 delegate，只做外层包裹。 3. 在 full reload 和 soft reload 路径里，先在任何 reflection shell 替换前广播 `OnReloadStarting`，成功后再按原顺序保留 `OnClassReload / OnStructReload / OnDelegateReload / OnPostReload`，最后补 `OnReloadCompleted`；若中途编译或物化失败，则广播 `OnReloadAborted`。 4. 把 `FClassReloadHelper` 与 `UScriptEditorMenuExtension` 迁移成 transaction-aware 样板：`OnReloadStarting` 做卸载/冻结，`OnReloadCompleted` 做重建，`OnReloadAborted` 做回滚或保持原状态。 5. 在 `StateDump` 增加 `ReloadTransactions.csv`，记录每次 transaction 的开始时间、结果、受影响模块和 full/soft 类型，方便第三方扩展核对时序。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/AngelscriptReloadTransaction.h` |
| 预估工作量 | M |
| 架构风险 | 当前 reload 已经是 staged pipeline；若 transaction 事件插在错误时机，可能破坏 editor 刷新顺序或让 first-party helper 提前看到尚未稳定的对象。第一阶段必须只新增外层 begin/end/abort 信号，不改既有 post-event 触发次序。 |
| 兼容性 | 向后兼容。现有 `OnClassReload`、`OnStructReload`、`OnFullReload`、`OnPostReload` 全部保留；新 transaction event 只是给扩展者补完整生命周期。 |
| 验证方式 | 1. 新建一个宿主扩展模块，维护一份 class cache，验证 success case 收到 `Starting -> ...delta events... -> Completed`，failure case 收到 `Starting -> Aborted`。 2. 复跑现有 full reload / soft reload / editor menu extension 流程，确认旧事件触发次数与顺序不变。 3. 检查 `ReloadTransactions.csv`，确认 full/soft reload 与受影响模块集合和实际行为一致。 |

### Arch-EP42：public extension hook 缺少 thread contract，扩展者需要读实现才知道哪些回调能碰 `UObject / Slate`

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 外部扩展在订阅 compile / preprocess / reload hook 时，能否仅凭 public contract 判断线程域和 `UObject` 安全边界，而不是去猜哪些回调一定在 `GameThread` |
| 当前设计 | `FAngelscriptRuntimeModule`、`FAngelscriptPreprocessor`、`FAngelscriptClassGenerator` 的 public header 只声明了 delegate 类型和 accessor，没有任何线程注释、没有 `GameThreadOnly`/`AnyThread` 标记、也没有自动切回 `GameThread` 的镜像事件。与此同时，运行时实现实际已经跨线程：初始化可在 `AnyHiPriThreadHiPriTask` 上执行，hot reload 还有独立 `AngelscriptHotReload` 线程，而 preprocessor / compile / reload hook 又都直接在各自阶段 `Broadcast()`。这意味着第一方代码或许知道哪些点要谨慎，但第三方扩展者并没有正式 contract 可依赖。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h:9`-`:49` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h:8`-`:30` 只声明 delegate/accessor，没有线程约束说明；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:823`-`:847` 显示初始化可通过 `AsyncTask(ENamedThreads::AnyHiPriThreadHiPriTask, ...)` 进入 `Initialize_AnyThread()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1658`-`:1700` 会启动专用 `AngelscriptHotReload` 线程；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3066`-`:3068`、`:4138`-`:4140` 直接广播 `PreCompile / PostCompile`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:232`-`:287` 在预处理过程中直接广播 `OnProcessChunks` / `OnPostProcessCode`。 |
| 优点 | 实现层有较高自由度，可以按性能需要决定初始化、文件监控和编译的线程策略，不会被过早锁死。 |
| 不足 | 扩展者如果在 hook 里访问 `AssetRegistry`、`BlueprintActionDatabase`、`Slate` 或直接修改 `UObject`，当前只能靠读实现或试错判断是否安全。随着外部扩展增多，这会从“文档欠缺”升级成“并发与 reentrancy 易错点”。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | 生成与编译链虽然同样复杂，但会显式把一部分 editor-facing 动作 `AsyncTask(ENamedThreads::GameThread, ...)` 回到 `GameThread`，同时仍保留 `OnBeginGenerator / OnEndGenerator` 作为高层阶段边界。 | `Reference/UnrealCSharp/Source/Compiler/Private/FCSharpCompilerRunnable.cpp:189`-`:191`、`:205`-`:210`、`:328`-`:339`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Delegate/FUnrealCSharpCoreModuleDelegates.h:14`-`:30` | 线程切换和阶段事件都应显式暴露，不能只让扩展者从实现里猜。 |
| UnLua | 模块实现里直接用 `IsInGameThread()` 保护系统错误处理与 stack dump，说明 owner 对线程域有明确假设；同时 `IUnLuaModule` 仍把 `SetActive / GetEnv / HotReload` 收口为正式 contract。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaModule.h:26`-`:32`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaModule.cpp:190`-`:196` | 即便不把所有 hook 都包装成 game-thread 事件，也应把 thread assumption 写死在 owner 侧，而不是留给外部扩展猜。 |
| puerts | 运行时内部会在关键路径上显式判断 `IsInGameThread()`，模块级 hot reload 也通过 `ReloadModule()` 这类明确入口收口，而不是散落为无语义的 raw callback。 | `Reference/puerts/unreal/Puerts/Source/Puerts/Public/PuertsModule.h:37`-`:45`；`Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:80`-`:93`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:1589`-`:1607` | 哪些点允许 `AnyThread`，哪些点必须在 `GameThread`，应该体现在 contract 或 guard 中。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 不先改线程模型本身，而是先把 public extension surface 的 thread contract 文档化、类型化，并为高风险 hook 补 game-thread mirror。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/` 新增 `EAngelscriptExecutionThread` 或 `FAngelscriptThreadContract`，为每个 public hook 标记 `GameThreadOnly`、`CompilerThread`、`AnyThread`、`MayReenterGameThread`。 2. 在统一 extension surface header 和自动生成文档里，给 `PreCompile`、`PostCompile`、`OnProcessChunks`、`OnPostReload` 等事件补线程域、reentrancy、`UObject`/`Slate` 安全说明。 3. 对高风险 observe-only hook 增加可选 mirror，例如 `OnPostReload_GameThread`、`OnPostCompile_GameThread`，内部用 `AsyncTask(ENamedThreads::GameThread, ...)` 转发 typed context；raw hook 保留给 expert-mode 扩展。 4. 迁移一两个第一方消费者做样板，例如 editor 菜单或 blueprint action refresh 改为只使用 `_GameThread` mirror，并在旧 consumer 上补 `check(IsInGameThread())`/`ensure`。 5. 增加自动化测试记录 thread id 和 `IsInGameThread()`，覆盖初次初始化、hot reload、commandlet compile 三种路径，确保文档声明与实际一致。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/AngelscriptThreadContract.h` |
| 预估工作量 | M |
| 架构风险 | 如果一开始就强行把所有 hook marshal 回 `GameThread`，会影响编译/热重载吞吐并隐藏真正的并发问题。第一阶段应只补 metadata、文档和少量 mirror，不改 raw hook 行为。 |
| 兼容性 | 向后兼容。现有 delegate 继续保持当前线程行为；新增 thread metadata 和 `_GameThread` mirror 只提供更安全的推荐路径。 |
| 验证方式 | 1. 新建一个宿主扩展模块，同时订阅 raw hook 与 `_GameThread` mirror，记录 `IsInGameThread()` 和 thread id，验证文档声明与运行时一致。 2. 复跑 hot reload 与 compile 回归，确认新增 mirror 不改变旧 hook 次序。 3. 在 editor 场景下验证需要碰 `BlueprintActionDatabase` / `Slate` 的第一方消费者已全部迁移到 `GameThread` 安全路径。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-EP41 | reload 扩展只有事后通知，缺少 `prepare/commit/abort` 事务 contract | 生命周期 contract 新增 | 高 |
| P2 | Arch-EP42 | public hook 没有 thread contract，外部扩展难以安全处理 `UObject / Slate` | 扩展约束显式化 | 中高 |

---

## 架构分析 (2026-04-08 23:44)

### Arch-EP43：settings 既有“初始化快照”也有“运行时直读”，但插件没有正式的 apply contract

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `ini/settings` 配置改变后，哪些行为会立刻生效、哪些必须重启、哪些会被 engine 启动时快照，当前是否有稳定 contract 可供扩展者依赖 |
| 当前设计 | `UAngelscriptSettings` 大部分字段都标了 `ConfigRestartRequired = true`，但插件并没有与之对应的 `OnModified`、`ApplySettings()` 或 reload lane。更关键的是，源码里同时存在两种消费模式：一类在 `FAngelscriptEngine::PreInitialize_GameThread()` 中把 settings 快照进 `ConfigSettings`、static 变量和排序后的 namespace 规则；另一类又在运行时直接读 `UAngelscriptSettings::Get()`。这让 settings 语义变成“靠读实现推断”，不是正式 contract。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h:51`-`:150` 大量字段标记 `ConfigRestartRequired = true`；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:384`-`:393` 只做 `RegisterSettings()`，没有 `OnModified` 绑定；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1291`-`:1308` 在初始化时把 `bAutomaticImports`、`bUseScriptNameForBlueprintLibraryNamespaces`、前后缀列表等复制进 engine/static 状态；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:916`-`:919` 后续通过 `FAngelscriptEngine::Get().ConfigSettings` 读取快照；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:680`-`:688` 直接依赖传入 `Config` 的 debugger blacklist；反过来 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:5568`-`:5587` 又在 loop detection 时直接读取 `UAngelscriptSettings::Get().EditorMaximumScriptExecutionTime`。 |
| 优点 | 当前实现成本低，启动路径直接，性能敏感设置可以在初始化时一次性归档，少量 editor-only 值也能通过直接读 CDO 临时生效。 |
| 不足 | 对扩展作者来说，这条 contract 几乎不可预测。改一个 settings 字段，到底是“立即生效”“要重启 editor”“要重建 engine”“还是其实只影响下一次 compile”，当前没有统一规则。后续如果把 import resolver、binding contributor 或 compile observer 也接入 settings，这种混合语义会直接放大为可维护性问题。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | settings 注册后立即把 `Section->OnModified()` 绑定到模块处理函数；同时非 editor 打包场景还会显式 `ReloadConfig()`，然后再把最新 settings 应用到 runtime 状态。运行时真正使用 locator 时，也是从当前 settings 取 `EnvLocatorClass` / `PreBindClasses`。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaModule.cpp:238`-`:260`、`:272`-`:276`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaModule.cpp:103`-`:128` | 至少要把“配置保存后怎么重新应用”做成显式模块 contract，而不是只暴露一个 Project Settings 页面。 |
| puerts | `RegisterSettings()` 注册完 settings 后，立刻把 `OnModified()` 绑定到 `HandleSettingsSaved()`；保存设置时会根据 `AutoModeEnable` 直接 `Enable()` / `Disable()`，把配置修改和 runtime 状态切换串成同一条路径。 | `Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:353`-`:389`；`Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:482`-`:497` | 配置项不一定都要 hot-apply，但应该有明确的 apply lane，把“可热切换”和“需重启”分层处理。 |
| UnrealCSharp | settings 不是单纯 editor 展示对象，而是被 getter/consumer 正式消费。例如 `AssemblyLoader` 在 settings 中声明为 `TSubclassOf<UAssemblyLoader>`，再通过 `GetAssemblyLoader()` 和 `FMonoDomain` 取用，形成稳定的 producer-consumer contract。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpSetting.h:140`-`:144`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Setting/UnrealCSharpSetting.cpp:87`-`:91`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp:1106`-`:1110`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp:500`-`:503` | settings 的价值不在字段多，而在于“谁生产配置、谁消费配置、何时重新应用”这条链必须稳定。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先不追求所有 settings 都支持热更新，而是补一条正式的 settings apply contract，把“启动快照”和“运行时直读”显式区分。 |
| 具体步骤 | 1. 在 `AngelscriptRuntime` 新增 `FAngelscriptAppliedSettings` 或 `FAngelscriptSettingsSnapshot`，把当前真正会被 engine/init 快照的字段集中到一个结构里，并标记每个字段的 apply phase，例如 `StartupOnly`、`CompileTime`、`RuntimeLive`。 2. 在 `FAngelscriptEngine::PreInitialize_GameThread()` 中统一从 `UAngelscriptSettings` 生成 snapshot，不再让部分字段散落为 static globals、部分字段直接混用 CDO。 3. 在 `AngelscriptEditorModule` 注册 settings 时增加 `OnModified` 回调；第一阶段只做校验和提示，例如当用户修改 `StartupOnly` 字段时明确提示“需要重新初始化/重启”，对 `RuntimeLive` 字段则调用轻量 `ApplyLiveSettings()`。 4. 对当前已被运行时直读的字段做白名单化处理，例如 `EditorMaximumScriptExecutionTime` 可继续 live read，但需要进入统一 metadata，避免未来再出现“靠搜源码才知道是否 live”的情况。 5. 在 `StateDump` 或独立诊断输出里新增 `AppliedSettings.csv`，列出每个扩展相关设置当前的 apply phase、来源值和最后应用时刻。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/AngelscriptAppliedSettings.h` |
| 预估工作量 | M |
| 架构风险 | 最大风险是误把今天依赖“重启后才干净生效”的设置做成 hot-apply，导致缓存、已编译模块或 bind 名字空间状态不一致。第一阶段必须只做分类、校验和少量确定安全的 live apply。 |
| 兼容性 | 向后兼容。默认语义保持现状；新增的 apply contract 先以诊断和 editor 提示为主，不强行改变已有字段的生效时机。 |
| 验证方式 | 1. 增加自动化测试覆盖三类字段：`StartupOnly`、`CompileTime`、`RuntimeLive`，验证修改后分别得到“提示重启”“重新编译后生效”“立即生效”的预期反馈。 2. 回归 `EditorMaximumScriptExecutionTime`，确认 live 修改后 loop detection 仍读取到新值。 3. 回归 `AdditionalEditorOnlyScriptPackageNames`、namespace strip 配置和 automatic imports，确认在未触发重新初始化时行为不被意外热改。 |

### Arch-EP44：核心扩展 contract 仍是 raw delegate / `TFunction` / static API，缺少有身份的 provider object

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 自定义全局函数、type binding、compile observer、import/path policy 等核心扩展点，当前是否以“可识别、可配置、可持有生命周期”的 provider object 暴露，还是主要依赖 native callback |
| 当前设计 | 当前插件的核心扩展面仍明显偏向 native callback。`FAngelscriptRuntimeModule` 公开的是一组 `DECLARE_DELEGATE` / `DECLARE_MULTICAST_DELEGATE` accessor；`FAngelscriptEngineDependencies` 是 `TFunction` 聚合；`FAngelscriptType` 则要求扩展者在 C++ 中派生虚类并调用 static `Register()` / `RegisterTypeFinder()`。这说明插件确实“能扩展”，但扩展参与者没有 `UObject/UInterface` 身份、没有 config 可引用 identity，也没有统一生命周期。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h:9`-`:21` 定义 raw delegates，`:32`-`:47` 再以 static accessor 暴露；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:86`-`:95` 把 script root 等宿主策略建模成 `TFunction`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:71`-`:81` 提供 static `Register()` / `RegisterTypeFinder()`，`:98`-`:209` 则要求扩展者通过 C++ `virtual` 实现 type operations；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:54`-`:118` 说明这些扩展最终都直接进入 engine-local database；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h:41`-`:225` 全文是标量/数组配置，可推断当前没有 `TSubclassOf<>` 或 provider class 字段来承载这些扩展参与者。 |
| 优点 | 对熟悉插件内核的 C++ 扩展者来说，这种 contract 很直接，零额外 `UObject` 开销，也不会被 UObject 生命周期和反射限制束缚。 |
| 不足 | 这种 shape 让扩展者必须先写宿主原生模块和 startup glue，才能谈“扩展”。provider 没有 class identity，就很难进入 settings、diagnostics、state dump 和 editor 校验链。结果是：添加自定义全局函数/类型绑定虽然可行，但天然是 native-only expert path；更高阶的 import resolver、compile policy、bind contributor 也难以产品化为可配置能力。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | 把 assembly loading 做成 `UAssemblyLoader` 这个 `UObject` provider，settings 中直接保存 `TSubclassOf<UAssemblyLoader>`，运行时再统一通过 `GetAssemblyLoader()` 取当前 provider 并调用 `Load()`。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Domain/AssemblyLoader.h:13`-`:18`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpSetting.h:140`-`:144`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Setting/UnrealCSharpSetting.cpp:87`-`:91`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp:1106`-`:1110`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp:500`-`:503` | 当扩展参与者有 class identity 后，settings、runtime 消费者和验证逻辑才能围绕同一个 provider contract 收敛。 |
| UnLua | 一部分核心扩展直接做成 `UObject/UInterface`：`UUnLuaSettings` 暴露 `EnvLocatorClass` / `ModuleLocatorClass`，宿主对象则可实现 `IUnLuaInterface::GetModuleName()` 以声明脚本入口，且该接口还是 `BlueprintNativeEvent`。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaSettings.h:47`-`:56`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaModule.cpp:103`-`:128`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaInterface.h:23`-`:39` | 不必把所有扩展点都做成 raw callback；关键 runtime 路由点更适合有身份、可实例化、可配置的 provider object。 |
| puerts | 把模块能力和 module loader 分别做成 `IPuertsModule` 与 `IJSModuleLoader`，外部扩展不需要直接碰内部 lambda 或全局静态状态，而是面向正式 interface 编程。 | `Reference/puerts/unreal/Puerts/Source/Puerts/Public/PuertsModule.h:17`-`:55`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSModuleLoader.h:17`-`:50` | interface/provider object 可以保留多态能力，同时提供明确的身份、生命周期和组合边界。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 保留现有 raw delegate / static register 作为 expert-mode 底层接口，同时在其上补一层 typed provider contract，让核心扩展参与者拥有 class identity、settings identity 和生命周期。 |
| 具体步骤 | 1. 在 `Public/Extension/` 新增一组最小 provider contract，例如 `UAngelscriptExtensionProviderBase`、`IAngelscriptCompileObserver`、`IAngelscriptImportResolverProvider`、`IAngelscriptBindingContributor`；第一阶段不覆盖所有扩展点，只挑 import resolver、compile observer、binding contributor 三类高价值场景。 2. 在 runtime module/extension registry 中增加 provider discovery 与实例化逻辑：若 settings/manifest 配置了 provider class，就创建对象并把它桥接到现有 `GetPreCompile()`、`RegisterTypeFinder()`、未来 import resolver seam；若没有，则继续走 legacy raw callback。 3. 为 provider 补最小 identity metadata，例如 `ProviderName`、`SourceModule`、`Priority`、`SupportsLiveReload`，并把这些信息纳入 `StateDump`。 4. 对已存在的 raw callback 扩展者保持兼容，但新增一条官方样板模块，演示“只实现 provider class + 配置 manifest”即可挂接扩展。 5. 在 editor settings 或 manifest 校验中验证 provider class 是否实现预期接口，避免今天这种“编译通过但启动阶段静默不生效”的 native-only 心智负担。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/AngelscriptExtensionProvider.h` |
| 预估工作量 | M |
| 架构风险 | 如果一步把所有 raw callback 全改成 provider object，容易引入初始化顺序和 module 依赖回归。第一阶段应采用“provider overlay”策略，只把 provider 结果桥接到现有底层接口。 |
| 兼容性 | 向后兼容。现有 `FAngelscriptBinds::BindGlobalFunction()`、`FAngelscriptType::Register()`、`GetPreCompile()` 等 expert-mode 扩展路径全部保留；新 provider contract 只是新增一条正式、可配置的扩展通路。 |
| 验证方式 | 1. 新建一个宿主 provider 类，验证无需手写额外 startup glue 即可通过 manifest/settings 接入 compile observer 或 binding contributor。 2. 回归现有原生扩展路径，确认 legacy callback 与 provider overlay 可共存。 3. 运行 `StateDump`，确认每个启用的 provider 都能看到 class、source module、priority 与桥接到的底层 hook。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-EP44 | 核心扩展仍是 native callback contract，缺少 provider object 身份 | 扩展 contract 分层 | 高 |
| P2 | Arch-EP43 | settings 同时存在快照与直读语义，但没有正式 apply contract | 配置语义收口 | 中高 |

---

## 架构分析 (2026-04-08 23:55)

### Arch-EP45：`UHT function-table` 的直绑/回退策略仍是 closed-world，用户无法正式定制生成期绑定行为

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 用户想改变 `BlueprintCallable` 绑定行为时，能否在不改插件源码的前提下控制“哪些函数生成 direct bind、哪些函数强制 reflective fallback、哪些函数直接 skip” |
| 当前设计 | 本轮补查 `AngelscriptUHTTool` 后可以确认，这条策略目前仍是内建 `if/else`。`AngelscriptFunctionTableExporter` 会在 UHT 阶段固定执行，`AngelscriptFunctionTableCodeGenerator` 先从 `AngelscriptRuntime.Build.cs` 反推允许生成的 module，再在 `ShouldGenerate()` 里硬编码 header 可见性、metadata 和 `CustomThunk` 过滤；运行时 `Bind_BlueprintCallable()` 则先看 `ClassFuncMaps` 里是否有 generated direct entry，没有才尝试 `BindBlueprintCallableReflectiveFallback()`。项目侧既没有 `ini/settings`，也没有 public policy object 可以改这条决策链。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:21`-`:24` 把 exporter 固定注册为 `ModuleName = "AngelscriptRuntime"`；`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:53`-`:75` 遍历 `factory.Session.Modules`，但先经过 `LoadSupportedModules()` 过滤；`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:334`-`:383` 直接解析 `AngelscriptRuntime.Build.cs` 推断支持模块；`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:490`-`:514` 在 `ShouldGenerate()` 里硬编码 `NotInAngelscript`、`BlueprintInternalUseOnly`、`CustomThunk` 和 `UUniversalObjectLocatorScriptingExtensions` 特例；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp:17`-`:47` 先从 `GetClassFuncMaps()` 取 generated entry，`:72`-`:76` 未命中时转 `BindBlueprintCallableReflectiveFallback()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp:254`-`:287` 的 eligibility 也只按 `CustomThunk` 和参数数量等内部规则判断；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h:49`-`:124` 虽有 `bDefaultFunctionBlueprintCallable`、`DisabledBindNames` 等开关，但没有 module/class/function 级的 function-table policy。 |
| 优点 | 生成策略简单、可重复、默认项目行为稳定；UHT 阶段和 runtime 阶段都没有额外 provider 生命周期复杂度。 |
| 不足 | 这意味着“修改绑定行为”在 generated function table 这条主路径上仍基本等价于改 `AngelscriptUHTTool` 或改 runtime binder。项目无法正式声明“这个 module 不生成 direct bind”“这类函数一律走 reflective fallback”“这个 metadata 表示 opt-in direct bind”，扩展点缺口正好落在用户最想自定义的 binding policy 上。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | 生成边界先由 settings 显式声明，再由 generator 阶段广播 begin/end 事件。`bEnableExport`、`ExportModule`、`ClassBlacklist` 决定导出范围，`Generator()` 再通过 `OnBeginGenerator` / `OnEndGenerator` 给其它模块稳定接入点。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpEditorSetting.h:140`-`:148`；`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:237`-`:309`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Delegate/FUnrealCSharpCoreModuleDelegates.h:13`-`:30` | 先把“生成什么”做成 manifest/policy，再让生成阶段事件保持 observe-friendly，扩展者就不必回到 generator 内部改分支。 |
| puerts | `IPuertsModule` 公开 `GetIgnoreClassListOnDTS()` 与 `InitExtensionMethodsMap()`，`DeclarationGenerator` 和 runtime `JsEnvImpl` 又共享 `UExtensionMethods` 这套显式 class family。 | `Reference/puerts/unreal/Puerts/Source/Puerts/Public/PuertsModule.h:41`-`:49`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Public/ExtensionMethods.h:18`-`:20`；`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:225`-`:228`、`:315`-`:351`、`:369`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:931`-`:983` | 即便继续保留自动扫描，也应给项目一个正式 filter surface 和显式 class contract，而不是把生成/回退规则锁死在工具内部。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 给 `UHT function-table` 新增一层 project-visible `binding policy`，把今天分散在 UHT tool 与 runtime binder 里的直绑/回退决策显式化。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptUHTTool/` 新增 `AngelscriptFunctionTablePolicy` schema，第一阶段可落成 `Intermediate/Angelscript/FunctionTablePolicy.json`，字段至少包含 `AllowedModules`、`DeniedClasses`、`DeniedFunctions`、`ForceReflectiveFallbackFunctions`、`ForceDirectBindFunctions`、`ExtraOptInMetadataTags`。 2. 让 `AngelscriptFunctionTableCodeGenerator` 在 `LoadSupportedModules()` / `ShouldGenerate()` 前先读取 policy；若 policy 缺失则完整回退到当前 `Build.cs + metadata + hardcoded special cases` 行为，保证旧项目不受影响。 3. 扩充 generated shard 注册信息：让 `BuildShard()` 或 `FFuncEntry` 附带 `EntryKind` / `PolicySource` / `WhyGenerated`，避免 runtime 只能通过“有没有 entry”来猜 direct bind 还是 fallback。 4. 在 `Bind_BlueprintCallable.cpp` 中优先消费显式 policy 决策；当 policy 要求 `ForceReflectiveFallback` 时，即使存在 generated stub 也走 fallback 路径；当 policy 要求 `Skip` 时，输出明确 diagnostics，而不是静默依赖 UHT 过滤。 5. 补三类测试：module allowlist、function force-fallback、function explicit skip，确保 policy 既能改变生成结果，也能改变 runtime 绑定决策。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/FunctionCallers.h`、新增 `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTablePolicy.cs` |
| 预估工作量 | L |
| 架构风险 | 最大风险是 UHT policy 与 runtime policy 不一致，导致“生成时认为 direct bind、运行时又被改成 fallback”这类双重决策漂移。第一阶段必须坚持单一 policy 文件、双端共享读取逻辑。 |
| 兼容性 | 向后兼容。缺省 policy 文件时完全保留当前行为；新字段只在项目主动声明时生效。 |
| 验证方式 | 1. 增加 UHT tool 测试，验证 policy 能改变 `AS_FunctionTable_*.cpp` 与 `AS_FunctionTable_Entries.csv`。 2. 增加 runtime 测试，验证相同函数在 `ForceReflectiveFallback` 下不会再走 generated direct entry。 3. 回归现有 generated function table tests，确保未提供 policy 的项目仍产出与当前一致的 shard/summary。 |

### Arch-EP46：`AS_FunctionTable_*` 产物与 runtime `BindDatabase/ClassFuncMaps` 仍是两套事实来源，扩展者看不到最终生效面

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 生成期 direct bind、stub、skip 结果，是否会进入 runtime 的统一 manifest、`StateDump`、`BindDatabase` 和文档面，供扩展者确认“最终到底暴露了什么” |
| 当前设计 | 当前仓库已经有一套很完整的 generated diagnostics，但它仍是 write-only artifact。UHT exporter 会写 `AS_FunctionTable_Summary.json`、module/entry CSV、skipped CSV 和 skipped-reason summary；generated shard 再通过 `FAngelscriptBinds::FBind` 调 `AddFunctionEntry()` 把 direct entry 塞进 `ClassFuncMaps`。问题在于，这条产物链和 runtime introspection 没有汇合：`FBindInfo` 不记录 `SourceModule`/`EntryKind`，`AddFunctionEntry()` 也只改 `ClassFuncMaps`；`StateDump` 虽然会导出 `BindRegistrations` 和 `BindDatabase_Classes`，但 `BindModule` 列是空的，也不会读取任何 `AS_FunctionTable_*` artifact。结果是扩展者能在 UHT 输出目录里看到一套“生成结果”，也能在 runtime 里看到另一套“绑定结果”，却没有正式地方把两者对齐。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:35`-`:44` 会在导出后写 skipped diagnostics，`:101`-`:142` 生成 `AS_FunctionTable_SkippedEntries.csv` 与 `AS_FunctionTable_SkippedReasonSummary.csv`；`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:174`-`:246` 生成 `AS_FunctionTable_Summary.json`、`AS_FunctionTable_ModuleSummary.csv`、`AS_FunctionTable_Entries.csv`，`:302`-`:320` 的 shard 再用 `AS_FORCE_LINK const FAngelscriptBinds::FBind` 注册 entry；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:431`-`:436` 的 `FBindInfo` 只有 `BindName`、`BindOrder`、`bEnabled`，`:497`-`:510` 的 `AddFunctionEntry()` 只写 `ClassFuncMaps`；`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp:150`-`:224` 在 `DumpAll()` 中并不会纳入任何 `AS_FunctionTable_*` 表，`:221`-`:222` 甚至只硬编码检查两张 editor 扩展表；`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp:853`-`:867` 的 `DumpBindRegistrations()` 虽然声明了 `BindModule` 列，但实际写入的是空字符串；`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp:459`-`:721` 只验证 `AS_FunctionTable_*.json/csv` artifact 存在与内容正确，没有把这些 artifact 和 `StateDump` / runtime bind facts 对齐。 |
| 优点 | 当前 generated diagnostics 已经相当完整，而且有专门测试覆盖 summary、entry、skip 和 skip-reason artifact，这为后续统一 manifest 提供了很好的原始材料。 |
| 不足 | 但只要这两套事实来源不统一，扩展作者就仍然要在“UHT 生成目录”“runtime class func map”“bind database”“state dump”之间来回拼图。尤其当函数既可能由 generated direct bind 命中，也可能在 runtime 里转 reflective fallback 时，当前系统没有正式地方给出最终判决。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | `FBinding` 是 generator 和 runtime 共享的 binding registry：`FBindingClassGenerator` 从 `FBinding::Get().GetClasses()` 生成绑定壳，`FMonoDomain::RegisterBinding()` 又从同一 registry 取 class/method 注册 runtime ABI。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Binding/FBinding.h:14`-`:31`；`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FBindingClassGenerator.cpp:10`-`:14`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp:783`-`:787` | 生成期与运行期共享同一份 binding registry，扩展者不必维护“两套真相”。 |
| puerts | `UExtensionMethods` 既被 declaration generator 消费，也被 runtime `JsEnvImpl` 消费；同一个 `ExtensionMethodsMap` 语义横跨两条 phase，而不是一边生成、一边再各自扫描一套无关结果。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/ExtensionMethods.h:18`-`:20`；`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:225`-`:228`、`:315`-`:351`、`:1157`-`:1160`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:931`-`:983`、`:3052`-`:3065` | 若同一扩展既影响工具输出又影响运行时行为，就应复用同一份 descriptor/map。 |
| UnLua | `ModuleLocatorClass` 这样的 typed contract 同时被 runtime `LuaEnv` 与 editor `UnLuaEditorCore` 消费，用户只需理解一份 settings/object contract。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaSettings.h:49`-`:56`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:78`-`:79`、`:375`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorCore.cpp:39`-`:43` | 即使 phase 不同，也应围绕同一 contract 做消费，而不是各自产生 side artifact。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 `AS_FunctionTable_*`、`ClassFuncMaps`、`BindDatabase` 和 `StateDump` 收口到同一份 `binding surface manifest`，让生成期与运行期共享统一事实来源。 |
| 具体步骤 | 1. 在 `AngelscriptUHTTool` 新增结构化 `AngelscriptBindingSurfaceManifest.json`，每条记录至少包含 `ModuleName`、`ClassName`、`FunctionName`、`EntryKind(Direct/Stub/ReflectiveExpected/Skipped)`、`SkipReason`、`GeneratedShard`、`SourceHeader`、`GeneratedAt`。 2. 在 runtime 初始化时加载该 manifest，并扩充 `FAngelscriptBinds::FBindInfo` 或新增 `FAngelscriptGeneratedBindInfo`，把 generated provenance 合并进 `BindState`/`BindDatabase`，而不是只把 `FFuncEntry` 塞进 `ClassFuncMaps`。 3. 改造 `DumpBindRegistrations()` 与 `DumpBindDatabaseClasses()`：填充真实 `BindModule`，并新增 `EntryKind`、`SkipReason`、`SourceHeader`、`GeneratedShard` 列；同时把 `AS_FunctionTable_Summary.json` / skipped diagnostics 摘要收进新的 `GeneratedFunctionBindings.csv`。 4. 把现有 generated function table tests 与 dump tests 串起来，新增一条回归：同一轮 UHT 产物应当能在 `StateDump` 中看到相同的 module/function/skip reason，而不是只在 `Intermediate` 目录里可见。 5. 后续 `Docs`、IDE 导出和扩展诊断统一只消费这份 manifest，避免继续各自扫 `ClassFuncMaps`、CSV 和 bind database。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Dump/AngelscriptDumpTests.cpp` |
| 预估工作量 | M-L |
| 架构风险 | 最大风险是 manifest 与实际生成的 shard 或 runtime 应用结果不同步，导致用户看到“manifest 说 direct bind，runtime 实际走 fallback”的伪一致。第一阶段必须给 manifest 增加版本/hash，并在 runtime 读入时做 mismatch warning。 |
| 兼容性 | 向后兼容。现有 `AS_FunctionTable_*.json/csv`、`ClassFuncMaps` 和 `BindDatabase` 全部保留；新 manifest 只是把已有信息收口并补 provenance，不改变现有绑定行为。 |
| 验证方式 | 1. 生成一轮 `AS_FunctionTable_*` 后执行 `StateDump`，确认 `GeneratedFunctionBindings.csv` 与 UHT artifact 在 module/function/entry-kind 上一致。 2. 构造一个会被 `SkippedEntries.csv` 记录的函数，验证相同 `SkipReason` 会同时出现在 runtime dump。 3. 回归现有 generated function table tests 与 dump tests，确保旧 artifact 仍生成，且新 manifest 不破坏现有流程。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-EP45 | `UHT function-table` 直绑/回退策略仍是 closed-world，项目无法正式定制生成期绑定行为 | build-time policy contract 新增 | 高 |
| P2 | Arch-EP46 | `AS_FunctionTable_*` 产物与 runtime bind facts 各自为政，缺少统一 binding manifest | 生成期/运行期事实来源收口 | 中高 |

---

## 架构分析 (2026-04-09 00:06)

### Arch-EP47：扩展相关 settings 仍以裸 `FName/FString` 选择器为主，缺少 option-backed 与 rename-safe contract

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `ini/settings` 中用于控制扩展行为的选择器，是否已经具备类型安全、候选项发现和保存期校验 |
| 当前设计 | 当前最接近“扩展控制面”的配置，仍主要是 `DisabledBindNames`、`AdditionalEditorOnlyScriptPackageNames`、`DebuggerBlacklistAutomaticFunctionEvaluation`、`DebuggerBlacklistAutomaticFunctionEvaluationWithoutWorldContext` 这类裸 `FName/FString` 集合。runtime 侧直接按 `Contains()` 消费这些字符串/名字；editor 侧只注册 settings 页面，没有把运行时已有的 bind/function/package 信息反向喂回 UI 做候选项或校验。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h:59`、`:201`、`:207`、`:214` 定义了上述四类裸选择器；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1928`-`:1941` 直接把 `DisabledBindNames` 合并到运行时集合；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:917` 用 `AdditionalEditorOnlyScriptPackageNames.Contains(...)` 决定是否过滤 class；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:681`-`:688` 用字符串形式的 `FunctionPath` 命中 debugger blacklist；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:388`-`:395` 只做 `RegisterSettings(...)`；与此同时，runtime 其实已经提供 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:473`-`:474` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:161`-`:181` 的 `GetAllRegisteredBindNames()` / `GetBindInfoList()`，但它们没有回流成 settings 候选项。 |
| 优点 | 实现成本低，`ini` 可直接修改；对少量 expert-only 项目来说，手写名称也足够灵活。 |
| 不足 | 这是一种典型的 stringly-typed contract：拼写错误、类/函数重命名、脚本名变换后都容易静默失效；用户也无法从 Project Settings 直接知道“哪些 bind 名字是合法的”“debugger blacklist 应填 C++ 名、脚本名还是最终暴露名”。插件已经有 introspection 数据，却没有形成“可发现 -> 可编辑 -> 可验证”的闭环。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | settings 直接把候选项接到 UI：`SupportedModule`、`ExportModule`、`ClassBlacklist` 都通过 `GetOptions = "GetModuleList"` / `GetOptions = "GetClassList"` 绑定动态候选集，底层再由 `GetModuleList()` / `GetClassList()` 提供合法值。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpEditorSetting.h:123`、`:143`、`:147`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Setting/UnrealCSharpEditorSetting.cpp:299`、`:320` | 即便最终仍保存为字符串，编辑面也先做 option-backed contract，显著降低拼写和 discoverability 成本。 |
| UnLua | 把关键扩展选择器做成 typed settings：`EnvLocatorClass` / `ModuleLocatorClass` 用 `TSubclassOf<>`，`PreBindClasses` 用 `FSoftClassPath`；模块注册 settings 后还会绑定 `Section->OnModified()`，在保存时更新运行期状态。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaSettings.h:49`、`:52`、`:56`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaModule.cpp:104`-`:112`、`:249`、`:272` | 与其让扩展控制面长期停留在裸字符串，不如尽量提升为 typed class/path selector，并补保存期 apply/validation。 |
| puerts | 过滤面没有挂在一个泛化的字符串 settings 页里，而是收口到模块接口：`IPuertsModule::GetIgnoreClassListOnDTS()` 提供 generator 使用的忽略类列表，消费方直接围绕该 API 工作。 | `Reference/puerts/unreal/Puerts/Source/Puerts/Public/PuertsModule.h:49`；`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:369`、`:861` | 即便短期还保留字符串标识，也应先把“谁产生、谁消费”做成正式 API，而不是只把裸值暴露给默认 settings 页。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先把现有裸 `FName/FString` 选择器包成结构化 selector，再让 editor 基于 runtime introspection 提供候选项与保存期校验。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/` 或 `Public/Extension/` 新增轻量 selector 类型，例如 `FAngelscriptBindSelector`、`FAngelscriptFunctionSelector`、`FAngelscriptPackageSelector`，第一阶段只做序列化和显示，不改 runtime 语义。 2. 给 `UAngelscriptSettings` 增加对应新字段，内部仍可在加载期映射回现有 `DisabledBindNames` / debugger blacklist / package-name 集合。 3. 在 `AngelscriptEditorModule` 注册 settings 时增加自定义详情或 `OnModified` 校验：bind 选项来自 `GetBindInfoList()`，function 选项来自 bind database / generated manifest，package 选项来自当前已发现的 script package。 4. 保存时对未知 selector 输出明确 warning，并在 `StateDump` 新增 `InvalidExtensionSelectors.csv` 或等价摘要，避免问题只在运行时静默暴露。 5. 待新 selector 跑稳后，再把旧裸数组字段标记为 legacy alias，仅用于兼容老 `ini`。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/AngelscriptExtensionSelectors.h/.cpp` |
| 预估工作量 | M |
| 架构风险 | editor 期拿到的 bind/function 候选集可能受初始化时机影响，不一定在 settings 页面打开时已经完整；第一阶段应以 warning 和候选增强为主，避免把“暂时未发现”误判成硬错误。 |
| 兼容性 | 向后兼容。现有 `DisabledBindNames`、debugger blacklist 和 package-name 配置继续可读；新 selector 只是补上结构化入口、候选项和校验。 |
| 验证方式 | 1. 新增 settings 测试或 editor automation：故意填错 bind 名、函数路径和 package 名，验证保存时能收到明确诊断。 2. 用已有 `GetBindInfoList()` 驱动 settings 候选项，检查内建 bind 与外部 bind 都能在 UI 中列出。 3. 回归 debugger blacklist 与 editor-only package 过滤，确认旧 `ini` 在迁移前后行为一致。 |

### Arch-EP48：`DisabledBindNames` 的 kill-switch 依赖不稳定的 contributor identity，尤其是 `UnnamedBind_n`

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 当前插件是否已经具备“可长期维护的扩展灰度/回滚 ID”，还是只是在启动时按临时注册顺序生成禁用键 |
| 当前设计 | 当前唯一正式的 bind 灰度入口是 `DisabledBindNames`，但默认 `FBind` authoring model 并不要求提供稳定名字。无名 `FBind` 最终会落成自增的 `UnnamedBind_n`；而仓内高价值的反射扩展入口 `Bind_BlueprintType_Declarations` 恰好就是无名 `FBind(EOrder::Early, ...)`。结果是：表面上有 kill-switch，实际上很多扩展批次没有稳定、可文档化、可跨版本沿用的 ID。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:460`-`:467` 的 `FBind(EOrder BindOrder, TFunction<void()> Function)` 与 `FBind(TFunction<void()> Function)` 都不会传入 `BindName`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:138`-`:141` 的 `MakeUnnamedBindName()` 用自增 `NextUnnamedBindId` 生成 `UnnamedBind_%d`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:151`-`:158` 在 `BindName.IsNone()` 时自动回退到该名字；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1928`-`:1941` 收集的 `DisabledBindNames` 最终只按名字工作，而 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:195`-`:206` 也是在 `CallBinds()` 时按 `BindName` 跳过；对照 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:712` 与 `:1029`，`Bind_BlueprintType_Declarations` 是无名 bind，随后 `:1034` 与 `:1366` 还会扫描全部 `TObjectRange<UClass>()` 建立反射扩展面。 |
| 优点 | 对 bind 作者非常省事，写一个无名 `FBind` 就能接入系统，不必先设计稳定 ID。 |
| 不足 | 这让 rollout/回滚 contract 变得脆弱：只要 TU 顺序、代码增删或新 bind 插入导致 `UnnamedBind_n` 序号变化，原有 `DisabledBindNames` 配置就可能失效；更严重的是，像 `Bind_BlueprintType` 这种大批量 reflective lane 会被一个不可预测的 auto-ID 包住，用户几乎无法做稳定灰度。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | 导出边界围绕稳定的 module/class identity：`ExportModule` 和 `ClassBlacklist` 直接指向模块名与类名，而不是某次运行时注册顺序。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpEditorSetting.h:143`-`:147`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Setting/UnrealCSharpEditorSetting.cpp:299`、`:320` | kill-switch/过滤面最好绑定到稳定的逻辑身份，而不是临时 registration slot。 |
| puerts | 扩展方法和 DTS 过滤都依赖显式 class-level identity：`InitExtensionMethodsMap()` 先建立 extension-method map，`GetIgnoreClassListOnDTS()` 再按类名过滤 declaration generation。 | `Reference/puerts/unreal/Puerts/Source/Puerts/Public/PuertsModule.h:43`-`:49`；`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:315`、`:369`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:931` | 即便仍是字符串名，也应围绕稳定 class identity 工作，而不是 `UnnamedBind_n` 这种序号键。 |
| UnLua | 关键 runtime seam 的身份也来自显式 class/path：`EnvLocatorClass`、`ModuleLocatorClass`、`PreBindClasses` 都是可长期保存的 provider/class 标识。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaSettings.h:49`-`:56`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaModule.cpp:104`-`:112` | 当扩展点要进入配置面时，首先要保证其 identity 本身是稳定的。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 给每个 bind contributor 引入稳定 `ExtensionId`，把今天的 `BindName` 从“有时是作者命名，有时是自增序号”升级成正式、可迁移的灰度键。 |
| 具体步骤 | 1. 在 `FAngelscriptBinds::FBindInfo` 旁新增 `ExtensionId` / `LegacyBindName` / `SourceModule` 字段，或新增 `FAngelscriptBindDescriptor`；第一阶段只要求新 public API 和内建高价值 bind 填这个稳定 ID。 2. 为现有无名内建 bind 补显式 ID，优先处理 `Bind_BlueprintType_Declarations`、generated function-table shard、核心 diagnostics/dump bind 等 rollout 价值高的批次。 3. 在 `UAngelscriptSettings` 与 `FAngelscriptEngineConfig` 中新增 `DisabledExtensionIds`；运行时先按新 ID 禁用，再兼容回读 `DisabledBindNames` 并做 alias 映射。 4. 对 `Bind_BlueprintType` 这类范围过大的批次，再拆成更细的 stable 子 ID，例如 `Reflective.BlueprintCallable`、`Reflective.Mixin`、`Reflective.EventOverride`，避免一个不可见大批次承载全部 reflective 扩展。 5. 在 `StateDump` 中输出 `ExtensionId -> LegacyBindName -> SourceModule -> SourceFile` 映射，配合日志提示用户把旧 `DisabledBindNames` 迁移到新键。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp` |
| 预估工作量 | M |
| 架构风险 | 为现有内建 bind 赋新 ID 时，若没有 alias/migration map，老项目里依赖 `DisabledBindNames` 的配置会失效；因此第一阶段必须保留 legacy 名称回读，并在 dump/log 中同时显示新旧键。 |
| 兼容性 | 向后兼容。现有 `DisabledBindNames` 继续有效；`DisabledExtensionIds` 只是新增更稳定的灰度面。旧的无名 bind 在迁移期仍可运行，但应输出 warning 提示其 kill-switch 不稳定。 |
| 验证方式 | 1. 新增 bind identity 测试：插入一个新的无关 bind 后，已显式赋 ID 的核心 bind 仍保持相同 `ExtensionId`。 2. 用 `DisabledExtensionIds` 禁用 `Bind_BlueprintType` 的显式子 ID，验证 reflective extension lane 可被稳定关闭，而不是依赖 `UnnamedBind_n`。 3. 复跑现有 `DisabledBindNames` 相关回归，确认旧配置在 alias 兼容层下仍能命中。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-EP48 | `DisabledBindNames` 依赖 `UnnamedBind_n`，扩展灰度键不稳定 | 扩展 identity 与 kill-switch 收口 | 高 |
| P2 | Arch-EP47 | 扩展相关 settings 仍是裸字符串选择器，缺少候选项与保存期校验 | 配置 contract 产品化 | 中高 |

---

## 架构分析 (2026-04-09 00:21)

### Arch-EP49：`StaticJIT` 自定义 lowering 仍依赖 `GetPreviousBind()` side effect，缺少正式的 native-call descriptor

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 插件用户给 Angelscript 新增自定义 global function / `UFunction` / native helper 后，能否在不改插件源码的情况下，同时声明该绑定的 `StaticJIT` lowering、header 依赖和 trivial-call 语义 |
| 当前设计 | 当前普通绑定扩展面与 `StaticJIT` 优化扩展面是分离的。`FAngelscriptBinds::BindGlobalFunction()` 已经是正式 public API，但它只负责向 script engine 注册函数；真正的 native-call lowering contract 则藏在 `FScriptFunctionNativeForm` 这一组 `virtual` 方法与 `BindNativeFunction()` / `BindUFunction()` 等 static helper 中，而且注册是通过 `GScriptNativeForms.Add(FAngelscriptBinds::GetPreviousBind(), ...)` 这种“上一条刚绑定完的函数” side effect 完成。结果是：添加自定义全局函数本身不需要改插件源码，但如果用户还想给该函数声明自定义 `StaticJIT` lowering、header 注入或 custom call，大多需要直接包含 `StaticJIT/StaticJITBinds.h`、理解内部注册时序，甚至继续修改插件内部实现。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:606`-`:607` 对外公开 `BindGlobalFunction()` overload；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:588`-`:600` 说明常规 global function 绑定与 `SCRIPT_NATIVE_FUNCTION(...)` 只是松耦合地串在一起；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:299`-`:305` 的 `GetPreviousBind()` 明确暴露了“依赖最近一次绑定结果”的 side channel；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.h:25`-`:64` 把 `GenerateCall()`、`CanCallCustom()`、`GenerateCustomCall()` 等 lowering 决策集中在 `FScriptFunctionNativeForm` 的 `virtual` 接口中，`:47`-`:49` 再对外公开 `BindNativeFunction()` / `BindUFunction()`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.cpp:27`-`:60` 以 process-global `GScriptNativeForms` 查找 lowering；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.cpp:401`-`:405` 与 `:530`-`:534` 则把 `GetPreviousBind()` 作为 key 写入 lowering map。 |
| 优点 | 当前实现对内核开发者很直接，零额外 descriptor/object 开销，`StaticJIT` 能以很低成本复用现有 bind 注册时序。 |
| 不足 | 这是一个典型的 expert-only seam。扩展作者若只想“注册函数”可以走 public API，但一旦想“声明该函数如何被 `StaticJIT` 优化”，就必须进入 internal header、全局 map 和注册时序领域。它没有稳定 identity、没有 dump/diagnostics、也没有正式的 provider/descriptor contract，因此不利于插件外部扩展包长期维护。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | 把 binding authoring 先收口为 `FBinding` registry，再由 generator 和 runtime 共同消费：`FBindingClassGenerator::Generator()` 从 `FBinding::Get().GetClasses()` 遍历生成代码，`FMonoDomain::RegisterBinding()` 又从同一 registry 读取 methods 注册 runtime internal call。扩展者声明绑定时，不需要再依赖“上一条刚注册的函数”这种 side effect。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Binding/FBinding.h:8`-`:46`；`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FBindingClassGenerator.cpp:10`-`:15`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp:783`-`:790` | 如果某个扩展既影响 authoring，又影响运行期执行路径，应先把它提升成稳定 registry/descriptor，而不是隐含在 bind 时序里。 |
| puerts | `IPuertsModule` 把扩展方法初始化与过滤能力做成正式 module contract，消费方围绕接口调用，而不是围绕某个隐式“前一条注册项”。`DeclarationGenerator` 启动时显式调用 `InitExtensionMethodsMap()`，runtime `JsEnvImpl` 也有对应初始化逻辑。 | `Reference/puerts/unreal/Puerts/Source/Puerts/Public/PuertsModule.h:17`-`:49`；`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:225`-`:229`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:931`-`:945` | 即使底层最终仍落成 map，也应该先给外部扩展者一个正式、具名、可组合的 contract，而不是让其依赖内部注册时序。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留现有 `FScriptFunctionNativeForm` 与 `SCRIPT_NATIVE_*` 宏的前提下，新增一层 public `native-call descriptor` contract，把“绑定函数”和“声明其 `StaticJIT` lowering”合并成同一条正式扩展路径。 |
| 具体步骤 | 1. 在 `AngelscriptRuntime/Public/Extension/` 新增轻量结构，例如 `FAngelscriptNativeCallDescriptor`，至少包含 `EntryKind`、`CallMethod`、`Header`、`bTrivial`、`bIgnoreObjectArgument`、`SupportsCustomCall`、可选 `CustomProviderClass` 等字段。 2. 在 `FAngelscriptBinds` 上新增显式 API，例如 `RegisterNativeCallDescriptor(int FunctionId, const FAngelscriptNativeCallDescriptor&)`，并提供 `RegisterPreviousBindNativeCallDescriptor(...)` 仅作为 legacy shim；外部模块以后不必直接 subclass `FScriptFunctionNativeForm` 或访问 `GScriptNativeForms`。 3. 在 `StaticJIT` 内部增加一个 adapter，把新 descriptor 转成现有 `FScriptFunctionNativeForm` 或等价 lowering object；旧的 `SCRIPT_NATIVE_FUNCTION` / `BindUFunction()` 继续写入同一套 descriptor registry，保证现有内建绑定不变。 4. 在 `StateDump` 中新增 `NativeCallLowerings.csv` 或等价输出，至少列出 `FunctionId/Signature -> DescriptorSource -> Header -> CallMethod -> LegacyPath`，让扩展者能看见 lowering 是否真正注册。 5. 增加黑盒扩展测试：外部测试模块只通过 public bind API + descriptor API 注册一个自定义 global function，验证普通解释执行与 `StaticJIT` 两条路径都能命中相同 lowering，而无需编辑插件内核源码。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/AngelscriptNativeCallDescriptor.h`、新增 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeCallLoweringExtensionTests.cpp` |
| 预估工作量 | M |
| 架构风险 | 主要风险是 descriptor registry 与现有 `FScriptFunctionNativeForm`/`GetNativeForm()` 语义短期并存，若桥接不完整，可能出现“解释执行正常但 `StaticJIT` 没命中 descriptor”的双轨不一致。第一阶段应保留旧 map 作为 backend，只把 public contract 前移。 |
| 兼容性 | 向后兼容。现有 `BindGlobalFunction(..., FuncName, bTrivial)`、`SCRIPT_NATIVE_*` 宏和内建 `FScriptFunctionNativeForm` 子类继续有效；新 descriptor API 只是新增一条正式扩展路由。 |
| 验证方式 | 1. 新建一个插件外测试模块，仅依赖 public header，为自定义 global function 注册 descriptor，确认不改 `Plugins/Angelscript` 内核源码也能拿到 `StaticJIT` lowering。 2. 回归现有内建 bindings，确认旧宏路径仍能通过 `GetNativeForm()` 命中。 3. 运行 `StateDump`，验证新增的 lowering dump 能列出 legacy 和 descriptor 两类来源，并能定位未注册 lowering 的函数。 |

### Arch-EP50：`UHT/codegen` 仍只有固定 exporter 与 shard emitter，扩展包无法插入自定义生成期 pass

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 扩展作者若想在 function-table 生成阶段追加自己的声明文件、IDE artifact、分析报告或附加注册代码，是否存在不修改插件源码的正式 pass/hook |
| 当前设计 | 当前 `AngelscriptUHTTool` 的生成流程仍是 closed-world。`AngelscriptFunctionTableExporter` 以固定 `UhtExporter` 身份注册，入口里直接调用 `AngelscriptFunctionTableCodeGenerator.Generate(factory)`；后者再固定地解析 `AngelscriptRuntime.Build.cs` 选择模块、固定执行 `ShouldGenerate()` 过滤，并在 `BuildShard()` 中直接拼出 `FAngelscriptBinds::AddFunctionEntry(...)` 的 shard 内容。整个链路没有 `pre/post pass` delegate、没有 interface/registry，也没有“让外部模块基于本轮 UHT 结果再追加 artifact”的正式入口。用户若想在这条生成链上挂自己的 pass，当前基本只能修改 `AngelscriptFunctionTableExporter.cs` / `AngelscriptFunctionTableCodeGenerator.cs` 本体。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:21`-`:27` 把 exporter 固定注册为 `Name = "AngelscriptFunctionTable"`、`ModuleName = "AngelscriptRuntime"`；`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:34`-`:44` 的主流程中直接调用 `AngelscriptFunctionTableCodeGenerator.Generate(factory)` 并写 summary/skipped diagnostics；`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:51`-`:79` 的 `Generate()` 固定遍历 `factory.Session.Modules`；`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:334`-`:384` 的 `LoadSupportedModules()` 直接解析 `AngelscriptRuntime.Build.cs` 得出支持模块；`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:490`-`:515` 的 `ShouldGenerate()` 内建 metadata / header 过滤；`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:282`-`:324` 的 `BuildShard()` 则把产物硬编码成 `AS_FORCE_LINK const FAngelscriptBinds::FBind ... AddFunctionEntry(...)`。 |
| 优点 | 生成过程确定性强，容易定位问题；只维护一条 exporter 链，构建复杂度较低。 |
| 不足 | 这让“为插件生态追加自定义 codegen artifact”变成 fork-only 能力。即便用户不想改现有 function-table 逻辑，只想基于同一轮 UHT 结果生成额外的声明、诊断或 IDE 辅助数据，也没有正式插槽。对于成熟插件交付来说，这会直接抬高扩展包与周边工具链的接入成本。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 把一类 codegen 行为做成正式 interface：`UCodeGenerator` / `ICodeGenerator` 声明了 `Gen()`，`DeclarationGenerator` 在 `GenTypeScriptCppDeclaration()` 里扫描所有实现该接口的类并执行 `ICodeGenerator::Execute_Gen(...)`。也就是说，扩展包可以通过新增 class 介入生成，而不是修改 declaration generator 本体。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Public/CodeGenerator.h:10`-`:26`；`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1708`-`:1717` | 生成期 pass 不一定都要硬编码在核心工具里；可以先开放一个最小 interface，让扩展方追加 side artifact。 |
| UnrealCSharp | 生成主流程前后有正式 phase hook：`FUnrealCSharpCoreModuleDelegates` 提供 `OnBeginGenerator` / `OnEndGenerator`，`FUnrealCSharpEditorModule::Generator()` 在完整生成链前后分别广播这两个事件。即使外部模块不接管核心 generator，也能围绕同一轮生成做协同工作。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Delegate/FUnrealCSharpCoreModuleDelegates.h:7`-`:30`；`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:237`-`:309` | 除了“可替换整个 generator”，还可以先补 phase hook，让外部模块围绕生成过程做 observe/augment。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先不重写 `AngelscriptUHTTool` 的核心 exporter，而是把当前 closed-world 生成流程拆成“固定 core exporter + 可扩展 post-pass lane”，给扩展包一个不必 fork UHT tool 的增量入口。 |
| 具体步骤 | 1. 在 `AngelscriptFunctionTableCodeGenerator.Generate()` 完成后，新增结构化 `AngelscriptCodeGenManifest.json`（可复用后续 binding manifest 思路），至少记录 `GeneratedFiles`、`Modules`、`Entries`、`SkippedReasonSummary`、`OutputDirectory`、`GeneratorVersion`。这一步只新增产物，不改变现有 shard 与 csv/json。 2. 在 `AngelscriptEditor` 或独立 tooling 模块引入 `IAngelscriptCodeGenContributor` / `UAngelscriptCodeGenContributor` 这类 provider contract，由扩展模块实现“读取 manifest 并生成额外 artifact/诊断”的后处理逻辑。 3. 给现有构建工具链补一个 post-UHT 入口，例如新增 `AngelscriptCodeGenPostProcessCommandlet` 或在 `RunBuild.ps1` 的生成阶段后调用统一 post-process runner；第一阶段只允许 observe/emit side artifact，不允许改写 `AS_FunctionTable_*.cpp`。 4. 在 editor/tooling 层增加 begin/end hook，供扩展包感知“本轮 codegen 开始/结束”；等 post-pass 方案稳定后，再评估是否需要把部分 seam 前移到 `AngelscriptUHTTool` 内部。 5. 增加一条扩展示例：外部模块读取 manifest 生成额外 `json/csv` 声明或 IDE index，验证无需改 `AngelscriptUHTTool` 源码即可接入。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Tools/RunBuild.ps1`、`Tools/Shared/UnrealCommandUtils.ps1`、新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/Extension/AngelscriptCodeGenContributor.h`、新增 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Commandlets/AngelscriptCodeGenPostProcessCommandlet.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptCodeGenContributorTests.cpp` |
| 预估工作量 | M-L |
| 架构风险 | 核心风险是 post-pass 看到的 manifest 与实际 UHT 输出不一致，或 post-process 介入后破坏构建确定性。第一阶段必须限定为 append-only side artifact，并为 manifest 增加版本号/内容 hash。 |
| 兼容性 | 向后兼容。现有 `AS_FunctionTable_*` 文件、UHT exporter 名称和现有构建链都保持不变；新增的 manifest 与 post-pass runner 默认可关闭，不影响旧项目。 |
| 验证方式 | 1. 新建一个外部 contributor 示例，只消费 manifest 生成额外 `json/csv`，验证无需修改 `AngelscriptUHTTool` 本体。 2. 回归现有 generated function table tests，确认新增 manifest/post-pass 后原有 shard、summary、skipped diagnostics 不变。 3. 在增量构建和全量构建下分别验证 output hash 稳定，确保 post-pass 不引入非确定性产物。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-EP49 | 自定义绑定的 `StaticJIT` lowering 仍依赖 `GetPreviousBind()` side effect | native-call contract 正式化 | 高 |
| P2 | Arch-EP50 | `UHT/codegen` 没有正式的 pre/post pass seam，扩展包无法附加自定义生成期产物 | toolchain extension lane 新增 | 中高 |

---

## 架构分析 (2026-04-09 00:32)

### Arch-EP51：扩展配置的持久化范围仍被压平在 `DefaultEngine.ini`，缺少团队共享与本地偏好分层

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `ini/settings` 驱动的扩展与调试行为，是否按“项目共享 / 编辑器共享 / 本地用户偏好”分层 |
| 当前设计 | 正式插件扩展相关配置目前几乎都挂在 `UAngelscriptSettings` 这一份 `Config=Engine` 对象上；它同时承载编译器行为、运行时兼容策略、编辑器超时保护和 debugger 自动求值 blacklist。相对地，测试链路已经证明仓内能按作用域拆出 `Editor` 与 `EditorPerProjectUserSettings` 两层配置，但这套分层尚未用于正式扩展面。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h:41`-`:42` 定义 `UAngelscriptSettings` 为 `UCLASS(Config=Engine, DefaultConfig)`；`:137`-`:138` 的 `EditorMaximumScriptExecutionTime` 与 `:206`-`:219` 的 debugger blacklist 仍保存在同一对象里；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:384`-`:393` 也只注册这一份 settings 页面；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:668`-`:688` 在 debugger 自动求值路径直接读取 `DebuggerBlacklistAutomaticFunctionEvaluation`；另一方面，`Plugins/Angelscript/Source/AngelscriptRuntime/Testing/AngelscriptTestSettings.h:8`-`:30` 已有 `UAngelscriptTestUserSettings(config = EditorPerProjectUserSettings)`，`:32`-`:65` 又有 `UAngelscriptTestSettings(config = Editor)`，并且 `Plugins/Angelscript/Source/AngelscriptRuntime/Testing/UnitTest.cpp:552`-`:554`、`:652`-`:654` 明确按 user settings 控制 hot-reload 后执行哪些测试。 |
| 优点 | 所有核心开关集中在一个对象里，查找和序列化成本低；对只关心编译器/运行时语义的项目来说也足够直接。 |
| 不足 | 扩展与工具链相关的偏好没有作用域隔离。一个开发者为了本地调试修改 blacklist、编辑器执行超时或未来的 editor 扩展设置，天然会落到团队共享的 `Engine` 配置；同时正式扩展 manifest 也没有位置区分“应随项目提交”与“只在本机 editor 生效”。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | 运行时与 editor 配置分为两份 settings：`UUnrealCSharpSetting` 注册到 `Project/Plugins`，`UUnrealCSharpEditorSetting` 单独注册到 `Editor/Plugins`，两者各自承载不同责任。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpSetting.h:77`-`:84`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Setting/UnrealCSharpSetting.cpp:26`-`:37`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpEditorSetting.h:32`-`:44`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Setting/UnrealCSharpEditorSetting.cpp:107`-`:114` | 先把“项目配置”和“编辑器配置”拆开，再谈更细的扩展 manifest，能明显降低 settings 污染面。 |
| UnLua | runtime 与 editor 也是分离的：`UUnLuaSettings` 放 runtime seam（`EnvLocatorClass`、`ModuleLocatorClass`、`PreBindClasses`），`UUnLuaEditorSettings` 单独承载热重载、intellisense、build 选项；两边模块注册设置页后还各自绑定 `OnModified()`。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaSettings.h:23`-`:24`、`:47`-`:56`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaModule.cpp:245`-`:249`、`:272`-`:276`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorSettings.h:39`-`:67` | 不同作用域的可定制能力应该落在不同 settings carrier 上，否则 runtime 和 editor 偏好会互相污染。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 保留现有 `UAngelscriptSettings` 作为“项目共享的编译器/运行时语义”载体，再补两层 settings：`Editor` 级项目共享配置与 `EditorPerProjectUserSettings` 级本地偏好配置。 |
| 具体步骤 | 1. 新增 `UAngelscriptEditorSettings`（建议 `Config=Editor, DefaultConfig`），专门承载 editor 工作流但应随项目共享的设置，例如蓝图创建默认路径、editor 扩展 provider manifest、项目级调试可视化策略。 2. 新增 `UAngelscriptEditorUserSettings`（建议 `Config=EditorPerProjectUserSettings`），承载本机偏好，例如 debugger 自动求值 blacklist override、本地热重载观察器开关、只影响个人工作流的菜单/弹窗默认值。 3. 第一阶段不要立刻删除 `UAngelscriptSettings` 里的旧字段，而是在读取路径中按 `UserSettings > EditorSettings > Legacy EngineSettings` 合并，确保旧项目不需要立刻迁移。 4. 在 `AngelscriptEditorModule` 中分别注册这三类 settings，并为 editor/user 两类 settings 补 `OnModified` 保存期校验；把今天直接读 `UAngelscriptSettings` 的 editor/tooling 路径逐步改为读 merged effective settings。 5. 等新设置跑稳后，再把明显属于本地偏好的 legacy 字段标记为 deprecated alias，仅保留向后兼容读取。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/AngelscriptEditorSettings.h/.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/AngelscriptEditorUserSettings.h/.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEffectiveSettings.h/.cpp` |
| 预估工作量 | M |
| 架构风险 | 最大风险是字段迁移后出现“同名配置在三个层级下冲突”的行为变化，尤其是 debugger blacklist 这类集合型设置。第一阶段必须采用合并与 alias 读取，而不是直接搬家。 |
| 兼容性 | 向后兼容。`DefaultEngine.ini` 里的旧字段继续可读；新 settings 只是补充更精细的持久化范围，不改变旧项目默认行为。 |
| 验证方式 | 1. 增加 editor automation：分别在 `Engine`、`Editor`、`EditorPerProjectUserSettings` 中写入同一项调试配置，验证 effective settings 的覆盖顺序稳定。 2. 回归 debugger 自动求值相关测试，确认 legacy blacklist 仍有效。 3. 新增一条本地偏好测试或手工验证：修改 user settings 后不再污染团队共享的 `DefaultEngine.ini`。 |

### Arch-EP52：插件已经有成熟 `StateDump` 管线，但没有导出“插件自身扩展面”审计工件

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 委托、settings、`virtual` 扩展点是否能像 bind/type/runtime state 一样被导出为可审计 artifact，而不是只能靠 `rg` 搜源码 |
| 当前设计 | 当前 `StateDump` 已经可以稳定导出 script/runtime/JIT/debugger 大量状态，但“插件本身有哪些扩展 hook”仍不在导出范围内。`DumpDelegates()` 导出的是脚本模块里的 delegate 定义，不是 `FAngelscriptRuntimeModule` / `FAngelscriptClassGenerator` 这类插件生命周期 hook；`DumpEngineSettings()` 只枚举 `UAngelscriptSettings` 字段；`OnDumpExtensions` 目前也只约定第一方 editor 额外表。结果是，像本轮这种 ExtensionPoints 审查，仍必须直接 grep 源码去拼出委托、settings、`virtual` seam 全貌。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.h:18`-`:23` 暴露了 `DumpAll()` 与 `OnDumpExtensions`；`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp:159`-`:184` 列出当前核心 dump 表，但没有任何 `ExtensionHooks/ExtensionSettings/ExtensionProviders` 表；`:221`-`:222` 仅把 extension tables 固定为 `EditorReloadState.csv` 与 `EditorMenuExtensions.csv`；`:662`-`:691` 的 `DumpDelegates()` 明确遍历 `Module->Delegates`，导出的是脚本 delegate，而不是 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h:9`-`:21` 或 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h:12`-`:38` 里的 native extensibility hook；`:952`-`:978` 的 `DumpEngineSettings()` 也只迭代 `UAngelscriptSettings::StaticClass()` 属性。 |
| 优点 | 现有 dump 仍保持“纯外部观察者”原则，没有为了做审计去侵入 runtime/editor 主流程；表结构也已经足够成熟，新增 CSV 的工程成本不高。 |
| 不足 | 扩展者看不到“当前版本正式支持哪些 hook、这些 hook 属于 runtime 还是 editor、是 single-cast 还是 multicast、是否能多插件组合、是否只在 C++ 可用”。这直接抬高 discoverability 成本，也让扩展点回归缺少机器可比对的事实来源。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | 把核心扩展事件集中在专门的 public delegate 头里，运行时与 editor settings 也各有独立 public header；可推断其扩展 contract 的审计入口天然更集中。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Delegate/FUnrealCSharpCoreModuleDelegates.h:7`-`:37`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpSetting.h:77`-`:84`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpEditorSetting.h:32`-`:44` | 即使暂时不做自动导出，也应先把扩展 contract 收敛为少数可审计入口；在此之前，生成审计表能补 discoverability 缺口。 |
| UnLua | `FUnLuaDelegates`、`UUnLuaSettings`、`ULuaEnvLocator` / `ULuaModuleLocator` 这些 public contract 文件把“事件、配置、策略类”明确分开放置；可推断扩展面更容易被工具或文档直接索引。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDelegates.h:22`-`:50`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaSettings.h:23`-`:24`、`:47`-`:56` | 当扩展面还没有完全 API 收口时，至少要给审查和使用者一个稳定的索引视图。 |
| puerts | `IPuertsModule` 与 `IJSModuleLoader` 把模块 hook 和加载策略收敛到两个 public interface；扩展者不需要遍历整仓去猜哪些 `OnXxx` 才是正式入口。 | `Reference/puerts/unreal/Puerts/Source/Puerts/Public/PuertsModule.h:17`-`:55`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSModuleLoader.h:17`-`:50` | 若当前 Angelscript 还处于“多种 seam 并存”的阶段，增加 audit artifact 是低风险补偿手段。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在不改变现有扩展语义的前提下，把“扩展点清单”纳入 `StateDump`/审计工件，先让扩展面可见、可比对、可回归，再逐步收口 public API。 |
| 具体步骤 | 1. 在 `Dump/` 新增 `DumpExtensionHooks()`、`DumpExtensionSettings()`、`DumpExtensionProviders()` 三类表，最小列建议包含 `HookKind`、`Name`、`Owner`、`PublicHeader`、`Scope`、`Composable`、`EditorOnly`、`Notes`。 2. 第一阶段不要做全仓 AST 解析，而是基于已有 registry 与人工声明的轻量 descriptor：runtime module delegates、class generator reload events、settings classes/fields、已批准的 strategy/virtual base class 各自登记到统一描述表。 3. 扩展 `OnDumpExtensions`，允许 editor 模块和未来外部扩展模块继续追加自己的 extension-audit 表，而不是只固定 `EditorReloadState.csv` / `EditorMenuExtensions.csv`。 4. 给 `AngelscriptTest` 新增快照测试，验证关键 hook 例如 `GetOnInitialCompileFinished`、`OnPostReload`、`DisabledBindNames`、`FAngelscriptAdditionalCompileChecks` 在审计表中可见，避免后续重构把 public seam 静默删掉。 5. 等 future public contract 收口后，再把这套 audit descriptor 作为文档生成和 settings 候选项的数据源，减少重复维护。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/AngelscriptExtensionAudit.h/.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptTest/Dump/AngelscriptExtensionAuditDumpTests.cpp` |
| 预估工作量 | M |
| 架构风险 | descriptor 如果全靠人工维护，最容易发生“代码变了，审计表没更新”的漂移。第一阶段应优先从已有 runtime registry 和 settings 反射拿数据，把人工维护范围限制在少量 `virtual` provider 类。 |
| 兼容性 | 完全向后兼容。新增的只是 dump 表与审计元数据，不改变现有扩展注册、settings 读取或事件广播行为。 |
| 验证方式 | 1. 运行 `as.DumpEngineState`，确认新增 `ExtensionHooks.csv` / `ExtensionSettings.csv` / `ExtensionProviders.csv` 被写出并进入 `DumpSummary.csv`。 2. 增加自动化测试，对若干代表性 seam 做行级断言，避免回归。 3. 人工对照一次本轮 `rg` 结果，确认新增审计表已覆盖至少 runtime delegates、reload events、settings 字段与 approved provider 基类四类信息。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-EP51 | 扩展配置缺少“项目共享 / editor 共享 / 本地偏好”分层 | settings 作用域重构 | 高 |
| P2 | Arch-EP52 | `StateDump` 尚未导出插件自身扩展面，扩展审计仍依赖源码搜索 | 审计工件新增 | 中高 |

---

## 架构分析 (2026-04-09 00:42)

### Arch-EP53：strategy hook 仍以“返回值 / 可变引用”暴露，缺少显式 request context

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `OnXxx` / single-cast strategy hook 是否把调用上下文显式传给扩展者，还是要求扩展者回读 ambient engine/world 状态 |
| 当前设计 | 当前扩展面里最关键的一批 strategy hook 仍是窄签名：`GetDynamicSpawnLevel()` 完全无入参，`GetClassAnalyze()` 直接让外部改 `GeneratedStatics` 字符串和 `bool&`，不同 hook 的上下文携带量并不一致。调用点已经拿到了 `World`、`WorldContext`、`ClassDesc` 等信息，但 public contract 没把它们组织成稳定 request object。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h:9`-`:14`、`:21` 定义了 `GetDynamicSpawnLevel`、`ClassAnalyze` 等单播 delegate；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp:183`-`:196` 与 `:215`-`:223` 在 `SpawnActor` 路径里已经构造出 `World`、`WorldContext`、`Params`，但最终只对 `GetDynamicSpawnLevel().Execute()` 传一个“无参取 `ULevel*`”请求；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:1310`-`:1311` 让 `GetClassAnalyze()` 直接改 `GeneratedStatics` / `bHasStatics`，却没有把 `Module`、`Engine`、预处理状态打包给扩展者；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:718`-`:733` 显示很多扩展最终只能再回读 `TryGetCurrentEngine()` / subsystem ambient state。作为对照，`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:658`-`:661` 的 `GetDebugCheckBreakOptions()` 又显式传入了 `BreakOptions` 和 `WorldContext`，说明当前 surface 在“是否给上下文”这件事上并不一致。 |
| 优点 | 签名短、旧扩展接入成本低；对早期只需要一个 override 点的场景，实现非常直接。 |
| 不足 | 一旦扩展要做 context-sensitive 决策，就容易退回 ambient global state：spawn policy 看不到 `ActorClass`/`SpawnParams`，class analyze 看不到 `ModuleName`/`EngineInstance`/compile type。这样既限制扩展能力，也让多引擎、PIE、tool world 或后续 typed provider 演进更脆弱。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | module/env 选路与 loader hook 都带显式调用上下文。`ULuaEnvLocator::Locate(const UObject* Object)`、`ULuaModuleLocator::Locate(const UObject* Object)` 直接拿宿主对象做决策；legacy `CustomLoadLuaFile` 也会收到 `FLuaEnv&`、`FileName`、`Data`、`ChunkName`。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/LuaEnvLocator.h:22`-`:33`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/LuaModuleLocator.h:20`-`:26`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDelegates.h:33`-`:35`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:557`-`:566` | strategy hook 的核心不是“能不能 override”，而是签名本身要把决策所需上下文带齐。 |
| puerts | 模块加载与 env 选路都走显式 request。`IJSModuleLoader::Search()`/`Load()` 直接拿 `RequiredDir`、`RequiredModule`、`Path`；`IPuertsModule::SetJsEnvSelector()` 把 `UObject*` 和 env index 作为 selector 输入。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSModuleLoader.h:17`-`:24`；`Reference/puerts/unreal/Puerts/Source/Puerts/Public/PuertsModule.h:41`-`:45`；`Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:112`-`:120` | request-driven API 比“无参 callback + ambient lookup”更适合多实例和工具化场景。 |
| UnrealCSharp | 即使是事件型 contract，也倾向于把关键输入显式化，例如 `OnCompile` 明确附带 `TArray<FFileChangeData>`；provider 类 `UAssemblyLoader::Load(const FString& InAssemblyName)` 也要求调用方显式给出请求目标。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Delegate/FUnrealCSharpCoreModuleDelegates.h:18`-`:21`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Domain/AssemblyLoader.h:12`-`:18` | 无论 observer 还是 strategy，public contract 都应先定义输入对象，而不是让扩展者自己去猜上下文。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 保留 legacy delegate 不动，但在其上补一层 typed request/result contract，把关键 strategy seam 从“ambient state + mutable refs”升级为“显式输入 + 显式结果”。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/` 新增 `FAngelscriptSpawnLevelRequest`、`FAngelscriptClassAnalyzeRequest`、`FAngelscriptClassAnalyzeResult` 等结构，至少包含 `WorldContext`、`World`、`ActorClass`、`SpawnParams`、`EngineInstanceId`、`ModuleName`、`GeneratedStatics`、`bHasStatics`。 2. 为 `GetDynamicSpawnLevel()`、`GetClassAnalyze()` 增加新的 provider API，例如 `IAngelscriptSpawnLevelResolver::Resolve(const FAngelscriptSpawnLevelRequest&)`、`IAngelscriptClassAnalyzeProvider::Analyze(const FAngelscriptClassAnalyzeRequest&, FAngelscriptClassAnalyzeResult&)`。 3. 在 `Bind_AActor.cpp` 与 `AngelscriptPreprocessor.cpp` 中优先走 typed provider；若未配置 provider，则用 legacy delegate adapter 把旧返回值/`FString&` 路径桥接到新结果对象。 4. 对 `GetDebugCheckBreakOptions()` 这类已经有部分上下文的 hook，不需要立刻重写语义，只需统一迁移到 typed request struct，避免 surface 继续风格分裂。 5. 在 `StateDump` 或扩展审计表中输出每个 strategy seam 的 request/result shape 和当前 provider，减少扩展者回读 ambient state 的必要性。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/AngelscriptStrategyRequests.h`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/AngelscriptStrategyProviders.h` |
| 预估工作量 | M |
| 架构风险 | 最大风险是 request/result 设计不当后，反而把旧 hook 的隐式语义固定错了。第一阶段应只覆盖 1-2 个高价值 strategy seam，并保留 legacy adapter 做行为对照。 |
| 兼容性 | 向后兼容。现有 `GetDynamicSpawnLevel()` / `GetClassAnalyze()` 仍可继续绑定；新 provider 只是更正式的入口，旧 delegate 通过 adapter 映射到新结果对象。 |
| 验证方式 | 1. 新增一个宿主测试模块，让 spawn policy 依据 `ActorClass` 或 `WorldType` 选择不同 level，验证 typed request 能拿到 legacy hook 拿不到的上下文。 2. 增加 class analyze 测试，验证 provider 能看到 `ModuleName`/`EngineInstanceId`，同时旧 delegate 适配后生成结果不变。 3. 复跑现有 `SpawnActor`、预处理与 debug 相关测试，确认未使用新 provider 时行为与当前版本一致。 |

### Arch-EP54：`FAngelscriptEngineDependencies` 仍是 test-friendly lambda bag，尚未成为可演进的 host service contract

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `FAngelscriptEngineDependencies` / `FAngelscriptEngineConfig` 这一层是否已经是正式宿主扩展 API，还是主要服务测试注入与少量固定文件系统 seam |
| 当前设计 | 当前正式 public seam 里，`FAngelscriptEngineDependencies` 仍只是五个 `TFunction` 字段的平面 struct；production 默认走 `CreateDefault()` 填 lambda，测试则手工逐字段赋值。它很适合隔离 `ProjectDir` / `DirectoryExists` / plugin script roots，但这套 shape 很难继续演进成“带身份、可配置、可校验”的宿主服务对象。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:86`-`:95` 将 dependencies 固定为 `GetProjectDir`、`ConvertRelativePathToFull`、`DirectoryExists`、`MakeDirectory`、`GetEnabledPluginScriptRoots` 五个 `TFunction`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:539`-`:567` 的 `CreateDefault()` 直接把它们绑定到 `FPaths`、`IFileManager`、`IPluginManager` lambda；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1328`-`:1355` 的 `DiscoverScriptRoots()` 又 `check()` 这五个回调并直接消费；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDependencyInjectionTests.cpp:51`-`:80` 则展示了测试路径需要手工逐个字段填充 fake lambda。可以推断，这条 seam 当前主要优化的是 isolated testability，而不是长期稳定的 host extension contract。 |
| 优点 | 非常轻量，测试替身写起来直接；不需要 `UObject`、module registry 或 settings，适合快速验证脚本根发现逻辑。 |
| 不足 | 一旦宿主定制需求超出这五个字段，就只能继续往 struct 里追加 lambda，或者在别处再开新的全局 seam。它没有 provider identity、没有 settings 对应项、没有 editor 校验，也无法表达“同一宿主服务对象还负责 module resolution / path policy / logging / diagnostics”。从扩展 API 演进角度看，这是一个 closed-world bag，而不是 versionable contract。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 把宿主相关路由做成可实例化的 `UObject` provider，并把 provider class 放进 settings。`EnvLocatorClass` / `ModuleLocatorClass` 是配置项，模块启动与 `FLuaEnv` 初始化时按配置实例化 locator。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaSettings.h:47`-`:56`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaModule.cpp:103`-`:106`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:77`-`:79` | host seam 一旦有了 class identity，就能进入 settings、runtime 初始化和 editor 校验链。 |
| puerts | `FJsEnv` 构造函数直接接收 `std::shared_ptr<IJSModuleLoader>`；`IJSModuleLoader` 再通过 `Search()` / `Load()` / `GetScriptRoot()` 提供多态能力。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JsEnv.h:64`-`:68`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnv.cpp:19`-`:24`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSModuleLoader.h:17`-`:26` | 与其持续给 POD struct 加字段，不如把 host 交互收口到少数 versionable interface。 |
| UnrealCSharp | assembly loading 也走 provider class：`UAssemblyLoader` 是显式 `UObject` seam，settings 直接保存 `TSubclassOf<UAssemblyLoader>`，运行时统一通过 helper 获取当前 loader。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Domain/AssemblyLoader.h:12`-`:18`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpSetting.h:140`-`:142`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp:1106`-`:1110` | 即使最终功能很小，也值得先给它一个可配置、可替换、可验证的 provider 身份。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 保留 `FAngelscriptEngineDependencies` 作为测试和底层构造 adapter，但在 production contract 上补一层更窄、更可演进的 host services/provider object。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/` 新增 `IAngelscriptHostServices`，第一阶段至少拆成 `GetProjectScriptRoot()`、`EnumeratePluginScriptRoots()`、`EnsureProjectScriptRootExists()`、`NormalizePath()` 四类职责；若不想一步做大，也可先拆成 `IAngelscriptScriptRootLocator` + `IAngelscriptFileSystem` 两个 interface。 2. 新增默认实现 `FAngelscriptDefaultHostServices` 或 `UAngelscriptDefaultHostServices`，内部复用今天 `CreateDefault()` 的 `FPaths` / `IFileManager` / `IPluginManager` 逻辑。 3. 让 `FAngelscriptEngineConfig` 或新的 runtime extension settings 支持声明 `HostServicesClass`/factory；`FAngelscriptEngine` 初始化时优先实例化 host services，再由 adapter 生成 legacy `FAngelscriptEngineDependencies` 给旧代码使用。 4. 把 `DiscoverScriptRoots()` 逐步改为依赖新 host services，而不是直接 `check()` 五个 lambda；legacy tests 可继续直接构造 `FAngelscriptEngineDependencies`，由 adapter 包装成临时 host services。 5. 在 editor 侧增加 provider 校验和 dump 输出，明确当前 engine 用的是默认 host services 还是项目自定义实现。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDependencyInjectionTests.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/AngelscriptHostServices.h`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Private/Extension/AngelscriptDefaultHostServices.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptLegacyDependencyAdapter.h/.cpp` |
| 预估工作量 | M |
| 架构风险 | 若一步把所有调用点都切到新 interface，容易影响现有测试与多引擎构造路径。第一阶段应采用 adapter 过渡，让旧 `FAngelscriptEngineDependencies` 继续可用。 |
| 兼容性 | 向后兼容。现有 `FAngelscriptEngine(Config, Dependencies)`、`CreateForTesting()`、依赖注入测试都可以继续保留；新增的 host services 只是 production 推荐路径。 |
| 验证方式 | 1. 复跑现有 `AngelscriptDependencyInjectionTests`，确认 legacy struct adapter 下结果完全一致。 2. 新增一条正式 runtime 路径测试，通过 settings/provider class 指定自定义 host services，验证无需改插件源码即可改变 script root 发现。 3. 运行 `StateDump` 或新增扩展审计表，确认当前 engine 使用的 host services class / source module 可见。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-EP53 | strategy hook 缺少显式 request context，扩展决策依赖 ambient state | request/result contract 新增 | 高 |
| P1 | Arch-EP54 | `EngineDependencies` 仍是 test seam，未形成可演进的 host service API | host contract 分层 | 高 |

---

## 架构分析 (2026-04-09 00:53)

### Arch-EP55：扩展事件仍是 edge-triggered signal，late subscriber 需要自己拼“查状态 + 订阅”补偿逻辑

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 编译完成 / reload 这类生命周期 hook 是否自带正式的 state query / replay contract，还是要求扩展者自己补偿时序 |
| 当前设计 | 当前 public 生命周期面仍以 edge-triggered delegate 为主。`GetOnInitialCompileFinished()` 只是一个 multicast delegate 引用，`PostInitialize_GameThread()` 里直接 `Broadcast()`；运行时虽然另外保留了 `bIsInitialCompileFinished` / `bDidInitialCompileSucceed` 状态位，但这套状态没有被整理成正式 extensibility contract，导致 late subscriber 必须自己写“如果已经完成就立即执行，否则先订阅”的补偿代码。更进一步，`FAngelscriptClassGenerator::OnPostReload / OnFullReload / OnClassReload` 只暴露事件，没有任何 `LastReloadRevision / LastReloadWasFull / CurrentReloadState` 等查询接口。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h:37`-`:47` 只公开 `GetPreCompile()` / `GetPostCompile()` / `GetOnInitialCompileFinished()` 等 delegate accessor；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp:84`-`:87` 返回的只是 static delegate 实例；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1653`-`:1655` 在 `PostInitialize_GameThread()` 中一次性 `Broadcast()` 初始编译完成事件；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:481`-`:488` 另外维护 `bDidInitialCompileSucceed`、`bIsInitialCompileFinished` 和 `IsInitialCompileFinished()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Console.h:18`-`:31` 必须手工先查 `IsInitialCompileFinished()`，否则再订阅 `GetOnInitialCompileFinished()`；`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:25`-`:45` 同样要手工把 `OnPostReload` 订阅与“当前是否已经完成初编译”判断拼在一起；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h:12`-`:38` 只暴露 reload 事件，没有配套状态查询对象。 |
| 优点 | 事件模型直接、实现成本低，不需要额外 snapshot/state object，也不会强行把所有扩展者都拉进更重的生命周期管理框架。 |
| 不足 | 时序补偿逻辑已经泄漏到第一方消费者里，说明 contract 本身不完整。外部扩展模块如果晚于初编译加载，或者在 Live Coding / module reload 后重新挂接，就必须重复写“查状态 + 补订阅”的样板。更严重的是，reload 只有 post-event 没有可查询快照，late subscriber 无法正式知道自己错过了哪一次 full reload；这一点会直接影响“订阅编译/热重载事件”的可靠性。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | 把核心状态转换放到显式 module owner 上：`SetActive(bool)` 负责 active/inactive 切换，并对称广播 `OnUnrealCSharpCoreModuleActive` / `OnUnrealCSharpCoreModuleInActive`；同时像 `AssemblyLoader` 这类扩展能力走 request-driven service，调用方按需查询，不依赖一次性事件是否刚好命中。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/UnrealCSharpCore.h:17`-`:23`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/UnrealCSharpCore.cpp:19`-`:32`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Domain/AssemblyLoader.h:12`-`:18`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp:1106`-`:1110`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp:500`-`:503` | 对“准备完成/可用状态”这种生命周期信号，事件之外还需要正式 owner 或 service query，避免 late subscriber 自己拼补偿逻辑。 |
| UnLua | 关键扩展 seam 更偏 request-driven provider：`ModuleLocatorClass` / `EnvLocatorClass` 由 settings 选定，`FLuaEnv` 初始化时实例化 locator，后续通过 `Locate(const UObject*)` 按需求值对象到脚本环境/模块的映射；扩展者不需要押注某个过去时刻的事件是否已经广播。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaSettings.h:47`-`:56`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/LuaEnvLocator.h:22`-`:34`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/LuaModuleLocator.h:20`-`:26`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:74`-`:81` | 对需要晚接入、可重复调用的扩展场景，provider 查询比 one-shot event 更稳健。 |
| puerts | `IPuertsModule` 直接暴露 `ReloadModule()`、`InitExtensionMethodsMap()`、`SetJsEnvSelector()` 这类可显式再次调用的动作；`IJSModuleLoader` 也是 request-driven interface，扩展者在需要时调用/实现它，而不是等待某个过去的初始化事件。 | `Reference/puerts/unreal/Puerts/Source/Puerts/Public/PuertsModule.h:17`-`:49`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSModuleLoader.h:17`-`:25`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JsEnv.h:64`-`:70` | 生命周期里凡是需要 late-join 或 replay 的能力，更适合显式 action/state API，而不是只给 edge-triggered signal。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留现有 delegate 的前提下，补一层正式的 runtime state + replay helper，把“查状态 + 订阅”从扩展者样板代码收回到插件公共 contract。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/` 新增 `FAngelscriptLifecycleState` 或 `FAngelscriptExtensionRuntimeState`，至少公开 `bIsInitialized`、`bInitialCompileFinished`、`bInitialCompileSucceeded`、`LastReloadSerial`、`bLastReloadWasFull`、`LastReloadCompletedAt`。 2. 在 `FAngelscriptEngine` 与 `FAngelscriptClassGenerator` 的现有广播点同步更新该状态对象，例如 `PostInitialize_GameThread()`、`InitialCompile()` 收尾、`OnPostReload.Broadcast()` 前后。 3. 给外部扩展者新增 helper API，例如 `RunWhenInitialCompileFinished(FAngelscriptEngine&, TFunctionRef<void(const FAngelscriptLifecycleState&)>)`、`SubscribePostReload(EReplayPolicy, ...)`；默认 `ReplayIfAlreadySatisfied`，从而把 `Bind_Console` / `ScriptEditorMenuExtension` 里的补偿样板收回到公共层。 4. 对 reload 相关扩展点增加最小快照查询，例如 `GetLastReloadSnapshot()`，第一阶段不追求完整 diff，只先暴露 `Serial`、`bWasFullReload`、`AffectedKinds(Class/Struct/Enum/Delegate)`。 5. 在扩展审计表或 `StateDump` 中追加 `LifecycleState.csv` / `LifecycleHooks.csv`，让项目侧能直接看到当前 engine 的 readiness/reload 状态，而不是只看过去日志。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Console.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/AngelscriptLifecycleState.h/.cpp` |
| 预估工作量 | M |
| 架构风险 | 最大风险是把今天隐式存在的时序语义“冻结”错了，例如 full reload 是否应覆盖旧 snapshot、soft reload 是否应增加 serial。第一阶段应只给最少状态字段和 replay helper，不急于一次性把所有 reload 细节都 formalize。 |
| 兼容性 | 向后兼容。现有 `GetOnInitialCompileFinished()` / `OnPostReload` / `OnFullReload` 保留；新 state/helper 只是把 late-subscriber 补偿逻辑收口为正式入口。 |
| 验证方式 | 1. 新增一个宿主测试模块，分别在“初编译前加载”和“初编译后加载”两种时机调用 `RunWhenInitialCompileFinished()`，验证都只执行一次且 payload 一致。 2. 增加 reload 回归，验证 full/soft reload 后 `LastReloadSerial` 和 `bLastReloadWasFull` 正确推进。 3. 把 `Bind_Console` 与 `UScriptEditorMenuExtension` 迁移到新 helper 后复跑相关测试，确认不再需要手写补偿逻辑且行为不变。 |

### Arch-EP56：public hook 同时暴露订阅权和发布权，扩展 API 缺少 authority boundary

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | public extensibility hook 是否只允许外部扩展“订阅/实现”，还是同时把 `Broadcast/Clear/覆盖绑定` 权限也暴露给了外部模块 |
| 当前设计 | 当前 runtime/class-generator 扩展面把 delegate 实例本体直接暴露给外部。`FAngelscriptRuntimeModule` 的 `GetPreCompile()` / `GetPostCompile()` / `GetOnInitialCompileFinished()` / `GetClassAnalyze()` 都返回非 `const` delegate 引用；`FAngelscriptClassGenerator::OnPostReload / OnFullReload / OnClassReload` 则是 public static delegate 字段。内部调用点当然只是在正确时机 `Broadcast()`，但从 API 形状看，可以推断任何包含这些 public header 的外部模块都拥有与发布者同等级的 delegate 变异权限，而不是只有订阅权。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h:9`-`:21` 定义 raw delegate 类型，`:32`-`:47` 直接把它们以 `GetXxx() -> Delegate&` 形式公开；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp:42`-`:135` 每个 accessor 都返回 static delegate 实例本体；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1653`-`:1655` 通过同一个 public 对象执行 `GetOnInitialCompileFinished().Broadcast()`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h:12`-`:38` 把 `OnClassReload`、`OnFullReload`、`OnPostReload` 等 delegate 直接做成 public static 字段；作为对照，第一方消费者的实际使用方式只是订阅：`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Console.h:24`-`:29` 只做 `AddLambda/Remove`，`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:27`-`:39` 也只是监听 `OnPostReload` 和 `OnEnginePreExit`。基于这些公开类型与用法，可以推断当前 contract 没有把“谁能发布、谁只能订阅”做成 API 级边界。 |
| 优点 | 访问成本极低，内部模块和宿主模块都能直接 `AddLambda()`，不需要再包一层 façade 或订阅器对象。 |
| 不足 | authority boundary 缺失会让 observer hook 和 strategy hook 一样都暴露为“可任意改写的对象”。这不仅增加误用面，也使后续很难在 API 层表达“这个点是 observe-only、那个点是 single active policy”。随着外部扩展模块增多，这类“默认你不会乱调 `Broadcast/Clear`”的约定会变成真实的兼容性负担。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 关键扩展能力主要走 deliberate interface：模块级动作收口在 `IPuertsModule` 的 `ReloadModule()`、`InitExtensionMethodsMap()`、`SetJsEnvSelector()`，模块加载则走 `IJSModuleLoader` / `FJsEnv` 构造注入。扩展者拿到的是明确的方法/接口，而不是一个可随意 `Broadcast()` 的 raw bus。 | `Reference/puerts/unreal/Puerts/Source/Puerts/Public/PuertsModule.h:17`-`:49`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSModuleLoader.h:17`-`:25`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JsEnv.h:64`-`:68` | 先把 authority boundary 固定在 module/interface 层，再决定内部是否用 delegate 实现，外部不会天然拿到发布权。 |
| UnLua | 虽然也有 `FUnLuaDelegates`，但更关键的 runtime 路由 seam 是 settings 选定的 `ULuaEnvLocator` / `ULuaModuleLocator` provider class，扩展者主要实现 `Locate()` 这类方法，而不是直接拿到 LuaEnv 生命周期的发布权。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDelegates.h:22`-`:50`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaSettings.h:47`-`:56`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/LuaEnvLocator.h:22`-`:34`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/LuaModuleLocator.h:20`-`:26` | 即使保留 delegate，核心扩展 contract 也应尽量落到 provider/interface，而不是把 raw event source 本体公开给所有外部模块。 |
| UnrealCSharp | 核心路由能力同样优先走 provider/service：`UAssemblyLoader::Load()` 由 settings 选定的 loader 承担；module owner 再通过 `SetActive(bool)` 统一管理 active/inactive 广播。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Domain/AssemblyLoader.h:12`-`:18`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpSetting.h:140`-`:142`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/UnrealCSharpCore.h:17`-`:23`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/UnrealCSharpCore.cpp:19`-`:32` | 扩展 surface 应尽量把“执行能力”和“事件通知”分开，由 owner 统一发布状态转换，而不是把底层 carrier 整体公开。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 对 observer hook 建立 subscribe-only façade，对 strategy hook 建立 provider/policy façade，把发布权和订阅权在 API 层分离。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/` 新增 `FAngelscriptExtensionEvents` 或按域拆分的 `FAngelscriptCompilationEvents` / `FAngelscriptReloadEvents`，内部持有 private delegate，外部只看到 `SubscribePreCompile()`、`SubscribePostReload()` 这类返回 `FDelegateHandle` 的方法。 2. 对 observe-only 点优先改为 `DECLARE_EVENT` / private publisher 包装，例如 `PreCompile`、`PostCompile`、`OnInitialCompileFinished`、`OnPostReload`、`OnFullReload`、`OnLiteralAssetCreated`；内部模块继续通过 friend/private `PublishXxx()` 触发。 3. 对 strategy 点不要再沿用 `Delegate&` 暴露，例如 `GetDynamicSpawnLevel`、`GetDebugCheckBreakOptions`、`GetClassAnalyze`、`GetEditorGetCreateBlueprintDefaultAssetPath` 统一迁移到 provider interface / policy object，避免把“覆盖行为”伪装成普通事件。 4. 保留 legacy accessor 作为过渡层，但标记为 `legacy/expert-only`；新第一方消费者优先改用 subscribe-only façade，以便逐步缩小原始 delegate 暴露面。 5. 在扩展审计表中增加 `AuthorityKind` 字段，明确每个 hook 是 `Observer(SubscribeOnly)` 还是 `Strategy(SingleProvider/Reducer)`，减少扩展者猜 API 语义的成本。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Console.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/AngelscriptExtensionEvents.h/.cpp` |
| 预估工作量 | M |
| 架构风险 | 风险主要在迁移顺序。如果一口气收紧所有 raw delegate 暴露面，仓内现有第一方/第三方扩展可能立刻编译失败。第一阶段应先新增 façade，再把第一方调用点迁过去，最后才考虑压缩 legacy 入口。 |
| 兼容性 | 向后兼容。legacy `GetXxx()` / public static delegate 先保留；新 façade 只是增加明确的 subscribe-only / provider-only 正式入口。 |
| 验证方式 | 1. 新建一个宿主扩展模块，只通过新 façade 订阅 compile/reload 事件，验证无需接触 raw delegate 本体。 2. 把 `Bind_Console` 或 editor reload helper 迁到新 API 后复跑回归，确认事件次序不变。 3. 在扩展审计表中检查 `AuthorityKind` 输出，确保 observer hook 与 strategy hook 已被明确分类。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-EP55 | 生命周期 hook 缺少正式 state/replay contract，late subscriber 需手写补偿逻辑 | stateful lifecycle façade 新增 | 高 |
| P1 | Arch-EP56 | public hook 同时暴露订阅权和发布权，authority boundary 不清 | 事件/策略边界收口 | 高 |

---

## 架构分析 (2026-04-09 01:01)

### Arch-EP57：`ini/settings` 目前只能切换内建行为，不能声明“由谁提供该行为”

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `ini/settings` 是否是“配置型扩展点”，还是只是内建行为的开关面 |
| 当前设计 | 当前公开配置面主要由 `UAngelscriptSettings` 与 `FAngelscriptEngineConfig` 组成，但两者都只暴露 `bool / enum / string / array / set` 这类值配置；运行时、预处理器、调试数据库直接读取这些值并分支，没有让宿主在配置层声明自定义 provider class 或 policy object。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h:41`-`:223` 定义的字段全部是 `PreprocessorFlags`、`DisabledBindNames`、各类 `bool/enum/TArray/TSet`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:64`-`:95` 的 `FAngelscriptEngineConfig` / `FAngelscriptEngineDependencies` 也只提供 flags 与 lambda 依赖；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1291`-`:1296` 在初始化时直接把 settings 复制到 runtime state；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1931`-`:1940` 直接把 `DisabledBindNames` 合并进 bind 执行路径；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:485`-`:489` 与 `:523`-`:527` 直接按 settings 决定 warning 和 `float` 初始化语义；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:1500`-`:1504` 直接把 settings 投影到 debug database。 |
| 优点 | 配置面简单直接，已有行为的灰度和兼容开关成本低；对单一宿主项目也比较容易说明。 |
| 不足 | 宿主只能“调插件已有策略的参数”，不能在配置层声明自己的 `resolver/loader/provider`。这使得扩展仍然偏向 C++ 改码或手工注册，而不是项目级可配置 contract；同时设置读取散落在 `Engine/Preprocessor/DebugServer`，也不利于后续把扩展策略按实例或按环境收口。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 直接把 provider class 放进 settings：`EnvLocatorClass`、`ModuleLocatorClass`、`PreBindClasses` 都是项目配置的一部分，模块启动时实例化所选 locator，`LuaEnv` 再按 settings 取默认 module locator。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaSettings.h:23`-`:57`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaModule.cpp:103`-`:107`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:77`-`:79` | 配置不只控制“开还是关”，还控制“由哪个 provider 实现”。 |
| UnrealCSharp | settings 里直接放 `TSubclassOf<UAssemblyLoader>`；运行时通过 `GetAssemblyLoader()` 取默认对象，再由 `FMonoDomain` 用这个 loader 决定程序集加载来源。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpSetting.h:77`-`:159`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Setting/UnrealCSharpSetting.cpp:87`-`:93`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp:1106`-`:1110`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp:500`-`:503` | 用 settings 选择一个稳定的服务类，让扩展点从“改内部流程”变成“提供子类实现”。 |
| puerts | 虽然不是把 loader class 放进 `UPROPERTY`，但 `IJSModuleLoader` 是一等对象，`FJsEnv` 构造函数可直接注入自定义 loader；模块 owner 再决定传入默认 loader 还是别的实现。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSModuleLoader.h:17`-`:50`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JsEnv.h:64`-`:68`；`Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:185`-`:233` | 即使配置面仍然简单，核心策略对象也应先被做成正式 contract。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 保留现有布尔/枚举开关，但把关键行为的“实现者”提升为可配置 provider class，形成“值配置 + 服务配置”双层模型。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/` 新增可运行时安全实例化的 provider 基类，例如 `UAngelscriptImportResolver`、`UAngelscriptScriptRootProvider`、`UAngelscriptDebugSettingsProjector`。 2. 在 `UAngelscriptSettings` 中新增对应的 `TSubclassOf<>` 字段；默认类完整复用当前逻辑，继续读取现有 `bAutomaticImports`、`PreprocessorFlags`、`DisabledBindNames` 等值配置。 3. 在 `FAngelscriptEngine::Initialize()` 统一构建一次 `ConfiguredServices`，由它向 `Preprocessor`、`DebugServer`、script root 发现流程提供服务，逐步替代散落的 `GetDefault<UAngelscriptSettings>()`。 4. 第一阶段只迁移低风险点：`import warning`、script root 发现、debug database settings 投影；等默认 provider 稳定后再扩展到 bind policy 或 compile policy。 5. 在 `StateDump` 或新的扩展审计表里输出当前激活的 provider class 名称，避免项目侧只能靠读配置猜测行为来源。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/AngelscriptConfiguredServices.h/.cpp` |
| 预估工作量 | M |
| 架构风险 | 最大风险是把 editor-only 类型或不安全的 UObject 生命周期引入 runtime 启动路径。第一阶段应只允许 runtime-safe provider，并让默认 provider 完全复用现有逻辑。 |
| 兼容性 | 向后兼容。旧 `ini` 字段继续保留且语义不变；新增 `TSubclassOf<>` 未填写时自动回落到默认 provider。 |
| 验证方式 | 1. 新建宿主测试模块，配置一个自定义 provider class，验证无需改插件源码即可改变 import/script-root/debug settings 投影行为。 2. 复跑现有 settings/bind/preprocessor 相关测试，确认默认 provider 下行为与今天一致。 3. 用 dump 或审计表确认运行中的 provider class 可见。 |

### Arch-EP58：`virtual` 数量很多，但大多服务于脚本 Gameplay 生命周期，不是宿主侧插件扩展 contract

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 搜索到的大量 `virtual` 函数里，哪些是真正给宿主项目用来扩展插件服务的，哪些只是脚本/Gameplay 层覆写点 |
| 当前设计 | 当前最密集的 `virtual` surface 集中在 `UScriptEngineSubsystem`、`UScriptGameInstanceSubsystem`、`UScriptWorldSubsystem` 这三组 `Blueprintable, Abstract` 基类上，它们主要把 UE 生命周期转发成 `BP_Initialize`、`BP_Tick`、`BP_OnWorldBeginPlay` 之类脚本事件；而真正持有 `FAngelscriptEngine` 的 runtime owner 仍然是 concrete 的 `UAngelscriptGameInstanceSubsystem` 与 `FAngelscriptRuntimeModule`。这意味着“脚本作者可以很方便地子类化 gameplay subsystem”成立，但“宿主作者可以靠子类化替换插件核心服务”并没有成为正式 contract。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/BaseClasses/ScriptEngineSubsystem.h:6`-`:104`、`Plugins/Angelscript/Source/AngelscriptRuntime/BaseClasses/ScriptGameInstanceSubsystem.h:6`-`:88`、`Plugins/Angelscript/Source/AngelscriptRuntime/BaseClasses/ScriptWorldSubsystem.h:7`-`:158` 的 `virtual` 主要围绕 `BP_ShouldCreateSubsystem`、`BP_Initialize`、`BP_Tick`、world begin play 等脚本生命周期；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.h:11`-`:44` 定义的是 concrete `UAngelscriptGameInstanceSubsystem`，内部直接持有 `OwnedEngine` 与 `PrimaryEngine`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp:12`-`:29` 在初始化时直接决定 engine owner；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp:94`-`:118` 通过 `GetSubsystem<UAngelscriptGameInstanceSubsystem>()` 和 `ActiveTickOwners` 维护唯一当前 owner；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h:25`-`:63` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp:13`-`:24`、`:138`-`:165` 继续用 concrete module + concrete engine 完成启动和 fallback tick。 |
| 优点 | 脚本侧的 subsystem/gameplay 扩展体验很好，生命周期覆写点密集且语义贴近 UE 原生模式；runtime owner 也因此保持直接和稳定。 |
| 不足 | `virtual` 搜索结果会给人一种“插件已经高度可子类化”的错觉，但这些可覆写点多数并不作用于 bind、compile、import、runtime routing 等插件服务层。宿主如果想改变这些核心行为，仍然主要依赖 raw delegate、lambda 注入或改插件源码，而不是“继承一个公开基类就完成扩展”。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `ULuaEnvLocator` / `ULuaModuleLocator` 本身就是服务层对象，`Locate()` / `HotReload()` / `Reset()` 都是为了 runtime 路由而设计；模块启动时实例化 settings 选定的类。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/LuaEnvLocator.h:22`-`:49`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/LuaModuleLocator.h:20`-`:36`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaModule.cpp:103`-`:106`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:77`-`:79` | 真正鼓励用户“去子类化”的，是服务边界对象，而不是运行时 owner 本身。 |
| puerts | `IJSModuleLoader` 是纯虚接口，`DefaultJSModuleLoader` 提供默认实现；`FJsEnv` 构造函数显式接收 loader，`IPuertsModule` 还提供 `SetJsEnvSelector()` 这样的策略注入点。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSModuleLoader.h:17`-`:50`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JsEnv.h:64`-`:68`；`Reference/puerts/unreal/Puerts/Source/Puerts/Public/PuertsModule.h:37`-`:47` | `virtual` 应该落在 loader/selector 这类策略接口上，而不是只落在 gameplay wrapper 上。 |
| UnrealCSharp | `UAssemblyLoader::Load()` 是公开服务点，settings 选择使用哪个 loader，运行时在真正加载程序集时调用它。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Domain/AssemblyLoader.h:12`-`:18`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpSetting.h:140`-`:142`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp:500`-`:503` | 把 `virtual` 放在服务接口上，再由 owner 组合这个服务，比让用户猜“能不能继承某个 subsystem”更清楚。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把“脚本 Gameplay 可子类化”与“插件服务可替换”明确拆成两套公共 contract，避免 `virtual` surface 误导扩展者。 |
| 具体步骤 | 1. 保留 `UScriptEngineSubsystem`、`UScriptGameInstanceSubsystem`、`UScriptWorldSubsystem` 作为 script/gameplay-facing 基类，并在头文件注释或架构文档里明确它们不是 bind/compile/runtime owner 的正式替换 seam。 2. 为真正需要宿主替换的行为新增服务层基类或接口，例如 `UAngelscriptSourceLocator`、`UAngelscriptImportResolver`、`IAngelscriptBindingContributor`、`IAngelscriptRuntimeSelector`，让 `virtual` 出现在正确的层级。 3. 让 `UAngelscriptGameInstanceSubsystem` 与 `FAngelscriptRuntimeModule` 继续承担 owner/tick 责任，但改为组合这些 provider，而不是被误用为替换入口。 4. 提供一个最小样例：宿主项目定义自定义 provider 子类并通过 settings 或 registry 装配，证明不必继承 `UAngelscriptGameInstanceSubsystem` 也能改变核心服务行为。 5. 在扩展审计表中额外标明 `GameplaySubclassPoint` 与 `ServiceProviderPoint` 两类入口，降低使用者误判成本。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/BaseClasses/ScriptEngineSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/BaseClasses/ScriptGameInstanceSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/BaseClasses/ScriptWorldSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/AngelscriptServiceProviders.h/.cpp` |
| 预估工作量 | M |
| 架构风险 | 若仓外已有项目私下把 `UAngelscriptGameInstanceSubsystem` 当作可替换 seam 使用，直接收紧语义会造成预期落差。第一阶段应先增加 provider contract 和文档澄清，不改现有 concrete owner 行为。 |
| 兼容性 | 向后兼容。现有 script subsystem 子类与 concrete `UAngelscriptGameInstanceSubsystem` 均继续可用；新增服务层 contract 只是把真正支持的扩展路径显式化。 |
| 验证方式 | 1. 新建宿主扩展模块，分别实现一个自定义 service provider 和一个 `UScriptWorldSubsystem` 子类，验证两者职责边界清晰且互不干扰。 2. 复跑 subsystem 与 runtime owner 相关测试，确认默认路径行为不变。 3. 用扩展审计表检查 `GameplaySubclassPoint` / `ServiceProviderPoint` 分类是否正确输出。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-EP57 | `ini/settings` 只有值开关，没有 provider 级配置 contract | 配置型扩展面升级 | 高 |
| P1 | Arch-EP58 | `virtual` 主要落在 script gameplay 包装层，宿主侧服务替换 seam 不明确 | 服务层接口拆分 | 高 |

---

## 架构分析 (2026-04-09 01:13)

### Arch-EP59：`bind` 扩展发现仍是“单插件 cache + 初始化单次扫描”，不适合独立扩展插件并行演进

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 外部插件如何在不修改 `Plugins/Angelscript/` 源码的前提下，稳定追加自定义 `global function`、类型绑定和生成 bind module |
| 当前设计 | `FAngelscriptBinds::FBind` 仍是主要注册入口，但它本质上只是把函数塞进一个进程内静态数组；正式运行时再由 `FAngelscriptEngine::Initialize()` 在绑定前从 `Angelscript` 核心插件目录读取一次 `BindModules.Cache` 并加载其中列出的模块，然后立即执行单次 `BindScriptTypes()`。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:438`-`:469` 的 `FBind` 构造直接调用 `RegisterBinds()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:151`-`:154` 只把 bind 追加进 `GetBindArray()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1473`-`:1489` 只从 `FindPlugin("Angelscript")` 得到的核心插件目录读取 `BindModules.Cache` 并 `LoadModule()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1491`-`:1495` 与 `:1915`-`:1921` 随后立刻进入单次 `BindScriptTypes()`；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1005`-`:1077` 表明 cache 的生成也由核心插件自己的 editor 生成流程主导。 |
| 优点 | 启动顺序清晰，内建 bind 的执行顺序可控；editor 生成的 bind shards 能在首次编译前集中装载。 |
| 不足 | 外部扩展插件没有独立声明自己的 `bind manifest` 或 contributor contract。由已读 runtime 源码可推断，扩展发现入口被固定在核心插件的一次性初始化窗口里，扩展者要么依赖低层 `FBind` 静态注册和装载时序，要么介入核心插件的 cache 生成链，难以形成独立发布、独立升级的扩展插件生态。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 把运行时扩展声明成 settings 中可配置的 provider class 与预绑定列表。`EnvLocatorClass`、`ModuleLocatorClass`、`PreBindClasses` 都是正式配置项，模块启动时按配置实例化 locator，`LuaEnv` 再按 `ModuleLocatorClass` 路由脚本模块。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaSettings.h:47`-`:56`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaModule.cpp:103`-`:106`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:77`-`:79` | 扩展发现不依赖核心插件私有 cache，而是通过正式配置和 provider object 声明。 |
| puerts | 把扩展接入做成 module/interface contract。`IPuertsModule` 公开 `InitExtensionMethodsMap()` 和 `SetJsEnvSelector()`；`FJsEnvImpl::InitExtensionMethodsMap()` 每次扫描所有继承 `UExtensionMethods` 的原生类，把扩展方法挂进 map；`FJsEnv`/`FJsEnvGroup` 构造时显式接收 `IJSModuleLoader`。 | `Reference/puerts/unreal/Puerts/Source/Puerts/Public/PuertsModule.h:17`-`:49`；`Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:97`-`:121`、`:185`-`:233`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:931`-`:983`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Public/ExtensionMethods.h:18`-`:22` | 扩展者只需提供约定类型或调用公开 module API，而不是修改核心插件自己的生成 cache。 |
| UnrealCSharp | 把“哪些模块/类型进入导出与生成”做成显式 workflow。`GeneratorModules()` 在构建期扫描模块，`bEnableExport`/`ExportModule`/`ClassBlacklist` 来自 settings，`OnBeginGenerator`/`OnEndGenerator` 是稳定的再进入口。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/UnrealCSharpCore.build.cs:100`-`:140`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpEditorSetting.h:139`-`:148`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Delegate/FUnrealCSharpCoreModuleDelegates.h:14`-`:30`；`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:237`-`:309` | 发现范围、执行阶段和重入时机都是显式 contract，不需要扩展者反向推理核心插件的初始化窗口。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 保留现有 `FBind` / `BindModules.Cache` 兼容层，但新增“多插件 contributor 发现层”，把扩展发现从核心插件私有生成物升级为正式 public contract。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/` 新增 `IAngelscriptBindingContributorModule` 或 `UAngelscriptBindingContributor`，明确 `RegisterGlobalBinds()`、`RegisterTypeBindings()`、`GetContributorPriority()`。 2. 在 `FAngelscriptEngine::Initialize()` 前半段新增 `DiscoverBindingContributors()`，通过 `IPluginManager::Get().GetEnabledPlugins()` 与 `FModuleManager` 枚举每个启用插件的 contributor manifest，而不是只读取 `FindPlugin("Angelscript")`。 3. 把当前 `BindModules.Cache` 读取逻辑迁成兼容 fallback：先收集各插件 contributor，再把老的核心插件 cache 结果并入 registry，最后统一排序后执行 `BindScriptTypes()`。 4. 给 hot reload / 重新编译入口增加显式 `RefreshBindingContributors()`，允许外部扩展插件在 editor 生命周期内重新发现，而不是只能赌首次初始化窗口。 5. 提供一个最小示例插件，演示“自定义全局函数 + 自定义类型绑定 + 不修改核心插件源码”的标准接入路径。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/AngelscriptBindingContributor.h/.cpp` |
| 预估工作量 | M |
| 架构风险 | 主要风险是 contributor 排序与重复 bind 冲突。若没有把 `BindName`、来源插件和优先级一起纳入 registry，外部插件并行扩展时会把今天的“隐式装载顺序”问题换一种形式复现。 |
| 兼容性 | 向后兼容。现有 `FAngelscriptBinds::FBind`、核心插件内置的 `BindModules.Cache` 和老的 generated bind module 继续可用；新 contract 只是补正式扩展面。 |
| 验证方式 | 1. 新建两个独立宿主插件，各自提供一个 binding contributor，验证无需修改核心插件源码即可被发现并加载。 2. 复跑 `AngelscriptBindConfigTests`，确认 `DisabledBindNames` 对外部 contributor 仍然生效。 3. 在 editor 中触发一次 contributor refresh，验证新增模块可在不重启进程的情况下进入后续编译流程。 |

### Arch-EP60：扩展面大多是进程级 `static` 或共享状态，不是 `FAngelscriptEngine` instance-scoped contract

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 多 `FAngelscriptEngine`/clone 场景下，扩展 hook、settings 和服务策略能否做到按 engine instance 定制与隔离 |
| 当前设计 | 当前 compile/debug/editor hook 主要暴露为 `FAngelscriptRuntimeModule` 的函数内 `static delegate`；bind/type 数据库在没有当前 engine 时退回进程级 legacy storage，有当前 engine 时又被 clone 共享；settings 既会在 engine 初始化时缓存到 `ConfigSettings`，又会在 `Preprocessor`/`DebugServer` 中直接再次读取 `GetDefault<UAngelscriptSettings>()`，因此扩展策略天然更接近“进程全局状态”而不是“engine instance 服务”。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h:9`-`:21` 声明了大量 delegate 型扩展面，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp:42`-`:134` 为每个入口返回函数内 `static Delegate`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:23`-`:33` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:35`-`:45` 在没有当前 engine 时退回 `LegacyBindState` / `LegacyDatabase`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:628`-`:651` 让 clone 共享 `SharedState`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2848`-`:2857` 又把 `ConfigSettings` 指针直接从 source engine 复制到 clone；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1291`-`:1296` 在 full engine 初始化时从 `GetMutableDefault<UAngelscriptSettings>()` 抓取配置并写入静态命名空间剥离规则；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:485`-`:489`、`:523`-`:529` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:1499`-`:1504` 继续直接读取全局 settings。 |
| 优点 | 单 engine 路径简单直接，clone 共享 bind/type 数据可减少重复初始化成本；多数扩展点实现代价低。 |
| 不足 | 由此可推断，想在同一进程里让两个 engine/clone 使用不同的 import policy、compile observer、debug settings 投影或 editor hook，当前几乎没有正式路径。扩展者一旦绑定 `static delegate` 或修改全局 settings，影响范围天然是“当前进程/当前共享状态”，而不是“这个 engine instance”。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `ULuaEnvLocator` / `ULuaModuleLocator` 是正式服务对象，`Locate()` / `HotReload()` / `Reset()` 都是实例方法；`ULuaEnvLocator_ByGameInstance` 甚至直接按 `UGameInstance` 保存多份 `Env`。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/LuaEnvLocator.h:22`-`:49`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/LuaModuleLocator.h:20`-`:37`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaModule.cpp:103`-`:106` | 把扩展策略放进可实例化 service object，天然具备按环境或按宿主对象隔离的能力。 |
| puerts | `FJsEnv` / `FJsEnvGroup` 构造时显式接收 `IJSModuleLoader`，`IPuertsModule::SetJsEnvSelector()` 再把 env 选路策略交给 group；`DefaultJSModuleLoader` 自己携带 `ScriptRoot` 和 `Search/Load` 逻辑。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSModuleLoader.h:17`-`:46`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JsEnv.h:61`-`:70`；`Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:112`-`:121`、`:185`-`:233`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DefaultJSModuleLoader.cpp:53`-`:123` | 扩展策略与 env 实例一起构造，避免把 module resolution / env selection 固定成进程级静态状态。 |
| UnrealCSharp | `UAssemblyLoader` 是显式服务基类，settings 只负责选择默认 loader class；真正加载程序集时由 `FMonoDomain` 调用当前 loader 对象的 `Load()`。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Domain/AssemblyLoader.h:12`-`:18`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpSetting.h:104`、`:140`-`:142`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Setting/UnrealCSharpSetting.cpp:87`-`:92`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp:500`-`:503` | 即使全局 settings 仍存在，真正的可变策略也被收敛到 provider object，而不是散落在各处 `GetDefault<>` 调用里。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把扩展相关的 hook、配置快照和策略对象收敛到 `engine-scoped extension context`，默认仍走当前全局行为，但允许 clone / test engine 拿到独立服务实例。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/` 新增 `FAngelscriptExtensionContext` 或 `FAngelscriptConfiguredServices`，由 `FAngelscriptEngine` 在 `Initialize()` 时解析 settings/provider 并持有。 2. 把 `AngelscriptPreprocessor`、`AngelscriptDebugServer`、compile/reload hook 的读取路径改成优先消费 `FAngelscriptEngine` 当前上下文的 `ConfiguredServices`，逐步消除直接 `GetDefault<UAngelscriptSettings>()`。 3. 为 `FAngelscriptRuntimeModule::GetPreCompile()` 等静态扩展入口增加 per-engine façade：legacy 静态入口仍保留，但内部转发到当前 engine 的 event hub；clone engine 可拥有自己的 observer 列表。 4. 明确哪些状态继续共享、哪些状态应该独立：`TypeDatabase/BindState` 第一阶段可继续共享只读结果，`CompileObservers/ImportResolver/DebugProjector` 则改为 instance-scoped。 5. 增加多 engine 回归测试，验证两个 clone engine 在同一进程内可挂不同扩展服务而互不串扰。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/AngelscriptExtensionContext.h/.cpp` |
| 预估工作量 | M |
| 架构风险 | 若过早把所有状态都从 shared state 拆走，会影响当前 clone 复用和初始化性能。第一阶段应只把“真正可变的扩展策略”抽离出来，bind/type 结果继续共享。 |
| 兼容性 | 基本向后兼容。静态 `GetXxx()` hook 与现有 settings 字段先继续存在；新 `engine-scoped` context 作为增量能力引入，旧调用点逐步迁移。 |
| 验证方式 | 1. 新增多 engine 测试：给两个 clone engine 注入不同的 import/debug provider，确认 warning 与 debug database 输出不同。 2. 复跑现有 `AngelscriptMultiEngineTests`、`AngelscriptEngineIsolationTests`，确认默认单 engine 行为不变。 3. 增加一条扩展审计输出，列出当前 engine 绑定的 provider class / observer 数量，验证 clone 之间互不污染。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-EP59 | `bind` 扩展发现被锁在核心插件的一次性 cache/初始化窗口中 | 扩展发现层显式化 | 高 |
| P1 | Arch-EP60 | 扩展 hook 与 settings 主要是进程级 `static`，不是 engine-scoped contract | 扩展上下文实例化 | 高 |

---

## 架构分析 (2026-04-09 23:56)

### Arch-EP61：脚本工作区与 staging 仍依赖固定目录约定，缺少 project-level workspace contract

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 项目方能否在不改插件源码的前提下，自定义脚本工作区位置、额外脚本根和打包 staging 路径 |
| 当前设计 | 当前脚本根发现仍是约定优先：runtime 默认把 `ProjectDir()/Script` 作为主根，再把所有 `CanContainContent` 的已启用插件目录下的 `Script/` 追加进来；editor directory watcher 也直接复用这套发现结果。与此同时，`UAngelscriptSettings` 只有行为开关和 warning/debug 相关值，没有 `ScriptDirectory`、`AdditionalScriptRoots`、`DirectoriesToStageAsUFS` 之类项目级 workspace/staging 字段。这意味着项目如果想把脚本放在非约定目录、拆分多个脚本工作区，或显式声明交付时需要 stage 的目录，当前仍主要依赖改目录布局或改宿主代码。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:558`-`:565` 的 `GetEnabledPluginScriptRoots` 只返回 `Plugin->GetBaseDir() / "Script"`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1326`-`:1369` 的 `DiscoverScriptRoots()` / `MakeAllScriptRoots()` 固定从 `ProjectDir()/Script` 与插件 `Script/` 目录生成 root 列表；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:367`-`:380` 直接把 `MakeAllScriptRoots()` 的结果注册给 `DirectoryWatcher`；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:388`-`:393` 只注册 `UAngelscriptSettings` 页面；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h:41`-`:225` 没有任何脚本目录、publish/stage 目录或工作区层级字段。 |
| 优点 | 默认约定非常直接，项目首次接入成本低，script root 搜索顺序也相对稳定。 |
| 不足 | 这套设计把“工作区布局”隐含进目录命名规则，而不是产品化为可配置 contract。对扩展作者来说，脚本仓布局、editor watch 范围和 cook/staging 范围无法通过 settings/manifest 显式表达，项目一旦想采用非默认目录结构或多工作区组织，就会落回源码级调整。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | 把脚本工作区与生成范围显式做成 editor settings：`ScriptDirectory`、`SupportedModule`、`ExportModule`、`ClassBlacklist` 都是正式配置项；editor module 启动时还会把 publish 目录写入 `DirectoriesToAlwaysStageAsUFS`。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpEditorSetting.h:98`-`:148`；`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:209`-`:233` | 工作区位置和交付目录不应只靠目录约定，应该进入 settings 与 packaging orchestration。 |
| UnLua | editor module 会把 `Script`、插件脚本目录以及 `UnLuaExtensions` 内容插件里的 `Content/Script` 自动加入 `DirectoriesToAlwaysStageAsUFS`；这些路径被显式视为 shipped artifact。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:188`-`:232` | 即使保持约定式目录，也可以把 staging/update 责任收口到正式模块流程里，而不是让项目侧自己补打包配置。 |
| puerts | 直接把脚本根做成 settings 字段 `RootPath`，模块创建 `DefaultJSModuleLoader` 和 `FJsEnv` 时显式读取该值，而不是硬编码某个固定目录。 | `Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsSetting.h:15`-`:23`；`Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:185`-`:233` | 先把“脚本根是什么”做成正式配置输入，再决定默认 loader 如何消费。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留当前 `ProjectDir()/Script` 约定的前提下，补一个 project-level workspace/staging contract，让脚本根、watch 范围和交付目录可以被显式声明。 |
| 具体步骤 | 1. 在 `AngelscriptEditor` 新增独立 `UAngelscriptEditorSettings`，至少提供 `PrimaryScriptRoot`、`AdditionalScriptRoots`、`bAutoStageScriptRoots`、`GeneratedArtifactRoot` 四类字段；未配置时默认回落到今天的 `ProjectDir()/Script` 规则。 2. 把 `FAngelscriptEngine::DiscoverScriptRoots()` 改为优先读取 editor/project settings 中声明的 roots，再合并 legacy 的 project/plugin `Script/` 路径；第一阶段只做 append，不打破旧顺序。 3. 让 `AngelscriptEditorModule` 的 directory watcher 与生成产物输出路径消费同一份 resolved root 列表，避免 runtime/editor 对“工作区”各自推断。 4. 为 `bAutoStageScriptRoots` 增加 editor-only helper：当脚本根指向非默认目录时，自动检查并可选地更新 `UProjectPackagingSettings::DirectoriesToAlwaysStageAsUFS`，同时在 settings UI 给出明确提示。 5. 在 `StateDump` 增加 `WorkspaceRoots.csv` 或等价表，输出实际命中的脚本根、来源（legacy/project/editor setting）与 staging 状态，降低项目排查成本。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/AngelscriptEditorSettings.h/.cpp` |
| 预估工作量 | M |
| 架构风险 | 主要风险是把当前“约定式路径”与新 settings 同时引入后，root 优先级不清导致重复编译或 watch 重复注册。第一阶段必须把 resolved root 排序和去重逻辑固定下来，并保留 legacy fallback 的显式来源标记。 |
| 兼容性 | 向后兼容。旧项目不配置新 settings 时继续使用当前目录约定；新字段只是增加显式工作区与 staging 控制面。 |
| 验证方式 | 1. 新建一个把脚本根改到非默认目录的宿主样例，验证 editor watcher、compile、hot reload 都能命中新路径。 2. 开启 `bAutoStageScriptRoots` 后检查 `DirectoriesToAlwaysStageAsUFS` 是否正确更新，关闭后确认只给 warning 不强写配置。 3. 保留一个 legacy `ProjectDir()/Script` 项目回归，确认默认布局行为不变。 |

### Arch-EP62：`AngelscriptEditor` 目前只接受 `UASClass` 型 contributor，原生 editor module 没有对等扩展入口

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 原生 `C++ Editor module` 是否能像脚本类一样，正式接入 Angelscript 的 editor workflow、菜单和作者工具，而不必修改 `AngelscriptEditor` 自身 |
| 当前设计 | 前文已经记录 editor 侧存在成熟的 `UScriptEditorMenuExtension` object model；本轮新增发现是，这套 contributor contract 目前只接受 `UASClass`。`RegisterExtensions()` 只扫描 `TObjectIterator<UASClass>` 中派生自 `UScriptEditorMenuExtension` 的脚本类，而 editor module 自己的 debug-asset 列表、create-blueprint、tool menu startup callback 仍是 `StartupModule()` 里的匿名 lambda。结果是：脚本作者可以很方便地扩展菜单，但原生 editor 插件若想新增 toolbar、资产动作或 workflow listener，没有一条与脚本类对等的 public contributor seam。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h:42`-`:85` 定义了脚本扩展对象基类；`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp:845`-`:863` 的 `RegisterExtensions()` 只遍历 `TObjectIterator<UASClass>`，并要求 `ScriptClass->IsChildOf(UScriptEditorMenuExtension::StaticClass())`；同文件 `:868`-`:1116` 之后的注册逻辑也完全围绕脚本类 CDO 展开；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:397`-`:415` 又把 `GetDebugListAssets()`、`GetEditorCreateBlueprint()` 和 `UToolMenus::RegisterStartupCallback()` 直接硬编码在模块启动里，没有暴露 `IAngelscriptEditorContributor` 之类的 native registry。 |
| 优点 | 对 Angelscript 脚本作者来说，这条路径非常强，菜单和上下文动作可以完全通过 script class authoring 完成。 |
| 不足 | 对原生 editor 扩展作者来说，contract 仍然不完整。想做与 UnrealCSharp Blueprint toolbar、UnLua editor toolbar、puerts editor watcher 类似的 native owner object，只能绕过现有模型去碰模块内部实现，难以形成可复用的 editor 扩展插件生态。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | 用 `FUnrealCSharpBlueprintToolBar` 这类 native owner object 注册 Blueprint editor extender，并在 `Deinitialize()` 中对称解绑；editor module 负责创建和持有这些 contributor。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:21`-`:55`；`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:61`-`:67`、`:154`-`:170` | contributor 不必是脚本类；native owner object 也可以成为正式 public seam。 |
| UnLua | `FBlueprintToolbar` 作为 editor toolbar owner，在 `Initialize()` 中向 `FBlueprintEditorModule` 注册 extender；`FUnLuaEditorModule::OnPostEngineInit()` 统一初始化 toolbar 和 IntelliSense 相关 owner。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/BlueprintToolbar.cpp:19`-`:30`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:88`-`:105` | 由 editor module 持有 native contributor，可把 UI、listener 与生命周期清晰绑定在一起。 |
| puerts | `FPuertsEditorModule` 自己持有 `SourceFileWatcher` 和 `JsEnv`，在 `OnPostEngineInit()` 中创建这些 editor-side service object，而不是把 editor 扩展能力塞回脚本类扫描。 | `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:76`-`:164` | 对 editor workflow 来说，模块级 owner object 往往比脚本 class scan 更适合承载 watcher、toolbar 和 command。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 保留 `UScriptEditorMenuExtension` 这条强脚本路径，但为原生 editor 模块补一个对等的 contributor registry，让 script contributor 与 native contributor 可以并存。 |
| 具体步骤 | 1. 在 `AngelscriptEditor/Public/Extension/` 新增 `IAngelscriptEditorContributor` 或 `FAngelscriptEditorContributorRegistry`，最小职责包括 `RegisterMenus()`、`RegisterAssetActions()`、`RegisterWorkflowHooks()` 与 `Unregister()`。 2. 把 `AngelscriptEditorModule.cpp` 里当前匿名 lambda 的 debug-asset/create-blueprint/toolmenu 逻辑迁成默认 native contributor，先证明这条 seam 可以承载第一方功能。 3. 让 `UScriptEditorMenuExtension` 通过 adapter 接入同一个 registry，使脚本类 contributor 仍然存在，但不再是 editor extensibility 的唯一入口。 4. 对外提供 module-owned registration API，返回 `FDelegateHandle` 或 contributor handle，允许外部 editor 插件在 `StartupModule()` / `ShutdownModule()` 成对接入与撤销。 5. 在 `StateDump` 的 editor 扩展表中增加 `ContributorKind(ScriptClass/NativeModule)`、`SourceModule`、`OwnerType` 字段，便于排查冲突与来源。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorStateDump.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptEditor/Public/Extension/AngelscriptEditorContributor.h/.cpp` |
| 预估工作量 | M |
| 架构风险 | 最大风险是把现有 script extension 生命周期和 native contributor 生命周期混在一起，导致重复注册或 teardown 时序错乱。第一阶段应让 script contributor 先通过 adapter 进入 registry，并保持 native/script 两类 contributor 分开统计。 |
| 兼容性 | 向后兼容。现有 `UScriptEditorMenuExtension` 与现有菜单行为保持不变；新增 native contributor registry 只是补齐一条正式 public seam。 |
| 验证方式 | 1. 新建一个外部 `C++ Editor` 插件，只通过新 registry 注册一个 toolbar/menu action，验证无需修改 `AngelscriptEditorModule` 即可出现。 2. 同时保留一个 `UScriptEditorMenuExtension` 脚本类，验证 script/native contributor 可并存且卸载时不会残留。 3. 通过 `EditorMenuExtensions.csv` 或新增 dump 字段确认 contributor 来源和类型都可见。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-EP61 | 脚本工作区与 staging 仍依赖固定目录约定，缺少 project-level workspace contract | 配置与交付入口显式化 | 高 |
| P2 | Arch-EP62 | `AngelscriptEditor` 只接受 `UASClass` 型 contributor，原生 editor module 没有对等扩展入口 | editor contributor registry 新增 | 中高 |

---

## 架构分析 (2026-04-10 00:07)

### Arch-EP63：`FAngelscriptEngine` 已有 compile/reload hook，但“environment created/ready/shutdown” 不是正式扩展阶段

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 外部模块能否在不改插件源码的前提下，于 engine/environment 创建、ready、销毁时稳定挂接服务与清理资源 |
| 当前设计 | 当前 public lifecycle hook 主要停在 compile/class-analyze/literal-asset 层；`FAngelscriptEngine::Initialize()` / `Shutdown()` 与 `UAngelscriptGameInstanceSubsystem` 的 owner 生命周期没有对应 public event，第一方代码只能把 `OnInitialCompileFinished` 当成“环境已可用”的替代信号。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h:15`-`:21`、`:37`-`:47` 只公开 `PreCompile`、`PostCompile`、`OnInitialCompileFinished`、`PreGenerateClasses`、literal asset 和 editor 相关 delegate；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp:72`-`:116` 这些 accessor 只返回 static delegate，没有 `OnEngineInitialized/OnEngineShutdown`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:819`-`:857` 的 `Initialize()` 内部只执行 `PreInitialize_GameThread()`、`Initialize_AnyThread()`、`PostInitialize_GameThread()`、`InitializeOwnedSharedState()`，而 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1653`-`:1656` 的 `PostInitialize_GameThread()` 仅广播 `GetOnInitialCompileFinished()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp:16`-`:23`、`:39`-`:45` 直接调用 `OwnedEngine.Initialize()` / `PrimaryEngine->Shutdown()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Console.h:18`-`:28` 需要手工先查 `IsInitialCompileFinished()`，再订阅 `GetOnInitialCompileFinished()` 延迟注册 console variable，说明第一方代码也在把 compile-finished 当 readiness proxy。 |
| 优点 | 生命周期实现直接，engine owner 关系简单；现有项目几乎不需要理解更多阶段名。 |
| 不足 | 外部扩展者没有一条“在 environment 真正创建/就绪/销毁时接入”的正式 contract，只能挂在 compile/reload 事件或 subsystem 初始化上自行推断时机。这会直接抬高 per-engine service、资源清理、multi-engine 隔离和宿主工具链接入成本。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 明确把 Lua VM 生命周期作为 public contract 暴露：`FUnLuaDelegates` 提供 `OnLuaStateCreated`、`OnLuaContextInitialized`、`OnPreLuaContextCleanup`、`OnPostLuaContextCleanup`；`FLuaEnv` 构造/析构时分别广播创建和销毁。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDelegates.h:25`-`:49`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:144`-`:176` | 让扩展者围绕“脚本环境生命周期”接入，而不是借用编译完成事件替代。 |
| UnrealCSharp | 把 runtime environment 初始化做成专门 delegate，`FCSharpEnvironment` 初始化完成后显式广播 `OnCSharpEnvironmentInitialize`。 | `Reference/UnrealCSharp/Source/UnrealCSharp/Public/Delegate/FUnrealCSharpModuleDelegates.h:6`-`:16`；`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp:133` | 即使阶段很少，也要把“环境 ready”做成独立事件，而不是与编译/生成阶段混用。 |
| puerts | 不依赖隐式全局时机，而是把 `FJsEnv` / `FJsEnvGroup` 作为显式 environment owner；创建时直接接收 `IJSModuleLoader`、`ILogger`、`OnSourceLoadedCallback` 与 `JsEnvSelector`，扩展从 env 构造边界进入。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JsEnv.h:64`-`:68`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JsEnvGroup.h:21`-`:39`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:98`-`:100`、`:347`-`:349` | 不一定都用 delegate；关键是 environment 边界要成为正式 contract，而不是隐式内部流程。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留现有 compile/reload hook 的前提下，新增 engine-scoped 的 `environment lifecycle` contract，把“环境创建/ready/销毁”与“编译完成”拆开。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/` 新增 `FAngelscriptEnvironmentLifecycleEvents` 或 `IAngelscriptEnvironmentObserver`，最小事件集建议为 `OnEnvironmentPreInitialize`、`OnEnvironmentReady`、`OnEnvironmentShutdown`。 2. 在 `FAngelscriptEngine::Initialize()` 中分别于 `PreInitialize_GameThread()` 之后和 `InitializeOwnedSharedState()` 之后发布 context；第一阶段 `OnEnvironmentReady` 不等同于 `OnInitialCompileFinished`，两者并存。 3. 在 `FAngelscriptEngine::Shutdown()` 资源释放前发布 `OnEnvironmentShutdown`，payload 至少包含 `InstanceId`、`bOwnsEngine`、`SharedState` 是否共享、当前 script roots。 4. 提供 `RunWhenEnvironmentReady()` helper，把 `Bind_Console` 这类“先查状态、再订阅 delegate”的补偿样板收口到公共层。 5. 对 clone/multi-engine 路径，事件必须挂在 `FAngelscriptEngine` 实例或 `ExtensionContext` 上，避免再次回到 process-global static delegate。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Console.h`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/AngelscriptEnvironmentLifecycle.h/.cpp` |
| 预估工作量 | M |
| 架构风险 | 最大风险是 ready 时机定义不清，导致扩展者把 `EnvironmentReady` 误当成“初编译成功”。第一阶段必须明确区分 `EnvironmentReady` 与 `InitialCompileFinished` 两类阶段，并把旧 compile hook 保留下来。 |
| 兼容性 | 向后兼容。现有 `GetOnInitialCompileFinished()` / compile/reload delegates 继续可用；新 contract 只是补一层更早、更稳定的 environment lifecycle seam。 |
| 验证方式 | 1. 新增宿主扩展测试，分别在 environment 创建前后注册 observer，验证 `RunWhenEnvironmentReady()` 只执行一次。 2. 在 subsystem owner 与 module fallback owner 两条路径上验证 `OnEnvironmentShutdown` 都会触发且不泄露 handle。 3. 增加 clone engine 回归，验证两个 engine 的 lifecycle observer 不串扰。 |

### Arch-EP64：诊断与异常通道被硬编码到 `FAngelscriptEngine` 内部，外部模块没有正式 `logger/diagnostic sink` contract

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 项目方能否在不改插件源码的前提下，自定义 compile diagnostics、exception reporting、CI/IDE sink 或 editor 提示策略 |
| 当前设计 | 当前诊断链路是 engine 内部硬编码：AngelScript message callback 固定指向 `LogAngelscriptError`，context exception callback 固定指向 `LogAngelscriptException`，`ScriptCompileError()` 只会写内部 `Diagnostics` map 并 `UE_LOG`；这些 sink 没有 public delegate、provider class 或 `ILogger` 式接口，异常 UI 甚至直接调用 `FMessageDialog` / `devEnsure`。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:239`-`:257` 的 `CreateConfiguredContext()` 把 exception callback 固定为 `LogAngelscriptException`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:910`、`:1422` 在初始化时把 engine message callback 固定为 `LogAngelscriptError`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:341`-`:353`、`:510`-`:534` 表明 `FDiagnostic`、`Diagnostics`、`EmitDiagnostics()` 都是 `FAngelscriptEngine` 内部成员/方法；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:4926`-`:4941` 的 `ScriptCompileError()` 只写 `Diagnostics` 并 `UE_LOG`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:5012`-`:5084` 的 `LogAngelscriptError()` 只做 `UE_LOG` + 内部 `Diagnostics` 累积；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:5283`-`:5295` 的 `LogAngelscriptException()` 直接弹 `FMessageDialog` 或 `devEnsure`；与此同时 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h:15`-`:21`、`:37`-`:47` 暴露了 compile/literal asset/editor hook，但没有任何 diagnostics/logger 扩展点。 |
| 优点 | 默认行为一致且容易排查，日志、on-screen message、debug server 内部诊断都由同一 owner 控制。 |
| 不足 | 外部 IDE 集成、CI 收集、editor 面板、遥测或自定义异常策略都无法正式接管；项目若不想弹 modal dialog、想把 error sink 改成自定义 reporter，当前几乎只能改插件源码。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 公开 `ReportLuaCallError` 与 `CustomLoadLuaFile` delegate，错误报告和加载策略都允许宿主替换；`ReportLuaCallError()` 在默认实现前先查询外部 delegate。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDelegates.h:33`-`:49`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaBase.cpp:151`-`:160`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:557`-`:567` | 对错误与加载这种高波动环节，至少要有一条可替换的 reporter/loader seam，而不是只写死默认日志。 |
| puerts | 把日志抽象成 `ILogger`，`FJsEnv` / `FJsEnvGroup` 构造时直接注入 logger；默认实现 `FDefaultLogger` 只是一个可替换 baseline。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSLogger.h:19`-`:37`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JsEnv.h:64`-`:68`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JsEnvGroup.h:21`-`:25`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:98`-`:100`、`:347`-`:349` | `logger/reporter` 更适合作为 env-scoped service，而不是散落在静态回调里。 |
| UnrealCSharp | 至少把日志职责集中成 `FMonoLog` 这类专门组件，并通过 `OnCSharpEnvironmentInitialize` 给外部 registry 一个稳定的挂接阶段。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Log/FMonoLog.h:5`-`:17`；`Reference/UnrealCSharp/Source/UnrealCSharp/Public/Delegate/FUnrealCSharpModuleDelegates.h:10`-`:16`；`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp:133` | 即使先不完全开放日志替换，也应把“日志组件”和“环境挂接阶段”明确化，避免所有 sink 都硬编码在 `engine.cpp`。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把当前 compile/error/exception 输出从 `FAngelscriptEngine` 内部 hardcode 提升为 engine-scoped 的 `diagnostic services`，默认实现保持现状。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/` 新增 `IAngelscriptDiagnosticSink`、`IAngelscriptExceptionReporter` 或 `FAngelscriptDiagnosticServices`，至少覆盖 `OnCompileDiagnostic`、`OnRuntimeException`、`OnUserFacingExceptionUI` 三类职责。 2. 在 `FAngelscriptEngine::Initialize()` / `CreateConfiguredContext()` 中，改为从当前 `ExtensionContext` 读取 diagnostic service，再设置 AngelScript message/exception callback；默认 service 内部完整复用 `LogAngelscriptError()`、`LogAngelscriptException()` 当前行为。 3. 保留 `Diagnostics` map 和 `EmitDiagnostics()` 作为第一阶段内部存储，但把新增 service 放在写入点之前，使外部 sink 能收到结构化 `FDiagnostic` / exception payload。 4. 把 modal dialog 与 `devEnsure` 决策从 `LogAngelscriptException()` 拆到默认 `ExceptionReporter`，为 editor-only 项目、自动化测试、CI/headless 运行提供可替换策略。 5. 在 `StateDump` 或扩展审计表中增加 `DiagnosticServices.csv`，输出当前 engine 使用的 sink/reporter class、是否启用 modal UI、是否转发到 debug server。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/AngelscriptDiagnosticServices.h/.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp` |
| 预估工作量 | M |
| 架构风险 | 诊断回调可能发生在编译线程、game thread 和异常路径上，若第一阶段就允许任意 UObject/UI 操作，容易把线程问题外露给扩展者。应先把 payload 做成 thread-safe 只读结构，并明确哪些 reporter 允许切回 game thread。 |
| 兼容性 | 向后兼容。默认 sink/reporter 完全保持当前 `UE_LOG + Diagnostics map + modal dialog/devEnsure` 行为；新增接口只是给项目方一条正式替换路径。 |
| 验证方式 | 1. 新增一个测试 sink，验证 compile error/warning 都能收到结构化 payload，且默认日志输出仍存在。 2. 在自动化测试环境替换 exception reporter，确认不再弹 modal dialog，但错误仍能记录到 diagnostics。 3. 回归 debug server/diagnostics 流程，确认 `EmitDiagnostics()` 与默认 UI/日志输出不变。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-EP63 | 缺少 `engine/environment created-ready-shutdown` 正式扩展阶段 | 生命周期 contract 新增 | 高 |
| P2 | Arch-EP64 | compile/exception 诊断链路被硬编码到 engine 内部，缺少 pluggable sink | toolchain/diagnostic service 新增 | 中高 |

---

## 架构分析 (2026-04-10 00:19)

### Arch-EP65：插件内部已经自建多条 `host-event adapter`，但只有 `StateDump` 被产品化为外部 contract

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 外部 editor/tooling 插件能否在不修改插件源码的前提下，复用 Angelscript 已经做过的脚本文件、资产、标签、测试等宿主事件适配层 |
| 当前设计 | 当前插件内部已经把多类 UE/editor 事件翻译成 Angelscript 语义，但这些 adapter 基本都只服务第一方模块。对外公开的 runtime surface 仍主要停留在 compile/reload、literal asset、editor create blueprint；真正具备显式 `Add/Remove` 扩展合同的，当前只看到 `FAngelscriptStateDump::OnDumpExtensions`。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h:9`-`:21`、`:32`-`:47` 公开的是 compile/debug/literal asset/editor blueprint 相关 hook，没有脚本文件变化、asset database、automation run state 这类工具事件；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:359`-`:412` 在模块内部直接订阅 `IGameplayTagsModule::OnTagSettingsChanged`、`OnGameplayTagTreeChanged`、`FCoreDelegates::OnPostEngineInit`、`DirectoryWatcher` 与 `FCoreUObjectDelegates::OnObjectPreSave`，并把脚本根枚举、reload 触发、asset pre-save 全部留在私有实现里；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:2093`-`:2143` 私下监听 `AssetRegistry` 的 add/remove/rename/files-loaded 事件并直接推送 debug database 增量；`Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/AngelscriptCodeCoverage.cpp:22`-`:28` 内部监听 `AutomationController` 的 `OnTestsAvailable` / `OnTestsComplete`；对照 `Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.h:18`-`:22`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp:186`-`:188` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorStateDump.cpp:114`-`:126`，只有 dump extension 形成了显式 `delegate + handle` 的 register/unregister seam。 |
| 优点 | 第一方功能实现直接，编译链、debug server、code coverage 与 editor 模块都能就近消费宿主事件，不需要先设计一层通用抽象。 |
| 不足 | 外部扩展作者如果想感知“脚本文件变了”“调试资产数据库变了”“automation run 开始/结束了”“literal asset 即将保存”等 Angelscript 特有事件，当前只能重新订阅底层 UE delegate，并自行重复脚本根发现、资产过滤和事件归并逻辑。插件已经替自己做过一次 adapter，却没有把这层成果变成正式 public contract。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | 先把高层工作流事件提升成公共 delegate，再让 editor listener 订阅这些 delegate，并显式保存/移除 `FDelegateHandle`。`OnBeginGenerator` / `OnEndGenerator` / `OnCompile` 成了 editor 工具链的正式输入，而不是每个消费者各自去盯底层文件系统或生成器实现。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Delegate/FUnrealCSharpCoreModuleDelegates.h:10`-`:35`；`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:33`-`:40`、`:101`-`:112`、`:221`-`:228` | 先把“对工具真正有意义的高层事件”产品化，再让 listener 订阅它们，比把所有消费者都绑在底层宿主 delegate 上更稳。 |
| puerts | editor 侧不把文件观察和分析环境散在匿名全局回调里，而是由 `FPuertsEditorModule` 在 `OnPostEngineInit()` 创建 `SourceFileWatcher` 与 `JsEnv`，在 `ShutdownModule()` 中成对销毁；runtime 侧还通过 `IPuertsModule` 提供 `ReloadModule()` 等高层动作。 | `Reference/puerts/unreal/Puerts/Source/Puerts/Public/PuertsModule.h:37`-`:49`；`Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:77`-`:165` | host-event adapter 最好有明确 owner object 和成对生命周期，再决定是否继续向外暴露为 public event/action。 |
| UnLua | editor 初始化同样收口到 module-owned object：`FUnLuaEditorModule` 统一初始化 toolbar、IntelliSense 与 mainframe hook，而不是让多个子系统各自散落订阅 editor 事件。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:89`-`:105` | 即便不立即公开所有 hook，也应先把宿主事件适配层组织成可识别、可复用的 contributor/listener 对象。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把现有第一方自用的宿主事件适配层提升为 `tooling-facing event hub`，优先暴露 Angelscript 语义化后的高层事件，而不是把外部扩展重新推回底层 UE delegate。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/` 或 `AngelscriptEditor/Public/Extension/` 新增 `FAngelscriptHostEventHub` / `IAngelscriptToolingEvents`，第一阶段只定义只读事件与上下文结构，例如 `OnScriptFilesChanged(const FAngelscriptScriptFilesChangedContext&)`、`OnGameplayTagsChanged()`、`OnAssetDatabaseDelta(const FAngelscriptAssetDeltaContext&)`、`OnAutomationRunStateChanged(EAngelscriptAutomationState)`、`OnLiteralAssetPreSave(UObject*)`。 2. 把 `AngelscriptEditorModule.cpp`、`AngelscriptDebugServer.cpp`、`AngelscriptCodeCoverage.cpp` 里现有的底层订阅迁成模块私有 listener，由它们向 event hub 发布高层事件；第一方逻辑继续作为默认 subscriber 保留。 3. 复用当前 `StateDump::OnDumpExtensions` 的 handle 化模式，要求所有外部订阅都通过 `FDelegateHandle` 成对注册/移除，避免再次把无 owner 的 raw delegate 暴露出去。 4. 给每个事件的 payload 明确 `Domain(Runtime/Editor/Test)`、`Thread`、`bMayTouchUObject` 之类元信息，避免外部模块还要通过读实现猜调用约束。 5. 在 dump 中新增 `ToolingEvents.csv` 或等价审计表，列出当前启用的 host-event contributor、事件域和订阅者数量，把这条 seam 变成可观察 contract。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/AngelscriptCodeCoverage.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/AngelscriptToolingEvents.h/.cpp` |
| 预估工作量 | M |
| 架构风险 | 如果第一阶段只是把底层 UE delegate 原样转发出去，会把现有内部耦合冻结成 public ABI。应优先暴露 Angelscript 语义化后的高层 payload，而不是直接公开 `DirectoryWatcher` / `AssetRegistry` 原始事件。 |
| 兼容性 | 向后兼容。现有第一方 listener 和 `StateDump` 扩展继续工作；新增 event hub 只是把已经存在的适配层正式开放给外部模块复用。 |
| 验证方式 | 1. 新建一个外部 `Editor` 插件，只订阅新的 tooling event hub，不直接接触 `DirectoryWatcher`/`AssetRegistry`，验证能收到脚本文件变化、asset delta、automation run 状态事件。 2. 关闭和重开 editor module，确认外部订阅不会累积重复 handle。 3. 运行 dump，确认 `ToolingEvents.csv` 能列出当前注册的 listener 与事件域。 |

### Arch-EP66：扩展贡献能够进入 `BindScriptTypes()`，但“绑定图已就绪”没有正式观察/查询 contract

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 外部模块在添加自定义全局函数、类型绑定或 generated bind module 后，能否正式获知“最终哪些绑定已经生效”，并在正确阶段构建自己的二级 registry、文档或工具反馈 |
| 当前设计 | 目前 public extensibility 更偏“贡献前”而不是“生效后”。`FBind` / `RegisterBinds()`、`RegisterTypeFinder()` 允许外部把贡献塞进 bind pass，但 `BindScriptTypes()` 本身只是内部调用 `CallBinds()`；绑定完成后没有对外广播的 `OnBindingsApplied`，也没有可重放的 binding snapshot。唯一与结果接近的公开查询 `GetBindInfoList()` 只返回 `BindName`、`BindOrder`、`bEnabled`，不包含最终脚本声明、function id、来源模块或实际生效的类型/函数条目。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:431`-`:475` 中 `FBindInfo` 只有 `BindName / BindOrder / bEnabled`，而 `RegisterBinds()` / `GetBindInfoList()` 是仅面向注册前后粗粒度信息的 API；同文件 `:672`-`:680` 说明真正的 `OnBind()` 与 `CallBinds()` 仍是私有内部实现；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:151`-`:181` 只是把 bind lambda 追加到 static array 并返回粗粒度列表，`:409`-`:433` 的 `OnBind()` 仅设置 `UserData`、`Protected`、implicit property accessor 和 `PreviousBoundFunctionRef`，没有外部广播；`:588`-`:608` 的 `BindGlobalFunction()` / `BindGlobalFunctionDirect()` 虽然能立即作用于当前 engine，但生效后也没有正式 observer；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1915`-`:1941` 的 `BindScriptTypes()` 只包了一层 `CallBinds(CollectDisabledBindNames())`；对照 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h:12`-`:38`，当前公开的类生成 delegate 只覆盖 reload/class/enum/struct/delegate/literal asset 变化，没有“initial binding graph ready”事件。 |
| 优点 | 绑定热路径很短，贡献者只要在初始化前进入 `FBind`/`RegisterTypeFinder()` 即可，不必处理更多阶段对象。 |
| 不足 | 外部工具和扩展在“绑定已生效”这一刻没有正式挂接点。结果是：项目虽可不改插件源码添加自定义全局函数或类型绑定，但无法正式订阅“我的扩展现在真的进入 runtime 了”；late subscriber 也拿不到上一次 bind pass 的最终事实，只能依赖 compile 完成事件、dump 结果或再次读内部实现去推断。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 把绑定相关阶段拆成显式 hook：`OnPreStaticallyExport` 在静态导出前广播，`OnLuaStateCreated` 在 Lua state 创建后广播，而 `ObjectRegistry` 在对象真正进入 Lua registry 时再广播 `OnObjectBinded`。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDelegates.h:25`-`:49`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:144`-`:164`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ObjectRegistry.cpp:154`-`:160` | 扩展面不只需要“怎么贡献”，还需要“何时可以观察最终生效结果”。 |
| UnrealCSharp | 在运行时环境真正建好后显式广播 `OnCSharpEnvironmentInitialize`，让 registry/bind 相关消费者有稳定的 post-initialize 挂接阶段。 | `Reference/UnrealCSharp/Source/UnrealCSharp/Public/Delegate/FUnrealCSharpModuleDelegates.h:6`-`:16`；`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp:133` | 即便阶段数很少，也要给“binding/runtime environment 已可消费”一个正式 contract。 |
| puerts | 除了有 module 级 `InitExtensionMethodsMap()` 动作外，还提供 `ForeachRegisterClass()` 这类显式查询 API，允许工具链在任意时刻枚举已注册 class 定义，而不是只能押注某一次初始化广播。 | `Reference/puerts/unreal/Puerts/Source/Puerts/Public/PuertsModule.h:41`-`:49`；`Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:80`-`:120`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSClassRegister.h:142`-`:146`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JSClassRegister.cpp:282`-`:314`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:940`-`:983` | 对 late-join tooling 来说，query/action API 与一次性事件同样重要。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在不改变现有 bind 注册方式的前提下，补一层 `binding lifecycle + binding snapshot` contract，让外部模块既能等到“绑定已完成”，也能在错过事件后查询最后一次生效面。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/` 新增 `FAngelscriptBindingLifecycleEvents` 与 `FAngelscriptBindingSnapshot`，第一阶段至少提供 `OnPreApplyBindings`、`OnBindingsApplied(const FAngelscriptBindingSnapshot&)` 与 `GetLastBindingSnapshot()`。 2. 把 `FAngelscriptBinds::OnBind()` 从纯私有副作用点升级为 snapshot recorder：在保持现有 `SetUserData` / `SetProtected` 逻辑不变的前提下，额外记录 `FunctionId`、最终声明、`BindName`、`BindOrder`、绑定种类；若当前没有来源模块信息，第一阶段允许该字段为空。 3. 在 `FAngelscriptEngine::BindScriptTypes()` 前后发布 lifecycle 事件，并把本次 snapshot 缓存在 engine-scoped context 中；后续若未来实现 `RefreshBindingContributors()` / reflective rescan，也复用同一条 lifecycle。 4. 让现有 dump 直接消费 `GetLastBindingSnapshot()`，新增 `ActiveBindings.csv` 或扩充 `BindRegistrations.csv`，把“最后一次实际绑定结果”与“仅注册信息”分开。 5. 第二阶段再考虑更细的 `OnScriptObjectBound`/`OnTypeBindingApplied`；第一阶段先把 bind pass ready 做成正式 contract，避免一次性把对象生命周期也拉进来。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/AngelscriptBindingLifecycle.h/.cpp` |
| 预估工作量 | M |
| 架构风险 | 如果 snapshot 直接暴露底层 `asCScriptFunction*` 或可变内部容器，会把当前实现细节锁成 public ABI。第一阶段应只导出稳定 metadata 和 query helper，不公开可变内部对象。 |
| 兼容性 | 向后兼容。现有 `FBind`、`RegisterTypeFinder()`、`BindGlobalFunction()`、reload delegate 与 compile delegate 均保持不变；新增的只是 post-bind 观察与查询层。 |
| 验证方式 | 1. 新建一个宿主扩展模块，注册自定义全局函数和类型绑定，验证 `OnBindingsApplied` 能收到对应 snapshot 条目。 2. 在 listener 晚于初始化注册的情况下，通过 `GetLastBindingSnapshot()` 仍能看到上一轮 bind 结果。 3. 回归现有 compile/reload/bind 配置测试，确认新增 snapshot 记录不会改变实际绑定顺序与 `DisabledBindNames` 行为。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-EP65 | 宿主事件适配层仍是第一方私有实现，外部工具无法正式复用 | tooling/event hub 新增 | 高 |
| P1 | Arch-EP66 | 绑定完成后的生效面没有正式观察与查询 contract | binding lifecycle + snapshot 新增 | 高 |

---

## 架构分析 (2026-04-10 00:29)

### Arch-EP67：公共扩展 seam 缺少 `version/capability negotiation`，升级兼容只能靠源码猜测

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 外部扩展模块如何在插件升级后判断“这个宿主是否支持我依赖的扩展点、payload 形状和 dump/manifest 能力” |
| 当前设计 | 当前公开扩展面主要是 raw delegate、raw registry 和 output-dir callback。`FAngelscriptRuntimeModule` 暴露一组 `GetXxx()`；`FAngelscriptBinds` 暴露 `FBind` / `GetBindInfoList()`；`FAngelscriptType` 暴露 `Register()` / `RegisterTypeFinder()`；`FAngelscriptStateDump` 只给 `OnDumpExtensions(OutputDir)`。这些入口都没有 `ApiVersion`、`MinCompatibleVersion`、`CapabilityMask`、`SupportsXxx()` 或 schema version 字段。与此同时，插件内部其实已经在别的跨边界协议里使用版本协商，例如 debug server 会先接收 `DebugAdapterVersion`，再回发 `DebugServerVersion`。可以推断，当前缺的不是“不会做版本化”，而是“扩展点尚未被产品化成 versioned contract”。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h:9`-`:21`、`:32`-`:49` 只有 delegate 定义和 accessor；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:431`-`:474` 的 `FBindInfo` 只有 `BindName / BindOrder / bEnabled`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:71`-`:81` 只有 `Register()` 与 `RegisterTypeFinder()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.h:18`-`:22` 的扩展入口只传 `OutputDir`；对照 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h:103`-`:125` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:897`-`:907`，debug server 已经显式收发 `DebugAdapterVersion` / `DebugServerVersion`。 |
| 优点 | 对第一方快速迭代最省成本，不需要先维护一层 capability 枚举或版本常量。 |
| 不足 | 外部扩展一旦想依赖“新 provider 类”“新 dump 表”“新 snapshot 字段”或未来的 `Public/Extension` façade，就只能用 include 是否存在、字段是否能编译通过、运行时是否偶然崩溃来反推兼容性。对长期维护的第三方扩展包来说，这不是正式 contract。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 对真正跨模块/跨二进制的扩展直接做版本校验。加载 addon 时先找 `PESAPI_MODULE_VERSION` 导出，缺失或不匹配就明确报错；同时模块加载策略本身收口到 `IJSModuleLoader` 这种命名接口。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PesapiAddonLoad.cpp:113`-`:125`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSModuleLoader.h:17`-`:44` | 即使不是所有 seam 都做 runtime handshake，至少真正对外的扩展 contract 要有“我支持什么版本/能力”的明确边界。 |
| UnrealCSharp | 不直接让外部模块盯具体实现细节，而是把加载扩展能力收口到 `UAssemblyLoader` 这类命名 provider，并通过 settings 选择 provider class。扩展者依赖的是稳定抽象层，而不是一串 concrete header。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Domain/AssemblyLoader.h:12`-`:18`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpSetting.h:104`-`:142`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Delegate/FUnrealCSharpCoreModuleDelegates.h:7`-`:35` | 不一定所有地方都用整数版本号；把 seam 收口成命名 interface/provider，本身就是减少兼容猜测成本的第一步。 |
| UnLua | runtime hook 与 loader seam 都集中在显式 public contract 上，例如 `FUnLuaDelegates` 与 `CustomLoadLuaFile`。宿主示例工程直接通过这些 contract 绑定/解绑自定义 loader，而不是去猜内部哪个 module 正在读哪个 raw callback。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDelegates.h:22`-`:49`；`Reference/UnLua/Source/TPSProject/TutorialBlueprintFunctionLibrary.cpp:96`-`:102` | named contract 虽不等于 version negotiation，但它给后续 capability/version 字段留下了稳定载体。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留现有 raw seam 的前提下，补一层统一 `extension API descriptor`，让外部扩展先做能力探测，再决定是否注册。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/` 新增 `AngelscriptExtensionApi.h`，定义 `FAngelscriptExtensionApiInfo`，最小字段建议包含 `ApiVersion`、`MinCompatibleVersion`、`CapabilityMask`、`DumpSchemaVersion`、`bSupportsProviderClasses`、`bSupportsBindingSnapshot`。 2. 在 `FAngelscriptRuntimeModule` 或新的 `ExtensionRegistry` 上新增 `GetExtensionApiInfo()`，并把未来正式支持的 seam 映射为 `EAngelscriptExtensionCapability`，例如 `BindContributor`、`TypeProvider`、`CompileObserver`、`ImportResolver`、`BindingSnapshot`、`ToolingEvents`、`DumpManifestV1`。 3. 让新的 provider/contributor manifest 可选声明 `RequiredApiVersion` 与 `RequiredCapabilities`；若宿主不满足，则在启动期给出明确 warning 并跳过注册，而不是让扩展继续走半兼容路径。 4. 把 `FAngelscriptStateDump`、未来 `Public/Extension` façade 和 editor 设置校验统一接到这份 descriptor 上，避免 capability 信息分散在文档、代码和 dump 三处。 5. 旧 `FBind` / `RegisterTypeFinder()` / raw delegate 继续保留，但在 dump 中标记为 `LegacyUnversionedSurface`，让新增扩展优先迁移到 versioned contract。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/AngelscriptExtensionApi.h/.cpp` |
| 预估工作量 | M |
| 架构风险 | 如果 capability 定义和真实实现不同步，会制造“自称支持但实际不能用”的伪安全感。第一阶段必须只声明已经稳定、已有测试覆盖的 seam，不要把实验性 raw hook 一起纳入稳定 capability 集。 |
| 兼容性 | 向后兼容。旧扩展不需要立刻声明版本；新增 descriptor 只为新扩展提供探测与校验能力。legacy raw 路径继续可用，但会在审计输出中被标成未版本化。 |
| 验证方式 | 1. 新增一个测试扩展，声明不存在的 `RequiredCapabilities`，验证宿主会明确 warning 并拒绝注册。 2. 新增一条回归，验证 `GetExtensionApiInfo()` 在默认项目中始终可读，且 dump 能输出相同版本/能力集合。 3. 保留一个 legacy `FBind` 扩展测试，确认未声明版本的旧路径仍按当前行为工作。 |

### Arch-EP68：扩展可观测面仍停留在无 schema 的 `CSV dump`，外部工具无法把它当正式 machine-readable contract

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 外部 IDE、诊断工具或第三方扩展能否稳定消费 `StateDump`/extension audit 这类产物，而不是只把它们当人工排查日志 |
| 当前设计 | `FAngelscriptStateDump::DumpAll()` 当前是“固定 CSV 集合 + extension callback 写额外 CSV”。核心 runtime 先写一批固定表，再广播 `OnDumpExtensions(OutputDir)`；随后用硬编码文件名去推断扩展表是否生成，例如 `EditorReloadState.csv`、`EditorMenuExtensions.csv`。扩展侧也只是拿到一个目录字符串，自行落文件，没有表 schema、自描述元数据、producer identity 或 manifest 汇总。测试层验证的也是“这些 CSV 文件在不在、Summary 状态对不对”，而不是“schema 是否可机器消费”。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.h:18`-`:22` 只把扩展入口定义成 `TMulticastDelegate<void(const FString&)>`；`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp:148`-`:224` 先固定写 runtime CSV，再 `OnDumpExtensions.Broadcast(ResolvedOutputDir)`，最后仅用 `EditorReloadState.csv` / `EditorMenuExtensions.csv` 这类文件名回填结果；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorStateDump.cpp:114`-`:126` 说明 editor 扩展只是 `AddStatic(&DumpEditorState)`，没有注册 schema/producer 信息；`Plugins/Angelscript/Source/AngelscriptTest/Dump/AngelscriptDumpTests.cpp:45`-`:75` 与 `:210`-`:247` 只校验期望 CSV 名称和 `DumpSummary.csv` 状态。 |
| 优点 | 人工排查非常直接，生成成本低；对当前第一方开发流程来说，CSV 足够读。 |
| 不足 | 一旦这些 dump 开始承担“public extension audit”“settings 候选源”“IDE 索引输入”之类职责，就会遇到 schema 漂移问题。外部工具既不知道某张表由哪个模块生成，也不知道列集合何时变化，更无法判断自己当前解析的是哪一版 contract。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | 生成期和消费期共享同一份 machine-readable manifest。构建脚本把模块信息写入 `Intermediate/UnrealCSharp_Modules.json`，运行时再从同一路径读回 `ProjectModules` / `EngineModules` / plugin 列表。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/UnrealCSharpCore.build.cs:131`-`:176`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp:1259`-`:1326` | 只要产物需要被下一阶段消费，就应先给它一个结构化 manifest，而不是只输出人读 CSV。 |
| puerts | 类型声明文件在写出时就带 `FileVersionString`，解析时再读回这个版本字符串决定如何处理声明块。版本信息是产物本体的一部分，不依赖外部约定。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:438`-`:444`；`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:570`-`:582` | 如果一份扩展产物要跨阶段、跨工具传递，schema/version 应嵌入产物，而不是只留在文档描述里。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 保留现有 CSV 作为人工可读视图，但新增统一的 `ExtensionSurfaceManifest.json`，把扩展 dump 变成可版本化的 machine-readable contract。 |
| 具体步骤 | 1. 在 `Dump/` 新增 `FAngelscriptDumpManifestEntry` 与 `ExtensionSurfaceManifest.json`，最小字段建议包含 `SchemaVersion`、`ExtensionApiVersion`、`GeneratedAt`、`Tables[]`、每表的 `Name`、`ProducerModule`、`TableSchemaVersion`、`Columns`、`Semantics`、`bOptional`。 2. 把 `OnDumpExtensions` 从“只传 output dir”升级为上下文对象，例如 `FAngelscriptDumpExtensionContext`；上下文既提供 `OutputDir`，也提供 `RegisterProducedTable(...)` 帮助函数。旧 `void(const FString&)` 形式先保留为 legacy adapter，并自动登记 `ProducerModule=UnknownLegacy`。 3. 让 `DumpAll()` 不再硬编码 `EditorReloadState.csv` / `EditorMenuExtensions.csv` 这类 extension 表名，而是统一从 manifest entry 汇总 `FTableResult`；`DumpSummary.csv` 继续存在，但改为引用 manifest 里的表集合。 4. 把 extension audit、未来 provider/capability 审计和 settings 候选生成都改为优先消费这份 manifest，而不是继续各自扫目录或猜 CSV 名。 5. 在 dump 测试里新增 manifest 解析回归，要求每张 CSV 都能在 manifest 中找到对应 schema/version/producer；未来新增表时先改 manifest，再改 CSV。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorStateDump.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Dump/AngelscriptDumpTests.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptDumpManifest.h/.cpp` |
| 预估工作量 | S-M |
| 架构风险 | manifest 与实际 CSV 内容若由两套代码分别维护，容易再次漂移。第一阶段应让 `SaveTable()` 或注册 helper 成为单一事实来源，尽量避免手写重复列描述。 |
| 兼容性 | 向后兼容。现有 CSV 文件名和 `DumpSummary.csv` 保持不变；新 JSON manifest 只是额外产物，legacy extension dumper 仍可继续写 CSV。 |
| 验证方式 | 1. 运行 `DumpAll`，验证 `ExtensionSurfaceManifest.json` 中列出的表集合与实际输出 CSV 完全一致。 2. 保留一个 legacy `OnDumpExtensions` handler，确认它仍可工作，但 manifest 会把它标为 `UnknownLegacy` producer。 3. 新增一条 schema-version 回归：当某张表列集合变化时，测试要求同步更新 manifest 中的 `TableSchemaVersion`。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-EP67 | 扩展 API 没有版本/能力协商，升级兼容只能靠源码猜测 | versioned contract 新增 | 高 |
| P2 | Arch-EP68 | 扩展可观测产物仍是无 schema 的 CSV 集合，难以支撑外部工具 | machine-readable manifest 新增 | 中高 |

---

## 架构分析 (2026-04-10 00:40)

### Arch-EP69：脚本根发现仍是 `Plugin/Script` 目录约定，不是可替换的 locator contract

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 宿主项目或扩展插件如何在不改插件源码的前提下，改变脚本搜索根、覆盖顺序与非标准脚本来源 |
| 当前设计 | 当前正式入口不是 `settings/provider class`，而是 `FAngelscriptEngineDependencies` 里的 `GetEnabledPluginScriptRoots` lambda。默认实现把所有 `GetEnabledPluginsWithContent()` 的 `BaseDir/Script` 收进来，`DiscoverScriptRoots()` 再按字符串排序 plugin roots、把项目 `Script/` 固定插到首位，`InitialCompile()` 对这些物理目录做递归扫描。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:86`-`:94` 仅把脚本来源抽象成 `GetProjectDir`/`GetEnabledPluginScriptRoots` 等函数指针；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:539`-`:567` 默认实现直接遍历 `IPluginManager::Get().GetEnabledPluginsWithContent()` 并拼接 `Script` 子目录；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1326`-`:1363` 把 plugin roots 排序后再把 project root 插到索引 `0`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1999`-`:2015` 与 `:2061`-`:2078` 说明编译期就是对 `AllRootPaths` 全量递归扫描；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h:41`-`:224` 的 public settings 全是行为 flag，没有 script locator / root provider / mount policy 字段。 |
| 优点 | 约定非常简单。只要是 enabled plugin 且带 content，放一个 `Script/` 目录就能自动进入脚本扫描，无需额外注册代码。 |
| 不足 | 扩展者不能只通过 `ini/settings` 声明自定义 root order、额外挂载目录、虚拟逻辑根、远端/生成目录、按 engine instance 切换脚本源，也不能把“哪些 roots 生效”做成正式 provider。想突破 `Plugin/Script` 约定，目前仍要自己构造 `FAngelscriptEngineDependencies` 或改插件源码。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 把脚本定位器做成 settings 驱动的 provider class。`UUnLuaSettings` 公开 `EnvLocatorClass` 与 `ModuleLocatorClass`，`LuaEnv` 初始化时直接取 `Settings->ModuleLocatorClass.GetDefaultObject()`；当默认定位仍不够时，再用 `FUnLuaDelegates::CustomLoadLuaFile` 接管最终文件加载。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaSettings.h:47`-`:56`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:78`-`:81`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDelegates.h:33`-`:49`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:560`-`:566` | “发现脚本”与“读取脚本”都被提升成命名 contract，宿主只靠配置或绑定 delegate 就能换 locator / loader。 |
| puerts | `FJsEnv` 构造函数直接接收 `std::shared_ptr<IJSModuleLoader>`，而 `IJSModuleLoader` 抽象出 `Search()` / `Load()` / `GetScriptRoot()`；默认 loader 的 `CheckExists()` / `SearchModuleInDir()` 仍保持 `virtual`，允许宿主替换搜索策略。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JsEnv.h:64`-`:68`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSModuleLoader.h:17`-`:25`、`:31`-`:50`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DefaultJSModuleLoader.cpp:53`-`:65`、`:92`-`:123` | 把“脚本从哪里来”收口成 strategy object，而不是固定目录拼接。 |
| UnrealCSharp | 即便不是脚本文件加载器，也把运行时载体和绑定来源提升成 settings provider。`UUnrealCSharpSetting` 公开 `AssemblyLoader` 与 `BindClass`，让宿主能声明由谁负责加载 domain、暴露哪些绑定类。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpSetting.h:140`-`:144` | provider class + declarative bind source 的组合，比“硬编码目录约定”更容易演进成正式扩展面。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留现有 `Project/Script + EnabledPlugin/Script` 默认行为的前提下，引入 `script root provider`，把物理目录约定升级为可替换的定位协议。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/` 新增 `UAngelscriptScriptRootProvider` 或 `IAngelscriptScriptRootProvider`，返回 `FAngelscriptScriptRootDesc` 列表，最少包含 `PhysicalPath`、`LogicalRoot`、`Priority`、`bWatchForChanges`。 2. 在 `UAngelscriptSettings` 增加 `TSubclassOf<UAngelscriptScriptRootProvider>`，默认 provider 完全复用现有 `GetProjectDir + GetEnabledPluginScriptRoots + sort + project-root-first` 逻辑。 3. 在 `FAngelscriptEngineDependencies` 中保留 `GetEnabledPluginScriptRoots` 作为 legacy adapter，但优先走 provider；测试和特殊 commandlet 仍可继续直接注入 dependencies。 4. 让 `InitialCompile()` 与 hot reload watcher 消费统一的 root descriptor，而不是直接扫描裸 `FString` 路径；同时把最终生效 roots 写入 `StateDump`，便于排查覆盖顺序。 5. 为 provider 增加最小宿主示例与自动化测试，验证外部插件可挂载非 `Plugin/Script` 目录且不需要改 `Plugins/Angelscript/` 源码。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/AngelscriptScriptRootProvider.h/.cpp` |
| 预估工作量 | M |
| 架构风险 | root order 一旦变化，会直接影响同名模块覆盖关系与 hot reload 观察集合。第一阶段必须把默认 provider 做成现状的逐字复刻，并用 dump 把实际顺序打出来。 |
| 兼容性 | 向后兼容。未配置 provider 时，现有 `Project/Script`、`EnabledPlugin/Script`、project-root-first 和排序规则全部保持不变；`FAngelscriptEngineDependencies` 的测试注入路径继续保留。 |
| 验证方式 | 1. 新增一个宿主测试 provider，把外部临时目录挂进 roots，验证 `InitialCompile()` 能编译其中脚本。 2. 构造 project 与 plugin 同名模块，验证默认 provider 下覆盖顺序与当前行为一致。 3. 运行 dump，确认 `ActiveScriptRoots` 或同类表能准确反映 provider 输出顺序。 |

### Arch-EP70：现有“给已有类型加方法”能力是 `ExistingClass(\"TypeName\")` 专家接口，缺少类似 puerts 的正式 `extension methods` 协议

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 用户在不改插件源码的前提下，如何给已有 `UClass/UStruct` 增加 type-scoped helper，而不是继续污染 global function 空间 |
| 当前设计 | 运行时其实已经有一条低层 method-augmentation seam：public 的 `FAngelscriptBinds::ExistingClass(FBindString Name)` 配合 `Method()`，可以在 bind phase 对已存在的 AngelScript 类型继续注册 object methods。问题在于这个 seam 只认 script type name 字符串，且必须和 `CallBinds()` 的执行时序绑定；它不是基于 `UClass/UStruct/UFunction` 的可扫描 contract。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:138`-`:139` 只公开 `ReferenceClass(FBindString Name, UClass* UnrealClass)` 与 `ExistingClass(FBindString Name)`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:169`-`:249` 说明追加方法的主要入口仍是 `Method(FBindString Signature, ...)`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:229`-`:231` 表明 `ExistingClass()` 只是用字符串名字包装 `FAngelscriptBinds(Name)`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:449`-`:459` 通过 `RegisterObjectMethod(ClassName.ToCString(), ...)` 直接按 script-visible class name 绑定；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AssetRegistry.cpp:20`-`:30`、`:32`-`:55` 展示了第一方就是这样给 `FAssetData`、`FTopLevelAssetPath` 补方法；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1915`-`:1921` 说明这些扩展只会在 `BindScriptTypes()` 的 `CallBinds()` 阶段生效；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:431`-`:435` 的 `FBindInfo` 只有 `BindName/BindOrder/bEnabled`，没有目标类型或 extension-class 维度。 |
| 优点 | 功能上足够强。高级用户已经可以在宿主 `C++ module` 里，不改插件源码就给现有类型补方法。 |
| 不足 | 这是一条专家模式接口，不是正式扩展协议。扩展者必须知道正确的 AngelScript 类型名、自己处理 bind order、自己决定是否与现有方法冲突，也拿不到“这些方法来自哪个 extension class / 哪个 module”的统一元数据。更关键的是，这条 seam 不会像 puerts 那样自然进入声明生成或反射扫描流程。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 定义专用标记类 `UExtensionMethods`，运行时 `InitExtensionMethodsMap()` 扫描所有 native `UExtensionMethods` 子类，用“静态函数首参类型”决定目标 `UStruct`；同一份 `ExtensionMethodsMap` 既被 runtime `StructWrapper->AddExtensionMethods()` 消费，也被 TypeScript declaration generator 复用来生成声明。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/ExtensionMethods.h:18`-`:20`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:940`-`:975`、`:3059`-`:3065`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:340`-`:348`；`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:315`-`:352`、`:1157`-`:1166` | type-scoped 扩展不必暴露底层 bind 字符串；只要有一个显式 marker 和目标类型推导协议，就能同时服务运行时与工具链。 |
| UnrealCSharp | 虽然不是“扩展方法”模型，但绑定来源与导出范围至少是 declarative 的。`bEnableExport`、`ExportModule`、`ClassBlacklist` 让扩展者明确声明哪些 module/class 参与导出，而不是仅靠 include 头文件和字符串约定。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpEditorSetting.h:139`-`:148` | 即便保留低层 API，也应给上层扩展者一个配置化、可审计的 contributor surface。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留 `ExistingClass()` 原始能力的同时，新增一层 `extension methods registry`，把“给已有类型加方法”从字符串 bind 协议升级为 `UFunction`/`UStruct` 驱动的正式 contract。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/` 新增 `UAngelscriptExtensionMethods`（建议继承 `UBlueprintFunctionLibrary`）和 `FAngelscriptExtensionMethodRegistry`。规则与 puerts 类似：只扫描 static `UFunction`，用首个参数的 `UClass/UScriptStruct` 决定 receiver type。 2. registry 内部仍复用现有 `FAngelscriptBinds::ExistingClass(...).Method(...)` 作为后端，避免第一阶段重写 binder；但对外暴露的是 `RegisterExtensionMethods(UClass* ExtensionClass)` 或自动扫描入口，而不是 `FBindString`。 3. 复用现有 `Helper_FunctionSignature` 生成 script-visible method 名、namespace 和 metadata，避免另一套签名规则。 4. 扩充 dump / manifest，把每个 extension method 记录为 `TargetType`、`SourceClass`、`SourceModule`、`BindName`、`ConflictPolicy`；同时在 settings 中补一个最小 allowlist/denylist（按 module 或 extension class）作为产品化入口。 5. `ExistingClass()` 与 raw `Method()` 保持 legacy 支持，但标记为 low-level surface；新增宿主示例优先示范 `UAngelscriptExtensionMethods`。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/AngelscriptExtensionMethods.h/.cpp` |
| 预估工作量 | M |
| 架构风险 | 首参推导 receiver 时，如果直接复用所有 static `UFunction`，容易把不该暴露的 helper 误扫进来。第一阶段必须要求显式 marker base class 或 metadata，不要对全项目 `UBlueprintFunctionLibrary` 做全量扫描。 |
| 兼容性 | 向后兼容。现有 `ExistingClass()`、`FBind` 和手写 `Bind_*.cpp` 不需要迁移即可继续工作；新 registry 只是补一层更高层、可审计的 authoring model。 |
| 验证方式 | 1. 在宿主测试模块里新增一个 `UAngelscriptExtensionMethods` 子类，为 `FAssetData` 或 `AActor` 增加静态扩展方法，验证脚本侧能以 instance-method 形式调用。 2. 同时验证 dump/manifest 能显示 `TargetType` 与 `SourceClass`，而不是只有 `BindName`。 3. 保留一个 legacy `ExistingClass(\"FAssetData\")` 测试，确认新 registry 不改变旧 bind 时序和方法解析。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-EP69 | 脚本根发现仍依赖 `Plugin/Script` 目录约定，缺少可替换 locator/provider | 扩展点新增 | 高 |
| P1 | Arch-EP70 | 给已有类型加方法仍是 string-based `ExistingClass()` 专家接口 | 扩展点显式化 | 高 |

---

## 架构分析 (2026-04-10 00:48)

### Arch-EP71：穷举 `DECLARE_*` / `OnXxx` 后，public extension hook 与私有回调没有命名边界，扩展发现结果被 grep 噪音污染

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 在按要求穷举 `DECLARE_MULTICAST_DELEGATE` / `DECLARE_DELEGATE` / `OnXxx` 后，扩展作者能否直接区分“正式 public hook”与“插件内部监听实现” |
| 当前设计 | 这次实际源码搜索显示，真正对外暴露的 hook carrier 主要集中在四处：`FAngelscriptRuntimeModule`、`FAngelscriptClassGenerator`、`FAngelscriptPreprocessor`、`FAngelscriptStateDump`。但插件内部同样大量使用 `OnXxx` 命名的 file-local / feature-local callback，例如 editor 里的 `OnScriptFileChanges`、`OnEngineInitDone`、`OnLiteralAssetSaved`，以及 code coverage 里的 `OnTestsStarting` / `OnTestsStopping`。结果是“搜索所有事件”时，public contract 与 private plumbing 会混在同一结果集里。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h:9`-`:21`、`:32`-`:47` 定义并暴露 compile/debug/editor hook；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h:12`-`:19`、`:31`-`:38` 暴露 reload hook；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h:8`、`:29`-`:30` 暴露 preprocess hook；`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.h:18`-`:22` 暴露 dump hook。对照 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:78`-`:94`、`:111`-`:123`、`:339`-`:349` 的 `OnScriptFileChanges` / `OnEngineInitDone` / `OnLiteralAssetSaved` / `OnLiteralAssetPreSave`，以及 `Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/AngelscriptCodeCoverage.cpp:22`-`:39` 的 `OnTestsStarting` / `OnTestsStopping`，同一命名风格被同时用于 public hook 与 private handler。 |
| 优点 | 对第一方实现来说成本很低，新增监听逻辑时不必先设计 facade，也能快速复用 UE 原生 delegate 习惯。 |
| 不足 | `OnXxx` 搜索结果会高频命中私有实现细节，扩展作者很难只靠 grep 判断“这个事件是否被正式支持”。这会直接抬高 discoverability 成本，也让文档、样例和回归测试更难围绕一条稳定 public boundary 组织。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | 把正式扩展阶段集中定义在 `FUnrealCSharpCoreModuleDelegates` 一个 public header 中，`OnBeginGenerator` / `OnEndGenerator` / `OnCompile` 都先经过这个统一 carrier，再由外部 listener 订阅。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Delegate/FUnrealCSharpCoreModuleDelegates.h:7`-`:35` | 先集中命名 public hook，再让具体 listener 自己实现成员函数，可以显著减少 grep 噪音。 |
| UnLua | 把 lifecycle / object-binding / loader hook 集中收口到 `FUnLuaDelegates`，例如 `OnLuaStateCreated`、`OnObjectBinded`、`CustomLoadLuaFile`；运行时 `LuaEnv` 只是消费这些公开 contract。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDelegates.h:22`-`:49`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:77`-`:81`、`:557`-`:567` | public delegate header 与内部运行时逻辑有清晰边界，扩展者不需要先过滤大量私有 `On...` 回调。 |
| puerts | 不把扩展入口散落成若干 `OnXxx` 命名，而是把 module action 收口到 `IPuertsModule`，把脚本加载 contract 收口到 `IJSModuleLoader`。 | `Reference/puerts/unreal/Puerts/Source/Puerts/Public/PuertsModule.h:17`-`:49`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSModuleLoader.h:17`-`:50`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JsEnv.h:64`-`:68` | 当 extensibility 入口由命名 interface 承担时，public/private 边界会比“全仓库搜 `OnXxx`”稳定得多。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先给 public hook 建立单一入口与命名保留区，再逐步清理私有 `OnXxx` 命名噪音；第一阶段不改任何实际触发时机。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/` 新增 `AngelscriptExtensionHooks.h` 或等价 facade，统一 re-export 当前正式支持的 compile/reload/preprocess/dump/editor hook，并按 `Observer` / `Strategy` / `Tooling` 分类。 2. 把 `AngelscriptEditorModule.cpp`、`AngelscriptCodeCoverage.cpp` 这类内部 file-local callback 重命名为 `HandleScriptFileChanges`、`HandlePostEngineInit`、`HandleLiteralAssetSaved`、`HandleTestsStarting`、`HandleTestsStopping` 等 private handler 名称，避免继续与 public hook 混名。 3. 新增一个轻量审计测试或 dump 表，只枚举 canonical header 中的 public hook，要求以后新增对外扩展点时必须先进入该入口。 4. 在扩展文档中把“扩展事件搜索入口”改成 canonical header 路径，而不是继续建议全仓库搜索 `OnXxx`。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.h`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/AngelscriptCodeCoverage.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/AngelscriptExtensionHooks.h` |
| 预估工作量 | S-M |
| 架构风险 | 如果第一阶段直接移动或删除现有 carrier，会打断现有 include 路径。应先做 re-export 和私有重命名，再考虑更深的 facade 收口。 |
| 兼容性 | 向后兼容。现有 delegate carrier 与 include 路径全部保留；内部 `OnXxx` 重命名只影响私有实现，不影响宿主扩展代码。 |
| 验证方式 | 1. 再次运行 `rg "DECLARE_(MULTICAST_)?DELEGATE|\\bOn[A-Z]"`，确认 public hook 能从 canonical header 一次定位，而私有 handler 不再与 public hook 混名。 2. 编译 runtime/editor 模块，确认内部重命名不影响现有行为。 3. 若新增审计表，验证其输出集合与 canonical header 完全一致。 |

### Arch-EP72：穷举 `virtual` 后，真正的 service-level subclass seam 仍是“继承 + side-channel 激活”双阶段协议

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 在按要求穷举所有 `virtual` 后，用户子类化这些基类时，能否只靠“继承一个类”就真正接入插件核心服务 |
| 当前设计 | 这次 `virtual` 搜索显示，真正影响 plugin core extensibility 的 service-level vtable 主要集中在 `FAngelscriptType` 与 `FScriptFunctionNativeForm` 两族。问题不在于它们不能继承，而在于“继承本身不会自动生效”：`FAngelscriptType` 子类必须再走 `Register()` / `RegisterTypeFinder()` 写入 type database；`FScriptFunctionNativeForm` 子类必须再通过 `BindNativeFunction()` / `BindNativeFunctionHeader()` / `BindUFunction()` 把实例塞进 `GScriptNativeForms`，而 key 还是 `FAngelscriptBinds::GetPreviousBind()`。因此 `virtual` 搜索给出的“可子类化表面积”明显大于真实“可接入 contract”。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:71`-`:81` 先要求显式 `Register()` / `RegisterTypeFinder()`，`:96`-`:161` 才是大段 `virtual` 语义；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:54`-`:118`、`:147`-`:168` 说明 type 子类只有写入数据库或 finder 列表后才会被查询到。`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.h:25`-`:49` 把 lowering 能力定义成 `FScriptFunctionNativeForm` 的 `virtual` 接口；但 `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.cpp:27`-`:60` 用的是 process-global `GScriptNativeForms`，并在 `:401`-`:405`、`:439`-`:443`、`:530`-`:534` 里通过 `FAngelscriptBinds::GetPreviousBind()` 完成 side-channel 注册。对照 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:299`-`:305`，这个“前一个 bind”本身又依赖 bind 时序。 |
| 优点 | 这套 expert seam 非常强，内核作者几乎可以覆盖类型系统、参数编组和 `StaticJIT` lowering 的所有细节。 |
| 不足 | 子类化不是完整 contract，真正起作用的是“继承 + 再找对激活 side channel”。这让扩展作者即使搜到了正确的 `virtual` 基类，也仍需阅读内部注册路径、全局 map 和 bind 时序，才能把自定义实现真正接入运行时。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `ULuaModuleLocator` 是明确可子类化的 strategy，真正的激活路径也同样显式：`UUnLuaSettings` 持有 `ModuleLocatorClass`，`FLuaEnv` 启动时直接取 `GetDefaultObject()`。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/LuaModuleLocator.h:20`-`:33`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:77`-`:79` | “继承什么”与“怎样激活它”在同一条 public contract 上，不需要额外猜 side channel。 |
| UnrealCSharp | `UAssemblyLoader::Load()` 是公开 `virtual` seam，而 `UUnrealCSharpSetting` 直接用 `TSubclassOf<UAssemblyLoader>` 选择实现。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Domain/AssemblyLoader.h:12`-`:18`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpSetting.h:140`-`:144` | 子类化只有在 activation route 同样公开时，才会成为真正可用的扩展点。 |
| puerts | `IJSModuleLoader` 把可替换行为直接定义为 interface，`FJsEnv` 构造函数显式接收该 loader；module 侧再通过 `IPuertsModule` 暴露其它动作。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSModuleLoader.h:17`-`:50`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JsEnv.h:64`-`:68`；`Reference/puerts/unreal/Puerts/Source/Puerts/Public/PuertsModule.h:17`-`:45` | 真正成熟的 subclass seam 需要配套的 constructor / settings / module API 激活路径，而不是默认 inert。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 不先废弃现有 expert vtable，而是先把“如何激活这些子类”产品化成显式 registry/activation contract，让子类化从隐式 side channel 变成正式入口。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/` 新增 `AngelscriptExtensionActivation.h` 或等价 registry，先覆盖两类高价值 seam：`FAngelscriptType` 与 `FScriptFunctionNativeForm`。 2. 为类型扩展增加显式激活 API，例如 `RegisterTypeContribution(SourceModule, Type)`、`RegisterTypeFinder(SourceModule, Finder)`；对 `StaticJIT` lowering 增加显式 `RegisterNativeFormContribution(SourceModule, StableKey, NativeForm)`，把 `GetPreviousBind()` 收缩成 legacy wrapper，而不是继续作为外部作者必须知道的知识。 3. 在 registry 中记录 `SourceModule`、`ContributionKind`、`StableKey`、`ActivationTime`，并把这些元数据输出到 dump/manifest，避免扩展者只能靠断点或 grep 追踪自己子类是否真正生效。 4. 在头文件注释与扩展文档中明确区分“supported expert seam”和“internal-only virtuals”，减少 `virtual` 搜索带来的误导。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/AngelscriptExtensionActivation.h/.cpp` |
| 预估工作量 | M |
| 架构风险 | 若第一阶段试图一次性替换掉现有 `Register()`、`RegisterTypeFinder()` 与 `GetPreviousBind()` 路径，容易引入 bind 顺序与 `StaticJIT` 命中回归。应先让新 registry 作为 façade，旧路径只变成 legacy adapter。 |
| 兼容性 | 向后兼容。现有 `FAngelscriptType::Register()`、`RegisterTypeFinder()`、`FScriptFunctionNativeForm::BindNativeFunction()` / `BindUFunction()` 继续保留；新 activation registry 只是把原本隐式的激活路径显式化并带上来源元数据。 |
| 验证方式 | 1. 新建一个宿主测试模块，定义自定义 `FAngelscriptType` 子类和自定义 `FScriptFunctionNativeForm` 子类，只通过新 activation API 激活，验证无需再依赖隐式 side channel。 2. 保留一条 legacy `Register()` / `BindNativeFunction()` 路径测试，确认新 façade 不改变旧行为。 3. 运行 dump，确认能看到每个 expert seam 的来源模块与激活方式。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-EP72 | `virtual` seam 仍依赖隐式 side-channel 激活，子类化本身不是正式扩展入口 | 激活合同显式化 | 高 |
| P2 | Arch-EP71 | public hook 与私有 `OnXxx` 回调缺少命名边界，扩展发现成本被 grep 噪音放大 | public boundary 收口 | 中高 |

---

## 架构分析 (2026-04-10 00:58)

### Arch-EP73：`ini/settings` 已覆盖大量行为开关，但它们仍是 `toggle bank`，不是可装配的扩展 provider surface

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `ini/settings` 是否只是调整内建行为参数，还是已经能装配用户自定义的 `resolver/locator/contributor/observer` |
| 当前设计 | `UAngelscriptSettings` 与 `FAngelscriptEngineConfig` 已暴露大量 `config` 面，覆盖预处理、bind 灰度、命名兼容、warning/debugger 语义；但这些配置项本质上都是 `bool/enum/float/TArray/TSet`。运行时各模块直接读取这些值并切换内建逻辑，没有任何 `TSubclassOf`、`FSoftClassPath` 或 provider 列表可让用户通过配置接入自定义策略对象。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h:41`-`:225` 的配置项全部是 `bool` / `enum` / `float` / `TArray` / `TSet`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:64`-`:95` 的 `FAngelscriptEngineConfig` 也只有 command-line flags、`DebugServerPort` 与 `DisabledBindNames`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1291`-`:1296` 在初始化时把 settings 直接复制到 `bUseAutomaticImportMethod` 与 namespace strip 全局状态；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1928`-`:1941` 只把 `DisabledBindNames` 合并进 bind 过滤；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:485`-`:490`、`:523`-`:527` 直接按 settings 改写 warning 与 `float` 语义；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Primitives.cpp:610`-`:625` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:1499`-`:1504` 继续直接消费这些配置值。 |
| 优点 | 现有 `config` 面已经足够细，宿主项目可以在不写额外代码的情况下快速调整兼容模式、警告级别、bind 黑名单和 debugger 行为。 |
| 不足 | 这套设置面只能 `gate built-in behavior`，不能装配“由谁实现该行为”。脚本开发者想把自定义 `import resolver`、script-root locator、bind contributor、compile observer 产品化给项目组使用，仍然只能写宿主 `C++ module` 直接碰底层 API，甚至改插件源码。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | settings 直接声明 `TSubclassOf<UAssemblyLoader> AssemblyLoader` 和 `TArray<FBindClass> BindClass`，编辑器侧还用 `bEnableExport`、`ExportModule`、`ClassBlacklist` 控制导出范围；运行时通过 `GetAssemblyLoader()->Load()` 执行自定义加载策略。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpSetting.h:104`-`:145`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Setting/UnrealCSharpSetting.cpp:87`-`:93`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp:500`-`:508`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpEditorSetting.h:140`-`:148` | settings 不只是开关，还能选择 strategy class 和 contributor set。 |
| UnLua | `UUnLuaSettings` 公开 `EnvLocatorClass`、`ModuleLocatorClass`、`PreBindClasses`；`FLuaEnv` 构造时直接从 settings 取 `ModuleLocatorClass.GetDefaultObject()`，把模块定位策略和预绑定范围正式纳入配置面。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaSettings.h:47`-`:56`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:77`-`:79` | 让宿主通过配置替换定位/预绑定策略，而不是只改若干布尔值。 |
| puerts | `FJsEnv` 构造函数可直接接收 `std::shared_ptr<IJSModuleLoader>`，默认 loader 的 `Search/Load/CheckExists` 又是 `virtual`。扩展面首先是独立 strategy object，其次才是默认配置。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSModuleLoader.h:17`-`:50`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JsEnv.h:64`-`:68`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnv.cpp:14`-`:25` | 即便不走 `ini`，也把“策略实现者”建模成显式对象，而不是 flag 组合。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 保留现有 flags 语义，同时给 `settings/config` 增加 class-based provider slots，把配置从 `toggle bank` 升级成真正的装配面。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/` 新增最小 provider contract，例如 `UAngelscriptImportResolver`、`UAngelscriptScriptRootLocator`、`UAngelscriptBindContributor`、`UAngelscriptCompileObserver`。 2. 在 `UAngelscriptSettings` 增加 `TSoftClassPtr` / `TArray<FSoftClassPath>` 配置项，例如 `ImportResolverClass`、`ScriptRootLocatorClass`、`BindContributorClasses`、`CompileObserverClasses`；默认全部为空，保持当前默认行为。 3. 在 `FAngelscriptEngine::PreInitialize_GameThread()` 与 preprocessor 启动路径中解析这些 provider，并注册到新的 extension registry；现有 `bAutomaticImports`、`DisabledBindNames` 等 flags 继续作为默认 provider 的参数，而不是被删除。 4. 在 `Dump` 中追加 `ExtensionProviders` 表，记录每个已装配 provider 的 class、source module、enabled state，降低排障成本。 5. 增加一条宿主回归测试：仅通过 `DefaultEngine.ini` 指定自定义 provider class，不改插件源码即可替换 import 解析或追加 bind contributor。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/*` |
| 预估工作量 | M |
| 架构风险 | 如果第一阶段就让 provider 直接绕过现有 flags，容易让 `bAutomaticImports`、`DisabledBindNames` 这类历史配置丢失原义。应先让 provider 包装并复用当前默认实现。 |
| 兼容性 | 向后兼容。现有 `ini` 键和值保持原义；新增 class slots 默认为空，不配置时行为完全不变。 |
| 验证方式 | 1. 新增 `config-driven provider` 自动化测试，验证 `DefaultEngine.ini` 可装配自定义 resolver/contributor。 2. 复跑 `AngelscriptBindConfigTests` 与 preprocessor 相关测试，确认旧 flags 仍控制默认实现。 3. 运行 `as.DumpEngineState`，确认 dump 能列出已装配 provider。 |

### Arch-EP74：多引擎已是正式能力，但 compile/preprocess 扩展 hook 仍是 process-global static delegate，观察者只能依赖 `current engine` side-channel 推断上下文

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 当插件已经支持多个 `FAngelscriptEngine` 并存时，外部模块订阅 compile/preprocess/hot reload hook 能否天然做到 engine-scoped |
| 当前设计 | 关键 hook 仍主要挂在 process-global static carrier 上：`FAngelscriptRuntimeModule::GetPreCompile()/GetPostCompile()/GetOnInitialCompileFinished()` 返回无参 static delegate，`FAngelscriptPreprocessor::OnProcessChunks/OnPostProcessCode` 也是 static 全局 hook。回调若想知道“是哪一个 engine 在编译”，只能在 callback 内再调用 `FAngelscriptEngine::TryGetCurrentEngine()` 或读取 ambient context。与此同时，多引擎测试已经证明 engine-local 状态必须依赖 `FAngelscriptEngineScope` 切换当前上下文。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h:15`-`:17`、`:37`-`:39` 把 compile hook 定义成无参 delegate；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp:72`-`:87` 以 function-local static 形式存储这些 delegate；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3066`-`:3068`、`:4138`-`:4140` 编译前后直接广播无上下文 delegate；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h:8`、`:29`-`:30` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:35`-`:36`、`:265`-`:287` 表明 preprocess hook 也是 static 全局状态；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:85`、`:268`-`:300`、`:718`-`:733` 说明 engine/world 上下文依赖 `GAmbientWorldContext` 与 `TryGetCurrentEngine()` side-channel；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp:842`-`:875` 明确验证 `EngineA/EngineB` 的 `current context` 语义只有在各自 `FAngelscriptEngineScope` 下才正确。 |
| 优点 | 现有 hook 注册成本很低，宿主模块只要拿到 static delegate 就能快速监听编译或预处理阶段，不需要先管理 engine 生命周期。 |
| 不足 | 对扩展作者来说，这意味着“注册 observer”与“识别 callback 属于哪一个 engine”是两件事。只订阅一次 static hook 时，所有 engine 都会走同一条回调，回调体还必须假设 `current engine` side-channel 永远正确。随着多引擎、测试隔离、未来并发工具链继续推进，这种 contract 很难长期稳定。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `FJsEnv` 天然是实例级对象，loader 在构造时随 env 注入；多 env 场景再由 `IPuertsModule::SetJsEnvSelector()` 指定对象到 env 的选路。扩展策略与 env 实例是绑定的，不需要在 callback 里反查全局“当前 env”。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JsEnv.h:64`-`:68`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnv.cpp:14`-`:25`；`Reference/puerts/unreal/Puerts/Source/Puerts/Public/PuertsModule.h:41`-`:47`；`Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:112`-`:120` | 多实例可扩展性的关键不是“更多 static delegate”，而是让扩展 contract 直接附着到实例。 |
| UnLua | 即使 `CustomLoadLuaFile` 是 static delegate，它的签名也显式携带 `UnLua::FLuaEnv&`；`FLuaEnv::LoadFromCustomLoader()` 调用时把当前 env 直接传给扩展者，避免让外部代码再去猜全局上下文。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDelegates.h:33`-`:35`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:557`-`:567` | 如果暂时不做实例级 registry，至少也要把实例上下文作为 hook 参数显式传出。 |
| UnrealCSharp | `UAssemblyLoader` 虽然不是 event hook，但它代表的扩展 contract 始终通过具体对象执行：settings 选中 loader class，运行时再拿 default object 调 `Load()`。扩展行为绑定在 concrete object 上，而不是散落在若干全局 side-channel。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Domain/AssemblyLoader.h:12`-`:18`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Setting/UnrealCSharpSetting.cpp:87`-`:93`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp:500`-`:508` | 即便不是 delegate，也值得借鉴“扩展行为跟着实例对象走”的 contract 设计。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 给 `FAngelscriptEngine` 增加 engine-scoped extension hub，并把高价值 hook 改成显式携带 `Engine`/`CompileContext` 的 observer contract；现有 static delegate 只保留为 legacy façade。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/` 新增 `FAngelscriptCompileContext`、`FAngelscriptPreprocessContext`、`FAngelscriptReloadContext` 和 `FAngelscriptEngineExtensionHub`；hub 作为 `FAngelscriptEngine` 成员拥有 add/remove handle 风格的 observer 列表。 2. 在 `CompileModules()`、preprocessor、class reload 流程里，先广播 engine-scoped context hook，再向后兼容地转发到现有 `GetPreCompile()`、`GetPostCompile()`、`OnProcessChunks`、`OnPostProcessCode`。 3. 让新 hook 显式传出 `FAngelscriptEngine&`、`ECompileType`、`CompiledModules`、`FFile` 视图等必要上下文，禁止扩展作者再通过 `TryGetCurrentEngine()` 反推来源 engine。 4. 复用 `FAngelscriptStateDump::OnDumpExtensions` 的 add/remove handle 模式，把 observer 生命周期做成可解绑的正式 API，而不是只有全局 static 入口。 5. 新增双 engine 回归：分别在 `EngineA` 和 `EngineB` 上注册不同 observer，验证编译与预处理事件不会串台，且 callback 可直接拿到正确 `Engine`。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.h`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/AngelscriptEngineExtensionHub.h/.cpp` |
| 预估工作量 | M-L |
| 架构风险 | 若一步到位删除现有 static delegate，会打断现有 editor/test 模块和宿主代码。应先加 engine-scoped hook，再把 static delegate 变成 deprecated façade，留出迁移窗口。 |
| 兼容性 | 向后兼容。现有 `GetPreCompile()`、`GetPostCompile()`、`OnProcessChunks`、`OnPostProcessCode` 继续保留；新增 hook 只是提供显式 `Engine/Context`，让新扩展代码不再依赖 side-channel。 |
| 验证方式 | 1. 增加双 engine observer 自动化测试，验证 `EngineA` 订阅者不会收到 `EngineB` 的事件。 2. 复跑现有 compile/preprocess/hot reload 测试，确认 legacy static delegate 仍按旧时序触发。 3. 在 debugger 或 dump 中记录 observer 来源与目标 engine，验证实例级扩展关系可观测。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-EP74 | 多引擎已经实例隔离，但 compile/preprocess 扩展 hook 仍是 process-global static delegate | 扩展点实例化 | 高 |
| P1 | Arch-EP73 | `ini/settings` 仍停留在行为开关层，缺少 class-based provider 装配面 | 配置面扩展 | 高 |

---

## 架构分析 (2026-04-10 01:07)

### Arch-EP75：`RuntimeModule` 把 observer 与 policy slot 混装在同一组 delegate 里，关键扩展点仍是 `last-writer-wins`

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 穷举 `DECLARE_DELEGATE` / `DECLARE_MULTICAST_DELEGATE` 后，哪些正式扩展点其实只允许单个模块独占，无法让多个外部扩展组合生效 |
| 当前设计 | `FAngelscriptRuntimeModule` 同时承担两类职责：一类是 compile/editor 通知型 `multicast delegate`，另一类是 spawn/debug/class-analyze/blueprint-path 这类策略型 `single-cast delegate`。它们都通过 `GetXxx()` 暴露在同一个 runtime facade 上，但只有前者支持多贡献者，后者一旦被第二个模块 `Bind`，就会覆盖前一个模块的实现。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h:9`-`:14`、`:21` 定义 `FAngelscriptGetDynamicSpawnLevel`、`FAngelscriptDebugCheckBreakOptions`、`FAngelscriptGetDebugBreakFilters`、`FAngelscriptDebugObjectSuffix`、`FAngelscriptComponentCreated`、`FAngelscriptClassAnalyzeDelegate`、`FAngelscriptEditorGetCreateBlueprintDefaultAssetPath` 为 `DECLARE_DELEGATE`；同文件 `:15`-`:20` 才是 `DECLARE_MULTICAST_DELEGATE` 的 compile/editor hooks。`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp:42`-`:92` 把这些 policy seam 全部实现成单个 function-local static delegate。实际调用点也直接走 `Execute()` / `ExecuteIfBound()`：`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp:189`-`:190` 用单个 delegate 决定 `OverrideLevel`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UActorComponent.cpp:102` 用单个 delegate 处理 `ComponentCreated`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:1310`-`:1311` 用单个 delegate 修改 `GeneratedStatics`，`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:658`-`:661` 与 `:1150` 读取 break policy/filter。对照 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:405`-`:409` 的 `GetEditorCreateBlueprint().AddLambda(...)` 可见，插件内部同时也在使用真正可多播的 editor hook。 |
| 优点 | 设计简单，单个宿主模块要替换某条策略时几乎没有接入成本，`ExecuteIfBound()` 也让“无扩展时走默认逻辑”足够直接。 |
| 不足 | 对外部生态来说，这不是“可扩展”，而是“可占坑”。例如一个宿主模块想接管 `DynamicSpawnLevel`，另一个工具模块想补充 `DebugBreakFilters` 或 `ClassAnalyze`，现在都只能抢同一个 slot；没有优先级、没有合并协议、没有来源元数据，也无法像 `multicast` hook 那样自然并存。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 在同一个 public delegate header 中，明确把 lifecycle observer 建成 `multicast`，把真正单一策略角色留给 `CustomLoadLuaFile` 这种单播 loader，而且签名显式携带 `FLuaEnv&`。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDelegates.h:25`-`:35`、`:36`-`:49` | 单播只用于明确的 strategy role，observer 与 strategy 的命名边界清楚，不会把所有 seam 都做成一类 carrier。 |
| UnrealCSharp | 生成/编译阶段统一走 `FUnrealCSharpCoreModuleDelegates` 的 `multicast` hooks，`Generator()` 流程只负责广播 `OnBeginGenerator` / `OnEndGenerator`，允许多个 editor/tooling listener 并存。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Delegate/FUnrealCSharpCoreModuleDelegates.h:14`-`:35`；`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:239`、`:309` | 观察者型扩展面统一做成 `multicast`，避免 first-party 与 third-party listener 相互覆盖。 |
| puerts | 把行为替换类 seam 直接建模为 interface/module API，而不是全局 delegate slot：`IJSModuleLoader` 负责模块搜索/加载，`IPuertsModule` 再暴露 `SetJsEnvSelector()` 等显式动作。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSModuleLoader.h:17`-`:50`；`Reference/puerts/unreal/Puerts/Source/Puerts/Public/PuertsModule.h:37`-`:47` | 真正的 strategy seam 更适合 provider object / interface，不适合塞进匿名 `DECLARE_DELEGATE` 再靠 `Bind` 抢占。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先把现有 runtime 扩展面按 `Observer` 与 `Policy` 两类拆开；前者继续 `multicast`，后者升级成显式 policy registry / provider contract。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/` 新增 `AngelscriptPolicyRegistry.h/.cpp`，至少拆出 `SpawnLevelPolicy`、`DebugBreakFilterProvider`、`ClassAnalyzePolicy`、`BlueprintAssetPathPolicy` 四类 contract。 2. 对可合并的 seam（如 `DebugBreakFilters`、`ComponentCreated`、`ClassAnalyze`）提供 `AddContributor(Priority, Delegate)` 或 interface list，由 runtime 按优先级顺序执行并支持 `Handled/Continue` 语义。 3. 对真正单一 owner 的 seam（如 `DynamicSpawnLevel`、`EditorGetCreateBlueprintDefaultAssetPath`）改成 settings/provider class 或 runtime host policy，而不是开放匿名 `Bind` 入口。 4. 保留 `FAngelscriptRuntimeModule::GetDynamicSpawnLevel()` 等旧 API，但内部把第一次 legacy 绑定适配成 registry 的一个 provider，并在 dump 中标记 `LegacyDelegateAdapter` 来源。 5. 为每个 policy seam 增加来源追踪表，记录 `SourceModule`、`Priority`、`ContributionKind`，避免扩展冲突只能靠断点排查。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UActorComponent.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/AngelscriptPolicyRegistry.h/.cpp` |
| 预估工作量 | M |
| 架构风险 | 若直接把所有 `DECLARE_DELEGATE` 一次性替换成新 registry，会影响现有 editor/test/宿主代码的绑定方式。第一阶段应保持旧 `GetXxx()` facade 存活，只把内部实现改为 policy adapter。 |
| 兼容性 | 向后兼容。现有宿主代码仍可通过旧 delegate API 注册，只是底层不再是唯一 slot；新增 registry 让未来多个扩展模块能并存。 |
| 验证方式 | 1. 新增双 contributor 回归：两个宿主模块同时注册 `DebugBreakFilters` 或 `ClassAnalyze`，验证结果可组合而不是互相覆盖。 2. 保留 legacy `GetDynamicSpawnLevel().Bind...` 测试，确认旧路径仍能驱动 `Bind_AActor.cpp` 的 `OverrideLevel`。 3. 运行 dump，确认每个 policy seam 能列出已激活贡献者及优先级。 |

### Arch-EP76：`UAngelscriptSettings` 仍直接渗透到 preprocessor/debugger/class generator，`FAngelscriptEngineConfig` 还不是完整的 engine-scoped policy snapshot

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 插件已经支持多 `FAngelscriptEngine`，但编译语义、命名兼容和调试设置是否真的能随 engine instance 独立，而不是继续共享进程级 `settings/static` 状态 |
| 当前设计 | `FAngelscriptEngineConfig` 的确是 per-engine 对象，构造时被存入 `RuntimeConfig`；但大量关键语义仍绕过它，直接从 `UAngelscriptSettings` 或 static 变量读取。结果是 engine instance 只隔离了部分 runtime flags，未隔离大部分语言/绑定/调试策略。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:64`-`:83` 定义 `FAngelscriptEngineConfig`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:575`-`:578` 在构造函数中保存 `RuntimeConfig`。但同文件 `:76`-`:80` 仍保留 `bUseScriptNameForBlueprintLibraryNamespaces` 与 namespace strip arrays 的 static 全局状态；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1291`-`:1295` 在初始化中直接从 `GetMutableDefault<UAngelscriptSettings>()` 覆盖 `bUseAutomaticImportMethod` 和这些 static 兼容策略。`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:47`-`:56`、`:487`-`:489`、`:523`-`:527` 在预处理阶段直接读取 default settings 和 mutable default settings，影响 `PreprocessorFlags`、`bWarnOnManualImportStatements`、`float` 初始化语义。`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:1499`-`:1504` 在发调试数据库时继续直接读取 `UAngelscriptSettings`。`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:293` 再次直接抓取 default settings。对比 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h:100`-`:117` 可见，这些都不是边缘选项，而是 namespace、构造、兼容语义级设置。 |
| 优点 | 对单引擎项目来说，实现简单，`ini` 改动天然成为全局真相，不需要把 policy 逐层透传到 preprocessor/debugger/class generator。 |
| 不足 | 一旦进入多引擎、并行测试、host-specific override 或未来的 extension provider 场景，这种设计会把 engine instance 边界打穿。某个测试或 commandlet 若改 `GetMutableDefault<UAngelscriptSettings>()`，会影响同进程其它 engine 的预处理和调试语义；外部扩展也无法把 policy 精确绑定到某个 engine。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `FJsEnv` 构造函数直接接收 `IJSModuleLoader`、`ILogger`、`DebugPort` 等 env-scoped 依赖，`SetJsEnvSelector()` 再负责多 env 选路。核心语义跟着 env instance 走，而不是在运行时散落读取全局配置。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JsEnv.h:64`-`:68`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnv.cpp:19`-`:25`；`Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:112`-`:120` | 多实例可扩展性首先要求 policy/strategy 是实例级依赖，而不是 process-global side effect。 |
| UnrealCSharp | settings 并不直接在每个 runtime 角落散读，而是先声明 `AssemblyLoader` / `BindClass`，再通过 `GetAssemblyLoader()->Load(...)` 这类对象调用进入运行时。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpSetting.h:140`-`:144`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp:500`-`:503`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Domain/AssemblyLoader.h:12`-`:18` | 即便配置来自全局 settings，也最好先 materialize 成 concrete object/policy，再让运行时消费该对象。 |
| UnLua | settings 中的 `EnvLocatorClass`、`ModuleLocatorClass`、`PreBindClasses` 会在模块/env 启动时被 materialize 成 `EnvLocator` 与 `ModuleLocator` 对象；真正运行路径依赖的是这些对象，而不是到处散读原始设置字段。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaSettings.h:47`-`:56`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaModule.cpp:103`-`:123`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:77`-`:79` | “全局 settings 作为默认值来源”并不矛盾，但 runtime 行为应尽早收敛成 env-owned policy/object。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把当前直接散读 `UAngelscriptSettings` 的语义收敛成 `engine-owned policy snapshot`；`UAngelscriptSettings` 只负责生成默认 snapshot，不再直接支配运行中每个子系统。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/` 新增 `FAngelscriptEnginePolicy` 或扩展 `FAngelscriptEngineConfig`，把 `bAutomaticImports`、`bScriptFloatIsFloat64`、`StaticClassDeprecation`、namespace strip、preprocessor defaults、warning 相关设置全部纳入一个明确 snapshot。 2. 在 `FAngelscriptEngine::Create(...)` / `Initialize()` 早期增加 `Policy = FAngelscriptEnginePolicy::FromSettings(*GetDefault<UAngelscriptSettings>())`；测试和多引擎场景可直接传自定义 policy，而无需修改 global default object。 3. 让 `FAngelscriptPreprocessor`、`FAngelscriptDebugServer`、`FAngelscriptClassGenerator` 改为从 `OwnerEngine` 或显式 context 读取 policy，逐步移除 runtime 路径中的 `GetDefault<UAngelscriptSettings>()` / `GetMutableDefault<UAngelscriptSettings>()`。 4. 把当前 static 的 `bUseScriptNameForBlueprintLibraryNamespaces` 与 namespace strip arrays 收回到 engine policy；如短期内无法彻底移除 static，先通过 `TryGetCurrentEngine()` facade 读取当前 engine 的 policy，避免继续直接写全局静态字段。 5. 在 `DumpRuntimeConfig.csv` 旁新增 `RuntimePolicy.csv` 或扩展现有表，明确输出本 engine 的 semantic policy snapshot，便于比对多引擎差异。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEnginePolicy.h/.cpp` |
| 预估工作量 | M-L |
| 架构风险 | 这类改动会触碰大量 helper/static code path；如果某些路径拿不到当前 engine context，第一阶段可能需要兼容 facade。最大的风险不是编译错误，而是少量语义项漏迁移后导致新旧 policy 混用。 |
| 兼容性 | 向后兼容。现有 `ini` 键保持不变，默认 primary engine 仍从 `UAngelscriptSettings` 生成 policy；只是新增了 per-engine override 能力，并减少测试/工具对 global settings 的副作用。 |
| 验证方式 | 1. 新增双 engine 语义隔离测试：让 `EngineA` 与 `EngineB` 使用不同 `float/static-class/namespace` policy，验证 preprocessor/debug database 输出互不污染。 2. 复跑现有 preprocessor、debugger、bind config 相关测试，确认从默认 settings 派生的行为不变。 3. 运行 state dump，确认 `RuntimeConfig` 与新增 `RuntimePolicy` 能清楚区分“进程参数”和“engine 语义快照”。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-EP75 | 单播 `delegate slot` 混入正式扩展面，多个宿主扩展无法组合 | 扩展点分层与 contract 重构 | 高 |
| P1 | Arch-EP76 | `settings/static` 继续穿透多引擎 runtime，`EngineConfig` 不是完整 policy snapshot | 实例级配置收口 | 高 |

---

## 架构分析 (2026-04-10 01:17)

### Arch-EP77：扩展治理仍停在匿名 `BindName` 层，无法按 contributor/class/module 做 rollout 与 rollback

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 用户在不改插件源码的前提下新增 `global function` / `type binding` 之后，能否按扩展包、扩展类、来源模块做启停、审计和回滚 |
| 当前设计 | 当前正式治理面仍是 `BindName` 级别。`UAngelscriptSettings` 与 `FAngelscriptEngineConfig` 只接受 `DisabledBindNames`；`FBindInfo` 也只记录 `BindName / BindOrder / bEnabled`。更关键的是，state dump 明明为 `BindRegistrations.csv` 预留了 `BindModule` 列，但实际导出时始终写空字符串。结果是“扩展能加进来”与“扩展能被运营治理”仍是两件事。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h:59` 只暴露 `DisabledBindNames`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:431`-`:435` 的 `FBindInfo` 只有 `BindName`、`BindOrder`、`bEnabled`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:138`、`:151`-`:153` 说明未命名贡献会退化成 `UnnamedBind_n`；同文件 `:176`-`:181` 的 `GetBindInfoList()` 仍只返回三元组；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1928`-`:1940` 把 settings/runtime config 的 disable 逻辑统一压成 `TSet<FName>`；`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp:847`-`:871` 为 `BindRegistrations.csv` 定义了 `BindModule` 列，但每行实际写入的第二列都是 `FString()`。 |
| 优点 | 低成本 emergency kill-switch 已经存在，项目可以快速屏蔽某个已知 bind name。 |
| 不足 | 这套模型无法回答“这个扩展来自哪个 module/class”“我要关掉某个第三方扩展包的全部贡献”“同一个扩展类生成的多条 bind 如何整体回滚”。一旦扩展作者没有手工维护稳定 `BindName`，治理面就退化成不稳定的 `UnnamedBind_n`。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | 把导出边界做成 contributor-set 级 manifest，而不是匿名符号列表。`SupportedModule`、`SupportedAssetPath`、`SupportedAssetClass`、`ExportModule`、`ClassBlacklist` 都是正式 settings；`RegisterSettings()` 还会主动补齐默认集合。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpEditorSetting.h:70`-`:76`、`:86`、`:124`-`:148`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Setting/UnrealCSharpEditorSetting.cpp:48`-`:104` | rollout/rollback 应首先面向“哪组 contributor 生效”，其次才是底层单条 symbol。 |
| UnLua | 把参与脚本生态的来源集做成显式 provider/class 清单：`EnvLocatorClass`、`ModuleLocatorClass`、`PreBindClasses` 都是项目配置，模块启动时按这些清单实例化和预绑定。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaSettings.h:49`-`:56`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaModule.cpp:104`-`:105`、`:112` | 即使不是按 bind 粒度治理，也先把“哪些来源会生效”提升成声明式配置。 |
| puerts | 即使 settings 较轻，也仍按类型集合暴露过滤面。`RootPath`、`WatchDisable`、`IgnoreClassListOnDTS`、`IgnoreStructListOnDTS` 都是显式字段，而不是让用户去屏蔽单条生成结果。 | `Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsSetting.h:22`、`:50`、`:53`、`:56` | 治理面更适合按 class/module/root 等稳定身份表达，而不是按最终产物字符串表达。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把扩展治理单位从匿名 `BindName` 提升到有来源的 `contributor descriptor`，让 enable/disable、dump、回归测试都围绕稳定身份工作。 |
| 具体步骤 | 1. 扩展 `FAngelscriptBinds::FBindInfo`，新增 `ContributorId`、`SourceModule`、`SourceClass`、`ContributionKind`、`bLegacyAnonymous`；旧路径缺失这些信息时才回退到 `LegacyAnonymous + BindName`。 2. 在 `UAngelscriptSettings` 或独立 `UAngelscriptExtensionGovernanceSettings` 中新增 `DisabledExtensionModules`、`DisabledExtensionClasses`、`DisabledContributionKinds`，保留 `DisabledBindNames` 作为最底层 escape hatch。 3. 为 `RegisterBinds()`、未来 `ExtensionMethods`、`RegisterTypeFinder()` 增加带 `FAngelscriptContributorDesc` 的 overload；老 API 自动适配成 `LegacyAnonymousContributor`。 4. 改造 `DumpBindRegistrations()`：真正填充 `BindModule`，并新增 `ConfiguredExtensions.csv` 或等价表，输出 `ContributorId -> SourceModule/SourceClass -> Enabled/DisabledReason`。 5. 为 reflective UFUNCTION 扩展与 type contributor 复用同一 contributor descriptor，避免治理面只覆盖手写 `FBind` 而漏掉 ambient discovery。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/AngelscriptContributorDesc.h/.cpp` |
| 预估工作量 | M |
| 架构风险 | 若直接强制所有旧注册路径都提供 `SourceModule/SourceClass`，会打断现有 static bind 与历史生成代码。第一阶段应允许 legacy 匿名 contributor 存在，只是让其在 dump 和日志里显式带风险标签。 |
| 兼容性 | 向后兼容。现有 `DisabledBindNames`、`FBind`、`RegisterBinds()`、旧 dump 文件名全部保留；新治理字段只是提供更高层的稳定身份。 |
| 验证方式 | 1. 新建两个宿主扩展模块，各自注册多条 `global function`/`type finder`，验证可按 `SourceModule` 整体禁用其中一个而不影响另一个。 2. 保留一条未命名 legacy bind，验证它仍可工作，但 dump 会把它标成 `LegacyAnonymous`。 3. 运行 state dump，确认 `BindRegistrations.csv` 不再出现永远为空的 `BindModule` 列。 |

### Arch-EP78：运行时缺少 lightweight service-object 扩展平面，扩展逻辑不是 `static hook` 就是 world/subsystem

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 用户子类化能否落在“可拥有、可初始化、可热重载、可销毁”的轻量 service object 上，而不是被迫选择 `static delegate` 或 gameplay subsystem |
| 当前设计 | 当前 public `virtual` 面主要服务 script/gameplay 生命周期：`UScriptEngineSubsystem`、`UScriptGameInstanceSubsystem` 都是 `Blueprintable, Abstract`，核心方法也是 `BP_ShouldCreateSubsystem`、`BP_Initialize`、`BP_Tick`。真正改变插件核心行为的 seam 则还是 `FAngelscriptRuntimeModule::GetDynamicSpawnLevel()`、`GetPreCompile()`、`GetPostCompile()` 和 `FAngelscriptPreprocessor::OnProcessChunks/OnPostProcessCode` 这类 `static` carrier。与此同时，真正持有 runtime 的还是 concrete `UAngelscriptGameInstanceSubsystem`。也就是说，当前没有一层介于“匿名 static hook”和“重型 subsystem owner”之间的 runtime service object。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/BaseClasses/ScriptEngineSubsystem.h:6`、`:88`-`:98` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/BaseClasses/ScriptGameInstanceSubsystem.h:6`、`:42`-`:52` 说明现有可继承基类主要围绕 Blueprintable subsystem 生命周期；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h:32`、`:37`-`:38` 仍把关键策略/观察点暴露为 `static` accessor；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h:29`-`:30` 继续使用 public static preprocess hook；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.h:11`-`:39` 显示 concrete `UAngelscriptGameInstanceSubsystem` 直接持有 `OwnedEngine` 与 `PrimaryEngine`。 |
| 优点 | script 作者做 gameplay/editor 生命周期扩展很顺手，第一方也能用极低开销的 static hook 快速接入。 |
| 不足 | `import resolver`、`script root provider`、`binding policy`、`diagnostic sink` 这类真正的 runtime service 很难被包装成“有 owner、可 reset、可 shutdown”的对象。扩展作者要么抢占 `static hook`，要么误把 subsystem 当核心服务替换入口。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 用 plain `UObject` service 承载核心策略。`ULuaEnvLocator` 定义 `Locate()`、`HotReload()`、`Reset()`，`ULuaModuleLocator` 定义 `Locate()`；settings 选择具体 class，模块启动时用 `NewObject<ULuaEnvLocator>(..., EnvLocatorClass)` 实例化。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/LuaEnvLocator.h:22`-`:45`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/LuaModuleLocator.h:20`-`:33`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaSettings.h:49`-`:56`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaModule.cpp:104`-`:105` | 核心扩展对象不必是 subsystem；轻量 `UObject` service 更适合承接 reload/reset 和 per-host state。 |
| UnrealCSharp | 把加载策略收敛成一个轻量 provider class。`UAssemblyLoader` 只有 `virtual Load()`，settings 通过 `TSubclassOf<UAssemblyLoader>` 选择实现，运行时在 `FMonoDomain` 中直接调用 `GetAssemblyLoader()->Load(...)`。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Domain/AssemblyLoader.h:13`-`:18`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpSetting.h:104`、`:141`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Setting/UnrealCSharpSetting.cpp:87`-`:91`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp:500`-`:502` | 当职责足够窄时，provider object 比继承 runtime owner 更易组合、测试和替换。 |
| puerts | 把核心装载策略做成 interface 注入。`IJSModuleLoader` 是单独接口，`FJsEnv` 构造函数直接接收 `std::shared_ptr<IJSModuleLoader>`，module 侧再通过 `SetJsEnvSelector()` 暴露更高层路由。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSModuleLoader.h:17`-`:42`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JsEnv.h:66`；`Reference/puerts/unreal/Puerts/Source/Puerts/Public/PuertsModule.h:45` | 先把策略 owner 化，再谈 static facade，扩展面会天然更清晰。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 新增 engine-owned lightweight extension service 层，把 runtime core 的可替换策略从 `static hook`/subsystem 中拆出来，做成 narrow provider object。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/` 新增一组轻量 service base，例如 `UAngelscriptScriptRootProvider`、`UAngelscriptImportResolver`、`UAngelscriptBindingPolicyService`，以及统一的 `IAngelscriptExtensionService` 或 `Initialize/Shutdown/HandleHotReload` 约定。 2. 在 `FAngelscriptEngine` 或新的 `FAngelscriptRuntimeHost` 中增加 `FAngelscriptExtensionHost`，按 engine 实例 materialize 这些服务对象；默认实现完全适配今天的 `DiscoverScriptRoots()`、`ProcessImports()`、`BindScriptTypes()` 逻辑。 3. 让 `UAngelscriptGameInstanceSubsystem` 继续只负责 owner/tick，把真正的策略替换转为组合 `ExtensionHost`，避免项目方再误继承 subsystem 去改 core policy。 4. 把现有 `GetPreCompile()`、`OnProcessChunks`、`GetDynamicSpawnLevel()` 等入口先适配成 default service 的 façade；第一阶段保留旧 API，只让新服务层成为正式推荐路径。 5. 增加 per-engine 回归：两个 engine 使用不同 `ScriptRootProvider/ImportResolver` 时，初始化、hot reload、shutdown 都能收到对应 service 回调且互不串台。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/BaseClasses/ScriptEngineSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/BaseClasses/ScriptGameInstanceSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/AngelscriptExtensionService.h/.cpp`、`AngelscriptExtensionHost.h/.cpp` |
| 预估工作量 | M-L |
| 架构风险 | service owner 一旦设计不清，会和现有 `GameInstanceSubsystem`、clone engine、editor-only 路径产生双重所有权。第一阶段应坚持“subsystem 仍是 owner，service 只承载策略”，避免一次性迁走生命周期。 |
| 兼容性 | 向后兼容。现有 Blueprintable subsystem、`static delegate`、concrete `UAngelscriptGameInstanceSubsystem` 全部保留；新 service plane 只是把正式支持的 runtime strategy 路径从 side-channel 中收口出来。 |
| 验证方式 | 1. 新建一个宿主 provider class，只通过 settings/registry 替换 script-root 或 import 逻辑，验证无需继承 subsystem。 2. 触发 hot reload 与 engine shutdown，确认 provider 的 `HandleHotReload/Shutdown` 被调用且不会残留到下一实例。 3. 保留一个 legacy `static hook` 路径测试，确认 façade 迁移不改变旧项目行为。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-EP77 | 扩展 rollout/rollback 仍是匿名 `BindName` 级，缺少 contributor 级治理 | 治理面与审计面新增 | 高 |
| P1 | Arch-EP78 | runtime 缺少 lightweight service-object 扩展平面 | provider/service layer 新增 | 高 |

---

## 架构分析 (2026-04-10 01:28)

### Arch-EP79：扩展任务没有正式的“角色 contract”，宿主作者仍要在多条专家 surface 之间自行拼装

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | “添加自定义全局函数 / 修改编译行为 / 添加自定义类型绑定 / 订阅编译热重载事件”这四类任务，是否各自对应清晰、命名化、可发现的扩展角色 |
| 当前设计 | 目前这四类任务分散在互不对称的 surface 上：函数扩展既可以走 `FAngelscriptBinds::FBind` / `BindGlobalFunction()`，也可能被 `Bind_BlueprintType.cpp` 的全局 `UClass` 扫描自动纳入；类型扩展要直接实现 `FAngelscriptType` 或 `RegisterTypeFinder()`；编译阶段定制依赖 `UAngelscriptSettings` flag、`FAngelscriptPreprocessor` hook 和零散 delegate；事件订阅则落在 `FAngelscriptRuntimeModule` / `FAngelscriptClassGenerator` 的 static delegate 上。`AngelscriptRuntime.Build.cs` 还把 `ModuleDirectory`、`Core` 和 third-party source 直接暴露为 `PublicIncludePaths`，等于默认让扩展者直接接触内部头，而不是从单独的 `Public/Extension` 入口进入。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs:15`-`:22` 直接公开 `ModuleDirectory`、`Core` 与 `ThirdParty/angelscript/source`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:438`-`:475`、`:606`-`:611` 把 `FBind` / `RegisterBinds()` / `BindGlobalFunction()` 作为 raw function seam；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:71`-`:81`、`:96`-`:120` 把 `RegisterTypeFinder()` 与大型 `FAngelscriptType` vtable 作为 type seam；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1366`-`:1406` 会扫全局 `TObjectRange<UClass>()` 自动发现 `BlueprintCallable`/`ScriptCallable`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp:72`-`:90`、`:100`-`:120` 说明同一条函数扩展又会在 direct native pointer 与 reflective fallback 两条后端之间切换；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h:32`-`:47` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h:51`-`:154` 又分别承载事件和行为开关。 |
| 优点 | 第一方开发非常灵活，几乎任何一点能力都能找到现成的低层入口；仓内团队可以按实现便利性选择 raw bind、反射扫描或 delegate。 |
| 不足 | 对宿主作者来说，这不是“角色清晰的扩展系统”，而是“多条专家通道的拼图”。相同任务可能有两套入口且缺少升级路径，例如自定义函数既可能写 `UFUNCTION`，也可能要退回 `BindGlobalFunction()`；而工具链、dump、治理表又无法统一知道某个扩展到底属于 `FunctionContributor`、`TypeContributor` 还是 `CompileObserver`。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 直接把不同 concern 切成不同角色：`UExtensionMethods` 只承载“给现有类型加方法”；`IJSModuleLoader` 只承载模块解析/加载；`IPuertsModule` 只承载 env 级模块动作；`UCodeGenerator`/`ICodeGenerator` 只承载 declaration/codegen 扩展。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Public/ExtensionMethods.h:18`-`:20`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:940`-`:980`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Public/JSModuleLoader.h:17`-`:25`、`:31`-`:50`；`Reference/puerts/unreal/Puerts/Source/Puerts/Public/PuertsModule.h:17`-`:49`；`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Public/CodeGenerator.h:11`-`:19`；`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1710`-`:1717` | 先把“扩展者扮演什么角色”命名出来，再决定底层用扫描、settings 还是 runtime call。 |
| UnLua | 把 runtime 任务拆成宿主对象接口、locator、settings 和 loader hook：`IUnLuaInterface` 负责对象到脚本模块的映射，`EnvLocatorClass` / `ModuleLocatorClass` 负责 env/module 选路，`PreBindClasses` 负责预绑定范围，`CustomLoadLuaFile` 负责最终加载。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaInterface.h:23`-`:39`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaSettings.h:47`-`:56`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaModule.cpp:103`-`:125`；`Reference/UnLua/Source/TPSProject/TutorialBlueprintFunctionLibrary.cpp:91`-`:104` | 角色不必很多，但每个角色都要让用户一眼知道“我该实现哪个类/接口来做这件事”。 |
| UnrealCSharp | 通过 `AssemblyLoader`、`BindClass`、`SupportedModule/ExportModule/ClassBlacklist` 和 `FUnrealCSharpCoreModuleDelegates` 把“加载器”“绑定来源”“导出范围”“生命周期观察者”分成几类独立 contract。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Domain/AssemblyLoader.h:12`-`:18`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpSetting.h:53`-`:63`、`:140`-`:144`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpEditorSetting.h:122`-`:148`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Delegate/FUnrealCSharpCoreModuleDelegates.h:7`-`:35` | 即便底层实现仍可多样，也应让扩展作者先从少数命名角色中选，而不是从内部头文件中反推。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留 legacy API 的前提下，为扩展任务补一层显式 `role contract`，把“任务 -> 角色 -> 后端适配”关系固定下来。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/` 新增最小角色族：`UAngelscriptFunctionContributor`、`UAngelscriptTypeContributor`、`UAngelscriptImportResolver`、`IAngelscriptCompileObserver`、`IAngelscriptReloadObserver`；第一阶段只覆盖用户最常问的四类任务。 2. 在 runtime 初始化期建立 `FAngelscriptExtensionRoleRegistry`，为每个贡献者记录 `Role`、`SourceModule`、`SourceClass`、`BackendKind(DirectBind / ReflectiveFallback / DelegateAdapter / SettingsBacked)`，而不是让扩展直接散落在 static API 上。 3. 让现有 `BindGlobalFunction()`、`RegisterTypeFinder()`、`GetPreCompile()`、`OnProcessChunks` 等入口通过 adapter 注册到 registry；旧代码不动，但新 dump / 审计 / 测试只面向 role registry。 4. 在 `UAngelscriptSettings` 或独立 extension settings 中新增 `FunctionContributorClasses`、`TypeContributorClasses`、`ImportResolverClass`、`CompileObserverClasses`，把 role 与配置面接起来。 5. 增加一个最小宿主样例，明确展示四类任务分别落在哪个 role 上，并通过 `StateDump` 导出 `ExtensionRoles.csv`，让升级和排障不再依赖读核心源码。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/AngelscriptExtensionRoles.h/.cpp` |
| 预估工作量 | M |
| 架构风险 | 如果第一阶段就试图一次性替换所有 legacy API，会和现有 bind/type/reload 路径产生双重 authority。应先让 registry 只做“角色归档 + adapter”，等 role 模型稳定后再收口原始 surface。 |
| 兼容性 | 向后兼容。现有 `FBind`、`BindGlobalFunction()`、`RegisterTypeFinder()`、runtime/class-generator delegate 和 `ini` 开关全部保留；新角色层只负责命名、装配和审计。 |
| 验证方式 | 1. 新建一个宿主扩展模块，同时实现函数贡献者、类型贡献者和编译观察者，验证三者都能进入同一个 `ExtensionRoles.csv`。 2. 对同一条自定义函数分别走 `UFUNCTION` 反射路径和 raw `BindGlobalFunction()` 路径，确认 registry 能区分 `BackendKind`，但最终脚本行为一致。 3. 复跑现有 bind/reload/settings 回归，确认未接入新 role 的旧项目行为完全不变。 |

### Arch-EP80：宿主对象无法声明自己的脚本/扩展上下文，运行时仍主要依赖 ambient engine 反推

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 宿主项目能否让 `GameInstance` / `UObject` / 资产类自身声明“我属于哪个 script/runtime context”，从而影响扩展选路，而不是把一切都压成当前 ambient engine |
| 当前设计 | 当前 runtime 主要通过 ambient world 和当前 `GameInstanceSubsystem` 反推上下文。`FAngelscriptRuntimeModule` 的关键 strategy seam 仍偏 engine-global，例如 `GetDynamicSpawnLevel()` 无入参；`UAngelscriptGameInstanceSubsystem::GetCurrent()` 通过 `FAngelscriptEngine::GetAmbientWorldContext()` 找 world，再回到当前 `GameInstance` 的单个 subsystem；`FAngelscriptEngine::ShouldUseAutomaticImportMethodForCurrentContext()`、`IsHotReloadingForCurrentContext()` 等 helper 也都先 `TryGetCurrentEngine()`。换句话说，当前 contract 的中心是“当前 engine 是谁”，而不是“哪个宿主对象在请求扩展能力”。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h:9`-`:14`、`:32`-`:47` 显示 `GetDynamicSpawnLevel` 等 strategy seam 仍是 module/static 级 delegate；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp:16`-`:23` 在初始化时直接把当前 `OwnedEngine` 绑定到 subsystem，`:94`-`:113` 的 `GetCurrent()` 又从 ambient world context 反推当前 subsystem；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:298`-`:301` 公开 `GAmbientWorldContext`，`:702`-`:709`、`:718`-`:733` 说明一系列 “ForCurrentContext” helper 最终都依赖 `TryGetCurrentEngine()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:86`-`:95` 的 `FAngelscriptEngineDependencies` 也只包含文件系统相关 lambda，没有任何 `Locate(const UObject*)` 或 context resolver seam。 |
| 优点 | 单引擎默认路径简单直接，绝大多数项目不需要先声明对象级上下文就能跑起来；对现有 host project 成本最低。 |
| 不足 | 一旦项目想做“不同 `GameInstance` / `World` / `UObject` 使用不同扩展 profile、脚本根、env 选择或治理规则”，当前 surface 基本无从表达。即使未来补齐 typed request struct，如果没有宿主对象级 contract，扩展者仍只能从 ambient engine 侧向推断来源对象，难以形成真正的 per-object / per-host customization。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 直接把“对象属于哪个脚本模块 / env”做成宿主对象和 locator 的责任：`IUnLuaInterface::GetModuleName()` 允许 `UCLASS`/Blueprint 声明模块名，`ULuaModuleLocator::Locate(const UObject*)` 与 `ULuaEnvLocator::Locate(const UObject*)` 都显式拿宿主对象做路由。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaInterface.h:23`-`:39`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/LuaModuleLocator.h:20`-`:26`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaModuleLocator.cpp:18`-`:42`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/LuaEnvLocator.h:22`-`:33`；`Reference/UnLua/Source/TPSProject/TPSGameInstance.h:22`-`:32` | 让宿主对象自己参与声明上下文，比所有扩展都围着 ambient engine 打补丁更稳。 |
| puerts | 多 env 选路不是隐式 ambient lookup，而是显式 selector：`IPuertsModule::SetJsEnvSelector(std::function<int(UObject*, int)>)` 和 `FJsEnvGroup::SetJsEnvSelector()` 都把 `UObject*` 作为路由输入。 | `Reference/puerts/unreal/Puerts/Source/Puerts/Public/PuertsModule.h:41`-`:47`；`Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:112`-`:119`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvGroup.cpp:176`-`:182` | 当系统已经支持多 env / 多实例时，selector 应该面向宿主对象，而不是面向全局当前状态。 |
| UnrealCSharp | 即便不走对象级 env，扩展范围也尽量挂在显式 class/module 身份上：`GetModuleName(const UField*)` / `GetModuleName(const UPackage*)` 明确从字段或包推导 module，`BindClass` 设置项显式声明哪些类要进入绑定。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Common/FUnrealCSharpFunctionLibrary.h:14`-`:32`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp:53`-`:145`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpSetting.h:53`-`:63`、`:143`-`:144` | 即便不做 `UObject` 级 selector，也应让扩展路由依赖显式 identity，而不是只依赖“当前 engine”。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保持 ambient fallback 的前提下，新增宿主对象级 `context resolver` contract，让对象/类可以显式声明 Angelscript 扩展上下文。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/` 新增 `FAngelscriptObjectContext` 与 `UAngelscriptContextResolver`（或 `IAngelscriptContextResolver`），最小字段建议包括 `SourceObject`、`ResolvedWorld`、`ResolvedGameInstance`、`ProfileId`、`SourceModule`、`PreferredScriptRoot`、`PreferredEngineInstanceId`。 2. 新增 `UINTERFACE(Blueprintable)` 风格的 `UAngelscriptContextProvider`，允许 `GameInstance`、`Actor`、资产 CDO 或宿主 Blueprint 通过 `BlueprintNativeEvent` 提供局部 context；resolver 默认先问对象/类，再回退到今天的 ambient world/subsystem。 3. 给 `FAngelscriptEngine::TryGetCurrentEngine()` 和 `UAngelscriptGameInstanceSubsystem::GetCurrent()` 增加 `const UObject* ContextObject` 重载；旧无参版本继续存在，内部转发为 `ContextObject = nullptr` 的 legacy fallback。 4. 让后续 spawn/debug/import/provider 类 seam 都优先消费 `FAngelscriptObjectContext`，而不是各自再做一次 ambient world 推断；至少先覆盖 `GetDynamicSpawnLevel`、debug break policy 和 script-root/provider 选择。 5. 在 dump 中新增 `ResolvedContextRoutes.csv` 或等价表，记录当前有哪些对象/类提供了 context override、最终落到哪个 engine/profile，便于排查多世界与多实例问题。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/Extension/AngelscriptContextResolver.h/.cpp` |
| 预估工作量 | M-L |
| 架构风险 | 对象级 context 若在对象尚未完成 world 归属时就被读取，可能引入早期生命周期空值和 PIE 切换问题。第一阶段必须允许 resolver 返回空上下文，并清晰回退到 legacy ambient 路径。 |
| 兼容性 | 向后兼容。无参 `TryGetCurrentEngine()`、当前 `GameInstanceSubsystem` owner 模型和所有现有 static helper 全部保留；只有显式实现 context provider 的项目才会获得新的 per-object routing 能力。 |
| 验证方式 | 1. 新建两个 `GameInstance` 或两个宿主对象实现不同的 context provider，验证它们能解析到不同 `ProfileId/EngineInstance`，且默认对象仍走旧路径。 2. 在 PIE、多 world 和 tool world 下回归 `TryGetCurrentEngine(ContextObject)`，确认 resolver 为空时会稳定回落到 ambient engine。 3. 运行 dump，确认 `ResolvedContextRoutes.csv` 能显示对象级 override 与最终生效的 engine/profile。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-EP79 | 扩展任务缺少正式角色映射，宿主作者要在 raw bind / reflective scan / delegate / settings 间自行拼装 | role contract 显式化 | 高 |
| P1 | Arch-EP80 | 宿主对象无法声明自己的扩展上下文，运行时主要依赖 ambient engine 反推 | context resolver / host-object contract 新增 | 高 |
