# Angelscript 插件差距分析与经验吸收建议

> **分析日期**: 2026-04-08
> **对比基准**: UnrealCSharp / UnLua / puerts / sluaunreal
> **说明**: `Documents/AutoPlans/ReferenceComparison/` 下的前置分析文件本轮均不存在；本次先读取 `Documents/Comparisons/2026-04-07/*.md`、`Documents/AutoPlans/DiscoveryPlans/*_Plan.md`、`Documents/AutoPlans/ArchitectureReview/*_ArchReview.md`，再回到 `Plugins/Angelscript/` 与 `Reference/` 实际源码取证。

## 执行摘要

- 当前最大阻塞点不是“接口完全没有能力”，而是 `UInterface` 仍被拆成 `Bind_UObject` 的 cast/query、`ClassGenerator` 的 full-reload 壳、以及 UHT stub 三段孤立路径；这已经直接撞上 `P10 UInterface` 主线。证据链集中在 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:656-662,962-1047`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp:100-105,135-219`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2762-2797,3359-3365,5059-5179`。
- 当前仓库已经有 `AS_FunctionTable_*.cpp`、`Docs/angelscript/generated/*.hpp` 和 live `DebugDatabase` 三套 D6 资产，但它们没有统一 contract；尤其 `UInterface/NativeInterface` 在 UHT 侧仍被整体压成 `ERASE_NO_FUNCTION()`，导致 Bind API GAP 难以稳定量化。证据见 `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:45-76,449-488`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs:18-106,171-177`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp:675-755`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:1545-1617`。
- D4 不是当前最弱项。相对参考插件，Angelscript 已经具备比它们更显式的 `ECompileResult -> ReloadRequirement` 语义桥；真正缺的是 `file -> symbol/type` 预过滤和 reload 后的显式 rebind 通知，而不是另起一套热重载哲学。证据见 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:104-109`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp:329-349`。
- 最值得吸收的不是某一个插件的整套技术栈，而是三类模式组合：UnrealCSharp 的 `class/interface` 同源生成链，UnLua 的 `FInterfaceProperty` 一等 property bridge，以及 puerts 的 `declaration artifact + rebind` 显式合同。

## 差距矩阵

| 维度 | 当前主路径 | 总评等级 | 主要差距类型 | 优先级 |
| --- | --- | --- | --- | --- |
| D2 反射绑定机制 | `Bind_BlueprintType` + `Bind_UObject` + `ClassGenerator` | 能力缺失 | native `UInterface` 自动暴露、`FInterfaceProperty/TScriptInterface<>`、typed callable/signature 仍未进入主链 | P0 |
| D6 代码生成与 IDE 支持 | `UHT FunctionTable` + `.hpp docs dump` + live `DebugDatabase` | 实现差异 | 资产已存在，但 contract 分裂；缺少统一 type catalog / IDE manifest | P1 |
| D4 热重载 | watcher 队列 + `ReloadReq` + `ECompileResult` | 实现差异 | 语义合同已强于多数参考实现；差距在 prefilter 与 rebind/observer 闭环 | P2 |

> 本轮按“深度优先”只深挖 `D2 / D6 / D4` 三个维度。未纳入本轮的 `D1 / D3 / D5 / D7-D11` 不在此文件做浅层结论。

## 按维度详细分析

### D2 反射绑定机制：`UInterface` 仍未进入统一的 type/property/callable 主链

**当前状态**

当前实现已经有“接口能参与运行时判断”的局部能力，但 owner 被拆在三段不同链路里：

- native `UClass` 统一通过 `BindUClass()` 注册为 `ReferenceClass + FUObjectType`，并没有 interface 专用注册分支，见 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:656-662`。
- native 暴露筛选只认 `CLASS_Native`、`BlueprintType`、`BlueprintCallable/BlueprintEvent`，没有把 `CLASS_Interface` 作为 first-class bind family，见 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:962-1047`。
- 运行时只在 `Bind_UObject.cpp` 暴露了 `ImplementsInterface()` 和 `opCast`，说明接口更多被当成“cast/query 目标”，而不是和 object/container/delegate 同级的 property/callable family，见 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp:100-105,135-219`。
- script-defined interface 壳与实现校验主要落在 `ClassGenerator` 的 full-reload/materialization 路径：接口类会设置 `CLASS_Interface`，实现类则在 finalize 阶段靠字符串解析与 `TObjectIterator<UClass>` 回查补 `ImplementedInterfaces`，见 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2762-2797,3359-3365,5059-5179`。

```
[Angelscript] D2 Current Owner Split
├─ Bind_BlueprintType::BindUClass()                 // native UObject/UClass 统一注册
├─ Bind_UObject::ImplementsInterface/opCast         // 运行时只暴露 cast/query
├─ ClassGenerator::bIsInterface path                // script-defined interface 壳
└─ ClassGenerator::ImplementedInterfaces finalize   // 实现类再晚期补接口校验
```

[1] 当前 native bind 入口并没有给 `CLASS_Interface` 建 first-class 分支：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp
// 函数: BindUClass / ShouldBindEngineType
// 位置: native UClass 统一注册入口
// ============================================================================
static void BindUClass(UClass* Class, const FString& TypeName)
{
	auto Class_ = FAngelscriptBinds::ReferenceClass(TypeName, Class);
	auto Type = MakeShared<FUObjectType>(Class, TypeName, Class_.GetTypeInfo());
	FAngelscriptType::Register(Type); // ★ class/interface 当前共用同一条 UObject family 注册入口
}

bool ShouldBindEngineType(UClass* Class)
{
	if (!Class->HasAnyClassFlags(CLASS_Native))
		return false;

	if (Class->GetBoolMetaData(NAME_BlueprintType))
		return true;

	// ★ 这里只检查 BlueprintCallable/BlueprintEvent，没有 `CLASS_Interface` 的独立放行策略
	if (Function->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintEvent))
		bHasBlueprintCallable = true;
}
```

[2] 当前 interface 壳和实现校验被留在 class generator 的 late path：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 函数: DoFullReloadClass / FinalizeClass
// 位置: script-defined interface materialization 与实现校验
// ============================================================================
else if (ClassData.NewClass->bIsInterface)
{
	// ★ interface 壳不走普通属性/脚本函数路径，而是另起一条 metadata-only 分支
	UClass* SuperClass = InterfaceDesc->CodeSuperClass;
	if (SuperClass == nullptr)
		SuperClass = UInterface::StaticClass();
	NewClass->SetSuperStruct(SuperClass);
}

if (ClassDesc->bIsInterface)
{
	NewClass->ClassFlags |= CLASS_Interface | CLASS_Abstract;
	// ★ interface 语义在 class shell 阶段补标志，而不是在 bind family 阶段显式建模
}

if (ClassDesc->ImplementedInterfaces.Num() > 0 && !ClassDesc->bIsInterface)
{
	// ★ 实现类 late-bound 地解析 interface，并在 finalize 时补 `ImplementedInterfaces`
	UClass* InterfaceClass = ResolveInterfaceClass(InterfaceName);
	ImplementedInterface.Class = InterfaceClass;
	NewClass->Interfaces.Add(ImplementedInterface);
}
```

**差距描述**

- **能力缺失**：native `UInterface` 仍未自动进入 `BindUClass` 主链。`Bind_BlueprintType.cpp:962-1047` 的筛选逻辑没有 interface 专门策略，这和 `Documents/Plans/Plan_CppInterfaceBinding.md:21-22,78-98` 中明确列出的主线缺口完全一致。
- **能力缺失**：`FInterfaceProperty / TScriptInterface<>` 没有进入 property/type pipeline。当前 runtime 搜索没有 `FInterfaceProperty` 适配器，`Bind_UObject.cpp` 只补 cast/query，不足以支撑 `P10` 所需的 by-value、const ref、out ref、container 嵌套。
- **实现差异**：接口 owner 分裂。当前是 `Bind_BlueprintType`、`Bind_UObject`、`ClassGenerator` 三段拼起来；最佳参考实现把 interface 当作 class/property/callable graph 的一部分统一建模。
- **实现质量差异**：接口身份解析仍含 late-bound heuristic。`ResolveInterfaceClass()` 会在 `FAngelscriptType::GetByAngelscriptTypeName()` 失败后退回 `TObjectIterator<UClass>` 按名字猜，稳定性弱于 descriptor-first 方案。

**参考方案**

- **UnrealCSharp** 把 interface 合并进 class-generation/runtime-bind 主链，而不是拆成旁路：
  - `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp:94-126,257-277`
  - `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FCSharpBind.cpp:179-203`
- **UnLua** 把 interface 同时建成 class flag 和 property bridge：
  - `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/ClassDesc.cpp:29-36`
  - `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:533-560`

[3] UnrealCSharp 的 interface 直接并进 class generation 和 runtime function map：

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp
// 函数: FClassGenerator::Generator
// 位置: class/interface/function 同源生成链
// ============================================================================
auto bIsInterface = InClass->IsChildOf(UInterface::StaticClass());

for (auto Interface : InClass->Interfaces)
{
	InterfaceContent += FString::Printf(TEXT(", %s"),
		*FUnrealCSharpFunctionLibrary::GetFullInterface(Interface.Class));
}

for (const auto& InInterface : InClass->Interfaces)
{
	for (TFieldIterator<UFunction> FunctionIterator(InInterface.Class,
		EFieldIteratorFlags::IncludeSuper,
		EFieldIteratorFlags::ExcludeDeprecated); FunctionIterator; ++FunctionIterator)
	{
		// ★ interface 函数和 class 函数走同一条生成主链
	}
}
```

[4] UnLua 的 `FInterfacePropertyDesc` 证明 interface 应该进入 property 主桥，而不是只停在 cast/query：

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp
// 结构: FInterfacePropertyDesc
// 位置: interface 的 property bridge
// ============================================================================
virtual void GetValueInternal(lua_State *L, const void *ValuePtr, bool bCreateCopy) const override
{
	const FScriptInterface &Interface = InterfaceProperty->GetPropertyValue(ValuePtr);
	UnLua::PushUObject(L, Interface.GetObject()); // ★ interface 值桥接进入统一 property pipeline
}

virtual bool SetValueInternal(lua_State *L, void *ValuePtr, int32 IndexInStack, bool bCopyValue) const override
{
	FScriptInterface *Interface = (FScriptInterface*)ValuePtr;
	UObject *Value = UnLua::GetUObject(L, IndexInStack);
	Interface->SetObject(Value);
	Interface->SetInterface(Value ? Value->GetInterfaceAddress(InterfaceProperty->InterfaceClass) : nullptr);
	return true;
}
```

**吸收建议**

- 优先走**插件内可落地**路线，不把修改 AngelScript 2.33.0 runtime 或 Unreal Engine 当作 `P0` 前置。具体上先沿用当前 `ReferenceClass + plainUserData` 模式，把 native `UInterface` 自动注册接进 `Bind_BlueprintType.cpp`，而不是等待 `RegisterInterface()` 或第三方引擎 patch。
- 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/` 新增 `FAngelscriptPropertySignature / FAngelscriptPropertyBridgeDesc`，把 `Object / Interface / Array / Map / Set / Optional / Delegate` 统一成结构化 family；`FInterfaceProperty / TScriptInterface<>` 只新增一条 family，不再在 `ClassGenerator`、`UHTTool`、`Bind_UObject` 三处各写一套特判。
- 让 `CallInterfaceMethod`、`Bind_UObject::opCast`、UHT sidecar 与未来 `FInterfaceProperty` 共享同一份 typed callable/property descriptor，避免 interface 继续停留在“名字校验 + late-bound owner”模式。
- 当前 `ClassGenerator` 的 `bIsInterface` 壳生成可以保留，但应从“主 owner”降级为“materialization 尾段”；真正的一等 owner 应前移到 bind/type/property pipeline。

**优先级**

- **P0**
- **理由**：`Documents/Plans/Plan_CppInterfaceBinding.md:21-22,78-98` 已把 “C++ UInterface 未自动注册 / FInterfaceProperty 未参与绑定” 定义成当前主线缺口；`Documents/AutoPlans/ArchitectureReview/TypeSystem_ArchReview.md:199,225` 又明确指出这条缺口直接阻塞 `P10`。

### D6 代码生成与 IDE 支持：资产不缺，统一 contract 缺

**当前状态**

当前仓库并不是没有 D6 资产，而是已经有三套并行产物：

- `AngelscriptFunctionTableExporter` / `AngelscriptFunctionTableCodeGenerator` 在 UHT 阶段生成 `AS_FunctionTable_*.cpp`、summary JSON/CSV，见 `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:21-54` 与 `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:45-76,174-205`。
- `FAngelscriptDocs::DumpDocumentation()` 会把当前脚本 API 落成 `Docs/angelscript/generated/*.hpp`，见 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.h:16-36` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp:675-755`。
- `FAngelscriptDebugServer::SendDebugDatabase()` 会向 IDE 客户端推送 live `doc / keywords / meta / params` schema，见 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:1545-1617`。

真正的缺口在于：

- `UInterface / NativeInterface` 在 UHT 侧被整体打成 `ERASE_NO_FUNCTION()`，见 `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:465-476`。
- 头文件签名恢复仍然依赖文本级 `FindCandidates + NormalizeTypeText`，并主动去掉 `const` / `&` 再比较，见 `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs:18-106,171-177`。

```
[Angelscript] D6 Current Artifact Split
├─ UHT exporter -> AS_FunctionTable_*.cpp           // 运行时函数注册 sidecar
├─ HeaderSignatureResolver -> text match            // 文本级签名恢复
├─ Docs::DumpDocumentation -> *.hpp                 // 离线文档快照
└─ DebugServer::SendDebugDatabase                   // 运行期 live IDE schema
```

[1] 当前 UHT 侧能生成 shard 和 summary，但 interface 仍被 blanket-stub：

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs
// 函数: CollectEntries
// 位置: UHT function table 生成主链
// ============================================================================
if (classObj.ClassType == UhtClassType.Interface || classObj.ClassType == UhtClassType.NativeInterface)
{
	eraseMacro = "ERASE_NO_FUNCTION()"; // ★ interface 在生成阶段被整体降成 stub
}
else if (AngelscriptFunctionSignatureBuilder.TryBuild(classObj, function, out AngelscriptFunctionSignature? signature, out string? _))
{
	eraseMacro = signature!.BuildEraseMacro();
}
else
{
	eraseMacro = "ERASE_NO_FUNCTION()";
}
```

[2] 当前签名恢复仍是文本归一化，而不是结构化 type graph：

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs
// 函数: TryBuild / NormalizeTypeText
// 位置: 头文件文本恢复函数签名
// ============================================================================
List<CandidateDeclaration> candidates = FindCandidates(header, classBodyStart, classBodyEnd, function.SourceName);
...
if (parsedSignature!.ParameterTypes.Count == expectedParameterTypes.Count &&
	AreTypesEquivalent(expectedParameterTypes, parsedSignature.ParameterTypes) &&
	NormalizeTypeText(expectedReturnType) == NormalizeTypeText(parsedSignature.ReturnType))
{
	exactMatches.Add(parsedSignature);
}

private static string NormalizeTypeText(string typeText)
{
	return CollapseWhitespace(typeText)
		.Replace("const ", string.Empty, StringComparison.Ordinal)
		.Replace("&", string.Empty, StringComparison.Ordinal)
		.Replace(" ", string.Empty, StringComparison.Ordinal)
		.Trim(); // ★ `const/ref/interface/container` 最终都被压回字符串比较
}
```

**差距描述**

- **实现差异**：当前 D6 资产已经存在，但不是一个统一 contract。`FunctionTable`、`.hpp docs`、`DebugDatabase` 各自成立，却无法回答“某个 interface/container family 是否已有 runtime owner、为何仍被 stub、IDE 应该消费哪一份真相”。
- **能力缺失**：没有面向 `P10/Bind API GAP` 的 `TypeCatalog` 或 `IDE manifest`。所以 interface/container family 的 gap 只能靠人工交叉比对 UHT 输出、runtime bind、debug 数据库。
- **实现质量差异**：UHT 仍靠文本头文件恢复签名，并对 interface 直接 `ERASE_NO_FUNCTION()`，导致 `UInterface` 不可能沿现有 sidecar 逐步放开。

**参考方案**

- **UnrealCSharp** 把 class/struct/enum/binding 生成放在同一条 generator pipeline：
  - `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:264-305`
- **puerts** 把声明资产落成 `ue.d.ts / ue_bp.d.ts`，并把接口函数也并入声明图：
  - `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:360-457`
  - `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1311-1327`

[3] puerts 的 `DeclarationGenerator` 证明“IDE artifact”应该是明确、稳定、可落盘的：

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp
// 函数: FTypeScriptDeclarationGenerator::GenTypeScriptDeclaration / GenClass
// 位置: d.ts 生成主链
// ============================================================================
for (int i = 0; i < Class->Interfaces.Num(); i++)
{
	for (TFieldIterator<UFunction> FunctionIt(Class->Interfaces[i].Class, EFieldIteratorFlags::IncludeSuper);
	     FunctionIt; ++FunctionIt)
	{
		if (!GenFunction(TmpBuff, *FunctionIt))
		{
			continue;
		}
		TryToAddOverload(Outputs, FunctionIt->GetName(),
			(FunctionIt->FunctionFlags & FUNC_Static) != 0, TmpBuff.Buffer); // ★ 接口函数进入同一份声明图
	}
}

FFileHelper::SaveStringToFile(ToString(), *UEDeclarationFilePath,
	FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM); // ★ 统一落成 `ue.d.ts`
```

**吸收建议**

- 不要推翻现有三套产物；先在 `AngelscriptUHTTool` 上叠加一份**只读统一资产**，例如 `AS_TypeCatalog.json` 或 `AS_IdeManifest.json`。首版只回答四个问题：`Kind`、`Owner`、`InterfaceStubbed`、`FailureReason`。
- 保持 `AS_FunctionTable_*.cpp` 只服务 runtime registration，不强迫它同时承担 IDE schema；而 `Docs::DumpDocumentation()` 与 `DebugDatabase` 改为复用同一份 catalog/manifest 做交叉链接。
- 在 catalog 层显式保留 `Interface / NativeInterface / Array / Map / Set / Optional / Delegate` family，而不是继续在 `HeaderSignatureResolver` 里把一切压回字符串。
- 这条路线完全可以在插件内完成：只动 `AngelscriptUHTTool`、`AngelscriptRuntime/Core` 和 `Debugging`，不需要改 Unreal Engine 或 AngelScript runtime。

**优先级**

- **P1**
- **理由**：`Documents/Guides/TechnicalDebtInventory.md:266` 已把 type metadata 缺失归到 `Bind API GAP`；`Documents/AutoPlans/ArchitectureReview/TypeSystem_ArchReview.md:574,589,714,750` 进一步说明 catalog/structured type metadata 是 `P10` 的关键支撑，但它本身不能替代 runtime interface bridge，所以放在 `D2/P0` 之后。

### D4 热重载：主干不弱，缺的是 prefilter 与 rebind 闭环

**当前状态**

当前 D4 主链实际上已经比不少参考实现更“语义化”：

- watcher 输入先经过 `QueueScriptFileChanges()` 做脚本根归一化、文件/目录增删分流，见 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp:43-89`。
- runtime 结果不是 bool，而是 `Error / ErrorNeedFullReload / PartiallyHandled / FullyHandled` 四档，见 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:104-109`。
- 测试 helper 还能把 compile result 继续翻译成 `SoftReload / FullReloadSuggested / FullReloadRequired`，见 `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp:329-349`。

但当前仍有两个明显短板：

- reload 输入还是“文件 -> 模块 -> ReloadReq”这条粗粒度路径，缺少 file-to-symbol/type 预过滤。
- reload 完成后没有统一的 `Rebind/ReloadComplete` 通知 contract 给脚本对象、函数句柄、IDE cache；这和现有 hot reload 发现的 stale `UASFunction` silent no-op 问题是同一类 owner 缺口。

```
[Angelscript] D4 Current
├─ DirectoryWatcher -> QueueScriptFileChanges       // 路径归一化与增删分流
├─ Compile -> ECompileResult                        // 四档语义结果
├─ ClassGenerator::ReloadReq propagation            // 语义级 reload 判定
└─ SoftReload / FullReload                          // 分阶段执行
```

[1] 当前主链已经有明确的语义结果，不是单纯“编译成败”：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h
// 位置: compile outcome 顶层契约
// ============================================================================
enum class ECompileResult : uint8
{
	Error,
	ErrorNeedFullReload,
	PartiallyHandled,
	FullyHandled,
};
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp
// 函数: AnalyzeReloadFromMemory
// 位置: compile result -> reload requirement 语义桥
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
```

**差距描述**

- **实现差异**：当前决策轴是 `ReloadReq` 语义边界；参考插件里更常见的是 `file -> dynamic type` 预过滤或 `ReloadComplete -> Rebind` 明确广播。
- **实现质量差异**：当前 reload 结束后没有统一的 rebind observer。对象、函数句柄、IDE cache 对“某个 type 已换 epoch”感知不足。
- **无能力缺失**：soft/full reload、语义化 compile result、watcher 队列、测试 helper 当前都已经存在，不应误判成“没有热重载体系”。

**参考方案**

- **UnrealCSharp**：`FEditorListener` + `FCSharpCompilerRunnable` 提供 latest-wins 文件监听/编译门控：
  - `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:18-65`
  - `Reference/UnrealCSharp/Source/Compiler/Private/FCSharpCompilerRunnable.cpp:109-155`
- **puerts**：`SourceFileWatcher` + `ReloadCompleteDelegate` + `NotifyRebind` 提供显式 rebind 闭环：
  - `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/SourceFileWatcher.cpp:22-80`
  - `Reference/puerts/unreal/Puerts/Source/Puerts/Private/PuertsModule.cpp:424-438`
  - `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:1209-1219`
  - `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/TypeScriptGeneratedClass.cpp:77-99`

[2] puerts 的价值不在“更复杂”，而在 reload 完成后有显式 `NotifyRebind`：

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/TypeScriptGeneratedClass.cpp
// 函数: UTypeScriptGeneratedClass::NotifyRebind
// 位置: reload -> class/object rebind 通知
// ============================================================================
void UTypeScriptGeneratedClass::NotifyRebind(UClass* Class)
{
	if (Class->ClassConstructor == &UTypeScriptGeneratedClass::StaticConstructor)
	{
		while (Class)
		{
			if (UTypeScriptGeneratedClass* TsClass = Cast<UTypeScriptGeneratedClass>(Class))
			{
				if (TsClass->NeedReBind && TsClass->DynamicInvoker.IsValid())
				{
					TsClass->NeedReBind = false;
					CachedClass->DynamicInvoker.Pin()->NotifyReBind(CachedClass); // ★ reload 结束后显式通知重新注入
				}
			}
		}
	}
}
```

**吸收建议**

- 保留当前 `ECompileResult + ReloadReq` 主轴，不要为了“看起来像参考实现”而退回 file-driven 黑箱流程。
- 在现有 watcher 之前或 compile 前，补一层轻量 `file -> module/type` 预过滤。实现上可以先复用已有 module/code section 信息，不需要引入第二套完整的 generator/runtime。
- 在 runtime/core 或 debug lane 增加显式 `ReloadComplete / RebindRequested` 通知 contract，让 `UASFunction`、IDE cache、测试工具和对象代理都能拿到“某个 owner 已换代”的信号。
- 这条路线应后置到 `P0/P1` 之后，因为它更适合消费新建的 interface/property catalog，而不是继续堆新的 heuristics。

**优先级**

- **P2**
- **理由**：D4 当前不是 `P10` 的直接 blocker；而且若在 interface/property/type catalog 还没收敛前先做 hot reload 扩面，会把同一批 identity/owner 问题放大到更多路径上。

## 值得吸收的设计模式

| 模式 | 参考实现 | 可吸收点 | 对当前仓库的落点 |
| --- | --- | --- | --- |
| `Class / Interface / Property` 同源建模 | `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp`、`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp` | interface 不再是 cast/query 特例，而是 class graph 与 property graph 的一等输入 | `Bind_BlueprintType.cpp`、`Core/AngelscriptType*`、`ClassGenerator` |
| 构建资产与 live 协议分层 | `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` | build-time artifact 负责稳定 contract，runtime protocol 负责即时状态；两者共享 schema，不共享职责 | `AngelscriptUHTTool`、`AngelscriptDocs`、`DebugServer` |
| runtime-owned registry + 可观测 bind plan | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:102-113`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:23-34,176-214` | registry owner 显式、bind 列表可枚举，便于做 manifest、审计与增量测试 | `FAngelscriptBindState`、`GetBindInfoList()`、后续 `TypeCatalog` |
| reload 后显式 rebind | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/TypeScriptGeneratedClass.cpp:77-99` | “对象/函数已经过期”要有 observer hook，不要 silent no-op | `HotReload`、`DebugServer`、`UASFunction` 调用方 |

## 改进路线建议

| 优先级 | 路线 | 工作量 | 主要落点 | 预期收益 |
| --- | --- | --- | --- | --- |
| P0 | 让 `UInterface` 进入统一 type/property/callable 主链 | M-L | `Bind_BlueprintType.cpp`、`Core/AngelscriptType*`、`ClassGenerator`、`Bind_UObject.cpp` | 直接解除 `P10 UInterface` 与 `FInterfaceProperty/TScriptInterface<>` 的主线阻塞 |
| P1 | 建立 `AS_TypeCatalog / AS_IdeManifest`，统一 `FunctionTable + Docs + DebugDatabase` contract | M | `AngelscriptUHTTool`、`Core/AngelscriptDocs*`、`Debugging/AngelscriptDebugServer.cpp` | 把 Bind API GAP 从“人工对比”变成可机器校验的资产 |
| P2 | 在现有 `ReloadReq` 之上补 `file -> module/type` 预过滤 + `ReloadComplete/Rebind` observer | M | `AngelscriptEditor` watcher、`HotReload`、`DebugServer`、相关 cache owner | 减少 reload 误扩散，同时消除 stale handle / stale IDE cache 的 silent failure |
| P3 | 把 bind/type owner 显式化为可注册 provider 与可回收 registry | M | `Core/AngelscriptBinds.cpp`、`Core/AngelscriptType*` | 为后续外部扩展、实验性 family、IDE 报表和多 engine 隔离打基础 |

建议执行顺序：

1. 先做 **P0**：只做插件内 `UInterface`/`FInterfaceProperty`/typed callable 桥接，不把修改引擎或升级 AngelScript 作为主线路径。
2. 再做 **P1**：把 `P0` 引入的新 interface/property family 变成 UHT/runtime/IDE 可见的统一 catalog。
3. 然后做 **P2**：让 hot reload 消费 `P1` 的 catalog/owner 信息，补 prefilter 和 rebind，而不是继续靠名字猜。
4. 最后做 **P3**：收 registry/provider ownership，避免下一轮能力扩展继续回到 ambient state + heuristic 模式。

## 结论

当前 Angelscript 与参考插件的真正差距，不是“功能点总数不够”，而是 **interface/type metadata 还没有一份单一真相**。只要先把 `UInterface` 和 `FInterfaceProperty` 拉进统一主链，再补统一 catalog，后续的 Bind API GAP、IDE contract 和 hot reload 质量问题都可以沿同一条 owner 路径逐步收口。

---

## 深化分析 (2026-04-08 17:57:39)

- 本轮把 `D4` 的判断从“可能存在能力缺失”修正为“实现差异为主”。当前 `ECompileResult` 与 `ReloadRequirement` 的语义桥已经比多数参考实现更清晰，后续不应轻易推翻。
- 本轮明确 `D2` 的 blocker 不在 `opCast` 或 `ImplementsInterface` 本身，而在 `Bind_BlueprintType`、`property bridge` 与 `UHT sidecar` 没有共用同一份 interface/type descriptor。
- 本轮确认 `D6` 的高性价比切入点不是重写 IDE 协议，而是把现有 `AS_FunctionTable_*`、`Docs/angelscript/generated/*.hpp`、`DebugDatabase` 统一挂到一份机器可校验的 manifest 上。

---

## 深化分析 (2026-04-08 18:10:02)

### 判定口径补充：把“总评等级”拆成可核对矩阵

| 维度 | 无差距 | 实现差异 | 实现质量差异 | 能力缺失 | 本轮补充说明 |
| --- | --- | --- | --- | --- | --- |
| D2 反射绑定机制 |  | ✓ | ✓ | ✓ | `opCast/ImplementsInterface` 只能证明“运行时 query 存在”，不能证明 `FInterfaceProperty/TScriptInterface<>` 已进入 type/property/callable 主链 |
| D6 代码生成与 IDE 支持 |  | ✓ | ✓ | ✓ | 资产本身并不缺，但缺 `TypeCatalog/IDE manifest` 这一层单一真相；因此总评仍记为“实现差异”，不是“纯能力缺失” |
| D4 热重载 |  | ✓ | ✓ |  | `SoftReload/FullReload`、watcher 队列、`ECompileResult` 已存在；差距集中在 prefilter 与 `ReloadComplete/Rebind` observer |

> 口径说明：总评等级取“当前最主要的阻塞点”，但实施排序需要同时看到该维度内部是否还混有 `实现质量差异` 或局部 `能力缺失`。

### D2 补充：`FInterfaceProperty` 缺口已经直接暴露在 property 主桥

这轮补证后，`D2` 的关键问题可以更精确地落到 `FUObjectType` 本身，而不是泛化成“interface 支持不完整”：

```
[Angelscript] D2 Property/Callable Gap
├─ FUObjectType::CreateProperty -> FClassProperty/FObjectProperty   // 没有 FInterfaceProperty
├─ FUObjectType::MatchesProperty -> CastField<FObjectProperty>      // 只认 object family
├─ Bind_TArray::CreateProperty -> Usage.SubTypes[0].CreateProperty  // container 递归放大子类型缺口
└─ ReflectiveFallback -> RejectedInterfaceClass                     // callable 侧继续把 interface 排除在外
```

[1] 当前 object family 的 property 主桥只会生成 `FClassProperty` 或 `FObjectProperty`：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp
// 结构: FUObjectType
// 位置: UObject family 的 property 创建与匹配主桥
// ============================================================================
FProperty* CreateProperty(const FAngelscriptTypeUsage& Usage, const FPropertyParams& Params) const override
{
	// ★ 唯一特殊分支是 UClass*，其余对象一律降成 FObjectProperty
	if (Class == UClass::StaticClass())
	{
		auto* Property = new FClassProperty(Params.Outer, Params.PropertyName, RF_Public);
		Property->PropertyClass = Class;
		Property->MetaClass = UObject::StaticClass();
		return Property;
	}

	auto* Property = new FObjectProperty(Params.Outer, Params.PropertyName, RF_Public);
	Property->PropertyClass = Class != nullptr ? Class : (UClass*)Usage.ScriptClass->GetUserData();
	return Property; // ★ 这里没有 FInterfaceProperty 分支
}

bool MatchesProperty(const FAngelscriptTypeUsage& Usage, const FProperty* Property, EPropertyMatchType MatchType) const override
{
	const FObjectProperty* ObjectProp = CastField<FObjectProperty>(Property);
	if (ObjectProp == nullptr)
		return false; // ★ 只匹配 FObjectProperty，interface property 不会命中这条主桥
	...
}
```

[2] container 桥是递归复用子类型 `CreateProperty()/MatchesProperty()` 的，因此子类型没有 interface family，`TArray<TScriptInterface<...>>` 也不会凭空成立：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp
// 函数: FAngelscriptArrayType::CreateProperty / MatchesProperty
// 位置: container 对子类型 property bridge 的递归复用
// ============================================================================
FProperty* FAngelscriptArrayType::CreateProperty(const FAngelscriptTypeUsage& Usage, const FPropertyParams& Params) const
{
	auto* ArrayProp = new FArrayProperty(Params.Outer, Params.PropertyName, RF_Public);
	...
	ArrayProp->Inner = Usage.SubTypes[0].CreateProperty(InnerParams); // ★ container 完全依赖子类型桥
	return ArrayProp;
}

bool FAngelscriptArrayType::MatchesProperty(const FAngelscriptTypeUsage& Usage, const FProperty* Property, EPropertyMatchType MatchType) const
{
	...
	return Usage.SubTypes[0].MatchesProperty(ArrayProp->Inner, FAngelscriptType::EPropertyMatchType::InContainer);
	// ★ 子类型没有 interface property support，则 container 也不会有
}
```

[3] callable fallback 也把 interface 直接挡在门外，因此当前不是“值桥已通、只差调用层”：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp
// 函数: GetReflectiveFallbackEligibility
// 位置: reflective callable 的准入判定
// ============================================================================
if (OwningClass->HasAnyClassFlags(CLASS_Interface))
{
	return EAngelscriptReflectiveFallbackEligibility::RejectedInterfaceClass;
	// ★ interface 在 callable fallback 上也被整体排除
}
```

### 设计取舍补充

- **D2**：当前方案把大多数 UE object 都收进 `FUObjectType`，前期实现成本低，也便于沿用 `plainUserData + UObject*` 的既有路径；代价是 interface value、container 与 callable 只能不断叠加特判，最后 owner 被拆散。
- **D6**：当前 UHT sidecar 只输出 function shard 和 coverage 统计，编译侵入小、产物稳定；代价是 type family 真相丢失，`P10` 相关差距只能靠人工交叉比对 runtime、docs 和 debug JSON。
- **D4**：当前 `ECompileResult -> ReloadRequirement` 把“需要 full reload 的原因”显式化，这是明显优点；代价是 reload 语义已经前移到编译链后，更需要一个统一的 `Rebind/Observer` 合同，否则 stale handle 会变成静默失效而不是显式错误。

---

## 深化分析 (2026-04-08 18:22:00)

- 本轮新增证据表明，`D2 / D6 / D4` 的共性不是“功能点数量不够”，而是 `interface` 相关真相没有被收敛成一份可复用 contract。
- `D2` 的主要阻塞已经可以精确落到“字符串签名 + 名字分发”链路；这比“UInterface 支持不完整”更接近当前 `P10` 的真实问题。
- `D6` 不是单纯“没有生成产物”，而是 `interface stub` 已经进入生成物，却没有进入可诊断 contract，因此 Bind API GAP 仍难以稳定量化。
- `D4` 不应再被误判为“没有热重载”；真正缺的是 `execution epoch / prepare-finish observer` 合同，而不是 reload 框架本身。

### D2 补充：interface 目前仍是“字符串签名 + 名字分发”路径，不是结构化 `type/function family`

**当前状态**

```text
[Interface Signature Flow]
interface body text
  -> NormalizeInterfaceMethodDeclaration()
  -> InterfaceMethodDeclarations[]          // raw declaration string
  -> RegisterInterfaceMethodSignature()     // 只登记 FunctionName
  -> minimal UFunction stub                 // full reload 只保留名字
  -> FindFunction(FunctionName)             // 运行时按名字转发
  -> ProcessEvent
```

- `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:1101-1109` 把 interface 方法真相定义成 `TArray<FString> InterfaceMethodDeclarations`。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:1059-1078,1106-1139` 先从 interface body 按行抽取声明文本，再从文本里二次切出 `MethodName`，最后把所有方法注册到统一的 `CallInterfaceMethod` generic callback。
- `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2803-2831` full reload 时只创建最小 `UFunction` stub；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:56-67` 真正调用时只靠 `FindFunction(Sig->FunctionName)` 找实现。

[1] 关键源码引用：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:1107-1109
// 位置: interface method 的持久化真相
// ============================================================================
TArray<FString> InterfaceMethodDeclarations;
// ★ 当前只保存原始声明字符串，不保存结构化参数/返回值/metadata

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:1077-1078,1121-1123,1134-1137
// 位置: 预处理阶段把 interface body 降成“字符串 + 方法名”
// ============================================================================
ClassDesc->InterfaceMethodDeclarations.Add(NormalizeInterfaceMethodDeclaration(Trimmed));
FString MethodName = BeforeParen.Mid(LastSpace + 1).TrimStartAndEnd();
auto* Sig = Engine.RegisterInterfaceMethodSignature(FName(*MethodName));
int32 FuncId = Engine.Engine->RegisterObjectMethod(
	TCHAR_TO_ANSI(*InterfaceName),
	TCHAR_TO_ANSI(*ASDecl),
	asFUNCTION(CallInterfaceMethod),
	asCALL_GENERIC,
	nullptr);
// ★ signature registry 只拿到 FunctionName，所有方法共用同一个 generic callback

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2818-2825,65-67
// 位置: full reload 的 interface stub + runtime dispatch
// ============================================================================
UFunction* NewFunction = NewObject<UFunction>(NewClass, *FuncName, RF_Public);
NewFunction->FunctionFlags = FUNC_Event | FUNC_BlueprintEvent | FUNC_Public;
NewFunction->Bind();
NewFunction->StaticLink(true);
UFunction* RealFunc = Object->FindFunction(Sig->FunctionName);
InvokeReflectiveUFunctionFromGenericCall(Generic, Object, RealFunc);
// ★ stub 只物化名字，真正执行时再按名字查真实函数
```

**差距描述**

- `没有实现`：当前没有一份可复用到 `property / callable / UHT / docs / debug` 的结构化 interface descriptor；`FInterfaceProperty / TScriptInterface<>` 仍未进入主桥。
- `实现方式不同`：当前采用“raw declaration -> generic callback -> runtime FindFunction”路径；参考实现普遍是“interface/type/property 同源建模”，运行时只消费既有 descriptor。
- `实现质量差异`：signature 真相在预处理、class generation、runtime dispatch 三处重复切分，参数、返回值和 metadata 无法稳定复用；这也是 full reload 只能造最小 `UFunction` stub 的根因。

**参考方案**

- **UnrealCSharp**：`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicInterfaceGenerator.cpp:154-180,231-265,315-322` 为 interface 建独立生成生命周期；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:415-430` 把 `FInterfaceProperty` 直接映射成 `TScriptInterface<>`；`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:144-149` 用同一份信息输出生成代码类型。
- **UnLua**：`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:531-575,1592-1594` 把 `FInterfacePropertyDesc` 做成 property descriptor 的一等分支；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/FunctionDesc.cpp:48-49,209-210,562-564` 则让 interface function 与普通 function 共用 descriptor 体系，只在最终调用时识别 owner。

[2] 关键参考源码引用：

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:426-430
// 位置: interface property -> typed generic
// ============================================================================
const auto FoundGenericClass = FReflectionRegistry::Get().GetTScriptInterfaceClass();
const auto FoundClass = FReflectionRegistry::Get().GetClass(InProperty->InterfaceClass);
return MakeGenericTypeInstance(FoundGenericClass, FoundClass);
// ★ interface property 直接物化为 typed generic，而不是降成字符串签名

// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1592-1594
// 位置: interface 作为 property descriptor 的一等分支
// ============================================================================
case CPT_Interface:
{
	PropertyDesc = new FInterfacePropertyDesc(InProperty);
}
// ★ interface property 不依赖额外字符串 suffix 或事后补洞
```

**吸收建议**

- 第一阶段不要推翻现有 `CallInterfaceMethod` 行为，也不要把升级 `AngelScript 2.33.0 WIP` 或修改引擎当成前置条件；先在插件内新增 `FAngelscriptInterfaceMethodDesc`，让 `InterfaceMethodDeclarations` 退居兼容字段或调试字段。
- 用同一份 descriptor 驱动三条链：interface stub building、`FUObjectType / Bind_TArray / future FInterfaceProperty` bridge、`AngelscriptUHTTool + Docs + DebugDatabase` 资产生成。
- `P10` 期间优先补齐最小闭环：`OwnerInterface + FunctionName + ParamList + ReturnType + Metadata`；先让 `UInterface` 与 `Bind API GAP` 能共享同一份真相，再考虑更大范围的 interface sugar。

**优先级**

- `P0`
- 理由：这条差距直接卡住 `P10 UInterface` 和 `Bind API GAP` 主线；如果 `D2` 继续停留在 raw string 阶段，后续 `D6 / D4` 只能围绕名字和特判继续补洞。

### D6 补充：interface stub 已进入生成物，但没有进入可诊断 contract，Bind API GAP 无法稳定量化

**当前状态**

```text
[Toolchain Blind Spot]
UHT class scan
  -> CodeGenerator
     -> interface => ERASE_NO_FUNCTION()
     -> Entries.csv / Summary.json 仍计入 Stub
  -> Exporter
     -> TryBuild success  => reconstructedCount++
     -> TryBuild failure  => SkippedEntries.csv + FailureReason
     -> interface special-case 不进入 skipped reason
  -> Tests
     -> 校验 totals / header / 非空 reason
     -> 不校验 interface stub 是否可追踪
```

- `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:465-479` 对 `UInterface / NativeInterface` 强制写入 `ERASE_NO_FUNCTION()`，说明 interface member 已进入 generated entry。
- `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:29-43,65-88` 的统计只区分 `TryBuild` 成功或失败；interface special-case 不会进入 `SkippedEntries.csv` 的 `FailureReason` 视图。
- `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp:459-591,594-667,669-748` 现有测试只校验 summary/csv 的总量、列头和 reason 非空，没有 interface-specific coverage 断言。

[1] 关键源码引用：

```cs
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:466-479
// 位置: generator 对 interface 的处理分支
// ============================================================================
if (classObj.ClassType == UhtClassType.Interface || classObj.ClassType == UhtClassType.NativeInterface)
{
	eraseMacro = "ERASE_NO_FUNCTION()";
}
...
entries.Add(new AngelscriptGeneratedFunctionEntry(classObj.SourceName, function.SourceName, eraseMacro));
// ★ interface member 已进入 generated entry，但 outcome 只有“Stub”，没有更细分类

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:75-87
// 位置: exporter 的 skipped 统计
// ============================================================================
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
// ★ interface 被 generator 主动 stub 的情况不会落到这里，因此 reason 视图看不到它
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp:683-702,727-748
// 位置: 现有 skipped/summary 测试口径
// ============================================================================
TestEqual(TEXT("Generated function table skipped csv test should write the expected skipped csv header"),
	SkippedLines[0], TEXT("ModuleName,ClassName,FunctionName,FailureReason"));
TestTrue(TEXT("Generated function table skipped csv rows should include non-empty failure reasons"), bFoundFailureReason);

TestEqual(TEXT("Generated function table skipped reason summary test should write the expected summary csv header"),
	SummaryLines[0], TEXT("FailureReason,SkippedCount"));
TestEqual(TEXT("Generated function table skipped reason summary test should keep aggregate counts aligned with the skipped entry csv"),
	SummedSkippedCount, SkippedLines.Num() - 1);
// ★ 测试只校验列头和聚合关系，不校验 interface stub 是否有独立 category 或稳定 join key
```

**差距描述**

- `没有实现`：缺少 interface 专属 `BindingOutcome / FailureReason` 分类和稳定 `BindingCoverageId`，无法把 `Entries / Skipped / Docs / DebugDatabase` 串成一条可核对证据链。
- `实现方式不同`：当前 generator 选择把 interface 统一压成 `ERASE_NO_FUNCTION()`；参考实现更常见的是把 interface type/member 当作 generator 的一等输入，而不是后置例外。
- `实现质量差异`：interface 明明已写入 generated entry，却不会出现在 skipped reason 统计里；现有测试也无法阻止“interface stub 被静默吞掉”的回归。

**参考方案**

- **UnrealCSharp**：`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:144-149,838-846` 让 `FInterfaceProperty` 与 `Class->Interfaces` 直接进入 generator 的 type/support contract。
- **puerts**：`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:891-894,1311-1313` 在声明生成阶段同时输出 interface property type 与 interface functions，不靠事后文本恢复。

[2] 关键参考源码引用：

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:144-149
// 位置: generator 直接输出 interface 类型
// ============================================================================
if (const auto InterfaceProperty = CastField<FInterfaceProperty>(Property))
{
	return FString::Printf(TEXT("TScriptInterface<%s>"),
		*FUnrealCSharpFunctionLibrary::GetFullInterface(InterfaceProperty->InterfaceClass));
}
// ★ generator 直接消费结构化 interface type，而不是生成后再猜

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:891-894,1311-1313
// 位置: declaration generator 的 interface 输入
// ============================================================================
AddToGen.Add(InterfaceProperty->InterfaceClass);
StringBuffer << GetNameWithNamespace(InterfaceProperty->InterfaceClass);
for (TFieldIterator<UFunction> FunctionIt(Class->Interfaces[i].Class, EFieldIteratorFlags::IncludeSuper); FunctionIt;)
{
}
// ★ property 与 interface function 都是 generator 的显式输入
```

**吸收建议**

- 在 `AngelscriptUHTTool` 内新增稳定 `BindingCoverageId`，建议最小组成是 `OwnerKind + OwnerName + FunctionName + SignatureHash`；首版只要求 sidecar 间可 join，不要求 runtime 立即全量消费。
- 把 generator outcome 显式拆成 `Direct / Stub.Interface / Stub.Unsupported / Skipped.ParseFailure`；其中 `Stub.Interface` 必须单独出现在 summary、entry csv 和 reason summary。
- 给测试补一组 interface fixture，至少覆盖三件事：`interface stub` 可见、`reason summary` 可见、`summary total` 与 `entry csv` 可按 `BindingCoverageId` 对齐。
- 这条建议不需要修改引擎，也不依赖升级 AngelScript；优先在插件和 `UHTTool` 侧把量化口径立住，再让 `Docs / DebugDatabase` 逐步接入。

**优先级**

- `P1`
- 理由：它不是当前 runtime 的直接 blocker，但它决定 `P10` 与 Bind API GAP 能否从“人工对比”转成机器可校验指标；这一步越晚做，后续对比成本越高。

### D4 补充：reload 仍没有 `execution epoch` 合同，运行中上下文与新模块会跨 epoch 混跑

**当前状态**

```text
[Reload Epoch Gap]
active context
  -> pooled context may PushState()
  -> soft reload repoints UASFunction::ScriptFunction
  -> swap-in path discards old module
  -> active/suspended old frames 没有 epoch 标记
  -> debug / stale handle / error attribution 可能跨代混杂
```

- `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:195-215` 释放 context 时直接 `check` 非 `asEXECUTION_ACTIVE/asEXECUTION_SUSPENDED`，说明当前 cleanup 合同只覆盖非运行态 context。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1795-1808` 运行中 context 可以通过 `PushState()` 被嵌套复用。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:4010-4025` 新模块 swap-in 后会直接 `DiscardModule` 旧模块。
- `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4253-4259` soft reload 的核心动作只是把 `UASFunction->ScriptFunction` 指到新函数，再调用 `SoftReloadFunction`。

[1] 关键源码引用：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:210-213
// 位置: context pool 清理前提
// ============================================================================
check(Context->GetState() != asEXECUTION_ACTIVE);
check(Context->GetState() != asEXECUTION_SUSPENDED);
Context->Unprepare();
Context->Release();
// ★ 当前 cleanup 合同不处理运行中或挂起中的 context

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1801-1805
// 位置: 运行中 context 的嵌套复用
// ============================================================================
if (State == asEXECUTION_ACTIVE
	&& (DesiredScriptEngine == nullptr || ActiveContext->GetEngine() == DesiredScriptEngine))
{
	Context = ActiveContext;
	Context->PushState();
}
// ★ 执行中的 context 可以继续承载新调用，但当前没有 epoch 标记

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:4015-4025
// 位置: swap-in 之后的旧模块退役
// ============================================================================
for (auto OldModule : DiscardedModules)
{
	if (OldModule->ScriptModule != nullptr)
	{
		Engine->DiscardModule(OldModule->ScriptModule->GetName());
		OldModule->ScriptModule = nullptr;
	}
}
// ★ 旧模块会被直接退役，没有“等待旧 epoch drain 完成”的中间层

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4255-4259
// 位置: soft reload 的函数切换动作
// ============================================================================
FuncDesc->Function = OldFuncDesc->Function;
((UASFunction*)FuncDesc->Function)->ScriptFunction = FuncDesc->ScriptFunction;
SoftReloadFunction(OldFuncDesc->Function);
// ★ soft reload 目前只换 ScriptFunction 指针，不跟踪 in-flight frame 属于哪个 epoch
```

**差距描述**

- `没有实现`：无。`SoftReload / FullReload`、`ECompileResult -> ReloadRequirement`、watcher 队列和 reload helper 当前都已经存在，不应把本维度误判成“没有热重载体系”。
- `实现方式不同`：当前以“替换 module / 替换 `ScriptFunction` 指针”为主，没有 `ReloadPrepare / ReloadFinish` observer，也没有 `ContextEpoch / ModuleEpoch`。
- `实现质量差异`：active/suspended/in-flight stack 不受 epoch 保护；当旧 frame 尚未退尽而新调用已进入新 `ScriptFunction` 时，debug、诊断和错误归因会跨代混杂。

**参考方案**

- **UnLua**：`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:381-412,475-476` 在热更时显式更新 running stack，而不是只替换静态引用。
- **puerts**：`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js:87-90` 把 reload 生命周期拆成 `HMR.prepare / HMR.finish`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/TypeScriptGeneratedClass.cpp:77-98` 与 `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:2325-2365` 再把 rebind/inject 显式落到 class/object。

[2] 关键参考源码引用：

```lua
-- ============================================================================
-- 文件: Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:381-412,475-476
-- 位置: 热更时显式修补运行中栈帧
-- ============================================================================
local function update_running_stack(co, level)
    ...
    debug.setlocal(co, level + 1, i, nv)
    ...
    return update_running_stack(co, level + 1)
end

update_running_stack(running_state, 2)
-- ★ 运行中的 frame 会被主动修补，而不是假设 reload 只影响后续调用
```

```js
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js:87-90
// 位置: reload 生命周期边界
// ============================================================================
let m = puerts.getModuleByUrl(url);
puerts.emit('HMR.prepare', moduleName, m, url);
let res = await sendCommand("Debugger.setScriptSource", {scriptId:"" + scriptId, scriptSource:source});
puerts.emit('HMR.finish', moduleName, m, url);
// ★ reload 前后有显式 prepare/finish 边界，便于 observer 与 rebind 消费
```

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/TypeScriptGeneratedClass.cpp:85-98
// 位置: reload 结束后的显式 rebind
// ============================================================================
if (TsClass->NeedReBind && TsClass->DynamicInvoker.IsValid())
{
	TsClass->NeedReBind = false;
	CachedClass->DynamicInvoker.Pin()->NotifyReBind(CachedClass);
}
// ★ reload 完成后显式通知 rebind，而不是让旧对象静默承受 stale state
```

**吸收建议**

- 在插件层新增 `ReloadEpoch`、`ContextEpoch` 与 `PendingRetireModules`，不要修改引擎，也不要把 AngelScript runtime core 版本升级当成前置条件。首阶段只要求“能观测、能延迟退役、能断言”，不要求彻底迁移旧 frame。
- 在现有 reload 流程上加轻量 `ReloadPrepare / ReloadFinish` observer，让 `DebugServer`、函数缓存、对象代理和未来 `TypeCatalog` 都能明确感知“谁已经换 epoch”。
- 第一阶段先做诊断型 contract：记录 epoch、阻止明显跨代复用、把旧模块退役延迟到无相关 active context；第二阶段再考虑更激进的栈帧迁移策略。

**优先级**

- `P2`
- 理由：它不是当前 `P10 UInterface / Bind API GAP` 的直接 blocker；在 `D2 / D6` 的单一真相尚未收敛前，先扩 hot reload 语义只会放大跨代 owner 不清的问题。

---

## 深化分析 (2026-04-08 18:31:00)

### 差距矩阵增补

| 维度 | 聚焦子问题 | 总评等级 | 主要差距类型 | 优先级 |
| --- | --- | --- | --- | --- |
| D3 Blueprint 交互 | native `UInterface` callable 暴露 | 能力缺失 | query/cast 已有，但 interface function 仍未进入正向 callable path | P0 |
| D4 热重载 | impact 收集与 rebind 编排 | 实现差异 | watcher、scanner、reload helper 各自成立，但没有统一 `ReloadImpactManifest` | P1 |
| D9 测试基础设施 | `P10` / Bind API GAP 的端到端回归 | 实现质量差异 | artifact test、runtime test、eligibility test 分离，且负向行为已被锁成基线 | P0 |

### D3 Blueprint 交互补充：native interface callable 仍停在“能判定、不能正向调用”

**当前状态**

- `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp:100-105` 只给脚本侧暴露了 `ImplementsInterface()` 查询。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp:254-287,374-419` 把 owning class 带 `CLASS_Interface` 的 `UFunction` 整体判成 `RejectedInterfaceClass`，即使参数列表本身并不复杂。
- `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptBlueprintCallableReflectiveFallbackTests.cpp:228-250` 还把这条拒绝写成了显式正确行为，因此当前 D3 contract 仍是“负向守门”，不是“正向 callable bridge”。

```text
[Current Interface Callable Path]
script object
├─ ImplementsInterface(UClass)                     // 只有 query
│  └─ UObject::GetClass()->ImplementsInterface()
└─ BlueprintCallableReflectiveFallback
   ├─ owning class has CLASS_Interface ? yes
   └─ RejectedInterfaceClass                       // 正向 callable path 到此结束
```

[1] 当前实现把 interface 暴露停在 query，并在 reflective callable 准入阶段整体拒绝：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp
// 函数: UObject::ImplementsInterface binding
// 位置: 脚本侧 interface 能力目前主要停在 query
// ============================================================================
UObject_.Method("bool ImplementsInterface(const UClass InterfaceClass) const",
	[](UObject* Object, UClass* InterfaceClass)
	{
		if (Object == nullptr || InterfaceClass == nullptr)
			return false;
		return Object->GetClass()->ImplementsInterface(InterfaceClass); // ★ 这里只有 query，没有 callable bridge
	});

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp
// 函数: EvaluateReflectiveFallbackEligibility
// 位置: reflective callable 的准入判定
// ============================================================================
if (OwningClass->HasAnyClassFlags(CLASS_Interface))
{
	return EAngelscriptReflectiveFallbackEligibility::RejectedInterfaceClass;
	// ★ interface owner 在 callable fallback 上被整体排除
}
```

[2] 当前自动化还把“interface 被拒绝”固化成正确基线：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptBlueprintCallableReflectiveFallbackTests.cpp
// 函数: FAngelscriptBlueprintCallableReflectiveFallbackEligibilityTest::RunTest
// 位置: interface reflective fallback 当前的测试口径
// ============================================================================
TestEqual(
	TEXT("Reflective fallback should reject interface-class functions explicitly"),
	EvaluateReflectiveFallbackEligibility(InterfaceFunction),
	EAngelscriptReflectiveFallbackEligibility::RejectedInterfaceClass);
// ★ 这说明当前测试保护的是“拒绝 interface”，不是“允许可调用 interface”
```

**差距描述**

- **能力缺失**：native interface function 仍不能进入正向 callable path。只补 `FInterfaceProperty` 或 `TScriptInterface<>` 还不够，如果 owner 仍被 `RejectedInterfaceClass` 整体挡掉，脚本依旧只能“知道某对象实现了接口”，不能稳定调用接口函数。
- **实现方式不同**：当前把 interface 当成“owner category 例外”；更成熟的参考实现把 interface method 当作 wrapper/prototype 上的普通 callable family 处理，再由参数/返回值 family 决定是否可导出。
- **实现质量差异**：自动化已把“拒绝 interface callable”编码成回归基线，这会让后续 `P10` 修复缺少正向成功用例，甚至在修好后仍被旧测试噪音拖累。

**参考方案**

- **puerts**：`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:312-327` 会枚举 `Class->Interfaces` 并把 interface `UFunction` 直接挂到 JS prototype。
- **UnLuaTestSuite**：`Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Tests/Issue595TestInterface.h:19-31` 与 `.../Issue595Test.cpp:33-47` 用一个真实回归锁住“Lua 能调用 interface 中的函数”。

[3] puerts 不是把 interface method 先排除，而是显式并入 wrapper：

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp
// 函数: FStructWrapper::Init
// 位置: class wrapper 把 interface method 并入 prototype
// ============================================================================
for (const FImplementedInterface& Interface : Class->Interfaces)
{
	if (Interface.Class)
	{
		for (TFieldIterator<UFunction> ItfFuncIt(Interface.Class, EFieldIteratorFlags::ExcludeSuper); ItfFuncIt; ++ItfFuncIt)
		{
			UFunction* ItfFunction = *ItfFuncIt;
			if (!ItfFunction->HasAnyFunctionFlags(FUNC_Static) && !AddedMethods.Contains(ItfFunction->GetFName()))
			{
				auto ItfFunctionTranslator = GetMethodTranslator(ItfFunction, false);
				AddedMethods.Add(ItfFunction->GetFName());
				Result->PrototypeTemplate()->Set(FV8Utils::InternalString(Isolate, ItfFunction->GetName()),
					ItfFunctionTranslator->ToFunctionTemplate(Isolate)); // ★ interface method 走正向 wrapper path
			}
		}
	}
}
```

[4] UnLua 把“能否调用 interface function”做成真实 issue regression，而不是只测 query：

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Tests/Issue595TestInterface.h
// 位置: interface fixture 定义
// ============================================================================
UINTERFACE(Blueprintable)
class UNLUATESTSUITE_API UIssue595Interface : public UInterface
{
    GENERATED_BODY()
};

class UNLUATESTSUITE_API IIssue595Interface
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
    int32 Test() const; // ★ regression fixture 明确把 interface method 暴露为 callable
}

// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Tests/Issue595Test.cpp
// 位置: 真实调用 interface function 的回归
// ============================================================================
const auto Chunk = R"(
    local Obj = NewObject(UE.UIssue595Object)
    UE.UIssue595Interface.Test(Obj)
    return Obj:Test()
)";
UnLua::RunChunk(L, Chunk); // ★ 不是只测 ImplementsInterface，而是直接调用 interface function
```

**吸收建议**

- 先不要等完整 `FInterfaceProperty` 全量落地后再处理 D3。第一阶段就可以把 `RejectedInterfaceClass` 从 blanket rule 收缩成“按参数/返回值 family 判定”：只有 `CustomThunk`、未支持 family、参数过多等真实技术约束才拒绝。
- interface callable 首版可以继续沿用现有 `UObject* + UFunction` 调用链，不要求修改引擎，也不要求升级 AngelScript runtime；关键是把 interface owner 从“类别性拒绝”改成“签名可表达则准入”。
- 测试上复用现有 `AngelscriptTest` 模块即可，不要另起新测试框架。新增一个 native `UINTERFACE(BlueprintNativeEvent, BlueprintCallable)` fixture 和一个脚本正向调用回归，参考 UnLua `Issue595` 的粒度即可。

**优先级**

- `P0`
- 理由：`P10 UInterface` 的结束条件不能只是“能生成壳、能 cast、能 `ImplementsInterface`”，还必须包括“interface function 可被脚本正向调用”。否则 Bind API GAP 只会从 value bridge 转移到 callable bridge。

### D4 热重载补充：impact 事实已经存在，但 owner 被拆成 file list / module list / reload map 三套

**当前状态**

- `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp:55-86` 负责把文件变化排入 `FileChangesDetectedForReload`。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2481-2496,3914-3999,4130-4186` 负责给 hot-reload test runner 提供 `RelativeFileList`、给 delegate 广播 `CompiledModules`，并产出 `ReloadRequirement / ECompileResult`。
- `Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp:112-145,150-245,278-304` 已经能从 `CompiledModules` 构建 `Symbols` 并按 reason 扫描受影响 Blueprint。
- 但 `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:83-112` 又会从 `ReloadClasses / ReloadStructs / ReloadEnums / ReloadDelegates` 手工重建一份 `ImpactSymbols`，再自己遍历全部已加载 Blueprint。

```text
[Current Reload Impact Owners]
DirectoryWatcher
  -> FileChangesDetectedForReload
Runtime Compile
  -> CompiledModules / RelativeFileList / ReloadRequirement
Impact Scanner
  -> BuildImpactSymbols(Modules) -> AnalyzeLoadedBlueprint()
ClassReloadHelper
  -> ReloadClasses/Structs/... -> ad-hoc ImpactSymbols -> PerformReinstance()
```

[1] 当前项目已经有“按 module 构建 impact symbol”的扫描器，但 reinstance helper 仍在手工重建同类事实：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp
// 函数: BuildImpactSymbols / AnalyzeLoadedBlueprint
// 位置: editor 侧按 compiled modules 计算影响范围
// ============================================================================
FBlueprintImpactSymbols BuildImpactSymbols(const TArray<TSharedRef<FAngelscriptModuleDesc>>& Modules)
{
	FBlueprintImpactSymbols Symbols;
	for (const TSharedRef<FAngelscriptModuleDesc>& Module : Modules)
	{
		for (const TSharedRef<FAngelscriptClassDesc>& ClassDesc : Module->Classes)
		{
			if (ClassDesc->Class != nullptr)
				Symbols.Classes.Add(ClassDesc->Class);
			if (UScriptStruct* ScriptStruct = Cast<UScriptStruct>(ClassDesc->Struct))
				Symbols.Structs.Add(ScriptStruct);
		}
		...
	}
	return Symbols; // ★ scanner 已经能从 modules 直接推出 symbol set
}

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp
// 函数: FClassReloadHelper::FReloadState::PerformReinstance
// 位置: reload helper 手工重建 impact symbols
// ============================================================================
AngelscriptEditor::BlueprintImpact::FBlueprintImpactSymbols ImpactSymbols;
for (const auto& ReloadClass : ReloadClasses)
{
	ImpactSymbols.Classes.Add(ReloadClass.Key);
	ImpactSymbols.ReplacementObjects.Add(ReloadClass.Key, ReloadClass.Value);
}
for (const auto& ReloadStruct : ReloadStructs)
{
	ImpactSymbols.Structs.Add(ReloadStruct.Key);
	ImpactSymbols.ReplacementObjects.Add(ReloadStruct.Key, ReloadStruct.Value);
}
...
const bool bHasDependency = AngelscriptEditor::BlueprintImpact::AnalyzeLoadedBlueprint(*BP, ImpactSymbols, ImpactReasons);
// ★ helper 没有消费 scanner 产出的 module-based symbol set，而是再拼一遍 impact truth
```

[2] hot reload runtime 侧已经握有 `CompiledModules` 和 `RelativeFileList`，但 learning/test 目前主要只观察 reload decision：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: PerformHotReload / CompileModules
// 位置: runtime hot reload 对外广播的事实
// ============================================================================
HotReloadTestRunner->PrepareTests(GetActiveModules(), CompiledModules, RelativeFileList, ShouldUseAutomaticImportMethod());

FAngelscriptPostCompileClassCollection& PostCompileDelegate = FAngelscriptRuntimeModule::GetPostCompileClassCollection();
if (PostCompileDelegate.IsBound())
	PostCompileDelegate.Broadcast(CompiledModules); // ★ runtime 已能广播 compiled modules

switch (ReloadReq)
{
	case FAngelscriptClassGenerator::EReloadRequirement::SoftReload:
	case FAngelscriptClassGenerator::EReloadRequirement::FullReloadSuggested:
	case FAngelscriptClassGenerator::EReloadRequirement::FullReloadRequired:
		... // ★ 目前主合同仍聚焦 ReloadRequirement / ECompileResult
}
```

**差距描述**

- **没有实现**：不是。当前仓库已经有 watcher、reload helper、impact scanner 和 reload decision trace，不能误判成“没有 impact 基础设施”。
- **实现方式不同**：同一个“reload impact”事实被分散建模成三套 owner。file list、compiled modules、reload maps 各自成立，但没有一份稳定 manifest 供 test/rebind/editor 共用。
- **实现质量差异**：`BuildImpactSymbols()` 已能产出结构化 reason，但这些 reason 没有进入 hot reload 的主诊断/测试合同。`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningHotReloadDecisionTraceTests.cpp:54-84` 当前记录的是 `ReloadRequirement / WantsFullReload / NeedsFullReload`，不是“哪些 Blueprint/哪些 interface consumer 会被影响”。

**参考方案**

- **puerts**：`Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js:87-90` 先发 `HMR.prepare / HMR.finish` 生命周期边界，`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/TypeScriptGeneratedClass.cpp:77-99` 再由 runtime class 消费 `NotifyReBind()`。
- **UnLua**：`Reference/UnLua/Plugins/UnLua/Content/Script/UnLua/HotReload.lua:381-412,475-476` 会显式修补 running stack，而不是假设“compile decision 正确”就等于“reload 已闭环”。

[3] 参考实现把 reload 生命周期和 consumer 明确拆开：

```js
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Content/JavaScript/puerts/hot_reload.js
// 位置: reload 生命周期边界
// ============================================================================
let m = puerts.getModuleByUrl(url);
puerts.emit('HMR.prepare', moduleName, m, url);
let res = await sendCommand("Debugger.setScriptSource", {scriptId:"" + scriptId,scriptSource:source});
puerts.emit('HMR.finish', moduleName, m, url);
// ★ prepare/finish 是共享边界，rebind consumer 可以独立订阅
```

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/TypeScriptGeneratedClass.cpp
// 函数: UTypeScriptGeneratedClass::NotifyRebind
// 位置: reload 完成后的显式 consumer
// ============================================================================
if (TsClass->NeedReBind && TsClass->DynamicInvoker.IsValid())
{
	TsClass->NeedReBind = false;
	CachedClass->DynamicInvoker.Pin()->NotifyReBind(CachedClass); // ★ reload impact 由显式 consumer 消费
}
```

**吸收建议**

- 在插件层新增一份轻量 `FReloadImpactManifest`，首版只要求聚合 `ChangedScripts`、`MatchingModules`、`Symbols`、`ImpactedBlueprints` 与 `Reasons`，不要求修改引擎。
- manifest 的 producer 应放在当前 runtime 已经拥有 `CompiledModules` 的节点上，然后通过 `GetPostCompileClassCollection()` 或新的 multicast delegate 广播；`ClassReloadHelper`、`HotReloadTestRunner`、未来 rebind observer 都消费这一份事实，而不是各自重建。
- `BuildImpactSymbols()` / `AnalyzeLoadedBlueprint()` 已有可复用实现，且 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactScannerTests.cpp:297-309,485-490` 已给出基础 guard；优先做 owner 收敛，不要再新写第四套扫描器。

**优先级**

- `P1`
- 理由：它不是 `P10 UInterface` 的直接 blocker，但会决定 interface/Bind API GAP 补洞之后能否在 editor hot reload 中稳定落地。先收敛 impact owner，后续 rebind/diagnostics 的扩展面会小很多。

### D9 测试基础设施补充：`P10` 仍缺少“同一个 fixture 同时验证 artifact + callable + reload”的回归

**当前状态**

- `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp:594-667,675-748` 当前主要校验 summary/csv 的列头、总量和聚合关系。
- `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceImplementTests.cpp:175-247` 当前主要校验 interface class 生成成功以及 `ImplementsInterface()` happy path。
- `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptBlueprintCallableReflectiveFallbackTests.cpp:228-250` 当前还把 interface callable rejection 写成了正确行为。

```text
[Current D9 Coverage Split]
UHT artifact tests
  -> summary/csv header/count
runtime interface tests
  -> generated class exists / ImplementsInterface == true
reflective fallback tests
  -> interface rejection is expected

missing joined regression
  -> interface symbol 是否进生成物？
  -> interface callable 是否成功？
  -> hot reload 后 dispatch 是否仍正确？
```

[1] 当前 artifact 测试主要验证“文件格式正确、数量对齐”，不是“interface gap 被闭环验证”：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp
// 函数: FAngelscriptGeneratedFunctionTableCsvOutputTest / SkippedReasonSummaryCsvOutputTest
// 位置: UHT artifact 测试口径
// ============================================================================
TestEqual(TEXT("Generated function table csv test should keep one entry csv row per generated binding entry"),
	EntryLines.Num() - 1, TotalGeneratedEntries);
TestEqual(TEXT("Generated function table csv test should write the expected entry csv header"),
	EntryLines[0], TEXT("ModuleName,EditorOnly,ClassName,FunctionName,EntryKind,EraseMacro,ShardIndex"));

TestEqual(TEXT("Generated function table skipped reason summary test should write the expected summary csv header"),
	SummaryLines[0], TEXT("FailureReason,SkippedCount"));
TestEqual(TEXT("Generated function table skipped reason summary test should keep aggregate counts aligned with the skipped entry csv"),
	SummedSkippedCount, SkippedLines.Num() - 1);
// ★ 这些断言能证明 artifact 存在，但不能证明 interface symbol 已经可调用
```

[2] 当前 runtime interface 测试和 fallback 测试分别守住 happy-path query 与 negative gate：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceImplementTests.cpp
// 函数: FAngelscriptScenarioInterfaceImplementsInterfaceMethodTest::RunTest
// 位置: runtime interface happy path
// ============================================================================
if (this.ImplementsInterface(UIDamageableImplCheck::StaticClass()))
{
	ImplementsResult = 1;
}
...
TestEqual(TEXT("ImplementsInterface via StaticClass() should succeed in AS script"), ImplementsResult, 1);
// ★ 这里只证明 query/happy-path 成立

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptBlueprintCallableReflectiveFallbackTests.cpp
// 函数: FAngelscriptBlueprintCallableReflectiveFallbackEligibilityTest::RunTest
// 位置: negative gate baseline
// ============================================================================
TestEqual(
	TEXT("Reflective fallback should reject interface-class functions explicitly"),
	EvaluateReflectiveFallbackEligibility(InterfaceFunction),
	EAngelscriptReflectiveFallbackEligibility::RejectedInterfaceClass);
// ★ negative gate 已被锁住，但没有正向 callable regression
```

**差距描述**

- **实现质量差异**：测试是“按子系统分层正确”，不是“按用户能力闭环正确”。artifact、runtime、reload 三层各有测试，但同一个 interface gap 没有一条 joined regression 把它们串起来。
- **能力缺失**：缺少 issue-keyed、端到端的 `UInterface` regression。当前没有任何一个 fixture 能同时回答“UHT 生成物是否记录了这个 interface symbol”“脚本是否真的调用成功”“reload 后 dispatch 是否仍然正确”。
- **实现方式不同**：参考实现更倾向于“一条回归锁一个真实 bug/能力点”，而当前测试更偏 subsystem-local invariant，这对 `P10` 这种跨 `D2 / D6 / D4` 的主线不够用。

**参考方案**

- **UnLuaTestSuite**：`Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Tests/Issue595Test.cpp:33-47` 直接把“Lua 无法调用 Interface 中函数”写成单独 regression；`.../BindingTest.cpp:51-71` 再用 world-backed binding test 验证脚本绑定真实可运行。

[3] 参考实现把“一个能力缺口 = 一条端到端回归”做成显式模式：

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Tests/Issue595Test.cpp
// 位置: interface callable regression
// ============================================================================
const auto Chunk = R"(
    local Obj = NewObject(UE.UIssue595Object)
    UE.UIssue595Interface.Test(Obj)
    return Obj:Test()
)";
UnLua::RunChunk(L, Chunk);
RUNNER_TEST_EQUAL(1, Actual); // ★ 回归直接验证 interface callable 的用户结果

// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Tests/BindingTest.cpp
// 位置: world-backed binding test
// ============================================================================
const char* Chunk1 = R"(
    local ActorClass = UE.UClass.Load('/UnLuaTestSuite/Tests/Binding/BP_UnLuaTestActor_StaticBinding.BP_UnLuaTestActor_StaticBinding_C')
    G_Actor = World:SpawnActor(ActorClass)
)";
UnLua::RunChunk(L, Chunk1);
World->Tick(LEVELTICK_All, SMALL_NUMBER);
UnLua::RunChunk(L, "return G_Actor:RunTest()");
// ★ 不只看 artifact 或 helper，而是让绑定真正跑一遍
```

**吸收建议**

- 不要新建一套测试框架，直接复用现有 `AngelscriptTest` 模块和 helper，在其中新增一个 joined `InterfaceGapRegression` fixture 即可。
- 这条 fixture 的首版最少要覆盖四步：`[1]` 编译带 native `UINTERFACE(BlueprintCallable)` 的 fixture；`[2]` 读取 UHT artifact，确认目标 symbol 的 outcome；`[3]` 从脚本正向调用 interface function；`[4]` 对同一 fixture 做 body-only 与 signature-change 两类 hot reload，确认 dispatch 与 outcome 一起变化。
- 如果 `D6` 的 `BindingCoverageId` 还没落地，首版可以先用 `ModuleName + ClassName + FunctionName` 组合作为 join key；等 `BindingCoverageId` 成熟后再无缝接入，不阻塞当前主线。

**优先级**

- `P0`
- 理由：`P10 UInterface` 与 Bind API GAP 都要求“能被机器稳定证明已经补齐”。如果没有 joined regression，后续即使修好一层，也很容易在 UHT/runtime/reload 另一层静默回退。

### 值得吸收的设计模式（补充）

- `Issue-driven end-to-end regression`：UnLua `Issue595` 的粒度非常适合当前 `P10`。一个缺口对应一条真实可执行回归，比散落在 artifact/runtime 子系统里的浅断言更适合 Bind API GAP 收口。
- `Prepare/Finish + Rebind consumer`：puerts 的 `HMR.prepare / HMR.finish` 加 `NotifyReBind()` 说明 reload 生命周期和 consumer 应该分层，这正适合当前仓库把 `ImpactScanner`、`ClassReloadHelper`、`HotReloadTestRunner` 串成同一条 observer 链。
- `Single impact fact, multiple consumers`：当前仓库已经有 `BuildImpactSymbols()` 与 `AnalyzeLoadedBlueprint()`，下一步最值得吸收的不是新扫描器，而是给现有 scanner 一个共享 manifest owner。

### 改进路线建议（补充）

1. `P0`：先在 `AngelscriptTest` 内补 `InterfaceGapRegression` 夹具，连通 UHT artifact、runtime callable、hot reload 三段验证；这一步应与 `P10 UInterface` 同步推进。
2. `P0`：在 callable 层放开 interface 的正向准入，不再把 `CLASS_Interface` 当成 blanket reject；仅保留 `CustomThunk`、unsupported family、参数上限等真正的技术性拒绝。
3. `P1`：把 `BuildImpactSymbols()`、`ClassReloadHelper` 和 hot-reload test runner 收敛成统一 `ReloadImpactManifest`，让 editor reinstance、rebind、trace/test 消费同一份 impact 真相。
4. `P1`：等 `D6` 的 `BindingCoverageId` 落稳后，再把它接进 joined regression 和 reload manifest，形成 `Bind API GAP -> 可调用能力 -> reload 安全` 的单一量化闭环。

---

## 深化分析 (2026-04-08 18:48:15)

### 差距矩阵增补

| 维度 | 聚焦子问题 | 总评等级 | 主要差距类型 | 优先级 |
| --- | --- | --- | --- | --- |
| D1 插件架构与模块划分 | bind provider ownership / authority | 实现差异 | bind 已可扩展，但 provider owner 仍是 `process-global static + BindModules.Cache` 的隐式结果 | P1 |
| D5 调试与开发体验 | debug session / workspace / symbol contract | 实现差异 | `DebugServer V2` 已完整可用，但协议面把 debug、workspace、editor 写操作混成一条 lane，且缺 capability / revision / symbol contract | P1 |
| D11 部署与打包 | staged artifact / version manifest | 实现差异 | `Binds.Cache`、JIT、UHT sidecar 各自成立，但缺统一 staging/version manifest，构建与部署边界仍是隐式文件拼盘 | P1 |

### D1 插件架构与模块划分补充：bind provider 已可扩展，但 owner 仍是隐式静态初始化结果

**当前状态**

- `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:23-34` 把 bind state 绑定到“当前 engine，否则回落 legacy static state”。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:132-154,176-183` 把所有 binder 收进同一个 `static TArray<FBindFunction>`，`GetBindInfoList()` 也只返回 `BindName / BindOrder / bEnabled`。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:438-475` 说明对外公开扩展面其实已经存在，外部模块理论上可以直接 `FBind(...)`。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1473-1495,1915-1922` 运行时先读 `BindModules.Cache`、动态加载生成模块，再统一 `CallBinds()`。
- `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:999-1077,1285-1328` editor 会按 class shard 生成 `ASRuntimeBind_* / ASEditorBind_*` 模块并回写 `BindModules.Cache`，但这些模块本体仍只是 `RegisterBinds()` 包装。

```text
[Current D1 Bind Ownership]
static FBind in Bind_*.cpp
├─ process-global BindArray                        // 注册目录是全局静态数组
├─ editor GenerateNativeBinds()
│  └─ writes BindModules.Cache                     // editor 负责把分片模块名落盘
└─ runtime BindScriptTypes()
   ├─ load modules from cache
   └─ CallBinds() against current-engine state     // 执行状态是 engine-scoped
```

[1] 当前仓库的 provider owner 仍是“静态初始化 + 全局数组”：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp
// 函数: GetBindState / GetBindArray / RegisterBinds
// 位置: bind 注册目录与执行状态的 owner
// ============================================================================
static FAngelscriptBindState& GetBindState()
{
	if (FAngelscriptEngine* Engine = FAngelscriptEngine::TryGetCurrentEngine())
	{
		if (FAngelscriptBindState* State = Engine->GetBindState())
		{
			return *State; // ★ 执行态跟随当前 engine
		}
	}
	static FAngelscriptBindState LegacyBindState;
	return LegacyBindState; // ★ 仍保留进程级 fallback
}

static TArray<FBindFunction>& GetBindArray()
{
	static TArray<FBindFunction> BindArray;
	return BindArray; // ★ 注册目录本身是全局静态数组
}

void FAngelscriptBinds::RegisterBinds(FName BindName, int32 BindOrder, TFunction<void()> Function)
{
	GetBindArray().Add({BindName.IsNone() ? MakeUnnamedBindName() : BindName, BindOrder, MoveTemp(Function)});
	// ★ 只有 bind name / order，没有 provider/module/source owner
}
```

[2] 参考实现把“生成 owner”与“执行 owner”拆成显式阶段：

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp
// 位置: generator stage 的显式 owner
// ============================================================================
FGeneratorCore::BeginGenerator();
FClassGenerator::Generator();
FStructGenerator::Generator();
FEnumGenerator::Generator();
FAssetGenerator::Generator();
FBindingClassGenerator::Generator();
FBindingEnumGenerator::Generator();
FGeneratorCore::EndGenerator();
FCSharpCompiler::Get().ImmediatelyCompile();
// ★ 先有显式生成阶段，再进入 runtime 消费阶段

// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FCSharpBind.cpp
// 函数: FCSharpBind::BindImplementation
// 位置: runtime 只消费生成物并回填 hash/descriptor
// ============================================================================
const auto NewClassDescriptor = FCSharpEnvironment::GetEnvironment().AddClassDescriptor(InStruct);
...
FCSharpEnvironment::GetEnvironment().AddPropertyHash(FieldHash, NewClassDescriptor, Property);
...
FCSharpEnvironment::GetEnvironment().AddFunctionHash<FUnrealFunctionDescriptor>(
	FieldHash, NewClassDescriptor, Function);
// ★ provider owner 已在生成阶段固定，runtime 只做回填和索引
```

**差距描述**

- **能力缺失**：不是。当前仓库已经支持手写 `FBind`、editor 生成 bind module、运行时动态加载这些 module，不能判成“没有 provider 能力”。
- **实现方式不同**：当前 provider owner 由静态初始化和 `BindModules.Cache` 间接表达；参考方案更强调 `generator/env registry -> runtime consumer` 的显式 owner 边界。
- **实现质量差异**：当前 `GetBindInfoList()` 无法回答“这个 bind 来自哪个 module、哪条生成链、哪种 authority”。这会直接抬高 `Bind API GAP` 的量化成本，因为新增 family 时只能靠 grep `Bind_*.cpp`、`AS_FunctionTable_*` 和 `Binds.Cache` 交叉猜测。

**参考方案**

- `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:266-305` 的 `Generator()` 总控，把 class/struct/enum/binding 生成统一串在一条显式 stage。
- `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FCSharpBind.cpp:103-223` 把 runtime bind 限定为对 descriptor/hash 的回填，而不是让注册 owner 混在静态初始化里。
- `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:102-113,144-159` 与 `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:40-45,108-130,225-233` 把 registry 生命周期绑定到 `FLuaEnv`，使“这一轮 VM 到底注册了什么”可以被显式追踪。

**吸收建议**

- 第一阶段只做 `report-only` 的 `BindingProviderManifest`，字段至少包含 `ProviderName`、`ModuleName`、`SourceKind(BindFile/Generated/UHT/Reflective)`、`Families`、`OwnerSymbols`。先让 owner 可见，不急着改掉现有 `FBind` 写法。
- 现有 `FBind` / `BindModules.Cache` 保留兼容；但 runtime 在 `BindScriptTypes()` 前应先构造一份 resolved provider plan，再执行 `CallBinds()`。这样 `Bind API GAP`、测试、state dump 才能共享同一份 provider 真相。
- 不建议现在就让 UHT 直接生成 `FAngelscriptType` 或完全替代手写 binder。当前最缺的是 owner，可见性先于自动化比例。

**优先级**

- `P1`
- 理由：它不是 `P10 UInterface` 第一刀 runtime bridge 的 blocker，但在 `P10` 完成最小闭环后，应立即落地。否则 interface 之外的新 family 继续扩张时，只会再长出第四套、第五套 authority owner。

### D5 调试与开发体验补充：`DebugServer V2` 已经够强，缺的是 debug/workspace/symbol 合同边界

**当前状态**

- `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h:25-80` 同一套 `EDebugMessageType` 里同时放了 `CallStack/Variables/Evaluate`、`AssetDatabase/FindAssets`、`CreateBlueprint/ReplaceAssetDefinition`。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h:433-437,581-692` 说明协议版本和变量扩展字段仍是进程级/服务级概念，不是 session 级 capability。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:822-826,1493-1505,2049-2203` 中，`RequestDebugDatabase` 会立刻发送 `DebugDatabaseSettings`、完整 `DebugDatabase`、diagnostics，并继续补发整份 `AssetDatabase`。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:1161-1190` 直接在调试协议里转发 `FindAssets` 和 `CreateBlueprint`。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2833-2835` 表明整个 debug server 仍跑在 engine tick 主路径上。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp:675-755` 只会 dump `.hpp` 文档；而 `SendDebugDatabase()` 再临时从 live `FAngelscriptDocs` / `UFunction*` 拼 JSON，说明“离线 docs”与“在线 symbol metadata”仍是两份真相。

```text
[Current D5 Protocol Surface]
socket V2
├─ debug session
│  ├─ break / step / callstack / variables
│  └─ DebugDatabaseSettings
├─ workspace browse
│  ├─ AssetDatabase
│  ├─ FindAssets
│  └─ GoToDefinition
└─ editor write actions
   ├─ CreateBlueprint
   └─ ReplaceAssetDefinition
```

[1] 当前协议把 debug、workspace 和 editor 写操作压在同一 message surface：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h
// 位置: 25-80，单一消息枚举同时覆盖 debug / workspace / editor write
// ============================================================================
enum class EDebugMessageType : uint8
{
	RequestCallStack,
	CallStack,
	RequestVariables,
	Variables,
	RequestEvaluate,
	Evaluate,
	GoToDefinition,
	...
	AssetDatabaseInit,
	AssetDatabase,
	AssetDatabaseFinished,
	FindAssets,
	DebugDatabaseSettings,
	...
	CreateBlueprint,
	ReplaceAssetDefinition,
};
// ★ 同一协议面同时承载调试、浏览和 editor 写动作

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 位置: 822-826, 1161-1180，RequestDebugDatabase 与 workspace/editor 行为混在一起
// ============================================================================
if (MessageType == EDebugMessageType::RequestDebugDatabase)
{
	ClientsThatWantDebugDatabase.Add(Client);
	SendDebugDatabase(Client);
	FAngelscriptEngine::Get().EmitDiagnostics(Client); // ★ 请求调试数据库时顺带推送诊断
}
...
else if (MessageType == EDebugMessageType::FindAssets)
{
	FAngelscriptRuntimeModule::GetDebugListAssets().Broadcast(AssetList.Assets, BaseClass);
}
else if (MessageType == EDebugMessageType::CreateBlueprint)
{
	FAngelscriptRuntimeModule::GetEditorCreateBlueprint().Broadcast(Cast<UASClass>(ClassDesc->Class));
	// ★ editor 写操作同样走这条调试 socket
}
```

[2] 参考实现把“可观测原语”和“前端 transport”拆开：

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.h
// 位置: 46-73，transport/channel 抽象
// ============================================================================
class V8InspectorChannel
{
public:
	virtual void DispatchProtocolMessage(const std::string& Message) = 0;
	virtual void OnMessage(std::function<void(const std::string&)> Handler) = 0;
};

class V8Inspector
{
public:
	virtual void Close() = 0;
	virtual bool Tick() = 0;
	virtual V8InspectorChannel* CreateV8InspectorChannel() = 0;
};
// ★ 调试语义先抽成 channel/session，再决定 websocket 怎么承载

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp
// 位置: 330-345, 459-477，先暴露 discovery contract，再进 websocket 会话
// ============================================================================
JSONVersion = R"({
    "Browser": "Puerts/v1.0.0",
    "Protocol-Version": "1.1"
})";

JSONList = R"([
	{
		"description": "Puerts Inspector",
		"id": "0",
		"title": "Puerts Inspector",
		"type": "node",
		"webSocketDebuggerUrl": "ws://127.0.0.1:..."
	}
])";

if (Resource == "/json" || Resource == "/json/list")
{
	Connection->set_body(JSONList);
}
else if (Resource == "/json/version")
{
	Connection->set_body(JSONVersion);
}
// ★ 先 discovery，再进入消息会话
```

**差距描述**

- **能力缺失**：不是。当前仓库已经有断点、单步、变量、调用栈、`GoToDefinition`、资产数据库和 editor action，D5 不能误判成“缺 debugger”。
- **实现方式不同**：当前实现是 `UE-owned protocol`，而且把 workspace/editor 行为一起塞进同一 socket lane；参考实现更倾向于“transport/session 抽象”和“runtime debug primitive 独立于 IDE”。
- **实现质量差异**：当前没有显式 capability / revision / symbol identity 协议。结果是前端无法先知道“这次连接拿到的是哪一版语言模式、哪一版 artifact、哪一版 asset snapshot”，`Bind API GAP` 相关证据也更难在 remote/headless/CI 路径稳定复用。

**参考方案**

- `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.h:46-73` 与 `.../V8InspectorImpl.cpp:60-117,315-345,452-589`：先定义 channel/session，再暴露 `/json/list`、`/json/version` discovery contract。
- `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDebugBase.h:75-94` 与 `.../Private/UnLuaDebugBase.cpp:613-732`：先把 `GetStackVariables()`、`GetLuaCallStack()` 做成 runtime debug primitive，而不是把 IDE/workspace 行为直接并进同一个 transport。

**吸收建议**

- 保留现有 `DebugServer V2` envelope，不推倒重来；但内部应拆出 `DebugSessionService` 与 `WorkspaceService` 两个逻辑域，并给旧 client 维持兼容消息号。
- 在 `RequestDebugDatabase` 前增加轻量 discovery/capability/revision 握手，至少声明 `supportsWorkspaceWrite`、`supportsAssetBrowse`、`symbolRevision`、`artifactRevision`。这一步不需要先上 DAP/CDP。
- 让后续 `BindingCoverageId` / `SymbolId` / `ArtifactManifest` 成为 `DebugDatabase` 的 join key，而不是继续只靠 live `UFunction*` 和进程内 doc cache 拼一份临时真相。

**优先级**

- `P1`
- 理由：它不是 `P10` 的首个 runtime blocker，但和 `D6` 的统一 contract 紧密耦合。若继续把 `Bind API GAP` 相关语义只保留在 live editor 会话里，后续 CI、remote game、headless commandlet 都很难稳定复用同一份诊断真相。

### D11 部署与打包补充：运行时缓存已经成形，但 staged contract 仍是隐式文件拼盘

**当前状态**

- `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs:29-88` 只声明模块依赖，没有看到 `RuntimeDependencies`、staged toolchain artifact 或 manifest 类声明。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp:42-137` 把 `Binds.Cache` 与 `.Headers` 作为 cook/runtime contract 真正读写出来，说明 D11 不是空白。
- `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp:3582-3695` 会产出按 module 切分的 `.as.jit.hpp`、unity `AngelscriptJitCode_*.jit.cpp`，最后再写一份 `AngelscriptJitInfo.jit.cpp`。
- `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp:19-35` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1550-1555` 说明 runtime 只认一份全局 `PrecompiledDataGuid`，不匹配就整包清空 JIT。
- `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:204-264,334-385,449-479` 与 `.../AngelscriptFunctionTableExporter.cs:99-160` 则说明 `AS_FunctionTable_*.json/.csv` sidecar 仍靠 `File.WriteAllText()` 和 `Build.cs` 文本解析，自身没有显式 staging/version manifest。

```text
[Current D11 Artifact Contract]
AngelscriptRuntime.Build.cs
└─ module dependencies only                        // 没有显式 artifact staging 声明

cook/runtime
├─ Binds.Cache + .Headers                          // bind/runtime contract
├─ PrecompiledData + single JitInfo GUID           // JIT contract
└─ GUID mismatch => clear whole JIT bundle

toolchain sidecars
├─ AS_FunctionTable_*.cpp / .json / .csv
├─ Docs/angelscript/generated/*.hpp
└─ no shared manifest / no staged revision owner
```

[1] 当前部署/构建合同是“缓存存在，但 family 分裂”：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp
// 函数: FAngelscriptBindDatabase::Save / Load
// 位置: bind contract 的真实落盘点
// ============================================================================
Serialize(Writer);
bool bSaveSuccess = FFileHelper::SaveArrayToFile(Data, *Path);
...
FFileHelper::SaveArrayToFile(HeaderData, *(Path + TEXT(".Headers")));
// ★ cook/runtime 真正依赖的是 Binds.Cache + .Headers

FFileHelper::LoadFileToArray(Data, *Path);
Serialize(Reader);
if (Classes.Num() == 0 && Structs.Num() == 0)
{
	UE_LOG(Angelscript, Fatal, TEXT("Unable to load script bind database..."));
}
// ★ 这已经是正式 runtime contract，不是临时调试产物

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs
// 位置: 204-264, 334-385，sidecar 仍靠裸写文件和 Build.cs 文本解析
// ============================================================================
File.WriteAllText(summaryPath, summaryJson, Encoding.UTF8);
...
File.WriteAllText(csvPath, builder.ToString(), Encoding.UTF8);
...
string buildCsPath = ResolveRuntimeBuildCsPath(factory);
foreach (string rawLine in File.ReadAllLines(buildCsPath))
{
	if (line.Contains("DependencyModuleNames.AddRange", StringComparison.Ordinal))
	{
		inDependencyBlock = true;
	}
	...
}
// ★ 说明 toolchain artifact 还没有进入显式 manifest/staging owner
```

[2] 参考实现把发布目录、运行时依赖或模块解析边界做成显式合同：

```csharp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs
// 位置: 360-384，显式 staged runtime dependencies
// ============================================================================
void AddRuntimeDependencies(string[] DllNames, string LibraryPath, bool Delay)
{
	foreach (var DllName in DllNames)
	{
		var DllPath = Path.Combine(LibraryPath, DllName);
		var DestDllPath = Path.Combine("$(BinaryOutputDir)", DllName);
		RuntimeDependencies.Add(DestDllPath, DllPath, StagedFileType.NonUFS);
	}
}
// ★ 哪些文件需要跟包，由 Build.cs 显式声明

// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp
// 位置: 995-1048，显式 publish root 与 assembly publish path
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
}
// ★ 发布目录和需要分发的程序集路径都可以被机器稳定发现
```

**差距描述**

- **能力缺失**：不是。当前仓库已经有 `Binds.Cache`、`.Headers`、precompiled data 和 JIT info，也有 UHT sidecar；D11 绝不是“没有部署合同”。
- **实现方式不同**：当前是“runtime cache 一套、toolchain sidecar 一套、docs dump 一套、JIT info 一套”的并行文件族；参考方案更强调显式 publish root、runtime dependency staging 和模块解析边界。
- **实现质量差异**：当前缺统一 artifact manifest 与 staged revision。结果是 UHT sidecar 生命周期、JIT bundle 有效性、docs dump、cook/runtime cache 之间没有可 join 的版本 owner；一旦出现 mismatch，只能靠局部规则或整包清空兜底。

**参考方案**

- `Reference/puerts/unreal/Puerts/Source/JsEnv/JsEnv.Build.cs:360-384`：`RuntimeDependencies.Add(..., StagedFileType.NonUFS)` 明确声明需要跟包的 VM 依赖。
- `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/DefaultJSModuleLoader.cpp:72-89`：module 解析边界是 `package.json / index.js / node_modules` 这类显式文件规则，不靠再解析另一份 Build 脚本。
- `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Setting/UnrealCSharpSetting.h:90-120`、`.../Private/Setting/UnrealCSharpSetting.cpp:9-22,52-55`、`.../Private/Common/FUnrealCSharpFunctionLibrary.cpp:995-1048`：发布目录和程序集输出路径被做成正式设置与工具函数。

**吸收建议**

- 第一阶段新增 `AngelscriptArtifactManifest.json`，只做 `report-only`：记录 `Binds.Cache`、`.Headers`、`AS_FunctionTable_*`、`Docs/*.hpp`、JIT bundle、`PrecompiledDataGuid`、module list、artifact revision。
- `AngelscriptUHTTool` 不应继续靠解析 `AngelscriptRuntime.Build.cs` 文本推导支持模块。更稳妥的路径是让 build step 先产出支持模块 manifest，再由 UHT tool 消费。
- 继续保留当前 runtime cache 和 JIT GUID 兜底；但 CI、cook、IDE、debugger 至少要能读取同一份 artifact manifest，知道“这次构建到底更新了哪一层事实”。

**优先级**

- `P1`
- 理由：它不是 `P10` 第一阶段修 interface bridge 的 blocker，但它决定 `Bind API GAP` 后续能否跨 editor/cook/CI 稳定量化。越晚补这层 manifest，越容易把新修的 family 又分散到新的 sidecar 和缓存里。

### 值得吸收的设计模式（补充）

- `Explicit provider owner before more automation`：先把谁生成、谁注册、谁消费说清楚，再提高自动化比例。UnrealCSharp 的 generator/runtime 分层和 UnLua 的 env-owned registry 都在强调这一点。
- `Discovery before payload`：puerts 先给 `/json/list`、`/json/version`，再进入 inspector 会话。当前仓库最值得吸收的不是 websocket 本身，而是“先告诉前端我是谁、我支持什么、我是第几版”。
- `Runtime cache + toolchain manifest dual contract`：当前仓库已经有 runtime cache，不需要推翻；真正缺的是一份把 `Binds.Cache`、UHT sidecar、docs、JIT bundle 串起来的可发现 manifest。

### 改进路线建议（补充）

1. `P1`：在不改现有 `FBind` 写法的前提下，先落 `BindingProviderManifest`，把 `Bind_*.cpp`、generated bind module、UHT sidecar、reflective path 的 owner 统一列出来；这一步应紧跟 `P10` 的最小 runtime 闭环之后。
2. `P1`：给 `DebugServer V2` 增加 `capability + revision` 握手，并把 `DebugSessionService` / `WorkspaceService` 内部分层；先保兼容，不急着上标准协议。
3. `P1`：为 `Binds.Cache`、`AS_FunctionTable_*`、docs、JIT 增加统一 `AngelscriptArtifactManifest`，同时停止让 UHT tool 通过解析 `Build.cs` 文本推导模块边界。
4. `P2`：等 `D1/D5/D11` 的 owner 和 manifest 都稳定后，再评估 DAP/CDP adapter、更细粒度 JIT bundle invalidation、以及 editor 作者路径前移等更大改动。

---

## 深化分析 (2026-04-08 18:58:20)

> **本轮补读范围**: `Documents/AutoPlans/ReferenceComparison/Hazelight_Analysis.md`、`UnrealCSharp_Analysis.md`、`UnLua_Analysis.md`、`puerts_Analysis.md`、`sluaunreal_Analysis.md`、`CrossComparison.md`，以及 `DiscoveryPlans/*_Plan.md`、`ArchitectureReview/*_ArchReview.md` 的现有结论。
> **本轮策略**: 不重写前文已覆盖的 `D1/D2/D3/D4/D5/D6/D9/D11`，只补 `D7 / D8 / D10` 三个还未在本文件深入展开、且与当前主线优先级不会冲突的维度。

### 执行摘要（增量）

- `D7` 的结论不是“编辑器能力不足”。`AngelscriptEditor` 已经把 `DirectoryWatcher`、`Content Browser`、`StateDump`、`CreateBlueprint` 和 commandlet 接进工作流；真正的差距是这些入口仍是多套 driver，各自直接驱动逻辑，没有共用一个 `producer/service contract`。
- `D8` 的结论不是“没有性能优化”。`PrecompiledData + StaticJIT + StateDump + performance tests` 已经形成厚实栈；真正的短板是优化决策与性能回归仍缺 `FunctionId / CoverageId / AssumptionId` 这一层可归因合同，导致能测量、难解释。
- `D10` 的结论不是“没有文档和示例”。`.hpp` API dump、`TESTING_GUIDE`、`Examples/*.cpp`、`Coverage/*.as` 都存在；真正缺的是面向用户的 `Learn -> Scaffold -> Run -> IDE` 闭环。当前示例主要服务测试与维护，而不是直接服务脚本作者。

### 差距矩阵（增量）

| 维度 | 当前主路径 | 总评等级 | 主要差距类型 | 优先级 |
| --- | --- | --- | --- | --- |
| D7 编辑器集成 | `AngelscriptEditorModule + *Commandlet` | 实现差异 | 交互式菜单、资产工作流、headless commandlet 各自成立，但没有统一 producer/service contract | P2 |
| D8 性能与优化 | `PrecompiledData + StaticJIT + StateDump + performance tests` | 实现质量差异 | 已能优化、已能测量，但缺 `BindingCoverageId / DispatchAssumption / runtime sample` 这类可归因合同 | P2 |
| D10 文档与示例组织 | `Docs dump + test-embedded examples + testing guides` | 实现质量差异 | 参考手册和示例都在，但学习资产仍主要附着在测试/计划文件，不是 workspace-native 教学链 | P3 |

### D7 编辑器集成补充：接入面很深，但 `ToolMenus` / `commandlet` 还不是同一个 producer

**当前状态**

- `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:363-415` 显示 `StartupModule()` 已直接注册 `StateDump`、脚本目录监听、项目设置页、debug server 回调桥和 `CreateBlueprint` 弹窗。
- 同文件 `:722-730` 把 “Legacy Native Bind Generator (Debug Only)” 直接挂进菜单动作；`:999-1077` 里的 `GenerateNativeBinds()` 会即时生成 bind module 并写 `BindModules.Cache`。
- headless 路径也不是空白：`Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp:55-120`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAllScriptRootsCommandlet.cpp:5-21`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptTestCommandlet.cpp:5-24` 分别承担影响扫描、脚本根发现和 unit test。
- `sluaunreal_Analysis.md:2186-2294` 已证明相较 slua 的 “两个工具壳”，当前 `Angelscript` 明显更深地把编辑器纳入脚本作者工作流，因此这里不能误判成“没有 D7 能力”。

```
[Angelscript] D7 Current Driver Split
Editor Startup
├─ DirectoryWatcher + Settings + StateDump         // StartupModule 直接接管编辑器生命周期
├─ Runtime bridge -> CreateBlueprint popup         // runtime 消息驱动资产工作流
├─ ToolMenus -> GenerateNativeBinds()              // 交互式菜单动作直连实现
└─ Commandlets
   ├─ BlueprintImpactScan                          // 直接扫资产并打印结果
   ├─ AllScriptRoots                               // 直接输出脚本根
   └─ Test                                         // 直接跑 unit tests

[Reference] Shared Producer
ToolMenus / Console / Commandlet
└─ single generator service
   ├─ build artifact
   ├─ save canonical files
   └─ IDE/editor consume same output
```

[1] 当前 editor startup、菜单动作和 legacy bind 生成仍是直接调用式接线：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 函数: FAngelscriptEditorModule::StartupModule / RegisterToolsMenuEntries /
//       GenerateNativeBinds
// 行号: 363-415, 722-730, 1003-1077
// 位置: 编辑器入口直接挂接 StateDump、资产工作流、菜单动作与 bind 生成
// ============================================================================
UScriptEditorMenuExtension::InitializeExtensions();
AngelscriptEditor::Private::RegisterStateDumpExtension(StateDumpExtensionHandle);

FAngelscriptRuntimeModule::GetEditorCreateBlueprint().AddLambda(
	[](UASClass* ScriptClass)
	{
		FAngelscriptEditorModule::ShowCreateBlueprintPopup(ScriptClass); // ★ runtime 回调直接驱动 editor 资产弹窗
	}
);

UToolMenus::RegisterStartupCallback(
	FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FAngelscriptEditorModule::RegisterToolsMenuEntries));

FToolUIActionChoice GenerateAction(FExecuteAction::CreateLambda([]() { GenerateNativeBinds(); }));
BindSection.AddMenuEntry(
	"ASGenerateBindings",
	NSLOCTEXT("Angelscript", "GenerateBind.Label", "Legacy Native Bind Generator (Debug Only)"),
	NSLOCTEXT("Angelscript", "GenerateBind.ToolTip",
		"Legacy editor-side generator retained only for debugging old FunctionCallers output. The UHT-based AngelscriptUHTTool pipeline is the primary path."));

GenerateBindDatabases();
FAngelscriptBinds::GetBindModuleNames().Empty();
// ...
FAngelscriptBinds::SaveBindModules(FString(FAngelscriptEngine::GetScriptRootDirectory() / "BindModules.Cache"));
// ★ 菜单动作直接驱动生成与落盘，没有中间 producer/service 层
```

[2] 当前 commandlet 也存在，但每条路径都自己组织输入输出合同：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp
// 函数: UAngelscriptBlueprintImpactScanCommandlet::Main
// 行号: 55-120
// 位置: headless 扫描路径直接调用 scanner 并把结果打印为 ad-hoc JSON/log
// ============================================================================
if (!FAngelscriptEngine::Get().bDidInitialCompileSucceed)
{
	UE_LOG(Angelscript, Error, TEXT("Blueprint impact commandlet requires a successfully initialized Angelscript engine."));
	return static_cast<int32>(EBlueprintImpactCommandletExitCode::EngineNotReady);
}

const AngelscriptEditor::BlueprintImpact::FBlueprintImpactScanResult ScanResult =
	AngelscriptEditor::BlueprintImpact::ScanBlueprintAssets(
		FAngelscriptEngine::Get(),
		AssetRegistryModule.Get(),
		Request);

UE_LOG(
	Angelscript,
	Display,
	TEXT("{ \"BlueprintImpact\": { \"FullScan\": %s, \"ChangedScripts\": %d, \"MatchingModules\": %d, ... } }"),
	Request.IsFullScan() ? TEXT("true") : TEXT("false"),
	ScanResult.NormalizedChangedScripts.Num(),
	ScanResult.MatchingModules.Num());
// ★ 命令行路径是有效的，但输出合同只服务这一条命令本身
```

**差距描述**

- `无能力缺失`：目录监听、资产工作流、状态导出、脚本根发现、测试 commandlet 当前都已经存在。
- `实现方式不同`：相对 `puerts` 的 “生成/分析优先”，当前 `Angelscript` 明显偏 “资产工作流/状态审计优先”；这本身不是问题。
- `实现质量差异`：`ToolMenus`、legacy bind 生成、`BlueprintImpact` commandlet、`AllScriptRoots` commandlet 彼此没有共享的 producer/service contract。结果是 UI、headless 和后续 IDE/CI 只能各自消费一套入口，而不是共享同一份 artifact 和 result schema。

**参考方案**

- `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:417-457,568-616,1110-1160,1640-1687`
- `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp:55-123`

[3] `puerts` 的菜单与 console command 共用同一 generator，并把结果写回统一产物族：

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp
// 函数: StartupModule / GenTypeScriptDeclaration / GatherExtensions
// 行号: 417-457, 1110-1160, 1646-1687
// 位置: ToolMenus 与 console command 共用 `GenUeDts()`，最终写回同一组 `.d.ts`
// ============================================================================
FGenDTSCommands::Register();
PluginCommands->MapAction(FGenDTSCommands::Get().PluginAction,
	FExecuteAction::CreateRaw(this, &FDeclarationGenerator::GenUeDtsCallback), FCanExecuteAction());

UToolMenus::RegisterStartupCallback(
	FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FDeclarationGenerator::RegisterMenus));

ConsoleCommand = MakeUnique<FAutoConsoleCommand>(
	TEXT("Puerts.Gen"), TEXT("Execute GenDTS action"),
	FConsoleCommandWithArgsDelegate::CreateLambda([this](const TArray<FString>& Args)
	{
		this->GenUeDts(GenFull, SearchPath); // ★ 菜单和控制台都回到同一个 producer
	}));

FFileHelper::SaveStringToFile(ToString(), *UEDeclarationFilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
FFileHelper::SaveStringToFile(ToString(), *BPDeclarationFilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

GenTemplateBindingFunction(Tmp, FunctionInfo, true);
TryToAddOverload(Outputs, FunctionInfo->Name, true, Tmp.Buffer);
// ★ 扩展函数、Blueprint 增量信息和最终声明文件都归到同一产物家族
```

**吸收建议**

- 在插件内新增轻量 `FAngelscriptEditorArtifactService` 或等价 façade，不改引擎，也不推翻现有 `AngelscriptEditorModule`。第一阶段只收口 `GenerateNativeBinds`、`BlueprintImpactScan`、`AllScriptRoots` 三类 producer。
- 让 `ToolMenus`、console/commandlet 和未来 IDE/CI 都调用同一个 service，统一返回 `Result + Diagnostics + ArtifactPaths` 结构；旧菜单文字和旧 commandlet 名称可以保留兼容。
- `CreateBlueprintPopup`、资产浏览器弹窗这类强交互功能先不做 headless 化；当前最先需要统一的是“产物型”或“扫描型”流程，而不是模态 UI。
- 这一步应与前文 `D1/D11` 的 manifest 工作配合，而不是抢在其前面自定义第二套 artifact family。

**优先级**

- `P2`
- 理由：它不阻塞当前 `P10 UInterface / Bind API GAP` 主线；只有在 `D6/D11` 的 type/artifact owner 稳定后，统一 driver 才不会把错误的 authority 再产品化一遍。

### D8 性能与优化补充：优化栈已经很厚，缺的是可归因性能合同

**当前状态**

- `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1433-1505` 表明生成预编译数据时会同时接入 `StaticJIT`、bind DB 和 bind module 加载，优化链路已经在 engine 启动主路径里。
- `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp:3415-3463` 表明 `StaticJIT` 会在生成期直接做 `devirtualize` 并把函数标成 `asTRAIT_FINAL`。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp:1038-1099` 目前只导出 `PrecompiledData.csv` 和 `StaticJITState.csv` 的汇总计数。
- `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEnginePerformanceTests.cpp:73-98` 与 `.../HotReload/AngelscriptHotReloadPerformanceTests.cpp:60-74` 会把 startup/hot reload 性能写成 `metrics.json`，但粒度仍停在 total/median。
- `DebugAndToolchain_ArchReview.md:914-949,1059-1095` 已明确指出 `BindingCoverageId` 与 `DispatchAssumptionManifest` 是当前工具链最缺的两层性能归因合同。

```
[Angelscript] D8 Current Perf Evidence Chain
Engine Init
├─ PrecompiledData + StaticJIT                     // 启动即接入优化链
├─ AnalyzeScriptFunction()                        // 生成期直接写 final / devirtualize 假设
├─ StateDump -> PrecompiledData.csv / StaticJITState.csv
└─ PerformanceTests -> metrics.json
   └─ total / median only                         // 缺少 FunctionId / CoverageId / AssumptionId

[Reference] Runtime Profiling
UnrealCSharp
├─ -trace=CSharp
└─ Method enter/leave callbacks

sluaunreal
├─ takeSample(call/ret/tick)
├─ WatchBegin / WatchEnd / CoroutineBegin / End
└─ socket streaming or local record
```

[4] 当前优化栈很强，但关键决策仍停在生成期副作用：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: FAngelscriptEngine::Initialize
// 行号: 1433-1447
// 位置: 生成预编译数据时同时接入 StaticJIT
// ============================================================================
if (bGeneratePrecompiledData)
	PrecompiledData = new FAngelscriptPrecompiledData(Engine);

if (bGeneratePrecompiledData)
{
	StaticJIT = new FAngelscriptStaticJIT();
	StaticJIT->PrecompiledData = PrecompiledData;
	Engine->SetJITCompiler(StaticJIT); // ★ 优化链直接在 engine 初始化里接通
}

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp
// 函数: DevirtualizeFunction / AnalyzeScriptFunction
// 行号: 3415-3463
// 位置: JIT 生成期直接做去虚化和 final trait 改写
// ============================================================================
if (bAllowDevirtualize && !FunctionsWithVirtualOverrides.Contains(VirtualFunction))
{
	return VirtualFunction; // ★ 生成期直接假设“可去虚化”
}

if (!FunctionsWithVirtualOverrides.Contains(ScriptFunction))
{
	ScriptFunction->traits.SetTrait(asTRAIT_FINAL, true); // ★ 直接改 live function trait
}
```

[5] 当前输出工件能告诉你“有多少”，但很难告诉你“为什么”：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp
// 函数: DumpPrecompiledData / DumpStaticJITState
// 行号: 1038-1099
// 位置: 现有 state dump 只导出聚合计数
// ============================================================================
Writer.AddHeader({ TEXT("DataGuid"), TEXT("ModuleCount"), TEXT("FunctionMappingCount"), TEXT("ClassesLoadedCount"), TEXT("TimingData") });
Writer.AddRow({
	Engine.PrecompiledData->DataGuid.ToString(EGuidFormats::DigitsWithHyphens),
	LexToString(Engine.PrecompiledData->Modules.Num()),
	LexToString(Engine.PrecompiledData->FunctionReferences.Num()),
	LexToString(Engine.PrecompiledData->ClassesLoadedFromPrecompiledData.Num()),
	FString()
});

Writer.AddHeader({ TEXT("JITFileCount"), TEXT("FunctionsToGenerateCount"), TEXT("SharedHeaderCount"), TEXT("ComputedOffsetsCount") });
Writer.AddRow({
	LexToString(JITFileCount),
	LexToString(FunctionsToGenerateCount),
	LexToString(SharedHeaderCount),
	LexToString(ComputedOffsetsCount)
});
// ★ 能看规模，看不到某个函数为何 direct / dynamic / final

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEnginePerformanceTests.cpp
// 函数: ValidateAndWriteStartupMetrics
// 行号: 73-98
// 位置: 性能测试工件同样只落 total/median
// ============================================================================
Metrics.Add({ TEXT("startup.total_seconds"), StartupTotals, ComputeMedian(StartupTotals) });
Metrics.Add({ TEXT("startup.bind_script_types_seconds"), BindTotals, ComputeMedian(BindTotals) });
Metrics.Add({ TEXT("startup.call_binds_seconds"), CallBindTotals, ComputeMedian(CallBindTotals) });
// ★ 已能做回归基线，但无法把回归指回具体 bind/JIT assumption
```

**差距描述**

- `无能力缺失`：`PrecompiledData`、`StaticJIT`、`StateDump`、性能回归工件和 `FCpuProfilerTraceScoped` 当前都已经存在。
- `实现方式不同`：相对 `sluaunreal` 的 runtime profiler 子系统、`UnrealCSharp` 的 `-trace=CSharp` 方法级 trace，当前仓库更偏向 “优化执行主路径 + 导出离线工件”。
- `实现质量差异`：现有工件没有稳定 join key。开发者能看到 `startup.total_seconds`、`FunctionsToGenerateCount`、`DataGuid`，却无法把某次回归直接指回 `哪个 UHT entry`、`哪个 native form`、`哪个 devirtualization assumption`。

**参考方案**

- `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoProfiler.cpp:7-25`
- `Reference/UnrealCSharp/Script/UE/Library/FFunctionImplementation.cs:7-102`
- `Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaProfiler.cpp:222-252`
- `Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/SluaProfilerDataManager.cpp:393-447`

[6] 参考实现共同点不是“都比当前更快”，而是“性能事实有稳定身份和可持续消费面”：

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoProfiler.cpp
// 函数: FMonoProfiler::Register
// 行号: 7-25
// 位置: opt-in runtime trace 直接挂 method enter/leave callback
// ============================================================================
if (Channels.ToLower().Contains(TEXT("CSharp")))
{
	if (ProfilerHandle = mono_profiler_create(nullptr); ProfilerHandle != nullptr)
	{
		mono_profiler_set_method_enter_callback(ProfilerHandle, Method_Enter);
		mono_profiler_set_method_leave_callback(ProfilerHandle, Method_Leave);
		mono_profiler_set_method_exception_leave_callback(ProfilerHandle, Method_Exception_Leave);
	}
}
// ★ 性能观测和 runtime 直接共享方法身份，而不是只输出总量

// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/LuaProfiler.cpp
// 函数: takeSample
// 行号: 222-252
// 位置: 统一采样入口可切换远程 streaming 或本地录制
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

// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Private/SluaProfilerDataManager.cpp
// 函数: ProcessCPUCommand
// 行号: 393-447
// 位置: call/return/tick 事件统一进入调用树处理
// ============================================================================
if (hookEvent == NS_SLUA::ProfilerHookEvent::PHE_CALL)
{
	SluaProfilerDataManager::WatchBegin(shortSrc, lineDefined, funcName, time, funcProfilerRoot, profilerStack);
}
else if (hookEvent == NS_SLUA::ProfilerHookEvent::PHE_RETURN)
{
	SluaProfilerDataManager::WatchEnd(shortSrc, lineDefined, funcName, time, profilerStack);
}
// ★ 采样事件有统一后端，可持续构建调用树而不是只看 aggregate
```

**吸收建议**

- 第一阶段不要新建一套独立 profiler UI；先按 `DebugAndToolchain_ArchReview.md:914-949,1059-1095` 的建议，把 `BindingCoverageId` 和 `DispatchAssumptionManifest` 落下来。
- 在现有 `metrics.json`、`StateDump` 和 `AS_FunctionTable_*` 旁补一份可 join 的性能 sidecar，例如 `AS_PerfCoverage.json`，字段首版只需 `BindingCoverageId`、`NativeFormKind`、`AssumptionKinds`、`SampleMetricNames`。
- 保留 `FCpuProfilerTraceScoped` 和 `AS_LLM_SCOPE` 的现有路线；如果要补 live profiling，优先做 dev-only `UE_TRACE`/JSON emit，并复用同一份 `BindingCoverageId`，不要再造第三套函数身份。
- 让性能测试从“只写 total/median”扩展到“写 total/median + 参与函数/绑定范围摘要”；这样才能把回归结果和 `Bind API GAP`、JIT coverage 放进同一张账本。

**优先级**

- `P2`
- 理由：它不阻塞当前 `P10 UInterface / Bind API GAP` 主线，但又强依赖前文 `D6` 与 `Arch-DT26/30` 的身份合同。先收 identity，再扩 profiling，成本和回归面都更可控。

### D10 文档与示例组织补充：参考手册已经够多，缺的是用户学习闭环

**当前状态**

- `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp:407-457,675-721` 说明当前仓库能够从 live script engine 收集 API 元数据，并稳定导出 `Docs/angelscript/generated/*.hpp`。
- `Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleActorTest.cpp:9-33` 等文件内嵌了质量很高的 `.as` 示例脚本，但这些示例被包在 C++ 自动化测试里。
- `Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleTestSupport.cpp:16-58` 会把示例脚本拼成内存字符串，并以 `ScriptExamples/<File>.as` 的虚拟路径编译；这意味着示例首先是测试 fixture，不是用户工作区文件。
- `Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleCoverageTests.cpp:172-180` 甚至直接把 coverage 示例放在 `Documents/Plans/Plan_ScriptExamplesExpansion/Coverage/*.as` 下，进一步证明教学资产与计划/测试资产仍混在一起。
- `CrossComparison.md:1070-1082` 与 `Hazelight_Analysis.md:1393-1401` 已说明当前 `.hpp docs dump` 是一条延续自 Hazelight 的有效路径；因此 `D10` 的新增差距不在 “有没有 docs exporter”，而在 “有没有给用户直接上手的 sample/tooling”。

```
[Angelscript] D10 Current Learn Surface
DumpDocumentation()
├─ Docs/angelscript/generated/*.hpp                // API reference
Examples/*.cpp
├─ embedded .as strings                            // 示例主要活在测试里
├─ compile from memory via VirtualFileName         // 运行路径是测试专用虚拟文件
└─ coverage examples under Documents/Plans/...     // 教学资产与计划资产混用
TESTING_GUIDE.md                                   // 面向测试作者

[UnLua] Learn -> Scaffold -> Run
README -> Quickstart -> Toolbar.CreateLuaTemplate
      -> Config/LuaTemplates/*.lua
      -> Content/Script/Tutorials/*.lua
      -> IntelliSense commandlet
```

[7] 当前 docs dump 和 examples 的确都存在，但两者都更偏维护者资产：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp
// 函数: FAngelscriptDocs::DumpDocumentation
// 行号: 675-713
// 位置: 把 API 产物导出成 `Docs/angelscript/generated/*.hpp`
// ============================================================================
for (auto It : Classes)
{
	FDocClass& ClassDoc = It.Value;
	FString Filename = FPaths::ProjectDir() / TEXT("/Docs/angelscript/generated") / ClassDoc.ClassName + TEXT(".hpp");
	Content += FString::Printf(TEXT("/* Class: %s \n %s */ \n class %s"),
		*ClassDoc.ClassName, *ClassDoc.Documentation, *ClassDoc.ClassName);
	// ...
	FFileHelper::SaveStringToFile(Content, *Filename);
}
// ★ 这是一条很强的 reference manual 导出链

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleTestSupport.cpp
// 函数: AngelscriptScriptExamples::RunScriptExampleCompileTest
// 行号: 28-58
// 位置: 示例脚本以内嵌字符串方式进入测试引擎
// ============================================================================
const FString ExampleFileName = Example.ExampleFileName;
const FString ModuleNameString = FPaths::GetBaseFilename(ExampleFileName);
// ...
CombinedScriptCode += Example.ScriptText;

const FString VirtualFileName = FString::Printf(TEXT("ScriptExamples/%s"), *ExampleFileName);
const bool bCompiled = CompileAnnotatedModuleFromMemory(&Engine, ModuleName, VirtualFileName, CombinedScriptCode);
// ★ 示例首先是测试 fixture，而不是用户工作区里的真实 `.as` 文件
```

[8] 当前示例质量不低，但仍主要被包装在测试/计划环境里：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleActorTest.cpp
// 函数: GActorExample
// 行号: 9-33
// 位置: 教学性质很强的脚本示例直接内嵌在 C++ 自动化测试中
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
	default bReplicates = true;
	default Tags.Add(n"ExampleTag");
	UFUNCTION()
	// ...

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleCoverageTests.cpp
// 函数: FAngelscriptScriptExampleCoverageActorTest::RunTest
// 行号: 172-180
// 位置: coverage 示例直接取自 `Documents/Plans/`
// ============================================================================
UClass* ScriptClass = CompileCoverageExample(
	*this,
	Engine,
	ModuleName,
	TEXT("Documents/Plans/Plan_ScriptExamplesExpansion/Coverage/Example_Coverage_Actor.as"),
	TEXT("ACoverageExampleActor"));
// ★ 教学/验证资产仍附着在计划目录而不是正式 sample workspace
```

**差距描述**

- `无能力缺失`：自动 API 参考导出、示例脚本内容、测试指南当前都已经存在。
- `实现方式不同`：当前仓库更偏 “reference manual + maintainer test assets”；`UnLua` 更偏 “新手入口 + 模板生成 + 可直接运行的 tutorial 脚本”。
- `实现质量差异`：示例不是 workspace-native 资产。它们难以直接被用户打开、复制、改写和跟随教程运行；同时 docs、tests、plans 三类资产还没有清晰分层。

**参考方案**

- `Reference/UnLua/README.md:35-69`
- `Reference/UnLua/Docs/CN/Quickstart_For_UE_Newbie.md:11-30`
- `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:257-313`
- `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp:55-123`
- `Reference/UnLua/Plugins/UnLua/Config/LuaTemplates/Actor.lua:9-36`

[9] `UnLua` 的重点不是“文档更多”，而是“学习资产就是可执行资产”：

```markdown
<!-- =========================================================================
文件: Reference/UnLua/README.md
函数: 新手入口 / 更多示例 / 文档
行号: 35-69
位置: README 直接把用户引向 Quickstart、教程脚本与 IDE 文档
=========================================================================== -->
1. 新建蓝图后打开，在UnLua工具栏中选择 `绑定`
2. 在接口的 `GetModule` 函数中填入Lua文件路径
3. 选择UnLua工具栏中的 `创建Lua模版文件`
4. 打开 `Content/Script/...` 编写你的代码

更多示例：
- `01_HelloWorld`
- `07_CallLatentFunction`
- `12_CustomLoader`
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp
// 函数: FUnLuaEditorToolbar::CreateLuaTemplate_Executed
// 行号: 257-313
// 位置: README/Quickstart 里的“创建模板”是真实 editor 动作，不是纸面步骤
// ============================================================================
const auto Func = Class->FindFunctionByName(FName("GetModuleName"));
Class->GetDefaultObject()->ProcessEvent(Func, &ModuleName);

auto RelativeFilePath = "Config/LuaTemplates" / TemplateClassName + ".lua";
FFileHelper::LoadFileToString(Content, *FullFilePath);
Content = Content.Replace(TEXT("TemplateName"), *TemplateName)
				 .Replace(TEXT("ClassName"), *UnLua::IntelliSense::GetTypeName(Class));
FFileHelper::SaveStringToFile(Content, *FileName, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
// ★ 学习脚手架和 editor 行为直接闭环
```

```lua
-- ============================================================================
-- 文件: Reference/UnLua/Plugins/UnLua/Config/LuaTemplates/Actor.lua
-- 行号: 9-36
-- 位置: 模板本身就是用户可直接修改的工作区资产
-- ============================================================================
---@type ClassName
local M = UnLua.Class()

-- function M:UserConstructionScript()
-- end

-- function M:ReceiveBeginPlay()
-- end

-- function M:ReceiveTick(DeltaSeconds)
-- end
```

**吸收建议**

- 继续保留 `.hpp docs dump`，不要把它误判成技术债；它解决的是 API reference，而不是 quickstart。
- 选取一批已经成熟的 `Examples/*.cpp`，拆成真实 `.as` 文件，落到 `Samples/ScriptTutorials/` 或等价目录；测试反过来消费这些真实 sample 文件，而不是继续以内嵌字符串为唯一真相。
- 为 sample/template 增加最小 editor 或 commandlet materializer，让用户可以把示例拷进项目脚本目录；不需要改引擎，也不需要先做复杂 UI。
- 把 `Documents/Plans/.../Coverage/*.as` 这类验证资产与“对外教学 sample”彻底分层：前者继续服务 regression，后者服务 quickstart/workspace/IDE。

**优先级**

- `P3`
- 理由：它与当前 `P10 UInterface / Bind API GAP` 没有直接阻塞关系；在 `D6/D7` 的统一产物和 driver 稳定前，过早包装用户教程只会把现有测试资产形态产品化。

### 值得吸收的设计模式（增量）

- `One producer, multiple drivers`：`puerts` 的 `ToolMenus + console command -> GenUeDts()`，以及 `UnLua` 的 `Editor + commandlet -> IntelliSense` 都说明，先有单一 producer，UI/headless 才不会各长一套 contract。
- `Stable identity before performance UI`：`D8` 最值得吸收的不是马上做 profiler 面板，而是先让 `UHT coverage / JIT native form / perf metrics` 共用稳定键；没有 identity 的可视化只会制造第三本账。
- `Workspace-native learning assets`：`UnLua` 的模板和教程之所以有效，不是因为文案多，而是因为资产本身就存在于 `Content/Script` 工作区里；这比把示例继续埋在 `Examples/*.cpp` 更适合长期吸收。

### 改进路线建议（增量）

1. `P2`：在 `D6/D11` 现有 manifest 工作完成后，新增统一 `EditorArtifactService`，先收口 `BlueprintImpactScan`、`AllScriptRoots` 与 legacy bind 生成三条产物/扫描型路径，让 `ToolMenus`、commandlet、CI 共用一个 producer。
2. `P2`：落 `BindingCoverageId + DispatchAssumptionManifest + perf sidecar`，先把 `StaticJIT`、`metrics.json`、`StateDump` 对账，再决定是否需要更重的 profiler UI 或 remote stream。
3. `P3`：把一批高质量 `Examples/*.cpp` 外置成真实 `.as` sample/template，新增最小 quickstart/materializer，让 `README -> sample -> IDE` 成为正式用户路径；测试继续复用这些 sample，而不是再维护一份并行脚本真相。

---

## 深化分析 (2026-04-08 19:09:05)

### 执行摘要（增量）

- `D2` 当前 `NativeImplement / NativeInheritedImplement / NativeReferenceRoundTrip` 三个 native interface 场景并不走生产自动绑定链，而是测试先手工 `ReferenceClass + GenericMethod + plainUserData` 补齐 `UInterface`；这会把“PoC 可跑通”误读成“主路径已补齐”。
- `D6` 现在至少存在三套互不相认的失败口径：UHT 只区分 `Stub/Direct`，runtime reflective fallback 用 `RejectedInterfaceClass` 等枚举，`Docs/DebugDatabase` 则只枚举已经进 script engine 的函数。相同 gap 不能在 artifact、运行时、IDE 三侧对账。
- `D9` 当前 `CreateTestingFullEngine()->InitializeForTesting()` 只做 minimal bind graph，但若干 core test 却把它当 production-like full engine 使用，并直接把 `FAngelscriptType::GetTypes().Num() > 0` 视为主路径健康证据；这会把 harness 初始化差异和真实 `Bind API GAP` 混成一类 failure。
- 这三点组合起来意味着：`P10 UInterface / Bind API GAP` 目前缺的不只是 runtime 功能，还缺“机器可证明的 closeout contract”。仓库中未发现 `todo.md`，本轮优先级按 `Documents/Plans/Plan_CppInterfaceBinding.md` 与 `Documents/Guides/TechnicalDebtInventory.md` 体现的主线排序。

### 差距矩阵（增量）

| 维度 | 新增观察点 | 总评等级 | 主要差距类型 | 优先级 |
| --- | --- | --- | --- | --- |
| D2 反射绑定机制 | native interface regression 仍走手工绑定旁路 | 实现质量差异 | 测试遮蔽真实能力缺口 | P0 |
| D6 代码生成与 IDE 支持 | UHT / runtime / IDE 的 failure vocabulary 三分裂 | 实现质量差异 | 诊断 contract 缺失 | P1 |
| D9 测试基础设施 | `Testing-Full` 初始化不等于 production-like bind graph | 实现差异 | 验证底座与生产 owner 不一致 | P1 |

### D2 补充：native interface regression 仍在绕过生产自动绑定链

**当前状态**

- `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceNativeTests.cpp:23-84` 在测试内部自建 `TestCallInterfaceMethod()`、`BindNativeInterfaceMethod()` 和 `EnsureNativeInterfaceFixturesBound()`；helper 会直接 `ReferenceClass(TypeName, InterfaceClass)`，再写 `plainUserData = InterfaceClass`，并手工把 `GetNativeValue` / `SetNativeMarker` / `AdjustNativeValue` 等方法挂进 type。
- 同文件 `:102-107,218-224` 显示 `NativeImplement` 和 `NativeInheritedImplement` 用例都会在编译脚本前先调用 `EnsureNativeInterfaceFixturesBound()`。因此这些测试证明的是“补钉后 `Cast + Execute_ + reflective invoke` 能成立”，不是“生产路径已经会自动暴露 C++ `UINTERFACE`”。
- 生产 owner 仍然是 `Bind_BlueprintType.cpp` 的 `TObjectRange<UClass>() -> ShouldBindEngineType() -> BindUClass()`，而 `ShouldBindEngineType()` 仍没有 `CLASS_Interface` 的专门放行逻辑，见 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:962-1027`。
- `FUObjectType::CreateProperty()` / `MatchesProperty()` 仍只处理 `FClassProperty` / `FObjectProperty`，没有 `FInterfaceProperty` 分支，见 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:122-139,149-180`。
- `Bind_UObject.cpp:185-214` 说明 `opCast` 的 interface 语义本来就建立在 `ScriptType->GetUserData() -> UClass* -> ImplementsInterface()` 上。也就是说，一旦测试 helper 预先把 `plainUserData` 填好，cast/query 会看起来“像已支持”；这正是当前 regression 容易误导的原因。

```text
[Current Native UInterface Regression Path]
NativeInterfaceTests
├─ EnsureNativeInterfaceFixturesBound()             // 测试先手工补 interface type
│  ├─ ReferenceClass(TypeName, InterfaceClass)
│  ├─ plainUserData = InterfaceClass
│  └─ GenericMethod("GetNativeValue"...)
├─ CompileScriptModule(... : UAngelscriptNativeParentInterface)
└─ UObject::opCast()
   ├─ ScriptType->GetUserData() -> UClass*
   └─ ImplementsInterface(UClass*)                 // 因 helper 已补 userData 而成功

[Production Auto Bind Path]
TObjectRange<UClass>()
└─ ShouldBindEngineType()
   ├─ CLASS_Native gate
   ├─ BlueprintType / callable scan
   └─ no CLASS_Interface-specific registration
```

[10] 当前 regression 的前提就是测试自己先把 native interface 注册进去：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceNativeTests.cpp
// 函数: BindNativeInterfaceMethod / EnsureNativeInterfaceFixturesBound / RunTest
// 位置: native interface 测试前置 helper
// ============================================================================
void BindNativeInterfaceMethod(FAngelscriptBinds& Binds, const TCHAR* Declaration, const TCHAR* FunctionName)
{
	FInterfaceMethodSignature* Signature = FAngelscriptEngine::Get().RegisterInterfaceMethodSignature(FName(FunctionName));
	Binds.GenericMethod(FString(Declaration), TestCallInterfaceMethod, Signature);
	// ★ 测试直接把 generic 调用桥挂到 interface type 上
}

void EnsureNativeInterfaceBoundForTests(UClass* InterfaceClass)
{
	auto* ScriptEngine = FAngelscriptEngine::Get().Engine;
	const FString TypeName = FAngelscriptType::GetBoundClassName(InterfaceClass);
	if (ScriptEngine->GetTypeInfoByName(TCHAR_TO_ANSI(*TypeName)) != nullptr)
	{
		return;
	}

	FAngelscriptBinds Binds = FAngelscriptBinds::ReferenceClass(TypeName, InterfaceClass);
	auto* TypeInfo = (asCTypeInfo*)Binds.GetTypeInfo();
	if (TypeInfo != nullptr)
	{
		TypeInfo->plainUserData = (SIZE_T)InterfaceClass;
		// ★ 关键前提：测试手工写入 interface 对应的 UClass
	}

	if (InterfaceClass == UAngelscriptNativeParentInterface::StaticClass())
	{
		BindNativeInterfaceMethod(Binds, TEXT("int GetNativeValue() const"), TEXT("GetNativeValue"));
		BindNativeInterfaceMethod(Binds, TEXT("void SetNativeMarker(FName Marker)"), TEXT("SetNativeMarker"));
	}
}

bool FAngelscriptScenarioInterfaceNativeImplementTest::RunTest(const FString& Parameters)
{
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_FRESH();
	EnsureNativeInterfaceFixturesBound();
	// ★ 用例在编译脚本前就绕过生产注册链，把 interface fixture 提前补齐
```

[11] 生产路径里，`UInterface` 仍没有进入统一自动绑定与 property 主桥：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp
// 函数: BindUClass / ShouldBindEngineType / FUObjectType::CreateProperty / MatchesProperty
// 位置: 生产自动绑定与 UObject property 桥
// ============================================================================
static void BindUClass(UClass* Class, const FString& TypeName)
{
	auto Class_ = FAngelscriptBinds::ReferenceClass(TypeName, Class);
	auto Type = MakeShared<FUObjectType>(Class, TypeName, Class_.GetTypeInfo());
	FAngelscriptType::Register(Type);
	// ★ class/interface 当前仍共用普通 UObject 注册入口
}

bool ShouldBindEngineType(UClass* Class)
{
	if (!Class->HasAnyClassFlags(CLASS_Native))
		return false;

	if (Class->GetBoolMetaData(NAME_BlueprintType))
		return true;

	while (CheckClass != nullptr && !bHasBlueprintCallable)
	{
		CheckClass->GenerateFunctionList(NameArray);
		for (auto& Elem : NameArray)
		{
			UFunction* Function = CheckClass->FindFunctionByName(Elem);
			if (Function->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintEvent))
			{
				bHasBlueprintCallable = true;
			}
		}
	}
	// ★ 没有 `CLASS_Interface` 的专门放行或专门 bind family
}

FProperty* CreateProperty(const FAngelscriptTypeUsage& Usage, const FPropertyParams& Params) const override
{
	auto* Property = new FObjectProperty(Params.Outer, Params.PropertyName, RF_Public);
	Property->PropertyClass = Class != nullptr ? Class : (UClass*)Usage.ScriptClass->GetUserData();
	return Property;
	// ★ interface 值仍退化成 `FObjectProperty`
}

bool MatchesProperty(const FAngelscriptTypeUsage& Usage, const FProperty* Property, EPropertyMatchType MatchType) const override
{
	const FObjectProperty* ObjectProp = CastField<FObjectProperty>(Property);
	if (ObjectProp == nullptr)
		return false;
	// ★ 没有 `FInterfaceProperty` 分支
}
```

**差距描述**

- **能力缺失**：生产主链仍然没有自动把 native `UInterface` 当作一等 type/property family 暴露；这一点由 `Bind_BlueprintType.cpp` 和 `Bind_UObject.cpp` 的组合可以直接确认。
- **实现质量差异**：当前 regression 对主线 closeout 不够可信。测试 helper 在 feature 尚未落地主路径时就能制造“cast 与方法调用都成立”的假象。
- **实现方式不同**：当前仓库把 interface 支持拆成“测试手工注册 + runtime cast/query 特判”；最佳参考实现则把 interface 纳入统一 descriptor/factory。

**参考方案**

- `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/FPropertyDescriptor.cpp:47-88`
- `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/ObjectProperty/FInterfacePropertyDescriptor.cpp:29-45`
- `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:533-560`

[12] 参考实现的共同点是：`interface` 先进入统一 property/descriptor factory，再谈上层调用：

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/FPropertyDescriptor.cpp
// 函数: FPropertyDescriptor::Factory
// 位置: property family 工厂
// ============================================================================
FPropertyDescriptor* FPropertyDescriptor::Factory(FProperty* InProperty)
{
	NEW_PROPERTY_DESCRIPTOR(FObjectProperty)
	NEW_PROPERTY_DESCRIPTOR(FNameProperty)
	NEW_PROPERTY_DESCRIPTOR(FDelegateProperty)
	NEW_PROPERTY_DESCRIPTOR(FInterfaceProperty)
	NEW_PROPERTY_DESCRIPTOR(FStructProperty)
	NEW_PROPERTY_DESCRIPTOR(FArrayProperty)
	// ★ interface 和 object/struct/array 同级进入同一条 descriptor factory
}

// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/ObjectProperty/FInterfacePropertyDescriptor.cpp
// 函数: FInterfacePropertyDescriptor::Set
// 位置: interface 值桥接
// ============================================================================
void FInterfacePropertyDescriptor::Set(void* Src, void* Dest) const
{
	const auto Interface = static_cast<FScriptInterface*>(Dest);
	const auto Object = SrcMulti->GetObject();
	Interface->SetObject(Object);
	Interface->SetInterface(Object ? Object->GetInterfaceAddress(Property->InterfaceClass) : nullptr);
	// ★ interface 值桥直接以 `FScriptInterface` 为底层载体
}
```

**吸收建议**

- 在 `P10` closeout 口径上，不应再把 `EnsureNativeInterfaceFixturesBound()` 驱动的测试视作“主路径已支持”的证据；它更适合作为现有 runtime callback 的 PoC 或 compatibility harness。
- 新增一条 production-path regression：不调用 helper，直接断言 `GetTypeInfoByName("UAngelscriptNativeParentInterface")`、正向脚本调用和 `Execute_` bridge 同时成立。当前可先标记为 expected-fail，待 runtime bridge 落地后翻正。
- runtime 层完成 `FInterfaceProperty` / native interface bind 后，再把 `AngelscriptInterfaceNativeTests.cpp` 里的 helper 删掉，避免 future contributors 继续被“测试先补钉”误导。

**优先级**

- `P0`
- 理由：`Plan_CppInterfaceBinding.md:21-23` 已把 “C++ UInterface 未自动注册” 定义成核心缺口。只要 regression 仍走手工绑定旁路，就无法可靠证明 `P10` 已经关闭。

### D6 补充：UHT、runtime 与 IDE 仍在维护三套互不贯通的 failure vocabulary

**当前状态**

- `AngelscriptFunctionTableCodeGenerator.cs:100-139` 对生成物只统计 `Direct` vs `Stub`，判断条件仅是 `EraseMacro == "ERASE_NO_FUNCTION()"`。
- 同文件 `:465-479` 直接把 `Interface / NativeInterface` 写成 `ERASE_NO_FUNCTION()`，没有附带结构化 reason。
- `AngelscriptHeaderSignatureResolver.cs:90-106,171-177` 又把 header 恢复失败压缩成 `non-public / unexported-symbol / overloaded-unresolved`，且 `NormalizeTypeText()` 会主动抹掉 `const` 与 `&`。
- runtime reflective fallback 用另一套枚举：`RejectedNullFunction / RejectedMissingOwningClass / RejectedInterfaceClass / RejectedCustomThunk / RejectedTooManyArguments`，见 `BlueprintCallableReflectiveFallback.h:12-20` 与 `.cpp:254-283`。
- `Docs::DumpDocumentation()` 和 `DebugServer::SendDebugDatabase()` 只遍历已经进入 `asIScriptEngine` 的 type / function，见 `AngelscriptDocs.cpp:520-589,599-645` 与 `AngelscriptDebugServer.cpp:1545-1617`。因此 interface stub 在 UHT 里可能是 `Stub`，在 runtime 里是 `RejectedInterfaceClass`，到了 IDE 侧则可能完全消失。

```text
[Current Failure Vocabulary Split]
UHT generator
├─ Interface -> ERASE_NO_FUNCTION()
└─ Summary csv/json -> Stub or Direct only

Header resolver
├─ non-public
├─ unexported-symbol
└─ overloaded-unresolved

Runtime reflective fallback
├─ RejectedInterfaceClass
├─ RejectedCustomThunk
└─ RejectedTooManyArguments

Docs / DebugDatabase
└─ only bound script types/functions               // unsupported 项通常不出现在数据面里
```

[13] 当前 UHT 侧既把 interface 强制降成 stub，又只输出 `Stub/Direct` 两档统计：

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs
// 函数: GenerateModule / CollectEntries
// 位置: 生成物统计与 interface 特判
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
	// ★ summary 只知道 `Stub` 和 `Direct`
}

if (classObj.ClassType == UhtClassType.Interface || classObj.ClassType == UhtClassType.NativeInterface)
{
	eraseMacro = "ERASE_NO_FUNCTION()";
	// ★ interface/native interface 被硬编码写成 stub
}
else if (AngelscriptFunctionSignatureBuilder.TryBuild(classObj, function, out AngelscriptFunctionSignature? signature, out string? _))
{
	eraseMacro = signature!.BuildEraseMacro();
}
```

[14] runtime 与 IDE 使用的是另一套状态机，而且不会把 unsupported 原因回流给 artifact：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp
// 函数: EvaluateReflectiveFallbackEligibility
// 位置: runtime fallback eligibility
// ============================================================================
if (OwningClass->HasAnyClassFlags(CLASS_Interface))
{
	return EAngelscriptReflectiveFallbackEligibility::RejectedInterfaceClass;
	// ★ runtime 知道“这是 interface-class rejection”，但这个 reason 不会写回 UHT summary
}

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp
// 函数: FAngelscriptDocs::DumpDocumentation
// 位置: 文档导出只遍历已经绑定进 script engine 的类型/函数
// ============================================================================
FString TypeName = ANSI_TO_TCHAR(ScriptType->GetName());
FDocClass& ClassDoc = Classes.FindOrAdd(TypeName);

int32 MethodCount = ScriptType->GetMethodCount();
for (int32 MethodIndex = 0; MethodIndex < MethodCount; ++MethodIndex)
{
	auto* ScriptFunction = ScriptType->GetMethodByIndex(MethodIndex);
	if (ScriptFunction->GetObjectType() != ScriptType)
		continue;

	AddFunction(ClassDoc, ScriptFunction, true);
	// ★ 只有已经存在于 script engine 的 function 才会进入 docs
}
```

[15] 头文件 resolver 还会把一部分类型差异压回文本比较，进一步扩大“无法对账”的范围：

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs
// 函数: TryBuild / NormalizeTypeText
// 位置: header 签名恢复
// ============================================================================
if (parsedSignature!.ParameterTypes.Count == expectedParameterTypes.Count &&
	AreTypesEquivalent(expectedParameterTypes, parsedSignature.ParameterTypes) &&
	NormalizeTypeText(expectedReturnType) == NormalizeTypeText(parsedSignature.ReturnType))
{
	exactMatches.Add(parsedSignature);
}

private static string NormalizeTypeText(string typeText)
{
	return CollapseWhitespace(typeText)
		.Replace("const ", string.Empty, StringComparison.Ordinal)
		.Replace("&", string.Empty, StringComparison.Ordinal)
		.Replace(" ", string.Empty, StringComparison.Ordinal)
		.Trim();
	// ★ `const/ref/interface wrapper` 的细节在这里被压回了字符串
}
```

**差距描述**

- **实现质量差异**：当前仓库已经有 summary、csv、docs、debug DB，但它们没有共享同一套 unsupported reason，也没有共享 join key。
- **能力缺失**：缺少“为什么这个 symbol 不可调用/未导出/未入 IDE”这件事的单一账本。于是 `Bind API GAP` 的 closeout 只能靠人工对读三套状态机。
- **实现方式不同**：参考实现倾向于让 declaration/type factory 直接表达接口与函数可见性，而不是先在 UHT 降成 stub，再在 runtime/IDE 各自补解释。

**参考方案**

- `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:360-457`
- `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1300-1327`
- `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/FPropertyDescriptor.cpp:47-88`

[16] 参考实现更接近“单一 producer 直接导出可消费 contract”：

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp
// 函数: FTypeScriptDeclarationGenerator::GenTypeScriptDeclaration / GenClass
// 位置: 统一声明资产导出
// ============================================================================
for (int i = 0; i < SortedClasses.Num(); ++i)
{
	UObject* Class = SortedClasses[i];
	Gen(Class);
}

FFileHelper::SaveStringToFile(ToString(), *UEDeclarationFilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
// ★ 所有 class/interface 声明最终落成同一份 `ue.d.ts`

for (int i = 0; i < Class->Interfaces.Num(); i++)
{
	for (TFieldIterator<UFunction> FunctionIt(Class->Interfaces[i].Class, EFieldIteratorFlags::IncludeSuper); FunctionIt; ++FunctionIt)
	{
		if (!GenFunction(TmpBuff, *FunctionIt))
		{
			continue;
		}
		TryToAddOverload(Outputs, FunctionIt->GetName(), (FunctionIt->FunctionFlags & FUNC_Static) != 0, TmpBuff.Buffer);
		// ★ interface function 直接进入同一份声明图，而不是先降成独立的 stub 口径
	}
}
```

**吸收建议**

- 在插件内新增一份共享 `CapabilityLedger` 或 `UnsupportedReasonManifest`，至少统一四个字段：`SymbolKey`、`OwnerStage`、`Status`、`Reason`。首版不必改引擎，不必一次解决所有 family。
- `UHT` 生成 summary/csv 时不应再只输出 `Stub/Direct`；对 `Interface/NativeInterface`、`non-public`、`unexported-symbol`、`overloaded-unresolved` 至少需要保留结构化 reason。
- runtime reflective fallback 和 `Docs/DebugDatabase` 应消费同一份 reason 枚举。即使 IDE 端不展示 unsupported 细节，也应能按 `SymbolKey` 回查“为什么没出现”。
- `P10` regression 增加一条 reason-parity 断言：同一 interface symbol 在 UHT、runtime、IDE 三层的 `Status/Reason` 必须可对齐，而不是一层是 `Stub`、一层是 `RejectedInterfaceClass`、第三层直接缺席。

**优先级**

- `P1`
- 理由：它不是 `UInterface` 值桥接本身，但没有统一 failure ledger，就无法稳定量化 `Bind API GAP` 的收口进度，也很难避免修一层、漏一层的静默回退。

### D9 补充：`Testing-Full` 初始化与 production-like bind graph 仍不是同一个 owner

**当前状态**

- `AngelscriptTestUtilities.h:435-438` 的 `CreateFullTestEngine()` 只是 `CreateIsolatedFullEngine()` 的薄包装；后者最终调用 `FAngelscriptEngine::CreateTestingFullEngine()`。
- `AngelscriptEngine.cpp:615-624` 显示 `CreateTestingFullEngine()` 直接进入 `InitializeForTesting()`。
- `Initialize()` 与 `InitializeForTesting()` 的差异是实质性的：前者执行 `PreInitialize_GameThread() -> Initialize_AnyThread() -> PostInitialize_GameThread() -> InitializeOwnedSharedState()`，见 `AngelscriptEngine.cpp:819-857`；后者则走 `PreInitialize_GameThread()`、设置 engine properties、`EnsureSharedStateCreated()`、`BindScriptTypes()`、`CreateContext()`、`InitializeOwnedSharedState()`，见 `:859-920`。
- 与此同时，`AngelscriptEngineCoreTests.cpp:174-183,210-218` 把 `CreateFullTestEngine()` 产物当作 full engine 主路径，直接用 `FAngelscriptType::GetTypes().Num() > 0` 断言类型元数据“已经像生产环境一样建立”。
- 这意味着：只要某个能力依赖的不是 `BindScriptTypes()`，而是 production 初始化链中的其他 bind replay / registry 建立 / artifact replay，那么测试失败时很难区分“真实 runtime 缺口”和“test init profile 没有 replay 对应 owner”。

```text
[Testing-Full Init]
CreateFullTestEngine()
└─ CreateTestingFullEngine()
   └─ InitializeForTesting()
      ├─ PreInitialize_GameThread()
      ├─ EnsureSharedStateCreated()
      ├─ BindScriptTypes()
      ├─ CreateContext()
      └─ InitializeOwnedSharedState()

[Production-Like Init]
RuntimeModule / current engine
└─ Initialize()
   ├─ PreInitialize_GameThread()
   ├─ Initialize_AnyThread()
   ├─ PostInitialize_GameThread()
   └─ InitializeOwnedSharedState()

[Current Core Assertion]
EngineCoreTests
└─ FAngelscriptType::GetTypes().Num() > 0         // 把 minimal bind graph 当 production parity 证据
```

[17] 当前 test engine helper 与 core test 的语义是假定 “Testing-Full = full production owner”：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h
// 函数: CreateFullTestEngine
// 位置: 测试 full engine helper
// ============================================================================
inline TUniquePtr<FAngelscriptEngine> CreateFullTestEngine()
{
	return CreateIsolatedFullEngine();
	// ★ helper 名称是 full engine，但底层走的是 testing-specific 初始化路径
}

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineCoreTests.cpp
// 函数: FAngelscriptFullDestroyClearsTypeStateTest::RunTest
// 位置: full engine 生命周期断言
// ============================================================================
TUniquePtr<FAngelscriptEngine> FullEngine = AngelscriptTestSupport::CreateFullTestEngine();
{
	FAngelscriptEngineScope Scope(*FullEngine);
	TestTrue(TEXT("Last full destroy core test should populate type metadata while the full engine is alive"),
		FAngelscriptType::GetTypes().Num() > 0);
	// ★ 这里把 minimal init 产生的 type metadata 直接当作主路径健康信号
}
```

[18] 生产初始化链和 testing 初始化链并不相同：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: Initialize / InitializeForTesting
// 位置: 生产与测试初始化差异
// ============================================================================
void FAngelscriptEngine::Initialize()
{
	PreInitialize_GameThread();
	if (ShouldInitializeThreaded())
	{
		Initialize_AnyThread();
	}
	else
	{
		Initialize_AnyThread();
	}
	PostInitialize_GameThread();
	InitializeOwnedSharedState();
	// ★ 生产路径是完整初始化事务
}

void FAngelscriptEngine::InitializeForTesting()
{
	PreInitialize_GameThread();
	EnsureSharedStateCreated();
	{
		FAngelscriptEngineScope ScopedTestingEngine(*this);
		BindScriptTypes();
	}
	GameThreadTLD->primaryContext = CreateContext();
	bIsInitialCompileFinished = true;
	InitializeOwnedSharedState();
	// ★ testing 路径只 replay 了 minimal script-type bind graph
}
```

**差距描述**

- **实现差异**：当前 `Testing-Full` 更像 “minimal script engine bootstrap”，不是 production-like bind graph 的镜像。
- **实现质量差异**：当测试拿 `CreateFullTestEngine()` 去为 `Bind API GAP`、type metadata 或 interface capability 背书时，容易把 harness 差异误判成产品缺陷，反之亦然。
- **能力缺失**：没有一个明确的 production-parity test profile，去稳定复用生产 owner 的 bind/registry/capability contract。

**参考方案**

- `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp:54-88`
- `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:98-113`

[19] 参考实现更偏向“环境初始化一次建齐 registry graph”，测试/工具不会再去猜 owner：

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp
// 函数: FCSharpEnvironment::Initialize
// 位置: 环境初始化时一次建齐 registry graph
// ============================================================================
void FCSharpEnvironment::Initialize()
{
	Domain = new FDomain({ "", FUnrealCSharpFunctionLibrary::GetFullAssemblyPublishPath() });
	DynamicRegistry = new FDynamicRegistry();
	CSharpBind = new FCSharpBind();
	ClassRegistry = new FClassRegistry();
	ReferenceRegistry = new FReferenceRegistry();
	ObjectRegistry = new FObjectRegistry();
	StructRegistry = new FStructRegistry();
	ContainerRegistry = new FContainerRegistry();
	DelegateRegistry = new FDelegateRegistry();
	MultiRegistry = new FMultiRegistry();
	BindingRegistry = new FBindingRegistry();
	// ★ runtime owner 初始化时，descriptor/registry 就全部就位
}

// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp
// 位置: Lua 环境创建时同样直接建立 registries
// ============================================================================
ObjectRegistry = new FObjectRegistry(this);
ClassRegistry = new FClassRegistry(this);
ClassRegistry->Initialize();
FunctionRegistry = new FFunctionRegistry(this);
DelegateRegistry = new FDelegateRegistry(this);
ContainerRegistry = new FContainerRegistry(this);
PropertyRegistry = new FPropertyRegistry(this);
EnumRegistry = new FEnumRegistry(this);
EnumRegistry->Initialize();
// ★ 测试或编辑器工具只要拿到 env，就消费同一个 owner graph
```

**吸收建议**

- 为测试引擎显式区分两个 profile：`MinimalScriptBootstrap` 与 `ProductionParityBindGraph`。当前 `CreateFullTestEngine()` 更接近前者，不应继续默认拿它给 `Bind API GAP` 背书。
- 新增 production-parity helper，复用生产初始化 owner 的 bind replay / registry 初始化 / artifact producer；`P10` 相关 regression 与 `TypeMetadata` 相关 core test 改走这个 profile。
- 在 `TechnicalDebtInventory.md:260-267` 已把 `InitializeForTesting()` 的元数据缺口归到 `Bind API GAP` 的前提下，应把这件事从“测试 harness 解释”升级为正式 closeout criteria：测试 profile 必须与主线 capability owner 对齐。

**优先级**

- `P1`
- 理由：它不是 `UInterface` runtime bridge 的直接实现，但它决定后续 `P10` 回归到底是在验证产品功能，还是在验证一个精简测试引擎。没有 production-parity profile，closeout 证据始终不够稳。

### 值得吸收的设计模式（增量）

- `No test-only registration in capability closeout`：如果测试为了跑通能力点而先调用 `ReferenceClass()`、`GenericMethod()` 或手工写 `plainUserData`，这条回归就不应被当作 feature support 证据。
- `Single failure ledger across build/runtime/IDE`：UHT、runtime fallback、docs/debug DB 必须共享同一份 `Status + Reason + SymbolKey`，否则 `Bind API GAP` 永远只能靠人工逐层对读。
- `Environment-owned registry graph`：像 UnrealCSharp / UnLua 那样，让环境初始化一次建齐 registries/descriptor factories，测试与工具直接消费 owner graph，而不是自己猜哪些 bind 已 replay。

### 改进路线建议（增量）

1. `P0`：把 `AngelscriptInterfaceNativeTests.cpp` 从“手工 helper 驱动的 PoC”与“production-path regression”拆开；后者禁止调用 `EnsureNativeInterfaceFixturesBound()`，作为 `P10` 的真正 closeout gate。
2. `P1`：新增共享 `CapabilityLedger`，统一 `UHT summary/csv`、`reflective fallback`、`Docs/DebugDatabase` 的 `Status/Reason/SymbolKey`，先把 interface/native interface reason 打通。
3. `P1`：新增 `ProductionParityBindGraph` 测试引擎 profile，并把 `TypeMetadata`、`Bind API GAP`、`UInterface` joined regression 切到这个 profile；`MinimalScriptBootstrap` 保留给局部 parser/runtime 单测。

---

## 深化分析 (2026-04-08 19:25:22)

### 本轮新增关键发现

- `P10` 还没进入“可收口”阶段的关键，不只是 native `UInterface` 没自动注册，而是 interface callable 仍然停在 `FName` 级别转发；UE 反射层同时只拿到 name-only `UFunction` 壳。
- 当前 interface identity 仍受短名索引与全局 `UClass` 扫描支配。这不是“是否支持接口”的问题，而是 reload / namespace / 同短名场景下的确定性问题。
- `AngelscriptUHTTool` 已经具备 `SkippedEntries / SkippedReasonSummary` 账本，但 interface `forced stub` 的最终决策不从这条账本流出，所以它目前还量不出 `P10` 的真实收口进度。

### 差距矩阵（本轮增量修正）

| 维度 | 本轮聚焦点 | 差距等级 | 差距判断 | 优先级 |
| --- | --- | --- | --- | --- |
| D2 反射绑定机制 | interface callable signature authority | 能力缺失 | 缺少可复用的 typed interface callable descriptor；runtime dispatch 与 UE 反射都只保留了 name-only 信息 | P0 |
| D4 热重载 | interface identity / reload determinism | 实现差异 | 已有 reload 能力，但 interface 索引与恢复依赖 short-name / global scan，soft path 也没有显式重建 `UClass::Interfaces` | P1 |
| D6 代码生成与 IDE 支持 | UHT diagnostics authority split | 实现差异 | 已有 skipped-reason artifact，但它与最终 `Interface -> ERASE_NO_FUNCTION()` 不是同一个 producer | P1 |

### [D2] 反射绑定机制：interface callable 的 authority 仍停在 `FName`，不是完整签名

**当前状态**

`Preprocessor` 在注册 interface method 时拿到了完整 `MethodDecl`，但 `FInterfaceMethodSignature` 实际只保存了 `FName FunctionName`，见 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:59-62`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:1123-1153`。运行时 `CallInterfaceMethod()` 再按名字 `FindFunction()`，见 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:56-67`。与此同时，interface full reload 生成的 UE 反射壳只创建了“同名、无参数、无返回值”的最小 `UFunction`，见 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2803-2830`。

```
[Current Interface Callable Flow]
MethodDecl "void TakeDamage(float Amount)"
├─ Preprocessor -> RegisterObjectMethod(full decl)     // AS 侧看见完整声明
│  └─ userData = FInterfaceMethodSignature{FunctionName}
├─ Runtime -> CallInterfaceMethod()                    // 只按函数名转发
│  └─ UObject::FindFunction(FunctionName)
└─ FullReload -> minimal UFunction stub                // UE 反射层只剩名字壳
```

[20] 当前 interface payload 在进入 runtime 前已经丢失签名形状：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h
// 位置: interface 方法 payload 定义
// ============================================================================
struct FInterfaceMethodSignature
{
	FName FunctionName; // ★ 只保存名字，没有参数、返回值、const/ref 等信息
};

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp
// 位置: interface method 注册到 AS engine
// ============================================================================
auto* Sig = Engine.RegisterInterfaceMethodSignature(FName(*MethodName));

int32 FuncId = Engine.Engine->RegisterObjectMethod(
	TCHAR_TO_ANSI(*InterfaceName),
	TCHAR_TO_ANSI(*ASDecl),
	asFUNCTION(CallInterfaceMethod),
	asCALL_GENERIC,
	nullptr);

ScriptFunc->SetUserData(Sig, 0); // ★ 完整 MethodDecl 到这里被压成 name-only payload
```

[21] runtime dispatch 和 UE 反射壳都继续沿用 name-only 语义：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 函数: CallInterfaceMethod / DoFullReloadClass(interface path)
// 位置: interface runtime dispatch 与 UE 反射壳生成
// ============================================================================
UFunction* RealFunc = Object->FindFunction(Sig->FunctionName);
if (RealFunc == nullptr) return;
InvokeReflectiveUFunctionFromGenericCall(Generic, Object, RealFunc);
// ★ runtime 只按名字找真实函数；同名不同签名在这里没有第二道校验

for (const FString& MethodDecl : InterfaceDesc->InterfaceMethodDeclarations)
{
	// For now, create a minimal UFunction with just the name
	UFunction* NewFunction = NewObject<UFunction>(NewClass, *FuncName, RF_Public);
	NewFunction->FunctionFlags = FUNC_Event | FUNC_BlueprintEvent | FUNC_Public;
	NewFunction->ReturnValueOffset = MAX_uint16;
	NewFunction->StaticLink(true);
	// ★ UE 反射层看见的是“同名壳”，不是带参数/返回值的完整函数签名
}
```

**差距描述**

- `能力缺失`：interface 方法还没有进入统一的 typed callable descriptor。`FInterfaceMethodSignature` 无法表达参数类型、返回值、`const/ref/out` 或默认值，因此 native `UInterface` 进入主链后，`ref/out`、overload 与 `TScriptInterface<>` 仍然没有可靠 contract。
- `实现质量差异`：AS 侧是完整 declaration，runtime dispatch 是 `FName`，UE 反射壳又退成 minimal `UFunction`。同一能力点在三层使用三种表示法，authority 已经分裂。

**参考方案**

最值得吸收的是把 interface 当成正常 class/property/function graph 的一部分统一建模，而不是单独走 name-only 旁路。参考：

- `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp:118-126,257-282`
- `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FCSharpBind.cpp:179-203`
- `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1300-1326`
- `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:598-629`

[22] UnrealCSharp 与 puerts 都把 interface 放进统一的函数/属性主链：

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp
// 位置: 生成 class 时把 interface 直接并入方法图
// ============================================================================
for (auto Interface : InClass->Interfaces)
{
	InterfaceContent += FString::Printf(TEXT(", %s"),
		*FUnrealCSharpFunctionLibrary::GetFullInterface(Interface.Class));
}

for (const auto& InInterface : InClass->Interfaces)
{
	for (TFieldIterator<UFunction> FunctionIterator(InInterface.Class, EFieldIteratorFlags::IncludeSuper,
		EFieldIteratorFlags::ExcludeDeprecated); FunctionIterator; ++FunctionIterator)
	{
		if (!FGeneratorCore::IsSupported(*FunctionIterator))
		{
			continue;
		}
		// ★ interface function 和普通 class function 走同一条生成主链
	}
}

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp
// 位置: interface property 进入统一值桥
// ============================================================================
const FScriptInterface& Interface = InterfaceProperty->GetPropertyValue(ValuePtr);
UObject* Object = Interface.GetObject();
...
FScriptInterface* Interface = reinterpret_cast<FScriptInterface*>(ValuePtr);
Interface->SetObject(Object);
Interface->SetInterface(Object ? Object->GetInterfaceAddress(InterfaceProperty->InterfaceClass) : nullptr);
// ★ interface 值桥直接以 FScriptInterface 为底层载体，而不是额外开旁路
```

**吸收建议**

- 以插件内改造为前提，先把 `FInterfaceMethodSignature` 升级成可复用的 `FAngelscriptCallableDescriptor` 或轻量 handle，字段至少包含 `FunctionName`、`ArgumentTypes`、`ReturnType`、`Qualifier`、`OwnerKind(Interface/Class)`。
- `Preprocessor` 继续调用 `RegisterObjectMethod()`，但 `userData` 不再直接挂 `FName`；改挂 descriptor handle。这样 `CallInterfaceMethod()`、future `FInterfaceProperty`、以及后续 `UInterface` native bind 都能共享同一份签名 authority。
- interface full reload 不应继续停留在 minimal `UFunction` 壳。首选复用 `UASFunction` 现有的 `AddFunctionReturnType()` / `AddFunctionArgument()` / `FinalizeArguments()` 路径；如果短期不想引入完整 `UASFunction`，至少也要把参数 `FProperty` 和 `ReturnValueOffset` 物化出来。

**优先级**

- `P0`
- 理由：这条不是“锦上添花”。如果 native `UInterface` 只补类型可见性，不补 callable authority，`P10` 最终仍只能得到“能 cast / 能看到名字，但签名语义不稳定”的半成品。

### [D4] 热重载：interface identity 与 reload correctness 仍受短名索引控制

**当前状态**

当前 interface identity 主要靠短名驱动。`GetClassDesc()` 查 `DataRefByName.Find(ClassName)`，而 `DataRefByName` 建表时只写 `ClassName`，见 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:145-155,1752-1767`。interface 注册也直接 `RegisterObjectType(InterfaceName)` + `GetTypeInfoByName(InterfaceName)`，见 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2592-2612`。类实现接口时的 fallback 进一步把前导 `U` 去掉后，按 `UClass::GetName()` 全局线性扫描，见 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:5060-5108`。

reload 侧同样存在 interface-only 的恢复旁路。`ShouldFullReload()` 只要“新类当前仍有 `ImplementedInterfaces`”就强制走 full path；反过来，“旧类曾经有接口、但新类已经删空”的情况并没有本地对称条件，见 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2081-2094`。`DoSoftReload()` 重建的是属性偏移、默认组件和实例状态，代码里没有 `Class->Interfaces` 的清空/重建点；真正写 `NewClass->Interfaces.Add(...)` 的逻辑只出现在 finalize/full path，见 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:4113-4198,5059-5139`。这里的“删除最后一个 interface 后是否一定留下脏状态”，是基于源码路径做出的推断。

```
[Current Interface Identity / Reload]
ClassDesc
├─ DataRefByName["UIDamageable"]                    // 短名索引
├─ RegisterObjectType("UIDamageable")              // engine-level 短名 type
├─ ResolveInterfaceClass()
│  └─ TObjectIterator<UClass> by GetName()         // 全局短名扫描
└─ Reload
   ├─ ShouldFullReload() -> only if new ImplementedInterfaces > 0
   ├─ DoSoftReload() -> relink properties/instances
   └─ FinalizeClass() -> only here adds Interfaces
```

[23] 当前 identity 解析链本身就是 short-name first：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 函数: GetClassDesc / CreateFullReloadClass / FinalizeClass
// 位置: interface identity 解析链
// ============================================================================
FDataRef* Ref = DataRefByName.Find(ClassName);
// ★ class/interface 描述的第一层查找只按短名

DataRefByName.Add(ClassData.NewClass->ClassName, FDataRef(ModuleData, ClassData));
// ★ 建表同样只用短名，不带 namespace / module / full symbol key

int TypeId = Engine.Engine->RegisterObjectType(
	TCHAR_TO_ANSI(*InterfaceName),
	0,
	asOBJ_REF | asOBJ_NOCOUNT | asOBJ_IMPLICIT_HANDLE);

asITypeInfo* InterfaceScriptType = Engine.Engine->GetTypeInfoByName(TCHAR_TO_ANSI(*InterfaceName));
// ★ engine-level interface type 也是短名注册与短名回查

for (TObjectIterator<UClass> It; It; ++It)
{
	if (It->GetName() == UnrealInterfaceName && It->HasAnyClassFlags(CLASS_Interface))
		return *It;
}
// ★ native fallback 最终退回到全局短名扫描
```

[24] 当前 reload 逻辑对 interface 增删并不对称：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 函数: ShouldFullReload / DoSoftReload / FinalizeClass
// 位置: interface 相关 reload 决策
// ============================================================================
bool FAngelscriptClassGenerator::ShouldFullReload(FClassData& Class)
{
	if (Class.NewClass->bIsInterface)
		return true;
	if (Class.NewClass->ImplementedInterfaces.Num() > 0)
		return true; // ★ 只看“新类当前仍然实现接口”
	return false;
}

void FAngelscriptClassGenerator::DoSoftReload(FModuleData& ModuleData, FClassData& ClassData)
{
	...
	// ★ 这里重建属性、默认组件和实例，但没有清空或重建 Class->Interfaces 的代码点
}

FImplementedInterface ImplementedInterface;
ImplementedInterface.Class = InterfaceClass;
ImplementedInterface.PointerOffset = 0;
ImplementedInterface.bImplementedByK2 = true;
NewClass->Interfaces.Add(ImplementedInterface);
// ★ interface 表的真正写入发生在 finalize/full path
```

**差距描述**

- `实现差异`：当前系统已有 interface hot reload 与实现校验，但 identity owner 是短名与全局扫描，不是稳定 symbol key。它在“只有一个同名接口”时能工作，在 namespace / 多模块 / reload 链下确定性不足。
- `实现差异`：interface 增加时有强制 full path，interface 删除时本地条件不对称。只要外层编译事务允许 soft path，就存在 `UClass::Interfaces` 继续沿用旧状态的风险。

**参考方案**

最值得吸收的是“registry 按对象身份建模，生成阶段输出全限定 interface 名称”的组合，而不是继续把 interface 当作可容忍的短名特例。参考：

- `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FClassRegistry.cpp:77-102`
- `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Environment/FCSharpEnvironment.cpp:54-73`
- `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp:118-126`

[25] UnrealCSharp 的 identity owner 更接近“对象身份 + 全限定名”：

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FClassRegistry.cpp
// 位置: class/interface descriptor registry
// ============================================================================
FClassDescriptor* FClassRegistry::GetClassDescriptor(const UStruct* InStruct) const
{
	const auto FoundClassDescriptor = ClassDescriptorMap.Find(InStruct);
	return FoundClassDescriptor != nullptr ? *FoundClassDescriptor : nullptr;
}

FClassDescriptor* FClassRegistry::AddClassDescriptor(UStruct* InStruct)
{
	if (const auto FoundClassDescriptor = ClassDescriptorMap.Find(InStruct))
	{
		return *FoundClassDescriptor;
	}

	const auto ClassDescriptor = new FClassDescriptor(InStruct);
	ClassDescriptorMap.Add(InStruct, ClassDescriptor);
	return ClassDescriptor;
}
// ★ registry 以 UStruct* 为 key，不依赖短名碰运气

// ============================================================================
// 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp
// 位置: 生成阶段保留全限定 interface 名称
// ============================================================================
for (auto Interface : InClass->Interfaces)
{
	InterfaceContent += FString::Printf(TEXT(", %s"),
		*FUnrealCSharpFunctionLibrary::GetFullInterface(Interface.Class));
}
// ★ 输出的是 full interface name，不是运行时再回头做短名猜解
```

**吸收建议**

- 在插件内把 `DataRefByName` 升级为稳定 `SymbolKey`。首版不必改 AngelScript runtime，只要把 key 从短名提升为 `Namespace + ClassName` 或等价全限定标识，就能先消掉大部分 script-side 冲突。
- interface 注册不要再用裸 `GetTypeInfoByName(InterfaceName)` 作为 authority；应优先通过 `ClassDesc`、qualified key 或 `UClass*` 反查。如果 fallback 仍要扫 `TObjectIterator<UClass>`，至少在命中多个候选时报编译错误，而不是静默取第一个。
- 对 `ImplementedInterfaces` 的 delta，建议统一提升为“显式 full reload”或在 soft path 新增 `RebuildImplementedInterfaces()`。当前 `P10` 主线应优先保证正确性，不值得为了保留 soft path 而让接口表存在旧状态残留的可能。

**优先级**

- `P1`
- 理由：它不是 native `UInterface` 首次可见性的第一步，但如果不尽快修，`P10` 一旦补齐正向绑定，reload 与 namespace 场景会立刻把能力变成“时好时坏”的非确定性 feature。

### [D6] 代码生成与 IDE 支持：UHT 已有 skipped-reason 账本，但它与最终 interface stub 不是同一个 authority

**当前状态**

`AngelscriptFunctionTableExporter` 已经会统计 `reconstructed / skipped`，并落出 `AS_FunctionTable_SkippedEntries.csv` 与 `AS_FunctionTable_SkippedReasonSummary.csv`，见 `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:27-44,65-161`。但真正生成 `AS_FunctionTable_*.cpp` 时，`CollectEntries()` 会无条件把 `Interface / NativeInterface` 写成 `ERASE_NO_FUNCTION()`，见 `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:449-479`。这两条链路并不是同一个 producer。

更关键的是，当前自动化只验证“skipped csv/summary 存在且格式正确”，并没有要求“被最终 forced stub 的 interface symbol 必须在 skipped ledger 里也能对账”，见 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp:669-745`。这意味着 interface symbol 可以在 exporter 侧被算作 `reconstructed`，却在最终产物里继续是 `Stub`。

```
[Current UHT Authority Split]
SignatureBuilder.TryBuild()
├─ Exporter
│  ├─ reconstructedCount++
│  └─ SkippedEntries.csv / ReasonSummary.csv
└─ CodeGenerator
   ├─ if Interface -> ERASE_NO_FUNCTION()
   └─ Summary.json / EntryCsv -> Direct or Stub

Tests
├─ verify skipped csv header / count
└─ verify direct / stub math
   // 没有 interface-symbol parity 断言
```

[26] 当前 exporter 与 code generator 对 interface 使用的是两套 authority：

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs
// 位置: 诊断账本 producer
// ============================================================================
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
// ★ exporter 的 world-view 是 reconstructed / skipped

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs
// 位置: 最终函数表 producer
// ============================================================================
if (classObj.ClassType == UhtClassType.Interface || classObj.ClassType == UhtClassType.NativeInterface)
{
	eraseMacro = "ERASE_NO_FUNCTION()";
}
else if (AngelscriptFunctionSignatureBuilder.TryBuild(classObj, function, out AngelscriptFunctionSignature? signature, out string? _))
{
	eraseMacro = signature!.BuildEraseMacro();
}
// ★ interface/native interface 在这里被强制降成 stub，不消费 skipped ledger
```

[27] 当前回归只验证账本“存在”，不验证它是否覆盖 interface forced stub：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp
// 位置: skipped csv / skipped reason summary 回归
// ============================================================================
TestEqual(TEXT("Generated function table skipped csv test should write the expected skipped csv header"),
	SkippedLines[0], TEXT("ModuleName,ClassName,FunctionName,FailureReason"));

TestEqual(TEXT("Generated function table skipped reason summary test should write the expected summary csv header"),
	SummaryLines[0], TEXT("FailureReason,SkippedCount"));

TestTrue(TEXT("Generated function table skipped csv rows should include non-empty failure reasons"), bFoundFailureReason);
// ★ 当前只校验“格式和非空”，没有要求 interface forced stub 能在 reason ledger 中被同一 symbol key 对账
```

**差距描述**

- `实现差异`：仓库已经有 skipped-reason artifact，但这条 artifact 不代表最终生成决策。对 `P10` 来说，最危险的不是“没有账本”，而是“账本和最终产物说的不是同一件事”。
- `实现差异`：interface 是当前最重要的差距族，但它恰好被硬编码排除在 skipped ledger 之外。这样一来，`Bind API GAP` 的量化数据会天然低估 interface gap。

**参考方案**

最佳参考不是“再做一份新日志”，而是像 puerts 那样让 declaration producer 直接决定 interface/class 的最终产物。参考：

- `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1300-1326`

[28] puerts 的 declaration generation 使用同一个 producer 处理 class 与 interface：

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp
// 位置: declaration producer
// ============================================================================
FunctionOutputs& Outputs = GetFunctionOutputs(Class);
for (TFieldIterator<UFunction> FunctionIt(Class, EFieldIteratorFlags::ExcludeSuper); FunctionIt; ++FunctionIt)
{
	if (!GenFunction(TmpBuff, *FunctionIt))
	{
		continue;
	}
	TryToAddOverload(Outputs, FunctionIt->GetName(), (FunctionIt->FunctionFlags & FUNC_Static) != 0, TmpBuff.Buffer);
}

for (int i = 0; i < Class->Interfaces.Num(); i++)
{
	for (TFieldIterator<UFunction> FunctionIt(Class->Interfaces[i].Class, EFieldIteratorFlags::IncludeSuper); FunctionIt; ++FunctionIt)
	{
		if (!GenFunction(TmpBuff, *FunctionIt))
		{
			continue;
		}
		TryToAddOverload(Outputs, FunctionIt->GetName(), (FunctionIt->FunctionFlags & FUNC_Static) != 0, TmpBuff.Buffer);
	}
}
// ★ class/interface 最终都由同一个 GenFunction producer 决定是否进产物
```

**吸收建议**

- 不要新造第二套 `CapabilityLedger`。更直接的路线，是把现有 `AngelscriptSkippedFunctionEntry` 扩成统一 `GeneratedSymbolDecision`：至少包含 `SymbolKey`、`Status`、`Reason`、`EraseMacro`。
- `CollectEntries()` 不应再单独硬编码 `Interface -> ERASE_NO_FUNCTION()` 后直接绕过 exporter。应把它也表达成结构化决策，例如 `Status = ForcedStub`、`Reason = interface-class-currently-unsupported`。
- `Summary.json`、`Entries.csv`、`SkippedEntries.csv`、未来 `DebugDatabase` 都应该复用这同一份决策记录。这样 `P10` 一旦补齐 interface 绑定，只需要看到同一 `SymbolKey` 从 `ForcedStub` 变成 `Direct/Reflective`，而不是到处改口径。

**优先级**

- `P1`
- 理由：它不是 interface runtime 能力本身，但它决定 `Bind API GAP` 有没有可相信的量化口径。没有单一 authority，就很难判断 `P10` 到底是“真的补齐了”，还是“换了一套统计话术”。

### 值得吸收的设计模式（增量）

- `One declaration -> one callable descriptor`：同一个 interface method 不应在预处理、runtime dispatch、UE 反射层各自降维；应始终沿同一份 typed descriptor 流动。
- `Stable symbol identity before reload policy`：先把 interface 的 `SymbolKey` 做稳定，再讨论 soft/full reload 粒度；否则 reload policy 只能建立在短名猜测之上。
- `One producer for diagnostics and generated artifacts`：对外可见的 csv/json/debug 数据都应复用同一份“最终生成决策”，不要让 exporter 和 code generator 各说各话。

### 改进路线建议（增量）

1. `P0`：以 `FInterfaceMethodSignature` 为切口引入结构化 callable descriptor，并让 interface `UFunction` 生成参数/返回值元数据。这一步直接服务 `P10 UInterface` 主线。
2. `P1`：把 interface identity 从短名查找升级为稳定 `SymbolKey`，并把任意 `ImplementedInterfaces` delta 变成显式 full reload 或 soft-path `RebuildImplementedInterfaces()`。
3. `P1`：把 `AngelscriptFunctionTableExporter` 与 `CodeGenerator` 的决策合并成同一个 producer，扩展现有 `SkippedEntries / SkippedReasonSummary`，不要另起一套统计体系。
4. `P1`：补一组 interface parity regression，至少覆盖三件事：`callable descriptor` 是否保留完整签名、`soft/full reload` 后 `UClass::Interfaces` 是否与脚本声明一致、同一 interface symbol 在 `csv/json/debug` 三层是否共享同一 `Status/Reason`。

---
## 深化分析 (2026-04-08 19:39:41)

### 执行摘要（增量）

- `D1` 的更深层 blocker 不是“`UInterface` 暂时未补齐”，而是 interface declaration 在 `Preprocessor` 阶段虽然已经拿到完整 `MethodDecl`，后续却被压缩成 `TArray<FString>` 和 `FName` payload。这样 `runtime dispatch`、`UE UFunction`、`docs/debug` 都拿不到同一份结构化 authority。
- `D3` 当前并不是“interface callable 覆盖率不足”，而是把 interface-owner `UFunction` 显式判成 `RejectedInterfaceClass`，并且自动化把这条拒绝写成了正确行为。只补 `FInterfaceProperty / TScriptInterface<>` 不会自然得到正向 interface callable。
- `D5` 当前 debug 协议本身已经很强，但 symbol lifetime 仍然绑在 live `asITypeInfo` 上。`CleanupRemovedClass()` 没有清理 interface `asITypeInfo::UserData`，而 `SendDebugDatabase()` / `GoToDefinition` 又直接消费 type table；基于源码推断，interface 删除或重载后 IDE 侧可能保留幽灵 symbol。
- 参考实现在这三处收敛到相同模式：UnrealCSharp 让 interface 与普通函数共用 generator/registry；puerts 在 callable 期按“是否 interface function”映射到实现类函数，而不是 blanket reject；UnLua 把 debug 先收敛成 runtime primitive，再由外层工具决定 UI。
- 当前工作区未找到 `Todo.md`，因此本轮优先级按用户显式给定的主线 `P10 UInterface / Bind API GAP 补齐` 判定：`D1`、`D3` 仍是 `P0`，`D5` 是伴随实现落地的 `P1` 护栏。

### 差距矩阵（增量）

| 维度 | 当前主路径 | 总评等级 | 主要差距类型 | 优先级 |
| --- | --- | --- | --- | --- |
| D1 插件架构与模块划分 | `Preprocessor -> FAngelscriptClassDesc -> ClassGenerator` | 能力缺失 | 缺少统一 interface descriptor owner，完整声明被连续降成 raw string / `FName` / name-only `UFunction` | P0 |
| D3 Blueprint 交互 | `BlueprintCallableReflectiveFallback` + interface `ProcessEvent` 路径 | 能力缺失 | interface-owner `UFunction` 被 blanket reject，正向 callable path 不存在 | P0 |
| D5 调试与开发体验 | `DebugServer::SendDebugDatabase` + `GoToDefinition` + live `asITypeInfo` | 实现差异 | symbol lifetime 绑定 live type table，缺少 cleanup / revision contract | P1 |

### [D1] 插件架构与模块划分：interface declaration owner 仍是 raw string 旁路，不是统一 descriptor

**当前状态**

当前 interface method 的 authoritative source 在插件内部被连续降维三次：`Preprocessor` 先把接口体解析成 `InterfaceMethodDeclarations` 的 raw string 数组；随后 `RegisterInterfaceMethodSignature()` 只保存 `FName FunctionName`；最终 `ClassGenerator` 在 UE 反射层又只创建“同名、无参数、无返回值”的最小 `UFunction`。这说明当前 interface declaration 还不是一等 callable descriptor，而只是三条子链路各自消费的过渡文本。

```
[当前 interface declaration owner]
script interface body
  -> Preprocessor
     -> InterfaceMethodDeclarations[]        // raw declaration string
     -> RegisterInterfaceMethodSignature()   // FName only
  -> runtime generic callback
     -> CallInterfaceMethod()
        -> FindFunction(FunctionName)
  -> UE reflection shell
     -> minimal UFunction(name only)
```

[29] 当前 owner 的降维链在源码里是连续可见的：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h
// 函数: FInterfaceMethodSignature / FAngelscriptClassDesc
// 位置: 59-62, 1101-1109，interface method 的持久化载体
// ============================================================================
struct FInterfaceMethodSignature
{
	FName FunctionName;
	// ★ runtime payload 只保留函数名
};

bool bIsInterface = false;
TArray<FString> ImplementedInterfaces;
TArray<FString> InterfaceMethodDeclarations;
// ★ interface body 的 authoritative source 只是 raw string 数组
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp
// 函数: FAngelscriptPreprocessor::ProcessClass
// 位置: 1078-1123, 1147-1153，完整声明进入 runtime 前再次降维
// ============================================================================
ClassDesc->InterfaceMethodDeclarations.Add(NormalizeInterfaceMethodDeclaration(Trimmed));
// ★ 先把源码行落成标准化字符串

auto* Sig = Engine.RegisterInterfaceMethodSignature(FName(*MethodName));
// ★ 完整 MethodDecl 到这里已经只剩 MethodName

if (auto* PreviousSig = (FInterfaceMethodSignature*)ScriptFunc->GetUserData())
{
	Engine.ReleaseInterfaceMethodSignature(PreviousSig);
}
ScriptFunc->SetUserData(Sig, 0);
// ★ AS method userData 挂的还是 name-only payload
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 函数: CallInterfaceMethod / FAngelscriptClassGenerator::FinalizeClass
// 位置: 56-67, 2803-2830，runtime dispatch 与 UE 反射壳都继续沿用 name-only 语义
// ============================================================================
auto* Sig = (FInterfaceMethodSignature*)Generic->GetFunction()->GetUserData();
UFunction* RealFunc = Object->FindFunction(Sig->FunctionName);
// ★ 调用期只按名字找真实函数

for (const FString& MethodDecl : InterfaceDesc->InterfaceMethodDeclarations)
{
	// For now, create a minimal UFunction with just the name
	UFunction* NewFunction = NewObject<UFunction>(NewClass, *FuncName, RF_Public);
	NewFunction->FunctionFlags = FUNC_Event | FUNC_BlueprintEvent | FUNC_Public;
	NewFunction->ReturnValueOffset = MAX_uint16;
	// ★ UE 反射层拿到的也是 name-only stub
}
```

**差距描述**

- `能力缺失`：当前没有一份能跨 `Preprocessor / runtime dispatch / UE reflection / docs/debug` 复用的 interface callable descriptor。接口方法虽然有完整声明文本，但没有稳定的结构化 owner。
- `实现质量差异`：同一份 interface method ABI 在进入 engine 之前被丢失了两次。结果不是“元数据少一点”，而是 `callable`、`UFunction`、`IDE` 三条链天然不可能对同一个签名达成一致。

**参考方案**

最值得借鉴的是 UnrealCSharp 的“interface 和普通函数共用 generator core”，而不是继续追加一条 interface 特例旁路。参考：

- `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicInterfaceGenerator.cpp:315-322`
- `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGeneratorCore.cpp:945-1015`
- `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FClassRegistry.cpp:77-103`

[30] UnrealCSharp 的 interface generator 直接复用统一 function generator，而不是把接口方法存成 raw string：

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicInterfaceGenerator.cpp
// 函数: FDynamicInterfaceGenerator::GeneratorFunction
// 位置: 315-322，interface 直接接入统一 generator core
// ============================================================================
void FDynamicInterfaceGenerator::GeneratorFunction(const FClassReflection* InClassReflection, UClass* InClass)
{
	FDynamicGeneratorCore::GeneratorFunction(FDynamicGeneratorCore::UInterfaceToIInterface(InClassReflection),
	                                         InClass,
	                                         [](FMethodReflection* InMethodReflection, const UFunction* InFunction)
	                                         {
		                                         InFunction->SetInternalFlags(EInternalObjectFlags::Native);
	                                         });
	// ★ interface function 不走字符串旁路，直接进入统一生成流程
}

// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGeneratorCore.cpp
// 函数: FDynamicGeneratorCore::GeneratorFunction
// 位置: 954-1012，参数、返回值、flags 都在同一条生成链里完成
// ============================================================================
for (const auto& [Pair, Method] : InClassReflection->GetMethods())
{
	auto Function = NewObject<UFunction>(InClass, FName(Pair.Key), RF_Public | RF_Transient);

	if (const auto Return = Method->GetReturn())
	{
		if (const auto Property = FTypeBridge::Factory<true>(Return, Function, "", RF_Public | RF_Transient))
		{
			Property->SetPropertyFlags(CPF_Parm | CPF_OutParm | CPF_ReturnParm);
			Function->AddCppProperty(Property);
		}
	}

	for (auto Index = Method->GetParamCount() - 1; Index >= 0; --Index)
	{
		const auto Property = FTypeBridge::Factory<true>(
			Params[Index]->GetReflectionType(),
			Function,
			FName(Params[Index]->GetName()),
			RF_Public | RF_Transient);
		Property->SetPropertyFlags(CPF_Parm);
		Function->AddCppProperty(Property);
	}

	Function->Bind();
	Function->StaticLink(true);
	InClass->AddFunctionToFunctionMap(Function, FName(Method->GetName()));
	// ★ generator core 自己持有完整 method shape
}
```

**吸收建议**

- 在插件内新增 `FAngelscriptInterfaceMethodDesc`，或直接把 `FInterfaceMethodSignature` 升级为可跨链复用的 `FAngelscriptCallableDesc` 轻量 handle。字段至少应包含 `FunctionName`、`ArgumentTypes`、`ReturnType`、`Qualifier`、`OwnerKind`。
- `InterfaceMethodDeclarations` 不要再承担 authority，只保留为调试/兼容字段。真正的 owner 应该是 descriptor 数组，由 `Preprocessor` 一次构建，`runtime dispatch`、`ClassGenerator`、`Docs/DebugDatabase` 全部消费这同一份对象。
- `ClassGenerator` 第一阶段不必改引擎，也不必升级 `AngelScript 2.33.0 WIP`；只要在插件内用新 descriptor 驱动 interface `UFunction` 壳体生成，并保留旧 `FindFunction(FunctionName)` fallback，即可先把 authority 立起来。

**优先级**

- `P0`
- 理由：这是 `P10 UInterface` 的根 owner 问题。只要 interface declaration 还是 raw string 旁路，后续 `FInterfaceProperty`、正向 callable、UHT 量化、debug 可观测性都会继续在不同链路里各修各的。

### [D3] Blueprint 交互：reflective fallback 仍把 interface owner 当作 blanket reject，正向 callable 永远进不来

**当前状态**

当前 `BlueprintCallableReflectiveFallback` 的 eligibility 判断在 owner class 带 `CLASS_Interface` 时直接返回 `RejectedInterfaceClass`，`BindBlueprintCallableReflectiveFallback()` 随即短路退出。更关键的是，自动化还把 `UGameplayTagAssetInterface::HasMatchingGameplayTag` 被拒绝视为正确行为。这意味着当前 contract 不是“暂未优化”，而是明确声明“interface-owner function 不走正向 callable path”。

```
[当前 interface callable 判定链]
UFunction (owner = interface)
  -> EvaluateReflectiveFallbackEligibility()
     -> RejectedInterfaceClass
        -> ShouldBind... == false
           -> BindBlueprintCallableReflectiveFallback() 直接退出
              -> 脚本侧没有通用正向 callable entry
```

[31] 当前 callable gate 是 owner-based blanket reject，而不是 signature-based capability gate：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.h
// 函数: EAngelscriptReflectiveFallbackEligibility
// 位置: 12-20，eligibility 分类
// ============================================================================
enum class EAngelscriptReflectiveFallbackEligibility : uint8
{
	Eligible,
	RejectedNullFunction,
	RejectedMissingOwningClass,
	RejectedInterfaceClass,
	RejectedCustomThunk,
	RejectedTooManyArguments,
};
// ★ interface class 是一级拒绝原因，而不是参数/返回值 family 的推导结果

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp
// 函数: EvaluateReflectiveFallbackEligibility / BindBlueprintCallableReflectiveFallback
// 位置: 254-287, 374-385
// ============================================================================
if (OwningClass->HasAnyClassFlags(CLASS_Interface))
{
	return EAngelscriptReflectiveFallbackEligibility::RejectedInterfaceClass;
}

if (!ShouldBindBlueprintCallableReflectiveFallback(Function))
{
	return false;
}
// ★ interface function 在真正看签名前就已经被整体挡掉
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptBlueprintCallableReflectiveFallbackTests.cpp
// 函数: FAngelscriptBlueprintCallableReflectiveFallbackEligibilityTest::RunTest
// 位置: 230-245，回归把 interface reject 写成正确行为
// ============================================================================
const UFunction* InterfaceFunction = UGameplayTagAssetInterface::StaticClass()->FindFunctionByName(TEXT("HasMatchingGameplayTag"));

TestEqual(
	TEXT("Reflective fallback should reject interface-class functions explicitly"),
	EvaluateReflectiveFallbackEligibility(InterfaceFunction),
	EAngelscriptReflectiveFallbackEligibility::RejectedInterfaceClass);
// ★ 当前自动化在守护“拒绝”而不是“可调用”
```

**差距描述**

- `能力缺失`：当前没有通用正向 interface callable path。即使后续补齐 `FInterfaceProperty`、`TScriptInterface<>` 或 native interface 自动注册，interface-owner `UFunction` 依旧无法经由 reflective fallback 进入脚本主调用链。
- `实现方式不同`：当前 eligibility 是按 owner class 一刀切，而不是按参数/返回值 family 决定“这条接口函数是否真的不可桥接”。结果是“接口”这个语义标签本身被当成拒绝理由。

**参考方案**

最值得吸收的是 puerts 的“interface function 在调用期映射到实现类函数”，而不是“见到 interface owner 就退回特例路径”。参考：

- `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp:250-271`
- `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1300-1326`
- `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:627-630`

[32] puerts 不是 blanket reject interface function，而是把 interface 当成正常 callable/property family 处理：

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/FunctionTranslator.cpp
// 函数: FFunctionTranslator::Call
// 位置: 250-271，interface function 在调用期映射到实现类函数
// ============================================================================
TWeakObjectPtr<UFunction> CallFunction =
	!IsInterfaceFunction ? Function : (CallObject->GetClass()->FindFunctionByName(Function->GetFName()));

if (!CallFunction.IsValid())
{
	CallFunction = CallObject->GetClass()->FindFunctionByName(FunctionName);
	Init(CallFunction.Get(), false);
}
// ★ interface 不是拒绝理由；真正调用时按对象实际类去解析函数
```

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp
// 函数: FTypeScriptDeclarationGenerator::GenClass
// 位置: 1300-1326，class 与 interface 方法由同一个 producer 决定是否进入产物
// ============================================================================
FunctionOutputs& Outputs = GetFunctionOutputs(Class);
for (TFieldIterator<UFunction> FunctionIt(Class, EFieldIteratorFlags::ExcludeSuper); FunctionIt; ++FunctionIt)
{
	if (!GenFunction(TmpBuff, *FunctionIt))
	{
		continue;
	}
	TryToAddOverload(Outputs, FunctionIt->GetName(), (FunctionIt->FunctionFlags & FUNC_Static) != 0, TmpBuff.Buffer);
}

for (int i = 0; i < Class->Interfaces.Num(); i++)
{
	for (TFieldIterator<UFunction> FunctionIt(Class->Interfaces[i].Class, EFieldIteratorFlags::IncludeSuper); FunctionIt; ++FunctionIt)
	{
		if (!GenFunction(TmpBuff, *FunctionIt))
		{
			continue;
		}
		TryToAddOverload(Outputs, FunctionIt->GetName(), (FunctionIt->FunctionFlags & FUNC_Static) != 0, TmpBuff.Buffer);
	}
}
// ★ interface function 先进入同一 callable producer，再谈下游调用
```

**吸收建议**

- 不要再把 `RejectedInterfaceClass` 作为 blanket rule。第一阶段就可以把它收缩成“owner 是 interface 时改走 descriptor-based resolve”，只有 `CustomThunk`、参数 family 未支持、参数个数超限等真实技术约束才拒绝。
- 在现有 `InvokeReflectiveUFunctionFromGenericCall()` 之上新增 interface owner 分支：先根据 descriptor/`UFunction` 名称在 `TargetObject->GetClass()` 上解析真正可调用函数，再沿现有参数打包逻辑执行。这样不需要改引擎，也不要求先把所有 interface family 一次性补完。
- 把现有 eligibility 测试改成双向矩阵：保留真正 unsupported 的 reject case，但新增“native interface function 在受支持 family 下应可绑定/可调用”的正向回归，避免 `P10` 后期继续被旧测试口径绑住。

**优先级**

- `P0`
- 理由：这是 `P10` 的直接用户面。如果这条路不打通，脚本侧拿到的仍然只是“能判断对象实现接口”，而不是“能稳定地通过接口引用调用函数”。

### [D5] 调试与开发体验：debug database 的 symbol lifetime 仍绑定 live type table，interface 删除后可能出现幽灵 symbol

**当前状态**

当前 interface type 在生成阶段直接把 `UClass*` 挂进 `asITypeInfo::UserData`；但删除阶段 `CleanupRemovedClass()` 只清 UE 侧 `UASClass` 状态，没有同步清理 engine-level `asITypeInfo`。与此同时，`SendDebugDatabase()` 会遍历 `ScriptEngine->GetObjectTypeCount()` 并读取每个 `ScriptType->GetUserData()`，`GoToDefinition` 也直接按 `GetTypeInfoByName(TypeName)` 取方法。基于源码推断，这意味着 interface 删除/重载后，IDE 看到的 symbol source 可能仍是旧 type table，而不是本次 reload 后的稳定 manifest。

```
[当前 D5 symbol lifetime]
interface register
  -> asITypeInfo::SetUserData(UClass*)

remove / reload
  -> CleanupRemovedClass()
     -> 清 UE 侧 UASClass / ScriptTypePtr
     -> 没有清 engine-level asITypeInfo::UserData

IDE request
  -> SendDebugDatabase()
     -> GetObjectTypeByIndex() / GetUserData()
  -> GoToDefinition()
     -> GetTypeInfoByName(TypeName)

结果：可能出现旧 interface symbol 继续可见
// ★ 这里是基于源码路径做出的推断，当前仓库暂无直接回归证明
```

[33] 当前 interface register / cleanup / debug 消费链是断开的：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 函数: FAngelscriptClassGenerator::CreateFullReloadClass / CleanupRemovedClass
// 位置: 2588-2612, 4990-5037，register 会写 userData，cleanup 不会清它
// ============================================================================
if (ScriptType != nullptr)
	ScriptType->SetUserData(NewClass);

if (ClassDesc->bIsInterface && ScriptType == nullptr)
{
	asITypeInfo* InterfaceScriptType = Engine.Engine->GetTypeInfoByName(TCHAR_TO_ANSI(*InterfaceName));
	if (InterfaceScriptType != nullptr)
	{
		InterfaceScriptType->SetUserData(NewClass);
		ClassDesc->ScriptType = InterfaceScriptType;
	}
}
// ★ interface register 会把 UClass* 挂到 engine-level type 上

void FAngelscriptClassGenerator::CleanupRemovedClass(TSharedPtr<FAngelscriptClassDesc> ClassDesc)
{
	UASClass* Class = (UASClass*)ClassDesc->Class;
	if (Class != nullptr)
	{
		Class->ScriptTypePtr = nullptr;
		Class->ConstructFunction = nullptr;
		Class->DefaultsFunction = nullptr;
		// ...
	}
}
// ★ cleanup 只清 UE 侧对象，没有对 ClassDesc->ScriptType / asITypeInfo 调 SetUserData(nullptr)
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 函数: FAngelscriptDebugServer::SendDebugDatabase / HandleMessage(GoToDefinition)
// 位置: 1707-1718, 1885-1895, 1319-1333，调试侧直接消费 live type table
// ============================================================================
int32 TypeCount = ScriptEngine->GetObjectTypeCount();
for (int32 TypeIndex = 0; TypeIndex < TypeCount; ++TypeIndex)
{
	auto* ScriptType = ScriptEngine->GetObjectTypeByIndex(TypeIndex);
	FString TypeName = ANSI_TO_TCHAR(ScriptType->GetName());
	// ★ DebugDatabase 按当前 engine type table 遍历 symbol
}

if (ScriptType->GetFlags() & asOBJ_REF)
{
	UClass* UnrealClass = (UClass*)ScriptType->GetUserData();
	if (UnrealClass)
	{
		const FString& Keywords = UnrealClass->GetMetaData(NAME_ScriptKeywords);
	}
}
// ★ 文档/关键字等元数据直接依赖 userData 指向的 UClass*

asITypeInfo* TypeInfo = Engine->GetTypeInfoByName(TCHAR_TO_ANSI(*GoTo.TypeName));
if (TypeInfo != nullptr && ScriptFunction == nullptr)
{
	for (int32 i = 0; i < Methods; ++i)
	{
		asIScriptFunction* Method = TypeInfo->GetMethodByIndex(i);
		if (FCStringAnsi::Strcmp(Method->GetName(), AnsiSymbol.Get()) == 0)
		{
			ScriptFunction = Method;
			break;
		}
	}
}
// ★ GoToDefinition 同样先信任 live type table
```

**差距描述**

- `实现差异`：当前协议 framing 与 database settings 已经是正式 contract，但 symbol lifetime 不是 registry-owned，而是隐式绑定在 live `asITypeInfo` 上。这使调试协议很强，symbol authority 却不够稳定。
- `实现质量差异`：删除和重载后的 symbol 没有 `Deleted / Stale / RevisionMismatch` 之类的显式状态。IDE 只能拿“当前还能不能从 type table 找到东西”当真相，这对 `P10` 的验证非常危险。
- `推断说明`：这里关于“幽灵 symbol”的判断，基于 `register 写入 userData -> cleanup 不清 -> debug/go-to-definition 读取 live type table` 这一组源码路径推导得出，当前仓库暂无直接自动化证明。

**参考方案**

最值得吸收的不是放弃当前协议，而是把 symbol 生命周期从“live type table 隐式状态”提升为“registry 显式状态”，并把 runtime debug primitive 与 symbol catalog 分开。参考：

- `Reference/UnrealCSharp/Source/UnrealCSharp/Public/Registry/FClassRegistry.h:19-27,52-60`
- `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FClassRegistry.cpp:115-174`
- `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDebugBase.h:84-94`
- `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaDebugBase.cpp:614-722`

[34] 参考实现更强调“显式 registry + 独立 debug primitive”：

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Public/Registry/FClassRegistry.h
// 函数: FClassRegistry
// 位置: 19-27, 52-60，class/function/property descriptor 的 owner 明确在 registry
// ============================================================================
FClassDescriptor* GetClassDescriptor(const UStruct* InStruct) const;
FClassDescriptor* AddClassDescriptor(UStruct* InStruct);
void RemoveClassDescriptor(const UStruct* InStruct);
void RemoveFunctionDescriptor(uint32 InFunctionHash);

TMap<TWeakObjectPtr<const UStruct>, FClassDescriptor*> ClassDescriptorMap;
TMap<uint32, FPropertyDescriptor*> PropertyDescriptorMap;
TMap<uint32, std::tuple<FClassDescriptor*, UFunction*>> UnrealFunctionHashMap;
// ★ symbol lifetime 不是靠 live type table 猜，而是 registry 显式管理

// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FClassRegistry.cpp
// 函数: FClassRegistry::RemoveClassDescriptor / RemoveFunctionDescriptor
// 位置: 115-174，descriptor 删除路径
// ============================================================================
if (const auto FoundClassDescriptor = ClassDescriptorMap.Find(InStruct))
{
	delete *FoundClassDescriptor;
	ClassDescriptorMap.Remove(InStruct);
}

if (const auto FoundFunctionDescriptor = FunctionDescriptorMap.Find(InFunctionHash))
{
	delete *FoundFunctionDescriptor;
	FunctionDescriptorMap.Remove(InFunctionHash);
	CSharpFunctionHashMap.Remove(InFunctionHash);
	UnrealFunctionHashMap.Remove(InFunctionHash);
}
// ★ 删除是显式动作，不依赖外层 IDE 自己猜 live state
```

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDebugBase.h
// 函数: GetStackVariables / GetLuaCallStack
// 位置: 84-94；以及 Private/UnLuaDebugBase.cpp:614-722
// ============================================================================
UNLUA_API bool GetStackVariables(lua_State *L, int32 StackLevel, TArray<FLuaVariable> &LocalVariables, TArray<FLuaVariable> &Upvalues, int32 Level = MAX_int32);
UNLUA_API FString GetLuaCallStack(lua_State *L);
// ★ runtime 先暴露稳定的调试 primitive，再由宿主 IDE/plugin 决定如何展示
```

**吸收建议**

- 先在插件内修补生命周期最短板的部分：`CleanupRemovedClass()` 如果 `ClassDesc->ScriptType` 仍有效，应主动 `SetUserData(nullptr)`，并为 interface/class/delegate 统一引入 `SymbolRevision` 或等价版本号；这一步不需要改调试协议主体，也不需要改引擎。
- `SendDebugDatabase()` 与 `GoToDefinition` 不要只信 live `asITypeInfo`。短期可先增加轻量 `SymbolCatalog` / `CapabilityLedger` 适配层：symbol 是否存在、是否被删除、是否 revision mismatch，都从 catalog 给出显式状态；调试协议只负责序列化这份状态。
- 维持当前自定义协议，不引入新的外部 debugger 作为前置条件。对 `P10` 来说，关键不是协议换代，而是让 IDE 看到的 interface symbol 与本次 reload 真正一致。
- 补一条 delete/reload 调试回归：创建 interface -> 请求 `DebugDatabase` -> 删除/重载 interface -> 再次请求 `DebugDatabase` + `GoToDefinition`，断言 symbol 要么消失，要么明确带 `Deleted/Stale` 状态，而不是静默沿用旧类型。

**优先级**

- `P1`
- 理由：这不是 `P10` 的第一功能块，但它是实现过程中的关键护栏。没有稳定 symbol lifetime，`UInterface` 的真实进展会被 IDE cache 和旧 type table 混淆，开发者很难判断当前问题到底在 runtime、reload 还是工具链。

### 值得吸收的设计模式（增量）

- `Descriptor-first interface owner`：interface method 应像普通函数一样先有统一 descriptor，再让 `runtime dispatch`、`UFunction`、`docs/debug` 去消费；不要让 raw string 成为长期 authority。
- `Eligibility by capability family, not by owner label`：正向 callable 的 gate 应以参数/返回值 family 为中心，而不是见到 `CLASS_Interface` 就整类拒绝。
- `Registry-owned symbol lifetime`：调试协议可以继续自定义，但 symbol 的存在、删除、revision 必须有显式 registry/catalog owner，不能完全依赖 live type table。

### 改进路线建议（增量）

1. `P0`：沿 `D1 + D2` 先落一版 `FAngelscriptInterfaceMethodDesc / FAngelscriptCallableDesc`，把 `InterfaceMethodDeclarations` 降为兼容字段，让 interface declaration 首先拥有稳定 owner。
2. `P0`：沿 `D3` 收缩 `RejectedInterfaceClass` blanket rule，新增“interface owner 解析到实现类函数”的 reflective path，只对真实 unsupported family 保留拒绝。
3. `P1`：把现有 `D6` 的 `Status / Reason / SymbolKey` 扩展到 `DebugDatabase / GoToDefinition`，并在 `CleanupRemovedClass()` 上补 `SetUserData(nullptr)` 与 `SymbolRevision`，让工具侧先停止产生幽灵 symbol。
4. `P1`：补一组跨 `callable / reload / debug` 的 parity regression，覆盖“interface 正向调用”、“删除后 symbol 不残留”、“同一 symbol 在 runtime 与 IDE 侧共享相同 revision/status”。

---
## 深化分析 (2026-04-08 19:52:21)

> **说明**: 工作区本轮未定位到 `todo.md`；以下优先级按 `Documents/Plans/Plan_CppInterfaceBinding.md`、`Documents/AutoPlans/DiscoveryPlans/ClassGenerator_Plan.md` 与 `Documents/AutoPlans/ArchitectureReview/TypeSystem_ArchReview.md` 中已经显式写出的 `P10 UInterface / Bind API GAP` 主线对齐。

### 本轮新增关键发现

- `ImplementedInterfaces` 在预处理阶段会记录 interface 声明里的第二、第三继承项，但 `FinalizeClass()` 只对 `!bIsInterface` 物化接口图，导致 `interface IChild : IParent, IOther` 这类 secondary interface 边会被静默丢弃。
- `ImplementedInterfaces` 的变更目前同时落在 `AreFlagsEqual()`、`ShouldFullReload()` 和现有测试口径三处，但三者判定不对称：分析阶段只是 `FullReloadSuggested`，执行阶段只看“新类当前还有没有接口”，测试阶段又把 interface hot reload 锁在 full reload path。
- `Interface/*` 回归集当前主要覆盖 happy path：非空 `Cast`、`ImplementsInterface` 成功、单继承链、full-reload hot reload。参考插件已经把 interface 相关缺陷固化成 issue-scoped instant test，当前仓库还没有等价的负向锁定面。

### 差距矩阵（增量）

| 维度 | 新增结论 | 差距类型 | 最佳参考 | 优先级 |
| --- | --- | --- | --- | --- |
| D2 反射绑定机制 | script interface 的 secondary interface 边未 materialize | 能力缺失 | UnrealCSharp `GeneratorInterface` + `FClassGenerator` | P0 |
| D4 热重载 | `ImplementedInterfaces` delta 的分析/执行/测试口径不对称 | 实现质量差异 | UnrealCSharp `GeneratorInterface` + `ReInstance` | P1 |
| D9 测试基础设施 | interface regression 缺少 issue-scoped negative matrix | 实现质量差异 | UnLuaTestSuite issue regressions | P1 |

### [D2] 反射绑定机制：script interface 的次级接口边会在 materialization 阶段丢失

**当前状态**

- `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:800-819` 会把 inheritance clause 里第一个父类型后的名字全部写进 `ImplementedInterfaces`，因此 `interface IChild : IParent, IOther` 的 `IOther` 在描述数据层其实已经被捕获。
- 同文件 `:1014-1137` 随后会把整个 interface chunk 抹空，只保留 `SuperClass`、`ImplementedInterfaces` 和 `InterfaceMethodDeclarations` 等描述数据。
- `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2762-2797` 的 interface full reload 分支只负责 `SetSuperStruct(SuperClass)`；真正递归写入 `NewClass->Interfaces` 的逻辑只存在于 `FinalizeClass()` 的 `!ClassDesc->bIsInterface` 分支，见同文件 `:5059-5155,5189-5198`。
- 本轮定位到的传递接口回归仍停在单 super 链 `UIBaseChain -> UIMidChain -> UILeafChain`，见 `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceAdvancedTests.cpp:622-673`。这能证明 `GetSuperClass()` 递归有效，但覆盖不到 `, IOther` 这种 secondary interface。

```text
[Current Secondary Interface Flow]
interface IChild : IParent, IOther
├─ DetectClasses()
│  ├─ SuperClass = IParent
│  └─ ImplementedInterfaces += IOther
├─ Interface chunk blanked before AS compile
├─ DoFullReload(interface)
│  └─ SetSuperStruct(IParent)
└─ FinalizeClass()
   ├─ if !bIsInterface -> build NewClass->Interfaces
   └─ if bIsInterface -> early return

Result: IOther never enters InterfaceClass->Interfaces
```

[35] 预处理阶段已经抓到了 secondary interface，但后续 owner 仍停在描述数据：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp
// 函数: DetectClasses / interface preprocessing
// 位置: 800-819, 1014-1078
// ============================================================================
// First entry is the superclass (already captured), remaining are interfaces
for (int32 i = 1; i < InheritanceList.Num(); ++i)
{
	FString InterfaceName = InheritanceList[i].TrimStartAndEnd();
	if (InterfaceName.Len() > 0)
	{
		ClassDesc->ImplementedInterfaces.Add(InterfaceName);
		// ★ `interface IChild : IParent, IOther` 的 `IOther` 在这里已经被记下
	}
}

if (ClassDesc->bIsInterface)
{
	// ★ interface chunk 随后被整体擦空；后续只能依赖 ClassDesc 里保存的描述数据
	for (int32 Pos = 0; Pos < Chunk.Content.Len(); ++Pos)
	{
		if (Chunk.Content[Pos] != '\n' && Chunk.Content[Pos] != '\r')
			Chunk.Content[Pos] = ' ';
	}

	if (Trimmed.Len() > 0)
		ClassDesc->InterfaceMethodDeclarations.Add(NormalizeInterfaceMethodDeclaration(Trimmed));
}
```

[36] materialization 阶段只给普通 class 构建 `Interfaces` 图，interface 自己提前返回：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 函数: DoFullReload / FinalizeClass
// 位置: 2762-2797, 5059-5155, 5189-5198
// ============================================================================
else if (ClassData.NewClass->bIsInterface)
{
	// ★ interface full reload 这里只接直接 `SuperClass`
	UClass* SuperClass = InterfaceDesc->CodeSuperClass;
	if (SuperClass == nullptr)
		SuperClass = UInterface::StaticClass();
	NewClass->SetSuperStruct(SuperClass);
}

if (ClassDesc->ImplementedInterfaces.Num() > 0 && !ClassDesc->bIsInterface)
{
	// ★ 只有普通 class 才会递归把 `ImplementedInterfaces` 写进 `NewClass->Interfaces`
	TFunction<void(UClass*)> AddInterfaceRecursive = [&](UClass* InterfaceClass)
	{
		for (const FImplementedInterface& ParentImpl : InterfaceClass->Interfaces)
		{
			AddInterfaceRecursive(ParentImpl.Class);
		}
		NewClass->Interfaces.Add(ImplementedInterface);
	};
}

if (ClassDesc->bIsInterface)
{
	FinalizeObjectClass(ClassDesc);
	return; // ★ interface 分支在这里提前结束，secondary interface 边不会被 materialize
}
```

**差距描述**

- `能力缺失`：script-defined interface 当前只有“一条 `SuperClass` 链”能进入 UE 反射图，`ImplementedInterfaces` 中的 secondary interface 不会出现在 interface 自身的 `UClass::Interfaces` 里。
- `实现质量差异`：当前自动化只验证单 super 链，会让这条缺口长期隐藏在 happy path 之后。也就是说，不是“interface graph 已经完整，只差 native bind”，而是脚本侧 interface graph 本身还存在裁剪。

**参考方案**

- `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicClassGenerator.cpp:656-676`
- `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp:118-126,257-277`

[37] UnrealCSharp 把 interface graph 的 authority 放在 materialized `InClass->Interfaces` 上，而不是停在预处理字符串：

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicClassGenerator.cpp
// 函数: FDynamicClassGenerator::GeneratorInterface
// 位置: 663-676；配合 ScriptCodeGenerator/Private/FClassGenerator.cpp:118-126,257-277
// ============================================================================
for (const auto Interface : InClassReflection->GetInterfaces())
{
	if (const auto InterfaceClass = LoadClass<UObject>(nullptr, *InterfacePathName))
	{
		if (InterfaceClass != UInterface::StaticClass())
		{
			InClass->Interfaces.Emplace(InterfaceClass, 0, true);
			// ★ 先把完整 interface graph 写进 `Interfaces`
		}
	}
}

for (const auto& InInterface : InClass->Interfaces)
{
	for (TFieldIterator<UFunction> FunctionIterator(InInterface.Class, EFieldIteratorFlags::IncludeSuper,
		EFieldIteratorFlags::ExcludeDeprecated); FunctionIterator; ++FunctionIterator)
	{
		// ★ 下游 codegen/runtime 统一消费同一份 materialized graph
	}
}
```

**吸收建议**

- 从 `FinalizeClass()` 抽出统一的 `BuildImplementedInterfaceGraph()`，同时供 `class` 和 `interface` 两个分支使用；不要再让 interface graph 只在 `!bIsInterface` 路径里 materialize。
- 在 graph builder 开头显式 `NewClass->Interfaces.Reset()`，把“构建当前接口图”与“保留旧 reload 残留”彻底分开。
- 继续保留 `SuperClass` 作为 direct interface inheritance 的第一入口，但 secondary interface 一律从 `ImplementedInterfaces` 落到同一个 graph builder；这样不需要改引擎，也不需要先碰 AngelScript 2.33.0 runtime。
- 补一个最小回归：`interface IChild : IParent, IOther` + `class Foo : UObject, IChild`，断言 `Foo->ImplementsInterface()` 同时覆盖 `IChild / IParent / IOther`。

**优先级**

- `P0`
- 理由：这是 `P10 UInterface` 的基础语义洞，不是“后续优化项”。如果 interface graph 先天会丢边，后续 native `UInterface` 自动注册、`FInterfaceProperty`、callable 和 debug 观测都会建立在半真半假的接口图之上。

### [D4] 热重载：`ImplementedInterfaces` delta 的分析、执行与回归口径仍不对称

**当前状态**

- `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:1187-1198` 的 `AreFlagsEqual()` 已经把 `ImplementedInterfaces` 视为 class flag 的一部分。
- 但分析阶段只是把 flag 变化提升到 `FullReloadSuggested`，见 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:1318-1322`。
- 执行阶段 `ShouldFullReload()` 又只检查“新类当前是否还是 interface”或“新类当前是否仍有 `ImplementedInterfaces`”，见同文件 `:2081-2088`。因此 `implements IFoo -> remove IFoo` 这种“从 1 变 0”的 delta 没有对称保护。
- `DoSoftReload()` 会重链 property、defaults、component offset 与 `ScriptTypePtr`，见同文件 `:4113-4209`；本轮在该函数体内没有定位到 `Interfaces.Reset()` / `Interfaces.Empty()` / `Interfaces.Add()` 之类的接口表重建路径。
- 现有 interface hot reload 回归 `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceAdvancedTests.cpp:389-409` 明确把当前口径锁成“interface hot reload 走 full reload path”，并没有覆盖 `SoftReloadOnly` 下删除最后一个接口的分叉。

```text
[Current Interface Delta Reload Path]
old class: implements IFoo
new class: implements <none>
├─ AreFlagsEqual() -> false
│  └─ ReloadReq = FullReloadSuggested
├─ ShouldFullReload()
│  ├─ bIsInterface ? no
│  └─ New ImplementedInterfaces.Num() == 0 -> no
├─ DoSoftReload()
│  ├─ relink properties/defaults/script type
│  └─ no visible rebuild of Class->Interfaces
└─ stale interface metadata may survive
```

[38] `ImplementedInterfaces` 已经被分析阶段识别，但只被提升到 `Suggested`：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h
// 函数: FAngelscriptClassDesc::AreFlagsEqual
// 位置: 1187-1198
// ============================================================================
bool AreFlagsEqual(const FAngelscriptClassDesc& Other) const
{
	return bIsInterface == Other.bIsInterface
		&& ImplementedInterfaces == Other.ImplementedInterfaces;
	// ★ interface 集合变化已经被视为结构变化
}

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 函数: 分析阶段 reload 判定
// 位置: 1318-1322
// ============================================================================
if (!ClassData.NewClass->AreFlagsEqual(*ClassData.OldClass.Get()))
{
	if (ClassData.ReloadReq < EReloadRequirement::FullReloadSuggested)
		ClassData.ReloadReq = EReloadRequirement::FullReloadSuggested;
	// ★ 这里只是 `Suggested`，还没有把 interface delta 提升成硬性 full reload
}
```

[39] 执行阶段只看“新类现在还有没有接口”，soft reload 路径里看不到接口表重建：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 函数: ShouldFullReload / DoSoftReload
// 位置: 2081-2088, 4113-4209
// ============================================================================
bool FAngelscriptClassGenerator::ShouldFullReload(FClassData& Class)
{
	if (Class.NewClass->bIsInterface)
		return true;
	if (Class.NewClass->ImplementedInterfaces.Num() > 0)
		return true;
	// ★ `1 -> 0` 的 interface delta 在这里失去保护
}

void FAngelscriptClassGenerator::DoSoftReload(FModuleData& ModuleData, FClassData& ClassData)
{
	// ★ 这段路径会重链 property/defaults/script type
	Class->ScriptTypePtr = ScriptType;
	if (!ClassDesc->bPlaceable)
		Class->ClassFlags |= CLASS_NotPlaceable;
	// ★ 本轮在函数体内没有定位到 `Class->Interfaces` 的清空或重建逻辑
}
```

**差距描述**

- `实现质量差异`：analysis 认为 `ImplementedInterfaces` 变化是结构变化，execution 却没有把它当成硬性 full reload 条件；这使 `ReloadReq`、`ShouldFullReload()` 和真实 metadata 刷新范围之间出现了不一致。
- `实现质量差异`：现有自动化又把 interface hot reload 固定在 full reload path，导致最危险的 `SoftReloadOnly + remove last interface` 分叉一直没有被实际回归约束。

**参考方案**

- `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicClassGenerator.cpp:656-676`
- `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicInterfaceGenerator.cpp:271-310`

[40] UnrealCSharp 对 interface 变化的处理更接近“重建当前 graph + 显式刷新依赖者”：

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicClassGenerator.cpp
// 函数: FDynamicClassGenerator::GeneratorInterface
// 位置: 663-676
// ============================================================================
for (const auto Interface : InClassReflection->GetInterfaces())
{
	if (const auto InterfaceClass = LoadClass<UObject>(nullptr, *InterfacePathName))
	{
		InClass->Interfaces.Emplace(InterfaceClass, 0, true);
		// ★ 每次生成都从当前 reflection graph 重建 interface 列表
	}
}

// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicInterfaceGenerator.cpp
// 函数: FDynamicInterfaceGenerator::ReInstance
// 位置: 271-310
// ============================================================================
return InBlueprintGeneratedClass->ImplementsInterface(InClass);
...
FBlueprintEditorUtils::RefreshAllNodes(Blueprint);
FKismetEditorUtilities::CompileBlueprint(Blueprint, BlueprintCompileOptions);
// ★ interface 变化后，依赖蓝图会被显式刷新，而不是默默沿用旧 metadata
```

**吸收建议**

- 第一阶段直接收紧策略：只要 `ImplementedInterfaces` 有任意增删改，都提升为 `FullReloadRequired`，直到 soft path 真正拥有 `RebuildImplementedInterfaces()` 为止。
- 第二阶段若确实要保留 soft path，再引入显式 `RebuildImplementedInterfaces()` 或等价 helper，并补依赖蓝图/缓存的 refresh 通知；否则不要让 `SoftReloadOnly` 继续“看起来能过”。
- `FinalizeClass()` 的 interface graph builder 要先 `Reset()` 再构建，避免 future full reload 也继续累积旧条目。
- 增加 `SoftReloadOnly` 负向回归：`class Foo : UObject, IFoo` 首次成功后删除 `IFoo`，断言不会进入 `DoSoftReload()`；或者若故意走 soft path，也必须显式证明 `Foo->ImplementsInterface(UFoo::StaticClass()) == false`。

**优先级**

- `P1`
- 理由：这不是 `P10` 第一批“让 interface 能被绑定”的入口，但它是把 `P10` 做成可迭代工程能力的必要护栏。interface delta 一旦在 hot reload 上留下旧 metadata，开发者会很难判断当前失败到底来自绑定、reload 还是旧 class 状态。

### [D9] 测试基础设施：interface regression 仍缺少 issue-scoped negative matrix

**当前状态**

- `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceImplementTests.cpp:175-246` 主要验证 `ImplementsInterface(StaticClass())` 的 happy path。
- `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceCastTests.cpp:68-73,137-142,215-221` 覆盖的是非空 `Self` 上的 `Cast` 成功、失败和方法调用。
- `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceAdvancedTests.cpp:622-673` 覆盖单 super 链 inheritance；同文件 `:389-409` 则把 interface hot reload 的当前 contract 明确锁成 full reload path。
- 换句话说，本轮定位到的 interface test surface 仍然集中在“正向可用”；还没有等价的 issue-scoped regression 去锁 `secondary interface materialization`、`ImplementsInterface(nullptr / non-interface)`、`Cast<Interface>(null)`、`SoftReloadOnly remove-last-interface` 这些负向边界。

```text
[Current Interface Test Surface]
Interface tests
├─ ImplementsInterface happy path
├─ Cast success / fail on live object
├─ Method call through cast
├─ Single-chain inheritance
└─ Hot reload -> full reload path only

Missing negative matrix
├─ secondary interface on interface declaration
├─ invalid InterfaceClass / nullptr
├─ Cast<Interface>(null)
└─ SoftReloadOnly remove-last-interface
```

[41] 参考插件已经把 interface 问题固化成 issue-scoped instant regression，而不是只停在 feature happy path：

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Tests/Issue595Test.cpp
// 位置: 22-47，锁住“脚本无法调用 interface 函数”这个具体缺陷
// ============================================================================
struct FUnLuaTest_Issue595 : FUnLuaTestBase
{
	virtual bool SetUp() override
	{
		const auto Chunk = R"(
			local Obj = NewObject(UE.UIssue595Object)
			UE.UIssue595Interface.Test(Obj)
			return Obj:Test()
		)";
		UnLua::RunChunk(L, Chunk);
		RUNNER_TEST_EQUAL(1, (int32)lua_tointeger(L, -1));
		return true;
	}
};

// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Tests/Issue398Test.cpp
// 位置: 21-64，锁住 interface list roundtrip 缺陷
// ============================================================================
const auto Chunk = R"(
	local ResultArray = FunctionLibrary.Test(Array)
	Result = 0
	for i=1, 4 do
		if ResultArray:Get(i) ~= nil then
			Result = Result + 1
		end
	end
)";
RUNNER_TEST_EQUAL(Result, 4);
// ★ 不是“接口大功能能用”就算结束，而是把具体 bug 变成独立回归
```

**差距描述**

- `实现质量差异`：当前接口测试数量并不少，但主要是 feature confirmation；对 `P10` 来说，真正危险的是“边角条件恢复成旧行为”却没人立刻发现。
- `实现质量差异`：现有 hot reload 回归本身就在锁 full reload path，这会让 soft path 的 interface 漏洞自然脱离自动化视野。

**参考方案**

- `Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Tests/Issue595Test.cpp:22-47`
- `Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Tests/Issue398Test.cpp:21-64`
- `Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Tests/Issue562Test.cpp:25-60`

**吸收建议**

- 在 `Plugins/Angelscript/Source/AngelscriptTest/Interface/` 下增补一组 `Issue-*` 风格的 interface regression，不再把所有边角条件继续塞进“大场景学习测试”。
- 第一批最值得立刻落地的四条回归是：
- `Interface.SecondaryInterfaceMaterialization`
- `Interface.ImplementsInterfaceRejectsNullOrNonInterface`
- `Interface.CastNullObjectIsSafe`
- `Interface.SoftReloadRemoveLastInterface`
- 这些测试应尽量复用 `CompileScriptModule()` / `CompileModuleWithResult()` 这样的 memory-script fixture，保持用例最小且直接指向一个 defect；不要等 `P10` 全量完工后再统一补。

**优先级**

- `P1`
- 理由：它不直接增加功能面，但会决定 `P10` 后续每一步是否可验证。没有这组负向 regression，interface graph、reload 和 helper 语义会继续靠人工记忆维持。

### 值得吸收的设计模式（增量）

- `Materialized interface graph as authority`：`ImplementedInterfaces` 与预处理字符串只能当输入线索，真正的 authority 应该是统一构建出来的 `UClass::Interfaces` 图，再让 codegen、callable、reload、debug 去消费。
- `Delta-classified reload policy`：reload 判定应按“接口集合有没有变化”这种结构性 delta 分类，而不是等到执行阶段再根据“现在还有没有接口”做经验判断。
- `Issue-scoped negative regression`：对 interface 这种边界条件密集的能力，最稳的不是再堆场景测试，而是把每个具体 defect 固化成独立、最小、可复现的 instant regression。

### 改进路线建议（增量）

1. `P0`：先抽 `BuildImplementedInterfaceGraph()`，让 `class` 与 `interface` 共用同一条 graph materialization 路径，并在 builder 前统一 `Interfaces.Reset()`。
2. `P0`：补 `interface IChild : IParent, IOther` 的 secondary-interface regression，把 script-side interface graph 的真实性先锁住，再继续 native `UInterface` 与 `FInterfaceProperty` 主线。
3. `P1`：把任意 `ImplementedInterfaces` delta 统一提升为 `FullReloadRequired`；若后续确实要保留 soft path，再引入显式 `RebuildImplementedInterfaces()` 与 dependent refresh。
4. `P1`：按 `Issue-*` 风格补齐四条 interface negative regression，用最小 memory-script fixture 把 `null`、`non-interface`、`secondary interface`、`remove-last-interface` 这四个边界条件固化下来。

---

## 深化分析 (2026-04-08 20:01:38)

### 本轮新增关键发现

- `D2` 的新增问题不是“interface 还没注册”，而是 **同一个 interface type 的 owner 仍被 `Preprocessor` 与 `ClassGenerator` 分摊**。前者在编译前 `RegisterObjectType/RegisterObjectMethod`，后者在 materialization 时补 `UserData/StaticClass`，并在 `ScriptType == nullptr` 时再次注册；这让 type identity、method table 与 `UClass` 绑定不在同一个事务里。
- `D7` 的新增问题是 **editor/reload 工具链仍没有 interface pin family**。`BlueprintImpactScanner` 与 `ClassReloadHelper` 都只把 `Struct/Enum/Byte` 当成 pin-type owner；一旦 `P10` 把 `FInterfaceProperty/TScriptInterface<>` 带进蓝图变量或节点 pin，当前 impact scan 与 reload refresh 不会自动命中。
- `D6` 的新增问题是 **artifact producer 仍是隐藏式入口**。文档 dump 只能靠 `-dump-as-doc` 启动参数触发并直接退出，`DebugDatabase` 只能等 live client 请求；这与 UHT function table 一起形成三套不同生命周期的 producer，Bind API GAP 仍缺一条可稳定重放的统一产物链。

### 差距矩阵（增量）

| 维度 | 总评等级 | 本轮新增差距判定 | 对当前主线的关系 | 优先级 |
| --- | --- | --- | --- | --- |
| D2 反射绑定机制 | 实现质量差异 | interface type 注册与 `UClass` materialization 仍是双 owner | 直接影响 `P10 UInterface` 与后续 `Bind API GAP` 收口 | P0 |
| D6 代码生成与 IDE 支持 | 实现差异 | docs/debug artifact 不是一等 producer，缺统一 headless 入口 | 直接影响 `Bind API GAP` 的量化、CI 与 IDE 对账 | P1 |
| D7 编辑器集成 | 能力缺失 | interface pin family 未进入 impact scan / reload refresh 主链 | 不是 `P10` 第一刀，但会阻塞 interface 进入 Blueprint 工作流 | P2 |

### [D2] 反射绑定机制：interface type 仍由 `Preprocessor` 与 `ClassGenerator` 双重持有

**当前状态**

- `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:1089-1145` 在预处理阶段就为 script interface 执行 `RegisterObjectType(InterfaceName)`、`GetTypeInfoByName()` 与 `RegisterObjectMethod(CallInterfaceMethod)`。这一步发生在 `UClass` materialize 之前。
- `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2588-2639` 到 class materialization 阶段又根据 `ClassDesc->ScriptType` 与 `bIsInterface` 去补 `SetUserData(NewClass)`、`StaticClass` global；若 `ScriptType == nullptr`，还会再次 `RegisterObjectType(InterfaceName)`。
- 结果是同一个 interface 的 `asITypeInfo*`、method table、`UClass* userData`、`StaticClass()` global 并不由同一处 authority 负责；这与 `Documents/Plans/Plan_CppInterfaceBinding.md:9-11,21-24,100-106` 中“自动注册 + 自动方法注册”的目标相比，还差一层 owner 收口。

```text
[Current Interface Type Ownership]
Preprocessor
├─ Parse interface chunk                          // 读取声明与方法文本
├─ RegisterObjectType(InterfaceName)              // 先造 AS placeholder type
└─ RegisterObjectMethod(CallInterfaceMethod)      // 先挂 method table

ClassGenerator
├─ Create UASClass + CLASS_Interface              // 后生成 UE-side class shell
├─ if ScriptType == nullptr -> RegisterObjectType // legacy/fallback 再注册一次
├─ SetUserData(NewClass)                          // 再把 UClass 回填到 AS type
└─ Patch StaticClass global                       // 再补 module global
```

[42] 当前 preprocessor 先直接写入 engine-level interface type：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp
// 函数: 处理 UINTERFACE chunk 的预处理路径
// 位置: 1089-1139
// ============================================================================
// ★ 这里在模块编译前就直接向 AS engine 注册 interface type，
//   并把方法声明立即挂到 placeholder type 上。
auto& Engine = FAngelscriptEngine::Get();
FString InterfaceName = ClassDesc->ClassName;
int TypeId = Engine.Engine->RegisterObjectType(
	TCHAR_TO_ANSI(*InterfaceName),
	0,
	asOBJ_REF | asOBJ_NOCOUNT | asOBJ_IMPLICIT_HANDLE);

if (TypeId >= 0 || TypeId == asALREADY_REGISTERED)
{
	asITypeInfo* InterfaceScriptType = Engine.Engine->GetTypeInfoByName(TCHAR_TO_ANSI(*InterfaceName));
	if (InterfaceScriptType != nullptr)
	{
		ClassDesc->ScriptType = InterfaceScriptType;

		for (const FString& MethodDecl : ClassDesc->InterfaceMethodDeclarations)
		{
			int32 FuncId = Engine.Engine->RegisterObjectMethod(
				TCHAR_TO_ANSI(*InterfaceName),
				TCHAR_TO_ANSI(*MethodDecl),
				asFUNCTION(CallInterfaceMethod),
				asCALL_GENERIC,
				nullptr);
		}
	}
}
```

[43] 到 class generator 再补另一半 ownership：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 函数: CreateFullReloadClass
// 位置: 2588-2639
// ============================================================================
asITypeInfo* ScriptType = ClassDesc->ScriptType;
if (ScriptType != nullptr)
	ScriptType->SetUserData(NewClass); // ★ 预处理阶段产出的 type 到这里才知道真实 UClass

if (ClassDesc->bIsInterface && ScriptType == nullptr)
{
	// ★ 如果前面没有经过 preprocessor owner，这里会以 fallback 方式再注册一次
	FString InterfaceName = ClassDesc->ClassName;
	int TypeId = Engine.Engine->RegisterObjectType(
		TCHAR_TO_ANSI(*InterfaceName),
		0,
		asOBJ_REF | asOBJ_NOCOUNT | asOBJ_IMPLICIT_HANDLE);

	if (TypeId >= 0 || TypeId == asALREADY_REGISTERED)
	{
		asITypeInfo* InterfaceScriptType = Engine.Engine->GetTypeInfoByName(TCHAR_TO_ANSI(*InterfaceName));
		if (InterfaceScriptType != nullptr)
		{
			InterfaceScriptType->SetUserData(NewClass);
			ClassDesc->ScriptType = InterfaceScriptType;
		}
	}
}

// ★ StaticClass global 也在这里补，不和前面的 method registration 同事务
asCGlobalProperty* Prop = ScriptModule->scriptGlobals.FindFirst(
	TCHAR_TO_ANSI(*ClassDesc->StaticClassGlobalVariableName), Ns);
```

**差距描述**

- `实现质量差异`：当前 interface type 的 compile-time placeholder、runtime `asITypeInfo`、`UClass* userData` 与 module `StaticClass()` global 不是同一个 authority。owner 一旦分裂，reload、namespace 与 cleanup 就必须跨阶段对账。
- `实现差异`：当前不是“先建 descriptor，再由同一 owner 生成 runtime/codegen/tooling 产物”，而是“preprocessor 先改 engine，class generator 再补洞”。这和普通 class/type family 的中心化 owner 明显不同。
- `实现质量差异`：只要一条 legacy 或测试路径绕过 preprocessor，`ClassGenerator` fallback 就会变成第二个 producer；这会把 `Plan_CppInterfaceBinding.md` 想要的通用自动注册，继续做成“路径相关能力”。

**参考方案**

- `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Reflection/FReflectionRegistry.cpp:59-60` 先把 `TScriptInterface` generic type 注册进统一 registry。
- `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:266-268,415-430` 与 `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/ObjectProperty/FInterfacePropertyDescriptor.cpp:29-44` 再统一消费 `InterfaceClass`；不会出现“预处理器写一半、类生成器写一半”的双 owner。
- `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:144-149,328-333,720-722` codegen 也沿同一份 interface authority 输出 `TScriptInterface<>` 类型名、namespace 与 support 判定。

[44] UnrealCSharp 的 interface owner 是 registry/bridge 单链，而不是两阶段各写一半：

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Reflection/FReflectionRegistry.cpp
// 函数: FReflectionRegistry constructor
// 位置: 59-60
// ============================================================================
TScriptInterfaceClass = GetClass(
	COMBINE_NAMESPACE(NAMESPACE_ROOT, NAMESPACE_CORE_UOBJECT), GENERIC_T_SCRIPT_INTERFACE);
// ★ generic interface type 先进入统一 registry

// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp
// 函数: FTypeBridge::GetClass(const FProperty*) / GetClass(const FInterfaceProperty*)
// 位置: 266-268, 415-430
// ============================================================================
if (const auto InterfaceProperty = CastField<FInterfaceProperty>(InProperty))
{
	return GetClass(InterfaceProperty); // ★ property family 统一落到 interface branch
}

const auto FoundGenericClass = FReflectionRegistry::Get().GetTScriptInterfaceClass();
const auto FoundClass = FReflectionRegistry::Get().GetClass(InProperty->InterfaceClass);
return MakeGenericTypeInstance(FoundGenericClass, FoundClass);

// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/ObjectProperty/FInterfacePropertyDescriptor.cpp
// 函数: FInterfacePropertyDescriptor::Set
// 位置: 29-44
// ============================================================================
const auto Object = SrcMulti->GetObject();
Interface->SetObject(Object);
Interface->SetInterface(Object ? Object->GetInterfaceAddress(Property->InterfaceClass) : nullptr);
// ★ runtime marshaller 只消费同一个 InterfaceClass，不再另起 producer
```

**吸收建议**

- 先在插件内引入一份显式 `FAngelscriptInterfaceTypeAuthority`（或等价 descriptor registry），key 至少包含 `Namespace + InterfaceName`、`asITypeInfo*`、normalized method decls、`PendingUClass`、`StaticClassGlobalVariableName` 与 revision。
- 让 `Preprocessor` 降级为“解析者 + placeholder 申请者”：它可以调用 `EnsurePlaceholderType()` 满足 AS 2.33.0 在模块编译前需要可见 type 的约束，但不能再自己决定 method table 与最终 owner。
- 让 `ClassGenerator` 降级为“materialization/finalize 阶段消费者”：禁止它在主路径里再次 `RegisterObjectType`；它只通过 authority 回填 `UserData(NewClass)`、`StaticClass` 与 final method binding。`ScriptType == nullptr` 分支短期保留为 legacy 兼容，但要记录 warning，不能继续当 silent producer。
- 这条收口不要求修改 Unreal Engine，也不要求改 AngelScript third-party；仍可沿用现有 `RegisterObjectType` 占位策略，只是把双写路径压成一份 authority。

**优先级**

- `P0`
- 理由：`Documents/Plans/Plan_CppInterfaceBinding.md:21-24,82-106` 把“C++ UInterface 自动注册 + 自动方法注册”定义成当前主线；如果 type authority 继续双 owner，后续 `FInterfaceProperty`、namespace 与 cleanup 会继续在 runtime 之外扩散。

### [D7] 编辑器集成：`BlueprintImpact` / `ClassReloadHelper` 仍不认识 interface pin family

**当前状态**

- `Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp:44-57` 的 `MatchesPinType()` 只认 `PC_Struct`、`PC_Enum`、`PC_Byte`。
- 同文件 `:179-202,228-232` 的 `AnalyzeLoadedBlueprint()` 虽然有 `Symbols.Classes` / `Symbols.Delegates`，但 pin 与变量层只通过 `MatchesPinType()` 判断；interface-typed pin/variable 没有命中路径。
- `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:55-80,208-225` 的 `ReplacePinType()` / `CheckRefresh()` 也只刷新 `Struct/Enum/Byte`。如果 interface graph 或 interface-typed variable 在 reload 后变化，当前 helper 不会自动替换或刷新节点。
- 补充证据是 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:812` 只剩一行 `//FInterfaceProperty` 注释，说明 editor 模块里还没有对应 family 的正式 owner。

```text
[Editor Interface Coverage Today]
BlueprintImpactScanner
├─ BuildImpactSymbols -> Classes / Structs / Enums / Delegates
├─ MatchesPinType -> Struct / Enum / Byte only
└─ AnalyzeLoadedBlueprint -> no interface pin / variable path

ClassReloadHelper
├─ ReplacePinType -> Struct / Enum / Byte only
└─ CheckRefresh -> Struct / Enum / Byte only
```

[45] 当前 Blueprint impact 与 reload refresh 都没有 interface pin 分支：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp
// 函数: MatchesPinType / AnalyzeLoadedBlueprint
// 位置: 44-57, 179-202, 228-232
// ============================================================================
bool MatchesPinType(const FEdGraphPinType& PinType, const FBlueprintImpactSymbols& Symbols)
{
	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
		return Symbols.Structs.Contains(Cast<UScriptStruct>(PinType.PinSubCategoryObject.Get()));

	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Enum || PinType.PinCategory == UEdGraphSchema_K2::PC_Byte)
		return Symbols.Enums.Contains(Cast<UEnum>(PinType.PinSubCategoryObject.Get()));

	return false; // ★ interface/object/class pin 当前全部掉出这里
}

if (Symbols.Classes.Contains(Cast<UClass>(Dependency)) || Symbols.Structs.Contains(Cast<UScriptStruct>(Dependency)))
	AddUniqueReason(OutReasons, EBlueprintImpactReason::NodeDependency);
// ★ node dependency 看 class/struct，但 pin 与 variable 仍只信 MatchesPinType()

if (MatchesPinType(Variable.VarType, Symbols))
	AddUniqueReason(OutReasons, EBlueprintImpactReason::VariableType);

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp
// 函数: ReplacePinType / CheckRefresh
// 位置: 55-80, 208-225
// ============================================================================
if (PinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
{
	// ★ 只替换 struct pin
}
else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Enum || PinType.PinCategory == UEdGraphSchema_K2::PC_Byte)
{
	// ★ 只刷新 enum/byte pin
}
else
{
	return false; // ★ interface pin reload 不会自动命中
}
```

**差距描述**

- `能力缺失`：interface-typed Blueprint variable、user pin、macro wildcard pin 不会产出 `PinType/VariableType` impact reason；变更扫描会低估 interface 改动影响面。
- `能力缺失`：reload patch 只修 struct/enum pin，interface pin 不会被替换或刷新；一旦 `P10` 让 interface 值真正进入蓝图 pin，这条空洞会直接变成手动刷新负担。
- `实现质量差异`：当前 editor tooling 不是 family-driven 设计。runtime 侧已经把 interface 当独立语义对待，editor 侧却仍把它当“未来再补”的旁路。

**参考方案**

- `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:148-155,179-183` 显式使用 `Blueprint->ImplementedInterfaces`，说明 editor owner 会直接处理 interface 集合，而不是只靠泛化 class fallback。
- `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSense.cpp:392-395` 与 `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:891-895` 都把 `FInterfaceProperty` 作为 editor/tooling 的一等分支，而不是让它落入 object/unknown fallback。

[46] 参考插件的 editor/tooling 路径会显式认识 interface family：

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSense.cpp
// 函数: IntelliSense::GetTypeName
// 位置: 392-395
// ============================================================================
if (CastField<FInterfaceProperty>(Property))
{
	const UClass* Class = ((FInterfaceProperty*)Property)->InterfaceClass;
	return FString::Printf(TEXT("TScriptInterface<%s%s>"), Class->GetPrefixCPP(), *Class->GetName());
	// ★ editor/intellisense 直接给 interface property 一条独立分支
}

// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp
// 函数: FTypeScriptDeclarationGenerator::GenTypeDecl
// 位置: 891-895
// ============================================================================
else if (auto InterfaceProperty = CastFieldMacro<InterfacePropertyMacro>(Property))
{
	AddToGen.Add(InterfaceProperty->InterfaceClass);
	StringBuffer << GetNameWithNamespace(InterfaceProperty->InterfaceClass);
	// ★ declaration generator 也不会把 interface family 混成普通 object
}
```

**吸收建议**

- 给 `FBlueprintImpactSymbols` 增加显式 `Interfaces` 集合；`BuildImpactSymbols()` 在扫描 module classes 时把 `CLASS_Interface` 类型与其 `ImplementedInterfaces` 边都纳入影响图。
- 扩展 `MatchesPinType()`、`ReplacePinType()`、`CheckRefresh()`，至少覆盖 `PinSubCategoryObject` 指向 interface class 的 pin；若当前引擎 pin category 为 `PC_Interface`，则应把它作为 first-class case，而不是走 object fallback。
- 在 `BlueprintImpact` 与 `ClassReloadHelper` 各补一条 interface regression：`Blueprint` 持有 interface variable / macro pin，修改 interface 后必须被 scan 命中、reload 后节点必须自动 refresh。
- 这条工作建议放在 runtime `P10` 首刀之后做；但在放开 interface 进入 Blueprint pin 之前，必须先补齐，否则会把“绑定成功”做成“编辑器工作流不稳定”的半成品。

**优先级**

- `P2`
- 理由：它不是 `P10` 的首个 blocker，但会决定 interface family 能否安全进入 Blueprint 工作流。当前主线仍应先做 runtime/type/property authority；editor impact/reload 在 interface pin 真正暴露前完成即可。

### [D6] 代码生成与 IDE 支持：artifact producer 仍是隐藏式入口，不是一等工具链

**当前状态**

- `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:528` 只通过启动参数 `dump-as-doc` 打开文档 dump；`2224-2227` 调用 `FAngelscriptDocs::DumpDocumentation()` 后立刻 `RequestExit(false)`。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp:407-460` 的 `DumpDocumentation()` 是直接遍历 live script engine 构建 `.hpp` 文档，而不是显式 artifact job。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:1493-1515` 只有在 client 发 `RequestDebugDatabase` 后才序列化 live `DebugDatabase`。
- 这意味着当前至少有三种 producer lifetime：`UHT -> function table`、`runtime startup flag -> docs`、`live debug request -> ide db`。三者不共享同一条可重放的用户入口。

```text
[Current Artifact Producer Topology]
Build/UHT
└─ AngelscriptFunctionTableCodeGenerator           // 生成 AS_FunctionTable_*.cpp

Runtime startup
└─ -dump-as-doc -> DumpDocumentation() -> exit     // 隐藏式 headless docs path

Live debug session
└─ RequestDebugDatabase -> SendDebugDatabase()     // 运行期临时拼装 IDE schema
```

[47] 当前 docs 与 IDE 数据库都不是一等工具入口：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: RuntimeConfig parse / Initial compile tail
// 位置: 528, 2224-2227
// ============================================================================
Config.bDumpDocumentation = FParse::Param(FCommandLine::Get(), TEXT("dump-as-doc"));
// ★ docs dump 只能通过命令行参数打开

if (RuntimeConfig.bDumpDocumentation)
{
	FAngelscriptDocs::DumpDocumentation(Engine);
	FPlatformMisc::RequestExit(false); // ★ dump 完即退出，不是常规 artifact job
}

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 函数: FAngelscriptDebugServer::SendDebugDatabase
// 位置: 1493-1515
// ============================================================================
FAngelscriptDebugDatabaseSettings DebugSettings;
SendMessageToClient(Client, EDebugMessageType::DebugDatabaseSettings, DebugSettings);

auto SendTypes = [&]()
{
	FAngelscriptDebugDatabase DB;
	FJsonSerializer::Serialize(Root, JsonWriter);
	SendMessageToClient(Client, EDebugMessageType::DebugDatabase, DB);
	// ★ IDE schema 只在 live client 请求时临时生成
};
```

**差距描述**

- `实现差异`：当前 D6 产物不是通过显式 generator/service 产生，而是分别绑在启动参数与 live 调试请求上。对使用者来说，“怎么稳定生成同一份 docs/IDE artifact”仍是隐式知识。
- `实现质量差异`：headless docs path 以“dump 后退出”实现，适合一次性导出，不适合 CI、差异比较、或和 UHT summary 同次构建对账。
- `实现质量差异`：producer identity 隐藏会继续放大 `Bind API GAP` 的统计口径分裂。即使 D6 资产本身已经存在，也很难回答“这次生成到底是哪条链的真相”。

**参考方案**

- `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp:34-37` 与 `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp:29-115` 把 IntelliSense generator 同时暴露成 toolbar action 与 commandlet。
- `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1650-1687,1700-1705` 把 `GenUeDts` 绑定到 toolbar / console command / generator service；`469-505,891-895` 还让 namespace 与 interface property 进入同一份产物逻辑。

[48] 参考插件会把 artifact producer 做成显式、可重复调用的工具入口：

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/MainMenuToolbar.cpp
// 函数: FMainMenuToolbar::FMainMenuToolbar
// 位置: 34-37
// ============================================================================
CommandList->MapAction(FUnLuaEditorCommands::Get().GenerateIntelliSense, FExecuteAction::CreateLambda([]
{
	FUnLuaIntelliSenseGenerator::Get()->UpdateAll();
}));
// ★ editor 菜单显式触发同一份 generator

// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp
// 函数: UUnLuaIntelliSenseCommandlet::Main
// 位置: 29-115
// ============================================================================
int32 UUnLuaIntelliSenseCommandlet::Main(const FString &Params)
{
	const auto ExportedReflectedClasses = UnLua::GetExportedReflectedClasses();
	// ★ 省略 blacklist/enum/function 收集细节；核心是同一入口统一驱动生成流程
	for (auto Pair : ExportedReflectedClasses)
	{
		Pair.Value->GenerateIntelliSense(GeneratedFileContent);
		SaveFile(ModuleName, Pair.Key, GeneratedFileContent);
	}

	if (ParamsMap.Contains(TEXT("BP")) && ParamsMap[TEXT("BP")] == TEXT("1"))
	{
		auto Generator = FUnLuaIntelliSenseGenerator::Get();
		Generator->Initialize();
		Generator->UpdateAll();
	}
	return 0;
}
// ★ 同一份产物逻辑既能交互触发，也能 commandlet/headless 复用
```

**吸收建议**

- 在插件内引入显式 `FAngelscriptArtifactGenerator` 或 `UAngelscriptArtifactCommandlet`，首版只统一三类产物：`Docs`、`IdeManifest/DebugDatabase snapshot`、`BindingCoverage`。`-dump-as-doc` 退化成兼容别名。
- 把 `Docs::DumpDocumentation()` 与 `SendDebugDatabase()` 的“遍历 live engine 即时生成”改成“消费同一份 manifest + 可选 live delta”；这样 UHT summary、docs 与 IDE 才能共享 `SymbolKey/Reason/Revision`。
- 在 editor 侧补一个轻量入口即可，不必一开始就复制 puerts 全套模块：比如 `ToolMenus + console command + commandlet` 三选二，重点是让 CI 和人工对账都能走同一份 producer。
- 这条工作不要求改 Unreal Engine，也不需要等 AngelScript runtime 升级；优先在插件层把 producer contract 立住，就能立刻改善 `Bind API GAP` 的量化与回归可重复性。

**优先级**

- `P1`
- 理由：`Bind API GAP` 当前主线需要一条可机器对账的产物链；D6 不必先于 `P10` 的 runtime 绑定改动，但应在 interface family 开始扩张时同步落地，否则 gap 会继续靠人工比对三份不同生命周期的资产。

### 值得吸收的设计模式（增量）

- `Single authority, staged finalize`：允许 precompile 阶段先申请 placeholder，但 type identity、`UserData`、method table 与 `StaticClass` 最终只能由一份 authority 收口，不能让多个阶段各自成为 producer。
- `Family-driven editor tooling`：无论是 Blueprint impact、reload refresh 还是 IDE declaration，只要某个 family 已经在 runtime 成为一等类型，它就应在 editor/tooling 层也拥有显式分支。
- `Explicit artifact producers`：docs、IDE manifest、coverage 这类产物应由可重复调用的 generator/service 生产，而不是分散在启动参数、live request 或隐式 side effect 里。

### 改进路线建议（增量）

1. `P0`：先把 script interface 的 type authority 收到单点，禁止 `ClassGenerator` 在主路径里再次成为 `RegisterObjectType` producer；以兼容日志方式保留 legacy fallback。
2. `P1`：在同一轮里补一个最小 `ArtifactGenerator`，让 `docs/debug manifest/coverage` 共享 `SymbolKey + Reason + Revision`，把 `Bind API GAP` 量化从“人工比对”升级成“单次生成物对账”。
3. `P2`：等 `FInterfaceProperty/TScriptInterface<>` 真正进入 editor pin 之后，再把 `BlueprintImpact`、`ClassReloadHelper`、相关回归测试一起升级为 interface-aware；不要把这一步继续留到接口值已经广泛暴露之后。

---

## 深化分析 (2026-04-08 23:17:33)

### 本轮新增关键发现

- 当前 `D6` 的真正短板不是“没有 skipped ledger”，而是 **UHT 已经按 symbol 记 `FailureReason`，runtime bind registry 却只有 `bEnabled`**；一旦 `P10 UInterface` 继续推进，`Bind API GAP` 会先撞上 provenance 缺失，而不是先撞上文件产物缺失。
- 这不是简单的“参考插件更成熟”。相反，参考插件在 negative-space 诊断上大多只有 `bool cache`、`continue` 或 positive-only export；当前 Angelscript 的 UHT 账本反而更强。值得吸收的是它们的**单入口 contract**，不是它们更薄的诊断面。
- 当前测试也已经暴露出这条 split：`GeneratedFunctionTable` 测试校验 `FailureReason` 与 reason-summary 聚合，`DumpAll` 只校验 `BindRegistrations.csv` 文件存在，`BindConfig` 只校验 `bEnabled=false`。也就是说，**测试在保护 artifact 形状，但没有保护跨阶段语义一致性**。

### 差距矩阵（增量）

| 维度 | 当前观察点 | 总评等级 | 主要差距类型 | 优先级 |
| --- | --- | --- | --- | --- |
| D6 代码生成与 IDE 支持 | UHT `FailureReason` 完整，但 runtime `FBindInfo` 无 provenance | 实现差异 | runtime provenance 能力缺失 + taxonomy split | P1 |
| D9 测试基础设施 | UHT artifact 已做内容级回归，StateDump 仍停在文件级回归 | 实现质量差异 | cross-stage consistency regression 缺失 | P1 |

### [D6] 代码生成与 IDE 支持：当前优势是 reasonful ledger，真正缺口是 runtime provenance

**当前状态**

- `AngelscriptFunctionTableExporter` 在 UHT 阶段把“没生成出来的函数”正式写进 `AS_FunctionTable_SkippedEntries.csv` 和 `AS_FunctionTable_SkippedReasonSummary.csv`，不是简单 `continue` 掉。见 `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:27-44,75-87,99-161`。
- `AngelscriptFunctionSignatureBuilder` 与 `AngelscriptHeaderSignatureResolver` 已经形成一套稳定的 failure vocabulary，包括 `header-missing`、`class-range`、`declaration-missing`、`non-public`、`unexported-symbol`、`overloaded-unresolved`、`static-array-parameter`。见 `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs:43-90` 与 `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs:22-105`。
- 但 runtime 侧 `FAngelscriptBinds::FBindInfo` 只有 `BindName / BindOrder / bEnabled` 三个字段；`DumpBindRegistrations()` 虽然输出了 `BindModule` 和 `SkipReason` 列，实际既没有 module provenance，也只能把配置禁用写成 `DisabledBindNames`。见 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:431-436`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:176-183`、`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp:847-871`。

```
[Angelscript] Negative-Space Ownership Today
├─ UHT exporter
│  ├─ TryBuild() -> failureReason                  // 有结构化失败原因
│  ├─ AS_FunctionTable_SkippedEntries.csv          // 按 symbol 记账
│  └─ AS_FunctionTable_SkippedReasonSummary.csv    // 按 reason 聚合
├─ Runtime bind registry
│  └─ FBindInfo { name, order, enabled }           // 没有 module/provenance/reason
└─ StateDump
   └─ BindRegistrations.csv                        // 只能表达 DisabledBindNames
```

[49] 当前 UHT 已经把 negative space 做成正式 artifact，而不是 silent skip：

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs
// 函数: Export / CountBlueprintCallableFunctions / WriteSkippedReasonSummaryCsv
// 位置: 27-44, 75-87, 140-161
// ============================================================================
int generatedFileCount = AngelscriptFunctionTableCodeGenerator.Generate(factory);
// ★ 正向 function table 生成完成后，还会继续统计“哪些 BlueprintCallable 没进去”

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
	// ★ 每个 skipped function 都会带上 `FailureReason`
}

foreach (var reasonGroup in skippedEntries
	.GroupBy(static entry => entry.FailureReason, StringComparer.Ordinal)
	.OrderByDescending(static group => group.Count()))
{
	builder
		.Append(EscapeCsv(reasonGroup.Key))
		.Append(',')
		.Append(reasonGroup.Count())
		.Append("\r\n");
	// ★ 再把 reason 聚合成 `AS_FunctionTable_SkippedReasonSummary.csv`
}
```

[50] runtime 侧目前还没有同等级的 provenance 结构，因此 `StateDump` 无法解释语义性缺口：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h
// 结构: FAngelscriptBinds::FBindInfo
// 位置: 431-436
// ============================================================================
struct FBindInfo
{
	FName BindName;
	int32 BindOrder = 0;
	bool bEnabled = true;
	// ★ 没有 BindModule / SymbolKey / SkipReason / SourceStage
};

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp
// 函数: FAngelscriptBinds::GetBindInfoList
// 位置: 176-183
// ============================================================================
for (const FBindFunction& Bind : GetSortedBindArray())
{
	BindInfos.Add({Bind.BindName, Bind.BindOrder, !DisabledBindNames.Contains(Bind.BindName)});
	// ★ runtime 只保留“是否启用”，没有保留 provenance
}

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp
// 函数: FAngelscriptStateDump::DumpBindRegistrations
// 位置: 847-871
// ============================================================================
Writer.AddHeader({
	TEXT("BindName"),
	TEXT("BindModule"),
	TEXT("bIsSkipped"),
	TEXT("SkipReason")
});

Writer.AddRow({
	BindInfo.BindName.ToString(),
	FString(), // ★ BindModule 当前为空
	BoolToString(bIsSkipped),
	bIsSkipped && DisabledBindNames.Contains(BindInfo.BindName) ? FString(TEXT("DisabledBindNames")) : FString()
	// ★ SkipReason 只能表达“配置禁用”，无法表达 `unexported-symbol`、`header-missing` 等 build-time 语义
});
```

**差距描述**

- `能力缺失`：runtime bind registry 还没有 `BindModule / SymbolKey / SourceStage / SkipReason`。因此 `Bind API GAP` 只能在 UHT 侧回答“为什么没生成”，不能在 runtime 侧回答“为什么没注册/没生效”。
- `实现质量差异`：UHT `FailureReason` 与 runtime `SkipReason` 不是同一份 taxonomy。当前两条线都在记录 negative space，但无法对齐成“同一个 symbol 在不同阶段的同一条诊断链”。
- `实现方式不同`：参考插件大多没有当前 Angelscript 这么厚的 negative-space artifact。这里不能简单判成“参考实现更完整”；更准确的说法是 **Angelscript 的账本更强，但 owner 仍分裂**。

**参考方案**

- `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:813-873`：用 `SupportedMap` 把 support verdict 收到单入口；优点是 owner 单一，缺点是只有 `true/false`，没有当前 AS 这种 reason ledger。
- `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1164-1224`：对 unsupported function 多数直接 `continue`，只有 super overload 用 `@deprecated Unsupported super overloads.` 留局部痕迹；说明它的 contract 更轻，但不适合替代 AS 现有账本。
- `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp:56-114`：commandlet 只消费 positive export registry，未见对等的 skipped-reason 账本；优点是 producer 简单稳定。

[51] UnrealCSharp 的 support owner 很单，但只有 bool，不携带失败原因：

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp
// 函数: FGeneratorCore::IsSupported(const UClass*) / IsSupported(const UFunction*)
// 位置: 813-873
// ============================================================================
if (const auto FoundSupported = SupportedMap.Find(InClass))
{
	return *FoundSupported;
}

if (!IsSupported(InClass->GetPackage()))
{
	SupportedMap.Add(InClass, false);
	return false;
}

for (const auto& Interface : InClass->Interfaces)
{
	if (!IsSupported(Interface.Class))
	{
		SupportedMap.Add(InClass, false);
		return false;
	}
}

SupportedMap.Add(InClass, true);
// ★ owner 单一，但 verdict 只有 bool；开发者无法从产物里直接知道“为什么 false”
```

[52] puerts 与 UnLua 更接近“轻 contract”，能借鉴其单入口，不适合照搬其诊断厚度：

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp
// 函数: FTypeScriptDeclarationGenerator::GenFunctions / GenResolvedFunctions
// 位置: 1164-1224
// ============================================================================
if (!GenFunction(Tmp, Function, true, false, false, true))
{
	continue; // ★ 大多数 unsupported function 直接跳过
}

if (!Overloads.Contains(*SuperOverloadIter))
{
	Buff << "     * @deprecated Unsupported super overloads.\n";
	// ★ 只有局部场景会显式留痕
}

// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp
// 函数: UUnLuaIntelliSenseCommandlet::Main
// 位置: 56-114
// ============================================================================
for (auto Pair : ExportedReflectedClasses)
{
	if (ClassBlackList.Contains(Pair.Key))
		continue;

	Pair.Value->GenerateIntelliSense(GeneratedFileContent);
	SaveFile(ModuleName, Pair.Key, GeneratedFileContent);
}
// ★ 只消费 positive export registry；没有与当前 AS 对等的 skipped reason ledger
```

**吸收建议**

- 不要把当前 `AS_FunctionTable_SkippedEntries.csv` / `AS_FunctionTable_SkippedReasonSummary.csv` 降级成 `SupportedMap<bool>` 式缓存。应保留现有 reasonful ledger，把**需要吸收的部分限定为 owner 收口**。
- 首选插件内增量路线：在 `FAngelscriptBinds::FBindInfo` 或并行 sidecar 中补 `BindModule`、`SymbolKey`、`SourceStage`、`SkipReason`、`SkipReasonCode`，让 runtime/state dump/debugger 能消费同一份 provenance，而不是要求改 Unreal Engine 或 AngelScript third-party。
- 让 `AngelscriptFunctionSignatureBuilder` / `AngelscriptHeaderSignatureResolver` 输出的 `failureReason` 成为统一 taxonomy 的 seed；runtime 侧若没有精确等价项，也至少保留 `origin = config/runtime/uht` 和 `reason = DisabledBindNames / MissingTypeAuthority / MissingPropertyBridge / ...` 的层级，而不是退回纯布尔。
- 对 `P10 UInterface` 来说，第一批最值得接这条合同的 family 就是 `UInterface / FInterfaceProperty / TScriptInterface<>`。原因不是它们最容易，而是它们已经是当前 `Bind API GAP` 最需要可诊断的缺口。

**优先级**

- `P1`
- 理由：它不应挡住 `P10` 的第一刀绑定实现，但必须与 `Bind API GAP` 的量化工作并行推进；否则 interface family 一旦开始扩张，当前更强的 UHT ledger 会在 runtime 侧立刻断链。

### [D9] 测试基础设施：当前已经在保护 artifact，但还没保护跨阶段 vocabulary 一致性

**当前状态**

- `GeneratedFunctionTable` 测试已经做了内容级断言：要求 `AS_FunctionTable_SkippedEntries.csv` 头是 `ModuleName,ClassName,FunctionName,FailureReason`，且至少有一条非空 `FailureReason`；还要求 `AS_FunctionTable_SkippedReasonSummary.csv` 的聚合计数与 skipped entries 行数对齐。见 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp:683-749`。
- `DumpAll` 测试只验证 `BindRegistrations.csv` 等文件是否生成、`DumpSummary.csv` 是否有行和状态；并没有解析 `BindRegistrations.csv` 的 `SkipReason` 语义。见 `Plugins/Angelscript/Source/AngelscriptTest/Dump/AngelscriptDumpTests.cpp:25-32,45-75,202-250`。
- `BindConfig` 测试能证明 disabled bind 会反映为 `bEnabled=false`，但它同样停在布尔层，没有把 runtime skip 与 build-time reason ledger 接起来。见 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp:301-314`。

```
[Angelscript] Regression Coverage Split
├─ GeneratedFunctionTableTests
│  ├─ assert FailureReason column                 // UHT 内容级回归
│  └─ assert reason-summary aggregate             // UHT 聚合一致性
├─ DumpTests
│  ├─ assert CSV files exist                      // runtime 文件级回归
│  └─ assert DumpSummary status                   // runtime 状态级回归
├─ BindConfigTests
│  └─ assert bEnabled flips                       // runtime 布尔级回归
└─ Missing
   └─ one test that compares SymbolKey + Reason across UHT/runtime
```

[53] 当前测试已经很有价值，但 coverage 切面仍是分裂的：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp
// 函数: FAngelscriptGeneratedFunctionTableSkippedReasonSummaryCsvOutputTest::RunTest
// 位置: 683-749
// ============================================================================
TestEqual(TEXT("Generated function table skipped csv test should write the expected skipped csv header"),
	SkippedLines[0], TEXT("ModuleName,ClassName,FunctionName,FailureReason"));

TestTrue(TEXT("Generated function table skipped csv rows should include non-empty failure reasons"), bFoundFailureReason);
// ★ UHT 账本已经是内容级断言，不只是“文件存在”

TestEqual(TEXT("Generated function table skipped reason summary test should write the expected summary csv header"),
	SummaryLines[0], TEXT("FailureReason,SkippedCount"));
TestEqual(TEXT("Generated function table skipped reason summary test should keep aggregate counts aligned with the skipped entry csv"),
	SummedSkippedCount, SkippedLines.Num() - 1);
// ★ reason summary 和 per-entry skipped csv 已经要求计数一致

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Dump/AngelscriptDumpTests.cpp
// 函数: FAngelscriptStateDumpEndToEndTest::RunTest / FAngelscriptStateDumpSummaryTest::RunTest
// 位置: 202-250
// ============================================================================
for (const FString& ExpectedFilename : GetExpectedPhaseOneCsvFiles())
{
	TestTrue(*FString::Printf(TEXT("DumpAll should create '%s'"), *ExpectedFilename),
		IFileManager::Get().FileExists(*CsvPath));
}
// ★ runtime dump 目前只校验每张表“有没有写出来”

TestEqual(
	*FString::Printf(TEXT("'%s' should report the expected summary status"), *ExpectedFilename),
	SummaryRow->Value,
	GetExpectedSummaryStatus(ExpectedFilename));
// ★ 进一步校验 summary 状态，但没有深入 `BindRegistrations.csv` 的语义内容

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp
// 函数: FAngelscriptGlobalDisabledBindNamesTest::RunTest
// 位置: 301-314
// ============================================================================
const TArray<FAngelscriptBinds::FBindInfo> BindInfos = FAngelscriptBinds::GetBindInfoList(MergedDisabledBindNames);
...
TestFalse(TEXT("BindConfig.GlobalDisabledBindNames should report the disabled named bind as disabled"), NamedBindInfo->bEnabled);
// ★ 这里能证明 runtime bool 状态正确，但还证明不了 runtime reason 与 UHT reason 是否同源
```

**差距描述**

- `没有实现`：当前没有一条 regression 会拿同一个 `SymbolKey` 去同时对比 UHT skipped csv、future manifest 和 runtime state dump。
- `实现质量差异`：现有测试已经覆盖了“产物是否生成”“聚合计数是否一致”“disabled bind 是否翻转”，但没有覆盖“不同阶段是否在说同一件事”。
- `实现质量差异`：对于 `P10 UInterface` 这类 family 扩张工作，这种缺口会先表现成 silent drift，而不是 test 立即爆红。也就是 build 说 `unexported-symbol`，runtime 说 nothing，测试仍可能通过。

**参考方案**

- `Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Tests/IssueOverridesTest.cpp:25-58`：把一个真实问题绑定成 issue-named regression，打开具体地图、跑脚本、校验具体返回值。这种“问题编号 -> 真实场景 -> 自动化断言”的模式，适合拿来保护未来 `UInterface / FInterfaceProperty` 的跨阶段一致性。

[54] UnLua 值得借鉴的是 issue-scoped regression 组织方式，而不是替换现有 runner：

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Tests/IssueOverridesTest.cpp
// 函数: FIssueOverridesTest::RunTest
// 位置: 25-58
// ============================================================================
BEGIN_TESTSUITE(FIssueOverridesTest, TEXT("UnLua.Regression.IssueOverrides 覆写引起的问题"))

const auto MapName = TEXT("/UnLuaTestSuite/Tests/Regression/IssueOverrides/IssueOverrides.IssueOverrides");
ADD_LATENT_AUTOMATION_COMMAND(FOpenMapLatentCommand(MapName))
ADD_LATENT_AUTOMATION_COMMAND(FEngineWaitLatentCommand(0.5));
ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this] {
	const auto L = UnLua::GetState();
	...
	UnLua::RunChunk(L, "return G_IssueObject:CollectInfo()");
	TestEqual(TEXT("Result1"), Result1, 2);
	UnLua::RunChunk(L, "return G_IssueObject:GetConfig()");
	TestEqual(TEXT("Result2"), Result2, 1);
	return true;
}));
// ★ 每个历史问题都有独立、可复跑、可定位的 regression 场景
```

**吸收建议**

- 不要新起一套测试基础设施。直接在现有 `GeneratedFunctionTableTests`、`DumpTests`、`BindConfigTests` 之上补一条 **cross-stage consistency regression** 即可。
- 第一条增量 case 应直接服务 `P10`：选一个最小 `UInterface` 或 `FInterfaceProperty` 缺口，要求同一测试同时断言 `AS_FunctionTable_SkippedEntries.csv`、`AS_FunctionTable_SkippedReasonSummary.csv`、runtime `BindRegistrations.csv`（或未来 manifest）对同一 `SymbolKey` 给出可对齐的 reason/origin。
- 可以借鉴 UnLua 的 issue-named 组织方式，把它沉成 `IssueXXXX` 风格测试目录或 map asset；但不应放弃当前仓库已经很强的 repo-owned runner、UHT artifact 回归和 state dump harness。
- 这条工作完全可以插件内落地，不需要修改引擎。短期甚至不必等 interface fully supported：先用一个“当前必然 skipped 的 interface case”建立 negative regression，后续再随着实现推进把预期从 `Skipped` 改成 `Bound`。

**优先级**

- `P1`
- 理由：它不会先于 `P10` 本身，但应紧跟在第一批 interface/property authority 改动之后落地；否则 `Bind API GAP` 的量化会继续靠人工追 UHT csv、state dump 和运行时行为三条线。

### 值得吸收的设计模式（增量）

- `Reasonful negative-space ledger`：对“没生成/没绑定/没生效”的对象，优先保留结构化 reason，而不是退回单纯 `bool` 或 silent skip。当前 Angelscript 这条线本身就是优势，应保留并扩展。
- `Single verdict owner, many consumers`：可借鉴 UnrealCSharp/UnLua/puerts 的单入口 producer 思路，但 verdict 不应只剩 `true/false`。更合理的路线是单一 owner 输出 `Reason + Origin + SymbolKey`，由 UHT、runtime、docs、IDE 共同消费。
- `Issue-scoped cross-stage regression`：借鉴 UnLua 的 issue regression 组织方式，把“某个 symbol 在 build/runtime 两侧是否说同一件事”沉成可复跑案例，而不是继续靠人工交叉阅读 csv。

### 改进路线建议（增量）

1. `P1`：先给 `FBindInfo` 或并行 sidecar 补 provenance 字段，把 `BindModule` 空列和 runtime `SkipReason` 贫血问题解决掉；目标不是一次性完美，而是让 runtime 至少能表达 `origin + reason`。
2. `P1`：把 UHT `failureReason` taxonomy 抽成共享 contract，先服务 `UInterface / FInterfaceProperty / TScriptInterface<>` 三类最急 family，避免 `P10` 一推进就把账本做断。
3. `P1`：在现有 automation 上补一条 issue-scoped cross-stage regression，强制校验同一个 `SymbolKey` 在 UHT artifact、runtime dump、future manifest 上的状态一致；这条测试应在 interface 绑定主线合入前就开始存在。

---

## 深化分析 (2026-04-08 23:31:08)

### 本轮新增关键发现

- `P10 UInterface` 当前最需要先收紧的，不是“支持更多 callable 形状”，而是 **把 interface callable 的正确边界说清楚**。源码显示这条链在进入 runtime/UE 之后会从完整声明退化成 `FName`；基于源码推断，同名多签名 interface method 目前没有稳定落到同一 callable owner 的条件。
- 当前仓库的 `failureReason` 账本已经很强，但它主要覆盖 UHT native callable。interface callable 恰好绕开了这套账本，于是最危险的 unsupportedness 反而最不透明。
- 现有 interface regression 已经覆盖继承、缺失方法、hot reload、native interop，但都建立在“每个 interface method 名字唯一”的前提上；这不足以保护 `P10` 推进时的 callable 边界。

### 差距矩阵（增量）

| 维度 | 关注点 | 差距等级 | 差距类型 | 优先级 |
| --- | --- | --- | --- | --- |
| D2 反射绑定机制 | interface callable 的签名 owner / overload 安全边界 | 能力缺失 | 当前只有 `FunctionName`，没有可复用的 `CallableKey/Descriptor` | P0 |
| D6 代码生成与 IDE 支持 | interface callable 的 unsupported reason 可见性 | 能力缺失 | interface lane 直接 `ERASE_NO_FUNCTION()`，没有 `interface-overload-unsupported` 一类显式账本 | P1 |
| D9 测试基础设施 | interface callable 边界的负向回归 | 能力缺失 | 没有保护 same-name multi-signature / signature collapse 的 regression | P1 |

### [D2] 反射绑定机制：interface callable 目前只在 VM 层保留完整声明，进入 runtime/UE 后立即退化成 `FunctionName`

**当前状态**

当前 interface callable 的 owner 被切成三段，而且每走一段都会丢信息：

- `FAngelscriptClassDesc` 只保存 `InterfaceMethodDeclarations` 原始字符串，见 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:1101-1109`。
- 预处理阶段确实拿到了完整 `MethodDecl`，并且把完整声明传给 `RegisterObjectMethod()`；但挂在 `ScriptFunc->UserData` 上的只有 `FInterfaceMethodSignature{ FunctionName }`，见 `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:1112-1157` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1254-1260`。
- runtime dispatch 再按 `FindFunction(FName)` 找真实 `UFunction`，见 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:56-67`。
- full reload 生成 interface `UFunction` 壳时，又只保留“同名、无参数、无返回值”的最小 `UFunction`，见 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2803-2830`。
- 实现类校验同样只按 `FindFunctionByName(InterfaceFunc->GetFName())` 做名字匹配，见 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:5160-5184`。

```
[Angelscript] Interface Callable Degradation
MethodDecl "void Ping(float Amount)"
MethodDecl "void Ping(int32 Amount)"
├─ Preprocessor keeps full ASDecl                    // VM 注册时还有完整声明
├─ RegisterInterfaceMethodSignature(FName("Ping"))  // userData 只剩函数名
├─ CallInterfaceMethod -> FindFunction("Ping")      // runtime dispatch key 退化成名字
├─ DoFullReload -> minimal UFunction shell          // UE 反射壳不含参数/返回值
└─ FinalizeClass -> FindFunctionByName("Ping")      // 实现校验仍只按名字
```

[55] 当前 interface callable 链的关键问题不是“没注册”，而是“注册后立刻丢签名”：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h
// 函数: FInterfaceMethodSignature / FAngelscriptClassDesc
// 位置: 59-62, 1101-1109
// ============================================================================
struct FInterfaceMethodSignature
{
	FName FunctionName; // ★ interface callable 的 steady-state payload 只剩名字
};

bool bIsInterface = false;
TArray<FString> ImplementedInterfaces;
TArray<FString> InterfaceMethodDeclarations;
// ★ class desc 也只保存 raw declaration string，没有结构化 callable descriptor

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp
// 函数: ParseClass / interface 注册分支
// 位置: 1112-1157
// ============================================================================
for (const FString& MethodDecl : ClassDesc->InterfaceMethodDeclarations)
{
	FString MethodName = BeforeParen.Mid(LastSpace + 1).TrimStartAndEnd();
	auto* Sig = Engine.RegisterInterfaceMethodSignature(FName(*MethodName));

	int32 FuncId = Engine.Engine->RegisterObjectMethod(
		TCHAR_TO_ANSI(*InterfaceName),
		TCHAR_TO_ANSI(*ASDecl),          // ★ VM 注册层仍拿到完整声明
		asFUNCTION(CallInterfaceMethod),
		asCALL_GENERIC,
		nullptr);

	ScriptFunc->SetUserData(Sig, 0);    // ★ 但挂到 runtime dispatch 上的只剩 FunctionName
}

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 函数: CallInterfaceMethod / DoFullReloadClass / FinalizeClass
// 位置: 56-67, 2803-2830, 5160-5184
// ============================================================================
UFunction* RealFunc = Object->FindFunction(Sig->FunctionName);
// ★ runtime dispatch 不再有参数/返回值/qualifier 的第二道校验

FString FuncName = BeforeParen.Mid(LastSpace + 1).TrimStartAndEnd();
UFunction* NewFunction = NewObject<UFunction>(NewClass, *FuncName, RF_Public);
NewFunction->FunctionFlags = FUNC_Event | FUNC_BlueprintEvent | FUNC_Public;
NewFunction->ReturnValueOffset = MAX_uint16;
// ★ interface 壳只保留名字，UE 反射层看不到完整签名

UFunction* ImplFunc = NewClass->FindFunctionByName(InterfaceFunc->GetFName());
if (ImplFunc == nullptr || bResolvedToInterfaceStub)
{
	// ★ 实现类校验同样只按名字，不按完整 callable identity
}
```

**差距描述**

- `能力缺失`：当前没有 interface callable 的结构化 `Descriptor/Key`。这不是“实现方式不同”而已，而是 `P10` 若继续扩大 callable 覆盖面，`ref/out`、容器、默认值之外，**连同名多签名是否允许** 都没有统一 owner。
- `实现质量差异`：native callable 的 UHT 路径至少会把 `overloaded-unresolved` 显式记账；interface callable 现在没有等价的 compile-time gate。基于源码推断，只要 interface 声明出现同名多签名，当前链路至多只能“偶然工作”，不能算稳定 contract。
- `实现质量差异`：现有实现把完整声明仅用于 AngelScript VM 注册，而不是用于 runtime/UE/验证链。结果是“脚本编译看见的东西”和“UE 反射/实现校验看见的东西”不是同一份 callable 真相。

**参考方案**

- `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGeneratorCore.cpp:945-1010`：interface function 生成时直接用结构化 method reflection 创建完整 `UFunction`，返回值、参数、`ref` 标记都进入同一条生成主链。
- `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:415-430` 与 `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/ObjectProperty/FInterfacePropertyDescriptor.cpp:29-45`：`FInterfaceProperty` 不是旁路特判，而是显式进 `TScriptInterface<>` 桥与 property descriptor。

[56] UnrealCSharp 的可借鉴点不是“支持更多语法”，而是 **callable/property 都吃同一份结构化描述**：

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGeneratorCore.cpp
// 函数: FDynamicGeneratorCore::GeneratorFunction
// 位置: 945-1010
// ============================================================================
for (const auto& [Pair, Method] : InClassReflection->GetMethods())
{
	auto Function = NewObject<UFunction>(InClass, FName(Pair.Key), RF_Public | RF_Transient);

	if (const auto Return = Method->GetReturn())
	{
		const auto Property = FTypeBridge::Factory<true>(Return, Function, "", RF_Public | RF_Transient);
		Property->SetPropertyFlags(CPF_Parm | CPF_OutParm | CPF_ReturnParm);
		Function->AddCppProperty(Property); // ★ 返回值进入同一条生成主链
	}

	for (auto Index = Method->GetParamCount() - 1; Index >= 0; --Index)
	{
		const auto Property = FTypeBridge::Factory<true>(
			Params[Index]->GetReflectionType(),
			Function,
			FName(Params[Index]->GetName()),
			RF_Public | RF_Transient);
		if (Params[Index]->IsRef())
		{
			Property->SetPropertyFlags(CPF_OutParm | CPF_ReferenceParm);
		}
		Function->AddCppProperty(Property); // ★ 参数/ref 也都是结构化 property
	}
}

// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp
// 函数: FTypeBridge::GetClass(const FInterfaceProperty*)
// 位置: 415-430
// ============================================================================
const auto FoundGenericClass = FReflectionRegistry::Get().GetTScriptInterfaceClass();
const auto FoundClass = FReflectionRegistry::Get().GetClass(InProperty->InterfaceClass);
return MakeGenericTypeInstance(FoundGenericClass, FoundClass);
// ★ interface 值桥和 callable 生成都建立在结构化 type/property graph 上
```

**吸收建议**

- 在当前约束下，不要把“支持 overloaded interface method”当作 `P10` 第一刀目标。更稳妥的插件内路线是先定义 `FAngelscriptInterfaceMethodDesc` 或等价 `CallableKey`，字段至少包含 `Name`、`NormalizedReturnType`、`NormalizedParamTypes`、`Qualifier`、`OwnerKind`。
- `Preprocessor` 先把 `MethodDecl` 解析成 descriptor；如果两个 declaration 在当前 runtime/UE lane 会坍缩到同一 callable identity，就在编译期显式报 `interface-overload-unsupported` 或 `interface-signature-collapsed`，而不是默许继续走 `FunctionName`-only lane。
- `CallInterfaceMethod()` 只把 `FindFunction(FName)` 保留为 legacy unique-name fallback。主路径应优先消费 descriptor 绑定好的 `UFunction*` 或 typed callable handle。
- `FinalizeClass()` 的实现校验也应升级到 descriptor 粒度；否则即使 property bridge 补齐了，callable correctness 仍会被名字匹配拖回旧语义。

**优先级**

- `P0`
- 理由：这一步不是“再加一批 API”，而是给 `P10 UInterface` 主线画出稳定边界。若不先把 unsupported callable 形状显式拒绝，后续 `FInterfaceProperty`、native interface 自动暴露、IDE 清单都会建立在不稳定 contract 上。

### [D6] 代码生成与 IDE 支持：当前最强的 reason ledger，恰好没有覆盖 interface callable 的 name-only collapse

**当前状态**

当前仓库对 native callable 的 unsupported 诊断已经很成熟：

- `AngelscriptHeaderSignatureResolver` 会把 header 解析失败区分成 `unexported-symbol` / `overloaded-unresolved` 等原因，见 `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs:90-106,171-177`。
- `AngelscriptFunctionSignatureBuilder` 再决定哪些 reason 可以继续生成 direct-bind fallback，见 `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs:43-60`。

但 interface callable 完全绕开了这套 vocabulary：

- `AngelscriptFunctionTableCodeGenerator` 对 `Interface/NativeInterface` 直接写 `ERASE_NO_FUNCTION()`，不会进入 `TryBuild()` 的失败原因链，见 `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:465-479`。
- 这意味着最需要被量化的 `interface-signature-collapsed`、`interface-overload-unsupported` 一类问题，在当前产物里没有正式 reason code。

```
[Angelscript] D6 Ledger Blind Spot
Native UFunction
├─ HeaderResolver -> unexported-symbol / overloaded-unresolved
├─ SignatureBuilder -> allow / reject fallback
└─ skipped csv / summary -> reasonful ledger

Interface callable
├─ ClassType == Interface / NativeInterface
└─ ERASE_NO_FUNCTION() only                        // 没有 interface-specific reason
```

[57] 当前账本的盲区并不是“没有 skipped ledger”，而是 **interface lane 直接绕过 ledger**：

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs
// 函数: AngelscriptFunctionSignatureBuilder::TryBuild
// 位置: 43-60
// ============================================================================
if (AngelscriptHeaderSignatureResolver.TryBuild(classObj, function, out signature, out failureReason))
{
	return true;
}

if (failureReason == "non-public" || failureReason == "unexported-symbol")
{
	return false;
}

if (failureReason == "overloaded-unresolved" && !IsWhitelistedDirectBindFallback(classObj, function))
{
	return false; // ★ native callable 至少会显式留下 overload 失败原因
}

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs
// 函数: TryBuild / NormalizeTypeText
// 位置: 90-106, 171-177
// ============================================================================
if (exactMatches.Count == 1)
{
	signature = exactMatches[0];
	return true;
}

failureReason = matchedUnexportedSymbol ? "unexported-symbol" : "overloaded-unresolved";
// ★ 现有账本有明确的 overload 失败 vocabulary

private static string NormalizeTypeText(string typeText)
{
	return CollapseWhitespace(typeText)
		.Replace("const ", string.Empty, StringComparison.Ordinal)
		.Replace("&", string.Empty, StringComparison.Ordinal)
		.Replace(" ", string.Empty, StringComparison.Ordinal)
		.Trim();
}

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs
// 函数: CollectEntries
// 位置: 465-479
// ============================================================================
if (classObj.ClassType == UhtClassType.Interface || classObj.ClassType == UhtClassType.NativeInterface)
{
	eraseMacro = "ERASE_NO_FUNCTION()"; // ★ interface callable 不进 failureReason 链
}
else if (AngelscriptFunctionSignatureBuilder.TryBuild(classObj, function, out AngelscriptFunctionSignature? signature, out string? _))
{
	eraseMacro = signature!.BuildEraseMacro();
}
entries.Add(new AngelscriptGeneratedFunctionEntry(classObj.SourceName, function.SourceName, eraseMacro));
```

**差距描述**

- `能力缺失`：当前没有 interface callable 专属的 `ReasonCode`。这会让 `Bind API GAP` 在最关键的 interface family 上失去可量化性。
- `实现差异`：当前仓库已经拥有比多数参考插件更强的 skipped ledger；因此这里不应判成“整体 D6 落后”，而应判成 **现有强项没有接到 interface lane**。
- `实现质量差异`：IDE/docs 只能看到“interface 没进来”，却不知道是“尚未支持”“同名多签名坍缩”“property bridge 缺失”还是“native symbol 不可导出”。

**参考方案**

- `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1164-1224`：即便 declaration 层不能完整支持所有 overload，也会把 unsupported super overload 显式写成 `@deprecated Unsupported super overloads.`，把 unsupportedness 留在产物里，而不是完全沉默。

[58] puerts 值得借鉴的是“unsupported 也要进入产物 contract”，不是它的具体技术栈：

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp
// 函数: GenResolvedFunctions
// 位置: 1164-1224
// ============================================================================
if (!GenFunction(Tmp, Function, true, false, false, true))
{
	continue; // ★ 多数 unsupported function 直接跳过
}
TryToAddOverload(Outputs, Function->GetName(), false, Tmp.Buffer);

if (!Overloads.Contains(*SuperOverloadIter))
{
	Buff << "    /**\n";
	Buff << "     * @deprecated Unsupported super overloads.\n";
	Buff << "     */\n";
	Buff << "    " << *SuperOverloadIter << ";\n";
	// ★ 但 declaration 产物至少会显式保留“这里有个不完全支持的 overload”
}
```

**吸收建议**

- 不需要等到 interface fully supported 才做这层工作。先把当前 UHT 账本扩成 `GeneratedSymbolDecision` 或等价结构，新增 `SymbolKind = InterfaceCallable`、`Reason = interface-overload-unsupported / interface-name-only-dispatch / interface-minimal-ufunction-stub` 即可。
- 这条 ledger 的 key 不应只写 `InterfaceName + MethodName`，而应至少保留完整 `MethodDecl` 或其规范化签名；否则仍然无法表达“同名多签名里哪一条没被支持”。
- `Docs/DebugDatabase` 第一阶段不必展示所有 reason 文案，但至少要能按同一个 `SymbolKey` 追问“为什么没出现”。

**优先级**

- `P1`
- 理由：它不先于 `D2/P0` 的 callable owner 收口，但应与 `P10` 同轮启动。否则主线一旦开始补 interface callable，产物层仍然无法区分“尚未覆盖”与“被 name-only lane 吞掉”。

### [D9] 测试基础设施：interface regression 还没有保护“名字不唯一”的负向边界

**当前状态**

现有 interface 测试已经覆盖了很多正确路径：

- `AngelscriptInterfaceAdvancedTests.cpp` 覆盖继承接口、缺失方法、无属性、GC 安全、hot reload、native `UInterface`、多继承链与 dispatch，见 `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceAdvancedTests.cpp:19-208`。
- `AngelscriptInterfaceImplementTests.cpp` 覆盖基础实现、多接口实现与 `ImplementsInterface()` happy path，见 `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceImplementTests.cpp:18-249`。

但这些用例有一个共同前提：interface method 都是 unique-name。当前没有看到 same-name multi-signature 的负向 regression，也没有一条测试去锁定“如果当前不支持 overload，必须稳定报错而不是静默坍缩”。

```
[Current Interface Test Matrix]
Covered
├─ inherited interface graph
├─ missing required method
├─ multiple interfaces
├─ native interface happy path
└─ hot reload / dispatch / GC safe

Missing
├─ same-name multi-signature interface declaration
├─ name collision after normalization
└─ explicit error contract for unsupported interface callable shape
```

[59] 当前测试矩阵确实很厚，但仍建立在 unique-name callable 假设上：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceAdvancedTests.cpp
// 函数: FAngelscriptScenarioInterfaceInheritedInterfaceTest /
//       FAngelscriptScenarioInterfaceMissingMethodTest
// 位置: 19-208
// ============================================================================
UINTERFACE()
interface UIDamageableParent
{
	void TakeDamage(float Amount); // ★ 当前 advanced regression 里的方法名都是唯一的
}

UINTERFACE()
interface UIKillableChild : UIDamageableParent
{
	void Kill();
}

UINTERFACE()
interface UIDamageableMissing
{
	void TakeDamage(float Amount);
	float GetHealth(); // ★ 缺的是“少一个名字”，不是“同名多签名”
}

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceImplementTests.cpp
// 函数: FAngelscriptScenarioInterfaceImplementMultipleTest /
//       FAngelscriptScenarioInterfaceImplementsInterfaceMethodTest
// 位置: 94-249
// ============================================================================
UINTERFACE()
interface UIDamageableMulti
{
	void TakeDamage(float Amount);
}

UINTERFACE()
interface UIHealableMulti
{
	void Heal(float Amount);
}
// ★ 多接口回归也建立在“每个 callable 名字都不冲突”的前提上
```

**差距描述**

- `没有实现`：当前没有 regression 会在 interface lane 上故意制造 same-name multi-signature，再断言系统给出稳定、显式、可复现的错误。
- `实现质量差异`：现有测试可以证明“name-only lane 的 happy path 能工作”，但不能证明 `P10` 扩面后不会把 unsupported case 静默吞掉。
- `实现质量差异`：一旦未来开始补 `FInterfaceProperty`、typed callable descriptor 或 interface UHT ledger，没有这类负向回归，很容易出现“能力做对了 80%，边界悄悄变坏了 20%”的情况。

**参考方案**

- `Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Tests/IssueOverridesTest.cpp:25-58`：把一个明确缺陷沉成 issue-scoped regression，用真实脚本调用链验证结果，而不是只测 helper 或 csv。

[60] UnLua 值得吸收的是“一个已知问题 = 一条持续回归”，很适合拿来守 interface 边界：

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Tests/IssueOverridesTest.cpp
// 函数: FIssueOverridesTest::RunTest
// 位置: 25-58
// ============================================================================
BEGIN_TESTSUITE(FIssueOverridesTest, TEXT("UnLua.Regression.IssueOverrides 覆写引起的问题"))

const auto MapName = TEXT("/UnLuaTestSuite/Tests/Regression/IssueOverrides/IssueOverrides.IssueOverrides");
ADD_LATENT_AUTOMATION_COMMAND(FOpenMapLatentCommand(MapName))
ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this] {
	UnLua::RunChunk(L, "return G_IssueObject:CollectInfo()");
	TestEqual(TEXT("Result1"), Result1, 2);
	UnLua::RunChunk(L, "return G_IssueObject:GetConfig()");
	TestEqual(TEXT("Result2"), Result2, 1);
	return true;
}));
// ★ 历史边界问题被沉成稳定、可复跑的 regression，而不是停留在人工验证
```

**吸收建议**

- 直接新增一条 issue-scoped interface regression，例如 `Angelscript.TestModule.Interface.UnsupportedOverload`。
- 第一阶段不要尝试证明“系统支持 overload”；应证明“当前系统会稳定拒绝它”。最小样例可以是：
  - `UINTERFACE() interface UIFoo { void Ping(float Amount); void Ping(int32 Amount); }`
  - 期望：编译失败，错误文本命中 `interface-overload-unsupported` 或等价明确 reason。
- 第二条回归应检查反射侧没有生成模糊的 `UFunction` 壳，避免“编译报错了，但旧 interface 壳还留在 class table”这类半失败状态。
- 这些测试完全可以插件内落地，不要求修改引擎，也不依赖 AngelScript 2.33.0 runtime 升级。

**优先级**

- `P1`
- 理由：它不替代 `P10` 的功能开发，但会成为 `P10` 的安全阀。先把 unsupported interface callable 形状锁成明确错误，后续 capability closeout 才不会把 silent collapse 误当作“已经支持一部分”。

### 值得吸收的设计模式（增量）

- `Descriptor-first boundary, fallback-second`：先用结构化 descriptor 定义 callable/property 的真实边界，再决定哪些 case 可以走 legacy fallback；不要让 fallback 反过来定义 capability。
- `Unsupported shape is also a product artifact`：像 puerts 那样，即使某类 overload 暂不支持，也要把 unsupportedness 留在 declaration/manifest 中，而不是只在运行时缺席。
- `Issue-scoped negative regression`：对“当前明确不支持”的形状，同样建立稳定回归。这样主线推进时，unsupported 到 supported 的迁移才是可观察的。

### 改进路线建议（增量）

1. `P0`：先为 interface callable 引入最小 `CallableKey/Descriptor`，并在当前无法稳定表示的同名多签名场景下编译失败，显式报 `interface-overload-unsupported`。
2. `P1`：把 interface callable 也接进现有 reason ledger，新增 `InterfaceCallable` 维度的 `ReasonCode`，让 UHT/docs/debug manifest 至少能解释“为什么没进来”。
3. `P1`：补一条 issue-scoped negative regression，锁定 same-name multi-signature interface 的错误 contract；之后再随着 `P10` 推进，把预期从 `Unsupported` 逐步迁移到 `Supported`。

---

## 深化分析 (2026-04-08 23:45:30)

> **优先级校准说明**：仓库内未发现根级 `todo.md`。本轮优先级按 `Documents/Plans/Plan_CppInterfaceBinding.md:21-24,96-106`、`Documents/Plans/Plan_InterfaceBinding.md:150-180,242-275` 与 `Documents/AutoPlans/ArchitectureReview/TypeSystem_ArchReview.md:39-75,189-225` 对齐，因此仍以 `P10 UInterface / Bind API GAP` 为主线。

### [D2] 命名面与 `TypeFinder` 接缝：native interface 目前只稳定暴露 `U...` handle，`FInterfaceProperty` 也还没进入同一入口

**当前状态**

- `FAngelscriptType::GetBoundClassName()` 直接返回 `PrefixCPP + Name`，因此 native `UINTERFACE` 的脚本显示名天然落在 `U...` 这一侧，见 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:28-32`。
- `FAngelscriptType::RegisterAlias()` 已经存在，但当前 interface 主路径没有调用它，见 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:108-111`。
- `BindUClassLookup()` 是当前 `FProperty -> FAngelscriptTypeUsage` 的集中接缝，但只覆盖 `FObjectProperty`、`FWeakObjectProperty`、`FClassProperty/TSubclassOf`、`TObjectPtr`；没有 `FInterfaceProperty` 分支，见 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:2420-2515`。
- native interface 场景测试也只验证 `Cast<UAngelscriptNativeParentInterface>` 这一条命名面，见 `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceNativeTests.cpp:153-160,393-400`。

```
[D2-Delta] Native Interface Entry Surface
Native UINTERFACE
├─ GetBoundClassName() -> UMyInterface             // 当前 canonical 名字
├─ RegisterAlias() exists                          // 但 interface 主链没调用
├─ BindUClassLookup()                              // property/type 集中入口
│  ├─ Object / Weak / Class / Subclass / TObjectPtr
│  └─ no FInterfaceProperty
└─ Tests -> Cast<UMyInterface>()                   // 只覆盖 U 前缀语义
```

[61] 当前命名与别名能力都在 `AngelscriptType.cpp`，但 interface 主路径只用了第一半：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp
// 函数: FAngelscriptType::GetBoundClassName / RegisterAlias
// 位置: 28-32, 108-111
// ============================================================================
FString FAngelscriptType::GetBoundClassName(UClass* Class)
{
	FString Name = Class->GetPrefixCPP();
	Name += Class->GetName();
	return Name; // ★ native interface 当前天然生成 `U...` 名字
}

void FAngelscriptType::RegisterAlias(const FString& Alias, TSharedRef<FAngelscriptType> Type)
{
	auto& Database = GetTypeDatabase();
	Database.TypesByAngelscriptName.Add(Alias, Type); // ★ alias 能力已存在，但 interface 主链没有接入
}
```

[62] `BindUClassLookup()` 已经是最窄的 property/type choke point，但目前没有 interface case：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp
// 函数: BindUClassLookup / FUObjectType::CreateProperty / FUObjectType::MatchesProperty
// 位置: 122-139, 149-180, 2420-2515
// ============================================================================
FProperty* CreateProperty(const FAngelscriptTypeUsage& Usage, const FPropertyParams& Params) const override
{
	if (Class == UClass::StaticClass())
	{
		auto* Property = new FClassProperty(Params.Outer, Params.PropertyName, RF_Public);
		Property->PropertyClass = Class;
		Property->MetaClass = UObject::StaticClass();
		return Property;
	}

	auto* Property = new FObjectProperty(Params.Outer, Params.PropertyName, RF_Public);
	Property->PropertyClass = Class != nullptr ? Class : (UClass*)Usage.ScriptClass->GetUserData();
	return Property; // ★ 这里只有 FClassProperty / FObjectProperty，没有 FInterfaceProperty
}

bool MatchesProperty(const FAngelscriptTypeUsage& Usage, const FProperty* Property, EPropertyMatchType MatchType) const override
{
	const FObjectProperty* ObjectProp = CastField<FObjectProperty>(Property);
	if (ObjectProp == nullptr)
		return false; // ★ interface property 直接被挡在外面
	...
}

FAngelscriptType::RegisterTypeFinder([=](FProperty* Property, FAngelscriptTypeUsage& Usage) -> bool
{
	const FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property);
	...
	const FWeakObjectProperty* WeakObjectProperty = CastField<FWeakObjectProperty>(Property);
	...
	const FClassProperty* ClassProperty = CastField<FClassProperty>(Property);
	...
	return false; // ★ 没有 FInterfaceProperty -> TypeUsage 的集中分支
});
```

**差距描述**

- `能力缺失`：`BindUClassLookup()` 还不能把 `FInterfaceProperty` 送进主 type pipeline；这比“UI 命名好不好看”更早卡住 `P10`。
- `实现差异`：当前 native interface 的脚本 surface 默认是 `UMyInterface`，而不是 `UMyInterface + IMyInterface` 双入口；`Plan_InterfaceBinding.md:176-180` 中规划的 `U/I dual name alias` 还未落地。
- `实现质量差异`：测试只覆盖 `U...` 命名面，会把“脚本目前只能沿 canonical handle 访问”误读成“interface 命名约定已定稿”。

**参考方案**

- `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp:94-126`：生成阶段显式把 `GetFullInterface()` 追加到 type surface，class 与 interface 双形态同时可见。
- `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1537-1595,533-560`：`CPT_Interface` 与 `FInterfacePropertyDesc` 直接进入 property factory，没有单独旁路。

[63] UnrealCSharp 的命名面不是靠临时 alias 猜出来，而是在 generator 层显式输出 interface surface：

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp
// 函数: FClassGenerator::Generator
// 位置: 94-126
// ============================================================================
auto bIsInterface = InClass->IsChildOf(UInterface::StaticClass());

for (auto Interface : InClass->Interfaces)
{
	UsingNameSpaces.Add(FUnrealCSharpFunctionLibrary::GetClassNameSpace(Interface.Class));

	InterfaceContent += FString::Printf(TEXT(
		", %s"
	),
		*FUnrealCSharpFunctionLibrary::GetFullInterface(Interface.Class)); // ★ 生成面显式追加 interface 形态
}
```

**吸收建议**

- 先把 `BindUClassLookup() + FUObjectType::CreateProperty/MatchesProperty` 当作 `P10` 最窄切口；`FInterfaceProperty` 进入这里后，再谈容器、callable、IDE 资产。
- `UMyInterface` 在首阶段可以继续作为 canonical 名字，避免扩大兼容面；但 `BindUClass` 成功注册 native interface 后，应该立刻评估 `RegisterAlias(IMyInterface, Type)`，把 `Plan_InterfaceBinding.md:176-180` 的双入口从“文档决策”推进成真实 capability。
- 回归层应新增两条 production-path 验证：一条验证 canonical `U...` 能直接解析；一条验证 alias `I...` 是否存在。两条不能再混成一个“接口可用”的笼统结论。

**优先级**

- `P0`
- 理由：这一维里真正卡 `P10` 的不是 alias，而是 `FInterfaceProperty` 还没进入 `BindUClassLookup()` 这个集中入口；alias 属于同一条主线上的紧随步骤，适合放在 `P0` 收口尾段或 `P1` 起步。

### [D4] 依赖粒度仍是“是否有 interface”的布尔判定，缺少 reference-style dependency graph / rebind fanout

**当前状态**

- `ShouldFullReload()` 只要类本身是 interface，或者实现了任意 interface，就直接要求 full reload，见 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:2081-2088`。
- `DoFullReload()` 的 interface 分支会重建 interface metadata 与最小 `UFunction` stub，见 `.../AngelscriptClassGenerator.cpp:2762-2808`。
- `FinalizeClass()` 再按 `ImplementedInterfaces` 重新解析 interface、递归补 `NewClass->Interfaces`，并按名字检查实现函数，见 `.../AngelscriptClassGenerator.cpp:5059-5184`。
- `FAngelscriptEngine::CompileModule()` 最终只有 `SoftReload / FullReloadSuggested / FullReloadRequired` 这一层 reload lattice；并不知道“哪几个实现类只是 interface graph 变了、哪几个对象需要 rebind”，见 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3936-3997`。

```
[D4-Delta] Interface Reload Granularity
interface diff
├─ Analyze -> ReloadReq lattice                     // 只有 soft/full 层级
├─ ShouldFullReload()
│  ├─ bIsInterface => true
│  └─ ImplementedInterfaces.Num() > 0 => true
├─ DoFullReload(interface metadata)
└─ FinalizeClass(implementers)
   └─ resolve interface names + add Interfaces[]    // 没有 symbol/object 级 impact 集
```

[64] 当前 reload 判定把 interface 影响压成一个布尔条件，而不是依赖图：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 函数: ShouldFullReload / DoFullReload / FinalizeClass
// 位置: 2081-2088, 2762-2808, 5059-5184
// ============================================================================
bool FAngelscriptClassGenerator::ShouldFullReload(FClassData& Class)
{
	if (bIsDoingFullReload && Class.ReloadReq >= EReloadRequirement::FullReloadSuggested)
		return true;
	if (Class.NewClass->bIsInterface)
		return true; // ★ interface 本体一律 full reload
	if (Class.NewClass->ImplementedInterfaces.Num() > 0)
		return true; // ★ 任何实现类也直接进入 full path
	...
}

else if (ClassData.NewClass->bIsInterface)
{
	// ★ interface reload 仍走 metadata 重建 + 方法字符串解析
	for (const FString& MethodDecl : InterfaceDesc->InterfaceMethodDeclarations)
	{
		// For now, create a minimal UFunction with just the name
		...
	}
}

if (ClassDesc->ImplementedInterfaces.Num() > 0 && !ClassDesc->bIsInterface)
{
	// ★ 实现类在 finalize 阶段再回填 Interfaces[] 并按名字校验
	UClass* InterfaceClass = ResolveInterfaceClass(InterfaceName);
	NewClass->Interfaces.Add(ImplementedInterface);
	...
	UFunction* ImplFunc = NewClass->FindFunctionByName(InterfaceFunc->GetFName());
}
```

**差距描述**

- `没有实现`：当前没有单独的 `InterfaceImpactSet` / `ReloadImpactManifest` 去表达“哪个 interface 变了，受影响的实现类/对象是谁”。
- `实现差异`：现有系统有成熟的 `SoftReload / FullReloadRequired` 枚举，这不是“完全没有热重载”；真正差距在于 interface 相关影响仍是 class 级粗粒度，而非 dependency graph。
- `实现质量差异`：一旦 `P10` 把 native `UInterface`、`FInterfaceProperty` 和更多 interface callable 送进主链，当前这种“只要有 interface 就 full reload”的路径会把编辑回路成本放大。

**参考方案**

- `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGeneratorCore.cpp:212-230` 与 `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicDependencyGraph.cpp:11-16,54-77,80-120`：先把 interface 依赖记入 graph，再按依赖顺序调度生成。
- `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/TypeScriptGeneratedClass.cpp:77-99` 与 `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:2325-2366`：reload 后显式 walk `GeneratedObjects` 做 rebind，而不是把所有影响都抬成 full class reload。

[65] UnrealCSharp 的关键点不是“它也能 reload”，而是 interface 依赖首先进入图，再决定生成顺序：

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGeneratorCore.cpp
// 函数: FDynamicGeneratorCore::GeneratorInterface
// 位置: 212-230
// ============================================================================
void FDynamicGeneratorCore::GeneratorInterface(const FClassReflection* InClassReflection,
                                               FDynamicDependencyGraph::FNode& OutNode)
{
	const auto AttributeClass = FReflectionRegistry::Get().GetUInterfaceAttributeClass();

	for (const auto Interface : InClassReflection->GetInterfaces())
	{
		if (Interface->HasAttribute(AttributeClass))
		{
			OutNode.Dependency(FDynamicDependencyGraph::FDependency{
				IInterfaceToUInterface(Interface)->GetName(), false
			}); // ★ interface 先进入依赖图，再决定后续生成次序
		}
	}
}
```

**吸收建议**

- 不建议在 `P10` 当前轮次就整包移植完整 graph 系统；更现实的路线是在插件内先补一份最小 `InterfaceImpactSet`，只记录 `ChangedInterface`、`AffectedImplementers`、`RequiresRebindObjects`。
- `ShouldFullReload()` 的布尔条件可以先保留作为 fallback，但在 compile 结束后应把 interface diff 显式广播给 editor/test/rebind consumer；这样未来接 `BlueprintImpactScanner`、`ClassReloadHelper` 或 puerts-style object rebind 时，不必再重扫全世界。
- 对当前主线的优先级判断应保守：先完成 `P10` 能力闭环，再缩 reload 半径。否则容易把“能力尚未闭环”与“回路成本优化”混成一项。

**优先级**

- `P2`
- 理由：这是明确存在的实现质量差异，但不应抢在 `P10 UInterface` 能力补齐之前。先让 interface value/callable 进入主链，再优化 reload 粒度，工程风险更低。

### [D6] `FailureReason` 目前只停在 UHT exporter，`Docs/DebugDatabase` 仍拿不到同一份 join key

**当前状态**

- `AngelscriptFunctionTableExporter` 已经会把 `TryBuild()` 失败的函数写进 `AS_FunctionTable_SkippedEntries.csv` 与 `AS_FunctionTable_SkippedReasonSummary.csv`，见 `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:75-160`。
- `FAngelscriptDocs::DumpDocumentation()` 只把已经进入 runtime doc cache 的 class/property/function declaration 写成 `*.hpp`，并不携带 `Status/Reason`，见 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp:675-755`。
- `FAngelscriptDebugServer::SendDebugDatabase()` 只序列化已经存在的 `asCScriptFunction` 的 `name/return/doc/meta` 等字段；同样没有 `FailureReason` 或 `SymbolKey`，见 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:1545-1617`。

```
[D6-Delta] FailureReason Air Gap
HeaderResolver / TryBuild
├─ Exporter -> SkippedEntries.csv                  // 有 FailureReason
├─ ReasonSummary.csv                              // 有聚合计数
├─ Docs::DumpDocumentation()                      // declaration only
└─ DebugServer::SendDebugDatabase()               // loaded symbols only
   └─ no SymbolKey / no Status / no Reason
```

[66] 当前 reason ledger 只在 exporter 这条支线上是完整的：

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs
// 函数: CountBlueprintCallableFunctions / WriteSkippedEntriesCsv / WriteSkippedReasonSummaryCsv
// 位置: 75-160
// ============================================================================
if (AngelscriptFunctionSignatureBuilder.TryBuild(classObj, function, out AngelscriptFunctionSignature? signature, out string? failureReason))
{
	_ = signature!.BuildEraseMacro();
	reconstructedCount++;
}
else
{
	skippedCount++;
	skippedEntries.Add(new AngelscriptSkippedFunctionEntry(
		moduleName,
		classObj.SourceName,
		function.SourceName,
		string.IsNullOrEmpty(failureReason) ? "unknown" : failureReason)); // ★ UHT 侧已经拿到细粒度 reason
}

builder.AppendLine("ModuleName,ClassName,FunctionName,FailureReason");
...
builder.AppendLine("FailureReason,SkippedCount"); // ★ 还能按 reason 聚合
```

[67] 到 IDE/runtime 侧，这份 reason 已经断掉了，只剩“成功进入引擎的符号”：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 函数: FAngelscriptDebugServer::SendDebugDatabase
// 位置: 1545-1617
// ============================================================================
auto MakeFuncDesc = [&](asCScriptFunction* ScriptFunction) -> TSharedPtr<FJsonObject>
{
	auto FuncDesc = MakeShared<FJsonObject>();
	...
	FuncDesc->SetStringField(TEXT("name"), Name);
	FuncDesc->SetStringField(TEXT("return"), ReturnType);
	...
	const FString& Doc = FAngelscriptDocs::GetUnrealDocumentation(ScriptFunction->GetId());
	if (Doc.Len() != 0)
		FuncDesc->SetStringField(TEXT("doc"), Doc);
	...
	if (UnrealFunction->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintPure))
	{
		FuncDesc->SetBoolField(TEXT("ufunction"), true);
	}
	...
	FuncDesc->SetObjectField(TEXT("meta"), MetaObject); // ★ 这里只有成功符号的展示信息，没有 reason/join key
};
```

**差距描述**

- `没有实现`：当前没有一份能跨 `SkippedEntries.csv -> Docs -> DebugDatabase` 复用的 `SymbolKey/Status/Reason` 合同。
- `实现差异`：UHT exporter 已经是 reasonful ledger，这不是“完全没量化”；问题在于它没有被 runtime/tooling 消费。
- `实现质量差异`：同一个 interface gap 在构建侧表现为 `FailureReason`，在 IDE 侧却可能只是“符号不存在”；这会让 `P10` 收口难以客观对账。

**参考方案**

- `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1174-1224`：即使最终 declaration 不支持某个 super overload，也会把 unsupportedness 保留在产物里，而不是直接沉默。

[68] puerts 值得吸收的是“unsupported 也要进入 artifact contract”：

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp
// 函数: FTypeScriptDeclarationGenerator::GenResolvedFunctions
// 位置: 1174-1224
// ============================================================================
if (!Overloads.Contains(*SuperOverloadIter))
{
	Buff << "    /**\n";
	Buff << "     * @deprecated Unsupported super overloads.\n";
	Buff << "     */\n";
	Buff << "    " << *SuperOverloadIter << ";\n"; // ★ unsupported 也被显式写进声明产物
}
```

**吸收建议**

- 不需要先推翻 `Docs` 或 debug 协议；第一步只要定义一个最小 `FAngelscriptSymbolDecision`，字段至少包含 `SymbolKey`、`Status`、`ReasonCode`、`Origin(UHT/Runtime/IDE)`。
- `SkippedEntries.csv` 可以继续保留为落盘诊断；但 `DumpDocumentation()` 与 `SendDebugDatabase()` 至少要能按同一个 `SymbolKey` 回查“这个 symbol 为什么没出现”。
- 对 `P10` 最有价值的不是直接把所有 unsupported symbol 都展示给 IDE，而是先让产物之间能对账。这样当 `UInterface/FInterfaceProperty` 从 `Stub` 变成 `Supported` 时，三侧口径会同时变化。

**优先级**

- `P1`
- 理由：这不是能力主闭环本身，但它直接决定 `P10` 是否可量化、可验收。等到 interface 主链刚开始补时再补账本，会明显放大回归排查成本。

### 值得吸收的设计模式（增量）

- `Narrow chokepoint first`：优先打通 `BindUClassLookup()` 这类已经存在的集中入口，而不是一开始就重写整套 type system。
- `Graph before fanout`：像 UnrealCSharp 那样先把 interface 依赖记成 graph，再决定 full reload、reinstance 或 rebind，避免把所有影响都压成 class 级布尔值。
- `Reason survives phase boundaries`：像 puerts 那样让 unsupportedness 也成为正式产物的一部分；不要只在 UHT 一侧有 reason，到了 IDE/runtime 就变成“静默缺席”。

### 改进路线建议（增量）

1. `P0`：按 `Plan_CppInterfaceBinding.md:21-24,96-106` 与 `Plan_InterfaceBinding.md:242-275`，先把 `FInterfaceProperty` 接进 `BindUClassLookup()` 与 `FUObjectType`，把 native interface 从“只有 U handle + cast/query”推进到真正的 property/type 入口。
2. `P1`：在不改引擎的前提下补 `RegisterAlias(IMyInterface, Type)` 与共享 `SymbolDecision`/`ReasonCode`，让 `U/I` 命名面与 `UHT -> Docs -> DebugDatabase` 账本同时稳定下来。
3. `P2`：待 `P10` 基础能力闭环后，再把 interface 影响从 `ShouldFullReload()` 的布尔判定升级成最小 dependency graph / rebind manifest，缩小 editor 与 hot reload 的放大半径。

---

## 深化分析 (2026-04-08 23:57:56)

### 本轮新增关键发现

- 仓库根目录未找到 `todo.md`；本轮优先级仍按现有 `GapAnalysis.md`、`TypeSystem_ArchReview.md` 与 `UHTTool_Analysis.md` 中反复出现的 `P10 UInterface / Bind API GAP` 口径收敛，不额外发散到其他主线。
- `UObject::ImplementsInterface()` 当前是脚本侧唯一稳定可用的 interface query 入口，但 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp:100-105` 会把 `nullptr` 和非 interface 误传一并压成 `false`；同时 `UKismetSystemLibrary::DoesClassImplementInterface` 在生成表里仍只是 `Stub`，脚本层拿不到 checked path。
- `AngelscriptFunctionTableExporter` 与 `AngelscriptFunctionTableCodeGenerator` 对 interface callable 仍不是同一个 authority。本轮复核当前生成产物：`UCameraLensEffectInterface`、`UInterface_AssetUserData`、`ULevelInstanceInterface`、`UNavMovementInterface`、`UTypedElementWorldInterface` 共 `44` 条 entry 在 `AS_FunctionTable_Entries.csv` 中被写成 `Stub`，但 `AS_FunctionTable_SkippedEntries.csv` 对这 5 个类均为 `0` 行。
- `GeneratedFunctionTable` 回归当前只锁 “CSV 结构存在、算术自洽、至少有一条 reason”，没有锁 “同一 interface symbol 在 `Entries.csv` / `SkippedEntries.csv` / summary 的 decision parity”；因此 interface forced-stub 即使继续扭曲 coverage rate，自动化也会稳定绿灯。

### 差距矩阵（增量）

| 维度 | 新增观察焦点 | 总评等级 | 主要差距类型 | 优先级 |
| --- | --- | --- | --- | --- |
| D2 反射绑定机制 | interface query surface 的 checked contract | 实现质量差异 | 已有 `ImplementsInterface()`，但缺 checked helper，invalid input 被压成普通 `false` | P1 |
| D6 代码生成与 IDE 支持 | exporter / generator 对 interface stub 的 authority 漂移 | 实现质量差异 | 生成资产存在，但 `ForcedStub` 没有 reasonful contract，coverage 指标被扭曲 | P0 |
| D9 测试基础设施 | interface decision parity regression 缺失 | 能力缺失 | 只测 CSV 算术与表头，不测同 symbol 的 decision 一致性 | P1 |

### [D2] 反射绑定机制：interface 查询面仍缺 checked contract，错误输入会被压成普通 `false`

**当前状态**

- `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp:100-105` 当前直接把 `Object == nullptr` 或 `InterfaceClass == nullptr` 压成 `false`，没有区分“接口没实现”和“参数无效”。
- 同文件 `:135-219` 的 `opCast` 也只在 `AssociatedClass->HasAnyClassFlags(CLASS_Interface)` 时做 `ImplementsInterface()` 判定，没有任何输入合法性反馈。
- 当前脚本面拿不到引擎自带的 checked helper：`Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv:2523-2524` 把 `UKismetSystemLibrary::DoesClassImplementInterface/DoesImplementInterface` 生成成 `Stub`，而 `.../AS_FunctionTable_SkippedEntries.csv:1997-1998` 只记录了 `non-public`。

```
[D2-Delta] Interface Query Contract
script
├─ UObject::ImplementsInterface(UClass)
│  ├─ nullptr / wrong class -> false              // 错误输入与真实“不实现”同值
│  └─ valid interface     -> UClass::ImplementsInterface
└─ checked helper path
   └─ UKismetSystemLibrary::*ImplementInterface   // 当前 generated table 只有 Stub
```

[1] 当前脚本 API 对 invalid input 没有 checked branch：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp
// 函数: UObject::ImplementsInterface
// 位置: 脚本侧 interface query 入口
// ============================================================================
UObject_.Method("bool ImplementsInterface(const UClass InterfaceClass) const",
[](UObject* Object, UClass* InterfaceClass)
{
	if (Object == nullptr || InterfaceClass == nullptr)
		return false; // ★ 当前把空对象/空类直接压成普通 false
	return Object->GetClass()->ImplementsInterface(InterfaceClass);
});
```

[2] 当前 generated table 没有把 checked helper 暴露出来：

```csv
# ============================================================================
# 文件: Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv
# 位置: 2523-2524
# 说明: 引擎自带 checked helper 在脚本面仍只是 Stub
# ============================================================================
Engine,false,UKismetSystemLibrary,DoesClassImplementInterface,Stub,ERASE_NO_FUNCTION(),10
Engine,false,UKismetSystemLibrary,DoesImplementInterface,Stub,ERASE_NO_FUNCTION(),10
```

**差距描述**

- `能力缺失`：当前脚本 surface 没有等价于 `UKismetSystemLibrary::DoesClassImplementInterface()` 的 checked helper；一旦 `FindClass()`、热重载后的 lookup 或配置反射返回了错误 `UClass`，脚本侧只能看到 `false`。
- `实现质量差异`：现有 `ImplementsInterface()` 语义弱于同文件的 `IsA()`，后者在 `Class == nullptr` 时会显式 `Throw()`；interface query 反而把错误输入静默吞掉。
- `实现方式不同`：当前系统把“接口校验”放在调用者自己保证正确参数的前提上，而不是把 interface validity 作为 binding contract 的一部分。

**参考方案**

- **UnLua** 在 interface bridge 里显式校验“对象是否真的实现目标接口”，而不是把类型错误沉默处理：
  - `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:533-580`

[3] UnLua 的 `FInterfacePropertyDesc` 会把 interface mismatch 变成明确错误：

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp
// 结构: FInterfacePropertyDesc::CheckPropertyType
// 位置: interface bridge 的类型检查
// ============================================================================
virtual bool CheckPropertyType(lua_State* L, int32 IndexInStack, FString& ErrorMsg, void* UserData)
{
	UObject* Object = UnLua::GetUObject(L, IndexInStack);
	if (Object)
	{
		UClass* Class = Object->GetClass();
		if ((Class)
			&& (!Class->ImplementsInterface(InterfaceProperty->InterfaceClass)))
		{
			ErrorMsg = FString::Printf(
				TEXT("implements of interface %s is needed but got nil for object %s"),
				*InterfaceProperty->InterfaceClass->GetName(),
				*Class->GetName()); // ★ 直接把 interface mismatch 暴露成可诊断错误
			return false;
		}
	}

	return true;
};
```

**吸收建议**

- 不需要改引擎。先在插件内提取一个统一 `ValidateInterfaceClass(UClass*)` helper，给 `ImplementsInterface()`、未来 `FInterfaceProperty` bridge、以及 P10 后续的 interface callable path 复用。
- 对兼容性最保守的做法不是直接改旧 API，而是补一个 checked 变体，例如 `ImplementsInterfaceChecked()` 或等价全局 helper；旧 `ImplementsInterface()` 保留，但在 `InterfaceClass == nullptr` 或 `!CLASS_Interface` 时至少写 runtime warning。
- 如果后续 `UKismetSystemLibrary::*ImplementInterface` 仍然不能作为 generated binding 直接开放，也应在插件内提供同等语义，而不是继续让脚本作者手动区分“参数错了”和“类没实现接口”。

**优先级**

- `P1`
- 理由：这不是 `P10` 能力闭环本身，但它直接决定 `P10` 落地后的排障成本。当前主线仍应先把 native/interface/property/callable 接进主链；checked query contract 适合紧随其后补齐。

### [D6] 代码生成与 IDE 支持：interface forced-stub 正在直接扭曲 coverage 指标

**当前状态**

- `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:65-97` 的 exporter 只认 `TryBuild()` 成功或失败：成功就记 `reconstructedCount`，失败才进 `SkippedEntries.csv`。
- `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:465-479` 的 generator 则在 `ClassType == Interface || NativeInterface` 时无条件写 `ERASE_NO_FUNCTION()`，连 `TryBuild()` 都不看。
- 当前生成产物已经把这个 owner 分裂具象化了。本轮复核 `AS_FunctionTable_Entries.csv` 后确认：`UCameraLensEffectInterface`、`UInterface_AssetUserData`、`ULevelInstanceInterface`、`UNavMovementInterface`、`UTypedElementWorldInterface` 共 `44` 条 interface entry 被写成 `Stub`；对 `AS_FunctionTable_SkippedEntries.csv` 做同名全文检索，这 5 个类均为 `0` 行。
- 这种 forced-stub 已经开始扭曲模块指标：`.../AS_FunctionTable_ModuleSummary.csv:2` 显示 `Engine` 模块 `2108` 条 `Stub`；`:7` 显示 `GameplayTags` 模块 `35/35` 全部是 `Stub`；`:14` 显示 `EnhancedInput` 模块 `29` 条 `Stub`，其中 `UEnhancedInputSubsystemInterface` 单类就占 `26` 条，见 `.../AS_FunctionTable_Entries.csv:5560-5585`。

```
[D6-Delta] UHT Metric Drift
Exporter::CountBlueprintCallableFunctions
├─ TryBuild success -> reconstructedCount++        // 不写 reason ledger
└─ TryBuild fail    -> SkippedEntries + FailureReason

CodeGenerator::CollectEntries
├─ Interface / NativeInterface -> ERASE_NO_FUNCTION()
└─ Others -> TryBuild / ERASE_NO_FUNCTION()

Artifacts
├─ Entries.csv / ModuleSummary -> Stub
└─ SkippedEntries.csv          -> 可能没有对应行
```

[4] exporter 和 generator 现在不是同一个 decision owner：

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs
// 函数: CountBlueprintCallableFunctions
// 位置: exporter 只把 TryBuild 失败记入 skipped ledger
// ============================================================================
if (AngelscriptFunctionSignatureBuilder.TryBuild(classObj, function, out AngelscriptFunctionSignature? signature, out string? failureReason))
{
	_ = signature!.BuildEraseMacro();
	reconstructedCount++; // ★ 成功就只加 reconstructedCount
}
else
{
	skippedCount++;
	skippedEntries.Add(new AngelscriptSkippedFunctionEntry(
		moduleName,
		classObj.SourceName,
		function.SourceName,
		string.IsNullOrEmpty(failureReason) ? "unknown" : failureReason)); // ★ 只有失败才进 skipped ledger
}
```

[5] code generator 对 interface class 的 blanket rule 会直接绕过上面的 reason ledger：

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs
// 函数: CollectEntries
// 位置: interface / native interface 的 forced-stub 分支
// ============================================================================
string eraseMacro;
if (classObj.ClassType == UhtClassType.Interface || classObj.ClassType == UhtClassType.NativeInterface)
{
	eraseMacro = "ERASE_NO_FUNCTION()"; // ★ blanket rule：没有 reason code，也不走 TryBuild 结果
}
else if (AngelscriptFunctionSignatureBuilder.TryBuild(classObj, function, out AngelscriptFunctionSignature? signature, out string? _))
{
	eraseMacro = signature!.BuildEraseMacro();
}
else
{
	eraseMacro = "ERASE_NO_FUNCTION()";
}
```

[6] 当前产物里已经能看到 interface forced-stub 对模块 coverage 的实际污染：

```csv
# ============================================================================
# 文件: Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_ModuleSummary.csv
# 位置: 2, 7, 14
# 说明: 模块 direct/stub rate 已经被 interface forced-stub 放大
# ============================================================================
Engine,false,4054,1946,2108,0.480019733596448,0.519980266403552,16
GameplayTags,false,35,0,35,0,1,1
EnhancedInput,false,87,58,29,0.666666666666667,0.333333333333333,1
```

[7] `UGameplayTagAssetInterface` 与 `UEnhancedInputSubsystemInterface` 是两个最小、最直观的当前样本：

```csv
# ============================================================================
# 文件: Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv
# 位置: 4308-4310, 5560-5566
# 说明: interface class 当前都被直接写成 Stub
# ============================================================================
GameplayTags,false,UGameplayTagAssetInterface,HasAllMatchingGameplayTags,Stub,ERASE_NO_FUNCTION(),1
GameplayTags,false,UGameplayTagAssetInterface,HasAnyMatchingGameplayTags,Stub,ERASE_NO_FUNCTION(),1
GameplayTags,false,UGameplayTagAssetInterface,HasMatchingGameplayTag,Stub,ERASE_NO_FUNCTION(),1
EnhancedInput,false,UEnhancedInputSubsystemInterface,AddMappingContext,Stub,ERASE_NO_FUNCTION(),1
EnhancedInput,false,UEnhancedInputSubsystemInterface,AddTagToInputMode,Stub,ERASE_NO_FUNCTION(),1
EnhancedInput,false,UEnhancedInputSubsystemInterface,AppendTagsToInputMode,Stub,ERASE_NO_FUNCTION(),1
EnhancedInput,false,UEnhancedInputSubsystemInterface,ClearAllMappings,Stub,ERASE_NO_FUNCTION(),1
EnhancedInput,false,UEnhancedInputSubsystemInterface,GetAllPlayerMappableActionKeyMappings,Stub,ERASE_NO_FUNCTION(),1
EnhancedInput,false,UEnhancedInputSubsystemInterface,GetInputMode,Stub,ERASE_NO_FUNCTION(),1
EnhancedInput,false,UEnhancedInputSubsystemInterface,GetUserSettings,Stub,ERASE_NO_FUNCTION(),1
```

**差距描述**

- `没有实现`：当前 artifact schema 里没有 `ForcedStubReason`、`DecisionKind` 或等价字段来表达 “这个符号不是 `TryBuild` 失败，而是 policy 强制 stub”。
- `实现差异`：exporter 与 generator 同时存在，但 decision 不是同源。一个只看 `TryBuild`，另一个先按 `Interface/NativeInterface` blanket rule 短路。
- `实现质量差异`：模块级 `directBindRate/stubRate` 现在混入了大量“无 reason 的 forced-stub”，直接削弱了 `Bind API GAP` 的量化价值。

**参考方案**

- **puerts** 把“当前不支持”也保留在同一个 declaration artifact 里，而不是把 unsupportedness 丢到旁路：
  - `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1174-1224`

[8] puerts 借鉴点不是“全部支持”，而是 unsupported 也进入同一个 artifact contract：

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp
// 函数: FTypeScriptDeclarationGenerator::GenResolvedFunctions
// 位置: unsupported super overload 仍写进声明产物
// ============================================================================
if (!Overloads.Contains(*SuperOverloadIter))
{
	Buff << "    /**\n";
	Buff << "     * @deprecated Unsupported super overloads.\n";
	Buff << "     */\n";
	Buff << "    " << *SuperOverloadIter << ";\n"; // ★ unsupported 仍在同一个产物里有明确语义
}
```

**吸收建议**

- 第一阶段不要急着让 interface 直接 `Direct`；先把 forced-stub 变成有身份的 decision，例如 `EntryKind = ForcedStub.InterfaceClass`，并在 summary/CSV/未来 IDE manifest 中统一复用。
- exporter 与 generator 应共享一份最小 `SymbolDecision`，字段至少包含 `SymbolKey`、`DecisionKind`、`ReasonCode`、`Phase(UHTExport/UHTGenerate)`；这样 `TryBuild` 成功但 policy 拒绝的情况不再沉默。
- `directBindRate/stubRate` 不应继续把 forced-stub 当成普通健康度分母。对 `P10` 更有用的是“可行动 direct / 可诊断 skip / policy-forced stub”三分口径。

**优先级**

- `P0`
- 理由：这不是 interface capability 本身，但它直接决定 `Bind API GAP` 是否还能被可靠量化。若继续用当前混杂口径推进 `P10`，回归验收会先被错误指标误导。

### [D9] 测试基础设施：现有回归只校验 CSV 形状，不校验 interface decision parity

**当前状态**

- `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp:551-579` 只断言 summary 里存在 `directBindEntries/stubEntries/directBindRate/stubRate`，以及 `direct + stub == total`。
- 同文件 `:684-748` 只断言 skipped csv 有正确表头、至少一条非空 `FailureReason`，以及 reason summary 聚合和 skipped 行数对齐。
- 这组回归没有任何 interface-specific assertion：不会挑一个 forced-stub symbol 去验证它是否在 `Entries.csv` 和 `SkippedEntries.csv` 上给出同一份 decision，也不会验证 `Summary.json` 是否把 forced-stub 从“健康失败”里剥离出来。

```
[D9-Delta] Current UHT Regression
SummaryJsonTest
├─ field exists
└─ direct + stub == total

SkippedCsvTest
├─ header exists
└─ somewhere has non-empty reason

Missing
├─ interface forced-stub has explicit reason / decision kind
├─ same symbol parity across Entries / Skipped / Summary
└─ P10 closeout can diff one symbol's status change
```

[9] 当前 summary test 只看算术，不看 interface decision 的语义正确性：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp
// 函数: Summary JSON 回归
// 位置: 只校验 direct/stub/total 的算术关系
// ============================================================================
if (!TestTrue(TEXT("Generated function table summary test should expose per-module directBindEntries"),
	(*ModuleObject)->TryGetNumberField(TEXT("directBindEntries"), ModuleDirectBindEntries)))
{
	return false;
}

if (!TestTrue(TEXT("Generated function table summary test should expose per-module stubEntries"),
	(*ModuleObject)->TryGetNumberField(TEXT("stubEntries"), ModuleStubEntries)))
{
	return false;
}

if (!TestEqual(TEXT("Generated function table summary test should keep module totals aligned"),
	ModuleEntries, ModuleDirectBindEntries + ModuleStubEntries))
{
	return false; // ★ 只要算术自洽，forced-stub 被当成普通 stub 也会绿灯
}
```

[10] 当前 skipped csv test 只要求 “至少有某条 reason”，不会盯 interface forced-stub：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp
// 函数: Skipped CSV / SkippedReasonSummary CSV 回归
// 位置: 只校验表头、列数和聚合
// ============================================================================
bool bFoundFailureReason = false;
for (int32 LineIndex = 1; LineIndex < SkippedLines.Num(); ++LineIndex)
{
	TArray<FString> Columns;
	SkippedLines[LineIndex].ParseIntoArray(Columns, TEXT(","), false);
	if (!TestTrue(TEXT("Generated function table skipped csv rows should expose four columns"), Columns.Num() == 4))
	{
		return false;
	}

	if (!Columns[3].IsEmpty())
	{
		bFoundFailureReason = true;
	}
}

TestTrue(TEXT("Generated function table skipped csv rows should include non-empty failure reasons"), bFoundFailureReason);
// ★ 这里没有任何“某个 interface stub 必须有同 symbol 的 reason / decision”断言
```

**差距描述**

- `没有实现`：当前没有 issue-scoped regression 去锁 `interface forced-stub must be reasonful` 或 `same symbol parity across artifacts`。
- `实现质量差异`：现有测试更像 schema smoke test，而不是 `P10` closeout gate；它们保证 CSV 文件存在，却不保证 decision contract 可对账。
- `实现方式不同`：当前测试把 `Summary`、`Entries`、`SkippedEntries` 当三份独立报表看，而不是把它们视为同一 symbol decision 的三个投影。

**参考方案**

- **puerts** 的 declaration generator 把 unsupportedness 直接写进单一 artifact，天然适合做 artifact-level regression：
  - `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1174-1224`

**吸收建议**

- 新增一组 interface-specific parity regression，首批最小样本建议直接锁：
  - `UGameplayTagAssetInterface::HasMatchingGameplayTag`，因为当前 `Entries.csv:4308-4310` 已明确是 forced-stub。
  - `UEnhancedInputSubsystemInterface::AddMappingContext`，因为当前 `Entries.csv:5560-5585` 会显著放大模块级 stub rate。
- 断言不应再停在“文件存在”。应要求同一 `SymbolKey` 要么：
  - 在 `SkippedEntries.csv` 中有明确 `FailureReason`；或
  - 在 `Entries.csv/Summary.json` 中被标成 `ForcedStub.InterfaceClass` 之类的显式 decision。
- 等 `P10` 进入 closeout 阶段后，再加一条回归：某个 interface symbol 从 `ForcedStub` 变为 `Direct/Supported` 时，`Entries.csv`、`Summary.json`、IDE manifest 三侧必须同步变化。

**优先级**

- `P1`
- 理由：这条不阻塞写能力代码，但会决定 `P10` 何时能被可信地宣告完成。没有 parity regression，就会继续出现“指标已变、原因没变、测试仍绿”的假收敛。

### 值得吸收的设计模式（增量）

- `Checked query, not silent false`：interface surface 不应把 invalid input 与真实 negative result 合并成同一个布尔值。至少要有一条 checked contract，供 property/query/callable 统一复用。
- `One symbol, one decision`：同一个 symbol 的 `Supported / Skipped / ForcedStub` 不应由 exporter、generator、runtime、IDE 各自再判断一遍；应先生成一个稳定 decision，再把它投影到不同产物。
- `Parity regression over artifact arithmetic`：对 `P10` 这类主线，测试应锁“同一 symbol 的 decision 是否一致”，而不是只锁 CSV 有表头、计数能相加。

### 改进路线建议（增量）

1. `P0`：先收口 `D6` 的 decision owner。给 `AngelscriptFunctionTableExporter` 与 `AngelscriptFunctionTableCodeGenerator` 引入共享 `SymbolDecision`，并把 `Interface/NativeInterface` 的 blanket stub 升级成显式 `ForcedStub.InterfaceClass`。
2. `P1`：基于新的 decision contract 扩展 `GeneratedFunctionTable` 回归，最少覆盖 `UGameplayTagAssetInterface` 与 `UEnhancedInputSubsystemInterface` 两个真实样本，禁止继续出现“Entries 有 stub、Skipped 无 reason、Summary 仍算进健康 stub rate”的状态。
3. `P1`：补 interface checked query helper，把 `ImplementsInterface(nullptr / non-interface)` 从静默 `false` 升级成可诊断路径，并让未来 `FInterfaceProperty` / `TScriptInterface<>` bridge 复用同一校验逻辑。

---
## 深化分析 (2026-04-09 00:11:11)

### 本轮新增关键发现

- `D6` 新发现不是“还有若干 raw 名字没美化”，而是 **function-level `ScriptMethod` contract 还没有进入 generated artifact**。当前运行时测试、引擎头文件与 `AS_FunctionTable_*` 看到的是三套身份。
- `D3` 新发现是 **`BlueprintCosmetic` 这类执行边界没有进入 Angelscript 的统一 callable contract**。`UUserWidget::AddToViewport()` 这类 API 在脚本面被当成普通 `Direct`，与 Blueprint 原语义脱节。
- `D4` 新发现是 **interface hot-reload 的 stale 风险不是单点 debug 问题，而是同一份 raw `asITypeInfo::UserData` 同时喂给 property、type lookup、debug 三个消费者**。当前已经具备 typed user-data API，但插件还没把它用成受控注册表。

### 差距矩阵（增量）

| 维度 | 无差距 | 实现差异 | 能力缺失 | 本轮判定 |
| --- | --- | --- | --- | --- |
| `D6` 代码生成与 IDE 支持：`ScriptMethod` contract |  |  | `✓` | **能力缺失**：generated table / bind-db / IDE 产物没有 function-level script-facing identity |
| `D3` Blueprint 交互：`BlueprintCosmetic` 执行语义 |  |  | `✓` | **能力缺失**：Blueprint 执行边界没有进入 callable metadata 与运行时 guard |
| `D4` 热重载：interface type lifetime |  | `✓` |  | **实现差异**：已有 reload 与 cleanup，但 owner 仍是 raw `UserData`，没有显式 unregister / tombstone |

### [D6] 代码生成与 IDE 支持：`ScriptMethod` 仍停在零散 helper/test，没进入 generated artifact contract

**当前状态**

- `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h:16-24, 51-56, 268-314` 当前仍只把 `ScriptName`、`ScriptMixin` 当作签名层元数据；这里没有 `ScriptMethod`、`ScriptMethodSelfReturn` 或等价字段。
- `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs:8-15, 90-97` 的 generated signature 只记录 `OwningType`、`FunctionName`、`ReturnType`、`ParameterTypes`、`IsStatic`、`IsConst`。它不知道“脚本侧 owner 是谁”“脚本侧名字是什么”“首参是否应折叠成 receiver”。
- 引擎原始 `UFUNCTION` 已经在 `EngineElementsLibrary.h` 与 `KismetSystemLibrary.h` 上写了 `ScriptMethod`，但 `AS_FunctionTable_*` 产物仍输出 raw `K2_...` / `Conv_...` 身份。这意味着 `Bind API GAP` 当前统计的是 **source symbol**，不是 **script-facing symbol**。
- 仓库内测试仍把 `ScriptMethod` 当作 contract：`AngelscriptUhtCoverageTestTypes.h:31-32` 声明了 `meta=(ScriptMethod)`，`AngelscriptBindConfigTests.cpp:639-644` 还在要求首参折叠与成员式声明。

```
[D6-New] ScriptMethod Contract Split
Engine UFUNCTION metadata
├─ ScriptMethod="AcquireEditorElementHandle" / "ToString"   // 引擎已经给出脚本侧别名
├─ runtime test still expects member-style declaration      // 当前测试仍承认这份 contract
└─ current generated artifact
   ├─ SignatureBuilder -> only SourceName / IsStatic / IsConst
   ├─ AS_FunctionTable_* -> raw K2_ / Conv_ names
   └─ IDE / GAP audit -> 看到的是源函数身份，不是脚本身份
```

[1] 当前 runtime helper 常量区和持久化结构都没有 `ScriptMethod` / `ScriptMethodSelfReturn`：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h
// 位置: 16-24, 51-56, 379-394
// 说明: 当前签名层只承认 ScriptName / ScriptMixin，不承认 function-level ScriptMethod
// ============================================================================
static const FName NAME_Signature_ScriptName("ScriptName");
static const FName NAME_Signature_ScriptMixin("ScriptMixin");
static const FName NAME_Signature_ScriptTrivial("ScriptTrivial");
static const FName NAME_Signature_NotAngelscriptProperty("NotAngelscriptProperty");

bool bGlobalScope = false;
bool bNotAngelscriptProperty = false;
bool bTrivial = false;
bool bBlueprintProtected = false;
FString ScriptName;

// ★ 写入 bind-db 时也只有 Declaration / ClassName / ScriptName 等字段
DBBind.Declaration = Declaration;
DBBind.UnrealPath = Function->GetName();
if (bStaticInUnreal)
	DBBind.ClassName = ClassName;
...
if (Function->HasAnyFunctionFlags(FUNC_BlueprintEvent))
	DBBind.ScriptName = ScriptName;
```

[2] generated signature model 只保留 raw source identity：

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs
// 位置: 8-15, 90-97
// 说明: UHT 生成侧没有脚本 owner / 脚本函数名 / 首参 receiver policy
// ============================================================================
internal sealed record AngelscriptFunctionSignature(
	string OwningType,
	string FunctionName,
	string ReturnType,
	IReadOnlyList<string> ParameterTypes,
	bool IsStatic,
	bool IsConst,
	bool UseExplicitSignature)

signature = new AngelscriptFunctionSignature(
	classObj.SourceName,      // ★ 只记录源类名
	function.SourceName,      // ★ 只记录源函数名
	returnType,
	parameterTypes,
	HasFunctionFlag(function, "Static"),
	HasFunctionFlag(function, "Const"),
	true);
```

[3] 引擎头已经给出 `ScriptMethod`，但当前产物仍回退成 raw `K2_` / `Conv_`：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UERelease\Engine\Source\Runtime\Engine\Public\Elements\Framework\EngineElementsLibrary.h
// 位置: 37-39, 50-52, 61-63, 72-74
// 文件: J:\UnrealEngine\UERelease\Engine\Source\Runtime\Engine\Classes\Kismet\KismetSystemLibrary.h
// 位置: 2223-2232
// 说明: 引擎原始 metadata 已经明确声明了脚本侧方法名
// ============================================================================
UFUNCTION(BlueprintCallable, BlueprintPure=false, Category="TypedElementFramework|Object", meta=(DisplayName="Acquire Editor Object Element Handle", ScriptMethod="AcquireEditorElementHandle"))
static ENGINE_API FScriptTypedElementHandle K2_AcquireEditorObjectElementHandle(const UObject* Object, const bool bAllowCreate = true);

UFUNCTION(BlueprintPure, meta = (DisplayName = "To String (PrimaryAssetId)", CompactNodeTitle = "->", ScriptMethod="ToString", BlueprintThreadSafe, BlueprintAutocast), Category = "AssetManager")
static ENGINE_API FString Conv_PrimaryAssetIdToString(FPrimaryAssetId PrimaryAssetId);
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Engine_004.cpp
// 位置: 364-367
// 文件: Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv
// 位置: 1167-1170, 2509
// 说明: 当前 generated artifact 仍把 source-level raw name 当最终脚本身份
// ============================================================================
FAngelscriptBinds::AddFunctionEntry(UEngineElementsLibrary::StaticClass(), "K2_AcquireEditorActorElementHandle", { ERASE_AUTO_FUNCTION_PTR(UEngineElementsLibrary::K2_AcquireEditorActorElementHandle) });
FAngelscriptBinds::AddFunctionEntry(UEngineElementsLibrary::StaticClass(), "K2_AcquireEditorComponentElementHandle", { ERASE_AUTO_FUNCTION_PTR(UEngineElementsLibrary::K2_AcquireEditorComponentElementHandle) });
FAngelscriptBinds::AddFunctionEntry(UEngineElementsLibrary::StaticClass(), "K2_AcquireEditorObjectElementHandle", { ERASE_AUTO_FUNCTION_PTR(UEngineElementsLibrary::K2_AcquireEditorObjectElementHandle) });
FAngelscriptBinds::AddFunctionEntry(UEngineElementsLibrary::StaticClass(), "K2_AcquireEditorSMInstanceElementHandle", { ERASE_AUTO_FUNCTION_PTR(UEngineElementsLibrary::K2_AcquireEditorSMInstanceElementHandle) });

Engine,false,UEngineElementsLibrary,K2_AcquireEditorObjectElementHandle,Direct,ERASE_AUTO_FUNCTION_PTR(UEngineElementsLibrary::K2_AcquireEditorObjectElementHandle),5
Engine,false,UKismetSystemLibrary,Conv_PrimaryAssetIdToString,Stub,ERASE_NO_FUNCTION(),10
```

[4] 仓库内测试仍把 `ScriptMethod` 当作既定 contract：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptUhtCoverageTestTypes.h
// 位置: 31-32
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp
// 位置: 639-644
// 说明: 测试希望 function-level ScriptMethod 仍把首参折叠成脚本成员调用
// ============================================================================
UFUNCTION(BlueprintCallable, meta = (ScriptMethod, DisplayName = "Get Coverage Value"))
static int32 GetCoverageValue(const UObject* Target);

FAngelscriptFunctionSignature Signature(HostType.ToSharedRef(), ScriptMethodFunction);
TestTrue(TEXT("FunctionLevelScriptMethodUsesFirstParameterAsMixin should keep the Unreal function static"), Signature.bStaticInUnreal);
TestFalse(TEXT("FunctionLevelScriptMethodUsesFirstParameterAsMixin should bind ScriptMethod functions as script members"), Signature.bStaticInScript);
TestEqual(TEXT("FunctionLevelScriptMethodUsesFirstParameterAsMixin should remove the first parameter from the exposed signature"), Signature.ArgumentTypes.Num(), 0);
```

**差距描述**

- `没有实现`：当前 generated binding schema 没有 `ScriptOwnerClass`、`ScriptFunctionName`、`BindingKind(ScriptMethod/Static/Method)`、`ScriptMethodSelfReturn` 这类 function-level contract 字段。
- `实现方式不同`：runtime/test 仍保留少量 `ScriptMethod` 预期，但 UHT tool、`AS_FunctionTable_*`、bind-db/IDE 产物全都按 raw `UFunction` 身份输出。
- `实现质量差异`：`Bind API GAP` 和 IDE surface 现在统计的是 `K2_AcquireEditorObjectElementHandle`、`Conv_PrimaryAssetIdToString` 这类 source 名，不是脚本最终可见名，导致 gap closure 会针对错误 symbol 验收。

**参考方案**

- **UnrealCSharp** 把 `ScriptMethod` / `ScriptMethodSelfReturn` 升级成 reflection registry 的一等元数据类，再统一喂给动态生成流程：
  - `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Reflection/FReflectionRegistry.cpp:776-780, 2284-2291`
  - `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGeneratorCore.cpp:1252-1273`
- **puerts** 把 `ScriptMethod` / `ScriptMethodSelfReturn` 暴露到脚本装饰器声明层，让 script-facing contract 先有稳定 artifact：
  - `Reference/puerts/unreal/Puerts/Typing/ue/puerts_decorators.d.ts:1091-1099`

[5] UnrealCSharp 把 `ScriptMethod` / `ScriptMethodSelfReturn` 当成 registry 输入，而不是临时字符串判断：

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Reflection/FReflectionRegistry.cpp
// 位置: 776-780, 2284-2291
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGeneratorCore.cpp
// 位置: 1252-1273
// 说明: 参考实现先把 metadata 建成稳定 registry，再喂给后续生成阶段
// ============================================================================
ScriptMethodAttributeClass = GetClass(
	COMBINE_NAMESPACE(NAMESPACE_ROOT, NAMESPACE_DYNAMIC), CLASS_SCRIPT_METHOD_ATTRIBUTE);

ScriptMethodSelfReturnAttributeClass = GetClass(
	COMBINE_NAMESPACE(NAMESPACE_ROOT, NAMESPACE_DYNAMIC), CLASS_SCRIPT_METHOD_SELF_RETURN_ATTRIBUTE);

FClassReflection* FReflectionRegistry::GetScriptMethodAttributeClass() const
{
	return ScriptMethodAttributeClass;
}

FClassReflection* FReflectionRegistry::GetScriptMethodSelfReturnAttributeClass() const
{
	return ScriptMethodSelfReturnAttributeClass;
}

ReflectionRegistry.GetScriptMethodAttributeClass(),
ReflectionRegistry.GetScriptMethodSelfReturnAttributeClass(),
```

[6] puerts 至少保证 declaration artifact 不会把这两类 metadata 隐身：

```typescript
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Typing/ue/puerts_decorators.d.ts
// 位置: 1091-1099
// 说明: 参考实现把 function-level metadata 暴露为脚本装饰器 contract
// ============================================================================
let ScriptMethod: MetaKey;

/**
 * @brief
 *      [FunctionMetadata] Used with ScriptMethod to denote that the return value of the function should overwrite the value of the instance that made the call.
 */
let ScriptMethodSelfReturn: MetaKey;
```

**吸收建议**

- 在不改引擎、只改插件的前提下，把 `ScriptMethod` contract 提升成 plugin 内统一 schema：
  - runtime：`FAngelscriptFunctionSignature`
  - cooked/cache：`FAngelscriptMethodBind`
  - UHT：`AngelscriptFunctionSignatureBuilder` / `AS_FunctionTable_*`
  - IDE：`Entries.csv` / `Summary.json` / 未来 manifest
- 最小字段建议：
  - `ScriptOwnerClass`
  - `ScriptFunctionName`
  - `BindingKind`：`Static` / `Method` / `ScriptMethod`
  - `SelfPolicy`：`None` / `RemoveFirstParam` / `SelfReturn`
- 第一阶段先把 symbol identity 纠正，不必一上来追求全部 direct bind。即便 `Conv_PrimaryAssetIdToString` 仍是 `Stub`，artifact 也必须先显示它的 script-facing 名字是 `ToString`，而不是 raw `Conv_...`。

**优先级**

- `P1`
- 理由：它不先于 `P10 UInterface`，但它直接决定 `Bind API GAP` 是否在对的 symbol 上闭环。建议放在 interface decision owner 收口之后、全面补 API 之前实施。

### [D3] Blueprint 交互：`BlueprintCosmetic` 还没有进入 Angelscript 的统一 callable 语义

**当前状态**

- `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:1412, 1641-1644` 与 `Core/AngelscriptEngine.h:990-991, 1044` 只为 `BlueprintAuthorityOnly` 建了字段与比较语义；当前源码里不存在 `BlueprintCosmetic`、`UnsafeDuringActorConstruction` 的对应字段。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h:16-31, 414-459` 只会把 `WorldContext`、`DeterminesOutputType`、`NotAngelscriptProperty`、`BlueprintProtected`、`Deprecated`、`EditorOnly` 回写到 script function；执行边界元数据没有 owner。
- 引擎原始 `UUserWidget::AddToViewport()` / `AddToPlayerScreen()` / `RemoveFromViewport()` 都带 `BlueprintCosmetic`，但当前 generated function table 和手写 bind 都把它们当普通 `Direct` 暴露。

```
[D3-New] Execution Semantic Loss
Engine UFUNCTION
├─ BlueprintCosmetic                         // 原始 API 已声明执行边界
└─ should not run on dedicated server

Current Angelscript paths
├─ Preprocessor / FunctionDesc
│  └─ only BlueprintAuthorityOnly           // 没有 BlueprintCosmetic owner
├─ Helper_FunctionSignature::ModifyScriptFunction
│  └─ no execution semantic write-back
├─ AS_FunctionTable_* / manual binds
│  └─ direct callable
└─ script side sees unconditional method
```

[7] 当前 script function contract 没有 `BlueprintCosmetic` 入口：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp
// 位置: 1412, 1641-1644
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h
// 位置: 990-991, 1044
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 位置: 3474-3475
// 说明: 当前 pipeline 只显式持久化了 BlueprintAuthorityOnly
// ============================================================================
static FName PP_NAME_BlueprintAuthorityOnly("BlueprintAuthorityOnly");

else if (Spec.Name == PP_NAME_BlueprintAuthorityOnly)
{
	FunctionDesc->bBlueprintAuthorityOnly = true;
}

bool bBlueprintAuthorityOnly = false;
...
&& Other.bBlueprintAuthorityOnly == bBlueprintAuthorityOnly

if (FunctionDesc->bBlueprintAuthorityOnly)
	NewFunction->FunctionFlags |= FUNC_BlueprintAuthorityOnly;
```

[8] `ModifyScriptFunction()` 当前只处理 world-context / property / protection / deprecated / editor-only：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h
// 位置: 414-459
// 说明: 当前没有 BlueprintCosmetic / UnsafeDuringActorConstruction 等执行边界回写
// ============================================================================
if (WorldContextArgument != -1)
{
	ScriptFunction->hiddenArgumentIndex = WorldContextArgument;
	ScriptFunction->hiddenArgumentDefault = "__WorldContext()";
}

if (DeterminesOutputTypeArgument != -1)
{
	ScriptFunction->determinesOutputTypeArgumentIndex = DeterminesOutputTypeArgument;
}

if (bNotAngelscriptProperty)
{
	ScriptFunction->SetProperty(false);
}

if (bBlueprintProtected)
{
	ScriptFunction->SetProtected(true);
}

if (bDeprecated)
{
	ScriptFunction->traits.SetTrait(asTRAIT_DEPRECATED, true);
	ScriptFunction->deprecationMessage = TCHAR_TO_UTF8(*DeprecationMessage);
}

if (IsFunctionEditorOnly())
{
	ScriptFunction->traits.SetTrait(asTRAIT_EDITOR_ONLY, true);
}
```

[9] 引擎头已经声明 `BlueprintCosmetic`，但当前脚本绑定仍把这些 API 当普通 direct callable：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UERelease\Engine\Source\Runtime\UMG\Public\Blueprint\UserWidget.h
// 位置: 344-361
// 文件: Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_UMG_001.cpp
// 位置: 292-293, 329
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UUserWidget.cpp
// 位置: 76
// 说明: Blueprint 原语义是 cosmetic，当前 script surface 却是无条件 direct
// ============================================================================
UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category="User Interface|Viewport", meta=( AdvancedDisplay = "ZOrder" ))
UMG_API void AddToViewport(int32 ZOrder = 0);

UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category="User Interface|Viewport", meta=( AdvancedDisplay = "ZOrder" ))
UMG_API bool AddToPlayerScreen(int32 ZOrder = 0);

UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category="User Interface|Viewport", meta=( DeprecatedFunction, DeprecationMessage="Use RemoveFromParent instead" ))
UMG_API void RemoveFromViewport();

FAngelscriptBinds::AddFunctionEntry(UUserWidget::StaticClass(), "AddToPlayerScreen", { ERASE_AUTO_METHOD_PTR(UUserWidget, AddToPlayerScreen) });
FAngelscriptBinds::AddFunctionEntry(UUserWidget::StaticClass(), "AddToViewport", { ERASE_AUTO_METHOD_PTR(UUserWidget, AddToViewport) });
FAngelscriptBinds::AddFunctionEntry(UUserWidget::StaticClass(), "RemoveFromViewport", { ERASE_AUTO_METHOD_PTR(UUserWidget, RemoveFromViewport) });

UUserWidget_.Method("void AddToViewport(int32 ZOrder = 0)", METHOD_TRIVIAL(UUserWidget, AddToViewport));
```

**差距描述**

- `没有实现`：当前没有等价的 `BlueprintCosmetic` callable metadata，也没有统一 runtime guard 去保护 dedicated server / non-cosmetic 场景。
- `实现方式不同`：Blueprint 原生把它表达成 `UFunction` flag；current 的 generated bind 与 manual bind 则直接绕过这层语义，平铺成普通 direct call。
- `实现质量差异`：这会让 Angelscript API surface 看起来比 Blueprint 更“宽”，但这种宽不是能力增强，而是执行边界丢失。

**参考方案**

- **puerts** 先把 `BlueprintAuthorityOnly` / `BlueprintCosmetic` 收成稳定 flag，再暴露到脚本侧装饰器 contract：
  - `Reference/puerts/unreal/Puerts/PuertsEditor/UEMeta.ts:1168-1183`
  - `Reference/puerts/unreal/Puerts/Typing/ue/puerts_decorators.d.ts:831-837`
- **UnrealCSharp** 也把 `BlueprintCosmetic` 作为 reflection registry 的正式属性类，而不是散落字符串：
  - `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Reflection/FReflectionRegistry.cpp:1618-1620`

[10] puerts 的做法值得借鉴之处不是“已经全部强制执行”，而是 **先把语义字段做成一等公民**：

```typescript
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/PuertsEditor/UEMeta.ts
// 位置: 1168-1183
// 文件: Reference/puerts/unreal/Puerts/Typing/ue/puerts_decorators.d.ts
// 位置: 831-837
// 说明: 参考实现至少不会让 BlueprintCosmetic 在脚本声明层消失
// ============================================================================
case 'BlueprintAuthorityOnly'.toLowerCase():
	FunctionFlags |= BigInt(UE.FunctionFlags.FUNC_BlueprintAuthorityOnly);
	break;

case 'BlueprintCosmetic'.toLowerCase():
	FunctionFlags |= BigInt(UE.FunctionFlags.FUNC_BlueprintCosmetic);
	break;

let BlueprintAuthorityOnly: FunctionKey;
let BlueprintCosmetic: FunctionKey;
```

**吸收建议**

- 不建议把 `BlueprintCosmetic` 单独补成又一条散乱布尔位。更可持续的做法是引入统一 `InvocationConstraint` / `FunctionSemanticFlags`：
  - `BlueprintAuthorityOnly`
  - `BlueprintCosmetic`
  - `UnsafeDuringActorConstruction`
  - 后续其他执行边界
- 先做三层同步，不需要改引擎：
  - `FAngelscriptFunctionSignature` / `FAngelscriptMethodBind`
  - `AngelscriptFunctionSignatureBuilder` / `AS_FunctionTable_*`
  - `ModifyScriptFunction()` 与 manual bind helper
- 第一阶段目标不是马上做复杂运行时拒绝，而是先让 artifact、IDE、docs、bind-gap 报表能看见 `Cosmetic`。第二阶段再把 dedicated server guard 接到统一调用入口，避免每个 `Bind_*.cpp` 自己重复判断。

**优先级**

- `P2`
- 理由：这是明确能力缺失，但它不应插队到 `P10 UInterface` 前面。更合适的顺序是先把 interface 主线和 symbol identity 收口，再补这条 Blueprint 执行语义闭环。

### [D4] 热重载：interface type lifetime 仍由 raw `asITypeInfo::UserData` 驱动，没有显式 unregister owner

**当前状态**

- `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:1092-1154` 与 `ClassGenerator/AngelscriptClassGenerator.cpp:2596-2611` 都会在 interface register/materialize 阶段把 `UClass*` 直接挂进 `asITypeInfo::UserData`。
- `ClassGenerator/AngelscriptClassGenerator.cpp:4990-5030` 的 `CleanupRemovedClass()` 只清 UE 侧 `UASClass` / `UASStruct` 状态，没有清 `ClassDesc->ScriptType`，也没有对 engine-level `asITypeInfo` 执行 `SetUserData(nullptr)`。
- 同一份 raw user data 现在被至少三类消费者复用：
  - `Bind_BlueprintType.cpp:133-139, 165-175`：property materialization / property match
  - `Core/AngelscriptType.cpp:220-227`：script type -> `UClass*`
  - `Debugging/AngelscriptDebugServer.cpp:1885-1893`：debug database keywords/source
- 当前 fork 自带 typed user-data API：`ThirdParty/angelscript/source/as_typeinfo.h:137-139` 已经支持 `SetUserData(void*, asPWORD type)` / `GetUserData(asPWORD type)`。所以 raw `plainUserData` 不是引擎限制，而是当前插件的 owner 选择。

```
[D4-New] Interface Lifetime Fan-out
Preprocessor / ClassGenerator
├─ RegisterObjectType(interface)
├─ SetUserData(UClass*)                          // raw plainUserData
└─ ClassDesc->ScriptType = InterfaceScriptType

CleanupRemovedClass
└─ clear UASClass / UASStruct only               // 没有 unregister / tombstone

Live consumers after remove/recreate
├─ Bind_BlueprintType -> PropertyClass
├─ FAngelscriptTypeUsage -> UClass lookup
└─ DebugServer -> keywords / doc source
```

[11] 当前 interface register path 直接把 `UClass*` 写进 raw `UserData`：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp
// 位置: 1092-1154
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 位置: 2596-2611
// 说明: interface 生命周期当前主要靠 ScriptType + raw UserData 粘连
// ============================================================================
int TypeId = Engine.Engine->RegisterObjectType(
	TCHAR_TO_ANSI(*InterfaceName),
	0,
	asOBJ_REF | asOBJ_NOCOUNT | asOBJ_IMPLICIT_HANDLE);

if (InterfaceScriptType != nullptr)
{
	ClassDesc->ScriptType = InterfaceScriptType;
	...
	ScriptFunc->SetUserData(Sig, 0);
}

if (ClassDesc->bIsInterface && ScriptType == nullptr)
{
	...
	if (InterfaceScriptType != nullptr)
	{
		InterfaceScriptType->SetUserData(NewClass); // ★ 这里把 live UClass 直接塞进 plain user data
		ClassDesc->ScriptType = InterfaceScriptType;
	}
}
```

[12] cleanup 没有对应 unregister，而消费者继续直接读 `GetUserData()`：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 位置: 4990-5030
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp
// 位置: 133-139, 165-175
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp
// 位置: 220-227
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 位置: 1885-1893
// 说明: 同一份 raw user data 当前同时喂给 property / type / debug
// ============================================================================
void FAngelscriptClassGenerator::CleanupRemovedClass(TSharedPtr<FAngelscriptClassDesc> ClassDesc)
{
	UASClass* Class = (UASClass*)ClassDesc->Class;
	if (Class != nullptr)
	{
		Class->ScriptTypePtr = nullptr;
		Class->ConstructFunction = nullptr;
		Class->DefaultsFunction = nullptr;
		...
	}

	UASStruct* Struct = (UASStruct*)ClassDesc->Struct;
	if (Struct != nullptr)
	{
		Struct->ScriptType = nullptr;
		Struct->UpdateScriptType();
	}
	// ★ 这里没有对 ClassDesc->ScriptType / asITypeInfo 调 SetUserData(nullptr)
}

Property->PropertyClass = (UClass*)Usage.ScriptClass->GetUserData();
...
UClass* AssociatedClass = (UClass*)Usage.ScriptClass->GetUserData();
...
return (UClass*)ScriptClass->GetUserData();
...
UClass* UnrealClass = (UClass*)ScriptType->GetUserData();
```

[13] 当前 fork 已经有 typed user-data API，可在插件内直接利用，不需要改引擎：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_typeinfo.h
// 位置: 137-139
// 说明: typed slot 能力已经在 fork 内存在，当前 raw plainUserData 只是插件层还没用起来
// ============================================================================
void *SetUserData(void *data, asPWORD type);
void *GetUserData() const { return (void*)plainUserData; }
void *GetUserData(asPWORD type) const;
```

**差距描述**

- `没有实现`：当前没有 interface type unregister / tombstone / revision registry；删除或 replace 后没有一个地方声明“这个 script type 已失效”。
- `实现方式不同`：当前把 live `UClass*` 挂在 raw `plainUserData` 上，多个消费者分别直接读取；这不是注册表，而是隐式共享缓存。
- `实现质量差异`：一旦 stale pointer 出现，污染会同时扩散到 property 构造、type lookup 和 debug database，而不是只影响单个工具面。

**参考方案**

- **UnLua** 的 reflected type 注册表用 `TWeakObjectPtr<UStruct>` 持有真实类型，并在 invalid/删除时显式 `Unregister()`：
  - `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/ClassDesc.h:84-91`
  - `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.h:37-58`
  - `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp:265-322`
- **sluaunreal** 的 override/object/class registry 也统一使用 `TWeakObjectPtr<UObject/UClass/UFunction>` 作为 map key，而不是裸指针 user data：
  - `Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaOverrider.h:23-46, 101-130`

[14] UnLua 的借鉴点是“弱引用 + 显式注销”，不是把 live class 藏进匿名 user data：

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/ClassDesc.h
// 位置: 84-91
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ClassRegistry.cpp
// 位置: 265-322
// 说明: 参考实现把 reflected type 放进可注销注册表，并用弱引用承载生命周期
// ============================================================================
void Load();
void UnLoad();

private:
	UStruct* RawStructPtr;
	TWeakObjectPtr<UStruct> Struct;

void FClassRegistry::Unregister(const UStruct* Class)
{
	const auto Desc = Find(Class);
	if (!Desc)
		return;
	Classes.Remove(Class);
	Desc->UnLoad();
	Unregister(Desc, true); // ★ 明确从注册表与 Lua registry 清掉
}
```

[15] sluaunreal 的对象/类/函数 registry 也统一走 `TWeakObjectPtr`：

```cpp
// ============================================================================
// 文件: Reference/sluaunreal/Plugins/slua_unreal/Source/slua_unreal/Public/LuaOverrider.h
// 位置: 23-46, 101-130
// 说明: 参考实现把 live UObject / UClass / UFunction 生命周期收敛到弱引用 map
// ============================================================================
typedef TMap<TWeakObjectPtr<UObject>, FObjectTable, FDefaultSetAllocator, TWeakObjectPtrMapKeyFuncs<TWeakObjectPtr<UObject>, FObjectTable>> ObjectTableMap;
typedef TMap<TWeakObjectPtr<UClass>, NativeMap, FDefaultSetAllocator, TWeakObjectPtrMapKeyFuncs<TWeakObjectPtr<UClass>, NativeMap>> ClassNativeMap;
...
static TMap<UClass*, TArray<TWeakObjectPtr<UFunction>>> classAddedFuncs;
static TMap<UClass*, TArray<TWeakObjectPtr<UFunction>>> classHookedFuncs;
static TMap<TWeakObjectPtr<UClass>, UClass::ClassConstructorType> classConstructors;
```

**吸收建议**

- 不改引擎，直接在插件里引入 `ResolveLiveUClassFromScriptType(asITypeInfo*)` + typed slot registry：
  - slot-A：stable registry handle / revision
  - slot-B：interface dispatch signature 或其他附加元数据
  - 停止让 property/type/debug 三个消费者自己读 raw `GetUserData()`
- `CleanupRemovedClass()` 至少补三件事：
  - 若 `ClassDesc->ScriptType` 仍有效，先写 tombstone 或 `SetUserData(nullptr, slot)`
  - 清掉 `ClassDesc->ScriptType` 的 live 映射
  - 对 remove/recreate 同名 interface 增加 revision 比对，避免旧 handle 继续接管新类
- `Bind_BlueprintType.cpp`、`AngelscriptType.cpp`、`AngelscriptDebugServer.cpp` 应统一改用同一 resolver helper。这样 `P10 UInterface` 后续补 `FInterfaceProperty` / `TScriptInterface<>` 时不会再把 raw pointer owner 扩散到更多地方。

**优先级**

- `P0`
- 理由：它直接影响 `UInterface` 主线的 reload correctness，而且当前修复完全可以在插件层完成，不需要碰引擎；应放在继续扩大 interface API surface 之前。

### 值得吸收的设计模式（增量）

- `Metadata must survive phase changes`：`UFUNCTION` metadata 不能只在 runtime helper 或单个测试里存在，必须穿过 UHT、cache、artifact、IDE 四个阶段，否则 gap closure 会对着错误 symbol 做验收。
- `Execution semantics are part of API shape`：`BlueprintCosmetic`、`BlueprintAuthorityOnly` 这类约束不是可有可无的注释，而是脚本可调用面的组成部分。先有统一字段，再谈 enforcement。
- `Weak registry beats raw user-data cache`：只要一个 live pointer 被多个消费者复用，它就不应该挂在匿名 `plainUserData` 上；应该升级成可注销、可分 slot、可加 revision 的注册表。

### 改进路线建议（增量）

1. `P0`：先做 interface type lifetime 收口。给 `asITypeInfo` 引入 plugin-local typed slot registry，并在 `CleanupRemovedClass()` 上补 tombstone / unregister / revision；同时把 property/type/debug 三个消费者切到统一 resolver。
2. `P1`：把 `ScriptMethod` / `ScriptMethodSelfReturn` 提升为 generated artifact 的正式字段，修正 `AS_FunctionTable_*`、bind-db、IDE 报表的 symbol identity，避免 `Bind API GAP` 继续对 raw `K2_` / `Conv_` 名称闭环。
3. `P2`：统一执行语义字段，把 `BlueprintCosmetic`、`BlueprintAuthorityOnly`、`UnsafeDuringActorConstruction` 收进同一个 `InvocationConstraint` 模型；先让 artifact 可见，再逐步接入 runtime guard 与 manual bind。

---

## 深化分析 (2026-04-09 00:22:30)

### 本轮新增关键发现

- `UInterface` 当前仍停在 `K2ZeroOffset` 的 class-flag 语义：`ImplementsInterface()` 与 `opCast` 能工作，但 runtime 内没有显式 `FScriptInterface` / `GetInterfaceAddress()` bridge owner，这与 `P10 UInterface` 要补的 value/parameter/container 语义不是同一层能力。
- 类型语法打印仍由 runtime type adapter 持有：`EAngelscriptDeclarationMode` 同时负责 surface syntax、compiler hint、getter/setter、event helper 与 tooltip，意味着 `TScriptInterface<>` 一旦进入主线，会继续放大 mode 与隐藏 token 特判。
- 多个 gameplay binder 仍直接解 `asCObjectType::templateBaseType/templateSubTypes/plainUserData`。这不是“容器暂未支持 interface”这么简单，而是高层 API 已经依赖 VM 内部布局；未来 `TArray<TScriptInterface<...>>` 会把修补点扩散到 bind 层。

### 差距矩阵（增量）

| 维度 | 新增焦点 | 差距等级 | 新增判断 | 优先级 |
| --- | --- | --- | --- | --- |
| D2 反射绑定机制 | interface instance bridge | 能力缺失 | 当前只有 `UClass::Interfaces` + `ImplementsInterface()`，没有显式 object/address 双槽 bridge | `P0` |
| D6 代码生成与 IDE 支持 | type syntax owner | 实现差异 | 当前可产出声明，但 syntax 与 runtime bridge 强耦合，future interface wrapper 会继续放大特判 | `P1` |
| D1 插件架构与模块划分 | container query boundary | 实现差异 | TypeSystem 边界已泄漏到 gameplay binder，缺统一 container query service | `P2` |

### [D2] 反射绑定机制：`UInterface` 仍缺显式 instance bridge，`ImplementsInterface()` 只能证明类关系

**当前状态**

```
[Current] Interface Runtime Flow
├─ script interface -> UClass::Interfaces              // 接口关系写进 UE 类型图
├─ opCast / ImplementsInterface -> ImplementsInterface // 只做类级判定
└─ missing owner -> FScriptInterface / interface ptr   // 没有 object + address 双槽 bridge

[Reference] Interface Value Flow
├─ FInterfaceProperty -> FScriptInterface              // 先把值容器显式建模
├─ SetObject(Object)                                   // object 槽
└─ SetInterface(GetInterfaceAddress(InterfaceClass))   // interface-address 槽
```

[1] 当前插件把 interface 实现语义硬编码成 Blueprint/K2 风格的 `PointerOffset=0`：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 位置: 3359-3364, 5135-5139
// 说明: interface 关系写入的是 UE 类图约定，不是显式 interface value bridge
// ============================================================================
// UInterface classes: set CLASS_Interface and configure as Blueprint/Script interface
if (ClassDesc->bIsInterface)
{
	NewClass->ClassFlags |= CLASS_Interface | CLASS_Abstract;
	// ★ 当前选择是不设 CLASS_Native，直接复用 Blueprint/Script interface 语义
	// Do NOT set CLASS_Native — this makes GetInterfaceAddress() return this (PointerOffset=0)
}

FImplementedInterface ImplementedInterface;
ImplementedInterface.Class = InterfaceClass;
ImplementedInterface.PointerOffset = 0;
ImplementedInterface.bImplementedByK2 = true; // ★ interface instance 语义被硬编码在 K2ZeroOffset 上
NewClass->Interfaces.Add(ImplementedInterface);
```

[2] runtime cast 也只问 `ImplementsInterface()`，没有任何 `FScriptInterface` / `GetInterfaceAddress()` 桥：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 112-127
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp
// 位置: 185-214
// 说明: interface cast 现在是 class-flag 级判断，返回的仍是 UObject*，不是 interface value
// ============================================================================
bool FAngelscriptEngine::CanCastScriptObjectToUnrealInterface(asITypeInfo* RuntimeType, asITypeInfo* TargetType, void* ObjectPtr)
{
	UClass* TargetClass = reinterpret_cast<UClass*>(TargetType->GetUserData());
	if (TargetClass == nullptr || !TargetClass->HasAnyClassFlags(CLASS_Interface))
	{
		return false;
	}

	UObject* Object = reinterpret_cast<UObject*>(ObjectPtr);
	UClass* ObjectClass = Object != nullptr ? Object->GetClass() : nullptr;
	const bool bImplementsInterface = ObjectClass != nullptr && ObjectClass->ImplementsInterface(TargetClass);
	// ★ 这里只有“类是否实现接口”的布尔判断，没有 interface address/value bridge
}

else if (bImplementsInterface)
{
	*(UObject**)OutAddress = Object; // ★ cast 成功时仍直接回写 UObject*
}
```

补充证据：当前 `Plugins/Angelscript/Source/AngelscriptRuntime/` 内搜索 `FScriptInterface`、`TScriptInterface`、`FInterfaceProperty` 均无实现命中，说明 instance/value bridge 还没有进入 runtime 主链。

**差距描述**

- `没有实现`：没有显式 `FScriptInterface` / `GetInterfaceAddress()` bridge，也没有统一 owner 去描述 interface value 的 `Object` 槽与 `InterfaceAddress` 槽。
- `实现方式不同`：当前把 interface 支持建模成 `UClass::Interfaces + PointerOffset=0 + bImplementedByK2=true`，本质是“类实现关系”方案，不是“interface 值桥接”方案。
- `实现质量差异`：`opCast` 和 `ImplementsInterface()` 看起来能工作，会掩盖 value/parameter/container 语义仍未入链的事实；这会把 `P10 UInterface` 的验收口径错误地收缩成“能 cast 就算支持”。

**参考方案**

- **UnLua**：`FInterfacePropertyDesc` 直接把 `FInterfaceProperty` 读写成 `FScriptInterface`，并显式回填 interface address。
  - `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:549-559`
- **puerts**：`FInterfacePropertyTranslator` 同样直接维护 `FScriptInterface` 的 object/address 双槽，wrapper 继续交给 object mapper。
  - `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:608-629`
- **UnrealCSharp**：`FInterfacePropertyDescriptor::Set()` 与 `FTypeBridge::GetClass(const FInterfaceProperty*)` 说明 interface 既有 instance-side value owner，也有 type-side generic owner。
  - `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/ObjectProperty/FInterfacePropertyDescriptor.cpp:29-45`
  - `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:415-430`

[3] 参考实现都先把 interface 值显式建模成 `FScriptInterface`：

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp
// 位置: 549-559
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp
// 位置: 608-629
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/ObjectProperty/FInterfacePropertyDescriptor.cpp
// 位置: 38-44
// 说明: object 槽与 interface-address 槽都由 property/value bridge 显式维护
// ============================================================================
const FScriptInterface &Interface = InterfaceProperty->GetPropertyValue(ValuePtr);
UnLua::PushUObject(L, Interface.GetObject());

FScriptInterface* Interface = reinterpret_cast<FScriptInterface*>(ValuePtr);
Interface->SetObject(Object);
Interface->SetInterface(Object ? Object->GetInterfaceAddress(InterfaceProperty->InterfaceClass) : nullptr);

const auto InterfaceValue = static_cast<FScriptInterface*>(Dest);
InterfaceValue->SetObject(Object);
InterfaceValue->SetInterface(Object ? Object->GetInterfaceAddress(Property->InterfaceClass) : nullptr);
```

**吸收建议**

- 在插件内新增 `IAngelscriptInterfaceBridge` 或 `FAngelscriptImplementedInterfacePlan`，第一阶段只把当前 `PointerOffset=0 / bImplementedByK2=true` 封装成一个显式 strategy，不改行为。
- 让 `CanCastScriptObjectToUnrealInterface()`、`Bind_UObject.cpp` 的 `opCast`、未来的 `FInterfaceProperty` / `TScriptInterface<>` adapter 统一查询这条 bridge，而不是各自重写 `ImplementsInterface()` 逻辑。
- 第二阶段再把 `GetInterfaceAddress()` / `FScriptInterface` 写回接到同一 bridge；这样 value、parameter、callable 和 container family 才能共享一个 authority，而不是继续在 `Bind_UObject`、`ClassGenerator`、`UHT` 三边各补一套 interface 特判。

**优先级**

- `P0`
- 理由：这条缺口直接压在 `P10 UInterface` 主线上，且不要求修改引擎；先把 instance bridge owner 显式化，后续 `Bind API GAP` 才不会继续把 interface 支持误判成“cast 已可用”。

### [D6] 代码生成与 IDE 支持：type syntax 仍由 runtime type adapter 持有，`TScriptInterface<>` 会继续放大 mode 特判

**当前状态**

```
[Current] Syntax Ownership
├─ FAngelscriptType::GetAngelscriptDeclaration(mode)   // 语法打印入口
├─ BuildFunctionDeclaration()                          // 函数签名复用 mode
├─ getter/setter generation                            // 访问器类型复用 mode
├─ Blueprint event helper                              // event helper 名称与签名复用 mode
└─ hidden token path                                   // unresolved_object 这类编译 hint 也混在这里

[Risk for P10]
├─ add TScriptInterface<> family
├─ add member / arg / return / event forms
└─ mode enum + hidden token keep growing
```

[4] 当前 `EAngelscriptDeclarationMode` 同时承担 surface syntax 与编译期 hint：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h
// 位置: 108-120
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp
// 位置: 174-190, 570-619
// 说明: type syntax 的 owner 仍是 runtime type adapter，本身不区分“显示名”与“桥接 hint”
// ============================================================================
enum EAngelscriptDeclarationMode
{
	Generic,
	MemberVariable,
	PreResolvedObject,
	FunctionReturnValue,
	FunctionArgument,
	MemberVariable_InContainer,
};

FString FAngelscriptType::GetAngelscriptDeclaration(const FAngelscriptTypeUsage& Usage, EAngelscriptDeclarationMode Mode) const
{
	EAngelscriptDeclarationMode InnerMode = Mode;
	if (Mode == EAngelscriptDeclarationMode::MemberVariable)
		InnerMode = EAngelscriptDeclarationMode::MemberVariable_InContainer;
	// ★ 内层 subtype 的显示方式直接跟着外层 mode 改写
	Decl += Usage.SubTypes[i].GetAngelscriptDeclaration(InnerMode);
}

Declaration = ReturnType.GetAngelscriptDeclaration(FAngelscriptType::EAngelscriptDeclarationMode::FunctionReturnValue);
Declaration += ArgumentTypes[i].GetAngelscriptDeclaration(FAngelscriptType::EAngelscriptDeclarationMode::FunctionArgument);
// ★ 函数签名、默认值转换、tooltip 都共享这套 mode 驱动的 syntax 生成
```

[5] `TObjectPtr` 这类 wrapper 已经在 declaration printer 里夹带隐藏 token，getter/setter 与 event helper 也跟着绑定这条路径：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp
// 位置: 1135-1150, 1799-1812
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp
// 位置: 358-380
// 说明: 同一条 GetAngelscriptDeclaration() 既决定用户可见写法，也决定特殊 compiler hint
// ============================================================================
if (Usage.IsUnresolvedObjectPointer())
{
	AccessorType = Usage.GetAngelscriptDeclaration(FAngelscriptType::EAngelscriptDeclarationMode::PreResolvedObject);
	DBProp.bGeneratedUnresolvedObject = true;
}

if (Mode == EAngelscriptDeclarationMode::MemberVariable)
{
	// ★ `unresolved_object` 不是 surface syntax，而是编译 hint，却被塞进 declaration 字符串
	return Usage.SubTypes[0].GetAngelscriptDeclaration(Mode) + TEXT(" unresolved_object");
}

FString Decl = FString::Printf(TEXT("void __Evt_PushArgument__%s(const %s& Value)"),
	*PushTypeName,
	*Type->GetAngelscriptDeclaration(FAngelscriptTypeUsage::DefaultUsage, FAngelscriptType::FunctionArgument));
// ★ event helper 也只能复用同一条 declaration printer
```

**差距描述**

- `没有实现`：当前没有独立的 `TypeSyntaxDesc` / `SyntaxEmitter` / `CompilerTags` 合同，无法把“用户看到的类型名”和“编译器/生成器需要的附加 hint”分开表达。
- `实现方式不同`：现在由 runtime `FAngelscriptType` virtual interface 直接生成所有语法形态；reference 方案则把 generic 名、surface name、runtime property bridge 明确拆层。
- `实现质量差异`：只要新增 `TScriptInterface<>`、`TOptional<TScriptInterface<...>>` 或其它 wrapper family，就必须同步修改 `BuildFunctionDeclaration`、getter/setter、event helper、tooltip 等多个 surface；扩展成本已经高于真正的 bridge 实现本身。

**参考方案**

- **UnrealCSharp**：`TName<T>` 负责命名，`TGeneric<T>` 负责 generic template 名，`FGeneratorCore::GetPropertyType()` 只消费结构化信息输出 `TScriptInterface<...>`。
  - `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Binding/TypeInfo/TName.inl:155-166`
  - `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Binding/TypeInfo/TGeneric.inl:28-35`
  - `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:144-149`

[6] 参考方案把“接口类型怎么写”独立成 naming/generic contract，而不是塞进 runtime adapter mode：

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Binding/TypeInfo/TName.inl
// 位置: 155-166
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Binding/TypeInfo/TGeneric.inl
// 位置: 28-35
// 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp
// 位置: 144-149
// 说明: generic 名、接口名、代码生成输出各自有 owner，runtime bridge 不需要再参与拼字符串
// ============================================================================
template <typename T>
struct TName<T, std::enable_if_t<TIsIInterface<std::decay_t<T>>::Value, T>>
{
	static auto Get()
	{
		return FUnrealCSharpFunctionLibrary::GetFullInterface(std::decay_t<T>::UClassType::StaticClass());
	}
};

template <typename T>
struct TGeneric<T, std::enable_if_t<TIsTScriptInterface<std::decay_t<T>>::Value, T>> : FGenericNameSpace
{
	static auto GetTemplateName() { return TEMPLATE_T_SCRIPT_INTERFACE; }
};

if (const auto InterfaceProperty = CastField<FInterfaceProperty>(Property))
{
	return FString::Printf(TEXT("TScriptInterface<%s>"), *FUnrealCSharpFunctionLibrary::GetFullInterface(InterfaceProperty->InterfaceClass));
}
```

**吸收建议**

- 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/` 新增 `FAngelscriptTypeSyntaxDesc` 或 `IAngelscriptTypeSyntaxEmitter`，至少显式区分 `SurfaceName`、`TemplateArguments`、`QualifierStyle`、`CompilerTags`、`AccessorForm`、`EventArgumentForm`。
- 第一阶段只迁移现有 `TObjectPtr` 路径：把 `unresolved_object` 从 declaration 字符串里拆到结构化 `CompilerTags`，验证 getter/setter、event helper、tooltip 输出保持不变。
- 第二阶段再把 `TScriptInterface<>` 接到同一 syntax owner 上，避免 `P10` 期间继续扩张 `EAngelscriptDeclarationMode` 或发明新的隐藏 token。

**优先级**

- `P1`
- 理由：它不是 `P10` 的第一阻塞点，但会决定 `Bind API GAP` 后续是“补一个 family 改五处”还是“补一个 family 改一处”。结合 `Documents/Plans/Plan_StatusPriorityRoadmap.md:203-205` 对 interface binding 的 sibling-plan 节奏，这个 owner 拆分应在 `P0` bridge 稳住后尽快做成窄改动，而不是拖到更多 wrapper family 落地后再返工。

### [D1] 插件架构与模块划分：TypeSystem 边界仍在泄漏，container query 还没成为 Core 服务

**当前状态**

```
[Current] Container Query Flow
├─ Bind_AActor / Bind_USceneComponent / Bind_UDataTable
│  ├─ GetTypeInfoById(TypeId)
│  ├─ templateBaseType == ArrayTemplateTypeInfo
│  └─ templateSubTypes[0].plainUserData -> UClass / UStruct
└─ gameplay binder 直接依赖 VM 模板内部布局

[Reference] Container Ownership
├─ PropertyTranslator::Create(FProperty) -> family factory
├─ Array translator -> FindOrAddContainer(InnerProperty, ScriptArray)
└─ Container wrapper / registry owns element identity
```

[7] 当前高层 binder 直接解 AngelScript 模板内部结构，不经过统一 query service：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp
// 位置: 39-68
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_USceneComponent.cpp
// 位置: 48-77
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UDataTable.cpp
// 位置: 42-68
// 说明: gameplay binder 自己检查 templateBaseType / templateSubTypes / plainUserData
// ============================================================================
asCObjectType* ObjectType = (asCObjectType*)(ScriptType);
if (ObjectType->templateBaseType != FAngelscriptType::GetArrayTemplateTypeInfo())
{
	FAngelscriptEngine::Throw("GetComponentsByClass must take a TArray of components as its out argument.");
	return;
}

auto* SubTypeInfo = ObjectType->templateSubTypes[0].GetTypeInfo();
if (SubTypeInfo == nullptr
	|| (SubTypeInfo->GetFlags() & asOBJ_REF) == 0
	|| (SubTypeInfo->plainUserData == 0))
{
	FAngelscriptEngine::Throw("GetChildrenComponentsByClass must take a TArray of scene components as its out argument.");
	return;
}

auto SubClass = reinterpret_cast<const UStruct*>(SubTypeInfo->plainUserData);
// ★ 高层 API 直接依赖 VM 内部 subtype 布局，没有统一 container descriptor/query
```

[8] Core 目前暴露的仍是低层模板 typeinfo 槽，不是“数组元素是什么”的正式查询服务：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp
// 位置: 560-567
// 说明: Core 只有 ArrayTemplateTypeInfo 这类低层入口，没有高层 container query API
// ============================================================================
asITypeInfo* FAngelscriptType::GetArrayTemplateTypeInfo()
{
	return GetTypeDatabase().ArrayTemplateTypeInfo;
}

void FAngelscriptType::SetArrayTemplateTypeInfo(asITypeInfo* TypeInfo)
{
	GetTypeDatabase().ArrayTemplateTypeInfo = TypeInfo;
}
```

**差距描述**

- `没有实现`：当前没有统一的 `ContainerQuery` / `ContainerDescriptor` / `TryResolveArrayElement()` 之类服务，无法让高层 binder 只问“元素语义是什么”。
- `实现方式不同`：现在由 gameplay binder 自己解 `templateBaseType/templateSubTypes/plainUserData`；reference 方案则把 family 识别和 element identity 收敛到 translator/registry/wrapper。
- `实现质量差异`：一旦未来补 `TArray<TScriptInterface<...>>`、`TOptional<TScriptInterface<...>>` 或别的 wrapper 组合，修补点不只在 TypeSystem，还会扩散到 `Bind_AActor`、`Bind_USceneComponent`、`Bind_UDataTable` 等高层 API。

**参考方案**

- **puerts**：`FPropertyTranslator::Create()` 统一决定这是 `Interface/Array/Map/Set` 哪个 family，数组 translator 再把 `InnerProperty` 交给 `FindOrAddContainer()`；上层不直接解 VM 模板对象。
  - `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:862-889`
  - `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1308-1322`
  - `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/ContainerWrapper.cpp:221-229`
- **UnLua**：`ContainerRegistry` 以 `ITypeInterface` 作为 element/key/value identity，容器 cache 只认 descriptor，不认脚本 VM 内部布局。
  - `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ContainerRegistry.cpp:34-68`

[9] 参考实现把容器语义收敛到 translator/registry，而不是让高层业务 API 直接看模板内部字段：

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp
// 位置: 862-889, 1308-1322
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Registries/ContainerRegistry.cpp
// 位置: 34-68
// 说明: family 识别与元素 identity 都在统一工厂/registry 中完成
// ============================================================================
return FV8Utils::IsolateData<IObjectMapper>(Isolate)->FindOrAddContainer(
	Isolate, Context, ArrayProperty->Inner, ScriptArray, ByPointer);

else if (InProperty->IsA<ArrayPropertyMacro>())
{
	return Creator<FScriptArrayPropertyTranslator>::Do(InProperty, IgnoreOut, Ptr);
}
else if (InProperty->IsA<InterfacePropertyMacro>())
{
	return Creator<FInterfacePropertyTranslator>::Do(InProperty, IgnoreOut, Ptr);
}

FLuaArray* FContainerRegistry::NewArray(lua_State* L, TSharedPtr<ITypeInterface> ElementType, FLuaArray::EScriptArrayFlag Flag)
{
	const auto Ret = new(Userdata) FLuaArray(ScriptArray, ElementType, Flag);
}
// ★ 容器 cache 依赖的是 ElementType descriptor，不是 VM 模板内部字段
```

**吸收建议**

- 在 `Core/` 新增 `FAngelscriptContainerQuery` 或等价 descriptor 服务，第一批至少支持：
  - `TryResolveArrayElement(TypeId/Usage, OutElementDesc)`
  - `TryResolveNominalClass(TypeDesc, ExpectedBaseClass)`
  - `TryResolveStruct(TypeDesc)`
- 先迁移 `Bind_AActor.cpp`、`Bind_USceneComponent.cpp`、`Bind_UDataTable.cpp` 三个高频示例，把当前的 `templateBaseType/plainUserData` 路径保留一轮作为 fallback，并在命中 fallback 时打 trace。
- 等 `P0` interface bridge 落地后，再把 `TArray<TScriptInterface<...>>`、`TOptional<TScriptInterface<...>>` 挂到同一 query service；不要让 interface-container 组合先把高层 binder 扩散一遍。

**优先级**

- `P2`
- 理由：它不阻塞第一批 `UInterface` value/callable 收口，但会决定第二波 `container + interface` 组合是局部扩展还是全仓打补丁；应在 `P0/P1` 完成后尽快推进，避免高层 binder 继续固化 VM 内部耦合。

### 值得吸收的设计模式（增量）

- `Bridge owner before capability expansion`：先明确 interface value 的 owner，再扩大 callable/property/container 覆盖率；否则每补一层都会重复实现“对象槽 + address 槽”。
- `Syntax is data, not adapter side effect`：类型显示名、generic 名、compiler hint 不应继续塞进 `GetAngelscriptDeclaration(mode)`；它们需要可复用的结构化 contract。
- `High-level binders query descriptors, never decode VM layout`：一旦高层 API 自己看 `templateBaseType` / `plainUserData`，新增 family 的成本就不再受控。

### 改进路线建议（增量）

1. `P0`：先做 `IAngelscriptInterfaceBridge` / `ImplementedInterfacePlan`，把当前 `K2ZeroOffset` 语义收进显式 owner，并让 `opCast` / `CanCastScriptObjectToUnrealInterface()` / future `FInterfaceProperty` 共享同一 bridge。
2. `P1`：引入 `FAngelscriptTypeSyntaxDesc` 或 `SyntaxEmitter`，先把 `TObjectPtr` 的 `unresolved_object` 从 declaration 字符串里拆出来，再为 `TScriptInterface<>` 预留稳定 syntax owner，避免 `Bind API GAP` 继续堆 mode 特判。
3. `P2`：补 `FAngelscriptContainerQuery`，先迁移 `Bind_AActor`、`Bind_USceneComponent`、`Bind_UDataTable` 三个高频入口，再让 `TArray<TScriptInterface<...>>` / `TOptional<TScriptInterface<...>>` 挂到同一 descriptor service。

---

## 深化分析 (2026-04-09 00:37:07)

### 本轮新增关键发现

- `D2` 的真实阻塞点比“`FInterfaceProperty` 还没支持”更窄也更硬：它当前根本没有进入 `FAngelscriptType::GetByProperty()` / `RegisterTypeFinder()` / `CreateProperty()` 这条 property family dispatch，所以不仅单个 interface 属性不可用，`TArray<TScriptInterface<...>>` 这类 container 组合也会被同一缺口一并挡住。
- `D3` 当前的 interface callable 不是一条主链，而是三条互不对账的 lane：脚本 `UINTERFACE` 走 `FName` 级 generic callback，native `UInterface` 测试靠手工 `ReferenceClass + GenericMethod` 旁路，engine interface 又被 reflective fallback 明确拒绝；这说明 callable owner 还没有统一，而不是“已经有能力，只差扩面”。
- `D6` 对 interface symbol 仍没有跨阶段一致状态词汇。以 `UGameplayTagAssetInterface::HasMatchingGameplayTag` 为例：runtime 侧结论是 `RejectedInterfaceClass`，UHT 产物里是 `Stub`，`SkippedEntries.csv` 里又完全没有对应 reason 行，直接把 `Bind API GAP` 的量化口径扭曲成了阶段相关的表象。

### 差距矩阵（本轮增量修正）

| 维度 | 子主题 | 差距等级 | 本轮新增结论 | 优先级 |
| --- | --- | --- | --- | --- |
| D2 反射绑定机制 | `FInterfaceProperty` / `TScriptInterface<>` property family dispatch | 能力缺失 | interface family 还没进入 `GetByProperty + CreateProperty + MatchesProperty` 主链，container 组合会被同一缺口放大 | P0 |
| D3 Blueprint 交互 | interface callable owner 统一性 | 能力缺失 | script/native/engine 三类 interface callable 各走不同 lane，没有统一 descriptor、统一 eligibility 和统一 registration | P0 |
| D6 代码生成与 IDE 支持 | interface symbol 的跨阶段状态词汇 | 实现质量差异 | runtime `RejectedInterfaceClass`、artifact `Stub`、skip ledger 缺席，导致报表和 closeout 指标失真 | P1 |

### [D2] 反射绑定机制：`FInterfaceProperty` 还没成为 property family

```
[Current AS Property Family Dispatch]
FProperty
├─ TypeFinders
│  ├─ FObjectProperty / FWeakObjectProperty        // UObject / TObjectPtr / TWeakObjectPtr
│  └─ FClassProperty                               // TSubclassOf / object-wrapper class
├─ TypesImplementingProperties
│  ├─ Primitive / Struct / Delegate
│  ├─ Array / Map / Set / Optional
│  └─ ...
└─ FInterfaceProperty                              // 当前没有 resolver / descriptor / property builder

[Container Consequence]
TArray<T> / TSet<T> / TOptional<T>
└─ delegate to SubType.CreateProperty()/MatchesProperty()
   └─ T == interface family -> 同样失效
```

**当前状态**

- `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:147-171` 的中心分发器只做两件事：先跑 `TypeFinders`，再扫 `TypesImplementingProperties`。它本身没有任何 `FInterfaceProperty` 专门分支。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:122-139,149-170` 的基础 object family 只会生成 / 匹配 `FObjectProperty` 与 `FClassProperty`。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:2438-2500` 注册的 type finder 也只覆盖 `FObjectProperty`、`FWeakObjectProperty` 与 `FClassProperty`。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp:88-116` 明确把容器 property 创建与匹配委托给 subtype；因此只要 subtype 没有 interface family，`TArray<TScriptInterface<...>>` 这类组合就不会“自动变好”。

[1] 当前中心 dispatch 与 object/class family 仍没有 `FInterfaceProperty` 落点：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp
// 位置: 147-171
// 说明: 中心 property dispatch 只认 TypeFinder 与 TypesImplementingProperties
// ============================================================================
TSharedPtr<FAngelscriptType> FAngelscriptType::GetByProperty(FProperty* Property, bool bQueryTypeFinders)
{
	// ★ 第一层：问已注册的 TypeFinder
	if (bQueryTypeFinders)
	{
		FAngelscriptTypeUsage Usage;
		for (auto& Finder : Database.TypeFinders)
		{
			if (Finder(Property, Usage))
				return Usage.Type;
		}
	}

	// ★ 第二层：扫“会处理 property 的 type”
	for (auto& CheckType : Database.TypesImplementingProperties)
	{
		if (CheckType->MatchesProperty(FAngelscriptTypeUsage::DefaultUsage, Property, FAngelscriptType::EPropertyMatchType::TypeFinder))
			return CheckType;
	}

	// 这里没有任何 FInterfaceProperty 的专门兜底
	return nullptr;
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp
// 位置: 122-139, 149-170, 2438-2500
// 说明: 当前 object family 只生成 / 匹配 FObjectProperty 与 FClassProperty
// ============================================================================
FProperty* CreateProperty(const FAngelscriptTypeUsage& Usage, const FPropertyParams& Params) const override
{
	if (Class == UClass::StaticClass())
	{
		auto* Property = new FClassProperty(Params.Outer, Params.PropertyName, RF_Public);
		Property->PropertyClass = Class;
		Property->MetaClass = UObject::StaticClass();
		return Property;
	}

	auto* Property = new FObjectProperty(Params.Outer, Params.PropertyName, RF_Public);
	Property->PropertyClass = Class != nullptr ? Class : (UClass*)Usage.ScriptClass->GetUserData();
	return Property;
}

bool MatchesProperty(const FAngelscriptTypeUsage& Usage, const FProperty* Property, EPropertyMatchType MatchType) const override
{
	const FObjectProperty* ObjectProp = CastField<FObjectProperty>(Property);
	if (ObjectProp == nullptr)
		return false;
	// ★ 这里匹配的是 object family，不是 interface family
	return ObjectProp->PropertyClass == Class;
}

FAngelscriptType::RegisterTypeFinder([=](FProperty* Property, FAngelscriptTypeUsage& Usage) -> bool
{
	const FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property);
	if (ObjectProperty == nullptr)
	{
		const FWeakObjectProperty* WeakObjectProperty = CastField<FWeakObjectProperty>(Property);
		// ★ 没有 FInterfaceProperty 分支
		...
	}

	if ((ObjectProperty->PropertyFlags) != 0)
	{
		Usage.Type = ObjectPtrType;
		...
		return true;
	}

	const FClassProperty* ClassProperty = CastField<FClassProperty>(Property);
	if (ClassProperty != nullptr && (ClassProperty->PropertyFlags) != 0)
	{
		Usage.Type = ObjectPtrType;
		...
		return true;
	}

	return false;
});
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp
// 位置: 88-116
// 说明: container family 直接依赖 subtype 的 property 能力
// ============================================================================
bool FAngelscriptArrayType::CanCreateProperty(const FAngelscriptTypeUsage& Usage) const
{
	if (Usage.SubTypes.Num() != 1)
		return false;
	return Usage.SubTypes[0].CanCreateProperty();
}

FProperty* FAngelscriptArrayType::CreateProperty(const FAngelscriptTypeUsage& Usage, const FPropertyParams& Params) const
{
	auto* ArrayProp = new FArrayProperty(Params.Outer, Params.PropertyName, RF_Public);
	ArrayProp->Inner = Usage.SubTypes[0].CreateProperty(InnerParams);
	return ArrayProp;
}

bool FAngelscriptArrayType::MatchesProperty(const FAngelscriptTypeUsage& Usage, const FProperty* Property, EPropertyMatchType MatchType) const
{
	const FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property);
	return Usage.SubTypes[0].MatchesProperty(ArrayProp->Inner, FAngelscriptType::EPropertyMatchType::InContainer);
}
```

**差距描述**

- `能力缺失`：当前运行时没有任何 `FInterfaceProperty` 的 resolver / descriptor / `CreateProperty()` / `MatchesProperty()` / `FScriptInterface` 写回路径。
- `实现方式不同`：参考插件普遍把 interface 当成显式 property family；当前实现则把它留在 object/class family 之外，导致 interface 只能靠 `ImplementsInterface()`、`Cast<>` 等 class-flag 语义“擦边”出现。
- `实现质量差异`：因为 container family 递归依赖 subtype，interface family 缺失会直接放大成 `TArray<TScriptInterface<...>>`、`TSet<TScriptInterface<...>>`、`TOptional<TScriptInterface<...>>` 全线不可建模，而不是单点缺口。

**参考方案**

- **UnrealCSharp**：在 `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/FPropertyDescriptor.cpp:47-87` 直接把 `FInterfaceProperty` 纳入 descriptor factory；在 `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/ObjectProperty/FInterfacePropertyDescriptor.cpp:29-45` 用同一 descriptor 负责 `FScriptInterface` 的 `SetObject()` + `SetInterface()` 双槽写回；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:266-269,415-430` 再把它映射回统一的 `TScriptInterface<>` generic reflection class。
- **UnLua / puerts** 也都维持相同原则：`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:533-560,1592-1595` 与 `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:598-630,1308-1310` 都把 `FInterfaceProperty` 做成一等 property translator，而不是 object 特判。

[2] 参考实现先承认 interface family，再写双槽值桥：

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/FPropertyDescriptor.cpp
// 位置: 47-87
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/ObjectProperty/FInterfacePropertyDescriptor.cpp
// 位置: 29-45
// 说明: factory 与 value bridge 都把 FInterfaceProperty 当作正式 family
// ============================================================================
FPropertyDescriptor* FPropertyDescriptor::Factory(FProperty* InProperty)
{
	...
	NEW_PROPERTY_DESCRIPTOR(FDelegateProperty)
	NEW_PROPERTY_DESCRIPTOR(FInterfaceProperty) // ★ factory 直接有 interface 分支
	NEW_PROPERTY_DESCRIPTOR(FStructProperty)
	NEW_PROPERTY_DESCRIPTOR(FArrayProperty)
	...
}

void FInterfacePropertyDescriptor::Set(void* Src, void* Dest) const
{
	Property->InitializeValue(Dest);

	const auto Interface = static_cast<FScriptInterface*>(Dest);
	const auto Object = SrcMulti->GetObject();

	Interface->SetObject(Object);
	Interface->SetInterface(Object ? Object->GetInterfaceAddress(Property->InterfaceClass) : nullptr);
	// ★ object 槽与 interface-address 槽一起写回
}
```

**吸收建议**

- 先在插件层补一个最小 `FInterfaceProperty` family owner，而不是继续扩 test helper。最窄切口是：
  - 在 `Core/AngelscriptType.*` 与 `Bind_BlueprintType.cpp` 引入 `FInterfaceProperty` resolver；
  - 为 `UInterface` / `TScriptInterface<>` 定义可复用的 `CreateProperty()` / `MatchesProperty()` / `GetByProperty()` 结果；
  - 用显式 helper 封装 `FScriptInterface` 的 `SetObject()` + `SetInterface()` 双槽写回。
- 第一轮不要发散到语法糖或引擎改动。只要 `Usage.SubTypes[0].CanCreateProperty()` 能对 interface family 返回真，`TArray/TSet/TOptional` 就能自动吃到第一批收益。
- `TScriptInterface<>` 的 script-facing syntax 可以晚一拍；当前更关键的是先把 property family owner 立住，让 `P10 UInterface` 不再停留在 cast/query 层。

**优先级**

- `P0`
- 理由：`Documents/Plans/Plan_CppInterfaceBinding.md:21-23` 已把“`FInterfaceProperty` 未参与绑定”与“`FInterfaceMethodSignature` 只有 `FName`”并列为核心缺口；而 `Documents/Plans/Plan_StatusPriorityRoadmap.md:203-205` 明确要求 interface binding 的推进服从当前主线闸门。这里是直接解除主线阻塞的最窄实现点，不依赖改引擎，也不要求整体升级 AngelScript。

### [D3] Blueprint 交互：interface callable 仍被拆成三条 lane

```
[Current Interface Callable Lanes]
Script-defined UINTERFACE
├─ Preprocessor extracts MethodName from declaration
├─ RegisterObjectMethod(full declaration)
└─ UserData = { FunctionName } -> CallInterfaceMethod -> FindFunction(name)

Native C++ UInterface (current tests)
├─ EnsureNativeInterfaceFixturesBound()
└─ ReferenceClass + GenericMethod(manual declaration)   // 测试旁路，不是生产自动链

Engine UInterface
└─ EvaluateReflectiveFallbackEligibility()
   └─ RejectedInterfaceClass                           // 显式拒绝
```

**当前状态**

- `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:59-62` 的 `FInterfaceMethodSignature` 只有一个 `FName FunctionName` 字段。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:1106-1154` 为脚本 `UINTERFACE` 注册方法时，会从声明字符串里提取方法名，把 `FName` 填进 user data。
- `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:56-67` 的 `CallInterfaceMethod()` 运行时只做 `FindFunction(Sig->FunctionName)`，没有结构化签名 key。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp:261-270` 对 engine interface `UFunction` 明确返回 `RejectedInterfaceClass`。
- `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptBlueprintCallableReflectiveFallbackTests.cpp:228-250` 还把这条 rejection 锁成了预期行为。
- `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceNativeTests.cpp:41-83` 的 native interface 正向场景仍通过 `ReferenceClass + GenericMethod` 手工补方法声明，不走生产自动绑定链。

[3] 当前 callable owner 仍按来源拆裂：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h
// 位置: 59-62
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp
// 位置: 1106-1154
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 位置: 56-67
// 说明: 脚本 interface 的 callable user data 只有 FunctionName
// ============================================================================
struct FInterfaceMethodSignature
{
	FName FunctionName; // ★ 没有参数、返回值、owner interface、reason 等结构化信息
};

// 预处理阶段从字符串声明中抽方法名
FString MethodName = BeforeParen.Mid(LastSpace + 1).TrimStartAndEnd();
auto* Sig = Engine.RegisterInterfaceMethodSignature(FName(*MethodName));
int32 FuncId = Engine.Engine->RegisterObjectMethod(
	TCHAR_TO_ANSI(*InterfaceName),
	TCHAR_TO_ANSI(*ASDecl),
	asFUNCTION(CallInterfaceMethod),
	asCALL_GENERIC,
	nullptr);
ScriptFunc->SetUserData(Sig, 0);

// 运行时调用再退化成按名查找
void CallInterfaceMethod(asIScriptGeneric* InGeneric)
{
	auto* Sig = (FInterfaceMethodSignature*)Generic->GetFunction()->GetUserData();
	UObject* Object = (UObject*)Generic->GetObject();
	UFunction* RealFunc = Object->FindFunction(Sig->FunctionName);
	InvokeReflectiveUFunctionFromGenericCall(Generic, Object, RealFunc);
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp
// 位置: 261-270
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptBlueprintCallableReflectiveFallbackTests.cpp
// 位置: 242-250
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceNativeTests.cpp
// 位置: 41-45, 68-83
// 说明: engine interface 被显式拒绝，native interface 正向测试仍靠手工 helper
// ============================================================================
if (OwningClass->HasAnyClassFlags(CLASS_Interface))
{
	return EAngelscriptReflectiveFallbackEligibility::RejectedInterfaceClass;
}

TestEqual(
	TEXT("Reflective fallback should reject interface-class functions explicitly"),
	EvaluateReflectiveFallbackEligibility(InterfaceFunction),
	EAngelscriptReflectiveFallbackEligibility::RejectedInterfaceClass);

void BindNativeInterfaceMethod(FAngelscriptBinds& Binds, const TCHAR* Declaration, const TCHAR* FunctionName)
{
	FInterfaceMethodSignature* Signature = FAngelscriptEngine::Get().RegisterInterfaceMethodSignature(FName(FunctionName));
	Binds.GenericMethod(FString(Declaration), TestCallInterfaceMethod, Signature);
}

// ★ native interface fixtures 仍是逐方法手工补 declaration
BindNativeInterfaceMethod(Binds, TEXT("int GetNativeValue() const"), TEXT("GetNativeValue"));
BindNativeInterfaceMethod(Binds, TEXT("void SetNativeMarker(FName Marker)"), TEXT("SetNativeMarker"));
BindNativeInterfaceMethod(Binds, TEXT("void AdjustNativeValue(int Delta, int& Value)"), TEXT("AdjustNativeValue"));
```

**差距描述**

- `能力缺失`：当前没有统一的 interface callable descriptor。script interface、native interface、engine interface 的 eligibility / registration / invoke 逻辑都不共享同一个 owner。
- `实现方式不同`：参考方案更倾向于让 interface callable 继续走“对象值 + 普通 `UFunction` 调用”主线，而不是给 interface 单独开一个 name-only lane，再把 engine interface 整包拒绝。
- `实现质量差异`：当前正向测试覆盖到的多是“侧路能跑通”，还不是“生产自动绑定主链已经统一”。这会把 `P10` closeout 错误收窄成“能 cast、能少数调用就算完成”。

**参考方案**

- **UnLua** 的做法最值得吸收：`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:541-560` 把 interface 值仍压回 `UObject + FScriptInterface` 双槽，`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaCore.cpp:637-642` 读取时直接把 interface 元素推成普通对象，随后 `Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Tests/Issue595Test.cpp:33-47` 证明 `UE.UIssue595Interface.Test(Obj)` 与 `Obj:Test()` 可以走同一条对象调用主链。

[4] 参考实现并不把 interface callable 拆成单独 lane：

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp
// 位置: 541-560
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaCore.cpp
// 位置: 637-642
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Tests/Issue595Test.cpp
// 位置: 33-47
// 说明: interface 值先回到对象主链，再由普通调用路径执行 interface 方法
// ============================================================================
virtual void GetValueInternal(lua_State *L, const void *ValuePtr, bool bCreateCopy) const override
{
	const FScriptInterface &Interface = InterfaceProperty->GetPropertyValue(ValuePtr);
	UnLua::PushUObject(L, Interface.GetObject()); // ★ 读出时直接回到对象主链
}

virtual bool SetValueInternal(lua_State *L, void *ValuePtr, int32 IndexInStack, bool bCopyValue) const override
{
	FScriptInterface *Interface = (FScriptInterface*)ValuePtr;
	UObject *Value = UnLua::GetUObject(L, IndexInStack);
	Interface->SetObject(Value);
	Interface->SetInterface(Value ? Value->GetInterfaceAddress(InterfaceProperty->InterfaceClass) : nullptr);
	return true;
}

const auto Chunk = R"(
	local Obj = NewObject(UE.UIssue595Object)
	UE.UIssue595Interface.Test(Obj)
	return Obj:Test()
)";
// ★ 回归直接验证 interface 静态入口与对象成员入口都可调用
```

**吸收建议**

- 不建议继续扩张 `FInterfaceMethodSignature{FName}`。应把它升级成最小 `FAngelscriptInterfaceCallableDesc`，字段至少包含：
  - `OwnerInterfaceClass`
  - `FunctionName`
  - `NormalizedReturnType`
  - `NormalizedParamTypes`
  - `CallableSourceKind(Script/Native/Engine)`
  - `ReasonCode`
- `Plan_CppInterfaceBinding.md:100-105` 已经要求实现 native interface 自动扫描与注册。这里应直接复用同一 callable descriptor，而不是再做第二套 native-only helper。
- reflective fallback 不应永久停留在 “interface 一律 reject”。更稳妥的插件内路线是：
  - 第一阶段：把 interface callable 收敛到统一 descriptor，并显式区分 `Supported` / `UnsupportedShape` / `PolicyDisabled`；
  - 第二阶段：只让 descriptor 已覆盖的 interface method 进入 runtime 主链；其余继续 reasonful reject，而不是 silent `false` 或匿名拒绝。

**优先级**

- `P0`
- 理由：`Documents/Plans/Plan_CppInterfaceBinding.md:21-23,100-105` 已把“`FInterfaceMethodSignature` 只有 `FName`”与“native interface 需要自动方法注册”明确列入主线；如果 callable owner 继续按来源拆裂，后续即使 property/value bridge 补上，engine/native/script 三侧仍无法形成统一 closeout 口径。

### [D6] 代码生成与 IDE 支持：同一个 interface symbol 仍有三套状态词汇

```
[One Symbol, Three Statuses Today]
UGameplayTagAssetInterface.HasMatchingGameplayTag
├─ Runtime fallback eligibility
│  └─ RejectedInterfaceClass
├─ UHT generated entry
│  └─ Stub / ERASE_NO_FUNCTION()
├─ Skipped ledger
│  └─ no row
└─ Module summary
   └─ GameplayTags stubRate = 1.0
```

**当前状态**

- runtime 侧有一套 `EAngelscriptReflectiveFallbackEligibility` 词汇，明确区分 `RejectedInterfaceClass`、`RejectedCustomThunk` 等，见 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.h:12-20`。
- UHT exporter 的 skipped ledger 只记录 `AngelscriptFunctionSignatureBuilder.TryBuild(...)` 失败的情况，见 `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:65-87`。
- 但 code generator 对 interface class 会无条件写 `ERASE_NO_FUNCTION()`，见 `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:465-479`。
- 当前生成产物里，`Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv:4308-4310` 已把 `UGameplayTagAssetInterface` 三个方法写成 `Stub`；`AS_FunctionTable_SkippedEntries.csv` 对同类名没有对应 row；`AS_FunctionTable_ModuleSummary.csv:6` 则把 `GameplayTags` 记成 `35` 个总 entry、`35` 个 stub、`stubRate = 1.0`。

[5] 当前 source 与 artifact 已经能看出状态词汇分裂：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.h
// 位置: 12-20
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp
// 位置: 261-270
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs
// 位置: 65-87
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs
// 位置: 465-479
// 说明: runtime、exporter、codegen 对 interface symbol 的结论不是同一套 vocabulary
// ============================================================================
enum class EAngelscriptReflectiveFallbackEligibility : uint8
{
	Eligible,
	RejectedNullFunction,
	RejectedMissingOwningClass,
	RejectedInterfaceClass, // ★ runtime 明确知道“interface class 被拒绝”
	RejectedCustomThunk,
	RejectedTooManyArguments,
};

if (OwningClass->HasAnyClassFlags(CLASS_Interface))
{
	return EAngelscriptReflectiveFallbackEligibility::RejectedInterfaceClass;
}

if (AngelscriptFunctionSignatureBuilder.TryBuild(classObj, function, out AngelscriptFunctionSignature? signature, out string? failureReason))
{
	reconstructedCount++;
}
else
{
	skippedEntries.Add(new AngelscriptSkippedFunctionEntry(..., failureReason));
}

if (classObj.ClassType == UhtClassType.Interface || classObj.ClassType == UhtClassType.NativeInterface)
{
	eraseMacro = "ERASE_NO_FUNCTION()"; // ★ 这里变成无 reason 的 forced stub
}
```

```text
# ============================================================================
# 文件: Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv
# 位置: 4308-4310
# 文件: Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_ModuleSummary.csv
# 位置: 6
# 说明: interface forced-stub 已经直接反映到产物与模块统计里
# ============================================================================
GameplayTags,false,UGameplayTagAssetInterface,HasAllMatchingGameplayTags,Stub,ERASE_NO_FUNCTION(),1
GameplayTags,false,UGameplayTagAssetInterface,HasAnyMatchingGameplayTags,Stub,ERASE_NO_FUNCTION(),1
GameplayTags,false,UGameplayTagAssetInterface,HasMatchingGameplayTag,Stub,ERASE_NO_FUNCTION(),1

GameplayTags,false,35,0,35,0,1,1
```

**差距描述**

- `没有实现`：还没有一个共享的 `SymbolKey + DecisionKind + ReasonCode + ProducerStage` 合同，能同时被 runtime、UHT exporter、artifact summary 和 IDE 消费。
- `实现方式不同`：参考方案会在 generator/runtime 两侧保持同一 family identity；当前实现则让 runtime 说“rejected”，UHT 说“stub”，skipped ledger 又保持沉默。
- `实现质量差异`：模块 `stubRate` 目前混入了大量无 reason 的 interface forced-stub，直接削弱了 `Bind API GAP` 报表的可行动性。用户看到的是 “GameplayTags 全是 Stub”，却不知道究竟是 policy、未实现 family，还是签名真的无法重建。

**参考方案**

- **UnrealCSharp** 的 generator/runtime contract 更接近当前主线需要：`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:144-150` 在生成侧把 `FInterfaceProperty` 明确写成 `TScriptInterface<...>`；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:266-269,415-430` 在 runtime 侧再把同一个 `FInterfaceProperty` 映射回 `TScriptInterface<>` generic reflection class。也就是说，generator 与 runtime 至少共享同一份 interface family 身份，而不是各说各话。

[6] 参考实现让 generator/runtime 共用同一份 interface family 真相：

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp
// 位置: 144-150
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp
// 位置: 266-269, 415-430
// 说明: 生成侧与 runtime 侧都把 FInterfaceProperty 归到 TScriptInterface<>
// ============================================================================
if (const auto InterfaceProperty = CastField<FInterfaceProperty>(Property))
{
	return FString::Printf(TEXT("TScriptInterface<%s>"),
		*FUnrealCSharpFunctionLibrary::GetFullInterface(InterfaceProperty->InterfaceClass));
}

if (const auto InterfaceProperty = CastField<FInterfaceProperty>(InProperty))
{
	return GetClass(InterfaceProperty); // ★ runtime 继续走同一个 family
}

const auto FoundGenericClass = FReflectionRegistry::Get().GetTScriptInterfaceClass();
const auto FoundClass = FReflectionRegistry::Get().GetClass(InProperty->InterfaceClass);
return MakeGenericTypeInstance(FoundGenericClass, FoundClass);
```

**吸收建议**

- 先不要尝试一次性统一所有 family。最有价值的窄改动，是先给 interface family 增加共享状态合同：
  - `SymbolKey`
  - `DecisionKind(Direct / Skip / PolicyStub / UnsupportedShape)`
  - `ReasonCode`
  - `ProducerStage(UHTExporter / UHTCodeGen / RuntimeBind / RuntimeFallback / IDEManifest)`
- 对当前 forced-stub 的 interface class，不应再只留下匿名 `Stub`。更可执行的做法是把它记成 `PolicyStub + InterfaceFamilyNotYetBridged` 或等价 reason，并写进 artifact ledger。
- `AS_FunctionTable_ModuleSummary.csv` 的健康度也应从二分的 `DirectBindRate/StubRate` 升级成至少三分：
  - `Direct`
  - `ReasonfulSkip`
  - `PolicyStub`
- 这条工作适合紧跟 `P10` 的第一批 runtime bridge 落地后做，而不是在当前就重写整套报表。

**优先级**

- `P1`
- 理由：它不替代 `P0` 的 runtime capability closeout，但决定 `Bind API GAP` 能否从“人工解读几份 CSV 和运行时现象”升级成“同一 symbol 的状态对账”。考虑到 `Documents/Plans/Plan_StatusPriorityRoadmap.md:236-248` 明确要求优先解决当前 blocker、避免被大范围 parity 扩面牵着走，因此更适合作为 interface family 起步后的第一批量化配套。

### 值得吸收的设计模式（增量）

- `Property family first, containers inherit automatically`：先让 `FInterfaceProperty` 成为一等 property family，`TArray/TSet/TOptional` 才能通过 subtype 递归自动吃到收益；反过来先修 container 只会把同一缺口复制到更多入口。
- `One callable descriptor across script/native/engine sources`：interface callable 的 owner 不应由来源决定；来源最多影响 `ProducerStage`，不能决定是 name-only lane、manual helper lane 还是 reject lane。
- `Reasonful policy stub`：如果当前策略必须把某类 symbol forced-stub，也要把 reason 以可 join 的字段写进 ledger；匿名 `Stub` 只会制造伪指标。

### 改进路线建议（增量）

1. `P0`：在 `Core/AngelscriptType.*` 与 `Bind_BlueprintType.cpp` 先补最小 `FInterfaceProperty` family owner，连通 `GetByProperty / CreateProperty / MatchesProperty / FScriptInterface` 双槽写回；让 `TArray/TOptional` 自动继承这条能力。
2. `P0`：把 `FInterfaceMethodSignature{FName}` 升级为结构化 callable descriptor，并让脚本 interface 与 native interface 自动注册共用同一 owner；engine interface 暂未支持的形状继续显式 reasonful reject，不再维持“整类一律匿名拒绝”。
3. `P1`：给 interface family 建立共享 `DecisionKind + ReasonCode + ProducerStage` 合同，先拿 `UGameplayTagAssetInterface`、`UCameraLensEffectInterface` 这类已经出现在 `AS_FunctionTable_Entries.csv` 的 engine interface 做试点，把 `Bind API GAP` 量化收敛到“同一 symbol 的多阶段状态对账”。

---
## 深化分析 (2026-04-09 00:52:06)

### 执行摘要（增量）

- `P10 UInterface` 当前真正缺的不是单一 `adapter`，而是两层 owner 同时缺位：`property -> type family` 仍是 append-order first-match，`value -> FScriptInterface` 也没有显式双槽桥。
- UHT 生成物虽然有稳定 shard 文件名，但注册到 runtime 时没有保留同一 producer identity；`AS_FunctionTable_Engine_000.cpp` 最终会退化成 `UnnamedBind_N` 风格的匿名 bind。
- `BindModules.Cache` 的写入根和读取根不一致：editor 侧写到 project script root，runtime 侧却从 plugin base dir 读取，这使 bind module manifest 本身也不是 authoritative source。
- 当前 generated function table 自动化主要守“列头/计数/样例行”，还没有守“CSV 行 -> shard 文件 -> runtime bind producer”这条 join contract；这会直接削弱 `Bind API GAP` 的定位效率。

### 差距矩阵（增量）

| 维度 | 新增观察点 | 差距等级 | 优先级 |
| --- | --- | --- | --- |
| D2 反射绑定机制 | `FInterfaceProperty` 不仅缺 family，还缺 dispatch priority 与 `FScriptInterface` 双槽 value bridge | 能力缺失 | `P0` |
| D1 插件架构与模块划分 | generated bind 的 producer identity 在文件、CSV、module manifest、runtime bind 四段不一致 | 实现质量差异 | `P1` |
| D6 代码生成与 IDE 支持 | `AS_FunctionTable_Entries.csv` 不能稳定 join 到实际 shard / bind producer，自动化也未守这条合同 | 实现质量差异 | `P2` |

### [D2] 反射绑定机制：`UInterface` 还没有“优先级分发 + 双槽 value bridge”这一对 owner

```text
[Current] Interface Property Flow
FProperty
├─ TypeFinders (append-order)                    // 先命中的 finder 直接决定 family
│  ├─ TArray / TMap / TSet / TOptional
│  └─ Object / WeakObject finder                // 只识别 object-like property
├─ TypesImplementingProperties (append-order)    // 没有显式优先级
└─ BindHelpers
   ├─ UObject** read/write                       // 只处理 object pointer
   └─ raw CopySingleValue                        // 没有 FScriptInterface 专用双槽写回

[Reference] UnrealCSharp Interface Bridge
FProperty
├─ explicit CastField<FInterfaceProperty>        // family dispatch 是显式分支
├─ generator prints TScriptInterface<...>        // 生成侧保留 interface family
└─ descriptor writes Object + Interface slot     // runtime 同步写回两槽
```

[1] 当前 `property -> type family` 仍是 append-order first-match，没有 interface 优先级合同：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp
// 位置: 54-99, 147-168, 234-248
// 说明: 类型注册、property 发现和 property -> usage 重建都依赖 append-order
// ============================================================================
void FAngelscriptType::Register(TSharedRef<FAngelscriptType> Type)
{
	...
	if (Type->CanQueryPropertyType())
	{
		Database.TypesImplementingProperties.Add(Type); // ★ 仅按注册顺序追加
	}
}

TSharedPtr<FAngelscriptType> FAngelscriptType::GetByProperty(FProperty* Property, bool bQueryTypeFinders)
{
	...
	for (auto& Finder : Database.TypeFinders)
	{
		if (Finder(Property, Usage))
			return Usage.Type; // ★ 第一个命中的 finder 直接决定结果
	}
	...
	for (auto& CheckType : Database.TypesImplementingProperties)
	{
		if (CheckType->MatchesProperty(...))
			return CheckType; // ★ 第二层同样是 first-match
	}
	return nullptr;
}

FAngelscriptTypeUsage FAngelscriptTypeUsage::FromProperty(FProperty* Property)
{
	...
	for (auto& Finder : Database.TypeFinders)
	{
		if (Finder(Property, Usage))
			break; // ★ 重建路径没有 explicit priority / family tag
	}
	if (!Usage.Type.IsValid())
	{
		Usage.Type = FAngelscriptType::GetByProperty(Property, false);
	}
}
```

[2] 当前 object-like finder 与 property helper 仍没有 `FInterfaceProperty` 专用 owner：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp
// 位置: 2438-2478
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Helpers.h
// 位置: 42-69
// 说明: object-like finder 只看 FObjectProperty/FWeakObjectProperty；helper 只有 object/raw copy
// ============================================================================
FAngelscriptType::RegisterTypeFinder([=](FProperty* Property, FAngelscriptTypeUsage& Usage) -> bool
{
	const FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property);
	if (ObjectProperty == nullptr)
	{
		const FWeakObjectProperty* WeakObjectProperty = CastField<FWeakObjectProperty>(Property);
		if (WeakObjectProperty != nullptr)
		{
			Usage.Type = WeakObjectPtrType;
			Usage.SubTypes[0] = InnerType;
			return true;
		}
		return false; // ★ FInterfaceProperty 在这里没有任何入口
	}
	...
	Usage.Type = ObjectPtrType;
	Usage.SubTypes[0] = InnerType;
	return true;
});

static UObject* GetObjectFromProperty(void* Container, asCScriptFunction* Function)
{
	SIZE_T Offset = (SIZE_T)Function->userData;
	return *(UObject**)((SIZE_T)Container + Offset); // ★ 只按 UObject* 单槽读取
}

static void SetObjectFromProperty(void* Container, asCScriptFunction* Function, UObject* NewValue)
{
	SIZE_T Offset = (SIZE_T)Function->userData;
	*(UObject**)((SIZE_T)Container + Offset) = NewValue; // ★ 没有 FScriptInterface::SetObject/SetInterface
}

static void SetValueFromProperty(void* Container, asCScriptFunction* Function, void* NewValue)
{
	auto* Prop = (FProperty*)Function->userData;
	Prop->CopySingleValue(...); // ★ generic copy 不是 interface family 的显式 bridge
}
```

[3] `UObject::ImplementsInterface()` 与 runtime cast 目前只证明类关系，不提供 interface value bridge：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp
// 位置: 100-106, 185-190
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 112-127
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h
// 位置: 59-62
// 说明: 当前 runtime 只保存 FunctionName，并用 ImplementsInterface() 做 class-level 判定
// ============================================================================
UObject_.Method("bool ImplementsInterface(const UClass InterfaceClass) const",
[](UObject* Object, UClass* InterfaceClass)
{
	if (Object == nullptr || InterfaceClass == nullptr)
		return false;
	return Object->GetClass()->ImplementsInterface(InterfaceClass); // ★ 只有类关系判断
});

const bool bAssociatedClassIsInterface = AssociatedClass->HasAnyClassFlags(CLASS_Interface);
const bool bImplementsInterface = bAssociatedClassIsInterface
	&& Object->GetClass()->ImplementsInterface(AssociatedClass);

struct FInterfaceMethodSignature
{
	FName FunctionName; // ★ callable metadata 仍只有名字
};
```

[4] 参考方案把 interface family 和 value bridge 都做成显式 owner：

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp
// 位置: 144-150
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp
// 位置: 266-268, 415-430
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/ObjectProperty/FInterfacePropertyDescriptor.cpp
// 位置: 29-45
// 说明: generator/runtime/descriptor 共享同一个 FInterfaceProperty family
// ============================================================================
if (const auto InterfaceProperty = CastField<FInterfaceProperty>(Property))
{
	return FString::Printf(TEXT("TScriptInterface<%s>"),
		*FUnrealCSharpFunctionLibrary::GetFullInterface(InterfaceProperty->InterfaceClass));
}

if (const auto InterfaceProperty = CastField<FInterfaceProperty>(InProperty))
{
	return GetClass(InterfaceProperty); // ★ runtime 先显式命中 interface family
}

const auto FoundGenericClass = FReflectionRegistry::Get().GetTScriptInterfaceClass();
const auto FoundClass = FReflectionRegistry::Get().GetClass(InProperty->InterfaceClass);
return MakeGenericTypeInstance(FoundGenericClass, FoundClass);

const auto Interface = static_cast<FScriptInterface*>(Dest);
const auto Object = SrcMulti->GetObject();
Interface->SetObject(Object);
Interface->SetInterface(Object ? Object->GetInterfaceAddress(Property->InterfaceClass) : nullptr); // ★ 双槽一起写
```

**当前状态**

- `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:54-99,147-168,234-248` 把 property family 发现做成了 `TypeFinders + TypesImplementingProperties` 两层 append-order first-match。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:2438-2478` 的 object-like finder 只认识 `FObjectProperty` / `FWeakObjectProperty`，没有 `FInterfaceProperty` 的优先分支。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Helpers.h:42-69` 只有 `UObject**` 和 generic `CopySingleValue` helper，没有 `FScriptInterface` 双槽桥。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp:100-106,185-190` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:112-127` 当前主要守的是“类是否实现了 interface”，不是“如何把 interface value 进出 property/callable”。

**差距描述**

- `没有实现`：还没有正式的 `FInterfaceProperty -> TypeUsage -> FScriptInterface bridge` 主链。
- `实现方式不同`：参考方案把 interface family 做成显式分支和专用 descriptor；当前实现把 interface 暂时挤在 object-like / class-relation 特例外侧。
- `实现质量差异`：即使后续补一个最小 interface adapter，当前 append-order 分发也会继续让新增 family 依赖“注册顺序碰巧正确”，可维护性仍然偏弱。

**参考方案**

- `UnrealCSharp` 的参考点不是“支持了 C#”，而是它把 `FInterfaceProperty` 做成了 generator/runtime/property-descriptor 三段统一 owner：
  - `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:144-150`
  - `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:266-268,415-430`
  - `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/ObjectProperty/FInterfacePropertyDescriptor.cpp:29-45`

**吸收建议**

- `P10` 不要只补 `CreateProperty()`。更稳的切入顺序是：
  - 先给 `TypeFinders` 增加 explicit `FInterfaceProperty` 优先分支，禁止它继续依赖 object-like finder 的 append-order。
  - 给 `Bind_Helpers` 增加 `Get/SetInterfaceFromProperty` 级别的 helper，显式处理 `FScriptInterface::SetObject/SetInterface` 双槽。
  - `FInterfaceMethodSignature` 至少扩成 `OwnerClass + FunctionName + Param/ReturnShape` 的结构化 descriptor，避免 property 已经 typed，callable 仍是 name-only。
- 若短期只想做最小闭环，建议先覆盖三类形状：
  - `TScriptInterface<IMyInterface>`
  - `const TScriptInterface<IMyInterface>&`
  - `TArray<TScriptInterface<IMyInterface>>`

**优先级**

- `P0`
- 理由：这是 `P10 UInterface` 的直接 blocker，不属于“更多 parity 扩面”。如果这里不先收口，后面的 `Bind API GAP` 统计和测试都只能继续围绕 `Stub/Reject` 打转。

### [D1] 插件架构与模块划分：generated bind 的 producer identity 目前在四段链路里各说各话

```text
[Current] Generated Bind Identity
AS_FunctionTable_Engine_000.cpp                // 生成文件身份
├─ Entries.csv -> ShardIndex=1                // sidecar 身份
├─ static FBind(order only)                   // 注册时没带 bind name
│  └─ UnnamedBind_N                           // runtime bind 身份
└─ BindModules.Cache
   ├─ save -> ProjectScriptRoot               // editor 写入根
   └─ load -> PluginBaseDir                   // runtime 读取根

[Reference] Stable Identity
registration key -> runtime registry -> artifact root
└─ one explicit class/root key survives all stages
```

[1] UHT shard 文件名是稳定的，但生成的 `FBind` 没有把这份 identity 传进 runtime：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs
// 位置: 120-121, 302-306
// 文件: Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Engine_000.cpp
// 位置: 221-223
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h
// 位置: 455-467
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp
// 位置: 138-153
// 说明: 文件名有 shard identity，但 runtime 注册入口只拿到了 bind order
// ============================================================================
string outputPath = factory.MakePath($"AS_FunctionTable_{module.ShortName}_{shardIndex:D3}", ".cpp");
factory.CommitOutput(outputPath, BuildShard(..., shardIndex, shardCount));

builder.Append("AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_AS_FunctionTable_")
	.Append(moduleShortName).Append('_').Append(shardIndex.ToString("D3"))
	.AppendLine("((int32)FAngelscriptBinds::EOrder::Late + 50, []()");

AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_AS_FunctionTable_Engine_000((int32)FAngelscriptBinds::EOrder::Late + 50, []()
{
	FAngelscriptBinds::AddFunctionEntry(AActor::StaticClass(), "ActorHasTag", { ERASE_AUTO_METHOD_PTR(AActor, ActorHasTag) });
});

FBind(int32 BindOrder, TFunction<void()> Function)
{
	FAngelscriptBinds::RegisterBinds(BindOrder, MoveTemp(Function)); // ★ 没有 bind name
}

static FName MakeUnnamedBindName()
{
	return FName(*FString::Printf(TEXT("UnnamedBind_%d"), NextUnnamedBindId++));
}

void FAngelscriptBinds::RegisterBinds(FName BindName, int32 BindOrder, TFunction<void()> Function)
{
	GetBindArray().Add({BindName.IsNone() ? MakeUnnamedBindName() : BindName, BindOrder, MoveTemp(Function)});
}
```

[2] `BindModules.Cache` 的持久化根也不是同一份 authority：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 位置: 1017-1057, 1077
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 781-794, 1473-1488
// 说明: editor 写 project script root；runtime 却从 plugin base dir 读取
// ============================================================================
FString ModuleName = FString("ASRuntimeBind_");
ModuleName += FString::FromInt(i - (ModuleArray.Num() - 1));
GenerateNewModule(ModuleName, ModuleArray, false);
FAngelscriptBinds::GetBindModuleNames().Add(ModuleName);

FString ModuleName = FString("ASEditorBind_");
ModuleName += FString::FromInt(i - (ModuleArray.Num() - 1));
GenerateNewModule(ModuleName, ModuleArray, true);
FAngelscriptBinds::GetBindModuleNames().Add(ModuleName);

FAngelscriptBinds::SaveBindModules(FString(FAngelscriptEngine::GetScriptRootDirectory() / "BindModules.Cache"));

FString FAngelscriptEngine::GetScriptRootDirectory()
{
	return AllRootPaths.IsEmpty() ? TEXT("") : CurrentEngine->AllRootPaths[0]; // ★ project script root
}

TSharedPtr<IPlugin> plugin = IPluginManager::Get().FindPlugin("Angelscript");
if (plugin)
{
	FAngelscriptBinds::LoadBindModules(plugin->GetBaseDir() / "BindModules.Cache"); // ★ plugin base dir
}
```

[3] 参考方案至少会把“注册 key”保留下来，而不是在入口处匿名化：

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Binding/FBinding.cpp
// 位置: 44-64
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Binding/Class/FBindingClassRegister.cpp
// 位置: 47-63
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaFunctionLibrary.cpp
// 位置: 20-23
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp
// 位置: 186-239
// 说明: 一个参考保留显式 registration key；另一个参考保证 runtime root 与 packaging root 同源
// ============================================================================
const auto Class = InClassFunction();
for (auto& ClassRegister : ClassRegisters)
{
	if (!ClassRegister->IsReflectionClass())
	{
		if (Class == ClassRegister->GetClass())
		{
			return ClassRegister; // ★ runtime registry 按显式 class key 去重
		}
	}
}

return new FBindingClass(..., ClassFunction(), ...); // ★ class key 继续进入运行时对象

FString UUnLuaFunctionLibrary::GetScriptRootPath()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() + TEXT("Script/"));
}

auto ScriptPaths = TArray<FString>{TEXT("Script"), TEXT("../Plugins/UnLua/Content/Script")};
PackagingSettings->DirectoriesToAlwaysStageAsUFS.Add(DirectoryPath); // ★ runtime root 与 staging root 同源
```

**当前状态**

- `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:120-121,302-306` 生成了稳定 shard 文件名，但没有把同名 `Bind_AS_FunctionTable_*` 作为 runtime bind key 传进去。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:455-467` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:138-153` 说明 order-only `FBind` 一律被降成 `UnnamedBind_N`。
- `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1017-1057,1077` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:781-794,1473-1488` 说明 bind module manifest 的写入根与读取根也不一致。

**差距描述**

- `没有实现`：还没有一个从 `generated file -> manifest -> runtime bind` 贯通的 single producer key。
- `实现方式不同`：当前实现把 identity 分散成 `文件名 / CSV 行号 / module name / UnnamedBind_N` 四套局部标识；参考方案更倾向于把 key 显式保留到运行时。
- `实现质量差异`：一旦出现 generated bind 回归，当前很难回答“是哪一个 shard、哪一个 manifest entry、哪一个 runtime bind 在负责这段注册”，排障成本明显偏高。

**参考方案**

- `UnrealCSharp` 值得吸收的是“显式 registration key 不在运行时入口处丢失”：
  - `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Binding/FBinding.cpp:44-64`
  - `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Binding/Class/FBindingClassRegister.cpp:47-63`
- `UnLua` 值得吸收的是“runtime root 与交付 root 使用同一 authority”：
  - `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaFunctionLibrary.cpp:20-23`
  - `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:186-239`

**吸收建议**

- 先不要重做整个 bind module 体系。最小、可执行的修正顺序是：
  - 给 UHT 生成的 `FBind` 显式传入 bind name，例如 `FName("AS_FunctionTable_Engine_000")`，停止在注册入口匿名化。
  - `BindModules.Cache` 的 save/load 必须改成同一 authority；若继续放在 project script root，runtime 也应从同一路径读。
  - 新增 `GeneratedProducerManifest`，至少记录 `ProducerId / OutputFile / ModuleName / BindName / ShardIndex`。
- `ASRuntimeBind_*` / `ASEditorBind_*` 与 `AS_FunctionTable_*` 不一定要统一成同一模块系统，但必须能在 manifest 里互相引用，而不是互相看不见。

**优先级**

- `P1`
- 理由：这不是 `P0` 的能力闭环本身，但它决定 `Bind API GAP` 后续到底是“看 CSV 猜责任人”，还是“拿到 symbol 直接定位 producer”。按照 `Documents/Plans/Plan_StatusPriorityRoadmap.md:236-248` 的优先级原则，这类 observability / delivery baseline 工作应先于继续扩面。

### [D6] 代码生成与 IDE 支持：当前 artifact contract 还不能把一行 GAP 记录稳定 join 回真实 producer

```text
[Current] GAP Artifact Join
AS_FunctionTable_Entries.csv
├─ ModuleName / ClassName / FunctionName
├─ EntryKind / EraseMacro
└─ ShardIndex                               // 只有数字，没有 output file / bind name

Generated tests
├─ header equality
├─ total row count
└─ one representative line contains Direct  // 不校验 row -> file -> runtime producer
```

[1] 当前 UHT sidecar 只有 `ShardIndex`，而且它与文件名的编号系统并不一致：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs
// 位置: 115-135, 246-260
// 文件: Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv
// 位置: 1-6
// 说明: 文件名用 000-based shard，CSV 却写 1-based index，且没有 OutputFile / BindName
// ============================================================================
int shardCount = (entries.Count + MaxEntriesPerShard - 1) / MaxEntriesPerShard;
for (int shardIndex = 0; shardIndex < shardCount; shardIndex++)
{
	string outputPath = factory.MakePath($"AS_FunctionTable_{module.ShortName}_{shardIndex:D3}", ".cpp");
	...
	csvEntries.Add(new AngelscriptGeneratedFunctionCsvEntry(
		module.ShortName,
		editorOnly,
		entry.ClassName,
		entry.FunctionName,
		entry.EraseMacro == "ERASE_NO_FUNCTION()" ? "Stub" : "Direct",
		entry.EraseMacro,
		shardIndex + 1)); // ★ CSV 是 1-based
}

builder.AppendLine("ModuleName,EditorOnly,ClassName,FunctionName,EntryKind,EraseMacro,ShardIndex");

ModuleName,EditorOnly,ClassName,FunctionName,EntryKind,EraseMacro,ShardIndex
Engine,false,AActor,ActorHasTag,Direct,"ERASE_AUTO_METHOD_PTR(AActor, ActorHasTag)",1
Engine,false,AActor,AddTickPrerequisiteActor,Direct,"ERASE_AUTO_METHOD_PTR(AActor, AddTickPrerequisiteActor)",1
```

[2] 现有自动化仍在守“表形状”，没有守 join contract：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp
// 位置: 644-665, 683-702, 727-748
// 说明: 自动化校验 header、计数和样例行，但没有验证 row -> shard file -> runtime producer
// ============================================================================
TestEqual(..., EntryLines[0], TEXT("ModuleName,EditorOnly,ClassName,FunctionName,EntryKind,EraseMacro,ShardIndex"));

if (EntryLine.Contains(TEXT(",RunBehaviorTree,")))
{
	bFoundRunBehaviorTreeCsv = true;
	TestTrue(..., EntryLine.Contains(TEXT(",Direct,")));
	TestFalse(..., EntryLine.Contains(TEXT("ERASE_NO_FUNCTION()")));
	break;
}

SkippedLines[LineIndex].ParseIntoArray(Columns, TEXT(","), false);
TestTrue(..., Columns.Num() == 4);

SummaryLines[LineIndex].ParseIntoArray(Columns, TEXT(","), false);
TestTrue(..., Columns.Num() == 2);
```

[3] 参考方案把 identity 放在结构化类型/类 key 上，而不是只给一个数字 shard index：

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp
// 位置: 144-150, 328-334, 720-723
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp
// 位置: 266-268, 415-430
// 说明: generator 产物保存结构化 type/class identity，runtime 继续消费同一份 identity
// ============================================================================
if (const auto InterfaceProperty = CastField<FInterfaceProperty>(Property))
{
	return FString::Printf(TEXT("TScriptInterface<%s>"),
		*FUnrealCSharpFunctionLibrary::GetFullInterface(InterfaceProperty->InterfaceClass));
}

if (const auto InterfaceProperty = CastField<FInterfaceProperty>(Property))
{
	return {
		COMBINE_NAMESPACE(NAMESPACE_ROOT, NAMESPACE_CORE_UOBJECT),
		FUnrealCSharpFunctionLibrary::GetClassNameSpace(InterfaceProperty->InterfaceClass)
	};
}

if (const auto InterfaceProperty = CastField<FInterfaceProperty>(Property))
{
	return IsSupported(InterfaceProperty->InterfaceClass);
}

if (const auto InterfaceProperty = CastField<FInterfaceProperty>(InProperty))
{
	return GetClass(InterfaceProperty);
}
```

**当前状态**

- `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:115-135,246-260` 只给 sidecar 写了 `ShardIndex`，没有 `OutputFile`、`BindName`、`ProducerId`。
- `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv:1-6` 与 `AS_FunctionTable_Engine_000.cpp` 现实中已经是 “CSV=1, 文件=000” 两套编号。
- `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp:644-665,683-702,727-748` 目前没有任何断言在验证这条 join。

**差距描述**

- `没有实现`：缺少可稳定连接 `Entries.csv`、实际 `.cpp` shard、runtime bind name 的 artifact key。
- `实现方式不同`：参考方案更偏向保留结构化类/类型 identity；当前生成物主要靠 `Module/Class/Function + numeric shard` 近似表达。
- `实现质量差异`：只要需要从 `Bind API GAP` 的某一行回跳到“谁生成的、谁注册的、谁执行的”，当前 sidecar 仍然要依赖额外人工推断。

**参考方案**

- `UnrealCSharp` 的可吸收点是：生成侧把 type/class identity 保存成结构化名字，runtime 再按同一 identity 消费，而不是中途压成匿名数字：
  - `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:144-150,328-334,720-723`
  - `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:266-268,415-430`

**吸收建议**

- 不建议只给 CSV 再加几列文本。更有价值的是先定义一个 machine-joinable `ProducerId`：
  - `AS.FunctionTable.Engine.000`
  - 或 `UHT:Engine:000`
- 然后让以下产物都带上同一字段：
  - shard `.cpp` 文件头
  - `AS_FunctionTable_Entries.csv`
  - `AS_FunctionTable_Summary.json`
  - runtime bind info / debug dump
- 自动化应新增三类断言：
  - `Entries.csv` 的 `ProducerId` 能定位到唯一 `.cpp` shard
  - shard 的 `ProducerId` 能定位到唯一 runtime bind name
  - `EntryKind/ReasonCode` 与 runtime 观测结果一致

**优先级**

- `P2`
- 理由：它直接提升 `Bind API GAP` 的可诊断性，但仍属于 `P0/P1` 之后的 artifact/tooling 收口；应建立在 interface family 已有最小 runtime owner、generated bind 也已有显式 bind name 的前提上。

### 值得吸收的设计模式（增量）

- `Priority before family proliferation`：在扩新 family 前，先把 property dispatch 从 append-order 提升为 explicit priority；否则每加一个 family 都是在赌注册顺序。
- `Producer identity must survive registration`：文件名、manifest、runtime bind 如果不能共享同一个 key，后续所有 GAP 报表都只能停在“现象描述”。
- `Artifact rows should be machine-joinable`：报表字段不只为人读，也要能自动跳回生成文件、运行时 producer 和测试断言。

### 改进路线建议（增量）

1. `P0`：先在 `TypeFinders` 和 `Bind_Helpers` 落最小 `FInterfaceProperty + FScriptInterface` 双 owner，打通 property/value 主链。
2. `P1`：让 UHT generated `FBind` 停止匿名注册，并修正 `BindModules.Cache` 的 save/load authority，使 generated bind producer 有稳定 runtime identity。
3. `P2`：在 `AS_FunctionTable_*` 产物和自动化里引入 `ProducerId`/`BindName`/`OutputFile` join contract，把 `Bind API GAP` 从人工排查升级成可自动定位的账本。

## 深化分析 (2026-04-09 01:03:23)

### 本轮新增关键发现

- native `UInterface` 上最容易先撞线的不是 `TScriptInterface<>`，而是 `BlueprintNativeEvent` 风格的方法 owner。当前仓库里这类方法同时落在“测试 helper 手工补绑可调用”“reflective fallback 必须拒绝”“UHT function table 统一 stub”三套 lane 上。
- 参考插件的共同做法不是把 interface-owner `UFunction` 当成独立旁路，而是把 interface 方法混进实现类 / wrapper / override 主路径，再把 `BlueprintNativeEvent`、`CannotImplementInterfaceInBlueprint` 等 authoring policy 前移成结构化 metadata。
- 因此 `P10 UInterface` 不能只做“类型可见 + 方法可见”。如果没有先定义 native interface event 的 canonical lane，`Bind API GAP`、IDE artifact、测试与运行时会继续各记一份互相矛盾的真相。

### 差距矩阵（增量）

| 维度 | 新增观察点 | 等级 | 主要差距类型 | 优先级 |
| --- | --- | --- | --- | --- |
| D3 Blueprint 交互 | native `UInterface(BlueprintNativeEvent)` 的 canonical call lane | 能力缺失 | helper / reflective / generated 三条 lane 没有统一 owner | P0 |
| D6 代码生成与 IDE 支持 | interface authoring policy metadata（`BlueprintNativeEvent` / `CannotImplementInterfaceInBlueprint`） | 能力缺失 | 当前只有 flag/class-type 判定，没有结构化 policy artifact | P1 |
| D9 测试基础设施 | interface lane-aware regression vocabulary | 实现质量差异 | 自动化同时锁定“必须拒绝”和“可以调用”两种互斥 contract | P1 |

### [D3] Blueprint 交互：native interface event 还没有 canonical call lane

```
[Current] Native Interface Event Lane
Test helper
├─ ReferenceClass(UInterface)                    // 测试手工注册类型
├─ GenericMethod("GetNativeValue")              // 测试手工注册接口方法
└─ Cast<UInterface>(Self).GetNativeValue()      // 脚本调用走 helper lane

Auto / artifact lanes
├─ Bind_BlueprintType -> BindBlueprintEvent      // 自动发现时会按 BlueprintEvent 走 event binder
├─ ReflectiveFallback -> RejectedInterfaceClass  // reflective fallback 明确拒绝 interface owner
└─ UHT FunctionTable -> ERASE_NO_FUNCTION()      // function table 对 interface class 统一 stub
```

**当前状态**

- `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptNativeInterfaceTestTypes.h:19-26,40-41` 的原生接口夹具全部声明成 `BlueprintCallable + BlueprintNativeEvent`，这正是 `P10` 最先要接通的真实方法形状。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:747-754,1397-1405` 显示 runtime 自动 lane 会把 `FUNC_BlueprintEvent` 统一送进 `BindBlueprintEvent(...)`，但当前通过中的 native interface 回归并没有走这条产品路径。
- `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceNativeTests.cpp:47-77` 先手工 `ReferenceClass + GenericMethod` 补上 interface type 与方法，再让脚本里的 `Cast<UAngelscriptNativeParentInterface>(Self)` 和 `ParentRef.GetNativeValue()` 通过。
- 与此同时，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp:261-270` 又把 interface-owner `UFunction` 明确判成 `RejectedInterfaceClass`，并在 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptBlueprintCallableReflectiveFallbackTests.cpp:230-245` 中被锁成正确行为。

[1] 当前 passing path 依赖测试 helper，而不是 production auto lane：

```cpp
// ============================================================================
// [1] 文件: Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptNativeInterfaceTestTypes.h
// 位置: 19-26, 40-41
// 说明: 测试夹具上的 native interface 方法本身就是 BlueprintNativeEvent
// ============================================================================
UFUNCTION(BlueprintCallable, BlueprintNativeEvent)
int32 GetNativeValue() const;

UFUNCTION(BlueprintCallable, BlueprintNativeEvent)
void SetNativeMarker(FName Marker);

UFUNCTION(BlueprintCallable, BlueprintNativeEvent)
void AdjustNativeValue(int32 Delta, UPARAM(ref) int32& Value);

UFUNCTION(BlueprintCallable, BlueprintNativeEvent)
int32 GetChildValue() const;

// ============================================================================
// [1] 文件: Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceNativeTests.cpp
// 位置: 47-77, 153-160
// 说明: 当前 native interface 测试先手工补绑，再让脚本通过 Cast<UInterface> 调方法
// ============================================================================
FAngelscriptBinds Binds = FAngelscriptBinds::ReferenceClass(TypeName, InterfaceClass);
...
BindNativeInterfaceMethod(Binds, TEXT("int GetNativeValue() const"), TEXT("GetNativeValue"));
BindNativeInterfaceMethod(Binds, TEXT("void SetNativeMarker(FName Marker)"), TEXT("SetNativeMarker"));
BindNativeInterfaceMethod(Binds, TEXT("void AdjustNativeValue(int Delta, int& Value)"), TEXT("AdjustNativeValue"));

UObject Self = this;
UAngelscriptNativeParentInterface ParentRef = Cast<UAngelscriptNativeParentInterface>(Self);
if (ParentRef != nullptr)
{
	ParentCastWorked = 1;
	NativeValue = ParentRef.GetNativeValue();      // ★ 这里走的是 helper lane
	ParentRef.SetNativeMarker(n"FromScript");
}

// ============================================================================
// [1] 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp
// 位置: 261-270
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptBlueprintCallableReflectiveFallbackTests.cpp
// 位置: 230-245
// 说明: 另一条产品补偿路径又把 interface-owner UFunction 视为必须拒绝
// ============================================================================
const UClass* OwningClass = Function->GetOuterUClass();
...
if (OwningClass->HasAnyClassFlags(CLASS_Interface))
{
	return EAngelscriptReflectiveFallbackEligibility::RejectedInterfaceClass; // ★ interface owner 被显式拒绝
}

const UFunction* InterfaceFunction = UGameplayTagAssetInterface::StaticClass()->FindFunctionByName(TEXT("HasMatchingGameplayTag"));
TestEqual(
	TEXT("Reflective fallback should reject interface-class functions explicitly"),
	EvaluateReflectiveFallbackEligibility(InterfaceFunction),
	EAngelscriptReflectiveFallbackEligibility::RejectedInterfaceClass);
```

**差距描述**

- `能力缺失`：native interface event 还没有统一的 production call lane。当前能证明的只有“测试 helper lane 可通”和“C++ `Execute_` 可通”，不能证明自动绑定/生成物/reflective fallback 已经对齐。
- `实现方式不同`：参考插件倾向于把 interface 方法混入实现类或 wrapper 的正常调用面，而不是长期保留一个“interface-owner function 必须拒绝”的旁路判定。
- `实现质量差异`：现在的 passing signal 容易高估现状。`AngelscriptInterfaceNativeTests.cpp` 保护的是 helper lane，`ReflectiveFallbackEligibilityTest` 保护的是 reject lane；二者都能 pass，但没有告诉后续实现者“真正应该保哪条 lane”。

**参考方案**

- **UnLua**：`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaFunction.cpp:102-117` 在收集可覆写函数时直接 `IncludeInterfaces`；`Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Tests/Issue595Test.cpp:33-41` 再用单条产品路径同时验证 `UE.UIssue595Interface.Test(Obj)` 和 `Obj:Test()`。
- **puerts**：`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp:312-324` 把 `Class->Interfaces` 上的方法直接并入对象 prototype；`Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:1311-1319` 声明生成也沿同一套 interface 枚举逻辑输出。

[2] 参考插件都把 interface 方法并回“正常对象/声明面”，而不是长期保留 reject lane：

```cpp
// ============================================================================
// [2] 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaFunction.cpp
// 位置: 102-117
// 说明: UnLua 的 override 收集直接把 interface 方法并入同一张函数表
// ============================================================================
for (TFieldIterator<UFunction> It(Class, EFieldIteratorFlags::IncludeSuper,
	EFieldIteratorFlags::ExcludeDeprecated, EFieldIteratorFlags::IncludeInterfaces); It; ++It)
{
	UFunction* Function = *It;
	if (!IsOverridable(Function))
		continue;
	Functions.Add(Function->GetFName(), Function); // ★ interface 和普通 BlueprintEvent 走同一收集面
}

// ============================================================================
// [2] 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Tests/Issue595Test.cpp
// 位置: 33-41
// 说明: 单条产品路径同时验证“接口静态调用”和“对象调用”
// ============================================================================
local Obj = NewObject(UE.UIssue595Object)
UE.UIssue595Interface.Test(Obj) // ★ interface 入口
return Obj:Test()               // ★ object 入口

// ============================================================================
// [2] 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/StructWrapper.cpp
// 位置: 312-324
// 文件: Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp
// 位置: 1311-1319
// 说明: puerts 的 runtime wrapper 与 declaration generator 都会遍历 Class->Interfaces
// ============================================================================
for (const FImplementedInterface& Interface : Class->Interfaces)
{
	for (TFieldIterator<UFunction> ItfFuncIt(Interface.Class, EFieldIteratorFlags::ExcludeSuper); ItfFuncIt; ++ItfFuncIt)
	{
		UFunction* ItfFunction = *ItfFuncIt;
		auto ItfFunctionTranslator = GetMethodTranslator(ItfFunction, false); // ★ interface 方法并入对象 wrapper
	}
}

for (int i = 0; i < Class->Interfaces.Num(); i++)
{
	for (TFieldIterator<UFunction> FunctionIt(Class->Interfaces[i].Class, EFieldIteratorFlags::IncludeSuper); FunctionIt; ++FunctionIt)
	{
		if (!GenFunction(TmpBuff, *FunctionIt))
			continue; // ★ declaration 侧继续沿同一 interface 枚举输出
	}
}
```

**吸收建议**

- `P10` 第一阶段不要再把 native interface event 继续分成“helper 先补、fallback 先拒、UHT 先 stub”三条 lane。先明确一个 canonical lane，建议优先采用“实现类/对象 owner 调用面”，让 interface 方法像 UnLua/puerts 一样并入实现类或 wrapper，而不是直接把 interface-owner `UFunction` 当最终执行体。
- 如果短期仍需保留 `Execute_` 作为过渡 owner，也应该把它正式写进 contract，例如 `InterfaceCallLane = ExecuteBridge`，并让 UHT sidecar、debug database、测试命名都使用同一词汇；不要继续靠 `EnsureNativeInterfaceFixturesBound()` 暗含这条事实。
- `Bind_BlueprintType.cpp` 中 future `P2.1` 的方法自动注册必须与这个 lane 绑定一起落地，否则只是把“方法可见”做出来，但真正执行时仍会在 helper / reject / stub 三条语义里打架。

**优先级**

- `P0`
- **理由**：`Documents/Plans/Plan_CppInterfaceBinding.md:96-106` 的 Phase 2 就是“C++ UInterface 自动方法注册”。如果不先收口 canonical lane，Phase 2 只会把现有 helper lane 和 reject lane 的冲突搬进生产代码。

### [D6] 代码生成与 IDE 支持：interface authoring policy 还不是结构化 metadata

```
[Current] Interface Policy Facts
Bind_BlueprintType
└─ flag scan only                               // 只看 BlueprintCallable / BlueprintEvent

UHT FunctionTable
└─ classType == Interface -> ERASE_NO_FUNCTION  // 只看 class type

Tooling outputs
├─ no InterfacePolicy record                    // 没有统一 policy ledger
└─ no shared reason for event/interface lanes   // 没有共享的 interface 解释词汇
```

**当前状态**

- `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:998-1015` 的 class 暴露准入逻辑只看 `BlueprintType` 和函数 flag；interface authoring policy 并没有结构化 owner。
- `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:465-479` 又把 `Interface / NativeInterface` 整类直接降成 `ERASE_NO_FUNCTION()`，同一批方法的 policy 在生成侧被压成了 class-type hardcode。
- 结果是当前工具链最多只能回答“它是不是 interface class”或“它有没有 BlueprintEvent flag”，但回答不了“它属于哪条 interface call lane、为什么当前被 reject/stub、以后应不应该允许 Blueprint 实现”。

[3] 当前工具链只保留了“flag/class type”，没有独立的 interface policy artifact：

```cpp
// ============================================================================
// [3] 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp
// 位置: 998-1015
// 说明: runtime 暴露准入只看 BlueprintType 和函数 flag
// ============================================================================
bool bHasBlueprintCallable = false;
...
UFunction* Function = CheckClass->FindFunctionByName(Elem);
if (Function->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintEvent))
{
	bHasBlueprintCallable = true; // ★ 看到了 event/callable flag，但没有记录 interface policy
	break;
}

// ============================================================================
// [3] 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs
// 位置: 465-479
// 说明: 生成侧又按 class type 一刀切地把 interface class 降成 stub
// ============================================================================
string eraseMacro;
if (classObj.ClassType == UhtClassType.Interface || classObj.ClassType == UhtClassType.NativeInterface)
{
	eraseMacro = "ERASE_NO_FUNCTION()"; // ★ 没有区分 BlueprintNativeEvent、实现方式或未来 lane
}
else if (AngelscriptFunctionSignatureBuilder.TryBuild(classObj, function, out AngelscriptFunctionSignature? signature, out string? _))
{
	eraseMacro = signature!.BuildEraseMacro();
}
entries.Add(new AngelscriptGeneratedFunctionEntry(classObj.SourceName, function.SourceName, eraseMacro));
```

**差距描述**

- `能力缺失`：当前没有一份结构化的 interface authoring policy 资产，无法统一表达 `BlueprintNativeEvent`、`CannotImplementInterfaceInBlueprint`、`Net` 约束与最终选定的 call lane。
- `实现方式不同`：参考方案把这类 policy 作为 metadata/attribute graph 的正式部分，由 generator、runtime、IDE 共享；当前实现则把 policy 分散成 flag 判定和 class-type 特判。
- `实现质量差异`：只要 `P10` 一推进到 native interface event，`Bind API GAP`、IDE 提示和 sidecar 就会继续各自给出不同原因，诊断结果不可执行。

**参考方案**

- **UnrealCSharp**：`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Reflection/FReflectionRegistry.cpp:346-350,398-399,1654-1656` 先把 `BlueprintNativeEvent` 与 `CannotImplementInterfaceInBlueprint` 建成正式 reflection class；`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGeneratorCore.cpp:515-536,1126-1129` 再在 generator 阶段校验这些 policy，并把 interface metadata 单独列入 `GetInterfaceMetaDataAttributes()`。

[4] 参考方案把 interface policy 提前建模成可复用 metadata，而不是事后拼 flag：

```cpp
// ============================================================================
// [4] 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Reflection/FReflectionRegistry.cpp
// 位置: 346-350, 398-399, 1654-1656
// 说明: 先把 BlueprintNativeEvent / CannotImplementInterfaceInBlueprint 注册成正式 metadata class
// ============================================================================
BlueprintNativeEventAttributeClass = GetClass(
	COMBINE_NAMESPACE(NAMESPACE_ROOT, NAMESPACE_DYNAMIC), CLASS_BLUEPRINT_NATIVE_EVENT_ATTRIBUTE);
...
CannotImplementInterfaceInBlueprintAttributeClass = GetClass(
	COMBINE_NAMESPACE(NAMESPACE_ROOT, NAMESPACE_DYNAMIC), CLASS_CANNOT_IMPLEMENT_INTERFACE_IN_BLUEPRINT_ATTRIBUTE);
...
FClassReflection* FReflectionRegistry::GetCannotImplementInterfaceInBlueprintAttributeClass() const
{
	return CannotImplementInterfaceInBlueprintAttributeClass; // ★ metadata 有稳定 owner
}

// ============================================================================
// [4] 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGeneratorCore.cpp
// 位置: 515-536, 1126-1129
// 说明: generator 阶段直接消费这些 metadata，提前做 authoring 校验
// ============================================================================
if (InReflection->HasAttribute(FReflectionRegistry::Get().GetBlueprintNativeEventAttributeClass()))
{
	if (InFunction->FunctionFlags & FUNC_Net)
	{
		UE_LOG(LogUnrealCSharp, Error, TEXT("BlueprintNativeEvent functions cannot be replicated!"));
	}
	...
	InFunction->FunctionFlags |= FUNC_Event;
	InFunction->FunctionFlags |= FUNC_BlueprintEvent; // ★ metadata -> function policy 是显式步骤
}

static TArray<FClassReflection*> InterfaceMetaDataAttributes = {
	ReflectionRegistry.GetConversionRootAttributeClass(),
	ReflectionRegistry.GetCannotImplementInterfaceInBlueprintAttributeClass(),
	ReflectionRegistry.GetToolTipAttributeClass()
}; // ★ interface policy 是独立 metadata 集
```

**吸收建议**

- 在 `AngelscriptUHTTool` 与 runtime 之间新增一份极小的 `InterfacePolicyDesc` / `InterfaceEligibilityRecord`，至少包含：
  - `FunctionKey`
  - `IsBlueprintEvent`
  - `IsBlueprintNativeEvent`
  - `CanImplementInterfaceInBlueprint`
  - `NetPolicy`
  - `ChosenLane`
  - `FailureReason`
- `Bind_BlueprintType.cpp`、`AS_FunctionTable_*` sidecar、`DebugDatabase` 和后续的 `Bind API GAP` 统计都消费这同一份结构化 policy，不再让 runtime 看 flag、UHT 看 class type、测试看 helper。
- 首批只覆盖 native interface methods 即可，不要试图把全部 `UFUNCTION` policy 一次性重做；这条最直接服务 `P10`，也最能让后续 `Bind API GAP` 报告从“现象表”升级成“策略账本”。

**优先级**

- `P1`
- **理由**：这不是 `P10` 的第一锤执行 owner，但它决定 `P10` 落地后能否被 sidecar、IDE 和文档稳定描述。按照当前主线，应在 canonical lane 定义之后立刻补上，而不是等 interface 能力散到更多 family 再补。

### [D9] 测试基础设施：自动化还没有 lane-aware vocabulary

```
[Current] Automation Truths
ReflectiveFallbackEligibilityTest
└─ interface function => RejectedInterfaceClass        // 保护拒绝语义

NativeInterfaceTests
├─ EnsureNativeInterfaceFixturesBound()                // 先手工补绑
├─ script Cast<UInterface>().GetNativeValue()          // 保护 helper 可调用语义
└─ C++ Execute_ bridge works                           // 保护 Execute_ 语义

Result
└─ one feature, two incompatible pass conditions       // 没有 lane 标签
```

**当前状态**

- `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptBlueprintCallableReflectiveFallbackTests.cpp:230-245` 明确把 interface-class function 的 reflective fallback 拒绝写成回归正确值。
- `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceNativeTests.cpp:47-77,203-211,339-342,427-429` 又在同一仓库里把“手工补绑后脚本可以调 native interface 方法、C++ `Execute_` 也能 round-trip”写成另一组回归正确值。
- 这两组测试分别保护不同 lane，但名字和断言词汇都没有说明自己属于哪条 lane；对后续实现者来说，它们看起来像是在保护同一个 feature。

[5] 当前自动化同时锁定了两种互斥 truth，但没有 lane 标签：

```cpp
// ============================================================================
// [5] 文件: Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptBlueprintCallableReflectiveFallbackTests.cpp
// 位置: 230-245
// 说明: 这组测试保护“interface owner 必须拒绝 reflective fallback”
// ============================================================================
const UFunction* InterfaceFunction = UGameplayTagAssetInterface::StaticClass()->FindFunctionByName(TEXT("HasMatchingGameplayTag"));
...
TestEqual(
	TEXT("Reflective fallback should reject interface-class functions explicitly"),
	EvaluateReflectiveFallbackEligibility(InterfaceFunction),
	EAngelscriptReflectiveFallbackEligibility::RejectedInterfaceClass);

// ============================================================================
// [5] 文件: Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceNativeTests.cpp
// 位置: 47-77, 203-211, 339-342, 427-429
// 说明: 另一组测试保护“手工补绑后的 helper lane + Execute_ lane 可以正常工作”
// ============================================================================
EnsureNativeInterfaceBoundForTests(UAngelscriptNativeParentInterface::StaticClass());
EnsureNativeInterfaceBoundForTests(UAngelscriptNativeChildInterface::StaticClass());

TestEqual(TEXT("C++ Execute_ bridge should call the script implementation of GetNativeValue"),
	IAngelscriptNativeParentInterface::Execute_GetNativeValue(Actor), 123);
...
TestEqual(TEXT("C++ Execute_ should dispatch child interface method on child implementation"),
	IAngelscriptNativeChildInterface::Execute_GetChildValue(Actor), 11);
...
IAngelscriptNativeParentInterface::Execute_AdjustNativeValue(Actor, 7, CppAdjustedValue);
TestEqual(TEXT("C++ Execute_ bridge should round-trip ref parameters through the script implementation"), CppAdjustedValue, 27);
```

**差距描述**

- `实现质量差异`：自动化没有 lane-aware vocabulary，导致“拒绝 reflective fallback”与“允许 helper/Execute_ 调用”看起来像在保护同一条能力。
- `能力缺失`：没有一条 production-path regression 去验证“在不调用 `EnsureNativeInterfaceFixturesBound()` 的前提下，选定的 canonical lane 是否真的成立”。
- `实现方式不同`：参考插件更倾向于用单条产品路径回归守住 interface 行为，例如 `Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Tests/Issue595Test.cpp:33-41` 直接验证 interface call 与 object call，两者共享同一份 contract。

**参考方案**

- **UnLua**：`Issue595` regression 直接运行 `UE.UIssue595Interface.Test(Obj)` 与 `Obj:Test()`，没有额外的 test-only rebind helper，也没有另一组测试同时把同类接口函数锁成“必须拒绝”。这类 product-path regression 更接近当前 `P10` 需要的信号质量。

**吸收建议**

- 把现有 native interface 自动化按 lane 重命名和拆分，至少区分：
  - `Interface.HelperLane`
  - `Interface.ExecuteLane`
  - `Interface.ReflectiveRejectedLane`
  - 未来的 `Interface.CanonicalProductionLane`
- 在 `P0` 落 lane 之前，新增一条**不调用** `EnsureNativeInterfaceFixturesBound()` 的 expected-fail regression，明确记录“当前 production lane 尚未闭环”；等 canonical lane 真落地后再翻正。
- 让 generated artifact 测试也消费同一 vocabulary，例如断言某个 interface method 当前是 `RejectedInterfaceClass`、`ExecuteBridgeOnly` 或 `WrapperMixedIn`，而不是只有 `Direct/Stub` 二元值。

**优先级**

- `P1`
- **理由**：测试命名和 vocabulary 不会直接补功能，但它决定 `P10` 期间每次接口相关改动到底是在“按设计迁移 lane”，还是“无意打破另一条隐藏路径”。这属于当前主线推进前必须先校准的验证面。

### 值得吸收的设计模式（增量）

- `Call lane must be first-class`：interface 方法不能只记录“是否支持”，还要记录“由哪条 lane 负责执行”，否则 helper、fallback、wrapper、`Execute_` 很快就会各写一套真相。
- `Authoring policy should be structured metadata`：像 `BlueprintNativeEvent`、`CannotImplementInterfaceInBlueprint` 这种规则不应等到 runtime/UHT 末端再从 flag 猜回去，应该在 generator/registry 阶段就成为正式 artifact。
- `Tests should name the lane they protect`：拒绝路径和支持路径可以同时存在，但前提是测试名称与报表字段明确标出 lane；否则 CI 只会持续放大语义歧义。

### 改进路线建议（增量）

1. `P0`：先定义 `InterfaceCallLane`，并让 native interface event 统一收口到单一 production owner；建议优先走 `ExecuteBridge` 或“实现类/wrapper 混入”路线，不再继续扩张 test-only helper lane。
2. `P1`：在 `AngelscriptUHTTool`、runtime 和 debug artifact 间引入最小 `InterfacePolicyDesc`，把 `BlueprintNativeEvent`、`CannotImplementInterfaceInBlueprint`、`NetPolicy`、`ChosenLane`、`FailureReason` 串成同一份账本。
3. `P1`：重构 interface 自动化词汇，把 `HelperLane / ExecuteLane / ReflectiveRejectedLane / CanonicalProductionLane` 明确分栏；随后让 `Bind API GAP` 报表和 generated artifact 测试也复用这套 lane vocabulary。

---

## 深化分析 (2026-04-09 01:20:23)

### 本轮新增关键发现

- `D2`：当前 C++/script interface 支持的问题已经不只是“C++ UInterface 没有自动注册/自动方法注册”。更深一层的缺口是 `FScriptInterface` 实例桥接 owner 缺位。`Bind_UObject.cpp` 目前只有 class-flag 查询与 `UObject*` 回写；`AngelscriptClassGenerator.cpp` 也只是继续沿用 `PointerOffset=0` 的 K2/Blueprint interface 约定，还没有一条显式维护 `object slot + interface slot` 的 value bridge。这与 `Documents/AutoPlans/ArchitectureReview/TypeSystem_ArchReview.md` 中 `Arch-TS-44` / `Arch-TS-06` 的判断形成了更直接的源码闭环。
- `D7`：`BlueprintImpact` 虽然已经把 `Classes` 纳入 `FBlueprintImpactSymbols`，但 `MatchesPinType()` 仍只识别 `PC_Struct / PC_Enum / PC_Byte`。这意味着 interface/class pin、variable 与 wildcard pin 的受影响面不会被扫描出来。对于正在推进的 `P10 UInterface` 与 `Bind API GAP` 主线，这会造成“运行时能力变了，但 editor impact scan 没有报警”的盲区。
- `D10`：当前 onboarding 资产仍停在 plan/doc 层，没有 editor-native 的脚本创建入口。`Script/Examples/README.md` 明确把真实交付示例继续留在 `Documents/Plans/Plan_ScriptExamplesExpansion/Coverage/`；`AngelscriptEditorModule` 的可见 create flow 也仍只有 `Create Blueprint / Create Asset`。用户现在看不到“Create Script Template”或“Generate Workspace Stubs”这类正式入口。

### 差距矩阵（增量）

| 维度 | 差距等级 | 新增结论 | 最佳参考 | 优先级 |
| --- | --- | --- | --- | --- |
| `D2` 反射绑定机制 | `能力缺失` | 缺少 `FScriptInterface` dual-slot value bridge owner，interface value 仍停在 class-flag/query lane | `UnLua` `FInterfacePropertyDesc` | `P0` |
| `D7` 编辑器集成 | `能力缺失` | `BlueprintImpact` 不识别 interface/class pin family，impact scan 对 `P10` 相关改动有盲区 | `UnrealCSharp` interface reinstance + Blueprint toolbar | `P2` |
| `D10` 文档与示例组织 | `能力缺失` | 没有 editor-native script template / workspace stub 入口，公开示例仍停在 companion plan | `UnLua` toolbar template + IntelliSense generator | `P2` |

### [D2] 反射绑定机制：`FScriptInterface` dual-slot value bridge 仍缺位

```
[Current] Native Interface Value Flow
script cast / query
├─ UObject::ImplementsInterface()                  // 只回答 class 是否实现
├─ UObject::opCast() -> UObject*                   // 只把 object 指针写回脚本侧
├─ ClassGenerator bIsInterface -> PointerOffset=0  // 继续沿用 K2/Blueprint interface 约定
└─ no explicit FScriptInterface bridge             // 没有 owner 维护 object/interface 双槽

[Reference] UnLua Interface Property Flow
FInterfacePropertyDesc::SetValueInternal()
├─ GetUObject() -> UObject* Value                  // 先取对象
├─ SetObject(Value)                                // 写 object 槽
├─ SetInterface(Value->GetInterfaceAddress(...))   // ★ 写 interface-address 槽
└─ CheckPropertyType()                             // 入参阶段就守住 interface 合法性
```

**当前状态**

- `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp:100-105` 的 `ImplementsInterface()` 只是 `Object->GetClass()->ImplementsInterface(InterfaceClass)` 查询，没有 interface value owner。
- 同文件 `135-214` 的 `opCast()` 虽然对 `CLASS_Interface` 做了 `ImplementsInterface()` 判定，但成功时仍只把 `UObject*` 写回 `OutAddress`，没有填充 `FScriptInterface` 的 `interface slot`。
- `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3359-3364` 对 script interface 的核心说明仍是“不设 `CLASS_Native`，这样 `GetInterfaceAddress()` 会返回 this”，这本质上仍是 K2/Blueprint interface pattern，而不是独立的 interface instance bridge。

[6] 当前实现只覆盖 class 查询与 object pointer cast，没有 dual-slot bridge：

```cpp
// ============================================================================
// [6] 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp
// 位置: 100-105, 188-214
// 说明: interface 查询与 cast 仍以 class flag / UObject* 为中心
// ============================================================================
UObject_.Method("bool ImplementsInterface(const UClass InterfaceClass) const",
	[](UObject* Object, UClass* InterfaceClass)
	{
		if (Object == nullptr || InterfaceClass == nullptr)
			return false;
		return Object->GetClass()->ImplementsInterface(InterfaceClass); // ★ 只回答 class query
	});

const bool bAssociatedClassIsInterface = AssociatedClass->HasAnyClassFlags(CLASS_Interface);
const bool bImplementsInterface = bAssociatedClassIsInterface && Object->GetClass()->ImplementsInterface(AssociatedClass);
...
if (bIsA)
{
	*(UObject**)OutAddress = Object;
}
else if (bImplementsInterface)
{
	*(UObject**)OutAddress = Object; // ★ interface cast 成功时仍只回写 UObject*
}
else
{
	*(UObject**)OutAddress = nullptr;
}

// ============================================================================
// [6] 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 位置: 3359-3364
// 说明: script interface 仍依赖 PointerOffset=0 的 K2/Blueprint 约定
// ============================================================================
if (ClassDesc->bIsInterface)
{
	NewClass->ClassFlags |= CLASS_Interface | CLASS_Abstract;
	// Do NOT set CLASS_Native — this makes GetInterfaceAddress() return this (PointerOffset=0)
	// which is the Blueprint/Script interface pattern
}
```

**差距描述**

- `能力缺失`：当前没有显式的 `FScriptInterface` value bridge，无法统一管理 `object slot` 与 `interface slot`，因此 interface 参数、返回值、属性与容器值都没有稳定 owner。
- `实现方式不同`：现实现主要靠 `CLASS_Interface`、`PointerOffset=0` 和 `ImplementsInterface()` 查询拼出“像 interface”的行为；最佳参考则把 interface 当成正式 property family，用独立 adapter 负责双槽写入。
- `实现质量差异`：现有 helper/query lane 足以支撑 cast probe 与少量测试夹具，但不足以支撑 `Plan_CppInterfaceBinding.md:96-119` 的自动方法注册主线。一旦进入 `P2.1/P2.2` 的 native interface 方法和继承链收口，缺少实例桥接 owner 会继续把 callable、property 与 UHT sidecar 三边拆开。

**参考方案**

- **UnLua**：`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:533-580` 直接把 `FInterfaceProperty` 建成 `FInterfacePropertyDesc`，在 `SetValueInternal()` 中同时写 `SetObject()` 与 `SetInterface()`，并在 `CheckPropertyType()` 阶段校验 `ImplementsInterface()`。它解决的不是“脚本能不能判断 implements”，而是“interface value 由谁持有完整 representation”。

[7] 参考实现把 interface value 当成正式 property family，双槽由 adapter 明确拥有：

```cpp
// ============================================================================
// [7] 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp
// 位置: 533-580
// 说明: interface property adapter 同时维护 object slot 与 interface slot
// ============================================================================
class FInterfacePropertyDesc : public FPropertyDesc
{
public:
	virtual bool SetValueInternal(lua_State *L, void *ValuePtr, int32 IndexInStack, bool bCopyValue) const override
	{
		FScriptInterface *Interface = (FScriptInterface*)ValuePtr;
		UObject *Value = UnLua::GetUObject(L, IndexInStack);
		Interface->SetObject(Value); // ★ 写 object 槽
		Interface->SetInterface(Value ? Value->GetInterfaceAddress(InterfaceProperty->InterfaceClass) : nullptr); // ★ 写 interface-address 槽
		return true;
	}

	virtual bool CheckPropertyType(lua_State* L, int32 IndexInStack, FString& ErrorMsg, void* UserData)
	{
		UObject* Object = UnLua::GetUObject(L, IndexInStack);
		if (Object)
		{
			UClass* Class = Object->GetClass();
			if ((Class) && (!Class->ImplementsInterface(InterfaceProperty->InterfaceClass)))
			{
				ErrorMsg = FString::Printf(TEXT("implements of interface %s is needed but got nil for object %s"),
					*InterfaceProperty->InterfaceClass->GetName(), *Class->GetName());
				return false; // ★ 非法 interface 值在入参阶段就被拒绝
			}
		}

		return true;
	};
};
```

**吸收建议**

- 先不要把 `UInterface` 全家桶一次性铺开，而是补一个最小 `InterfaceValueBridge`，首批只服务 `native interface parameter / return / property` 三类路径。
- bridge owner 建议落在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/` 或 `ClassGenerator/`，直接复用 UE 现成原语：`FScriptInterface::SetObject()`、`SetInterface()`、`UObject::GetInterfaceAddress()`；不要引入引擎改动，也不要发明第二套 interface 内存布局。
- `Bind_UObject.cpp` 的 cast/query 继续保留为“轻量探测层”，但不要再把它当成未来 `FInterfaceProperty`、`TScriptInterface<>`、container/interface value 的正式 owner。
- 与 `Bind API GAP` 报表协调时，建议把这条 bridge 产物同步暴露到 sidecar/test 词汇里，例如新增 `ValueBridge=ObjectOnly / DualSlotReady`，避免后面只看“类型已注册”就误判接口值语义已闭环。

**优先级**

- `P0`
- **理由**：这是 `P10 UInterface` 当前最底层的真实闸门，优先级应高于 editor workflow 和 onboarding。没有 dual-slot bridge，`Plan_CppInterfaceBinding.md:96-119` 的方法注册就算补出来，也仍然只能落在 class/query lane 上，无法自然扩展到参数、返回值、属性与容器。

### [D7] 编辑器集成：`BlueprintImpact` 对 interface/class pin family 仍是盲区

```
[Current] BlueprintImpact Scan
ChangedScripts
└─ BuildImpactSymbols()
   ├─ Classes / Structs / Enums / Delegates        // 符号集已经包含 class
   └─ AnalyzeLoadedBlueprint()
      ├─ ParentClass / NodeDependency -> Classes   // 这里会看 class
      ├─ PinType / VariableType -> MatchesPinType()
      │   └─ PC_Struct / PC_Enum / PC_Byte only    // interface/class pin 全漏
      └─ Result: interface/class pin changes are silent

[Reference] UnrealCSharp Interface Refresh Loop
Interface changed
└─ FDynamicInterfaceGenerator::ReInstance()
   ├─ find BPGC implementing interface             // 找出所有受影响 Blueprint
   ├─ RefreshAllNodes()                            // 刷新节点
   ├─ MarkBlueprintAsModified()                    // 标记修改
   └─ CompileBlueprint()                           // 直接走编译闭环
```

**当前状态**

- `Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h:23-29` 的 `FBlueprintImpactSymbols` 已经包含 `Classes`，说明设计上并不排斥 class family。
- 但 `Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp:44-56` 的 `MatchesPinType()` 目前只识别 `PC_Struct / PC_Enum / PC_Byte`。
- `AnalyzeLoadedBlueprint()` 在 `187-235` 对 node pins、editable pins、macro wildcard pin 与 `Blueprint.NewVariables` 都复用了这条函数，因此 interface/class pin family 会整体失踪。
- `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactScannerTests.cpp:351-490` 现有自动化覆盖了 struct variable、struct pin、delegate signature 与 node dependency，但没有 interface/class pin regression。

[8] 当前 scanner 已有 `Classes` 概念，但 pin/variable family 仍只认 struct/enum/byte：

```cpp
// ============================================================================
// [8] 文件: Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h
// 位置: 23-29
// 说明: 符号集已经收了 Classes，但后续 pin family 没有完整消费
// ============================================================================
struct FBlueprintImpactSymbols
{
	TSet<UClass*> Classes;
	TSet<UScriptStruct*> Structs;
	TSet<UEnum*> Enums;
	TSet<UDelegateFunction*> Delegates;
	TMap<UObject*, UObject*> ReplacementObjects;
};

// ============================================================================
// [8] 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp
// 位置: 44-56, 187-235
// 说明: pin/variable 检测统一依赖 MatchesPinType()，但这里只识别三类 pin category
// ============================================================================
bool MatchesPinType(const FEdGraphPinType& PinType, const FBlueprintImpactSymbols& Symbols)
{
	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
	{
		return Symbols.Structs.Contains(Cast<UScriptStruct>(PinType.PinSubCategoryObject.Get()));
	}

	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Enum || PinType.PinCategory == UEdGraphSchema_K2::PC_Byte)
	{
		return Symbols.Enums.Contains(Cast<UEnum>(PinType.PinSubCategoryObject.Get()));
	}

	return false; // ★ PC_Object / PC_Class / PC_Interface 当前不会命中
}

for (UEdGraphPin* Pin : Node->Pins)
{
	if (MatchesPinType(Pin->PinType, Symbols))
	{
		AddUniqueReason(OutReasons, EBlueprintImpactReason::PinType);
		break;
	}
}

for (const FBPVariableDescription& Variable : Blueprint.NewVariables)
{
	if (MatchesPinType(Variable.VarType, Symbols))
	{
		AddUniqueReason(OutReasons, EBlueprintImpactReason::VariableType);
		break;
	}
}

// ============================================================================
// [8] 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactScannerTests.cpp
// 位置: 351-402, 453-490
// 说明: 当前自动化只锁 struct/delegate，不锁 interface/class pin family
// ============================================================================
Variable.VarType.PinCategory = UEdGraphSchema_K2::PC_Struct;
Variable.VarType.PinSubCategoryObject = TBaseStructure<FVector>::Get();
...
Symbols.Structs.Add(TBaseStructure<FVector>::Get());
...
return TestTrue(TEXT("BlueprintImpact.AnalyzeVariableType should record the variable-type reason"),
	Reasons.Contains(EBlueprintImpactReason::VariableType));

UK2Node_CallFunction* CallFunctionNode = AddCallFunctionNode(*Blueprint, UKismetMathLibrary::StaticClass()->FindFunctionByName(TEXT("MakeVector")));
...
Symbols.Structs.Add(TBaseStructure<FVector>::Get());
...
return TestTrue(TEXT("BlueprintImpact.AnalyzePinType should record the pin-type reason"),
	Reasons.Contains(EBlueprintImpactReason::PinType));

Symbols.Delegates.Add(SignatureFunction);
...
return TestTrue(TEXT("BlueprintImpact.AnalyzeDelegateSignature should record the delegate-signature reason"),
	Reasons.Contains(EBlueprintImpactReason::DelegateSignature));
```

**差距描述**

- `能力缺失`：当前 impact scan 对 `PC_Object / PC_Class / PC_Interface` 没有检测能力，直接缺失一整条与 `P10 UInterface` 和 Bind API GAP 最相关的 editor safety net。
- `实现方式不同`：当前实现按“今天已有测试的 pin category”写死；最佳参考更接近“interface 变更 -> 找受影响 Blueprint -> refresh/compile”的 family-aware 闭环。
- `实现质量差异`：`FBlueprintImpactSymbols` 已经包含 `Classes`，但 pin/variable family 没有把它消费完，这会让后续实现者误以为 scanner 已经覆盖 class/interface 影响面。

**参考方案**

- **UnrealCSharp**：`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicInterfaceGenerator.cpp:271-309` 会枚举所有 `ImplementsInterface(InClass)` 的非 native `UBlueprintGeneratedClass`，然后统一执行 `RefreshAllNodes()`、`MarkBlueprintAsModified()` 与 `CompileBlueprint()`。
- **UnrealCSharp**：`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp:57-95,157-205` 在 Blueprint editor 上下文直接提供 `Open File / Code Analysis / Override Blueprint` 这类 task-oriented surface，把“发现影响”与“执行后续动作”放在同一个 editor 入口里。

[9] 参考方案不是只做符号比对，而是把 interface change 收口成可执行的 Blueprint refresh/compile 闭环：

```cpp
// ============================================================================
// [9] 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicInterfaceGenerator.cpp
// 位置: 271-309
// 说明: interface 变化后，直接找出所有实现该 interface 的 Blueprint 并刷新编译
// ============================================================================
void FDynamicInterfaceGenerator::ReInstance(UClass* InClass)
{
	TArray<UBlueprintGeneratedClass*> BlueprintGeneratedClasses;

	FDynamicGeneratorCore::IteratorObject<UBlueprintGeneratedClass>(
		[InClass](const TObjectIterator<UBlueprintGeneratedClass>& InBlueprintGeneratedClass)
		{
			if (InBlueprintGeneratedClass->IsNative())
			{
				return false;
			}

			return InBlueprintGeneratedClass->ImplementsInterface(InClass); // ★ 先确定受影响 Blueprint 集
		},
		[&BlueprintGeneratedClasses](const TObjectIterator<UBlueprintGeneratedClass>& InBlueprintGeneratedClass)
		{
			BlueprintGeneratedClasses.AddUnique(*InBlueprintGeneratedClass);
		});

	for (const auto BlueprintGeneratedClass : BlueprintGeneratedClasses)
	{
		if (const auto Blueprint = Cast<UBlueprint>(BlueprintGeneratedClass->ClassGeneratedBy))
		{
			Blueprint->Modify();
			FBlueprintEditorUtils::RefreshAllNodes(Blueprint);            // ★ 刷节点
			FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);    // ★ 标脏
			FKismetEditorUtilities::CompileBlueprint(Blueprint,
				EBlueprintCompileOptions::SkipGarbageCollection | EBlueprintCompileOptions::SkipSave); // ★ 编译闭环
		}
	}
}

// ============================================================================
// [9] 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpBlueprintToolBar.cpp
// 位置: 57-95, 157-205
// 说明: Blueprint 上下文直接给出与动态脚本相关的执行入口
// ============================================================================
CommandList->MapAction(FUnrealCSharpEditorCommands::Get().OpenFile, ...);
CommandList->MapAction(FUnrealCSharpEditorCommands::Get().CodeAnalysis, ...);
CommandList->MapAction(FUnrealCSharpEditorCommands::Get().OverrideBlueprint, ...);
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
```

**吸收建议**

- 先把 `MatchesPinType()` 从“硬编码三类”扩到 family-aware 最小闭环：首批覆盖 `PC_Object`、`PC_Class`、`PC_Interface`，并让它们消费 `Symbols.Classes`。
- 对 `AnalyzeLoadedBlueprint()` 的四个入口统一补齐：普通 node pins、`UK2Node_EditablePinBase::UserDefinedPins`、`UK2Node_MacroInstance::ResolvedWildcardType`、`Blueprint.NewVariables`。不要只补其中一个入口，否则问题会从 variable 漏到 macro pin，或者从普通 pin 漏到 user-defined pin。
- 自动化至少新增 3 条回归：`interface variable`、`interface/class pin`、`interface/class wildcard pin`。这样 `P10` 推进时可以第一时间暴露哪些 Blueprint 需要 refresh/recompile。
- 如果后续确实要把 impact scan 变成日常 workflow，可以在 `AngelscriptEditor` 里补一个小型 toolbar/commandlet surface，把“扫描受影响 Blueprint”和“批量 refresh/recompile”收在同一入口里；这个入口可借鉴 `UnrealCSharpBlueprintToolBar.cpp` 的 task-oriented 组织方式，但不必一次性复制其全部 editor action。

**优先级**

- `P2`
- **理由**：它不是 `P10` 的首个 runtime blocker，但它直接决定 `P10 UInterface` 和 Bind API GAP 补齐后的 editor 回归面是否可见。按照 `Documents/Plans/Plan_StatusPriorityRoadmap.md:151-174`，这类“工作流入口/可见性/上手安全网”应排在基线能力收口之后、长期演进之前，属于典型 `P2`。

### [D10] 文档与示例组织：onboarding 仍停在 plan/doc 层，没有 editor-native 创建入口

```
[Current] Onboarding Surface
Script/Examples/README
├─ points to Documents/Plans/.../Coverage          // 真实示例仍在 companion plan
└─ no promoted public examples yet                 // 还没有正式公开入口

AngelscriptEditor create flow
└─ ShowCreateBlueprintPopup()
   ├─ Create Blueprint                             // 只建 Blueprint
   └─ Create Asset                                 // 或 DataAsset

Missing Surface
├─ Create Script Template                          // 缺脚本骨架入口
└─ Generate Workspace Stubs                        // 缺 IDE/workspace stub 入口

[Reference] UnLua Onboarding Surface
Blueprint Toolbar
├─ Create Lua Template                             // 从 Blueprint 直接产出脚本模板
└─ Bind / Reveal commands                          // 把路径和动作放在 editor 里

IntelliSense Generator
├─ OutputDir = Plugin/Intermediate/IntelliSense    // 统一输出目录
├─ AssetRegistry hooks                             // 资产变化自动触发
└─ UpdateAll() / SaveFile()                        // 持续生成 workspace stubs
```

**当前状态**

- `Script/Examples/README.md:1-21` 明确写着当前首波 Coverage 示例仍驻留 `Documents/Plans/Plan_ScriptExamplesExpansion/Coverage/`，`Script/Examples/` 只保留长期入口说明。
- `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:405-516` 当前 editor 可见 create flow 只围绕 `ShowCreateBlueprintPopup()` 展开，最终只会创建 `Blueprint` 或 `DataAsset`。
- 这意味着当前仓库虽然已经有脚本运行时、debug server 和 source navigation 底座，但对新用户最需要的两个入口仍缺位：`Create Script Template` 与 `Generate Workspace Stubs`。

[10] 当前公开入口仍然把示例与脚本工作流停留在 plan/doc 层：

```cpp
// ============================================================================
// [10] 文件: Script/Examples/README.md
// 位置: 1-21
// 说明: Script/Examples 当前只是说明牌，真实示例仍在 Plan 目录
// ============================================================================
# Script 示例目录

本目录保留为后续正式公开的 Script 示例入口。
...
- 当前首波需要交付的 Coverage `.as` 资产先放在 `Documents/Plans/Plan_ScriptExamplesExpansion/Coverage/`。
- 这里暂时只保留长期入口说明，等这批交付资产稳定后，再决定是否同步提升为 `Script/Examples/` 正式示例。

// ============================================================================
// [10] 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 位置: 405-516
// 说明: editor create flow 当前只覆盖 Blueprint / DataAsset
// ============================================================================
FAngelscriptRuntimeModule::GetEditorCreateBlueprint().AddLambda(
	[](UASClass* ScriptClass)
	{
		FAngelscriptEditorModule::ShowCreateBlueprintPopup(ScriptClass);
	}
);
...
if (bIsDataAsset)
{
	Asset = NewObject<UDataAsset>(Package, Class, AssetName, RF_Public | RF_Transactional | RF_Standalone);
}
else
{
	Asset = FKismetEditorUtilities::CreateBlueprint(
		Class, Package, AssetName, BPTYPE_Normal,
		BlueprintClass, BlueprintGeneratedClass, FName("AngelscriptCreateBlueprint")
	); // ★ 当前正式入口只会落到 Asset/Blueprint
}
```

**差距描述**

- `能力缺失`：当前没有 editor-native 的脚本模板生成和 workspace stub 生成入口，onboarding 仍需要用户跳到 plan 文档和手工目录组织。
- `实现方式不同`：当前仓库把脚本示例与 IDE 入口当成“后续整理资产”；参考方案则把模板创建、路径揭示、stub 生成当成 editor workflow 的正式组成部分。
- `实现质量差异`：`Script/Examples` 虽然已经有目录约束，但还不是对外可消费资产；这会让“插件已经具备 runtime/debug/test 能力”的事实无法转换成低门槛上手体验。

**参考方案**

- **UnLua**：`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:55-72,257-313` 在 Blueprint toolbar 中直接暴露 `Create Lua Template`，根据模块名定位模板、写出 `.lua` 文件。
- **UnLua**：`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:42-106,222-244` 建立独立的 `OutputDir`，挂接 `AssetRegistry` 的 `OnAssetAdded/Removed/Renamed/Updated`，并通过 `UpdateAll()` / `SaveFile()` 增量生成 IntelliSense/stub 资产。

[11] 参考实现把“模板创建 + workspace stubs”都做成 editor 原生工作流，而不是停留在文档说明：

```cpp
// ============================================================================
// [11] 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp
// 位置: 55-72, 257-313
// 说明: Blueprint 上下文直接提供 Create Lua Template
// ============================================================================
MenuBuilder.AddMenuEntry(Commands.CreateLuaTemplate, NAME_None, LOCTEXT("CreateLuaTemplate", "Create Lua Template"));
...
void FUnLuaEditorToolbar::CreateLuaTemplate_Executed()
{
	const auto Blueprint = Cast<UBlueprint>(ContextObject);
	...
	const auto RelativePath = ModuleName.Replace(TEXT("."), TEXT("/"));
	const auto FileName = FString::Printf(TEXT("%s%s.lua"), *GLuaSrcFullPath, *RelativePath);
	...
	FFileHelper::LoadFileToString(Content, *FullFilePath);
	Content = Content.Replace(TEXT("TemplateName"), *TemplateName)
	                 .Replace(TEXT("ClassName"), *UnLua::IntelliSense::GetTypeName(Class));
	FFileHelper::SaveStringToFile(Content, *FileName, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM); // ★ editor 内直接落模板文件
}

// ============================================================================
// [11] 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp
// 位置: 42-106, 222-244
// 说明: IntelliSense/stub 由独立生成器持续维护
// ============================================================================
void FUnLuaIntelliSenseGenerator::Initialize()
{
	OutputDir = IPluginManager::Get().FindPlugin("UnLua")->GetBaseDir() + "/Intermediate/IntelliSense";
	AssetRegistryModule.Get().OnAssetAdded().AddRaw(this, &FUnLuaIntelliSenseGenerator::OnAssetAdded);
	AssetRegistryModule.Get().OnAssetRemoved().AddRaw(this, &FUnLuaIntelliSenseGenerator::OnAssetRemoved);
	AssetRegistryModule.Get().OnAssetRenamed().AddRaw(this, &FUnLuaIntelliSenseGenerator::OnAssetRenamed);
	AssetRegistryModule.Get().OnAssetUpdated().AddRaw(this, &FUnLuaIntelliSenseGenerator::OnAssetUpdated); // ★ 资产变化自动驱动
}

void FUnLuaIntelliSenseGenerator::UpdateAll()
{
	...
	ExportUE(NativeTypes);
	ExportUnLua(); // ★ 全量刷新 stub/workspace 资产
}

void FUnLuaIntelliSenseGenerator::SaveFile(const FString& ModuleName, const FString& FileName, const FString& GeneratedFileContent)
{
	const FString Directory = OutputDir / ModuleName;
	const FString FilePath = FString::Printf(TEXT("%s/%s.lua"), *Directory, *FileName);
	...
	if (FileContent != GeneratedFileContent)
		FFileHelper::SaveStringToFile(GeneratedFileContent, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
```

**吸收建议**

- 在 `AngelscriptEditor` 里补两个正式入口，而不是继续把示例/工作流停在 Plan 目录：
  - `Create Script Template`：从 `UASClass` 或绑定到 `Blueprint` 的上下文出发，按父类/模板族生成最小 `.as` 骨架，并在 editor 中直接打开文件。
  - `Generate Workspace Stubs`：以 plugin/project `Intermediate/AngelscriptWorkspace` 为输出目录，生成 IDE 友好的最小 stub 资产；先服务 `Source Navigation`、API discoverability 与 workspace bootstrap，不必一开始就追求完整语言服务器。
- 首批模板与 stub 生成都应复用现有插件约束：`2.33 + selective 2.38`、不修改引擎、优先走 plugin 内 `AssetRegistry` / editor module 事件，而不是新增引擎 patch。
- 当前 `Documents/Plans/Plan_ScriptExamplesExpansion/Coverage/` 中已经存在的稳定 Coverage 示例，在模板/工作流入口成型后应按主题逐步提升到 `Script/Examples/`，并挂接 README/guide，而不是长期停留在 companion plan。
- 如果 scope 需要进一步收缩，建议先落 `Generate Workspace Stubs` 的只读产物，再补 `Create Script Template`；但两者最终都应进入正式 editor surface，而不是继续散落在文档或一次性脚本中。

**优先级**

- `P2`
- **理由**：根据 `Documents/Plans/Plan_StatusPriorityRoadmap.md:151-174`，onboarding 资产和 workflow entry points 本来就处在“delivery baseline 之后、长期 parity 之前”的第二阶段。它不应抢在 `P10 UInterface` bridge owner 前面，但也不应继续被推迟到 `P4` 长期项。

### 值得吸收的设计模式（增量）

- `Value bridge owns the full representation`：interface 不是“能 cast 就算支持”，而是必须有明确 owner 维护 `object slot + interface slot`；否则 callable、property、container 会各走一条临时旁路。
- `Impact scan should be family-aware, not testcase-aware`：既然 `FBlueprintImpactSymbols` 已经有 `Classes`，scanner 就不应只盯住现有测试覆盖到的 `struct/enum/byte` 三类 pin category；应该按 type family 建立统一匹配规则。
- `Onboarding entrypoints should be editor-native`：示例、模板和 workspace stubs 一旦长期对外承诺，就不应继续托管在 plan/doc 目录；最小可发现入口应该直接出现在 editor toolbar、commandlet 或 settings 中。

### 改进路线建议（增量）

1. `P0`：先补 `FScriptInterface` 最小 dual-slot bridge，并为 `native interface parameter / return / property` 增加一条 end-to-end regression，确认 interface value 不再只是 `UObject*` query/cast 语义。
2. `P2`：在第一条 production interface lane 稳定后，立即把 `BlueprintImpact` 扩到 `PC_Object / PC_Class / PC_Interface`，补齐 interface/class pin regression，再决定是否加批量 refresh/recompile surface。
3. `P2`：把 `Create Script Template` 和 `Generate Workspace Stubs` 收成 `AngelscriptEditor` 的正式入口，同时逐步把已经稳定的 Coverage 示例从 `Documents/Plans/Plan_ScriptExamplesExpansion/Coverage/` 提升为 `Script/Examples/` 对外资产。

---

## 深化分析 (2026-04-09 06:37:59)

> 本轮不重复前文已经铺开的 `D4/D10` 结论，只收口与当前主线最直接相关的三条链：`P10 UInterface` 的 runtime owner、`Bind API GAP` 的可观测 contract、以及 editor/test safety net 为什么还接不住这条主线。

### [维度 D2/D6] `UInterface` 的真正阻塞点不是少一条 cast API，而是 parse / materialize / bind / UHT 四段 owner 分裂

**当前状态**

当前仓库对 `UInterface` 的支持已经跨了四段，但四段彼此没有共享同一份 family owner：

```
[Current] Interface Owner Split
Script source
├─ Preprocessor                               // 先把 interface 固定到 UInterface super
├─ ClassGenerator                             // 生成 metadata-only UClass，再晚期补实现链
├─ Runtime Type/Binds                         // object/class family 可用，interface value family 缺席
├─ ReflectiveFallback                         // owning class 是 interface 时直接拒绝
├─ UHT FunctionTable                          // Interface/NativeInterface 一律写 stub
└─ Native tests                               // 仍靠手工 ReferenceClass + GenericMethod 补夹具
```

- `Preprocessor` 会把脚本 `interface` 默认映射到 `UInterface`，但这只是 superclass owner，不是 type/property owner。
- `ClassGenerator` 会为 interface 生成 metadata-only `UClass`，并在实现类 finalize 时按名字回查 interface；这说明 interface 仍然是 materialization 特例，不是统一 type family。
- `Bind_BlueprintType` / `FAngelscriptTypeUsage::FromProperty()` 这条主链只稳定覆盖 `FObjectProperty` / `FClassProperty` / 容器等既有 family；本轮在 `Plugins/Angelscript/Source/AngelscriptRuntime/` 下对 `FInterfaceProperty|TScriptInterface` 做检索，没有定位到 runtime 生产代码 owner。
- `ReflectiveFallback` 继续把 interface owning class 判成拒绝项，因此 callable 也没有沿现有 generic bridge 进入主链。
- `AngelscriptInterfaceNativeTests.cpp` 仍要手工 `ReferenceClass + GenericMethod` 才能测试 native interface，说明 production path 还没有接手。

[1] 解析期与 class materialization 先把 interface 固定成 `UInterface` / metadata shell，但还没进入 type family：

```cpp
// ============================================================================
// [1] 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp
// 位置: 788-792, 2909-2913
// 说明: interface 在解析期先被归到 UInterface superclass
// ============================================================================
if (ClassDesc->bIsInterface)
{
	// ★ 这里只是把脚本 interface 固定到 code superclass
	ClassDesc->SuperClass = TEXT("UInterface");
}

if (ClassDesc->bIsInterface && ClassDesc->SuperClass == TEXT("UInterface"))
{
	ClassDesc->bSuperIsCodeClass = true;
	ClassDesc->CodeSuperClass = UInterface::StaticClass(); // ★ owner 仍是 superclass，不是 property family
	return;
}

// ============================================================================
// [1] 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 位置: 2762-2798, 5059-5108
// 说明: interface class 走 metadata-only reload path，实现类再晚期补接口解析
// ============================================================================
else if (ClassData.NewClass->bIsInterface)
{
	// ★ interface 不走普通 AS property / function materialization
	UClass* SuperClass = InterfaceDesc->CodeSuperClass;
	if (SuperClass == nullptr)
		SuperClass = UInterface::StaticClass();

	NewClass->SetSuperStruct(SuperClass);
	NewClass->ClassFlags |= CLASS_Interface | CLASS_Abstract;
}

auto InterfaceType = FAngelscriptType::GetByAngelscriptTypeName(InterfaceName);
if (InterfaceType.IsValid())
{
	UClass* Found = InterfaceType->GetClass(FAngelscriptTypeUsage::DefaultUsage);
	if (Found != nullptr)
		return Found;
}

for (TObjectIterator<UClass> It; It; ++It)
{
	if (It->GetName() == UnrealInterfaceName && It->HasAnyClassFlags(CLASS_Interface))
		return *It; // ★ 仍保留名字 + 全局类搜索的 late fallback
}
```

[2] runtime bind 主链仍然只拥有 object/class family；native interface 测试要靠手工夹具补洞：

```cpp
// ============================================================================
// [2] 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp
// 位置: 122-139, 149-162, 2440-2486
// 说明: 当前生产代码只稳定创建/识别 FClassProperty 与 FObjectProperty
// ============================================================================
FProperty* CreateProperty(const FAngelscriptTypeUsage& Usage, const FPropertyParams& Params) const override
{
	if (Class == UClass::StaticClass())
	{
		auto* Property = new FClassProperty(Params.Outer, Params.PropertyName, RF_Public);
		Property->PropertyClass = Class;
		Property->MetaClass = UObject::StaticClass();
		return Property;
	}

	auto* Property = new FObjectProperty(Params.Outer, Params.PropertyName, RF_Public); // ★ 其余对象一律落到 FObjectProperty
	Property->PropertyClass = Class != nullptr ? Class : (UClass*)Usage.ScriptClass->GetUserData();
	return Property;
}

bool MatchesProperty(const FAngelscriptTypeUsage& Usage, const FProperty* Property, EPropertyMatchType MatchType) const override
{
	const FObjectProperty* ObjectProp = CastField<FObjectProperty>(Property); // ★ 没有 FInterfaceProperty 分支
	if (ObjectProp == nullptr)
		return false;
	...
}

FAngelscriptType::RegisterTypeFinder([=](FProperty* Property, FAngelscriptTypeUsage& Usage) -> bool
{
	const FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property);
	...
	const FClassProperty* ClassProperty = CastField<FClassProperty>(Property);
	...
	return false; // ★ TypeFinder 只认 object/class wrapper
});

// ============================================================================
// [2] 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp
// 位置: 261-282
// 说明: interface callable 仍被现有 reflective path 明确拒绝
// ============================================================================
const UClass* OwningClass = Function->GetOuterUClass();
if (OwningClass->HasAnyClassFlags(CLASS_Interface))
{
	return EAngelscriptReflectiveFallbackEligibility::RejectedInterfaceClass; // ★ callable owner 也没并入主链
}

// ============================================================================
// [2] 文件: Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceNativeTests.cpp
// 位置: 41-77
// 说明: Native interface 目前仍靠测试侧手工注册 ReferenceClass + GenericMethod
// ============================================================================
void BindNativeInterfaceMethod(FAngelscriptBinds& Binds, const TCHAR* Declaration, const TCHAR* FunctionName)
{
	FInterfaceMethodSignature* Signature = FAngelscriptEngine::Get().RegisterInterfaceMethodSignature(FName(FunctionName));
	Binds.GenericMethod(FString(Declaration), TestCallInterfaceMethod, Signature);
}

void EnsureNativeInterfaceBoundForTests(UClass* InterfaceClass)
{
	FAngelscriptBinds Binds = FAngelscriptBinds::ReferenceClass(TypeName, InterfaceClass); // ★ 测试夹具自己补 production 缺口
	...
	BindNativeInterfaceMethod(Binds, TEXT("int GetNativeValue() const"), TEXT("GetNativeValue"));
}
```

**差距描述**

- `没有实现`：`FInterfaceProperty` / `TScriptInterface<>` 在 runtime type/property pipeline 里没有 production owner；当前只有 `ImplementsInterface` / `opCast` 和测试手工绑定能工作。
- `实现方式不同`：当前把 interface 语义拆进 `Preprocessor`、`ClassGenerator`、`Bind_UObject`、`UHT` 四段；参考实现会把 interface 当成和 object/container/delegate 同级的 family，再让 generator/wrapper/translator 消费它。
- `实现质量差异`：当前实现类补接口时仍保留 `GetByAngelscriptTypeName()` + `TObjectIterator<UClass>` fallback，稳定性和可审计性都弱于 descriptor-first 路线。

**参考方案**

- `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:533-560,1592-1618`
  - `FInterfacePropertyDesc` 直接用 `FScriptInterface` 作为值载体；factory 在 `CPT_Interface` 分支把 interface 视为普通 property family。
- `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:598-630,1308-1310`
  - `FInterfacePropertyTranslator` 与 array/map/set translator 同级注册，先把 `UEToJs/JsToUE` 的 value bridge 打通。
- `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:144-166`
  - `GetPropertyType()` 直接把 `FInterfaceProperty` 生成为 `TScriptInterface<...>`。
- `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FClassGenerator.cpp:94-126,257-280`
  - 生成期显式并入 `InClass->Interfaces` 与接口函数，而不是让 interface 留在 late-bound 特例里。

[3] 参考实现把 interface 先做成 property/value family，再让生成器与 wrapper 消费：

```cpp
// ============================================================================
// [3] 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp
// 位置: 541-560, 1592-1618
// 说明: interface 先成为 property family，再进入值读写与 factory
// ============================================================================
virtual void GetValueInternal(lua_State *L, const void *ValuePtr, bool bCreateCopy) const override
{
	const FScriptInterface &Interface = InterfaceProperty->GetPropertyValue(ValuePtr);
	UnLua::PushUObject(L, Interface.GetObject()); // ★ value bridge owner 是 FScriptInterface
}

virtual bool SetValueInternal(lua_State *L, void *ValuePtr, int32 IndexInStack, bool bCopyValue) const override
{
	FScriptInterface *Interface = (FScriptInterface*)ValuePtr;
	UObject *Value = UnLua::GetUObject(L, IndexInStack);
	Interface->SetObject(Value);
	Interface->SetInterface(Value ? Value->GetInterfaceAddress(InterfaceProperty->InterfaceClass) : nullptr);
	return true;
}

case CPT_Interface:
{
	PropertyDesc = new FInterfacePropertyDesc(InProperty); // ★ interface 与 array/map/set 同级进入 factory
	break;
}
```

**吸收建议**

- 第一阶段不要修改 Unreal Engine，也不要要求 AngelScript 2.33 runtime 先升级；在插件内新增独立 interface binder/adapter 即可。
- 以 `FScriptInterface` 为唯一 value owner，先补 `FInterfaceProperty` / `TScriptInterface<>` 的 `CreateProperty`、`MatchesProperty`、`SetArgument`、`GetReturnValue`、`GetCppForm`，把 interface 拉进现有 `FAngelscriptTypeUsage::FromProperty()` 主链。
- `Bind_UObject::ImplementsInterface` 与 `opCast` 保留为兼容 fallback，但不再承担“唯一 interface 语义入口”。
- `AngelscriptInterfaceNativeTests.cpp` 的 `EnsureNativeInterfaceBoundForTests()` 应在 production path 打通后退化为 legacy regression，而不是继续充当 happy-path 夹具。

**优先级**

- `P0`
- **理由**：`Documents/Plans/Plan_CppInterfaceBinding.md:21-24` 已把“C++ UInterface 未自动注册 / FInterfaceProperty 未参与绑定”定义成当前主线缺口；`Documents/AutoPlans/ArchitectureReview/TypeSystem_ArchReview.md:189-219` 又明确指出这不是单点 bind 漏洞，而是 type/property/callable owner 缺席。

### [维度 D6/D9] 当前 `Bind API GAP` 账本仍是 function-centric，看不见 “哪种 type family 还没有 owner”

**当前状态**

当前仓库已经有很强的 UHT sidecar 和自动化，但这套账本仍然只围绕 function entry 建立：

```
[Current] Function-Centric Gap Ledger
UHT scan
├─ CollectEntries(UhtClass -> UhtFunction)         // 只产出函数 entry
├─ AS_FunctionTable_*.cpp                          // runtime registration shard
├─ Summary.json / ModuleSummary.csv               // 统计 direct/stub 比例
├─ Entries.csv / SkippedEntries.csv               // 逐函数明细与 failure reason
└─ GeneratedFunctionTableTests                     // 校验计数、表头、代表类 coverage

Blind Spot
├─ X no TypeCatalog / SignatureManifest            // 没有 type family 资产
├─ X no runtime-owner map for interface/container  // 不知道哪类 family 没 owner
└─ X no test gate for interface family coverage    // interface 整族 stub 也可能通过现有测试
```

- `CollectEntries()` 只扫描 `UhtFunction`，遇到 `Interface` / `NativeInterface` 直接写 `ERASE_NO_FUNCTION()`。
- `GeneratedFunctionTable` 自动化只校验代表类是否有函数表、summary/json/csv 计数是否对齐、CSV 表头是否稳定；它不回答 “interface family 在 runtime 是否已有 owner”。
- 现有产物里已经能直接看到 interface 整族是 stub，例如：
  - `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv:883`
  - `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv:1524`
  - `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv:2783`
  - `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv:2957`
  - 这些行分别对应 `UCameraLensEffectInterface`、`UInterface_AssetUserData`、`ULevelInstanceInterface`、`UNavMovementInterface`，全部仍是 `Stub,ERASE_NO_FUNCTION()`。

[4] 生成器与现有自动化都围绕 function entry / csv 计数，而不是 type family：

```csharp
// ============================================================================
// [4] 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs
// 位置: 449-479
// 说明: sidecar 的最小记录单位仍然是 function entry
// ============================================================================
private static void CollectEntries(IUhtExportFactory factory, UhtType type, SortedSet<string> includes, List<AngelscriptGeneratedFunctionEntry> entries)
{
	if (type is UhtClass classObj)
	{
		foreach (UhtType child in classObj.Children)
		{
			if (child is UhtFunction function && ShouldGenerate(classObj, function))
			{
				string eraseMacro;
				if (classObj.ClassType == UhtClassType.Interface || classObj.ClassType == UhtClassType.NativeInterface)
				{
					eraseMacro = "ERASE_NO_FUNCTION()"; // ★ interface 在生成阶段整体压成 stub
				}
				else if (AngelscriptFunctionSignatureBuilder.TryBuild(classObj, function, out AngelscriptFunctionSignature? signature, out string? _))
				{
					eraseMacro = signature!.BuildEraseMacro();
				}
				else
				{
					eraseMacro = "ERASE_NO_FUNCTION()";
				}

				entries.Add(new AngelscriptGeneratedFunctionEntry(classObj.SourceName, function.SourceName, eraseMacro));
			}
		}
	}
}
```

```cpp
// ============================================================================
// [4] 文件: Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp
// 位置: 278-296, 480-481, 645, 683
// 说明: 现有 gate 校验代表类、总数和 CSV 表头，但没有 type-family 视角
// ============================================================================
struct FRepresentativeClassExpectation
{
	const TCHAR* ObjectPath;
	const TCHAR* DisplayName;
};

const FRepresentativeClassExpectation Expectations[] =
{
	{ TEXT("/Script/Engine.Actor"), TEXT("AActor") },
	{ TEXT("/Script/Engine.World"), TEXT("UWorld") },
	{ TEXT("/Script/Engine.PlayerController"), TEXT("APlayerController") },
	...
}; // ★ 代表类 coverage 里没有 interface family

int32 TotalGeneratedEntries = 0;
SummaryObject->TryGetNumberField(TEXT("totalGeneratedEntries"), TotalGeneratedEntries); // ★ 主 gate 仍是 entry 计数

TestEqual(TEXT("Generated function table csv test should write the expected entry csv header"),
	EntryLines[0], TEXT("ModuleName,EditorOnly,ClassName,FunctionName,EntryKind,EraseMacro,ShardIndex"));

TestEqual(TEXT("Generated function table skipped csv test should write the expected skipped csv header"),
	SkippedLines[0], TEXT("ModuleName,ClassName,FunctionName,FailureReason")); // ★ 没有任何 type-family 列
```

**差距描述**

- `没有实现`：当前没有 `TypeCatalog` / `SignatureManifest` 这类 type-family 资产，无法系统回答“哪个 family 缺 runtime owner、哪个 interface 只是暂时 stub”。
- `实现方式不同`：当前产物服务的是 `AddFunctionEntry()` 和 direct/stub 统计；参考实现把 `.d.ts`、stub 树或 property descriptor 当成正式 authoring/runtime 资产。
- `实现质量差异`：现有自动化可以证明 “函数表生成稳定”，但不能证明 “interface/container/wrapper family 已闭环”；因此 `Bind API GAP` 会天然偏向手工盘点。

**参考方案**

- `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:360-457,1311-1327`
  - `GenTypeScriptDeclaration()` 把 `ue.d.ts / ue_bp.d.ts` 作为正式产物落盘，并显式遍历 `Class->Interfaces` 把接口函数并入声明图。
- `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:42-106,222-244`
  - IntelliSense generator 以 `AssetRegistry` 事件维持 `Intermediate/IntelliSense` stub 树；作者侧 artifact 是稳定、可刷新、可落盘的。
- `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:144-166`
  - property graph 直接进入 generator，而不是先被压平成 `ClassName + FunctionName + EraseMacro`。

[5] 参考实现把 interface/type family 直接导出为 authoring artifact，而不是只给 function table 留痕：

```cpp
// ============================================================================
// [5] 文件: Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp
// 位置: 360-457, 1311-1327
// 说明: d.ts 是正式产物，interface method 直接进入同一份声明图
// ============================================================================
void FTypeScriptDeclarationGenerator::GenTypeScriptDeclaration(bool InGenStruct, bool InGenEnum)
{
	...
	FFileHelper::SaveStringToFile(ToString(), *UEDeclarationFilePath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM); // ★ `ue.d.ts` 是正式输出，不是临时日志
	...
	FFileHelper::SaveStringToFile(ToString(), *BPDeclarationFilePath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM); // ★ `ue_bp.d.ts` 同样是正式输出
}

for (int i = 0; i < Class->Interfaces.Num(); i++)
{
	for (TFieldIterator<UFunction> FunctionIt(Class->Interfaces[i].Class, EFieldIteratorFlags::IncludeSuper); FunctionIt; ++FunctionIt)
	{
		if (!GenFunction(TmpBuff, *FunctionIt))
			continue;

		TryToAddOverload(Outputs, FunctionIt->GetName(),
			(FunctionIt->FunctionFlags & FUNC_Static) != 0, TmpBuff.Buffer); // ★ interface 函数直接并入 authoring surface
	}
}
```

**吸收建议**

- 保留现有 `AS_FunctionTable_*.cpp` / `Summary.json` / `Entries.csv`，但新增一份只读 `AS_TypeCatalog.json` 或 `AS_SignatureManifest.json`。
- 首版 manifest 不参与 runtime 决策，只回答四个问题：`Kind`、`PropertyFamilies`、`DispatchKind`、`FailureReason`。
- `Bind API GAP` 的自动化从 “函数数对不对” 扩成 “catalog 中出现的 `Interface/Array/Map/Set/Optional` family，runtime 是否能找到 owner”。
- 后续 `Docs / DebugDatabase / Workspace stubs` 都应复用这份 manifest，而不是继续各自产生自己的半结构化真相。

**优先级**

- `P0`
- **理由**：`Documents/AutoPlans/ArchitectureReview/TypeSystem_ArchReview.md:570-595,714-744` 已把“缺少 type catalog / 结构化 type metadata”定义成 P10 的工具链主阻塞；在 `Bind API GAP` 主线里，这不是 nice-to-have，而是减少重复人工盘点的前置资产。

### [维度 D7/D9] editor / regression 安全网仍盯函数和 struct，看不见 interface/class pin family

**当前状态**

`BlueprintImpact` 和当前 regression 的问题，不是“完全没有 editor safety net”，而是它们还没盯住和 `P10` 同一条 family model：

```
[Current] P10 Safety Net
Interface/Class change
├─ GeneratedFunctionTableTests                 // 看 entry 数、CSV、代表类
├─ NativeInterfaceTests                        // 仍有手工绑定 happy path
└─ BlueprintImpact
   ├─ NodeDependency uses Symbols.Classes      // class 级外部依赖可见
   ├─ Pin/Variable/Macro wildcard              // 只识别 Struct / Enum / Byte
   └─ No interface/class pin regressions       // interface pin 变化不会冒泡到 editor gate
```

- `FBlueprintImpactSymbols` 已经有 `Classes` 集合，但 `MatchesPinType()` 只识别 `PC_Struct / PC_Enum / PC_Byte`。
- `AnalyzeLoadedBlueprint()` 已经在普通 pins、`UserDefinedPins`、`ResolvedWildcardType`、`Blueprint.NewVariables` 四个入口统一调用 `MatchesPinType()`；这反而说明只要 family model补对，scanner 覆盖面本身足够大。
- 现有自动化只锁住 struct variable/pin、node dependency 和 delegate signature，没有 interface/class pin regression。
- native interface regression 里仍有测试手工绑定路径，因此即使 production owner 漏了，部分快速回归也可能不第一时间红。

[6] scanner 已有 `Classes`，但 pin family 只认 struct/enum/byte：

```cpp
// ============================================================================
// [6] 文件: Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h
// 位置: 23-31
// 说明: Symbols 已经持有 Classes，但后续 pin family 没完整消费
// ============================================================================
struct FBlueprintImpactSymbols
{
	TSet<UClass*> Classes;
	TSet<UScriptStruct*> Structs;
	TSet<UEnum*> Enums;
	TSet<UDelegateFunction*> Delegates;
	TMap<UObject*, UObject*> ReplacementObjects;
};

// ============================================================================
// [6] 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp
// 位置: 44-56, 187-230
// 说明: pin/variable family 统一汇到 MatchesPinType()，但这里只识别三类
// ============================================================================
bool MatchesPinType(const FEdGraphPinType& PinType, const FBlueprintImpactSymbols& Symbols)
{
	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
		return Symbols.Structs.Contains(Cast<UScriptStruct>(PinType.PinSubCategoryObject.Get()));

	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Enum || PinType.PinCategory == UEdGraphSchema_K2::PC_Byte)
		return Symbols.Enums.Contains(Cast<UEnum>(PinType.PinSubCategoryObject.Get()));

	return false; // ★ `PC_Object / PC_Class / PC_Interface` 当前不会命中
}

for (UEdGraphPin* Pin : Node->Pins)
{
	if (MatchesPinType(Pin->PinType, Symbols))
		AddUniqueReason(OutReasons, EBlueprintImpactReason::PinType);
}

for (const FBPVariableDescription& Variable : Blueprint.NewVariables)
{
	if (MatchesPinType(Variable.VarType, Symbols))
		AddUniqueReason(OutReasons, EBlueprintImpactReason::VariableType);
}
```

[7] 现有自动化只锁 struct/delegate，不锁 interface/class pin family：

```cpp
// ============================================================================
// [7] 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactScannerTests.cpp
// 位置: 351-402, 453-490
// 说明: 当前回归面仍以 struct variable/pin 和 delegate signature 为主
// ============================================================================
FBPVariableDescription Variable;
Variable.VarType.PinCategory = UEdGraphSchema_K2::PC_Struct;
Variable.VarType.PinSubCategoryObject = TBaseStructure<FVector>::Get();
Symbols.Structs.Add(TBaseStructure<FVector>::Get()); // ★ variable regression 只测 struct

UK2Node_CallFunction* CallFunctionNode = AddCallFunctionNode(*Blueprint, UKismetMathLibrary::StaticClass()->FindFunctionByName(TEXT("MakeVector")));
Symbols.Structs.Add(TBaseStructure<FVector>::Get()); // ★ pin regression 也只测 struct

Symbols.Delegates.Add(SignatureFunction); // ★ 另一条已有 regression 是 delegate
```

**差距描述**

- `没有实现`：`PC_Object / PC_Class / PC_Interface` 的 pin/variable/wildcard impact 检测和对应 regression 还没有落地。
- `实现方式不同`：当前 editor safety net 更像“函数表 + node dependency + struct pin”三段式；最佳参考会把 interface 变化直接收口成 impacted Blueprint refresh/recompile。
- `实现质量差异`：当前 scanner 已经有足够大的调用面，但 family model 选得太窄；这会让 interface/class 改动继续变成“只有实现者自己知道”的隐性风险。

**参考方案**

- `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicInterfaceGenerator.cpp:271-310`
  - `ReInstance()` 会枚举所有 `ImplementsInterface(InClass)` 的 `UBlueprintGeneratedClass`，然后统一执行 `RefreshAllNodes()`、`MarkBlueprintAsModified()`、`CompileBlueprint()`。
- `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:42-106`
  - 编辑器侧用 `AssetRegistry` 事件持续刷新 authoring artifact，证明“变化后立刻刷新可见层”应该是正式 contract，而不是一次性人工操作。

[8] 参考实现把 interface 变化直接闭环到 Blueprint refresh/recompile：

```cpp
// ============================================================================
// [8] 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicInterfaceGenerator.cpp
// 位置: 271-310
// 说明: interface 变化后，先找受影响 Blueprint，再统一刷新与编译
// ============================================================================
void FDynamicInterfaceGenerator::ReInstance(UClass* InClass)
{
	TArray<UBlueprintGeneratedClass*> BlueprintGeneratedClasses;

	FDynamicGeneratorCore::IteratorObject<UBlueprintGeneratedClass>(
		[InClass](const TObjectIterator<UBlueprintGeneratedClass>& InBlueprintGeneratedClass)
		{
			if (InBlueprintGeneratedClass->IsNative())
				return false;

			return InBlueprintGeneratedClass->ImplementsInterface(InClass); // ★ 先确定受影响蓝图集合
		},
		[&BlueprintGeneratedClasses](const TObjectIterator<UBlueprintGeneratedClass>& InBlueprintGeneratedClass)
		{
			BlueprintGeneratedClasses.AddUnique(*InBlueprintGeneratedClass);
		});

	for (const auto BlueprintGeneratedClass : BlueprintGeneratedClasses)
	{
		if (const auto Blueprint = Cast<UBlueprint>(BlueprintGeneratedClass->ClassGeneratedBy))
		{
			Blueprint->Modify();
			FBlueprintEditorUtils::RefreshAllNodes(Blueprint);         // ★ 刷节点
			FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint); // ★ 标脏
			FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection | EBlueprintCompileOptions::SkipSave); // ★ 重新编译
		}
	}
}
```

**吸收建议**

- 在 `MatchesPinType()` 里补 `PC_Object / PC_Class / PC_Interface`，并让它消费 `Symbols.Classes`；如果后续确实需要区分 object/class/interface，再考虑把 `Classes` 分裂成更细 family，而不是现在就再造一套平行 symbol bag。
- 自动化至少新增三条：`interface variable`、`class/interface pin`、`class/interface wildcard pin`；让 `P10` 改动第一次红在 editor/test，而不是红在用户 Blueprint。
- `GeneratedFunctionTable` 的代表类 coverage 至少补一个 interface family 样本，避免 function gate 永远只盯住普通 UObject 类。
- 如果 scope 允许，在 interface/class 变更落稳后，加一条可选的 `refresh/recompile impacted blueprints` commandlet / editor action，参考 UnrealCSharp 的 `ReInstance()`，但不要求当前就复制其完整 generated-class 流程。

**优先级**

- `P1`
- **理由**：如果把它看成通用 editor workflow，它仍可放在 `P2`；但如果限定到当前 `P10 UInterface / Bind API GAP` 主线，`Plan_CppInterfaceBinding.md:34,121-140` 已把 regression baseline 当成正式交付物，因此 `PC_Interface/PC_Class` safety net 至少应前置为 `P1` 子闸门。

### 值得吸收的设计模式（本轮补充）

- `Type family first, function entry second`：先确定 `Interface/Object/Class/Container/Delegate` 的 family owner，再生成函数表；不要再让 interface 以“函数表 stub + class flag + 手工测试夹具”三种半 owner 并存。
- `Read-only manifest before runtime takeover`：先补只读 `TypeCatalog/SignatureManifest`，用它把 gap 看清楚，再逐步让 runtime / docs / debug database 复用；这比一上来就替换现有 function table 风险低得多。
- `Safety net must watch the same family model as the bridge`：runtime 想支持 `FInterfaceProperty`，editor/test 就必须同时看见 `PC_Interface` 和 interface family；否则 capability 和 gate 会继续错位。
- `Manual happy-path fixtures are a smell`：当测试必须先 `ReferenceClass + GenericMethod` 才能证明能力存在时，通常说明 production owner 还没有真正落地。

### 改进路线建议（本轮收口）

1. `P0`：先在插件内补 `FScriptInterface` / `FInterfaceProperty` 的最小 production owner，把 native `UInterface` 自动注册、参数/返回值/property roundtrip 和 callable owner 打通；不改引擎，不要求 AS runtime 先升级。
2. `P0`：并行补一份只读 `AS_TypeCatalog.json` 或 `AS_SignatureManifest.json`，让 `Bind API GAP` 从“人工扫 CSV + 记忆”升级为“可比较的 type-family 账本”。
3. `P1`：把 `BlueprintImpact` 和 regression 扩到 `PC_Object / PC_Class / PC_Interface`，再补 interface/class pin 与 wildcard tests，让 editor/test 与 runtime owner 对齐。
4. `P2`：等 `P10` 最小闭环稳定后，再把 `Docs / DebugDatabase / Workspace stubs` 逐步改成消费 manifest 的统一 contract；这一步是收口，不是前置条件。

---
## 深化分析 (2026-04-09 06:50:02)

### 本轮新增关键发现

- 本轮不重复前文已经确认的 `BindModules.Cache` save/load authority 分裂；新增补充是更底层的 schema 问题：当前 artifact 仍只是 `TArray<FString>`，producer、consumer、observer 没有可 join 的 phase/scope/source 字段。
- 本轮不重复前文已经指出的 `PC_Interface` pin 盲区；新增补充是更结构性的 implementer 盲区：`BlueprintImpact` 的 reason model 里根本没有“Blueprint 实现了已变更 interface”这一类，导致 implementer-only Blueprint 即使不走 pin，也不会被 editor/commandlet 命中。
- 当前 test net 的主要缺口，不再是“有没有测试”，而是“测试是不是直接钉住 interface 主线的真实回归”。现有用例大多按 subsystem reason 或 happy-path query 组织，缺一条“接口变更后 editor/runtime/commandlet 同时报警”的 issue-scoped regression。

### 差距矩阵（增量）

| 维度 | 当前主证据 | 差距等级 | 本轮新增结论 | 优先级 |
| --- | --- | --- | --- | --- |
| `D1` 插件架构与模块划分 | `AngelscriptBinds.h` + `AngelscriptEditorModule.cpp` + `AngelscriptEngine.cpp` | `实现差异` | bind module artifact 仍是 raw string list，不是带 phase/scope/source 的 manifest | `P1` |
| `D7` 编辑器集成 | `AngelscriptBlueprintImpactScanner.*` + `ClassReloadHelper.cpp` + `BlueprintImpactScanCommandlet.cpp` | `能力缺失` | editor 仍没有 “implemented interface changed” 的命中理由与汇总口径 | `P1` |
| `D9` 测试基础设施 | `AngelscriptBlueprintImpactScannerTests.cpp` + `AngelscriptBlueprintImpactTests.cpp` + `AngelscriptInterfaceImplementTests.cpp` | `实现差异` | regression 仍按 reason/happy-path 切片，缺 issue-scoped interface 闭环测试 | `P1` |

### [D1] 插件架构与模块划分：`BindModules.Cache` 仍是 raw list，不是可 join 的 artifact contract

**当前状态**

前文已经记录过 `BindModules.Cache` 的 save/load authority 不一致；本轮新增补充是：即使先忽略路径分裂，artifact 自身也仍然只有“模块名字符串数组”这一个维度，运行时读到后只能 `LoadModule()`，无法回答“这个模块来自哪里、是否 editor-only、由哪次生成产生、是否属于宿主项目/引擎/外部扩展”。

```
[Current] Generated Bind Module Artifact
Editor GenerateNativeBinds
├─ FAngelscriptBinds::GetBindModuleNames()          // 内存里只有名字列表
├─ SaveBindModules(Path)
│  └─ SaveStringArrayToFile(...)                    // 落盘仍是裸字符串数组
└─ No schema fields                                 // 无 source/scope/generated-at

Runtime Initialize
├─ LoadBindModules(Path)
│  └─ LoadFileToStringArray(...)                    // 读回后仍只知道模块名
└─ LoadModule(ModuleName)                           // 无法判断 editor/runtime/来源
```

- `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:583-601` 已直接写着 `//TO-DO make sure the binds are written to base directory not inside another module`，同时 `SaveBindModules()` / `LoadBindModules()` 都只是 `FFileHelper::{Save,Load}StringArrayToFile()`。
- `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1077` 仍只把模块名列表写到 `GetScriptRootDirectory() / "BindModules.Cache"`。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:781-793,1473-1487` 运行时从 script root 读取 `Binds.Cache`，但 bind module 名单则改从 plugin base dir 读取；再加上 raw list schema，artifact 无法成为 phase contract。

[46] 当前 artifact 只有字符串数组，没有任何 phase/source 元数据：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h
// 位置: 583-601
// 说明: bind module 持久化仍是原始字符串数组
// ============================================================================
//TO-DO make sure the binds are written to base directory not inside another module
static void SaveBindModules(FString Path)
{
	auto& BindModuleNames = GetBindModuleNames();
	FFileHelper::SaveStringArrayToFile(BindModuleNames, *Path); // ★ 只有模块名，没有 scope/source/generated-at
}

static void LoadBindModules(FString Path)
{
	auto& BindModuleNames = GetBindModuleNames();
	FFileHelper::LoadFileToStringArray(BindModuleNames, *Path); // ★ 读回后只能继续按字符串逐个 LoadModule
}
```

**差距描述**

- `没有实现`：当前没有正式的 `BindModuleManifest` 或等价 schema，artifact 不能表达 `SourceProjectRoot`、`Producer`、`Scope(Runtime/Editor)`、`GeneratedAt`、`bEditorOnly`。
- `实现方式不同`：当前是 “editor 写字符串名单，runtime 读字符串名单”；参考实现是 “producer 与 consumer 围绕同一个结构化 manifest 工作”。
- `实现质量差异`：现有 raw list 让 state dump、CI、debug、delivery 无法判断某个 bind module 为什么存在、该不该在当前 phase 加载，也无法稳定扩到外部 provider。

**参考方案**

- `Reference/UnrealCSharp/Source/UnrealCSharpCore/UnrealCSharpCore.build.cs:140-211`
  - build 阶段把 `ProjectModules`、`ProjectPlugins`、`EngineModules`、`EnginePlugins` 写进同一份 `Project/Intermediate/UnrealCSharp_Modules.json`。
- `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp:1265-1324`
  - runtime 读取同一路径同一 schema，不再依赖“写文件的人和读文件的人恰好知道同一个隐式目录”。

[47] 参考实现把 artifact 先产品化成 JSON manifest，再让 runtime/tooling 共用：

```csharp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/UnrealCSharpCore.build.cs
// 位置: 140-211
// 说明: build 阶段把模块来源和类别写入同一份 manifest
// ============================================================================
var Intermediate = Path.Combine(ProjectPath, "Intermediate");
var JsonFullFilename = Path.Combine(Intermediate, "UnrealCSharp_Modules.json");

using var Writer = new JsonWriter(JsonFullFilename);
Writer.WriteObjectStart();
Writer.WriteObjectStart("ProjectModules");
foreach (var Item in Target.ExtraModuleNames)
{
	Writer.WriteValue(Item, Path.Join(Target.ProjectFile.Directory.FullName, "Source", Item)); // ★ 记录模块来源
}
Writer.WriteObjectEnd();

Writer.WriteObjectStart("ProjectPlugins");
foreach (var Item in ProjectPlugins)
{
	Writer.WriteValue(Item.Key, Item.Value); // ★ Project plugin 路径显式写进 manifest
}
Writer.WriteObjectEnd();
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp
// 位置: 1265-1324
// 说明: runtime 从同一 canonical 路径读取同一份 manifest
// ============================================================================
static auto FilePath = FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("UnrealCSharp_Modules.json"));

if (FString JsonStr; FFileHelper::LoadFileToString(JsonStr, *FilePath))
{
	...
	if (const TSharedPtr<FJsonObject>* OutObject; JsonObj->TryGetObjectField(TEXT("ProjectModules"), OutObject))
	{
		for (const auto& [Key, PLACEHOLDER] : OutObject->Get()->Values)
		{
			ProjectModuleList.AddUnique(Key); // ★ consumer 直接复用 producer 写出的分类结果
		}
	}
}
```

**吸收建议**

- 在插件内新增轻量 `FAngelscriptBindModuleManifest`，首版只做 dual-write / dual-read，不替换现有 `BindModules.Cache`。
- manifest 第一阶段最少记录 `ModuleName`、`SourceKind(Project/Plugin/Generated)`、`Scope(Runtime/Editor)`、`ManifestPathVersion`、`GeneratedAtUtc`、`Producer`。
- runtime、state dump、未来 commandlet 统一走 manifest helper；legacy `BindModules.Cache` 只保留兼容 fallback。
- 不把它升级成 `P0`，避免和当前 `P10 UInterface` runtime owner 争抢主线；但也不宜拖到非常靠后，因为 delivery baseline 和外部 bind provider 最终都要落在同一份 artifact contract 上。

**优先级**

- `P1`
- **理由**：它不是当前 `P10` runtime bridge 的直接 blocker，因此不应抢 `P0`；但 `AGENTS.md` 已把 delivery baseline 放在高优先级，而 bind module artifact 恰好是插件外部交付与扩展 provider 的基础合同，应该在 `P10` 最小闭环后立刻收口。

### [D7] 编辑器集成：当前 editor 仍看不见“实现了已变更 interface 的 Blueprint”

**当前状态**

前文已经覆盖过 `PC_Interface / PC_Class` pin family 盲区；本轮新增补充是更深一层的 implementer blind spot。当前 `BlueprintImpact` 的 reason model 只有 `ScriptParentClass / NodeDependency / PinType / VariableType / DelegateSignature / ReferencedAsset` 六种，`AnalyzeLoadedBlueprint()` 也完全没有 `Blueprint->ImplementedInterfaces` 或 `Blueprint->GeneratedClass->ImplementsInterface(...)` 分支。因此哪怕将来 pin family 补齐，那些“只是声明实现了某个 interface，但图里没有显式 interface pin”的 Blueprint 仍然不会命中。

```
[Current] Interface Change In Editor
Changed Interface
├─ BuildImpactSymbols -> Classes/Structs/Enums/Delegates
├─ AnalyzeLoadedBlueprint
│  ├─ ParentClass IsChildOf(ImpactedClass)
│  ├─ NodeDependency / PinType / VariableType
│  ├─ DelegateSignature
│  └─ ReferencedAsset
├─ BlueprintImpact commandlet summary
│  └─ Classes / Structs / Enums / Delegates only
└─ No ImplementedInterface lane
   └─ implementer-only Blueprint stays invisible
```

- `Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h:13-21` 的 `EBlueprintImpactReason` 没有 interface-implementer 相关枚举。
- `Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp:150-245` 的 `AnalyzeLoadedBlueprint()` 只看 parent class、node dependency、pin、delegate、referenced asset。
- `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:108-145` 依赖同一个 `AnalyzeLoadedBlueprint()` 结果决定 `DependencyBPs`，所以 implementer 盲区会直接继承到 reload。
- `Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp:87-100` 输出的 machine-readable summary 也只有 `Classes/Structs/Enums/Delegates`，没有 interface bucket。

[48] 当前 scanner/reload/commandlet 的共享问题，不是扫描器不存在，而是 reason model 里根本没有 implementer lane：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h
// 位置: 13-21
// 说明: 当前 impact reason 没有 “ImplementedInterface” 或等价类别
// ============================================================================
enum class EBlueprintImpactReason : uint8
{
	ScriptParentClass,
	NodeDependency,
	PinType,
	VariableType,
	DelegateSignature,
	ReferencedAsset,
};

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp
// 位置: 158-245
// 说明: AnalyzeLoadedBlueprint 没有消费 Blueprint->ImplementedInterfaces
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

for (UK2Node* Node : AllNodes)
{
	...
	if (MatchesPinType(Pin->PinType, Symbols))
	{
		AddUniqueReason(OutReasons, EBlueprintImpactReason::PinType);
	}
	...
}

// ★ 这里后续只有 VariableType / ReferencedAsset，没有 Blueprint->ImplementedInterfaces 分支
```

**差距描述**

- `没有实现`：当前没有 “changed interface -> affected Blueprint implementers” 的 editor reason，也没有对应 commandlet summary 字段。
- `实现方式不同`：当前 editor impact 更像 “类型/节点/资产引用扫描”；最佳参考会把 interface implementer 当成一等输入集合，再触发 refresh/recompile。
- `实现质量差异`：即使后续补齐 `PC_Interface` pin family，如果 implementer lane 仍不存在，editor 仍会漏掉那些只在 class settings 上声明接口、没有显式 interface pin 的 Blueprint。

**参考方案**

- `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicInterfaceGenerator.cpp:271-310`
  - `ReInstance()` 直接用 `UBlueprintGeneratedClass::ImplementsInterface(InClass)` 找受影响蓝图，然后统一 `RefreshAllNodes()`、`MarkBlueprintAsModified()`、`CompileBlueprint()`。
- `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:148-183`
  - editor 直接操作 `Blueprint->ImplementedInterfaces`，说明 interface set 在编辑器层就是一等数据，不应只靠泛化 pin/type fallback 间接推断。

[49] 参考实现把 implementer set 当成 editor 侧的正式输入，而不是顺带从 pin/type 推断：

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicInterfaceGenerator.cpp
// 位置: 271-310
// 说明: interface 变更后先枚举实现者，再统一刷新 Blueprint
// ============================================================================
void FDynamicInterfaceGenerator::ReInstance(UClass* InClass)
{
	TArray<UBlueprintGeneratedClass*> BlueprintGeneratedClasses;

	FDynamicGeneratorCore::IteratorObject<UBlueprintGeneratedClass>(
		[InClass](const TObjectIterator<UBlueprintGeneratedClass>& InBlueprintGeneratedClass)
		{
			if (InBlueprintGeneratedClass->IsNative())
			{
				return false;
			}

			return InBlueprintGeneratedClass->ImplementsInterface(InClass); // ★ 先按 interface implementer 建集合
		},
		[&BlueprintGeneratedClasses](const TObjectIterator<UBlueprintGeneratedClass>& InBlueprintGeneratedClass)
		{
			BlueprintGeneratedClasses.AddUnique(*InBlueprintGeneratedClass);
		});

	for (const auto BlueprintGeneratedClass : BlueprintGeneratedClasses)
	{
		if (const auto Blueprint = Cast<UBlueprint>(BlueprintGeneratedClass->ClassGeneratedBy))
		{
			Blueprint->Modify();
			FBlueprintEditorUtils::RefreshAllNodes(Blueprint);         // ★ 刷节点
			FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint); // ★ 标脏
			FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection | EBlueprintCompileOptions::SkipSave); // ★ 重新编译
		}
	}
}
```

**吸收建议**

- 给 `FBlueprintImpactSymbols` 增加显式 `Interfaces` 集合，或在保持 `Classes` 集合不拆分的前提下，至少补一条 `ImplementedInterface` reason 与对应 helper。
- `AnalyzeLoadedBlueprint()` 首阶段至少补两条判断：`Blueprint->ImplementedInterfaces` 的显式 interface 描述，以及 `Blueprint->GeneratedClass->ImplementsInterface(ImpactedInterface)` 的运行时 fallback。
- `BlueprintImpactScanCommandlet` 的 summary 增加 `Interfaces` 与 `ImplementedInterfaceMatches` 计数，避免 CI 只能看到 `Classes` 总量增长，却不知道 interface 受影响面是否为零。
- 不新建第二套 reload 扫描器；继续让 `ClassReloadHelper` 复用 `AnalyzeLoadedBlueprint()`，但把 implementer lane 补进同一条 scanner。

**优先级**

- `P1`
- **理由**：这不是 `P10` runtime owner 的第一阻塞点，所以不是 `P0`；但只要 `FInterfaceProperty/TScriptInterface<>` 或 native interface callable 开始进入用户面，implementer-only Blueprint 如果仍然静默漏检，就会把正确性问题推迟到用户资产编译期甚至运行期暴露，因此必须紧跟在 `P10` 最小闭环之后。

### [D9] 测试基础设施：当前 regression 仍按 reason/happy-path 切片，缺少 interface 主线的 issue test

**当前状态**

本轮新增补充不是“现有测试太少”，而是“现有测试还没有把 interface 主线最脆弱的跨阶段合同钉住”。当前 editor 回归大多围绕单个 `BlueprintImpactReason` 建立，runtime interface 回归则主要证明 happy-path `ImplementsInterface()` 成立。两者中间缺少一条明确的 issue-scoped regression，去验证“接口变化后 implementer Blueprint 会被 impact/reload/commandlet 同时看见”。

```
[Current] Interface Regression Shape
Editor scanner tests
├─ AnalyzeParentClass
├─ AnalyzeVariableType / AnalyzePinType
├─ AnalyzeNodeDependency / AnalyzeReferencedAsset
├─ AnalyzeDelegateSignature
└─ CommandletInvalidFile

Scenario tests
├─ ScriptParentMatch
├─ ChangedScriptFilter
└─ DiskBackedAssetScan

Interface runtime tests
└─ ImplementsInterface happy path

Missing
├─ implemented-interface impact scenario
├─ commandlet success-schema interface assertion
└─ checked interface query negative path
```

- `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactScannerTests.cpp:23-74` 注册的 editor scanner tests 仍围绕 `AnalyzeParentClass / AnalyzeVariableType / AnalyzePinType / AnalyzeNodeDependency / AnalyzeReferencedAsset / AnalyzeDelegateSignature / CommandletInvalidFile`。
- 同文件 `:312-490` 的断言也全是这些 reason 的正向命中，没有 implementer-only interface case。
- `Plugins/Angelscript/Source/AngelscriptTest/Blueprint/AngelscriptBlueprintImpactTests.cpp:145-156,159-300` 的 scenario tests 仍只覆盖 `ScriptParentMatch / ChangedScriptFilter / DiskBackedAssetScan`。
- `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceImplementTests.cpp:175-246` 里的 `Interface.ImplementsInterfaceMethod` 仍是标准 happy path：脚本类实现 interface，`this.ImplementsInterface(UIDamageableImplCheck::StaticClass())` 返回 `true`。

[50] 当前 regression 的命名和断言形状，决定了它更擅长守住 reason/happy-path，而不是 issue 闭环：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactScannerTests.cpp
// 位置: 37-74, 312-348
// 说明: editor 测试以 reason 名称切片，commandlet 也只测 invalid-args
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptBlueprintImpactAnalyzeParentClassTest,
	"Angelscript.Editor.BlueprintImpact.AnalyzeParentClass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptBlueprintImpactAnalyzeDelegateSignatureTest,
	"Angelscript.Editor.BlueprintImpact.AnalyzeDelegateSignature",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptBlueprintImpactCommandletInvalidFileTest,
	"Angelscript.Editor.BlueprintImpact.CommandletInvalidFile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

return TestEqual(
	TEXT("BlueprintImpact.CommandletInvalidFile should return the invalid-arguments exit code for a missing ChangedScriptFile"),
	Commandlet->Main(TEXT("ChangedScriptFile=J:/Missing/DoesNotExist.txt")),
	1); // ★ 目前 commandlet regression 只有错误参数，不测成功输出 schema
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceImplementTests.cpp
// 位置: 192-246
// 说明: interface regression 目前主要证明 happy-path query 成立
// ============================================================================
UINTERFACE()
interface UIDamageableImplCheck
{
	void TakeDamage(float Amount);
}

class AScenarioInterfaceImplMethod : AActor, UIDamageableImplCheck
{
	...
	void BeginPlay()
	{
		if (this.ImplementsInterface(UIDamageableImplCheck::StaticClass()))
		{
			ImplementsResult = 1; // ★ 这里只证明合法输入时 query 成立
		}
	}
}

TestTrue(TEXT("ImplementsInterface() should return true for the implementing class"), ScriptClass->ImplementsInterface(InterfaceClass));
TestEqual(TEXT("ImplementsInterface via StaticClass() should succeed in AS script"), ImplementsResult, 1);
```

**差距描述**

- `没有实现`：当前没有一条 regression 同时覆盖 `implemented-interface impact`、`commandlet success schema`、`checked interface query negative path`。
- `实现方式不同`：当前测试主要按 subsystem reason 或 happy-path 场景命名；参考项目会把真实 bug/contract 直接固化为 issue test。
- `实现质量差异`：现有测试在绿灯时更容易回答“某个局部能力仍在”，但不容易回答“当前这次 interface 改动会不会让 editor/runtime/tooling 三条线再次分叉”。

**参考方案**

- `Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Public/UnLuaTestCommon.h:171-205`
  - 通过 `IMPLEMENT_UNLUA_INSTANT_TEST` / `BEGIN_TESTSUITE` 这类宏，把 issue-scoped regression 的落地成本降到很低。
- `Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Tests/Issue398Test.cpp:21-63`
  - 直接用 `Issue398` 夹具锁住 “接口列表从脚本传到 C++/蓝图层会丢失一半信息” 这个真实 interface bug。
- `Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Tests/Issue398Test.h:20-23` 与 `.../Issue398TestInterface.h:19-27`
  - 专门准备 interface fixture，而不是把问题稀释进一个泛化 happy-path scenario。

[51] 参考实现把 interface 回归直接钉成 issue test，而不是只验证抽象能力：

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Tests/Issue398Test.cpp
// 位置: 21-63
// 说明: interface 相关 bug 直接固化为独立 regression
// ============================================================================
struct FUnLuaTest_Issue398 : FUnLuaTestBase
{
	virtual bool SetUp() override
	{
		...
		const auto Chunk = R"(
            local CharacterClass = UE.UClass.Load("/UnLuaTestSuite/Tests/Regression/Issue398/BP_Character.BP_Character_C")
            local Character = G_World:SpawnActor(CharacterClass)
            local Array = UE.TArray(UE.AActor)
            Array:Add(Character)
            ...
            local ResultArray = FunctionLibrary.Test(Array)
            ...
            if ResultArray:Get(i) ~= nil then
                Result = Result + 1
            end
        )";
		UnLua::RunChunk(L, Chunk);
		...
		RUNNER_TEST_EQUAL(Result, 4); // ★ 直接锁“接口列表丢失一半信息”这个具体回归
		return true;
	}
};

IMPLEMENT_UNLUA_INSTANT_TEST(FUnLuaTest_Issue398, TEXT("UnLua.Regression.Issue398 接口列表从lua传到c++/蓝图层会丢失一半信息"))
```

**吸收建议**

- 在当前仓内补一条最小 `Issue-style` interface regression 夹具，首版就覆盖三个断言：`AnalyzeLoadedBlueprint` 能命中 implementer Blueprint、commandlet 成功输出里能看到 interface bucket、脚本侧 checked query 对非 interface 输入给出明确失败。
- 继续保留现有 `AnalyzeParentClass` / `ImplementsInterfaceMethod` 这类基础测试，但不要再把它们当成 `P10` 交付闭环的主要证据。
- 优先把 editor/runtime/tooling 共用同一份小夹具，而不是分别堆三套无关联样例；否则每次改 interface lane 都要在三处复制改名。
- 一旦真实 bug 编号或 PR 问题号出现，直接冻结为 `IssueXXX` 测试名；如果当前还没有 issue 号，也至少把 test name 写成 invariant，而不是泛化 reason 名称。

**优先级**

- `P1`
- **理由**：这类测试不会抢占 `P10` runtime owner 的 `P0` 实现顺序，但必须和 `P10` 的第一波 interface/editor 改动同批落地；否则 rollout 一旦跨过 runtime、editor、commandlet 三条线，就很难再靠现有 green bar 发现回归发生在哪一层。

### 值得吸收的设计模式（增量）

- `Schema before consumer count`：先把 artifact schema 做出来，再讨论有多少 consumer；否则 producer/consumer/observer 永远只能共享“文件名记忆”，不能共享 contract。
- `Reason model must include identity edges`：impact/reload 不应只盯 `pin/variable/node` 这类使用痕迹，也要显式表达 `implemented interface` 这类声明边。
- `Issue-shaped regression beats reason-shaped regression`：对 interface 这种跨 runtime/editor/tooling 的脆弱主线，按 bug/invariant 命名的回归比按 subsystem reason 命名更能长期保住真实行为。

### 改进路线建议（增量）

1. `P1`：在 `P10` 最小 runtime bridge 落稳后，先补 `ImplementedInterface` reason、commandlet interface bucket 和一条 implementer-only Blueprint scenario，把 editor 盲区关掉。
2. `P1`：并行把 `BindModules.Cache` 升级为 dual-write `BindModuleManifest`，至少补 `SourceKind/Scope/GeneratedAt/Producer` 四类字段；legacy raw list 仅作 fallback。
3. `P1`：新增一条 issue-scoped interface regression，复用同一组夹具同时验证 runtime query、editor impact、commandlet summary，避免三条线继续各测各的。
4. `P2`：等 manifest 与 implementer lane 稳定后，再把 state dump、debug/tooling 与外部 bind provider 统一改为消费同一份 artifact contract。

---

## 深化分析 (2026-04-09 07:03:59)

### [D1] 插件架构与模块划分：binding eligibility 的 authority 仍拆在 editor / runtime / UHT / event 四条 lane

**当前状态**

这轮新增观察不是“当前没有准入规则”，而是**同一个 symbol 是否应该进入 Angelscript 表面**，现在分别由四套不同 owner 决定：

- `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:1088-1160` 的 `GenerateBindDatabases()` 只从 `TObjectRange<UClass>()` 收录非 `Abstract/Deprecated`、非 `Private/` header、且存在 `BlueprintCallable` 的类。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:962-1047` 的 `ShouldBindEngineType()` 又按 `CLASS_Native`、editor-only、`UASClass`、`NotInAngelscript`、`BlueprintType`、`BlueprintCallable | BlueprintEvent` 重新做一遍 class-level 判定。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:83-117` 的 `ShouldSkipBlueprintCallableFunction()` 额外要求 `FUNC_Native`，并硬编码 `UActorComponent::GetOwner` 例外。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp:553-587` 的 `BindBlueprintEvent()` 再独立处理 `DeprecatedFunction`、`BlueprintInternalUseOnly`、`AllowAngelscriptOverride`、signature validity 和参数数目。
- `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:490-515` 的 `ShouldGenerate()` 又用自己的 `header + metadata + CustomThunk + hardcoded special case` 规则做 UHT lane 准入。

这不是“没有实现”，而是**实现方式不同且 authority 分裂**。对 `P10 UInterface` 和后续 `Bind API GAP` 来说，这意味着每次补一类 symbol，不是在一处把状态从 `Unsupported` 切到 `Supported`，而是在四条 lane 里手动同步多组条件。

```
[Current] Eligibility Ownership Split
EditorClassScan
├─ GenerateBindDatabases()                         // 只决定哪些 UClass 进入 class DB
├─ skip deprecated/private/no BlueprintCallable
└─ output -> RuntimeClassDB / EditorClassDB

RuntimeTypeScan
├─ ShouldBindEngineType()                          // 决定 class/type 是否可见
└─ class flag + metadata + callable/event probe

RuntimeCallableOrEvent
├─ ShouldSkipBlueprintCallableFunction()           // direct callable lane
└─ BindBlueprintEvent()                            // event lane

UhtFunctionTable
└─ ShouldGenerate()                                // sidecar 生成 lane

Result
└─ one symbol may have four different admission answers
```

[52] 当前 direct callable lane 与 UHT lane 的准入逻辑已经是两套 owner：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp
// 函数: FAngelscriptBinds::ShouldSkipBlueprintCallableFunction
// 位置: 83-117
// 说明: runtime direct callable lane 的准入规则
// ============================================================================
bool FAngelscriptBinds::ShouldSkipBlueprintCallableFunction(const UFunction* Function)
{
	static const FName NAME_Function_NotInAngelscript(TEXT("NotInAngelscript"));
	static const FName NAME_Function_BlueprintInternalUseOnly(TEXT("BlueprintInternalUseOnly"));
	static const FName NAME_Function_UsableInAngelscript(TEXT("UsableInAngelscript"));

	if (Function == nullptr)
	{
		return true;
	}

	if (!Function->HasAnyFunctionFlags(FUNC_Native))
	{
		return true; // ★ direct lane 要求 native
	}

	if (Function->HasMetaData(NAME_Function_NotInAngelscript))
	{
		return true;
	}

	if (Function->HasMetaData(NAME_Function_BlueprintInternalUseOnly) && !Function->HasMetaData(NAME_Function_UsableInAngelscript))
	{
		return true;
	}

	if (const UClass* OwningClass = Function->GetOuterUClass())
	{
		if (OwningClass == UActorComponent::StaticClass() && Function->GetFName() == FName(TEXT("GetOwner")))
		{
			return true; // ★ 还有 runtime-only 的硬编码例外
		}
	}

	return false;
}
```

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs
// 函数: ShouldGenerate
// 位置: 490-515
// 说明: UHT function-table lane 的准入规则
// ============================================================================
private static bool ShouldGenerate(UhtClass classObj, UhtFunction function)
{
	if (classObj.HeaderFile == null || !IsSupportedHeader(classObj.HeaderFile.FilePath))
	{
		return false;
	}

	if (!AngelscriptFunctionTableExporter.IsBlueprintCallable(function))
	{
		return false; // ★ UHT lane 只认 BlueprintCallable/Pure
	}

	if (function.MetaData.ContainsKey("NotInAngelscript") ||
		(function.MetaData.ContainsKey("BlueprintInternalUseOnly") && !function.MetaData.ContainsKey("UsableInAngelscript")))
	{
		return false;
	}

	if (classObj.SourceName == "UUniversalObjectLocatorScriptingExtensions" &&
		(function.SourceName == "MakeUniversalObjectLocator" || function.SourceName == "UniversalObjectLocatorFromString"))
	{
		return false; // ★ 这里又有 UHT-only 的硬编码例外
	}

	return !function.FunctionExportFlags.ToString().Contains("CustomThunk", StringComparison.Ordinal);
}
```

**差距描述**

- `没有实现`：不是。当前已经有 editor/runtime/UHT/event 四条独立准入逻辑。
- `实现方式不同`：参考插件通常先定义一份统一的 export/generator context，再由 runtime 或 artifact lane 消费；当前则是每条 lane 自己判断。
- `实现质量差异`：当前没有共享 `ReasonCode / Lane / SourceMeta`。因此同一个 interface symbol 为什么“进 editor 不进 UHT”“进 event 不进 direct”无法从一份账本解释。

**参考方案**

- **UnLua**：先用 `Binding.h` / `Binding.cpp` 把 `class / enum / function / type` 收成一份 `FExported` registry，再由 `FLuaEnv` 在初始化阶段统一消费。
  - `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/Binding.h:21-43`
  - `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Binding.cpp:19-57`
  - `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:144-159`
- **puerts**：`FTypeScriptDeclarationGenerator` 把 ignore list、类/资产扫描和 `UCodeGenerator` 扩展都放进同一个 generator context；扩展点不另起一套并行 authority。
  - `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:360-457,1708-1717`
  - `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Public/CodeGenerator.h:10-27`

[53] 参考实现先有统一 exported / generator context，再谈各消费面：

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Binding.cpp
// 位置: 19-57
// 说明: UnLua 先把 exported 对象收进单一 registry
// ============================================================================
struct FExported
{
	TArray<IExportedEnum*> Enums;
	TArray<IExportedFunction*> Functions;
	TMap<FString, IExportedClass*> ReflectedClasses;
	TMap<FString, IExportedClass*> NonReflectedClasses;
	TMap<FString, TSharedPtr<ITypeInterface>> Types;
};

void ExportClass(IExportedClass* Class)
{
	if (Class->IsReflected())
		GetExported()->ReflectedClasses.Add(Class->GetName(), Class);
	else
		GetExported()->NonReflectedClasses.Add(Class->GetName(), Class);
}

void ExportFunction(IExportedFunction* Function)
{
	GetExported()->Functions.Add(Function);
}

void AddType(FString Name, TSharedPtr<ITypeInterface> TypeInterface)
{
	if (!ensure(!Name.IsEmpty() && TypeInterface))
		return;
	GetExported()->Types.Add(Name, TypeInterface);
}
```

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp
// 函数: FTypeScriptDeclarationGenerator::GenTypeScriptDeclaration / GenTypeScriptCppDeclaration
// 位置: 360-373, 1708-1717
// 说明: puerts 的 include/ignore/extension 共享同一个 generator context
// ============================================================================
for (int i = 0; i < SortedClasses.Num(); ++i)
{
	UObject* Class = SortedClasses[i];
	const TArray<FString>& IgnoreClassListOnDTS = IPuertsModule::Get().GetIgnoreClassListOnDTS();
	if (IgnoreClassListOnDTS.Contains(Class->GetName()))
	{
		continue; // ★ 准入先在统一 generator context 里决定
	}
	Gen(Class);
}

for (int i = 0; i < SortedClasses.Num(); ++i)
{
	UClass* Class = Cast<UClass>(SortedClasses[i]);
	if (Class && Class->ImplementsInterface(UCodeGenerator::StaticClass()))
	{
		ICodeGenerator::Execute_Gen(Class->GetDefaultObject()); // ★ 扩展也进入同一轮生成
	}
}
```

**吸收建议**

- 在插件内新增最小 `FAngelscriptBindingEligibilityDecision` 与 `EAngelscriptBindingLane`，第一版至少覆盖 `EditorClassScan / RuntimeType / RuntimeCallable / RuntimeEvent / UhtFunctionTable`。
- 首阶段不要改任何功能开关，只把 `GenerateBindDatabases()`、`ShouldBindEngineType()`、`ShouldSkipBlueprintCallableFunction()`、`BindBlueprintEvent()`、`ShouldGenerate()` 的判断收敛到共享 helper，并把 `ReasonCode` 写出到 sidecar/dump。
- `P10` 的 interface rollout 不再以“改了几处 if”为验收，而是以同一个 symbol 在五条 lane 上的 `Decision -> ReasonCode -> ChosenLane` 是否符合策略为验收。
- 对用户可配置的 allow/deny 规则，优先落到 `UAngelscriptSettings` 或等价 settings surface，不要继续扩散硬编码例外。

**优先级**

- `P1`
- **理由**：它不是 `P10` 的第一颗 runtime bridge 钉子，但如果没有共享 eligibility owner，后续 `UInterface/FInterfaceProperty` 每开一条 lane 都会重新制造一次口径分叉；这会直接污染 `Bind API GAP` 的量化结果。

### [D6] 代码生成与 IDE 支持：外部 provider 目前只能注入 live bind，进不了正式 artifact lane

**当前状态**

当前仓库其实已经允许 C++ 扩展者在插件外注册 bind，但这个能力只稳定停留在 live runtime：

- `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:431-483` 公开了 `FBind` / `RegisterBinds()` / `GetBindInfoList()`，任何模块都能注册 bind。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineTests.cpp:610-639` 与 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp:276-313` 也明确在 `Binds/` 目录外直接构造 `FBind` 做回归。
- 但 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:999-1077` 的 `GenerateNativeBinds()` 只会把 `GetRuntimeClassDB()/GetEditorClassDB()` 里的类扫描结果切成 `ASRuntimeBind_* / ASEditorBind_*` 并写 `BindModules.Cache`。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1466-1490,1915-1921` 运行时只加载插件基目录的 `BindModules.Cache`，然后统一 `CallBinds()`。
- `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:51-78` 的 UHT sidecar 只遍历 `factory.Session.Modules` 里的 supported modules。
- 更关键的是，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:176-183` 的 `GetBindInfoList()` 只输出 `BindName / BindOrder / bEnabled`，没有 provider/source/artifact 信息。

所以这里的差距不是“没有扩展能力”，而是**外部 provider 只能进 live bind，进不了 `BindModules / BindDB / UHT / IDE artifact` 这条正式交付链**。这对“把 `Plugins/Angelscript` 做成独立可复用插件”是一个明显 delivery contract 缺口。

```
[Current] External Provider Reach
ProjectModule / ExtraPlugin
├─ FBind / RegisterTypeFinder()                    // 可以注入 live runtime
└─ tests can observe execution order               // 现有自动化已证明这点

Artifact Producers
├─ GenerateBindDatabases() -> RuntimeClassDB       // 只扫描当前反射类
├─ GenerateNativeBinds() -> BindModules.Cache      // 只写内部 generated shard
├─ UHT Generate() -> AS_FunctionTable_*            // 只看 UHT session modules
└─ GetBindInfoList()                               // 只有 name/order/enabled

Result
└─ external bind can execute, but cannot become first-class artifact
```

[54] 当前 public bind API 与 artifact producer 已经明显分叉：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h
// 位置: 431-483
// 说明: runtime 公开了 bind 注入入口，但 bind info 只有最小排序信息
// ============================================================================
struct FBindInfo
{
	FName BindName;
	int32 BindOrder = 0;
	bool bEnabled = true; // ★ 没有 ProviderName / ArtifactLane / SourceFile
};

struct ANGELSCRIPTRUNTIME_API FBind
{
	FBind(FName BindName, int32 BindOrder, TFunction<void()> Function)
	{
		FAngelscriptBinds::RegisterBinds(BindName, BindOrder, MoveTemp(Function));
	}
	...
};

static TArray<FBindInfo> GetBindInfoList(const TSet<FName>& DisabledBindNames = TSet<FName>());
static TArray<FString>& GetBindModuleNames();
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 函数: GenerateNativeBinds
// 位置: 999-1077
// 说明: generated bind module 只来自 RuntimeClassDB / EditorClassDB
// ============================================================================
void FAngelscriptEditorModule::GenerateNativeBinds()
{
	GenerateBindDatabases();

	const uint32 ModuleCount = 10;
	TArray<FString> Keys;
	FAngelscriptBinds::GetRuntimeClassDB().GetKeys(Keys);
	TArray<FString> ModuleArray;
	FAngelscriptBinds::GetBindModuleNames().Empty();

	for (int i = 0; i < Keys.Num(); i++)
	{
		ModuleArray.Add(Keys[i]);
		if (ModuleArray.Num() >= ModuleCount)
		{
			FString ModuleName = FString("ASRuntimeBind_");
			ModuleName += FString::FromInt(i - (ModuleArray.Num() - 1));
			GenerateNewModule(ModuleName, ModuleArray, false); // ★ 只为内部类扫描结果生成 shard
			FAngelscriptBinds::GetBindModuleNames().Add(ModuleName);
			ModuleArray.Empty();
		}
	}

	FAngelscriptBinds::SaveBindModules(FString(FAngelscriptEngine::GetScriptRootDirectory() / "BindModules.Cache"));
}
```

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs
// 函数: Generate
// 位置: 51-78
// 说明: UHT sidecar 只遍历当前 UHT session modules
// ============================================================================
public static int Generate(IUhtExportFactory factory)
{
	AngelscriptSupportedModules supportedModules = LoadSupportedModules(factory);
	...
	foreach (UhtModule module in factory.Session.Modules)
	{
		if (!supportedModules.All.Contains(module.ShortName))
		{
			continue;
		}

		AngelscriptModuleGenerationSummary? moduleSummary =
			GenerateModule(factory, module, supportedModules.EditorOnly.Contains(module.ShortName), generatedPaths, csvEntries);
		...
	}
	...
}
```

**差距描述**

- `没有实现`：不是。当前 runtime 确实允许外部模块注入 bind。
- `能力缺失`：缺少正式的 artifact extension lane。外部 provider 无法自然进入 `BindModules.Cache`、`Binds.Cache`、`AS_FunctionTable_*`、未来 `Docs/DebugDatabase manifest`。
- `实现质量差异`：由于 `FBindInfo` 没有 provenance，连内建 generated shard 与手写 bind 也难以稳定 join，更不用说后续外部 provider merge。

**参考方案**

- **UnLua**：`Binding.h` 公开 `ExportClass / ExportEnum / ExportFunction / AddType`；`Binding.cpp` 用 `FExported` 统一保存 `Enums/Functions/Classes/Types`；`FLuaEnv` 初始化时统一注册。这意味着“能扩展”与“能进入正式 registry”是同一件事。
  - `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/Binding.h:21-43`
  - `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/Binding.cpp:19-57`
  - `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/LuaEnv.cpp:144-159`
- **puerts**：`UCodeGenerator` 把第三方生成扩展做成正式 interface；`FTypeScriptDeclarationGenerator` 在同一轮 `GenTypeScriptDeclaration()` / `GenTypeScriptCppDeclaration()` 内执行它们，扩展不会掉出 declaration artifact 主链。
  - `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Public/CodeGenerator.h:10-27`
  - `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:360-457,1708-1717`

[55] 参考插件把“可扩展”与“可进入 artifact 主链”做成同一个 contract：

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/Binding.h
// 位置: 21-43
// 说明: UnLua 直接公开 exported contract
// ============================================================================
UNLUA_API void ExportClass(IExportedClass* Class);
UNLUA_API void ExportEnum(IExportedEnum* Enum);
UNLUA_API void ExportFunction(IExportedFunction* Function);
UNLUA_API void AddType(FString Name, TSharedPtr<ITypeInterface> TypeInterface);

UNLUA_API TMap<FString, IExportedClass*> GetExportedReflectedClasses();
UNLUA_API TMap<FString, IExportedClass*> GetExportedNonReflectedClasses();
UNLUA_API TArray<IExportedEnum*> GetExportedEnums();
UNLUA_API TArray<IExportedFunction*> GetExportedFunctions();
UNLUA_API TSharedPtr<ITypeInterface> FindTypeInterface(FString Name);
```

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Public/CodeGenerator.h
// 文件: Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp
// 位置: 10-27, 1708-1717
// 说明: puerts 把第三方 artifact 生成扩展纳入主 generator context
// ============================================================================
UINTERFACE(MinimalAPI)
class UCodeGenerator : public UInterface
{
	GENERATED_BODY()
};

class DECLARATIONGENERATOR_API ICodeGenerator
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintNativeEvent)
	void Gen() const;
};

for (int i = 0; i < SortedClasses.Num(); ++i)
{
	UClass* Class = Cast<UClass>(SortedClasses[i]);
	if (Class && Class->ImplementsInterface(UCodeGenerator::StaticClass()))
	{
		ICodeGenerator::Execute_Gen(Class->GetDefaultObject()); // ★ 扩展也进正式 artifact pass
	}
}
```

**吸收建议**

- 在 `AngelscriptRuntime` 侧定义正式 `IAngelscriptBindingExtension`，最少包含 `RegisterTypes()`、`RegisterBinds()`、`DescribeContribution()`；旧 `FBind` 继续保留为兼容入口。
- 在 editor/tooling 侧补一条对等的 artifact extension lane。可以是 `IAngelscriptArtifactContributor`，也可以让 `IAngelscriptBindingExtension` 增加 `ContributeArtifacts()`；关键不是接口名字，而是**外部 provider 能参与与内建 provider 相同的一轮 manifest / summary / docs / IDE 生成**。
- `BindModules.Cache` 不应再只是一列 raw module name；至少要并行补一份 provider-aware manifest，字段包括 `ProviderName`、`ArtifactLane`、`SourceModule`、`GeneratedAt`、`Version/Hash`。
- 这条线不应抢在 `P10` runtime interface owner 之前做大改。正确顺序是：先把 `UInterface/FInterfaceProperty` 在插件内主链跑通，再把“未来外部 provider 如何安全补 family”产品化。

**优先级**

- `P2`
- **理由**：它不是 `P10 UInterface` 的直接 blocker，因此不应抢 `P0/P1`；但本仓目标是把 `Plugins/Angelscript` 做成可复用插件，外部 provider 迟早要从“能跑”升级到“能交付、能审计、能进 IDE artifact”，所以不应无限后移。

### 值得吸收的设计模式（增量）

- `Eligibility before lane work`：先为 symbol 产出共享 `Decision + ReasonCode + Lane`，再让 editor/runtime/UHT/event 消费；不要继续让每条 lane 各自发明准入条件。
- `Extension is only real when artifact-aware`：扩展点如果只能注入 live runtime，却进不了 generator/manifest/docs/IDE，就还不是正式插件 contract。
- `Report-only first, policy switch later`：先 dual-write 决策账本与 provider manifest，不急着一次性改变绑定行为；这样最符合当前 `P10` 渐进收口节奏。

### 改进路线建议（增量）

1. `P1`：新增共享 `FAngelscriptBindingEligibilityDecision`，先把 `GenerateBindDatabases()`、`ShouldBindEngineType()`、`ShouldSkipBlueprintCallableFunction()`、`BindBlueprintEvent()`、UHT `ShouldGenerate()` 接到同一份 helper，上线前不改行为，只写 `Lane/ReasonCode`。
2. `P1`：拿 `UInterface` 作为试点 family，把同一个 symbol 在 `RuntimeCallable / RuntimeEvent / UhtFunctionTable` 三条 lane 的 decision 对齐；这样 `Bind API GAP` 才能按 symbol 量化，而不是按文件或报表口径猜。
3. `P2`：定义正式 `IAngelscriptBindingExtension` + provider-aware artifact manifest，让未来项目侧/外部插件侧扩展不再停留在 live bind。
4. `P2`：等 provider manifest 稳定后，再把 `Docs / DebugDatabase / state dump / coverage` 统一切到消费同一份 provider + decision contract，完成“可扩展”到“可交付”的收口。

---

## 深化分析 (2026-04-09 07:13:03)

### 本轮新增关键发现

- `D11` 当前最值得补的不是“再加一种 cache”，而是把 `script root discovery`、`BindModules.Cache`、扩展插件形态和 packaging policy 收敛成同一份交付 contract。现状已经有 artifact，但 owner 仍被 `CanContainContent`、project/plugin 双根和隐式路径拼接拆散。
- `D7` 的 `ContentBrowserDataSource` 已经把脚本资产挂进 `/All/Angelscript`，但编辑、删除、引用复制、路径回桥都还是空壳；这不是“没有编辑器集成”，而是**authoring loop 没闭合**。
- `D10` 真正缺的不是文档数量，而是**首轮 authoring 入口**。当前 shipped 菜单更像“打开工作区/调试旧生成器”，还没有像 UnLua / UnrealCSharp 那样提供面向脚本作者的模板或脚手架。

### 差距矩阵（增量）

| 维度 | 新增观察点 | 总评等级 | 主要差距类型 | 优先级 |
| --- | --- | --- | --- | --- |
| `D11` 部署与打包 | extension package shape / artifact root / staging policy | `实现差异` | 有 cache 与脚本根，但 discovery、manifest、packaging 各自为政 | `P1` |
| `D7` 编辑器集成 | `ContentBrowser` authoring workflow | `能力缺失` | data source 已接入，但 edit/ref/delete/path bridge 未实现 | `P2` |
| `D10` 文档与示例组织 | first-run template / scaffold entrypoint | `能力缺失` | 有 docs / tests / examples，但没有 shipped scaffold workflow | `P2` |

### [D11] 部署与打包：扩展包形态仍被 `CanContainContent + root split` 隐式锁死

**当前状态**

当前仓库已经拥有 `Binds.Cache`、`BindModules.Cache`、`PrecompiledScript*.Cache` 三类交付物，但它们不属于同一个 authority：

- 脚本根发现只扫描 `<Project>/Script` 与 `GetEnabledPluginsWithContent()` 返回插件的 `<Plugin>/Script`，见 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:558-565,1326-1363`。
- runtime 从 project script root 读 `Binds.Cache` 和 `PrecompiledScript*.Cache`，却从主插件 base dir 读 `BindModules.Cache`，见 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1466-1478,1519-1535`。
- editor 生成 bind shard 后把 `BindModules.Cache` 写回 project script root，而不是写回主插件根或扩展包根，见 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:999-1077`。
- 更底层的问题是：主插件和生成出来的 standalone bind plugin 模板都硬编码 `"CanContainContent": false`，见 `Plugins/Angelscript/Angelscript.uplugin:13-16` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:2300-2317`。这意味着“能被自动发现脚本”的插件形态与“能承载自动生成 bind module”的插件形态天然错位。

```
[Angelscript] Delivery Root Split
ProjectRoot/Script
├─ Binds.Cache                                // runtime 从 project script root 读
├─ PrecompiledScript*.Cache                   // runtime 从 project script root 读
└─ BindModules.Cache                          // editor 生成后写在这里

PluginRoot/Plugins/Angelscript
├─ Angelscript.uplugin (CanContainContent=false)
└─ BindModules.Cache                          // runtime 实际从这里读

EnabledPluginsWithContent
└─ <Plugin>/Script                            // 只有 content plugin 才进入脚本根发现
```

[56] 当前 delivery contract 同时被 `content plugin discovery`、`project root cache` 和 `plugin root bind manifest` 三套规则持有：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: FAngelscriptEngineDependencies::CreateDefault / DiscoverScriptRoots / 构造阶段加载 bind artifacts
// 位置: 558-565, 1326-1363, 1466-1478
// 说明: 脚本根发现、bind DB、bind module manifest 当前并不共享同一条交付根
// ============================================================================
Dependencies.GetEnabledPluginScriptRoots = []()
{
	TArray<FString> ScriptRoots;
	for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetEnabledPluginsWithContent())
	{
		ScriptRoots.Add(Plugin->GetBaseDir() / TEXT("Script")); // ★ 只有 content plugin 会进入脚本根发现
	}
	return ScriptRoots;
};

FString RootPath = Dependencies.ConvertRelativePathToFull(Dependencies.GetProjectDir() / TEXT("Script"));
...
for (const FString& PluginScriptRoot : Dependencies.GetEnabledPluginScriptRoots())
{
	const FString ScriptPath = Dependencies.ConvertRelativePathToFull(PluginScriptRoot);
	if (Dependencies.DirectoryExists(ScriptPath) && ScriptPath != RootPath)
	{
		DiscoveredRootPaths.Add(ScriptPath); // ★ plugin script roots 走的是另一套发现入口
	}
}

FAngelscriptBindDatabase::Get().Load(GetScriptRootDirectory() / TEXT("Binds.Cache"), bGeneratePrecompiledData);

TSharedPtr<IPlugin> plugin = IPluginManager::Get().FindPlugin("Angelscript");
if (plugin)
{
	FAngelscriptBinds::LoadBindModules(plugin->GetBaseDir() / "BindModules.Cache"); // ★ bind module manifest 却固定从主插件根读取
}
```

[57] 当前 editor 生成链与扩展插件模板进一步放大了这条分裂：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 函数: GenerateNativeBinds / GeneratePluginDirectory
// 位置: 999-1077, 2300-2317
// 说明: bind shard 写 project script root；生成的 standalone plugin 模板又强制 `CanContainContent = false`
// ============================================================================
void FAngelscriptEditorModule::GenerateNativeBinds()
{
	...
	FAngelscriptBinds::SaveBindModules(FString(FAngelscriptEngine::GetScriptRootDirectory() / "BindModules.Cache"));
	// ★ editor 只把 manifest 写到 project script root
}

void FAngelscriptEditorModule::GeneratePluginDirectory(FString PluginName, TArray<FString>& PluginFile, TArray<FString> ModuleNames)
{
	...
	Lines.Add("\t\"EnabledByDefault\": true,");
	Lines.Add("\t\"CanContainContent\": false,"); // ★ 生成的 bind plugin 模板天然不会进入 `GetEnabledPluginsWithContent()`
	Lines.Add("\t\"IsBetaVersion\": false,");
	...
}
```

**差距描述**

- `没有实现`：不是。当前已经有 project-root cache、plugin script root discovery、generated bind shard 和 precompiled cache。
- `实现方式不同`：当前把“扩展包是否可发现”隐式绑定到 `CanContainContent`，而参考方案更倾向于显式声明 extension package shape 或自动补 packaging policy。
- `实现质量差异`：同一份 `BindModules.Cache` 在 editor 和 runtime 之间存在 project/plugin 双根分裂；即使先不谈 `P10`，这也会直接削弱 standalone plugin 的 delivery baseline。

**参考方案**

- **UnLua** 把扩展包形态显式化：`UnLuaExtensions` 目录下的内容插件以 `CanContainContent = true` 声明自己，editor 再自动把这些 `Content/Script` 路径写入 `DirectoriesToAlwaysStageAsUFS`。
  - `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp:186-239`
  - `Reference/UnLua/Plugins/UnLuaExtensions/LuaSocket/LuaSocket.uplugin:13-22`
  - `Reference/UnLua/Plugins/UnLuaExtensions/LuaSocket/Source/Private/LuaSocketModule.cpp:30-37`
- **UnrealCSharp** 则把 publish 根与 staging policy 放进 editor 自动修复链，而不是让用户手工猜路径。
  - `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp:209-234`
  - `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Common/FUnrealCSharpFunctionLibrary.cpp:995-1049`

[58] 参考项目把“扩展包长什么样”和“打包时怎么带上它”做成显式 contract：

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaEditorModule.cpp
// 函数: SetupPackagingSettings
// 位置: 186-239
// 说明: UnLua 显式识别 extension content plugin，并自动补 staging policy
// ============================================================================
auto ScriptPaths = TArray<FString>{TEXT("Script"), TEXT("../Plugins/UnLua/Content/Script")};

const auto Plugins = IPluginManager::Get().GetEnabledPlugins();
for (const auto Plugin : Plugins)
{
	if (!Plugin->CanContainContent())
		continue;

	const auto ContentDir = Plugin->GetContentDir();
	if (!ContentDir.Contains("UnLuaExtensions"))
		continue;

	auto ScriptPath = ContentDir / "Script";
	if (!FPaths::DirectoryExists(ScriptPath))
		continue;

	if (FPaths::MakePathRelativeTo(ScriptPath, *FPaths::ProjectContentDir()))
		ScriptPaths.Add(ScriptPath); // ★ 扩展包形态与 staging 规则是同一条 owner
}

for (auto& ScriptPath : ScriptPaths)
{
	...
	PackagingSettings->DirectoriesToAlwaysStageAsUFS.Add(DirectoryPath); // ★ editor 主动修复打包策略
}
```

**吸收建议**

- 先补一个**只读 delivery descriptor**，不要一上来改动 `P10` 主线的绑定逻辑。第一阶段只解决三件事：`ScriptRoots`、`BindModulesManifestPath`、`ArtifactRootTag(Project/Plugin/Extension)`。
- 把 `BindModules.Cache` 的 save/load authority 收敛到同一 helper；在 schema 稳定前允许 dual-read，但必须让 runtime、editor、state dump 都能报告“实际命中的 root 是哪一个”。
- 对扩展插件形态做显式选择，而不是继续借 `CanContainContent` 猜：要么正式支持 `CanContainContent=true` 的 script extension plugin，要么引入 `ExtensionPackageDescriptor` 让 code-only leaf plugin 也能声明脚本根与 bind manifest。
- packaging policy 先走 `warn/report-only`，再考虑 auto-repair；这样不会抢占 `P10 UInterface` 的 critical path。

**优先级**

- `P1`
- **理由**：它不是 `P10 UInterface` 的直接 blocker，但 `AGENTS.md` 已把“known blockers & delivery baseline”排在 onboarding 之前。若不先把 delivery root 与 extension package 形态收口，后续即使补完 `Bind API GAP`，插件仍难以作为独立交付物稳定复用。

### [D7] 编辑器集成：`ContentBrowserDataSource` 现在更像只读浏览层，不是 authoring workflow

**当前状态**

这一轮新增补充不是“有没有 `ContentBrowser` 数据源”，而是**这个数据源有没有真正承接 authoring 行为**：

- `OnEngineInitDone()` 确实会创建并激活 `AngelscriptData`，见 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:111-119`。
- `UAngelscriptContentBrowserDataSource` 会把 `AssetsPackage` 中对象映射到 `/All/Angelscript/<AssetName>`，见 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:16-29,65-121`。
- 但 `EnumerateItemsAtPath()`、`GetItemPhysicalPath()`、`CanEditItem()`、`EditItem()`、`BulkEditItems()`、`AppendItemReference()`、`TryGetCollectionId()`、`Legacy_TryConvert*()` 当前全部空实现或直接 `return false`，见 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp:124-256`。
- 创建与打开资产仍通过 runtime delegate 跳回 editor 模块静态函数 `ShowCreateBlueprintPopup()` / `ShowAssetListPopup()`，见 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:404-409,418-559`。换句话说，浏览器负责“看见”，真正的 authoring 仍走旁路 popup。

```
[Angelscript] ContentBrowser Authoring Split
OnEngineInitDone()
└─ ActivateDataSource("AngelscriptData")          // 虚拟数据源已接入

UAngelscriptContentBrowserDataSource
├─ EnumerateItemsMatchingFilter()                 // 能列出 `/All/Angelscript/*`
├─ CreateAssetItem()                              // 能显示 Script Asset
├─ CanEditItem / EditItem -> false                // 不能原生打开/编辑
├─ AppendItemReference -> false                   // 不能复制引用
└─ Legacy_TryConvert* -> false                    // 不能从虚拟路径回桥

EditorModule side popup
├─ ShowCreateBlueprintPopup()                     // 创建走旁路
└─ ShowAssetListPopup()                           // 打开走旁路
```

[59] 当前 data source 的核心问题不是“没接入”，而是 mutation/authoring contract 还没进来：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptContentBrowserDataSource.cpp
// 函数: CreateAssetItem / CanEditItem / EditItem / BulkEditItems / AppendItemReference / Legacy_TryConvertPackagePathToVirtualPath
// 位置: 16-29, 187-205, 220-256
// 说明: 当前虚拟脚本项可以显示，但不能沿 Content Browser 标准工作流继续 authoring
// ============================================================================
return FContentBrowserItemData(
	this,
	EContentBrowserItemFlags::Type_File | EContentBrowserItemFlags::Category_Asset,
	*(TEXT("/All/Angelscript/") + Asset->GetName()), Asset->GetFName(), FText::FromString(DisplayName), Payload, *Payload->Path);
// ★ 浏览器里能看见脚本资产

bool UAngelscriptContentBrowserDataSource::CanEditItem(const FContentBrowserItemData& InItem, FText* OutErrorMsg)
{
	return false; // ★ 不能原生编辑
}

bool UAngelscriptContentBrowserDataSource::EditItem(const FContentBrowserItemData& InItem)
{
	return false; // ★ 不能原生打开
}

bool UAngelscriptContentBrowserDataSource::AppendItemReference(const FContentBrowserItemData& InItem, FString& InOutStr)
{
	return false; // ★ 不能复制引用
}

bool UAngelscriptContentBrowserDataSource::Legacy_TryConvertPackagePathToVirtualPath(const FName InPackagePath, FName& OutPath)
{
	return false; // ★ 虚拟路径与真实对象路径不能回桥
}
```

[60] 当前 authoring 仍由 editor 模块静态 popup 拿着：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 函数: StartupModule 内绑定 CreateBlueprint delegate / ShowCreateBlueprintPopup
// 位置: 404-409, 418-537
// 说明: 创建 Blueprint/DataAsset 仍是旁路 workflow，不在 data source owner 内
// ============================================================================
FAngelscriptRuntimeModule::GetEditorCreateBlueprint().AddLambda(
	[](UASClass* ScriptClass)
	{
		FAngelscriptEditorModule::ShowCreateBlueprintPopup(ScriptClass);
	});

FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
FString SaveObjectPath = ContentBrowserModule.Get().CreateModalSaveAssetDialog(SaveAssetDialogConfig);
...
Asset = FKismetEditorUtilities::CreateBlueprint(
	Class, Package, AssetName, BPTYPE_Normal,
	BlueprintClass, BlueprintGeneratedClass, FName("AngelscriptCreateBlueprint"));
...
GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Asset);
// ★ “创建/打开”是 editor 模块自管 popup，而不是虚拟 data source 的原生 add-new/edit lane
```

**差距描述**

- `没有实现`：不是。当前已经有 `ContentBrowserDataSource`、popup 创建入口和资产列表弹窗。
- `能力缺失`：`Edit / Reference / PathBridge / Mutation` 这些 authoring 核心能力尚未进入 data source。
- `实现质量差异`：owner 被拆成“浏览器列举”和“旁路 popup”，导致后续若要支持更多脚本资产类型，只能继续扩散静态 helper。

**参考方案**

- **UnrealCSharp** 把 `display / create / edit / delete / reference / path conversion` 放回同一个 `UDynamicDataSource` owner：
  - `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp:518-700`
  - `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/NewClass/DynamicNewClassUtils.cpp:12-48`
- **UnLua** 虽然没有虚拟 data source，但 Blueprint toolbar 至少把 “Bind / Create Lua Template / RevealInExplorer” 放进同一条上下文菜单 owner，减少旁路。
  - `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:47-75,257-313`

[61] UnrealCSharp 的 data source 证明“既然已经走进 `ContentBrowser`，就不该只做展示层”：

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ContentBrowser/DynamicDataSource.cpp
// 函数: CanEditItem / EditItem / BulkEditItems / DeleteItem / Legacy_TryConvertPackagePathToVirtualPath / OnNewClassRequested
// 位置: 518-700
// 说明: 同一个 data source 同时承担编辑、删除、引用与新建入口
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
		       : false; // ★ 编辑直接回到 data source owner
}

bool UDynamicDataSource::Legacy_TryConvertPackagePathToVirtualPath(const FName InPackagePath, FName& OutPath)
{
	return TryConvertInternalPathToVirtual(InPackagePath, OutPath); // ★ 虚拟路径与真实路径可回桥
}

void UDynamicDataSource::OnNewClassRequested(const FName& InSelectedPath)
{
	...
	FDynamicNewClassUtils::OpenAddDynamicClassToProjectDialog(SelectedFileSystemPath); // ★ 新建入口也在同一条 workflow 里
}
```

**吸收建议**

- 不要先推翻现有 popup；先新增一个薄的 `FAngelscriptAssetWorkflowService`，让 `ContentBrowserDataSource` 与 `ShowCreateBlueprintPopup()` / `ShowAssetListPopup()` 共用同一条 create/open/reference/path resolver。
- 第一阶段只补最小闭环：`EditItem()`、`AppendItemReference()`、`Legacy_TryConvert*()`；这些改动不需要等 `P10` 全部完成，也不会碰 `Bind_BlueprintType` 主链。
- 第二阶段再把 `ContentBrowser.AddNewContextMenu` 或等价 add-new surface 接进来，让“看见脚本资产”和“新建/打开脚本资产”回到同一个 owner。
- 删除与 undo/redo 可以放到后手，避免这一轮为了 workflow 纯度引入大范围 mutation 风险。

**优先级**

- `P2`
- **理由**：它不会阻塞 `P10 UInterface`，也不是 delivery baseline 的最前置问题；但 `AGENTS.md` 已把“onboarding assets & workflow entry points”列为 delivery baseline 之后的下一阶段，而当前 data source 的只读形态会直接拖慢这条主线。

### [D10] 文档与示例组织：当前首轮入口仍是“打开工作区”，不是模板/脚手架

**当前状态**

这一轮不重复前文已经确认的 `docs.hpp + examples + learning tests` 优势，只补**新用户第一次点击插件时真正会看到什么**：

- `RegisterToolsMenuEntries()` 当前只挂了三个内建动作：`Open Angelscript workspace (VS Code)`、`Legacy Native Bind Generator (Debug Only)`、`Run Function Tests`，见 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:696-745`。
- `UScriptEditorMenuExtension` 与 `UScriptEditorSubsystem` 是通用扩展壳，能承载项目方自定义，但插件本身没有随之提供默认 scaffold workflow，见 `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.h:42-138` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h:7-90`。
- `UScriptableFactory` 也只暴露 `CreateFromText/CreateFromBinary` primitive，没有接回 `AssetTools`/`ContentBrowser` 默认工作流，见 `Plugins/Angelscript/Source/AngelscriptEditor/Private/FunctionLibraries/ScriptableFactory.h:8-33`。

```
[Angelscript] First-Run Authoring Entry
Tools Menu
├─ Open Angelscript workspace (VS Code)           // 打开外部 IDE
├─ Legacy Native Bind Generator (Debug Only)      // 旧调试入口
└─ Run Function Tests                             // 调试测试入口

Generic primitives
├─ UScriptEditorMenuExtension                     // 可扩展菜单壳
├─ UScriptEditorSubsystem                         // 可扩展生命周期壳
└─ UScriptableFactory                             // 低层创建 primitive

Result
└─ no shipped Actor/Component/Object/Interface scaffold workflow
```

[62] 当前 shipped 菜单说明 authoring first-run 入口仍未产品化：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 函数: RegisterToolsMenuEntries
// 位置: 696-745
// 说明: 当前内建菜单偏向工作区打开与旧调试入口，没有模板/脚手架动作
// ============================================================================
Section.AddMenuEntry
(
	"ASOpenCode",
	NSLOCTEXT("Angelscript", "OpenCode.Label", "Open Angelscript workspace (VS Code)"),
	NSLOCTEXT("Angelscript", "OpenCode.ToolTip", "Opens Visual Studio Code in this project's Angelscript workspace"),
	FSourceCodeNavigation::GetOpenSourceCodeIDEIcon(),
	Action
);

BindSection.AddMenuEntry
(
	"ASGenerateBindings",
	NSLOCTEXT("Angelscript", "GenerateBind.Label", "Legacy Native Bind Generator (Debug Only)"),
	NSLOCTEXT("Angelscript", "GenerateBind.ToolTip", "Legacy editor-side generator retained only for debugging old FunctionCallers output. The UHT-based AngelscriptUHTTool pipeline is the primary path."),
	FSourceCodeNavigation::GetOpenSourceCodeIDEIcon(),
	GenerateAction
);

Section.AddMenuEntry
(
	"Function Tests",
	NSLOCTEXT("Angelscript", "OpenCode.Label", "Run Function Tests"),
	...
);
```

[63] 当前扩展面更多是 primitive，不是 shipped authoring workflow：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/FunctionLibraries/ScriptableFactory.h
// 位置: 7-90, 8-33
// 说明: 插件已提供扩展壳与创建 primitive，但没有默认模板/脚手架 owner
// ============================================================================
UCLASS(NotBlueprintable, Abstract, Meta = (NoBlueprintsOfChildren))
class ANGELSCRIPTEDITOR_API UScriptEditorSubsystem : public UEditorSubsystem, public FTickableGameObject
{
	...
	UFUNCTION(BlueprintImplementableEvent)
	void BP_Initialize();
	...
}; // ★ 这是扩展宿主，不是现成的 authoring workflow

UCLASS(Meta = ())
class UScriptableFactory : public UFactory
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintImplementableEvent)
	UObject* CreateFromText(UClass* InClass, UObject* InParent, FName InName, int InFlags, UObject* Context, const FString& Buffer);

	UFUNCTION(BlueprintImplementableEvent)
	UObject* CreateFromBinary(UClass* InClass, UObject* InParent, FName InName, int InFlags, UObject* Context, const TArray<uint8>& Buffer);
}; // ★ 只有 primitive，没有内建 template/scaffold 选择与写盘策略
```

**差距描述**

- `没有实现`：不是。当前有文档导出、examples、learning tests、以及可扩展的 editor primitives。
- `能力缺失`：缺少 shipped 的 first-run scaffold/template 入口，用户第一次接触插件仍主要被引导去外部 IDE 和现有脚本目录。
- `实现质量差异`：当前 authoring 入口过于偏“工程师视角”。对插件交付来说，`Open workspace` 是必要动作，但不是足够的 onboarding artifact。

**参考方案**

- **UnLua** 在 Blueprint 上下文菜单中直接暴露 `Create Lua Template`，并沿父类链查找模板文件、只在目标文件不存在时写盘。
  - `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp:47-75,257-313`
- **UnrealCSharp** 提供 `Open NewDynamicClass` 菜单、专用 dialog 与 plugin template 文件链，面向新作者直接生成可编辑脚手架。
  - `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpPlayToolBar.cpp:50-102`
  - `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/NewClass/DynamicNewClassUtils.cpp:12-139`
  - `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/NewClass/SDynamicNewClassDialog.cpp:27-110`

[64] 参考项目已经把 scaffold 当成显式产品 surface，而不是留给用户自己猜目录结构：

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Toolbars/UnLuaEditorToolbar.cpp
// 函数: BuildToolbar / CreateLuaTemplate_Executed
// 位置: 47-75, 257-313
// 说明: UnLua 把模板生成放进 Blueprint 上下文菜单，并按父类链选择模板
// ============================================================================
MenuBuilder.AddMenuEntry(Commands.CreateLuaTemplate, NAME_None, LOCTEXT("CreateLuaTemplate", "Create Lua Template"));

const auto RelativePath = ModuleName.Replace(TEXT("."), TEXT("/"));
const auto FileName = FString::Printf(TEXT("%s%s.lua"), *GLuaSrcFullPath, *RelativePath);

if (FPaths::FileExists(FileName))
{
	UE_LOG(LogUnLua, Warning, TEXT("%s"), *FText::Format(LOCTEXT("FileAlreadyExists", "Lua file ({0}) is already existed!"), FText::FromString(TemplateName)).ToString());
	return; // ★ write-if-missing，保护用户修改
}

for (auto TemplateClass = Class; TemplateClass; TemplateClass = TemplateClass->GetSuperClass())
{
	...
	FFileHelper::LoadFileToString(Content, *FullFilePath);
	Content = Content.Replace(TEXT("TemplateName"), *TemplateName)
	                 .Replace(TEXT("ClassName"), *UnLua::IntelliSense::GetTypeName(Class));
	FFileHelper::SaveStringToFile(Content, *FileName, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	break;
}
```

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/ToolBar/UnrealCSharpPlayToolBar.cpp
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/NewClass/DynamicNewClassUtils.cpp
// 位置: 50-102, 12-139
// 说明: UnrealCSharp 提供独立脚手架入口与模板文件选择链
// ============================================================================
CommandList->MapAction(
	FUnrealCSharpEditorCommands::Get().OpenNewDynamicClass,
	FExecuteAction::CreateLambda([]
	{
		FDynamicNewClassUtils::OpenAddDynamicClassToProjectDialog(
			FUnrealCSharpFunctionLibrary::GetGameDirectory());
	}), FCanExecuteAction());

const auto TemplateFileName = FUnrealCSharpFunctionLibrary::GetPluginTemplateDynamicFileName(AncestorClass);
FFileHelper::LoadFileToString(OutContent, *TemplateFileName); // ★ 模板是正式 shipped asset
OutContent.ReplaceInline(...); // ★ 根据父类、命名空间、类名定制脚手架
```

**吸收建议**

- 先补**最小模板工作流**，不要一步做成复杂 IDE：首版只提供 `Actor / Component / Object / Interface` 四类 script template，入口可以先挂到 `Tools` 菜单和 `ContentBrowser` add-new。
- 模板策略应从第一天区分 `可重建产物` 与 `用户会手改的脚手架`。这条线更适合吸收 UnLua 的 `write-if-missing` 语义，而不是把脚手架做成每次生成都覆盖。
- 可以直接复用现有 `UScriptEditorPrompts` / `ScriptableFactory` / `UScriptEditorSubsystem` 作为实现底座，但对用户暴露的 surface 应是一个稳定的 `Create Script Template...` workflow，而不是要求项目方先理解这些 primitive。
- 在 `P10` 完成前，不要把模板生成和 `Bind API GAP` 绑定成强校验；第一阶段只生成保守模板，并显式标记当前不支持的 family（尤其 interface value/container）仍是 roadmap 项。

**优先级**

- `P2`
- **理由**：它不阻塞 `UInterface/FInterfaceProperty` 主线，也不比 delivery baseline 更靠前；但一旦 `P10` 最小闭环稳定，onboarding 入口就是下一阶段最直接的外部体验增量。

### 值得吸收的设计模式（增量）

- `Extension package shape is part of the product`：扩展插件是否可发现、脚本根在哪里、bind manifest 放哪、打包时谁负责 staging，不应再由 `CanContainContent` 和路径拼接隐式推导。
- `If a data source exists, authoring should live there too`：一旦脚本资产已经进入 `ContentBrowser` 数据层，创建、打开、复制引用、路径回桥至少要共享同一个 owner。
- `Starter artifacts are not docs`：文档、examples、tests 只能证明“系统可学”；模板/脚手架才能证明“系统可立即上手”。两者不是一回事。

### 改进路线建议（增量）

1. `P1`：在不碰 `P10` 关键路径的前提下，先引入 `ExtensionPackageDescriptor + BindingArtifactLocator`，统一 `ScriptRoots / Binds.Cache / BindModules.Cache / PrecompiledScript*.Cache` 的 root authority，并输出 report-only delivery diagnostics。
2. `P1`：把 `BindModules.Cache` 的 project/plugin 双根先做成 dual-read + explicit logging，避免 standalone plugin 交付时继续靠“碰巧路径对上”工作。
3. `P2`：新增 `FAngelscriptAssetWorkflowService`，让 `ContentBrowserDataSource` 与 `ShowCreateBlueprintPopup()` / `ShowAssetListPopup()` 共用 create/open/reference/path resolver；首批只补 `EditItem`、`AppendItemReference`、`Legacy_TryConvert*()`。
4. `P2`：补一个最小 `Create Script Template...` workflow，首版模板只覆盖 `Actor / Component / Object / Interface`，并采用 `write-if-missing` 语义；等 `P10` 稳定后，再让模板感知更完整的 type family 支持面。

---

## 深化分析 (2026-04-09 07:24:17)

### 本轮新增关键发现

- 同一个 `BlueprintCallable` interface owner，目前会先经过 `ShouldSkipBlueprintCallableFunction()` 的“继续处理”，再在 `EvaluateReflectiveFallbackEligibility()` 被拒绝，最后在 UHT `CollectEntries()` 里被直接写成 `Stub`。这不是单点 bug，而是 **callable exposure policy 没有单一 authority**。
- `FInterfaceProperty` 的缺口已经不只在 live `TypeSystem`；`Binds.Cache` 的序列化模型仍只有 `Declaration + bGeneratedHandle + bGeneratedUnresolvedObject`，cook/runtime replay 根本没有位置保存 `InterfaceClass`、`dual-slot value bridge` 或 nested family shape。也就是说，即使 live lane 补了 adapter，cooked lane 仍会断。
- `GeneratedFunctionTable` 的当前自动化主要校验 `header/count/代表 direct 行`，但不校验 `Entries.csv`、`SkippedEntries.csv` 与 family reason 的一致性。generator 现在会把 interface owner 直接写成 `Stub`，exporter 却只在 `TryBuild()` 失败时记 skipped；`Bind API GAP` 指标因此仍可能“看起来有 reason，实际上漏了整类 stub”。

### 差距矩阵（增量）

| 维度 | 本轮判断 | 差距等级 | 新增证据 |
| --- | --- | --- | --- |
| `D6` 代码生成与 IDE 支持 | interface callable 的准入规则被 runtime / fallback / UHT 三处分裂 | `能力缺失` | `AngelscriptBinds.cpp`、`BlueprintCallableReflectiveFallback.cpp`、`AngelscriptFunctionTableCodeGenerator.cs` |
| `D2` 反射绑定机制 | `Binds.Cache` 还不能表达 `FInterfaceProperty` / `TScriptInterface<>` 的结构化 shape | `能力缺失` | `AngelscriptBindDatabase.h`、`Bind_BlueprintType.cpp`、`AngelscriptType.cpp` |
| `D9` 测试基础设施 | 生成产物测试仍只保 shape，不保 interface family parity | `能力缺失` | `AngelscriptGeneratedFunctionTableTests.cpp`、`AS_FunctionTable_Entries.csv`、`AS_FunctionTable_SkippedEntries.csv` |

### [D6] 代码生成与 IDE 支持：interface callable 准入规则仍是三套 authority

**当前状态**

这一轮不重复前文已经分析过的 `name-only collapse`，只补一个前文还没落到源码 owner 的断层：**同一类 `BlueprintCallable` interface function，在 runtime skip、reflective fallback、UHT codegen 三条 lane 上各自使用不同的 eligibility 规则**。

```
[Current] Interface Callable Eligibility
BlueprintCallable UFunction
├─ Runtime Skip Gate -> ShouldSkipBlueprintCallableFunction()    // 不拒绝 interface owner
├─ Reflective Fallback -> RejectedInterfaceClass                 // 单独拒绝 interface class
└─ UHT CodeGen -> Interface => ERASE_NO_FUNCTION()               // 直接写 stub
```

[65] runtime 第一层 skip gate 只过滤 metadata 和少数函数名特例，不理解 interface owner：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp
// 函数: FAngelscriptBinds::ShouldSkipBlueprintCallableFunction
// 位置: 83-117
// 说明: skip gate 不把 interface owner 当成独立 family；只处理 metadata 和少数白名单/黑名单
// ============================================================================
bool FAngelscriptBinds::ShouldSkipBlueprintCallableFunction(const UFunction* Function)
{
	static const FName NAME_Function_NotInAngelscript(TEXT("NotInAngelscript"));
	static const FName NAME_Function_BlueprintInternalUseOnly(TEXT("BlueprintInternalUseOnly"));
	static const FName NAME_Function_UsableInAngelscript(TEXT("UsableInAngelscript"));

	...

	if (const UClass* OwningClass = Function->GetOuterUClass())
	{
		if (OwningClass == UActorComponent::StaticClass() && Function->GetFName() == FName(TEXT("GetOwner")))
		{
			return true; // ★ 这里只有特定函数特判，没有 interface owner policy
		}
	}

	return false;
}
```

[66] reflective fallback 在第二层才单独拒绝 `CLASS_Interface`：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp
// 函数: EvaluateReflectiveFallbackEligibility
// 位置: 254-282
// 说明: interface owner 不是在统一 policy 里判掉，而是在 reflective fallback lane 里单独拒绝
// ============================================================================
EAngelscriptReflectiveFallbackEligibility EvaluateReflectiveFallbackEligibility(const UFunction* Function)
{
	...
	const UClass* OwningClass = Function->GetOuterUClass();
	if (OwningClass == nullptr)
	{
		return EAngelscriptReflectiveFallbackEligibility::RejectedMissingOwningClass;
	}

	if (OwningClass->HasAnyClassFlags(CLASS_Interface))
	{
		return EAngelscriptReflectiveFallbackEligibility::RejectedInterfaceClass; // ★ 第二条 lane 才看见 interface
	}

	if (Function->HasMetaData(NAME_BlueprintCallableReflectiveFallback_CustomThunk))
	{
		return EAngelscriptReflectiveFallbackEligibility::RejectedCustomThunk;
	}

	return EAngelscriptReflectiveFallbackEligibility::Eligible;
}
```

[67] UHT codegen 又在第三层直接把 interface/native interface owner 写成 `ERASE_NO_FUNCTION()`：

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs
// 函数: CollectEntries
// 位置: 449-479
// 说明: UHT lane 不共享 runtime eligibility；遇到 interface owner 直接生成 stub
// ============================================================================
private static void CollectEntries(IUhtExportFactory factory, UhtType type, SortedSet<string> includes, List<AngelscriptGeneratedFunctionEntry> entries)
{
	...
	if (child is UhtFunction function && ShouldGenerate(classObj, function))
	{
		string eraseMacro;
		if (classObj.ClassType == UhtClassType.Interface || classObj.ClassType == UhtClassType.NativeInterface)
		{
			eraseMacro = "ERASE_NO_FUNCTION()"; // ★ 第三条 lane 的判定结果
		}
		else if (AngelscriptFunctionSignatureBuilder.TryBuild(classObj, function, out AngelscriptFunctionSignature? signature, out string? _))
		{
			eraseMacro = signature!.BuildEraseMacro();
		}
		else
		{
			eraseMacro = "ERASE_NO_FUNCTION()";
		}

		entries.Add(new AngelscriptGeneratedFunctionEntry(classObj.SourceName, function.SourceName, eraseMacro));
	}
}
```

**差距描述**

- `没有实现`：不是。当前已经有 skip gate、reflective fallback 和 UHT function table 三套 callable 准入能力。
- `实现方式不同`：是，但更准确地说，是**同一条能力没有共享 policy**。interface owner 的结论分别散落在 `Core`、`Binds`、`UHTTool` 三处，且 reason vocabulary 不一致。
- `实现质量差异`：当前 `Bind API GAP` 账本回答不了“这是 family policy 禁止、还是 runtime 缺 owner、还是 header/signature 失败”。P10 若继续按 lane 局部补丁推进，结果很容易变成一条 lane 放开、一条 lane 继续 stub。

**参考方案**

- **UnrealCSharp** 值得吸收的不是 C# 语法本身，而是它把 `generic types + attribute families + interface generation` 提升成同一个 registry / generator authority。
  - `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Reflection/FReflectionRegistry.cpp:19-119`
  - `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicInterfaceGenerator.cpp:21-55`

[68] `FReflectionRegistry` 先收口泛型/属性族真相，再交给 generator 消费：

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Reflection/FReflectionRegistry.cpp
// 函数: FReflectionRegistry::Initialize
// 位置: 19-119
// 说明: interface/type/function 相关真相先进入 registry，再由其它 lane 消费
// ============================================================================
void FReflectionRegistry::Initialize()
{
	...
	TScriptInterfaceClass = GetClass(
		COMBINE_NAMESPACE(NAMESPACE_ROOT, NAMESPACE_CORE_UOBJECT), GENERIC_T_SCRIPT_INTERFACE);

	...
	OverrideAttributeClass = GetClass(
		COMBINE_NAMESPACE(NAMESPACE_ROOT, NAMESPACE_CORE_UOBJECT), CLASS_OVERRIDE_ATTRIBUTE);

	UFunctionAttributeClass = GetClass(
		COMBINE_NAMESPACE(NAMESPACE_ROOT, NAMESPACE_DYNAMIC), CLASS_U_FUNCTION_ATTRIBUTE);

	UInterfaceAttributeClass = GetClass(
		COMBINE_NAMESPACE(NAMESPACE_ROOT, NAMESPACE_DYNAMIC), CLASS_U_INTERFACE_ATTRIBUTE); // ★ interface/function 进入同一 authority
}
```

[69] interface generator 不再自己猜 policy，而是直接消费 registry + dependency graph：

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicInterfaceGenerator.cpp
// 函数: FDynamicInterfaceGenerator::Generator
// 位置: 21-55
// 说明: generator 直接从 registry 取 UInterface attribute，并显式编码依赖图
// ============================================================================
void FDynamicInterfaceGenerator::Generator()
{
	FDynamicGeneratorCore::Generator(FReflectionRegistry::Get().GetUInterfaceAttributeClass(),
	                                 [](FClassReflection* InClassReflection)
	                                 {
		                                 ...
		                                 if (const auto Parent = InClassReflection->GetParent())
		                                 {
			                                 if (Parent->HasAttribute(
				                                 FReflectionRegistry::Get().GetUInterfaceAttributeClass()))
			                                 {
				                                 Node.Dependency(FDynamicDependencyGraph::FDependency{
					                                 Parent->GetName(), false
				                                 }); // ★ 依赖关系也来自同一 policy 面
			                                 }
		                                 }

		                                 FDynamicGeneratorCore::GeneratorFunction(InClassReflection, Node);
		                                 FDynamicGeneratorCore::AddNode(Node);
	                                 });
}
```

**吸收建议**

- 不要继续在 `ShouldSkipBlueprintCallableFunction()`、`EvaluateReflectiveFallbackEligibility()`、`CollectEntries()` 三处各补一小块。应先新增一个插件内共享的 `FAngelscriptCallableExposurePolicy` 或等价 manifest，字段至少包含 `ExposureMode(Direct/Reflective/Stub/Skip)`、`ReasonCode`、`OwnerFamily`、`BlockingFamilies`。
- 第一阶段只做 **report-only**：UHT 先产出 manifest；runtime skip/fallback 继续跑旧逻辑，但把实际结果与 manifest 做 diff 日志，不立刻改行为。
- 第二阶段再让 `ShouldSkipBlueprintCallableFunction()` 和 reflective fallback 优先消费同一 policy；`CollectEntries()` 只负责把 policy 投影成 `Direct/Stub`，不再重复做 family 判定。
- 这条线完全可以插件内落地，不需要引擎补丁，也不依赖 AngelScript `2.38` 升级；它优先解决的是 **P10 rollout governance**，不是 VM 能力本身。

**优先级**

- `P0`
- **理由**：`P10 UInterface` 和 `Bind API GAP` 当前最大的风险不是“少一两个 helper”，而是三条 lane 没有共享 policy，任何单点放开都可能把产物、runtime 和指标面拉裂。

### [D2] 反射绑定机制：`Binds.Cache` 仍无法表达 `FInterfaceProperty` / `TScriptInterface<>` 的结构化 shape

**当前状态**

前文已经多次确认 live `TypeSystem` 缺 `FInterfaceProperty` owner；这一轮新增的是 **cook/runtime bind replay 视角**。当前系统即便 live lane 补上 `FInterfaceProperty` adapter，`Binds.Cache` 也还装不下它，因为 cache 只知道 declaration string 和两个 object-like 特判布尔位。

```
[Current] Interface Property In Cooked Bind Path
FProperty
├─ TypeFinder
│  ├─ FObjectProperty / FWeakObjectProperty / FClassProperty   // 已有 owner
│  └─ no FInterfaceProperty branch                             // 没有 interface family owner
├─ Binds.Cache
│  ├─ Declaration                                              // 只有字符串
│  ├─ bGeneratedHandle                                         // object handle 特判
│  └─ bGeneratedUnresolvedObject                               // unresolved 特判
└─ Replay
   ├─ handle/unresolved -> dedicated helper                    // 两个硬编码分支
   └─ else -> Get/SetValueFromProperty                         // 无 dual-slot interface bridge
```

[70] `FAngelscriptPropertyBind` 现在没有任何 shape 字段，只保 declaration 和两个 object-like flag：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h
// 结构: FAngelscriptPropertyBind
// 位置: 9-33
// 说明: cache 侧没有 InterfaceClass、InnerShape、ValueBridge 等字段
// ============================================================================
struct FAngelscriptPropertyBind
{
	FString Declaration;
	FString UnrealPath;
	FString GeneratedName;
	bool bCanWrite = false;
	bool bCanRead = false;
	bool bCanEdit = false;
	bool bGeneratedGetter = false;
	bool bGeneratedSetter = false;
	bool bGeneratedHandle = false;
	bool bGeneratedUnresolvedObject = false;

	inline friend FArchive& operator<<(FArchive& Archive, FAngelscriptPropertyBind& Data)
	{
		Archive << Data.Declaration;
		...
		Archive << Data.bGeneratedHandle;
		Archive << Data.bGeneratedUnresolvedObject; // ★ 到此为止，没有 interface family 的持久化空间
		return Archive;
	}
};
```

[71] property `TypeFinder` 只处理 object / weak object / subclass 等 family，没有 `FInterfaceProperty` 分支：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp
// 函数: BindUClassLookup 中注册的 TypeFinder
// 位置: 2438-2519
// 说明: property family dispatch 只覆盖 object/weak/class/subclass，interface family 没有 owner
// ============================================================================
FAngelscriptType::RegisterTypeFinder([=](FProperty* Property, FAngelscriptTypeUsage& Usage) -> bool
{
	const FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property);
	if (ObjectProperty == nullptr)
	{
		const FWeakObjectProperty* WeakObjectProperty = CastField<FWeakObjectProperty>(Property);
		if (WeakObjectProperty != nullptr)
		{
			...
			Usage.Type = WeakObjectPtrType;
			return true;
		}

		return false; // ★ 没有 CastField<FInterfaceProperty>
	}

	...

	if (ClassProperty != nullptr && (ClassProperty->PropertyFlags & CPF_UObjectWrapper) != 0)
	{
		Usage.Type = SubclassOfType;
		return true;
	}

	Usage = FAngelscriptTypeUsage::FromClass(ObjectProperty->PropertyClass);
	return Usage.IsValid();
});
```

[72] DB replay 时也只认识 `handle/unresolved/else` 三类，没有 interface dual-slot：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp
// 函数: Bind_BlueprintType_Declarations
// 位置: 794-867, 1135-1267
// 说明: cache replay 只按 declaration + 两个布尔位重放 getter/setter
// ============================================================================
if (DBProp.bGeneratedGetter)
{
	FString Decl = FString::Printf(TEXT("%s Get%s() const"), *PropertyType, *PropertyName);

	if (DBProp.bGeneratedHandle)
	{
		Binds.Method(Decl, FUNC_TRIVIAL(FAngelscriptBindHelpers::GetObjectFromProperty), ...);
	}
	else if (DBProp.bGeneratedUnresolvedObject)
	{
		Binds.Method(Decl, FUNC_TRIVIAL(FAngelscriptBindHelpers::GetUnresolvedObjectFromProperty), ...);
	}
	else
	{
		Binds.Method(Decl, FUNC_TRIVIAL(FAngelscriptBindHelpers::GetValueFromProperty), ...); // ★ 其它 family 全压成 generic value
	}
}

...

if (!Property->HasAnyPropertyFlags(CPF_EditorOnly) && (DBProp.bGeneratedGetter || DBProp.bGeneratedSetter))
{
	DBProp.Declaration = AccessorType;
	DBProp.GeneratedName = PropertyName;
	DBProperties.Add(DBProp); // ★ 写入 cache 的仍只有 declaration + flag
}
else
{
	FString Declaration = FString::Printf(TEXT("%s %s"), *PropertyType, *PropertyName);
	Binds.Property(Declaration, Property->GetOffset_ForUFunction(), Params);
	DBProp.Declaration = Declaration; // ★ simple property 只留 declaration string
}
```

**差距描述**

- `没有实现`：从 cooked/cache 角度看，`FInterfaceProperty` 的结构化持久化确实还没有实现。
- `实现方式不同`：当前不是“选择了另一种等价表示”，而是 **根本没有一个能表达 interface dual-slot 的 cache schema**。
- `实现质量差异`：当前 object-like family 已经靠 `bGeneratedHandle` / `bGeneratedUnresolvedObject` 跑通两条硬编码分支；继续给 `FInterfaceProperty` 加第三个/第四个布尔位会把 cache contract 进一步锁死在 copy-expand 特判里。

**参考方案**

- **puerts** 把 property family dispatch 明确收口到 `PropertyTranslatorCreator`，其中 `FInterfaceProperty` 是正式分支，不是 declaration-string fallback。
  - `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:598-632`
  - `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:1225-1315`
- **UnrealCSharp** 更进一步，把 `FInterfaceProperty -> TScriptInterface<>` 的结构化 type bridge 和 dual-slot set/get 单独做成 descriptor。
  - `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:266-269,415-430`
  - `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/ObjectProperty/FInterfacePropertyDescriptor.cpp:4-55`
  - `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Domain/InternalCall/FRegisterScriptInterface.cpp:11-63`

[73] puerts 的做法是把 `FInterfaceProperty` 放进统一 property-family creator：

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp
// 位置: 598-632, 1225-1315
// 说明: interface property 是正式 translator family；creator 统一按 FProperty class 分发
// ============================================================================
class FInterfacePropertyTranslator : public FPropertyWithDestructorReflection
{
public:
    v8::Local<v8::Value> UEToJs(...) const override
    {
        const FScriptInterface& Interface = InterfaceProperty->GetPropertyValue(ValuePtr);
        UObject* Object = Interface.GetObject();
        ...
        return FV8Utils::IsolateData<IObjectMapper>(Isolate)->FindOrAdd(Isolate, Context, Object->GetClass(), Object);
    }

    bool JsToUE(...) const override
    {
        ...
        FScriptInterface* Interface = reinterpret_cast<FScriptInterface*>(ValuePtr);
        Interface->SetObject(Object);
        Interface->SetInterface(Object ? Object->GetInterfaceAddress(InterfaceProperty->InterfaceClass) : nullptr); // ★ 双槽 bridge
        return true;
    }
};

...
else if (InProperty->IsA<InterfacePropertyMacro>())
{
    return Creator<FInterfacePropertyTranslator>::Do(InProperty, IgnoreOut, Ptr); // ★ interface 进入统一 creator
}
```

[74] UnrealCSharp 则把 `FInterfaceProperty` 的类型和 value bridge 都做成结构化 owner：

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/ObjectProperty/FInterfacePropertyDescriptor.cpp
// 位置: 266-269, 415-430; 4-55
// 说明: interface property 不走字符串 declaration，而是显式映射到 generic TScriptInterface<> + descriptor
// ============================================================================
if (const auto InterfaceProperty = CastField<FInterfaceProperty>(InProperty))
{
	return GetClass(InterfaceProperty); // ★ family 入口
}

FClassReflection* FTypeBridge::GetClass(const FInterfaceProperty* InProperty)
{
	...
	const auto FoundGenericClass = FReflectionRegistry::Get().GetTScriptInterfaceClass();
	const auto FoundClass = FReflectionRegistry::Get().GetClass(InProperty->InterfaceClass);
	return MakeGenericTypeInstance(FoundGenericClass, FoundClass); // ★ type shape = TScriptInterface<InterfaceClass>
}

void FInterfacePropertyDescriptor::Set(void* Src, void* Dest) const
{
	...
	const auto Interface = static_cast<FScriptInterface*>(Dest);
	const auto Object = SrcMulti->GetObject();
	Interface->SetObject(Object);
	Interface->SetInterface(Object ? Object->GetInterfaceAddress(Property->InterfaceClass) : nullptr); // ★ value shape 也是双槽
}
```

**吸收建议**

- 第一阶段先不要追求“一次补齐所有 wrapper/container/interface 组合”；应先给 `Binds.Cache` 增加一个最小 `PropertyShape` / `ValueBridgeKind`，至少包含 `Family`、`ObjectClassPath`、`InterfaceClassPath`、`Inner/Key/Value`、`AccessorPolicy`。
- `Declaration` 继续保留，但降级成 display / legacy fallback，不再承载 family 真相。这样不需要推翻现有 `.Cache` 消费链，只是新增双写和新 reader。
- 对 `FInterfaceProperty`，首版只做 `non-container property + generated getter/setter + cooked replay` 三个面；暂不同时打开 `TArray<TScriptInterface<>>`、`TMap<...>` 等嵌套场景，避免在 `2.33.0 WIP` 下把 type family rollout 做成大爆炸。
- value bridge 需要显式建模成 `ObjectOnly / UnresolvedObject / ScriptInterfaceDualSlot`，不要再继续扩 `bGeneratedHandle` 风格布尔位。

**优先级**

- `P1`
- **理由**：它直接服务 `P10 FInterfaceProperty`，但比 “先统一 callable exposure policy” 稍后一步。先把 policy/manifest 收口，再补 cache schema，能避免 live lane 与 cooked lane 再次各做一套 interface 语义。

### [D9] 测试基础设施：`GeneratedFunctionTable` 仍缺 interface family parity 断言

**当前状态**

前文已经指出现有测试更偏统计与产物存在性；这一轮补的是更具体的一处断层：**generator、exporter 和 tests 三者现在并不保证 interface family 的账本一致**。

```
[Current] UHT Artifact Verification
UHT CodeGenerator
├─ Entries.csv -> Direct / Stub                               // interface owner 直接写 stub
UHT Exporter
├─ SkippedEntries.csv -> TryBuild failure only                // 不等价于全部 stub
Tests
└─ header / count / representative direct row                 // 不断言 interface parity
```

[75] generator 会把 interface/native interface owner 直接写成 `Stub`，但 exporter 只在 `TryBuild()` 失败时记 skipped：

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs
// 位置: 449-479; 65-88
// 说明: generator 与 exporter 的“为什么是 stub”不是同一套口径
// ============================================================================
if (classObj.ClassType == UhtClassType.Interface || classObj.ClassType == UhtClassType.NativeInterface)
{
	eraseMacro = "ERASE_NO_FUNCTION()"; // ★ generator：interface owner 直接 stub
}
else if (AngelscriptFunctionSignatureBuilder.TryBuild(...))
{
	eraseMacro = signature!.BuildEraseMacro();
}
else
{
	eraseMacro = "ERASE_NO_FUNCTION()";
}

...

if (AngelscriptFunctionSignatureBuilder.TryBuild(classObj, function, out AngelscriptFunctionSignature? signature, out string? failureReason))
{
	_ = signature!.BuildEraseMacro();
	reconstructedCount++;
}
else
{
	skippedCount++;
	skippedEntries.Add(new AngelscriptSkippedFunctionEntry(
		moduleName,
		classObj.SourceName,
		function.SourceName,
		string.IsNullOrEmpty(failureReason) ? "unknown" : failureReason)); // ★ exporter：只记 TryBuild 失败
}
```

[76] 当前自动化只校验 CSV 头、总数、存在非空 reason，以及一个代表性 direct 行：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp
// 函数: FAngelscriptGeneratedFunctionTableCsvOutputTest / SkippedCsvOutputTest / SkippedReasonSummaryCsvOutputTest
// 位置: 594-749
// 说明: 现有测试验证的是 shape 和 aggregate，不是 family parity
// ============================================================================
TestEqual(TEXT("Generated function table csv test should write the expected entry csv header"),
	EntryLines[0], TEXT("ModuleName,EditorOnly,ClassName,FunctionName,EntryKind,EraseMacro,ShardIndex"));

...

if (EntryLine.Contains(TEXT(",RunBehaviorTree,")))
{
	bFoundRunBehaviorTreeCsv = true;
	TestTrue(TEXT("Generated function table csv test should classify RunBehaviorTree as a direct entry"),
		EntryLine.Contains(TEXT(",Direct,"))); // ★ 代表性 happy-path direct row
}

...

TestEqual(TEXT("Generated function table skipped csv test should write the expected skipped csv header"),
	SkippedLines[0], TEXT("ModuleName,ClassName,FunctionName,FailureReason"));

...

TestTrue(TEXT("Generated function table skipped csv rows should include non-empty failure reasons"),
	bFoundFailureReason); // ★ 只要求“有 reason”，不要求“哪类 stub 必须有哪类 reason”
```

[77] 产物上已经能看到 mismatch：`Entries.csv` 里有大量 interface owner stub，但 `SkippedEntries.csv` 只记录了少量 support library 行。以下是当前工作区的真实产物：

```text
# ============================================================================
# 文件: Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv
# 位置: 2783-2788, 2954-2964, 3961-3982
# 文件: Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_SkippedEntries.csv
# 位置: 765-768
# 说明: interface owner 已在 entries 里被 hard-stub，但 skipped 账本只出现了 support library 的少数行
# ============================================================================
Engine,false,ULevelInstanceInterface,GetLoadedLevel,Stub,ERASE_NO_FUNCTION(),11
Engine,false,ULevelInstanceInterface,UnloadLevelInstance,Stub,ERASE_NO_FUNCTION(),11
Engine,false,UNavMovementInterface,RequestDirectMove,Stub,ERASE_NO_FUNCTION(),12
Engine,false,UTypedElementWorldInterface,GetWorldTransform,Stub,ERASE_NO_FUNCTION(),16

Engine,UCameraLensEffectInterfaceClassSupportLibrary,GetInterfaceClass,unexported-symbol
Engine,UCameraLensEffectInterfaceClassSupportLibrary,IsInterfaceClassValid,unexported-symbol
Engine,UCameraLensEffectInterfaceClassSupportLibrary,IsInterfaceValid,unexported-symbol
Engine,UCameraLensEffectInterfaceClassSupportLibrary,SetInterfaceClass,unexported-symbol
```

补充核对结果：本地对生成产物执行 `rg -c` 后，`ULevelInstanceInterface`、`UNavMovementInterface`、`UTypedElementWorldInterface`、`UInterface_AssetUserData` 在 `Entries.csv` 中分别有 `6/11/22/3` 条 stub 行，但在 `SkippedEntries.csv` 中都是 `0` 命中。这说明 **“stub entry” 与 “skipped reason” 目前不是同一账本**。

**差距描述**

- `没有实现`：是。当前没有一条测试明确断言“所有 interface-owner stub 都必须有可 join 的 reason / policy code”。
- `实现方式不同`：现有测试更像产物形状检查；但 `Bind API GAP` 需要的是 family-aware semantic check，这不是同一层次。
- `实现质量差异`：当 P10 开始灰度放开 interface family 时，测试现在无法告诉我们“这次多出来的 stub 是 policy 预期、reason 漏写，还是某条 lane 又退回 hard-stub”。

**参考方案**

- **UnLuaTestSuite** 的可借鉴点不是 Lua 本身，而是它把问题回归写成 issue-scoped named tests，直接固定场景、断言和回放入口。
  - `Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Tests/Issue401Test.cpp:21-42`

[78] UnLua 的 issue 测试模式很适合吸收到 `GeneratedFunctionTable` / `P10` 回归里：

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Tests/Issue401Test.cpp
// 位置: 21-42
// 说明: 把具体语义 bug 写成命名化回归，而不是只靠 aggregate 指标
// ============================================================================
BEGIN_TESTSUITE(FIssue401Test, TEXT("UnLua.Regression.Issue401 LUA覆写导致数组传参错误"))

bool FIssue401Test::RunTest(const FString& Parameters)
{
	const auto MapName = TEXT("/UnLuaTestSuite/Tests/Regression/Issue401/Issue401");
	ADD_LATENT_AUTOMATION_COMMAND(FOpenMapLatentCommand(MapName))
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this] {
		...
		TestEqual(TEXT("Result1"), Result1, 2);
		TestEqual(TEXT("Result2"), Result2, 2); // ★ 问题被写成精确语义断言
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

END_TESTSUITE(FIssue401Test)
```

**吸收建议**

- 增加一个 `GeneratedFunctionTable.InterfaceParity` 或等价测试，直接同时读取 `AS_FunctionTable_Entries.csv`、`AS_FunctionTable_SkippedEntries.csv` 和未来的 `CallableExposurePolicy/Manifest`，断言：
  - interface/native interface owner 的 `Stub` 必须带稳定 `ReasonCode`；
  - `Entries.csv` 与 `SkippedEntries.csv` 至少能通过 `ClassName + FunctionName + ReasonCode` 或 manifest key 做 join；
  - 当 `P10` 打开某个 family 后，对应 entry 必须从 `Stub` 迁到预期 mode，而不是继续落在“有/无 skipped row 都行”的灰区。
- 除了 aggregate test，再补 2-3 个 issue-scoped named regression：
  - `GeneratedFunctionTable.InterfaceOwnerStubReasonParity`
  - `GeneratedFunctionTable.InterfaceSupportLibraryAndOwnerConsistent`
  - `GeneratedFunctionTable.InterfaceRolloutPolicyDiff`
- 这些测试不需要引擎修改；它们消费现有 UHT 产物即可，适合和 `Bind API GAP` 账本一起推进。

**优先级**

- `P1`
- **理由**：它不先于 policy/shape 设计，但必须跟着它们一起落地，否则 `P10` 每推进一步都会继续靠人工读 CSV 判定是否回归。

### 值得吸收的设计模式（增量）

- `Policy before projection`：先有共享的 `ExposurePolicy / PropertyShape`，再把它投影成 runtime bind、UHT entry、editor diagnostics。不要再让每条 lane 各自推导一次。
- `Family-specific value bridge beats declaration string`：`interface` 这类 family 的关键不是“打印成什么字符串”，而是 `type shape + dual-slot value bridge` 是否被结构化建模。
- `Artifact parity is a test target`：`Entries.csv`、`SkippedEntries.csv`、future manifest 之间的一致性本身就该是正式回归对象，不应只测“文件存在”和“总数对得上”。

### 改进路线建议（增量）

1. `P0`：新增插件内共享的 `AngelscriptCallableExposurePolicy`（或等价 manifest），先做 report-only；UHT 生成、runtime skip/fallback、`Bind API GAP` 工具全部输出同一套 `ReasonCode/ExposureMode`。
2. `P1`：扩 `Binds.Cache` 为 `PropertyShape + ValueBridgeKind` 双写模型，首批只覆盖 `FInterfaceProperty` 的 non-container property + generated accessor + cooked replay。
3. `P1`：补 `GeneratedFunctionTable.InterfaceParity` 与 2-3 个 issue-scoped named regression，强制校验 `Entries/Skipped/Manifest` 的 join key 和 reason parity。
4. `P2`：等 `P10` 最小闭环稳定后，再把 editor surface 和 diagnostics 面接上 family-level status，让 `BlueprintImpact` / tools / gap dashboard 看到“interface family 当前在哪些 lane 已有 owner”。 

---

## 深化分析 (2026-04-09 07:34:04)

### 本轮新增关键发现

- `TypeSystem` 入口今天仍只承认 `ScriptObjectType`，没有与 `ScriptStructType` / `ScriptDelegateType` 对等的 `ScriptInterfaceType`。这意味着 script-defined interface 在 `FromTypeId()` / `FromClass()` 入口就已经失去 family 身份，只能靠 `userData + CLASS_Interface` 在后置阶段补猜。
- `FAngelscriptTypeUsage::SubTypes[]` 现在同时承担 `Element`、`Key`、`Value`、`MetaClass`、`ObjectClass` 等多种语义角色，但结构里没有 role tag。`TScriptInterface<>` 一旦进入主链，就会继续逼出新的“下标约定”，而不是得到可复用的 type shape。
- `P10` 真正缺的不是再补一个 `ShouldSkip...` 特判，而是给每个 type family 一份统一 `SurfaceSupportProfile`。同一个 family 目前在 property、script `UFUNCTION`、native direct bind、reflective fallback、UHT 五条 lane 上各自判定，无法做渐进 rollout。

### 差距矩阵（增量）

| 议题 | 维度 | 新增判定 | 差距类型 | 优先级 |
| --- | --- | --- | --- | --- |
| script-defined interface family | D2 | `FromTypeId()/FromClass()` 入口没有显式 `ScriptInterfaceType` | 能力缺失 | P0 |
| role-tagged type shape | D2 / D6 | `SubTypes[]` 仍是隐式下标协议，无法稳定表达 `InterfaceClass` 与 dual-slot | 实现质量差异 | P1 |
| surface support profile | D2 / D6 / D9 | property / callable / UHT 四条 lane 仍无共享支持状态与原因码 | 能力缺失 | P0 |

### [D2] 反射绑定机制补充：script-defined interface 在 `TypeSystem` 入口仍被压成 `ScriptObjectType`

**当前状态**

`FAngelscriptType` 数据库今天只保留了 `ScriptObjectType / ScriptStructType / ScriptDelegateType / ScriptMulticastDelegateType / ScriptEnumType` 五个 script family，见 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:46-58,597-603`。`FAngelscriptTypeUsage::FromClass()` 只要碰到 `UASClass` 就统一回落到 `GetScriptObject()`，见 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:291-305`；`FromTypeId()` 只要碰到非值的 `asOBJ_SCRIPT_OBJECT` 也统一回落到 `GetScriptObject()`，见 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:367-406`。真正的 interface 判断又被推迟到 `GetClass()` 与 `CanCastScriptObjectToUnrealInterface()` 的 `userData + CLASS_Interface` 路径，见 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:210-231` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:112-137`。

这不是“另一种等价表示”，而是 **family 在入口已经丢失，只能后置恢复**。

```
[Current] Script Reference Classification
asITypeInfo / UASClass
├─ FromTypeId() / FromClass()                         // 入口分类
│  └─ non-value script object -> ScriptObjectType    // interface 在这里被折叠
├─ GetClass()                                        // 晚期取回 UClass*
│  └─ ScriptClass->GetUserData()
└─ QuickScriptInterfaceCast                          // 再次猜测 interface 语义
   └─ TargetType->GetUserData() + CLASS_Interface
```

[79] 当前 `TypeSystem` 入口没有对等的 `ScriptInterfaceType`，interface 只能在后置阶段再被识别：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h
// 位置: 46-58, 597-603
// 说明: script family 数据库里没有 `ScriptInterfaceType` 这个一等槽位
// ============================================================================
static TSharedPtr<FAngelscriptType>& GetScriptObject();
static TSharedPtr<FAngelscriptType>& GetScriptEnum();
static TSharedPtr<FAngelscriptType>& GetScriptStruct();
static TSharedPtr<FAngelscriptType>& GetScriptDelegate();
static TSharedPtr<FAngelscriptType>& GetScriptMulticastDelegate();

TSharedPtr<FAngelscriptType> ScriptObjectType;
TSharedPtr<FAngelscriptType> ScriptEnumType;
TSharedPtr<FAngelscriptType> ScriptStructType;
TSharedPtr<FAngelscriptType> ScriptDelegateType;
TSharedPtr<FAngelscriptType> ScriptMulticastDelegateType;

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp
// 位置: 291-305, 367-406
// 说明: `UASClass` 和非值 script object 都统一压回 `GetScriptObject()`
// ============================================================================
if (ASClass != nullptr && ASClass->ScriptTypePtr != nullptr)
{
	Usage.Type = FAngelscriptType::GetScriptObject(); // ★ script-defined class/interface 共用同一 family
	Usage.ScriptClass = (asITypeInfo*)ASClass->ScriptTypePtr;
}

if (ScriptType->GetFlags() & asOBJ_SCRIPT_OBJECT)
{
	if (ScriptType->GetFlags() & asOBJ_VALUE)
	{
		...
	}
	else
	{
		Usage.Type = FAngelscriptType::GetScriptObject(); // ★ 非值 script object 统一折叠
	}

	Usage.ScriptClass = ScriptType;
	return Usage;
}

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 112-137
// 说明: interface 语义要等到 quick cast 时再靠 `userData + CLASS_Interface` 重新猜出来
// ============================================================================
UClass* TargetClass = reinterpret_cast<UClass*>(TargetType->GetUserData());
if (TargetClass == nullptr || !TargetClass->HasAnyClassFlags(CLASS_Interface))
{
	return false;
}

UObject* Object = reinterpret_cast<UObject*>(ObjectPtr);
UClass* ObjectClass = Object != nullptr ? Object->GetClass() : nullptr;
const bool bImplementsInterface = ObjectClass != nullptr && ObjectClass->ImplementsInterface(TargetClass);
```

**差距描述**

- `没有实现`：没有显式 `ScriptInterfaceType` / `ScriptReferenceKind`。
- `实现方式不同`：当前不是像参考实现那样“入口先分 family，后续共享 descriptor”，而是入口先折叠，后续再猜。
- `实现质量差异`：这种后置恢复会把 `P10` 的 `FInterfaceProperty`、reflective fallback、UHT sidecar 都逼成旁路，因为它们拿不到稳定的 family authority。

**参考方案**

- `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:388-430` 直接把 `FObjectProperty` 与 `FInterfaceProperty` 分成不同桥接入口；`FInterfaceProperty` 显式变成 `TScriptInterface<>`。
- `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:1537-1595,533-560` 在 factory 层把 `CPT_Interface` 分流到 `FInterfacePropertyDesc`，不让 interface 先退化成 generic object。

[80] 参考实现的共同点是 `family` 在入口就显式化，而不是晚期再猜：

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp
// 位置: 388-430
// 说明: object 与 interface 在 bridge 入口就是两个 family
// ============================================================================
FClassReflection* FTypeBridge::GetClass(const FObjectProperty* InProperty)
{
	return InProperty != nullptr ? FReflectionRegistry::Get().GetClass(InProperty->PropertyClass) : nullptr;
}

FClassReflection* FTypeBridge::GetClass(const FInterfaceProperty* InProperty)
{
	const auto FoundGenericClass = FReflectionRegistry::Get().GetTScriptInterfaceClass();
	const auto FoundClass = FReflectionRegistry::Get().GetClass(InProperty->InterfaceClass);
	return MakeGenericTypeInstance(FoundGenericClass, FoundClass); // ★ interface 有独立 family 结果
}

// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp
// 位置: 1537-1595, 533-560
// 说明: property factory 先分 `CPT_Interface`，再走专用 descriptor
// ============================================================================
case CPT_Interface:
{
	PropertyDesc = new FInterfacePropertyDesc(InProperty); // ★ 入口直接分流
	break;
}

virtual bool SetValueInternal(lua_State *L, void *ValuePtr, int32 IndexInStack, bool bCopyValue) const override
{
	FScriptInterface *Interface = (FScriptInterface*)ValuePtr;
	UObject *Value = UnLua::GetUObject(L, IndexInStack);
	Interface->SetObject(Value);
	Interface->SetInterface(Value ? Value->GetInterfaceAddress(InterfaceProperty->InterfaceClass) : nullptr);
	return true;
}
```

**吸收建议**

- 先在插件内补一层轻量 `EScriptReferenceKind` 或等价 `ScriptInterfaceType`，第一阶段只在 `FAngelscriptTypeUsage::FromTypeId()` / `FromClass()` 内部生效，不改脚本语法、不改 AngelScript `2.33.0 WIP` runtime。
- `CanCastScriptObjectToUnrealInterface()`、future `FInterfaceProperty`、UHT interface support 全部改查这份 family，而不是继续各自读 `GetUserData()` + `CLASS_Interface`。
- `ClassGenerator` 的 `bIsInterface` 继续保留，但降级成 materialization 消费者，不再承担“interface family 唯一来源”。

**优先级**

- `P0`
- **理由**：它直接对应 `Documents/AutoPlans/ArchitectureReview/TypeSystem_ArchReview.md:1966-1996` 与 `Documents/Plans/Plan_CppInterfaceBinding.md:21-22,78-98` 的共同阻塞点；若入口 family 仍缺位，`P10` 只能继续在每条 lane 单独补洞。

### [D2 / D6] 类型系统补充：`SubTypes[]` 还不是 role-tagged `TypeShape`

**当前状态**

`FAngelscriptTypeUsage` 目前只有一个无语义标签的 `TArray<FAngelscriptTypeUsage> SubTypes`，见 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:349-387`。容器和 wrapper 通过硬编码下标解释它：

- `Bind_TMap.cpp:110-156,1175-1177` 约定 `[0] = Key`、`[1] = Value`。
- `Bind_TOptional.cpp:110-123,506-512` 约定 `[0] = Inner`。
- `Bind_BlueprintType.cpp:1557-1588,1801-1807` 又把 `[0]` 同时拿来表示 `MetaClass/ObjectClass`，并拼接 `unresolved_object` 等 object-wrapper 语义。

这意味着 `SubTypes[0]` 在不同 family 里已经承载了完全不同的角色；结构本身并不知道“这个槽位到底是 element、key、meta-class，还是未来 `InterfaceClass`”。

```
[Current] Implicit Shape Encoding
FAngelscriptTypeUsage
└─ SubTypes[]
   ├─ [0] element / key / inner / meta-class / object-class
   └─ [1] value

Bind_TMap          -> [0]=Key,   [1]=Value
Bind_TOptional     -> [0]=Inner
Bind_BlueprintType -> [0]=MetaClass/ObjectClass
future TScriptInterface<> -> ?   // 没有 InterfaceClass / ObjectSlot / InterfaceSlot role
```

[81] 当前 shape 仍靠隐式下标约定传播，`TScriptInterface<>` 没有可落脚的结构位：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h
// 位置: 349-387
// 说明: TypeUsage 只有 `SubTypes[]`，没有 role-tagged shape
// ============================================================================
struct ANGELSCRIPTRUNTIME_API FAngelscriptTypeUsage
{
	TArray<FAngelscriptTypeUsage> SubTypes; // ★ 只有位置，没有角色名
	TSharedPtr<FAngelscriptType> Type;
	bool bIsReference = false;
	bool bIsConst = false;
	...
};

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp
// 位置: 110-156, 1175-1177
// 说明: map 约定 `[0]=Key`、`[1]=Value`
// ============================================================================
return Usage.SubTypes[0].CanCreateProperty() && Usage.SubTypes[0].CanHashValue()
	&& Usage.SubTypes[1].CanCreateProperty();

MapProp->KeyProp = Usage.SubTypes[0].CreateProperty(InnerParams);
MapProp->ValueProp = Usage.SubTypes[1].CreateProperty(InnerParams);

Usage.Type = MapType;
Usage.SubTypes.Add(KeyType);
Usage.SubTypes.Add(ValueType);

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TOptional.cpp
// 位置: 115-123, 510-512
// 说明: optional 又约定 `[0]=Inner`
// ============================================================================
OptionalProp->SetValueProperty(Usage.SubTypes[0].CreateProperty(InnerParams));
...
Usage.Type = OptionalType;
Usage.SubTypes.Add(InnerUsage);

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp
// 位置: 1557-1588, 1801-1807
// 说明: object wrapper 再把 `[0]` 解释成 `MetaClass/ObjectClass`
// ============================================================================
UClass* GetMetaClass(const FAngelscriptTypeUsage& Usage) const
{
	if (Usage.SubTypes.Num() == 0)
		return nullptr;
	return Usage.SubTypes[0].GetClass(); // ★ 同一个槽位又变成 meta-class
}

return Usage.SubTypes[0].GetAngelscriptDeclaration(Mode) + TEXT(" unresolved_object");
```

**差距描述**

- `没有实现`：没有 `InterfaceClass`、`ObjectSlot`、`InterfaceSlot` 这类显式 role。
- `实现方式不同`：当前不是“另一种结构化 shape”，而是单纯的 positional convention。
- `实现质量差异`：`TScriptInterface<>` 一旦落地，就会继续扩展新的下标约定；这会让 property 创建、debugger、JIT、cache replay 各自读出不同语义。

**参考方案**

- `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:738-760,890-915,1537-1595` 直接把 `InnerProperty`、`KeyProperty`、`ValueProperty`、`FInterfacePropertyDesc` 做成具名字段。
- `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:598-632,862-985,1225-1315` 的 translator family 也都持有显式 `InterfaceProperty`、`ArrayProperty->Inner`、`MapProperty->KeyProp/ValueProp`，不是靠 `SubTypes[N]` 反推。

[82] 参考实现保留的是具名结构角色，而不是“谁记得住第几个下标代表什么”：

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp
// 位置: 738-760, 890-915, 1537-1595
// 说明: array/map/interface 的结构角色都是显式字段
// ============================================================================
class FArrayPropertyDesc : public FPropertyDesc
{
	...
	: FPropertyDesc(InProperty), InnerProperty(FPropertyDesc::Create(ArrayProperty->Inner))
{}
};

class FMapPropertyDesc : public FPropertyDesc
{
	...
	: FPropertyDesc(InProperty), KeyProperty(FPropertyDesc::Create(MapProperty->KeyProp)), ValueProperty(FPropertyDesc::Create(MapProperty->ValueProp))
{}
};

case CPT_Interface:
{
	PropertyDesc = new FInterfacePropertyDesc(InProperty); // ★ interface 也有自己的具名 owner
	break;
}
```

**吸收建议**

- 在不立刻移除 `SubTypes[]` 的前提下，先在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/` 叠一层 `FAngelscriptTypeShape` / `EAngelscriptTypeRole` 视图。
- 第一版 role 至少覆盖 `Element`、`Key`、`Value`、`MetaClass`、`ObjectClass`、`InterfaceClass`；若未来要表达 `FScriptInterface` 双槽值，再补 `ObjectSlot`、`InterfaceSlot`。
- 迁移顺序先从 `Bind_TArray.cpp`、`Bind_TMap.cpp`、`Bind_TOptional.cpp`、`Bind_BlueprintType.cpp` 开始，让旧逻辑继续从 `SubTypes[]` 生成 shape，避免一次性重写 runtime。

**优先级**

- `P1`
- **理由**：它不是 `P1/P2` “先让 C++ UInterface 可见与可调” 的前置条件，但它是 `Phase 4 FInterfaceProperty` 想稳定落地的前提。若不先把 shape 显式化，后续 cache、debugger、JIT 仍会继续发明新的隐式槽位。

### [D2 / D6 / D9] 支持矩阵补充：同一 type family 仍没有统一 `SurfaceSupportProfile`

**当前状态**

今天的 Angelscript 并没有一个地方能回答“某个 type family 在不同使用面上到底支持到哪”。同一件事被拆成至少五条 lane：

- `FAngelscriptType` 只提供 `CanCreateProperty()`、`CanBeArgument()`、`CanBeReturned()`、`CanBeTemplateSubType()` 这类离散布尔能力，见 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:150-255`。
- `ClassGenerator` 组合这些布尔位去决定 script `UFUNCTION` 是否可编译，见 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:572-610`。
- native direct bind 先查 `FUNC_Native`，再查 `ShouldSkipBlueprintCallableFunction()`，再查 `Entry` 是否存在，见 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp:27-48`。
- reflective fallback 又另起一套 `RejectedInterfaceClass / RejectedCustomThunk / RejectedTooManyArguments` 规则，见 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp:254-282`。
- UHT exporter / code generator 则再维护一套 `IsBlueprintCallable()`、`ShouldGenerate()`、`Interface -> ERASE_NO_FUNCTION()` 的规则，见 `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:56-87` 与 `.../AngelscriptFunctionTableCodeGenerator.cs:465-514`。

结果就是：**同一个 family 无法只打开 property/script callable，而把 direct bind/UHT 保持关闭并附稳定 reason code。**

```
[Current] Surface Decision Split
Type::CanCreateProperty / CanBeArgument / CanBeReturned
├─ ClassGenerator::Analyze UFUNCTION                // script callable lane
├─ Bind_BlueprintCallable                           // native direct-bind lane
├─ EvaluateReflectiveFallbackEligibility            // reflective lane
└─ UHT Exporter / CodeGenerator                     // artifact lane

No shared profile:
  - no lane state
  - no shared reason code
  - no family-level rollout view
```

[83] 当前 surface decision 被拆在多处局部判断里，同一 family 没有单一 authority：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h
// 位置: 150-255
// 说明: type core 只暴露离散能力布尔值，没有 lane-aware profile
// ============================================================================
virtual bool CanCreateProperty(const FAngelscriptTypeUsage& Usage) const { return false; }
virtual bool CanBeArgument(const FAngelscriptTypeUsage& Usage) const { return false; }
virtual bool CanBeReturned(const FAngelscriptTypeUsage& Usage) const { return false; }
virtual bool CanBeTemplateSubType() const { return true; }

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 位置: 572-610
// 说明: script UFUNCTION lane 组合 property/arg/return 布尔位
// ============================================================================
FunctionDesc->ReturnType = FAngelscriptTypeUsage::FromReturn(ScriptFunction);
if (!FunctionDesc->ReturnType.IsValid() || !FunctionDesc->ReturnType.CanCreateProperty() || !FunctionDesc->ReturnType.CanBeReturned() || FunctionDesc->ReturnType.bIsReference)
{
	...
}

auto Type = FAngelscriptTypeUsage::FromParam(ScriptFunction, i);
if (!Type.IsValid() || !Type.CanBeArgument() || !Type.CanCreateProperty())
{
	...
}

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp
// 位置: 27-48
// 说明: native direct bind lane 又是一套条件
// ============================================================================
if (!Function->HasAnyFunctionFlags(FUNC_Native))
	return;
if (FAngelscriptBinds::ShouldSkipBlueprintCallableFunction(Function))
	return;
...
if (Entry == nullptr)
	return;

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp
// 位置: 254-282
// 说明: reflective lane 再单独拒绝 interface/custom thunk/参数过多
// ============================================================================
if (OwningClass->HasAnyClassFlags(CLASS_Interface))
{
	return EAngelscriptReflectiveFallbackEligibility::RejectedInterfaceClass;
}
if (Function->HasMetaData(NAME_BlueprintCallableReflectiveFallback_CustomThunk))
{
	return EAngelscriptReflectiveFallbackEligibility::RejectedCustomThunk;
}
if (GetNonReturnParameterCount(Function) > BlueprintCallableReflectiveFallbackMaxArgs)
{
	return EAngelscriptReflectiveFallbackEligibility::RejectedTooManyArguments;
}

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs
// 位置: 56-87; 465-514
// 说明: UHT lane 继续维护自己的 BlueprintCallable / interface / custom thunk 规则
// ============================================================================
return function.FunctionType == UhtFunctionType.Function &&
	(functionFlags.Contains("BlueprintCallable", StringComparison.Ordinal) ||
	functionFlags.Contains("BlueprintPure", StringComparison.Ordinal));

if (classObj.ClassType == UhtClassType.Interface || classObj.ClassType == UhtClassType.NativeInterface)
{
	eraseMacro = "ERASE_NO_FUNCTION()"; // ★ artifact lane 直接 hard-stub
}

return !function.FunctionExportFlags.ToString().Contains("CustomThunk", StringComparison.Ordinal);
```

**差距描述**

- `没有实现`：没有一个 family-level `SurfaceSupportProfile` 来描述 `PropertyMaterialization / ScriptFunctionArgument / ScriptFunctionReturn / NativeBlueprintCallableBind / ReflectiveFallback / UHTDirectBind`。
- `实现方式不同`：当前不是“同一 profile 投影到多条 lane”，而是每条 lane 自己解释一遍。
- `实现质量差异`：没有共享 reason code，就无法给 `Bind API GAP`、`AS_FunctionTable_*`、runtime skip/fallback 输出同一套可 join 的账本。

**参考方案**

- `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:930-1072,1248-1327` 让 `GenFunction()` 同时服务 class 函数和 interface 函数，函数签名 authority 只有一套。
- `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:82-272` 用 `GetPropertyType()` 统一递归处理 object / interface / array / map / set / optional family，让“这个 property 应该长成什么形状”不再散落到各个生成面。

[84] 参考实现的关键不是技术栈，而是“helper 先统一，surface 再投影”：

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp
// 位置: 930-1072, 1248-1327
// 说明: `GenFunction()` 是 class / interface 共用的签名 authority
// ============================================================================
bool FTypeScriptDeclarationGenerator::GenFunction(
	FStringBuffer& OwnerBuffer, UFunction* Function, bool WithName, bool ForceOneway, bool IgnoreOut, bool IsExtensionMethod)
{
	...
	if (!GenTypeDecl(TmpBuf, Property, RefTypes))
	{
		return false;
	}
	...
}

for (TFieldIterator<UFunction> FunctionIt(Class, EFieldIteratorFlags::ExcludeSuper); FunctionIt; ++FunctionIt)
{
	if (!GenFunction(TmpBuff, *FunctionIt))
	{
		continue;
	}
	...
}

for (int i = 0; i < Class->Interfaces.Num(); i++)
{
	for (TFieldIterator<UFunction> FunctionIt(Class->Interfaces[i].Class, EFieldIteratorFlags::IncludeSuper); FunctionIt; ++FunctionIt)
	{
		if (!GenFunction(TmpBuff, *FunctionIt))
		{
			continue;
		}
		...
	}
}

// ============================================================================
// 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp
// 位置: 82-272
// 说明: `GetPropertyType()` 统一递归 object/interface/container/wrapper family
// ============================================================================
if (const auto InterfaceProperty = CastField<FInterfaceProperty>(Property))
{
	return FString::Printf(TEXT(
		"TScriptInterface<%s>"
	),
		*FUnrealCSharpFunctionLibrary::GetFullInterface(InterfaceProperty->InterfaceClass));
}

if (const auto MapProperty = CastField<FMapProperty>(Property))
{
	return FString::Printf(TEXT(
		"TMap<%s, %s>"
	),
		*GetPropertyType(MapProperty->KeyProp),
		*GetPropertyType(MapProperty->ValueProp));
}
```

**吸收建议**

- 在插件内新增 `FAngelscriptSurfaceSupportProfile`，第一版只做 report-only，字段至少覆盖 `PropertyMaterialization`、`ScriptFunctionArgument`、`ScriptFunctionReturn`、`NativeBlueprintCallableBind`、`ReflectiveFallback`、`UHTDirectBind`、`TemplateEmbedding`，并带 `ReasonCode`。
- `FAngelscriptCallableExposurePolicy` 不应被推翻，而应降级成 `SurfaceSupportProfile` 在 callable lane 上的一个投影；这样 `P10` 可以先只把 `FInterfaceProperty` 打开到 property/script callable，不必同步承诺 direct bind/UHT。
- 在 `AngelscriptTest` 增加 `SurfaceSupportProfile` snapshot，直接把 `UObject*`、`delegate`、`TOptional<T>`、future `TScriptInterface<>` 的 lane 状态固化成回归资产。

**优先级**

- `P0`
- **理由**：它可以 report-only 落地，不要求引擎修改，也不会阻断当前主线；但没有它，`P10 UInterface` 和 `Bind API GAP` 仍只能靠人工交叉阅读 runtime/UHT/test 三套规则。

### 值得吸收的设计模式（增量）

- `Family before lane`：先确定 type family 身份，再让 property/callable/UHT/editor 等 lane 去消费；不要让 lane 反过来猜 family。
- `Role-tagged shape beats positional subtype`：`Element/Key/Value/InterfaceClass/ObjectSlot` 这类结构角色应该是具名字段，不应继续埋在 `SubTypes[0/1]` 里。
- `Profile first, rollout second`：先有 report-only 的 lane support profile，再决定打开哪条 lane；这样 `P10` 才能渐进推进，而不是一次改五处条件。

### 改进路线建议（增量）

1. `P0`：在 `TypeSystem` 入口补 `EScriptReferenceKind` 或等价 `ScriptInterfaceType`，先加 snapshot，不改脚本表面语法。
2. `P0`：新增 `FAngelscriptSurfaceSupportProfile`，把 `ClassGenerator`、runtime callable、reflective fallback、UHT 先接成 report-only，同步输出 `ReasonCode`。
3. `P1`：在 `FAngelscriptTypeUsage` 上叠 `FAngelscriptTypeShape` 视图，先从 `SubTypes[]` 映射生成 role，再逐步迁移 `Bind_TArray/TMap/TOptional/Bind_BlueprintType`。
4. `P1`：基于 `ScriptReferenceKind + SurfaceSupportProfile + TypeShape` 三件套，再进入 `P10 Phase 4 FInterfaceProperty`，首批只开 `non-container property + script callable`，direct bind/UHT 继续显式关闭并输出原因。

---
## 深化分析 (2026-04-09 07:45:41)

### 本轮新增关键发现

- 前文已经收口了 `TypeShape + SurfaceSupportProfile`。本轮补证后更明确：`P10 UInterface` 还缺第三个 `P0 owner`，即真正的 `InterfaceValueBridge`。当前插件能生成 `UInterface` 关系图，也能做 `ImplementsInterface()` 查询，但 runtime/UHT 里没有任何直接处理 `FInterfaceProperty` 或 `TScriptInterface<>` 的正式入口。
- `interface callable` 的真正缺口不是“接口函数不存在”，而是没有 class/interface 共用的 callable authority。`ClassGenerator`、runtime fallback、UHT function table 三条 lane 现在分别做判断，所以同一个 `UFunction` 无法得到统一的“可暴露 / 不可暴露 + 原因”结论。
- `BlueprintImpact` 已经把 `Classes` 放进 symbol bag，但 pin、variable、commandlet summary 和自动化仍停在 `Struct / Enum / Delegate`。这意味着 `P10` 一旦把 interface family 带进 pin/variable，editor 和 CI 仍会静默漏报。

### 差距矩阵（增量）

| 维度 | 本轮新增判断 | 差距等级 | 最佳参考实现 | 优先级 |
| --- | --- | --- | --- | --- |
| `D2 / D6` 反射绑定机制 / 代码生成 | 缺的是 `InterfaceValueBridge`，不只是 `TypeShape/Profile` | `能力缺失` | `UnLua` `FInterfacePropertyDesc` + `UnrealCSharp` `TScriptInterfaceClass/FInterfacePropertyDescriptor` + `puerts` `FInterfacePropertyTranslator` | `P0` |
| `D3` Blueprint 交互 | 缺的是 class/interface 共用 callable builder，不是更多名字特判 | `实现质量差异` | `puerts` `FTypeScriptDeclarationGenerator::GenFunction()` | `P0` |
| `D7 / D9` 编辑器集成 / 测试基础设施 | 缺的是 structured pin family service 与 issue-scoped interface regression | `能力缺失` | `puerts` `FPEGraphPinType/ToFEdGraphPinType` + `UnrealCSharp` `FDynamicClassGenerator` | `P1` |

### [D2 / D6] 反射绑定机制补充：`TypeShape` 与 `SurfaceSupportProfile` 之外，还缺一个真正的 `InterfaceValueBridge`

前文已经指出 `TypeShape` 与 `SurfaceSupportProfile` 的缺口；本轮新增的收口点是：**即使这两个抽象都补上，`UInterface` 仍然需要一个单独的 value owner**。当前代码里，接口关系主要存在于 `UClass::Interfaces` 图和 `ImplementsInterface()` 查询面上；一旦进入 property/materialization/debugger/UHT artifact lane，就找不到 `FScriptInterface` 的正式承载者。

**当前状态**

- `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:2438-2514` 的 type finder 只处理 `FObjectProperty`、`FWeakObjectProperty`、`FClassProperty` 与 `CPF_UObjectWrapper`，没有 `FInterfaceProperty` 分支。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h:150-255` 暴露的是离散能力位；它能回答“能不能建 property / 当参数 / 当返回值”，但没有“interface value 由谁读写”的一等 owner。
- 对 `Plugins/Angelscript/Source/AngelscriptRuntime/` 与 `Plugins/Angelscript/Source/AngelscriptUHTTool/` 全量检索 `FInterfaceProperty|UInterfaceProperty|TScriptInterface<`，当前命中为 `0`。这不是“实现方式不同”，而是 runtime/UHT 直接缺少 family owner。

```
[Current AS] Interface Value Ownership
UClass Graph
├─ Preprocessor / ClassGenerator                     // 记录 implements 关系
│  └─ NewClass->Interfaces
Runtime Type / Value Lane
├─ TypeUsage + SubTypes[0]                           // object/class/subclass
├─ QuickScriptInterfaceCast                          // 只问 ImplementsInterface
└─ no FInterfaceProperty owner                       // 没有 interface value bridge
Artifact Lane
└─ no TScriptInterface / FInterfaceProperty entry    // UHT/runtime 都没有 family owner

[Reference] Three Complementary Owners
UnLua        -> FInterfacePropertyDesc               // property/value owner
puerts       -> FInterfacePropertyTranslator         // marshal owner
UnrealCSharp -> TScriptInterfaceClass + registry     // generic/value identity owner
```

[85] 当前 `Bind_BlueprintType` 的 type finder 只认识 object/class/subclass family；interface value 没有入口：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp
// 位置: 2438-2514
// 说明: type finder 只处理 object/class/subclass，没有 `FInterfaceProperty` 分支
// ============================================================================
FAngelscriptType::RegisterTypeFinder([=](FProperty* Property, FAngelscriptTypeUsage& Usage) -> bool
{
	const FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property);
	if (ObjectProperty == nullptr)
	{
		const FWeakObjectProperty* WeakObjectProperty = CastField<FWeakObjectProperty>(Property);
		if (WeakObjectProperty != nullptr)
		{
			FAngelscriptTypeUsage InnerType = FAngelscriptTypeUsage::FromClass(WeakObjectProperty->PropertyClass);
			Usage.Type = WeakObjectPtrType;
			Usage.SubTypes.SetNum(1);
			Usage.SubTypes[0] = InnerType;
			return true;
		}

		return false; // ★ 这里没有 interface fallback
	}

	if ((ObjectProperty->PropertyFlags) != 0)
	{
		FAngelscriptTypeUsage InnerType = FAngelscriptTypeUsage::FromClass(ObjectProperty->PropertyClass);
		Usage.Type = ObjectPtrType;
		Usage.SubTypes.SetNum(1);
		Usage.SubTypes[0] = InnerType;
		return true;
	}

	const FClassProperty* ClassProperty = CastField<FClassProperty>(Property);
	if (ClassProperty != nullptr && (ClassProperty->PropertyFlags & CPF_UObjectWrapper) != 0)
	{
		FAngelscriptTypeUsage InnerType = FAngelscriptTypeUsage::FromClass(ClassProperty->MetaClass);
		Usage.Type = SubclassOfType;
		Usage.SubTypes.SetNum(1);
		Usage.SubTypes[0] = InnerType;
		return true;
	}

	return false;
});
```

[86] `UnLua` 与 `puerts` 虽然脚本语言不同，但都把 interface value 的核心动作收敛成同一对操作：`SetObject()` + `SetInterface()`：

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp
// 位置: 533-576, 1537-1595
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp
// 位置: 598-632
// 说明: 两个参考实现都给 `FInterfaceProperty` 独立 owner，并显式回填 dual-slot `FScriptInterface`
// ============================================================================
class FInterfacePropertyDesc : public FPropertyDesc
{
public:
	explicit FInterfacePropertyDesc(FProperty *InProperty)
		: FPropertyDesc(InProperty)
	{
	}

	virtual bool SetValueInternal(lua_State *L, void *ValuePtr, int32 IndexInStack, bool bCopyValue) const override
	{
		FScriptInterface *Interface = (FScriptInterface*)ValuePtr;
		UObject *Value = UnLua::GetUObject(L, IndexInStack);
		Interface->SetObject(Value);
		Interface->SetInterface(Value ? Value->GetInterfaceAddress(InterfaceProperty->InterfaceClass) : nullptr);
		return true; // ★ dual-slot 同时回填
	}
};

case CPT_Interface:
{
	PropertyDesc = new FInterfacePropertyDesc(InProperty); // ★ interface 直接进入 property factory
	break;
}

class FInterfacePropertyTranslator : public FPropertyWithDestructorReflection
{
public:
	bool JsToUE(v8::Isolate* Isolate, v8::Local<v8::Context>& Context, const v8::Local<v8::Value>& Value, void* ValuePtr,
		bool DeepCopy) const override
	{
		UObject* Object = FV8Utils::GetUObject(Context, Value);
		FScriptInterface* Interface = reinterpret_cast<FScriptInterface*>(ValuePtr);
		Interface->SetObject(Object);
		Interface->SetInterface(Object ? Object->GetInterfaceAddress(InterfaceProperty->InterfaceClass) : nullptr);
		return true; // ★ marshal owner 也保持同一套 dual-slot 语义
	}
};
```

[87] `UnrealCSharp` 再补上了当前 Angelscript 最缺的那一层：**generic identity owner**。`TScriptInterface` 被注册成独立 reflection class，并由 descriptor/registry 共同持有：

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Reflection/FReflectionRegistry.cpp
// 位置: 57-60, 1087-1090
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/ObjectProperty/FInterfacePropertyDescriptor.cpp
// 位置: 4-45
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Domain/InternalCall/FRegisterScriptInterface.cpp
// 位置: 9-62
// 说明: `TScriptInterface` 既是 reflection class，又有 descriptor 和 runtime registry
// ============================================================================
TScriptInterfaceClass = GetClass(
	COMBINE_NAMESPACE(NAMESPACE_ROOT, NAMESPACE_CORE_UOBJECT), GENERIC_T_SCRIPT_INTERFACE);

FClassReflection* FReflectionRegistry::GetTScriptInterfaceClass()
{
	return TScriptInterfaceClass; // ★ generic identity owner
}

void FInterfacePropertyDescriptor::Set(void* Src, void* Dest) const
{
	...
	const auto Interface = static_cast<FScriptInterface*>(Dest);
	const auto Object = SrcMulti->GetObject();
	Interface->SetObject(Object);
	Interface->SetInterface(Object ? Object->GetInterfaceAddress(Property->InterfaceClass) : nullptr);
}

FClassBuilder(TEXT("TScriptInterface"), NAMESPACE_LIBRARY)
	.Function("Register", RegisterImplementation)
	.Function("Identical", IdenticalImplementation)
	.Function("UnRegister", UnRegisterImplementation)
	.Function("GetObject", GetObjectImplementation); // ★ runtime registry 也是一等入口
```

**差距描述**

- `没有实现`：当前 runtime/UHT 没有 `FInterfaceProperty` 或 `TScriptInterface<>` 的正式 owner。
- `实现方式不同`：当前把 interface 主要建模成 `UClass` 关系图；参考实现把它建模成有独立 value carrier 的 family。
- `实现质量差异`：没有 family owner，就无法把 `Bind API GAP`、debugger、UHT artifact、property materialization 对齐到同一份账本。

**参考方案**

- `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:533-576,1537-1595`
- `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:598-632`
- `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Reflection/FReflectionRegistry.cpp:57-60,1087-1090`
- `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/ObjectProperty/FInterfacePropertyDescriptor.cpp:4-45`
- `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Domain/InternalCall/FRegisterScriptInterface.cpp:9-62`

**吸收建议**

- 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/` 新增 `FAngelscriptInterfaceValueBridge`，第一版只服务 `FScriptInterface` dual-slot 读写、debugger 展示和 property materialization，不急着暴露新的脚本语法。
- 让 `Bind API GAP` 与 future `SurfaceSupportProfile` 直接出现 `InterfaceValueBridge` / `InterfacePropertyMaterialization` 行，避免继续把 interface 缺口伪装成“某些函数缺少 bind”。
- 若要引入 `TScriptInterface<T>` 作为公开语法，应该晚于 bridge 落地；先把 owner 立起来，再讨论脚本 surface，而不是反过来。

**优先级**

- `P0`
- **理由**：没有 value bridge，`P10` 最终只能停在“类关系可见、接口值不可物化”的半成品状态，`FInterfaceProperty`、debugger 和 generated artifact 都无从落地。

### [D3] Blueprint 交互补充：当前缺的不是更多 interface test，而是一个 class/interface 共用的 callable builder

前文已经反复证明当前接口可生成、可继承、可 dispatch；本轮新增的收口点是：**production callable lane 仍没有共享 builder**。也就是说，接口现在更像“有结构、没主线”。

**当前状态**

- `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:5060-5185` 会把 `ImplementedInterfaces` 挂到 `NewClass->Interfaces`，并在编译期验证必需方法是否存在。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp:100-106,189-214` 对外公开的是 `ImplementsInterface()` 与 `opCast` 语义，本质仍是 query/cast lane。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp:254-282` 直接把 interface owner 判成 `RejectedInterfaceClass`。
- `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:465-514` 对 interface/native interface 直接写 `ERASE_NO_FUNCTION()`。

```
[Current AS] Interface Callable Lanes
Compile Lane
├─ Parse interface / build NewClass->Interfaces     // 结构存在
└─ Validate required method names                   // 只校验“有没有”

Runtime Lane
├─ UObject::ImplementsInterface()                   // query
├─ opCast                                           // cast
└─ ReflectiveFallback -> RejectedInterfaceClass     // call reject

Artifact Lane
└─ UHT FunctionTable -> Interface => ERASE_NO_FUNCTION()

[Reference puerts] Shared Callable Authority
Class UFunction      ┐
Interface UFunction  ├─> GenFunction()              // 同一套签名 authority
Extension Method     ┘
```

[88] 当前接口 callable 的 authority 被拆成“结构生成 / query-cast / fallback 拒绝 / UHT stub”四段：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 位置: 5060-5185
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp
// 位置: 100-106, 189-214
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp
// 位置: 254-282
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs
// 位置: 465-514
// 说明: interface 已进入 class graph，但 runtime callable/UHT 仍没有共享 authority
// ============================================================================
if (ClassDesc->ImplementedInterfaces.Num() > 0 && !ClassDesc->bIsInterface)
{
	...
	NewClass->Interfaces.Add(ImplementedInterface); // ★ 结构 owner 在这里
	...
	UFunction* ImplFunc = NewClass->FindFunctionByName(InterfaceFunc->GetFName());
	if (ImplFunc == nullptr || bResolvedToInterfaceStub)
	{
		FAngelscriptEngine::Get().ScriptCompileError(...); // ★ 这里只验证“有没有同名实现”
	}
}

UObject_.Method("bool ImplementsInterface(const UClass InterfaceClass) const",
[](UObject* Object, UClass* InterfaceClass)
{
	return Object != nullptr && InterfaceClass != nullptr
		? Object->GetClass()->ImplementsInterface(InterfaceClass)
		: false;
});

if (OwningClass->HasAnyClassFlags(CLASS_Interface))
{
	return EAngelscriptReflectiveFallbackEligibility::RejectedInterfaceClass; // ★ runtime fallback 直接拒绝
}

if (classObj.ClassType == UhtClassType.Interface || classObj.ClassType == UhtClassType.NativeInterface)
{
	eraseMacro = "ERASE_NO_FUNCTION()"; // ★ artifact lane 也直接 stub
}
```

[89] `puerts` 的关键不在 TypeScript，而在 **class/interface 共用同一个 `GenFunction()`**。它先把签名 authority 收束，再投影到具体生成面：

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp
// 位置: 891-899, 930-1010, 1303-1325
// 说明: interface property 与 interface function 都走统一 helper，而不是另起旁路
// ============================================================================
else if (auto InterfaceProperty = CastFieldMacro<InterfacePropertyMacro>(Property))
{
	AddToGen.Add(InterfaceProperty->InterfaceClass);
	StringBuffer << GetNameWithNamespace(InterfaceProperty->InterfaceClass); // ★ type helper 先统一
}

bool FTypeScriptDeclarationGenerator::GenFunction(
	FStringBuffer& OwnerBuffer, UFunction* Function, bool WithName, bool ForceOneway, bool IgnoreOut, bool IsExtensionMethod)
{
	...
	for (TFieldIterator<PropertyMacro> ParamIt(Function); ParamIt; ++ParamIt)
	{
		...
		// ★ class/interface/extension method 最终都由同一套参数与默认值规则处理
	}
}

for (TFieldIterator<UFunction> FunctionIt(Class, EFieldIteratorFlags::ExcludeSuper); FunctionIt; ++FunctionIt)
{
	if (!GenFunction(TmpBuff, *FunctionIt))
	{
		continue;
	}
}

for (int i = 0; i < Class->Interfaces.Num(); i++)
{
	for (TFieldIterator<UFunction> FunctionIt(Class->Interfaces[i].Class, EFieldIteratorFlags::IncludeSuper); FunctionIt; ++FunctionIt)
	{
		if (!GenFunction(TmpBuff, *FunctionIt))
		{
			continue;
		}
		TryToAddOverload(Outputs, FunctionIt->GetName(), (FunctionIt->FunctionFlags & FUNC_Static) != 0, TmpBuff.Buffer);
	}
}
```

**差距描述**

- `没有实现`：当前没有 class/interface 共用的 callable builder，可以同时服务 runtime、fallback 与 UHT。
- `实现方式不同`：当前更像“class graph 主线 + interface 旁路特判”；`puerts` 是“共享 helper -> 多个 lane 投影”。
- `实现质量差异`：因为 authority 分裂，同一个 interface `UFunction` 今天可能同时处于“ClassGenerator 认为存在、runtime fallback 拒绝、UHT 直接 stub”的状态。

**参考方案**

- `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:891-899,930-1010,1303-1325`

**吸收建议**

- 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` 或 `.../Core/` 新增 `TryBuildCallableSignature(UClass* OwnerClass, UFunction* Function, FAngelscriptCallableBuildResult&)`，让 runtime bind scan、reflective fallback diagnostics、UHT exporter 先共用同一套 builder。
- 第一阶段只做 report-only：对 interface owner 不再直接 `ERASE_NO_FUNCTION()`，而是输出 `interface-owner + reason code`，让 `Bind API GAP` 能看见“理论可建 / 当前未开放”的边界。
- 第二阶段优先打开 `script callable` 与 diagnostics lane；`native direct bind` 是否开放，可以等 `InterfaceValueBridge` 稳定后再决定。

**优先级**

- `P0`
- **理由**：这一步不要求立刻开放 interface native bind，但它决定了 `P10` 是否能被稳定观测。没有 shared builder，就没有可回归的 rollout 账本。

### [D7 / D9] 编辑器集成与测试补充：`BlueprintImpact` 需要从“reason list”升级到 structured `PinFamily` service

前文已经指出 interface/class pin blind spot；本轮新增的是**该怎么吸收参考实现**。当前 `BlueprintImpact` 的问题不只是少几个 `if`，而是 pin identity 本身没有被建模成可复用服务。

**当前状态**

- `Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp:44-57` 的 `MatchesPinType()` 只认 `PC_Struct`、`PC_Enum`/`PC_Byte`。
- 同文件 `:62-68,112-147` 的 `FBlueprintImpactSymbols` 与 `BuildImpactSymbols()` 已经收集 `Classes`，但 pin/variable 检测并不消费 `Classes`。
- `Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp:87-95` 的 summary 只有 `Classes/Structs/Enums/Delegates`，没有 interface bucket。
- `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactScannerTests.cpp:378-402` 的 pin regression 只验证 struct pin，不验证 interface/class pin。

```
[Current AS] BlueprintImpact Pin Flow
BuildImpactSymbols
├─ Classes
├─ Structs
├─ Enums
└─ Delegates

AnalyzeLoadedBlueprint
├─ MatchesPinType() -> Struct / Enum / Byte only
├─ VariableType -> same matcher
└─ Commandlet summary -> no interface bucket

[Reference] Structured Pin Ownership
puerts
├─ FPEGraphPinType                            // category + sub-category + container
└─ ToFEdGraphPinType()                        // 单点转换

UnrealCSharp
└─ FDynamicClassGenerator                     // 替换 old/new class 时直接重写 PinSubCategoryObject
```

[90] 当前 `BlueprintImpact` 已经收集 `Classes`，但 pin 与测试主线都还没有把它消费掉：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp
// 位置: 44-57, 62-68, 112-147
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp
// 位置: 87-95
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactScannerTests.cpp
// 位置: 378-402
// 说明: symbol bag 里有 `Classes`，但 pin family 与 regression 仍停在 struct/enum/delegate
// ============================================================================
bool MatchesPinType(const FEdGraphPinType& PinType, const FBlueprintImpactSymbols& Symbols)
{
	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
	{
		return Symbols.Structs.Contains(Cast<UScriptStruct>(PinType.PinSubCategoryObject.Get()));
	}

	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Enum || PinType.PinCategory == UEdGraphSchema_K2::PC_Byte)
	{
		return Symbols.Enums.Contains(Cast<UEnum>(PinType.PinSubCategoryObject.Get()));
	}

	return false; // ★ 没有 `PC_Object / PC_Class / PC_Interface`
}

bool FBlueprintImpactSymbols::IsEmpty() const
{
	return Classes.IsEmpty()
		&& Structs.IsEmpty()
		&& Enums.IsEmpty()
		&& Delegates.IsEmpty()
		&& ReplacementObjects.IsEmpty();
}

if (ClassDesc->Class != nullptr)
{
	Symbols.Classes.Add(ClassDesc->Class); // ★ class 已经被收集
}

TEXT("{ \"BlueprintImpact\": { ... \"Classes\": %d, \"Structs\": %d, \"Enums\": %d, \"Delegates\": %d, ... } }")
// ★ commandlet summary 仍没有 interface / implemented-interface bucket

Symbols.Structs.Add(TBaseStructure<FVector>::Get());
...
return TestTrue(TEXT("BlueprintImpact.AnalyzePinType should record the pin-type reason"),
	Reasons.Contains(EBlueprintImpactReason::PinType));
// ★ 现有 pin regression 只验证 struct pin
```

[91] `puerts` 与 `UnrealCSharp` 提供了两个互补的参考点：一个把 pin type 做成结构化对象，一个在 class 变更后统一重写 `PinSubCategoryObject`：

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/PuertsEditor/Public/PEBlueprintAsset.h
// 位置: 35-56
// 文件: Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp
// 位置: 215-273
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicClassGenerator.cpp
// 位置: 503-505, 656-676
// 说明: pin identity 被建模成结构化数据，并在 class/interface graph 变更时统一修补
// ============================================================================
struct FPEGraphPinType
{
	FName PinCategory;
	UObject* PinSubCategoryObject;
	int PinContainerType;
	bool bIsReference;
	bool bIn; // ★ pin identity 是结构化对象，而不是零散 if
};

static FEdGraphPinType ToFEdGraphPinType(FPEGraphPinType InGraphPinType, FPEGraphTerminalType InPinValueType)
{
	if (InGraphPinType.PinSubCategoryObject && InGraphPinType.PinCategory == UEdGraphSchema_K2::PC_Object)
	{
		if (InGraphPinType.PinSubCategoryObject->IsA<UScriptStruct>())
		{
			InGraphPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		}
		else if (InGraphPinType.PinSubCategoryObject->IsA<UEnum>())
		{
			InGraphPinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
		}
		else
		{
			InGraphPinType.PinCategory = UEdGraphSchema_K2::PC_Object;
		}
	}

	FEdGraphPinType PinType(InGraphPinType.PinCategory, ..., InGraphPinType.PinSubCategoryObject, ...);
	return PinType; // ★ 单点转换，后续扩 interface/class family 更容易
}

if (Pin->PinType.PinSubCategoryObject == InOldClass)
{
	Pin->PinType.PinSubCategoryObject = InNewClass; // ★ 变更后统一重写 pin object identity
}

for (const auto Interface : InClassReflection->GetInterfaces())
{
	if (const auto InterfaceClass = LoadClass<UObject>(nullptr, *Interface->GetPathName()))
	{
		InClass->Interfaces.Emplace(InterfaceClass, 0, true); // ★ class graph 与 pin/class repair 使用同一批 class object
	}
}
```

**差距描述**

- `没有实现`：当前没有 interface-aware 的 `PinFamily` service，也没有对应的 commandlet summary bucket。
- `实现方式不同`：当前是 reason-first 的散点判断；参考实现是先建模 pin identity，再让分析器、编辑器和修补逻辑复用。
- `实现质量差异`：当前 regression 把 pin case 等同于 struct case；interface family 上线后，editor/CI 很容易继续绿灯但真实受影响 Blueprint 没有被命中。

**参考方案**

- `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Public/PEBlueprintAsset.h:35-56`
- `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PEBlueprintAsset.cpp:215-273`
- `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicClassGenerator.cpp:503-505,656-676`

**吸收建议**

- 在 `Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/` 新增 `MatchesPinFamily()` / `FAngelscriptBlueprintPinFamily`，统一识别 `PinCategory + PinSubCategoryObject + ContainerType`，不要再让 scanner/test/refresh helper 各写一套 `if`.
- `BlueprintImpactScanCommandlet` summary 至少补 `Interfaces` 与 `ImplementedInterfaceMatches` 两个计数，否则 CI 看不到 interface 改动覆盖面。
- 新增 issue-scoped regression：`implemented-interface only` Blueprint、`interface variable` Blueprint、`interface pin` Blueprint 三条 case 必须同时验证 `AnalyzeLoadedBlueprint()`、commandlet summary 与 reload/pin refresh。

**优先级**

- `P1`
- **理由**：它不阻挡 `InterfaceValueBridge` 与 shared callable builder 落地，但若继续后置，`P10` 的 editor/test 安全网会在最需要它的时候失效。

### 值得吸收的设计模式（增量）

- `Value owner is separate from type owner`：`TypeShape` 和 `SurfaceSupportProfile` 解决“它是什么、在哪些 lane 支持”，`InterfaceValueBridge` 解决“这个值由谁真正读写和持有”。
- `Shared builder, projected lanes`：先有 class/interface 共用的 callable builder，再把结果投影到 runtime、fallback、UHT、diagnostics；不要每条 lane 自己发明一套资格判断。
- `Structured pin identity beats reason-specific heuristics`：pin/category/subcategory/container 应该先变成结构化对象，再让 impact scan、reload helper、tests 复用；否则每加一个 family 都要补三遍 `if`.

### 改进路线建议（增量）

1. `P0`：补 `FAngelscriptInterfaceValueBridge`，先把 `FScriptInterface` dual-slot 读写、debugger 展示和 property materialization 接成独立 owner；同时把该 family 写进 gap/report 产物。
2. `P0`：新增 shared callable builder，并让 runtime bind scan、reflective fallback、UHT exporter 先以 report-only 方式共用它；优先把 interface `UFunction` 的可建性和失败原因显式化。
3. `P1`：把 `BlueprintImpact`、commandlet 与相关自动化迁到 structured `PinFamily` 服务，先补 interface/class pin 与 implementer-only Blueprint 三条 issue regression。
4. `P2`：等 `InterfaceValueBridge + shared callable builder + editor/test closure` 三件套稳定后，再决定是否把 `TScriptInterface<T>` 提升成公开脚本语法；在那之前，不建议先改 surface 再补 owner。

---
## 深化分析 (2026-04-09 07:57:25)

### 本轮新增关键发现

- `Bind_BlueprintType.cpp` 的正式 type adapter 已覆盖 `UObject`、`TSubclassOf`、`TObjectPtr`、`TWeakObjectPtr` 四族，但没有任何 `FInterfaceProperty` / `TScriptInterface<>` 的注册点；当前 `QuickScriptInterfaceCast()` 仍只是 `ImplementsInterface()` 布尔查询，不是 value bridge。
- 当前 interface 主线的自动化主要保护 `Cast<UInterface>` + method dispatch 与“fallback 必须拒绝 interface class”两件事；docs/debug/IDE/artifact 层还没有任何一条生产级 lane 会稳定产出 `TScriptInterface<IFoo>` 或显示 `FScriptInterface` 双槽状态。
- `ClassReloadHelper` 已经拿到了 `ReloadClasses`，但 pin rewrite / refresh lambda 仍只处理 `PC_Struct`、`PC_Enum`、`PC_Byte`；即使未来 `BlueprintImpact` 命中 interface/class family，真正 editor 修补仍不会发生。

### 差距矩阵（增量）

| 维度 | 本轮新增判断 | 差距等级 | 最佳参考方案 | 优先级 |
| --- | --- | --- | --- | --- |
| `D2 / D6` 反射绑定机制 / 代码生成与 IDE 支持 | interface family 在 wrapper/property adapter 层整族缺席 | `能力缺失` | `UnLua` `FInterfacePropertyDesc` + `puerts` `FInterfacePropertyTranslator` + `UnrealCSharp` `FInterfacePropertyDescriptor` | `P0` |
| `D5 / D6 / D9` 调试与开发体验 / 代码生成与 IDE 支持 / 测试基础设施 | 当前只保护 interface dispatch，不保护 interface value observability | `实现质量差异` | `UnLua` `UnLuaDebugBase` + `UnLuaIntelliSense` + `UnrealCSharp` `FGeneratorCore` | `P1` |
| `D7 / D4 / D9` 编辑器集成 / 热重载 / 测试基础设施 | reload helper 仍不会重写 class/interface pin identity，impact 命中后也缺修补 owner | `能力缺失` | `UnrealCSharp` `FDynamicClassGenerator` | `P1` |

### [D2 / D6] 反射绑定机制 / 代码生成与 IDE 支持补充：wrapper/property adapter 已覆盖 object/class family，唯独 interface family 仍是空洞

前文已经把 `InterfaceValueBridge` 收口成 `P0 owner`。本轮新增的证据是更靠底层的一层：**当前不是“interface 还有几个特判没补”，而是整个 wrapper/property adapter 家族里根本没有 interface 的正式席位**。现有 `Bind_BlueprintType.cpp` 已经把 `UObject`、`TSubclassOf`、`TObjectPtr`、`TWeakObjectPtr` 各自做成 `FAngelscriptType`，并给 `FProperty -> FAngelscriptTypeUsage` 建了 type finder；但同文件没有任何 `CastField<FInterfaceProperty>` 或 `TScriptInterface<>` 分支。与此同时，runtime 的 `CanCastScriptObjectToUnrealInterface()` 只返回一个 `bool`，它能证明“某对象实现了某接口”，却不承担 `FScriptInterface(Object, InterfacePtr)` 的 dual-slot 物化职责。

**当前状态**

- `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:117-134`：普通对象类型只会生成 `FObjectProperty` / `FClassProperty`。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1543-1606,1780-1868,2422-2515`：`TSubclassOf`、`TObjectPtr`、`TWeakObjectPtr` 已有完整的 `CreateProperty / MatchesProperty / RegisterTypeFinder`，但没有 interface family 对位实现。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:112-137`：`QuickScriptInterfaceCast` 仍只是 class relation 判定，不读写 `FScriptInterface`。

```
[Current AS] Interface Family Entry Points
Bind_BlueprintType
├─ FUObjectType -> FObjectProperty                 // UObject
├─ FSubclassOfType -> FClassProperty              // class wrapper
├─ FObjectPtrType -> FObjectProperty              // TObjectPtr
├─ FWeakObjectPtrType -> FWeakObjectProperty      // TWeakObjectPtr
└─ (missing) InterfaceValueBridge                 // 无 FInterfaceProperty / TScriptInterface

Runtime cast
└─ QuickScriptInterfaceCast -> bool only          // 只证明类关系，不承载 dual-slot value
```

[92] 当前 `TypeSystem` 已经有四条 object/class wrapper lane，但没有 interface 对位 lane：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp
// 位置: 117-134, 1543-1596, 1780-1855, 2422-2515
// 说明: 现有 wrapper/property/type-finder 只覆盖 object/class family
// ============================================================================
FProperty* CreateProperty(const FAngelscriptTypeUsage& Usage, const FPropertyParams& Params) const override
{
	// ★ UObject / UClass 路径只会落到 FObjectProperty / FClassProperty
	if (Class == UClass::StaticClass())
	{
		auto* Property = new FClassProperty(Params.Outer, Params.PropertyName, RF_Public);
		Property->PropertyClass = Class;
		Property->MetaClass = UObject::StaticClass();
		return Property;
	}

	auto* Property = new FObjectProperty(Params.Outer, Params.PropertyName, RF_Public);
	Property->PropertyClass = Class;
	return Property;
}

struct FSubclassOfType : TAngelscriptCppType<TSubclassOf<UObject>>
{
	FProperty* CreateProperty(const FAngelscriptTypeUsage& Usage, const FPropertyParams& Params) const override
	{
		auto* Property = new FClassProperty(Params.Outer, Params.PropertyName, RF_Public);
		Property->PropertyFlags |= CPF_UObjectWrapper;
		Property->SetMetaClass(Usage.SubTypes[0].GetClass());
		return Property; // ★ class wrapper
	}
};

struct FObjectPtrType : TAngelscriptCppType<TObjectPtr<UObject>>
{
	FProperty* CreateProperty(const FAngelscriptTypeUsage& Usage, const FPropertyParams& Params) const override
	{
		auto* Property = new FObjectProperty(Params.Outer, Params.PropertyName, RF_Public);
		Property->PropertyClass = Usage.SubTypes[0].GetClass();
		return Property; // ★ object wrapper
	}
};

auto SubclassOfType = MakeShared<FSubclassOfType>();
FAngelscriptType::Register(SubclassOfType);
auto ObjectPtrType = MakeShared<FObjectPtrType>();
FAngelscriptType::Register(ObjectPtrType);
auto WeakObjectPtrType = MakeShared<FWeakObjectPtrType>();
FAngelscriptType::Register(WeakObjectPtrType);

FAngelscriptType::RegisterTypeFinder([=](FProperty* Property, FAngelscriptTypeUsage& Usage) -> bool
{
	const FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property);
	const FWeakObjectProperty* WeakObjectProperty = CastField<FWeakObjectProperty>(Property);
	const FClassProperty* ClassProperty = CastField<FClassProperty>(Property);
	// ★ 源码里没有 CastField<FInterfaceProperty>(Property) 分支
	// ★ 也没有把 TScriptInterface<> materialize 回 FAngelscriptTypeUsage 的入口
	...
});
```

[93] runtime 侧对 interface 仍停在“类关系为真/假”，不是 value owner：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 112-137
// 说明: QuickScriptInterfaceCast 只做 ImplementsInterface() 判定
// ============================================================================
bool FAngelscriptEngine::CanCastScriptObjectToUnrealInterface(asITypeInfo* RuntimeType, asITypeInfo* TargetType, void* ObjectPtr)
{
	UClass* TargetClass = reinterpret_cast<UClass*>(TargetType->GetUserData());
	if (TargetClass == nullptr || !TargetClass->HasAnyClassFlags(CLASS_Interface))
	{
		return false;
	}

	UObject* Object = reinterpret_cast<UObject*>(ObjectPtr);
	UClass* ObjectClass = Object != nullptr ? Object->GetClass() : nullptr;
	const bool bImplementsInterface = ObjectClass != nullptr && ObjectClass->ImplementsInterface(TargetClass);
	return bImplementsInterface; // ★ 这里只有 bool，没有 FScriptInterface dual-slot 物化
}
```

[94] 三家参考虽然语言不同，但在 property/value owner 上做法高度一致：**都把 `FScriptInterface` 读写做成正式 adapter**：

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp
// 位置: 541-560, 568-575
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp
// 位置: 605-629
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/ObjectProperty/FInterfacePropertyDescriptor.cpp
// 位置: 29-45
// 说明: 三家都显式读写 FScriptInterface 的 object slot + interface slot
// ============================================================================
const FScriptInterface &Interface = InterfaceProperty->GetPropertyValue(ValuePtr);
UnLua::PushUObject(L, Interface.GetObject());

FScriptInterface *Interface = (FScriptInterface*)ValuePtr;
UObject *Value = UnLua::GetUObject(L, IndexInStack);
Interface->SetObject(Value);
Interface->SetInterface(Value ? Value->GetInterfaceAddress(InterfaceProperty->InterfaceClass) : nullptr);
// ★ UnLua：property owner 直接维护 dual-slot

const FScriptInterface& Interface = InterfaceProperty->GetPropertyValue(ValuePtr);
UObject* Object = Interface.GetObject();
return FV8Utils::IsolateData<IObjectMapper>(Isolate)->FindOrAdd(Isolate, Context, Object->GetClass(), Object);
...
FScriptInterface* Interface = reinterpret_cast<FScriptInterface*>(ValuePtr);
Interface->SetObject(Object);
Interface->SetInterface(Object ? Object->GetInterfaceAddress(InterfaceProperty->InterfaceClass) : nullptr);
// ★ puerts：translator 同样把 dual-slot 当成一等责任

const auto Interface = static_cast<FScriptInterface*>(Dest);
const auto Object = SrcMulti->GetObject();
Interface->SetObject(Object);
Interface->SetInterface(Object ? Object->GetInterfaceAddress(Property->InterfaceClass) : nullptr);
// ★ UnrealCSharp：descriptor 还顺手把 GC / identity 一并挂在同一 owner 上
```

**差距描述**

- `没有实现`：当前没有 `FInterfaceProperty -> FAngelscriptTypeUsage` 的正式 type finder，也没有 `TScriptInterface<>` 对位的 `FAngelscriptType` / property adapter。
- `实现方式不同`：当前 interface 主要存在于 `CLASS_Interface`、`ImplementedInterfaces` 与 `ImplementsInterface()` 这些 nominal/class-relation lane；参考实现把它直接拉进 property/value lane。
- `实现质量差异`：没有 dual-slot owner 时，`P10` 后续每开一条 lane 都会重新发明一套“object slot / interface slot 从哪里来”的规则，`Bind API GAP` 也只能继续把 interface 缺口伪装成 function GAP。

**参考方案**

- `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:533-575`
- `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/PropertyTranslator.cpp:598-629`
- `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/ObjectProperty/FInterfacePropertyDescriptor.cpp:1-53`

**吸收建议**

- 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/` 新增 `FAngelscriptInterfaceValueBridge`，把 `FScriptInterface` 的 `SetObject / SetInterface / GetObject / Identical` 收到同一 owner；这一步完全可以在插件内完成，不需要改引擎，也不依赖 AngelScript 2.38 全量升级。
- 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp` 为 `FInterfaceProperty` 增加正式 `RegisterTypeFinder()`，并引入一份 interface family 的 `FAngelscriptType`，首阶段只要求它能 materialize property 与 script callable arg/return。
- `Bind API GAP` 与后续 `SurfaceSupportProfile` 不要再把 interface family 摊平到 function 行；应新增 family-level 条目，例如 `InterfacePropertyMaterialization`、`InterfaceArgument`、`InterfaceReturn`，让主线能按 family rollout。

**优先级**

- `P0`
- **理由**：这不是外围优化，而是 `P10 UInterface` 是否能进入正式 type/property 主链的前置条件。没有它，后续 D6/D9/D7 的任何闭环都会继续建在旁路上。

### [D5 / D6 / D9] 调试与开发体验 / 代码生成与 IDE 支持 / 测试基础设施补充：当前只保护 interface dispatch，不保护 interface value observability

前文已经指出 interface family 缺 value owner；本轮新增的是另一层风险：**当前自动化和产物主要证明“interface 调用能不能跑”，却没有证明“interface 值在 docs/debug/IDE/artifact 里能不能被看见”**。`AngelscriptInterfaceNativeReferenceRoundTrip` 验证的是 `Cast<UInterface>` 后的方法调用和 `int&` roundtrip；`ReflectiveFallbackEligibility` 验证的是 interface 函数必须被拒绝；`DumpDocumentation()` 则直接用 `GetDecl(TypeId)` 生成属性声明。换句话说，生产环境里最需要看见 interface value 的几条观察面，今天还没有任何正式回归。

**当前状态**

- `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp:542-575`：文档导出的属性声明来自 script `TypeId`，不是 `FProperty` descriptor。
- `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceNativeTests.cpp:349-428`：当前“reference roundtrip”测试走的是 `Cast<UAngelscriptNativeParentInterface>(Self)` + method call，不是 `FInterfaceProperty` / `TScriptInterface<>` property roundtrip。
- `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptBlueprintCallableReflectiveFallbackTests.cpp:230-245`：当前明确回归的是 “`RejectedInterfaceClass` 必须成立”，不是 interface 替代 artifact。

```
[Current AS] Interface Observability
runtime
├─ QuickScriptInterfaceCast -> bool only          // 类关系
tests
├─ NativeReferenceRoundTrip -> Cast + method call // dispatch
└─ ReflectiveFallbackEligibility -> reject        // 策略拒绝
docs
└─ DumpDocumentation -> GetDecl(TypeId)           // 没有 FInterfaceProperty owner
```

[95] 当前 docs/test 保护的都是 dispatch / rejection，不是 interface value：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp
// 位置: 542-575
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceNativeTests.cpp
// 位置: 393-425
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptBlueprintCallableReflectiveFallbackTests.cpp
// 位置: 230-245
// 说明: 当前生产 artifact 与回归更关心 dispatch 是否能跑，不关心 interface value 是否可观察
// ============================================================================
int32 PropertyCount = ScriptType->GetPropertyCount();
for (int32 PropertyIndex = 0; PropertyIndex < PropertyCount; ++PropertyIndex)
{
	const char* Name;
	int TypeId;
	ScriptType->GetProperty(PropertyIndex, &Name, &TypeId);

	FDocProperty Prop;
	Prop.Declaration = GetDecl(TypeId) + TEXT(" ") + Prop.Name;
	// ★ 属性声明直接来自 script TypeId；如果 interface family 没有正式 script type，这里就无从表达
}

UObject Self = this;
UAngelscriptNativeParentInterface ParentRef = Cast<UAngelscriptNativeParentInterface>(Self);
if (ParentRef == nullptr)
	return;

int Value = 10;
ParentRef.AdjustNativeValue(5, Value);
ScriptAdjustedValue = Value;
// ★ 这里保护的是 Cast + method dispatch + int& roundtrip，不是 interface property/value bridge

TestEqual(
	TEXT("Reflective fallback should reject interface-class functions explicitly"),
	EvaluateReflectiveFallbackEligibility(InterfaceFunction),
	EAngelscriptReflectiveFallbackEligibility::RejectedInterfaceClass);
// ★ 当前回归的是“拒绝 interface callable”，不是“给 interface family 生成替代 artifact”
```

[96] 参考实现会把 interface value 同时接进 debugger、IntelliSense 和 declaration generator：

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaDebugBase.cpp
// 位置: 548-556
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSense.cpp
// 位置: 392-395
// 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp
// 位置: 144-149
// 说明: reference 把 interface value 的“怎么看见”也做成了一等 contract
// ============================================================================
FInterfaceProperty *InterfaceProperty = (FInterfaceProperty*)InProperty;
FString InterfaceTypeText = InterfaceProperty->GetCPPType(&InterfaceExtendedTypeText, 0);
const FScriptInterface &Interface = InterfaceProperty->GetPropertyValue(ValuePtr);
ReadableValue = FString::Printf(TEXT("%s%s: Object(0x%p), Interface(0x%p)"),
	*InterfaceTypeText, *InterfaceExtendedTypeText, Object, Interface.GetInterface());
// ★ UnLua debugger 同时显示 object slot 与 interface slot

const UClass* Class = ((FInterfaceProperty*)Property)->InterfaceClass;
return FString::Printf(TEXT("TScriptInterface<%s%s>"), Class->GetPrefixCPP(), *Class->GetName());
// ★ UnLua IntelliSense 会把 interface property 直接写成 TScriptInterface<IFoo>

if (const auto InterfaceProperty = CastField<FInterfaceProperty>(Property))
{
	return FString::Printf(TEXT("TScriptInterface<%s>"),
		*FUnrealCSharpFunctionLibrary::GetFullInterface(InterfaceProperty->InterfaceClass));
}
// ★ UnrealCSharp declaration generator 同样保留泛型形式，不会在 artifact 上把 interface family 抹平
```

**差距描述**

- `没有实现`：当前没有任何一条正式 artifact lane 会把 UE 的 `FInterfaceProperty` 输出成稳定的 interface type spelling。
- `实现方式不同`：当前 tests 偏向 `dispatch correctness`；参考实现则同步保护 `debug text`、`IDE type spellings`、`property descriptor`。
- `实现质量差异`：如果只保护 dispatch，不保护 observability，那么将来即使 `P10` 局部上线，文档、调试器、IDE 仍可能继续把 interface value 看成普通 object 或直接缺席，CI 也不会报警。

**参考方案**

- `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaDebugBase.cpp:548-556`
- `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSense.cpp:392-395`
- `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:144-149`

**吸收建议**

- 等 `FAngelscriptInterfaceValueBridge` 落地后，立即让 docs/debug/IDE/artifact 都从同一 owner 取值，不再各自发明 interface spelling。第一版至少统一到 `TScriptInterface<UFoo>` 或项目约定的 canonical spell。
- 在 `Plugins/Angelscript/Source/AngelscriptTest/Interface/` 新增三条 issue-scoped regression：`InterfacePropertyRoundTrip`、`InterfaceDebuggerValue`、`InterfaceDocsDeclaration`。它们要分别保护 property materialization、debug text 与文档/声明输出。
- `Bind API GAP` 的收口不要只看 `Direct/Stub/Rejected`；需要补一层 “artifact-visible / artifact-missing” 口径，否则主线会误以为 interface family 已经上线。

**优先级**

- `P1`
- **理由**：它依赖 `P0 InterfaceValueBridge` 先建立正式 owner，但必须紧跟其后；否则 `P10` 会进入“运行时局部可用、但 diagnostics/artifact 全盲”的危险区。

### [D7 / D4 / D9] 编辑器集成 / 热重载 / 测试基础设施补充：reload helper 还不会重写 class/interface pin object identity

前文已经指出 `BlueprintImpact` 和 pin family 的盲区；本轮新增的更深一层结论是：**就算 impact 扫描将来能命中 interface/class family，当前真正执行 Blueprint 修补的 `ClassReloadHelper` 也还不会改这些 pin**。它一开始就把 `ReloadClasses` 填进 `ClassReplaceList`，说明“类被替换”这件事已经被感知到了；但后续 `ReplacePinType()` 和 `CheckRefresh()` 仍只处理 `PC_Struct`、`PC_Enum`、`PC_Byte`。测试层也只验证 struct variable/pin。也就是说，当前 editor/reload/test 三条 lane 在 interface/class pin family 上还是同时缺位。

**当前状态**

- `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:49-81`：`ReloadClasses` 已经被收集进 `ClassReplaceList`，但 `ReplacePinType()` 不处理 class/interface pin。
- `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:208-221`：`CheckRefresh()` 同样只看 struct/enum。
- `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactScannerTests.cpp:360-402`：现有 variable/pin regression 只覆盖 struct。

```
[Current AS] Editor Reload Repair
ReloadClasses + ReloadStructs
├─ ReplacePinType()
│  ├─ PC_Struct -> rewrite object
│  └─ PC_Enum / PC_Byte -> refresh only
└─ CheckRefresh()
   ├─ PC_Struct -> yes
   └─ PC_Enum / PC_Byte -> yes

Missing lanes
├─ PC_Object / PC_Class                          // class pin
└─ interface-related PinSubCategoryObject        // interface pin
```

[97] 当前 editor repair 已经知道“class 被替换了”，但 pin 修补代码并不消费这个事实：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp
// 位置: 49-81, 208-221
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactScannerTests.cpp
// 位置: 360-402
// 说明: reload helper 与 regression 目前都只覆盖 struct/enum family
// ============================================================================
TMap<UObject*, UObject*> ClassReplaceList;
for (auto& Elem : ReloadClasses)
	ClassReplaceList.Add(Elem.Key, Elem.Value);
for (auto& Elem : ReloadStructs)
	ClassReplaceList.Add(Elem.Key, Elem.Value);
// ★ class replacement 已经被记录

auto ReplacePinType = [&](FEdGraphPinType& PinType) -> bool
{
	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
	{
		PinType.PinSubCategoryObject = *NewStruct;
		return true;
	}
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Enum || PinType.PinCategory == UEdGraphSchema_K2::PC_Byte)
	{
		return ReloadEnums.Contains((UEnum*)PinType.PinSubCategoryObject.Get());
	}
	else
	{
		return false; // ★ class/interface pin family 直接漏掉
	}
};

auto CheckRefresh = [&](FEdGraphPinType& PinType) -> bool
{
	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
		return NewlyCreatedStructs.Contains(Cast<UScriptStruct>(PinType.PinSubCategoryObject.Get()));
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Enum || PinType.PinCategory == UEdGraphSchema_K2::PC_Byte)
		return ReloadEnums.Contains((UEnum*)PinType.PinSubCategoryObject.Get());
	return false; // ★ refresh lane 同样没有 class/interface 分支
};

Variable.VarType.PinCategory = UEdGraphSchema_K2::PC_Struct;
Variable.VarType.PinSubCategoryObject = TBaseStructure<FVector>::Get();
...
Symbols.Structs.Add(TBaseStructure<FVector>::Get());
// ★ 当前 regression 只证明 struct variable / pin 可见
```

[98] `UnrealCSharp` 的参考点不在“它写了更多 `if`”，而在它让 class graph rebuild 与 pin identity repair 使用同一批对象：

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicClassGenerator.cpp
// 位置: 503-505, 656-676
// 说明: class/interface graph rebuild 与 PinSubCategoryObject repair 共享同一套 object identity
// ============================================================================
if (Pin->PinType.PinSubCategoryObject == InOldClass)
{
	Pin->PinType.PinSubCategoryObject = InNewClass;
}
// ★ pin repair 不关心“它是 struct 还是 interface”；关键是 object identity 是否换了

for (const auto Interface : InClassReflection->GetInterfaces())
{
	if (const auto InterfaceClass = LoadClass<UObject>(nullptr, *Interface->GetPathName()))
	{
		InClass->Interfaces.Emplace(InterfaceClass, 0, true);
	}
}
// ★ interface graph rebuild 与 pin repair 使用同一个 class object universe
```

**差距描述**

- `没有实现`：当前没有 class/interface-aware 的 pin rewrite / refresh owner。
- `实现方式不同`：当前是 “先收集 class replacement，再在 pin lambda 里忽略 class”；参考实现是 “class/interface graph rebuild 与 pin identity repair 共用同一批 object identity”。
- `实现质量差异`：若将来只补 `BlueprintImpact` 扫描而不补 `ClassReloadHelper`，结果会是“命中了受影响 Blueprint，但图内 pin 仍保留旧 identity，需要手动刷新”，测试也未必能捕获。

**参考方案**

- `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicClassGenerator.cpp:503-505`
- `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicClassGenerator.cpp:656-676`

**吸收建议**

- 不要在 `ClassReloadHelper` 继续堆 interface 特判；应让它消费同一份 `FAngelscriptBlueprintPinFamily` / `PinIdentity` 服务，把 `PinCategory + PinSubCategoryObject + ContainerType` 统一映射成可重写对象。
- `ReplacePinType()` 与 `CheckRefresh()` 至少补 `PinSubCategoryObject` 指向旧 `UClass` / interface `UClass` 的 case；实现上可以先从“object identity 相等则替换”开始，不要求一步到位覆盖所有 category 常量。
- 在 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/` 新增三条 regression：`ClassPinRewrite`、`InterfacePinRewrite`、`ImplementedInterfaceOnlyRefresh`。这三条要和 `BlueprintImpact` 扫描一起跑，不能只测扫描命中。

**优先级**

- `P1`
- **理由**：它不阻挡 `P0 InterfaceValueBridge` 自身落地，但直接决定 `P10` 上线后的 editor 可信度。若继续后置，最先暴露的问题会是 Blueprint 图里出现陈旧 pin / 手动刷新依赖，而不是编译期报错。

### 值得吸收的设计模式（增量）

- `Wrapper family completeness beats ad-hoc special cases`：既然 `UObject`、`TSubclassOf`、`TObjectPtr`、`TWeakObjectPtr` 都已经是正式 family，那么 `UInterface` 也应该拥有对位 family；不要继续靠 `ImplementsInterface()` 和字符串声明把它维持在旁路。
- `Observability must consume the same owner as materialization`：declaration、debugger、IntelliSense、docs 不该各自猜 interface spelling；它们都应消费同一个 `InterfaceValueBridge` / `TypeShape` owner。
- `Repair should follow identity, not enum-style category lists`：editor/reload 侧真正需要修的是 `PinSubCategoryObject` identity，而不是不断给 `PC_*` 列表补分支。

### 改进路线建议（增量）

1. `P0`：在 `P10 UInterface` 主线上优先补 `FAngelscriptInterfaceValueBridge + FInterfaceProperty type finder`，让 interface family 先进入正式 `type/property` lane；`Bind API GAP` 同步增加 family-level 账本。
2. `P1`：紧跟着把 docs/debug/IDE/artifact 都接到同一份 owner，并补 `InterfacePropertyRoundTrip`、`InterfaceDebuggerValue`、`InterfaceDocsDeclaration` 三条 regression，避免主线只剩“能跑但看不见”。
3. `P1`：把 `ClassReloadHelper` 从 `struct/enum-only` 修补升级到 `identity-based pin repair`，并让 `BlueprintImpact` / reload helper / editor tests 共用同一份 pin family service。
4. `P2`：等以上三步稳定后，再决定是否公开 `TScriptInterface<T>` 语法与更广的 interface callable/direct-bind rollout；在此之前，不建议先扩 surface 再补 artifact 与 editor 闭环。

---
## 深化分析 (2026-04-09 08:09:46)

### 本轮新增关键发现

- `D2 / D6`：当前真正缺的不是再补一个零散 `Cast` helper，而是 `FInterfaceProperty -> FAngelscriptTypeUsage` 的 canonical producer。`AngelscriptType` 已经能输出泛型 declaration，但 `Bind_BlueprintType` 从未为 interface family 产出 `SubTypes`。
- `D7 / D4`：`BlueprintImpact`、commandlet 与 reload helper 共享的是同一份 `Classes / Structs / Enums / Delegates` vocabulary；因此 interface 变更即使能在 runtime 生效，也无法在 editor/headless surface 上被解释成结构化 impact reason。
- `D6 / D9`：`Bind API GAP` 目前仍是 function row + failure reason 账本，而 `ImplementedInterfaces` 还只在 class dump 里以 joined string 形式存在。没有 type-family authority，`P10 UInterface` 即使局部落地，也无法稳定回答“哪一层已经支持 interface family，哪一层还没有 owner”。

### 差距矩阵（增量）

| 维度 | 本轮结论 | 差距等级 | 核心证据 |
| --- | --- | --- | --- |
| `D2 / D6` | interface generic grammar 没有 canonical producer | `能力缺失` | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:147-168,174-193,234-257`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:2440-2514` |
| `D7 / D4` | impact scan / commandlet 没有 interface relation vocabulary | `能力缺失` | `Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h:13-31`，`Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp:44-56,112-237`，`Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:83-105` |
| `D6 / D9` | `Bind API GAP` 仍是 function-centric ledger，看不见 type family | `实现质量差异` | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:1101-1109,1272-1299`，`Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp:463-492`，`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:224-260` |

### [D2 / D6] 反射绑定机制 / 代码生成与 IDE 支持补充：interface generic grammar 仍没有 canonical producer

前文已经把重点放在 `InterfaceValueBridge`、callable lane 和 `FScriptInterface` 双槽值桥上。本轮新增的更前置结论是：**当前 Angelscript 其实已经拥有“泛型 declaration writer”，但没有任何正式 producer 会把 `FInterfaceProperty` 物化成带 `SubTypes` 的 `FAngelscriptTypeUsage`**。这解释了为什么 interface family 会同时在 docs、UHT、IDE 与 runtime 上反复分裂成不同旁路。

**当前状态**

- `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:147-168`：`GetByProperty()` 的 authority 先查 `TypeFinders`，再退回 `MatchesProperty()`；也就是说，property family 想进入统一类型系统，必须先有显式 producer。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:174-193`：`GetAngelscriptDeclaration()` 已经能稳定输出 `Type<SubType>` 形状，writer 不是缺口。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp:234-257`：`FromProperty()` 只消费 finder 产物与通用 fallback，不会自己发明 interface family。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:2440-2514`：当前 finder 只覆盖 `FObjectProperty`、`FWeakObjectProperty`、`FClassProperty`、`TSubclassOf<>` 与普通 object property，没有 `FInterfaceProperty` 分支。

```
[Current AS] Interface Type Grammar
FProperty
├─ TypeFinder(FObject/FWeak/FClass)               // 产出 Usage.Type + Usage.SubTypes
├─ FromProperty()                                 // 只消费 finder / property matcher
└─ GetAngelscriptDeclaration()                    // 能写 Type<SubType>，但不负责生成 interface family

Missing
└─ FInterfaceProperty -> canonical Usage producer // 这一步今天不存在

[Reference] Interface Family
FInterfaceProperty
├─ declaration -> TScriptInterface<T>             // 统一拼写
├─ property bridge -> SetObject + SetInterface    // 双槽值桥
└─ arg / return -> same family                    // 同一族贯穿 runtime/toolchain
```

关键源码 [99]：当前只有 generic writer，没有 interface producer

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp
// 位置: 147-168, 174-193, 234-257
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp
// 位置: 2440-2514
// 说明: 当前系统已经能写泛型声明，但没有任何 `FInterfaceProperty` producer
// ============================================================================
for (auto& Finder : Database.TypeFinders)
{
	if (Finder(Property, Usage))
		break;
}
// ★ property family 想进入主链，先要有人把它生产成 Usage

if (!Usage.Type.IsValid())
{
	Usage.Type = FAngelscriptType::GetByProperty(Property, false);
}
// ★ 找不到 TypeFinder 时，只能退回 generic matcher；没有 interface 专属入口

FString Decl = GetAngelscriptTypeName(Usage);
if (Usage.SubTypes.Num() != 0)
{
	Decl += TEXT("<");
	Decl += Usage.SubTypes[i].GetAngelscriptDeclaration(InnerMode);
	Decl += TEXT(">");
}
// ★ writer 已经能输出 `Type<SubType>`，它缺的不是语法能力，而是上游 producer

const FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property);
...
const FWeakObjectProperty* WeakObjectProperty = CastField<FWeakObjectProperty>(Property);
...
const FClassProperty* ClassProperty = CastField<FClassProperty>(Property);
...
if (ClassProperty != nullptr && (ClassProperty->PropertyFlags & CPF_UObjectWrapper) != 0)
{
	Usage.Type = SubclassOfType;
	Usage.SubTypes[0] = InnerType;
	return true;
}
// ★ finder 明确照顾了 TObjectPtr / TWeakObjectPtr / TSubclassOf / object，
//   但没有 `FInterfaceProperty` 对位 family
```

**差距描述**

- `没有实现`：当前没有任何正式 owner 把 `FInterfaceProperty` 生产成稳定的 `FAngelscriptTypeUsage + SubTypes`。
- `实现方式不同`：当前只有 downstream generic writer；参考实现则把 interface family 视为 upstream canonical type，由 declaration / property bridge / arg / return 共用。
- `实现质量差异`：如果没有 canonical producer，后续 docs/debug/UHT/IDE 就只能各自猜拼写，或者继续把 interface value 压回 `UObject` / `FunctionName` / raw string。

**参考方案**

- `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSense.cpp:392-395`
- `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp:533-580`
- `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:144-150`
- `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/ObjectProperty/FInterfacePropertyDescriptor.cpp:4-55`
- `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Domain/InternalCall/FRegisterScriptInterface.cpp:11-62`
- `Reference/UnrealCSharp/Source/UnrealCSharp/Public/Binding/Function/TArgument.inl:259-263`
- `Reference/UnrealCSharp/Source/UnrealCSharp/Public/Binding/Function/TReturnValue.inl:130-134`

关键源码 [100]：参考实现把 interface family 做成 declaration/bridge/arg/return 的统一 owner

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp
// 位置: 144-150
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Reflection/Property/ObjectProperty/FInterfacePropertyDescriptor.cpp
// 位置: 4-55
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Domain/InternalCall/FRegisterScriptInterface.cpp
// 位置: 11-62
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Public/Binding/Function/TArgument.inl
// 位置: 259-263
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Public/Binding/Function/TReturnValue.inl
// 位置: 130-134
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSense.cpp
// 位置: 392-395
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/ReflectionUtils/PropertyDesc.cpp
// 位置: 541-559
// 说明: reference 把 interface spelling、值桥和调用面统一收进一条 family
// ============================================================================
if (const auto InterfaceProperty = CastField<FInterfaceProperty>(Property))
{
	return FString::Printf(TEXT("TScriptInterface<%s>"),
		*FUnrealCSharpFunctionLibrary::GetFullInterface(InterfaceProperty->InterfaceClass));
}
// ★ declaration generator 直接输出 `TScriptInterface<T>`

Interface->SetObject(Object);
Interface->SetInterface(Object ? Object->GetInterfaceAddress(Property->InterfaceClass) : nullptr);
// ★ property bridge 同时维护 object slot 与 interface slot

FClassBuilder(TEXT("TScriptInterface"), NAMESPACE_LIBRARY)
	.Function("Register", RegisterImplementation)
	.Function("Identical", IdenticalImplementation)
	.Function("UnRegister", UnRegisterImplementation)
	.Function("GetObject", GetObjectImplementation);
// ★ lifecycle API 也以 family 为单位注册

struct TArgument<T, std::enable_if_t<TIsTScriptInterface<std::decay_t<T>>::Value, T>> : TMultiArgument<T> {};
struct TReturnValue<T, std::enable_if_t<TIsTScriptInterface<std::decay_t<T>>::Value>> : TCompoundReturnValue<T> {};
// ★ arg / return 不再各自发明 interface 特判

const UClass* Class = ((FInterfaceProperty*)Property)->InterfaceClass;
return FString::Printf(TEXT("TScriptInterface<%s%s>"), Class->GetPrefixCPP(), *Class->GetName());
// ★ UnLua IntelliSense 即使更动态，也仍然保留统一 spelling

const FScriptInterface &Interface = InterfaceProperty->GetPropertyValue(ValuePtr);
UnLua::PushUObject(L, Interface.GetObject());
Interface->SetObject(Value);
Interface->SetInterface(Value ? Value->GetInterfaceAddress(InterfaceProperty->InterfaceClass) : nullptr);
// ★ UnLua property desc 至少把 `FInterfaceProperty` 视作独立 family，而不是普通 object
```

**吸收建议**

- 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/` 内把前文已提出的 `InterfaceValueBridge` 再前推半步，给它正式 `TypeFinder / Declaration / Property / Arg / Return / Debug / CppForm` owner；第一版完全可以插件内完成，不需要改引擎，也不依赖 AngelScript `2.38` 全量升级。
- 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` 新增 `FInterfaceProperty` finder，先只要求 `FInterfaceProperty`、function arg、function return 三个 surface 共用同一 canonical spell；容器支持可以后续跟进。
- canonical spell 建议直接采用 `TScriptInterface<UFoo>` 或项目确认后的等价写法，但要保证 docs / UHT / debug / IDE / runtime 都消费同一个 producer，而不是五处各写一份 if-else。

**优先级**

- `P0`
- **理由**：这是 `P10 UInterface` 能否进入正式 `type/property` 主链的最前置 owner 之一；如果继续只补 runtime helper，不补 canonical producer，`Bind API GAP` 会继续把 interface family 误判成“局部已支持”。

### [D7 / D4] 编辑器集成 / 热重载补充：`BlueprintImpact` 仍没有 interface relation vocabulary

前文已经指出 `ClassReloadHelper` 不会修 class/interface pin identity。本轮新增的更前置结论是：**在 pin repair 之前，`BlueprintImpact` 自己就还没有 interface relation vocabulary**。也就是说，今天 commandlet、reload helper 与任何潜在 UI 面板都无法说出“这个 Blueprint 是因为实现了已变更 interface 才被命中”。

**当前状态**

- `Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h:13-31`：`EBlueprintImpactReason` 只有 `ScriptParentClass / NodeDependency / PinType / VariableType / DelegateSignature / ReferencedAsset` 六类；`FBlueprintImpactSymbols` 只有 `Classes / Structs / Enums / Delegates / ReplacementObjects`。
- `Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp:44-56`：`MatchesPinType()` 只认 `PC_Struct` 与 `PC_Enum / PC_Byte`。
- `Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp:112-237`：`BuildImpactSymbols()` 与 `AnalyzeLoadedBlueprint()` 都没有 interface-specific 分支；唯一和 class 相关的 reason 只是 `ParentClass->IsChildOf(ImpactedClass)`。
- `Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp:83-105`：reload helper 手工拼装的 `ImpactSymbols` 复用同一 vocabulary，因此 blind spot 会自动传播到 hot reload。
- `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactScannerTests.cpp:290-490`：现有回归覆盖 `class parent / struct symbol / struct pin / variable / delegate signature`，没有 interface-aware case。

```
[Current AS] Interface Impact Vocabulary
changed scripts
├─ BuildImpactSymbols -> Classes / Structs / Enums / Delegates
├─ AnalyzeLoadedBlueprint -> Parent / Dependency / Pin / Variable / Delegate / Asset
└─ ClassReloadHelper / Commandlet -> consume same reason set

Missing
├─ ImplementedInterface reason
├─ InterfacePinType reason
└─ Interface-only blueprint match

[Reference]
UClass::Interfaces
├─ GetAllInterfaceClasses()                        // 可枚举 interface graph
└─ GeneratorInterface()                            // 可重建 interface graph
```

关键源码 [101]：当前 scanner/reload/test 三条 lane 共用同一份“无 interface vocabulary”合同

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h
// 位置: 13-31
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp
// 位置: 44-56, 112-237
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp
// 位置: 83-105
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptBlueprintImpactScannerTests.cpp
// 位置: 290-309, 351-402, 453-490
// 说明: scanner / commandlet / reload helper / regression 目前都没有 interface reason
// ============================================================================
enum class EBlueprintImpactReason : uint8
{
	ScriptParentClass,
	NodeDependency,
	PinType,
	VariableType,
	DelegateSignature,
	ReferencedAsset,
};

struct FBlueprintImpactSymbols
{
	TSet<UClass*> Classes;
	TSet<UScriptStruct*> Structs;
	TSet<UEnum*> Enums;
	TSet<UDelegateFunction*> Delegates;
	TMap<UObject*, UObject*> ReplacementObjects;
};
// ★ reason 和 symbol 都没有 interface family 的一等位置

if (PinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
{
	return Symbols.Structs.Contains(Cast<UScriptStruct>(PinType.PinSubCategoryObject.Get()));
}
if (PinType.PinCategory == UEdGraphSchema_K2::PC_Enum || PinType.PinCategory == UEdGraphSchema_K2::PC_Byte)
{
	return Symbols.Enums.Contains(Cast<UEnum>(PinType.PinSubCategoryObject.Get()));
}
return false;
// ★ pin matcher 天然看不见 interface/class pin

if (ClassDesc->Class != nullptr)
{
	Symbols.Classes.Add(ClassDesc->Class);
}
...
if (Symbols.Classes.Contains(Cast<UClass>(Dependency)) || Symbols.Structs.Contains(Cast<UScriptStruct>(Dependency)))
{
	AddUniqueReason(OutReasons, EBlueprintImpactReason::NodeDependency);
}
// ★ 命中条件只有 class/struct/delegate/asset，没有 interface relation

ImpactSymbols.Classes.Add(ReloadClass.Key);
ImpactSymbols.Structs.Add(ReloadStruct.Key);
ImpactSymbols.Enums.Add(ReloadEnum);
ImpactSymbols.Delegates.Add(ReloadDelegate.Key);
// ★ reload helper 直接复用同一 vocabulary，blind spot 会同步传播

Module->Classes.Add(MakeClassDesc(TEXT("BlueprintImpactActor"), AActor::StaticClass()));
Module->Classes.Add(MakeStructDesc(TEXT("BlueprintImpactStruct"), TBaseStructure<FVector>::Get()));
Module->Enums.Add(MakeEnumDesc(TEXT("BlueprintImpactEnum"), StaticEnum<EAutoReceiveInput::Type>()));
...
Variable.VarType.PinCategory = UEdGraphSchema_K2::PC_Struct;
...
Symbols.Delegates.Add(SignatureFunction);
// ★ regression 目前只保护 class parent、struct symbol/pin/variable、delegate signature
```

**差距描述**

- `没有实现`：当前没有 `ImplementedInterface` / `InterfacePinType` / `InterfaceOnlyDependency` 这类 reason，也没有对位的 symbol owner。
- `实现方式不同`：当前 scanner 尝试把 interface 影响面摊平到 class/struct/delegate；参考实现则把 `UClass::Interfaces` 当成可遍历 object graph，而不是晚期猜测。
- `实现质量差异`：即便后续补了 runtime 与 pin repair，只要 `BlueprintImpact` vocabulary 不升级，headless commandlet、后续 editor 面板和自动化日志仍然无法向用户解释 “为什么这个 Blueprint 受 interface 变更影响”。

**参考方案**

- `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Template/TFieldIteratorExt.inl:142-167`
- `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicClassGenerator.cpp:656-676`

关键源码 [102]：reference 先把 interface relation 做成可遍历图，再消费到 class generation

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Template/TFieldIteratorExt.inl
// 位置: 142-167
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicClassGenerator.cpp
// 位置: 656-676
// 说明: reference 让 interface relation 先成为一等 graph，再进入生成与编辑器侧
// ============================================================================
static TArray<UClass*> GetAllInterfaceClasses(const UClass* InClass)
{
	TArray<UClass*> InterfaceClasses;
	for (const FImplementedInterface& ImplementedInterface : InClass->Interfaces)
	{
		if (ImplementedInterface.Class != nullptr)
		{
			UClass* CurrentClass = ImplementedInterface.Class;
			while (CurrentClass &&
				CurrentClass->HasAnyClassFlags(CLASS_Interface) &&
				!InterfaceClasses.Contains(CurrentClass))
			{
				InterfaceClasses.Add(CurrentClass);
				CurrentClass = CurrentClass->GetSuperClass();
			}
		}
	}
	return InterfaceClasses;
}
// ★ 先把 interface graph 做成稳定可枚举的数据结构

for (const auto Interface : InClassReflection->GetInterfaces())
{
	if (const auto InterfaceClass = LoadClass<UObject>(nullptr, *Interface->GetPathName()))
	{
		if (InterfaceClass != UInterface::StaticClass())
		{
			InClass->Interfaces.Emplace(InterfaceClass, 0, true);
		}
	}
}
// ★ class generation 消费的是 interface graph，不是临时字符串规则
```

**吸收建议**

- 不要直接在 `MatchesPinType()` 里继续堆 `if (PC_Interface)`；先把 `FBlueprintImpactSymbols` 升级为结构化 `InterfaceRelations`/`InterfaceClasses`/`InterfacePins` owner，再让 `AnalyzeLoadedBlueprint()`、commandlet 与 reload helper 共用。
- `BuildImpactSymbols()` 可以先消费项目现有数据：`ClassDesc->bIsInterface`、`ClassDesc->ImplementedInterfaces`、已生成的 interface `UClass`。这一步完全可以在插件内完成，不需要改引擎。
- 建议新增至少两个 reason：`ImplementedInterface` 与 `InterfacePinType`。前者负责“Blueprint 自己或其 parent class 实现了已变更 interface”，后者负责图内 pin/variable/object identity 命中。
- 在 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/` 补三条 issue-scoped regression：`BuildImpactSymbolsInterface`、`AnalyzeImplementedInterface`、`AnalyzeInterfacePinType`；commandlet 结果也要能透出这些 reason 名称。

**优先级**

- `P1`
- **理由**：它不先于 `P0 Interface family owner`，但必须和 `P10 UInterface` 的 editor rollout 同步推进；否则 runtime 能用、editor/headless 说不清影响范围，会直接削弱主线可信度。

### [D6 / D9] 代码生成与测试基础设施补充：`Bind API GAP` 仍没有 type-family authority

前文已经多次指出 function-centric 的 `Direct / Stub / FailureReason` 看不见 interface family。本轮新增的更底层证据是：**这个问题并不只出在 CSV 最后几列，而是从预处理 descriptor、state dump、UHT generator 到 regression header 都没有把 type family 建成一等 key**。

**当前状态**

- `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:1101-1109`：`FAngelscriptClassDesc` 只用 `bIsInterface`、`ImplementedInterfaces` 和 `InterfaceMethodDeclarations` 描述 interface，owner 仍是 bool/string/raw declaration。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:1272-1299`：`FAngelscriptModuleDesc` 暴露的是 `Classes / Enums / Delegates` 三类编译产物，没有 type-family ledger。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp:463-492`：`Classes.csv` 只是把 `ImplementedInterfaces` `JoinStrings()` 后输出。
- `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:224-260`：`AS_FunctionTable_*` 只输出 module/function 级 `Direct / Stub / EntryKind / EraseMacro`。
- `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp:636-748`：现有 regression 只锁 header、entry row 与 `FailureReason,SkippedCount` 聚合，完全没有 family-level assert。

```
[Current AS] Bind API GAP Ledger
preprocess / class desc
├─ bIsInterface + ImplementedInterfaces(strings)
├─ InterfaceMethodDeclarations(raw declarations)
└─ ModuleDesc -> Classes / Enums / Delegates

dump / UHT / tests
├─ Classes.csv -> JoinStrings(ImplementedInterfaces)
├─ ModuleSummary.csv -> DirectBindEntries / StubEntries
├─ Entries.csv -> ClassName / FunctionName / EntryKind
└─ SkippedReasonSummary.csv -> FailureReason / SkippedCount

Missing
└─ TypeFamily / Surface / Owner / Decision
```

关键源码 [103]：当前 artifact/test contract 从上到下都是 function-centric 或 string-centric

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h
// 位置: 1101-1109, 1272-1299
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp
// 位置: 463-492
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs
// 位置: 224-260
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp
// 位置: 636-748
// 说明: interface family 目前既不是 descriptor key，也不是 artifact/test key
// ============================================================================
bool bIsInterface = false;
TArray<FString> ImplementedInterfaces;
TArray<FString> InterfaceMethodDeclarations;
// ★ interface 还停在 bool/string/raw declaration 描述层

TArray<TSharedRef<FAngelscriptClassDesc>> Classes;
TArray<TSharedRef<FAngelscriptEnumDesc>> Enums;
TArray<TSharedRef<FAngelscriptDelegateDesc>> Delegates;
// ★ module artifact 也还没有 type family 的独立收口

Writer.AddRow({
	...,
	JoinStrings(ClassDesc->ImplementedInterfaces),
	...
});
// ★ dump 只能看见“拼起来的 interface 名字”，看不见结构化 family 状态

builder.AppendLine("ModuleName,EditorOnly,TotalEntries,DirectBindEntries,StubEntries,DirectBindRate,StubRate,ShardCount");
builder.AppendLine("ModuleName,EditorOnly,ClassName,FunctionName,EntryKind,EraseMacro,ShardIndex");
// ★ UHT 账本只统计函数行和 module 行

TestEqual(..., TEXT("ModuleName,EditorOnly,ClassName,FunctionName,EntryKind,EraseMacro,ShardIndex"));
TestEqual(..., TEXT("ModuleName,ClassName,FunctionName,FailureReason"));
TestEqual(..., TEXT("FailureReason,SkippedCount"));
// ★ regression 也只锁 function/failure vocabulary，没有 family-level assert
```

**差距描述**

- `没有实现`：当前没有 `TypeFamily`、`Surface`、`Owner`、`Decision` 这类一等 artifact 列，也没有对应回归。
- `实现方式不同`：当前 truth 被拆成 `bool/string dump + function csv + failure reason`；参考实现则先有 family registry，再让 generator/bridge/tooling 统一消费。
- `实现质量差异`：只要账本继续 function-centric，`Bind API GAP` 就无法明确回答 interface gap 究竟在 parser、property、arg/return、editor impact 还是 docs/debug；CI 只能看到 stub rate 变化，无法看到 “哪种 family 还没 owner”。

**参考方案**

- `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Reflection/FReflectionRegistry.cpp:55-60`
- `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:81-89`
- `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp:144-150`

关键源码 [104]：reference 先建 family registry，再把它喂给 bridge 与 generator

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Reflection/FReflectionRegistry.cpp
// 位置: 55-60
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp
// 位置: 81-89
// 文件: Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp
// 位置: 144-150
// 说明: reference 让 interface 先成为 registry/bridge 的一等 family，再进入生成链
// ============================================================================
TScriptInterfaceClass = GetClass(
	COMBINE_NAMESPACE(NAMESPACE_ROOT, NAMESPACE_CORE_UOBJECT), GENERIC_T_SCRIPT_INTERFACE);
// ★ registry 层先拿到 canonical family class

if (TypeDefinition->IsAssignableFrom(FReflectionRegistry::Get().GetTScriptInterfaceClass()))
{
	return EPropertyTypeExtent::Interface;
}
// ★ bridge 层再把它映射成稳定 property extent

if (const auto InterfaceProperty = CastField<FInterfaceProperty>(Property))
{
	return FString::Printf(TEXT("TScriptInterface<%s>"),
		*FUnrealCSharpFunctionLibrary::GetFullInterface(InterfaceProperty->InterfaceClass));
}
// ★ generator 只是消费 family authority，不再重新发明 interface 判定
```

**吸收建议**

- 把前文已提出的 `TypeShape / SurfaceSupportProfile` 再落成真正 artifact key：建议最小集合至少包含 `TypeFamily`、`Surface`、`Owner`、`Decision`、`FailureReason`。`InterfaceProperty`、`InterfaceArgument`、`InterfaceReturn`、`ImplementedInterfaceImpact` 应该成为第一批行。
- 不必一开始就废弃 `AS_FunctionTable_*`；第一阶段可并行新增 `AS_TypeFamilySupport.csv/json`，由 `AngelscriptUHTTool` 或 runtime dump 生成。等 schema 稳定，再决定是否并入现有 summary。
- `GeneratedFunctionTableTests.cpp` 应新增 family-level regression，至少锁住 `TypeFamily` header、`InterfaceProperty` 行存在性，以及 family-level `FailureReason` 不得为空。
- `Classes.csv` 建议追加结构化 interface 产物，例如单独 `Interfaces.csv` 或 `ImplementedInterfacesCount / InterfaceMethodCount`，避免继续只有 `JoinStrings()` 这类肉眼友好但机器不可 join 的列。

**优先级**

- `P0`
- **理由**：这正对当前主线里的 `Bind API GAP` 补齐。如果没有 family-level authority，`P10 UInterface` 即使开始 rollout，也没有可信的 artifact/test 体系来护航，只能靠人工阅读零散日志和 CSV。

### 值得吸收的设计模式（增量）

- `Canonical producer before canonical spelling`：先确保 `FInterfaceProperty` 能稳定产出同一份 `TypeUsage`，再谈 docs/debug/IDE 用什么 spelling。`Reference/UnrealCSharp` 的做法是 registry/bridge 先行，`Reference/UnLua` 则至少保证 property desc 与 IntelliSense 都承认 `FInterfaceProperty` 是独立 family。
- `Impact systems need relationship vocabulary, not only replacement objects`：只靠 `ReplacementObjects` 或 `ParentClass->IsChildOf()` 无法表达 interface 影响面；关系图要先成为一等数据，后续 commandlet、reload、UI 才能共享。
- `Metrics should be keyed by family + surface + owner`：`Direct/Stub/FailureReason` 适合函数，不适合 type rollout。要让 `Bind API GAP` 真正服务主线，统计键必须升级到 family 级。

### 改进路线建议（增量）

1. `P0`：把 `FInterfaceProperty` 正式接进 `FAngelscriptTypeUsage` 主链，给 interface family 建立 canonical producer；第一版只要求覆盖 property / arg / return / declaration 四个 surface，不改引擎。
2. `P0`：同步为 `Bind API GAP` 新增 family-level artifact/test contract；哪怕先以独立 `AS_TypeFamilySupport.csv/json` 落地，也要让 `InterfaceProperty`、`InterfaceArgument`、`InterfaceReturn` 拥有可追踪 owner。
3. `P1`：升级 `BlueprintImpact` vocabulary，把 `ImplementedInterface` / `InterfacePinType` 变成正式 reason，并让 commandlet、reload helper 与 editor tests 共用同一份 interface relation graph。
4. `P2`：待 `producer + ledger + impact vocabulary` 三件套稳定后，再决定是否扩大公开 authoring surface，例如 `TScriptInterface<T>` 用户语法、更多容器支持与 editor-native UI；在此之前不建议先扩 surface。

---

## 深化分析 (2026-04-09 08:22:26)

本轮不重复前文已经收口的 `ScriptInterfaceType / SurfaceSupportProfile / PropertyShape / InterfaceValueBridge` 结论。新增补充只聚焦三个仍未被写透的后置 lane：**调试值检查**、**Precompiled/JIT subtype schema**、以及**对应的自动化盲区**。

### 执行摘要

- `D5` 的新增问题不是“还没有 debugger”，而是 debugger 仍在 **live request 时临时重建类型真相**。`FAngelscriptDebugDatabase` 线上负载只是一个 `FString Database`，局部变量与表达式求值又回到 `FAngelscriptTypeUsage::FromTypeId()/FromClass()` 现推，因此 interface/wrapper family 即使在 runtime 主链补齐，也还会在调试 lane 再次被压扁。
- `D8/D11` 目前存在第二套未纳入前文路线图的 subtype schema。`PrecompiledData.cpp` 用 `Ref.SubTypes[i]` 位置化保存 template subtype，`StaticJIT.cpp` 又直接假设 `templateSubTypes[0]` 决定 size。也就是说，只修 `Binds.Cache` 与 live `TypeShape`，还不足以让 cooked/JIT lane 获得 `TScriptInterface<>` 级别的一致语义。
- `D9` 当前自动化更像“控制面烟雾测试”，而不是 “schema 回归”。Debugger tests 锁 breakpoint/callstack，dump tests 锁文件存在与 summary status；`RequestDebugDatabase/RequestVariables/RequestEvaluate` 的 payload 结构，以及 `PrecompiledData/StaticJIT` 的 subtype 行为，都还没有被回归资产钉住。

### 差距矩阵

| 维度 | 本轮新增聚焦点 | 差距等级 | 主要证据 | 优先级 |
| --- | --- | --- | --- | --- |
| D5 调试与开发体验 | 调试值检查仍依赖 `live TypeUsage re-inference` 与字符串型 `DebugDatabase` | 实现质量差异 | `AngelscriptDebugServer.h:185-193,536-553`、`AngelscriptDebugServer.cpp:1669-1673,2435-2495,2718-2805` | P1 |
| D8/D11 性能与优化 / 部署与打包 | `PrecompiledData + StaticJIT` 仍维护一套位置化 `templateSubTypes` schema | 实现质量差异 | `StaticJIT/PrecompiledData.cpp:1664-1822`、`StaticJIT/AngelscriptStaticJIT.cpp:2856-2884` | P2 |
| D9 测试基础设施 | debugger/dump 自动化还没有锁住 `DebugDatabase / Variables / Precompiled subtype schema` | 能力缺失 | `AngelscriptDebuggerTestClient.h:68-80`、`AngelscriptDebuggerBreakpointTests.cpp:324-432`、`AngelscriptDumpTests.cpp:45-75,202-250` | P1 |

### [D5] 调试与开发体验补充：值检查 lane 仍在“重新猜类型”，而不是消费 descriptor

前文已经覆盖了 `RequestDebugDatabase` 的 producer 生命周期和 `DebugDatabase` 的幽灵 symbol 风险。本轮新增的更底层结论是：**即便 symbol 生命周期修好，value inspection lane 依然会因为没有 descriptor owner 而继续误报/漏报 interface family**。

**当前状态**

- `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h:185-193` 显示 `FAngelscriptDebugDatabase` 负载只是一个 `FString Database`；`536-553` 的 settings 也只有 5 个布尔位，没有 `ValueFamily / SymbolRevision / TypeShapeRevision`。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:1669-1673` 在生成参数类型文本时，只有当 `ObjType->templateSubTypes.GetLength() == 0` 才追加 `const &in`，说明 debug schema 仍把“无 template subtype 的 object”当特殊主路径。
- 同文件 `2435-2495,2718-2805` 的 `RequestEvaluate / RequestVariables` 路径都先回到 `FAngelscriptTypeUsage::FromClass()` 或 `FromTypeId()`，再调用 `GetDebuggerValue()` / `GetDebuggerScope()`。这意味着 debugger 读取变量值时并没有消费结构化 descriptor，而是临场现推一次 family。

```
[D5-Deep] Value Inspection Flow
RequestDebugDatabase
├─ SendDebugDatabase()
│  ├─ build JSON in memory
│  └─ send FString Database                    // 线载荷仍是单字符串

RequestVariables / RequestEvaluate
├─ FromClass() / FromTypeId()                 // live 现推 TypeUsage
├─ GetDebuggerValue() / GetDebuggerScope()
└─ stringify declaration with heuristics      // `templateSubTypes==0` 特判

Missing
├─ descriptor-backed value lookup
├─ interface slot / wrapper role metadata
└─ value-lane revision key
```

关键源码 [105]：当前 debug payload 与 value inspection 仍是“字符串 + live 现推”

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h
// 位置: 185-193, 536-553
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 位置: 1669-1673, 2435-2495, 2718-2805
// 说明: DebugDatabase 是字符串；变量值与参数类型文本都在请求时临时重建
// ============================================================================
struct FAngelscriptDebugDatabase : FDebugMessage
{
	FString Database; // ★ IDE 侧看到的是一整块字符串，不是带 revision 的 descriptor graph
};

struct FAngelscriptDebugDatabaseSettings : FDebugMessage
{
	bool bAutomaticImports = false;
	bool bFloatIsFloat64 = false;
	bool bUseAngelscriptHaze = false;
	bool bDeprecateStaticClass = false;
	bool bDisallowStaticClass = false;
	// ★ 这里只有环境布尔位，没有 `SymbolRevision / TypeShapeRevision`
};

FString Decl = GetDecl(ParamType, &ParamFlags);
if ((ParamFlags & (asTM_INOUTREF | asTM_CONST)) == (asTM_INREF | asTM_CONST)
	&& ObjType != nullptr && ObjType->templateSubTypes.GetLength() == 0)
{
	Decl = TEXT("const ") + Decl + TEXT("&in");
	// ★ const ref 的 spelling 只对“没有 template subtype 的 object”做特判
}

auto Usage = FAngelscriptTypeUsage::FromClass(UASClass::GetFirstASOrNativeClass(StackFrameObject->GetClass()));
if (Usage.GetDebuggerValue(Address, CurrentValue))
{
	bValidValue = true;
}

auto Usage = FAngelscriptTypeUsage::FromTypeId(Context->GetVarTypeId(i, Frame));
if (void* VarAddress = Context->GetAddressOfVar(i, Frame))
{
	if (Usage.GetDebuggerValue(VarAddress, CurrentValue))
	{
		bValidValue = true;
	}
}
// ★ `this/local/module` 三条值检查 lane 都依赖 live TypeUsage 现推
```

**差距描述**

- `实现质量差异`：当前 D5 已经有 breakpoint/callstack/value inspection，但 value inspection owner 仍是 `TypeUsage + declaration string`，不是结构化 descriptor。
- `能力缺失`：没有任何正式字段告诉 IDE 或测试“这个值是不是 interface dual-slot、wrapper family、container role”。因此 `P10` 就算在 runtime 主链打通 `FScriptInterface`，调试器也仍会把它降格成“某个 object declaration 字符串”。
- `实现方式不同`：当前把“类型解释”放在每次 debug request；最佳参考实现更倾向让 debug lane 复用 property/function descriptor 或直接把 interface 值桥做成一等 debug value。

**参考方案**

- `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaDebugBase.cpp:546-555`：`CPT_Interface` debug path 直接读取 `FScriptInterface` 的 `Object` 与 `Interface` 双槽。
- `Reference/UnrealCSharp/Source/UnrealCSharp/Public/Registry/FClassRegistry.h:52-63`
- `Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FClassRegistry.cpp:135-189`：runtime 侧维护 `PropertyDescriptorMap / FunctionDescriptorMap`，值检查不需要每次都重建 family 判定。

关键源码 [106]：参考实现把“值如何解释”收敛成 descriptor / interface-aware debug value

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaDebugBase.cpp
// 位置: 546-555
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Public/Registry/FClassRegistry.h
// 位置: 52-63
// 文件: Reference/UnrealCSharp/Source/UnrealCSharp/Private/Registry/FClassRegistry.cpp
// 位置: 135-189
// 说明: 一个直接展示 interface 双槽，一个把 property/function descriptor 做成稳定 registry
// ============================================================================
case CPT_Interface:
{
	FInterfaceProperty *InterfaceProperty = (FInterfaceProperty*)InProperty;
	const FScriptInterface &Interface = InterfaceProperty->GetPropertyValue(ValuePtr);
	UObject *Object = Interface.GetObject();
	ReadableValue = FString::Printf(
		TEXT("%s%s: Object(0x%p), Interface(0x%p)"),
		*InterfaceTypeText, *InterfaceExtendedTypeText, Object, Interface.GetInterface());
	// ★ UnLua debug value 明确保留 object slot + interface slot
}

TMap<uint32, FPropertyDescriptor*> PropertyDescriptorMap;
TMap<uint32, FFunctionDescriptor*> FunctionDescriptorMap;
// ★ UnrealCSharp 先把可复用 descriptor 存进 registry，再供 runtime/debug lane 查询

if (const auto FoundPropertyDescriptor = PropertyDescriptorMap.Find(InPropertyHash))
{
	return *FoundPropertyDescriptor;
}
...
PropertyDescriptorMap.Add(InPropertyHash, FoundPropertyDescriptor);
// ★ property/function descriptor 不是 request-time 现推，而是稳定缓存
```

**吸收建议**

- 不要等整个 debug 协议重写后再补这条线。先在插件内新增轻量 `FAngelscriptDebugValueDescriptor` 或等价 view，字段至少包括 `ValueFamily`、`TypeShapeRole`、`InterfaceClassPath`、`SymbolRevision`。
- `RequestVariables` / `RequestEvaluate` 应优先消费前文已提出的 `TypeShape / InterfaceValueBridge / SymbolCatalog` 投影，而不是继续每次都走 `FromTypeId()` 再猜一次 declaration。
- `SendDebugDatabase()` 暂时可以保持字符串 payload 兼容，但应先把 descriptor key 与 revision 埋进去；这样 IDE 与测试可以稳定判断 “这个 symbol 是没支持、被 stub，还是 value-lane 不可检查”。

**优先级**

- `P1`
- **理由**：它不先于前文 `P0 InterfaceValueBridge / canonical producer`，但必须在 `P10` 对外宣称“debugger/IDE 也能解释 interface family”之前落地。否则 runtime lane 已补齐，debugger 仍会继续输出误导性的旧声明与旧值形态。

### [D8 / D11] 性能与优化 / 部署与打包补充：PrecompiledData 与 StaticJIT 还有第二套位置化 subtype schema

前文已经把 `Binds.Cache` 的 schema 缺口写清。本轮新增的是另一个更容易漏掉的事实：**就算 `Binds.Cache`、live `TypeShape`、`SurfaceSupportProfile` 都补了，`PrecompiledData + StaticJIT` 仍然会沿自己的 `templateSubTypes[]` 位置协议继续运行**。

**当前状态**

- `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp:1664-1672` 保存类型引用时，直接 `Ref.SubTypes[i].InitFrom(*this, ObjType->templateSubTypes[i])`。
- 同文件 `1746-1758` 与 `1811-1818` 在恢复阶段按位置回填 subtype，再调用 `Engine->GetTemplateInstanceType(...)` 实例化模板。
- `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp:2856-2884` 在计算 template-determined size 时直接拿 `templateSubTypes[0]`；这意味着 JIT size/align 假设同样建立在“第 0 个 subtype 就是决定大小的那个角色”。

```
[D8/D11-Deep] Template Shape In Cook/JIT
live asCObjectType
├─ templateBaseType
└─ templateSubTypes[i]                        // VM 内部位置数组
      |
      v
PrecompiledData::ReferenceTypeInfo
├─ Ref.Name / Ref.Module
└─ Ref.SubTypes[i]                           // 仍是位置化快照
      |
      v
PrecompiledData::GetTypeInfo
└─ Ref.SubTypes[i].Create(..., subTypes[i])  // 按位置回放
      |
      v
StaticJIT::GetComputedOffsets
└─ templateSubTypes[0]                       // size 规则再次假设固定槽位
```

关键源码 [107]：cook/JIT lane 仍维护独立的 `templateSubTypes[]` 位置协议

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp
// 位置: 1664-1672, 1746-1758, 1811-1818
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp
// 位置: 2856-2884
// 说明: PrecompiledData 保存/恢复 template subtype 时仍是纯位置协议；StaticJIT 也直接看第 0 个 subtype
// ============================================================================
int32 SubTypeCount = TypeInfo->GetSubTypeCount();
if (SubTypeCount > 0)
{
	auto* ObjType = (asCObjectType*)TypeInfo;
	if (ObjType->templateBaseType != nullptr)
	{
		Ref.SubTypes.SetNum(SubTypeCount);
		for (int32 i = 0; i < SubTypeCount; ++i)
			Ref.SubTypes[i].InitFrom(*this, ObjType->templateSubTypes[i]);
		// ★ 保存阶段只知道第 i 个 subtype，不知道它的角色是 Element / Key / Value / InterfaceClass
	}
}

if (Ref.SubTypes.Num() != 0)
{
	asCArray<asCDataType> subTypes;
	subTypes.SetLength(Ref.SubTypes.Num());
	for (int32 i = 0, Count = Ref.SubTypes.Num(); i < Count; ++i)
		Ref.SubTypes[i].Create(*this, subTypes[i]);
	// ★ 恢复阶段继续按位置回放
}

if (ObjectType->templateBaseType != nullptr
	&& (ObjectType->templateBaseType->flags & asOBJ_TEMPLATE_SUBTYPE_DETERMINES_SIZE) != 0)
{
	auto subType = ObjectType->templateSubTypes[0];
	if (subType.IsPrimitive() || subType.IsEnumType())
	{
		Offsets->HardcodedSize = Align(ObjectType->templateBaseType->size + subType.GetSizeInMemoryBytes(), Offsets->HardcodedAlignment);
	}
	// ★ JIT 也默认“第 0 个 subtype”就是决定 size 的角色
}
```

**差距描述**

- `实现质量差异`：当前不是“没有 PrecompiledData/JIT”，而是它们还没共享前文提出的 `TypeShape / PropertyShape` 思路。
- `实现方式不同`：当前优化 lane 自己保存一份位置化 subtype 数组；最佳参考方案把 generic family 树建在统一 reflection/bridge 层，再让 runtime 或 codegen 去消费。
- `风险补充`：如果后续只修 live `TypeUsage` 与 `Binds.Cache`，`TScriptInterface<>` 一旦进入 cooked/JIT lane，仍可能在 `PrecompiledData/JIT` 上重新分叉成“editor 可用、cook 或 JIT 不可重建”的新故障面。

**参考方案**

- `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Reflection/FReflectionRegistry.cpp:57-60`：先把 `TScriptInterfaceClass` 注册成统一 generic family。
- `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:415-430`：`FInterfaceProperty -> TScriptInterface<>`。
- `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp:562-608`：`Map/Set/Optional` 等 generic family 递归复用同一套 type bridge。

关键源码 [108]：参考方案先有统一 generic tree，再让各 lane 消费

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Reflection/FReflectionRegistry.cpp
// 位置: 57-60
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Bridge/FTypeBridge.cpp
// 位置: 415-430, 562-608
// 说明: generic family 先进入统一 reflection/bridge，再由 runtime/codegen 复用
// ============================================================================
NameClass = GetClass<FName>();
TScriptInterfaceClass = GetClass(
	COMBINE_NAMESPACE(NAMESPACE_ROOT, NAMESPACE_CORE_UOBJECT), GENERIC_T_SCRIPT_INTERFACE);
// ★ registry 先注册 `TScriptInterface<>` 这类 generic family

const auto FoundGenericClass = FReflectionRegistry::Get().GetTScriptInterfaceClass();
const auto FoundClass = FReflectionRegistry::Get().GetClass(InProperty->InterfaceClass);
return MakeGenericTypeInstance(FoundGenericClass, FoundClass);
// ★ interface property 直接变成结构化 generic instance

const auto FoundGenericClass = FReflectionRegistry::Get().GetTMapClass();
const auto FoundKeyClass = GetClass(InProperty->KeyProp);
const auto FoundValueClass = GetClass(InProperty->ValueProp);
...
return MakeGenericTypeInstance(FoundGenericClass, ReflectionTypeArray);
// ★ map/set/optional 也都递归消费同一条 type bridge，而不是额外再发明位置协议
```

**吸收建议**

- 这条线不应抢在前文 `P0/P1` 之前。建议等 `TypeShape / PropertyShape` 在 live lane 与 `Binds.Cache` 站稳后，再把 `PrecompiledData` 的 `Ref.SubTypes` 升级为 `ShapeRef` 或等价 role-tagged 结构。
- `StaticJIT` 的 `templateSubTypes[0]` size 假设不要继续扩散；后续应通过共享 `TypeShape` 或 `GenericRole` 查询“谁决定 size”，而不是把 `InterfaceClass`、future wrapper 等 family 都塞回 index 约定。
- 首批只需要覆盖 `Element / Key / Value / InterfaceClass` 四类角色，不需要一口气重做整套 JIT 系统，也不需要修改引擎。

**优先级**

- `P2`
- **理由**：它不先于当前主线的 `P0 canonical producer / InterfaceValueBridge`，也不先于前文已提出的 `P1 Binds.Cache PropertyShape`。但如果这条线不提前排进路线图，后续很容易出现“editor/runtime 看似通了，cooked/JIT 另起一套故障”的迟发问题。

### [D9] 测试基础设施补充：当前自动化还没有锁住 `DebugDatabase / Variables / Precompiled subtype schema`

前文已经指出 interface 场景测试存在 helper lane。本轮新增的不是再谈 helper，而是更普遍的结论：**当前自动化主要锁控制面，不锁 schema 面**。这会让 `P10` 的后置 lane 在没有明显红灯的情况下漂移。

**当前状态**

- `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h:25-29` 已定义 `RequestDebugDatabase / DebugDatabase` 消息。
- 但 `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.h:68-80` 只封装了 `SendStartDebugging / SendContinue / SendRequestCallStack / SendRequestVariables / SendRequestEvaluate / SendSetBreakpoint / SendClearBreakpoints`，没有 `SendRequestDebugDatabase` 或 `GoToDefinition` helper。
- `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp:324-432` 断言的是 breakpoint 命中与 callstack；并未对 `RequestVariables / RequestEvaluate / DebugDatabase` payload 结构做 snapshot。
- `Plugins/Angelscript/Source/AngelscriptTest/Dump/AngelscriptDumpTests.cpp:45-75,202-250` 只检查 `PrecompiledData.csv / StaticJITState.csv / DebugServerState.csv` 等文件是否存在，以及 `DumpSummary.csv` 的 status，不检查 schema/rows 是否表达了 subtype 与 family 语义。

```
[D9-Deep] Validation Envelope
Debugger automation
├─ StartDebugging
├─ Set/ClearBreakpoint
├─ RequestCallStack
└─ Continue / stepping

Dump automation
├─ file exists?
└─ summary status?

Missing
├─ RequestDebugDatabase snapshot
├─ RequestVariables / Evaluate schema snapshot
└─ PrecompiledData / StaticJIT subtype row assertions
```

关键源码 [109]：当前自动化主要锁“流程跑通”，还没有锁“schema 正确”

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h
// 位置: 25-29
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.h
// 位置: 68-80
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp
// 位置: 324-432
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Dump/AngelscriptDumpTests.cpp
// 位置: 45-75, 202-250
// 说明: 协议面支持更丰富，但自动化暂时没有把这些 schema 变成回归资产
// ============================================================================
enum class EDebugMessageType : uint8
{
	Diagnostics,
	RequestDebugDatabase,
	DebugDatabase,
	// ★ 协议已支持 `RequestDebugDatabase`
};

bool SendStartDebugging(int32 AdapterVersion);
bool SendContinue();
bool SendRequestCallStack();
bool SendRequestVariables(const FString& ScopePath);
bool SendRequestEvaluate(const FString& Path, int32 DefaultFrame = 0);
bool SendSetBreakpoint(const FAngelscriptBreakpoint& Breakpoint);
bool SendClearBreakpoints(const FAngelscriptClearBreakpoints& Breakpoints);
// ★ 这里没有 `SendRequestDebugDatabase()` / `SendGoToDefinition()` helper

if (!TestTrue(TEXT("Debugger.Breakpoint.HitLine should send the target breakpoint"), Client.SendSetBreakpoint(Breakpoint)))
{
	return false;
}
...
TestEqual(TEXT("Debugger.Breakpoint.HitLine should stop because of a breakpoint"), StopMessage->Reason, FString(TEXT("breakpoint")));
TestTrue(TEXT("Debugger.Breakpoint.HitLine should report the fixture filename in the top stack frame"), Callstack.Frames[0].Source.EndsWith(Fixture.Filename));
// ★ 当前 breakpoint 测试保护的是 stop/callstack，不是 symbol/value schema

TArray<FString> GetExpectedPhaseOneCsvFiles()
{
	return {
		TEXT("PrecompiledData.csv"),
		TEXT("StaticJITState.csv"),
		TEXT("DebugServerState.csv"),
		...
	};
}

for (const FString& ExpectedFilename : GetExpectedPhaseOneCsvFiles())
{
	TestTrue(*FString::Printf(TEXT("DumpAll should create '%s'"), *ExpectedFilename), IFileManager::Get().FileExists(*CsvPath));
}
// ★ dump 测试当前只锁“有没有文件”，没有锁 subtype/schema 内容
```

**差距描述**

- `能力缺失`：没有专门回归去锁 `DebugDatabase`、`RequestVariables/RequestEvaluate`、`PrecompiledData/JIT` 的 schema 内容。
- `实现质量差异`：当前测试更像“控制面 smoke test”。这对 breakpoint、文件生成很有价值，但不足以保护 `P10` 这种 family rollout 的后置 lane。
- `结果风险`：未来最容易出现的假绿不是编译失败，而是 runtime/IDE/cooked 三边里有一边还在沿用旧 shape，现有自动化却仍全绿。

**参考方案**

- `Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp:417-451`：把 `ue.d.ts / ue_bp.d.ts` 落成稳定文件，天然适合 snapshot 对比。
- `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:222-233`：只在内容变化时写 `.lua` 文件，稳定 sink 明确。

关键源码 [110]：稳定落盘的 artifact 更容易被回归系统直接比较

```cpp
// ============================================================================
// 文件: Reference/puerts/unreal/Puerts/Source/DeclarationGenerator/Private/DeclarationGenerator.cpp
// 位置: 417-451
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp
// 位置: 222-233
// 说明: 参考实现先把 IDE/类型产物落到稳定文件，再谈 editor/commandlet 入口
// ============================================================================
PlatformFile.DeleteFile(*(PuertsBaseDir / TEXT("Typing/ue/ue.d.ts")));
PlatformFile.DeleteFile(*(PuertsBaseDir / TEXT("Typing/ue/ue_bp.d.ts")));
...
FFileHelper::SaveStringToFile(ToString(), *UEDeclarationFilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
// ★ puerts 的 declaration artifact 是稳定可比对的文件，不是 request-time 临时字符串

const FString FilePath = FString::Printf(TEXT("%s/%s.lua"), *Directory, *FileName);
FString FileContent;
FFileHelper::LoadFileToString(FileContent, *FilePath);
if (FileContent != GeneratedFileContent)
	FFileHelper::SaveStringToFile(GeneratedFileContent, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
// ★ UnLua 的 IntelliSense 输出也天然适合做 snapshot/regression
```

**吸收建议**

- 在现有 test client 上补 `SendRequestDebugDatabase()`，并为 `RequestVariables / RequestEvaluate` 增加一条最小 schema snapshot。第一批只锁一个 interface symbol 和一个 container symbol，不需要先覆盖全部语言面。
- `DumpTests` 不要停在 file existence。可以先加一份轻量 `DebugDatabaseSnapshot.json` / `PrecompiledTypeRefs.csv` / `JitTypeRefs.csv`，让 schema 成为可断言资产。
- 这条线完全可以与主线并行推进，不改引擎、不改 AngelScript `2.33.0 WIP` runtime；本质只是把当前已经能拿到的信号，变成可重复比较的回归资产。

**优先级**

- `P1`
- **理由**：它不先于 `P0` 的 interface owner，但应与 `P10` rollout 同步进入执行面。否则后续每补一条 lane，都需要靠人工盯日志/CSV/IDE 画面确认，主线成本会迅速失控。

### 值得吸收的设计模式（增量）

- `Descriptor-backed inspection beats live re-inference`：前文更多讨论了 bind owner；本轮补充的关键是 debugger/value lane 也要有自己的 descriptor owner，不能继续把 `FromTypeId()` 当万能解释器。
- `Shared shape must cross runtime, cache, and precompiled lanes`：`TypeShape / PropertyShape` 如果只落在 live bind 与 `Binds.Cache`，JIT/cooked 仍会保留第二套位置协议。真正可交付的 family rollout，需要把优化 lane 一起收口。
- `Stable sink before stable regression`：如果 `DebugDatabase`、variables、precompiled refs 永远只在 request-time 临时拼装，就很难为它们建立低噪音回归。先把 sink 稳定下来，再补 snapshot test，收益最高。

### 改进路线建议（增量）

1. `P1`：在前文 `P0 InterfaceValueBridge / canonical producer` 落地后，立刻为 debugger 加一层 `DebugValueDescriptor` 或等价 view，并补 `RequestDebugDatabase + RequestVariables/RequestEvaluate` 的最小 snapshot tests。
2. `P1`：把当前 dump/test 的“文件存在”升级成“schema 存在”。首批只需要一份面向 `DebugDatabase` 与 `Precompiled/JIT type refs` 的轻量 JSON/CSV sidecar，不必重构现有测试框架。
3. `P2`：等 `TypeShape / PropertyShape` 在 live lane 与 `Binds.Cache` 稳定后，再把 `PrecompiledData / StaticJIT` 的 subtype 序列化切到同一套 role-tagged shape，避免 cooked/JIT 继续成为第二套 family 语义系统。

---

## 深化分析 (2026-04-09 23:29:07)

### 本轮新增关键发现

- 当前 `Bind API GAP` 仍主要统计“这个函数有没有被看见”，还没有统计“这个函数在脚本面保留了多少 Blueprint 语义”。`BlueprintThreadSafe`、`DefaultToSelf`、`AutoCreateRefTerm`、`ExpandEnumAsExecs`、`NativeMake/Break`、`BlueprintAutocast` 这类 semantic metadata 目前大多停在 live `UFunction`，没有进入 `BindDB` / `AS_FunctionTable_*` / `DebugDatabase`。
- `script-defined UFUNCTION` 与 `native/generated BlueprintCallable` 还在走两套 callable builder。前者在 `ClassGenerator` 中复制 `Meta`、计算 `bThreadSafe`、补 `DefaultToSelf`；后者在 `Helper_FunctionSignature` / `FunctionTable` 中只保留少数签名级或执行级字段。差距不只是“少几个 metadata 特判”，而是 owner 分裂。
- editor/test 侧目前仍把 semantic metadata 当作边角料。`BlueprintImpact` 只追踪 class/struct/enum/delegate/pin/asset，`BindConfig` 只钉 `ScriptMethod` 与 `CallableWithoutWorldContext` 两个样本；只要主线继续推进 `P10 UInterface` 与 `Bind API GAP`，这类 semantic regression 就会稳定处在自动化盲区。

### 差距矩阵（增量）

| 维度 | 无差距 | 实现差异 | 能力缺失 | 本轮判定 |
| --- | --- | --- | --- | --- |
| `D6` 代码生成与 IDE 支持：semantic metadata artifact |  |  | `✓` | **能力缺失**：semantic metadata 还没有成为 `BindDB` / `FunctionTable` / `DebugDatabase` 的正式 schema |
| `D3` Blueprint 交互：callable semantic builder |  | `✓` |  | **实现差异**：script-defined 与 native/generated callable 语义构建链仍是两套 owner |
| `D7 / D9` 编辑器集成 / 测试基础设施：semantic metadata safety net |  |  | `✓` | **能力缺失**：impact scanner 与 regression 都还看不见 semantic metadata |

### [D6] 代码生成与 IDE 支持补充：semantic metadata 仍停在 live reflection，不是 artifact schema

**当前状态**

- `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs:8-15, 90-97` 的 UHT signature 只记录 `OwningType / FunctionName / ReturnType / ParameterTypes / IsStatic / IsConst / UseExplicitSignature`，完全不知道 callable 的 semantic metadata。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h:56-86` 的 `FAngelscriptMethodBind` 只持久化 `Declaration / ScriptName / WorldContextArgument / DeterminesOutputTypeArgument / static/global/trivial` 等少数字段；`BlueprintProtected / Deprecated / ToolTip / Category / BlueprintThreadSafe / DefaultToSelf` 都不在 schema 里。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:1545-1564, 1688-1699` 导出的 `DebugDatabase` 只吐出 `name / return / args / outputTypeIndex / isProperty`；对 IDE 和诊断前端来说，callable 语义继续是黑箱。
- 反过来，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h:260-266, 463-468` 明明已经能在 live lane 看见 `BlueprintProtected / DeprecatedFunction / ToolTip / Category`，说明问题不是源头没有 metadata，而是 artifact lane 没有 owner。

```
[D6-Deep] Semantic Metadata Split
UFunction metadata
├─ live binder (`Helper_FunctionSignature`)
│  ├─ BlueprintProtected / Deprecated / ToolTip / Category
│  └─ WorldContext / DeterminesOutputType
├─ script class generator
│  └─ BlueprintThreadSafe / DefaultToSelf / raw Meta map
├─ persistent artifacts
│  ├─ `AngelscriptFunctionSignatureBuilder` -> signature only
│  ├─ `FAngelscriptMethodBind` -> tiny semantic subset
│  └─ `DebugDatabase` -> callable shape only
└─ result
   └─ GAP / IDE / cooked 各自看到不同的 semantic 子集
```

关键源码 [111]：当前三个 artifact lane 都没有 semantic metadata 正式列

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs
// 位置: 8-15, 90-97
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h
// 位置: 56-86
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 位置: 1545-1564, 1688-1699
// 说明: UHT / cooked bind-db / debug database 都没有 semantic metadata schema
// ============================================================================
internal sealed record AngelscriptFunctionSignature(
	string OwningType,
	string FunctionName,
	string ReturnType,
	IReadOnlyList<string> ParameterTypes,
	bool IsStatic,
	bool IsConst,
	bool UseExplicitSignature);
// ★ UHT 生成侧只保存“怎么调用”，不保存“脚本面怎么解释这个 callable”

struct FAngelscriptMethodBind
{
	FString Declaration;
	FString UnrealPath;
	FString ClassName;
	FString ScriptName;
	int8 WorldContextArgument = -1;
	int8 DeterminesOutputTypeArgument = -1;
	bool bStaticInUnreal = false;
	bool bStaticInScript = false;
	bool bGlobalScope = false;
	bool bNotAngelscriptProperty = false;
	bool bTrivial = false;
};
// ★ cooked bind-db 里只有极小的 semantic 子集，更多 metadata 直接消失

auto MakeFuncDesc = [&](asCScriptFunction* ScriptFunction) -> TSharedPtr<FJsonObject>
{
	FuncDesc->SetStringField(TEXT("name"), Name);
	FuncDesc->SetStringField(TEXT("return"), ReturnType);
	...
	FuncDesc->SetArrayField(TEXT("args"), ArgDesc);

	if (ScriptFunction->determinesOutputTypeArgumentIndex != -1)
		FuncDesc->SetNumberField(TEXT("outputTypeIndex"), ...);

	if (!ScriptFunction->IsProperty())
		FuncDesc->SetBoolField(TEXT("isProperty"), false);
};
// ★ debug database 也只导出 callable 形状，不导出 semantic profile
```

关键源码 [112]：参考实现把 semantic metadata 做成结构化、可消费的 owner

```csharp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Reflection/FReflectionRegistry.cpp
// 位置: 461-462, 605-606, 740-783
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGeneratorCore.cpp
// 位置: 1238-1278
// 说明: UnrealCSharp 先注册 metadata attribute class，再让 generator 统一消费
// ============================================================================
BlueprintThreadSafeAttributeClass = GetClass(
	COMBINE_NAMESPACE(NAMESPACE_ROOT, NAMESPACE_DYNAMIC), CLASS_BLUEPRINT_THREAD_SAFE_ATTRIBUTE);

MustImplementAttributeClass = GetClass(
	COMBINE_NAMESPACE(NAMESPACE_ROOT, NAMESPACE_DYNAMIC), CLASS_MUST_IMPLEMENT_ATTRIBUTE);

AutoCreateRefTermAttributeClass = GetClass(
	COMBINE_NAMESPACE(NAMESPACE_ROOT, NAMESPACE_DYNAMIC), CLASS_AUTO_CREATE_REF_TERM_ATTRIBUTE);
...
ScriptOperatorAttributeClass = GetClass(
	COMBINE_NAMESPACE(NAMESPACE_ROOT, NAMESPACE_DYNAMIC), CLASS_SCRIPT_OPERATOR_ATTRIBUTE);
// ★ registry 先把 semantic metadata 都收成一等 reflection object

return {
	ReflectionRegistry.GetAutoCreateRefTermAttributeClass(),
	ReflectionRegistry.GetCallableWithoutWorldContextAttributeClass(),
	ReflectionRegistry.GetDefaultToSelfAttributeClass(),
	ReflectionRegistry.GetExpandEnumAsExecsAttributeClass(),
	ReflectionRegistry.GetExpandBoolAsExecsAttributeClass(),
	ReflectionRegistry.GetScriptMethodAttributeClass(),
	ReflectionRegistry.GetScriptMethodSelfReturnAttributeClass(),
	ReflectionRegistry.GetScriptOperatorAttributeClass(),
	ReflectionRegistry.GetHidePinAttributeClass(),
	ReflectionRegistry.GetNativeBreakFuncAttributeClass(),
	ReflectionRegistry.GetNativeMakeFuncAttributeClass(),
	ReflectionRegistry.GetBlueprintAutoCastAttributeClass(),
	ReflectionRegistry.GetNotBlueprintThreadSafeAttributeClass(),
	ReflectionRegistry.GetDeterminesOutputTypeAttributeClass(),
	...
};
// ★ function generator 统一从 registry 拿 semantic metadata，不靠临场拼字段
```

**差距描述**

- `能力缺失`：当前没有一个 artifact 能稳定表达 callable semantic metadata；这意味着 `Bind API GAP` 无法回答“函数存在了，但脚本语义是否齐了”。
- `实现方式不同`：当前 Angelscript 主要靠 live `UFunction` 临场重读 metadata；最佳参考方案先建结构化 metadata registry，再让 codegen / runtime / IDE 共同消费。
- `实现质量差异`：因为 schema 缺位，`DebugDatabase`、`Docs`、`FunctionTable`、`BindDB` 每条 lane 只能各自暴露一点点事实，导致 diagnosis 与 IDE 能看到的 callable 语义天然不一致。

**参考方案**

- `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Reflection/FReflectionRegistry.cpp:461-462, 605-606, 740-783`
- `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGeneratorCore.cpp:1238-1278`
- `Reference/puerts/unreal/Puerts/Typing/ue/puerts_decorators.d.ts:983-999, 1029-1058, 1091-1119, 1223-1231, 1840`
- `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Public/PEBlueprintMetaData.h:69-76, 251-259`

**吸收建议**

- 不要先追求把所有 metadata 都“立刻执行”。第一步先把 `FunctionSemanticProfile` 或等价 schema 正式写进 `FAngelscriptMethodBind`、`AS_FunctionTable_*` sidecar 与 `DebugDatabase`。
- 首批字段建议只覆盖当前主线最相关的语义：`WorldContext`、`DeterminesOutputType`、`BlueprintProtected`、`Deprecated`、`BlueprintThreadSafe`、`DefaultToSelf`、`ScriptMethod`、`ScriptOperator`、`AutoCreateRefTerm`、`ExpandExecs`、`NativeMake/Break`、`BlueprintAutocast`。
- `Bind API GAP` 统计口径需要从“函数是否导出”升级成“两层 coverage”：
  - `SignatureCoverage`
  - `SemanticCoverage`
- 这条线不需要修改引擎，也不要求升级 AngelScript `2.33.0 WIP`；本质是补 plugin 内部 artifact schema 与消费点。

**优先级**

- `P1`
- **理由**：它不先于 `P0/P1` 的 `UInterface` type/value owner，但应紧跟 `Bind API GAP` 主线，否则后续 API 补齐仍只能得到“函数在不在”的粗粒度绿灯。

### [D3] Blueprint 交互补充：native/generated callable builder 与 script-defined callable builder 仍不是同一条语义链

**当前状态**

- `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:527-563` 会根据 class/function metadata 计算 `bThreadSafe`；`3431-3441` 又把 `FunctionDesc->Meta` 全量写回 `UFunction`，并为 mixin 额外补 `DefaultToSelf`。
- 但 native/generated lane 的主 builder 仍是 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h:222-239, 414-458`。它只会回写 `WorldContext`、`DeterminesOutputType`、`NotAngelscriptProperty`、`BlueprintProtected`、`Deprecated`、`EditorOnly`。
- 再往 cooked 走，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h:56-86` 又只剩更小的一组字段；`script-defined` lane 与 `native/generated` lane 的 semantic surface 进一步分叉。

```
[D3-Deep] Callable Builder Split
script-defined UFUNCTION
├─ class desc meta
├─ `ClassGenerator`
│  ├─ compute `bThreadSafe`
│  ├─ copy raw `Meta`
│  └─ add `DefaultToSelf` for mixin
└─ `UASFunction` / `ASClass` 执行期直接消费

native/generated BlueprintCallable
├─ `FunctionSignatureBuilder`
│  └─ signature only
├─ `Helper_FunctionSignature::ModifyScriptFunction`
│  ├─ WorldContext
│  ├─ DeterminesOutputType
│  ├─ Protected / Deprecated / EditorOnly
│  └─ no shared semantic profile
└─ cooked lane
   └─ semantic subset 再次缩小
```

关键源码 [113]：script-defined lane 已有 richer semantics，native/generated lane 仍是瘦 builder

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 位置: 527-563, 3431-3441
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h
// 位置: 222-239, 414-458
// 说明: script-defined callable 与 native/generated callable 的 semantic owner 不一致
// ============================================================================
const bool bClassThreadSafe = ClassData.NewClass->Meta.Contains(FUNCMETA_BlueprintThreadSafe);
...
if (bClassThreadSafe)
	FunctionDesc->bThreadSafe = !FunctionDesc->Meta.Contains(FUNCMETA_NotBlueprintThreadSafe);
else
	FunctionDesc->bThreadSafe = FunctionDesc->Meta.Contains(FUNCMETA_BlueprintThreadSafe);
// ★ script-defined lane 会显式计算线程安全语义

for (auto& Elem : FunctionDesc->Meta)
	NewFunction->SetMetaData(Elem.Key, *Elem.Value);
...
NewFunction->SetMetaData(NAME_Function_MixinArgument, *MixinArgumentName);
NewFunction->SetMetaData(NAME_Function_DefaultToSelf, *MixinArgumentName);
// ★ script-defined lane 还会保留 raw Meta，并补 `DefaultToSelf`

const FString& WorldContextParam = Function->GetMetaData(NAME_Signature_WorldContext);
...
if (WorldContextArgument != -1)
{
	ScriptFunction->hiddenArgumentIndex = WorldContextArgument;
	ScriptFunction->hiddenArgumentDefault = "__WorldContext()";
}

if (DeterminesOutputTypeArgument != -1)
	ScriptFunction->determinesOutputTypeArgumentIndex = DeterminesOutputTypeArgument;

if (bBlueprintProtected)
	ScriptFunction->SetProtected(true);

if (bDeprecated)
{
	ScriptFunction->traits.SetTrait(asTRAIT_DEPRECATED, true);
	ScriptFunction->deprecationMessage = TCHAR_TO_UTF8(*DeprecationMessage);
}
// ★ native/generated lane 只回写少量语义；`BlueprintThreadSafe` / `DefaultToSelf` / `AutoCreateRefTerm`
// ★ / `ExpandExecs` / `ScriptOperator` 等都没有进入共享 builder
```

关键源码 [114]：参考实现把 callable semantics 建在共享 registry / validator 上

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGeneratorCore.cpp
// 位置: 1238-1278
// 文件: Reference/puerts/unreal/Puerts/Source/PuertsEditor/Public/PEBlueprintMetaData.h
// 位置: 69-76, 251-259
// 说明: 一个参考点强调共享 metadata registry，另一个强调集中 validator
// ============================================================================
return {
	ReflectionRegistry.GetAutoCreateRefTermAttributeClass(),
	ReflectionRegistry.GetCallableWithoutWorldContextAttributeClass(),
	ReflectionRegistry.GetDefaultToSelfAttributeClass(),
	ReflectionRegistry.GetExpandEnumAsExecsAttributeClass(),
	ReflectionRegistry.GetExpandBoolAsExecsAttributeClass(),
	ReflectionRegistry.GetScriptMethodAttributeClass(),
	ReflectionRegistry.GetScriptOperatorAttributeClass(),
	ReflectionRegistry.GetNativeBreakFuncAttributeClass(),
	ReflectionRegistry.GetNativeMakeFuncAttributeClass(),
	ReflectionRegistry.GetBlueprintAutoCastAttributeClass(),
	ReflectionRegistry.GetNotBlueprintThreadSafeAttributeClass(),
	...
};
// ★ UnrealCSharp 用共享 registry 统一定义 callable semantics 的合法集合

static const FName NAME_ExpandBoolAsExecs = TEXT("ExpandBoolAsExecs");
static const FName NAME_ExpandEnumAsExecs = TEXT("ExpandEnumAsExecs");
...
if (InKey == NAME_ExpandBoolAsExecs || InKey == NAME_ExpandEnumAsExecs)
{
	if (!ValidateFunctionExpandAsExecs(InField.template Get<UFunction>(), InValue))
	{
		OutMessage = TEXT("invalid meta data for expand as execs");
		return false;
	}
	return true;
}
// ★ puerts 至少把 node-shape metadata 放进统一 validator，而不是散在各条调用链临场猜
```

**差距描述**

- `实现差异`：当前插件不是“完全不支持 semantic metadata”，而是同一类语义被拆到 `ClassGenerator`、`Helper_FunctionSignature`、`BindDB` 三条 builder，且覆盖面不同。
- `能力缺失`：只要函数来自 native/generated lane，`BlueprintThreadSafe`、`DefaultToSelf`、`AutoCreateRefTerm`、`ExpandExecs`、`ScriptOperator`、`NativeMake/Break`、`BlueprintAutocast` 等语义就没有统一 owner。
- `实现质量差异`：editor 与 cooked 的 callable surface 会继续分裂。editor live session 还能从 `UFunction` 临场捞到一部分 metadata，cooked / cache / IDE lane 则只能看到更窄的降级结果。

**参考方案**

- `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Reflection/FReflectionRegistry.cpp:461-462, 605-606, 740-783`
- `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGeneratorCore.cpp:1238-1278`
- `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Public/PEBlueprintMetaData.h:69-76, 251-259`

**吸收建议**

- 抽一个 plugin 内部共享的 `BuildFunctionSemanticProfile(UFunction)` 或等价 descriptor，禁止 `ClassGenerator`、`Helper_FunctionSignature`、`UHT exporter` 再各自硬编码一套 metadata 读取逻辑。
- `ClassGenerator` 不应继续独占 `BlueprintThreadSafe` / `DefaultToSelf` 等语义；它应消费同一份 profile，然后只负责把结果 materialize 到 `UASFunction/UFunction`。
- `Helper_FunctionSignature::ModifyScriptFunction()` 只负责把 profile 回写给 VM callable，不再承担 metadata 解读逻辑。
- 与 `P10 UInterface` 的协调方式是：先把 builder owner 收口，再把 interface callable/profile 接进同一套 descriptor；不要先在 interface 路径继续堆专用特判。

**优先级**

- `P1`
- **理由**：这条线直接影响 Blueprint/native/script 三条 callable lane 是否还能继续并行演进。它不抢在 `P0 UInterface` 前，但应在大规模 `Bind API GAP` 补齐前完成收口。

### [D7 / D9] 编辑器集成 / 测试基础设施补充：semantic metadata 还没有进入 impact 与 regression 主线

**当前状态**

- `Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h:13-31` 的 `EBlueprintImpactReason` 只有 `ScriptParentClass / NodeDependency / PinType / VariableType / DelegateSignature / ReferencedAsset` 六类，`FBlueprintImpactSymbols` 也只收 `Classes / Structs / Enums / Delegates / ReplacementObjects`。
- `Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp:44-57, 112-147, 150-249` 的 scanner 只看 pin family、外部依赖、变量与 delegate signature；看不到 `DefaultToSelf / AutoCreateRefTerm / ExpandExecs / BlueprintThreadSafe` 这类 callable semantics。
- `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp:233-240, 604-703` 目前只注册两条 metadata coverage：
  - `FunctionLevelScriptMethodUsesFirstParameterAsMixin`
  - `CallableWithoutWorldContextKeepsHiddenWorldContextButClearsTrait`
- `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptUhtCoverageTestTypes.h:31-38` 对应的 fixture 也只播种了 `ScriptMethod` 与 `CallableWithoutWorldContext`；其它 semantic metadata 还没有标准样本。

```
[D7/D9-Deep] Semantic Metadata Safety Net Blind Spot
semantic metadata change
├─ DefaultToSelf / AutoCreateRefTerm / ExpandExecs / ThreadSafe / ...
├─ editor impact
│  └─ `BlueprintImpact` -> only type / delegate / asset reasons
├─ regression
│  └─ `BindConfigTests` -> only ScriptMethod + CallableWithoutWorldContext
└─ result
   └─ 节点行为与 callable 语义变了，suite 仍可能全绿
```

关键源码 [115]：当前 editor/test safety net 主要盯类型，不盯 callable semantics

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Public/BlueprintImpact/AngelscriptBlueprintImpactScanner.h
// 位置: 13-31
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanner.cpp
// 位置: 44-57, 112-147, 150-249
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp
// 位置: 233-240, 604-703
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptUhtCoverageTestTypes.h
// 位置: 31-38
// 说明: impact 与 regression 当前都没有 semantic metadata 这一层 owner
// ============================================================================
enum class EBlueprintImpactReason : uint8
{
	ScriptParentClass,
	NodeDependency,
	PinType,
	VariableType,
	DelegateSignature,
	ReferencedAsset,
};
// ★ 没有 `CallableSemantics`、`NodeShape`、`MetadataPolicy` 之类的 reason

struct FBlueprintImpactSymbols
{
	TSet<UClass*> Classes;
	TSet<UScriptStruct*> Structs;
	TSet<UEnum*> Enums;
	TSet<UDelegateFunction*> Delegates;
	TMap<UObject*, UObject*> ReplacementObjects;
};
// ★ symbol 集合也只覆盖 type/delegate/asset

bool MatchesPinType(const FEdGraphPinType& PinType, const FBlueprintImpactSymbols& Symbols)
{
	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
		return Symbols.Structs.Contains(...);
	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Enum || PinType.PinCategory == UEdGraphSchema_K2::PC_Byte)
		return Symbols.Enums.Contains(...);
	return false;
}
// ★ scanner 只看 pin type，不看函数 semantic metadata

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptScriptMethodMetadataCoverageTest,
	"Angelscript.TestModule.Engine.BindConfig.FunctionLevelScriptMethodUsesFirstParameterAsMixin", ...);

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptCallableWithoutWorldContextMetadataTest,
	"Angelscript.TestModule.Engine.BindConfig.CallableWithoutWorldContextKeepsHiddenWorldContextButClearsTrait", ...);
// ★ metadata coverage 目前就这两条

UFUNCTION(BlueprintCallable, meta = (ScriptMethod, DisplayName = "Get Coverage Value"))
static int32 GetCoverageValue(const UObject* Target);

UFUNCTION(BlueprintCallable, meta = (WorldContext = "WorldContextObject", CallableWithoutWorldContext))
static int32 CallableWithoutWorldContext(UObject* WorldContextObject, int32 Value);
// ★ fixture 也只播种了两类 metadata
```

关键源码 [116]：参考实现会把 semantic metadata 问题直接落成产物与回归

```cpp
// ============================================================================
// 文件: Reference/UnLua/Plugins/UnLua/Source/UnLuaDefaultParamCollector/Private/UnLuaDefaultParamCollector.cpp
// 位置: 124-150
// 文件: Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Tests/Issue294Test.cpp
// 位置: 65
// 说明: UnLua 把 AutoCreateRefTerm 直接接进构建期产物与 issue-scoped regression
// ============================================================================
const FString& AutoCreateRefTerm = MetaMap->FindRef("AutoCreateRefTerm");
TArray<FString> AutoEmitParameterNames;
if (!AutoCreateRefTerm.IsEmpty())
{
	AutoCreateRefTerm.ParseIntoArray(AutoEmitParameterNames, TEXT(","), true);
	for (FString& ParamName : AutoEmitParameterNames)
		ParamName.TrimStartAndEndInline();
}
...
if (!FindDefaultValueString(MetaMap, Property, ValueStr))
{
	if (AutoEmitParameterNames.Find(Property->GetName()) == INDEX_NONE)
		continue;
}
// ★ metadata 先进入 collector 产物，再影响默认参数生成

IMPLEMENT_UNLUA_INSTANT_TEST(
	FUnLuaTest_Issue294,
	TEXT("UnLua.Regression.Issue294 AutoCreateRefTerm标记的参数需要生成默认值"))
// ★ 一个具体 metadata 缺口对应一条具体 regression，而不是混在大而泛的 smoke test 里
```

**差距描述**

- `能力缺失`：当前没有一条 editor/test 主线能回答“callable semantics 变了，哪些 Blueprint/产物/测试该变红”。
- `实现质量差异`：现有回归更偏向签名、流程和 happy-path 行为，semantic metadata 只要没直接打穿编译，就很容易绿过去。
- `扩展风险`：如果后续按 `P10 UInterface` / `Bind API GAP` 主线把 semantic profile 补上，但不同时补 impact/regression，新增语义只会继续停在人工 spot-check。

**参考方案**

- `Reference/UnLua/Plugins/UnLua/Source/UnLuaDefaultParamCollector/Private/UnLuaDefaultParamCollector.cpp:124-150`
- `Reference/UnLua/Plugins/UnLuaTestSuite/Source/UnLuaTestSuite/Private/Tests/Issue294Test.cpp:65`
- `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Public/PEBlueprintMetaData.h:69-76, 251-259`

**吸收建议**

- `BlueprintImpact` 至少新增一个 semantic 维度：
  - `EBlueprintImpactReason::CallableSemantics`
  - 首批只覆盖 `WorldContext / DefaultToSelf / AutoCreateRefTerm / ExpandExecs / BlueprintThreadSafe`
- `BindConfig` / `GeneratedFunctionTable` / `Dump` 三类现有测试上各补一条 issue-scoped regression，不要新起第四套测试框架：
  - `BindConfig`：验证 profile 构建与 VM 回写
  - `GeneratedFunctionTable`：验证 sidecar/schema 持久化
  - `Dump/DebugDatabase`：验证 IDE/debug sink 看到同一份 semantic profile
- fixture 先只补 3 到 4 个高价值样本即可，不要一次把所有 metadata 全铺开。

**优先级**

- `P1`
- **理由**：它不先于 D6 的 schema，也不先于 D3 的 shared builder；但应与它们进入同一执行批次。否则 semantic metadata 一旦开始落地，自动化与 editor 仍然看不见新增 contract。

### 值得吸收的设计模式（增量）

- `Semantic metadata must be first-class IR`：不要把 metadata 留在 `UFunction->MetaData` 或零散布尔位里临场读；要先变成 artifact/schema，再谈 runtime、IDE、test 各自消费。
- `One semantic profile, many consumers`：`ClassGenerator`、`Helper_FunctionSignature`、`FunctionTable`、`BindDB`、`DebugDatabase`、`BlueprintImpact`、tests 都应消费同一份 `FunctionSemanticProfile`，而不是各自再解析一次 metadata。
- `Issue-scoped regression beats generic smoke test`：像 `UnLua Issue294` 这种“一个 metadata 缺口对应一条命名回归”的方式，比把所有语义塞进一条大而泛的 smoke test 更稳。

### 改进路线建议（增量）

1. `P1`：先给 `FAngelscriptMethodBind`、`AS_FunctionTable_*`、`DebugDatabase` 引入最小 `FunctionSemanticProfile` schema，并把 `Bind API GAP` 统计拆成 `SignatureCoverage + SemanticCoverage`。
2. `P1`：把 `ClassGenerator` 与 `Helper_FunctionSignature` 的 metadata 解读逻辑合并为共享 builder，首批收口 `BlueprintThreadSafe / DefaultToSelf / ScriptMethod / AutoCreateRefTerm / ExpandExecs / BlueprintAutocast / NativeMake/Break`。
3. `P1`：在现有 `BlueprintImpact`、`BindConfigTests`、`GeneratedFunctionTableTests`、`Dump/DebugDatabase` 上补 semantic metadata 回归，不新增独立测试框架。
4. `P2`：等 `P10 UInterface` 的 type/value owner 稳定后，把 interface callable semantics 也接进同一份 `FunctionSemanticProfile`，避免再次形成 interface 专用旁路。

---

## 深化分析 (2026-04-09 23:40:01)

补一条优先级说明：根 `Todo.md` 当前只写了“全局变量问题 / 各种全局数据结构的问题 / 委托”三条粗项，无法为 `P10 UInterface / Bind API GAP` 提供可执行排序。因此本轮新增优先级仍以 `Documents/Plans/Plan_StatusPriorityRoadmap.md`、`Documents/Plans/Plan_InterfaceBinding.md` 与现有自动化主线为准，只补那些会影响 `P10` 后续 debugger / coverage / editor 可信度的后置差距。

### [D4 / D5] 多引擎 clone 已经引入 `internal name + base name` 双层模块身份，但 debugger 公开面还没有 canonical key

**当前状态**

当前 runtime core 已经正式支持 clone engine，并把模块身份拆成了两层：

- `FAngelscriptEngine::MakeModuleName()` 在 clone 模式下把 raw module name 改写成 `Clone_n::Module` 形式的 internal key。
- `CompileModule_Types_Stage1()` 同时把 `ScriptModule->baseModuleName` 保留为 raw module name。
- `SwapInModules()` / `GetModule()` 一直以 internal key 操作 `ActiveModules`，而 debugger 的 breakpoint store 与 callstack payload 却分别消费 `baseModuleName` 和 `GetModuleName()`。
- 现有自动化只覆盖了“clone 间 internal name 隔离”和“外部 raw lookup 仍可用”两件事，见 `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineTests.cpp:320-332`；并没有一条回归把 clone module identity 与 debugger payload 合起来验证。

```
[Current AS] Clone Module Identity Split
raw module name
├─ external lookup: "Gameplay.Player"                 // 对外仍传 raw 名
│  └─ GetModule() -> MakeModuleName()                 // 运行时折成 internal key
├─ ScriptModule.baseModuleName = "Gameplay.Player"    // 协议/账本更适合消费这层
└─ ScriptModule.name = "Clone_1::Gameplay.Player"     // engine-private internal key
   ├─ breakpoints use `baseModuleName`
   └─ callstack uses `GetModuleName()`
```

关键源码 [1]：current 已经显式维护双层 module identity，但 debugger 两条子路径消费的 key 不同

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 位置: AngelscriptEngine.cpp:595-600, 2913-2938, 4251-4260;
//       AngelscriptEngine.h:311-317;
//       AngelscriptDebugServer.cpp:592-594, 1473-1474
// 说明: clone engine 内部名、外部名、debugger 消费点已经分叉
// ============================================================================
FString FAngelscriptEngine::MakeModuleName(const FString& ModuleName) const
{
	if (CreationMode == EAngelscriptEngineCreationMode::Clone && !InstanceId.IsEmpty())
		return FString::Printf(TEXT("%s::%s"), *InstanceId, *ModuleName); // ★ clone 内部名

	return ModuleName;
}

auto* ModRef = ActiveModules.Find(MakeModuleName(ModuleName));         // ★ 外部 raw lookup 先折成 internal key

const FString InternalModuleName = MakeModuleName(Module->ModuleName);
Module->ScriptModule->SetName(TCHAR_TO_ANSI(*InternalModuleName));     // ★ ScriptModule.name = internal
ActiveModules.Add(InternalModuleName, Module);

auto* ScriptModule = (asCModule*)Engine->GetModule(TCHAR_TO_ANSI(*TempName), asGM_ALWAYS_CREATE);
ScriptModule->baseModuleName = TCHAR_TO_ANSI(*Module->ModuleName);     // ★ baseModuleName = raw

FString ModuleName = ANSI_TO_TCHAR(Context->m_currentFunction->module->baseModuleName.AddressOf());
TSharedPtr<FFileBreakpoints>& BreakpointStore = Breakpoints.FindOrAdd(ModuleName); // ★ breakpoint 用 raw/base

Frame.ModuleName = ScriptFunction->GetModuleName() ? ANSI_TO_TCHAR(ScriptFunction->GetModuleName()) : TEXT("");
// ★ callstack 发的是 ScriptFunction->GetModuleName()，即 internal name
```

**差距描述**

- `实现质量差异`：这不是“没有 debugger”，而是 **public protocol key 还没统一**。对外部 client 来说，同一个 clone session 里，breakpoint store 看见的是 raw/base module name，callstack frame 看见的却可能是 internal module name。
- `扩展风险`：一旦 `P10 UInterface` 和 `Bind API GAP` 继续把 `DebugDatabase`、`GoToDefinition`、artifact manifest 接进同一条调试链，module key 不统一会直接抬高 join 成本，导致“符号是对的、module 账本对不上”的问题。
- `测试盲区`：`AngelscriptDebugProtocolTests.cpp:107-117` 目前只验证 `ModuleName` 字符串 round-trip，本质上还停在 transport 层；并没有 clone-aware contract test。

**参考方案**

- `Hazelight` 的公开边界仍然保持单 token module name：`ActiveModules.Find(ModuleName)`、breakpoint store 和 observer lookup 都消费同一层 raw key。
  - `J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/AngelscriptManager.h:208-214`
  - `J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/AngelscriptManager.cpp:1653-1677`
  - `J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Debugging/AngelscriptDebugServer.cpp:488-492,1345-1349`

关键源码 [2]：Hazelight 不存在 internal/public module key 分叉，因此 protocol boundary 天然稳定

```cpp
// ============================================================================
// 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/AngelscriptManager.h
// 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/AngelscriptManager.cpp
// 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Debugging/AngelscriptDebugServer.cpp
// 位置: AngelscriptManager.h:208-214; AngelscriptManager.cpp:1653-1677;
//       AngelscriptDebugServer.cpp:488-492, 1348-1349
// 说明: 上游 public boundary 一直使用单一 raw module token
// ============================================================================
auto* ModRef = ActiveModules.Find(ModuleName);                        // ★ raw 名就是 map key

auto* OldModule = ActiveModules.Find(Module->ModuleName);
Module->ScriptModule->SetName(TCHAR_TO_ANSI(*Module->ModuleName));
ActiveModules.Add(Module->ModuleName, Module);                       // ★ 运行时与脚本模块都只认 raw 名

TSharedPtr<FFileBreakpoints>& BreakpointStore =
	Breakpoints.FindOrAdd(ANSI_TO_TCHAR(Context->m_currentFunction->module->baseModuleName.AddressOf()));

if (AngelscriptDebugServer::DebugAdapterVersion >= 1)
	Frame.ModuleName = ScriptFunction->GetModuleName() ? ANSI_TO_TCHAR(ScriptFunction->GetModuleName()) : TEXT("");
// ★ 在单 token 模式下，breakpoint/callstack 不会天然分叉
```

**吸收建议**

- 不要回退 current 的 clone architecture；真正要吸收的是 **“internal key 只在 engine-private lookup 存在，external protocol 永远有 canonical key”** 这条原则。
- 建议新增 `FAngelscriptModuleIdentityDescriptor` 或等价结构，最小字段至少包括：
  - `BaseModuleName`
  - `InternalModuleName`
  - `DisplayModuleName`
  - `EngineInstanceId`
- `FAngelscriptBreakpoint`、`FAngelscriptStackFrame`、future `DebugDatabase` / `BindingCoverageId` 优先统一消费 `BaseModuleName` 作为对外 canonical key；如果前端确实需要 clone disambiguation，再额外传 `InternalModuleName`，不要继续隐式复用 `ModuleName` 单字段。
- 在测试层补一条 joined regression，把 `MultiEngine.CloneModuleIsolation` 与 `Debug.Protocol.*` 串起来：同一 clone session 下，断点命中、callstack frame、future `GoToDefinition` / debug database 应该能稳定通过同一 canonical key 对账。

**优先级**

- `P1`
- **理由**：它不先于 `P10 UInterface` 的最小 type/value bridge；但在 `P10` 开始对外承诺 debugger、artifact manifest、remote diagnosis 之前必须落地。否则新增 interface family 只会继承当前 module key 的不一致。

### [D8 / D9] `CodeCoverage` 与 observer 路径仍在把 internal module name 回喂给 raw lookup API，clone 场景存在二次前缀风险

**当前状态**

- `CodeCoverage` 命中路径直接取 `CurrentFunction->GetModuleName()`，再调用 `AngelscriptManager.GetModule(ModuleName)`。
- `asCScriptFunction::GetModuleName()` 返回的是 `module->name`，而不是 `baseModuleName`。
- current clone engine 又明确把 `module->name` 设成 internal key，因此 coverage / observer 路径等于在用 internal name 调用一个默认期望 raw name 的 lookup helper。
- 当前测试只覆盖了 “active modules 上的 line coverage 统计是否成立”，见 `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptCodeCoverageTests.cpp:17-67`；没有 clone-aware coverage regression。

```
[Current AS] Coverage Lookup Path
CurrentFunction->GetModuleName()
└─ "Clone_1::Gameplay.Player"                      // ScriptFunction 返回 internal name
   └─ AngelscriptManager.GetModule(ModuleName)
      └─ MakeModuleName(...)                       // helper 默认再做 raw -> internal 折叠
         └─ "Clone_1::Clone_1::Gameplay.Player"    // clone 场景可能二次前缀
```

关键源码 [3]：coverage/observer 目前确实在用 internal name 调 raw-lookup helper

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_scriptfunction.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_module.h
// 位置: AngelscriptEngine.cpp:5539-5546;
//       as_scriptfunction.cpp:572-576;
//       as_module.h:237-240
// 说明: `GetModuleName()` 返回的是 `module->name`，不是 `baseModuleName`
// ============================================================================
if (AngelscriptManager.CodeCoverage != nullptr)
{
	int Line = Context->GetLineNumber(0, &Column, nullptr);
	asIScriptFunction* CurrentFunction = Context->GetFunction(0);
	FString ModuleName = ANSI_TO_TCHAR(CurrentFunction->GetModuleName());
	TSharedPtr<struct FAngelscriptModuleDesc> Module = AngelscriptManager.GetModule(ModuleName);
	// ★ observer 这里把 ScriptFunction.module->name 重新喂回 raw-lookup helper
}

const char *asCScriptFunction::GetModuleName() const
{
	if( module )
		return module->name.AddressOf();                            // ★ 返回 internal name
}

asCString name;
asCString baseModuleName;                                        // ★ raw/base 其实另有单独字段
```

**差距描述**

- `实现质量差异`：当前并不是“没有 coverage / 没有 observer”，而是 **observer lookup 还没有明确 internal/raw name 归一化策略**。
- `连锁风险`：只要继续沿这条 observer path 扩展 `DebugValueDescriptor`、precompiled/JIT refs、future type-family metrics，clone engine 下的统计结果就会越来越依赖隐式字符串约定。
- `测试盲区`：`AngelscriptCodeCoverageTests.cpp:17-67` 只在当前 active modules 上做普通行命中统计；`AngelscriptDebugProtocolTests.cpp:107-117` 也只 round-trip `ModuleName` 文本。两者都没有覆盖 clone + observer 的组合路径。

**参考方案**

- `Hazelight` 的 observer path 因为没有 internal/public 分叉，所以 `CurrentFunction->GetModuleName()` 回喂 `GetModule(ModuleName)` 是安全的：
  - `J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/AngelscriptManager.h:208-214`
  - `J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/AngelscriptManager.cpp:4041-4044`

关键源码 [4]：Hazelight 的同位 observer 路径在单 token module identity 下没有二次归一化问题

```cpp
// ============================================================================
// 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/AngelscriptManager.h
// 文件: J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/AngelscriptManager.cpp
// 位置: AngelscriptManager.h:208-214; AngelscriptManager.cpp:4041-4044
// 说明: 单 token identity 下，observer 直接回喂 module name 是安全的
// ============================================================================
TSharedPtr<struct FAngelscriptModuleDesc> GetModule(const FString& ModuleName)
{
	auto* ModRef = ActiveModules.Find(ModuleName);
	return ModRef == nullptr ? nullptr : *ModRef;                  // ★ 没有额外 MakeModuleName()
}

asIScriptFunction* CurrentFunction = Context->GetFunction(0);
FString ModuleName = ANSI_TO_TCHAR(CurrentFunction->GetModuleName());
TSharedPtr<struct FAngelscriptModuleDesc> Module = AngelscriptManager.GetModule(ModuleName);
// ★ 上游这里不会把 module name 再次转换
```

**吸收建议**

- current 最直接的修法不是“禁用 clone”，而是 **禁止 observer path 用 user-facing string 反查 engine-private map**。
- 第一阶段可以二选一：
  - 增加 `GetModuleByInternalName()` / `GetModuleByBaseName()` 并让 coverage/debugger 显式选 lane。
  - 或者直接新增 `GetModule(asIScriptModule*)` / `GetModuleByScriptFunction(asIScriptFunction*)`，完全绕开字符串归一化。
- `CodeCoverage`、future `DebugValueDescriptor`、precompiled/JIT refs 都应统一通过 `ModuleIdentityDescriptor` 做归一化；不要让每条 observer path 自己判断要读 `name` 还是 `baseModuleName`。
- 测试至少补两条：
  - `MultiEngine.CloneCoverageLookup`：clone 模块命中 coverage 时，module lookup 不得二次前缀。
  - `MultiEngine.CloneDebuggerModuleIdentity`：breakpoint / callstack / observer stats 对同一 clone 模块必须稳定指向同一 canonical key。

**优先级**

- `P1`
- **理由**：它和前一条一样，不阻挡 `P10` 第一刀 runtime bridge；但属于 `P10` 后续观测/诊断收口的必备修复。越晚补，后续 `DebugDatabase`、coverage、JIT/dump 都会继续放大这个 identity seam。

### [D1 / D7] current 的 module-level teardown 已经优于 Hazelight，但 future resident service 仍应转向 owner-object cleanup

**当前状态**

- current runtime module 在 `ShutdownModule()` 里负责删除 fallback ticker、弹出 `OwnedPrimaryEngine`。
- current editor module 在 `ShutdownModule()` 里负责移除 `OnObjectPreSave` handle、注销 state dump extension、注销 `ToolMenus` owner。
- `ScriptEditorMenuExtension` 再手动按 `DelegateHandle` 逐个 `RemoveAll()` 已注册 extender。
- 这说明 current 已经把 teardown 当成正式 contract；但 cleanup owner 仍主要停留在 module / extension 对象层，而不是像 `UnrealCSharp` / `puerts` 那样更系统地拆到 listener / watcher / domain 对象。

```
[Current AS] Teardown Ownership
RuntimeModule::Shutdown
├─ RemoveTicker
└─ Pop / Reset OwnedPrimaryEngine

EditorModule::Shutdown
├─ Remove PreSave handle
├─ Unregister StateDump extension
└─ UnregisterOwner(ToolMenus)

ScriptEditorMenuExtension
└─ RemoveAll(...) by DelegateHandle                // 清理仍大量停在 module/extension 层
```

关键源码 [5]：current 已经有 teardown，但主要还是 module-owned cleanup

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorMenuExtension.cpp
// 位置: AngelscriptRuntimeModule.cpp:27-39;
//       AngelscriptEditorModule.cpp:676-689;
//       ScriptEditorMenuExtension.cpp:676-703
// 说明: current 清理已经很认真，但 owner 仍偏 module / extension
// ============================================================================
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
		OwnedPrimaryEngine.Reset();                                 // ★ runtime cleanup 写在 module
	}
}

void FAngelscriptEditorModule::ShutdownModule()
{
	FCoreUObjectDelegates::OnObjectPreSave.Remove(GLiteralAssetPreSaveHandle);
	AngelscriptEditor::Private::UnregisterStateDumpExtension(StateDumpExtensionHandle);
	UToolMenus::UnregisterOwner(this);                            // ★ editor cleanup 也集中在 module
}

for (auto Extension : RegisteredExtensions)
{
	LevelEditorModule.GetAllLevelViewportContextMenuExtenders().RemoveAll(
		[&](const FLevelEditorModule::FLevelViewportMenuExtender_SelectedActors& Value)
		{
			return Value.GetHandle() == Extension.DelegateHandle;   // ★ extension 逐项清理
		});
}
```

**差距描述**

- `实现质量差异`：相对 `Hazelight` 的 `ShutdownModule() {}`，current 已经明显更强；但对比 `UnrealCSharp` / `puerts`，cleanup 责任还没有充分下沉到真正的 owner object。
- `扩展风险`：后续如果继续把 `BindingProviderManifest`、`EditorArtifactService`、debug database cache、future watcher/client 都加进来，而不同时 owner 化 teardown，module 里的清理逻辑会迅速膨胀，最后又退回“全局状态 + ShutdownModule 大串 RemoveAll/Reset”模式。
- `主线协调`：这条不是 `P10` blocker，但它会决定 `P10` 衍生的新 resident service 是否能稳定启停、是否容易写 isolated test。

**参考方案**

- `UnrealCSharp`：`FEditorListener::~FEditorListener()` 自己注销 directory watcher，`FMonoDomain::Deinitialize()` 自己释放 reflection registry 与 assembly。
  - `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp:69-84`
  - `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp:135-145`
- `puerts`：editor module 只负责 `Reset()` owner，真正 watcher callback 注销在 `FSourceFileWatcher::~FSourceFileWatcher()` 里。
  - `Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp:154-164`
  - `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/SourceFileWatcher.cpp:92-103`

关键源码 [6]：参考实现把 cleanup 收回 owner object，而不是继续堆在 module

```cpp
// ============================================================================
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp
// 文件: Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Domain/FMonoDomain.cpp
// 文件: Reference/puerts/unreal/Puerts/Source/PuertsEditor/Private/PuertsEditorModule.cpp
// 文件: Reference/puerts/unreal/Puerts/Source/JsEnv/Private/SourceFileWatcher.cpp
// 位置: FEditorListener.cpp:69-84; FMonoDomain.cpp:135-145;
//       PuertsEditorModule.cpp:154-164; SourceFileWatcher.cpp:92-103
// 说明: watcher/domain 自己清理自己，module 只做 orchestrator
// ============================================================================
FEditorListener::~FEditorListener()
{
	DirectoryWatcherModule.Get()->UnregisterDirectoryChangedCallback_Handle(
		Directory, OnDirectoryChangedDelegateHandle);              // ★ listener dtor 自己注销 watcher
}

void FMonoDomain::Deinitialize()
{
	FReflectionRegistry::Get().Deinitialize();
	UnloadAssembly();
	DeinitializeAssembly();                                      // ★ domain 自己退场
}

void FPuertsEditorModule::ShutdownModule()
{
	if (JsEnv.IsValid())
		JsEnv.Reset();
	if (SourceFileWatcher.IsValid())
		SourceFileWatcher.Reset();                                // ★ module 只 reset owner
}

FSourceFileWatcher::~FSourceFileWatcher()
{
	DirectoryWatcher->UnregisterDirectoryChangedCallback_Handle(KV.Key, KV.Value);
}                                                               // ★ watcher dtor 负责解绑细节
```

**吸收建议**

- current 不需要回退成 “更薄的 shutdown”；相反，应把未来新增的 resident service 从第一天就写成 owner-based cleanup：
  - `FAngelscriptBindingProviderService`
  - `FAngelscriptEditorArtifactService`
  - future debug database cache / watcher / client holder
- module 应只承担：
  - 创建 owner
  - `Reset()` / `Shutdown()` owner
  - `UToolMenus::UnregisterOwner(this)` 这类 UE module 级收口
- 每新增一个长驻 service，都应同步补一条最小 lifecycle regression，模式可以直接复用 `AngelscriptSubsystemTests.cpp:372-385` 这种 “Startup -> Shutdown -> handle cleared” 风格。

**优先级**

- `P2`
- **理由**：它不抢在 `P10 UInterface` 和 `Bind API GAP` 前面；但应在更大规模引入 editor artifact / debug / provider 服务之前完成设计收口。否则 current 好不容易建立起来的 teardown discipline 会再次退化回 module 大杂烩。

### 值得吸收的设计模式（增量）

- `Canonical external key, private internal key`：clone / reload / temp module name 可以继续存在，但 public protocol、artifact、coverage、debugger 必须统一消费一份 canonical external key。
- `Observer should resolve by identity object, not round-trip string`：只要 observer path 还能把 `GetModuleName()` 文本重新喂回 lookup helper，clone/multi-engine 迟早会出现隐式归一化 bug。
- `Owner object cleans itself`：module 负责 orchestration，watcher/domain/cache/service 自己负责解绑、释放和状态清理；这样 future `P10` 衍生服务才不会把 `ShutdownModule()` 重新堆爆。

### 改进路线建议（增量）

1. `P1`：先落 `ModuleIdentityDescriptor`，统一 `BaseModuleName / InternalModuleName / EngineInstanceId`，让 debugger payload、breakpoint store、future artifact manifest 共用同一份 module identity。
2. `P1`：修 `CodeCoverage` 和其它 observer path，禁止再用 `GetModuleName()` 文本回喂 raw lookup；同时补 `CloneCoverageLookup` 与 `CloneDebuggerModuleIdentity` 两条 joined regression。
3. `P2`：在 `BindingProviderManifest`、`EditorArtifactService`、future debug cache/watcher 进入主线前，先把 resident service 统一改成 owner-based cleanup，并为每个 owner 增加最小 lifecycle test。
