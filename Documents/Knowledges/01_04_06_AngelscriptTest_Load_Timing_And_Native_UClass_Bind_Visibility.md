# `AngelscriptTest` 原生 `UClass` 的 AS 可见性与加载时序缺口

> **所属模块**: 插件模块清单与装载关系 → Test 模块加载时序 / Native `UClass` Bind Visibility
> **关键源码**: `Plugins/Angelscript/Angelscript.uplugin`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTestModule.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestActor.h`

这篇文档只回答一个具体但很容易误判的问题：为什么 `AngelscriptTest` 模块里那些本来应该能暴露给 AngelScript 的原生 `UClass`，在主 `FAngelscriptEngine` 里看不到。结论先说在前面：当前问题的主因不是 `ShouldBindEngineType()` 把它们全过滤掉了，而是 **Runtime 在 `AngelscriptTest` 模块真正加载之前就已经执行完了首轮 `Bind_BlueprintType` 扫描，而 Test 模块后续又没有触发 native bind replay。**

## 先把结论钉死

- `Bind Blueprint` 的起点不是 `OnPostEngineInit`，而是 `FAngelscriptRuntimeModule::StartupModule()` 触发 `InitializeAngelscript()` 后，`FAngelscriptEngine::Initialize()` 内部的 `BindScriptTypes()`
- Editor 构建下 `AS_USE_BIND_DB = !WITH_EDITOR`，所以当前走的是 **live reflection scan**，即直接对当前进程里已经存在的 `TObjectRange<UClass>()` 做两轮扫描，而不是用 `Binds.Cache` 回放
- `AngelscriptTest` 模块本身声明为 `PostDefault`，且插件描述顺序位于 `AngelscriptRuntime`、`AngelscriptEditor` 之后；结合 UE 模块逐个 `LoadModule -> StartupModule` 的通用语义，可以推断 Runtime 的主引擎初始化先于 Test 模块 `StartupModule()`
- `AngelscriptTestModule::StartupModule()` 当前只打日志，不做任何 native 补注册，也没有调用 native bind replay 逻辑
- 因此问题的精确定义不是“`AngelscriptTest` 中所有 `UCLASS` 都不会注册”，而是：**`AngelscriptTest` 中那些本来满足 `ShouldBindEngineType()` 的 native `UClass`，因为模块加载时序太晚，错过了主引擎首轮 bind pass**

## 总调用链

- 这条问题链的关键不在单个函数，而在 **模块装载顺序 + 引擎初始化顺序 + bind pass 只扫当前已存在 `UClass`** 三件事同时成立
- 先发生的是 Runtime 模块启动和主引擎初始化；后发生的才是 Test 模块加载
- 绑定链路中没有“模块后加载时重新补扫原生 `UClass`”这一环

```text
UE PostDefault Module Load                               // UE PostDefault 阶段模块装载
├─ AngelscriptRuntime                                    // 先加载 Runtime
│  └─ FAngelscriptRuntimeModule::StartupModule()
│     └─ InitializeAngelscript()
│        ├─ Create / adopt primary FAngelscriptEngine    // 创建或接管主引擎
│        └─ FAngelscriptEngine::Initialize()
│           ├─ PreInitialize_GameThread()
│           ├─ Initialize_AnyThread()
│           │  ├─ Load BindModules.Cache                 // 读取 bind module 名单
│           │  ├─ Load generated/manual bind modules     // 装载直绑函数入口
│           │  └─ ★ BindScriptTypes()
│           │     └─ FAngelscriptBinds::CallBinds()
│           │        ├─ Bind_BlueprintType_Declarations  // 第一轮：声明并注册类型
│           │        │  └─ TObjectRange<UClass>()        // 只看当前已存在的 UClass
│           │        └─ Bind_Defaults                    // 第二轮：绑定函数/属性
│           │           └─ TObjectRange<UClass>() + TFieldIterator<UFunction>()
│           └─ PostInitialize_GameThread()
├─ AngelscriptEditor                                     // 随后加载 Editor
└─ AngelscriptTest                                       // 更晚加载 Test
   └─ FAngelscriptTestModule::StartupModule()
      └─ Log only                                        // 不触发 native bind replay
```

## 谁触发了 `Bind_BlueprintType`

- 真正触发 bind pass 的不是 `Bind_BlueprintType.cpp` 自己，而是 `FAngelscriptBinds::CallBinds()`
- `CallBinds()` 又是 `FAngelscriptEngine::BindScriptTypes()` 调的
- `BindScriptTypes()` 又在 `FAngelscriptEngine::Initialize_AnyThread()` 里被主引擎初始化路径调用

```text
Bind_BlueprintType_Declarations / Bind_Defaults          // 目标 bind 项
└─ Called by: FAngelscriptBinds::CallBinds()
   └─ Called by: FAngelscriptEngine::BindScriptTypes()
      └─ Called by: FAngelscriptEngine::Initialize_AnyThread()
         └─ Called by: FAngelscriptEngine::Initialize()
            └─ Called by: FAngelscriptRuntimeModule::InitializeAngelscript()
               └─ Called by: FAngelscriptRuntimeModule::StartupModule()
```

先看 Runtime 模块入口和引擎 bind 入口：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp
// 函数: FAngelscriptRuntimeModule::StartupModule / InitializeAngelscript
// 位置: Runtime 模块启动后，决定是否立刻初始化主 Angelscript 引擎
// ============================================================================
void FAngelscriptRuntimeModule::StartupModule()
{
    bool bIsEditor = GIsEditor;
    bool bIsRunningCommandlet = IsRunningCommandlet();

    if (bIsEditor || bIsRunningCommandlet)
    {
        // ★ 在 Editor / Commandlet 启动阶段就进入 Angelscript 初始化链
        InitializeAngelscript();
    }

    if (bIsEditor)
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

    // ★ 这里不是等待 Test 模块，而是直接确保 Runtime 已加载并创建主引擎
    FModuleManager::Get().LoadModuleChecked(TEXT("AngelscriptRuntime"));

    if (FAngelscriptEngine* CurrentEngine = FAngelscriptEngine::TryGetCurrentEngine())
    {
        if (CurrentEngine->GetScriptEngine() == nullptr)
        {
            CurrentEngine->Initialize();
        }
    }
    else
    {
        OwnedPrimaryEngine = MakeUnique<FAngelscriptEngine>();
        FAngelscriptEngineContextStack::Push(OwnedPrimaryEngine.Get());
        OwnedPrimaryEngine->Initialize();
    }
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp
// 函数: FAngelscriptEngine::BindScriptTypes
// 位置: 主引擎 AnyThread 初始化阶段的 bind 执行入口
// ============================================================================
void FAngelscriptEngine::BindScriptTypes()
{
    FAngelscriptBinds::ResetGeneratedFunctionTableTiming();

    // ★ 统一执行所有通过 FAngelscriptBinds::FBind 静态注册进来的 bind 项
    FAngelscriptBinds::CallBinds(CollectDisabledBindNames());

    FAngelscriptBinds::LogGeneratedFunctionTableTimingSummary();
}
```

`Bind_BlueprintType.cpp` 并不是某处显式手写 `Call` 的函数，而是通过静态 `FBind` 对象挂进 bind 队列：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h
// 类型: FAngelscriptBinds::FBind
// 位置: 所有 Bind_*.cpp 用来静态注册 bind 执行项的统一门面
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
};
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp
// 函数: FAngelscriptBinds::CallBinds
// 位置: 把所有静态注册的 bind 项按顺序统一跑一遍
// ============================================================================
void FAngelscriptBinds::CallBinds(const TSet<FName>& DisabledBindNames)
{
    for (const FBindFunction& Bind : GetSortedBindArray())
    {
        if (DisabledBindNames.Contains(Bind.BindName))
        {
            continue;
        }

        // ★ 这里才真正执行 Bind_BlueprintType.cpp 里静态注册的 lambda
        Bind.Function();
    }
}
```

## `Bind_BlueprintType` 的两阶段职责

- `Bind_BlueprintType_Declarations` 是 **声明阶段**：决定哪些 `UClass` 要进入 AS 类型系统，并建立 `UClass -> AngelscriptType` 映射
- `Bind_Defaults` 是 **补全阶段**：在已经声明好的类型上继续绑定 BlueprintCallable / BlueprintEvent / Property / `StaticClass()`
- 这两个阶段都只扫描 **当前时刻已经存在于反射系统中的 `UClass`**

先看 Editor 路径的两个静态 bind 项：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp
// 位置: Editor 构建下的 live reflection bind 路径（AS_USE_BIND_DB == false）
// ============================================================================
AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_BlueprintType_Declarations(FAngelscriptBinds::EOrder::Early, []
{
    // ★ 第一轮：枚举当前进程里已经存在的所有 UClass
    for (UClass* Class : TObjectRange<UClass>())
    {
        if (!ShouldBindEngineType(Class))
        {
            continue;
        }

        FString ClassName = FAngelscriptType::GetBoundClassName(Class);
        BindUClass(Class, ClassName);
    }

    BindUClassLookup();
});

AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_Defaults((int32)FAngelscriptBinds::EOrder::Late + 100, []
{
    TArray<FBindOrder> ClassesToBind;
    TSet<UClass*> VisitedClasses;

    // ★ 第二轮：再次对当前已存在 UClass 做收集和排序
    for (UClass* Class : TObjectRange<UClass>())
    {
        FClassVisiter::Visit(ScriptEngine, Class, ClassesToBind, VisitedClasses);
    }

    for (auto& BindOrder : ClassesToBind)
    {
        // 绑定 BlueprintCallable / BlueprintEvent
        for (TFieldIterator<UFunction> FuncIt(BindOrder.Class, EFieldIteratorFlags::ExcludeSuper); FuncIt; ++FuncIt)
        {
            UFunction* Function = *FuncIt;

            if (Function->HasAnyFunctionFlags(FUNC_BlueprintEvent | FUNC_NetFuncFlags))
                BindBlueprintEvent(ClassType.ToSharedRef(), Function, DBMethod);
            else if (Function->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintPure))
                BindBlueprintCallable(ClassType.ToSharedRef(), Function, DBMethod);
        }
    }

    for (auto& BindOrder : ClassesToBind)
    {
        // 绑定 UObject Property、StaticClass()、并把结果写入 BindDatabase
        BindProperties(Binds, ClassType.ToSharedRef(), BindOrder.DBBind.Properties);
        BindStaticClass(Binds, TypeName, BindOrder.Class);
        FAngelscriptBindDatabase::Get().Classes.Add(BindOrder.DBBind);
    }
});
```

这里最重要的不是实现细节，而是两个“当前态”：

1. **第一轮 `TObjectRange<UClass>()` 取的是声明时刻的反射快照**
2. **第二轮 `TObjectRange<UClass>()` 取的是稍后同一次 bind pass 的反射快照**

如果 `AngelscriptTest` 模块在这两个阶段都还没有完成模块加载，那么它内部的 native `UClass` 就根本不在采样集合里。

## `ShouldBindEngineType()` 真正筛掉了什么

- 当前实现并不会把“所有 `UCLASS`”都绑定进 AS
- 只有满足 native / metadata / callable surface 等条件的类才会进入类型系统
- 所以这次问题必须区分“本来就不该绑定的测试桩”与“本来该绑定但没赶上扫描时机的测试类型”

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp
// 函数: ShouldBindEngineType
// 位置: live reflection bind 的 UClass 过滤器
// ============================================================================
bool ShouldBindEngineType(UClass* Class)
{
    if (Class == nullptr)
        return false;

    if (Class == UObject::StaticClass())
        return true;

    // ★ 只绑定 native class
    if (!Class->HasAnyClassFlags(CLASS_Native))
        return false;

    // 编辑器模拟 cooked 时排除 editor-only class
    if (!FAngelscriptEngine::ShouldUseEditorScriptsForCurrentContext())
    {
        if (IsEditorOnlyClass(Class))
            return false;
    }

    // 脚本运行时生成的 UASClass 不走这条原生 bind 路径
    UASClass* asClass = Cast<UASClass>(Class);
    if (asClass != nullptr && asClass->bIsScriptClass)
        return false;

    if (Class->HasMetaData(NAME_NotInAngelscript))
        return false;

    if (Class->GetBoolMetaData(NAME_BlueprintType))
        return true;

    if (Class->HasAnyClassFlags(CLASS_Interface) && Class != UInterface::StaticClass())
        return true;

    if (Class->HasMetaData(NAME_NotBlueprintType))
        return false;

    // ★ 兜底规则：如果类上有 BlueprintCallable / BlueprintEvent，也允许进入
    //    因此不是只有显式 BlueprintType 的类才会被扫进来
    ...
}
```

这解释了两个常见误区：

- 误区一：“`AngelscriptTest` 下所有 `UCLASS` 都应该进入 AS”  
  不对。很多仅用于 C++ 测试夹具的裸 `UCLASS()` 其实未必满足这个过滤条件
- 误区二：“既然有不少 `UCLASS()` 没进 AS，就一定是过滤规则坏了”  
  也不对。只要能找到一个 **本来满足条件** 的 `UClass` 也没进，就足以证明还存在时序问题

`AAngelscriptTestActor` 就是这样一个关键例子：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestActor.h
// 位置: Test 模块中一个明确可绑定的原生测试基类
// ============================================================================
UCLASS(Blueprintable, BlueprintType)
class ANGELSCRIPTTEST_API AAngelscriptTestActor : public AActor
{
    GENERATED_BODY()
    ...
};
```

它显式带了 `BlueprintType`，按规则本来就应该被 `ShouldBindEngineType()` 接受。因此如果主引擎里看不到它，问题就更像 **加载时序**，而不是 **过滤条件**。

## “注册到 AS 引擎”在这里到底是什么意思

- 这不是一个单点动作，而是三层可见性同时建立
- 只有把这三层拆开，才能准确判断“到底是类型没注册、函数没注册，还是缓存没更新”

```text
UClass -> AS Visibility Surface                           // 原生类进入 AS 的三层可见性
├─ [Surface 1] asIScriptEngine type slot                 // AngelScript VM 类型槽位
│  └─ FAngelscriptBinds::ReferenceClass()
│     └─ RegisterObjectType()
├─ [Surface 2] FAngelscriptTypeDatabase index            // 插件内部类型查询索引
│  └─ FAngelscriptType::Register()
│     ├─ TypesByAngelscriptName
│     └─ TypesByClass
└─ [Surface 3] callable / property surface               // 方法、属性、StaticClass 暴露面
   ├─ TFieldIterator<UFunction>() -> BindBlueprintCallable/Event
   ├─ TFieldIterator<FProperty>() -> BindProperties
   └─ BindStaticClass()
```

`BindUClass()` 就是这三层里的前两层入口：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp
// 函数: BindUClass
// 位置: UClass -> Angelscript type 的声明注册入口
// ============================================================================
static void BindUClass(UClass* Class, const FString& TypeName)
{
    // ★ 先在 AngelScript VM 中注册一个 UObject 引用类型
    auto Class_ = FAngelscriptBinds::ReferenceClass(TypeName, Class);

    // ★ 再在插件自己的类型数据库里登记 UClass -> FAngelscriptType 映射
    auto Type = MakeShared<FUObjectType>(Class, TypeName, Class_.GetTypeInfo());
    FAngelscriptType::Register(Type);

    auto* TypeInfo = (asCTypeInfo*)Class_.GetTypeInfo();
    if (TypeInfo != nullptr)
    {
        // ★ 把关联 UClass 塞到 as type 的 user data 里，供后续桥接和反查
        TypeInfo->plainUserData = (SIZE_T)Class;
    }
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp
// 函数: FAngelscriptType::Register
// 位置: 把新类型写入当前引擎的 TypeDatabase
// ============================================================================
void FAngelscriptType::Register(TSharedRef<FAngelscriptType> Type)
{
    auto& Database = GetTypeDatabase();

    FString AngelscriptName = Type->GetAngelscriptTypeName();
    UClass* Class = Type->GetClass(FAngelscriptTypeUsage::DefaultUsage);
    void* Data = Type->GetData();

    Database.RegisteredTypes.Add(Type);
    Database.TypesByAngelscriptName.Add(AngelscriptName, Type);

    if (Class != nullptr)
    {
        // ★ 这是后续 FAngelscriptType::GetByClass(Class) 能成功的关键索引
        Database.TypesByClass.Add(Class, Type);
    }

    if (Data != nullptr)
    {
        Database.TypesByData.Add(Data, Type);
    }
}
```

因此这里“`AngelscriptTest` 的原生 `UClass` 没有注册到 AS 引擎”更精确的含义是：

- 它们没有在主引擎的 `asIScriptEngine` 里拿到对应 object type
- 也没有在当前 `FAngelscriptTypeDatabase` 的 `TypesByClass` 里建立映射
- 所以下游 `Bind_Defaults`、脚本类型解析、`FAngelscriptType::GetByClass()` 乃至调试值提取都看不到它们

## `BindBlueprintCallable` / `BindBlueprintEvent` 又依赖什么

- 类型声明成功并不等于函数面一定完整
- `Bind_Defaults` 在枚举 `UFunction` 后，还要继续走 `BindBlueprintCallable` / `BindBlueprintEvent`
- 对 `BlueprintCallable` 来说，真正可直绑的 native call entry 来自 `ClassFuncMaps`

`ClassFuncMaps` 的单条记录格式是 `UClass* -> FunctionName -> FFuncEntry`：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/FunctionCallers.h
// 类型: FFuncEntry
// 位置: 直绑 BlueprintCallable 函数时使用的函数入口记录
// ============================================================================
struct FFuncEntry
{
    FGenericFuncPtr FuncPtr;                              // 原生函数指针
    ASAutoCaller::FunctionCaller Caller;                  // 参数封送/调用器
    bool bReflectiveFallbackBound = false;                // 是否走过反射回退
};
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h
// 函数: FAngelscriptBinds::AddFunctionEntry
// 位置: 把 UClass + 函数名 对应的直绑入口登记到 ClassFuncMaps
// ============================================================================
static void AddFunctionEntry(UClass* Class, FString Name, FFuncEntry Entry)
{
    auto& ClassFuncMaps = GetClassFuncMaps();
    if (ClassFuncMaps.Contains(Class))
    {
        if (!ClassFuncMaps[Class].Contains(Name))
        {
            ClassFuncMaps[Class].Add(Name, Entry);
        }
    }
    else
    {
        ClassFuncMaps.Add(Class, TMap<FString, FFuncEntry>()).Add(Name, Entry);
    }
}
```

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp
// 函数: BindBlueprintCallable
// 位置: BlueprintCallable 绑定时，先反查 ClassFuncMaps 决定是否能直绑
// ============================================================================
void BindBlueprintCallable(TSharedRef<FAngelscriptType> InType, UFunction* Function, FAngelscriptMethodBind& DBBind, ...)
{
    UClass* OwningClass = CastChecked<UClass>(Function->GetOuter());
    FFuncEntry* Entry = nullptr;

    if (OwningClass != nullptr)
    {
        FString Name = Function->GetFName().ToString();
        auto* Map = FAngelscriptBinds::GetClassFuncMaps().Find(OwningClass);
        if (Map)
        {
            Entry = Map->Find(Name);
        }
    }

    // ★ 找不到入口就无法做 direct bind；只能退出或走 reflective fallback
    if (Entry == nullptr)
        return;

    ...
}
```

这个点和当前问题的关系是：

- `ClassFuncMaps` 决定 callable surface 能不能走直绑
- 但比它更早的前置条件仍然是：**这个 `UClass` 首先得在 `Bind_BlueprintType` 两阶段里被看到**
- 如果类本身都没进 `ClassesToBind`，后面所有 callable/property surface 都无从谈起

## 关键数据格式

- 当前问题横跨三类数据：**模块描述数据**、**运行时 bind 状态**、**cooked/cache 绑定描述**
- Editor 模式主要依赖前两类；cooked 模式才依赖第三类

### 1. `.uplugin` 模块描述数据

- 这是模块是否会在某个阶段被装载的最上游配置
- 当前三个模块都配置为 `LoadingPhase = PostDefault`
- 声明顺序是 `AngelscriptRuntime`、`AngelscriptEditor`、`AngelscriptTest`

```json
"Modules": [
  { "Name": "AngelscriptRuntime", "Type": "Runtime", "LoadingPhase": "PostDefault" },
  { "Name": "AngelscriptEditor",  "Type": "Editor",  "LoadingPhase": "PostDefault" },
  { "Name": "AngelscriptTest",    "Type": "Editor",  "LoadingPhase": "PostDefault" }
]
```

它本身不决定 bind 逻辑，但决定 `AngelscriptTest` 的 `UClass` 是否来得及在首轮 bind pass 前出现在反射系统里。

### 2. `FAngelscriptBindState`：当前引擎的运行期 bind 状态

- 这是 **当前引擎实例** 持有的 bind 运行态
- 不是持久化格式，而是 live state

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h
// 类型: FAngelscriptBindState
// ============================================================================
struct ANGELSCRIPTRUNTIME_API FAngelscriptBindState
{
    TMap<UClass*, TMap<FString, FFuncEntry>> ClassFuncMaps;
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
    FGeneratedFunctionTableTimingSummary GeneratedFunctionTableTimingSummary;
};
```

这里和当前问题直接相关的字段有三个：

- `ClassFuncMaps`：`BindBlueprintCallable()` 查 callable surface 时查它
- `BindModuleNames`：`Initialize_AnyThread()` 会先读取 `BindModules.Cache` 再加载这些模块，以便提前填充 callable 入口
- `SkipBinds` / `SkipBindNames` / `SkipBindClasses`：决定某些函数或类是否在 bind pass 中被显式跳过

### 3. `FAngelscriptBindDatabase`：cooked / cache 绑定描述

- 这是 **持久化** 数据结构，用于非 Editor 场景复原绑定面
- 当前问题发生在 Editor，因此主要路径不是它；但要讲完整 bind 生命周期，必须把它放进图里

```text
FAngelscriptBindDatabase                               // cooked/cache 绑定描述
├─ Structs[] : FAngelscriptStructBind                 // 结构体绑定项
├─ Classes[] : FAngelscriptClassBind                  // 类绑定项
│  ├─ TypeName                                        // AS 类型名
│  ├─ UnrealPath                                      // UE 反射路径
│  ├─ Methods[] : FAngelscriptMethodBind
│  │  ├─ Declaration                                  // AS 声明
│  │  ├─ UnrealPath                                   // 对应 UFunction 名/路径
│  │  ├─ ScriptName                                   // 脚本可见名
│  │  └─ static/global/trivial flags
│  └─ Properties[] : FAngelscriptPropertyBind
│     ├─ Declaration                                  // 属性声明
│     ├─ UnrealPath                                   // 对应 FProperty 名
│     └─ getter/setter/generated flags
├─ BoundEnums[]                                       // 已缓存枚举
├─ BoundDelegateFunctions[]                           // 已缓存 delegate function
└─ HeaderLinks                                        // UField -> header 路径
```

具体结构定义在 `AngelscriptBindDatabase.h`：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h
// 位置: cooked game 下复原 class / method / property bind 的缓存结构
// ============================================================================
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

    UFunction* ResolvedFunction = nullptr;             // 运行时临时态
};

struct FAngelscriptClassBind
{
    FString TypeName;
    FString UnrealPath;
    TArray<FAngelscriptMethodBind> Methods;
    TArray<FAngelscriptPropertyBind> Properties;

    UClass* ResolvedClass = nullptr;                   // 运行时临时态
};
```

### 4. `FAngelscriptTypeDatabase`：真正的“类型已注册”查询面

- 如果要判断某个原生 `UClass` 是否真的已经被当前主引擎看见，最直接的信号就是 `TypesByClass`
- `BindUClass()` 成功后，这里应该能通过 `FAngelscriptType::GetByClass()` 反查回来

```text
FAngelscriptTypeDatabase                               // 当前引擎的类型数据库
├─ RegisteredTypes                                     // 所有已注册 AS 类型
├─ TypesByAngelscriptName                              // AS 类型名 -> 类型对象
├─ TypesByClass                                        // UClass* -> 类型对象
├─ TypesByData                                         // 自定义 data 指针 -> 类型对象
└─ TypeFinders                                         // Property -> Type 的查找器
```

## 生命周期：`UClass` 何时“能被 AS 看见”

- 这个问题的本质不是单个函数 bug，而是一个 **四阶段可见性生命周期**
- 只要把这个生命周期看清，就知道为什么 Test 模块 late load 会漏掉首轮扫描

```text
Native UClass Visibility Lifecycle                      // 原生 UClass 被 AS 看见的生命周期
├─ [1] Module Load                                      // 模块装载
│  ├─ Trigger: UE LoadModule / plugin phase load        // UE 装载模块 DLL
│  ├─ Result: UHT generated reflection is registered    // 该模块的 UClass 才进入反射系统
│  └─ Owner: UE module manager                          // 持有方是模块系统
├─ [2] Declaration Bind Pass                            // AS 声明绑定阶段
│  ├─ Trigger: FAngelscriptEngine::BindScriptTypes()
│  ├─ Input: current TObjectRange<UClass>() snapshot
│  └─ Result: BindUClass + FAngelscriptType::Register
├─ [3] Surface Completion Pass                          // AS 面补全阶段
│  ├─ Trigger: later item in same CallBinds pass
│  ├─ Input: current TObjectRange<UClass>() + reflected UFunction/FProperty
│  └─ Result: callable/property/static-class surfaces are bound
├─ [4] Late Module Arrival                              // Test 模块晚到
│  ├─ Trigger: UE continues loading later PostDefault modules
│  ├─ Result: new UClass appears after step [2]/[3]
│  └─ Gap: no replay of declaration/surface bind
└─ [5] Steady-State Miss                                // 运行稳态缺口
   ├─ Symptom: FAngelscriptType::GetByClass(TestClass) returns null
   ├─ Symptom: AS scripts cannot resolve the native test type
   └─ Until: engine restart with earlier load or explicit replay
```

这里最关键的事实是：**模块里的 `UClass` 不是在编译时“天然永远存在”，而是在该模块真正被装载进进程后，UHT 生成的反射注册代码才会把它们放进全局反射系统。**

因此如果 `AngelscriptTest` 没装载，`TObjectRange<UClass>()` 自然就看不到它的原生类型。

## 为什么说当前更像“时序缺口”，不是“过滤逻辑失效”

- 有明确符合条件的 `UClass`，例如 `AAngelscriptTestActor`
- `AngelscriptTest` 自身依赖 `AngelscriptRuntime`，依赖方向是单向的，Runtime 不会反过来等待 Test
- `AngelscriptTestModule::StartupModule()` 当前不做任何补绑定动作
- 在 Runtime / Editor 里也没有看到针对 `OnModulesChanged`、`OnObjectLoaded`、`OnPackageReloaded` 之类事件去 replay native bind 的路径

看 `AngelscriptTest` 模块启动实现就很直观：

```cpp
// ============================================================================
// 文件: Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTestModule.cpp
// 位置: Test 模块启动逻辑
// ============================================================================
IMPLEMENT_MODULE(FAngelscriptTestModule, AngelscriptTest);

void FAngelscriptTestModule::StartupModule()
{
    // ★ 当前只打日志，不做 “重新扫描本模块 UClass 并补注册到主引擎” 的动作
    UE_LOG(LogAngelscriptTest, Log, TEXT("AngelscriptTest module started."));
}
```

如果当前问题是纯过滤规则失效，那么应该能在：

- `ShouldBindEngineType()`
- `BindBlueprintCallable()`
- `BindBlueprintEvent()`

这些点上找到“本来已被扫描到，但被错误拒绝”的证据。现在更强的证据链反而是：

1. `AngelscriptTest` 里存在满足条件的 `BlueprintType` 原生类  
2. Runtime 主引擎在 Test 模块启动前就执行完了首轮 bind  
3. Test 模块启动后没有 replay  

这三条连在一起时，根因更接近 **late module load visibility gap**。

## 这件事对 Editor 与 Cooked 的影响不同

- 当前问题主要发生在 **Editor / Automation / TestModule** 这一侧
- 因为 Editor 构建走的是 live reflection scan，而不是 `BindDatabase` 回放
- Cooked 路径更多依赖 `Binds.Cache`，问题表面可能不同

这里的关键开关在 `AngelscriptEngine.h`：

```cpp
#define AS_USE_BIND_DB (!WITH_EDITOR)
```

所以：

- **Editor**：边跑边扫当前 `UClass` / `UFunction` / `FProperty`
- **Cooked**：更多依赖事先生成好的 `FAngelscriptBindDatabase`

这也是为什么你现在看到的是“`AngelscriptTest` 模块的 `UClass` 没进 AS 引擎”，而不是“`Binds.Cache` 少了某条记录”。问题发生点更靠前，在 live bind visibility。

## 当前最值得记住的修复方向

- 这篇主要做根因分析，不直接替你选择修复方案；但时序根因已经把可选解法缩得很窄
- 真正可行的修复方向只有三类

1. **提前加载 `AngelscriptTest`**  
   在主 `FAngelscriptEngine` 首次 `BindScriptTypes()` 之前，显式确保 `AngelscriptTest` 已加载  
   代价：把测试模块更早拉进主进程，可能扩大 Editor 常驻模块面

2. **增加 late module replay API**  
   Runtime 暴露一个“对新出现 native `UClass` 做增量 bind”的入口，由 `AngelscriptTestModule::StartupModule()` 主动调用  
   代价：要把 `Bind_BlueprintType` 的两阶段逻辑重构成可重入的增量路径

3. **监听模块后加载并自动增量补扫**  
   由 Runtime / Editor 监听模块装载事件，把新模块的 native `UClass` 补注册到当前主引擎  
   代价：实现最通用，但要仔细处理重复 bind、已存在类型冲突和热重载路径

如果只是修 `AngelscriptTest`，第一种最直接；如果想把“任意 late-loaded native module” 都纳入主引擎可见性，后两种更架构化。

## 小结

- `Bind Blueprint` 的启动时机在 `FAngelscriptRuntimeModule::StartupModule()` 触发的主引擎初始化链里，而不是 `OnPostEngineInit`
- Editor 构建下 `Bind_BlueprintType` 走 live reflection scan：两轮 `TObjectRange<UClass>()` 只扫描当前已存在的原生类
- `AngelscriptTest` 模块晚于 Runtime 主引擎 bind pass 加载，因此其 native `UClass` 会错过首轮类型声明与 surface 绑定
- 当前 `AngelscriptTestModule::StartupModule()` 不做任何 native bind replay，Runtime / Editor 侧也没有看到现成的 late module auto-rebind 机制
- 因此更准确的根因是：**`AngelscriptTest` 中满足条件的 native `UClass` 存在模块加载时序缺口，导致未进入当前主 `FAngelscriptEngine` 的类型/绑定可见面**
