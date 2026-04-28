# BindingPipeline 架构评审

---

## 架构分析 (2026-04-08 14:02)

### Arch-BP-1：注册目录是 process-global，执行状态却是 engine-scoped，缺少 provider ownership

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 绑定注册目录与生命周期作用域 |
| 当前设计 | `FBind` 在静态初始化阶段把 lambda 塞进进程级 `BindArray`，真正执行时再借当前 `FAngelscriptEngine` 的 `BindState` / `TypeDatabase` 落到具体上下文。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:23-34` — `GetBindState()` 优先取当前 engine，否则回落 `LegacyBindState`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:132-154` — `GetBindArray()`/`RegisterBinds()` 使用静态数组保存所有 bind family；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:35-45,54-118` — type database 同样是 current-engine / legacy fallback 双态；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1915-1921` — `BindScriptTypes()` 统一调用 `FAngelscriptBinds::CallBinds(CollectDisabledBindNames())`；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineTests.cpp:610-639` — 测试可在任意调用点直接构造 `FAngelscriptBinds::FBind` 并观察排序执行。 |
| 优点 | binder 作者模型简单，`Bind_*.cpp` 只需声明静态 `FBind`；配合 `DisabledBindNames` 和执行观测，可以在不改 bind 文件的情况下过滤或审计启动期 bind pass。 |
| 不足 | 注册 catalog 没有 provider/module ownership，`BindArray` 生命周期比 engine 更长；多 engine/clone 场景下，执行状态已 engine-scoped，但“谁提供了这个 bind”仍是隐式静态初始化结果，不利于按 provider 卸载、审计和扩展。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | 先由 editor 侧 `Generator()` 串起 class/struct/enum/binding generator，再由 runtime `FCSharpBind` 在 `OnCSharpEnvironmentInitialize` 时把生成物回填到 descriptor/hash 表。provider 边界是显式生成阶段，而不是隐式静态对象。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:266-305`；`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FCSharpBind.cpp:24-30,103-223` | 把“注册目录”与“执行上下文”拆成显式阶段，便于记录 owner、生成状态和缺失项。 |
| UnLua | `FLuaEnv` 构造时创建 `ClassRegistry`、`DelegateRegistry`、`FunctionRegistry` 等 registry；registry 生命周期跟随 env，而不是挂在进程级静态 bind array 上。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:102-113,144-159`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:40-45,108-130,225-233` | 用 env-owned registry 明确“本次 VM/engine 初始化到底注册了什么”，适合后续做 provider 级审计和多实例隔离。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在现有 `FBind` 之上增加显式 provider 层，把“静态声明 bind”升级成“可枚举的 bind provider”。 |
| 具体步骤 | 1. 在 `Core/AngelscriptBinds.h` 增加 `FAngelscriptBindProviderDescriptor`，至少包含 `ProviderName`、`ModuleName`、`BindOrder`、`RegisterLambda`。<br>2. 让现有 `FAngelscriptBinds::FBind` 构造函数继续可用，但内部改为注册一个默认 provider descriptor，保持旧 `Bind_*.cpp` 不动。<br>3. 在 `Core/AngelscriptEngine.cpp` 的 `BindScriptTypes()` 前先构建当前 engine 的 resolved provider plan，把 provider ownership 写入 `FAngelscriptBindState`。<br>4. 扩展 `GetBindInfoList()` 与 state dump，让输出不止有 `BindName/BindOrder/Enabled`，还带 `ProviderName/ModuleName/RegistrationSource`。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineTests.cpp` |
| 预估工作量 | M |
| 架构风险 | 需要小心兼容现有静态初始化路径；如果 provider plan 与现有排序逻辑不一致，可能改变启动 bind 顺序。 |
| 兼容性 | 向后兼容。旧 `FBind`、旧 `Bind_*.cpp` 和现有脚本 API 都可保留；只是内部记录形式更显式。 |
| 验证方式 | 复跑现有 `BindConfig` / `MultiEngine` 测试；新增 provider ownership 测试，验证同名 bind 的 provider 来源、排序和 `DisabledBindNames` 过滤仍与旧行为一致。 |

### Arch-BP-2：现有自动化链是三条支线，仍没有“绑定合同”的单一来源

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 新增 UE 类型时的接入成本，以及现有 codegen/缓存是否已成为绑定主干 |
| 当前设计 | 当前仓库同时存在 `Bind_BlueprintType.cpp` 的反射自动绑定、`Bind_*.cpp` 的手写 binder、`Binds.Cache` 的 cook 缓存、`BindModules.Cache` 的分片模块生成，以及 `AS_FunctionTable_*.cpp` 的 UHT exporter；但这些产物互相补充，不是单一 authoritative contract。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:962-1047` — 原生 `UClass` 只要满足 `BlueprintType` 或存在 `BlueprintCallable/BlueprintEvent` 即进入声明阶段；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1317-1505` — late pass 遍历类函数/属性并把结果写入 `FAngelscriptBindDatabase::Get().Classes`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1514-1778,2028-2440` — `TSubclassOf`、`TObjectPtr`、`TWeakObjectPtr` 仍需手写 `FAngelscriptType`、手写 declaration、手写 late binder 与 `RegisterTypeFinder`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp:37-107,273-400` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp:25-149,286-313` — 典型 binder 仍以 `ExistingClass().Method()`/`BindGlobalFunction()` 为核心模式；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp:42-101,103-137` — `Binds.Cache` 仅缓存 class/struct bind 结果与 header link；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:999-1077,1285-1328` — editor 会生成 `ASRuntimeBind_*` / `ASEditorBind_*` 模块并写 `BindModules.Cache`，但模块本体仍只是 `RegisterBinds` 包装；`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:46-79,449-470` — UHT exporter 生成的是 `AddFunctionEntry` shard，而不是 `FAngelscriptType`/binder DSL。 |
| 优点 | 已经有多条自动化支线可复用：反射自动发现覆盖了大量 `UClass`，bind DB 解决 cook/runtime 元数据缺失，UHT function table 提供可审计的注册 sidecar。 |
| 不足 | 新增 reflected `UClass` 的“基础可见性”可能是零源码接入，但新增 value/container/wrapper 类型仍通常需要 4 到 6 步：写 `FAngelscriptType` 子类、注册 type/type finder、补 declaration bind、补 late bind、补调试/CppForm/文档。现有自动化链没有一条能告诉你“哪个类型已经全自动、哪个类型仍需要手工 binder、手工 binder 覆盖了哪些 contract”。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | editor 侧一个总控 `Generator()` 顺序执行 `FClassGenerator`、`FStructGenerator`、`FEnumGenerator`、`FBindingClassGenerator`、`FBindingEnumGenerator` 并立即编译；runtime 侧 `FCSharpBind` 只负责把生成层的 `__Field`/`__Function` 槽位回填到 UE 反射对象。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:266-305`；`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp:16-25,66-120`；`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FDelegateGenerator.cpp:10-47,74-107`；`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FCSharpBind.cpp:143-223` | 先把“绑定合同”落成生成资产，再在 runtime 做轻量回填，authoritative source 更单一。 |
| puerts | `DeclarationGenerator` 通过 `FTypeScriptDeclarationGenerator` 扫描 `UClass/UStruct/UEnum` 与 Blueprint 资产，统一输出 `Typing/ue/ue.d.ts` 与 `ue_bp.d.ts`，并保留 Blueprint 缓存标记。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:360-457,1700-1706`；`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Public/TypeScriptDeclarationGenerator.h:33-131` | 即便 runtime 不完全 codegen-first，也先把可消费的声明合同统一落盘，便于查覆盖率、查缺口、驱动 IDE。 |
| UnLua | `FLuaEnv` 启动时初始化 `ClassRegistry` / `FunctionRegistry` / `DelegateRegistry` / `PropertyRegistry`，`ClassRegistry` 与 `FFunctionDesc` 直接从 `UStruct/UFunction/FProperty` 构造 runtime descriptor，减少 per-type 手写 binder。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:104-113`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:58-77,108-130`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:31-76` | 把“反射足够表达的类型”尽量交给通用 descriptor/registry，而不是继续扩散手写 binder family。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先不推翻 binder-first 路线，而是把现有三条自动化支线统一成 `BindingCoverageManifest`，再基于 manifest 渐进提高自动化比例。 |
| 具体步骤 | 1. 新增一个只读 manifest 生成步骤，合并 `Binds.Cache`、`BindModules.Cache`、`AS_FunctionTable_Entries.csv` 与 `GetBindInfoList()`，输出“类型/函数属于 manual bind、reflective bind、function-table only、missing contract”的统一报表。<br>2. 在 manifest 中明确区分 `reflected UObject path` 与 `custom FAngelscriptType path`，把“新增一个类型需要几步”变成显式分类，而不是靠读源码猜。<br>3. 基于 manifest 加一个 binder scaffold：对 `TObjectPtr`/`TSubclassOf` 这类 wrapper/value 类型自动生成 `Bind_<Type>.cpp` 骨架，至少预填 `Register`、`RegisterTypeFinder`、declaration bind、late bind 四段模板。<br>4. 对纯 `BlueprintCallable` helper 类，优先复用现有 reflective path/UHT function-table sidecar，减少继续新增手写 `Bind_*.cpp` 的压力。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.*`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.*`，`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`，`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`，以及新增 `Documents/` 或 `Intermediate/` 下的 manifest 导出脚本/产物 |
| 预估工作量 | L |
| 架构风险 | 如果一开始就试图让 UHT tool 直接生成 binder 代码，容易把当前 `FAngelscriptType` 语义压扁；应先做 manifest/report-only，再决定哪些 family 适合自动化。 |
| 兼容性 | 第一阶段完全向后兼容，只增加报告与脚手架；第二阶段可以按类型族逐步切换，不需要一次性重写全部 `Bind_*.cpp`。 |
| 验证方式 | 对现有仓库生成 manifest，抽样核对 `Bind_UObject`、`Bind_AActor`、`Bind_BlueprintType`、`Bind_Delegates` 与 `AS_FunctionTable_*` 是否能正确归类；新增一个小型 wrapper type，通过 scaffold + tests 验证生成模板足以落地。 |

### Arch-BP-3：当前只给了 C++ 扩展者隐式入口，没有给脚本开发者正式的自定义绑定协议

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 插件用户能否在不改插件源码的前提下注册自定义绑定 |
| 当前设计 | 对 C++ 模块作者来说，`AngelscriptRuntime` 公开了 `AngelscriptBinds.h`、`AngelscriptType.h` 与 `RegisterBinds/Register/RegisterTypeFinder`；因此技术上可以在独立模块里声明 `FBind` 或自定义 `FAngelscriptType`。但 engine 没有正式的 provider discovery 协议，默认仍依赖静态链接结果或 editor 生成的 `BindModules.Cache`；对纯脚本开发者则没有“注册自定义 binder/type”入口。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs:15-18` — `ModuleDirectory` 与 `Core` 被加入 `PublicIncludePaths`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:438-475` — `FBind`/`RegisterBinds` 是公开 API；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:71-82` — `Register`/`RegisterTypeFinder` 也是公开 API；`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp:276-313` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineTests.cpp:610-639` — 测试代码可直接在 `Binds/` 目录外构造 bind；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1473-1488` — runtime 只会自动加载 `BindModules.Cache` 中列出的模块；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:999-1077` — 这个 cache 由 editor 生成；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1311-1314,1403-1406` — 对脚本作者可利用的扩展面主要还是 `BlueprintCallable/BlueprintPure/ScriptCallable` 反射路径，而不是自定义 binder/type DSL。 |
| 优点 | C++ 层面并非完全封闭，游戏模块或额外插件理论上可以不改 `AngelscriptRuntime` 源码就注册 bind/type。 |
| 不足 | 这条扩展路径是“隐式可用”而不是“正式支持”：没有 provider 接口、没有加载约定、没有 ownership/版本契约、没有脚本作者可直接使用的扩展协议。结果是 C++ 扩展依赖静态初始化和模块装载时机，脚本开发者则只能在现有反射规则内活动。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 公开 `ExportClass`、`ExportEnum`、`ExportFunction`、`AddType` 等 API，`FLuaEnv` 启动时统一消费 exported classes/functions/enums；`ClassRegistry` 在建 metatable 时还会补注入 reflected export。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/Binding.h:21-43`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:144-159`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:225-233` | 扩展入口是公开合同，不要求扩展方猜测静态初始化顺序。 |
| puerts | 声明生成阶段显式寻找实现了 `UCodeGenerator` 的类并执行 `ICodeGenerator::Execute_Gen()`，把扩展点建成标准 interface。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Public/CodeGenerator.h:10-27`；`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1708-1717` | 即便只是工具链扩展，也提供显式 interface，而不是依赖隐藏的全局状态。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把“技术上能 include 头文件注册 bind”升级成“官方支持的 binding extension protocol”，并给脚本开发者保留一条无需手写 binder 的 reflective fallback。 |
| 具体步骤 | 1. 在 `AngelscriptRuntime` 新增 `IAngelscriptBindingExtension` 或等价 delegate 协议，至少包含 `RegisterTypes()`、`RegisterBinds()`、`DescribeExtension()`。<br>2. 在 `FAngelscriptEngine::BindScriptTypes()` 前枚举这些 extension，而不是只依赖 `BindModules.Cache`。<br>3. 在 `UAngelscriptSettings` 增加 `AdditionalBindProviderModules`，让项目模块可以显式声明要加载的扩展 provider。<br>4. 对脚本开发者，先把 `meta=(ScriptCallable)` 的 reflective fallback 和生成文档/manifest 结合起来，明确“哪些扩展无需 C++ binder，哪些仍必须写 C++ extension provider”。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.*`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h`，`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`，以及新增的 extension interface 头/实现 |
| 预估工作量 | M |
| 架构风险 | 需要定义 provider 何时加载、何时可禁用、是否允许卸载；若直接暴露过多低层 API，后续 ABI 稳定性会变差。 |
| 兼容性 | 向后兼容。旧静态 `FBind` 继续可用；新协议只是增加正式入口。对脚本作者来说，新增的是能力边界说明和反射 fallback，不会破坏现有脚本。 |
| 验证方式 | 新增一个独立测试模块或示例项目模块，实现 `IAngelscriptBindingExtension` 并在不修改插件源码的情况下注册一个 bind/type；再验证该 extension 能出现在 bind info/manifest 中，并可被 `DisabledBindNames` 或 provider-level config 禁用。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P0 | Arch-BP-2 | 绑定合同缺少单一来源，自动化链分裂 | 结构性收敛 + manifest 化 | 高 |
| P1 | Arch-BP-1 | process-global 注册目录与 engine-scoped 执行状态之间的 ownership 缺口 | 生命周期梳理 | 中 |
| P1 | Arch-BP-3 | 自定义绑定没有正式 extension protocol | 扩展点新增 | 高 |

---

## 架构分析 (2026-04-08 23:36)

### Arch-BP-4：绑定元数据依赖 `PreviousBind` 游标回填，binder DSL 存在时序耦合

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 绑定作者如何为同一个注册动作附加 world-context、deprecation、generated-accessor、compile-out 等元数据 |
| 当前设计 | `FAngelscriptBinds` 先把函数注册进 AngelScript engine，再把“上一个刚注册的函数/全局变量”记到 `FAngelscriptBindState`，后续通过 `SetPreviousBind...()` / `PreviousBindPass...()` / `DeprecatePreviousBind()` 等 API 追加语义。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:409-433` — `OnBind()` 在注册后更新 `PreviouslyBoundFunction`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:308-407,542-553,628-634` — implicit constructor、deprecated、property/editor/world-context、first-param metadata、compile-out、pure constant 都是对“上一个 bind”的二次修改；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:627-640` — `SetPreviousBindArgumentDeterminesOutputType()`、`PreviousBindPassScriptFunctionAsFirstParam()` 等能力全部暴露为 process API；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp:61-73,556-579` — `GetTypedOuter()`、`NewObject()` 先 `Method/BindGlobalFunction()` 再立即设置 output-type；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp:303-312,450-467` — `Spawn()`/`SpawnActor()` 注册后再注入 first-param metadata 与 output-type；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:687-694,1154-1250` — `StaticClass()`、property accessor 生成器必须在每次 `Binds.Method()` 后继续补 deprecated / editor-only / generated trait。 |
| 优点 | 旧 binder 写法很短，历史上往 `Bind_*.cpp` 补一个 trait 的改造成本低。 |
| 不足 | 元数据依赖调用顺序而不是绑定对象本身，helper 抽取、自动生成、批量转换时都必须小心维护“上一条 bind 是谁”；语义分散在多次 API 调用中，静态检查和跨文件审计都很困难。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | 生成阶段就把绑定合同拆成明确文件：`FBindingClassGenerator` 为每个 class 生成 implementation stub，`FCSharpBind` 再按 `__Field` 名称把 UE `FProperty/UFunction` 哈希回填到 descriptor。元数据附着在生成物和 descriptor 上，而不是“上一次注册的函数”。 | `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FBindingClassGenerator.cpp:743-810`；`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FCSharpBind.cpp:133-170,206-223` | 让“函数声明”和“函数元数据”同属于一个显式对象，减少时序副作用。 |
| puerts | `FTypeScriptDeclarationGenerator` 把 overload、namespace、Blueprint 版本信息都保存在 `FunctionOutputs`、`BlueprintTypeDeclInfoCache`、`WriteOutput()` 的显式结构里，最后统一落盘到 `ue.d.ts/ue_bp.d.ts`。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Public/TypeScriptDeclarationGenerator.h:38-70,112-126`；`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:530-550` | 先收集结构化描述，再统一输出，避免“注册后再补一个 side effect”式 authoring。 |
| UnLua | `FFunctionDesc` 在构造时一次性从 `UFunction` 提取参数、返回值、latent/out-ref 语义，descriptor 本身就是调用合同；`ClassRegistry` 返回 `FClassDesc*` 作为后续 metatable 注册对象。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:31-76`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:247-263,302-311` | 让“注册结果”有稳定 descriptor，可组合、可缓存、可测试。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把当前“注册函数 + 回头改上一个函数”的 DSL，收敛成“注册时一次性提交 metadata”的显式 bind descriptor。 |
| 具体步骤 | 1. 在 `Core/AngelscriptBinds.h` 新增 `FAngelscriptBindMetadata`，至少包含 `FirstParamMetaData`、`DeterminesOutputTypeArgument`、`bGeneratedAccessor`、`bEditorOnly`、`bDeprecated`、`DeprecationMessage`、`CompileOutType`、`bNoDiscard`、`bCallable`。<br>2. 为 `Method()`、`BindGlobalFunction()`、`Constructor()`、`BindProperty()` 增加接收 metadata 的重载，在 `OnBind()` 内原子应用 trait，而不是依赖 `GetPreviousBind()`。<br>3. 先迁移高频生成路径：`Bind_BlueprintType.cpp` 的 getter/setter 生成器、`Bind_UObject.cpp` 的 output-type/world-context 辅助函数、`Bind_AActor.cpp` 的 `Spawn` 家族。<br>4. 保留现有 `SetPreviousBind...()` 作为兼容层一段时间，但在 Editor/Test 下记录告警，推动后续 bind family 迁移到新 API。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`，以及相关测试文件 |
| 预估工作量 | M |
| 架构风险 | 需要确保 metadata 重载与旧 `SetPreviousBind...()` 叠加时不会重复写 trait；如果迁移过程中混用两套 API，容易出现双写或遗漏。 |
| 兼容性 | 向后兼容。旧 binder 先不删，只是新增更显式的注册接口；现有脚本签名不需要改变。 |
| 验证方式 | 为 `BindConfig`/`MultiEngine` 测试补充 trait 断言，验证 deprecation、world-context、output-type、generated-accessor 在新旧写法下得到同样的 `asCScriptFunction` 状态；再对 `Bind_BlueprintType` 生成 accessor 做回归编译。 |

### Arch-BP-5：自动化分片只覆盖 public `BlueprintCallable` `UClass`，还不是统一的绑定自动化主干

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 哪些类型能走自动生成分片，哪些类型仍必须手写 binder/type，以及分片是否具备稳定的扩展边界 |
| 当前设计 | `GenerateBindDatabases()` 只扫描满足条件的 `UClass`，把它们按 package 名聚合进 `RuntimeClassDB/EditorClassDB`；`GenerateNativeBinds()` 再每 10 个 key 切一份 `ASRuntimeBind_*` / `ASEditorBind_*` 模块，运行时仅按 `BindModules.Cache` 载入这些模块。模板包装类型和自定义 value/wrapper type 不在这条自动化链上。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1088-1160` — 自动化入口只遍历 `TObjectRange<UClass>()`，过滤 abstract/deprecated/`Private/` header/无 `BlueprintCallable` 的类，并按 package 名写入 `RuntimeClassDB/EditorClassDB`；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1005-1057` — 分片算法按 `ModuleCount = 10` 把 key 装进 `ASRuntimeBind_*` / `ASEditorBind_*`；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1324-1471` — 生成模块本体只是 `StartupModule()` 里注册一个 late lambda，再串行调用每个 `Bind_<Class>()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1466-1496` — runtime 先读 `Binds.Cache`，再按 `BindModules.Cache` 载入分片模块并统一执行 `BindScriptTypes()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1514-1778,2028-2105` — `TSubclassOf<T>`、`TObjectPtr<T>` 仍然要分别手写 early declaration、`FAngelscriptType` 子类/bridge、late method bind。 |
| 优点 | 对 public `BlueprintCallable` `UClass` 的 common path 已经有一定自动化，能把大量函数暴露切散到多个编译单元，缓解单文件过大问题。 |
| 不足 | 这条自动化链的覆盖面非常窄：新增一个“公共 `BlueprintCallable` `UClass`”与新增一个 `TSubclassOf`/`TObjectPtr`/自定义 wrapper/value type 完全不是同一工作流；而且分片名称按当前 key 序号生成，新增或重排 package 时会牵连后续 shard 命名和重建范围。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | editor 总控每次统一跑 class/struct/enum/binding generator；每个 class/binding class 都单独生成文件并写盘，而不是把多个 package 先塞进固定 10 个 bucket 再统一包壳。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:237-305`；`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp:870-874`；`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FBindingClassGenerator.cpp:732-740,941-948` | 以“类型/产物”为粒度生成文件，新增一个类型只影响该类型对应的生成物。 |
| puerts | `FTypeScriptDeclarationGenerator` 既生成 `ue.d.ts`，也维护 `BlueprintTypeDeclInfoCache`，能按 package/version 恢复旧声明，只重写变更的 Blueprint 类型；同时保留 `UCodeGenerator` 作为额外生成入口。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Public/TypeScriptDeclarationGenerator.h:61-70`；`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:360-457,561-607,1708-1717` | 自动化边界与增量缓存是显式 contract，可以知道“哪些资产变了、哪些声明需要重写”。 |
| UnLua | `FLuaEnv` 启动时只初始化 registry，真正遇到 metatable 时由 `ClassRegistry::RegisterReflectedType()` / `PushMetatable()` 按需注册 reflected type，额外扩展则通过 `Binding.h` 的 `ExportClass/ExportFunction/AddType` 接口注入。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:104-113,144-159`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:59-77,108-140,221-233`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/Binding.h:21-43` | 自动化与扩展点分层清晰：反射路径按需注册，非反射路径走显式导出，不需要把所有能力都塞进同一种分片模块。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把现有“按 key 数量切 shard 的 `UClass` 自动化”升级成稳定的 `BindingShardManifest`，并把 wrapper/value/template family 纳入同一自动化框架。 |
| 具体步骤 | 1. 把 `BindModules.Cache` 从纯字符串数组升级成 manifest 条目：至少记录 `ShardId`、`Scope(Runtime/Editor)`、`SourcePackage/Provider`、`Classes`、`Hash/Version`，运行时优先读新 manifest，旧缓存缺失时再回退到旧格式。<br>2. 让 shard 命名以 `SourcePackage` 或 provider 为稳定键，而不是 `i - (ModuleArray.Num() - 1)` 这类序号命名，避免新增一个 package 导致后续 shard 全部改名。<br>3. 新增 `IAngelscriptBindGenerator`（或 editor 侧等价接口），允许除了 `UClass` 反射扫描外，再为 `TSubclassOf`、`TObjectPtr`、`TWeakObjectPtr` 这类 family 产出 descriptor/scaffold/source shard。<br>4. 在 manifest 中显式标记自动化覆盖类型：`ReflectedUClass`、`GeneratedWrapper`、`ManualOnly`。这样新增一个 UE 类型时，可以先判断它落在哪个 lane，而不是靠读 `Bind_BlueprintType.cpp` 和 `Binds/` 猜流程。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`，以及新增的 manifest/interface 导出文件 |
| 预估工作量 | L |
| 架构风险 | shard manifest、旧 `BindModules.Cache`、现有 editor 生成流程需要并行兼容一段时间；如果一步到位把 wrapper family 也切进 codegen，调试和回归成本会明显上升。 |
| 兼容性 | 第一阶段可以完全向后兼容：继续保留旧 `ASRuntimeBind_*`/`ASEditorBind_*` 产物与旧缓存读取逻辑，只额外产出 manifest；后续再按 family 渐进接入新 generator。 |
| 验证方式 | 以一个新增 public `BlueprintCallable` `UClass`、一个新增 wrapper type、一个现有 package 重命名作为三组样本，验证 manifest 能正确报告 lane、仅重建受影响 shard，并保证 runtime 仍能从旧 `BindModules.Cache` 回退加载。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-BP-4 | binder 元数据依赖 `PreviousBind` 时序副作用 | DSL 显式化 | 高 |
| P1 | Arch-BP-5 | 自动化分片覆盖面窄且 shard 边界不稳定 | 自动化管线收敛 | 高 |

---

## 架构分析 (2026-04-08 23:46)

### Arch-BP-6：`BindOrder` 只有整数排序，真实依赖图被拆散在多个 bind pass 里

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 绑定注册阶段如何表达“先声明类型、再补方法/属性、最后做特化”的依赖关系 |
| 当前设计 | 核心框架只提供 `Early / Normal / Late` 三个整数 phase，具体 family 再通过 `+100`、`-10`、`+150` 这类偏移把真实依赖图编码进各个 `Bind_*.cpp`。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:424-429` — `EOrder` 只有三个枚举值；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:144-148,176-183,201-210` — runtime 只是按整数排序后串行执行，没有显式依赖声明；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:712-725,1317-1368` — `UClass` 先在 `Early` 做 declarations，再在 `Late+100` 收集并回填 methods/properties；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1514-1530,1756-1778,2028-2048,2069-2105` — `TSubclassOf<T>`、`TObjectPtr<T>` 都被拆成 `*_Declaration` 和 `Late-10` 的运行期 bind；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp:286-312,450-467` — `AActor` family 继续使用 `Late+150` 插入特化逻辑。 |
| 优点 | 实现简单，bind 作者只要给一个整数即可，不需要额外 scheduler 或 dependency DSL。 |
| 不足 | 新增一个 wrapper/value/type family 时，作者必须先猜“声明应该挂在哪个 slot、特化应该挂在哪个 slot”，再把逻辑拆到多个 `FBind`；自动化生成器和第三方扩展也只能复刻这些 magic number，而不能声明“我依赖某个类型已声明完成”。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | 编辑器总控用显式 generator 顺序组织阶段：`Class -> Struct -> Enum -> Asset -> BindingClass -> BindingEnum -> Compile`，阶段边界是代码层可见的，而不是靠整数插槽猜测。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:266-305` | 让“阶段”成为一等概念，便于后续插入新 generator 或输出审计信息。 |
| puerts | `FTypeScriptDeclarationGenerator` 先定义 `GenTypeScriptDeclaration()`、`GenClass()`、`GenStruct()`、`GenEnum()`、`GetFunctionOutputs()` 等结构化步骤，再在总流程里先扫类型、后处理 Blueprint 资产、最后统一落盘。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Public/TypeScriptDeclarationGenerator.h:33-124`；`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:360-415` | 用显式 pass 和中间结构表示依赖，而不是把顺序编码进离散整数。 |
| UnLua | `FLuaEnv` 启动时只创建 registry；真正遇到 metatable 时由 `ClassRegistry::PushMetatable()` 按需调用 `RegisterReflectedType()`，反射注册不依赖全局排序表。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:104-113,144-159`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:108-140` | 对“可懒加载”的 family 直接按需注册，减少全局 phase 数量。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留旧 `int32 BindOrder` 的前提下，引入显式 `BindPhase + dependency` 描述，把当前隐式 phase 图显式化。 |
| 具体步骤 | 1. 在 `Core/AngelscriptBinds.h` 新增 `EBindPhase` 与 `FAngelscriptBindDependencies`，至少覆盖 `DeclareTypes`、`RegisterMethods`、`FinalizeTraits`、`PostProcess` 等当前已存在的逻辑阶段。<br>2. 为 `FBind` 增加新构造：允许声明 `Phase`、`AfterBindNames`、`RequiresTypeNames`；旧 `int32 BindOrder` 构造保留，并先映射到 legacy phase。<br>3. 先迁移最复杂的 `Bind_BlueprintType.cpp` 和模板 wrapper family，把 `*_Declaration` / `Late-10` / `Late+100` 收敛为显式 phase；其余 `Bind_*.cpp` 暂时走 legacy 适配层。<br>4. 扩展 `GetBindInfoList()` 与 bind observation 输出，让调试信息显示 `Phase`、`ResolvedDependencies` 和最终执行序列；新增循环依赖与 phase 冲突测试。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineTests.cpp` |
| 预估工作量 | M |
| 架构风险 | phase 解析器与旧整数排序并存期间，最容易出现“同一 family 被 legacy 和 new phase 双重排序”的兼容 bug。 |
| 兼容性 | 向后兼容。现有 `FBind(int32 BindOrder, ...)` 和所有已存在 `Bind_*.cpp` 可继续工作；只有迁移到新 phase API 的 family 才会获得更强的可观测性。 |
| 验证方式 | 复跑现有 bind order / multi-engine 测试；新增 `Bind_BlueprintType` 与 `TSubclassOf<T>` 的 phase 快照测试，确认新旧路径生成相同注册序列；再故意构造依赖环，验证 editor/test 下能给出明确报错。 |

### Arch-BP-7：`Binds.Cache` 是无版本的单体合同，cook/runtime 缺少 shard 与降级策略

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 绑定合同如何持久化到 cooked/runtime，以及外部扩展能否增量参与这套持久化协议 |
| 当前设计 | `FAngelscriptBindDatabase` 把 classes/structs 序列化成单个 `Binds.Cache`，headers 再额外写到 `Binds.Cache.Headers`；engine 启动时整体加载，若 classes/structs 为空则直接 fatal。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp:27-31` — `Serialize()` 只序列化 `Structs` 和 `Classes`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h:132-137` — `BoundEnums`、`BoundDelegateFunctions`、`HeaderLinks` 只保存在内存结构上；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp:42-50,60-99` — 保存时整包写 `Binds.Cache` 和 `.Headers` sidecar；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp:103-115` — 加载后若 `Classes/Structs` 为空就 fatal；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1466-1477,1502-1505` — engine 初始化先整体加载 `Binds.Cache`，cook 再整体回写；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:712-725,1498-1505` — `Bind_BlueprintType` 一方面在 `Early` 消费 DB，一方面在非 DB 路径里重新构建并追加整个 `Classes` 列表。 |
| 优点 | cooked game 不需要 editor 反射导航能力也能重放 class/struct 绑定合同，部署形态简单。 |
| 不足 | 缺少 schema version、provider 归属、分片边界和部分失效恢复：新增或修改一个 UE 类型通常意味着重写整份 cache；第三方扩展若想在 packaged build 里参与 reflective bind，只能接入这份全局 cache；一旦 cache 缺失或过旧，失败是全局 fatal，而不是“哪一类绑定缺失”。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | `FClassGenerator`、`FBindingClassGenerator` 分别把 class 声明、binding API、implementation 写成独立文件，生成粒度是“每个类型/每个 binding class”，不是单个二进制缓存。 | `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp:870-874`；`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FBindingClassGenerator.cpp:736-740,943-948` | 让新增/修改单个类型只影响对应生成物，便于增量重建和外部审计。 |
| puerts | `BlueprintTypeDeclInfoCache` 按 package 记录 Blueprint 声明变化，`GenTypeScriptDeclaration()` 只重建变更资产，然后分别写 `ue.d.ts` 与 `ue_bp.d.ts`。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Public/TypeScriptDeclarationGenerator.h:61-70`；`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:387-457` | 缓存与输出天然带有 package 粒度，便于知道“哪个资产失效、需要重写哪一份声明”。 |
| UnLua | 没有要求先读一个全局 bind DB；`FLuaEnv` 从导出的 class/function/enum 列表直接注册，反射类型则在 `PushMetatable()` 时按需 materialize。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:144-159`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:126-130` | 对 runtime 可即时解析的 family，优先使用显式导出或按需注册，而不是所有信息都压进单体缓存。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 `Binds.Cache` 从单体二进制升级为“版本化 manifest + 可分片 shard”，并保留旧 cache 作为过渡兼容层。 |
| 具体步骤 | 1. 在 `Core/AngelscriptBindDatabase.*` 新增 `BindingManifest`，至少记录 `SchemaVersion`、`ProviderName`、`ShardKind(Class/Struct/Enum/Delegate/Header)`、`Hash`、`ObjectCount`、`RequiredInCookedBuild`。<br>2. 第一阶段 dual-write：cook 时继续写旧 `Binds.Cache`，同时额外写 `Bindings/<Provider>/<ShardKind>.bin` 和 manifest；headers 不再单独放到 `.Headers`，而是并入对应 shard。<br>3. 第一阶段 dual-read：runtime 若检测到 manifest，则按 shard 精确加载；editor/dev 缺失 optional shard 时给出 warning 并允许回退到 legacy cache 或直接反射重建，只有 cooked build 缺失 required shard 才 hard fail。<br>4. 为未来的扩展 provider 预留写入口，让项目模块/外部插件在 cook 时能产出自己的 shard，而不必修改插件内部的全局 `Binds.Cache` 生成逻辑。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`，以及新增的 manifest/shard 定义文件 |
| 预估工作量 | L |
| 架构风险 | dual-write / dual-read 过渡期需要长期维护两套加载逻辑；如果 manifest 与 legacy cache 描述不一致，可能造成 editor/cooked 行为分叉。 |
| 兼容性 | 渐进兼容。旧工程和已有 cooked 产物仍可只依赖 `Binds.Cache`；新工程可以先开启 manifest 旁路输出，不需要一次性切换全部消费方。 |
| 验证方式 | 进行两次 cook：第一次基线、第二次只修改一个 `BlueprintCallable` `UClass`，确认只有相关 shard/hash 变化；再模拟缺失单个 shard、缺失 manifest、缺失 legacy cache 三种场景，验证 editor/dev 与 cooked build 的降级/报错策略符合预期。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-BP-7 | `Binds.Cache` 单体化导致持久化合同缺少分片与降级策略 | 持久化管线重构 | 高 |
| P1 | Arch-BP-6 | `BindOrder` 用整数隐式编码依赖图 | 注册调度显式化 | 高 |

---

## 架构分析 (2026-04-08 23:55)

### Arch-BP-8：函数签名合同被拆成三套模型，自动化和校验没有共享 IR

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 绑定函数的签名、默认值、world-context、mixin、deprecation 等语义由谁作为 authoritative contract 持有 |
| 当前设计 | 当前至少同时存在三套函数签名模型：`Bind_*.cpp` 里的手写字符串声明，`Helper_FunctionSignature.h` 里的 runtime 反射签名对象，以及 `AngelscriptUHTTool` 里的 C# `AngelscriptFunctionSignature`。三者没有共享 schema。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp:61-73,556-580` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp:303-312,450-467` — 典型手写 binder 直接写 `"UObject NewObject(...)"`、`"%s Spawn(...)"` 这类 declaration，再额外调用 `SetPreviousBindArgumentDeterminesOutputType()` / `PreviousBindPassScriptFunctionAsFirstParam()` 补元数据；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h:33-63,178-333,336-394,414-512` — reflective path 里另有 `FAngelscriptFunctionSignature`，它能从 `UFunction` 推导 `ScriptName`、默认值、`WorldContext`、`DeterminesOutputType`、`ScriptMixin`、tooltip 和 deprecation；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp:51-55,100-150` — `BindBlueprintCallable()` 只在 Blueprint/reflection lane 复用这套签名对象；`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs:8-38,41-100` 与 `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:19-22,449-479` — UHT tool 又维护一套只关心 `OwningType/FunctionName/ReturnType/ParameterTypes/IsStatic/IsConst` 的签名记录，用来生成 `ERASE_*` 宏和 function-table shard。 |
| 优点 | 每条 lane 都只解决自己最迫切的问题：手写 binder 可快速定制脚本 API，reflection lane 能消费 `UFunction` 元数据，UHT lane 能独立生成 `ERASE_*` 宏。 |
| 不足 | 新增一个需要被自动化理解的函数语义时，通常要同时考虑 `Bind_*.cpp` 字符串、`FAngelscriptFunctionSignature` 字段、UHT C# 签名 builder 三处；`world-context`、`global-scope`、`mixin`、tooltip/deprecation 等 rich metadata 也没有进入 UHT lane，导致自动化产物和 runtime binder 很难共享校验逻辑。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | `FClassGenerator` 直接从 `UFunction/FProperty` 生成宿主侧函数与字段声明，runtime `FCSharpBind` 再用同一批 encoded 名称和 `GetTypeHash()` 回填到 descriptor/hash registry。 | `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp:255-340`；`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FCSharpBind.cpp:133-223` | 生成阶段和运行阶段虽然分离，但共享同一组反射导出的名字与 hash，不需要再维护第三套手工 signature schema。 |
| puerts | `GenFunction()` 统一从 `UFunction` 生成参数名、默认值、nullable/ref 包装和返回类型；`GenClass()`、interface 扫描、extension-method 收集都复用这套函数声明生成逻辑。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:930-1072,1248-1331` | 把函数签名生成器做成公共基础设施，class/interface/extension 不再各写一套字符串拼接。 |
| UnLua | `FFunctionDesc` 在构造时遍历 `UFunction` 的参数属性并生成 `FPropertyDesc` 列表，后续调用、delegate 和 registry 都围绕这一份 descriptor 运转。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:31-76`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/Binding.h:21-43` | descriptor 先统一，再决定运行时怎么调用；扩展 API 只负责注入，不再复制函数签名语义。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把当前分散在 C++ runtime、手写 binder、UHT C# 工具里的函数签名信息收敛为一份可序列化的 `BindingFunctionDescriptor`，先做 dual-write，再逐步让各条 lane 共享它。 |
| 具体步骤 | 1. 在 `AngelscriptRuntime` 新增 plain-data `FAngelscriptBindingFunctionDescriptor`，字段至少覆盖 `OwnerType`、`ScriptName`、`Declaration`、参数类型/名字/默认值、`WorldContext`、`DeterminesOutputType`、`StaticInScript`、`GlobalScope`、`MixinTarget`、tooltip/deprecation。<br>2. 让 `Helper_FunctionSignature.h` 先把现有 `FAngelscriptFunctionSignature` 转换成 descriptor；再给手写 binder 增加可选 builder API，例如 `BindFromDescriptor()` 或 `MakeDescriptor().Declaration(...).DeterminesOutputType(...)`，旧字符串 API 保留为兼容层。<br>3. 把 `AngelscriptUHTTool` 的 `AngelscriptFunctionSignatureBuilder.cs` 改为生成同构 schema，只负责填充 native erasure 所需字段，不再另造一套只含 `ReturnType/ParameterTypes` 的 record。<br>4. 在 editor/test 下新增 contract validator：对 reflective `UFunction` 同时生成 runtime descriptor 和 UHT descriptor，比较 declaration、default、`WorldContext`、`DeterminesOutputType` 等字段；对手写 binder 则至少校验 declaration 与附加 trait 是否被完整落到 descriptor。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.*`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`，`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs`，`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs` |
| 预估工作量 | L |
| 架构风险 | schema 需要同时被 C++ 和 C# 消费，初期 dual-write 容易出现字段含义不完全一致的问题；手写 binder 若同时保留旧字符串 API 和新 descriptor API，也要避免双重写入 trait。 |
| 兼容性 | 向后兼容。旧 `BindGlobalFunction()/Method()` 字符串接口和现有 `AS_FunctionTable_*` 产物可以继续存在；第一阶段只新增 descriptor 和校验，不强制全部 binder 立刻迁移。 |
| 验证方式 | 以 `NewObject`、`SpawnActor`、一个 BlueprintCallable static function、一个 mixin function 为样本，比较 manual/runtime/UHT 三条 lane 生成的 descriptor 是否一致；再跑现有 bind tests，确认 script declaration、tooltip、`WorldContext`、`DeterminesOutputType` 和 UHT shard 不回退。 |

### Arch-BP-9：绑定元素缺少统一稳定 key，cache、UHT 产物和 runtime registry 无法直接对齐

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 绑定系统如何唯一标识“同一个 class/property/function/delegate/enum”，以及不同产物之间能否直接 join / diff / 增量更新 |
| 当前设计 | 当前不同 lane 使用的是 family-specific key：class/struct 多用 object path，method/property 在 bind DB 里却只存名字，function-table shard 只存 `ClassName + FunctionName`，runtime skip/config 又改用 `UClass* + FString` 或 `(FName ClassName, FName FunctionName)`。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h:9-35,56-87,90-118,132-137` — `FAngelscriptPropertyBind`、`FAngelscriptMethodBind`、`FAngelscriptClassBind` 都有名为 `UnrealPath` 的字段，但不同 family 并没有统一含义；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h:379-394` — method bind 写库时把 `DBBind.UnrealPath` 设成 `Function->GetName()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:716-743,788-790,1099-1117,1503-1505` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UStruct.cpp:865-900,1213-1242` — class/struct 解析使用完整 object path，而 method/property 回放只靠 `FindFunctionByName()` / `FindPropertyByName()` 和字段名；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:497-559` — runtime direct-bind map 用 `AddFunctionEntry(UClass*, FString Name, FFuncEntry)`，skip list 用 `(FName ClassName, FName FunctionName)`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp:27-31,65-98` — `Serialize()` 只持久化 class/struct，enum/delegate 只在 `.Headers` sidecar 里按 object path 留 header link；`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:14-23,37-44` — UHT shard 和 CSV 只记录 `ClassName`、`FunctionName`、`EraseMacro`。 |
| 优点 | 每个局部场景的 lookup 都很便宜：回放 class/struct 可直接 `FindObject()`，回放 property/function 可直接 `FindFunctionByName()`，UHT CSV 也容易阅读。 |
| 不足 | 同一个绑定元素跨 DB、UHT summary、skip config、docs/diagnostics 时没有 canonical symbol key，做覆盖率对账、增量 diff、外部 provider merge、重命名跟踪时都要写 family-specific join 逻辑；`UnrealPath` 这个字段名本身也会误导扩展者，以为 method/property 存的是完整 object path。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | 生成阶段先为 `UField` 生成 `[PathName(...)]`，runtime registry 再以 `UStruct*`、`uint32 property/function hash` 维护统一索引。 | `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:39-80`；`Reference/UnrealCSharp/Source/UnrealCSharp/Public/Registry/FClassRegistry.h:19-62`；`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FClassRegistry.cpp:91-180` | 路径负责“谁”，hash 负责“哪一个成员”，生成物和 runtime registry 可以直接对齐。 |
| puerts | `BlueprintTypeDeclInfoCache` 以 `PackageName` 为稳定键跟踪 Blueprint 声明变化，再按 `ue.d.ts / ue_bp.d.ts` 输出，增量刷新时可以直接知道哪个 package 失效。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:389-457` | 先定义稳定 cache key，再做输出和增量策略，避免不同产物各自挑一套命名。 |
| UnLua | `ClassRegistry` 同时维护 `UStruct*` 和 metatable name 的映射，建 metatable 时还把 `TypeHash` 写进 Lua 元表。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:59-145` | 用“对象引用 + 名称 + hash”三层键值把 runtime lookup 和跨边界识别统一起来。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在现有 cache/CSV/runtime map 之上增加统一的 `BindingSymbolKey`，先 dual-write 再逐步把所有诊断、跳转和覆盖率工具切到 canonical key。 |
| 具体步骤 | 1. 定义 `FAngelscriptBindingSymbolKey`，至少包含 `OwnerObjectPath`、`MemberName`、`ScriptNameOrNamespace`、`Kind(Class/Struct/Property/Function/Enum/Delegate)`、`SignatureHash`、`ProviderName`；不要再复用语义模糊的 `UnrealPath` 名称。<br>2. 让 `FAngelscriptMethodBind` / `FAngelscriptPropertyBind` / `FAngelscriptClassBind` dual-write 新 key，同时保留旧字段；runtime 回放优先走新 key，缺失时再回退到 `FindFunctionByName()` / `FindPropertyByName()`。<br>3. 扩展 `AddFunctionEntry()`、`SkipBindNames`、UHT CSV/summary 和未来 manifest，把 canonical key 一并写出，保证 DB、function-table、skip config、diagnostics 可以直接 join。<br>4. 在 editor 工具链里补一层 symbol audit：扫描同一个 key 在 bind DB、function-table shard、docs/header-link 中是否都能对上，并报告“只存在于某一 lane”的 orphan 绑定。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UStruct.cpp`，`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs` |
| 预估工作量 | M |
| 架构风险 | 对 generated accessor、mixin member、global-scope static function 这类“脚本名不等于 Unreal 名”的条目，key 设计必须同时容纳 owner path 和 script-facing name，否则会引入错误聚合。 |
| 兼容性 | 向后兼容。旧 `Binds.Cache`、旧 `BindModules.Cache`、旧 skip config 都可以继续读取；新 key 先作为附加字段出现，等工具链稳定后再逐步淘汰旧 join 逻辑。 |
| 验证方式 | 抽样对 `Bind_UClass_Base::NewObject`、`Bind_Actors::Spawn`、一个 BlueprintCallable reflected function、一个 generated getter、一个 delegate、一个 enum 做 cross-artifact 对账，确认 DB、UHT CSV、runtime bind info、header link 都能输出同一个 canonical key；再模拟重命名 package / class / function，验证 diff 结果只在对应 key 上变化。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-BP-8 | 函数签名合同分裂成手写字符串、runtime descriptor、UHT descriptor 三套模型 | 绑定 IR 收敛 | 高 |
| P1 | Arch-BP-9 | 绑定元素缺少统一稳定 key，跨产物无法直接对齐 | 标识模型收敛 | 高 |

---

## 架构分析 (2026-04-09 00:04)

### Arch-BP-10：绑定准入规则分散在 editor / UHT / runtime，多条 lane 没有共享 eligibility policy

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 哪些 `UClass` / `UFunction` 能进入 binding pipeline，以及跳过原因是否可预测、可复用 |
| 当前设计 | 当前至少有四套独立的准入规则：editor 扫类、runtime `ShouldBindEngineType(UClass)`、runtime `ShouldSkipBlueprintCallableFunction()` / `BindBlueprintEvent()`、UHT `ShouldGenerate()`；它们共享部分 meta，但不是同一个 policy 对象。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1088-1161` — `GenerateBindDatabases()` 只收录非 abstract/deprecated、非 `Private/` header、且至少有一个 `BlueprintCallable` 的 `UClass`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:962-1010` — `ShouldBindEngineType(UClass)` 又按 `CLASS_Native`、editor-only、`NotInAngelscript`、`BlueprintType`、是否存在 `BlueprintCallable` 重新判定；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:83-117` — `ShouldSkipBlueprintCallableFunction()` 再按 `NotInAngelscript`、`BlueprintInternalUseOnly`、`UsableInAngelscript` 和 `UActorComponent::GetOwner` 特例过滤；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp:553-585` — event lane 还有 `DeprecatedFunction`、`UserConstructionScript`、`AllowAngelscriptOverride` 的单独例外；`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:490-515` — UHT `ShouldGenerate()` 还要额外排除 `CustomThunk` 与 `UUniversalObjectLocatorScriptingExtensions` 的硬编码函数。 |
| 优点 | 每条 lane 都能针对自己的约束做精细过滤，短期内很容易补一个例外规则并配自动化测试。 |
| 不足 | 新增一个 UE 类型或函数时，作者很难一次判断它会在哪个阶段被挡住；同一 meta 在不同 lane 的语义也不完全一致，容易出现“UHT 有 entry、runtime 不绑定”或“editor 发现了 class、late pass 仍跳过”的分叉。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | DTS 生成的忽略策略通过 `IPuertsModule::Get().GetIgnoreClassListOnDTS()` 从设置集中读取，生成器在 class/property 生成时复用同一份 ignore list；额外扩展则通过 `UCodeGenerator` / `ICodeGenerator::Execute_Gen()` 显式挂入。 | `Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:246-249`；`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:369-373,861-865,1708-1717`；`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Public/CodeGenerator.h:10-26` | 先把“是否生成”收敛成设置和接口，再让不同生成阶段消费同一套 policy。 |
| UnLua | 一条 lane 是 `Binding.h` 暴露的 `ExportClass/ExportFunction/AddType` 显式导出列表，另一条 lane 是 `ClassRegistry::RegisterReflectedType()` 的按需反射注册；`FLuaEnv` 在 `OnPreStaticallyExport` 之后统一消费导出集合。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/Binding.h:21-35`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Binding.cpp:19-57`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:144-159`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:90-130` | 把“显式导出”和“反射发现”分成清楚的两条 lane，而不是每个阶段各写一套跳过条件。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 引入共享的 `BindingEligibilityPolicy`，把 editor/UHT/runtime 的准入判定收敛成结构化 decision，而不是继续在各处复制 `if` 链。 |
| 具体步骤 | 1. 新增 `FAngelscriptBindingEligibilityDecision` 与 `EAngelscriptBindingLane`，字段至少包含 `bIncluded`、`ReasonCode`、`Lane(EditorClassScan/UhtFunctionTable/RuntimeCallable/RuntimeEvent)`、`SourceMeta`。<br>2. 把 `GenerateBindDatabases()`、`ShouldBindEngineType(UClass)`、`ShouldSkipBlueprintCallableFunction()`、`BindBlueprintEvent()`、UHT `ShouldGenerate()` 的条件提炼到共享 helper，先保持原条件不变，只把判断位置统一。<br>3. 在 `UAngelscriptSettings` 增加 `IgnoredClassNames` / `ForcedFunctionAllowList` 这类配置入口，并预留 delegate/interface 让外部 provider 可以追加 policy，避免继续写硬编码例外。<br>4. 把 decision reason 输出到 `AS_FunctionTable_Entries.csv`、bind dump 和未来 manifest，这样“为什么没绑定”可以直接从产物读出来。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp`，`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`，`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h` |
| 预估工作量 | M |
| 架构风险 | 需要保证现有特例在迁移后逐项保留，否则很容易出现 bind 覆盖率回退；共享 policy 若设计得过于抽象，也可能把 runtime/editor/UHT 的真正差异抹平。 |
| 兼容性 | 向后兼容。现有 meta（`NotInAngelscript`、`BlueprintInternalUseOnly`、`UsableInAngelscript`、`AllowAngelscriptOverride`）与现有硬编码特例可以原样保留，只是决策来源变成统一 helper。 |
| 验证方式 | 复跑现有 `GeneratedFunctionTable`、`BlueprintInternalUseOnlyCanBeOverriddenForAngelscript`、`BlueprintCallableReflectiveFallbackEligibility` 相关测试；新增一个“同一函数在 editor/UHT/runtime 三条 lane 的 decision 一致性”测试，校验输出的 `ReasonCode` 与现有行为完全对应。 |

### Arch-BP-11：native thunk 解析记录过于贫弱，冲突、stub 和 fallback 只有结果没有原因

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `BlueprintCallable` 绑定如何在 direct native、reflective fallback、unresolved 之间切换，以及外部扩展能否安全覆盖已有 entry |
| 当前设计 | 当前 `ClassFuncMaps` 以 `UClass* + FunctionName` 存 `FFuncEntry`，记录内容只有 `FuncPtr`、`Caller` 和 `bReflectiveFallbackBound`；UHT 侧只有 `Direct/Stub`，runtime 侧只有 `bound/not bound/fallback`，没有保存失败原因、来源 provider 或覆盖策略。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/FunctionCallers.h:384-389` — `FFuncEntry` 只有 `FuncPtr`、`Caller`、`bReflectiveFallbackBound` 三个字段；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:497-512` — `AddFunctionEntry()` 对同名项采用 first-write-wins，后续重复注册被静默忽略；`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp:563-584` — 测试明确要求“保留第一份注册，忽略后续 duplicate”；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp:34-48,72-90` — runtime 只按 `Function->GetFName()` 查 entry，找不到就直接返回，direct native 缺失时才尝试 reflective fallback；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp:374-420` — fallback 也只把 `bReflectiveFallbackBound` 置 `true`，不会记录 `RejectedInterfaceClass`、`RejectedCustomThunk` 等原因；`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:29-44,103-139,465-479` — 生成阶段只区分 `DirectBindEntries` 和 `StubEntries`，`ERASE_NO_FUNCTION()` 的具体来源（interface、签名失败、策略跳过）在产物里丢失；`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp:367-398` — 运行时统计也只剩 `DirectCount / ReflectiveCount / UnresolvedCount` 三类。 |
| 优点 | 数据结构非常轻，legacy handwritten entry 与 UHT shard 很容易混用；first-write-wins 还能避免自动生成覆盖现有手写 direct bind。 |
| 不足 | 体系无法回答“为什么 unresolved”“为什么被 fallback”“为什么后来的 provider 没能覆盖一个 `ERASE_NO_FUNCTION()` stub”；未来即使引入外部 binding extension，也无法在不改插件源码的前提下安全替换低质量 entry。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | `FCSharpBind` 先把 `UFunction` 组织成编码后的 `Functions` map，再为每个函数写入 `FieldHash` 并通过 `AddFunctionHash<FUnrealFunctionDescriptor>()` 注册到 environment；解析单元是“具体函数描述符”，不是“类名下一个字符串 entry”。 | `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FCSharpBind.cpp:179-230` | 让 runtime 持有可追踪的 per-function resolution record，后续诊断和覆盖策略可以围绕 descriptor/hash 做。 |
| UnLua | `FunctionRegistry` 以真实 `UFunction*` 作为键缓存 `FFunctionDesc`，`FFunctionDesc` 在构造时就提取参数、返回值、`bInterfaceFunc` 等信息；如果 Lua 侧没有找到实现，还会显式回落到 `Overridden->Invoke()`。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/FunctionRegistry.cpp:35-80`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:31-76` | 缺失实现、接口函数、参数形状这些信息都挂在函数描述符上，而不是压缩成一个布尔位。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 `FFuncEntry` 升级成带状态与来源的 `FunctionResolutionRecord`，并引入显式 collision / override policy；旧 first-write-wins 作为默认兼容模式保留。 |
| 具体步骤 | 1. 新增 `EAngelscriptFunctionResolutionState`，至少覆盖 `DirectNative`、`ReflectiveFallback`、`RejectedInterface`、`RejectedCustomThunk`、`RejectedUnsupportedSignature`、`MissingEntry`、`ShadowedByEarlierProvider`。<br>2. 把 `FFuncEntry` 扩展为记录 `ProviderName`、`SourceArtifact(Manual/UHT/RuntimeFallback)`、`ReasonCode`、`Priority`；`AddFunctionEntry()` 增加可选 policy，例如 `KeepFirst`、`ReplaceStubWithDirect`、`ReplaceLowerPriority`，默认仍是 `KeepFirst`。<br>3. 让 UHT generator 在生成 `ERASE_NO_FUNCTION()` 时同步写出具体原因，不再只落 `Stub`；runtime `Bind_BlueprintCallable()` 在 entry 缺失、fallback eligibility 被拒绝或声明已冲突时，也把原因写回 bind state。<br>4. 扩展 dump/CSV/tests，把每个 unresolved/fallback 条目都能追到原因和来源 provider；这样将来外部扩展 provider 可以只替换 `Stub` 或 `Rejected*` 条目，而不必粗暴覆盖全部同名函数。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/FunctionCallers.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`，`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`，`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | M |
| 架构风险 | 如果 override policy 定义不严谨，可能让自动生成条目意外覆盖已有手写 bind；同时 generator/runtime 双写状态时也要防止 reason 不一致。 |
| 兼容性 | 向后兼容。默认仍可保持 first-write-wins 与现有 `FFuncEntry` 行为；新增状态字段和 policy 先作为附加能力存在，不改变已有脚本声明与现有手写绑定的默认执行结果。 |
| 验证方式 | 复跑现有 duplicate、overload、inline、reflective fallback 统计测试；新增一组 provider-collision 测试，验证“后来的 direct bind 可以替换早先的 stub，但不能无提示覆盖已有 handwritten direct bind”；再抽样检查 CSV/dump 是否能给出 `RejectedCustomThunk`、`RejectedInterface`、`ShadowedByEarlierProvider` 等具体原因。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P0 | Arch-BP-11 | native thunk 解析缺少原因、来源与覆盖策略 | 解析状态模型收敛 | 高 |
| P1 | Arch-BP-10 | editor/UHT/runtime 准入规则分散，缺少统一 eligibility policy | 准入策略收敛 | 高 |

---

## 架构分析 (2026-04-09 00:12)

### Arch-BP-12：wrapper/value 类型的 authoring surface 仍是 copy-expand，新增一个 UE 类型要跨 5 个接入面

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `TSubclassOf`、`TObjectPtr`、`TWeakObjectPtr` 这类非直接 reflected `UClass` 类型的新增成本与自动化阻力 |
| 当前设计 | reflected `UClass` 可以走 `Bind_BlueprintType` 的扫描主链，但 wrapper/value 类型仍要把“脚本声明”“`FAngelscriptType` 语义”“late bind 方法”“`RegisterTypeFinder` 回查”“个别函数元数据补丁”分别写在不同位置。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1514-1541` — `Bind_TSubclassOf_Declaration` 先注册模板声明与 `TemplateCallback`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1543-1726` — `FSubclassOfType` 继续单独实现 `CreateProperty`、`MatchesProperty`、`SetArgument`、`GetReturnValue`、`DefaultValue_*`、`GetCppForm`、debugger 逻辑；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1756-1777` — `Bind_TSubclassOf` 再补 late methods；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:2028-2418` — `TObjectPtr` / `TWeakObjectPtr` 以相同模式复制一遍 declaration + late bind + type subclass；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:2420-2517` — `BindUClassLookup()` 还要显式 `Register()` 三个 type 并追加 `RegisterTypeFinder()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp:556-579` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp:450-467` — 关联这些 wrapper 的工厂函数还要手动补 `SetPreviousBindArgumentDeterminesOutputType()`。 |
| 优点 | 每个 family 的运行时语义都能写得很精细，`FAngelscriptType` 可以直接控制 `FProperty` 形状、参数搬运、默认值转换和 debugger。 |
| 不足 | authoring surface 被拆成多个必须同步维护的片段；新增一个新的 UE wrapper（例如 `TSoftObjectPtr` 或 `TScriptInterface` 的更完整脚本形态）时，扩展者需要先理解 declaration、type-object、late bind、type finder、factory metadata 五个面，自动生成也很难只对其中一个面生效。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | editor 端先统一跑 `Generator()` 主链；属性级别的 wrapper 映射集中在 `FGeneratorCore::GetPropertyType()` / `GetPropertyTypeNameSpace()`，直接把 `FClassProperty`、`FWeakObjectProperty`、`FSoftClassProperty`、`FSoftObjectProperty` 等收敛成宿主语言类型名和 namespace 规则，runtime `FCSharpBind` 只负责把生成物里的 `__Field` / `__Function` hash 回填。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:237-305`；`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:82-250,279-430`；`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FCSharpBind.cpp:143-223` | wrapper family 的“类型名推导”和“运行时回填”是两层稳定协议，不需要每个 wrapper 都自己重写一套声明 + late bind + 回查 wiring。 |
| UnLua | `FLuaEnv` 创建时挂起 `ClassRegistry` / `FunctionRegistry` / `PropertyRegistry`；`PropertyRegistry::CreateTypeInterface()` 先按 Lua 栈值或 metatable 找到 `UField`，再由 `GetFieldProperty()` 统一构造对应 `FProperty`/`ITypeInterface`，class metatable 缺失时也会走 `RegisterReflectedType()` 动态补齐。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:104-159`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/PropertyRegistry.cpp:25-85,316-360`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:59-145` | 先把“field shape -> type interface”的桥接做成 registry，再由 registry 驱动具体类型，不把 wrapper family 的 glue 分散在多个 bind 文件块里。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留 `FAngelscriptType` 能力的前提下，加一层 `TemplateTypeAdapterDescriptor` / `WrapperTypeDescriptor`，把 declaration、type-object、late bind、type finder 统一从一个描述对象派生。 |
| 具体步骤 | 1. 在 `AngelscriptType` 或新的 `BindingPipeline` 目录定义 `FAngelscriptTemplateTypeAdapterDescriptor`，最少包含 `TemplateName`、`CanAcceptSubType`、`CreateProperty`、`MatchProperty`、`BindDeclaration`、`BindRuntimeMethods`、`BuildCppForm`、`BuildDebuggerValue`。<br>2. 提供通用注册入口，例如 `RegisterTemplateObjectWrapper(Descriptor)`，内部一次性完成 early declaration、`FAngelscriptType` 注册、late bind、`RegisterTypeFinder()` 挂接。<br>3. 先把 `TSubclassOf` 迁到 descriptor 模式作为试点，再迁 `TObjectPtr` / `TWeakObjectPtr`，比较迁移前后重复代码行数和新增测试规模。<br>4. 在第二阶段扩展到 `TSoftObjectPtr`、`TSoftClassPtr`、`TScriptInterface` 这类当前尚未体系化的 wrapper family；已有 `Bind_UObject` / `Bind_AActor` 的 `ArgumentDeterminesOutputType` 特例则改成 descriptor 可声明的 function metadata，而不是散落在业务 binder。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`，以及新增的 wrapper descriptor 头/实现 |
| 预估工作量 | L |
| 架构风险 | 如果 descriptor 抽象设计过窄，后续遇到需要自定义 GC/debugger/默认值转换的 family 仍会被迫逃逸回手写特例；如果设计过宽，又可能把原本直接可读的 `FAngelscriptType` 代码包进难理解的 callback 表。 |
| 兼容性 | 向后兼容。第一阶段只新增 descriptor helper，并把 `TSubclassOf`、`TObjectPtr`、`TWeakObjectPtr` 做双轨实现或逐个迁移；旧 `Bind_*.cpp` 入口和已有脚本声明不必一次性删除。 |
| 验证方式 | 对 `TSubclassOf`、`TObjectPtr`、`TWeakObjectPtr` 各补一组 round-trip 测试，覆盖 `CreateProperty` / `MatchesProperty` / argument / return / debugger / `GetCppForm`；再新增一个 descriptor-only 的试验类型，验证无需手工追加 late bind 和 `RegisterTypeFinder()` 也能完整工作。 |

### Arch-BP-13：外部自定义 provider 目前只能注入 live bind，进不了 `BindDB`、`BindModules` 和 declaration artifact 管线

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 插件用户在“不修改插件源码”的前提下注册自定义绑定后，能否同时获得 cook/runtime、IDE 与生成 sidecar 的完整支持 |
| 当前设计 | C++ 扩展者确实可以利用公开头文件调用 `FBind`、`FAngelscriptType::Register()`、`RegisterTypeFinder()`；但 editor 生成的 `Binds.Cache` / `BindModules.Cache` 以及 UHT 的 `AS_FunctionTable_*` 都是插件内建扫描与生成流程，各自只认识自己的输入源。结果是外部 provider 最多能把 bind 注入 live runtime，却无法自然进入现有 artifact 管线。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs:15-18` — `ModuleDirectory` 和 `Core` 被公开到 `PublicIncludePaths`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:438-483` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:71-81` — `FBind`、`Register()`、`RegisterTypeFinder()` 是 public API；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:999-1077` — `GenerateNativeBinds()` 只根据 `GetRuntimeClassDB()/GetEditorClassDB()` 生成 `ASRuntimeBind_*` / `ASEditorBind_*` 模块并保存到 `BindModules.Cache`；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1081-1161` — `GenerateBindDatabases()` 仅扫描 `TObjectRange<UClass>()`，按 header 路径和 `BlueprintCallable` 条件分组；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1285-1328` — 生成模块只是写一个 `StartupModule()->RegisterBinds()` 包装；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1466-1488,1915-1921` — runtime 只加载插件目录里的 `Binds.Cache` / `BindModules.Cache` 然后统一 `CallBinds()`；`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:51-78,449-515` — `AS_FunctionTable_*` 仅遍历 UHT session modules 和 `BlueprintCallable` UHT 函数，完全不知道外部 runtime provider。 |
| 优点 | 插件自己的 reflective bind、generated bind module 和 UHT function table 都有稳定的内建生成入口，不需要先设计复杂的第三方协议。 |
| 不足 | “能注册 bind”与“能被完整工具链消费”是两件事。当前外部 provider 即使成功注入 runtime，也不会自动进入 `Binds.Cache`、`BindModules.Cache`、`AS_FunctionTable_Summary.json`、`AS_FunctionTable_Entries.csv`，IDE/覆盖率/诊断看到的仍是插件内建视角；纯脚本开发者则更没有路径把自定义 API 变成可审计 artifact。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `FTypeScriptDeclarationGenerator` 维护 `ExtensionMethodsMap`、`BlueprintTypeDeclInfoCache` 和统一的 `ue.d.ts / ue_bp.d.ts` 输出；生成结束前还会枚举所有实现了 `UCodeGenerator` 的类并执行 `ICodeGenerator::Execute_Gen()`，把 declaration 扩展点做成正式 interface。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Public/TypeScriptDeclarationGenerator.h:33-131`；`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:360-457,1110-1172,1708-1717`；`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Public/CodeGenerator.h:10-27` | runtime 之外还有一条显式的 artifact extension lane，第三方扩展不需要篡改主插件源码就能参与声明生成。 |
| UnrealCSharp | editor 模块把 solution、code analysis、class/struct/enum/asset generator、binding generator 和 compiler 串成单一 `Generator()` 主链；生成物随后由 runtime `FCSharpBind` 回填 hash。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:237-305`；`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FCSharpBind.cpp:143-223` | 至少“脚本可见合同”和“runtime 回填”在同一条 pipeline 里，不会出现运行时有 bind、离线 artifact 却完全无感知的分裂状态。 |
| UnLua | `Binding.h` 公开 `ExportClass`、`ExportEnum`、`ExportFunction`、`AddType`；`Binding.cpp` 把它们收进 exported registry，`FLuaEnv` 构造时统一注册这些导出对象。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/Binding.h:21-35`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Binding.cpp:34-57`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:144-159` | 即使没有独立 UHT sidecar，也把外部扩展定义成正式的 export 集合，而不是只有“你可以 include 一个头试试看”。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把“runtime 注册协议”和“artifact 贡献协议”拆开设计：先允许外部 provider 描述自己的 binding contract，再让 editor/UHT/runtime 共同消费这份描述。 |
| 具体步骤 | 1. 在 `AngelscriptRuntime` 新增 `IAngelscriptBindingProvider` 的 artifact 面，例如 `DescribeBindings()` / `DescribeGeneratedFunctions()` / `DescribeHeaders()`，返回结构化 manifest，而不是只暴露 lambda 注册入口。<br>2. editor `GenerateBindDatabases()` 不再只依赖 `TObjectRange<UClass>()`，而是先收集内建 reflective provider 和外部 provider 的 manifest，再决定写入 `Binds.Cache`、`BindModules.Cache` 与 header link。<br>3. UHT generator 保持现有 UHT 扫描逻辑不变，但增加一个 merge 步骤，把 provider manifest 生成的 supplemental entries 合并进 `AS_FunctionTable_Summary.json` / `AS_FunctionTable_Entries.csv`，至少让“外部 direct bind / stub / skip reason”可见。<br>4. docs/dump/未来 manifest 统一写出 `ProviderName`、`ArtifactSource(RuntimeManual/EditorReflective/UHTGenerated/ExternalProvider)`；这样项目侧 provider 注册后，不仅 runtime 可用，IDE、覆盖率和诊断也能看到它。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`，`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`，以及新增的 provider manifest 头/实现 |
| 预估工作量 | L |
| 架构风险 | UHT 进程拿不到运行时 lambda，本方案必须坚持“provider 产出声明性 manifest，UHT 只合并 manifest”而不是试图在 UHT 中直接执行 provider 代码；否则会把构建阶段和运行时阶段耦死。 |
| 兼容性 | 向后兼容。第一阶段可以把现有 reflective scan、`BindModules.Cache` 生成和 UHT generator 都包装成默认 provider；旧 `FBind` 路径不变，只是新增 artifact 描述能力。项目侧如果不实现 provider manifest，行为与今天一致。 |
| 验证方式 | 新建一个独立项目模块或辅助插件，实现一个最小 `IAngelscriptBindingProvider`：注册 1 个 custom type、1 个 global function、1 条 supplemental function-table entry；验证不改 `Angelscript` 插件源码的情况下，runtime 能调用、`Binds.Cache`/`BindModules.Cache` 能看到 provider、`AS_FunctionTable_Entries.csv` 能出现 supplemental 行，且 bind dump/manifest 能显示 `ProviderName`。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P0 | Arch-BP-13 | 外部 provider 只能注入 live bind，无法进入现有 artifact 管线 | 扩展协议补全 + 工具链接入 | 高 |
| P1 | Arch-BP-12 | wrapper/value 类型新增需要跨 declaration/type-object/late bind/type finder/metadata 五个面 | authoring surface 收敛 | 高 |

---

## 架构分析 (2026-04-09 00:23)

### Arch-BP-14：`Bind_BlueprintType` 在 `AS_USE_BIND_DB` 双轨下维护两套 class discovery/bind 实现

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | reflective `UClass` 绑定在 live scan 与 bind-db 两条 lane 上是否共享同一份实现 |
| 当前设计 | `Bind_BlueprintType.cpp` 在 `#if AS_USE_BIND_DB` 与 `#elif !AS_USE_BIND_DB` 下分别定义了同名的 `Bind_BlueprintType_Declarations` / `Bind_Defaults`。前者消费 `FAngelscriptBindDatabase::Get().Classes`，后者直接扫描 `TObjectRange<UClass>()` 并重新构造 `DBBind`。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:705-725,727-885` — bind-db lane 先从 `Binds.Cache` 解析出的 `Classes` 做 early declaration，再按缓存的 `Methods/Properties` 做 late bind；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:887-1045,1317-1509` — live lane 又独立执行 `ShouldBindEngineType()`、函数/属性扫描、generated accessor 处理，并把结果重新写回 `FAngelscriptBindDatabase::Get().Classes`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp:27-31,103-115` — bind DB 本身只是序列化 `Structs/Classes` 并在 load 失败时 hard fail，因此 cooked/editor 的 reflective `UClass` 行为实际上依赖这两套实现保持一致。 |
| 优点 | bind-db lane 让 cooked/runtime 可以在缺少完整 editor metadata 时仍复现 reflective `UClass` 绑定；live lane 则保留了 editor 里直接从当前反射状态重建 cache 的能力。 |
| 不足 | 绑定准入、函数挑选、property accessor 生成、deprecated/editor-only trait 处理都在两个分支重复维护。新增一个 reflective `UClass` 规则或修一个 accessor 问题时，维护者需要同时理解并改动两条 lane，否则 editor/live 与 cooked/bind-db 的行为会悄悄漂移。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | `FUnrealCSharpEditorModule::Generator()` 只有一条生成主链，`FClassGenerator` / `FStructGenerator` 都复用 `FGeneratorCore::GetPropertyType()` 这类共享映射函数；runtime `FCSharpBind` 只做生成物回填，而不是再写一套第二版 class discovery。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:237-305`；`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp:166-176`；`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:82-250`；`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FCSharpBind.cpp:143-223` | 把“发现/描述 UE 类型”和“runtime 应用描述”拆成两个阶段，但 discovery 逻辑只维护一份。 |
| puerts | `FTypeScriptDeclarationGenerator` 把输出缓存、扩展方法与 Blueprint 声明缓存都挂在同一个 generator 对象上；第三方扩展通过 `UCodeGenerator` 接口接入同一轮生成，而不是另起一套分支。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Public/TypeScriptDeclarationGenerator.h:33-131`；`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:360-457,1708-1717`；`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Public/CodeGenerator.h:10-27` | 即便存在缓存和扩展，仍坚持一个 authoritative generator context。 |
| UnLua | `Binding.cpp` 把导出类/函数/枚举统一收进 `FExported`，`FLuaEnv` 创建时只消费这一个 exported registry；没有“editor 一套、runtime 另一套”的 reflected export 实现。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Binding.cpp:19-57`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:144-159` | 先把导出集合建成稳定 registry，再让运行时消费 registry，而不是复制注册逻辑。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 抽出共享的 `ReflectedClassBindingDescriptor` 构建层，让 live reflection 和 bind-db 只替换“数据源”，不再复制 bind 逻辑。 |
| 具体步骤 | 1. 在 `Bind_BlueprintType.cpp` 相邻目录或新的 `BindingPipeline/` 目录提炼 `FReflectedClassBindingDescriptor`，统一承载 `TypeName`、`UnrealPath`、`ResolvedClass`、`Methods`、`Properties`、generated accessor 元信息。<br>2. 把 `ShouldBindEngineType()`、函数扫描、property 扫描、`BindFunctionWithAdditionalName()`、`BindProperties()` 里和“描述构建”相关的部分抽成共享 helper；`AS_USE_BIND_DB` 分支只负责“从 cache 反序列化 descriptor”，`!AS_USE_BIND_DB` 分支只负责“从 `TObjectRange<UClass>()` 构建 descriptor”。<br>3. 让两个 `Bind_BlueprintType_Declarations` / `Bind_Defaults` 入口都只消费 descriptor 数组，保留现有宏开关和调用时序，不一次性推翻 bind-db 机制。<br>4. 在 test/editor 下增加一个 parity audit：对同一批 `UClass` 同时跑 live descriptor 构建和 DB round-trip，比较 `TypeName`、method/property 数量、generated getter/setter 标记和 editor-only/deprecated trait 是否一致。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp`，以及新增的 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BindingPipeline/*` 或等价共享 helper 文件 |
| 预估工作量 | M |
| 架构风险 | 共享 descriptor 后，排序、generated accessor 命名和 trait 写回时机可能与旧分支出现细微差别；若没有 parity test，容易把“只是重构”变成行为改动。 |
| 兼容性 | 向后兼容。`AS_USE_BIND_DB`、`Binds.Cache` 格式和现有 bind 入口都可先保留；第一阶段只是把双轨实现收束到共享 descriptor builder，不改变现有脚本 API。 |
| 验证方式 | 选取 `UObject`、`AActor`、一个带 `BlueprintGetter/Setter` 的类和一个 editor-only 类做样本，比较 live-scan 与 DB-lane 的 descriptor 摘要；再复跑已有 bind/config 相关自动化测试，确认 cooked 与 editor 下的 reflective class bind 结果一致。 |

### Arch-BP-15：`BindModules.Cache` 的产物根目录在 editor 生成与 runtime 加载之间分裂

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | generated bind module 列表的 artifact ownership 是否有单一、可预期的根目录 |
| 当前设计 | editor `GenerateNativeBinds()` 把 `BindModules.Cache` 写到 `GetScriptRootDirectory()`，而 runtime 初始化时却从 `Angelscript` 插件根目录读取 `BindModules.Cache`；与此同时，生成出来的 `ASRuntimeBind_*` / `ASEditorBind_*` 源码又被写进插件 `Source/` 目录。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:781-794` — `GetScriptRootDirectory()` 明确返回 `AllRootPaths[0]`，注释写明“第一个 root 是 game project root”；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1077` — editor 生成阶段把 `BindModules.Cache` 保存到 `GetScriptRootDirectory() / "BindModules.Cache"`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1466-1478` — runtime 先从 script root 读 `Binds.Cache`，再从 `plugin->GetBaseDir() / "BindModules.Cache"` 读取 bind module 列表；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1181-1187` — `GenerateNewModule()` 通过 `FindModulePath(\"AngelscriptRuntime\")` 推导插件 `Source/` 根并把生成模块写到插件目录。 |
| 优点 | 生成出来的 `ASRuntimeBind_*` / `ASEditorBind_*` 源码天然落在插件自己的 `Source/` 目录，符合“绑定模块属于插件代码”的直觉；`Binds.Cache` 继续待在 script root，也保留了与脚本工程同根的工作流。 |
| 不足 | `BindModules.Cache` 没有单一 authoritative root。editor 写的是 project script root，runtime 读的是 plugin root，generated source 又在 plugin `Source/`；这让 host project 切换、插件独立交付和缓存回收都缺少明确契约，存在“生成了一份、加载了另一份”或“迁移宿主工程后仍沿用旧列表”的结构风险。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | declaration 生成完成后会先清理插件内旧 `Typing/ue/*.d.ts`，再把 project `Typing` 目录显式复制回插件 `Typing`，随后把新的 `ue.d.ts / ue_bp.d.ts` 保存到 project 根；双根存在，但同步动作是显式的，不是“写一处、读另一处”。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:417-457` | 若必须同时服务 project root 与 plugin root，就把同步规则做成显式步骤而不是隐式假设。 |
| UnrealCSharp | `Generator()` 把生成、编译和完成广播串成一条连续工作流，生成物与消费阶段之间没有单独的第二个“猜路径”加载步骤。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:237-309` | 让 artifact 生产和消费尽量处于同一条 workflow，可减少“文件在哪个根目录才算 authoritative”这类歧义。 |
| UnLua | 导出类/函数/枚举全部先进入进程内 `FExported` registry，`FLuaEnv` 构造时直接消费该 registry；不需要额外磁盘 cache 去桥接 editor/runtime。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Binding.cpp:19-57`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:144-159` | 对必须长期存在的 artifact 才落盘，能不跨根目录的注册信息就不要做成隐式文件协定。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 为 bind pipeline 增加共享的 artifact locator，并让 `BindModules.Cache` 先进入 dual-read/dual-write 过渡，再收敛到单一 authoritative root。 |
| 具体步骤 | 1. 在 runtime/editor 共用位置新增 `GetBindingArtifactPath(EAngelscriptBindingArtifactKind)` 或等价 helper，至少统一 `Binds.Cache`、`BindModules.Cache`、generated module root 三类路径的来源，不再让 editor/runtime 各自拼字符串。<br>2. 第一阶段保持兼容：editor 生成时同时写 project script root 与 plugin root 两份 `BindModules.Cache`，runtime 则按“authoritative root -> legacy fallback”顺序尝试加载，并记录日志说明实际命中的路径。<br>3. 第二阶段结合插件交付目标，把 `BindModules.Cache` 固定为 plugin-side artifact，或反之固定为 script-side artifact，但必须在 helper 和文档里明确；若选择 plugin-side，则保留 script-side 镜像仅用于旧流程兼容。<br>4. 在 cache 内增加最小元数据（例如 schema version、generated timestamp、source root tag），为后续 stale-cache 检查和 delivery audit 提供依据。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`，`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`，以及新增的 artifact locator/helper 文件 |
| 预估工作量 | M |
| 架构风险 | dual-write 过渡期如果缺少日志和优先级规则，反而可能让“到底读了哪一份”更难追；因此必须先定义 authoritative root 和 fallback 次序，再上线兼容层。 |
| 兼容性 | 向后兼容。第一阶段通过双写双读保留现有路径；现有 `Binds.Cache` 与已生成模块不需要立刻迁移，只是把路径选择显式化。 |
| 验证方式 | 1. 生成 bind modules 后分别删除 plugin-root 与 script-root 的 `BindModules.Cache`，验证 runtime 能按预期主次顺序加载并输出命中日志。<br>2. 在新的 host project 中引用同一插件，验证 regenerate 后加载到的是新宿主对应的 module list，而不是旧项目遗留列表。<br>3. 复跑生成绑定和引擎启动流程，确认 `ASRuntimeBind_*` / `ASEditorBind_*` 模块实际加载集合与 cache 内容一致。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P0 | Arch-BP-15 | `BindModules.Cache` 在 project root / plugin root / generated module root 之间缺少统一 artifact ownership | 路径契约收敛 + dual-read/dual-write 兼容层 | 高 |
| P1 | Arch-BP-14 | `Bind_BlueprintType` 的 bind-db lane 与 live-scan lane 维护两套 reflective `UClass` 绑定实现 | 双轨收束到共享 descriptor builder | 高 |

---

## 架构分析 (2026-04-09 00:33)

### Arch-BP-16：属性类型发现是 append-order 链，adapter 优先级与覆写关系没有显式合同

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `FProperty -> FAngelscriptTypeUsage` 的解析是否有稳定、可扩展的优先级模型 |
| 当前设计 | `FAngelscriptType::RegisterTypeFinder()` 与 `FAngelscriptType::Register()` 都只是把 finder/type 追加到数组；`GetByProperty()` 和 `FromProperty()` 运行时按注册顺序短路匹配，先命中的 finder/type 获胜。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:54-99` — `Register()` 只把可 `CanQueryPropertyType()` 的 type 追加到 `TypesImplementingProperties`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:114-118,147-171,236-248` — `RegisterTypeFinder()` 仅 `Add(Finder)`，`GetByProperty()`/`FromProperty()` 都按数组顺序查询并在首个成功匹配时返回；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp:1550-1565` — `TArray` 自己注册一段 finder lambda；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:2420-2518` — `BindUClassLookup()` 把 `FWeakObjectProperty`、`FObjectProperty`、`FClassProperty`、`TSubclassOf` 和普通 `UObject` fallback 混在同一个 finder 里；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h:56-87` — `FAngelscriptMethodBind`/`FAngelscriptPropertyBind` 没有记录“最终由哪个 adapter/type finder 命中”。 |
| 优点 | 进入门槛低，新增一个 wrapper/container family 只要 `RegisterTypeFinder()` 或实现 `MatchesProperty()` 就能接进主链。 |
| 不足 | 缺少 `Priority`、`PropertyKind`、`ProviderName` 或 override 语义，第三方 provider 想覆盖内建规则只能赌静态初始化顺序；一旦 finder 写得过宽，就会把后续更具体的 rule 吞掉，而且 dump/cache 也看不到到底是哪个 adapter 负责了最终映射。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FPropertyDesc::Create()` 用集中式 `switch(Type)` 决定 `FPropertyDesc` 子类；`FFunctionDesc` 构造时对每个参数都统一走这条工厂。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1537-1658`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:31-76` | 先把 property 分类工厂收口成单一入口，再让函数/registry 复用它，优先级与覆盖关系不会散落在各个 bind 文件的注册顺序里。 |
| UnrealCSharp | `FGeneratorCore::GetPropertyType()` 集中处理 `FClassProperty`、`FObjectProperty`、`FInterfaceProperty`、容器和 wrapper family，generator 统一消费这条映射。 | `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:82-272` | 把“属性长什么样”与“生成什么脚本类型”绑定成共享 helper，而不是让每个 family 在不同注册点各自判断。 |
| puerts | `GenTypeDecl()` 递归处理 `Array/Set/Map/Object/Struct/Enum`，输出 `.d.ts` 时只有一条类型推导路径。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:758-876` | 即使目标只是 declaration，也坚持集中式递归映射，避免多处 lambda 对同一 `FProperty` 重复判定。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 append-only 的 `TypeFinder` 升级为显式 `PropertyAdapter` registry，让优先级、provider 来源和命中结果都可观测。 |
| 具体步骤 | 1. 在 `Core/AngelscriptType.h` 定义 `FAngelscriptPropertyAdapterDescriptor`，至少包含 `AdapterName`、`Priority`、`ProviderName`、`CanHandle(Property)`、`BuildUsage(Property, Usage)`。<br>2. 让现有 `RegisterTypeFinder()` 成为兼容层：内部把 lambda 包装成默认 `Priority=0` 的 adapter；旧 bind 文件先不改。<br>3. 把 `GetByProperty()` / `FromProperty()` 改成先按 `Priority`、再按 `ProviderName/AdapterName` 稳定排序执行，并在 debug dump/manifest 中输出最终命中的 adapter。<br>4. 先把 `BindUClassLookup()`、`Bind_TArray`、`Bind_TMap`、`Bind_TSet`、`Bind_TOptional` 迁到 descriptor 形式，验证“内建 object finder”与“第三方 wrapper finder”可以通过显式优先级共存，而不是靠初始化时机碰运气。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSet.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TOptional.cpp`，以及相关 type tests/dump 文件 |
| 预估工作量 | M |
| 架构风险 | 一旦显式化优先级，某些今天“靠注册顺序恰好工作”的 family 可能暴露出冲突；需要先补 adapter trace，避免把重构变成静默行为变化。 |
| 兼容性 | 向后兼容。旧 `RegisterTypeFinder()` 和已有 `FAngelscriptType` 子类都能保留，第一阶段只增加 descriptor registry 与观测面，不强制重写全部 bind 文件。 |
| 验证方式 | 1. 为 `FObjectProperty`、`FWeakObjectProperty`、`FClassProperty(CPF_UObjectWrapper)`、`FArrayProperty`、`FMapProperty` 建立 adapter resolution 测试，确认命中 adapter 可预测。<br>2. 增加一个测试用外部 provider，以更高优先级覆盖某个 built-in wrapper rule，验证无需依赖静态初始化顺序。<br>3. 导出 bind dump/manifest，确认每个 property 映射都能看到 `AdapterName/ProviderName`。 |

### Arch-BP-17：泛型参数约束与 per-type augmentation 没有可序列化合同，自动化只能停在基础签名

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 复杂 helper API 和“按类型自动增生”的脚本表面是否有共享 descriptor，还是只能写手工 runtime lambda |
| 当前设计 | 基础函数签名可以落到 `Declaration` 字符串，但更高阶的能力仍依赖手写 C++ 逻辑：generic `?&` 参数要在 lambda 里手动解析 `TypeId/templateSubTypes/plainUserData`，按类型增生的 namespace/static helper 则通过 `FNamespace` 和 `PreviousBindPassScriptFunctionAsFirstParam()` 这类 side effect 现场生成。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp:39-149` — `GetComponentsByClass(?& OutComponents)` 手工读取 `TypeId`、校验 `templateBaseType`、取 `templateSubTypes[0].plainUserData` 得到 `UClass*`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp:286-312` — `Bind_Actors` 遍历每个已绑定 `AActor` 类型，在 `ClassName` namespace 下动态生成 `Spawn()`，再用 `PreviousBindPassScriptFunctionAsFirstParam()` 把目标类塞给脚本函数；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp:317-390,450-467` — `GetAllActorsOfClass()` 和 `SpawnActor()` 继续分别手写 generic out-array 约束与 return-type coupling；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp:135-190,556-580` — `opCast(?&)`/`NewObject()` 同样直接操作 `TypeId` 与 `SetPreviousBindArgumentDeterminesOutputType()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:684-694` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp:100-116` — `StaticClass()` 和 static BlueprintCallable namespace 函数都是 runtime augmentation；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h:56-87` — `FAngelscriptMethodBind` 只保存 `Declaration/ScriptName/WorldContext/DeterminesOutputType` 等基础字段，没有“generic 参数约束”“namespace augmentation 来源”“script-function capture mode”一类结构化合同。 |
| 优点 | 功能表达力很强，绑定作者可以直接利用 AngelScript runtime 细节实现 `Spawn`、`opCast`、generic out-array 这类高阶 helper，而不必先设计一套通用 IR。 |
| 不足 | 自动化上限被卡在“基础签名”和“普通 reflective function”层面；一旦是 `?&` 泛型参数、按类型自动生成的 namespace helper 或返回类型依赖输入类型的工厂函数，就必须写懂 AngelScript internals 的 C++ binder。更关键的是，这些 augmentation/generic contract 无法自然进入 bind DB、UHT sidecar、IDE manifest 或未来外部 provider 协议。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | `FClassGenerator` 在生成方法声明时统一处理参数、`out/ref`、默认值和返回类型；`FBindingClassGenerator` 再生成固定 ABI stub，复杂参数语义不需要每个 helper 自己在 runtime 重新解析脚本类型。 | `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp:436-540`；`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FBindingClassGenerator.cpp:743-810` | 把“函数长什么样”和“底层如何调用”拆成两层显式资产，高阶函数语义先进入生成物，再由 runtime 消费。 |
| puerts | `FTypeScriptDeclarationGenerator` 维护 `ExtensionMethodsMap`、`FunctionOutputs`、`BlueprintTypeDeclInfoCache`；生成阶段还会执行实现了 `UCodeGenerator` 的扩展类，把 extension method/augmentation 也变成声明合同的一部分。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Public/TypeScriptDeclarationGenerator.h:33-70`；`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1157-1171,1708-1717` | 先把 augmentation/extension 记录成结构化输出，再统一写入 declaration；第三方扩展也有正式入口，而不是只能在 runtime 临时拼接。 |
| UnLua | `ExportClass`/`ExportFunction`/`AddType` 把扩展先放进 exported registry，`FLuaEnv` 初始化时统一注册；函数调用则由 `FFunctionDesc` + `FPropertyDesc` 描述参数，而不是每个 helper 都自己解析脚本 VM 类型信息。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/Binding.h:21-31`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Binding.cpp:34-57`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:144-159`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:31-76` | augmentation 和参数语义都先进入 registry/descriptor，再由 env 统一消费，扩展与运行时调用面不会完全绑死在手写 lambda 上。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在现有 declaration/metadata 之上补一层 `Invocation/Augmentation Descriptor`，把 generic 参数约束、return-type coupling 和 per-type namespace helper 变成可复用合同。 |
| 具体步骤 | 1. 在 `AngelscriptRuntime` 新增 `FAngelscriptInvocationContract`，字段至少包含 `GenericParamKinds`、`TemplateBaseRequirement`、`RequiredBaseClass`、`OutputTypeSourceArg`、`NeedsScriptFunctionFirstParam`、`WorldContextSource`。<br>2. 再定义 `FAngelscriptAugmentationDescriptor`，描述“把哪个函数挂到哪个 namespace/type 上”，至少包含 `TargetTypePattern`、`NamespaceName`、`Declaration`、`InvocationContractId`、`SourceProvider`。<br>3. 第一阶段只包装现有高频模式：`GetComponentsByClass`/`GetAllActorsOfClass` 的 `TArray<Subclass>` out-arg 约束、`NewObject`/`SpawnActor` 的 output-type coupling、`StaticClass()`/`ActorType::Spawn()` 的 per-type namespace augmentation；运行时仍可调用原 lambda，但 metadata/manifest/UHT 先写出同一份 descriptor。<br>4. 第二阶段让 `Bind_BlueprintCallable`、`Bind_BlueprintType`、外部 provider 协议都能产出 augmentation descriptor，使 IDE manifest、bind DB、UHT summary、docs dump 可以看到“这个 API 是普通 method、generic helper，还是 per-type augmentation”。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp`，以及 editor/UHT sidecar 对应的 manifest 输出文件 |
| 预估工作量 | L |
| 架构风险 | 如果一开始就试图把所有手写 helper 抽象成万能 contract，descriptor 会很快膨胀；更稳妥的做法是先只收口 3 到 4 类重复模式，保留逃逸口给少数真正特殊的 binder。 |
| 兼容性 | 向后兼容。现有 declaration 字符串、`SetPreviousBindArgumentDeterminesOutputType()`、`PreviousBindPassScriptFunctionAsFirstParam()` 和手写 lambda 都可继续存在；descriptor 先作为附加产物与新 API，后续再逐步把高频 helper 迁过去。 |
| 验证方式 | 1. 为 `GetComponentsByClass`、`GetAllActorsOfClass`、`NewObject`、`SpawnActor`、`StaticClass()`、`ActorType::Spawn()` 生成 descriptor snapshot，确认 generic/augmentation 信息能稳定落盘。<br>2. 复跑现有 Actor/UObject 相关自动化测试，确认脚本行为不变。<br>3. 新增一个项目侧示例 provider，用 descriptor 方式声明一个“按类型增生的 namespace helper”，验证无需直接解析 `asCTypeInfo` 也能进入 runtime + manifest。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-BP-16 | property-to-type 解析依赖注册顺序，缺少 adapter 优先级与 provider 可观测性 | 解析模型显式化 | 高 |
| P1 | Arch-BP-17 | generic helper 与 per-type augmentation 没有可序列化合同，自动化停在基础签名 | 高阶 bind contract 收敛 | 高 |

---

## 架构分析 (2026-04-09 00:44)

### Arch-BP-18：持久化 binding contract 只把 `UClass/UStruct` 当一等公民，`UEnum` / delegate / global helper 仍停留在 live-only side lane

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 绑定管线是否对 `class / struct / enum / delegate / global helper` 提供同级别的持久化合同与工具链入口 |
| 当前设计 | 当前真正进入 `Binds.Cache` 并在 cooked/runtime 两侧共享的只有 `Classes` 和 `Structs`。`UEnum`、delegate 以及 `Bind_UObject` / `Bind_AActor` 这类手写 global helper 仍主要依赖 live bind 执行，持久化侧最多只留下 header link 或完全无记录。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp:27-31` — `Serialize()` 只序列化 `Structs` 和 `Classes`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp:65-98,118-137` — `BoundEnums` / `BoundDelegateFunctions` 只在保存时参与 `.Headers` sidecar，加载阶段不会恢复成可消费的 bind entry；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:712-790` — `Bind_BlueprintType_Declarations` / `Bind_Defaults` 只消费 `FAngelscriptBindDatabase::Get().Classes`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UStruct.cpp:865-905` — struct lane 同样只消费 `Structs`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UEnum.cpp:363-385` — enum 绑定在 live pass 中直接 `FAngelscriptType::Register(MakeShared<FEnumType>(Enum))`，随后只把 `Enum` 指针追加到 `BoundEnums`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.cpp:432-445` — delegate 也是 live pass 中 `Register(MakeShared<FScriptDelegateType>(Decl, Function))`，只把 `Function` 追加到 `BoundDelegateFunctions`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp:556-579` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp:301-317,450-467` — `NewObject`、`Spawn`、`SpawnActor` 这类脚本开发者高频使用的 global/namespace helper 直接 `BindGlobalFunction(...)`，没有任何对应的 DB 结构体。 |
| 优点 | `UClass/UStruct` 的 reflective path 已经能靠 `Binds.Cache` 在 cooked 环境重建主要绑定；而 enum、delegate、manual helper 保持 live-only，也避免了短期内为所有 family 设计统一 schema。 |
| 不足 | 绑定产物天然分层：新增 reflected `UClass/UStruct` 可以走 cache/DB lane，而新增 `UEnum`、delegate 或常用 helper API 仍然必须手写 live binder，且不会自然进入同一份 artifact。结果是“完整脚本 API 面”无法通过单一产物审计，自动化覆盖统计也必须为 family 特判。对外部扩展者来说，哪怕不改插件源码能注入 runtime bind，也很难把 enum/delegate/global helper 变成和 class/struct 同级别的可追踪合同。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | editor 总控在同一条 `Generator()` 主链里依次运行 `FClassGenerator`、`FStructGenerator`、`FEnumGenerator`、`FBindingClassGenerator`、`FBindingEnumGenerator`；enum 不是“头文件附带信息”，而是和 class 一样拥有独立生成物。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:266-305`；`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FBindingEnumGenerator.cpp:7-67` | 至少先把 `Enum` 提升成与 `Class/Struct` 同级的 artifact family，避免运行时有 enum bind、离线合同却只认识 class/struct。 |
| puerts | `FTypeScriptDeclarationGenerator` 在一个 `GenTypeDecl()` 里统一处理 `EnumProperty`、`StructProperty`、`ObjectProperty`、`Array/Set/Map`；扩展方法与重载又统一进入 `FunctionOutputs` / `ExtensionMethodsMap`。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Public/TypeScriptDeclarationGenerator.h:38-70,94-116`；`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:741-876,1157-1172` | family 的声明输出先收敛到一个共享类型/函数图，再决定最终如何落盘；不会只有 class/struct 才有正式声明 lane。 |
| UnLua | `Binding.cpp` 的 `FExported` 同时维护 `Enums`、`Functions`、`ReflectedClasses`、`NonReflectedClasses` 和 `Types`；`FLuaEnv` 创建时统一注册 exported classes/functions/enums。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Binding.cpp:19-57`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:144-159` | 即使 runtime 仍以注册表为主，也先把 `class/function/enum/type` 视为同一份 exported contract，而不是让 enum/function 永远停留在“只在 live bind 里存在”的边缘状态。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先不推翻现有 `Binds.Cache` 读取路径，而是在其旁边补一层 family-complete artifact catalog，让 `Enum`、delegate、global helper 至少先进入同一份可审计合同。 |
| 具体步骤 | 1. 在 `Core/AngelscriptBindDatabase.h` 新增轻量 `FAngelscriptBindingArtifactEntry` 与 `EAngelscriptBindingArtifactKind`，至少覆盖 `Class`、`Struct`、`Enum`、`Delegate`、`GlobalFunction`、`NamespaceFunction`、`ManualType`。<br>2. 第一阶段保持 class/struct 旧结构不动，只让 `Bind_UEnum.cpp`、`Bind_Delegates.cpp`、`FAngelscriptBinds::BindGlobalFunction()` 和相关 helper 在注册成功后额外写一份 artifact entry，字段至少包含 `ScriptDeclaration/Name`、`OwnerOrNamespace`、`ProviderName`、`HeaderPath/ObjectPath`、`ArtifactKind`。<br>3. cook/editor 继续 dual-write 旧 `Binds.Cache`，同时额外输出 family-complete manifest 或 CSV/JSON sidecar；runtime 初期只把这份 sidecar 用于 docs、coverage、dump 和 provider audit，不立即拿它驱动真实绑定。<br>4. 第二阶段再让外部 provider、UHT summary、bind dump 统一 join 这份 artifact key，做到“新增一个 enum/delegate/global helper”至少能在工具链里被完整看见；只有在 schema 稳定后，才考虑让 enum/delegate family 进一步复用 manifest 做加载或校验。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UEnum.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`，以及新增的 manifest/dump 输出文件 |
| 预估工作量 | M |
| 架构风险 | artifact key 如果设计不稳，后续 enum/delegate/global helper 很容易再次出现“同名不同义”的 join 问题；另外 dual-write 期间也要避免 manifest 与 live bind 统计不一致。 |
| 兼容性 | 向后兼容。第一阶段不改 `Class/Struct` 的旧 cache 读写，也不要求 runtime 用新 artifact 做真实绑定；现有手写 binder 只是在注册后多写一份 sidecar entry。 |
| 验证方式 | 1. 以一个 `UEnum`、一个 delegate、一个 `NewObject`/`SpawnActor` 级别的 global helper 为样本，验证新 artifact 能与 `Classes/Structs` 一起落盘。<br>2. 对比 runtime bind observation、docs dump、manifest 三份产物，确认 family 计数一致且不再只有 class/struct。<br>3. 新增一个项目侧最小 C++ 扩展 bind，验证即使暂时还不能进入旧 `Binds.Cache` 主体，也至少能被 family-complete artifact catalog 与审计工具看见。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-BP-18 | `Binds.Cache`/bind artifact 只把 `Class/Struct` 当一等 family，`Enum/Delegate/GlobalHelper` 仍是 live-only lane | family-complete artifact catalog | 高 |

---

## 架构分析 (2026-04-09 00:53)

### Arch-BP-19：注册 API 直接写入 AngelScript engine，返回码与冲突策略没有进入统一 preflight lane

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 手写 bind 扩展在注册失败、重复注册、签名冲突时，是否有统一的预校验与错误收束机制 |
| 当前设计 | `Bind_*.cpp` 里的 authoring API 直接调用 `RegisterObjectType/RegisterObjectMethod/RegisterGlobalFunction/RegisterObjectProperty` 改写运行中的 AngelScript engine；返回的 raw result code 没有进入统一 validator，后续元数据 API 仍继续依赖 `PreviousBind` 游标追加 side effect。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:264-277` — `FAngelscriptBinds` 构造时立即 `RegisterObjectType()`，只有 `asALREADY_REGISTERED` 特判和 `ensure`，没有结构化诊断；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:288-296,452-460,588-617` — method/global function 注册都把 AngelScript 返回值直接喂给 `OnBind()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:409-433` — `OnBind()` 即使 `GetFunctionById(FunctionId)` 失败，也会无条件把 `PreviouslyBoundFunction` 设为这个 `FunctionId`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:377-405` — `SetPreviousBindArgumentDeterminesOutputType()` / `PreviousBindPassScriptFunctionAsFirstParam()` 只在 `GetPreviousBind()` 非空时生效，失败时静默跳过；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp:556-579` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp:303-312,450-467` — 典型 binder 都是“先注册，再补 metadata”；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:497-512` — `AddFunctionEntry()` 对重名条目只保留第一次写入；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:643-683` — enum 注册把 `asALREADY_REGISTERED` 当成 silent success。 |
| 优点 | binder 作者路径很短，直接写 lambda 就能把功能挂进脚本表面；对于临时实验性 bind，这种 immediate-mutation 模式上手成本低。 |
| 不足 | 冲突策略按 family 各自散落：type、enum、function-table entry、global function 的重复处理方式并不一致；一旦外部扩展或新增 UE 类型引入签名/命名冲突，当前管线大多只能得到 AngelScript 内部返回码或完全 silent no-op，缺少“在真正改 engine 前先做一轮可审计 preflight”的正式阶段。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | `Generator()` 先跑 solution/code analysis/class-struct-enum-asset/binding generator，再统一 compile；也就是先把合同产物和分析阶段做完，再进入 runtime 回填，而不是在 authoring API 调用点直接改 live registry。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:237-305` | 先有 generator/preflight phase，再有 runtime apply phase，扩展者遇到问题时先看生成和编译输出，而不是只盯 live registration 结果。 |
| puerts | declaration generator 在 `Gen()` 阶段就检测重复名并发出 warning；`GenTypeDecl()` 遇到 ignore list 或不支持的 property 会显式 `return false`，让“不能生成”的决定停留在生成期；额外扩展也通过 `UCodeGenerator` 接口进入同一轮生成。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:704-709,741-876,1713-1716`；`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Public/CodeGenerator.h:10-27` | 把 duplicate/unsupported/extension 统统收口到 generator context，而不是让每个 runtime bind family 各自决定如何吞掉错误。 |
| UnLua | `ExportClass/ExportFunction/AddType` 先把导出对象放进 `FExported` registry，`AddType()` 还会先 `ensure` 参数合法；真正的 `Register(L)` 在 `FLuaEnv` 初始化时统一执行。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Binding.cpp:19-57`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:144-159` | 即使最终仍是 runtime 注册，也先把“我要注册什么”收束成 exported set，给校验、枚举和冲突处理留出中间层。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在不推翻现有 `Bind_*.cpp` DSL 的前提下，加一层 report-first 的 `BindPreflight`，先标准化返回码与冲突策略，再决定是否真正提交到 AngelScript engine。 |
| 具体步骤 | 1. 在 `Core/AngelscriptBinds.h` 新增 `FAngelscriptBindDraft`、`FAngelscriptBindIssue`、`EAngelscriptBindConflictPolicy`，统一表达 `Type/Method/GlobalFunction/Property/Enum/FunctionEntry` 的注册意图与结果。<br>2. 给 `BindGlobalFunction()`、`BindExternMethod()`、`ValueClass()`、`FEnumBind` 等入口增加内部 shared helper：先把 AngelScript 返回码、符号 key、冲突类型转成 `FAngelscriptBindIssue`，再决定 `Accept/WarnAndSkip/Error`；第一阶段只做 issue 收集，不改变现有 commit 时机。<br>3. 在 editor/test 配置下增加 strict preflight：若 `OnBind()` 收到无效 `FunctionId`、`PreviousBind` 元数据未成功附着、或 `AddFunctionEntry()` 发生重名吞并，就把问题写入 manifest/dump 并在 automation 中 fail；shipping 先保持 warning/report-only。<br>4. 第二阶段再把最高风险 family 迁到两段式 `Stage -> Commit`：例如 `Bind_UObject` 的 global factory、`Bind_AActor` 的 generic helper、外部 provider 注册路径，先验证 descriptor/冲突，再统一提交。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Testing/AngelscriptBindExecutionObservation.*`，以及新增的 issue/manifest 导出文件 |
| 预估工作量 | M |
| 架构风险 | 如果一开始就要求所有 bind family 改成真正 transactional commit，改动面会很大；更稳妥的路线是先把 raw result code 和冲突分类显式化，再逐步把高风险 family 切到 staged commit。 |
| 兼容性 | 向后兼容。旧 `Bind_*.cpp` 写法、旧 `FBind` 和现有脚本 API 均可保留；第一阶段只是增加 issue 收集和 editor/test 严格模式，不强迫项目方立刻重写 binder。 |
| 验证方式 | 1. 人工构造一个 duplicate global function、一个 duplicate enum element、一个 duplicate `AddFunctionEntry()`，验证 preflight report 能区分 `WarnAndSkip` 与 `Error`。<br>2. 新增一条故意返回无效 `FunctionId` 的测试 bind，确认 `PreviousBind` 元数据失败会被显式记录，而不是静默丢失。<br>3. 复跑现有 `BindConfig` / `MultiEngine` / `function-table` 相关测试，确认 report-only 模式下旧行为不变。 |

### Arch-BP-20：注册队列里的贡献者是 opaque lambda，dump 与测试快照无法回答“这个 bind 来自哪里”

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 绑定注册管线对手写 bind、自动分片 bind、外部扩展 bind 是否保留了足够的来源追踪信息，便于审计和扩展支持 |
| 当前设计 | 注册目录里真正持久保存的只有 `BindName`、`BindOrder` 和 `TFunction<void()>`；测试观察面和 state dump 也几乎只保留 bind name。当前架构能告诉你“哪个 bind 被执行/跳过”，但不能回答“它来自哪个文件、哪个 provider、哪个生成 shard、哪个类”。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:120-153` — `FBindFunction` 只有 `BindName/BindOrder/Function` 三个字段；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:138-141` — 未命名条目会被自动生成为 `UnnamedBind_N`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:431-436` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:176-183` — `FBindInfo`/`GetBindInfoList()` 只暴露 `BindName/BindOrder/bEnabled`；`Plugins/Angelscript/Source/AngelscriptRuntime/Testing/AngelscriptBindExecutionObservation.h:7-14` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Testing/AngelscriptBindExecutionObservation.cpp:48-63` — 自动化快照只记录 `DisabledBindNames/ExecutedBindNames`；`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp:853-867` — CSV 明明预留了 `BindModule` 列，但实际逐行写入的是空字符串。 |
| 优点 | 运行时状态面很轻，序列化和测试观测都简单；对于纯内建 bind family，靠 `BindName` 已足够做基本排序和 disable 控制。 |
| 不足 | 一旦进入“外部 provider 注入”“generated shard 排错”“同名 bind 冲突”“历史 diff 审计”这些更现实的扩展场景，`BindName` 不足以定位来源；尤其是 `UnnamedBind_N`、generated shard 和 project-side bind 混在一起时，当前官方 dump 已经暴露出想记录 `BindModule` 却拿不到数据的问题。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | 每个 `UClass` 的生成结果都会通过 `FGeneratorCore::GetFileName(InClass)` 取得稳定输出路径，并先登记进 generator file list，再写盘。生成单元天然绑定到具体反射对象和文件。 | `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp:870-874` | 即使后续 runtime 只消费生成物，authoring/provenance 仍然能回溯到具体 `UClass -> generated file`。 |
| puerts | `WriteOutput(UObject* Obj, ...)` 以具体 `UObject`/`UPackage` 为 key 写入 `BlueprintTypeDeclInfoCache.NameToDecl`；第三方扩展则通过显式 `UCodeGenerator` interface class 进入生成。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:530-550`；`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Public/CodeGenerator.h:10-27` | 输出与扩展入口都是命名对象，而不是匿名闭包，因此生成物可以直接追溯到包、对象和扩展类。 |
| UnLua | exported registry 按 `ReflectedClasses/NonReflectedClasses/Functions/Enums/Types` 分类保存命名集合，并提供 `FindExportedClass()` 等查询接口；`FLuaEnv` 再统一消费这些命名集合。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Binding.cpp:19-25,34-57,59-98`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:146-159` | 即便是静态导出，也先把贡献者收成可查询 registry，而不是把来源丢进 opaque lambda。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 给每个 bind contributor 增加 lightweight provenance context，把“执行序列”升级成“可回溯的注册账本”，先满足诊断和审计，再服务未来 provider/manifest。 |
| 具体步骤 | 1. 在 `Core/AngelscriptBinds.h` 新增 `FAngelscriptBindSourceContext`，至少包含 `ProviderName`、`ModuleName`、`SourceFile`、`SourceLine`、`DeclaringSymbol`、`ArtifactLane(Manual/GeneratedShard/Reflective/UHT/External)`。<br>2. 扩展 `FBindFunction` 和 `FBindInfo` 携带这份 context；为旧 `FBind` 构造函数提供默认宏包装，自动捕获 `__FILE__`、`__LINE__` 和函数名。生成的 `ASRuntimeBind_*` / `ASEditorBind_*` 模块则显式填 `ModuleName` 和目标 `ClassName`。<br>3. 把 `FAngelscriptBindExecutionSnapshot`、`GetBindInfoList()`、`AngelscriptStateDump.cpp` 一起扩展，真正填满现有 `BindModule` 列，并新增 `SourceFile`/`ArtifactLane`/`ProviderName` 可选列；对于 legacy unnamed bind，至少输出 `SourceFile:Line` 和一个稳定 hash。<br>4. 在第二阶段让 bind observation、provider manifest、future artifact catalog 共用这份 source context，做到从“某个脚本 API”可以一路追到“哪个 binder/哪个 generated shard/哪个项目扩展”提供了它。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Testing/AngelscriptBindExecutionObservation.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Testing/AngelscriptBindExecutionObservation.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`，`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`，以及生成 shard 模板代码 |
| 预估工作量 | M |
| 架构风险 | `__FILE__` 直接落盘可能导致路径噪声和机器差异，需要同时设计 path normalization；另外 generated shard 的 source context 需要避免把中间文件路径误当成真正的语义来源。 |
| 兼容性 | 向后兼容。旧 `BindName`/`BindOrder`/disable 机制不变；新 context 先作为附加字段存在，legacy bind 和现有测试即使不填 `ProviderName` 也仍可运行。 |
| 验证方式 | 1. 为一个手写 named bind、一个 unnamed bind、一个 generated shard bind、一个外部 provider bind 各导出一条 snapshot/dump，确认能分辨来源。<br>2. 验证 `BindModule` 列不再恒为空，并能稳定追溯到 `ASRuntimeBind_*` 或项目模块名。<br>3. 复跑现有 `BindConfig` / `MultiEngine` 相关测试，确认新增 provenance 字段不会改变排序与 disable 语义。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-BP-19 | 注册失败、重复冲突与 metadata 追加缺少统一 preflight/report lane | 校验链补强 | 高 |
| P2 | Arch-BP-20 | bind contributor 来源不可追踪，官方 dump 连 `BindModule` 列都无法填充 | 诊断与审计可观测性增强 | 中 |

---

## 架构分析 (2026-04-09 01:01)

### Arch-BP-21：绑定阶段依赖被 `BindOrder` 偏移量与 `TypeFinder` 注册顺序隐式编码

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 新增一个 UE 类型或外部 provider 时，如何把 “type 注册 / property 解析 / class declaration / late bind / global helper” 放进正确阶段 |
| 当前设计 | 核心管线只公开 `Early / Normal / Late` 三个粗粒度阶段，但实际依赖关系靠整数偏移和注册先后隐式表达；`TypeFinder` 也是 append-only，谁先注册谁先匹配。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:424-429` — 公开阶段只有 `EOrder::Early/Normal/Late`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UEnum.cpp:358-402` — `Bind_Enums` 用 `Early-1` 先注册 enum/type finder；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:712-725` — `Bind_BlueprintType_Declarations` 在 `Early` 先声明 class；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp:37-72,273-337,510-579` — `UObject/UClass/global UObject helpers` 放在 `Late-1` 并依赖前面已存在的 class/type；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1317-1505` — Blueprint reflective bind 再在 `Late+100` 扫描 class/function/property 并回写 `FAngelscriptBindDatabase::Get().Classes`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp:286-313,450-467` — actor helper 又额外占用 `Late+150`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:114-118,147-159` — `RegisterTypeFinder()` 只是 append 到数组，`GetByProperty()` 按注册顺序返回第一个命中的 finder。 |
| 优点 | 对内建 binder 作者来说实现成本低，写一个 `Bind_*.cpp` 就能直接落位，不需要先声明复杂图结构。 |
| 不足 | 扩展者必须自己猜 “我应该插在 `Early-1`、`Late-1` 还是 `Late+100`”，还要猜 `TypeFinder` 是否会被已有 family 抢先命中。新增 wrapper/container/value type 时，阶段知识散落在多个文件和魔法数字里，不是显式 contract。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | `Generator()` 把 `Solution -> CodeAnalysis -> Class -> Struct -> Enum -> Asset -> BindingClass -> BindingEnum -> Compile` 固定成显式阶段链，而不是让每个 family 自己选整数插槽。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:237-305` | 阶段依赖应成为 orchestrator 的显式模型，而不是 binder 作者脑内知识。 |
| puerts | `FTypeScriptDeclarationGenerator::Gen()` 统一分发 `UClass/UStruct/UEnum`，`GenTypeDecl()` 在一个中心函数里处理 `Enum/Struct/Object/Array/Set/Map/Delegate`，第三方扩展也通过 `UCodeGenerator` 接口进入固定生成阶段。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:691-738,741-876,1708-1716`；`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Public/CodeGenerator.h:10-27` | 把 family 分发和扩展入口收敛到统一阶段函数，避免扩展方直接操心底层顺序细节。 |
| UnLua | `Binding.cpp` 先把 `ReflectedClasses/NonReflectedClasses/Functions/Enums/Types` 收进 `FExported`，`FLuaEnv` 初始化时再按统一时机消费这些 exported sets。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Binding.cpp:19-57`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:144-159` | 即使仍允许静态导出，也应先形成显式 registry，再由 env/runtime 在确定阶段消费。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留现有 `FBind` 写法的前提下，引入显式 `BindPhase` 与有优先级的 `TypeFinder` 计划，把 today 的魔法数字映射成可审计执行图。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h` 新增 `EAngelscriptBindPhase`，至少拆成 `RegisterCoreTypes`、`RegisterTypeFinders`、`DeclareReflectedTypes`、`BindReflectedMembers`、`BindGlobals`、`PostBind`。<br>2. 新增 `FAngelscriptBindStepDescriptor`，包含 `Phase`、`WithinPhasePriority`、`ProviderName`、`Dependencies`；旧 `FBind(int32 BindOrder, ...)` 继续保留，但内部先映射到 phase，再用原整数做 phase 内 tie-break，确保老 binder 不用立刻改。<br>3. 扩展 `FAngelscriptType::RegisterTypeFinder()`，让 finder 带 `Priority` 与 `ProviderName`；`GetByProperty()` 先按 priority 排序，再执行匹配，并把命中的 finder 来源写入调试/manifest。<br>4. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` 新增 bind-plan 测试，显式验证 `Enum/Object wrapper finder -> BlueprintType declarations -> BlueprintType late bind -> Actor helpers` 的解析顺序；再把 resolved plan 输出到 dump，降低新类型接入时的逆向成本。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/*Bind*.cpp` |
| 预估工作量 | M |
| 架构风险 | 最大风险是旧 `BindOrder` 映射不准导致顺序回归，尤其是 `Late-1` 与 `Late+100/+150` 这些历史插槽；必须先做 report-only bind plan，再逐步切换执行器。 |
| 兼容性 | 向后兼容。旧 `FBind`/旧 `Bind_*.cpp` 不需要马上重写；第一阶段只是把隐式顺序外显化，并允许新 provider 使用更明确的 phase API。 |
| 验证方式 | 1. 复跑 `BindConfig`、`MultiEngine`、Blueprint/Actor 相关自动化，确认阶段映射前后注册结果一致。<br>2. 新增一个项目侧自定义 `TypeFinder`，验证可以通过显式 priority 覆盖或让位于内建 finder，而不再依赖 include/link 顺序。<br>3. 抽样比对 `Bind_UObject`、`Bind_BlueprintType`、`Bind_AActor` 的 resolved phase plan 与当前行为一致。 |

### Arch-BP-22：`Legacy Native Bind Generator` 已被标为调试旧链路，但运行时仍把它当成默认输入 authority

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 当前插件到底以哪条生成链为“正式绑定输入”：`AngelscriptUHTTool`，还是 editor 侧 `ASRuntimeBind_* / ASEditorBind_*` legacy shard |
| 当前设计 | 编辑器 UI 已明确声明 `GenerateNativeBinds()` 是 “Legacy Native Bind Generator (Debug Only)” 且 `AngelscriptUHTTool` 才是 primary path，但 runtime 仍会无条件尝试读取 `BindModules.Cache` 并加载这些 auto-generated module，再进入 `BindScriptTypes()`。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:728-730` — 菜单文案明确写着 legacy/debug-only，且指明 `AngelscriptUHTTool` 是主路径；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:999-1077` — `GenerateNativeBinds()` 仍会 `GenerateBindDatabases()`、切分 `ASRuntimeBind_*` / `ASEditorBind_*` 模块并写 `BindModules.Cache`；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1285-1327` — 生成 module 的 `StartupModule()` 本质上只是再注册一个 `EOrder::Late` 的 bind lambda；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1466-1496` — runtime 先 `Load(Binds.Cache)`，再 `LoadBindModules(plugin->GetBaseDir() / "BindModules.Cache")`，随后逐个 `LoadModule()`，最后才执行 `BindScriptTypes()`；`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:21-35` 与 `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:19-21,51-79,449-479` — UHT tool 另一边又独立生成 `AS_FunctionTable_*.cpp` shard 与 summary/coverage 输出。 |
| 优点 | 兼容历史工程与旧 `FunctionCallers` 路径，必要时仍可通过 legacy shard 快速验证旧自动绑定实现。 |
| 不足 | 现在存在两个“看起来都像正式生成输入”的 lane：一个被文案宣称已退役但 runtime 仍默认消费，另一个是当前主线的 UHT lane。扩展者无法直观看出应该接入哪条链，runtime 也无法区分加载到的是“官方主路径产物”还是“遗留 debug shard”。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | 编辑器 `Generator()` 在一个总控函数里串起所有生成步骤并立即编译，生成 authority 只有一条主链。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:237-305` | 主生成链应该只有一个 authoritative orchestrator；旧链路若保留，应退到显式 opt-in。 |
| puerts | declaration generator 在一次 pass 中写 `Typing/ue/ue.d.ts` 与 `ue_bp.d.ts`；第三方代码生成也通过 `UCodeGenerator` 接口挂入同一主生成阶段。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:384-457,1708-1716`；`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Public/CodeGenerator.h:10-27` | 自定义扩展不应另起一条并行 authority，而应进入主生成器的正式扩展点。 |
| UnLua | `FLuaEnv` 启动时统一消费 exported classes/functions/enums，没有 editor 生成 module cache 与 runtime auto-load 的第二条注册 authority。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Binding.cpp:19-57`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:146-159` | 即便保留静态导出，也尽量让 runtime 只认一份显式 registry，而不是同时认多条历史输入。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 legacy bind module lane 从“隐式默认 authority”降级成“显式可选 extension/debug lane”，并把官方主线收束到 UHT/manifest 路径。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h` 增加 `LegacyBindModuleMode`（如 `Disabled / Explicit / Auto`）；第一阶段默认保持现状的 `Auto`，但 runtime 要在日志和 dump 中明确标记 “本次是否消费了 legacy shard”。<br>2. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` 中，只在 `LegacyBindModuleMode != Disabled` 且 `BindModules.Cache` 版本匹配时才读取/加载 legacy modules；如果 cache 缺版本或来自旧生成器，先 warning，不再静默进入正式管线。<br>3. 把 `GenerateNativeBinds()` 输出补充 producer/version/target-engine 信息，并将其定义为 `LegacyGeneratedProvider`；长期如果还要保留这条链，就让它走和外部 provider 一样的显式注册协议，而不是 plugin-root 隐式 auto-load。<br>4. 把官方默认的 generated authority 收敛到 `AngelscriptUHTTool` sidecar 与未来统一 manifest；待新 manifest 足够覆盖后，再把 legacy lane 的默认模式从 `Auto` 逐步迁到 `Explicit`，新工程默认关闭，旧工程可通过设置兼容。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h`，`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`，`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs` |
| 预估工作量 | M |
| 架构风险 | 若直接“一刀切”停用 legacy lane，可能影响仍依赖旧 shard 的本地工作流；更稳妥的路线是先 telemetry + versioning + explicit mode，再逐步调整默认值。 |
| 兼容性 | 可增量且向后兼容。第一阶段保留 `Auto` 作为旧项目默认行为，只增加显式开关和来源标记；第二阶段才考虑对新工程改默认值，不强迫旧项目立即迁移。 |
| 验证方式 | 1. 准备三组场景：仅 UHT lane、仅 legacy lane、双 lane 同时存在，验证 runtime dump 能明确指出本次实际消费的输入来源。<br>2. 人工放入一个过期 `BindModules.Cache`，确认 runtime 不再静默加载，而是给出可见 warning 或按模式拒绝。<br>3. 复跑 function-table、bind config、editor startup 相关流程，确认在 `Auto` 模式下旧行为不变，在 `Disabled/Explicit` 模式下仍可正常启动主线绑定。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-BP-21 | 绑定阶段依赖被 magic order 与 finder 注册顺序隐式编码 | 阶段模型显式化 | 高 |
| P1 | Arch-BP-22 | legacy bind module lane 与 UHT 主线并存，authority 不清晰 | 生成入口收敛 | 高 |

---

## 架构分析 (2026-04-09 23:56)

### Arch-BP-23：绑定激活是启动期全量 materialize，新增类型与外部 provider 会线性放大初始化面

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 绑定激活时机与扩展规模上限 |
| 当前设计 | runtime 初始化时先加载 `Binds.Cache` 和 `BindModules.Cache`，随后一次性执行全部 `FBind`；`Bind_BlueprintType` 再把 `Classes` 或 `TObjectRange<UClass>()` 全量展开为 script type / method / property。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1466-1496` — engine 启动期先 `Load(Binds.Cache)`、再 `LoadBindModules()`、最后直接 `BindScriptTypes()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:151-214` — `RegisterBinds()` 只把 lambda 塞进全局数组，`CallBinds()` 会按排序把所有 bind 全部执行；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:712-725` — DB lane 在 `Early` 遍历 `FAngelscriptBindDatabase::Get().Classes` 为每个 class 声明类型；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1317-1505` — live lane 在 `Late+100` 遍历 `TObjectRange<UClass>()`，为每个 class 扫函数、属性并回填 DB。 |
| 优点 | 启动完成后脚本面是完整 materialize 的，后续脚本执行阶段无需再处理“首次访问时补注册”的状态切换。 |
| 不足 | 新增一个 reflected `UClass`、一个 wrapper family、或一个外部 provider，都会进入同一条全量启动路径；扩展能力与初始化成本被硬绑定，项目侧扩展越多，启动 pass 越大。由这些代码可推断，当前架构没有“仅在脚本首次碰到某个 type 时再激活该 type 绑定”的正式车道。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FLuaEnv` 启动时只创建 `ClassRegistry/FunctionRegistry/PropertyRegistry` 等 registry，并消费静态导出集合；真正遇到 metatable 时，`ClassRegistry::PushMetatable()` 才调用 `RegisterReflectedType()` materialize reflected type。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:104-113,144-159`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:108-130`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/Binding.h:21-35` | 把“启动时建立 registry”与“首次访问时注册 reflected type”分开，扩展面不必全部挤进同一个 startup pass。 |
| puerts | `LoadClassByID()` 先查现有 `JSClassDefinition`，未命中时才通过 `ClassNotFoundCallback` 触发补加载，然后再重查。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JSClassRegister.cpp:85-100` | 把 class definition 的获取设计成 lookup-driven，而不是要求所有类型在环境初始化前就全部 ready。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留现有 eager 路径兼容性的前提下，补一条 descriptor-first 的 lazy activation lane，把“注册目录”与“真正写入 AngelScript engine”拆开。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h` 新增轻量 `FAngelscriptBindingActivationDescriptor`，字段至少包含 `SymbolKey`、`ArtifactKind`、`ProviderName`、`ActivationMode(Eager/Lazy)`、`ActivateLambda`。<br>2. 让现有 `FBind` 默认仍走 `Eager`，保证旧 `Bind_*.cpp` 和旧工程行为不变；同时为 reflective `UClass` lane 和未来外部 provider 提供 `RegisterLazyDescriptor()`。<br>3. 在 `Bind_BlueprintType.cpp` 中把 “扫描 class -> 构造 method/property bind 数据” 与 “真正调用 `Binds.Method()` / `BindBlueprintCallable()`” 分离；启动期先登记 descriptor，首次 `GetByClass()`、`GetTypeInfoByName()` 或 provider 查询时再激活对应 class。<br>4. 在 dump / observation 中新增 `ActivationMode`、`ActivatedAtStartup`、`ActivationCount`，先以 report-only 方式观察哪些 family 适合改成 lazy；第一批优先试点 reflected `UClass` lane 和项目侧 provider，不碰 `Bind_UObject` / `Bind_AActor` 这种高频基础 helper。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`，以及相关自动化测试 |
| 预估工作量 | L |
| 架构风险 | lazy 与 eager 并存时最容易引入“首次访问时机不同导致行为差异”的回归，尤其是依赖 `CopySystemType()` 和 late augmentation 的 class family。 |
| 兼容性 | 向后兼容。默认仍保持 eager；lazy 先作为新 family / 新 provider 的 opt-in 能力，不会破坏现有脚本 API。 |
| 验证方式 | 1. 对 `Bind_BlueprintType` 增加 activation snapshot，验证未访问 class 不会在启动期 materialize，首次访问后得到与旧路径相同的 method/property surface。<br>2. 新增一个项目侧 provider，分别以 `Eager` 和 `Lazy` 模式注册同一组 helper，验证脚本调用结果一致。<br>3. 对比改造前后启动期 bind 数量、执行时间和已激活 type 数量，确认新增类型不再必然放大全局 startup pass。 |

### Arch-BP-24：`BindDB` 回放对陈旧条目是“静默缩面”而不是显式失效，重命名后容易出现部分 API 消失

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `Binds.Cache` / `.Headers` 回放时对重命名、删除、迁移成员的失效行为 |
| 当前设计 | `FAngelscriptBindDatabase::Load()` 只要 `Classes/Structs` 不是同时为空就继续运行；之后 DB lane 用 `FindObject/FindFunctionByName/FindPropertyByName` 逐项回放，找不到就直接 `continue`，不会形成结构化 stale report。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp:103-115` — `Load()` 只在 `Classes.Num()==0 && Structs.Num()==0` 时 fatal；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:714-718` — `FindObject<UClass>(..., *DBBind.UnrealPath)` 失败直接跳过整个 class；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:741-745` — `FindFunctionByName(*DBFunc.UnrealPath)` 失败直接跳过 method；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:788-792` — `FindPropertyByName(*DBProp.UnrealPath)` 失败直接跳过 property；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp:121-135` — `.Headers` sidecar 回放时 `FindObject` 失败同样直接 `continue`，不会记录 header link 缺失原因。 |
| 优点 | 对局部失配有容错性，单个成员找不到不会直接阻断整个引擎启动。 |
| 不足 | 这是一种“静默缩面”容错。由上述 `continue` 路径可以推断：当 `UClass/UFunction/FProperty` 被重命名、移包或移除，但旧 `Binds.Cache` 仍可加载时，启动不一定失败，而是可能悄悄少掉一部分脚本 API；调用侧看到的是绑定面缩小，而不是明确的 stale-contract 诊断。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `BlueprintTypeDeclInfoCache` 先按 `PackageName` 跟踪 blueprint 声明变化，只对 `Changed` 包重新生成；写出结果时也按 `Package -> ObjectName` 落到 `NameToDecl`，同时会清理旧 `ue.d.ts / ue_bp.d.ts` 再整体重写。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:387-457`；`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:530-550` | 先把“哪个 package 变了”做成显式 dirty model，再决定重建与清理，不靠 runtime 回放时的 silent skip 去吞失效。 |
| UnrealCSharp | 编辑器总控每次走固定 generator 主链；每个 `UClass` 都有稳定输出文件，生成时先把文件名登记进 generator file list，再写盘并立即编译。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:237-305`；`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp:870-874` | 生成物是 type-granular 的，失效与重建边界更清楚，不会把“某个成员丢了”隐藏在整库回放的 `continue` 分支里。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 给 `BindDB` 回放补一层 per-entry stale diagnostics，把今天的 silent skip 升级成“可报告、可配置、可局部重建”的失效模型。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h` 为 `FAngelscriptClassBind/FAngelscriptMethodBind/FAngelscriptPropertyBind` 增加 `SourcePackage`、`ObjectPath`、`SignatureHash` 或等价 fingerprint 字段；旧字段保持不动，先 dual-write。<br>2. 在 `Bind_BlueprintType.cpp` 的 DB 回放路径中，把现有 `continue` 改为同时写入 `FAngelscriptBindingReplayIssue`：区分 `MissingClass`、`MissingFunction`、`MissingProperty`、`MissingHeaderLink`，并附带原始 key。<br>3. 在 editor/dev 模式下，若 issue 出现则优先触发局部 regenerate 或至少输出明确 report；在 cooked 模式下增加 `StaleBindDBPolicy`（如 `Warn` / `Fatal` / `FallbackToLiveScan`），避免今天这种“成功启动但绑定面悄悄缩小”的不透明行为。<br>4. 让 dump / docs / future manifest 输出 stale issues，并按 `SourcePackage` 聚合；这样重命名一个 UE 类型时，可以直接看到受影响 package / class / member，而不是靠脚本编译报错倒推。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`，以及相关 editor/test 输出文件 |
| 预估工作量 | M |
| 架构风险 | 如果 fingerprint 设计不稳，可能把 harmless rename 与真实 ABI 变化混在一起，产生过多噪声；因此第一阶段应先 report-only，再决定哪些 issue 升级为 fatal。 |
| 兼容性 | 向后兼容。旧 `Binds.Cache` 仍可读取；新增 stale 诊断初期只做 warning/report，不改变现有脚本接口和 cache 格式的读取入口。 |
| 验证方式 | 1. 人工重命名一个 reflected `UClass`、一个 `BlueprintCallable` `UFunction`、一个 property，验证 runtime 能输出对应的 `MissingClass/MissingFunction/MissingProperty`，而不是静默少掉 API。<br>2. 在 `Warn` 与 `Fatal` 两种策略下分别启动，确认行为可控。<br>3. 对未变更 package 复跑 bind 相关自动化，确认新增 fingerprint 与 stale report 不会影响正常回放。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-BP-23 | 启动期全量 materialize 让新增类型与 provider 扩展直接放大全局初始化面 | 激活模型分层 | 高 |
| P1 | Arch-BP-24 | `BindDB` 对陈旧条目是静默缩面，不是显式失效 | 失效诊断与局部重建 | 高 |

---

## 架构分析 (2026-04-10 00:05)

### Arch-BP-25：`FBind` 依赖静态初始化与模块保活，外部 provider 的接入前提仍是隐式链路

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 外部模块在“不修改插件源码”的前提下，注册自定义 bind 的真实前提条件 |
| 当前设计 | 手写 binder 主要依赖 translation unit 级静态 `FBind` 对象在模块加载时执行构造函数；引擎本体并不枚举“provider”，而是把 bind lambda 直接塞进全局数组，随后在启动时统一 `CallBinds()`。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:438-468` — `FBind` 构造即调用 `RegisterBinds()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:151-158` — `RegisterBinds()` 只把 `{BindName, BindOrder, Function}` 追加到静态数组；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h:41-46` — `AS_FORCE_LINK` 只在 `__GNUC__/__clang__` 下有 `retain` 语义，在当前常见的 MSVC 路径上为空；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp:37-43` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp:25-31` — 典型 `Bind_*.cpp` 都用 TU 级静态 `FBind` 起手；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1473-1488` — runtime 额外依赖 `BindModules.Cache` 去装载 `ASRuntimeBind_* / ASEditorBind_*` 模块；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:999-1077,1322-1327` — editor 生成的 bind module 本质上也是在 `StartupModule()` 中再调用一次 `FAngelscriptBinds::RegisterBinds()`。 |
| 优点 | 插件内部 authoring 成本低，新增一个手写 binder 文件时只要写 `AS_FORCE_LINK const FBind` 就能接入既有排序管线。 |
| 不足 | 对外部扩展者来说，真实要求不是“include 一个头并注册 bind”，而是同时满足“TU 被链接保活 + module 被加载 + 启动时序正确”三件事。当前源码没有把这条链路提升成正式 contract，因此自定义 provider 在 Windows/MSVC 或按需模块加载场景下更容易出现“代码写了，但 bind 没出现”的隐式失效。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 把扩展面建成公开 registry API：`ExportClass/ExportEnum/ExportFunction/AddType` 先写入 `FExported`，`FLuaEnv` 构造时统一消费这些 exported 集合。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/Binding.h:21-43`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Binding.cpp:19-57`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:144-159` | 至少把“谁在扩展、何时被消费”做成显式 registry，而不是让扩展者自己猜测链接器和模块装载路径。 |
| puerts | declaration 扩展不靠 TU 静态对象，而是枚举实现了 `UCodeGenerator` interface 的类并调用 `ICodeGenerator::Execute_Gen()`。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Public/CodeGenerator.h:10-27`；`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1708-1717` | 把扩展发现变成 interface discovery，避免“扩展是否生效”取决于某个源文件有没有被保活。 |
| UnrealCSharp | editor 有单一 `Generator()` 总控，统一串起 generator / compiler / binding 生成阶段，而不是靠散落 TU 的静态构造器拼出完整管线。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:237-305` | 对外扩展更友好的方式是显式 orchestration，而不是隐式 side effect。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留旧 `FBind` 向后兼容入口的前提下，补一层显式 `provider` 协议，把“写 bind 代码”和“让 bind 真正被发现/加载”分开。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/` 新增 `IAngelscriptBindingProvider`（或基于 `IModularFeatures` 的等价接口），至少包含 `DescribeProvider()`、`RegisterTypes()`、`RegisterBinds()`。<br>2. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` 的 `BindScriptTypes()` 前显式枚举 provider；provider 未加载、重复加载、注册失败都进入日志和 dump，而不是只靠 `BindModules.Cache` 与静态数组的副作用。<br>3. 让现有 `FBind` 继续可用，但内部归并到一个“默认 static provider”；editor 生成的 `ASRuntimeBind_* / ASEditorBind_*` 模块也改为实现 provider 接口，而不是在 `StartupModule()` 里再塞匿名 lambda。<br>4. 在 `UAngelscriptSettings` / `FAngelscriptEngineConfig` 增加 `AdditionalBindProviderModules`，让项目或外部插件只需“声明模块 + 实现 provider”即可接入，无需修改插件源码或依赖隐式保活。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h`，`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`，以及新增的 provider interface / provider registry 文件 |
| 预估工作量 | M |
| 架构风险 | provider discovery 会改变启动顺序的显式 owner；若旧 `BindOrder` 与新 provider 调度整合不当，可能引入启动顺序回归。 |
| 兼容性 | 向后兼容。旧 `AS_FORCE_LINK const FBind` 与 `BindModules.Cache` 第一阶段都可保留，只是新增显式 provider lane 供新扩展使用。 |
| 验证方式 | 1. 新建一个独立项目模块或外部插件模块，实现 `IAngelscriptBindingProvider`，验证不修改 `AngelscriptRuntime` 源码也能注册 `type + bind`。<br>2. 在 MSVC/Windows 构建下验证 provider 生效不再依赖 `AS_FORCE_LINK`。<br>3. 复跑 `BindConfig`、`MultiEngine`、`StartupBindObservation` 相关自动化，确认旧静态 binder 与新 provider 可并存且顺序稳定。 |

### Arch-BP-26：项目侧 `ScriptCallable/ScriptMixin` 已能扩展绑定面，但缺少面向脚本作者的正式声明产物

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 脚本开发者如何获知“项目侧新增的 native/reflected 绑定到底暴露了什么” |
| 当前设计 | 项目模块可以通过 `ScriptCallable` / `ScriptMixin` 元数据走 reflective path 进入绑定系统，但当前对外可消费的声明能力主要停留在单个 `UASClass/UASFunction` introspection 和一次性 `dump-as-doc` 调试导出，不是常规生成管线。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:659-673` — reflected `UClass` 进入 `ReferenceClass + FAngelscriptType::Register` 主链；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1303-1314,1398-1409` — `BlueprintCallable/BlueprintPure` 之外，带 `ScriptCallable` metadata 的项目函数也会进入 `BindBlueprintCallable()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h:276-282` — `ScriptMixin` 元数据会改变函数签名归属；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp:299-302,421-430` — `GetScriptTypeDeclaration()` / `GetScriptFunctionDeclaration()` 只对 `UASClass/UASFunction` 返回非空，native/reflected `UClass/UFunction` 默认拿不到声明字符串；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp:407-430,675-755` — `DumpDocumentation()` 会遍历当前 script engine 后写 `ProjectDir()/Docs/angelscript/generated/*.hpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:528,2224-2227` — 该导出只能通过 `-dump-as-doc` 触发，运行后直接退出进程；`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp:223-246` — 现有测试验证的是 script-generated `UASClass/UASFunction` 元数据，而非 native/reflected 绑定面的批量导出。 |
| 优点 | 项目侧确实已经有“不改插件源码就扩展暴露面”的 reflective 入口，且 debug 模式下可以从当前 engine 状态导出伪头文件文档。 |
| 不足 | 对脚本作者而言，这条能力仍不是正式 contract：项目新加的 `ScriptCallable` / `ScriptMixin` 是否成功暴露、声明长什么样、和手写 bind 是否冲突，今天更多依赖运行时验证或单次文档 dump，而不是像 `ue.d.ts` 一样的稳定产物。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 声明生成器每次显式重写 `Typing/ue/ue.d.ts` 与 `ue_bp.d.ts`，并用 `BlueprintTypeDeclInfoCache` 保存 package 级增量状态；额外扩展通过 `UCodeGenerator` 进入同一生成阶段。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:417-457`；`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:530-550`；`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1700-1717`；`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Public/CodeGenerator.h:10-27` | 把“IDE/作者要消费的声明面”做成正式 artifact，而不是调试时临时导出。 |
| UnLua | editor / commandlet 侧既能判断 Blueprint 是否应导出 IntelliSense，又会把 exported classes/enums/functions 按文件写到输出目录。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:120-140,222-233`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp:55-99,117-131`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Binding.cpp:19-57` | 即便 runtime registry 仍存在，也额外提供稳定的 authoring artifact，让扩展结果可见、可 diff。 |
| UnrealCSharp | 代码生成是 editor 正式工作流的一部分，`Generator()` 统一跑 class/struct/enum/binding generator，再立即编译产物。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:237-305` | “声明/代码产物”应是主线工具链，而不是附加调试动作。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把现有 `DumpDocumentation()` 和零散 introspection API 提升为正式的 `Binding Declaration Export` 阶段，为 native/reflected/project-side 扩展提供稳定声明产物。 |
| 具体步骤 | 1. 在 `AngelscriptEditor` 或 `AngelscriptUHTTool` 新增 `FAngelscriptBindingDeclarationExporter`，不要再依赖 `-dump-as-doc` + 进程退出；生成时机可挂到现有 UHT/export 或 editor generator 流程。<br>2. 第一阶段先做 report-only：复用 runtime 已有的 `Bind_BlueprintType` 结果、`FAngelscriptDocs` 文档表、`GetBindInfoList()` 和 bound script engine introspection，输出结构化 `manifest + declaration`，覆盖 native/reflected/manual 三类来源。<br>3. 产物建议拆成 `ProjectDir()/Typing/Angelscript/ue.asdecl` 与按 package/module 分片的 sidecar，确保项目侧 `ScriptCallable`、`ScriptMixin`、外部 provider 贡献都能进入同一出口。<br>4. 后续再让 provider 协议补 `AppendDeclarations()` 或等价接口，避免新增外部扩展后只能在 runtime 看结果、不能进入 IDE/审计产物。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`，以及新增的 declaration exporter / 自动化测试文件 |
| 预估工作量 | L |
| 架构风险 | manual bind 的 declaration 质量取决于现有 binder 元数据是否完整；如果直接要求所有手写 bind 一次性补齐，容易把工作面做成大爆炸。 |
| 兼容性 | 向后兼容。第一阶段只新增声明产物，不改变现有运行时绑定行为；`dump-as-doc` 可继续保留为 debug fallback。 |
| 验证方式 | 1. 新增一个项目侧 `UFUNCTION(meta=(ScriptCallable))` 和一个 `ScriptMixin`，验证 exporter 产物里能看到声明，而不仅是 runtime 成功注册。<br>2. 对比 exporter 产物与运行时 `DumpDocumentation()`/state dump，确认 native/reflected/manual 来源都能对齐。<br>3. 新增自动化测试，覆盖“产物生成不要求进程退出”“native reflected bind 也有声明”“外部 provider 的声明能进入同一产物”。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-BP-25 | 外部自定义绑定仍依赖静态初始化、链接保活与模块装载三段隐式链路 | 扩展点显式化 | 高 |
| P1 | Arch-BP-26 | 项目侧 reflective 扩展缺少正式声明产物，脚本作者只能依赖 runtime/debug 导出 | 工具链与作者入口补齐 | 中 |

---

## 架构分析 (2026-04-10 00:15)

### Arch-BP-27：`AS_FunctionTable_*` 的生成范围被 `AngelscriptRuntime.Build.cs` 白名单锁死，项目模块默认进不了 direct-bind 车道

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | UHT 直连函数表的覆盖边界 |
| 当前设计 | `AngelscriptUHTTool` 不是对全部 UHT session module 产出 `AddFunctionEntry()`，而是先解析 `AngelscriptRuntime.Build.cs` 的 dependency 列表做白名单；只有白名单模块的 `BlueprintCallable` 才会进入 `AS_FunctionTable_*` shard。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:51-79` — `Generate()` 遍历 `factory.Session.Modules`，但先用 `supportedModules.All` 过滤；`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:334-384` — `LoadSupportedModules()` 通过 `ResolveRuntimeBuildCsPath()` 读取 `AngelscriptRuntime.Build.cs` 的 `DependencyModuleNames.AddRange`；`Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs:30-79` — 白名单来源是 `AngelscriptRuntime` 自身依赖；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp:35-48,72-90` — runtime direct bind 只从 `GetClassFuncMaps()` 取 `FFuncEntry`，拿不到 entry 就只能 fallback 或直接返回。 |
| 优点 | 插件内建绑定模块的 direct native lane 可控，生成量受限，UHT 产物不会无限扩大。 |
| 不足 | 由这些代码可推断：项目或外部插件即使定义了 `BlueprintCallable`/`ScriptCallable` `UFunction`，只要其模块不在 `AngelscriptRuntime.Build.cs` 白名单里，就不会自动得到 `AddFunctionEntry()` 直连记录；新增一个项目侧 UE 类型若想进入 direct-bind lane，通常仍要额外修改插件源码中的依赖列表，或再写一个手工 `AddFunctionEntry()` binder，自动化边界对插件用户并不开放。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | `FClassGenerator::Generator()` 直接遍历 `TObjectIterator<UClass>`，生成范围来自当前加载的反射世界，不受单个 runtime module 的依赖白名单限制。 | `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp:16-25` | 把“能否生成绑定”从 core module 的静态依赖列表中解耦出来，降低项目扩展门槛。 |
| puerts | 声明扩展不是改 core `Build.cs`，而是枚举实现了 `UCodeGenerator` 的类并执行 `ICodeGenerator::Execute_Gen()`；额外生成器以接口形式插入同一 declaration pipeline。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Public/CodeGenerator.h:10-26`；`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1708-1717` | 把扩展范围做成显式 opt-in contract，比解析某个模块的 dependency 列表更适合第三方模块。 |
| UnLua | 扩展函数和类型通过 `ExportFunction()`/`ExportClass()` 写入公共 exported registry，`FLuaEnv` 启动时统一消费。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Binding.cpp:34-57`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:144-159` | 外部模块可以把导出能力挂到统一 registry，而不是先修改核心模块的构建依赖。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 `AS_FunctionTable_*` 从“core-module dependency whitelist”改成“显式 contributor opt-in”。 |
| 具体步骤 | 1. 在 `AngelscriptUHTTool` 新增 `IAngelscriptFunctionTableContributor` 或等价 module manifest 约定，允许任意 module 显式声明“参与 function-table 生成”。<br>2. `LoadSupportedModules()` 第一阶段继续保留现有 `AngelscriptRuntime.Build.cs` 白名单作为 fallback，同时新增从 contributor interface / config / manifest 收集 module 名称的 lane。<br>3. 让 `Generate()` 对 contributor 模块同样输出 `AS_FunctionTable_<Module>_*.cpp` 与 CSV/summary；runtime 侧继续沿用 `AddFunctionEntry()` 消费逻辑，无需立刻改 `Bind_BlueprintCallable.cpp`。<br>4. 第二阶段再把默认策略从“依赖白名单”迁到“module 自声明”，让项目模块或外部插件在不改 `AngelscriptRuntime.Build.cs` 的前提下，自动拿到 direct-bind lane。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`，`Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp`，以及新增的 contributor interface / manifest 文件 |
| 预估工作量 | M |
| 架构风险 | 如果 contributor 发现策略设计得过宽，function-table 生成规模会快速膨胀；因此第一阶段应保持 opt-in，而不是把所有 session modules 一次性放开。 |
| 兼容性 | 向后兼容。旧白名单逻辑和现有 `AS_FunctionTable_*` 产物可继续工作；新 contributor 只是新增一条更开放的自动化入口。 |
| 验证方式 | 1. 新建一个不在 `AngelscriptRuntime.Build.cs` dependency 列表中的项目模块，声明一个 `BlueprintCallable` `UFunction`，验证 contributor 开启前只有 reflective/live 路径，开启后会生成对应 `AS_FunctionTable_*` entry。<br>2. 复跑现有 `AngelscriptGeneratedFunctionTableTests`，确认内建模块的 direct/stub 统计不回退。<br>3. 抽样验证新模块的 `ClassFuncMaps` 在启动后已出现 direct `FFuncEntry`，而不是只能走 fallback。 |

### Arch-BP-28：`AddSkipEntry/AddSkipClass` 是 name-only 的旁路策略，只被 editor 生成阶段消费

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 插件用户在不改插件源码时，能否稳定地“排除/替换一个已有绑定” |
| 当前设计 | 当前确实公开了 `AddSkipEntry()` / `AddSkipClass()`，并且内建 `AngelscriptSkipBinds.cpp` 也在用；但这套 skip state 是 name-only side table，实际消费点集中在 `AngelscriptEditorModule.cpp` 的 editor 生成阶段。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:531-581` — skip API 只记录 `FName ClassName` 或 `(FName ClassName, FName FunctionName)`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSkipBinds.cpp:4-17` — 内建排除名单通过 `AddSkipEntry()` / `AddSkipClass()` 注册；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1094-1095` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1890-1894` — editor 扫类/扫函数时会调用 `CheckForSkipClass()` / `CheckForSkipEntry()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:962-1026,1366-1409` — runtime live scan 只看 `BlueprintType`、`NotInAngelscript`、`BlueprintCallable`、`ScriptCallable` 等条件，没有消费 skip API；`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:490-515` — `ShouldGenerate()` 也不读取 skip state。 |
| 优点 | 对于插件内部维护者，临时排除几个已知坏 case 非常便宜，直接加一条 `AddSkipEntry()` 就能影响 editor 生成结果。 |
| 不足 | 这条能力对外并不稳定。由这些代码可推断：外部模块即使调用同样的 skip API，也只能影响 editor DB/module 生成，不会同步影响 runtime non-DB live scan、UHT function-table 生成、手写 `Bind_*.cpp` 或 direct/fallback 决策；再加上 key 只有类名/函数名，没有 object path 或 signature hash，重载、重命名、跨模块同名时很难形成可靠 contract。结果是“我想在不改插件源码的前提下禁用一个绑定”并没有统一答案。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 导出面先统一进入 `FExported`，`FLuaEnv` 创建时集中消费 exported classes/functions/enums；扩展或缺省都围绕同一 registry 生效。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Binding.cpp:19-57`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:144-159` | 扩展/排除策略应挂在统一 registry 上，而不是只在某一个生成阶段读一份旁路名单。 |
| UnrealCSharp | `FClassRegistry` 既有 `AddClassDescriptor/AddFunctionDescriptor`，也有 `RemoveClassDescriptor/RemoveFunctionDescriptor/RemovePropertyDescriptor`，移除是正式 registry 操作，不是散落在扫描器里的特判。 | `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FClassRegistry.cpp:91-103,115-190` | 把“替换/移除已有绑定”做成 first-class registry contract，外部扩展者才能安全地覆盖低质量条目。 |
| puerts | 扩展声明通过 `UCodeGenerator` 和 `GatherExtensions()` 并入同一 declaration 输出，不是单独的 side table。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Public/CodeGenerator.h:10-26`；`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1110-1171,1708-1717` | 策略与扩展应进入同一主线 pipeline，避免某条 side table 只对单一路径生效。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把现有 skip API 升级成统一的 `BindingPolicyProvider`，让 editor/UHT/runtime/manual 四条 lane 读同一份决策。 |
| 具体步骤 | 1. 新增 `FAngelscriptBindingPolicyKey` 与 `FAngelscriptBindingPolicyDecision`，key 至少包含 `OwnerObjectPath`、`MemberName`、`SignatureHash(optional)`、`Kind(Class/Function/Property)`，decision 至少覆盖 `Skip`、`PreferDirect`、`PreferReflective`、`Alias`。<br>2. 保留 `AddSkipEntry()` / `AddSkipClass()`，但内部不再直接操作裸 `TSet`，而是写入默认 policy provider；这样旧调用点和现有 `AngelscriptSkipBinds.cpp` 不用立刻改。<br>3. 让 `AngelscriptEditorModule.cpp` 的 class/function 扫描、`AngelscriptFunctionTableCodeGenerator.ShouldGenerate()`、runtime `ShouldBindEngineType()` / `Bind_Defaults()`、以及未来外部 provider preflight 都统一查询 policy provider。<br>4. 在 CSV/dump/manifest 中写出 `PolicySource`、`DecisionKind`、`MatchedKey`，让“为什么这个绑定被排除/替换”可审计；第二阶段再开放 `AdditionalBindingPolicyModules`，允许项目或外部插件在不改插件源码的前提下追加策略模块。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSkipBinds.cpp`，`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`，`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`，以及新增的 policy provider / manifest 导出文件 |
| 预估工作量 | M |
| 架构风险 | 如果一开始就让 policy 覆盖 manual bind 和 direct-bind 结果，可能误伤现有内建条目；应先从 report-only 和 `Skip` 决策开始，再逐步开放 `PreferDirect/PreferReflective`。 |
| 兼容性 | 向后兼容。旧 `AddSkipEntry()` / `AddSkipClass()` 继续可用，只是内部存储和消费点变成统一 provider；没有新 policy module 时，现有内建 skip 行为可保持不变。 |
| 验证方式 | 1. 新增一个项目侧策略模块，对一个 engine class function 和一个 project class function 分别下发 `Skip`，验证 editor DB、UHT function-table、runtime live scan 的结果一致。<br>2. 复跑现有 `AngelscriptBindConfigTests` 与 generated-function-table 相关测试，确认旧 skip 行为没有回退。<br>3. 对一个重载函数补 `SignatureHash` 匹配测试，确认 policy 不会因为只按名字匹配而误伤同名重载。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P0 | Arch-BP-27 | UHT direct-bind 生成范围被 `AngelscriptRuntime.Build.cs` 白名单锁死，项目/外部模块默认进不了自动直连车道 | 自动化入口开放 | 高 |
| P1 | Arch-BP-28 | `AddSkipEntry/AddSkipClass` 只在 editor lane 生效，无法形成统一排除/替换 contract | 策略收敛与扩展点新增 | 高 |

---

## 架构分析 (2026-04-10 00:25)

### Arch-BP-29：当前只有“手写 `Bind_*.cpp`”与“反射扫描”两条作者入口，缺少可插拔 `recipe/generator` 中间层

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 新增一个 UE 类型时，绑定作者到底有哪些正式入口 |
| 当前设计 | 现在只有两条 lane：一条是 `FAngelscriptBinds` 提供的命令式 DSL，手写 `Bind_*.cpp` 直接往 engine 注册 method/global helper；另一条是 `Bind_BlueprintType` 依据 `BlueprintType` / `BlueprintCallable` / `ScriptCallable` 做反射扫描。两条 lane 最终都收敛成 `CallBinds()` 执行 lambda，没有第三种“先描述 recipe，再由工具链物化”的公开 contract。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:169-219` — `Method()` / `BindExternMethod()` 暴露的是立即注册式 DSL；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp:37-107` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp:25-37` — 典型手写 binder 逐条枚举方法和 lambda；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1029-1045` — 只有通过 `ShouldBindEngineType()` 的 `UClass` 才会进入自动声明 lane；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1317-1506` — 自动 lane 再扫描函数/属性并回填 `FAngelscriptClassBind`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1915-1921` — runtime 只知道统一 `CallBinds()`，不知道“这些 bind 是由哪种 recipe 生成的”。 |
| 优点 | 手写 lane 表达力极强，适合 `UObject` / `AActor` 这种需要补 helper、namespace 函数、特殊 metadata 的复杂 surface；反射 lane 则让 `BlueprintType` / `ScriptCallable` 能在不写 `Bind_*.cpp` 的情况下自动进入基本绑定面。 |
| 不足 | 对“新增一个 UE 类型”这件事，当前正式选项仍然是两极分化：1. 如果只接受 reflected surface，需要依赖 `BlueprintType` / `BlueprintCallable` / `ScriptCallable`，再触发 `Bind_BlueprintType` 与 editor 生成链做全量扫描；2. 如果需要 richer helper、alias、工厂函数或额外元数据，就必须回到插件源码里的手写 `Bind_*.cpp`、`SetPreviousBind...`、甚至 `FAngelscriptType` 扩展。当前没有给项目模块/外部插件一条“声明 recipe，由工具链自动产出 bind/声明产物”的中间车道。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | editor 侧存在单一 `Generator()` 总控，按顺序调度 `Class/Struct/Enum/Binding` generator；`FClassGenerator` 直接遍历 `UClass` 并为每个 class 写出独立生成文件。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:237-305`；`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp:16-25`；`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp:870-874` | 先把“如何从 UE 元数据生成绑定产物”提升成正式 generator phase，再把每个类型映射到稳定输出文件，避免作者只能在“全手写”和“全反射”之间二选一。 |
| puerts | `DeclarationGenerator` 不只生成内建声明，还会枚举实现了 `UCodeGenerator` interface 的类并执行 `ICodeGenerator::Execute_Gen()`，允许外部扩展把自定义生成逻辑并入同一工具链。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Public/CodeGenerator.h:10-26`；`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1710-1716` | 扩展点不要求修改核心插件源码，而是通过显式 generator interface 接入主生成流程。 |
| UnLua | runtime 公开 `ExportClass/ExportEnum/ExportFunction/AddType`，先把导出对象写入 `FExported` registry；`FLuaEnv` 创建时再统一消费 exported classes/functions/enums。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/Binding.h:21-43`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Binding.cpp:19-57`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:144-159` | 即便仍保留静态导出，也把“作者声明导出内容”和“runtime 消费导出内容”分成两个显式阶段，扩展契约比散落手写 binder 更清楚。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留现有手写 `Bind_*.cpp` 与 reflective `Bind_BlueprintType` 的前提下，增加一层公开的 `BindingRecipe` / `BindingGenerator` 中间层，让项目或外部插件可以“声明一个类型的绑定配方”，再由 editor/UHT 工具链物化为 bind artifact。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/` 新增 `FAngelscriptBindingRecipe` 与 `IAngelscriptBindingRecipeGenerator`，recipe 至少描述 `TargetType/ObjectPath`、`Methods`、`Properties`、`GlobalHelpers`、`Metadata`、`SourceKind(Manual/Reflective/Generated)`。<br>2. 在 `AngelscriptEditor` 或 `AngelscriptUHTTool` 新增 recipe 收集阶段：先调用外部 generator，再把现有 `Bind_BlueprintType` 产出的 reflected 信息也投影到同一 recipe IR，做到“不同来源，共用一种中间表示”。<br>3. 第一阶段先只支持把 recipe 物化为生成的 `ASRuntimeBind_*` 或 manifest，不替换现有手写 binder；这样项目模块可以不改插件源码，仅通过实现 generator + 声明 allowlist，就为某个 `UClass` family 生成 helper/alias bind。<br>4. 第二阶段再逐步把现有手写高重复 binder family 迁到 recipe producer，减少未来新增 UE 类型时回落到插件核心源码的概率。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`，`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`，以及新增的 recipe interface / recipe manifest / generator runner 文件 |
| 预估工作量 | L |
| 架构风险 | 如果 recipe 抽象只覆盖 reflected surface，复杂 helper family 仍会逃逸回手写 binder；如果抽象过宽，又可能把 today 直接可读的手写 binder 变成难以调试的 callback 表。第一阶段应明确只覆盖“可生成的公共子集”，不要试图一次取代全部 DSL。 |
| 兼容性 | 向后兼容。旧 `Bind_*.cpp`、旧 `Bind_BlueprintType`、旧 `CallBinds()` 保持可用；新 recipe/generator lane 先作为 opt-in 扩展入口，不要求现有插件用户迁移。 |
| 验证方式 | 1. 新建一个项目模块，实现 `IAngelscriptBindingRecipeGenerator`，为一个未写 `Bind_*.cpp` 的 `UClass` 生成额外 helper bind，验证无需修改 `AngelscriptRuntime` 源码也能出现在最终脚本 surface。<br>2. 对同一个类型同时保留 reflected lane 与 recipe lane，确认冲突检测、优先级和最终声明产物可审计。<br>3. 复跑绑定观测/声明导出相关自动化，确认 recipe producer 的输出最终仍通过同一条 bind pipeline 进入 engine。 |

### Arch-BP-30：自动产物仍是“全量扫描 + 固定批量分片”，新增一个类型会放大全局生成面

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 当前自动化绑定产物的粒度，是否足以支撑增量扩展 |
| 当前设计 | editor 端先全量扫描所有 `UClass`，再按 package key 填充 `RuntimeClassDB/EditorClassDB`；后续 `GenerateNativeBinds()` 不是按稳定 owner 输出产物，而是把 key 每 10 个打成一个 `ASRuntimeBind_* / ASEditorBind_*` 模块，最终 `BindModules.Cache` 只保存模块名列表。runtime 侧同时还会再跑一遍 `Bind_BlueprintType` 的全量 `TObjectRange<UClass>` 扫描。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1088-1160` — `GenerateBindDatabases()` 全量遍历 `TObjectRange<UClass>()`，以 package 名为 key 把 class 收进 `RuntimeClassDB/EditorClassDB`；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1005-1058` — `GenerateNativeBinds()` 使用固定 `ModuleCount = 10` 把 key 批量分进 `ASRuntimeBind_<index>` / `ASEditorBind_<index>`；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1285-1352` — `GenerateSourceFilesV2()` 按 `ModuleList` 把多个 package 的 class 混进同一个生成模块；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:583-601` — `BindModules.Cache` 只保存字符串数组，没有记录“哪个类型属于哪个 shard”；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp:27-31` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp:42-50` 以及 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp:103-115` — `Binds.Cache` 仍是单体序列化/反序列化；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1034-1045` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1366-1506` — runtime 绑定流程再次全量扫描所有 class；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1469-1489` — 启动时只会加载单体 `Binds.Cache` 和全部 `BindModules.Cache` 中列出的模块。 |
| 优点 | bulk pipeline 比较直观，当前实现成本低；生成模块数量可通过固定批量控制，不会瞬间把模块数打爆。 |
| 不足 | 这套设计对扩展者不够友好：1. 新增一个类型，通常意味着全量扫描与整批分片重算，而不是局部 regenerate；2. shard 名来自循环索引，不来自 package/type identity，外部插件用户很难回答“我的类型到底落在哪个 artifact”；3. `BindModules.Cache` 只有模块名，没有 owner 清单，无法做细粒度 diff、脏区重建或 targeted invalidation。结果是自动化虽然存在，但粒度仍偏“全局批处理”。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | `FClassGenerator` 遍历 `UClass` 时按 class 写单独输出文件，并把 `FileName` 记入 generator file list。 | `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp:16-25`；`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp:870-874` | artifact owner 是单个 class，不是“每 10 个 package 一组”的临时批次，因此增量更新与责任归属都更清楚。 |
| puerts | `BlueprintTypeDeclInfoCache` 以 package 为键缓存 `NameToDecl`、`Changed`、`IsExist`；生成时只对 `Changed` 的 blueprint/package 重建，并把对象级声明写回对应 package 槽位。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Public/TypeScriptDeclarationGenerator.h:61-70`；`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:387-412`；`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:530-550` | 先把 dirty model 和 package/object owner 建起来，再谈生成，能显著降低“一处小变更牵动整库重写”的概率。 |
| UnLua | IntelliSense generator 监听 AssetRegistry 事件，`UpdateAll()` / `Export()` 按 asset 或 field 增量导出，保存时也是按 `ModuleName/FileName` 写单独 `.lua` 文件。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:42-55`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:58-80`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:222-233`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp:63-99` | 即使不是 runtime bind 本体，也把“生成产物归属哪个对象/文件”做成稳定 mapping，方便增量更新和作者定位。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 给自动化绑定产物补一层稳定 `BindingArtifactManifest`，把今天的全局批处理改成“先判脏、再生成、最后装配”的增量管线。 |
| 具体步骤 | 1. 在 `AngelscriptEditor` 新增 `FAngelscriptBindingArtifactManifest`，记录 `ObjectPath`、`SourcePackage`、`ArtifactKind(BindDB/GeneratedModule/Declaration)`、`OutputShard`、`ContentHash`、`SourceKind`；先与现有 `Binds.Cache` / `BindModules.Cache` 并存。<br>2. 把 `GenerateBindDatabases()` 的全量扫描结果先落成 manifest entry，再由 manifest 决定是否需要重写某个 shard；第一阶段可以继续全量扫描，但输出必须是“稳定 owner -> 稳定 shard”，不要再按循环索引每 10 个 key 分组。<br>3. 让生成模块名称改为 package/module 身份驱动，例如 `ASRuntimeBind_<PackageHash>` 或 `ASRuntimeBind_<ModuleName>`，并把 shard 所属对象列表写入 manifest；`BindModules.Cache` 仅作为兼容层保留模块名数组。<br>4. 第二阶段再接 AssetRegistry/UHT changed-set，只重建 dirty package 或 dirty object 对应的 shard；runtime 仍可在启动时汇总读取，但编辑器不再需要“一处小变更重写整批模块”。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，以及新增的 manifest 定义与生成器辅助文件 |
| 预估工作量 | L |
| 架构风险 | 若 shard 拆得过细，模块数和 build graph 可能膨胀；若继续保持过粗，又达不到增量收益。第一阶段应优先追求“stable owner + stable shard”，而不是盲目最细粒度。 |
| 兼容性 | 向后兼容。旧 `Binds.Cache` 与 `BindModules.Cache` 仍可继续生成/读取；manifest 先做旁路观测与 editor 增量优化，不改变现有 runtime 入口。 |
| 验证方式 | 1. 新增一个 project `UClass` 和一个 `BlueprintCallable` `UFunction`，比较改造前后生成输出，确认只会新增/修改对应 shard，而不是无关模块全部变化。<br>2. 对同一 package 做重命名与删除，验证 manifest 能准确反映 owner 迁移，并保持 runtime 最终绑定面不变。<br>3. 复跑 bind 相关自动化与生成脚本，确认旧 `BindModules.Cache` 兼容读取不回退。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-BP-29 | 当前缺少位于“手写 DSL”和“反射扫描”之间的正式 `recipe/generator` 扩展层 | 工具链扩展点新增 | 高 |
| P1 | Arch-BP-30 | 自动化绑定产物仍是全量扫描与固定批量分片，新增类型会放大全局生成面 | 生成粒度与 artifact 治理 | 高 |

---

## 架构分析 (2026-04-10 00:33)

### Arch-BP-31：bind control plane 依赖 `BindName`，但大量生产绑定实际只有不稳定的 synthetic identity

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 绑定贡献者的稳定身份，以及配置/审计是否能长期指向同一个 bind contribution |
| 当前设计 | `FBind` 既支持显式 `BindName`，也支持只传 `BindOrder` 或只传 lambda；后一类会在注册时自动补成 `UnnamedBind_<N>`。但 runtime 配置、state dump、自动化观测和禁用逻辑全部只看这个 `BindName`。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:438-465` — `FBind` 有三组不带 `BindName` 的构造函数；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:138-153` — `RegisterBinds()` 对空名调用 `MakeUnnamedBindName()` 生成 `UnnamedBind_<N>`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:161-203` — `GetAllRegisteredBindNames()`、`GetBindInfoList()`、`CallBinds()` 全部以 `BindName` 作为唯一控制键；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1928-1941` — `CollectDisabledBindNames()` 只合并 settings/runtime config 中的 `FName` 列表；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp:37`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp:25`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FMath.cpp:7`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Logging.cpp:6`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Debugging.cpp:68` — 典型生产 bind 直接使用无名构造；`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp:370-410` — 测试明确把自动生成 `UnnamedBind_*` 视为“backward compatibility” contract；`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp:849-867` — dump 输出和 skip 来源归因同样只写 `BindName`。 |
| 优点 | binder 作者负担很低，现有 126 个 `Bind_*.cpp` 大量沿用 order-only/unnamed 写法，不需要额外维护显式 id。 |
| 不足 | `UnnamedBind_<N>` 本质是注册顺序衍生出来的 synthetic name，不是稳定 owner identity；一旦未来继续引入外部 provider、自动生成 bind 或重排静态初始化顺序，配置、dump 和审计仍只能指向“当次启动序号”，很难长期安全地禁用、替换或追踪单个 contribution。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | registry 直接按 `UStruct*` 和 hash 建立 class/function/property descriptor，支持显式 add/remove，不依赖启动顺序生成临时名字。 | `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FClassRegistry.cpp:91-190`；`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FCSharpBind.cpp:160-222` | 把“绑定贡献者是谁”建成稳定 descriptor key，后续才能安全做移除、覆盖和差异分析。 |
| puerts | `BlueprintTypeDeclInfoCache` 以 package 为键、`NameToDecl` 以对象名为键保存声明产物；扩展生成器通过 `UCodeGenerator` 接口并入主流程。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Public/TypeScriptDeclarationGenerator.h:61-70`；`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:389-444,541-549,1714-1716`；`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Public/CodeGenerator.h:11-26` | 即便主要是 declaration lane，也先把 artifact owner 固定到 package/object，而不是使用运行时临时名字。 |
| UnLua | `FExported` 把 reflected class、non-reflected class、global function、type interface 分别放进显式 map/array，再由 `FLuaEnv` 启动时统一消费。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Binding.cpp:17-98`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:144-159` | 扩展对象先进入显式 exported registry，identity 由类名/类型名/函数对象决定，而不是后置生成 synthetic name。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 为每个 bind contribution 增加稳定 `BindId`，把今天的 `BindName` 从唯一主键降级为“展示名/兼容别名”。 |
| 具体步骤 | 1. 在 `Core/AngelscriptBinds.h` 引入 `FAngelscriptBindDescriptor`，至少包含 `BindId`、`DisplayName`、`BindOrder`、`LegacyBindName`、`SourceFile`、`SourceLine`。<br>2. 保留现有 `FBind` 构造函数，但内部统一落到 descriptor 注册；对无名旧路径，先用 `__FILE__ + __LINE__` 或等价 source-location hash 生成稳定 `BindId`，继续保留 `UnnamedBind_<N>` 作为 `LegacyBindName` 兼容字段。<br>3. 在 `UAngelscriptSettings` / runtime config 新增 `DisabledBindIds`，`CollectDisabledBindNames()` 扩展为同时合并 `DisabledBindIds`；第一阶段仍允许旧 `DisabledBindNames` 工作。<br>4. 扩展 `GetBindInfoList()`、state dump 与测试快照，输出 `BindId`、`DisplayName`、`LegacyBindName`、`SourceFile/Line`，让“这个 bind 到底是谁”变成可审计 contract。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` |
| 预估工作量 | M |
| 架构风险 | 如果一开始就废弃 `DisabledBindNames`，会直接打破现有配置与自动化；因此必须先做双轨兼容，再逐步把文档和新 provider 迁到 `BindId`。 |
| 兼容性 | 向后兼容。旧 named bind、旧 `UnnamedBind_*` 和现有 `DisabledBindNames` 都可保留；新系统只是额外提供稳定 id，不要求一次性改完所有 `Bind_*.cpp`。 |
| 验证方式 | 1. 新增自动化用例，验证 order-only bind 能生成稳定 `BindId` 且 state dump 可见。<br>2. 对同一个 bind 分别用 `DisabledBindNames` 与 `DisabledBindIds` 关闭，确认执行结果一致。<br>3. 在测试中插入额外 bind contribution，确认既有 bind 的 `BindId` 不会像 `UnnamedBind_<N>` 一样随插入顺序漂移。 |

### Arch-BP-32：manual bind 可写入的函数 trait 远多于 `BindDB` 能表达的 schema，生成/反射 lane 无法达到 live bind 语义保真

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 绑定语义是否有统一可序列化合同，还是只有手写 live DSL 才能表达完整 trait |
| 当前设计 | `FAngelscriptBinds` 的 live DSL 可以在注册后继续给“上一个 bind”写入 `NoDiscard`、`PassScriptFunctionAsFirstParam`、`PassScriptObjectTypeAsFirstParam`、`CompileOutType`、`GeneratedAccessor`、`Callable` 等 trait；但 `FAngelscriptMethodBind` 和 `Helper_FunctionSignature` 的 DB roundtrip 只覆盖 `WorldContextArgument`、`DeterminesOutputTypeArgument`、`Static/Global`、`NotProperty`、`Trivial` 这类少数字段。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:318-401,422-428,542-549` — live DSL 提供 `DeprecatePreviousBind`、`SetPreviousBindNoDiscard`、`PreviousBindPassScriptFunctionAsFirstParam`、`PreviousBindPassScriptObjectTypeAsFirstParam`、`CompileOutPreviousBind`、`CompileOutPreviousBindAsMethodChain` 等语义写入；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h:56-83` — `FAngelscriptMethodBind` 序列化字段只有 `Declaration`、`WorldContextArgument`、`DeterminesOutputTypeArgument`、`ClassName/ScriptName`、`bStaticInUnreal`、`bStaticInScript`、`bGlobalScope`、`bNotAngelscriptProperty`、`bTrivial`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h:336-391` — `InitFromDB()`/`WriteToDB()` 的读写面与 `FAngelscriptMethodBind` 完全一致；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h:414-457` — `ModifyScriptFunction()` 只补 world-context、output-type、property/protected、deprecated/editor-only，没有 `NoDiscard`、first-param metadata 或 compile-out 入口；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UUserWidget.cpp:293-350`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_USceneComponent.cpp:164`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp:311`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp:1395-1639`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSoftObjectPtr.cpp:553` — 生产 bind 已在依赖 `RequiresWorldContext`、`NoDiscard`、`PassScriptFunctionAsFirstParam`、`PassScriptObjectTypeAsFirstParam`、`EditorOnly` 这类 live-only trait。 |
| 优点 | 手写 binder 表达力强，某个 family 需要 AngelScript 特有 trait 时，不必先扩 schema 就能直接落地。 |
| 不足 | 一旦想把同样的语义交给 `BindDB`、UHT/codegen、外部 generator 或未来 manifest/declaration 产物复用，就会遇到 schema ceiling：manual lane 可以做到的 first-param metadata、`NoDiscard`、compile-out、generated accessor 等，自动化 lane 没有统一字段可落。结果是“能自动化的绑定”天然比“手写 live bind”语义更弱。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | class/function/property 都进入 descriptor registry，并通过 hash 作为 first-class entry 注册和移除，而不是只靠 live mutation 改运行时对象。 | `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FClassRegistry.cpp:91-190`；`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FCSharpBind.cpp:160-222` | 先有 descriptor，再有 runtime 回填；这样语义扩展是加 descriptor 字段，不是再造一条 live-only side effect。 |
| puerts | declaration generator 把每个类型的声明文本保存在 `BlueprintTypeDeclInfoCache.NameToDecl`，并允许 `UCodeGenerator` 扩展生成逻辑并入同一主流程。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Public/TypeScriptDeclarationGenerator.h:61-70`；`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:437-444,541-549,1714-1716` | 即便是 IDE/declaration lane，也先把语义落成 artifact；新增语义时扩展 cache/生成器，比靠 live trait side effect 更容易保持一致。 |
| UnLua | `FFunctionDesc` 在构造时统一解析 `UFunction` 的参数、返回值、latent 参数和 out 参数；`FunctionRegistry` 再缓存并复用这份 descriptor 调用 Lua。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:31-72`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/FunctionRegistry.cpp:22-71`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:151-152` | 运行时调用 contract 建立在显式 descriptor 上，而不是注册后再零散 patch 一批 VM 内部字段。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把今天散落在 `PreviousBind*` API 里的 runtime trait 抽成统一 `FunctionTraitsDescriptor`，让 manual、DB、generator、dump 四条 lane 共享同一份语义模型。 |
| 具体步骤 | 1. 在 `Core/AngelscriptBindDatabase.h` 新增 `FAngelscriptFunctionTraitsDescriptor`，第一阶段至少覆盖 `bNoDiscard`、`FirstParamMetaData`、`CompileOutType`、`bGeneratedAccessor`、`bPropertyAccessor`、`bCallable`、`bProtected`、`bEditorOnly`、`bDeprecated`。<br>2. 把 `FAngelscriptMethodBind` 和 `Helper_FunctionSignature.h` 扩到读写这份 traits descriptor；`ModifyScriptFunction()` 不再只补 world-context/output-type，而是统一消费 traits descriptor。<br>3. 在 `FAngelscriptBinds` 中引入 pending bind semantic state：现有 `SetPreviousBind...()` / `PreviousBindPass...()` / `CompileOutPreviousBind...()` 继续保留 API，但内部同时写 runtime `asCScriptFunction` 和 pending descriptor。<br>4. 第二阶段再让 state dump、manifest、未来 declaration export 都输出这份 traits descriptor，确保“手写 bind 与自动生成 bind 到底差了什么”可观测。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`，以及未来生成 lane 对接文件 |
| 预估工作量 | M |
| 架构风险 | 如果一口气把全部 trait 都塞进 schema，容易把旧 cache 兼容和生成路径一起搅动；建议先覆盖已经在生产 bind 中高频使用、且对 runtime 行为有直接影响的 trait，再扩 editor/docs 侧字段。 |
| 兼容性 | 向后兼容。旧 `Bind_*.cpp` 无需改写；旧 `Binds.Cache` 读取时可给新增 traits 字段填默认值。manual lane 仍可立即生效，只是多了一份可序列化镜像。 |
| 验证方式 | 1. 新增自动化：对一个手写 bind 同时设置 `NoDiscard` 与 `PassScriptObjectTypeAsFirstParam`，确认 state dump/manifest 能读到 traits descriptor。<br>2. 为一个走 `BindDB` 或反射 lane 的函数补 traits roundtrip 测试，确认载入后 `ModifyScriptFunction()` 能恢复同样语义。<br>3. 回归 `Bind_TArray`、`Bind_UUserWidget`、`Bind_AActor` 相关测试，确认 traits schema 化后不改变现有运行时行为。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-BP-32 | manual bind 与 `BindDB` 之间缺少统一的函数 trait schema，自动化 lane 语义保真不足 | 合同收敛 + 语义模型补齐 | 高 |
| P2 | Arch-BP-31 | bind control plane 依赖 `BindName`，无名 bind 的 synthetic identity 不适合作为长期扩展合同 | 控制面标识治理 | 中 |

---

## 架构分析 (2026-04-10 00:43)

### Arch-BP-33：手写 bind 的控制粒度仍停留在 bind family，外部扩展无法按 symbol 精准替换

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 手写绑定面的禁用、替换与外部覆盖粒度 |
| 当前设计 | `BindScriptTypes()` 只对 `FBind` family 执行 `CallBinds(DisabledBindNames)`；一个 `FBind` lambda 往往一次性注册整组 method/global helper，而不是拆成可独立治理的 symbol contribution。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1915-1941` — runtime 只收集 `DisabledBindNames`，随后整组执行或整组跳过；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:176-214` — `GetBindInfoList()` 和 `CallBinds()` 的观测/控制面只有 `BindName/BindOrder/bEnabled`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:497-581` — per-function `AddFunctionEntry/SkipFunctionEntry` 只存在于 direct native function-table lane，没有覆盖 `Method()` / `BindGlobalFunction()` 产出的手写 symbol；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp:37-107,273-356,510-579` — `Bind_UObject_Base`、`Bind_UClass_Base`、`Bind_UObject_Operations` 分别在单个 `FBind` 里打包了大量 `UObject/UClass` method 与 `NewObject/FindClass/GetAllClasses` 等 global helper。 |
| 优点 | binder 作者负担低，维护者可以按 family 组织源码，也便于用一个 `FBind` 控制初始化顺序。 |
| 不足 | 对插件用户或项目模块来说，“我只想替换/禁用一个 symbol，而不是整组 family”几乎没有正式车道。即使未来开放 provider discovery，外部扩展若想覆盖 `NewObject()`、`GetTypedOuter()`、`FindClass()` 这类单个 API，仍要么关掉整组 bind，要么直接改插件源码。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | registry 把 class/function/property 都作为 first-class descriptor 管理，支持分别 `Add*` / `Remove*`，函数与属性还用 hash 建独立索引。 | `Reference/UnrealCSharp/Source/UnrealCSharp/Public/Registry/FClassRegistry.h:19-45`；`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FClassRegistry.cpp:135-190`；`Reference/UnrealCSharp/Source/UnrealCSharp/Public/Registry/FClassRegistry.inl:56-66` | 替换与移除发生在 symbol 粒度，而不是“整个生成批次”或“整个初始化 lambda”粒度。 |
| UnLua | `Binding.h` 公开 `ExportClass`、`ExportFunction`、`AddType`，`Binding.cpp` 把 class/function/type 分别放入显式 registry，`FLuaEnv` 启动时逐项消费。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/Binding.h:21-43`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Binding.cpp:19-57`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:146-159` | 先把“贡献了哪些函数/类型”建成显式列表，再做统一注册，外部扩展更容易做细粒度覆盖。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留 `FBind` family 写法的同时，引入 symbol-level contribution registry，把“初始化批次”与“可治理的绑定元素”分层。 |
| 具体步骤 | 1. 在 `Core/AngelscriptBinds.h` 新增 `FAngelscriptSymbolContributionDescriptor`，至少包含 `BindingSymbolKey`、`OwningBindId`、`ProviderName`、`ContributionKind(Method/Global/Property/Type)`、`InstallResultRef`。<br>2. 保留现有 `FBind` 与 `Method()/BindGlobalFunction()/BindProperty()` API，但内部 dual-write：继续即时注册到 AngelScript engine，同时把每个 symbol 记录到当前 family 的 contribution list。<br>3. 在 `UAngelscriptSettings` / runtime config 新增 `DisabledBindingSymbols`，让 symbol 级策略优先于 `DisabledBindNames`；旧 `DisabledBindNames` 继续作为粗粒度总开关保留。<br>4. 第一阶段先迁移高密度 family：`Bind_UObject_Base`、`Bind_UClass_Base`、`Bind_UObject_Operations`、`Bind_Actors`，验证无需拆文件也能获得 symbol 粒度治理。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`，以及对应测试文件 |
| 预估工作量 | L |
| 架构风险 | 如果 symbol descriptor 与 live 注册结果不同步，会出现“实际已绑定但 registry 未记录”或相反的分叉；第一阶段必须以 dual-write + dump 校验保证两者一致。 |
| 兼容性 | 向后兼容。旧 `FBind`、旧 `DisabledBindNames`、现有 `Bind_*.cpp` 都可继续使用；新 registry 先作为更细粒度的控制与诊断层加入。 |
| 验证方式 | 1. 新增自动化：只禁用 `Bind_UObject_Operations` 中的 `NewObject()` symbol，确认同 family 的 `FindObject()`、`GetTransientPackage()` 仍可用。<br>2. 新增项目侧扩展示例，以更高优先级替换单个 global helper，而不关闭整个 family。<br>3. 回归现有 `BindConfig` / `MultiEngine` 测试，确认未配置 symbol 策略时行为与旧版完全一致。 |

### Arch-BP-34：`BlueprintCallable` 直连函数表按 raw `UFunction` 名命中，而脚本面冲突检测按 `ScriptName/Declaration` 去重，key model 已经分叉

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | native callable 解析键与脚本可见名称是否是同一套合同 |
| 当前设计 | `BlueprintCallable` direct native lane 先用 `Function->GetFName()` 去 `ClassFuncMaps` 取 `FFuncEntry`，而真正暴露给脚本的名字/namespace/alias 则稍后由 `FAngelscriptFunctionSignature` 计算；event lane 又单独维护了一份按 `Signature.ScriptName` 建的 map。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp:34-48` — `BindBlueprintCallable()` 先按 raw `Function->GetFName().ToString()` 从 `GetClassFuncMaps()` 查 `FFuncEntry`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/FunctionCallers.h:384-389` — `FFuncEntry` 本身不记录 `ScriptName/Namespace/Alias`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h:85-120,123-175,251-323` — `FAngelscriptFunctionSignature` 之后才根据 `ScriptName` meta、前缀剥离、namespace 规则、`ScriptMixin`、`ScriptGlobalScope` 生成最终 `ScriptName/ClassName/Declaration`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp:166-245` — 重复检测走的是 `Signature.ScriptName` 与 `Signature.Declaration`，不是 raw native name；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp:640` — event lane 额外维护 `GBlueprintEventsByScriptName`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1422-1460` — BlueprintGetter/Setter 又会生成 `GetX/SetX` alias，并通过 `BindFunctionWithAdditionalName()` 把原始 `UFunction` 绑定到新脚本名。 |
| 优点 | raw `UFunction` 名直连 `FFuncEntry` 很适合复用现有 UHT/function-table 产物，也避免 script-facing rename 影响 native pointer 查找。 |
| 不足 | 当前至少并存三套 key：`UFunction` raw name、`ScriptName/Namespace/Declaration`、event lane 的 `ScriptName` map。对外部扩展者而言，这意味着“我在脚本里看到的名字”与“我必须命中的 native key”不是同一个合同；一旦存在 `ScriptName`、getter/setter alias、mixin 或 namespace 重写，覆盖、审计、冲突诊断都会出现理解成本和隐藏分叉。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | declaration generator 先以最终暴露的 `FunctionKey(FunctionName, IsStatic)` 组织 overload，再统一输出 extension/native/Blueprint 三类函数。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Public/TypeScriptDeclarationGenerator.h:39-55`；`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1100-1185` | 先稳定“脚本面函数键”，再把不同来源的 overload 汇入同一输出，alias 与扩展不会晚于 key 解析。 |
| UnrealCSharp | runtime 先把 `UFunction` 编码成稳定 symbol，再用 `AddFunctionHash` / `AddFunctionDescriptor` 进入 registry；查找依赖的是 descriptor/hash，不是临时 `Find(Name)`。 | `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FCSharpBind.cpp:179-223`；`Reference/UnrealCSharp/Source/UnrealCSharp/Public/Registry/FClassRegistry.inl:56-66` | native 解析键与 registry contract 是显式对象，可在后续再映射到宿主语言表面，而不是把 raw name lookup 和 surface alias 混在一个流程里。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 `BlueprintCallable`/event/property accessor 三条 lane 的“native lookup key”与“script surface key”显式分层，并让两者都进入统一 registry。 |
| 具体步骤 | 1. 新增 `FAngelscriptNativeCallableKey`（至少含 `OwnerObjectPath`、`UnrealFunctionName`、`NativeSignatureHash`）和 `FAngelscriptScriptSurfaceKey`（至少含 `ContainerName/Namespace`、`ScriptName`、`OverloadHash`、`SurfaceKind(Method/Global/Getter/Setter/Event)`）。<br>2. 扩展 `AddFunctionEntry()` / `FFuncEntry`，让 UHT 与手写 direct bind 产出 native key，同时可附带一个或多个 surface alias；旧 `UClass* + FString Name` 接口保留，并内部生成兼容 key。<br>3. 把 `BindBlueprintCallable()`、`BindBlueprintEvent()`、`BindFunctionWithAdditionalName()`、generated getter/setter 统一改成先构建 `ScriptSurfaceKey` 再做冲突检测；`GBlueprintEventsByScriptName` 则收敛为同一 registry 的 event 视图。<br>4. DB roundtrip 与 dump/manifest 同时写 native key 和 surface key，确保“同一个 raw `UFunction` 暴露出几个脚本名”可观测；旧 raw-name replay 先保留为 fallback。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/FunctionCallers.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`，以及后续 UHT/export 文件 |
| 预估工作量 | M |
| 架构风险 | 若直接替换现有 raw-name lookup，容易影响大量已生成 `AS_FunctionTable_*` 产物；第一阶段应坚持 dual-key 写入与 raw-name fallback，不要一口气切断旧入口。 |
| 兼容性 | 向后兼容。旧 function-table、旧 `DBBind.UnrealPath` 和旧 raw-name 命中规则都可继续工作；新增的是更完整的 surface key 与 alias registry。 |
| 验证方式 | 1. 选取一个带 `meta=(ScriptName=...)` 的 `BlueprintCallable`、一个 `K2_` 前缀函数、一个 `ScriptMixin` static function、一个 generated getter/setter，验证 native key 与 surface key 都能被 dump 出来。<br>2. 新增测试，确认 external provider 可以按脚本面 alias 报告冲突或追加 overload，而不必猜 raw `UFunction` 名。<br>3. 回归 `GeneratedFunctionTable`、Blueprint reflective fallback 与 property accessor 相关测试，确认 dual-key 引入后现有脚本 API 不变。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-BP-33 | 手写 bind 的控制粒度停留在 family，外部扩展无法按 symbol 精准替换 | 控制面细粒度化 | 高 |
| P1 | Arch-BP-34 | native callable raw key 与 script-facing key 分叉，alias/扩展/诊断难以统一 | 解析键模型收敛 | 高 |

---

## 架构分析 (2026-04-10 00:53)

### Arch-BP-35：type admission gate 在 `runtime/editor/UHT` 三条 lane 上分叉，新增类型会出现“局部生效”

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 自动绑定的准入规则是否是单一合同 |
| 当前设计 | 当前至少有三套独立 gate：editor `GenerateBindDatabases()` 只收“非 deprecated/abstract、header 可定位、非 `Private`、且至少有一个 `BlueprintCallable`”的 `UClass`；runtime `ShouldBindEngineType()` 则接受 native `BlueprintType`，或任意带 `BlueprintCallable/BlueprintEvent` 的类；UHT `AngelscriptFunctionTableExporter` 又单独把 `BlueprintCallable/BlueprintPure` 视为可导出的函数。更关键的是，`Bind_Defaults()` 真正绑定函数时还会接受 `ScriptCallable`。这意味着“一个类型/函数能否进入声明、DB、direct function-table、最终脚本 surface”并不是同一份决策。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1088-1160` — editor 生成只接收通过 header/path/`BlueprintCallable` 条件的类，并按 runtime/editor DB 分流；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:962-1024` — `ShouldBindEngineType()` 只检查 native、`BlueprintType`、`NotInAngelscript/NotBlueprintType`、以及 `BlueprintCallable | BlueprintEvent`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1397-1406` — class 一旦进入 `Bind_Defaults()`，函数层面又会绑定 `BlueprintEvent`、`BlueprintCallable | BlueprintPure`、以及 `ScriptCallable`；`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:56-76` — UHT exporter 把 `BlueprintCallable | BlueprintPure` 视为可重建 direct entry 的候选；`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:53-78,334-385` — UHT lane 还叠加了一层“仅处理 `AngelscriptRuntime.Build.cs` 依赖模块”的模块白名单。 |
| 优点 | 每条 lane 都能按自己的成本模型做局部优化：editor lane 关心 header 可达性与生成源码可编译，runtime lane 关心 cooked/非 editor 环境可用性，UHT lane 关心能否重建 `ERASE_*` macro。 |
| 不足 | 这会让“新增一个 UE 类型需要几步”变成猜谜：1. `BlueprintPure-only` 或 `ScriptCallable-only` class 可能在函数层面明明可绑定，却因为 class gate 不一致而根本不进 declaration/DB；2. `BlueprintEvent-only` class 可被 runtime type gate 接纳，但 editor lane不会因为它没有 `BlueprintCallable` 而生成 DB/module 产物；3. UHT 可能为某个函数产出 direct-entry 诊断，但 runtime/editor class lane 根本没把 owning class 视为候选。结果不是“完全自动化”或“完全手写”，而是最难排查的 partial coverage。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | editor 侧有单一 `Generator()` 总控，`BeginGenerator()` 之后按固定阶段串行调度 `FClassGenerator`、`FStructGenerator`、`FEnumGenerator`、`FBindingClassGenerator` 等生成器；`FClassGenerator` 自己遍历 `UClass` 并把输出文件注册到 `FGeneratorCore`。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:237-297`；`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp:16-25,870-874` | 先有统一 generation driver，再有各子阶段；class eligibility 不会分别散落在 runtime/editor/UHT 三处各自推导。 |
| puerts | declaration lane 通过 `BlueprintTypeDeclInfoCache` 保存 package 级声明状态，又通过 `FunctionKey`/`FunctionOutputs` 统一组织最终函数输出；`GatherExtensions()` 追加的 template/native extension 也并入同一份 `Outputs`。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Public/TypeScriptDeclarationGenerator.h:39-70`；`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:387-446,1110-1130` | 即便来源不同，最终也先汇总到统一 output/cache 结构，再落盘；不会出现“扩展 lane 一套 key、基础 lane 另一套准入”的长期分叉。 |
| UnLua | 导出面不是通过多处隐式条件推断，而是显式调用 `ExportClass/ExportFunction/AddType` 写入 `FExported`；`FLuaEnv` 创建时统一消费这些 exported classes/functions/enums。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/Binding.h:21-43`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Binding.cpp:19-77`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:144-159` | 哪些类/函数会进入 runtime surface 是显式 export contract，而不是多处启发式 gate 的交集。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先补一层 authoritative `BindingAdmission` 合同，把 today 分散在 `GenerateBindDatabases()`、`ShouldBindEngineType()`、UHT exporter 的准入判断收敛成可审计的 lane decision。 |
| 具体步骤 | 1. 在 `AngelscriptRuntime/Core` 或 editor/runtime 共用位置新增 `FAngelscriptBindingAdmissionDecision` 与 `FAngelscriptBindingAdmissionReason`，至少覆盖 `ClassPath/FunctionPath`、`bEmitDeclaration`、`bEmitBindDatabase`、`bEmitFunctionTable`、`bEmitGeneratedShard`、`ReasonCodes`。<br>2. 先把 C++ 侧 `GenerateBindDatabases()` 与 `ShouldBindEngineType()` 改成调用同一个 admission evaluator；第一阶段保留旧逻辑旁路比对，发现 decision 不一致时把 diff 写入 CSV/JSON，不立即改变 runtime 行为。<br>3. 由于 UHT tool 是 C#，不要强行直接复用 C++；改为让 editor 产出 `BindingAdmissionReport.json`（或等价 CSV），`AngelscriptFunctionTableExporter` 读取这份 report，把 `AS_FunctionTable_SkippedEntries.csv` 的 reason code 与 class/function 资格判断对齐到同一套 vocabulary。<br>4. 第二阶段再让 state dump/automation test 输出 admission 决策，给新增类型一个明确答案：是 `Auto-All`、`DeclarationOnly`、`FunctionTableOnly`、还是 `ManualOnly`。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`，`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`，`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`，以及新增的 admission evaluator / report DTO 文件 |
| 预估工作量 | M |
| 架构风险 | 如果一开始就强制三条 lane 共用一份过于严格的 gate，可能误伤当前依赖局部特判的 class/function。第一阶段应坚持 `report-only + diff`，先把分叉面量化出来，再逐项收敛。 |
| 兼容性 | 向后兼容。旧 gate 可在一段时间内保留为 fallback；新增的是 admission report 和统一 reason code，不会直接破坏现有脚本 surface。 |
| 验证方式 | 1. 添加四组样本：`BlueprintPure-only` class、`ScriptCallable-only` class、`BlueprintEvent-only` class、`Private` header class，验证 admission report 能清晰区分每条 lane 的去留。<br>2. 对同一组样本对比 `GenerateBindDatabases()`、`Bind_BlueprintType`、`AS_FunctionTable_SkippedEntries.csv`，确认 reason code 与 lane flag 一致。<br>3. 回归现有 bind 生成与运行时自动化，确认在未切换强制模式前最终脚本 API 不变。 |

### Arch-BP-36：自动生成绑定产物仍绑定在单一 `Angelscript` 插件根目录，外部插件无法自带 self-contained bind shard

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 外部插件/项目模块能否在不改核心插件分发结构的前提下交付自己的生成绑定产物 |
| 当前设计 | 生成产物的发现仍是 single-root：editor `GenerateNativeBinds()` 会清空全局 `BindModuleNames`，统一生成 `ASRuntimeBind_*` / `ASEditorBind_*` 名称，并把模块名列表写成一份 `BindModules.Cache`；runtime 初始化时只会 `FindPlugin("Angelscript")`，然后从这个插件根目录读取该 cache 并按模块名加载。cache 本身只存字符串数组，不带 owner plugin、manifest path 或 artifact root。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1005-1058` — editor 侧统一生成 `ASRuntimeBind_*` / `ASEditorBind_*`，并把模块名塞进同一个全局数组；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1077` — 最终只写一份 `BindModules.Cache`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:583-601` — `SaveBindModules/LoadBindModules` 只读写 `TArray<FString>`，没有 owner/path/version 信息；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1473-1488` — runtime 仅查询 `Angelscript` 插件 base dir，并从这一个根目录载入 cache 后逐个 `LoadModule()`。 |
| 优点 | 对当前 host project 模式来说，bootstrap 成本很低：核心插件知道唯一 cache 路径，也不需要处理多 provider 合并、优先级或冲突。 |
| 不足 | 这与“把 `Plugins/Angelscript` 做成可复用交付物”的目标仍有冲突：即使未来项目侧/外部插件获得了生成 binding 的能力，它也无法自带独立 manifest 与 shard，而必须把产物并入核心插件的单一 cache 体系。换句话说，问题不再只是“有没有 extension API”，而是“生成产物的 delivery topology 仍然假定只有一个 owner”。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 扩展贡献不是通过单个插件根目录上的模块列表发现，而是通过公开 `ExportClass/ExportFunction/AddType` API 写入共享 export registry；辅助宏 `EXPORT_UNTYPED_CLASS`、`EXPORT_FUNCTION_EX` 让任意模块都能把 class/function 导出到该 registry，`FLuaEnv` 启动时统一消费。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/Binding.h:21-43`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Binding.cpp:19-77`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaEx.h:442-460,570-586`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:144-159` | provider 可以来自任意模块，runtime 只认 registry contract，不认“某一个插件目录里的唯一 cache 文件”。 |
| puerts | `DeclarationGenerator` 不是固定读取某个插件目录下的扩展列表，而是遍历 `GetSortedClasses()`，对所有实现了 `UCodeGenerator` 的类执行 `ICodeGenerator::Execute_Gen()`。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Public/CodeGenerator.h:10-26`；`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1710-1716` | 扩展发现以 interface/object discovery 为中心，而不是硬编码到单一插件根路径。 |
| UnrealCSharp | 增量生成由 listener 和 dynamic generator 按变化资产/文件触发，生成 ownership 跟着 asset/file 走，而不是先把所有生成产物压成一个中心化模块列表。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:382-391`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGenerator.cpp:130-177` | 生成与 owner 关系更自然，后续要做分模块交付或按源追踪更容易。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 today 的单一 `BindModules.Cache` 升级成 per-provider `BindArtifactManifest` 发现机制，让外部插件可以交付自己的生成 bind shard，而不是把所有产物都并到核心插件根目录。 |
| 具体步骤 | 1. 新增 `FAngelscriptBindArtifactManifestEntry`，至少包含 `OwnerPluginName`、`OwnerModuleName`、`ManifestVersion`、`BindModuleNames`、`ArtifactRoot`、`Scope(Runtime/Editor)`；旧 `BindModules.Cache` 继续作为 legacy fallback。<br>2. 调整 `GenerateNativeBinds()`：不要再只维护一个全局字符串数组，而是按 owner 生成 manifest；module 名至少带 owner 前缀或 hash，避免不同 provider 产出的 `ASRuntimeBind_*` 名称碰撞。<br>3. 在 `FAngelscriptEngine::Initialize()` 阶段改为枚举 enabled plugins（或显式实现 `IAngelscriptBindArtifactProvider` 的模块），收集每个 provider 的 manifest 后再统一排序/加载；当没有新 manifest 时，仍回退到当前 `FindPlugin(\"Angelscript\") + BindModules.Cache` 路径。<br>4. 增加一个示例 external plugin，证明“只启用插件、不修改核心 `Angelscript` 源码”也能加载自己的 generated bind shard；随后把这条交付路径补进 build/test/documentation。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`，以及新增的 artifact manifest / provider interface / external sample plugin 文件 |
| 预估工作量 | M |
| 架构风险 | 多 provider 合并后会引入模块加载顺序、owner 冲突和 stale manifest 清理问题；第一阶段应先让 manifest 只承担“发现与归属”，不要同时重做 symbol 优先级模型。 |
| 兼容性 | 向后兼容。旧 `BindModules.Cache` 读取逻辑保留，现有 `ASRuntimeBind_*`/`ASEditorBind_*` 仍可继续工作；新 manifest discovery 先作为 opt-in 交付路径加入。 |
| 验证方式 | 1. 新建一个独立 plugin，生成一份只属于它自己的 bind manifest 和 shard，验证启用插件即可被 runtime 发现，禁用插件即不加载。<br>2. 同时保留 legacy `BindModules.Cache` 与新 manifest，确认加载顺序稳定、无重复模块加载。<br>3. 回归现有 build/test runner，确认未配置 external provider 时行为与今天一致。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P0 | Arch-BP-35 | `runtime/editor/UHT` 三条 lane 的 admission gate 分叉，新增类型会出现 partial coverage | 合同收敛 + 资格报告新增 | 高 |
| P1 | Arch-BP-36 | 自动生成 bind shard 仍被单一 `Angelscript` 插件根目录绑定，外部插件无法自带交付 | 交付拓扑重构 + provider 发现 | 高 |

---

## 架构分析 (2026-04-10 01:03)

### Arch-BP-37：公开 binding SDK 直接暴露 AngelScript VM internals，扩展接口缺少稳定边界

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 对外开放的 binding 扩展接口是否是稳定、瘦身过的 SDK 边界 |
| 当前设计 | `AngelscriptRuntime` 目前把 `Core/` 和 `ThirdParty/angelscript/source` 直接公开给依赖方，公开头里的 binder API 也直接暴露 `asSFuncPtr`、`asIScriptFunction`、`asITypeInfo`、`asIScriptGeneric`、`ASAutoCaller::FunctionCaller` 等 VM 细节；一旦扩展者要写稍复杂的 helper，还会自然滑向 `asCTypeInfo` / `asCObjectType` 这类内部结构。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs:15-23,67-73` — `ModuleDirectory`、`Core`、`ThirdParty/angelscript/source` 都在 `PublicIncludePaths`，editor 构建时甚至把 `UnrealEd` / `EditorSubsystem` 公开给依赖方；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:6-15` — 公开头自己就声明“需要 access to `asSMethodPtr`”，并直接 include `AngelscriptInclude.h` / `StaticJIT/StaticJITBinds.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:169-251,606-672` — `Method()` / `BindGlobalFunction()` / `BindMethodDirect()` / `GetPreviousBind()` 等公开 API 直接以 `asSFuncPtr`、`asIScriptGeneric*`、`asIScriptFunction*`、`asITypeInfo*`、`ASAutoCaller::FunctionCaller` 为签名；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:61-81,126-127,316,400-406` — 类型系统公开面同样暴露 `asITypeInfo*` 与 `asIScriptFunction*`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp:39-67` — 一个典型的多态 helper 已经要直接检查 `asCTypeInfo` / `asCObjectType` / `plainUserData`，说明“高级自定义绑定”天然会穿透到 VM 内部对象布局。 |
| 优点 | 插件内部 authoring 很直接，`Bind_*.cpp` 可以不经过额外 wrapper 就使用 AngelScript 全部能力，某些高性能或特化 binding 也更容易落地。 |
| 不足 | 这条“公开 API”实际上没有把 supported surface 与 internal surface 分开：外部项目模块一旦依赖 `AngelscriptRuntime`，编译上看到的是整块 runtime internals，而不是受控 SDK。结果是 1. 上游 VM / StaticJIT / internal caller 细节一改，外部 provider 就可能跟着重编或失配；2. editor-only public dependency 让 runtime 扩展边界进一步变脏；3. 用户虽然“能注册自定义绑定”，但代价是把自己的模块锁死在当前 fork 的内部实现上，而不是锁在一个长期可维护的扩展合同上。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 对外公开的是 `ExportClass/ExportEnum/ExportFunction/AddType` 这类瘦接口，扩展对象先写入 `FExported` registry，再由 `FLuaEnv` 启动时统一消费；公开头不要求扩展方直接操纵 Lua VM 内部结构。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/Binding.h:21-43`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Binding.cpp:19-57`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:144-159` | 先定义“可支持的扩展合同”，再把 VM 细节留在 registry/runtime 内部。 |
| puerts | 对外扩展点是 `UCodeGenerator` / `ICodeGenerator::Gen()` 这种 UE interface，扩展者只需要实现生成接口；最终声明拼装和扩展并入仍由 `DeclarationGenerator` 内部完成。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Public/CodeGenerator.h:10-26`；`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1710-1716` | 工具链扩展面可以只暴露 UE 级抽象，不把宿主语言 runtime internals 变成公共依赖。 |
| UnrealCSharp | 对外 registry 公开的是 `UStruct`、`FName`、hash、descriptor 的增删接口；运行时清理和回填虽然复杂，但复杂性主要留在 registry 实现里，而不是渗进每个扩展者的 include 面。 | `Reference/UnrealCSharp/Source/UnrealCSharp/Public/Registry/FClassRegistry.h:14-46`；`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FCSharpBind.cpp:119-223` | 公开 API 围绕宿主引擎对象和显式 descriptor，而不是围绕语言 VM 内部指针类型。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 today 的 “include 整个 runtime internals 即可扩展” 收敛成一个瘦身的 `Binding SDK`，用受控 builder/provider 接口隔离 AngelScript VM 内部细节。 |
| 具体步骤 | 1. 在 `AngelscriptRuntime` 新增一层公开 `Binding SDK` 头，例如 `Public/BindingSDK/`，第一阶段只暴露 `IAngelscriptBindingProvider`、`FAngelscriptBindingBuilder`、`FAngelscriptTypeAdapterBuilder`、`FAngelscriptBindingDescriptor` 这类不含 `as*` 类型的 API。<br>2. 把现有 `AngelscriptBinds.h` / `AngelscriptType.h` 中对外最常用的注册动作包进 builder/provider façade；`asSFuncPtr`、`ASAutoCaller::FunctionCaller`、`asIScriptFunction`、`asITypeInfo` 等低层签名改为 runtime 内部桥接。<br>3. 将 `ThirdParty/angelscript/source` 与 `Core/` 从默认 public include 面中收缩出来，只给 legacy/internal 扩展路径保留兼容入口；同时把 `Target.bBuildEditor` 下的 editor-only public dependency 下沉到 editor-specific 扩展层，避免新 SDK 自动继承 `UnrealEd`。<br>4. 新增一个 sample external provider，仅使用新 SDK 完成一个 `type + method/global helper` 扩展，验证项目模块不再需要 include AngelScript internal source 头或依赖 `asCTypeInfo`。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`，以及新增的 `Plugins/Angelscript/Source/AngelscriptRuntime/Public/BindingSDK/*` façade / adapter 文件 |
| 预估工作量 | M |
| 架构风险 | 如果一口气隐藏掉旧头文件，会直接打断现有内部 binder 和外部实验性扩展；第一阶段必须保持旧 API 可用，只是把新 SDK 作为首选入口，并把旧头标记为 legacy/internal。 |
| 兼容性 | 向后兼容。现有 `Bind_*.cpp`、旧 `FBind`/`FAngelscriptType` 扩展方式和已存在的外部 include 路径都可继续存在；新 SDK 是额外增加的稳定边界，不要求一次性迁移。 |
| 验证方式 | 1. 新建一个独立模块，只 include 新 `Binding SDK` 头完成自定义 bind/type 注册，确认不需要 `ThirdParty/angelscript/source` public include。<br>2. 在 editor target 下验证该模块不必显式消费 `UnrealEd` 也能编译通过。<br>3. 复跑现有 `BindConfig` / `MultiEngine` / runtime 自动化，确认旧 binder 与新 façade 并存时行为不变。 |

### Arch-BP-38：注册只有 append/reset，没有 provider 级 unregister，外部扩展无法安全撤销

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 外部 provider 注册的 type/bind 能否在模块卸载、热重载、替换实现时被精确撤销 |
| 当前设计 | 当前公开面只有 `RegisterBinds()`、`Register()`、`RegisterTypeFinder()` 这类 append API；`bind` 侧只有 `ResetBindState()` 能清空当前 engine 的执行态，静态 `BindArray` 本身不会被移除；`type` 侧只有 `ResetTypeDatabase()` 这种全库重置；editor 自动生成的 bind module 甚至明确把 `ShutdownModule()` 留空。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:132-154` — `GetBindArray()` 是 process-global 静态数组，`RegisterBinds()` 只做 `Add(...)`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:186-189` — `ResetBindState()` 只重置 `FAngelscriptBindState`，并不移除已注册的 bind contribution；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:71-81` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:54-118` — 类型系统公开面同样只有 `Register()`、`RegisterAlias()`、`RegisterTypeFinder()` 和全量 `ResetTypeDatabase()`，没有 `UnregisterType` / `UnregisterTypeFinder`；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1461-1462` — 生成模块的 `ShutdownModule()` 明确是空实现；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:438-469` — `FBind` 构造时立即注册，但没有任何对称的释放句柄或 token。 |
| 优点 | 当前模型实现简单，插件内部默认假设“大多数 bind/type 都是进程期常驻”，不需要处理复杂的撤销顺序和引用计数。 |
| 不足 | 这让外部扩展进入了一种“只进不出”的状态：1. provider 一旦注册 type/finder/bind，后续模块禁用、热重载、替换实现时只能赌全局 reset；2. `ResetTypeDatabase()` / `ResetBindState()` 粒度过粗，会把不相关 provider 一起波及；3. 生成 bind module 空 `ShutdownModule()` 进一步固化了“没有 teardown contract”这一事实。对于要把插件做成可复用交付物的场景，这会直接卡住“项目模块试验一组自定义绑定，再安全回滚”的闭环。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | registry 从一开始就把清理建成正式能力：`Deinitialize()` 会撤销 class constructor hook 并释放 descriptor map，`RemoveClassDescriptor/RemoveFunctionDescriptor/RemovePropertyDescriptor` 支持按粒度移除。 | `Reference/UnrealCSharp/Source/UnrealCSharp/Public/Registry/FClassRegistry.h:14-46`；`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FClassRegistry.cpp:21-75,115-190` | 先把 teardown 设计成 registry contract，扩展者和动态生成链才能安全替换已有绑定。 |
| UnLua | `ClassRegistry` 支持 `Unregister(const UStruct*)` 与 `Unregister(const FClassDesc*, bool)`；对象删除或 metatable 失效时可以显式撤销 registry/projected Lua state。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:265-322` | 即便 runtime 最终仍是全局 VM，也给 type/class 注册面留了可回收路径，而不是只能全局清空。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 给 bind/type/property-adapter/provider 引入可撤销的 registration handle，把 today 的 append-only 注册升级为 “register with lifetime”。 |
| 具体步骤 | 1. 在 `AngelscriptRuntime` 新增 `FAngelscriptRegistrationHandle` / `FAngelscriptProviderScope`，由 `RegisterBinds()`、`Register()`、`RegisterTypeFinder()` 的新重载返回；handle 至少记录 `ProviderId`、`ContributionKind`、`RegistrationIds`。<br>2. 让 `FBind`/旧 `Register*` API 保持现状并默认落到 immortal scope，保证内建 binder 不受影响；新 provider API 则显式创建 scoped registration。<br>3. 在 `FAngelscriptBinds` 和 `FAngelscriptTypeDatabase` 内增加按 `ProviderId` / handle 解绑的路径，`ShutdownModule()` 或 provider reload 时可只撤销自己的 bind/type/finder，而不是全局 reset。<br>4. 把 editor 生成的 `ASRuntimeBind_*` / `ASEditorBind_*` module 改为保存自己的 scope，并在 `ShutdownModule()` 中释放；同时新增自动化覆盖“注册 -> 可见 -> 卸载 -> 不可见 -> 重新注册”的完整循环。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`，`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`，以及新增的 registration handle / provider scope 文件 |
| 预估工作量 | M |
| 架构风险 | 撤销语义如果设计不清，会与现有全局静态 binder 混出悬空引用或顺序问题；第一阶段应坚持“新 provider opt-in，可撤销；旧静态 binder 仍是 immortal scope”，不要强行把所有历史 bind 一次性改成可卸载。 |
| 兼容性 | 向后兼容。旧 `FBind`、旧 `Register()`、旧 `RegisterTypeFinder()`、旧生成模块都可继续按常驻模式工作；只有选择新 scoped API 的 provider 才会启用 teardown 语义。 |
| 验证方式 | 1. 新建一个测试 provider/module，注册一个自定义 type、一个 type finder、一个 global helper，然后在测试中显式 unload/release，确认对应贡献从 bind dump 和 type resolution 中消失。<br>2. 回归 `MultiEngine` / `BindConfig` / startup bind 观测测试，确认内建 immortal bind 在引入 scoped registration 后不受影响。<br>3. 对 editor 生成 module 做一轮“生成 -> load -> shutdown -> reload”验证，确认不再残留重复 bind 或 stale type finder。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-BP-37 | 公开 binding 扩展接口直接暴露 VM internals，缺少稳定 SDK 边界 | 扩展面瘦身 + API 分层 | 高 |
| P1 | Arch-BP-38 | 注册只有 append/reset，没有 provider 级 unregister，外部扩展无法安全撤销 | 生命周期补齐 + teardown 合同新增 | 高 |

---

## 架构分析 (2026-04-10 01:15)

### Arch-BP-39：`StaticJIT` 对模板类型 native form 的定位仍依赖方法序号，模板绑定扩展缺少稳定 identity

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | template bind 在注册阶段与 `StaticJIT` 消费阶段是否共享稳定的函数身份 |
| 当前设计 | `StaticJIT` 注册侧只把“刚刚绑定完成的 script function 指针”写入 `GScriptNativeForms`；当调用目标是模板实例化类型时，查找侧再通过 `templateBaseType` 的 constructor/method 序号回推 native form，并在代码注释中显式承认“methods are always in the same order”这一假设。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.cpp:29-59` — `GetNativeForm()` 对 template type 先比较 constructor/destructor，再用 `ObjectType->methods.IndexOf(...)` 回查 `templateBaseType->methods[MethodIndex]`，并注明依赖“相同顺序”；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.cpp:368-372,401-405,703-707` — `BindNativeMethod()`、`BindNativeFunction()`、`BindTemplateInstantiatedCall()` 只用 `FAngelscriptBinds::GetPreviousBind()` 作为注册键写入 `GScriptNativeForms`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp:1388-1440`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp:1080-1125`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSet.cpp:566-612` — `TArray/TMap/TSet` 的模板方法都是按手写顺序连续注册，并在每个方法后通过 `SCRIPT_NATIVE_TEMPLATED_CALL*` 宏补登记 native form。 |
| 优点 | 当前内建模板 family 都由同一份手写 `Bind_*.cpp` 控制，顺序稳定时无需额外 descriptor，即可把 `StaticJIT` native form 直接挂到 script function 上。 |
| 不足 | 只要未来给模板 family 插入新方法、重排现有注册顺序，或允许外部 provider 为 `TArray/TMap/TSet` 这类模板类型追加扩展方法，`StaticJIT` 就可能把实例化类型的方法错误映射到错误的 native form。此时问题不是“某个 bind 没注册”，而是“下游 codegen 仍把 ordinal 当 identity”，扩展者无法通过更高层配置修复。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | 函数注册先转成稳定 hash，再写入 `AddFunctionHash` / descriptor registry；后续 override 与 runtime 回查依赖 `GetTypeHash(UFunction*)`，不是依赖容器中的位置。 | `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FCSharpBind.cpp:315-382`；`Reference/UnrealCSharp/Source/UnrealCSharp/Public/Registry/FClassRegistry.inl:56-66` | 下游消费者读取的是稳定 symbol identity，而不是“第 N 个 method”。 |
| puerts | 声明生成使用 `FunctionKey(FunctionName, IsStatic)` 聚合 overload，extension method 与原生 method 最终写入同一份 `FunctionOutputs`。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Public/TypeScriptDeclarationGenerator.h:39-55`；`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1097-1185` | 先稳定函数键，再汇总不同来源输出，避免扩展方法对已有输出造成序号耦合。 |
| UnLua | `FFunctionDesc` 构造时一次性从 `UFunction` 提取名称、参数和调用约束，后续运行时消费 descriptor，而不是依赖函数在数组中的相对位置。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:31-76` | 把“函数是什么”固化为 descriptor，可被多个下游阶段共享。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 给 template native form 增加稳定 `BindingSymbolKey`，让 `StaticJIT` 对模板实例化方法优先按 key 命中、按序号回退。 |
| 具体步骤 | 1. 在 `StaticJIT/StaticJITBinds.h/.cpp` 新增 `FTemplateNativeFormKey` 或复用未来统一 `BindingSymbolKey`，至少包含 `TemplateBaseTypeName`、`MethodKind(Constructor/Destructor/Method)`、`DeclarationOrScriptName`、`Arity/SignatureFingerprint`。<br>2. 保留现有 `GScriptNativeForms<asIScriptFunction*, ...>`，同时新增 `GTemplateNativeForms<FTemplateNativeFormKey, ...>`；`BindNativeMethod()`、`BindTemplateInstantiatedCall()`、相关 `SCRIPT_NATIVE_TEMPLATED_CALL*` 宏在注册时 dual-write。<br>3. 改写 `GetNativeForm()` 的 template 分支：先从实例化函数构造 key 并命中新 map；只有老 DB 或老 bind 没写 key 时，才退回 `MethodIndex` 方案；一旦 key 命中结果与 ordinal 命中不一致，记录 report-only 诊断。<br>4. 第二阶段再开放一个可选的 template-extension registration 接口，让项目侧模块可以为模板 family 追加方法，而不要求“必须保持与核心插件同一注册顺序”。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`，以及模板 family 的测试文件 |
| 预估工作量 | M |
| 架构风险 | 稳定 key 的 canonicalization 如果不包含 overload 维度，可能把同名不同签名的方法折叠到同一个 native form；第一阶段必须保留 ordinal fallback，并把 mismatch 只作为诊断输出。 |
| 兼容性 | 向后兼容。现有内建模板绑定与旧 precompiled data 仍可继续使用 ordinal 路径；新 key 先作为增强的 identity 层加入，不要求一次性迁移所有 `Bind_*.cpp`。 |
| 验证方式 | 1. 新增一个测试 template family 或测试 provider，在现有 `TArray` 风格绑定中间插入一个额外方法，确认 key 路径仍能为旧方法命中正确 native form。<br>2. 对 `TArray/TMap/TSet` 全量生成 precompiled data，统计 `ordinal-hit-but-key-mismatch` 次数应为 `0`。<br>3. 回归现有 `StaticJIT` 相关自动化，确认未写 key 的历史路径仍可成功回退。 |

### Arch-BP-40：`PrecompiledData` 与 class-generator 下游仍按 raw `UFunction` 名回查，最终脚本面 descriptor 没有成为共享合同

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | bind 注册、bind DB、`StaticJIT`、class generator 是否复用同一份“最终脚本可见函数描述” |
| 当前设计 | 上游 `FAngelscriptFunctionSignature` 已负责计算 `ScriptName`、前缀剥离、namespace 和 override name，但下游 `PrecompiledData` 仍按 `Function->GetName()` 去 `GetMethodByScriptName()`；class-generator 在 override 校验时还要再次调用 `GetScriptNameForFunction()` 重新推导脚本名；DB 写入层则仍以 `UnrealPath=Function->GetName()` 为主，`ScriptName` 只在 `BlueprintEvent` 路径被持久化。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h:85-120` — `GetScriptNameForFunction()` 会处理 `ScriptName` metadata、`K2_/BP_/AS_` 前缀剥离、`Receive*` event 名规整；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1303-1315,1422-1460` — generated getter/setter alias 通过 `BindFunctionWithAdditionalName()` 传入新的 `TargetName`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp:39-55,141-150` — callable 入口先按 raw `Function->GetFName().ToString()` 查 `FFuncEntry`，`SCRIPT_NATIVE_UFUNCTION` 也默认沿用 raw function name；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h:336-394` — `InitFromDB()` 读不到 `ScriptName` 时直接回退 `InFunction->GetName()`，`WriteToDB()` 始终写 `DBBind.UnrealPath = Function->GetName()`，且只在 `FUNC_BlueprintEvent` 时写 `DBBind.ScriptName`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp:745-767,1449-1450` — `Context.ClassDesc->GetMethodByScriptName(ANSI_TO_TCHAR(Function->GetName()))` 仍以 raw name 回查脚本方法；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:795-811` — override 错误提示需要再调用 `GetScriptNameForFunction()`，说明下游没有直接拿到 authoritative descriptor。 |
| 优点 | raw `UFunction` 名是引擎侧稳定键，bind DB 体积更小，也方便在 cooked 环境中只靠 `UFunction` 基础元信息做最小恢复。 |
| 不足 | 一旦函数使用 `meta=(ScriptName=...)`、`K2_`/`BP_` stripped name、`ScriptMixin` member 化、generated getter/setter alias 或 `ScriptCallable` override name，下游阶段就必须“重新猜一次最终脚本名”或直接误用 raw name。对于“新增一个 UE 类型能否自动化”这个问题，这意味着自动生成链并没有复用一份 authoritative surface descriptor；对于“用户能否在不改插件源码的情况下注册自定义绑定”这个问题，这意味着外部扩展者也拿不到统一的最终函数表面合同。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `DeclarationGenerator` 先用 `FunctionKey(FunctionName, IsStatic)` 聚合函数，再把原生扩展、template extension、蓝图函数统一并入同一份 `FunctionOutputs`。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Public/TypeScriptDeclarationGenerator.h:39-55`；`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1097-1185` | 最终暴露表面先成为正式数据结构，然后才被不同阶段消费。 |
| UnrealCSharp | 绑定链先把 `UFunction` 变成稳定 hash/descriptor，后续通过 `AddFunctionHash` 和 function descriptor registry 复用，不需要每个阶段再按名字猜一次。 | `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FCSharpBind.cpp:327-381`；`Reference/UnrealCSharp/Source/UnrealCSharp/Public/Registry/FClassRegistry.inl:56-66` | descriptor 先行，native/runtime/tooling 都围绕同一份结构组织。 |
| UnLua | `FFunctionDesc` 在构造时一次性提取 `FuncName`、参数和调用约束，后续 Lua 调用桥接直接消费 descriptor。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:31-76` | 把函数的宿主语言可见信息前置固化，减少运行时重复推导。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把“最终脚本面函数描述”提升为共享 descriptor，让 bind、DB、`StaticJIT`、class-generator 都围绕同一份 resolved surface 工作。 |
| 具体步骤 | 1. 在 `Binds/Helper_FunctionSignature.h` 或新增共享头中定义 `FAngelscriptResolvedFunctionSurface`，至少包含 `UnrealPath`、`ResolvedScriptName`、`ResolvedNamespaceOrClass`、`Declaration`、`SurfaceKind(Method/Global/Getter/Setter/Event)`、`bStaticInUnreal`、`bStaticInScript`、`AliasSource`。<br>2. 扩展 `FAngelscriptMethodBind` 与 `WriteToDB()`：不仅 event，所有 callable/event/generated accessor 都持久化 resolved surface；旧字段继续保留，新字段按追加序列化，保证旧 DB 仍可读取。<br>3. `Bind_BlueprintCallable()`、`Bind_BlueprintEvent()`、`BindFunctionWithAdditionalName()` 在注册完成后，把 resolved surface 同步写入统一 registry；`PrecompiledData.cpp` 与 `AngelscriptClassGenerator.cpp` 优先查询该 registry/DB descriptor，不再直接用 raw `Function->GetName()` 猜脚本面名字。<br>4. 第一阶段保留 raw-name fallback，并在 fallback 命中时输出诊断计数，优先覆盖 `ScriptName metadata`、`K2_` stripped callable、generated getter/setter alias、`ScriptCallable` 四类样本。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 预估工作量 | M |
| 架构风险 | bind DB 序列化扩展和 cooked 兼容读取需要小心处理版本化；如果 descriptor 与 live bind 注册不同步，反而会产生第二份不可信数据源，因此第一阶段必须坚持 dual-write 和 raw fallback。 |
| 兼容性 | 向后兼容。旧 DB 缺少 resolved surface 时仍可走当前 raw-name 路径；现有脚本 API 不需要改名，新 descriptor 只是把今天分散在多个阶段的推导结果收敛成正式合同。 |
| 验证方式 | 1. 为四类样本建立自动化：`meta=(ScriptName=...)` callable、`K2_` 前缀函数、generated getter/setter、`ScriptCallable` function，确认 bind DB 与 runtime registry 都能读出同一 resolved surface。<br>2. 复跑 `PrecompiledData` 生成与 class override 检查，确认不再依赖 `Function->GetName()` 才能定位脚本方法。<br>3. 增加一项诊断统计，验证新路径启用后 raw-name fallback 计数逐步降到 `0`。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-BP-39 | `StaticJIT` 对模板实例化方法仍按序号回查 native form，模板扩展缺少稳定 identity | identity 合同补齐 + key-first 回查 | 高 |
| P1 | Arch-BP-40 | 下游 `PrecompiledData` / class-generator 未复用最终脚本面 descriptor，仍按 raw `UFunction` 名回查 | 共享 descriptor 新增 + 下游消费收敛 | 高 |

---

## 架构分析 (2026-04-10 01:25)

### Arch-BP-41：`namespace` 仍靠 engine 默认作用域隐式推进，手写与外部扩展缺少可审计的 scope 合同

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | namespace / scope 是否是结构化合同，还是仅靠注册时的隐式全局状态 |
| 当前设计 | 当前 `BindGlobalFunction()` / `BindGlobalVariable()` 公开面不接收显式 namespace；作者若要把 helper 挂到 `UClass::`、`ActorType::` 或 `TypeName::StaticClass()` 这类作用域，只能先构造 `FAngelscriptBinds::FNamespace`，由其临时修改 `asIScriptEngine` 的 default namespace，再在该隐式状态下继续注册。与此同时，`FAngelscriptMethodBind` 本身没有 `Namespace` 或 `OwningScope` 字段，因此这条 scope 信息只存在于 live 注册时刻，不存在稳定 artifact。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:487-492,606-610` — `FNamespace` 是公开 RAII 对象，但 `BindGlobalFunction/BindGlobalVariable` 签名没有 namespace 参数；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:685-695` — `FNamespace` 构造/析构直接调用 `Engine->SetDefaultNamespace(...)` 改写全局默认作用域；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp:329-356` — `Bind_UClass_Base` 通过 `FNamespace ns("UClass")` 临时挂出 `FindClass/GetAllClasses/GetAllSubclassesOf`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp:286-312` — `Bind_Actors` 遍历所有已绑定 actor 类型，再用 `FNamespace ns(ClassName)` 为每个类型临时生成 `Spawn()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:684-694` — `StaticClass()` 也是在 `FNamespace ns(TypeName)` 的隐式上下文里注入；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h:56-87` — `FAngelscriptMethodBind` 序列化字段没有显式 scope/namespace。 |
| 优点 | 对当前插件内部作者来说很直接，写一小段 RAII 块就能得到目标 namespace 下的 helper，不需要额外 descriptor 或 builder。 |
| 不足 | 这会把作用域变成“注册顺序副作用”而不是正式合同：1. 外部 provider 若想给某个类型追加 namespace helper，只能跟核心代码一样操作 shared engine state；2. manifest / dump / bind DB 很难回答“这个 helper 到底属于哪个 scope”；3. 一旦未来要把 bind 生成变成批处理、并行或 report-first pipeline，ambient namespace 会成为天然的串行耦合点。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | declaration generator 把 namespace 当作显式数据：`NamespaceMap` 缓存每个对象的 namespace，`GetNamespace()/GetNameWithNamespace()` 负责计算完全限定名，`NamespaceBegin/NamespaceEnd` 再把该数据写入输出。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Public/TypeScriptDeclarationGenerator.h:57-84`；`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:469-519,537-557` | 先把 scope 变成数据，再由输出层消费，而不是在注册 API 调用期间改写全局状态。 |
| UnrealCSharp | binding class generator 直接从 `InClass->GetTypeInfo().GetNameSpace()` 读取显式 namespace 集合，并把 namespace 同时用于生成内容与输出文件路径。 | `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FBindingClassGenerator.cpp:28-35,730-740,743-752,937-948`；`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:370-435` | scope 既是生成输入，也是 artifact ownership 的一部分，后续不会丢失。 |
| UnLua | 对外公开的是按名字或对象导出的 registry：`ExportClass/ExportFunction/AddType` 把 entry 写进 `FExported`，`FLuaEnv` 初始化时按 registry 内容逐项注册。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/Binding.h:21-27`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Binding.cpp:19-57`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:144-159` | 即便不是 namespace-first 设计，scope/owner 也先表现为显式 registry entry，而不是“调用时临时切一个全局 namespace”。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 给 bind 注册补一层显式 `BindingScope`，让 namespace/type scope 成为 descriptor 字段，`FNamespace` 退化为兼容包装。 |
| 具体步骤 | 1. 在 `AngelscriptRuntime` 新增 `FAngelscriptBindingScope`，至少包含 `ScopeKind(Global/Namespace/TypeNamespace)`、`Namespace`、`OwningTypeName`、`SourceProvider`。<br>2. 为 `BindGlobalFunction()`、`BindGlobalVariable()`、未来 provider/builder API 增加可选 scope 参数；内部 apply 阶段才把 scope 翻译成 `SetDefaultNamespace()`，而不是让调用者直接操作 engine 全局状态。<br>3. 扩展 `FAngelscriptMethodBind` 或未来 artifact entry，使 manual helper 也能持久化 `Namespace/OwningScope`；第一阶段先让 `Bind_UClass_Base`、`Bind_Actors`、`BindStaticClass()` dual-write scope 信息，不改变最终脚本名。<br>4. 在 dump / manifest / bind info 里输出 scope，并新增冲突检查：同一 `Scope + Name + Signature` 的 helper 若来自不同 provider，要能直接审计。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`，以及相关 dump/manifest 输出文件 |
| 预估工作量 | M |
| 架构风险 | scope descriptor 与最终 declaration 若不同步，可能出现“artifact 说在某 namespace，下游实际注册到了另一 namespace”的双源问题；第一阶段必须坚持 dual-write 和诊断优先，不立刻移除 `FNamespace`。 |
| 兼容性 | 向后兼容。现有 `FNamespace`、旧 `BindGlobalFunction()` 调用点和最终脚本 API 都可保留；新增的是显式 scope 描述与新重载，外部 provider 可逐步迁移。 |
| 验证方式 | 1. 以 `UClass::FindClass`、`ActorType::Spawn`、`TypeName::StaticClass` 为样本，确认新 scope descriptor 与现有脚本访问路径完全一致。<br>2. 新建一个项目侧 provider，仅靠显式 scope API 为某个类型追加 namespace helper，验证无需直接操作 `FNamespace` 也能成功注册。<br>3. 检查 dump/manifest，确认 manual helper 已能显示 `Namespace/OwningTypeName`，并能在冲突时给出明确 scope 级诊断。 |

### Arch-BP-42：手写 bind 的作者单元按 behavior family 组织，而不是按 target type 组织，新增一个 UE 类型的 richer surface 没有单一落点

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | “给某个 UE 类型补完整脚本 surface”是否存在 target-centric 的扩展槽位 |
| 当前设计 | 当前手写 bind 的基本组织单元不是“一个目标类型一个 owner”，而是“一个行为 family 一个文件/一个 `FBind`”。结果是同一个类型最终暴露到脚本侧的 surface 往往跨多个 family：`Bind_UClass_Base` 在 `Bind_UObject.cpp` 里给 `UClass` 增补 namespace helper，`Bind_UObject_Operations` 同文件又追加全局对象工厂；`Bind_AActor.cpp` 一边给 `AActor` 本体补 method，一边遍历所有 actor 类型生成 `ClassName::Spawn`，同时又额外挂全局 `SpawnActor`；`Bind_BlueprintType.cpp` 再为所有 admitted class 注入 `StaticClass()` 和 BlueprintGetter/Setter alias。新增一个类型若只靠反射可自动进入基本面，但一旦想拥有这些 richer helper，就必须找到多个 cross-cutting hotspot，而不是往某个“该类型的扩展槽”里填一份 fragment。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp:273-356` — `Bind_UClass_Base` 在 `Bind_UObject.cpp` 内同时为 `UClass` 本体和 `UClass` namespace 生成 helper；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp:510-579` — 同一文件的 `Bind_UObject_Operations` 继续挂全局 `FindClass/GetAllClasses/NewObject`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp:25-37,286-312,450-467` — `Bind_AActor_Base`、遍历全部 actor 类型的 `ClassName::Spawn`、以及全局 `SpawnActor/SpawnPersistentActor` 都在同一 family 文件里；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:684-694,1413-1469` — `StaticClass()` 和 generated getter/setter alias 又是在另一条 reflective family 中统一注入；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:195-214` — runtime 最终只按排序执行 bind family，本身不知道“这些 contribution 最终属于哪个 target type 的 surface”。 |
| 优点 | cross-cutting helper 可以集中维护，例如 `SpawnActor`、`StaticClass`、getter/setter alias 这种模式不必在每个类型文件里复制。 |
| 不足 | 但扩展性成本直接转嫁给作者：1. “新增一个类型并补齐 richer surface”通常不是 1 个步骤，而是要先判断它落在哪些 family；2. 外部插件想只给一个类型追加 helper，缺少 target-centric 正式入口，只能写新的 late bind 并赌排序/冲突；3. 未来若要自动生成 richer surface，也很难只为一个类型产出完整结果，因为该类型的 surface ownership 本身是分散的。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | binding 生成按 class 粒度落盘：每个 `FBindingClass` 都从自己的 `TypeInfo().GetNameSpace()`、class 名、implementation 名生成独立 partial/implementation 文件，并登记到 generator file list。 | `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FBindingClassGenerator.cpp:28-35,730-740,743-752,937-948` | target type 的生成 ownership 很清晰，“给哪个类型补东西”就有稳定的产物落点。 |
| puerts | 扩展方法以 `UStruct*` 为 key 进入 `ExtensionMethodsMap`，`GatherExtensions()` 再把这些扩展并到该 `Struct` 自己的 `FunctionOutputs`。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Public/TypeScriptDeclarationGenerator.h:38-55`；`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1110-1171` | 即使 extension 是跨模块提供的，最终 ownership 仍然回到具体 target struct/class，而不是散落在若干 family 文件里。 |
| UnLua | 外部贡献按 entry 粒度进入 export registry，`ExportClass/ExportFunction/AddType` 先写 registry，再由 `FLuaEnv` 启动时逐项注册。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/Binding.h:21-27`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Binding.cpp:34-57`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:146-159` | 先把“我要扩展哪个 class/function/type”做成显式 entry，再决定运行时怎样消费。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在现有 family bind 之上补一层 target-centric `SurfaceFragment` 组合阶段，让 richer surface 先围绕目标类型聚合，再统一应用。 |
| 具体步骤 | 1. 新增 `FAngelscriptTargetSurfaceFragment` 与 `IAngelscriptTargetSurfaceProvider`，fragment 至少包含 `TargetSelector(UClass/ObjectPath/BaseClassPredicate)`、`Methods`、`StaticHelpers`、`Aliases`、`Traits`、`SourceProvider`。<br>2. admission / class-scan 阶段先得到 admitted type 列表，再由组合器为每个 target type 收集 fragment；第一批把 `StaticClass()`、`ActorType::Spawn`、BlueprintGetter/Setter alias 迁成 fragment producer，cross-cutting 全局 helper 则单独落到 `GlobalUtilityFragment`。<br>3. 让现有 `Bind_UClass_Base`、`Bind_AActor_Base`、`Bind_BlueprintType` 暂时保留，但内部逐步改为“声明 fragment -> 由组合器 apply”；外部插件则可只实现 `IAngelscriptTargetSurfaceProvider` 为某个类型或某个基类族追加 helper。<br>4. 在 dump / manifest 中按 target type 输出“最终 surface 由哪些 fragment 组合而成”，使“新增一个 UE 类型需要几步”能够被明确回答为：admission、可选 fragment、验证，而不是去猜 family 热点。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`，以及新增的 `TargetSurfaceFragment` / provider / manifest 文件 |
| 预估工作量 | M |
| 架构风险 | fragment 组合若没有明确优先级，可能把今天“靠 bind 顺序自然生效”的 helper 变成新的冲突源；第一阶段应只覆盖少量高价值 family，并保留旧 family apply 路径做对照。 |
| 兼容性 | 向后兼容。旧 `FBind` 与现有 `Bind_*.cpp` 可以继续工作；新 target-centric fragment 先作为 opt-in 组合层加入，不要求一次性拆散所有 hand-written binder。 |
| 验证方式 | 1. 选 `AActor` 家族做试点，验证 `StaticClass()`、`ActorType::Spawn`、getter/setter alias 能通过 fragment 组合得到与现状一致的脚本 surface。<br>2. 新建一个外部 provider，只为某个具体 `UClass` 追加一个 namespace helper，确认无需修改核心 `Bind_AActor.cpp` / `Bind_UObject.cpp` 也能接入。<br>3. 对比 dump/manifest，确认目标类型的最终 surface 已能显示 fragment 来源与组合顺序。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-BP-42 | 手写 bind 按 family 而不是按 target type 组织，新增 richer surface 缺少单一落点 | target-centric 组合层新增 | 高 |
| P2 | Arch-BP-41 | `namespace` 依赖 engine 默认作用域隐式推进，scope 不可审计 | scope 合同显式化 | 中 |
