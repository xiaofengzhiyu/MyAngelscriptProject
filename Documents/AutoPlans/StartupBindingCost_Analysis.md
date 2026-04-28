# Angelscript 启动绑定耗时分析（对比 UEAS2）

## 目的

- 解释用户日志里两类关键输出分别代表什么。
- 说明为什么当前插件在启动绑定阶段看起来比 `UEAS2` 更重。
- 区分 **运行时启动成本** 与 **UHT/编译期成本**，避免把两者混在一起。

## 日志现象

用户提供的关键日志片段包括：

```text
Angelscript: delegate bindings took 18.770 ms
Angelscript: [UHT] Registered 256 generated BlueprintCallable entries for module Engine shard 14/16
Angelscript: [UHT] Registered 256 generated BlueprintCallable entries for module Engine shard 13/16
...
Angelscript: [UHT] Registered 214 generated BlueprintCallable entries for module Engine shard 16/16
```

从这段日志可以直接看出，当前启动阶段至少包含两类工作：

1. **delegate 绑定阶段**：输出 `delegate bindings took ... ms`。
2. **UHT 生成函数表注册阶段**：输出多条 `[UHT] Registered ...`。

这两段日志都发生在 **启动期 bind replay**，不是在运行中按需懒加载才触发。

---

## 结论摘要

### 一句话结论

当前插件比 `UEAS2` 看起来更重，最稳妥的原因不是“日志更多”，也不是“UHT 代码生成本身拖慢启动”，而是：

> **当前实现除了回放原有手写 bind 之外，还在启动期额外串行回放了一层 UHT 生成函数表注册；同时 delegate 路径还会对 `UDelegateFunction` 做两次全量枚举。**

### 更具体地说

- `UEAS2` 参考实现里，当前证据只看到 **手写 bind 注册 + 排序后执行**，没有看到同等规模的启动期 UHT 函数表回放层。
- 当前插件里，`BindScriptTypes()` 会进入 `FAngelscriptBinds::CallBinds(...)`，而 `CallBinds()` 是 **按排序结果逐个串行执行** bind。
- delegate 路径在 `Bind_Delegate_Declarations` 和 `Bind_Delegates` 中都各自遍历一次 `TObjectRange<UDelegateFunction>()`。
- 当前工作区这轮生成产物显示：启动期会回放 **32 个 shard / 6043 条 generated entry**，其中光 `Engine` 模块就有 **4054 条 / 16 个 shard**。

因此，当前日志更重，核心是 **启动期需要重放的注册工作本身更多**，而且这些工作是 **串行叠加** 的。

---

## 日志各自对应哪段代码

### 1. `delegate bindings took ... ms` 来自哪里

`FAngelscriptScopeTimer` 定义在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`，说明它是一个专门用于打印阶段耗时的 scope timer。

delegate 的两段绑定代码位于 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.cpp`：

- `Bind_Delegate_Declarations`：`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.cpp:1331`
- `Bind_Delegates`：`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.cpp:1410`

其中：

- `Bind_Delegate_Declarations` 使用 `FAngelscriptScopeTimer Timer(TEXT("delegate declarations"));`
- `Bind_Delegates` 使用 `FAngelscriptScopeTimer Timer(TEXT("delegate bindings"));`

所以日志里的 `delegate bindings took 18.770 ms`，对应的是 `Bind_Delegates` 这个 Late bind 阶段本身的执行耗时，而不是泛指整个启动流程。

### 2. `[UHT] Registered ...` 来自哪里

UHT 生成代码的模板在 `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:282` 开始的 `BuildShard(...)` 中。

生成出来的每个 shard 都会写出类似代码：

```cpp
AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_AS_FunctionTable_<Module>_<Shard>((int32)FAngelscriptBinds::EOrder::Late + 50, []()
{
    ...
    UE_LOG(Angelscript, Log, TEXT("[UHT] Registered %d generated BlueprintCallable entries for module %s shard %d/%d"), ...);
});
```

当前工作区实际产物 `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Engine_000.cpp:221` 到 `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Engine_000.cpp:479` 也能直接看到这条注册路径和末尾日志。

这说明 `[UHT] Registered ...` 不是“UHT 正在运行”的日志，而是：

> **UHT 之前已经生成好了这些 `AS_FunctionTable_*.cpp` 文件；启动时只是把这些生成好的注册函数作为 bind 回放执行。**

---

## 当前插件的启动绑定链路

### 1. 总入口

`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1915`：

```cpp
void FAngelscriptEngine::BindScriptTypes()
{
    FAngelscriptBinds::CallBinds(CollectDisabledBindNames());
}
```

也就是说，启动期绑定核心动作就是把所有已注册 bind 按顺序 replay 一遍。

### 2. replay 方式是串行的

`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:195`：

```cpp
void FAngelscriptBinds::CallBinds(const TSet<FName>& DisabledBindNames)
{
    for (const FBindFunction& Bind : GetSortedBindArray())
    {
        if (DisabledBindNames.Contains(Bind.BindName))
            continue;

        Bind.Function();
    }
}
```

这段实现的含义很直接：

- 先取排序后的 bind 列表。
- 然后逐个执行。
- 没有迹象表明这里做了并行化或按模块分发到多个线程。

因此，额外的注册层会 **直接累加到启动路径** 上。

### 3. delegate 路径会做两次全量扫描

`Bind_Delegate_Declarations` 和 `Bind_Delegates` 里都包含：

```cpp
for (UDelegateFunction* Function : TObjectRange<UDelegateFunction>())
```

位置分别在：

- `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.cpp:1343`
- `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.cpp:1414`

这至少意味着：

- 当前 delegate 初始化不是只扫一遍引擎里的 `UDelegateFunction`。
- 它会在 **声明阶段** 扫一遍。
- 然后在 **操作绑定阶段** 再扫一遍。

现有证据能稳妥支持“**两次全量枚举**”，但不能仅凭这段材料断言“两次成本完全相同”。更稳妥的说法是：**delegate 相关工作至少存在双遍历。**

### 4. 当前插件还会额外 replay UHT 生成函数表

当前生成产物 `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Summary.json:2` 到 `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Summary.json:8` 显示：

- `totalGeneratedEntries = 6043`
- `totalDirectBindEntries = 3394`
- `totalStubEntries = 2649`
- `totalShardCount = 32`
- `moduleCount = 14`

同一文件中还可以看到：

- `Engine` 模块：`4054` 条 entry，`16` 个 shard
- `UMG` 模块：`753` 条 entry，`3` 个 shard
- `AngelscriptRuntime` 模块：`408` 条 entry，`2` 个 shard

这批 shard 在启动期不是空壳日志，而是会实际执行一串 `AddFunctionEntry(...)`。例如：

- `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Engine_000.cpp:223`
- `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Engine_000.cpp:479`

其中间部分是一整段：

```cpp
FAngelscriptBinds::AddFunctionEntry(AActor::StaticClass(), "ActorHasTag", { ... });
FAngelscriptBinds::AddFunctionEntry(AActor::StaticClass(), "AddTickPrerequisiteActor", { ... });
...
UE_LOG(Angelscript, Log, TEXT("[UHT] Registered %d generated BlueprintCallable entries for module %s shard %d/%d"), 256, TEXT("Engine"), 1, 16);
```

也就是说，启动期真实做的工作是：

1. 进入每个 generated shard 的 bind lambda。
2. 反复调用 `AddFunctionEntry(...)`。
3. 最后打印一条 `[UHT] Registered ...`。

其中真正重的显然是前面的注册动作，不是最后那条 `UE_LOG` 本身。

### 5. `AddFunctionEntry()` 的运行时含义

`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:497` 定义了：

```cpp
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

这说明 generated shard 启动期并不是只做计数，而是在构建/补充 `ClassFuncMaps` 这张运行时调用映射表。

因此，当前启动成本上升的本质是：

> **当前插件把更大的 BlueprintCallable 暴露面，转化成了启动期更多的映射表写入工作。**

---

## UEAS2 是怎么做的

### 1. 启动入口更薄

`UEAS2` 的 loader 入口 `J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptLoader\Private\AngelscriptLoaderModule.cpp:6` 很简单：

```cpp
void FAngelscriptLoaderModule::StartupModule()
{
    FAngelscriptCodeModule::InitializeAngelscript();
}
```

也就是说，`Loader` 只做初始化转发，本身不承担额外注册层。

### 2. bind 容器也更简单

`J:\UnrealEngine\UEAS2\Engine\Plugins\Angelscript\Source\AngelscriptCode\Private\AngelscriptBinds.cpp:17` 开始定义的是一个简单的全局数组：

```cpp
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

这个执行模型仍然是串行 replay，但当前证据没有显示它还额外叠了一层与当前插件等规模的 generated function-table 启动期注册。

### 3. 当前证据下，UEAS2 没看到这些东西

在本轮取证范围内，没有在 `UEAS2` 插件源码中发现：

- `[UHT] Registered ...` 这类 generated shard 注册日志。
- `AS_FunctionTable_*.cpp` 这类运行时回放产物。
- 与当前仓库等价的“6043 条 generated entry / 32 shards”这一级别的启动期 UHT 函数表 replay。

因此更稳妥的对比结论是：

> **基于当前可见证据，UEAS2 的启动期 bind 模型更接近“手写 bind 排序后直接回放”；当前插件则是在这个基础上，额外增加了一层启动期 generated function-table 注册。**

这里要特别注意：

- 本文是基于当前手头 `UEAS2` 代码证据得出的结论。
- 它稳妥支持“当前可见的 UEAS2 风格参考没有同类 UHT 启动期注册层”。
- 但它不应该写成“Hazelight 全量实现绝对没有任何等价机制”，除非再补更多源码或对应启动日志证据。

---

## 为什么当前看起来比 UEAS2 慢

把前面的证据合并后，可以得到一个比较稳妥的因果链：

### 原因 1：当前启动期回放的工作量更多

`UEAS2` 当前证据下主要是：

- 手写 bind 注册
- 排序
- replay

当前插件则是：

- 手写 bind 注册
- delegate 声明阶段 replay
- delegate 操作阶段 replay
- generated function-table shard replay
- `ClassFuncMaps` 填充
- 部分条目的 reflective fallback 相关后续路径准备

因此它不是“同样工作做得更慢”，而是“**启动期多做了一层能力覆盖换来的额外注册工作**”。

### 原因 2：bind replay 是串行累加的

`CallBinds()` 当前没有显示出并行执行机制。

这意味着：

- 多一个 bind，就多一份启动期 replay 成本。
- 多一层 generated shard，就多一层串行叠加。
- 日志里一长串 `[UHT] Registered ...` 本质上反映的是这些 shard 被一个接一个执行完。

### 原因 3：delegate 路径本身就不是零成本

delegate 相关路径最少会进行两次 `TObjectRange<UDelegateFunction>()` 枚举：

- 一次声明
- 一次操作绑定

如果工程里的 `UDelegateFunction` 数量很多，这里本来就会形成一个可见的启动阶段。

### 原因 4：当前插件把更大的 BlueprintCallable 覆盖面前置到启动期

`6043` 条 generated entry 不是小修饰，而是实打实把更多 BlueprintCallable 映射在启动时预先写进表里。

这背后反映的是一个架构取舍：

- `UEAS2` 更接近“手写 bind 主导”。
- 当前插件更接近“手写 bind + 自动生成函数表 + fallback”组合路线。

这种路线的收益是覆盖面更高、自动化更强；代价就是启动期注册阶段天然更重。

---

## 哪些是启动期成本，哪些主要是编译期成本

这是本次分析里最容易混淆的一点。

### 1. 运行时启动期成本

当前有直接证据支持的启动期成本包括：

- `Bind_Delegate_Declarations` 的执行
- `Bind_Delegates` 的执行
- 所有 bind 的串行 replay
- 所有 generated shard 的串行 replay
- shard 内部大量 `AddFunctionEntry(...)` 对 `ClassFuncMaps` 的写入

这些都与用户看到的启动日志直接相关。

### 2. 更偏编译/UHT/构建期的成本

当前生成产物可以看到，`Engine` 的两个 shard：

- `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Engine_000.cpp`
- `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Engine_001.cpp`

都包含 **218 个相同的 include**。

这说明当前 shard 策略是：

- **把条目切片了**
- 但**没有把 include 依赖面一起切小**

这个事实更直接解释的是：

- 编译依赖面偏大
- 增量构建时很多 shard 会一起重编
- UHT 产物的编译负担较高

但它不是解释“当前这条启动日志为什么更久”的最强证据。

更稳妥的写法应该是：

> **重复 include 列表主要说明当前 generated shard 的构建/编译成本偏高；而启动日志变重的直接原因，是这些 shard 在启动时仍要逐个执行注册体。**

---

## 对比表

| 对比项 | 当前插件 | UEAS2（基于本轮可见证据） | 结论 |
| --- | --- | --- | --- |
| 启动入口 | `BindScriptTypes() -> CallBinds(...)` | `Loader -> InitializeAngelscript()` 转发 | 当前插件路径更厚 |
| bind replay 模式 | 串行 | 串行 | 执行模型类似 |
| delegate 全量枚举 | 有，两次 `TObjectRange<UDelegateFunction>()` | 本轮未见同等证据 | 当前插件可见成本更高 |
| UHT generated function-table 启动期注册 | 有，`32` 个 shard / `6043` 条 entry | 本轮未见同类证据 | 当前插件额外增加了一层启动期 replay |
| `ClassFuncMaps` 启动期填充 | 有，`AddFunctionEntry(...)` 大量执行 | 本轮未见同类证据 | 当前插件更重 |
| shard include 重复 | 明显存在 | 本轮未取证 | 当前更偏编译期成本问题 |

---

## 最终判断

如果只回答“为什么当前这段日志比 UEAS2 看起来久”，最稳妥的答案是：

> **因为当前插件在 UEAS2 风格的手写 bind replay 之外，又把大规模 BlueprintCallable 自动导出结果前置到了启动期回放。这个阶段不仅要做 delegate 双遍历，还要串行执行 32 个 generated shard、把 6043 条 function entry 尝试写入运行时映射表，所以日志上自然会比只靠手写 bind 的 UEAS2 更重。**

同时需要补一句限定：

> **重复 include、shard 依赖面过大等问题确实存在，但它们更直接解释的是生成产物的编译/增量构建成本，不应当作为本次启动日志偏重的主要 runtime 根因。**

---

## 可继续追的量化点

如果后续要继续精确量化“到底慢在 delegate，还是慢在 generated shard”，最合适的下一步不是先改实现，而是先补更细粒度的测量：

1. 对 `CallBinds()` 做按 bind name / bind order 的分段计时。
2. 单独统计 `Bind_Delegate_Declarations` 和 `Bind_Delegates` 各自的扫描数量与耗时。
3. 对 generated shard 按模块汇总启动期注册耗时，例如 `Engine`、`UMG`、`GameplayAbilities` 分别占多少。
4. 单独测 `AddFunctionEntry()` 造成的 `ClassFuncMaps` 扩容/插入成本。

这样就能把当前“架构层面原因成立”的判断，再推进到“具体哪几个模块最值回票价”的优化决策层面。
