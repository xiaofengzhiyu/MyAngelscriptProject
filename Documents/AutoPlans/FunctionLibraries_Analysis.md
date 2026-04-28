# FunctionLibraries 分析

---

## 分析 (2026-04-08 02:33)

### 发现 1：`UAssetManagerMixinLibrary` 有 3 个声明存在但不会进入脚本绑定的查询 API

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/UAssetManagerMixinLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp` |
| 行号 | 53-74；1311-1314；1403-1406 |
| 描述 | `GetPrimaryAssetTypeInfo`、`GetPrimaryAssetTypeInfoList`、`GetPrimaryAssetRules` 在头文件里仍保留 `//UFUNCTION(ScriptCallable)` 注释，但实际声明只有裸 `UFUNCTION()`，而同一文件前半段相邻 API 使用的是 `UFUNCTION(BlueprintCallable)`。绑定器在 `Bind_BlueprintType.cpp` 中只对 `FUNC_BlueprintCallable | FUNC_BlueprintPure` 或带 `ScriptCallable` 元数据的函数调用 `BindBlueprintCallable`，因此这 3 个函数不会被自动收集进脚本可见 API。 |
| 根因 | 该库经历了从 `ScriptCallable` 向 `BlueprintCallable` 的迁移，但迁移只覆盖了前半段函数，后三个资产元数据相关入口停留在半完成状态。 |
| 影响 | `UAssetManager` 的脚本暴露面在同一库内部出现断层：脚本能拿到 `PrimaryAssetData` 和 `PrimaryAssetIdList`，却拿不到同一组查询链路里的 `PrimaryAssetTypeInfo` / `PrimaryAssetRules`。`rg -n "GetPrimaryAssetTypeInfo|GetPrimaryAssetRules" Plugins/Angelscript/Source/AngelscriptTest` 无命中，当前也没有测试会在绑定缺失时报警。 |

### 发现 2：`GetEngineDefined*Mappings` 的筛选参数被声明出来但实现完全忽略

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/InputComponentScriptMixinLibrary.h` |
| 行号 | 197-220 |
| 描述 | `GetKeysForAction` / `GetKeysForAxis` 正确把 `ActionName`、`AxisName` 传给 `UPlayerInput`，但紧随其后的 `GetEngineDefinedActionMappings(UPlayerInput* PlayerInput, const FName ActionName)` 和 `GetEngineDefinedAxisMappings(UPlayerInput* PlayerInput, const FName AxisName)` 直接返回 `PlayerInput->GetEngineDefinedActionMappings()` / `PlayerInput->GetEngineDefinedAxisMappings()`，两个筛选参数在实现里完全未使用。 |
| 根因 | 该组 API 显然按“按名字取映射”的接口形态设计了签名，但转发到底层 UE 调用时复用了全量 getter，没有补脚本侧过滤逻辑。 |
| 影响 | 脚本调用者会以为自己拿到的是某个 `ActionName` / `AxisName` 的 engine-defined 映射，实际拿到的是整个全局数组，行为与函数名不一致，容易把输入配置逻辑写错。`rg -n "GetEngineDefinedActionMappings|GetEngineDefinedAxisMappings" Plugins/Angelscript/Source/AngelscriptTest` 无命中，当前没有回归用例覆盖这两个入口。 |

### 发现 3：`AddSmartAutoCurveKey` 与 `AddAutoCurveKey` 是同一份实现

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/RuntimeFloatCurveMixinLibrary.h`；`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` |
| 行号 | 190-205；781-839 |
| 描述 | `AddAutoCurveKey` 和 `AddSmartAutoCurveKey` 都执行同一序列：`AddKey`、`SetKeyInterpMode(..., RCIM_Cubic, false)`、`SetKeyTangentMode(..., RCTM_Auto, false)`，两者实现没有任何一行不同。也就是说，脚本侧虽然暴露了两个名字不同的 helper，但第二个名字没有带来任何不同语义。现有测试只在 `AngelscriptBindConfigTests.cpp` 里验证 `URuntimeFloatCurveMixinLibrary::GetNumKeys` 与 `GetTimeRange` 能恢复 direct bind，没有覆盖任何 key-creation helper。 |
| 根因 | 新增 helper 时直接复制了 `AddAutoCurveKey`，但没有实现与函数名对应的差异化行为，也没有增加针对曲线 key 类型语义的测试。 |
| 影响 | 脚本作者如果按函数名选用 “SmartAuto” 版本，会得到与 “Auto” 完全相同的曲线结果；一旦实际曲线形状不符合预期，问题会被误判为引擎曲线算法差异，而不是 wrapper 自身的语义塌缩。 |

### 发现 4：`Math::WrapIndex` 的 `uint32` overload 在源码中定义，但不会暴露给脚本

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptMathLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp` |
| 行号 | 294-321；1311-1314；1403-1406 |
| 描述 | `WrapIndex(int32 Value, int32 Min, int32 Max)` 使用的是 `UFUNCTION(BlueprintCallable)`，而紧接着的 `WrapIndexUInt(uint32 Value, uint32 Min, uint32 Max)` 只有 `UFUNCTION(Meta = (ScriptName = "WrapIndex"))`，保留的 `//UFUNCTION(ScriptCallable, ...)` 仅存在于注释里。结合绑定器只扫描 `BlueprintCallable` / `BlueprintPure` / `ScriptCallable` 的逻辑，`uint32` overload 不会进入脚本绑定，而同名的 `int32` overload 会进入。 |
| 根因 | 同名 overload 在迁移到当前暴露策略时只补齐了有符号版本，`uint32` 版本遗漏了真正可被绑定器识别的标记。 |
| 影响 | `Math::WrapIndex` 在脚本侧失去 signed/unsigned 对称性，调用方只能手动转成 `int32` 或自行实现 `uint32` 版本。`rg -n "WrapIndexUInt" Plugins/Angelscript/Source/AngelscriptTest` 无命中，当前没有测试验证该 overload 的可见性。 |

### 发现 5：输入辅助库把外部传入的 `UObject*` 统一当成必定非空，缺少任何防御

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/InputComponentScriptMixinLibrary.h` |
| 行号 | 23-29；36-43；50-56；63-73；80-90；98-108；127-148；160-248 |
| 描述 | `BindAction`、`BindKey`、`BindChord`、`BindAxis`、`BindAxisKey`、`BindVectorAxis` 直接解引用 `Component`；`PushInputComponent` / `PopInputComponent` / `GetPlayerInput` 直接解引用 `PlayerController`；`AddActionMapping` 到 `GetMouseSensitivityY` 整段 API 都直接解引用 `PlayerInput`。文件内没有任何 `nullptr` 检查、`ensure` 或失败返回路径。 |
| 根因 | 这些 wrapper 基本都是“一行转发 UE 原生调用”的写法，但没有把脚本环境下常见的空句柄输入视为需要防御的边界条件。 |
| 影响 | 只要脚本把空的 `UInputComponent`、`APlayerController` 或 `UPlayerInput` 传进这些 helper，就会在函数库内部直接崩溃，而不是得到可诊断的失败结果。`rg -n "InputComponentScriptMixinLibrary|GetEngineDefinedActionMappings|GetEngineDefinedAxisMappings" Plugins/Angelscript/Source/AngelscriptTest` 无命中，当前没有针对这些空指针路径的回归测试。 |

---

## 分析 (2026-04-08 02:42)

### 发现 6：`UAngelscriptActorLibrary` 的整组 transform/attachment helper 仍停留在裸 `UFUNCTION()`，不会进入 `AActor` 脚本绑定

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptActorLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` |
| 行号 | 12-68，80-223；1311-1314；1403-1406；512-551 |
| 描述 | `UAngelscriptActorLibrary` 里从 `SetActorRelativeLocation`、`GetActorRelativeLocation`、`SetActorRelativeRotation` 一直到 `AttachToComponent`、`AttachToActor`、`RerunConstructionScripts` 基本都还是 `UFUNCTION()` 裸声明，只有 `GetActorLocation` 和 `GetActorRotation` 明确标了 `BlueprintCallable`。`SetActorLocationAdvanced` 甚至只有 `UFUNCTION(meta = (ScriptName = "SetActorLocation", NotAngelscriptProperty))`，同样没有任何可被绑定器识别的 callable flag。绑定器在 `Bind_BlueprintType.cpp` 里只会对 `FUNC_BlueprintCallable | FUNC_BlueprintPure` 或带 `ScriptCallable` 元数据的函数调用 `BindBlueprintCallable`，因此这批 helper 不会被挂到 `AActor`。现有 `GeneratedBlueprintCallableEntriesPopulateClassMaps` 测试只校验 `AActor::K2_DestroyActor` 等原生条目进入 `ClassFuncMaps`，没有覆盖 `UAngelscriptActorLibrary` 自己声明的 mixin 方法。 |
| 根因 | `AActor` mixin 库只完成了极少数函数从旧 `ScriptCallable` 到 `BlueprintCallable` 的迁移，大部分 wrapper 仍保留注释中的旧标记，实际声明却丢失了绑定所需 flag。 |
| 影响 | 脚本侧会得到一个严重不完整的 `AActor` helper 面：能读 `GetActorLocation` / `GetActorRotation`，却拿不到同库声明的相对变换、附着和 editor helper。由于问题覆盖的是整组高频 API，而不是单个遗漏函数，调用方只能退回引擎原生 `K2_*` 接口或自行重复封装。 |

### 发现 7：`AngularDistance` 系列 helper 对零向量和数值漂移没有任何保护，会直接返回 `NaN`

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptMathLibrary.h` |
| 行号 | 380-393；471-484 |
| 描述 | `UAngelscriptFVectorMixinLibrary::AngularDistance` 和 `UAngelscriptFVector3fMixinLibrary::AngularDistance` 都直接计算 `Acos(Dot / Sqrt(|A|^2 * |B|^2))`，没有在分母为 0 时短路；只要任一输入是零向量，就会做 0 除法并把 `NaN` 送进 `FMath::Acos`。对应的 `AngularDistanceForNormals` 也直接对 `DotProduct` 调 `Acos`，没有把值 clamp 到 `[-1, 1]`，因此单位向量在浮点误差下略微越界时同样会产生 `NaN`。这四个 helper 的注释都没有声明“零向量非法”或“调用方必须先归一化并自行 clamp”。 |
| 根因 | 这些函数把数学定义直接翻译成了一行表达式，但没有补上脚本运行时比 C++ 调用点更常见的边界条件防御。 |
| 影响 | 一旦脚本把零向量、退化法线或轻微失真的“近似单位向量”传进这些 helper，返回值会变成 `NaN`，后续再参与插值、比较或序列化时会形成难定位的连锁异常。`rg -n "AngularDistance|AngularDistanceForNormals" Plugins/Angelscript/Source/AngelscriptTest` 无命中，当前没有任何回归测试覆盖这些边界。 |

### 发现 8：`GameplayTag` / `GameplayTagQuery` helper 仍写在 FunctionLibrary 里，但对应实例方法已经没有真正绑定通路

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/GameplayTagMixinLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/GameplayTagQueryMixinLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FGameplayTag.cpp` |
| 行号 | 9-80；9-38；26-42；136-179 |
| 描述 | `GameplayTagMixinLibrary.h` 和 `GameplayTagQueryMixinLibrary.h` 的类注释仍明确写着“bind functions on `FGameplayTag` / `FGameplayTagQuery` that are not BlueprintCallable by default”，但两者的 `ScriptMixin` 元数据都已被注释掉，只剩 `UCLASS(Meta = ())`。与此同时，手写绑定 `Bind_FGameplayTag.cpp` 只给 `FGameplayTag` 暴露了 `opEquals`、`IsValid`、`GetTagName`，给 `FGameplayTagQuery` 暴露了工厂函数、`IsEmpty` 和 `opEquals`；对 `MatchesTag`、`MatchesTagExact`、`MatchesAny`、`MatchesTagDepth`、`RequestDirectParent`、`GetGameplayTagParents`、`Matches`、`GetDescription` 没有任何对应 `Method(...)` 绑定。也就是说，这批 helper 虽然还写在 FunctionLibrary 头文件里，但已经不会按“实例方法”进入 `FGameplayTag` / `FGameplayTagQuery` API 面。 |
| 根因 | `GameplayTag*` 功能在从旧 `ScriptCallable + ScriptMixin` 模式迁到手写 bind 时，只补了最基础的一小部分方法，FunctionLibrary 里的其余 helper 没有同步迁移，也没有恢复 `ScriptMixin` 元数据。 |
| 影响 | 脚本侧的 `GameplayTag` / `GameplayTagQuery` 能力出现隐性断层：基础构造和 `IsEmpty` 仍可用，但常见的匹配、父标签查询和描述读取 helper 无法以实例方法形式调用。`rg -n "MatchesTag|MatchesAny|MatchesTagDepth|RequestDirectParent|GetGameplayTagParents|GetDescription|GameplayTagMixinLibrary|GameplayTagQueryMixinLibrary" Plugins/Angelscript/Source/AngelscriptTest` 无命中，现有测试也不会在这些 helper 失联时报警。 |

### 发现 9：`UAngelscriptWorldLibrary` 失去 `ScriptMixin` 后没有任何手写绑定兜底，`GetStreamingLevels` 不再扩展到 `UWorld`

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptWorldLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FunctionLibraryMixins.cpp` |
| 行号 | 6-19；7-32 |
| 描述 | `UAngelscriptWorldLibrary` 头文件里原本的 `//UCLASS(Meta = (ScriptMixin = "UWorld"))` 被注释掉，实际类只有 `UCLASS(Meta = ())`，因此 `GetStreamingLevels(const UWorld* World)` 不会被 `Helper_FunctionSignature` 当成 `UWorld` 实例方法处理。更关键的是，手写兜底文件 `Bind_FunctionLibraryMixins.cpp` 只补了 `ULevelStreaming::GetShouldBeVisibleInEditor()` 和 `FRuntimeCurveLinearColor::AddDefaultKey()`，完全没有任何 `UWorld` 相关补绑。 |
| 根因 | 该文件从旧 mixin 元数据迁出后，没有像其它特殊库那样同步补一份显式 `ExistingClass("UWorld").Method(...)` 绑定。 |
| 影响 | 这个文件唯一声明的高频 helper `GetStreamingLevels()` 仍然存在于源码里，但已经不能再以 `UWorld` 扩展方法的形式出现，脚本 API 与文件命名和注释预期脱节。`rg -n "GetStreamingLevels|UAngelscriptWorldLibrary" Plugins/Angelscript/Source/AngelscriptTest` 无命中，当前也没有编译级测试覆盖该入口。 |

### 发现 10：`GameplayTagContainer` 既有手写实例绑定又保留一整份失效的 FunctionLibrary wrapper，形成分裂维护

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/GameplayTagContainerMixinLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FGameplayTag.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGameplayTagBindingsTests.cpp` |
| 行号 | 8-157；149-172；111-155 |
| 描述 | `UGameplayTagContainerMixinLibrary` 仍保留 `AddTag`、`RemoveTag`、`HasTag*`、`HasAny*`、`HasAll*`、`Num`、`IsValid`、`IsEmpty`、`First`、`MatchesQuery` 等一整套 wrapper，但类级 `ScriptMixin` 元数据已经被注释掉。与此同时，`Bind_FGameplayTag.cpp` 又手写把同名核心方法直接绑到了 `FGameplayTagContainer`，而现有兼容性测试也正是通过 `Tags.AddTag(...)`、`Tags.HasTag(...)`、`Tags.HasAny(...)`、`Combined.RemoveTag(...)` 这条实例方法路径验证通过。这意味着 FunctionLibrary 里的同名实现已经不再承担主绑定职责，只剩下一份与手写 bind 并行存在的重复代码。 |
| 根因 | `GameplayTagContainer` 的绑定方案从 FunctionLibrary 迁向手写 bind 后，没有同步删除或收敛旧 wrapper；再叠加 `ScriptMixin` 被注释，导致同一功能域同时存在“真正生效的绑定实现”和“仍在源码中持续演化的备用实现”。 |
| 影响 | 维护者很难一眼判断某个 `GameplayTagContainer` helper 应该改哪一处，后续一旦只修改手写 bind 或只修改 library wrapper，就会继续扩大漂移面。更糟的是，`AddTagFast`、`AddLeafTag`、`RemoveTags`、`GetGameplayTagParents`、`Filter*`、`Last` 这类只存在于 FunctionLibrary 的高级 helper 与核心方法处在两套不同机制里，API 形态已经天然不一致。 |

---

## 分析 (2026-04-08 03:23)

### 发现 11：`UAngelscriptComponentLibrary` 回退成仅覆盖 `USceneComponent`，UEAS2 中的 `UPrimitiveComponent` overlap helper 已整体丢失

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptComponentLibrary.h`；`../UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/FunctionLibraries/AngelscriptComponentLibrary.h` |
| 行号 | 7-8；15-16；277-291 |
| 描述 | 当前 `UAngelscriptComponentLibrary` 的类级元数据只剩 `ScriptMixin = "USceneComponent"`，而 UEAS2 参考实现仍是 `ScriptMixin = "USceneComponent UPrimitiveComponent"`，并额外暴露了 `GetOverlappingActorsOfClass(const UPrimitiveComponent* Component, TSubclassOf<AActor> ActorClass)`。在当前仓库执行 `rg -n "GetOverlappingActors\\(" Plugins/Angelscript/Source/AngelscriptRuntime/Binds Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries` 无命中，说明这条高频 helper 既没有留在 FunctionLibrary，也没有迁到手写 bind。 |
| 根因 | 组件库在从旧 `ScriptCallable` 迁到当前 `BlueprintCallable` 方案时，被裁成了 scene-component 子集，但没有把 `UPrimitiveComponent` 侧的 class-filter overlap 查询一起迁移。 |
| 影响 | 脚本侧失去了 UEAS2 已提供过的 `UPrimitiveComponent::GetOverlappingActorsOfClass` 便利入口，只能自己先拿全量重叠 actor 再过滤，或自行重写 wrapper。`rg -n "GetOverlappingActorsOfClass|GetOverlappingActors\\(" Plugins/Angelscript/Source/AngelscriptTest` 同样无命中，当前没有测试会在该 utility 缺失时报警。 |

### 发现 12：`GetChildrenComponentsByClass` 的数组返回版 helper 被删掉，但旧 out-param 绑定也不再标记为 deprecated

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_USceneComponent.cpp`；`../UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_USceneComponent.cpp`；`../UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/FunctionLibraries/AngelscriptComponentLibrary.h` |
| 行号 | 48-109；111-112；250-275 |
| 描述 | 当前仓库只保留了 `USceneComponent_.Method("void GetChildrenComponentsByClass(..., ?& OutChildren)")` 这一条旧式 out-param 绑定；同一文件已经没有 UEAS2 中那版 `TArray<USceneComponent*> GetChildrenComponentsByClass(...)` 的 FunctionLibrary helper。更糟的是，UEAS2 会在绑定后调用 `DeprecatePreviousBind("Use GetChildrenComponentsByClass that returns an array instead")`，而当前 `Bind_USceneComponent.cpp` 112 行之后直接进入 `GetComponentTransform()` 绑定，不再给旧签名任何 deprecated 提示。 |
| 根因 | 迁移过程中删除了返回数组的高层 helper，却没有同步调整旧绑定的演进策略，导致 API 面回退到更笨重的签名。 |
| 影响 | 脚本调用者重新被迫走 `?& OutChildren` 风格接口，失去 UEAS2 已提供过的值返回便利形式；同时旧签名不再带迁移提示，调用方很难感知自己正停留在退化接口上。`rg -n "GetChildrenComponentsByClass" Plugins/Angelscript/Source/AngelscriptTest` 无命中，当前也没有覆盖这个 API 形态回退。 |

### 发现 13：`UWidget::GetRenderTransform` 的 mixin 绑定被切断，源码里只剩一份失效的 wrapper

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/WidgetBlueprintStatics.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UUserWidget.cpp`；`../UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/FunctionLibraries/WidgetBlueprintStatics.h` |
| 行号 | 28-40；276-314；286-293；27-38 |
| 描述 | 当前 `UAngelscriptWidgetMixinLibrary` 把原本的 `ScriptMixin = "UWidget"` 注释掉，只剩 `UCLASS(Meta = ())`。而 `Helper_FunctionSignature.h` 明确只有在 outer class 带 `ScriptMixin` 且首参数类型匹配时，才会把静态 `UFUNCTION` 改写成实例方法；否则会退回静态类函数。当前仓库对 `GetRenderTransform` 没有任何手写 `UWidget` 兜底绑定，`Bind_UUserWidget.cpp` 286-293 只额外补了 `WidgetBlueprint::CreateWidget`。与之相比，UEAS2 版本仍把 `GetRenderTransform(const UWidget* Widget)` 声明为 `UWidget` mixin。 |
| 根因 | UI helper 从旧 `ScriptCallable + ScriptMixin` 迁向 `BlueprintCallable` 时，只保留了函数体，没有保留把它挂到 `UWidget` 实例上的元数据，也没有补手写绑定。 |
| 影响 | 脚本 API 面不再包含 `UWidget.GetRenderTransform()` 这种实例扩展，文件里保留的 wrapper 只会挂在 `UAngelscriptWidgetMixinLibrary` 自己名下，和文件命名、UEAS2 参考实现都不一致。`rg -n "GetRenderTransform|UAngelscriptWidgetMixinLibrary" Plugins/Angelscript/Source/AngelscriptTest` 无命中，当前测试也不会发现这个实例方法已经失联。 |

### 发现 14：`UAngelscriptHitResultLibrary` 同时丢失 `ScriptMixin`、`SetPhysMaterial` 与可控 `Reset` overload，`FHitResult` helper 面明显缩水

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptHitResultLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FHitResult.cpp`；`../UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/FunctionLibraries/AngelscriptHitResultLibrary.h` |
| 行号 | 8-10；30-35；51-56；9-41；8-68 |
| 描述 | 当前 `UAngelscriptHitResultLibrary` 已不再声明 `ScriptMixin = "FHitResult"`，因此文件里的 helper 不会再以 `FHitResult` 实例方法形式出现。与此同时，当前版本只剩 `Reset(FHitResult& HitResult)` 这一种无参封装，UEAS2 中原有的 `Reset(FHitResult&, float InTime = 1.f, bool bPreserveTraceData = true)` 被缩成默认行为；`SetPhysMaterial(FHitResult&, UPhysicalMaterial*)` 也从头文件里完全消失，只留下只读的 `GetPhysMaterial()`。手写绑定 `Bind_FHitResult.cpp` 9-41 只补了构造函数和字段 property，没有对这些 helper 做任何替代绑定。 |
| 根因 | `FHitResult` 在迁移到当前绑定策略时只保留了少量最表层 wrapper，但没有维持 UEAS2 已有的实例扩展面，也没有为删掉的 setter / overload 准备新的手写绑定。 |
| 影响 | 脚本侧无法再像 UEAS2 那样直接给 `FHitResult` 赋 `PhysMaterial`，也无法显式控制 `Reset()` 是否保留 trace 数据；同时 surviving helper 还从实例方法退化成静态类函数。`rg -n "SetPhysMaterial|GetPhysMaterial|UAngelscriptHitResultLibrary" Plugins/Angelscript/Source/AngelscriptTest` 无命中，当前没有覆盖这组回退。 |

### 发现 15：`CreateWidget` 同时留在 `UWidgetBlueprintStatics` 和手写 `WidgetBlueprint::` namespace 中，形成重复实现与双轨 API

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/WidgetBlueprintStatics.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UUserWidget.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleWidgetUmgTest.cpp` |
| 行号 | 7-24；286-293；47 |
| 描述 | `WidgetBlueprintStatics.h` 的类注释明确写着“Expands the `WidgetBlueprint::` namespace in script”，并在类里保留了 `CreateWidget(UObject* WorldContextObject, TSubclassOf<UUserWidget> WidgetType, APlayerController* OwningPlayer)`。但真正的 `WidgetBlueprint::CreateWidget` 命名空间绑定已经在 `Bind_UUserWidget.cpp` 286-293 手写实现，并同样调用 `UWidgetBlueprintLibrary::Create(...)`。示例测试 `AngelscriptScriptExampleWidgetUmgTest.cpp` 47 行走的也是命名空间版本，而不是 `UWidgetBlueprintStatics` 类方法。 |
| 根因 | UI 创建 helper 的职责在某个阶段从 FunctionLibrary 转移到了手写 namespace bind，但旧的 UObject 包装没有随之删除或重新定位。 |
| 影响 | 脚本侧同时看到“类静态函数”和“命名空间函数”两条几乎等价的 widget 创建路径，且两者的 world context 处理方式还不同步：一个显式传参，一个隐式读取当前 world context。后续如果只修一边，UI 创建行为和文档都会继续漂移。 |

---

## 分析 (2026-04-08 03:05)

### 发现 16：`URuntimeFloatCurveMixinLibrary` 丢失 `ScriptMixin` 后没有任何兜底绑定，整组曲线 helper 从实例方法退化成类静态函数

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/RuntimeFloatCurveMixinLibrary.h`；`../UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/FunctionLibraries/RuntimeFloatCurveMixinLibrary.h`；`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` |
| 行号 | 16-17，68-282；16-254；781-839 |
| 描述 | 当前 `URuntimeFloatCurveMixinLibrary` 把类级 `ScriptMixin = "FRuntimeFloatCurve UCurveFloat"` 注释掉，只剩 `UCLASS(meta = ())`，但 `GetNumKeys`、`GetTimeRange`、`GetValueRange`、`AddAutoCurveKey`、`SetKeyInterpMode` 等整组 helper 仍保留在同一个类里。UEAS2 参考实现仍把这批函数声明为 `FRuntimeFloatCurve` / `UCurveFloat` mixin。与此同时，当前仓库执行 `rg -n "RuntimeFloatCurve|UCurveFloat" Plugins/Angelscript/Source/AngelscriptRuntime/Binds` 无命中，说明没有任何手写 `ExistingClass("FRuntimeFloatCurve")` 或 `ExistingClass("UCurveFloat")` 兜底绑定把这些函数重新挂回实例方法。 |
| 根因 | 从旧 `ScriptCallable + ScriptMixin` 迁到 `BlueprintCallable` 时，只保留了函数体和 direct-bind 恢复测试，没有把类级 mixin 元数据一并迁移，也没有为 `FRuntimeFloatCurve` / `UCurveFloat` 补手写绑定。 |
| 影响 | 脚本侧会失去整组高频曲线编辑 API 的实例写法，例如 `Curve.GetNumKeys()`、`Curve.GetTimeRange(...)`、`Curve.AddAutoCurveKey(...)`、`Curve.SetKeyInterpMode(...)` 都不再能像 UEAS2 那样挂在 `FRuntimeFloatCurve` / `UCurveFloat` 上。现有测试只验证 `URuntimeFloatCurveMixinLibrary::StaticClass()` 下的 direct bind 能恢复（`AngelscriptBindConfigTests.cpp` 781-839），没有任何测试去编译或断言 `FRuntimeFloatCurve` / `UCurveFloat` 的实例方法面，因此这类 API 形态回退目前不会被发现。 |

### 发现 17：`USubsystemLibrary` 已经落后于当前 UE 原生 `USubsystemBlueprintLibrary`，缺少 `GetAudioEngineSubsystem`

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/SubsystemLibrary.h`；`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Public/Subsystems/SubsystemBlueprintLibrary.h` |
| 行号 | 17-64；23-46 |
| 描述 | 当前 `USubsystemLibrary` 只包装了 `GetEngineSubsystem`、`GetGameInstanceSubsystem`、`GetLocalPlayerSubsystem`、`GetWorldSubsystem`、`GetLocalPlayerSubsystemFromPlayerController` 和 `GetLocalPlayerSubsystemFromLocalPlayer` 六个入口；而当前 UE 原生 `USubsystemBlueprintLibrary` 在相同家族里还额外提供了 `GetAudioEngineSubsystem(UObject* ContextObject, TSubclassOf<UAudioEngineSubsystem> Class)`。在当前插件源码执行 `rg -n "GetAudioEngineSubsystem" Plugins/Angelscript/Source` 无命中，说明这条 helper 既没有被补进 FunctionLibrary，也没有迁到其它 bind 文件。 |
| 根因 | `SubsystemLibrary` 是对引擎内部 blueprint helper 的手工镜像，但后续没有随着 UE 原生 `USubsystemBlueprintLibrary` 的扩展继续同步。 |
| 影响 | Angelscript 对 subsystem 的暴露面已经不再与当前 UE 原生 helper 集合保持一致，脚本层拿不到 `UAudioEngineSubsystem` 的统一获取入口，只能绕回更底层的 world/audio-device 路径自行封装。`rg -n "GetAudioEngineSubsystem|SubsystemLibrary" Plugins/Angelscript/Source/AngelscriptTest` 同样无命中，当前没有任何测试覆盖这个缺口。 |

### 发现 18：`UAngelscriptScriptLibrary` 在迁移后丢掉了 `NotAngelscriptProperty` 和 `ScriptNoDiscard`，把运行期查询 API 退化成可隐式属性化、可静默丢弃的调用

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptScriptLibrary.h`；`../UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/FunctionLibraries/AngelscriptScriptLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp` |
| 行号 | 17-35；17-32；260，328，417-440；428 |
| 描述 | 当前 `GetNameOfGlobalVariableBeingInitialized`、`GetNamespaceOfGlobalVariableBeingInitialized`、`GetModuleNameOfGlobalVariableBeingInitialized` 三个入口只剩 `UFUNCTION(BlueprintCallable)`，注释里也只残留了旧的 `NotAngelscriptProperty`，而 UEAS2 版本明确为三者同时标记了 `NotAngelscriptProperty, ScriptNoDiscard`。绑定层在 `Helper_FunctionSignature.h` 260 行读取 `NotAngelscriptProperty`，在 328 行只有检测到 `ScriptNoDiscard` 才把 `no_discard` 追加进声明；同时 `AngelscriptBinds.cpp` 428 行会先把所有 C++ 绑定函数设成 property accessors，只有 `Helper_FunctionSignature.h` 440 行在检测到 `NotAngelscriptProperty` 时才会关掉这个行为。 |
| 根因 | 从 `ScriptCallable` 迁到 `BlueprintCallable` 时，只迁了最外层 callable 标记，没有把这组脚本语义元数据一并保留。 |
| 影响 | 这三条 API 本质上是“读取当前全局变量初始化上下文”的运行期查询，不再适合作为隐式 property accessor；同时返回值失去 `no_discard` 后，脚本可以无提示地忽略查询结果，降低诊断价值。`rg -n "GetNameOfGlobalVariableBeingInitialized|GetNamespaceOfGlobalVariableBeingInitialized|GetModuleNameOfGlobalVariableBeingInitialized|Script::Get" Plugins/Angelscript/Source/AngelscriptTest` 无命中，当前没有任何测试覆盖这些脚本元数据语义。 |

### 发现 19：`USubsystemLibrary` 的 6 个 subsystem getter 全部失去 `ScriptNoDiscard`，纯查询 API 的误用不再有脚本层诊断

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/SubsystemLibrary.h`；`../UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/FunctionLibraries/SubsystemLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h` |
| 行号 | 17-64；17-58；328 |
| 描述 | `GetEngineSubsystem`、`GetGameInstanceSubsystem`、`GetLocalPlayerSubsystem`、`GetWorldSubsystem`、`GetLocalPlayerSubsystemFromPlayerController`、`GetLocalPlayerSubsystemFromLocalPlayer` 在 UEAS2 版本里全部带有 `ScriptNoDiscard`，而当前版本在迁成 `BlueprintCallable` 后全部把这个元数据丢掉了。`Helper_FunctionSignature.h` 328 行明确只有函数带 `ScriptNoDiscard` 时才会在脚本声明后附加 `no_discard`。 |
| 根因 | subsystem wrapper 只完成了“让绑定器看见函数”的迁移，没有保留原先用来约束脚本调用方式的返回值语义。 |
| 影响 | 这 6 个函数都是典型“查一个 subsystem 并立即使用”的纯 getter；一旦脚本侧误写成单独调用而不检查返回值，当前绑定层不会给出任何 `no_discard` 级别提示，空 subsystem 路径更容易静默滑过去。`rg -n "GetEngineSubsystem\\(|GetGameInstanceSubsystem\\(|GetLocalPlayerSubsystem\\(|GetWorldSubsystem\\(|GetLocalPlayerSubsystemFromPlayerController\\(|GetLocalPlayerSubsystemFromLocalPlayer\\(" Plugins/Angelscript/Source/AngelscriptTest` 无命中，当前测试也没有覆盖这组诊断语义。 |

### 发现 20：`UGameplayLibrary::AsyncSaveGameToSlot` 把 UE 原生的可选 completion delegate 收窄成了必填参数

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/GameplayLibrary.h`；`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Kismet/GameplayStatics.h` |
| 行号 | 38-45；1156 |
| 描述 | UE 原生 `UGameplayStatics::AsyncSaveGameToSlot(...)` 在 `GameplayStatics.h` 1156 行提供了默认参数 `FAsyncSaveGameToSlotDelegate SavedDelegate = FAsyncSaveGameToSlotDelegate()`，允许调用方走 fire-and-forget 保存；当前 `UGameplayLibrary::AsyncSaveGameToSlot(...)` 却把动态 delegate 写成了无默认值的必填参数。当前插件源码里执行 `rg -n "AsyncSaveGameToSlot\\(" Plugins/Angelscript/Source/AngelscriptRuntime` 只命中这一个 wrapper，没有任何“无 delegate overload”可供替代。 |
| 根因 | 在把 native delegate 适配成 dynamic delegate 时，只保留了回调转发逻辑，没有同步保留原生签名的可选参数语义。 |
| 影响 | 脚本侧无法像原生 UE 调用那样直接发起不关心回调的异步保存，必须显式构造一个空 delegate 或额外写一层包装；这会让同样的 save-game 工作流在 Angelscript 里比 UE 原生更啰嗦，也破坏 API 一致性。`rg -n "AsyncSaveGameToSlot\\(|AsyncLoadGameFromSlot\\(" Plugins/Angelscript/Source/AngelscriptTest` 无命中，当前没有回归测试锁住这条签名差异。 |

---

## 分析 (2026-04-08 03:17)

### 发现 21：`UAssetManagerMixinLibrary` 同时落后于 UE5 原生和 UEAS2，缺少目录扫描与过滤版资产枚举能力

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/UAssetManagerMixinLibrary.h`；`../UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/FunctionLibraries/UAssetManagerMixinLibrary.h`；`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Engine/AssetManager.h` |
| 行号 | 7-83，48-50；76-82；154，238，259 |
| 描述 | 当前 `UAssetManagerMixinLibrary` 只包装了 `GetPrimaryAssetData*`、`GetPrimaryAssetObject`、`GetPrimaryAssetIdForObject`、`GetPrimaryAssetIdList`、`GetPrimaryAssetTypeInfo*` 与 `CallOrRegister_OnCompletedInitialScan`。相比之下，UEAS2 版本还额外暴露了 `ScanPathForPrimaryAssets(...)`；当前 UE5 原生 `UAssetManager` 还提供 `GetPrimaryAssetObjectList(...)`，并把 `GetPrimaryAssetIdList(...)` 扩展为带默认参数 `EAssetManagerFilter Filter = EAssetManagerFilter::Default` 的过滤版签名。当前插件源码执行 `rg -n "ScanPathForPrimaryAssets|GetPrimaryAssetObjectList|EAssetManagerFilter" Plugins/Angelscript/Source/AngelscriptRuntime` 只有 `GetPrimaryAssetIdList` 的旧签名命中，说明这几条能力既没有留在 FunctionLibrary，也没有迁到其它绑定文件。 |
| 根因 | `UAssetManagerMixinLibrary` 采用手工镜像 `UAssetManager` 的方式维护脚本 API，但后续既没有跟进 UE5 新增/扩展的目录与过滤接口，也没有保住 UEAS2 已经提供过的扫描入口。 |
| 影响 | 脚本侧无法像原生引擎那样直接扫描路径注册 primary assets、按 `EAssetManagerFilter` 过滤 `PrimaryAssetId` 集合，也拿不到某个 `PrimaryAssetType` 的已加载 `UObject` 列表；涉及 startup scan、资源热注册和资产枚举的工作流都需要额外自写 wrapper。`rg -n "ScanPathForPrimaryAssets|GetPrimaryAssetObjectList|GetPrimaryAssetIdList\\(" Plugins/Angelscript/Source/AngelscriptTest` 无命中，当前没有测试会在这组缺口出现时报警。 |

### 发现 22：`FVector` / `FVector3f` 的自定义平面 helper 从实例 API 面脱落，`FVector` 版还少了一整个 `GetSafeNormal2D` overload

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptMathLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FVector.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FVector3f.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`；`../UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/FunctionLibraries/AngelscriptMathLibrary.h` |
| 行号 | 337-425，428-510；228-296；220-287；276-314；347-404，414-477 |
| 描述 | 当前 `UAngelscriptFVectorMixinLibrary` / `UAngelscriptFVector3fMixinLibrary` 都把类级 `ScriptMixin` 注释掉，只剩 `UCLASS(Meta = ())`。而 `Helper_FunctionSignature.h` 明确只有 outer class 带 `ScriptMixin` 且首参数类型匹配时，才会把静态 helper 改写成目标类型实例方法；否则退回静态类函数。结果就是 `Size2D(Vector, UpDirection)`、`SizeSquared2D(..., UpDirection)`、`PointPlaneProject`、`Dist2D(..., UpDirection)`、`DistSquared2D(..., UpDirection)`、`ConstrainToPlane`、`ConstrainToDirection`、`MoveTowards` 这批围绕“任意 UpDirection/平面”的 helper 都不再出现在 `FVector` / `FVector3f` 实例 API 上。手写绑定 `Bind_FVector.cpp` / `Bind_FVector3f.cpp` 只补了原生 `Size2D()`、`Dist2D(const Other&)`、`GetSafeNormal2D()` 这类 XY 平面版本，没有任何对应的自定义平面 overload。更进一步，UEAS2 里 `FVector` 版本还曾额外提供 `GetSafeNormal2D(const FVector& Vector, const FVector& UpDirection, ...)`，当前头文件已经把这条 helper 整体删除。 |
| 根因 | 数学库在从旧 `ScriptCallable + ScriptMixin` 迁到当前手写 bind 体系时，只保留了引擎自带的 `FVector` / `FVector3f` 方法；插件自己扩展出来的“自定义平面”工具既没有保住 mixin 元数据，也没有逐条迁到 `Bind_FVector*`。 |
| 影响 | 脚本侧仍能调用内建的 XY 平面 `Size2D()` / `Dist2D()`，却失去了面向任意 `UpDirection` 的同族 helper，一旦做球面世界、任意重力方向或局部平面运动，就得退回 `UAngelscriptFVectorMixinLibrary::*` 这种脱离类型的方法名，甚至自己重写 `GetSafeNormal2D(..., UpDirection)`。`rg -n "Size2D\\(|Dist2D\\(|PointPlaneProject\\(|ConstrainToPlane\\(|ConstrainToDirection\\(|MoveTowards\\(|GetSafeNormal2D\\(" Plugins/Angelscript/Source/AngelscriptTest` 无命中，当前没有测试锁住这组自定义平面 helper 的可见性与行为。 |

### 发现 23：`RuntimeCurveLinearColor` 的 `AddDefaultKey` 被在 FunctionLibrary 和手写 bind 中各维护一份，实现已经分叉成双源

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/RuntimeCurveLinearColorMixinLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FunctionLibraryMixins.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp` |
| 行号 | 8-22；14-31；542-564 |
| 描述 | `URuntimeCurveLinearColorMixinLibrary::AddDefaultKey(...)` 本身已经在头文件里实现了一次 4 通道 `AddKey` 写入；`Bind_FunctionLibraryMixins.cpp` 又在 `ExistingClass("FRuntimeCurveLinearColor").Method("AddDefaultKey", ...)` 里把完全相同的四行 `ColorCurves[0..3].AddKey(...)` 再抄了一遍，随后又额外绑了一条 `URuntimeCurveLinearColorMixinLibrary::AddDefaultKey(...)` 命名空间入口。结果是同一个 helper 的业务逻辑至少存在“头文件静态函数”和“手写 lambda”两份实现源。现有 parity test 只验证 `Curve.AddDefaultKey(...)` 与 `URuntimeCurveLinearColorMixinLibrary::AddDefaultKey(...)` 都能编译，不验证两条路径对曲线 key 的实际写入结果始终一致。 |
| 根因 | 这个库在从旧 FunctionLibrary 走向手写 mixin 补绑时，没有把实例方法绑定直接复用到现有静态 helper，而是把实现体再复制了一次。 |
| 影响 | 后续只要有人在其中一处修改 key 插入策略，例如补插值模式、切线模式或 editor-only 行为，另一处就会悄悄漂移，脚本层同名 API 可能编译都通过但运行结果不一致。当前测试面只覆盖“入口存在”，没有覆盖“双源实现是否同值”。 |

### 发现 24：`UAssetManagerMixinLibrary` 剩余的查询 helper 也已脱离 `UAssetManager` 实例面，当前资产 API 被拆成两套入口

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/UAssetManagerMixinLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UAssetManager.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`；`../UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/FunctionLibraries/UAssetManagerMixinLibrary.h` |
| 行号 | 6-83；83-112；276-314；7-82 |
| 描述 | 当前 `UAssetManagerMixinLibrary` 已把类级 `ScriptMixin = "UAssetManager"` 注释掉，只剩 `UCLASS(MinimalAPI, Meta = ())`。按照 `Helper_FunctionSignature.h` 276-314 行的规则，这意味着 `GetPrimaryAssetData`、`GetPrimaryAssetDataList`、`GetPrimaryAssetObject`、`GetPrimaryAssetIdForObject`、`GetPrimaryAssetIdList`、`CallOrRegister_OnCompletedInitialScan` 不会再被改写成 `UAssetManager` 实例方法，而是退回 `UAssetManagerMixinLibrary::*` 这一路静态入口。与此同时，真正手写挂到 `UAssetManager` 上的只有 `Bind_UAssetManager.cpp` 87-105 行那组 `GetPrimaryAssetIdForPath`、`GetPrimaryAssetPath`、`GetPrimaryAssetIdForData`、`LoadPrimaryAsset*`、`UnloadPrimaryAsset*`。UEAS2 版本则是整类都作为 `UAssetManager` mixin 暴露。 |
| 根因 | 资产管理 API 在从旧 `ScriptCallable + ScriptMixin` 迁向“部分手写 bind、部分 BlueprintCallable 包装”过程中，没有维持统一的暴露策略，导致一半 helper 留在 FunctionLibrary 静态类，一半 helper 被迁到真实 `UAssetManager` 类型上。 |
| 影响 | 脚本作者面对的是割裂的资产 API：有些操作写成 `AssetManager.LoadPrimaryAsset(...)`，有些近邻查询却必须退回 `UAssetManagerMixinLibrary::GetPrimaryAssetData(AssetManager, ...)`。这既破坏了类型直觉，也让文档和自动补全更难形成完整心智模型。`rg -n "GetPrimaryAssetData\\(|GetPrimaryAssetDataList\\(|GetPrimaryAssetObject\\(|GetPrimaryAssetIdForObject\\(|CallOrRegister_OnCompletedInitialScan\\(" Plugins/Angelscript/Source/AngelscriptTest` 无命中，当前没有测试约束这组入口必须留在 `UAssetManager` 实例 API 上。 |

### 发现 25：`FRotator` / `FRotator3f` 的高频方向与组合 helper 退化成 namespace 静态函数，实例方法面出现明显断层

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptMathLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FRotator.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FRotator3f.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`；`../UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/FunctionLibraries/AngelscriptMathLibrary.h` |
| 行号 | 512-621；163-214；165-213；276-314；512-575 |
| 描述 | 当前 `UAngelscriptFRotatorLibrary` / `UAngelscriptFRotator3fLibrary` 保留了 `ScriptName = "FRotator" / "FRotator3f"`，但把类级 `ScriptMixin` 注释掉了。这样一来，`MakeFromAxes` 这类纯工厂函数还能继续挂在 `FRotator::` / `FRotator3f::` namespace 下，而首参数是 rotator 本身的 helper 则不会再被改写成实例方法。源码里仍声明着 `GetForwardVector`、`GetRightVector`、`GetUpVector`、`Compose`、`AngularDistance`；可手写绑定 `Bind_FRotator*.cpp` 实际只补了 `Vector()`、`Quaternion()`、`Euler()`、`RotateVector()`、`UnrotateVector()` 等 method，没有任何 `GetRightVector` / `GetUpVector` / `Compose` / `AngularDistance` 对应 `Method(...)` 绑定。UEAS2 版本则把这整组函数作为 `FRotator` / `FRotator3f` mixin 暴露。 |
| 根因 | rotator 辅助库在迁移过程中只保住了 `ScriptName` 带来的 namespace 工厂函数，却没有给依赖 `ScriptMixin` 的实例 helper 做手写回填。 |
| 影响 | 当前脚本 API 面会出现很不自然的断层：`Rotator.Vector()` 仍可用，但 `Rotator.GetRightVector()`、`Rotator.Compose(Other)`、`Rotator.AngularDistance(Other)` 这组同域 helper 不再以实例方法形式出现，调用者只能改写成 `FRotator::GetRightVector(Rotator)` / `FRotator::Compose(A, B)` 风格。`rg -n "GetForwardVector|GetRightVector|GetUpVector|Compose\\(|AngularDistance\\(" Plugins/Angelscript/Source/AngelscriptTest` 无命中，当前没有测试会在这组实例 helper 退化时报警。 |

---

## 分析 (2026-04-08 03:30)

### 发现 26：`Math::Wrap` 的 `uint32` overload 在当前仓库被整段注释掉，unsigned 包装能力直接消失

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptMathLibrary.h`；`../UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/FunctionLibraries/AngelscriptMathLibrary.h` |
| 行号 | 241-284；229-271 |
| 描述 | 当前 `UAngelscriptMathLibrary` 里 `WrapUInt(uint32 X, uint32 Min, uint32 Max)` 从 `UFUNCTION` 到函数体全部被注释掉，只剩文档注释和一整段失效实现；而 UEAS2 参考实现中这条 `ScriptName = "Wrap"` 的 overload 仍然存在并可绑定。当前文件里 `int32` 版 `Wrap(...)` 还在正常暴露，说明这不是“整个 API 家族退役”，而是仅 `uint32` 版本在迁移过程中被静态裁掉。 |
| 根因 | 数学库从旧 `ScriptCallable` 迁到 `BlueprintCallable` 时，没有给 `uint32` 版 `Wrap` 找到兼容的新暴露路径，最后直接把声明和实现一起注释，留下半删除状态。 |
| 影响 | 脚本侧失去 `Math::Wrap(uint32, uint32, uint32)` 这一条与 `int32` 对称的高频 utility，处理 bitmask、索引缓存或 asset ID 这类天然使用 unsigned 的值时只能手动转成 `int32` 或重写包装逻辑。`rg -n "WrapUInt|WrapIndexUInt|WrapIndex\\(" Plugins/Angelscript/Source/AngelscriptTest` 无命中，当前没有任何测试会在 unsigned wrapping helper 消失时报警。 |

### 发现 27：`FTransform` / `FTransform3f` 失去 rotator 友好 overload，实例 API 只剩 quaternion 版本

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptMathLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FTransform.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FTransform3f.cpp`；`../UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/FunctionLibraries/AngelscriptMathLibrary.h` |
| 行号 | 781-821，867-895；151-205；150-200；713-747，787-809 |
| 描述 | 当前 `UAngelscriptFTransformLibrary` / `UAngelscriptFTransform3fLibrary` 仍然保留 `TransformRotation(const FRotator&)`、`InverseTransformRotation(const FRotator&)`、`SetRotation(const FRotator&)` 以及 `FRotator3f` 对应 overload，但类级 `ScriptMixin` 已被注释，只剩 `ScriptName = "FTransform" / "FTransform3f"`。与此同时，手写绑定 `Bind_FTransform.cpp` / `Bind_FTransform3f.cpp` 只给实例类型补了 `TransformRotation(const FQuat&)`、`InverseTransformRotation(const FQuat&)` 和 `SetRotation(const FQuat&)` / `FQuat4f` 版本，没有任何 rotator 版本 `Method(...)` 兜底。UEAS2 版本则把这三组 rotator overload 作为 `FTransform` / `FTransform3f` mixin 直接暴露。 |
| 根因 | `FTransform` 系列在迁往手写 bind 时优先保住了原生 quaternion 成员函数，却没有把 FunctionLibrary 里“rotator 转 quat 后再转回”的脚本便利 overload 一并回填到实例方法面。 |
| 影响 | 脚本作者现在只能写 `Transform.TransformRotation(FQuat(Rotator)).Rotator()`、`Transform.SetRotation(Rotator.Quaternion())` 这类更底层的样板，失去 UEAS2 曾提供的直接 rotator overload。由于 `SetRotation(FTransform&, const FRotator&)` 旧版本还带 `NotAngelscriptProperty` 元数据，而当前 BlueprintCallable 版本也把这层脚本语义一起丢掉，接口退化不仅体现在可见性，也体现在调用形态。`rg -n "TransformRotation\\(const FRotator|InverseTransformRotation\\(const FRotator|SetRotation\\(const FRotator|TransformRotation\\(const FRotator3f|InverseTransformRotation\\(const FRotator3f|SetRotation\\(const FRotator3f" Plugins/Angelscript/Source/AngelscriptTest` 无命中，当前没有测试会在这组 rotator overload 掉线时报警。 |

### 发现 28：`TSoftObjectPtr::LoadAsync` 删掉了空引用保护，null soft path 会落到必然失败的异步包加载路径

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSoftObjectPtr.cpp`；`../UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_TSoftObjectPtr.cpp`；`J:/UnrealEngine/UERelease/Engine/Source/Runtime/CoreUObject/Private/Serialization/AsyncPackageLoader.cpp` |
| 行号 | 483-526；486-534；629-637 |
| 描述 | 当前 `TSoftObjectPtr_.Method("void LoadAsync(FOnSoftObjectLoaded OnLoaded) const", ...)` 在读取 subtype 后直接进入“已加载对象短路”或 `LoadPackageAsync(*FPackageName::ObjectPathToPackageName(ObjectCopy.ToString()), ...)` 分支，中间不再检查 `Self->IsNull()`。UEAS2 版本同一位置曾先对 null soft reference 抛错并返回。引擎 `AsyncPackageLoader.cpp` 629-637 行的注释明确写着：`PackagePath` 为空时“we are going to fail because its not mounted”。这意味着当前实现会把空 object path 转成空 package path，再进入一个已知会失败的异步加载流程。 |
| 根因 | 软引用绑定在从旧 `asCObjectType* TemplateType` 传参模式切到 `GetCurrentFunctionObjectType()` 后，保留了 actor/component 限制和成功路径逻辑，却把 null soft reference 的前置保护删掉了。 |
| 影响 | 以前脚本对空 `TSoftObjectPtr` 调 `LoadAsync()` 会立即得到同步错误；现在会静默排队一个注定失败的 async request，调用方只能在回调里拿到 `nullptr` 或依赖底层日志排查，诊断性明显变差。`rg -n "TSoftObjectPtr|TSoftClassPtr|LoadAsync\\(" Plugins/Angelscript/Source/AngelscriptTest` 只覆盖了 `Get()`、构造和 editor-only load 的 smoke test，没有任何针对 null `LoadAsync()` 行为的回归用例。 |

### 发现 29：`ULevelStreaming::GetShouldBeVisibleInEditor()` 同时由 FunctionLibrary 和手写 bind 暴露，形成双源实现

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptLevelStreamingLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FunctionLibraryMixins.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp` |
| 行号 | 6-18；7-12；1311-1314，1403-1406；527-531 |
| 描述 | `UAngelscriptLevelStreamingLibrary` 仍保留 `ScriptMixin = "ULevelStreaming"`，并在 `#if WITH_EDITOR` 下把 `GetShouldBeVisibleInEditor(const ULevelStreaming*)` 声明成 `UFUNCTION(BlueprintCallable)`。绑定器 `Bind_BlueprintType.cpp` 会对 `BlueprintCallable` 函数自动走 `BindBlueprintCallable(...)`。但 `Bind_FunctionLibraryMixins.cpp` 9-12 行又手工对 `ExistingClass("ULevelStreaming")` 绑了一次完全同名同签名的 `GetShouldBeVisibleInEditor() const`。结果是这条 helper 同时存在“自动收集的 FunctionLibrary 路径”和“手写 bind 路径”两份来源。现有 parity test 只断言 `ULevelStreaming` 上能拿到该方法，没有验证它只由单一来源维护。 |
| 根因 | `ULevelStreaming` 这个 editor-only helper 在迁移过程中没有确定单一暴露策略，既没有删除 FunctionLibrary 声明，也没有让手写补绑复用该静态 helper。 |
| 影响 | 只要后续有人修改其中一处签名、文档注释、editor-only 条件或行为，另一处就会继续滞后，最终造成“脚本里方法还在，但实现来源已经分叉”的隐性维护债务。当前测试只验证入口存在，不验证绑定来源是否唯一，因此双源漂移不会被自动发现。 |

### 发现 30：`FQuat` / `FQuat4f` 的轴向构造 helper 丢掉了 `no_discard` 语义，而同命名空间的手写全局函数仍保留该约束

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptMathLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FQuat.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FQuat4f.cpp`；`../UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/FunctionLibraries/AngelscriptMathLibrary.h` |
| 行号 | 623-699，702-779；142-161；142-159；577-642，645-710 |
| 描述 | 当前 `UAngelscriptFQuatLibrary` / `UAngelscriptFQuat4fLibrary` 中 `MakeFromX/Y/Z`、`MakeFromXY/XZ/YX/YZ/ZX/ZY`、`MakeFromAxes` 这整组纯工厂 helper 都只剩 `UFUNCTION(BlueprintCallable)`，注释里保留的旧声明则明确带有 `ScriptNoDiscard`。相比之下，同一命名空间下手写绑定的 `FQuat::MakeFromEuler`、`MakeFromRotator`、`FindBetween*`、`Slerp*` 以及 `FQuat4f::MakeFromEuler`、`FindBetween*` 等函数仍显式带 `no_discard`。UEAS2 版本里，这批轴向构造 helper 也同样带有 `ScriptNoDiscard`。结果是 `FQuat::MakeFromX(...)` 这类函数在当前脚本层可以被静默丢弃，而相邻的 `FQuat::MakeFromEuler(...)` 却仍会触发 `no_discard` 级别诊断。 |
| 根因 | quaternion 工厂 API 在从 `ScriptCallable` 迁到 `BlueprintCallable` 时，只迁移了可见性，没有把原先的 `ScriptNoDiscard` 语义一起迁走；而手写 bind 维护的那部分命名空间函数继续保留了旧约束，造成同域 API 规则不一致。 |
| 影响 | 脚本作者会在同一个 `FQuat` / `FQuat4f` 命名空间里遇到“有的工厂函数必须消费返回值，有的却能被无提示忽略”的行为分裂，降低 API 可预测性，也弱化了对误写代码的静态诊断。`rg -n "MakeFromAxes|MakeFromXY\\(|MakeFromXZ\\(|MakeFromYX\\(|MakeFromYZ\\(|MakeFromZX\\(|MakeFromZY\\(|MakeFromX\\(|MakeFromY\\(|MakeFromZ\\(" Plugins/Angelscript/Source/AngelscriptTest` 无命中，当前没有测试覆盖这组 helper 的脚本声明语义。 |

---

## 分析 (2026-04-08 03:39)

### 发现 31：`SubsystemLibrary` 的 6 个 getter 在迁移后全部丢失 `ScriptNoDiscard`，返回的 subsystem handle 可以被静默丢弃

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/SubsystemLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`；`../UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/FunctionLibraries/SubsystemLibrary.h` |
| 行号 | 16-62；325-330；16-55 |
| 描述 | 当前 `USubsystemLibrary` 里 `GetEngineSubsystem`、`GetGameInstanceSubsystem`、`GetLocalPlayerSubsystem`、`GetWorldSubsystem`、`GetLocalPlayerSubsystemFromPlayerController`、`GetLocalPlayerSubsystemFromLocalPlayer` 都只剩 `UFUNCTION(BlueprintCallable, Meta = (...))`，而 UEAS2 版本这 6 个返回指针的 helper 全部带有 `ScriptNoDiscard`。`Helper_FunctionSignature.h` 明确只有函数保留 `ScriptNoDiscard` 元数据时，最终脚本声明才会追加 `no_discard`。因此当前脚本层对这组 subsystem getter 已经失去“返回值必须消费”的静态约束。 |
| 根因 | 这组封装在从 `ScriptCallable` 迁移到 `BlueprintCallable` 时，只保住了可见性和 `DeterminesOutputType` / `WorldContext` 元数据，没有把原先的 `ScriptNoDiscard` 一起迁移。 |
| 影响 | subsystem 获取 API 的脚本诊断能力退化为“可以无提示地写出 `Subsystem::GetWorldSubsystem(...)` 然后把结果直接丢掉”，与 UEAS2 以及当前仓库里大量保留 `no_discard` 的 query/helper 不一致。`rg -n "GetEngineSubsystem\\(|GetGameInstanceSubsystem\\(|GetLocalPlayerSubsystemFromPlayerController\\(|GetLocalPlayerSubsystemFromLocalPlayer\\(|GetWorldSubsystem\\(" Plugins/Angelscript/Source/AngelscriptTest` 无命中，当前没有测试锁住这组 getter 的声明语义。 |

### 发现 32：`AngelscriptHitResultLibrary` 在当前分支缩成了残缺子集，`Reset` 高级参数和 `SetPhysMaterial` 已经完全掉线

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptHitResultLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FHitResult.cpp`；`../UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/FunctionLibraries/AngelscriptHitResultLibrary.h` |
| 行号 | 8-91；7-42；8-113 |
| 描述 | 当前 `UAngelscriptHitResultLibrary` 已经不再暴露 UEAS2 版本里的 `Reset(FHitResult&, float InTime = 1.f, bool bPreserveTraceData = true)`，而是退化成无参 `Reset(FHitResult&)`，同时 `SetPhysMaterial(FHitResult&, UPhysicalMaterial*)` 整个 helper 被删除，只剩 `GetPhysMaterial(...)`。`Bind_FHitResult.cpp` 现有手写绑定也只补了构造函数和字段 property，没有任何 `Method("Reset"... )` 或 `Method("SetPhysMaterial"... )` 兜底，因此这两个能力在当前脚本 API 面中确实不存在，而不是换了绑定位置。 |
| 根因 | `FHitResult` 功能在从旧 `ScriptMixin + ScriptCallable` 迁往“少量 property 手写绑定 + BlueprintCallable 包装”时，只保留了最基础的读写入口，没有把需要附加参数或非字段赋值的 helper 一并迁走。 |
| 影响 | 脚本侧无法再精确控制 `FHitResult::Reset` 是否保留 trace data，也不能直接写入 `PhysMaterial`；涉及命中结果复用、手工构造 trace 命中、测试桩伪造 `PhysMaterial` 的场景都要退回原生 C++ 或改写为更笨重的字段拼装。`rg -n "SetPhysMaterial|GetPhysMaterial|SetComponent\\(|SetActor\\(|SetBlockingHit|SetbBlockingHit|SetbStartPenetrating|Reset\\(" Plugins/Angelscript/Source/AngelscriptTest` 只有与这些 helper 无关的通用 `Reset()` 命中，当前没有针对 `FHitResult` helper 面完整性的回归测试。 |

### 发现 33：`WorldCollision` 全局 helper 统一走 `GetWorld()->...` 直解引用，脚本一旦脱离 world context 会直接崩溃

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_WorldCollision.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp` |
| 行号 | 12-17，146-317，328-467；264-306 |
| 描述 | `WorldCollision::GetWorld()` 明确调用 `GEngine->GetWorldFromContextObject(..., EGetWorldErrorMode::LogAndReturnNull)`，也就是拿不到 world 时直接返回 `nullptr`。但同文件从同步 trace 到 async trace、再到 `QueryTraceData` / `QueryOverlapData` / `IsTraceHandleValid` 的所有绑定 lambda 都紧接着无条件执行 `WorldCollision::GetWorld()->...`，没有任何 `nullptr` 检查或失败返回路径。也就是说，只要脚本在没有当前 world context 的位置触发这些 helper，就会把“记录日志并返回 null”立刻升级成空指针解引用。 |
| 根因 | 这批全局 utility 把 `GetWorldFromContextObject` 的 world 解析逻辑抽到了统一 helper，但后续所有调用点都默认 world 一定存在，没有把 `LogAndReturnNull` 对应的失败分支落实到 API 边界。 |
| 影响 | 与 `System::LineTrace*`、`System::Sweep*`、`System::Overlap*`、`System::Async*Trace*` 以及 `QueryTraceData` 同族的全部 helper 都共享同一条崩溃路径，任何无 world 上下文的脚本调用都可能直接打挂运行时。现有 `FAngelscriptWorldCollisionBindingTest` 仅验证一段脚本能编译出这些调用，`AngelscriptEngineParityTests.cpp` 264-306 行没有执行它们，也没有覆盖 null world 行为，因此这条 crash path 现在不会被测试发现。 |

### 发现 34：`Script` 命名空间的 3 个“正在初始化的全局变量”查询函数同时丢失 `NotAngelscriptProperty` 与 `ScriptNoDiscard`

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptScriptLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`；`../UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/FunctionLibraries/AngelscriptScriptLibrary.h` |
| 行号 | 15-35；24-30，260，325-330，417-440；15-30 |
| 描述 | 当前 `GetNameOfGlobalVariableBeingInitialized()`、`GetNamespaceOfGlobalVariableBeingInitialized()`、`GetModuleNameOfGlobalVariableBeingInitialized()` 都只剩 `UFUNCTION(BlueprintCallable)`；UEAS2 版本则统一带有 `Meta = (NotAngelscriptProperty, ScriptNoDiscard)`。`Helper_FunctionSignature.h` 显示这两个元数据分别决定了脚本函数是否被标成 property，以及是否追加 `no_discard`。因此这组三个 query helper 在当前脚本层已经失去“不要当属性”和“返回值不要被忽略”两层声明约束。 |
| 根因 | `UAngelscriptScriptLibrary` 在从旧 `ScriptCallable` 声明迁移到 `BlueprintCallable` 时，只保留了最基本的可见性，没有把 Angelscript 特有的脚本形态元数据一起迁回。 |
| 影响 | `Script::GetNameOfGlobalVariableBeingInitialized()` 这类 runtime introspection API 会以比旧版本更弱的脚本契约暴露出来，既可能被当作 property 参与自动属性语义，也允许脚本静默丢弃返回的字符串结果，进一步拉大与历史 API 预期的偏差。`rg -n "GetNameOfGlobalVariableBeingInitialized|GetNamespaceOfGlobalVariableBeingInitialized|GetModuleNameOfGlobalVariableBeingInitialized" Plugins/Angelscript/Source/AngelscriptTest` 无命中，当前没有测试约束这组三个 helper 的脚本声明形态。 |

### 发现 35：`Gameplay::AsyncSaveGameToSlot` 没有对齐 UE 原生的“回调可选”签名，脚本侧被迫传入占位 delegate

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/GameplayLibrary.h`；`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Kismet/GameplayStatics.h` |
| 行号 | 26-45；1147-1156 |
| 描述 | `UGameplayLibrary::AsyncSaveGameToSlot(...)` 当前声明把 `FAsyncSaveGameToSlotDynamicDelegate Delegate` 设成必填参数，而 UE 原生 `UGameplayStatics::AsyncSaveGameToSlot(...)` 在 `GameplayStatics.h` 1156 行明确给 `SavedDelegate` 提供了默认值 `FAsyncSaveGameToSlotDelegate()`。这意味着原生 API 支持“只发起异步保存、不关心完成回调”的调用方式，但脚本 wrapper 把这条调用形态裁掉了。 |
| 根因 | 该 wrapper 在把原生 `FAsyncSaveGameToSlotDelegate` 适配成 dynamic delegate 时，只复刻了参数列表，没有继续保留原生函数的默认参数语义。 |
| 影响 | 脚本作者如果只想 fire-and-forget 保存，就必须构造一个空 `FAsyncSaveGameToSlotDynamicDelegate` 作为占位参数，API 体验与 UE 原生不一致，也会让自动补全误导调用方以为回调始终必需。`rg -n "AsyncSaveGameToSlot\\(|AsyncLoadGameFromSlot\\(" Plugins/Angelscript/Source/AngelscriptTest` 无命中，当前没有测试覆盖这条签名一致性。 |

### 发现 36：`FHitResult` 的脚本文档覆盖在当前分支被整体拔掉，碰撞结果字段只剩裸名字没有说明

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Low |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FHitResult.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.h`；`../UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_FHitResult.cpp` |
| 行号 | 1-42；16-34；1-57 |
| 描述 | 当前 `Bind_FHitResult.cpp` 只注册了构造函数和 property，本文件既没有包含 `AngelscriptDocs.h`，也没有像 UEAS2 那样为 `FaceIndex`、`ElementIndex`、`PenetrationDepth`、`ImpactPoint`、`Location`、`Normal`、`BoneName` 等字段逐条调用 `SCRIPT_PROPERTY_DOCUMENTATION(...)`。与此同时，当前 `Core/AngelscriptDocs.h` 仍然保留 `AddUnrealDocumentationForProperty` / `GetUnrealDocumentationForProperty` 接口，说明脚本文档系统依然支持 property 级说明，只是 `FHitResult` 这组绑定已经不再向它喂任何内容。 |
| 根因 | `FHitResult` 绑定在迁移或清理过程中只保留了最小的 property 注册代码，原先那批对碰撞字段语义的说明文本没有被一起迁回当前 bind。 |
| 影响 | `FHitResult` 属于 trace / overlap helper 的核心结果类型，但脚本作者在自动补全、文档导出或编辑器提示里将只看到字段名，看不到“`ImpactPoint` 与 `Location` 的区别”“`PenetrationDepth` 何时有效”这类关键说明，易把碰撞结果读错。`rg -n "GetUnrealDocumentationForProperty|GetUnrealDocumentationCount|GetUnrealDocumentation\\(" Plugins/Angelscript/Source/AngelscriptTest` 无命中，当前没有测试监控 property 文档条目是否被持续产出。 |

---

## 分析 (2026-04-08 03:49)

### 发现 37：`FVector` / `FVector3f` 的高频扩展 helper 仍留在 FunctionLibrary 中，但实例方法绑定已整体掉线

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptMathLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FVector.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FVector3f.cpp`；`../UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/FunctionLibraries/AngelscriptMathLibrary.h` |
| 行号 | 338-422，429-503；120-341；112-309；322-401，407-471 |
| 描述 | 当前 `UAngelscriptFVectorMixinLibrary` / `UAngelscriptFVector3fMixinLibrary` 的类级声明都退化成了 `UCLASS(Meta = ())`，不再是 UEAS2 里的 `ScriptMixin = "FVector"` / `ScriptMixin = "FVector3f"`。但库里仍保留了一批只存在于 FunctionLibrary 的扩展 helper，例如 `Size2D(Vector, UpDirection)`、`Dist2D(Vector, Other, UpDirection)`、`AngularDistance`、`AngularDistanceForNormals`、`ConstrainToPlane`、`ConstrainToDirection`、`ToColorString`、`MoveTowards`。对当前仓库执行 `rg -n "AngularDistance\\(|ConstrainToPlane\\(|ConstrainToDirection\\(|ToColorString\\(|MoveTowards\\(|Size2D\\(const FVector& Vector, const FVector& UpDirection|Dist2D\\(const FVector& Vector, const FVector& Other, const FVector& UpDirection|Size2D\\(const FVector3f& Vector, const FVector3f& UpDirection|Dist2D\\(const FVector3f& Vector, const FVector3f& Other, const FVector3f& UpDirection" Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FVector.cpp Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FVector3f.cpp` 无命中；对应 bind 文件只覆盖了原生成员如 `Projection()`、`Dist2D(const Other)` 等，并没有把这批 helper 手写补回 `FVector` / `FVector3f`。 |
| 根因 | 数学库从旧 `ScriptMixin` 方案迁向手写结构体绑定时，只迁移了引擎原生就存在的成员函数，没有把 FunctionLibrary 里额外提供的脚本便利 helper 一并迁过去。 |
| 影响 | 脚本 API 面明显退化：过去可直接写 `Vector.ConstrainToPlane(...)`、`Vector.MoveTowards(...)`、`Vector.ToColorString()`，现在这些 helper 虽然还躺在源码里，却不再作为 `FVector` / `FVector3f` 的实例能力存在。`rg -n "ConstrainToPlane|ConstrainToDirection|MoveTowards|AngularDistanceForNormals|ToColorString|Size2D\\(|Dist2D\\(" Plugins/Angelscript/Source/AngelscriptTest` 无命中，当前没有测试能在这批向量 utility 掉线时报警。 |

---

## 分析 (2026-04-08 03:51)

### 发现 38：UEAS2 中按任意 `UpDirection` 取二维安全法线的 `FVector` helper 已被整个删除

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `../UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/FunctionLibraries/AngelscriptMathLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptMathLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FVector.cpp` |
| 行号 | 360-364；338-422；262 |
| 描述 | UEAS2 版本曾提供 `GetSafeNormal2D(const FVector& Vector, const FVector& UpDirection, double Tolerance = 0.0, const FVector& ResultIfZero = FVector::ZeroVector)`，实现是先按任意 `UpDirection` 做 `VectorPlaneProject`，再取安全法线。当前 `AngelscriptMathLibrary.h` 的 `UAngelscriptFVectorMixinLibrary` 已不再包含这条函数；现行 `Bind_FVector.cpp` 只剩原生 `FVector::GetSafeNormal2D(float64 Tolerance = SMALL_NUMBER, const FVector& ResultIfZero = FVector::ZeroVector) const`，它固定按 XY 平面工作，没有 `UpDirection` 参数。全仓 `rg -n "GetSafeNormal2D\\(" Plugins/Angelscript/Source/AngelscriptRuntime` 只命中这一条原生绑定。 |
| 根因 | 数学辅助库在裁剪 `FVector` mixin 能力时，保留了 UE 原生自带的 XY 平面版本，却直接删掉了脚本层补出来的“任意平面二维法线”高层 overload。 |
| 影响 | 对于角色地面切线、局部重力、球面移动等并不以世界 Z 轴为“二维平面法线”的脚本逻辑，当前 API 已无法像 UEAS2 那样直接写 `Vector.GetSafeNormal2D(UpDirection, ...)`，只能手动组合 `VectorPlaneProject(...).GetSafeNormal(...)`。`rg -n "GetSafeNormal2D\\(" Plugins/Angelscript/Source/AngelscriptTest` 无命中，现有测试也没有覆盖这条 utility 的缺失。 |

---

## 分析 (2026-04-08 03:52)

### 发现 39：`FRuntimeCurveLinearColor::AddDefaultKey` 同时由自动 FunctionLibrary 绑定和手写 mixin 绑定维护

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/RuntimeCurveLinearColorMixinLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FunctionLibraryMixins.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp` |
| 行号 | 8-22；14-30；1311-1314，1403-1406；548-560 |
| 描述 | `URuntimeCurveLinearColorMixinLibrary` 仍声明了 `ScriptMixin = "FRuntimeCurveLinearColor"`，且 `AddDefaultKey(...)` 本身就是 `UFUNCTION(BlueprintCallable)`；按 `Bind_BlueprintType.cpp` 1311-1314、1403-1406 的自动收集逻辑，这已经足以走 `BindBlueprintCallable(...)`。但 `Bind_FunctionLibraryMixins.cpp` 14-22 行又手工给 `ExistingClass("FRuntimeCurveLinearColor")` 绑了一次完全同名的 `AddDefaultKey(float32 InTime, FLinearColor InColor)`，并在 25-30 行额外手写了 `URuntimeCurveLinearColorMixinLibrary::AddDefaultKey(...)` 的全局 helper 入口。现有 parity test 548-560 行还同时要求 `Curve.AddDefaultKey(...)` 和 `URuntimeCurveLinearColorMixinLibrary::AddDefaultKey(...)` 都能编译，却没有验证实例方法只来自单一路径。 |
| 根因 | `FRuntimeCurveLinearColor` 的 helper 在迁移过程中没有统一暴露策略，自动 FunctionLibrary 绑定和手写 mixin 绑定被同时保留下来。 |
| 影响 | 这条 API 的行为、签名和文档以后只要在任一实现源里发生变更，另一处就可能继续滞后，形成与 `ULevelStreaming::GetShouldBeVisibleInEditor()` 相同的双源漂移风险。当前测试只验证“入口存在”，不会约束绑定来源唯一，因此这类漂移很容易长期潜伏。 |

---

## 分析 (2026-04-08 03:54)

### 发现 40：`RuntimeFloatCurveMixinLibrary` 的目标类型扩展面已经失去绑定保障，但现有测试只验证 helper class 自身

| 项目 | 内容 |
|------|------|
| 维度 | D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/RuntimeFloatCurveMixinLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FunctionLibraryMixins.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` |
| 行号 | 16-94；1-30；1311-1314，1403-1406；781-787，829-835 |
| 描述 | `URuntimeFloatCurveMixinLibrary` 当前把原来的 `ScriptMixin = "FRuntimeFloatCurve UCurveFloat"` 注释掉后退化成了 `UCLASS(meta = ())`，库内却仍保留了 `GetFloatValue`、`GetTimeRange`、`GetNumKeys`、`AddDefaultKey`、`AddCurveKey` 等本应扩展 `FRuntimeFloatCurve` / `UCurveFloat` 的 helper。与此同时，全仓执行 `rg -n "FRuntimeFloatCurve|UCurveFloat|URuntimeFloatCurveMixinLibrary" Plugins/Angelscript/Source/AngelscriptRuntime/Binds` 对 bind 目录无命中，说明当前没有任何手写 bind 在 `FRuntimeFloatCurve` 或 `UCurveFloat` 上兜底这些方法。更关键的是，现有 `AngelscriptBindConfigTests.cpp` 781-787、829-835 行只验证 `URuntimeFloatCurveMixinLibrary::StaticClass()` 上的 `GetNumKeys` / `GetTimeRange` 进入了 `ClassFuncMaps`，完全没有验证脚本类型 `FRuntimeFloatCurve` 或 `UCurveFloat` 是否还能以实例方法形式拿到这些 helper。 |
| 根因 | 这组曲线 helper 在从 `ScriptMixin` 迁向当前绑定体系时，只保留了反射到 helper class 本身的路径，没有同步建立目标类型级别的显式绑定和覆盖测试。 |
| 影响 | 即使 `URuntimeFloatCurveMixinLibrary::GetNumKeys(...)` 这类静态入口仍然存在，也不能证明脚本作者还能继续写 `Curve.GetNumKeys()`、`Curve.GetTimeRange(...)` 或 `Curve.AddDefaultKey(...)`。当前测试把“helper class 反射成功”等同于“目标类型扩展仍可用”，会直接放过真正的 API 回退。 |

---

## 分析 (2026-04-08 03:55)

### 发现 41：`UAssetManager` 在当前分支丢掉了 `ScanPathForPrimaryAssets` 这条高频扫描 utility

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | High |
| 文件 | `../UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/FunctionLibraries/UAssetManagerMixinLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/UAssetManagerMixinLibrary.h` |
| 行号 | 79-81；6-80 |
| 描述 | UEAS2 版本的 `UAssetManagerMixinLibrary` 还提供 `ScanPathForPrimaryAssets(UAssetManager* AssetManager, FPrimaryAssetType PrimaryAssetType, const FString& Path, UClass* BaseClass, bool bHasBlueprintClasses, bool bIsEditorOnly = false, bool bForceSynchronousScan = true)`，直接转发到 `AssetManager->ScanPathForPrimaryAssets(...)`。当前分支的同名头文件在 `CallOrRegister_OnCompletedInitialScan(...)` 之后已经结束，没有任何 `ScanPathForPrimaryAssets` 声明。对当前仓库执行 `rg -n "ScanPathForPrimaryAssets\\(" Plugins/Angelscript/Source/AngelscriptRuntime` 与 `rg -n "ScanPathForPrimaryAssets\\(" Plugins/Angelscript/Source/AngelscriptTest` 都无命中，说明这条 API 既没有迁到别的运行时绑定，也没有测试覆盖。 |
| 根因 | `UAssetManager` helper 在迁移/裁剪过程中只保留了查询现有 primary asset 元数据的入口，没有把编辑器和启动时常用的主动扫描 utility 一并带过来。 |
| 影响 | 脚本侧失去了直接触发 primary asset 路径扫描的官方 helper，依赖动态扫描注册资产的工具链或编辑器脚本只能退回 C++、蓝图，或者自行重写包装层。相较 UEAS2，`UAssetManager` 的脚本 API 面已经从“可查询 + 可扫描”退化成只剩“可查询”。 |

---

## 分析 (2026-04-08 04:07)

### 发现 42：输入辅助库的 3 组 mixin 元数据全部被注释掉，`UInputComponent` / `APlayerController` / `UPlayerInput` 扩展方法整体退化成静态 helper

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/InputComponentScriptMixinLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UEnhancedInputComponent.cpp` |
| 行号 | 12-25，116-146，153-246；32-35 |
| 描述 | `UInputComponentScriptMixinLibrary`、`UPlayerControllerInputScriptMixinLibrary`、`UPlayerInputScriptMixinLibrary` 的类级 `ScriptMixin` 元数据都已经被注释掉，只剩 `UCLASS(Meta = ())`，但库内仍保留 `BindAction`、`BindKey`、`PushInputComponent`、`GetPlayerInput`、`AddActionMapping`、`GetKeysForAction`、`SetMouseSensitivity` 等整组 helper。对当前仓库执行 `rg -n "BindAction\\(|BindKey\\(|BindChord\\(|BindAxis\\(|BindAxisKey\\(|BindVectorAxis\\(|PushInputComponent\\(|PopInputComponent\\(|GetPlayerInput\\(|AddActionMapping\\(|RemoveActionMapping\\(|AddAxisMapping\\(|RemoveAxisMapping\\(|ForceRebuildingKeyMaps\\(|GetKeysForAction\\(|GetKeysForAxis\\(|GetEngineDefinedActionMappings\\(|GetEngineDefinedAxisMappings\\(|InvertAxis\\(|SetMouseSensitivity\\(|GetMouseSensitivityX\\(|GetMouseSensitivityY\\(" Plugins/Angelscript/Source/AngelscriptRuntime/Binds Plugins/Angelscript/Source/AngelscriptTest`，除 `Bind_UEnhancedInputComponent.cpp` 里 Enhanced Input 自己的 `BindAction` 外无任何手写兜底绑定或测试命中，说明这些 classic input helper 当前不会再作为目标类型实例方法出现。 |
| 根因 | 输入函数库从旧 `ScriptCallable + ScriptMixin` 暴露模式迁到 `BlueprintCallable` 时，只迁移了函数可见性，没有为 `UInputComponent` / `APlayerController` / `UPlayerInput` 建立新的显式实例绑定通路。 |
| 影响 | 脚本 API 面会出现明显不一致：Enhanced Input 仍可写成实例风格 `InputComponent.BindAction(...)`，但 classic input helper 只能退回 `UInputComponentScriptMixinLibrary::BindAction(Component, ...)`、`UPlayerInputScriptMixinLibrary::AddActionMapping(PlayerInput, ...)` 这类静态调用。`rg -n "BindAction\\(|PushInputComponent\\(|AddActionMapping\\(" Plugins/Angelscript/Source/AngelscriptTest` 只命中 Enhanced Input 路径，当前没有任何回归测试会在这些 classic input mixin 掉线时报警。 |

### 发现 43：`TSoftObjectPtr::LoadAsync` 删掉空引用防御后会把 null path 直接送进 `LoadPackageAsync`

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSoftObjectPtr.cpp`；`../UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_TSoftObjectPtr.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp` |
| 行号 | 483-527；486-538；462-485 |
| 描述 | 当前 `TSoftObjectPtr_.Method("void LoadAsync(FOnSoftObjectLoaded OnLoaded) const", ...)` 在进入异步加载前只检查 subtype 是否是 `AActor` / `UActorComponent`，随后无条件把 `ObjectCopy.ToString()` 转成 package path 并传给 `LoadPackageAsync(...)`。相比之下，UEAS2 同一位置在 489-495 行先判断 `Self->IsNull()`，对空 soft reference 直接 `Throw` 并返回。也就是说，当前分支已经删掉了对 null soft pointer 的唯一显式防御。 |
| 根因 | `Bind_TSoftObjectPtr.cpp` 在从“脚本对象类型显式作为首参”的旧绑定写法迁到当前 `GetCurrentFunctionObjectType()` 写法时，保留了 actor/component 限制和已加载短路分支，但漏掉了 `Self->IsNull()` 的提前退出。 |
| 影响 | 脚本一旦对空的 `TSoftObjectPtr` 调 `LoadAsync`，当前实现就会走到 `LoadPackageAsync(*FPackageName::ObjectPathToPackageName(ObjectCopy.ToString()), ...)` 这条无效路径，而不是得到可诊断的脚本异常。现有 `FAngelscriptSoftReferenceBindingCompileTest` 462-485 行只编译 `Get()`、`EditorOnlyLoadSynchronous()` 和 `TSoftClassPtr::Get()`，完全没有覆盖 `LoadAsync` 或 null soft reference 行为，因此这条回退现在不会被测试发现。 |

### 发现 44：`WorldCollision` 回调 delegate 只暴露 `uint64` handle，导致脚本无法保留 `FTraceHandle::bTransactional`

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/WorldCollisionStatics.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_WorldCollision.cpp`；`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Public/WorldCollision.h`；`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Private/Collision/WorldCollisionAsync.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp` |
| 行号 | 7-8；58-69，329-468；28-49，76-79；600-645；278-306 |
| 描述 | `WorldCollisionStatics.h` 把 `FScriptTraceDelegate` / `FScriptOverlapDelegate` 的第一个参数声明成了裸 `uint64 TraceHandle`。绑定实现里所有 async trace/overlap lambda 也都把原生 `const FTraceHandle& TraceHandle` 缩成 `TraceHandle._Handle` 传给脚本回调。虽然脚本侧提供了 `FTraceHandle(uint64 InHandle)` 构造函数，但这条构造只写入 `_Handle`，不会恢复 `FTraceHandle` 里的 `bTransactional`。而 UE 原生 `FTraceHandle` 在 `WorldCollision.h` 40-49、76-79 行明确把 `bTransactional` 作为状态位；`WorldCollisionAsync.cpp` 600-645 行的 `IsTraceHandleValid`、`QueryTraceData`、`QueryOverlapData` 又都依赖 `Handle.IsTransactional()` 选择普通缓冲区还是 transactional 缓冲区。 |
| 根因 | 为了让 dynamic delegate 参数更简单，`WorldCollisionStatics` 把异步回调 handle 从结构体 `FTraceHandle` 压扁成了 `uint64`，但没有同步暴露能无损恢复 `bTransactional` 的脚本侧载体。 |
| 影响 | 在 AutoRTFM transactional trace 场景里，脚本回调即使手工把 `uint64` 重新包成 `FTraceHandle(TraceHandle)`，得到的也是 `bTransactional=false` 的残缺 handle，后续再调用 `QueryTraceData` / `QueryOverlapData` / `IsTraceHandleValid` 就会查错缓冲区。现有 `FAngelscriptWorldCollisionBindingTest` 278-306 行只编译了一次 `System::AsyncOverlapByProfile(...)`，既没有给 delegate 传回调，也没有验证 callback handle 能继续参与查询，因此这条 API 断裂当前没有测试兜底。 |

### 发现 45：`Script::Get*GlobalVariableBeingInitialized` 直接读取第三方的进程级静态指针，没有任何 engine/module 隔离

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptScriptLibrary.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_module.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_module.cpp` |
| 行号 | 9-32；318；358，396-408 |
| 描述 | `UAngelscriptScriptLibrary` 的三个查询函数没有经过 `FAngelscriptEngine`、module handle 或当前 context，而是直接访问 `asCModule::InitializingGlobalProperty`。第三方源码里这个字段在 `as_module.h` 318 行被声明为 `static asCGlobalProperty*`，并在 `as_module.cpp` 358 行以进程级静态定义；全局变量初始化时只是通过 `TGuardValue ScopeGlobalProperty(InitializingGlobalProperty, desc)` 临时写入当前 `desc`。也就是说，这组 helper 读取的是 AngelScript 核心里的全局静态状态，而不是插件自己的 engine-local 状态。 |
| 根因 | `UAngelscriptScriptLibrary` 直接复用了 AngelScript 内核暴露的 `InitializingGlobalProperty` 静态变量来实现 introspection，没有为当前插件已经存在的多 engine / scope 体系建立隔离层。 |
| 影响 | 推断：在同一进程内存在多个 Angelscript engine 或多个 module 初始化路径时，这三个 helper 会共享同一根 `InitializingGlobalProperty` 指针，无法从 API 设计上保证“返回的是当前 engine / 当前 module 正在初始化的那个全局变量”。当前仓库对 `GetNameOfGlobalVariableBeingInitialized`、`GetNamespaceOfGlobalVariableBeingInitialized`、`GetModuleNameOfGlobalVariableBeingInitialized` 的测试扫描为 0 命中，因此没有回归用例能证明这组查询在 engine-isolation 场景下仍然可靠。 |

### 发现 46：`TSoftClassPtr::LoadAsync` 也缺少空引用前置检查，null class soft reference 会走同一条无效 package 加载路径

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSoftObjectPtr.cpp`；`../UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_TSoftObjectPtr.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp` |
| 行号 | 622-651；640-669；463-485 |
| 描述 | 当前 `TSoftClassPtr_.Method("void LoadAsync(FOnSoftClassLoaded OnLoaded) const", ...)` 在 `Self->Get()` 失败后，直接把 `ObjectCopy.ToString()` 经 `ObjectPathToPackageName(...)` 交给 `LoadPackageAsync(...)`。UEAS2 同一实现也是这条路径，说明这个空 soft class reference 边界从旧版开始就没有被防守。当前仓库里没有任何在 `LoadAsync` 前检查 `Self->IsNull()` 或返回诊断错误的代码。 |
| 根因 | `TSoftClassPtr` 的异步加载 helper 一直沿用“已加载则立即回调，否则直接按路径请求包加载”的最短路径实现，没有把 null soft reference 当作需要显式拒绝的脚本输入。 |
| 影响 | 只要脚本对空的 `TSoftClassPtr` 调 `LoadAsync`，就会和 `TSoftObjectPtr::LoadAsync` 一样走到无效 package path 的加载分支。现有 `FAngelscriptSoftReferenceBindingCompileTest` 463-485 行只验证 `TSoftClassPtr::Get()` 能编译，完全没有编译或执行 `LoadAsync`，因此这个长期存在的空句柄边界缺口当前也没有测试覆盖。 |

---

## 分析 (2026-04-08 04:18)

### 发现 47：`USceneComponent` 的高频 transform helper 面只保留“无 sweep / 无 hit result”简化签名，和 UE 原生碰撞语义脱节

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptComponentLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_USceneComponent.cpp`；`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Components/SceneComponent.h`；`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp` |
| 行号 | 35-219；131-135；427-428，440-455，481-540，554-638，1221-1244；118-129 |
| 描述 | `UAngelscriptComponentLibrary` 里 `SetRelativeLocation`、`SetRelativeRotation`、`SetRelativeTransform`、`SetRelativeLocationAndRotation`、`AddRelativeLocation`、`AddRelativeRotation`、`AddLocalOffset`、`AddLocalRotation`、`AddLocalTransform`、`SetWorldLocation`、`SetWorldRotation`、`SetWorldTransform`、`SetWorldLocationAndRotation`、`AddWorldOffset`、`AddWorldRotation`、`AddWorldTransform` 全都只暴露了最短参数版本，直接转发到 `Set*` / `Add*` C++ 方法。相比之下，UE 原生 `K2_*` 版本同名 API 普遍带 `bSweep`、`SweepHitResult`、`bTeleport`，允许脚本在移动组件时拿到碰撞命中信息并显式控制 teleport 语义。当前绑定层 `Bind_USceneComponent.cpp` 131-135 行甚至还手写补了一条只收 `FVector NewLocation` 的 `SetRelativeLocation` 简化方法；现有测试 118-129 行也只编译这种一参调用，没有任何覆盖 sweep 或 hit-result 路径。 |
| 根因 | FunctionLibrary 和手写 bind 都优先提供“少参数、好调用”的糖衣 overload，但没有同步给这组高频移动 API 补齐与 UE Blueprint/K2 对齐的碰撞语义入口或测试。 |
| 影响 | 脚本作者如果按当前 helper 面直接写 `Component.SetWorldLocation(...)` / `AddWorldOffset(...)`，拿不到 `SweepHitResult`，也无法从同名简化入口里显式选择 sweep/teleport 行为；这会把 UE 原生依赖碰撞反馈的组件移动工作流切成“脚本便捷版”和“原生完整版”两套不一致接口，而当前测试只锁住了缩水版。 |

### 发现 48：`AttachToComponent` helper 把 UE 原生 attach 语义压扁成单一策略，固定丢弃 `ScaleRule`、`bWeldSimulatedBodies` 和返回值

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptComponentLibrary.h`；`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Components/SceneComponent.h` |
| 行号 | 223-236；750-760 |
| 描述 | `UAngelscriptComponentLibrary::AttachToComponent(...)` 只接受一个 `EAttachmentRule AttachmentRule`，随后无条件调用 `Component->K2_AttachToComponent(Parent, SocketName, AttachmentRule, AttachmentRule, EAttachmentRule::KeepWorld, false)`。这意味着脚本 helper 永远把 `ScaleRule` 固定成 `KeepWorld`，把 `bWeldSimulatedBodies` 固定成 `false`，同时还把原生 `K2_AttachToComponent(...)` 的 `bool` 返回值抹成了 `void`。UE 原生声明明确把 `LocationRule`、`RotationRule`、`ScaleRule`、`bWeldSimulatedBodies` 都视为可选输入，并通过返回值报告 attach 是否成功。 |
| 根因 | 该 helper 为了追求单参数的简化调用，把原生 attach 规则结构压平成“位置/旋转同规则 + 缩放永远 KeepWorld + 不焊接”的硬编码策略，但没有在 API 名称或文档上显式声明这只是受限版本。 |
| 影响 | 脚本侧如果想做“snap location/rotation 但也 snap scale”或“附着时焊接物理体”的常见工作流，当前 helper 入口无法表达；更糟的是 attach 失败时也没有返回值可供判断，只能靠后续状态查询间接发现。`rg -n "AttachToComponent\\(" Plugins/Angelscript/Source/AngelscriptTest` 无命中，当前没有任何测试覆盖这条受限 attach 语义。 |

### 发现 49：`RuntimeFloatCurve` 的两个 key-mutation helper 对无效 `FCurveKeyHandle` 直接静默吞掉，脚本拿不到任何失败信号

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/RuntimeFloatCurveMixinLibrary.h` |
| 行号 | 149-168 |
| 描述 | `SetKeyUserTangents(...)` 和 `SetKeyUserTangentWeights(...)` 在进入修改逻辑前都会先检查 `Curve->FloatCurve.IsKeyHandleValid(KeyHandle.KeyHandle)`，但检查失败后直接 `return`。这两个 helper 返回类型都是 `void`，文件内也没有 `ensure`、`FAngelscriptEngine::Throw(...)`、日志或布尔结果来告诉脚本调用方“这次修改实际上没有生效”。 |
| 根因 | 曲线编辑 helper 沿用了“输入无效就 quietly bail out”的 C++ 工具式写法，但没有把脚本环境更依赖显式诊断这一点纳入设计。 |
| 影响 | 只要脚本保存了过期的 `FCurveKeyHandle`，或者把来自另一条曲线的 handle 误传进来，当前 API 就会无声失败，后续曲线仍保持旧切线值，调用方只能靠额外读取曲线数据或人工调试发现问题。`rg -n "SetKeyUserTangents|SetKeyUserTangentWeights|FCurveKeyHandle" Plugins/Angelscript/Source/AngelscriptTest` 无命中，当前没有测试覆盖这两条无效 handle 路径。 |

### 发现 50：`Script::GetNamespaceOfGlobalVariableBeingInitialized` 用空字符串同时表示“无变量正在初始化”和“变量位于全局命名空间”，返回值语义天然歧义

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptScriptLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptScriptLibrary.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_scriptengine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_namespace.h` |
| 行号 | 21-27；16-23；816-817；39-45 |
| 描述 | `GetNamespaceOfGlobalVariableBeingInitialized()` 的文档写着“如果当前没有正在初始化的全局变量，则返回空字符串”，实现里也确实在 `InitializingGlobalProperty == nullptr` 或 `nameSpace == nullptr` 时返回 `TEXT(\"\")`。但 AngelScript 内核在 `as_scriptengine.cpp` 816-817 行创建的默认全局命名空间本身就是 `AddNameSpace(\"\")`，`asSNameSpace` 又直接把名字存成 `name` 字符串并通过 `GetName()` 返回。因此，一个合法的“全局命名空间中的变量”同样会得到空字符串结果。 |
| 根因 | 该 API 选用了 `""` 作为“无当前变量”的 sentinel，却没有考虑 AngelScript 默认命名空间的合法值本来就是空字符串。 |
| 影响 | 脚本侧无法仅凭返回值区分“当前没有全局变量正在初始化”和“当前正在初始化的变量位于 global namespace”这两种状态；任何依赖命名空间字符串做分支或日志归类的逻辑都会把这两种情况混为一谈。`rg -n "GetNamespaceOfGlobalVariableBeingInitialized" Plugins/Angelscript/Source/AngelscriptTest` 无命中，当前没有回归测试约束这条接口契约。 |

### 发现 51：`FQualifiedFrameTime` 的 FunctionLibrary 只补了 `AsSeconds()`，却漏掉帧率转换和 timecode 这两组高频 utility

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptFrameTimeMixinLibrary.h`；`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Core/Public/Misc/QualifiedFrameTime.h` |
| 行号 | 7-17；40-77 |
| 描述 | 当前 `UAngelscriptFrameTimeMixinLibrary` 对 `FQualifiedFrameTime` 只额外暴露了一个 `AsSeconds()` helper。相比之下，UE 原生 `FQualifiedFrameTime` 紧邻的同一组实用方法还包括 `ConvertTo(FFrameRate DesiredRate)` 以及两种 `ToTimecode(...)` overload，用于 Sequencer、Timecode 和跨帧率换算场景。对当前仓库执行 `rg -n "FQualifiedFrameTime|ConvertTo\\(|ToTimecode\\(" Plugins/Angelscript/Source/AngelscriptRuntime/Binds Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries Plugins/Angelscript/Source/AngelscriptTest`，除 `AsSeconds()` 外没有任何运行时绑定或测试命中。 |
| 根因 | `AngelscriptFrameTimeMixinLibrary` 目前只做了最小化补口，没有继续把 `FQualifiedFrameTime` 最常和 `AsSeconds()` 一起使用的帧率转换 / timecode 输出工具补齐到脚本层。 |
| 影响 | 脚本处理 frame-accurate 时间时，只能先拿 `Time` / `Rate` 自己拼 `FFrameRate::TransformTime(...)` 或 `FTimecode::FromFrameTime(...)`，失去本应由 mixin 直接提供的高频 utility；这和当前库名、`ScriptMixin = "FQualifiedFrameTime"` 传达的“补齐 frame-time 工作流”预期并不一致。 |

---

## 分析 (2026-04-08 10:34)

### 发现 52：`AddSmartAutoCurveKey` 已从 `SmartAuto` 语义回退成普通 `Auto`，与 `UEAS2` 参考实现不一致

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/RuntimeFloatCurveMixinLibrary.h`；`../UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/FunctionLibraries/RuntimeFloatCurveMixinLibrary.h`；`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` |
| 行号 | 190-205；168-182；781-839 |
| 描述 | 当前分支里 `AddAutoCurveKey` 和 `AddSmartAutoCurveKey` 都把新 key 的 tangent mode 设成 `ERichCurveTangentMode::RCTM_Auto`。但 `UEAS2` 参考实现中，`AddSmartAutoCurveKey` 177-182 行明确使用的是 `ERichCurveTangentMode::RCTM_SmartAuto`，只有 `AddAutoCurveKey` 才使用 `RCTM_Auto`。这说明当前不是单纯“多了个同义别名”，而是把原本应当保留的 `SmartAuto` 语义实打实回退掉了。现有测试 781-839 行仍只验证 `GetNumKeys` / `GetTimeRange` 的 direct bind，可见性，不覆盖任何 key tangent mode。 |
| 根因 | `RuntimeFloatCurveMixinLibrary` 在从 `ScriptCallable` 迁到 `BlueprintCallable` 的整理过程中，`AddSmartAutoCurveKey` 的实现被改写或复制时没有保住参考实现中的 `RCTM_SmartAuto` 分支。 |
| 影响 | 脚本侧即使显式选择 `AddSmartAutoCurveKey`，也再也拿不到 `SmartAuto` 曲线 key，只会得到和 `AddAutoCurveKey` 一样的 `Auto` 行为。这会让依赖 smart tangent 平滑策略的曲线编辑逻辑产生静默形状漂移，而且当前测试不会报警。 |

### 发现 53：`AActor` 在当前分支丢掉了整组 attached/overlap 便捷 helper，只剩更底层的 component 枚举绑定

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptActorLibrary.h`；`../UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/FunctionLibraries/AngelscriptActorLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp` |
| 行号 | 6-225；188-225；39-142；154-161 |
| 描述 | `UEAS2` 版本的 `UAngelscriptActorLibrary` 在 188-225 行还提供了 `GetComponents()`、`GetAttachedActors()`、`GetAttachedActorsOfClass()`、`GetOverlappingActorsOfClass()` 这组高频 actor utility。当前分支同名头文件 6-225 行已经结束于 `RerunConstructionScripts()`，这四条 helper 在文件内完全消失。对当前仓库执行 `rg -n "GetAttachedActorsOfClass|GetAttachedActors\\(|GetOverlappingActorsOfClass\\(" Plugins/Angelscript/Source/AngelscriptRuntime/Binds Plugins/Angelscript/Source/AngelscriptTest` 无命中，说明它们既没有迁到手写 bind，也没有测试覆盖；现有 `Bind_AActor.cpp` 39-142 行只剩 `GetComponentsByClass(?& OutComponents)` 这种更底层、带脚本类型检查的 out-param 枚举接口。 |
| 根因 | `AActor` 辅助库在当前分支的裁剪重点落在 transform / attachment wrapper，附带把 UEAS2 已经存在的一批“直接拿结果数组”的 actor utility 一并删掉了，但没有补任何等价替代入口。 |
| 影响 | 脚本作者不能再直接写 `Actor.GetAttachedActors()`、`Actor.GetAttachedActorsOfClass(...)` 或 `Actor.GetOverlappingActorsOfClass(...)` 这类高层查询；想拿组件集合也只能退回更笨重的 `GetComponentsByClass(?& OutComponents)` 模式。现有原生绑定测试 154-161 行只覆盖了 `GetComponentsByClass`，不会在 attached/overlap utility 缺失时报警。 |

### 发现 54：`Math` 库当前整批缺失 `delta/relative` 旋转与变换 utility，`UEAS2` 已提供的工作流被直接裁掉

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptMathLibrary.h`；`../UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/FunctionLibraries/AngelscriptMathLibrary.h`；`Plugins/Angelscript/Source/AngelscriptTest` |
| 行号 | 514-800；869-1107；全目录检索 |
| 描述 | 当前 `AngelscriptMathLibrary.h` 里 `UAngelscriptFRotatorLibrary`、`UAngelscriptFQuatLibrary`、`UAngelscriptFTransformLibrary` 仍然存在，但对当前仓库执行 `rg -n "GetDelta\\(|ApplyDelta\\(|GetRelative\\(|ApplyRelative\\(|MakeDeltaRotationFromAngularVelocity|MakeAngularVelocityFromDeltaRotation" Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptMathLibrary.h Plugins/Angelscript/Source/AngelscriptRuntime/Binds Plugins/Angelscript/Source/AngelscriptTest` 无命中。相比之下，`UEAS2` 参考实现 869-1107 行明确提供了 `FQuat::GetDelta/ApplyDelta/GetRelative/ApplyRelative`、`MakeDeltaRotationFromAngularVelocity`、`MakeAngularVelocityFromDeltaRotation`，以及 `FRotator` / `FTransform` 对应的 `GetDelta/ApplyDelta/GetRelative/ApplyRelative`。也就是说，这批围绕“世界空间 <-> 相对空间”“角速度 <-> delta rotation”转换的基础 utility 在当前分支已经被整组移除，而不是换了绑定位置。 |
| 根因 | `Math` FunctionLibrary 在当前分支的保留重点偏向轴向构造、基础投影和少量 transform helper，但没有继续同步 `UEAS2` 已经补齐的 delta/relative 工作流函数族。 |
| 影响 | 脚本做常见的“由父子旋转求相对旋转”“由前后 transform 求 delta transform”“由角速度生成一帧 delta quaternion”时，不能再直接调用库函数，只能手写 quaternion / transform 组合逻辑。这不仅增加重复实现，也更容易引入乘法顺序错误，而当前测试对这整组缺口完全失明。 |

### 发现 55：`UAssetManager.LoadPrimaryAsset*` 遇到无效 `FPrimaryAssetId` 时会直接返回，完成/取消回调都不会触发

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UAssetManager.cpp`；`Plugins/Angelscript/Source/AngelscriptTest` |
| 行号 | 49-79，93-104；全目录检索 |
| 描述 | `AssetManager_LoadPrimaryAssets(...)` 在 54 行先用 `ensureMsgf(Asset.IsValid(), TEXT("Tried to load invalid asset!"))` 检查输入，只要数组里有一个无效 `FPrimaryAssetId` 就把 `bShouldLoad` 置 false 并直接 `return`。这个早退发生在 65-79 行绑定 `CompleteDelegate`、调用 `AssetManager->LoadPrimaryAssets(...)` 和注册 `BindCancelDelegate(...)` 之前，因此脚本即使传了 `OptionalFinishedCallbackFunctionName` / `OptionalCanceledCallbackName`，这两个回调也根本没有机会被触发。更讽刺的是，同一函数 69 行的注释还明确写着“complete delegate 可能在 `LoadPrimaryAssets` 发现没活可干时立即调用”，但当前 invalid-id 分支连这条引擎兜底路径都绕开了。对测试目录执行 `rg -n "LoadPrimaryAsset\\(|LoadPrimaryAssets\\(" Plugins/Angelscript/Source/AngelscriptTest` 无命中，当前没有任何回归用例覆盖这条失败路径。 |
| 根因 | 这层脚本包装为了提前拦截无效 asset id，自行在进入引擎 API 前做了整组输入校验，但没有同时设计与成功路径对齐的失败回调或脚本异常。 |
| 影响 | 脚本如果基于完成回调推进状态机、释放 loading UI 或发起后续链式加载，只要资产 ID 拼错或缓存里混入无效条目，就会落入“记录 ensure 后静默返回”的挂死状态：没有 handle、没有完成通知、也没有取消通知。 |

### 发现 56：`GameplayTagContainer::RemoveTags` 的 wrapper 把待删除容器退化成按值传递，平白引入整份拷贝

| 项目 | 内容 |
|------|------|
| 维度 | E |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/GameplayTagContainerMixinLibrary.h`；`../UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/FunctionLibraries/GameplayTagContainerMixinLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FGameplayTag.cpp`；`Plugins/Angelscript/Source/AngelscriptTest` |
| 行号 | 48-51；48-51；149-172；全目录检索 |
| 描述 | 当前 `UGameplayTagContainerMixinLibrary::RemoveTags(...)` 的第二个参数签名是 `FGameplayTagContainer TagsToRemove`，也就是按值接收整份容器，然后再调用 `GameplayTagContainer.RemoveTags(TagsToRemove)`。`UEAS2` 参考实现同一位置使用的是 `const FGameplayTagContainer& TagsToRemove`，不会额外复制。对当前仓库执行 `rg -n "RemoveTags\\(" Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FGameplayTag.cpp Plugins/Angelscript/Source/AngelscriptTest` 无命中，说明既没有手写绑定替换这条签名，也没有测试覆盖它的成本或行为。 |
| 根因 | `GameplayTagContainer` wrapper 在迁移过程中把原本的 `const&` 参数签名退化成了 by-value 版本，但函数体仍只是简单转发给底层容器方法。 |
| 影响 | 只要脚本用这条 helper 批量移除 tag，调用前就要先复制一整份 `TagsToRemove`。在 tag 数量较大或高频调用的 gameplay loop 中，这会引入纯粹没有业务价值的内存拷贝和分配成本，而当前 API 也没有任何理由要求调用者为此买单。 |

---

## 分析 (2026-04-08 10:52)

### 发现 57：`InputComponent` 三个 helper 库已经不再扩展到 `UInputComponent` / `APlayerController` / `UPlayerInput`

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/InputComponentScriptMixinLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_APlayerController.cpp` |
| 行号 | 8-29，113-149，153-248；5-10 |
| 描述 | `InputComponentScriptMixinLibrary.h` 的三段类注释仍然分别写着 “ScriptMixin library to bind functions on UInputComponent / APlayerController / UPlayerInput”，但类级 `ScriptMixin` 元数据都被注释掉，只剩 `UCLASS(Meta = ())`。文件里仍声明了 `BindAction`、`BindKey`、`PushInputComponent`、`GetPlayerInput`、`AddActionMapping`、`GetKeysForAction` 等整套 helper，可当前手写绑定侧只有 `Bind_APlayerController.cpp` 7-10 行给 `APlayerController` 补了 `SetPlayer` 和 `GetLocalPlayer` 两个方法。对运行时绑定目录执行 `rg -n 'ExistingClass\("UInputComponent"|ExistingClass\("UPlayerInput"|PushInputComponent|AddActionMapping|BindAction\(' Plugins/Angelscript/Source/AngelscriptRuntime/Binds` 为 0 命中，说明这批 helper 没有任何替代性 hand-written bind。 |
| 根因 | 这组输入 helper 在从旧 `ScriptCallable + ScriptMixin` 迁到当前 `BlueprintCallable` 方案时，只保留了 wrapper 函数体，没有同步保留 mixin 元数据或补实例方法绑定。 |
| 影响 | 脚本 API 面会出现明显断层：源码里仍保留 `BindAction` / `PushInputComponent` / `AddActionMapping` 等高频输入 helper，但它们已经不能再作为 `InputComponent.BindAction(...)`、`PlayerController.PushInputComponent(...)`、`PlayerInput.AddActionMapping(...)` 这类扩展方法出现。对测试目录执行 `rg -n '\.BindAction\(|\.PushInputComponent\(|\.AddActionMapping\(' Plugins/Angelscript/Source/AngelscriptTest` 同样为 0 命中，当前没有任何回归用例会在这条绑定断层出现时报警。 |

### 发现 58：`FHitResult` 的 helper 面在当前分支回退成“只剩字段”，`GetActor` / `Reset` / `SetPhysMaterial` 等实例辅助函数已经失联

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptHitResultLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FHitResult.cpp`；`../UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/FunctionLibraries/AngelscriptHitResultLibrary.h`；`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp` |
| 行号 | 8-91；8-41；8-65；573-622 |
| 描述 | 当前 `UAngelscriptHitResultLibrary` 已把类级 `ScriptMixin = "FHitResult"` 注释掉，但仍保留 `SetComponent`、`SetActor`、`Reset`、`GetComponent`、`GetActor`、`GetPhysMaterial`、`SetbBlockingHit`、`SetbStartPenetrating` 等 helper。与此同时，`Bind_FHitResult.cpp` 8-41 行只注册了两个构造函数和一组 property，没有任何 `Method(...)` 补绑定。对照 `UEAS2` 可见，这组 helper 原本是以 `ScriptMixin = "FHitResult"` 形式直接挂到 `FHitResult` 上的，而且旧实现还提供了当前已经消失的 `SetPhysMaterial(FHitResult&, UPhysicalMaterial*)` 和更完整的 `Reset(FHitResult&, float InTime = 1.f, bool bPreserveTraceData = true)`。 |
| 根因 | `FHitResult` 迁移时只保留了“属性可直接读写”的最低限度绑定，没有把旧 mixin helper 一并迁到 hand-written bind，也没有把 richer overload 留在当前 library 中。 |
| 影响 | 脚本现在只能把 `FHitResult` 当成裸 struct 操作，无法再通过实例 helper 直接写 `Hit.GetActor()`、`Hit.GetComponent()`、`Hit.Reset(0.5f, false)` 或设置 `PhysMaterial`。这既削弱了和 `UEAS2` 既有脚本面的兼容性，也让高频 trace/overlap 代码重新退回原始字段拼装。现有 parity 测试 587-622 行只验证字段读写与构造函数，不覆盖任何 helper，可见性回退不会被当前测试捕获。 |

### 发现 59：`FHitResult` 绑定把整段脚本文档元数据删掉了，trace 结果字段不再有内联说明

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FHitResult.cpp`；`../UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_FHitResult.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp` |
| 行号 | 1-41；1-57；573-622 |
| 描述 | 当前 `Bind_FHitResult.cpp` 不再包含 `AngelscriptDocs.h`，并且在注册 `FaceIndex`、`PenetrationDepth`、`Time`、`ImpactPoint`、`Location`、`Normal`、`BoneName`、`MyBoneName` 等 property 后，没有任何一条 `SCRIPT_PROPERTY_DOCUMENTATION(...)`。对照 `UEAS2` 的同名 bind 文件，27-57 行曾为这些字段逐一补了含语义解释的文档字符串，例如 `ImpactPoint` 与 `Location` 的区别、`Time` 的 sweep 语义、`Normal` 与 `ImpactNormal` 的关系。 |
| 根因 | 当前分支在精简 `FHitResult` 绑定时保留了最小注册逻辑，但把文档层依赖和对应的 property documentation 调用整段裁掉了。 |
| 影响 | `FHitResult` 是 trace / overlap / hit event 中的高频基础类型，当前脚本侧虽然还能访问字段，但失去了最关键的内联说明，调用者无法在文档/hover 层直接分辨 `ImpactPoint` vs `Location`、`Normal` vs `ImpactNormal`、`Time` 的取值语义。现有 parity 测试只要求示例脚本编译执行成功，不验证文档元数据是否存在，因此这条文档覆盖回退目前没有任何自动化护栏。 |

### 发现 60：`SubsystemLibrary` 没有跟上 UE 原生 `USubsystemBlueprintLibrary` 的 `GetAudioEngineSubsystem`

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/SubsystemLibrary.h`；`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Public/Subsystems/SubsystemBlueprintLibrary.h`；`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Private/Subsystems/SubsystemBlueprintLibrary.cpp` |
| 行号 | 16-65；21-39；86-95 |
| 描述 | 当前 `USubsystemLibrary` 只暴露了 `GetEngineSubsystem`、`GetGameInstanceSubsystem`、`GetLocalPlayerSubsystem`、`GetWorldSubsystem`、`GetLocalPlayerSubsystemFromPlayerController`、`GetLocalPlayerSubsystemFromLocalPlayer` 六个入口。UE 当前原生 `USubsystemBlueprintLibrary` 除了这几项外，还在头文件 37-39 行和实现 86-95 行提供了 `GetAudioEngineSubsystem(UObject* ContextObject, TSubclassOf<UAudioEngineSubsystem> Class)`，用于按 `World -> AudioDevice` 获取 audio device 级 subsystem。对当前仓库执行 `rg -n 'GetAudioEngineSubsystem' Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries Plugins/Angelscript/Source/AngelscriptRuntime/Binds Plugins/Angelscript/Source/AngelscriptTest` 为 0 命中，说明该入口既没有 wrapper，也没有 hand-written bind 或测试。 |
| 根因 | `SubsystemLibrary` 看起来仍停留在较早一版 `USubsystemBlueprintLibrary` 的子集镜像，没有随着当前引擎版本把 audio engine subsystem 访问器一起同步进脚本层。 |
| 影响 | 脚本无法像 UE 原生 Blueprint/C++ 一样直接通过上下文对象获取 `UAudioEngineSubsystem`，任何 audio device 级功能都必须退回 C++ 或额外自定义绑定。这让 `SubsystemLibrary` 在“把 blueprint internal API 补给 Angelscript”这个目标上出现了可验证的缺口，而测试目录对这些 getter 全部为 0 命中，当前没有回归覆盖。 |

### 发现 61：`UAssetManager` 之前已有的 `ScanPathForPrimaryAssets` helper 在当前分支被直接删掉了

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/UAssetManagerMixinLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UAssetManager.cpp`；`../UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/FunctionLibraries/UAssetManagerMixinLibrary.h` |
| 行号 | 45-84；88-104；69-82 |
| 描述 | 当前 `UAssetManagerMixinLibrary.h` 在 `CallOrRegister_OnCompletedInitialScan` 之后直接结束，`Bind_UAssetManager.cpp` 88-104 行也只补了 `GetPrimaryAssetIdForPath`、`GetPrimaryAssetPath`、`GetPrimaryAssetIdForData`、`UnloadPrimaryAsset(s)`、`LoadPrimaryAsset(s)` 这些入口。对照 `UEAS2`，同名 FunctionLibrary 在 76-82 行还提供了 `ScanPathForPrimaryAssets(...)`，直接转发 `AssetManager->ScanPathForPrimaryAssets(...)`。对当前仓库执行 `rg -n 'ScanPathForPrimaryAssets' Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries Plugins/Angelscript/Source/AngelscriptRuntime/Binds Plugins/Angelscript/Source/AngelscriptTest` 为 0 命中，说明这条 helper 并不是迁移了位置，而是整个脚本入口消失。 |
| 根因 | `UAssetManager` 绑定在当前分支只保留了查询/加载/卸载的子集，没有把参考实现里已经存在的路径扫描 helper 一起迁过来。 |
| 影响 | 脚本侧失去了直接触发 primary asset path 扫描的入口，涉及 asset registry 预热、editor 下动态发现 primary asset、或工具链脚本补扫路径时，只能退回 C++。这与 `UAssetManagerMixinLibrary` 作为资产管理辅助库的职责不一致，而且测试目录对该 helper 为 0 命中，当前不会在这条 utility 缺失时报警。 |

---

## 分析 (2026-04-08 11:00)

### 发现 62：`GetNumChildrenComponents` 同时由 `FunctionLibrary` 和手写 `USceneComponent` bind 暴露，形成重复绑定源

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptComponentLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_USceneComponent.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp` |
| 行号 | 7；99-101；1403-1406；26-26；130-130 |
| 描述 | `UAngelscriptComponentLibrary` 仍带 `ScriptMixin = "USceneComponent"`，并在 99-101 行新增了 `BlueprintCallable` 的 `GetNumChildrenComponents(const USceneComponent*)` wrapper；`Bind_BlueprintType.cpp` 1403-1406 行会把这类 `BlueprintCallable` mixin 自动当成实例方法绑到目标类型上。与此同时，`Bind_USceneComponent.cpp` 26 行早已手写注册了 `USceneComponent_.Method("int32 GetNumChildrenComponents() const", METHOD_TRIVIAL(...))`。也就是说，当前分支把同一个 `USceneComponent` 方法同时交给了自动 FunctionLibrary 绑定和 hand-written native bind 两套机制。现有原生绑定测试 130 行只验证 `GetNumChildrenComponents()` 可调用，不会区分它来自哪一条绑定链。 |
| 根因 | 组件库在补齐 `USceneComponent` helper 时新增了一个本来已经由 hand-written bind 负责的 trivial wrapper，但没有像 `GetSocketQuaternion` 那样通过 `NotInAngelscript` 或删除旧入口来消除重复。 |
| 影响 | `USceneComponent::GetNumChildrenComponents` 进入了双源维护状态：后续一旦只修改其中一处的签名、元数据、文档或空指针策略，两条绑定路径就会继续漂移，而且现有测试只能证明“有一个版本可用”，无法对重复绑定导致的覆盖顺序或行为分叉报警。 |

### 发现 63：`USceneComponent` 的 quat rotation setter overload 丢掉了 `NotAngelscriptProperty`，会被重新当成 property accessor

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptComponentLibrary.h`；`../UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/FunctionLibraries/AngelscriptComponentLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`；`Plugins/Angelscript/Source/AngelscriptTest` |
| 行号 | 49-52；167-170；52-53；148-149；425-428；260；417-440；全目录检索 |
| 描述 | 当前 `SetRelativeRotationQuat` 和 `SetWorldRotationQuat` 的真实声明分别是 `UFUNCTION(BlueprintCallable, Meta = (ScriptName = "SetRelativeRotation"))` 与 `UFUNCTION(BlueprintCallable, Meta = (ScriptName = "SetWorldRotation"))`，注释里保留的旧声明才带 `NotAngelscriptProperty`。对照 `UEAS2` 参考实现，这两个 overload 原本都显式标记了 `Meta = (..., NotAngelscriptProperty)`。绑定层在 `AngelscriptBinds.cpp` 425-428 行会先把所有 C++ 绑定函数默认设成 property accessor，而 `Helper_FunctionSignature.h` 260 行和 438-440 行只有检测到 `NotAngelscriptProperty` 才会调用 `SetProperty(false)` 关闭这一行为。由于当前元数据已丢失，这两个 quat overload 会重新以 property accessor 语义进入脚本 API，而不是仅作为普通方法。对测试目录执行 `rg -n 'SetRelativeRotation\\(|SetWorldRotation\\(' Plugins/Angelscript/Source/AngelscriptTest` 为 0 命中，当前没有用例覆盖这种属性/方法形态差异。 |
| 根因 | `USceneComponent` helper 从 `ScriptCallable` 迁到 `BlueprintCallable` 时，只保留了 `ScriptName` 用来和 `FRotator` overload 共用名字，但漏掉了旧实现专门用于关闭 property 化的 `NotAngelscriptProperty`。 |
| 影响 | 脚本侧的 `SetRelativeRotation` / `SetWorldRotation` quat overload 不再是单纯的重载方法，而会被绑定系统视为 property accessor 的候选入口。这会让同名 `FRotator`/`FQuat` setter 的可见形态和自动补全行为变得不稳定，也让后续任何依赖 property 规则的诊断、重写或文档生成更难保持一致。 |

### 发现 64：`UAngelscriptComponentLibrary` 的高频 `USceneComponent` helper 普遍缺少空指针防御，和现有 hand-written bind 的容错策略不一致

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptComponentLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_USceneComponent.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp` |
| 行号 | 16-39；65-74；155-157；183-192；225-235；271-280；83-85；114-116；118-120 |
| 描述 | `UAngelscriptComponentLibrary` 中的 `GetRelativeLocation`、`GetRelativeRotation`、`GetRelativeScale3D`、`SetRelativeLocation`、`SetRelativeLocationAndRotation*`、`SetWorldLocation`、`SetWorldLocationAndRotation*`、`AttachToComponent`、`GetBounds`、`GetShapeCenter` 等高频 helper 都直接解引用 `Component`，文件内没有任何 `nullptr` 检查或失败路径。对比同一目标类型的 hand-written `Bind_USceneComponent.cpp`，`GetChildrenComponentsByClass` 在 83-85 行会对空 `ParentComp` 主动 `Throw("Scene component was null.")`，`GetComponentTransform` 在 114-116 行会对空 `Component` 返回 `FTransform::Identity`。也就是说，当前 `USceneComponent` API 面内部已经存在两套相互冲突的空指针策略：一部分入口有防御，一部分入口会直接崩。现有原生组件绑定测试只在 118-120 行覆盖了非空对象的 `SetRelativeLocation` / `GetRelativeLocation` happy path，没有任何空句柄回归。 |
| 根因 | 这组 FunctionLibrary wrapper 基本都是“一行转发原生方法”的迁移结果，但迁移时没有沿用 hand-written bind 已经建立的 null-guard/Throw 约定。 |
| 影响 | 脚本只要把空的 `USceneComponent` 句柄传给这些 helper，就会在 wrapper 内部直接解引用崩溃，而不是得到和同类 API 一致的脚本异常或默认值。这会让 `USceneComponent` 相关调用在运行时表现出难以预测的“有些入口可诊断、有些入口直接炸”的差异。 |

### 发现 65：`GetPrimaryAssetIdForObject` 在迁移后丢掉了 `ScriptNoDiscard`，资产 ID 查询结果可以被静默忽略

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Low |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/UAssetManagerMixinLibrary.h`；`../UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/FunctionLibraries/UAssetManagerMixinLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`；`Plugins/Angelscript/Source/AngelscriptTest` |
| 行号 | 40-42；35-38；328-329；全目录检索 |
| 描述 | 当前 `UAssetManagerMixinLibrary::GetPrimaryAssetIdForObject` 只剩 `UFUNCTION(BlueprintCallable)`，而 `UEAS2` 参考实现同一入口明确带有 `Meta = (ScriptNoDiscard)`。`Helper_FunctionSignature.h` 328-329 行显示，只有函数保留了这个元数据，最终脚本声明才会追加 `no_discard`。这意味着当前脚本侧可以直接调用 `GetPrimaryAssetIdForObject(...)` 却完全不消费返回值，而绑定层不会给出任何诊断。对测试目录执行 `rg -n 'GetPrimaryAssetIdForObject\\(' Plugins/Angelscript/Source/AngelscriptTest` 为 0 命中，当前没有测试约束这条 query API 的使用形态。 |
| 根因 | `UAssetManagerMixinLibrary` 从 `ScriptCallable` 迁到 `BlueprintCallable` 时，只保住了可见性，没有把旧实现里针对“返回值必须消费”的 `ScriptNoDiscard` 一起迁移。 |
| 影响 | 脚本在做 primary asset 识别时，容易把这条查询误当成“带副作用的检查函数”而静默丢掉 `FPrimaryAssetId` 结果，和 `UEAS2` 既有 API 契约不一致；一旦后续逻辑本应基于返回的 asset id 做缓存或加载链路，误用会更难靠静态检查暴露。 |

### 发现 66：`UAngelscriptActorLibrary` 当前真正自动绑定出来的仅有 `GetActorLocation` / `GetActorRotation`，而这两条本来就已由 hand-written native bind 提供

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptActorLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp` |
| 行号 | 5；75-77；94-96；1403-1406；32-33；44-45 |
| 描述 | 当前 `UAngelscriptActorLibrary` 仍是 `ScriptMixin = "AActor"`，但在整文件里真正带 `BlueprintCallable`、能被 `Bind_BlueprintType.cpp` 1403-1406 行自动收集到 `AActor` 的，只剩 `GetActorLocation` 和 `GetActorRotation`。然而这两个方法在 `Bind_AActor.cpp` 32-33 行早已通过 `METHOD_TRIVIAL(AActor, GetActorLocation/GetActorRotation)` 手写绑定到了 `AActor`。现有原生绑定测试 44-45 行验证的也正是这条 native bind 路径可用，而不是 `UAngelscriptActorLibrary` 提供了什么独有能力。 |
| 根因 | `AActor` FunctionLibrary 在迁移过程中只把已经由 native bind 覆盖的两个 getter 升成了 `BlueprintCallable`，却没有同步把真正新增价值的相对 transform / attach / editor helper 一起迁成可绑定状态。 |
| 影响 | 当前 `UAngelscriptActorLibrary` 对脚本 API 的实际贡献变成“新增了两条重复入口，却没有补上自己声明的高价值 helper”。这既制造了 `GetActorLocation` / `GetActorRotation` 的双源维护，又掩盖了 actor 库实质上几乎没有把独有功能成功暴露给脚本的问题。 |

---

## 分析 (2026-04-08 11:18)

### 发现 67：`UAssetManager` 现有绑定把 `LoadPrimaryAssetsWithType` 整条按类型批量加载入口删掉了

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UAssetManager.cpp`；`../UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_UAssetManager.cpp`；`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Engine/AssetManager.h`；`Plugins/Angelscript/Source/AngelscriptTest` |
| 行号 | 83-112；90-140；321-330；全目录检索 |
| 描述 | 当前 `Bind_UAssetManager.cpp` 只保留了 `LoadPrimaryAsset(...)` 和 `LoadPrimaryAssets(...)` 两个 wrapper；参考实现里 90-136 行的 `AssetManager_LoadPrimaryAssetWithType(...)` 和 132-136 行的 `UAssetManager_.Method("void LoadPrimaryAssetsWithType(...)")` 已完全消失。与此同时，UE 原生 `UAssetManager` 仍在 `AssetManager.h` 321-330 行保留 `LoadPrimaryAssetsWithType(...)` overload。对测试目录执行 `rg -n "LoadPrimaryAssetsWithType|UAssetManager" Plugins/Angelscript/Source/AngelscriptTest` 为 0 命中，说明这条退化没有任何回归覆盖。 |
| 根因 | `UAssetManager` 的 hand-written bind 在迁移过程中只保留了“按 `FPrimaryAssetId` 精确加载”的子集，没有把参考实现里已经存在、且 UE 原生仍支持的“按 `FPrimaryAssetType` 整批加载”路径一起迁回。 |
| 影响 | 脚本层失去了按资产类型直接触发批量加载的高层入口。涉及 cook 预热、按类型预加载或工具链脚本时，只能先自行枚举 `PrimaryAssetId` 列表再逐步转发到 `LoadPrimaryAssets(...)`，或者退回 C++。 |

### 发现 68：`UAssetManager` 命名空间下的安全获取入口被直接注释掉，脚本无法再判断 singleton 是否已就绪

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UAssetManager.cpp`；`../UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_UAssetManager.cpp`；`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Engine/AssetManager.h`；`Plugins/Angelscript/Source/AngelscriptTest` |
| 行号 | 107-110；137-140；91-102；全目录检索 |
| 描述 | 当前 `Bind_UAssetManager.cpp` 在 `FAngelscriptBinds::FNamespace ns("UAssetManager")` 块里把 `BindGlobalFunction("bool IsInitialized() no_discard", ...)` 和 `BindGlobalFunction("UAssetManager Get() no_discard", ...)` 整行注释掉了。参考实现同一位置 137-140 行仍显式导出这两个 helper，而 UE 原生 `AssetManager.h` 91-102 行也继续提供 `IsInitialized()` 与 `GetIfInitialized()`。对测试目录执行 `rg -n "GetIfInitialized|UAssetManager" Plugins/Angelscript/Source/AngelscriptTest` 为 0 命中，当前没有任何测试锁住这组静态入口。 |
| 根因 | 当前分支在整理 `UAssetManager` 绑定时，把 namespace 级 helper 当作可删除的旧接口处理，但没有提供新的脚本等价入口来替代“安全检查是否已初始化”与“拿到可空 singleton”这两个能力。 |
| 影响 | 脚本不能再写 `UAssetManager::IsInitialized()` 或 `UAssetManager::Get()` 这类守卫式调用来决定是否进入资产管理逻辑，只能依赖外部 C++ 注入现成实例，或者直接假定全局 singleton 已经可用。这会让启动阶段、测试环境和 editor 工具脚本里的防御性代码面收窄。 |

### 发现 69：`FPrimaryAssetId` 的类型安全构造器被删掉，当前只剩字符串解析入口且实现里还留着明显的类型拷贝错误

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UAssetManager.cpp`；`../UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_UAssetManager.cpp`；`J:/UnrealEngine/UERelease/Engine/Source/Runtime/CoreUObject/Public/UObject/PrimaryAssetId.h`；`Plugins/Angelscript/Source/AngelscriptTest` |
| 行号 | 29-37；30-44；124-156；全目录检索 |
| 描述 | 当前 `Bind_PrimaryAssetId` 只注册了 `Constructor("void f(const FString& InString)")`，并且 lambda 首参还误写成了 `FPrimaryAssetType* Address`。参考实现 34-43 行除了字符串构造外，还绑定了 `Constructor("void f(const FPrimaryAssetType& InType, const FName& InName)")`；UE 原生 `FPrimaryAssetId` 在 `PrimaryAssetId.h` 144-156 行同样保留了 `FPrimaryAssetId(FPrimaryAssetType InAssetType, FName InAssetName)` 与 `explicit FPrimaryAssetId(const FString& TypeAndName)` 两条构造路径。对测试目录执行 `rg -n "FPrimaryAssetId\\(const FPrimaryAssetType&|UAssetManager" Plugins/Angelscript/Source/AngelscriptTest` 为 0 命中，当前没有覆盖这条构造面。 |
| 根因 | `FPrimaryAssetId` 绑定在清理过程中丢掉了 pair ctor，只留下通过 `"Type:Name"` 字符串解析的构造方式；同时残存实现里又出现了把 `FPrimaryAssetId` 构造器首参写成 `FPrimaryAssetType*` 的拷贝错误，说明这段绑定缺少最基本的回归校验。 |
| 影响 | 脚本现在不能像原生类型那样直接用 `FPrimaryAssetType + FName` 组装 `FPrimaryAssetId`，只能先拼字符串再交给解析构造器，既失去类型安全，也引入额外格式耦合。那条首参类型写错的 lambda 还会继续增加后续维护和审查成本。 |

### 发现 70：`FTraceHandle` 绑定只暴露了最小字段，原生类型自带的 `Invalidate()` / `IsTransactional()` 都没有脚本入口

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_WorldCollision.cpp`；`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Public/WorldCollision.h`；`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp` |
| 行号 | 49-69；28-79；294-294 |
| 描述 | 当前 `Bind_WorldCollision.cpp` 对 `FTraceHandle` 只绑定了两个构造器、`opEquals`、`IsValid()`，以及 `_Handle` / `_FrameNumber` / `_Index` 三个原始字段。UE 原生 `FTraceHandle` 在 `WorldCollision.h` 40-79 行还明确包含 `bTransactional` 状态位、`Invalidate()` 和 `IsTransactional()` 两个实例方法。也就是说，脚本虽然拿到了 `FTraceHandle` 这个类型名，却没有完整拿到该类型最基本的生命周期和状态查询接口。现有 parity test 294 行只编译了一次 `System::AsyncOverlapByProfile(...)`，没有任何用例验证 `FTraceHandle` 自身的方法面。 |
| 根因 | `WorldCollision` 绑定把 `FTraceHandle` 当成近似 POD 的 carrier 暴露，只做了最小字段映射，没有把原生结构体自己定义的语义化 API 一起映射出来。 |
| 影响 | 脚本代码无法像 C++ 一样显式 `Invalidate()` 一个 handle，也无法可靠判断某个 handle 是否来自 transactional async trace。调用方只能直接篡改 `_Handle` 这类内部字段或完全跳过状态分支，API 既不完整，也和原生类型的封装层次不一致。 |

---

## 分析 (2026-04-08 11:32)

### 发现 71：`WrapIndexUInt` 的 unsigned 实现对 `Value < Min` 给出错误结果，注释承诺的区间语义被破坏

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptMathLibrary.h`；`Plugins/Angelscript/Source/AngelscriptTest` |
| 行号 | 319-333；全目录检索 |
| 描述 | `WrapIndexUInt` 注释声称“`Value` 小于 `Min` 时会 wrap 到 `Max` 以下”，但实现用的是 `uint32 ModValue = (Value - Min) % Range; if (ModValue >= 0) return Min + ModValue; else return Max + ModValue;`。由于 `ModValue` 是 `uint32`，`ModValue >= 0` 永远为真，`else` 分支是死代码。更关键的是，unsigned 下溢后 `(Value - Min) % Range` 并不能表达“负余数”。我执行了一个最小算例：按当前源码公式计算 `WrapIndexUInt(8, 10, 20)`，结果是 `14`；同一文件里 `int32` 版 `WrapIndex(8, 10, 20)` 返回 `18`，后者才符合注释里“wrap 到 `Max` 以下”的语义。测试目录对 `WrapIndexUInt` / `WrapIndex` 都是 0 命中。 |
| 根因 | 该实现直接把 `int32` 版本的“负余数走 `Max + ModValue`”逻辑复制到了 `uint32`，却没有考虑 unsigned 算术不会产生负数，导致分支条件与取模语义同时失效。 |
| 影响 | 即使后续补回了这条 helper 的脚本可见性，`Value < Min` 的 unsigned index wrapping 仍会返回错误索引；任何依赖环形 buffer、分页索引或 unsigned slot id 的代码都会得到偏小结果，而不是注释承诺的“贴近 `Max`”行为。 |

### 发现 72：核心 `Math` 纯返回值 helper 在迁移后批量丢失 `ScriptNoDiscard`，静态误用不再有脚本诊断

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptMathLibrary.h`；`../UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/FunctionLibraries/AngelscriptMathLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`；`Plugins/Angelscript/Source/AngelscriptTest` |
| 行号 | 28-30，40-42，52-65，92-112，155-202，294-296；26-27，37-38，57-59，85-101，145-190，281-282；328-329；全目录检索 |
| 描述 | 当前 `LerpShortestPath`、`RInterpShortestPathTo`、`TInterpTo`、`Modf_32/64`、`WrapDouble/Float/Int`、`WrapIndex` 上方仍保留了旧声明注释，里面都明确写着 `ScriptNoDiscard`；但实际声明已经退化成 `UFUNCTION(BlueprintCallable, Meta = ())` 或只剩 `ScriptName`，不再携带任何 `ScriptNoDiscard` 元数据。对照 `UEAS2` 参考实现，同名函数仍然保留 `Meta = (..., ScriptNoDiscard)`。绑定层 `Helper_FunctionSignature.h` 328-329 行明确只有函数带 `ScriptNoDiscard` 时才会把 `no_discard` 追加到最终脚本声明，因此这批纯返回值 math helper 在当前脚本层都可以被静默调用后直接丢弃结果。测试目录对这些函数全部为 0 命中。 |
| 根因 | `AngelscriptMathLibrary` 从旧 `ScriptCallable` 迁到 `BlueprintCallable` 时，只保留了函数可见性和少量 `ScriptName`，没有把原来批量存在的 `ScriptNoDiscard` 一并迁回。 |
| 影响 | 脚本现在可以无提示地写出 `Math::Wrap(...)`、`Math::WrapIndex(...)`、`Math::LerpShortestPath(...)`、`Math::TInterpTo(...)` 这类纯查询/纯变换调用并丢掉返回值，导致本应由声明层阻止的误用滑入运行时；这和 `UEAS2` 既有契约、也和当前仓库里仍保留 `no_discard` 的其它 math API 不一致。 |

### 发现 73：同一批 `Math` helper 还批量丢失了 `ScriptTrivial`，会退回更重的 `UFUNCTION` 绑定路径

| 项目 | 内容 |
|------|------|
| 维度 | E |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptMathLibrary.h`；`../UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/FunctionLibraries/AngelscriptMathLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp`；`Plugins/Angelscript/Source/AngelscriptTest` |
| 行号 | 28-30，40-42，52-65，92-112，155-202，294-296；26-27，37-38，57-59，85-101，145-190，281-282；261；143-145；全目录检索 |
| 描述 | 与上一条同一批函数在当前源码里不仅失去了 `ScriptNoDiscard`，上方注释保留的 `ScriptTrivial` 也没有被迁回真实声明；而 `UEAS2` 参考实现对这些函数依然显式标记了 `ScriptTrivial`。绑定层在 `Helper_FunctionSignature.h` 261 行只通过 `ScriptTrivial` 元数据设置 `Signature.bTrivial`，`Bind_BlueprintCallable.cpp` 143-145 行再把这个标记传入 `SCRIPT_NATIVE_UFUNCTION(...)`。这意味着当前 `Math::LerpShortestPath`、`Math::Modf`、`Math::Wrap*`、`Math::WrapIndex` 等 helper 都不再以 trivial native bind 方式注册，而是退回普通 `UFUNCTION` 调用路径。测试目录同样对这些入口全部为 0 命中。 |
| 根因 | 数学函数库在从 `ScriptCallable` 迁到 `BlueprintCallable` 时，把 Angelscript 侧的轻量绑定语义一起丢掉了，只留下“能调用”这一层最低保障。 |
| 影响 | 这批本来最适合出现在热路径里的基础 numeric helper，现在每次脚本调用都要走更重的反射/封送路径；即使单次开销不大，累计到插值、wrap、modf 这类高频运算上也会形成稳定的脚本侧性能回退。 |

### 发现 74：`GetLocalPlayerSubsystemFromLocalPlayer` 被挂上了无用的 hidden world-context，和真实实现语义不一致

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/SubsystemLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Subsystems.cpp`；`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Public/Subsystems/SubsystemBlueprintLibrary.h`；`Plugins/Angelscript/Source/AngelscriptTest` |
| 行号 | 59-64；223-231，423-429；1292；99-105；29-31，41-46；全目录检索 |
| 描述 | `USubsystemLibrary::GetLocalPlayerSubsystemFromLocalPlayer` 的声明仍然带 `Meta = (WorldContext = "WorldContextObject")`，因此绑定层会在 `Helper_FunctionSignature.h` 223-231 行把第一个参数改成 hidden `__WorldContext()`，并在 423-429 行给脚本函数打上 `asTRAIT_USES_WORLDCONTEXT`。但函数体 62-64 行完全忽略 `WorldContextObject`，只做 `USubsystemBlueprintLibrary::GetLocalPlayerSubsystem(LocalPlayer, Class)`。更直接的对照是：当前插件自己在 `Bind_Subsystems.cpp` 99-105 行已经为每个 `ULocalPlayerSubsystem` 生成了一个不依赖 world context 的 `ClassName Get(ULocalPlayer LocalPlayer)`，而预处理器 1292 行却仍生成 `Subsystem::GetLocalPlayerSubsystemFromLocalPlayer(LocalPlayer, Class.Get())` 这条会继承 hidden world-context trait 的路径。UE 原生 `USubsystemBlueprintLibrary` 头文件只公开了 `GetLocalPlayerSubsystem(ContextObject, Class)` 和 `GetLocalPlayerSubSystemFromPlayerController(...)`，并没有一个“显式传 `ULocalPlayer` 还要额外 world context”的对应入口。测试目录对这组 API 为 0 命中。 |
| 根因 | 该 wrapper 把“按 context 查 local-player subsystem”和“已经拿到 `ULocalPlayer` 直接查 subsystem”两种调用模型混在了一条声明里，导致签名层仍保留 world-context 约束，而实现层已经不再需要它。 |
| 影响 | 生成脚本和手写脚本都会把这条 helper 视为 world-context consumer，尽管它真正依赖的只有 `ULocalPlayer`。这会让 API 文档、trait 和调用约束比实现更严格，也让 `SubsystemLibrary` 与当前插件已存在的 `ClassName.Get(ULocalPlayer)` 直达路径形成不必要的双轨语义。 |

---

## 分析 (2026-04-08 11:46)

### 发现 75：`TSoftObjectPtr` / `TSoftClassPtr` 的属性匹配完全忽略 subtype，软引用静态类型在反射层被擦掉

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSoftObjectPtr.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptObjectBindingsTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptNativeScriptTestObject.h` |
| 行号 | 195-200，251-256；149-170，2163-2185；417-449；95-189；63-72 |
| 描述 | `FSoftObjectPtrType::MatchesProperty(...)` 和 `FSoftClassPtrType::MatchesProperty(...)` 现在只要看到 `FSoftObjectProperty` / `FSoftClassProperty` 就直接 `return true`，完全不比较 `PropertyClass` 或 `MetaClass`。同仓库里普通 `UObject*` 绑定的 `FUObjectType::MatchesProperty(...)` 会严格校验 `ObjectProp->PropertyClass == Class`，`TWeakObjectPtr` 绑定也会比较 `FWeakObjectProperty::PropertyClass`。也就是说，当前只有 soft reference 这一类模板在属性匹配阶段把 subtype 信息整体丢掉了。现有测试只验证 `MakeTemplateTypeUsage("TSoftObjectPtr", UTexture2D::StaticClass())` / `MakeTemplateTypeUsage("TSoftClassPtr", AActor::StaticClass())` 能解析出 subtype，以及一组 `TSoftObjectPtr<UTexture2D>` happy-path 操作能编译运行；测试对象里虽然真实声明了 `TSoftObjectPtr<UTexture2D>` / `TSoftClassPtr<UObject>` 属性，但没有任何 negative case 去验证“错误 subtype 的软引用属性必须拒绝匹配”。 |
| 根因 | `Bind_TSoftObjectPtr.cpp` 在实现 soft reference 模板类型时，只完成了 `CreateProperty()` 的 subtype 写入，却没有像 `FUObjectType` / `FWeakObjectPtrType` 那样把 subtype 校验同步写进 `MatchesProperty()`。 |
| 影响 | 凡是依赖 `FAngelscriptTypeUsage::MatchesProperty()` 做类型核对的反射路径，都会把 `TSoftObjectPtr<UTexture2D>`、`TSoftObjectPtr<AActor>`、`TSoftClassPtr<UUserWidget>` 等不同 subtype 视为同一种“软引用属性”。这会削弱脚本到原生属性/参数映射时的静态类型约束，把本应在匹配阶段暴露的 subtype 错配延后到更晚的运行时。 |

### 发现 76：`QueryTraceData` / `QueryOverlapData` 返回的 async trace 结果结构体被裁成残缺子集

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_WorldCollision.cpp`；`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Public/WorldCollision.h`；`Plugins/Angelscript/Source/AngelscriptTest` |
| 行号 | 95-106，131-140，299-306；110-125，170-194，202-222；全目录检索 |
| 描述 | 当前脚本绑定给 `FTraceDatum` 只暴露了 `Start`、`End`、`Rot`、`OutHits`、`TraceType`、`TraceChannel`、`UserData`，给 `FOverlapDatum` 只暴露了 `Pos`、`Rot`、`OutOverlaps`、`TraceChannel`、`UserData`。但 UE 原生 `FBaseTraceDatum` 还定义了 `PhysWorld`、完整的 `CollisionParams`（其中又包含 `CollisionQueryParam`、`ResponseParam`、`ObjectQueryParam`、`CollisionShape`）以及 `FrameNumber`，而 `FTraceDatum` / `FOverlapDatum` 正是从这个基类继承这些字段。当前运行时同时又通过 `System::QueryTraceData(...)` / `System::QueryOverlapData(...)` 把整结构体返回给脚本，说明脚本已经有“查询 async trace 明细”的用法入口，只是拿到的是被裁掉关键上下文的版本。对测试目录执行 `rg -n "FTraceDatum|FOverlapDatum|QueryTraceData|QueryOverlapData|CollisionParams|FrameNumber|PhysWorld" Plugins/Angelscript/Source/AngelscriptTest` 为 0 命中，当前没有任何回归去锁定这些字段暴露面。 |
| 根因 | `Bind_WorldCollision.cpp` 在把 native async trace 数据结构映射到脚本时，只挑了少数“结果字段”和一两个输入字段做 property 绑定，没有把基类里的请求上下文信息一起映射出来。 |
| 影响 | 脚本虽然能查询 async trace 结果，却无法从返回的 datum 中还原原始 `CollisionShape`、query params、object filters、发起帧号或物理 world。任何想在脚本侧做 trace 结果审计、调试回放、按 shape/过滤条件分支处理的逻辑，都必须自己额外缓存一份请求上下文，API 完整性明显落后于 native 结构体。 |

### 发现 77：soft reference 绑定缺少 `GetUniqueID` / `GetLongPackageFName` / `ResetWeakPtr`，脚本只能退回更重的字符串路径工作流

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSoftObjectPtr.cpp`；`J:/UnrealEngine/UERelease/Engine/Source/Runtime/CoreUObject/Public/UObject/SoftObjectPtr.h`；`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptObjectBindingsTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp` |
| 行号 | 390-428；565-591，806-808，927-953；463-485；107-169；207-250 |
| 描述 | `BindSoftPtrBaseMethods(...)` 当前只绑定了 `ToSoftObjectPath()`、`ToString()`、`GetLongPackageName()`、`GetAssetName()`、`IsValid/IsPending/IsNull()`、`Reset()` 和从 `FSoftObjectPath` 赋值；没有任何 `GetUniqueID()`、`GetLongPackageFName()` 或 `ResetWeakPtr()` 入口。UE 原生 `TSoftObjectPtr` 与 `TSoftClassPtr` 则都显式提供了这三类 API：`GetUniqueID()` 返回底层 `FSoftObjectPath`，`GetLongPackageFName()` 返回无分配的包名 `FName`，`ResetWeakPtr()` 用于在 object id 变化时只重置弱引用缓存。现有测试只编译了 `Get()`、`EditorOnlyLoadSynchronous()`、`ToSoftObjectPath()`、`ToString()`、`GetLongPackageName()`、`Reset()` 等 happy path；对这三条 native utility 在测试目录执行 `rg -n "GetUniqueID|GetLongPackageFName|ResetWeakPtr" Plugins/Angelscript/Source/AngelscriptTest` 为 0 命中。 |
| 根因 | soft reference 绑定目前只镜像了最基础的字符串化与状态查询接口，没有继续把 native 类型上常用的 path/cache utility 同步进脚本层。 |
| 影响 | 脚本要么被迫把包名取成 `FString` 再做比较/哈希，要么只能手工拆 `FSoftObjectPath`，也无法在对象重命名或重定向后只刷新 weak cache。对于资源表、缓存键和编辑器工具脚本，这会把原生可零分配或低成本完成的工作流退化成更重、更啰嗦的路径字符串处理。 |

### 发现 78：`TSoftClassPtr` 没有任何同步加载入口，连 editor-only 版本都缺失

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSoftObjectPtr.cpp`；`J:/UnrealEngine/UERelease/Engine/Source/Runtime/CoreUObject/Public/UObject/SoftObjectPtr.h`；`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp` |
| 行号 | 531-553，556-652；975-979；467-485；207-250 |
| 描述 | 当前绑定明确给 `TSoftObjectPtr<T>` 提供了 `EditorOnlyLoadSynchronous()`，并在文档里解释“只在 editor 暴露，因为 gameplay 中会造成 hitch”；但同一文件的 `TSoftClassPtr<T>` 区段只有构造、赋值、`Get()` 和 `LoadAsync()`，直到 652 行结束都没有任何同步加载函数。UE 原生 `TSoftClassPtr` 自身则在 `SoftObjectPtr.h` 975-979 行保留了 `LoadSynchronous()`。现有 parity test 也反映了这条 API 断层：测试只编译 `TSoftObjectPtr<UTexture2D>::EditorOnlyLoadSynchronous()` 与 `TSoftClassPtr<AActor>::Get()`，没有任何 `TSoftClassPtr` 的同步加载 smoke test；兼容性测试同样只覆盖 `Get()`、构造、赋值和 `Reset()`。 |
| 根因 | 当前 soft reference 绑定只把“避免 gameplay hitch”的 editor-only 同步加载策略落实到了 `TSoftObjectPtr`，却没有给 `TSoftClassPtr` 提供对应的 editor/tooling 入口。 |
| 影响 | 编辑器脚本如果手里拿的是 `TSoftClassPtr`，现在只能改写成异步加载流程，或先降级到更底层的 `FSoftObjectPath` / `UClass` 处理；这让 class soft reference 的工作流明显弱于 object soft reference，也不再与 UE 原生 `TSoftClassPtr` 的能力面对齐。 |

### 发现 79：soft reference 构造器丢失 `no_discard` 诊断，和 UE 原生 `[[nodiscard]]` 及旧绑定都不一致

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Low |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSoftObjectPtr.cpp`；`../UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_TSoftObjectPtr.cpp`；`J:/UnrealEngine/UERelease/Engine/Source/Runtime/CoreUObject/Public/UObject/SoftObjectPtr.h`；`Plugins/Angelscript/Source/AngelscriptTest` |
| 行号 | 375-380，442-447，559-569；377，383，446，452，576，582，588；50-71，181-307，770-794；全目录检索 |
| 描述 | 当前 `Bind_TSoftObjectPtr.cpp` 仍然注册了 `FSoftObjectPtr()`、`FSoftObjectPtr(FSoftObjectPath)`、`TSoftObjectPtr(T handle_only Object)`、`TSoftObjectPtr(const TSoftObjectPtr<T>&)`、`TSoftClassPtr(UClass)`、`TSoftClassPtr(const TSoftClassPtr<T>&)`、`TSoftClassPtr(const TSubclassOf<T>&)` 等一整组构造器，但这些绑定后面已经没有任何 `FAngelscriptBinds::SetPreviousBindNoDiscard(true)`。对照 `UEAS2` 旧实现，同一批构造器在 377、383、446、452、576、582、588 行后都显式补过 `SetPreviousBindNoDiscard(true)`；更底层的 UE 原生 `SoftObjectPtr.h` 也把 `FSoftObjectPtr` / `TSoftObjectPtr` / `TSoftClassPtr` 的这些构造器统一标成了 `[[nodiscard]]`。当前测试目录只覆盖了构造、复制和赋值的 happy path，对“丢弃 soft reference 临时值是否仍有诊断”执行 `rg -n "no_discard|SetPreviousBindNoDiscard" Plugins/Angelscript/Source/AngelscriptTest` 为 0 命中。 |
| 根因 | soft reference 绑定从旧实现迁到当前写法时，保留了构造器本身，但把先前显式补上的 `no_discard` 语义一并删掉了。 |
| 影响 | 脚本现在可以无提示地写出 `TSoftObjectPtr<UTexture2D>(Texture);`、`TSoftClassPtr<AActor>(AActor::StaticClass());` 这类纯构造表达式并直接丢弃结果，失去原生和旧绑定都提供过的误用提示。虽然这不是即时运行时崩溃，但会降低 API 诊断力度，让临时对象误用更难在编译期暴露。 |

---

## 分析 (2026-04-08 12:01)

### 发现 80：`GameplayTagContainerMixinLibrary` 已不是 `FGameplayTagContainer` 实际 API 的完整来源，`AppendTags` / `Reset` 只存在于手写 bind

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/GameplayTagContainerMixinLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FGameplayTag.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGameplayTagBindingsTests.cpp` |
| 行号 | 8-157；158-165，172；149，158-159 |
| 描述 | 当前 `UGameplayTagContainerMixinLibrary` 只保留了 `AddTag`、`RemoveTag`、`HasTag*`、`MatchesQuery`、`First/Last` 等 wrapper，但文件里已经完全看不到 `AppendTags` 和 `Reset`。与之相对，真实生效的 hand-written bind 在 `Bind_FGameplayTag.cpp` 164-165 行仍把 `AppendTags(const FGameplayTagContainer& Other)` 和 `Reset(int Slack = 0)` 直接挂到了 `FGameplayTagContainer` 上，现有兼容性测试 149、158-159 行也正是通过 `Combined.AppendTags(Tags); Combined.Reset();` 这条实例方法路径验证通过。换言之，`FunctionLibraries/GameplayTagContainerMixinLibrary.h` 已经不再反映当前脚本真实可用的 `FGameplayTagContainer` API 面。 |
| 根因 | `GameplayTagContainer` 的绑定职责被拆分到了 `FunctionLibrary` 和 `Bind_FGameplayTag.cpp` 两处后，没有维持单一来源：一部分 helper 只留在头文件，一部分核心操作只留在手写 bind。 |
| 影响 | 任何基于 `FunctionLibraries/` 目录做 API 盘点、文档生成或人工审查的人，都会漏掉测试真实依赖的 `AppendTags` / `Reset`；后续如果维护者只改 `FunctionLibrary` 或只改 hand-written bind，`FGameplayTagContainer` 的文档、实现和测试会继续三向漂移。 |

### 发现 81：`AActor::GetActorLocation` / `GetActorRotation` 被 `FunctionLibrary` 和手写 bind 同时维护

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptActorLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp` |
| 行号 | 5，75-96；1311-1314，1403-1404；32-33；44-45 |
| 描述 | `UAngelscriptActorLibrary` 仍保留 `ScriptMixin = "AActor"`，并把 `GetActorLocation(const AActor* Actor)`、`GetActorRotation(const AActor* Actor)` 声明成 `UFUNCTION(BlueprintCallable)`。`Bind_BlueprintType.cpp` 会把这类 `BlueprintCallable` mixin 自动走 `BindBlueprintCallable(...)`。与此同时，`Bind_AActor.cpp` 32-33 行又手写注册了完全同名同语义的 `AActor_.Method("FVector GetActorLocation() const", ...)` 和 `AActor_.Method("FRotator GetActorRotation() const", ...)`。现有测试 44-45 行只验证脚本里能调用 `GetActorLocation()` / `GetActorRotation()`，并不区分最终命中的是哪条绑定链。 |
| 根因 | `AActor` helper 在向 `BlueprintCallable` 迁移时，只把极少数 getter 补成了自动可收集状态，却没有同步删掉旧的 hand-written bind。 |
| 影响 | `GetActorLocation` / `GetActorRotation` 进入双源维护状态：签名、元数据、性能标签或未来空指针策略只要有一边继续演化，另一边就会滞后。现有测试只能证明“某个版本可用”，无法对覆盖顺序变化或两份实现分叉报警。 |

### 发现 82：`USceneComponent::SetRelativeLocation` 存在双源绑定，而且两份实现已经开始分叉

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptComponentLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_USceneComponent.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp` |
| 行号 | 7，37-40，79-81；131-176；118-120 |
| 描述 | `UAngelscriptComponentLibrary` 仍以 `ScriptMixin = "USceneComponent"` 暴露 `BlueprintCallable` 的 `SetRelativeLocation(USceneComponent* Component, const FVector& NewLocation)`。这意味着它会像其它 mixin 一样被自动收集成实例方法。与此同时，`Bind_USceneComponent.cpp` 131-134 行又手写注册了一条同名 `SetRelativeLocation(FVector NewLocation)`，而且实现并不相同：hand-written 版本在调用 `Component->SetRelativeLocation(NewLocation);` 后还额外执行 `Component->UpdateComponentToWorld();`。同一文件 172-176 行甚至专门对 `GetSocketQuaternion` 打了 `NotInAngelscript` 元数据来避免自动绑定重复，反过来证明维护者已经知道这类双重暴露风险，但 `SetRelativeLocation` 没有得到同样处理。现有测试 118-120 行只验证脚本里 `SetRelativeLocation(...)` / `GetRelativeLocation()` 能跑通，不区分最终走的是哪份实现。 |
| 根因 | `USceneComponent` 的迁移处于“FunctionLibrary 自动绑定”和“历史 hand-written bind”并存状态，但去重只针对个别已知函数做了特判，没有系统性清理同名入口。 |
| 影响 | `SetRelativeLocation` 的实际脚本语义将依赖绑定覆盖顺序，而不是单一实现：未来只要有一边继续调整更新时机、元数据或空指针策略，另一边就会形成不同步的行为分支。现有测试无法在双实现发生覆盖顺序变化时报警。 |

### 发现 83：`UAngelscriptWidgetMixinLibrary::GetRenderTransform` 把原生 const getter 缩成了可变 `UWidget*` 签名

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Low |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/WidgetBlueprintStatics.h`；`../UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/FunctionLibraries/WidgetBlueprintStatics.h`；`J:/UnrealEngine/UERelease/Engine/Source/Runtime/UMG/Public/Components/Widget.h`；`Plugins/Angelscript/Source/AngelscriptTest` |
| 行号 | 28，37-40；27，34-37；500；全目录检索 |
| 描述 | 当前 `UAngelscriptWidgetMixinLibrary::GetRenderTransform` 的签名是 `static const FWidgetTransform& GetRenderTransform(UWidget* Widget)`，把参数类型收窄成了可变 `UWidget*`。对照 `UEAS2` 参考实现，同一 wrapper 原本接收的是 `const UWidget*`；更底层的 UE 原生声明也明确是 `const FWidgetTransform& GetRenderTransform() const`。这意味着当前脚本层对一个纯 getter 人为引入了“必须持有可变 widget 才能调用”的约束，而测试目录对 `GetRenderTransform` 为 0 命中。 |
| 根因 | `Widget` helper 在整理到当前头文件时，没有保住原来和 native API 一致的 const-correctness，只保留了最基本的返回逻辑。 |
| 影响 | 即使调用方接受退回 `UAngelscriptWidgetMixinLibrary::GetRenderTransform(...)` 这种静态入口，API 也已经比 UE 原生和旧实现更严格：任何只持有只读 `UWidget` 语义的调用点都需要额外去掉 const，文档和自动补全也会把这个纯查询 helper 误导成“可能修改对象”的接口。 |

### 发现 84：`USubsystemLibrary::GetEngineSubsystem` 的公开注释仍在描述 `GameInstance` 语义，文档契约与真实 API 冲突

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Low |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/SubsystemLibrary.h`；`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Public/Subsystems/SubsystemBlueprintLibrary.h`；`Plugins/Angelscript/Source/AngelscriptTest` |
| 行号 | 16-21；23-35；全目录检索 |
| 描述 | `USubsystemLibrary::GetEngineSubsystem` 上方注释仍写着“Get a Game Instance Subsystem from the Game Instance associated with the provided context”，但真实签名是 `UEngineSubsystem* GetEngineSubsystem(TSubclassOf<UEngineSubsystem> Class)`，既不接收 `WorldContextObject`，也不返回 `UGameInstanceSubsystem`。对照 UE 原生 `USubsystemBlueprintLibrary`，`GetEngineSubsystem` 也是单纯按 `Class` 取 `UEngineSubsystem`，只有 `GetGameInstanceSubsystem` / `GetLocalPlayerSubsystem` / `GetWorldSubsystem` 才接收 context。测试目录对 `GetEngineSubsystem(` 和 `GetGameInstanceSubsystem(` 都是 0 命中，当前没有任何文档或回归会校验这类公开注释是否与实际签名一致。 |
| 根因 | `SubsystemLibrary` 的注释在复制 `GameInstance` getter 模板时没有针对 `EngineSubsystem` 版本做语义修正。 |
| 影响 | 任何根据头文件注释生成文档、评审 API 或直接阅读源码的维护者，都会被误导去寻找并不存在的 context 依赖，降低 `SubsystemLibrary` 作为脚本入口说明文档的可信度。 |

---

## 分析 (2026-04-08 12:12)

### 发现 85：`GameplayTagContainer::AddLeafTag` 被 wrapper 裁成 `void`，脚本层丢失“是否真正插入标签”的返回语义

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/GameplayTagContainerMixinLibrary.h`；`J:/UnrealEngine/UERelease/Engine/Source/Runtime/GameplayTags/Classes/GameplayTagContainer.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FGameplayTag.cpp`；`Plugins/Angelscript/Source/AngelscriptTest` |
| 行号 | 32-37；515-517；149-172；全目录检索 |
| 描述 | `UGameplayTagContainerMixinLibrary::AddLeafTag(...)` 当前声明为 `static void AddLeafTag(...)`，实现里只是直接调用 `GameplayTagContainer.AddLeafTag(TagToAdd);` 后丢弃返回值。UE 原生 `FGameplayTagContainer::AddLeafTag(const FGameplayTag& TagToAdd)` 在头文件里明确返回 `bool`，用于告诉调用方这次插入是否真的改变了容器。与此同时，手写绑定 `Bind_FGameplayTag.cpp` 149-172 行根本没有补一条 `AddLeafTag` 实例方法，因此当前 FunctionLibrary wrapper 是这条 helper 的唯一实现来源，返回语义就在这里被截断了。对 `Plugins/Angelscript/Source/AngelscriptTest` 执行 `rg -n "AddLeafTag\\("` 为 0 命中，当前没有测试覆盖这条 API 契约。 |
| 根因 | `GameplayTagContainer` 的 wrapper 在把 native 方法镜像到 FunctionLibrary 时，只保留了副作用调用，没有同步保住原生签名里表示“是否成功添加 leaf tag”的 `bool` 结果。 |
| 影响 | 脚本无法判断 `AddLeafTag` 是真正插入了新叶子标签，还是因为父标签/重复标签关系而没有改变容器；任何依赖返回值做去重、统计或条件分支的逻辑都必须额外在调用前后自行比较容器状态，API 完整性低于 UE 原生。 |

### 发现 86：`FVector3f` 的任意平面 `Dist2D` / `DistSquared2D` 实现退化成了世界 XY 距离

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptMathLibrary.h`；`../UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/FunctionLibraries/AngelscriptMathLibrary.h`；`Plugins/Angelscript/Source/AngelscriptTest` |
| 行号 | 368-377，459-468；348-356，433-441；全目录检索 |
| 描述 | 同一文件里 `FVector` 版 `Dist2D` / `DistSquared2D` 先对 `Vector` 和 `Other` 做 `VectorPlaneProject(..., UpDirection)`，随后调用的是完整的 `FVector::DistSquared(...)`，因此会计算投影后点在任意平面中的真实距离。对应的 `FVector3f` 版却在相同投影之后调用 `FVector3f::DistSquaredXY(...)`。`DistSquaredXY` 只忽略世界坐标的 `Z` 分量，并不会沿 `UpDirection` 定义的任意平面求距离；当 `UpDirection` 不是世界 `+Z` 时，投影结果里的 `Z` 分量仍然可能携带平面内的有效位移，结果就会被错误丢掉。也就是说，当前 `FVector3f` 实现和紧邻的 `FVector` 版本已经在数学语义上分叉。UEAS2 参考实现 433-441 行同样沿用了这份 `DistSquaredXY` 逻辑，说明该错误是长期遗留，而不是本轮修改才引入；但它在当前分支里仍然是未修复的真实源码行为。对测试目录执行 `rg -n "Dist2D\\(|DistSquared2D\\(" Plugins/Angelscript/Source/AngelscriptTest` 为 0 命中。 |
| 根因 | `FVector3f` helper 是从世界 XY 版本复制出来后补了 `VectorPlaneProject(...)`，但最终距离公式仍停留在 `DistSquaredXY(...)`，没有像 `FVector` 版那样切换到投影后向量的全分量距离。 |
| 影响 | 只要脚本在非世界 XY 平面上使用 `FVector3f::Dist2D(..., UpDirection)` 或 `DistSquared2D(..., UpDirection)`，得到的就不是“沿指定平面的 2D 距离”，而是“把投影结果再丢掉世界 Z 后的距离”。在任意重力方向、球面地表或局部导航平面逻辑里，这会直接给出偏小的距离值，导致阈值判断、移动制导和调试显示全部失真。 |

### 发现 87：`BindAxisKey` 把引擎原生的 `FKey` API 退化成了 `FName`，classic input 失去键位类型约束

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/InputComponentScriptMixinLibrary.h`；`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Components/InputComponent.h`；`J:/UnrealEngine/UERelease/Engine/Source/Runtime/InputCore/Classes/InputCoreTypes.h`；`Plugins/Angelscript/Source/AngelscriptTest` |
| 行号 | 76-90；543-553，950-963；49-72；全目录检索 |
| 描述 | `UInputComponentScriptMixinLibrary::BindAxisKey(...)` 当前把第二个参数声明成了 `const FName& AxisKey`，随后直接用它构造 `FInputAxisKeyBinding AB(AxisKey)`。而引擎原生 `FInputAxisKeyBinding` 的字段和构造函数都明确使用 `FKey`，`UInputComponent::BindAxisKey(...)` 两个 overload 也都是 `const FKey AxisKey`。`FKey` 虽然可以从 `FName` 隐式构造，但这意味着脚本层暴露出来的不再是“输入键”这个类型，而是任意名字字符串；和同文件里正确使用 `FKey` 的 `BindVectorAxis(...)` 形成了直接不一致。对测试目录执行 `rg -n "BindAxisKey\\(" Plugins/Angelscript/Source/AngelscriptTest` 为 0 命中。 |
| 根因 | 该 wrapper 在镜像 axis-name 风格 API 时，把原生 `BindAxisKey(FKey)` 错套成了 `BindAxis(FName)` 的参数形状，没有保住 native 接口的键位类型。 |
| 影响 | 脚本作者会得到一个比 UE 原生更弱的签名：任何拼错的 `FName` 都能通过静态类型检查，只有在 `FInputAxisKeyBinding` 内部构造 `FKey` 后才可能通过 `ensure(AxisKey.IsAxis1D())` 暴露问题。自动补全、文档和代码评审也会把这条 API 误导成“传 axis mapping 名称”而不是“传具体 axis key”。 |

### 发现 88：classic input 的 6 个 bind helper 全部吞掉返回的 binding 对象，脚本无法配置 `bExecuteWhenPaused` 等核心行为

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/InputComponentScriptMixinLibrary.h`；`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Components/InputComponent.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UEnhancedInputComponent.cpp`；`Plugins/Angelscript/Source/AngelscriptTest` |
| 行号 | 25-29，38-43，52-56，65-73，82-90，100-108；109-121，814，873-901，950-985，1007-1035；40-43；全目录检索 |
| 描述 | `BindAction`、`BindKey`、`BindChord`、`BindAxis`、`BindAxisKey`、`BindVectorAxis` 现在全部声明成 `static void ...`，内部只是在本地构造 `FInputActionBinding` / `FInputKeyBinding` / `FInputAxisBinding` / `FInputAxisKeyBinding` 后直接塞进组件数组。引擎原生 `UInputComponent` 对应入口则统一返回 binding 引用，例如 `AddActionBinding(...) -> FInputActionBinding&`、`BindAction(...) -> FInputActionBinding&`、`BindAxisKey(...) -> FInputAxisKeyBinding&`、`BindVectorAxis(...) -> FInputVectorAxisBinding&`。这些 binding 共同继承自 `FInputBinding`，其基类明确携带 `bConsumeInput` 和 `bExecuteWhenPaused` 两个控制位。当前 wrapper 只有 `BindKey` 额外暴露了一个 `bConsumeInput` 形参，其余 5 条 helper 都没有任何办法调整这两个核心开关，更不返回 binding 供后续修改。对照同仓库的 `Bind_UEnhancedInputComponent.cpp`，`BindDebugKey(...)` 就明确返回 `FInputDebugKeyBinding&` 并暴露 `bExecuteWhenPaused = true`，说明插件内部并非没有这种脚本暴露模式。测试目录对 `bExecuteWhenPaused`、`BindAxisKey(`、`BindVectorAxis(` 为 0 命中。 |
| 根因 | classic input 的 FunctionLibrary wrapper 在追求“一次调用直接完成绑定”时，把 native API 的 binding 对象生命周期和配置面一并裁掉了。 |
| 影响 | 脚本现在无法通过这组 helper 创建“暂停时仍执行”的输入绑定，也拿不到返回的 binding 去做更细粒度调整；`BindChord` / `BindAction` / `BindAxis*` 创建出来的绑定将永久停留在 `FInputBinding` 默认值 `bConsumeInput=true`、`bExecuteWhenPaused=false`。这让 Angelscript 的 classic input 工作流明显弱于 UE 原生，也弱于当前插件已经提供给 Enhanced Input 的绑定能力。 |

### 发现 89：`GetRenderTransform` 仍挂着一个找不到实参的 `WorldContext` 元数据，`UFUNCTION` 契约与真实签名脱钩

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Low |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/WidgetBlueprintStatics.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`；`Plugins/Angelscript/Source/AngelscriptTest` |
| 行号 | 36-40；223-235，423-429；全目录检索 |
| 描述 | `UAngelscriptWidgetMixinLibrary::GetRenderTransform` 当前声明为 `UFUNCTION(BlueprintCallable, Meta = (WorldContext = "WorldContextObject"))`，但函数签名只有一个参数 `UWidget* Widget`，并不存在名为 `WorldContextObject` 的形参。绑定层在 `Helper_FunctionSignature.h` 223-235 行会先读出 `WorldContext` 元数据，再按参数名匹配；只有找到同名参数时，才会把该参数改写成 hidden `__WorldContext()` 并记录 `WorldContextArgument`。这条函数因为压根没有对应参数，元数据会被静默忽略，423-429 行也不会给最终脚本函数打上 world-context trait。换言之，源码里保留了一份“这是 world-context API”的反射声明，但真实签名和最终脚本形态都不承认它。对测试目录执行 `rg -n "GetRenderTransform" Plugins/Angelscript/Source/AngelscriptTest` 为 0 命中。 |
| 根因 | `GetRenderTransform` 在复制 `CreateWidget` 的 `UFUNCTION` 模板时，把 `WorldContext` 元数据一并保留了下来，但函数本身并不消费任何 context 参数。 |
| 影响 | 任何直接读取 `UFUNCTION` 元数据做文档生成、静态检查或 API 盘点的工具，都会把这条纯 getter 错读成 world-context consumer；而实际脚本签名又不会隐藏任何参数。这会继续放大 `WidgetBlueprintStatics` 文档面和真实绑定面的偏差。 |

---

## 分析 (2026-04-08 12:36)

### 发现 90：`Script::GetModuleNameOfGlobalVariableBeingInitialized` 返回的是去 hotreload 后缀的 `baseModuleName`，不是实际模块名

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptScriptLibrary.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_module.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_module.cpp`；`Plugins/Angelscript/Source/AngelscriptTest` |
| 行号 | 26-33；237-240，318；169-177；全目录检索 |
| 描述 | `UAngelscriptScriptLibrary::GetModuleNameOfGlobalVariableBeingInitialized()` 在 31 行返回的是 `asCModule::InitializingGlobalProperty->module->baseModuleName`。但底层 `asCModule` 同时保留了真正的模块名字段 `name` 和专门注明“excluding any suffixes that get added during hotreload”的 `baseModuleName`；`GetName()` 实现也明确返回 `name.AddressOf()`。这意味着当前脚本 API 名字虽然叫“GetModuleName”，实际给出的却是 hotreload 去后缀后的基名，而不是当前正在初始化该全局变量的精确模块名。对测试目录执行 `rg -n "GetModuleNameOfGlobalVariableBeingInitialized" Plugins/Angelscript/Source/AngelscriptTest` 为 0 命中。 |
| 根因 | wrapper 在把 `asCGlobalProperty->module` 映射到脚本字符串时，直接取了 `asCModule::baseModuleName`，没有使用该类型已经暴露的真实模块名字段/访问器。 |
| 影响 | 一旦同一脚本模块经历 hotreload 或并行存在多个带后缀的重编译版本，脚本层就无法从这个 helper 拿到唯一、可回溯的模块标识；日志、调试面板和基于“当前初始化模块名”的分支逻辑都会把不同实例折叠成同一个 base name。 |

### 发现 91：向量平面/方向 helper 对未归一化输入给出错误结果，但源码和注释都没有声明这个前提

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptMathLibrary.h`；`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Core/Public/Math/Vector.h`；`Plugins/Angelscript/Source/AngelscriptTest` |
| 行号 | 347-400，405-407，438-491，496-498；804，1008，1418-1420，2121-2123；全目录检索 |
| 描述 | `UAngelscriptFVectorMixinLibrary` / `UAngelscriptFVector3fMixinLibrary` 的 `Size2D`、`SizeSquared2D`、`Dist2D`、`DistSquared2D`、`ConstrainToPlane` 都直接把 `UpDirection` / `PlaneUp` 传给 `VectorPlaneProject(...)`；而底层 `VectorPlaneProject` 又直接调用 `ProjectOnToNormal(PlaneNormal)`，实现是 `Normal * (V | Normal)`，没有做任何归一化。紧邻的 `ConstrainToDirection` 也直接返回 `Direction * Dot(Vector, Direction)`，同样把方向长度平方乘进结果。源码注释和函数名都只写 `UpDirection` / `Direction`，没有声明“必须传单位向量”。这意味着只要调用者传入非单位方向，这批 helper 就会静默算错，例如 `ConstrainToDirection((1,0,0), (2,0,0))` 会得到 `(4,0,0)`，而不是沿该方向的真实投影。对测试目录执行 `rg -n "Size2D\\(|Dist2D\\(|ConstrainToPlane\\(|ConstrainToDirection\\(" Plugins/Angelscript/Source/AngelscriptTest` 为 0 命中。 |
| 根因 | 这些 wrapper 直接复用了“法线已归一化”的底层几何公式，却没有在入口处调用 `GetSafeNormal()` / 除以 `SizeSquared()`，也没有把“输入必须归一化”写进契约。 |
| 影响 | 脚本一旦把任意长度的 `UpDirection` / `Direction` 传进来，平面长度、平面距离和方向约束都会发生缩放失真，而且不会有任何诊断信号。对于任意重力方向、局部坐标系或运行时动态生成方向向量的逻辑，这会直接把运动、导航和调试结果带偏。 |

### 发现 92：`TSoftObjectPtr<T>` / `TSoftClassPtr<T>` 的 `LoadAsync` 回调把模板实参退化成了 `UObject*` / `UClass*`

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/SoftReferenceStatics.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSoftObjectPtr.cpp`；`Plugins/Angelscript/Source/AngelscriptTest` |
| 行号 | 5-6；474-483，507，523，613-622，631，647；全目录检索 |
| 描述 | `SoftReferenceStatics.h` 把 async 回调类型固定声明成 `FOnSoftObjectLoaded(UObject* LoadedObject)` 和 `FOnSoftClassLoaded(UClass* LoadedClass)`。但同一绑定文件里，`TSoftObjectPtr<T>::Get()` 已经能返回 `T handle_only`，`TSoftClassPtr<T>::Get()` 也能返回 `TSubclassOf<T>`，说明运行时实际上知道模板子类型；`LoadAsync(...)` 内部还会用 `GetSoftPtrSubType()` 对加载结果做 `IsA` / `IsChildOf` 过滤，最后再把结果交给回调。也就是说，绑定层已经掌握了 `T`，却仍然在最后一步把回调 payload 退化成 `UObject*` / `UClass*`。对测试目录执行 `rg -n "LoadedObject|LoadedClass|FOnSoftObjectLoaded|FOnSoftClassLoaded|LoadAsync\\(" Plugins/Angelscript/Source/AngelscriptTest` 为 0 命中。 |
| 根因 | soft reference 的回调类型被做成了全局非模板 delegate，`Bind_TSoftObjectPtr.cpp` 直接复用这两个宽类型 delegate，没有为 `TSoftObjectPtr<T>` / `TSoftClassPtr<T>` 生成保留模板实参的回调签名。 |
| 影响 | 脚本在 async load 完成后会失去静态类型信息：`TSoftObjectPtr<UTexture2D>.LoadAsync(...)` 的回调参数不再是 `UTexture2D`，`TSoftClassPtr<AActor>.LoadAsync(...)` 的回调参数也不再是 `TSubclassOf<AActor>` 或至少 `AActor` 子类。调用方被迫在回调里手工 cast，自动补全、类型检查和脚本文档都会比同步 `Get()` 路径更弱。 |

### 发现 93：`MoveTowards` 只提供了 `FVector` 版本，`FVector3f` 在同组 math helper 里完全缺席

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Low |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptMathLibrary.h`；`Plugins/Angelscript/Source/AngelscriptRuntime`；`Plugins/Angelscript/Source/AngelscriptTest` |
| 行号 | 422-425，430-508；全目录检索；全目录检索 |
| 描述 | 在 `UAngelscriptFVectorMixinLibrary` / `UAngelscriptFVector3fMixinLibrary` 这组本来成对出现的 helper 里，当前源码只给 `FVector` 定义了 `MoveTowards(const FVector& Vector, const FVector& Target, double StepSize)`，紧随其后的 `FVector3f` 区段一直到 `ToColorString(...)` 结束都没有对应 overload。对整个运行时目录执行 `rg -n "MoveTowards\\(" Plugins/Angelscript/Source/AngelscriptRuntime` 也只命中这一条 `FVector` 版本，说明它不是迁到了 hand-written bind 或别的 FunctionLibrary。测试目录对 `MoveTowards(` 同样为 0 命中。 |
| 根因 | 这组向量扩展在维护时保持了 `Size2D`、`Dist2D`、`AngularDistance`、`ConstrainToPlane`、`ConstrainToDirection`、`ToColorString` 等 `FVector` / `FVector3f` 双版本对称，但 `MoveTowards` 没有同步补到 `FVector3f`。 |
| 影响 | 脚本一旦选择 `FVector3f` 这套 float32 数学类型，就会在最基础的“按固定步长逼近目标”工作流上失去与 `FVector` 对称的 utility，只能手工转成 `FVector`、自己调用 `VInterpConstantTo`，或重复写一层包装。这会把同域 API 面做成不必要的双轨。 |

---

## 分析 (2026-04-08 12:49)

### 发现 94：classic input 的 action/key/chord 绑定只暴露了带 `FKey` 的回调形态，原生无参 handler 在脚本侧消失

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/InputComponentScriptMixinLibrary.h`；`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Components/InputComponent.h`；`Plugins/Angelscript/Source/AngelscriptRuntime`；`Plugins/Angelscript/Source/AngelscriptTest` |
| 行号 | 19-57；125-134，208-239，873-890，1007-1035；全目录检索；全目录检索 |
| 描述 | 当前 `UInputComponentScriptMixinLibrary::BindAction`、`BindKey`、`BindChord` 全都把回调参数固定成 `FInputActionHandlerDynamicSignature`。而引擎原生 `InputComponent.h` 明确区分了无参 `FInputActionHandlerSignature`、带键位参数的 `FInputActionHandlerWithKeySignature`，`FInputActionUnifiedDelegate` 也同时支持这两种 native 形态，再由 `UInputComponent::BindAction(...)` / `BindKey(...)` 分别提供 overload。当前运行时目录对 `FInputActionHandlerSignature`、`FInputActionHandlerWithKeySignature` 做检索是 0 命中，说明脚本侧没有任何替代 wrapper 去保住“无需 `FKey` 参数”的绑定入口。 |
| 根因 | FunctionLibrary 在把 classic input 适配成动态委托时，只镜像了 `DECLARE_DYNAMIC_DELEGATE_OneParam(..., FKey, Key)` 这一条脚本可见签名，没有继续为原生的无参 action/key/chord handler 设计独立入口。 |
| 影响 | 现有 UE C++/Blueprint 代码里大量合法的 `void OnJump()`、`void OnConfirm()` 风格 handler，迁到 Angelscript 后会被迫改成额外接收一个根本不需要的 `FKey` 形参，或者完全绕开这些 helper。API 形态因此明显弱于 UE 原生，也让自动补全把 action 绑定误导成“总是带键位参数”的工作流。 |

### 发现 95：`UInputComponentScriptMixinLibrary` 完全漏掉了 `BindTouch` / `BindGesture`，classic input 在触摸与手势维度出现整块空洞

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/InputComponentScriptMixinLibrary.h`；`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Components/InputComponent.h`；`Plugins/Angelscript/Source/AngelscriptRuntime`；`Plugins/Angelscript/Source/AngelscriptTest` |
| 行号 | 18-109；411-415，625-629，1042-1062；全目录检索；全目录检索 |
| 描述 | 当前 `UInputComponentScriptMixinLibrary` 只覆盖了 `BindAction`、`BindKey`、`BindChord`、`BindAxis`、`BindAxisKey`、`BindVectorAxis`，整个文件直到 109 行结束都没有任何 `BindTouch` 或 `BindGesture`。对照引擎原生 `InputComponent.h`，`FInputTouchHandlerSignature` / `FInputGestureHandlerSignature` 以及对应的 `UInputComponent::BindTouch(...)`、`BindGesture(...)` 都是现成能力。运行时目录和测试目录对 `BindTouch(`、`BindGesture(` 的检索都是 0 命中，说明这两条 classic input helper 既没有迁到别的绑定文件，也没有任何回归覆盖。更直接的是，当前文件 `BindVectorAxis` 的注释里还残留了一行 `GB.GestureDelegate = FInputGestureUnifiedDelegate(Delegate);`（96 行），暴露出这里原本就处在 gesture 绑定实现被裁掉后的拷贝残片状态。 |
| 根因 | 该 FunctionLibrary 在补 classic input 辅助 API 时，只补了键盘/轴向路径，没有把 UE 原生已提供的 touch、gesture 绑定一并镜像进脚本层；注释残片说明这不是“故意不支持”，而更像未完成的移植。 |
| 影响 | Angelscript 在 classic input 工作流里无法直接绑定触摸屏和 gesture 事件，移动端、Steam Deck 触控板或任何依赖 pinch/swipe 类输入的脚本逻辑都只能退回 C++/Blueprint，或者再造一层自定义 wrapper。相对于同一个 `UInputComponent` API 面里已经存在的键盘/轴向 helper，这种缺口会让输入能力在不同设备类型之间明显失衡。 |

### 发现 96：`UPlayerInputScriptMixinLibrary` 只暴露映射管理，不暴露运行时按键状态与输入丢弃控制

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/InputComponentScriptMixinLibrary.h`；`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/GameFramework/PlayerInput.h`；`Plugins/Angelscript/Source/AngelscriptRuntime`；`Plugins/Angelscript/Source/AngelscriptTest` |
| 行号 | 153-250；592-596，691-692，719-741；全目录检索；全目录检索 |
| 描述 | 当前 `UPlayerInputScriptMixinLibrary` 只包装了 action/axis mapping 增删、`ForceRebuildingKeyMaps`、若干 mapping 查询、`InvertAxis` 和鼠标灵敏度 getter/setter。对照引擎原生 `UPlayerInput`，运行时同一对象还提供了 `FlushPressedKeys()`、`DiscardPlayerInput()`、`IsPressed()`、`WasJustPressed()`、`WasJustReleased()`、`GetTimeDown()`、`GetKeyValue()`、`GetRawKeyValue()`、`GetProcessedVectorKeyValue()`、`GetRawVectorKeyValue()` 这一整组“当前输入状态”和“清空本帧输入”的高频接口。对运行时目录和测试目录执行这些符号的检索均为 0 命中，说明插件当前没有别的脚本绑定兜底。 |
| 根因 | `UPlayerInputScriptMixinLibrary` 在设计上停留在“管理映射表”的子集镜像，没有继续把 `UPlayerInput` 作为 runtime keystate 容器的一半职责同步暴露给脚本。 |
| 影响 | 脚本现在能改 legacy input 配置，却不能直接问“某个键此刻是否按下/刚刚抬起/按了多久”，也不能在暂停、UI 抢占或切换控制权时通过 `FlushPressedKeys()` / `DiscardPlayerInput()` 清掉当前输入帧。任何需要基于 `UPlayerInput` 做低层输入状态机、一次性吞输入或调试采样的逻辑，都只能退回 `APlayerController` 其它 API 或原生代码。 |

### 发现 97：`UPlayerInputScriptMixinLibrary` 只保留了鼠标特例，丢掉了通用 axis 配置与反查接口

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/InputComponentScriptMixinLibrary.h`；`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/GameFramework/PlayerInput.h`；`Plugins/Angelscript/Source/AngelscriptRuntime`；`Plugins/Angelscript/Source/AngelscriptTest` |
| 行号 | 223-249；95-124，465-509；全目录检索；全目录检索 |
| 描述 | 当前脚本库在 `UPlayerInput` 侧只额外暴露了 `InvertAxis(FName AxisName)`、`SetMouseSensitivity(float)`、`GetMouseSensitivityX/Y()`。但原生 `PlayerInput.h` 同一能力域还提供了 `GetAxisProperties(FKey, FInputAxisProperties&)`、`SetAxisProperties(FKey, const FInputAxisProperties&)`、`GetInvertAxisKey(FKey)`、`GetInvertAxis(FName)`、`InvertAxisKey(FKey)`、`ClearSmoothing()`。`FInputAxisProperties` 本身还承载了 `DeadZone`、`Sensitivity`、`Exponent`、`bInvert` 四个核心字段。对运行时目录和测试目录检索这些 API 名称均为 0 命中，说明脚本侧既拿不到通用 axis property 配置，也拿不到“当前是否已反转”的查询入口。 |
| 根因 | `UPlayerInputScriptMixinLibrary` 只镜像了最常见的鼠标灵敏度和按名字反转 axis 这条窄路径，没有把 `UPlayerInput` 已经具备的“按 `FKey` 配置 axis 特性”能力同步补齐。 |
| 影响 | 脚本可以粗粒度地改鼠标灵敏度，却不能设置摇杆 dead zone、response exponent、按键级 axis sensitivity，也不能先查询某条 axis/axis key 当前是否已被反转。这会把 legacy input 的调参面压缩成少数特例接口，迫使任何做运行时输入设置页、辅助功能选项或设备校准逻辑的脚本重新下沉到 C++。 |

### 发现 98：axis 绑定只剩“必须注册回调”的版本，原生 value-only 轮询路径在脚本侧缺席

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/InputComponentScriptMixinLibrary.h`；`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Components/InputComponent.h`；`Plugins/Angelscript/Source/AngelscriptRuntime`；`Plugins/Angelscript/Source/AngelscriptTest` |
| 行号 | 59-109；772-797，931-999；全目录检索；全目录检索 |
| 描述 | 当前 `UInputComponentScriptMixinLibrary` 只提供了带 delegate 的 `BindAxis`、`BindAxisKey`、`BindVectorAxis` 三个 helper。引擎原生 `UInputComponent` 则额外提供了三条无回调 overload：`BindAxis(const FName AxisName)`、`BindAxisKey(const FKey AxisKey)`、`BindVectorAxis(const FKey AxisKey)`，并配套 `GetAxisValue()`、`GetAxisKeyValue()`、`GetVectorAxisValue()` 走轮询式读取。这允许调用方只声明“我对这个 axis 感兴趣”，而不必每帧注册委托。当前运行时目录和测试目录对这些 getter / value-only overload 的检索均为 0 命中，说明脚本侧没有任何等价入口。 |
| 根因 | FunctionLibrary 在镜像 classic axis API 时，只实现了“回调驱动”的那一半，没有继续覆盖 `UInputComponent` 原生已经支持的 value-tracking 工作流。 |
| 影响 | 任何想在 `Tick` 或状态机里自己读取 axis 值的脚本，都不能像原生 UE 那样先 `BindAxis(...)` 再按需 `GetAxisValue()` 轮询；只能强行绑定一个委托、自己缓存值，或者绕回别的输入系统。这让 classic input 的轮询式用法在 Angelscript 里比 UE 原生更笨重，也更难迁移现有代码。 |
