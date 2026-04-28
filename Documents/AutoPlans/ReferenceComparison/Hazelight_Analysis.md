# Hazelight Angelscript (UEAS2) 源码分析

> **分析对象**: Hazelight Angelscript engine integration（UEAS2 上游实现）
> **源码路径**: `J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\`
> **路径来源**: `.worktrees/ue-bind-gap-roadmap/AgentConfig.ini:13`
> **对比基准**: `Plugins/Angelscript/`
> **分析日期**: 2026-04-08

本轮不追求把 11 个维度平均摊薄，而是先把最容易误判的几条主线钉死到源码：模块边界、绑定系统、Blueprint 互操作、热重载、调试协议，以及 Hazelight 的引擎侧改造在当前插件中的替代方案。一个重要前置结论是：任务说明里的若干统计口径与当前源码快照不一致，例如 `Bind_*.cpp` 实际是 Hazelight `112`、当前插件 `123`，且按文件名比对没有 Hazelight-only 的 `Bind_*.cpp`。

## 插件架构总览

```
[Hazelight UEAS2]
├─ AngelscriptCode                     // 运行时、绑定、编译、热重载
├─ AngelscriptEditor                   // 编辑器工具
└─ AngelscriptLoader                   // 启动时只做初始化转发

[AngelPortV2]
├─ AngelscriptRuntime                  // 吸收 Loader + Runtime 主职责
├─ AngelscriptEditor                   // 编辑器扩展
├─ AngelscriptTest                     // 自动化验证层
├─ Binds / Core / ClassGenerator       // 运行时子系统拆分
└─ AngelscriptUHTTool                  // 无引擎补丁的 UHT 导出路径
```

这里最关键的变化不是“模块数变多了”，而是宿主边界变了。Hazelight 假设自己运行在已补丁过的 UE 引擎里，所以很多能力直接沉到 `CoreUObject` / UHT；当前插件则必须把同类能力尽量回收到插件内部，并靠生成代码、wrapper 和测试补足稳定性。

## [维度 D1] 插件架构与模块划分

```
[D1] Module Startup Ownership
Hazelight
├─ AngelscriptLoader::StartupModule()
│  └─ FAngelscriptCodeModule::InitializeAngelscript()
└─ AngelscriptCode owns runtime lifecycle

AngelPortV2
├─ AngelscriptRuntime::StartupModule()
│  ├─ InitializeAngelscript()
│  └─ Editor fallback ticker
├─ UAngelscriptGameInstanceSubsystem
│  ├─ acquire current engine
│  └─ own/shutdown fallback engine
└─ AngelscriptTest validates Runtime / Editor / UHT / Debugger
```

### 实现概述

Hazelight 的 `AngelscriptLoader` 并不承担热重载、目录监听或调试职责，它只是把启动时机从模块层转发到 `AngelscriptCode`。也就是说，`Loader` 是一个“初始化承接层”，不是一个独立的运行时子系统。

当前插件把这层独立模块彻底吸收进 `AngelscriptRuntime`，再用 `UAngelscriptGameInstanceSubsystem` 承接真实生命周期，并新增 `AngelscriptTest` 模块把验证正式模块化。这不是“少了 Loader，功能退化”，而是“把 Loader 的责任拆给更贴近 UE 生命周期的宿主抽象”。

### 关键源码引用

[1] Hazelight 的三模块结构里，`Loader` 明确存在，但启动函数几乎只有一行：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Angelscript.uplugin
// 位置: 18-33
// 说明: UEAS2 明确声明 Code / Editor / Loader 三模块
// ============================================================================
"Modules": [
	{
		"Name": "AngelscriptCode",
		"Type": "Runtime",
		"LoadingPhase": "PostDefault"
	},
	{
		"Name": "AngelscriptEditor",
		"Type": "Editor",
		"LoadingPhase": "PostDefault"
	},
	{
		"Name": "AngelscriptLoader",
		"Type": "Runtime",
		"LoadingPhase": "PostDefault"
	}
]
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptLoader\Private\AngelscriptLoaderModule.cpp
// 函数: FAngelscriptLoaderModule::StartupModule
// 位置: 6-9
// 说明: Loader 不做逻辑编排，只做初始化转发
// ============================================================================
void FAngelscriptLoaderModule::StartupModule()
{
	// ★ 真正初始化仍在 AngelscriptCode 内部
	FAngelscriptCodeModule::InitializeAngelscript();
}
```

[2] 当前插件把初始化、fallback tick 和生命周期接管下沉到 Runtime + Subsystem：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Angelscript.uplugin
// 位置: 18-47
// 说明: Loader 被移除，新增 Test，并显式启用 StructUtils / EnhancedInput / GameplayAbilities
// ============================================================================
"Modules": [
	{
		"Name": "AngelscriptRuntime",
		"Type": "Runtime",
		"LoadingPhase": "PostDefault"
	},
	{
		"Name": "AngelscriptEditor",
		"Type": "Editor",
		"LoadingPhase": "PostDefault"
	},
	{
		"Name": "AngelscriptTest",
		"Type": "Editor",
		"LoadingPhase": "PostDefault"
	}
],
"Plugins": [
	{ "Name": "StructUtils", "Enabled": true },
	{ "Name": "EnhancedInput", "Enabled": true },
	{ "Name": "GameplayAbilities", "Enabled": true }
]
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp
// 函数: StartupModule / InitializeAngelscript / TickFallbackPrimaryEngine
// 位置: 13-24, 138-166, 186-199
// 说明: Runtime 直接吸收 Loader 责任，并在 Editor 下增加 fallback tick
// ============================================================================
void FAngelscriptRuntimeModule::StartupModule()
{
	if (GIsEditor || IsRunningCommandlet())
	{
		// ★ 原 Loader 入口被 Runtime 模块直接接管
		InitializeAngelscript();
	}

	if (GIsEditor)
	{
		FallbackTickHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateRaw(this, &FAngelscriptRuntimeModule::TickFallbackPrimaryEngine));
	}
}

void FAngelscriptRuntimeModule::InitializeAngelscript()
{
	if (bInitializeAngelscriptCalled)
		return;

	bInitializeAngelscriptCalled = true;
	FModuleManager::Get().LoadModuleChecked(TEXT("AngelscriptRuntime"));
	if (FAngelscriptEngine* CurrentEngine = FAngelscriptEngine::TryGetCurrentEngine())
	{
		CurrentEngine->Initialize();
	}
	else
	{
		// ★ 没有现成上下文时自建主引擎，而不是依赖 Loader 保存一个薄壳模块
		OwnedPrimaryEngine = MakeUnique<FAngelscriptEngine>();
		FAngelscriptEngineContextStack::Push(OwnedPrimaryEngine.Get());
		OwnedPrimaryEngine->Initialize();
	}
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp
// 函数: Initialize / Deinitialize
// 位置: 12-29, 32-47
// 说明: 生命周期从“模块薄转发”变成“Subsystem 真正持有引擎”
// ============================================================================
void UAngelscriptGameInstanceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	bInitialized = true;
	PrimaryEngine = FAngelscriptEngine::TryGetCurrentEngine();
	if (PrimaryEngine == nullptr)
	{
		PrimaryEngine = &OwnedEngine;
		FAngelscriptEngineContextStack::Push(PrimaryEngine);
		OwnedEngine.Initialize();
		// ★ 没有外部主引擎时，由 Subsystem 自持
		bOwnsPrimaryEngine = true;
	}
}

void UAngelscriptGameInstanceSubsystem::Deinitialize()
{
	if (bOwnsPrimaryEngine)
	{
		FAngelscriptEngineContextStack::Pop(PrimaryEngine);
		if (PrimaryEngine != nullptr)
		{
			// ★ 生命周期结束时负责 Shutdown
			PrimaryEngine->Shutdown();
		}
		bOwnsPrimaryEngine = false;
	}
}
```

### 设计取舍

Hazelight 的优点是入口显眼，`Loader` 的职责肉眼可见；代价是运行时生命周期仍然堆回 `AngelscriptCode`，模块边界并没有真正形成“多宿主可复用”的结构。

当前插件的收益是：

| 对比点 | Hazelight | 当前插件 | 判断 |
| --- | --- | --- | --- |
| 初始化承接 | `AngelscriptLoader` 单行转发 | `AngelscriptRuntime` 直接初始化 | 实现方式不同 |
| 生命周期 owner | 模块中心化 | `RuntimeModule + GameInstanceSubsystem` | 架构增强 |
| Editor fallback | 未见独立 fallback tick | `TickFallbackPrimaryEngine()` | 新增增强 |
| 验证层 | 无独立测试模块 | `AngelscriptTest` | 能力增强 |

本维度的差距判断应标为“**实现方式不同**”，不是“**没有实现 Loader 能力**”。

## [维度 D2] 反射绑定机制

```
[D2] Binding Pipeline
Hazelight
├─ Bind_*.cpp static constructors
├─ global bind array
├─ CallBinds()
└─ handwritten type/function registration

AngelPortV2
├─ Bind_*.cpp static constructors
├─ engine-scoped FAngelscriptBindState
├─ CallBinds(disabled set / observation)
├─ AngelscriptUHTTool -> AS_FunctionTable_*.cpp
├─ reflective fallback for unresolved BlueprintCallable
└─ tests verify ClassFuncMaps / direct-vs-fallback stats
```

### 实现概述

Hazelight 的主绑定模型仍然是“手写 `Bind_*.cpp` + 静态注册 + 运行期类型桥接”。当前插件保留了这个骨架，但已经明显演进成三层：

1. 手写 bind：继续覆盖 `UObject`、容器、自定义 struct、GAS、EnhancedInput 等。
2. UHT 生成 FunctionTable：批量收集 BlueprintCallable/Pure，生成 `AS_FunctionTable_*.cpp` 分片。
3. Reflective fallback：当 direct bind 无法重建签名时，仍可走反射调用路径。

这意味着当前插件不再是 Hazelight 那种“几乎全靠手写 bind 文件吃完整个 API 面”，而是把“人工精细绑定”和“自动导出覆盖面”组合起来。

### 关键源码引用

[1] Hazelight 的 bind 注册只有排序和调用，没有 engine-context 隔离：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\AngelscriptBinds.cpp
// 位置: 17-39
// 说明: 所有 Bind_*.cpp 都塞进进程级静态数组
// ============================================================================
struct FBindFunction
{
	int32 BindOrder;
	TFunction<void()> Function;

	bool operator<(const FBindFunction& Other) const
	{
		return BindOrder < Other.BindOrder;
	}
};

static TArray<FBindFunction>& GetBindArray()
{
	static TArray<FBindFunction> BindArray;
	return BindArray;
}

void FAngelscriptBinds::RegisterBinds(int32 BindOrder, TFunction<void()> Function)
{
	// ★ 静态初始化阶段只记录顺序和回调
	GetBindArray().Add({BindOrder, Function});
}

void FAngelscriptBinds::CallBinds()
{
	// ★ 排序后统一执行
	GetBindArray().Sort();
	for (auto& Function : GetBindArray())
		Function.Function();
}
```

[2] 当前插件把 bind 状态和执行观测绑到 `FAngelscriptEngine` 上下文：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp
// 位置: 23-50, 120-217
// 说明: bind 状态不再天然是全局单例，同时增加禁用名单和执行观测
// ============================================================================
static FAngelscriptBindState& GetBindState()
{
	if (FAngelscriptEngine* Engine = FAngelscriptEngine::TryGetCurrentEngine())
	{
		if (FAngelscriptBindState* State = Engine->GetBindState())
		{
			// ★ 绑定数据库跟随当前引擎上下文切换
			return *State;
		}
	}

	static FAngelscriptBindState LegacyBindState;
	return LegacyBindState;
}

TMap<UClass*, TMap<FString, FFuncEntry>>& FAngelscriptBinds::GetClassFuncMaps()
{
	return GetBindState().ClassFuncMaps;
}

struct FBindFunction
{
	FName BindName;
	int32 BindOrder;
	TFunction<void()> Function;
};

void FAngelscriptBinds::RegisterBinds(FName BindName, int32 BindOrder, TFunction<void()> Function)
{
	GetBindArray().Add({BindName.IsNone() ? MakeUnnamedBindName() : BindName, BindOrder, MoveTemp(Function)});
}

void FAngelscriptBinds::CallBinds(const TSet<FName>& DisabledBindNames)
{
#if WITH_DEV_AUTOMATION_TESTS
	FAngelscriptBindExecutionObservation::BeginObservationPass(DisabledBindNames);
#endif

	for (const FBindFunction& Bind : GetSortedBindArray())
	{
		if (DisabledBindNames.Contains(Bind.BindName))
			continue;

		Bind.Function();
#if WITH_DEV_AUTOMATION_TESTS
		FAngelscriptBindExecutionObservation::RecordExecutedBind(Bind.BindName);
#endif
	}
}
```

[3] 当前插件新增了 UHT 生成 function table，并把 direct / stub / skipped 全部落成产物：

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs
// 位置: 51-78, 166-215, 282-314, 449-497
// 说明: 这是当前插件区别于 Hazelight 手写 bind 的核心增量
// ============================================================================
public static int Generate(IUhtExportFactory factory)
{
	int generatedFileCount = 0;
	HashSet<string> generatedPaths = new(StringComparer.OrdinalIgnoreCase);
	List<AngelscriptModuleGenerationSummary> moduleSummaries = new();

	// ★ 按模块扫描并生成 AS_FunctionTable_*.cpp 分片
	...
	DeleteStaleOutputs(factory, generatedPaths);
	WriteGenerationSummary(factory, moduleSummaries, csvEntries, generatedFileCount);
	return generatedFileCount;
}

private static void WriteGenerationSummary(...)
{
	int totalGeneratedEntries = moduleSummaries.Sum(static summary => summary.TotalEntries);
	int totalDirectBindEntries = moduleSummaries.Sum(static summary => summary.DirectBindEntries);
	int totalStubEntries = moduleSummaries.Sum(static summary => summary.StubEntries);

	// ★ 输出 summary json/csv，不再是“生成了就算完”
	File.WriteAllText(summaryPath, summaryJson, Encoding.UTF8);
	Console.WriteLine(
		"AngelscriptUHTTool generated {0} binding entries ({1} direct, {2} stubs) across {3} modules and {4} shard files. Summary: {5}",
		totalGeneratedEntries,
		totalDirectBindEntries,
		totalStubEntries,
		moduleSummaries.Count,
		generatedFileCount,
		summaryPath);
}

private static StringBuilder BuildShard(...)
{
	// ★ 每个 shard 都会生成注册日志，便于定位覆盖范围
	builder.Append("\tUE_LOG(Angelscript, Log, TEXT(\"[UHT] Registered %d generated BlueprintCallable entries for module %s shard %d/%d\"), ");
}
```

[4] reflective fallback 明确把“没有 direct bind”与“完全不支持”区分开：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp
// 位置: 254-287, 374-419
// 说明: 当前插件给 BlueprintCallable 提供了 direct bind 之外的兜底路径
// ============================================================================
EAngelscriptReflectiveFallbackEligibility EvaluateReflectiveFallbackEligibility(const UFunction* Function)
{
	if (Function == nullptr)
		return EAngelscriptReflectiveFallbackEligibility::RejectedNullFunction;
	if (Function->HasMetaData(NAME_BlueprintCallableReflectiveFallback_CustomThunk))
		return EAngelscriptReflectiveFallbackEligibility::RejectedCustomThunk;
	if (GetNonReturnParameterCount(Function) > BlueprintCallableReflectiveFallbackMaxArgs)
		return EAngelscriptReflectiveFallbackEligibility::RejectedTooManyArguments;
	return EAngelscriptReflectiveFallbackEligibility::Eligible;
}

bool BindBlueprintCallableReflectiveFallback(...)
{
	Entry.bReflectiveFallbackBound = false;
	if (!ShouldBindBlueprintCallableReflectiveFallback(Function))
		return false;

	// ★ 能绑定就打上 reflective 标记，而不是静默漏掉
	Entry.bReflectiveFallbackBound = true;
	return true;
}
```

[5] 现有自动化测试要求生成表必须把 `ClassFuncMaps` 推高到手写 bind 基线以上：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp
// 位置: 150-195, 360-455, 459-527
// 说明: 绑定系统已经不是“靠人工目测”的阶段
// ============================================================================
const TMap<UClass*, TMap<FString, FFuncEntry>>& ClassFuncMaps = FAngelscriptBinds::GetClassFuncMaps();
int32 TotalFunctionEntryCount = 0;
for (const TPair<UClass*, TMap<FString, FFuncEntry>>& ClassEntry : ClassFuncMaps)
{
	TotalFunctionEntryCount += ClassEntry.Value.Num();
}

// ★ 要求当前生成表明显超过旧手写基线
if (!TestTrue(TEXT("Generated function table startup pass should populate many ClassFuncMaps entries beyond the legacy handwritten baseline"), TotalFunctionEntryCount > 1000))
	...

// ★ 统计 direct / reflective / unresolved 的实际占比
if (!TestTrue(TEXT("Generated function table stats should report at least one reflective fallback binding"), ReflectiveCount > 0))
	...

// ★ summary json 还必须和实际注册行数一致
TestEqual(TEXT("Generated function table summary test should match the generated binding registration count"), TotalGeneratedEntries, CountedRegistrations);
```

### Bind 文件差异的实际结论

实际目录扫描结果如下：

| 统计口径 | Hazelight | 当前插件 | 结论 |
| --- | --- | --- | --- |
| `Bind_*.cpp` 文件数 | `112` | `123` | 当前插件多 `11` |
| bind 目录总文件数 | `125` | `150` | 当前插件多出的不全是实现文件 |
| Hazelight-only `Bind_*.cpp` | `0` | - | 没有“缺失的 11 个 bind 实现文件” |
| 当前插件新增 `Bind_*.cpp` | - | `11` | 主要是 GAS / EnhancedInput / mixin / timers / platform misc |

当前插件新增的 `Bind_*.cpp` 实际是：

| 文件 | 覆盖能力 |
| --- | --- |
| `Bind_AngelscriptGASLibrary.cpp` | GAS 异步库 direct function entry |
| `Bind_FGameplayAbilitySpec.cpp` | `FGameplayAbilitySpec` |
| `Bind_FGameplayAttribute.cpp` | `FGameplayAttribute` |
| `Bind_FGameplayEffectSpec.cpp` | `FGameplayEffectSpec` |
| `Bind_FGameplayTagBlueprintPropertyMap.cpp` | GameplayTag Blueprint property map |
| `Bind_FGenericPlatformMisc.cpp` | 平台杂项命名空间 |
| `Bind_FInputActionValue.cpp` | Enhanced Input 值类型 |
| `Bind_FInputBindingHandle.cpp` | Enhanced Input handle / binding 族 |
| `Bind_FunctionLibraryMixins.cpp` | mixin helper |
| `Bind_SystemTimers.cpp` | System timer 世界上下文桥接 |
| `Bind_UEnhancedInputComponent.cpp` | `UEnhancedInputComponent` |

### 设计取舍

本维度应明确区分三类判断：

| 子项 | 判断 |
| --- | --- |
| Hazelight 手写 bind 主线 | 当前插件**已保留** |
| bind 状态作用域 | 当前插件是**实现方式不同**，不是缺失 |
| BlueprintCallable 大规模覆盖 | 当前插件是**能力增强**，因为新增 UHT 表和 reflective fallback |

## [维度 D3] Blueprint 交互

```
[D3] Blueprint Interop Chain
UFUNCTION meta
├─ ScriptMixin / BlueprintEvent metadata
├─ FAngelscriptFunctionSignature
│  └─ static -> member remap when first arg matches mixin
├─ Bind_BlueprintEvent.cpp
│  ├─ cache by script name
│  └─ CallMixinWithSignature(...)
└─ script-generated UClass
   └─ Blueprint child inherits and triggers BlueprintOverride
```

### 实现概述

Hazelight 的 Blueprint 交互不是单一“脚本覆写 Blueprint 事件”，而是三条链一起工作：

1. `Helper_FunctionSignature.h` 负责把 `ScriptMixin` 之类的静态函数重写成脚本侧成员签名。
2. `Bind_BlueprintEvent.cpp` 缓存 script name 到 `UFunction` 的映射，并在调用时注入 mixin object。
3. 生成出来的脚本 `UClass` 要能继续被 Blueprint 继承，形成“Script parent -> Blueprint child”链。

当前插件这三条链都还在，只是事件调用入口从旧式 `FASBindFunctionPointers` 走向 `ASAutoCaller::FunctionCaller::Make()`，说明桥接器内部实现升级了，但语义没有放弃。

### 关键源码引用

[1] `ScriptMixin` 仍然决定“静态函数是否在脚本侧折叠成成员方法”：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Binds\Helper_FunctionSignature.h
// 位置: 279-320
// 说明: Hazelight 会在签名阶段把 mixin 风格静态函数改写成成员样式
// ============================================================================
// If our class is marked as `ScriptMixin` or function as `ScriptMethod`, and our argument matches, bind it as a member
...
if (!FoundMixin.IsEmpty())
{
	// ★ 命中 mixin 后剥掉首参，让脚本侧看到成员函数形态
	ArgumentTypes.RemoveAt(0);
	ArgumentNames.RemoveAt(0);
	ArgumentDefaults.RemoveAt(0);
	ClassName = FoundMixin;
	bStaticInScript = false;
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h
// 位置: 276-315
// 说明: 当前插件保留 ScriptMixin 折叠逻辑
// ============================================================================
// If our class is marked as a 'script mixin', and our argument matches, bind it as a member
bool bFoundMixin = false;
const FString& MixinClasses = Function->GetOuterUClass()->GetMetaData(NAME_Signature_ScriptMixin);
...
if (FirstParamType == Mixin)
{
	// ★ 仍然通过“剥掉首参”把 static UFUNCTION 变成脚本成员方法
	ArgumentTypes.RemoveAt(0);
	ArgumentNames.RemoveAt(0);
	ArgumentDefaults.RemoveAt(0);
	ClassName = Mixin;
	bStaticInScript = false;
}
```

[2] Blueprint event 绑定在两边都维护 `script name -> UFunction` 映射，并在调用时注入 mixin object：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp
// 位置: 67-74, 492-640
// 说明: 当前插件延续 BlueprintEvent / mixin 的桥接结构
// ============================================================================
TMap<UClass*, TMap<FString, UFunction*>> GBlueprintEventsByScriptName;

struct FBlueprintEventSignature
{
	FAngelscriptTypeUsage MixinType;
	...
};

void BindBlueprintEvent(...)
{
	FAngelscriptFunctionSignature Signature(InType, Function, OverrideName);
	auto* Sig = new FBlueprintEventSignature;
	...
	Sig->MixinType = FAngelscriptTypeUsage();
	Sig->MixinType.Type = InType;

	// ★ 事件桥接器升级到了 ASAutoCaller，但 mixin 注入语义没变
	DBMethod.FunctionId = FAngelscriptBinds::BindMethodDirect(
		...,
		asFUNCTION(CallMixinWithSignature), asCALL_GENERIC, ASAutoCaller::FunctionCaller::Make(), Sig);

	GBlueprintEventsByScriptName.FindOrAdd(CastChecked<UClass>(Function->GetOuter())).Add(Signature.ScriptName, Function);
}
```

[3] 当前插件的学习型测试明确要求“脚本类 -> Blueprint 子类 -> BeginPlay 的 BlueprintOverride”整条链能跑通：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningScriptClassToBlueprintTraceTests.cpp
// 位置: 167-225
// 说明: 这里不是单元点测，而是把继承链完整走一遍
// ============================================================================
Trace.AddStep(TEXT("CompileScriptClass"), TEXT("Compiled the script parent class with BlueprintOverride so Unreal reflection can generate a UClass that Blueprint can inherit from"));
...
Trace.AddStep(TEXT("CreateBlueprintChild"), TEXT("Created a transient Blueprint asset that inherits from the generated script class"));
...
Trace.AddStep(TEXT("CompileBlueprintChild"), TEXT("Compiled the Blueprint asset into a generated Blueprint class that preserves the script parent hierarchy"));
...
Trace.AddKeyValue(TEXT("InheritsFromScriptClass"), BlueprintClass->IsChildOf(ScriptClass) ? TEXT("true") : TEXT("false"));
...
Trace.AddStep(TEXT("InvokeBeginPlay"), TEXT("Invoked BeginPlay on the spawned actor to trigger the script-defined BlueprintOverride"));
```

### 设计取舍

本维度最容易说错的一点是：当前插件并不是“没有 Blueprint mixin / override / inheritance chain”，而是**主线仍在，只是内部桥接器实现不同**。Hazelight 更依赖引擎里 `bIsScriptClass` 等补丁让这条链天然成为一等公民；当前插件则需要靠插件内部的 `UASClass`、bridge 和测试来反复校准。

因此 D3 的判断应是“**主体能力保留，工程化实现方式更插件内聚**”，不是“缺失实现”。

## [维度 D4] 热重载

```
[D4] Hot Reload Flow
Hazelight
├─ checker thread
├─ FileChangesDetectedForReload
├─ PerformHotReload(changed files)
└─ ClassGenerator soft/full reload

AngelPortV2
├─ Editor directory watcher
├─ checker thread or direct queue
├─ dependency expansion
│  ├─ automatic import graph
│  └─ reverse imported-module graph
├─ PerformHotReload(...)
├─ FullReloadSuggested / Required classification
└─ hot-reload tests + queued full reload after PIE
```

### 实现概述

Hazelight 的热重载框架已经很完整：文件变化队列、hot reload thread、`PerformHotReload()`、`ClassGenerator::Setup()`、`SoftReload / FullReloadSuggested / FullReloadRequired`。但 UEAS2 的 `PerformHotReload()` 在当前快照里对依赖文件的处理明显更保守，直接走“changed files only”路径，把更多依赖决策留给 compile step。

当前插件在 `FAngelscriptEngine::PerformHotReload()` 里补上了两套依赖扩展逻辑：

1. `automatic import` 开启时，沿 `moduleDependencies` 递归标记受影响模块。
2. 否则，根据 `ImportedModules` 先建反向依赖图，再把受影响模块的所有 section 推入 reload 集。

这意味着当前插件的热重载不是简单照抄 Hazelight，而是把依赖传播做成了显式策略分支。

### 关键源码引用

[1] Hazelight 当前快照里的 `PerformHotReload()` 对文件集合处理较薄：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\AngelscriptManager.cpp
// 位置: 1161-1245
// 说明: UEAS2 当前快照把依赖扩展 largely 交给 compile step
// ============================================================================
bool FAngelscriptManager::PerformHotReload(ECompileType CompileType, const TArray<FFilenamePair>& InReloadFiles)
{
	...
	TSet<FFilenamePair> FilesToHotReload;
	if (FileList.Num() > 0)
	{
		// ★ 当前快照这里直接只保留 changed files
		FilesToHotReload.Append(FileList);
	}
	...
	ECompileResult Result = CompileModules(CompileType, Preprocessor.GetModulesToCompile(), CompiledModules);
	...
	if (GEngine && bCompletedAssetScan && HotReloadTestRunner != nullptr && HotReloadTestRunner->ShouldRunUnitTestsOnHotReload())
	{
		// ★ hot reload 后仍可挂测试，但 PrepareTests 入参较少
		HotReloadTestRunner->PrepareTests(GetActiveModules(), CompiledModules, RelativeFileList);
	}
}
```

[2] 当前插件在 Runtime 里加入依赖传播和 editor watcher：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp
// 位置: 61-86
// 说明: 文件变化不再完全依赖引擎管理器内部线程，Editor watcher 直接向 Engine 队列推送
// ============================================================================
Engine.FileDeletionsDetectedForReload.AddUnique({ AbsolutePath, RelativePath });
Engine.FileChangesDetectedForReload.AddUnique({ AbsolutePath, RelativePath });
...
Engine.FileChangesDetectedForReload.AddUnique(ScriptFile);
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 2253-2490
// 说明: 当前插件显式展开依赖模块，而不是只重编译被改文件
// ============================================================================
bool FAngelscriptEngine::PerformHotReload(ECompileType CompileType, const TArray<FFilenamePair>& InReloadFiles)
{
	...
	if (GAngelscriptRecompileAvoidance && ShouldUseAutomaticImportMethod())
	{
		// ★ 开启 automatic import + recompile avoidance 时，只编 changed files
		FilesToHotReload.Append(FileList);
	}
	else
	{
		// ★ 否则显式构建依赖传播
		if (ShouldUseAutomaticImportMethod())
		{
			// 沿 asCModule::moduleDependencies 递归标记
			...
			FilesToHotReload.Add(FFilenamePair{ Section.AbsoluteFilename, Section.RelativeFilename });
		}
		else
		{
			// 沿 ImportedModules 建 reverse deps，再把整模块 section 推入 reload 集
			...
			FilesToHotReload.Add(FFilenamePair{ Section.AbsoluteFilename, Section.RelativeFilename });
		}
	}
	...
	HotReloadTestRunner->PrepareTests(GetActiveModules(), CompiledModules, RelativeFileList, ShouldUseAutomaticImportMethod());
}
```

[3] 当前插件把 soft/full reload 的“降级策略”写得比 Hazelight 更明确：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 3936-3999
// 说明: FullReloadSuggested 和 FullReloadRequired 被显式区分
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
			// ★ PIE 中先 soft reload，并把 full reload 延后到安全时机
			UE_LOG(Angelscript, Warning, TEXT("%s"), *Msg);
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
			// ★ 真正必须 full reload 时，拒绝 swap-in，并保留旧代码继续运行
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

[4] 热重载分析和场景测试已经补齐到“能证明策略分类”的程度：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadAnalysisTests.cpp
// 位置: 123-174, 213-225, 262-315, 357-365
// 说明: 当前插件直接测 reload requirement，而不是只测“能不能编过”
// ============================================================================
// 属性数量变化 => FullReloadSuggested 或 FullReloadRequired
TestTrue(TEXT("Property count change should request a full reload path"), bWantsFullReload || bNeedsFullReload);

// body-only 改动 => 保持 soft reload
TestEqual(TEXT("Body-only change should remain soft reload"), ReloadRequirement, FAngelscriptClassGenerator::SoftReload);

// class add => Suggested
TestEqual(TEXT("Class add should suggest a full reload"), ReloadRequirement, FAngelscriptClassGenerator::FullReloadSuggested);

// class remove / function signature 改动 => Required
TestEqual(TEXT("Class remove should require a full reload"), ReloadRequirement, FAngelscriptClassGenerator::FullReloadRequired);
TestEqual(TEXT("Function signature change should require a full reload"), ReloadRequirement, FAngelscriptClassGenerator::FullReloadRequired);
```

### 设计取舍

对 D4 应这样定性：

| 子项 | Hazelight | 当前插件 | 判断 |
| --- | --- | --- | --- |
| hot reload 主框架 | 有 | 有 | 已实现 |
| editor 文件变化接入 | manager 内部主导 | 增加 `AngelscriptDirectoryWatcherInternal` | 能力增强 |
| 依赖传播 | 当前快照更偏 changed files only | 显式两套依赖传播策略 | 实现质量差异 |
| full reload 延迟策略 | 有 | 有，测试更细 | 已保留并增强 |
| hot reload 后测试 | 有 | 有，PrepareTests 入参更多 | 能力增强 |

顺带回答任务里的 Loader 问题：`Loader` 从来不是热重载核心。热重载的真正 owner 一直是 manager/engine + class generator；当前插件移除 `Loader` 后，靠 `RuntimeModule + Engine + DirectoryWatcher + Subsystem` 继续承担这条链。

## [维度 D5] 调试与开发体验

```
[D5] DebugServer V2
Client
├─ StartDebugging(adapterVersion=2)
├─ DebugServerVersion(version=2)
├─ SetBreakpoint / RequestVariables / RequestEvaluate / GoToDefinition
└─ Continue / StepIn / StepOver / StepOut

Server delta
├─ Hazelight: message enum includes StopPIE
└─ AngelPortV2: adds reusable envelope serializer, keeps core V2 surface
```

### 实现概述

从消息枚举和握手代码看，当前插件仍然是 `DEBUG_SERVER_VERSION 2`，并且保留了变量查看、表达式求值、跳转定义、断点、步进等核心消息。也就是说，D5 不是“协议断代”。

真正的变化有两点：

1. 当前插件新增了 `FAngelscriptDebugMessageEnvelope` 和序列化辅助函数，便于统一打包消息体。
2. 消息面缩小了一个命令：Hazelight 头文件里还有 `StopPIE`，当前插件枚举里已去掉它。

因此这里应判定为“**协议基本一致，少量消息面收窄，序列化实现不同**”。

### 关键源码引用

[1] 两边都声明 `DEBUG_SERVER_VERSION 2`，但 Hazelight 多一个 `StopPIE`：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Debugging\AngelscriptDebugServer.h
// 位置: 49-79, 109-115
// 说明: UEAS2 的 V2 消息面包含 StopPIE
// ============================================================================
#define DEBUG_SERVER_VERSION 2
...
RequestVariables,
Variables,
RequestEvaluate,
Evaluate,
GoToDefinition,
...
DebugServerVersion,
CreateBlueprint,
...
StopPIE,
...
struct FDebugServerVersionMessage : FDebugMessage
{
	int32 DebugServerVersion = 0;
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h
// 位置: 51-93, 118-124
// 说明: 当前插件仍是 V2，但加入 envelope helper，且枚举不再含 StopPIE
// ============================================================================
#define DEBUG_SERVER_VERSION 2
...
RequestVariables,
Variables,
RequestEvaluate,
Evaluate,
GoToDefinition,
...
DebugServerVersion,
CreateBlueprint,
...
ClearDataBreakpoints,

struct FAngelscriptDebugMessageEnvelope
{
	EDebugMessageType MessageType = EDebugMessageType::Disconnect;
	TArray<uint8> Body;
};

ANGELSCRIPTRUNTIME_API bool SerializeDebugMessageEnvelope(...);
ANGELSCRIPTRUNTIME_API bool TryDeserializeDebugMessageEnvelope(...);
```

[2] 握手逻辑保持一致，仍然回发 `DebugServerVersion`：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Debugging\AngelscriptDebugServer.cpp
// 位置: 788-803
// 说明: Hazelight 的 V2 握手
// ============================================================================
FStartDebuggingMessage Msg;
*Datagram << Msg;

bIsDebugging = true;
AngelscriptDebugServer::DebugAdapterVersion = Msg.DebugAdapterVersion;

FDebugServerVersionMessage DebugServerVersionMessage;
DebugServerVersionMessage.DebugServerVersion = DEBUG_SERVER_VERSION;
SendMessageToClient(Client, EDebugMessageType::DebugServerVersion, DebugServerVersionMessage);
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 位置: 897-907
// 说明: 当前插件保持相同握手语义
// ============================================================================
FStartDebuggingMessage Msg;
*Datagram << Msg;

bIsDebugging = true;
AngelscriptDebugServer::DebugAdapterVersion = Msg.DebugAdapterVersion;

FDebugServerVersionMessage DebugServerVersionMessage;
DebugServerVersionMessage.DebugServerVersion = DEBUG_SERVER_VERSION;
SendMessageToClient(Client, EDebugMessageType::DebugServerVersion, DebugServerVersionMessage);
```

[3] 当前插件已经把调试协议做成自动化测试，而不是靠 IDE 联调回归：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSmokeTests.cpp
// 位置: 49-88, 125-132
// 说明: Smoke test 直接验证握手和退出状态
// ============================================================================
if (!TestTrue(TEXT("Debugger.Smoke.Handshake should send StartDebugging"), Client.SendStartDebugging(2)))
	...

if (!TestTrue(TEXT("Debugger.Smoke.Handshake should receive the DebugServerVersion response"), bReceivedDebugVersion))
	...

TestEqual(TEXT("Debugger.Smoke.Handshake should report the current debug server version"), DebugServerVersion->DebugServerVersion, DEBUG_SERVER_VERSION);
TestTrue(TEXT("Debugger.Smoke.Handshake should put the session in debugging mode after StartDebugging"), Session.GetDebugServer().bIsDebugging);

if (!TestTrue(TEXT("Debugger.Smoke.Handshake should send StopDebugging"), Client.SendStopDebugging()))
	...

TestTrue(TEXT("Debugger.Smoke.Handshake should leave debugging mode after StopDebugging"), bStoppedDebugging);
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp
// 位置: 334-447, 456-647
// 说明: step in / over / out 都有独立自动化场景
// ============================================================================
"Angelscript.TestModule.Debugger.Stepping.StepIn"
"Angelscript.TestModule.Debugger.Stepping.StepOver"
"Angelscript.TestModule.Debugger.Stepping.StepOut"

// ★ 测试断言不是只看“收到 stop”，而是比对停靠行号和栈深
TestEqual(TEXT("Stepping.StepIn should land inside Inner()"), ...);
TestEqual(TEXT("Stepping.StepOver should land at the line after the call"), ...);
TestTrue(TEXT("Stepping.StepOut should reduce stack depth after returning"), ...);
```

### 设计取舍

本维度的准确判断应是：

| 子项 | 结论 |
| --- | --- |
| V2 协议版本 | **已保持一致** |
| Variables / Evaluate / GoToDefinition | **已保持一致** |
| `StopPIE` | **消息面收窄**，不是整个调试器缺失 |
| 序列化实现 | **实现方式不同**，当前插件增加 envelope helper |
| 回归方式 | **实现质量差异**，当前插件更多依赖自动化测试 |

## [维度 D8/D11] 引擎侧修改意味着什么，以及当前插件如何绕开

```
[D8/D11] Engine Patch vs Plugin Path
Hazelight UEAS2
├─ UHT parser adds AngelscriptPropertyFlags
├─ generated code emits extra flag word
├─ Class.h exposes bIsScriptClass / RuntimeCallFunction
└─ ScriptCore / UObject / Actor / Blueprint pipeline recognize script classes

AngelPortV2
├─ no UnrealHeaderTool engine patch required
├─ AngelscriptUHTTool exports FunctionTable shards
├─ wrapper UFUNCTION libraries cover awkward signatures
├─ generated summary/csv artifacts quantify coverage
└─ tests enforce direct / stub / skipped accounting
```

### 实现概述

Hazelight 的 UHT 修改不是“优化项”，而是把 Angelscript 语义直接写进 Unreal 反射生成链：属性多一组 `AngelscriptPropertyFlags`，函数参数能标记 `WorldContext` / `CppRef` / `CppConst` / `CppEnumAsByte`，运行时 `UFunction` 还新增 `RuntimeCallFunction()` / `GetRuntimeValidateFunction()`。这意味着 script class / script function 在引擎里更接近 Blueprint/Native 的一等公民。

当前插件没有继续走“给引擎打补丁”这条路，而是把可替代部分分成两类处理：

1. 可批量生成的 BlueprintCallable surface：交给 `AngelscriptUHTTool` 生成 `AS_FunctionTable_*.cpp`。
2. UHT 无法自然表达或不值得补引擎的棘手类型：交给 wrapper / 手写 bind，例如 `UAngelscriptAbilitySystemComponent`。

因此本维度不是“当前插件完全达到 Hazelight 引擎补丁等价能力”，而是“**在不改引擎前提下覆盖了很大一块功能面，但承认还有 wrapper 成本**”。

### 关键源码引用

[1] Hazelight 确实把 Angelscript 标志位写进了 UHT 类型系统和生成代码：

```csharp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Programs\Shared\EpicGames.UHT\Types\UhtProperty.cs
// 位置: 759-763, 1508-1510
// 说明: UHT property 对象直接携带 AngelscriptPropertyFlags，并把它写进生成代码
// ============================================================================
// AS FIX(LV): Angelscript-specific property flags
public EAngelscriptPropertyFlags AngelscriptPropertyFlags { get; set; }
...
// AS FIX(LV): Angelscript-specific property flags
builder.Append("0x").AppendFormat(CultureInfo.InvariantCulture, "{0:x4}", (ushort)AngelscriptPropertyFlags).Append(", ");
```

```csharp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Programs\Shared\EpicGames.Core\UnrealEngineTypes.cs
// 位置: 1316-1325
// 说明: 这些 flag 不是插件私有常量，而是 UHT / generated code 共用的引擎类型
// ============================================================================
[Flags]
public enum EAngelscriptPropertyFlags : ushort
{
	None = 0,
	CppConst       = 0x0001,
	CppRef         = 0x0002,
	CppEnumAsByte  = 0x0004,
	RuntimeGenerated = 0x0008,
	WorldContext   = 0x0010,
}
```

```csharp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Programs\Shared\EpicGames.UHT\Parsers\UhtFunctionParser.cs
// 位置: 575-586
// 说明: WorldContext 参数在 UHT 解析期就被打标
// ============================================================================
if (function.MetaData.ContainsKey("WorldContext"))
{
	string WorldContextArgName = function.MetaData.GetValueOrDefault("WorldContext");
	...
	if (Property.SourceName == WorldContextArgName)
	{
		Property.AngelscriptPropertyFlags |= EAngelscriptPropertyFlags.WorldContext;
	}
}
```

```csharp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Programs\Shared\EpicGames.UHT\Exporters\CodeGen\UhtHeaderCodeGeneratorCppFile.cs
// 位置: 2548-2572
// 说明: 生成器会据此还原 C++ ref / const / enum-as-byte 语义
// ============================================================================
bool bIsRef = Property.AngelscriptPropertyFlags.HasFlag(EAngelscriptPropertyFlags.CppRef);
bool bIsConst = Property.AngelscriptPropertyFlags.HasFlag(EAngelscriptPropertyFlags.CppConst);
...
if (Property is UhtByteProperty ByteProp && ByteProp.Enum != null && ByteProp.AngelscriptPropertyFlags.HasFlag(EAngelscriptPropertyFlags.CppEnumAsByte))
{
	...
}
```

[2] Hazelight 还把运行时调用入口补进了 `CoreUObject`：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Runtime\CoreUObject\Public\UObject\Class.h
// 位置: 2606-2610, 3909-3913
// 说明: 引擎层明确承认 runtime-generated function / script class
// ============================================================================
// AS FIX (LV): Allow calling runtime generated functions directly through UFunction
virtual void RuntimeCallFunction(UObject* Object, FFrame& Stack, RESULT_DECL) { ensureAlways(false); }
virtual void RuntimeCallEvent(UObject* Object, void* Params) { ensureAlways(false); }
virtual UFunction* GetRuntimeValidateFunction() { ensureAlways(false); return nullptr; }

TMap<FName, ASAutoCaller::FReflectedFunctionPointers> ASReflectedFunctionPointers;
void* ScriptTypePtr = nullptr;
bool bIsScriptClass = false;
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Runtime\CoreUObject\Private\UObject\ScriptCore.cpp
// 位置: 1114-1117, 2107-2113
// 说明: ScriptCore 直接认识 FUNC_RuntimeGenerated 与 runtime validate
// ============================================================================
// HAZE FIX(LV): Angelscript function calls
if (Function->FunctionFlags & (FUNC_Native | FUNC_RuntimeGenerated))
{
	...
}

if (Function->FunctionFlags & FUNC_RuntimeGenerated)
{
	// ★ 网络 RPC 校验函数也能从 script runtime 拉取
	UFunction* ValidateFunction = Function->GetRuntimeValidateFunction();
}
```

[3] 当前插件明确选择“不做引擎补丁”，必要处用 wrapper 顶上：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilitySystemComponent.h
// 位置: 32
// 说明: 维护者在源码里直接承认这是“避免引擎修改”的折中
// ============================================================================
// It's a shame we have to wrap. But it's not a hot path, and it's better than doing an engine mod.
// Best would of course be if AS type binds were made aware to UHT so binding worked here.
```

[4] 当前插件把可自动化的部分迁到插件内 UHT exporter，并且要求产出可审计：

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs
// 位置: 33-53, 65-83, 99-161
// 说明: 当前插件不改引擎 UHT，而是作为插件 exporter 扫描 BlueprintCallable/Pure
// ============================================================================
int skippedCount = 0;
List<AngelscriptSkippedFunctionEntry> skippedEntries = new();
int generatedFileCount = AngelscriptFunctionTableCodeGenerator.Generate(factory);

CountBlueprintCallableFunctions(..., ref reconstructedCount, ref skippedCount);
WriteSkippedEntriesCsv(factory, skippedEntries);
WriteSkippedReasonSummaryCsv(factory, skippedEntries);

Console.WriteLine(
	"AngelscriptUHTTool exporter visited {0} packages, {1} classes, {2} BlueprintCallable/Pure functions, reconstructed {3}, skipped {4}, wrote {5} module files.",
	...);
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp
// 位置: 459-748
// 说明: 生成物必须带 summary json / module csv / entry csv / skipped csv / reason summary
// ============================================================================
const FString SummaryPath = FPaths::Combine(GeneratedDirectory, TEXT("AS_FunctionTable_Summary.json"));
...
const FString ModuleCsvPath = FPaths::Combine(GeneratedDirectory, TEXT("AS_FunctionTable_ModuleSummary.csv"));
const FString EntryCsvPath = FPaths::Combine(GeneratedDirectory, TEXT("AS_FunctionTable_Entries.csv"));
...
const FString SkippedCsvPath = FPaths::Combine(GeneratedDirectory, TEXT("AS_FunctionTable_SkippedEntries.csv"));
const FString ReasonSummaryCsvPath = FPaths::Combine(GeneratedDirectory, TEXT("AS_FunctionTable_SkippedReasonSummary.csv"));
```

### 设计取舍

这一组差异的本质不是“Hazelight 更先进，当前插件更落后”，而是交付前提不同：

| 问题 | Hazelight | 当前插件 | 判断 |
| --- | --- | --- | --- |
| UHT 是否理解 Angelscript 属性语义 | 是，直接改 UHT | 否，改为插件 exporter | 实现方式不同 |
| `UFunction` runtime hook | 引擎层内建 | 不做引擎补丁 | 能力边界不同 |
| BlueprintCallable 大面覆盖 | 依赖引擎补丁 + 手写 bind | 插件内 UHT 表 + reflective fallback | 实现方式不同 |
| 棘手类型处理 | 可借引擎补丁走更直接路径 | 通过 wrapper / helper 兜底 | 非“没有实现”，是“成本转移” |
| 可审计性 | 未见产物级 summary/csv | 有 summary/csv/tests | 当前插件更强 |

如果目标是“让 script class / runtime function 完全像引擎内建类型那样在所有 CoreUObject 路径上一等公民”，Hazelight 的 engine patch 仍然更彻底。  
如果目标是“作为插件分发，尽量不改引擎，也能覆盖大部分 BlueprintCallable / bind 场景”，当前插件已经给出了可运维、可测试、可审计的替代路径。

## 与 Angelscript 差异速查

| 维度 | 现状速判 | 本轮结论 |
| --- | --- | --- |
| D1 架构与模块 | `Code + Editor + Loader` vs `Runtime + Editor + Test` | Loader 能力被吸收，测试层新增 |
| D2 反射绑定 | 手写 bind vs 手写 bind + UHT 表 + reflective fallback | 当前插件绑定栈更厚，不存在“缺失 11 个 bind 实现文件” |
| D3 Blueprint 交互 | 主体主线都在 | 不是缺失，而是 bridge 内部实现不同 |
| D4 热重载 | 两边都有 soft/full reload | 当前插件依赖传播和验证更细 |
| D5 调试体验 | DebugServer V2 主体一致 | 当前插件少 `StopPIE`，多 envelope/helper/test |
| D6 代码生成与 IDE | 当前插件更偏插件内 UHT 产物 | 本轮未单独展开 |
| D7 编辑器集成 | 两边都较深 | 本轮未单独展开 |
| D8 性能与优化 | 与 engine patch 交织 | 本轮重点转到“无引擎补丁替代路径” |
| D9 测试基础设施 | 当前插件更显式模块化 | `AngelscriptTest` 是重要结构增量 |
| D10 文档与示例 | 当前仓库文档面更完整 | 本轮未单独展开 |
| D11 部署与打包 | 当前插件更插件化 | 本轮与 D8 合并看“无引擎补丁分发” |

## 小结

1. `AngelscriptLoader` 在 Hazelight 中只是初始化转发层。当前插件移除它并不代表能力缺失，而是把责任重分配给 `AngelscriptRuntime`、`UAngelscriptGameInstanceSubsystem` 和 editor fallback tick。
2. 反射绑定是当前插件相对 Hazelight 变化最大的维度之一，但变化方向不是“少了 bind”，而是“手写 bind 之上又叠了 UHT FunctionTable 和 reflective fallback”。当前源码快照下，实际是当前插件多 `11` 个 `Bind_*.cpp`。
3. 热重载和调试协议两条线都没有断代。热重载在依赖传播和测试上更细；DebugServer 仍是 V2，只是消息面收窄了 `StopPIE`。
4. Hazelight 的引擎侧补丁真正买到的是“让 script class / property / runtime function 成为引擎反射链的一等公民”。当前插件没有照搬这条路，而是用插件内 UHT exporter、wrapper 和大量自动化测试换取“无需改引擎也能交付”的能力。

---

## 深化分析 (2026-04-08 18:18:19)

### [维度 D2] `Bind_*.cpp` 的真实差异是“能力簇新增”，不是“上游文件缺失”

```
[D2-Deep] Bind File Delta by Capability Cluster
Hazelight Bind_*.cpp snapshot
├─ total: 112                              // 当前快照下的实际数量
└─ filename-only delta: none               // 没有 Hazelight-only 的 Bind_*.cpp

AngelPortV2 Bind_*.cpp snapshot
├─ total: 123                              // 当前插件实际数量
├─ GAS cluster
│  ├─ Bind_AngelscriptGASLibrary.cpp
│  ├─ Bind_FGameplayAbilitySpec.cpp
│  ├─ Bind_FGameplayAttribute.cpp
│  ├─ Bind_FGameplayEffectSpec.cpp
│  └─ Bind_FGameplayTagBlueprintPropertyMap.cpp
├─ EnhancedInput cluster
│  ├─ Bind_FInputActionValue.cpp
│  ├─ Bind_FInputBindingHandle.cpp
│  └─ Bind_UEnhancedInputComponent.cpp
└─ Runtime glue cluster
   ├─ Bind_FunctionLibraryMixins.cpp
   ├─ Bind_SystemTimers.cpp
   └─ Bind_FGenericPlatformMisc.cpp
```

目录扫描结果显示：Hazelight 当前快照的 `Bind_*.cpp` 是 `112`，当前插件是 `123`，且按文件名比较不存在 Hazelight-only 的 `Bind_*.cpp`。因此，任务说明中的“缺失 11 个文件、仅新增 9 个文件”并不符合当前源码快照；若要继续追“缺失能力”，应转向共享文件内的实现差异，而不是文件名差集。

更有价值的新发现是：这 `11` 个新增文件并非零散补丁，而是集中补了三个能力簇。也就是说，当前插件把 Hazelight 当年更多依赖引擎环境或共享绑定文件承载的能力，拆成了更可审计、可按模块维护的独立 bind 文件。

[1] GAS 相关新增不是简单“多几个 helper”，而是把异步能力和复杂 struct 直接做成一等绑定面：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AngelscriptGASLibrary.cpp
// 位置: 4-14
// 说明: 当前插件单独为 GAS async library 建立绑定入口
// ============================================================================
AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_AngelscriptGAS
(
	(int32)FAngelscriptBinds::EOrder::Late - 1,
	[]()
	{
		// ★ WaitForAttributeChanged / WaitGameplayEventToActor 等异步节点被直接暴露给脚本
		FAngelscriptBinds::AddFunctionEntry(UAngelscriptAbilityAsyncLibrary::StaticClass(), "WaitForAttributeChanged", { ERASE_FUNCTION_PTR(UAngelscriptAbilityAsyncLibrary::WaitForAttributeChanged, (AActor*, const FGameplayAttribute&, const bool), ERASE_ARGUMENT_PACK(UAbilityAsync_WaitAttributeChanged*)) });
		FAngelscriptBinds::AddFunctionEntry(UAngelscriptAbilityAsyncLibrary::StaticClass(), "WaitGameplayEventToActor", { ERASE_FUNCTION_PTR(UAngelscriptAbilityAsyncLibrary::WaitGameplayEventToActor, (AActor*, const FGameplayTag, const bool, const bool), ERASE_ARGUMENT_PACK(UAbilityAsync_WaitGameplayEvent*)) });
		FAngelscriptBinds::AddFunctionEntry(UAngelscriptAbilityAsyncLibrary::StaticClass(), "WaitGameplayTagAddToActor", { ERASE_FUNCTION_PTR(UAngelscriptAbilityAsyncLibrary::WaitGameplayTagAddToActor, (AActor*, const FGameplayTag, const bool), ERASE_ARGUMENT_PACK(UAbilityAsync_WaitGameplayTagAdded*)) });
		FAngelscriptBinds::AddFunctionEntry(UAngelscriptAbilityAsyncLibrary::StaticClass(), "WaitGameplayTagRemoveFromActor", { ERASE_FUNCTION_PTR(UAngelscriptAbilityAsyncLibrary::WaitGameplayTagRemoveFromActor, (AActor*, const FGameplayTag, const bool), ERASE_ARGUMENT_PACK(UAbilityAsync_WaitGameplayTagRemoved*)) });
	}
);
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FGameplayAbilitySpec.cpp
// 位置: 5-16, 32-79
// 说明: 复杂 GAS struct 被拆成可构造、可读写、可绕开 bit-field 限制的脚本表面
// ============================================================================
AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_FGameplayAbilitySpec(FAngelscriptBinds::EOrder::Late, [] {
	auto FGameplayAbilitySpec_ = FAngelscriptBinds::ExistingClass("FGameplayAbilitySpec");

	FGameplayAbilitySpec_.Constructor(
	"void f(TSubclassOf<UGameplayAbility> InAbilityClass, int32 InLevel = 1, int32 InInputID = -1, UObject InSourceObject = nullptr)",
	[](FGameplayAbilitySpec* Address, TSubclassOf<UGameplayAbility> InAbilityClass, int32 InLevel, int32 InInputID, UObject* InSourceObject)
	{
		// ★ 不只暴露字段，还显式补上原生构造路径
		new(Address) FGameplayAbilitySpec(InAbilityClass, InLevel, InInputID, InSourceObject);
	});

	FGameplayAbilitySpec_.Property("FGameplayAbilitySpecHandle Handle", &FGameplayAbilitySpec::Handle);
	FGameplayAbilitySpec_.Property("UGameplayAbility unresolved_object Ability", &FGameplayAbilitySpec::Ability);
	// ★ bit-field 无法直接映射成 property，于是转成 Get/Set 方法桥接
	FGameplayAbilitySpec_.Method("bool GetbInputPressed() const", [](const FGameplayAbilitySpec& Spec) { return Spec.InputPressed != 0; });
	FGameplayAbilitySpec_.Method("void SetbInputPressed(bool bValue)", [](FGameplayAbilitySpec& Spec, bool bInputPressed) { Spec.InputPressed = bInputPressed; });
	FGameplayAbilitySpec_.Method("bool GetbActivateOnce() const", [](const FGameplayAbilitySpec& Spec) { return Spec.bActivateOnce != 0; });
	FGameplayAbilitySpec_.Method("void SetbActivateOnce(bool bValue)", [](FGameplayAbilitySpec& Spec, bool bActivateOnce) { Spec.bActivateOnce = bActivateOnce; });
});
```

[2] 另一个新增簇是 `EnhancedInput + WorldContext glue`，说明当前插件在补“现代 UE 工程常用表面”而不是机械对齐上游文件名：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UEnhancedInputComponent.cpp
// 位置: 5-46
// 说明: EnhancedInput 被提升为独立绑定簇，而不是零散塞进共享 bind 文件
// ============================================================================
AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_UEnhancedInputComponent(FAngelscriptBinds::EOrder::Late, []
{
	auto UEnhancedInputComponent_ = FAngelscriptBinds::ExistingClass("UEnhancedInputComponent");

	UEnhancedInputComponent_.Method("bool HasBindings() const", METHOD_TRIVIAL(UEnhancedInputComponent, HasBindings));
	UEnhancedInputComponent_.Method("void ClearActionBindings() const", METHOD_TRIVIAL(UEnhancedInputComponent, ClearActionBindings));
	UEnhancedInputComponent_.Method("bool RemoveBinding(const FInputBindingHandle& BindingToRemove)", METHODPR_TRIVIAL(bool, UEnhancedInputComponent, RemoveBinding, (const FInputBindingHandle&)));

	UEnhancedInputComponent_.Method(
		"FEnhancedInputActionEventBinding& BindAction(const UInputAction Action, ETriggerEvent TriggerEvent, FEnhancedInputActionHandlerDynamicSignature Delegate)",
		[](UEnhancedInputComponent& InputComponent, const UInputAction* Action, ETriggerEvent TriggerEvent, FEnhancedInputActionHandlerDynamicSignature Delegate) -> FEnhancedInputActionEventBinding&
		{
			// ★ 动态 delegate 的 UObject/functionName 拆包在绑定层完成
			return InputComponent.BindAction(Action, TriggerEvent, Delegate.GetUObject(), Delegate.GetFunctionName());
		});
});
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_SystemTimers.cpp
// 位置: 6-48
// 说明: 当前插件用独立 glue bind 处理 WorldContext 定时器桥接
// ============================================================================
AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_SystemTimers((int32)FAngelscriptBinds::EOrder::Late, []
{
	FAngelscriptBinds::FNamespace System_("System");

	FAngelscriptBinds::BindGlobalFunction(
		"bool IsTimerPausedHandle(FTimerHandle Handle)",
		[](FTimerHandle Handle)
		{
			// ★ 通过当前引擎 world context 注入 KismetSystemLibrary
			return UKismetSystemLibrary::K2_IsTimerPausedHandle(FAngelscriptEngine::TryGetCurrentWorldContextObject(), Handle);
		});
	FAngelscriptBinds::SetPreviousBindRequiresWorldContext(true);

	FAngelscriptBinds::BindGlobalFunction(
		"void ClearAndInvalidateTimerHandle(FTimerHandle& Handle)",
		[](FTimerHandle& Handle)
		{
			UKismetSystemLibrary::K2_ClearAndInvalidateTimerHandle(FAngelscriptEngine::TryGetCurrentWorldContextObject(), Handle);
		});
	FAngelscriptBinds::SetPreviousBindRequiresWorldContext(true);
});
```

### 差距判断

| 子问题 | 结论 | 依据 |
| --- | --- | --- |
| Hazelight 是否“缺了 11 个当前插件同名 bind 文件” | **按当前快照文件名比较，不成立** | 目录扫描结果为 Hazelight-only `0`、current-only `11` |
| 当前插件是否仅仅“文件更多而已” | **不成立** | 新增文件集中覆盖 GAS、EnhancedInput、WorldContext glue |
| 两边绑定能力关系 | **实现方式不同 + 当前插件新增能力簇** | 当前插件把现代 UE 常用表面拆成独立 bind 文件 |

更准确的表述应是：`D2` 在当前快照下不是“缺失 11 个上游 bind 文件”，而是“**当前插件新增了 11 个面向现代 UE 子系统和插件内桥接的 bind 文件；真正的差异比较应深入共享 bind 文件内部，而不是停在文件名列表**”。

### [维度 D4] 当前插件把热重载从“重编译入口”推进成“显式依赖策略 + 显式降级策略 + 显式测试矩阵”

```
[D4-Deep] Hot Reload Decision Lattice
Hazelight snapshot
├─ changed files
├─ FilesToHotReload = changed files
├─ compile modules
└─ class generator decides soft/full reload

AngelPortV2
├─ changed files
├─ dependency expansion strategy
│  ├─ automatic import path          // 递归扫描 moduleDependencies
│  └─ legacy import path             // 反向 ImportedModules 图
├─ compile modules
├─ reload requirement lattice
│  ├─ SoftReload
│  ├─ FullReloadSuggested
│  └─ FullReloadRequired
└─ automation tests
   ├─ body-only change
   ├─ property count change
   └─ super-class change
```

前文已经指出两边都有 `PerformHotReload()`，但这一轮更深的结论是：Hazelight 当前快照把“依赖扩展”更多交给 compile step，而当前插件把依赖传播和降级策略都明写成了运行时决策树，并且用自动化测试直接锁住各条分支。

[1] Hazelight 当前快照的 `FilesToHotReload` 处理保持在“changed files only”：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\AngelscriptManager.cpp
// 函数: FAngelscriptManager::PerformHotReload
// 位置: 1190-1198
// 说明: 当前快照没有在这里显式展开依赖图
// ============================================================================
// Build a set of all files which are dependent on any of the modified files,
// such that we can hot reload all of them.
TSet<FFilenamePair> FilesToHotReload;
if (FileList.Num() > 0)
{
	// ★ 这里直接只保留实际变动文件
	FilesToHotReload.Append(FileList);
}
```

[2] 当前插件在同一入口里拆出两套依赖传播算法：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: FAngelscriptEngine::PerformHotReload
// 位置: 2282-2375
// 说明: 当前插件把 automatic import 与 legacy import 路径都显式化
// ============================================================================
TSet<FFilenamePair> FilesToHotReload;
if (FileList.Num() > 0)
{
	if (GAngelscriptRecompileAvoidance && ShouldUseAutomaticImportMethod())
	{
		// ★ 只在“自动 import + 避免重编译”这个特定组合下才退回 changed files only
		FilesToHotReload.Append(FileList);
	}
	else
	{
		TMap<FString, FAngelscriptModuleDesc*> RelativeFileToModule;
		TMap<asCModule*, FAngelscriptModuleDesc*> ScriptModuleToModule;

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
				else
				{
					FilesToHotReload.Add(File);
				}
			}

			// ★ 沿 moduleDependencies 递归扩散受影响模块
			bool bDidMarkModules = true;
			while (bDidMarkModules)
			{
				bDidMarkModules = false;
				for (auto& Module : ActiveModules)
				{
					auto* ScriptModule = (asCModule*)Module.Value->ScriptModule;
					if (ScriptModule == nullptr || MarkedModules.Contains(ScriptModule))
						continue;

					for (const auto& DependencyElem : ScriptModule->moduleDependencies)
					{
						if (MarkedModules.Contains(DependencyElem.Key))
						{
							MarkedModules.Add(ScriptModule);
							bDidMarkModules = true;
							break;
						}
					}
				}
			}

			for (asCModule* ReloadModule : MarkedModules)
			{
				if (auto* ModulePtr = ScriptModuleToModule.Find(ReloadModule))
				{
					for (const auto& Section : (*ModulePtr)->Code)
						FilesToHotReload.Add(FFilenamePair{ Section.AbsoluteFilename, Section.RelativeFilename });
				}
			}
		}
	}
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: FAngelscriptEngine::PerformHotReload
// 位置: 2399-2443, 3942-3997
// 说明: legacy import 路径与 soft/full reload 降级策略都被明确编码
// ============================================================================
// Build a reverse dependency map for module->dependent modules (non-recursive)
TMap<FAngelscriptModuleDesc*, TArray<FAngelscriptModuleDesc*>> ReverseDeps;
for (auto& Module : ActiveModules)
{
	auto ModulePtr = &(Module.Value.Get());
	for (const FString& ImportedModule : Module.Value->ImportedModules)
	{
		auto ImportedModuleDesc = GetModuleByModuleName(ImportedModule);
		if (ImportedModuleDesc.IsValid())
		{
			auto ImportedModulePtr = &(ImportedModuleDesc.ToSharedRef().Get());
			ReverseDeps.FindOrAdd(ImportedModulePtr).Add(ModulePtr);
		}
	}
}

while (ModuleJobs.Num() > 0)
{
	auto ModulePtr = ModuleJobs.Pop(false);
	for (const auto& Section : ModulePtr->Code)
	{
		FilesToHotReload.Add(FFilenamePair{ Section.AbsoluteFilename, Section.RelativeFilename });
	}
}

case FAngelscriptClassGenerator::EReloadRequirement::FullReloadSuggested:
	if (CompileType == ECompileType::SoftReloadOnly)
	{
		// ★ PIE 中允许先 soft reload，并把 full reload 延后到安全时机
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
		// ★ 结构性变更在不安全时机直接拒绝换入新模块
		bShouldSwapInModules = false;
		bFullReloadRequired = true;
	}
	else
	{
		SwapInModules(CompiledModules, DiscardedModules);
		ClassGenerator.PerformFullReload();
	}
	break;
```

[3] 更关键的是，当前插件把 reload 判定矩阵写成自动化测试，而不是靠人工回归：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadAnalysisTests.cpp
// 位置: 75-87, 123-135, 173-175, 223-225
// 说明: body-only / property-count / super-class 三种变化分别锁定不同 reload 判定
// ============================================================================
TestEqual(TEXT("Unchanged module should remain soft reload"), ReloadRequirement, FAngelscriptClassGenerator::SoftReload);
TestFalse(TEXT("Unchanged module should not suggest full reload"), bWantsFullReload);
TestFalse(TEXT("Unchanged module should not require full reload"), bNeedsFullReload);

TestTrue(TEXT("Property count change should request a full reload path"), bWantsFullReload || bNeedsFullReload);
TestTrue(TEXT("Property count change should not remain soft reload"), ReloadRequirement == FAngelscriptClassGenerator::FullReloadRequired || ReloadRequirement == FAngelscriptClassGenerator::FullReloadSuggested);

TestEqual(TEXT("Super-class change should require a full reload"), ReloadRequirement, FAngelscriptClassGenerator::FullReloadRequired);

TestEqual(TEXT("Body-only change should remain soft reload"), ReloadRequirement, FAngelscriptClassGenerator::SoftReload);
TestFalse(TEXT("Body-only change should not suggest full reload"), bWantsFullReload);
TestFalse(TEXT("Body-only change should not require full reload"), bNeedsFullReload);
```

### 差距判断

| 子问题 | 结论 | 依据 |
| --- | --- | --- |
| 热重载是否都已实现 | **是** | 两边都存在 `PerformHotReload()` 与 soft/full reload |
| 依赖传播是否同一层级实现 | **否，当前插件更显式** | 当前插件在运行时层写出了 `moduleDependencies` 与 `ImportedModules` 两套扩展逻辑 |
| 失败/降级策略是否同样可审计 | **当前插件更强** | `FullReloadSuggested` / `Required` 的行为与测试都更清晰 |

因此，`D4` 的准确判断不是“有没有热重载”，而是“**当前插件把 Hazelight 隐含在 compile step 和类生成器里的部分策略，前移成了运行时可读、可测试的决策格**”。

### [维度 D5] V2 协议本身没有分叉，但当前插件把消息 framing 和回归验证做硬了

```
[D5-Deep] Debug Message Transport
Hazelight
├─ inline prepend header               // 在 SendMessage* 内直接写长度和类型
├─ socket queue
└─ V2 message set + StopPIE

AngelPortV2
├─ SerializeDebugMessageEnvelope()
├─ queued sends
├─ TryDeserializeDebugMessageEnvelope()
│  ├─ partial-buffer tolerant          // 包不完整时先返回 true
│  └─ invalid-length guard             // 非法长度直接报错
├─ V2 message set - StopPIE
└─ smoke + stepping automation tests
```

这一轮的新增结论不是“当前插件也有 V2”，而是：当前插件没有改掉 Hazelight 的线协议语义，却把 `header prepend / body parse` 从模板函数里的内联细节抽成了显式 envelope helper，并加上长度校验、半包容忍和自动化测试。

[1] Hazelight 直接在 `SendMessage*` 模板里拼 header；当前插件把同一协议抽成 helper：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Debugging\AngelscriptDebugServer.h
// 位置: 0580-0616
// 说明: Hazelight 直接在模板函数里写 header
// ============================================================================
TArray<uint8> Buffer;
FMemoryWriter Writer(Buffer);
// prepend a header
int32 MessageLength = Body.Num();
uint8 MessageTypeByte = (uint8)MessageType;
Writer << MessageLength;
Writer << MessageTypeByte;
Buffer.Append(Body);
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 函数: SerializeDebugMessageEnvelope / TryDeserializeDebugMessageEnvelope
// 位置: 52-64, 72-108
// 说明: 当前插件把同样的 framing 规则抽成可复用 envelope helper
// ============================================================================
bool SerializeDebugMessageEnvelope(EDebugMessageType MessageType, const TArray<uint8>& Body, TArray<uint8>& OutBuffer)
{
	OutBuffer.Reset();
	FMemoryWriter Writer(OutBuffer);
	const int32 MessageLength = static_cast<int32>(sizeof(uint8)) + Body.Num();
	const uint8 MessageTypeByte = static_cast<uint8>(MessageType);
	Writer << const_cast<int32&>(MessageLength);
	Writer << const_cast<uint8&>(MessageTypeByte);
	OutBuffer.Append(Body);
	return true;
}

if (MessageLength <= 0 || MessageLength > 1024 * 1024)
{
	// ★ 非法包长直接报错，避免坏包把协议状态拖垮
	*OutError = FString::Printf(TEXT("Received debugger envelope with invalid message length %d."), MessageLength);
	return false;
}

if (InOutBuffer.Num() < TotalEnvelopeSize)
{
	// ★ 半包时不当作协议错误，继续等待后续数据
	return true;
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h
// 位置: 86-93, 648-687
// 说明: helper 被所有发送路径统一复用
// ============================================================================
struct FAngelscriptDebugMessageEnvelope
{
	EDebugMessageType MessageType = EDebugMessageType::Disconnect;
	TArray<uint8> Body;
};

ANGELSCRIPTRUNTIME_API bool SerializeDebugMessageEnvelope(EDebugMessageType MessageType, const TArray<uint8>& Body, TArray<uint8>& OutBuffer);
ANGELSCRIPTRUNTIME_API bool TryDeserializeDebugMessageEnvelope(TArray<uint8>& InOutBuffer, FAngelscriptDebugMessageEnvelope& OutEnvelope, bool& bOutHasEnvelope, FString* OutError = nullptr);

template<typename T>
void SendMessageToClient(FSocket* Client, EDebugMessageType MessageType, T& Message)
{
	TArray<uint8> Buffer;
	if (!SerializeDebugMessageEnvelope(MessageType, Body, Buffer))
	{
		return;
	}

	FQueuedMessage& Msg = QueuedSends.FindOrAdd(Client).Emplace_GetRef();
	Msg.Buffer = MoveTemp(Buffer);
	TrySendingMessages(Client);
}
```

[2] 协议面上保持 V2，但枚举面确实比 Hazelight 少了 `StopPIE`：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Debugging\AngelscriptDebugServer.h
// 位置: 71-80
// 说明: Hazelight 的 V2 消息面包含 StopPIE
// ============================================================================
DebugServerVersion,
CreateBlueprint,

ReplaceAssetDefinition,

SetDataBreakpoints,
ClearDataBreakpoints,

StopPIE,
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h
// 位置: 73-80
// 说明: 当前插件保持 V2，但消息面在这里收窄
// ============================================================================
DebugServerVersion,
CreateBlueprint,

ReplaceAssetDefinition,

SetDataBreakpoints,
ClearDataBreakpoints,
```

[3] 当前插件还把“协议兼容”从口头说法变成了 smoke + step 级别的自动化断言：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSmokeTests.cpp
// 位置: 49-88, 125-132
// 说明: handshake 不是手工联调，而是自动化验证
// ============================================================================
if (!TestTrue(TEXT("Debugger.Smoke.Handshake should send StartDebugging"), Client.SendStartDebugging(2)))
{
	return false;
}

if (!TestTrue(TEXT("Debugger.Smoke.Handshake should receive the DebugServerVersion response"), bReceivedDebugVersion))
{
	return false;
}

TestEqual(TEXT("Debugger.Smoke.Handshake should report the current debug server version"), DebugServerVersion->DebugServerVersion, DEBUG_SERVER_VERSION);

if (!TestTrue(TEXT("Debugger.Smoke.Handshake should send StopDebugging"), Client.SendStopDebugging()))
{
	return false;
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp
// 位置: 443-449, 541-549, 645-651
// 说明: 单步测试直接校验行号与栈深，而不只是“收到了一个 stopped”
// ============================================================================
TestEqual(TEXT("Stepping.StepIn should land inside Inner()"),
	MonitorResult.Stops[1].Callstack->Frames[0].LineNumber,
	Fixture.GetLine(TEXT("StepInnerEntryLine")));

TestEqual(TEXT("Stepping.StepOver should land at the line after the call"),
	MonitorResult.Stops[1].Callstack->Frames[0].LineNumber,
	Fixture.GetLine(TEXT("StepAfterCallLine")));
TestEqual(TEXT("Stepping.StepOver should stay in the same frame depth"),
	MonitorResult.Stops[1].Callstack->Frames.Num(),
	MonitorResult.Stops[0].Callstack->Frames.Num());

TestEqual(TEXT("Stepping.StepOut should return to the line after the call"),
	MonitorResult.Stops[2].Callstack->Frames[0].LineNumber,
	Fixture.GetLine(TEXT("StepAfterCallLine")));
TestTrue(TEXT("Stepping.StepOut should reduce stack depth after returning"),
	MonitorResult.Stops[2].Callstack->Frames.Num() < MonitorResult.Stops[1].Callstack->Frames.Num());
```

### 差距判断

| 子问题 | 结论 | 依据 |
| --- | --- | --- |
| 协议版本是否分叉 | **没有** | 两边都维持 `DEBUG_SERVER_VERSION 2` 与相同握手主线 |
| 消息面是否完全一致 | **不完全一致** | 当前插件缺少 `StopPIE` |
| transport/解析健壮性是否一致 | **当前插件更强** | envelope helper、半包容忍、非法长度保护 |
| 调试回归质量是否一致 | **当前插件更强** | smoke + step in/over/out 自动化测试覆盖 |

因此，`D5` 的准确表述应是：`DebugServer V2` 语义主线仍然一致；差异主要体现在“**消息面小幅收窄**”和“**当前插件把 transport 与回归验证工程化了**”，而不是出现协议代际分裂。

---

## 深化分析 (2026-04-08 18:29:36)

### [维度 D3] `ScriptMethod` 的语义入口发生了收敛，当前插件把更多 mixin 工作前移到 fallback 调用期

```
[D3-Deep] Mixin Signature and Call Path
Hazelight
├─ UFUNCTION meta: ScriptMethod / ScriptMixin   // 函数级与类级双入口
├─ Helper_FunctionSignature rewrites first arg   // 绑定期改成员签名
├─ Bind_BlueprintEvent / Bind_BlueprintCallable
└─ generated UFunction keeps BlueprintEvent chain

AngelPortV2
├─ UFUNCTION meta: ScriptMixin                   // 运行时 helper 只读类级入口
├─ Helper_FunctionSignature rewrites first arg   // 绑定期只处理 ScriptMixin
├─ BlueprintCallableReflectiveFallback
│  └─ inject mixin UObject at call time         // 调用期补回 self
└─ generated UFunction writes MixinArgument      // 生成期保留 Blueprint/DefaultToSelf 元数据
```

这一轮继续往下钻后，可以更精确地区分 `D3` 的三条子链路：

1. `BlueprintOverride / BlueprintEvent` 主链在两边基本仍然同构，前文结论不变。  
2. 真正发生变化的是 **function-level mixin 入口**。Hazelight 在签名构造器里同时承认 `ScriptMethod` 与 `ScriptMixin`；当前插件的 `Helper_FunctionSignature` 已经只保留 `ScriptMixin`。  
3. 当前插件没有把 mixin 整体拿掉，而是把一部分“把宿主对象塞回首参”的责任移到了 `BlueprintCallableReflectiveFallback`，这样自动生成的 BlueprintCallable 表也能吃到 mixin self 注入。

[1] Hazelight 在签名层明确同时支持 `ScriptMethod` 与 `ScriptMixin`：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Binds\Helper_FunctionSignature.h
// 位置: 22-23, 84-86, 279-315
// 说明: UEAS2 的 mixin 入口是双通道，既看类级 ScriptMixin，也看函数级 ScriptMethod
// ============================================================================
static const FName NAME_Signature_ScriptMethod("ScriptMethod");
static const FName NAME_Signature_ScriptMixin("ScriptMixin");

const FString* ScriptName = InFunction->FindMetaData(NAME_Signature_ScriptName);
const FString* ScriptMethod = InFunction->FindMetaData(NAME_Signature_ScriptMethod);
const FString* ScriptAlias = ScriptName != nullptr ? ScriptName : ScriptMethod;

// ★ 如果函数自己带 ScriptMethod，也会把第一个参数折叠成脚本侧的 this
if (Function->HasMetaData(NAME_Signature_ScriptMethod))
{
	FoundMixin = FirstParamType;
}
else if (MixinClasses.Len() != 0)
{
	...
}

ArgumentTypes.RemoveAt(0);
ArgumentNames.RemoveAt(0);
ArgumentDefaults.RemoveAt(0);
ClassName = FoundMixin;
```

[2] 当前插件的运行时 helper 只保留了 `ScriptMixin`，而没有再读取 `ScriptMethod`：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h
// 位置: 22, 85-93, 277-304
// 说明: 当前快照下，签名改写阶段只看类级 ScriptMixin
// ============================================================================
static const FName NAME_Signature_ScriptMixin("ScriptMixin");

static FString GetScriptNameForFunction(UFunction* InFunction)
{
	FString OutScriptName = InFunction->GetName();

	if (InFunction->HasMetaData(NAME_Signature_ScriptName))
	{
		OutScriptName = GetPrimaryScriptName(InFunction->GetMetaData(NAME_Signature_ScriptName));
	}
	// ★ 这里不再读取 ScriptMethod 元数据
}

const FString& MixinClasses = Function->GetOuterUClass()->GetMetaData(NAME_Signature_ScriptMixin);
if (MixinClasses.Len() != 0 && ArgumentTypes.Num() > 0
	&& (ArgumentTypes[0].IsObjectPointer() || ArgumentTypes[0].bIsReference))
{
	...
	ClassName = Mixin;
	bStaticInScript = false;
}
```

[3] 但当前仓库的测试仍然把 function-level `ScriptMethod` 当成既定契约，这说明这里至少存在“语义收窄风险”，而不是 Blueprint mixin 整体不存在：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptUhtCoverageTestTypes.h
// 位置: 31-32
// 说明: 测试类型仍声明 ScriptMethod 元数据
// ============================================================================
UFUNCTION(BlueprintCallable, meta = (ScriptMethod, DisplayName = "Get Coverage Value"))
static int32 GetCoverageValue(const UObject* Target);
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp
// 位置: 639-644
// 说明: 自动化测试仍要求 ScriptMethod 把首参折叠成脚本成员调用
// ============================================================================
FAngelscriptFunctionSignature Signature(HostType.ToSharedRef(), ScriptMethodFunction);
TestTrue(TEXT("FunctionLevelScriptMethodUsesFirstParameterAsMixin should keep the Unreal function static"), Signature.bStaticInUnreal);
TestFalse(TEXT("FunctionLevelScriptMethodUsesFirstParameterAsMixin should bind ScriptMethod functions as script members"), Signature.bStaticInScript);
TestEqual(TEXT("FunctionLevelScriptMethodUsesFirstParameterAsMixin should remove the first parameter from the exposed signature"), Signature.ArgumentTypes.Num(), 0);
TestTrue(TEXT("FunctionLevelScriptMethodUsesFirstParameterAsMixin should expose a const member declaration when the first parameter is const"), Signature.Declaration.Contains(TEXT("const")));
```

[4] 当前插件把另一部分 mixin 责任前移到了 reflective fallback，并在生成 `UFunction` 时保留 `MixinArgument / DefaultToSelf` 元数据：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp
// 位置: 143-151, 318-323
// 说明: 静态 BlueprintCallable 在 reflective fallback 路径里可把宿主对象注回首参
// ============================================================================
ReflectiveSignature->bInjectMixinObject = true;
const int32 FunctionId = FAngelscriptBinds::BindMethodDirect(
	Signature.ClassName,
	Signature.Declaration,
	asFUNCTION(CallBlueprintCallableReflectiveFallback),
	asCALL_GENERIC,
	ASAutoCaller::FunctionCaller::Make(),
	ReflectiveSignature);

if (bInjectMixinObject && !bInjectedMixinObject)
{
	UObject* MixinObject = static_cast<UObject*>(Generic->GetObject());
	Property->CopySingleValue(Destination, &MixinObject);
	bInjectedMixinObject = true;
	continue;
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 位置: 3435-3441, 3455-3458
// 说明: script mixin 生成出的 UFunction 仍然带 Blueprint 侧需要的 self 元数据
// ============================================================================
if (ScriptFunction->traits.GetTrait(asTRAIT_MIXIN)
	&& ScriptFunction->parameterNames.GetLength() >= 1)
{
	FString MixinArgumentName = ANSI_TO_TCHAR(ScriptFunction->parameterNames[0].AddressOf());
	NewFunction->SetMetaData(NAME_Function_MixinArgument, *MixinArgumentName);
	NewFunction->SetMetaData(NAME_Function_DefaultToSelf, *MixinArgumentName);
}

if ((FunctionDesc->bBlueprintEvent && FunctionDesc->bCanOverrideEvent) || FunctionDesc->bBlueprintOverride)
	NewFunction->FunctionFlags |= FUNC_BlueprintEvent;
```

### 差距判断

| 子问题 | 结论 | 依据 |
| --- | --- | --- |
| class-level `ScriptMixin` 是否仍然存在 | **存在** | `Helper_FunctionSignature.h:277-304` 仍然按 `ScriptMixin` 折叠首参 |
| function-level `ScriptMethod` 是否与 Hazelight 同口径 | **当前快照存在语义收窄风险** | UEAS2 明确读取 `ScriptMethod`，当前运行时 helper 未见读取点；测试仍保留该契约 |
| 自动生成 BlueprintCallable 的 mixin self 注入 | **当前插件实现方式不同且更工程化** | `BlueprintCallableReflectiveFallback.cpp:143-151,318-323` 把 self 注入挪到调用期 |

因此，`D3` 本轮最重要的新结论不是“当前插件不会 mixin”，而是：**mixin 机制已经从 Hazelight 的“签名层双入口”演进成“签名层单入口 + fallback 注入 + 生成期元数据补偿”**。其中 `ScriptMethod` 这条 function-level 入口在当前快照下表现出明显的兼容性收窄迹象；这更接近“**实现边界变化/潜在回归**”，而不是“Blueprint 交互整体缺失”。

### [维度 D8/D11] 当前插件绕开引擎补丁的关键，不只是 UHT exporter，而是把 `FUNC_RuntimeGenerated` 改写成 `FUNC_Native + NativeThunk`

```
[D8/D11-Deep] Engine Patch Surface vs Plugin-Owned Hooks
Hazelight UEAS2
├─ UHT emits AngelscriptPropertyFlags
├─ UFunction base class gets RuntimeCall* virtuals
├─ UClass base class gets ScriptTypePtr / bIsScriptClass
├─ Actor / BlueprintGeneratedClass / Kismet / ScriptCore branch on those fields
└─ deployment requires patched engine branch

AngelPortV2
├─ plugin UASClass stores ScriptTypePtr / bIsScriptClass
├─ plugin UASFunction stores RuntimeCall* implementation
├─ class generator sets FUNC_Native + UASFunctionNativeThunk
├─ AngelscriptUHTTool ships as plugin-local UnrealHeaderTool exporter
└─ deployment stays inside plugin, but engine-wide implicit awareness shrinks
```

前文已经说明当前插件“不依赖引擎补丁”，但这一轮更细的结论是：它不是简单放弃了 `runtime generated function/class` 这套语义，而是做了两层替代：

1. **类型侧替代**：把 `ScriptTypePtr / bIsScriptClass / RuntimeCall*` 从引擎基类搬回插件自有的 `UASClass / UASFunction`。  
2. **调用侧替代**：不再走 Hazelight 的 `FUNC_RuntimeGenerated` 分支，而是把脚本函数伪装成 `FUNC_Native`，再用 `UASFunctionNativeThunk` 接回 Angelscript runtime。  

这解释了为什么当前插件可以不改 `CoreUObject` 也跑起来，同时也解释了它为什么不可能天然拥有 Hazelight 那种“整个引擎凡是看 `UClass/UFunction` 的地方都自动认识 script class/function”的渗透深度。

[1] Hazelight 把 runtime hook 直接写进了引擎基类；因此其 `UASClass/UASFunction` 不需要再重复声明这些字段：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Runtime\CoreUObject\Public\UObject\Class.h
// 位置: 2606-2609, 3909-3912
// 说明: UEAS2 把 script function / script class 元语义直接抬进 UFunction / UClass 基类
// ============================================================================
virtual void RuntimeCallFunction(UObject* Object, FFrame& Stack, RESULT_DECL) { ensureAlways(false); }
virtual void RuntimeCallEvent(UObject* Object, void* Params) { ensureAlways(false); }
virtual UFunction* GetRuntimeValidateFunction() { ensureAlways(false); return nullptr; }

TMap<FName, ASAutoCaller::FReflectedFunctionPointers> ASReflectedFunctionPointers;
void* ScriptTypePtr = nullptr;
bool bIsScriptClass = false;
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Public\ClassGenerator\ASClass.h
// 位置: 11-24, 202-204
// 说明: 上游插件子类只做 override，不再自己存放 ScriptTypePtr / bIsScriptClass
// ============================================================================
class ANGELSCRIPTCODE_API UASClass : public UClass
{
	...
	UClass* ComposeOntoClass = nullptr;
	// ★ 这里没有 ScriptTypePtr / bIsScriptClass，本体已经在引擎基类里
};

virtual void RuntimeCallFunction(UObject* Object, FFrame& Stack, RESULT_DECL) override;
virtual void RuntimeCallEvent(UObject* Object, void* Parms) override;
virtual UFunction* GetRuntimeValidateFunction() override;
```

[2] 这也意味着 Hazelight 的引擎补丁会自然渗透到 Blueprint / Actor 主干，而不是只停留在 UHT：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Runtime\Engine\Private\BlueprintGeneratedClass.cpp
// 位置: 1222-1229, 1416-1419
// 说明: BlueprintGeneratedClass 直接把 bIsScriptClass 视作与 Native/Blueprint 并列的一类
// ============================================================================
UClass* SuperClass = GetSuperClass();
while (SuperClass && !(SuperClass->HasAnyClassFlags(CLASS_Native | CLASS_Intrinsic) || SuperClass->bIsScriptClass))
{
	SuperClass = SuperClass->GetSuperClass();
}

if (Function == nullptr)
	return false;
UClass* FunctionClass = Function->GetOuterUClass();
return FunctionClass && (FunctionClass->bIsScriptClass || Function->GetOuter()->IsA(UBlueprintGeneratedClass::StaticClass()));
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Runtime\Engine\Private\Actor.cpp
// 位置: 578-579, 1658-1659
// 说明: AActor 逻辑直接识别 script class / runtime generated function
// ============================================================================
if (!GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint) && !GetClass()->bIsScriptClass)
	return;

if ((Function->FunctionFlags & (FUNC_Native | FUNC_RuntimeGenerated)) == 0 && (Function->Script.Num() == 0))
```

[3] 当前插件把同名字段和虚函数收回到了插件自有子类：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h
// 位置: 14-30, 204-207
// 说明: 这些本来在 UEAS2 引擎基类上的成员，被当前插件搬回了 UASClass/UASFunction
// ============================================================================
class ANGELSCRIPTRUNTIME_API UASClass : public UClass
{
	...
	UClass* ComposeOntoClass = nullptr;
	void* ScriptTypePtr = nullptr;
	bool bIsScriptClass = false;
	...
};

virtual void RuntimeCallFunction(UObject* Object, FFrame& Stack, RESULT_DECL);
virtual void RuntimeCallEvent(UObject* Object, void* Parms);
virtual UFunction* GetRuntimeValidateFunction();
```

[4] 更关键的是，当前插件没有再设置 `FUNC_RuntimeGenerated`，而是改走 `FUNC_Native + UASFunctionNativeThunk`：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\ClassGenerator\AngelscriptClassGenerator.cpp
// 位置: 3259-3264
// 说明: 上游明确依赖 FUNC_RuntimeGenerated，让引擎主干识别 script function
// ============================================================================
auto* NewFunction = UASFunction::AllocateFunctionFor(NewClass, FunctionName, FunctionDesc);
NewFunction->SetSuperStruct(ParentFunction);
NewFunction->ReturnValueOffset = MAX_uint16;
NewFunction->FirstPropertyToInit = NULL;
NewFunction->FunctionFlags |= FUNC_RuntimeGenerated;
NewFunction->ScriptFunction = FunctionDesc->ScriptFunction;
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 位置: 3415-3429
// 说明: 当前插件把 runtime-generated dispatch 改写成原生 thunk 分派
// ============================================================================
//NewFunction->FunctionFlags |= FUNC_RuntimeGenerated;
NewFunction->ScriptFunction = FunctionDesc->ScriptFunction;
NewFunction->GeneratedSourceLineNumber = FunctionDesc->LineNumber + 1;
...
NewFunction->FunctionFlags |= FUNC_Native;
NewFunction->SetNativeFunc(&UASFunctionNativeThunk);
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp
// 位置: 1931-1958
// 说明: NativeThunk 再把标准 UFunction 调用桥接回 Angelscript runtime
// ============================================================================
void UASFunction::RuntimeCallFunction(UObject* Object, FFrame& Stack, RESULT_DECL)
{
	AngelscriptCallFromBPVM<true, false>(this, Object, Stack, RESULT_PARAM);
}

void UASFunctionNativeThunk(UObject* Object, FFrame& Stack, RESULT_DECL)
{
	UASFunction* Function = Cast<UASFunction>(Stack.Node);
	check(Function != nullptr);
	Function->RuntimeCallFunction(Object, Stack, RESULT_PARAM);
}

UFunction* UASFunction::GetRuntimeValidateFunction()
{
	return ValidateFunction;
}
```

[5] 部署侧的替代则体现在插件自带的 UHT exporter：它编译成插件目录下的 `DotNET` 组件，而不是改写引擎内置 UHT 程序：

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs
// 位置: 12-27
// 说明: 当前插件通过标准 UnrealHeaderTool exporter 扩展点接入，而不是修改 UHT 源码
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
```

```xml
<!-- =========================================================================
文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptUHTTool.ubtplugin.csproj
位置: 5-14, 47-51
说明: exporter 产物落在插件自己的 Binaries\DotNET\UnrealBuildTool\Plugins 目录
=========================================================================== -->
<TargetFramework>net8.0</TargetFramework>
<RootNamespace>AngelscriptUHTTool</RootNamespace>
<AssemblyName>AngelscriptUHTTool</AssemblyName>
<OutputPath>..\..\Binaries\DotNET\UnrealBuildTool\Plugins\AngelscriptUHTTool\</OutputPath>

<Reference Include="EpicGames.UHT">
  <HintPath>$(EngineDir)\Binaries\DotNET\UnrealBuildTool\EpicGames.UHT.dll</HintPath>
</Reference>
<Reference Include="UnrealBuildTool">
  <HintPath>$(EngineDir)\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.dll</HintPath>
</Reference>
```

### 差距判断

| 子问题 | 结论 | 依据 |
| --- | --- | --- |
| runtime function 调度是否完全缺失 | **没有缺失** | 当前插件用 `FUNC_Native + UASFunctionNativeThunk` 代替 `FUNC_RuntimeGenerated` |
| engine-wide script awareness 是否仍与 Hazelight 等价 | **不等价，集成深度更浅** | UEAS2 的 `Actor.cpp / BlueprintGeneratedClass.cpp / Class.h` 都直接读取 `bIsScriptClass` / `FUNC_RuntimeGenerated` |
| 无引擎补丁的部署边界是否更好 | **是** | `AngelscriptUHTTool` 作为插件本地 `DotNET` exporter 输出到插件目录 |

因此，`D8/D11` 本轮最重要的新结论是：**当前插件绕开引擎补丁的核心手法，不只是“把 UHT 挪到插件里”，而是“把 runtime-generated 语义伪装成标准 native thunk，再把 class/function 元数据收回插件子类”**。这带来了明显更好的插件分发边界，但代价也很明确: Hazelight 那种通过 `CoreUObject / Engine / Blueprint / Kismet` 主干自动认识 script class/function 的深度集成，不再是 stock engine 下天然可得的能力。

---

## 深化分析 (2026-04-08 18:44:00)

### [维度 D1] `UE module` 数量并没有明显膨胀，真正变化的是 build boundary 和验证边界

```
[D1-Deep] Build Boundary
Hazelight
├─ AngelscriptCode                  // 主二进制，运行时/绑定/测试代码同仓
│  ├─ Engine deps
│  └─ Plugin ThirdParty/include
├─ AngelscriptEditor                // 编辑器层
└─ AngelscriptLoader                // 只依赖 Code(+Editor)，本身几乎不承载逻辑

AngelPortV2
├─ AngelscriptRuntime               // 自带 ThirdParty/angelscript 与导出宏
│  ├─ Core / Binds / ClassGenerator
│  └─ GameplayAbilities / EnhancedInput 等能力面
├─ AngelscriptEditor                // 编辑器层
└─ AngelscriptTest                  // 独立测试二进制，直接覆盖 Debugger/Core/ClassGenerator
```

这一轮往 `Build.cs` 深挖后，一个容易被说反的点可以更明确地钉死：**当前插件的“多模块化”主要发生在编译边界和验证边界，不是把 Runtime 继续拆成更多 Unreal module**。  

从 `uplugin` 视角看，当前插件仍然是 `Runtime + Editor + Test` 三个 UE 模块；所谓 `Core / Binds / ClassGenerator / Debugging / ThirdParty` 这些更多是 `AngelscriptRuntime` 内部的源码分层。与 Hazelight 的真正差异有两条：

1. 上游的 `Loader` 在 build graph 中就是一个极薄的依赖转发层。  
2. 当前插件把 ThirdParty、feature surface 和测试层都显式拉成独立边界，因此可以在不增加 UE 模块数量的前提下提升可验证性和可分发性。  

[1] Hazelight 的 `Loader` 在 build graph 里确实只是一个依赖壳；它既不拥有 ThirdParty，也不拥有独立 feature 依赖：

```csharp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptLoader\AngelscriptLoader.Build.cs
// 位置: 6-25
// 说明: Loader 模块只依赖 Code / Editor，本身不承载功能面
// ============================================================================
public class AngelscriptLoader : ModuleRules
{
	public AngelscriptLoader(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"Engine",
			"AngelscriptCode",
		});

		if (Target.bBuildEditor)
		{
			PublicDependencyModuleNames.AddRange(new string[] 
			{
				"AngelscriptEditor", 
			});
		}
	}
}
```

[2] Hazelight 的核心二进制仍然把运行时、绑定面和测试代码一起装进 `AngelscriptCode`，ThirdParty 也是通过插件相对路径直接暴露：

```csharp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\AngelscriptCode.Build.cs
// 位置: 12-24, 27-44, 60-64
// 说明: 上游把大部分能力面压在单一 Code 模块里，ThirdParty 通过插件路径暴露
// ============================================================================
/* Link to libraries used in core angelscript code */
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
});

/* Link to libraries used in bindings */
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
	"TraceLog",
	"AssetRegistry",
	"Projects",
	"PhysicsCore",
	"CoreOnline",
});

var PluginPath = "../Plugins/Angelscript";
PublicIncludePaths.Add(PluginPath + "/ThirdParty/include");
PublicIncludePaths.Add(PluginPath + "/ThirdParty/source");
```

[3] 当前插件把 ThirdParty 收进 `AngelscriptRuntime` 自己的模块目录，并在同一二进制内显式声明扩展能力面；同时增加导出宏和 debug 配置下的 `OptimizeCode = Never`：

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs
// 位置: 10-26, 29-65
// 说明: 当前插件把脚本运行时、ThirdParty 与现代 UE 能力面收束到 Runtime 模块
// ============================================================================
PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
NumIncludedBytesPerUnityCPPOverride = 131072;
PrivateDefinitions.Add("ANGELSCRIPT_EXPORT=1");
PublicDefinitions.Add("ANGELSCRIPT_DLL_LIBRARY_IMPORT=1");

PublicIncludePaths.Add(ModuleDirectory);
PrivateIncludePaths.Add(ModuleDirectory);
PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Core"));
PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Core"));

var AngelscriptThirdPartyPath = Path.Combine(ModuleDirectory, "ThirdParty", "angelscript");
PublicIncludePaths.Add(Path.Combine(AngelscriptThirdPartyPath, "source"));
PublicIncludePaths.Add(AngelscriptThirdPartyPath);

if (Target.Configuration == UnrealTargetConfiguration.Debug || Target.Configuration == UnrealTargetConfiguration.DebugGame)
{
	OptimizeCode = CodeOptimization.Never;
}

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
	"TraceLog",
	"AssetRegistry",
	"Projects",
	"PhysicsCore",
	"CoreOnline",
	"EnhancedInput",
	"GameplayAbilities",
	"GameplayTasks",
});
```

[4] 当前插件把测试真正拉成了独立 module，这和 UEAS2 把 `Testing/Tests` 目录埋在 `AngelscriptCode` 里是不同的 build boundary：

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs
// 位置: 12-22, 23-49
// 说明: Test 模块可以直接覆盖 Runtime 的 Core / Debugger / ClassGenerator 路径
// ============================================================================
// Module root + subdirectories mirroring AngelscriptRuntime layout
PublicIncludePaths.Add(ModuleDirectory);
PrivateIncludePaths.Add(ModuleDirectory);
PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Core"));
PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Debugger"));
PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Dump"));
PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Internals"));
PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Native"));
PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Preprocessor"));
PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "ClassGenerator"));

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

### 差距判断

| 子问题 | 结论 | 依据 |
| --- | --- | --- |
| 当前插件是否在 UE module 层面显著更碎 | **不是** | 主要新增的是 `AngelscriptTest`；`Core/Binds/ClassGenerator` 仍是 `AngelscriptRuntime` 内部分层 |
| `Loader` 的删除是否意味着模块能力缩水 | **不是** | `AngelscriptLoader.Build.cs` 本身就是依赖壳，能力 owner 仍在 `Code/Runtime` |
| 当前插件的验证边界是否更强 | **是** | `AngelscriptTest.Build.cs` 让 Debugger / Core / ClassGenerator 有独立测试二进制 |

因此，`D1` 本轮最重要的新结论是：**Hazelight 和当前插件在“Unreal module 数量”上并没有形成想象中的代差；真正的结构差异在于 build boundary。Hazelight 用 `Loader` 维持一个很薄的启动壳，而当前插件用 `Runtime + Test` 把运行时自包含和回归验证正式模块化。**

### [维度 D4] 当前插件把“新类进入 PIE 后的 hot reload”从硬阻塞改成了可降级换入

```
[D4-Deep] New-Class Hot Reload Policy
Hazelight
├─ ReplacedClass found
├─ mark FullReloadRequired
└─ SoftReloadOnly
   ├─ do NOT swap in modules
   └─ keep old script code active

AngelPortV2
├─ ReplacedClass found
├─ mark FullReloadSuggested
└─ SoftReloadOnly
   ├─ SwapInModules()
   ├─ PerformSoftReload()
   └─ queue full reload for later
```

前两轮已经说明两边都有 `SoftReload / FullReloadSuggested / FullReloadRequired`。本轮更细的发现是：**当前插件专门改了“新类替换旧占位类”这条判定，让它在 PIE 的 `SoftReloadOnly` 场景下从“必须阻塞”变成“允许先换入、稍后再补 full reload”**。这不是一般性的 hot reload 存在与否差异，而是对“编辑中断成本”做出的明确取舍。

[1] Hazelight 在发现 `ReplacedClass` 时直接把 reload 要求抬到 `FullReloadRequired`：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\ClassGenerator\AngelscriptClassGenerator.cpp
// 位置: 1048-1053
// 说明: 上游把“替换已存在脚本类”视为必须 full reload 的条件
// ============================================================================
// It's possible we're replacing a class that was previously removed
UASClass* ReplacedClass = FindObject<UASClass>(FAngelscriptManager::GetPackage(), *UnrealName);
if (ReplacedClass != nullptr)
{
	if (ClassData.ReloadReq < EReloadRequirement::FullReloadRequired)
		ClassData.ReloadReq = EReloadRequirement::FullReloadRequired;
}
```

[2] 当前插件在同一位置显式写了降级注释，把它改成 `FullReloadSuggested`：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 位置: 1063-1072
// 说明: 当前插件允许新类在 SoftReloadOnly 下先换入模块，再排队 full reload
// ============================================================================
else if (FAngelscriptEngine::Get().bIsInitialCompileFinished)
{
	UASClass* ReplacedClass = FindObject<UASClass>(FAngelscriptEngine::GetPackage(), *UnrealName);
	if (ReplacedClass != nullptr)
	{
		//[UE++]: Downgrade to FullReloadSuggested so SoftReloadOnly can still swap in the module;
		// ShouldFullReload() will route brand-new classes through CreateFullReloadClass during soft reload
		if (ClassData.ReloadReq < EReloadRequirement::FullReloadSuggested)
			ClassData.ReloadReq = EReloadRequirement::FullReloadSuggested;
		//[UE--]
	}
}
```

[3] 这个改动会直接改变 `SoftReloadOnly` 分支的运行结果。Hazelight 在 `FullReloadRequired` 下明确“不换入模块，保持旧代码继续跑”：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\AngelscriptManager.cpp
// 位置: 2655-2674
// 说明: 上游在 SoftReloadOnly + FullReloadRequired 时拒绝 swap-in
// ============================================================================
case FAngelscriptClassGenerator::EReloadRequirement::FullReloadRequired:
	if (CompileType == ECompileType::SoftReloadOnly)
	{
		FString Msg =
			TEXT("Full Reload is required due to UPROPERTY() or UFUNCTION() changes, but cannot")
				TEXT(" perform a full reload right now. Keeping old angelscript code active.");
		UE_LOG(Angelscript, Error, TEXT("%s"), *Msg);
		...
		bShouldSwapInModules = false;
		bFullReloadRequired = true;
	}
```

[4] 当前插件则让这类变化尽量落入 `FullReloadSuggested` 分支，在 PIE 中先 `SwapInModules()` 再 `PerformSoftReload()`：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 3942-3964
// 说明: 当前插件允许 suggested 级别的变化在 PIE 中先换入模块，随后再补 full reload
// ============================================================================
case FAngelscriptClassGenerator::EReloadRequirement::FullReloadSuggested:
	if (CompileType == ECompileType::SoftReloadOnly)
	{
#if WITH_EDITOR
		FString Msg =
			TEXT("Performing a Soft Reload during PIE. New UPROPERTY()s and UFUNCTION()s won't show up")
				TEXT(" until full reload. A Full Reload will be queued for after PIE ends.");
		UE_LOG(Angelscript, Warning, TEXT("%s"), *Msg);
		...
#endif
		bWasFullyHandled = false;
		SwapInModules(CompiledModules, DiscardedModules);
		ClassGenerator.PerformSoftReload();
	}
```

[5] 当前插件还补了一层上游没看到的“丢弃模块即清理 reload 队列/诊断状态”逻辑，避免陈旧文件在后续循环里继续触发 full reload：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 1089-1096
// 说明: 丢弃旧模块时同步清理 hot reload 队列与诊断缓存
// ============================================================================
for (const FAngelscriptModuleDesc::FCodeSection& Section : ModuleToDiscard->Code)
{
	const FFilenamePair FilenamePair{ Section.AbsoluteFilename, Section.RelativeFilename };
	FileHotReloadState.Remove(Section.RelativeFilename);
	PreviouslyFailedReloadFiles.Remove(FilenamePair);
	QueuedFullReloadFiles.Remove(FilenamePair);
	Diagnostics.Remove(Section.AbsoluteFilename);
	LastEmittedDiagnostics.Remove(Section.AbsoluteFilename);
}
```

### 差距判断

| 子问题 | 结论 | 依据 |
| --- | --- | --- |
| 两边是否都有 hot reload 降级分级 | **有** | 都存在 `SoftReload / FullReloadSuggested / FullReloadRequired` |
| “新增/替换脚本类”在 PIE 下的处理是否一致 | **不一致** | Hazelight 走 `FullReloadRequired`，当前插件主动降到 `FullReloadSuggested` |
| 当前插件是否只是在更激进地换入模块，而无保护 | **不是** | 它明确记录 warning，并把 full reload 排队到后续时机 |
| 当前插件是否增加了 stale reload state 清理 | **是** | `PreviouslyFailedReloadFiles.Remove` / `QueuedFullReloadFiles.Remove` / 诊断缓存移除是新增清理面 |

因此，`D4` 本轮最重要的新结论是：**当前插件并不是“热重载更强”这么简单，而是明确选择了更偏编辑体验的降级策略：允许新类在 `SoftReloadOnly` 期间先换入模块，再把真正需要的 full reload 延后处理；同时通过丢弃模块时的状态清理，减少反复卡在旧 reload 队列上的概率。**

### [维度 D5] `DebugServer V2` 的协议面基本没变，但 data breakpoint 的运行时模型已经不是同一个并发假设

```
[D5-Deep] Data Breakpoint Runtime Path
Hazelight
Socket message
├─ DataBreakpoints[] authoritative state
└─ VectoredExceptionHandler mutates same array directly

AngelPortV2
Socket message
├─ DataBreakpoints[] authoritative state
├─ RebuildActiveDataBreakpoints()
│  └─ ActiveDataBreakpoints[4] atomic snapshot
├─ VectoredExceptionHandler reads/writes atomic snapshot
└─ SyncActiveDataBreakpointsToAuthoritativeState()
```

前文已经说明两边 `DebugServerVersion == 2`、source breakpoint / callstack / evaluate 主消息面基本一致。本轮继续往下看后，可以更明确地说：**真正变化最大的不是协议，而是 data breakpoint 的运行时并发模型。**

Hazelight 让 socket 收到的 `DataBreakpoints` 数组同时充当“权威状态”和“异常处理线程直接读写的工作状态”；当前插件则拆成两层：

1. `DataBreakpoints` 继续作为权威状态，便于消息序列化。  
2. `ActiveDataBreakpoints[4]` 作为硬件断点镜像，用 `TAtomic` 字段承接 vectored exception handler 的并发读写。  

[1] Hazelight 的数据断点结构是普通字段；异常处理器直接修改 `DataBreakpoints[i]`：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Debugging\AngelscriptDebugServer.h
// 位置: 291-306
// 说明: 上游的数据断点状态全部堆在一个普通结构里
// ============================================================================
struct FAngelscriptDataBreakpoint
{
	int32 Id;
	uint64 Address;
	uint8 AddressSize;
	int8 HitCount;
	bool bCppBreakpoint = false;
	FString Name;
	class asCContext* Context = nullptr;
	bool bTriggered = false;
	EAngelscriptDataBreakpointStatus Status = EAngelscriptDataBreakpointStatus::Keep;
};
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Debugging\AngelscriptDebugServer.cpp
// 位置: 191-239
// 说明: 上游的 vectored exception handler 直接读写权威数组
// ============================================================================
bool bCppBreakpointTriggered = false;
bool bBreakpointTriggered = false;
for (uint8 i = 0; i < FAngelscriptDebugServer::DATA_BREAKPOINT_HARDWARE_LIMIT; i++)
{
	if (!DebugServer->DataBreakpoints.IsValidIndex(i))
	{
		continue;
	}

	auto& Breakpoint = DebugServer->DataBreakpoints[i];
	if (Breakpoint.Status != EAngelscriptDataBreakpointStatus::Keep)
	{
		continue;
	}

	if (Breakpoint.HitCount > 0)
	{
		Breakpoint.HitCount--;
		...
	}
	else
	{
		Breakpoint.bTriggered = true;
	}

	asCContext* Context = (asCContext*)asGetActiveContext();
	Breakpoint.Context = Context;
	bCppBreakpointTriggered |= (Breakpoint.bTriggered && Breakpoint.bCppBreakpoint);
	bBreakpointTriggered |= Breakpoint.bTriggered;
}

if (bBreakpointTriggered)
{
	DebugServer->bBreakNextScriptLine = true;
	ApplyBreakpointsToThreadContext(DebugServer->DataBreakpoints, *ExceptionInfo->ContextRecord);
}
```

[2] 当前插件单独引入了 `FAngelscriptActiveDataBreakpoint`，把 hit count / status / triggered / context 都原子化：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h
// 位置: 330-392
// 说明: 当前插件把硬件断点运行时状态拆到 atomic mirror 结构里
// ============================================================================
struct FAngelscriptActiveDataBreakpoint
{
	int32 Id = -1;
	uint64 Address = 0;
	uint8 AddressSize = 0;
	bool bCppBreakpoint = false;
	TAtomic<int32> HitCount { 0 };
	TAtomic<bool> bTriggered { false };
	TAtomic<int8> Status { static_cast<int8>(EAngelscriptDataBreakpointStatus::Keep) };
	TAtomic<UPTRINT> ContextPtr { 0 };

	void Reset()
	{
		Id = -1;
		Address = 0;
		AddressSize = 0;
		bCppBreakpoint = false;
		HitCount.Store(0);
		bTriggered.Store(false);
		Status.Store(static_cast<int8>(EAngelscriptDataBreakpointStatus::Keep));
		ContextPtr.Store(0);
	}

	void CopyFrom(const FAngelscriptDataBreakpoint& Source)
	{
		Id = Source.Id;
		Address = Source.Address;
		AddressSize = Source.AddressSize;
		bCppBreakpoint = Source.bCppBreakpoint;
		HitCount.Store(Source.HitCount);
		bTriggered.Store(Source.bTriggered);
		SetStatus(Source.Status);
		SetContext(Source.Context);
	}
}
```

[3] 当前插件还把 active debug server 指针和清断点开关都做成了原子对象，异常处理器不再依赖“默认只有一个普通全局对象”这种假设：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 位置: 111-117, 404-423
// 说明: 当前插件显式维护 active debug server，并把清断点开关原子化
// ============================================================================
namespace DataBreakpoint_Windows
{
#if PLATFORM_WINDOWS && WITH_AS_DEBUGSERVER
	static TAtomic<FAngelscriptDebugServer*> GActiveDebugServer { nullptr };
	static PVOID DegbugRegisterExceptionHandlerHandle;
	static TAtomic<bool> GClearDataBreakpoints { false };
}

...

#if PLATFORM_WINDOWS && WITH_AS_DEBUGSERVER
DataBreakpoint_Windows::GActiveDebugServer.Store(this);
if (DataBreakpoint_Windows::DegbugRegisterExceptionHandlerHandle)
{
	::RemoveVectoredExceptionHandler(DataBreakpoint_Windows::DegbugRegisterExceptionHandlerHandle);
}
DataBreakpoint_Windows::DegbugRegisterExceptionHandlerHandle = ::AddVectoredExceptionHandler(0, DataBreakpoint_Windows::DebugRegisterExceptionHandler);
#endif

...

#if PLATFORM_WINDOWS && WITH_AS_DEBUGSERVER
DataBreakpoint_Windows::GActiveDebugServer.Store(nullptr);
::RemoveVectoredExceptionHandler(DataBreakpoint_Windows::DegbugRegisterExceptionHandlerHandle);
#endif
```

[4] 这会把真正由异常处理器读写的状态切换到 `ActiveDataBreakpoints`，而不是直接改权威数组：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 位置: 286-338, 1248-1277
// 说明: 当前插件把权威状态与异常处理路径解耦
// ============================================================================
const int32 ActiveBreakpointCount = FMath::Min(DebugServer->ActiveDataBreakpointCount.Load(), FAngelscriptDebugServer::DATA_BREAKPOINT_HARDWARE_LIMIT);
...
auto& Breakpoint = DebugServer->ActiveDataBreakpoints[i];
if (Breakpoint.GetStatus() != EAngelscriptDataBreakpointStatus::Keep)
{
	continue;
}

const int32 PreviousHitCount = Breakpoint.HitCount.Load();
...
Breakpoint.SetContext(Context);
bCppBreakpointTriggered |= (bTriggeredThisIteration && Breakpoint.bCppBreakpoint);
bBreakpointTriggered |= bTriggeredThisIteration || Breakpoint.bTriggered.Load();

...

void FAngelscriptDebugServer::RebuildActiveDataBreakpoints()
{
	ActiveDataBreakpointCount.Store(0);
	const int32 BreakpointCountToCopy = FMath::Min(DataBreakpoints.Num(), DATA_BREAKPOINT_HARDWARE_LIMIT);
	for (int32 BreakpointIndex = 0; BreakpointIndex < BreakpointCountToCopy; ++BreakpointIndex)
	{
		ActiveDataBreakpoints[BreakpointIndex].CopyFrom(DataBreakpoints[BreakpointIndex]);
	}
	...
	ActiveDataBreakpointCount.Store(BreakpointCountToCopy);
}

void FAngelscriptDebugServer::SyncActiveDataBreakpointsToAuthoritativeState()
{
	const int32 BreakpointCountToSync = FMath::Min(DataBreakpoints.Num(), ActiveDataBreakpointCount.Load());
	for (int32 BreakpointIndex = 0; BreakpointIndex < BreakpointCountToSync; ++BreakpointIndex)
	{
		ActiveDataBreakpoints[BreakpointIndex].CopyTo(DataBreakpoints[BreakpointIndex]);
	}
}
```

[5] 当前插件的调试回归测试则先把 source breakpoint / stepping 主链锁死，说明它在工程化上优先保住最常用路径：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp
// 位置: 370-431
// 说明: 断点命中、停止原因、callstack 行号都被自动化验证
// ============================================================================
FAngelscriptBreakpoint Breakpoint;
Breakpoint.Filename = Fixture.Filename;
Breakpoint.ModuleName = Fixture.ModuleName.ToString();
Breakpoint.LineNumber = Fixture.GetLine(TEXT("BreakpointHelperLine"));
if (!TestTrue(TEXT("Debugger.Breakpoint.HitLine should send the target breakpoint"), Client.SendSetBreakpoint(Breakpoint)))
{
	AddError(Client.GetLastError());
	return false;
}

if (!WaitForBreakpointCount(*this, Session, 1, TEXT("Debugger.Breakpoint.HitLine should observe the breakpoint registration before running the script")))
{
	return false;
}

...

TestEqual(TEXT("Debugger.Breakpoint.HitLine should stop because of a breakpoint"), StopMessage->Reason, FString(TEXT("breakpoint")));
TestTrue(TEXT("Debugger.Breakpoint.HitLine should report the fixture filename in the top stack frame"), Callstack.Frames[0].Source.EndsWith(Fixture.Filename));
TestEqual(TEXT("Debugger.Breakpoint.HitLine should stop at the requested helper line"), Callstack.Frames[0].LineNumber, Fixture.GetLine(TEXT("BreakpointHelperLine")));
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp
// 位置: 386-448
// 说明: StepIn/StepOver/StepOut 以 stopped reason + callstack 行号做回归验证
// ============================================================================
FAngelscriptBreakpoint Breakpoint;
Breakpoint.Filename = Fixture.Filename;
Breakpoint.ModuleName = Fixture.ModuleName.ToString();
Breakpoint.LineNumber = Fixture.GetLine(TEXT("StepCallLine"));
if (!TestTrue(TEXT("Stepping.StepIn should set the call-site breakpoint"), Client.SendSetBreakpoint(Breakpoint)))
{
	AddError(Client.GetLastError());
	return false;
}

...

TestEqual(TEXT("Stepping.StepIn first stop should be a breakpoint"), FirstStop->Reason, FString(TEXT("breakpoint")));
TestEqual(TEXT("Stepping.StepIn second stop should be a step"), SecondStop->Reason, FString(TEXT("step")));
TestEqual(TEXT("Stepping.StepIn should first stop at the call line"),
	MonitorResult.Stops[0].Callstack->Frames[0].LineNumber,
	Fixture.GetLine(TEXT("StepCallLine")));
TestTrue(TEXT("Stepping.StepIn should enter the callee frame"), MonitorResult.Stops[1].Callstack->Frames.Num() >= 2);
```

### 差距判断

| 子问题 | 结论 | 依据 |
| --- | --- | --- |
| `DebugServer V2` 协议是否分叉 | **没有明显分叉** | breakpoint / stepping / callstack 主链仍按相同语义工作 |
| data breakpoint 的运行时并发模型是否仍一致 | **不一致** | 当前插件新增 `FAngelscriptActiveDataBreakpoint`、`GActiveDebugServer` 与 sync 流程 |
| 当前插件是否只是“代码更多” | **不是** | 新增代码对应的是权威状态与异常处理状态解耦 |
| 当前插件是否把调试体验回归验证工程化 | **是** | `AngelscriptDebuggerBreakpointTests.cpp` 与 `SteppingTests.cpp` 把主链锁成自动化测试 |

因此，`D5` 本轮最重要的新结论是：**如果只看消息枚举，会误以为两边几乎一样；但只要深入到 data breakpoint 的异常处理路径，就会发现当前插件已经不再依赖 Hazelight 那种“socket 状态数组直接被异常处理器改写”的并发假设，而是改成了“authoritative state + atomic mirror”的双层模型。**

---

## 深化分析 (2026-04-08 18:55:51)

### [维度 D3] `ScriptMixin` 在当前插件里已经分裂成“保留的 member surface”和“退回 namespace surface”的两条路径

前文已经证明 `BlueprintOverride` 与 script class -> Blueprint child 继承链没有丢。本轮补的不是那条主链，而是更细的结论：**当前插件不再把所有 Hazelight 风格的 mixin function library 都维持成同一种暴露方式。**

```
[D3-Deep] Mixin Surface Split
Hazelight
├─ UCLASS(meta = ScriptMixin)                         // 类级 mixin
├─ UFUNCTION(ScriptCallable)                          // 非 Blueprint 也能进反射绑定
├─ Helper_FunctionSignature strips first parameter
└─ BindBlueprintCallable -> BindMethodDirect(member)

AngelPortV2
├─ retained class-level mixin on selected wrappers    // 局部保留
├─ several wrapper libs comment out ScriptMixin       // 局部降级
├─ function-level ScriptMethod still tested           // 函数级 mixin 还在
└─ no class meta => BindBlueprintCallable -> namespace/global static
```

Hazelight 的 `InputComponent` / `PlayerController` / `PlayerInput` wrapper 是标准的 class-level mixin：`UCLASS(Meta=(ScriptMixin=...))` 决定首参能否被剥离，`UFUNCTION(ScriptCallable)` 则保证这些本来不是 Blueprint surface 的 helper 仍会进入 script 绑定链。当前插件把这一类 wrapper 改成了 `BlueprintCallable`，但同时把 `ScriptMixin` 注释掉；因此它们**至少在 reflective binding 这条路径上**已经不再自动折叠成成员方法。

与此同时，当前插件并没有把 mixin 整体砍掉。`RuntimeCurveLinearColorMixinLibrary` 仍保留类级 `ScriptMixin`，而函数级 `ScriptMethod` 还有专门的自动化测试锁定。所以这个维度不能一刀切地说“没有 mixin”，更准确的判断是：**主链保留，但 class-level mixin 的覆盖面已经从“普遍规则”退成“选择性保留”。**

[1] 上游 `InputComponent` wrapper 仍是标准 class-level mixin；当前插件把同一组声明改成了 stock-UHT 友好的 `BlueprintCallable`，并把 `ScriptMixin` / `ScriptCallable` 留成注释：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Public\FunctionLibraries\InputComponentScriptMixinLibrary.h
// 位置: 12-22, 109-149
// 说明: UEAS2 仍把这些 helper 当成真正的 ScriptMixin library
// ============================================================================
UCLASS(Meta = (ScriptMixin = "UInputComponent"))
class UInputComponentScriptMixinLibrary : public UObject
{
	GENERATED_BODY()
public:
	UFUNCTION(ScriptCallable)
	static void BindAction(UInputComponent* Component, const FName& ActionName, EInputEvent KeyEvent, const FInputActionHandlerDynamicSignature& Delegate)
	{
		// ★ 非 BlueprintCallable 的输入绑定 helper 仍能通过 ScriptCallable 进入 script 暴露面
		FInputActionBinding AB(ActionName, KeyEvent);
		AB.ActionDelegate = Delegate;
		Component->AddActionBinding(AB);
	}
};

UCLASS(Meta = (ScriptMixin = "APlayerController"))
class UPlayerControllerInputScriptMixinLibrary : public UObject
{
	GENERATED_BODY()
public:
	UFUNCTION(ScriptCallable)
	static void PushInputComponent(APlayerController* PlayerController, UInputComponent* Component)
	{
		PlayerController->PushInputComponent(Component);
	}
};
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/InputComponentScriptMixinLibrary.h
// 位置: 12-24, 116-145, 153-161
// 说明: 当前插件把同一类 wrapper 改成标准 BlueprintCallable，并去掉 class-level ScriptMixin metadata
// ============================================================================
//UCLASS(Meta = (ScriptMixin = "UInputComponent"))
UCLASS(Meta = ())
class UInputComponentScriptMixinLibrary : public UObject
{
	GENERATED_BODY()
public:
	//UFUNCTION(ScriptCallable)
	UFUNCTION(BlueprintCallable)
	static void BindAction(UInputComponent* Component, const FName& ActionName, EInputEvent KeyEvent, const FInputActionHandlerDynamicSignature& Delegate)
	{
		FInputActionBinding AB(ActionName, KeyEvent);
		AB.ActionDelegate = Delegate;
		Component->AddActionBinding(AB);
	}
};

//UCLASS(Meta = (ScriptMixin = "APlayerController"))
UCLASS(Meta = ())
class UPlayerControllerInputScriptMixinLibrary : public UObject
{
	GENERATED_BODY()
public:
	//UFUNCTION(ScriptCallable)
	UFUNCTION(BlueprintCallable)
	static void PushInputComponent(APlayerController* PlayerController, UInputComponent* Component)
	{
		PlayerController->PushInputComponent(Component);
	}
};

//UCLASS(Meta = (ScriptMixin = "UPlayerInput"))
UCLASS(Meta = ())
class UPlayerInputScriptMixinLibrary : public UObject
{
	GENERATED_BODY()
public:
	//UFUNCTION(ScriptCallable)
	UFUNCTION(BlueprintCallable)
	static void AddActionMapping(UPlayerInput* PlayerInput, const FInputActionKeyMapping& KeyMapping)
	{
		PlayerInput->AddActionMapping(KeyMapping);
	}
};
```

[2] 当前插件的 reflective path 只有在类上仍有 `ScriptMixin` metadata 时才会剥首参改成成员方法；否则就按 namespace/global static 绑定：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h
// 位置: 276-305
// 说明: class-level mixin 是否生效，完全取决于 UClass metadata 里是否还有 ScriptMixin
// ============================================================================
// If our class is marked as a 'script mixin', and our argument matches, bind it as a member
bool bFoundMixin = false;
const FString& MixinClasses = Function->GetOuterUClass()->GetMetaData(NAME_Signature_ScriptMixin);
if (MixinClasses.Len() != 0 && ArgumentTypes.Num() > 0
	&& (ArgumentTypes[0].IsObjectPointer() || ArgumentTypes[0].bIsReference))
{
	// ★ 只有命中 ScriptMixin metadata 才会剥掉首参，改成成员暴露
	ArgumentTypes.RemoveAt(0);
	ArgumentNames.RemoveAt(0);
	ArgumentDefaults.RemoveAt(0);
	ClassName = Mixin;
	bStaticInScript = false;
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp
// 位置: 100-125
// 说明: mixin 没命中时就走 namespace/global static；命中时才走 member bind
// ============================================================================
if (Signature.bStaticInScript)
{
	// ★ 没有 ScriptMixin metadata 的静态函数，绑定成 namespace/global function
	FAngelscriptBinds::FNamespace ns(Signature.ClassName);
	int FunctionId = FAngelscriptBinds::BindGlobalFunction(Signature.Declaration, ASFuncPtr, Entry->Caller);
	Signature.ModifyScriptFunction(FunctionId);
}
else if (Signature.bStaticInUnreal)
{
	// ★ 只有 class-level mixin 成立，才会走成员函数语义
	int FunctionId = FAngelscriptBinds::BindMethodDirect(
		Signature.ClassName,
		Signature.Declaration, ASFuncPtr,
		asCALL_CDECL_OBJFIRST, Entry->Caller);
	Signature.ModifyScriptFunction(FunctionId);
}
```

[3] 当前插件保留了 function-level mixin 路径，并用测试锁死；但这套测试锁的是 `ScriptMethod`，不是上面那批被注释掉 class-level `ScriptMixin` 的 wrapper：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptUhtCoverageTestTypes.h
// 位置: 31-32
// 说明: 当前插件仍允许用函数级 metadata 把首参折叠成脚本成员
// ============================================================================
UFUNCTION(BlueprintCallable, meta = (ScriptMethod, DisplayName = "Get Coverage Value"))
static int32 GetCoverageValue(const UObject* Target);
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp
// 函数: FAngelscriptScriptMethodMetadataCoverageTest::RunTest
// 位置: 604-644
// 说明: 测试明确要求函数级 ScriptMethod 仍按 member 语义工作
// ============================================================================
FAngelscriptFunctionSignature Signature(HostType.ToSharedRef(), ScriptMethodFunction);
TestTrue(TEXT("FunctionLevelScriptMethodUsesFirstParameterAsMixin should keep the Unreal function static"), Signature.bStaticInUnreal);
TestFalse(TEXT("FunctionLevelScriptMethodUsesFirstParameterAsMixin should bind ScriptMethod functions as script members"), Signature.bStaticInScript);
TestEqual(TEXT("FunctionLevelScriptMethodUsesFirstParameterAsMixin should remove the first parameter from the exposed signature"), Signature.ArgumentTypes.Num(), 0);
```

### 差距判断

| 子问题 | 结论 | 依据 |
| --- | --- | --- |
| `BlueprintOverride` / 继承链是否丢失 | **没有** | 前文已证实 override/inheritance 主链仍在 |
| function-level `ScriptMethod` 是否保留 | **保留** | `AngelscriptUhtCoverageTestTypes.h:31-32` 与 `AngelscriptBindConfigTests.cpp:604-644` |
| Hazelight 那批 class-level mixin wrapper 是否原样保留 | **不是** | `InputComponentScriptMixinLibrary.h` 等已把 `ScriptMixin`/`ScriptCallable` 注释掉 |
| 这应判为“完全没有实现”吗 | **不应** | 更准确是“主链保留，但 class-level mixin 覆盖面局部收窄” |

### [维度 D2/D8] `ScriptCallable` 不再是当前插件的 authoring contract；它被拆成 `BlueprintCallable + exporter filter + 定向 direct-bind 修复`

```
[D2/D8-Deep] UHT Contract Shift
Hazelight
├─ UFUNCTION(ScriptCallable)
├─ UhtFunctionSpecifiers adds MetaData["ScriptCallable"]
├─ UhtFunction::ResolveSelf keeps CPP_Default_* for ScriptCallable
└─ reflective bind can treat non-Blueprint helpers as first-class exports

AngelPortV2
├─ wrapper headers switch to BlueprintCallable
├─ codegen filters NotInAngelscript / BlueprintInternalUseOnly
├─ UsableInAngelscript re-opens selected internal nodes
├─ direct-bind whitelist repairs inline / out-ref edge cases
└─ tests lock ScriptMethod / override / inline fallback behavior
```

Hazelight 的 `ScriptCallable` 不是普通注释，而是 UHT 真正认识的自定义 specifier。它的直接含义至少有两层：  
第一，非 `BlueprintCallable` 的 helper 也可以进 script 绑定链。  
第二，UHT 会像处理 `BlueprintCallable` 一样，为这类函数保留 `CPP_Default_*` 元数据，方便脚本侧重建默认参数。  

当前插件没有把这条自定义 specifier 路继续移植到 stock engine；它改成了更保守的 authoring contract：  
1. 想走自动导出的函数，尽量声明成 `BlueprintCallable`。  
2. 对 `BlueprintInternalUseOnly` 节点，只有显式打 `UsableInAngelscript` 才重开。  
3. 对 `URuntimeFloatCurveMixinLibrary` 这种 inline / out-ref 边角函数，再用定向 whitelist 把 direct bind 补回来。  

所以这一维的差距要拆开判断：**自定义 specifier 写法本身在当前插件里不再成立；但大面积 callable coverage 并没有消失，而是换成了 stock-UHT 友好的生成/过滤模型。**

[1] UEAS2 直接改了 UHT specifier 解析和默认值回填逻辑：

```csharp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Programs\Shared\EpicGames.UHT\Specifiers\UhtFunctionSpecifiers.cs
// 位置: 23-28
// 说明: ScriptCallable 在 UEAS2 里是 UHT 真正认识的自定义函数 specifier
// ============================================================================
// AS FIX (LV): Angelscript support
[UhtSpecifier(Extends = UhtTableNames.Function, ValueType = UhtSpecifierValueType.Legacy)]
private static void ScriptCallableSpecifier(UhtSpecifierContext specifierContext)
{
	UhtFunction function = (UhtFunction)specifierContext.Type;
	// ★ UFUNCTION(ScriptCallable) 会直接落成 metadata
	function.MetaData.Add("ScriptCallable", "");
}
```

```csharp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Programs\Shared\EpicGames.UHT\Types\UhtFunction.cs
// 位置: 566-591
// 说明: UEAS2 把 ScriptCallable 纳入“需要保留 C++ 默认值 metadata”的路径
// ============================================================================
protected override bool ResolveSelf(UhtResolvePhase phase)
{
	bool results = base.ResolveSelf(phase);

	// If true, we need to echo the default values into the meta data
	bool storeCppDefaultValueInMetaData = FunctionFlags.HasAnyFlags(EFunctionFlags.BlueprintCallable | EFunctionFlags.Exec)
		|| MetaData.ContainsKey("ScriptCallable");

	if (phase == UhtResolvePhase.Properties)
	{
		UhtPropertyParser.ResolveChildren(this, GetPropertyParseOptions(false));
		if (storeCppDefaultValueInMetaData)
		{
			// ★ 这让 ScriptCallable 即便不是 BlueprintCallable，也能保住 CPP_Default_*
			...
		}
	}
}
```

[2] 当前插件则把 wrapper header 改成 stock-UHT 友好的 `BlueprintCallable`，并在 exporter 里用 metadata 精细过滤：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/RuntimeFloatCurveMixinLibrary.h
// 位置: 16-26, 46-48, 68-70
// 说明: 当前插件不再声明 UFUNCTION(ScriptCallable)，而是转成 BlueprintCallable
// ============================================================================
//UCLASS(meta = (ScriptMixin = "FRuntimeFloatCurve UCurveFloat"))
UCLASS(meta = ())
class ANGELSCRIPTRUNTIME_API URuntimeFloatCurveMixinLibrary : public UObject
{
	GENERATED_BODY()
public:
	//UFUNCTION(ScriptCallable, Category = "Math|Curves")
	UFUNCTION(BlueprintCallable, Category = "Math|Curves")
	static float GetFloatValue(const FRuntimeFloatCurve& Target, const float InTime, const float DefaultValue = 0);

	//UFUNCTION(ScriptCallable, Category = "Math|Curves", Meta = (ScriptName = "GetTimeRange"))
	UFUNCTION(BlueprintCallable, Category = "Math|Curves", Meta = (ScriptName = "GetTimeRange"))
	static void GetTimeRange_Double(const FRuntimeFloatCurve& Target, double& MinTime, double& MaxTime);

	//UFUNCTION(ScriptCallable, Category = "Math|Curves")
	UFUNCTION(BlueprintCallable, Category = "Math|Curves")
	static int32 GetNumKeys(const FRuntimeFloatCurve& Target);
};
```

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs
// 位置: 490-514
// 说明: 自动导出走的是 BlueprintCallable surface + metadata filter，而不是自定义 UHT specifier
// ============================================================================
private static bool ShouldGenerate(UhtClass classObj, UhtFunction function)
{
	if (!AngelscriptFunctionTableExporter.IsBlueprintCallable(function))
	{
		return false;
	}

	if (function.MetaData.ContainsKey("NotInAngelscript") ||
		(function.MetaData.ContainsKey("BlueprintInternalUseOnly") && !function.MetaData.ContainsKey("UsableInAngelscript")))
	{
		// ★ BlueprintInternalUseOnly 只有显式打 UsableInAngelscript 才重开
		return false;
	}

	return !function.FunctionExportFlags.ToString().Contains("CustomThunk", StringComparison.Ordinal);
}
```

[3] 当前插件把难以直接重建签名的 edge case 收缩成少量定向修复，并用测试锁住：

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs
// 位置: 57-107
// 说明: 对少数 inline / out-ref 边角函数走白名单 direct-bind fallback
// ============================================================================
if (failureReason == "overloaded-unresolved" && !IsWhitelistedDirectBindFallback(classObj, function))
{
	return false;
}

private static bool IsWhitelistedDirectBindFallback(UhtClass classObj, UhtFunction function)
{
	// ★ 这里不是全局兜底，而是点名修复已知困难函数
	return classObj.SourceName == "URuntimeFloatCurveMixinLibrary" &&
		(function.SourceName == "GetNumKeys" || function.SourceName == "GetTimeRange");
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptUhtCoverageTestTypes.h
// 位置: 22-32
// 说明: 当前插件把 exporter filter 的例外条件和函数级 mixin 都写进测试样本
// ============================================================================
UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", UsableInAngelscript = "true"))
static int32 InternalCallableWithOverride(UAngelscriptUhtCoverageTestObject* Target);

UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true"))
static int32 InternalCallableWithoutOverride(UAngelscriptUhtCoverageTestObject* Target);

UFUNCTION(BlueprintCallable, meta = (ScriptMethod, DisplayName = "Get Coverage Value"))
static int32 GetCoverageValue(const UObject* Target);
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp
// 位置: 588-600, 781-847
// 说明: override 开关与 inline/out-ref fallback 都有自动化回归
// ============================================================================
TestTrue(TEXT("BlueprintInternalUseOnlyCanBeOverriddenForAngelscript should mark the override function as UsableInAngelscript"), WithOverride->HasMetaData(TEXT("UsableInAngelscript")));
TestFalse(TEXT("BlueprintInternalUseOnlyCanBeOverriddenForAngelscript should not skip override-marked functions"), FAngelscriptBinds::ShouldSkipBlueprintCallableFunction(WithOverride));
TestTrue(TEXT("BlueprintInternalUseOnlyCanBeOverriddenForAngelscript should still skip BlueprintInternalUseOnly functions without an override"), FAngelscriptBinds::ShouldSkipBlueprintCallableFunction(WithoutOverride));

return TestTrue(TEXT("InlineDefinitionFunctionsCanRecoverDirectBind should recover a direct bind instead of ERASE_NO_FUNCTION"), IsFunctionEntryBound(*InlineEntry));
...
return TestTrue(TEXT("InlineOutRefFunctionsCanRecoverDirectBind should recover a direct bind instead of ERASE_NO_FUNCTION"), IsFunctionEntryBound(*InlineEntry));
```

### 差距判断

| 子问题 | 结论 | 依据 |
| --- | --- | --- |
| `UFUNCTION(ScriptCallable)` 这种 authoring syntax 是否仍是当前插件的一等路径 | **不是** | 当前 wrapper headers 已改成 `BlueprintCallable`，stock engine 没有对应 UHT specifier |
| `BlueprintCallable` 大面积导出是否缺失 | **没有缺失** | `AngelscriptFunctionTableCodeGenerator.cs:490-514` 仍在自动导出 |
| `BlueprintInternalUseOnly` 的例外能力是否保留 | **保留且更显式** | `UsableInAngelscript` 过滤 + 测试 |
| inline / out-ref 这种难点是“没有实现”还是“定向修复” | **定向修复** | `AngelscriptFunctionSignatureBuilder.cs:57-107` 与测试 `781-847` |

### [维度 D8/D11] `bIsScriptClass` 的作用域已经从“全局 UClass 补丁”缩回“插件自有 UASClass”

```
[D8/D11-Deep] Script Class Identity Scope
Hazelight engine patch
├─ UClass owns ScriptTypePtr / bIsScriptClass / ASReflectedFunctionPointers
├─ Bind_BlueprintType uses UClass::GetFunctionMap()
└─ engine/editor code can branch on Class->bIsScriptClass everywhere

AngelPortV2 plugin path
├─ UASClass : UClass owns ScriptTypePtr / bIsScriptClass
├─ ClassGenerator sets NewClass->bIsScriptClass = true
├─ Bind_BlueprintType first Cast<UASClass>()
├─ function scan rebuilt via GenerateFunctionList() + FindFunctionByName()
└─ generated table/tests explicitly pull UASClass into the coverage set
```

前几轮已经说明当前插件用 `UASClass` 替代了 Hazelight 的引擎基类补丁。本轮再往下看，可以更明确地说：**差异不只是“字段搬家”，而是 script class identity 的作用域从“全局 UClass 事实”缩成了“只有知道 `UASClass` 的代码才能显式识别”。**

这带来两个直接后果：  
1. 当前插件更容易作为 stock engine 插件分发，这是 `D11` 的实际收益。  
2. 但任何想识别 script class 的路径，都要显式 cast / 适配；它不再像 UEAS2 那样天然渗入所有 `UClass*` 分支，这是 `D8` 的真实代价。  

[1] UEAS2 把 script class 身份直接塞进 `UClass`，并给出 `GetFunctionMap()` 这种全局可用入口：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Runtime\CoreUObject\Public\UObject\Class.h
// 位置: 3910-3914
// 说明: script class 身份是 UClass 全局字段，不需要额外子类判断
// ============================================================================
TMap<FName, ASAutoCaller::FReflectedFunctionPointers> ASReflectedFunctionPointers;
void* ScriptTypePtr = nullptr;
bool bIsScriptClass = false;

const TMap<FName, TObjectPtr<UFunction>>& GetFunctionMap() { return FuncMap; }
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Binds\Bind_BlueprintType.cpp
// 位置: 1004-1024
// 说明: 上游直接把 UClass 当成“天然知道 script class 的类型”
// ============================================================================
// Ignore runtime generated types (impossible?)
if (Class->bIsScriptClass)
	return false;

UClass* CheckClass = Class;
bool bHasBlueprintCallable = false;
while (CheckClass != nullptr && !bHasBlueprintCallable)
{
	auto& FuncMap = CheckClass->GetFunctionMap();
	for (auto& Elem : FuncMap)
	{
		if (Elem.Value->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintEvent))
		{
			bHasBlueprintCallable = true;
			break;
		}
	}
}
```

[2] 当前插件把这些状态缩回 `UASClass`，并在需要的位置手动做 subtype-aware 处理：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h
// 位置: 28-30, 93-101
// 说明: script class 身份退回到插件自有的 UASClass
// ============================================================================
void* ScriptTypePtr = nullptr;
bool bIsScriptClass = false;

UFUNCTION(BlueprintCallable, Category = "Angelscript")
FString GetSourceFilePath() const;

UFUNCTION(BlueprintCallable, Category = "Angelscript")
bool IsDeveloperOnly() const;
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 位置: 3289-3292
// 说明: script class 身份由插件自己的 class generator 负责写入
// ============================================================================
// Set up the class' base data
NewClass->ClassFlags = CLASS_CompiledFromBlueprint;
NewClass->bIsScriptClass = true;
NewClass->ClassFlags |= (SuperClass->ClassFlags & CLASS_ScriptInherit);
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp
// 位置: 985-1008
// 说明: 当前插件必须先识别 UASClass，再用公开 API 重建函数扫描
// ============================================================================
UASClass* asClass = Cast<UASClass>(Class);
if (asClass != nullptr && asClass->bIsScriptClass)
	return false;

UClass* CheckClass = Class;
bool bHasBlueprintCallable = false;
TArray<FName> NameArray;
while (CheckClass != nullptr && !bHasBlueprintCallable)
{
	// ★ 不再依赖引擎 patch 出来的 GetFunctionMap()，而是改用 stock engine 可用的 GenerateFunctionList()
	CheckClass->GenerateFunctionList(NameArray);
	for (auto& Elem : NameArray)
	{
		UFunction* Function = CheckClass->FindFunctionByName(Elem);
		if (Function->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintEvent))
		{
			bHasBlueprintCallable = true;
			break;
		}
	}
}
```

[3] 当前插件还专门验证 `UASClass` 自己能进入 generated function table，而不是只覆盖原生引擎类型：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp
// 位置: 512-551
// 说明: 测试要求 UASClass::IsDeveloperOnly 也能进入自动生成的 ClassFuncMaps
// ============================================================================
UFunction* IsDeveloperOnlyFunction = UASClass::StaticClass()->FindFunctionByName(TEXT("IsDeveloperOnly"));
...
const TMap<FString, FFuncEntry>* ScriptClassEntries = ClassFuncMaps.Find(UASClass::StaticClass());
...
const FFuncEntry* IsDeveloperOnlyEntry = ScriptClassEntries->Find(IsDeveloperOnlyFunction->GetName());
...
TestTrue(TEXT("GeneratedBlueprintCallableEntriesPopulateClassMaps should bind UASClass::IsDeveloperOnly to a direct native function entry"), IsFunctionEntryBound(*IsDeveloperOnlyEntry));
```

### 差距判断

| 子问题 | 结论 | 依据 |
| --- | --- | --- |
| script class 身份是否仍是引擎全局 `UClass` 事实 | **不是** | 当前只在 `UASClass` 上持有 `bIsScriptClass` |
| runtime class 生成是否缺失 | **没有缺失** | `AngelscriptClassGenerator.cpp:3289-3292` 仍会写入 `bIsScriptClass` |
| 函数枚举/类型识别是否完全等价 | **不等价** | Hazelight 走 `GetFunctionMap()`；当前插件改成 `Cast<UASClass> + GenerateFunctionList()` |
| 这带来的主要收益是什么 | **插件可部署性更强** | 不再要求 `CoreUObject` / UHT 引擎补丁 |

因此，本轮补完后的结论可以更精确地写成三句：  
1. 当前插件**保住了 Blueprint 主链**，但并没有把所有 Hazelight 的 class-level mixin wrapper 原样保留。  
2. 当前插件**放弃了 `ScriptCallable` 作为自定义 UHT specifier 的 authoring contract**，转而依赖 `BlueprintCallable + metadata filter + 定向 direct-bind 修复`。  
3. 当前插件**把 script class 身份从引擎全局事实缩回到插件自有 subtype**，这正是它能脱离引擎补丁分发的原因，也是它必须额外维护 wrapper、codegen 与测试护栏的根源。  

---

## 深化分析 (2026-04-08 19:07:53)

### [维度 D5] `DebugServer V2` 的消息面基本没变，但 session owner 已从 `manager singleton` 改成 `engine scope`；只有 data breakpoint 后端仍保留进程级单入口

```
[D5-Deep] Debug Session Ownership
Hazelight
├─ FAngelscriptManager singleton
│  ├─ DebugServer*
│  └─ new FAngelscriptDebugServer(Port)
├─ FAngelscriptDebugServer
│  └─ no OwnerEngine field
└─ data breakpoint handler
   └─ reads FAngelscriptManager::Get().DebugServer

AngelPortV2
├─ FAngelscriptEngineContextStack
│  └─ resolves active engine scope
├─ each FAngelscriptEngine
│  ├─ DebugServer*
│  └─ new FAngelscriptDebugServer(this, Port)
├─ FAngelscriptDebugServer
│  └─ stores OwnerEngine
└─ data breakpoint handler
   └─ still reads process-global GActiveDebugServer
```

前文已经把 `DebugServerVersion == 2` 和主消息枚举对齐钉住了；本轮补的是更底层的 owner model。Hazelight 的调试入口仍然是全局 `FAngelscriptManager`，它天然假设“当前进程里只有一个脚本运行时值得被调试”。当前插件则把这层 owner 下沉到 `FAngelscriptEngine`，再借 `FAngelscriptEngineContextStack` 解决作用域内的当前引擎解析。

这不是 wire protocol 分叉，而是**运行时会话归属**分叉：  
1. 对 source breakpoint / variables / evaluate 这类普通调试消息，当前插件已经具备 engine-scoped 的宿主边界。  
2. 但 Windows data breakpoint 后端仍保留进程级 `GActiveDebugServer` 单入口，所以“完全并行的多 engine data breakpoint”目前还不是事实。  

[1] UEAS2 的调试器 owner 就是全局 manager；`DebugServer` 自身也没有 `OwnerEngine`：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Public\AngelscriptManager.h
// 位置: 373-378
// 说明: 上游把调试器挂在 manager singleton 上
// ============================================================================
#if WITH_AS_DEBUGSERVER
	class FAngelscriptDebugServer* DebugServer = nullptr;
	bool IsEvaluatingDebuggerWatch();
#endif

void BroadcastDebugDatabase() const;
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\AngelscriptManager.cpp
// 位置: 388-394
// 说明: 启动时直接在 manager 内部 new 一个全局 DebugServer
// ============================================================================
#if WITH_AS_DEBUGSERVER
	if ((!bUsePrecompiledData || bScriptDevelopmentMode) && FApp::HasProjectName())
	{
		int Port = FAngelscriptManager::ConfigSettings->ConnectionPort;
		FParse::Value(FCommandLine::Get(), TEXT("-asdebugport="), Port);
		DebugServer = new FAngelscriptDebugServer(Port);
	}
#endif
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Debugging\AngelscriptDebugServer.h
// 位置: 510-526
// 说明: UEAS2 的 DebugServer 类型没有 owner 字段，默认跟随全局 manager
// ============================================================================
class FAngelscriptDebugServer
{
	class FTcpListener* Listener;
	TQueue<class FSocket*, EQueueMode::Mpsc> PendingClients;
	TArray<class FSocket*> Clients;
	TArray<class FSocket*> ClientsThatWantDebugDatabase;
	TArray<class FSocket*> ClientsThatAreDebugging;
	double NextPingDebuggerAliveTime = 0.0;
	...
};
```

[2] 当前插件把调试器改成 `engine-owned`，并用 context stack 验证嵌套作用域解析：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 71-75
// 说明: 当前插件已经删除全局 GAngelscriptEngine，改成作用域栈解析
// ============================================================================
static TArray<FAngelscriptEngine*> GAngelscriptEngineContextStack;

FAngelscriptEngine::FAngelscriptDebugStack* GAngelscriptStack = nullptr;
// GAngelscriptEngine removed — engine resolution now uses FAngelscriptEngineContextStack
static bool GAngelscriptLineReentry = false;
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h
// 位置: 581-595, 640-641
// 说明: DebugServer 显式记录 OwnerEngine，构造时必须带 engine 指针
// ============================================================================
class ANGELSCRIPTRUNTIME_API FAngelscriptDebugServer
{
	FAngelscriptEngine* OwnerEngine;
	class FTcpListener* Listener;
	...
public:
	FAngelscriptEngine* GetOwnerEngine() const { return OwnerEngine; }
	...
public:
	FAngelscriptDebugServer(FAngelscriptEngine* InOwnerEngine, int Port);
	~FAngelscriptDebugServer();
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 1452-1456
// 说明: DebugServer 由每个 engine 实例创建，而不是全局 manager 创建
// ============================================================================
#if WITH_AS_DEBUGSERVER
	if ((!bUsePrecompiledData || bScriptDevelopmentMode) && FApp::HasProjectName())
	{
		DebugServer = new FAngelscriptDebugServer(this, RuntimeConfig.DebugServerPort);
	}
#endif
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp
// 位置: 174-191
// 说明: 测试明确要求嵌套 scope 优先返回当前栈顶 engine
// ============================================================================
TestTrue(TEXT("Context stack should start empty after guard clears it"), FAngelscriptEngineContextStack::IsEmpty());

{
	FAngelscriptEngineScope PrimaryScope(*PrimaryEngine);
	TestTrue(TEXT("Scoped resolution should return the primary engine while its scope is active"), &FAngelscriptEngine::Get() == PrimaryEngine.Get());
	TestTrue(TEXT("Context stack should expose the active primary engine"), FAngelscriptEngineContextStack::Peek() == PrimaryEngine.Get());

	{
		FAngelscriptEngineScope SecondaryScope(*SecondaryEngine);
		TestTrue(TEXT("Nested scoped resolution should prefer the nested engine"), &FAngelscriptEngine::Get() == SecondaryEngine.Get());
		TestTrue(TEXT("Context stack should update its top entry for nested scopes"), FAngelscriptEngineContextStack::Peek() == SecondaryEngine.Get());
	}

	TestTrue(TEXT("Nested scope teardown should restore the previous engine"), &FAngelscriptEngine::Get() == PrimaryEngine.Get());
	TestTrue(TEXT("Context stack should restore the previous engine after nested scope teardown"), FAngelscriptEngineContextStack::Peek() == PrimaryEngine.Get());
}
```

[3] 但 data breakpoint 后端还没有完全变成多 engine 并行模型：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Debugging\AngelscriptDebugServer.cpp
// 位置: 186-190
// 说明: 上游异常处理器直接回读 manager 全局 DebugServer
// ============================================================================
// TODO: This function should probably guard a bit better against multithreading, if we get exceptions from multiple threads
if (ExceptionInfo->ExceptionRecord->ExceptionCode == EXCEPTION_SINGLE_STEP && (ExceptionInfo->ContextRecord->Dr6 & 0xF) > 0)
{
	auto DebugServer = FAngelscriptManager::Get().DebugServer;
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 位置: 279-286, 402-415
// 说明: 当前插件把 handler 改成读 GActiveDebugServer，但它依旧是进程级单入口
// ============================================================================
if (ExceptionInfo->ExceptionRecord->ExceptionCode == EXCEPTION_SINGLE_STEP && (ExceptionInfo->ContextRecord->Dr6 & 0xF) > 0)
{
	FAngelscriptDebugServer* DebugServer = GActiveDebugServer.Load();
	if (DebugServer == nullptr)
	{
		return EXCEPTION_CONTINUE_SEARCH;
	}
	...
}

FAngelscriptDebugServer::FAngelscriptDebugServer(FAngelscriptEngine* InOwnerEngine, int Port)
{
	OwnerEngine = InOwnerEngine;
	...
	DataBreakpoint_Windows::GActiveDebugServer.Store(this);
	...
}
```

这一维的新判断应写得更精确：  
- `DebugServer V2` **协议一致**。  
- 调试会话 owner **实现方式不同**，当前插件更偏向 engine-scoped。  
- data breakpoint 后端 **不是“没有实现”**，而是**隔离粒度仍然保留进程级约束**。  

### [维度 D4] 当前插件额外补上了 interface-sensitive 的 editor cache 刷新，以及 reload state 的外部可观测性

```
[D4-Deep] Reload Observability
reload event
├─ OnStructReload
│  └─ mark RefreshAllActions
├─ OnClassReload
│  ├─ Hazelight: refresh old class only unless full refresh already set
│  └─ AngelPortV2: if interface touched -> force RefreshAllActions
├─ OnFullReload
│  └─ PerformReinstance()
└─ postmortem
   ├─ Hazelight: in-memory reload state only
   └─ AngelPortV2: dump ReloadState -> EditorReloadState.csv
```

前文把 reload requirement 分级、`SoftReloadOnly` 降级策略和 stale queue 清理讲清了；这一轮新增的发现是：**当前插件同时补了 editor cache 的保守刷新条件和 reload state 的外部化诊断面**。这两件事都不改变“能不能热重载”，但会直接影响 reload 后蓝图动作菜单是否稳定、以及问题能不能被离线复盘。

[1] Hazelight 的 `OnClassReload` 只知道“是否已经全量刷新过”，没有专门识别 interface 变更：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptEditor\Private\ClassReloadHelper.h
// 位置: 42-67
// 说明: 上游 class reload 只在已有 full refresh 标志下走全量刷新
// ============================================================================
FAngelscriptClassGenerator::OnStructReload.AddLambda(
[](UScriptStruct* OldStruct, UScriptStruct* NewStruct)
{
	ReloadState().ReloadStructs.Add(OldStruct, NewStruct);
	ReloadState().bRefreshAllActions = true;
});

FAngelscriptClassGenerator::OnClassReload.AddLambda(
[](UClass* OldClass, UClass* NewClass)
{
	if (OldClass != nullptr)
		ReloadState().ReloadClasses.Add(OldClass, NewClass);
	else
		ReloadState().NewClasses.Add(NewClass);

	bool bRefreshedAll = false;
	if (ReloadState().bRefreshAllActions)
		bRefreshedAll = true;

	if (OldClass != nullptr)
	{
		if (!bRefreshedAll && GEngine != nullptr)
		{
			auto& Database = FBlueprintActionDatabase::Get();
			Database.RefreshClassActions(OldClass);
```

[2] 当前插件在同一位置明确检测 interface 相关 reload，并强制切到全量动作刷新：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.h
// 位置: 59-84
// 说明: 只要旧类/新类本身是 interface，或 class 实现链触碰 interface，就直接抬升到 RefreshAllActions
// ============================================================================
FAngelscriptClassGenerator::OnClassReload.AddLambda(
[](UClass* OldClass, UClass* NewClass)
{
	if (OldClass != nullptr)
		ReloadState().ReloadClasses.Add(OldClass, NewClass);
	else
		ReloadState().NewClasses.Add(NewClass);

	const bool bTouchesInterfaceReload =
		(OldClass != nullptr && (OldClass->HasAnyClassFlags(CLASS_Interface) || OldClass->Interfaces.Num() > 0))
		|| (NewClass != nullptr && (NewClass->HasAnyClassFlags(CLASS_Interface) || NewClass->Interfaces.Num() > 0));
	if (bTouchesInterfaceReload)
	{
		// ★ interface 相关变更被视为 editor action cache 的高风险事件
		ReloadState().bRefreshAllActions = true;
	}

	bool bRefreshedAll = false;
	if (ReloadState().bRefreshAllActions)
		bRefreshedAll = true;
```

[3] 当前插件还把 editor reload state 接进统一 state dump 扩展，可直接落 CSV：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorStateDump.cpp
// 位置: 21-68, 114-126
// 说明: 这条能力在上游插件内未见对应实现
// ============================================================================
void SaveEditorReloadState(const FString& OutputDir)
{
	FCSVWriter Writer;
	Writer.AddHeader({
		TEXT("Category"),
		TEXT("OldName"),
		TEXT("NewName")
	});

	FClassReloadHelper::FReloadState& ReloadState = FClassReloadHelper::ReloadState();
	for (const TPair<UClass*, UClass*>& ReloadClass : ReloadState.ReloadClasses)
	{
		Writer.AddRow({ TEXT("ReloadClass"), GetObjectName(ReloadClass.Key), GetObjectName(ReloadClass.Value) });
	}
	...
	const FString Filename = FPaths::Combine(OutputDir, TEXT("EditorReloadState.csv"));
	...
}

void RegisterStateDumpExtension(FDelegateHandle& OutHandle)
{
	if (!OutHandle.IsValid())
	{
		// ★ 复用统一 state dump 扩展点，把 reload state 外部化
		OutHandle = FAngelscriptStateDump::OnDumpExtensions.AddStatic(&DumpEditorState);
	}
}

void UnregisterStateDumpExtension(FDelegateHandle& InOutHandle)
{
	if (InOutHandle.IsValid())
	{
		FAngelscriptStateDump::OnDumpExtensions.Remove(InOutHandle);
		InOutHandle.Reset();
	}
}
```

因此，这一维新增后的判断不是“当前插件热重载更多”，而是：  
1. interface reload 的 editor cache 失效条件更保守，属于**实现质量差异**。  
2. reload state dump 是**新增能力**，它补的是可诊断性，不是 reload 主链本身。  

### [维度 D8/D11] UEAS2 的引擎补丁不只是“让 UHT 认识 Angelscript”，还把 native call metadata 直接注入 `.gen.cpp`；当前插件则把这条链改造成插件内 `UHTTool + header parser`

```
[D8/D11-Deep] Native Call Metadata Source
Hazelight patched engine
├─ EpicGames.UHT codegen
│  └─ emits ASFunctionPointers per native UFunction
├─ CoreUObject
│  └─ extends UE::CodeGen::FClassNativeFunction
└─ runtime
   └─ consumes generated type-erased native pointers directly

AngelPortV2 stock-engine path
├─ AngelscriptUHTTool.ubtplugin
│  └─ ships inside plugin Binaries/DotNET/UnrealBuildTool/Plugins
├─ AngelscriptHeaderSignatureResolver
│  └─ strips defaults / names from public headers
├─ AngelscriptFunctionSignatureBuilder
│  └─ builds ERASE_* macros
└─ AS_FunctionTable_*.cpp
   └─ registers generated BlueprintCallable entries
```

前文已经说明 `ScriptCallable` 与 `UASClass` 这两条“去引擎补丁化”主线。本轮补充的更底层结论是：**UEAS2 的 UHT 修改还在 generated code 层直接生产 native method metadata；当前插件没有这层引擎产物，于是把同类信息重建挪到了插件自带的 UBT/UHT exporter。**

[1] UEAS2 在 UHT 代码生成阶段就为每个 `FClassNativeFunction` 填入 `ASFunctionPointers`，而 `CoreUObject` 也被补丁扩展出了对应字段：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Programs\Shared\EpicGames.UHT\Exporters\CodeGen\UhtHeaderCodeGeneratorCppFile.cs
// 位置: 2591-2610, 2778-2790
// 说明: 上游不是在插件里“猜” native 指针，而是让 UHT 直接把 type-erased 指针生成进 .gen.cpp
// ============================================================================
private static string GetASFunctionPointers(UhtClass classObj, UhtFunction function)
{
	bool bBindMethodPtr = true;
	...
	// Custom thunks not supported for export
	if (function.FunctionExportFlags.HasFlag(UhtFunctionExportFlags.CustomThunk))
		bBindMethodPtr = false;
	...
}

builder.Append("\tstatic constexpr UE::CodeGen::FClassNativeFunction Funcs[] = {\r\n");
...
	.Append($"::exec{function.EngineName}")
	.Append($", .ASFunctionPointers = {GetASFunctionPointers(classObj, function)} }},\r\n");
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Runtime\CoreUObject\Public\UObject\UObjectGlobals.h
// 位置: 4301-4317
// 说明: 这意味着 AS metadata 已经变成 CoreUObject codegen contract 的一部分
// ============================================================================
namespace ASAutoCaller
{
	using TFunctionPtr = void(*)();
	using FunctionCallerPtr = void(*)(TFunctionPtr FunctionPtr, void** Parameters, void* ReturnValue);

	struct FReflectedFunctionPointers
	{
		void* FunctionPointerOrRetriever;
		FunctionCallerPtr CallerPtr;
	};
}

namespace UE::CodeGen
{
```

[2] 当前插件的替代路径是把 exporter 作为插件内 `ubtplugin` 发货，再从 header 文本重建 direct bind 所需签名：

```xml
<!-- =========================================================================
文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptUHTTool.ubtplugin.csproj
位置: 12-14
说明: 当前插件把 UHT 扩展做成随插件发货的 UBT 插件，而不是要求替换 Engine/Programs
============================================================================ -->
<RootNamespace>AngelscriptUHTTool</RootNamespace>
<AssemblyName>AngelscriptUHTTool</AssemblyName>
<OutputPath>..\..\Binaries\DotNET\UnrealBuildTool\Plugins\AngelscriptUHTTool\</OutputPath>
```

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs
// 位置: 43-60
// 说明: direct bind 先尝试 header-based signature reconstruction；失败则显式降级
// ============================================================================
public static bool TryBuild(UhtClass classObj, UhtFunction function, out AngelscriptFunctionSignature? signature, out string? failureReason)
{
	signature = null;

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
		return false;
	}
```

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs
// 位置: 554-570
// 说明: 当前插件会从头文件文本里去掉默认值和参数名，只保留 direct bind 需要的类型骨架
// ============================================================================
List<string> rawParameters = SplitTopLevel(parameterSegment);
int parameterIndex = 0;
foreach (string rawParameter in rawParameters)
{
	string parameter = StripDefaultValue(StripLeadingUparam(rawParameter.Trim()));
	...
	if (property.ArrayDimensions != null)
	{
		return new List<string>();
	}

	results.Add(StripTrailingIdentifier(parameter));
	parameterIndex++;
}
```

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs
// 位置: 302-312, 465-479
// 说明: 重建出的签名会落成 AS_FunctionTable_*.cpp，而不是写回 Engine 的 .gen.cpp contract
// ============================================================================
builder.Append("AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_AS_FunctionTable_")
	.Append(moduleShortName)
	.Append('_')
	.Append(shardIndex.ToString("D3"))
	.AppendLine("((int32)FAngelscriptBinds::EOrder::Late + 50, []()");
...
for (int entryIndex = startIndex; entryIndex < startIndex + entryCount; entryIndex++)
{
	builder.AppendLine(entries[entryIndex].BuildRegistrationLine());
}

string eraseMacro;
if (classObj.ClassType == UhtClassType.Interface || classObj.ClassType == UhtClassType.NativeInterface)
{
	eraseMacro = "ERASE_NO_FUNCTION()";
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

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 位置: 728-730
// 说明: 当前工程自己也把旧 editor-side bind generator 明确降成了 debug-only 辅助路径
// ============================================================================
"ASGenerateBindings",
NSLOCTEXT("Angelscript", "GenerateBind.Label", "Legacy Native Bind Generator (Debug Only)"),
NSLOCTEXT("Angelscript", "GenerateBind.ToolTip", "Legacy editor-side generator retained only for debugging old FunctionCallers output. The UHT-based AngelscriptUHTTool pipeline is the primary path."),
```

这说明 UEAS2 那批 UHT / `CoreUObject` 补丁真正换来的东西是：**native method metadata 可以作为 engine codegen contract 被稳定生产**。当前插件并没有“没有实现这条能力”，而是改成了**插件内重建**：

| 子问题 | 结论 | 依据 |
| --- | --- | --- |
| native method metadata 是否仍由 engine `.gen.cpp` 直接产出 | **不是** | 当前改为 `AngelscriptUHTTool + AS_FunctionTable_*.cpp` |
| stock engine 下是否还能做大面积 direct bind | **可以** | `HeaderSignatureResolver + BuildEraseMacro` 仍能重建大量公开函数 |
| direct bind 覆盖边界是否与 UEAS2 完全等价 | **不等价** | `non-public` / `unexported-symbol` / `overloaded-unresolved` 会显式降级 |
| 这属于“没有实现”吗 | **不是** | 更准确是“实现方式不同，且 direct-bind 上界更依赖 public/exported 头文件形态” |

因此，`D8/D11` 本轮最值得记住的新结论是：**UEAS2 把一部分绑定元数据生产权下沉到了引擎 codegen；当前插件则把这部分生产权重新抬回插件构建链。它换来了 stock-engine 可部署性，但代价是 direct bind 的确定性不再由引擎内部 AST/生成流程兜底，而要靠 header 可见性、签名重建与更强的测试护栏来保证。**

---

## 深化分析 (2026-04-08 19:18:14)

### [维度 D6] UEAS2 的 IDE/代码生成支持更偏“可浏览产物”，当前插件把主可见面前移成“UHT 生成诊断产物 + 回归校验”

```
[D6-Deep] Developer-Facing Artifacts
Hazelight
├─ Binds.Cache + Binds.Cache.Headers             // 绑定名与头文件路径缓存
├─ DumpDocumentation()                          // 生成 Docs/angelscript/generated/*.hpp
└─ SourceCodeNavigation handler                 // 编辑器内跳转到 .as / header

AngelPortV2
├─ AS_FunctionTable_*.cpp                       // 真实绑定分片
├─ AS_FunctionTable_Summary.json                // 总量与 direct/stub 比例
├─ AS_FunctionTable_ModuleSummary.csv           // 按模块覆盖率
├─ AS_FunctionTable_SkippedReasonSummary.csv    // 降级原因聚合
└─ tests lock summary / csv / source path       // 产物和导航都进入自动化
```

前文已经把 `AngelscriptUHTTool` 的 direct-bind 主链讲清了；这一轮从 `D6` 往下钻的重点，是“开发者第一手能看到什么”。UEAS2 当前快照对 IDE/代码生成最明确的可见产物，仍然是 `Binds.Cache(.Headers)` 与 `Docs/angelscript/generated/*.hpp`。前者解决 bind 名与 header 定位，后者生成一套便于浏览的伪声明头文件。它们都更像“编辑器便利层”。

当前插件当然没有把这些 inherited surface 全砍掉，但它新增的主可见面已经明显变了：`AS_FunctionTable_Summary.json`、`ModuleSummary.csv`、`Entries.csv`、`SkippedEntries.csv`、`SkippedReasonSummary.csv` 这组 UHT exporter 产物，把“哪些函数 direct bind 成功、哪些只能 stub、为什么失败”直接变成了构建期可审计数据；再加上自动化测试锁定这些文件内容与计数关系，`D6` 的重心已经从“让人能跳过去看”推进成“让人能判断生成链到底覆盖到了什么程度”。

[1] UEAS2 的绑定数据库会单独落 `Binds.Cache.Headers`，把 `UnrealPath -> HeaderPath` 序列化出来，服务静态跳转与后续 JIT/工具链：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\AngelscriptBindDatabase.cpp
// 函数: FAngelscriptBindDatabase::Save / Load
// 位置: 31-95, 132-149
// 说明: UEAS2 把绑定数据库和头文件映射缓存为显式磁盘产物
// ============================================================================
void FAngelscriptBindDatabase::Save(const FString& Path)
{
	...
#if WITH_EDITOR
	{
		TArray<uint8> HeaderData;
		FMemoryWriter Writer(HeaderData);

		TArray<FAngelscriptClassHeader> Headers;
		for (auto& Bind : Classes)
		{
			UClass* Class = FindObject<UClass>(nullptr, *Bind.UnrealPath);
			FString HeaderPath;
			if (FSourceCodeNavigation::FindClassHeaderPath(Class, HeaderPath) && IFileManager::Get().FileSize(*HeaderPath) != INDEX_NONE)
				Headers.Add(FAngelscriptClassHeader{Bind.UnrealPath, HeaderPath});
		}
		...
		// ★ editor 阶段把 header 映射额外写入 .Headers 文件
		bool bSaveSuccess = FFileHelper::SaveArrayToFile(HeaderData, *(Path + TEXT(".Headers")));
		...
	}
#endif
}

#if AS_CAN_GENERATE_JIT
if (bGeneratingPrecompiledData)
{
	...
	for (const auto& Header : Headers)
	{
		UObject* Field = FindObject<UObject>(nullptr, *Header.UnrealPath);
		if (Field == nullptr)
			continue;
		// ★ 非 editor 侧回读缓存的头文件路径
		HeaderLinks.Add(Field, Header.Header);
	}
}
#endif
```

[2] UEAS2 的 `DumpDocumentation()` 直接生成 `Docs/angelscript/generated/*.hpp`，这是一种“可浏览声明镜像”而不是“覆盖率诊断面”：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\AngelscriptDocs.cpp
// 函数: FAngelscriptDocs::DumpDocumentation
// 位置: 400-424, 668-749
// 说明: 上游把脚本文档导出成按类拆分的 .hpp 伪声明文件
// ============================================================================
void FAngelscriptDocs::DumpDocumentation(asIScriptEngine* Engine)
{
#if WITH_EDITOR
	TMap<FString, FDocClass> Classes;
	...
	auto GetDecl = [&](int TypeId, asDWORD* Flags = nullptr) -> FString
	{
		...
		// ★ 先把 script type 转成可打印声明
		const char* DeclRaw = ScriptEngine->GetTypeDeclaration(TypeId);
		FString Decl = ANSI_TO_TCHAR(DeclRaw);
		...
		return Decl;
	};
	...
	for (auto It : Classes)
	{
		FDocClass& ClassDoc = It.Value;
		...
		FString Filename = FPaths::ProjectDir() / TEXT("/Docs/angelscript/generated") / ClassDoc.ClassName + TEXT(".hpp");
		...
		Content += FString::Printf(TEXT("/* Class: %s \n %s */ \n class %s"),
			*ClassDoc.ClassName, *ClassDoc.Documentation, *ClassDoc.ClassName);
		...
		// ★ 每个类型都被导出成可浏览的头文件替身
		FFileHelper::SaveStringToFile(Content, *Filename);
	}
#endif
}
```

[3] 当前插件把主可见面改成了“生成摘要 + 覆盖率 CSV + skipped reason 聚合”，直接告诉开发者 exporter 的真实覆盖边界：

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs
// 函数: WriteCoverageDiagnostics / WriteGenerationSummary
// 位置: 142-206
// 说明: 当前插件把 direct/stub 统计正式落成 JSON/CSV，而不是只在 editor 内部可见
// ============================================================================
private static void WriteCoverageDiagnostics(List<AngelscriptModuleGenerationSummary> moduleSummaries)
{
	moduleSummaries.Sort(static (left, right) =>
	{
		int stubComparison = right.StubEntries.CompareTo(left.StubEntries);
		return stubComparison != 0
			? stubComparison
			: StringComparer.Ordinal.Compare(left.ModuleName, right.ModuleName);
	});

	Console.WriteLine("AngelscriptUHTTool per-module coverage diagnostics:");
	foreach (AngelscriptModuleGenerationSummary summary in moduleSummaries)
	{
		// ★ 直接把每个模块的 total/direct/stub/shard 暴露出来
		Console.WriteLine(
			"  - {0}{1}: total={2}, direct={3}, stubs={4}, shards={5}",
			summary.ModuleName,
			summary.EditorOnly ? " [EditorOnly]" : string.Empty,
			summary.TotalEntries,
			summary.DirectBindEntries,
			summary.StubEntries,
			summary.ShardCount);
	}
}

private static void WriteGenerationSummary(IUhtExportFactory factory, List<AngelscriptModuleGenerationSummary> moduleSummaries, List<AngelscriptGeneratedFunctionCsvEntry> csvEntries, int generatedFileCount)
{
	...
	string summaryPath = factory.MakePath("AS_FunctionTable_Summary", ".json");
	...
	File.WriteAllText(summaryPath, summaryJson, Encoding.UTF8);
	WriteModuleSummaryCsv(factory, moduleSummaries);
	WriteEntryCsv(factory, csvEntries);
	// ★ JSON + CSV 同时落盘，构建结束后立即可审计
}
```

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs
// 函数: WriteSkippedEntriesCsv / WriteSkippedReasonSummaryCsv
// 位置: 99-161
// 说明: direct-bind 失败不再只是“静默降级”，而是显式聚合失败原因
// ============================================================================
private static void WriteSkippedEntriesCsv(IUhtExportFactory factory, List<AngelscriptSkippedFunctionEntry> skippedEntries)
{
	string csvPath = factory.MakePath("AS_FunctionTable_SkippedEntries", ".csv");
	...
	builder.AppendLine("ModuleName,ClassName,FunctionName,FailureReason");
	...
	File.WriteAllText(csvPath, builder.ToString(), Encoding.UTF8);
	// ★ 每个 skipped function 都被记录
}

private static void WriteSkippedReasonSummaryCsv(IUhtExportFactory factory, List<AngelscriptSkippedFunctionEntry> skippedEntries)
{
	string csvPath = factory.MakePath("AS_FunctionTable_SkippedReasonSummary", ".csv");
	...
	builder.AppendLine("FailureReason,SkippedCount");
	foreach (var reasonGroup in skippedEntries
		.GroupBy(static entry => entry.FailureReason, StringComparer.Ordinal)
		.OrderByDescending(static group => group.Count()))
	{
		// ★ 同类失败原因被聚合，便于判断 direct-bind 上界受什么约束
		builder
			.Append(EscapeCsv(reasonGroup.Key))
			.Append(',')
			.Append(reasonGroup.Count())
			.Append("\r\n");
	}
	...
}
```

[4] 当前插件不仅生成这些产物，还用自动化测试锁死“摘要计数”和“生成函数源码定位”：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp
// 函数: FAngelscriptGeneratedFunctionTableSummaryOutputTest::RunTest
// 位置: 459-527, 706-749
// 说明: summary/csv 不是辅助日志，而是正式回归契约
// ============================================================================
const FString SummaryPath = FPaths::Combine(GeneratedDirectory, TEXT("AS_FunctionTable_Summary.json"));
...
if (!TestTrue(TEXT("Generated function table summary test should expose totalGeneratedEntries"), SummaryObject->TryGetNumberField(TEXT("totalGeneratedEntries"), TotalGeneratedEntries)))
{
	return false;
}
...
// ★ JSON 里的总量必须与实际注册行数对齐
TestEqual(TEXT("Generated function table summary test should match the generated binding registration count"), TotalGeneratedEntries, CountedRegistrations);
...
const FString ReasonSummaryCsvPath = FPaths::Combine(GeneratedDirectory, TEXT("AS_FunctionTable_SkippedReasonSummary.csv"));
...
// ★ skipped reason summary 的聚合计数还必须和原始 skipped csv 对齐
TestEqual(TEXT("Generated function table skipped reason summary test should keep aggregate counts aligned with the skipped entry csv"), SummedSkippedCount, SkippedLines.Num() - 1);
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSourceNavigationTests.cpp
// 函数: FAngelscriptFunctionSourceNavigationTest::RunTest
// 位置: 57-80
// 说明: 生成函数的 source file / line 已被纳入 IDE 跳转回归测试
// ============================================================================
UClass* RuntimeClass = FindGeneratedClass(&Engine, TEXT("UFunctionNavigationCarrier"));
...
UASFunction* RuntimeASFunction = Cast<UASFunction>(RuntimeFunction);
...
TestEqual(TEXT("Generated function should preserve source file path"), RuntimeASFunction->GetSourceFilePath(), ScriptPath);
TestEqual(TEXT("Generated function should preserve source line number"), RuntimeASFunction->GetSourceLineNumber(), 6);
// ★ IDE 导航不再只靠人工点开验证
TestTrue(TEXT("Source navigation should recognize generated script class"), FSourceCodeNavigation::CanNavigateToClass(RuntimeClass));
TestTrue(TEXT("Source navigation should recognize generated script function"), FSourceCodeNavigation::CanNavigateToFunction(RuntimeFunction));
```

### 差距判断

| 子问题 | 结论 | 依据 |
| --- | --- | --- |
| UEAS2 是否完全没有代码生成/IDE 支持 | **不是** | `Binds.Cache.Headers` 与 `Docs/angelscript/generated/*.hpp` 已构成显式 IDE 产物 |
| 当前插件是否只是“同样能生成表”，没有新增开发可见面 | **不是** | `AS_FunctionTable_Summary.json`、多张 CSV、`SkippedReasonSummary` 把覆盖边界前移到构建期 |
| 当前插件的 source navigation 是否只是沿用旧逻辑 | **不止是沿用** | 生成函数的 `source path / line` 已被测试锁定 |
| 这属于“没有实现”还是“实现方式不同” | **实现方式不同，且当前插件新增了 exporter telemetry** | UEAS2 偏浏览/跳转产物，当前插件偏覆盖率/降级原因产物 |

因此，`D6` 本轮最重要的新结论是：**UEAS2 更像在给 IDE 喂一套“可浏览的声明影子”；当前插件则把构建链本身变成了可诊断对象。它不只是让人能跳转，而是让人能回答“为什么这个函数没有 direct bind 成功”。**

### [维度 D7] 当前插件把 editor integration 从“交互式入口”扩成了“交互式入口 + headless commandlet + 可导出 editor state”

```
[D7-Deep] Editor Automation Surface
Shared baseline
├─ ContentBrowser data source
└─ ToolMenus entry point

Hazelight
└─ interactive editor callbacks
   ├─ create blueprint / asset picker
   └─ VS Code / menu commands

AngelPortV2
├─ same editor chrome baseline
├─ ScriptEditorMenuExtension registry
├─ BlueprintImpactScanCommandlet          // 命令行批量分析蓝图影响
├─ StateDump extension                    // 导出 editor reload/menu 状态
└─ tests for dump + commandlet contracts
```

如果只看 `OnEngineInitDone()` 和 `RegisterToolsMenuEntries()`，两边的 editor 集成基线其实非常接近：都注册内容浏览器数据源，也都往 `ToolMenus` 里挂入口。真正的新变化不在“有没有编辑器菜单”，而在**当前插件把 editor 信息面做成了 headless automation surface**。

更具体地说，当前插件在 `AngelscriptEditor` 里新增了两条 UEAS2 当前快照里看不到的正式出口：

1. `BlueprintImpactScanCommandlet`：可以在命令行下基于 changed scripts 扫描受影响蓝图，返回结构化统计和 exit code。  
2. `AngelscriptEditorStateDump`：把 editor reload state 与注册过的 script menu extension 直接落成 CSV，并挂到统一 `StateDump` 扩展点上。

这意味着 `D7` 的重点已经不只是“编辑器里能点什么”，而是“CI/命令行/问题复盘能不能拿到 editor 侧证据”。

[1] UEAS2 的 editor 启动基线仍然主要是内容浏览器数据源和工具菜单回调：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptEditor\Private\AngelscriptEditorModule.cpp
// 函数: OnEngineInitDone / FAngelscriptEditorModule::StartupModule
// 位置: 133-140, 433-435
// 说明: 上游 editor 模块的显式注册点主要仍是 UI/data source
// ============================================================================
void OnEngineInitDone()
{
	// Register the content browser data source
	auto* DataSource = NewObject<UAngelscriptContentBrowserDataSource>(GetTransientPackage(), "AngelscriptData", RF_MarkAsRootSet | RF_Transient);
	DataSource->Initialize();

	UContentBrowserDataSubsystem* ContentBrowserData = IContentBrowserDataModule::Get().GetSubsystem();
	ContentBrowserData->ActivateDataSource("AngelscriptData");
}

...
// ★ 编辑器准备好后再注册 UI extension
UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FAngelscriptEditorModule::RegisterToolsMenuEntries));
```

[2] 当前插件保留这条基线，但在 `StartupModule()` 里额外挂了 menu-extension registry 和 state dump extension：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 函数: OnEngineInitDone / FAngelscriptEditorModule::StartupModule
// 位置: 111-118, 351-365, 414-415
// 说明: 当前插件把 editor 注册点从纯 UI 扩成 UI + state dump + extension registry
// ============================================================================
void OnEngineInitDone()
{
	// Register the content browser data source
	auto* DataSource = NewObject<UAngelscriptContentBrowserDataSource>(GetTransientPackage(), "AngelscriptData", RF_MarkAsRootSet | RF_Transient);
	DataSource->Initialize();

	UContentBrowserDataSubsystem* ContentBrowserData = IContentBrowserDataModule::Get().GetSubsystem();
	ContentBrowserData->ActivateDataSource("AngelscriptData");
}

void FAngelscriptEditorModule::StartupModule()
{
	FClassReloadHelper::Init();
	RegisterAngelscriptSourceNavigation();
	...
	UScriptEditorMenuExtension::InitializeExtensions();
	AngelscriptEditor::Private::RegisterStateDumpExtension(StateDumpExtensionHandle);
	...
	// ★ 仍然保留 ToolMenus 基线，但现在不是唯一 editor 出口
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FAngelscriptEditorModule::RegisterToolsMenuEntries));
}
```

[3] `BlueprintImpactScanCommandlet` 把“变更 script 会影响哪些蓝图”正式做成了命令行接口，而不是只能在 editor 手工判断：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/BlueprintImpact/AngelscriptBlueprintImpactScanCommandlet.cpp
// 函数: UAngelscriptBlueprintImpactScanCommandlet::Main
// 位置: 55-120
// 说明: 当前插件新增了 headless blueprint impact 分析面
// ============================================================================
int32 UAngelscriptBlueprintImpactScanCommandlet::Main(const FString& Params)
{
	if (!FAngelscriptEngine::Get().bDidInitialCompileSucceed)
	{
		UE_LOG(Angelscript, Error, TEXT("Blueprint impact commandlet requires a successfully initialized Angelscript engine."));
		return static_cast<int32>(EBlueprintImpactCommandletExitCode::EngineNotReady);
	}
	...
	const AngelscriptEditor::BlueprintImpact::FBlueprintImpactScanResult ScanResult = AngelscriptEditor::BlueprintImpact::ScanBlueprintAssets(
		FAngelscriptEngine::Get(),
		AssetRegistryModule.Get(),
		Request);

	UE_LOG(
		Angelscript,
		Display,
		TEXT("{ \"BlueprintImpact\": { \"FullScan\": %s, \"ChangedScripts\": %d, \"MatchingModules\": %d, \"Classes\": %d, \"Structs\": %d, \"Enums\": %d, \"Delegates\": %d, \"CandidateAssets\": %d, \"Matches\": %d, \"FailedAssetLoads\": %d } }"),
		...
		ScanResult.FailedAssetLoads);
	...
	// ★ 用 exit code 区分成功、参数错误和 asset scan failure
	return ScanResult.FailedAssetLoads > 0
		? static_cast<int32>(EBlueprintImpactCommandletExitCode::AssetScanFailure)
		: static_cast<int32>(EBlueprintImpactCommandletExitCode::Success);
}
```

[4] `StateDump` 扩展会直接导出 editor reload/menu extension 状态，而且这些文件名本身也被端到端测试锁死：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorStateDump.cpp
// 位置: 21-28, 62-67, 78-118
// 说明: 当前插件把 editor 侧状态接进统一 dump pipeline
// ============================================================================
void SaveEditorReloadState(const FString& OutputDir)
{
	FCSVWriter Writer;
	Writer.AddHeader({
		TEXT("Category"),
		TEXT("OldName"),
		TEXT("NewName")
	});
	...
	const FString Filename = FPaths::Combine(OutputDir, TEXT("EditorReloadState.csv"));
	...
}

void SaveEditorMenuExtensions(const FString& OutputDir)
{
	FCSVWriter Writer;
	Writer.AddHeader({
		TEXT("ExtensionPoint"),
		TEXT("Location"),
		TEXT("SectionName")
	});
	...
	const FString Filename = FPaths::Combine(OutputDir, TEXT("EditorMenuExtensions.csv"));
	...
}

void RegisterStateDumpExtension(FDelegateHandle& OutHandle)
{
	if (!OutHandle.IsValid())
	{
		// ★ editor dump 被作为 runtime state dump 的扩展点挂入
		OutHandle = FAngelscriptStateDump::OnDumpExtensions.AddStatic(&DumpEditorState);
	}
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Dump/AngelscriptDumpTests.cpp
// 函数: GetExpectedPhaseOneCsvFiles / FAngelscriptStateDumpEndToEndTest::RunTest / FAngelscriptStateDumpSummaryTest::RunTest
// 位置: 45-75, 202-249
// 说明: dump 产物名和 summary 状态被测试固定，不是临时调试脚本
// ============================================================================
TArray<FString> GetExpectedPhaseOneCsvFiles()
{
	return {
		...
		TEXT("EditorReloadState.csv"),
		TEXT("EditorMenuExtensions.csv"),
		TEXT("DumpSummary.csv")
	};
}

bool FAngelscriptStateDumpEndToEndTest::RunTest(const FString& Parameters)
{
	...
	for (const FString& ExpectedFilename : GetExpectedPhaseOneCsvFiles())
	{
		const FString CsvPath = FPaths::Combine(OutputDir, ExpectedFilename);
		// ★ 每张 CSV 都必须真实落盘
		TestTrue(*FString::Printf(TEXT("DumpAll should create '%s'"), *ExpectedFilename), IFileManager::Get().FileExists(*CsvPath));
	}
	...
}

bool FAngelscriptStateDumpSummaryTest::RunTest(const FString& Parameters)
{
	...
	// ★ summary 还必须覆盖所有期望文件，并报告各自状态
	TestEqual(
		*FString::Printf(TEXT("'%s' should report the expected summary status"), *ExpectedFilename),
		SummaryRow->Value,
		GetExpectedSummaryStatus(ExpectedFilename));
	...
}
```

### 差距判断

| 子问题 | 结论 | 依据 |
| --- | --- | --- |
| editor 内容浏览器 / 工具菜单能力是否缺失 | **没有缺失** | 两边都注册 `ContentBrowserDataSource` 与 `ToolMenus` |
| 当前插件是否只是在 UEAS2 基线上小修小补 | **不是** | 新增 `BlueprintImpactScanCommandlet` 与 `StateDump` 扩展 |
| 这些新增能力是“实现方式不同”还是“新能力” | **新能力** | 它们把 editor 信息面扩展到了命令行和离线 dump |
| 这些 editor 自动化面是否只是实验性辅助脚本 | **不是** | `AngelscriptDumpTests.cpp` 与 `AngelscriptBlueprintImpactScannerTests.cpp` 已锁定产物和 exit code 契约 |

因此，`D7` 本轮最值得记住的新结论是：**当前插件并不是单纯“编辑器集成更多”，而是把 editor state 和 blueprint impact 变成了可批处理、可落盘、可回归验证的 automation surface。UEAS2 更偏 editor 内部工作流，当前插件则开始把 editor 侧信息产品化。**

### [维度 D9] UEAS2 不是“没有测试”，但当前插件已经把测试从 runtime 内嵌用法推进成“两层测试体系 + 专用夹具 + 文档化约定”

```
[D9-Deep] Test Layering
Hazelight
└─ AngelscriptCode module
   ├─ Private/Testing/*.cpp              // Unit / Integration / discover
   ├─ Private/Tests/*.cpp                // 少量 C++ 覆盖率回归
   └─ runtime owns everything            // 没有独立 test binary

AngelPortV2
├─ AngelscriptRuntime/Testing + /Tests   // 低层协议、coverage、多引擎、自举
└─ AngelscriptTest module
   ├─ engine macros + guide
   ├─ debugger session/client fixtures
   ├─ dump / blueprint impact / examples
   └─ editor-only deps for scenario tests
```

这个维度最容易被说成“上游测试少、当前测试多”，但源码层面更准确的说法是：**UEAS2 已经有完整自动化测试入口，只是它们主要嵌在 `AngelscriptCode` 模块内部；当前插件则在保留这层 low-level self-test 的同时，再额外拉出 `AngelscriptTest` 作为独立 scenario/regression binary。**

也就是说，`D9` 的真实差异不是“有没有测试框架”，而是“测试边界有没有被产品化”。这在当前插件里至少体现在三件事上：

1. low-level `CppTests` 仍保留在 `AngelscriptRuntime/Tests`。  
2. `AngelscriptTest` 单独声明编译边界和 editor-only 依赖。  
3. guide / 宏 / debugger session fixture 已经把“怎么写隔离测试”正式文档化。

顺带补一个目录粗扫结果：按 `Source/**/*.cpp` 名称包含 `Test` 的粗口径扫描，UEAS2 当前快照是 `7` 个 C++ 测试/测试支撑实现，当前插件是 `169` 个。这个统计不等同于全部有效 case 数，但足以说明当前插件已经把 test surface 独立扩成一个明显更大的子产品。

[1] UEAS2 的测试仍然主要附着在 `AngelscriptCode` 运行时模块内部，`Build.cs` 本身没有独立 test binary 的边界：

```csharp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\AngelscriptCode.Build.cs
// 位置: 12-64
// 说明: 上游把运行时、editor 依赖和测试承载在同一个 Code 模块里
// ============================================================================
public class AngelscriptCode : ModuleRules
{
	public AngelscriptCode(ReadOnlyTargetRules Target) : base(Target)
	{
		/* Link to libraries used in core angelscript code */
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"ApplicationCore",
			"Core",
			"CoreUObject",
			"Engine",
			...
		});

		/* Link to libraries used in bindings */
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"AIModule",
			...
			"UMG",
			"TraceLog",
			"AssetRegistry",
			...
		});

		if (Target.bBuildEditor)
		{
			PublicDependencyModuleNames.AddRange(new string[]
			{
				"UnrealEd",
				"EditorSubsystem",
			});
		}
	}
}
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Testing\IntegrationTest.cpp
// 位置: 780-805
// 说明: 上游的 integration test runner 仍然定义在 Code 模块内部
// ============================================================================
#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_COMPLEX_AUTOMATION_TEST(
	FAngelscriptIntegrationTests, "Angelscript.IntegrationTests", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter);

bool LookupIntegrationTest(const FString& ModuleAndTestName, TSharedPtr<struct FAngelscriptModuleDesc>* OutModule,
	FAngelscriptTestDesc* OutTestFuncDesc, FString* OutMapObjectPath)
{
	...
	FAngelscriptManager* AngelscriptManager = &FAngelscriptManager::Get();
	*OutModule = AngelscriptManager->GetModule(ModuleName);
	...
}
```

[2] UEAS2 确实也有 C++ 层 coverage regression，但它们仍是 runtime 内嵌测试，而不是独立 scenario 层：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Tests\AngelscriptCodeCoverageTests.cpp
// 函数: FAngelscriptCodeCoverageTests0::RunTest
// 位置: 12-84
// 说明: 上游已经有覆盖率回归，但入口仍在 Code 模块内部
// ============================================================================
#if WITH_DEV_AUTOMATION_TESTS && WITH_AS_COVERAGE

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAngelscriptCodeCoverageTests0,
	"Angelscript.CppTests.AngelscriptCodeCoverage.IntegrationTest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter);

bool FAngelscriptCodeCoverageTests0::RunTest(const FString& Parameters)
{
	FAngelscriptManager& Manager = FAngelscriptManager::Get();
	...
	// ★ 直接拿真实 active modules 做 coverage 验证
	for (TSharedRef<struct FAngelscriptModuleDesc>& Module : Manager.GetActiveModules())
	{
		Coverage.MapExecutableLines(*Module);
	}
	...
	Coverage.StopRecordingAndWriteReport(TempDir);
	...
}
```

[3] 当前插件保留了这层 runtime self-test，但又单独拉出了 `AngelscriptTest` 模块承载高层场景：

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs
// 位置: 12-50
// 说明: 当前插件把 scenario/regression test 独立成单独模块，并显式声明 editor-only 依赖
// ============================================================================
// Module root + subdirectories mirroring AngelscriptRuntime layout
PublicIncludePaths.Add(ModuleDirectory);
PrivateIncludePaths.Add(ModuleDirectory);
PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Core"));
PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Debugger"));
PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Dump"));
...

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

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp
// 位置: 39-77, 79-118
// 说明: 当前仓库并没有把低层 runtime 测试迁空；它额外叠加了 Test module
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptDebugProtocolStartDebuggingRoundTripTest,
	"Angelscript.CppTests.Debug.Protocol.StartDebugging.RoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
...
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptDebugProtocolDatabaseSettingsRoundTripTest,
	"Angelscript.CppTests.Debug.Protocol.DatabaseSettings.RoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptDebugProtocolStartDebuggingRoundTripTest::RunTest(const FString& Parameters)
{
	FStartDebuggingMessage Message;
	Message.DebugAdapterVersion = 2;
	...
	// ★ wire format round-trip 仍在 runtime 层被锁定
	TestEqual(TEXT("Debug.Protocol.StartDebugging.RoundTrip should preserve the adapter version"), RoundTripped.DebugAdapterVersion, 2);
	...
}
```

[4] `AngelscriptTest` 不只是“多一个目录”，而是把测试引擎生命周期、fixture 选择和 live debugger session 都文档化/模板化了：

```markdown
<!-- =========================================================================
文件: Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md
位置: 5-18, 237-254
说明: 当前插件把测试生命周期和 fixture 选择写成了正式规范
============================================================================ -->
This project uses a two-layer macro system defined in `Shared/AngelscriptTestMacros.h` to reduce test boilerplate.
...
| `ASTEST_CREATE_ENGINE_FULL()` | `FAngelscriptEngine&` | Fresh isolated Full engine. Full bind environment, supports hot-reload testing. |
| `ASTEST_CREATE_ENGINE_CLONE()` | `FAngelscriptEngine&` | Lightweight isolation. Shares source engine read-only state (bindings, types). |
...
The following scenarios should use `IMPLEMENT_SIMPLE_AUTOMATION_TEST` directly without the `ASTEST_*` macros:
...
5. **Debugger session tests** - Tests under `Debugger/` that attach to a running debug server and need socket/session fixtures
...
- **Debugger session helpers**: `Shared/AngelscriptDebuggerTestSession.h`
- **Debugger client helpers**: `Shared/AngelscriptDebuggerTestClient.h`
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSmokeTests.cpp
// 函数: FAngelscriptDebuggerSmokeHandshakeTest::RunTest
// 位置: 16-33, 49-88
// 说明: 当前插件已经把 live debugger session 当成正式自动化回归对象
// ============================================================================
bool FAngelscriptDebuggerSmokeHandshakeTest::RunTest(const FString& Parameters)
{
	FAngelscriptDebuggerTestSession Session;
	FAngelscriptDebuggerSessionConfig SessionConfig;
	SessionConfig.ExistingEngine = TryGetRunningProductionDebuggerEngine();
	if (!TestNotNull(TEXT("Debugger.Smoke.Handshake should find a debuggable production engine inside the editor automation process"), SessionConfig.ExistingEngine))
	{
		return false;
	}

	if (!TestTrue(TEXT("Debugger.Smoke.Handshake should initialize a debugger test session"), Session.Initialize(SessionConfig)))
	{
		return false;
	}

	FAngelscriptDebuggerTestClient Client;
	if (!TestTrue(TEXT("Debugger.Smoke.Handshake should connect a debugger test client"), Client.Connect(TEXT("127.0.0.1"), Session.GetPort())))
	{
		...
	}
	...
	if (!TestTrue(TEXT("Debugger.Smoke.Handshake should send StartDebugging"), Client.SendStartDebugging(2)))
	{
		...
	}
	...
	// ★ 当前调试协议不是只测结构体 round-trip，还测真实 socket/session 握手
	TestEqual(TEXT("Debugger.Smoke.Handshake should report the current debug server version"), DebugServerVersion->DebugServerVersion, DEBUG_SERVER_VERSION);
	TestTrue(TEXT("Debugger.Smoke.Handshake should put the session in debugging mode after StartDebugging"), Session.GetDebugServer().bIsDebugging);
}
```

### 差距判断

| 子问题 | 结论 | 依据 |
| --- | --- | --- |
| UEAS2 是否没有自动化测试 | **不是** | `IntegrationTest.cpp`、`AngelscriptCodeCoverageTests.cpp` 已说明上游有 runtime 内嵌测试 |
| 当前插件是否只是“把 UEAS2 的测试原样搬过来” | **不是** | 额外新增 `AngelscriptTest` 模块、宏指南、debugger session fixtures |
| 当前插件是否仍保留 low-level runtime self-tests | **是** | `AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp` 等仍存在 |
| 差异属于“没有实现”还是“组织/质量差异” | **组织方式不同 + 覆盖面显著扩大** | 从 runtime 内嵌测试演进为 runtime self-test + scenario test 双层结构 |

因此，`D9` 本轮最重要的新结论是：**UEAS2 已经有测试文化，但它的测试边界基本还附着在 `AngelscriptCode` 本体里；当前插件则把测试正式拆成“runtime 自校验层”和“独立回归/场景层”。这不是简单的 case 数增加，而是把测试本身做成了插件能力的一部分。**

---

## 深化分析 (2026-04-08 19:31:45)

### [维度 D1] UEAS2 的“模块边界”其实还外扩到 `AngelscriptGAS` / `AngelscriptEnhancedInput`；当前插件把它们折叠回主插件，但把 script-root 发现做成了可注入契约

```
[D1-Deep] Feature Boundary
Hazelight
├─ Angelscript
│  ├─ AngelscriptCode                       // 核心运行时
│  ├─ AngelscriptEditor                     // 编辑器扩展
│  └─ AngelscriptLoader                     // 启动转发
├─ AngelscriptGAS                           // GameplayAbilities 扩展插件
│  └─ depends on AngelscriptCode
└─ AngelscriptEnhancedInput                 // EnhancedInput 扩展插件
   └─ depends on AngelscriptCode

AngelPortV2
└─ Angelscript
   ├─ AngelscriptRuntime                    // 直接吸收 GAS / EI 绑定
   ├─ AngelscriptEditor
   ├─ AngelscriptTest
   └─ DiscoverScriptRoots()                 // 通过 DI 收集插件 Script 根
      └─ tests lock ordering / skip rules   // 根顺序、去重、缺失目录都有测试
```

这一点和前文已经写过的“Loader 被 Runtime 吸收”不是同一件事。更深一层的边界差异是：Hazelight 不只把 Angelscript 拆成三模块，它还把 `GameplayAbilities` 与 `EnhancedInput` 能力继续拆成两个独立插件；当前插件则把这些能力并回 `Angelscript` 主插件，只保留 UE 侧依赖，不保留独立插件开关。

这带来两个直接后果：

1. Hazelight 的发布边界更细，项目可按插件维度决定是否引入 GAS / EnhancedInput 绑定。  
2. 当前插件的二进制边界更粗，但为了不把“可扩展脚本根”一起丢掉，它把 script-root 发现抽成 `FAngelscriptEngineDependencies`，并且专门加了依赖注入测试锁死行为。

[1] UEAS2 的能力拆分不是文档口号，而是独立 `.uplugin + Build.cs`：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\AngelscriptGAS\AngelscriptGAS.uplugin
// 位置: 18-34
// 说明: GAS 能力不是主插件子目录，而是单独 Runtime 插件
// ============================================================================
"Modules": [
	{
		"Name": "AngelscriptGAS",
		"Type": "Runtime",
		"LoadingPhase": "Default"
	}
],
"Plugins": [
	{
		"Name": "GameplayAbilities",
		"Enabled": true
	},
	{
		"Name": "Angelscript",
		"Enabled": true
	}
]
```

```csharp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\AngelscriptGAS\Source\AngelscriptGAS.Build.cs
// 位置: 18-30
// 说明: GAS 插件直接依赖 AngelscriptCode，而不是被 AngelscriptCode 内收
// ============================================================================
PublicDependencyModuleNames.AddRange(new string[]
{
	"Core",
	"CoreUObject",
	"Engine",
	"PhysicsCore",

	"AngelscriptCode",

	"GameplayAbilities",
	"GameplayTags",
	"GameplayTasks"
});
```

```csharp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\AngelscriptEnhancedInput\Source\AngelscriptEnhancedInput.Build.cs
// 位置: 17-28
// 说明: EnhancedInput 也是同样的独立插件边界
// ============================================================================
PublicDependencyModuleNames.AddRange(new string[]
{
	"Core",
	"CoreUObject",
	"Engine",
	"InputCore",
	"Slate",

	"AngelscriptCode",

	"EnhancedInput",
});
```

[2] 当前插件把这两条能力边界折回主插件，但保留 UE 插件依赖：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Angelscript.uplugin
// 位置: 35-47
// 说明: 当前插件没有独立 AngelscriptGAS / AngelscriptEnhancedInput 插件
// ============================================================================
"Plugins": [
	{
		"Name": "StructUtils",
		"Enabled": true
	},
	{
		"Name": "EnhancedInput",
		"Enabled": true
	},
	{
		"Name": "GameplayAbilities",
		"Enabled": true
	}
]
```

[3] 两边都还支持“项目脚本根 + 插件脚本根”，但当前插件把这条链改造成可注入依赖并写了自动化测试：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\AngelscriptManager.cpp
// 函数: FAngelscriptManager::MakeAllScriptRoots
// 位置: 269-304
// 说明: UEAS2 直接从 IPluginManager 枚举启用插件的 Script 目录
// ============================================================================
TArray<FString> FAngelscriptManager::MakeAllScriptRoots(bool bOnlyProjectRoot)
{
	FString RootPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Script"));
	...
	if (!bOnlyProjectRoot)
	{
		IPluginManager& PluginManager = IPluginManager::Get();
		for (auto& Plugin : PluginManager.GetEnabledPluginsWithContent())
		{
			auto ScriptPath = FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir() / TEXT("Script"));
			if (FileManager.DirectoryExists(*ScriptPath) && ScriptPath != RootPath)
			{
				AllRootPaths.Add(ScriptPath);
			}
		}

		// ★ 仅做排序，没有可测试替换点
		AllRootPaths.Sort();
	}

	// ★ 项目根永远排第一
	AllRootPaths.Insert(RootPath, 0);
	return AllRootPaths;
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: MakeDefaultDependencies / DiscoverScriptRoots
// 位置: 558-566, 1326-1363
// 说明: 当前插件把插件脚本根发现抽成依赖项，便于测试和多引擎复用
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

TArray<FString> FAngelscriptEngine::DiscoverScriptRoots(bool bOnlyProjectRoot) const
{
	FString RootPath = Dependencies.ConvertRelativePathToFull(Dependencies.GetProjectDir() / TEXT("Script"));
	...
	if (!bOnlyProjectRoot)
	{
		for (const FString& PluginScriptRoot : Dependencies.GetEnabledPluginScriptRoots())
		{
			const FString ScriptPath = Dependencies.ConvertRelativePathToFull(PluginScriptRoot);
			if (Dependencies.DirectoryExists(ScriptPath) && ScriptPath != RootPath)
			{
				DiscoveredRootPaths.Add(ScriptPath);
			}
		}

		// ★ 根顺序仍保持确定性，但数据来源可替换
		DiscoveredRootPaths.Sort();
	}

	DiscoveredRootPaths.Insert(RootPath, 0);
	return DiscoveredRootPaths;
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDependencyInjectionTests.cpp
// 函数: 多个 root-discovery test
// 位置: 76-90, 171-189, 227-240
// 说明: 当前插件把“插件脚本根发现”正式变成回归契约
// ============================================================================
Dependencies.GetEnabledPluginScriptRoots = []()
{
	return TArray<FString>
	{
		TEXT("C:/Plugins/Beta/Script"),
		TEXT("C:/Plugins/Alpha/Script"),
	};
};
...
TestEqual(TEXT("Injected project root should be first"), Roots[0], FString(TEXT("C:/InjectedProject/Script")));
TestEqual(TEXT("Injected plugin roots should be sorted deterministically"), Roots[1], FString(TEXT("C:/Plugins/Alpha/Script")));
...
TestEqual(TEXT("Only existing plugin root should remain after skipping missing roots"), Roots[1], FString(TEXT("C:/Plugins/Alpha/Script")));
...
TestEqual(TEXT("Created project root should be returned by discovery"), Roots[0], FString(TEXT("C:/InjectedEditorProject/Script")));
```

### 差距判断

| 子问题 | 结论 | 依据 |
| --- | --- | --- |
| 当前插件是否“没有” GAS / EnhancedInput 能力边界 | **不是** | 能力还在，只是从独立插件边界折叠进主插件依赖 |
| 两边是否都支持插件脚本根 | **是** | `MakeAllScriptRoots()` 与 `DiscoverScriptRoots()` 都枚举启用插件的 `Script` 目录 |
| 当前插件在该维度的变化本质是什么 | **实现方式不同** | 从“独立功能插件 + 直接枚举”改成“主插件内收 + 可注入依赖” |
| 当前插件是否在某处反而更强 | **是，测试可观测性更强** | `AngelscriptDependencyInjectionTests.cpp` 把根顺序、缺失目录、编辑器建目录行为都锁成回归用例 |

因此，这个维度不能简化成“Hazelight 模块更多，所以更先进”。更准确的结论是：**Hazelight 的优势在于发布边界更细；当前插件的优势在于把 script-root 发现从环境逻辑提炼成可测试契约。前者是 packaging 粒度优势，后者是可验证性优势。**

### [维度 D5/D6] 当前插件保留了 UEAS2 的离线文档导出链，但把非 `UFunction` manual bind 的调试元数据面收窄成“`UFunction` metadata + dump stats”

```
[D5/D6-Deep] Documentation And Debug Metadata
Hazelight
Bind_*.cpp / UFunction metadata
├─ SCRIPT_BIND_DOCUMENTATION
├─ SCRIPT_MANUAL_BIND_META
├─ FAngelscriptDocs stores doc + free-form meta
├─ DebugServer emits "doc" and fallback "meta"
└─ DumpDocumentation -> Docs/angelscript/generated/*.hpp

AngelPortV2
Bind_*.cpp / UFunction metadata
├─ SCRIPT_BIND_DOCUMENTATION
├─ FAngelscriptDocs stores doc + counters
├─ DebugServer emits "doc" and UFunction-derived "meta"
├─ StateDump -> DocumentationStats.csv
└─ DumpDocumentation -> Docs/angelscript/generated/*.hpp
```

这一点和“有没有 `DumpDocumentation()`”也不是一回事。当前插件实际上保留了 UEAS2 的 `.hpp` 文档导出链，甚至命令行触发方式也还在；真正变化的是 `DebugServer` 背后的文档数据库不再接受任意 `SCRIPT_MANUAL_BIND_META` 注入，改成只信任 `UFunction` 自身 metadata，再额外把文档库规模导出成 `DocumentationStats.csv`。

换句话说，当前插件不是把文档系统删了，而是把它从“文档 + 自由 meta 通道”收敛成了“文档 + 统计可观测性”。这对离线审计有利，但会缩窄纯 manual bind 在 IDE / debugger 里的附加提示面。

[1] UEAS2 的 `FAngelscriptDocs` 明确包含 free-form meta 通道，`DebugServer` 在拿不到 `UFunction` 时会退回这条通道：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Public\AngelscriptDocs.h
// 位置: 5-9, 20-35
// 说明: 上游 docs API 不只存 tooltip，还暴露 SCRIPT_MANUAL_BIND_META / GetScriptFunctionMeta
// ============================================================================
#define AS_DOC(FunctionId, Documentation) FAngelscriptDocs::AddUnrealDocumentation(FunctionId, Documentation);
#define SCRIPT_BIND_DOCUMENTATION(Documentation) FAngelscriptDocs::AddUnrealDocumentation(FAngelscriptBinds::GetPreviousFunctionId(), TEXT(Documentation), TEXT(""), nullptr);
#define SCRIPT_GLOBAL_DOCUMENTATION(Documentation) FAngelscriptDocs::AddDocumentationForGlobalVariable(FAngelscriptBinds::GetPreviousGlobalVariableId(), TEXT(Documentation));
#define SCRIPT_PROPERTY_DOCUMENTATION(Binds, Documentation) FAngelscriptDocs::AddUnrealDocumentationForProperty(Binds.GetTypeId(), FAngelscriptBinds::GetPreviousPropertyOffset(), TEXT(Documentation));
#define SCRIPT_MANUAL_BIND_META(MetaName, MetaValue) FAngelscriptDocs::AddScriptFunctionMeta(FAngelscriptBinds::GetPreviousFunctionId(), TEXT(MetaName), TEXT(MetaValue));

struct ANGELSCRIPTCODE_API FAngelscriptDocs
{
	...
	static void AddScriptFunctionMeta(int FunctionId, FStringView MetaName, FStringView MetaValue);
	...
	static const TMap<FString, FString>* GetScriptFunctionMeta(int FunctionId);
	...
};
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Binds\Bind_FAngelscriptDelegateWithPayload.cpp
// 位置: 22-39
// 说明: 上游给纯 manual bind 的 delegate API 手动写入 debugger 可消费的 meta
// ============================================================================
FAngelscriptDelegateWithPayload_.Method("void BindUFunction(UObject Object, const FName& FunctionName)",
[](FAngelscriptDelegateWithPayload& Delegate, UObject* Object, const FName& FunctionName)
{
	Delegate.BindUFunction(Object, FunctionName);
});
SCRIPT_MANUAL_BIND_META("DelegateObjectParam", "Object");
SCRIPT_MANUAL_BIND_META("DelegateFunctionParam", "FunctionName");
SCRIPT_MANUAL_BIND_META("DelegateBindType", "FInternalEmptyDelegate");

FAngelscriptDelegateWithPayload_.Method("void BindWithPayload(UObject Object, const FName& FunctionName, const ?&in Payload)",
[](FAngelscriptDelegateWithPayload& Delegate, UObject* Object, const FName& FunctionName, void* PayloadPtr, int PayloadScriptTypeId)
{
	Delegate.BindUFunctionWithPayload(Object, FunctionName, PayloadPtr, PayloadScriptTypeId);
});
SCRIPT_MANUAL_BIND_META("DelegateObjectParam", "Object");
SCRIPT_MANUAL_BIND_META("DelegateFunctionParam", "FunctionName");
SCRIPT_MANUAL_BIND_META("DelegateBindType", "FInternalEmptyDelegateWithPayload");
SCRIPT_MANUAL_BIND_META("DelegateWildcardParam", "Payload");
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Debugging\AngelscriptDebugServer.cpp
// 位置: 1497-1507
// 说明: 没有 UnrealFunction 时，调试服务器仍会回落到 ScriptFunctionMeta
// ============================================================================
else
{
	const TMap<FString, FString>* ScriptMeta = FAngelscriptDocs::GetScriptFunctionMeta(ScriptFunction->GetId());
	if (ScriptMeta != nullptr && !ScriptMeta->IsEmpty())
	{
		TSharedPtr<FJsonObject> MetaObject = MakeShared<FJsonObject>();
		for (auto MetaTag : *ScriptMeta)
			MetaObject->SetStringField(MetaTag.Key, MetaTag.Value);

		FuncDesc->SetObjectField(TEXT("meta"), MetaObject);
	}
}
```

[2] 当前插件保留 `.hpp` 文档导出，但 `FAngelscriptDocs` 公共接口已经改成“文档计数器 + dump”；`DebugServer` 的 meta 来源只剩 `UnrealFunction`：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.h
// 位置: 5-12, 16-36
// 说明: 当前 docs API 保留文档导出，但公开面已经没有 AddScriptFunctionMeta / GetScriptFunctionMeta
// ============================================================================
#define AS_DOC(FunctionId, Documentation) FAngelscriptDocs::AddUnrealDocumentation(FunctionId, Documentation);
#define SCRIPT_BIND_DOCUMENTATION(Documentation) FAngelscriptDocs::AddUnrealDocumentation(FAngelscriptBinds::GetPreviousFunctionId(), TEXT(Documentation), TEXT(""), nullptr);
#define SCRIPT_GLOBAL_DOCUMENTATION(Documentation) FAngelscriptDocs::AddDocumentationForGlobalVariable(FAngelscriptBinds::GetPreviousGlobalVariableId(), TEXT(Documentation));
...
struct ANGELSCRIPTRUNTIME_API FAngelscriptDocs
{
	...
	static int32 GetUnrealDocumentationCount();
	static int32 GetUnrealTypeDocumentationCount();
	static int32 GetGlobalVariableDocumentationCount();
	static int32 GetUnrealPropertyDocumentationCount();
	...
};
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 位置: 33-38, 1575-1577, 1599-1617
// 说明: 当前调试器仍认得这些 meta key，但只从 UnrealFunction metadata 取值
// ============================================================================
const FName NAME_ScriptKeywords("ScriptKeywords");
const TArray<FName> NAMES_InformedMeta = {
	"DelegateBindType",
	"DelegateFunctionParam",
	"DelegateObjectParam",
};
...
const FString& Doc = FAngelscriptDocs::GetUnrealDocumentation(ScriptFunction->GetId());
if (Doc.Len() != 0)
	FuncDesc->SetStringField(TEXT("doc"), Doc);
...
const FString& Keywords = UnrealFunction->GetMetaData(NAME_ScriptKeywords);
...
for (FName MetaTag : NAMES_InformedMeta)
{
	const FString& MetaValue = UnrealFunction->GetMetaData(MetaTag);
	if (!MetaValue.IsEmpty())
	{
		...
		MetaObject->SetStringField(MetaTag.ToString(), MetaValue);
	}
}
FuncDesc->SetObjectField(TEXT("meta"), MetaObject);
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FAngelscriptDelegateWithPayload.cpp
// 位置: 15-32
// 说明: 当前 manual bind 仍存在，但不再跟随 SCRIPT_MANUAL_BIND_META
// ============================================================================
AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_AngelscriptDelegateWithPayload((int32)FAngelscriptBinds::EOrder::Late, []
{
	auto Delegate_ = FAngelscriptBinds::ExistingClass("FAngelscriptDelegateWithPayload");

	Delegate_.Method("void ExecuteIfBound() const", &FAngelscriptDelegateWithPayload::ExecuteIfBound);
	Delegate_.Method("bool IsBound() const", &FAngelscriptDelegateWithPayload::IsBound);

	Delegate_.Method("void BindUFunction(UObject Object, const FName& FunctionName)",
	[](FAngelscriptDelegateWithPayload& Delegate, UObject* Object, const FName& FunctionName)
	{
		Delegate.BindUFunction(Object, FunctionName);
	});

	Delegate_.Method("void BindWithPayload(UObject Object, const FName& FunctionName, const ?&in Payload)",
	[](FAngelscriptDelegateWithPayload& Delegate, UObject* Object, const FName& FunctionName, void* PayloadPtr, int PayloadScriptTypeId)
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 528, 2224-2227
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp
// 位置: 936-949
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Dump/AngelscriptDumpTests.cpp
// 位置: 45-75
// 说明: 当前插件在保留 dump-as-doc 的同时，把文档规模显式导出并纳入 dump 回归
// ============================================================================
Config.bDumpDocumentation = FParse::Param(FCommandLine::Get(), TEXT("dump-as-doc"));
...
if (RuntimeConfig.bDumpDocumentation)
{
	FAngelscriptDocs::DumpDocumentation(Engine);
	FPlatformMisc::RequestExit(false);
}
...
Writer.AddRow({ TEXT("UnrealDocumentation"), LexToString(FAngelscriptDocs::GetUnrealDocumentationCount()) });
Writer.AddRow({ TEXT("UnrealTypeDocumentation"), LexToString(FAngelscriptDocs::GetUnrealTypeDocumentationCount()) });
Writer.AddRow({ TEXT("GlobalVariableDocumentation"), LexToString(FAngelscriptDocs::GetGlobalVariableDocumentationCount()) });
Writer.AddRow({ TEXT("UnrealPropertyDocumentation"), LexToString(FAngelscriptDocs::GetUnrealPropertyDocumentationCount()) });
...
return {
	TEXT("EngineOverview.csv"),
	...
	TEXT("DocumentationStats.csv"),
	...
	TEXT("DumpSummary.csv")
};
```

### 差距判断

| 子问题 | 结论 | 依据 |
| --- | --- | --- |
| 当前插件是否没有文档导出链 | **不是** | `DumpDocumentation()`、`-dump-as-doc` 和 `Docs/angelscript/generated/*.hpp` 仍保留 |
| 当前插件是否比 UEAS2 多了一层文档可观测性 | **是** | `DocumentationStats.csv` 与 dump 回归测试是新增能力 |
| 非 `UFunction` manual bind 的 debugger meta 面是否完全等价 | **不是** | UEAS2 有 `SCRIPT_MANUAL_BIND_META -> GetScriptFunctionMeta()` 回退，当前插件公共 API 已无这条通道 |
| 这属于“没有实现”还是“实现质量差异” | **局部质量差异** | 主文档链还在，但 manual bind 的附加 IDE/调试提示面更窄 |

因此，`D5/D6` 在这一轮最值得记住的结论是：**当前插件不是放弃了 Hazelight 的文档系统，而是把它改造成“导出仍在、统计更强、自由 meta 更少”的形态。对离线审计和自动回归更友好，但对纯 manual bind 的调试描述力略有收缩。**

### [维度 D10] 当前插件不是没有示例，而是把“可分发 `.as` 示例资产”改成了“测试内联脚本夹具”

```
[D10-Deep] Example Delivery
Hazelight
Script-Examples/
├─ Examples (20)                      // 通用语言与 UE 交互示例
├─ GASExamples (3)                    // GAS 示例
├─ EnhancedInputExamples (2)          // EI 示例
└─ EditorExamples (1)                 // Editor 扩展示例

AngelPortV2
Source/AngelscriptTest/Examples/
├─ *Example*Test.cpp (20)             // C++ 自动化用例
├─ embedded raw string scripts        // 脚本正文内联到测试源码
└─ CompileAnnotatedModuleFromMemory   // 以虚拟文件名编译
   └─ not part of production Script roots
```

我本地直接枚举 `J:\UnrealEngine\UEAS2\Script-Examples\**\*.as`，当前快照共有 `26` 个脚本文件，按目录分布是 `Examples=20`、`GASExamples=3`、`EnhancedInputExamples=2`、`EditorExamples=1`。当前插件侧则没有任何插件内 `.as` 示例目录；对应能力被迁移成 `Plugins/Angelscript/Source/AngelscriptTest/Examples/` 下 `20` 个 `*Example*Test.cpp` 自动化用例。

所以这个维度的真实判断不能写成“当前插件没有示例”。更准确的说法是：**示例仍然存在，但它们已经不再是用户可直接浏览/复制/挂进 `Script/` 根目录的脚本资产，而是测试工程内部的编译夹具。**

[1] UEAS2 的示例是实打实的 `.as` 文件，面向读者而不是面向 test harness：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Script-Examples\Examples\Example_Actor.as
// 位置: 1-74
// 说明: 这是仓库直接分发的脚本示例文件，用户能直接阅读并拷入 Script 根
// ============================================================================
/*
 * Script classes can always derive from the same classes that
 * blueprints can be derived from.
 */

// For example, we can make a new Actor class
class AExampleActorType : AActor
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

	void ScriptOnlyMethod()
	{
		Log("ScriptOnlyMethod Called");
	}

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
};
```

[2] 当前插件把同类示例改成测试内联字符串，并通过内存编译验证它们是否还能成立：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleActorTest.cpp
// 位置: 9-18, 22-27, 58-84, 90-94
// 说明: 当前“示例”已经进入自动化测试源码，而不是单独脚本文件
// ============================================================================
const AngelscriptScriptExamples::FScriptExampleSource GActorExample = {
	TEXT("Example_Actor.as"),
	TEXT(R"ANGELSCRIPT(
class AExampleActor_UnitTest : AActor
{
	UPROPERTY()
	int ExampleValue = 15;

	default bReplicates = true;
	default Tags.Add(n"ExampleTag");
	...
	UFUNCTION(BlueprintOverride)
	void BeginPlay()
	{
		ScriptOnlyMethod();
		NewOverridableMethod();
	}
	...
};)ANGELSCRIPT"),
	nullptr,
	nullptr,
};
...
bool FAngelscriptScriptExampleActorTest::RunTest(const FString& Parameters)
{
	return AngelscriptScriptExamples::RunScriptExampleCompileTest(*this, GActorExample);
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleTestSupport.cpp
// 函数: AngelscriptScriptExamples::RunScriptExampleCompileTest
// 位置: 28-58
// 说明: 示例现在通过虚拟文件名从内存编译，目标是回归验证而不是直接分发
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
const bool bCompiled = CompileAnnotatedModuleFromMemory(&Engine, ModuleName, VirtualFileName, CombinedScriptCode);
Test.TestTrue(*FString::Printf(TEXT("Compile example '%s' succeeds"), *ExampleFileName), bCompiled);
```

[3] 结合当前插件的 script-root 发现规则，可以确定这些测试示例并不会进入生产脚本根：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: FAngelscriptEngine::DiscoverScriptRoots
// 位置: 1334-1363
// 说明: 生产发现面只看 Project/Script 和启用插件的 Script 目录
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
...
DiscoveredRootPaths.Insert(RootPath, 0);
return DiscoveredRootPaths;
```

### 差距判断

| 子问题 | 结论 | 依据 |
| --- | --- | --- |
| 当前插件是否完全没有示例 | **不是** | `AngelscriptTest/Examples/*.cpp` 里保留了与 UEAS2 对应的示例内容 |
| 当前插件是否还提供可直接分发的 `.as` 示例目录 | **不是** | 示例正文内联在 C++ 测试里，不在 `Script/` 根，也不在独立 `Script-Examples/` |
| 当前插件在 GAS / EnhancedInput / Editor 示例分类上是否等价 | **不是** | UEAS2 还有 `GASExamples` / `EnhancedInputExamples` / `EditorExamples` 三类独立脚本目录，当前插件这一层已缺席 |
| 差异属于什么类型 | **交付形态不同 + 部分覆盖缺口** | 通用示例大多被迁成测试夹具，但“面向用户的脚本资产交付”确实少了一层 |

因此，`D10` 这一轮最关键的新结论是：**当前插件把示例的第一职责从“教学/分发”改成了“回归验证”。它在工程内更稳，因为每个示例都能被自动编译检查；但对最终使用者来说，可直接拿来改的 `.as` 资产面比 UEAS2 更薄，而且 GAS / EnhancedInput / Editor 三类示例目前没有等价交付物。**

---

## 深化分析 (2026-04-08 19:41:50)

### [维度 D8/D11] `PrecompiledData + StaticJIT` 并没有在当前插件里消失，真正变化的是“owner”与“命中条件”

```
[D8/D11-Deep] Precompiled And JIT Lifecycle
Hazelight
├─ CLI flags: as-generate-precompiled-data / as-ignore-precompiled-data
├─ FAngelscriptManager owns PrecompiledData + StaticJIT
├─ load Binds.Cache
├─ InitialCompile()
│  ├─ ApplyToModule_Stage1/2/3 by module name
│  └─ ScriptModule->JITCompile()
└─ optional WriteOutputCode() / Save(PrecompiledScript.Cache)

AngelPortV2
├─ RuntimeConfig carries the same CLI semantics
├─ FAngelscriptEngine owns PrecompiledData + StaticJIT
│  └─ FAngelscriptOwnedSharedState keeps engine-scoped shared ownership
├─ load Binds.Cache + BindModules.Cache
├─ InitialCompile()
│  ├─ ApplyToModule_Stage1/2/3 only if imports ready + CodeHash matches
│  └─ ScriptModule->JITCompile()
└─ optional WriteOutputCode() / Save(PrecompiledScript.Cache)
```

前文已经把 `UHTTool`、`FunctionTable` 和 `NativeThunk` 讲清；这一轮补的是另一条常被误判的链: 很容易把“无引擎补丁”误读成“没有预编译脚本 / StaticJIT”。源码对齐后更准确的结论是：**这条优化链在当前插件里仍然完整存在，变化主要发生在生命周期 owner 和 cache 复用条件**。

[1] UEAS2 当前快照里，`FAngelscriptManager` 仍完整负责 `PrecompiledData`、`StaticJIT`、bind database、JIT 输出和 `PrecompiledScript.Cache` 落盘：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\AngelscriptManager.cpp
// 位置: 361-382, 404-407, 434-479, 497-520
// 说明: 上游优化链是完整闭环，而不是“只支持解释执行”
// ============================================================================
bGeneratePrecompiledData = FParse::Param(FCommandLine::Get(), TEXT("as-generate-precompiled-data"));
bUsePrecompiledData = !bGeneratePrecompiledData && !FParse::Param(FCommandLine::Get(), TEXT("as-ignore-precompiled-data"))
	&& !IsRunningCommandlet() && !WITH_EDITOR && !bScriptDevelopmentMode;

if (bGeneratePrecompiledData)
	PrecompiledData = new FAngelscriptPrecompiledData(Engine);

if (bGeneratePrecompiledData)
{
	StaticJIT = new FAngelscriptStaticJIT();
	StaticJIT->PrecompiledData = PrecompiledData;
	Engine->SetJITCompiler(StaticJIT);
}

FAngelscriptBindDatabase::Get().Load(GetScriptRootDirectory() / TEXT("Binds.Cache"), bGeneratePrecompiledData);

if (bUsePrecompiledData)
{
	PrecompiledData = new FAngelscriptPrecompiledData(Engine);
	PrecompiledData->Load(Filename);

	if (!PrecompiledData->IsValidForCurrentBuild())
	{
		delete PrecompiledData;
		PrecompiledData = nullptr;
	}
	else
	{
		if (StaticJIT != nullptr)
			StaticJIT->PrecompiledData = PrecompiledData;

		// ★ 只有 DataGuid 匹配时才允许复用已编进二进制的 JIT 代码
		const FStaticJITCompiledInfo* CompiledInfo = FStaticJITCompiledInfo::Get();
		if (CompiledInfo != nullptr && CompiledInfo->PrecompiledDataGuid != PrecompiledData->DataGuid)
			FJITDatabase::Get().Clear();
	}
}

if (StaticJIT != nullptr && StaticJIT->bGenerateOutputCode)
	StaticJIT->WriteOutputCode();

if (bGeneratePrecompiledData)
{
	PrecompiledData->InitFromActiveScript();
	PrecompiledData->Save(Filename);
}
```

[2] 当前插件把同一条链平移到 `FAngelscriptEngine`，并额外把 bind-module 自动加载与 engine-scoped shared state 纳入初始化：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 1425-1492, 1512-1602
// 说明: APV2 并没有删掉 precompiled/JIT，只是把 owner 从 manager singleton 改成 engine instance
// ============================================================================
bGeneratePrecompiledData = RuntimeConfig.bGeneratePrecompiledData;
bUsePrecompiledData = !bGeneratePrecompiledData && !RuntimeConfig.bIgnorePrecompiledData
	&& !RuntimeConfig.bRunningCommandlet && !WITH_EDITOR && !bScriptDevelopmentMode;

if (bGeneratePrecompiledData)
	PrecompiledData = new FAngelscriptPrecompiledData(Engine);

if (bGeneratePrecompiledData)
{
	StaticJIT = new FAngelscriptStaticJIT();
	StaticJIT->PrecompiledData = PrecompiledData;
	Engine->SetJITCompiler(StaticJIT);
}

FAngelscriptBindDatabase::Get().Load(GetScriptRootDirectory() / TEXT("Binds.Cache"), bGeneratePrecompiledData);
FAngelscriptBinds::LoadBindModules(plugin->GetBaseDir() / "BindModules.Cache");
EnsureSharedStateCreated();

if (bUsePrecompiledData)
{
	PrecompiledData = new FAngelscriptPrecompiledData(Engine);
	PrecompiledData->Load(Filename);

	if (!PrecompiledData->IsValidForCurrentBuild())
	{
		delete PrecompiledData;
		PrecompiledData = nullptr;
	}
	else
	{
		if (StaticJIT != nullptr)
			StaticJIT->PrecompiledData = PrecompiledData;

		const FStaticJITCompiledInfo* CompiledInfo = FStaticJITCompiledInfo::Get();
		if (CompiledInfo != nullptr && CompiledInfo->PrecompiledDataGuid != PrecompiledData->DataGuid)
			FJITDatabase::Get().Clear();
	}
}

if (StaticJIT != nullptr && StaticJIT->bGenerateOutputCode)
	StaticJIT->WriteOutputCode();

if (bGeneratePrecompiledData)
{
	PrecompiledData->InitFromActiveScript();
	PrecompiledData->Save(Filename);
}
```

[3] 真正新的质量差异在 module 级 precompiled 命中条件。UEAS2 当前快照按模块名命中就直接 `ApplyToModule_Stage1()`；当前插件则先要求“所有 imports 已预编译”且 `CodeHash` 一致：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\AngelscriptManager.cpp
// 位置: 2942-2955
// 说明: 上游当前快照的 Stage1 入口只按模块名查 precompiled blob
// ============================================================================
if (PrecompiledData != nullptr && bUsePrecompiledData)
{
	const FAngelscriptPrecompiledModule* CompiledModule = PrecompiledData->Modules.Find(Module->ModuleName);
	if (CompiledModule != nullptr)
	{
		CompiledModule->ApplyToModule_Stage1(*PrecompiledData, ScriptModule);
		Module->PrecompiledData = CompiledModule;
		Module->bLoadedPrecompiledCode = true;
		return;
	}
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 4283-4300, 4370-4400
// 说明: 当前插件在复用前增加 import 完整性与 CodeHash 校验，再继续 Stage2/3
// ============================================================================
if (PrecompiledData != nullptr && bAllImportsPreCompiled && bUsePrecompiledData)
{
	const FAngelscriptPrecompiledModule* CompiledModule = PrecompiledData->Modules.Find(Module->ModuleName);
	if (CompiledModule != nullptr)
	{
		if (CompiledModule->CodeHash == Module->CodeHash)
		{
			CompiledModule->ApplyToModule_Stage1(*PrecompiledData, ScriptModule);
			Module->PrecompiledData = CompiledModule;
			Module->bLoadedPrecompiledCode = true;
			return;
		}
	}
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

ScriptModule->JITCompile();
```

[4] 当前插件还把这条链的共享 owner 显式挂到了 engine scope；同时用兼容性测试锁住“仍嵌入 `2.33.0 WIP`，但公开 property/flag layout 要兼容 stock `2.38`”：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 144-163, 922-942
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h
// 位置: 68-69, 153-167
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptUpgradeCompatibilityTests.cpp
// 位置: 56-78
// 说明: 当前插件把优化链 owner 改成 engine scope，同时显式测试核心 fork contract
// ============================================================================
struct FAngelscriptOwnedSharedState
{
	asCScriptEngine* ScriptEngine = nullptr;
	asCContext* PrimaryContext = nullptr;
	FAngelscriptPrecompiledData* PrecompiledData = nullptr;
	FAngelscriptStaticJIT* StaticJIT = nullptr;
	...
};

void FAngelscriptEngine::InitializeOwnedSharedState()
{
	SharedState->ScriptEngine = Engine;
	SharedState->PrimaryContext = ...;
	SharedState->PrecompiledData = PrecompiledData;
	SharedState->StaticJIT = StaticJIT;
}

#define ANGELSCRIPT_VERSION        23300
#define ANGELSCRIPT_VERSION_STRING "2.33.0 WIP"
//[UE++]: Restore the stock 2.38 public engine-property surface before the APV2 custom range.
asEP_INIT_STACK_SIZE = 29,
...
asEP_FOREACH_SUPPORT = 40,

TestEqual(TEXT("Embedded Angelscript version should remain pinned to 2.33.0 until the 2.38 upgrade resumes"), ANGELSCRIPT_VERSION, 23300);
TestEqual(TEXT("Stock 2.38 JIT interface version property id should remain available"), static_cast<int32>(asEP_JIT_INTERFACE_VERSION), 35);
```

### 差距判断

| 子问题 | 结论 | 依据 |
| --- | --- | --- |
| 当前插件是否已经没有 `PrecompiledScript.Cache` / `StaticJIT` | **不是** | `AngelscriptEngine.cpp:1425-1602` 仍完整保留开关、加载、写出与 `JITCompile()` |
| 两边 module 级 precompiled 命中条件是否一致 | **不一致，当前插件更保守** | `AngelscriptManager.cpp:2942-2955` vs `AngelscriptEngine.cpp:4283-4300` |
| 优化链 owner 是否相同 | **实现方式不同** | UEAS2 是 manager singleton；当前插件是 engine-scoped `SharedState` |
| 当前插件的 AngelScript core contract 是否仍完全等同 Hazelight | **不是** | 版本仍是 `2.33.0 WIP`，但 `angelscript.h:153-167` 与升级兼容测试把公开 ID/flag 布局对齐到 stock `2.38` |

因此，这一维的新结论不是“当前插件少了优化”，而是：**优化链基本保留，但 cache 复用更保守、所有权更局部化、底层 core contract 也被测试显式钉在“2.33.0 内核 + 2.38 公开布局兼容层”这一过渡态上。** 这属于“**实现方式不同 + 局部实现质量差异**”，不是“**没有实现**”。

### [维度 D5] 当前插件把 `DebugServer V2` 的 framing contract 显式化了，但 live socket loop 仍沿用上游内联收包模型

```
[D5-Deep] Debug Transport Contract
Hazelight
├─ socket loop reads int32 PacketSize
├─ reads payload bytes
├─ first byte => EDebugMessageType
└─ HandleMessage(MessageType, Datagram, Client)

AngelPortV2
├─ same live socket loop still exists
├─ plus SerializeDebugMessageEnvelope()
├─ plus TryDeserializeDebugMessageEnvelope()
└─ plus protocol/transport round-trip tests
```

前文已经把 `DebugServerVersion == 2`、消息枚举与 data breakpoint 并发模型讲过。本轮新增的点更底层：**当前插件第一次把 transport framing 这层约束抽成了独立 helper 与自动化测试；但真正在线上的 `FSocket` 收包循环仍基本照搬 UEAS2 旧实现。** 所以这里没有协议代际分裂，只有工程化程度提升。

[1] UEAS2 的 transport framing 完全内联在 `Tick()` 的收包循环里，没有独立 envelope helper：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Debugging\AngelscriptDebugServer.cpp
// 位置: 648-681
// 说明: 上游直接在 socket loop 中读取长度头与 payload
// ============================================================================
int32 BytesReceived = 0;
int32 PacketSize = -1;

Datagram->SetNumUninitialized(sizeof(PacketSize));
while (BytesReceived < sizeof(PacketSize))
{
	int32 BytesRead = 0;
	Client->Recv(Datagram->GetData(), Datagram->Num() - BytesReceived, BytesRead);
	BytesReceived += BytesRead;
}

*Datagram << PacketSize;
if (PacketSize <= 0 || PacketSize > 1024 * 1024)
	break;

BytesReceived = 0;
Datagram->SetNumUninitialized(PacketSize);
while (BytesReceived < PacketSize)
{
	int32 BytesRead = 0;
	Client->Recv(Datagram->GetData(), Datagram->Num() - BytesReceived, BytesRead);
	BytesReceived += BytesRead;
}

EDebugMessageType MessageType;
*Datagram << MessageType;
```

[2] 当前插件在保留同一 live loop 的同时，额外导出了可复用的 envelope helper，把“长度头 + type byte + body”写成显式契约：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h
// 位置: 88-93
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 位置: 52-103
// 说明: transport framing 首次被抽成独立 API，便于测试 partial / invalid length / empty body
// ============================================================================
struct FAngelscriptDebugMessageEnvelope
{
	EDebugMessageType MessageType = EDebugMessageType::Disconnect;
	TArray<uint8> Body;
};

ANGELSCRIPTRUNTIME_API bool SerializeDebugMessageEnvelope(EDebugMessageType MessageType, const TArray<uint8>& Body, TArray<uint8>& OutBuffer);
ANGELSCRIPTRUNTIME_API bool TryDeserializeDebugMessageEnvelope(TArray<uint8>& InOutBuffer, FAngelscriptDebugMessageEnvelope& OutEnvelope, bool& bOutHasEnvelope, FString* OutError = nullptr);

const int32 MessageLength = static_cast<int32>(sizeof(uint8)) + Body.Num();
Writer << const_cast<int32&>(MessageLength);
Writer << const_cast<uint8&>(MessageTypeByte);
OutBuffer.Append(Body);

if (MessageLength <= 0 || MessageLength > 1024 * 1024)
{
	*OutError = FString::Printf(TEXT("Received debugger envelope with invalid message length %d."), MessageLength);
	return false;
}

if (InOutBuffer.Num() < TotalEnvelopeSize)
	return true;
```

[3] 这层 helper 已被测试覆盖，但运行中的收包主循环仍然是 Hazelight 式内联实现：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugTransportTests.cpp
// 位置: 53-78, 176-195
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp
// 位置: 79-101
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 位置: 751-794
// 说明: framing/protocol 已工程化验证，但 live socket loop 仍未复用 helper
// ============================================================================
TestTrue(TEXT("Debug.Transport.SingleEnvelope should serialize a debugger envelope"), SerializeDebugMessageEnvelope(...));
TestEqual(TEXT("Debug.Transport.SingleEnvelope should store the payload length as type-byte plus body"), MessageLength, static_cast<int32>(sizeof(uint8)) + Body.Num());
TestTrue(TEXT("Debug.Transport.TruncatedEnvelope should not treat a partial packet as a protocol error"), TryDeserializeDebugMessageEnvelope(Buffer, Envelope, bHasEnvelope, &Error));
TestFalse(TEXT("Debug.Transport.InvalidLength should reject zero-length envelopes"), TryDeserializeDebugMessageEnvelope(Buffer, Envelope, bHasEnvelope, &Error));

TestEqual(TEXT("Debug.Protocol.DebugServerVersion.RoundTrip should preserve the server version"), RoundTripped.DebugServerVersion, DEBUG_SERVER_VERSION);

while (Client->HasPendingData(DataSize))
{
	int32 BytesReceived = 0;
	int32 PacketSize = -1;
	...
	Client->Recv(...);
	*Datagram << PacketSize;
	...
	Client->Recv(...);
	*Datagram << MessageType;
	HandleMessage(MessageType, Datagram, Client);
}
```

### 差距判断

| 子问题 | 结论 | 依据 |
| --- | --- | --- |
| `DebugServer V2` 的 framing 格式是否变了 | **没有** | 两边都是 `int32 length + uint8 type + payload body` |
| 当前插件是否新增了 transport 契约级自动化验证 | **是** | `AngelscriptDebugTransportTests.cpp` 与 `AngelscriptDebugProtocolTests.cpp` |
| live socket 读包实现是否已完全改成 helper | **没有** | `AngelscriptDebugServer.cpp:751-794` 仍是上游式内联循环 |
| 这属于什么差距 | **实现质量差异** | 协议没变，但 framing contract 在当前插件里更可测试、更可复盘 |

基于源码可以更精确地说：**当前插件没有“改协议”，而是把协议外围的 framing 规则工程化了；不过线上读包路径还没完成去重复。** 这是“**实现质量差异**”，不是“**实现方式完全不同**”，更不是“**没有实现 DebugServer V2**”。

### [维度 D8/D11] 前文已说明 `NativeThunk` 是关键替代点；这一轮补的是“替代范围边界”：当前插件替掉了调用边界，但没有等价替掉 UEAS2 那层 engine-mainline 默认认知

```
[D8/D11-Deep] Runtime-Generated Contract Boundary
UEAS2 Engine Patch
├─ UHT parser/codegen writes AngelscriptPropertyFlags
├─ CoreUObject adds FUNC_RuntimeGenerated + RuntimeCallFunction()
├─ Engine/Editor checks APF_RuntimeGenerated / PKG_RuntimeGenerated / bIsScriptClass
└─ runtime-generated class/function/property become first-class in mainline code

AngelPortV2 Plugin Substitute
├─ UASClass / UASFunction subclass localize script metadata
├─ ClassGenerator emits FUNC_Native + UASFunctionNativeThunk
├─ plugin code reconstructs WorldContext / DefaultToSelf / CPP_Default_*
└─ some call/bind paths manually cast UASClass instead of relying on engine-wide flags
```

前文已经说过“当前插件把 `FUNC_RuntimeGenerated` 改写成 `FUNC_Native + NativeThunk`”。这一轮继续往下拆后，可以更明确地区分三层 contract：

1. **参数语义层**：`CppRef / CppConst / CppEnumAsByte / WorldContext` 这类信息在 UEAS2 是 UHT/Property 主链认识的。
2. **调用入口层**：UEAS2 让 `UFunction` / `ScriptCore` 原生知道 runtime-generated function；当前插件则用 `UASFunctionNativeThunk` 把这层转成标准 native path。
3. **主干默认认知层**：UEAS2 的 `Actor` / `PropertyEditor` / `K2` / package flag 都会主动识别 runtime-generated class/property/function；当前插件源码里没有等价的 `APF_RuntimeGenerated` / `PKG_RuntimeGenerated` 主干接入，只能在插件自有路径里显式 `Cast<UASClass>()`。

[1] UEAS2 的引擎补丁不是单点，而是从 UHT parser 一路写到 generated code 和 CoreUObject flag：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Programs\Shared\EpicGames.UHT\Parsers\UhtFunctionParser.cs
// 位置: 575-586
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Programs\Shared\EpicGames.UHT\Exporters\CodeGen\UhtHeaderCodeGeneratorCppFile.cs
// 位置: 2538-2585
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Runtime\CoreUObject\Public\UObject\ObjectMacros.h
// 位置: 483-490
// 说明: 参数语义直接进 UHT / generated code / property flags 主链
// ============================================================================
if (function.MetaData.ContainsKey("WorldContext"))
{
	...
	Property.AngelscriptPropertyFlags |= EAngelscriptPropertyFlags.WorldContext;
}

bool bIsRef = Property.AngelscriptPropertyFlags.HasFlag(EAngelscriptPropertyFlags.CppRef);
bool bIsConst = Property.AngelscriptPropertyFlags.HasFlag(EAngelscriptPropertyFlags.CppConst);
if (Property is UhtByteProperty ByteProp && ByteProp.Enum != null && ByteProp.AngelscriptPropertyFlags.HasFlag(EAngelscriptPropertyFlags.CppEnumAsByte))
{
	builder.Append("TEnumAsByte<");
	...
}

enum EAngelscriptPropertyFlags : uint16
{
	APF_CppConst = 0x0001,
	APF_CppRef = 0x0002,
	APF_CppEnumAsByte = 0x0004,
	APF_RuntimeGenerated = 0x0008,
	APF_WorldContext = 0x0010,
};
```

[2] 同一条链还继续下沉到 `UFunction`、`ScriptCore`、`Actor` 和 package flag；也就是 engine/editor 主干默认知道什么是 runtime-generated 元素：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Runtime\CoreUObject\Public\UObject\Script.h
// 位置: 138-143
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Runtime\CoreUObject\Public\UObject\Class.h
// 位置: 2606-2609
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Runtime\CoreUObject\Private\UObject\ScriptCore.cpp
// 位置: 1172-1175, 2108-2113
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Runtime\Engine\Private\Actor.cpp
// 位置: 702-703, 1658-1659
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Runtime\CoreUObject\Public\UObject\ObjectMacros.h
// 位置: 156-160
// 说明: UEAS2 的 runtime-generated 语义已进入引擎主干分支条件
// ============================================================================
FUNC_RuntimeGenerated = 0x00000010,

virtual void RuntimeCallFunction(UObject* Object, FFrame& Stack, RESULT_DECL) { ensureAlways(false); }
virtual void RuntimeCallEvent(UObject* Object, void* Params) { ensureAlways(false); }
virtual UFunction* GetRuntimeValidateFunction() { ensureAlways(false); return nullptr; }

// Always call RuntimeCallFunction on the class that the function was generated in
Function->RuntimeCallFunction(this, Stack, RESULT_PARAM);

if (Function->FunctionFlags & FUNC_RuntimeGenerated)
{
	UFunction* ValidateFunction = Function->GetRuntimeValidateFunction();
	...
}

if ((Prop->AngelscriptPropertyFlags & APF_RuntimeGenerated) == 0)
	continue;

if ((Function->FunctionFlags & (FUNC_Native | FUNC_RuntimeGenerated)) == 0 && (Function->Script.Num() == 0))
	return;

PKG_RuntimeGenerated = 0x20000000,
```

[3] 当前插件的替代链则更局部：`UASClass` / `UASFunction` 自带 script 元数据，`ClassGenerator` 把 script function 伪装成标准 native thunk；world context 也由插件生成器补回。但这层语义不会自动进入 engine 主干所有分支：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h
// 位置: 14-30, 205-213
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 位置: 3290-3291, 3411-3429, 3521-3555
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp
// 位置: 736-769, 1931-1944
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp
// 位置: 984-988
// 说明: 当前插件把 script 语义收在子类与生成器里，再通过 native thunk 回接标准 UE 调用面
// ============================================================================
class UASClass : public UClass
{
	...
	void* ScriptTypePtr = nullptr;
	bool bIsScriptClass = false;
};

virtual void RuntimeCallFunction(UObject* Object, FFrame& Stack, RESULT_DECL);
ANGELSCRIPTRUNTIME_API void UASFunctionNativeThunk(UObject* Object, FFrame& Stack, RESULT_DECL);

NewClass->ClassFlags = CLASS_CompiledFromBlueprint;
NewClass->bIsScriptClass = true;

auto* NewFunction = UASFunction::AllocateFunctionFor(NewClass, FunctionName, FunctionDesc);
//NewFunction->FunctionFlags |= FUNC_RuntimeGenerated;
NewFunction->FunctionFlags |= FUNC_Native;
NewFunction->SetNativeFunc(&UASFunctionNativeThunk);

// Generate a hidden world context argument for all static functions by default
if (ParamIndex == -1)
{
	FProperty* Prop = AddFunctionArgument(NewFunction, ArgDesc, false);
	NewFunction->SetMetaData(NAME_Arg_WorldContext, *Prop->GetName());
	NewFunction->WorldContextIndex = FunctionDesc->Arguments.Num();
	NewFunction->bIsWorldContextGenerated = true;
}

if (WorldContextIndex == i)
	Arg.VMBehavior = EArgumentVMBehavior::WorldContextObject;

UASFunction* Function = Cast<UASFunction>(Stack.Node);
Function->RuntimeCallFunction(Object, Stack, RESULT_PARAM);

// Ignore runtime generated types (impossible?)
UASClass* asClass = Cast<UASClass>(Class);
if (asClass != nullptr && asClass->bIsScriptClass)
	return false;
```

### 差距判断

| 子 contract | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| 参数语义进入 UHT / property 主链 | `AngelscriptPropertyFlags` 直接进 UHT 与 generated code | 主要靠生成器、metadata 与 `FinalizeArguments()` 重建 | **实现方式不同** |
| 函数调用入口 | `FUNC_RuntimeGenerated + RuntimeCallFunction()` | `FUNC_Native + UASFunctionNativeThunk()` | **实现方式不同** |
| Engine/editor 默认识别 runtime-generated property/package/function | `APF_RuntimeGenerated` / `PKG_RuntimeGenerated` / 主干分支直接识别 | 插件树内未见等价主干 flag 接入；更多依赖 `Cast<UASClass>()` 的局部补偿 | **局部没有实现** |

因此，这一维最关键的新边界是：**当前插件已经替掉了“脚本函数如何被调用”“world context 如何补回”这两层 contract，但没有等价替掉 UEAS2 那层“引擎主干默认知道这些 runtime-generated 元素是什么”的广域接入。** 这正是为什么很多能力可以插件内复现，但 `Actor` / `K2` / `PropertyEditor` / package 行为层面的“一等公民感”仍然不可能完全等价。

---
## 深化分析 (2026-04-08 20:00:55)

### [维度 D2/D5] 当前插件把 bind 启动过程做成了“engine-scope state + 可观测回放”；UEAS2 仍是进程级静态表

```
[D2/D5-Deep] Bind Startup Ownership
UEAS2
├─ static BindArray                         // 进程级静态数组
├─ CallBinds()
│  ├─ Sort()
│  └─ execute every bind
└─ no per-engine state / no replay observation

AngelPortV2
├─ GetBindState()
│  ├─ current engine -> Engine.BindState   // 优先取当前 engine 的 bind 状态
│  └─ no scope -> LegacyBindState          // 仍保留无作用域兼容回退
├─ CallBinds(disabled set)
│  ├─ BeginObservationPass()
│  ├─ skip named binds
│  ├─ RecordExecutedBind()
│  └─ EndObservationPass()
└─ tests assert full-create replays binds, clone-create does not
```

前文已经确认当前插件把 `ClassFuncMaps`、skip list 等 bind 状态收到了 engine scope。本轮继续往下看，关键新增发现是：**当前插件把“启动 bind pass 是否执行、按什么顺序执行、clone engine 是否重放”都做成了可观测契约；UEAS2 仍然只是“有个静态数组，排完序后全跑一遍”。**

更细一点说，当前插件的迁移也不是绝对的纯 per-engine 模式。`GetBindState()` 在拿不到当前 engine 时仍会回退到 `LegacyBindState`。这说明它的真实策略是“**优先按 engine 隔离，但保留无作用域兼容入口**”，而不是彻底消灭全局状态。

[1] UEAS2 的 bind owner 仍是进程级静态数组，`CallBinds()` 只做排序与执行：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\AngelscriptBinds.cpp
// 位置: 17-44
// 说明: 上游 bind 生命周期仍然围绕进程级静态数组展开
// ============================================================================
struct FBindFunction
{
	int32 BindOrder;
	TFunction<void()> Function;
};

static TArray<FBindFunction>& GetBindArray()
{
	static TArray<FBindFunction> BindArray;
	return BindArray;
}

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

[2] 当前插件把同一条链拆成“按当前 engine 取 state + bind pass 可禁用 + bind pass 可观测”，但仍保留 `LegacyBindState` 兜底：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp
// 位置: 23-33, 48-75, 195-218
// 说明: 当前插件把 bind 状态收进 engine；如果当前没有 engine scope，才回退到 LegacyBindState
// ============================================================================
static FAngelscriptBindState& GetBindState()
{
	if (FAngelscriptEngine* Engine = FAngelscriptEngine::TryGetCurrentEngine())
	{
		if (FAngelscriptBindState* State = Engine->GetBindState())
		{
			return *State;
		}
	}
	static FAngelscriptBindState LegacyBindState;
	return LegacyBindState;
}

TMap<UClass*, TMap<FString, FFuncEntry>>& FAngelscriptBinds::GetClassFuncMaps()
{
	return GetBindState().ClassFuncMaps;
}

TMap<UClass*, TSet<FString>>& FAngelscriptBinds::GetSkipBinds()
{
	return GetBindState().SkipBinds;
}

void FAngelscriptBinds::CallBinds(const TSet<FName>& DisabledBindNames)
{
#if WITH_DEV_AUTOMATION_TESTS
	FAngelscriptBindExecutionObservation::BeginObservationPass(DisabledBindNames);
#endif

	for (const FBindFunction& Bind : GetSortedBindArray())
	{
		if (DisabledBindNames.Contains(Bind.BindName))
			continue;

#if WITH_DEV_AUTOMATION_TESTS
		// ★ 把这次 pass 实际执行了哪些 bind 记下来
		FAngelscriptBindExecutionObservation::RecordExecutedBind(Bind.BindName);
#endif
		Bind.Function();
	}

#if WITH_DEV_AUTOMATION_TESTS
	FAngelscriptBindExecutionObservation::EndObservationPass();
#endif
}
```

[3] 这条契约不是“留个日志”级别，而是有专门快照对象和多引擎测试把 replay 语义锁住：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Testing/AngelscriptBindExecutionObservation.cpp
// 位置: 13-29, 48-71
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineTests.cpp
// 位置: 615-639, 657-670, 718-735
// 说明: 当前插件明确测试 full engine 会 replay startup binds，而 clone engine 不会
// ============================================================================
void FAngelscriptBindExecutionObservation::Reset()
{
	FScopeLock Lock(&GAngelscriptBindExecutionObservationMutex);
	GAngelscriptBindExecutionSnapshot = FAngelscriptBindExecutionSnapshot();
}

int32 FAngelscriptBindExecutionObservation::GetInvocationCount()
{
	FScopeLock Lock(&GAngelscriptBindExecutionObservationMutex);
	return GAngelscriptBindExecutionSnapshot.InvocationCount;
}

void FAngelscriptBindExecutionObservation::BeginObservationPass(const TSet<FName>& DisabledBindNames)
{
	FScopeLock Lock(&GAngelscriptBindExecutionObservationMutex);
	++GAngelscriptBindExecutionSnapshot.InvocationCount;
	GAngelscriptBindExecutionSnapshot.DisabledBindNames = DisabledBindNames.Array();
	GAngelscriptBindExecutionSnapshot.DisabledBindNames.Sort(FNameLexicalLess());
}

const FAngelscriptBindExecutionSnapshot Snapshot = FAngelscriptBindExecutionObservation::GetLastSnapshot();
TestEqual(TEXT("MultiEngine.StartupBindObservation.FullCreateRecordsOrderedBinds should observe a single startup bind pass"), FAngelscriptBindExecutionObservation::GetInvocationCount(), 1);

TestEqual(TEXT("MultiEngine.StartupBindObservation.CloneCreateDoesNotReplayBinds should not observe a fresh startup bind pass for clone creation"), FAngelscriptBindExecutionObservation::GetInvocationCount(), 0);

TestEqual(TEXT("MultiEngine.StartupBindObservation.CreateForTestingFullFallbackReplaysBinds should observe one startup bind pass"), FAngelscriptBindExecutionObservation::GetInvocationCount(), 1);
```

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| bind registry owner | 进程级静态 `BindArray` | engine `BindState`，无 scope 时回退 `LegacyBindState` | **实现方式不同** |
| 启动 bind pass 是否可禁用/可观测 | 未见显式机制 | `DisabledBindNames + ObservationSnapshot` | **能力增强** |
| clone engine 是否重放 startup binds | 源码中未见等价概念 | 明确有 full / clone / fallback 三种测试语义 | **能力增强** |

这条差异最值得记录的不是“当前插件把 bind 搬到了 engine 里”，而是：**它把 bind pass 变成了一个可以回放、可以断言、可以区分 full/clone 行为的测试对象。** 这对 D2 的绑定稳定性和 D5 的开发体验都是真正的工程化增量。

### [维度 D1/D4] 当前插件专门为多引擎作用域引入了 context-stack snapshot/restore；UEAS2 仍假设 manager singleton 永远是唯一 owner

```
[D1/D4-Deep] Current Engine Resolution
UEAS2
├─ GAngelscriptManager
└─ FAngelscriptManager::Get()              // 默认全局唯一 owner

AngelPortV2
├─ FAngelscriptEngineScope
│  ├─ Push(engine)
│  └─ Pop(engine)
├─ TryGetCurrentEngine()
│  ├─ Peek context stack                   // 先看当前作用域
│  └─ fallback GameInstanceSubsystem       // 再退回宿主 subsystem
└─ tests use SnapshotAndClear/RestoreSnapshot
   └─ verify nested scope does not leak ambient engine
```

前文已经说明 session owner 从 `FAngelscriptManager` 迁到了 `FAngelscriptEngine`。这一轮新增的关键点是：**当前插件不是只把全局变量换了个类名，而是补出了一整套“作用域快照/恢复”语义，用来证明多引擎嵌套场景下不会把 ambient engine 泄漏到外层。**

这件事会直接影响热重载、调试、测试夹具和 clone engine：一旦当前 engine 解析还是“全局唯一”，这些场景就只能靠约定避免串扰；而不是像当前插件这样，靠明确的 stack contract 去隔离。

[1] UEAS2 的主入口仍然是 manager singleton：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\AngelscriptManager.cpp
// 位置: 120-123
// 说明: 上游默认存在一个全局 manager，调用方直接取 singleton
// ============================================================================
FAngelscriptManager& FAngelscriptManager::Get()
{
	checkf(GAngelscriptManager != nullptr, TEXT("Attempted to use angelscript manager before initialization. Make sure FAngelscriptCodeModule::InitializeAngelscript has been called."));
	return *GAngelscriptManager;
}
```

[2] 当前插件把“当前 engine”解析拆成 `context stack -> subsystem fallback` 两级，并显式提供 snapshot/restore：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 391-433, 718-729
// 说明: 当前插件把当前 engine 解析做成了作用域栈；测试环境还能快照后清空，再恢复
// ============================================================================
void FAngelscriptEngineContextStack::Push(FAngelscriptEngine* Engine)
{
	if (Engine != nullptr)
		GAngelscriptEngineContextStack.Add(Engine);
}

void FAngelscriptEngineContextStack::Pop(FAngelscriptEngine* Engine)
{
	ensureAlwaysMsgf(GAngelscriptEngineContextStack.Last() == Engine, TEXT("Angelscript engine context stack pop order mismatch."));
	if (GAngelscriptEngineContextStack.Last() == Engine)
		GAngelscriptEngineContextStack.Pop();
}

TArray<FAngelscriptEngine*> FAngelscriptEngineContextStack::SnapshotAndClear()
{
	TArray<FAngelscriptEngine*> Saved = MoveTemp(GAngelscriptEngineContextStack);
	GAngelscriptEngineContextStack.Empty();
	return Saved;
}

void FAngelscriptEngineContextStack::RestoreSnapshot(TArray<FAngelscriptEngine*>&& SavedStack)
{
	GAngelscriptEngineContextStack = MoveTemp(SavedStack);
}

FAngelscriptEngine* FAngelscriptEngine::TryGetCurrentEngine()
{
	if (FAngelscriptEngine* ScopedEngine = FAngelscriptEngineContextStack::Peek())
		return ScopedEngine;

	if (UAngelscriptGameInstanceSubsystem* Subsystem = UAngelscriptGameInstanceSubsystem::GetCurrent())
		return Subsystem->GetEngine();

	return nullptr;
}
```

[3] 这层语义已经被自动化测试钉住：先清空上下文，再验证嵌套 scope 的 push/pop 会正确恢复外层 engine：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp
// 位置: 46-56, 158-191
// 说明: 测试不信任 ambient state，先 snapshot/clear，再验证嵌套作用域恢复
// ============================================================================
struct FIsolationContextStackGuard
{
	TArray<FAngelscriptEngine*> SavedStack;
	FIsolationContextStackGuard()
	{
		SavedStack = FAngelscriptEngineContextStack::SnapshotAndClear();
	}
	~FIsolationContextStackGuard()
	{
		FAngelscriptEngineContextStack::RestoreSnapshot(MoveTemp(SavedStack));
	}
};

FIsolationContextStackGuard StackGuard;
TestTrue(TEXT("Context stack should start empty after guard clears it"), FAngelscriptEngineContextStack::IsEmpty());

{
	FAngelscriptEngineScope PrimaryScope(*PrimaryEngine);
	TestTrue(TEXT("Scoped resolution should return the primary engine while its scope is active"), &FAngelscriptEngine::Get() == PrimaryEngine.Get());

	{
		FAngelscriptEngineScope SecondaryScope(*SecondaryEngine);
		TestTrue(TEXT("Nested scoped resolution should prefer the nested engine"), &FAngelscriptEngine::Get() == SecondaryEngine.Get());
	}

	TestTrue(TEXT("Nested scope teardown should restore the previous engine"), &FAngelscriptEngine::Get() == PrimaryEngine.Get());
}
```

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| 当前 runtime owner 解析 | `GAngelscriptManager` singleton | `context stack + subsystem fallback` | **实现方式不同** |
| 嵌套引擎作用域恢复语义 | 未见等价抽象 | `SnapshotAndClear / RestoreSnapshot` + `FAngelscriptEngineScope` | **当前插件新增** |
| 多引擎测试隔离 | 未见同层级自动化约束 | 显式测试 ambient engine 不泄漏 | **能力增强** |

因此，这条链更准确的结论不是“当前插件支持多引擎”，而是：**它把“当前 engine 是谁”从一个默认成立的全局前提，改成了一个必须被 push/pop、可以被 snapshot/restore、而且能被测试验证的显式 contract。**

### [维度 D3] class-level mixin wrapper 在当前插件里已经变成“选择性保留”；`FHitResult` 是最典型的收缩案例

```
[D3-Deep] Wrapper-Based Mixin Policy
Hazelight
├─ FHitResult wrapper: ScriptMixin active
│  └─ helper UFUNCTIONs stay member-style
├─ direct Bind_FHitResult still exists
└─ wrapper layer is broadly enabled

AngelPortV2
├─ FHitResult wrapper: ScriptMixin removed
│  ├─ helper functions still exist as BlueprintCallable
│  └─ direct Bind_FHitResult keeps ctor/property surface
├─ RuntimeCurveLinearColor wrapper: ScriptMixin still active
└─ Bind_FunctionLibraryMixins.cpp only backfills selected hard cases
```

前文已经指出当前插件的 `ScriptMixin` 走向更“选择性保留”。这一轮把它落到具体 wrapper 后，可以更清楚地看出变化不是抽象口号，而是**按类型逐个收口**。

`FHitResult` 是最典型的例子。Hazelight 里它同时拥有两层 surface：

1. `Bind_FHitResult.cpp` 暴露 constructor / property。
2. `UAngelscriptHitResultLibrary` 作为 `ScriptMixin = "FHitResult"` 的 helper layer，再提供 `SetComponent()` / `GetActor()` 这类 member-style sugar。

当前插件保留了第 1 层，但把第 2 层的 `ScriptMixin` 注释掉了。也就是说，**底层能力没有完全丢，但 helper-style member 语法已经不是默认保证。**

[1] Hazelight 的 `FHitResult` helper wrapper 仍是激活状态，显式声明 `ScriptMixin = "FHitResult"`：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Public\FunctionLibraries\AngelscriptHitResultLibrary.h
// 位置: 8-31
// 说明: 上游把 FHitResult helper 显式做成 class-level mixin
// ============================================================================
UCLASS(Meta = (ScriptMixin = "FHitResult"))
class UAngelscriptHitResultLibrary : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(ScriptCallable, Meta = (ScriptTrivial))
	static void SetComponent(FHitResult& HitResult, UPrimitiveComponent* Component)
	{
		HitResult.Component = Component;
	}

	UFUNCTION(ScriptCallable, Meta = (ScriptTrivial))
	static void SetActor(FHitResult& HitResult, AActor* Actor)
	{
		HitResult.HitObjectHandle = FActorInstanceHandle(Actor);
	}

	UFUNCTION(ScriptCallable, Meta = (ScriptTrivial))
	static void Reset(FHitResult& HitResult, float InTime = 1.f, bool bPreserveTraceData = true)
	{
		HitResult.Reset(InTime, bPreserveTraceData);
	}
}
```

[2] 当前插件把同一个 wrapper 的 `ScriptMixin` 关掉了，但函数本体仍保留为 `BlueprintCallable`：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptHitResultLibrary.h
// 位置: 8-18, 23-39, 44-46
// 说明: wrapper 还在，但 class-level ScriptMixin 已被注释掉，helper 语法不再自动等同于 member mixin
// ============================================================================
//UCLASS(Meta = (ScriptMixin = "FHitResult"))
UCLASS(Meta = ())
class UAngelscriptHitResultLibrary : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Meta = ())
	static void SetComponent(FHitResult& HitResult, UPrimitiveComponent* Component)
	{
		HitResult.Component = Component;
	}

	UFUNCTION(BlueprintCallable, Meta = ())
	static void Reset(FHitResult& HitResult)
	{
		HitResult.Reset();
	}

	UFUNCTION(BlueprintCallable, Meta = ())
	static UPrimitiveComponent* GetComponent(const FHitResult& HitResult)
	{
		return HitResult.GetComponent();
	}
```

[3] 但 `FHitResult` 的底层数据 surface 仍然由手写 bind 保留；这说明差异在“helper-style member sugar”，不在“是否还有 FHitResult 能力”：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FHitResult.cpp
// 位置: 9-39
// 说明: 当前插件仍然直接绑定 FHitResult 的构造与核心字段
// ============================================================================
auto FHitResult_ = FAngelscriptBinds::ExistingClass("FHitResult");

FHitResult_.Constructor("void f(AActor InActor, UPrimitiveComponent InComponent, const FVector& HitLoc, const FVector& HitNorm)", [](FHitResult* Address, class AActor* InActor, class UPrimitiveComponent* InComponent, FVector const& HitLoc, FVector const& HitNorm)
{
	new(Address) FHitResult(InActor, InComponent, HitLoc, HitNorm);
});
FHitResult_.Constructor("void f(const FVector& TraceStart, const FVector& TraceEnd)", [](FHitResult* Address, FVector const& TraceStart, FVector const& TraceEnd)
{
	new(Address) FHitResult(TraceStart, TraceEnd);
});

FHitResult_.Property("int FaceIndex", &FHitResult::FaceIndex);
FHitResult_.Property("uint8 ElementIndex", &FHitResult::ElementIndex);
FHitResult_.Property("int Item", &FHitResult::Item);
FHitResult_.Property("float32 PenetrationDepth", &FHitResult::PenetrationDepth);
FHitResult_.Property("float32 Distance", &FHitResult::Distance);
FHitResult_.Property("FVector ImpactNormal", &FHitResult::ImpactNormal);
FHitResult_.Property("FVector ImpactPoint", &FHitResult::ImpactPoint);
FHitResult_.Property("FVector Location", &FHitResult::Location);
```

[4] 与之相对，当前插件并没有把所有 wrapper mixin 全部关掉；`FRuntimeCurveLinearColor` 仍然保留 active mixin，而且还专门用 `Bind_FunctionLibraryMixins.cpp` 做手工补位：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/RuntimeCurveLinearColorMixinLibrary.h
// 位置: 8-16
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FunctionLibraryMixins.cpp
// 位置: 7-31
// 说明: 当前插件保留的是“少量仍有必要的 wrapper mixin”，而不是一刀切全部移除
// ============================================================================
UCLASS(meta = (ScriptMixin = "FRuntimeCurveLinearColor"))
class ANGELSCRIPTRUNTIME_API URuntimeCurveLinearColorMixinLibrary : public UObject
{
	GENERATED_BODY()

	UFUNCTION(BlueprintCallable, Category = "Math|Curves")
	static void AddDefaultKey(FRuntimeCurveLinearColor& Target, float InTime, FLinearColor InColor)
	{
		Target.ColorCurves[0].AddKey(InTime, InColor.R);
		Target.ColorCurves[1].AddKey(InTime, InColor.G);
		Target.ColorCurves[2].AddKey(InTime, InColor.B);
		Target.ColorCurves[3].AddKey(InTime, InColor.A);
	}
}

AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_FunctionLibraryMixins((int32)FAngelscriptBinds::EOrder::Late + 110, []
{
	auto RuntimeCurveLinearColor_ = FAngelscriptBinds::ExistingClass("FRuntimeCurveLinearColor");
	RuntimeCurveLinearColor_.Method(
		"void AddDefaultKey(float32 InTime, FLinearColor InColor)",
		[](FRuntimeCurveLinearColor* Target, float InTime, const FLinearColor& InColor)
		{
			Target->ColorCurves[0].AddKey(InTime, InColor.R);
			Target->ColorCurves[1].AddKey(InTime, InColor.G);
			Target->ColorCurves[2].AddKey(InTime, InColor.B);
			Target->ColorCurves[3].AddKey(InTime, InColor.A);
		});

	FAngelscriptBinds::BindGlobalFunction(
		"void AddDefaultKey(FRuntimeCurveLinearColor& Target, float32 InTime, FLinearColor InColor)",
		[](FRuntimeCurveLinearColor& Target, float InTime, const FLinearColor& InColor)
		{
			URuntimeCurveLinearColorMixinLibrary::AddDefaultKey(Target, InTime, InColor);
		});
});
```

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| `FHitResult` helper wrapper | active `ScriptMixin`，helper 可直接作为 member surface | wrapper 仍在，但 `ScriptMixin` 已移除；主要退回 property/direct-bind surface | **实现方式不同 + helper 覆盖面收窄** |
| 直接类型能力是否消失 | 没有 | 也没有，`Bind_FHitResult.cpp` 仍保留 ctor/property | **没有缺失** |
| wrapper mixin 策略 | 较广泛启用 | 选择性保留，按类型逐个补位 | **实现方式不同** |

因此，`D3` 这条链更精确的新结论是：**当前插件并不是简单“还保留 mixin / 不保留 mixin”，而是把 wrapper-based member sugar 改成了按类型 opt-in 的策略。** `FHitResult` 这类 helper surface 已经明显收缩，而 `FRuntimeCurveLinearColor` 这类确实有额外价值的 wrapper 仍被保留并手工补位。这属于“**实现方式不同 + 局部覆盖面收窄**”，不是“Blueprint 交互整体不存在”。

---
## 深化分析 (2026-04-08 20:18:02)

### [维度 D4] 当前插件真正新增的不是“替代 Loader”，而是把 directory watcher 队列抽成了可单测 contract

```
[D4-Deep] Reload Trigger Ownership
UEAS2
├─ AngelscriptLoader.Build.cs                 // 只声明对 Code / Editor 的依赖
├─ AngelscriptEditorModule::OnScriptFileChanges()
│  └─ inline queue -> Manager.FileChangesDetectedForReload
└─ watcher queue 逻辑直接写在模块回调里

AngelPortV2
├─ no Loader module
├─ AngelscriptEditorModule::OnScriptFileChanges()
│  └─ QueueScriptFileChanges(...)
│     ├─ GatherLoadedScriptsForFolder(...)
│     └─ queue -> Engine.FileChangesDetectedForReload
└─ AngelscriptDirectoryWatcherTests
   └─ 验证 add / remove / folder queue 语义
```

前文已经说明 `Loader` 只是一层启动转发。本轮再往下看，能更准确地回答用户给的 D4 问题：**`Loader` 从来就不是热重载触发 owner。** UEAS2 真正负责 watcher 注册和文件变化入队的，一直是 `AngelscriptEditorModule`；当前插件移除 `Loader` 后，这条链并没有“另起炉灶”，而是把 watcher queue 抽成了独立 helper，并把它做成了可单测输入面。

也就是说，这里的差距不是“UEAS2 有 Loader，所以热重载能力更强”，而是“UEAS2 把 reload 输入逻辑内联在 editor module；当前插件把同一层逻辑拆成可复用、可验证的 contract”。这属于 **实现质量差异**，不是 **是否存在替代方案** 的差异。

[1] UEAS2 的 `Loader` Build 依赖只把自己挂到 `AngelscriptCode / AngelscriptEditor` 上，没有任何 `DirectoryWatcher` 或 reload 队列职责：

```csharp
// ============================================================================
// 文件: References.HazelightAngelscriptEngineRoot/Engine/Plugins/Angelscript/Source/AngelscriptLoader/AngelscriptLoader.Build.cs
// 位置: 6-25
// 说明: Loader 模块只做依赖桥接；从 Build 依赖看不出它拥有 reload trigger 责任
// ============================================================================
public class AngelscriptLoader : ModuleRules
{
	public AngelscriptLoader(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"Engine",
			"AngelscriptCode",
		});

		if (Target.bBuildEditor)
		{
			PublicDependencyModuleNames.AddRange(new string[] 
			{
				"AngelscriptEditor", 
			});
		}
	}
}
```

[2] UEAS2 真正的 watcher 注册和 `.as` 变化入队都写在 `AngelscriptEditorModule.cpp` 里，而不是 `Loader`：

```cpp
// ============================================================================
// 文件: References.HazelightAngelscriptEngineRoot/Engine/Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 函数: OnScriptFileChanges / FAngelscriptEditorModule::StartupModule
// 位置: 47-63, 361-383
// 说明: UEAS2 的 reload 输入面直接内联在 editor module 回调中
// ============================================================================
void OnScriptFileChanges(const TArray<FFileChangeData>& Changes)
{
	// ★ reload trigger 在 EditorModule，不在 Loader
	if (!FAngelscriptManager::IsInitialized())
		return;

	for (const FFileChangeData& Change : Changes)
	{
		FString AbsolutePath = FPaths::ConvertRelativePathToFull(Change.Filename);
		for (const auto& RootPath : FAngelscriptManager::Get().AllRootPaths)
		{
			if (AbsolutePath.StartsWith(RootPath))
			{
				// ★ 文件变化直接写入 manager 的 reload 队列
				AngelscriptManager.FileChangesDetectedForReload.AddUnique(
					FAngelscriptManager::FFilenamePair{ AbsolutePath, RelativePath });
			}
		}
	}
}

void FAngelscriptEditorModule::StartupModule()
{
	// ★ watcher 注册同样发生在 EditorModule
	FDirectoryWatcherModule& DirectoryWatcherModule = FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>("DirectoryWatcher");
	IDirectoryWatcher* DirectoryWatcher = DirectoryWatcherModule.Get();
	if (ensure(DirectoryWatcher != nullptr))
	{
		TArray<FString> AllRootPaths = FAngelscriptManager::MakeAllScriptRoots();
		for (const auto& RootPath : AllRootPaths)
		{
			DirectoryWatcher->RegisterDirectoryChangedCallback_Handle(
				*RootPath,
				IDirectoryWatcher::FDirectoryChanged::CreateStatic(&OnScriptFileChanges),
				WatchHandle,
				IDirectoryWatcher::IncludeDirectoryChanges);
		}
	}
}
```

[3] 当前插件沿用同一层 owner，但把回调拆成 `QueueScriptFileChanges()`，让 queue 语义不再被锁死在模块回调里：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 函数: OnScriptFileChanges / FAngelscriptEditorModule::StartupModule
// 位置: 78-93, 351-381
// 说明: 当前插件仍由 EditorModule 接管 watcher，但 queue 逻辑已外提
// ============================================================================
void OnScriptFileChanges(const TArray<FFileChangeData>& Changes)
{
	if (!FAngelscriptEngine::IsInitialized())
		return;

	FAngelscriptEngine& AngelscriptManager = FAngelscriptEngine::Get();
	AngelscriptEditor::Private::QueueScriptFileChanges(
		Changes,
		AngelscriptManager.AllRootPaths,
		AngelscriptManager,
		IFileManager::Get(),
		[&AngelscriptManager](const FString& AbsoluteFolderPath)
		{
			// ★ 目录展开逻辑也抽成可替换依赖
			return AngelscriptEditor::Private::GatherLoadedScriptsForFolder(AngelscriptManager, AbsoluteFolderPath);
		});
}

void FAngelscriptEditorModule::StartupModule()
{
	FDirectoryWatcherModule& DirectoryWatcherModule = FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>("DirectoryWatcher");
	IDirectoryWatcher* DirectoryWatcher = DirectoryWatcherModule.Get();
	if (ensure(DirectoryWatcher != nullptr))
	{
		TArray<FString> AllRootPaths = FAngelscriptEngine::MakeAllScriptRoots();
		for (const auto& RootPath : AllRootPaths)
		{
			DirectoryWatcher->RegisterDirectoryChangedCallback_Handle(
				*RootPath,
				IDirectoryWatcher::FDirectoryChanged::CreateStatic(&OnScriptFileChanges),
				WatchHandle,
				IDirectoryWatcher::IncludeDirectoryChanges);
		}
	}
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.h
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp
// 位置: 8-13; 75-102
// 说明: 当前插件把 watcher queue 暴露成可测试 API，并用自动化测试锁住行为
// ============================================================================
namespace AngelscriptEditor::Private
{
	ANGELSCRIPTEDITOR_API void QueueScriptFileChanges(
		const TArray<FFileChangeData>& Changes,
		const TArray<FString>& RootPaths,
		FAngelscriptEngine& Engine,
		IFileManager& FileManager,
		const FEnumerateLoadedScripts& EnumerateLoadedScripts);
}

bool FAngelscriptDirectoryWatcherScriptQueueTest::RunTest(const FString& Parameters)
{
	TUniquePtr<FAngelscriptEngine> Engine = MakeTestEngineWithRoot(RootPath);
	const TArray<FFileChangeData> Changes = {
		MakeFileChange(AddedAbsolutePath, FFileChangeData::FCA_Added),
		MakeFileChange(RemovedAbsolutePath, FFileChangeData::FCA_Removed)
	};

	AngelscriptEditor::Private::QueueScriptFileChanges(Changes, Engine->AllRootPaths, *Engine, FileManager, ...);

	// ★ 不再靠人工改脚本试一遍，而是直接断言 reload 队列输入
	TestEqual(TEXT("DirectoryWatcher.Queue.ScriptAddAndRemove should queue one added script"), Engine->FileChangesDetectedForReload.Num(), 1);
	return TestTrue(TEXT("DirectoryWatcher.Queue.ScriptAddAndRemove should store the removed script pair"), ...);
}
```

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| `Loader` 是否直接参与 reload trigger | 没有；只负责依赖桥接与启动转发 | 已移除 | **实现方式不同** |
| watcher queue 入口 | `AngelscriptEditorModule.cpp` 内联 | 抽到 `AngelscriptDirectoryWatcherInternal.*` | **能力增强** |
| reload 输入是否可单测 | 未见专门 directory watcher 测试 | `AngelscriptDirectoryWatcherTests.cpp` 直接覆盖 | **实现质量差异** |

因此，D4 这一条更准确的新结论是：**当前插件对 UEAS2 的真正替代，不是“拿什么顶掉 Loader”，而是把原本就不属于 Loader 的 watcher queue 逻辑拆成了可替换、可验证、可回归的 editor-side contract。**

### [维度 D5] `DebugServerVersion` 不是唯一协商轴；两边都保留了 `DebugAdapterVersion -> CallStack payload` 的双层兼容语义

```
[D5-Deep] Debug Capability Negotiation
Client
├─ StartDebugging{DebugAdapterVersion}
└─ expects DebugServerVersion{2}

Server (UEAS2 / AngelPortV2)
├─ store AngelscriptDebugServer::DebugAdapterVersion
├─ reply DebugServerVersion = 2
└─ when building CallStack
   ├─ if DebugAdapterVersion >= 1 -> emit ModuleName
   └─ else -> omit ModuleName
```

前文已经把 `DebugServerVersion == 2` 和主消息枚举对齐钉住了。本轮新增的点是：**真正决定 payload 细节的，不只有 `DebugServerVersion`，还包括 `StartDebugging` 里客户端主动上报的 `DebugAdapterVersion`。** 两边都先保存 adapter version，再在后续 `CallStack` 序列化里按 `>= 1` 决定是否填 `ModuleName`。这说明当前插件保留的不只是消息名，而是更细一级的“字段级兼容策略”。

这个点很重要，因为它直接影响“旧 adapter 能不能连上新 runtime”。如果只有 `DebugServerVersion` 一个版本号，server 端只能按单一 schema 发包；而这里是双层协商，server version 描述协议代际，adapter version 描述客户端能吃下多少字段。当前插件把这套老语义原样保住了。

[1] 两边的 `StartDebugging` 处理逻辑完全同构：先吃掉 `DebugAdapterVersion`，再回发 `DebugServerVersion = 2`：

```cpp
// ============================================================================
// 文件: References.HazelightAngelscriptEngineRoot/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Debugging/AngelscriptDebugServer.cpp
// 函数: FAngelscriptDebugServer::HandleMessage
// 位置: 787-803
// 说明: UEAS2 的握手不是单一版本号，而是 adapter version + server version 双轨
// ============================================================================
else if (MessageType == EDebugMessageType::StartDebugging)
{
	FStartDebuggingMessage Msg;
	*Datagram << Msg;

	bIsDebugging = true;
	// ★ 先记住客户端适配器能力
	AngelscriptDebugServer::DebugAdapterVersion = Msg.DebugAdapterVersion;

	FDebugServerVersionMessage DebugServerVersionMessage;
	DebugServerVersionMessage.DebugServerVersion = DEBUG_SERVER_VERSION;
	// ★ 再回发 server protocol version
	SendMessageToClient(Client, EDebugMessageType::DebugServerVersion, DebugServerVersionMessage);
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 函数: FAngelscriptDebugServer::HandleMessage
// 位置: 897-913
// 说明: 当前插件保留了完全相同的双轨协商顺序
// ============================================================================
else if (MessageType == EDebugMessageType::StartDebugging)
{
	FStartDebuggingMessage Msg;
	*Datagram << Msg;

	bIsDebugging = true;
	// ★ 适配器版本仍由客户端在 StartDebugging 中声明
	AngelscriptDebugServer::DebugAdapterVersion = Msg.DebugAdapterVersion;

	FDebugServerVersionMessage DebugServerVersionMessage;
	DebugServerVersionMessage.DebugServerVersion = DEBUG_SERVER_VERSION;
	SendMessageToClient(Client, EDebugMessageType::DebugServerVersion, DebugServerVersionMessage);
}
```

[2] 两边的 `CallStack` 构造也都按 `DebugAdapterVersion >= 1` 条件化填充 `ModuleName`，说明这不是偶然兼容，而是同一份 payload 协商策略：

```cpp
// ============================================================================
// 文件: References.HazelightAngelscriptEngineRoot/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Debugging/AngelscriptDebugServer.cpp
// 函数: FAngelscriptDebugServer::SendCallStackToClient
// 位置: 1309-1349
// 说明: UEAS2 只在 adapter version >= 1 时发送 ModuleName
// ============================================================================
Frame.Source = FString::Printf(TEXT("::%s"), *Function->GetOuter()->GetName());
if (AngelscriptDebugServer::DebugAdapterVersion >= 1)
	Frame.ModuleName = TEXT("");

...

Frame.Source = SectionName ? ANSI_TO_TCHAR(SectionName) : TEXT("");
if (AngelscriptDebugServer::DebugAdapterVersion >= 1)
	Frame.ModuleName = ScriptFunction->GetModuleName() ? ANSI_TO_TCHAR(ScriptFunction->GetModuleName()) : TEXT("");
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 函数: FAngelscriptDebugServer::SendCallStackToClient
// 位置: 1434-1474
// 说明: 当前插件逐字段保留了相同的 adapter capability gate
// ============================================================================
Frame.Source = FString::Printf(TEXT("::%s"), *Function->GetOuter()->GetName());
if (AngelscriptDebugServer::DebugAdapterVersion >= 1)
	Frame.ModuleName = TEXT("");

...

Frame.Source = SectionName ? ANSI_TO_TCHAR(SectionName) : TEXT("");
if (AngelscriptDebugServer::DebugAdapterVersion >= 1)
	Frame.ModuleName = ScriptFunction->GetModuleName() ? ANSI_TO_TCHAR(ScriptFunction->GetModuleName()) : TEXT("");
```

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| `DebugServerVersion` | `2` | `2` | **一致** |
| adapter capability 上报 | `StartDebugging.DebugAdapterVersion` | 同样保留 | **一致** |
| `CallStack.ModuleName` 何时发送 | `DebugAdapterVersion >= 1` | 同样按该条件发送 | **一致** |

因此，D5 这一轮的新结论不是“V2 继续兼容”这么泛，而是：**当前插件连 `DebugAdapterVersion` 这层更细的 payload 协商都没有改写；它保留的是一整套双层版本语义，而不是只保留了一个 `DebugServerVersion` 常量。**

### [维度 D8/D11] 当前插件不是简单“没有 `AngelscriptPropertyFlags`”；它把上游集中式语义传输链拆成了插件内三段补偿

```
[D8/D11-Deep] Semantics Transport
UEAS2
├─ UHT specifier/parser
│  ├─ ScriptCallable
│  └─ AngelscriptPropertyFlags(WorldContext / CppRef / CppConst / ...)
├─ CodeGen emits original C++ declaration hints
├─ FProperty stores AngelscriptPropertyFlags
└─ Engine/Editor branches on APF_RuntimeGenerated directly

AngelPortV2
├─ AngelscriptUHTTool scans BlueprintCallable / BlueprintPure only
├─ Helper_PropertyBind reads regular metadata
├─ ClassGenerator may synthesize hidden _World_Context
├─ Helper_FunctionSignature writes __WorldContext() defaults / traits
└─ manual binds call SetPreviousBindRequiresWorldContext(true)
```

前文已经讲过 UEAS2 有 `AngelscriptPropertyFlags`。本轮新增的是替代路径的拆解：**当前插件并不是简单“没有 engine patch，所以少一层能力”；它是把上游那条集中式 UHT -> generated code -> `FProperty` -> editor/runtime 的传输链，拆成了插件内三段局部补偿。**

这三段分别是：

| 补偿层 | 当前插件证据 | 作用 |
| --- | --- | --- |
| UHT 导出层 | `AngelscriptFunctionTableExporter.cs` | 只导出 `BlueprintCallable / BlueprintPure`，不再把 `ScriptCallable` 写进引擎 UHT |
| 生成器层 | `AngelscriptClassGenerator.cpp` + `Helper_FunctionSignature.h` | 自动推导 / 生成 world context 参数，并把默认值写成 `__WorldContext()` |
| 手写 bind 层 | `Helper_PropertyBind.h` + `SetPreviousBindRequiresWorldContext(true)` | 用 regular metadata 和 bind trait 重建 script 可见性 / world-context 语义 |

这回答了“UEAS2 的 UHT 修改意味着什么、当前插件如何不依赖引擎修改达到类似能力”：UEAS2 是**集中式主干传输**，当前插件是**分布式插件内重建**。前者的一致性更强，后者的可移植性更强。

另外，用户给的“10 个 C# + 22 个 C++ 文件”里，前半句在当前快照下是成立的：按 `AS FIX|AngelscriptPropertyFlags|ScriptCallable` 检索，UEAS2 的 UHT 触点正好是 `10` 个 `.cs` 文件；但后半句明显偏小。按 `AS FIX|HAZE FIX|AngelscriptPropertyFlags|bIsScriptClass|FUNC_RuntimeGenerated|BindComponent property to Angelscript|ScriptCallable` 检索 `CoreUObject / Engine / Editor`，本地快照至少命中 `82` 个 `.cpp/.h` 文件。这意味着引擎侧改造的真实影响面远大于“22 个核心补丁文件”的直觉。

[1] UEAS2 在 UHT 层直接引入 `ScriptCallable` 和 `AngelscriptPropertyFlags`，并在解析阶段给 `WorldContext` 之类的语义打旗标：

```csharp
// ============================================================================
// 文件: References.HazelightAngelscriptEngineRoot/Engine/Source/Programs/Shared/EpicGames.UHT/Specifiers/UhtFunctionSpecifiers.cs
// 位置: 23-29
// 文件: References.HazelightAngelscriptEngineRoot/Engine/Source/Programs/Shared/EpicGames.UHT/Types/UhtProperty.cs
// 位置: 759-763
// 文件: References.HazelightAngelscriptEngineRoot/Engine/Source/Programs/Shared/EpicGames.UHT/Parsers/UhtFunctionParser.cs
// 位置: 583-586
// 说明: 上游在 UHT 主链里直接写入 Angelscript 语义，而不是留给插件后处理
// ============================================================================
[UhtSpecifier(Extends = UhtTableNames.Function, ValueType = UhtSpecifierValueType.Legacy)]
private static void ScriptCallableSpecifier(UhtSpecifierContext specifierContext)
{
	UhtFunction function = (UhtFunction)specifierContext.Type;
	function.MetaData.Add("ScriptCallable", "");
}

public EAngelscriptPropertyFlags AngelscriptPropertyFlags { get; set; }

if (Property.SourceName == WorldContextArgName)
{
	// ★ 参数语义在 parser 阶段就已经变成结构化 flag
	Property.AngelscriptPropertyFlags |= EAngelscriptPropertyFlags.WorldContext;
}
```

[2] 这些 flag 会继续被 UHT codegen 和 runtime/editor 主链消费，而不是停留在 metadata 文本层：

```csharp
// ============================================================================
// 文件: References.HazelightAngelscriptEngineRoot/Engine/Source/Programs/Shared/EpicGames.UHT/Exporters/CodeGen/UhtHeaderCodeGeneratorCppFile.cs
// 位置: 2548-2575
// 说明: UHT 生成器会根据 AngelscriptPropertyFlags 恢复 ref/const/enum-as-byte 的原始 C++ 形状
// ============================================================================
bool bIsRef = Property.AngelscriptPropertyFlags.HasFlag(EAngelscriptPropertyFlags.CppRef);
bool bIsConst = Property.AngelscriptPropertyFlags.HasFlag(EAngelscriptPropertyFlags.CppConst);

if (Property is UhtByteProperty ByteProp && ByteProp.Enum != null && ByteProp.AngelscriptPropertyFlags.HasFlag(EAngelscriptPropertyFlags.CppEnumAsByte))
{
	builder.Append("TEnumAsByte<");
	builder.Append(ByteProp.Enum.CppType);
	builder.Append(">");
}
```

```cpp
// ============================================================================
// 文件: References.HazelightAngelscriptEngineRoot/Engine/Source/Runtime/CoreUObject/Public/UObject/ObjectMacros.h
// 位置: 482-491
// 文件: References.HazelightAngelscriptEngineRoot/Engine/Source/Runtime/CoreUObject/Public/UObject/UnrealType.h
// 位置: 185-188
// 文件: References.HazelightAngelscriptEngineRoot/Engine/Source/Runtime/Engine/Private/Actor.cpp
// 位置: 697-703
// 文件: References.HazelightAngelscriptEngineRoot/Engine/Source/Editor/PropertyEditor/Private/PropertyNode.cpp
// 位置: 440-441
// 说明: flag 最终进入 FProperty，并被 runtime/editor 主干直接消费
// ============================================================================
enum EAngelscriptPropertyFlags : uint16
{
	APF_CppConst = 0x0001,
	APF_CppRef = 0x0002,
	APF_CppEnumAsByte = 0x0004,
	APF_RuntimeGenerated = 0x0008,
	APF_WorldContext = 0x0010,
	APF_ConstTemplateArg = 0x0020,
};

EPropertyFlags PropertyFlags;
uint16 AngelscriptPropertyFlags;

if ((Prop->AngelscriptPropertyFlags & APF_RuntimeGenerated) == 0)
	continue;

SetNodeFlags(EPropertyNodeFlags::NoCacheAddress, MyProperty && (MyProperty->AngelscriptPropertyFlags & APF_RuntimeGenerated));
```

[3] 当前插件的替代链则是插件内分散重建：UHTTool 只看 `BlueprintCallable / BlueprintPure`，生成器自动补 world context，helper 再把默认值和 trait 写回脚本函数：

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs
// 位置: 56-63
// 说明: 当前插件不再让引擎 UHT 认识 ScriptCallable，而是只导出 BlueprintCallable / Pure
// ============================================================================
internal static bool IsBlueprintCallable(UhtFunction function)
{
	string functionFlags = function.FunctionFlags.ToString();

	return function.FunctionType == UhtFunctionType.Function &&
		(functionFlags.Contains("BlueprintCallable", StringComparison.Ordinal) ||
		functionFlags.Contains("BlueprintPure", StringComparison.Ordinal));
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 位置: 651-656, 3521-3555
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h
// 位置: 223-231, 423-429
// 说明: world context 语义由生成器与 helper 在插件内补齐，而不是进引擎 FProperty 主链
// ============================================================================
if (Type.Type->IsObjectPointer() && ArgDesc.ArgumentName.Equals(STR_Arg_WorldContext, ESearchCase::IgnoreCase))
{
	if (!FunctionDesc->Meta.Contains(NAME_Arg_WorldContext))
		FunctionDesc->Meta.Add(NAME_Arg_WorldContext, ArgDesc.ArgumentName);
}

if (FunctionDesc->bIsStatic)
{
	// ★ 如果静态函数没有 world context，就在生成阶段补一个隐藏参数
	FAngelscriptArgumentDesc ArgDesc;
	ArgDesc.ArgumentName = TEXT("_World_Context");
	...
	NewFunction->WorldContextIndex = FunctionDesc->Arguments.Num();
	NewFunction->bIsWorldContextGenerated = true;
}

const FString& WorldContextParam = Function->GetMetaData(NAME_Signature_WorldContext);
if (WorldContextParam.Len() != 0)
{
	ArgumentDefaults[ArgIndex] = TEXT("__WorldContext()");
	WorldContextArgument = ArgIndex;
}

if (WorldContextArgument != -1)
{
	ScriptFunction->hiddenArgumentIndex = WorldContextArgument;
	ScriptFunction->hiddenArgumentDefault = "__WorldContext()";
	ScriptFunction->traits.SetTrait(asTRAIT_USES_WORLDCONTEXT, true);
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_PropertyBind.h
// 位置: 27-43
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_SystemTimers.cpp
// 位置: 21-31
// 说明: 其余 script-facing 语义继续靠 regular metadata 和 manual bind trait 重建
// ============================================================================
const bool bHasScriptEditOnly = Property->HasMetaData(NAME_ScriptEditOnly);
const bool bHasScriptReadOnly = Property->HasMetaData(NAME_ScriptReadOnly);
const bool bHasScriptReadWrite = Property->HasMetaData(NAME_ScriptReadWrite);
const bool bHasNotInAngelscript = Property->HasMetaData(NAME_NotInAngelscript);

if (bHasNotInAngelscript)
{
	Params.bCanRead = false;
	Params.bCanWrite = false;
	Params.bCanEdit = false;
}

return UKismetSystemLibrary::K2_IsTimerPausedHandle(FAngelscriptEngine::TryGetCurrentWorldContextObject(), Handle);
FAngelscriptBinds::SetPreviousBindRequiresWorldContext(true);
```

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| 语义入口 | UHT 主链直接认识 `ScriptCallable` / `AngelscriptPropertyFlags` | 插件内 `UHTTool + generator + helper + manual bind` 分布式重建 | **实现方式不同** |
| world context 传递 | parser 直接给 property/function 打 flag | 自动识别参数名、必要时补 `_World_Context`、再写 `hiddenArgumentDefault` | **实现方式不同** |
| property/script 可见性 | `FProperty.AngelscriptPropertyFlags` 可被主干直接消费 | 主要靠 metadata 和 bind helper 重建 | **局部没有实现主干同等能力** |
| 集成影响面 | `10` 个 UHT `.cs` + 至少 `82` 个 engine/editor/coreuobject 触点 | 影响面主要收敛在插件目录 | **可移植性增强，但一致性依赖分散规则** |

因此，D8/D11 这一轮最值得保留的新结论是：**当前插件并不是“没有引擎补丁所以只能降级”，而是用分布式插件规则换掉了 UEAS2 的集中式主干传输链。** 这让插件化迁移成为可能，但代价是语义来源被拆散了，后续任何 `ScriptEditOnly` / world-context / callable 可见性问题，都不能只查一个统一 flag 面，而要沿 `UHTTool -> generator -> helper -> manual bind` 四层去追。

---

## 深化分析 (2026-04-08 23:19:13)

### [维度 D2] `Bind_*.cpp` 文件数在当前插件里已经不再等价于“总绑定覆盖面”

```
[D2-Deep] Coverage Accounting
Hazelight
├─ handwritten Bind_*.cpp
├─ static bind array
└─ file count ~= manual callable surface

AngelPortV2
├─ handwritten Bind_*.cpp                       // 人工精细绑定
├─ AngelscriptUHTTool
│  ├─ scan BlueprintCallable / Pure
│  ├─ emit AS_FunctionTable_*.cpp shards
│  └─ write summary json/csv into Intermediate/UHT
├─ reflective fallback markers in ClassFuncMaps
└─ tests assert generated coverage > legacy handwritten baseline
```

前文已经把 `112 vs 123` 的手写 `Bind_*.cpp` 差异钉住了；这一轮补的是**统计口径本身为什么已经失真**。  

当前插件里，`Bind_*.cpp` 更像“人工精细绑定层”的规模，而不是“脚本可调用总表面积”的规模。真正把大量 `BlueprintCallable / BlueprintPure` 暴露进 `ClassFuncMaps` 的，是 `AngelscriptUHTTool` 在构建期生成的 `AS_FunctionTable_*.cpp` 分片；这些产物默认写到 `Intermediate/Build/.../UHT`，本来就不在 `Source/` 树里。也就是说，**继续拿 `Bind_*.cpp` 文件数去估算 API 覆盖面，会系统性低估当前插件。**

[1] `AngelscriptUHTTool` 的输出目标就是“模块扫描 -> shard 生成 -> summary 落盘”，而不是再往 `Bind_*.cpp` 里堆文件：

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs
// 位置: 49-76, 166-215, 282-325
// 说明: 当前插件把大批 BlueprintCallable 绑定转移到构建期生成，不再由手写 Bind_*.cpp 计数代表全貌
// ============================================================================
private const int MaxEntriesPerShard = 256;

public static int Generate(IUhtExportFactory factory)
{
	int generatedFileCount = 0;
	HashSet<string> generatedPaths = new(StringComparer.OrdinalIgnoreCase);
	List<AngelscriptModuleGenerationSummary> moduleSummaries = new();

	foreach (UhtModule module in factory.Session.Modules)
	{
		...
		generatedFileCount += moduleSummary.ShardCount;
		moduleSummaries.Add(moduleSummary);
	}

	DeleteStaleOutputs(factory, generatedPaths);
	WriteGenerationSummary(factory, moduleSummaries, csvEntries, generatedFileCount);
	WriteCoverageDiagnostics(moduleSummaries);
	return generatedFileCount;
}

private static void WriteGenerationSummary(...)
{
	string summaryPath = factory.MakePath("AS_FunctionTable_Summary", ".json");
	...
	// ★ 生成结果会写成 summary json/csv，而不是只留在源码树里
	Console.WriteLine(
		"AngelscriptUHTTool generated {0} binding entries ({1} direct, {2} stubs) across {3} modules and {4} shard files. Summary: {5}",
		totalGeneratedEntries,
		totalDirectBindEntries,
		totalStubEntries,
		moduleSummaries.Count,
		generatedFileCount,
		summaryPath);
}

private static StringBuilder BuildShard(...)
{
	builder.Append("AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_AS_FunctionTable_")
		.Append(moduleShortName)
		.Append('_')
		.Append(shardIndex.ToString("D3"))
		.AppendLine("((int32)FAngelscriptBinds::EOrder::Late + 50, []()");

	// ★ 每个 shard 都注册一批 BlueprintCallable，而不是新增一个手写 Bind_*.cpp
	builder.Append("\tUE_LOG(Angelscript, Log, TEXT(\"[UHT] Registered %d generated BlueprintCallable entries for module %s shard %d/%d\"), ")
		.Append(entryCount)
		.Append(", TEXT(\"")
		.Append(moduleShortName)
		.Append("\"), ")
		.Append(shardIndex + 1)
		.Append(", ")
		.Append(shardCount)
		.AppendLine(");");
}
```

[2] 当前插件自己的测试也明确把“生成表覆盖面”当成一等指标，而不是再盯 `Bind_*.cpp` 数量：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp
// 位置: 160-195, 367-427, 461-530
// 说明: 自动化测试直接把 generated table 的规模和 summary 产物锁成回归基线
// ============================================================================
const TMap<UClass*, TMap<FString, FFuncEntry>>& ClassFuncMaps = FAngelscriptBinds::GetClassFuncMaps();
int32 TotalFunctionEntryCount = 0;
for (const TPair<UClass*, TMap<FString, FFuncEntry>>& ClassEntry : ClassFuncMaps)
{
	TotalFunctionEntryCount += ClassEntry.Value.Num();
}

// ★ 断言 generated table 规模明显超过 legacy handwritten baseline
if (!TestTrue(TEXT("Generated function table startup pass should populate many ClassFuncMaps entries beyond the legacy handwritten baseline"), TotalFunctionEntryCount > 1000))
	...

// ★ direct / reflective / unresolved 都要进入统计，而不是简单看文件数
if (!TestTrue(TEXT("Generated function table stats should report at least one reflective fallback binding"), ReflectiveCount > 0))
	...

const FString GeneratedDirectory = FPaths::Combine(
	FPaths::ProjectPluginsDir(),
	TEXT("Angelscript"),
	TEXT("Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT"));
const FString SummaryPath = FPaths::Combine(GeneratedDirectory, TEXT("AS_FunctionTable_Summary.json"));

// ★ summary 本身就是 Intermediate/UHT 构建产物，天然不在 Source/ 统计里
if (!TestTrue(TEXT("Generated function table summary test should find the UHT summary json output"), FFileHelper::LoadFileToString(SummaryJson, *SummaryPath)))
	...

TestEqual(TEXT("Generated function table summary test should match the generated binding registration count"), TotalGeneratedEntries, CountedRegistrations);
```

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| `Bind_*.cpp` 文件数是否仍接近总 API surface | 基本仍接近手写主绑定面 | 只能代表“人工绑定层”，总面还要加 `AS_FunctionTable_* + reflective fallback` | **实现方式不同** |
| 绑定覆盖面的可审计性 | 主要依赖手写绑定目录与运行时注册 | `summary json/csv + ClassFuncMaps 统计 + reflective fallback 分布` | **能力增强** |
| “125 vs 123” 是否足以判断差距 | 口径尚可作为近似 | 明显不够，尤其会低估当前插件覆盖面 | **统计方法失效，不是能力缺失** |

因此，D2 本轮最重要的新结论是：**当前插件的 `Bind_*.cpp` 数量只能衡量“手工 curated surface”，不能再拿来近似总代码暴露面。** 真正的差异已经从“少/多几个 bind 文件”转成“手写绑定 + 构建期 function table + 反射 fallback”的三层覆盖体系。

### [维度 D3] 当前插件把 Hazelight 的“RPC 也是 event wrapper”语义移植到了标准 UE RPC specifier 路径

```
[D3-Deep] Blueprint Event + RPC Contract
script UFUNCTION specifiers
├─ Preprocessor
│  ├─ mark bBlueprintEvent
│  ├─ optionally mark bNet*
│  ├─ generate wrapper -> __Evt_Execute(this, StaticName)
│  └─ rename script impl -> *_Implementation
├─ ClassGenerator
│  ├─ set FUNC_BlueprintEvent
│  └─ set FUNC_NetMulticast / FUNC_NetClient / FUNC_NetServer
└─ reload analysis
   └─ adding a new BlueprintEvent still escalates to FullReloadRequired
```

前文主要在讲 `BlueprintOverride` 继承链、`ScriptMixin` 折叠和 event bridge 入口。本轮新增的点是：**当前插件并不是简单照抄 UEAS2 的 stock-engine 限制，而是在 preprocessor 层把 Hazelight 的 “network call 也是 event thunk” 语义，平移到了标准 UE `NetMulticast / NetServer / NetClient` 路径。**

这个差异非常具体：

1. UEAS2 的 `WITH_ANGELSCRIPT_HAZE` 分支里，`NetFunction / CrumbFunction` 会直接把函数标成 `bBlueprintEvent`，生成 `__Evt_Execute()` wrapper。
2. UEAS2 的非 HAZE 分支则更保守，若函数已经是 `BlueprintEvent`/`BlueprintOverride`，再遇到 stock UE 网络 specifier 会直接报错。
3. 当前插件改写了这条 stock UE 分支，只继续禁止 `BlueprintOverride + network`，但允许 `BlueprintEvent + Net*`，并复用同一套 wrapper 生成路径。

也就是说，**当前插件不是弱化了 Hazelight 的 Blueprint/RPC 交互，恰恰相反，它是在 stock engine 约束下主动把 Hazelight 风格的“event thunk + implementation”语义补了回来。**

[1] Hazelight 的 HAZE 路径本来就把网络函数当作事件 wrapper 处理：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Preprocessor\AngelscriptPreprocessor.cpp
// 位置: 1288-1325
// 说明: HAZE 分支里，NetFunction / CrumbFunction 直接走 BlueprintEvent wrapper 语义
// ============================================================================
else if (Spec.Name == PP_NAME_NetFunction || Spec.Name == PP_NAME_CrumbFunction)
{
	if (FunctionDesc->bBlueprintOverride)
	{
		MacroError(File, Macro, FString::Printf(TEXT("UFUNCTION() %s cannot be both NetFunction and BlueprintOverride"), *FunctionDesc->FunctionName));
		...
	}

	bool bAlreadyHasWrapper = FunctionDesc->bBlueprintEvent;

	if (!bHadNotCallable)
		FunctionDesc->bBlueprintCallable = true;
	if (Spec.Name == PP_NAME_CrumbFunction)
		FunctionDesc->Meta.Add(Spec.Name, FString());

	// ★ 网络函数被提升成 BlueprintEvent 语义
	FunctionDesc->bBlueprintEvent = true;
	FunctionDesc->bNetFunction = true;

	if (!bAlreadyHasWrapper)
	{
		FunctionDesc->bCanOverrideEvent = false;
		GenerateBlueprintEventWrapper(File, Chunk, Macro, FunctionDesc);
		FunctionDesc->ScriptFunctionName += TEXT("_Implementation");
	}
}
```

[2] 当前插件把同样的 wrapper 语义扩展到了标准 UE RPC specifier，而不是继续沿用 UEAS2 非 HAZE 分支的硬拒绝：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp
// 位置: 1575-1625
// 说明: stock UE 的 NetMulticast / NetServer / NetClient 也会生成 BlueprintEvent wrapper
// ============================================================================
else if (Spec.Name == PP_NAME_NetMulticast || Spec.Name == PP_NAME_NetServer || Spec.Name == PP_NAME_NetClient)
{
	if (FunctionDesc->bBlueprintOverride)
	{
		// ★ 仍然禁止 override + network，避免继承语义混淆
		MacroError(File, Macro, FString::Printf(TEXT("UFUNCTION() %s cannot both be BlueprintOverride and have network specifiers"), *FunctionDesc->FunctionName));
		...
	}

	bool bAlreadyHasWrapper = FunctionDesc->bBlueprintEvent;

	if (!bHadNotCallable)
		FunctionDesc->bBlueprintCallable = true;

	// ★ 这里不再拒绝 BlueprintEvent + Net*，而是显式把 RPC 继续当 event thunk 处理
	FunctionDesc->bBlueprintEvent = true;
	FunctionDesc->bNetMulticast = Spec.Name == PP_NAME_NetMulticast;
	FunctionDesc->bNetClient = Spec.Name == PP_NAME_NetClient;
	FunctionDesc->bNetServer = Spec.Name == PP_NAME_NetServer;

	if (!bAlreadyHasWrapper)
	{
		FunctionDesc->bCanOverrideEvent = false;
		GenerateBlueprintEventWrapper(File, Chunk, Macro, FunctionDesc);
		FunctionDesc->ScriptFunctionName += TEXT("_Implementation");
	}
}
```

[3] 这不是“只改了预处理标志”，因为 wrapper 和 `UFunction` flag 最终都真的进入类生成链；同时，新增 `BlueprintEvent` 仍然会把 reload 要求抬到 full reload：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp
// 位置: 1959-2022
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 位置: 1277-1285, 3455-3464
// 说明: wrapper 会调用 __Evt_Execute()，类生成阶段继续打上 FUNC_BlueprintEvent / FUNC_Net*，并保持 full reload 约束
// ============================================================================
void FAngelscriptPreprocessor::GenerateBlueprintEventWrapper(...)
{
	Code += FString::Printf(TEXT("%s %s(%s) %sfinal%s {"),
		*ReturnTypeWithVisibility, *FunctionDesc->FunctionName, *Arguments,
		bConstMethod ? TEXT("const ") : TEXT(""),
		bPropertyMethod ? TEXT(" property") : TEXT(""));
	...
	// ★ wrapper 统一通过 __Evt_Execute 回到 Blueprint VM / 虚调用路径
	Code += FString::Printf(TEXT(" __Evt_Execute(this, %s);"), *GenerateStaticName(File, FunctionDesc->FunctionName));
	...
}

// ★ 新增 BlueprintEvent 仍然要求 full reload，不允许 soft reload 偷过
if (NewFunctionDesc->bBlueprintEvent)
{
	if (ClassData.ReloadReq < EReloadRequirement::FullReloadRequired)
	{
		ClassData.ReloadReq = EReloadRequirement::FullReloadRequired;
		ClassData.ReloadReqLines.AddUnique(NewFunctionDesc->LineNumber);
	}
}

if ((FunctionDesc->bBlueprintEvent && FunctionDesc->bCanOverrideEvent) || FunctionDesc->bBlueprintOverride)
	NewFunction->FunctionFlags |= FUNC_BlueprintEvent;
if (FunctionDesc->bNetMulticast)
	NewFunction->FunctionFlags |= FUNC_NetMulticast;
```

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| RPC 是否可走 event wrapper 语义 | HAZE 分支可以，stock UE 分支更保守 | stock UE `NetMulticast / NetServer / NetClient` 也复用 event wrapper | **实现方式不同，兼容面扩展** |
| `BlueprintOverride + network` | 拒绝 | 拒绝 | **一致** |
| 新增 `BlueprintEvent` 的 reload 要求 | `FullReloadRequired` | `FullReloadRequired` | **一致** |

因此，D3 这一轮新增的结论是：**当前插件不是在 Blueprint/RPC 交互上缩水，而是在 stock engine 分支里主动吸收了 Hazelight 的“RPC 也是 event thunk”设计。** 这属于“**实现方式不同 + 兼容面扩展**”，不是“和上游脱节”。

### [维度 D4] 当前插件把 hot-reload 专用比较接口并回通用比较接口，状态保持策略更保守但没有丢

```
[D4-Deep] State Preservation Strategy
UEAS2
├─ Type API
│  ├─ CanCompare / IsValueEqual
│  └─ CanCompareForHotReload / IsValueEqualForHotReload
│     └─ FTransform opts in only here
└─ ClassGenerator uses hot-reload comparator to decide copy set

AngelPortV2
├─ Type API
│  └─ CanCompare / IsValueEqual only
│     └─ FTransform reports CanCompare = false
├─ ClassGenerator
│  └─ !CanCompare => conservative copy on reload
└─ Scenario tests assert values survive reload and new code executes
```

前文已经覆盖过 reload queue、依赖扩散、soft/full reload 分级、editor cache 刷新。这一轮补的是更底层的一件事：**热重载时到底如何判断“哪些旧值需要拷贝到新 class layout 里”。**

UEAS2 的设计是给 type system 留一套**专门的 hot-reload 比较接口**。这样某些类型即使不适合做通用 `==`，仍然可以声明“我在 hot reload 场景里可比较”。`FTransform` 就是最直观的例子：它在一般比较接口上返回 `false`，但在 `CanCompareForHotReload()` / `IsValueEqualForHotReload()` 里显式启用 `Equals(...)`。

当前插件把这层专用接口合并掉了。`FAngelscriptType` 只保留通用 `CanCompare()` / `IsValueEqual()`；类生成器也只看这两个接口。结果是：**像 `FTransform` 这种“只想在 hot reload 时参与相等性判断”的类型，现在无法表达这层细粒度语义，只能退回保守复制。**

这不是“状态保持没了”，因为类生成器在 `!CanCompare` 时会直接选择 copy，而且当前插件还补了明确的 hot reload 场景测试来锁住行为；但它确实意味着**判定粒度比 UEAS2 更粗，倾向于多拷贝而不是精确跳过。**

[1] 两边的 type API 差别非常直接：UEAS2 还保留 hot-reload 专用 compare hook，当前插件已经合并掉：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Public\AngelscriptType.h
// 位置: 482-486
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h
// 位置: 465-469
// 说明: UEAS2 有 hot-reload 专用 compare facade；当前插件只剩通用 compare facade
// ============================================================================
// UEAS2
FORCEINLINE bool CanCompareForHotReload() const { return Type.IsValid() && Type->CanCompareForHotReload(*this); }
FORCEINLINE bool IsValueEqualForHotReload(void* SourcePtr, void* DestinationPtr) const { return Type->IsValueEqualForHotReload(*this, SourcePtr, DestinationPtr); }

// AngelPortV2
FORCEINLINE bool CanCompare() const { return Type.IsValid() && Type->CanCompare(*this); }
FORCEINLINE bool IsValueEqual(void* SourcePtr, void* DestinationPtr) const { return Type->IsValueEqual(*this, SourcePtr, DestinationPtr); }
```

[2] `FTransform` 正好证明这不是命名小改，而是语义收口：上游允许“通用不可比，但 hot reload 可比”；当前插件则只能把它视作不可比：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Binds\Bind_FTransform.cpp
// 位置: 16-25
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FTransform.cpp
// 位置: 15-19
// 说明: UEAS2 为 FTransform 单独开放 hot-reload compare；当前插件没有对应独立入口
// ============================================================================
// UEAS2
bool IsValueEqual(const FAngelscriptTypeUsage& Usage, void* SourcePtr, void* DestinationPtr) const override
{
	return false;
}

bool CanCompareForHotReload(const FAngelscriptTypeUsage& Usage) const override { return true; }
bool IsValueEqualForHotReload(const FAngelscriptTypeUsage& Usage, void* SourcePtr, void* DestinationPtr) const override
{
	return ((FTransform*)SourcePtr)->Equals(*(FTransform*)DestinationPtr, 0);
}

// AngelPortV2
bool CanCompare(const FAngelscriptTypeUsage& Usage) const override { return false; }
bool IsValueEqual(const FAngelscriptTypeUsage& Usage, void* SourcePtr, void* DestinationPtr) const override
{
	return false;
}
```

[3] 这会直接反映到 class reinstance 的 copy 策略上。UEAS2 用 hot-reload comparator 做判定；当前插件则在 `!CanCompare` 时保守复制：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\ClassGenerator\AngelscriptClassGenerator.cpp
// 位置: 4319-4343, 4417-4433, 4527-4545
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 位置: 4479-4503, 4576-4594, 4687-4705
// 说明: 上游是专用 hot-reload compare；当前插件是通用 compare + conservative copy
// ============================================================================
// UEAS2
Copy.bCanCompare = PropertyType.CanCompareForHotReload();
...
if (!Copy.Type.IsValueEqualForHotReload(BaseCDOPtr, CDONoDefaultsPtr))
{
	Copy.bModifiedByDefaults = true;
}
...
bool bShouldCopy = Copy.bModifiedByDefaults || !Copy.bCanCompare || !Copy.Type.IsValueEqualForHotReload(CDOPtr, OriginalPtr);

// AngelPortV2
Copy.bCanCompare = PropertyType.CanCompare();
...
if (!Copy.Type.IsValueEqual(BaseCDOPtr, CDONoDefaultsPtr))
{
	Copy.bModifiedByDefaults = true;
}
...
bool bShouldCopy = Copy.bModifiedByDefaults || !Copy.bCanCompare || !Copy.Type.IsValueEqual(CDOPtr, OriginalPtr);
```

[4] 当前插件没有把行为赌在“多拷贝应该也没事”上，而是把 property-preserved 场景锁成自动化测试；同时热重载入口会准备回归用例：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp
// 位置: 23-25, 42-146
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 2481-2489
// 说明: 当前插件用场景测试确认“值保持 + 新逻辑生效”，并把 hot reload test runner 接到主流程
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptScenarioHotReloadPropertyPreservedTest,
	"Angelscript.TestModule.HotReload.PropertyPreserved",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

CounterProperty->SetPropertyValue_InContainer(Actor, 42);
...
TestEqual(TEXT("Scenario hot-reload property-preserved should keep the actor property value after soft reload"), CounterValue, 42);
...
TestEqual(TEXT("Scenario hot-reload property-preserved function should observe the preserved property value after reload"), Result, 142);

if (GEngine && bCompletedAssetScan && HotReloadTestRunner != nullptr && HotReloadTestRunner->ShouldRunUnitTestsOnHotReload())
{
	// ★ reload 主流程会把本次变更文件交给 hot reload test runner
	HotReloadTestRunner->PrepareTests(GetActiveModules(), CompiledModules, RelativeFileList, ShouldUseAutomaticImportMethod());
}
```

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| hot-reload 专用 compare API | 有，`CanCompareForHotReload / IsValueEqualForHotReload` | 无，合并进 `CanCompare / IsValueEqual` | **实现方式不同** |
| `FTransform` 这类“仅 reload 可比”类型 | 可精确比较后再决定是否 copy | 无法表达该粒度，只能保守 copy | **实现质量/效率差异** |
| 状态保持能力 | 依靠专用 comparator + reinstance 复制策略 | 依靠更保守的 copy 策略 + 场景测试锁定结果 | **没有丢能力，但实现更保守** |

因此，D4 本轮新增的结论是：**当前插件并没有失去 hot reload 的状态保持能力，但它把“hot-reload 专用相等性”收口成了通用 compare 语义。** 结果是某些类型的判定不如 UEAS2 精细，不过系统会转向保守复制，并通过自动化场景测试确认结果正确。

---

## 深化分析 (2026-04-08 23:36:56)

### [维度 D1/D11] 当前插件把 AngelScript 内核和自动导出入口都收进 `AngelscriptRuntime` 根目录；UEAS2 仍假设“插件根目录 + 引擎补丁”是固定前提

```
[D1/D11-Deep] Runtime Ownership Root
UEAS2
├─ AngelscriptCode.Build.cs
│  ├─ PluginPath = ../Plugins/Angelscript
│  └─ ThirdParty/include + ThirdParty/source      // 内核头文件挂在插件根目录
├─ AngelscriptLoader.Build.cs
│  └─ thin dependency shell                       // 只把 Code/Editor 重新串起来
└─ engine-patched UHT                             // script 语义默认由引擎主链理解

AngelPortV2
├─ AngelscriptRuntime.Build.cs
│  ├─ ModuleDirectory/ThirdParty/angelscript      // 内核源码直接跟 Runtime module 走
│  ├─ ANGELSCRIPT_EXPORT / DLL_LIBRARY_IMPORT
│  └─ feature deps declared here                  // StructUtils / EI / GAS
├─ Angelscript.uplugin
│  └─ explicit plugin dependencies                // 插件依赖在分发面显式声明
└─ runtime module becomes ownership root          // codegen / bind / third-party 共用一套根
```

前文已经讲过 Loader 被吸收、测试模块被拉出；这一轮补的是更底层的 **ownership root**。  
UEAS2 的 `AngelscriptCode` 仍把 AngelScript 内核头文件当作“插件根目录下固定存在的 `ThirdParty/` 资源”；`Loader` 也只是把 `Code + Editor` 再串一次。当前插件则把 AngelScript 内核直接搬进 `Source/AngelscriptRuntime/ThirdParty/angelscript`，再由 `AngelscriptRuntime.Build.cs` 同时承担导出宏、第三方 include path、功能依赖声明。这样一来，`Runtime module` 不只是执行 owner，也成了构建和分发 owner。

[1] UEAS2 的 build root 仍然是插件根目录，`Loader` 本身也只是依赖壳：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\AngelscriptCode.Build.cs
// 位置: 60-64
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptLoader\AngelscriptLoader.Build.cs
// 位置: 12-24
// 说明: UEAS2 的 AngelScript 内核 include path 仍锚定插件根目录；Loader 只是把 Code/Editor 重新接起来
// ============================================================================
var PluginPath = "../Plugins/Angelscript";

// ★ 运行时默认假定 ThirdParty 位于插件根目录，而不是某个具体 module 根目录
PublicIncludePaths.Add(PluginPath + "/ThirdParty/include");
PublicIncludePaths.Add(PluginPath + "/ThirdParty/source");

PublicDependencyModuleNames.AddRange(new string[]
{
	"Core",
	"Engine",
	"AngelscriptCode",
});

if (Target.bBuildEditor)
{
	// ★ Editor 构建时再顺手把 AngelscriptEditor 一起挂上
	PublicDependencyModuleNames.AddRange(new string[]
	{
		"AngelscriptEditor",
	});
}
```

[2] 当前插件把第三方内核、导出宏和能力面统一收进 `AngelscriptRuntime`，并把宿主插件依赖写进 `.uplugin`：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs
// 位置: 12-22, 29-79
// 文件: Plugins/Angelscript/Angelscript.uplugin
// 位置: 18-48
// 说明: 当前插件把 AngelScript 内核和自动导出所需依赖都显式绑在 Runtime module / plugin manifest 上
// ============================================================================
PrivateDefinitions.Add("ANGELSCRIPT_EXPORT=1");
PublicDefinitions.Add("ANGELSCRIPT_DLL_LIBRARY_IMPORT=1");

var AngelscriptThirdPartyPath = Path.Combine(ModuleDirectory, "ThirdParty", "angelscript");
// ★ 内核源码跟着 Runtime module 走，不再依赖插件根目录约定
PublicIncludePaths.Add(Path.Combine(AngelscriptThirdPartyPath, "source"));
PublicIncludePaths.Add(AngelscriptThirdPartyPath);

PublicDependencyModuleNames.AddRange(new string[]
{
	"ApplicationCore",
	"Core",
	"CoreUObject",
	"Engine",
	"StructUtils",
});

PrivateDependencyModuleNames.AddRange(new string[]
{
	"EnhancedInput",
	"GameplayAbilities",
	"GameplayTasks",
});

"Modules": [
	{ "Name": "AngelscriptRuntime", "Type": "Runtime", "LoadingPhase": "PostDefault" },
	{ "Name": "AngelscriptEditor", "Type": "Editor", "LoadingPhase": "PostDefault" },
	{ "Name": "AngelscriptTest", "Type": "Editor", "LoadingPhase": "PostDefault" }
],
"Plugins": [
	{ "Name": "StructUtils", "Enabled": true },
	{ "Name": "EnhancedInput", "Enabled": true },
	{ "Name": "GameplayAbilities", "Enabled": true }
]
```

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| AngelScript 内核源码 ownership | 插件根目录 `ThirdParty/` | `AngelscriptRuntime/ThirdParty/angelscript` | **实现方式不同** |
| Loader 对 build graph 的作用 | 依赖转发壳 | 无独立模块，责任下沉进 Runtime/SubSystem/Test | **前文已证实为实现方式不同** |
| 分发面是否显式声明能力依赖 | 当前主插件 `.uplugin` 未声明这些宿主插件 | `StructUtils / EnhancedInput / GameplayAbilities` 显式列入 manifest | **部署契约更显式** |

因此，这一轮 D1/D11 最值得补记的点不是“模块又多/又少了”，而是：**当前插件把 ownership root 从“插件根目录 + engine patch 假设”收口成了“Runtime module 自持”。** 这让 `AngelscriptRuntime` 同时成为内核源码、bind 能力面和后续 UHT 导出链的共同锚点。

### [维度 D5/D6] 当前插件把源码定位元数据正式抬成脚本/API contract，但 `DebugServer V2` 仍故意维持 `SectionName + ModuleName` 的符号化 payload

```
[D5/D6-Deep] Source Fidelity Contract
UEAS2
├─ UASClass / UASFunction know AbsoluteFilename   // 元数据存在于 runtime class/function
├─ Bind_UClass_Base exposes basic reflection ops  // 脚本侧未见同等级 source API
└─ DebugServer CallStack uses SectionName+Module  // 远端协议仍是符号化定位

AngelPortV2
├─ UClass / UFunction script API
│  ├─ GetSourceFilePath()
│  ├─ GetScriptModuleName()
│  ├─ GetSourceLineNumber()
│  └─ GetScriptFunctionDeclaration()
├─ tests lock source path + line preservation      // 编辑器/脚本两侧都可验证
└─ DebugServer CallStack still uses SectionName+Module
   └─ richer metadata stays local/editor-side      // 没把 wire contract 改成绝对路径
```

前文已经覆盖过 `DebugServerVersion`、framing、session owner 和 `CallStack.ModuleName` 协商。这一轮补的是一个更隐蔽的 contract：**当前插件明明已经掌握绝对源码路径，并且把这层信息开放给脚本和自动化测试了，但 DebugServer socket payload 仍然坚持发 `SectionName + ModuleName`。**

这说明两件事：

1. 当前插件不是“还拿不到更精确的源码定位信息”。  
2. 它是**主动**把 richer source metadata 留在本地/editor/script API 层，而没有改写 `DebugServer V2` 的远端定位协议。

[1] UEAS2 当前快照里，`UASClass/UASFunction` 内部已经保存源码路径，但 `Bind_UClass_Base` 公开给脚本的仍是基础反射 helper：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\ClassGenerator\ASClass.cpp
// 位置: 1459-1524
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Binds\Bind_UObject.cpp
// 位置: 219-238
// 说明: UEAS2 的 runtime object 已经知道源码路径，但在当前快照的 Bind_UClass_Base 中没有把这层信息抬成脚本 API
// ============================================================================
FString UASClass::GetSourceFilePath() const
{
	auto& Manager = FAngelscriptManager::Get();
	auto Module = Manager.GetModule(((asITypeInfo*)ScriptTypePtr)->GetModule());
	if (!Module.IsValid() || Module->Code.Num() == 0)
		return TEXT("");
	// ★ 内部元数据里已有绝对路径
	return Module->Code[0].AbsoluteFilename;
}

FString UASFunction::GetSourceFilePath() const
{
	auto& Manager = FAngelscriptManager::Get();
	auto Module = Manager.GetModule(ScriptFunction->GetModule());
	if (!Module.IsValid() || Module->Code.Num() == 0)
		return TEXT("");
	return Module->Code[0].AbsoluteFilename;
}

int UASFunction::GetSourceLineNumber() const
{
	// ★ 行号也已存在于 runtime function
	return (scriptData->declaredAt & 0xFFFFF) + 1;
}

auto UClass_ = FAngelscriptBinds::ExistingClass("UClass");
UClass_.Method("UObject GetDefaultObject() const", [](UClass* Class)
{
	return Class->GetDefaultObject();
});
UClass_.Method("bool IsChildOf(UClass Other) const", METHODPR_TRIVIAL(bool, UStruct, IsChildOf, (const UStruct*) const));
UClass_.Method("bool IsAbstract() const", [](UClass* Class) { return Class->HasAnyClassFlags(CLASS_Abstract); });
UClass_.Method("UClass GetSuperClass() const", [](UClass* Class) -> UClass* { return Class->GetSuperClass(); });
// ★ 这里仍未出现 GetSourceFilePath / GetSourceLineNumber / GetScriptModuleName
```

[2] 当前插件把这层 metadata 正式暴露给脚本侧 `UClass/UFunction`，并用脚本/Editor 自动化双重锁定：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp
// 位置: 280-297, 405-421
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp
// 位置: 223-248
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSourceNavigationTests.cpp
// 位置: 69-78
// 说明: 当前插件把源码路径/模块名/声明信息抬成脚本 API，并让脚本与 editor navigation 都显式回归
// ============================================================================
UClass_.Method("FString GetSourceFilePath() const", [](UClass* Class) -> FString
{
	if (const UASClass* ScriptClass = Cast<UASClass>(Class))
	{
		return ScriptClass->GetSourceFilePath();
	}
	return FString();
});
UClass_.Method("FString GetScriptModuleName() const", [](UClass* Class) -> FString
{
	const UASClass* ScriptClass = Cast<UASClass>(Class);
	if (ScriptClass == nullptr || ScriptClass->ScriptTypePtr == nullptr)
	{
		return FString();
	}
	auto& Manager = FAngelscriptEngine::Get();
	auto Module = Manager.GetModule(((asITypeInfo*)ScriptClass->ScriptTypePtr)->GetModule());
	return Module.IsValid() ? Module->ModuleName : FString();
});

UFunction_.Method("FString GetSourceFilePath() const", [](UFunction* Function) -> FString
{
	if (const UASFunction* ScriptFunction = Cast<UASFunction>(Function))
	{
		return ScriptFunction->GetSourceFilePath();
	}
	return FString();
});
UFunction_.Method("int GetSourceLineNumber() const", [](UFunction* Function) -> int32
{
	if (const UASFunction* ScriptFunction = Cast<UASFunction>(Function))
	{
		return ScriptFunction->GetSourceLineNumber();
	}
	return -1;
});

if (!(Type.GetSourceFilePath() == "__SCRIPT_PATH__"))
	return 20;
if (!Type.GetScriptModuleName().Contains("RuntimeSourceMetadataBindingsTest"))
	return 30;
if (!(Func.GetSourceFilePath() == "__SCRIPT_PATH__"))
	return 50;
if (Func.GetSourceLineNumber() != 6)
	return 60;

TestEqual(TEXT("Generated function should preserve source file path"), RuntimeASFunction->GetSourceFilePath(), ScriptPath);
TestEqual(TEXT("Generated function should preserve source line number"), RuntimeASFunction->GetSourceLineNumber(), 6);
TestTrue(TEXT("Source navigation should recognize generated script function"), FSourceCodeNavigation::CanNavigateToFunction(RuntimeFunction));
```

[3] 但两边的 debug wire payload 仍然保持同一套“符号化 source”约定，而不是发送绝对路径：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Debugging\AngelscriptDebugServer.cpp
// 位置: 1342-1349
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 位置: 1467-1474
// 说明: DebugServer V2 仍然发送 SectionName + ModuleName；当前插件没有把更富的本地路径元数据塞进 socket payload
// ============================================================================
Frame.Name = ANSI_TO_TCHAR(ScriptFunction->GetName());

const char* SectionName = nullptr;
Frame.LineNumber = Context->GetLineNumber(i, nullptr, &SectionName);
// ★ Source 仍是脚本 section name，而不是 AbsoluteFilename
Frame.Source = SectionName ? ANSI_TO_TCHAR(SectionName) : TEXT("");

if (AngelscriptDebugServer::DebugAdapterVersion >= 1)
	// ★ 远端仍靠 module name 做二次解析
	Frame.ModuleName = ScriptFunction->GetModuleName() ? ANSI_TO_TCHAR(ScriptFunction->GetModuleName()) : TEXT("");
```

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| runtime 是否掌握源码路径元数据 | 有，`UASClass/UASFunction` 内部可读 | 有 | **没有差距** |
| 脚本侧是否能直接读取这些元数据 | 当前快照未见同等级 `Bind_*.cpp` 暴露 | `UClass/UFunction` 已有脚本 API + 自动化测试 | **能力增强** |
| DebugServer 远端 payload 是否升级为绝对路径 | 没有，仍是 `SectionName + ModuleName` | 仍保持一致 | **协议一致** |

因此，这一轮 D5/D6 的新增结论是：**当前插件已经把 source fidelity 做到了“脚本/API 可见 + Editor 可测试”，但它刻意没有改写 `DebugServer V2` 的远端定位 contract。** 这属于“**本地开发体验增强 + 远端协议刻意保持一致**”，不是“**调试协议落后**”。

### [维度 D8/D11] 当前插件的 UHT 替代链不只是“插件本地化”，还显式补上了增量重生成与陈旧产物清理 contract

```
[D8/D11-Deep] Incremental UHT Export Lifecycle
UEAS2
├─ patched EpicGames.UHT parser/codegen
│  ├─ WorldContext -> AngelscriptPropertyFlags
│  └─ CppRef / CppConst / EnumAsByte emitted by engine codegen
└─ regeneration lifecycle owned by engine UHT binary

AngelPortV2
├─ AngelscriptUHTTool.ubtplugin.csproj
│  └─ plugin DLL -> Binaries/DotNET/UnrealBuildTool/Plugins/AngelscriptUHTTool
└─ AngelscriptFunctionTableCodeGenerator
   ├─ AddExternalDependency(AngelscriptRuntime.Build.cs)
   ├─ parse dependency/editor blocks -> supported/editor-only modules
   ├─ AddExternalDependency(class header)
   └─ DeleteStaleOutputs(AS_FunctionTable_*.cpp)
```

前文已经说明当前插件用 plugin-local exporter 取代了 engine patch。这一轮继续往下钻后，可以更明确地说：**当前插件补的不只是“导出能力”，还补了一套显式的增量生命周期 contract。**

这和 UEAS2 的差异很大。UEAS2 的 regeneration 责任天然埋在被打过补丁的 engine UHT 二进制里；当前插件因为不能假设引擎被改过，必须自己回答下面三个问题：

1. 哪些模块算本次 runtime 可导出的 surface？  
2. 哪些 header / build 脚本变化应触发重新导出？  
3. 哪些旧的 `AS_FunctionTable_*.cpp` 需要删掉，避免 stale output 假装还有效？

[1] UEAS2 的路径仍是 engine-wide patch：parser 直接打 `WorldContext` flag，codegen 直接还原 C++ 语义：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Programs\Shared\EpicGames.UHT\Parsers\UhtFunctionParser.cs
// 位置: 575-586
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Programs\Shared\EpicGames.UHT\Exporters\CodeGen\UhtHeaderCodeGeneratorCppFile.cs
// 位置: 2548-2575
// 说明: UEAS2 的 regeneration contract 由 engine UHT 主链隐式承担
// ============================================================================
if (function.MetaData.ContainsKey("WorldContext"))
{
	string WorldContextArgName = function.MetaData.GetValueOrDefault("WorldContext");
	foreach (UhtType Child in function.Children)
	{
		if (Child is UhtProperty Property && Property.SourceName == WorldContextArgName)
		{
			// ★ parser 直接把 script 语义写进 Property flag
			Property.AngelscriptPropertyFlags |= EAngelscriptPropertyFlags.WorldContext;
		}
	}
}

bool bIsRef = Property.AngelscriptPropertyFlags.HasFlag(EAngelscriptPropertyFlags.CppRef);
bool bIsConst = Property.AngelscriptPropertyFlags.HasFlag(EAngelscriptPropertyFlags.CppConst);
if (Property is UhtByteProperty ByteProp && ByteProp.Enum != null && ByteProp.AngelscriptPropertyFlags.HasFlag(EAngelscriptPropertyFlags.CppEnumAsByte))
{
	// ★ codegen 直接按 flag 还原 TEnumAsByte<...> 形状
	builder.Append("TEnumAsByte<");
	builder.Append(ByteProp.Enum.CppType);
	builder.Append(">");
}
```

[2] 当前插件则必须把“导出触发条件”和“陈旧产物清理”显式写进 exporter 自己：

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptUHTTool.ubtplugin.csproj
// 位置: 12-15
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs
// 位置: 21-27
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs
// 位置: 336-405, 432-459
// 说明: 当前插件不只是“有个 exporter”，而是把构建触发、模块范围和 stale cleanup 都工程化了
// ============================================================================
<RootNamespace>AngelscriptUHTTool</RootNamespace>
<AssemblyName>AngelscriptUHTTool</AssemblyName>
// ★ tool DLL 直接跟随插件分发，而不是塞回 engine UHT 项目树
<OutputPath>..\..\Binaries\DotNET\UnrealBuildTool\Plugins\AngelscriptUHTTool\</OutputPath>

[UhtExporter(
	Name = "AngelscriptFunctionTable",
	Options = UhtExporterOptions.Default | UhtExporterOptions.CompileOutput,
	CppFilters = ["AS_FunctionTable_*.cpp"],
	ModuleName = "AngelscriptRuntime")]

string buildCsPath = ResolveRuntimeBuildCsPath(factory);
// ★ Runtime Build.cs 变化会直接触发重新导出
factory.AddExternalDependency(buildCsPath);

foreach (string rawLine in File.ReadAllLines(buildCsPath))
{
	...
	foreach (Match match in QuotedStringPattern.Matches(line))
	{
		string moduleName = match.Groups[1].Value;
		allModules.Add(moduleName);
		if (inEditorBlock)
		{
			// ★ Editor-only surface 也不是硬编码，而是从 Build.cs 实时推导
			editorOnlyModules.Add(moduleName);
		}
	}
}

foreach (string existingFile in Directory.EnumerateFiles(outputDirectory, "AS_FunctionTable_*.cpp"))
{
	if (!generatedPaths.Contains(existingFile))
	{
		// ★ 旧 shard 不在本轮生成集里就删除，避免 stale output 混入编译
		File.Delete(existingFile);
	}
}

if (classObj.HeaderFile != null)
{
	// ★ 每个参与导出的头文件都登记成 external dependency
	factory.AddExternalDependency(classObj.HeaderFile.FilePath);
}
```

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| regeneration 责任 owner | engine-patched UHT 二进制 | plugin-local exporter + code generator | **实现方式不同** |
| 自动导出范围的来源 | UHT 主链天然全局可见 | 运行时 `Build.cs` 被当作显式配置源解析 | **实现方式不同** |
| stale output 清理 | 对插件作者基本不可见，责任埋在 engine codegen 流程 | `DeleteStaleOutputs()` 显式删除旧 shard | **实现质量/可维护性增强** |

因此，这一轮 D8/D11 最重要的新结论是：**当前插件不是只把 UHT 补丁“搬到插件里”而已，它还顺手补了一套 UEAS2 在 engine-wide patch 时代不需要显式写出的增量生命周期 contract。** 这让分发边界更干净，也让“为什么这次 function table 需要重生成”有了可追溯的源码答案。

---

## 深化分析 (2026-04-08 23:51:17)

### [维度 D4/D2] `__WorldContext` 的脚本侧 contract 已从“可写全局变量”改成“当前 engine 解析函数”

```
[D4/D2-Deep] Script-visible World Context Contract
UEAS2
├─ FAngelscriptManager::CurrentWorldContext        // 进程级静态指针
├─ __WorldContext                                  // 直接暴露成脚本全局变量
└─ FAngelscriptGameThreadScopeWorldContext         // RAII 改写/恢复同一指针

AngelPortV2
├─ FAngelscriptEngineScope                         // 进入/离开 engine 作用域
├─ __WorldContext()                                // 每次调用都解析当前 engine
├─ FAngelscriptEngine::TryGetCurrentWorldContextObject()
└─ isolation tests lock nested restore             // 嵌套 engine world context 有单测锁定
```

前文已经讲过 current 把 manager singleton 拆成 engine-scope runtime；这一轮补的是**脚本真正看到的 world-context contract 也一起变了**。  
UEAS2 让脚本读一个名为 `__WorldContext` 的全局变量，本质上就是直连 `FAngelscriptManager::CurrentWorldContext`。current 则不再把这块状态暴露成可写全局，而是公开一个零参函数 `__WorldContext()`，每次都通过 `FAngelscriptEngine::TryGetCurrentWorldContextObject()` 解析当前 engine 或 ambient context。

这不是语法层的小修，而是把 world-context 从“全局共享状态”收缩成“当前 engine 的可派生状态”。因此 Loader 被吸收、engine context stack 引入之后，脚本层拿到的 world context 也自然跟着 engine scope 走，而不是继续依赖单一静态指针。

[1] UEAS2 的 world-context 是 manager 静态变量，bind 直接把它暴露给脚本：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Public\AngelscriptManager.h
// 位置: 73, 429-431, 519-522
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Binds\Bind_UWorld.cpp
// 位置: 32-38
// 说明: UEAS2 把 world context 作为进程级静态指针保存，并直接暴露成脚本变量
// ============================================================================
static UObject* CurrentWorldContext;

FORCEINLINE static void AssignWorldContext(UObject* NewWorldContext)
{
	// ★ scope 进入时直接覆写全局静态指针
	*(UObject* volatile*)&CurrentWorldContext = NewWorldContext;
}

FAngelscriptGameThreadScopeWorldContext(UObject* WorldContext)
{
	PreviousWorldContext = FAngelscriptManager::CurrentWorldContext;
	FAngelscriptManager::AssignWorldContext(WorldContext);
}

AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_World((int32)FAngelscriptBinds::EOrder::Late, []
{
	// ★ 脚本读到的是变量，不是 resolver
	FAngelscriptBinds::BindGlobalVariable("UObject __WorldContext", &FAngelscriptManager::CurrentWorldContext);
	FAngelscriptBinds::BindGlobalFunction("UWorld GetCurrentWorld() no_discard",
	[]() -> UWorld*
	{
		return GEngine->GetWorldFromContextObject(FAngelscriptManager::CurrentWorldContext, EGetWorldErrorMode::ReturnNull);
	});
});
```

[2] current 把脚本侧入口改成 resolver，并用嵌套 engine scope 测试锁住恢复语义：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptRuntime\Binds\Bind_UWorld.cpp
// 位置: 31-41
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptRuntime\Core\AngelscriptEngine.cpp
// 位置: 682-689
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptRuntime\Tests\AngelscriptEngineIsolationTests.cpp
// 位置: 218-229
// 说明: current 不再暴露全局变量，而是让脚本每次通过当前 engine 解析 world context
// ============================================================================
AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_World((int32)FAngelscriptBinds::EOrder::Late, []
{
	FAngelscriptBinds::BindGlobalFunction("UObject __WorldContext()",
	[]() -> UObject*
	{
		// ★ 运行时按当前 engine scope 解析，而不是读共享静态指针
		return FAngelscriptEngine::TryGetCurrentWorldContextObject();
	});
	FAngelscriptBinds::BindGlobalFunction("UWorld GetCurrentWorld()",
	[]() -> UWorld*
	{
		return GEngine->GetWorldFromContextObject(FAngelscriptEngine::TryGetCurrentWorldContextObject(), EGetWorldErrorMode::ReturnNull);
	});
});

UObject* FAngelscriptEngine::TryGetCurrentWorldContextObject()
{
	if (FAngelscriptEngine* CurrentEngine = TryGetCurrentEngine())
	{
		return CurrentEngine->GetCurrentWorldContextObject();
	}

	return GAmbientWorldContext;
}

FAngelscriptEngineScope OuterScope(*PrimaryEngine, OuterContext);
TestTrue(TEXT("Outer scope should expose its world context through the active engine"), PrimaryEngine->GetCurrentWorldContextObject() == OuterContext);
{
	FAngelscriptEngineScope InnerScope(*SecondaryEngine, InnerContext);
	TestTrue(TEXT("Inner scope should expose its world context through the nested engine"), SecondaryEngine->GetCurrentWorldContextObject() == InnerContext);
}
TestTrue(TEXT("Leaving the inner scope should restore the outer world context"), PrimaryEngine->GetCurrentWorldContextObject() == OuterContext);
return TestNull(TEXT("Leaving the outer scope should clear the world context"), PrimaryEngine->GetCurrentWorldContextObject());
```

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| 脚本侧 world-context 入口 | 全局变量 `__WorldContext` | resolver `__WorldContext()` | **实现方式不同** |
| owner 作用域 | manager 进程级静态状态 | current engine / ambient context | **架构收缩** |
| 嵌套作用域恢复验证 | 依赖 RAII + 静态变量 | 有显式 isolation test | **实现质量增强** |

因此，这一维的新结论是：**current 去掉 Loader 后，并不是把 world-context contract 留在原地，而是顺手把它从“共享可变状态”改成了“engine-scope 解析协议”。** 这属于 `D4` 的生命周期重构，也直接影响 `D2` 里所有隐藏 world-context 参数的真实来源。

### [维度 D2/D8] `CallableWithoutWorldContext` 在 current 里已不是 UEAS2 的等价 contract

```
[D2/D8-Deep] Hidden World Context Trait Decision
UEAS2
├─ WorldContext metadata -> hide arg
├─ OptionalWorldContext   -> clear uses-world-context trait
└─ CallableWithoutWorldContext -> clear uses-world-context trait

AngelPortV2
├─ WorldContext metadata -> hide arg
├─ OptionalWorldContext   -> clear uses-world-context trait
└─ CallableWithoutWorldContext
   ├─ test fixture still declares it
   └─ helper no longer checks it
```

这里不能粗暴写成“current 没有 world-context 支持”。它仍然会把 `WorldContext` 参数隐藏起来，也仍然能写入 `hiddenArgumentDefault`。  
真正发生漂移的是更细的 trait 规则：UEAS2 明确把 `CallableWithoutWorldContext` 当成“隐藏参数保留，但 `asTRAIT_USES_WORLDCONTEXT` 不再打到脚本函数上”；current helper 则只保留了 `OptionalWorldContext` 分支。

更关键的是，这不是我主观猜测的“可能不一样”。current 仓库自己的测试夹具和自动化测试，仍然把 `CallableWithoutWorldContext` 当成既定 contract。这说明这里更接近“**实现质量差异 / 潜在回归**”，而不是“**作者故意改了需求**”。

[1] UEAS2 在 helper 层明确持有 `CallableWithoutWorldContext` 常量，并在写 traits 时排除它：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Binds\Helper_FunctionSignature.h
// 位置: 30-35, 233-234, 445-449
// 说明: UEAS2 明确把 CallableWithoutWorldContext 纳入 world-context trait 决策
// ============================================================================
static const FName NAME_OptionalWorldContext("OptionalWorldContext");
static const FName NAME_CallableWithoutWorldContext("CallableWithoutWorldContext");
static const FName NAME_ScriptNoDiscard("ScriptNoDiscard");
static const FName NAME_ScriptAllowDiscard("ScriptAllowDiscard");
static const FName NAME_ScriptAllowTemporaryThis("ScriptAllowTemporaryThis");
static const FName NAME_UnsafeDuringActorConstruction("UnsafeDuringActorConstruction");

ArgumentDefaults[ArgIndex] = TEXT("__WorldContext");
WorldContextArgument = ArgIndex;

ScriptFunction->hiddenArgumentIndex = WorldContextArgument;
ScriptFunction->hiddenArgumentDefault = "__WorldContext";
#if WITH_EDITOR
if (!Function->HasMetaData(NAME_OptionalWorldContext) && !Function->HasMetaData(NAME_CallableWithoutWorldContext))
	// ★ 只有“真正要求脚本感知 world context”的函数才保留 trait
	ScriptFunction->traits.SetTrait(asTRAIT_USES_WORLDCONTEXT, true);
#endif
```

[2] current helper 已不再读取 `CallableWithoutWorldContext`，但测试源码仍然要求这条 contract 继续成立：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptRuntime\Binds\Helper_FunctionSignature.h
// 位置: 22-29, 230-231, 425-429
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptTest\Core\AngelscriptUhtCoverageTestTypes.h
// 位置: 34-38
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptTest\Core\AngelscriptBindConfigTests.cpp
// 位置: 684-703
// 说明: current 仍然隐藏 world-context 参数，但 helper 已经不再把 CallableWithoutWorldContext 当成清 trait 条件
// ============================================================================
static const FName NAME_Signature_ScriptMixin("ScriptMixin");
static const FName NAME_Signature_ScriptTrivial("ScriptTrivial");
static const FName NAME_Signature_NotAngelscriptProperty("NotAngelscriptProperty");
static const FName NAME_AS_Tooltip("ScriptTooltip");
static const FName NAME_AS_BlueprintProtected("BlueprintProtected");
static const FName NAME_Function_DeprecatedFunction("DeprecatedFunction");
static const FName NAME_Function_DeprecationMessage("DeprecationMessage");
static const FName NAME_OptionalWorldContext("OptionalWorldContext");

ArgumentDefaults[ArgIndex] = TEXT("__WorldContext()");
WorldContextArgument = ArgIndex;

ScriptFunction->hiddenArgumentIndex = WorldContextArgument;
ScriptFunction->hiddenArgumentDefault = "__WorldContext()";
#if WITH_EDITOR
if (!Function->HasMetaData(NAME_OptionalWorldContext))
	// ★ 这里已经不再检查 CallableWithoutWorldContext
	ScriptFunction->traits.SetTrait(asTRAIT_USES_WORLDCONTEXT, true);
#endif

UFUNCTION(BlueprintCallable, meta = (WorldContext = "WorldContextObject"))
static int32 RequiresWorldContext(UObject* WorldContextObject, int32 Value);

UFUNCTION(BlueprintCallable, meta = (WorldContext = "WorldContextObject", CallableWithoutWorldContext))
static int32 CallableWithoutWorldContext(UObject* WorldContextObject, int32 Value);

FAngelscriptFunctionSignature RequiredSignature(HostType.ToSharedRef(), RequiredWorldContextFunction);
FAngelscriptFunctionSignature OptionalSignature(HostType.ToSharedRef(), OptionalWorldContextFunction);
...
TestTrue(TEXT("CallableWithoutWorldContextKeepsHiddenWorldContextButClearsTrait should mark required world-context functions with the world-context trait"), RequiredScriptFunction->traits.GetTrait(asTRAIT_USES_WORLDCONTEXT));
return TestFalse(TEXT("CallableWithoutWorldContextKeepsHiddenWorldContextButClearsTrait should not mark callable-without-world-context functions with the world-context trait"), OptionalScriptFunction->traits.GetTrait(asTRAIT_USES_WORLDCONTEXT));
```

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| `WorldContext` 隐藏参数 | 有 | 有 | **没有差距** |
| `OptionalWorldContext` 清除 trait | 有 | 有 | **没有差距** |
| `CallableWithoutWorldContext` 清除 trait | 有 | helper 未见对应逻辑，测试仍要求它存在 | **实现质量差异 / 潜在回归** |

因此，这一维不能写成“current 没有 world-context metadata 语义”。更精确的说法是：**主干语义还在，但 `CallableWithoutWorldContext` 这条 finer-grained contract 已从 UEAS2 的正式规则退化成了 current 测试仍在声明、helper 却不再等价执行的漂移点。**

### [维度 D8/D11] `UnsafeDuringActorConstruction` 在 UEAS2 是整条 trait 管线；current 已失去等价 owner

```
[D8/D11-Deep] Unsafe During Construction Pipeline
UEAS2
├─ as_scriptfunction.h -> trait bit
├─ as_builder.cpp      -> parse decorator / factory flag
├─ as_compiler.cpp     -> defaults/constructor call guard
├─ FAngelscriptBinds   -> SetPreviousBindUnsafeDuringConstruction()
├─ Bind_UObject.cpp    -> mark NewObject / LoadObject
└─ Helper_FunctionSignature -> bridge UFUNCTION metadata

AngelPortV2
├─ as_scriptfunction.h -> bit reused by asTRAIT_EXPLICIT
├─ Core/AngelscriptBinds.h -> no unsafe setter
├─ Bind_UObject.cpp -> NewObject / LoadObject no longer mark unsafe
└─ Helper_FunctionSignature -> no UnsafeDuringActorConstruction bridge
```

这条链是这轮最明确的“**没有实现等价能力**”，不是“实现方式不同”。  
UEAS2 里，`UnsafeDuringActorConstruction` 不是单个 helper 的局部判断，而是一路贯穿到第三方 AngelScript runtime、compiler 诊断、手写 bind API 和 `UFUNCTION` metadata bridge。current 则把 `0x1000000` 这个 bit 改作 `asTRAIT_EXPLICIT`，同时删掉了 bind setter 和 `Bind_UObject` 上的标记调用，导致这条 trait 已经没有等价 owner。

[1] UEAS2 把 `unsafe_during_construction` 当成正式 trait，既能被 builder 写入，也能被 compiler 在 defaults/constructor 场景拒绝：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\ThirdParty\source\as_scriptfunction.h
// 位置: 120-128
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\ThirdParty\source\as_builder.cpp
// 位置: 1480-1481, 4198-4203
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\ThirdParty\source\as_compiler.cpp
// 位置: 18184-18187
// 说明: 这是 UEAS2 的语言级 / 编译级 contract，而不是插件局部习惯
// ============================================================================
asTRAIT_EDITOR_ONLY = 0x800000,
asTRAIT_UNSAFE_DURING_CONSTRUCTION = 0x1000000,
asTRAIT_INSTANTIATED_TEMPLATE_FUNCTION = 0x2000000,
asTRAIT_DEFAULTS_ONLY = 0x4000000,

else if (source.TokenEquals(n->tokenPos, n->tokenLength, UNSAFE_DURING_CONSTRUCTION_TOKEN))
	func->traits.SetTrait(asTRAIT_UNSAFE_DURING_CONSTRUCTION, true);

scriptFunction->traits.SetTrait(asTRAIT_UNSAFE_DURING_CONSTRUCTION, true);
engine->scriptFunctions[funcId]->traits.SetTrait(asTRAIT_UNSAFE_DURING_CONSTRUCTION, true);

if ((m_isInitDefaults || ((m_isConstructor || m_isDefaultConstructor) && (outFunc->objectType->GetFlags() & asOBJ_REF))) && descr->traits.GetTrait(asTRAIT_UNSAFE_DURING_CONSTRUCTION))
{
	// ★ defaults / constructor 里直接拒绝调用这类函数
	asCString msg;
	msg.Format("Function %s is unsafe during construction and cannot be called from defaults", descr->name.AddressOf());
}
```

[2] UEAS2 在插件桥接层继续把这条 trait 往上传，而 current 对应链路已经断掉：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Public\AngelscriptBinds.h
// 位置: 567-572
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\AngelscriptBinds.cpp
// 位置: 176-180
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Binds\Bind_UObject.cpp
// 位置: 378-389
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Binds\Helper_FunctionSignature.h
// 位置: 34-35, 480-483
// 说明: UEAS2 既能给手写 bind 打 unsafe trait，也能把 UFUNCTION metadata 回填进 script function
// ============================================================================
static void SetPreviousBindRequiresWorldContext(bool bRequiresWorldContext);
static void SetPreviousBindIsPropertyAccessor(bool bIsProperty);
static void SetPreviousBindIsCallable(bool bIsCallable);
static void SetPreviousBindNoDiscard(bool bNoDiscard);
static void SetPreviousBindForceConstArgumentExpressions(bool bForceConst);
static void SetPreviousBindUnsafeDuringConstruction(bool bUnsafe);

void FAngelscriptBinds::SetPreviousBindUnsafeDuringConstruction(bool bUnsafe)
{
	if (auto* Function = (asCScriptFunction*)GetPreviousBind())
	{
		Function->traits.SetTrait(asEFuncTrait::asTRAIT_UNSAFE_DURING_CONSTRUCTION, bUnsafe);
	}
}

FAngelscriptBinds::SetPreviousBindArgumentDeterminesOutputType(1);
FAngelscriptBinds::SetPreviousBindUnsafeDuringConstruction(true);
...
FAngelscriptBinds::BindGlobalFunction("UObject LoadObject(UObject Outer, const FString& Name) no_discard", ...);
FAngelscriptBinds::SetPreviousBindUnsafeDuringConstruction(true);

static const FName NAME_ScriptAllowTemporaryThis("ScriptAllowTemporaryThis");
static const FName NAME_UnsafeDuringActorConstruction("UnsafeDuringActorConstruction");
...
if (Function->HasMetaData(NAME_UnsafeDuringActorConstruction))
{
	if (Function->GetMetaData(NAME_UnsafeDuringActorConstruction) != TEXT("false"))
		ScriptFunction->traits.SetTrait(asTRAIT_UNSAFE_DURING_CONSTRUCTION, true);
}
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptRuntime\ThirdParty\angelscript\source\as_scriptfunction.h
// 位置: 137-140
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptRuntime\Core\AngelscriptBinds.h
// 位置: 633-638
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptRuntime\Binds\Bind_UObject.cpp
// 位置: 579-588
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptRuntime\Binds\Helper_FunctionSignature.h
// 位置: 22-29
// 说明: current 的对应 bit / setter / 手写 bind 标记 / metadata 常量都已不再等价存在
// ============================================================================
asTRAIT_ALLOWDISCARD = 0x400000,
asTRAIT_EDITOR_ONLY = 0x800000,
// ★ 这里的 0x1000000 已被 current 重新分配给 explicit trait
asTRAIT_EXPLICIT = 0x1000000,

static void SetPreviousBindRequiresWorldContext(bool bRequiresWorldContext);
static void SetPreviousBindIsPropertyAccessor(bool bIsProperty);
static void SetPreviousBindIsCallable(bool bIsCallable);
static void SetPreviousBindNoDiscard(bool bNoDiscard);
static void SetPreviousBindForceConstArgumentExpressions(bool bForceConst);
// ★ 当前公开 API 已没有 SetPreviousBindUnsafeDuringConstruction

static const FName NAME_Signature_ScriptMixin("ScriptMixin");
static const FName NAME_Signature_ScriptTrivial("ScriptTrivial");
static const FName NAME_Signature_NotAngelscriptProperty("NotAngelscriptProperty");
static const FName NAME_AS_BlueprintProtected("BlueprintProtected");
static const FName NAME_Function_DeprecatedFunction("DeprecatedFunction");
static const FName NAME_Function_DeprecationMessage("DeprecationMessage");
static const FName NAME_OptionalWorldContext("OptionalWorldContext");
// ★ current helper 常量区已没有 NAME_UnsafeDuringActorConstruction

FAngelscriptBinds::SetPreviousBindArgumentDeterminesOutputType(1);

#if !WITH_ANGELSCRIPT_HAZE
FAngelscriptBinds::BindGlobalFunction(
	  "UObject LoadObject(UObject Outer, const FString& Name)",
[](UObject* Outer, const FString& Name) -> UObject*
{
	return LoadObject<UObject>(Outer, *Name);
});
// ★ 这里也没有任何 unsafe trait 回写
#endif
```

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| `unsafe_during_construction` trait bit | 有正式枚举位 | `0x1000000` 已改作 `asTRAIT_EXPLICIT` | **没有实现等价 trait** |
| compiler defaults/constructor 护栏 | 有 | 未见等价输入 trait 源 | **没有实现** |
| 手写 bind 标记入口 | `SetPreviousBindUnsafeDuringConstruction()` | 当前公开 API 无此入口 | **没有实现** |
| `UFUNCTION` metadata 回填 | helper 会写回 trait | current helper 未见同等级桥接 | **没有实现** |

因此，这一维的结论必须写得更硬一点：**`UnsafeDuringActorConstruction` 在 current 不是“实现方式不同”，而是 UEAS2 那条从 trait bit 到 compiler/bind/UFUNCTION bridge 的完整 contract 已经不闭合。** 如果未来要恢复同等级能力，必须重新回答“trait 位、compiler 护栏、bind API、UFUNCTION metadata 回填”这四个 owner 分别放在哪一层。

---

## 深化分析 (2026-04-09 00:01:37)

### [维度 D3/D8] `DefaultComponent / OverrideComponent` 的 owner 一直在插件内；current 主要改的是默认编辑性策略与验证方式

前文已经分析过脚本类生成与 Blueprint 继承链，但还没把组件组合这条子链单独钉死。这里的新结论是：`DefaultComponent` / `OverrideComponent` 这套能力在 UEAS2 里本来就不是靠引擎 UHT 补丁成立，而是由插件自己的 preprocessor、`UASClass` 元数据和 class generator 落地。  
current 没有丢掉这条链，反而把它固定成更强约束的插件内 contract；真正变化的是 **`DefaultComponent` 默认是否可在 defaults 中编辑** 不再由配置项决定，而是直接写死为 `EditableOnDefaults + EditInlineDefaults`，并补了自动化场景测试。

```
[D3/D8] Script Component Composition Pipeline
Script Property Specifiers
├─ DefaultComponent / OverrideComponent / RootComponent / Attach   // 脚本声明
├─ Preprocessor -> PropDesc flags + meta                           // 预处理写属性语义
├─ UASClass::DefaultComponents / OverrideComponents                // 类元数据缓存
├─ ClassGenerator validates base/abstract/attach chain             // 编译期校验
├─ ASClass runtime creates subobjects + fills override vars        // 运行期实例化
└─ Automation tests spawn actor and verify hierarchy               // current 新增回归约束
```

[1] UEAS2 把 `DefaultComponent` 的默认编辑性挂在 settings 开关上：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Public\AngelscriptSettings.h
// 位置: 101-103
// 说明: UEAS2 允许通过配置项决定 DefaultComponent 默认是可编辑还是只读显示
// ============================================================================
/* Whether to mark DefaultComponents as EditDefaultsOnly by default. Otherwise, they will be VisibleAnywhere by default */
UPROPERTY(Config, EditDefaultsOnly, Category = "Backwards Compatibility", Meta = (ConfigRestartRequired = true))
bool bDefaultComponentsEditable = false;
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Preprocessor\AngelscriptPreprocessor.cpp
// 位置: 2465-2488
// 说明: UEAS2 的 DefaultComponent 行为不是固定值，而是读取全局配置分支
// ============================================================================
else if (Spec.Name == PP_NAME_DefaultComponent)
{
	if (!bHadShowOnActor)
	{
		if (FAngelscriptManager::ConfigSettings->bDefaultComponentsEditable)
		{
			// ★ 打开配置时走 EditDefaultsOnly
			PropDesc->bEditableOnDefaults = true;
			PropDesc->bEditConst = false;
		}
		else
		{
			// ★ 否则退回 VisibleAnywhere 风格
			PropDesc->bEditableOnDefaults = false;
			PropDesc->bEditConst = true;
		}
		PropDesc->bEditableOnInstance = false;
	}

	PropDesc->bBlueprintWritable = false;
	PropDesc->bBlueprintReadable = true;
	PropDesc->bInstancedReference = true;
	bIsDefaultComponent = true;
	PropDesc->Meta.Add(Spec.Name, TEXT("True"));
}
```

[2] current 把这条分支收敛成固定策略，并额外写入 `EditInlineDefaults`：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptRuntime\Preprocessor\AngelscriptPreprocessor.cpp
// 位置: 2650-2667
// 说明: current 不再保留 bDefaultComponentsEditable，直接把 DefaultComponent 视为 defaults 可编辑的内联子对象
// ============================================================================
else if (Spec.Name == PP_NAME_DefaultComponent)
{
	if (!bHadShowOnActor)
	{
		// ★ 不再读全局配置，行为固定
		PropDesc->bEditConst = false;
		PropDesc->bEditableOnDefaults = true;
		PropDesc->bEditableOnInstance = false;
	}

	PropDesc->bBlueprintWritable = false;
	PropDesc->bBlueprintReadable = true;
	PropDesc->bInstancedReference = true;
	bIsDefaultComponent = true;

	// ★ current 新增内联 defaults 元数据
	PropDesc->Meta.Add(PP_NAME_EditInlineDefaults, TEXT("true"));
	PropDesc->Meta.Add(Spec.Name, TEXT("True"));
}
```

[3] current 仍保留 UEAS2 的组件继承/覆写编译期校验，并在运行时实际创建组件树：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptRuntime\ClassGenerator\AngelscriptClassGenerator.cpp
// 位置: 5360-5443, 5498-5526
// 说明: current 继续沿用“先找可覆写父组件，再验证抽象父类必须被 override”的编译期规则
// ============================================================================
for (const auto& DefComp : ParentASClass->DefaultComponents)
{
	if (DefComp.ComponentName == Comp.OverrideComponentName)
	{
		// ★ 先从父脚本类里找同名默认组件
		ClassOfOverrideComponent = DefComp.ComponentClass;
		break;
	}
}
...
if (ClassOfOverrideComponent == nullptr)
{
	FAngelscriptEngine::Get().ScriptCompileError(
		ModuleData.NewModule, Property->LineNumber,
		FString::Printf(TEXT("OverrideComponent %s::%s could not find component %s in base class to override."),
		*ClassDesc->ClassName, *Property->PropertyName, *Comp.OverrideComponentName.ToString()));
	ModuleData.NewModule->bModuleSwapInError = true;
	continue;
}
else if (!Comp.ComponentClass->IsChildOf(ClassOfOverrideComponent))
{
	// ★ 类型不兼容直接编译失败，而不是拖到运行时
	FAngelscriptEngine::Get().ScriptCompileError(...);
	ModuleData.NewModule->bModuleSwapInError = true;
	continue;
}
...
if (PropertyClass && PropertyClass->HasAnyClassFlags(CLASS_Abstract))
{
	if (!bFoundOverride)
	{
		// ★ 抽象父组件必须显式 override
		FAngelscriptEngine::Get().ScriptCompileError(
			ModuleData.NewModule, ClassDesc->LineNumber,
			FString::Printf(TEXT("OverrideComponent for %s::%s missing that's specificed in base class %s. Component override is needed because component class %s is Abstract."),
				*ClassDesc->ClassName, *Property->GetName(), *ParentClass->GetName(), *PropertyClass->GetName()));
		ModuleData.NewModule->bModuleSwapInError = true;
	}
}
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptRuntime\ClassGenerator\ASClass.cpp
// 位置: 1176-1198, 1202-1254, 1331-1344
// 说明: 运行时 owner 仍在插件自己，不依赖引擎生成额外构造逻辑
// ============================================================================
static FORCEINLINE_DEBUGGABLE void ApplyOverrideComponents(const FObjectInitializer& Initializer, AActor* Actor, UASClass* ScriptClass)
{
	for(int32 i = 0, Count = ScriptClass->OverrideComponents.Num(); i < Count; ++i)
	{
		auto& Override = ScriptClass->OverrideComponents[i];
		UClass* ComponentClass = Override.ComponentClass;
		// ★ 在 Actor 初始化前改写默认子对象类
		Initializer.SetDefaultSubobjectClass(Override.OverrideComponentName, ComponentClass);
	}
}

static FORCEINLINE_DEBUGGABLE void CreateDefaultComponents(const FObjectInitializer& Initializer, AActor* Actor, UASClass* ScriptClass)
{
	for(int32 i = 0, Count = ScriptClass->DefaultComponents.Num(); i < Count; ++i)
	{
		auto& DefaultComponent = ScriptClass->DefaultComponents[i];
		// ★ 运行时按 UASClass 记录的定义创建默认组件
		UActorComponent* NewComp = NewObject<UActorComponent>(Actor, ComponentClass, DefaultComponent.ComponentName, RF_DefaultSubObject);
		...
		UActorComponent** VariablePtr = (UActorComponent**)((SIZE_T)Actor + DefaultComponent.VariableOffset);
		*VariablePtr = NewComp;
	}
}

for (auto* CheckComponent : Actor->GetComponents())
{
	if (CheckComponent->GetFName() == Override.OverrideComponentName)
	{
		// ★ 覆写属性最终回填到脚本字段
		*VariablePtr = CheckComponent;
		break;
	}
}
```

[4] current 还把这条链锁进了自动化场景，而不是只靠人工试用：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptTest\Component\AngelscriptComponentScenarioTests.cpp
// 位置: 385-389, 456-462, 492-511
// 说明: current 对 DefaultComponent 根节点和 Attach 关系都有显式回归测试
// ============================================================================
class AScenarioDefaultComponentBasic : AActor
{
	UPROPERTY(DefaultComponent, RootComponent)
	UScenarioDefaultComponentBasicRoot RootScene;
}
...
class AScenarioDefaultComponentMultiple : AActor
{
	UPROPERTY(DefaultComponent, RootComponent)
	UScenarioDefaultComponentMultipleRoot RootScene;

	UPROPERTY(DefaultComponent, Attach = RootScene)
	UScenarioDefaultComponentMultipleBillboard Billboard;
}
...
TestTrue(TEXT("Scenario actor root component should use the scripted root component class"), RootScene->IsA(RootSceneClass));
TestTrue(TEXT("Scenario actor attached default component should preserve the scripted hierarchy"), Billboard->GetAttachParent() == RootScene);
```

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| `DefaultComponent` / `OverrideComponent` owner | 插件内 `Preprocessor + UASClass + ClassGenerator` | 同样在插件内 `Preprocessor + UASClass + ClassGenerator` | **没有实现差距** |
| 默认编辑性策略 | `bDefaultComponentsEditable` 配置驱动 | 固定为 `EditableOnDefaults + EditInlineDefaults` | **实现方式不同** |
| 抽象父组件必须 override | 有编译期报错 | 有等价编译期报错 | **没有实现差距** |
| 组件树回归验证 | 主要靠上游工程使用场景 | current 有专门自动化场景 | **实现质量增强** |

因此，这一维更准确的说法是：**current 没有失去 Hazelight 的脚本组件继承/覆写能力；它收缩的是“默认编辑性可配置”这根 back-compat 旋钮，同时把组件组合从“约定”推进成“受测试约束的插件 contract”。**

### [维度 D3] `UserConstructionScript` 在两边都不是普通 Blueprint 函数，而是被硬编码保留的事件覆写入口

前文讨论了 Blueprint 事件和 mixin，但没有把 `ConstructionScript` 这条特殊入口单独拉出来。这里的新增发现是：`ConstructionScript` 并不是“恰好也能被脚本调用”的普通 `UFUNCTION`，而是在 `Bind_BlueprintEvent.cpp` 里被显式当作 `BlueprintInternalUseOnly` 的例外保留下来。  
这意味着它与 `BeginPlay`、`Tick` 一样，走的是 **Blueprint event override** 主链，而不是单独的 helper/namespace function 旁路。

```
[D3] ConstructionScript Override Chain
C++ Blueprint Event Surface
├─ BlueprintInternalUseOnly on UserConstructionScript         // UE 原生对外不鼓励直接暴露
├─ Bind_BlueprintEvent hardcoded exception                    // Angelscript 明确保留该事件
├─ UFUNCTION(BlueprintOverride) / event validation            // 仍走标准 override 校验
├─ Script function emitted into generated UClass              // 进入脚本类 vtable / wrapper
└─ Construction-time actor setup runs script body             // 组件创建与派生属性初始化
```

[1] UEAS2 明确把 `UserConstructionScript` 从 `BlueprintInternalUseOnly` 过滤里放行：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Binds\Bind_BlueprintEvent.cpp
// 位置: 457-486
// 说明: 上游不是泛泛地“支持 ConstructionScript”，而是把它写成硬编码例外
// ============================================================================
static const FName NAME_Event_ConstructionScript("UserConstructionScript");
static const FName NAME_Event_AllowAngelscriptOverride("AllowAngelscriptOverride");
...
// BlueprintInternalUseOnly functions are not bound, with the hardcoded exception of constructionscript
if (Function->HasMetaData(NAME_Event_BlueprintInternalUseOnly)
	&& Function->GetFName() != NAME_Event_ConstructionScript
	&& !Function->HasMetaData(NAME_Event_AllowAngelscriptOverride))
{
	return;
}
```

[2] current 保留了同一条硬编码 contract；因此 Loader 被删、UHT 替代链引入，都没有改掉 ConstructionScript 入口本身：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptRuntime\Binds\Bind_BlueprintEvent.cpp
// 位置: 548-578
// 说明: current 的过滤条件与 UEAS2 在这一点上仍然等价
// ============================================================================
static const FName NAME_Event_BlueprintInternalUseOnly("BlueprintInternalUseOnly");
static const FName NAME_Event_ConstructionScript("UserConstructionScript");
static const FName NAME_Event_AllowAngelscriptOverride("AllowAngelscriptOverride");
...
// BlueprintInternalUseOnly functions are not bound, with the hardcoded exception of constructionscript
if (Function->HasMetaData(NAME_Event_BlueprintInternalUseOnly)
	&& Function->GetFName() != NAME_Event_ConstructionScript
	&& !Function->HasMetaData(NAME_Event_AllowAngelscriptOverride))
{
	return;
}
```

[3] 覆写语义仍由 `BlueprintOverride` 正式进入 class generator 校验，而不是“ConstructionScript 特判后直接跳过验证”：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptRuntime\Preprocessor\AngelscriptPreprocessor.cpp
// 位置: 1654-1678
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptRuntime\ClassGenerator\AngelscriptClassGenerator.cpp
// 位置: 732-829
// 说明: current 仍要求 BlueprintOverride 走标准继承链匹配，不存在“ConstructionScript 免校验”
// ============================================================================
else if (Spec.Name == PP_NAME_BlueprintOverride)
{
	if (FunctionDesc->bIsStatic)
	{
		MacroError(File, Macro, FString::Printf(TEXT("Global UFUNCTION() %s may not be BlueprintOverride."), *FunctionDesc->FunctionName));
		continue;
	}

	FunctionDesc->bBlueprintEvent = true;
	FunctionDesc->bBlueprintOverride = true;
	// ★ wrapper 沿用父类，不是旁路生成
}
...
if (FunctionDesc->bBlueprintOverride)
{
	auto* ParentFunction = GetBlueprintEventByScriptName(CodeSuperClass, FunctionDesc->FunctionName);
	...
	if (ParentFunction == nullptr)
	{
		FAngelscriptEngine::Get().ScriptCompileError(
			ModuleData.NewModule, FunctionDesc->LineNumber,
			FString::Printf(TEXT("BlueprintOverride method %s in class %s does not exist in superclass %s, or is not a BlueprintImplementableEvent or BlueprintNativeEvent in C++."),
				*FunctionDesc->FunctionName, *ClassData.NewClass->ClassName, *CodeSuperClass->GetName()));
		ClassData.ReloadReq = EReloadRequirement::Error;
	}
}
```

[4] UEAS2 把 ConstructionScript 当作正式示例；current 至少把这条语义写进了回归测试夹具：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Script-Examples\Examples\Example_ConstructionScript.as
// 位置: 22-32
// 说明: UEAS2 的用户示例直接示范 BlueprintOverride 版本的 ConstructionScript
// ============================================================================
/* The overridden construction script will run when needed. */
UFUNCTION(BlueprintOverride)
void ConstructionScript()
{
	// ★ 在 construction 阶段动态创建组件
	Billboard = UBillboardComponent::Create(this, n"Billboard");
	Billboard.SetHiddenInGame(false);

	// ★ 同时写派生属性
	Product = ValueA * ValueB;
}
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptTest\Examples\AngelscriptScriptExampleConstructionScriptTest.cpp
// 位置: 9-18, 32-38, 48-52
// 说明: current 把同类 construction-script 场景纳入自动化编译测试
// ============================================================================
const AngelscriptScriptExamples::FScriptExampleSource GConstructionScriptExample = {
	TEXT("Example_ConstructionScript.as"),
	TEXT(R"ANGELSCRIPT(/*
 * This is an example on how to use construction scripts
 * for angelscript classes.
 */
class AExampleConstructionScript_UnitTest : AActor
{
	...
	UFUNCTION()
	void ConstructionScript()
	{
		// ★ 回归测试仍覆盖“construction 阶段创建组件 + 写派生属性”这件事
		Billboard = UBillboardComponent::Create(this, n"Billboard");
		Product = ValueA * ValueB;
	}
};)ANGELSCRIPT"),
	...
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(..., "Angelscript.TestModule.ScriptExamples.ConstructionScript", ...)
bool FAngelscriptScriptExampleConstructionScriptTest::RunTest(const FString& Parameters)
{
	return AngelscriptScriptExamples::RunScriptExampleCompileTest(*this, GConstructionScriptExample);
}
```

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| `UserConstructionScript` 绑定入口 | `Bind_BlueprintEvent.cpp` 硬编码放行 | 同样硬编码放行 | **没有实现差距** |
| 覆写校验 owner | `BlueprintOverride + ClassGenerator` | `BlueprintOverride + ClassGenerator` | **没有实现差距** |
| construction-script 场景保真 | 有独立示例脚本 | 有自动化示例夹具 | **实现方式不同** |
| 回归约束 | 以示例和集成为主 | current 显式编译测试 | **实现质量增强** |

所以这一维不应写成“current 对 ConstructionScript 的支持弱化了”。更准确的是：**入口和 override contract 还在，变化主要发生在 onboarding 载体上；Hazelight 更像示例资产，current 更像回归测试资产。**

### [维度 D8/D11] UEAS2 的引擎补丁真正拆成了三类 owner；current 则把它们分流到 `UHTTool`、preprocessor 和 runtime helper

前文已经写过 current 通过 `AngelscriptUHTTool` 绕开了引擎补丁，但这里还缺一个更细的 owner 拆解。  
这轮新增结论是：UEAS2 的引擎改造并不是一整坨“让 UHT 认识 Angelscript”，而是至少分成三类职责：

1. `ScriptCallable` 这类函数 authoring 语义进入 UHT 解析与默认值导出。
2. `AngelscriptPropertyFlags` 这类属性级别信息进入 `.gen.cpp` / exporter。
3. `WorldContext` 这类参数语义在 UHT 解析期就被打成 flag。  

current 对这三类职责没有做“一比一 UHT 补丁复刻”，而是显式分流：

- **函数覆盖面**：交给插件内 `AngelscriptUHTTool` 产出 `AS_FunctionTable_*.cpp`
- **组件/property specifier**：继续留在插件 preprocessor / class generator
- **脚本签名解释**：留给 runtime helper 与 reflective fallback

```
[D8/D11] UHT Responsibility Split
UEAS2 patched engine
├─ EpicGames.UHT specifier parser              // ScriptCallable 等 authoring 语义
├─ UhtFunction/UhtProperty state               // 默认值 / property flags / world-context flag
├─ CodeGen exporter emits Angelscript payload  // .gen.cpp 直接带 Angelscript 信息
└─ Runtime/plugin consumes engine-generated data

AngelPortV2 plugin-local split
├─ AngelscriptUHTTool                          // 只生成 BlueprintCallable function table
├─ Runtime Preprocessor                        // DefaultComponent / BlueprintOverride 等脚本 specifier
├─ ClassGenerator / Bind helpers               // 解释 property/function 元数据
└─ Reflective fallback / tests                 // 弥补非 direct-bind 覆盖缺口
```

[1] UEAS2 直接改 UHT 语义表，让 `ScriptCallable` 进入 UHT 生命周期，并把 property flag/world-context 信息塞进生成路径：

```csharp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Programs\Shared\EpicGames.UHT\Specifiers\UhtFunctionSpecifiers.cs
// 位置: 23-29
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Programs\Shared\EpicGames.UHT\Types\UhtFunction.cs
// 位置: 570-573
// 说明: 上游把 ScriptCallable 作为 UHT 识别的正式 specifier，而不是插件后处理约定
// ============================================================================
[UhtSpecifier(Extends = UhtTableNames.Function, ValueType = UhtSpecifierValueType.Legacy)]
private static void ScriptCallableSpecifier(UhtSpecifierContext specifierContext)
{
	UhtFunction function = (UhtFunction)specifierContext.Type;
	// ★ UHT 直接把 ScriptCallable 写入 metadata
	function.MetaData.Add("ScriptCallable", "");
}
...
bool storeCppDefaultValueInMetaData = FunctionFlags.HasAnyFlags(EFunctionFlags.BlueprintCallable | EFunctionFlags.Exec)
	// ★ ScriptCallable 也进入默认值导出逻辑
	|| MetaData.ContainsKey("ScriptCallable");
```

```csharp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Programs\Shared\EpicGames.UHT\Parsers\UhtFunctionParser.cs
// 位置: 575-585
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Programs\Shared\EpicGames.UHT\Types\UhtProperty.cs
// 位置: 1508-1509
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Programs\Shared\EpicGames.UHT\Exporters\CodeGen\UhtHeaderCodeGeneratorCppFile.cs
// 位置: 2548-2571
// 说明: property flags 与 world-context 语义都在 UHT 期就被注入生成结果
// ============================================================================
if (function.MetaData.ContainsKey("WorldContext"))
{
	...
	// ★ UHT 解析阶段直接给参数打 WorldContext flag
	Property.AngelscriptPropertyFlags |= EAngelscriptPropertyFlags.WorldContext;
}
...
// ★ `.gen.cpp` 里额外写入 AngelscriptPropertyFlags
builder.Append("0x").AppendFormat(CultureInfo.InvariantCulture, "{0:x4}", (ushort)AngelscriptPropertyFlags).Append(", ");
...
bool bIsRef = Property.AngelscriptPropertyFlags.HasFlag(EAngelscriptPropertyFlags.CppRef);
bool bIsConst = Property.AngelscriptPropertyFlags.HasFlag(EAngelscriptPropertyFlags.CppConst);
if (Property is UhtByteProperty ByteProp && ByteProp.Enum != null && ByteProp.AngelscriptPropertyFlags.HasFlag(EAngelscriptPropertyFlags.CppEnumAsByte))
{
	// ★ exporter 继续消费这些额外 flag
	builder.Append("TEnumAsByte<");
}
```

[2] current 的 `UHTTool` 明确只接管函数表生成，不试图把全部 property 语义重新塞回 Unreal 原生 codegen：

```csharp
// ============================================================================
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptUHTTool\AngelscriptFunctionTableCodeGenerator.cs
// 位置: 51-79, 282-320, 497-505
// 说明: current 的 UHT owner 被收窄到“生成 BlueprintCallable function table + 诊断产物”
// ============================================================================
public static int Generate(IUhtExportFactory factory)
{
	...
	foreach (UhtModule module in factory.Session.Modules)
	{
		// ★ 只处理受支持模块，目标是函数表产物
		AngelscriptModuleGenerationSummary? moduleSummary = GenerateModule(...);
		...
	}
	DeleteStaleOutputs(factory, generatedPaths);
	WriteGenerationSummary(factory, moduleSummaries, csvEntries, generatedFileCount);
}
...
builder.Append("AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_AS_FunctionTable_")
	.Append(moduleShortName)
	.Append('_')
	.Append(shardIndex.ToString("D3"));
...
UE_LOG(Angelscript, Log, TEXT("[UHT] Registered %d generated BlueprintCallable entries for module %s shard %d/%d"), ...);
...
if (!AngelscriptFunctionTableExporter.IsBlueprintCallable(function))
	return false;

if (function.MetaData.ContainsKey("NotInAngelscript") ||
	(function.MetaData.ContainsKey("BlueprintInternalUseOnly") && !function.MetaData.ContainsKey("UsableInAngelscript")))
{
	// ★ 当前 UHTTool 的过滤面聚焦在函数暴露，不负责完整 property flag 复制
	return false;
}
```

[3] 与之对应，`DefaultComponent` 这类脚本 specifier 在 current 继续留在插件 preprocessor，不依赖 UHT 引擎补丁：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptRuntime\Preprocessor\AngelscriptPreprocessor.cpp
// 位置: 2360-2375, 2650-2667
// 说明: current 把 component/property authoring 语义留在插件预处理器，而不是反推 UHT 引擎改造
// ============================================================================
static FName PP_NAME_DefaultComponent("DefaultComponent");
static FName PP_NAME_OverrideComponent("OverrideComponent");
static FName PP_NAME_ShowOnActor("ShowOnActor");
static FName PP_NAME_RootComponent("RootComponent");
static FName PP_NAME_Attach("Attach");
static FName PP_NAME_AttachSocket("AttachSocket");
...
static FName PP_NAME_EditInlineDefaults("EditInlineDefaults");
...
else if (Spec.Name == PP_NAME_DefaultComponent)
{
	PropDesc->bEditConst = false;
	PropDesc->bEditableOnDefaults = true;
	PropDesc->bEditableOnInstance = false;
	PropDesc->bInstancedReference = true;
	bIsDefaultComponent = true;

	// ★ 这类语义仍在插件内建模
	PropDesc->Meta.Add(PP_NAME_EditInlineDefaults, TEXT("true"));
	PropDesc->Meta.Add(Spec.Name, TEXT("True"));
}
```

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| 函数 authoring specifier owner | 引擎补丁 UHT 直接识别 `ScriptCallable` | 插件内 `UHTTool` 以 `BlueprintCallable + filter` 生成 function table | **实现方式不同** |
| property/world-context flag 传输 | UHT 解析期注入 `AngelscriptPropertyFlags` | 多数语义拆到 preprocessor / helper / runtime 解释 | **实现方式不同** |
| `DefaultComponent` 语义 owner | 插件 preprocessor / class generator | 同样在插件 preprocessor / class generator | **没有实现差距** |
| 生成产物可观测性 | 依赖引擎 codegen 与插件消费 | current 有 shard、summary、stale cleanup、测试对账 | **实现质量增强** |

因此，`D8/D11` 的更细结论是：**current 并没有“完整复刻 UEAS2 的 UHT 补丁”，而是把 Hazelight 原本塞进引擎的三类 owner 重新拆散。** 这也是为什么有些能力在 current 上看起来“不像 UHT 补丁”，但依然能闭环工作。

### [维度 D1/D4] 移除 `AngelscriptLoader` 的真正增量不是少一个模块，而是多了显式 teardown 和测试注入 seam

前文已经说明 `Loader` 在 UEAS2 里很薄，但还没有把 **teardown owner** 和 **testing seam** 单独拉出来。  
这里的新发现是：UEAS2 的 `Loader` / `CodeModule` 在模块生命周期上几乎是“只起不收”，而 current 在 `AngelscriptRuntimeModule` 里把 fallback ticker、owned engine 和测试注入都变成了显式 API。  
所以移除 `Loader` 不只是模块名变少，而是把“初始化入口”升级成了“初始化 + 清理 + 可替换初始化源”的完整 contract。

```
[D1/D4] Initialization Ownership After Loader Removal
UEAS2
├─ Loader::StartupModule -> Code::InitializeAngelscript   // 启动转发
├─ Loader::ShutdownModule -> no-op                        // 无显式清理
└─ CodeModule::ShutdownModule -> no-op                    // 无测试注入 seam

AngelPortV2
├─ RuntimeModule::StartupModule
│  ├─ InitializeAngelscript()
│  └─ Add fallback ticker
├─ RuntimeModule::ShutdownModule
│  ├─ Remove ticker
│  └─ Pop OwnedPrimaryEngine from context stack
└─ Testing seam
   ├─ SetInitializeOverrideForTesting()
   └─ ResetInitializeStateForTesting()
```

[1] UEAS2 的 `Loader` 和 `CodeModule` 在生命周期上都非常轻，`ShutdownModule()` 为空：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptLoader\Private\AngelscriptLoaderModule.cpp
// 位置: 6-13
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\AngelscriptCodeModule.cpp
// 位置: 8-14
// 说明: 上游 Loader 真正负责的是启动转发，不负责显式 teardown
// ============================================================================
void FAngelscriptLoaderModule::StartupModule()
{
	// ★ 仅仅转发初始化
	FAngelscriptCodeModule::InitializeAngelscript();
}

void FAngelscriptLoaderModule::ShutdownModule()
{
}

void FAngelscriptCodeModule::StartupModule()
{
}

void FAngelscriptCodeModule::ShutdownModule()
{
}
```

[2] current 把 startup/shutdown 都收进 `RuntimeModule`，并且显式管理 fallback ticker 与 owned engine：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptRuntime\Core\AngelscriptRuntimeModule.cpp
// 位置: 13-40, 138-182, 186-199
// 说明: current 的 runtime module 不是单向入口，而是完整生命周期 owner
// ============================================================================
void FAngelscriptRuntimeModule::StartupModule()
{
	if (GIsEditor || IsRunningCommandlet())
	{
		// ★ 初始化入口直接内收
		InitializeAngelscript();
	}

	if (GIsEditor)
	{
		// ★ 增加 fallback tick owner
		FallbackTickHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateRaw(this, &FAngelscriptRuntimeModule::TickFallbackPrimaryEngine));
	}
}

void FAngelscriptRuntimeModule::ShutdownModule()
{
	if (FallbackTickHandle.IsValid())
	{
		// ★ 显式拆除 ticker
		FTSTicker::GetCoreTicker().RemoveTicker(FallbackTickHandle);
		FallbackTickHandle.Reset();
	}

	if (OwnedPrimaryEngine.IsValid())
	{
		// ★ 显式从 context stack 弹出 owned engine
		FAngelscriptEngineContextStack::Pop(OwnedPrimaryEngine.Get());
		OwnedPrimaryEngine.Reset();
	}
}
...
if (InitializeOverrideForTesting)
{
	if (FAngelscriptEngine* OverrideEngine = InitializeOverrideForTesting())
	{
		// ★ 测试可替换初始化源
		FAngelscriptEngineContextStack::Push(OverrideEngine);
	}
	return;
}
...
void FAngelscriptRuntimeModule::ResetInitializeStateForTesting()
{
	if (OwnedPrimaryEngine.IsValid())
	{
		FAngelscriptEngineContextStack::Pop(OwnedPrimaryEngine.Get());
		OwnedPrimaryEngine.Reset();
	}
	bInitializeAngelscriptCalled = false;
	InitializeOverrideForTesting = nullptr;
}
```

[3] 这些 testing seam 不是死代码，current 的 subsystem 测试会直接访问：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptRuntime\Tests\AngelscriptSubsystemTests.cpp
// 位置: 90-110
// 说明: current 已经把模块初始化状态视为可测试对象，而不是只能靠编辑器启动隐式覆盖
// ============================================================================
struct FAngelscriptRuntimeModuleTickTestAccess
{
	static void ResetInitializeState()
	{
		// ★ 测试直接重置模块初始化状态
		FAngelscriptRuntimeModule::ResetInitializeStateForTesting();
	}

	static void SetInitializeOverride(TFunction<FAngelscriptEngine*()> InOverride)
	{
		// ★ 测试直接注入替代 engine
		FAngelscriptRuntimeModule::SetInitializeOverrideForTesting(MoveTemp(InOverride));
	}

	static bool HasFallbackTicker(const FAngelscriptRuntimeModule& Module)
	{
		return Module.FallbackTickHandle.IsValid();
	}
};
```

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| 初始化入口 | `Loader -> Code::InitializeAngelscript()` | `RuntimeModule::InitializeAngelscript()` 直接内收 | **实现方式不同** |
| 模块 teardown | `Loader` / `CodeModule` 均无显式清理 | 显式移除 ticker、弹出 owned engine | **实现质量增强** |
| 测试注入 seam | 未见模块级 override/reset API | `SetInitializeOverrideForTesting` / `ResetInitializeStateForTesting` | **实现质量增强** |
| Loader 移除后的热重载/生命周期含义 | 薄转发模块 | 完整生命周期 owner | **架构增强** |

所以 `Loader` 被移除之后，差距判断不应写成“current 少了一个上游模块”。更准确的是：**current 用更强的生命周期 owner 替代了 Loader 这个薄壳入口，并额外获得了确定性的 teardown 与可测试初始化 seam。**

---

## 深化分析 (2026-04-09 00:14:20)

### [维度 D3/D4] `Script*Subsystem` 在 current 里形成了“支架保留、正向能力未闭环”的分裂 contract

前文已经讨论过 `UAngelscriptGameInstanceSubsystem` 如何接管主引擎 tick，但那还是“插件自己的宿主管理层”；这轮补的是另一条更容易混淆的链：**脚本自己定义 `UScriptWorldSubsystem` / `UScriptGameInstanceSubsystem` 子类到底处于什么状态。**

新的源码结论是：

1. UEAS2 在 **base class / preprocessor / hot-reload / editor guard** 四层都给 subsystem 铺了正向通路。  
2. current 仍保留其中大部分底层支架，甚至把 base class 表面做得更开放；但 `WorldSubsystem` / `GameInstanceSubsystem` 的脚本生成在当前分支里仍被测试明确钉成“应当编译失败”。  
3. 因此这里不能简单归类成“完全没有实现”，更准确的判断是：**底层机制大多还在，但 feature closure 没有完成，且公开 surface 与真实能力之间出现了 contract 失配。**

```
[D3/D4] Script Subsystem Contract Split
UEAS2
├─ BaseClasses: NotBlueprintable + BP_* lifecycle      // 基类明确提供生命周期回调
├─ Preprocessor: auto-generate static Get()            // 预处理器补静态获取入口
├─ ClassGenerator: deactivate/activate on reload       // 热重载后重挂 subsystem
└─ Bind_Subsystems: NoBlueprintsOfChildren guard       // 编辑器侧明确禁止子蓝图

AngelPortV2
├─ BaseClasses: Blueprintable + streaming hook         // 表面能力更开放
├─ Preprocessor: same static Get() generation          // helper 仍在
├─ ClassGenerator: same reload reactivation path       // reload 支架仍在
└─ Tests: world/game-instance script subsystem FAIL    // 正向脚本生成尚未闭环
```

[1] UEAS2 的 world/game-instance subsystem 基类本身就是“可 tick 的脚本宿主”，并不是只留了一个空壳类型：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Public\BaseClasses\ScriptWorldSubsystem.h
// 位置: 9-18, 52-60, 91-117
// 说明: UEAS2 直接把 subsystem 生命周期回调定义成脚本 override contract
// ============================================================================
UCLASS(NotBlueprintable, Abstract, Meta = (NoBlueprintsOfChildren))
class ANGELSCRIPTCODE_API UScriptWorldSubsystem : public UWorldSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

	bool bInitialized = false;

public:
	// ★ world subsystem 在基类层就自带 editor/game world 筛选与 tick contract
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "World Subsystem")
	bool bCreateForLevelEditorWorlds = false;
	...
	virtual void Initialize(FSubsystemCollectionBase& Collection) override
	{
		Super::Initialize(Collection);

		bInitialized = true;
		if (!IsUnreachable())
			BP_Initialize();
	}
	...
	UFUNCTION(BlueprintImplementableEvent)
	void BP_Initialize();

	UFUNCTION(BlueprintImplementableEvent)
	void BP_Deinitialize();

	UFUNCTION(BlueprintImplementableEvent)
	void BP_PostInitialize();

	UFUNCTION(BlueprintImplementableEvent)
	void BP_OnWorldBeginPlay();

	UFUNCTION(BlueprintImplementableEvent)
	void BP_OnWorldComponentsUpdated();
```

[2] 这条通路在 UEAS2 里并不止停留在基类。预处理器会为 subsystem 脚本类自动生成 `Get()` helper，bind 层还会主动把 subsystem 子类蓝图入口封死，避免 editor surface 与真实 runtime contract 打架：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Preprocessor\AngelscriptPreprocessor.cpp
// 位置: 1003-1027
// 说明: subsystem 脚本类在预处理阶段会获得静态 Get() 入口
// ============================================================================
else if (ClassDesc->CodeSuperClass->IsChildOf(UGameInstanceSubsystem::StaticClass()))
{
	bHasStatics = true;

	GeneratedStatics += FString::Printf(
		TEXT("\n %s Get() __generated no_discard {")
		TEXT("return Cast<%s>(Subsystem::GetGameInstanceSubsystem(%s.Get()));")
		TEXT("}"),
		*ClassDesc->ClassName,
		*ClassDesc->ClassName,
		*ClassDesc->StaticClassGlobalVariableName
	);
}
else if (ClassDesc->CodeSuperClass->IsChildOf(UWorldSubsystem::StaticClass()))
{
	bHasStatics = true;
	GeneratedStatics += FString::Printf(
		TEXT("\n %s Get() __generated no_discard {")
		TEXT("return Cast<%s>(Subsystem::GetWorldSubsystem(%s.Get()));")
		TEXT("}"),
		*ClassDesc->ClassName,
		*ClassDesc->ClassName,
		*ClassDesc->StaticClassGlobalVariableName
	);
}
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Binds\Bind_Subsystems.cpp
// 位置: 125-128
// 说明: UEAS2 明确阻止基于 Angelscript subsystem class 再造 Blueprint 子类
// ============================================================================
#if WITH_EDITOR
	// ★ editor surface 被显式收窄，避免暴露一条不稳定的子蓝图路径
	USubsystem::StaticClass()->SetMetaData(TEXT("NoBlueprintsOfChildren"), TEXT(""));
#endif
```

[3] current 没有删掉 subsystem 底层支架，反而把 base class surface 放宽了。`UScriptWorldSubsystem` 变成 `Blueprintable`，还新增了 `IStreamingWorldSubsystemInterface` 与 `BP_UpdateStreamingState()`：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptRuntime\BaseClasses\ScriptWorldSubsystem.h
// 位置: 7-9, 100-129
// 说明: current 的 base class surface 比 UEAS2 更开放，但这不等于正向脚本生成已经闭环
// ============================================================================
UCLASS(Blueprintable, Abstract)
class ANGELSCRIPTRUNTIME_API UScriptWorldSubsystem : public UWorldSubsystem, public FTickableGameObject, public IStreamingWorldSubsystemInterface
{
	GENERATED_BODY()
	...
	virtual void OnUpdateStreamingState() override
	{
		if (!IsUnreachable())
			BP_UpdateStreamingState();
	}

	UFUNCTION(BlueprintNativeEvent)
	bool BP_ShouldCreateSubsystem(UObject* Outer) const;

	UFUNCTION(BlueprintImplementableEvent)
	void BP_Initialize();

	UFUNCTION(BlueprintImplementableEvent)
	void BP_Deinitialize();

	UFUNCTION(BlueprintImplementableEvent)
	void BP_PostInitialize();

	UFUNCTION(BlueprintImplementableEvent)
	void BP_OnWorldBeginPlay();

	UFUNCTION(BlueprintImplementableEvent)
	void BP_OnWorldComponentsUpdated();

	UFUNCTION(BlueprintImplementableEvent)
	void BP_UpdateStreamingState();

	UFUNCTION(BlueprintImplementableEvent)
	void BP_Tick(float DeltaTime);
```

[4] current 的 class generator 也还保留了 subsystem 热重载的 deactivate/reactivate 路径；也就是说，reload substrate 并没有被删：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptRuntime\ClassGenerator\AngelscriptClassGenerator.cpp
// 位置: 2642-2656
// 说明: subsystem class 仍然会在 full reload 中走停用/重挂流程
// ============================================================================
// If we're creating a new dynamic subsystem class, mark it
if (ClassDesc->CodeSuperClass->IsChildOf<UDynamicSubsystem>() || ClassDesc->CodeSuperClass->IsChildOf<UWorldSubsystem>())
{
	if (ReplacedClass != nullptr)
		FSubsystemCollectionBase::DeactivateExternalSubsystem(ReplacedClass);
	// ★ reload 后仍会要求把新 class 重新挂回 subsystem collection
	ReinstancedSubsystems.Add(NewClass);
}

void FAngelscriptClassGenerator::FullReloadRemoveClass(FModuleData& ModuleData, TSharedPtr<FAngelscriptClassDesc> RemovedClass)
{
	// ★ 删除旧 class 时同样会显式停用 subsystem
	if (RemovedClass->Class != nullptr && (RemovedClass->Class->IsChildOf<UDynamicSubsystem>() || RemovedClass->Class->IsChildOf<UWorldSubsystem>()))
		FSubsystemCollectionBase::DeactivateExternalSubsystem(RemovedClass->Class);
}
```

[5] 但 current 自己的 scenario test 又把 world/game-instance subsystem 脚本生成固定为负例。这说明问题不是“找不到 subsystem 的基类”，而是“正向编译链尚未打通”：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptTest\Subsystem\AngelscriptSubsystemScenarioTests.cpp
// 位置: 66-108, 203-245
// 说明: 当前分支把 script-defined world/game-instance subsystem 编译失败视为预期行为
// ============================================================================
bool FAngelscriptScenarioWorldSubsystemLifecycleTest::RunTest(const FString& Parameters)
{
	...
	const bool bCompiled = CompileModuleWithResult(
		&Engine,
		ECompileType::SoftReloadOnly,
		ModuleName,
		TEXT("ScenarioWorldSubsystemLifecycle.as"),
		TEXT(R"AS(
UCLASS()
class UScenarioWorldLifecycleTracker : UScriptWorldSubsystem
{
	UFUNCTION(BlueprintOverride)
	void BP_Initialize() {}
	UFUNCTION(BlueprintOverride)
	void BP_Deinitialize() {}
}
)AS"),
		CompileResult);
	...
	if (!TestFalse(TEXT("Scenario world subsystem script generation remains unsupported on this branch"), bCompiled))
		return false;

	TestEqual(TEXT("Scenario world subsystem lifecycle should currently fail compilation on this branch"), CompileResult, ECompileResult::Error);
}

bool FAngelscriptScenarioGameInstanceSubsystemLifecycleTest::RunTest(const FString& Parameters)
{
	...
	const bool bCompiled = CompileModuleWithResult(
		&Engine,
		ECompileType::SoftReloadOnly,
		ModuleName,
		TEXT("ScenarioGameInstanceSubsystemLifecycle.as"),
		TEXT(R"AS(
UCLASS()
class UScenarioGameInstanceLifecycleTracker : UScriptGameInstanceSubsystem
{
	UFUNCTION(BlueprintOverride)
	void BP_Initialize() {}
	UFUNCTION(BlueprintOverride)
	void BP_Deinitialize() {}
}
)AS"),
		CompileResult);
	...
	if (!TestFalse(TEXT("Scenario game-instance subsystem script generation remains unsupported on this branch"), bCompiled))
		return false;

	TestEqual(TEXT("Scenario game-instance subsystem lifecycle should currently fail compilation on this branch"), CompileResult, ECompileResult::Error);
}
```

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| subsystem base class surface | `NotBlueprintable`，并通过 `NoBlueprintsOfChildren` 收窄 editor surface | `Blueprintable`，`UScriptWorldSubsystem` 额外暴露 streaming update hook | **实现方式不同** |
| subsystem `Get()` helper 生成 | 有 | 有 | **没有实现差距** |
| subsystem reload reactivation | 有 `DeactivateExternalSubsystem` / `ActivateExternalSubsystem` | 同样保留 | **没有实现差距** |
| script-defined `UScriptWorldSubsystem` / `UScriptGameInstanceSubsystem` 正向编译 | 代码结构上按正向能力设计 | 现有测试显式断言“当前分支应编译失败” | **真实缺口：没有闭环实现** |
| editor surface 与真实能力是否一致 | 有显式 guard 限制子蓝图路径 | guard 已移除，但正向脚本生成仍失败 | **实现质量差异 / contract 一致性问题** |

这条链最值得记录的不是“current 没有 subsystem 相关代码”，而是：**它保留了 UEAS2 的大部分 subsystem substrate，甚至把公开 surface 放得更宽，但 world/game-instance 这两个 script subsystem 的正向能力还没有真正接通。** 因而这里的差距判断必须拆成两层：底层机制多数是“实现方式不同”或“仍然存在”，真正缺的是 **feature closure**。

### [维度 D5/D7] `CreateBlueprint` 调试工作流没有断代，但 owner 已从 `CodeModule` 转移到 `RuntimeModule`，editor hook 也更干净

前文 `D5` 已经证明 `DebugServer V2` 的消息类型没有代际分裂；这轮补的不是 transport，而是 **收到 `CreateBlueprint` 之后，消息如何穿过 runtime/editor 边界，最后落到 `FKismetEditorUtilities::CreateBlueprint()`**。

新的源码结论是：

1. `CreateBlueprint` 这条 UX 链在两边都还存在，消息语义没有断。  
2. current 的差异不在“有没有这个功能”，而在 **delegate owner 从 `FAngelscriptCodeModule` 迁到了 `FAngelscriptRuntimeModule`**。这说明 Loader/CodeModule 边界调整后，调试器仍能打通 editor 工作流。  
3. 更深一层的变化是 literal asset 保存钩子：UEAS2 直接改 `GAssetEditorToolkit_PreSaveObject`，current 改用 `FCoreUObjectDelegates::OnObjectPreSave` 并在 `ShutdownModule()` 里显式卸载，editor 集成更干净。

```
[D5/D7] Debugger-to-Editor Blueprint Flow
Socket Client
└─ DebugServer::CreateBlueprint
   ├─ resolve script class name -> UASClass       // 先把 payload 解析成脚本 class
   ├─ broadcast editor delegate
   │  ├─ UEAS2: CodeModule::GetEditorCreateBlueprint()
   │  └─ AngelPortV2: RuntimeModule::GetEditorCreateBlueprint()
   └─ EditorModule::ShowCreateBlueprintPopup()
      ├─ choose asset path / asset name           // 计算默认保存位置
      └─ FKismetEditorUtilities::CreateBlueprint() // 真正创建 Blueprint 资产
```

[1] UEAS2 的 `CreateBlueprint` 消息到 editor 的落点很直接：DebugServer 解析 `ClassName`，然后通过 `CodeModule` delegate 触发 editor popup：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Debugging\AngelscriptDebugServer.cpp
// 位置: 1077-1095
// 说明: UEAS2 的调试器消息会把创建 Blueprint 的请求转发给 editor delegate
// ============================================================================
else if (MessageType == EDebugMessageType::CreateBlueprint)
{
	FAngelscriptCreateBlueprint CreateBlueprint;
	*Datagram << CreateBlueprint;

	auto ClassDesc = FAngelscriptManager::Get().GetClass(CreateBlueprint.ClassName);
	if (ClassDesc.IsValid() && Cast<UASClass>(ClassDesc->Class) != nullptr)
	{
		// ★ 调试器不直接碰 editor UI，而是经由 module delegate 转发
		FAngelscriptCodeModule::GetEditorCreateBlueprint().Broadcast(Cast<UASClass>(ClassDesc->Class));
	}
	else
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(
			FString::Printf(
				TEXT("Cannot create blueprint/asset: class %s does not exist.\nHas the script file been saved?"),
				*CreateBlueprint.ClassName)
		));
	}
}
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptEditor\Private\AngelscriptEditorModule.cpp
// 位置: 413-418, 437-454
// 说明: editor 模块接住 delegate，再进入 Blueprint 创建弹窗
// ============================================================================
FAngelscriptCodeModule::GetEditorCreateBlueprint().AddLambda(
	[](UASClass* ScriptClass)
	{
		FAngelscriptEditorModule::ShowCreateBlueprintPopup(ScriptClass);
	}
);
...
FString AssetPath;
if (FAngelscriptCodeModule::GetEditorGetCreateBlueprintDefaultAssetPath().IsBound())
	AssetPath = FAngelscriptCodeModule::GetEditorGetCreateBlueprintDefaultAssetPath().Execute(Class);
```

[2] current 保留了同一条消息语义，但 delegate owner 改成 `RuntimeModule`；editor 侧仍旧走 `ShowCreateBlueprintPopup()`，最终调用 `FKismetEditorUtilities::CreateBlueprint()`：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptRuntime\Core\AngelscriptRuntimeModule.cpp
// 位置: 126-134
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptRuntime\Debugging\AngelscriptDebugServer.cpp
// 位置: 1172-1190
// 说明: owner 从 CodeModule 迁到 RuntimeModule，但消息语义未变
// ============================================================================
FAngelscriptEditorCreateBlueprint& FAngelscriptRuntimeModule::GetEditorCreateBlueprint()
{
	static FAngelscriptEditorCreateBlueprint Delegate;
	return Delegate;
}
...
else if (MessageType == EDebugMessageType::CreateBlueprint)
{
	FAngelscriptCreateBlueprint CreateBlueprint;
	*Datagram << CreateBlueprint;

	auto ClassDesc = FAngelscriptEngine::Get().GetClass(CreateBlueprint.ClassName);
	if (ClassDesc.IsValid() && Cast<UASClass>(ClassDesc->Class) != nullptr)
	{
		// ★ 调试器仍然只负责转发，UI 仍在 editor 模块
		FAngelscriptRuntimeModule::GetEditorCreateBlueprint().Broadcast(Cast<UASClass>(ClassDesc->Class));
	}
}
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptEditor\Private\AngelscriptEditorModule.cpp
// 位置: 404-416, 433-435, 513-517
// 说明: current 的 editor 实现继续沿用 popup -> default path -> Kismet create 的工作流
// ============================================================================
FAngelscriptRuntimeModule::GetEditorCreateBlueprint().AddLambda(
	[](UASClass* ScriptClass)
	{
		FAngelscriptEditorModule::ShowCreateBlueprintPopup(ScriptClass);
	}
);
...
if (FAngelscriptRuntimeModule::GetEditorGetCreateBlueprintDefaultAssetPath().IsBound())
	AssetPath = FAngelscriptRuntimeModule::GetEditorGetCreateBlueprintDefaultAssetPath().Execute(Class);
...
Asset = FKismetEditorUtilities::CreateBlueprint(
	Class, Package, AssetName, BPTYPE_Normal,
	BlueprintClass, BlueprintGeneratedClass, FName("AngelscriptCreateBlueprint")
);
```

[3] 更细的一层 editor integration 差异体现在 literal asset 保存钩子。UEAS2 直接写 `GAssetEditorToolkit_PreSaveObject`，而 current 改用标准 `OnObjectPreSave` delegate，并在 shutdown 时显式卸载：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptEditor\Private\AngelscriptEditorModule.cpp
// 位置: 421-434, 699-704
// 说明: UEAS2 通过 editor 全局函数对象截获保存事件，Shutdown 没有对应的卸载动作
// ============================================================================
extern UNREALED_API TFunction<void(UObject*)> GAssetEditorToolkit_PreSaveObject;
GAssetEditorToolkit_PreSaveObject = [](UObject* Object)
{
	if (!FAngelscriptManager::IsInitialized())
		return;
	if (!GIsEditor)
		return;
	if (Object != nullptr && Object->GetOutermost() == FAngelscriptManager::Get().AssetsPackage)
		OnLiteralAssetSaved(Object);
};
...
void FAngelscriptEditorModule::ShutdownModule()
{
	// ★ 这里只卸载 tool menus，没有撤销前面的全局 pre-save hook
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
}
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptEditor\Private\AngelscriptEditorModule.cpp
// 位置: 339-348, 412-415, 676-681
// 说明: current 把同类能力迁到标准 UObject pre-save delegate，并在 shutdown 中显式解绑
// ============================================================================
void OnLiteralAssetPreSave(UObject* Object, FObjectPreSaveContext SaveContext)
{
	if (!FAngelscriptEngine::IsInitialized())
		return;
	if (!GIsEditor)
		return;
	if (Object == nullptr)
		return;
	if (Object->GetOutermost() == FAngelscriptEngine::Get().AssetsPackage)
		OnLiteralAssetSaved(Object);
}
...
GLiteralAssetPreSaveHandle = FCoreUObjectDelegates::OnObjectPreSave.AddStatic(&OnLiteralAssetPreSave);
...
void FAngelscriptEditorModule::ShutdownModule()
{
	if (GLiteralAssetPreSaveHandle.IsValid())
	{
		FCoreUObjectDelegates::OnObjectPreSave.Remove(GLiteralAssetPreSaveHandle);
		GLiteralAssetPreSaveHandle.Reset();
	}
}
```

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| `CreateBlueprint` 调试消息是否仍可打到 editor | 是 | 是 | **没有实现差距** |
| 调试器到 editor 的 delegate owner | `FAngelscriptCodeModule` | `FAngelscriptRuntimeModule` | **实现方式不同** |
| Blueprint 创建弹窗与 `FKismetEditorUtilities::CreateBlueprint()` 工作流 | 有 | 有 | **没有实现差距** |
| literal asset pre-save hook | editor 全局函数对象赋值 | 标准 delegate 注册 + 显式解绑 | **实现质量增强** |

这一维最重要的新补充不是“current 还保留 `CreateBlueprint` 消息”这么表层，而是：**它已经把这条 debugger→editor 工作流从 `CodeModule` 时代平移到了 `RuntimeModule`，并顺手把 editor pre-save hook 从隐式全局改成了可注册/可卸载的标准 delegate。** 这意味着 Loader/模块边界调整并没有把开发体验入口打断，反而把 editor integration 做得更可维护了。

---
## 深化分析 (2026-04-09 00:24:35)

### [维度 D1/D11] `Script root` 已经多源化，但持久化产物的 canonical owner 仍然是 `project Script root`；`BindModules.Cache` 还暴露出一条未收口的 owner 迁移链

前文已经说明 current 支持从启用插件收集 `Script/` 根目录。这一轮补的不是“能不能发现多个 root”，而是“发现结果最后由谁持有编译/部署产物”。源码表明：从 UEAS2 到 current，`GetScriptRootDirectory()` 都直接返回 `AllRootPaths[0]`，而 `MakeAllScriptRoots()` / `DiscoverScriptRoots()` 又都把项目 `Script` 根强制插到索引 `0`。因此 `Binds.Cache`、`PrecompiledScript*.Cache` 这类持久化文件虽然服务于多 root 搜索，但它们的 canonical owner 仍然固定在项目根，而不是插件根。

更关键的新发现是 current 的 `BindModules.Cache`。这条链路的目标很明显，是把自动生成的 bind 分片模块名作为运行时可装载清单持久化下来；但它当前还处在 owner 迁移的半途。Editor 生成阶段把清单写到 `GetScriptRootDirectory()/BindModules.Cache`，Runtime 初始化阶段却去 `plugin->GetBaseDir()/BindModules.Cache` 读取；辅助函数上方甚至直接留着 “write to base directory” 的 `TO-DO`。这不是“没有实现 bind module registry”，而是“registry 已经存在，但交付 owner 还没有完全插件化”。  

```
[D1/D11] Artifact Ownership Topology
Script discovery
├─ Project/Script                         // root[0]，两边都强制放第一位
├─ PluginA/Script                         // 参与脚本源扫描
└─ PluginB/Script                         // 参与脚本源扫描

Persistent artifacts
├─ root[0]/Binds.Cache                    // UEAS2 / current 共用的 canonical cache owner
├─ root[0]/PrecompiledScript*.Cache       // UEAS2 / current 共用的预编译产物 owner
└─ BindModules.Cache
   ├─ save -> root[0]                     // current editor 生成阶段
   └─ load -> plugin base                 // current runtime 初始化阶段
```

[1] UEAS2 的 root 发现虽然覆盖插件目录，但持久化 owner 仍然被 `AllRootPaths[0]` 锁死在项目根：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\AngelscriptManager.cpp
// 函数: FAngelscriptManager::GetScriptRootDirectory / MakeAllScriptRoots
// 位置: 133-137, 269-299
// 说明: UEAS2 的“多 root 搜索”与“单 root 产物 owner”是同时成立的
// ============================================================================
FString FAngelscriptManager::GetScriptRootDirectory()
{
	const auto& AllRootPaths = GAngelscriptManager->AllRootPaths;
	// ★ 持久化路径始终取 root[0]
	return AllRootPaths.IsEmpty() ? TEXT("") : GAngelscriptManager->AllRootPaths[0];
}

TArray<FString> FAngelscriptManager::MakeAllScriptRoots(bool bOnlyProjectRoot)
{
	...
	for (auto& Plugin : PluginManager.GetEnabledPluginsWithContent())
	{
		auto ScriptPath = FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir() / TEXT("Script"));
		if (FileManager.DirectoryExists(*ScriptPath) && ScriptPath != RootPath)
		{
			// ★ 插件 Script 根会被收集进来
			AllRootPaths.Add(ScriptPath);
		}
	}

	AllRootPaths.Sort();
	// ★ 但项目根永远被强插到索引 0
	AllRootPaths.Insert(RootPath, 0);
}
```

[2] current 把 root 发现做成了依赖注入和 engine-scope 逻辑，但 `Binds.Cache` / `PrecompiledScript*.Cache` 的 owner 语义并没有改变：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptRuntime\Core\AngelscriptEngine.cpp
// 函数: FAngelscriptEngine::GetScriptRootDirectory / DiscoverScriptRoots / Initialize
// 位置: 781-793, 1326-1362, 1466-1529
// 说明: current 增强了 root 发现方式，但核心缓存仍然写到 root[0]
// ============================================================================
FString FAngelscriptEngine::GetScriptRootDirectory()
{
	FAngelscriptEngine* CurrentEngine = TryGetCurrentEngine();
	checkf(CurrentEngine != nullptr, TEXT("Attempted to access Angelscript script roots before an engine was available."));

	const auto& AllRootPaths = CurrentEngine->AllRootPaths;
	// ★ 仍然把 root[0] 当作唯一 canonical owner
	return AllRootPaths.IsEmpty() ? TEXT("") : CurrentEngine->AllRootPaths[0];
}

TArray<FString> FAngelscriptEngine::DiscoverScriptRoots(bool bOnlyProjectRoot) const
{
	FString RootPath = Dependencies.ConvertRelativePathToFull(Dependencies.GetProjectDir() / TEXT("Script"));
	...
	for (const FString& PluginScriptRoot : Dependencies.GetEnabledPluginScriptRoots())
	{
		const FString ScriptPath = Dependencies.ConvertRelativePathToFull(PluginScriptRoot);
		if (Dependencies.DirectoryExists(ScriptPath) && ScriptPath != RootPath)
		{
			// ★ 插件根通过 DI 进入发现列表
			DiscoveredRootPaths.Add(ScriptPath);
		}
	}

	DiscoveredRootPaths.Sort();
	DiscoveredRootPaths.Insert(RootPath, 0);
	return DiscoveredRootPaths;
}

FAngelscriptBindDatabase::Get().Load(GetScriptRootDirectory() / TEXT("Binds.Cache"), bGeneratePrecompiledData);
...
FAngelscriptBindDatabase::Get().Save(GetScriptRootDirectory() / TEXT("Binds.Cache"));
...
Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript_Development.Cache");
if (!IFileManager::Get().FileExists(*Filename))
	Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript.Cache");
```

[3] current 的 `BindModules.Cache` 则明确出现了 save/load owner 分裂，而且源码已经把这个问题自己标了出来：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptRuntime\Core\AngelscriptBinds.h
// 函数: FAngelscriptBinds::SaveBindModules / LoadBindModules
// 位置: 583-601
// 说明: helper 自己承认产物 owner 还没完全迁到插件根
// ============================================================================
//TO-DO make sure the binds are written to base directory not inside another module
static void SaveBindModules(FString Path)
{
	auto& BindModuleNames = GetBindModuleNames();
	FFileHelper::SaveStringArrayToFile(BindModuleNames, *Path);
}

static void LoadBindModules(FString Path)
{
	auto& BindModuleNames = GetBindModuleNames();
	FFileHelper::LoadFileToStringArray(BindModuleNames, *Path);
}
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptEditor\Private\AngelscriptEditorModule.cpp
// 函数: FAngelscriptEditorModule::GenerateNativeBinds
// 位置: 999-1077
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptRuntime\Core\AngelscriptEngine.cpp
// 位置: 1472-1488
// 说明: 生成阶段写到 project Script 根，运行阶段却从 plugin base 读取
// ============================================================================
void FAngelscriptEditorModule::GenerateNativeBinds()
{
	...
	// ★ Editor 侧把模块清单写到 GetScriptRootDirectory()
	FAngelscriptBinds::SaveBindModules(FString(FAngelscriptEngine::GetScriptRootDirectory() / "BindModules.Cache"));
}

...

if (plugin)
{
	// ★ Runtime 侧改成从插件根找同名文件
	FAngelscriptBinds::LoadBindModules(plugin->GetBaseDir() / "BindModules.Cache");
}
```

[4] current 至少把 “项目根必须在第一位” 做成了可测试 contract，而不只是隐式约定：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptRuntime\Tests\AngelscriptDependencyInjectionTests.cpp
// 函数: FAngelscriptInjectedScriptRootDiscoveryTest::RunTest
// 位置: 47-88
// 说明: 这里明确把 root[0] = project root 写成自动化断言
// ============================================================================
TUniquePtr<FAngelscriptEngine> Engine = FAngelscriptEngine::CreateForTesting(Config, Dependencies);
TArray<FString> Roots = Engine->DiscoverScriptRoots(false);

TestEqual(TEXT("Injected project root should be first"), Roots[0], FString(TEXT("C:/InjectedProject/Script")));
TestEqual(TEXT("Injected plugin roots should be sorted deterministically"), Roots[1], FString(TEXT("C:/Plugins/Alpha/Script")));
TestEqual(TEXT("Injected plugin roots should keep all entries"), Roots[2], FString(TEXT("C:/Plugins/Beta/Script")));
```

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| 是否支持多 `Script root` 发现 | 支持插件 Script 根扫描 | 支持，而且做成 DI + 自动化断言 | **实现质量增强** |
| `Binds.Cache` / `PrecompiledScript*.Cache` 的 owner | `project Script root` | 仍然是 `project Script root` | **没有实现差距** |
| `BindModules.Cache` registry | 无此链路 | 有，但 save/load owner 分裂 | **实现质量差异 / 交付边界未闭环** |
| “项目根优先”是否可外部验证 | 隐式约定 | 有自动化测试，`StateDump` 也可导出 `ScriptRootPaths` | **实现质量增强** |

这一维真正值得记住的点不是“current 已经多 root 了”，而是：**它把“脚本源发现”做成了多源输入，但把“持久化交付物”仍然锚在项目主根；新增的 bind-module registry 还处在从 project-owned artifact 向 plugin-owned artifact 迁移的半途。** 对“把 `Plugins/Angelscript` 做成真正可复用插件”的目标来说，这是一条明确的交付边界，而不是运行时功能缺口。

### [维度 D11/D6] `!AS_USE_BIND_DB` 的烹饪保护语义在 current 中变松了：`cookworker` 豁免消失，`.Headers` 失败也少了一层显式诊断

这条差异只会出现在 `!AS_USE_BIND_DB` 路径，两边宏定义完全一致，都是 `AS_USE_BIND_DB (!WITH_EDITOR)`。换句话说，影响的是 editor/commandlet 侧的交付链，而不是 shipping runtime 的常规读取链。UEAS2 在这条链上保留了两个保护点：一是 `cookworker` 显式不写 `Binds.Cache`，二是 `.Headers` 文件在 cook 中写失败会打错误日志。current 把 commandlet 状态收敛成 `RuntimeConfig.bRunningCommandlet`，配置结构里已经没有 `bIsCookWorker` 这一层；同时 `FAngelscriptBindDatabase::Save()` 写 `.Headers` 时也直接调用 `SaveArrayToFile()`，没有保留 UEAS2 那个对 cook 失败的日志分支。

据源码推断，这意味着 current 在 `!AS_USE_BIND_DB` 的 editor/cook worker 场景下，更依赖外部流程保证 `Binds.Cache` 的单写者约束；而 `.Headers` 丢失时的第一手报错也比 UEAS2 更弱。需要强调的是，这不是“完全没有保护”或“完全没有可观测性”：`bSkipWriteBindDB` / `bWriteBindDB` / `bRunningCommandlet` 已经进入 `FAngelscriptEngineConfig` 和 `StateDump`，所以 current 的后验诊断能力更强；变化发生在 **guard 语义**，不是发生在 **配置入口缺失**。

```
[D11/D6] Cook-Time Cache Guard
UEAS2 (!AS_USE_BIND_DB)
├─ commandlet?
│  ├─ no -> skip write
│  └─ yes
│     ├─ cookworker -> skip write
│     └─ other      -> write Binds.Cache
└─ save .Headers failure -> log error

current (!AS_USE_BIND_DB)
├─ RuntimeConfig.bRunningCommandlet?
│  ├─ no -> skip write
│  └─ yes -> write Binds.Cache
└─ save .Headers failure -> no equivalent cook log branch
```

[1] 这条差异只在 `!AS_USE_BIND_DB` 分支成立；两边宏定义相同，所以比较口径是对齐的：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Public\AngelscriptManager.h
// 位置: 16-18
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptRuntime\Core\AngelscriptEngine.h
// 位置: 15-17
// 说明: 只有 WITH_EDITOR 路径才会进入下面要比较的“写 bind DB”逻辑
// ============================================================================
#define AS_USE_BIND_DB (!WITH_EDITOR)
```

[2] UEAS2 的 `!AS_USE_BIND_DB` 路径明确绕开了 `cookworker`：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\AngelscriptManager.cpp
// 函数: FAngelscriptManager::Initialize
// 位置: 419-427
// 说明: UEAS2 明确把 cook worker 从写库路径中排除
// ============================================================================
const bool bSkipWriteBindDB = FParse::Param(FCommandLine::Get(), TEXT("as-skip-write-bind-db"));
const bool bForceWriteBindDB = FParse::Param(FCommandLine::Get(), TEXT("as-write-bind-db"));
const bool bIsCookWorker = FParse::Param(FCommandLine::Get(), TEXT("cookworker"));
if ((IsRunningCommandlet() && !bSkipWriteBindDB && !bIsCookWorker) || bForceWriteBindDB)
{
	// ★ 只有非 cookworker 的 commandlet 才会写 Binds.Cache
	FAngelscriptBindDatabase::Get().Save(GetScriptRootDirectory() / TEXT("Binds.Cache"));
}
```

[3] current 的 process config 已经没有 `bIsCookWorker`，写库条件也只剩 `bRunningCommandlet`：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptRuntime\Core\AngelscriptEngine.h
// 位置: 68-83
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptRuntime\Core\AngelscriptEngine.cpp
// 函数: FAngelscriptEngineConfig::FromCurrentProcess / FAngelscriptEngine::Initialize
// 位置: 514-535, 1498-1505
// 说明: current 有 commandlet 开关，但没有与 UEAS2 对等的 cookworker guard
// ============================================================================
struct FAngelscriptEngineConfig
{
	bool bSkipWriteBindDB = false;
	bool bWriteBindDB = false;
	bool bIsEditor = false;
	bool bRunningCommandlet = false;
	...
};

Config.bSkipWriteBindDB = FParse::Param(FCommandLine::Get(), TEXT("as-skip-write-bind-db"));
Config.bWriteBindDB = FParse::Param(FCommandLine::Get(), TEXT("as-write-bind-db"));
Config.bRunningCommandlet = IsRunningCommandlet();

const bool bSkipWriteBindDB = RuntimeConfig.bSkipWriteBindDB;
const bool bForceWriteBindDB = RuntimeConfig.bWriteBindDB;
if ((RuntimeConfig.bRunningCommandlet && !bSkipWriteBindDB) || bForceWriteBindDB)
{
	// ★ 这里已经没有 cookworker 维度
	FAngelscriptBindDatabase::Get().Save(GetScriptRootDirectory() / TEXT("Binds.Cache"));
}
```

[4] `.Headers` 的 cook 失败诊断也出现了不对称：UEAS2 有显式错误日志，current 没有等价分支：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\AngelscriptBindDatabase.cpp
// 位置: 85-93
// 说明: UEAS2 会把 .Headers 的写失败显式打到 cook 日志
// ============================================================================
Writer << Headers;

bool bSaveSuccess = FFileHelper::SaveArrayToFile(HeaderData, *(Path + TEXT(".Headers")));
if (IsRunningCookCommandlet())
{
	if (!bSaveSuccess)
	{
		UE_LOG(Angelscript, Error, TEXT("Unable to write the Script/Binds.Cache.Headers file during cook"));
	}
}
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptRuntime\Core\AngelscriptBindDatabase.cpp
// 位置: 96-99
// 说明: current 仍然生成 .Headers，但没有保留 UEAS2 的 cook 失败日志
// ============================================================================
Writer << Headers;

// ★ 直接写文件，没有等价的 bSaveSuccess / cook error 分支
FFileHelper::SaveArrayToFile(HeaderData, *(Path + TEXT(".Headers")));
```

[5] current 的增强点在于：这些写库配置已经进入外部可观测状态，而不是只能靠命令行肉眼推断：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptRuntime\Dump\AngelscriptStateDump.cpp
// 位置: 389-401
// 说明: current 至少把写库相关配置暴露给 dump，方便事后追诊
// ============================================================================
AddConfigValue(TEXT("bGeneratePrecompiledData"), BoolToString(Config.bGeneratePrecompiledData));
AddConfigValue(TEXT("bIgnorePrecompiledData"), BoolToString(Config.bIgnorePrecompiledData));
AddConfigValue(TEXT("bSkipWriteBindDB"), BoolToString(Config.bSkipWriteBindDB));
AddConfigValue(TEXT("bWriteBindDB"), BoolToString(Config.bWriteBindDB));
AddConfigValue(TEXT("bRunningCommandlet"), BoolToString(Config.bRunningCommandlet));
```

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| `!AS_USE_BIND_DB` 下是否显式跳过 `cookworker` 写库 | 是 | 未见等价 guard | **实现质量差异** |
| `.Headers` cook 写失败是否显式报错 | 是 | 未见等价日志分支 | **实现质量差异** |
| 写库相关配置是否可被外部观测 | 主要靠命令行与日志 | `StateDump` 明确导出相关 config | **实现质量增强** |
| 这条差异是否影响常规 shipping runtime | 否，主要在 editor/commandlet 交付链 | 否，主要在 editor/commandlet 交付链 | **实现方式不同，不是运行时缺失** |

这一维新增的结论不是“current 不能写 bind DB”，而是：**它把写库行为纳入了 engine config 和 dump 可观测面，但在 `!AS_USE_BIND_DB` 的烹饪保护语义上，比 UEAS2 少了 `cookworker` 豁免和 `.Headers` 失败日志这两层显式 guard。** 这更像交付稳定性与诊断质量的变化，而不是核心脚本能力缺口。

---
## 深化分析 (2026-04-09 00:35:38)

### [维度 D3/D8/D11] `BindComponent` 是 UEAS2 的“Blueprint 现有组件回填到脚本字段”contract；current 已收缩到“脚本自产组件”contract

前文已经把 `DefaultComponent / OverrideComponent` 的 owner 钉在插件内部，但还没有把第三条组件路径单独拎出来。继续读上游源码后可以确认：UEAS2 不是只有“脚本声明默认组件”和“脚本覆写父类组件”两条链，它还保留了 `BindComponent`，专门把 **Blueprint 或 CDO 上已经存在的组件实例** 回填到脚本字段。  

这条链之所以必须上升到引擎补丁，不只是因为需要一个新的 property specifier。它还要求三段协作同时成立：preprocessor 把脚本属性改名成 `__BindComponent` 防止与真实 Blueprint 组件属性撞名；class generator 把绑定目标记入 `UASClass::BindComponents`；Actor 生命周期再通过 `AActor::OnPostInitializeComponents` 在“组件已经构建完成、但 BeginPlay 尚未开始”的窗口做一次回填。最后 `BlueprintCompilationManager` 还会检查 Blueprint 上是否真有同名同类型组件。  

current 没有保留这条 contract。当前插件的组件语义已经收缩成两类：一类是在 class constructor 路径里由 `DefaultComponent / OverrideComponent` 直接创建或替换默认子对象；另一类是脚本运行时显式调用 `UActorComponent::Create()` 创建组件。也就是说，**“把 Blueprint 现有组件别名绑定进一个隐藏脚本字段”这条上游能力在 current 中是局部没有实现，而不是简单改名。**

```
[D3/D8/D11] Component Binding Ownership
UEAS2
├─ UPROPERTY(BindComponent)                     // 脚本声明“我要接管现有组件引用”
├─ Preprocessor -> "__BindComponent" suffix    // 避免与 Blueprint 真实组件属性重名
├─ UASClass::BindComponents                     // 记录 name/type/offset
├─ AActor::OnPostInitializeComponents           // 组件创建完成后统一回填
└─ BlueprintCompilationManager                  // 编译期检查 Blueprint 是否提供匹配组件

current
├─ UPROPERTY(DefaultComponent / OverrideComponent) // 脚本自己创建或覆写默认子对象
├─ UASClass::DefaultComponents / OverrideComponents
├─ CreateDefaultComponents()                    // 构造期直接生成组件并设置根/Attach
└─ UActorComponent::Create()                    // 运行时显式创建组件
```

[1] UEAS2 先把 `BindComponent` 定义成独立 specifier，并显式禁止它暴露给 Blueprint/Details 面板：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Preprocessor\AngelscriptPreprocessor.cpp
// 位置: 2136-2142, 2489-2502, 2622-2632
// 说明: `BindComponent` 不是“可编辑引用”，而是隐藏的绑定占位符
// ============================================================================
static FName PP_NAME_DefaultComponent("DefaultComponent");
static FName PP_NAME_OverrideComponent("OverrideComponent");
static FName PP_NAME_BindComponent("BindComponent");
static FName PP_NAME_ShowOnActor("ShowOnActor");
static FName PP_NAME_RootComponent("RootComponent");
static FName PP_NAME_Attach("Attach");
static FName PP_NAME_AttachSocket("AttachSocket");

...

else if (Spec.Name == PP_NAME_BindComponent)
{
	PropDesc->bEditableOnDefaults = false;
	PropDesc->bEditableOnInstance = false;
	PropDesc->bBlueprintWritable = false;
	PropDesc->bBlueprintReadable = false;
	PropDesc->bTransient = true;
	PropDesc->bInstancedReference = true;

	bIsBindComponent = true;
	PropDesc->Meta.Add(Spec.Name, Spec.Value);

	// ★ 给 Unreal 侧真实 FProperty 追加后缀，避免和 Blueprint 组件属性同名冲突
	PropDesc->UnrealPropertyNameOverride = PropDesc->PropertyName + TEXT("__BindComponent");
}

...

if (bIsBindComponent)
{
	if (PropDesc->bBlueprintReadable || PropDesc->bBlueprintWritable || PropDesc->bEditableOnDefaults || PropDesc->bEditableOnInstance)
	{
		// ★ 上游明确要求：BindComponent 绝不能成为 Blueprint/编辑器公开字段
		MacroError(File, Macro, TEXT("BindComponent should not be exposed to blueprint at all. Remove any BlueprintRead/Write and Editable specifiers."));
		bHasError = true;
	}
}
```

[2] 上游不仅解析 specifier，还把这类属性单独落进 `UASClass::BindComponents`，并在 Actor 初始化后统一回填：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Public\ClassGenerator\ASClass.h
// 位置: 47-54
// 说明: `BindComponent` 在类元数据里有独立 owner，而不是混在 Default/Override 里
// ============================================================================
struct FBindComponent
{
	UClass* ComponentClass;
	FName ComponentName;
	FName VariableName;
	SIZE_T VariableOffset;
};
TArray<FBindComponent> BindComponents;
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\AngelscriptManager.cpp
// 位置: 242-243
// 说明: UEAS2 借助引擎补丁新增的 `AActor::OnPostInitializeComponents` 做时机接入
// ============================================================================
// Allow UASClass to respond to when an actor is spawning, after the components have been created, but before BeginPlay has been called.
AActor::OnPostInitializeComponents.AddStatic(&UASClass::OnPostInitializeComponents);
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\ClassGenerator\ASClass.cpp
// 函数: LinkBindComponents / UASClass::OnPostInitializeComponents
// 位置: 1413-1455
// 说明: 组件创建完成后，按 name/type 搜索现有组件并把指针回填进脚本字段
// ============================================================================
static FORCEINLINE_DEBUGGABLE void LinkBindComponents(AActor* Actor, UASClass* Class)
{
	if (UASClass* ParentClass = Cast<UASClass>(Class->GetSuperClass()))
	{
		// ★ 先处理父类，保证继承链上的绑定顺序稳定
		LinkBindComponents(Actor, ParentClass);
	}

	for (const auto& BindComponent : Class->BindComponents)
	{
		UClass* ComponentClass = BindComponent.ComponentClass;
#if AS_CAN_HOTRELOAD
		ComponentClass = ComponentClass->GetMostUpToDateClass();
#endif

		UActorComponent* ComponentInstance = nullptr;
		for (auto* CheckComponent : Actor->GetComponents())
		{
			if (IsValid(CheckComponent)
				&& CheckComponent->GetFName() == BindComponent.ComponentName
				&& CheckComponent->IsA(ComponentClass))
			{
				// ★ Blueprint/现有组件命中后，直接回填到脚本对象内存
				ComponentInstance = CheckComponent;
				break;
			}
		}

		UObject** VariablePtr = (UObject**)((SIZE_T)Actor + BindComponent.VariableOffset);
		*VariablePtr = ComponentInstance;
	}
}

void UASClass::OnPostInitializeComponents(AActor* Actor)
{
	if (Actor->GetClass()->ScriptTypePtr != nullptr)
	{
		if (UASClass* Class = GetFirstASClass(Actor))
		{
			// ★ 真正的 BindComponent 回填发生在这里
			LinkBindComponents(Actor, Class);
		}
	}
}
```

[3] UEAS2 还用引擎侧 `BlueprintCompilationManager` 把这条绑定链升级成编译期校验，而不是运行时静默失败：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Editor\Kismet\Private\BlueprintCompilationManager.cpp
// 位置: 1753-1804
// 说明: Blueprint 编译阶段会检查脚本声明的 `BindComponent` 是否真能在 BP 上找到
// ============================================================================
// AS FIX (FB): Added BindComponent property to Angelscript
if (GIsEditor && !IsRunningCommandlet())
{
	for (FCompilerData& CompilerData : CurrentlyCompilingBPs)
	{
		...
		for (const TFObjectPropertyBase<UActorComponent*>* BindComponentProperty : TFieldRange<TFObjectPropertyBase<UActorComponent*>>(GenClass))
		{
			if (!BindComponentProperty->HasMetaData("BindComponent"))
				continue;

			FString BoundComponentName = BindComponentProperty->GetName();
			BoundComponentName.RemoveFromEnd(TEXT("__BindComponent"));

			const FObjectProperty* ComponentProperty = CastField<FObjectProperty>(GenClass->FindPropertyByName(FName(BoundComponentName)));
			if(ComponentProperty == nullptr)
			{
				// ★ 缺组件时直接给编译警告，而不是等运行时发现空指针
				CompilerData.ActiveResultsLog->Warning(...);
				continue;
			}

			if(!ComponentProperty->PropertyClass->IsChildOf(BindComponentProperty->PropertyClass))
			{
				// ★ 类型不匹配同样在编译阶段报出
				CompilerData.ActiveResultsLog->Warning(...);
				continue;
			}
		}
	}
}
```

[4] current 的 actor/component specifier 面和类元数据里已经没有这条 contract；保留下来的只有“脚本自产组件”：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptRuntime\Preprocessor\AngelscriptPreprocessor.cpp
// 位置: 2359-2365, 2639-2699, 2756-2775
// 说明: current 的组件专用 specifier 集合里没有 `BindComponent`
// ============================================================================
static FName PP_NAME_DefaultComponent("DefaultComponent");
static FName PP_NAME_OverrideComponent("OverrideComponent");
static FName PP_NAME_ShowOnActor("ShowOnActor");
static FName PP_NAME_RootComponent("RootComponent");
static FName PP_NAME_Attach("Attach");
static FName PP_NAME_AttachSocket("AttachSocket");

...

else if (Spec.Name == PP_NAME_OverrideComponent)
{
	PropDesc->bInstancedReference = true;
	PropDesc->Meta.Add(Spec.Name, Spec.Value);
	bIsOverrideComponent = true;
}
else if (Spec.Name == PP_NAME_DefaultComponent)
{
	PropDesc->bBlueprintWritable = false;
	PropDesc->bBlueprintReadable = true;
	PropDesc->bInstancedReference = true;
	bIsDefaultComponent = true;
	PropDesc->Meta.Add(PP_NAME_EditInlineDefaults, TEXT("true"));
	PropDesc->Meta.Add(Spec.Name, TEXT("True"));
}
else if (Spec.Name == PP_NAME_ShowOnActor)
{
	...
}
else if (Spec.Name == PP_NAME_RootComponent)
{
	...
}
else if (Spec.Name == PP_NAME_Attach || Spec.Name == PP_NAME_AttachSocket)
{
	...
}

...

if (!bIsDefaultComponent)
{
	if (bHadAttachment)
		MacroError(File, Macro, TEXT("Attachments can only be specified on DefaultComponents"));
	if (bHadRootComponent)
		MacroError(File, Macro, TEXT("RootComponent can only be specified on DefaultComponents"));
}
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptRuntime\ClassGenerator\ASClass.h
// 位置: 38-57
// 说明: current 的组件元数据只保留 Default/Override 两类
// ============================================================================
struct FDefaultComponent
{
	UClass* ComponentClass;
	FName ComponentName;
	SIZE_T VariableOffset;
	bool bIsRoot;
	bool bEditorOnly;
	FName Attach;
	FName AttachSocket;
};
TArray<FDefaultComponent> DefaultComponents;

struct FOverrideComponent
{
	UClass* ComponentClass;
	FName OverrideComponentName;
	FName VariableName;
	SIZE_T VariableOffset;
};
TArray<FOverrideComponent> OverrideComponents;
```

[5] current 的组件路径改成“构造期创建 + 运行时显式创建”，现有测试也只覆盖这两条：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptRuntime\ClassGenerator\ASClass.cpp
// 函数: ApplyOverrideComponents / CreateDefaultComponents
// 位置: 1176-1275
// 说明: current 把组件逻辑前移到构造路径，直接创建/覆写默认子对象
// ============================================================================
static FORCEINLINE_DEBUGGABLE void ApplyOverrideComponents(const FObjectInitializer& Initializer, AActor* Actor, UASClass* ScriptClass)
{
	for(int32 i = 0, Count = ScriptClass->OverrideComponents.Num(); i < Count; ++i)
	{
		auto& Override = ScriptClass->OverrideComponents[i];
		UClass* ComponentClass = Override.ComponentClass;
#if AS_CAN_HOTRELOAD
		UASClass* asClass = Cast<UASClass>(ComponentClass);
		if (asClass != nullptr)
			ComponentClass = asClass->GetMostUpToDateClass();
#endif
		// ★ 直接覆写默认子对象 class，而不是等 actor 生成后再回填字段
		Initializer.SetDefaultSubobjectClass(Override.OverrideComponentName, ComponentClass);
	}
	...
}

static FORCEINLINE_DEBUGGABLE void CreateDefaultComponents(const FObjectInitializer& Initializer, AActor* Actor, UASClass* ScriptClass)
{
	for(int32 i = 0, Count = ScriptClass->DefaultComponents.Num(); i < Count; ++i)
	{
		auto& DefaultComponent = ScriptClass->DefaultComponents[i];
		UActorComponent* Component = (UActorComponent*)Initializer.CreateDefaultSubobject(...);
		UActorComponent** VariablePtr = (UActorComponent**)((SIZE_T)Actor + DefaultComponent.VariableOffset);
		*VariablePtr = Component;

		// ★ 根组件与 Attach 关系都在这里一次性建立
		if (auto* SceneComponent = Cast<USceneComponent>(Component))
		{
			if (DefaultComponent.bIsRoot)
			{
				SceneComponent->SetupAttachment(nullptr);
				Actor->SetRootComponent(SceneComponent);
			}
		}
	}
}
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptRuntime\Binds\Bind_UActorComponent.cpp
// 位置: 102-122
// 说明: 运行时新增组件也是显式 `Create()`，不是 `BindComponent` 式回填
// ============================================================================
FAngelscriptRuntimeModule::GetComponentCreated().ExecuteIfBound(Component);
Component->OnComponentCreated();

if (USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
{
	if (InActor->GetRootComponent() == nullptr)
	{
		// ★ 没有根节点就直接设根
		InActor->SetRootComponent(SceneComponent);
	}
	else
	{
		// ★ 否则默认挂到现有根节点下
		FAttachmentTransformRules AttachmentRules(EAttachmentRule::KeepRelative, false);
		SceneComponent->AttachToComponent(InActor->GetRootComponent(), AttachmentRules);
	}
}

Component->RegisterComponent();
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptTest\Examples\AngelscriptScriptExampleCoverageTests.cpp
// 位置: 450-484
// 说明: current 的回归测试验证的是“脚本生成组件层级”，不是“绑定 Blueprint 现有组件”
// ============================================================================
UClass* RootComponentClass = FindGeneratedClass(&Engine, TEXT("UCoveragePropertyRootComponent"));
UClass* BillboardClass = FindGeneratedClass(&Engine, TEXT("UCoveragePropertyBillboardComponent"));

USceneComponent* RootComponent = Actor->GetRootComponent();
// ★ 验证 root 是脚本生成的组件 class
if (!TestTrue(TEXT("Coverage property example root component should use the scripted root component class"), RootComponent->IsA(RootComponentClass)))
	return false;

UBillboardComponent* BillboardComponent = nullptr;
for (UActorComponent* Component : Actor->GetComponents())
{
	if (Component != nullptr && Component->IsA(BillboardClass))
	{
		BillboardComponent = Cast<UBillboardComponent>(Component);
		break;
	}
}

// ★ 验证 attach 层级仍由脚本创建链维护
TestTrue(TEXT("Coverage property example attached billboard should preserve the scripted hierarchy"), BillboardComponent->GetAttachParent() == RootComponent);
```

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| `DefaultComponent / OverrideComponent` 生成默认子对象 | 有 | 有 | **没有实现差距** |
| `BindComponent` 把 Blueprint 现有组件回填到脚本隐藏字段 | 有 | 预处理器/类元数据/生命周期钩子均未见等价实现 | **局部没有实现** |
| 组件创建后的 spawn-time rebind pass | `AActor::OnPostInitializeComponents -> LinkBindComponents()` | 未见等价 pass，更多依赖构造期创建 | **实现方式不同，且能力边界收缩** |
| Blueprint 编译期校验组件名/类型是否匹配 | `BlueprintCompilationManager` 警告 | 未见等价编译校验 | **局部没有实现** |
| 运行时显式创建组件 | 有 | 有，`UActorComponent::Create()` | **实现方式不同** |

这一维最容易误判的地方是把 `DefaultComponent` 当成 `BindComponent` 的替代品。源码证明两者不是一回事：前者是“脚本自己产出组件”，后者是“脚本字段接管 Blueprint 已有组件”。current 保留了前者，也补强了测试，但对后者目前是**能力收缩而不是仅仅换 owner**。

### [维度 D7/D11] parity 在 current 里止步于插件自有 UI；UEAS2 的 GameMode / AI Graph 等 engine-owned editor affordance 仍然是引擎补丁

前文已经说明两边都有 `AngelscriptContentBrowserDataSource` 和菜单扩展，所以“内容浏览器里能不能看到脚本资产”不是当前差距。本轮新增的结论是：UEAS2 还有一批 **插件外的 editor 体验补丁**，它们直接进入 `UnrealEd` / `AIGraph` 这类引擎模块，把 Angelscript class 当成一等公民处理；current 只能覆盖插件自己拥有的 UI 面。  

这意味着 current 的 editor parity 是分层的：在 `AngelscriptEditor` 自己拥有的 surface 上已经能做到虚拟资产浏览、菜单扩展、保存对话框等；但像 `GameMode` 选择器、AI 图节点显示名这种原生引擎面板，current 并没有等价接管点。这里不是“菜单没补齐”，而是 **owner 不同导致的交付边界**。

```
[D7/D11] Editor Surface Ownership
UEAS2
├─ Plugin-owned surface
│  └─ AngelscriptContentBrowserDataSource        // 插件自己提供的虚拟资产入口
├─ Engine-owned GameMode picker
│  └─ GameModeInfoCustomizer patch              // 原生类浏览器直接认识 script class
└─ Engine-owned AI graph labels
   └─ AIGraphTypes patch                        // 原生图节点显示名直接美化

current
├─ Plugin-owned surface
│  └─ AngelscriptContentBrowserDataSource        // 仍然可用
└─ Engine-owned native pickers/graphs
   └─ no plugin-local equivalent found          // 当前树内未见等价接管点
```

[1] UEAS2 直接修改引擎 editor 代码，让 `GameMode` 选择器能浏览 script class：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Editor\UnrealEd\Public\GameModeInfoCustomizer.h
// 位置: 314-327
// 说明: 这不是插件菜单，而是引擎原生 class browser 的判断逻辑
// ============================================================================
// AS FIX (PG): Allow browsing to Angelscript classes
return (Class != NULL && (Class->ClassGeneratedBy != NULL || Class->bIsScriptClass));
// END AS FIX
}

void SyncBrowserToClass(const UClass* Class)
{
	if (CanSyncToClass(Class))
	{
		// ★ script class 直接走源码导航，而不是被当作“不可浏览的外来类型”
		if (Class->bIsScriptClass)
		{
			FSourceCodeNavigation::NavigateToClass(Class);
			return;
		}
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Editor\AIGraph\Private\AIGraphTypes.cpp
// 位置: 62-91
// 说明: AI Graph 节点显示名也直接分支识别 script class
// ============================================================================
// AS FIX (PG): Display Angelscript class nodes with pretty names
UClass* MyClass = Class.Get();
if (MyClass)
{
	FString ClassDesc = MyClass->GetName();

	if (MyClass->HasAnyClassFlags(CLASS_CompiledFromBlueprint) && !MyClass->bIsScriptClass)
	{
		return ClassDesc.LeftChop(2);
	}

	const int32 ShortNameIdx = ClassDesc.Find(TEXT("_"), ESearchCase::CaseSensitive);
	if (ShortNameIdx != INDEX_NONE)
	{
		ClassDesc.MidInline(ShortNameIdx + 1, MAX_int32, EAllowShrinking::No);
	}

	// ★ script class 不再被原生 Blueprint 规则误裁剪
	return ClassDesc;
}

const FString ShortName = GetDisplayName();
if (!ShortName.IsEmpty())
{
	return ShortName;
}
```

[2] current 能做到的是插件自有 surface 的资产浏览，而不是改写引擎原生 picker/graph：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptEditor\Private\AngelscriptEditorModule.cpp
// 位置: 111-118
// 说明: current 的 editor 入口仍然是插件自己注册的数据源
// ============================================================================
void OnEngineInitDone()
{
	// ★ 插件初始化时注册自己的 content browser data source
	auto* DataSource = NewObject<UAngelscriptContentBrowserDataSource>(GetTransientPackage(), "AngelscriptData", RF_MarkAsRootSet | RF_Transient);
	DataSource->Initialize();

	UContentBrowserDataSubsystem* ContentBrowserData = IContentBrowserDataModule::Get().GetSubsystem();
	ContentBrowserData->ActivateDataSource("AngelscriptData");
}
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptEditor\Private\AngelscriptContentBrowserDataSource.cpp
// 位置: 16-28
// 说明: current 通过虚拟路径 `/All/Angelscript/` 暴露脚本资产
// ============================================================================
FContentBrowserItemData UAngelscriptContentBrowserDataSource::CreateAssetItem(UObject* Asset)
{
	auto Payload = MakeShared<FAngelscriptContentBrowserPayload>();
	Payload->Path = Asset->GetPathName();
	Payload->Asset = Asset;

	FString DisplayName = Asset->GetName();
	DisplayName.RemoveFromStart(TEXT("Asset_"));

	// ★ 资产显示面仍然存在，但 owner 是插件 data source，不是引擎原生 class picker
	return FContentBrowserItemData(
		this,
		EContentBrowserItemFlags::Type_File | EContentBrowserItemFlags::Category_Asset,
		*(TEXT("/All/Angelscript/") + Asset->GetName()), Asset->GetFName(), FText::FromString(DisplayName), Payload, *Payload->Path);
}
```

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| 插件自有 `ContentBrowserDataSource` | 有 | 有 | **没有实现差距** |
| `GameMode` 原生 class browser 直接识别/导航 script class | 有，引擎补丁 | 未见插件树内等价接管点 | **局部没有实现** |
| AI Graph/Behavior Tree 原生节点显示名美化 | 有，引擎补丁 | 未见插件树内等价接管点 | **局部没有实现** |
| editor parity 的 owner | 插件 UI + engine UI 双覆盖 | 主要是插件 UI | **实现方式不同** |

这一维真正需要记住的是：**current 并不是“没有 editor integration”，而是 editor integration 的 owner 被压回了插件自己控制的 surface。** 因此它能稳定交付内容浏览器、菜单、commandlet，但凡需要改写引擎原生类选择器、图编辑器、属性面板的地方，仍然会落回“没有引擎补丁就没有等价 owner”的边界上。

---

## 深化分析 (2026-04-09 00:48:11)

### [维度 D1/D2] 把 `AngelscriptGAS` / `AngelscriptEnhancedInput` 一并计入后，current 真正新增的 `Bind_*.cpp` 只剩 4 个

前文按“主插件对主插件”口径比较，得到 `UEAS2 root = 112`、`current root = 123`。继续把 UEAS2 的两个可选扩展插件一起纳入后，差异会显著收敛：`AngelscriptGAS` 提供 `4` 个 `Bind_*.cpp`，`AngelscriptEnhancedInput` 提供 `3` 个，UEAS2 全生态合并去重后其实是 `119` 个 `Bind_*.cpp`；current 是 `123` 个。  

这意味着 current 看起来“多出的 11 个 bind 文件”里，有 `7` 个不是全新能力，而是把上游拆在 feature plugin 里的实现折叠回 `AngelscriptRuntime/Binds/`。真正 current-only 的 `Bind_*.cpp` 只剩 `Bind_AngelscriptGASLibrary.cpp`、`Bind_FGenericPlatformMisc.cpp`、`Bind_FunctionLibraryMixins.cpp`、`Bind_SystemTimers.cpp` 四个。用户给的 `125 vs 123` 则混入了另一种统计口径：`125` 是 UEAS2 主插件 `Private/Binds/` 目录总文件数，不是 `Bind_*.cpp` translation unit 数量。

```
[D1/D2] Bind Ownership Scope
UEAS2 ecosystem
├─ AngelscriptCode/Private/Binds              // 112 root bind translation units
├─ AngelscriptGAS/Private/Binds               // +4 optional GAS binds
└─ AngelscriptEnhancedInput/Private/Binds     // +3 optional input binds
   => unique Bind_*.cpp = 119

current
└─ AngelscriptRuntime/Binds                   // 123 bind translation units
   ├─ folded from UEAS2 feature plugins = 7
   └─ current-only additions = 4

Directory-file count is different
├─ UEAS2 root Private/Binds total files = 125
└─ current Runtime/Binds total files = 150
   // helper headers / fallback sources are mixed into the same folder
```

[1] UEAS2 把 GAS / EnhancedInput 绑定明确做成独立插件，所以“根插件 bind 文件少”并不代表这些能力不存在：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\AngelscriptGAS\AngelscriptGAS.uplugin
// 位置: 18-34
// 说明: GAS 能力在 UEAS2 里是可选插件，不属于根 Angelscript 插件
// ============================================================================
"Modules": [
	{
		"Name": "AngelscriptGAS",
		"Type": "Runtime",
		"LoadingPhase": "Default"
	}
],
"Plugins": [
	{
		"Name": "GameplayAbilities",
		"Enabled": true
	},
	{
		"Name": "Angelscript",
		"Enabled": true
	}
]
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\AngelscriptGAS\Source\AngelscriptGAS.Build.cs
// 位置: 18-30
// 说明: `GameplayAbilities` 依赖和 bind owner 都留在独立模块内
// ============================================================================
PublicDependencyModuleNames.AddRange(new string[]
{
	"Core",
	"CoreUObject",
	"Engine",
	"PhysicsCore",

	"AngelscriptCode",

	"GameplayAbilities",
	"GameplayTags",
	"GameplayTasks"
});
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\AngelscriptEnhancedInput\AngelscriptEnhancedInput.uplugin
// 位置: 17-33
// 说明: EnhancedInput 也单独拆成可选插件
// ============================================================================
"Modules": [
	{
		"Name": "AngelscriptEnhancedInput",
		"Type": "Runtime",
		"LoadingPhase": "Default"
	}
],
"Plugins": [
	{
		"Name": "EnhancedInput",
		"Enabled": true
	},
	{
		"Name": "Angelscript",
		"Enabled": true
	}
]
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\AngelscriptEnhancedInput\Source\AngelscriptEnhancedInput.Build.cs
// 位置: 17-28
// 说明: EnhancedInput bind 仍然是独立模块 owner
// ============================================================================
PublicDependencyModuleNames.AddRange(new string[]
{
	"Core",
	"CoreUObject",
	"Engine",
	"InputCore",
	"Slate",

	"AngelscriptCode",

	"EnhancedInput",
});
```

[2] current 则把这些依赖直接收回主插件清单与 `AngelscriptRuntime.Build.cs`：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Angelscript.uplugin
// 位置: 35-47
// 说明: 当前插件在根 `.uplugin` 就直接启用 `EnhancedInput` / `GameplayAbilities`
// ============================================================================
"Plugins": [
	{
		"Name": "StructUtils",
		"Enabled": true
	},
	{
		"Name": "EnhancedInput",
		"Enabled": true
	},
	{
		"Name": "GameplayAbilities",
		"Enabled": true
	}
]
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptRuntime\AngelscriptRuntime.Build.cs
// 位置: 44-65
// 说明: 这些 feature dependency 已经折叠回 Runtime 主模块
// ============================================================================
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
	"TraceLog",
	"AssetRegistry",
	"Projects",
	"PhysicsCore",
	"CoreOnline",
	"EnhancedInput",
	"GameplayAbilities",
	"GameplayTasks",
});
```

[3] `125` 这种目录总数之所以会高于 `112`，是因为文件夹里混着 helper / header / fallback，而不是只有 bind translation unit：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Binds\Helper_FunctionSignature.h
// 位置: 15-35, 38-60
// 说明: 这是签名解析 helper，不是单独的 `Bind_*.cpp`
// ============================================================================
#if WITH_EDITOR
static const FName NAME_Signature_ScriptName("ScriptName");
static const FName NAME_Signature_WorldContext("WorldContext");
static const FName NAME_Signature_DeterminesOutputType("DeterminesOutputType");
static const FName NAME_Signature_ScriptGlobalScope("ScriptGlobalScope");
static const FName NAME_Signature_ToolTip("ToolTip");
static const FName NAME_Signature_Category("Category");
static const FName NAME_Signature_ScriptMethod("ScriptMethod");
static const FName NAME_Signature_ScriptMixin("ScriptMixin");
static const FName NAME_Signature_ScriptTrivial("ScriptTrivial");
...
struct FAngelscriptFunctionSignature
{
	TArray<FAngelscriptTypeUsage> ArgumentTypes;
	FAngelscriptTypeUsage ReturnType;
	...
	bool bStaticInScript = false;
	bool bStaticInUnreal = false;
	bool bGlobalScope = false;
	bool bNotAngelscriptProperty = false;
	bool bTrivial = false;
	bool bBlueprintProtected = false;
	FString ScriptName;
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptRuntime\Binds\Bind_TArray.h
// 位置: 1-38
// 说明: current 也把大量容器支持拆进 header helper，不能算作 `Bind_*.cpp` 数量
// ============================================================================
#pragma once
#include "AngelscriptBinds.h"
#include "AngelscriptType.h"
#include "Bind_TArray_Structs.h"

struct ANGELSCRIPTRUNTIME_API FAngelscriptArrayType : public FAngelscriptType
{
	bool HasReferences(const FAngelscriptTypeUsage& Usage) const override;
	FString GetAngelscriptTypeName() const override;
	virtual bool CanQueryPropertyType() const;
	void EmitReferenceInfo(const FAngelscriptTypeUsage& Usage, FGCReferenceParams& Params) const override;
	...
	bool CanBeTemplateSubType() const override { return false; }
};
```

[4] 折叠回主插件的 7 个文件里，有些是原样收编，有些只做了 owner / API 适配；例如 `Bind_UEnhancedInputComponent.cpp` 只改了一个 UE API 签名，`Bind_FGameplayAttribute.cpp` 只把 manager include 换成 engine include：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\AngelscriptEnhancedInput\Source\Private\Binds\Bind_UEnhancedInputComponent.cpp
// 位置: 39-45
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptRuntime\Binds\Bind_UEnhancedInputComponent.cpp
// 位置: 39-45
// 说明: 这是 feature-plugin 折叠后的典型“轻量 API 对齐”，不是全新设计
// ============================================================================
// UEAS2
UEnhancedInputComponent_.Method("FInputActionValue GetBoundActionValue(const UInputAction Action)", METHODPR_TRIVIAL(FInputActionValue, UEnhancedInputComponent, GetBoundActionValue, (const UInputAction*) const));

// current
UEnhancedInputComponent_.Method("FInputActionValue GetBoundActionValue(const UInputAction Action)", METHODPR_TRIVIAL(FInputActionValue, UEnhancedInputComponent, GetBoundActionValue, (const UInputAction*)));
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\AngelscriptGAS\Source\Private\Binds\Bind_FGameplayAttribute.cpp
// 位置: 1-5
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptRuntime\Binds\Bind_FGameplayAttribute.cpp
// 位置: 1-5
// 说明: 另一类改动只是 owner 从 `Manager` 迁到 `Engine`
// ============================================================================
// UEAS2
#include "AngelscriptBinds.h"
#include "AngelscriptManager.h"

// current
#include "AngelscriptBinds.h"
#include "AngelscriptEngine.h"
```

### 差距判断

| 口径 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| 主插件 `Bind_*.cpp` | `112` | `123` | current 多 `11`，但这是 **root-only** 口径 |
| 全生态 `Bind_*.cpp`（含 GAS / EnhancedInput） | `119` | `123` | current 真正只多 `4`，其余 `7` 是 **模块 owner 折叠** |
| `Binds` 目录总文件数 | `125` | `150` | 这是 **helper/header/fallback 混入** 口径，不能直接拿来比 bind translation unit |
| feature-plugin bind owner | `AngelscriptGAS` / `AngelscriptEnhancedInput` | `AngelscriptRuntime` | **实现方式不同** |
| current-only bind 能力 | 少 `4` 个当前专有文件 | `GASLibrary / PlatformMisc / Mixins / SystemTimers` | **能力新增** |

这一轮最关键的修正是：**“125 vs 123”“缺失 11 / 新增 9”都不是当前源码快照下最稳的口径。** 更稳的说法应当是：current 把 UEAS2 的两个 feature plugin 收编进主插件后，真正新增的 `Bind_*.cpp` 只剩 4 个；其余大头是 owner 迁移，不是 capability gap。

### [维度 D3/D9] 不要把 `BindComponent` 的收缩误推成整条组件继承链收缩；`OverrideComponent` 在 current 里基本原样保留，弱的是回归封口

前文已经确认 `BindComponent` 是 current 的真实能力收缩点，但继续往组件继承链里深挖后可以看到另一层结论：`OverrideComponent` 这条链并没有跟着收缩。当前插件仍然保留了上游那套完整 contract，包括：

1. 编译期检查属性类型必须是 `UActorComponent`。
2. 非抽象脚本类不能直接实例化抽象 component type。
3. 必须能在父脚本类或父 C++ 类中找到被 override 的基组件。
4. override type 必须继承父组件类型。
5. 遇到抽象父组件时，非抽象子类必须显式给出 override。
6. 构造期仍然先 `SetDefaultSubobjectClass()`，随后把实际组件实例回填到 override 变量。

所以 D3 这条线上，current 不是“整个 component inheritance 都退化了”，而是 **`BindComponent` 收缩，但 `OverrideComponent` 主链仍在**。真正更弱的是 D9：当前专用测试模块里能看到 `DefaultComponent.Basic / Multiple`，但没有显式的 `OverrideComponent` 自动化封口。

```
[D3/D9] OverrideComponent Contract
Specifier parse
├─ OverrideComponent metadata
└─ ClassGenerator validation
   ├─ must be UActorComponent
   ├─ parent component must exist
   ├─ type must inherit parent component
   └─ abstract parent component requires explicit override

Constructor path
├─ ApplyOverrideComponents()                    // SetDefaultSubobjectClass
└─ CreateDefaultComponents()
   └─ fill override variable from live component instance

Test closure in current
├─ DefaultComponent.Basic
└─ DefaultComponent.Multiple
   // no explicit OverrideComponent automation found
```

[1] current 的 `OverrideComponent` 编译期约束几乎完整保留了 UEAS2 的语义：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptRuntime\ClassGenerator\AngelscriptClassGenerator.cpp
// 位置: 5326-5444, 5476-5536
// 说明: current 仍然执行 override 目标解析、继承合法性检查，以及抽象父组件强制 override
// ============================================================================
else if (Property->Meta.Contains(NAME_Actor_OverrideComponent))
{
	UASClass::FOverrideComponent Comp;
	Comp.ComponentClass = Property->PropertyType.GetClass();
	Comp.OverrideComponentName = *Property->Meta[NAME_Actor_OverrideComponent];
	...
	if (Comp.ComponentClass == nullptr || !Comp.ComponentClass->IsChildOf(UActorComponent::StaticClass()))
	{
		// ★ 不是组件类型，直接编译报错
		FAngelscriptEngine::Get().ScriptCompileError(...);
		continue;
	}
	if (Comp.ComponentClass->HasAnyClassFlags(CLASS_Abstract) && !ClassDesc->bAbstract)
	{
		// ★ 非抽象脚本类不能直接拿抽象 component type 充当 override
		FAngelscriptEngine::Get().ScriptCompileError(...);
		continue;
	}
	...
	if (ClassOfOverrideComponent == nullptr)
	{
		// ★ 找不到父组件就不允许生成类
		FAngelscriptEngine::Get().ScriptCompileError(...);
		continue;
	}
	else if (!Comp.ComponentClass->IsChildOf(ClassOfOverrideComponent))
	{
		// ★ override type 必须继承父组件类型
		FAngelscriptEngine::Get().ScriptCompileError(...);
		continue;
	}
	ASClass->OverrideComponents.Add(Comp);
}

...

if (!ClassDesc->bAbstract)
{
	TArray<UASClass::FOverrideComponent> OverrideComponentsInHierarchy;
	OverrideComponentsInHierarchy.Append(ASClass->OverrideComponents);
	...
	if (PropertyClass && PropertyClass->HasAnyClassFlags(CLASS_Abstract))
	{
		bool bFoundOverride = false;
		for (UASClass::FOverrideComponent& OverrideComponent : OverrideComponentsInHierarchy)
		{
			if (Property->GetFName() == OverrideComponent.OverrideComponentName)
			{
				bFoundOverride = true;
				break;
			}
		}

		if (!bFoundOverride)
		{
			// ★ 抽象父组件缺 override 时，同样在编译阶段挡住
			FAngelscriptEngine::Get().ScriptCompileError(...);
		}
	}
}
```

[2] 运行期构造路径同样还在，并且顺序仍是“子类 override 先应用，父类后应用，再把 live component 回填到变量”：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptRuntime\ClassGenerator\ASClass.cpp
// 位置: 1176-1199, 1331-1345
// 说明: current 仍按上游方式在构造期接管 override component class，并回填变量
// ============================================================================
static FORCEINLINE_DEBUGGABLE void ApplyOverrideComponents(const FObjectInitializer& Initializer, AActor* Actor, UASClass* ScriptClass)
{
	// ★ 先处理子类 override，顺序与 C++ 默认子对象覆盖一致
	for(int32 i = 0, Count = ScriptClass->OverrideComponents.Num(); i < Count; ++i)
	{
		auto& Override = ScriptClass->OverrideComponents[i];
		UClass* ComponentClass = Override.ComponentClass;
		...
		Initializer.SetDefaultSubobjectClass(Override.OverrideComponentName, ComponentClass);
	}

	if (UASClass* ParentClass = Cast<UASClass>(ScriptClass->GetSuperClass()))
	{
		ApplyOverrideComponents(Initializer, Actor, ParentClass);
	}
}

...

for(int32 i = 0, Count = ScriptClass->OverrideComponents.Num(); i < Count; ++i)
{
	auto& Override = ScriptClass->OverrideComponents[i];
	UActorComponent** VariablePtr = (UActorComponent**)((SIZE_T)Actor + Override.VariableOffset);

	for (auto* CheckComponent : Actor->GetComponents())
	{
		if (CheckComponent->GetFName() == Override.OverrideComponentName)
		{
			// ★ 组件真正实例化后，再把实例回填到脚本字段
			*VariablePtr = CheckComponent;
			break;
		}
	}
}
```

[3] 这条链与 UEAS2 上游实现基本同构；差异主要是 owner 从 `Manager` 换成 `Engine`，以及 hot reload 取最新类的细节：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\ClassGenerator\AngelscriptClassGenerator.cpp
// 位置: 5007-5123, 5176-5237
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\ClassGenerator\ASClass.cpp
// 位置: 1142-1161, 1291-1305
// 说明: 上游对应链条与 current 语义几乎一致
// ============================================================================
// compile-time: 校验 override 目标存在、类型兼容、抽象父组件必须显式 override
else if (Property->Meta.Contains(NAME_Actor_OverrideComponent))
{
	...
	if (ClassOfOverrideComponent == nullptr)
	{
		FAngelscriptManager::Get().ScriptCompileError(...);
		continue;
	}
	else if (!Comp.ComponentClass->IsChildOf(ClassOfOverrideComponent->GetMostUpToDateClass()))
	{
		FAngelscriptManager::Get().ScriptCompileError(...);
		continue;
	}
	ASClass->OverrideComponents.Add(Comp);
}

...

if (!ClassDesc->bAbstract)
{
	TArray<UASClass::FOverrideComponent> OverrideComponentsInHierarchy;
	OverrideComponentsInHierarchy.Append(ASClass->OverrideComponents);
	...
	if (!bFoundOverride)
	{
		FAngelscriptManager::Get().ScriptCompileError(...);
	}
}

// runtime: 先 SetDefaultSubobjectClass，再把实例回填到 override 变量
Initializer.SetDefaultSubobjectClass(Override.OverrideComponentName, ComponentClass);
...
if (CheckComponent->GetFName() == Override.OverrideComponentName)
{
	*VariablePtr = CheckComponent;
	break;
}
```

[4] 但 current 的专用测试模块目前只显式注册了 `DefaultComponent` 场景，没有 `OverrideComponent` 场景：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptTest\Component\AngelscriptComponentScenarioTests.cpp
// 位置: 62-90
// 说明: 当前组件场景测试只公开了 Component / DefaultComponent 用例
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptScenarioComponentBeginPlayTest,
	"Angelscript.TestModule.Component.BeginPlay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
...
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptScenarioDefaultComponentBasicTest,
	"Angelscript.TestModule.DefaultComponent.Basic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptScenarioDefaultComponentMultipleTest,
	"Angelscript.TestModule.DefaultComponent.Multiple",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
```

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| `OverrideComponent` 编译期类型/继承校验 | 有 | 有 | **没有实现差距** |
| 抽象父组件的强制 override 校验 | 有 | 有 | **没有实现差距** |
| 构造期 `SetDefaultSubobjectClass` + 实例回填 | 有 | 有 | **没有实现差距** |
| `BindComponent` 回填 Blueprint 现有组件 | 有 | 已在前文证实缺失 | **局部没有实现** |
| `OverrideComponent` 专项自动化封口 | 本轮未单独展开 UEAS2 测试树 | 当前 Test 模块未见显式场景 | **current 的验证闭环偏弱，主要体现在 D9** |

因此，这一轮对 D3 的修正应当更精确：**current 收缩的是 `BindComponent` 路径，不是整个 component inheritance path。** 对 `OverrideComponent` 而言，当前问题更接近“测试闭环弱于实现复杂度”，而不是“没有实现”。

### [维度 D8/D11] `10 个 C# + 22 个 C++` 只是其中一层；当前快照的 UEAS2 引擎补丁应按 4 个 owner 桶来看，总计 `92` 个文件

按实际源码检索 `AS FIX` / `HAZE FIX` / `AngelscriptPropertyFlags` / `bIsScriptClass` / `FUNC_RuntimeGenerated` / `ScriptCallable`，UEAS2 当前快照可以更稳地拆成四个 owner 桶：

- `EpicGames.UHT`：`10` 个 C# 文件
- `CoreUObject`：`22` 个文件
- `Runtime/Engine`：`21` 个文件
- `Editor/*`：`39` 个文件

合起来是 `92` 个文件。这个拆法比“10 个 C# + 22 个 C++”更有分析价值，因为它直接告诉我们：current 真正插件内替代掉的是哪几桶，哪些桶依旧天生属于 engine-owned surface。

```
[D8/D11] Engine Patch Ownership
UEAS2 patch buckets
├─ UHT (10 files)                          // parser / specifier / codegen
├─ CoreUObject (22 files)                  // class flags / property flags / reflected call metadata
├─ Runtime.Engine (21 files)               // actor lifecycle / subsystem / runtime hooks
└─ Editor (39 files)                       // BP compiler / picker / details / graph UX

current replacement buckets
├─ AngelscriptUHTTool                      // replaces UHT parse/export owner
├─ ClassGenerator + NativeThunk            // replaces part of reflected call / script-class owner
└─ plugin-owned editor/runtime helpers     // only covers plugin-controlled surfaces

Implication
├─ UHT / reflected-call buckets            // mostly "实现方式不同"
└─ engine/editor native hooks              // often "没有等价 owner"
```

[1] UEAS2 的 UHT 补丁不是“只改导出代码”，而是从 parser 就开始给参数和函数打 Angelscript 语义：

```csharp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Programs\Shared\EpicGames.UHT\Parsers\UhtFunctionParser.cs
// 位置: 575-590
// 说明: parser 阶段就把 `WorldContext` 参数打成 Angelscript 专用 property flag
// ============================================================================
// HAZE FIX(LV): Flag for worldcontext arguments so we can send them nicely
if (function.MetaData.ContainsKey("WorldContext"))
{
	string WorldContextArgName = function.MetaData.GetValueOrDefault("WorldContext");
	foreach (UhtType Child in function.Children)
	{
		if (Child is UhtProperty Property)
		{
			if (Property.SourceName == WorldContextArgName)
			{
				// ★ 这一步不是导出时猜测，而是 parser 期直接落 flag
				Property.AngelscriptPropertyFlags |= EAngelscriptPropertyFlags.WorldContext;
			}
		}
	}
}
// END HAZE FIX
```

```csharp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Programs\Shared\EpicGames.UHT\Exporters\CodeGen\UhtHeaderCodeGeneratorCppFile.cs
// 位置: 2538-2574, 2788-2791
// 说明: codegen 阶段继续把 Angelscript 元数据灌进原始 C++ 声明与函数指针表
// ============================================================================
// AS FIX (LV): Angelscript support
private static string GetOriginalCppDeclaration(UhtProperty? Property)
{
	...
	bool bIsRef = Property.AngelscriptPropertyFlags.HasFlag(EAngelscriptPropertyFlags.CppRef);
	bool bIsConst = Property.AngelscriptPropertyFlags.HasFlag(EAngelscriptPropertyFlags.CppConst);
	...
	if (Property is UhtByteProperty ByteProp && ByteProp.Enum != null && ByteProp.AngelscriptPropertyFlags.HasFlag(EAngelscriptPropertyFlags.CppEnumAsByte))
	{
		builder.Append("TEnumAsByte<");
		builder.Append(ByteProp.Enum.CppType);
	}
}
// END AS FIX

// AS FIX(LV): Expose type-erased method pointers to native C++ methods
.Append($"::exec{function.EngineName}")
.Append($", .ASFunctionPointers = {GetASFunctionPointers(classObj, function)} }},\r\n");
// END AS FIX
```

[2] UHT 之外，UEAS2 还把运行时 call boundary 和 actor 生命周期都补进了引擎核心类型里：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Runtime\CoreUObject\Public\UObject\Class.h
// 位置: 3909-3915
// 说明: `UClass` 直接新增了 Angelscript 专用字段
// ============================================================================
// AS FIX(LV): Runtime generated classes (angelscript)
TMap<FName, ASAutoCaller::FReflectedFunctionPointers> ASReflectedFunctionPointers;
void* ScriptTypePtr = nullptr;
bool bIsScriptClass = false;

const TMap<FName, TObjectPtr<UFunction>>& GetFunctionMap() { return FuncMap; }
// END AS FIX
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Runtime\Engine\Classes\GameFramework\Actor.h
// 位置: 4390-4395
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Runtime\Engine\Private\Actor.cpp
// 位置: 107-109, 6752-6753
// 说明: actor 生命周期也被引擎侧直接加了一个脚本专用 hook
// ============================================================================
// AS FIX (FB): Added BindComponent property to Angelscript
static ENGINE_API TMulticastDelegate<void(AActor*)> OnPostInitializeComponents;
// END AS FIX

// AS FIX (FB): Added BindComponent property to Angelscript
TMulticastDelegate<void(AActor*)> AActor::OnPostInitializeComponents;
// END AS FIX

...
// AS FIX (FB): Added BindComponent property to Angelscript
AActor::OnPostInitializeComponents.Broadcast(this);
// END AS FIX
```

[3] current 的替代链主要落在插件自有模块里：`UHTTool` 负责自动导出，`ClassGenerator` 负责 `bIsScriptClass + NativeThunk`，但 owner 已经不再是 engine mainline：

```csharp
// ============================================================================
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptUHTTool\AngelscriptFunctionTableCodeGenerator.cs
// 位置: 51-78, 282-339
// 说明: current 用插件内 exporter 生成 `AS_FunctionTable_*.cpp`，并把 `Build.cs` 作为显式配置源
// ============================================================================
public static int Generate(IUhtExportFactory factory)
{
	AngelscriptSupportedModules supportedModules = LoadSupportedModules(factory);
	int generatedFileCount = 0;
	...
	foreach (UhtModule module in factory.Session.Modules)
	{
		if (!supportedModules.All.Contains(module.ShortName))
		{
			continue;
		}
		...
	}
	DeleteStaleOutputs(factory, generatedPaths);
	WriteGenerationSummary(factory, moduleSummaries, csvEntries, generatedFileCount);
	WriteCoverageDiagnostics(moduleSummaries);
	return generatedFileCount;
}

private static StringBuilder BuildShard(...)
{
	...
	builder.Append("AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_AS_FunctionTable_")
		.Append(moduleShortName)
		...
	builder.Append("\tUE_LOG(Angelscript, Log, TEXT(\"[UHT] Registered %d generated BlueprintCallable entries for module %s shard %d/%d\"), ");
	...
}

private static AngelscriptSupportedModules LoadSupportedModules(IUhtExportFactory factory)
{
	string buildCsPath = ResolveRuntimeBuildCsPath(factory);
	factory.AddExternalDependency(buildCsPath);
	// ★ 生成范围改由插件自己的 Runtime Build.cs 驱动
	...
}
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptRuntime\ClassGenerator\AngelscriptClassGenerator.cpp
// 位置: 3289-3292, 3411-3429
// 说明: current 仍保留 `bIsScriptClass` 语义，但它已经是插件内 `UASClass` owner
// ============================================================================
// Set up the class' base data
NewClass->ClassFlags = CLASS_CompiledFromBlueprint;
NewClass->bIsScriptClass = true;
NewClass->ClassFlags |= (SuperClass->ClassFlags & CLASS_ScriptInherit);

...

auto* NewFunction = UASFunction::AllocateFunctionFor(NewClass, FunctionName, FunctionDesc);
NewFunction->SetSuperStruct(ParentFunction);
...
NewFunction->FunctionFlags |= FUNC_Native;
// ★ 用插件内 `NativeThunk` 接管调用边界，而不是依赖 engine-mainline 的 `FUNC_RuntimeGenerated`
NewFunction->SetNativeFunc(&UASFunctionNativeThunk);
```

### 差距判断

| owner 桶 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| UHT parser / exporter | `10` 个 C# 引擎补丁 | `AngelscriptUHTTool` + function table 生成 | **实现方式不同，已大体替代** |
| reflected call / script-class metadata | `CoreUObject` 内 `ASReflectedFunctionPointers` / `bIsScriptClass` / property flags | `UASClass` + `UASFunctionNativeThunk` + plugin-side metadata | **实现方式不同，部分替代** |
| actor lifecycle hook | `Runtime/Engine` 里直接补 `AActor::OnPostInitializeComponents` 等 | 前文已证实这类 hook 没有完全等价的插件内 owner | **局部没有实现** |
| editor-native affordance | `39` 个 Editor 文件直接接管 Blueprint/compiler/picker/graph | 前文所示主要收敛到插件自有 surface | **实现方式不同，且部分无等价 owner** |

因此，这一轮 D8/D11 的新结论不是“补丁很多”这么泛，而是：**UEAS2 的引擎改造至少分成 4 类 owner。current 真正成功插件内化的是 UHT 与 reflected-call 这两桶；一旦功能需要 Runtime/Engine 或 Editor 原生 owner，前文看到的那些 parity gap 就不再是单点遗漏，而是 owner 级边界。**

---

## 深化分析 (2026-04-09 01:02:15)

本轮先重新核验了 `.worktrees/ue-bind-gap-roadmap/AgentConfig.ini` 中的 `References.HazelightAngelscriptEngineRoot=J:\UnrealEngine\UEAS2`，以下新增证据都基于这个路径。重点不再重复前文已经钉死的 bind gap 和引擎补丁 owner，而是补三条更容易被误判的链路：`D4` 的 reload 触发拓扑、`D5` 的 Blueprint/script 混合调试栈 contract，以及 `D5/D6` 的 IDE 元数据协议面。

### [维度 D4] `DirectoryWatcher` 只是 editor 增量入口；current 仍保留 UEAS2 的 thread + tick 调度骨架

```
[D4] Reload Trigger Topology
UEAS2
├─ init
│  └─ bUseHotReloadCheckerThread = bScriptDevelopmentMode && !GIsEditor
├─ non-editor
│  ├─ StartHotReloadThread()
│  └─ background CheckForFileChanges()
├─ tick gate (0.1s)
│  └─ CheckForHotReload(SoftReloadOnly / FullReload)
└─ editor
   └─ AngelscriptEditor DirectoryWatcher queues file changes

AngelPortV2
├─ init
│  └─ bUseHotReloadCheckerThread = bScriptDevelopmentMode && !RuntimeConfig.bIsEditor
├─ non-editor
│  ├─ StartHotReloadThread()
│  └─ background CheckForFileChanges()
├─ tick gate (0.1s)
│  └─ CheckForHotReload(SoftReloadOnly / FullReload)
└─ editor
   ├─ DirectoryWatcher -> QueueScriptFileChanges()
   └─ dedicated queue tests for rename/folder-add
```

前文已经讲过 current 在 editor 侧新增了 `DirectoryWatcher` 和更细的 queue 测试，但这一轮更关键的补点是：**它并没有替换 UEAS2 的 hot reload scheduler，只是把 editor 的“变更发现入口”单独插件化了。** 真正负责 `SoftReloadOnly / FullReload` 分流、`0.1s` tick 节流和后台扫描线程的骨架，在 current 里仍然保留，而且连注释都基本同构。

[1] 两边初始化阶段都保留了“非 editor 才开 checker thread”的判断，`StartHotReloadThread()` 也都先做一次 `CheckForFileChanges()` 预热，再启动低优先级后台线程：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\AngelscriptManager.cpp
// 函数: FAngelscriptManager::Initialize / StartHotReloadThread
// 位置: 537-542, 575-617
// 说明: UEAS2 的 core scheduler 是 checker thread + prefill timestamps
// ============================================================================
// Use the checker thread if we want to detect hot reloads,
// but we don't have access to the editor. In editor, the AngelscriptEditor
// module will use the directory watcher system to detect reloads instead.
bUseHotReloadCheckerThread = bScriptDevelopmentMode && !GIsEditor;
if (bUseHotReloadCheckerThread)
	StartHotReloadThread();

void FAngelscriptManager::StartHotReloadThread()
{
	if (bUsedPrecompiledDataForPreprocessor)
		return;
	if (!bUseHotReloadCheckerThread)
		return;
	if (bHotReloadThreadStarted)
		return;
	bHotReloadThreadStarted = true;

	// ★ 先扫一轮，预填充时间戳，随后丢弃本轮事件
	CheckForFileChanges();
	FileChangesDetectedForReload.Empty();

	struct FAngelscriptHotReloadThread : public FRunnable
	{
		uint32 Run() override
		{
			auto& Manager = FAngelscriptManager::Get();
			while (bRunning)
			{
				if (Manager.bWaitingForHotReloadResults)
				{
					Manager.CheckForFileChanges();
					Manager.bWaitingForHotReloadResults = false;
				}
				FPlatformProcess::Sleep(0.001f);
			}
			return 0;
		}
	};

	FRunnableThread::Create(new FAngelscriptHotReloadThread(), TEXT("AngelscriptHotReload"), 0, EThreadPriority::TPri_Lowest);
}
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptRuntime\Core\AngelscriptEngine.cpp
// 函数: FAngelscriptEngine::Initialize / StartHotReloadThread
// 位置: 1615-1620, 1658-1700
// 说明: current 仍保留同一套 scheduler，只是 owner 从 Manager 迁到 Engine
// ============================================================================
// Use the checker thread if we want to detect hot reloads,
// but we don't have access to the editor. In editor, the AngelscriptEditor
// module will use the directory watcher system to detect reloads instead.
bUseHotReloadCheckerThread = bScriptDevelopmentMode && !RuntimeConfig.bIsEditor;
if (bUseHotReloadCheckerThread)
	StartHotReloadThread();

void FAngelscriptEngine::StartHotReloadThread()
{
	if (bUsedPrecompiledDataForPreprocessor)
		return;
	if (!bUseHotReloadCheckerThread)
		return;
	if (bHotReloadThreadStarted)
		return;
	bHotReloadThreadStarted = true;

	// ★ 与上游同样先预热扫描，再启动后台线程
	CheckForFileChanges();
	FileChangesDetectedForReload.Empty();

	struct FAngelscriptHotReloadThread : public FRunnable
	{
		uint32 Run() override
		{
			auto& Manager = FAngelscriptEngine::Get();
			while (bRunning)
			{
				if (Manager.bWaitingForHotReloadResults)
				{
					Manager.CheckForFileChanges();
					Manager.bWaitingForHotReloadResults = false;
				}
				FPlatformProcess::Sleep(0.001f);
			}
			return 0;
		}
	};

	FRunnableThread::Create(new FAngelscriptHotReloadThread(), TEXT("AngelscriptHotReload"), 0, EThreadPriority::TPri_Lowest);
}
```

[2] 两边 `Tick()` 也都保留了同一条调度分流：先按 `0.1s` 节流，再根据 `HasGameWorld()` 决定 `SoftReloadOnly` 还是 `FullReload`。这说明 current 没有换掉上游的“何时真正执行 reload”判定面：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\AngelscriptManager.cpp
// 函数: FAngelscriptManager::Tick
// 位置: 1544-1578
// 说明: UEAS2 的真正 reload gate 在 Tick，而不是在 Loader
// ============================================================================
if (bUseHotReloadCheckerThread)
{
	double CurrentTime = FPlatformTime::Seconds();
	if (NextHotReloadCheck > CurrentTime && !bWaitingForHotReloadResults)
		return;
	NextHotReloadCheck = CurrentTime + 0.1;
}

if (!GIsEditor || HasGameWorld())
{
	CheckForHotReload(ECompileType::SoftReloadOnly);
}
else
{
	CheckForHotReload(ECompileType::FullReload);
}
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptRuntime\Core\AngelscriptEngine.cpp
// 函数: FAngelscriptEngine::Tick
// 位置: 2794-2828
// 说明: current 的 gate 逻辑同构，只多了 primary-engine queue log
// ============================================================================
if (bUseHotReloadCheckerThread)
{
	double CurrentTime = FPlatformTime::Seconds();
	if (NextHotReloadCheck > CurrentTime && !bWaitingForHotReloadResults)
		return;
	NextHotReloadCheck = CurrentTime + 0.1;
}

if (!GIsEditor || HasGameWorld())
{
	CheckForHotReload(ECompileType::SoftReloadOnly);
}
else
{
	CheckForHotReload(ECompileType::FullReload);
}
```

[3] current 真正新增的是 editor 入口的插件内化和队列级回归封口，不是 scheduler 重写：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptEditor\Private\AngelscriptEditorModule.cpp
// 函数: OnScriptFileChanges / StartupModule
// 位置: 78-90, 367-380
// 说明: current 把 editor 变更发现入口显式插件化
// ============================================================================
void OnScriptFileChanges(const TArray<FFileChangeData>& Changes)
{
	if (!FAngelscriptEngine::IsInitialized())
		return;

	FAngelscriptEngine& AngelscriptManager = FAngelscriptEngine::Get();
	// ★ watcher 只负责把变化喂给 Engine 队列
	AngelscriptEditor::Private::QueueScriptFileChanges(...);
}

DirectoryWatcher->RegisterDirectoryChangedCallback_Handle(
	*RootPath,
	IDirectoryWatcher::FDirectoryChanged::CreateStatic(&OnScriptFileChanges),
	WatchHandle,
	IDirectoryWatcher::IncludeDirectoryChanges);
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptEditor\Private\Tests\AngelscriptDirectoryWatcherTests.cpp
// 函数: FAngelscriptDirectoryWatcherRenameWindowTest::RunTest
// 位置: 214-222
// 说明: rename 窗口的 add/remove 双事件被单独回归
// ============================================================================
AngelscriptEditor::Private::QueueScriptFileChanges(Changes, Engine->AllRootPaths, *Engine, FileManager, ...);

TestEqual(TEXT("DirectoryWatcher.Queue.RenameWindowTracksRemoveAndAdd should queue one removed script"), Engine->FileDeletionsDetectedForReload.Num(), 1);
TestEqual(TEXT("DirectoryWatcher.Queue.RenameWindowTracksRemoveAndAdd should queue one added script"), Engine->FileChangesDetectedForReload.Num(), 1);
TestTrue(TEXT("DirectoryWatcher.Queue.RenameWindowTracksRemoveAndAdd should retain the old filename in the deletion queue"), ...);
TestTrue(TEXT("DirectoryWatcher.Queue.RenameWindowTracksRemoveAndAdd should retain the new filename in the addition queue"), ...);
```

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| non-editor checker thread | 有 | 有 | **没有实现差距** |
| `Tick()` 中的 `SoftReloadOnly / FullReload` gate | 有 | 有 | **没有实现差距** |
| editor 目录变化接入 | `AngelscriptEditor` 直接接 watcher | 同样接 watcher，但抽成 helper 并可单测 | **实现方式不同，current 更工程化** |
| rename/folder-add queue 回归 | 未见专门测试入口 | 有独立 automation test | **实现质量差异** |

这一节最该修正的误读是：**current 不是“把热重载从轮询改成了 watcher”，而是“保留 UEAS2 的 thread + tick 主链，再把 editor 的文件事件入口单独插件化”。** 所以 `Loader` 的缺位并不等于 reload scheduler 缺位。

### [维度 D5] Blueprint / script 混合调试栈 contract 在 current 中基本原样保留

```
[D5] Mixed Debugger Frame Resolution
RequestCallStack
├─ iterate script frames
├─ splice Blueprint frames via GetBlueprintCallstackFrame()
├─ expose debugger index
│  ├─ script frame -> raw callstack index
│  └─ blueprint frame -> FLAG_BlueprintFrame | BPStackIndex
└─ GetDebuggerValue()
   ├─ script frame -> %local% / %this% / %module%
   └─ blueprint frame -> only this/member
```

前文已经把 transport、adapter version 和 data breakpoint 讲清了，但 IDE 体感里还有一个更隐蔽的 contract：**CallStack 返回的 frame 不是纯 script 栈，而是 script + Blueprint 混合栈；随后 `RequestVariables` / evaluate 会先用 `ResolveDebuggerFrame()` 把 debugger frame index 重新映射回 script frame 或 Blueprint frame。** 这条 contract 在 current 里并没有断，限制也保持一致: Blueprint frame 只支持 `this`/member 视图，不支持 `%local%` 或 `%module%`。

[1] 两边的 `SendCallStack()` 都会在遍历脚本调用栈时插入 Blueprint frame；差异只在过滤 generated Blueprint frame 的谓词写法：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Debugging\AngelscriptDebugServer.cpp
// 函数: FAngelscriptDebugServer::SendCallStack
// 位置: 1257-1349
// 说明: UEAS2 直接用 FUNC_RuntimeGenerated 过滤脚本生成的 Blueprint frame
// ============================================================================
void FAngelscriptDebugServer::SendCallStack(FSocket* Client)
{
	...
	auto* BPStack = FBlueprintContextTracker::TryGet();
	...
	int BPFrame = Context->GetBlueprintCallstackFrame(i);
	for (; BPStackIndex < BPFrame; ++BPStackIndex)
	{
		UFunction* Function = StackView[BPStackIndex]->Node;
		if (Function == nullptr || Function->HasAnyFunctionFlags(FUNC_RuntimeGenerated))
			continue;
		...
		if (AngelscriptDebugServer::DebugAdapterVersion >= 1)
			Frame.ModuleName = TEXT("");
		Stack.Frames.Insert(Frame, 0);
	}
	...
}
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptRuntime\Debugging\AngelscriptDebugServer.cpp
// 函数: FAngelscriptDebugServer::SendCallStack
// 位置: 1382-1474
// 说明: current 保留同样的 mixed-stack 形态，只把过滤逻辑收口到 helper
// ============================================================================
void FAngelscriptDebugServer::SendCallStack(FSocket* Client)
{
	...
	auto* BPStack = FBlueprintContextTracker::TryGet();
	...
	int BPFrame = Context->GetBlueprintCallstackFrame(i);
	for (; BPStackIndex < BPFrame; ++BPStackIndex)
	{
		UFunction* Function = StackView[BPStackIndex]->Node;
		if (Function == nullptr || IsAngelscriptGenerated(Function))
			continue;
		...
		if (AngelscriptDebugServer::DebugAdapterVersion >= 1)
			Frame.ModuleName = TEXT("");
		Stack.Frames.Insert(Frame, 0);
	}
	...
}
```

[2] 两边的 frame 解析和变量查看规则同样保持一致：Blueprint frame 会被编码成 `FLAG_BlueprintFrame | BPStackIndex`，后续变量查看只允许 `this`：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Debugging\AngelscriptDebugServer.cpp
// 函数: ResolveDebuggerFrame / GetDebuggerValue
// 位置: 2210-2277, 2352-2368
// 说明: Blueprint frame 先编码，再在 GetDebuggerValue 中走专门分支
// ============================================================================
static const int FLAG_BlueprintFrame = 0x10000000;
int FAngelscriptDebugServer::ResolveDebuggerFrame(int DebuggerFrame)
{
	...
	ResolvedFrames.Insert(FLAG_BlueprintFrame | BPStackIndex, 0);
	...
}

if ((Frame & FLAG_BlueprintFrame) != 0)
{
	// ★ If this is a blueprint stack frame, we only support evaluating the 'this' pointer
	int BPFrame = Frame & ~FLAG_BlueprintFrame;
	auto* BPStack = FBlueprintContextTracker::TryGet();
	...
}
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptRuntime\Debugging\AngelscriptDebugServer.cpp
// 函数: ResolveDebuggerFrame / GetDebuggerValue
// 位置: 2281-2348, 2423-2439
// 说明: current 保留完全同构的编码与访问限制
// ============================================================================
static const int FLAG_BlueprintFrame = 0x10000000;
int FAngelscriptDebugServer::ResolveDebuggerFrame(int DebuggerFrame)
{
	...
	ResolvedFrames.Insert(FLAG_BlueprintFrame | BPStackIndex, 0);
	...
}

if ((Frame & FLAG_BlueprintFrame) != 0)
{
	// ★ If this is a blueprint stack frame, we only support evaluating the 'this' pointer
	int BPFrame = Frame & ~FLAG_BlueprintFrame;
	auto* BPStack = FBlueprintContextTracker::TryGet();
	...
}
```

[3] current 至少对 script frame 的 callstack / stepping 做了正式回归，因此这条 mixed-stack contract 不是“只剩源码，没有验证”的状态：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptTest\Debugger\AngelscriptDebuggerBreakpointTests.cpp
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptTest\Debugger\AngelscriptDebuggerSteppingTests.cpp
// 位置: 418-430, 447-447, 541-541, 650-650
// 说明: current 至少验证了 top frame 源文件/行号与 step 语义
// ============================================================================
TestTrue(TEXT("Debugger.Breakpoint.HitLine should capture a callstack via the monitor"), MonitorResult.CapturedCallstack.IsSet());
TestTrue(TEXT("Debugger.Breakpoint.HitLine should report the fixture filename in the top stack frame"), Callstack.Frames[0].Source.EndsWith(Fixture.Filename));
TestEqual(TEXT("Debugger.Breakpoint.HitLine should stop at the requested helper line"), Callstack.Frames[0].LineNumber, Fixture.GetLine(TEXT("BreakpointHelperLine")));

TestEqual(TEXT("Stepping.StepIn should land inside Inner()"), ...);
TestEqual(TEXT("Stepping.StepOver should land at the line after the call"), ...);
TestTrue(TEXT("Stepping.StepOut should reduce stack depth after returning"), ...);
```

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| CallStack 是否混入 Blueprint frame | 是 | 是 | **没有实现差距** |
| debugger frame 编码方式 | `FLAG_BlueprintFrame` | 同样 `FLAG_BlueprintFrame` | **没有实现差距** |
| Blueprint frame 变量查看范围 | 仅 `this` / member | 仅 `this` / member | **没有实现差距** |
| 过滤脚本生成 Blueprint frame 的写法 | `FUNC_RuntimeGenerated` | `IsAngelscriptGenerated(Function)` | **实现方式不同，current 更插件内聚** |

所以 D5 这一点不能写成“current debugger 只懂 script 栈”。更准确的说法是：**它仍然懂混合栈，而且连 Blueprint frame 只允许 `this` 的限制也沿用了上游 contract。**

### [维度 D5/D6] `RequestDebugDatabase` / `AssetDatabase` 主流程还在，但 `DebugDatabaseSettings` schema 已从 version 7 收窄到 version 5

```
[D5/D6] IDE Metadata Stream
RequestDebugDatabase
├─ add client to ClientsThatWantDebugDatabase
├─ SendDebugDatabase()
│  ├─ DebugDatabaseSettings
│  └─ DebugDatabase payload
└─ SendAssetDatabase()
   ├─ AssetDatabaseInit
   ├─ AssetDatabase chunks
   └─ AssetDatabaseFinished

Schema delta
├─ UEAS2: version 7
│  ├─ bExposeGlobalFunctions
│  ├─ bDeprecateActorGenerics
│  └─ bDisallowActorGenerics
└─ current: version 5
   └─ core flags only
```

这条线前文还没展开，但它其实直接关系到 IDE 补全/数据库生成的“协商面”是否一致。源码显示 current 并没有移除 `RequestDebugDatabase` 或 `AssetDatabase` 工作流，初始全量推送与 `AssetRegistry` 增量广播都还在；**真正发生变化的是 `DebugDatabaseSettings` 的 schema 收窄了**，从 UEAS2 的 version `7` 降到 current 的 version `5`，并去掉了 `bExposeGlobalFunctions`、`bDeprecateActorGenerics`、`bDisallowActorGenerics` 三个开关字段。

[1] 两边都还保留了 `RequestDebugDatabase` / `DebugDatabaseSettings` / `AssetDatabase*` 这组消息类型，但 settings 结构体版本和字段数已经不一样：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Debugging\AngelscriptDebugServer.h
// 位置: 459-481
// 说明: UEAS2 的 DebugDatabaseSettings 是 version 7，带 8 个 bool 字段
// ============================================================================
struct FAngelscriptDebugDatabaseSettings : FDebugMessage
{
	bool bAutomaticImports = false;
	bool bFloatIsFloat64 = false;
	bool bUseAngelscriptHaze = false;
	bool bDeprecateStaticClass = false;
	bool bDisallowStaticClass = false;
	bool bExposeGlobalFunctions = false;
	bool bDeprecateActorGenerics = false;
	bool bDisallowActorGenerics = false;

	FORCEINLINE friend FArchive& operator<<(FArchive& Ar, FAngelscriptDebugDatabaseSettings& Msg)
	{
		int32 Version = 7;
		...
		Ar << Msg.bExposeGlobalFunctions;
		Ar << Msg.bDeprecateActorGenerics;
		Ar << Msg.bDisallowActorGenerics;
		return Ar;
	}
};
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptRuntime\Debugging\AngelscriptDebugServer.h
// 位置: 536-550
// 说明: current 的 DebugDatabaseSettings 降到 version 5，只保留核心五项
// ============================================================================
struct FAngelscriptDebugDatabaseSettings : FDebugMessage
{
	bool bAutomaticImports = false;
	bool bFloatIsFloat64 = false;
	bool bUseAngelscriptHaze = false;
	bool bDeprecateStaticClass = false;
	bool bDisallowStaticClass = false;

	FORCEINLINE friend FArchive& operator<<(FArchive& Ar, FAngelscriptDebugDatabaseSettings& Msg)
	{
		int32 Version = 5;
		...
	}
};
```

[2] 请求与推送主流程没有断，`AssetDatabase` 仍旧是 `Init -> chunk -> Finished`，并且都继续绑定 `AssetRegistry` 的 add/remove/rename 增量事件：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Debugging\AngelscriptDebugServer.cpp
// 函数: RequestDebugDatabase handler / SendDebugDatabase / BindAssetRegistry / SendAssetDatabase
// 位置: 712-715, 1373-1383, 2007-2132
// 说明: UEAS2 的 IDE 元数据流
// ============================================================================
if (MessageType == EDebugMessageType::RequestDebugDatabase)
{
	ClientsThatWantDebugDatabase.Add(Client);
	SendDebugDatabase(Client);
}

DebugSettings.bAutomaticImports = true;
DebugSettings.bExposeGlobalFunctions = GetDefault<UAngelscriptSettings>()->bExposeGlobalFunctionsToOtherScriptFiles;
DebugSettings.bDeprecateActorGenerics = ...;
DebugSettings.bDisallowActorGenerics = ...;
SendMessageToClient(Client, EDebugMessageType::DebugDatabaseSettings, DebugSettings);

AssetRegistry.OnFilesLoaded().AddLambda([this, BindAssetRegistryChanges]()
{
	for (auto* ConnectedClient : ClientsThatWantDebugDatabase)
		SendAssetDatabase(ConnectedClient);
	BindAssetRegistryChanges();
});

SendMessageToClient(Client, EDebugMessageType::AssetDatabaseInit, InitMessage);
AssetRegistry.EnumerateAllAssets(..., UE::AssetRegistry::EEnumerateAssetsFlags::OnlyOnDiskAssets);
SendMessageToClient(Client, EDebugMessageType::AssetDatabaseFinished, FinishedMessage);
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptRuntime\Debugging\AngelscriptDebugServer.cpp
// 函数: RequestDebugDatabase handler / SendDebugDatabase / BindAssetRegistry / SendAssetDatabase
// 位置: 822-825, 1498-1505, 2078-2203
// 说明: current 保留同一条元数据流，但 settings 只发 core flags
// ============================================================================
if (MessageType == EDebugMessageType::RequestDebugDatabase)
{
	ClientsThatWantDebugDatabase.Add(Client);
	SendDebugDatabase(Client);
}

DebugSettings.bAutomaticImports = FAngelscriptEngine::Get().ShouldUseAutomaticImportMethod();
DebugSettings.bFloatIsFloat64 = GetDefault<UAngelscriptSettings>()->bScriptFloatIsFloat64;
DebugSettings.bUseAngelscriptHaze = !!WITH_ANGELSCRIPT_HAZE;
DebugSettings.bDeprecateStaticClass = ...;
DebugSettings.bDisallowStaticClass = ...;
SendMessageToClient(Client, EDebugMessageType::DebugDatabaseSettings, DebugSettings);

AssetRegistry.OnFilesLoaded().AddLambda([this, BindAssetRegistryChanges]()
{
	for (auto* ConnectedClient : ClientsThatWantDebugDatabase)
		SendAssetDatabase(ConnectedClient);
	BindAssetRegistryChanges();
});

SendMessageToClient(Client, EDebugMessageType::AssetDatabaseInit, InitMessage);
AssetRegistry.EnumerateAllAssets([&](const FAssetData& AssetData) -> bool { ... });
SendMessageToClient(Client, EDebugMessageType::AssetDatabaseFinished, FinishedMessage);
```

[3] current 的自动化客户端已经覆盖断点、步进、变量和求值，但还没有把 `RequestDebugDatabase` 做成同等级 helper。这意味着“实现还在”与“验证闭环同样完整”要分开判断：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\AngelscriptProject\Plugins\Angelscript\Source\AngelscriptTest\Shared\AngelscriptDebuggerTestClient.h
// 位置: 67-80
// 说明: 当前测试客户端公开的主动请求 API 仍聚焦断点/步进/变量查看
// ============================================================================
bool SendStartDebugging(int32 AdapterVersion);
bool SendContinue();
bool SendStopDebugging();
bool SendDisconnect();
bool SendStepIn();
bool SendStepOver();
bool SendStepOut();
bool SendRequestCallStack();
bool SendRequestBreakFilters();
bool SendRequestVariables(const FString& ScopePath);
bool SendRequestEvaluate(const FString& Path, int32 DefaultFrame = 0);
bool SendSetBreakpoint(const FAngelscriptBreakpoint& Breakpoint);
bool SendClearBreakpoints(const FAngelscriptClearBreakpoints& Breakpoints);
```

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| `RequestDebugDatabase` 消息入口 | 有 | 有 | **没有实现差距** |
| `AssetDatabaseInit -> AssetDatabase -> Finished` 流程 | 有 | 有 | **没有实现差距** |
| `AssetRegistry` 增量广播 | 有 | 有 | **没有实现差距** |
| `DebugDatabaseSettings` schema | version `7`，含 3 个附加策略字段 | version `5`，附加策略字段缺失 | **接口收窄** |
| debugger automation 对 database 流的直接封装 | 本轮未展开上游测试树 | current 客户端 helper 未覆盖该请求 | **实现存在，但验证闭环较弱** |

这一节最稳的结论不是“debug database 还在”这么粗，而是：**current 保留了 UEAS2 的 IDE 元数据主流程，但把 settings 协商面收窄到了更核心的五项；因此它不是‘没有实现’，而是‘协议仍在、schema 变窄、自动化回归还没跟上这条支线’。**

---

## 深化分析 (2026-04-09 01:12:15)

### [维度 D2/D3/D11] `UINTERFACE` 已经变成 current 的插件内 owner；UEAS2 插件源码里没有同位预处理入口

前文多次谈到 `BlueprintOverride`、`ScriptMixin` 和组件链，但没有把 `UInterface` 这条线单独钉死。补完源码后更准确的结论是：**在当前快照里，UEAS2 插件源码侧没有把 `UINTERFACE()` 当作一等 chunk 处理；current 则把它完整做成了插件内流水线。**

更具体地说，current 的能力不是“AngelScript 原生 interface 恢复了”，而是“`UINTERFACE()` 声明在插件侧被识别、注册成 AS type，再生成 `CLASS_Interface` 的 `UClass`，并用反射桥执行方法”。这条线解决的是 **UE interface bridge**，不是完整的 **stock AngelScript interface parity**。current 自己也用负测把边界写死了：纯 `interface IValueProvider {}` 依旧判定为 unsupported。

```
[D2/D3/D11] UINTERFACE Ownership
UEAS2 plugin source
├─ Preprocessor::EChunkType = Global/Class/Struct/Enum     // 没有 Interface chunk
├─ macro scan = UCLASS / USTRUCT / UENUM                  // 没有 UINTERFACE 入口
└─ no plugin-owned UINTERFACE pipeline                    // 插件源码侧未见同位 owner

current plugin
├─ Preprocessor::EChunkType::Interface                    // 新增 Interface chunk
├─ detect UINTERFACE + parse inheritance/method decls     // 捕获接口声明与方法签名
├─ RegisterObjectType + RegisterObjectMethod              // 先注册 AS interface type
├─ ClassGenerator builds CLASS_Interface UClass           // 再生成 UE metadata class
└─ tests cover inherit / dispatch / hot reload            // 形成专门回归树
```

[1] UEAS2 插件源码侧的 preprocessor 没有 `Interface` chunk，也没有 `UINTERFACE()` 扫描入口；从插件源码证据看，这条 owner 并不在插件主链里：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Public\Preprocessor\AngelscriptPreprocessor.h
// 位置: 89-95
// 说明: UEAS2 插件侧 chunk 类型只有 Global / Class / Struct / Enum
// ============================================================================
enum class EChunkType : uint8
{
	Global,
	Class,
	Struct,
	Enum,
};
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Preprocessor\AngelscriptPreprocessor.cpp
// 位置: 695-704, 2984-3008, 3333-3353
// 说明: 检测与挂 class macro 的入口也只覆盖 class / struct / enum
// ============================================================================
for (FChunk& Chunk : File.ChunkedCode)
{
	if (Chunk.Type == EChunkType::Class)
		DetectClasses(File, Chunk);
	else if (Chunk.Type == EChunkType::Struct)
		DetectClasses(File, Chunk);
	else if (Chunk.Type == EChunkType::Enum)
		DetectEnum(File, Chunk);
}

if (bHasClassMacro && (ChunkType == EChunkType::Class || ChunkType == EChunkType::Struct || ChunkType == EChunkType::Enum))
{
	Chunk.Macros.Add(PendingClassMacro);
	...
}

bool bIsClass = FCString::Strncmp(&File.RawCode[ChunkEnd], TEXT("UCLASS("), 7) == 0;
bool bIsStruct = FCString::Strncmp(&File.RawCode[ChunkEnd], TEXT("USTRUCT("), 8) == 0;
bool bIsEnum = FCString::Strncmp(&File.RawCode[ChunkEnd], TEXT("UENUM("), 6) == 0;

if (bIsClass || bIsStruct)
{
	// ★ 插件侧没有并列的 UINTERFACE 检测分支
	PendingClassMacro = FMacro();
	PendingClassMacro.Type = EMacroType::Class;
	...
}
```

[2] current 则在 preprocessor 层显式引入 `Interface` chunk，解析继承关系，并把 `UINTERFACE()` 声明预注册成 AS interface type：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h
// 位置: 78-84
// 说明: current 的 chunk taxonomy 已经正式包含 Interface
// ============================================================================
enum class EChunkType : uint8
{
	Global,
	Class,
	Struct,
	Interface,
	Enum,
};
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp
// 位置: 778-823, 1014-1018, 1093-1157, 3557-3562
// 说明: current 在插件侧识别 UINTERFACE，提取方法声明，并注册 AS interface type
// ============================================================================
if (Chunk.Type == EChunkType::Interface)
{
	ClassDesc->bIsInterface = true;
	ClassDesc->bAbstract = true; // Interfaces are always abstract
}

if (ClassDesc->bIsInterface)
{
	// ★ 没写父类时，默认落到 UInterface
	ClassDesc->SuperClass = TEXT("UInterface");
}

if (Chunk.Type == EChunkType::Class || Chunk.Type == EChunkType::Interface)
{
	// ★ 解析 "class Foo : Base, IOne, ITwo" 中逗号后的接口名
	for (int32 i = 1; i < InheritanceList.Num(); ++i)
	{
		FString InterfaceName = InheritanceList[i].TrimStartAndEnd();
		if (InterfaceName.Len() > 0)
			ClassDesc->ImplementedInterfaces.Add(InterfaceName);
	}
}

// ★ 接口 chunk 自己不会交给 AS parser；UE metadata 与方法表由插件后续生成
if (ClassDesc->bIsInterface)
{
	...
}

int TypeId = Engine.Engine->RegisterObjectType(
	TCHAR_TO_ANSI(*InterfaceName),
	0,
	asOBJ_REF | asOBJ_NOCOUNT | asOBJ_IMPLICIT_HANDLE);

if (TypeId >= 0 || TypeId == asALREADY_REGISTERED)
{
	asITypeInfo* InterfaceScriptType = Engine.Engine->GetTypeInfoByName(TCHAR_TO_ANSI(*InterfaceName));
	...
	int32 FuncId = Engine.Engine->RegisterObjectMethod(
		TCHAR_TO_ANSI(*InterfaceName),
		TCHAR_TO_ANSI(*ASDecl),
		asFUNCTION(CallInterfaceMethod),
		asCALL_GENERIC,
		nullptr);
	// ★ 接口方法先挂到 AS type 上，真实 UFunction 运行时再解析
}

bool bIsClass = FCString::Strncmp(&File.RawCode[ChunkEnd], TEXT("UCLASS("), 7) == 0;
bool bIsStruct = FCString::Strncmp(&File.RawCode[ChunkEnd], TEXT("USTRUCT("), 8) == 0;
bool bIsInterface = FCString::Strncmp(&File.RawCode[ChunkEnd], TEXT("UINTERFACE("), 11) == 0;

if (bIsClass || bIsStruct || bIsInterface)
{
	// ★ 插件自己持有 UINTERFACE 的检测入口
	PendingClassMacro = FMacro();
	...
}
```

[3] current 的 class generator 再把这批描述落成真正的 UE interface class，并通过 generic callback 做运行时分派：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 函数: CallInterfaceMethod
// 位置: 56-67
// 说明: 接口方法不会直接编译出 AS body，而是在调用时转向 UObject::FindFunction + ProcessEvent
// ============================================================================
void CallInterfaceMethod(asIScriptGeneric* InGeneric)
{
	asCGeneric* Generic = static_cast<asCGeneric*>(InGeneric);
	auto* Sig = (FInterfaceMethodSignature*)Generic->GetFunction()->GetUserData();
	if (Sig == nullptr) return;

	UObject* Object = (UObject*)Generic->GetObject();
	if (Object == nullptr) return;

	UFunction* RealFunc = Object->FindFunction(Sig->FunctionName);
	if (RealFunc == nullptr) return;
	InvokeReflectiveUFunctionFromGenericCall(Generic, Object, RealFunc);
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 位置: 2762-2804, 3359-3364, 5060-5105
// 说明: current 把接口类当成“纯 UE metadata + AS type 桥”，并显式建立 ImplementsInterface 链
// ============================================================================
else if (ClassData.NewClass->bIsInterface)
{
	// ★ 接口类不走普通 AS 属性/函数生成路径
	auto InterfaceDesc = ClassData.NewClass;
	UClass* NewClass = InterfaceDesc->Class;
	...
	NewClass->SetSuperStruct(SuperClass);
	NewClass->ClassFlags |= CLASS_Interface | CLASS_Abstract;
	...
	for (const FString& MethodDecl : InterfaceDesc->InterfaceMethodDeclarations)
	{
		// ★ 从 method declaration 反建 UFunction
		...
	}
}

if (ClassDesc->bIsInterface)
{
	NewClass->ClassFlags |= CLASS_Interface | CLASS_Abstract;
	// ★ 不设置 CLASS_Native，沿用 Blueprint/Script interface 的指针模式
}

if (ClassDesc->ImplementedInterfaces.Num() > 0 && !ClassDesc->bIsInterface)
{
	auto ResolveInterfaceClass = [this](const FString& InterfaceName) -> UClass*
	{
		// ★ 先查 angelscript interface，再退到 C++ class / TObjectIterator
		...
		if (It->GetName() == UnrealInterfaceName && It->HasAnyClassFlags(CLASS_Interface))
			return *It;
		return nullptr;
	};
}
```

[4] current 不是只把实现放进生产代码里，还给 `UINTERFACE` 建了专门回归；同时它也显式保留了“纯 AS interface 仍不支持”的负测边界：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceAdvancedTests.cpp
// 位置: 82-96, 131-143, 327-345, 388-409
// 说明: current 对 script UINTERFACE 的继承与 hot reload 有正式自动化回归
// ============================================================================
UINTERFACE()
interface UIDamageableParent
{
	void TakeDamage(float Amount);
}

UINTERFACE()
interface UIKillableChild : UIDamageableParent
{
	void Kill();
}

class AScenarioInterfaceInherited : AActor, UIKillableChild
{
	...
}

TestNotNull(TEXT("Parent interface class should exist"), ParentInterface);
TestNotNull(TEXT("Child interface class should exist"), ChildInterface);
TestTrue(TEXT("Actor should implement child interface UIKillableChild"), Actor->GetClass()->ImplementsInterface(ChildInterface));
TestTrue(TEXT("Actor implementing child interface should also satisfy parent UIDamageableParent"), Actor->GetClass()->ImplementsInterface(ParentInterface));

if (!TestTrue(TEXT("Interface hot reload should succeed on the full reload path"),
	CompileModuleWithResult(&Engine, ECompileType::FullReload, ModuleName, TEXT("ScenarioInterfaceHotReload.as"), ScriptV2, ReloadResult)))
{
	return false;
}

TestTrue(TEXT("V2 class should still implement interface after hot reload"), ClassV2->ImplementsInterface(InterfaceV2));
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptInheritanceTests.cpp
// 位置: 63-78
// 说明: current 也明确保留了负边界：纯 AngelScript interface 语法并未完全打通
// ============================================================================
const bool bCompiled = CompileModuleWithResult(
	&Engine,
	ECompileType::SoftReloadOnly,
	TEXT("ASInheritanceInterface"),
	ScriptFilename,
	TEXT("interface IValueProvider { int GetValue(); } class Provider : IValueProvider { int GetValue() { return 42; } } int Test() { Provider Instance; return 42; }"),
	CompileResult);

if (!TestFalse(TEXT("Inheritance.Interface should remain unsupported on this branch"), bCompiled))
{
	return false;
}
```

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| 插件源码是否识别 `UINTERFACE()` chunk | 未见；`EChunkType` 和 macro scan 只覆盖 `Class/Struct/Enum` | 有完整 `Interface` chunk 与 `UINTERFACE` 检测 | **没有实现 vs 已实现** |
| 接口方法调用 owner | 插件源码里未见同位 `RegisterObjectMethod + generic bridge` | `RegisterObjectMethod(..., CallInterfaceMethod)` + `FindFunction/ProcessEvent` | **当前插件新增能力** |
| 接口 `UClass` 生成与 `ImplementsInterface` 链 | 插件源码里未见同位生成 owner | `CLASS_Interface` + `SetSuperStruct` + `ImplementedInterfaces` 解析 | **当前插件新增能力** |
| 纯 AngelScript `interface` 语法完整度 | 本轮未在插件源码里定位到同位路径 | 仍由负测固定为 unsupported | **current 只补了 UE interface bridge，不是完整 parity** |

这一节更精确的表述应该是：**current 不是“把 UEAS2 的 interface 功能原样搬回插件”，而是在无引擎补丁前提下新增了一条插件自持的 `UINTERFACE` 桥接链；与此同时，它也明确没有宣称自己已经恢复 stock AngelScript `interface` 的全语义。**

### [维度 D4] current 把“新增 delegate”从 `FullReloadRequired` 降成 `FullReloadSuggested`，目标是让 `SoftReloadOnly` 仍能换入模块

前文已经把 class / property / new-type 的 reload 策略讲得比较多，但 delegate 自己还有一条没写开的分叉。源码显示：**两边都把“已有 delegate 的签名变化”视为 `FullReloadRequired`；真正分叉的是“新增 delegate”这一支。UEAS2 仍要求 `FullReloadRequired`，current 则专门降成 `FullReloadSuggested`。**

这不是语义噪音。因为运行时 dispatcher 两边都把 `FullReloadSuggested + SoftReloadOnly` 处理成“警告 + `SwapInModules()` + `PerformSoftReload()` + 排队等全量 reload”，所以 current 实际上把“新增 delegate”从“PIE 下只能保留旧模块”改成了“PIE 下先软换入，再补全量 reload”。换句话说，**dispatcher 没变，进入 dispatcher 的 reload 分类被 current 改了。**

```
[D4] Delegate Reload Decision
UEAS2
├─ old delegate signature changed -> FullReloadRequired
└─ new delegate introduced        -> FullReloadRequired
   └─ SoftReloadOnly keeps old module active

current
├─ old delegate signature changed -> FullReloadRequired
└─ new delegate introduced        -> FullReloadSuggested
   └─ SoftReloadOnly may swap in module, then queue full reload
```

[1] UEAS2 对 delegate 的两条分支都走硬性的 full reload：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\ClassGenerator\AngelscriptClassGenerator.cpp
// 函数: FAngelscriptClassGenerator::Analyze(FModuleData&, FDelegateData&)
// 位置: 1518-1538
// 说明: UEAS2 对“签名变更”与“新增 delegate”都维持 FullReloadRequired
// ============================================================================
if (DelegateData.OldDelegate.IsValid())
{
	if (!DelegateData.OldDelegate->Signature.IsValid()
		|| !DelegateData.OldDelegate->Signature->SignatureMatches(FunctionDesc, true)
		|| !DelegateData.OldDelegate->Signature->IsDefinitionEquivalent(*FunctionDesc))
	{
		// ★ 已有 delegate 变更签名，必须 full reload
		if (DelegateData.ReloadReq < EReloadRequirement::FullReloadRequired)
		{
			DelegateData.ReloadReq = EReloadRequirement::FullReloadRequired;
			DelegateData.ReloadReqLines.AddUnique(DelegateDesc->LineNumber);
		}
	}
}
else
{
	// ★ 新增 delegate 在 UEAS2 里也仍然是 FullReloadRequired
	if (DelegateData.ReloadReq < EReloadRequirement::FullReloadRequired)
	{
		DelegateData.ReloadReq = EReloadRequirement::FullReloadRequired;
		DelegateData.ReloadReqLines.AddUnique(DelegateDesc->LineNumber);
	}
}
```

[2] current 只改了“新增 delegate”这条分支，源码直接把意图写在注释里：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 函数: FAngelscriptClassGenerator::Analyze(FModuleData&, FDelegateData&)
// 位置: 1539-1560
// 说明: current 有意把“新增 delegate”降到 FullReloadSuggested
// ============================================================================
if (DelegateData.OldDelegate.IsValid())
{
	if (!DelegateData.OldDelegate->Signature.IsValid()
		|| !DelegateData.OldDelegate->Signature->SignatureMatches(FunctionDesc, true)
		|| !DelegateData.OldDelegate->Signature->IsDefinitionEquivalent(*FunctionDesc))
	{
		// ★ 已有 delegate 变更签名仍然是硬性 full reload
		if (DelegateData.ReloadReq < EReloadRequirement::FullReloadRequired)
		{
			DelegateData.ReloadReq = EReloadRequirement::FullReloadRequired;
			DelegateData.ReloadReqLines.AddUnique(DelegateDesc->LineNumber);
		}
	}
}
else
{
	//[UE++]: Downgrade to FullReloadSuggested so SoftReloadOnly can swap in the module
	if (DelegateData.ReloadReq < EReloadRequirement::FullReloadSuggested)
	{
		DelegateData.ReloadReq = EReloadRequirement::FullReloadSuggested;
		DelegateData.ReloadReqLines.AddUnique(DelegateDesc->LineNumber);
	}
	//[UE--]
}
```

[3] 两边的 runtime dispatcher 本身没有分叉；`FullReloadSuggested` 在 `SoftReloadOnly` 下都会走“先 soft swap-in，再排 full reload”的处理。因此 current 的真实变化点是分类条件，而不是执行器：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\AngelscriptManager.cpp
// 位置: 2625-2653
// 说明: UEAS2 的 dispatcher 对 Suggested 分支会在 SoftReloadOnly 下先软换入模块
// ============================================================================
case FAngelscriptClassGenerator::EReloadRequirement::FullReloadSuggested:
	if (CompileType == ECompileType::SoftReloadOnly)
	{
		// ★ 发警告，但仍执行 SwapInModules + PerformSoftReload
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
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 3942-3970
// 说明: current 的 dispatcher 同构；因此前一段分类降级会直接改变运行时行为
// ============================================================================
case FAngelscriptClassGenerator::EReloadRequirement::FullReloadSuggested:
	if (CompileType == ECompileType::SoftReloadOnly)
	{
		// ★ 仍然先 SwapInModules + PerformSoftReload
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
```

[4] current 现有 delegate 自动化主要锁定“编译后元数据是否生成正确”，还没有把这条 reload 决策单独做成同等级回归：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineTests.cpp
// 位置: 56-68, 395-409
// 说明: 当前测试覆盖 delegate metadata / UDelegateFunction 产物，但不直接覆盖 reload requirement
// ============================================================================
const TSharedPtr<FAngelscriptDelegateDesc> SimpleDelegate = Engine.GetDelegate(TEXT("FCompilerTransferDelegate"));
const TSharedPtr<FAngelscriptDelegateDesc> MultiDelegate = Engine.GetDelegate(TEXT("FCompilerTransferEvent"));
...
TestFalse(TEXT("Simple delegate should remain single-cast"), SimpleDelegate->bIsMulticast);
TestTrue(TEXT("Event delegate should remain multicast"), MultiDelegate->bIsMulticast);

const TSharedPtr<FAngelscriptDelegateDesc> SingleCast = Engine.GetDelegate(TEXT("FCompilerSingleCastSignature"));
const TSharedPtr<FAngelscriptDelegateDesc> MultiCast = Engine.GetDelegate(TEXT("FCompilerMultiCastSignature"));
...
TestNotNull(TEXT("Single-cast delegate should materialize a UDelegateFunction"), SingleCast->Function);
TestNotNull(TEXT("Multicast delegate should materialize a UDelegateFunction"), MultiCast->Function);
```

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| 已有 delegate 签名变更 | `FullReloadRequired` | `FullReloadRequired` | **没有实现差距** |
| 新增 delegate | `FullReloadRequired` | `FullReloadSuggested` | **实现方式不同** |
| `SoftReloadOnly` 下对 `FullReloadSuggested` 的执行器 | `SwapInModules + PerformSoftReload` | 同样 `SwapInModules + PerformSoftReload` | **没有执行器差距** |
| delegate 自动化重心 | 本轮未展开上游测试树 | 现有测试偏 compile metadata / `UDelegateFunction` materialize | **当前这条新 contract 的验证闭环偏弱** |

这条差异不能简单写成“current 的 hot reload 更强”或“UEAS2 更保守”这么空。更准确的说法是：**current 只把“新增 delegate”从硬阻塞降成了可软换入，再复用原本就存在的 `FullReloadSuggested` dispatcher；这是一种交互体验取舍，不是基础设施重写。**

### [维度 D2/D5/D6] `FAngelscriptDelegateWithPayload` 的运行时能力还在，但 current 把 delegate payload 元数据面收窄了

这一条很容易被误读成“delegate payload 没了”，源码并不支持这种说法。更准确的结论是：**current 保留了 `BindWithPayload()` 的运行时绑定与执行能力，但削薄了这条能力暴露给 IDE/debug database 的元数据面。**

最直接的证据有两条：

1. `Bind_FAngelscriptDelegateWithPayload.cpp` 里，UEAS2 会给两个方法附 `SCRIPT_MANUAL_BIND_META(...)`，包括 `DelegateWildcardParam = Payload`；current 保留方法本身，但把这组 meta 去掉了。  
2. `DebugServer` 的 `NAMES_InformedMeta` 在 UEAS2 有 4 项，current 只剩 3 项，少的正好就是 `DelegateWildcardParam`。由于两边后续都用 `for (FName MetaTag : NAMES_InformedMeta)` 把 meta 发给客户端，所以 current IDE 侧天然收不到这个 wildcard 提示。

```
[D2/D5/D6] Delegate Payload Metadata Surface
UEAS2
├─ BindWithPayload runtime path exists                      // 运行时绑定存在
├─ SCRIPT_MANUAL_BIND_META for Object/Function/Type/Wild   // 手写 bind 元数据完整
├─ DebugServer informed meta = 4 tags                      // IDE/debug database 可见 wildcard
└─ ScriptCallable helper keeps delegate bind hints         // 函数库入口也带 meta

current
├─ BindWithPayload runtime path exists                      // 运行时功能未删
├─ manual bind meta removed from payload binder            // wildcard hint owner 消失
├─ DebugServer informed meta = 3 tags                      // 少了 DelegateWildcardParam
└─ simple delegate helper still keeps basic meta           // 简单 delegate 提示仍保留
```

[1] UEAS2 的 payload delegate binder 同时维护运行时能力与 IDE/debugger 可消费的 manual bind meta：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Binds\Bind_FAngelscriptDelegateWithPayload.cpp
// 位置: 22-39
// 说明: UEAS2 不只暴露方法，还额外打上 Delegate* 元数据
// ============================================================================
FAngelscriptDelegateWithPayload_.Method("void BindUFunction(UObject Object, const FName& FunctionName)",
[](FAngelscriptDelegateWithPayload& Delegate, UObject* Object, const FName& FunctionName)
{
	Delegate.BindUFunction(Object, FunctionName);
});
SCRIPT_MANUAL_BIND_META("DelegateObjectParam", "Object");
SCRIPT_MANUAL_BIND_META("DelegateFunctionParam", "FunctionName");
SCRIPT_MANUAL_BIND_META("DelegateBindType", "FInternalEmptyDelegate");

FAngelscriptDelegateWithPayload_.Method("void BindWithPayload(UObject Object, const FName& FunctionName, const ?&in Payload)",
[](FAngelscriptDelegateWithPayload& Delegate, UObject* Object, const FName& FunctionName, void* PayloadPtr, int PayloadScriptTypeId)
{
	Delegate.BindUFunctionWithPayload(Object, FunctionName, PayloadPtr, PayloadScriptTypeId);
});
SCRIPT_MANUAL_BIND_META("DelegateObjectParam", "Object");
SCRIPT_MANUAL_BIND_META("DelegateFunctionParam", "FunctionName");
SCRIPT_MANUAL_BIND_META("DelegateBindType", "FInternalEmptyDelegateWithPayload");
SCRIPT_MANUAL_BIND_META("DelegateWildcardParam", "Payload");
```

[2] current 运行时方法还在，但 manual bind meta 已经被拿掉了：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FAngelscriptDelegateWithPayload.cpp
// 位置: 22-33, 85-95
// 说明: current 保留运行时 BindWithPayload，但不再附加 DelegateWildcardParam 等 manual meta
// ============================================================================
Delegate_.Method("void BindUFunction(UObject Object, const FName& FunctionName)",
[](FAngelscriptDelegateWithPayload& Delegate, UObject* Object, const FName& FunctionName)
{
	Delegate.BindUFunction(Object, FunctionName);
});

Delegate_.Method("void BindWithPayload(UObject Object, const FName& FunctionName, const ?&in Payload)",
[](FAngelscriptDelegateWithPayload& Delegate, UObject* Object, const FName& FunctionName, void* PayloadPtr, int PayloadScriptTypeId)
{
	Delegate.BindUFunctionWithPayload(Object, FunctionName, PayloadPtr, PayloadScriptTypeId);
});

UScriptStruct* StructType = Cast<UScriptStruct>(FAngelscriptEngine::Get().GetUnrealStructFromAngelscriptTypeId(PayloadScriptTypeId));
if (StructType == nullptr)
{
	StructType = FAngelscriptDelegateWithPayload::GetBoxedPrimitiveStructFromTypeId(PayloadScriptTypeId);
}
// ★ 运行时 payload 装箱逻辑仍在，收缩的是“元数据面”，不是“执行能力”
```

[3] 这个差异会直接进入 debug database，因为两边都按 `NAMES_InformedMeta` 白名单挑选要发给客户端的 meta tag：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Debugging\AngelscriptDebugServer.cpp
// 位置: 33-39, 1478-1490
// 说明: UEAS2 的 informed meta 白名单包含 DelegateWildcardParam
// ============================================================================
const FName NAME_ScriptKeywords("ScriptKeywords");
const TArray<FName> NAMES_InformedMeta = {
	"DelegateBindType",
	"DelegateFunctionParam",
	"DelegateObjectParam",
	"DelegateWildcardParam",
};

const FString& Keywords = UnrealFunction->GetMetaData(NAME_ScriptKeywords);
...
for (FName MetaTag : NAMES_InformedMeta)
{
	const FString& MetaValue = UnrealFunction->GetMetaData(MetaTag);
	if (!MetaValue.IsEmpty())
	{
		if (!MetaObject.IsValid())
			MetaObject = MakeShared<FJsonObject>();
		MetaObject->SetStringField(MetaTag.ToString(), MetaValue);
	}
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 位置: 33-38, 1600-1612
// 说明: current 的白名单只剩 3 个键，因此客户端天然看不到 wildcard 提示
// ============================================================================
const FName NAME_ScriptKeywords("ScriptKeywords");
const TArray<FName> NAMES_InformedMeta = {
	"DelegateBindType",
	"DelegateFunctionParam",
	"DelegateObjectParam",
};

const FString& Keywords = UnrealFunction->GetMetaData(NAME_ScriptKeywords);
...
for (FName MetaTag : NAMES_InformedMeta)
{
	const FString& MetaValue = UnrealFunction->GetMetaData(MetaTag);
	if (!MetaValue.IsEmpty())
	{
		if (!MetaObject.IsValid())
			MetaObject = MakeShared<FJsonObject>();
		MetaObject->SetStringField(MetaTag.ToString(), MetaValue);
	}
}
```

[4] 不是所有 delegate 提示都没了。current 仍保留简单 delegate 场景的 meta，只是入口从 `ScriptCallable` 收敛到了 `BlueprintCallable`：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Public\FunctionLibraries\UAssetManagerMixinLibrary.h
// 位置: 69-74
// 说明: UEAS2 用 ScriptCallable + Delegate* meta 暴露简单 delegate 绑定入口
// ============================================================================
UFUNCTION(ScriptCallable, Meta = (DelegateObjectParam = "Object", DelegateFunctionParam = "FunctionName", DelegateBindType = "FSimpleDelegate"))
static void CallOrRegister_OnCompletedInitialScan(UAssetManager* AssetManager, UObject* Object, const FName& FunctionName)
{
	AssetManager->CallOrRegister_OnCompletedInitialScan(FSimpleDelegate::CreateUFunction(Object, FunctionName));
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/UAssetManagerMixinLibrary.h
// 位置: 77-82
// 说明: current 继续保留简单 delegate 提示，但 authoring contract 已切到 BlueprintCallable
// ============================================================================
//UFUNCTION(ScriptCallable, Meta = (DelegateObjectParam = "Object", DelegateFunctionParam = "FunctionName", DelegateBindType = "FSimpleDelegate"))
UFUNCTION(BlueprintCallable, Meta = (DelegateObjectParam = "Object", DelegateFunctionParam = "FunctionName", DelegateBindType = "FSimpleDelegate"))
static void CallOrRegister_OnCompletedInitialScan(UAssetManager* AssetManager, UObject* Object, const FName& FunctionName)
{
	AssetManager->CallOrRegister_OnCompletedInitialScan(FSimpleDelegate::CreateUFunction(Object, FunctionName));
}
```

[5] current 的现有 parity test 只验证了 `FAngelscriptDelegateWithPayload` 类型存在、且暴露了两个基础方法，还没有把 `BindWithPayload` 元数据导出链一并锁住：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp
// 位置: 151-167
// 说明: 现有测试覆盖类型与基础方法，不覆盖 payload wildcard meta / debug database 可见性
// ============================================================================
asITypeInfo* TypeInfo = Engine->GetScriptEngine()->GetTypeInfoByName("FAngelscriptDelegateWithPayload");
if (!TestNotNull(TEXT("FAngelscriptDelegateWithPayload should exist in the script type system"), TypeInfo))
{
	return false;
}

const bool bHasIsBound = TestNotNull(TEXT("FAngelscriptDelegateWithPayload should expose IsBound()"), TypeInfo->GetMethodByDecl("bool IsBound() const"));
const bool bHasExecuteIfBound = TestNotNull(TEXT("FAngelscriptDelegateWithPayload should expose ExecuteIfBound()"), TypeInfo->GetMethodByDecl("void ExecuteIfBound() const"));
return bHasIsBound && bHasExecuteIfBound;
```

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| `BindWithPayload` 运行时能力 | 有 | 有 | **没有实现差距** |
| payload delegate 的 manual bind meta | `DelegateObjectParam / DelegateFunctionParam / DelegateBindType / DelegateWildcardParam` 全有 | `Bind_FAngelscriptDelegateWithPayload.cpp` 未保留这组 meta | **实现质量差异 / tooling 面收窄** |
| debug database 对 payload wildcard 的提示 | `NAMES_InformedMeta` 包含 `DelegateWildcardParam` | 白名单里缺少 `DelegateWildcardParam` | **接口收窄** |
| 简单 delegate helper 的 meta | `ScriptCallable` + `Delegate*` meta | `BlueprintCallable` + `Delegate*` meta | **实现方式不同** |
| 自动化回归重心 | 本轮未展开上游测试树 | 现有 parity test 只锁类型与基础方法 | **当前这条元数据链验证偏弱** |

这一节最需要避免的误判是：**current 并没有丢掉 payload delegate 的运行时功能，丢的是“把 payload wildcard 这层语义显式告诉 IDE/debugger”的那部分 metadata surface。** 这应归类为 **实现质量差异 / tooling 面收窄**，而不是 **没有实现 payload delegate**。

---

## 深化分析 (2026-04-09 06:38:46)

### [维度 D1/D8] 外部依赖 owner 的变化不只是“少两个模块”，而是从 `sibling plugin contract` 收口成单插件 manifest + `Runtime` 自持 vendoring

前文已经讨论过 `Loader`、`Runtime` 和测试边界，但还有一层更底的差异没有单独钉死：**第三方 AngelScript 内核与 GAS / EnhancedInput 扩展到底由谁拥有。**

这层 owner 在 UEAS2 与 current 之间变化很大。UEAS2 的 base plugin 只声明 `AngelscriptCode / AngelscriptEditor / AngelscriptLoader` 三模块，`AngelscriptCode.Build.cs` 还直接假设自己位于 `../Plugins/Angelscript`，从插件根目录取 `ThirdParty`。而 `EnhancedInput` / `GameplayAbilities` 并不在 base plugin 内闭环，而是分别落在独立的 `AngelscriptEnhancedInput` / `AngelscriptGAS` 插件里。  
current 则把两类 owner 一并收口：AngelScript 源码通过 `ModuleDirectory/ThirdParty/angelscript` 变成 `AngelscriptRuntime` 自持；`EnhancedInput / GameplayAbilities / GameplayTasks` 直接进 `AngelscriptRuntime.Build.cs`，并在 `Angelscript.uplugin` 的 `Plugins` 数组里显式启用依赖插件。

这意味着 current 的“插件化交付边界”更像一个闭包: **一个 `Angelscript` 插件 + 一个 `Runtime` 模块 + 一组 manifest 依赖**。UEAS2 则更像“base plugin + optional capability plugins + engine-relative filesystem 假设”。

```
[D1/D8] Dependency Ownership
UEAS2
├─ Angelscript.uplugin -> Code / Editor / Loader          // 基础插件
├─ AngelscriptCode.Build.cs -> ../Plugins/Angelscript/... // ThirdParty 走插件根目录假设
├─ AngelscriptEnhancedInput.uplugin                        // EnhancedInput 作为 sibling plugin
└─ AngelscriptGAS.uplugin                                  // GAS 作为 sibling plugin

current
├─ Angelscript.uplugin -> Runtime / Editor / Test         // 单插件闭环
├─ AngelscriptRuntime.Build.cs -> ModuleDirectory/...      // ThirdParty 由 Runtime 自持
├─ AngelscriptRuntime.Build.cs -> EnhancedInput/GAS deps   // 能力依赖直接并回 Runtime
└─ Angelscript.uplugin -> enable dependency plugins        // manifest 显式闭包
```

[1] UEAS2 的 base plugin 与 `AngelscriptCode` 仍然假设“插件根目录 + sibling plugin”是固定环境：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Angelscript.uplugin
// 位置: 18-34
// 说明: UEAS2 base plugin 只声明 Code / Editor / Loader 三模块
// ============================================================================
"Modules": [
	{
		"Name": "AngelscriptCode",
		"Type": "Runtime",
		"LoadingPhase": "PostDefault"
	},
	{
		"Name": "AngelscriptEditor",
		"Type": "Editor",
		"LoadingPhase": "PostDefault"
	},
	{
		"Name": "AngelscriptLoader",
		"Type": "Runtime",
		"LoadingPhase": "PostDefault"
	}
]
```

```csharp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\AngelscriptCode.Build.cs
// 位置: 60-64
// 说明: AngelScript include path 直接写死到插件根目录相对路径
// ============================================================================
var PluginPath = "../Plugins/Angelscript";

/* Link to Angelscript */
PublicIncludePaths.Add(PluginPath + "/ThirdParty/include");
PublicIncludePaths.Add(PluginPath + "/ThirdParty/source");
```

[2] `EnhancedInput` / `GameplayAbilities` 在 UEAS2 里也不是 `AngelscriptCode` 自己吃掉，而是由 sibling plugin 分别声明：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\AngelscriptEnhancedInput\AngelscriptEnhancedInput.uplugin
// 位置: 17-33
// 说明: EnhancedInput 支持是单独插件，不在 base plugin 内闭环
// ============================================================================
"Modules": [
	{
		"Name": "AngelscriptEnhancedInput",
		"Type": "Runtime",
		"LoadingPhase": "Default"
	}
],
"Plugins": [
	{ "Name": "EnhancedInput", "Enabled": true },
	{ "Name": "Angelscript", "Enabled": true }
]
```

```csharp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\AngelscriptEnhancedInput\Source\AngelscriptEnhancedInput.Build.cs
// 位置: 17-28
// 说明: 模块直接依赖 AngelscriptCode + EnhancedInput
// ============================================================================
PublicDependencyModuleNames.AddRange(new string[]
{
	"Core",
	"CoreUObject",
	"Engine",
	"InputCore",
	"Slate",

	"AngelscriptCode",

	"EnhancedInput",
});
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\AngelscriptGAS\AngelscriptGAS.uplugin
// 位置: 18-34
// 说明: GAS 也是独立插件，依赖 GameplayAbilities + base Angelscript
// ============================================================================
"Modules": [
	{
		"Name": "AngelscriptGAS",
		"Type": "Runtime",
		"LoadingPhase": "Default"
	}
],
"Plugins": [
	{ "Name": "GameplayAbilities", "Enabled": true },
	{ "Name": "Angelscript", "Enabled": true }
]
```

```csharp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\AngelscriptGAS\Source\AngelscriptGAS.Build.cs
// 位置: 18-30
// 说明: GAS owner 在独立模块，依赖链没有并回 base plugin
// ============================================================================
PublicDependencyModuleNames.AddRange(new string[]
{
	"Core",
	"CoreUObject",
	"Engine",
	"PhysicsCore",

	"AngelscriptCode",

	"GameplayAbilities",
	"GameplayTags",
	"GameplayTasks"
});
```

[3] current 把这两层 owner 一起并回主插件：AngelScript 源码在 `Runtime` 内 vendoring，GAS / EnhancedInput 也进入 `Runtime` 模块依赖和插件 manifest：

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs
// 位置: 11-22, 30-65
// 说明: current 用 Runtime 自持 ThirdParty，并把 GAS / EnhancedInput 直接并回主模块
// ============================================================================
NumIncludedBytesPerUnityCPPOverride = 131072;
PrivateDefinitions.Add("ANGELSCRIPT_EXPORT=1");
PublicDefinitions.Add("ANGELSCRIPT_DLL_LIBRARY_IMPORT=1");

PublicIncludePaths.Add(ModuleDirectory);
PrivateIncludePaths.Add(ModuleDirectory);
...
var AngelscriptThirdPartyPath = Path.Combine(ModuleDirectory, "ThirdParty", "angelscript");
PublicIncludePaths.Add(Path.Combine(AngelscriptThirdPartyPath, "source"));
PublicIncludePaths.Add(AngelscriptThirdPartyPath);

PublicDependencyModuleNames.AddRange(new string[]
{
	"ApplicationCore",
	"Core",
	"CoreUObject",
	"Engine",
	...
	"StructUtils",
});

PrivateDependencyModuleNames.AddRange(new string[]
{
	...
	"EnhancedInput",
	"GameplayAbilities",
	"GameplayTasks",
});
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Angelscript.uplugin
// 位置: 18-48
// 说明: 依赖插件在主插件 manifest 里显式闭包，不再要求额外 sibling plugin
// ============================================================================
"Modules": [
	{
		"Name": "AngelscriptRuntime",
		"Type": "Runtime",
		"LoadingPhase": "PostDefault"
	},
	{
		"Name": "AngelscriptEditor",
		"Type": "Editor",
		"LoadingPhase": "PostDefault"
	},
	{
		"Name": "AngelscriptTest",
		"Type": "Editor",
		"LoadingPhase": "PostDefault"
	}
],
"Plugins": [
	{ "Name": "StructUtils", "Enabled": true },
	{ "Name": "EnhancedInput", "Enabled": true },
	{ "Name": "GameplayAbilities", "Enabled": true }
]
```

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs
// 位置: 23-49
// 说明: current 还把验证 owner 正式模块化，形成插件自带验证边界
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

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| AngelScript 第三方源码 owner | `AngelscriptCode.Build.cs` 通过 `../Plugins/Angelscript` 假设插件根目录 | `AngelscriptRuntime.Build.cs` 通过 `ModuleDirectory/ThirdParty/angelscript` 自持 | **实现方式不同** |
| EnhancedInput / GAS owner | 作为 `AngelscriptEnhancedInput` / `AngelscriptGAS` sibling plugin 独立存在 | 直接并入 `AngelscriptRuntime` 依赖 + 主插件 manifest | **实现方式不同** |
| 交付闭包 | 需要协调 base plugin + sibling plugin | 单插件 manifest 就能声明主要依赖 | **部署边界更集中** |
| 验证边界 | base plugin 源树内未见对等 `Test` 模块 | `AngelscriptTest` 作为正式模块存在 | **能力增强** |

更精确地说，current 在这一维不是“少了上游插件能力”，而是**把 upstream 的多插件能力 owner 收口进一个更可分发、但依赖闭包更紧的主插件**。这是架构和交付边界的变化，不是功能面简单相减。

### [维度 D3] `BlueprintOverride` 的 script-side contract 并没有跟着 wrapper header 的 authoring shift 一起变松

前文已经写过 wrapper header 从 `ScriptCallable` 转向 `BlueprintCallable`、也写过 mixin surface 的收缩；但这很容易让人误判成“`BlueprintOverride` 的 script authoring contract 也一起退了”。源码并不支持这个说法。  
更准确的结论是：**current 改的是部分原生 wrapper header 的 authoring 入口；脚本类自己的 `UFUNCTION(BlueprintOverride)` 语义链仍然沿着 `preprocessor -> class generator -> generated UClass -> Blueprint child runtime` 完整保留，而且 current 还补上了显式自动化。**

```
[D3] BlueprintOverride Contract
script source
├─ Preprocessor parses BlueprintOverride            // 语法层仍保留
│  ├─ reject static/global usage
│  └─ set BlueprintEvent + BlueprintOverride flags
├─ ClassGenerator validates override target         // 语义层未放松
│  ├─ parent event exists
│  ├─ signature / const / editor-only match
│  └─ respect ScriptName-based C++ event lookup
└─ Generated UClass
   └─ Blueprint child inherits script parent        // 运行时层继续闭环
      └─ tests spawn BP child and execute override
```

[1] `BlueprintOverride` 在 preprocessor 里的 parse contract 两边仍然是同一条规则：不能是 static/global，不能与 `BlueprintEvent` 共存，并且会同时打上 `bBlueprintEvent + bBlueprintOverride`。

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Preprocessor\AngelscriptPreprocessor.cpp
// 位置: 1410-1434
// 说明: UEAS2 的 BlueprintOverride preprocessor 规则
// ============================================================================
else if (Spec.Name == PP_NAME_BlueprintOverride)
{
	if (FunctionDesc->bIsStatic)
	{
		MacroError(File, Macro, FString::Printf(TEXT("Global UFUNCTION() %s may not be BlueprintOverride."), *FunctionDesc->FunctionName));
		bHasError = true;
		continue;
	}

	if (FunctionDesc->bBlueprintEvent)
	{
		MacroError(File, Macro, FString::Printf(TEXT("UFUNCTION() %s cannot be both BlueprintEvent and BlueprintOverride."), *FunctionDesc->FunctionName));
		bHasError = true;
		continue;
	}

	if (!bHadCallable)
		FunctionDesc->bBlueprintCallable = false;

	// ★ 仍然把 override 视为 event 的一个特例
	FunctionDesc->bBlueprintEvent = true;
	FunctionDesc->bBlueprintOverride = true;
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp
// 位置: 1654-1678
// 说明: current 对 BlueprintOverride 的 preprocessor 规则与 UEAS2 同构
// ============================================================================
else if (Spec.Name == PP_NAME_BlueprintOverride)
{
	if (FunctionDesc->bIsStatic)
	{
		MacroError(File, Macro, FString::Printf(TEXT("Global UFUNCTION() %s may not be BlueprintOverride."), *FunctionDesc->FunctionName));
		bHasError = true;
		continue;
	}

	if (FunctionDesc->bBlueprintEvent)
	{
		MacroError(File, Macro, FString::Printf(TEXT("UFUNCTION() %s cannot be both BlueprintEvent and BlueprintOverride."), *FunctionDesc->FunctionName));
		bHasError = true;
		continue;
	}

	if (!bHadCallable)
		FunctionDesc->bBlueprintCallable = false;

	// ★ current 没有把这条 script contract 删掉，只是保留在脚本预处理器侧
	FunctionDesc->bBlueprintEvent = true;
	FunctionDesc->bBlueprintOverride = true;
}
```

[2] override 的合法性校验链在 class generator 里也没有被 current 放松。无论是父类事件存在性、签名一致性、`const` 正确性，还是 editor-only 边界，current 仍然逐项报错。

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\ClassGenerator\AngelscriptClassGenerator.cpp
// 位置: 744-810, 963-997
// 说明: UEAS2 用两段校验覆盖“能不能 override”与“override 得对不对”
// ============================================================================
if (!SuperFunctionDesc.IsValid())
{
	FAngelscriptManager::Get().ScriptCompileError(ModuleData.NewModule, FunctionDesc->LineNumber, FString::Printf(
		TEXT("BlueprintOverride method %s in class %s does not exist in superclass %s."),
		*FunctionDesc->FunctionName, *ClassData.NewClass->ClassName, *AngelscriptSuperClass->ClassName));
	ClassData.ReloadReq = EReloadRequirement::Error;
}
...
else if (!SuperFunctionDesc->SignatureMatches(FunctionDesc))
{
	FAngelscriptManager::Get().ScriptCompileError(ModuleData.NewModule, FunctionDesc->LineNumber, FString::Printf(
		TEXT("BlueprintOverride method %s in class %s does not match signature of event declared in superclass %s."),
		*FunctionDesc->FunctionName, *ClassData.NewClass->ClassName, *AngelscriptSuperClass->ClassName));
	ClassData.ReloadReq = EReloadRequirement::Error;
}
...
FAngelscriptManager::Get().ScriptCompileError(ModuleData.NewModule, FunctionDesc->LineNumber, FString::Printf(
	TEXT("BlueprintOverride method %s in class %s does not match function signature of event in superclass %s.\nExpected Signature: %s"),
	*FunctionDesc->FunctionName, *ClassData.NewClass->ClassName, *CodeSuperClass->GetName(), *ExpectedSignature));
...
FAngelscriptManager::Get().ScriptCompileError(ModuleData.NewModule, FunctionDesc->LineNumber, FString::Printf(
	TEXT("BlueprintOverride method %s in class %s overrides an editor-only parent function, but is not in editor-only code."),
	*FunctionDesc->FunctionName, *ClassData.NewClass->ClassName));
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 位置: 761-829, 980-1014
// 说明: current 仍保留同级别的 override 验证矩阵，只是 owner 从 Manager 转成 Engine
// ============================================================================
if (!SuperFunctionDesc.IsValid())
{
	FAngelscriptEngine::Get().ScriptCompileError(ModuleData.NewModule, FunctionDesc->LineNumber, FString::Printf(
		TEXT("BlueprintOverride method %s in class %s does not exist in superclass %s."),
		*FunctionDesc->FunctionName, *ClassData.NewClass->ClassName, *AngelscriptSuperClass->ClassName));
	ClassData.ReloadReq = EReloadRequirement::Error;
}
...
else if (!SuperFunctionDesc->SignatureMatches(FunctionDesc))
{
	FAngelscriptEngine::Get().ScriptCompileError(ModuleData.NewModule, FunctionDesc->LineNumber, FString::Printf(
		TEXT("BlueprintOverride method %s in class %s does not match signature of event declared in superclass %s."),
		*FunctionDesc->FunctionName, *ClassData.NewClass->ClassName, *AngelscriptSuperClass->ClassName));
	ClassData.ReloadReq = EReloadRequirement::Error;
}
...
FAngelscriptEngine::Get().ScriptCompileError(ModuleData.NewModule, FunctionDesc->LineNumber, FString::Printf(
	TEXT("BlueprintOverride method %s in class %s does not match function signature of event in superclass %s.\nExpected Signature: %s"),
	*FunctionDesc->FunctionName, *ClassData.NewClass->ClassName, *CodeSuperClass->GetName(), *ExpectedSignature));
...
FAngelscriptEngine::Get().ScriptCompileError(ModuleData.NewModule, FunctionDesc->LineNumber, FString::Printf(
	TEXT("BlueprintOverride method %s in class %s overrides an editor-only parent function, but is not in editor-only code."),
	*FunctionDesc->FunctionName, *ClassData.NewClass->ClassName));
```

[3] current 还把这条链补成了显式自动化，不只依赖源码同构。这里至少有三层证据：preprocessor 夹具、Blueprint 子类运行时用例、以及学习型 trace 用例。

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp
// 位置: 128-160
// 说明: 测试夹具直接把 UFUNCTION(BlueprintOverride) 写进脚本源，再断言宏被记录
// ============================================================================
const FString AbsoluteScriptPath = WriteFixtureFile(
	RelativeScriptPath,
	TEXT("class AMacroActor : AActor\n")
	TEXT("{\n")
	TEXT("    UPROPERTY(EditAnywhere, BlueprintReadWrite)\n")
	TEXT("    UStaticMesh Mesh;\n\n")
	TEXT("    UFUNCTION(BlueprintOverride)\n")
	TEXT("    void BeginPlay()\n")
	TEXT("    {\n")
	TEXT("    }\n")
	TEXT("}\n"));
...
const bool bHasFunctionMacro = Macros.ContainsByPredicate([](const FAngelscriptPreprocessor::FMacro* Macro)
{
	return Macro->Type == FAngelscriptPreprocessor::EMacroType::Function && Macro->Name == TEXT("BeginPlay");
});
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Blueprint/AngelscriptBlueprintSubclassRuntimeTests.cpp
// 位置: 222-275
// 说明: current 直接验证“脚本父类定义 BlueprintOverride -> Blueprint 子类继承后仍执行”
// ============================================================================
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
...
BeginPlayActor(Engine, *Actor);
...
TestEqual(TEXT("Blueprint child should inherit and execute the script BeginPlay override"), BeginPlayCount, 1);
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningScriptClassToBlueprintTraceTests.cpp
// 位置: 167-225
// 说明: 学习型 trace 用例把这条继承链分阶段记录出来，便于回归时追路径
// ============================================================================
Trace.AddStep(TEXT("CompileScriptClass"), TEXT("Compiled the script parent class with BlueprintOverride so Unreal reflection can generate a UClass that Blueprint can inherit from"));
...
Trace.AddStep(TEXT("CreateBlueprintChild"), TEXT("Created a transient Blueprint asset that inherits from the generated script class"));
...
Trace.AddStep(TEXT("CompileBlueprintChild"), TEXT("Compiled the Blueprint asset into a generated Blueprint class that preserves the script parent hierarchy"));
...
Trace.AddStep(TEXT("InvokeBeginPlay"), TEXT("Invoked BeginPlay on the spawned actor to trigger the script-defined BlueprintOverride"));
...
Trace.AddStep(TEXT("ReadScriptPropertyDefaults"), bReadBeginPlayCount && bReadActorLabel ? TEXT("Read reflected properties from the Blueprint actor instance to show that script defaults propagated through the Blueprint inheritance chain") : TEXT("Failed to read one or more reflected properties from the Blueprint actor"));
```

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| script-side `BlueprintOverride` 语法 | preprocessor 正式识别 | 仍由 preprocessor 正式识别 | **没有实现差距** |
| override 合法性验证 | 存在完整编译期校验矩阵 | 同级别校验矩阵仍在 | **没有实现差距** |
| Blueprint 子类继承脚本 override | 设计上支持 | current 还用运行时/learning 测试显式锁定 | **实现质量增强** |
| wrapper header authoring contract | 更依赖引擎补丁 specifier | 有部分 surface 转向 `BlueprintCallable` | **实现方式不同，但不等于 script-side override 缺失** |

这一节最重要的新结论是：**不能把 current 某些 wrapper header 的 authoring shift，误投射成 `BlueprintOverride` 主链退化。** 从源码看，script 侧 override contract 仍然完整存在，而且 current 在验证闭环上比源码树可见的 UEAS2 更硬。

### [维度 D5] `StopPIE` 是尾部 opcode 缺口，不是整条 `DebugServer V2` opcode 表漂移

前文已经写过 current 少 `StopPIE`。本轮往更细的 wire 级别下钻后，可以把这件事说得更准确：**current 缺的是一个“尾部 opcode”，而不是把整张 `EDebugMessageType` 表重新编号。**

这点很关键。因为 UEAS2 的 `StopPIE` 就是枚举最后一项；current 只是在尾部截断，没有在中间删项。因此 `Diagnostics -> ClearDataBreakpoints` 这一整段 opcode 的数值顺序仍然保持一致。  
真正的问题在于：current 的 envelope helper 按原样读写 `uint8 MessageType`，但不会验证这个 byte 是否映射到已知枚举；而消息分发链又没有 `StopPIE` 分支，也没有默认报错。结果就是: **旧客户端如果发 `StopPIE`，current 会成功收包、消费掉 envelope，然后静默不做任何事。**

```
[D5] StopPIE Wire Compatibility
UEAS2
├─ enum tail includes StopPIE                  // 最后一个 opcode
├─ receive StopPIE
└─ RequestEndPlayMap()                         // PIE 结束

current
├─ enum tail ends at ClearDataBreakpoints      // 之前的 opcode 序号不漂移
├─ deserialize raw uint8 -> enum cast
├─ no StopPIE branch and no default error
└─ envelope consumed, PIE unchanged            // 旧客户端会静默失效
```

[1] 两边的 `EDebugMessageType` 直到 `ClearDataBreakpoints` 都保持同一顺序；UEAS2 只是在尾部多了一个 `StopPIE`。

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Debugging\AngelscriptDebugServer.h
// 位置: 23-80
// 说明: UEAS2 的 StopPIE 位于枚举尾部，不会影响前面 opcode 的编号
// ============================================================================
enum class EDebugMessageType : uint8
{
	Diagnostics,
	RequestDebugDatabase,
	DebugDatabase,
	...
	ReplaceAssetDefinition,

	SetDataBreakpoints,
	ClearDataBreakpoints,

	StopPIE,
};
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h
// 位置: 25-80
// 说明: current 只截掉尾部 StopPIE，之前的 opcode 列表没有重排
// ============================================================================
enum class EDebugMessageType : uint8
{
	Diagnostics,
	RequestDebugDatabase,
	DebugDatabase,
	...
	ReplaceAssetDefinition,

	SetDataBreakpoints,
	ClearDataBreakpoints,
};
```

[2] UEAS2 在消息分发里确实实现了 `StopPIE -> RequestEndPlayMap()`；current 则没有同位分支。

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Debugging\AngelscriptDebugServer.cpp
// 位置: 1052-1064
// 说明: UEAS2 明确把 StopPIE 映射到 editor RequestEndPlayMap
// ============================================================================
else if (MessageType == EDebugMessageType::StopPIE)
{
	bIsPaused = false;
	bBreakNextScriptLine = false;

	ConditionBreakFrame = -1;
	ConditionBreakFunction = nullptr;

#if WITH_EDITOR
	GEditor->RequestEndPlayMap();
#endif

	FAngelscriptManager::Get().UpdateLineCallbackState();
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 位置: 1161-1195
// 说明: current 的分发链在 FindAssets / CreateBlueprint / Disconnect 后直接结束，没有 StopPIE 或默认报错分支
// ============================================================================
else if (MessageType == EDebugMessageType::FindAssets)
{
	...
}
else if (MessageType == EDebugMessageType::CreateBlueprint)
{
	...
}
else if (MessageType == EDebugMessageType::Disconnect)
{
	Client->Close();
}
}
```

[3] current 的 transport helper 会原样序列化 / 反序列化 `uint8 MessageType`，但不会检查它是不是当前版本已知的 opcode。也就是说，老客户端发来的 `StopPIE` 会先通过 framing。

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 位置: 52-60, 96-107
// 说明: framing 只负责长度和类型字节，不负责验证 opcode 是否在当前 enum 中有定义
// ============================================================================
bool SerializeDebugMessageEnvelope(EDebugMessageType MessageType, const TArray<uint8>& Body, TArray<uint8>& OutBuffer)
{
	OutBuffer.Reset();
	FMemoryWriter Writer(OutBuffer);
	const int32 MessageLength = static_cast<int32>(sizeof(uint8)) + Body.Num();
	const uint8 MessageTypeByte = static_cast<uint8>(MessageType);
	Writer << const_cast<int32&>(MessageLength);
	Writer << const_cast<uint8&>(MessageTypeByte);
	OutBuffer.Append(Body);
	return true;
}
...
uint8 MessageTypeByte = static_cast<uint8>(EDebugMessageType::Disconnect);
PayloadReader << MessageTypeByte;

OutEnvelope.MessageType = static_cast<EDebugMessageType>(MessageTypeByte);
...
InOutBuffer.RemoveAt(0, TotalEnvelopeSize, EAllowShrinking::No);
bOutHasEnvelope = true;
return true;
```

[4] current 的自动化也主要覆盖 framing 和已知 message round-trip，没有 `StopPIE` 或 unknown-opcode 回归。这个判断来自测试入口列表本身。

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugTransportTests.cpp
// 位置: 18-41
// 说明: transport 只回归 Single/Multiple/Truncated/InvalidLength/EmptyBody 五类 framing
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptDebugTransportSingleEnvelopeTest,
	"Angelscript.CppTests.Debug.Transport.SingleEnvelope",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
...
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptDebugTransportEmptyBodyEnvelopeTest,
	"Angelscript.CppTests.Debug.Transport.EmptyBodyEnvelope",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp
// 位置: 39-77
// 说明: protocol round-trip 覆盖 StartDebugging / DebugServerVersion / Breakpoint / Variables / DataBreakpoints / BreakFilters / DatabaseSettings，但没有 StopPIE
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptDebugProtocolStartDebuggingRoundTripTest,
	"Angelscript.CppTests.Debug.Protocol.StartDebugging.RoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
...
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptDebugProtocolDatabaseSettingsRoundTripTest,
	"Angelscript.CppTests.Debug.Protocol.DatabaseSettings.RoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
```

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| `Diagnostics` 到 `ClearDataBreakpoints` 的 opcode 顺序 | 保持既有次序 | 保持同一顺序 | **没有 wire-order 差距** |
| `StopPIE` opcode | 枚举尾部存在，且有处理分支 | 不存在 | **没有实现** |
| 收到未知尾部 opcode 的行为 | `StopPIE` 被显式处理 | envelope 会被消费，但无处理、无报错 | **实现质量差异 / 诊断缺口** |
| 自动化覆盖 | 本轮未展开上游测试 | current 覆盖 framing / 已知 round-trip，但未覆盖 `StopPIE` 缺口 | **验证闭环偏弱** |

因此，D5 这一轮最值得记住的新结论不是“current 改坏了整个 V2 协议”，而是更细的这一句：**current 只在尾部少了 `StopPIE`，所以大部分既有 opcode 仍然数值兼容；真正的风险是老客户端的 `StopPIE` 会静默失效，而不是显式报不兼容。**

---

## 深化分析 (2026-04-09 06:49:24)

### [维度 D2/D6/D11] stock UHT 下真正断开的不是 `ScriptMixin`，而是 `UFUNCTION(ScriptCallable)` 的自动链

前文多次追 `ScriptMixin` 覆盖面，但把 UHT、wrapper header 和当前绑定入口重新串起来后，可以把边界说得更准：

1. `ScriptMixin` 这种 `UCLASS(meta = (...))` 级别的信息，stock UHT 仍然能保留下来，所以 current 里仍能看到激活状态的类级 mixin。
2. `ScriptCallable` 这种函数级 specifier，只有 UEAS2 的 UHT 补丁认识。current 运行时仍然会读取 `MetaData["ScriptCallable"]`，但 stock UHT 已经不会替 header 生成这条 metadata，于是 current 源码里大量出现“保留注释、改写成 `BlueprintCallable` 或 `UFUNCTION()`”的 wrapper。
3. 因此，current 与 UEAS2 在这一点上的核心差异不是“有没有 mixin”，而是 **函数进入脚本自动绑定链的 authoring contract 已经从 `ScriptCallable` 优先，改成了 `BlueprintCallable/Pure` 优先 + 其他插件内补偿路径。**

```
[D2/D6/D11] Wrapper Authoring Contract
UEAS2
├─ UCLASS(meta = ScriptMixin)                     // 类级 mixin
├─ UFUNCTION(ScriptCallable)                      // 函数级脚本入口
├─ UHT custom specifier -> MetaData["ScriptCallable"]
└─ Bind_BlueprintType auto-binds ScriptCallable

current
├─ class-level ScriptMixin can still survive      // stock UHT 可保留普通 metadata
│  └─ example: UAngelscriptActorLibrary
├─ function-level ScriptCallable cannot be emitted by stock UHT
│  ├─ some wrappers rewrite to BlueprintCallable
│  └─ many legacy ScriptCallable lines remain as comments
└─ auto-bind surface becomes BlueprintCallable/Pure-first
```

[1] UEAS2 直接把 `ScriptCallable` 注册成 UHT 函数 specifier；current 的运行时绑定入口也仍然支持读取同名 metadata。也就是说，current 缺的不是 runtime 解释器，而是 **header -> metadata** 这一步的 stock-UHT 生成权。

```csharp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Programs\Shared\EpicGames.UHT\Specifiers\UhtFunctionSpecifiers.cs
// 位置: 23-29
// 说明: UEAS2 的 UHT 主链直接认识 ScriptCallable，并把它写成函数 metadata
// ============================================================================
// AS FIX (LV): Angelscript support
[UhtSpecifier(Extends = UhtTableNames.Function, ValueType = UhtSpecifierValueType.Legacy)]
private static void ScriptCallableSpecifier(UhtSpecifierContext specifierContext)
{
	UhtFunction function = (UhtFunction)specifierContext.Type;
	// ★ 这一步是 current 在 stock UHT 下拿不到的 authoring 能力
	function.MetaData.Add("ScriptCallable", "");
}
// END AS FIX
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp
// 位置: 1303-1314
// 说明: current 运行时仍保留 ScriptCallable metadata 分支，说明 runtime contract 并未删除
// ============================================================================
void BindFunctionWithAdditionalName(TSharedRef<FAngelscriptType> InType, UFunction* Function, FString TargetName, FAngelscriptMethodBind& DBMethod)
{
	if (Function->HasAnyFunctionFlags(FUNC_BlueprintEvent | FUNC_NetFuncFlags))
		BindBlueprintEvent(InType, Function, DBMethod, *TargetName);
	else if (Function->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintPure))
		BindBlueprintCallable(InType, Function, DBMethod, *TargetName);
	else if (Function->HasMetaData(NAME_ScriptCallable))
		// ★ 只要 metadata 真存在，current 仍会走 callable 绑定
		BindBlueprintCallable(InType, Function, DBMethod, *TargetName);
}
```

[2] 但 current 的 wrapper header 已经明显分裂成两种写法。`AActor` 这一类还能保留类级 `ScriptMixin`，而 `InputComponent` 这类 wrapper 已经把 `ScriptMixin` 和 `ScriptCallable` 都退回注释，并改写成 stock-UHT 认可的 `BlueprintCallable`。

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Public\FunctionLibraries\InputComponentScriptMixinLibrary.h
// 位置: 12-23
// 说明: UEAS2 的 Input wrapper 仍是完整的 class-level ScriptMixin + function-level ScriptCallable
// ============================================================================
UCLASS(Meta = (ScriptMixin = "UInputComponent"))
class UInputComponentScriptMixinLibrary : public UObject
{
	GENERATED_BODY()
public:
	UFUNCTION(ScriptCallable)
	static void BindAction(UInputComponent* Component, const FName& ActionName, EInputEvent KeyEvent, const FInputActionHandlerDynamicSignature& Delegate)
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/InputComponentScriptMixinLibrary.h
// 位置: 12-25
// 说明: current 把同一组 wrapper 改写成 stock-UHT 友好的写法
// ============================================================================
//UCLASS(Meta = (ScriptMixin = "UInputComponent"))
UCLASS(Meta = ())
class UInputComponentScriptMixinLibrary : public UObject
{
	GENERATED_BODY()
public:
	//UFUNCTION(ScriptCallable)
	UFUNCTION(BlueprintCallable)
	static void BindAction(UInputComponent* Component, const FName& ActionName, EInputEvent KeyEvent, const FInputActionHandlerDynamicSignature& Delegate)
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptActorLibrary.h
// 位置: 5-14, 74-80
// 说明: current 不是把 ScriptMixin 全砍了，而是保留 class metadata，同时把函数入口改写成其他形态
// ============================================================================
UCLASS(meta = (ScriptMixin = "AActor"))
class UAngelscriptActorLibrary : public UObject
{
	GENERATED_BODY()
public:
	//UFUNCTION(ScriptCallable)
	UFUNCTION()
	static void SetActorRelativeLocation(AActor* Actor, const FVector& NewRelativeLocation)
	...
	UFUNCTION(BlueprintCallable, Meta = ())
	static FVector GetActorLocation(const AActor* Actor)
```

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| class-level `ScriptMixin` | UHT 原样保留 | stock UHT 下仍可保留 | **没有实现缺口** |
| function-level `ScriptCallable` authoring | UHT 直接识别 specifier | runtime 仍支持 metadata，但 stock header 不再自动产生它 | **实现方式不同** |
| wrapper 暴露入口 | `ScriptCallable` 一条主链 | `BlueprintCallable/Pure` + 手写 bind / 其他插件内路径 | **实现方式不同，authoring 统一性收窄** |

这一节最重要的新结论是：**current 在 D2/D6/D11 上真正变化的是“函数 authoring contract”，不是“mixin/runtime callable 语义整体消失”。**

### [维度 D6/D11] `AngelscriptUHTTool` 改变的是交付 owner，不是把 UEAS2 的 engine fork 平移成第四个 runtime 模块

如果只看“current 新增了 `AngelscriptUHTTool`”，很容易误判成“只是把上游 UHT 能力换了个模块名”。源码并不支持这个说法。真正发生的是：

1. current 的 `.uplugin` 只加载 `Runtime / Editor / Test` 三个模块，`AngelscriptUHTTool` 并不作为运行时模块参与插件加载。
2. `AngelscriptUHTTool` 是一个 **运行在 UHT 会话内部的 exporter**，它生成 `AS_FunctionTable_*.cpp` 编译产物，而不是在引擎主干里扩展 `FProperty` / `UFunction` ABI。
3. 因此，current 的交付边界确实更接近“可独立插件化”，但代价是 auto-export 的能力边界要重新定义，不能再等同于 UEAS2 的 engine-mainline 集成深度。

```
[D6/D11] Delivery Boundary
UEAS2
├─ Engine fork
│  ├─ UHT parser/specifier changes
│  ├─ generated code writes AngelscriptPropertyFlags
│  ├─ FProperty layout grows new field
│  └─ editor/runtime read APF_RuntimeGenerated directly
└─ plugin assumes patched engine exists

current
├─ Plugins/Angelscript/Angelscript.uplugin
│  └─ loads Runtime / Editor / Test only
├─ AngelscriptUHTTool
│  ├─ runs inside UHT session as exporter
│  ├─ emits AS_FunctionTable_*.cpp compile outputs
│  └─ does not modify engine ABI
└─ runtime/editor consume generated tables inside plugin boundary
```

[1] current 的 `.uplugin` 没有把 `AngelscriptUHTTool` 放进模块清单；真正的 build-time 入口是 `UhtExporter` attribute。

```json
// ============================================================================
// 文件: Plugins/Angelscript/Angelscript.uplugin
// 位置: 18-33
// 说明: current 的可加载模块只有 Runtime / Editor / Test
// ============================================================================
"Modules": [
	{
		"Name": "AngelscriptRuntime",
		"Type": "Runtime",
		"LoadingPhase": "PostDefault"
	},
	{
		"Name": "AngelscriptEditor",
		"Type": "Editor",
		"LoadingPhase": "PostDefault"
	},
	{
		"Name": "AngelscriptTest",
		"Type": "Editor",
		"LoadingPhase": "PostDefault"
	}
]
```

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs
// 位置: 12-27
// 说明: AngelscriptUHTTool 是 UHT exporter，不是 runtime module
// ============================================================================
[UnrealHeaderTool]
internal static class AngelscriptFunctionTableExporter
{
	[UhtExporter(
		Name = "AngelscriptFunctionTable",
		Description = "Exports Angelscript function table data",
		Options = UhtExporterOptions.Default | UhtExporterOptions.CompileOutput,
		CppFilters = ["AS_FunctionTable_*.cpp"],
		// ★ 输出 owner 指向 AngelscriptRuntime 的编译产物，而不是改写引擎主干类布局
		ModuleName = "AngelscriptRuntime")]
	private static void Export(IUhtExportFactory factory)
```

[2] current 的 UHT exporter 不是从 engine patch 里拿现成的 `AngelscriptPropertyFlags`，而是先尝试从 header 文本恢复声明，再退回 UHT type text 拼装 `ERASE_*` 宏。这说明它本质上是 **构建期重建器**，不是 ABI 级主链能力。

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs
// 位置: 18-39, 49-67, 75-106
// 说明: 先读 header 文本定位声明；失败时再交给 builder 做退化重建
// ============================================================================
public static bool TryBuild(UhtClass classObj, UhtFunction function, out AngelscriptFunctionSignature? signature, out string? failureReason)
{
	signature = null;

	if (classObj.HeaderFile == null || string.IsNullOrEmpty(classObj.HeaderFile.FilePath) || !File.Exists(classObj.HeaderFile.FilePath))
	{
		failureReason = "header-missing";
		return false;
	}
	...
	if (candidates.Count == 1 && publicCandidates.Count == 1)
	{
		...
		signature = new AngelscriptFunctionSignature(..., false);
		return true;
	}
	...
	failureReason = matchedUnexportedSymbol ? "unexported-symbol" : "overloaded-unresolved";
	return false;
}
```

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs
// 位置: 43-100
// 说明: header 解析不成时，再用 UHT 元数据重建显式签名
// ============================================================================
public static bool TryBuild(UhtClass classObj, UhtFunction function, out AngelscriptFunctionSignature? signature, out string? failureReason)
{
	signature = null;

	if (AngelscriptHeaderSignatureResolver.TryBuild(classObj, function, out signature, out failureReason))
		return true;

	if (failureReason == "non-public" || failureReason == "unexported-symbol")
		return false;
	...
	signature = new AngelscriptFunctionSignature(
		classObj.SourceName,
		function.SourceName,
		returnType,
		parameterTypes,
		HasFunctionFlag(function, "Static"),
		HasFunctionFlag(function, "Const"),
		true);
}
```

[3] 相比之下，UEAS2 的能力是把 Angelscript 语义真的写进引擎主干对象布局与 editor 消费路径。`FProperty` 直接多出 `AngelscriptPropertyFlags` 字段，`PropertyEditor` 也直接按 `APF_RuntimeGenerated` 改节点行为。这不是 exporter 能等价替代的层级。

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Runtime\CoreUObject\Public\UObject\UnrealType.h
// 位置: 185-189
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Runtime\CoreUObject\Public\UObject\ObjectMacros.h
// 位置: 482-491
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Editor\PropertyEditor\Private\PropertyNode.cpp
// 位置: 440-441
// 说明: UEAS2 的 owner 是引擎 ABI + editor 主干，不是插件 build step
// ============================================================================
EPropertyFlags PropertyFlags;
uint16 RepIndex;
uint16 AngelscriptPropertyFlags;
...
enum EAngelscriptPropertyFlags : uint16
{
	APF_CppConst = 0x0001,
	APF_CppRef = 0x0002,
	APF_CppEnumAsByte = 0x0004,
	APF_RuntimeGenerated = 0x0008,
	APF_WorldContext = 0x0010,
};
...
SetNodeFlags(EPropertyNodeFlags::NoCacheAddress, MyProperty && (MyProperty->AngelscriptPropertyFlags & APF_RuntimeGenerated));
```

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| 交付 owner | engine fork + plugin | stock engine + plugin + build-time exporter | **实现方式不同** |
| `AngelscriptUHTTool` 是否等价于引擎补丁 | 不适用 | 只是一条 build-time 重建链 | **不是等价替代** |
| engine/editor 默认理解 runtime-generated property | 主干直接理解 `APF_RuntimeGenerated` | 未见 stock engine 主干等价字段/分支 | **局部没有实现主干同等能力** |

### [维度 D2/D6] current 的自动导出面已经明确收窄到 `BlueprintCallable/Pure`；`ScriptCallable`-only helper 不再共用同一条流水线

把上面两节合起来后，还能得到一个前文没有明确钉死的新结论：**current 的 auto-export 覆盖面不是“所有脚本可调用 wrapper”，而是“所有 stock-UHT 可识别为 `BlueprintCallable/Pure` 的 wrapper”。** 这意味着 UEAS2 那批“只靠 `ScriptCallable` 进入脚本面、并不要求 Blueprint 可见”的 helper，如今已经不再和 `BlueprintCallable` 函数共用同一条生成链。

```
[D2/D6] Function Exposure Partition In current
BlueprintCallable / BlueprintPure
├─ stock UHT marks function flags
├─ AngelscriptUHTTool scans function
└─ AS_FunctionTable_*.cpp auto-export path

ScriptCallable-only metadata
├─ runtime Bind_BlueprintType still understands metadata
└─ but stock wrapper headers rarely emit it automatically

legacy helper wrappers
└─ comments + manual bind / other plugin-local paths
```

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs
// 位置: 490-514
// 说明: current 的自动导出入口只看 BlueprintCallable/Pure
// ============================================================================
private static bool ShouldGenerate(UhtClass classObj, UhtFunction function)
{
	if (classObj.HeaderFile == null || !IsSupportedHeader(classObj.HeaderFile.FilePath))
		return false;

	if (!AngelscriptFunctionTableExporter.IsBlueprintCallable(function))
	{
		// ★ 不是 BlueprintCallable/Pure，直接不进函数表生成链
		return false;
	}

	if (function.MetaData.ContainsKey("NotInAngelscript") ||
		(function.MetaData.ContainsKey("BlueprintInternalUseOnly") && !function.MetaData.ContainsKey("UsableInAngelscript")))
	{
		return false;
	}

	return !function.FunctionExportFlags.ToString().Contains("CustomThunk", StringComparison.Ordinal);
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp
// 位置: 1311-1314
// 说明: runtime 还能吃 ScriptCallable metadata，但这和 UHTTool 的生成面已经不是同一 owner
// ============================================================================
else if (Function->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintPure))
	BindBlueprintCallable(InType, Function, DBMethod, *TargetName);
else if (Function->HasMetaData(NAME_ScriptCallable))
	// ★ 运行时能吃，不代表 build-time exporter 也会生成这类条目
	BindBlueprintCallable(InType, Function, DBMethod, *TargetName);
```

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| non-Blueprint `ScriptCallable` helper 的统一入口 | 可直接靠 UHT specifier 进入主绑定链 | 运行时能识别 metadata，但 auto-export 只覆盖 BlueprintCallable/Pure | **实现方式不同，覆盖面分流** |
| “脚本可调用”与“Blueprint 可调用”是否仍是同一集合 | 更接近同一集合 | 已明显分裂成不同 owner | **实现质量差异 / authoring 成本上升** |

这一轮最值得记住的补充结论是：**current 的插件化胜利，部分是靠把“脚本可调用 surface”重新约束到 stock UHT 能理解的函数集合换来的。** 这不是简单的“缺失实现”，而是一次明确的 owner 重划分：交付更干净，但 wrapper authoring 不再像 UEAS2 那样一条 specifier 贯通到底。

---

## 深化分析 (2026-04-09 07:02:14)

### [维度 D3/D8/D11] `RuntimeGenerated` 调用 owner 在 current 中已经缩到 `UASFunction + NativeThunk + 局部 ProcessEvent`

前文已经多次讨论 `UHT`、`FunctionTable` 和 `NativeThunk`，但还没有把**真正执行脚本 `UFunction` 时，调用权到底挂在引擎主干还是插件本地**这件事钉死。把 UEAS2 的 `CoreUObject` 补丁和 current 的 `ClassGenerator` / `UASFunction` 串起来后，可以更准确地说：

1. UEAS2 不是只在插件里造了一个 `UASFunction`，它把 `RuntimeCallFunction()` / `RuntimeCallEvent()` / `GetRuntimeValidateFunction()` 直接补进了 `UFunction` 基类，再让 `ScriptCore.cpp` 在看到 `FUNC_RuntimeGenerated` 时走这条主干分发。
2. current 的 `AngelscriptClassGenerator.cpp` 在同一位置把 `FUNC_RuntimeGenerated` 明确注释掉，改成 `FUNC_Native + SetNativeFunc(&UASFunctionNativeThunk)`；也就是说，**current 的脚本 `UFunction` 对 stock UE 来说更像“插件伪装出来的 native thunk”**。
3. 因为没有引擎主干的 `FUNC_RuntimeGenerated` owner，current 必须把 `_Validate` RPC 之类的运行时语义挪到插件自己控制的调用点，例如 `UAngelscriptComponent::ProcessEvent()`。

```
[D3/D8/D11] Runtime Function Dispatch Owner
UEAS2
├─ ClassGenerator -> NewFunction |= FUNC_RuntimeGenerated
├─ UFunction base owns RuntimeCallFunction / RuntimeCallEvent
├─ ScriptCore checks FUNC_RuntimeGenerated
│  └─ Fn->RuntimeCallFunction(Context, Stack, Result)
└─ engine-mainline callers get script dispatch by default

current
├─ ClassGenerator -> // FUNC_RuntimeGenerated
├─ NewFunction |= FUNC_Native
├─ NewFunction->SetNativeFunc(UASFunctionNativeThunk)
├─ UASFunction owns local RuntimeCallFunction / RuntimeCallEvent
│  ├─ BPVM path -> AngelscriptCallFromBPVM(...)
│  └─ ProcessEvent path -> AngelscriptCallFromParms(...)
└─ plugin-owned callsites special-case script semantics
   └─ example: UAngelscriptComponent::ProcessEvent() handles _Validate
```

[1] UEAS2 把调用入口挂到 `UFunction` 基类和 `ScriptCore` 主干，而不是只留在插件内部：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Runtime\CoreUObject\Public\UObject\Class.h
// 位置: 2606-2609
// 说明: UEAS2 直接扩展 UFunction 基类虚接口
// ============================================================================
// AS FIX (LV): Allow calling runtime generated functions directly through UFunction
virtual void RuntimeCallFunction(UObject* Object, FFrame& Stack, RESULT_DECL) { ensureAlways(false); }
virtual void RuntimeCallEvent(UObject* Object, void* Params) { ensureAlways(false); }
virtual UFunction* GetRuntimeValidateFunction() { ensureAlways(false); return nullptr; }
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Runtime\CoreUObject\Private\UObject\ScriptCore.cpp
// 位置: 1114-1119, 2107-2114
// 说明: 主干执行器直接把 RuntimeGenerated 函数当成一等调用目标
// ============================================================================
// HAZE FIX(LV): Angelscript function calls
if (Function->FunctionFlags & (FUNC_Native | FUNC_RuntimeGenerated))
{
	// ★ 一旦命中 FUNC_RuntimeGenerated，就进入主干 native/runtime 调用分支
	...
}

if (Function->FunctionFlags & FUNC_RuntimeGenerated)
{
	// ★ RPC _Validate 也由主干通过 UFunction 虚接口回调
	if (Function->FunctionFlags & FUNC_NetValidate)
	{
		UFunction* ValidateFunction = Function->GetRuntimeValidateFunction();
```

[2] UEAS2 的类生成器在生成脚本 `UFunction` 时，明确把 flag 打成 `FUNC_RuntimeGenerated`：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\ClassGenerator\AngelscriptClassGenerator.cpp
// 位置: 3259-3264
// 说明: 上游把脚本函数注册成 runtime-generated，而不是伪装成 native thunk
// ============================================================================
auto* NewFunction = UASFunction::AllocateFunctionFor(NewClass, FunctionName, FunctionDesc);
NewFunction->SetSuperStruct(ParentFunction);
NewFunction->ReturnValueOffset = MAX_uint16;
NewFunction->FirstPropertyToInit = NULL;
NewFunction->FunctionFlags |= FUNC_RuntimeGenerated;
NewFunction->ScriptFunction = FunctionDesc->ScriptFunction;
```

[3] current 在同位点把 `FUNC_RuntimeGenerated` 注释掉，并改挂 `UASFunctionNativeThunk`：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 位置: 3410-3429
// 说明: current 不再依赖引擎主干 runtime-generated flag，而是走 native thunk
// ============================================================================
auto* NewFunction = UASFunction::AllocateFunctionFor(NewClass, FunctionName, FunctionDesc);
NewFunction->SetSuperStruct(ParentFunction);
NewFunction->ReturnValueOffset = MAX_uint16;
NewFunction->FirstPropertyToInit = NULL;
//NewFunction->FunctionFlags |= FUNC_RuntimeGenerated;
NewFunction->ScriptFunction = FunctionDesc->ScriptFunction;
...
NewFunction->FunctionFlags |= FUNC_Native;
NewFunction->SetNativeFunc(&UASFunctionNativeThunk);
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h
// 位置: 204-207
// 说明: current 的 RuntimeCall* 接口属于 UASFunction 自己，不是 UFunction 基类 contract
// ============================================================================
//WILL-EDIT
virtual void RuntimeCallFunction(UObject* Object, FFrame& Stack, RESULT_DECL);
virtual void RuntimeCallEvent(UObject* Object, void* Parms);
virtual UFunction* GetRuntimeValidateFunction();
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp
// 位置: 1931-1958
// 说明: NativeThunk 只是跳板，真正逻辑仍在 UASFunction 本地
// ============================================================================
void UASFunction::RuntimeCallFunction(UObject* Object, FFrame& Stack, RESULT_DECL)
{
	// ★ BPVM 进入 Angelscript VM 的本地桥
	AngelscriptCallFromBPVM<true, false>(this, Object, Stack, RESULT_PARAM);
}

void UASFunctionNativeThunk(UObject* Object, FFrame& Stack, RESULT_DECL)
{
	UASFunction* Function = Cast<UASFunction>(Stack.Node);
	check(Function != nullptr);
	Function->RuntimeCallFunction(Object, Stack, RESULT_PARAM);
}

UFunction* UASFunction::GetRuntimeValidateFunction()
{
	return ValidateFunction;
}
```

[4] current 还把 UEAS2 原本位于 `ScriptCore.cpp` 的 `_Validate` 分支，局部搬到了插件组件的 `ProcessEvent()`：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptComponent.cpp
// 位置: 38-49, 89-109
// 说明: current 的 RPC 校验不是引擎主干默认行为，而是插件局部 override
// ============================================================================
void UAngelscriptComponent::ProcessEvent(UFunction* Function, void* Parameters)
{
	Super::ProcessEvent(Function, Parameters);

	UASFunction* ASFunction = (UASFunction*)Function;
	if (ASFunction != nullptr)
	{
		if (Function->FunctionFlags & FUNC_NetValidate)
		{
			UFunction* ValidateFunction = ASFunction->GetRuntimeValidateFunction();
			...
			// ★ 手动执行 Angelscript _Validate
			ASValidate->RuntimeCallEvent(this, ValidateFunctionParmsPtr);
			...
			ASFunction->RuntimeCallEvent(this, Parameters);
		}
		else
		{
			ASFunction->RuntimeCallEvent(this, Parameters);
		}
	}
}
```

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| Blueprint VM 调到脚本 `UFunction` | `FUNC_RuntimeGenerated -> ScriptCore -> RuntimeCallFunction()` | `FUNC_Native -> UASFunctionNativeThunk -> RuntimeCallFunction()` | **实现方式不同** |
| `_Validate` RPC 的默认 owner | `CoreUObject::ScriptCore.cpp` 主干 | `UAngelscriptComponent::ProcessEvent()` 局部 override | **实现方式不同，owner 收窄** |
| 所有引擎调用点是否天然认识脚本函数 | 是，主干 flag/virtual 已接通 | 否，需要走插件自己控制的 thunk / override | **局部没有实现主干同等 owner** |

这一节最重要的新结论是：**current 没有丢掉“脚本函数可执行”和“脚本 RPC 可校验”这两类能力，丢掉的是 UEAS2 那层“引擎任意调用点天然认识脚本函数”的主干 owner。**

### [维度 D5/D7/D11] editor affordance 在 current 中变成“显式 handler + reload helper”，不再是引擎 widget 的默认认知

前文已经写过 `GetSourceFilePath()` / `GetSourceLineNumber()` 这类源码定位 metadata；这一轮补的是**这些 metadata 最终由谁接到 editor 行为上**。UEAS2 的做法是直接补引擎 editor 主干，让 Blueprint 节点、MyBlueprint 面板、PropertyEditor、KismetCompiler 都默认理解 Angelscript 产物；current 则把同类能力拆成两个插件侧服务：

1. `FAngelscriptSourceCodeNavigation`：给 `FSourceCodeNavigation` 注册 handler，把 `UASFunction` 导到 `.as` 文件。
2. `FClassReloadHelper`：在 reload 后扫描依赖节点、重构 Blueprint node、刷新 PropertyEditor，并强制 recompile/reinstance。

这两条链解决的是“能跳转”“reload 后节点别脏掉”两个最痛的问题，但**它们不是 UEAS2 那种 engine-widget 默认认知**。像 `SMyBlueprint` 属性显示名、`PropertyNode` 的 `NoCacheAddress` 这种细粒度 affordance，在 current 里本轮没看到同位引擎 patch。

```
[D5/D7/D11] Editor Affordance Owner
UEAS2
├─ K2Node_CallFunction -> JumpToDefinition knows FUNC_RuntimeGenerated
├─ KismetCompilerMisc -> event owner mismatch accepts runtime-generated
├─ SMyBlueprint -> runtime-generated property uses DisplayNameText
└─ PropertyNode -> APF_RuntimeGenerated forces NoCacheAddress

current
├─ AngelscriptEditorModule::StartupModule()
│  ├─ FClassReloadHelper::Init()
│  └─ RegisterAngelscriptSourceNavigation()
├─ SourceNavigation handler
│  └─ UASFunction::GetSourceFilePath() / GetSourceLineNumber()
├─ ClassReloadHelper
│  ├─ ReconstructNode() on dependent nodes
│  ├─ QueueForCompilation() + FlushCompilationQueueAndReinstance()
│  └─ NotifyCustomizationModuleChanged()
└─ engine-owned widgets keep stock heuristics unless plugin explicitly refreshes them
```

[1] UEAS2 直接修改 editor 主干，让 Blueprint 调用节点、事件查找、MyBlueprint 和 PropertyEditor 默认认 Angelscript 产物：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Editor\BlueprintGraph\Private\K2Node_CallFunction.cpp
// 位置: 3549-3564
// 说明: Blueprint 调用节点把 runtime-generated 也当成可跳转定义的函数
// ============================================================================
// HAZE FIX(LV): Navigation to script functions
const bool bNativeFunction = (TargetFunction != nullptr)
	&& (TargetFunction->IsNative() || TargetFunction->HasAnyFunctionFlags(FUNC_RuntimeGenerated));
...
if (TargetFunction->IsNative() || TargetFunction->HasAnyFunctionFlags(FUNC_RuntimeGenerated))
{
	// ★ 调用节点默认允许跳到脚本定义
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Editor\KismetCompiler\Private\KismetCompilerMisc.cpp
// 位置: 655-658
// 说明: 事件 owner 不匹配时，runtime-generated 例外允许继续
// ============================================================================
const UFunction* FoundEvent = Class ? Class->FindFunctionByName(EventName, EIncludeSuperFlag::IncludeSuper) : NULL;
if (FoundEvent->GetOuter() != Class && !FoundEvent->HasAnyFunctionFlags(FUNC_RuntimeGenerated))
	return nullptr;
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Editor\Kismet\Private\SMyBlueprint.cpp
// 位置: 1461-1464
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Editor\PropertyEditor\Private\PropertyNode.cpp
// 位置: 440-441, 1774-1777
// 说明: 属性显示名和缓存策略也被主干 widget 直接吸收
// ============================================================================
const FText PropertyDesc = (Property->IsNative() || (Property->AngelscriptPropertyFlags & APF_RuntimeGenerated))
	? Property->GetDisplayNameText()
	: FText::FromName(PropertyName);

SetNodeFlags(EPropertyNodeFlags::NoCacheAddress, MyProperty && (MyProperty->AngelscriptPropertyFlags & APF_RuntimeGenerated));
if (CachedReadAddresses.Num() && !CachedReadAddresses.bRequiresCache
	&& !HasNodeFlags(EPropertyNodeFlags::RequiresValidation | EPropertyNodeFlags::NoCacheAddress))
```

[2] current 则在插件 editor 模块启动时显式注册 source navigation handler：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 位置: 351-354
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp
// 位置: 26-44, 136-138
// 说明: current 的跳转能力来自 editor 插件注册，而不是 K2Node 主干 patch
// ============================================================================
void FAngelscriptEditorModule::StartupModule()
{
	FClassReloadHelper::Init();
	RegisterAngelscriptSourceNavigation();
}

virtual bool NavigateToFunction(const UFunction* InFunction) override
{
	auto* ASFunc = Cast<const UASFunction>(InFunction);
	if (ASFunc == nullptr)
		return false;
	FString Path = ASFunc->GetSourceFilePath();
	if (Path.Len() == 0)
		return false;

	// ★ 通过插件 handler 把 UASFunction 导到脚本源文件
	OpenFile(Path, ASFunc->GetSourceLineNumber());
	return true;
}

void RegisterAngelscriptSourceNavigation()
{
	FSourceCodeNavigation::AddNavigationHandler(new FAngelscriptSourceCodeNavigation);
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp
// 位置: 1535-1545, 1548-1555
// 说明: handler 依赖 UASFunction 自己保存的源码位置
// ============================================================================
FString UASFunction::GetSourceFilePath() const
{
	if (ScriptFunction == nullptr)
		return TEXT("");
	auto& Manager = FAngelscriptEngine::Get();
	auto Module = Manager.GetModule(ScriptFunction->GetModule());
	...
	return Module->Code[0].AbsoluteFilename;
}

int UASFunction::GetSourceLineNumber() const
{
	if (ScriptFunction == nullptr)
		return -1;
	auto* RealFunc = ((asCScriptFunction*)ScriptFunction);
	auto* scriptData = RealFunc->scriptData;
```

[3] reload 后的 Blueprint/node 修复也不是 editor 主干默认行为，而是 `FClassReloadHelper` 显式做的：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/ClassReloadHelper.cpp
// 位置: 228-298, 333-338
// 说明: current 明确扫描依赖、重构节点、强制编译并刷新 PropertyEditor
// ============================================================================
for (UK2Node* Node : AllNodes)
{
	TArray<UStruct*> Dependencies;
	bool bShouldRefresh = false;
	...
	if (auto* Event = Cast<UK2Node_Event>(Node))
	{
		if (auto* Function = Cast<UDelegateFunction>(Event->FindEventSignatureFunction()))
		{
			if (NewDelegates.Contains(Function) || ReloadDelegates.Contains(Function))
			{
				// ★ 发现受影响 delegate/event 后显式重构节点
				bShouldRefresh = true;
			}
		}
	}
	...
	if (bShouldRefresh)
	{
		const UEdGraphSchema* Schema = Node->GetGraph()->GetSchema();
		Schema->ReconstructNode(*Node, true);
	}
}

for (UBlueprint* BP : DependencyBPs)
{
	RefreshRelevantNodesInBP(BP);
	FBlueprintCompilationManager::QueueForCompilation(BP);
}
FBlueprintCompilationManager::FlushCompilationQueueAndReinstance();

FPropertyEditorModule* PropertyModule = FModuleManager::GetModulePtr<FPropertyEditorModule>("PropertyEditor");
if (PropertyModule)
	PropertyModule->NotifyCustomizationModuleChanged();
```

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| “跳到脚本定义” owner | `K2Node_CallFunction` 等 engine/editor 主干直接认识 `FUNC_RuntimeGenerated` | `FSourceCodeNavigation` handler + `UASFunction` 源码定位 | **实现方式不同** |
| reload 后 Blueprint 节点修复 | 更多依赖引擎主干对 runtime-generated 产物的默认认知 | `ClassReloadHelper` 显式 `ReconstructNode` + `QueueForCompilation` | **实现方式不同，显式补偿更强** |
| MyBlueprint 属性显示名 / PropertyNode `NoCacheAddress` | 主干 widget 直接支持 `APF_RuntimeGenerated` | 仅见统一 refresh，未见同位 widget-level patch | **局部没有实现主干同等 affordance** |

这一节最值得补记的一句是：**current 并不是“没有 editor 集成”，而是把 editor 集成从“引擎 widget 默认懂脚本产物”改成了“插件主动注册 handler 并在 reload 后主动修复”。**

### [维度 D4] current 新增的 hot-reload 闭环，不只是 reload 分类器，而是 import preflight + post-swap resolve

前文已经把 `SoftReload / FullReloadSuggested / FullReloadRequired` 的分类树讲清楚了，但那一节更多是在看**类生成器怎么分流**。这一轮往前后各挪一步，会发现 current 真正新增的一条闭环是：

1. `BuildCompleted()` 之后、决定是否 swap-in 之前，先跑 `CheckFunctionImportsForNewModules()`。
2. 这一步会逐个 `ImportedFunction` 检查“来源模块是否存在”“签名是否还能在 swapping/new module 集里解析到”，失败则提前打编译错误，并把相关 section 放进 `PreviouslyFailedReloadFiles`。
3. swap-in 成功后，再 `ResolveAllDeclaredImports()`，把全部声明式 import 重新绑定一遍。

UEAS2 在同位阶段则是 `BuildCompleted()` 后直接进入 `bShouldSwapInModules` 决策；它仍保留 `bModuleSwapInError -> PreviouslyFailedReloadFiles` 这条老的重试队列，但本轮没有在上游插件源码里看到与 current 同位的 pre-swap import preflight / post-swap declared-import resolve。

```
[D4] Import Validation Around Hot Reload
UEAS2
├─ BuildCompleted()
├─ Decide bShouldSwapInModules
├─ ClassGenerator / SwapInModules
└─ synthetic swap-in errors
   └─ PreviouslyFailedReloadFiles retry queue

current
├─ BuildCompleted()
├─ if !ShouldUseAutomaticImportMethod()
│  └─ CheckFunctionImportsForNewModules()
│     ├─ validate source module exists
│     ├─ validate imported signature exists
│     └─ enqueue failing sections into PreviouslyFailedReloadFiles
├─ Decide bShouldSwapInModules
├─ synthetic swap-in errors
│  └─ same retry queue still exists
└─ post-swap ResolveAllDeclaredImports()
```

[1] UEAS2 在 `BuildCompleted()` 后直接进入 swap 决策，本轮未见 import 预检插槽：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\AngelscriptManager.cpp
// 位置: 2554-2558
// 说明: 上游在 BuildCompleted 之后直接进入 swap 决策
// ============================================================================
ScriptEngine->BuildCompleted();
bIgnoreCompileErrorDiagnostics = false;

// Decide whether to swap in the new modules or not
bool bShouldSwapInModules = true;
```

[2] current 在同一位置新增了 import preflight：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 3863-3870
// 说明: current 先检查导入函数是否会在 swap 后失配，再决定是否继续 hot reload
// ============================================================================
ScriptEngine->BuildCompleted();
bIgnoreCompileErrorDiagnostics = false;

// Check if any function imports would error out later
if (!ShouldUseAutomaticImportMethod())
{
	if (!CheckFunctionImportsForNewModules(CompiledModules))
	{
		// ★ 提前把 import 失配升级成 compile error
		bHadCompileErrors = true;
	}
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 4646-4707
// 说明: import preflight 会验证 source module / function decl，并把失败文件推进重试队列
// ============================================================================
bool FAngelscriptEngine::CheckFunctionImportsForNewModules(const TArray<TSharedRef<struct FAngelscriptModuleDesc>>& Modules)
{
	...
	for (int32 ImportIndex = 0, ImportCount = ScriptModule->GetImportedFunctionCount(); ImportIndex < ImportCount; ++ImportIndex)
	{
		const char* Decl = ScriptModule->GetImportedFunctionDeclaration(ImportIndex);
		FString FromModuleName = ANSI_TO_TCHAR(ScriptModule->GetImportedFunctionSourceModule(ImportIndex));
		auto FromModule = FindModule(FromModuleName);
		if (!FromModule.IsValid() || FromModule->ScriptModule == nullptr)
		{
			// ★ 来源模块不存在，直接报错
			ScriptCompileError(...);
			bModuleValid = false;
			continue;
		}

		asIScriptFunction* Function = FromModule->ScriptModule->GetFunctionByDecl(Decl);
		if (Function == nullptr)
		{
			// ★ 来源签名不存在，也提前报错
			ScriptCompileError(...);
			bModuleValid = false;
			continue;
		}
	}

	if (!bModuleValid)
	{
		// ★ 把失败 section 放回 PreviouslyFailedReloadFiles，下轮继续重试
		for (auto& Section : Module->Code)
			PreviouslyFailedReloadFiles.Add(FFilenamePair{ Section.AbsoluteFilename, Section.RelativeFilename });
	}
}
```

[3] swap-in 成功后，current 还会重新绑定全部 declared imports；而 `bModuleSwapInError` 这条老的失败重试队列仍然保留：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 4047-4063
// 说明: current 不是替换老队列，而是在老队列之外新增 import resolve 闭环
// ============================================================================
if (Module->bModuleSwapInError)
{
	for (auto& Section : Module->Code)
		PreviouslyFailedReloadFiles.Add(FFilenamePair{ Section.AbsoluteFilename, Section.RelativeFilename });
}

// We changed some modules, so we should re-resolve all declared imports in all modules
if (!ShouldUseAutomaticImportMethod())
{
	FAngelscriptScopeTimer PostTimer(TEXT("resolve declared imports"));
	ResolveAllDeclaredImports();
}
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\AngelscriptManager.cpp
// 位置: 2730-2735
// 说明: 上游保留了 bModuleSwapInError -> retry queue，但本轮未见 current 同位的 declared-import resolve
// ============================================================================
// If the module has received any synthetic errors from the class generator,
// make sure it gets recompiled until those go away
if (Module->bModuleSwapInError)
{
	for (auto& Section : Module->Code)
		PreviouslyFailedReloadFiles.Add(FFilenamePair{ Section.AbsoluteFilename, Section.RelativeFilename });
}
```

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| `BuildCompleted()` 后的 import 预检 | 本轮在同位阶段未见 | `CheckFunctionImportsForNewModules()` 显式预检 | **能力增强** |
| `bModuleSwapInError -> PreviouslyFailedReloadFiles` 重试队列 | 有 | 仍保留 | **没有实现差距** |
| swap-in 后 declared import 重新解析 | 本轮未见同位显式入口 | `ResolveAllDeclaredImports()` | **能力增强 / 验证闭环增强** |

这一轮关于 D4 最值得记住的新结论是：**current 真正新增的一条热重载闭环，不是又改了一次 `Soft/FullReload` 分类，而是把“导入函数签名是否还能成立”从事后爆炸，前移成了 swap-in 前的显式校验。**

---

## 深化分析 (2026-04-09 07:11:33)

### [维度 D4/D5] 断点在 hot reload 之后的“重挂链”并没有丢，只是 owner 从 `Manager` 收口到 `Engine`

前文已经把 `DebugServer V2` 的握手、断点消息和 transport 说清楚了，但还有一条更容易漏看的链：**脚本模块重编译完成后，已有 source breakpoint 到底有没有重新挂回新模块**。  
这条链对 `D4` 和 `D5` 是同一个问题。如果没有这一跳，协议兼容也只能停留在“能发断点消息”，而不是“reload 后断点还能继续命中”。

```
[D4/D5] Breakpoint Reapply After Reload
UEAS2
├─ Compile finished in FAngelscriptManager
├─ DebugServer->ReapplyBreakpoints()
└─ Breakpoints[module] -> hasBreakPoints on new asCModule

current
├─ Compile finished in FAngelscriptEngine
├─ DebugServer->ReapplyBreakpoints()
├─ Breakpoints[module] -> hasBreakPoints on new asCModule
└─ StateDump exports BreakpointCount / DataBreakpointCount
```

### 实现概述

源码显示，两边在 `PostCompile` 之后都会显式调用 `DebugServer->ReapplyBreakpoints()`。也就是说，current 并没有因为移除 `Loader`、把 runtime 生命周期改到 `FAngelscriptEngine`，就失去“reload 后重新绑定断点”的闭环。  

真正变化的是 owner：

1. UEAS2 在 `FAngelscriptManager` 完成编译后触发重挂。  
2. current 在 `FAngelscriptEngine` 完成编译后触发同一动作。  
3. current 额外把断点计数落进 `StateDump`，所以这条链第一次变成可落盘、可回归复盘的 runtime 状态。

### 关键源码引用

[1] 两边的“编译完成 -> 重挂断点”调用点在逻辑上同构，只是 owner 从 `Manager` 变成 `Engine`：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\AngelscriptManager.cpp
// 位置: 1251-1254
// 说明: 上游在编译完成后，显式要求 DebugServer 把已有断点重新应用到新模块
// ============================================================================
#if WITH_AS_DEBUGSERVER
	// ★ newly compiled modules 需要重新挂断点
	if (DebugServer != nullptr)
		DebugServer->ReapplyBreakpoints();
#endif
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 2499-2502
// 说明: current 保留同一条闭环，只是触发者变成 engine owner
// ============================================================================
#if WITH_AS_DEBUGSERVER
	// ★ 模块重编译后，同样重新应用已有断点
	if (DebugServer != nullptr)
		DebugServer->ReapplyBreakpoints();
#endif
```

[2] `ReapplyBreakpoints()` 的主体逻辑也基本保持同构：按 module name 回查新 `ScriptModule`，再把 `hasBreakPoints` 重新打回去：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Debugging\AngelscriptDebugServer.cpp
// 函数: FAngelscriptDebugServer::ReapplyBreakpoints
// 位置: 1102-1121
// 说明: UEAS2 通过 Manager 重新解析模块，再把断点位挂回 asCModule
// ============================================================================
void FAngelscriptDebugServer::ReapplyBreakpoints()
{
	auto& Manager = FAngelscriptManager::Get();
	for (auto& BreakpointElem : Breakpoints)
	{
		auto FileBreakpoints = BreakpointElem.Value;
		...
		auto ModuleDesc = Manager.GetModuleByModuleName(BreakpointElem.Key);
		FileBreakpoints->Module = ModuleDesc;

		if (ModuleDesc.IsValid())
		{
			auto* ScriptModule = (asCModule*)ModuleDesc->ScriptModule;
			if (ScriptModule != nullptr)
				ScriptModule->hasBreakPoints = true;
		}
	}
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 函数: FAngelscriptDebugServer::ReapplyBreakpoints
// 位置: 1197-1216
// 说明: current 只把 Manager 解析改成 Engine 解析，其余断点重挂 contract 保持一致
// ============================================================================
void FAngelscriptDebugServer::ReapplyBreakpoints()
{
	auto& Manager = FAngelscriptEngine::Get();
	for (auto& BreakpointElem : Breakpoints)
	{
		auto FileBreakpoints = BreakpointElem.Value;
		...
		auto ModuleDesc = Manager.GetModuleByModuleName(BreakpointElem.Key);
		FileBreakpoints->Module = ModuleDesc;

		if (ModuleDesc.IsValid())
		{
			auto* ScriptModule = (asCModule*)ModuleDesc->ScriptModule;
			if (ScriptModule != nullptr)
				ScriptModule->hasBreakPoints = true;
		}
	}
}
```

[3] current 额外把这条断点状态导出到 dump，而不是只能靠 IDE 联调时肉眼判断：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp
// 位置: 1107-1114
// 说明: current 把 debugger 状态做成可落盘观测面
// ============================================================================
#if WITH_AS_DEBUGSERVER
	if (Engine.DebugServer != nullptr)
	{
		Writer.AddRow({ TEXT("HasAnyClients"), BoolToString(Engine.DebugServer->HasAnyClients()) });
		Writer.AddRow({ TEXT("BreakpointCount"), LexToString(Engine.DebugServer->BreakpointCount) });
		Writer.AddRow({ TEXT("DataBreakpointCount"), LexToString(Engine.DebugServer->DataBreakpoints.Num()) });
		Writer.AddRow({ TEXT("bIsPaused"), BoolToString(Engine.DebugServer->bIsPaused) });
		Writer.AddRow({ TEXT("bIsDebugging"), BoolToString(Engine.DebugServer->bIsDebugging) });
	}
#endif
```

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| reload 后 source breakpoint 是否重新挂回模块 | 有，`ReapplyBreakpoints()` | 有，`ReapplyBreakpoints()` | **没有实现差距** |
| 触发 owner | `FAngelscriptManager` | `FAngelscriptEngine` | **实现方式不同** |
| 断点运行态是否可外部落盘观察 | 本轮未见同位导出 | `StateDump` 导出计数和状态 | **实现质量增强** |

这条链最值得补记的一句是：**current 的 debugger/hot-reload 集成并不是“协议还在，但 reload 后靠运气”；它保留了上游的重挂闭环，只是把 owner 收口到 engine，并新增了 dump 级可观测性。**

### [维度 D1/D5] `BreakOptions` / `BreakFilters` 仍是模块委托扩展点，但“是否该停”已经改为按 `OwnerEngine` 的 world context 判定

前文已经确认过 `BreakFilters` opcode、`BreakOptions` 消息和 `DebugServer V2` 主线兼容；这一轮补的是**谁来决定“当前这一停是否应该在 active side 触发”**。  

这里有个很细但重要的变化：current 并没有把断点过滤器彻底做成 per-engine delegate，它仍然是模块级委托；但 `DebugServer` 在执行委托前拿到的 world context，已经从全局 `CurrentWorldContext` 改成了 `OwnerEngine->GetCurrentWorldContextObject()`。

```
[D1/D5] Break Decision Ownership
UEAS2
├─ DebugServer
│  ├─ read FAngelscriptManager::CurrentWorldContext
│  └─ call FAngelscriptCodeModule::GetDebugCheckBreakOptions()
└─ RequestBreakFilters -> CodeModule delegate

current
├─ DebugServer(OwnerEngine)
│  ├─ read OwnerEngine->GetCurrentWorldContextObject()
│  └─ call FAngelscriptRuntimeModule::GetDebugCheckBreakOptions()
└─ RequestBreakFilters -> RuntimeModule delegate
```

### 实现概述

这意味着 current 在 D5 上不是“全量 per-engine 化”，而是更精确的这句：

1. **扩展点 owner 仍是模块级**，所以外部系统接入 `BreakFilters` / `BreakOptions` 的方式没有断。  
2. **运行时判定输入改成 engine-scope**，所以同一进程里多个 engine/scope 并存时，至少“当前 world context 是谁”不再只能依赖全局静态状态。  

换句话说，current 的改动不是 wire protocol 级，而是 stop-policy owner 级。

### 关键源码引用

[1] `ShouldBreakOnActiveSide()` 的差异只在 world-context owner：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Debugging\AngelscriptDebugServer.cpp
// 函数: FAngelscriptDebugServer::ShouldBreakOnActiveSide
// 位置: 545-554
// 说明: 上游直接读取全局 CurrentWorldContext
// ============================================================================
bool FAngelscriptDebugServer::ShouldBreakOnActiveSide()
{
	UObject* WorldContext = FAngelscriptManager::CurrentWorldContext;
	if (WorldContext == nullptr)
		return true;

	auto& Delegate = FAngelscriptCodeModule::GetDebugCheckBreakOptions();
	if (Delegate.IsBound())
	{
		return Delegate.Execute(BreakOptions, WorldContext);
	}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 函数: FAngelscriptDebugServer::ShouldBreakOnActiveSide
// 位置: 652-661
// 说明: current 仍用模块委托，但把 world context 切到 OwnerEngine
// ============================================================================
bool FAngelscriptDebugServer::ShouldBreakOnActiveSide()
{
	UObject* WorldContext = OwnerEngine != nullptr ? OwnerEngine->GetCurrentWorldContextObject() : nullptr;
	if (WorldContext == nullptr)
		return true;

	auto& Delegate = FAngelscriptRuntimeModule::GetDebugCheckBreakOptions();
	if (Delegate.IsBound())
	{
		return Delegate.Execute(BreakOptions, WorldContext);
	}
```

[2] `RequestBreakFilters` 的协议面保持一致，只是 provider 从 `CodeModule` 平移到 `RuntimeModule`：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Debugging\AngelscriptDebugServer.cpp
// 位置: 1038-1050
// 说明: 过滤器列表仍由模块委托提供，再发回客户端
// ============================================================================
else if (MessageType == EDebugMessageType::RequestBreakFilters)
{
	TMap<FName, FString> FilterList;
	FAngelscriptCodeModule::GetDebugBreakFilters().ExecuteIfBound(FilterList);
	...
	SendMessageToClient(Client, EDebugMessageType::BreakFilters, Filters);
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 位置: 1147-1159
// 说明: current 的 client-facing protocol 没变，变化的是 provider owner
// ============================================================================
else if (MessageType == EDebugMessageType::RequestBreakFilters)
{
	TMap<FName, FString> FilterList;
	FAngelscriptRuntimeModule::GetDebugBreakFilters().ExecuteIfBound(FilterList);
	...
	SendMessageToClient(Client, EDebugMessageType::BreakFilters, Filters);
}
```

[3] 两边头文件里的 delegate 签名都还是同一套，这能解释为什么 current 是“owner 迁移”，不是“扩展接口重写”：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Public\AngelscriptCodeModule.h
// 位置: 6-11, 33-34
// 说明: 上游的 BreakOptions / BreakFilters 都是模块静态委托
// ============================================================================
typedef TArray<FName> FAngelscriptDebugBreakOptions;
typedef TMap<FName, FString> FAngelscriptDebugBreakFilters;
DECLARE_DELEGATE_RetVal_TwoParams(bool, FAngelscriptDebugCheckBreakOptions, const FAngelscriptDebugBreakOptions&, UObject*);
DECLARE_DELEGATE_OneParam(FAngelscriptGetDebugBreakFilters, FAngelscriptDebugBreakFilters&);
...
static FAngelscriptDebugCheckBreakOptions& GetDebugCheckBreakOptions();
static FAngelscriptGetDebugBreakFilters& GetDebugBreakFilters();
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h
// 位置: 7-11, 33-34
// 说明: current 没有改 delegate contract，只改 owner 与 context 来源
// ============================================================================
typedef TArray<FName> FAngelscriptDebugBreakOptions;
typedef TMap<FName, FString> FAngelscriptDebugBreakFilters;
DECLARE_DELEGATE_RetVal_TwoParams(bool, FAngelscriptDebugCheckBreakOptions, const FAngelscriptDebugBreakOptions&, UObject*);
DECLARE_DELEGATE_OneParam(FAngelscriptGetDebugBreakFilters, FAngelscriptDebugBreakFilters&);
...
static FAngelscriptDebugCheckBreakOptions& GetDebugCheckBreakOptions();
static FAngelscriptGetDebugBreakFilters& GetDebugBreakFilters();
```

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| break filter / break option 的扩展接口是否还在 | 有 | 有 | **没有实现差距** |
| “是否该停”读取的 world context owner | 全局 `CurrentWorldContext` | `OwnerEngine->GetCurrentWorldContextObject()` | **实现方式不同** |
| 扩展点是否已经完全 per-engine 化 | 否，模块委托 | 仍然是否，模块委托 + engine context 输入 | **部分收口，不是彻底重写** |

因此，这一节更准确的结论是：**current 在 D5 上做的不是“另起一套 debugger extension API”，而是把 stop-policy 的上下文 owner 从全局 manager 收到了 engine scope；接口面保持兼容，运行时判定更局部化。**

### [维度 D1/D2/D8/D11] `AngelscriptGAS` 这个上游 optional plugin 说明：一部分“无引擎补丁替代”其实早就在 Hazelight 生态里存在，只是 current 把 owner 折回主插件

前文多次说 current 用 wrapper + UHTTool + fallback 绕开引擎补丁，但如果只看主插件源码，容易误判成“这些都是 AngelPortV2 后来才发明的替代层”。  

把 UEAS2 的 sibling plugin `AngelscriptGAS` 也拉进来后，能看到一个更精确的事实：**Hazelight 自己就已经把一部分高层 feature surface 写成 stock-UHT 友好的 `BlueprintCallable/BlueprintPure` wrapper，而不是强依赖 `ScriptCallable` / `APF_CppRef`。current 做的主要是 owner 重划分与生成链整合。**

```
[D1/D2/D8/D11] GAS Wrapper Ownership
UEAS2 ecosystem
├─ AngelscriptGAS sibling plugin
│  ├─ UAngelscriptAbilitySystemComponent
│  ├─ BlueprintCallable / BlueprintPure wrappers
│  └─ ScriptName keeps script-facing API name
└─ optional feature plugin boundary

current
├─ AngelscriptRuntime/Core/AngelscriptAbilitySystemComponent.h
├─ same wrapper-shaped API folded into main plugin
├─ UHTTool scans BlueprintCallable / Pure only
└─ reflective fallback copies back CPF_ReferenceParm out refs
```

### 实现概述

这个例子把 D8/D11 的边界说得更细：

1. **不是所有高层扩展都依赖引擎补丁。** `AngelscriptGAS` 里的 `UAngelscriptAbilitySystemComponent`，上游就已经是 `BlueprintCallable/BlueprintPure` 形状。  
2. **current 的关键变化是 owner 折叠。** 这些 API 现在被并入 `AngelscriptRuntime`，再由 `AngelscriptUHTTool` 扫描生成表。  
3. **ref/out 语义在这类 wrapper 上不需要 `APF_CppRef`。** 因为 stock UHT 仍会给 `float& OutCurrentValue` 这样的参数打 `CPF_ReferenceParm`，current 的 reflective fallback 会把它们拷回脚本侧。  

所以这一块的差距不能写成“UEAS2 靠引擎补丁才能做 GAS，而 current 靠 wrapper 勉强补”。更准确的是：**上游可选插件本来就在走 wrapper 路线；current 把这条路线从 sibling-plugin packaging 收口成了 main-plugin delivery。**

### 关键源码引用

[1] UEAS2 的引擎补丁确实为通用 C++ surface 提供了 `APF_CppRef` 这类额外语义位，runtime 也会显式读取：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Programs\Shared\EpicGames.UHT\Parsers\UhtPropertyParser.cs
// 位置: 664-666
// 说明: 上游 UHT parser 会给非 const ref 额外写入 AngelscriptPropertyFlags.CppRef
// ============================================================================
// HAZE FIX(LV): Expose type-erased method pointers to native C++ methods
outProperty.AngelscriptPropertyFlags |= EAngelscriptPropertyFlags.CppRef;
// END HAZE FIX
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Programs\Shared\EpicGames.UHT\Exporters\CodeGen\UhtHeaderCodeGeneratorCppFile.cs
// 位置: 2548-2549
// 说明: codegen 侧继续消费 CppRef / CppConst
// ============================================================================
bool bIsRef = Property.AngelscriptPropertyFlags.HasFlag(EAngelscriptPropertyFlags.CppRef);
bool bIsConst = Property.AngelscriptPropertyFlags.HasFlag(EAngelscriptPropertyFlags.CppConst);
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\AngelscriptType.cpp
// 位置: 256-268
// 说明: UEAS2 runtime 对普通 C++ property 更相信 APF_CppRef，而不是 CPF_ReferenceParm
// ============================================================================
if (Property->HasAnyPropertyFlags(CPF_ConstParm) || (Property->AngelscriptPropertyFlags & APF_CppConst) != 0)
	Usage.bIsConst = true;

if ((Property->AngelscriptPropertyFlags & APF_RuntimeGenerated) != 0)
{
	if (Property->HasAnyPropertyFlags(CPF_ReferenceParm))
		Usage.bIsReference = true;
}
else
{
	// ★ 普通 C++ property 走 AngelscriptPropertyFlags.CppRef
	if ((Property->AngelscriptPropertyFlags & APF_CppRef) != 0)
		Usage.bIsReference = true;
}
```

[2] 但 UEAS2 的 `AngelscriptGAS` sibling plugin 本身，已经把公开 wrapper 写成了 stock-UHT 友好的 `BlueprintCallable/BlueprintPure` 形状：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\AngelscriptGAS\Source\Public\AngelscriptAbilitySystemComponent.h
// 位置: 163-225
// 说明: 上游 optional GAS plugin 自己就不是 ScriptCallable-only authoring
// ============================================================================
UFUNCTION(BlueprintCallable, Category = "Attribute Callbacks")
void GetAndRegisterAttributeChangedCallback(
	TSubclassOf<UAngelscriptAttributeSet> AttributeSetClass,
	FName AttributeName,
	UObject* CallbackObject,
	FName CallbackFunctionName_FAngelscriptAttributeChangedData,
	float& OutCurrentValue);

UFUNCTION(BlueprintPure, Category = "Attributes")
bool TryGetAttributeCurrentValue(TSubclassOf<UAngelscriptAttributeSet> AttributeSetClass, FName AttributeName, float& OutCurrentValue) const;

UFUNCTION(BlueprintCallable, Category = "Abilities", meta = (DisplayName = "Give Ability", ScriptName = "GiveAbility"))
FGameplayAbilitySpecHandle BP_GiveAbility(TSubclassOf<UGameplayAbility> InAbilityClass, int32 Level = 1, int32 OptionalInputID = -1, UObject* OptionalSourceObject = nullptr);
```

[3] current 只是把这套 wrapper owner 并回主插件；函数形状本身保持同一条路线：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilitySystemComponent.h
// 位置: 172-223
// 说明: current 复用的是同一类 BlueprintCallable / ScriptName wrapper，而不是重新发明一套 API 形状
// ============================================================================
UFUNCTION(BlueprintCallable, Category = "Attribute Callbacks")
void GetAndRegisterAttributeChangedCallback(
	TSubclassOf<UAngelscriptAttributeSet> AttributeSetClass,
	FName AttributeName,
	UObject* CallbackObject,
	FName CallbackFunctionName_FAngelscriptAttributeChangedData,
	float& OutCurrentValue);

UFUNCTION(BlueprintPure, Category = "Attributes")
bool TryGetAttributeCurrentValue(TSubclassOf<UAngelscriptAttributeSet> AttributeSetClass, FName AttributeName, float& OutCurrentValue) const;

UFUNCTION(BlueprintCallable, Category = "Abilities", meta = (DisplayName = "Give Ability", ScriptName = "GiveAbility"))
FGameplayAbilitySpecHandle BP_GiveAbility(TSubclassOf<UGameplayAbility> InAbilityClass, int32 Level = 1, int32 OptionalInputID = -1, UObject* OptionalSourceObject = nullptr);
```

[4] current 的真正新增 owner 是 build-time 生成链：`UHTTool` 只扫描 `BlueprintCallable/Pure`，这刚好能把这类 GAS wrapper 吃进去：

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs
// 位置: 56-63, 72-75
// 说明: current 的 auto-export 面明确只认 BlueprintCallable / BlueprintPure
// ============================================================================
internal static bool IsBlueprintCallable(UhtFunction function)
{
	string functionFlags = function.FunctionFlags.ToString();

	return function.FunctionType == UhtFunctionType.Function &&
		(functionFlags.Contains("BlueprintCallable", StringComparison.Ordinal) ||
		functionFlags.Contains("BlueprintPure", StringComparison.Ordinal));
}

...
if (child is UhtFunction function && IsBlueprintCallable(function))
{
	functionCount++;
	...
}
```

[5] 对 `float& OutCurrentValue` 这类 out-ref，current 在 runtime 里也不是“完全没 owner”，而是回到 stock UHT 能提供的 `CPF_ReferenceParm`：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp
// 位置: 252-257
// 说明: current 不再读取 APF_CppRef；这类 wrapper 直接依赖 CPF_ReferenceParm
// ============================================================================
if (Property->HasAnyPropertyFlags(CPF_ConstParm))
	Usage.bIsConst = true;

if (Property->HasAnyPropertyFlags(CPF_ReferenceParm))
	Usage.bIsReference = true;
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp
// 位置: 305-355
// 说明: reflective fallback 会把非 const ref 参数在 ProcessEvent 后拷回脚本侧
// ============================================================================
FReflectiveOutReference OutReferences[BlueprintCallableReflectiveFallbackMaxArgs];
...
if (Property->HasAnyPropertyFlags(CPF_ReferenceParm) && !Property->HasAnyPropertyFlags(CPF_ConstParm))
{
	// ★ 记录 out-ref 原地址
	OutReferences[OutReferenceCount++] = { Property, SourceAddress };
}

TargetObject->ProcessEvent(Function, ParameterBuffer);

for (int32 OutReferenceIndex = 0; OutReferenceIndex < OutReferenceCount; ++OutReferenceIndex)
{
	const FReflectiveOutReference& OutReference = OutReferences[OutReferenceIndex];
	OutReference.Property->CopySingleValue(
		OutReference.ScriptValue,
		OutReference.Property->ContainerPtrToValuePtr<void>(ParameterBuffer));
}
```

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| GAS 高层 wrapper 是否必须依赖引擎补丁 authoring | **不是**，`AngelscriptGAS` 已经大量使用 `BlueprintCallable/Pure` | 同样使用 `BlueprintCallable/Pure` | **没有实现差距** |
| 这套 wrapper 的交付 owner | sibling plugin `AngelscriptGAS` | 主插件 `AngelscriptRuntime` | **实现方式不同** |
| ref/out 语义 owner | 通用面可依赖 `APF_CppRef`；wrapper 也可走 Blueprint flags | wrapper 子集主要靠 `CPF_ReferenceParm` + reflective fallback | **实现方式不同，覆盖面更依赖 authoring 形状** |

这个例子最该修正的一句误读是：**current 的“无引擎补丁替代”并不全是从零重做；至少在 GAS 这类 optional feature surface 上，Hazelight 上游自己就已经证明了 `BlueprintCallable wrapper` 是可行路线。current 真正变化的是把这条路线从 sibling-plugin packaging，收口成 main-plugin delivery，并接到 UHTTool/fallback 的统一 owner 下。**

---
## 深化分析 (2026-04-09 07:21:24)

### [维度 D1/D4] `Loader` 退出之后，editor 场景下的“变更发现 owner”已经前移到 `AngelscriptEditor`

前文已经确认 `Loader` 不是 UEAS2 的主运行时；这一轮补上的关键证据是：**editor 场景下，Hazelight 与 current 的文件变更发现都不由 runtime 轮询线程独占，真正的 owner 是 `AngelscriptEditor + DirectoryWatcher`。**  
因此 current 移除 `Loader` 后，变化不是“少了一个热重载模块”，而是把初始化入口收进 `AngelscriptRuntime`，同时把 editor 侧监听能力显式留在 `AngelscriptEditor`。

```
[D1/D4] Hot Reload Ownership Split
UEAS2
├─ AngelscriptLoader                   // 薄启动层
│  └─ depends on AngelscriptCode
├─ AngelscriptCode / FAngelscriptManager
│  ├─ checker thread (!GIsEditor)
│  ├─ CheckForHotReload / PerformHotReload
│  └─ DebugServer::ReapplyBreakpoints
└─ AngelscriptEditor
   └─ DirectoryWatcher (editor-only detection)

current
├─ AngelscriptRuntime / FAngelscriptEngine
│  ├─ checker thread (!RuntimeConfig.bIsEditor)
│  ├─ CheckForHotReload / PerformHotReload
│  └─ DebugServer::ReapplyBreakpoints
├─ AngelscriptEditor
│  ├─ RegisterDirectoryChangedCallback_Handle
│  ├─ QueueScriptFileChanges
│  └─ DirectoryWatcher automation tests
└─ Dump
   ├─ HotReloadState.csv
   └─ EditorReloadState.csv
```

### 实现概述

UEAS2 的 `Loader` 模块从依赖面上就已经非常薄，只依赖 `AngelscriptCode`，editor 下再顺带依赖 `AngelscriptEditor`。真正的 editor 文件监听并不在 `Loader`，而在 `AngelscriptEditor` 的 `DirectoryWatcher`。  

current 保留了同样的分工方向，但把职责边界写得更硬：

1. `AngelscriptRuntime` 继续保留 non-editor 场景的 checker thread、reload 执行器和 breakpoint reapply。  
2. `AngelscriptEditor` 明确注册 `DirectoryWatcher`，把文件变更统一归一化成 `FileChangesDetectedForReload` / `FileDeletionsDetectedForReload` 队列。  
3. 这条 editor 监听链不再只是运行期经验，而是有独立自动化测试和 dump 表可回归。  

### 关键源码引用

[1] UEAS2 的 `Loader` 依赖面本身已经说明它只是启动薄壳，editor 侧监听能力在 `AngelscriptEditor`：

```csharp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptLoader\AngelscriptLoader.Build.cs
// 位置: 12-25
// 说明: Loader 只直接依赖 Core / Engine / AngelscriptCode；editor 下顺带拉起 AngelscriptEditor
// ============================================================================
PublicDependencyModuleNames.AddRange(new string[]
{
	"Core",
	"Engine",
	"AngelscriptCode",
});

if (Target.bBuildEditor)
{
	PublicDependencyModuleNames.AddRange(new string[]
	{
		"AngelscriptEditor",
	});
}
```

```csharp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptEditor\AngelscriptEditor.Build.cs
// 位置: 12-40
// 说明: UEAS2 的 editor 模块自己就显式依赖 DirectoryWatcher
// ============================================================================
PublicDependencyModuleNames.AddRange(new string[]
{
	"Core",
	"CoreUObject",
	"Engine",
	"UnrealEd",
	"EditorSubsystem",
	"AngelscriptCode",
	"BlueprintGraph",
	"Kismet",
	"DirectoryWatcher",
	"Slate",
	"SlateCore",
	"AssetTools",
});
```

[2] current 把这条 editor 监听链固定在 `AngelscriptEditor`，并把回调结果归一化到 primary engine 队列：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 位置: 366-381
// 说明: current 在 Editor 模块注册 DirectoryWatcher，而不是把文件监听塞回 Runtime/Loader
// ============================================================================
FDirectoryWatcherModule& DirectoryWatcherModule = FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>("DirectoryWatcher");
IDirectoryWatcher* DirectoryWatcher = DirectoryWatcherModule.Get();

if (ensure(DirectoryWatcher != nullptr))
{
	TArray<FString> AllRootPaths = FAngelscriptEngine::MakeAllScriptRoots();
	for (const auto& RootPath : AllRootPaths)
	{
		FDelegateHandle WatchHandle;
		DirectoryWatcher->RegisterDirectoryChangedCallback_Handle(
			*RootPath,
			IDirectoryWatcher::FDirectoryChanged::CreateStatic(&OnScriptFileChanges),
			WatchHandle,
			IDirectoryWatcher::IncludeDirectoryChanges);
	}
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp
// 位置: 43-89
// 说明: 回调只负责把 editor 事件压成统一队列，真正 reload 仍由 Runtime 执行
// ============================================================================
void QueueScriptFileChanges(...)
{
	for (const FFileChangeData& Change : Changes)
	{
		...
		Engine.LastFileChangeDetectedTime = FPlatformTime::Seconds();

		if (AbsolutePath.EndsWith(TEXT(".as")))
		{
			if (Change.Action == FFileChangeData::EFileChangeAction::FCA_Removed)
			{
				Engine.FileDeletionsDetectedForReload.AddUnique({ AbsolutePath, RelativePath });
			}
			else
			{
				Engine.FileChangesDetectedForReload.AddUnique({ AbsolutePath, RelativePath });
			}

			// ★ 这里只入队，不在 editor 回调里直接编译
			UE_LOG(Angelscript, Log, TEXT("Queued script file change for primary engine reload: %s"), *RelativePath);
			continue;
		}
		...
	}
}
```

[3] current 的 runtime 仍然保留与 UEAS2 同级的 reload 执行器，只是消费的是 editor 已归一化的队列；同时新增 dump 观测面：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 1615-1620, 2729-2770, 2499-2502
// 说明: Runtime 只在 non-editor 场景保留 checker thread；editor 场景则消费 watcher 队列并执行 reload
// ============================================================================
// Use the checker thread if we want to detect hot reloads,
// but we don't have access to the editor. In editor, the AngelscriptEditor
// module will use the directory watcher system to detect reloads instead.
bUseHotReloadCheckerThread = bScriptDevelopmentMode && !RuntimeConfig.bIsEditor;
if (bUseHotReloadCheckerThread)
	StartHotReloadThread();

void FAngelscriptEngine::CheckForHotReload(ECompileType CompileType)
{
	...
	FileList.Append(FileChangesDetectedForReload);
	FileChangesDetectedForReload.Empty();
	...
	if (FileList.Num() != 0)
	{
		UE_LOG(Angelscript, Log, TEXT("Primary engine consuming %d queued script file change(s) for hot reload."), FileList.Num());
		PerformHotReload(CompileType, FileList);
	}
}

if (DebugServer != nullptr)
	DebugServer->ReapplyBreakpoints();
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp
// 位置: 981-1008
// 说明: current 把待 reload / 待删除队列落成 HotReloadState.csv，方便离线观测
// ============================================================================
for (const FAngelscriptEngine::FFilenamePair& FilenamePair : Engine.FileChangesDetectedForReload)
{
	Writer.AddRow({
		GetFilenamePairPath(FilenamePair),
		TEXT("PendingReload"),
		FString()
	});
}

for (const FAngelscriptEngine::FFilenamePair& FilenamePair : Engine.FileDeletionsDetectedForReload)
{
	Writer.AddRow({
		GetFilenamePairPath(FilenamePair),
		TEXT("PendingDeletion"),
		FString()
	});
}
```

[4] current 给 `DirectoryWatcher` 队列补了针对 rename / folder add / folder remove 的自动化断言；这在 UEAS2 代码里本轮未见同层粒度的测试入口：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp
// 位置: 75-102, 131-191, 194-222
// 说明: current 不只“有 watcher”，还把队列语义做成独立回归测试
// ============================================================================
TestEqual(TEXT("DirectoryWatcher.Queue.ScriptAddAndRemove should queue one added script"), Engine->FileChangesDetectedForReload.Num(), 1);
TestEqual(TEXT("DirectoryWatcher.Queue.ScriptAddAndRemove should queue one removed script"), Engine->FileDeletionsDetectedForReload.Num(), 1);

TestEqual(TEXT("DirectoryWatcher.Queue.FolderAddScansContainedScripts should queue the two script files in the new folder"), Engine->FileChangesDetectedForReload.Num(), 2);

TestEqual(TEXT("DirectoryWatcher.Queue.FolderRemoveUsesLoadedScriptEnumerator should queue two removed scripts from the enumerator"), Engine->FileDeletionsDetectedForReload.Num(), 2);

TestEqual(TEXT("DirectoryWatcher.Queue.RenameWindowTracksRemoveAndAdd should queue one removed script"), Engine->FileDeletionsDetectedForReload.Num(), 1);
TestEqual(TEXT("DirectoryWatcher.Queue.RenameWindowTracksRemoveAndAdd should queue one added script"), Engine->FileChangesDetectedForReload.Num(), 1);
```

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| `Loader` 是否承担真实的热重载 owner | 否，依赖面很薄，执行 owner 仍是 `AngelscriptCode` / `Manager` | 已移除，但入口改由 `RuntimeModule + Engine` 接管 | **实现方式不同** |
| editor 场景文件变更发现是否仍在 `Editor` 模块 | 是，`AngelscriptEditor` 依赖 `DirectoryWatcher` | 是，`AngelscriptEditor` 注册 watcher 并归一化队列 | **没有实现差距** |
| non-editor 场景轮询线程是否仍存在 | 是，`!GIsEditor` 启 checker thread | 是，`!RuntimeConfig.bIsEditor` 启 checker thread | **没有实现差距** |
| watcher 队列语义是否具备自动化回归与 dump 观测 | 本轮未见同层粒度 | 有独立测试 + `HotReloadState.csv` | **实现质量增强** |

这一节最该修正的旧印象是：**current 的“去 Loader 化”并不是把热重载入口粗暴并回 Runtime；更准确地说，是把“启动入口”收回 Runtime，把“editor 变更发现”固定在 Editor，并把两者之间的队列边界做成可测试、可落盘的契约。**

### [维度 D4/D9] current 已把 `SoftReload / FullReloadSuggested / FullReloadRequired` 变成可回归矩阵，而不只是运行时分支

UEAS2 和 current 都保留 `HotReloadTestRunner`，但 current 额外把 reload requirement 本身抽成了学习型自动化测试，直接验证“改函数体”“加类”“改签名”三种变化的期望决策。这样一来，hot reload 的风险不再只靠跑完一轮 editor 手测发现。

```
[D4/D9] Reload Requirement Matrix
BodyOnlyChange             -> SoftReload
ClassAdded                 -> FullReloadSuggested
FunctionSignatureChanged   -> FullReloadRequired
```

[1] current 的 `HotReloadTestRunner` 接口已经把 `AutomaticImport` 当成输入显式纳入，而 UEAS2 版本没有这条上下文参数：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Testing/UnitTest.h
// 位置: 33-51
// 说明: current 的 hot-reload test runner 会把 automatic import 语义带进 PrepareTests
// ============================================================================
class FHotReloadTestRunner
{
public:
	...
	void PrepareTests(
		const TArray<TSharedRef<struct FAngelscriptModuleDesc>>& ActiveModules,
		const TArray<TSharedRef<FAngelscriptModuleDesc>>& ModulesToCompile,
		const TArray<FString>& FileList,
		bool AutomaticImport);
	...
};
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Testing\UnitTest.h
// 位置: 33-50
// 说明: UEAS2 版本仍然只有模块列表 + 文件列表，没有额外把 import 策略显式送进测试准备阶段
// ============================================================================
class FHotReloadTestRunner
{
public:
	...
	void PrepareTests(
		const TArray<TSharedRef<struct FAngelscriptModuleDesc>>& ActiveModules,
		const TArray<TSharedRef<FAngelscriptModuleDesc>>& ModulesToCompile,
		const TArray<FString>& FileList);
	...
};
```

[2] 更重要的是 current 直接把 reload requirement 做成断言矩阵：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningReloadAndClassAnalysisTests.cpp
// 位置: 138-145, 167-172, 201-227
// 说明: current 对三类改动的 reload requirement 有直接断言，不再只靠手动回归
// ============================================================================
const FLearningReloadScenario BodyOnlyScenario{
	...
	FAngelscriptClassGenerator::SoftReload,
	false,
	false,
};

const FLearningReloadScenario ClassAddedScenario{
	...
	FAngelscriptClassGenerator::FullReloadSuggested,
	true,
	false,
};

const FLearningReloadScenario SignatureChangeScenario{
	...
	FAngelscriptClassGenerator::FullReloadRequired,
	true,
	true,
};

TestEqual(TEXT("Body-only change should remain soft reload"), BodyOnlyOutcome.ReloadRequirement, BodyOnlyScenario.ExpectedRequirement);
TestTrue(TEXT("Class-added change should request the full reload path"), ClassAddedOutcome.bWantsFullReload && !ClassAddedOutcome.bNeedsFullReload);
TestTrue(TEXT("Signature change should mark full reload as required"), SignatureChangeOutcome.bWantsFullReload && SignatureChangeOutcome.bNeedsFullReload);
```

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| hot reload 后是否运行回归测试 | 有 `HotReloadTestRunner` | 同样有 `HotReloadTestRunner` | **没有实现差距** |
| reload decision 是否被独立成自动化矩阵 | 本轮未见同层测试 | 有 `Learning.ReloadAnalysis` 直断三类场景 | **实现质量增强** |
| 自动导入策略是否进入测试准备上下文 | `PrepareTests(FileList)` | `PrepareTests(FileList, AutomaticImport)` | **实现方式不同，current 更可观测** |

### [维度 D3/D8/D11] UEAS2 的引擎补丁，本质上是在给 UHT 增加一套新的 authoring 语言；current 则改成 `stock UHT + hybrid bind`

这一轮把引擎补丁拆开看以后，能更准确地理解它在干什么：它不只是“多了几处 `ScriptCallable` 关键字”，而是把 **UHT 可理解的 authoring 语言** 和 **`FProperty` 的 Angelscript 语义位** 一起扩了出来。  

current 的对应策略不是“完全不做这些事”，而是把它拆成三层：

1. **stock-UHT wrapper 层**：尽量把 native surface 写成 `BlueprintCallable / BlueprintPure`。  
2. **UHTTool 导出层**：只扫描 stock UHT 能稳定给出的 Blueprint flags，并把跳过项写成 CSV。  
3. **runtime 兼容层**：保留 `ScriptCallable` metadata 的消费能力，同时用 `CPF_ReferenceParm + reflective fallback` 兜住 ref/out。  

```
[D3/D8/D11] Authoring Pipeline
UEAS2 engine-patched path
├─ UFUNCTION(ScriptCallable)
├─ UCLASS(meta=(ScriptMixin=...))
├─ ScriptMethod / ScriptName
└─ non-const ref -> APF_CppRef
    ↓
Patched UHT
├─ add MetaData("ScriptCallable")
├─ echo CPP_Default_* for ScriptCallable
└─ store AngelscriptPropertyFlags on FProperty
    ↓
plugin runtime
├─ Helper_FunctionSignature hoists mixins
└─ AngelscriptType reads APF_CppRef / APF_CppConst

current stock-UHT path
├─ BlueprintCallable / BlueprintPure wrappers
├─ selective ScriptMixin metadata
├─ commented legacy ScriptCallable breadcrumbs
└─ manual bind files for irregular surfaces
    ↓
AngelscriptUHTTool
├─ scan BlueprintCallable / Pure only
├─ emit AS_FunctionTable_*.cpp
└─ emit skipped-entry CSVs
    ↓
plugin runtime
├─ still accepts ScriptCallable metadata
├─ hoists mixins when ScriptMixin metadata exists
└─ uses CPF_ReferenceParm + reflective fallback
```

### 实现概述

UEAS2 的引擎补丁至少做了两件不同性质的事：

1. **把 `ScriptCallable` 做成 UHT 原生 specifier**，并让 `ScriptCallable` 函数同样写出 `CPP_Default_*` metadata。  
2. **给 `FProperty` 增加 `AngelscriptPropertyFlags`**，把 `CppRef / CppConst / RuntimeGenerated` 等语义从 authoring 层一路带到 runtime。  

current 的替代并不是简单“把所有 `ScriptCallable` 改成 `BlueprintCallable`”：

1. 对 **stock UHT 可表达的 wrapper 面**，直接改成 `BlueprintCallable / BlueprintPure`，再交给 `AngelscriptUHTTool` 导出。  
2. 对 **还能保留 class metadata 的 mixin 面**，继续用 `ScriptMixin`，例如 `FRuntimeCurveLinearColor`。  
3. 对 **不适合继续走 UHT auto-export 的面**，直接手写 bind，例如 `UAssetManager`。  
4. 对 **仍然来自 patched engine 的遗留 metadata**，runtime 继续识别 `ScriptCallable`，避免兼容性断层。  

### 关键源码引用

[1] UEAS2 的 UHT 补丁不仅添加 `ScriptCallable`，还把 `CppRef` 写进 `FProperty`：

```csharp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Programs\Shared\EpicGames.UHT\Specifiers\UhtFunctionSpecifiers.cs
// 位置: 23-29
// 说明: UEAS2 把 ScriptCallable 直接扩成了 UHT 可识别的 UFUNCTION specifier
// ============================================================================
[UhtSpecifier(Extends = UhtTableNames.Function, ValueType = UhtSpecifierValueType.Legacy)]
private static void ScriptCallableSpecifier(UhtSpecifierContext specifierContext)
{
	UhtFunction function = (UhtFunction)specifierContext.Type;
	function.MetaData.Add("ScriptCallable", "");
}
```

```csharp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Programs\Shared\EpicGames.UHT\Types\UhtFunction.cs
// 位置: 570-574
// 说明: ScriptCallable 还会触发默认参数回写 meta，和 BlueprintCallable/Exec 同级
// ============================================================================
bool storeCppDefaultValueInMetaData = FunctionFlags.HasAnyFlags(EFunctionFlags.BlueprintCallable | EFunctionFlags.Exec)
	|| MetaData.ContainsKey("ScriptCallable");
```

```csharp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Programs\Shared\EpicGames.UHT\Parsers\UhtPropertyParser.cs
// 位置: 616-617, 664-665
// 说明: 非 const ref 的 C++ 语义会继续落进 AngelscriptPropertyFlags
// ============================================================================
outProperty.AngelscriptPropertyFlags |= EAngelscriptPropertyFlags.CppConst;
...
outProperty.AngelscriptPropertyFlags |= EAngelscriptPropertyFlags.CppRef;
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Runtime\CoreUObject\Public\UObject\ObjectMacros.h
// 位置: 483-491
// 说明: 引擎层真的给 FProperty 侧开了专用语义位
// ============================================================================
enum EAngelscriptPropertyFlags : uint16
{
	APF_CppConst         = 0x0001,
	APF_CppRef           = 0x0002,
	APF_CppEnumAsByte    = 0x0004,
	APF_RuntimeGenerated = 0x0008,
	APF_WorldContext     = 0x0010,
	APF_ConstTemplateArg = 0x0020,
};
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Runtime\CoreUObject\Public\UObject\UnrealType.h
// 位置: 185-189
// 说明: 这些位不是临时变量，而是被并入 FProperty 布局
// ============================================================================
EPropertyFlags PropertyFlags;
uint16         RepIndex;
uint16         AngelscriptPropertyFlags;
```

[2] current 的 UHTTool 明确只认 `BlueprintCallable / BlueprintPure`，并把没法重建的函数写入 CSV，而不是试图复刻 UHT specifier 体系：

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs
// 位置: 56-75, 99-150
// 说明: current 的 auto-export 主轴只建立在 stock UHT 已知的 Blueprint flags 上
// ============================================================================
internal static bool IsBlueprintCallable(UhtFunction function)
{
	string functionFlags = function.FunctionFlags.ToString();

	return function.FunctionType == UhtFunctionType.Function &&
		(functionFlags.Contains("BlueprintCallable", StringComparison.Ordinal) ||
		functionFlags.Contains("BlueprintPure", StringComparison.Ordinal));
}

private static void WriteSkippedEntriesCsv(...)
{
	string csvPath = factory.MakePath("AS_FunctionTable_SkippedEntries", ".csv");
	...
	builder.AppendLine("ModuleName,ClassName,FunctionName,FailureReason");
}

private static void WriteSkippedReasonSummaryCsv(...)
{
	string csvPath = factory.MakePath("AS_FunctionTable_SkippedReasonSummary", ".csv");
	...
	builder.AppendLine("FailureReason,SkippedCount");
}
```

[3] current 的 authoring 实际是混合制，而不是单一路线：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/RuntimeCurveLinearColorMixinLibrary.h
// 位置: 8-16
// 说明: 这类 surface 仍然保留 ScriptMixin metadata，但把函数 authoring 改成 BlueprintCallable
// ============================================================================
UCLASS(meta = (ScriptMixin = "FRuntimeCurveLinearColor"))
class ANGELSCRIPTRUNTIME_API URuntimeCurveLinearColorMixinLibrary : public UObject
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category = "Math|Curves")
	static void AddDefaultKey(FRuntimeCurveLinearColor& Target, float InTime, FLinearColor InColor)
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/SubsystemLibrary.h
// 位置: 7-27
// 说明: 这类 wrapper 不再依赖 ScriptCallable，由 stock BlueprintCallable 直接进入 UHTTool 扫描面
// ============================================================================
// These functions are blueprint internal by default, but we need them exposed in Angelscript
...
//UFUNCTION(ScriptCallable, Meta = (DeterminesOutputType = "Class"))
UFUNCTION(BlueprintCallable, Meta = (DeterminesOutputType = "Class"))
static UEngineSubsystem* GetEngineSubsystem(TSubclassOf<UEngineSubsystem> Class)
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/UAssetManagerMixinLibrary.h
// 位置: 6-16
// 说明: 有些旧 mixin authoring 已经不再走 class metadata + auto-export，而是显式降成普通 UCLASS
// ============================================================================
//UCLASS(MinimalAPI, Meta = (ScriptMixin = "UAssetManager"))
UCLASS(MinimalAPI, Meta = ())
class UAssetManagerMixinLibrary : public UObject
{
	GENERATED_BODY()
public:
	//UFUNCTION(ScriptCallable)
	UFUNCTION(BlueprintCallable)
	static bool GetPrimaryAssetData(UAssetManager* AssetManager, const FPrimaryAssetId& PrimaryAssetId, FAssetData& AssetData)
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UAssetManager.cpp
// 位置: 85-105
// 说明: current 对这类 surface 直接补手写 bind，而不是强求都进入 UHTTool
// ============================================================================
auto UAssetManager_ = FAngelscriptBinds::ExistingClass("UAssetManager");

UAssetManager_.Method("FPrimaryAssetId GetPrimaryAssetIdForPath(const FSoftObjectPath& ObjectPath) const", ...);
UAssetManager_.Method("FSoftObjectPath GetPrimaryAssetPath(const FPrimaryAssetId& PrimaryAssetId) const", ...);
UAssetManager_.Method("FPrimaryAssetId GetPrimaryAssetIdForData(const FAssetData& AssetData) const", ...);
UAssetManager_.Method("int UnloadPrimaryAsset(const FPrimaryAssetId& AssetToUnload)", ...);
```

[4] current 不是“彻底丢掉 `ScriptCallable` 生态”；runtime 仍然保留对 residual metadata 的消费能力，所以 patched-engine 表面仍可被 current 识别：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp
// 位置: 1311-1314, 1403-1406
// 说明: auto-bind 主轴虽然转到 BlueprintCallable/Pure，但 runtime 仍接受 ScriptCallable metadata
// ============================================================================
else if (Function->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintPure))
	BindBlueprintCallable(InType, Function, DBMethod, *TargetName);
else if (Function->HasMetaData(NAME_ScriptCallable))
	BindBlueprintCallable(InType, Function, DBMethod, *TargetName);
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp
// 位置: 636-640
// 说明: event registry 也同样保留 ScriptCallable 的 callable 判定
// ============================================================================
if (!Function->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintPure) && !Function->HasMetaData(NAME_Event_ScriptCallable))
	FAngelscriptBinds::SetPreviousBindIsCallable(false);

GBlueprintEventsByScriptName.FindOrAdd(CastChecked<UClass>(Function->GetOuter())).Add(Signature.ScriptName, Function);
```

[5] ref/out 语义的替代也不是“完全没有”，只是 owner 从 `APF_CppRef` 收窄成了 `CPF_ReferenceParm + reflective fallback`：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp
// 位置: 252-257
// 说明: current 读取的是 stock UHT 本来就会给出的 ReferenceParm
// ============================================================================
if (Property->HasAnyPropertyFlags(CPF_ConstParm))
	Usage.bIsConst = true;

if (Property->HasAnyPropertyFlags(CPF_ReferenceParm))
	Usage.bIsReference = true;
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp
// 位置: 305-355, 374-419
// 说明: 对 auto-direct bind 失败但仍可反射调用的函数，current 会把 out-ref 在 ProcessEvent 后拷回脚本
// ============================================================================
if (Property->HasAnyPropertyFlags(CPF_ReferenceParm) && !Property->HasAnyPropertyFlags(CPF_ConstParm))
{
	OutReferences[OutReferenceCount++] = { Property, SourceAddress };
}

TargetObject->ProcessEvent(Function, ParameterBuffer);

for (int32 OutReferenceIndex = 0; OutReferenceIndex < OutReferenceCount; ++OutReferenceIndex)
{
	const FReflectiveOutReference& OutReference = OutReferences[OutReferenceIndex];
	OutReference.Property->CopySingleValue(
		OutReference.ScriptValue,
		OutReference.Property->ContainerPtrToValuePtr<void>(ParameterBuffer));
}
...
Entry.bReflectiveFallbackBound = true;
```

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| UHT 是否原生理解 `UFUNCTION(ScriptCallable)` | 是，引擎补丁直接扩 specifier | 否，auto-export 只认 `BlueprintCallable/Pure` | **实现方式不同** |
| `ScriptCallable` 表面是否仍可被 current 消费 | 上游原生支持 | runtime 继续检查 `HasMetaData(NAME_ScriptCallable)` | **没有兼容性断层** |
| mixin 是否整体消失 | 否 | 否，`RuntimeCurveLinearColor` 这类仍保留 `ScriptMixin`；但部分面改 wrapper/手写 bind | **实现方式不同，不是没有实现** |
| 通用 C++ ref 语义 owner | `APF_CppRef / APF_CppConst` 直接进 `FProperty` | `CPF_ReferenceParm` + reflective fallback | **实现方式不同，current 的 authoring 面更依赖 stock-UHT 可表达形状** |
| auto-export 失败时的可观测性 | 本轮未见同级 skipped 清单 | `AS_FunctionTable_SkippedEntries.csv` + `SkippedReasonSummary.csv` | **实现质量增强** |

这一节最重要的新结论是：**UEAS2 的引擎补丁本质上是在给 UHT 增加一门“Angelscript authoring 方言”；current 没有复制这门方言，而是把能力拆成了 `BlueprintCallable wrapper + selective mixin metadata + manual bind + runtime compatibility` 的混合流水线。**  
所以很多差距都不该写成“current 没实现”，而应该写成“**同一能力被拆成了不同 owner，表达能力边界也随 authoring 形状发生了收窄或增强**”。

---
## 深化分析 (2026-04-09 07:29:43)

本轮先复核 `.worktrees/ue-bind-gap-roadmap/AgentConfig.ini`，确认 `References.HazelightAngelscriptEngineRoot=J:\UnrealEngine\UEAS2`。前文已经把 Loader、`DebugServer V2`、UHT 替代链和大部分 Blueprint / hot-reload 主线钉住了，这一轮只补三个前文还没落到源码的细粒度 contract：删除类 tombstone 过滤、literal asset 软重载重建路径，以及 `SpawnActor` 的统一审核钩子。

### [维度 D2/D4] current 仍保留 tombstone 生产者，但 `GetAllSubclassesOf()` 已不再消费 tombstone

### 实现概述

```
[Deleted Script Class Visibility]
UEAS2
├─ CleanupRemovedClass() marks tombstone         // class-generator 打墓碑
├─ IsDeletedAngelscriptClass()                   // bind 层识别墓碑
└─ GetAllSubclassesOf() skips tombstone          // 枚举结果不暴露已删类

current
├─ CleanupRemovedClass() marks tombstone         // 仍然置空 script 指针并打隐藏标记
└─ GetAllSubclassesOf() returns tombstone too    // bind 层不再做二次过滤
```

这一组源码说明，current 不是“没有删除类清理”。`FAngelscriptClassGenerator::CleanupRemovedClass()` 仍然会把被移除的 `UASClass` 置成 tombstone；真正收缩的是**bind 层把 tombstone 再屏蔽出 class 枚举结果**的这半条 contract。

### 关键源码引用

[1] UEAS2 先在 bind 层定义 tombstone 判定，再在 `GetAllSubclassesOf()` 里显式跳过：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Binds\Bind_UObject.cpp
// 函数: IsDeletedAngelscriptClass / Bind_UClass_Base
// 位置: 202-210, 242-263
// 说明: UEAS2 把“已删除脚本类”的识别与枚举过滤都放在 bind 层
// ============================================================================
static bool IsDeletedAngelscriptClass(UClass* Class)
{
	if (UASClass* ASClass = Cast<UASClass>(Class))
	{
		// ★ tombstone 判定直接复用 class-generator 清理后的状态
		return ASClass->ScriptTypePtr == nullptr
			&& ASClass->ConstructFunction == nullptr
			&& ASClass->DefaultsFunction == nullptr
			&& ASClass->HasAllClassFlags(CLASS_Hidden | CLASS_HideDropDown | CLASS_NotPlaceable);
	}
	return false;
}

FAngelscriptBinds::BindGlobalFunction("TArray<UClass> GetAllSubclassesOf(UClass Class, bool bIncludeAbstractClasses = false)",
[](UClass* ParentClass, bool bIncludeAbstractClasses) -> TArray<UClass*>
{
	for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
	{
		UClass* Class = *ClassIt;
		...
#if WITH_EDITOR
		// ★ editor 下把 tombstone 从枚举结果里剔除
		if (IsDeletedAngelscriptClass(Class))
			continue;
#endif
		Subclasses.Add(Class);
	}
	return Subclasses;
});
```

[2] current 的 `GetAllSubclassesOf()` 保留了抽象类/弃用类过滤，但已经没有 tombstone 分支：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp
// 函数: Bind_UClass_Base
// 位置: 356-377
// 说明: current 仍有 subclass 枚举 helper，但过滤条件只剩 UE 通用标记
// ============================================================================
FAngelscriptBinds::BindGlobalFunction("TArray<UClass> GetAllSubclassesOf(UClass Class, bool bIncludeAbstractClasses = false)",
[](UClass* ParentClass, bool bIncludeAbstractClasses) -> TArray<UClass*>
{
	TArray<UClass*> Subclasses;
	for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
	{
		UClass* Class = *ClassIt;
		...
		if (Class->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists))
			continue;
		if (!bIncludeAbstractClasses && Class->HasAnyClassFlags(CLASS_Abstract))
			continue;

		if (!Class->IsChildOf(ParentClass))
			continue;

		// ★ 这里已经没有 editor-only tombstone 过滤
		Subclasses.Add(Class);
	}
	return Subclasses;
});
```

[3] 但 current 的 class-generator 仍然明确生产 tombstone：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 函数: FAngelscriptClassGenerator::CleanupRemovedClass
// 位置: 4990-5002
// 说明: 删除类时仍会置空脚本函数指针并打隐藏标记
// ============================================================================
void FAngelscriptClassGenerator::CleanupRemovedClass(TSharedPtr<FAngelscriptClassDesc> ClassDesc)
{
	UASClass* Class = (UASClass*)ClassDesc->Class;
	if (Class != nullptr)
	{
		Class->ScriptTypePtr = nullptr;
		Class->ConstructFunction = nullptr;
		Class->DefaultsFunction = nullptr;

		// ★ tombstone 标记仍在
		Class->ClassFlags |= CLASS_NotPlaceable;
		Class->ClassFlags |= CLASS_HideDropDown;
		Class->ClassFlags |= CLASS_Hidden;
	}
}
```

从这三段代码推断，current 把“删除类的 producer”保留了下来，但没有把 UEAS2 的“consumer-side shield”一起迁入。结果不是热重载删除类失效，而是**删类后仍可能通过通用 subclass 枚举 API 被看见**，需要调用方自己额外识别。

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| tombstone 标记是否仍生成 | 是，`CleanupRemovedClass` + bind 识别 | 是，`CleanupRemovedClass` 仍在 | **实现方式不同** |
| `GetAllSubclassesOf()` 是否屏蔽已删类 | 是 | 否 | **没有实现** |
| 差距性质 | producer + consumer 成对存在 | producer 还在，consumer 缺席 | **实现质量差异** |

这一条更准确的结论是：**current 不是没有“删除类”能力，而是把删除类的可见性治理收缩成了 class-generator 内部语义，没有再把它封成 bind 层通用 contract。**

### [维度 D2/D4/D11] literal asset 软重载在 current 中退成“整对象拷贝”，少了 script object 重建和 instanced-property 保护

### 实现概述

```
[Literal Asset Soft Reload]
UEAS2
├─ Existing asset found
├─ ReconstructScriptObject()                 // 先析构再重建脚本对象
├─ Copy non-instanced props from CDO         // 跳过 instanced object property
└─ Broadcast OnLiteralAssetReload

current
├─ Existing asset found
├─ Copy all props from CDO                   // 直接全属性覆盖
└─ Broadcast OnLiteralAssetReload
```

前文已经写过 literal asset 的 editor hook owner 从引擎全局函数转成标准 delegate；这一轮补的是**已有对象命中软重载分支时，object-state 重建 contract 本身**。UEAS2 在这里做了两层安全垫：先重建 script object，再避开 instanced subobject 属性；current 两层都没有保留。

### 关键源码引用

[1] UEAS2 的 `__CreateLiteralAsset()` 在命中已有对象时会先重建脚本对象，再只恢复非 instanced 属性：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Binds\Bind_UObject.cpp
// 函数: __CreateLiteralAsset helper
// 位置: 475-488
// 说明: UEAS2 的软重载分支先重建 script object，再选择性 reset 属性
// ============================================================================
else
{
	// ★ 先析构并重建脚本对象，清掉旧脚本实例状态
	if (auto* ScriptAssetClass = Cast<UASClass>(AssetClass))
		ScriptAssetClass->ReconstructScriptObject(ExistingObject);

	// ★ 再从 CDO 恢复数据，但跳过 instanced subobject/property
	auto* CDO = AssetClass->GetDefaultObject();
	for (TFieldIterator<FProperty> It(AssetClass); It; ++It)
	{
		FProperty* Prop = *It;
		if (!Prop->ContainsInstancedObjectProperty())
			Prop->CopyCompleteValue_InContainer(ExistingObject, CDO);
	}
}
```

[2] `ReconstructScriptObject()` 本身不是空壳；它显式执行析构、构造和 defaults 函数：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\ClassGenerator\ASClass.cpp
// 函数: UASClass::ReconstructScriptObject
// 位置: 1131-1139
// 说明: UEAS2 用专门 helper 把脚本对象重置回“刚构造完”的状态
// ============================================================================
void UASClass::ReconstructScriptObject(UObject* Object)
{
	if (ScriptTypePtr == nullptr)
		return;

	auto* ScriptObject = (asCScriptObject*)(Object);
	ScriptObject->CallDestructor((asCObjectType*)ScriptTypePtr);
	ExecuteConstructFunction(Object, this);
	ExecuteDefaultsFunctions(Object, this);
}
```

[3] current 的同一路径只保留了 `CDO -> ExistingObject` 的整属性拷贝：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp
// 函数: __CreateLiteralAsset helper
// 位置: 674-682
// 说明: current 在已有对象分支里没有 script-object 重建，也没有 instanced-property 过滤
// ============================================================================
else
{
	// ★ 只做属性回填，没有先重建脚本对象
	auto* CDO = AssetClass->GetDefaultObject();
	for (TFieldIterator<FProperty> It(AssetClass); It; ++It)
	{
		FProperty* Prop = *It;
		Prop->CopyCompleteValue_InContainer(ExistingObject, CDO);
	}
}
```

补充一点，`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h:13-80` 的 current 公共接口里已经看不到 `ReconstructScriptObject()`；这更像是该 helper owner 已整体退出 current，而不是 bind 层单纯漏掉一次调用。

从实现取舍看，current 的分支更直接，也更像“把对象恢复到 CDO 值”；但它不再区分 script instance 生命周期和 instanced ownership。源码层面的后果是：**脚本对象内部状态与 instanced subobject 的所有权保护都被收窄了**。

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| 软重载前是否重建 script object | 是 | 否 | **没有实现** |
| 是否跳过 `ContainsInstancedObjectProperty()` | 是 | 否 | **没有实现** |
| literal asset 主流程是否还在 | 在 | 在，`OnLiteralAssetReload` / metadata / redirector 仍保留 | **实现方式不同** |
| 总体判断 | 安全重建 contract 完整 | 主流程在，安全垫收缩 | **实现质量差异** |

更精确的表述应是：**current 没丢 literal asset 系统，但把“已有对象如何安全软重载”的 contract 压扁成了单步属性覆盖。**

### [维度 D1] `SpawnActor` 的公共扩展缝从“双 delegate”收缩成“只保留 dynamic level”

### 实现概述

```
[Spawn Policy Injection]
UEAS2
Module API
├─ GetDynamicSpawnLevel()          // 决定 override level
└─ GetVerifySpawnActor()           // 统一 spawn 审核

Bind_AActor
├─ SpawnActorFromMeta()
├─ SpawnActor()
└─ SpawnPersistentActor()
   └─ execute verify delegate before World->SpawnActor()

current
Module API
└─ GetDynamicSpawnLevel()          // 只剩 level 注入

Bind_AActor
├─ SpawnActorFromMeta()
├─ SpawnActor()
└─ SpawnPersistentActor()
   └─ direct World->SpawnActor()
```

这条差异不在“能不能 spawn actor”本身，而在**项目级 spawn policy 的统一注入缝**。UEAS2 给模块层留了两个扩展点：`level` 选择和 `pre-spawn verify`；current 只保留前者。

### 关键源码引用

[1] UEAS2 的模块公开接口明确暴露 `GetVerifySpawnActor()`：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Public\AngelscriptCodeModule.h
// 位置: 8-10, 31-32
// 说明: UEAS2 的 runtime surface 把 spawn policy 暴露成模块级 delegate
// ============================================================================
DECLARE_DELEGATE_RetVal(class ULevel*, FAngelscriptGetDynamicSpawnLevel);
DECLARE_DELEGATE_RetVal_TwoParams(bool, FAngelscriptVerifySpawnActor, struct FActorSpawnParameters&, bool);

static FAngelscriptGetDynamicSpawnLevel& GetDynamicSpawnLevel();
static FAngelscriptVerifySpawnActor& GetVerifySpawnActor();
```

[2] 三条 actor spawn 入口都会在最终 `World->SpawnActor()` 前执行这一审核 delegate：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Binds\Bind_AActor.cpp
// 函数: FAngelscriptActorBinds::SpawnActorFromMeta / SpawnActor / SpawnPersistentActor
// 位置: 240-259, 278-298, 317-325
// 说明: UEAS2 把 spawn policy 的最后一道闸门统一放在 bind 层公共 helper
// ============================================================================
FActorSpawnParameters Params;
...
if (FAngelscriptCodeModule::GetVerifySpawnActor().IsBound())
{
	// ★ 普通 spawn 与 persistent spawn 共用同一审核入口，只是第二个参数区分语义
	if (!FAngelscriptCodeModule::GetVerifySpawnActor().Execute(Params, false))
		return nullptr;
}
return World->SpawnActor(Class, &Location, &Rotation, Params);

...
if (FAngelscriptCodeModule::GetVerifySpawnActor().IsBound())
{
	if (!FAngelscriptCodeModule::GetVerifySpawnActor().Execute(Params, true))
		return nullptr;
}
return World->SpawnActor(Class, &Location, &Rotation, Params);
```

[3] current 的模块接口只剩 `GetDynamicSpawnLevel()`，没有同位 `VerifySpawnActor` delegate：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h
// 位置: 9-10, 32-33
// 说明: current 保留 level 注入，但模块 public surface 已无 spawn 审核扩展点
// ============================================================================
DECLARE_DELEGATE_RetVal(class ULevel*, FAngelscriptGetDynamicSpawnLevel);
DECLARE_DELEGATE_RetVal_TwoParams(bool, FAngelscriptDebugCheckBreakOptions, const FAngelscriptDebugBreakOptions&, UObject*);

static FAngelscriptGetDynamicSpawnLevel& GetDynamicSpawnLevel();
static FAngelscriptDebugCheckBreakOptions& GetDebugCheckBreakOptions();
```

[4] current 的三条 spawn helper 在参数组装后直接进入 `World->SpawnActor()`：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp
// 函数: FAngelscriptActorBinds::SpawnActorFromMeta / SpawnActor / SpawnPersistentActor
// 位置: 183-196, 215-229, 248-253
// 说明: current 仍保留 level 注入，但没有统一 verify gate
// ============================================================================
FActorSpawnParameters Params;
Params.Name = Name;
Params.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
...
if (Level != nullptr)
	Params.OverrideLevel = Level;
else if (World->IsGameWorld() && FAngelscriptRuntimeModule::GetDynamicSpawnLevel().IsBound())
	Params.OverrideLevel = FAngelscriptRuntimeModule::GetDynamicSpawnLevel().Execute();

// ★ 参数组装完后直接 spawn，没有额外审核
return World->SpawnActor(Class, &Location, &Rotation, Params);
```

补充扫描结果：对 `Plugins/Angelscript/` 全仓库搜索 `VerifySpawnActor`、`FAngelscriptVerifySpawnActor`、`GetVerifySpawnActor` 均无命中，因此这不是“接口换名”，而是 public seam 的确已经被拿掉。

从模块设计取舍看，current 的接口面更窄，宿主集成成本也更低；但代价是**项目侧不能再用一个模块级 delegate 统一审核所有脚本 spawn**。如果仍需要同类策略，只能改 bind 本身，或在更外层另找 owner。

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| `GetDynamicSpawnLevel()` | 有 | 有 | **实现方式相同** |
| `GetVerifySpawnActor()` | 有，三条 spawn helper 共用 | 无同位入口 | **没有实现** |
| `SpawnActor` 基本功能 | 有 | 有 | **没有功能断层** |
| 模块 public seam | level + verify 双入口 | 只剩 level | **实现质量差异 / seam 收缩** |

因此这一条不该写成“current 不支持 spawn actor”，而应写成：**spawn 功能还在，但 UEAS2 那个项目级、统一的 pre-spawn policy hook 已经没有等价 owner。**

---
## 深化分析 (2026-04-09 07:40:53)

### [维度 D9] `AngelscriptTest` 不是对 UEAS2 runtime-native test harness 的替换，而是叠加在其外的一层验证壳

### 实现概述

```
[D9] Test Ownership Layers
UEAS2
├─ AngelscriptCode runtime
│  ├─ DiscoverTests() after initial asset scan   // runtime 内发现脚本测试
│  ├─ IntegrationTest.cpp                        // integration/latent 主入口
│  └─ LatentAutomationCommandClientExecutor      // client/server 复制执行器
└─ runtime owns discovery + latent/network harness

current
├─ AngelscriptRuntime
│  ├─ DiscoverTests() after initial asset scan   // 仍在 runtime 内发现
│  ├─ PrepareAngelscriptContextWithLog()         // 复杂测试发现时补显式 prepare 诊断
│  └─ LatentAutomationCommandClientExecutor      // 同类复制执行器仍在 runtime/Testing
└─ AngelscriptTest
   ├─ depends on Runtime + Editor + CQTest       // 额外验证层
   └─ hosts dump/native/bind/debugger scenarios  // 主题化自动化回归
```

前文已经写过 current 新增 `AngelscriptTest` 模块，但如果只看模块数量，很容易误判成“UEAS2 的脚本测试系统已经整体搬出 runtime”。源码不支持这个结论。  
更准确的 owner 变化是：**runtime-native discovery / latent / network harness 被保留下来，`AngelscriptTest` 负责把原来更零散的验证需求组织成独立模块和主题化 suite。**

### 关键源码引用

[1] UEAS2 的 integration test 入口直接包含 latent/network helper，并在 runtime 内部生成复制执行 actor：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Testing\IntegrationTest.cpp
// 函数: 文件头 include / latent client executor spawn
// 位置: 27-30, 549-554
// 说明: UEAS2 的 integration harness 完全属于 runtime 测试内核，不依赖额外测试模块
// ============================================================================
#include "Testing/AngelscriptTest.h"
#include "Testing/IntegrationTestTerminator.h"
#include "Testing/LatentAutomationCommand.h"
#include "Testing/LatentAutomationCommandClientExecutor.h"
...
FActorSpawnParameters SpawnParms;
SpawnParms.Owner = Controller;
// ★ runtime 直接在测试世界里生成复制执行器，负责 client/server latent 协调
ClientExecutor = GetTestWorld()->SpawnActor<ALatentAutomationCommandClientExecutor>(
	ALatentAutomationCommandClientExecutor::StaticClass(), SpawnParms);
ClientExecutor->SetTest(&LatentCommand);
```

[2] current 保留了同一条 runtime 发现链，但在复杂测试枚举时补上了显式 prepare 日志：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: FAngelscriptEngine::Initialize / DiscoverTests
// 位置: 2201-2208, 2232-2247
// 说明: current 仍在 runtime 内做脚本测试发现，只是 owner 从 manager 收到 engine
// ============================================================================
FCoreDelegates::OnPostEngineInit.AddLambda([&]()
{
	UAssetManager* AssetManager = UAssetManager::GetIfInitialized();
	if (AssetManager != nullptr)
	{
		AssetManager->CallOrRegister_OnCompletedInitialScan(
			FSimpleMulticastDelegate::FDelegate::CreateLambda([&]() {
				// ★ 初始资源扫描结束后仍由 runtime 自己发现脚本测试
				DiscoverTests();
				bCompletedAssetScan = true;
			})
		);
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

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Testing/DiscoverTests.cpp
// 函数: RegisterComplexFunctions
// 位置: 135-145
// 说明: current 不是简单平移 UEAS2 代码；复杂测试发现时新增 prepare 失败可诊断性
// ============================================================================
TArray<FString> OutTestCommands;
FAngelscriptContext AngelscriptContext(GetTestFunction->GetEngine());
if (!PrepareAngelscriptContextWithLog(AngelscriptContext, GetTestFunction, *GetTestFunctionName))
{
	// ★ prepare 失败时直接把错误回钉到脚本声明位置，而不是静默执行空上下文
	ComplexTestScriptCompileError(
		Module,
		GetTestFunction,
		FString::Printf(TEXT("During test discovery I failed to prepare %s for execution."), *GetTestFunctionName));
	continue;
}
AngelscriptContext->SetArgAddress(0, (void*)&OutTestCommands);
AngelscriptContext->Execute();
```

[3] 与此同时，current 确实新增了独立 `AngelscriptTest` 模块，把 dump/native/bind 场景正式模块化：

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs
// 位置: 23-49
// 说明: Test 模块是 current 新增的“外层验证环”，不是 runtime harness 的 owner
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

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Dump/AngelscriptDumpTests.cpp
// 位置: 24-32, 45-75
// 说明: Test 模块承接的是更外层的产物验证，而不是替代 runtime 内建 discovery
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptStateDumpEndToEndTest,
	"Angelscript.TestModule.Dump.DumpAll.EndToEnd",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptStateDumpSummaryTest,
	"Angelscript.TestModule.Dump.DumpAll.Summary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

TArray<FString> GetExpectedPhaseOneCsvFiles()
{
	return {
		TEXT("EngineOverview.csv"),
		TEXT("RuntimeConfig.csv"),
		...
		TEXT("CodeCoverage.csv"),
		TEXT("EditorReloadState.csv"),
		TEXT("EditorMenuExtensions.csv"),
		TEXT("DumpSummary.csv")
	};
}
```

### 设计取舍

UEAS2 的优点是测试内核和 runtime 非常贴近，发现、执行、网络同步都在一个 owner 下；缺点是高层回归组织和产物验证比较分散。  
current 没有放弃这层 runtime-native harness，而是做了两件更细的事：

1. 保留 runtime 内 discovery / latent / network 骨架，避免 script 测试能力和引擎生命周期脱节。
2. 把更偏交付验证的 suite 抽到 `AngelscriptTest`，让 dump、绑定、debugger、native SDK 等主题不再和 runtime 内核文件搅在一起。

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| 脚本测试发现 owner | `AngelscriptCode::DiscoverTests()` | `AngelscriptRuntime::DiscoverTests()` | **实现方式不同** |
| latent/network harness | runtime 内 `LatentAutomationCommandClientExecutor` | runtime 内同位 helper 仍保留 | **没有实现差距** |
| 复杂测试发现诊断 | 直接 `Prepare()` | `PrepareAngelscriptContextWithLog()` + compile error 回钉 | **实现质量增强** |
| 额外验证层 | 无独立 test module | `AngelscriptTest` 主题化 suite | **能力增强** |

这一维度更准确的结论是：**current 的测试架构不是“把 UEAS2 runtime 测试能力外包给新模块”，而是“保留 runtime-native harness，同时额外叠加独立验证环”。**

### [维度 D8/D9] code coverage 的 recorder owner 没变，但 current 把它变成了“HTML/JSON + CSV dump + summary status”的双轨观测链

### 实现概述

```
[D8/D9] Coverage Observation Flow
UEAS2
AutomationController
└─ FAngelscriptCodeCoverage
   ├─ StartRecording / HitLine
   └─ WriteReportHtml + WriteCoverageSummaries   // 产出 Saved/CodeCoverage

current
AutomationController
└─ FAngelscriptCodeCoverage
   ├─ StartRecording / HitLine
   ├─ WriteReportHtml + WriteCoverageSummaries   // 保留上游 HTML/JSON
   └─ FAngelscriptStateDump::DumpCodeCoverage    // 追加 CSV 观察面
      └─ AngelscriptDumpTests assert DumpSummary // 把“有/无覆盖率 recorder”写成状态契约
```

如果只看 `FAngelscriptCodeCoverage` 本体，current 和 UEAS2 几乎是同一套 recorder：都挂到 `AutomationController` 的测试开始/结束事件上，都在测试结束后生成 HTML/summary。  
真正新增的是 **observer layer**。current 在 runtime dump 里追加 `CodeCoverage.csv`，并把“当前没有 coverage recorder”从过去的隐式情况提升成 `DumpSummary.csv` 里可断言的 `Skipped` 状态。

### 关键源码引用

[1] UEAS2 的 coverage owner 仍然是 runtime；测试结束时只写标准报告目录：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\CodeCoverage\AngelscriptCodeCoverage.cpp
// 函数: FAngelscriptCodeCoverage::AddTestFrameworkHooks / OnTestsStopping / StopRecordingAndWriteReport
// 位置: 21-40, 58-64
// 说明: UEAS2 的覆盖率 recorder 与 AutomationController 紧耦合，但输出面只有 HTML/summary
// ============================================================================
void FAngelscriptCodeCoverage::AddTestFrameworkHooks()
{
	IAutomationControllerManagerRef AutomationController = AutomationModule.GetAutomationController();
	AutomationController->OnTestsAvailable().AddRaw(this, &FAngelscriptCodeCoverage::OnTestsStarting);
	AutomationController->OnTestsComplete().AddRaw(this, &FAngelscriptCodeCoverage::OnTestsStopping);
}

void FAngelscriptCodeCoverage::OnTestsStopping()
{
	FString OutputDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("CodeCoverage"));
	// ★ 测试结束后直接写报告目录，没有额外 CSV state dump owner
	StopRecordingAndWriteReport(OutputDir);
}

void FAngelscriptCodeCoverage::StopRecordingAndWriteReport(const FString& OutputDir)
{
	WriteReportHtml(OutputDir);
	WriteCoverageSummaries(OutputDir);
}
```

[2] current 保留这条 recorder 主线，并在 engine 初始化时显式挂上 test hooks：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: FAngelscriptEngine::Initialize
// 位置: 1459-1463, 1627-1633
// 说明: current 没把 coverage 移出 runtime；只是把 owner 从 manager 改成 engine
// ============================================================================
#if WITH_AS_COVERAGE
if (FAngelscriptCodeCoverage::CoverageEnabled())
{
	// ★ recorder 仍由 runtime engine 持有
	CodeCoverage = new FAngelscriptCodeCoverage;
}
#endif

#if WITH_EDITOR && WITH_AS_COVERAGE
FCoreDelegates::OnPostEngineInit.AddLambda([&]()
{
	if (CodeCoverage != nullptr)
	{
		// ★ 仍挂到 AutomationController 生命周期
		CodeCoverage->AddTestFrameworkHooks();
	}
});
#endif
```

[3] current 新增 `DumpCodeCoverage()`，把 hit-line 结果导出成 `CodeCoverage.csv`；无 recorder 时也输出显式 `Skipped`：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp
// 函数: FAngelscriptStateDump::DumpCodeCoverage
// 位置: 1173-1217
// 说明: 这是 current 新增的 observer layer，不改变 recorder owner，但改变外部可见性
// ============================================================================
FAngelscriptStateDump::FTableResult FAngelscriptStateDump::DumpCodeCoverage(FAngelscriptEngine& Engine, const FString& OutputDir)
{
	FCSVWriter Writer;
	Writer.AddHeader({ TEXT("Filename"), TEXT("LineNumber"), TEXT("HitCount") });

#if WITH_AS_COVERAGE
	if (Engine.CodeCoverage != nullptr)
	{
		for (const TSharedRef<FAngelscriptModuleDesc>& Module : Engine.GetActiveModules())
		{
			const FLineCoverage* Coverage = Engine.CodeCoverage->GetLineCoverage(*Module);
			...
			// ★ 只把真正命中的行写出成 CSV，便于做外部统计或 diff
			Writer.AddRow({ Coverage->AbsoluteFilename, LexToString(HitPair.Key), LexToString(HitPair.Value) });
		}
		return SaveTable(OutputDir, TEXT("CodeCoverage.csv"), Writer);
	}
#endif

	FTableResult Result = SaveTable(OutputDir, TEXT("CodeCoverage.csv"), Writer);
	if (Result.Status == TEXT("Success"))
	{
		// ★ coverage 不可用时不再“什么都没有”，而是显式写成 Skipped
		Result.Status = TEXT("Skipped");
		Result.ErrorMessage = TEXT("Code coverage support is not compiled or no coverage recorder is active.");
	}
	return Result;
}
```

[4] `AngelscriptTest` 随后把这条状态契约锁成自动化回归，而不是只靠人工看 `Saved/CodeCoverage`：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Dump/AngelscriptDumpTests.cpp
// 位置: 45-75, 78-95
// 说明: current 新增的是“coverage 产物的可验证性”，而不只是 recorder 本身
// ============================================================================
TArray<FString> GetExpectedPhaseOneCsvFiles()
{
	return {
		...
		TEXT("CodeCoverage.csv"),
		...
		TEXT("DumpSummary.csv")
	};
}

FString GetExpectedSummaryStatus(const FString& TableName)
{
	if (TableName == TEXT("CodeCoverage.csv"))
	{
		// ★ 没有 coverage recorder 也必须以 Skipped 明确落表
		return TEXT("Skipped");
	}
	return TEXT("Success");
}
```

### 设计取舍

UEAS2 的好处是 recorder 简单直接，测试结束就落 HTML/JSON 报告；代价是外部工具如果只想做“本轮脚本行命中 diff”或“把 coverage 纳入统一 dump 管线”，还要自己再解析报告目录。  
current 的 recorder 实现并没有本质改写，它新增的是一层更适合插件交付和自动化回归的观测壳：

1. 保留原始 HTML/summary 报告，兼容 UEAS2 的人工查看方式。
2. 追加 CSV dump 和 `Skipped` 状态，让 coverage 进入统一导出矩阵。

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| coverage recorder owner | runtime | runtime | **没有实现差距** |
| 测试生命周期挂钩 | `AutomationController` | `AutomationController` | **实现方式相同** |
| 行级 CSV 导出 | 未见同位 dump owner | `DumpCodeCoverage()` | **能力增强** |
| “未启用 coverage”可观测性 | 隐式缺席 | `DumpSummary.csv` 中显式 `Skipped` | **实现质量增强** |

因此本维度最准确的表述是：**current 没有把 coverage 机制重写成另一套系统；它新增的是“coverage 作为统一状态导出产物”的外部 contract。**

### [维度 D2/D6/D11] `BindDatabase` 的 cooked naming contract 已从“全局反查表”收缩成“局部 `GeneratedName`”

### 实现概述

```
[D2/D6/D11] Bind DB Naming Contract
UEAS2
├─ Serialize(Structs, Classes, BindDatabaseNamingData)
├─ Load() rebuilds BindingNameUnrealPaths
├─ GetPropertyBoundName / GetClassBoundName
└─ GetNamingData                              // generic cooked reverse-lookup

current
├─ engine-scoped BindDatabase::Get()
├─ Serialize(Structs, Classes) only
├─ accessor binds persist DBProp.GeneratedName
└─ consumers read local GeneratedName / Declaration
   // no generic reverse-lookup API for arbitrary class/property names
```

前文已经多次写过 `BindDatabase` 会参与 cooked 绑定和 source-header 恢复，但还没把它的 **命名缓存 contract** 单独拆出来。  
这条链在 current 中有一个很实质的收缩：UEAS2 会把“Unreal path -> script name”的反查关系和一些 naming data 一起持久化；current 则把这层全局缓存删掉，只在真正需要 cooked-time 名称恢复的 accessor property 上保留 `GeneratedName`。

### 关键源码引用

[1] UEAS2 的 `BindDatabase` 明确序列化 naming data，并公开通用反查 API：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Public\AngelscriptBindDatabase.h
// 位置: 136-148
// 说明: UEAS2 把 header 链接、路径到脚本名映射、额外 naming data 都作为 BindDatabase 的公共 surface
// ============================================================================
TArray<FAngelscriptStructBind> Structs;
TArray<FAngelscriptClassBind> Classes;
TArray<UEnum*> BoundEnums;
TArray<class UDelegateFunction*> BoundDelegateFunctions;

TMap<UObject*, FString> HeaderLinks;
TMap<FStringView, FStringView> BindingNameUnrealPaths;
TMap<FString, FString> BindDatabaseNamingData;

static FString GetSourceHeader(UField* Field);
static FString GetPropertyBoundName(FProperty* Property);
static FString GetClassBoundName(UClass* Class);
static FString GetNamingData(FString Key, FString EditorName);
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\AngelscriptBindDatabase.cpp
// 函数: Serialize / Load / GetPropertyBoundName / GetNamingData
// 位置: 16-21, 113-127, 169-176, 199-206
// 说明: cooked 时可以用全局缓存把 Unreal path 反推回脚本名称或其他 naming data
// ============================================================================
void FAngelscriptBindDatabase::Serialize(FArchive& Archive)
{
	Archive << Structs;
	Archive << Classes;
	Archive << BindDatabaseNamingData;
}
...
for (auto& DBStruct : Structs)
{
	BindingNameUnrealPaths.Add(DBStruct.UnrealPath, DBStruct.TypeName);
	for (auto& Property : DBStruct.Properties)
		BindingNameUnrealPaths.Add(Property.UnrealPath, Property.PropertyName);
}
...
FStringView* BoundName = FAngelscriptBindDatabase::Get().BindingNameUnrealPaths.Find(Property->GetPathName());
...
if (FString* Value = FAngelscriptBindDatabase::Get().BindDatabaseNamingData.Find(Key))
	return *Value;
```

[2] current 的 `BindDatabase` 改成 engine-scope，并把序列化面收缩到 `Structs/Classes`：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h
// 位置: 123-141
// 说明: current 去掉了 generic naming cache，只保留 bind 条目本身和 header links
// ============================================================================
class FAngelscriptBindDatabase
{
public:
	static FAngelscriptBindDatabase& Get();
	...
	TArray<FAngelscriptStructBind> Structs;
	TArray<FAngelscriptClassBind> Classes;
	TArray<UEnum*> BoundEnums;
	TArray<UDelegateFunction*> BoundDelegateFunctions;

	TMap<UObject*, FString> HeaderLinks;
	static FString GetSourceHeader(UField* Field);
private:
	void Serialize(FArchive& Archive);
};
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp
// 函数: Get / Serialize
// 位置: 14-25, 27-31
// 说明: current 的 BindDatabase 首先服务于 engine 隔离，其次才是 cooked 反查
// ============================================================================
FAngelscriptBindDatabase& FAngelscriptBindDatabase::Get()
{
	if (FAngelscriptEngine* Engine = FAngelscriptEngine::TryGetCurrentEngine())
	{
		if (FAngelscriptBindDatabase* DB = Engine->GetBindDatabase())
		{
			// ★ 先按当前 engine 取库；拿不到才回落到 legacy
			return *DB;
		}
	}
	static FAngelscriptBindDatabase LegacyBindDatabase;
	return LegacyBindDatabase;
}

void FAngelscriptBindDatabase::Serialize(FArchive& Archive)
{
	Archive << Structs;
	Archive << Classes;
}
```

[3] current 没有把“名字”完全丢掉，而是把 owner 缩到 accessor bind 条目本身：只有 getter/setter 需要 cooked-time 名称恢复时才写 `GeneratedName`，消费端也只在该分支读取它。

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp
// 位置: 1253-1257, 1266-1270, 794-797
// 说明: current 只为 generated getter/setter 显式持久化名字；简单属性声明不再额外存名
// ============================================================================
if (!Property->HasAnyPropertyFlags(CPF_EditorOnly) && (DBProp.bGeneratedGetter || DBProp.bGeneratedSetter))
{
	DBProp.Declaration = AccessorType;
	DBProp.GeneratedName = PropertyName;
	DBProperties.Add(DBProp);
}
...
DBProp.Declaration = Declaration;
// ★ 简单声明分支不再单独写 PropertyName/GeneratedName
if (!Property->HasAnyPropertyFlags(CPF_EditorOnly))
	DBProperties.Add(DBProp);
...
const FString& PropertyName = DBProp.GeneratedName;
const FString& PropertyType = DBProp.Declaration;
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Binds\Bind_BlueprintType.cpp
// 位置: 1303-1318, 771-774
// 说明: UEAS2 在 generated accessor 和简单声明两条分支都保留 PropertyName
// ============================================================================
if (!Property->HasAnyPropertyFlags(CPF_EditorOnly) && (DBProp.bGeneratedGetter || DBProp.bGeneratedSetter))
{
	DBProp.Declaration = AccessorType;
	DBProp.PropertyName = PropertyName;
	DBProperties.Add(DBProp);
}
...
DBProp.Declaration = Declaration;
DBProp.PropertyName = PropertyName;
...
const FString& PropertyName = DBProp.PropertyName;
const FString& PropertyType = DBProp.Declaration;
```

按当前消费者代码推断，这不是 accidental omission。current 更像是明确做了 owner 收缩：  
只有需要 cooked-time 二次拼装 getter/setter 名称的条目才保留 `GeneratedName`；普通成员变量则让“名字”继续附着在 `Declaration` 或生成当下的 bind 逻辑里，不再提供对任意 `UClass/FProperty` 的全局反查 API。

### 设计取舍

UEAS2 的收益是通用：任意调用点只要拿到 `UClass/FProperty`，就能通过 `BindDatabase` 查询脚本名和部分 naming data。  
current 的收益是边界更窄、更容易 engine-scope 化；代价则是 **generic introspection surface 变窄**，以后任何还想在 cooked 环境里做“随处拿到 `FProperty*` 就反推脚本名”的功能，都不能再直接依赖一个全局 helper。

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| bind database owner | 进程级 singleton | engine-scope + legacy fallback | **实现方式不同** |
| 通用 `GetPropertyBoundName / GetClassBoundName / GetNamingData` | 有 | 无同位 API | **没有实现** |
| generated accessor 命名持久化 | `PropertyName` 字段 | `GeneratedName` 字段 | **实现方式不同** |
| 普通属性名的 cooked 反查 | 全局 cache 可用 | 依赖局部 declaration/consumer 语义 | **实现质量差异 / surface 收缩** |

这一节最值得记住的新结论是：**current 并不是不再保存任何绑定名字，而是把 UEAS2 那种“全局可反查”的 naming contract，收缩成了“只有局部 consumer 真需要时才带名字”的更窄接口面。**

---
## 深化分析 (2026-04-09 07:50:53)

这一轮不再扩写前面已经多次展开的 `Loader` / `UHTTool` 主线，而是补三条此前没有单独钉死、但会直接影响 authoring contract 与交付边界判断的窄链路：`StaticClass` / `TSubclassOf`、`ScriptAsset` round-trip 编辑入口、以及 editor-only script package 的显式分类规则。

### [维度 D2/D3/D9] `StaticClass()` / `TSubclassOf<>` 兼容链没有退化；current 主要新增的是显式回归壳

### 实现概述

```
[D2/D3/D9] StaticClass / TSubclassOf Compat
Script class authoring
├─ Preprocessor emits `__StaticType_*`                // 生成脚本类静态句柄
├─ optional `Namespace::Foo::StaticClass()`          // 语法糖仍然保留
└─ `TSubclassOf<Foo>` assignment
   └─ `Bind_TSubclassOf::SetClass()`
      ├─ native `IsChildOf` check                    // 常规父子类校验
      ├─ `UASClass` hotreload branch                 // 允许 stale script class 过渡
      └─ `GetMostUpToDateClass()->IsChildOf(...)`

current only
└─ `AngelscriptClassBindingsTests`
   ├─ plain module path                              // 普通模块语法
   └─ annotated `UCLASS()` path                      // 生成类语法
```

前文已经多次分析 script class / `UASClass` / mixin 主线，但还没把 `StaticClass()` 与 `TSubclassOf<>` 这条作者体验 contract 单独拆出来。  
这一轮的新结论是：**current 并没有把 script class authoring 退回“只能手写 `UClass`”**。preprocessor 仍会为脚本类生成 `const TSubclassOf<UObject>` 句柄和可选的 `Namespace::Class::StaticClass()` 包装；运行时 `Bind_TSubclassOf` 仍保留对 stale `UASClass` 的 hot-reload 容忍分支。current 的真实增量是把这条 contract 补进了插件级自动化回归，而不只是留在 bind helper 里“默认应该工作”。

### 关键源码引用

[1] preprocessor 仍把脚本类的静态入口建在 `TSubclassOf` 之上，而不是退回裸 `UClass*`：

```cpp
// ============================================================================
// 文件: References.HazelightAngelscriptEngineRoot/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Preprocessor/AngelscriptPreprocessor.cpp
// 位置: 796-824
// 说明: UEAS2 为脚本类生成 `const TSubclassOf<UObject>` 和可选 `StaticClass()` 包装
// ============================================================================
if (ConfigSettings->StaticClassDeprecation == EAngelscriptDeprecationMode::Disallowed)
{
	File.GeneratedCode.Add(FString::Printf(TEXT("const TSubclassOf<UObject> %s;"), *ClassVar));
}
else
{
	File.GeneratedCode.Add(FString::Printf(
		TEXT("const TSubclassOf<UObject> %s; namespace %s { UClass StaticClass() __generated%s { return %s; } }"),
		*ClassVar, *ClassDesc->ClassName, *FunctionSpecifiers, *ClassVar));
}

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp
// 位置: 868-896
// 说明: current 仅把配置枚举改成 `EAngelscriptStaticClassMode`，生成物语义未变
// ============================================================================
if (ConfigSettings->StaticClassDeprecation == EAngelscriptStaticClassMode::Disallowed)
{
	File.GeneratedCode.Add(FString::Printf(TEXT("const TSubclassOf<UObject> %s;"), *ClassVar));
}
else
{
	File.GeneratedCode.Add(FString::Printf(
		TEXT("const TSubclassOf<UObject> %s; namespace %s { UClass StaticClass() __generated%s { return %s; } }"),
		*ClassVar, *ClassDesc->ClassName, *FunctionSpecifiers, *ClassVar));
}
```

[2] `TSubclassOf<>` 赋值路径仍保留 script-class aware 的 hot-reload 容忍分支；current 与 UEAS2 的差异主要只是 owner 从 `Manager` 换成 `Engine`：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSubclassOf.h
// 位置: 60-91
// 说明: current 仍允许“模板类已经更新，但资产类还没 reinstance 完”的短暂过渡状态
// 同位参考: References.HazelightAngelscriptEngineRoot/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/AngelscriptBinds/Bind_TSubclassOf.h:58-89
// ============================================================================
static void SetClass(TSubclassOf<UObject>* Ptr, asCObjectType* TemplateType, UClass* InClass)
{
	if (InClass == nullptr)
	{
		*Ptr = nullptr;
		return;
	}

	UClass* TemplateClass = TGetTypeInfo<UClass>::FromPropertyType(TemplateType->GetSubTypeId());

	bool bWillBecomeCorrect = false;
#if AS_CAN_HOTRELOAD
	// ★ 允许 stale script class 在 hot reload 过渡窗口里先通过
	if (auto* ASTemplate = Cast<UASClass>(TemplateClass))
	{
		if (UASClass* ASAsset = UASClass::GetFirstASClass(InClass))
		{
			if (ASAsset->GetMostUpToDateClass()->IsChildOf(ASTemplate))
				bWillBecomeCorrect = true;
		}
	}
#endif

	if (InClass->IsChildOf(TemplateClass) || bWillBecomeCorrect)
		*Ptr = InClass;
	else
		FAngelscriptEngine::Throw("Class set to TSubclassOf<> was not a child of templated class.");
}
```

[3] current 不只保留实现，还专门给 plain module 与 annotated `UCLASS()` 两条 authoring 路径补了自动化测试：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp
// 位置: 15-17, 105-139, 317-375
// 说明: current 显式回归 `TSubclassOf` 与 `StaticClass()` 的 plain/annotated 双路径
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptTSubclassOfBindingsTest,
	"Angelscript.TestModule.Bindings.TSubclassOfCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

TSubclassOf<AActor> ImplicitFromClassArg = EchoSubclass(AActor::StaticClass());
if (!(ImplicitFromClassArg == AActor::StaticClass()))
	return 55;

const bool bAnnotatedCompiled = CompileAnnotatedModuleFromMemory(
	&Engine,
	TEXT("ASAnnotatedStaticClassCompat"),
	TEXT("ASAnnotatedStaticClassCompat.as"),
	TEXT(R"(
UCLASS()
class ABindingStaticClassActor : AActor
{
	UFUNCTION()
	int ReadStaticClassCompat()
	{
		UClass SelfClass = ABindingStaticClassActor::StaticClass();
		TSubclassOf<ABindingStaticClassActor> CompatClass = ABindingStaticClassActor::StaticClass();
		...
	}
})"));

if (!TestEqual(TEXT("Plain module StaticClass and TSubclassOf compat syntax should behave as expected"), PlainResult, 1))
	return false;
if (!TestEqual(TEXT("Annotated module StaticClass and TSubclassOf compat syntax should behave as expected"), AnnotatedResult, 1))
	return false;
```

### 设计取舍

把 script class 的静态入口继续绑定在 `TSubclassOf<>` 上，而不是退回 `UClass*`，收益是 authoring surface 不会因为去引擎补丁化而突然变丑：脚本类、原生类、annotated `UCLASS()` 生成类都还能走同一套 `StaticClass()` / implicit convert 语法。  
代价是 bind 层必须继续知道 `UASClass` 的 hot-reload 过渡态，而 current 为了降低回归风险，额外付出了一层测试维护成本。

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| preprocessor 是否继续生成 `const TSubclassOf<UObject>` 静态入口 | 是 | 是 | **实现方式相同** |
| `StaticClass()` 语法是否仍建立在这条静态入口上 | 是 | 是 | **实现方式相同** |
| hot reload 过渡态下的 script-class `TSubclassOf` 容忍分支 | 是 | 是 | **实现方式相同** |
| plugin-level 自动化回归 | 本轮引用片段中未体现 | `AngelscriptClassBindingsTests.cpp` 显式覆盖 plain + annotated 两路 | **实现质量增强** |

### [维度 D4/D7/D11] `ScriptAsset` 的 round-trip 编辑入口没有断代；变的是 owner，不是 workflow

### 实现概述

```
[D4/D7/D11] ScriptAsset Roundtrip
Content Browser / Asset Save
├─ `UScriptAssetMenuExtension`                       // 资产菜单入口
├─ editor serializes modified literal asset         // 曲线/文本导出为行数组
├─ `ReplaceScriptAssetContent(Name, Lines)`         // 回推脚本文本
│  └─ `ReplaceAssetDefinition` message              // 通过 debug channel 分发
└─ runtime asset namespace
   ├─ `/Script/AngelscriptAssets`                   // 固定包名
   ├─ `Asset_<Name>` redirector                     // 兼容旧路径
   └─ `ScriptAssetFilename/LineNumber` metadata     // 源码定位
```

前文已经分析过 literal asset soft reload 的对象重建差异；这一节只补 **round-trip 交付链** 本身。  
新增结论是：**current 仍完整保留 UEAS2 的 `ScriptAsset` 编辑入口组合**。Content Browser 侧的 `UScriptAssetMenuExtension` 基本原样保留；asset save 侧仍把曲线/字面量内容序列化成 `TArray<FString>`，再经 `ReplaceScriptAssetContent()` 发送 `ReplaceAssetDefinition` 调试消息；运行时侧仍把资产放在 `/Script/AngelscriptAssets`，并继续为旧路径留 redirector 与源码定位 metadata。  
也就是说，这条链不是“UEAS2 的 engine-fork 特权”，而是本来就在插件 owner 里。current 只是把总 owner 从 `FAngelscriptManager` 收口到了 `FAngelscriptEngine`。

### 关键源码引用

[1] Content Browser 入口类本身几乎没有变化：仍然是 `ContentBrowser_AssetViewContextMenu` + `CommonAssetActions` + `SupportedClasses` 过滤：

```cpp
// ============================================================================
// 文件: References.HazelightAngelscriptEngineRoot/Engine/Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptAssetMenuExtension.h
// 位置: 7-26
// 文件: References.HazelightAngelscriptEngineRoot/Engine/Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptAssetMenuExtension.cpp
// 位置: 5-43
// 说明: UEAS2 的 asset context menu 入口
// ============================================================================
class UScriptAssetMenuExtension : public UScriptEditorMenuExtension
{
public:
	UScriptAssetMenuExtension()
	{
		ExtensionMenu = EScriptEditorMenuExtensionLocation::ContentBrowser_AssetViewContextMenu;
		ExtensionPoint = "CommonAssetActions";
	}

	UPROPERTY(EditDefaultsOnly, Category = "Menu Extension")
	TArray<TSubclassOf<UObject>> SupportedClasses;

	virtual TArray<UFunction*> GatherExtensionFunctions() const;
	virtual void CallFunctionOnSelection(UFunction* Function, FExtenderSelection Selection) const;
};

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptAssetMenuExtension.h
// 位置: 6-25
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptAssetMenuExtension.cpp
// 位置: 4-41, 76-81
// 说明: current 仍按 `SupportedClasses` 过滤资产并走同一套 context menu 扩展点
// ============================================================================
class UScriptAssetMenuExtension : public UScriptEditorMenuExtension
{
public:
	UScriptAssetMenuExtension()
	{
		ExtensionMenu = EScriptEditorMenuExtensionLocation::ContentBrowser_AssetViewContextMenu;
		ExtensionPoint = "CommonAssetActions";
	}

	UPROPERTY(EditDefaultsOnly, Category = "Menu Extension")
	TArray<TSubclassOf<UObject>> SupportedClasses;
};

if (SupportedClasses.Num() != 0)
{
	bool bIsSupportedClass = false;
	for (auto SupportedClass : SupportedClasses)
	{
		if (Asset.IsInstanceOf(SupportedClass.Get()))
		{
			bIsSupportedClass = true;
		}
	}
}
```

[2] editor save 侧仍然把修改后的 literal asset 内容回推到 `ReplaceScriptAssetContent()`；区别只是 owner 从 `Manager` 换成 `Engine`：

```cpp
// ============================================================================
// 文件: References.HazelightAngelscriptEngineRoot/Engine/Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 位置: 345-353
// 文件: References.HazelightAngelscriptEngineRoot/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/AngelscriptManager.cpp
// 位置: 883-889
// 说明: UEAS2 把 asset 内容通过 `ReplaceAssetDefinition` 消息广播给调试/编辑链
// ============================================================================
FAngelscriptManager::Get().ReplaceScriptAssetContent(Curve->GetName(), NewContent);

FAngelscriptReplaceAssetDefinition Message;
Message.AssetName = AssetName;
Message.Lines = AssetContent;
DebugServer->SendMessageToAll(EDebugMessageType::ReplaceAssetDefinition, Message);

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp
// 位置: 323-331
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 2028-2034
// 说明: current 工作流完全保留，只把发送者 owner 收口到 `FAngelscriptEngine`
// ============================================================================
FAngelscriptEngine::Get().ReplaceScriptAssetContent(Curve->GetName(), NewContent);

FAngelscriptReplaceAssetDefinition Message;
Message.AssetName = AssetName;
Message.Lines = AssetContent;
DebugServer->SendMessageToAll(EDebugMessageType::ReplaceAssetDefinition, Message);
```

[3] runtime 侧的资产包名、redirector 与源码定位 metadata 也保持不变：

```cpp
// ============================================================================
// 文件: References.HazelightAngelscriptEngineRoot/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/AngelscriptManager.cpp
// 位置: 197-202
// 文件: References.HazelightAngelscriptEngineRoot/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_UObject.cpp
// 位置: 452-467, 500-503
// 说明: UEAS2 用固定 `/Script/AngelscriptAssets` 包承载 literal assets，并为旧位置留 redirector
// ============================================================================
AssetsPackage = NewObject<UPackage>(nullptr, FName(TEXT("/Script/AngelscriptAssets")), RF_Public | RF_Standalone | RF_MarkAsRootSet);
AssetsPackage->SetPackageFlags(PKG_CompiledIn);

FString RedirectorName = TEXT("Asset_") + AssetName;
UObjectRedirector* Redirector = FindObject<UObjectRedirector>(ScriptPackage, *RedirectorName);
if (Redirector == nullptr)
	Redirector = NewObject<UObjectRedirector>(ScriptPackage, *RedirectorName, RF_Standalone | RF_Public);
Redirector->DestinationObject = ExistingObject;

AssetsPackage->GetMetaData().SetValue(ExistingObject, TEXT("ScriptAssetFilename"), *Filename);
AssetsPackage->GetMetaData().SetValue(ExistingObject, TEXT("ScriptAssetLineNumber"), *FString::Printf(TEXT("%d"), LineNumber));

NotifyRegistrationEvent(TEXT("/Script/AngelscriptAssets"), *Asset->GetName(), ENotifyRegistrationType::NRT_NoExportObject,
	ENotifyRegistrationPhase::NRP_Finished, nullptr, false, Asset);

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 878-882, 1382-1386
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp
// 位置: 651-666, 694-697
// 说明: current 同样固定使用 `/Script/AngelscriptAssets`，并保留 redirector + metadata + registration notify
// ============================================================================
AssetsPackage = NewObject<UPackage>(nullptr, FName(TEXT("/Script/AngelscriptAssets")), RF_Public | RF_Standalone | RF_MarkAsRootSet);
AssetsPackage->SetPackageFlags(PKG_CompiledIn);

auto* ScriptPackage = FAngelscriptEngine::Get().AngelscriptPackage;
FString RedirectorName = TEXT("Asset_") + AssetName;
UObjectRedirector* Redirector = FindObject<UObjectRedirector>(ScriptPackage, *RedirectorName);
if (Redirector == nullptr)
	Redirector = NewObject<UObjectRedirector>(ScriptPackage, *RedirectorName, RF_Standalone | RF_Public);
Redirector->DestinationObject = ExistingObject;

AssetsPackage->GetMetaData().SetValue(ExistingObject, TEXT("ScriptAssetFilename"), *Filename);
AssetsPackage->GetMetaData().SetValue(ExistingObject, TEXT("ScriptAssetLineNumber"), *FString::Printf(TEXT("%d"), LineNumber));

NotifyRegistrationEvent(TEXT("/Script/AngelscriptAssets"), *Asset->GetName(), ENotifyRegistrationType::NRT_NoExportObject,
	ENotifyRegistrationPhase::NRP_Finished, nullptr, false, Asset);
```

### 设计取舍

current 选择保留 `ScriptAsset` 的 package name、editor menu affordance 和 debug message contract，收益是已有工具链、资产路径约定和用户肌肉记忆几乎不需要迁移。  
它放弃的是 UEAS2 那种“单例 manager 看起来像唯一中心”的结构，把入口下沉到 engine-scope owner；代价是调用者必须更明确地经过 `FAngelscriptEngine::Get()`，但换来的是和多 engine 作用域、subsystem 生命周期更一致的所有权模型。

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| Content Browser `ScriptAsset` context menu | 有 | 有 | **实现方式相同** |
| asset save -> `ReplaceAssetDefinition` debug message | 有 | 有 | **实现方式相同，owner 不同** |
| canonical asset package `/Script/AngelscriptAssets` | 有 | 有 | **没有实现差距** |
| redirector + `ScriptAssetFilename/LineNumber` metadata | 有 | 有 | **没有实现差距** |

### [维度 D2/D7/D11] `AdditionalEditorOnlyScriptPackageNames` 说明：editor-only 边界今天仍然是插件内显式规则，不是引擎自动认知

### 实现概述

```
[D2/D7/D11] Editor-Only Script Package Classification
`IsEditorOnlyClass(UClass)`
├─ package flags `PKG_EditorOnly | PKG_UncookedOnly`
├─ config list `AdditionalEditorOnlyScriptPackageNames`
├─ source path heuristic
│  ├─ `/Source/Editor/`
│  ├─ `/Plugins/Editor/`
│  └─ `/Source/AngelscriptEditor/`
└─ recurse to `SuperClass`
```

这条线前文还没有单独展开，但它对“为什么 current 仍能在不改引擎的前提下维持 editor/runtime 边界”很关键。  
新增结论是：**UEAS2 和 current 都没有把 editor-only script package 的判定下沉给引擎主干自动处理**。两边都在插件自己的 `Bind_BlueprintType.cpp` 里维护 `IsEditorOnlyClass()`：先看 `PKG_EditorOnly | PKG_UncookedOnly`，再看 `AdditionalEditorOnlyScriptPackageNames`，再看源码路径是否落在 `Source/Editor` / `Plugins/Editor` / `AngelscriptEditor`，最后递归父类。  
这意味着 current 在这个点上并没有“能力回退”；它保留的本来就是一个 **config-driven + heuristic-driven** 的插件内边界模型。

### 关键源码引用

[1] 设置项本身是原样保留的，连注释语义都一致：

```cpp
// ============================================================================
// 文件: References.HazelightAngelscriptEngineRoot/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/AngelscriptSettings.h
// 位置: 237-241
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h
// 位置: 197-201
// 说明: 两边都暴露 `AdditionalEditorOnlyScriptPackageNames`，让项目侧显式补 editor-only script package 名
// ============================================================================
/**
 * Script package names (/Script/ModuleName) that should be considered editor-only for the purposes of checking for incorrect usage.
 */
UPROPERTY(Config)
TArray<FName> AdditionalEditorOnlyScriptPackageNames;
```

[2] 真正的分类逻辑也几乎没有变化；owner 差异只是 `Manager` -> `Engine`：

```cpp
// ============================================================================
// 文件: References.HazelightAngelscriptEngineRoot/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_BlueprintType.cpp
// 位置: 921-961
// 说明: UEAS2 的 editor-only class 判定顺序
// ============================================================================
bool IsEditorOnlyClass(UClass* Class)
{
	static TMap<UClass*, bool> CachedEditorClasses;
	bool* CachedValue = CachedEditorClasses.Find(Class);
	if (CachedValue != nullptr)
		return *CachedValue;

	bool bIsEditor = false;

	if (Class->GetOutermost()->HasAnyPackageFlags(PKG_EditorOnly | PKG_UncookedOnly))
	{
		bIsEditor = true;
	}

	if (!bIsEditor && FAngelscriptManager::Get().ConfigSettings->AdditionalEditorOnlyScriptPackageNames.Contains(Class->GetOutermost()->GetFName()))
	{
		bIsEditor = true;
	}

	if (!bIsEditor && FSourceCodeNavigation::FindClassHeaderPath(Class, ClassHeaderPath))
	{
		if (ClassHeaderPath.Contains(TEXT("/Source/Editor/"))
			|| ClassHeaderPath.Contains(TEXT("/Plugins/Editor/"))
			|| ClassHeaderPath.Contains(TEXT("/Source/AngelscriptEditor/")))
		{
			bIsEditor = true;
		}
	}

	if (!bIsEditor && Class->GetSuperClass() != nullptr)
		bIsEditor = IsEditorOnlyClass(Class->GetSuperClass());
}

// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp
// 位置: 901-941
// 说明: current 完整保留同一套规则，差异只在配置 owner 取自 `FAngelscriptEngine`。
// ============================================================================
bool IsEditorOnlyClass(UClass* Class)
{
	static TMap<UClass*, bool> CachedEditorClasses;
	bool* CachedValue = CachedEditorClasses.Find(Class);
	if (CachedValue != nullptr)
		return *CachedValue;

	bool bIsEditor = false;

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
			|| ClassHeaderPath.Contains(TEXT("/Plugins/Editor/"))
			|| ClassHeaderPath.Contains(TEXT("/Source/AngelscriptEditor/")))
		{
			bIsEditor = true;
		}
	}

	if (!bIsEditor && Class->GetSuperClass() != nullptr)
		bIsEditor = IsEditorOnlyClass(Class->GetSuperClass());
}
```

### 设计取舍

这套做法的优点是完全 plugin-side：不要求引擎知道“哪些 script package 只应在 editor 生效”，项目方可以用 config 直接补名单。  
代价也一样明显：它不是引擎级自动认知，而是一套显式规则。如果项目新增的 editor-only script package 既没有正确 package flag，也不在配置名单里，最后只能依赖路径 heuristic 命中；这说明 current 在此处保留的是 **同一套约定式边界**，不是更强的主干能力。

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| `AdditionalEditorOnlyScriptPackageNames` 配置入口 | 有 | 有 | **没有实现差距** |
| editor-only class 判定 owner | plugin bind layer | plugin bind layer | **实现方式相同** |
| 判定顺序：package flags -> config -> source path -> super class | 有 | 有 | **实现方式相同** |
| 是否依赖引擎主干自动识别所有 editor-only script package | 否 | 否 | **都没有实现该主干能力** |

---

## 深化分析 (2026-04-09 08:05:46)

### [维度 D1/D11] `packaged / non-editor` 启动 contract 已从“模块加载即启动”变成“`GameInstance` 出现后启动”

前文多次讨论过 `Loader` 被移除之后谁来接手初始化，但还没有把 **非 editor 运行时** 的真正启动边界单独钉死。补完源码后可以更明确地说：**Hazelight 在 `PostDefault` 模块加载阶段就无条件启动脚本系统；current 则只在 `Editor / Commandlet` 里做模块时初始化，打包运行时要等 `UAngelscriptGameInstanceSubsystem::Initialize()` 才真正拥有主引擎。**

这不是一句“少了 `Loader`”能概括的变化，而是交付前提真的变了：UEAS2 假定插件在模块启动时就应该存在；current 假定打包游戏里的主 owner 应该跟 `UGameInstance` 生命周期对齐。

```
[D1/D11] Non-Editor Startup Ownership
UEAS2
├─ PostDefault module load
│  └─ AngelscriptLoader::StartupModule()
│     └─ FAngelscriptCodeModule::InitializeAngelscript()
│        └─ FAngelscriptManager::GetOrCreate().Initialize()
└─ FAngelscriptManager : FTickableGameObject         // manager 自己持有 tick

current
├─ PostDefault RuntimeModule::StartupModule()
│  ├─ editor / commandlet -> InitializeAngelscript()
│  └─ editor only -> TickFallbackPrimaryEngine()
└─ packaged / non-editor
   └─ UAngelscriptGameInstanceSubsystem::Initialize()
      ├─ borrow current engine if present
      └─ else construct OwnedEngine and own tick
```

[1] UEAS2 的启动点是无条件模块入口，`Loader` 只是把初始化直接转发给 `CodeModule`，随后 `Manager` 自己初始化并自己 tick：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptLoader\Private\AngelscriptLoaderModule.cpp
// 函数: FAngelscriptLoaderModule::StartupModule
// 位置: 6-9
// 说明: UEAS2 在模块加载时无条件启动 Angelscript
// ============================================================================
void FAngelscriptLoaderModule::StartupModule()
{
	// ★ Loader 不看 editor/game 场景，直接转发
	FAngelscriptCodeModule::InitializeAngelscript();
}
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\AngelscriptCodeModule.cpp
// 函数: FAngelscriptCodeModule::InitializeAngelscript
// 位置: 124-131
// 说明: 真正 owner 仍是 manager singleton
// ============================================================================
void FAngelscriptCodeModule::InitializeAngelscript()
{
	static bool bInitialized = false;
	if (bInitialized)
		return;

	bInitialized = true;
	FModuleManager::Get().LoadModuleChecked(TEXT("AngelscriptCode"));
	// ★ 初始化直接落到全局 manager
	FAngelscriptManager::GetOrCreate().Initialize();
}
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Public\AngelscriptManager.h
// 位置: 67-122
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\AngelscriptManager.cpp
// 函数: FAngelscriptManager::Tick
// 位置: 1544-1585
// 说明: UEAS2 的 owner 假设写进了类型本身：manager 直接是 tickable
// ============================================================================
struct ANGELSCRIPTCODE_API FAngelscriptManager : FTickableGameObject
{
	...
	virtual void Tick(float DeltaTime) override;
	...
};

void FAngelscriptManager::Tick(float DeltaTime)
{
#if AS_CAN_HOTRELOAD
	// ★ 热重载轮询仍由 manager 本身驱动
	...
#endif

#if WITH_AS_DEBUGSERVER
	if(DebugServer != nullptr)
		DebugServer->Tick();
#endif
}
```

[2] current 把模块启动改成条件化，仅在 `Editor / Commandlet` 里提前初始化；打包运行时真正 owner 改成 `UAngelscriptGameInstanceSubsystem`：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp
// 函数: StartupModule / InitializeAngelscript
// 位置: 13-24, 138-166
// 说明: current 不再在所有运行时场景里做模块时初始化
// ============================================================================
void FAngelscriptRuntimeModule::StartupModule()
{
	if (GIsEditor || IsRunningCommandlet())
	{
		// ★ 只有 editor / commandlet 仍走模块时初始化
		InitializeAngelscript();
	}

	if (GIsEditor)
	{
		FallbackTickHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateRaw(this, &FAngelscriptRuntimeModule::TickFallbackPrimaryEngine));
	}
}

void FAngelscriptRuntimeModule::InitializeAngelscript()
{
	...
	if (FAngelscriptEngine* CurrentEngine = FAngelscriptEngine::TryGetCurrentEngine())
	{
		CurrentEngine->Initialize();
	}
	else
	{
		// ★ 没有现成上下文时才创建 OwnedPrimaryEngine
		OwnedPrimaryEngine = MakeUnique<FAngelscriptEngine>();
		FAngelscriptEngineContextStack::Push(OwnedPrimaryEngine.Get());
		OwnedPrimaryEngine->Initialize();
	}
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.h
// 位置: 12-38
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp
// 函数: Initialize / Deinitialize
// 位置: 12-29, 32-47
// 说明: packaged/non-editor 主 owner 已明确迁到 GameInstanceSubsystem
// ============================================================================
class ANGELSCRIPTRUNTIME_API UAngelscriptGameInstanceSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	...
	UPROPERTY()
	FAngelscriptEngine OwnedEngine;
	FAngelscriptEngine* PrimaryEngine = nullptr;
	bool bOwnsPrimaryEngine = false;
	...
};

void UAngelscriptGameInstanceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	...
	PrimaryEngine = FAngelscriptEngine::TryGetCurrentEngine();
	if (PrimaryEngine == nullptr)
	{
		PrimaryEngine = &OwnedEngine;
		FAngelscriptEngineContextStack::Push(PrimaryEngine);
		OwnedEngine.Initialize();
		// ★ 非 editor 运行时这里才真正拥有主引擎
		bOwnsPrimaryEngine = true;
	}
	...
}

void UAngelscriptGameInstanceSubsystem::Deinitialize()
{
	...
	if (bOwnsPrimaryEngine)
	{
		FAngelscriptEngineContextStack::Pop(PrimaryEngine);
		if (PrimaryEngine != nullptr)
		{
			// ★ teardown 也跟着 GameInstance 生命周期走
			PrimaryEngine->Shutdown();
		}
	}
}
```

#### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| 打包运行时启动触发点 | `PostDefault` 模块加载即启动 | `GameInstanceSubsystem::Initialize()` 才真正拥有主引擎 | **实现方式不同** |
| tick owner | `FAngelscriptManager : FTickableGameObject` | `UAngelscriptGameInstanceSubsystem : FTickableGameObject` | **实现方式不同** |
| commandlet 启动 | 模块加载直接启动 | `IsRunningCommandlet()` 仍走模块初始化 | **没有能力缺口** |
| teardown 对称性 | `Loader/CodeModule` 不承担实质 teardown | subsystem 显式 `Shutdown()` / context pop | **实现质量增强** |

这里最容易写错的一点是：current 不是“少了一个 Loader 所以启动弱了”，而是 **启动 owner 从模块生命周期后移到了宿主生命周期**。对可复用插件交付来说，这是一种更强的边界控制；代价是宿主不再默认享有“模块一加载就有脚本主引擎”的假设。

### [维度 D5/D9] current 把 receive-side framing 做成了可测 contract，但 live socket send 仍共享 UEAS2 的 partial-send 盲点

前文已经把 `FAngelscriptDebugMessageEnvelope`、`SerializeDebugMessageEnvelope()` 和 `TryDeserializeDebugMessageEnvelope()` 讲清了，但还没有把 **测试覆盖到哪、停在哪** 单独说透。新结论是：

1. current 的增强主要落在 **接收侧 framing contract**，而且已经被独立自动化覆盖。
2. 真正的 live socket 发送环节，两边仍然共享同一个假设：`Client->Send()` 只要返回 `true`，就默认整包已发完；`BytesSent < Msg.Buffer.Num()` 的尾包重发没有实现。

这意味着 current 的 D5 改进应判为“**接收/解析质量增强**”，不能写成“**transport 已全面更健壮**”。

```
[D5/D9] Debug Transport Robustness
current
├─ SerializeDebugMessageEnvelope()                 // 统一封包
├─ TryDeserializeDebugMessageEnvelope()            // 统一拆包
├─ transport tests
│  ├─ SingleEnvelope
│  ├─ MultipleEnvelopes
│  ├─ TruncatedEnvelope
│  ├─ InvalidLength
│  └─ EmptyBodyEnvelope
└─ live TrySendingMessages()
   ├─ Client->Send(Buffer, Num, BytesSent)
   └─ success => RemoveAt(0)                       // 未重放未发送尾包

UEAS2
└─ live TrySendingMessages()
   ├─ Client->Send(Buffer, Num, BytesSent)
   └─ success => RemoveAt(0)                       // 同样未处理 partial send
```

[1] current 的 transport 自动化只验证 receive-side envelope contract：单包、多包、截断包、非法长度、空 body 都有对位用例：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugTransportTests.cpp
// 函数: 文件头测试注册 + Single/Multiple/Truncated/InvalidLength/EmptyBody
// 位置: 15-36, 43-219
// 说明: current 已把 receive-side framing 明确做成独立回归面
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptDebugTransportSingleEnvelopeTest,
	"Angelscript.CppTests.Debug.Transport.SingleEnvelope",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptDebugTransportTruncatedEnvelopeTest,
	"Angelscript.CppTests.Debug.Transport.TruncatedEnvelope",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptDebugTransportSingleEnvelopeTest::RunTest(const FString& Parameters)
{
	...
	// ★ 验证 header 中的 length 是否等于 type byte + body
	TestEqual(TEXT("Debug.Transport.SingleEnvelope should store the payload length as type-byte plus body"), MessageLength, static_cast<int32>(sizeof(uint8)) + Body.Num());
	...
}

bool FAngelscriptDebugTransportTruncatedEnvelopeTest::RunTest(const FString& Parameters)
{
	...
	// ★ 截断包被视为“等待更多字节”，不是协议错误
	TestFalse(TEXT("Debug.Transport.TruncatedEnvelope should wait for more bytes instead of yielding an envelope"), bHasEnvelope);
	TestTrue(TEXT("Debug.Transport.TruncatedEnvelope should keep the partial bytes buffered"), Buffer.Num() > 0);
	return true;
}

bool FAngelscriptDebugTransportInvalidLengthTest::RunTest(const FString& Parameters)
{
	// ★ 非法长度会被明确拒绝
	TestFalse(TEXT("Debug.Transport.InvalidLength should reject zero-length envelopes"), TryDeserializeDebugMessageEnvelope(Buffer, Envelope, bHasEnvelope, &Error));
	...
}
```

[2] 但 live socket 发送环节两边仍是同一实现：只判断 `Send()` 是否成功，没有消费 `BytesSent`，因此没有 tail replay：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Debugging\AngelscriptDebugServer.cpp
// 函数: FAngelscriptDebugServer::TrySendingMessages
// 位置: 2774-2786
// 说明: UEAS2 发送成功即弹队，未处理 partial send
// ============================================================================
void FAngelscriptDebugServer::TrySendingMessages(FSocket* Client)
{
	TArray<FQueuedMessage>& Queue = QueuedSends.FindOrAdd(Client);
	while (Queue.Num() != 0)
	{
		FQueuedMessage& Msg = Queue[0];
		...
		int32 BytesSent;
		if (!Client->Send(Msg.Buffer.GetData(), Msg.Buffer.Num(), BytesSent))
			break;

		// ★ 只要返回 true 就移除整条消息，没有检查 BytesSent 是否小于 Buffer.Num()
		Queue.RemoveAt(0);
	}
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 函数: FAngelscriptDebugServer::TrySendingMessages
// 位置: 2845-2857
// 说明: current 的 live send loop 与 UEAS2 同构，transport 增强没有推进到发送尾包管理
// ============================================================================
void FAngelscriptDebugServer::TrySendingMessages(FSocket* Client)
{
	TArray<FQueuedMessage>& Queue = QueuedSends.FindOrAdd(Client);
	while (Queue.Num() != 0)
	{
		FQueuedMessage& Msg = Queue[0];
		...
		int32 BytesSent;
		if (!Client->Send(Msg.Buffer.GetData(), Msg.Buffer.Num(), BytesSent))
			break;

		// ★ current 同样在 bool-success 后直接出队
		Queue.RemoveAt(0);
	}
}
```

#### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| receive-side framing helper | 内联在 server 里，源码可读但不独立可测 | `Serialize/TryDeserialize` 已抽成 API | **实现质量增强** |
| transport 自动化 | 本轮未在插件内读到对位 transport test file | `Debug.Transport.*` 五组自动化覆盖 | **实现质量增强** |
| live socket partial-send 尾包重发 | 未实现 | 未实现 | **都没有实现该鲁棒性** |
| 对 transport 改进的真实边界 | 主要仍靠运行时逻辑 | 解析层增强明显，但发送层仍沿用旧假设 | **实现质量差异** |

因此，D5 这里最准确的表述不是“current debugger transport 已经全面强于上游”，而是：**它把收包/拆包 contract 做硬了，但发包队列还没有从“成功即整包完成”的上游假设里走出来。**

### [维度 D1/D9] current 把 owner 迁移做成了结构化回归矩阵，而不是只在实现里“默认应该成立”

前文已经分析过 `manager singleton -> engine scope + subsystem owner` 的架构变化，但还没有把 **验证方式** 单独拆出来。新证据说明：current 不是只改了实现，还专门补了一层结构化回归，把 “谁负责 tick、谁能 clone、上下文栈能否隔离” 这些原本很容易回退成隐含假设的东西，正式变成 automation contract。

UEAS2 这类约束大多直接固化在类型和主逻辑里；current 则把它们再往前推进了一步，变成“类型声明 + 测试矩阵”双保险。

```
[D1/D9] Ownership Verification
UEAS2
├─ FAngelscriptManager : FTickableGameObject
└─ Tick() drives hot reload + debug server        // owner 假设写在主类型里

current
├─ FAngelscriptEngine : USTRUCT                    // engine 本身不再 tick
├─ UAngelscriptGameInstanceSubsystem : FTickableGameObject
└─ automation matrix
   ├─ EngineNoLongerTickable
   ├─ GameInstanceSubsystemOwnsTick
   ├─ MultiEngine.Create.Full / Clone
   └─ ContextStack SnapshotAndClear / RestoreSnapshot
```

[1] UEAS2 的 owner 假设是“manager 自己就是 tick owner”，这从类型定义和 `Tick()` 主逻辑里就能直接看出来：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Public\AngelscriptManager.h
// 位置: 67-122
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\AngelscriptManager.cpp
// 函数: FAngelscriptManager::Tick
// 位置: 1544-1585
// 说明: 上游把 owner 假设直接写进主类型
// ============================================================================
struct ANGELSCRIPTCODE_API FAngelscriptManager : FTickableGameObject
{
	...
	virtual void Tick(float DeltaTime) override;
	...
};

void FAngelscriptManager::Tick(float DeltaTime)
{
	// ★ 热重载轮询与 DebugServer tick 都挂在 manager 自己身上
	...
	if(DebugServer != nullptr)
		DebugServer->Tick();
}
```

[2] current 先在类型层把 owner 切开，再用结构性测试钉死这条边界：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h
// 位置: 102-110
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.h
// 位置: 12-38
// 说明: current 明确把 engine 与 tick owner 分离
// ============================================================================
USTRUCT()
struct ANGELSCRIPTRUNTIME_API FAngelscriptEngine
{
	...
};

UCLASS()
class ANGELSCRIPTRUNTIME_API UAngelscriptGameInstanceSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	...
};
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptSubsystemOwnershipTests.cpp
// 位置: 9-30
// 说明: 这不是“看实现觉得应该如此”，而是正式回归项
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptEngineTickOwnershipTest,
	"Angelscript.CppTests.Subsystem.EngineNoLongerTickable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptSubsystemTickOwnershipTest,
	"Angelscript.CppTests.Subsystem.GameInstanceSubsystemOwnsTick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptEngineTickOwnershipTest::RunTest(const FString& Parameters)
{
	// ★ 明确断言 engine 本体不再是 tickable
	TestFalse(... std::is_base_of_v<FTickableGameObject, FAngelscriptEngine>);
	return true;
}

bool FAngelscriptSubsystemTickOwnershipTest::RunTest(const FString& Parameters)
{
	// ★ 明确断言 tick owner 已迁到 GameInstanceSubsystem
	TestTrue(... std::is_base_of_v<FTickableGameObject, UAngelscriptGameInstanceSubsystem>);
	return true;
}
```

[3] current 还进一步把多引擎上下文栈和 full/clone 创建模式做成了专用矩阵，这正是 UEAS2 的 singleton owner 本来不需要面对的问题：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineTests.cpp
// 位置: 67-76, 95-99, 183-199
// 说明: current 不只验证“能初始化”，还验证 owner 隔离与上下文栈恢复
// ============================================================================
struct FMultiEngineContextStackGuard
{
	TArray<FAngelscriptEngine*> SavedStack;
	FMultiEngineContextStackGuard()
	{
		// ★ 先清空上下文栈，保证每个用例在隔离状态下运行
		SavedStack = FAngelscriptEngineContextStack::SnapshotAndClear();
	}
	~FMultiEngineContextStackGuard()
	{
		FAngelscriptEngineContextStack::RestoreSnapshot(MoveTemp(SavedStack));
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptEngineCreateFullModeTest,
	"Angelscript.CppTests.MultiEngine.Create.Full",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptEngineCreateFullModeTest::RunTest(const FString& Parameters)
{
	...
	TestEqual(TEXT("MultiEngine.Create.Full should mark the engine as Full"), Engine->GetCreationMode(), EAngelscriptEngineCreationMode::Full);
	TestTrue(TEXT("MultiEngine.Create.Full should own the underlying script engine"), Engine->OwnsEngine());
	TestNull(TEXT("MultiEngine.Create.Full should not record a source engine"), Engine->GetSourceEngine());
	return TestNotNull(TEXT("MultiEngine.Create.Full should immediately create an asIScriptEngine"), Engine->GetScriptEngine());
}
```

#### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| tick owner 假设的表达方式 | 主类型直接实现 `FTickableGameObject` | `Engine` 与 `GameInstanceSubsystem` 分离 | **实现方式不同** |
| owner 迁移是否有专门结构测试 | 本轮引用里未见对位结构化用例 | `SubsystemOwnershipTests + MultiEngineTests` 明确覆盖 | **实现质量增强** |
| 多引擎 / 上下文栈隔离 | singleton owner 天然不强调这类场景 | 已正式成为测试矩阵的一部分 | **能力边界增强** |

这一点的意义不是“current 比 UEAS2 更会写测试”这么泛，而是：**当 owner 从单例 manager 迁到 engine scope + subsystem 后，风险面本来就变大了；current 不是回避这个复杂度，而是用结构化回归把复杂度封住。**

---
## 深化分析 (2026-04-09 08:13:20)

### [维度 D3/D7] `Blueprint asset` 自身的 mixin authoring 面在 current 中已经从 member-style helper 收缩成普通 editor helper

### 实现概述

前面的多轮分析主要盯着 runtime function library 的 mixin 面。这一轮补一个更容易被忽略、但很能说明问题的 editor 侧 target：`UBlueprintCore / UBlueprint` 本身。

UEAS2 仍把 `UBlueprintMixinLibrary` 声明成 `ScriptMixin = "UBlueprintCore UBlueprint"`，并把 `GetGeneratedClass(UBlueprintCore*)` 标成 `ScriptCallable`。这意味着它不是单纯的静态 helper，而是一个会在签名改写阶段剥掉首参的 member-style mixin。current 保留了同名类和同名函数，但把类级 `ScriptMixin` 清空、把 `ScriptCallable` 退回 `BlueprintCallable`。再结合 current 的 `Helper_FunctionSignature` 只在 outer class 仍然带着 `ScriptMixin` metadata 时才把首参折叠成成员方法，这条 Blueprint asset authoring surface 实际上已经失活。

这里要避免误判成“Blueprint interop 没了”。更准确的结论是：**功能 helper 还在，member-style sugar 没了**。收益是 header 完全退回 stock UHT 可理解的写法；代价是脚本作者不能再把 `UBlueprintCore/UBlueprint` 看成天然挂着 `GetGeneratedClass()` 这类 mixin 方法的对象。

```
[D3/D7] Blueprint Asset Mixin Surface
UEAS2
├─ UBlueprintMixinLibrary
│  ├─ UCLASS(meta = ScriptMixin = "UBlueprintCore UBlueprint")
│  └─ UFUNCTION(ScriptCallable) GetGeneratedClass(UBlueprintCore*)
└─ signature rewrite strips first arg              // Blueprint asset 作为 member receiver

current
├─ UBlueprintMixinLibrary
│  ├─ UCLASS(meta = ())
│  └─ UFUNCTION(BlueprintCallable) GetGeneratedClass(UBlueprintCore*)
├─ Helper_FunctionSignature only hoists when class has ScriptMixin
└─ same helper survives as plain callable surface  // helper 还在，但不再是 member sugar
```

### 关键源码引用

[1] `UBlueprintMixinLibrary` 在两边都还在，但 current 把决定 member-sugar 的两层 authoring 标记都关掉了：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptEditor\Public\FunctionLibraries\BlueprintMixinLibrary.h
// 位置: 7-18
// 说明: UEAS2 明确把 Blueprint 资产 helper 声明成 ScriptMixin + ScriptCallable
// ============================================================================
UCLASS(MinimalAPI, Meta = (ScriptMixin = "UBlueprintCore UBlueprint"))
class UBlueprintMixinLibrary : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(ScriptCallable)
	static UClass* GetGeneratedClass(UBlueprintCore* Blueprint)
	{
		// ★ 首参会被 mixin 逻辑当作 receiver，脚本侧看到的是 Blueprint.GetGeneratedClass()
		return Blueprint != nullptr ? Cast<UClass>(Blueprint->GeneratedClass) : nullptr;
	}
};
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Public/FunctionLibraries/BlueprintMixinLibrary.h
// 位置: 5-18
// 说明: current 保留 helper 本体，但把 class-level ScriptMixin 和 function-level ScriptCallable 都退掉了
// ============================================================================
//UCLASS(MinimalAPI, Meta = (ScriptMixin = "UBlueprintCore UBlueprint"))
UCLASS(MinimalAPI, Meta = ())
class UBlueprintMixinLibrary : public UObject
{
	GENERATED_BODY()

public:
	//UFUNCTION(ScriptCallable)
	UFUNCTION(BlueprintCallable)
	static UClass* GetGeneratedClass(UBlueprintCore* Blueprint)
	{
		// ★ helper 仍可复用，但不再天然属于 Blueprint asset 的 member-style surface
		return Blueprint != nullptr ? Cast<UClass>(Blueprint->GeneratedClass) : nullptr;
	}
};
```

[2] current 的签名改写器只在类上还能读到 `ScriptMixin` 时才剥首参；metadata 一旦清空，这类 helper 就会退回普通静态函数暴露：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h
// 位置: 273-305
// 说明: current 的 mixin 折叠严格依赖 outer UClass 的 ScriptMixin metadata
// ============================================================================
FString Namespace = GetScriptNamespaceForClass(InType, Function);
bGlobalScope = Function->HasMetaData(NAME_Signature_ScriptGlobalScope);

// ★ 只有 class-level ScriptMixin 命中时，才把首参剥掉并改成成员函数
bool bFoundMixin = false;
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
			ArgumentTypes.RemoveAt(0);
			ArgumentNames.RemoveAt(0);
			ArgumentDefaults.RemoveAt(0);
			ClassName = Mixin;
			bStaticInScript = false;
			bFoundMixin = true;
			break;
		}
	}
}
```

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| `UBlueprintCore/UBlueprint` 的 member-style helper | `ScriptMixin + ScriptCallable` 激活 | metadata 清空，退回普通 `BlueprintCallable` helper | **实现方式不同 + authoring surface 收缩** |
| `GetGeneratedClass()` 功能是否还在 | 在 | 在 | **没有实现缺口** |
| 收缩发生在哪一层 | editor-side mixin authoring | editor-side mixin authoring | **不是整体 Blueprint interop 缺失** |

这一点说明：**current 对 Blueprint 的主要收缩，不在“能不能读 Blueprint”，而在“Blueprint asset 能不能继续被当成脚本对象本身的 mixin receiver”。**

### [维度 D2/D3/D6/D11] current 真正保下来的 authoring 语义是 `ScriptName` 传播链，断开的不是 alias metadata

### 实现概述

前文多次提到 `ScriptCallable`、`ScriptMixin`、`AngelscriptPropertyFlags` 和 `FUNC_RuntimeGenerated`。这一轮把几个调用点重新串起来后，可以把边界说得更准：**current 失去的是“自定义 specifier 被 UHT / engine-mainline 理解”的那一层，不是 `ScriptName` 这种普通 metadata 的传播能力。**

证据很集中。第一，current 的 wrapper header 大量把 `UFUNCTION(ScriptCallable)` 改成了 `UFUNCTION(BlueprintCallable)`，但 `meta = (ScriptName = "...")` 原样保留。第二，`Helper_FunctionSignature` 里控制 namespace 与函数别名的开关 `bUseScriptNameForBlueprintLibraryNamespaces` 仍然保留。第三，property 绑定和 Blueprint override 校验两条链都继续直接消费 `ScriptName`：`Bind_BlueprintType.cpp` 用它改 property 暴露名，`Bind_BlueprintEvent.cpp + AngelscriptClassGenerator.cpp` 用它建立 `GBlueprintEventsByScriptName` 并在 override 时报错提示“该用别名而不是原始 C++ 名称”。

所以，本维度最稳的结论不是“stock UHT 下 ScriptName 也断了”，而是：**alias metadata 还活着，断的是围绕 custom specifier 的自动导出与主干语义。**

```
[D2/D3/D6/D11] ScriptName Survival Chain
Header Meta
├─ UCLASS(meta = ScriptName = "Math")
├─ UFUNCTION(... meta = ScriptName = "SinCos")
└─ UPROPERTY(... meta = ScriptName = "...")

current runtime/editor consumers
├─ Helper_FunctionSignature::GetScriptNameForFunction()
│  ├─ function alias
│  └─ namespace alias
├─ Bind_BlueprintType.cpp
│  └─ property alias
└─ Bind_BlueprintEvent.cpp + ClassGenerator
   └─ override lookup by script-facing name

what no longer survives automatically
└─ ScriptCallable / property flags / RuntimeGenerated mainline semantics
```

### 关键源码引用

[1] header authoring 层最直观的变化是：`ScriptName` 保留，`ScriptCallable` 退成 `BlueprintCallable`：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Public\FunctionLibraries\AngelscriptMathLibrary.h
// 位置: 6-20
// 说明: UEAS2 用 ScriptCallable + ScriptName 同时定义脚本别名
// ============================================================================
UCLASS(Meta = (ScriptName = "Math"))
class UAngelscriptMathLibrary : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(ScriptCallable, Meta = (ScriptTrivial, ScriptName = "SinCos"))
	static void SinCos_32(float& ScalarSin, float& ScalarCos, float Value)
	{
		// ★ 别名是 SinCos，authoring 入口是 ScriptCallable
		FMath::SinCos(&ScalarSin, &ScalarCos, Value);
	}
};
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptMathLibrary.h
// 位置: 6-22
// 说明: current 把 authoring 入口换成 BlueprintCallable，但保留 ScriptName
// ============================================================================
UCLASS(Meta = (ScriptName = "Math"))
class UAngelscriptMathLibrary : public UObject
{
	GENERATED_BODY()

public:
	//UFUNCTION(ScriptCallable, Meta = (ScriptTrivial, ScriptName = "SinCos"))
	UFUNCTION(BlueprintCallable, Meta = (ScriptName = "SinCos"))
	static void SinCos_32(float& ScalarSin, float& ScalarCos, float Value)
	{
		// ★ alias 没丢；丢的是上游自定义 specifier 的自动链
		FMath::SinCos(&ScalarSin, &ScalarCos, Value);
	}
};
```

[2] `ScriptName` 的 namespace / method alias 配置并没有跟着 manager-singleton 一起消失，而是平移到了 engine-scope：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Public\AngelscriptManager.h
// 位置: 83-85
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\AngelscriptManager.cpp
// 位置: 89-90
// 说明: UEAS2 允许用 ScriptName 改 Blueprint library namespace
// ============================================================================
static bool bUseScriptNameForBlueprintLibraryNamespaces;
static TArray<FString> BlueprintLibraryNamespacePrefixesToStrip;
static TArray<FString> BlueprintLibraryNamespaceSuffixesToStrip;

bool FAngelscriptManager::bUseScriptNameForBlueprintLibraryNamespaces = true;
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h
// 位置: 145-147, 1175-1180
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 78-80
// 说明: current 仍保留同一套 ScriptName namespace / method lookup 开关，只是 owner 换成 engine-scope
// ============================================================================
static bool bUseScriptNameForBlueprintLibraryNamespaces;
static TArray<FString> BlueprintLibraryNamespacePrefixesToStrip;
static TArray<FString> BlueprintLibraryNamespaceSuffixesToStrip;

TSharedPtr<FAngelscriptFunctionDesc> GetMethodByScriptName(const FString& FuncName)
{
	for (auto FuncDesc : Methods)
	{
		if (FuncName.Equals(FuncDesc->ScriptFunctionName))
		{
			// ★ runtime lookup 仍按 script-facing name 工作
			...
		}
	}
}

bool FAngelscriptEngine::bUseScriptNameForBlueprintLibraryNamespaces = true;
```

[3] property 与 event/override 两条链也都继续直接消费 `ScriptName`：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp
// 位置: 1089-1092
// 说明: current 仍然用 ScriptName 改 property 暴露名
// ============================================================================
const FString& ScriptName = Property->GetMetaData(NAME_Property_ScriptName);
if (ScriptName.Len() != 0)
{
	// ★ property 暴露名直接采用 script-facing alias
	PropertyName = FAngelscriptFunctionSignature::GetPrimaryScriptName(ScriptName);
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp
// 位置: 67-80, 635-640
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp
// 位置: 737-811
// 说明: event map 与 override 校验都继续按 ScriptName 工作
// ============================================================================
TMap<UClass*, TMap<FString, UFunction*>> GBlueprintEventsByScriptName;

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
				// ★ override 查找先按 script-facing alias 走
				return *Function;
			}
		}
		CheckClass = CheckClass->GetSuperClass();
	}
	...
}

GBlueprintEventsByScriptName.FindOrAdd(CastChecked<UClass>(Function->GetOuter())).Add(Signature.ScriptName, Function);

auto* ParentFunction = GetBlueprintEventByScriptName(CodeSuperClass, FunctionDesc->FunctionName);
...
FAngelscriptEngine::Get().ScriptCompileError(ModuleData.NewModule, FunctionDesc->LineNumber, FString::Printf(
	TEXT("Use name `%s` instead to override C++ event %s in parent class %s, as it has a ScriptName or stripped prefix."),
	*ScriptName, *NonEvent->GetName(), *NonEvent->GetOwnerClass()->GetName()));
```

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| `ScriptName` 作为 function/class alias metadata | 在 | 在 | **没有实现缺口** |
| `ScriptName` 驱动 namespace / property alias / override lookup | 在 | 在 | **实现方式基本延续** |
| `ScriptCallable` 自定义 specifier 自动链 | 在，引擎/UHT 主链理解 | 多数退成 `BlueprintCallable` + 插件内补偿 | **局部没有实现上游 authoring sugar** |
| 引擎补丁真正承担的职责 | 扩展 specifier 和主干语义 | 插件内重建别名消费点 | **实现方式不同** |

本段把一个常见误读钉住了：**current 不是把“脚本别名”整体弄丢了，而是把“谁来理解这些别名之外的自定义 specifier”从引擎主链退回了插件私有链。**

### [维度 D3/D7/D9] `Script*Subsystem` 在 current 中是“头文件平移成功、脚本类生成闭环未完成”

### 实现概述

`Script*Subsystem` 很容易被粗暴地归类成“current 没做”。源码更复杂一些。current 其实把 `UScriptGameInstanceSubsystem` 与 `UScriptEditorSubsystem` 的基类 surface 大体保留下来了：生命周期入口、`BP_ShouldCreateSubsystem()`、`BP_Initialize()`、`BP_Tick()`、`FTickableGameObject` 契约都还在；其中 `UScriptEditorSubsystem` 基本就是原样平移，`UScriptGameInstanceSubsystem` 甚至把 `UCLASS` 从 `NotBlueprintable` 放宽成了 `Blueprintable`。

真正没闭环的地方不是头文件，而是**脚本类生成正向路径**。current 自己的 `AngelscriptSubsystemScenarioTests.cpp` 已经把这点写成了回归 contract：`UScriptWorldSubsystem` 和 `UScriptGameInstanceSubsystem` 的脚本子类场景当前预期就是 `CompileResult == Error`。也就是说，current 的 subsystem 问题不能写成“没有 subsystem 基类”，更准确的判断是：**base class surface 在，compiler/class-generator closure 还没有完成。**

关于 `UScriptEditorSubsystem`，本轮有直接的 header 对位证据，但没有读到同形态的 current 自动化失败用例；因此“editor subsystem 也同样失败”属于推断，不宜当成已证实结论。

```
[D3/D7/D9] Script Subsystem Closure
UEAS2
├─ ScriptGameInstanceSubsystem / ScriptEditorSubsystem
│  ├─ BP_ShouldCreateSubsystem
│  ├─ BP_Initialize / BP_Deinitialize
│  └─ BP_Tick + FTickableGameObject
└─ base classes are part of script authoring surface

current
├─ same base-class surface still ships
│  ├─ ScriptGameInstanceSubsystem
│  └─ ScriptEditorSubsystem
├─ subsystem scenario tests still target those base classes
└─ expected result == Error                        // positive class-generation path not closed
```

### 关键源码引用

[1] `UScriptGameInstanceSubsystem` 在 current 中不是被删除，而是被大体平移；差异集中在 `UCLASS` 声明，而不是 BP 生命周期接口本身：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Public\BaseClasses\ScriptGameInstanceSubsystem.h
// 位置: 7-18, 21-58, 77-90
// 说明: UEAS2 的 subsystem 基类明确就是脚本 authoring surface
// ============================================================================
UCLASS(NotBlueprintable, Abstract, Meta = (NoBlueprintsOfChildren))
class ANGELSCRIPTCODE_API UScriptGameInstanceSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	UScriptGameInstanceSubsystem(const FObjectInitializer& Initializer)
		: Super()
		, FTickableGameObject(IsTemplate() ? ETickableTickType::Never : ETickableTickType::NewObject)
	{}

	bool bInitialized = false;

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override
	{
		return BP_ShouldCreateSubsystem(Outer);
	}

	UFUNCTION(BlueprintNativeEvent)
	bool BP_ShouldCreateSubsystem(UObject* Outer) const;

	UFUNCTION(BlueprintImplementableEvent)
	void BP_Initialize();

	UFUNCTION(BlueprintImplementableEvent)
	void BP_Deinitialize();

	UFUNCTION(BlueprintImplementableEvent)
	void BP_Tick(float DeltaTime);

	virtual bool IsAllowedToTick() const override final
	{
		// ★ tick 由脚本 BP_Tick 驱动
		return !IsTemplate() && bInitialized;
	}
};
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/BaseClasses/ScriptGameInstanceSubsystem.h
// 位置: 6-8, 15-20, 41-84
// 说明: current 仍然保留同一套 BP 生命周期与 tick 契约，只是 UCLASS 声明更宽
// ============================================================================
UCLASS(Blueprintable, Abstract)
class ANGELSCRIPTRUNTIME_API UScriptGameInstanceSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	bool bInitialized = false;

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override
	{
		return BP_ShouldCreateSubsystem(Outer);
	}

	UFUNCTION(BlueprintNativeEvent)
	bool BP_ShouldCreateSubsystem(UObject* Outer) const;

	UFUNCTION(BlueprintImplementableEvent)
	void BP_Initialize();

	UFUNCTION(BlueprintImplementableEvent)
	void BP_Deinitialize();

	UFUNCTION(BlueprintImplementableEvent)
	void BP_Tick(float DeltaTime);

	virtual bool IsAllowedToTick() const override final
	{
		// ★ 从 header surface 看，脚本子类 authoring contract 仍被保留
		return !IsTemplate() && bInitialized;
	}
};
```

[2] 但 current 自动化已经明确把“脚本子类能否真正生成”锁成失败预期；这证明 gap 在 class generator / compiler closure，而不在基类缺席：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemScenarioTests.cpp
// 位置: 203-245
// 说明: current 对 GameInstanceSubsystem 的正向脚本生成当前就是失败预期
// ============================================================================
bool FAngelscriptScenarioGameInstanceSubsystemLifecycleTest::RunTest(const FString& Parameters)
{
	...
	const bool bCompiled = CompileModuleWithResult(
		&Engine,
		ECompileType::SoftReloadOnly,
		ModuleName,
		TEXT("ScenarioGameInstanceSubsystemLifecycle.as"),
		TEXT(R"AS(
UCLASS()
class UScenarioGameInstanceLifecycleTracker : UScriptGameInstanceSubsystem
{
	UFUNCTION(BlueprintOverride)
	void BP_Initialize() {}

	UFUNCTION(BlueprintOverride)
	void BP_Deinitialize() {}
}
)AS"),
		CompileResult);

	// ★ 这不是“应该通过”的用例；测试明确要求它现在失败
	if (!TestFalse(TEXT("Scenario game-instance subsystem script generation remains unsupported on this branch"), bCompiled))
	{
		return false;
	}

	TestEqual(TEXT("Scenario game-instance subsystem lifecycle should currently fail compilation on this branch"), CompileResult, ECompileResult::Error);
	return true;
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptEditor/Public/BaseClasses/ScriptEditorSubsystem.h
// 位置: 7-18, 71-84
// 说明: editor subsystem 基类在 current 中也基本是原样保留
// ============================================================================
UCLASS(NotBlueprintable, Abstract, Meta = (NoBlueprintsOfChildren))
class ANGELSCRIPTEDITOR_API UScriptEditorSubsystem : public UEditorSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	UScriptEditorSubsystem(const FObjectInitializer& Initializer)
		: Super()
		, FTickableGameObject(IsTemplate() ? ETickableTickType::Never : ETickableTickType::NewObject)
	{}

	virtual bool IsTickableInEditor() const override
	{
		return true;
	}

	virtual void Tick(float DeltaTime) override
	{
		// ★ editor-side BP_Tick hook 也还在
		FEditorScriptExecutionGuard ScriptGuard;
		BP_Tick(DeltaTime);
	}
};
```

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| `ScriptGameInstanceSubsystem` / `ScriptEditorSubsystem` 基类是否存在 | 存在 | 存在 | **没有实现缺口** |
| 基类 surface 是否大体平移 | 是 | 是 | **实现方式基本延续** |
| 脚本子类生成是否已闭环 | 设计上是 authoring surface | current 自动化明确要求失败 | **局部没有实现正向生成能力** |
| `ScriptEditorSubsystem` 的失败状态是否已被 current 用例直接证实 | 本轮未查测试 | 本轮未查到同位失败用例 | **仅能谨慎推断，不宜过度下结论** |

这一段把 subsystem 维度的判断压实成一句话：**current 的问题不是“没有 subsystem 基类”，而是“把基类拷过来了，但脚本类生成链还没有从测试意义上宣告完成”。**

---

## 深化分析 (2026-04-09 23:28:39)

### [维度 D1/D4] current 新增了 `external -> internal -> base` 三层模块命名 contract；UEAS2 仍是单命名空间

前文已经把 `manager singleton -> engine scope` 和 `MultiEngineTests` 的 owner 矩阵写清了，这一段只补 **模块名 ownership** 本身。UEAS2 的 `ActiveModules`、`ScriptModule->name`、外部查询参数基本都还是同一个 raw module name。current 则在 clone engine 模式下新增了一层 internal module name：`MakeModuleName()` 会把 raw 名改写成 `Clone_n::Module`，`ActiveModules` 与 `ScriptModule->name` 用 internal name，`baseModuleName` 保留 raw name，`GetModuleByModuleName()` 再把外部 raw 名折回 internal key。

这不是 hot reload 原本就有的 `_OLD_/_NEW_` 临时后缀，而是 AngelPortV2 为了让多个 clone engine 可以同时装入**同名脚本模块**新增的一层命名空间。也因此，current 的“去 Loader 化 + engine scope”不只是生命周期 owner 变化，还顺手把 module identity 从“全局唯一名字”改成了“外部名字 + engine 内部名字”的双层模型。

```
[D1/D4] Module Naming Ownership
UEAS2
raw module name
├─ ActiveModules["Gameplay.Player"]              // 单例 manager 直接保存 raw key
├─ ScriptModule.name = "Gameplay.Player"
└─ external/runtime/debug all use same token     // 内外部标识一致

current
raw module name
├─ GetModuleByModuleName("Gameplay.Player")      // 外部 API 仍使用 raw 名
└─ MakeModuleName()
   ├─ Full engine  -> "Gameplay.Player"
   └─ Clone engine -> "Clone_1::Gameplay.Player"
      ├─ ActiveModules[internal]
      ├─ ScriptModule.name = internal
      └─ ScriptModule.baseModuleName = raw       // 给外部协议留稳定别名
```

### 关键源码引用

[1] current 的 clone engine 会分配 `InstanceId`，并在查找/换入脚本模块时系统性套上 internal name；同时又把 `baseModuleName` 留成 raw name：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 595-602, 628-640, 2913-2938, 4251-4260
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h
// 位置: 311-317
// 说明: current 明确区分 external module name、internal module name、base module name
// ============================================================================
FString FAngelscriptEngine::MakeModuleName(const FString& ModuleName) const
{
	if (CreationMode == EAngelscriptEngineCreationMode::Clone && !InstanceId.IsEmpty())
	{
		// ★ clone engine 内部统一加前缀，避免同名模块互撞
		return FString::Printf(TEXT("%s::%s"), *InstanceId, *ModuleName);
	}

	return ModuleName;
}

TUniquePtr<FAngelscriptEngine> FAngelscriptEngine::CreateCloneFrom(...)
{
	...
	EngineInstance->CreationMode = EAngelscriptEngineCreationMode::Clone;
	EngineInstance->InstanceId = MakeEngineInstanceId(TEXT("Clone"));
	// ★ 每个 clone 都会拿到自己的内部命名空间
	...
}

TSharedPtr<struct FAngelscriptModuleDesc> GetModule(const FString& ModuleName)
{
	auto* ModRef = ActiveModules.Find(MakeModuleName(ModuleName));
	// ★ 外部仍传 raw 名，内部统一折成 internal key
	...
}

void FAngelscriptEngine::SwapInModules(...)
{
	const FString InternalModuleName = MakeModuleName(Module->ModuleName);
	auto* OldModule = ActiveModules.Find(InternalModuleName);
	...
	Module->ScriptModule->SetName(TCHAR_TO_ANSI(*InternalModuleName));
	ActiveModules.Add(InternalModuleName, Module);
}

FString TempName = MakeModuleName(Module->ModuleName);
auto* ScriptModule = (asCModule*)Engine->GetModule(TCHAR_TO_ANSI(*TempName), asGM_ALWAYS_CREATE);
ScriptModule->baseModuleName = TCHAR_TO_ANSI(*Module->ModuleName);
// ★ AngelScript 内核对象同时保留 internal name 和 external base name
```

[2] current 不只是在实现里“顺手这么做”，它还把“clone 内部名必须隔离、外部 raw lookup 仍要可用”写成了自动化契约：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineTests.cpp
// 位置: 320-331
// 说明: 多 engine 测试直接锁定 internal name 隔离 + external lookup 可用
// ============================================================================
asIScriptModule* CloneAModule = FAngelscriptMultiEngineTestAccess::CreateNamedModule(*CloneA, ModuleName);
asIScriptModule* CloneBModule = FAngelscriptMultiEngineTestAccess::CreateNamedModule(*CloneB, ModuleName);
...
TestTrue(TEXT("MultiEngine.CloneModuleIsolation should give Clone A an internal module name"), FAngelscriptMultiEngineTestAccess::MakeModuleName(*CloneA, ModuleName).Contains(TEXT("::")));
TestTrue(TEXT("MultiEngine.CloneModuleIsolation should give Clone B an internal module name"), FAngelscriptMultiEngineTestAccess::MakeModuleName(*CloneB, ModuleName).Contains(TEXT("::")));
TestNotEqual(TEXT("MultiEngine.CloneModuleIsolation should isolate internal module names per clone"), FAngelscriptMultiEngineTestAccess::MakeModuleName(*CloneA, ModuleName), FAngelscriptMultiEngineTestAccess::MakeModuleName(*CloneB, ModuleName));
TestTrue(TEXT("MultiEngine.CloneModuleIsolation should keep external lookup working for Clone A"), CloneA->GetModuleByModuleName(ModuleName).IsValid());
TestTrue(TEXT("MultiEngine.CloneModuleIsolation should keep external lookup working for Clone B"), CloneB->GetModuleByModuleName(ModuleName).IsValid());
```

[3] UEAS2 则没有这层 internal namespace；查找和 swap-in 都直接按 raw module name 走：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Public\AngelscriptManager.h
// 位置: 208-214
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\AngelscriptManager.cpp
// 位置: 1653-1676
// 说明: 上游没有 clone namespace 这一层，module identity 基本就是单 token
// ============================================================================
TSharedPtr<struct FAngelscriptModuleDesc> GetModule(const FString& ModuleName)
{
	auto* ModRef = ActiveModules.Find(ModuleName);
	// ★ raw 名就是查找 key
	...
}

auto* OldModule = ActiveModules.Find(Module->ModuleName);
...
Module->ScriptModule->SetName(TCHAR_TO_ANSI(*Module->ModuleName));
ActiveModules.Add(Module->ModuleName, Module);
```

#### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| 同名脚本模块在多个运行时实例间并存 | 未见同层 internal namespace | `MakeModuleName()` + clone `InstanceId` 显式隔离 | **能力边界增强** |
| 外部 API 是否仍能用 raw module name | raw 名就是内部名 | raw 名经 `MakeModuleName()` 折到 internal key | **实现方式不同** |
| 新增复杂度来自哪里 | 几乎没有“内外名字分裂”问题 | downstream consumer 必须明确自己要的是 raw 还是 internal | **新复杂度引入** |

这一节最重要的新结论是：**current 相对 UEAS2 并不是简单“多了 clone 测试”，而是把 module identity 本身重构成了双层命名 contract。** 这条 contract 让多 engine 并存成为可能，但也要求所有观测链都明确自己消费哪一层名字。

### [维度 D5] `DebugServer V2` 在 clone engine 场景下出现 `baseModuleName` 与 internal `name` 的双轨

前文已经确认 opcode、framing、握手版本和大多数 payload 语义都保持 V2 主线。本轮补的是一个更窄的 identifier 细节：**在 current 里，断点存储和 callstack payload 已经不必然使用同一层 module name。**

原因很直接。`DebugServer` 的 breakpoint store 仍按 `baseModuleName` 建索引，这一点和 UEAS2 一样；但 callstack payload 取的是 `ScriptFunction->GetModuleName()`。AngelScript core 里这个接口返回的是 `module->name`，不是 `baseModuleName`。在 UEAS2 里两者通常相等，所以没有额外问题；在 current 的 clone engine 里，`module->name` 可能是 `Clone_n::Gameplay.Player`，而 `baseModuleName` 仍是 `Gameplay.Player`。

这不是“协议代际分裂”，而是 **同一套 `DebugServer V2` 消息，在 clone session 下多出一个 module identifier fork**。

```
[D5] Debugger Module Identifier Path
current breakpoint store
script context
└─ module->baseModuleName -> Breakpoints["Gameplay.Player"]      // raw key

current callstack payload
script function
└─ GetModuleName() -> module->name -> "Clone_1::Gameplay.Player" // internal key 可能发给 client

UEAS2
module->baseModuleName == module->name == "Gameplay.Player"      // 基本没有双轨
```

### 关键源码引用

[1] current 的断点命中缓存仍按 `baseModuleName` 入表，但 callstack 发包走的是 `GetModuleName()`：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 位置: 589-593, 1470-1474
// 说明: current 的 DebugServer 在不同子路径里消费了两层不同的 module name
// ============================================================================
TSharedPtr<FFileBreakpoints>& ActiveBreakpoints = SectionBreakpoints.FindOrAdd(Section);
if (!ActiveBreakpoints.IsValid())
{
	FString ModuleName = ANSI_TO_TCHAR(Context->m_currentFunction->module->baseModuleName.AddressOf());
	TSharedPtr<FFileBreakpoints>& BreakpointStore = Breakpoints.FindOrAdd(ModuleName);
	// ★ breakpoint owner 仍按 raw/base 名聚合
}

Frame.LineNumber = Context->GetLineNumber(i, nullptr, &SectionName);
Frame.Source = SectionName ? ANSI_TO_TCHAR(SectionName) : TEXT("");
if (AngelscriptDebugServer::DebugAdapterVersion >= 1)
	Frame.ModuleName = ScriptFunction->GetModuleName() ? ANSI_TO_TCHAR(ScriptFunction->GetModuleName()) : TEXT("");
// ★ callstack payload 直接发送 ScriptFunction->GetModuleName()
```

[2] AngelScript core 的 `GetModuleName()` 返回的是 `module->name`；而前一节已经证明 current clone 会把 `module->name` 改写成 internal name：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_scriptfunction.cpp
// 位置: 572-576
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_module.h
// 位置: 237-240
// 说明: GetModuleName() 取的是 name，不是 baseModuleName
// ============================================================================
const char *asCScriptFunction::GetModuleName() const
{
	if( module )
	{
		// ★ 这里返回的是 module->name
		return module->name.AddressOf();
	}
}

asCString name;
asCString baseModuleName;
// ★ AngelScript 内核同时保存两份名字，但 GetModuleName() 并不返回 baseModuleName
```

[3] UEAS2 的 DebugServer 写法几乎同构，但由于它没有 clone internal namespace，`baseModuleName` 与 `GetModuleName()` 在正常路径上仍然是同一个 token：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Debugging\AngelscriptDebugServer.cpp
// 位置: 488-492, 1345-1349
// 说明: 上游也有 base/name 双字段，但没有 current 那层 clone internal prefix
// ============================================================================
TSharedPtr<FFileBreakpoints>& ActiveBreakpoints = SectionBreakpoints.FindOrAdd(Section);
if (!ActiveBreakpoints.IsValid())
{
	TSharedPtr<FFileBreakpoints>& BreakpointStore = Breakpoints.FindOrAdd(ANSI_TO_TCHAR(Context->m_currentFunction->module->baseModuleName.AddressOf()));
	// ★ 上游也按 base 名存断点
}

Frame.LineNumber = Context->GetLineNumber(i, nullptr, &SectionName);
Frame.Source = SectionName ? ANSI_TO_TCHAR(SectionName) : TEXT("");
if (AngelscriptDebugServer::DebugAdapterVersion >= 1)
	Frame.ModuleName = ScriptFunction->GetModuleName() ? ANSI_TO_TCHAR(ScriptFunction->GetModuleName()) : TEXT("");
// ★ 但 UEAS2 正常情况下 name≈base，不会额外暴露 clone 前缀
```

#### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| `DebugServer V2` opcode / framing / 主消息面 | 保持 V2 | 保持 V2 | **没有协议代际差距** |
| breakpoint key 与 callstack `ModuleName` 是否天然同 token | 基本是 | clone 模式下可能分叉成 raw vs internal | **实现质量风险** |
| 这个差异的来源 | 单例命名空间 | multi-engine internal namespace 引入的新边角 | **不是缺功能，而是新架构副作用** |

按源码推断，**如果 clone 调试会话的 client 直接拿 `CallStack.Frame.ModuleName` 回填 breakpoint UI 或做 module 级聚合，就需要额外去前缀或归一化。** 这个判断来自上面的命名链与发送路径；它不是在说 current “没有 DebugServer V2”，而是在说 **clone-specific identifier normalization 还没有和 UEAS2 一样天然收敛成单 token。**

### [维度 D4/D9] external lookup shim 并未在所有 observer 链统一复用；`CodeCoverage` clone 路径存在 double-prefix 推断风险

同一套命名 contract 还影响另一条此前没单独钉住的观测链：`CodeCoverage`。current 的 `GetModule(const FString&)` 会先做一次 `MakeModuleName()`；与此同时，行命中路径又直接把 `CurrentFunction->GetModuleName()` 结果回喂给 `GetModule(...)`。如果当前执行的是 clone engine，而 `GetModuleName()` 已经返回 internal name，那么这一跳按源码就会形成 `Clone_1::Clone_1::Gameplay.Player` 这样的二次前缀。

UEAS2 没有这层 tension，因为它的 manager lookup 本来就用 raw module name；`CurrentFunction->GetModuleName()` 回填到 `GetModule(...)` 时不会再被额外改写。

这里要和“有没有 coverage”区分开：**coverage recorder 本身两边都有，差异不在能力有无，而在 current 的 multi-engine 命名改造后，observer lookup 是否已经全部做了 internal/raw 归一化。**

```
[D4/D9] Coverage Lookup Normalization
current clone hit
CurrentFunction->GetModuleName() = "Clone_1::Gameplay.Player"
└─ GetModule("Clone_1::Gameplay.Player")
   └─ MakeModuleName(...) -> "Clone_1::Clone_1::Gameplay.Player" // 二次前缀风险

UEAS2 hit
CurrentFunction->GetModuleName() = "Gameplay.Player"
└─ GetModule("Gameplay.Player")
   └─ ActiveModules["Gameplay.Player"]                           // 直接命中
```

### 关键源码引用

[1] current 的外部查询 helper 总是先做一次 `MakeModuleName()`：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h
// 位置: 311-317
// 说明: 这是 current 保证“外部 raw lookup 仍然可用”的 shim
// ============================================================================
TSharedPtr<struct FAngelscriptModuleDesc> GetModule(const FString& ModuleName)
{
	auto* ModRef = ActiveModules.Find(MakeModuleName(ModuleName));
	// ★ 调用者传的是 external 名，内部再归一化成真正的 map key
	...
}
```

[2] 但 current 的 code coverage 行命中路径回喂的是 `CurrentFunction->GetModuleName()`，不是 raw `baseModuleName`：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 位置: 5540-5546
// 说明: observer 路径这里消费的是 AngelScript function 自己报出的 module->name
// ============================================================================
if (AngelscriptManager.CodeCoverage != nullptr)
{
	int Line = Context->GetLineNumber(0, &Column, nullptr);
	asIScriptFunction* CurrentFunction = Context->GetFunction(0);
	FString ModuleName = ANSI_TO_TCHAR(CurrentFunction->GetModuleName());
	TSharedPtr<struct FAngelscriptModuleDesc> Module = AngelscriptManager.GetModule(ModuleName);
	// ★ 若 ModuleName 已经是 internal name，这里会再次进入 MakeModuleName()
}
```

[3] UEAS2 的同位路径没有这层二次归一化问题，因为 lookup 本来就直接按 raw 名查：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Public\AngelscriptManager.h
// 位置: 208-214
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\AngelscriptManager.cpp
// 位置: 4041-4044
// 说明: 上游没有 internal namespace，因此 observer 路径直接命中 module map
// ============================================================================
TSharedPtr<struct FAngelscriptModuleDesc> GetModule(const FString& ModuleName)
{
	auto* ModRef = ActiveModules.Find(ModuleName);
	// ★ raw 名直接就是 map key
	...
}

int Line = Context->GetLineNumber(0, &Column, nullptr);
asIScriptFunction* CurrentFunction = Context->GetFunction(0);
FString ModuleName = ANSI_TO_TCHAR(CurrentFunction->GetModuleName());
TSharedPtr<struct FAngelscriptModuleDesc> Module = AngelscriptManager.GetModule(ModuleName);
```

#### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| code coverage / runtime observer 是否存在 | 存在 | 存在 | **没有实现缺口** |
| multi-engine internal name 引入后 observer lookup 是否完全归一化 | 不适用，无 internal namespace | 按源码看仍有 call site 直接回喂 internal name | **实现质量差异（源码推断）** |
| 风险范围 | 单例 manager 下基本不会踩中 | clone / 多 engine 观测链更容易暴露 | **新架构带来的边角成本** |

如果后续继续吸收/修正，最具体的参考点已经很清楚：**凡是消费 `CurrentFunction->GetModuleName()` 的 observer/debugger 路径，都要明确它要的是 internal name、还是 raw `baseModuleName`，或者干脆直接改成 `GetModule(asIScriptModule*)` 这类不会再做字符串归一化的入口。** 这一点在 UEAS2 不显眼，是因为它没有 multi-engine internal namespace；在 current 里则属于应当中高优先级盯住的命名一致性问题。

---

## 深化分析 (2026-04-09 23:37:37)

### [维度 D5/D8/D11] DebugServer 折叠 Unreal/BP frame 的判定条件，已经从 `flag-based` 退成 `type-based`

### 实现概述

前文已经把 `RuntimeGenerated -> NativeThunk` 主线写清，这一轮只补一个更窄、但非常具体的 tooling 后果：**调试器今天如何判断“这个 `UFunction` 其实是 Angelscript 自己生成的，不该再额外当成 Blueprint/Unreal frame 展开”。**

- UEAS2 因为仍保留 `FUNC_RuntimeGenerated`，所以 `DebugServer` 可以直接用函数 flag 做过滤。这个判断不需要知道 `UASFunction` 类型本身，也不依赖插件私有 RTTI。
- current 的同位逻辑已经不再看 `FUNC_RuntimeGenerated`，而是调用 `IsAngelscriptGenerated(Function)`；这个 helper 的实现只有一行：`Cast<const UASFunction>(Function) != nullptr`。
- 这不是协议版本差异，但它说明 **current 的调试器已经从“消费引擎公共 flag”切成了“消费插件私有类型”**。换句话说，UEAS2 的识别谓词是 engine-visible 的，current 的识别谓词则是 plugin-type-aware 的。

```
[D5/D8/D11] Debug Frame Filtering Predicate
UEAS2
DebugServer
└─ UFunction* Function
   └─ HasAnyFunctionFlags(FUNC_RuntimeGenerated)
      └─ skip Unreal/BP frame                     // 用公共 flag 识别脚本生成函数

current
DebugServer
└─ UFunction* Function
   └─ IsAngelscriptGenerated(Function)
      └─ Cast<const UASFunction>(Function) != nullptr
         └─ skip Unreal/BP frame                  // 用插件私有类型识别
```

### 关键源码引用

[1] UEAS2 在两处 callstack 拼装路径里都直接依赖 `FUNC_RuntimeGenerated`：

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Debugging\AngelscriptDebugServer.cpp
// 位置: 1289-1291, 2243-2245
// 说明: 上游调试器直接用公共 function flag 折叠 Angelscript 生成的 Unreal frame
// ============================================================================
UFunction* Function = StackView[BPStackIndex]->Node;
if (Function == nullptr || Function->HasAnyFunctionFlags(FUNC_RuntimeGenerated))
	continue;
// ★ 只要还是 RuntimeGenerated，就不再把它当成普通 Blueprint/Unreal frame 展开
```

[2] current 的同位逻辑已经改成 `IsAngelscriptGenerated()` helper，而 helper 本体只认 `UASFunction` 类型：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h
// 位置: 213-218
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp
// 位置: 1414-1416, 2314-2316
// 说明: current 不再依赖引擎公共 flag，而是依赖插件私有类型判断
// ============================================================================
ANGELSCRIPTRUNTIME_API void UASFunctionNativeThunk(UObject* Object, FFrame& Stack, RESULT_DECL);

inline bool IsAngelscriptGenerated(const UFunction* Function)
{
	return Cast<const UASFunction>(Function) != nullptr;
}

UFunction* Function = StackView[BPStackIndex]->Node;
if (Function == nullptr || IsAngelscriptGenerated(Function))
	continue;
// ★ 现在要先知道 UASFunction 这个插件类型，才能完成同样的 frame 折叠
```

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| 调试器是否仍会折叠脚本生成的 Unreal/BP frame | 会 | 会 | **没有实现缺口** |
| 折叠判定是否仍可只靠公共 `UFunction` flag | 是，`FUNC_RuntimeGenerated` | 否，改成 `Cast<UASFunction>` | **实现方式不同** |
| tooling 对插件私有类型的耦合 | 低 | 更高 | **工程边界变化** |

这条结论比“有没有 DebugServer V2”更细，但很关键：**current 已经把一部分原本能靠 engine flag 判断的 tooling 语义，改写成了对 `UASFunction` 私有类型的显式认知。** 对可分发插件来说这是合理代价；但任何想在插件外部复用这套判定的工具，都不能再只看 stock `UFunction` flags。

### [维度 D2/D6/D11] auto-export 的作用域已从 `ScriptCallable` 全局语义收缩成 `Build.cs` 依赖闭包

### 实现概述

前文已经说明 current 把大量 `ScriptCallable` authoring 面退回成 `BlueprintCallable` + exporter。但补完 exporter 入口之后，还能再往前推进一步：**current 的自动导出不只是“换了 specifier”，而是连可见模块集合都从 engine-global 变成了 runtime dependency closure。**

- UEAS2 直接在 engine 版 UHT 里注册 `ScriptCallable` specifier，`UhtFunction::ResolveSelf()` 也把它和 `BlueprintCallable` 并列看待；下游 `Bind_BlueprintEvent.cpp` 再直接消费 `NAME_Event_ScriptCallable` metadata。这个语义天然覆盖整个 UHT 会话。
- current 的 `AngelscriptFunctionTable` exporter 虽然会扫描 `factory.Session.Modules`，但真正生成前先解析 `AngelscriptRuntime.Build.cs`，只保留 `supportedModules.All` 里的模块；`editorOnly` 也同样靠这个依赖图推断。
- 再加上 `IsBlueprintCallable()` 只认 `BlueprintCallable/Pure`，以及 exporter 主动产出 `AS_FunctionTable_SkippedEntries.csv` / `SkippedReasonSummary.csv`，可以把 current 的 contract 总结成一句：**自动导出覆盖面 = stock-UHT 可表达函数 ∩ runtime 依赖闭包 ∩ 签名可重建集合。**

```
[D2/D6/D11] Auto Export Scope
UEAS2
engine UHT
├─ register ScriptCallable specifier
├─ ResolveSelf treats ScriptCallable as callable
└─ bind layer reads metadata directly
   └─ module scope = whole UHT session

current
AngelscriptFunctionTable exporter
├─ Session.Modules
├─ parse AngelscriptRuntime.Build.cs
│  └─ supportedModules / editorOnlyModules
├─ accept BlueprintCallable / BlueprintPure only
└─ emit generated shards + skipped CSV
   └─ module scope = runtime dependency closure
```

### 关键源码引用

[1] UEAS2 的 `ScriptCallable` 是 engine-UHT 全局语义，不依赖插件自己去猜模块边界：

```csharp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Programs\Shared\EpicGames.UHT\Specifiers\UhtFunctionSpecifiers.cs
// 位置: 23-28
// 文件: J:\UnrealEngine\UEAS2\Engine\Source\Programs\Shared\EpicGames.UHT\Types\UhtFunction.cs
// 位置: 570-573
// 说明: 上游把 ScriptCallable 直接写进 UHT 语义层
// ============================================================================
[UhtSpecifier(Extends = UhtTableNames.Function, ValueType = UhtSpecifierValueType.Legacy)]
private static void ScriptCallableSpecifier(UhtSpecifierContext specifierContext)
{
	UhtFunction function = (UhtFunction)specifierContext.Type;
	function.MetaData.Add("ScriptCallable", "");
}

bool storeCppDefaultValueInMetaData = FunctionFlags.HasAnyFlags(EFunctionFlags.BlueprintCallable | EFunctionFlags.Exec)
	|| MetaData.ContainsKey("ScriptCallable");
// ★ ScriptCallable 与 BlueprintCallable 一起进入后续 UHT 处理
```

```cpp
// ============================================================================
// 文件: J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\Binds\Bind_BlueprintEvent.cpp
// 位置: 543-544
// 说明: 下游 bind 层直接消费 ScriptCallable metadata
// ============================================================================
if (!Function->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintPure) && !Function->HasMetaData(NAME_Event_ScriptCallable))
	FAngelscriptBinds::SetPreviousBindIsCallable(false);
```

[2] current 的 exporter 入口和筛选规则都更“插件化”，也更显式受 `AngelscriptRuntime.Build.cs` 约束：

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs
// 位置: 21-26, 46-56
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs
// 位置: 51-67, 334-384
// 说明: current 不是全会话自动暴露，而是先做模块依赖闭包筛选
// ============================================================================
[UhtExporter(
	Name = "AngelscriptFunctionTable",
	Description = "Exports Angelscript function table data",
	Options = UhtExporterOptions.Default | UhtExporterOptions.CompileOutput,
	CppFilters = ["AS_FunctionTable_*.cpp"],
	ModuleName = "AngelscriptRuntime")]

internal static bool IsBlueprintCallable(UhtFunction function)
{
	string functionFlags = function.FunctionFlags.ToString();
	return function.FunctionType == UhtFunctionType.Function &&
		(functionFlags.Contains("BlueprintCallable", StringComparison.Ordinal) ||
		functionFlags.Contains("BlueprintPure", StringComparison.Ordinal));
}

AngelscriptSupportedModules supportedModules = LoadSupportedModules(factory);
foreach (UhtModule module in factory.Session.Modules)
{
	if (!supportedModules.All.Contains(module.ShortName))
	{
		continue;
	}
	// ★ 只有 runtime 依赖闭包内的模块才会生成 function table shard
}

string buildCsPath = ResolveRuntimeBuildCsPath(factory);
...
HashSet<string> allModules = new(StringComparer.OrdinalIgnoreCase) { "AngelscriptRuntime" };
foreach (string rawLine in File.ReadAllLines(buildCsPath))
{
	...
	foreach (Match match in QuotedStringPattern.Matches(line))
	{
		string moduleName = match.Groups[1].Value;
		allModules.Add(moduleName);
		if (inEditorBlock)
		{
			editorOnlyModules.Add(moduleName);
		}
	}
}
```

[3] current 还把“没覆盖到哪里”写成产物，而不是让缺口静默存在：

```csharp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs
// 位置: 37-43, 99-101
// 说明: current 主动把 skip 面输出成审计文件
// ============================================================================
foreach (UhtModule module in factory.Session.Modules)
{
	packageCount++;
	CountBlueprintCallableFunctions(module.ShortName, module.ScriptPackage, skippedEntries, ref classCount, ref functionCount, ref reconstructedCount, ref skippedCount);
}
WriteSkippedEntriesCsv(factory, skippedEntries);

string csvPath = factory.MakePath("AS_FunctionTable_SkippedEntries", ".csv");
Directory.CreateDirectory(Path.GetDirectoryName(csvPath)!);
// ★ “未导出原因”是显式产物，不再靠人工猜
```

### 差距判断

| 子问题 | UEAS2 | 当前插件 | 判断 |
| --- | --- | --- | --- |
| callable authoring 是否由 UHT 主干直接理解 | `ScriptCallable` 是 engine-UHT specifier | 否，主要退回 `BlueprintCallable/Pure` | **实现方式不同** |
| 自动导出的模块作用域 | 整个 UHT 会话天然可见 | 受 `AngelscriptRuntime.Build.cs` 依赖闭包约束 | **交付边界收缩** |
| 缺口是否可审计 | 主要依赖引擎语义直接吃下 | `SkippedEntries.csv` / `SkippedReasonSummary.csv` 显式落盘 | **工程化增强** |

这一节对应的准确结论是：**current 不是没有 auto-export，而是把 auto-export 从“引擎全局语义”改造成“插件内可审计流水线”。** 代价是 authoring 面更窄、模块边界更显式；收益是无需引擎补丁也能把“哪些函数能进、哪些函数进不来、为什么进不来”稳定落盘。
