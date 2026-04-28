# TypeSystem 架构与扩展性分析

---

## 架构分析 (2026-04-08 14:13)

### Arch-TS-01：`FAngelscriptType` 把过多运行时合同压进单一虚表，新增容器类型的改动面偏大

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 新类型映射的扩展面 |
| 当前设计 | `TypeSystem` 通过 `FAngelscriptType` 统一承载声明、`FProperty` 创建、GC、debugger、StaticJIT 和复制/比较语义；`ClassGenerator` 再递归调用 `FAngelscriptTypeUsage::CreateProperty()` 生成真实 UE 反射属性。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:96` — `FAngelscriptType` 同时暴露 `MatchesProperty`、`CreateProperty`、`EmitReferenceInfo`、`GetCppForm`、`GetDebuggerValue`、`CopyValue` 等能力入口。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:147` — `GetByProperty()` 先跑 `TypeFinders`，再扫 `TypesImplementingProperties`，类型发现完全依赖运行时注册顺序。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp:88`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp:108`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSet.cpp:91` — 每个容器 binder 都要各自实现 `CanCreateProperty` / `CreateProperty` / `MatchesProperty` / `EmitReferenceInfo`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp:1550`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp:1158`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSet.cpp:618` — 每个容器还要各自注册 `TypeFinder`，把 `FArrayProperty` / `FMapProperty` / `FSetProperty` 回推成 `FAngelscriptTypeUsage`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp:1733`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp:1300`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSet.cpp:730` — 模板子类型合法性、hash 能力、nested-container 限制也分散在各自 binder。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2923` — 类生成阶段直接调用 `PropertyType.CreateProperty(Params)`，因此新增类型一旦缺少 property contract，会在 materialization 阶段才暴露。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp:1937` — StaticJIT 依赖同一套 `FromTypeId()` + `GetCppForm()`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:2467` — debugger 也依赖同一套 `FromTypeId()` + `GetDebuggerValue()`。 |
| 优点 | 所有语言桥接合同都能落在一个类型对象上，容器对子类型的递归也足够直接，`ClassGenerator` 基本不需要知道具体类型细节。 |
| 不足 | 新增一个容器型映射通常至少要同时补齐“类型发现 + property 生成 + 运行时模板校验 + GC/debugger/JIT 能力”，改动横跨 `Core`、`Binds`、`ClassGenerator`、`Debugging`、`StaticJIT`；这让扩展成本更接近“补一个小子系统”，而不是“加一个 type adapter”。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 把 UE 类型桥接收敛为 `FPropertyDesc::Create()` 工厂，容器描述符再递归持有 inner/key/value 的 `ITypeInterface`；对象和容器缓存分别由 registry 管。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1537`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:738`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:890`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1028`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/PropertyRegistry.cpp:316`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ContainerRegistry.cpp:58` | 新增 property/container 类型时，主要是补一个 descriptor 和一个 factory case，容器缓存与对象缓存不用重复发明。 |
| puerts | 用 `FPropertyTranslator::Create()` 做集中工厂，再让 `FScriptArrayPropertyTranslator` / `FScriptMapPropertyTranslator` / `FScriptSetPropertyTranslator` 复用统一 container wrapper；wrapper 通过持有 inner/key/value translator 工作。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1225`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:862`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:912`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:961`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/ContainerWrapper.cpp:221`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/ContainerWrapper.cpp:517`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:21` | “类型识别”与“容器运行时行为”分层，新增类型先扩 translator 工厂；只有新增全新容器家族时才需要再动 wrapper。 |
| UnrealCSharp | 通过 `FPropertyDescriptor::Factory()` 做 property 级别分派，容器 helper 再递归持有 inner/key/value descriptor；`FTypeBridge` 负责从托管类型回落到 UE property class。 | `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/FPropertyDescriptor.cpp:47`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Container/FArrayHelper.cpp:11`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Container/FMapHelper.cpp:23`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:266` | property marshalling 与 dynamic type creation 明确分层，新增映射可以先补 descriptor，再按需补 bridge，而不是让所有消费者都直接依赖同一大虚表。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在不推翻 `FAngelscriptType` 的前提下，先把“property/container adapter”从“大而全类型对象”中拆出来，减少新增类型的改动半径。 |
| 具体步骤 | 1. 在 `Core/AngelscriptType.h` 增加一个轻量 `FAngelscriptTypeCapabilities` / `IAngelscriptPropertyAdapter` 层，让 `CreateProperty`、`MatchesProperty`、`EmitReferenceInfo`、模板校验可以独立于 debugger/JIT 能力存在。 2. 新增 `Binds/ContainerTypeAdapterBase.*`，把 `TArray/TSet/TMap/TOptional` 共通的 subtype 递归、`RegisterTypeFinder`、`CanBeTemplateSubType` 校验、GC schema 递归抽成共享基类。 3. 保留现有 `FAngelscriptType` API 作为兼容壳，先只迁移 `TArray/TSet/TMap/TOptional` 四个容器 binder；`ClassGenerator/AngelscriptClassGenerator.cpp` 继续只认旧入口，内部转发到 adapter。 4. 在 `Tests/` 增加 “type capability manifest” 回归组，验证每个可 `CreateProperty` 的类型都同步声明了 GC/debugger/JIT 覆盖情况，避免新增容器时漏实现。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSet.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TOptional.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` |
| 预估工作量 | M |
| 架构风险 | 如果一次性迁移所有 binder，很容易把现有 debugger 或 StaticJIT 回归一起带出来；建议先只动容器族并保留旧虚表 fallback。 |
| 兼容性 | 对现有 Angelscript 脚本语法和已暴露 API 可保持向后兼容；变更集中在 C++ 注册层和测试层。 |
| 验证方式 | 1. 编译 `AngelscriptRuntime`。 2. 为 `TArray/TSet/TMap/TOptional` 增加 “UE property -> AS type -> UE property” roundtrip 测试。 3. 回归 `DebugServer` 容器观察、StaticJIT `GetCppForm()` 和 GC 引用收集。 |

### Arch-TS-02：`UInterface` 目前被建模成 full-reload 特例，且方法签名在 materialization 时被降成“只保留函数名”

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `UInterface` 支持的架构障碍 |
| 当前设计 | 当前实现没有把 interface 当成独立的 property/type family，而是把它塞进 `ClassGenerator` 的 full reload 分支：预处理阶段只保留 `InterfaceMethodDeclarations` 字符串；reload 阶段再临时生成空 `UFunction` stub，并用按名字查找的 generic callback 转发。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:1102` — `FAngelscriptClassDesc` 只记录 `bIsInterface`、`ImplementedInterfaces` 和 `TArray<FString> InterfaceMethodDeclarations`，接口方法没有结构化函数描述。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2081` — `ShouldFullReload()` 对 `bIsInterface` 和 `ImplementedInterfaces.Num() > 0` 直接返回 `true`，接口及其实例类都会被拉进 full reload。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2592` — 如果接口在 AS 编译后没有 `ScriptType`，运行时会手动 `RegisterObjectType()`，说明 interface 仍依赖额外补注册。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2803` — `DoFullReload()` 为接口只从 `InterfaceMethodDeclarations` 里抽函数名，然后创建 `FUNC_Event | FUNC_BlueprintEvent` 的最小 `UFunction` stub，没有复用现有参数/返回值建模。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:56` — `CallInterfaceMethod()` 只靠 `FindFunction(Sig->FunctionName)` 做按名转发。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:5060` — `FinalizeClass()` 通过递归搜 `ImplementedInterfaces` 并按函数名校验实现类是否覆盖接口方法。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:112` — 运行时 cast 也是通过 `TargetType->GetUserData()` 和 `ImplementsInterface()` 做 class-flag 级别判断，而不是经由 `FInterfaceProperty` / `TScriptInterface` 风格的显式类型桥接。 |
| 优点 | 现有路径能在不改动大量 binder 的情况下把脚本 interface 挂进 UE 反射体系，并且 `ImplementsInterface` 判定与 Blueprint 侧语义一致。 |
| 不足 | `UInterface` 现在不是一个稳定的 TypeSystem 一等公民，而是 “预处理字符串 + full reload + 名字转发” 的组合规则：这会放大 reload 半径，也让参数签名、默认值、元数据和后续反射演进难以复用现有函数描述体系。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 不生成新的 interface class；`FInterfacePropertyDesc` 直接把 `FScriptInterface` 映射为底层 `UObject` + `GetInterfaceAddress()`，接口仍停留在 property bridge 层。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:533`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1592`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ObjectRegistry.cpp:98` | 对“脚本侧持有接口值”这件事，优先复用对象桥接和 property descriptor，而不是先创建新的动态 interface 类。 |
| puerts | `FInterfacePropertyTranslator` 直接处理 `FScriptInterface` 的 `UEToJs/JsToUE`；类包装阶段仅遍历 `Class->Interfaces` 把接口函数挂到 JS prototype，没有把 interface 提升成独立 reload lane。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:598`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:312` | 接口方法暴露可作为 wrapper 层扩展，而不是强耦合到动态 class materialization。 |
| UnrealCSharp | 有独立的 `FDynamicInterfaceGenerator` 负责 interface lifecycle，`FTypeBridge` 把 `FInterfaceProperty` 明确映射到 `TScriptInterface<>` 泛型，`FInterfacePropertyDescriptor` 负责值桥接。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicInterfaceGenerator.cpp:21`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicInterfaceGenerator.cpp:154`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicInterfaceGenerator.cpp:231`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:415`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/ObjectProperty/FInterfacePropertyDescriptor.cpp:29`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp:799` | interface 需要独立的生命周期和 property bridge；一旦显式建模成 `TScriptInterface<>` 风格，class 生成、property 桥接和 codegen 可以共用同一套元数据。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 interface 从“full reload 特例”提升为显式的 type/function family，先补结构化方法描述，再逐步缩小 reload 半径。 |
| 具体步骤 | 1. 在预处理/分析阶段，把 `InterfaceMethodDeclarations` 从 `TArray<FString>` 升级为可复用的 `FAngelscriptFunctionDesc` 列表，直接复用现有参数、返回值、meta 解析逻辑。 2. 在 `DoFullReload()` 的 interface 分支里改用 `AddFunctionReturnType()` 和 `AddFunctionArgument()` 生成完整 `UFunction` 签名，替换当前“只抽函数名”的 stub 逻辑；`CallInterfaceMethod()` 继续保留为兼容 fallback。 3. 为 `ImplementedInterfaces` 增加 diff 级别判断：仅实现类的接口集合变化时走 soft relink，只有接口签名或继承链变化时才升级为 full reload。 4. 在 `TypeSystem` 中新增一个 interface/property adapter，让 `FInterfaceProperty` 或 `TScriptInterface` 风格桥接不再只靠 `CLASS_Interface` 标志和 `GetUserData()`，从而把 interface cast、property roundtrip 和 class reload 收束到同一条显式路径。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` |
| 预估工作量 | L |
| 架构风险 | interface 签名一旦从“名字 stub”变成真实 `UFunction`，现有热重载路径和 Blueprint 反射缓存会更敏感；需要分阶段保留旧 dispatch fallback。 |
| 兼容性 | 对现有脚本 `interface` 语法可保持向后兼容；首阶段主要提升运行时反射精度。第二阶段缩小 reload 半径时，可能改变热重载触发时机，但不应改变脚本源代码写法。 |
| 验证方式 | 1. 增加脚本 `interface` 的参数/返回值 roundtrip 用例，包括 `const&`、container、delegate。 2. 验证实现类增删接口时，`ShouldFullReload()` 只在签名变更时升级。 3. 用 Blueprint/`ImplementsInterface`/脚本 cast 三条路径回归 `GetInterfaceAddress()` 与方法分发。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P0 | Arch-TS-02 | `UInterface` 支持的生命周期与类型桥接 | 结构性重构 | 高 |
| P1 | Arch-TS-01 | 新类型映射的扩展半径 | 扩展点收敛 | 中 |

---

## 架构分析 (2026-04-08 14:26)

### Arch-TS-03：脚本类型身份依赖 `ScriptTypePtr` / `GetUserData()` 双向裸指针回填，热重载与新类型族扩展共享同一条隐式接缝

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 类型身份与反射对象之间的持久关联方式 |
| 当前设计 | 当前仓库把“AS 类型是谁、它对应哪个 `UClass` / `UStruct` / `UDelegateFunction`”这件事分散存放在 `UASClass::ScriptTypePtr`、`UASStruct::ScriptType` 以及 `asITypeInfo::GetUserData()` 三处，并在热重载时手工清空再回填。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h:29` — `UASClass` 直接持有 `void* ScriptTypePtr`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.h:15` — `UASStruct` 直接持有 `asITypeInfo* ScriptType`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:219`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:291`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:308` — `FAngelscriptTypeUsage` 在类型库查不到时，回退到 `ScriptClass->GetUserData()`、`UASClass::ScriptTypePtr`、`UASStruct::ScriptType`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:372`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:387` — `FromTypeId()` 通过 `GetUserData()` 分辨 delegate / script struct / script object。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2590`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2610`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2701` — class/interface/struct 物化后又把 UE 反射对象反写回 `SetUserData(...)`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:1037` — 分配脚本对象时再次从 `ScriptType->GetUserData()` 取回 `UASClass*`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4200`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4308`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4995`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:5029` — soft/full reload 会直接替换或清空这些裸指针。 |
| 优点 | 运行时跳转很短，`Cast<>`、对象构造、反射回查都能直接拿到 AngelScript 原生 `typeinfo`，实现成本低。 |
| 不足 | 类型身份没有单一 owner：`Core`、`ClassGenerator`、对象构造、hot reload、debugger 都默认自己知道哪一侧负责更新 `GetUserData()` 和 `ScriptTypePtr`。一旦后续再加 `TScriptInterface`、新容器、外部工具链或多轮 reload，这条隐式接缝就很容易出现 stale pointer、局部刷新成功但全局身份表未同步的问题。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FClassRegistry` 显式维护 `UStruct* -> FClassDesc` 与 metatable name -> `FClassDesc` 的双表；`FPropertyRegistry` 再按 `UField*` 缓存合成 property/interface，不依赖脚本 VM 内部 `userdata` 反向猜类型。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:46`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:90`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:302`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/PropertyRegistry.cpp:316` | 先把“类型身份表”做成 registry，再让 property/object/function 桥接去消费 registry；identity seam 不再散落在 runtime 各处。 |
| puerts | `JSClassRegister` 统一维护 `TypeId -> JSClassDefinition`、`UETypeName -> JSClassDefinition` 两套映射；对象、接口、容器桥接统一通过 `IObjectMapper` / translator 取类型定义，不把 `UStruct*` 塞回脚本运行时内部指针槽位。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JSClassRegister.cpp:143`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JSClassRegister.cpp:201`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JSClassRegister.cpp:267`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:615`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:887` | 把“类型定义缓存”和“对象实例映射”拆开，identity 先落成可查询表，再由 translator 复用。 |
| UnrealCSharp | `FDynamicClassGenerator` / `FDynamicInterfaceGenerator` 维护 `NamespaceMap`、`DynamicClassMap`、`DynamicInterfaceMap`；`FTypeBridge` 再集中做 `FProperty -> reflection class` 映射和泛型实例化。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicClassGenerator.cpp:26`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicInterfaceGenerator.cpp:15`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicInterfaceGenerator.cpp:231`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:197`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:415` | 动态类生命周期和属性类型桥接各有自己的事实表，避免“谁持有当前脚本类型指针”变成跨模块约定。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在现有 API 外包一层显式 `ScriptTypeRegistry`，把裸指针回填降级为缓存实现细节，而不是系统事实来源。 |
| 具体步骤 | 1. 在 `Core/` 新增 `FAngelscriptScriptTypeRegistry`，以稳定 key（建议 `FName + generation` 或内部 handle）记录 `{ ScriptType, UField, Kind, ModuleName }`。 2. 新增统一 adapter 封装 `SetUserData` / `GetUserData`，让 `FromTypeId()`、`FromClass()`、`FromStruct()`、`AllocScriptObject()` 全部只通过 registry 取回目标对象。 3. `UASClass::ScriptTypePtr` 与 `UASStruct::ScriptType` 第一阶段保留，但改成“registry lookup 的写透缓存”；热重载先更新 registry，再统一刷新缓存。 4. 第二阶段把 Blueprint 子类补丁、debugger 溯源、interface 临时注册都迁到 registry API，避免每个子系统各自直写 `SetUserData(...)`。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 预估工作量 | L |
| 架构风险 | registry 与现有裸指针缓存并存的过渡期最容易出现双写不一致；必须先把写入口收敛，再谈移除旧成员。 |
| 兼容性 | 对现有 Angelscript 语法与脚本用户保持向后兼容；影响集中在 Runtime 内部 C++ 结构。首阶段可以完全保留现有字段和 `GetUserData()` 行为。 |
| 验证方式 | 1. 增加 `UClass/UStruct/delegate/interface` 的 `AS type -> UE field -> AS type` roundtrip 测试。 2. 覆盖 soft/full reload 后 Blueprint 子类、`AllocScriptObject()`、debugger source lookup 不读取 stale pointer。 3. 人工回归 `Cast<>`、对象构造、interface 临时注册。 |

### Arch-TS-04：`UASFunction` 的 ABI 优化被编码成大量 `UClass` 子类矩阵，新调用形状扩展成本偏高

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 函数参数/返回值桥接的可扩展性 |
| 当前设计 | 当前仓库先在 `FinalizeArguments()` 把参数和返回值归类为 `ParmBehavior` / `VMBehavior`，再由 `AllocateFunctionFor()` 按“线程安全、JIT、参数个数、primitive size、是否引用、是否对象返回”等组合选择 `UASFunction_*` / `UASFunction_*_JIT` 子类。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h:129`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h:141` — `UASFunction` 先定义两套行为枚举。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h:230`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h:356` — header 里直接声明一整组 `UASFunction_NoParams` / `UASFunction_DWordArg` / `UASFunction_ReferenceArg` 及其 `_JIT` 变体。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:736` — `FinalizeArguments()` 用 `NeedConstruct()`、`IsObjectPointer()`、`IsPrimitive()`、value size 等规则推导 ABI 形状。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:1762`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:1924` — `AllocateFunctionFor()` 再把这些形状映射到具体 `UASFunction_*` 子类。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:1561`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:1599`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:1697` — optimized/generic call helper 仍需配合具体子类分发。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3513`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3624` — class generator 在 property 物化后才调用 `FinalizeArguments()`，ABI 规划与 `UFunction` 生成强绑定。 |
| 优点 | 对最常见的 “无参 / 单 primitive 参数 / 单引用参数 / primitive 返回值 / raw JIT” 热路径做了非常激进的专门优化，性能意图清晰。 |
| 不足 | 一旦要支持新的快路径形状，通常要同时修改行为分类、工厂分派、JIT 与非 JIT 子类、`RuntimeCallFunction` / `RuntimeCallEvent` 以及测试矩阵。对新增 `FOptionalProperty`、新容器引用语义、16-byte 值类型等未来类型映射，这个结构的改动点会迅速膨胀。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FFunctionDesc` 直接遍历 `UFunction` 参数列表，为每个参数创建 `FPropertyDesc`；参数行为由 descriptor 对象承担，而不是靠 `UFunction` 子类矩阵区分。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:51`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1537` | 函数级与属性级桥接职责分离，新增类型首先补 `PropertyDesc`，不会连带扩散成一组新的 function subclasses。 |
| puerts | `FFunctionTranslator::Init()` 为返回值和每个参数各创建一个 `FPropertyTranslator`；运行时由同一个 translator 对象驱动 `ProcessEvent()`，`UJSGeneratedFunction` 本身不按 ABI 形状分裂。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp:100`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp:131`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp:384`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JSGeneratedClass.cpp:232` | “函数对象”与“参数翻译策略”是组合关系，不是继承矩阵；新增参数类型主要扩 translator。 |
| UnrealCSharp | `FFunctionDescriptor` 保存 `PropertyDescriptors`、`ReturnPropertyDescriptor` 和 reference/out 索引，`FUnrealFunctionDescriptor` 再用模板化 call path 复用同一份 descriptor 数据。 | `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FFunctionDescriptor.cpp:18`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/FPropertyDescriptor.cpp:47`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Public/Reflection/Function/FUnrealFunctionDescriptor.inl:7` | 可以保留 fast path，但 fast path 依赖的是 descriptor/plan，而不是为每种 ABI 新建一个 `UFunction` 派生类。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 保留现有 fast path 思想，但把 ABI 选择结果收敛成数据对象 `CallThunkPlan`，逐步从 subclass matrix 迁移到“单函数壳 + 可替换执行计划”。 |
| 具体步骤 | 1. 在 `UASFunction` 旁新增 `FAngelscriptCallThunkPlan`，把当前 `FinalizeArguments()` 推导出的 `ParmBehavior` / `VMBehavior`、是否 raw JIT、是否 world context、是否有 out/ref 统一收敛成 plan。 2. 让 `AllocateFunctionFor()` 第一阶段继续创建现有子类，但同时产出 plan；新增类型或 ABI 形状优先走“base `UASFunction` + plan 驱动”的 generic fallback，不再立即新增子类。 3. 第二阶段把 `UASFunction_NoParams`、`UASFunction_DWordArg`、`UASFunction_ByteReturn` 这类可由同一模板 helper 覆盖的子类合并成少量执行器，把 `_JIT` 差异改成 plan 上的 function pointer。 4. 最后补基准和回归，确认最常见热路径仍命中优化，而未知新形状至少能正确落到 generic path。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/`, `Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 如果在第一步就删除现有 specialized subclasses，最容易把性能回归和调度 bug 混在一起；应先做双轨 plan，再逐步裁剪子类。 |
| 兼容性 | 对脚本作者完全向后兼容；变化只在 `UASFunction` 的 C++ 执行路径。首阶段可以保留现有类层次，旧序列化/反射名不必立刻变化。 |
| 验证方式 | 1. 为 `void()`、单 `float`、单 `const&`、object return、delegate invoke、`float extended to double` 建回归组。 2. 对比迁移前后的 `ProcessEvent()` / raw JIT benchmark。 3. 增加一个“当前没有专门子类的新 ABI 形状”用例，确认 generic fallback 可运行。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-TS-03 | 脚本类型身份的 owner 与热重载一致性 | 结构性收敛 | 高 |
| P2 | Arch-TS-04 | `UASFunction` ABI 扩展成本 | 执行模型收敛 | 中 |

---

## 架构分析 (2026-04-08 14:37)

### Arch-TS-05：`UHT` 函数表与运行时 `FAngelscriptTypeUsage` 各自维护一套类型真相，新增类型映射会落成“双轨同步”问题

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 代码生成管线与运行时类型系统是否共享同一套类型 canonicalization |
| 当前设计 | 运行时 `UASFunction` 已经用 `FArgument` 结构化保存 `FProperty*`、`FAngelscriptTypeUsage`、`ParmBehavior`、`VMBehavior`；但 `AngelscriptUHTTool` 生成函数表时并不复用这套模型，而是重新把 `UhtProperty` 展平成 C++ 签名字串，再用 header 文本做二次匹配。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h:161` — `UASFunction::FArgument` 持有 `FProperty* Property`、`FAngelscriptTypeUsage Type`、`ParmBehavior`、`VMBehavior`，说明运行时已经有结构化参数模型。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h:183` — `UASFunction` 直接维护 `Arguments`、`DestroyArguments`、`ReturnArgument`，并在 `FinalizeArguments()` 后驱动真实执行路径。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs:68` — `TryBuild()` 遍历 `function.ParameterProperties`，按参数顺序手工重建签名。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs:109` — `BuildParameterType()` 直接调用 `property.AppendFullDecl(...)` 把类型降成字符串。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs:70` — header resolver 再用 `BuildExpectedParameterTypes()` / `BuildExpectedReturnType()` 与解析后的声明做 exact match。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs:171` — `NormalizeTypeText()` 通过移除 `const`、`&`、空格进行弱 canonicalization，说明 generator 侧比较依赖文本归一化。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:470` — 只有 `TryBuild()` 成功后才生成 `signature.BuildEraseMacro()`，否则函数表要么退回特殊分支，要么放弃精确签名。 |
| 优点 | UHT sidecar 不需要链接运行时 C++ 类型对象，能独立在 UHT 阶段产出 `AS_FunctionTable_*.cpp`，对构建链侵入较小。 |
| 不足 | 同一个 UE 函数的参数类型同时存在“运行时 `FAngelscriptTypeUsage` 视角”和“UHT 字符串签名视角”，新增类型映射时必须同时保证两侧的 canonicalization 收敛；否则会出现运行时能调用、函数表却无法精确重建，或者函数表生成成功、运行时类型适配未跟上的错位。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | property bridge 只有一套事实来源：`FPropertyDesc::Create()` 按 `FProperty` 家族建 descriptor，容器 descriptor 再递归持有 inner/key/value descriptor，没有额外的 header 文本签名解析层。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:537`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:738`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:890`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1028`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1537`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1592`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1615` | 即使同时支持 interface、array、map、set，canonicalization 仍集中在 `FProperty` 工厂，而不是一套 runtime + 一套 generator。 |
| puerts | `.d.ts` 生成和运行时 translator 虽然分属两个模块，但都按 `FProperty` 家族递归工作；声明生成的 `GenTypeDecl()` 与运行时 `FPropertyTranslator::Create()` 使用同一条 property-kind 轴。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:741`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:827`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:891`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1308`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1390` | generator 与 runtime 不必共享二进制代码，但应共享同一种“按 property family 递归”的抽象，而不是字符串解析。 |
| UnrealCSharp | codegen `FGeneratorCore::GetPropertyType()`、运行时 `FPropertyDescriptor::Factory()`、反向桥接 `FTypeBridge::GetClass()` 三层都以 `FProperty` 家族为中心，支持 interface/container/optional 时扩展点集中。 | `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:82`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:144`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:231`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/FPropertyDescriptor.cpp:47`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/FPropertyDescriptor.cpp:87`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/FPropertyDescriptor.cpp:123`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:266`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:336`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:353` | 新增 `FOptionalProperty` 或 `FInterfaceProperty` 时，主要是在集中 switch/factory 上补齐，而不是再复制一套 header 签名重建规则。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 保留现有 `AngelscriptUHTTool` sidecar 形态，但补一层结构化 `Signature IR`，让 UHT 与运行时至少共享同一种 property-family 描述，而不是只靠字符串。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptUHTTool/` 增加 `AngelscriptPropertyShape` / `AngelscriptFunctionShape` 记录类型，把 `Kind`、`Inner`、`Key`、`Value`、`ObjectClass`、`InterfaceClass`、`QualifierFlags` 从 `UhtProperty` 中抽出来，与现有 `AngelscriptFunctionSignature` 并存。 2. 让 `AngelscriptFunctionSignatureBuilder.cs` 在生成 `ERASE_*` 前先产出 shape，并把 shape 写入现有 CSV 或新增 `GeneratedFunctionTableManifest.json`；`BuildParameterType()` / `NormalizeTypeText()` 暂时继续保留，作为 legacy fallback。 3. 在 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` 新增校验：对同一 `UFunction`，比较 UHT shape 与运行时 `FProperty` / `FAngelscriptTypeUsage` 的 family、wrapper、inner 递归结构是否一致。 4. 当 shape 覆盖率稳定后，让 `AngelscriptFunctionTableCodeGenerator.cs` 优先基于 shape 生成显式宏，仅对 `overloaded-unresolved`、`unexported-symbol` 等遗留边角继续走 header resolver。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | M |
| 架构风险 | 过渡期会同时存在“文本签名”和“结构化 shape”两套输出，如果没有测试钉住，很容易产生双写不一致。 |
| 兼容性 | 对现有脚本语法、现有 `AS_FunctionTable_*.cpp` 消费方可保持向后兼容；首阶段只是新增 manifest 和测试，不要求立刻替换旧签名逻辑。 |
| 验证方式 | 1. 运行 `AngelscriptGeneratedFunctionTableTests`，验证 `UFunction` 参数/返回值的 shape 与运行时 `FProperty` 递归结构一致。 2. 抽样回归 `TArray`、`TMap`、`TSet`、`TOptional`、delegate/native object 函数表生成。 3. 人工制造一个 overload/header 排版差异用例，确认新 shape 路径仍能生成稳定的 `ERASE_*`。 |

### Arch-TS-06：`UInterface` 的值桥接和 callable 桥接仍停留在 class-flag 特例，尚未进入主 property pipeline

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `UInterface` 在类型系统里是否是一等 `FProperty` / callable family |
| 当前设计 | 当前仓库对 interface 的主要支持点仍是“class metadata + cast/implements 判断”：对象类型桥接只生成 `FClassProperty` / `FObjectProperty`，接口调用面又在反射 fallback 和函数表生成里被单独排除，因此 interface value 与 interface callable 都没有进入统一 type/property pipeline。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:122` — `FUObjectType::CreateProperty()` 只在 `UClass::StaticClass()` 时生成 `FClassProperty`，其他对象一律生成 `FObjectProperty`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:149` — `MatchesProperty()` 只匹配 `FObjectProperty`，没有 `FInterfaceProperty` 分支。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:119` — runtime interface cast 通过 `TargetType->GetUserData()` 取出 `UClass*`，再检查 `CLASS_Interface` 和 `ImplementsInterface()`；值语义仍是“object + class flag”。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp:267` — reflective fallback 明确把 `CLASS_Interface` 归为 `RejectedInterfaceClass`。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:466` — interface/native interface 的函数表直接写成 `ERASE_NO_FUNCTION()`，没有进入常规签名生成。 |
| 优点 | 现有路径实现成本低，不必立刻引入 `FScriptInterface` / `TScriptInterface<>` 语义，也避免把尚未稳定的 interface 细节扩散到全部 callable 桥。 |
| 不足 | `UInterface` 现在更像“能建类壳、能做 `ImplementsInterface` 判断”的外围特例，而不是和 object/container/delegate 同级的 property family；这会直接阻塞 P10 相关主线需求，因为 interface 参数、返回值、native callable、reflective fallback、函数表生成都无法复用同一条桥接路径。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FInterfacePropertyDesc` 直接以 `FScriptInterface` 为值载体，`GetValueInternal()` / `SetValueInternal()` 都通过 `GetInterfaceAddress()` 完成值桥接；factory 里把 `CPT_Interface` 作为普通 property case。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:533`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:541`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:554`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1592` | interface 首先是 property/value family，再谈上层对象包装；这样不会把 interface support 绑死在 class materialization 特例上。 |
| puerts | `FInterfacePropertyTranslator` 把 `FScriptInterface` 的 `UEToJs/JsToUE` 做成独立 translator，并在工厂里与 array/map/set 同级注册；`StructWrapper` 还会显式把 `Class->Interfaces` 上的方法并入 wrapper。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:598`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:627`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1308`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:312` | 先把 interface value 打通，再把 interface method 暴露并入 wrapper 层，避免“值桥接缺失导致 callable 也只能禁用”。 |
| UnrealCSharp | `FPropertyDescriptor::Factory()`、`FInterfacePropertyDescriptor`、`FTypeBridge::GetClass(FInterfaceProperty*)`、`FGeneratorCore::GetPropertyType()` 和 `FDynamicInterfaceGenerator` 共同覆盖 interface 的 property、代码生成、反向桥接和生命周期。 | `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/FPropertyDescriptor.cpp:47`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/FPropertyDescriptor.cpp:87`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/ObjectProperty/FInterfacePropertyDescriptor.cpp:29`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:266`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:415`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:144`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicInterfaceGenerator.cpp:154` | interface support 不是某个单点补丁，而是 property bridge、type name、dynamic generator 一起承认 `FInterfaceProperty`/`TScriptInterface<>` 的存在。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先把 interface 值桥接收拢为显式 `FInterfaceProperty` adapter，再逐步放开 callable 和函数表生成；不要继续扩大 `CLASS_Interface` 特判的覆盖面。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` 新增独立的 interface binder（建议单独文件，而不是继续塞进 `Bind_BlueprintType.cpp`），实现 `CanCreateProperty`、`CreateProperty`、`MatchesProperty`、`GetCppForm`、参数/返回值搬运，并以 `FScriptInterface` / `FInterfaceProperty` 为底层载体。 2. 为 `FAngelscriptType::RegisterTypeFinder()` 增加 `FInterfaceProperty` 识别，把 interface 参数、返回值、成员属性都走新的 adapter；保留现有 `ImplementsInterface()` / quick cast 路径作为 fallback。 3. 放宽 `BlueprintCallableReflectiveFallback`：只有在 interface 参数或返回值仍落入 unsupported family 时才拒绝，已被新 adapter 覆盖的 native interface 函数允许进入 reflective path。 4. 更新 `AngelscriptFunctionTableCodeGenerator.cs`，对 `UhtClassType.Interface` / `NativeInterface` 优先尝试生成显式 erase 签名，仅在 adapter 尚未覆盖的场景继续输出 `ERASE_NO_FUNCTION()`。 5. 在测试层新增 interface value roundtrip、native interface BlueprintCallable、`TArray<TScriptInterface<...>>`、脚本 cast 四组回归，验证新 adapter 与旧 class-flag fallback 并存。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptTest/`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` |
| 预估工作量 | L |
| 架构风险 | interface value adapter 与现有 class-flag cast 并存时，最容易出现“同一接口一部分走 `FScriptInterface`，另一部分仍走 `UObject*`”的双语义问题；必须先在测试里钉住 roundtrip 和 fallback 优先级。 |
| 兼容性 | 首阶段可以保持现有脚本 `ImplementsInterface` / object cast 语义不变，新的 interface property 支持作为增量能力加入；不要求现有脚本立刻改写为新类型写法。 |
| 验证方式 | 1. 为 `FInterfaceProperty` 成员、参数、返回值增加 `UE property -> AS type -> UE property` roundtrip。 2. 回归 native interface 函数的 reflective fallback 与函数表生成，不再命中 `RejectedInterfaceClass` / `ERASE_NO_FUNCTION()`。 3. 覆盖 `GetInterfaceAddress()`、`ImplementsInterface()`、脚本 cast 三条路径，确保新旧桥接并存时行为一致。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P0 | Arch-TS-06 | `UInterface` 值桥接与 callable 桥接仍未进入主 property pipeline | 结构性补洞 | 高 |
| P1 | Arch-TS-05 | `UHT` 函数表与运行时类型模型双轨并存 | 生成链收敛 | 中 |

---

## 架构分析 (2026-04-08 14:48)

### Arch-TS-07：类型 canonical key 仍以脚本名和少量特例槽位为主，命名空间与新泛型族扩展容易退化成“继续加特判”

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 类型身份的 canonical key 与泛型族注册方式 |
| 当前设计 | `TypeSystem` 目前同时维护 `TypesByAngelscriptName`、`TypesByClass`、`TypesByData` 三类索引，但真正最常落地的入口仍是“脚本名字符串 + 少量 hard-coded singleton slot”。`FromTypeId()` 对大部分脚本类型直接拿 `ScriptType->GetName()` 回查；`FAngelscriptTypeDatabase` 又单独保留 `ScriptObjectType`、`ScriptStructType`、`ScriptDelegateType`、`ScriptEnumType` 以及 `ArrayTemplateTypeInfo` 这类专用槽位。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:54` — `Register()` 先以 `GetAngelscriptTypeName()` 检查重复，再写入 `TypesByAngelscriptName`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:110` — `RegisterAlias()` 直接向 `TypesByAngelscriptName` 追加别名，没有独立 canonical key 层。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:120` — `GetByAngelscriptTypeName()` 仍是公开静态入口之一。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:420` — `FromTypeId()` 对非 script-object / 非 script-enum 的脚本类型直接按 `ScriptType->GetName()` 查类型。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:592` — `FAngelscriptTypeDatabase` 除了 map 之外，还硬编码保存 `ScriptObjectType`、`ScriptStructType`、`ScriptDelegateType`、`ScriptMulticastDelegateType`、`ScriptEnumType`、`ScriptFloatType` 等槽位。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:608`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:560` — 当前只有 `ArrayTemplateTypeInfo` 被提升成专用模板基类型槽位。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2596` — `UInterface` 补注册路径通过 `RegisterObjectType(InterfaceName)` 和 `GetTypeInfoByName(InterfaceName)` 再次按名字回查。 |
| 优点 | 对现有脚本语法最直接，primitive 和常见对象类型的注册/别名成本低，调试时也容易从 declaration 文本回到注册项。 |
| 不足 | 类型 key 没有真正独立于“脚本显示名”存在：一旦引入 namespaced type、多个同名 script type、更多 generic family，或者需要把 `TScriptInterface` / 新容器模板也作为一等模板族处理，就会继续扩张 singleton slot、名字回查和 special-case 分支。`UInterface` 当前主线也会被这个问题放大，因为它正好走的是 engine-level `GetTypeInfoByName()` 路径。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FLuaEnv` 在环境内显式持有 `ClassRegistry` / `ContainerRegistry` / `PropertyRegistry`；`ClassRegistry` 以 `UStruct*` 为主 key，再补 metatable name；`PropertyRegistry` 则从 `UField*` / `FProperty*` 构造真实 `FPropertyDesc`，避免运行时只靠脚本名决定类型身份。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:104`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:53`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:90`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:302`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/PropertyRegistry.cpp:316` | 可以保留“名称别名”作为脚本层入口，但运行时 canonical identity 应优先落在 `UStruct*` / `FProperty*` / env-owned registry 上。 |
| puerts | `FPropertyTranslator::Create()` 直接按 `FProperty` 家族分派 `Interface` / `Array` / `Map` / `Set` translator；`StructWrapper` 也是围绕真实 property 重建 translator，而不是依赖脚本层字符串名。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1225`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1390`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:21` | 泛型族扩展面集中在 property-kind factory，新增 family 时不需要再创造新的“全局名字槽位”。 |
| UnrealCSharp | `FPropertyDescriptor::Factory()` 与 `FTypeBridge::GetClass()` 都以 `FProperty` 家族为主轴；`FInterfaceProperty`、`FArrayProperty`、`FMapProperty`、`FSetProperty`、`FOptionalProperty` 都能直接生成对应 generic reflection type。 | `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/FPropertyDescriptor.cpp:47`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:197`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:415`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:446`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:562`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:599` | 先定义稳定的类型 key 与 generic family，再决定脚本语言表面名；这对后续补 `TScriptInterface<>`、`TOptional<>`、自定义容器族更友好。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留现有脚本名称与 alias 兼容面的前提下，引入独立的 `TypeKey` / `TemplateFamily` 层，让运行时 canonical lookup 不再主要依赖字符串和专用槽位。 |
| 具体步骤 | 1. 在 `Core/AngelscriptType.h` 新增 `FAngelscriptTypeKey`，至少包含 `BaseFamily`（primitive/native class/native struct/script object/script struct/delegate/template instance）、`UField*` / `asITypeInfo*`、`TemplateBaseTypeInfo`、`TemplateArity` 等字段。 2. 在 `FAngelscriptTypeDatabase` 增加 `TypesByKey` 与 `TemplateBaseTypes`，并把现有 `TypesByAngelscriptName` 明确降级成 parser alias/index；`ArrayTemplateTypeInfo` 先改成 `TemplateBaseTypes[array]` 的兼容壳。 3. 让 `FAngelscriptType::Register()` 在写 `TypesByAngelscriptName` 的同时双写 `TypesByKey`，并在 `RegisterAlias()` 只影响 alias map、不影响 canonical key。 4. 让 `FAngelscriptTypeUsage::FromTypeId()`、`GetByProperty()` 和 interface 补注册路径优先按 `TypeInfo*` / `FProperty` / `UField` key 查找，仅在 legacy type 没有 key 时才回退到 `GetByAngelscriptTypeName()` / `GetTypeInfoByName()`。 5. 增加 `TypeManifest` 测试或 dump：输出同一 key 对应的 declaration、alias、template family，专门钉住“同名不同 namespace”“script interface + native interface”“`TArray/TOptional` family key 稳定性”三类用例。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp` |
| 预估工作量 | M |
| 架构风险 | 过渡期会同时存在 key-based 与 name-based 两套查找，若没有 trace/test 钉住，最容易出现 alias 命中但 canonical key 未同步的双写不一致。 |
| 兼容性 | 对现有脚本 declaration、alias、现有 `Bind_*.cpp` 注册 API 可保持向后兼容；首阶段只新增内部 key 和测试，不要求脚本改名。 |
| 验证方式 | 1. 增加 `TypeId -> TypeUsage -> Declaration` roundtrip 测试，覆盖 native class、script struct、script enum、`TArray`、`TOptional`。 2. 增加 interface 回归：脚本 `interface` 注册后通过 `TypeInfo*` key 查回，不再依赖 `GetTypeInfoByName()` 单点。 3. 制造同名 alias / namespace 场景，确认只影响 alias map，不覆盖 canonical key。 |

### Arch-TS-08：值生命周期合同分散在 `FAngelscriptType`、容器 binder、debugger 临时值和 `ICppStructOps` 四处，新增值/容器族需要重复实现 trait

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 值生命周期与 trait（construct/destruct/copy/hash/identical）的统一 owner |
| 当前设计 | 当前仓库把值生命周期合同拆散到了四个层面：`FAngelscriptType` 虚表定义 trait，container binder 自己循环元素并缓存 `bNeedConstruct/bNeedDestruct/bNeedCopy`，debugger 临时字面量再手工走 `NeedConstruct/NeedDestruct`，script-defined `UStruct` 另用 `FASStructOps` 把构造/析构/拷贝/相等/Hash 接回 UE `ICppStructOps`。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:202` — `FAngelscriptType` 直接定义 `NeedCopy` / `CopyValue` / `NeedConstruct` / `ConstructValue` / `NeedDestruct` / `DestructValue`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:456` — `FAngelscriptTypeUsage` 只是把这些 trait 直接转发出去。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:696` — debugger `FDebuggerValue::AllocateLiteral()` / `ClearLiteral()` 也手工调用 `NeedConstruct()` / `DestructValue()`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp:125` — `TArray` 拷贝逻辑自己按 subtype 的 `NeedCopy/NeedConstruct/NeedDestruct` 逐元素处理。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp:1770` — `TArray` 运行时 ops 还会缓存 `bNeedConstruct`、`bNeedDestruct`、`bNeedCopy`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TOptional.cpp:85`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TOptional.cpp:325` — `TOptional` 又单独重写一套 `Construct/Copy/Destruct` 与 `bNeed*` 缓存。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.cpp:40`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.cpp:127`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.cpp:228` — `FASStructOps` 另外维护 `Construct` / `Destruct` / `Copy` / `Identical` / `GetStructTypeHash`，并在 `UpdateScriptType()` 时改写 `STRUCT_IdenticalNative`。 |
| 优点 | script-only 值类型和容器可以针对 AngelScript 的 ABI/内存布局做专门优化，不必完全受限于 UE `FProperty` API。 |
| 不足 | “一个新值类型或新容器 family 需要在哪些地方补 trait” 没有单一 owner：除了 type binder 本身，container ops、debugger 临时值、script struct materialization 也都可能要跟着补。结果是新增类型时更容易出现“容器能跑但 debugger 泄漏/双析构”“script struct hash 变了但 container map/set 还在用旧语义”这类分叉回归。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FPropertyDesc` 直接把 `InitializeValue` / `DestroyValue` / `Copy` / `Identical` 委托给底层 `FProperty`；`FunctionDesc` 参数生命周期也只调用 descriptor；array/map/set 描述符递归持有 inner/key/value descriptor，而不是每个容器再发明一套 trait 布尔缓存。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.h:95`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:285`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:401`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:738`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:890`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1028` | trait owner 更集中，新增容器时主要是组合 descriptor，而不是复制生命周期逻辑。 |
| puerts | `FPropertyWithDestructorReflection` 统一把清理委托给 `Property->DestroyValue_InContainer()`；`ContainerWrapper` 在 array/map 场景里统一走 `Property->InitializeValue` / `DestroyValue` / `CopySingleValue`；array/set/map translator 则直接依赖 `CopyCompleteValue`。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:361`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:862`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/ContainerWrapper.cpp:221`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/ContainerWrapper.cpp:517` | 值生命周期先统一到 property translator / `FProperty` 语义，再由 wrapper 复用；容器只负责布局和桥接，不负责重新定义 trait。 |
| UnrealCSharp | `FPropertyDescriptor` 提供 `InitializeValue_InContainer`、size/alignment 等统一入口；`FArrayHelper` / `FMapHelper` 只持有 inner/key/value descriptor，并通过 descriptor 做 `Set`、`DestroyValue`、hash。 | `Reference/UnrealCSharp/Source/UnrealCSharp/Public/Reflection/Property/FPropertyDescriptor.inl:57`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Container/FArrayHelper.cpp:11`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Container/FArrayHelper.cpp:133`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Container/FMapHelper.cpp:23`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Container/FMapHelper.cpp:119`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Container/FMapHelper.cpp:200` | 把生命周期、尺寸、hash、set/get 统一封进 descriptor，container helper 不再直接认识每个语言类型的特殊 trait。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 保留 `FAngelscriptType` 的语言语义入口，但新增统一 `ValueOps` 层，把 construct/destruct/copy/hash/identical 收拢成可缓存的共享合同，先让容器和 debugger 复用它。 |
| 具体步骤 | 1. 在 `Core/AngelscriptType.h` 新增 `FAngelscriptValueOps`（或 `IAngelscriptValueOps`），至少包含 `Size`、`Alignment`、`Initialize`、`Destroy`、`Copy`、`Identical`、`Hash`；默认实现继续转发到现有 `FAngelscriptTypeUsage` trait。 2. 在 `FAngelscriptTypeUsage` 上增加 `GetValueOps()`，并让 `FDebuggerValue` 优先通过 `ValueOps.Initialize/Destroy` 管理 temporary literal，而不是直接散落调用 `NeedConstruct()` / `DestructValue()`。 3. 第一阶段只改 `Bind_TArray.cpp` 与 `Bind_TOptional.cpp`：把 `bNeedConstruct/bNeedDestruct/bNeedCopy` 和逐元素 construct/destruct/copy 逻辑改成基于 `ValueOps` 的统一 helper；`TMap/TSet` 维持旧路径作为对照。 4. 第二阶段把 `UASStruct::CreateCppStructOps()` 也接到同一套 trait 源上，让 `Construct` / `Copy` / `Identical` / `Hash` 至少从同一个 capability plan 派生，避免 script struct 与容器/hash 语义分叉。 5. 增加生命周期回归组：`TArray<ScriptStruct>`、`TOptional<ScriptStruct>`、`TMap<Key, ScriptStruct>`、debugger watch 临时值、reload 后 `STRUCT_IdenticalNative`/hash 一致性。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TOptional.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSet.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` |
| 预估工作量 | M |
| 架构风险 | 如果 `ValueOps` 设计成每次临时分配对象，容器热路径和 debugger 会立刻有性能回归；应优先做可缓存的轻量 struct / function pointer 方案，并先迁移 `TArray/TOptional` 验证收益。 |
| 兼容性 | 对现有脚本类型语法与外部 API 完全向后兼容；变化集中在 C++ 内部 trait owner 与测试面。 |
| 验证方式 | 1. 新增构造/析构计数型测试，覆盖 `TArray` 扩容缩容、`TOptional` 置空/重新赋值、`TMap` 覆盖写。 2. 回归 debugger 自动求值临时值，确认没有泄漏和 double-destroy。 3. 对 script-defined `UStruct` 做 `Identical` / `Hash` / container roundtrip 测试，确认 `FASStructOps` 与容器 trait 来源一致。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-TS-07 | 类型 canonical key 仍偏字符串/特例槽位 | 身份模型收敛 | 高 |
| P1 | Arch-TS-08 | 生命周期 trait owner 分散 | 值语义收敛 | 中 |

---

## 架构分析 (2026-04-08 15:02)

### Arch-TS-09：类型表示同时散落在 `TypeId`、`FCppForm` 和 UHT 字符串签名三条链上，新增类型族会演变成跨 runtime 与 codegen 的同步补丁

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 类型表示的一致性与新增类型映射的改动半径 |
| 当前设计 | runtime 侧由 `FAngelscriptTypeUsage::FromTypeId()` 把 AngelScript `TypeId` 还原成 `FAngelscriptTypeUsage`，property/materialization 由 `FAngelscriptType` 虚表处理，StaticJIT 再从同一类型对象拉 `GetCppForm()`；但 UHT tool 生成函数表时并不消费这套结构化类型信息，而是重新用 `UhtProperty.AppendFullDecl()`、`TypeTokens` 和 header 文本解析/归一化来重建 C++ 签名。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:148` — `FAngelscriptType` 同时暴露 `MatchesProperty`、`CreateProperty`、`EmitReferenceInfo` 等 property 合同。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:331` — 同一类型对象还要负责 `GetCppForm()`，供 native/JIT C++ 形态推导使用。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:340` — `FromTypeId()` 先按 AngelScript 内建 type id，再按 `asITypeInfo`、`GetUserData()`、模板子类型递归恢复 `FAngelscriptTypeUsage`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp:475`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp:495`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TOptional.cpp:298` — 容器类型各自递归子类型生成 `TArray<...>`、`TMap<...>`、`TOptional<...>` 的 `CppType/CppGenericType`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp:401`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp:447` — StaticJIT 直接依赖 `GetCppForm()` 来拼 `LiteralType` 和 include。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs:68` — UHT 侧重新遍历 `ParameterProperties` 并用 `AppendFullDecl()`/`TypeTokens` 组装字符串签名。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs:90`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs:171` — 头文件解析再按归一化后的字符串比对参数与返回值，归一化会移除 `const`、`&` 和空格。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:466` — `Interface` / `NativeInterface` 仍被直接降成 `ERASE_NO_FUNCTION()`。 |
| 优点 | runtime 类型对象足够灵活，`TypeId -> TypeUsage -> Property/JIT` 这条链对现有 binder 很直接；UHT tool 也能在不依赖 runtime 初始化的情况下独立生成函数表。 |
| 不足 | 一旦新增类型家族，不只是补 `FAngelscriptType` 或 binder；还要同时补 `GetCppForm()`、UHT 签名重建、header resolver 归一化与函数表生成策略。`UInterface` 现在被 UHT 直接 stub 掉，正是这三套表示没有统一源头的直接症状。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 以 `FPropertyDesc::Create(FProperty*)` 做单点 property 工厂；数组/Map/Set 只是递归持有 inner/key/value descriptor，再交给 `ContainerRegistry` 管理容器实例，没有再引入一条“字符串签名解析链”。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1537`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:738`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:890`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1028`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:104` | 类型扩展的主轴始终是 `FProperty` 家族和 descriptor 递归，不需要让代码生成器再去猜 C++ 原始声明。 |
| puerts | `FInterfacePropertyTranslator`、各类数值/对象 translator 都由 `FPropertyTranslator::Create()` 集中工厂分派；`StructWrapper` 缓存 translator，并把 interface 方法直接挂到 prototype。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:598`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1225`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:21`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:312` | interface、container、普通对象都先被压成统一的 property family，再决定 JS 暴露形态；wrapper 层不依赖 header 文本归一化。 |
| UnrealCSharp | `FPropertyDescriptor::Factory()` 和 `FTypeBridge::GetClass()` 都以 `FProperty` 家族为唯一分派入口，`FInterfaceProperty`、`FArrayProperty`、`FMapProperty`、`FSetProperty`、`FOptionalProperty` 都能直接得到泛型 reflection type；容器 helper 也只消费 descriptor。 | `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/FPropertyDescriptor.cpp:47`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:415`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:446`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:562`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:598`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Container/FArrayHelper.cpp:11`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Container/FMapHelper.cpp:23` | 先把“类型是什么”稳定建模成结构化 reflection type，再让容器 helper、runtime bind、codegen 共享这套结果，新增 family 的落点更集中。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 引入一层 runtime/UHT 共用的结构化 `TypeSignature`，把“`FProperty/UhtProperty` 属于什么 family、有哪些修饰符、子类型是什么”从文本拼接里抽出来，先缩掉 interface 与容器族的双轨表示。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptUHTTool/` 新增 `AngelscriptPropertySignature.cs`，把 `UhtProperty` 归一化为结构化 family：`Primitive`、`Object`、`Class`、`Struct`、`Enum`、`Interface`、`Array`、`Map`、`Set`、`Optional`、`Delegate`，并显式保留 `IsConst`、`IsRef`、`IsStaticArray`、sub-signatures。 2. 让 `AngelscriptFunctionSignatureBuilder.cs` 与 `AngelscriptHeaderSignatureResolver.cs` 先比较结构化 signature，再只把字符串归一化作为 legacy fallback；首批覆盖 `Interface`、`Array`、`Map`、`Set`、`Optional` 五类。 3. 在 `AngelscriptFunctionTableCodeGenerator.cs` 里把 `Interface/NativeInterface => ERASE_NO_FUNCTION()` 的 blanket rule 改成“只有结构化 signature 仍不可表达时才 stub”，这样 `UInterface` 可以按 family 单独逐步打开。 4. 在 runtime 侧增加一个轻量 dump/test，输出 `FAngelscriptTypeUsage -> FCppForm` 结果，并与 UHT 的 `AngelscriptPropertySignature` 对照，先钉住 `TArray/TMap/TOptional/FInterfaceProperty` 四组 roundtrip。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` |
| 预估工作量 | M |
| 架构风险 | 过渡期会同时存在“结构化 signature”和“旧字符串匹配”两套路径；如果没有 manifest/test 对照，最容易出现 UHT 认为可生成、runtime 却没有等价 `CppForm` 的分叉。 |
| 兼容性 | 对现有脚本语法、现有 `Bind_*.cpp` 注册 API 和已有生成产物格式可保持向后兼容；首阶段只是新增结构化建模与 fallback，不要求一次性删除旧字符串逻辑。 |
| 验证方式 | 1. 重新运行 `AngelscriptUHTTool`，确认现有非 interface 函数表无回归。 2. 新增 `Interface/Array/Map/Optional` 的 UHT 生成回归，验证不再无条件落到 `ERASE_NO_FUNCTION()`。 3. 在 runtime 测试中做 `FProperty -> TypeUsage -> CppForm` 与 UHT `UhtProperty -> PropertySignature` 的 family/修饰符一致性检查。 |

### Arch-TS-10：`UASClass` 同时承担 UE 反射壳、脚本对象生命周期和源码索引，导致 interface 与非实例化类型难以脱离 object-construction 轨道

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 动态 `UClass` 生成的职责边界，以及这条边界对 `UInterface`/新对象族扩展的影响 |
| 当前设计 | `UASClass` 不只是动态 `UClass` shell；它同时保存脚本构造/默认值函数、`ScriptTypePtr`、GC schema、debug value prototype、组件覆盖信息，还直接实现脚本对象分配、析构、虚函数解析和源码文件定位。换句话说，当前“类元数据”“实例生命周期”“调试/源码索引”都压在同一个 `UASClass` 对象上。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h:18` — `UASClass` 自带 `CodeSuperClass`、`ConstructFunction`、`DefaultsFunction`、`ComposeOntoClass`、`ScriptTypePtr`、`ReferenceSchema`、`DebugValues`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h:104` — 头文件直接把 `AllocScriptObject()`、`FinishConstructObject()` 放成 `UASClass` 静态入口。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:104` — `ResolveScriptVirtual()` 每次都先爬 `GetFirstASClass()`，再从 `ScriptTypePtr->virtualFunctionTable` 取真实脚本函数。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:927` — `GetFirstASClass()`/`GetFirstASOrNativeClass()` 说明混合继承链的脚本身份解析也由 `UASClass` 承担。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:965` — `RuntimeDestroyObject()` 直接通过 `ScriptTypePtr` 调脚本析构。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:1037` — `AllocScriptObject()` 从 `ScriptType->GetUserData()` 取回 `UASClass`，再走 `StaticAllocateObject` 与 `ClassConstructor`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:1137` — `FinishConstructObject()` 依赖 `TopClass->ScriptTypePtr` 决定 defaults 何时执行。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:1497` — `GetSourceFilePath()`、`GetRelativeSourceFilePath()`、`IsDeveloperOnly()` 也都经由 `ScriptTypePtr -> Module` 回查源码/模块信息。 |
| 优点 | 动态脚本类一旦 materialize 完成，构造、析构、虚调用、源码定位都能直接在 `UASClass` 上落地，热路径跳转短，对现有 script object 模型实现成本低。 |
| 不足 | `UASClass` 现在隐含了“这是一个可构造、可析构、可执行 defaults 的脚本对象类型”前提。推断上，这就是 `UInterface` 很难成为一等 type family 的根因之一，因为 interface 只需要反射壳和方法签名，不应被迫共享对象分配/默认值/析构通道；未来若再加新的非实例化脚本类型或轻量 wrapper，也会先撞到这层 object-centric 设计。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FLuaEnv` 显式持有 `ObjectRegistry`、`ClassRegistry`、`ContainerRegistry` 等服务；`ClassRegistry` 只负责 `UStruct/name -> FClassDesc`，`ObjectRegistry` 单独负责 live `UObject` 绑定与回收。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:104`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:46`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:90`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:302`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ObjectRegistry.cpp:47`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ObjectRegistry.cpp:83`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ObjectRegistry.cpp:113` | 类型描述、对象生命周期和容器实例是分 registry 管理的；类壳本身不需要同时背负实例构造与源码索引。 |
| puerts | `FInterfacePropertyTranslator` 只负责 `FScriptInterface <-> JS object` 值桥接；`StructWrapper` 缓存 property/function translator，并把 interface 函数投射到 prototype。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:598`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:21`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:312` | interface 支持主要是 property/wrapper 责任，而不是把接口也塞进一条“脚本对象构造”通路。 |
| UnrealCSharp | `FCSharpEnvironment` 启动时创建 `ClassRegistry`、`ObjectRegistry`、`ContainerRegistry`、`DelegateRegistry` 等独立服务；`FCSharpBind::BindImplementation(UObject*)` 只是把现有 `UObject` 绑定到托管对象；`FDynamicInterfaceGenerator` 也有独立的 interface lifecycle。 | `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp:63`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Public/Registry/FCSharpBind.inl:54`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicInterfaceGenerator.cpp:154`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicInterfaceGenerator.cpp:180`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicInterfaceGenerator.cpp:231` | 对象绑定、class descriptor、interface 生成是拆开的；interface 可以有完整生命周期，但不必共享 UObject 脚本构造逻辑。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 `UASClass` 从“运行时服务总汇”降成“UE 反射 shell”，抽出 engine-owned 的 `ClassRuntimeDescriptor`，先把源码/模块索引与构造/析构回调移出类对象，再为 interface 留出不走对象构造的独立 lane。 |
| 具体步骤 | 1. 在 `ClassGenerator/` 或 `Core/` 新增 `FAngelscriptClassRuntimeDescriptor`，集中保存 `asITypeInfo*`、`ConstructFunction`、`DefaultsFunction`、module/source info、debug value prototype、GC schema 等 runtime 元数据，并用 `UClass*`/`asITypeInfo*` 双索引挂到当前 `FAngelscriptEngine`。 2. 让 `ResolveScriptVirtual()`、`RuntimeDestroyObject()`、`GetSourceFilePath()`、`IsDeveloperOnly()`、`GetConstructingASObject()` 优先查 descriptor registry；`UASClass::ScriptTypePtr` 与现有字段先保留为兼容镜像，直到 reload 路径稳定。 3. 把 object-construction 相关 helper（`AllocScriptObject`/`FinishConstructObject`/defaults 执行）收敛到 descriptor 的 `InstantiationPolicy`，显式区分 `ScriptObjectClass` 与未来的 `ScriptInterfaceShell`；后者只需要函数签名和 module/source info，不进入对象构造/析构通道。 4. 增加三组回归：脚本类 + Blueprint 子类的虚调用仍正确命中 descriptor；reload 后源码路径/DeveloperOnly 仍可查询；interface shell 可生成并查询方法/源码，但不会触发 defaults/construct/destruct 逻辑。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.*`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` |
| 预估工作量 | L |
| 架构风险 | 该改造会碰到 hot reload、对象构造顺序和 debug/source 查询三条路径；如果 descriptor registry 与 `UASClass` 镜像不同步，最容易出现“虚调用修好了但源码路径/析构仍指向旧 type”的分裂状态。 |
| 兼容性 | 可以保持 `UASClass` 对外 API 与现有脚本语法不变；首阶段只是把内部 owner 挪到 descriptor registry，并保留旧字段作为 fallback，不要求用户修改脚本类定义。 |
| 验证方式 | 1. 回归脚本类对象构造、defaults 执行和析构顺序。 2. 回归 Blueprint 子类包裹脚本类时的 `ResolveScriptVirtual()`。 3. 为 interface shell 增加“不触发对象构造/析构但能查询源码路径与方法列表”的专门测试，验证新 lane 与旧对象 lane 成功分离。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P0 | Arch-TS-09 | runtime/UHT/StaticJIT 三套类型表示未统一 | 生成链收敛 | 高 |
| P1 | Arch-TS-10 | `UASClass` 责任过载，interface 仍受 object-centric 设计牵制 | 结构性解耦 | 高 |

---

## 架构分析 (2026-04-08 15:15)

### Arch-TS-11：`FAngelscriptTypeUsage` 的 payload 同时承载类型身份、原生 `FProperty` 句柄和 debugger 游标，扩展状态只能继续走隐式约定

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 类型使用对象的数据模型与扩展状态承载方式 |
| 当前设计 | `FAngelscriptTypeUsage` 既表示“这是什么类型”，又通过同一个 union 临时塞入 `ScriptClass`、`FProperty*` 和 `TypeIndex`；debugger 再把导航状态回写到 `Usage` 本体里。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:349` — `FAngelscriptTypeUsage` 只有 `SubTypes`、`Type`、`bIsReference`、`bIsConst` 和一个 `ScriptClass/UnrealProperty/TypeIndex` union。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:440` — `operator==` 只按 `ScriptClass` 分支比较 payload，没有区分 `UnrealProperty` 或 `TypeIndex` 语义。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:616` — `FDebuggerValue` 直接内嵌一个 `FAngelscriptTypeUsage Usage`，说明 debugger 导航复用了类型对象本体。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp:268`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp:299`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp:353` — `TMap` debugger 用 `Usage.TypeIndex` 在“容器根节点”和“第 N 个 pair”之间切换。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.cpp:810`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.cpp:896`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.cpp:917` — multicast delegate debugger 同样把 `TypeIndex` 当成绑定项游标。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4322`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4430`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4434` — class reload 的 property flattening 在识别不了 `FAngelscriptTypeUsage` 时，会构造 `FRawUnrealPropertyType` 并把真实 `FProperty*` 塞进 `Usage.UnrealProperty`。 |
| 优点 | 数据结构非常轻，现有 binder、debugger 和 reload 代码都能直接传递一个 `Usage` 继续工作，不需要额外 cursor/context 对象。 |
| 不足 | 同一个字段同时表示“脚本类型句柄”“原生 property fallback”“调试游标”，已经把类型身份和工具态状态混在了一起；后续若为 `UInterface`、fixed array、复杂容器 debugger 或更多 reload 辅助信息再加 side data，只能继续复用这一个隐式槽位。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FPropertyDesc` 家族按 property family 持有显式字段，数组/Map/Set 各自保存 `InnerProperty`、`KeyProperty`、`ValueProperty`；property 工厂只负责按 `FProperty` 构造正确 descriptor。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:738`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:890`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1028`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1537` | 容器子类型、原生 property 句柄和 copy-back 语义都由 descriptor 自己持有，不需要让“类型使用对象”兼任 debugger cursor。 |
| puerts | `FInterfacePropertyTranslator`、`FScriptArrayPropertyTranslator`、`FScriptMapPropertyTranslator` 都持有自己的 `Property`/`InterfaceProperty`/`ArrayProperty`/`MapProperty`；`FStructWrapper::GetPropertyTranslator()` 再按 `FProperty` 缓存 translator。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:598`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:862`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:961`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:21` | “类型身份”与“某次调试/包装会话的辅助状态”被拆在 translator/wrapper 层，扩展新 family 时不会去污染统一的 type usage 数据模型。 |
| UnrealCSharp | `FPropertyDescriptor::Factory()` 直接按 `FProperty` family 产出 descriptor，`FTypeBridge::GetClass()` 则递归处理 `FInterfaceProperty`、`FArrayProperty`、`FMapProperty`、`FOptionalProperty` 的结构化泛型类型。 | `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/FPropertyDescriptor.cpp:47`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:415`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:446`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:562`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:599` | type bridge 只消费结构化 property family；debugger/容器实例/运行时句柄都挂在独立 descriptor 或 registry 上，而不是挤进一个通用 usage payload。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 `FAngelscriptTypeUsage` 收缩成“类型身份对象”，把 debugger/reload 的游标与 native property fallback 拆成显式 sidecar。 |
| 具体步骤 | 1. 在 `Core/AngelscriptType.h` 新增带 tag 的 `FAngelscriptTypePayload`，至少拆出 `ScriptTypeInfo`、`NativeProperty`、`DebugCursor` 三种分支；保留旧 union 访问器做一轮兼容转发。 2. 修改 `FDebuggerValue` / `FDebuggerScope`，新增独立的 `FTypeDebugCursor`，让 `Bind_TMap.cpp`、`Bind_Delegates.cpp` 不再通过 `Usage.TypeIndex` 表示子节点导航。 3. 把 `FRawUnrealPropertyType` 改成显式接收 `FProperty*` 的 wrapper usage，而不是写 `Usage.UnrealProperty = Property`；这样 reload copy/flatten 路径可以单独演进。 4. 为 `FAngelscriptTypeUsage` 增加统一 `GetTypeHash`/比较语义，只覆盖真正的类型身份字段；新增回归测试覆盖 `TMap` debugger 展开、delegate debugger 展开、raw Unreal property copy fallback。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` |
| 预估工作量 | M |
| 架构风险 | debugger、reload property flattening 和任何直接读写 `Usage.TypeIndex/Usage.UnrealProperty` 的路径都会被触达；如果迁移期没有统一兼容层，最容易出现“类型等价正常但 debugger 子节点打不开”的局部回归。 |
| 兼容性 | 对脚本语法和脚本用户完全向后兼容；影响主要在插件内部 C++ 扩展 API，可通过保留旧字段访问器一段时间平滑迁移。 |
| 验证方式 | 1. 回归 `TMap`/multicast delegate debugger 展开，确认子节点游标不再依赖 `Usage.TypeIndex`。 2. 回归 class reload 的 property copy，确认 raw Unreal property fallback 仍能 `CopyCompleteValue`/`Identical`/`DestroyValue`。 3. 为 `FAngelscriptTypeUsage` 新增 equality/hash 测试，验证 debugger cursor 变化不会改变类型身份比较结果。 |

### Arch-TS-12：函数参数 materialization 仍是“`TypeUsage -> 布尔标志 -> CPF_*`”双阶段规则，`FInterfaceProperty` 和新容器语义无法在 TypeSystem 内闭环

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 函数参数/返回值的 property policy owner |
| 当前设计 | 参数的“by value / in ref / out ref / copy-back / 默认值元数据”不是由 type adapter 一次性给出，而是先在分析阶段生成 `FAngelscriptArgumentDesc` 布尔标志，随后在 `AddFunctionArgument()` / `AddFunctionReturnType()` 中再次翻译成 UE `CPF_*` 和 metadata。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:907` — `FAngelscriptArgumentDesc` 把参数 policy 存成 `bBlueprintByValue`、`bBlueprintOutRef`、`bBlueprintInRef`、`bInRefForceCopyOut`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:161`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:164` — type system 对参数 policy 只暴露 `CreateProperty()` 和一个单比特钩子 `IsParamForcedOutParam()`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.h:37` — `TArray` 通过覆写 `IsParamForcedOutParam()` 参与参数 policy，说明容器 family 目前只有这一条回调通道。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:602`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:619`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:624`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:639` — 分析阶段根据 `FromParam()`、`RefFlags` 和 `IsParamForcedOutParam()` 把 policy 编码成这些布尔字段。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3935`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3948`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3949` — return property 先由 `ReturnType.CreateProperty()` 创建，再统一补 `CPF_Parm | CPF_OutParm | CPF_ReturnParm`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3966`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3976`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3980`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3986`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3996` — argument property 也是先创建 property，再按这些布尔标志补 `CPF_OutParm` / `CPF_ReferenceParm` / `CPF_ConstParm` 和默认值 metadata。 |
| 优点 | 对当前已有的 Blueprint 暴露规则，这条路径集中在 `ClassGenerator` 一处，排查现有 `ref/out/default` 行为时比较直接。 |
| 不足 | type adapter 只能告诉 `ClassGenerator`“能不能建 property”与“是否强制 outparam”，不能完整声明函数级 contract；推断上，P10 的 `FInterfaceProperty` / `TScriptInterface<>` 若要支持 by-value、const ref、out ref 和默认值组合，首先就会撞上这条双阶段规则。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FPropertyDesc` 直接从 `FProperty` flags 推导 `IsConstOutParameter()` / `IsNonConstOutParameter()` / `IsOutParameter()`；数组和 Map 的 `CopyBack()` 直接复用 descriptor 上的 out-param 语义。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.h:57`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.h:71`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:753`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:905` | 参数 policy 跟着 property descriptor 走，函数桥接层不需要另发明一套 `bBlueprintOutRef/bBlueprintInRef` taxonomy。 |
| puerts | `FunctionTranslator` 遍历 `UFunction` 的真实参数 property，直接创建 `FPropertyTranslator`；数组/Map translator 在自己的构造函数里读取 `CPF_OutParm` / `CPF_ConstParm` 决定 shallow-copy/out 行为。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp:120`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:862`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:961` | 参数桥接语义归属 translator，本体是 property family，而不是“先从脚本语法归类一次，再在函数生成时翻译第二次”。 |
| UnrealCSharp | `FPropertyDescriptor::Factory()` 统一产出 property descriptor，`FTypeBridge::GetClass()` 再递归处理 `FInterfaceProperty`、`FArrayProperty`、`FMapProperty`、`FOptionalProperty` 这类泛型/property family。 | `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/FPropertyDescriptor.cpp:47`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:415`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:446`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:562`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:599` | property family 先被结构化建模出来，函数/容器/接口桥接共享同一份结果，新增 `FInterfaceProperty` 或容器族时不需要再去补第二套参数布尔规则。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把函数参数/返回值的 property policy 收敛成 type-system 可生成的 `FunctionPropertyPlan`，让 `ClassGenerator` 只负责 materialize，不再二次推断。 |
| 具体步骤 | 1. 在 `Core/AngelscriptType.h` 新增 `FAngelscriptFunctionPropertyPlan`，至少包含 `PropertyFlags`、`bNeedsCopyBack`、`DefaultValueMeta`、`BlueprintPassMode`、`bAllowReturnProperty` 等字段。 2. 为 `FAngelscriptTypeUsage` 新增 `BuildFunctionPropertyPlan(RefFlags, DefaultValue, bIsReturn)` 入口；当前 `TArray` 的 `IsParamForcedOutParam()` 先迁到 plan 生成器里。 3. 让 `AddFunctionArgument()` / `AddFunctionReturnType()` 只消费 plan 来创建 property、设置 `CPF_*` 和 metadata；保留旧 `FAngelscriptArgumentDesc` 布尔字段一轮作为 fallback。 4. 第一阶段只迁移 `primitives + UObject + TArray/TMap/TSet/TOptional + delegate`，第二阶段再把 `FInterfaceProperty` / `TScriptInterface<>` 接到同一条 plan 通路，避免接口支持继续散在 `Analyze()` 和 `AddFunctionArgument()` 两头。 5. 增加参数签名回归：`const ref`、`out ref`、`inout ref`、container 参数、默认值元数据，以及 interface 参数/返回值的 `UFunction` flag 与 Blueprint 暴露行为。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` |
| 预估工作量 | M |
| 架构风险 | 迁移期会同时存在“旧布尔规则”和“新 plan”两条路径；如果没有对比测试，最容易出现 native 参数 flag 正确但 Blueprint default/meta 或 copy-back 行为分叉。 |
| 兼容性 | 对 Angelscript 源码语法和已生成的脚本 API 可保持向后兼容；变更集中在插件内部 C++ 生成流程，首阶段无需用户修改脚本签名。 |
| 验证方式 | 1. 对比迁移前后的 `UFunction` 参数 flags、`CPP_Default_*` metadata 和 `NumParms/ParmsSize`。 2. 回归 `TArray/TMap/TSet/TOptional` 的 ref/out 调用与 copy-back。 3. 为 `UInterface`/`TScriptInterface<>` 原型增加签名 roundtrip 测试，确认接口参数不再需要额外的 class-generator 特判。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P0 | Arch-TS-12 | 函数参数/返回值 property policy 的 owner 分散，`FInterfaceProperty` 难以增量接入 | 生成链收敛 | 高 |
| P1 | Arch-TS-11 | `FAngelscriptTypeUsage` payload 混装身份、原生 property 与 debugger 游标 | 数据模型收敛 | 中 |

---

## 架构分析 (2026-04-08 15:27)

### Arch-TS-13：`FProperty -> FAngelscriptTypeUsage` 仍依赖会写 `Usage` 的 `TypeFinder` 回调链，复杂 family 没有稳定的结构化重建入口

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 从 UE `FProperty` 重建脚本类型签名的 owner 与扩展合同 |
| 当前设计 | `FromProperty()` 先跑全局 `TypeFinders`，失败后退回 `GetByProperty()` 只拿到 base `FAngelscriptType`；array/map/set/weak-object 这类复杂 family 只能在 finder 回调里递归 `FromProperty()` 并手工写 `Usage.SubTypes`。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:147` — `GetByProperty()` 先遍历 `TypeFinders`，再线性扫描 `TypesImplementingProperties`，fallback 只返回 `TSharedPtr<FAngelscriptType>`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:234`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:248`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:252`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:256` — `FromProperty()` 在拿不到 finder 结果时只回填 `Usage.Type`，随后只额外处理 `CPF_ConstParm` / `CPF_ReferenceParm`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp:1552`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp:1161`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSet.cpp:621`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:2440` — array/map/set/weak object family 都要通过 `RegisterTypeFinder()` 递归 inner property，并直接改写 `Usage.SubTypes`。 |
| 优点 | primitive / object 这类简单 family 的接入门槛低，旧 binder 可以继续依赖 `MatchesProperty()` 与少量回调存活。 |
| 不足 | 复杂 family 的“结构化签名”没有单一 owner；新增 `FInterfaceProperty`、`TScriptInterface<>` 或新容器时，不只是补一个 type adapter，还必须抢占 finder 顺序并自己负责把 subtype/extra state 写进 `Usage`。这使扩展面更像“插一段 discovery 脚本”，而不是“注册一个 property family”。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FPropertyDesc::Create()` 用单一工厂按 `CPT_*` 分派，`CPT_Interface` 直接落到 `FInterfacePropertyDesc`；array/map/set descriptor 在构造函数里递归调用 `FPropertyDesc::Create()` 处理 inner/key/value，`PropertyRegistry` 再按 `UField*` 缓存结果。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1537`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1592`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:533`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:738`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:890`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1028`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/PropertyRegistry.cpp:316` | 先把 `FProperty` 变成完整 descriptor，再让上层桥接消费 descriptor；复杂 family 不需要依赖外部回调去回填子类型。 |
| puerts | `PropertyTranslatorCreator` 在一个集中工厂里处理 `InterfacePropertyMacro`、`ArrayPropertyMacro`、`MapPropertyMacro`、`SetPropertyMacro`，`FPropertyTranslator::Create()` 是唯一入口；`FStructWrapper::GetPropertyTranslator()` 再把 translator 缓存在 wrapper 实例里。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1225`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1308`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1312`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1316`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1320`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1390`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:21` | property family 的识别与缓存都是结构化入口，扩展新 family 时不需要另外插一条“会写 usage”的 discovery callback。 |
| UnrealCSharp | `FPropertyDescriptor::Factory()` 直接覆盖 `FInterfaceProperty`、`FArrayProperty` 等 family；`FTypeBridge::GetClass()` 再递归把 interface/array/map/set/optional 映射为结构化泛型 reflection type。 | `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/FPropertyDescriptor.cpp:47`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/FPropertyDescriptor.cpp:87`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/FPropertyDescriptor.cpp:91`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:415`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:446`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:562`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:584`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:599` | property family 与泛型子类型先被结构化建模，后续 class bridge、function bridge、container bridge 共用一份结果。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先引入结构化 `PropertySignature`/`PropertyBridgeDesc` 层，把 `FProperty` 重建从“finder side effect”收敛成“单一 descriptor factory”，再让 `FAngelscriptTypeUsage` 从 descriptor 派生。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/` 新增 `FAngelscriptPropertySignature` 或 `FAngelscriptPropertyBridgeDesc`，至少显式保存 `Family`、`Qualifiers`、`BackingProperty`、`SubSignatures`、`ObjectClass` / `InterfaceClass` / `Struct` 等 family data。 2. 把 `FAngelscriptTypeUsage::FromProperty()` 改成先调用 `BuildPropertySignature(FProperty*)`；array/map/set/object/interface/optional 的内层递归都在这里完成，`Usage` 只负责承接结果。 3. 现有 `RegisterTypeFinder()` 第一阶段保留为 legacy hook，但只允许补充 family resolver，不再直接作为最终 `Usage` 写入口；随后逐步把 `Bind_TArray.cpp`、`Bind_TMap.cpp`、`Bind_TSet.cpp`、`Bind_BlueprintType.cpp` 迁到 descriptor path。 4. 在 `ClassGenerator` 和 `Debugging` 侧优先消费 `PropertySignature`，为 P10 的 `FInterfaceProperty` / `TScriptInterface<>` 提供统一入口，避免接口桥接继续散落在 class reload 与 binder finder 两边。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSet.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` |
| 预估工作量 | M |
| 架构风险 | descriptor path 与旧 finder path 并存期间，最容易出现“property 创建正确，但 debugger / UHT / reload 还在读旧 usage 约定”的双轨分叉；必须靠 roundtrip 测试钉住。 |
| 兼容性 | 对 Angelscript 脚本语法保持向后兼容；对已有 C++ binder 扩展可先保留旧 `RegisterTypeFinder()` API，一段时间内作为 fallback，不要求一次性迁移。 |
| 验证方式 | 1. 新增 `FProperty -> PropertySignature -> TypeUsage -> CreateProperty()` roundtrip 测试，首批覆盖 `TArray<T>`、`TMap<K,V>`、`TSet<T>`、`TWeakObjectPtr<T>`、`FInterfaceProperty`。 2. 回归 `ClassGenerator` 生成后的 property shape 与 `DebugServer` 展示结果。 3. 对比 descriptor path 接入前后的 `MatchesProperty()` 命中率，确认复杂 family 不再依赖 finder 顺序。 |

### Arch-TS-14：类型注册表仍依赖 ambient engine 与 `LegacyDatabase` 回退，扩展插件无法显式附着或卸载自己的 type family

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 类型注册表的作用域、生命周期和扩展 owner |
| 当前设计 | 类型系统对外只有 `FAngelscriptType` 静态注册 API；内部通过 `TryGetCurrentEngine()` 找当前 engine 的 `TypeDatabase`，找不到时静默回退到进程级 `LegacyDatabase`。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:72`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:74`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:77`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:81` — 对外 mutation API 只有 `Register` / `ResetTypeDatabase` / `RegisterAlias` / `RegisterTypeFinder`，没有 owner-scoped handle 或 `Unregister`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:35`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:44`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:45` — `GetTypeDatabase()` 取不到 current engine 时会直接返回静态 `LegacyDatabase`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:592`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:593`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:597`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:598` — database 只保存类型表、名字表、finder 数组与 property-implementer 数组，没有注册 owner/provenance。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:71`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:718`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:761`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:969`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:994` — current engine 又依赖 `GAngelscriptEngineContextStack` / `TryGetCurrentEngine()`，缺 `FAngelscriptEngineScope` 会报错，而 engine-owned `TypeDatabase` 只在 `SharedState` 上创建。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:373` — 生命周期结束时是整表 `Reset()`，不是按 owner/family 精确移除。 |
| 优点 | 静态 binder 写起来非常直接，早期启动路径也能靠 legacy fallback 勉强把类型注册跑起来。 |
| 不足 | 一旦扩展从“内建 binder”走向“外部模块/多 engine/实验性 family”，作用域就不清晰了：在错误的上下文调用注册 API 会写进 `LegacyDatabase`；也无法只卸载某个模块注册的 `TypeFinder` 或 alias。对 P10 而言，这会让 `FInterfaceProperty` 的试验适配器很难与旧路径并行存在。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FLuaEnv` 创建时显式 new `ClassRegistry` / `ContainerRegistry` / `PropertyRegistry`，析构时逐一 delete；`FClassRegistry` 还提供 `Register()` / `Unregister()`。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:105`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:110`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:111`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:180`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:184`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:186`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:247`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:265` | registry 生命周期与 env 一一对应，扩展点可以按实例初始化和销毁，而不是依赖 ambient 全局状态。 |
| puerts | `FJsEnv` 自己持有 `FJsEnvImpl`，`FJsEnvGroup` 甚至可以并行构造多个 `FJsEnvImpl`；property translator 缓存在 `FStructWrapper` 实例的 `PropertiesMap` 中，而不是写进单例类型表。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnv.cpp:14`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnv.cpp:16`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvGroup.cpp:95`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvGroup.cpp:100`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvGroup.cpp:105`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvGroup.cpp:113`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:21`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:26`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:29` | 环境实例天然隔离，多实例和实验性 wrapper 可以按 env 维度存在，不需要共享一张 process-wide fallback 表。 |
| UnrealCSharp | `FCSharpEnvironment::Initialize()` 按环境创建 `ClassRegistry` / `ObjectRegistry` / `ContainerRegistry` / `DelegateRegistry` / `OptionalRegistry`，`Deinitialize()` 再按同一 owner 精确释放。 | `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp:54`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp:65`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp:69`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp:73`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp:75`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp:84`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp:136`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp:148`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp:184`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp:198`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp:212` | registry 是 environment-owned service，新增 family 或动态功能可以按环境启停，不需要靠全局静态 API 猜当前上下文。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 TypeSystem 注册表下沉为 engine-owned service，并为每次注册返回显式 handle；旧静态 API 仅保留为兼容包装层。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/` 新增 `FAngelscriptTypeRegistry`，由 `FAngelscriptEngine::SharedState` 持有，提供实例方法 `RegisterType()`、`RegisterAlias()`、`RegisterPropertyResolver()`、`Unregister(Handle)`。 2. 给每次注册返回 `FAngelscriptTypeRegistrationHandle`，记录 owner module、名字 key、class/data key 和 finder slot；模块 shutdown / hot reload / P10 实验分支都可以按 handle 精确回收，而不是整表 reset。 3. 把当前 `LegacyDatabase` 降级为 test-only 或 bootstrap-only 显式 scope，例如 `FLegacyTypeRegistryScope`；生产路径在没有 current engine 时直接 warning + fail fast，避免 silent fallback。 4. 让 `BindScriptTypes()` / bind bootstrap 先拿到目标 `FAngelscriptTypeRegistry&`，再执行各 binder；第一阶段保留 `FAngelscriptType::Register*` 静态 API，内部只是桥接到 current engine registry，并在缺 scope 时打印高优先级日志。 5. 新增隔离测试：两个 engine instance 分别注册不同的实验性 `FInterfaceProperty` 适配器与 alias，验证 property lookup、reload 和 debug path 不串库。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/`, `Plugins/Angelscript/Source/AngelscriptRuntime/Testing/`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` |
| 预估工作量 | M |
| 架构风险 | 迁移期如果静态 API 与实例 registry 双写不一致，会出现“当前 engine 查得到、legacy 表也残留旧 finder”的隐性状态污染；必须先把写入口收敛到单点。 |
| 兼容性 | 对脚本作者完全向后兼容；对现有 C++ binder 扩展可以保持源级兼容，首阶段仍可继续调用 `FAngelscriptType::Register*`，只是内部会被重定向到实例 registry。 |
| 验证方式 | 1. 新增多 engine / clone engine 自动化用例，验证不同实例上的 alias、finder、family 注册互不污染。 2. 人工回归缺少 `FAngelscriptEngineScope` 的调用路径，确认不再静默写入 `LegacyDatabase`。 3. 回归模块 reload / bind disable-enable 流程，确认单个扩展 family 可以被精确卸载与重注册。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P0 | Arch-TS-13 | `FProperty -> TypeUsage` 重建入口未结构化，`FInterfaceProperty` 和新容器 family 只能靠 finder side effect 接入 | 解析链收敛 | 高 |
| P1 | Arch-TS-14 | 类型注册表作用域依赖 ambient engine / legacy fallback，扩展与卸载无法显式建模 | 注册表实例化 | 中 |

---

## 架构分析 (2026-04-08 15:42)

### Arch-TS-15：`UASStruct` 通过 UE 私有 fake vtable 回填脚本语义，struct capability 的扩展点被绑死在引擎私有 ABI 上

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | script-defined `UStruct` 的能力注入与扩展面 |
| 当前设计 | `UASStruct` 不是仅保存 `ScriptType` 并复用 UE 公共 `UScriptStruct` API；它会在 `ASStruct.cpp` 内构造 `UE::CoreUObject::Private::FStructOpsFakeVTable` 派生物，把构造、析构、拷贝、比较、Hash 这几类脚本语义直接写进 fake vtable，再由 `UpdateScriptType()` 改写 `StructFlags`。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.cpp:21`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.cpp:22`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.cpp:24` — `FASStructOps` 直接缓存 `EqualsFunction`、`ConstructFunction`、`HashFunction`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.cpp:26`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.cpp:48`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.cpp:58`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.cpp:67` — `FASFakeVTable` 继承 `UE::CoreUObject::Private::FStructOpsFakeVTable`，并显式写入 `Construct/Destruct/Copy/Identical/GetStructTypeHash` 标志和函数指针。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.cpp:84`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.cpp:92`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.cpp:111` — 能力发现依赖 `beh.construct`、`opEquals`、`Hash()` 这些固定脚本入口。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.cpp:127`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.cpp:175`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.cpp:195` — construct / identical / hash 的实际 UE struct 合同都回调到脚本 VM。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.cpp:228`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.cpp:233`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.cpp:236` — `UpdateScriptType()` 再按 `EqualsFunction` 是否存在改写 `STRUCT_IdenticalNative`。 |
| 优点 | script-defined struct 可以较快接入 UE 的 `Identical` / `GetTypeHash` / 构造析构协议，现有容器与反射路径也因此能直接把脚本 struct 当成原生 `UScriptStruct` 使用。 |
| 不足 | 新增 struct capability 时，扩展点不是“注册一个新语义描述符”，而是“继续改 private fake vtable + 继续补固定方法名探测 + 继续同步 `StructFlags`”。这既放大 UE 版本升级风险，也让 `NetSerialize`、文本导入导出、版本兼容序列化这类后续能力很难增量挂接。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FPropertyDesc` 家族把生命周期与拷贝建立在 UE 公共 `FProperty` API 上，容器 descriptor 再递归组合 inner/key/value descriptor，没有去修改 `UScriptStruct` 的私有 fake vtable。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.h:95`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.h:102`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:738`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:753` | 把 struct 值语义留在公共 property contract 上，扩展新 family 时优先扩 descriptor，而不是继续扩大引擎私有 ABI 接缝。 |
| puerts | `FScriptStructWrapper` 只通过 `UScriptStruct->InitializeStruct()` / `DestroyStruct()` 和 object mapper 做 wrapper 生命周期管理；property translator 与 wrapper 是分层的。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:21`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:26`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:636`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:639`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:643`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:652` | wrapper/value 生命周期依赖 UE 公共 API，扩展性问题更多收敛在 translator/wrapper 层，而不是 CoreUObject 私有实现细节。 |
| UnrealCSharp | 结构体暴露先生成 `StaticStruct()`、`UStruct_RegisterImplementation()`、`UStruct_IdenticalImplementation()` 这类显式 wrapper 合同；编辑器生成链把 `Class/Struct/Enum/Binding` 分开跑。 | `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FStructGenerator.cpp:122`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FStructGenerator.cpp:128`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FStructGenerator.cpp:146`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FStructGenerator.cpp:167`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:266`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:274` | 先把 struct contract 显式沉淀成 wrapper/implementation 边界，再由生成链或运行时桥接去消费，减少对 UE 私有 ABI 的依赖面。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 保留现有 `UASStruct` 行为，但把“脚本能力发现”和“UE 私有 ABI 应用”拆成两层，先把 fake-vtable seam 收敛到单点兼容层。 |
| 具体步骤 | 1. 在 `ClassGenerator/` 或 `Core/` 新增 `FAngelscriptStructCapabilitySet`，显式保存 `Construct`、`Destruct`、`Copy`、`Identical`、`Hash`，并预留 `NetSerialize`、`ExportText`、`ImportText` 这类未来能力槽位；`ASStruct.cpp` 只负责从 `ScriptType` 解析这些 capability。 2. 把当前 `FASFakeVTable` 写入和 `StructFlags` 回写迁到独立 `StructOpsCompat.*` 文件，形成唯一的 UE 私有 ABI 接缝；`UASStruct::UpdateScriptType()` 只调用 `ApplyStructCapabilities(UASStruct&, CapabilitySet)`。 3. 第一阶段 capability set 仍只覆盖现有五项能力，保持行为不变；第二阶段新增 struct 扩展能力时，只改 capability resolver 与 compat adapter，不再直接膨胀 `ASStruct.cpp`。 4. 给 `AngelscriptTest` 增加 engine-upgrade 哨兵测试：验证 `Construct/Identical/Hash`、`STRUCT_IdenticalNative` 和 `CPF_HasGetValueTypeHash` 仍一致，并为未来 `NetSerialize`/文本导出预留空实现测试位。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/`, `Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | compat adapter 抽离后如果 capability 与旧 fake-vtable 写入不同步，最容易出现容器/hash 回归通过但 `IdenticalNative` flag 丢失的分叉；首阶段必须保留旧行为逐项对照。 |
| 兼容性 | 对现有脚本 struct 语法和运行时行为可以保持向后兼容；变更集中在内部 C++ owner 分层，不要求脚本作者修改 `opEquals`/`Hash` 写法。 |
| 验证方式 | 1. 回归 script struct 的构造、复制、`opEquals`、`Hash()`、`TArray<Struct>`/`TMap<Key, Struct>` 容器行为。 2. 检查 `StructFlags` 与 `CPF_HasGetValueTypeHash` 在迁移前后保持一致。 3. 编译一次目标 UE 版本并做一次 fake-vtable 接缝自检，确认 compat 层是唯一私有 ABI 触点。 |

### Arch-TS-16：`AngelscriptUHTTool` 的 sidecar 产物仍是“函数表 + 覆盖率报表”，TypeSystem 没有可复用的 type-family 级构建资产

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 构建期可见的类型资产粒度 |
| 当前设计 | 当前 UHT sidecar 只生成函数绑定 shards 和函数覆盖率报表：遍历入口沿着 `UhtClass -> UhtFunction` 收集 `BlueprintCallable` 函数，产出 `AS_FunctionTable_*.cpp`、summary JSON、两份 CSV；type family、本模块有哪些 class/struct/enum/interface、哪些 family 仍未被 runtime TypeSystem 覆盖，在构建期没有单独资产。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:14`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:29`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:37` — 生成器的核心记录类型只有 `FunctionEntry`、模块 summary 和函数 CSV entry。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:51`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:81`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:86` — `Generate()` / `GenerateModule()` 只围绕 function entry 收集与写 shard。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:166`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:174`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:220`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:246` — sidecar 的持久化输出仍只有 `AS_FunctionTable_Summary`、`AS_FunctionTable_ModuleSummary`、`AS_FunctionTable_Entries`。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:449`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:455` — `CollectEntries()` 只对 `UhtFunction` 产出 entry。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:466` — interface / native interface 仍统一落成 `ERASE_NO_FUNCTION()` stub。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:56`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:65`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:101`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:142` — exporter 的诊断资产也只是“可重建函数/跳过函数/跳过原因”。 |
| 优点 | 生成链轻量，输出物直接服务 `FAngelscriptBinds::AddFunctionEntry(...)`，对构建流程侵入小，也方便统计 direct bind 与 stub 比例。 |
| 不足 | TypeSystem 在构建期仍然“只有函数可见、类型不可见”：新增 `FInterfaceProperty`、新容器或新的 struct family 后，sidecar 不能回答“哪些类型族已经有 runtime owner、哪些类/属性仍在 fallback”。这让 P10 的 `UInterface` 支持和后续 family 扩展缺少一份稳定、可比较、可自动回归的 type asset。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 运行时不是 function-only 资产；`FPropertyDesc::Create()` 直接按 `CPT_Interface`、`CPT_Array`、`CPT_Map`、`CPT_Set` 构造 property descriptor，descriptor 本身就是 type-family 级资产。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1537`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1592`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1615`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1620`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1625` | 即使不走额外 codegen，也把“类型族是什么”沉淀成显式 descriptor，而不是只统计函数覆盖率。 |
| puerts | `FStructWrapper` 先按 property 创建 translator 并缓存，再在 wrapper 模板里挂接普通方法与 interface 方法；运行时资产以 type wrapper 为核心，而不是只看 function table。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:21`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:26`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:167`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:324`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:360` | 先把 class/struct wrapper 和 property translator 变成可缓存、可复用的类型资产，函数只是附着在 wrapper 上的能力之一。 |
| UnrealCSharp | 编辑器生成链会显式跑 `Class/Struct/Enum/Asset/Binding` 多条生成器，再统一编译；结构体生成物还包含 `StaticStruct()`、注册/反注册和 `Identical` 包装。 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:266`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:270`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:274`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:278`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:291`<br>`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:305`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FStructGenerator.cpp:122`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FStructGenerator.cpp:146`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FStructGenerator.cpp:167` | 构建期直接沉淀多种 type asset，因此 interface、struct、enum、binding coverage 都有明确输出，不会退化成“只有函数表可查”。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 保留现有函数表 sidecar，但补一份只读的 type catalog/coverage 资产，让 runtime、测试和文档能共享同一份 type-family 视图。 |
| 具体步骤 | 1. 在 `AngelscriptUHTTool/` 新增 `AngelscriptTypeCatalogEntry` 与 `AngelscriptTypeCatalogGenerator`，复用当前 UHT 遍历，把 `UhtClass` / `UScriptStruct` / `UEnum` / interface 的名称、类别、property family、函数数、是否含 interface、是否含未支持 family 收集成 catalog。 2. 第一阶段只生成 `AS_TypeCatalog.json` 和 `AS_TypeCatalog_ModuleSummary.csv`，不改 `AS_FunctionTable_*` 逻辑；catalog 条目里显式记录 `InterfaceStubbed=true/false`、`HasUnsupportedPropertyFamily=true/false`。 3. 在 `AngelscriptTest` 增加 catalog 对 runtime 的只读比对：确认 catalog 中出现的 `interface/array/map/set/optional` family 能在 runtime `TypeDatabase` 或 bind state 中找到 owner；找不到时作为 warning 或失败输出。 4. 当 catalog 稳定后，再让 `TypeSystem_ArchReview`、P10 跟踪脚本或 UHT 日志直接消费它，形成“新增 family 后先有 catalog 资产，再谈 runtime enable”的增量流程。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/`, `Plugins/Angelscript/Source/AngelscriptTest/`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` |
| 预估工作量 | S |
| 架构风险 | 如果 catalog 一开始就试图绑定 runtime 内部细节，容易重演已有 UHT/runtime 双轨问题；首阶段必须把它限制为只读观测资产，不参与实际绑定决策。 |
| 兼容性 | 完全向后兼容；新增的是 sidecar 输出与测试输入，不改变现有脚本 API、函数表格式和运行时绑定行为。 |
| 验证方式 | 1. 重新运行 UHT，确认现有 `AS_FunctionTable_*` 产物不变。 2. 检查 `AS_TypeCatalog.json` 是否稳定列出 class/struct/enum/interface 条目与 family 统计。 3. 用一组包含 `UInterface`、container property、script-defined struct 的测试样本，验证 catalog 与 runtime 覆盖率报告一致。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-TS-16 | UHT sidecar 只有函数资产，缺少 type-family 级构建可见性 | 观测面新增 | 高 |
| P2 | Arch-TS-15 | script struct capability 依赖 UE 私有 fake vtable，struct 扩展与引擎升级风险偏高 | 私有接缝收敛 | 中 |

---

## 架构分析 (2026-04-08 16:44)

### Arch-TS-17：`UASFunction` 把调用 ABI 压成固定行为枚举和大量特化 thunk，新增 type family 不能只靠注册 `FAngelscriptType`

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 函数参数/返回值桥接的 owner 与新增类型映射的改动半径 |
| 当前设计 | `TypeSystem` 负责把类型还原成 `FAngelscriptTypeUsage`，但真正的调用 ABI 不是 descriptor-driven，而是由 `UASFunction` 再把 `TypeUsage` 降成 `EArgumentParmBehavior` / `EArgumentVMBehavior` 两套固定枚举，并据此挑选一组特化 `RuntimeCallFunction()` / `RuntimeCallEvent()` thunk。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h:129`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h:141`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h:161`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h:195` — `UASFunction` 先定义两套行为枚举和 `FArgument`，再在 `FinalizeArguments()` 固化调用计划。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:736`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:762`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:823`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:890` — `FinalizeArguments()` 只按 `bIsReference`、`IsObjectPointer()`、`IsPrimitive()`、`NeedCopy()`、`NeedDestruct()` 和 `GetValueSize()` 做 ABI 分类。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:194`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:309`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:358` — 通用调用路径再按 `VMBehavior` 手工做 `StepCompiledIn`、`CopyValue()`、`DestructValue()` 和返回值 copy-back。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:1762`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:1796`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:1853` — `AllocateFunctionFor()` 会根据“无参 / 单 primitive 参数 / 单 ref 参数 / 对象返回”等固定形状选 `UASFunction_*` 子类。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:1961`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:2450`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:2485`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:2952` — 特化 thunk/JIT thunk 矩阵直接绑定这些行为枚举。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3411`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3624`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3958` — `ClassGenerator` 只负责分配 `UASFunction` 并塞入 `FAngelscriptTypeUsage`，随后整个调用 ABI owner 都转到 `ASClass.cpp`。 |
| 优点 | 现有 primitive / object pointer / ref-value 这几类热路径可以做非常激进的 specialized thunk 和 JIT 快路径，已有脚本调用性能友好。 |
| 不足 | 新增类型家族若不完全落入“primitive / object pointer / reference object value”这几个旧槽位，就不能只扩 `FAngelscriptType`。推断上，`FInterfaceProperty` / `TScriptInterface<>` 这类值既需要 `FScriptInterface` 专属 get/set，又不等同于普通 `UObject*`，会先撞到 `UASFunction` 的 ABI 分类矩阵。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FFunctionDesc::PreCall()` / `PostCall()` 直接遍历 `Properties` descriptor，参数准备、copy-back、返回值读取都委托给 `FPropertyDesc`；`FInterfacePropertyDesc` 自己处理 `FScriptInterface` 的 `SetObject()` / `SetInterface()`。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:279`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:285`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:314`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:352`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:373`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:533`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:559` | 函数桥接只管遍历 descriptor；interface/container 等特殊 family 在 property descriptor 内闭环，不需要再扩一套 per-type thunk taxonomy。 |
| puerts | `FFunctionTranslator::Init()` 为 return/arguments 逐个创建 `FPropertyTranslator`；`FInterfacePropertyTranslator` 单独处理 `FScriptInterface <-> JS object`，`FPropertyTranslator::Create()` 是统一工厂。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp:131`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp:135`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:598`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:615`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:629`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1390` | 调用层只组合 translator；新增 family 先补 translator 工厂，不必同步膨胀函数 thunk 子类。 |
| UnrealCSharp | `FFunctionDescriptor::Initialize()` 统一为每个参数/返回值创建 `FPropertyDescriptor`，只记录 `OutPropertyIndexes` / `ReferencePropertyIndexes`；`FRegisterFunction.cpp` 的 `CallN` 只按 buffer 拓扑分派到 `FUnrealFunctionDescriptor`，type-specific marshalling 仍在 descriptor。 | `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FFunctionDescriptor.cpp:18`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FFunctionDescriptor.cpp:38`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FFunctionDescriptor.cpp:53`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FFunctionDescriptor.cpp:56`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Domain/InternalCall/FRegisterFunction.cpp:19`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Domain/InternalCall/FRegisterFunction.cpp:58`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Domain/InternalCall/FRegisterFunction.cpp:126`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Public/Reflection/Function/FUnrealFunctionDescriptor.h:7` | 可以保留 arity/return-kind 级优化，但把类型家族差异压回 descriptor 层；函数入口不再直接认识 interface、container、optional 的具体 ABI。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 保留现有 `UASFunction_*` 优化类，但先在 `TypeSystem` 与 `ClassGenerator` 之间引入显式 `CallMarshaller` / `FunctionAbiPlan`，把“类型如何进出 VM”从 `ASClass.cpp` 的硬编码枚举里抽出来。 |
| 具体步骤 | 1. 在 `Core/AngelscriptType.h` 或 `ClassGenerator/` 新增 `FAngelscriptCallSlotPlan`，显式描述 `ReadFromStack`、`WriteReturn`、`NeedsTempConstruct`、`NeedsCopyBack`、`CanUseOptimizedThunk`、`WorldContextPolicy` 等调用行为。 2. 让 `FAngelscriptTypeUsage` 或新的 `PropertySignature` 生成 `CallSlotPlan`；`FinalizeArguments()` 只消费 plan，不再直接按 `IsObjectPointer()` / `IsPrimitive()` / `GetValueSize()` 做开放式 `if-else` 分类。 3. 保留现有 `EArgumentParmBehavior` / `EArgumentVMBehavior` 作为兼容优化 tag，但只在 plan 明确命中“旧快路径形状”时才赋值；否则走统一 generic marshaller。 4. 把 `AllocateFunctionFor()` 改成“按 plan 挑是否可用 `UASFunction_NoParams` / `UASFunction_DWordArg` / `UASFunction_ObjectReturn` 等优化类”，而不是让类型家族直接决定 thunk 子类。 5. 第一阶段先用 `FScriptInterface`、`TArray<T>` 和 delegate 返回值验证新 plan，确保 P10 的 interface 参数/返回值可以增量接入，而不必继续增加新的 `UASFunction_*` 分支。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` |
| 预估工作量 | L |
| 架构风险 | 迁移期如果 `CallSlotPlan` 和旧 `EArgument*Behavior` 双轨不一致，最容易出现“generic path 正确但 optimized thunk 仍按旧假设读写参数”的隐性 ABI 回归；必须先让 plan 驱动旧 tag 生成，而不是双写两套逻辑。 |
| 兼容性 | 对现有 Angelscript 语法、已生成 `UFunction` 和现有 `UASFunction_*` 类可以保持向后兼容；首阶段只是把 ABI owner 收敛到 plan 层，不要求脚本作者修改函数签名。 |
| 验证方式 | 1. 回归现有 `NoParams`、单 primitive 参数、单 ref 参数、对象返回的 optimized thunk 与 JIT thunk。 2. 增加 container / delegate / interface 参数与返回值 roundtrip，用 `ProcessEvent`、Blueprint 调用和直接脚本调用三条路径比对。 3. 做一次 `UASFunction::FinalizeArguments()` manifest dump，确认新类型接入时只新增 plan，不需要再增加新的 thunk 子类。 |

### Arch-TS-18：引用跟踪依赖 `RequiresProperty` 与 `ReferenceSchema` 双轨物化，reference-bearing type family 需要同时接入 GC 和 class materialization

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 引用型类型的 owner、GC 集成与新增容器/interface family 的扩展面 |
| 当前设计 | 当前 TypeSystem 不只要回答“能不能建 `FProperty`”，还要负责“是否必须物化 hidden `UPROPERTY`”和“若不物化，如何向 `UASClass::ReferenceSchema` 递归发射 GC schema”。这两个决策都依赖 `FAngelscriptType` 上的布尔合同与 `EmitReferenceInfo()`。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:167`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:170`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:179`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:182`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:185` — `HasReferences()`、`FGCReferenceParams`、`EmitReferenceInfo()`、`NeverRequiresGC()`、`RequiresProperty()` 全都挂在同一类型虚表上。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h:34` — 最终 GC 结果被缓存进 `UASClass::ReferenceSchema`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:298`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:306`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:308` — hidden property 是否生成，直接取决于 `NeverRequiresGC()` / `RequiresProperty()`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4875`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4910`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4914`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4924` — 未物化成 `FProperty` 的脚本成员若 `HasReferences()`，就必须手工递归发射 `ReferenceSchema`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp:61`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp:66`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp:82` — `TArray` 自己递归 inner subtype 的引用信息。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp:70`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp:77`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp:89`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp:97` — `TMap` 需要分别处理 key/value 两条引用链。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSet.cpp:67`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSet.cpp:74`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TOptional.cpp:29`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TOptional.cpp:34`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UStruct.cpp:149`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UStruct.cpp:166` — set/optional/script struct 也各自实现引用递归与 schema 发射。 |
| 优点 | script-only 成员即使不落成真实 `UPROPERTY`，仍能通过 `ReferenceSchema` 进入 UE GC，可兼容当前脚本对象布局和 hot reload 方案。 |
| 不足 | 新增一个 reference-bearing type family，通常要同时回答三件事：是否强制 hidden `UPROPERTY`、是否递归进 `ReferenceSchema`、运行时引用对象谁来持有。当前这些 owner 没有分层，因此 `FInterfaceProperty` / `TScriptInterface<>` 之类新 family 不能只补桥接代码，还要同步接入 class materialization 与 GC schema。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FLuaEnv` 启动时显式创建 `ObjectRegistry`、`ClassRegistry`、`ContainerRegistry`、`PropertyRegistry`；interface 值桥接由 `FInterfacePropertyDesc` 完成，array/set/map 则由 `ContainerRegistry` 缓存和复用。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:104`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:105`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:110`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:111`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:533`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:559`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ContainerRegistry.cpp:34`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ContainerRegistry.cpp:58`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ContainerRegistry.cpp:82`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:265` | 引用型值与容器实例的 owner 在 env registry，而不是压进动态 class shell 的 GC schema 生成规则。 |
| puerts | `FunctionTranslator` / `ObjectMerger` 都是按 `FPropertyTranslator::Create()` 取得 property translator；`FInterfacePropertyTranslator` 直接处理 `FScriptInterface`，对象持有由 `IObjectMapper` 管理。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp:131`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp:135`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:598`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:615`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:629`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1390`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.h:389` | 引用 owner 由 translator + object mapper 组合管理，class wrapper 只消费它们，不需要自己构建一套递归 GC schema。 |
| UnrealCSharp | `FCSharpEnvironment` 初始化时把 `ClassRegistry`、`ReferenceRegistry`、`ObjectRegistry`、`ContainerRegistry`、`MultiRegistry` 全部挂成环境服务；`FInterfacePropertyDescriptor` 和 `FRegisterScriptInterface` 通过 `AddMultiReference<TScriptInterface<IInterface>>` / `GetMulti()` 管理 interface 值。 | `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp:65`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp:67`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp:69`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp:73`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp:77`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/ObjectProperty/FInterfacePropertyDescriptor.cpp:8`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/ObjectProperty/FInterfacePropertyDescriptor.cpp:22`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/ObjectProperty/FInterfacePropertyDescriptor.cpp:29`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Domain/InternalCall/FRegisterScriptInterface.cpp:13`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Domain/InternalCall/FRegisterScriptInterface.cpp:20`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FContainerRegistry.cpp:13` | interface/object/container 引用都先有独立 runtime owner，再由 property descriptor 接进去；新增 family 时不必同时修改 hidden-property 决策和 per-class schema 构建。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先把“引用如何被 UE GC 看见”和“引用值运行时由谁持有”抽成独立 `ReferenceBridge` 层，让 `UASClass::ReferenceSchema` 退化为编译后的 cache，而不是 reference family 的唯一 owner。 |
| 具体步骤 | 1. 在 `Core/` 或 `ClassGenerator/` 新增 `FAngelscriptReferencePlan` / `IAngelscriptReferenceBridge`，显式区分 `NoReference`、`HiddenPropertyRequired`、`CustomSchema`、`RuntimeRegistryOwned` 四类策略，并允许容器递归子 plan。 2. 让 `AngelscriptClassGenerator.cpp` 在 hidden property 判定时先问 `ReferencePlan`，不再直接散落读取 `NeverRequiresGC()` / `RequiresProperty()` / `HasReferences()` 三个布尔。 3. 保留 `UASClass::ReferenceSchema`，但只作为 `ReferencePlan -> FSchemaView` 的编译产物；array/map/set/optional/script struct 先迁移到 plan builder，避免每个 binder 继续直接操作 `FGCReferenceParams`。 4. 为 P10 的 `FInterfaceProperty` / `TScriptInterface<>` 新建一个 `InterfaceReferenceBridge`：运行时 owner 先走 registry/handle，只有在脚本成员必须进入 UE class shell 时才决定是否生成 hidden property 或 schema 节点。 5. 增加专门回归：script class 的 script-only object reference、`TArray<UObject>`、`TMap<Key, UObject>`、`TOptional<Interface>`、reload 后 reference schema 仍稳定；验证新增 family 不再需要同时改 hidden-property 规则和容器 binder。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSet.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TOptional.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UStruct.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` |
| 预估工作量 | M |
| 架构风险 | 如果 `ReferencePlan` 与旧 `ReferenceSchema` 构建并存但没有单点 owner，最容易出现“GC schema 新路径已迁，hidden property 仍按旧布尔规则生成”的双轨问题；第一阶段必须让 plan 反向生成旧布尔结果，避免两套规则漂移。 |
| 兼容性 | 对现有脚本字段声明与运行时 GC 语义可以保持向后兼容；首阶段只重排 owner，不要求用户修改 `UPROPERTY` 暴露或脚本容器写法。 |
| 验证方式 | 1. 为 script-only 引用成员做 GC 回收回归，确认 hidden property 与 custom schema 结果和迁移前一致。 2. 增加 container/interface 引用图测试，验证 `TArray`、`TMap`、`TSet`、`TOptional` 与未来 `TScriptInterface<>` 都能稳定被 GC 访问。 3. 在 hot reload 后对比 `ReferenceSchema` member 数和 debug path，确认 plan layer 没有引入重复或遗漏的 schema 节点。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P0 | Arch-TS-17 | `UASFunction` 的 ABI owner 固化在行为枚举和特化 thunk，新增 type family 难以只在 TypeSystem 内闭环 | 调用桥接收敛 | 高 |
| P1 | Arch-TS-18 | reference-bearing family 同时耦合 hidden property 决策与 `ReferenceSchema` 发射 | 引用 owner 解耦 | 高 |

---

## 架构分析 (2026-04-08 16:58)

### Arch-TS-19：`ScriptPropertyOffset` 已演变成 TypeSystem 到动态 `UClass` 布局的隐式 ABI，新增类型映射会连带撬动对象尾部布局与组件装配

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 类型桥接结果如何固化进动态 `UClass` / `UObject` 内存布局 |
| 当前设计 | 当前管线不是“把 `FAngelscriptTypeUsage` 转成 `FProperty` 就结束”，而是把 AngelScript 侧的 property offset 继续写进 `PropertyDesc->ScriptPropertyOffset`，再让 `UASClass`、reload、默认组件和 override 组件都共享这条 offset ABI。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h:24` — `UASClass` 持有 `ScriptPropertyOffset` 作为脚本尾部起点。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:361`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:374`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:376` — 分析脚本类属性时直接从 `ScriptType->GetProperty(...)` 读取 `PropertyOffset`，并写入 `PropertyDesc->ScriptPropertyOffset`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3666`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3673`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3678` — 物化类时同时设置 `ContainerSize`、`PropertiesSize` 和 `NewClass->ScriptPropertyOffset = ParentCodeClass->GetPropertiesSize()`，说明脚本类型映射已经决定了动态类尾部布局。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4821`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4822` — reload 销毁旧脚本对象时按 `Object + ASClass->ScriptPropertyOffset` 整段清零。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:5231`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:5332` — actor 默认组件和 override 组件把 `Property->ScriptPropertyOffset` 继续当成实例变量偏移持久化。 |
| 优点 | 脚本对象、默认组件、hot reload 和 `ClassGenerator` 可以共享同一套脚本尾部地址空间，不必为 script-only member 再额外维护一层 indirection。 |
| 不足 | 新增一种映射如果改变 `GetValueSize()`、`GetValueAlignment()`、`RequiresProperty()` 或“是否落成 hidden `UPROPERTY`”，影响的不只是 property bridge，还会波及 `UASClass` 尾部布局、reload 清理区间、默认组件变量偏移和后续调试/GC 路径。换句话说，类型系统和对象布局已经通过裸 offset 强耦合。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FLuaEnv` 启动后把 object/class/container/property 都挂进 env registry；`FPropertyRegistry` 直接按 `UField*` 创建 `FPropertyDesc`，`ContainerRegistry` 只缓存 `FScriptArray/FScriptSet/FScriptMap` 包装器，没有额外 script-tail offset。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:104`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:105`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:110`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:111`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/PropertyRegistry.cpp:405`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/PropertyRegistry.cpp:406`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ContainerRegistry.cpp:58`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ContainerRegistry.cpp:82` | 桥接 owner 以 `FProperty*`/registry 为中心，容器与对象包装的生命周期不依赖“脚本对象尾部偏移”这条 ABI。 |
| puerts | `ObjectMerger` 构造时直接遍历 `TFieldIterator<PropertyMacro>`，把每个字段映射成 `FPropertyTranslator`；translator 工厂和 interface translator 都围绕真实 `FProperty` / `FScriptInterface` 工作，而不是基于自定义对象尾部布局。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.h:385`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.h:389`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:598`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:608`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:627`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:629`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1390` | 字段 bridge 和对象包装都以 UE 反射字段为单一真相，新增 interface/container family 不需要同步修改“脚本对象尾部”消费者。 |
| UnrealCSharp | 环境启动时先创建 `ClassRegistry`、`ObjectRegistry`、`ContainerRegistry` 等服务；类描述符按 `FPropertyDescriptor::Factory(InProperty)` 建 descriptor，工厂内部再区分 `FInterfaceProperty`、`FArrayProperty`、`FMapProperty`、`FSetProperty`。 | `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp:65`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp:69`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp:73`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Class/FClassDescriptor.cpp:88`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Class/FClassDescriptor.cpp:92`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/FPropertyDescriptor.cpp:47`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/FPropertyDescriptor.cpp:87`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/FPropertyDescriptor.cpp:91` | 动态类描述和 property family 分层明确，descriptor/registry 可以演进而不把对象实例布局暴露成跨模块共享 ABI。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把“脚本成员布局”从 `FAngelscriptTypeUsage` 的副作用中单独抽成 `ScriptLayoutPlan`，让类型桥接只声明存储需求，不再直接把裸 offset 传播给组件装配和 reload。 |
| 具体步骤 | 1. 在 `ClassGenerator/` 新增 `FAngelscriptScriptLayoutPlan`，统一产出 `PropertyName -> {StorageKind, ScriptOffset, NativeProperty, Alignment, Size}` 表；首阶段允许 `PropertyDesc->ScriptPropertyOffset` 继续作为该 plan 的 cache 字段。 2. 把 `FinalizeActorClass()`、`FinalizeComponentClass()`、`DestructScriptObject()`、`CreateDebugValuePrototype()` 等 offset 消费方改成先查 layout plan，再读取 offset，避免后续新增 storage kind 时继续散落裸偏移。 3. 为 script-only member 与 hidden `UPROPERTY` member 明确区分 `StorageKind::ScriptTail` / `StorageKind::ReflectedProperty`，让新增 `TScriptInterface<>`、新容器或未来 handle-based type family 时，只扩 plan builder，不必继续修改每个 offset 消费方。 4. 保留 `UASClass::ScriptPropertyOffset` 作为向后兼容壳，仅表示“脚本尾部起始位置”；默认组件和 override 组件改存 `PropertyName + StorageKind`，在实例化时通过 layout plan 求值。 5. 增加 hot reload 回归：新增/删除容器字段、把字段从 script-only 迁到 hidden `UPROPERTY`、actor 默认组件字段换类型，确认 layout plan 能稳定重建旧对象尾部清理区和组件变量寻址。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` |
| 预估工作量 | M |
| 架构风险 | 迁移期最容易出现“旧代码仍直接读 `ScriptPropertyOffset`，新代码已改读 layout plan”的双轨偏移源；第一阶段必须让 plan 反向生成旧字段，避免对象构造与 reload 清零区间不一致。 |
| 兼容性 | 对现有脚本语法和类定义保持向后兼容；首阶段只重排 C++ 内部 owner，并保留 `ScriptPropertyOffset` 兼容字段。 |
| 验证方式 | 1. 新增脚本类字段布局快照测试，记录每个字段的 `StorageKind/ScriptOffset/Size/Alignment`。 2. 回归 actor 默认组件、override 组件和 hot reload，确认变量偏移解析一致。 3. 对新增 `TArray` / `TOptional` / 未来 `TScriptInterface<>` 字段做对象构造、reload、debug value 三条路径验证。 |

### Arch-TS-20：`AngelscriptUHTTool` 仍以头文件文本匹配恢复函数签名，并对 interface 直接落 `ERASE_NO_FUNCTION()`，P10 的 `UInterface` 支持无法复用结构化 type metadata

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | UHT sidecar 是否产出可复用的结构化类型元数据，尤其是 interface 与复杂包装类型 |
| 当前设计 | 现有 `AngelscriptUHTTool` 产出物的核心仍是 “类名 + 函数名 + erase macro”。signature 解析优先走头文件文本解析；对 `Interface` / `NativeInterface` 则直接写 `ERASE_NO_FUNCTION()`，没有保留可供运行时复用的 interface/property family 描述。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:449`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:455`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:466`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:468`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:470`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:476`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:479` — `CollectEntries()` 对 interface/nativeinterface 直接生成 `ERASE_NO_FUNCTION()`，其余函数也只留下 erase macro。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs:43`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs:52`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs:57`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs:68`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs:83`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs:86`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs:109`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs:116` — signature builder 失败时按白名单 fallback；参数类型通过 `AppendFullDecl()` 拼文本，返回值通过 `TypeTokens.ToString()` 拼文本。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs:28`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs:35`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs:42`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs:70`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs:85`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs:92`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs:171`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs:177`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs:180`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs:249` — resolver 先清洗头文件文本，再用字符串归一化比较类型，比较时会去掉 `const`、`&` 和空白。 |
| 优点 | 现有实现可以在不深度侵入 UHT 内部模型的情况下快速为大量 BlueprintCallable 函数生成 function table，普通非重载函数的落地成本低。 |
| 不足 | sidecar 输出没有“参数/返回值的结构化 type graph”，只有 erase macro 和少量文本推断。结果是 `UInterface` 在生成阶段就被降级成 stub，复杂包装类型仍依赖文本比较和白名单 fallback，运行时 `TypeSystem`/`ClassGenerator` 无法直接复用 UHT 的结构化结果。对 P10 来说，这意味着 interface 支持需要同时修改 UHT 字符串推断、runtime type bridge 和 class reload 特例。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 编辑器侧 IntelliSense 直接遍历 `UFunction`，区分是否包含 interface 函数；属性类型推导直接按 `FInterfaceProperty` 输出 `TScriptInterface<...>`，不经过头文件文本回推。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSense.cpp:150`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSense.cpp:162`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSense.cpp:392`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSense.cpp:395` | 即便只是工具链输出，也应该优先消费 UE 反射对象，而不是二次解析头文件文本。 |
| puerts | `DeclarationGenerator` 的 `GenTypeDecl()` 直接递归处理 `Array/Set/Map/Interface` property；类声明生成时显式遍历 `Class->Interfaces` 把接口函数并入 d.ts。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:741`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:827`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:836`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:845`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:891`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:893`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1311`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1313` | codegen 阶段先保留结构化类型树和 interface 枚举关系，运行时 wrapper 只消费结果，不需要再靠 header 文本归一化补洞。 |
| UnrealCSharp | `FGeneratorCore::GetPropertyType()` 直接按 `FInterfaceProperty` / `FArrayProperty` 等 property family 生成宿主类型名；`FClassGenerator` 在生成类声明时显式把 `InClass->Interfaces` 和接口函数并进主链。 | `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:144`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:147`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:158`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:163`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp:118`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp:122`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp:257`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp:264` | 生成器用 property graph 和 interface graph 作为一等输入，`UInterface` 不会在 codegen 阶段先被抹成空洞的 stub。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 保留现有 function table 输出，但新增一份结构化 `TypeManifest`/`SignatureManifest`，让 UHT sidecar 至少把 interface、container、wrapper family 的类型图完整导出。 |
| 具体步骤 | 1. 在 `AngelscriptUHTTool` 内新增 `AngelscriptExportedTypeRef` / `AngelscriptExportedFunctionSignature` 模型，字段至少包含 `Kind`、`PropertyClass`、`Inner`、`Key`、`Value`、`InterfaceClass`、`bConst`、`bReference`。 2. `CollectEntries()` 遇到 `Interface` / `NativeInterface` 时不再只写 `ERASE_NO_FUNCTION()`，而是额外写出 `DispatchKind = InterfaceStub` 的结构化签名记录；erase macro 仍保留，保证现有 direct bind 路径兼容。 3. `FunctionSignatureBuilder` 停止把结构化信息压平为单纯字符串比较；头文件解析只作为“链接符号恢复”补充，不能再承担 type graph 真相。 4. 在运行时新增一个只读 manifest loader，让 `Bind_BlueprintEvent`、`ClassGenerator` 或未来 P10 的 interface/property adapter 可以查询 UHT 已知的参数/返回值 family，而不是再自己重新推断。 5. 增加专门回归：`UInterface` 函数、`TArray<TScriptInterface<...>>`、`TMap<Key, Value>`、`const TArray<int32>&`、`TOptional<Struct>`；验证生成出的 manifest 能稳定区分 `const/ref/container/interface`。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` |
| 预估工作量 | M |
| 架构风险 | 如果第一阶段就试图让 runtime 强依赖新 manifest，会把现有 UHT 生成闭环全部拉进变更面；更稳妥的路径是先“并行产出 manifest + 保留旧 erase macro”，让 runtime 先只读新资产。 |
| 兼容性 | 对现有脚本 API 与已生成的 function table 可保持向后兼容；首阶段只是新增 sidecar 资产，不要求用户修改脚本或 C++ 头文件写法。 |
| 验证方式 | 1. 为 UHT 工具增加 snapshot 测试，校验 interface/container/wrapper family 会产出结构化记录。 2. 回归现有 `AS_FunctionTable_*.cpp` 生成结果，确认旧 bind 路径不回归。 3. 为 P10 准备一组 interface 签名用例，验证 runtime 可从 manifest 读取 `TScriptInterface<>`、`const&` 和 nested container 信息。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P0 | Arch-TS-20 | UHT sidecar 对 `UInterface` 与复杂包装类型缺少结构化 type metadata | 工具链补强 | 高 |
| P1 | Arch-TS-19 | `ScriptPropertyOffset` 作为跨模块共享 ABI，放大新增类型映射的布局连锁影响 | owner 解耦 | 高 |

---

## 架构分析 (2026-04-08 17:09)

### Arch-TS-21：`CreateProperty()` 之后仍有一层写死在 `ClassGenerator` 的 property decoration pass，新 wrapper family 不能只在 TypeSystem 内闭环

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `CreateProperty()` 之后的 property 后处理 owner，以及新增 wrapper/container family 的落地半径 |
| 当前设计 | 当前实现允许 binder 先产出 `FProperty` 树，但真正决定 `CPF_ContainsInstancedReference`、`EditInline`、`PersistentInstance` 冒泡方式的逻辑并不在 type adapter 里，而是集中写在 `FAngelscriptClassGenerator::AddClassProperties()` 的 concrete-property 分支里。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TOptional.cpp:115`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TOptional.cpp:123` — `TOptional` 已能在 type binder 内创建 `FOptionalProperty`，并递归创建 inner property。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2886`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2897`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2904` — property decoration 依赖 `ApplyInstancedPropertyFlags()` 这种 materialization 阶段 helper。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3041`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3053`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3073`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3084`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3091` — 后处理只显式覆盖 `FArrayProperty`、`FMapProperty`、`FSetProperty`、`FStructProperty` 和“其他”分支，没有针对 `FOptionalProperty` 或未来 wrapper family 的递归入口。 |
| 优点 | UE editor/runtime 相关的 property flag 修饰集中在 class materialization 阶段，早期 binder 不必理解全部 `CPF_*` 细节。 |
| 不足 | 这让“新增类型映射”无法停留在 `FAngelscriptType::CreateProperty()`：即便 binder 已能创建 `FOptionalProperty` 或未来的 `FInterfaceProperty` wrapper，只要它需要递归传播 instanced/reference/editor 语义，就还要继续改 `AngelscriptClassGenerator.cpp`。对 P10 而言，`TOptional<TScriptInterface<...>>`、新 wrapper family 或 future handle/property family 都会撞到同一接缝。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | container descriptor 在创建时就递归持有 inner/key/value descriptor；`FPropertyDesc::Create()` 统一按 property family 分派，不再依赖第二层 class generator 后修饰。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:734`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:738`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:886`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:890`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1024`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1028`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1537`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1615` | wrapper/container 语义跟着 descriptor 树走，新增 family 时不用再补第二套 materialization switch。 |
| puerts | `FInterfacePropertyTranslator`、`FScriptArrayPropertyTranslator`、`FScriptSetPropertyTranslator`、`FScriptMapPropertyTranslator` 直接持有各自 property family 的行为；family 选择收敛在 `PropertyTranslatorCreator`。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:598`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:608`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:862`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:912`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:961`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1225` | 扩展点在 translator/factory，本体只消费 `FProperty` 树；没有“先创建 property 再去别处补语义”的第二条 owner 链。 |
| UnrealCSharp | `FPropertyDescriptor::Factory()` 把 `FInterfaceProperty`、`FArrayProperty`、`FMapProperty`、`FSetProperty`、`FOptionalProperty` 都纳入同一 factory；`FGeneratorCore::GetPropertyType()` 再递归下钻 inner/key/value/value-property。 | `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/FPropertyDescriptor.cpp:47`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/FPropertyDescriptor.cpp:87`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/FPropertyDescriptor.cpp:91`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/FPropertyDescriptor.cpp:123`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/FPropertyDescriptor.cpp:125`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/FPropertyDescriptor.cpp:130`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:144`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:158`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:231`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:245`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:266` | family 递归和 wrapper 扩展都落在结构化 property graph 上，`Optional`/`Interface` 不需要额外敲 class generator 的 hardcoded branch。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 `CreateProperty()` 之后的 decoration 从 `AngelscriptClassGenerator.cpp` 的 concrete switch 抽成可复用的 `PropertyMaterializationPlan` / recursive property walker，让 wrapper family 通过同一条后处理通路声明自己的 inner 语义。 |
| 具体步骤 | 1. 在 `Core/AngelscriptType.h` 或 `ClassGenerator/` 新增 `FAngelscriptPropertyMaterializationPlan`，至少描述 `bPropagateInstancedReference`、`bNeedsEditInlineMeta`、`ChildProperties`；首阶段允许 plan 从已创建的 `FProperty` 树推导。 2. 把 `BubbleUpInstanceReferenceFlags()` 与 `ApplyInstancedPropertyFlags()` 从 `AddClassProperties()` 提炼成一个递归 walker，先覆盖 `FArrayProperty/FMapProperty/FSetProperty/FStructProperty/FOptionalProperty` 五类。 3. 保留当前 `3041-3098` 分支一轮作为 fallback，但让 `Bind_TOptional.cpp` 的 `FOptionalProperty` 先接入新 walker，验证 `TOptional<UObject>`、`TOptional<StructWithInstancedRef>`、`TOptional<TScriptInterface<...>>` 不再需要额外改 generator。 4. 第二阶段把 `AddFunctionArgument()` / `AddFunctionReturnType()` 也切到同一 walker，避免函数参数 property 与类成员 property 的 decoration 继续分叉。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TOptional.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` |
| 预估工作量 | M |
| 架构风险 | 迁移期如果新 walker 与旧 hardcoded 分支同时生效，最容易出现重复设置 `CPF_ContainsInstancedReference` / metadata 的差异；第一阶段应只让 `FOptionalProperty` 走新通路，其他 family 保持旧逻辑做对照。 |
| 兼容性 | 对现有脚本语法和已存在的 property family 保持向后兼容；首阶段只是把内部 owner 从 generator concrete switch 挪到统一后处理器。 |
| 验证方式 | 1. 为动态类生成增加 property flag snapshot，覆盖 `TArray`、`TMap`、`TSet`、`TOptional` 和 object/interface inner property。 2. 回归 editor 侧 `EditInline`、`ContainsInstancedReference` 和 `PersistentInstance` 元数据。 3. 为 P10 预加 `TOptional<TScriptInterface<...>>` 原型测试，确认新增 interface wrapper 时不需要再改 `AddClassProperties()` 分支。 |

### Arch-TS-22：默认值仍由 `FAngelscriptType` 做双向字符串翻译，function signature 与 codegen 没有共享的 typed default pipeline

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 默认值在 `Unreal metadata ↔ Angelscript declaration ↔ runtime UFunction` 三段之间如何流动，以及新增类型映射的改动面 |
| 当前设计 | 当前默认值合同直接挂在 `FAngelscriptType` 虚表上：builder 需要从 Unreal 字符串转成 Angelscript 文字，materializer 又要把 Angelscript 默认值转回 `CPP_Default_*` metadata；缺少一份可复用的结构化 default literal 表示。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:267`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:271`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:274` — `FAngelscriptType` 直接暴露 `DefaultValue_UnrealToAngelscript`、`DefaultValue_AngelscriptToUnreal`、`DefaultValue_AngelscriptFallback`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:570`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:595`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:601`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:622` — `BuildFunctionDeclaration()` 逐参数调用这些虚函数，把 Unreal 默认值转回 Angelscript declaration。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3995`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3999`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4003` — 动态 `UFunction` 生成时又反向调用 `DefaultValue_AngelscriptToUnreal()` 写 `CPP_Default_*` metadata。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:218`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:238`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1690`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1700`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1952`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1962`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:2248`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:2258` — `UObject`、`TSubclassOf`、`TObjectPtr`、`TWeakObjectPtr` 各自重复实现 `null/nullptr` 的双向文字翻译。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs:558`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs:638` — UHT 侧在比对签名时先 `StripDefaultValue()`，说明工具链没有保留可供 runtime 复用的结构化默认值。 |
| 优点 | 现有实现简单直接，某个 binder 只要自己知道文字格式，就能让该类型参与声明生成和 metadata 回填。 |
| 不足 | 代价是新增 family 往往要补一对甚至三套字符串规则，而且这些规则横跨 bind declaration、runtime materialization、UHT header 比对。对 P10 而言，`TScriptInterface<>` 一旦支持默认 `null`、wrapper 组合默认值或未来 `Optional`/`Interface` literal，都会继续扩散成“再给每个 family 写一组字符串转换”。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 默认参数不挂在 property descriptor 虚表上，而是先由生成物 `DefaultParamCollection` 把默认值做成 typed collection；`ClassDesc` 构建 `FFunctionDesc` 时注入该 collection，调用时按 property descriptor 直接拷贝值。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/DefaultParamCollection.cpp:19`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/DefaultParamCollection.cpp:27`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/ClassDesc.cpp:138`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/ClassDesc.cpp:139`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:323`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:329` | 默认值 owner 可以是独立资产/collection，运行时 property bridge 只消费 typed value，不必让每个类型桥接器都做双向字符串翻译。 |
| puerts | `DeclarationGenerator` 直接读取 `CPP_Default_*` metadata 决定参数是否 optional，并把默认值仅作为声明注释输出；类型递归由 `GenTypeDecl()` 负责，默认值不侵入 translator。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:741`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:827`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:891`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:996`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1000`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1043` | 类型映射和默认值展示是两层 owner；新增 property family 不需要同时改 runtime translator 与默认值 serializer。 |
| UnrealCSharp | `FClassGenerator` 在 codegen 层读取 `CPP_Default_*` metadata，再按 `FProperty` family 生成宿主语言默认值；`FPropertyDescriptor` factory 本身不承担默认值字符串翻译。 | `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp:906`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp:908`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp:945`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp:954`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp:1172`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp:1247`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp:1280`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp:1289` | 即便仍有 property-family 分支，默认值 owner 也局限在 generator/codegen 层，没有反向渗入 runtime type bridge 和 property descriptor。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 新增一个结构化 `FAngelscriptDefaultLiteral`/`DefaultValueDescriptor` 层，把默认值从类型虚表的双向字符串翻译中抽出来；type family 只在必要时提供 parser/serializer fallback。 |
| 具体步骤 | 1. 在 `Core/` 或 `AngelscriptUHTTool/` 新增 `FAngelscriptDefaultLiteral`，至少区分 `None`、`Null`、`Bool`、`Number`、`String`、`Enum`、`WorldContextSentinel`、`ExpressionFallback`，并显式保存原始 metadata 文本。 2. 让 `BuildFunctionDeclaration()` 先通过统一 helper 把 Unreal metadata 解析为 `DefaultLiteral`，再序列化为 Angelscript declaration；首批覆盖 primitives、enum、object-like family、`__WorldContext()`。 3. 让 `AddFunctionArgument()` 反向消费同一个 `DefaultLiteral` 写回 `CPP_Default_*`，把 `UObject/TSubclassOf/TObjectPtr/TWeakObjectPtr` 的 `null/nullptr` 规则收敛成一个 object-like serializer；旧 `DefaultValue_*` 虚函数保留为 fallback。 4. 在 `AngelscriptHeaderSignatureResolver` 旁边新增 default-literal capture，而不是继续在比对阶段直接 `StripDefaultValue()`；先只记录 literal，不改变现有 header 匹配逻辑。 5. 第二阶段为 P10 增加 `TScriptInterface<>` 与 `TOptional<TScriptInterface<...>>` 的 `Null` literal 支持，避免 interface family 再复制一套默认值字符串转换。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` |
| 预估工作量 | M |
| 架构风险 | 默认值文字格式一旦改成统一 serializer，最容易影响现有 declaration 输出与 `CPP_Default_*` snapshot；第一阶段必须保留旧虚函数 fallback，并对生成声明做 before/after 对比。 |
| 兼容性 | 对现有脚本函数签名与 Blueprint metadata 保持向后兼容；首阶段只是统一 owner，不要求用户改脚本默认值写法。 |
| 验证方式 | 1. 为 `BuildFunctionDeclaration()` 增加 snapshot，覆盖 `nullptr`、`__WorldContext()`、enum、string、number。 2. 为动态 `UFunction` 生成增加 roundtrip 测试，确认 `CPP_Default_* -> Angelscript declaration -> CPP_Default_*` 一致。 3. 加一组 P10 预备用例：`TScriptInterface<>`、`TOptional<TScriptInterface<...>>`、object wrapper family 的 `null` 默认值，确认不再需要为每个 binder 单独补 `DefaultValue_*`。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-TS-21 | property decoration 仍硬编码在 `ClassGenerator`，新增 wrapper family 不能只改 TypeSystem | owner 收敛 | 高 |
| P2 | Arch-TS-22 | 默认值依赖类型对象做双向字符串翻译，type family 与 codegen 耦合偏深 | 合同抽象 | 中 |

---

## 架构分析 (2026-04-08 17:20)

### Arch-TS-23：`asITypeInfo::plainUserData` 默认槽位已被复用成跨 TypeSystem / 容器 / StaticJIT 的共享总线，新增 metadata owner 容易互相踩踏

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 类型元数据放置位置，以及新增 type family / 工具链元数据时的可并存性 |
| 当前设计 | AngelScript 原生其实支持 `SetUserData(data, type)` 的 typed slot，但当前仓库大部分类型桥接仍占用 `type == 0` 的默认槽位，甚至直接读写 `plainUserData`；同一条槽位同时承载 `UClass/UStruct/UEnum/UDelegateFunction`、容器操作表、delegate sentinel 和 StaticJIT 假设。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_typeinfo.cpp:137`、`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_typeinfo.cpp:178`、`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_typeinfo.h:137`、`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_typeinfo.h:183` — AngelScript 的 `asCTypeInfo` 同时支持 `plainUserData` 与 typed `userData` 数组。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2590`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2610`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2701` — 动态 class/interface/struct materialize 后都把 UE 反射对象写回默认 `SetUserData(...)`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:668` — native object type 甚至直接写 `TypeInfo->plainUserData = (SIZE_T)Class`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp:1735`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp:1753`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp:1296`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp:1335`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TOptional.cpp:334`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TOptional.cpp:349`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSet.cpp:730`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSet.cpp:763` — 四个容器族都把 `F*Operations` 缓存在同一个默认槽位。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp:1557`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp:1564`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.h:69` — delegate / multicast delegate 再把这个槽位当成 sentinel 或 `UDelegateFunction*` 存储。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:5759`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.cpp:313`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp:61`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UDataTable.cpp:62` — runtime cast、JIT、gameplay helper 都直接把 `plainUserData` 当成真实 `UClass/UStruct` 解释。 |
| 优点 | 默认槽位读取开销最低，早期实现只要拿到 `asITypeInfo*` 就能常数时间回到 UE 反射对象或容器操作表。 |
| 不足 | 这个设计把“类型身份”“模板操作表”“JIT sentinel”“外部扩展元数据”压进了同一个无命名空间槽位。结果是新增一个 wrapper family、调试元数据或外部扩展插件时，要么继续抢占默认槽位，要么被迫再发明 shadow registry；更糟的是，部分调用点直接依赖 `plainUserData`，让后续迁移到 typed slot 时必须跨 `Binds`、`StaticJIT`、`Core` 一起改。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 类型身份和 property 桥接分别落在 `FClassRegistry` 与 `FPropertyRegistry`；descriptor 创建通过 `FPropertyDesc::Create()` 统一分派，容器 descriptor 再递归持有 inner/key/value descriptor。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:46`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:59`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:90`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:302`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/PropertyRegistry.cpp:20`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/PropertyRegistry.cpp:316`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/PropertyRegistry.cpp:405`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:738`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:890`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1028`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1537` | metadata owner 是 registry / descriptor，不需要把 VM 原生类型对象的单个 userdata 槽位当成全局总线。 |
| puerts | `FPropertyTranslator::Create()` 做统一工厂；`JsEnvImpl` 用 `ContainerPropertyMap` 缓存 `Property -> Translator`，再把 key/value/inner translator 作为独立 internal pointer 挂到 JS container object。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1225`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1390`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:2813`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:2845`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:2885`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/ContainerWrapper.cpp:221`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/ContainerWrapper.cpp:517`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/ContainerWrapper.cpp:568` | 把“谁拥有容器/属性桥接元数据”收敛到 env map 和 wrapper 内部字段，避免多个子系统争抢同一个 runtime slot。 |
| UnrealCSharp | `FPropertyDescriptor::Factory()` 与 `FTypeBridge` 用结构化 reflection registry 处理 `FInterfaceProperty`、`FOptionalProperty`、container 等 family；类型实例通过 `MakeGenericTypeInstance` 组合，而不是写回一个无类型指针。 | `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/FPropertyDescriptor.cpp:47`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/FPropertyDescriptor.cpp:87`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/FPropertyDescriptor.cpp:123`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/FPropertyDescriptor.cpp:130`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:266`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:415`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:426`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:599`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:603` | type family 扩展是“补 descriptor / bridge / registry”问题，而不是“继续往一个裸槽位里塞更多指针”问题。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先把默认 `userData` 槽位从“共享总线”降级成兼容镜像，新增显式 metadata slot / wrapper API，让 TypeBridge、TemplateOps、JIT tag 各有 owner。 |
| 具体步骤 | 1. 在 `Core/` 新增 `FAngelscriptMetadataSlots` 与统一 helper，显式定义至少 `TypeBridge`、`TemplateOps`、`DelegateTag`、`ModuleHash` 四类 slot；对 `asITypeInfo` / `asIScriptModule` / `asIScriptFunction` 暴露 `GetTypedUserData()` / `SetTypedUserData()` 包装。 2. 第一阶段只迁移 type family 侧：`ClassGenerator`、`Bind_BlueprintType.cpp`、`Bind_UStruct.cpp`、`Bind_UEnum.cpp` 把 `UClass/UStruct/UEnum/UDelegateFunction` 改写到 `TypeBridge` slot，同时保留 slot `0` 镜像，保证旧调用点不立刻失效。 3. 第二阶段迁移 `Bind_TArray.cpp`、`Bind_TMap.cpp`、`Bind_TSet.cpp`、`Bind_TOptional.cpp` 到 `TemplateOps` slot，并把 `Bind_AActor.cpp`、`Bind_UDataTable.cpp`、`Bind_Delegates.h`、`StaticJITBinds.cpp` 的 `plainUserData` 直读改成 helper。 4. 第三阶段把 `StaticJIT/PrecompiledData.cpp` 的 delegate sentinel 与 `Core/AngelscriptEngine.cpp` 的 module hash 也迁出默认槽位；新增编码规范，禁止新代码再直接访问 `plainUserData`。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UStruct.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UEnum.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSet.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TOptional.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UDataTable.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.cpp` |
| 预估工作量 | M |
| 架构风险 | 迁移期最容易出现“部分路径已切 typed slot，但 JIT / helper 仍读 slot 0”的双轨不一致；因此必须保留 slot `0` 镜像过渡一段时间，并优先清理所有 `plainUserData` 直读点。 |
| 兼容性 | 对现有脚本语法和已编译脚本保持向后兼容；变更只发生在 Runtime 内部 C++ metadata owner。首阶段保留 slot `0` 镜像，不要求用户修改任何脚本。 |
| 验证方式 | 1. 增加 `AS type -> UE field -> AS type` roundtrip，用同一个 typeinfo 分别验证 `UClass/UStruct/UEnum/delegate` lookup。 2. 回归 `GetAllActors`、`GetComponentsByClass`、`DataTable` 行读取、delegate 广播、StaticJIT cast，确认不再依赖 `plainUserData`。 3. 增加一组 typed-slot coexistence 测试，验证 template type 能同时挂 `TemplateOps` 与扩展 metadata，而不是互相覆盖。 |

### Arch-TS-24：`asIScriptFunction::userData` 单槽同时承载 interface / delegate / reflective payload，callable metadata 仍是临时指针而不是稳定 descriptor

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | callable type metadata 的 owner，以及它对 `UInterface`/反射调用扩展的影响 |
| 当前设计 | 当前 callable metadata 没有统一 registry：`GenericMethod` 绑定、Blueprint event、reflective fallback 和 interface method dispatch 都把各自 payload 塞进 `asIScriptFunction::userData`。更关键的是，interface payload 只有一个 `FName`，typed 参数/返回值在预处理之后就没有稳定 owner 了。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_scriptfunction.h:239`、`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_scriptfunction.h:240`、`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_scriptfunction.cpp:1578` — `asCScriptFunction` 只有一个 `userData` 指针，`SetUserData()` 直接覆盖它。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:285`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:417` — 通用 `GenericMethod` 绑定会把任意 `UserData` 放到这个单槽里。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp:511`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp:812` — Blueprint delegate/event 依赖 `FBlueprintEventSignature*` 作为 function userdata。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp:26`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp:82`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp:397` — reflective fallback 再把完整 `FAngelscriptTypeUsage` 数组和 `UFunction*` 塞进另一种 heap payload。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:59`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:165`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:420` — interface payload 只是 `FInterfaceMethodSignature { FName FunctionName; }`，由 engine 手工维护数组生命周期。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:1123`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:1149`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:1153`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:59` — 预处理阶段给 interface method 塞入这份最小签名，运行时 dispatch 再从 `GetFunction()->GetUserData()` 取回并按名字转发。 |
| 优点 | 早期接入成本低，任何 generic callback 只要附一个指针就能把上下文传到运行时，不必先设计额外 registry。 |
| 不足 | 这条路径的问题不是“只有一个槽位”这么简单，而是 metadata owner 完全按调用来源分裂：同一个脚本函数无法并存多类 payload，interface method 也只能记住 `FunctionName`，不能稳定承载参数类型、返回值、默认值或 future `TScriptInterface<>` 信息。对 P10 来说，这意味着即便补了 `FInterfaceProperty` adapter，interface callable 仍会卡在“没有稳定 typed signature owner”这一层。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `JsEnvImpl` 把 `FFunctionTranslator` 放进 `TsFunctionMap` 和 `JsCallbackPrototypeMap`，运行时一律通过 translator 调 `CallJs()`；函数元数据 owner 是 env map，而不是某个脚本函数对象上的裸指针。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:1367`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:1370`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:1941`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:1956`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:2215`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:2383` | callable metadata 可以随 env 生命周期统一更新、替换和失效处理，不需要每个绑定点自己管理 heap payload。 |
| UnLua | `FClassDesc` 为每个 `UFunction` 保存 `FFunctionDesc`；默认参数由独立 `DefaultParamCollection` 注入，运行时 `FunctionDesc` 直接按 typed parameter descriptor 拷贝值。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/ClassDesc.cpp:138`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/ClassDesc.cpp:139`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:323`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:329`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/DefaultParamCollection.cpp:19`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/DefaultParamCollection.cpp:27` | interface/default-param/callable marshalling 共享一份稳定 descriptor，而不是多个短生命周期 payload。 |
| UnrealCSharp | `FCSharpEnvironment` 启动时创建 `CSharpBind`、`ClassRegistry`、`ObjectRegistry`、`StructRegistry` 等长期服务；`FDynamicInterfaceGenerator::GeneratorFunction()` 直接按 reflection graph 生成 interface callable，不依赖 function userdata。 | `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp:63`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp:65`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp:69`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Public/Registry/FCSharpBind.inl:54`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicInterfaceGenerator.cpp:317` | callable 生命周期与 environment / generator 一起治理，interface 支持可以保留完整签名，而不是在预处理阶段降成 `FName`。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 新增 engine-owned 的 `CallableDescriptorRegistry`，把 function payload 从 `asIScriptFunction::userData` 解耦出来；interface dispatch 先拿到结构化 callable descriptor，再决定如何 materialize / 转发。 |
| 具体步骤 | 1. 在 `Core/AngelscriptEngine.*` 新增 `FAngelscriptCallableDescriptor` 与 registry，至少区分 `GenericBind`、`BlueprintEvent`、`ReflectiveBlueprintCallable`、`InterfaceDispatch` 四种 kind，并统一持有 `UFunction*`、`StaticObject`、结构化参数/返回值描述、默认值/flag 信息。 2. 让 `FAngelscriptBinds::GenericMethod()`、`Bind_BlueprintEvent.cpp`、`BlueprintCallableReflectiveFallback.cpp`、`AngelscriptPreprocessor.cpp` 全部改为注册 descriptor；首阶段可继续在 `userData` 中只放一个稳定 handle/index，旧 payload 作为 fallback。 3. 把 `FInterfaceMethodSignature` 从 `FName` 升级为可复用的函数描述（建议直接复用 `FAngelscriptFunctionDesc` 或其精简版），让 `CallInterfaceMethod()` 不再只靠名字查找，P10 所需的 `TScriptInterface<>`、container、默认值和 const/ref 语义都能沿同一份 descriptor 流动。 4. 在 hot reload / shutdown 时统一清理 descriptor registry，而不是让每个调用点手工 `new/delete` 自己的 signature payload；保留旧 `GetUserData()` 路径一个阶段用于兼容已有绑定。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` |
| 预估工作量 | M |
| 架构风险 | callable descriptor 一旦成为新 owner，最容易影响 generic callback 热路径和 hot reload 清理顺序；如果 registry cleanup 比旧 `userData` fallback 慢一步，可能出现 descriptor 已释放但旧函数仍被调用的问题。 |
| 兼容性 | 对现有脚本调用语法保持向后兼容；首阶段允许 `userData` 与新 registry 并存，逐步把旧 payload 迁空，不要求用户修改脚本或已绑定的 API。 |
| 验证方式 | 1. 为 interface method 增加带参数、返回值、container、default value 的 dispatch 测试，确认不再只靠 `FunctionName`。 2. 回归 Blueprint event、delegate broadcast、reflective BlueprintCallable 三条 generic path，确认都能从新 descriptor 成功取回签名。 3. 增加 hot reload / engine shutdown 测试，验证 callable descriptor 不泄漏、不悬挂，也不会与旧 `userData` fallback 冲突。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P0 | Arch-TS-24 | callable metadata 仍靠 `asIScriptFunction::userData` 临时指针承载，阻碍 `UInterface` typed signature 演进 | owner 重构 | 高 |
| P1 | Arch-TS-23 | `plainUserData` 默认槽位被多种 type metadata 复用，新增扩展 owner 容易互相踩踏 | 元数据分槽 | 高 |

---

## 架构分析 (2026-04-08 17:35)

### Arch-TS-25：动态 `UClass` finalization 仍硬编码成 `Actor/Component/Object` 三条宿主 lane，新宿主族扩展必须同步改 `ClassGenerator` 与 `UASClass`

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 动态 `UClass` 生成对新宿主类型族的扩展面 |
| 当前设计 | 当前 `ClassGenerator` 在类型 materialization 完成后，不是根据可替换的 host policy 执行 finalization，而是直接把动态类分流到 `Actor`、`UActorComponent`、普通 `UObject` 三条固定 lane；`UASClass` 头文件也只暴露三套静态构造入口。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h:108`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h:109`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h:110` — `UASClass` 只定义 `StaticActorConstructor`、`StaticComponentConstructor`、`StaticObjectConstructor` 三个构造入口。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:5201`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:5207` — `FinalizeClass()` 直接用 `IsChildOf(AActor)` / `IsChildOf(UActorComponent)` / `else` 三岔选择 finalizer。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:5219`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:5222`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:5452`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:5464` — `FinalizeActorClass()` 额外挂默认组件/覆盖组件扫描，`FinalizeComponentClass()` / `FinalizeObjectClass()` 只是在这里切换构造器。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:1352`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:1408`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:1461` — 三个静态构造器分别复制“先跑 `CodeSuperClass->ClassConstructor`，再构造脚本对象，再执行 `ConstructFunction`/`DefaultsFunction`”的相似序列。 |
| 优点 | 当前三条 lane 对现有脚本 `Actor`、脚本 `Component`、普通脚本 `Object` 足够直接，调试时也容易沿固定分支追踪构造顺序。 |
| 不足 | 一旦需要引入新的宿主族或新的 non-instantiable shell，改动点不会停在 type binder；必须再动 `FinalizeClass()` 分支、`UASClass` 静态构造入口以及对应 metadata pass。对 `UInterface` 来说，这意味着它很难变成“一个新的 class-kind policy”，只能继续作为旁路特例存在。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | 把 dynamic type generation 拆成 `FDynamicClassGenerator`、`FDynamicInterfaceGenerator`、`FDynamicStructGenerator` 三个 family；class generator 内部再按 `GeneratorProperty` / `GeneratorFunction` / `GeneratorInterface` 分阶段执行，constructor 安装则统一落在 `BeginGenerator()`。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicClassGenerator.cpp:36`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicClassGenerator.cpp:69`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicClassGenerator.cpp:71`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicClassGenerator.cpp:73`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicClassGenerator.cpp:279`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicClassGenerator.cpp:297`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicInterfaceGenerator.cpp:21`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicInterfaceGenerator.cpp:154`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicInterfaceGenerator.cpp:231` | “哪类 dynamic type 在生成” 与 “实例怎么构造” 被显式分层，新增 family 优先是补 generator/policy，而不是继续往一个 `if (Actor) else if (Component)` 表里加分支。 |
| puerts | `UJSGeneratedClass::Create()` 只负责选择 generated class shell，并把脚本 constructor/prototype 塞进 generated class；真正的对象构造在统一 `StaticConstructor()` 里转发给 `DynamicInvoker->JsConstruct()`，函数 override/mixin 也走单独通道。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JSGeneratedClass.cpp:18`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JSGeneratedClass.cpp:33`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JSGeneratedClass.cpp:45`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JSGeneratedClass.cpp:68`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JSGeneratedClass.cpp:82`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JSGeneratedClass.cpp:179`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JSGeneratedClass.cpp:232` | 宿主 class shell 与脚本运行时构造器之间只有一条统一转发 seam，扩新宿主行为时优先补 invoker / generated-class family，而不是复制整条构造序列。 |
| UnLua | `FClassRegistry` 只维护 `UStruct*`/name 到 `FClassDesc` 的类型注册；`FObjectRegistry::Bind()` 只是把现有 `UObject` 包成 Lua table，并不要求为不同宿主族再生成不同的自定义构造 lane。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:53`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:90`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:302`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ObjectRegistry.cpp:113`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ObjectRegistry.cpp:127`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ObjectRegistry.cpp:158` | type registry 和 object wrapper 解耦后，脚本桥接不必为每种宿主类都复制一条动态构造/后处理管线。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把当前三条固定 lane 抽成可注册的 `ClassKindPolicy`，先保留现有 `Actor/Component/Object` 行为不变，再为 `UInterface` 和未来宿主族预留增量入口。 |
| 具体步骤 | 1. 在 `ClassGenerator/` 新增 `FAngelscriptClassKindPolicy` 抽象层，最少拆出 `InstallClassConstructor()`、`FinalizeClassMetadata()`、`PostConstructScriptObject()` 三个阶段。 2. 先把现有逻辑原样迁成 `ActorPolicy`、`ComponentPolicy`、`ObjectPolicy`：`FinalizeActorClass()` 里的默认组件/覆盖组件处理整体搬进 `ActorPolicy`，`StaticActorConstructor` / `StaticComponentConstructor` / `StaticObjectConstructor` 先作为 policy 内部调用的 legacy 实现保留。 3. 让 `FinalizeClass()` 改为从 policy registry 选择策略，而不是继续写死 `IsChildOf(AActor)` / `IsChildOf(UActorComponent)` 分支；registry key 可以先按 `CodeSuperClass` 谓词实现，后续再补 class flag 或 metadata。 4. 增加一个 `NonInstantiableInterfacePolicy` 空壳，只负责 `Bind()` / `StaticLink()` / 方法壳与 metadata finalization，不进入脚本对象构造与 defaults 执行；首阶段即使仍由旧 interface reload lane 驱动，也先统一成“通过 policy 结尾”。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/` |
| 预估工作量 | M |
| 架构风险 | 迁移期最容易出现“policy 已切换，但旧静态构造器里仍隐含假设”的双轨问题，尤其是 `Actor` 默认组件和 `DefaultsFunction` 执行顺序；必须先以不改行为的搬迁为主。 |
| 兼容性 | 对现有脚本类定义、现有 `UASClass` 对外 API 和现有热重载语义可保持向后兼容；首阶段只是把 owner 从 `FinalizeClass()` 分支迁到 policy registry。 |
| 验证方式 | 1. 回归脚本 `Actor`、脚本 `Component`、普通脚本 `Object` 的构造顺序和 defaults 执行顺序。 2. 回归 `DefaultComponent` / `OverrideComponent` 元数据行为，确认迁移到 `ActorPolicy` 后无差异。 3. 增加一个仅走 `NonInstantiableInterfacePolicy` 的实验用 interface 壳测试，确认不触发脚本对象构造。 |

### Arch-TS-26：`ImplementedInterfaces` 仍以裸字符串保存，最终靠 `TypeName/U-prefix/TObjectIterator` 猜接口身份，`UInterface` 集成缺少稳定 identity seam

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `UInterface` 身份解析与实现类接线的稳定性 |
| 当前设计 | 当前仓库在预处理阶段只把继承列表里的 interface 名字存成 `TArray<FString>`；真正到 class finalization 时，才尝试用 script class desc、`FAngelscriptType` 名字表、去掉 `U` 前缀后的 `TObjectIterator<UClass>` 三段式 heuristics 去猜对应的 `UClass*`。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:1105` — `FAngelscriptClassDesc` 的 `ImplementedInterfaces` 只保存 `TArray<FString>`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:800`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:819` — 预处理只是把冒号后的 inheritance token 逐个裁成字符串加入 `ImplementedInterfaces`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:120`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:127` — `FAngelscriptType::GetByAngelscriptTypeName()` 本身也是单纯按名字查 map。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:5062`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:5088`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:5102` — `ResolveInterfaceClass()` 先找 script interface desc，再查 `GetByAngelscriptTypeName()`，最后去掉前导 `U` 后全局枚举 `UClass` 名称。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:5135`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:5138` — 接上实现接口后统一写 `PointerOffset = 0` 和 `bImplementedByK2 = true`，没有额外 identity/context 信息。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:5160`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:5173`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:5177` — 完整性校验也只是按 `FindFunctionByName()` 看是否找得到同名函数。 |
| 优点 | 当前设计不要求先引入新的 descriptor/manifest 系统，native interface 和 script interface 都能先通过同一份文本名字表勉强接上线。 |
| 不足 | interface identity 不是结构化资产，而是 late-bound heuristics：同名 interface、命名空间差异、后续 rename、外部插件提供的 interface family 都会把问题推迟到 finalization 阶段才暴露。对 P10 来说，这意味着即使 method/property bridge 补齐了，interface 的“我到底实现了谁”仍然没有稳定 owner。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | 代码生成阶段直接遍历 `InClass->Interfaces` 上的真实 `UClass*`；运行时 dynamic interface 也由 `DynamicInterfaceMap` / `NamespaceMap` 维护具体生成类，而不是临时猜名字。 | `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp:118`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp:257`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicInterfaceGenerator.cpp:231`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicInterfaceGenerator.cpp:235`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicInterfaceGenerator.cpp:237`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicInterfaceGenerator.cpp:261` | interface identity 先被固化成具体 class/reflection 节点，再进入函数生成和运行时绑定，避免后面再做名字猜测。 |
| puerts | `StructWrapper` 合并接口方法时直接遍历 `Class->Interfaces`；`FInterfacePropertyTranslator` 也直接依赖 `InterfaceProperty->InterfaceClass` 和 `GetInterfaceAddress()`。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:312`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:316`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:608`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:615`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:627`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:629` | interface 的 owner 始终是 `UClass*` / `FInterfaceProperty`，wrapper 层不需要再从字符串回推 identity。 |
| UnLua | `FClassRegistry` 以 `UStruct*` 为主索引注册 reflected type；`FInterfacePropertyDesc` 则直接消费 `InterfaceProperty->InterfaceClass`，并用 `GetInterfaceAddress()` 设置接口指针。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:53`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:90`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:302`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:549`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:556`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:559` | native reflection object 才是 identity 主键；名字只是辅助查找，不是最终 owner。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 `ImplementedInterfaces` 从 `FString` 列表升级成结构化 `InterfaceRef`，在预处理/校验阶段尽早绑定 identity，finalization 不再负责“猜是谁”。 |
| 具体步骤 | 1. 在 `FAngelscriptClassDesc` 中新增 `FAngelscriptInterfaceRef`，至少显式区分 `NativeUClass`、`ScriptInterfaceDesc` 两类，并保存 `FTopLevelAssetPath` 或稳定的 script desc handle；原有 `FString` 字段先保留为兼容 fallback。 2. 在 `AngelscriptPreprocessor.cpp` 解析 inheritance clause 后，立即调用现有 class validation 逻辑尝试解析每个 interface；若同名冲突或无法唯一解析，直接在预处理阶段报错，而不是拖到 `FinalizeClass()`。 3. 让 `FinalizeClass()` 只消费 `InterfaceRef`：native interface 直接拿 `UClass*`，script interface 直接拿 `ClassDesc->Class` 或稳定 descriptor；保留旧的字符串解析路径一个过渡版本，并挂警告。 4. 在方法完整性校验阶段，把当前 `FindFunctionByName()` 升级成“先按 resolved interface owner 找函数，再逐步过渡到 typed signature compare”；这样后续补 `TScriptInterface<>`、命名空间 interface 和外部扩展插件时，不会再受名字猜测牵连。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` |
| 预估工作量 | M |
| 架构风险 | 预处理阶段一旦提前解析 interface identity，最容易暴露历史脚本里依赖模糊名字匹配的隐性用法；过渡期需要保留 legacy fallback 并给出明确 warning。 |
| 兼容性 | 对现有脚本语法保持向后兼容；首阶段允许旧字符串列表与新 `InterfaceRef` 并存，不要求用户改写 `implements` 语法。 |
| 验证方式 | 1. 新增 native interface、script-defined interface、外部插件 interface 三组解析测试，确认预处理阶段就能得到稳定 owner。 2. 回归 rename / 命名空间 / 同名 interface 场景，确认不再落入 `TObjectIterator` 猜测。 3. 回归实现类接口校验，确认方法缺失仍能被报出，但错误信息能指向解析后的具体 interface identity。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P0 | Arch-TS-26 | `ImplementedInterfaces` 仍是字符串 + late-bound heuristics，`UInterface` identity seam 不稳定 | owner 显式化 | 高 |
| P1 | Arch-TS-25 | 动态 `UClass` finalization 仍写死 `Actor/Component/Object` 三条宿主 lane | 策略分层 | 中 |

---

## 架构分析 (2026-04-08 17:46)

### Arch-TS-27：`UObject` 包装族没有收敛成统一 wrapper algebra，新增 object-like 类型映射会沿着已漂移的复制实现继续扩散

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | object-like 类型映射的一致性与扩展面 |
| 当前设计 | 当前仓库把 `UObject*`、`TSubclassOf<T>`、`TObjectPtr<T>`、`TWeakObjectPtr<T>` 建成四套彼此平行的 `FAngelscriptType` 子类；每套都各自重复 `CreateProperty`、`MatchesProperty`、`SetArgument`、`GetReturnValue` 和默认值规则，而不是基于“对象核心语义 + wrapper trait”组合。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:122` — `FUObjectType::CreateProperty()` 直接产出 `FObjectProperty`，object family 的核心桥接从一开始就是“直接做一个具体 property”。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1591` — `FSubclassOfType::CreateProperty()` 另起一套 `FClassProperty + CPF_UObjectWrapper` 规则。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1853`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1865`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1926` — `FObjectPtrType` 产出的是 `FObjectProperty`，但 `CPF_TObjectPtr` 检查被整段注释掉，non-ref 参数又走 `Stack.StepCompiledIn<FClassProperty>()`，说明 wrapper contract 已经开始和 property 形状脱节。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:2157`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:2168`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:2222` — `FWeakObjectPtrType` 创建 `FWeakObjectProperty`，但匹配阶段却要求 `CPF_UObjectWrapper`，而创建时并没有设置该 flag；non-ref 参数同样走 `FClassProperty`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1690`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1952`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:2248` — `null/nullptr` 默认值翻译也在每个 wrapper 内重复实现。 |
| 优点 | 每个 wrapper family 都能独立补丁，短期适配单个 UE 类型时很直接，不需要先搭一层抽象框架。 |
| 不足 | 复制式扩展已经出现 contract 漂移，说明当前形态不适合继续加 `TScriptInterface<>`、`TSoftObjectPtr<>`、新的 handle/wrapper family；新增一类 object-like 映射时，开发者几乎必然会再复制一套近似实现，并继续把 property flag、stack ABI、默认值规则和 debugger 行为写散。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FPropertyDesc::Create()` 先集中按 `CPT_*` family 选择 descriptor，`ObjectReference/WeakObjectReference/LazyObjectReference` 复用 `FObjectPropertyDesc`，`Interface` 则单独落到 `FInterfacePropertyDesc`；对象实例包装由 `FObjectRegistry::Bind()` 统一托管。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1537`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1579`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1592`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:533`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ObjectRegistry.cpp:113` | wrapper 差异先在 property descriptor 工厂里收敛，实例生命周期再交给 registry；不会为每个 object-like family 再复制一套实例 owner。 |
| puerts | `FPropertyTranslator::Create()` 是唯一 factory 入口，`FInterfacePropertyTranslator` 单独处理 `FScriptInterface`，类包装时 `StructWrapper` 只消费 translator，并直接遍历 `Class->Interfaces` 挂方法。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:598`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:629`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1390`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:21`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:312` | object/interface/container 的差异先落为 translator family，wrapper 层不需要知道每种 property 的细节，也不会因新增 wrapper 再复制 stack ABI。 |
| UnrealCSharp | `FTypeBridge` 先把 `TScriptInterface`、`TWeakObjectPtr`、`TSoftClassPtr`、`TSoftObjectPtr`、`TMap`、`TSet`、`TArray` 识别成统一 `EPropertyTypeExtent` / generic reflection class，再由 `FInterfacePropertyDescriptor` 处理 `FScriptInterface` 的 object/interface 双槽写回。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:86`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:125`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:135`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:415`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/ObjectProperty/FInterfacePropertyDescriptor.cpp:29`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/ObjectProperty/FInterfacePropertyDescriptor.cpp:44` | wrapper family 先变成结构化 type extent/generic shape，再补具体 marshaller；新增 object-like family 时改 factory/descriptor，而不是复制一整套 type class。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把当前零散的 object-like `FAngelscriptType` 收敛成“对象核心语义 + wrapper trait”的组合式 bridge，先治理现有漂移，再把 `TScriptInterface<>` 作为同一模型下的新 family 落地。 |
| 具体步骤 | 1. 在 `Bind_BlueprintType.cpp` 或 `Core/` 新增 `FObjectReferenceWrapperTraits` / `TObjectReferenceFamilyType`，把 `CreateProperty` 所需的 property class、flag policy、`StepCompiledIn` 策略、return marshaller、default-literal policy 抽成 trait。 2. 先迁移 `TSubclassOf`、`TObjectPtr`、`TWeakObjectPtr` 三个现有 wrapper，要求它们共享一套 `GetObjectClass()/MatchesProperty()/SetArgument()/GetReturnValue()` 骨架，只保留最小差异。 3. 在 `FAngelscriptTypeUsage` 旁新增轻量 `EObjectReferenceExtent` 或类似 wrapper tag，把 `Object/Class/Weak/Interface/Soft` 这类差异变成可检查 metadata，而不是散落在不同子类的隐式约定里。 4. 第二阶段基于同一骨架补 `TScriptInterface<>`：value storage 用 `FScriptInterface`，property 生成走 `FInterfaceProperty`，实例方法/默认值/debugger 继续复用 object-core policy。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` |
| 预估工作量 | M |
| 架构风险 | 迁移 object-wrapper family 时，最敏感的是 stack ABI 和 `MatchesProperty()` 兼容性；如果 trait 抽象一次吃太多 family，容易把已有 `TObjectPtr`/`TWeakObjectPtr` 的历史行为同时改坏。 |
| 兼容性 | 对现有脚本声明语法可保持向后兼容；首阶段只替换 C++ 内部 owner，不要求用户改写 `UObject`/`TSubclassOf`/`TObjectPtr`/`TWeakObjectPtr` 的脚本代码。 |
| 验证方式 | 1. 为 `UObject*`、`TSubclassOf<T>`、`TObjectPtr<T>`、`TWeakObjectPtr<T>` 增加 `Property -> TypeUsage -> Property` roundtrip 测试。 2. 为每个 family 加参数传递/返回值测试，确认 `StepCompiledIn` 与 property class 一致。 3. 在迁移完成后再补一组 `TScriptInterface<>` 原型测试，验证新增 family 不需要复制新的 object-wrapper 骨架。 |

### Arch-TS-28：`UInterface` shell 仍会走 instancing metadata 初始化，非实例化类型没有从 `UASClass` 的构造/默认值合同中脱身

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `UInterface` 的 class shell 是否仍被 object-instancing 合同绑住 |
| 当前设计 | 当前仓库虽然在 class 生成时给 interface 打了 `CLASS_Interface`，但 interface shell 仍沿用 `UASClass` 的 instancing 数据模型：记录 `ScriptPropertyOffset` / `ScriptTypePtr` / `ConstructFunction` / `DefaultsFunction`，并在 finalization 时直接复用 `FinalizeObjectClass()`。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h:18`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h:24`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h:25`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h:26`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h:108` — `UASClass` 本体默认携带 `CodeSuperClass`、`ScriptPropertyOffset`、`ConstructFunction`、`DefaultsFunction` 和三套静态构造入口。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3359` — interface 只是在 class flags 上追加 `CLASS_Interface | CLASS_Abstract`，并没有切换到独立 shell 类型。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3678`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3686` — interface class 仍然记录 `ScriptPropertyOffset`/`ScriptTypePtr`，并统一调用 `UpdateConstructAndDefaultsFunctions()`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:5889`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:5894`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:5897` — `UpdateConstructAndDefaultsFunctions()` 不区分类别，直接从 `ObjType->beh.construct` 和 `void __InitDefaults()` 里抽 instancing callback。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:5190`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:5193`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:5461`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:5464` — finalization 遇到 interface 时直接走 `FinalizeObjectClass()`，最终仍把 `ClassConstructor` 指向 `UASClass::StaticObjectConstructor`。 |
| 优点 | 这条路径能最大化复用现有 class shell、reload 和 metadata pass，接口在编译产物里也能快速挂上 UE `UClass` 外壳。 |
| 不足 | `UInterface` 作为非实例化类型，本应只关心 callable/反射 identity；现在却被迫共享 object class 的 instancing metadata。结果是 P10 所需的 `FInterfaceProperty`、typed signature、接口专用 reload 细化都要先跨过“interface 仍然像对象类那样挂着 construct/defaults/class-constructor”的隐式合同。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FInterfacePropertyDesc` 只负责 `FScriptInterface` 的 value bridge，实例包装依旧走 `PushUObject()`/`ObjectRegistry`；interface 不需要额外进入 generated-class constructor lane。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:541`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:549`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:556`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:559`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ObjectRegistry.cpp:127` | interface 支持可以停留在 property/value bridge，不必顺带继承一整套脚本对象构造合同。 |
| puerts | `StructWrapper` 只是遍历 `Class->Interfaces` 把接口函数挂到 prototype；真正的动态类构造只有一条统一 `UJSGeneratedClass::StaticConstructor()`，interface 方法暴露并不要求新增 interface-specific instancing lane。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:312`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:322`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JSGeneratedClass.cpp:68`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JSGeneratedClass.cpp:82` | 先把 interface 视为 wrapper/prototype 扩展，再让动态类构造保持单一入口；interface 不会强耦合到 object defaults/construct callback。 |
| UnrealCSharp | `FDynamicInterfaceGenerator` 是独立 family：`BeginGenerator()` 只复制反射壳元数据并设置 `CLASS_Interface`，`EndGenerator()` 再做 `Bind()`、`StaticLink(true)`、reference token stream 和 editor refresh，没有额外抽取脚本 constructor/defaults callback。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicInterfaceGenerator.cpp:154`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicInterfaceGenerator.cpp:170`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicInterfaceGenerator.cpp:180`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicInterfaceGenerator.cpp:186`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicInterfaceGenerator.cpp:188`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicInterfaceGenerator.cpp:231` | interface shell 可以是“callable-only / reflection-only family”，不需要共享 instantiable class 的 metadata owner。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先把 interface shell 的 callable metadata 与 object-instancing metadata 拆开，让 `UInterface` 在内部成为“非实例化 family”，再逐步把 `FInterfaceProperty` 和 typed signature 接到这条干净路径上。 |
| 具体步骤 | 1. 把 `UpdateConstructAndDefaultsFunctions()` 拆成两层：`UpdateCallableMetadata()` 负责方法签名/dispatch 需要的数据，`UpdateInstancingMetadata()` 负责 `ConstructFunction`、`DefaultsFunction`、对象构造相关状态；当前仅 instantiable script class 调用后者。 2. 在 `FinalizeClass()` 中新增 `FinalizeInterfaceClass()`，不要再把 interface 复用到 `FinalizeObjectClass()`；首阶段该函数可以只做 `Bind()/StaticLink()/NotifyRegistrationEvent()` 收尾，并显式声明“不安装 `StaticObjectConstructor`”。 3. 在 `UASClass` 内把 interface 不需要的 instancing 字段视为 legacy mirror，保留读取兼容一个过渡版本，但在 interface 路径上停止写入；新增 `ensureMsgf` 防止后续代码继续从 interface class 取 `ConstructFunction`/`DefaultsFunction`。 4. 第二阶段把 P10 需要的 `FInterfaceProperty`/`TScriptInterface<>`、typed callable descriptor 挂到 `UpdateCallableMetadata()` 所拥有的新 owner 上，使 interface bridge 不再依赖 object shell 的 `ScriptPropertyOffset` 和构造回调。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` |
| 预估工作量 | M |
| 架构风险 | 最主要风险是热重载和旧 dispatch 路径可能仍隐式读取 `ConstructFunction`/`DefaultsFunction`；如果过渡期不保留 mirror/fallback，interface reload 容易先出回归。 |
| 兼容性 | 对现有 `interface` 脚本语法保持向后兼容；首阶段只是内部 owner 调整，不要求用户改写任何接口声明或实现类写法。 |
| 验证方式 | 1. 增加 interface-only 编译/热重载测试，确认 interface class 不再安装 object constructor 也能完成 `Bind()` 与方法壳生成。 2. 回归 `ImplementsInterface`、脚本接口调用、Blueprint 可见性三条路径，确认 callable metadata 仍正常。 3. 为后续 P10 预加 `FInterfaceProperty` 原型测试，验证 interface shell 与 value bridge 可以独立演进。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P0 | Arch-TS-28 | `UInterface` shell 仍绑在 instancing metadata，非实例化类型没有独立 owner | owner 分离 | 高 |
| P1 | Arch-TS-27 | object-wrapper family 复制实现已出现契约漂移，新增 object-like 类型会继续扩散 | family 收敛 | 高 |

---

## 架构分析 (2026-04-08 18:00)

### Arch-TS-29：`delegate/event` 调用包装仍靠 preprocessor 的字符串后缀表驱动，Blueprint event 参数桥接没有复用主 TypeSystem

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `delegate/event` 参数与返回值桥接的 owner |
| 当前设计 | 当前 `delegate/event` wrapper 不是从结构化 `FAngelscriptTypeUsage` 或 `FProperty` family 出发，而是先由 `Bind_BlueprintEvent.cpp` 注册一组按类型名拼接出来的 `__Evt_PushArgument__Type` / `__Evt_PushArgumentRef__Type` 函数，再由 preprocessor 通过字符串规则给每个参数选择 suffix；只有命不中专用 suffix 时，才退回 runtime `FromTypeId()` 的 generic path。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:550`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:552` — engine 侧只维护 `TSet<FString> BoundBlueprintEventArgumentSpecializations`，registry key 是裸类型名字符串。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp:358`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp:364`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp:378` — `BindPushArgument()` 为每个 `PushTypeName` 生成 `__Evt_PushArgument__%s` / `__Evt_PushArgumentRef__%s` 声明，专用绑定直接按名字展开。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp:396`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp:399`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp:400` — alias 也要额外走 `BindAliasedPushArgument()` 同步名字表。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:538`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:540`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:556` — `GetPushArgumentSuffix()` 只看类型文本，模板类型一律返回 generic suffix，是否存在专用 push 也只是查名字集合。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:637`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:640`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:643`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:650`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:685` — delegate wrapper 通过 `Contains(TEXT(\"&\"))`、`Contains(TEXT(\"const \"))` 和 suffix 字符串决定 `PushArgument` / `PushArgumentRef` / return push。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:1991`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:1994`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:1997`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:2004`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:2011` — 普通 event wrapper 也重复同一套文本 heuristics。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp:431`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp:433`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp:448`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp:451` — 只有 generic `__Evt_PushArgument(const ?& Value)` / `__Evt_PushArgumentRef(const ?& Value)` 才真正回到 `FAngelscriptTypeUsage::FromTypeId()`，形成“文本专用路径 + runtime generic 路径”双轨。 |
| 优点 | 常见 primitive / object family 可以走非常轻的专用 push 函数，不需要每次广播都做 runtime type lookup。 |
| 不足 | `delegate/event` 参数桥接没有复用主 property/type descriptor：template family 在 preprocessor 里被直接判成 generic，alias 需要额外同步字符串表，`const&` / `ref` 也靠文本匹配。新增 `TScriptInterface<>`、新的容器 wrapper、namespaced alias 或 future qualifier 时，必须同时修改 preprocessor 文本规则、`Bind_BlueprintEvent` 专用函数注册和通用 TypeSystem。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FFunctionDesc` 直接遍历 `UFunction` 参数，逐个创建 `FPropertyDesc`；调用时按 descriptor 顺序 `ReadValue_InContainer()` / `WriteValue()` / `CopyBack()`，没有额外的字符串 suffix registry。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:55`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:58`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:68`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:431`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:444`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:474`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:493`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1537` | event/function bridge 与普通参数桥接共用同一套 property descriptor，新增 family 不需要再补 preprocessor 文本规则。 |
| puerts | `FFunctionTranslator::Init()` 直接从 `UFunction` 构造 return translator 和 argument translators；translator 创建统一收敛在 `FPropertyTranslator::Create()`，`FInterfacePropertyTranslator` 也是同一套 factory 产物。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp:93`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp:120`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp:131`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp:135`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:598`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:608`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:627`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1390` | delegate/interface/container 参数都走 property translator factory，qualifier 与 family 不会在 wrapper 生成阶段再次被文本猜测。 |
| UnrealCSharp | `FDelegateGenerator` 直接遍历 `SignatureFunction` 的 `FProperty` 参数，再用 `FGeneratorCore::GetPropertyType()` 递归下钻 `FInterfaceProperty`、`FArrayProperty` 等 family，生成的 delegate 声明只消费结构化 property graph。 | `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FDelegateGenerator.cpp:27`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FDelegateGenerator.cpp:34`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FDelegateGenerator.cpp:87`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FDelegateGenerator.cpp:99`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FDelegateGenerator.cpp:106`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FDelegateGenerator.cpp:113`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:82`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:144`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:158` | delegate 签名生成和普通类型生成共用 `FProperty` 递归逻辑，interface/container 扩展不会再分裂出单独的 suffix 表。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 Blueprint event 的参数/返回值桥接从“字符串 suffix 选择器”收敛成可复用的 `EventArgumentPlan`，让 preprocessor 只消费计划，不再自己猜 family 与 qualifier。 |
| 具体步骤 | 1. 在 `Core/` 或 `Binds/Bind_BlueprintEvent.cpp` 新增 `FAngelscriptEventArgumentPlan`，字段至少包含 `PassKind(Value/Ref/ReturnRef)`、`TypeKey`、`bCanUseSpecializedPush`、`SpecializationName`、`ReturnInitPolicy`。 2. 在 `Bind_BlueprintEvent.cpp` 把现有 `BoundBlueprintEventArgumentSpecializations` 从 `TSet<FString>` 升级成 `TypeKey -> EventArgumentPlan` 的 registry；首阶段仍生成旧的 `__Evt_PushArgument__Type` / `__Evt_PushArgumentRef__Type` 入口，但这些名字只做 plan 的兼容别名。 3. 扩展 preprocessor 的参数解析结果，让 `ExtractArgumentList()` 除了 `ArgumentTypes` 字符串外，同时产出结构化 `FAngelscriptTypeUsage` / qualifier 信息；`GetPushArgumentSuffix()` 降级为 legacy fallback，不再用 `Contains(\"<\")`、`Contains(\"const \")`、`Contains(\"&\")` 判定核心语义。 4. 先迁移 `delegate` 和普通 `event` wrapper，验证 `const ref`、return-by-ref、alias family 与 container family 都能只通过 plan 选择 push 行为；第二阶段再让 P10 的 `TScriptInterface<>` 与 interface event 参数直接复用同一 plan。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` |
| 预估工作量 | M |
| 架构风险 | preprocessor 生成代码与旧的 `__Evt_PushArgument__Type` 名字已经被现有脚本广泛依赖；过渡期若直接删除 suffix 机制，最容易导致旧脚本 generated wrapper 编译失败。 |
| 兼容性 | 对现有脚本 `delegate/event` 语法保持向后兼容；首阶段保留旧名字形式和 generic fallback，只调整内部 owner 与 registry。 |
| 验证方式 | 1. 为 preprocessor 生成 wrapper 增加 snapshot，覆盖 primitive、alias、`const ref`、`TArray<int32>`、`TMap<FName, int32>`、`TScriptInterface<IMyInterface>`。 2. 回归 Blueprint event / delegate 广播 / return-by-ref 三条路径，确认 specialized push 与 generic fallback 行为一致。 3. 专门加入一组 template family 用例，确认不再因为 `Contains(\"<\")` 直接退回无信息 generic path。 |

### Arch-TS-30：核心类型重建仍是 VM-first 的 `FromTypeId()` / `FromDataType()`，脱离 live script engine 的消费者无法共享同一份类型真相

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 类型重建入口是 reflection-first 还是 VM-first |
| 当前设计 | 当前仓库里“把某个脚本值/脚本声明还原成 `FAngelscriptTypeUsage`”的核心入口仍是 `FromTypeId()` / `FromDataType()`：它们要求 live AngelScript engine、可查询的 `asITypeInfo`，以及已经写好的 `userData`/script shell 回填。`Debugging`、`StaticJIT`、`ClassGenerator` 都直接依赖这条 VM-first 路径，而不是优先消费 `FProperty` / `FAngelscriptPropertyDesc` 这类 runtime-independent 的结构化 type shape。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:281`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:283`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:285` — `FromDataType()` 直接向 live `asCScriptEngine` 取 `TypeId`，再回到 `FromTypeId()`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:340`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:363`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:364`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:372`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:420`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:424` — `FromTypeId()` 除 primitive 外都要通过 `Engine->GetTypeInfoById(TypeId)`、`GetUserData()` 和 subtype 递归恢复 type usage。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2917`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2918` — 类生成为 script property 做 reload 依赖 `ScriptType->engine->GetTypeIdFromDataType(ScriptProp->type)`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:2467`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:2487` — debugger locals / `this` scope 都是现场拿 `TypeId` 再调用 `FromTypeId()`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp:1932`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp:1937` — StaticJIT 在 property offset 推导时也是先读 `GetTypeInfoById(TypeId)`，再回到 `FromTypeId()`。 |
| 优点 | 对 VM 内局部变量、bytecode、script-defined template subtype 这类只有 `TypeId` / `asCDataType` 可拿的场景非常直接，桥接路径短。 |
| 不足 | canonical type truth 被绑在 live VM 上：只要没有活动 engine、`userData` 还没写完，或者消费者本身处在 UHT / codegen /离线分析阶段，就无法直接复用这套模型。结果是工具链与 runtime 容易各自再造 type shape，新增 family 时也要同时考虑 live-VM 解码和非 VM 消费者。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FPropertyDesc::Create()` 直接从 `FProperty` family 构造 descriptor，`FFunctionDesc` 也只是遍历 `UFunction` 的 `CPF_Parm` 属性；类型桥接不依赖 Lua VM 的 type id 才能成立。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1537`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1592`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1615`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:55`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:58` | type bridge 的核心 owner 是 UE reflection graph，本地 VM 只负责执行，不负责定义 canonical type shape。 |
| puerts | `FFunctionTranslator::Init()` 迭代 `UFunction` 参数并调用 `FPropertyTranslator::Create(Property)`；translator factory 接受的是 `FProperty`，不是 JS/V8 runtime type id。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp:93`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp:120`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp:131`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp:135`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1390` | VM 运行时只消费已经构造好的 translator；type reconstruction 不需要依赖 live script engine。 |
| UnrealCSharp | `FFunctionDescriptor::Initialize()` 先按 `UFunction` 的 `FProperty` 参数创建 `FPropertyDescriptor`，codegen 侧 `FGeneratorCore::GetPropertyType()` 也只吃 `FProperty`。runtime 与 generator 共享同一条 reflection-first 轴。 | `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FFunctionDescriptor.cpp:18`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FFunctionDescriptor.cpp:34`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FFunctionDescriptor.cpp:38`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/FPropertyDescriptor.cpp:47`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/FPropertyDescriptor.cpp:87`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:82`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:144`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:158` | 先把类型 family 固化成 runtime-independent descriptor，再让 runtime bind 和 codegen 共用它，降低“live VM 可用性”对架构的支配程度。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 新增 runtime-independent 的 `ResolvedTypeDesc` 层，让 `FromTypeId()` 从核心 owner 降级为适配器；能拿 `FProperty` / `FAngelscriptPropertyDesc` 的消费者优先走 reflection-first 路径。 |
| 具体步骤 | 1. 在 `Core/AngelscriptType.*` 或 `ClassGenerator/` 新增 `FAngelscriptResolvedTypeDesc`，字段至少包含 `Family`、`Qualifiers`、`ScriptTypeInfo`、`UnrealField`、`SubTypes`、`TemplateBase`；要求它既能从 `FProperty` / `FAngelscriptPropertyDesc` 构造，也能从 `asCDataType` / `TypeId` 适配。 2. 给 `FAngelscriptPropertyDesc`、`FAngelscriptFunctionDesc`、`CurrentCall` 等已有结构补一个缓存字段，优先存 `ResolvedTypeDesc`；这样 `ClassGenerator`、Blueprint event、future UHT manifest 都能先消费这份结构化结果。 3. 保留 `FAngelscriptTypeUsage::FromTypeId()` / `FromDataType()` 作为 VM adapter，但内部先生成 `ResolvedTypeDesc` 再落到 usage，避免 `Debugging`、`StaticJIT`、`ClassGenerator` 各自直接读 `GetTypeInfoById()` / `GetUserData()`。 4. 第一阶段先迁移 `DebugServer`、`StaticJIT::ReferencePropertyOffset()` 和 script property reload 三个高频消费者；只有纯 VM locals / bytecode 场景继续走 `TypeId` fallback。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` |
| 预估工作量 | M |
| 架构风险 | 过渡期会并存 reflection-first desc 和 VM-first fallback；如果 equality/hash/roundtrip 没钉住，最容易出现 debugger 看到的类型、JIT 推导的类型和 class reload 用的类型不一致。 |
| 兼容性 | 对现有脚本语法和 `FAngelscriptTypeUsage` 外部调用可保持向后兼容；首阶段只是新增一层 desc，并让旧 API 内部转发。 |
| 验证方式 | 1. 增加 `FProperty -> ResolvedTypeDesc`、`asCDataType -> ResolvedTypeDesc`、`TypeId -> ResolvedTypeDesc` 的三向等价测试，覆盖 primitive、`UObject`、container、delegate、interface。 2. 回归 `DebugServer` locals/`this` 展示、StaticJIT property offset 推导和 class reload，确认三者都能优先消费 desc，且 fallback 结果一致。 3. 对 hot reload 后的 script-defined class/struct/delegate 做 roundtrip，确认 desc 不依赖未更新的 `userData` 才能工作。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P0 | Arch-TS-29 | `delegate/event` 包装仍靠字符串 suffix 表驱动，Blueprint event 参数桥接没有复用主 TypeSystem | owner 收敛 | 高 |
| P1 | Arch-TS-30 | 核心类型重建仍是 VM-first 的 `FromTypeId()` / `FromDataType()`，非 live engine 消费者无法共享 canonical type shape | 解析轴转向 | 高 |

---

## 架构分析 (2026-04-08 18:15)

### Arch-TS-31：`UASFunction` 的入参 ABI 没有“按值复合类型”入口，`FScriptInterface` 这类 family 会被迫退化成 `const&` 或继续加特判

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 新 value-wrapper / interface value 参数的 callable 扩展面 |
| 当前设计 | `TypeSystem` 虽然允许 `FAngelscriptTypeUsage` 表达任意 family，但 `UASFunction` 的入参 ABI 仍只承认三种 ingress lane：reference、`UObject*` 指针、primitive value；只有返回值保留了“非 primitive / 非 object 的按值对象”分支。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h:161` — `UASFunction::FArgument` 最终只沉淀成 `ParmBehavior` / `VMBehavior` 两套调用枚举。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3975`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4010` — `AddFunctionArgument()` 只是按 `ArgDesc.Type.CreateProperty()` 生成 `FProperty` 并把 `TypeUsage` 塞进 `UASFunction::Arguments`，没有单独的“by-value wrapper”调用计划。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:753`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:763`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:774` — `FinalizeArguments()` 对入参只分 `bIsReference`、`IsObjectPointer()` 和 `IsPrimitive()` 三路；非引用且非对象指针的参数会直接 `check(Arg.Type.IsPrimitive())`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:875`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:883` — 返回值反而保留了非 primitive 复合值的 `ReturnObjectPOD` / `ReturnObjectValue` 分支，说明入参和返回值的 ABI 能力并不对称。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:1793`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:1850`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:1911` — `AllocateFunctionFor()` 的优化矩阵也只给“单 primitive 参数 / 单 ref 参数 / object return”等形状开槽。 |
| 优点 | primitive、裸 `UObject*` 和 ref/value-type 这几条热路径非常直接，JIT/非 JIT thunk 的优化目标清晰。 |
| 不足 | 对 P10 最直接的后果是：即便补出 `FInterfaceProperty` / `TScriptInterface<>` 的 property adapter，只要它需要作为非引用 value 参数进入 callable pipeline，就会先撞上 `FinalizeArguments()` 的三分法。新增其它 handle/value-wrapper family 也会遇到同样问题，最后不是被迫改语义成 `const&`，就是继续把 ABI 特判加进 `ASClass.cpp`。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FFunctionDesc` 对所有 `CPF_Parm` 参数统一创建 `FPropertyDesc`，interface 值参数走 `FInterfacePropertyDesc`，调用时按 descriptor 顺序 `ReadValue` / `WriteValue`，没有“非 primitive value 参数先天非法”的额外 ABI 门槛。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:55`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:58`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:444`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:449`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:533`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:554` | callable 参数搬运直接复用 property descriptor，`FScriptInterface` 只是又一种 property family，而不是 ABI 例外。 |
| puerts | `FFunctionTranslator::Init()` 给每个参数建立 `FPropertyTranslator`，interface 参数由 `FInterfacePropertyTranslator` 处理；实际调用时统一走 `JsToUEInContainer()` / `UEToJsInContainer()`，不需要额外声明“只有 primitive/object/ref 可入参”。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp:120`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp:131`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp:368`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp:390`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:598`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:618` | 先把“参数如何进出调用帧”抽象成 translator，再决定是否有快路径；新 family 主要补 translator，不必先改 ABI 枚举。 |
| UnrealCSharp | `FFunctionDescriptor` 初始化时对每个参数调用 `FPropertyDescriptor::Factory()`，`FInterfacePropertyDescriptor` 负责 `FScriptInterface` 的双槽写回；function 层没有单独的“by-value object wrapper 不合法”规则。 | `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FFunctionDescriptor.cpp:18`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FFunctionDescriptor.cpp:34`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/FPropertyDescriptor.cpp:47`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/FPropertyDescriptor.cpp:87`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/ObjectProperty/FInterfacePropertyDescriptor.cpp:29`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/ObjectProperty/FInterfacePropertyDescriptor.cpp:44` | 参数 marshalling 与 property family 是同一层抽象，`TScriptInterface<>` 扩展不需要先改一套额外的函数 ABI 三分法。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 给 `UASFunction` 补一条显式的“addressable value ingress”计划，把 `FScriptInterface` 和未来 value-wrapper family 先接入 generic callable path，再决定要不要继续做快路径优化。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h` 新增 `FAngelscriptCallArgPlan`，至少记录 `IngressKind(Primitive/ObjectPointer/Reference/AddressableValue)`、`NeedsTempConstruct`、`NeedsCopyBack`、`NeedsDestruct`、`CanUseOptimizedThunk`。 2. 让 `FinalizeArguments()` 先从 `FAngelscriptTypeUsage` 生成 plan，再映射旧的 `ParmBehavior` / `VMBehavior`；对“非引用、非 object pointer、非 primitive”的 family 不再 `check(false)`，而是统一落到 `AddressableValue` generic lane。 3. 在 `ASClass.cpp` 的 generic `RuntimeCallFunction()` / `RuntimeCallEvent()` 分支里补 `AddressableValue` 处理：先在 `ArgStack` 上构造临时值，再调用类型自己的 `SetArgument()` / copy-back / destruct 流程；首阶段只让 `UASFunction_NotThreadSafe` 和 `UASFunction` 基类消费这条新 lane，保持所有优化子类不变。 4. 第一批只迁移 `FInterfaceProperty` 原型和一个现有 wrapper family 的 by-value 用例；如果某些 family 语义上仍不允许 by-value，就在 `AddFunctionArgument()` / 分析阶段显式报“不支持的 pass mode”，而不是留到运行时触发断言。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` |
| 预估工作量 | M |
| 架构风险 | 过渡期 `CallArgPlan` 会和旧 `ParmBehavior` / `VMBehavior` 并存；如果 plan 到旧枚举的降级规则没钉住，最容易出现快路径和 generic path 对同一参数 family 行为不一致。 |
| 兼容性 | 对现有脚本函数声明完全向后兼容；primitive/object/ref 的既有快路径保持不动。新增变化首先只让原本会断言的 family 有了明确的 generic 行为或更早的诊断。 |
| 验证方式 | 1. 增加 `UFunction` 参数调用回归，覆盖 primitive、`UObject*`、`const Struct&`、`TScriptInterface<IMyInterface>` 和一个现有 wrapper/value family。 2. 专门验证“原本会命中 `check(Arg.Type.IsPrimitive())` 的签名”现在要么走 generic `AddressableValue`，要么在 class generation 阶段给出稳定报错。 3. 回归 `UASFunction_NoParams` / `UASFunction_DWordArg` 等旧快路径，确认 plan 引入后不会改变现有热路径结果。 |

### Arch-TS-32：interface callable 仍走 `FunctionName`-only 的 generic lane，`UASFunction` 的 typed pipeline 对接口方法完全不可见

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `UInterface` 方法是否能复用主 callable/type pipeline |
| 当前设计 | 当前仓库对普通脚本方法和 interface 方法维护了两条不同的 callable 轨道：普通方法会物化成 `UASFunction` 并挂上 `FArgument`、`ScriptFunction`、JIT/world-context 状态；interface 方法则只保留一个 `FunctionName` 签名，运行时经 `FindFunction()` + reflective invoke 转发。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:59` — `FInterfaceMethodSignature` 只有一个 `FName FunctionName` 字段，没有参数、返回值、默认值或 qualifier 描述。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1254`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:1149`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:1153` — interface 方法签名通过 engine registry 分配后，只以 `userData` 形式挂回脚本函数。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:56`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:65`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:67` — `CallInterfaceMethod()` 只按 `FunctionName` 做 `FindFunction()`，再走 `InvokeReflectiveUFunctionFromGenericCall()`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2803`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2820`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2824` — interface 壳生成时只创建最小 `UFunction` stub，并未进入 `UASFunction` 分配/参数物化路径。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h:161`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h:183`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h:188` — `UASFunction` 本身持有 `Arguments`、`ReturnArgument`、JIT function pointer 和 world-context 状态。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3411`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3513`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3566`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3624` — 普通方法则明确走 `UASFunction::AllocateFunctionFor()`、`AddFunctionReturnType()`、`AddFunctionArgument()` 和 `FinalizeArguments()`。 |
| 优点 | interface dispatch 的初始实现成本低，不必先把 `UInterface` 方法完整接入 `UASFunction` 体系，就能让名字匹配的实现类方法跑通。 |
| 不足 | 这条分叉意味着前面所有对 `UASFunction`、`CallSlotPlan`、`FInterfaceProperty` 的改进都不会自动覆盖 interface 方法。即便 P10 补齐了 interface value/property bridge，interface callable 仍旧只有“函数名 + reflective invoke”，参数 family、默认值、world context、fast path/JIT 以及 typed validation 都挂不上同一条 owner。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FFunctionDesc` 在识别出 interface function 后仍沿用同一条参数描述链：所有参数统一 `FPropertyDesc::Create()`，调用时按 descriptor 列表读写，不需要再维护一套“interface 名字转发”专用 callable lane。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:48`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:58`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:444`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:477` | interface 方法和普通方法共享同一份函数描述 owner，扩 interface 不需要再复制一条调用框架。 |
| puerts | `FFunctionTranslator::Init()` 虽然会标记 `IsInterfaceFunction`，但仍按 `FPropertyTranslator::Create()` 为每个参数建 translator；`StructWrapper` 暴露 interface 方法时也直接复用 `GetMethodTranslator()`。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp:113`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp:131`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:312`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:322` | interface 的差异只是一个 flag，不是另一条独立的 callable owner。 |
| UnrealCSharp | `FClassGenerator` 生成 class API 时会直接遍历 `InClass->Interfaces` 的 `UFunction`，并继续用 `FGeneratorCore::GetPropertyType()` 处理参数；动态 interface 生成阶段的 `GeneratorFunction()` 也复用统一函数生成 helper，运行时 `FFunctionDescriptor` 再用同一套 `FPropertyDescriptor::Factory()` 初始化参数描述。 | `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp:257`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp:264`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp:436`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp:457`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicInterfaceGenerator.cpp:315`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FFunctionDescriptor.cpp:34`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FFunctionDescriptor.cpp:38` | interface 既有独立生命周期，又不脱离统一函数 descriptor/pipeline；生成、动态 materialization 和 runtime 调用使用同一个 typed callable 模型。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 interface 方法从“名字签名 + generic reflective dispatch”升级成主 callable pipeline 的一个 `CallKind`，先补稳定 descriptor，再让 interface `UFunction` 逐步改用 `UASFunction` 壳。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/` 新增 `FAngelscriptCallableDesc` 或扩展现有 callable descriptor，字段至少包含 `FunctionName`、`ArgumentTypes`、`ReturnType`、`DefaultValues`、`WorldContextPolicy`、`DispatchKind(Regular/Interface)`；`FInterfaceMethodSignature` 第一阶段退化成这个 descriptor 的轻量 handle。 2. 修改 `Preprocessor/AngelscriptPreprocessor.cpp` 的 interface 注册逻辑：仍写 `userData`，但内容改为 descriptor handle；旧的 name-only payload 保留一个过渡版本做 fallback。 3. 在 `DoFullReload()` 的 interface 分支中，只要 descriptor 可用，就不要再创建裸 `UFunction`，而是创建 `UASFunction` 或 `UASFunction_Interface`，复用 `AddFunctionReturnType()`、`AddFunctionArgument()`、metadata 写入和 `FinalizeArguments()`；只有 legacy interface 声明继续走最小 stub。 4. 更新 `CallInterfaceMethod()`：优先从 descriptor 取 typed callable 并走统一 dispatch；只有 descriptor 缺失时才退回 `FindFunction(FunctionName)`。这样后续 `FInterfaceProperty`、container 参数、world context 和 fast path/JIT 才能真正沿主 callable pipeline 生效。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` |
| 预估工作量 | L |
| 架构风险 | interface callable lane 一旦开始复用 `UASFunction`，会触发和 world-context 注入、hot reload、Blueprint 反射缓存相关的连锁验证；必须保留 name-only reflective fallback 一个阶段。 |
| 兼容性 | 对现有 `interface` 语法和已编译脚本保持向后兼容；首阶段只是增加 typed descriptor 与新壳体，旧的 `FunctionName` fallback 继续可用。 |
| 验证方式 | 1. 增加 interface 方法签名 roundtrip 测试，覆盖返回值、`const ref`、container、`TScriptInterface<>`。 2. 验证普通类方法和 interface 方法最终都能产出一致的 callable descriptor，并在有 descriptor 时命中 `UASFunction` 路径。 3. 回归 `FindFunction(FunctionName)` fallback，确认旧预编译脚本或旧 interface 声明不会因为新 descriptor 缺失而失效。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P0 | Arch-TS-32 | interface callable 仍走 `FunctionName`-only 的独立 lane，无法复用 `UASFunction` typed pipeline | owner 收敛 | 高 |
| P1 | Arch-TS-31 | `UASFunction` 入参 ABI 缺少“按值复合类型”入口，`FScriptInterface`/新 value-wrapper 扩展会被迫退化成 `const&` 或继续特判 | 调用入口补洞 | 高 |

---

## 架构分析 (2026-04-08 18:28)

### Arch-TS-33：`GetClass()` 只表达 direct-reference 类，动态 `UClass` 生成却把它当成 nominal class owner，`TObjectPtr<>` 等 wrapper 的子类型信息进不了 class materialization

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | object-wrapper / future interface-wrapper 在动态 `UClass` 生成阶段的 nominal class 可见性 |
| 当前设计 | `FAngelscriptType` 对外只有一个 `GetClass()` 入口，语义是“这个 type 自己实现的 reference `UClass`”；但 `ClassGenerator` 的 `DefaultComponent` / `OverrideComponent` / `CodeSuper` 逻辑把它当成“这个属性最终指向哪个宿主类”的通用 owner。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:124` — `GetClass()` 的注释只承诺“the UClass that this angelscript type implements as a reference”，并不是 wrapper/subtype 解析接口。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1557`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1819`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:2123` — `TSubclassOf` / `TObjectPtr` / `TWeakObjectPtr` 各自都有私有 `GetMetaClass()` / `GetObjectClass()` helper，说明子类型信息其实存在。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1635`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1897`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:2199` — 但这些 wrapper 的公开 `GetClass()` 都直接返回 `nullptr`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3165`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3188` — `ResolveCodeSuperForProperty()` 只看 `Usage.GetClass()` 或 script-class `CodeSuperClass`，不会调用 wrapper 自己的 subtype helper。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:394`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:396` — 分析阶段验证 `DefaultComponent` 时，直接用 `ResolveCodeSuperForProperty(PropertyType)` 判定是否派生自 `UActorComponent`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:5229`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:5252`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:5329`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:5334` — `FinalizeActorClass()` 又直接取 `Property->PropertyType.GetClass()` 写入 `ComponentClass` 并做组件类型校验。 |
| 优点 | direct `UObject*` / script object 这条老路径足够直接，`ClassGenerator` 不需要知道每个 binder 的内部 subtype 解析细节。 |
| 不足 | 只要 nominal class 藏在 wrapper 私有 helper 里，动态类生成就看不见它。结果不是“新增 wrapper family 只补 type adapter”，而是还要继续改 `ResolveCodeSuperForProperty()`、`FinalizeActorClass()` 等 class-materialization 代码；未来 `TScriptInterface<>` 如果沿用 wrapper 形态，也会先撞上同一条接缝。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | object / interface family 先在 `FPropertyDesc::Create()` 里按 `CPT_ObjectReference` / `CPT_Interface` 分派，类身份再由 `FClassRegistry`、对象实例再由 `FObjectRegistry` 托管；property bridge 不直接承担动态类构造策略。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1537`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1583`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1592`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:53`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:90`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:302`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ObjectRegistry.cpp:113` | nominal type owner、property marshaller、对象实例 owner 是分层的，不会把 wrapper 子类型信息藏成仅 binder 私有的 helper。 |
| puerts | `FObjectPropertyTranslator`、`FClassPropertyTranslator`、`FInterfacePropertyTranslator` 各自只处理 value bridge；translator 工厂按 property family 分派，而 `UJSGeneratedClass::StaticConstructor()` 只做统一脚本构造，不去反向猜 property translator 的 nominal class。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:463`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:598`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:837`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1271`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1310`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JSGeneratedClass.cpp:18`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JSGeneratedClass.cpp:68` | wrapper/object/interface 的 nominal type 解析停在 translator/factory 层，动态类构造走单独 owner。 |
| UnrealCSharp | `FTypeBridge::GetClass()` 直接按 `FObjectProperty`、`FInterfaceProperty`、`FArrayProperty`、`FMapProperty`、`FSetProperty`、`FOptionalProperty` 这些 family 递归返回 reflection type；`FDynamicClassGenerator` 另有 `DefaultSubObjectInfoMap`、`GeneratorInterface()`、`NewComponentTemplate()` 管 component/interface 生命周期。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:388`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:415`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:446`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:562`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:599`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicClassGenerator.cpp:28`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicClassGenerator.cpp:656`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicClassGenerator.cpp:832` | “这个 property family 指向什么 nominal type” 与 “动态类如何处理 interface / component template” 是两层显式 owner，可独立扩展。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 给 `TypeSystem` 补一个显式的 nominal-class 能力层，让 class materialization 不再误用 `GetClass()` 这条 direct-reference 语义。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h` 新增 `ResolveNominalClass()` 或 `FAngelscriptObjectLikeShape`，至少区分 `DirectObjectClass`、`MetaClass`、`WrappedObjectClass`、`InterfaceClass` 四种角色；`GetClass()` 暂时保留为 legacy direct-reference 入口。 2. 在 `Bind_BlueprintType.cpp` 先迁移 `UObject*`、`TSubclassOf<T>`、`TObjectPtr<T>`、`TWeakObjectPtr<T>`：把现有私有 `GetMetaClass()` / `GetObjectClass()` 接到新能力层，不再只藏在 binder 私有实现里。 3. 修改 `ResolveCodeSuperForProperty()`、`Analyze()` 里的 `DefaultComponent` 校验、以及 `FinalizeActorClass()` / `FinalizeOverrideComponent` 路径，统一改读新的 nominal-class plan；首阶段保留旧 `GetClass()` fallback，避免 script object / raw object 老路径回归。 4. 第二阶段再把 P10 的 `TScriptInterface<>` / `FInterfaceProperty` 原型接到同一能力层，这样 interface family 可以暴露 `InterfaceClass` 而不必伪装成 direct object pointer。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` |
| 预估工作量 | M |
| 架构风险 | 最敏感的是过渡期 `GetClass()` 与 `ResolveNominalClass()` 双轨并存；如果 `DefaultComponent`、`CodeSuper`、reload 某些分支仍偷读旧入口，wrapper family 会继续出现“部分路径可见、部分路径不可见”的灰色状态。 |
| 兼容性 | 对现有脚本语法和现有 raw `UObject*` 成员保持向后兼容；首阶段主要调整 C++ 内部 owner，不要求用户改写声明。 |
| 验证方式 | 1. 增加 actor script class 回归，覆盖 raw `USceneComponent*` 与 `TObjectPtr<USceneComponent>` 两种 `DefaultComponent`/`OverrideComponent` 声明。 2. 回归 `ResolveCodeSuperForProperty()` 对 script object、native object、`TSubclassOf<T>`、`TObjectPtr<T>` 的结果。 3. 为 P10 预留 `TScriptInterface<IMyInterface>` property prototype 测试，确认 nominal-class 能力层能表达 `InterfaceClass`。 |

### Arch-TS-34：`AngelscriptUHTTool` 的 direct-bind eligibility 仍靠函数名白名单与分裂诊断治理，新增 type family 不能按 family 增量放开

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 新 type family 在 UHT direct-bind 路径中的可观测性与增量 rollout |
| 当前设计 | `AngelscriptFunctionSignatureBuilder` 在 header 解析失败时，只有极少数按 `ClassName + FunctionName` 写死的 fallback；`FunctionTableCodeGenerator` 又把结果压成 `Direct` / `Stub` 二值，而详细 `FailureReason` 只存在另一份 exporter CSV。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs:47`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs:57`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs:103` — `TryBuild()` 先依赖 header resolver；`overloaded-unresolved` 只有 `URuntimeFloatCurveMixinLibrary::GetNumKeys/GetTimeRange` 这类函数名白名单可以继续走 direct-bind fallback。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:101`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:133` — 生成侧统计和 CSV 只保留 `Stub` / `Direct` 二值，没有带上失败类型或 family。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:466`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:470`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:476` — `CollectEntries()` 对 interface 直接 stub，其它失败也统一降成 `ERASE_NO_FUNCTION()`。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:75`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:83`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:99`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:140` — 详细 `FailureReason` 其实会被写进 `AS_FunctionTable_SkippedEntries.csv` / `AS_FunctionTable_SkippedReasonSummary.csv`，但这份诊断资产并没有回流到生成 entry 自身。 |
| 优点 | 现有实现足够保守，遇到不确定签名时直接 stub，不会冒险生成错误的 `ERASE_*` 宏；排查单个失败函数时也还能从 skipped CSV 找到原因。 |
| 不足 | 对新增 type family 的 rollout 来说，这套治理面太“按函数名”而不是“按 family”：开发者既看不到某个 stub 是 `Interface blanket rule`、`UnsupportedFamily` 还是 `Header overload ambiguity`，也无法按 family 打开实验开关，只能继续堆函数白名单或人工比对两份 CSV/JSON。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FPropertyDesc::Create()` 直接按 `CPT_*` family 做工厂分派，`Object`、`Interface`、`Array` 等扩展点都落在同一 factory；coverage 的主语是 property family，而不是具体函数名。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1537`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1579`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1592`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1615` | 新增 family 时，扩展面和失败面都集中在 factory case，上层不需要再按函数名单独治理。 |
| puerts | `FPropertyTranslator::Create()` 也是按 `FProperty` family 统一建 translator；`Class`、`Object`、`Interface`、`Array` 等路径共享同一 factory，没有 per-function whitelist。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1271`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1278`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1310`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1312` | eligibility/unsupported 判定是 family-driven；如果某类 property 不支持，直接落在 translator factory，而不是散成函数名特例。 |
| UnrealCSharp | codegen 侧 `FClassGenerator` 遍历 `FProperty` / `CPF_Parm` 后统一走 `FGeneratorCore::GetPropertyType()`；runtime 侧 `FTypeBridge::GetClass()` 同样按 property family 递归解析 `Interface/Array/Map/Set/Optional`。 | `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp:166`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp:436`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp:457`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:415`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:446`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:562`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:599` | generator 与 runtime 都围绕同一组 family 扩展点演进，新增类型支持可以按 family 追踪覆盖率，而不是靠函数白名单。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 保留现有 `ERASE_*` 输出格式，但新增一层结构化 `DirectBindEligibility`，把 direct-bind/stub 的判定从“函数名白名单”推进到“family + 符号可恢复性 + policy override”。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptUHTTool/` 新增 `AngelscriptDirectBindEligibility`，至少包含 `Mode(Direct/Stub/PolicyOverride)`、`ReasonCode(UnsupportedFamily/HeaderMissing/OverloadAmbiguous/NonPublic/UnexportedSymbol/InterfaceStub)`、`BlockingFamilies`、`UsesHeaderResolver`、`UsesPolicyOverride`。 2. 让 `AngelscriptFunctionSignatureBuilder.TryBuild()` 即便失败也返回 eligibility；现有 `IsWhitelistedDirectBindFallback()` 第一阶段保留，但要把命中的 override 明确记录成 `PolicyOverride`，而不是静默成功。 3. 修改 `AngelscriptFunctionTableCodeGenerator.cs`：`AS_FunctionTable_Entries.csv` / summary JSON 继续保留 `Stub` / `Direct` 统计，同时附带 `ReasonCode` 与 `BlockingFamilies`；这样同一个 entry 不再需要去另一份 skipped CSV 才知道为什么 stub。 4. 第二阶段把 policy override 从硬编码函数名单独抽到 manifest 或配置文件，允许按 family 做实验性开关，例如 `EnableInterfaceDirectBindManifest`, `EnableOptionalDirectBindManifest`；旧硬编码白名单保留一个过渡版本。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | S-M |
| 架构风险 | 最大风险是新 eligibility 资产与现有 skipped CSV 双轨并存后出现 reason 漂移；第一阶段应由同一份 `ReasonCode` 枚举同时生成两边产物，避免 summary 与 exporter 各自发明字符串。 |
| 兼容性 | 对现有 UHT shard、`ERASE_*` 宏消费方和运行时行为完全向后兼容；首阶段只新增诊断与 policy 资产。 |
| 验证方式 | 1. 为 `AngelscriptUHTTool` 增加 snapshot，验证 `Entries.csv` / summary JSON 会稳定输出 `ReasonCode` 与 `BlockingFamilies`。 2. 人工构造 `overloaded-unresolved`、`unexported-symbol`、`interface`、`static-array-parameter` 四类样例，确认生成 entry 与 skipped CSV 的 reason 一致。 3. 新增一个 experiment 开关用例，验证打开某个 family policy 后，只有对应 family 的 entries 从 `Stub` 变成 `PolicyOverride/Direct`，而不是靠函数名单点漂移。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-TS-33 | `GetClass()` 的 direct-reference 语义被 class materialization 误用，`TObjectPtr<>` / future `TScriptInterface<>` 的 nominal class 无法复用 | owner 解耦 | 高 |
| P2 | Arch-TS-34 | UHT direct-bind 仍靠函数名白名单与分裂诊断治理，新增 family 难以按类型族增量 rollout | 观测与策略收敛 | 中 |

---

## 架构分析 (2026-04-08 18:38)

### Arch-TS-35：`MatchesProperty()` 同时承担“类型同一性”和“场景兼容性”，override 适配规则只能继续塞进 type matcher

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | property identity 与 override compatibility 的 owner 边界 |
| 当前设计 | 当前仓库把“这个 `FAngelscriptTypeUsage` 是否就是某个 `FProperty`”和“这个场景下是否允许兼容适配”都压进 `MatchesProperty()`；`BlueprintEvent` override 甚至会先改写脚本类型，再做 property match。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp:26`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp:33`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp:36` — event 校验先把调用场景压成 `OverrideArgument/OverrideReturnValue`，再统一走 `ArgumentType.MatchesProperty(...)`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:847`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:852`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:857`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:909`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:912`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:916` — class generator 为了支持 `double` 覆盖 UE `float`，会把返回值/参数临时改成 `ScriptFloatParamExtendedToDoubleType()`，然后再调用 `MatchesProperty()`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Primitives.cpp:484`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Primitives.cpp:489`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Primitives.cpp:491` — `double` 类型自己内建了“仅在 override 场景接受 `FFloatProperty`”的兼容规则。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:149`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp:107`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp:146`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TOptional.cpp:128` — 大多数 object/container family 虽然签名里带 `MatchType`，但实现里并不真正区分 override / type-finder / container 场景，只是直接按 family 递归匹配。 |
| 优点 | 小范围兼容需求可以很快落地，不必先设计单独的适配层；`float <- double` 这类历史 Blueprint 行为也能在现有链路上保住。 |
| 不足 | 一旦未来要支持 `FInterfaceProperty`、wrapper covariance、按值 value-wrapper 或更多 Blueprint 兼容语义，扩展面就会变成“继续给 `ClassGenerator` 加 extendo type + 继续给具体 binder 加 `MatchType` 特判”。类型 identity 与调用兼容规则耦在一起，新增 family 很难只补一层 adapter。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FFunctionDesc` 初始化时直接为每个 `CPF_Parm` 生成 `FPropertyDesc`，调用阶段按 descriptor 顺序 `ReadValue_InContainer()`；从源码可见，function 层直接消费真实 `FProperty` descriptor，而不是先定义一套 `OverrideMatchType` 再去问类型对象。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:55`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:58`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:444`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:449` | 可以把“是否允许转换/如何搬运”留给 callable/property descriptor，而不是让 type identity matcher 负担场景策略。 |
| puerts | `FFunctionTranslator::Init()` 对每个参数调用 `FPropertyTranslator::Create()`；真正调用时统一走 `JsToUEInContainer()` / `UEToJsInContainer()`。从源码可见，兼容与转换是 translator 的职责，而不是一个跨全局的 `MatchType` 参数。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp:120`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp:131`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp:368`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp:390` | 新 family 先补 translator，再决定是否给它快路径；兼容策略不会反过来污染 type identity。 |
| UnrealCSharp | `FFunctionDescriptor::Initialize()` 对所有参数统一调用 `FPropertyDescriptor::Factory()`，只记录 out/ref 索引；function 层没有把“override 上下文”再塞回 property identity matcher。 | `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FFunctionDescriptor.cpp:18`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FFunctionDescriptor.cpp:34`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FFunctionDescriptor.cpp:38`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FFunctionDescriptor.cpp:49` | 参数兼容、out/ref 语义和 property family 分层清楚；后续扩 `TScriptInterface<>` 或其它 wrapper，不需要先改一套 match-context 协议。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 `MatchesProperty()` 收敛回“严格 shape identity”，另起一层显式的 `CompatibilityPlan` 承担 override / Blueprint 兼容规则。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h` 新增 `FAngelscriptPropertyCompatibilityPlan`，字段至少包含 `IdentityResult(Exact/InContainerMismatch/KindMismatch)`、`AdaptationKind(None/FloatToDouble/AnyStructRef/LegacyFallback)`、`RequiresTypeRewrite`、`RequiresCopyBack`。 2. 让 `FAngelscriptTypeUsage::MatchesProperty()` 第一阶段只做严格 family/qualifier 匹配；`Bind_BlueprintEvent.cpp` 与 `AngelscriptClassGenerator.cpp` 改成先询问 `BuildCompatibilityPlan(Property, Context)`，只有 plan 允许时才继续改写参数行为。 3. 把现有 `double override float` 逻辑从 `Bind_Primitives.cpp` 和 `ClassGenerator` 的 extendo-type 改写迁到 plan 层；旧 `ScriptFloatParamExtendedToDoubleType()` 先保留一个兼容阶段，仅作为 plan 的 legacy backend。 4. 第二阶段再让 `FInterfaceProperty`、future wrapper covariance 和 by-value value-wrapper 共用这套 plan，避免每个 family 再定义自己版本的 `MatchType` 语义。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Primitives.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` |
| 预估工作量 | M |
| 架构风险 | 过渡期 strict identity 与 legacy `MatchesProperty()` 并存时，最容易出现旧特判还在生效、plan 又额外放行一次的“双重兼容”；需要先把 float/double 这条历史路径单点迁完，再扩其它 family。 |
| 兼容性 | 对现有脚本声明和 Blueprint override 语义可保持向后兼容；首阶段只是把兼容规则 owner 从 binder/class-generator 挪到显式 plan。 |
| 验证方式 | 1. 增加 Blueprint override 回归，覆盖 `float <- double` 返回值/参数、`AnyStructRef`、普通 exact-match。 2. 为新 plan 增加 snapshot，确认同一 `FProperty` 在 `TypeFinder` / `OverrideArgument` / `OverrideReturnValue` 三种上下文下得到稳定结果。 3. 预留一组 `FInterfaceProperty` 原型用例，验证未来 interface family 不需要再修改 `MatchesProperty()` 核心语义。 |

### Arch-TS-36：object/interface family 在 unresolved 阶段仍靠 stripped-name heuristic，P10 的 `UInterface` 集成会继续撞同一条 identity seam

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | unresolved nominal type 的统一解析机制 |
| 当前设计 | 当 wrapper 或 script object 还拿不到稳定的 `UClass*` / `userData` 时，当前仓库会退化到“去掉 `U/A` 前缀后按名字比对”或“全局扫 `TObjectIterator<UClass>`”的 heuristic；这套逻辑分散存在于多个 object-wrapper binder 和 interface finalization。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:219`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:226` — `FAngelscriptTypeUsage::GetClass()` 只有在 `ScriptClass->GetUserData()` 已经回填时才拿得到真实 `UClass*`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:173`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:176`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:179` — raw `UObject` family 在 `AssociatedClass == nullptr` 时，直接把 `Usage.ScriptClass->GetName()` 去掉 `U/A` 前缀再和 `PropertyClass->GetName()` 比。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1619`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1622`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1625` — `TSubclassOf<T>` 走同样的 stripped-name fallback。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1881`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1884`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1887` — `TObjectPtr<T>` 也复制了同一套 heuristic。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:2183`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:2186`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:2189` — `TWeakObjectPtr<T>` 同样如此。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:5062`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:5088`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:5097`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:5102` — `FinalizeClass()` 解析 implemented interface 时，先看 `ClassDesc`，再按 `GetByAngelscriptTypeName()`，最后把名字做一次 `U` 前缀裁剪并全局遍历所有已加载 `UClass`。 |
| 优点 | 在 `userData` 尚未完全回填、full reload 尚未结束时，系统仍然能“尽量猜中” native object/interface，对现有单名字空间项目比较宽容。 |
| 不足 | 这条 unresolved identity seam 没有统一 owner，而是散落在四个 wrapper family 和 interface finalization 里各自实现。只要出现 namespace、同名脚本/原生类、多个插件定义相同短名，或者 future `TScriptInterface<>` 想在 `userData` 就绪前参与匹配，这些 heuristic 就会变成误判源；对 P10 来说，这比“没支持 `FInterfaceProperty`”更早撞墙。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `ClassRegistry` 同时提供 `Find(const char*)` 和 `Find(const UStruct*)`，真正注册后会把 `UStruct*` 写进主表；`FInterfacePropertyDesc` 设值时直接使用 `InterfaceProperty->InterfaceClass` 调 `GetInterfaceAddress()`。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:53`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:90`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:302`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:556`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:559` | 名称只用于 bootstrap；steady-state identity 仍回到 `UStruct*` / `InterfaceClass`，不会把 stripped-name heuristic 散在多个 family。 |
| puerts | `FInterfacePropertyTranslator` 直接拿 `InterfaceProperty->InterfaceClass` 回填 `FScriptInterface`；`StructWrapper` 暴露接口函数时也是遍历 `Class->Interfaces` 的真实 `UClass*`。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:608`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:627`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:312`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:316` | object/interface nominal identity 在 translator/wrapper 层是显式字段，不需要在匹配时再做名字猜测。 |
| UnrealCSharp | `FTypeBridge::GetClass(const FObjectProperty*)` 直接读 `PropertyClass`，`GetClass(const FInterfaceProperty*)` 则用 `InterfaceClass` 生成 `TScriptInterface<>` generic type；`FInterfacePropertyDescriptor::Set()` 同样直接基于 `Property->InterfaceClass` 回填。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:388`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:392`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:415`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:426`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/ObjectProperty/FInterfacePropertyDescriptor.cpp:38`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/ObjectProperty/FInterfacePropertyDescriptor.cpp:44` | unresolved state 应该落在单一 resolver/registry 上，steady-state 一律回到 `PropertyClass` / `InterfaceClass` 等结构化字段。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在 `TypeSystem` 中补一个统一的 deferred nominal-type resolver，把 object-wrapper 和 interface finalization 的 stripped-name fallback 收口到单点。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h` 新增 `FAngelscriptNominalClassRef`，字段至少包含 `ResolvedClass`、`ScriptTypeInfo`、`DisplayName`、`ExpectedKind(Object/Class/Weak/Interface)`、`ExpectedFlags(CLASS_Interface 等)`、`SourceModule`。 2. 在 `FAngelscriptTypeUsage` 上提供 `ResolveNominalClassRef()`；`Bind_BlueprintType.cpp` 里的 raw `UObject`、`TSubclassOf<T>`、`TObjectPtr<T>`、`TWeakObjectPtr<T>` 全部改为先构造同一份 nominal ref，再调用统一 resolver，不再各自执行 `RemoveFromStart(TEXT(\"U\"/\"A\"))`。 3. 修改 `FinalizeClass()` 里的 interface 解析逻辑，让 `ImplementedInterfaces` 也先落到同一个 resolver；`TObjectIterator<UClass>` fallback 只保留在 legacy path，并记录 warning/trace，方便收敛历史命中。 4. 第二阶段把 future `FInterfaceProperty` / `TScriptInterface<>` 直接挂到这套 nominal ref 上，这样 interface value/property/class generation 在 `userData` 未完全就绪前也能共享同一条 unresolved identity seam。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` |
| 预估工作量 | M |
| 架构风险 | 新 resolver 与旧 heuristic 双轨并存时，最需要防的是“同一 unresolved type 在 wrapper 路径和 interface finalization 路径解析到不同 `UClass*`”；首阶段必须让旧 fallback 也通过统一 trace 输出命中来源。 |
| 兼容性 | 对现有脚本声明保持向后兼容；legacy stripped-name fallback 可以保留一个过渡阶段，不要求用户立即改名，但会让冲突场景从“静默猜测”升级为“可观测 warning”。 |
| 验证方式 | 1. 新增 duplicate-short-name 测试：native class / script class / interface 共享同一短名时，resolver 必须稳定区分，不再依赖 `U/A` 裁剪。 2. 回归 `UObject*`、`TSubclassOf<T>`、`TObjectPtr<T>`、`TWeakObjectPtr<T>` 的 `FProperty -> TypeUsage` 与 `TypeUsage -> Property` roundtrip。 3. 为 P10 增加 implemented-interface 回归，覆盖 reload 前后、native interface 与 script interface 混用、future `TScriptInterface<>` prototype。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P0 | Arch-TS-36 | unresolved object/interface identity 仍靠 stripped-name heuristic，P10 的 `UInterface` 集成缺少统一 nominal resolver | identity seam 收敛 | 高 |
| P1 | Arch-TS-35 | `MatchesProperty()` 混合了 strict identity 与 override compatibility，新增 family 兼容规则会继续扩散 | 兼容层解耦 | 中-高 |

---

## 架构分析 (2026-04-08 23:37)

### Arch-TS-37：`UASFunction` 用 `FAngelscriptTypeUsage` 规划调用帧，`FProperty` 只剩参数 struct 偏移，TypeSystem 与 UE 参数存储存在双真相

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | callable 参数/返回值布局的 owner |
| 当前设计 | `ClassGenerator` 先创建真实 `FProperty`，但进入 `UASFunction` 后，调用帧大小、对齐、入参搬运和返回值 copy-back 主要都按 `FAngelscriptTypeUsage` 的 `GetValueSize()` / `GetValueAlignment()` / `SetArgument()` / `GetReturnValue()` 推导；`FProperty` 在运行期大多只用于 `PosInParmStruct`。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3966`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3976`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4010` — `AddFunctionArgument()` 先 `CreateProperty()`，随后把 `{NewProperty, ArgDesc.Type}` 原样塞进 `UASFunction::Arguments`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:743`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:744`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:751`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:821` — `FinalizeArguments()` 的 stack size / alignment 来自 `Type.GetValueSize()` / `Type.GetValueAlignment()`，`FProperty` 只提供 `GetOffset_ForUFunction()`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:537`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:543`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:551`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:614`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:619`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:624` — `RuntimeCallEvent()` 直接按 `ParmBehavior` 从 parm struct 读裸值，再按 `Type` 语义写回返回值。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:378`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:383`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:385`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:458` — generic path 也主要通过 `Arg.Type.SetArgument()` / `ReturnArgument.Type.GetReturnValue()` 与 VM 交互。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:1793`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:1799`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:1867`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:1911` — 优化 thunk 选择同样按 `Type.GetValueSize()`、primitive/object/ref 分类，而不是按 `FProperty` family 或 `ParmsSize`。 |
| 优点 | 当前路径对 AngelScript VM 很直接，常见 primitive/object/ref 热路径能较早落入固定 thunk，运行时判断少。 |
| 不足 | 一旦 UE 存储形状与 AngelScript VM slot 形状不完全同构，TypeSystem 就要同时维护“`FProperty` 如何 materialize”和“调用帧如何搬运”两套真相。对 `FInterfaceProperty`、future by-value wrapper、或任何 storage 不是单纯 `UObject*` / primitive 宽度的 family，都不是只补 `CreateProperty()` 就能接入。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FFunctionDesc` 直接遍历 `UFunction` 的 `CPF_Parm` 属性，为每个参数构建 `FPropertyDesc`；函数调用阶段围绕 descriptor 读写，而不是让独立的 `TypeUsage` 再重新规划一套调用帧。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:51`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:58`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:68`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:72` | 参数布局与属性桥接共用同一份 `FProperty`-backed descriptor，新 family 不需要再额外定义一套 VM 帧布局真相。 |
| puerts | `FFunctionTranslator::Init()` 先用 `InFunction->PropertiesSize/ParmsSize` 确定参数 buffer，再为每个参数调用 `FPropertyTranslator::Create()`；translator factory 按 `FProperty` family 分派 `Object` / `Interface` / `Array` / `Map` / `Set`。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp:100`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp:120`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp:131`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1271`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1308`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1312` | callable buffer 与 type bridge 共用 property-family 工厂，`Interface`/container 只是 translator 扩展，而不是 ABI 例外。 |
| UnrealCSharp | `FFunctionDescriptor::Initialize()` 为每个参数构建 `FPropertyDescriptor`，`FUnrealFunctionDescriptor` 调用时统一用 buffer allocator + `ProcessEvent()`，descriptor 再提供 size/alignment。 | `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FFunctionDescriptor.cpp:18`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FFunctionDescriptor.cpp:34`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FFunctionDescriptor.cpp:38`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Public/Reflection/Property/FPropertyDescriptor.inl:57`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Public/Reflection/Property/FPropertyDescriptor.inl:65`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Public/Reflection/Property/FPropertyDescriptor.inl:75`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Public/Reflection/Function/FUnrealFunctionDescriptor.inl:23`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Public/Reflection/Function/FUnrealFunctionDescriptor.inl:33` | 先把参数存储与搬运固化成 property-backed descriptor，再决定是否有快路径；layout owner 不会漂移到语言私有 `TypeUsage`。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 保留现有 `UASFunction` 快路径，但把参数/返回值存储布局收敛成 `FProperty`-backed `CallStorageDesc`，让 `TypeUsage` 只负责语言语义，不再单独决定 UE 调用帧大小和对齐。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/` 新增 `FAngelscriptCallStorageDesc`，字段至少包含 `Property`、`ParmOffset`、`StorageSize`、`StorageAlignment`、`IngressKind`、`EgressKind`、`NeedsCopyBack`、`NeedsTempConstruct`；构建时要求同时拿到 `ArgDesc.Type` 和新创建的 `FProperty`。 2. 让 `AddFunctionArgument()` / return-property 物化阶段立即产出 `CallStorageDesc`；`FinalizeArguments()` 只消费 desc，不再直接按 `Type.GetValueSize()` / `GetValueAlignment()` 决定 `ArgStackSize`。 3. 在 `RuntimeCallFunction()` / `RuntimeCallEvent()` 中，primitive/object/ref 旧快路径继续保留，但 eligibility 改由 desc 的 `IngressKind/EgressKind` 决定；只要某个 family 的存储需要特殊处理，就落入基于 desc 的 generic marshaller，而不是修改全局 `ParmBehavior` 枚举。 4. 第一阶段只迁移 `FInterfaceProperty` 原型和一个现有非 primitive family 做验证；旧 `TypeUsage` API 保持兼容，`UASFunction_*` 优化子类仍可通过 desc 命中原有 fast path。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` |
| 预估工作量 | M-L |
| 架构风险 | 最大风险是 desc 与旧 `ParmBehavior/VMBehavior` 双轨期间出现 layout 不一致，导致 `ParmsSize`、return copy-back 或 JIT fast path 只改对一半；首阶段应限定在 generic path + 单个原型 family。 |
| 兼容性 | 对现有脚本语法和绝大多数 primitive/object/ref 函数签名保持向后兼容；变更主要在运行时 callable materialization 内部。 |
| 验证方式 | 1. 对比改造前后 `UFunction` 的 `ParmsSize`、`PropertiesSize`、参数 offset 与 fast path 选择。 2. 增加 `primitive`、`UObject*`、`const Struct&`、`FInterfaceProperty/TScriptInterface<>` 原型的入参/返回值 roundtrip。 3. 回归 `Blueprint` 调用、`ProcessEvent()`、直接脚本调用三条路径，确认 desc 驱动下结果一致。 |

### Arch-TS-38：容器 operation cache 仍附着在 live `asITypeInfo` 上，新增容器 family 需要同时复制 bridge 逻辑与 VM 生命周期

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 容器族 capability/cache 的 owner |
| 当前设计 | `TArray/TMap/TSet/TOptional` 的 element/key/value 能力并不落在显式 registry/descriptor，而是在首次验证模板类型时由各自 `Validate*Operations()` 从 `TemplateType->GetSubTypeId()` 恢复 `FAngelscriptTypeUsage`，然后把 `F*Operations` 对象直接写回 live `asITypeInfo::SetUserData()`。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp:1733`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp:1735`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp:1742`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp:1753`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp:1769` — `ValidateArrayOperations()` 先查 `TemplateType->GetUserData()`，再用 `FromTypeId()` 构造 `FArrayOperations` 并缓存 size/alignment/construct-copy-destruct trait。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp:1294`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp:1296`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp:1303`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp:1315`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp:1335` — `TMap` 同样把 key/value hash/compare/copy 能力塞回 template `userData`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TOptional.cpp:332`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TOptional.cpp:334`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TOptional.cpp:339`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TOptional.cpp:349` — `TOptional` 也是按 `TemplateType->GetSubTypeId(0)` 临时恢复 subtype，再缓存 `FOptionalOperations`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSet.cpp:728`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSet.cpp:730`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSet.cpp:735`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSet.cpp:745`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSet.cpp:763` — `TSet` 复制同一模式。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:102`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:105` — `ResetTypeDatabase()` 只重置 `FAngelscriptTypeDatabase`，从所读 TypeSystem 源码看，没有与这些 `TemplateType->SetUserData(Ops)` 对应的显式 cache invalidation hook。 |
| 优点 | 每个模板实例只在第一次使用时做一次 capability 计算，重复访问成本低，也便于在 binder 内局部实现容器特性。 |
| 不足 | 容器桥接能力被绑定到 live AngelScript typeinfo 的副作用缓存上，而不是显式 bridge owner。新增容器 family 或调整 subtype 合法性时，不仅要补 property/type adapter，还要再发明一套 `Validate*Operations + userData` 生命周期；`TArray<TScriptInterface<...>>` 这类未来需求也会把问题放大到“值桥接 + 容器 ops + reload/cache clear”三处同时改。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 容器缓存归 `FContainerRegistry` 所有，按 `FScriptArray/FScriptSet/FScriptMap` 指针和 `ITypeInterface` 组合做 `FindOrAdd`，并提供显式 `Remove`；容器 element/key/value 语义停在 env-owned registry，而不是 VM typeinfo 的隐式 user data。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ContainerRegistry.cpp:22`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ContainerRegistry.cpp:34`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ContainerRegistry.cpp:58`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ContainerRegistry.cpp:70`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ContainerRegistry.cpp:82`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ContainerRegistry.cpp:94` | container cache 有显式 owner 和移除路径，扩展新 family 时不需要把生命周期塞回脚本 VM 类型对象。 |
| puerts | container wrapper 持有 `FPropertyTranslator*` / `Property`，直接用 `Property->InitializeValue()`、`DestroyValue()`、`CopyCompleteValue()`、`GetSize()/GetMinAlignment()` 处理元素；translator factory 统一按 `Array/Map/Set` property family 分派。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/ContainerWrapper.cpp:221`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/ContainerWrapper.cpp:247`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/ContainerWrapper.cpp:255`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/ContainerWrapper.cpp:307`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/ContainerWrapper.cpp:311`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1312`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1316`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1320` | 容器操作缓存依赖 translator/property descriptor，而不是 live template type；新 family 优先扩 factory。 |
| UnrealCSharp | `FArrayHelper` 在构造时就用 `FPropertyDescriptor::Factory()` 持有 inner descriptor，析构时显式释放；size/alignment 来自 descriptor，不附着在脚本 runtime 类型对象上。 | `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Container/FArrayHelper.cpp:4`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Container/FArrayHelper.cpp:11`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Container/FArrayHelper.cpp:32`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Container/FArrayHelper.cpp:41`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Container/FArrayHelper.cpp:89`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Public/Reflection/Property/FPropertyDescriptor.inl:65`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Public/Reflection/Property/FPropertyDescriptor.inl:75` | container helper 和 descriptor 共享同一生命周期 owner，cache invalidation 与内存释放路径清晰。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把容器 operation cache 从 `asITypeInfo::userData` 迁到 engine-owned `ContainerBridgeRegistry`，让 template type 上只保留轻量 handle 或 legacy 兼容镜像。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/` 新增 `FAngelscriptContainerBridgeRegistry`，key 至少包含 `ContainerFamily(Array/Map/Set/Optional)` 与结构化 subtype signature；value 记录 element/key/value 的 `TypeUsage/ResolvedTypeDesc`、size/alignment、hash/compare/copy/construct/destruct 能力。 2. 让 `ValidateArrayOperations()` / `ValidateMapOperations()` / `ValidateSetOperations()` / `ValidateOptionalOperations()` 先查询 registry；只有 AngelScript 模板回调确实要求 `userData` 时，才把 registry handle 或兼容镜像写回 template type，而不再把完整 `Ops` 生命周期绑死在 VM 对象上。 3. 给 `ResetTypeDatabase()`、engine teardown 和 hot reload 增加显式 container-bridge invalidation；第一阶段允许旧 `userData` 路径继续读 registry 结果，避免一次性推翻现有容器 binder。 4. 迁移顺序先 `TOptional` 和 `TArray`，再 `TMap/TSet`；同时加入 `TArray<TScriptInterface<...>>` 与 `TOptional<TScriptInterface<...>>` 原型测试，确保 P10 的 interface value 不需要再复制一套容器缓存机制。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSet.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TOptional.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` |
| 预估工作量 | M |
| 架构风险 | 过渡期最敏感的是“旧 `Ops*` 指针缓存”和“新 registry handle”双轨并存时命中不同结果；首阶段必须让两边共用同一份 capability 计算，并对 legacy 命中打 trace。 |
| 兼容性 | 对现有脚本容器语法和大多数运行时行为可保持向后兼容；首阶段仅调整 cache owner 与 invalidation 方式。 |
| 验证方式 | 1. 增加 `TArray/TMap/TSet/TOptional` 的 capability snapshot，核对 size/alignment/hash/compare/copy trait 是否稳定。 2. 回归 engine reset / hot reload 后容器实例的重复访问，确认不会依赖 stale `userData`。 3. 增加 `TArray<TScriptInterface<...>>`、`TOptional<TScriptInterface<...>>` 原型 roundtrip，验证 interface family 可复用统一容器 bridge。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P0 | Arch-TS-37 | `UASFunction` 的调用帧布局由 `TypeUsage` 而非 `FProperty` 描述，`FInterfaceProperty` 等 family 会同时撞上 property 与 callable 双真相 | 调用布局 owner 收敛 | 高 |
| P1 | Arch-TS-38 | 容器 operation cache 仍绑定 live template `asITypeInfo`，新增容器或 interface-container 组合要复制 bridge 与 cache 生命周期 | cache owner 收敛 | 中-高 |

---

## 架构分析 (2026-04-08 23:52)

### Arch-TS-39：脚本类型语法打印仍嵌在 runtime type adapter 内，`TObjectPtr`/future `TScriptInterface<>` 只能继续借 declaration mode 和隐藏 token 传语义

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | source-level type syntax 与 runtime type bridge 的 owner |
| 当前设计 | 当前仓库把 Angelscript 侧“类型该怎么写”直接塞进 `FAngelscriptType` 运行时虚表：同一个 type adapter 既负责 property/callable 语义，也负责 member variable、function argument、function return、pre-resolved object 等多种语法上下文的字符串生成。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:108`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:120` — `FAngelscriptType` 内建 `EAngelscriptDeclarationMode`，把 `Generic`、`MemberVariable`、`PreResolvedObject`、`FunctionReturnValue`、`FunctionArgument`、`MemberVariable_InContainer` 都压进 type virtual interface。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:174`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:186`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:196` — 默认 declaration builder 会递归改写 inner mode，容器成员变量还要再切一次 `MemberVariable_InContainer`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:570`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:618` — `BuildFunctionDeclaration()` 直接用 `FunctionReturnValue` / `FunctionArgument` mode 生成对外函数签名。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Primitives.cpp:344`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Primitives.cpp:379` — `float` / `double` family 通过 mode 决定对外暴露 `float32` / `float64` 还是内部 type name。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1799`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1803`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1809` — `TObjectPtr` 在 member variable 场景会额外拼出 `unresolved_object` 隐藏 token，在 `PreResolvedObject` 场景又要把它剥掉。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1135`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1145`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1150` — 自动 getter/setter 生成还要根据 `IsObjectPointer()` / `IsUnresolvedObjectPointer()` 选择不同 declaration mode。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp:358`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp:364`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp:378` — Blueprint event push helper 的函数名与函数签名都直接依赖 `GetAngelscriptDeclaration(...FunctionArgument)`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h:470`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h:486` — tooltip / signature 说明文本同样直接消费这套 declaration printer。 |
| 优点 | 插件内部只有一套 Angelscript 类型字符串生成入口，现有 object wrapper、primitive alias、tooltip 和 bind declaration 至少不会各自发明一套语法。 |
| 不足 | 语法打印通道同时承载了“用户能看到的写法”和“编译器/生成器需要的隐藏提示”。结果是新增 family 时，不只是补 `CreateProperty()` 或 marshaller，还要决定它在 `MemberVariable`、`PreResolvedObject`、event helper、tooltip、getter/setter 里的所有拼写；`TScriptInterface<>`、`TOptional<TScriptInterface<...>>` 这类 P10 相关类型如果继续沿当前路径接入，很容易再引入新的 mode 或新的隐式 token。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | runtime 桥接围绕 `FPropertyDesc::Create()` 和 `FFunctionDesc` 的 property descriptor 展开；函数描述直接遍历 `UFunction` 的 `CPF_Parm`，没有在 runtime bridge 内维护“member variable / argument / return”多上下文字符串打印器。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1537`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:55`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:58` | 把 runtime type bridge 停在 property/function descriptor，source syntax 不需要继续压进每个 runtime adapter。 |
| puerts | `FunctionTranslator` 为参数和返回值统一创建 `FPropertyTranslator`；`FPropertyTranslator::Create()` 按 `Interface/Array/Map/Set` 等 `FProperty` family 分派，translator 本身不靠“FunctionArgument vs MemberVariable”的字符串 mode 决定语义。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp:131`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp:135`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1308`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1310`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1314`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1318`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1322` | runtime 映射以 property family 为中心，source declaration 不需要借 hidden token 传递 wrapper 解析语义。 |
| UnrealCSharp | source name 与 runtime bridge 明确分层：`TName` / `TGeneric` / codegen 负责把接口打印成 `TScriptInterface<...>`，`FTypeBridge::GetClass(const FInterfaceProperty*)` 则单独负责 runtime `FInterfaceProperty -> generic reflection class` 的桥接。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Binding/TypeInfo/TName.inl:155`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Binding/TypeInfo/TName.inl:165`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Binding/TypeInfo/TGeneric.inl:28`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:144`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:147`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:415`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:426` | “语言表面写法”和“runtime property bridge”分成两个扩展点，新增 interface/generic wrapper 不需要去改每个 runtime adapter 的 declaration mode 分支。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 Angelscript source syntax 从 `FAngelscriptType` runtime 虚表里抽成单独的 `TypeSyntaxEmitter` 层；runtime adapter 继续管 property/value/callable，语法 emitter 只管对外拼写和编译提示。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/` 新增 `FAngelscriptTypeSyntaxDesc` 或 `IAngelscriptTypeSyntaxEmitter`，字段至少包含 `SurfaceName`、`TemplateArguments`、`QualifierStyle`、`CompilerTags`、`AccessorForm`、`EventArgumentForm`。 2. 让 `BuildFunctionDeclaration()`、`Bind_BlueprintEvent.cpp`、`Helper_FunctionSignature.h`、getter/setter 生成逻辑先询问 syntax emitter，而不是直接调 `GetAngelscriptDeclaration(mode)`；首阶段保留旧 enum mode 作为 fallback。 3. 把 `TObjectPtr` 的 `unresolved_object` 和 primitive alias 的上下文改写迁到 syntax desc，parser/preprocessor 只解释结构化 `CompilerTags`，不再让运行时 type adapter 拼隐藏 token。 4. 第二阶段为 P10 增加 `TScriptInterface<>` 与 `TOptional<TScriptInterface<...>>` 的 syntax emitter 原型，验证接口 wrapper 不需要再新增 `PreResolvedObject` 之类的上下文特判。 5. 待 syntax emitter 稳定后，再逐步收缩 `EAngelscriptDeclarationMode`，只保留 legacy adapter 兼容入口。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Primitives.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` |
| 预估工作量 | M |
| 架构风险 | 迁移期最需要防的是“旧 declaration mode 路径”和“新 syntax emitter 路径”对同一类型输出不同拼写；首阶段必须对 bind declaration、tooltip 和 event helper 都做 snapshot 对照。 |
| 兼容性 | 对现有脚本 API 表面语法可保持向后兼容；第一阶段仅改变内部 owner，把旧字符串输出保持不变。 |
| 验证方式 | 1. 为 getter/setter declaration、`BuildFunctionDeclaration()`、Blueprint event helper 名称和 tooltip 增加 snapshot，确认 `TObjectPtr`、`float/float32`、`double/float64` 的现有输出不变。 2. 增加 `TScriptInterface<>` 原型 syntax 用例，验证 member variable、function argument、function return、event helper 四种上下文都不需要新增隐藏 token。 3. 回归 parser/preprocessor，确认 `CompilerTags` 路径和 legacy `unresolved_object` 输出一致后再切换默认路径。 |

### Arch-TS-40：高层 gameplay binder 直接解 AngelScript 模板内部结构，容器 family 语义没有停在 TypeSystem 边界

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | container/type query 是否由 TypeSystem 单点承载 |
| 当前设计 | 当前仓库里有一批高层 gameplay helper 并不通过 `FAngelscriptTypeUsage`/`TypeSystem` 查询“这是不是 `TArray<某类对象/某个 struct>`”，而是直接拿 `TypeId -> asCObjectType`，自己检查 `templateBaseType`、`templateSubTypes[0]` 和 `plainUserData`。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp:39`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp:43`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp:52`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp:58`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp:67` — `GetComponentsByClass(?& OutComponents)` 直接把 `TypeId` 解成 `asCObjectType`，要求 `templateBaseType == GetArrayTemplateTypeInfo()`，再从 subtype `plainUserData` 取 `UClass*`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp:89`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp:102`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp:108`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp:117` — 同一个文件里多个 overload 重复这套 array introspection。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_USceneComponent.cpp:48`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_USceneComponent.cpp:52`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_USceneComponent.cpp:61`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_USceneComponent.cpp:67`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_USceneComponent.cpp:76` — `GetChildrenComponentsByClass` 复制了同样的 `templateBaseType + subtype plainUserData` 检查。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UDataTable.cpp:14`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UDataTable.cpp:16`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UDataTable.cpp:42`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UDataTable.cpp:54`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UDataTable.cpp:60`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UDataTable.cpp:68` — `DataTable` 读取单 struct 时会走 `FAngelscriptTypeUsage::FromTypeId()`，但数组版本又回退到直接解 AngelScript 模板结构。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:61`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:560` — Core 对外公开的 template base 只有 `ArrayTemplateTypeInfo` 这个低层槽位，而不是“container query API”。 |
| 优点 | helper 就地验证 `TypeId`，实现直接、错误信息也离具体 API 很近。对当前只覆盖 `TArray<UObject*>` / `TArray<UStruct>` 的场景，写起来很快。 |
| 不足 | container family 语义已经越过 TypeSystem 边界，泄漏到了多个 gameplay binder。后续如果要支持新的容器 family、`TArray<TObjectPtr<...>>`、`TArray<TScriptInterface<...>>`、或统一 wrapper 解析，就不只是改 `Bind_TArray.cpp` / `Core/AngelscriptType.*`，还得全仓搜索这类 `templateBaseType` / `plainUserData` 手工解包点。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 高层容器 API 先通过 `PropertyRegistry->CreateTypeInterface()` 得到 element/key/value type interface，再交给 `ContainerRegistry` 创建或缓存容器；上层 helper 不直接读 Lua VM 的模板内部结构。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/BaseLib/LuaLib_Array.cpp:35`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/BaseLib/LuaLib_Array.cpp:36`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/BaseLib/LuaLib_Array.cpp:40`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ContainerRegistry.cpp:58`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ContainerRegistry.cpp:60`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ContainerRegistry.cpp:67` | 高层功能只问 registry/descriptor“元素类型是什么”，容器 family 细节不会扩散到业务 helper。 |
| puerts | container wrapper 直接持有 inner `FPropertyTranslator` 和 `Property`，数组元素的构造/销毁/拷贝统一通过 translator/property 完成；family 识别集中在 `FPropertyTranslator::Create()`。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/ContainerWrapper.cpp:221`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/ContainerWrapper.cpp:223`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/ContainerWrapper.cpp:227`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1312`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1314`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1318`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1322` | 上层只需要拿 translator/wrapper，不需要自己解 `templateBaseType` 或猜 subtype 元数据。 |
| UnrealCSharp | 容器地址与 helper 生命周期收敛在 `FContainerRegistry` 的模板特化里，family 区分停在 registry/descriptor 层；扩展 array/map/set 时先补 registry specialization，而不是修改高层 API 去读脚本 runtime 内部结构。 | `Reference/UnrealCSharp/Source/UnrealCSharp/Public/Registry/FContainerRegistry.inl:89`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Public/Registry/FContainerRegistry.inl:91`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Public/Registry/FContainerRegistry.inl:102`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Public/Registry/FContainerRegistry.inl:115` | 让 container family 语义留在 registry/descriptor 层，可以把“新增 family”的改动面控制在反射桥接边界内。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在 `Core` 暴露统一的 `ContainerQuery`/`TypeQuery` API，让高层 binder 只消费结构化查询结果，不再自己解析 `asCObjectType` 与 `plainUserData`。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/` 新增 `FAngelscriptContainerQuery` 或扩展已有 `ResolvedTypeDesc` 方案，至少支持 `TryResolveArrayElement(TypeId/Usage, OutElementDesc)`、`TryResolveNominalClass(TypeDesc, ExpectedBaseClass)`、`TryResolveStruct(TypeDesc)`。 2. 先把 `Bind_AActor.cpp`、`Bind_USceneComponent.cpp`、`Bind_UDataTable.cpp` 的 array out-arg 校验改为调用 query API；legacy `templateBaseType` 路径保留一轮作为 fallback，并在命中时打 trace，方便收敛散落调用点。 3. 在 query API 内部统一处理 `TArray<UObject*>`、`TArray<TObjectPtr<...>>`、`TArray<UStruct>` 和 future `TArray<TScriptInterface<...>>`，让新增 wrapper/container 组合时只改 query service，而不是逐个 helper 复制判断逻辑。 4. 第二阶段全仓搜索 `templateBaseType` / `templateSubTypes` / `plainUserData` 的 gameplay helper 用法，迁到同一 API；必要时给 API 增加 `ExpectedKind(Object/Struct/Interface)` 以支持 P10 的 interface-container 原型。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_USceneComponent.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UDataTable.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` |
| 预估工作量 | M |
| 架构风险 | 过渡期如果 query API 和 legacy 手工解析对同一 `TypeId` 得出不同结论，最容易出现“某些 helper 允许、某些 helper 拒绝”的灰色行为；首阶段必须在 fallback 命中时输出统一诊断。 |
| 兼容性 | 对现有脚本 API 形状和错误提示可以保持基本兼容；变化主要是内部校验 owner 从具体 binder 收口到 `Core`。 |
| 验证方式 | 1. 为 `GetComponentsByClass`、`GetChildrenComponentsByClass`、`DataTable` 读取增加回归，覆盖 `TArray<UActorComponent>`、`TArray<USceneComponent>`、`TArray<RowStruct>`。 2. 增加 `TArray<TObjectPtr<...>>` 与 `TArray<TScriptInterface<...>>` 原型测试，确认 helper 是否能通过统一 query API 识别或稳定拒绝。 3. 运行一次全仓 `templateBaseType/plainUserData` 命中扫描，确认 gameplay helper 已迁到统一诊断路径。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-TS-39 | source-level type syntax 与 runtime type bridge 共用一套 declaration mode，future `TScriptInterface<>` 会继续依赖隐藏 token/上下文特判 | owner 拆分 | 中-高 |
| P1 | Arch-TS-40 | gameplay helper 直接解 AngelScript 模板内部结构，新增容器或 interface-container 组合会扩散到高层 binder | 边界收敛 | 中-高 |

---

## 架构分析 (2026-04-09 00:02)

### Arch-TS-41：`GetByProperty()` / `FromProperty()` 仍是注册顺序驱动的双遍线性分派，新增 property family 缺少稳定 dispatch owner

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `FProperty -> FAngelscriptTypeUsage` 的分派拓扑 |
| 当前设计 | 当前 TypeSystem 对 `FProperty` 的识别仍靠两组全局数组协作完成：`FromProperty()` 先线性跑一遍 `TypeFinders` 写 `Usage`，如果没命中，再调用 `GetByProperty(Property, false)` 线性扫描 `TypesImplementingProperties`，第一个匹配的类型即成为结果。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:79`-`81` — `RegisterTypeFinder()` 暴露的是通用回调，而不是按 `FProperty` family 注册的结构化 resolver。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:590`-`609` — `FAngelscriptTypeDatabase` 只保存 `TypeFinders` 与 `TypesImplementingProperties` 两条线性链，没有 per-family 索引。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:96`-`99` — 只要 `CanQueryPropertyType()` 为真，`Register()` 就把该类型追加到 `TypesImplementingProperties`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:147`-`171` — `GetByProperty()` 先循环 `TypeFinders`，再循环 `TypesImplementingProperties`，命中完全依赖注册顺序。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:234`-`249` — `FromProperty()` 又单独循环一次 `TypeFinders`，失败后才退回 `GetByProperty(Property, false)`；同一条 property 解析路径天然是双遍分派。 |
| 优点 | binder 可以不改中心工厂，直接通过 `RegisterTypeFinder()` 或 `MatchesProperty()` 插入新类型，短期接入成本低。 |
| 不足 | 扩展新的 wrapper 或 `FInterfaceProperty` family 时，既要考虑“能不能命中”，也要考虑“是否比旧 resolver 更早命中”；这会让扩展结果受注册顺序影响。由于 `TypeFinder` 可以直接补写 qualifiers/subtypes，而 fallback 只返回 base type，后续要做缓存、冲突诊断或 family coverage 测试也缺少稳定 owner。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FPropertyDesc::Create()` 先按 `CPT_*` family 统一建 descriptor，`PropertyRegistry` 再按 `UField*` 缓存结果；调用方拿到的是稳定 descriptor，而不是依赖一串 finder 的先后顺序。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1537`-`1595`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/PropertyRegistry.cpp:316`-`319`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/PropertyRegistry.cpp:405`-`407` | 先做单点 family dispatch，再做缓存；扩展失败点和冲突点都集中在同一入口。 |
| puerts | `PropertyTranslatorCreator::Do()` 用集中 `IsA<...>` 链分派 translator，`FStructWrapper::GetPropertyTranslator()` 再按属性名缓存 translator，并允许 `CreateOn()` 刷新已有实例。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1225`-`1335`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:21`-`35` | dispatch owner 是 translator factory；wrapper 层只消费结果，不负责重新决定 family 归属。 |
| UnrealCSharp | `FPropertyDescriptor::Factory()` 直接按 `CastField<...>` 构造 descriptor，没有二次 finder 链；不同消费者共享同一 descriptor family。 | `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/FPropertyDescriptor.cpp:47`-`119` | property family 是一等扩展点，新增 `FInterfaceProperty` / `FOptionalProperty` 时只需补 factory case 与对应 descriptor。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 增加一个显式的 `PropertyDispatch` 层，把当前 `TypeFinder`/`MatchesProperty` 的线性链收敛成“按 property family 分桶、按优先级求解”的稳定 resolver。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/` 新增 `FAngelscriptPropertyDispatchKey` 与 `IAngelscriptPropertyResolver`，key 至少包含 `FFieldClass*`、`MatchContext(TypeFinder/Override/InContainer)`、可选 wrapper tag。 2. 让 `RegisterTypeFinder()` 和 `CanQueryPropertyType()` 保持兼容，但内部先注册到新 dispatch 表；`GetByProperty()` / `FromProperty()` 统一改成走单次 `ResolveProperty(Property, Context)`，直接返回完整 `FAngelscriptTypeUsage`。 3. 第一阶段只迁移 `Object/Class/Struct/Interface/Array/Map/Set/Optional` family；legacy 线性链保留为 fallback，并在命中时记录 diagnostics，帮助找出仍未迁移的 binder。 4. 增加 `PropertyDispatchManifest` 测试，专门验证 `FObjectProperty`、`FInterfaceProperty`、`FArrayProperty`、`FMapProperty` 在不同 binder 注册顺序下解析结果不变。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSet.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TOptional.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` |
| 预估工作量 | M |
| 架构风险 | 过渡期最大风险是“新 resolver 与 legacy finder 链对同一 property 给出不同结果”；如果不保留 fallback 诊断，很难定位到底是哪一类 binder 仍在隐式抢匹配顺序。 |
| 兼容性 | 对现有脚本语法和 binder 注册接口可保持向后兼容；首阶段变化集中在内部分派 owner，不要求立即重写所有 `Bind_*.cpp`。 |
| 验证方式 | 1. 为 `FObjectProperty`、`FInterfaceProperty`、`FArrayProperty`、`FMapProperty`、`FSetProperty`、`FOptionalProperty` 增加解析 snapshot。 2. 人为打乱 binder 注册顺序，确认解析结果不变。 3. 回归 `CreateProperty()`、debugger scope 和 `BuildFunctionDeclaration()`，确认新 dispatch 没有改变现有 family 的 qualifiers/subtypes。 |

### Arch-TS-42：动态类成员的类型真相仍由 `FAngelscriptPropertyDesc` 与 live `asITypeInfo::GetProperty()` 名字拼接得到，class materialization 内部存在 runtime 双轨

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 动态 `UClass` 生成阶段的成员描述 owner |
| 当前设计 | 当前 class generator 并没有一份一次构建、后续复用的“resolved member descriptor”。它先从 live `ScriptType->GetProperty()` 建 `PropertyIndexMap` 和 `PropertyTypes`，再按属性名把结果回填到解析阶段留下的 `FAngelscriptPropertyDesc`，后续 GC schema 和 debug prototype 又再次遍历 `ScriptType->GetProperty()`，必要时回退到 `FromProperty(ScriptType, i)` 重新还原类型。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:1123`-`1127` — `FAngelscriptClassDesc` 只保存解析阶段的 `Properties` / `Methods` 列表。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:1147`-`1160`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:5880`-`5889` — `GetProperty()` 仍是按名字线性查 `FAngelscriptPropertyDesc`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:200`-`220` — generator 先从 `ScriptType->GetProperty()` 建 `PropertyIndexMap`，并把 `FAngelscriptTypeUsage::FromProperty(ScriptType, i)` 存进 `PropertyTypes`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:345`-`379` — 之后再按 `PropertyDesc->PropertyName` 回表，把 `PropertyType`、`ScriptPropertyIndex`、`ScriptPropertyOffset`、访问控制写回 `FAngelscriptPropertyDesc`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4878`-`4916` — 构建 `ReferenceSchema` 时再次遍历 `ScriptType->GetProperty()`，如果找不到 `PropDesc` 才回退到 `FromProperty(ScriptType, i)`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4939`-`4954` — 构建 `DebugValues` 时第三次遍历 `ScriptType->GetProperty()`，并重新通过 `FromProperty(ScriptType, i)` 生成 `FAngelscriptTypeUsage`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h:34`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h:66` — 这套回填结果最终又分别缓存进 `UASClass::ReferenceSchema` 与 `UASClass::DebugValues`，成为动态类运行时反射壳的一部分。 |
| 优点 | 解析阶段的 `LineNumber`、metadata 和名字仍能留在 `FAngelscriptPropertyDesc`，而 live `ScriptType` 又能提供最终 offset/visibility；短期上实现成本较低。 |
| 不足 | 成员类型真相在 class materialization 内部已经分裂成两份：一份是 parser 产出的 `FAngelscriptPropertyDesc`，另一份是 live VM 属性表。新增 wrapper、`FInterfaceProperty`、成员级 qualifiers 或未来 interface 相关成员语义时，必须同时保证“名字 join 能对上”“三次遍历拿到同一结果”“debug/GC/property 创建消费的是同一版类型”；否则最容易出现 `UPROPERTY` 建好了，但 debug/GC 仍按旧类型理解的漂移。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `PropertyRegistry::GetFieldProperty()` 为每个 `UField*` 只创建并缓存一份 `FPropertyDesc`，`FFunctionDesc` 直接消费这些 property descriptor；没有“解析期属性描述 + 运行期属性表再拼接一次”的第二条链。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/PropertyRegistry.cpp:316`-`319`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/PropertyRegistry.cpp:405`-`407`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:55`-`59` | 先把 member descriptor 固化，再让调用/调试/容器逻辑复用它，避免同一成员被多次独立解析。 |
| puerts | `FStructWrapper::GetPropertyTranslator()` 按 property 名缓存 translator，`FFunctionTranslator::Init()` 遍历参数时只创建一遍 translator；interface 方法挂接也复用同一 wrapper/translator 体系。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:21`-`35`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp:120`-`136`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:312`-`326` | 先有稳定的 member translator，再扩展 interface method 或 wrapper 行为；不会在不同消费阶段重新猜一次类型。 |
| UnrealCSharp | `FFunctionDescriptor::Initialize()` 对每个 `FProperty` 只调用一次 `FPropertyDescriptor::Factory()`，之后 out/ref、返回值和 marshalling 都复用同一批 descriptor；`FInterfacePropertyDescriptor` 也是这套 family 的一部分。 | `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FFunctionDescriptor.cpp:18`-`59`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/FPropertyDescriptor.cpp:47`-`87`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/ObjectProperty/FInterfacePropertyDescriptor.cpp:29`-`45` | 让“成员是什么”先落成统一 descriptor，后续 class/function/property bridge 共享这份结构化结果。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在 class analysis 阶段生成一份一次构建、后续复用的 `ResolvedMemberDesc` 列表，把 parser property desc 与 live script property 表的 join 固化下来。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/` 或 `ClassGenerator/` 新增 `FAngelscriptResolvedMemberDesc`，字段至少包含 `PropertyName`、`ResolvedTypeUsage`、`ScriptPropertyIndex`、`ScriptPropertyOffset`、`bIsPrivate`、`bIsProtected`、`bHasUnrealProperty`、`SourcePropertyDesc`。 2. 在 `PrepareForCreateClass()` 或等价的 class analysis 阶段，把 `ScriptType->GetProperty()` 与 `FAngelscriptClassDesc::Properties` 只 join 一次，产出按 script property 顺序稳定排列的 `ResolvedMembers`；名字不匹配或类型漂移在这一阶段直接报错。 3. 让 `CreateProperty()`、`CreateReferenceSchema()`、`CreateDebugValuePrototype()`、hot reload property diff 全部改为消费 `ResolvedMembers`，不再各自重新遍历 `ScriptType->GetProperty()`。 4. 保留 `FAngelscriptTypeUsage::FromProperty(ScriptType, i)` 作为 VM-only fallback，只给局部变量/debugger 临时值等没有 `ResolvedMemberDesc` 的场景继续使用。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` |
| 预估工作量 | M |
| 架构风险 | 迁移期如果 `ResolvedMembers` 与旧的即时 `GetProperty()` 遍历并存，但没有约束谁是最终真相，最容易出现 `ReferenceSchema`、debug prototype、property diff 三边读取不同成员布局。 |
| 兼容性 | 对现有脚本类定义、property 名称和 `UPROPERTY` 物化结果可保持向后兼容；变化仅在内部 owner，从“多次实时拼接”收口为“单次解析缓存”。 |
| 验证方式 | 1. 为脚本类属性生成增加 snapshot：同一成员的 `PropertyType`、`ScriptPropertyIndex`、`ScriptPropertyOffset`、`bHasUnrealProperty` 在 property 创建、GC schema、debug prototype 三处保持一致。 2. 增加 `TArray<UObject>`、`TObjectPtr<UObject>`、future `TScriptInterface<>` 原型成员的回归，确认新增 wrapper 只需改一处 member resolve。 3. 做一次 hot reload 回归，验证属性重排、访问控制变化、metadata 保持时，`ResolvedMembers` 与 `UASClass::ReferenceSchema` / `DebugValues` 同步更新。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P0 | Arch-TS-42 | 动态类成员描述仍由 parser property desc 与 live script property 表名字拼接而成，class materialization 内部存在 runtime 双轨 | owner 收敛 | 高 |
| P1 | Arch-TS-41 | `FProperty -> FAngelscriptTypeUsage` 仍依赖双遍线性 finder 链，新增 family 易受注册顺序影响 | 分派层重构 | 中-高 |

---

## 架构分析 (2026-04-09 00:12)

### Arch-TS-43：`ComposeOntoClass` 仍只是 class desc 的旁路字段，TypeSystem 看不到“脚本类型投影到宿主类”的 canonical identity

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | composed/projection class 的类型身份 owner |
| 当前设计 | 当前仓库把“脚本类投影到哪个宿主类”记成 `FAngelscriptClassDesc::ComposeOntoClass` 字符串，并在 finalization 时镜像到 `UASClass::ComposeOntoClass`；但 `TypeSystem` 的注册、`GetByClass()`、`FromClass()` 仍只认直接 `UClass*` 或 `ScriptTypePtr`，没有显式 projection descriptor。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:1141`-`1145` — `FAngelscriptClassDesc` 只把 `ComposeOntoClass` 保存为 `FString`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h:27` — `UASClass` 只有一个运行时镜像字段 `UClass* ComposeOntoClass`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:65`-`88` — `Register()` 只把 `Type->GetClass(DefaultUsage)` 写入 `TypesByClass`，没有 compose/projection 维度。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:129`-`136` — `GetByClass()` 只按直接 `UClass*` 查 map。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:291`-`305` — `FromClass()` 只尝试 `GetByClass(Class)`，失败后再回退到 `UASClass::ScriptTypePtr`，并不会查看 `ComposeOntoClass`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:1337`-`1364` — compose 相关编译期检查只验证 `ComposedStruct` metadata。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:5052`-`5056` — runtime finalization 对 compose 的处理也只是把字符串解析成 `UClass*` 再写回 `UASClass`。 |
| 优点 | 对现有 class generator 侵入很小，compose 能快速挂到已有 `UASClass`/reload 管线，不需要先重写 type registry。 |
| 不足 | compose 信息没有进入 TypeSystem 的 canonical key，因此后续任何基于“宿主类是谁”的扩展都拿不到稳定 owner。新增 wrapper family、`UInterface` bridge、default-component 校验或 host-class 特殊规则时，都会继续在 `ClassGenerator`、binder 和 runtime lookup 三头补特判。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | reflected type 先注册到 `UStruct* -> FClassDesc` 主表，再在对象绑定时按 `Object->GetClass()` 选 metatable/module；脚本增强始终附着在宿主反射类上。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:59`-`77`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:90`-`105`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ObjectRegistry.cpp:133`-`151` | host reflected type 是一等 key，wrapper/module 只是附属层，不会先落成一个 parser 字符串再由各处猜它投影到谁。 |
| puerts | JS wrapper 以真实 `UClass`/`UStruct` 为中心工作：`StructWrapper` 直接遍历 `Class->Interfaces`，对象进 JS 时用 `IObjectMapper::FindOrAdd(..., Object->GetClass(), Object)`。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:312`-`326`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:519`-`522` | “宿主类 identity” 与 “脚本包装/方法投射”分层，扩展 interface/wrapper 时不需要再引入新的 parser-side class flag。 |
| UnrealCSharp | 运行时绑定围绕现有 `UClass` 展开：`BindImplementation(UStruct*)` 直接从 `FoundClass` 收集函数与接口函数，再通过 `FReflectionRegistry::Get().GetClass(InClass)` 把生成 descriptor 接回原生类。 | `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FCSharpBind.cpp:179`-`201`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FCSharpBind.cpp:307`-`340`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FCSharpBind.cpp:348`-`357` | projection/wrapper 的 owner 停在真实 `UClass` 和 reflection registry，而不是让 generated shell 独占 identity。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 `ComposeOntoClass` 从“parser 字符串 + `UASClass` 镜像字段”升级成显式 `HostClassProjection` 描述符，让 TypeSystem 也能查询“脚本壳对应的宿主类是谁”。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/` 或 `ClassGenerator/` 新增 `FAngelscriptHostClassProjectionDesc`，字段至少包含 `GeneratedShellClass`、`ProjectedHostClass`、`ProjectionMode(Standalone/ComposeOnto/Wrapper)`、`SourceClassDesc`、`Namespace`。 2. 在 class analysis 阶段一次性解析 `ComposeOntoClass`，把当前字符串转成稳定 projection desc；`FinalizeClass()` 改为消费 desc，而不是继续直接解字符串。 3. 扩展 `FAngelscriptType::GetByClass()` / `FAngelscriptTypeUsage::FromClass()` / default-component 与 future interface 校验路径，允许先查 projection desc，再决定是按 generated shell 还是 host class 工作；首阶段保持 direct `UClass*` 结果不变，只额外暴露 projection query。 4. 保留 `FAngelscriptClassDesc::ComposeOntoClass` 与 `UASClass::ComposeOntoClass` 作为兼容镜像一个过渡版本，待所有消费方迁到 projection desc 后再降级为 legacy 字段。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` |
| 预估工作量 | M |
| 架构风险 | 过渡期如果 direct class lookup 和 projection lookup 同时生效，但没有钉死优先级，最容易出现“某些路径按 generated shell 识别、某些路径按 host class 识别”的分叉。 |
| 兼容性 | 对现有脚本 `ComposeOntoClass` 写法可保持向后兼容；变化集中在 C++ 内部 owner，从旁路字段收敛成可查询的 projection 描述。 |
| 验证方式 | 1. 为 composed class 增加 `Class -> TypeUsage -> HostClassProjection` snapshot。 2. 增加 composed class 上的 default component / property lookup / source path 回归，确认现有行为不变。 3. 补一组 composed class + interface 原型测试，验证 future `TScriptInterface<>`/`FInterfaceProperty` 不需要再额外猜宿主类。 |

### Arch-TS-44：`UInterface` 实现仍依赖 Blueprint/K2 式 `PointerOffset=0` 装配，缺少显式 interface instance bridge

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `UInterface` 实例桥接与实现类挂接方式 |
| 当前设计 | 当前实现把脚本 interface 明确建模成 Blueprint/K2 风格接口：interface shell 不设 `CLASS_Native`，实现类在 finalization 时统一追加 `FImplementedInterface{Class, PointerOffset=0, bImplementedByK2=true}`，运行时 quick cast 再用 `ImplementsInterface()` 判定。也就是说，interface implementation 更像 class flag 装饰，而不是显式的 `FScriptInterface`/`GetInterfaceAddress()` bridge。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3359`-`3364` — interface shell 明确走“不要设 `CLASS_Native`，让 `GetInterfaceAddress()` 走 `PointerOffset=0` 的 Blueprint/Script interface 模式”。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:5135`-`5139` — 实现类统一追加 `FImplementedInterface`，且固定 `PointerOffset = 0`、`bImplementedByK2 = true`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:112`-`127` — `CanCastScriptObjectToUnrealInterface()` 只把 target type 的 `userData` 解释成 `UClass*`，再以 `ObjectClass->ImplementsInterface(TargetClass)` 判断是否可 cast。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp:267` — 下游 reflective fallback 仍把 owning interface class 当特殊拒绝路径。 |
| 优点 | 充分复用 UE 现有 Blueprint interface 语义，初期不必先补 `FInterfaceProperty` 或 `TScriptInterface<>` 的值语义，就能让脚本实现类通过 `ImplementsInterface()` 工作。 |
| 不足 | 这条路径没有显式表达“interface value 的 object 槽和 interface-address 槽”是谁在管理。结果是 class finalization、runtime cast、future property bridge 和 container/interface value 原型都只能继续围绕 `PointerOffset=0` + `bImplementedByK2` 打补丁；P10 真要补 `FInterfaceProperty` 时，最先缺的不是 callable，而是 instance-side bridge owner。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FInterfacePropertyDesc` 直接以 `FScriptInterface` 为底层值：`SetValueInternal()` 同时 `SetObject()` 和 `SetInterface(Value ? Value->GetInterfaceAddress(InterfaceClass) : nullptr)`。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:533`-`560` | interface implementation/value bridge 先被显式建模成 object + interface-address 双槽，class flag 只是上层事实，不是值桥接 owner。 |
| puerts | `FInterfacePropertyTranslator` 的 `UEToJs/JsToUE` 同样直接读写 `FScriptInterface`，对象包装继续交给 `IObjectMapper::FindOrAdd`。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:598`-`630` | interface family 与普通 object wrapper 共享同一 object mapper，但 interface address 仍由独立 translator 负责回填，不依赖 `bImplementedByK2`。 |
| UnrealCSharp | `FInterfacePropertyDescriptor::Set()` 把托管值写回 `FScriptInterface`，`FTypeBridge::GetClass(const FInterfaceProperty*)` 还会显式生成 `TScriptInterface<>` 的 generic reflection type。 | `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/ObjectProperty/FInterfacePropertyDescriptor.cpp:29`-`45`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:415`-`430` | interface bridge 需要同时有 instance-side value owner 和 type-side generic reflection owner；这两层都不能只靠 K2 flag 约定。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先把当前 `PointerOffset=0`/`bImplementedByK2` 规则包进显式 `InterfaceBridge`/`ImplementedInterfacePlan`，再让 runtime cast、future `FInterfaceProperty` adapter 和 callable 路径共享这条 owner。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/` 或 `ClassGenerator/` 新增 `FAngelscriptImplementedInterfacePlan`/`IAngelscriptInterfaceBridge`，字段至少包含 `InterfaceClass`、`ImplementationKind(K2ZeroOffset/NativeOffset/ScriptInterfaceValue)`、`PointerOffset`、`ResolveInterfaceAddress(UObject*)`。 2. 第一阶段不改行为，只把 `FinalizeClass()` 里硬编码的 `PointerOffset=0`/`bImplementedByK2=true` 收进 `K2ZeroOffset` strategy，再由该 strategy 生成 UE `FImplementedInterface`。 3. 让 `CanCastScriptObjectToUnrealInterface()` 和 future `FInterfaceProperty`/`TScriptInterface<>` adapter 都先查 `InterfaceBridge`，统一决定 `ImplementsInterface()`、`GetInterfaceAddress()` 和 `FScriptInterface` 的 object/interface 双槽写法。 4. 第二阶段再让 `BlueprintCallableReflectiveFallback`、UHT function table 和 interface container 原型查询“这个 interface family 是否已有 bridge”，只在 bridge 缺失时继续走 legacy reject/fallback。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` |
| 预估工作量 | L |
| 架构风险 | 如果 bridge plan 和现有 `NewClass->Interfaces` 直接写入并存，但没有统一 owner，最容易出现 `ImplementsInterface()` 成功、`GetInterfaceAddress()`/`FScriptInterface` 仍然走旧路径的半迁移状态。 |
| 兼容性 | 首阶段可以完全保留现有 K2/Blueprint interface 行为，只是把 owner 显式化；对现有脚本 interface 定义和实现类写法应保持向后兼容。 |
| 验证方式 | 1. 增加脚本实现 native interface、脚本 interface、以及 composed class 实现 interface 的回归，确认 `ImplementsInterface()` 与 `GetInterfaceAddress()` 结果一致。 2. 增加 `FScriptInterface`/`TScriptInterface<>` 原型 roundtrip，用同一 bridge 验证 object/address 双槽写回。 3. 回归 interface reflective fallback 与 future function-table bridge 覆盖判断，确保有 bridge 时不再无条件命中 reject path。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P0 | Arch-TS-44 | `UInterface` 实现仍建立在 `PointerOffset=0 + bImplementedByK2` 的 K2 约定上，缺少显式 interface instance bridge | bridge owner 收敛 | 高 |
| P1 | Arch-TS-43 | `ComposeOntoClass` 仍是 parser/class shell 旁路字段，未进入 TypeSystem canonical identity | projection owner 收敛 | 中-高 |

---

## 架构分析 (2026-04-09 00:23)

### Arch-TS-45：`NewerVersion` 热重载版本链没有进入 TypeSystem 的 canonical lookup，动态类与接口类的类型身份会沿 class shell 分叉

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 热重载后的 class identity 是否被 TypeSystem 统一归一 |
| 当前设计 | 当前仓库已经显式维护 `UASClass::NewerVersion` 链，并在少数构造路径手工追到最新类；但 `FAngelscriptType::TypesByClass`、`GetByClass()`、`FromClass()` 和 interface cast 仍直接使用传入的原始 `UClass*`，没有统一 canonicalization。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h:19`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h:76`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:912` — `UASClass` 明确持有 `NewerVersion` 并提供 `GetMostUpToDateClass()`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2573`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2578`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3695`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3699` — 热重载时旧类会被改名、打上 `CLASS_NewerVersionExists`，并串到 `ReplacedClass->NewerVersion = NewClass`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:1184`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:1189`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:1217`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:1222` — 默认组件路径已经不得不手工调用 `GetMostUpToDateClass()` 做归一。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:54`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:65`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:86`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:129`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:131` — type registry 只把注册时的原始 `UClass*` 写进 `TypesByClass`，`GetByClass()` 也只按原始指针查表。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:291`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:294`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:297`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:300` — `FromClass()` 不会先归一到最新类，只在查表失败后回退到当前 shell 的 `ScriptTypePtr`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:112`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:119`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:126`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:127` — interface cast 也直接拿 `TargetType->GetUserData()` 和 `Object->GetClass()` 做判定，没有版本链归一。 |
| 优点 | 原始指针查表简单直接，正常运行时路径几乎没有额外开销；热重载支持可以先靠 `ClassGenerator` 层局部补丁完成。 |
| 不足 | 版本归一规则只存在于少数调用点，TypeSystem 并不拥有“最新 class shell 是谁”的统一答案。结果是默认组件、type lookup、interface cast、future `FInterfaceProperty`/`TScriptInterface<>` bridge 可能各自命中不同代际的 `UClass*`，新增类型族或接口支持时很容易把“最新类归一”继续写散。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `ClassRegistry` 以当前 `UStruct*` 为主 key，并在 metatable 命中旧描述符时把当前 `UStruct*` 回填到主表；lookup owner 停在 registry，而不是让调用点各自决定要不要追最新类。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:59`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:64`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:90`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:100`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:302`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:308` | 即使脚本名或 metatable 名可复用，运行时 canonical identity 仍由 registry 上的当前 `UStruct*` 托管。 |
| puerts | 对象包装始终使用 `Object->GetClass()` 重新取当前类；热重载刷新时还会主动跳过 `REINST_` 临时类，避免旧 shell 继续进入映射。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:1803`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:1810`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:2070`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:2072`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:2350`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:2352`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.h:395`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.h:398` | 运行时总是以“对象当前类”为准，并对重实例化临时类做集中防护，而不是把版本归一交给每个高层功能。 |
| UnrealCSharp | 动态类 owner 收敛在 `DynamicClassMap` 和 `FReflectionRegistry`：按名字/namespace 找当前动态类，再由 bind/runtime 统一消费 registry 结果。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicClassGenerator.cpp:26`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicClassGenerator.cpp:30`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicClassGenerator.cpp:265`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGenerator.cpp:193`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGenerator.cpp:199`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FCSharpBind.cpp:348` | 热重载后的动态类 identity 先归一到 registry，再进入 binding/type bridge；不会让 `BindImplementation()`、property bridge、动态生成器各自追版本链。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先把“哪个 `UClass*` 才是当前有效 class shell”提升成 Core 级别的 canonicalization API，再让 TypeSystem、interface cast 和 class finalization 统一使用它。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/` 新增 `NormalizeGeneratedClass(UClass*)` 或 `FAngelscriptClassIdentityRegistry`，第一阶段只封装 `UASClass::GetMostUpToDateClass()`、`CLASS_NewerVersionExists` 和 composed/interface 兼容规则。 2. 让 `FAngelscriptType::Register()`、`GetByClass()`、`FAngelscriptTypeUsage::FromClass()`、`FAngelscriptEngine::CanCastScriptObjectToUnrealInterface()` 都先查 canonical class，再做查表或 `ImplementsInterface()` 判断；保留旧 raw-pointer 路径一轮作为 fallback。 3. 把 `ASClass.cpp` 里默认组件这类手工 `GetMostUpToDateClass()` 的调用迁到统一 API，避免继续产生局部版本归一逻辑。 4. 在第二阶段把 future `FInterfaceProperty`/`TScriptInterface<>`、`ComposeOntoClass`、class-based cache key 一起切到 canonical class，确保接口实现类与宿主类不会因旧 shell 残留而分叉。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` |
| 预估工作量 | M |
| 架构风险 | 过渡期最容易出现“某些路径按 canonical class、某些路径仍按旧 shell”导致双重身份；必须先在查表和 cast 入口统一打 trace，确认所有老路径都能观测到偏差。 |
| 兼容性 | 对现有脚本语法和公开 API 可保持向后兼容；变化集中在 C++ 内部 lookup owner。热重载后命中的类壳会更稳定，但不应改变用户脚本写法。 |
| 验证方式 | 1. 增加脚本类热重载回归，验证旧类实例、`GetByClass()`、`FromClass()` 和默认组件重建都命中同一个最新类。 2. 增加脚本 interface 与实现类热重载回归，确认 `ImplementsInterface()`、cast 和 future interface property 原型都不受旧 shell 影响。 3. 为 canonicalization 增加日志/manifest，列出 raw class 与 canonical class 是否一致。 |

### Arch-TS-46：`FromProperty(ScriptType, Index)` 在入口就把脚本成员 contract 压扁成 `TypeId`，新 property family 无法沿一条统一签名流扩展

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 脚本成员属性是否有 first-class 的 signature/descriptor 对象 |
| 当前设计 | 当前仓库为脚本成员保留了大量 member-level 状态，但 `FAngelscriptTypeUsage::FromProperty(asITypeInfo*, Index)` 只提取 `TypeId` 再转成 `TypeUsage`。成员的访问控制、offset、是否生成 `UPROPERTY`、metadata 和 Blueprint 相关规则都落在 `FAngelscriptPropertyDesc` 或 class generator 的后续回填步骤里，没有统一的 script-property signature。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:790`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:799`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:801`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:832`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:864`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:871` — `FAngelscriptPropertyDesc` 本身已经承载 `PropertyType`、`Meta`、`bHasUnrealProperty`、访问控制和 `ScriptPropertyIndex/Offset`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:262`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:265`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:267` — 但 `FromProperty(ScriptType, Index)` 只读出 `TypeId`，直接丢掉成员级别的其他 contract。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:200`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:218`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:219` — class analysis 的第一轮 `PropertyTypes` 也是从这个缩水后的入口建立。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:350`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:361`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:374`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:378` — 到真正生成 `UPROPERTY` 时，又要重新调用 `ScriptType->GetProperty(...)` 回填 offset/访问控制并写回 `PropertyDesc`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4878`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4905`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4939`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4953` — GC schema 和 debugger prototype 又重新遍历 script properties，并在 fallback 时再次调用这个只含 `TypeId` 的入口。 |
| 优点 | `TypeUsage` 的构造足够轻量，很多需要“只知道基础类型”的路径可以快速复用这一入口，不必一开始就建立完整成员描述。 |
| 不足 | 一旦新增的 family 需要成员级上下文，例如 `FInterfaceProperty`、instanced object wrapper、future member-level qualifier 或更细的 debug/GC owner，当前入口没有地方承载这些 contract。结果只能继续依赖 `PropertyDesc`、`GetProperty(...)` 多次回填和后续特判，扩展成本不会停在 TypeSystem 边界内。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `PropertyRegistry` 直接以 `UField*` 缓存 `FPropertyDesc`，descriptor 从一开始就是 property 级对象，而不是“先还原 type，再在别处找 member flags”。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/PropertyRegistry.cpp:316`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/PropertyRegistry.cpp:318`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/PropertyRegistry.cpp:405`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/PropertyRegistry.cpp:406` | property bridge 的 owner 是 descriptor，自带 `FProperty*` 全量语义，后续函数/容器/接口路径都消费同一对象。 |
| puerts | `StructWrapper` 以具体 property 为粒度缓存 `FPropertyTranslator`；interface 方法并入 wrapper 时也继续复用同一 translator/descriptor 体系。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:21`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:26`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:29`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:34`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:312`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:322` | 成员翻译器一旦建好，属性值桥接和 interface 方法暴露都不需要再次“从 type id 反推成员语义”。 |
| UnrealCSharp | `FFunctionDescriptor::Initialize()` 直接遍历真实 `FProperty` 参数列表并调用 `FPropertyDescriptor::Factory(Property)`；factory 本身也按 `FProperty` family 直接覆盖 `FObjectProperty`、`FClassProperty`、`FInterfaceProperty` 等。 | `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FFunctionDescriptor.cpp:18`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FFunctionDescriptor.cpp:34`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FFunctionDescriptor.cpp:38`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FFunctionDescriptor.cpp:49`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/FPropertyDescriptor.cpp:47`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/FPropertyDescriptor.cpp:69`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/FPropertyDescriptor.cpp:81`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/FPropertyDescriptor.cpp:87` | family 扩展点集中在 property descriptor factory；新增 interface/wrapper family 时，不需要让 generator、debugger、GC 分别重新拼一次成员 contract。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在 `TypeUsage` 之下补一层 script-member signature，让“成员是什么”先成为可复用对象，再让 class generation、GC、debugger 和 future interface family 共享它。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/` 或 `ClassGenerator/` 新增 `FAngelscriptScriptPropertySignature`，字段至少包含 `PropertyName`、`ResolvedTypeUsage`、`TypeId`、`PropertyOffset`、`bIsPrivate`、`bIsProtected`、`SourcePropertyDesc`、`bHasUnrealProperty`。 2. 新增 `BuildScriptPropertySignature(asITypeInfo*, int32, const FAngelscriptClassDesc*)`，把当前 `GetProperty(...)` 的信息一次性读全；`FAngelscriptTypeUsage::FromProperty(ScriptType, Index)` 第一阶段保留为兼容包装，只返回 `Signature.ResolvedTypeUsage`。 3. 让 `AngelscriptClassGenerator.cpp` 的 property analysis、GC schema、debug prototype 优先消费 signature，而不是多次调用 `GetProperty(...)` + `FromProperty(...)` 重新拼装。 4. 第二阶段把 `FInterfaceProperty`/`TScriptInterface<>`、instanced object wrapper 和 member-level qualifier 原型也挂到这条 signature 流上，确保新增 family 只需扩 member signature builder 和对应 adapter，不再额外补多处回填逻辑。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` |
| 预估工作量 | M |
| 架构风险 | 如果 signature builder 与旧 `PropertyDesc + GetProperty(...)` 回填同时存在但没有统一 owner，最容易出现 property 创建正确、debugger/GC 仍按旧 contract 读取的半迁移状态。 |
| 兼容性 | 对现有脚本成员声明和 `UPROPERTY` 行为可保持向后兼容；首阶段只是内部 owner 收敛，不要求用户改脚本。 |
| 验证方式 | 1. 对脚本类成员生成增加 snapshot，核对 `TypeUsage`、offset、访问控制、`bHasUnrealProperty` 在 property 创建、GC 和 debug prototype 三处一致。 2. 增加 `TObjectPtr<UObject>`、`TArray<UObject>`、future `TScriptInterface<>` 原型成员测试，验证新增 family 只需扩 signature builder。 3. 回归热重载后的成员重排和访问控制变化，确认 signature 更新能同步驱动所有消费方。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P0 | Arch-TS-45 | 热重载版本链 `NewerVersion` 没有进入 TypeSystem canonical lookup，动态类与接口类 identity 会随 class shell 分叉 | identity owner 收敛 | 高 |
| P1 | Arch-TS-46 | `FromProperty(ScriptType, Index)` 在入口就丢掉脚本成员 contract，新增 property family 无法沿统一 signature 扩展 | property signature owner 收敛 | 中-高 |

---

## 架构分析 (2026-04-09 00:32)

### Arch-TS-47：原生属性暴露没有统一 accessor descriptor，`Bind_BlueprintType.cpp` 在 TypeSystem 外又做了一次 family dispatch

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | native `UPROPERTY` 到脚本 getter/setter 的扩展入口 |
| 当前设计 | `FAngelscriptType` 虽然暴露了 `BindProperty()` 扩展点，但默认实现直接返回 `false`；真正的属性暴露主路径仍在 `Bind_BlueprintType.cpp`，由 binder 根据 `IsObjectPointer()`、`IsUnresolvedObjectPointer()`、`FEnumProperty`、POD size 等规则手工挑选 getter/setter helper。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:265` — `BindProperty()` 是可选虚函数，默认不提供任何统一 accessor 计划。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1082`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1102` — `BindProperties()` 先把 `FProperty` 还原成 `FAngelscriptTypeUsage`，再尝试调用 `Usage.Type->BindProperty(...)`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1138`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1145`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1150` — 如果没有 type-specific override，getter/setter 的脚本形态靠 object pointer / unresolved object / `const T&` 三分法生成。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1203`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1216`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1231` — setter helper 又继续按 enum、1/4/8-byte POD、generic property 分支。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Primitives.cpp:236`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Primitives.cpp:242`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Primitives.cpp:258`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Primitives.cpp:301` — 当前真正实现 `BindProperty()` 的典型是 bitmask bool，它必须自己生成特殊 getter/setter，说明 family 特例已经绕开通用路径。 |
| 优点 | 常见 object/POD/property family 不需要每个类型都写一套 accessor binder，现有 `Bind_BlueprintType.cpp` 就能快速覆盖大量蓝图属性。 |
| 不足 | 新增类型映射时，`CreateProperty()`、`MatchesProperty()` 打通并不代表“原生属性可稳定暴露给脚本”。只要 family 不是现有 object/unresolved/POD/enum 这几路之一，就得继续扩 `Bind_BlueprintType.cpp` 或写专用 `BindProperty()`；`FInterfaceProperty`、future wrapper value、特殊容器视图都会被迫在 TypeSystem 外补第二套分派。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FFunctionDesc` 和属性访问都先走 `FPropertyDesc::Create()`；interface 也是普通 property descriptor，get/set 仍通过 `GetValueInternal()` / `SetValueInternal()` 工作。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:55`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:58`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:533`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:541`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:554`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1537`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1592` | property accessor owner 就是 descriptor factory，新增 family 不需要再去改一个独立的 getter/setter 生成器。 |
| puerts | `FPropertyTranslator` 自带通用 `Getter/Setter`，`StructWrapper` 只负责缓存 translator 并把 accessor 挂到模板；具体 family 由 `FPropertyTranslator::Create()` 集中工厂分派。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:29`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:81`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:598`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:618`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1225`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1308`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:21`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:26`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:154`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:158` | accessor 安装和 family 翻译解耦，新增 interface/container/object wrapper 主要补 translator，不必修改统一 wrapper 安装逻辑。 |
| UnrealCSharp | `FFunctionDescriptor` 遍历 `FProperty` 时直接调用 `FPropertyDescriptor::Factory()`；descriptor 统一承载 `Get/Set/InitializeValue_InContainer`，`FInterfacePropertyDescriptor` 只是其中一个具体 family。 | `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FFunctionDescriptor.cpp:34`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FFunctionDescriptor.cpp:38`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/FPropertyDescriptor.cpp:47`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/FPropertyDescriptor.cpp:87`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/ObjectProperty/FInterfacePropertyDescriptor.cpp:29`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/ObjectProperty/FInterfacePropertyDescriptor.cpp:42`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Public/Reflection/Property/FPropertyDescriptor.inl:57` | property bridge 可以直接复用到函数参数、成员访问和 interface value；不会再额外维护一套“属性暴露专用分派”。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 native property accessor 暴露从 `Bind_BlueprintType.cpp` 的手工分支收敛为结构化 `PropertyAccessorPlan`，让 `FAngelscriptType` family 只回答自己的 accessor 能力，不再反复改中心 binder。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/` 或 `Binds/` 新增 `FAngelscriptPropertyAccessorPlan`，字段至少包含 `SurfaceDeclaration`、`GetterPolicy`、`SetterPolicy`、`bNeedsGeneratedGetter`、`bNeedsGeneratedSetter`、`bSupportsReferenceView`、`bCookSerializable`。 2. 让 `BindProperties()` 先根据 `FProperty + FAngelscriptTypeUsage + FBindParams` 构建 accessor plan，再由 plan 统一下发 `GetObjectFromProperty`、`GetValueFromProperty`、enum/POD setter 等 helper；旧 `BindProperty()` 第一阶段保留为 fallback，只在 plan builder 未覆盖时调用。 3. 首批把现有 bool bitfield、object pointer、unresolved object、enum、1/4/8-byte POD 迁入 plan builder，确保 `Bind_BlueprintType.cpp` 不再自己判断 family。 4. 第二阶段为 `FInterfaceProperty`、future wrapper value 和新容器视图新增 plan creator，要求只增加 creator，不再修改 `BindProperties()` 主流程。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Primitives.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/`, `Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 过渡期 plan builder 与 legacy `BindProperty()` 并存，最容易出现 editor 绑定走新 plan、少量旧 family 仍走旧 override 的双轨状态；需要先把 plan 输出做 manifest 化，确认每个属性命中了哪条路径。 |
| 兼容性 | 对现有脚本属性名、getter/setter 名称和语法可保持向后兼容；变化集中在 C++ 内部 owner 与生成路径，不要求用户改现有脚本。 |
| 验证方式 | 1. 为现有 object/unresolved object/bool/enum/POD 属性生成 accessor snapshot，确认脚本声明与 helper 选择不变。 2. 增加 native `UPROPERTY` 暴露回归，覆盖 editor 和 cooked `AS_USE_BIND_DB` 两条路径。 3. 加一组 `FInterfaceProperty`/future wrapper 原型测试，验证新增 family 只需补 plan creator，不再修改 `Bind_BlueprintType.cpp` 主分支。 |

### Arch-TS-48：`Binds.Cache` 只缓存 declaration 字符串和少量 wrapper flag，cooked rebind 无法结构化表达新 property family

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | property/type 暴露在 cooked bind cache 中的结构化程度 |
| 当前设计 | 当前 `FAngelscriptPropertyBind` 只保存 declaration 字符串、`UnrealPath`、权限位，以及 `bGeneratedHandle` / `bGeneratedUnresolvedObject` 这类少量 wrapper 标志；运行期加载 `Binds.Cache` 后，再由 `Bind_BlueprintType.cpp` 根据这些字符串和布尔位重放 getter/setter 生成逻辑。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h:9`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h:11`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h:17`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h:19`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h:20` — `FAngelscriptPropertyBind` 的持久字段只有 `Declaration`、`GeneratedName` 和两类 wrapper 布尔位，没有 family/inner/object/interface shape。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp:27`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp:29`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp:30` — cache 序列化只写 `Structs` / `Classes`，没有单独的 type-shape sidecar。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1132`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1141`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1146`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1255`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1267` — editor 生成 DB 时，只把 generated getter/setter 的 declaration 和 handle/unresolved-object 标志写回 `DBProp`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:787`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:803`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:808`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:824`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:829`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:842`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:864`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:879` — cooked rebind 时又按 `bGeneratedHandle` / `bGeneratedUnresolvedObject` / enum / POD size / fallback `BindProperty()` 重做一次分派。 |
| 优点 | cache 体积小，实现直接，现有对象属性与简单 POD 属性在 cooked 环境下可以快速恢复绑定，不依赖完整 editor 侧扫描。 |
| 不足 | cache schema 本身不知道 property family 树，只知道“长什么字符串”和“是不是 handle/unresolved object”。新增 `FInterfaceProperty`、future wrapper、复杂 accessor policy 或 container-wrapper 组合时，不仅 editor 侧要会生成，cooked 侧还得扩 cache 字段与 loader 分支；否则同一个类型 family 很容易出现 editor 可用、cook 后降级或丢失的分叉。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 调用和属性桥接直接从 live `FProperty` 创建 `FPropertyDesc`；interface family 也是 runtime descriptor factory 的普通分支，不依赖一份单独的 declaration cache。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:55`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:58`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1537`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1592`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1615` | cooked/runtime 侧的 canonical source 仍是 `FProperty` family，自然不会为了新 family 再发明 cache flag。 |
| puerts | `StructWrapper` 访问属性时只缓存 `FPropertyTranslator`，translator 由统一 factory 创建；getter/setter 本身复用 translator，不把 property 形状降成 declaration 字符串持久化。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:21`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:26`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:154`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:158`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1225`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1308`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1390` | property cache 保存的是 translator owner，而不是“已经摊平的语法字符串”，扩展点停在 factory。 |
| UnrealCSharp | `FFunctionDescriptor` 和 `FPropertyDescriptor::Factory()` 都在运行时按真实 `FProperty` 建 descriptor；descriptor 还自带 `InitializeValue_InContainer`、`GetSize`、`Set` 等操作。 | `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FFunctionDescriptor.cpp:34`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FFunctionDescriptor.cpp:38`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/FPropertyDescriptor.cpp:47`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/FPropertyDescriptor.cpp:87`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Public/Reflection/Property/FPropertyDescriptor.inl:57`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Public/Reflection/Property/FPropertyDescriptor.inl:65` | 即使要跨 runtime/codegen 共享信息，也优先共享结构化 property descriptor，而不是缓存“声明字符串 + 少数特例位”。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 给 `Binds.Cache` 增加结构化 `PropertyShape` / `AccessorShape`，让 cooked rebind 先按 family 恢复语义，再把 declaration 字符串降级为显示层或 legacy fallback。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/` 新增 `FAngelscriptSerializedPropertyShape`，字段至少包含 `Family`、`WrapperKind`、`ObjectClassPath`、`InterfaceClassPath`、`ElementSizeHint`、`AccessorPolicy`、可选 `Inner/Key/Value` 子 shape。 2. 扩展 `FAngelscriptPropertyBind`：第一阶段双写旧 `Declaration` / `bGeneratedHandle` / `bGeneratedUnresolvedObject` 与新 shape；旧 reader 继续可读，新 reader 优先消费 shape。 3. 把 `Bind_BlueprintType.cpp` 的 DB 加载路径改成先由 `PropertyShape + live FProperty` 生成 accessor plan，再统一绑定 getter/setter；只有旧 cache 或 shape 缺失时才回退到当前 declaration-string 分支。 4. 首批迁移现有 generated getter/setter 覆盖的 object、unresolved object、enum、POD 四类；第二批为 `FInterfaceProperty`、future wrapper family 和 container-wrapper 组合增加 shape，而不再扩布尔位。 5. 给 `Binds.Cache` 增加版本号和升级策略；如果读到旧版本，就在 editor/cook 时自动重建 cache，而不是让新 family 在 runtime 静默退回 legacy 分支。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/`, `Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | cache 版本升级期最容易出现 editor 写新 shape、旧 cooked 包仍按旧 reader 读取的兼容问题；必须显式版本化并在加载失败时给出重建诊断，而不是默默 fallback。 |
| 兼容性 | 对现有脚本 API 和已发布绑定名可保持向后兼容；对旧 cache 文件需要提供兼容读取或自动重建。用户脚本不需要修改，但构建产物可能需要重新生成 `Binds.Cache`。 |
| 验证方式 | 1. 生成一次新旧两版 `Binds.Cache` 对照，确认现有属性 declaration 和 accessor 行为不变。 2. 在 `AS_USE_BIND_DB` 开启的 cooked-like 路径下回归 object/unresolved object/bool/enum/POD 属性绑定。 3. 增加 `FInterfaceProperty`/future wrapper 原型缓存测试，验证 editor 路径与 DB 加载路径命中同一 `PropertyShape`/accessor plan。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-TS-47 | 原生属性暴露缺少统一 accessor descriptor，新增 family 需要同时改 type adapter 与 `Bind_BlueprintType.cpp` | owner 收敛 | 高 |
| P1 | Arch-TS-48 | `Binds.Cache` 只缓存 declaration 字符串与少量 wrapper flag，cooked rebind 无法结构化表达新 property family | 缓存模型升级 | 中-高 |

---

## 架构分析 (2026-04-09 00:46)

### Arch-TS-49：成员物化理由仍被压在 GC 补丁通道里，`Replication/Serialization/Editor` 语义没有独立的 `PropertyMaterialization` owner

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 动态成员何时必须落成真实 `FProperty`，以及落成后应携带哪些 UE 语义 |
| 当前设计 | 当前 class analysis 先基于 GC 需求决定是否为“没有 `UPROPERTY()` 的脚本成员”补 hidden property；这条 lane 默认 `Transient`、非 Blueprint 可见。真正的 `CPF_Net` / `CPF_RepNotify` 则只在后续 exported-property lane 里补，运行时复制列表也只枚举真实 `CPF_Net` 字段。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:295`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:298`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:301`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:308` — hidden property 的进入条件只围绕 “GC 需要真实 property” 组织。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:318`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:329`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:336`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:338` — 这条 synthetic lane 新建 `FAngelscriptPropertyDesc` 后默认把非 struct 成员标成 `Transient`，并且不带作者层的 replication/editor 意图。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2923`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2933`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2945`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2955` — 真正 exported 的 `FProperty` 在 materialization 时才写 `bHasUnrealProperty`、`CPF_Net`、`CPF_RepNotify`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:894`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:899`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:901` — `GetLifetimeScriptReplicationList()` 只扫描类上真实存在且带 `CPF_Net` 的 `FProperty`。 |
| 优点 | 现有实现允许脚本尾部成员在不显式暴露 `UPROPERTY` 的情况下仍接入 UE GC，避免把所有脚本字段都强制升级成反射字段。 |
| 不足 | “为什么要物化成 `FProperty`” 目前只有 GC 这一条显式 reason；`Replication`、`Serialization`、`Editor`、future `FInterfaceProperty`/wrapper value family 如果也需要真实 `FProperty`，只能继续在 `ClassGenerator` 里补分支。结果是新增 family 不是回答一次 materialization policy，而是分别改 hidden-property 启发式、property flags、runtime replication list 和后续验证路径。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `ClassDesc` / `FunctionDesc` 只围绕 live `FProperty` / `UFunction` 创建 descriptor；桥接 owner 是 descriptor，而不是再补一条“仅为 GC 存在”的 synthetic property lane。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/ClassDesc.cpp:129`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/ClassDesc.cpp:131`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:55`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:58` | property/value 语义与 UE 字段语义停留在同一层；没有单独的 GC-only property 模型，因此 replication/editor 语义不会漂到另一条旁路上。 |
| puerts | `StructWrapper` 初始化模板属性时直接遍历现有 `FProperty`，每个字段交给 `FPropertyTranslator::Create()`；不支持的 family 只是没有 translator，不会先额外注入 hidden property。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:146`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:154`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:158`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1390` | 字段桥接与 UE 字段存在性绑定在一起；扩展点是 translator factory，而不是“再造一条 synthetic property lane”。 |
| UnrealCSharp | 动态生成属性时先判断 `Property->IsUProperty()`，随后统一 `FTypeBridge::Factory()` + `SetFlags()`，同一条 property materialization 里直接写入 `CPF_Net` / `CPF_RepNotify` 等反射语义。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGeneratorCore.cpp:929`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGeneratorCore.cpp:931`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGeneratorCore.cpp:935`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGeneratorCore.cpp:393`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGeneratorCore.cpp:395`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGeneratorCore.cpp:402`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGeneratorCore.cpp:404` | 物化 owner 先显式决定“这是不是真实 `UProperty`”，再把 replication/editor/serialization flag 一次性挂上；不会把 GC 与其它 UE 语义拆成两条不同的 property policy。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 新增结构化 `PropertyMaterializationPlan`，把 `GC`、`Replication`、`Serialization`、`EditorExposure`、`ConstructionSlot` 等 reason 从当前 GC-only 启发式里拆出来；hidden property 退化为 plan 的一种结果，而不是唯一 owner。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/` 或 `Core/` 新增 `FAngelscriptPropertyMaterializationPlan`，字段至少包含 `StorageKind(ScriptTail/HiddenTransientProperty/ReflectedProperty)`、`Reasons`、`PropertyFlags`、`bCanReplicate`、`bCanSerialize`、`bCanEdit`。 2. 在 class analysis 阶段基于现有 `NeverRequiresGC()` / `RequiresProperty()` / `bReplicated` / metadata 构建 plan；首阶段输出要与当前行为完全对齐，让 GC-only 成员仍落到 `HiddenTransientProperty`。 3. 把 `AngelscriptClassGenerator.cpp` 里 hidden-property 创建、exported-property flag 写入、`GetLifetimeScriptReplicationList()` 的前置校验全部改为消费 plan；如果成员声明了 replication/editor/serialization 意图但 plan 仍是 `HiddenTransientProperty`，在生成阶段直接给稳定报错。 4. 先迁移 object-like family 与一个 P10 `FInterfaceProperty` 原型，验证“需要真实 `FProperty` 参与 UE 语义”的 family 不再只能借 GC lane 旁路进入系统。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | plan 与旧启发式并存期间，最容易出现“GC lane 仍按旧规则补 hidden property，但 replication/editor 校验已切到新 plan”的双轨状态；第一阶段必须让 plan 反向生成旧行为。 |
| 兼容性 | 对现有脚本语法与已有成员声明保持向后兼容；变化主要体现在生成期诊断更严格。少数以前“能过编译但实际只被当成 transient GC 字段”的组合，可能会被升级成显式错误。 |
| 验证方式 | 1. 增加脚本类成员矩阵测试：GC-only hidden property、显式 reflected property、replicated property、future `FInterfaceProperty` 原型。 2. 回归 `GetLifetimeScriptReplicationList()`，确认只有 `ReflectedProperty` lane 参与 replication，且 `CPF_Net/RepNotify` 行为与迁移前一致。 3. 对 `DefaultComponent` / editor 可见元数据做回归，确保 plan 引入后不会把原有构造器/细节面板行为改坏。 |

### Arch-TS-50：类型依赖预热仍是零散的 `EnsureReloaded()` 调用，而且当前实现只真正处理 class，不处理 delegate

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 动态 type shell 在 property / function / delegate materialization 之前的依赖完成顺序 |
| 当前设计 | 当前仓库没有独立的 type dependency graph。某些 class-member property 在物化前会按 `TypeId` 手工调用 `EnsureReloaded()`，但这条预热只在少数 site 触发；更关键的是 `EnsureReloaded(int TypeId)` 虽然能解析出 `DelegateDataPtr`，最终却只对 `ClassDataPtr` 真正执行 reload。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2907`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2917`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2918` — class property materialization 只在“top-level 是 object value type”时额外手工 `EnsureReloaded(TypeId)`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2525`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2535`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2544`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2546` — `EnsureReloaded(int TypeId)` 会递归 subtype 并调用 `GetDataFor(...)`，但真正只在 `ClassDataPtr != nullptr` 时执行 reload。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3131`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3149`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3156` — `GetDataFor(asITypeInfo*)` 明确可以返回 `DelegateData`，说明当前 `EnsureReloaded()` 的 class-only 行为不是数据模型限制，而是调用路径遗漏。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3903`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3909`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3935`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3966`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3976` — delegate signature与普通函数参数/返回值物化时直接 `CreateProperty()`，没有统一 dependency-preflight。 |
| 优点 | 现有逻辑对今天最常见的“脚本类成员依赖另一个脚本类 shell”场景够直接，额外开销也比较低。 |
| 不足 | 依赖完成顺序变成了 site-specific、family-specific 的约定：property lane 有一部分手工预热，function/delegate lane 没有；`EnsureReloaded()` 又天然偏向 class。未来只要出现 script delegate 参数/返回值、`interface` callable 依赖、container 包着动态 delegate/interface shell 之类的新 family，就会把“是否已完成 reload”变成调用顺序问题，而不是 TypeSystem 自身保证的前置条件。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FFunctionDesc` 直接遍历 live `UFunction` 参数列表并立刻 `FPropertyDesc::Create(Property)`；`ClassDesc` 注册字段时也是“拿到 `FProperty/UFunction` 就建 descriptor”，没有额外的 type-preload lane。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:55`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:58`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/ClassDesc.cpp:129`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/ClassDesc.cpp:138` | steady-state 直接消费已完成的 UE reflection graph；因此不会再引入“某个 family 需要先 EnsureReloaded，另一个 family 不需要”的旁路约定。 |
| puerts | `FunctionTranslator::Init()` 逐个参数读取真实 `FProperty`，返回值和参数都通过统一 `FPropertyTranslator::Create()` factory 建 translator。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp:120`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp:129`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp:131`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp:135` | callable 依赖前置条件就是“UE reflection 已经存在”；translator factory 本身不承担 site-specific reload 排序。 |
| UnrealCSharp | `FDynamicGeneratorCore` 把依赖顺序提升成显式 `FDynamicDependencyGraph` 服务，随后 property/function 生成都统一走 `FTypeBridge::Factory()`。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGeneratorCore.cpp:58`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGeneratorCore.cpp:73`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGeneratorCore.cpp:78`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGeneratorCore.cpp:945`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGeneratorCore.cpp:967`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGeneratorCore.cpp:984` | 把“谁先生成、谁依赖谁”收敛成单独 owner 后，property/function materialization 就能保持统一 factory，不必每个 site 自己补 `EnsureReloaded()`。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把当前零散的 `EnsureReloaded()` 升级成显式 `TypeDependencyPlan/ReloadDependencyGraph`，让 class、delegate、future interface shell 都能在 materialization 前走同一条依赖完成路径。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/` 新增 `FAngelscriptTypeDependencyPlan`，至少记录 `DependencyKind(Class/Struct/Delegate/Interface)`、`TypeId`、`TypeInfo*`、`Reason(MemberProperty/FunctionArg/FunctionReturn/ContainerSubType)`。 2. 先修正 `EnsureReloaded(int TypeId)`：保留现有递归 subtype 逻辑，但在 `GetDataFor(...)` 命中 `DelegateDataPtr` 时也执行 delegate reload；同时输出 debug trace，便于比对旧行为。 3. 在普通 class property、`AddFunctionReturnType()`、`AddFunctionArgument()`、`DoFullReload(FDelegateData&)` 之前统一构建并解析 dependency plan；原有 property-lane 的手工 `EnsureReloaded()` 第一阶段保留为 fallback。 4. 先用 script delegate 参数/返回值、container<delegate>、P10 interface callable 原型三类用例做试点；确认 plan 稳定后，再把更多 reload 前置判断从 site-specific `if` 里迁走。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 最大风险是把原本“偶然可工作”的顺序依赖显式化后，会提前暴露 reload cycle 或缺失 metadata；首阶段应先做 graph dump + 只读验证，再切换强制调度。 |
| 兼容性 | 对现有脚本语法与运行时 API 保持向后兼容；变化集中在 reload 顺序与生成期诊断。少量依赖旧顺序的边缘 case 可能从运行时异常变成更早的生成期错误。 |
| 验证方式 | 1. 增加 script delegate 参数/返回值、`TArray<Delegate>`、future interface callable 原型测试，确认不再依赖调用顺序碰运气。 2. 对 `EnsureReloaded()` 增加 trace/snapshot，验证 class 与 delegate 节点都会进入 plan。 3. 回归 hot reload，确认引入 dependency plan 后普通 class property materialization 不回退、不重复 reload。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P0 | Arch-TS-50 | 类型依赖预热仍是 site-specific 的 `EnsureReloaded()`，且当前实现漏掉 delegate | 依赖图收敛 | 高 |
| P1 | Arch-TS-49 | 动态成员的 `GC/Replication/Serialization/Editor` 物化理由没有独立 owner | property materialization owner 收敛 | 中-高 |

---

## 架构分析 (2026-04-09 01:01)

### Arch-TS-51：generic family 的可嵌套性仍是 `CanBeTemplateSubType()` 单布尔门禁，容器与 interface/wrapper 组合能力表现成隐式矩阵

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 新 generic family 与现有容器/wrapper/interface 组合时的扩展面 |
| 当前设计 | 当前 TypeSystem 对“某个类型能否作为另一个模板实参”只有 `CanBeTemplateSubType()` 这一位，再叠加少量额外 trait（例如 `CanHashValue()`）做 family-specific 判定；property/materialization 明明已经是递归式实现，但 generic 准入策略仍是散落在各 binder 里的布尔门禁。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:193` — `FAngelscriptType` 只提供 `virtual bool CanBeTemplateSubType() const` 一个模板嵌套能力位。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp:68`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSet.cpp:65`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TOptional.cpp:26` — `TMap` / `TSet` / `TOptional` 都通过覆写这个布尔位整族 opt-out。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TOptional.cpp:108`-`123` — `TOptional` 的 property bridge 本身已经是递归的，直接 `SetValueProperty(Usage.SubTypes[0].CreateProperty(...))`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TOptional.cpp:339`-`345` — 但运行时模板校验又单独用 `if (!Type.CanBeTemplateSubType())` 拒绝 subtype。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp:108`-`143` — `TMap::CreateProperty()` 同样递归创建 key/value property。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp:1303`-`1318` — `TMap` 的模板准入则额外叠加 `CanBeTemplateSubType()` 与 `CanHashValue()` 两个正交条件。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp:95`-`102`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp:1741`-`1767` — `TArray` 也是递归创建 inner property，但模板校验只问 subtype 的布尔位与生命周期 trait；从代码看，并不存在一个集中声明“哪些 family 可嵌到 `Array/Map/Set/Optional` 哪个槽位”的 owner。 |
| 优点 | binder 内部实现直接，family 作者只要补少数布尔/trait 就能先把模板类型跑起来。 |
| 不足 | generic 兼容性不是结构化矩阵，而是“`CanBeTemplateSubType()` + 若干额外 trait”的 emergent behavior。对新增 `TScriptInterface<>`、新容器、或 wrapper 组合而言，问题不再是“它支不支持 property recursion”，而是“它应该把哪些 family 作为 element/key/value/optional-value 接受”，而当前 API 没有表达这层语义的地方。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FPropertyDesc::Create()` 直接按 `FProperty` family 建 descriptor；`CPT_Interface`、`CPT_Array`、`CPT_Map`、`CPT_Set` 都是并列 case，interface 值也直接落到 `FScriptInterface`。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:533`-`560`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1537`-`1627`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1735`-`1759` | 可嵌套性由“inner `FProperty` 是否可描述”决定，而不是由 family 自己回一个全局布尔位。 |
| puerts | `PropertyTranslatorCreator::Do()` 用单一工厂覆盖 `InterfaceProperty`、`ArrayProperty`、`MapProperty`、`SetProperty`；array/set/map translator 都递归持有 inner/key/value property。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:598`-`630`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:862`-`980`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1225`-`1320` | generic family 的准入和 marshalling 都停在 property family factory，上层不需要再维护一套独立的“模板 subtype 布尔矩阵”。 |
| UnrealCSharp | `FTypeBridge::GetClass()` 与 `FGeneratorCore::IsSupported()` 都按 `FInterfaceProperty/FArrayProperty/FMapProperty/FSetProperty/FOptionalProperty` 递归求值。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:415`-`455`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:562`-`608`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:720`-`786` | 先得到结构化 family tree，再决定 generic 是否 supported；“array element”“map key”“optional value” 是显式角色，而不是共享一位 `CanBeTemplateSubType()`。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把当前单布尔 `CanBeTemplateSubType()` 升级成按 family/slot 求值的 `TemplateEmbeddingPolicy`，让 generic 兼容性从隐式 emergent matrix 变成显式规则表。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/` 新增 `FAngelscriptTemplateEmbeddingPolicy`，至少区分 `ArrayElement`、`MapKey`、`MapValue`、`SetElement`、`OptionalValue` 五种 slot，并允许返回 `Allowed / Rejected / AllowedWithHash` 这类结构化结果。 2. 保留 `CanBeTemplateSubType()` 作为兼容壳，第一阶段只把它映射成 “所有 slot 统一 Allowed/Rejected”；随后让 `Bind_TArray.cpp`、`Bind_TMap.cpp`、`Bind_TSet.cpp`、`Bind_TOptional.cpp` 的模板 validator 统一改查 policy，而不是各写一套布尔组合。 3. 用现有行为先对齐第一版 policy：`TMap` key 仍要求 hash，`TOptional/TMap/TSet` 默认维持当前 reject matrix，不改变已有脚本可编译性。 4. 第二阶段再为 P10 增加 `InterfaceValue`/`ObjectWrapper` family 的 slot 规则，例如先只开放 `TArray<TScriptInterface<...>>` 与 `TOptional<TScriptInterface<...>>`，而不是一次性放开全部 nested generic 组合。 5. 在 `AngelscriptTest` 增加 generic compatibility snapshot，明确记录每个 family 在五种 slot 上的判定，避免新类型映射继续靠“试出来”的矩阵演化。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSet.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TOptional.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 如果第一阶段直接放开更多组合，很容易把 existing container ABI、hash 语义和 StaticJIT `GetCppForm()` 一起带进未知组合；应先把 policy 显式化，再按 family 逐步开放。 |
| 兼容性 | 首阶段可保持当前脚本编译通过/拒绝矩阵不变，只是把 owner 从 scattered bool checks 收敛成统一 policy。后续新增 `TScriptInterface<>` 等组合能力应以 opt-in 方式逐步开放。 |
| 验证方式 | 1. 增加 `TArray/TMap/TSet/TOptional` 的 template compatibility snapshot。 2. 回归现有 `TArray<int>`、`TMap<FName,int>`、`TSet<FName>`、`TOptional<int>` 行为保持不变。 3. 补 `TArray<TScriptInterface<...>>`、`TOptional<TScriptInterface<...>>` 原型测试，确认新 family 只需补 policy 与 adapter，不再同步修改四套 validator。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-TS-51 | generic family 的可嵌套性仍由单布尔 gate 决定 | 兼容矩阵显式化 | 高 |

---

## 架构分析 (2026-04-09 01:03)

### Arch-TS-52：`GetAngelscriptDeclaration()` 已从“类型打印器”演化成上下文策略通道，新增 family 需要同时理解声明文本与绑定语义

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 类型表面声明（script declaration）与实际绑定/访问策略的 owner 边界 |
| 当前设计 | 当前 TypeSystem 不只是把类型打印成 Angelscript 文本；`GetAngelscriptDeclaration()` 还要根据 `MemberVariable / FunctionArgument / FunctionReturnValue / PreResolvedObject / MemberVariable_InContainer` 等 mode 改变文本形态，并被 `BuildFunctionDeclaration()`、native property accessor 生成、`Binds.Cache` 持久化直接消费。换句话说，声明字符串已经携带了部分 binding policy。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:109`-`120` — `FAngelscriptType` 公开 `EAngelscriptDeclarationMode`，把 type printer 设计成 mode-sensitive 接口。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:174`-`193` — 基类打印模板类型时，会把 outer `MemberVariable` 自动降成 inner `MemberVariable_InContainer`，说明打印器本身承担了容器上下文传播。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:196`-`207` — `FAngelscriptTypeUsage::GetAngelscriptDeclaration()` 再在字符串外层补 `const` 与 `&`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:570`-`636` — `BuildFunctionDeclaration()` 直接依赖 `FunctionReturnValue` / `FunctionArgument` 两种 mode 组装函数签名。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Primitives.cpp:344`-`356` — 连 `float`/`double` 这样的 primitive family 都需要按 mode 改写成 `float32` / `float64`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1799`-`1817` — unresolved object family 在 `MemberVariable` mode 下会额外输出 `unresolved_object` classifier，在 `PreResolvedObject` mode 下又切回普通对象声明。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1135`-`1150`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1262`-`1267` — property accessor 生成和 `DBProp.Declaration` 持久化都直接使用这些 mode-sensitive 文本来决定 getter/setter 形状和 cache 内容。 |
| 优点 | 现有实现让 binder 能快速用同一入口生成“函数签名文本”“成员声明文本”“pre-resolved accessor 文本”，对今天的脚本 API 足够直接。 |
| 不足 | 一旦新增 type family 或新增暴露场景，工作不只是“补 property/ABI adapter”，还要回答“它在每个 declaration mode 下长什么样”。而这些 mode 并不只是展示用途，已经反过来影响 accessor 生成、cache 和 unresolved-object 语义。对 `TScriptInterface<>` 这类新 family，这意味着 surface syntax 与 runtime policy 会继续一起膨胀。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 运行时函数桥接直接遍历真实 `FProperty` 并创建 `FPropertyDesc`；默认参数由独立 `DefaultParamCollection` 注入，运行时不依赖 per-type declaration mode 字符串。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:55`-`60`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:323`-`330` | runtime binding 先消费结构化 property descriptor，文本声明不是策略 owner。 |
| puerts | `StructWrapper` 以 `FProperty` 为 key 缓存 translator，translator 工厂再按 property family 分派 `Interface/Array/Map/Set/...`；wrapper 安装不通过 type printer 推导行为。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:21`-`35`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1225`-`1320` | value bridge 与 API 暴露通过 property translator/ wrapper descriptor 协作，避免把上下文策略编码进类型显示名。 |
| UnrealCSharp | codegen 侧的 `GetPropertyType()` / `GetPropertyTypeNameSpace()` 直接按 `FProperty` family 递归生成 `TScriptInterface<>`、`TArray<>`、`TMap<>` 等 surface type，同时 `IsSupported()` 也是按同一 property tree 判断。 | `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:132`-`166`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:328`-`415`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:720`-`786` | surface declaration 依赖结构化 property tree，而不是让每个 runtime type family 自己解释多种 mode；打印和 support 判定共享同一数据源。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 `GetAngelscriptDeclaration()` 从“兼做策略的虚函数”降成 formatter，先引入结构化 `SurfaceTypeSpec`，再让函数签名、property accessor 和 cache 都消费同一份 spec。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/` 新增 `FAngelscriptSurfaceTypeSpec`，至少包含 `Family`、`WrapperKind`、`Qualifiers`、`Role(Member/Return/Argument/Accessor)`、`ResolutionPolicy(Resolved/PreResolved/Unresolved)`、`SubTypes`。 2. 让 `FAngelscriptTypeUsage` 新增 `BuildSurfaceTypeSpec(Role)`；现有 `GetAngelscriptDeclaration()` 第一阶段只负责把 spec format 成旧文本，保证外部可见脚本声明不变。 3. 把 `BuildFunctionDeclaration()`、`Bind_BlueprintType.cpp` 的 accessor 生成、`DBProp.Declaration` 写入先改为消费 spec，再由统一 formatter 输出文本；object/unresolved-object、primitive alias、future interface family 都不再各自在 `GetAngelscriptDeclaration()` 里夹带策略。 4. 第一批只迁移 `primitive`、`UObject`、`unresolved_object`、`TSubclassOf`、`TObjectPtr`、`TWeakObjectPtr` 五类最依赖 mode 的 family；第二批再把 P10 的 `TScriptInterface<>` / `FInterfaceProperty` 接到同一 surface-spec 管线。 5. 在测试层增加 declaration snapshot：同一 `TypeUsage` 在 `Member/Argument/Return/Accessor` 四种 role 上必须从同一 spec 派生，避免未来再出现“展示文本”和“绑定文本”分叉。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Primitives.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UStruct.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 迁移期会并存“旧 mode-sensitive printer”和“新 surface spec formatter”；如果 accessor/cache 只迁对一半，最容易出现脚本展示文本正确但 DB/accessor 仍按旧模式解释的双轨状态。 |
| 兼容性 | 对现有脚本声明语法与已生成 binding 文本可保持向后兼容；首阶段只改变内部 owner，不要求用户改写 API 或重新学习声明规则。 |
| 验证方式 | 1. 增加 declaration snapshot，覆盖 `float/double` alias、`UObject*`、`unresolved_object`、`TSubclassOf`、`TObjectPtr`、`TWeakObjectPtr`。 2. 回归 property accessor 与 `Binds.Cache` 生成，确认迁移前后 declaration 文本和 getter/setter 行为一致。 3. 增加 `TScriptInterface<>` 原型用例，验证新 family 只需补 `SurfaceTypeSpec + formatter`，不必再向多个 mode 分支各写一遍字符串逻辑。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-TS-52 | declaration printer 已承载上下文策略，新增 family 需要同时补文本与绑定语义 | surface spec 收敛 | 中-高 |

---

## 架构分析 (2026-04-09 01:16)

### Arch-TS-53：script-defined 引用类型在 `FromTypeId()/FromClass()` 入口被压成单一 `ScriptObjectType`，`UInterface` 只能在后置阶段再被重新识别

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | script-defined 引用类型 family 的一等建模能力 |
| 当前设计 | 当前 TypeSystem 为 script-defined 类型预留了 `ScriptObject/ScriptStruct/ScriptDelegate/ScriptMulticastDelegate/ScriptEnum` 几个 singleton family，但没有独立的 script interface 引用 family。结果是 `FromTypeId()` 与 `FromClass()` 会把非值类型的 script-defined 引用统一折叠到 `GetScriptObject()`，后续再由 `userData`、`CLASS_Interface`、`ImplementedInterfaces` 这些旁路信息重新推断“它其实是不是 interface”。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:46`-`58`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:599`-`603` — 数据库里只有 `ScriptObjectType`、`ScriptStructType`、`ScriptDelegateType`、`ScriptMulticastDelegateType`、`ScriptEnumType`，没有对等的 `ScriptInterfaceType`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:367`-`406` — `FromTypeId()` 对 `asOBJ_SCRIPT_OBJECT` 分支里，非值类型直接落到 `FAngelscriptType::GetScriptObject()`，只把真实 `asITypeInfo*` 塞进 `Usage.ScriptClass`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:291`-`305` — `FromClass()` 在 `UASClass` 路径上同样直接回落到 `GetScriptObject()`，不区分普通脚本类与脚本 interface shell。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:210`-`231` — `GetClass()` 对这类 usage 也只是泛化地把 `ScriptClass->GetUserData()` 解释成 `UClass*`，family 本身不表达 interface 语义。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:112`-`127` — quick cast 到 Unreal interface 时，只能把 `TargetType->GetUserData()` 再解释成 `UClass*`，随后靠 `HasAnyClassFlags(CLASS_Interface)` 与 `ImplementsInterface()` 重新判断。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2081`-`2088`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2762`-`2804`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:5135`-`5139` — interface 的特殊语义只能在 reload/materialization/finalization 阶段通过 `bIsInterface`、方法声明字符串、`FImplementedInterface` 再次补回。 |
| 优点 | 对现有普通脚本类引用路径足够省事，`TypeSystem` 入口面较小，raw script object 支持能快速复用同一套 adapter。 |
| 不足 | `UInterface` 不是在 type-family 层进入系统，而是在多个后置阶段“被重新发现”。这会让 P10 的 `FInterfaceProperty`、`TScriptInterface<>`、reflective fallback 和 UHT 支持继续沿着不同旁路演化；新增一种 script-defined 引用家族时，也很难只改一处 family registry。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FPropertyDesc::Create()` 在 factory 层就把 `ObjectReference` 与 `Interface` 分成不同 descriptor；`FObjectPropertyDesc` 管 object/class 语义，`FInterfacePropertyDesc` 单独负责 `FScriptInterface`。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:268`-`277`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:533`-`560`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1537`-`1595` | “object family” 和 “interface family” 在入口就分流，后续 value bridge 不需要再从 class flag 倒推。 |
| puerts | `PropertyTranslatorCreator::Do()` 把 `ObjectProperty`/`WeakObjectProperty`/`LazyObjectProperty` 统一派到 `FObjectPropertyTranslator`，而 `InterfaceProperty` 进入独立的 `FInterfacePropertyTranslator`；两者共享 object mapper，但 family 边界是显式的。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:463`-`492`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:598`-`630`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1225`-`1310` | 可以共享底层对象包装器，但不能把 interface family 压回 generic object translator。 |
| UnrealCSharp | `FTypeBridge::GetClass(const FObjectProperty*)` 直接返回对象类，而 `GetClass(const FInterfaceProperty*)` 明确生成 `TScriptInterface<>` 的 generic reflection type。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:388`-`430` | type bridge 在 bridge-level 就承认 object 与 interface 是不同 reference family，这让 codegen/runtime/property descriptor 可以共享同一分类。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保持现有 `GetScriptObject()` 兼容面的前提下，引入显式 `ScriptReferenceKind`，让 script-defined object/interface/delegate 家族在 TypeSystem 入口就完成一次分类，而不是等到 class flag 阶段再补推断。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h` 新增 `FAngelscriptScriptReferenceDesc` 或 `EScriptReferenceKind`，至少区分 `Object`、`Interface`、`Delegate`、`MulticastDelegate`、`StructValue`。 2. 修改 `FAngelscriptTypeUsage::FromTypeId()` 与 `FromClass()`：先基于 registry / `ClassDesc` / 已知 `UClass` metadata 判定 reference kind，再决定是落到 legacy `GetScriptObject()` 还是新的 `GetScriptInterface()`/generic reference adapter；首阶段判不出时仍回退 `Object`，不改变现有脚本行为。 3. 让 `CanCastScriptObjectToUnrealInterface()`、`BlueprintCallableReflectiveFallback`、future `FInterfaceProperty` adapter 与 UHT support 先查这份 kind，而不是继续各自看 `GetUserData()` + `CLASS_Interface`。 4. 为 `ClassGenerator` 增加统一的 “script reference family -> class materialization policy” 查询，让 `bIsInterface`、`ImplementedInterfaces` 这些后置规则变成对 family desc 的消费，而不是 family 本身的唯一来源。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/`, `Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 迁移期最敏感的是 “legacy `ScriptObject` 推断” 与 “new reference kind” 双轨并存；如果一个调用点仍按 class flag 猜、另一个调用点已经按 kind 分流，interface 会出现半迁移行为。 |
| 兼容性 | 对现有脚本类、脚本对象引用和现有语法可保持向后兼容；第一阶段只是内部 family 分类变得显式，不要求用户改写 `class`/`interface` 定义。 |
| 验证方式 | 1. 增加 `TypeId -> TypeUsage -> FamilyKind` snapshot，覆盖普通脚本类、脚本 interface、delegate、multicast delegate。 2. 回归 script class / script interface 的 quick cast、reload 和 `FromClass()` 结果，确认 interface 不再晚到 class-flag 阶段才被认出来。 3. 为 P10 增加 `FInterfaceProperty`/`TScriptInterface<>` 原型测试，验证新增 adapter 只需接入 new kind，而不是再加一条旁路推断。 |

### Arch-TS-54：`FAngelscriptTypeUsage::SubTypes` 只有匿名位置语义，容器与 wrapper family 缺少共享的 role-tagged type shape

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | generic/container/wrapper family 的结构化类型形状 |
| 当前设计 | `FAngelscriptTypeUsage` 只提供一个匿名 `TArray<FAngelscriptTypeUsage> SubTypes`，TypeSystem 本身不表达这些子节点分别是 `Element`、`Key`、`Value`、`MetaClass` 还是 `InterfaceClass`。容器和 wrapper family 只能在各自 binder 里约定 “`SubTypes[0]` 代表什么”。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:349`-`357` — `FAngelscriptTypeUsage` 的结构里只有 `SubTypes`，没有 role/tag 字段。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:174`-`189` — 基础 declaration printer 也是按顺序遍历 `Usage.SubTypes[i]`，没有共享的 subtype 角色语义。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp:88`-`103` — `TArray` 默认把 `SubTypes[0]` 当 element。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSet.cpp:91`-`107` — `TSet` 同样把 `SubTypes[0]` 当 element，并在同一位置叠加 hash 语义。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp:108`-`140` — `TMap` 私有约定 `SubTypes[0]` 是 key、`SubTypes[1]` 是 value，并把这套位置约定延伸到 layout 计算与 property 命名。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TOptional.cpp:108`-`123` — `TOptional` 再把 `SubTypes[0]` 当 value。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1557`-`1562`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1819`-`1824`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:2123`-`2128` — `TSubclassOf`、`TObjectPtr`、`TWeakObjectPtr` 也都私有约定 `SubTypes[0]` 就是 nominal class/object class。 |
| 优点 | 数据模型轻，现有一元/二元模板 family 很好接入，基础 declaration 拼接也足够直接。 |
| 不足 | 位置约定一旦离开具体 binder 就会丢失语义。未来 `TScriptInterface<>`、soft/weak/interface 复合 wrapper、甚至 “对象槽 + 接口槽” 这类双槽值语义，都只能继续发明新的隐式下标约定；这会让 runtime bridge、class generator、UHT/codegen 和测试层无法共享一份结构化类型树。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | descriptor 自带显式角色字段：`FInterfacePropertyDesc` 持有 `InterfaceProperty`，`FArrayPropertyDesc` 持有 `InnerProperty`，`FMapPropertyDesc` 持有 `KeyProperty` 与 `ValueProperty`。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:533`-`560`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:738`-`760`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:890`-`915` | family 结构不是靠 subtype 下标隐式表达，而是靠显式命名字段保留。 |
| puerts | `FInterfacePropertyTranslator` 直接持有 `InterfaceProperty`；array/map translator 则分别消费 `ArrayProperty->Inner` 与 `MapProperty->KeyProp/ValueProp`，容器 role 停留在 property family 结构里。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:598`-`630`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:862`-`885`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:961`-`985` | translator 可以共享容器/对象 mapper，但前提是 `Element/Key/Value/InterfaceClass` 这些角色在结构里已经是显式的。 |
| UnrealCSharp | `FTypeBridge` 直接按 `InterfaceClass`、`Inner`、`KeyProp`、`ValueProp` 递归生成 reflection type；类型树的每个节点都有明确角色。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:415`-`430`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:446`-`454`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:562`-`578` | 一旦 role-tagged shape 明确，generic reflection、support 判定和 marshalling 都能沿同一棵树工作。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在不立即移除 `SubTypes` 的前提下，先给 `TypeSystem` 增加一层 role-tagged `TypeShape`/`TypeComponent` 视图，让容器、wrapper 与 future interface family 共享结构化类型形状。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/` 新增 `FAngelscriptTypeShape` 与 `EAngelscriptTypeRole`，第一版至少覆盖 `Element`、`Key`、`Value`、`MetaClass`、`ObjectClass`、`InterfaceClass`。 2. 让 `FAngelscriptTypeUsage` 提供 `BuildTypeShape()` 或 `TryGetComponent(Role)`，首阶段内部仍从 legacy `SubTypes` 映射而来，不改变脚本语法与现有 binder 注册接口。 3. 先迁移 `Bind_TArray.cpp`、`Bind_TMap.cpp`、`Bind_TSet.cpp`、`Bind_TOptional.cpp` 和 `Bind_BlueprintType.cpp` 里的 wrapper/container helper，统一改查 role，而不是直接写 `SubTypes[0/1]`。 4. 为 P10 的 `TScriptInterface<>` 预留显式 `InterfaceClass` role；如果后续需要表达 `FScriptInterface` 的 object/address 双槽，也把它建成 shape 上的明确节点，而不是继续约定新的下标。 5. 在测试层增加 `TypeShape` snapshot，覆盖 `TArray<TObjectPtr<UObject>>`、`TMap<FName, int32>`、`TOptional<TWeakObjectPtr<UObject>>` 和 `TScriptInterface<>` 原型，确保新增 family 只需补 role 映射。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSet.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TOptional.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 过渡期 `SubTypes` 与 `TypeShape` 会并存；如果某些 family 只迁移了 property 创建，但没迁移 debugger/JIT/codegen 侧读取，就会出现“结构树对内正确、某个旧读取点仍按下标解释”的双轨问题。 |
| 兼容性 | 对现有模板语法、现有 `Bind_*` 注册点和已编译脚本可保持向后兼容；第一阶段只是增加结构化读取视图，不要求用户改写任何声明。 |
| 验证方式 | 1. 增加 `TypeUsage -> TypeShape -> CreateProperty` roundtrip 测试，覆盖 array/map/set/optional/object-wrapper。 2. 回归现有 `TSubclassOf<T>`、`TObjectPtr<T>`、`TWeakObjectPtr<T>` 的 property 生成与匹配，确认迁移前后结果一致。 3. 增加 `TScriptInterface<>` 原型 shape 测试，验证 interface family 不需要再发明新的 `SubTypes[N]` 约定。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P0 | Arch-TS-53 | script-defined 引用类型在入口被压成单一 `ScriptObjectType`，`UInterface` 只能后置识别 | family 显式化 | 高 |
| P1 | Arch-TS-54 | `SubTypes` 只有匿名位置语义，容器与 wrapper 缺少共享的 role-tagged type shape | 结构化类型树 | 中-高 |

---

## 架构分析 (2026-04-09 01:28)

### Arch-TS-55：`TypeSystem` 只有粗粒度布尔能力位，新增类型 family 无法一次声明“在哪些使用面被支持”

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 类型支持面的 owner |
| 当前设计 | `FAngelscriptType` 只公开 `CanCreateProperty`、`CanBeTemplateSubType`、`CanBeArgument`、`CanBeReturned` 这类粗粒度布尔能力；真正到了脚本类生成、native `BlueprintCallable` 暴露、reflective fallback、UHT 函数表几个使用面时，又各自叠加一层独立过滤。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:151`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:194`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:239`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:250` — 类型核心只暴露 property/template/argument/return 四类布尔能力。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:576`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:603` — script-declared `UFUNCTION` 分析阶段只检查 `CanCreateProperty`、`CanBeReturned`、`CanBeArgument`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp:30`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:83` — native `BlueprintCallable` 暴露又改查 `ShouldSkipBlueprintCallableFunction()` 这条独立策略。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp:254`-`282` — reflective fallback 另有 `RejectedInterfaceClass` / `RejectedCustomThunk` / `RejectedTooManyArguments` 规则，不来自 `FAngelscriptType`。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:56`-`63`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:466`-`476`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:492`-`514` — UHT sidecar 又单独决定哪些 `BlueprintCallable` 生成 direct entry、哪些 interface/native interface 一律 stub。 |
| 优点 | 早期实现成本低，某个使用面需要临时开洞时，可以直接在本地加判断，不必先重构整个 `TypeSystem` 合同。 |
| 不足 | 新增一个类型 family 时，没有地方可以一次性表达“成员属性支持、脚本声明函数支持、native 直绑支持、reflective fallback 支持、UHT 直出支持”的完整状态。结果是 rollout 只能靠多处 site-specific 条件同步推进，P10 的 `FInterfaceProperty`/`TScriptInterface<>` 也会继续沿着几条使用面各自打补丁。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FFunctionDesc` 直接遍历真实 `UFunction` 参数并为每个参数创建 `FPropertyDesc`；是否可桥接首先停在 `FPropertyDesc::Create()` 这条 factory 轴上。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:55`-`60`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1537`-`1595` | function/property 使用面先共享同一条 property-family 事实来源，再谈上层脚本暴露策略。 |
| puerts | `FPropertyTranslator::Create()` 统一按 `FProperty` family 分派 `Object/Interface/Array/Map/Set` translator，运行时 wrapper 不再额外维护一套 type-family 过滤。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1225`-`1322`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:598`-`630` | family 支持首先被表达成 translator factory 的能力，而不是 scattered bool + scattered function filter。 |
| UnrealCSharp | codegen 阶段 `FGeneratorCore::IsSupported(UFunction*)` 逐参数递归回到 `IsSupported(FProperty*)`；runtime 阶段 `FFunctionDescriptor::Initialize()` 直接复用 `FPropertyDescriptor::Factory()`。 | `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:674`-`786`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:853`-`875`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FFunctionDescriptor.cpp:34`-`38` | “能不能支持”与“运行时怎么桥接”围绕同一条 `FProperty` family 轴展开，新增 family 时不必先在多个使用面重复声明能力。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 给 `TypeSystem` 增加显式 `SurfaceSupportProfile`，让每个 type family 能一次声明自己在不同使用面上的支持状态与拒绝原因。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/` 新增 `FAngelscriptSurfaceSupportProfile`，至少覆盖 `PropertyMaterialization`、`ScriptFunctionArgument`、`ScriptFunctionReturn`、`NativeBlueprintCallableBind`、`ReflectiveFallback`、`UHTDirectBind`、`TemplateEmbedding` 七个 lane，并附带 `ReasonCode`。 2. 让 `FAngelscriptTypeUsage` 基于现有 `CanCreateProperty/CanBeArgument/CanBeReturned/CanBeTemplateSubType` 先自动生成 profile，第一阶段不改变任何现有行为；`ClassGenerator`、`Bind_BlueprintCallable`、`EvaluateReflectiveFallbackEligibility`、UHT sidecar 只改为查询 profile。 3. 对 `FInterfaceProperty`/`TScriptInterface<>` 这类 P10 family，首阶段只把 `PropertyMaterialization`、`ScriptFunctionArgument/Return` 打开，`NativeBlueprintCallableBind` 与 `UHTDirectBind` 保持 `Unsupported`，形成可渐进 rollout 的显式状态，而不是继续散落特判。 4. 在 `AngelscriptTest` 增加 `SurfaceSupportProfile` snapshot，覆盖 `UObject*`、`TOptional<T>`、delegate、future interface family，确保“某类型在哪些 lane 支持”能被自动回归。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 过渡期 `profile` 与 legacy bool API 会并存；如果某些调用点仍偷读旧布尔位，最容易出现 profile 声称“不支持”但旧路径仍成功进入的双轨问题。 |
| 兼容性 | 对现有脚本语法、现有 bind 结果可保持向后兼容；首阶段只是把现有隐式规则显式化，不要求用户改脚本。 |
| 验证方式 | 1. 生成一份 `SurfaceSupportProfile` manifest，核对当前 `UObject*`、`TArray<T>`、`TOptional<T>`、delegate 行为与现状一致。 2. 回归 script-declared `UFUNCTION`、native `BlueprintCallable`、reflective fallback、UHT `AS_FunctionTable_*` 四条路径，确认 profile 接入前后输出不变。 3. 为 P10 增加 `FInterfaceProperty` 原型 lane 测试，验证可以先只开放 property/脚本 callable，不必同步打开全部使用面。 |

### Arch-TS-56：runtime 与 `AngelscriptUHTTool` 的 callable 曝光规则已经分叉，函数表不能作为真实类型支持面的权威清单

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | callable 曝光策略的一致性 |
| 当前设计 | runtime direct-bind、reflective fallback、UHT function-table 目前各自维护一套 callable 曝光规则，而且这些规则已经出现实际漂移。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp:27`-`30` — runtime direct-bind 入口先要求 `FUNC_Native`，再调用 `ShouldSkipBlueprintCallableFunction()`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:94`-`117` — `ShouldSkipBlueprintCallableFunction()` 会拦 `NotInAngelscript`、`BlueprintInternalUseOnly`，还额外硬编码跳过 `UActorComponent::GetOwner`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp:267`-`279` — reflective fallback 又单独拒绝 interface owner、custom thunk、参数过多函数。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:56`-`63` — UHT exporter 只按 `BlueprintCallable/BlueprintPure` 标志认函数，并不知道 runtime 的 `FUNC_Native` 和 `GetOwner` 特判。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:466`-`476` — UHT 对 `Interface/NativeInterface` 一律落 `ERASE_NO_FUNCTION()`。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:492`-`514` — `ShouldGenerate()` 复制了部分 metadata/custom-thunk 规则，但仍没有 runtime 的 native-only 与 `GetOwner` 特判。 |
| 优点 | runtime 与 UHT tool 可以独立演进；sidecar 不必链接 runtime C++ 代码，也能在 UHT 阶段先产出函数表。 |
| 不足 | 当前 `AS_FunctionTable_*` 更像“sidecar 视角下可导出的函数清单”，而不是“runtime 真正会暴露的 callable 真相”。一旦要渐进打开 interface、新 wrapper 或新的 direct-bind family，开发者没法只看一份资产判断 rollout 状态，必须同时读 runtime 和 UHT 两套规则。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FFunctionDesc` 直接围绕 live `UFunction` 与 `FPropertyDesc` 工作，调用时也只消费同一组参数 descriptor，没有第二套 sidecar 过滤清单。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:48`-`59`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:444`-`449` | callable 暴露面直接建立在 live reflection + property descriptor 上，减少了“生成清单”和“运行时清单”漂移的空间。 |
| puerts | function/struct wrapper 一旦进入运行时，参数/返回值都统一走 `FPropertyTranslator::Create()` 工厂；没有再额外维护一份按函数名/按工具链分叉的 type-family 过滤表。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1225`-`1322` | runtime 只认一条 property-family factory 轴，曝光规则不再分裂成 sidecar policy 与 runtime policy 两套。 |
| UnrealCSharp | `FGeneratorCore::IsSupported(UFunction*)` 在生成期就按参数 property 递归判支持；runtime `FFunctionDescriptor::Initialize()` 只负责消费 `FPropertyDescriptor::Factory()` 结果，不再追加一套独立 callable 过滤清单。 | `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:853`-`875`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:674`-`786`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FFunctionDescriptor.cpp:18`-`38` | 生成期与运行期共享同一条“支持性”事实轴，函数清单可以更接近真实可调用面。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 runtime 与 UHT 的 callable 曝光规则收敛成共享 `CallableExposurePolicy`，让函数表重新变成可追踪的 rollout 资产。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/` 与 `Plugins/Angelscript/Source/AngelscriptUHTTool/` 之间定义共享 `AngelscriptCallableExposurePolicy` 数据模型，至少包含 `ExposureMode(Direct/Reflective/Stub/Skipped)`、`ReasonCode`、`RequiresNative`、`RejectsInterfaceOwner`、`RejectsCustomThunk`、`SpecialCaseName`。 2. 让 UHT sidecar 生成 `AS_FunctionTable_ExposureManifest.json` 或等价 CSV，`ShouldGenerate()` 与 `CollectEntries()` 先产出 policy，再决定 erase macro；即使保持 `ERASE_NO_FUNCTION()`，也要把具体原因记录到 entry 本身，而不是只散落在另一份 skipped CSV。 3. runtime `ShouldSkipBlueprintCallableFunction()` 与 `EvaluateReflectiveFallbackEligibility()` 改为优先查询同一 policy；首阶段仍保留旧代码路径，但只作为 fallback。 4. 第二阶段把 interface/P10 rollout 也迁到 policy 开关上，例如先允许 `ReflectiveOnly`、后允许 `Direct+Reflective`，避免 runtime 与 UHT 各开一半。 5. 在测试层新增 manifest diff：同一 `UFunction` 在 runtime 与 UHT 两侧的 `ExposureMode/ReasonCode` 必须一致，否则直接报回归。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 如果 policy 只是“再包一层”但 runtime/UHT 仍各自偷偷保留旧特判，最终会得到三套规则而不是一套；首阶段必须把 manifest diff 做成硬校验。 |
| 兼容性 | 对现有 `AS_FunctionTable_*` 调用方和现有脚本行为可保持向后兼容；第一阶段新增的是 policy/manifest 与一致性校验，不要求立刻改 direct-bind 结果。 |
| 验证方式 | 1. 选取 `BlueprintCallable`、`BlueprintPure`、`BlueprintImplementableEvent`、interface owner、custom thunk、`UActorComponent::GetOwner` 六类函数生成 manifest，对比 runtime 与 UHT 输出。 2. 回归现有 `AS_FunctionTable_*` 结果，确认未开启新 policy 时 erase macro 不变。 3. 为 P10 增加 interface family rollout 试点，验证同一开关能同时驱动 runtime 绑定面与 UHT 函数表输出。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P0 | Arch-TS-55 | 缺少按使用面收敛的类型 capability profile，新增 family 无法渐进 rollout | 能力模型收敛 | 高 |
| P1 | Arch-TS-56 | runtime 与 UHT 的 callable 曝光规则已经分叉，函数表不能作为真实支持面清单 | 策略/诊断收敛 | 中-高 |

---

## 架构分析 (2026-04-10 00:01)

### Arch-TS-57：`TypesByData` 把 `UStruct/UEnum/UDelegateFunction` 混在同一 `void*` 域，native type bridge 缺少按领域分区的 owner

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | native 类型桥接注册表的 key 形状与领域分区 |
| 当前设计 | 当前 `FAngelscriptTypeDatabase` 只有一张 `TMap<void*, TSharedRef<FAngelscriptType>> TypesByData`。`UStruct`、`UEnum`、delegate signature 都通过覆写 `GetData()` 把各自的 UE 指针塞进这同一张擦除表，再由 `Register()` 用“同一 raw pointer 只能有一条绑定”做全局去重。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:590`-`608` — `FAngelscriptTypeDatabase` 只定义一张 `TypesByData`，没有按 `Struct/Enum/DelegateSignature` 分域的 registry。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:54`-`94` — `Register()` 取 `Type->GetData()` 后直接写入 `TypesByData`，若同一 `Data` 已存在就报错并拒绝注册。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:138`-`145` — `GetByData()` 也是单一 `void* -> FAngelscriptType` 查找，没有领域 tag。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UStruct.cpp:119`-`122`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UStruct.cpp:843`-`847` — `UStruct` binder 直接把 `Struct` 作为 `GetData()`，property/type 解析再回查同一 map。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UEnum.cpp:56`-`59`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UEnum.cpp:407`-`445` — `UEnum` 也走同一 `GetData(Enum)` 路径。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.cpp:118`-`121`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.cpp:694`-`697`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.cpp:1151`-`1154`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.cpp:1102`-`1108`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.cpp:1361`-`1379` — single-cast / multicast / sparse delegate 也都把 `UDelegateFunction*` 当成同一类 `Data` 处理。 |
| 优点 | 当前实现非常直接，native `UStruct/UEnum/UFunction` 反查 base type 的成本低，早期 binder 编写简单。 |
| 不足 | 这张表没有表达“这个 raw pointer 属于哪个领域、承担哪种桥接角色”。结果是 native 类型桥接只能共享一个全局 collision domain，后续若要给同一个 UE 反射对象增加第二种视图或辅助桥接，只能继续开旁路表或扩张 special-case。对 P10 而言，这会让 interface/value/property 三种桥接 owner 很难围绕同一个结构化 registry 演进。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | environment 初始化时分别创建 `ClassRegistry`、`FunctionRegistry`、`DelegateRegistry`、`ContainerRegistry`、`PropertyRegistry`、`EnumRegistry`；class/property/container 是分域 owner，不共享一张擦除 key 表。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:104`-`113`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:46`-`56`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:302`-`309`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/PropertyRegistry.cpp:20`-`25`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ContainerRegistry.cpp:22`-`31`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ContainerRegistry.cpp:58`-`91` | registry 按领域拆分后，同一 UE 身份的 class/property/container 语义可以并存，不必挤在单一 `void*` key 里互相竞争 owner。 |
| puerts | `StructWrapper` 把 property translator 缓存在 wrapper 自己的 `PropertiesMap`；translator 的 family 分派集中在 `PropertyTranslatorCreator::Do()`，不会把 class/property/container metadata 混进同一张全局 `void*` 表。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:21`-`35`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1225`-`1318` | 把“class wrapper”“property translator”“container translator”拆成不同 owner，可避免 native 身份桥接与 value bridge 争抢同一 registry 入口。 |
| UnrealCSharp | environment 初始化时分别创建 `ClassRegistry`、`StructRegistry`、`ContainerRegistry`、`DelegateRegistry` 等 registry；类型描述与 property/容器桥接再由 `FPropertyDescriptor::Factory()`、`FTypeBridge::GetClass()` 分层处理。 | `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp:54`-`85`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp:182`-`214`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/FPropertyDescriptor.cpp:47`-`133`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:197`-`266` | 先把 registry owner 分域，再把 property family 与 reflection type 递归求值，新增 `Interface/Array/Map/Optional` family 时不需要把更多语义塞回单一 data map。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把当前单一 `TypesByData` 升级为带领域 tag 的 `NativeFieldRegistry`，先做内部 owner 收敛，再逐步把 `UStruct/UEnum/UDelegateFunction` 查找迁出擦除表。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/` 新增 `FAngelscriptNativeFieldKey`，至少包含 `Domain(Struct/Enum/DelegateSignature/Function/TemplateBase)` 与 `Ptr`；同时新增 `NativeTypesByFieldKey`，首阶段只服务现有 native family。 2. 保留 `TypesByData` 与 `GetByData(void*)` 作为兼容壳，但 `Register()` 内部先双写新 registry；对 `UStruct/UEnum/UDelegateFunction` 的 duplicate check 改为先按 `Domain+Ptr` 判断，再决定是否允许注册。 3. 第一批迁移 `Bind_UStruct.cpp`、`Bind_UEnum.cpp`、`Bind_Delegates.cpp`，把当前 `GetByData(Struct/Enum/SignatureFunction)` 改成 `GetByNativeField(FAngelscriptNativeFieldKey{...})`；确认行为一致后，再把 `GetByData()` 限制为 legacy fallback。 4. 第二阶段为 P10 预留 `PropertyFamily`/`InterfaceSchema` 等新 domain，让 future `FInterfaceProperty` 原型不必复用 `Struct/Delegate` 的旧 collision domain。 5. 在 `AngelscriptTest` 增加 registry manifest，明确列出 `Struct/Enum/DelegateSignature` 三个域的 key 与 owner，防止后续新 family 又退回擦除 key。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UStruct.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UEnum.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 迁移期 `TypesByData` 与 `NativeTypesByFieldKey` 会并存；如果部分 binder 已切 typed key，另一部分仍直接写 `GetData()/GetByData()`，很容易出现“新 registry 有记录、旧表无记录”的双轨状态。 |
| 兼容性 | 对现有脚本声明、已有 native binding 名称与 property 行为可保持向后兼容；首阶段只改变 C++ 内部 owner，不要求用户改脚本。 |
| 验证方式 | 1. 为 `UScriptStruct`、`UEnum`、single-cast delegate、multicast delegate 各加一组 registry roundtrip 测试，确认迁移前后 lookup 结果一致。 2. 生成一份 `NativeFieldRegistry` manifest，对比当前注册规模与旧 `TypesByData` 命中规模一致。 3. 增加一个 P10 原型测试，验证新增 interface 相关 domain 时不需要再改动 `UStruct/UEnum/UDelegateFunction` 的既有查找路径。 |

### Arch-TS-58：缺少统一 `FromField` 轴，`UEnum/UDelegateFunction/FInterfaceProperty` 只能在各 binder 里私接 `GetByData()`

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | TypeSystem 对 UE 反射字段的统一重建入口 |
| 当前设计 | `FAngelscriptTypeUsage` 公开入口只有 `FromProperty(FProperty*)`、`FromTypeId()`、`FromClass()`、`FromStruct()` 等少数几条路径；其中 `UScriptStruct` 被单独提升成 `FromStruct()`，但 `UEnum`、`UDelegateFunction`、delegate property family 没有等价入口，只能在各自 binder 里直接 `GetByData()`。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:396`-`418` — `FAngelscriptTypeUsage` 只声明 `FromProperty`、`FromReturn`、`FromParam`、`FromTypeId`、`FromDataType`、`FromClass`、`FromStruct`，没有 `FromField/FromEnum/FromFunction`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:291`-`323` — `FromClass()`/`FromStruct()` 是 hard-coded 入口；`Struct` 还特地走 `GetByData(Struct)`，说明字段级重建没有统一 owner。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UStruct.cpp:839`-`847` — `FStructProperty` 解析在 binder 内手工 `GetByData(StructProperty->Struct)`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UEnum.cpp:404`-`445` — `FEnumProperty/FByteProperty` 的 enum 解析也在 binder 内直接 `GetByData(Enum)`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.cpp:1357`-`1379` — `FDelegateProperty/FMulticastDelegateProperty` 仍靠 `GetByData(SignatureFunction)` 做 family-specific 还原。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:147`-`171`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:234`-`249` — 即使走 `FromProperty()`，核心也只负责 `TypeFinder + MatchesProperty`，并没有把 `UEnum/UDelegateFunction` 提升成同一条字段桥接轴。 |
| 优点 | 当前 core API 面小，某个 family 需要补洞时，直接在 binder 里加 `GetByData()` 就能先跑通。 |
| 不足 | 这种做法把“字段身份重建”分裂成多条私有路径。新增 `FInterfaceProperty`、新的 delegate/property family 或未来的 `TScriptInterface<>` 支持时，不会有一个明确入口可以接入，而是继续在 `Bind_UEnum`、`Bind_Delegates`、`ClassGenerator`、UHT 等位置各补一层专用分派。P10 的主要障碍之一正是：`interface` 作为 property family 还没进入一条统一 field 轴。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FFunctionDesc` 遍历 `UFunction` 参数时统一调用 `FPropertyDesc::Create(Property)`；`Create()` 再按 `CPT_ObjectReference/CPT_Interface/CPT_Array/CPT_Map/CPT_Set` 集中分派。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:55`-`60`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1537`-`1605` | 运行时 function/property 桥接共用同一条 `FProperty` factory 轴，新增 interface/container family 时不需要再为某个 binder 发明专用 `GetByData()` 入口。 |
| puerts | `StructWrapper::GetPropertyTranslator()` 统一以真实 `FProperty` 为 key 缓存 translator；translator 创建集中在 `PropertyTranslatorCreator::Do()`，其中 `InterfaceProperty`、`ArrayProperty`、`MapProperty` 都是并列 case。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:21`-`35`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1225`-`1318` | “字段是什么”先在 property factory 层完成分类，上层 wrapper/function bridge 只消费 translator，不再自己重建字段身份。 |
| UnrealCSharp | `FTypeBridge::GetClass(FProperty*)` 统一分派 `FDelegateProperty/FInterfaceProperty/FStructProperty/FArrayProperty/FMapProperty/FSetProperty`；`FGeneratorCore::IsSupported(FProperty*)` 也沿同一条 property 轴递归求值。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:197`-`266`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:403`-`464`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:494`-`512`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:720`-`786` | runtime 与 codegen 都围绕同一条 `FProperty` family 入口工作，`interface/delegate/container` 扩展面因此天然集中。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在现有 `FromProperty/FromClass/FromStruct` 之上新增统一 `FromField`/`ResolveFieldBridge` 入口，把 `UEnum/UDelegateFunction/FInterfaceProperty` 纳入同一字段重建轴。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/` 新增 `FAngelscriptFieldRef` 或 `EAngelscriptFieldKind`，第一版至少覆盖 `Property`、`Struct`、`Enum`、`DelegateSignature`、`Class`；同时提供 `ResolveFieldBridge(FFieldVariant)` 或成组 overload。 2. 让现有 `FAngelscriptTypeUsage::FromClass()`、`FromStruct()` 内部先转到这套新入口，保持旧 API 不动；第一阶段 `Bind_UStruct.cpp`、`Bind_UEnum.cpp`、`Bind_Delegates.cpp` 只改为调用统一 resolver，而不是直接 `GetByData()`。 3. 第二阶段把 `FProperty` family 也显式标到 resolver 上，让 `FInterfaceProperty` 能像 `FDelegateProperty/FEnumProperty` 一样通过同一条轴落地，而不是继续靠 `TypeFinder` 和局部 binder 特判。 4. 第三阶段为 UHT sidecar 或 architecture dump 产出 `FieldBridgeManifest`，让 runtime、codegen、测试都能看到“某字段 family 由谁负责桥接”。 5. P10 rollout 时，先只接 `FInterfaceProperty -> InterfaceSchema + ValueCarrier` 原型，不要求同步改动所有 callable/UHT 路径，从而保持增量落地。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UStruct.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UEnum.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 如果新入口只包一层 helper，但 `Bind_*` 仍保留原始 `GetByData()` 私路，最终会得到两套 field reconstruction 规则；首阶段就需要把旧调用点改成统一转发，而不是仅新增 API。 |
| 兼容性 | 对现有脚本语法与已有 `FAngelscriptTypeUsage` 调用点可保持向后兼容；旧 `FromClass/FromStruct/FromProperty` API 可以继续存在，只是内部 owner 变为统一 field resolver。 |
| 验证方式 | 1. 增加 `UScriptStruct/UEnum/FDelegateProperty/FMulticastDelegateProperty` 的 field roundtrip 测试，确认迁移前后得到的 `TypeUsage` 不变。 2. 生成 `FieldBridgeManifest`，核对当前 struct/enum/delegate family 都能通过统一入口命中 owner。 3. 为 P10 增加 `FInterfaceProperty` 原型测试，验证新增 interface family 时只需要补一个 field resolver case，而不是同时修改多处 binder 私路。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P0 | Arch-TS-58 | 缺少统一 `FromField` 轴，`UEnum/UDelegateFunction/FInterfaceProperty` 只能在各 binder 里私接 | 字段桥接入口收敛 | 高 |
| P1 | Arch-TS-57 | `TypesByData` 把多个 native 领域压进同一 `void*` collision domain | registry 分域 | 中-高 |

---

## 架构分析 (2026-04-10 00:10)

### Arch-TS-59：`TypeDatabase` 同时存在 engine-owned 与静态 `LegacyDatabase` 两个 owner，类型桥接边界会随当前上下文漂移

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | TypeSystem registry 的 owner 与生命周期边界 |
| 当前设计 | `FAngelscriptTypeDatabase` 名义上挂在 `FAngelscriptEngine::SharedState`，但 `AngelscriptType.cpp` 的所有入口都先经 `GetTypeDatabase()`；只要当前线程上没有 `CurrentEngine`，就会静默退回进程级 `LegacyDatabase`。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:35`-`45` — `GetTypeDatabase()` 先取 `FAngelscriptEngine::TryGetCurrentEngine()->GetTypeDatabase()`，失败后直接返回静态 `LegacyDatabase`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:54`-`117` — `Register()`、`ResetTypeDatabase()`、`RegisterAlias()`、`RegisterTypeFinder()` 都统一写这条入口，说明不仅 lookup，连 registry 变更也会落到 fallback DB。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:144`-`155`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:969`-`995` — engine 侧确实把 `TypeDatabase` 放在 `FAngelscriptOwnedSharedState` 里，并只在 `EnsureSharedStateCreated()` 时创建。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:371`-`376` — engine release 时会销毁 shared-state 内的 `TypeDatabase`，但静态 `LegacyDatabase` 不在这条回收路径上。 |
| 优点 | 早期 bootstrap 很省事，某些 module init 场景可以在 engine 尚未 fully constructed 时先注册基础类型，不必显式传 registry handle。 |
| 不足 | TypeSystem 的真实 owner 变成“当前是否有 current engine”这个运行时条件，而不是显式上下文。推断上，这会放大两类风险：一是多 engine / cloned engine / commandlet tooling 复用时，类型注册与查询可能落到错误的 DB；二是 shutdown/reinit 期间，同一套 API 会在 engine-owned 与 legacy fallback 之间切换，P10 这类需要渐进 rollout 的新 family 很难准确知道自己到底注册在哪个生命周期域。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 每个 `FLuaEnv` 在初始化时显式创建 `ClassRegistry`、`ContainerRegistry`、`PropertyRegistry`、`EnumRegistry`，析构时逐个 delete；registry owner 绑定到 env，没有“当前 env 不存在就退回静态全局 registry”的旁路。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:104`-`113`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:176`-`186` | registry 生命周期与 VM/env 同步，哪份类型缓存属于哪个运行时实例是显式的。 |
| puerts | `FJsEnv`/`FJsEnvGroup` 每次都显式构造新的 `FJsEnvImpl`；`ObjectMerger` 与 `FPropertyTranslator` 缓存挂在 `FJsEnvImpl::ObjectMergers` 上，由 env 实例持有与复用。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnv.cpp:14`-`24`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvGroup.cpp:96`-`124`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.h:375`-`390`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.h:615`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:986`-`992` | 即便支持多 env 并行，缓存 owner 仍是显式 env object，而不是靠线程上的“当前 env”缺失时回退到进程全局。 |
| UnrealCSharp | `FCSharpEnvironment::Initialize()` 统一创建 `ClassRegistry`、`StructRegistry`、`ContainerRegistry` 等 registry，`GetEnvironment()` 只返回这份环境对象，销毁时再逐一回收。 | `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp:54`-`85`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp:182`-`214`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp:239`-`242` | 即使是单例环境，也把 owner 做成显式环境入口，没有让 registry API 在“有环境/没环境”两种模式间悄悄切换。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把当前隐式的 `LegacyDatabase` fallback 升级成显式 bootstrap registry，并把 runtime 查询默认收敛到 engine-owned DB。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/` 新增 `EAngelscriptTypeDatabaseScope` 或 `FAngelscriptTypeDatabaseRef`，显式区分 `Bootstrap` 与 `EngineOwned` 两类 scope。 2. 保留现有 `FAngelscriptType::Register/RegisterAlias/RegisterTypeFinder` 作为兼容入口，但内部改成先解析 scope：engine 已创建后默认只允许写 `EngineOwned`，如果命中 `Bootstrap` 就记录 warning/telemetry。 3. 把 `GetTypeDatabase()` 拆成 `GetCurrentEngineTypeDatabase()` 与 `GetBootstrapTypeDatabase()` 两条显式 API；`GetByClass/GetByData/GetByProperty/FromTypeId` 第一阶段只改为优先查 engine-owned，再在无 engine 的纯 bootstrap 场景下显式查 bootstrap DB。 4. 为 module startup 增加一个 `FAngelscriptBootstrapRegistrationScope`，只允许基础 binder 在这段窗口期写 bootstrap DB；窗口关闭后若还有注册落到 bootstrap，直接报诊断。 5. 在 `AngelscriptTest` 或 dump 层增加 `TypeDatabaseManifest`，同时输出 bootstrap/engine 两个域的 type 数量与 key，验证 engine 启动后不会再有运行时 family 漏写到 bootstrap DB。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | registry scope 改成显式后，某些依赖“无 engine 也能随时注册/查询”的旧路径会被暴露出来；首阶段必须保留兼容 fallback 并带诊断，不能直接硬切。 |
| 兼容性 | 对现有脚本语法、现有 binder 注册点与 plugin 用户保持向后兼容；首阶段主要增加 scope 语义与诊断，不要求外部脚本改写。 |
| 验证方式 | 1. 启动前后分别导出 `TypeDatabaseManifest`，确认基础类型只在 bootstrap 窗口注册一次，engine 初始化后 lookup 命中 engine-owned DB。 2. 增加多 engine / cloned engine / commandlet 原型测试，验证一个实例销毁不会污染另一个实例的类型表。 3. 为 P10 interface prototype 增加注册域断言，确保新 family 不会在 engine release 后残留在 fallback DB。 |

### Arch-TS-60：当前类型注册是 append-only 模式，缺少按 `UField` / family 的细粒度 invalidation，热重载与增量 rollout 只能依赖整库重置

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | TypeSystem registry 的增量更新与失效策略 |
| 当前设计 | `FAngelscriptType` 公开了 `Register()`、`RegisterAlias()`、`RegisterTypeFinder()` 和 `ResetTypeDatabase()`，但没有对应的 unregister / invalidate API。registry 一旦写入 `RegisteredTypes`、`TypesByClass`、`TypesByData`、`TypesImplementingProperties`、`TypeFinders`，就只能等整份 `TypeDatabase` 被重建。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:69`-`81` — header 只暴露 `Register`、`ResetTypeDatabase`、`RegisterAlias`、`RegisterTypeFinder`，没有 handle 或 unregister 入口。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:54`-`99` — `Register()` 直接把 type append 到 `RegisteredTypes`、`TypesByAngelscriptName`、`TypesByClass`、`TypesByData`、`TypesImplementingProperties`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:102`-`117` — alias 和 type finder 也只有 append 与 whole-database reset，没有单条移除。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:371`-`376` — engine 侧唯一明确的回收动作是 shared-state release 时整体 `TypeDatabase.Reset()`。 |
| 优点 | static binder 的启动成本低，注册逻辑简单，也不需要为每类 type 保存额外 handle。 |
| 不足 | 这种模式对成熟运行时不够细：hot reload、动态 script class/interface/struct 演进、实验性 family rollout、以及参考仓库比对中常见的 field deletion/rebind，都不能按“只淘汰这一类桥接”处理。推断上，P10 若先做 `FInterfaceProperty` 原型，再调整 `TypeFinder` 或 property adapter，很容易因为旧 finder/旧 mapping 还留在表里，只能靠整 engine teardown 才能得到干净状态。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `ClassRegistry` 提供 `Unregister(const UStruct*)`，`PropertyRegistry` 提供 `NotifyUObjectDeleted()` 清理 `FieldProperties`；字段级缓存有明确的回收点。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:265`-`273`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:314`-`320`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/PropertyRegistry.cpp:20`-`25`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/PropertyRegistry.cpp:316`-`320`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/PropertyRegistry.cpp:405`-`406` | registry 可以按 class/field 精确失效，新增或删除一种桥接不会强迫整个 VM 类型表重置。 |
| puerts | env 析构时释放内部缓存；对象删除时 `NotifyUObjectDeleted()` 会调用 `TryReleaseType()`，同时清理 generated class、function map 与 container metadata。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:698`-`740`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:2104`-`2142` | 类型/字段缓存至少有显式的 env teardown 与 object deletion invalidation，而不是无限追加。 |
| UnrealCSharp | 环境对象有统一析构；`NotifyUObjectDeleted()` 遇到 `UStruct` 会直接 `RemoveClassDescriptor()`，普通对象则移除 object reference。 | `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp:182`-`214`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp:270`-`289` | 动态类型与对象缓存都能按 deletion 事件增量回收，便于 generator/runtime 持续演进。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保持现有静态 binder API 可用的前提下，为 type/alias/finder 增加 registration handle 与按 field/family 的 invalidation。 |
| 具体步骤 | 1. 在 `Core/AngelscriptType.h` 新增 `FAngelscriptTypeRegistrationHandle`，记录注册来源、canonical name、`UClass*`/`void*` key、是否参与 `TypesImplementingProperties`、是否为 type finder。 2. 新增 `Unregister(const FAngelscriptTypeRegistrationHandle&)`、`InvalidateByField(UField*)`、`InvalidateByClass(UClass*)`、`RemoveTypeFinder(Handle)`；首阶段只要求动态生成的 script class/struct/interface family 与实验性 P10 adapter 使用 handle，现有静态基础 binder 仍可继续 fire-and-forget。 3. 给 `GetByClass/GetByData/GetByProperty` 增加轻量 epoch 或 generation 计数，避免迁移期出现“map 已删但 `TypesImplementingProperties` 旧条目还在遍历”的双轨状态。 4. 在 `AngelscriptClassGenerator` 与 future interface prototype 路径中，动态注册的 native bridge/type finder 必须在 reload 前先 invalidation，再注册新版本；不再依赖整 engine teardown 清空环境。 5. 在测试层增加 `RegistryInvalidation` 组，覆盖“删除旧 `UStruct`/重新生成新 `UStruct`”“切换 `TypeFinder` 优先级”“P10 interface adapter rollout/revert”三类场景。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 一旦引入 unregister，当前假设“注册后永久有效”的代码路径会被触发；如果 epoch/handle 不完整，最容易出现 dangling `TypeFinder` 或 `TypesImplementingProperties` 遍历旧对象的问题。 |
| 兼容性 | 对现有脚本 API 与现有基础 binder 行为保持向后兼容；首阶段只要求动态 family 与试验性 adapter 使用 handle，不强迫所有旧注册点一次性迁移。 |
| 验证方式 | 1. 增加 registry invalidation 回归：旧 `UStruct/UEnum/UDelegateFunction` 删除后 lookup 立即 miss，重新注册新版本后再命中新条目。 2. 热重载测试中验证 `TypeFinders` 与 `TypesImplementingProperties` 不会在 reload 后保留旧条目。 3. 为 P10 做 interface adapter 开关测试，确认打开/关闭原型支持时不需要整 engine restart 就能得到一致 registry 状态。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-TS-59 | `TypeDatabase` owner 会在 engine-owned 与静态 fallback 间漂移 | 生命周期边界收敛 | 高 |
| P1 | Arch-TS-60 | 类型注册缺少细粒度 invalidation，只能 whole-reset | registry 演进能力补齐 | 高 |

---

## 架构分析 (2026-04-10 00:20)

### Arch-TS-61：`AddClassProperties()` 把 property post-link 语义写死在 `ClassGenerator`，新增 container family 需要继续改 generator 而不只是补 type adapter

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 动态 `UClass/UStruct` 物化后，property owner flag / inner-property flag / metadata 的归属 |
| 当前设计 | `TypeSystem` 负责 `CreateProperty()`，但真正决定 owner 是否带 `CLASS_HasInstancedReference`、container inner property 是否继承 `EditInline`/`CPF_InstancedReference`、哪些 family 需要展开 inner property 的逻辑，仍然硬编码在 `FAngelscriptClassGenerator::AddClassProperties()` 里。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2923`-`2931` — generator 先只调用 `PropertyType.CreateProperty(Params)` 物化 `FProperty`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2868`-`2903` — 后续的 `BubbleUpInstanceReferenceFlags()` / `ApplyInstancedPropertyFlags()` 是 generator 私有 helper，不属于 type adapter。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3041`-`3098` — post-link 只显式分支 `FArrayProperty`、`FMapProperty`、`FSetProperty`、`FStructProperty`，其余 family 一律走 generic `else`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TOptional.cpp:115`-`125` — `TOptional` 已经能创建真实 `FOptionalProperty`，并递归挂上 `ValueProperty`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TOptional.cpp:128`-`137` — `TOptional` 的 property matching 也是递归对 `GetValueProperty()` 工作，说明它在 type adapter 层已经是“带 inner property 的 family”。 |
| 优点 | 早期把 UE 反射 flag/metadata 调整集中放在一个地方，实现路径直接，`CreateProperty()` 不需要携带太多 context。 |
| 不足 | family 的“创建后还要怎样 finalize”不是 type adapter 自己声明，而是 generator 事后猜当前 property 是哪一类。`TOptional` 这类已有 inner property 的 family 现在仍落到 generic 分支，未来若再加 `TScriptInterface<>`、新的容器 wrapper 或 owner flag 传播规则，就只能继续往 `AddClassProperties()` 叠 `else if`，扩展面会集中挤在 class generator。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FPropertyDesc::Create()` 先按真实 `FProperty` family 统一产出 descriptor；`FArrayPropertyDesc` / `FMapPropertyDesc` / `FSetPropertyDesc` 在各自 descriptor 构造时递归创建 inner/key/value interface，`PropertyRegistry` 再按 `UField*` 缓存 descriptor。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:737`-`739`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:889`-`891`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1027`-`1029`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1537`-`1595`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/PropertyRegistry.cpp:317`-`320`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/PropertyRegistry.cpp:405`-`406` | inner-property 递归与 family-specific 行为归 descriptor/registry owner，自顶向下的 generator 不需要再维护一份 container switch。 |
| puerts | `PropertyTranslatorCreator::Do()` 集中按 `FProperty` family 选 translator；container wrapper 再直接基于 `FProperty` 的 `InitializeValue` / `Identical` / `CopySingleValue` / `DestroyValue` 操作元素，不依赖额外的高层 family 后处理。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1225`-`1323`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/ContainerWrapper.cpp:248`-`262`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/ContainerWrapper.cpp:536`-`555` | family-specific 语义在 translator/container layer 解决，上层 wrapper 不需要为每种 container 再写 owner-branch。 |
| UnrealCSharp | runtime `FPropertyDescriptor::Factory()` 直接收录 `FOptionalProperty`；`FOptionalHelper` 通过 `GetValueProperty()` 建立 inner descriptor，codegen `GetPropertyType()` 也递归走 `OptionalProperty->GetValueProperty()`。 | `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/FPropertyDescriptor.cpp:47`-`133`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Optional/FOptionalHelper.cpp:13`-`24`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Optional/FOptionalHelper.cpp:64`-`91`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:266`-`272` | `Optional` 被当成一等 property family 递归建模，新增 wrapper family 时主要补 descriptor/helper，而不是再入侵 class generator。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 给 property adapter 增加显式 post-materialization contract，把 owner flag / inner flag / metadata 传播从 `AddClassProperties()` 下放到 family 自己。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h` 新增轻量 `FAngelscriptPropertyFinalizePlan` 或 `IAngelscriptPropertyFinalizer`，至少允许 family 声明：`OwnerStructFlags/ClassFlags`、是否需要遍历 inner property、哪些 inner field 要附加 `CPF_InstancedReference/CPF_PersistentInstance/EditInline`。 2. 保留 `CreateProperty()` 现状不变，但让 `AddClassProperties()` 在创建完 `NewProperty` 后优先调用 adapter 的 finalize plan；只有 plan 缺失时才回退到当前 `Array/Map/Set/Struct` 的 legacy switch。 3. 第一阶段只迁移 `Bind_TArray.cpp`、`Bind_TMap.cpp`、`Bind_TSet.cpp`、`Bind_TOptional.cpp`，把 `BubbleUpInstanceReferenceFlags()` / `ApplyInstancedPropertyFlags()` 的 family-specific 分支收敛到共享 helper；`TOptional` 作为首个验证对象，避免再继续走 generic `else`。 4. 第二阶段再把 future `FInterfaceProperty` / `TScriptInterface<>` family 接到同一 contract，上层 generator 只做 orchestration，不再认识具体 container 家族。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSet.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TOptional.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | finalize plan 与 legacy branch 并存阶段，最容易出现同一 property 被重复打 flag 或 metadata 顺序不一致；首阶段应让 migrated family 完全绕过旧分支。 |
| 兼容性 | 对现有脚本语法、现有 `UPROPERTY` specifier 书写和已生成类名保持向后兼容；首阶段只是把现有 generator 后处理 owner 显式化。 |
| 验证方式 | 1. 增加 `TArray/TMap/TSet/TOptional` 的 property finalize snapshot，核对 outer/inner `CPF_*`、`EditInline` metadata 与 owner `CLASS_HasInstancedReference/STRUCT_HasInstancedReference`。 2. 补 `bPersistentInstance` 与 `bInstancedReference` 回归，确认 `TOptional` 等 inner-property family 迁移后不再依赖 generator generic 分支。 3. 为 P10 准备 `TOptional<TScriptInterface<...>>` 原型测试，验证新增 family 时不需要再改 `AddClassProperties()`。 |

### Arch-TS-62：soft reload 的状态迁移依赖扁平化字段路径和 `TypeUsage ==`，新增 wrapper family 很难只补 descriptor 就获得稳定迁移

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 动态类 soft reload 时，旧实例脚本字段如何和新版本字段对齐并迁移值 |
| 当前设计 | 当前 reload 路径会先把 script/native 字段展平成 `NamePrefix + Name` 的字符串路径，再用 `FAngelscriptTypeUsage` 相等判断决定哪些字段可以复制；真正的 copy/compare/construct/destruct 语义则通过 `TypeUsage` trait 或 raw `FProperty` fallback 动态拼出来。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4339`-`4369` — 对无法还原成已注册 `TypeUsage` 的 native 字段，reload 会退到 `FRawUnrealPropertyType`，直接包一层 `FProperty::CopyCompleteValue/Identical/InitializeValue/DestroyValue`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4381`-`4417` — `FLocalPropertyContext` 用 `NamePrefix + Name + ";"` 扁平化 struct/script property 树。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4421`-`4453` — resolve 阶段分别从 `ScriptType` 与 `UnrealStruct` 重新枚举字段，未知 native property 再塞回 `FRawUnrealPropertyType`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4495`-`4516` — 旧字段是否进入迁移集，取决于 `CanCopy/CanConstruct/CanDestruct/CanCompare` 这组 trait。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4542`-`4546` — 新旧字段真正匹配时只看 `LocalProp.Name` 和 `Copy->Type == PropertyType`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:440`-`446` — `FAngelscriptTypeUsage::operator==` 只比较 `Type`、`SubTypes`、const/ref 与 `ScriptClass/IsTypeEquivalent()`，不包含 reload-specific field identity。 |
| 优点 | 这条路径对现有 script object / script struct 足够通用，很多 family 只要补齐 lifecycle trait 就能被 reload 机制复用。 |
| 不足 | reload 的 owner 不是“字段 descriptor/field identity”，而是“扁平路径 + 通用 trait 组合”。一旦新增 `TOptional`、future `TScriptInterface<>`、带 wrapper bytes 的新容器或更复杂的 native fallback family，开发者不但要补 property bridge，还得保证它能被 `FLocalPropertyContext` 展平、能通过 `TypeUsage ==` 命中、能正确实现 `CanCopy/Construct/Destruct/Compare`，否则实例状态迁移就会静默退化。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `PropertyRegistry` 以真实 `UField*` 为 key 缓存 `ITypeInterface`，字段删除时直接按 field invalidation；descriptor owner 绑定到实际反射字段，而不是运行时再拼 synthetic path。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/PropertyRegistry.cpp:317`-`320`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/PropertyRegistry.cpp:405`-`406` | 把字段桥接缓存锚定在真实 field owner 上，减少“再建一套 reload 路径语言”的需要。 |
| puerts | `StructWrapper::GetPropertyTranslator()` 直接按 live property 缓存 translator；container wrapper 内部用实际 `FProperty` 的 `Identical`、`InitializeValue`、`CopySingleValue`、`DestroyValue` 完成元素语义。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:21`-`35`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/ContainerWrapper.cpp:248`-`262`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/ContainerWrapper.cpp:536`-`555` | value semantics 绑定在真实 property/translator 上，而不是交给另一套扁平字段匹配规则。 |
| UnrealCSharp | `ClassRegistry` 用 property hash 延迟构造/缓存 `FPropertyDescriptor`，descriptor 比较走 `Property->SameType()`；`FOptionalHelper` 也是先比 inner descriptor `SameType()`，再调用真实 `FOptionalProperty->Identical()`。 | `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FClassRegistry.cpp:135`-`150`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FClassRegistry.cpp:177`-`190`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Class/FClassDescriptor.cpp:58`-`63`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Class/FClassDescriptor.cpp:88`-`96`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Public/Reflection/Property/FPropertyDescriptor.inl:95`-`100`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Optional/FOptionalHelper.cpp:64`-`69` | reload/bridge 语义优先依附真实 property descriptor 与 `SameType()`，新增 family 时不需要再手写一套 path-based transfer rule。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 给 soft reload 引入显式 `ReloadTransferDescriptor`，把字段 identity 与 copy/compare/construct/destruct contract 固化成可缓存对象，而不是每次从 `Name + TypeUsage` 现拼。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/` 新增 `FAngelscriptReloadTransferDesc`，字段至少包含 `StableFieldKey`、`DisplayPath`、`ResolvedTypeUsage`、`NativeProperty`、`CanTransfer`、`Construct/Copy/Compare/Destruct` function pointer 或轻量 ops。 2. 让旧/新类在进入 reinstance 之前各自先构建 transfer desc 列表；第一版 `StableFieldKey` 仍可由当前 `NamePrefix + Name` 生成，但 descriptor 内同时保留真实 `FProperty*` / script property index，为后续 family-specific 迁移留接口。 3. 对 raw native 字段与 `TOptional`/container family，优先走 `FProperty::SameType()`、`Identical()`、`CopyCompleteValue()` 这一层；只有没有真实 property 时才回退现有 `TypeUsage` trait。 4. 保留当前 `Name + TypeUsage ==` 路径一轮作为 fallback，并在命中 fallback 时记录 diagnostics，帮助定位哪些 family 还没有显式 transfer desc。 5. 第二阶段让 future `FInterfaceProperty` / `TScriptInterface<>` 直接提供自己的 transfer desc，这样 P10 rollout 时不必同时改 flattening、trait 和 offset 匹配三套逻辑。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TOptional.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSet.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | reload path 是现网热路径，descriptor 建模如果做成堆分配-heavy 或保留旧/新两套匹配规则过久，容易引入性能回归和双轨判定差异；首阶段应限制在 transfer analysis 阶段构建一次、实例迁移阶段只读。 |
| 兼容性 | 对现有脚本语法、热重载触发时机和外部插件接口保持向后兼容；第一阶段只是把现有 transfer 规则对象化，不改变用户写法。 |
| 验证方式 | 1. 增加 soft reload 回归：`TOptional<int>`、`TOptional<Struct>`、`TArray<Struct>`、native fallback property、future `TScriptInterface<>` 原型成员在字段保留但偏移变化时仍能迁移值。 2. 增加 descriptor/fallback hit-rate dump，确认迁移后大多数字段不再走 legacy `Name + TypeUsage ==`。 3. 对 CDO 与普通实例各做一次 reinstance 回归，核对 `bModifiedByDefaults`、instanced object 先搬后构造的现有语义保持不变。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P0 | Arch-TS-61 | property post-link 语义仍写死在 `ClassGenerator`，新增 container family 需要改 generator | owner/adapter 职责收敛 | 高 |
| P1 | Arch-TS-62 | soft reload 依赖扁平路径和 `TypeUsage ==`，新 family 难以稳定迁移状态 | 状态迁移 contract 显式化 | 高 |

---

## 架构分析 (2026-04-10 00:32)

### Arch-TS-63：`FAngelscriptTypeUsage` 没有“multiplicity”维度，`ArrayDim` 在核心类型模型里被提前抹平

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | fixed-size array / static array 这类“同一 `FProperty` family 下还带 multiplicity”的扩展能力 |
| 当前设计 | `FAngelscriptTypeUsage` 只表达 `Type`、`SubTypes`、`const/ref` 与少量 script/native 回填指针，没有任何 `ArrayDim`/extent/shape 字段；`FromProperty()` 也只把 `CPF_ConstParm`、`CPF_ReferenceParm` 抄进 usage。结果是 fixed-size array 不只是“暂未支持”，而是根本无法进入当前核心 type model。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:349` — `FAngelscriptTypeUsage` 只有 `SubTypes`、`Type`、`bIsReference`、`bIsConst` 和 `ScriptClass/UnrealProperty/TypeIndex` 联合体，没有 `ArrayDim` 或 multiplicity 字段。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:234` — `FromProperty(FProperty*)` 创建 usage 后，除了 finder 结果外只额外处理 `CPF_ConstParm` / `CPF_ReferenceParm`。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs:77` — UHT sidecar 发现 `property.ArrayDimensions != null` 时直接返回 `static-array-parameter`。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs:564` — header resolver 同样在 `ArrayDimensions != null` 时直接返回空结果，不再尝试保留 fixed-array 类型信息。 |
| 优点 | 当前 `TypeUsage` 足够轻，dynamic container 与 wrapper family 主要只需要 subtype 树，不必额外维护 shape/multiplicity。 |
| 不足 | fixed-size array 无法做 `FProperty -> TypeUsage -> declaration/UHT/tooling` roundtrip；未来如果要支持 `float[4]`、native struct 的定长成员数组，或者任何“不是模板容器但带 multiplicity”的 family，必须在 core model 之外另起一套旁路表示。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 显式把 fixed-size array 当作独立 owner：导出属性层有 `TExportedArrayProperty<T>` 保存 `ArrayDim`，runtime push 层有统一 `PushPropertyArray()` 按 `Property->ArrayDim` 逐元素展开。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaEx.inl:701`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaEx.inl:715`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaEx.inl:724`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaEx.inl:732`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaCore.cpp:711`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaCore.cpp:738` | multiplicity 不是丢给 callsite 临时判断，而是进入 property/export owner，本质上属于类型/字段 shape 的一部分。 |
| puerts | 声明生成阶段把 `ArrayDim > 1` 直接编码成 `FixSizeArray<...>`；运行时又用专门的 `FFixSizeArrayWrapper` 持有 inner translator，并按 `Property->ArrayDim` + `ElementSize` 做索引访问。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:744`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:746`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/ContainerWrapper.cpp:767`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/ContainerWrapper.cpp:792`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/ContainerWrapper.cpp:798`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/ContainerWrapper.cpp:840` | “fixed array”既有 surface syntax，也有 runtime wrapper owner；新增 family 时不用靠 `TypeUsage` 外围的字符串或 guard 去补洞。 |
| UnrealCSharp | 从已检查路径看，property 尺寸与 flags 仍由 descriptor 统一承载，`FPropertyDescriptor` / `FGeneratorCore` 不会先把 live property shape 降成只剩 `Type + const/ref` 的最小模型。 | `Reference/UnrealCSharp/Source/UnrealCSharp/Public/Reflection/Property/FPropertyDescriptor.inl:11`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:470` | 即便没有看到单独的 fixed-array wrapper，这种“shape 仍挂在 property/descriptor owner 上”的方向，仍优于把 multiplicity 在 core type model 中完全抹掉。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先给当前 `TypeSystem` 增加最小 multiplicity/shape 表达，再决定 fixed-size array 的脚本表面语法；第一阶段只解决“能表示”，不强行一次性解决“能完整暴露”。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h` 为 `FAngelscriptTypeUsage` 新增最小 shape 信息，建议是 `EAngelscriptMultiplicity { Single, FixedArray, DynamicContainer }` + `int32 FixedArrayDim`，或者并列新增一个轻量 `FAngelscriptPropertyShape` 由 usage 持有。 2. 修改 `FAngelscriptTypeUsage::FromProperty(FProperty*)`：当 `Property->ArrayDim > 1` 时，不再只返回“元素类型的 usage”，而是同步记录 `FixedArrayDim`；现有单值/动态容器 family 默认仍走 `Single`/`DynamicContainer`。 3. 在 `Plugins/Angelscript/Source/AngelscriptUHTTool/` 中新增 typed shape capture，让 `AngelscriptFunctionSignatureBuilder.cs` 与 `AngelscriptHeaderSignatureResolver.cs` 对 fixed-size array 先产出 `FixedArray` 节点，而不是直接失败；旧的 `static-array-parameter` 失败路径保留一轮作为 fallback。 4. 第一阶段不引入新的脚本语法，也不立刻开放 fixed-size array 参数/属性给用户；仅让 manifest/debug dump/test 能保留 multiplicity 信息，为后续 `DebuggerOnly` 或 `ReadOnlyPropertyView` rollout 做基础。 5. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加 `ArrayDim` shape snapshot，覆盖 native `float[4]`、`UObject* Fixed[2]`、`FStruct Fixed[3]`，验证 `FProperty -> TypeUsage/Shape -> UHT manifest` 不再丢失 extent。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 如果第一阶段就把 fixed-size array 强行映射成现有 `TArray`/template family，会污染已有 subtype 语义；应先只引入“shape 被保留”，避免过早承诺 surface syntax。 |
| 兼容性 | 对现有脚本语法和已支持 family 保持向后兼容；第一阶段只是内部类型表示与 manifest 扩展，不改变用户写法。 |
| 验证方式 | 1. 为 `FAngelscriptTypeUsage::FromProperty()` 增加 snapshot，确认 `ArrayDim > 1` 不再被抹平成普通单值 usage。 2. 回归 `AngelscriptFunctionSignatureBuilder` / `AngelscriptHeaderSignatureResolver`，确认 fixed-size array 不再直接落 `static-array-parameter`。 3. 回归现有 `TArray/TMap/TSet/TOptional` 与普通单值 property，确认新增 shape 字段不改变现有 roundtrip 结果。 |

### Arch-TS-64：static-array 的“不可绑定”策略散落在 editor/debugger/UHT，多表面 rollout 没有统一 owner

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | reflection consumer 对 unsupported property family 的一致性与增量 rollout 能力 |
| 当前设计 | 当前仓库并没有一个统一的“这个 property family 在哪些 surface 可用”的 profile。对 static-array 而言，editor property binding、object/struct debugger、函数返回值调试与 UHT tool 都各自写了一条 `ArrayDim != 1` 早退，互相之间没有共享 owner。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_PropertyBind.h:19` — `GetPropertyBindParams()` 遇到 `Property->ArrayDim != 1` 直接把 `bCanRead/bCanWrite` 关掉。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:469`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:599` — object debugger 枚举成员时两处都写了 “Can't bind static arrays. SAD!” 并直接 `continue`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UStruct.cpp:432`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UStruct.cpp:573` — struct debugger 同样各自跳过 `ArrayDim != 1` 的属性。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:804` — `GetDebuggerValueFromFunction()` 搜索 native property 时再次独立跳过 static array。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs:77`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs:564` — UHT sidecar 也单独维护自己的 static-array rejection。 |
| 优点 | 每个 surface 都能本地快速 fail，避免出现“某一层半支持、另一层崩溃”的危险状态。 |
| 不足 | 这会让 fixed-size array 或 future “带 multiplicity 的新 family”无法做分阶段 rollout：只要想打开一个 surface，就要同步改 editor bind、debugger、runtime helper、UHT tool 多个 callsite，而且很难自动确认各表面是否一致。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | static-array 的展开逻辑集中在 `PushPropertyArray()` 这类通用 helper；导出属性还有 `TExportedArrayProperty<T>` 保存 `ArrayDim`。不同 surface 复用同一 owner，而不是每个 surface 自己写一条 `ArrayDim != 1`。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaCore.cpp:711`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaCore.cpp:721`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaCore.cpp:738`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaEx.inl:701`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaEx.inl:724` | 先有统一 property-array owner，再把不同 surface 接到 owner 上，unsupported/partially-supported 也能在同一处表达。 |
| puerts | `GenTypeDecl()` 统一把 fixed-size array 写成 `FixSizeArray<...>`，runtime 再由 `FFixSizeArrayWrapper` 统一处理长度查询、索引边界和元素读写。 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:744`<br>`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:746`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/ContainerWrapper.cpp:767`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/ContainerWrapper.cpp:792`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/ContainerWrapper.cpp:840` | syntax owner 与 runtime owner 都是集中式，便于先开 declaration，再开 runtime，再开更多 surface。 |
| UnrealCSharp | 从已检查路径看，property 基础信息由 `FPropertyDescriptor` / `FGeneratorCore` 统一提供 `ElementSize` 和 flags 查询，而不是把 family 是否可见散落成 editor/debugger/UHT 的多处早退。 | `Reference/UnrealCSharp/Source/UnrealCSharp/Public/Reflection/Property/FPropertyDescriptor.inl:11`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Public/Reflection/Property/FPropertyDescriptor.inl:26`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:470` | 可借鉴的是“先做统一 descriptor/profile owner”，再让各 surface 消费同一 profile，而不是继续复制早退条件。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先把“某 property shape 在哪些 surface 可用”收敛成统一 profile；fixed-size array 作为第一批试点 family，不要求一次性在所有 surface 全开放。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/` 或 `Binds/` 新增 `FAngelscriptPropertySurfaceProfile`，至少包含 `EditorRead`、`EditorWrite`、`DebuggerRead`、`DebuggerIndexedChildren`、`FunctionSignatureUHT`、`ReasonCode`。profile 的输入应来自前一条建议中的 `PropertyShape`/`Multiplicity`，而不是再次直接读 `ArrayDim`。 2. 让 `GetPropertyBindParams()`、`Bind_BlueprintType.cpp`/`Bind_UStruct.cpp` 的 debugger 枚举、`FAngelscriptType::GetDebuggerValueFromFunction()` 和 UHT tool 全部先查询同一 profile；第一阶段 profile 对 fixed-size array 仍返回当前的 `Unsupported` 结果，确保行为不变。 3. 第二阶段只打开 `DebuggerIndexedChildren` 这一条 lane：对 fixed-size array 生成只读索引子项，不急着开放 editor write 或 script 参数，从而验证 unified profile 能支撑“单 surface 先开”的增量 rollout。 4. 第三阶段再按 family 能力逐步开放 `EditorRead/Write` 与 `FunctionSignatureUHT`，并把 `ReasonCode` 输出到 dump/CSV，避免后续再出现“UHT 已支持、debugger 还在静默跳过”的隐性分叉。 5. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加 `PropertySurfaceProfile` snapshot：覆盖普通单值、dynamic container、fixed-size array、future `FInterfaceProperty` 原型，确保各 surface 的 verdict 来自同一 owner。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_PropertyBind.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UStruct.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 如果 profile 只是“又包一层 helper”，但旧 callsite 继续保留各自的 `ArrayDim != 1` 早退，最终会从五套规则变成六套规则；首阶段就要把现有判定改成统一转发，而不是双轨并存。 |
| 兼容性 | 第一阶段完全保持当前行为，只改变 owner；第二阶段即使先开放 debugger 只读索引视图，也不会改变现有脚本语法或已支持函数签名。 |
| 验证方式 | 1. 在不开放任何新 lane 的情况下回归 editor bind、debugger 与 UHT 输出，确认 fixed-size array 的当前 verdict 与迁移前一致。 2. 增加 profile dump，核对同一 property 在多个 surface 的 `ReasonCode` 一致。 3. 若开启第二阶段 debugger 试点，新增 native `float[4]` / `UObject* Fixed[2]` 调试回归，确认只读索引访问可用而 editor/UHT 仍保持关闭。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-TS-63 | `FAngelscriptTypeUsage` 无法表达 fixed-size array 的 multiplicity | 类型模型扩展 | 中-高 |
| P2 | Arch-TS-64 | static-array 支持策略散落在 editor/debugger/UHT，多表面无法渐进 rollout | 表面策略收敛 | 中 |

---

## 架构分析 (2026-04-10 00:51)

### Arch-TS-65：核心 `TypeUsage` 没有显式 `ReferenceCarrierKind`，object handle 语义被迫散落到 `TypeId` bit、accessor cache 与 binder 私路

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | object-like type 的“引用承载方式”是否在 TypeSystem 核心模型中一等建模 |
| 当前设计 | `FAngelscriptTypeUsage` 只保存 `Type`、`SubTypes`、`const/ref` 与少量回填指针，不保存 `asTYPEID_OBJHANDLE` 或更通用的 carrier 信息；因此 “这是 object handle、unresolved object、还是未来的 interface/value carrier” 只能在 `TypeId` 原始 bit、`bGeneratedHandle` 布尔位和各 binder 私有 helper 里分别判断。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:351`-`360`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:423`-`427` — `FAngelscriptTypeUsage` 只有 `SubTypes/Type/bIsReference/bIsConst/ScriptClass` 等字段；`GetClass()` 注释明确说“不是 object handle 就返回 nullptr”，但结构本身没有 handle/carrier 字段。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:340`-`437` — `FromTypeId()` 用 `TypeId` 恢复 primitive、script object、enum 与 template subtype，但整个过程没有把 `TypeId & asTYPEID_OBJHANDLE` 持久化进 `Usage`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:187`-`191` — core virtual interface 只有 `IsObjectPointer()` / `IsUnresolvedObjectPointer()` 这类“family capability”查询，没有“本次 usage 的 reference carrier”维度。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp:168`-`176`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp:185`-`214`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp:264` — `UObject::opCast` 直接检查 `TypeId & asTYPEID_OBJHANDLE` 才允许 cast，随后再从 `ScriptType->GetUserData()` 拿 `UClass*`；同时 bind 注册又额外用 `bIsHandleType = true` 说明这是另一条旁路语义。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1138`-`1147`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1160`-`1168`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1253`-`1258` — generated accessor 先按 `Usage.IsObjectPointer()` / `Usage.IsUnresolvedObjectPointer()` 选择 helper，再把结果降成 `bGeneratedHandle` / `bGeneratedUnresolvedObject`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h:9`-`33` — cooked cache 只序列化 `bGeneratedHandle` / `bGeneratedUnresolvedObject` 两个 wrapper 布尔位，没有结构化 carrier shape。 |
| 优点 | 当前实现对已有 `UObject*` 和 `TObjectPtr<>` 路径足够轻量，很多 callsite 只要依赖现有 `Type` family 和少量布尔位就能工作。 |
| 不足 | “引用如何承载”没有统一 owner，导致同一类扩展必须同时碰 `FromTypeId()`、`opCast`、generated accessor、cache 序列化和 binder helper。对 P10 而言，`UInterface` 真正卡住的不只是新增 `FInterfaceProperty` adapter，而是没有一条核心语义能表达“这次 usage 是 interface carrier，需要 object slot + interface slot / handle 规则 / accessor 策略”的事实。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 先把 property family 显式归类成 `CPT_ObjectReference`、`CPT_WeakObjectReference`、`CPT_LazyObjectReference`、`CPT_SoftObjectReference`、`CPT_Interface`，再为 `FInterfacePropertyDesc`、`FSoftObjectPropertyDesc` 等 descriptor 分别实现读写语义；`PropertyRegistry` 再按 `UField*` 缓存 descriptor。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:533`-`560`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1579`-`1595`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1721`-`1737`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/PropertyRegistry.cpp:316`-`321` | carrier/family 是 registry+descriptor 的一等分类；新增 `Interface/Soft/Weak` 不是去搜全仓哪些地方在看 raw bit，而是在集中 factory 上加新分支。 |
| puerts | `PropertyTranslatorCreator` 统一按 `FProperty` family 分派 `FObjectPropertyTranslator`、`FSoftObjectPropertyTranslator`、`FInterfacePropertyTranslator` 等 translator；translator 自己负责 `UEToJs/JsToUE` 语义，外围只组合而不重新猜 carrier。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:598`-`630`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1275`-`1310`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1398`-`1407` | object/interface/soft 语义都落在统一 translator factory，而不是拆成 `TypeId bit + cache bool + cast helper` 三套旁路。 |
| UnrealCSharp | `FTypeBridge` 先用 `EPropertyTypeExtent` 区分 `ObjectReference`、`WeakObjectReference`、`LazyObjectReference`、`SoftObjectReference`、`Interface` 等 extent，再为 `FInterfaceProperty`、`FWeakObjectProperty`、`FLazyObjectProperty`、`FSoftObjectProperty`、`FOptionalProperty` 等提供专门 `GetClass(...)` overload。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:76`-`88`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:125`-`142`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:197`-`266`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:415`-`430`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:506`-`556` | reference carrier 被显式建模成 reflection/type bridge 的核心枚举，后续容器、interface、optional 都能在同一桥接轴上增量扩展。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在不改现有脚本语法的前提下，先给 `TypeSystem` 增加显式 `ReferenceCarrierKind` / `ReferenceCarrierShape`，让 `TypeUsage` 能回答“这个 object-like type 是怎样被承载的”，再逐步让 cast、accessor、cache 与 P10 prototype 都消费同一语义。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h` 为 `FAngelscriptTypeUsage` 新增 `EAngelscriptReferenceCarrierKind`，第一版至少覆盖 `None`、`ObjectHandle`、`UnresolvedObject`、`InterfaceValue`、`WeakObject`、`LazyObject`、`SoftObject`、`SoftClass`、`SubclassOf`；如担心字段膨胀，可并列新增轻量 `FAngelscriptReferenceCarrierShape`。 2. 修改 `FAngelscriptTypeUsage::FromTypeId()`、`FromProperty()`、`FromClass()` 以及 object-wrapper 的 `TypeFinder`/`MatchesProperty` 路径：`FromTypeId()` 显式保留 `asTYPEID_OBJHANDLE`，`FromProperty()` 统一从 `FObjectProperty/FInterfaceProperty/FWeakObjectProperty/FLazyObjectProperty/FSoftObjectProperty/FSoftClassProperty/FClassProperty` 归一化 carrier kind。 3. 保留现有 `IsObjectPointer()`、`IsUnresolvedObjectPointer()`、`bGeneratedHandle`、`bGeneratedUnresolvedObject` 一轮作为兼容壳，但要求 `GetClass()`、`UObject::opCast`、generated accessor、`Binds.Cache` 读写与 future `UInterface` prototype 先查询新 carrier；命中 legacy bool/bit 路径时记录 diagnostics。 4. 第二阶段只把 `UObject*`、`TObjectPtr<>`、`TWeakObjectPtr<>`、`TSoftObjectPtr<>`、`TScriptInterface<>` 原型接入新 carrier，不强迫所有 family 一次性迁移；`Bind_BlueprintType.cpp` 与 `Bind_UStruct.cpp` 先改为从 carrier 推 accessor plan，而不是新增更多布尔位。 5. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加 `TypeId -> TypeUsage -> CarrierKind`、`FProperty -> CarrierKind`、generated accessor cache snapshot 和 P10 interface prototype 回归，确保新增 interface carrier 时不需要再同时修改 raw `TypeId` 分支与 cache schema。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UStruct.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 迁移期最大的风险是 “新 carrier + 旧布尔位/旧 raw `TypeId`” 双轨同时存在并发生漂移；第一阶段必须加 assert 或 diagnostics，明确谁是 source of truth。 |
| 兼容性 | 对现有脚本语法、现有对象类型声明和 cooked content 保持向后兼容；首阶段只是增加内部 shape 和 fallback，旧 cache 可继续读取，必要时在 editor/cook 阶段重建新 schema。 |
| 验证方式 | 1. 增加 snapshot：同一 `TypeId` / `FProperty` 在 runtime、accessor 生成与 cache 写回上必须得到相同 `CarrierKind`。 2. 回归现有 `UObject` cast、`TObjectPtr<>` generated getter/setter 与 cooked rebind，确认行为不变。 3. 为 P10 增加 `FInterfaceProperty` / `TScriptInterface<>` 原型测试，验证新增 interface carrier 只需要补一个 core kind 和对应 adapter，而不是再改 `opCast` raw bit、cache bool 和 accessor helper 三套路径。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P0 | Arch-TS-65 | object handle / interface / object-wrapper 缺少统一 `ReferenceCarrierKind`，P10 会继续被 raw `TypeId` 与 cache bool 分叉卡住 | 核心类型语义补齐 | 高 |

---

## 架构分析 (2026-04-10 01:01)

### Arch-TS-66：`FProperty` 物化 contract 在成员字段、`UFunction` 参数/返回值和 interface stub 三条表面分叉，新增 family 不能只靠一个 adapter 落地

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 同一个 type family 在成员属性、函数签名、interface 方法壳上的 `FProperty/UFunction` 物化 owner |
| 当前设计 | `FAngelscriptType` 对外只提供 `CreateProperty()` 这一条基础工厂；真正的上下文语义由不同 callsite 各自补。类成员走 `AddClassProperties()` 的大段 post-link/finalize 分支，函数参数/返回值走 `AddFunctionArgument()` / `AddFunctionReturnType()` 的轻量 `CPF_Parm` 规则，interface 则直接创建 bare `UFunction` stub，完全绕开 typed property pipeline。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:150`-`161` — core virtual contract 只有 `CanCreateProperty()` / `CreateProperty()`，没有 `Context` 或 finalize hook。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2923`-`3098` — class member 物化后还要补 replication/editor/specifier/instanced-reference 传播，owner 明显在 `AddClassProperties()`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3951`-`4025` — 函数返回值/参数只做 `CreateProperty()`、`CPF_Parm`/`CPF_OutParm`/默认值 metadata 写入，没有走 member-lane 的 post-link/finalize。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2762`-`2825` — interface 壳只创建最小 `UFunction` stub，甚至不进入 `AddFunctionReturnType()` / `AddFunctionArgument()`。 |
| 优点 | 每条 surface 都能针对自己的 UE 反射需求快速写死规则，短期 patch 成本低，普通类方法和类成员的现有行为可直接维持。 |
| 不足 | 新 family 只实现 `CreateProperty()` 还不够，还要判断自己是否需要成员 post-link、函数签名 flag、interface callable 壳。`FOptionalProperty`、future `FInterfaceProperty`、新 container wrapper 或任何带 inner-property 语义的 family，都会继续在这三条 surface 上重复补逻辑。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FFunctionDesc` 遍历 `UFunction` 参数时直接调用 `FPropertyDesc::Create(Property)`；interface 调用只是在 dispatch 时改成 `FindFunctionByName()`，参数/返回值描述仍复用同一套 `FPropertyDesc`。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:48`-`58`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:209`-`211`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:441`-`449`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:533`-`560` | callable surface 和 property family 共用一套 descriptor，interface 只是 dispatch 差异，不再单独发明一条“最小 stub”物化链。 |
| puerts | `FFunctionTranslator::Init()` 对每个参数/返回值统一调用 `FPropertyTranslator::Create(Property)`；工厂 `PropertyTranslatorCreator::Do()` 直接把 `FInterfaceProperty`、`Array/Map/Set` 等 family 纳入同一分派。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp:100`-`136`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp:384`-`390`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1271`-`1314`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:598`-`630` | function 参数 surface 复用 property translator factory，新增 family 主要补工厂和 translator，而不是再找一条独立函数物化链。 |
| UnrealCSharp | `FFunctionDescriptor::Initialize()` 为参数/返回值统一调用 `FPropertyDescriptor::Factory(Property)`；同一工厂覆盖 `FInterfaceProperty`、`FArrayProperty`、`FMapProperty`、`FSetProperty`、`FOptionalProperty`。 | `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FFunctionDescriptor.cpp:18`-`59`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/FPropertyDescriptor.cpp:47`-`133`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/ObjectProperty/FInterfacePropertyDescriptor.cpp:29`-`55` | property family 工厂天然横跨成员和函数 surface，interface/container/optional 的扩展点集中。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在 `CreateProperty()` 之上补一个显式 `PropertyMaterializationContext` + `FinalizePlan`，让成员字段、函数签名、delegate、interface callable 共用同一条 property 物化主干。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h` 新增 `EAngelscriptPropertyMaterializationContext { MemberField, FunctionArgument, FunctionReturn, DelegateSignature, InterfaceFunction }` 与 `FAngelscriptPropertyMaterializationPlan`，至少携带 `PropertyFlags`、`MetadataWrites`、`InnerPropagationPolicy`、`OwnerStructFlags`。 2. 保留 `CreateProperty()` 签名不动，但新增 `FinalizeCreatedProperty(Usage, Context, Plan, FProperty*)` 或等价 helper；现有 `AddClassProperties()` 的 instanced-reference 传播、函数参数的 `CPF_Parm`/默认值 metadata、future interface 完整签名，都转成 plan 的不同 context。 3. 让 `AddClassProperties()`、`AddFunctionReturnType()`、`AddFunctionArgument()`、interface `DoFullReload()` 统一改调 `MaterializePropertyTree(Context, Usage, Desc)`；第一阶段接口壳仍可保留 legacy bare-stub fallback，但优先尝试 typed path。 4. 首批迁移 `TOptional`、`delegate`、`interface prototype` 三类：它们分别覆盖 inner-property finalize、typed callable 参数、interface callable 壳三个目前最分叉的 surface。 5. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 新增 snapshot，分别钉住 member property flags、`UFunction` parm flags、interface method signature 这三条 surface 是否来自同一 materialization plan。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TOptional.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 迁移期最容易出现同一 property 被 legacy branch 和新 plan 重复打 flag；首阶段应让已迁移 context 完全绕开旧分支，并在 debug build 对重复 flag/metadata 写入加诊断。 |
| 兼容性 | 对现有脚本语法、现有 `UProperty/UFunction` 名称和大多数运行时行为保持向后兼容；第一阶段只是把物化 owner 收敛，不要求用户改写脚本声明。 |
| 验证方式 | 1. 为 `TOptional<UObject>`、`TMap<FName, UObject>`、delegate 参数/返回值、interface 原型方法各加一组 `FProperty/UFunction` snapshot。 2. 回归 Blueprint 可见性、默认值 metadata 与 instanced-reference 传播，确认 member/function 两条旧行为不变。 3. 为 P10 增加 `FInterfaceProperty` 原型方法测试，确认接入新 family 时不需要再分别改 member lane、function lane 和 interface stub lane。 |

### Arch-TS-67：新增 type family 的扩展成本没有显式 `family manifest`，当前至少跨 `Core/Binds/ClassGenerator/UHTTool` 多个 owner 才能形成完整支持面

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 添加一个新类型映射时，架构能否明确告诉开发者“需要实现哪些 surface、当前已覆盖哪些 surface” |
| 当前设计 | 当前仓库没有一份 family 级别的覆盖声明。`FAngelscriptType` 虚表本身混合 property、callable、default literal、debugger、JIT、editor bind 等能力；`FromProperty()` / `GetByProperty()` 又走独立的 `TypeFinder + MatchesProperty` 解析链；`ClassGenerator` 和 `AngelscriptUHTTool` 还各自维护自己的 materialization/signature 逻辑。推断上，一个想同时支持 `Property -> TypeUsage -> Property`、函数参数/返回值和 UHT 直绑的 family，至少会跨 `Core`、`Binds`、`ClassGenerator`、`AngelscriptUHTTool` 四个 owner 落地。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:147`-`331` — `FAngelscriptType` 一个虚表同时承载 `MatchesProperty`、`CreateProperty`、`SetArgument`、`GetReturnValue`、默认值转换、`GetCppForm`、debugger、`BindProperty` 等多类 surface。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:147`-`171`、`234`-`259` — `GetByProperty()` / `FromProperty()` 仍通过 `TypeFinders` 与 `TypesImplementingProperties` 双链做 reverse mapping。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2923`-`3098`、`3951`-`4025` — class member 与函数参数/返回值各自维护不同的 property 物化 owner。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs:68`-`118` — UHT 侧完全不消费 runtime binder 注册信息，而是重新按 `UhtProperty` 文本构签名。 |
| 优点 | 早期做实验性 family 时，不必先改中心工厂或注册表 schema，只要在本地 binder/工具里把路径补通即可。 |
| 不足 | 缺少显式 `family manifest` 后，扩展成本只能靠开发者记忆代码路径。一个 family 可能在 runtime property roundtrip 已可用，但 callable、debugger、UHT、StaticJIT 仍然缺洞，而且当前没有统一资产能把这种“半支持”状态列出来。对 P10 和 future container family 而言，这会直接放大漏改风险。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FPropertyDesc::Create(Property)` 是 property family 的中心入口；`FFunctionDesc` 直接复用它构造参数 descriptor，`PropertyRegistry` 再按 `UField*` 缓存。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1537`-`1595`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:54`-`58`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/PropertyRegistry.cpp:316`-`320` | family 的入口、函数参数消费方和 registry owner 是连在一起的，扩展面更容易枚举。 |
| puerts | `PropertyTranslatorCreator::Do()` 是统一 factory，`FunctionTranslator::Init()` 只消费 `FPropertyTranslator::Create(Property)` 的结果；interface/array/map/set 都在同一工厂扩展。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1271`-`1314`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1398`-`1407`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp:120`-`136` | family 支持面由工厂集中声明，函数调用只组合 translator，不再重新猜“这个 family 是否支持某个 surface”。 |
| UnrealCSharp | codegen `FGeneratorCore::GetPropertyType()`、runtime `FPropertyDescriptor::Factory()`、函数层 `FFunctionDescriptor::Initialize()` 都围绕同一条 `FProperty` family 轴工作。 | `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:82`-`112`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:144`-`167`<br>`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:231`-`272`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/FPropertyDescriptor.cpp:47`-`133`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FFunctionDescriptor.cpp:18`-`59` | 即使跨 runtime 与 codegen，也有统一的 family 轴；新增 `Interface/Optional/Container` 时，改动面仍可预测。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 给每个 type family 增加显式 `FamilyManifest/CoverageManifest`，把“支持哪些 surface、缺哪条 lane”从隐式代码知识转成可生成、可测试的资产。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h` 新增 `FAngelscriptTypeFamilyManifest`，至少包含 `FamilyName`、`ResolveProperty`、`CreateProperty`、`MemberFinalize`、`FunctionArgument`、`FunctionReturn`、`Debugger`、`CppForm`、`DefaultLiteral`、`UHTSignature`、`Notes/ReasonCode`。 2. 扩展 `FAngelscriptType::Register()` / `RegisterTypeFinder()`：family 注册时必须同时提供 manifest 或由 core 根据已实现 virtual + resolver 自动生成初版 manifest；首阶段不改变行为，只输出 coverage dump。 3. 在 `ClassGenerator`、`Bind_BlueprintCallable`、`StaticJIT` 和 `AngelscriptUHTTool` 增加对 manifest 的读取：某条 lane 未声明支持时，优先输出结构化 reason，而不是继续默默依赖局部 hard-code。 4. 在 `Documents/AutoPlans/` 或 `Plugins/Angelscript/Source/AngelscriptTest/` 旁生成 `TypeFamilyCoverage.json/csv`，先覆盖 primitives、object-like、`TArray/TMap/TSet/TOptional`、delegate、script object/interface family。 5. 第二阶段把 P10 的 `FInterfaceProperty` / `TScriptInterface<>` 原型和 future container prototype 都先以 manifest 形式接入，这样 rollout 前就能看到它只开了哪些 lane、还缺哪些 lane。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs`, `Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 如果 manifest 只是“旁路说明书”，而 runtime/UHT 继续偷偷保留各自硬编码，最终会多出一份不可信的文档层资产；首阶段必须让测试对 manifest 和实际行为做 diff。 |
| 兼容性 | 对现有脚本语法、现有 binder API 和现有 UHT 输出保持向后兼容；第一阶段只新增 coverage 资产和断言，不要求立刻改变生成结果。 |
| 验证方式 | 1. 生成一版 `TypeFamilyCoverage`，核对现有 `UObject`、`TArray`、`TMap`、`TOptional`、delegate、script interface family 的 lane 是否与现实一致。 2. 为一个现有 family 故意关掉某条 manifest lane，确认 runtime/UHT 测试能立即报 coverage mismatch。 3. 为 P10 原型增加 coverage 回归，验证引入 `FInterfaceProperty` 或新 container family 时，开发者能在改单个 family manifest 后立即看到剩余缺口，而不是靠运行时碰撞发现。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P0 | Arch-TS-66 | `FProperty` 物化 contract 在成员、函数、interface 三条表面分叉 | 物化主干收敛 | 高 |
| P1 | Arch-TS-67 | 新 type family 缺少显式 `family manifest`，扩展成本与支持面不可枚举 | 覆盖清单显式化 | 中-高 |

---

## 架构分析 (2026-04-10 01:10)

### Arch-TS-68：`UASClass` 同时维护 `SuperClass` 与 `CodeSuperClass` 两条父系轴，类继承语义没有统一 owner

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 动态 `UClass` 生成时，“反射继承”“原生构造”“父函数校验”分别依赖哪条父类链 |
| 当前设计 | 当前实现把“脚本类真正挂在哪个 `SuperClass` 下”和“构造/校验要参考哪个 native 父类”拆成两条并行轴：`UClass::SuperStruct` 保存反射继承链，`UASClass::CodeSuperClass` 额外保存 native/code 父类；随后不同 callsite 再各自决定读取哪一条。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h:18`-`20` — `UASClass` 明确同时保存 `CodeSuperClass`、`NewerVersion`、`bHasASClassParent`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3258`-`3297` — full reload 先分别求 `ParentCodeClass` 与 `ParentASClass`，再用 `ParentASClass ? ParentASClass : ParentCodeClass` 作为 `SetSuperStruct()` 的结果。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3326`-`3328`、`3694`-`3696` — `NewClass->PropertyLink` 和 `SetSuperStruct()` 绑定到 `SuperClass`，但 `ScriptPropertyOffset` 又固定取 `ParentCodeClass->GetPropertiesSize()`，并把 `CodeSuperClass` 单独写回 `UASClass`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:905`-`908` — replication list 递归走 `GetSuperStruct()`，说明运行时反射继承使用的是 script/native 混合后的反射链。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:1370`、`1423`、`1475` — actor/component/object 三条构造路径都直接调用 `Class->CodeSuperClass->ClassConstructor(Initializer)`，构造继承又改走第二条轴。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:684`-`689`、`737`、`982` — Blueprint event 校验、父函数签名校验继续读 `CodeSuperClass`，而不是 `GetSuperStruct()`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2768`-`2798`、`5205`-`5214` — interface full reload 又单独重算一轮 `SuperClass` 并在 finalization 里走 object lane，说明这两条父系轴在 interface 路径上也没有统一入口。 |
| 优点 | 这套双轴模型让脚本子类既能挂到脚本父类反射链上，又能继续复用 native `ClassConstructor`、父函数和 subsystem 规则，短期兼容性较强。 |
| 不足 | 父类语义变成“谁在当前 callsite 自己选哪条轴”。新增 `TScriptInterface<>`、新的 object-wrapper、compose/projection 或 interface 继承时，开发者不仅要补 type bridge，还要判断应该沿 `SuperClass`、`CodeSuperClass` 还是两者都走；推断上，这正是 `UInterface` 和 future wrapper family 很容易在“构造正确但校验错父类”或“签名校验正确但 runtime hierarchy 判断走偏”之间分叉的根因之一。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | environment 启动时把 `ClassRegistry` 与 `ObjectRegistry` 拆开；`ClassRegistry` 只维护 `UStruct* / name -> FClassDesc`，对象绑定走独立 `ObjectRegistry`，没有额外的“构造父类”旁路字段。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:104`-`113`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:46`-`56`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:302`-`309`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ObjectRegistry.cpp:47`-`60`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ObjectRegistry.cpp:83`-`113` | “类型继承关系”与“实例绑定/生命周期”分域管理后，父类语义不需要在运行时再靠第二条轴补回来。 |
| puerts | 生成类时只设置单一 `Parent`：`SetSuperStruct(Parent)` 后，构造时统一调用 `Class->GetSuperClass()->ClassConstructor(ObjectInitializer)`；没有独立的 `CodeSuperClass` 存根。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JSGeneratedClass.cpp:55`-`63`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JSGeneratedClass.cpp:68`-`83` | 反射父类和构造父类共享同一入口，wrapper/interface 的附加语义放在别的 owner，而不是再引入第二条父系链。 |
| UnrealCSharp | `BeginGenerator()` 只接收一个 `InParentClass` 并直接 `SetSuperStruct(InParentClass)`；interface 另由 `GeneratorInterface()` 独立追加到 `InClass->Interfaces`，不是借第二条父类轴表达。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicClassGenerator.cpp:279`-`298`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicClassGenerator.cpp:656`-`676`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicClassGenerator.cpp:679`-`729` | “父类链”和“实现的 interface 集合”被显式拆开，后续加 interface family 时不用再问“是不是要再发明一个 code super”。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在 `ClassGenerator` 内新增显式 `ClassLineagePlan`，把 `ReflectionSuper`、`ConstructionSuper`、`ValidationSuper` 与 `ImplementedInterfaces` 一次性算清，再让所有 callsite 消费同一份 lineage。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/` 或 `Core/` 新增 `FAngelscriptClassLineageDesc`，至少包含 `ReflectionSuperClass`、`ConstructionSuperClass`、`ValidationSuperClass`、`ImplementedInterfaceClasses`、`bHasGeneratedSuper`、`bIsInterfaceShell`。 2. 在 `PrepareForCreateClass()` / `DoFullReloadClass()` 的前置分析阶段一次性构建 lineage：当前 `ParentASClass` / `ParentCodeClass` / interface super 解析全部收进这一步；`SetSuperStruct()` 只允许消费 `ReflectionSuperClass`。 3. 修改 `StaticActorConstructor` / `StaticComponentConstructor` / `StaticObjectConstructor`、父函数签名校验、`ResolveCodeSuperForProperty()`、subsystem special-case 全部改读 lineage descriptor；首阶段保留 `CodeSuperClass` 作为兼容镜像，但要求每次写入时与 `ConstructionSuperClass` 一致，否则输出诊断。 4. 第二阶段把 interface 继承和 P10 prototype 统一接到 `ImplementedInterfaceClasses + bIsInterfaceShell` 上，停止在 `DoFullReload()` 内临时重算 interface super。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` |
| 预估工作量 | M |
| 架构风险 | 迁移期最大的风险是 `CodeSuperClass` 与 `ClassLineagePlan::ConstructionSuperClass` 双轨并存；如果部分路径仍偷读旧字段，反而会把 lineage 更隐蔽。第一阶段必须加断言或 dump，明确哪条是 source of truth。 |
| 兼容性 | 对现有脚本类继承语法、Blueprint 继承行为和已生成 class 名称保持向后兼容；首阶段只收敛 C++ 内部 owner，不要求用户改写脚本声明。 |
| 验证方式 | 1. 增加 “native parent -> script parent -> script child” 回归，验证 replication、构造和父函数查找都命中同一 lineage plan。 2. 回归 actor/component 默认组件与 override component 校验，确认父类判断不再因 `CodeSuperClass` / `GetSuperStruct()` 选错轴。 3. 为 P10 增加 interface 继承原型测试，确认 interface shell 的 super/interface 集合不再需要在 full reload 里临时重算。 |

### Arch-TS-69：脚本对象存储同时用 `ContainerSize`、`PropertiesSize` 和固定 `128-byte` slack 表示，live layout 与 reserve layout 没有显式 owner

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 动态脚本类实例的“真实 live bytes”“为热重载预留的 bytes”“反射 `PropertiesSize`”分别由谁拥有 |
| 当前设计 | 当前实现没有一份显式 storage descriptor。`UASClass::ContainerSize` 被当作 live script/container size，`UClass::PropertiesSize` 又被当作实际分配尺寸；在支持 hot reload 的配置下，class generator 还会直接对 `PropertiesSize` 加固定 `128` 字节 slack。不同阶段再分别用这两个数字做 relink、memzero、layout 校验。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h:23`-`24` — `UASClass` 本体只额外保存 `ContainerSize` 和 `ScriptPropertyOffset`，没有独立的 storage plan 描述 live/reserve 区间。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3407`-`3413` — class 物化先算 `PropertiesSize` 与 `MinAlignment`，并用 `SuperClass->GetPropertiesSize()` / `GetContainerSize()` 混合校验父类布局。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3682`-`3689` — 生成完成后先 `NewClass->ContainerSize = PropertiesSize`，随后直接 `PropertiesSize += 128`，再 `SetPropertiesSize(PropertiesSize)`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4188`-`4190`、`4220`-`4222` — relink 阶段会临时把 `Class->PropertiesSize` 改成单个 property offset 来 `Link()`，最后再恢复，并把 `ContainerSize` 重写为 `ScriptType->GetSize()`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4834`-`4838` — 脚本对象析构后清零区间用的是 `GetPropertiesSize() - ScriptPropertyOffset`，即按“分配尺寸”而不是“live script 尺寸”清零。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2768`-`2800` — interface shell full reload 仍直接继承 `SuperClass->GetPropertiesSize()` / `GetMinAlignment()`，说明非实例化壳也共用同一套 storage 字段。 |
| 优点 | 现有方案实现简单，能在不额外引入 descriptor 的前提下，为 hot reload 预留空间，并继续复用 UE `UClass::PropertiesSize` 相关逻辑。 |
| 不足 | live bytes、reserve bytes 和反射布局挤在同两个字段里后，任何新 value family、对齐要求更高的容器、future `TScriptInterface<>` carrier，甚至 non-instantiable interface shell，都必须继续改 `ClassGenerator` 的尺寸语义。推断上，这会把“新增类型映射”的风险从 type adapter 扩散到对象布局和 hot reload 清理区间。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | environment 把 class/object/container/property registry 分开；property bridge 直接围绕 live `FProperty` 构建，`FArrayPropertyDesc` 持有 inner descriptor，`FPropertyDesc::Create()` 统一按 family 创建 descriptor，而不是再维护一套脚本类尾部 storage。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:104`-`113`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:53`-`56`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:302`-`309`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:737`-`739`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1537`-`1595` | 先把值布局归属到真实 `FProperty` / descriptor，类型桥接无需额外管理一份“脚本对象 reserve 区间”。 |
| puerts | 生成类时只设置 `SetSuperStruct(Parent)`、继承 flags，并在静态构造里先调 `GetSuperClass()->ClassConstructor()` 再执行 JS 构造；没有额外的 `ContainerSize + reserve bytes` 模型。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JSGeneratedClass.cpp:55`-`63`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JSGeneratedClass.cpp:68`-`83` | 动态类只承担“挂接父类 + 执行脚本构造”，值存储仍交给 UE 反射对象本身，不额外暴露第二套 live/reserve 尺寸协议。 |
| UnrealCSharp | `FPropertyDescriptor` 直接提供 `InitializeValue_InContainer()`、`GetSize()`、`GetMinAlignment()`、`DestroyValue()`；动态类构造时遍历真实 `FProperty` 初始化值，默认组件信息也按 `FObjectProperty*` 挂到 `DefaultSubObjectInfoMap`，不是按脚本 offset 或 reserve bytes。 | `Reference/UnrealCSharp/Source/UnrealCSharp/Public/Reflection/Property/FPropertyDescriptor.inl:57`-`79`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/FPropertyDescriptor.cpp:185`-`190`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicClassGenerator.cpp:542`-`633`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicClassGenerator.cpp:679`-`723` | layout/size/alignment 统一挂在 property descriptor 上，动态类构造消费 descriptor，不再额外发明一套 class-level slack 协议。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 引入显式 `ObjectStoragePlan`，把 live layout、allocated layout 和 hot-reload reserve 分开建模；第一阶段只收敛 owner，不改现有脚本语法和大部分 property offset 逻辑。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/` 新增 `FAngelscriptObjectStoragePlan`，至少包含 `LiveContainerSize`、`AllocatedPropertiesSize`、`HotReloadReserveBytes`、`ScriptPropertyOffset`、`MinAlignment`、`bInstantiableShell`。 2. 让 `DoFullReloadClass()`、interface full reload 和 relink 流程统一先构建 storage plan；`ContainerSize` 与 `SetPropertiesSize()` 第一阶段只做该 plan 的兼容镜像，原来的 `+128` 迁成 `HotReloadReserveBytes` 默认值。 3. 修改 `DestructScriptObject()`、relink 后恢复尺寸、hot reload 清零区间等路径，统一改读 plan 的 `LiveContainerSize` / `AllocatedPropertiesSize`，不再直接把 `GetPropertiesSize()` 当 live script 区间。 4. 对 interface shell 首先只做“显式标注 `bInstantiableShell = false` + `HotReloadReserveBytes = 0`”，保持当前反射行为不变，但让后续 P10 能在不碰 object storage 的前提下接入 interface value/callable prototype。 5. 首阶段不改现有 `DefaultComponents` / `OverrideComponents` 的 offset 字段，只要求任何继续读取 `GetPropertiesSize()` 作为 live size 的路径先打诊断，等 storage owner 稳定后再处理更细粒度的 offset 迁移。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` |
| 预估工作量 | M |
| 架构风险 | 迁移期如果 `ContainerSize`/`PropertiesSize` 和 `ObjectStoragePlan` 三者并存但没有强约束，很容易出现“析构按 live size，relink 按 allocated size，另一路还在读旧字段”的三轨状态；首阶段应增加 storage dump 和一致性断言。 |
| 兼容性 | 对现有脚本类定义、hot reload 触发时机和外部插件 API 保持向后兼容；首阶段只把内部尺寸语义对象化，不要求用户改写脚本属性声明。 |
| 验证方式 | 1. 增加 hot reload 回归：给脚本类新增/删除字段后，验证 `LiveContainerSize`、`AllocatedPropertiesSize` 与旧 `ContainerSize/PropertiesSize` 镜像一致，且 reserve bytes 只影响分配不影响 live clear range。 2. 回归对齐敏感的 value family 原型，例如 `TOptional<Struct>`、future `TScriptInterface<>` carrier，确认不需要再改 `+128` 或手工清零区间。 3. 增加 interface shell 测试，确认在 `bInstantiableShell = false` 下仍能保留反射壳，但不再被 storage reserve 逻辑当成普通对象类处理。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-TS-68 | `SuperClass` / `CodeSuperClass` 双父系轴缺少统一 lineage owner | 继承语义收敛 | 高 |
| P1 | Arch-TS-69 | `ContainerSize` / `PropertiesSize` / hot-reload slack 混成隐式 storage ABI | 存储计划显式化 | 中-高 |

---

## 架构分析 (2026-04-10 01:20)

### Arch-TS-70：`ClassConstructor` 劫持没有显式 install/uninstall owner，`ScriptTypePtr` 失效后构造钩子仍可能残留

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 动态 `UClass` 构造钩子与类型身份生命周期是否由同一 owner 管理 |
| 当前设计 | 当前 `TypeSystem` 通过 `UASClass::ScriptTypePtr` 判断一个 `UClass` 是否仍代表脚本类型，但 `ClassGenerator` 安装构造钩子时直接覆写 `UClass::ClassConstructor`；热重载清理只会清 `ScriptTypePtr/ConstructFunction/DefaultsFunction`，构造钩子安装与恢复没有对等账本。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:291`-`302` — `FAngelscriptTypeUsage::FromClass()` 只有在 `UASClass::ScriptTypePtr` 还有效时，才把该类重新识别成 `GetScriptObject()`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2406`-`2412` — reload 清理 `ReplacedClass` 时只把 `ScriptTypePtr`、`ConstructFunction`、`DefaultsFunction` 置空。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:5236`、`5468`、`5480` — actor/component/object 三条 finalization 直接写 `ClassDesc->Class->ClassConstructor = &UASClass::Static*Constructor`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:1077`-`1081` — `AllocScriptObject()` 再通过 `(*Class->ClassConstructor)(Initializer)` 回到这条钩子。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:1370`、`1423`、`1475` — 三个静态构造器继续手工转发到 `CodeSuperClass->ClassConstructor(...)`。 |
| 优点 | 安装路径极短，当前 actor/component/object 三类脚本宿主都能直接接入 UE 构造流程。 |
| 不足 | 这里的风险不是“有三套静态构造器”本身，而是 install/uninstall 生命周期没有和 nominal type identity 同步。推断：在本轮已读插件源码里，未定位到与 `ClassConstructor = &UASClass::Static*Constructor` 对等的 registry/restore 路径。这意味着类壳在 `ScriptTypePtr` 已失活后，仍可能保留 Angelscript 构造钩子；未来给 `UInterface` 壳、非实例化 schema 或新宿主 lane 扩展时，也只能继续沿“直接改 `ClassConstructor`”这条隐式接缝扩张。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnrealCSharp | 明确用 `FClassRegistry::ClassConstructorMap` 保存原始 `ClassConstructor`，安装时登记，移除 descriptor 或环境销毁时恢复；运行时构造入口始终先通过 map 调回原始构造器。 | `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FClassRegistry.cpp:21`-`41`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FClassRegistry.cpp:105`-`125`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FClassRegistry.cpp:193`-`227` | constructor hook 有明确 install/uninstall ledger，动态类型失活后不会把拦截器静默留在类对象上。 |
| puerts | generated class 仍会安装 `StaticConstructor`，但模块 reset 时会遍历类对象，把还挂着 `UTypeScriptGeneratedClass::StaticConstructor` 的类恢复到父类构造器或清空。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JSGeneratedClass.cpp:68`-`84`<br>`Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:322`-`345` | 即便没有 registry map，也把“卸载/重置时恢复 constructor”当成正式 lifecycle，而不是只清脚本侧指针。 |
| UnLua | interface/object bridge 主要停在 `FPropertyDesc` 层：`GetValueInternal()` / `SetValueInternal()` 直接收发 `ValuePtr` 与 `UObject*`，不需要先安装新的 `UClass::ClassConstructor`。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:541`-`560` | value bridge 与 class-construction hook 解耦后，新增 interface/value family 不必先解决 constructor 安装与恢复。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 为 Angelscript 动态类增加显式 `ClassConstructor` 安装账本，把“脚本类身份”与“构造钩子是否仍然有效”收敛到同一 registry。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/` 或 `Core/` 新增 `FAngelscriptClassConstructorRegistry`，记录 `{ UClass*, PreviousConstructor, InstalledConstructorKind, GenerationId, bInstantiable }`。 2. 把 `FinalizeActorClass()`、`FinalizeComponentClass()`、`FinalizeObjectClass()` 的直接赋值改成 `InstallConstructorHook(Class, HookKind)`；首阶段 registry 内部仍安装现有 `Static*Constructor`，不改脚本语法。 3. 在 reload teardown 和类替换路径上，当 `ScriptTypePtr` / runtime descriptor 被清空时，同步调用 `RestoreConstructorHook(Class)`；`AngelscriptClassGenerator.cpp:2406`-`2412` 这类清理点不再只清脚本侧指针。 4. 给 future `UInterface` shell 和其他 non-instantiable family 增加 `NoConstruct`/`PassThrough` hook kind，允许它们继续保留反射壳，但不再默认安装 `StaticObjectConstructor`。 5. 增加 hook dump 或 automation snapshot，核对“已安装 constructor hook 的类集合”和“仍有 `ScriptTypePtr`/descriptor 的类集合”完全一致。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` |
| 预估工作量 | M |
| 架构风险 | 迁移期若仍允许零散路径直接写 `Class->ClassConstructor`，registry 很快会失去权威性；首阶段必须加诊断，捕获 bypass 安装。 |
| 兼容性 | 对现有脚本类声明、构造顺序和脚本 API 保持向后兼容；第一阶段只是把 constructor hook 的 owner 显式化，不要求用户改脚本。 |
| 验证方式 | 1. 做一次 full reload / remove-class 回归，验证 `ScriptTypePtr` 清空后原始 `ClassConstructor` 也被恢复。 2. 回归 actor/component/object 三条现有构造路径，确认 `ConstructFunction`/`DefaultsFunction` 顺序不变。 3. 为 interface shell 原型增加测试，验证它可保留反射壳但不再被强制安装 object constructor hook。 |

### Arch-TS-71：`CurrentObjectInitializers` / `OverrideConstructingObject` 把实例化上下文编码成 ambient state，线程安全判定与构造流程被隐式耦合

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 动态脚本对象的实例化上下文是否由显式 descriptor/registry 持有 |
| 当前设计 | 当前实现把“谁正在构造”“默认值 outer 是谁”“这次是不是脚本分配”“当前能否在非 game thread 执行”分散在多个进程级可变状态里：`OverrideConstructingObject`、`CurrentObjectInitializers`、`FUObjectThreadContext::TopInitializer()`、`thread_local GASDefaultConstructorOuter`。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h:74`、`104`、`114` — `UASClass` 头文件直接暴露 `GetConstructingASObject()`、`GetDefaultConstructorOuter()` 和静态 `OverrideConstructingObject`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:36` — `OverrideConstructingObject` 是进程级静态指针。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:53`-`80` — `CheckGameThreadExecution()` 会根据 `GetConstructingASObject()` 的结果改变线程安全判定。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:987`、`1077`-`1081` — `CurrentObjectInitializers` 是进程级静态栈，`AllocScriptObject()` 通过压栈后再调用 `Class->ClassConstructor`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:990`-`1008` — `GetConstructingASObject()` 先看 `OverrideConstructingObject`，再回落到 `FUObjectThreadContext::TopInitializer()`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:1011`-`1026` — 默认构造 outer 由单独的 `thread_local GASDefaultConstructorOuter` 保存。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:1137`-`1169`、`1375`、`1428`、`1483` — `FinishConstructObject()` 和三条 `Static*Constructor()` 都依赖 `CurrentObjectInitializers.Last()` 判断这次是否为脚本分配。 |
| 优点 | 不需要改现有 Angelscript 语法，也能把 AngelScript 分配/UE 构造/defaults 执行串起来，落地成本低。 |
| 不足 | 这套模型的核心问题是：实例化上下文不是显式对象，而是“当前线程/当前栈/当前 override 指针”的组合。结果就是线程安全检查、defaults 执行时机、script allocation 判定都依赖 ambient state 顺序。对未来的 `UInterface` 壳、non-instantiable family、多 runtime 并存、嵌套构造或更细粒度 async load 来说，这条接缝会继续放大，因为这些场景并不天然拥有“栈顶就是我这次构造”的假设。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `StaticConstructor()` 只消费 `ObjectInitializer.GetClass()`、`GetObj()` 和挂在 generated class 上的 `DynamicInvoker`；没有额外的全局构造对象栈。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JSGeneratedClass.cpp:68`-`84` | 实例化上下文收敛在 `ObjectInitializer + per-class invoker`，构造行为不会再受进程级旁路状态影响。 |
| UnrealCSharp | `FClassRegistry::ClassConstructor()` 只从 `InObjectInitializer` 和环境 registry 取 `Class/Object/MonoObject`，hook 生命周期由 `ClassConstructorMap` 管；没有额外的“当前正在构造谁”的全局覆盖变量。 | `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FClassRegistry.cpp:193`-`227` | 构造上下文显式附着在 `ObjectInitializer` 和 registry 上，thread/runtime 边界更清楚。 |
| UnLua | interface property bridge 只在 `GetValueInternal()` / `SetValueInternal()` 上显式传 `ValuePtr` 与 `UObject*`，没有把值桥接建立在 class construction ambient state 之上。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:541`-`560` | 值桥接如果坚持“显式输入 -> 显式输出”，future interface/value family 就不必先接入一个全局构造上下文。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 引入 engine-owned 的显式 `InstantiationContext`，把“当前构造是谁”从全局状态改成 scoped token / registry，并让线程安全判定读取明确 phase。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/` 或 `ClassGenerator/` 新增 `FAngelscriptInstantiationContext` 与 registry，字段至少包含 `{ UObject* Object, asITypeInfo* ScriptType, UObject* DefaultOuter, bool bIsScriptAllocation, bool bApplyDefaults, EAngelscriptExecutionPhase Phase, GenerationId }`。 2. 让 `AllocScriptObject()` 返回或登记 scoped token；`FinishConstructObject()` 和三条 `Static*Constructor()` 改为按 `Initializer.GetObj()` 查询 token，而不是比较 `CurrentObjectInitializers.Last()`。 3. 把 `GetConstructingASObject()` 改成 registry 查询；`OverrideConstructingObject`、`CurrentObjectInitializers`、`GASDefaultConstructorOuter` 第一阶段仅作为 legacy fallback 保留，并在命中时打诊断。 4. 把 `CheckGameThreadExecution()` 从“是否存在 constructing object”改成读取显式 `ExecutionPhase`，把 `default` 语句、async loading、runtime event dispatch 区分开。 5. 对 future `UInterface` shell、non-instantiable family 或仅需 property bridge 的 value carrier，明确不注册 instantiation context，这样它们不会再被 ambient constructor state 误判成普通脚本对象构造。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.*`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` |
| 预估工作量 | M |
| 架构风险 | 迁移期最容易出现 registry token 与 legacy 全局状态并存、但不同路径各读一份的双轨问题；必须先增加 diagnostics，确认所有构造入口都命中同一 token。 |
| 兼容性 | 对脚本构造语法、`default` 语句语义和现有对外 API 保持向后兼容；首阶段只收敛内部 owner，不要求用户改脚本。 |
| 验证方式 | 1. 增加 nested construction / default statement / async loading 回归，确认 `bIsScriptAllocation` 与 defaults 执行不再依赖栈顶顺序。 2. 回归线程安全检查，确认 `CheckGameThreadExecution()` 按显式 phase 报错，而不是按 ambient constructing object 报错。 3. 为 interface shell 和 non-instantiable 原型增加测试，确认它们不注册 instantiation context 也能正常保留反射壳。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-TS-70 | `ClassConstructor` 劫持缺少 install/uninstall ledger，identity 与构造钩子生命周期分叉 | 生命周期 owner 收敛 | 高 |
| P2 | Arch-TS-71 | 实例化上下文依赖 `CurrentObjectInitializers` / `OverrideConstructingObject` 等 ambient state | 上下文显式化 | 中-高 |

---

## 架构分析 (2026-04-10 01:29)

### Arch-TS-72：`AngelscriptUHTTool` 的 overload 解析没有把 method `const` 与参数 qualifier 当成一等语义，直绑覆盖会被合法签名误伤

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | UHT 直绑签名恢复是否与 runtime 的 `const/ref` 语义保持一致 |
| 当前设计 | runtime `TypeSystem` 明确把 `bIsConst`、`bIsReference` 作为 `FAngelscriptTypeUsage` 的核心字段，并在 override 校验与 `UFunction` 物化时逐项核对 `CPF_ConstParm/CPF_ReferenceParm`；但 `AngelscriptUHTTool` 的 overload disambiguation 只比较参数数量、归一化后的参数文本和返回值文本，没有把 `parsedSignature.IsConst` 纳入 exact match。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:270`-`278`、`325`-`337` — `FromReturn()` / `FromParam()` 直接从 AngelScript flags 恢复 `bIsReference`、`bIsConst`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:886`-`887`、`916`-`960` — override 校验把 `CPF_ConstParm`、`CPF_ReferenceParm`、`CPF_OutParm` 与脚本参数语义逐项对齐。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3991`-`4007` — 生成 `UFunction` 参数时也会显式落 `CPF_ReferenceParm` / `CPF_OutParm` / `CPF_ConstParm`。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs:90`-`97` — UHT 侧实际上已经把 `HasFunctionFlag(function, "Const")` 存进 `AngelscriptFunctionSignature.IsConst`。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs:75`-`105` — exact match 只比较参数个数、`AreTypesEquivalent()` 和归一化后的返回值，没有比较 `parsedSignature.IsConst`。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs:153`-`177` — `NormalizeTypeText()` 会移除 `const`、`&` 和空格。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs:57`-`59` — 一旦落入 `overloaded-unresolved` 且不在白名单，函数表就直接放弃精确直绑。 |
| 优点 | UHT sidecar 可以容忍头文件排版差异，不需要链接 runtime 类型对象就能重建多数 native 签名。 |
| 不足 | 这里的问题不是“又有一套字符串签名”这么泛，而是 qualifier 语义在 runtime 和 UHT 两边已经出现了精度差：runtime 把 `const/ref/out` 当成行为合同，UHT exact match 却会把它们抹平。结果是合法的 `const`/非 `const` 成员重载、`const TArray<T>&` 一类 wrapper-heavy 参数、future `const TScriptInterface<...>&` 场景，都可能在 runtime 已可判定的前提下继续被 UHT 降成 `overloaded-unresolved` 或误入 stub。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FFunctionDesc` 构造时直接遍历真实 `UFunction` 参数，并为每个 `FProperty` 调 `FPropertyDesc::Create()`；`OutParm/ReferenceParm/ConstParm` 语义沿着 property descriptor 保留，不依赖 header 文本归一化。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:48`-`75`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1537`-`1595` | qualifier 语义从一开始就绑定在 `FProperty` 上，函数层不需要再做一次“弱 canonicalization”。 |
| puerts | `FFunctionTranslator::Init()` 直接遍历 `UFunction` 的 `CPF_Parm` 字段并构造 `FPropertyTranslator`；translator factory 以 `FProperty` family 为输入，而不是以头文件字符串为输入。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp:113`-`136`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1224`-`1265` | 参数 qualifier、`OutParm` 和 wrapper family 都由 property translator 统一吸收，overload 判定不需要再丢失 `const/ref`。 |
| UnrealCSharp | `FFunctionDescriptor::Initialize()` 遍历真实 `FProperty` 并用 `FPropertyDescriptor::Factory()` 建 descriptor，同时按 `CPF_OutParm/CPF_ConstParm/CPF_ReferenceParm` 建 out/ref 索引。 | `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Function/FFunctionDescriptor.cpp:18`-`59`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/FPropertyDescriptor.cpp:47`-`133` | qualifier 不是 header 解析副产品，而是 function descriptor 的基础输入，因此新 family 扩展不会额外制造 UHT 误判。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 保留现有 header resolver 形态，但把 `method const` 与参数 qualifier 提升为 UHT exact match 的显式维度，不再让 `NormalizeTypeText()` 单独决定 overload 归属。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptUHTTool/` 为 `AngelscriptFunctionSignature` 增加显式 qualifier shape，至少记录 `MethodConst`、每个参数的 `IsConstParm`、`IsReferenceParm`、`IsOutParm`。 2. 修改 `AngelscriptHeaderSignatureResolver.cs`：`exactMatches` 阶段除了现有参数文本/返回值文本外，必须额外比较 `parsedSignature.IsConst` 与 `function.FunctionFlags`，并比较参数 qualifier shape；`NormalizeTypeText()` 只保留为 whitespace/typedef 级 fallback。 3. 给 `failureReason` 增加更细的 `const-mismatch`、`ref-qualifier-mismatch`、`out-qualifier-mismatch`，避免所有合法重载都被折叠成 `overloaded-unresolved`。 4. 在 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` 增加最小回归：`Foo() const` / `Foo()`、`const TArray<int32>&`、`const UObject*&`，以及一组 future `const TScriptInterface<...>&` 占位样例；首阶段允许 interface 仍然 stub，但 qualifier shape 不能丢。 5. 当 qualifier shape 稳定后，再让 `AngelscriptFunctionTableCodeGenerator.cs` 的直绑统计区分“真的不支持 family”和“只是 qualifier 解析不全”，这样 P10 rollout 才不会继续被假阳性 stub 淹没。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | S-M |
| 架构风险 | 如果 qualifier shape 只加在 builder、不加在 resolver 和测试，最后仍会回到“文本匹配说可以，runtime 语义说不可以”的双轨状态；首阶段就要让 reason code 和 snapshot 一起落地。 |
| 兼容性 | 对现有脚本语法、现有 `AS_FunctionTable_*` 调用方和 runtime 行为保持向后兼容；变化主要是 UHT 生成结果更精确、失败原因更可诊断。 |
| 验证方式 | 1. 回归 `AngelscriptGeneratedFunctionTableTests`，确认 `const`/非 `const` 成员重载不再无差别落入 `overloaded-unresolved`。 2. 抽样验证 runtime override 校验与 UHT reason code 一致，避免同一函数在两侧得到不同 qualifier 结论。 3. 对 `const TArray<int32>&`、`TMap<FName, UObject*>&` 这类 wrapper-heavy 参数做生成快照，确认 qualifier shape 被保留。 |

### Arch-TS-73：callable receiver 只有 `Static` / `MemberThis` 两条 lane，`UInterface` 没有独立调用 ABI

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `UInterface` 方法是否拥有与普通类方法并列的调用接收者模型 |
| 当前设计 | 当前 callable 管线默认只有“静态函数”与“普通成员函数”两种 receiver：UHT 生成 `ERASE_FUNCTION_PTR` 或 `ERASE_METHOD_PTR`，runtime direct-bind 绑定成 global / `asCALL_THISCALL` / `asCALL_CDECL_OBJFIRST`。interface 方法没有第三种 receiver kind，只能走 `ClassGenerator` 里单独的 `CallInterfaceMethod()` generic callback 按名字转发。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs:17`-`37` — `BuildEraseMacro()` 只会生成 `ERASE_FUNCTION_PTR` / `ERASE_METHOD_PTR` 或 auto 版本，没有 interface receiver 变体。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp:100`-`139` — runtime direct-bind 只有 `BindGlobalFunction()`、`BindMethodDirect(... asCALL_CDECL_OBJFIRST)` 和 `BindMethodDirect(... asCALL_THISCALL)` 三条 lane。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp:267`-`270` — reflective fallback 对 `CLASS_Interface` 直接给出 `RejectedInterfaceClass`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:53`-`67` — interface method 现在只能走专门的 `CallInterfaceMethod()`，运行时再按 `FindFunction(Sig->FunctionName)` 找真实实现。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:465`-`476` — interface/native interface 在函数表生成时也被统一降成 `ERASE_NO_FUNCTION()`。 |
| 优点 | 普通 `UClass` native 函数的 fast path 非常直接，direct-bind ABI 简单，JIT 和 bind cache 也容易稳定。 |
| 不足 | 这里更深的障碍不是“interface 暂时被 stub”，而是 callable receiver 模型本身没有 interface lane。只要 receiver 仍然只有 `Static` / `MemberThis` 两种，P10 即使补齐 `FInterfaceProperty` 或 `TScriptInterface<>` value bridge，native interface 函数也仍然会被迫绕到 `CallInterfaceMethod()` 旁路，无法渐进复用主 callable pipeline。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FFunctionDesc` 会先标记 `bInterfaceFunc`，调用时如果是 interface 函数，就在具体对象类上 `FindFunctionByName()` 后再 `ProcessEvent()`；interface 只是 receiver 解析不同，不需要退出主函数描述体系。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:48`-`49`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:209`-`211`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:562`-`565` | interface receiver 可以作为 `FunctionDesc` 的一个显式分支，而不是单独的 class-generator 特例。 |
| puerts | `FFunctionTranslator::Init()` 直接记录 `IsInterfaceFunction`；实际调用时如果是 interface，就把 `Function` 替换成 `CallObject->GetClass()->FindFunctionByName(...)`。同时 wrapper 构建阶段会把 `Class->Interfaces` 上的方法补到 JS prototype。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp:113`-`115`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp:250`-`251`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:312`-`327` | receiver lane 先在函数翻译器里显式建模，再在 wrapper 层补接口方法；不需要把 interface 整体踢出 direct/reflective 调用主线。 |
| UnrealCSharp | `FDynamicInterfaceGenerator` 把 interface 作为独立动态类型生成，`FTypeBridge::GetClass(const FInterfaceProperty*)` 也把它显式映射成 `TScriptInterface<>` 泛型实例。 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicInterfaceGenerator.cpp:154`-`171`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicInterfaceGenerator.cpp:231`-`245`<br>`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:415`-`430` | interface 生命周期和 interface 值 carrier 都有独立 owner，后续再接 callable 不必挤进普通 member-pointer ABI。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先为 callable 管线引入显式 `ReceiverKind`，把 interface 从“被拒绝或特判”提升为第三种 receiver lane；第一阶段继续走现有 generic dispatch，不要求立刻实现 interface direct pointer。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/` 与 `Plugins/Angelscript/Source/AngelscriptUHTTool/` 之间新增共享 receiver 描述，至少定义 `EAngelscriptCallableReceiverKind { Static, MemberThis, InterfaceObject }`，并把它加到 `AngelscriptFunctionSignature` / bind DB / generated function table entry。 2. 修改 `Bind_BlueprintCallable.cpp`：保留现有 `Static` / `MemberThis` fast path 不动，但新增 `InterfaceObject` 分支，首阶段统一绑定到现有 `CallInterfaceMethod()` 或等价 reflective invoker，而不是直接拒绝。 3. 修改 `BlueprintCallableReflectiveFallback.cpp`：把 `CLASS_Interface` 从 blanket reject 改成“如果 entry 声明了 `InterfaceObject` receiver 且参数 family 已支持，就允许绑定 reflective path”；旧的 `RejectedInterfaceClass` 保留为未声明 receiver 的 fallback。 4. 修改 `AngelscriptFunctionTableCodeGenerator.cs`：interface/native interface 不再默认等价于 `ERASE_NO_FUNCTION()`，而是先输出 `ReceiverKind=InterfaceObject` 的 entry；即便 erase macro 仍走 generic thunk，也要进入与普通 callable 相同的资产与测试管线。 5. 在 `Plugins/Angelscript/Source/AngelscriptTest/Interface/`、`Bindings/AngelscriptBlueprintCallableReflectiveFallbackTests.cpp`、`Core/AngelscriptGeneratedFunctionTableTests.cpp` 增加三组回归：native interface BlueprintCallable、interface owner + container/ref 参数、generated table receiver snapshot。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptBlueprintCallableReflectiveFallbackTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Interface/` |
| 预估工作量 | M |
| 架构风险 | 如果只在 runtime 加 receiver 分支、不在 UHT/bind DB/测试资产同步建模，interface lane 又会退回旁路；首阶段必须保证 receiver kind 至少能在 generated table 和 runtime bind state 中被同一方式观察到。 |
| 兼容性 | 对现有普通 `UClass` 函数绑定、现有脚本语法和现有 interface 语法保持向后兼容；第一阶段只是把 interface callable 接回主管线，不改变用户脚本写法。 |
| 验证方式 | 1. 回归 native interface BlueprintCallable，确认函数表不再无条件落 `ERASE_NO_FUNCTION()`，runtime 也不再直接命中 `RejectedInterfaceClass`。 2. 覆盖 interface owner 的 `const&`、container 和 future `TScriptInterface<>` 参数样例，确认它们走的是统一 `ReceiverKind=InterfaceObject` 路径。 3. 回归现有普通类 direct-bind/JIT/cache，确认新增 receiver lane 不影响 `Static` / `MemberThis` 的已有性能路径。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P0 | Arch-TS-73 | callable 管线缺少 `InterfaceObject` receiver lane，`UInterface` 仍被迫走特判旁路 | 结构性补口 | 高 |
| P1 | Arch-TS-72 | UHT overload 解析丢失 `const/ref` qualifier 语义，直绑覆盖与 runtime 行为分叉 | 精度修复 | 中-高 |

---

## 架构分析 (2026-04-10 01:41)

### Arch-TS-74：generic family 仍以“半注册 + finder 补全”方式存在，新增 wrapper/interface family 没有 concrete-instantiation owner

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | generic family 的 concrete instantiation 由谁负责 |
| 当前设计 | 当前仓库把不少 generic/wrapper family 注册成“先有 base type、后补 concrete usage”的半成品：family 本身先声明 `CanQueryPropertyType() == false`，再用 `DescribesCompleteType()` 和独立 `RegisterTypeFinder()` 在需要时补齐 subtype。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:96-99` — 只有 `CanQueryPropertyType()` 为真的类型才会进入 `TypesImplementingProperties`，generic family 默认不会进入主 property 查询表。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp:74-79`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp:62-68`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TOptional.cpp:20-27`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSoftObjectPtr.cpp:203-206`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSoftObjectPtr.cpp:259-262` — `TArray/TMap/TOptional/Soft*Ptr` 都先把自己声明成“不参与直接 property 类型查询”。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1630-1643`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1892-1905`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:2194-2206`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSoftObjectPtr.cpp:37-58` — `TSubclassOf/TObjectPtr/TWeakObjectPtr/TSoftObjectPtr` 的“完整类型”判定都退化成 `SubTypes[0]` 是否存在且可解析。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp:1588-1600`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp:1161-1178`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TOptional.cpp:498-512`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSoftObjectPtr.cpp:331-360` — 真正把 `FProperty` 回推成 concrete `FAngelscriptTypeUsage` 的逻辑分散在各 family 自己的 `TypeFinder` lambda。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp:405-421` — Blueprint event argument 的预绑定 specialization 只会为 `DescribesCompleteType(DefaultUsage)` 的 family 生成。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp:430-464` — 其余情况再回退到运行期 `FromTypeId()`，说明 concrete usage 直到调用现场才被重建。 |
| 优点 | 这种做法允许在 analyze 阶段先注册 generic family，即便 subtype 对应的 `UClass`/script class 还没完全落地，也能把主骨架先接进系统。 |
| 不足 | generic family 没有统一的 concrete-instantiation owner。新增一个 wrapper 或 future `TScriptInterface<>` family，不只是补 `CreateProperty()` 或 `MatchesProperty()`，还得同时决定：何时算 complete、如何 reverse-map `FProperty`、哪些场景要做预 specialization、哪些场景只能 runtime fallback。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | concrete descriptor 直接从真实 `FProperty` 派生，容器 descriptor 在构造时递归创建 inner/key/value descriptor，不存在“先注册一个不完整 `TMap` family，之后再靠 finder 补 subtype”的阶段。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:738-739`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:890-891`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1028-1029`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1537-1595` | 把“类型是否完整”前移到 descriptor factory：只有拿到 concrete `FProperty` 才创建 concrete bridge。 |
| puerts | `PropertyTranslatorCreator::Do(PropertyMacro*)` 以 concrete `PropertyMacro` 为唯一输入，在同一工厂里直接分派 `Object/Interface/Array/Map/Set` translator。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1224-1345` | generic family 的 owner 是 property-translator factory，而不是 base family + 分散 finder。 |
| UnrealCSharp | `FPropertyDescriptor::Factory(FProperty*)` 直接返回 `Object/Class/Interface/Array/Map/Set/Optional` 等 concrete descriptor，container helper 再消费 descriptor。 | `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/FPropertyDescriptor.cpp:47-133` | 先有 concrete property descriptor，再谈 container/helper；family 不必经历“半注册”状态。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 增加 family-level concrete instantiation 层，把当前 `CanQueryPropertyType` / `DescribesCompleteType` / `TypeFinder` 三件事收敛成一个显式 resolver。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/` 新增 `FAngelscriptFamilyInstantiationDesc` 或 `IAngelscriptTypeFamilyResolver`，输入统一的 `PropertySignature/TypeShape`，输出 concrete `FAngelscriptTypeUsage`、completeness state 和 failure reason。 2. 保留 `CanQueryPropertyType()` 与 `DescribesCompleteType()` 作为兼容壳，内部转发到 resolver；第一阶段先迁移 `TOptional`、`TSoftObjectPtr`、`TSubclassOf/TObjectPtr/TWeakObjectPtr` 这几类“`SubTypes[0]` 就是全部语义”的 family。 3. 把 `RegisterTypeFinder()` 改为“补充 `PropertySignature` / resolver fragment”，不再允许各 binder 直接手改最终 `Usage`；`FromProperty()` 统一只消费 resolver 产物。 4. 修改 `Bind_BlueprintEvent.cpp`：预绑定 specialization 不再查询 `DescribesCompleteType(DefaultUsage)`，而是查询 family resolver 是否能为某个 concrete shape 产出稳定 usage；运行期 `FromTypeId()` fallback 先保留。 5. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加 concrete instantiation 回归：`TOptional<TSoftObjectPtr<AActor>>`、`TSubclassOf<script class>`、future `TScriptInterface<IMyInterface>` prototype、以及 `FProperty -> Usage -> FProperty` roundtrip。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TOptional.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSoftObjectPtr.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 如果一次性替换全部 `TypeFinder`，最容易把现有 reverse mapping 打断；应先让 resolver 与 legacy finder 并存，并输出 diagnostics 统计仍走旧路径的 family。 |
| 兼容性 | 对现有脚本语法、现有 generic 类型写法和 runtime fallback 保持向后兼容；变化主要是内部 concrete usage 的 owner 更明确。 |
| 验证方式 | 1. 回归 `FProperty -> FAngelscriptTypeUsage -> FProperty`，覆盖 `TArray/TMap/TOptional/TSoftObjectPtr/TSubclassOf`。 2. 验证 Blueprint event 参数 specialization 与 runtime fallback 的结果一致，不再因 family 是否“默认完整”而分叉。 3. 用 `TScriptInterface<>` 原型用例确认 future interface family 可以只新增 resolver，而不是再复制一套 completeness + finder + specialization 逻辑。 |

### Arch-TS-75：容器排序与 map key 选择器把展示/交互语义塞进核心类型虚表，新 key family 会反向拖动 debugger 与算法层

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | container UX / algorithm contract 的 owner |
| 当前设计 | 当前 TypeSystem 把容器所需的“可排序”“key 如何字符串化/反解析”建成 `FAngelscriptType` 的通用虚函数；`TArray` 排序和 `TMap` debugger/member 访问直接调用这些全局 hook。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:307-310`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:334-337` — `GetStringIdentifier()/FromStringIdentifier()/IsOrdered()/CompareOrder()` 都是核心类型虚表的一部分。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp:536-542`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp:1316-1323` — `TArray.Sort()` 默认直接走 `Ops->Type.CompareOrder()`，并在 family 未声明可排序时要求脚本类型自己提供 `opCmp`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp:334-345`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp:420-435` — map debugger 展示先尝试 `KeyType.GetStringIdentifier()` 生成 `[Key]` 标签，成员访问又反过来靠 `FromStringIdentifier()` 把 `[Key]` 解析回 key buffer。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Primitives.cpp:135-159`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FName.cpp:90-125` — primitive/FName 等基础类型额外实现这些 hook，只是为了满足 array sort 与 map debugger 这类 container 消费方。 |
| 优点 | 现有调试体验比较直接：基础 key 类型能显示成 `[Name]` / `[123]`，数组也能在没有额外 comparator 的情况下获得默认排序。 |
| 不足 | container-specific 需求反向侵入了所有 type family。新增一个 map key family 或 future `TScriptInterface<>` / wrapper key，即便 property bridge 已经成立，也还要回答“全局排序怎么定义”“debugger selector 如何 stringify/parse”这些并非类型核心语义的问题。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FLuaMap` 把 key/value 行为局限在 wrapper 内部：map 操作只依赖 `KeyInterface/ValueInterface` 的 hash、identical、copy、destruct；需要展示 key 时，直接导出 `Keys()` 为 `FLuaArray`，而不是要求每个 key family 实现字符串 selector。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Containers/LuaMap.h:126-156`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Containers/LuaMap.h:170-209`<br>`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Containers/LuaMap.h:306-324` | 把“容器如何显示/遍历 key”留在 container wrapper，而不是抬升成全局 type contract。 |
| puerts | `FScriptMapWrapper` 在 wrapper 对象上直接保存 key/value `FPropertyTranslator`；map 的 add/find 只用 `FProperty::GetValueTypeHash/Identical`，而 `GetKey()` 直接把 key 通过 translator 转成 JS 值。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/ContainerWrapper.cpp:516-592`<br>`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/ContainerWrapper.cpp:679-709` | key 的展示与交互属于 wrapper + translator，不需要为每个 family 发明额外字符串化 hook。 |
| UnrealCSharp | `FMapHelper` 由 `KeyPropertyDescriptor/ValuePropertyDescriptor` 驱动：查找和写入只依赖 `Identical/GetValueTypeHash/Set`，枚举则通过 `GetEnumeratorKey()/GetEnumeratorValue()` 暴露原始 key/value 指针。 | `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Container/FMapHelper.cpp:83-108`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Container/FMapHelper.cpp:174-208`<br>`Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Container/FMapHelper.cpp:246-255` | container helper 自己拥有迭代和展示入口，descriptor 只提供值语义最小集合。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 container-specific 的排序、key 标签和 selector 解析从 `FAngelscriptType` 虚表里移出来，收敛到 container policy / presentation 层。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` 或 `Core/` 新增 `FAngelscriptContainerElementPolicy` / `FAngelscriptContainerPresentation`，至少包含 `Hash`、`Equals`、`Compare`、`RenderKeyLabel`、`ParseKeySelector`、`ProjectDebuggerKey` 六类可选 hook。 2. `Bind_TMap.cpp` 与 `Bind_TArray.cpp` 优先从 policy 取排序/展示能力；旧的 `GetStringIdentifier()/FromStringIdentifier()/IsOrdered()/CompareOrder()` 先适配成 fallback，不立即删除。 3. 第一阶段只迁移 `TMap` debugger/member selector：若没有显式 `RenderKeyLabel`，默认显示 `[index]` 并把 key/value 作为子节点暴露，避免 future family 被 `GetStringIdentifier()` 卡死。 4. 第二阶段迁移 `TArray.Sort()`：array family 先查 policy comparator，再回退到现有 type-level `CompareOrder()`；这样 future family 可以只为 array policy 提供比较语义，不必修改全局 type 虚表。 5. 在测试层增加 `TMap<FName, int>`、`TMap<StructKey, UObject>`、`TArray<FName>`、future `TArray<TScriptInterface<...>>` 原型回归，确认旧标签/排序保持兼容，同时新 family 可以不实现全局字符串 selector。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Primitives.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FName.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/`, `Plugins/Angelscript/Source/AngelscriptTest/` |
| 预估工作量 | M |
| 架构风险 | 如果直接删掉旧 type-level hook，现有 `[Name]`/`[123]` selector 与默认 sort 行为会立刻回归；第一阶段必须让 policy 层完整兼容旧行为，再逐步降低对全局虚表的依赖。 |
| 兼容性 | 对现有脚本容器 API、已有 map debugger 标签和数组排序调用保持向后兼容；首阶段只是重排 owner，不改用户脚本写法。 |
| 验证方式 | 1. 回归 `TMap<FName, int32>` 与 `TMap<int32, FString>`，确认 `[Key]` 标签和成员访问保持不变。 2. 对 `TMap<StructKey, UObject>` 增加 debugger 用例，确认即便没有字符串 selector，也至少能稳定按 index 浏览 key/value。 3. 对 `TArray<FName>` / `TArray<int32>` 排序回归，并加一组 future `TArray<TScriptInterface<...>>` 原型，确认新 family 不再被全局 `CompareOrder()`/`GetStringIdentifier()` 绑定。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-TS-74 | generic/wrapper family 缺少 concrete-instantiation owner，仍依赖“半注册 + finder 补全” | 结构性收敛 | 高 |
| P2 | Arch-TS-75 | container 的排序与 key 展示语义反向侵入核心类型虚表 | owner 重分层 | 中 |
