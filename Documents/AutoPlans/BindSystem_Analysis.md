# BindSystem 分析

---

## 分析 (2026-04-08 02:32)

### 发现 1：literal asset 软重载不再重建 script object，旧脚本态会跨版本残留

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_UObject.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/ClassGenerator/ASClass.h`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/ClassGenerator/ASClass.cpp` |
| 行号 | 674-682；477-479；80；1131-1139 |
| 描述 | 当前 `__CreateLiteralAsset()` 在命中已有 asset 时只做 `FProperty` 级别的 `CopyCompleteValue_InContainer`。UEAS2 在同一位置会先对 `UASClass` 调用 `ReconstructScriptObject(ExistingObject)`，而该接口会执行 `CallDestructor()`、`ExecuteConstructFunction()`、`ExecuteDefaultsFunctions()`。当前分支的 `ASClass.h` / `ASClass.cpp` 已经不存在这条接口，因此 literal asset 软重载时不会重建 script object 本体。 |
| 根因 | 绑定层保留了 literal asset reload 入口，但移植过程中把“已有对象重建脚本实例”的运行时支撑一起裁掉了，导致 bind 只能退化成反射属性复制。 |
| 影响 | 任何不完全受 `FProperty` 覆盖、而是依赖 construct/defaults/script object 生命周期初始化的脚本状态，都会在 literal asset 热重载后继续保留旧值。结果是 asset 文本、反射属性和真实 script state 可能分叉，出现只在热重载后复现的状态残留问题。 |

### 发现 2：literal asset 重置路径会直接覆盖 instanced subobject 属性，偏离 UEAS2 的所有权保护

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_UObject.cpp` |
| 行号 | 676-681；481-487 |
| 描述 | 当前分支在重置已有 literal asset 时，对 `AssetClass` 的每个 `FProperty` 都无条件执行 `CopyCompleteValue_InContainer(ExistingObject, CDO)`。UEAS2 同一段代码会先判断 `!Prop->ContainsInstancedObjectProperty()`，显式跳过带 instanced object 的属性。也就是说，当前实现会把 instanced subobject/property 直接从 CDO 覆盖回运行中对象。 |
| 根因 | 软重载逻辑只保留了“把值恢复成 CDO”这一层表面语义，没有把 UEAS2 对 instanced ownership 的特殊处理一并迁移过来。 |
| 影响 | 对包含 instanced subobject 的 literal asset，热重载可能把运行中实例重新指回默认对象图，或者覆盖掉 asset 自己持有的子对象状态。这会带来对象别名、生命周期错乱、热重载后二次编辑丢失等问题，而且当前仓库里没有对应测试去锁住这种回归。 |

### 发现 3：`GetAllSubclassesOf()` 不再过滤已被清理的 script class tombstone

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_UObject.cpp` |
| 行号 | 356-379；4990-5024；202-213，242-269 |
| 描述 | 当前 `UClass::GetAllSubclassesOf()` 只过滤 `CLASS_Deprecated`、`CLASS_NewerVersionExists` 和 abstract class，然后直接把匹配类加入结果。与此同时，本地 `CleanupRemovedClass()` 会把被移除的 `UASClass` 置成 `ScriptTypePtr = nullptr`、`ConstructFunction = nullptr`、`DefaultsFunction = nullptr`，并打上 `CLASS_Hidden | CLASS_HideDropDown | CLASS_NotPlaceable`。UEAS2 在 `Bind_UObject.cpp` 里专门有 `IsDeletedAngelscriptClass()`，会把这类 tombstone 从 `GetAllSubclassesOf()` 结果里剔除；当前分支已没有这层过滤。 |
| 根因 | `Bind_UClass_Base` 在本地扩展脚本类 introspection API 时，没有把 UEAS2 为 hot-reload/删除类准备的 editor 过滤逻辑一起保留下来。 |
| 影响 | 依赖 subclass 枚举的脚本工具、菜单、选择器或运行时发现逻辑，可能在热重载/删除脚本类后看到“已不存在但仍可枚举”的 `UASClass`。后续一旦继续读取 `ScriptTypePtr` 或尝试实例化，就会落到失效类状态而不是稳定的新类集合。 |

### 发现 4：Bind core 已失去 `UnsafeDuringConstruction` trait 通道，construction safety 注解现在无法表达

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/AngelscriptBinds.h`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/AngelscriptBinds.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_UObject.cpp` |
| 行号 | 627-640；329-390；556-589；564-575；176-180；355-389 |
| 描述 | 当前 bind core 仍保留 `SetPreviousBindIsEditorOnly`、`SetPreviousBindRequiresWorldContext`、`SetPreviousBindNoDiscard` 等元数据入口，但 `AngelscriptBinds.h` / `.cpp` 已经完全没有 `SetPreviousBindUnsafeDuringConstruction()`。与之对应，当前 `Bind_UObject.cpp` 在 `NewObject`、`LoadObject` 绑定后也没有任何 construction-phase safety 标记。UEAS2 不仅保留了该 API，还在 `Bind_UObject.cpp` 上显式给 `NewObject`、`LoadObject` 打 `asTRAIT_UNSAFE_DURING_CONSTRUCTION`。这说明当前分支缺失的不是单个补丁，而是整条 trait 写入链路。 |
| 根因 | 绑定系统做 trait API 收缩时把 construction safety 从 core surface 上一起移除了，导致后续 bind 文件即使想补，也没有统一入口可用。 |
| 影响 | 任何在 `default` / construction / async loading 边界里本应被标记为危险的 API，现在都只能裸暴露。结果是错误从“可被 trait 约束或静态诊断”的绑定元数据问题，退化成运行时副作用、时序污染或更靠后的崩溃问题。 |

### 发现 5：`UObject` 创建/查找 helper 的 `no_discard` 注解整体回退，结果契约弱于 UEAS2

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_UObject.cpp` |
| 行号 | 514-603；350-404 |
| 描述 | 当前 `Bind_UObject.cpp` 里的 `GetTransientPackage()`、`GetAngelscriptPackage()`、`NewObject(...)`、`LoadObject(...)`、两个 `FindObject(...)` overload 都是普通返回值签名；UEAS2 对应声明全部带 `no_discard`。这些 API 的主要输出就是返回的 package/object handle，本地去掉 `no_discard` 后，脚本侧忽略结果将不再得到编译期契约提示。 |
| 根因 | 绑定声明在迁移时保留了函数体和参数，但没有把 UEAS2 已经补上的返回值语义注解一起迁入。 |
| 影响 | 对象创建/查找类 API 的误用会更容易静默发生，尤其是“调用成功但忘记接收返回对象”这类问题。当前分支里同文件的 `IsValid(const UObject Object) no_discard` 仍保留注解，因此这组 helper 的返回值约束已经出现文件内不一致。 |

---

## 分析 (2026-04-08 02:36)

### 发现 6：`UObject` 调试字符串格式化把 `UClass` 强行当成 `UASClass`，native class 可直接触发空指针解引用

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | Critical |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_UObject.cpp` |
| 行号 | 222-264；156-195 |
| 描述 | 当前 `FToStringHelper::Register(TEXT("UObject"), ...)` 先把 `ObjClass` 做 `Cast<UASClass>`，随后在两个 `FString::Printf` 分支里都执行 `(ObjClass->HasAnyClassFlags(CLASS_Native) || asClass->bIsScriptClass) ? asClass->GetPrefixCPP() : TEXT("")`。当对象类是普通 native `UClass` 时，条件左侧会因 `CLASS_Native` 为真而短路，但真分支仍然无条件调用 `asClass->GetPrefixCPP()`；而当对象类既不是 native、也不是 `UASClass` 时，条件右侧还会先解引用 `asClass->bIsScriptClass`。UEAS2 对应实现直接基于 `ObjClass->bIsScriptClass` / `ObjClass->GetPrefixCPP()`，没有这条空指针路径。 |
| 根因 | 本地分支把原先基于 `UClass` 的 prefix 判定替换成了 `UASClass` 特化指针，但没有给非 `UASClass` 的常规 `UClass` 建立保护分支。 |
| 影响 | 任意走到 `FToStringHelper` 的对象调试输出，只要类不是 `UASClass`，就可能在格式化阶段崩在空指针解引用上。这个路径会影响日志、断言、调试器对象展示等横切能力，故障点远离业务调用栈，定位成本高。 |

### 发现 7：`__WorldContext` 从全局变量退化成只读函数，UEAS2 显式 world-context 写法不再等价

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UWorld.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_UWorld.cpp` |
| 行号 | 33-41；230-232；230，423-426；34-38 |
| 描述 | UEAS2 把 world context 绑定成全局变量 `UObject __WorldContext`，脚本既可以读取也可以把它当作默认值或普通表达式使用。当前分支改成了零参函数 `UObject __WorldContext()`，并且仓库里仅在 `Bind_BlueprintType.cpp` 的默认值转换和 `Helper_FunctionSignature.h` 的 hidden argument 默认值生成处，把字符串形式的 `__WorldContext` 规范成 `__WorldContext()`；没有看到针对脚本显式读写 `__WorldContext` 的通用重写层。`Plugins/Angelscript/Source/AngelscriptTest` 下也没有任何 `__WorldContext` / `GetCurrentWorld()` 的绑定回归。 |
| 根因 | 运行时上下文访问从“暴露可读写变量”收口成“按需查询函数”后，只补了 blueprint/signature 生成链路上的字面量兼容，没有把脚本侧显式 API 形态的兼容一起覆盖。 |
| 影响 | 依赖 UEAS2 显式 world-context 写法的脚本，在当前分支里会遇到两类回退：一类是读取场景需要从变量名迁移到函数调用；另一类是赋值、取引用或需要 lvalue 语义的旧写法已经无法表达。由于仓库没有对应 smoke test，这类兼容性断裂只能在脚本编译或运行时由具体业务首次踩中。 |

### 发现 8：`FindClass` / `__StaticClass` 会把已删除或已替换的 script class 当成有效结果返回

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp` |
| 行号 | 331-334，382-394，519-531；2576-2578，4995-5003；50-55，386-393 |
| 描述 | 当前新增的 `UClass::FindClass`、全局 `FindClass` 和 `__StaticClass` 都是直接遍历 `TObjectIterator<UClass>` 后按名字返回，完全不检查 `CLASS_NewerVersionExists`、`CLASS_Hidden`、`CLASS_HideDropDown`、`CLASS_NotPlaceable`，也不检查 `UASClass::ScriptTypePtr` 是否已经被清空。与此同时，class generator 在 full reload 时会把被替换类重命名并打上 `CLASS_NewerVersionExists`，在删除类时则通过 `CleanupRemovedClass()` 把 `ScriptTypePtr/ConstructFunction/DefaultsFunction` 置空并打上隐藏类标记。结果是 direct lookup API 仍可能把这些“已退出有效脚本集合”的类返回给脚本。现有自动化只验证 `FindClass("AActor")` 和 `FindClass("ABindingStaticClassActor")` 的 happy path，没有覆盖 reload/remove 之后的 lookup 语义。 |
| 根因 | 本地分支在 `Bind_UObject.cpp` 新增 class lookup helper 时，复制了“按名字扫描对象迭代器”的最小实现，但没有复用同文件其它 helper 已经在做的 class-flag 过滤，也没有针对 script tombstone 定义统一判定。 |
| 影响 | 热重载或删除脚本类后，脚本侧用 `FindClass` / `__StaticClass` 取到的并不一定是当前可实例化、可调用的类，而可能是 `ScriptTypePtr == nullptr` 的失效壳。后续再去 `GetDefaultObject()`、枚举函数或实例化时，报错点会远离真正的 lookup 根因，而且当前测试不会提前暴露这种回退。 |

### 发现 9：`UClass` 命名空间查找与全局查找规则不一致，class lookup surface 出现命名契约分叉

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp` |
| 行号 | 331-334，356-380，519-534；28-32；50-55，386-393 |
| 描述 | 同一文件里同时暴露了两套 class lookup 入口，但匹配规则不一致：`UClass::FindClass` 直接走 `FindObject<UClass>(nullptr, *Name)`，只能按底层 UObject 名字查；全局 `FindClass` 则额外接受 `FAngelscriptType::GetBoundClassName(Class)`，也就是 `PrefixCPP + GetName()` 的脚本类型名，例如 `AActor` 这类带前缀名字。与此同时，`GetAllSubclassesOf` 只存在 `UClass` 命名空间版本，没有对应的全局重载。仓库测试只覆盖了全局 `FindClass("AActor")` / `GetAllClasses` happy path，未覆盖 `UClass::FindClass` 和 `GetAllSubclassesOf` 的一致性。 |
| 根因 | 本地分支在扩展 class lookup API 时，把“对象名查找”“脚本类型名查找”“命名空间/全局入口”三套语义分别落在不同 helper 上，但没有定义统一的查找契约。 |
| 影响 | 脚本作者会得到表面同名、实际语义不同的查找 API：同样传 `AActor`，全局 `FindClass` 可命中，`UClass::FindClass` 却可能返回空；而想从全局入口继续做 subclass 枚举时又缺少对称 overload。结果是 lookup 代码对调用位置和命名风格变得敏感，兼容性和可发现性都下降。 |

---

## 分析 (2026-04-08 02:48)

### 发现 10：`SpawnActor` 三条入口已绕过 UEAS2 的统一 spawn 审核钩子，项目级拦截点整条消失

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_AActor.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/AngelscriptCodeModule.h`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/AngelscriptCodeModule.cpp` |
| 行号 | 166-253，199-253；9-47；42-46；223-328；9-32；22-25 |
| 描述 | 当前 `SpawnActorFromMeta()`、`SpawnActor()`、`SpawnPersistentActor()` 在做完 world/class 检查和 `FActorSpawnParameters` 填充后，都会直接 `return World->SpawnActor(...)`。当前 `AngelscriptRuntimeModule.h/.cpp` 只保留了 `GetDynamicSpawnLevel()`、`GetDebugObjectSuffix()` 等 delegate 入口，没有任何 `GetVerifySpawnActor()`。UEAS2 对应模块声明了 `FAngelscriptVerifySpawnActor`，并在这三条 spawn 路径里统一执行 `GetVerifySpawnActor().Execute(...)`，失败时提前返回 `nullptr`。 |
| 根因 | 运行时模块迁移时只保留了“动态选择 spawn level”这类扩展点，没有把 UEAS2 用于 spawn policy 的审核 delegate 一起迁入；bind 层因此失去统一的 pre-spawn 校验位置。 |
| 影响 | 任何依赖该钩子实现的项目级约束都会静默失效，包括拦截非法 spawn、限制 persistent spawn、统一修正 `FActorSpawnParameters` 或做审计统计。结果不是编译期缺口，而是脚本照常调用成功，但原本应被拒绝或修正的 actor 现在会直接落到 `UWorld::SpawnActor`。 |

### 发现 11：actor 旧泛型 bind 的弃用控制面已被移除，命名迁移策略退化成永久裸暴露

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_AActor.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/AngelscriptSettings.h` |
| 行号 | 25-149，286-313；33-98；684-694；34-205，363-390；34-39，105-111 |
| 描述 | 当前 `Bind_AActor.cpp` 无条件注册旧式 `GetComponentsByClass(..., ?& OutComponents)` 系列和每个 actor namespace 下的 `ClassName::Spawn(...)`，文件内也没有任何 `DeprecatePreviousBind(...)`。同时，当前 `AngelscriptSettings.h` 已经不存在 UEAS2 的 `EAngelscriptDeprecationMode` / `DeprecateOldActorGenericMethods` 配置项。对照之下，UEAS2 会根据 `DeprecateOldActorGenericMethods` 决定这些旧 API 是允许、发 deprecated 还是彻底禁用。更重要的是，本地并不是整体放弃弃用体系，因为同仓库 `Bind_BlueprintType.cpp` 仍然按照 `StaticClassDeprecation` 对 `StaticClass()` 做 deprecated/disallowed 控制。 |
| 根因 | 迁移过程中只保留了部分弃用框架和配置面，actor 旧泛型 bind 的策略开关被裁掉，但旧 API surface 本身没有同步删除。 |
| 影响 | 当前分支的 bind naming policy 在同一插件内部已经不一致：`StaticClass()` 等 API 仍可按配置逐步迁移，而 `AActor::Spawn()`、旧式 `GetComponentsByClass(..., OutArray)` 永远是静态常驻入口，脚本无法通过配置得到统一的 deprecated 提示或禁用行为，长期会固化旧调用风格。 |

### 发现 12：`GetComponentsByClassWithTag` 从 UEAS2 丢失，actor 组件查询少了一个实际使用的 tag-filter overload

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_AActor.cpp` |
| 行号 | 39-149；151-165；154-205 |
| 描述 | 当前 `AActor` bind 只保留了两个 `GetComponentsByClass` overload：一个按数组元素类型推导，一个显式传 `UClass ComponentClass`。UEAS2 在同一区块还额外提供了 `void GetComponentsByClassWithTag(const FName& Tag, ?& OutComponents) const`，实现逻辑是同时按 `ComponentHasTag(Tag)` 和数组元素 subtype 过滤。当前仓库的 `AngelscriptNativeEngineBindingsTests.cpp` 也只验证了 `GetComponentsByClass(SceneComponents)` 与 `GetComponentsByClass(AllComponents)`，没有任何 tagged overload 回归。 |
| 根因 | `Bind_AActor.cpp` 迁移旧泛型组件查询时只搬了前两个 overload，遗漏了带 `Tag` 的第三个变体及其对应的弃用提示。 |
| 影响 | 依赖 UEAS2 组件 tag 过滤的脚本必须回退到“先取全量组件，再手写 tag 过滤”的两段式实现，API parity 明显下降；而且当前测试只覆盖未带 tag 的 happy path，使这个缺口不会在绑定回归里被自动发现。 |

### 发现 13：`__Actor_GetAllByClass` 暴露了一个未校验的隐藏枚举入口，错误处理契约弱于同文件公开 API

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 行号 | 355-409；1176-1184 |
| 描述 | 同文件公开的 `GetAllActorsOfClass(UClass Class, ?& OutActors)` 会完整检查 `TypeId`、`TArray` element type、`ActorClass == nullptr` 以及 `ActorClass->IsChildOf(ArraySubClass)`，出错时统一 `Throw(...)`。但当前分支额外导出了 `void __Actor_GetAllByClass(UClass Class, ?& OutActors)`，实现只有一行 `UGameplayStatics::GetAllActorsOfClass(...)`，完全不读取 `TypeId`、不检查 `ActorClass`、也不校验输出数组元素类型。更糟的是，预处理器里原本可能生成它的 `GetAll()` wrapper 已整段注释掉，说明这个 helper 在当前分支没有受控代码生成入口，却仍作为全局 bind 暴露。 |
| 根因 | 旧的 actor helper 在演进过程中只移除了自动生成调用点，没有同步删除或补齐 helper 本身的参数验证逻辑，留下了一个“带双下划线命名但实际公开可调用”的半成品入口。 |
| 影响 | 脚本如果直接调用这个 helper，会绕过同文件公开 API 的全部参数诊断，导致相同功能在不同入口上表现出不同的失败语义；同时它还是未文档化的 `__` 命名 surface，容易被业务当成内部约定误依赖，后续清理时风险更高。 |

### 发现 14：`__StaticClass` 新 helper 绕开了 `StaticClassDeprecation` 策略链，配置无法真正封禁旧 class lookup 习惯

| 项目 | 内容 |
|------|------|
| 维度 | B / C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h`<br>`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp` |
| 行号 | 382-394；684-694；870-897；94-98；438-472 |
| 描述 | 当前仓库仍保留 `StaticClassDeprecation` 配置，`Bind_BlueprintType.cpp` 和预处理器也会按照它决定 `Type::StaticClass()` 是正常、deprecated 还是彻底不生成。但 `Bind_UObject.cpp` 同时无条件注册了全局 `UClass __StaticClass(const FString& Name)`，并且没有任何设置判断。也就是说，即使项目把 `StaticClassDeprecation` 设成 `Disallowed`，脚本仍然能通过字符串调用 `__StaticClass("AActor")` 取得同类信息。现有 `AngelscriptClassBindingsTests.cpp` 只验证 namespace `StaticClass()` 和 `__StaticType_*` happy path，没有覆盖这个旁路入口。 |
| 根因 | 新增 class lookup helper 时没有接入已有的 static-class 配置治理链路，而是直接在 `Bind_UClass_Base` 旁边裸注册了另一个全局入口。 |
| 影响 | `StaticClassDeprecation` 现在只能限制“某一种语法形式”，不能真正限制“旧的类获取行为”。结果是配置表面上允许项目推进 direct type value 迁移，但 bind 层依旧保留一个未文档化的回退通道，策略收敛性被削弱。 |

---

## 分析 (2026-04-08 03:00)

### 发现 15：`UObject::opCast` 对 null handle 没有保护，任意空对象转型都可能直接解引用崩溃

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | Critical |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp` |
| 行号 | 135-214 |
| 描述 | `UObject_.Method("void opCast(?& Address) const", ...)` 在过滤掉非 handle/struct 后，直接执行 `const bool bIsA = Object->IsA(AssociatedClass);`，随后还会无条件访问 `Object->GetClass()->ImplementsInterface(AssociatedClass)` 和日志里的 `Object->GetClass()->GetName()`。整个函数没有任何 `if (Object == nullptr)` 分支，因此脚本只要对空 `UObject` handle 做一次 `cast`，就会在绑定层先于正常失败语义发生空指针解引用。 |
| 根因 | 这条通用 down-cast helper 把类型检查集中到了 `TypeId` / `AssociatedClass` 上，却默认 `Object` 一定非空；当前分支在为 interface cast 和调试日志扩展逻辑时，又额外增加了多处对 `Object` / `Object->GetClass()` 的直接访问。 |
| 影响 | 任何依赖“空对象 cast 返回 null”这一常见脚本语义的代码，都可能在绑定层直接崩溃，而不是回到脚本侧处理空结果。由于 `opCast` 是所有 UObject 向下转型共享的基础入口，影响面覆盖普通类 cast、接口 cast 以及相关调试路径。 |

### 发现 16：当前分支自增的 `GetAllClasses` surface 会把已删除的 script class tombstone 当成有效类枚举出来

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp` |
| 行号 | 337-352，537-552；4990-5002；39-92 |
| 描述 | 当前仓库在 `UClass` namespace 和全局空间各新增了一份 `GetAllClasses(TArray<UClass>& OutClasses)`，实现都只过滤 `CLASS_Deprecated | CLASS_NewerVersionExists`。但 class generator 的 `CleanupRemovedClass()` 会把被删除的 `UASClass` 保留下来，并仅设置 `ScriptTypePtr/ConstructFunction/DefaultsFunction = nullptr`，再打上 `CLASS_NotPlaceable | CLASS_HideDropDown | CLASS_Hidden`。这意味着 removed script class 不会被 `GetAllClasses` 两份实现剔除，会继续出现在脚本枚举结果里。现有 `AngelscriptClassBindingsTests.cpp` 对这组 helper 只验证“结果非空且包含 AActor”，没有覆盖 reload/remove 之后的枚举语义。 |
| 根因 | 本地分支新增 class 枚举 helper 时复用了 `Deprecated/NewerVersionExists` 过滤，但没有把删除脚本类的 tombstone 判定统一接进来；同时这条 helper 在 UEAS2 中并不存在，缺少成熟语义参照。 |
| 影响 | 脚本用 `GetAllClasses` 构建下拉列表、类型注册表或反射缓存时，会把已经退出有效脚本集合的 removed class 一并缓存进去。后续一旦再调用 `GetDefaultObject()`、查询 script module 或实例化，就会在离枚举点更远的位置暴露失效状态，而当前测试不会提前拦住这类回归。 |

### 发现 17：`UObject::opCast` 把接口转型排查代码硬编码成测试名/类型名字符串，runtime bind 已被测试调试逻辑污染

| 项目 | 内容 |
|------|------|
| 维度 | B / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceCastTests.cpp` |
| 行号 | 138-166，192-201；33-103，105-169 |
| 描述 | 当前 `UObject::opCast` 一开始就根据 `Object->GetClass()->GetName().Contains("ScenarioInterfaceCastSuccess")` 和 `RequestedType->GetName()` 是否包含 `"DamageableCast"` 来决定是否 `UE_LOG(Display, ...)`。而测试文件里正好存在 `ScenarioInterfaceCastSuccess` 模块 / 类，以及 `UIDamageableCastOk`、`UIDamageableCastFail` 两个接口名。也就是说，这段绑定代码不是通用诊断开关，而是对某次接口转型测试场景做了字符串特判，并直接提交进了运行时核心路径。 |
| 根因 | 接口 cast 问题排查时把临时调试代码直接写进了共享 bind helper，没有收口到测试、条件编译或显式 debug flag。 |
| 影响 | runtime bind 的行为开始依赖测试命名约定，生产代码只要碰巧包含相同子串就会触发额外 Display 日志；同时核心转型路径被测试场景耦合，后续维护者难以判断哪些日志是通用诊断、哪些只是一次性排查遗留。 |

### 发现 18：公开的 actor 查询 helper 不校验 world context，错误处理契约弱于同文件 spawn API

| 项目 | 内容 |
|------|------|
| 维度 | A / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp` |
| 行号 | 166-253；317-446 |
| 描述 | `SpawnActorFromMeta()`、`SpawnActor()`、`SpawnPersistentActor()` 在进入真正的 spawn 前都会调用 `GEngine->GetWorldFromContextObject(...)`，并在 `World == nullptr` 时统一 `FAngelscriptEngine::Throw("Invalid World Context")` 返回。反过来，公开的 `GetAllActorsOfClass(?&)`、`GetAllActorsOfClass(UClass, ?&)`、`GetAllActorsOfClassWithTag(FName, ?&)` 只验证数组 `TypeId` 和类层级，随后就把 `FAngelscriptEngine::TryGetCurrentWorldContextObject()` 直接传给 `UGameplayStatics::*`，没有任何 world-context null check，也没有脚本侧一致的错误消息。仓库内对这三条查询 bind 没有任何自动化覆盖。 |
| 根因 | `Bind_AActor.cpp` 在同一文件里对“需要 world context 的 helper”采用了两套错误处理标准：spawn 路径做了前置校验，而查询路径把上下文合法性完全下沉给 `UGameplayStatics`。 |
| 影响 | 相同的“当前没有可用 world context”错误，在 actor bind 里会表现成两种完全不同的失败语义：spawn API 明确抛脚本异常，查询 API 则不会给出统一的 bind 级诊断。结果是脚本作者必须记忆具体 helper 的失败模式，问题定位也会从脚本层退回到底层 UE helper 行为。 |

### 发现 19：`IsA` 与 `ImplementsInterface` 对空 class 参数给出相反失败语义，`UObject` 类型判断 API 已经失去一致性

| 项目 | 内容 |
|------|------|
| 维度 | A / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceImplementTests.cpp` |
| 行号 | 88-106；175-247 |
| 描述 | 同一段 `UObject` 基础 bind 里，`IsA(const UClass Class)` 在 `Class == nullptr` 时会 `FAngelscriptEngine::Throw("Class passed in to IsA was nullptr.")`；而紧接着新增的 `ImplementsInterface(const UClass InterfaceClass)` 对 `Object == nullptr` 或 `InterfaceClass == nullptr` 都只是直接 `return false`。两者都是“对象对某个 `UClass` 的类型断言”，但空参数的错误语义完全不同：一个把它视为调用错误，另一个把它吞成普通的“不实现接口”。现有 `AngelscriptInterfaceImplementTests.cpp` 只覆盖 `UIDamageableImplCheck::StaticClass()` 的 happy path，没有覆盖空 class 或 lookup 失败路径。 |
| 根因 | `ImplementsInterface` 作为新补的 helper 走了“方便返回 bool”的最短路径，没有沿用同文件已有的 null-parameter 诊断规范。 |
| 影响 | 当脚本先通过 `FindClass` / `StaticClass` / 热重载后的 class lookup 得到空接口类时，`ImplementsInterface` 会把“绑定输入无效”伪装成“对象不实现接口”，掩盖真正的配置或查找错误；而同一调用点换成 `IsA` 又会直接抛脚本异常，API 学习成本和调试成本都上升。 |

---

## 分析 (2026-04-08 03:08)

### 发现 20：`CopyScriptPropertiesFrom` 把内部 script-object 赋值原语裸暴露给全部 `UObject`，没有任何类型或空值保护

| 项目 | 内容 |
|------|------|
| 维度 | A / D |
| 严重度 | Critical |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp` |
| 行号 | 128-132 |
| 描述 | `CopyScriptPropertiesFrom(const UObject OtherObject)` 直接执行 `*(asIScriptObject*)Object = *(asIScriptObject*)OtherObject;`。这条方法挂在 `Bind_UObject_Base` 上，因此对所有 `UObject` 都可见，但实现里既不检查 `Object` / `OtherObject` 是否为 `nullptr`，也不验证双方是否真的是 script object、是否属于兼容 class。扫描 `Plugins/Angelscript/Source/AngelscriptTest` 与运行时代码对 `CopyScriptPropertiesFrom` 的引用，唯一命中就是这条 bind 实现，本仓库没有任何回归测试覆盖其 native object、空 handle 或跨 class 调用。 |
| 根因 | 绑定层把本应只服务 script-instance 内部复制的低层 `asIScriptObject` 赋值语义，直接包装成了通用 `UObject` API，但没有同步附带 script-type gate、null guard 和类兼容性检查。 |
| 影响 | 一旦脚本对 native `UObject`、空对象，或者布局不兼容的两个 script class 调用该 helper，就会在绑定层做未定义的指针重解释和对象覆盖。结果不是普通脚本异常，而是内存破坏、随机崩溃或把错误状态延后到更远的运行时路径。 |

### 发现 21：`GetScriptTypeDeclaration()` 只认 `UASClass`，与当前 `FindClass("AActor")` 的 native lookup 契约无法往返

| 项目 | 内容 |
|------|------|
| 维度 | B / C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp` |
| 行号 | 299-302，519-531；28-32；39-52；223-246 |
| 描述 | 当前分支新增的 `UClass::GetScriptTypeDeclaration()` 只有在 `Cast<UASClass>(Class) != nullptr` 时才返回 `PrefixCPP + Name`，对 native `UClass` 一律返回空串。与此同时，同仓库的全局 `FindClass(const FString& Name)` 明确用 `FAngelscriptType::GetBoundClassName(Class)` 做匹配，而 `GetBoundClassName()` 对任意 `UClass` 都返回 `Class->GetPrefixCPP() + Class->GetName()`。测试也已经把 `FindClass("AActor")` 当作兼容契约固定下来，但 `GetScriptTypeDeclaration()` 的现有覆盖只验证 script class `UBindingSourceMetadataCarrier` 的 happy path，没有任何 native class round-trip 用例。 |
| 根因 | 新补的 `UClass` 元数据 helper 采用了“script class 才有 declaration”的局部实现，但 class lookup surface 已经把 native class 也纳入统一的脚本命名空间；两条 API 没有共用同一套命名来源。 |
| 影响 | 推断：脚本或工具若先通过 `FindClass("AActor")` / `GetAllClasses()` 拿到 native `UClass`，再试图用 `GetScriptTypeDeclaration()` 反向导出可重放名字，会直接丢成空串，无法完成 class-name round-trip。结果是当前新增的 lookup/metadata surface 在 native class 上并不自洽，序列化、调试输出和兼容层代码都需要额外特判。 |

### 发现 22：`FindClass` 已经把“找不到类”公开成 null 结果，但多条 `UClass` helper 仍直接解引用接收者

| 项目 | 内容 |
|------|------|
| 维度 | A / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp` |
| 行号 | 276-326，331-334，519-531；39-52，386-393 |
| 描述 | 当前 `UClass` surface 里，`FindClass(...)` 无论是命名空间版还是全局版，查不到都会直接返回 `nullptr`。但紧邻的 `GetDefaultObject()`、`IsAbstract()`、`GetSuperClass()` 仍然分别执行 `Class->GetDefaultObject()`、`Class->HasAnyClassFlags(...)`、`Class->GetSuperClass()`，没有任何 null guard。反过来，同一代码块里的 `GetSourceFilePath()`、`GetScriptModuleName()`、`IsFunctionImplementedInScript()`、`FindFunctionByName()` 又都做了空值收敛或显式判空，说明 `UClass` helper 在同一文件内已经存在两套接收者契约。现有 `AngelscriptClassBindingsTests.cpp` 只覆盖 `FindClass("AActor")` / `FindClass("ABindingStaticClassActor")` 的 happy path，没有 missing-class 链式调用回归。 |
| 根因 | 绑定层先扩展了 nullable 的 class lookup API，再增补了一组 `UClass` helper，但没有统一规定“`UClass` member 在 null handle 上是抛错、返回空还是允许崩溃”；结果部分 helper 按脚本友好语义实现，部分仍停留在裸 C++ 解引用。 |
| 影响 | 脚本一旦把 `FindClass()`、`__StaticClass()` 或热重载后的 lookup 结果链式接到 `GetDefaultObject()` / `IsAbstract()` / `GetSuperClass()`，查找失败就可能直接跌进绑定层空指针解引用，而不是稳定的脚本级错误。由于 lookup API 本身鼓励这种链式写法，这个风险比传统手写空指针路径更容易被真实业务踩中。 |

### 发现 23：已删除 script class 的 `UClass` 元数据 helper 会给出互相矛盾的身份信息

| 项目 | 内容 |
|------|------|
| 维度 | B / C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp` |
| 行号 | 280-307；1497-1507；4990-5002；223-246 |
| 描述 | 当前 `UClass` metadata helper 对同一个 `UASClass` 的“是否仍是有效脚本类型”没有统一判定。`GetSourceFilePath()` 和 `GetScriptModuleName()` 最终都依赖 `ScriptTypePtr`，当 `CleanupRemovedClass()` 把 removed class 的 `ScriptTypePtr` 置空后，它们会返回空串；但同一块代码里的 `GetScriptTypeDeclaration()` 只要 `Cast<UASClass>(Class)` 成功，就仍然返回 `PrefixCPP + Name`，完全不看 `ScriptTypePtr`。也就是说，已删除脚本类会同时表现成“还有 script declaration”与“已经没有 source/module”。现有 metadata 测试只覆盖正常编译出来的 `UBindingSourceMetadataCarrier`，没有任何 hot-reload/remove 后的 helper 一致性验证。 |
| 根因 | 新增的 `UClass` 元数据 API 是分别围绕具体字段就地实现的，没有抽出“有效 script class”这一统一 predicate；removed-class tombstone 语义因此只在部分 helper 中生效。 |
| 影响 | 推断：编辑器导航、类型面板或脚本诊断若同时读取 declaration、module、source path 来构建 class 描述，会在 removed class 上拿到自相矛盾的数据，进而出现“列表里像是有效脚本类，但点进去没有源码/模块”的半失效状态。由于仓库没有针对 tombstone metadata 的回归，这类问题会长期停留在工具侧偶发异常而不是被测试提前锁住。 |

---

## 分析 (2026-04-08 03:19)

### 发现 24：`UObject::opCast` 会对所有 interface cast 无条件打印 `Display` 日志，runtime 路径残留调试副作用

| 项目 | 内容 |
|------|------|
| 维度 | E / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceCastTests.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_UObject.cpp` |
| 行号 | 188-201；33-103，212-260；139-151 |
| 描述 | 当前 `UObject::opCast` 在判定 `AssociatedClass` 带 `CLASS_Interface` 后，会无条件执行 `UE_LOG(Angelscript, Display, TEXT("UObject::opCast target=..."))`。这意味着任何脚本侧 `Cast<USomeInterface>(Obj)` 都会向常规日志输出一条 `Display` 级消息。仓库里的 `AngelscriptInterfaceCastTests.cpp` 正在大量覆盖 interface cast 成功路径，但没有针对日志副作用的约束；UEAS2 对应实现完全没有这段日志。 |
| 根因 | 接口 cast 调试代码在本地分支里直接留在共享 `opCast` helper 中，且没有收口到条件编译、debug flag 或一次性诊断开关。 |
| 影响 | interface cast 一旦出现在常规 gameplay 或自动化里，就会把每次转型都升级成日志 I/O，带来日志噪音、自动化输出污染以及不必要的性能开销。由于这条日志挂在通用 cast primitive 上，业务层无法局部规避。 |

### 发现 25：`GetCurrentWorld()` 丢失 `no_discard` 返回值契约，world helper 的误用提示弱于 UEAS2

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | Low |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UWorld.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_UWorld.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 行号 | 38-41；35-38；`rg -n 'GetCurrentWorld\\(|__WorldContext\\('` 未命中 |
| 描述 | 当前分支把 `GetCurrentWorld()` 绑定成普通返回值函数；UEAS2 同一入口是 `UWorld GetCurrentWorld() no_discard`。因此脚本现在可以静默丢弃当前 world handle，而不会得到返回值未使用的契约提示。扫描 `Plugins/Angelscript/Source/AngelscriptTest` 也没有发现任何 `GetCurrentWorld` / `__WorldContext` 相关回归。 |
| 根因 | `Bind_UWorld.cpp` 在把 world-context surface 从 UEAS2 迁到当前运行时时，只保留了函数体，没有把返回值语义注解和对应测试一起迁入。 |
| 影响 | world helper 最常见的误用是“调用了当前上下文查询，但忘了接收返回 world”。当前分支里这类错误会完全静默，直到后续逻辑在别处表现成空上下文或未生效行为，排查点会偏离真正的问题。 |

### 发现 26：`__CreateLiteralAsset` 作为公开 global bind 暴露，但缺少 `AssetClass == nullptr` 防线

| 项目 | 内容 |
|------|------|
| 维度 | A / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 行号 | 607-686；4116-4119；`rg -n '__CreateLiteralAsset|__PostLiteralAssetSetup'` 未命中 |
| 描述 | `__CreateLiteralAsset(UClass AssetClass, const FString& Name)` 被注册成全局函数，但实现中没有任何 `AssetClass == nullptr` 判定。它后续会直接走 `ExistingObject->IsA(AssetClass)`、错误消息里的 `AssetClass->GetName()`，以及软重载路径上的 `AssetClass->GetDefaultObject()`。同文件里公开的 `NewObject(...)` 在进入主体逻辑前会先 `Throw("Class was nullptr.")`，说明 bind 层本来有统一的 class-null 诊断模式。预处理器生成代码虽然始终传具体 `{Type}`，且会在返回值为 null 时提前 `return nullptr`，但这只是调用方自律，不是 helper 自身的安全性。 |
| 根因 | literal asset helper 设计上依赖预处理器生成的受控调用路径，却仍以普通 global bind 形式暴露；迁移时没有像 `NewObject`、`SpawnActor` 一样把参数校验固化到 helper 内部。 |
| 影响 | 一旦脚本直接调用这个 `__` helper，或后续生成器出现空 class 漏洞，失败模式会从可诊断的脚本异常退化成绑定层空指针解引用或更底层的 `NewObject` 崩溃。由于测试目录里完全没有覆盖这组 helper，这条隐式前提目前没有自动化保护。 |

---

## 分析 (2026-04-08 03:31)

### 发现 27：手写 world-context bind 没有标记 `asTRAIT_USES_WORLDCONTEXT`，现成的编译期/运行期保护被整体绕开

| 项目 | 内容 |
|------|------|
| 维度 | A / B / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UWorld.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_compiler.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_context.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` |
| 行号 | 166-253，317-466；33-41；633；353-358；18132-18137；5164-5178；674-703 |
| 描述 | 当前 `SpawnActorFromMeta()`、`SpawnActor()`、`SpawnPersistentActor()`、`GetAllActorsOfClass*()`、`GetAllActorsOfClassWithTag()`、`__Actor_GetAllByClass()`、`__WorldContext()`、`GetCurrentWorld()` 都直接依赖 `FAngelscriptEngine::TryGetCurrentWorldContextObject()` 或 `GEngine->GetWorldFromContextObject(...)`，但这些 bind 注册后没有任何 `FAngelscriptBinds::SetPreviousBindRequiresWorldContext(true)`。与此同时，bind core 明确提供了这条 trait API；编译器在 `as_compiler.cpp` 会对 `asTRAIT_USES_WORLDCONTEXT` 发出“不要在 constructor/default statement 里调用”的警告，运行时在 `as_context.cpp` 还会阻止 `BlueprintThreadSafe`/invalid-world-context 场景继续执行。也就是说，这批手写 bind 正在绕开插件自己已经实现好的 world-context 安全护栏。仓库测试只覆盖了 UHT 生成签名的 world-context trait 行为，没有覆盖这些手写 bind。 |
| 根因 | 运行时从 `CurrentWorldContext` 变量迁到 `TryGetCurrentWorldContextObject()` 辅助函数后，手写 global bind 继续直接读取 ambient world context，但没有把“函数依赖 world context”这层元数据同步写回 Angelscript trait 系统。 |
| 影响 | 这些 API 现在会从“编译期可预警、运行期可提前阻止”的 world-context 依赖，退化成普通函数调用。结果是脚本可以在 constructor/default statement、`BlueprintThreadSafe` 或无 world 的对象上下文里调用它们，而不是被 trait 机制提前拦下；真正暴露出来的将是更晚的 `Invalid World Context`、查询空结果，甚至直接进入有副作用的 spawn/query 路径。 |

### 发现 28：`__Actor_GetAllByClass` 已经没有真实生成路径，却仍以公开 global bind 形式暴露了一个无校验后门

| 项目 | 内容 |
|------|------|
| 维度 | A / B / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 行号 | 405-409；1176-1184 |
| 描述 | 当前分支公开注册了 `void __Actor_GetAllByClass(UClass Class, ?& OutActors)`，实现只有一行 `UGameplayStatics::GetAllActorsOfClass(...)`，没有任何数组类型检查、`ActorClass == nullptr` 检查、`ActorClass->IsChildOf(AActor)` 检查，也没有与同文件其它 actor 查询 helper 一致的 bind 级错误消息。对照源码检索，这个名字在仓库里只剩两个命中：一个是这里的 bind 定义，另一个是预处理器里已经整段注释掉的旧生成代码；`Plugins/Angelscript/Source/AngelscriptTest` 下没有任何覆盖。说明它已经失去真实 codegen 调用方，却仍对脚本公开可见。 |
| 根因 | actor statics codegen 曾经打算生成 `ClassName::GetAll(...)` 包装，但相关预处理器代码已被注释掉；配套的底层 `__Actor_GetAllByClass` bind 没有跟着回收，于是变成了一个遗留的公开内部接口。 |
| 影响 | 一旦脚本或后续生成器直接调用这个 `__` helper，就会绕过 `GetAllActorsOfClass(...)` 公开接口已经做好的参数验证和一致错误处理，失败模式退化为底层 `UGameplayStatics` 行为。更糟的是，这条 surface 现在既不被正常代码生成使用，也没有测试约束，属于会长期漂浮在 API 面上的未治理后门。 |

### 发现 29：`__PostLiteralAssetSetup` 作为公开 global bind 暴露，但对 `Asset == nullptr` 没有任何保护

| 项目 | 内容 |
|------|------|
| 维度 | A / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 行号 | 689-697；4116-4119 |
| 描述 | `__PostLiteralAssetSetup(UObject Asset, const FString& Name)` 被注册成普通全局函数，但实现里直接把 `Asset` 解引用给 `Asset->GetName()`，没有任何空值检查或统一异常。当前预处理器生成的 literal asset getter 确实会先判断 `__CreateLiteralAsset(...)` 返回值，只有非空时才继续执行 `__PostLiteralAssetSetup(...)`；但这只是唯一已知调用方的自律，helper 自身并不安全。源码检索显示这个名字在仓库里只有预处理器生成模板和 bind 定义两个命中，测试目录没有覆盖。 |
| 根因 | literal asset 初始化被拆成 `__CreateLiteralAsset` 与 `__PostLiteralAssetSetup` 两段内部 helper 后，第二段继续以公开 global bind 形式暴露，却没有像 `NewObject`/`SpawnActor` 一样在 helper 内部固化参数校验。 |
| 影响 | 一旦脚本直接调用这条 `__` helper，或者未来 codegen 在 `__Init_{Name}` 后引入空对象路径，失败模式就会从可诊断的脚本异常退化成绑定层空指针解引用。由于当前仓库没有任何自动化约束这条 helper 的前置条件，这种问题只会在真实业务第一次踩中时暴露。 |

---

## 分析 (2026-04-08 03:41)

### 发现 30：`FJsonArray` 仍保留 bool/array/object 构造实现，但绑定面把这三类入口全部丢掉了

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Json.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_Json.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Bindings/` |
| 行号 | 155-165，188，602-616；650-657，805；`rg -n 'FJsonArray|AddBoolean|AddArray|AddObject' Plugins/Angelscript/Source/AngelscriptTest/Bindings` 未命中 |
| 描述 | 当前 `FJsonValueArrayContainer` 仍实现了 `AddBoolean(bool)`、`AddArray(const FJsonValueArrayContainer&)`、`AddObject(const FJsonObjectContainer&)`，而且同文件还留着 “expand `FJsonValueArrayContainer` to support every value create/access method” 的 TODO。但真正注册到脚本侧的 `FJsonArray` 只有 `Empty()`、`AddString()`、两个 `AddNumber()`、`Num()`、`GetValueAt()`。UEAS2 对同一 bind block 明确还注册了 `AddBoolean`、`AddArray` 和 `AddObject`。 |
| 根因 | 迁移 `Bind_Json.cpp` 时只保留了最小可用的 string/number array surface，没有把容器层已经存在的 bool、嵌套 array、嵌套 object 入口一并重新注册。 |
| 影响 | 当前脚本无法直接用 bind API 组装包含 bool、嵌套数组或对象元素的 `FJsonArray`，只能退回到更底层的 `FJsonValue` 拼装或字符串 round-trip。这个缺口相对 UEAS2 是实打实的 API 覆盖回退，而且 `Bindings` 测试目录里没有任何回归会把它提前暴露出来。 |

### 发现 31：`FJsonObject` 的 primitive `TryGet*Field` 被整组裁掉，只剩抛错 getter 和 object/array 的 try-get，错误处理语义已经分叉

| 项目 | 内容 |
|------|------|
| 维度 | A / C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Json.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_Json.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Bindings/` |
| 行号 | 638-665；691-705；`rg -n 'FJsonObject|TryGetStringField|TryGetNumberField|TryGetBoolField' Plugins/Angelscript/Source/AngelscriptTest/Bindings` 未命中 |
| 描述 | 当前 `FJsonObject` surface 里，primitive 字段只暴露了会抛错的 `GetStringField()`、`GetNumberField()`、`GetBoolField()`；“非抛错”风格只剩 `TryGetObjectField()` 和 `TryGetArrayField()`。UEAS2 在同一区块还额外注册了 `TryGetStringField()`、`TryGetNumberField()`、`TryGetBoolField()` 三个 overload。也就是说，当前分支把 object/array 字段保留成 probe-style API，却把 string/number/bool 字段强制收口成异常式读取。 |
| 根因 | `Bind_Json.cpp` 迁移时删掉了 primitive `TryGet*Field` 绑定和对应实现，只留下 throwing getter 与复合类型的 try-get helper，导致 JSON field access surface 失去对称性。 |
| 影响 | 需要“字段不存在或类型不匹配时返回 false 而不是抛错”的脚本，现在对 primitive JSON 字段只能手写 `GetField()` + `FJsonValue::TryGet*()` 绕路，或者承受与 object/array 字段完全不同的失败语义。结果不仅破坏 UEAS2 兼容性，也让同一个 `FJsonObject` API 家族内部出现了不一致的错误处理契约。 |

### 发现 32：`AActor` 组件查询 helper 回退成非 `const` 接口，并丢掉 `no_discard`，可调用上下文和返回值契约都弱于 UEAS2

| 项目 | 内容 |
|------|------|
| 维度 | B / C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UActorComponent.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Actor.h`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_UActorComponent.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/AngelscriptBinds/Bind_Actor.h`<br>`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp` |
| 行号 | 351-357，385-387；9-18，32-35；332-338，366-368；9-18，32-35；132-146 |
| 描述 | 当前分支把 `FAngelscriptActorBinds::GetComponent`、`GetAllComponents`、`GetComponentFromMeta`、`GetAllComponentsGeneric` 的 actor 参数都改成了 `AActor*`，并据此把脚本可见声明一起回退成非 `const`：`AActor::GetComponent(...)` 不再带 `const no_discard`，`AActor::GetAllComponents(...)` 也失去了 `const`，component namespace 下的 `ClassName::Get(const AActor Actor, ...)` 同样去掉了 `no_discard`。UEAS2 对应头文件和 bind 声明保持的是 `const AActor*` + `const` method，并对 `GetComponent` / `ClassName::Get` 标了 `no_discard`。现有测试只覆盖了 `GetOwner().GetComponent(...)` 这类可变 actor happy path，没有任何 const-context 或“忽略返回值”回归。 |
| 根因 | 运行时移植时把本质上只读的组件查询 helper 与会修改状态的 `GetOrCreateComponent` / `CreateComponent` 统一到了可变 `AActor*` 签名，随后脚本声明也跟着一起放宽，导致读写边界和返回值契约同时退化。 |
| 影响 | 这组 API 现在不能再无缝用于 `const AActor` 调用场景，UEAS2 下合法的只读查询代码会在当前分支遇到签名不匹配；同时忽略 `GetComponent` / `ClassName::Get` 返回值也不会再收到 `no_discard` 级别的误用提示。结果是组件查询 surface 的 const-correctness 和结果契约同时变弱，而现有自动化并没有覆盖这类回退。 |

### 发现 33：`FJsonObject` 的 number/bool getter 在类型错误路径会返回未初始化栈值

| 项目 | 内容 |
|------|------|
| 维度 | A / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Json.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_Json.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Bindings/` |
| 行号 | 274-286，306-318；4912-4923；287-299，332-344；`rg -n 'FJsonObject|GetNumberField|GetBoolField' Plugins/Angelscript/Source/AngelscriptTest/Bindings` 未命中 |
| 描述 | 当前 `GetNumberField()` 在进入读取路径后声明的是 `double Number;`，`GetBoolField()` 声明的是 `bool bBool;`；如果字段类型不匹配，代码只会调用 `TypeErrorMessage(...)` 然后继续 `return Number` / `return bBool`。与此同时，`FAngelscriptEngine::Throw()` 的实现并不会 C++ 抛异常或中断当前函数，只是给 active execution/context 记录异常状态后直接返回。UEAS2 对应代码在同样位置保留了 `double Number = 0.0;` 和 `bool bBool = false;` 的初始化，因此即使进入错误路径，也不会把未定义栈值回传给脚本。 |
| 根因 | `Bind_Json.cpp` 迁移时去掉了 primitive getter 的默认初始化，但沿用的 `Throw()` 机制本身是“记录异常后返回”，不是强制控制流中断；两者叠加形成了未初始化返回值漏洞。 |
| 影响 | 一旦脚本把字符串/object/array 字段误读成 number 或 bool，当前分支不仅会记一条异常，还可能把随机栈值作为返回结果继续向上传播。结果是同一类 JSON 类型错误会表现成“有异常 + 不稳定返回值”的双重故障，调试时比稳定返回 `0/false` 更难定位，而且测试目录里没有任何绑定回归锁住这条路径。 |

---

## 分析 (2026-04-08 03:52)

### 发现 34：`GetAllActorsOfClass*` 在无效 world-context 时退化成 UE warning + 空结果，和同文件 spawn helper 的异常语义不一致

| 项目 | 内容 |
|------|------|
| 维度 | A / B / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Private/GameplayStatics.cpp`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Private/UnrealEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 行号 | 317-446；1097-1183；14052-14075；`rg -n 'GetAllActorsOfClassWithTag|GetAllActorsOfClass\\(|Invalid World Context' Plugins/Angelscript/Source/AngelscriptTest` 未命中 |
| 描述 | 当前 `GetAllActorsOfClass(?& OutActors)`、`GetAllActorsOfClass(UClass, ?& OutActors)`、`GetAllActorsOfClassWithTag(FName, ?& OutActors)` 在完成数组类型检查后，直接把 `FAngelscriptEngine::TryGetCurrentWorldContextObject()` 传给 `UGameplayStatics::GetAllActorsOfClass*`。而 Unreal 原生实现内部使用的是 `GEngine->GetWorldFromContextObject(..., EGetWorldErrorMode::LogAndReturnNull)`，world context 为空时只写 warning 并返回空数组。对比同文件的 `SpawnActorFromMeta()` / `SpawnActor()` / `SpawnPersistentActor()`，后者都会先显式检查 world，再通过 `FAngelscriptEngine::Throw("Invalid World Context")` 中止。也就是说，AActor bind 在同一文件内已经出现“查询 helper 静默退化、spawn helper 明确抛错”的错误处理分叉。 |
| 根因 | actor 查询 bind 直接复用了 `UGameplayStatics` 的默认 world-context 行为，没有像 spawn 系列 helper 一样把 ambient world lookup 收口为统一的 Angelscript 异常。 |
| 影响 | 脚本在 constructor、无 world 的工具上下文或错误的 engine scope 中调用 `GetAllActorsOfClass*` 时，不会得到和 spawn helper 一致的脚本异常，而是只看到空结果或底层 warning。调用方因此更难区分“世界里确实没有 actor”和“world context 根本无效”，而仓库当前也没有自动化锁住这条语义。 |

### 发现 35：`Math` 命名空间丢失整组 `int64/uint64` overload，当前测试源码已经直接依赖这些签名

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FMath.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_FMath.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathAndPlatformBindingsTests.cpp` |
| 行号 | 369-393；386-418；119-127 |
| 描述 | 当前 `Bind_FMath.cpp` 只绑定了 `Abs/Sign/Min/Max/Square` 的 `float64`、`float32`、`int32`（以及 `Min/Max/Square` 的 `uint32`）版本，没有 `int64` / `uint64` overload。UEAS2 同一段代码明确继续绑定了 `Abs(int64)`、`Sign(int64)`、`Min/Max(int64, int64)`、`Min/Max(uint64, uint64)`、`Square(int64)`、`Square(uint64)`。更直接的是，当前仓库自己的 `AngelscriptMathAndPlatformBindingsTests.cpp` 仍然在脚本 smoke test 里写了 `Math::Abs(int64(-7))`、`Math::Sign(int64(-7))`、`Math::Min/Max(int64(...))`、`Math::Square(int64(9))`。 |
| 根因 | `Bind_FMath.cpp` 移植时只保留了 32-bit 和浮点的基础 overload，没有把 UEAS2 已补齐的宽整数数学 surface 一起迁入，而测试侧的宽整数用例没有同步下调。 |
| 影响 | 宽整型脚本会在 `Math` 命名空间上直接遇到签名缺口，表现为已有 UEAS2 代码无法平移、现有脚本测试源码与实际 bind surface 脱节。由于缺的是最基础的数值 overload，这个问题会影响通用算法代码，而不是局限在某个小众 subsystem。 |

### 发现 36：`UAssetManager` 丢失 `LoadPrimaryAssetsWithType(...)` 异步入口，按类型批量预加载能力回退

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UAssetManager.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_UAssetManager.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 行号 | 83-112；90-141；`rg -n 'UAssetManager|LoadPrimaryAssetsWithType|PrimaryAssetType|PrimaryAssetId' Plugins/Angelscript/Source/AngelscriptTest` 未命中 |
| 描述 | 当前 `Bind_UAssetManager.cpp` 只保留了 `LoadPrimaryAsset(...)` 和 `LoadPrimaryAssets(...)` 两条按 `FPrimaryAssetId` 驱动的异步加载入口。UEAS2 同文件额外提供了 `AssetManager_LoadPrimaryAssetWithType(...)` helper，并把 `void LoadPrimaryAssetsWithType(FPrimaryAssetType PrimaryAssetType, ... OptionalFinishedCallbackFunctionName, OptionalCanceledCallbackName)` 直接绑定到脚本侧。当前分支整个按 `FPrimaryAssetType` 发起批量加载的 surface 已经缺失。 |
| 根因 | 资产管理 bind 迁移时只保留了“按具体 asset id 加载”的最小 surface，没有把 UEAS2 里已经补上的 “按 primary asset type 统一预加载 + 回调” 入口一起迁入。 |
| 影响 | 需要按类型预热资源的脚本现在只能先自己枚举 `FPrimaryAssetId` 列表，再退回 `LoadPrimaryAssets(...)`，失去了原生的 callback/cancel delegate 链路封装。仓库测试目录里也没有任何 `UAssetManager` bind 覆盖，因此这类回退不会被自动化及时发现。 |

### 发现 37：`FPrimaryAssetId` 丢失 `(FPrimaryAssetType, FName)` 构造器，剩余字符串构造绑定还残留明显的迁移错位

| 项目 | 内容 |
|------|------|
| 维度 | C / B / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UAssetManager.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_UAssetManager.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 行号 | 29-37；306-323；30-44；`rg -n 'FPrimaryAssetId|PrimaryAssetType' Plugins/Angelscript/Source/AngelscriptTest` 未命中 |
| 描述 | 当前 `FPrimaryAssetId` 只绑定了 `void f(const FString& InString)` 这一条构造器，没有 UEAS2 的 `void f(const FPrimaryAssetType& InType, const FName& InName)`。同时，现存这条构造器的 lambda 首参数还写成了 `FPrimaryAssetType* Address`，而不是 `FPrimaryAssetId* Address`，说明这段绑定代码在移植时连目标类型名都没有整理干净。 |
| 根因 | `PrimaryAsset` 相关构造 surface 在迁移过程中被半途收缩，只留下字符串解析构造，而实现代码又直接复制了上一段 `FPrimaryAssetType` 绑定模板，导致类型名与目标构造类发生错位。 |
| 影响 | 脚本无法再用强类型的 `(Type, Name)` 组合构造 `FPrimaryAssetId`，只能依赖字符串拼装/解析，既损失可读性也提高拼写错误概率。当前仓库没有任何绑定测试覆盖 `FPrimaryAssetId`，因此这类构造器回退和实现错位都缺乏自动化约束。 |

### 发现 38：`USceneComponent` 旧式 child-query 没再受 `DeprecateOldActorGenericMethods` 控制，且只读查询签名退化成非 `const`

| 项目 | 内容 |
|------|------|
| 维度 | B / C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_USceneComponent.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_USceneComponent.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 行号 | 23-110；23-113；`rg -n 'GetChildrenComponentsByClass|GetChildComponentByClass|DeprecateOldActorGenericMethods' Plugins/Angelscript/Source/AngelscriptTest` 未命中 |
| 描述 | 当前 `Bind_USceneComponent.cpp` 无条件公开 `void GetChildrenComponentsByClass(UClass ComponentClass, bool bIncludeAllDescendants, ?& OutChildren)`，文件内不再读取 `DeprecateOldActorGenericMethods`，也没有 `DeprecatePreviousBind(...)`。UEAS2 对应实现会在该配置为 `Disallowed` 时整段不注册，在 `Deprecated` 时打 deprecated 提示。同时，当前 `GetChildComponentByClass(TSubclassOf<USceneComponent> ComponentClass)` 也失去了 UEAS2 的 `const` method 约束。 |
| 根因 | scene component child-query bind 在迁移时脱离了 UEAS2 里统一的“旧式 generic/out-array API 逐步收口”策略，只保留了永远可见的旧接口和放宽后的可变签名。 |
| 影响 | 插件内部的迁移策略已经出现新的分叉：actor 旧 generic API 至少还有部分历史分析和配置讨论，而 scene component 对应 surface 现在是永久常驻且不可配置的。脚本侧既拿不到一致的 deprecated/disallowed 反馈，也失去了 `const USceneComponent` 场景下的只读查询契约。 |

---

## 分析 (2026-04-08 04:05)

### 发现 39：`UStruct` 哈希能力判定失去早期 fallback，带 `Hash()` 的 script struct 不能再作为 `TSet` / `TMap` key 通过绑定门禁

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UStruct.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSet.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_UStruct.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptContainerBindingsTests.cpp` |
| 行号 | 227-230；91-96；109-113；71-72，108-112；224-236；157-160，249-254 |
| 描述 | 当前 `FUStructType::CanHashValue()` 只认 `GetOps(Usage) != nullptr && Ops->HasGetTypeHash()`。而 `TSet` / `TMap` 的 property gate 会在建类型时直接要求 `Usage.SubTypes[0].CanHashValue()` 为真。对照 UEAS2，同一位置原本还有一层 `Usage.ScriptClass->GetMethodByDecl("uint32 Hash() const")` fallback，并明确写注释说明 `CanCreateProperty()` 调用阶段 `ICppStructOps` 仍可能是空。与此同时，当前 `ASStruct.cpp` 仍然会在 `HashFunction != nullptr` 时把 `HasGetTypeHash` 写进 fake vtable，说明 runtime hash 支撑并没有删除，删掉的是“在 ops 尚未准备好之前识别 script struct 可哈希”的这一步。当前绑定测试也只覆盖了 `TSet<FName>` / `TMap<FName, ...>`，没有任何 script struct key 回归。 |
| 根因 | `Bind_UStruct.cpp` 迁移时保留了最终依赖 `ICppStructOps` 的哈希实现，但把 UEAS2 为脚本 struct 初始化时序专门补的早期 fallback 删掉了，导致 `TSet` / `TMap` 的类型可绑定判定和 `ASStruct` 的 runtime 能力写入不再衔接。 |
| 影响 | 定义了合法 `uint32 Hash() const` 的 script struct，在 current 分支里会出现“运行时有 hash 实现，但在容器/property 入口先被判定为不可哈希”的断层。结果是 UEAS2 可绑定的 `TSet<MyScriptStruct>`、`TMap<MyScriptStruct, ...>` 会直接退化成签名/属性不可用，而且现有自动化不会提前暴露这一回归。 |

### 发现 40：`struct __StaticType_*` / `TStructType` 整条静态 struct 类型链路已被裁掉，但编译器仍保留专门分支

| 项目 | 内容 |
|------|------|
| 维度 | C / B / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UStruct.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_builder.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_compiler.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Preprocessor/AngelscriptPreprocessor.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_UStruct.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/ClassGenerator/AngelscriptClassGenerator.h`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/ClassGenerator/AngelscriptClassGenerator.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp` |
| 行号 | 863-900；1422-1432；2619-2640，4042-4058；2573-2574，6859-6862；11322-11328；828-841；888-892，1540-1593；232-233；4794-4810；453-472 |
| 描述 | 当前 preprocessor 只在 `Class` / `Interface` chunk 下生成 `__StaticType_*`，已经没有 UEAS2 的 `else if (Chunk.Type == EChunkType::Struct)` 分支。对应地，current `Bind_UStruct.cpp` 文件在 1432 行结束，UEAS2 里原本存在的 `BindStaticStruct(...)`、`TStructType<class T>` 模板声明、`TStructType<T>` 方法和 `Bind_StaticStructs` 全部消失；`AngelscriptClassGenerator.cpp` 也只剩 `SetScriptStaticClass(...)` 的填充路径，没有 UEAS2 头文件/实现中的 `SetScriptStaticStruct(...)`。但同一仓库的第三方编译器代码仍然把 `__StaticType_` 当作特殊 global：`as_builder.cpp` 会把这类变量加到 `allScriptGlobalVariables`，`as_compiler.cpp` 在解析类型引用时还会主动拼出 `__StaticType_<TypeName>` 去找全局属性。当前测试则只锁住了 `__StaticType_AActor` 的 class happy path，没有任何 struct static type 回归。 |
| 根因 | current 分支把 “script struct 的静态类型字面量” 从 preprocessor 生成、bind 注册、class generator 回填三段一起裁掉了，但底层编译器对 `__StaticType_` 的语义特判仍然保留，导致工具链能力出现半拆状态。 |
| 影响 | 相比 UEAS2，脚本侧已经失去 `TStructType` / `__StaticType_MyStruct` / 结构体静态类型直达 API 这一整块 surface。结果不是单个 overload 缺失，而是凡是依赖 struct type literal 的旧脚本、codegen 或调试辅助都需要改写，而 current 仓库又没有任何自动化去证明这条链路仍然可用。 |

### 发现 41：`TOptional` 已整体放弃 Unreal intrusive optional state，`TOptional<FName>` 等原生兼容布局不再成立

| 项目 | 内容 |
|------|------|
| 维度 | A / B / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TOptional.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TOptional.h`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_TOptional.cpp`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Core/Public/Misc/Optional.h`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Core/Public/UObject/NameTypes.h`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/CoreUObject/Public/UObject/PropertyOptional.h`<br>`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptNativeScriptTestObject.h`<br>`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptContainerBindingsTests.cpp` |
| 行号 | 34-46，85-103，147-178，317-378，478-583；19-54，59-63；31-49，60-93，135-175，373-453，591-699；52-55；619-620；28-74；92-96；80-85 |
| 描述 | current `Bind_TOptional.cpp` / `.h` 已经把 UEAS2 中所有 `HasIntrusiveUnsetOptionalState()`、`InitializeIntrusiveUnsetOptionalValue()`、`IsIntrusiveOptionalValueSet()`、`ClearIntrusiveOptionalValue()` 分支整组删除，统一退化成“value payload + 尾部 bool”模型：`GetValueSize()` 固定返回 `Align(InnerSize + 1, Alignment)`，`GetIsSetPtr()` 固定取 `&Optional + TypeSize`，`ConstructValue()` / `Reset()` / `Set()` / `EmitReferenceInfo()` 都不再区分 intrusive inner type。对照 Unreal 原生，`Optional.h` 明确说明当 inner type 具备 intrusive unset state 时，`bIsSet` 不占额外空间；`FName` 也明确声明了 `bHasIntrusiveUnsetOptionalState = true`。更关键的是，`FOptionalProperty` 在引擎层仍按 `ValueProperty->HasIntrusiveUnsetOptionalState()` 走两套完全不同的 set/unset 路径，而当前仓库自己又暴露了 `UPROPERTY TOptional<FName> OptionalName` 这种 native interop surface。现有测试只验证了纯脚本内的 `TOptional<FName>` happy path，没有覆盖脚本值和 native `TOptional<FName>` / `FOptionalProperty` 的边界互操作。 |
| 根因 | `TOptional` bind 在 current 分支被简化成单一的“外置 bool 标记”实现，同时移除了 UEAS2 的模板 size calculator 与 intrusive optional 分支；但 Unreal 原生 `TOptional` / `FOptionalProperty` 仍然依赖 inner type 是否具备 intrusive unset state 决定真实布局和状态机。 |
| 影响 | 对 `FName`、`FText`、shared pointer 等带 intrusive unset state 的 inner type，Angelscript 侧 `TOptional<T>` 现在会与原生 `TOptional<T>` / `FOptionalProperty` 使用不同的布局与 unset 语义。结果是脚本与 native 之间传参、属性复制、序列化和 reset/set 行为都可能出现状态错位；若 inner type 还带引用信息，current `EmitReferenceInfo()` 的统一外置 optional schema 也会继续放大 GC 语义分叉。 |

---

## 分析 (2026-04-08 10:25)

### 发现 42：`Bind_WorldCollision.cpp` 整组 trace/overlap helper 对无效 world-context 直接空指针解引用

| 项目 | 内容 |
|------|------|
| 维度 | A / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_WorldCollision.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp` |
| 行号 | 12-16，146-467；289-294 |
| 描述 | `WorldCollision::GetWorld()` 统一通过 `GEngine->GetWorldFromContextObject(FAngelscriptEngine::TryGetCurrentWorldContextObject(), EGetWorldErrorMode::LogAndReturnNull)` 取 world，world-context 无效时会返回 `nullptr`。但同文件后续所有 `System::LineTrace*`、`Sweep*`、`Overlap*`、`Component*`、`Async*`、`QueryTraceData`、`QueryOverlapData`、`IsTraceHandleValid` bind 都直接执行 `WorldCollision::GetWorld()->...`，没有任何空值保护或脚本异常收口。也就是说，这不是单点遗漏，而是整张 collision bind surface 共用的崩溃入口。当前测试里唯一命中的 `WorldCollision` 覆盖只验证脚本片段可以编译，通过 `System::LineTraceTestByChannel` / `SweepSingleByObjectType` / `OverlapMultiByProfile` / `AsyncOverlapByProfile` 做 parity smoke compile，没有执行无效 world-context 场景。 |
| 根因 | 绑定层把 world-context 查找抽到 `WorldCollision::GetWorld()`，但沿用 `LogAndReturnNull` 后没有在调用点补上判空和统一错误处理，导致“记录 warning 并返回 null”的底层语义被上层直接放大成空指针解引用。 |
| 影响 | 在 constructor、editor 工具上下文、缺失 `FAngelscriptEngineScope` 或其它无效 world-context 场景下，任一 collision helper 都可能不是稳定返回 `false`/空结果，也不是脚本异常，而是直接崩在 bind 层。由于当前自动化只锁住编译通过，这条路径会一直潜伏到真实运行时才暴露。 |

### 发现 43：`TSoftObjectPtr::LoadAsync` 对空 soft reference 缺少前置校验，会把空路径直接送进 `LoadPackageAsync`

| 项目 | 内容 |
|------|------|
| 维度 | A / C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSoftObjectPtr.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_TSoftObjectPtr.cpp`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/CoreUObject/Private/Misc/PackageName.cpp` |
| 行号 | 366-368，483-528；486-499；3056-3066 |
| 描述 | 当前 `TSoftObjectPtr<T>::LoadAsync(...)` 只检查 subtype 是否为 actor/component，然后在对象未加载时直接执行 `LoadPackageAsync(*FPackageName::ObjectPathToPackageName(ObjectCopy.ToString()), Delegate, 100)`。当 soft pointer 本身是默认构造或 `Reset()` 后的空值时，`ObjectCopy.ToString()` 会是空串，而 `ObjectPathToPackageName()` 对无分隔符输入会原样返回空串。也就是说，当前分支会把空 package name 直接交给 async loader。UEAS2 在同一函数入口已经补了 `if (Self->IsNull()) { Throw(...); return; }`，明确阻止这条路径。扫描 `Plugins/Angelscript/Source/AngelscriptTest` 对 `LoadAsync(` 的检索没有命中，现有 soft reference 测试只覆盖 `Get()` / 构造 / 赋值 / `EditorOnlyLoadSynchronous()` 的兼容 happy path。 |
| 根因 | soft reference bind 迁移时保留了“已加载直接回调、未加载就异步拉包”的主体流程，但漏掉了 UEAS2 已经补上的 null soft reference 前置校验，导致空路径进入底层加载器。 |
| 影响 | 脚本若对空 `TSoftObjectPtr` 调用 `LoadAsync`，当前行为不再是稳定的脚本级错误或立即回调 `null`，而是变成一次空 package name 的异步加载请求，后续只剩底层 loader 的失败路径和日志语义可依赖。这会让调用方难以区分“引用为空”和“资源加载失败”，而且当前仓库没有回归测试锁住这类边界条件。 |

### 发现 44：`FCollisionObjectQueryParams` 丢失 `TArray<EObjectTypeQuery>` 构造器，object-type trace 初始化能力弱于 UEAS2

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FCollisionQueryParams.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_FCollisionQueryParams.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp` |
| 行号 | 339-377；358-367；283-293 |
| 描述 | 当前 `FCollisionObjectQueryParams` 只保留了默认构造、`ECollisionChannel`、`ECollisionObjectQueryInitType` 和 `int32` bitfield 构造器，没有 UEAS2 的 `void f(const TArray<EObjectTypeQuery>& ObjectTypes)`。UEAS2 在该 overload 里会把脚本传入的 `TArray<EObjectTypeQuery>` 转成 `TArray<TEnumAsByte<EObjectTypeQuery>>` 后构造 `FCollisionObjectQueryParams`，这正是脚本侧最接近 Blueprint object-type 列表的输入形态。当前测试也只验证了默认构造出来的 `ObjectQueryParams` 能参与 `SweepSingleByObjectType` / `ComponentOverlapMulti` 的 compile parity，没有覆盖“按对象类型数组构造查询参数”的路径。 |
| 根因 | collision query bind 迁移时保留了低层 bitfield / channel 构造方式，但漏掉了 UEAS2 已补上的高层 object-type array 入口，导致 object-type trace surface 只剩更底层的手工组装方式。 |
| 影响 | 需要按多个 `EObjectTypeQuery` 直接组装查询集合的脚本，现在只能自己维护 bitfield 或循环调用 `AddObjectTypesToQuery(ECollisionChannel)`，无法像 UEAS2 一样一次性从对象类型数组构造。结果是从 Blueprint / UEAS2 迁移过来的 trace 代码会遇到明确的构造器缺口，而现有自动化不会提前提示。 |

### 发现 45：collision `System::*` 手写 bind 全部绕开 `RequiresWorldContext` trait，world-context 安全护栏对 trace API 不生效

| 项目 | 内容 |
|------|------|
| 维度 | B / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_WorldCollision.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` |
| 行号 | 12-16，146-467；633；353-359；675-703 |
| 描述 | `Bind_WorldCollision.cpp` 整组 `System::LineTrace*`、`Sweep*`、`Overlap*`、`Component*`、`Async*`、`QueryTraceData`、`QueryOverlapData`、`IsTraceHandleValid` 都依赖 `WorldCollision::GetWorld()`，而该 helper 明确从 ambient world-context 取 world。与此同时，源码检索显示此文件没有任何 `SetPreviousBindRequiresWorldContext(true)` 调用。bind core 明明提供了这条入口，`AngelscriptBindConfigTests.cpp` 也已经验证被标记的 world-context 函数会写入 `asTRAIT_USES_WORLDCONTEXT` 并得到隐藏参数/trait 行为。换言之，collision bind family 当前是整块绕过插件现成 metadata 护栏在裸跑。 |
| 根因 | hand-written collision helper 直接按“普通全局函数”注册，只复用了 ambient world lookup，没有把“该函数依赖 world context”这层约束同步回 bind trait 系统。 |
| 影响 | 编译器和运行时本来可以基于 world-context trait 在 constructor、default statement 或无效上下文里提前警告/拦截，但对这批 trace API 完全失效。结果是脚本作者看不到 trait 级提示，只会在更晚的运行期撞到 warning、空结果甚至像发现 42 那样的空指针崩溃，而当前测试也没有覆盖这批手写 bind 的 trait 状态。 |

---

## 分析 (2026-04-08 10:49)

### 发现 46：`IsFunctionImplementedInScript()` 在 hot-reload 清理后仍把失效 `UASFunction` 视为“已实现”

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp` |
| 行号 | 303-313，405-430；979-985，1535-1558；4990-5016；235 |
| 描述 | `Bind_UClass_Base` 把 `IsFunctionImplementedInScript(FName)` 和 `FindFunctionByName(FName)` 直接暴露给脚本；其中 `UASClass::IsFunctionImplementedInScript()` 的实现只要 `FindFunctionByName()` 找到 `UASFunction`，并且 `GetOuterUClass()` 非空，就直接返回 `true`。但 `CleanupRemovedClass()` 在脚本类被移除时，会遍历类上的 `UASFunction`，把每个 `Function->ScriptFunction = nullptr`，并不会把这些 `UFunction` 从类上摘掉。结果是 removed class / replaced class 的旧函数对象仍然能被 `FindFunctionByName()` 找到，`IsFunctionImplementedInScript()` 也仍会报告“已实现”，而同文件的 `UFunction` metadata helper 又会因为 `ScriptFunction == nullptr` 返回空字符串或 `-1`。 |
| 根因 | 绑定层把“函数对象仍存在”错误地当成“脚本实现仍有效”；hot-reload 清理只清空了运行时脚本函数指针，没有给 `IsFunctionImplementedInScript()` / `FindFunctionByName()` 增加 tombstone 判定。 |
| 影响 | 热重载或删除脚本函数后，脚本反射层会进入自相矛盾状态：类级 helper 说函数仍由脚本实现，函数级 helper 却已经拿不到 declaration/source 信息。依赖这组 API 做能力探测、调试 UI 或兼容判断的代码，会把失效函数误判成可用实现。现有测试只覆盖 `Type.IsFunctionImplementedInScript("ComputeValue")` 的单纯 happy path，没有任何 reload/remove 之后的回归。 |

### 发现 47：`GetSourceFilePath()` 对 `UASClass` / `UASFunction` 都硬编码返回模块首文件，多文件模块的源导航会稳定指错

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`<br>`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSourceNavigationTests.cpp` |
| 行号 | 280-285，405-410；1497-1507，1535-1545；1279-1286；204-251；69-76 |
| 描述 | `Bind_UClass_Base::GetSourceFilePath()` 和 `Bind_UFunction_Base::GetSourceFilePath()` 最终分别调用 `UASClass::GetSourceFilePath()`、`UASFunction::GetSourceFilePath()`。这两个实现都只要拿到 module，就直接返回 `Module->Code[0].AbsoluteFilename`。但 `FAngelscriptModuleDesc` 明确把脚本源码存成 `TArray<FCodeSection> Code`，也就是一个 module 可以包含多个脚本文件。当前 helper 没有任何按“类/函数实际声明所在 section”定位的逻辑，因此一旦 module 含有多个 `.as` 文件，后出现文件中的类和函数也都会被错误映射到第一个文件。 |
| 根因 | source metadata helper 只有 module 级视角，没有保存或恢复 declaration 到具体 `FCodeSection` 的映射，最后退化成“模块里第一个文件就是源文件”。 |
| 影响 | 编辑器源导航、调试工具和脚本侧 `GetSourceFilePath()` 查询，在多文件模块下会系统性打开错误文件；再叠加 `GetSourceLineNumber()`，得到的是“正确或接近的行号 + 错误的文件”。现有测试只覆盖单文件模块场景：`RuntimeSourceMetadataBindingsTest.as` 和 `AngelscriptSourceNavigationTests` 都把 `__SCRIPT_PATH__` 绑定到唯一脚本文件，没有任何多文件 module 回归。 |

### 发现 48：`GeneratedSourceLineNumber` 已被写入 `UASFunction`，但 `GetSourceLineNumber()` 完全不读取这份缓存

| 项目 | 内容 |
|------|------|
| 维度 | B / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSourceNavigationTests.cpp` |
| 行号 | 123-125；3411-3417；1548-1558；4990-5016；238-245；69-76 |
| 描述 | `UASFunction` 自己就带有 `GeneratedSourceLineNumber` 字段，class generator 在生成每个 `UASFunction` 时也会明确写入 `FunctionDesc->LineNumber + 1`。但 `UASFunction::GetSourceLineNumber()` 的实现从头到尾只读 `ScriptFunction->scriptData->declaredAt`；只要 `ScriptFunction == nullptr`，就立刻返回 `-1`。这意味着当前仓库已经维护了一份独立的行号缓存，却没有任何读取路径。更糟的是，`CleanupRemovedClass()` 在热重载/删除脚本类时会把 `UASFunction::ScriptFunction` 清空，但不会清掉 `GeneratedSourceLineNumber`，于是 getter 仍然丢弃本可继续提供的最后已知行号。 |
| 根因 | 元数据采集链路和元数据读取链路脱节：generator 负责缓存生成时的行号，bind / runtime helper 却仍然只信任 live `asCScriptFunction`。 |
| 影响 | 一旦函数进入“`UASFunction` 还在，但 `ScriptFunction` 已被清空”的状态，脚本和编辑器侧拿到的源行号会无条件退化成 `-1`，即使对象里明明还保留着最近一次生成时的行号。当前测试只覆盖 `ScriptFunction` 仍然有效的正常场景，没有任何 hot-reload/remove 后的 source-line 回归，因此这条断链会长期潜伏。 |

### 发现 49：失效 `UASFunction` 通过 `ProcessEvent` / thunk 调用时会静默 no-op，而不是暴露“脚本函数已失效”

| 项目 | 内容 |
|------|------|
| 维度 | A / C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Actor/AngelscriptScriptSpawnedActorOverrideTests.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Blueprint/AngelscriptBlueprintSubclassRuntimeTests.cpp` |
| 行号 | 311-313；4990-5016；153-156，1931-1945，1971-1975；60，238；147，176 |
| 描述 | `Bind_UClass_Base::FindFunctionByName()` 会把 `UFunction*` 直接返回给脚本/工具层。`CleanupRemovedClass()` 在类移除时只把 `UASFunction::ScriptFunction` 清空，函数对象本身仍留在类上。之后无论走通用 `UASFunctionNativeThunk -> RuntimeCallFunction()` 还是具体的 `UASFunction_NoParams::RuntimeCallFunction()` 等分派实现，代码第一件事都是在 `ScriptFunction == nullptr` 时直接 `return`。也就是说，旧函数句柄不是被标记为无效、不是抛异常、也不是 warning，而是进入“还能找到、还能被调用、但调用体静默不执行”的状态。 |
| 根因 | hot-reload 清理和 runtime dispatch 之间缺少统一的失效协议：清理阶段只置空脚本函数指针，dispatch 阶段则把空指针当成普通早退条件处理。 |
| 影响 | 一旦 UE 侧或脚本侧缓存了旧 `UFunction` 并继续通过 `ProcessEvent` / thunk 调用，外层会看到一次看似成功的调用返回，但函数体根本没有执行。对 RPC、事件桥接、工具按钮和反射调用来说，这比显式失败更难排查。现有 `ProcessEvent` 相关测试都覆盖正常脚本函数 happy path，没有任何 hot-reload/remove 后继续调用旧 `UFunction` 的回归。 |

---

## 分析 (2026-04-08 11:05)

### 发现 50：native subsystem `Class::Get()` 访问器整组丢失 `no_discard`，返回值契约弱于 UEAS2

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Subsystems.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_Subsystems.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemScenarioTests.cpp` |
| 行号 | 48-53，60-65，69-81，85-94，99-118；50-55，62-67，71-83，87-96，101-120；66-245 |
| 描述 | 当前 `Bind_Subsystems.cpp` 给 `UEditorSubsystem`、`UEngineSubsystem`、`UGameInstanceSubsystem`、`UWorldSubsystem`、`ULocalPlayerSubsystem` 暴露的 `Class::Get(...)` 访问器全部是普通返回值签名。UEAS2 同一批绑定声明全部带 `no_discard`。这些 helper 的唯一输出就是 subsystem handle，本地回退后脚本可以静默写出 `UMySubsystem::Get();` 这类丢弃结果的调用，而不会收到编译期契约提示。当前 `AngelscriptSubsystemScenarioTests.cpp` 只验证 subsystem script generation 在本分支仍然失败，没有任何 `Class::Get(...)` 绑定回归。 |
| 根因 | subsystem bind 迁移时保留了 lambda 实现和命名空间结构，但把 UEAS2 已经补上的返回值语义注解一起裁掉了。 |
| 影响 | subsystem 获取 API 从“结果必须使用”的访问器退化成普通函数后，误把查询调用当作有副作用操作的脚本更容易静默通过。对 editor/world/game-instance/local-player subsystem 这类全局入口来说，这会把本可在编译期暴露的调用错误推迟到更晚的运行阶段。 |

### 发现 51：`USubsystem` 的 `NoBlueprintsOfChildren` 保护已被移除，editor 现在会重新暴露不受支持的 subsystem 蓝图入口

| 项目 | 内容 |
|------|------|
| 维度 | B / C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Subsystems.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_Subsystems.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/BaseClasses/ScriptEngineSubsystem.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/BaseClasses/ScriptGameInstanceSubsystem.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/BaseClasses/ScriptWorldSubsystem.h`<br>`Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemScenarioTests.cpp` |
| 行号 | 24-122；125-128；6-7；6-7；7-8；66-245 |
| 描述 | UEAS2 在 `Bind_Subsystems.cpp` 末尾会对 `USubsystem::StaticClass()` 写入 `NoBlueprintsOfChildren` 元数据，源码注释明确说明这是为了阻止基于 Angelscript subsystem class 创建 Blueprint。当前分支已经把这段 editor 保护整块删掉。与此同时，本地的 `UScriptEngineSubsystem`、`UScriptGameInstanceSubsystem`、`UScriptWorldSubsystem` 仍然都是 `UCLASS(Blueprintable, Abstract)`。更重要的是，现有 `AngelscriptSubsystemScenarioTests.cpp` 明确把 “world/game-instance subsystem script generation remains unsupported on this branch” 固定成当前预期，却没有任何测试去约束 editor 不应再暴露 subsystem 蓝图创建入口。 |
| 根因 | subsystem bind 迁移时保留了运行时 `Get()` 访问器生成，但没有把 UEAS2 为 editor authoring surface 加的 `NoBlueprintsOfChildren` 防护一并迁入。 |
| 影响 | current 分支一边在测试里承认 subsystem script generation 仍不支持，一边又移除了 editor 侧禁止创建 subsystem 蓝图的总开关，结果是内容作者更容易创建到插件当前并不打算支持的 subsystem 资产类型。问题不会在创建时被拦住，而会推迟到编译、加载或生命周期阶段才暴露。 |

### 发现 52：UMG style helper 从 `FAppStyle` 退回 `FCoreStyle`，并把可选 brush 查询变成带 warning 的强查找

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UUserWidget.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_UUserWidget.cpp`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/SlateCore/Public/Styling/AppStyle.h`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/SlateCore/Private/Styling/AppStyle.cpp`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/SlateCore/Private/Styling/SlateStyleSet.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleWidgetUmgTest.cpp` |
| 行号 | 93-102，321-331；90-99，309-317；14-19；8-17；299-356；47 |
| 描述 | 当前 `FPaintContext::GetStyleColor()`、`GetStyleBrush()` 和 `FSlateBrush(BrushStyleName)` 构造器都改成直接查 `FCoreStyle::Get()`；UEAS2 对应实现使用的是 `FAppStyle::Get()`，其中 brush 构造器还是 `GetOptionalBrush()`。引擎自己的 `AppStyle.h` 明确写着“core Slate Application Widgets 应改用 `FAppStyle::Get()`，`FCoreStyle::Get()` 访问应被替换”，而 `AppStyle.cpp` 也说明 `FAppStyle` 本身已经会在找不到 app style 时 fallback 到 `FCoreStyle`。因此 current 这次改动不是等价重写，而是把 style 查询强行锁死在 core style。更进一步，`SlateStyleSet.cpp` 显示 `GetBrush()` 在 miss 时会记录 warning 并返回 default brush，而 `GetOptionalBrush()` 则直接返回默认值。现有 widget 示例测试只覆盖 `WidgetBlueprint::CreateWidget(...)`，没有任何 style lookup / brush-name 构造回归。 |
| 根因 | UMG bind 迁移时把 editor/app style surface 简化成了直接读 `FCoreStyle`，同时把原本“可选 style key”的 brush 查询 API 替换成了“缺失即 warning + default brush”的强查找。 |
| 影响 | 脚本 UI 现在无法像 UEAS2 那样跟随当前 app/editor style set 解析 brush/color 名称，许多仅注册在 `FAppStyle` 里的 style key 会退化成 core 默认资源。对不存在的 brush 名称，旧行为是安静返回空 brush，当前行为会额外产生 style warning 并落到 default brush，导致 UI 外观和错误噪声都发生回退。 |

---

## 分析 (2026-04-08 11:18)

### 发现 53：`FMemoryReader::Seek/Skip` 只防上界不防负偏移，脚本可把 archive 定位到 0 之前并触发越界读

| 项目 | 内容 |
|------|------|
| 维度 | A / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FMemoryReader.cpp`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Core/Public/Serialization/MemoryArchive.h`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Core/Public/Serialization/MemoryReader.h`<br>`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.h` |
| 行号 | 38-56；25-28；35-48；48 |
| 描述 | 当前 `Seek(int InPos)` 只拦截 `InPos > TotalSize()`，`Skip(int Count)` 只拦截 `Tell() + Count > TotalSize()`，因此 `Seek(-1)`、`Skip(-1)` 这类负偏移会直接通过。底层 `FMemoryArchive::Seek()` 只是裸写 `Offset = InPos`，没有任何 clamp；而 `FMemoryReader::Serialize()` 只要 `Offset + Num <= TotalSize()` 就会执行 `Memcpy(Data, &Bytes[(int32)Offset], Num)`。这意味着当脚本先把 offset 设成 `-1`，再读 1 byte 时，条件仍会成立，并从 `Bytes[-1]` 开始复制。源码检索 `Plugins/Angelscript/Source/AngelscriptTest` 对 `ReadInt*` / `Seek` / `Skip` 没有绑定回归，唯一命中只是 native `FMemoryReader Reader(Body)` 的调试 helper。 |
| 根因 | bind 层把“越过尾部”当成唯一非法定位条件，但底层 archive 的位置寄存器允许任意 `int64`，没有额外下界保护。 |
| 影响 | 脚本只需一次负偏移定位，就能把后续任意 `ReadInt*` / `ReadBytes` / `ReadAnsiString` 调用推向越界读取路径。由于错误发生在通用内存 reader 内部，最终表现可能是随机数据、崩溃或更远处的序列化异常，而当前仓库没有自动化会提前暴露这个边界条件。 |

### 发现 54：`FMemoryReader` 所有短读路径都只置 error flag，不抛错也不清零，绑定会把未初始化数据直接返回给脚本

| 项目 | 内容 |
|------|------|
| 维度 | A / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FMemoryReader.cpp`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Core/Public/Serialization/Archive.h`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Core/Public/Serialization/MemoryReader.h`<br>`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.h` |
| 行号 | 58-143；1438-1462，1905-1910；35-48；48 |
| 描述 | 当前 `ReadInt8/16/32/64`、`ReadUInt8/16/32/64`、`ReadFloat`、`ReadDouble` 都是“声明一个局部 `Result`，执行 `*reader << Result`，然后直接返回”。`Archive.h` 显示 `uint8` / `int8` 的 `operator<<` 直接走 `Serialize(&Value, 1)`，更大的数值类型在非 byte-swap 场景下也会通过 `ByteOrderSerialize()` 落到同一个 `Serialize()`。而 `FMemoryReader::Serialize()` 在剩余字节不足时只执行 `SetError()`，不会写入输出缓冲。结果是只要脚本读取超出剩余长度，数值 reader 就会把未初始化栈变量返回出去。`ReadBytes(int Count)` 和 `ReadAnsiString(int Count)` 也有同样问题：二者先 `SetNumUninitialized(Count)`，再无条件 `Serialize(...)`；短读时 `Serialize()` 不填充缓冲区，函数却继续把未初始化数组 / 字符串内容返回给脚本。当前文件也没有暴露 `AtEnd()` / `IsError()` 这类补救查询接口。 |
| 根因 | bind 实现把 `FMemoryReader` 的内部 error flag 当成足够的失败语义，但脚本封装层既不在读前检查剩余长度，也不在读后检查 archive error 状态，更没有给返回值安全默认值。 |
| 影响 | 对截断 buffer、错误长度字段或用户可控输入的读取，不会稳定抛出脚本异常，而会把未定义的数字、字节数组或字符串继续传播到上层逻辑。相比“读取失败立刻报错”，这种 silent corruption 更难定位，而且当前仓库没有绑定级测试会在短读时报警。 |

### 发现 55：`FText` 丢失 locale-aware 比较和大小写转换 API，连 `ETextComparisonLevel` 枚举一起退场

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FText.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_FText.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 行号 | 61-113；59-119；`rg -n "CompareTo\\(|EqualTo\\(|ToLower\\(|ToUpper\\(|ETextComparisonLevel" Plugins/Angelscript/Source/AngelscriptTest` 未命中 |
| 描述 | 当前 `Bind_FText.cpp` 只保留了 `ETextIdenticalModeFlags`、`IdenticalTo()` 和基础状态查询；文件里已经完全没有 `ETextComparisonLevel` 枚举，也没有 UEAS2 原本提供的 `CompareTo(...)`、`CompareToCaseIgnored(...)`、`EqualTo(...)`、`EqualToCaseIgnored(...)`、`ToLower()`、`ToUpper()`。UEAS2 对这些入口的绑定仍然完整存在，说明这不是引擎 API 消失，而是当前分支把整组 `FText` lexical / locale-aware surface 收缩掉了。 |
| 根因 | `FText` bind 迁移时只保留了 identity/diagnostic 相关最小集合，没有把比较等级枚举和基于文化规则的比较/大小写变换接口一起迁入。 |
| 影响 | 脚本现在只能用 `IdenticalTo()` 或先 `ToString()` 再退回 `FString` 比较，无法继续表达 UEAS2 已支持的 locale-aware 排序、大小写无关比较和 `FText` 级大小写转换。这样不仅破坏 API parity，也会把原本应保留在 `FText` 层的本地化语义压扁成字符串比较，而当前测试没有任何回归能在这些接口缺失时报警。 |

### 发现 56：`UInputSettings` 重新暴露已废弃的 speech mapping API，并用编译器 warning 压制来维持旧 surface

| 项目 | 内容 |
|------|------|
| 维度 | B / C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UInputSettings.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_UInputSettings.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 行号 | 11-20；11-15；`rg -n "DoesSpeechExist|GetSpeechMappings|Speech" Plugins/Angelscript/Source/AngelscriptTest` 未命中 |
| 描述 | 当前 `Bind_UInputSettings.cpp` 在 `GetActionMappings()` / `GetAxisMappings()` 之后，额外用两段 `PRAGMA_DISABLE_DEPRECATION_WARNINGS` 包住了 `GetSpeechMappings()` 和 `DoesSpeechExist()` 的注册，把已废弃的 speech mapping API 继续暴露给脚本。UEAS2 对应文件已经没有这两条绑定，也不再需要 warning suppression。说明当前分支并不是被动继承引擎弃用，而是主动选择保留一组上游已放弃的输入 surface。 |
| 根因 | 本地 bind 没有跟随 UEAS2 收缩 `UInputSettings` 的过时 speech mapping API，而是通过文件内 warning suppression 维持兼容，避免编译期直接暴露这条技术债。 |
| 影响 | 脚本会继续被鼓励依赖一组已废弃、且参考实现已经下线的输入接口，后续迁移到更现代的 input surface 会更难。同时这条 API 现在既没有测试覆盖，也没有正常的编译期噪声提醒，属于会长期潜伏的过时绑定面。 |

---

## 分析 (2026-04-08 11:33)

### 发现 57：`FString::RemoveAt` 越界路径丢了脚本级边界检查，会直接跌回底层 `RangeCheck`

| 项目 | 内容 |
|------|------|
| 维度 | A / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FString.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_FString.cpp`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Core/Public/Containers/Array.h`<br>`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptUtilityBindingsTests.cpp` |
| 行号 | 231-233；236-244；2109-2112；307-335 |
| 描述 | 当前 `Bind_FString.cpp` 把 `void RemoveAt(int Index, int Count)` 直接绑定成 `String.RemoveAt(Index, Count)`，没有任何越界保护。同文件的 `opIndex` 两个 overload 都会先 `Throw("String index out of bounds.")`，而 UEAS2 在 `RemoveAt` 上也保留了同样的 `IsValidIndex` 检查。底层 `TArray::RemoveAt` 会先执行 `RangeCheck(Index, Count)`，因此脚本若传入非法 index，不会得到一致的脚本异常，而是直接跌回容器层断言/检查路径。现有 `AngelscriptUtilityBindingsTests.cpp` 只覆盖 `"ABCDE"` 上的两个合法 `RemoveAt` happy path，没有任何 out-of-bounds 回归。 |
| 根因 | `FString` bind 迁移时保留了 `RemoveAt` 的最小包装，但漏掉了 UEAS2 已补上的边界检查和与 `opIndex` 对齐的错误处理。 |
| 影响 | 对脚本作者来说，字符串索引 API 现在出现了文件内不一致：读索引越界会稳定报脚本错误，删索引越界却可能直接触发底层 `RangeCheck`。这会把本应在脚本边界收口的输入错误升级成更底层的断言或异常，而且当前自动化不会提前暴露。 |

### 发现 58：`FMath` 丢了“带成功标志/输出参数”的几何 helper，脚本无法再表达判交失败语义

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FMath.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_FMath.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathAndPlatformBindingsTests.cpp` |
| 行号 | 252-258；263-279；130 |
| 描述 | 当前 `Bind_FMath.cpp` 只保留了三个“直接返回交点”的 `LinePlaneIntersection` overload，没有 UEAS2 里的 `bool LinePlaneIntersection(..., float32& T, FVector& Intersection)`；同一区块的 `bool LineExtentBoxIntersection(..., HitLocation, HitNormal, HitTime)` 也一并缺失。UEAS2 这两条 API 分别提供“是否真的与平面相交”的 success flag，以及 swept-box vs box 的完整命中信息。当前仓库测试只覆盖 `FVector PlaneIntersection = Math::LinePlaneIntersection(...)` 的 happy path，没有任何并行于 plane、无交点、或 swept-box 判交回归。 |
| 根因 | `Bind_FMath.cpp` 迁移时保留了返回值最简单的几何 helper，却漏掉了 UEAS2 已补齐的 out-param / bool 风格 overload。 |
| 影响 | 现在脚本只能拿到一个 `FVector` 结果，无法通过绑定层区分“成功求得交点”和“调用本应返回失败”。对 `LinePlaneIntersection` 来说，这会迫使业务自己重写 success 判定；对 `LineExtentBoxIntersection` 来说，则是整条 swept-box helper 直接消失。几何查询 surface 因此明显弱于 UEAS2，而且当前测试不会在这类判交语义回退时报警。 |

### 发现 59：`FNumberFormattingOptions` fluent setter 丢了 `accept_temporary_this`，builder-style temporary 在 current 已失效

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FNumberFormattingOptions.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_FNumberFormattingOptions.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCoreMiscBindingsTests.cpp` |
| 行号 | 35-41；35-41；189-203 |
| 描述 | 当前 `SetAlwaysSign`、`SetUseGrouping`、`SetRoundingMode`、`SetMinimumIntegralDigits`、`SetMaximumIntegralDigits`、`SetMinimumFractionalDigits`、`SetMaximumFractionalDigits` 仍然全部返回 `FNumberFormattingOptions&`，但绑定声明已经没有 UEAS2 的 `accept_temporary_this`。这意味着 `FNumberFormattingOptions().SetAlwaysSign(true).SetUseGrouping(false)`、`FNumberFormattingOptions::DefaultWithGrouping().SetUseGrouping(false)` 这类 builder-style temporary 在 current 不再等价，而 UEAS2 明确允许。现有测试只验证了命名变量 `Options.SetAlwaysSign(...).SetUseGrouping(...)` 和 `DefaultWithGrouping()/DefaultNoGrouping()` 的拷贝使用，没有覆盖 temporary 链式调用。 |
| 根因 | 绑定迁移时保留了 fluent setter 的返回类型，却丢掉了让它们能作用在 temporary receiver 上的脚本调用约束元数据。 |
| 影响 | `FNumberFormattingOptions` 表面上仍像 builder API，但 current 只支持“先命名变量再链式修改”，不能继续无缝复用 UEAS2 的 temporary builder 写法。结果是脚本语法兼容性回退，而且由于现有测试只覆盖 named-variable 场景，这个断口会长期保持未监测状态。 |

---

## 分析 (2026-04-08 11:37)

### 发现 60：`UInputSettings` 唯一命名/存在性查询 helper 整组丢失 `no_discard`，输入配置返回值契约退化

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UInputSettings.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_UInputSettings.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 行号 | 8-9，17-18；8-9，14-15；`rg -n "GetUniqueActionName|GetUniqueAxisName|DoesActionExist|DoesAxisExist" Plugins/Angelscript/Source/AngelscriptTest` 未命中 |
| 描述 | 当前 `Bind_UInputSettings.cpp` 里的 `GetUniqueActionName()`、`GetUniqueAxisName()`、`DoesActionExist()`、`DoesAxisExist()` 全部退化成普通返回值签名。UEAS2 对应四条绑定都明确带 `no_discard`。这四个 helper 的主要价值分别是返回唯一 `FName` 或判断 bool 结果；当前分支去掉返回值契约后，脚本可以静默写出 `Settings.GetUniqueActionName(Name);` 或 `Settings.DoesActionExist(Name);` 这类丢弃结果的调用，而不会得到编译期提示。扫描测试目录也没有发现任何对应绑定回归。 |
| 根因 | `Bind_UInputSettings.cpp` 在迁移输入配置 helper 时，只保留了函数体和参数列表，没有把 UEAS2 已经补上的返回值语义注解一起迁入。 |
| 影响 | 输入映射查询 API 从“调用结果必须被消费”的访问器退化成普通函数后，脚本误把它们当成副作用调用的概率会上升。尤其 `GetUnique*Name()` 这类 helper 一旦结果被忽略，后续仍可能继续使用冲突名称，而当前仓库没有自动化去锁住这组契约回退。 |

### 发现 61：`FMemoryReader` 对负长度读取没有任何前置校验，脚本会直接触发 `TArray` fatal 而不是脚本异常

| 项目 | 内容 |
|------|------|
| 维度 | A / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FMemoryReader.cpp`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Core/Public/Containers/Array.h`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Core/Private/Containers/ContainerHelpers.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 行号 | 128-141；2369-2378；6-10；`rg -n "ReadBytes\\(|ReadAnsiString\\(" Plugins/Angelscript/Source/AngelscriptTest` 未命中 |
| 描述 | 当前 `ReadBytes(int Count)` 和 `ReadAnsiString(int Count)` 都直接把脚本传入的 `Count` 交给 `TArray::SetNumUninitialized(Count)`，没有任何 `Count < 0` 检查。引擎侧 `Array.h` 明确显示 `SetNumUninitialized()` 在 `NewNum < 0` 时会调用 `OnInvalidArrayNum(...)`，而 `ContainerHelpers.cpp` 里的 `OnInvalidArrayNum()` 是 `UE_LOG(..., Fatal, ...)`。因此脚本若传入 `-1` 之类的负长度，不会得到统一的 `FAngelscriptEngine::Throw(...)`，而是直接跌进引擎 fatal 路径。 |
| 根因 | bind 层只把“超出剩余字节”视为异常来源，却完全没有约束 `Count` 这一直接参与容器分配的脚本输入。 |
| 影响 | 负长度读取会把普通脚本参数错误升级成进程级 fatal，而不是可恢复的脚本异常。对工具脚本、调试协议或任何读取外部输入长度字段的代码，这条路径都会显著放大错误半径；当前仓库也没有自动化覆盖这种负长度边界。 |

### 发现 62：`UAssetManager` 基础状态查询入口在 current 被整段注释掉，脚本失去初始化探测与安全获取句柄的标准 API

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UAssetManager.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_UAssetManager.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 行号 | 107-110；138-140；`rg -n "UAssetManager::Get\\(|AssetManager::Get\\(|UAssetManager::IsInitialized|AssetManager::IsInitialized" Plugins/Angelscript/Source/AngelscriptTest` 未命中 |
| 描述 | 当前 `Bind_UAssetManager.cpp` 在 `UAssetManager` namespace 下把 `BindGlobalFunction("bool IsInitialized()"... )` 和 `BindGlobalFunction("UAssetManager Get()"... )` 两条注册直接注释掉了。UEAS2 同位置仍然公开 `IsInitialized() no_discard` 与 `Get() no_discard`。这意味着 current 脚本侧已经没有标准方式先判断 asset manager 是否可用，再拿到 `GetIfInitialized()` 的安全返回值；只能依赖别的全局状态或直接持有外部传入的 manager。 |
| 根因 | `Bind_UAssetManager.cpp` 在迁移过程中保留了异步加载方法，却把最基础的状态查询/入口获取 helper 以注释形式整体下线，没有提供替代绑定。 |
| 影响 | 脚本若想调用 `LoadPrimaryAsset(...)` / `LoadPrimaryAssets(...)`，现在无法通过同一 namespace 的标准 helper 做前置初始化检查和 manager 句柄获取。结果是 API surface 从“先判断，再获取，再调用”退化成“默认外部已准备好 manager”，而当前仓库没有绑定测试会在这组基础入口缺失时报警。 |

---

## 分析 (2026-04-08 11:54)

### 发现 63：`FinishSpawningActor` 用 `HasActorBegunPlay()` 代替真正的 spawn-complete 状态，非 deferred actor 在 BeginPlay 前可被二次 `FinishSpawning`

| 项目 | 内容 |
|------|------|
| 维度 | A / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Private/Actor.cpp` |
| 行号 | 256-283；4331-4356 |
| 描述 | 当前 `FinishSpawningActor()` 和 `FinishSpawningActor(AActor, const FTransform&)` 只在 `Actor->HasActorBegunPlay()` 为真时才拒绝继续执行，然后直接调用 `Actor->FinishSpawning(...)`。但引擎原生 spawn 流程在 `bDeferConstruction == false` 时，会在 `SpawnActor` 内部立刻执行一次 `FinishSpawning(UserSpawnTransform, true)`；`AActor::FinishSpawning()` 自己真正保护的是 `bHasFinishedSpawning`，并通过 `ensure(!bHasFinishedSpawning)` 阻止二次完成生成。也就是说，对一个“已经完成 spawn 但尚未 BeginPlay”的普通 actor，当前 bind 仍会放行第二次 `FinishSpawning`。 |
| 根因 | 绑定层把“已经 BeginPlay”误当成“已经完成 spawning”的判定条件，没有跟引擎真正使用的 `bHasFinishedSpawning` 状态保持一致。 |
| 影响 | 脚本如果误把非 deferred spawn 出来的 actor 再传给 `FinishSpawningActor`，不会在 bind 边界得到稳定拒绝，而是继续跌进引擎内部 `ensure(!bHasFinishedSpawning)` 路径。结果是错误从可诊断的脚本 API 误用，退化成更底层的 ensure/生命周期异常，而且仓库里没有任何 `FinishSpawningActor` 绑定回归去锁住这个边界。 |

### 发现 64：`GFrameNumber` 被作为可写全局变量暴露给脚本，任何脚本都能篡改引擎级帧计数

| 项目 | 内容 |
|------|------|
| 维度 | A / B / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UWorld.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Core/Public/CoreGlobals.h`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Core/Private/Misc/CoreGlobals.cpp`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Core/Private/HAL/UnrealMemory.cpp`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Core/Private/Misc/MemStack.cpp` |
| 行号 | 87-89；622-625；558-559；424-425；120-133；86-96 |
| 描述 | 当前 `Bind_UWorld.cpp` 直接注册了 `uint GFrameNumber`，而 bind core 的 `BindGlobalVariable()` 只是把传入地址原样交给 `RegisterGlobalProperty(...)`，不会自动加只读保护。`CoreGlobals.h/.cpp` 明确表明 `GFrameNumber` 是“每帧递增一次”的核心全局帧计数器；引擎底层的内存清理和 memstack 回收逻辑也直接按它分桶和推进世代。也就是说，脚本现在不是在读取一个 harmless 统计值，而是在拿到一个可写的全局时间基准。 |
| 根因 | world bind 把调试/查询用的全局状态直接按普通 property 暴露，没有把 `GFrameNumber` 这种引擎核心计数器限制成 `const` 或纯 getter。 |
| 影响 | 一旦脚本误写 `GFrameNumber`，影响会越过 Angelscript 自身，直接污染依赖全局帧号的核心运行时逻辑，包括按帧轮换的内存回收与临时分配清理。结果会表现成跨系统、跨帧的异常，而当前仓库里没有任何 `GFrameNumber` 绑定测试去保证它至少是只读访问。 |

### 发现 65：`UObject::ImplementsInterface` 没有校验 `InterfaceClass` 真的是 interface，调用错误会被静默吞成 `false`

| 项目 | 内容 |
|------|------|
| 维度 | A / C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Private/KismetSystemLibrary.cpp`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/CoreUObject/Private/UObject/Class.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceImplementTests.cpp` |
| 行号 | 100-106；321-331；6265-6283；207-237 |
| 描述 | 当前绑定的 `ImplementsInterface(const UClass InterfaceClass)` 只检查 `Object == nullptr || InterfaceClass == nullptr`，然后直接调用 `Object->GetClass()->ImplementsInterface(InterfaceClass)`。引擎原生 `UClass::ImplementsInterface()` 在收到非 interface class 时只会返回 `false`；而更贴近脚本/蓝图调用语义的 `UKismetSystemLibrary::DoesClassImplementInterface()` 会先显式校验 `Interface->IsChildOf(UInterface::StaticClass())`，并在传入的类根本不是 interface 时记录 runtime error。当前仓库测试也只覆盖 `UIDamageableImplCheck::StaticClass()` 这类合法 interface happy path，没有任何“把普通 class 误传给 `ImplementsInterface`”的回归。 |
| 根因 | 绑定层为了直接复用 `UClass::ImplementsInterface()`，丢掉了 Kismet 层对输入类型的显式校验与错误报告。 |
| 影响 | 当脚本通过 `FindClass`、配置反射或热重载后的动态 lookup 拿到一个普通 `UClass`，再误传给 `ImplementsInterface` 时，当前实现不会暴露“参数根本不是 interface”的事实，只会静默返回 `false`。结果是配置错误、查找错误和真实的“不实现该接口”在脚本侧被压扁成同一种结果，排障信号明显变弱。 |

### 发现 66：两个 `GetComponentsByClass(..., ?& OutComponents)` overload 都只追加结果，不会清空调用方数组

| 项目 | 内容 |
|------|------|
| 维度 | A / C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Private/GameplayStatics.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp` |
| 行号 | 39-87，89-149；1097-1113，1165-1183；151-164 |
| 描述 | 当前两个 `AActor::GetComponentsByClass(..., ?& OutComponents)` 实现都直接遍历组件并 `OutComponents.Add(...)`，从头到尾没有任何 `Reset()/Empty()`。同一插件里，`GetAllClasses()` 会先 `OutClasses.Reset()`；引擎原生 `UGameplayStatics::GetAllActorsOfClass()` / `GetAllActorsOfClassWithTag()` 也都会先 `OutActors.Reset()`，说明“填充 out array 前先清空”才是这里更稳定的调用契约。现有绑定测试虽然在 `SceneComponents` 场景里手动先 `Empty()` 再调用，但 `AllComponents` 路径直接依赖的是空数组 happy path，没有任何“复用已有数组再次查询”的回归。 |
| 根因 | `GetComponentsByClass` 采用了手写循环实现，却没有继承同类查询 helper 的“输出前清空结果数组”约定。 |
| 影响 | 脚本一旦复用同一个数组多次调用 `GetComponentsByClass`，结果会把旧元素和新查询结果混在一起，轻则出现重复组件，重则在 actor/component 变化后保留陈旧句柄。因为 API 名字和 `?& OutComponents` 形态都强烈暗示“输出参数被重新填充”，这种语义偏差很容易进入业务代码而不被立刻发现。 |

### 发现 67：`ULevel::GetActors()` 把底层 `Level->Actors` 稀疏数组原样暴露给脚本，结果集可能包含 `nullptr` 槽位

| 项目 | 内容 |
|------|------|
| 维度 | A / C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UWorld.cpp`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Engine/Level.h`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Private/Level.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemScenarioTests.cpp` |
| 行号 | 107-110；431-432；1794-1799；174-181 |
| 描述 | 当前 `ULevel::GetActors()` 直接返回 `Level->Actors` 的引用。引擎头文件明确把它定义成“供 `FActorIteratorBase` 使用的所有 actor 数组”；`Level.cpp` 里还专门有“把排到末尾的 `nullptr` 条目裁掉”的整理逻辑，说明这个底层容器在日常运行中本来就会出现空洞。也就是说，脚本通过 `GetActors()` 拿到的不是像 `TActorIterator` / `GetAllActorsOfClass()` 那样已经过滤过的稳定结果集，而是 level 的原始 backing array。现有测试也只验证 `GetWorld().GetPersistentLevel().GetActors().Num()` 可以调用，没有任何对返回数组做遍历或空值处理的回归。 |
| 根因 | world bind 直接暴露了 `ULevel` 的内部存储结构，没有在脚本边界提供一层最基本的空值过滤或只读包装。 |
| 影响 | 脚本如果把 `GetActors()` 当成“当前 level 的有效 actor 列表”直接遍历，很容易在热重载、actor 删除或 level 整理前后遇到 `nullptr` 元素。结果是调用方必须自己知道 Unreal 底层容器的稀疏语义，否则就会在看似普通的 `for` 循环里撞上额外的空值分支甚至空句柄访问。 |

---

## 分析 (2026-04-08 12:06)

### 发现 68：`Bind_ConfigEnums.cpp` 不再按 channel 配置构造碰撞枚举，而是错误地从 collision profile 模板倒推出枚举项

| 项目 | 内容 |
|------|------|
| 维度 | C / B / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_ConfigEnums.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_ConfigEnums.cpp`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Engine/CollisionProfile.h`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 行号 | 12-45；13-50；43-59，95-118；`rg -n "ETraceTypeQuery::|EObjectTypeQuery::|ECollisionChannel::Visibility|ECollisionChannel::Camera|ECollisionChannel::Pawn|ECollisionChannel::Vehicle|ECollisionChannel::Destructible" Plugins/Angelscript/Source/AngelscriptTest -g "*.cpp" -g "*.as"` 未命中 |
| 描述 | 当前实现保留了 `//for (FCustomChannelSetup& Profile : Collision->DefaultChannelResponses)` / `//if (Profile.bTraceType)` 的注释，但实际代码已经改成 `GetNumOfProfiles()` + `GetProfileByIndex()`，并把 `FCollisionResponseTemplate::Name` 直接写成 `BPName`，再把 `temp->ObjectType` 写回 `ECollisionChannel_[BPName]`。UEAS2 对应实现仍然遍历 `DefaultChannelResponses`，使用 `FCustomChannelSetup::Name / Channel / bTraceType` 构造三组查询枚举；`CollisionProfile.h` 也明确表明 `FCollisionResponseTemplate` 表示 collision profile，而 `FCustomChannelSetup` 才表示 channel 定义。与此同时，UEAS2 还会显式回填 `Visibility`、`Camera`、`Pawn`、`Vehicle`、`Destructible` 等标准 channel 条目，current 版本已经不再为 `ECollisionChannel` 做这层补齐。 |
| 根因 | 这是一个带 `WILL-EDIT` 注释残留的半迁移实现：绑定层把“配置里的 channel 定义”误替换成了“运行时 profile 模板”，导致枚举来源和语义同时漂移。 |
| 影响 | 脚本侧的 `ECollisionChannel` / `ETraceTypeQuery` / `EObjectTypeQuery` 不再稳定反映项目 collision channel 配置，而会混入 profile 名称和 `ObjectType` 的间接映射。结果不是单个缺少 overload，而是整组碰撞枚举的命名与分类契约都可能偏离 UEAS2 和引擎配置；当前测试树对这些枚举名完全没有回归约束。 |

### 发现 69：`EPhysicalSurface` 整组绑定已从 current 消失，脚本无法再按项目 surface 名称访问物理材质表

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_ConfigEnums.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_ConfigEnums.cpp`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/PhysicsEngine/PhysicsSettings.h`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 行号 | 8-10，46；9-11，52-57；27-42，359-361；`rg -n "EPhysicalSurface::|EPhysicalSurface " Plugins/Angelscript/Source/AngelscriptTest -g "*.cpp" -g "*.as"` 未命中 |
| 描述 | current `Bind_ConfigEnums.cpp` 只创建了 `ETraceTypeQuery`、`ECollisionChannel`、`EObjectTypeQuery` 三个枚举，没有任何 `EPhysicalSurface` 绑定。UEAS2 同文件末尾会额外创建 `EPhysicalSurface`，并把 `UPhysicsSettings::PhysicalSurfaces` 中的每个 `FPhysicalSurfaceName` 映射成脚本枚举项。`PhysicsSettings.h` 也明确显示项目级物理 surface 名称就存放在这张配置表里。 |
| 根因 | 配置枚举迁移时只保留了 collision 相关三组枚举，没有把与之并列的 physical surface 枚举一起迁移。 |
| 影响 | 脚本失去了通过项目配置名访问 `EPhysicalSurface` 的标准入口，涉及 footstep、hit material、surface-based FX 或音效分流的逻辑，都不能再沿用 UEAS2 的配置枚举路径。当前测试树对这组枚举完全零覆盖，因此这个 API 面回退不会被自动化提前发现。 |

### 发现 70：`FGeometry` 丢失 render-transform 与 absolute-position 查询 helper，UMG 几何读数 surface 明显弱于 UEAS2

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FGeometry.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_FGeometry.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 行号 | 7-35；8-24；`rg -n "GetRenderTransformScale|GetRenderTransformTranslation|GetAbsolutePosition" Plugins/Angelscript/Source/AngelscriptTest -g "*.cpp" -g "*.as"` 未命中 |
| 描述 | current `Bind_FGeometry.cpp` 只保留了 `GetLocalSize()`、`GetAbsoluteSize()`、`AbsoluteToLocal()`、`LocalToAbsolute()` 和 `MakeChild()`。UEAS2 在同文件还提供 `GetRenderTransformScale()`、`GetRenderTransformTranslation()`、`GetAbsolutePosition()` 三个只读 helper，分别暴露 accumulated render transform 的 scale、translation 以及 geometry 的绝对位置。 |
| 根因 | `FGeometry` 绑定在迁移时只保留了最基础的 size / 坐标转换 API，没有把 UEAS2 已经补齐的 render-space 查询接口一并迁入。 |
| 影响 | 依赖 widget render transform、absolute position 做命中区域换算、拖拽偏移、tooltip 定位或动画调试的脚本，当前只能自己重建几何推导，无法直接沿用 UEAS2 的 helper。由于测试树对这组三个方法完全零覆盖，这个 surface 收缩会长期保持静默。 |

### 发现 71：`FGeometry::MakeTransformedChild()` 缺失，脚本失去直接构造 render-space child geometry 的标准入口

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FGeometry.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_FGeometry.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 行号 | 31-35；48-58；`rg -n "MakeTransformedChild" Plugins/Angelscript/Source/AngelscriptTest -g "*.cpp" -g "*.as"` 未命中 |
| 描述 | current 只绑定了 `FGeometry MakeChild(const FVector2D& Position, const FVector2D& Size) const`，它走的是 `FSlateLayoutTransform`。UEAS2 还额外提供 `FGeometry MakeTransformedChild(const FVector2D& Translation, const FVector2D& Scale) const`，直接通过 `FSlateRenderTransform(FScale2f(Scale), FVector2f(Translation))` 构造 child geometry。 |
| 根因 | current 的 `FGeometry` surface 收口到了 layout-only child 创建，render-transform 变体在迁移时被直接漏掉。 |
| 影响 | 使用 Slate/UMG 脚本构造局部 render-space geometry 时，当前仓库已经没有与 UEAS2 对齐的 helper，调用方只能自己在别处重建 `FSlateRenderTransform` 语义或放弃对应写法。由于测试树没有覆盖这一入口，缺失不会通过自动化暴露。 |

### 发现 72：`FKey::GetVirtualKey()` 从 current 消失，输入键归一化 surface 比 UEAS2 少一层标准访问器

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_InputEvents.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_InputEvents.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 行号 | 28-38；29-40；`rg -n "GetVirtualKey" Plugins/Angelscript/Source/AngelscriptTest -g "*.cpp" -g "*.as"` 未命中 |
| 描述 | current 的 `FKey` 绑定停在 `GetDisplayName()` / `GetKeyName()`，没有 UEAS2 中仍然存在的 `FKey GetVirtualKey() const`。这意味着脚本侧现在拿不到 Unreal 已提供的 virtual-key 归一化入口，只能保留原始物理键值或自行实现映射。 |
| 根因 | `Bind_InputEvents.cpp` 迁移时保留了大部分 `FKey` 基础查询，但漏掉了 UEAS2 已暴露的 virtual-key helper。 |
| 影响 | 需要把平台/设备特定物理键折叠到统一 virtual key 的脚本逻辑会失去标准 API，输入兼容层和 UI 快捷键逻辑都更容易各自实现一份映射。当前测试树对该 helper 完全零覆盖，因此这个缺口不会被回归测试发现。 |

### 发现 73：`Bind_InputEvents.cpp` 去掉了 `FKey` / `FInputChord` / `FEventReply` value-factory 的 `no_discard` 契约

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_InputEvents.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_InputEvents.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 行号 | 17-20，51-59，216-224；17-21，53-63，220-231；`rg -n "Handled\\(|Unhandled\\(|FInputChord\\(|FKey\\(" Plugins/Angelscript/Source/AngelscriptTest -g "*.cpp" -g "*.as"` 未命中 |
| 描述 | UEAS2 在 `FKey(const FName&)`、两个 `FInputChord` constructor 后都会立即 `SetPreviousBindNoDiscard(true)`，并把 `FEventReply::Handled()` / `Unhandled()` 直接声明成 `no_discard`。current 版本把这几处约束全部去掉了：constructor 后没有任何 `SetPreviousBindNoDiscard(true)`，`Handled()` / `Unhandled()` 也退化成普通返回值函数。 |
| 根因 | 输入事件绑定迁移时保留了 value-factory 本体，却漏掉了 UEAS2 已补齐的“构造出的值必须被消费”的契约元数据。 |
| 影响 | 当前脚本可以静默写出 `FKey("SpaceBar");`、`FInputChord(EKeys::Enter);`、`FEventReply::Handled();` 这类被丢弃的 value-factory 调用，而不会得到任何编译期提示。对 `FEventReply` 来说，这会把本应返回给 Slate 的 handled/unhandled 结果吞掉；对 `FKey` / `FInputChord` 来说，则会放大“看起来像有副作用、实际只是创建临时值”的误用概率。 |

---

## 分析 (2026-04-08 12:19)

### 发现 74：`GetCurrentWorld` 与三条 actor spawn helper 都无条件解引用 `GEngine`，在引擎启动/退出窗口可直接崩溃

| 项目 | 内容 |
|------|------|
| 维度 | A / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UWorld.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Private/UnrealEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 行号 | 38-41；166-170，199-202，232-235；427，3764；`rg -n "GetCurrentWorld\\(|SpawnPersistentActor|SpawnActor\\(" Plugins/Angelscript/Source/AngelscriptTest -g "*.cpp" -g "*.as"` 未命中 |
| 描述 | 当前 `GetCurrentWorld()` 直接执行 `GEngine->GetWorldFromContextObject(...)`；`SpawnActorFromMeta()`、`SpawnActor()`、`SpawnPersistentActor()` 进入 world-context 校验前也都先调用同一个 `GEngine->GetWorldFromContextObject(...)`。但引擎源码明确把 `GEngine` 定义为 `ENGINE_API UEngine* GEngine = NULL;`，并且在退出流程里再次置回 `NULL`。这意味着这些 bind 不是先做“无 world / 无 engine”诊断，而是在 `GEngine` 为空时直接空指针解引用。 |
| 根因 | 绑定层把 `GEngine` 当作始终可用的全局单例使用，却没有像同仓库 `IsPlayingPIE()` 那样先做 `GEngine == nullptr` 防护，再进入 world-context 获取。 |
| 影响 | 只要脚本、测试或工具在引擎尚未完成初始化，或已经进入 teardown 的窗口调用这些 helper，就不会得到稳定的脚本异常，而会直接崩在绑定层的 `GEngine->...` 解引用上。当前测试树对 `GetCurrentWorld()` 和 actor spawn bind 都没有覆盖，因此这条生命周期缺口处于未锁定状态。 |

### 发现 75：`GetCurrentWorld()` 明确允许返回 `null`，但同文件 `UWorld` 读写 helper 全部按非空接收者实现

| 项目 | 内容 |
|------|------|
| 维度 | A / C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UWorld.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemScenarioTests.cpp` |
| 行号 | 38-42，54-57，65-85；180 |
| 描述 | `GetCurrentWorld()` 调用 `GetWorldFromContextObject(..., EGetWorldErrorMode::ReturnNull)`，接口语义本身就允许在无效 world-context 时返回 `nullptr`。但同一文件随后公开的 `GetGameState()`、`IsStartingUp()`、`IsTearingDown()`、`SetGameInstance()`、`GetGameInstance()`、`GetLevelScriptActor()`、`GetPersistentLevel()` 都直接对 `World` 做成员访问或方法调用，没有任何空值收口。也就是说，绑定层一边把“当前 world 可能不存在”作为公开契约暴露出去，一边又让最自然的后续调用链全部依赖非空 `this`。 |
| 根因 | `Bind_UWorld.cpp` 把 world 获取 helper 和 world 实例 helper 分别独立注册，却没有为“nullable world handle”定义统一的链式调用约束或保护分支。 |
| 影响 | 脚本一旦写出 `GetCurrentWorld().GetPersistentLevel()`、`GetCurrentWorld().GetGameState()` 这类顺手链式调用，在 world-context 缺失时就不会得到稳定的脚本级错误，而是把 `nullptr` 继续传进后续 lambda 的裸解引用。现有测试只覆盖了 `GetWorld().GetPersistentLevel().GetActors().Num()` 的 happy path，没有任何无 world-context 的回归。 |

### 发现 76：两个 `FinishSpawningActor` overload 遇到空 actor 时直接静默返回，错误处理契约弱于同文件其它 actor helper

| 项目 | 内容 |
|------|------|
| 维度 | A / C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 行号 | 74-77，124-127，256-283；`rg -n "FinishSpawningActor|SpawnActor\\(" Plugins/Angelscript/Source/AngelscriptTest -g "*.cpp" -g "*.as"` 未命中 |
| 描述 | 同一文件里，`GetComponentsByClass(..., ?& OutComponents)` 两个 overload 在 `Actor == nullptr` 时都会显式 `Throw("Actor was null.")`。但 `FinishSpawningActor(AActor)` 和 `FinishSpawningActor(AActor, const FTransform&)` 的第一条分支都是 `if (Actor == nullptr) { return; }`，既不抛脚本异常，也不记录错误。结果是“传了空 actor”在 actor 查询 helper 上属于显式脚本错误，在 spawn-complete helper 上却被吞成无事发生。 |
| 根因 | `FinishSpawningActor` 只补了“重复完成 spawn”这一类误用诊断，没有把最基础的空输入校验纳入与同文件其它 actor API 一致的错误处理框架。 |
| 影响 | 当脚本把 `SpawnActor(...)` 的失败返回值、条件分支中的空 actor，或被提前销毁的句柄继续传给 `FinishSpawningActor` 时，当前实现会静默跳过，而不是把真正的调用错误暴露出来。由于测试树完全没有覆盖 `FinishSpawningActor`，这种 silent failure 很容易在业务里长期潜伏。 |

### 发现 77：`UWorld::WorldType` 被以普通可写 property 暴露，脚本可直接篡改世界类型判定

| 项目 | 内容 |
|------|------|
| 维度 | A / B / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UWorld.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Engine/World.h`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Private/World.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 行号 | 45-47，87；575-585；1231；9177-9189；`rg -n "WorldType" Plugins/Angelscript/Source/AngelscriptTest -g "*.cpp" -g "*.as"` 未命中 |
| 描述 | 当前 `Bind_UWorld.cpp` 既把 `IsGameWorld()`、`IsEditorWorld()`、`IsPreviewWorld()` 作为只读 helper 暴露，又额外注册了 `UWorld_.Property("EWorldType WorldType", &UWorld::WorldType)`。bind core 的 `BindProperty()` 默认直接走 `RegisterObjectProperty(...)`，只有传 `BindParams` 的重载才会生成读写权限控制；这里没有任何只读限制。引擎头文件明确表明 `WorldType` 是 `UWorld` 的核心字段，而 `World.cpp` 里的 `IsGameWorld()`、`IsEditorWorld()`、`IsPreviewWorld()` 都直接按 `WorldType` 分支。也就是说，脚本拿到的不只是一个观察值，而是可改写世界角色的内部状态位。 |
| 根因 | world bind 同时提供了“语义化查询 helper”和“底层原始字段访问”，但没有把 `WorldType` 这种驱动引擎行为的成员限制成只读暴露。 |
| 影响 | 一旦脚本写入 `WorldType`，后续所有依赖 `IsGameWorld()` / `IsEditorWorld()` / `IsPreviewWorld()` 的路径都会被污染，包括 world 行为分支、streaming/preview/editor 逻辑判断等。当前测试树对 `WorldType` 绑定完全零覆盖，因此这个高风险可写入口不会被自动化提前发现。 |

### 发现 78：`SetGameInstance()` 被直接暴露给脚本，world 对 `OwningGameInstance` 的核心所有权可被运行时改写

| 项目 | 内容 |
|------|------|
| 维度 | A / B / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UWorld.cpp`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Engine/World.h`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Private/World.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 行号 | 73-77；4265-4272；7927-7932，8423-8424；`rg -n "SetGameInstance\\(|GetGameInstance\\(" Plugins/Angelscript/Source/AngelscriptTest -g "*.cpp" -g "*.as"` 未命中 |
| 描述 | 当前 `Bind_UWorld.cpp` 把 `void SetGameInstance(UGameInstance NewGI)` 作为普通 `UWorld` 方法公开。引擎头文件显示这个方法本体只是 `OwningGameInstance = NewGI` 的直接赋值；而 `World.cpp` 里 `GetTimerManager()`、`GetLatentActionManager()` 等路径都会优先经由 `OwningGameInstance` 取管理器，load world 流程也会显式把当前 world 的 game instance 传递给新 world。也就是说，这里暴露的不是普通 gameplay setter，而是 world 与 owning game instance 之间的核心关联点。 |
| 根因 | world bind 在筛选可脚本化 API 时，没有区分“只读查询接口”和“引擎内部所有权/生命周期接线接口”，把 `SetGameInstance()` 与 `GetGameInstance()` 一起按普通方法公开了出来。 |
| 影响 | 一旦脚本在运行时改写 `OwningGameInstance`，world 后续拿到的 timer manager、latent action manager 乃至与 game instance 关联的系统行为都可能切到另一条对象链上，形成跨系统的状态污染。当前测试树完全没有覆盖 `SetGameInstance()`，因此这条高风险可变更入口处于无回归保护状态。 |

---

## 分析 (2026-04-08 12:30)

### 发现 79：`TArray` 绑定缺少 UEAS2 的 self-alias 防护，向同容器元素取引用再做增删会读到失效地址

| 项目 | 内容 |
|------|------|
| 维度 | A / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray_Functions.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/AngelscriptBinds/Bind_TArray_Functions.h`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_TArray.cpp` |
| 行号 | 17-49；654-675，796-825，1090-1119，1157-1190；14-46；626-637，777-789，1076-1088，1146-1159 |
| 描述 | current 的 `FArrayOperations` 头文件里没有 UEAS2 的 `CheckAddress()`，`Add()`、`Insert()`、`Remove()`、`RemoveSwap()` 也都直接把外部传入的 `Value` 指针继续用于 `CopyValue` / `Memcpy` / `IsValueEqual`。UEAS2 在同一套实现里先检查 `Addr` 是否落在 `Arr.GetData() .. Arr.ArrayMax * ElementSize` 范围内，命中时立即 `Throw("Attempting to use a container element which already comes from the container being modified.")`。这意味着 current 允许脚本把同一个数组里的元素引用直接传回正在修改该数组的 API。 |
| 根因 | `TArray` 绑定移植时保留了底层 `FScriptArray` 原地增删逻辑，但把 UEAS2 专门用于阻止“容器修改期间再次读取容器内部地址”的 debug 防护一起裁掉了。 |
| 影响 | 一旦脚本写出 `Values.Add(Values[0])`、`Values.Insert(Values[0], 0)`、`Values.Remove(Values[0])`、`Values.RemoveSwap(Values[0])` 这类 self-alias 调用，`Arr.Add`/`Arr.Insert` 可能先触发重分配，`Arr.Remove`/`Arr.RemoveSwap` 也会在比较过程中移动底层内存，随后 `Value` 就可能指向已经失效或被覆盖的位置。结果不是稳定的脚本异常，而是取决于容量变化与元素布局的悬空读取/错误比较。当前测试树里没有任何 `CheckAddress` 或 self-alias 容器回归来锁住这条路径。 |

### 发现 80：`TArray::RemoveSwap()` 漏掉删除计数累加，脚本拿到的返回值永远是 0

| 项目 | 内容 |
|------|------|
| 维度 | A / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_TArray.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/` |
| 行号 | 1157-1192；1149-1166；`rg -n "RemoveSwap\\(" Plugins/Angelscript/Source/AngelscriptTest -g "*.cpp" -g "*.as"` 未命中 |
| 描述 | current 的 `FArrayOperations::RemoveSwap()` 先声明 `int32 NumRemoved = 0`，循环中确实会执行 `Arr.Remove(...)`，但删除分支只做了 `Num--` 和 `i--`，没有任何 `NumRemoved++`，最终直接 `return NumRemoved;`。UEAS2 对应实现同一位置在删除后会显式 `NumRemoved++`。因此 current 脚本侧无论实际删掉多少元素，`RemoveSwap()` 返回值都固定为 0。 |
| 根因 | 这是一次典型的移植/合并遗漏：逻辑主体和循环控制都保留了，但计数回写语句在 current 分支里被落掉，导致 API 行为与签名契约分离。 |
| 影响 | 依赖返回值判断“是否删到元素”或“删了几个元素”的脚本会在 current 上得到静默错误结论，例如 `if (Values.RemoveSwap(Target) > 0)` 会走失败分支，但数组其实已经被修改。由于测试树完全没有覆盖 `RemoveSwap()`，这个行为回退当前不会被自动化发现。 |

### 发现 81：`TArray::Reserve()` 被错误实现成清空数组，调用后会直接丢失已有元素

| 项目 | 内容 |
|------|------|
| 维度 | A / D |
| 严重度 | Critical |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_TArray.cpp`<br>`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Core/Public/Containers/ScriptArray.h` |
| 行号 | 910-928；882-892；154-165，274-278 |
| 描述 | current 的 `FArrayOperations::Reserve()` 在算出 `ReservedSize` 后调用的是 `Arr.Reset(ReservedSize, ...)`。引擎 `ScriptArray.h` 明确表明 `Reset()` 在 `NewSize <= ArrayMax` 时会直接执行 `ArrayNum = 0`，否则走 `Empty(...)`；它不是“扩大 capacity 但保留元素”的语义。UEAS2 同一位置调用的是 `Arr.ResizeTo(ReservedSize, ...)`。这意味着 current 脚本侧对一个非空数组调用 `Reserve()`，结果不是保留内容并扩容，而是把逻辑长度清成 0。 |
| 根因 | 绑定实现把 `Reserve` 和 `Reset` 混用了：迁移时把原本正确的 `ResizeTo` 改成了 `Reset`，但函数名和脚本 surface 仍然宣称自己是 `Reserve()`。 |
| 影响 | 任意脚本一旦对已有元素的 `TArray` 调用 `Reserve()`，后续读取 `Num()`、遍历或索引都会看到空数组；当 `NewSize <= ArrayMax` 时，这个“清空”路径甚至不会先析构现有元素，而是单纯把 `ArrayNum` 归零，形成静默数据丢失和生命周期错乱。当前测试树里没有任何 `TArray::Reserve()` 绑定回归，因此这条 Critical 行为回退不会被自动化拦住。 |

### 发现 82：`TArray::SetNum()` 对无构造类型不做零初始化，新增槽位会暴露未初始化垃圾值

| 项目 | 内容 |
|------|------|
| 维度 | A / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_TArray.cpp` |
| 行号 | 930-967；894-935 |
| 描述 | current 的 `FArrayOperations::SetNum()` 在扩容时调用 `Arr.SetNumUninitialized(NewNum, ...)`，随后只在 `Ops->bNeedConstruct` 为真时逐元素 `ConstructValue()`。也就是说，对 `int`、`float`、`FName`、object pointer 这类“不需要构造函数”的元素类型，新增区间完全没有任何初始化步骤。UEAS2 对应实现先 `ResizeTo` + `ArrayNum = NewNum`，然后在 `!bNeedConstruct` 分支显式 `Memzero(...)` 新增区间。 |
| 根因 | current 分支把 `SetNum()` 错误地降级成了 `SetNumUninitialized()` + “只处理需要构造的类型”，遗漏了 UEAS2 用于 primitive / pointer 类型默认值初始化的 `Memzero` 分支。 |
| 影响 | 脚本如果写出 `Values.SetNum(8)`，新增元素不会得到预期的默认 `0` / `nullptr`，而是直接暴露底层未初始化内存。对标量类型，这会产生不可预测的随机值；对对象句柄或弱类型数据，则会把垃圾位模式伪装成看似有效的槽位。当前测试树在 `Bindings` / `Examples` 下没有任何 `TArray::SetNum()` 绑定回归，因此这条默认值契约回退目前处于无保护状态。 |

### 发现 83：delegate 内部签名 helper 没有隐藏，`BindUFunction` / `AddUFunction` 的公开命名面泄露了生成器内部协议

| 项目 | 内容 |
|------|------|
| 维度 | B / C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`<br>`J:/UnrealEngine/UEAS2/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/Bind_Delegates.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Delegate/AngelscriptDelegateScenarioTests.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp` |
| 行号 | 1430-1439；665-667，692-696；1449-1456；134，186，311，404；60，70 |
| 描述 | current 在 `_FMulticastScriptDelegate` 上公开注册了 `AddUFunction(const UObject Object, const FName& FunctionName, UDelegateFunction Signature)`，在 `_FScriptDelegate` 上公开注册了 `BindUFunction(UObject Object, const FName& FunctionName, UDelegateFunction Signature)`。但同仓库 preprocessor 明确把这两条当成代码生成内部桥接层使用：生成的公开包装只暴露 2 参数版本，并在内部自动补上 `__DelegateSignature(this)`。UEAS2 对应绑定已经把这两条 raw helper 重命名为 `__Internal_AddUFunction` / `__Internal_BindUFunction`。与此同时，当前测试也只覆盖 2 参数公开用法，没有任何直接调用 3 参数 raw helper 的回归。 |
| 根因 | delegate 绑定层保留了生成器内部所需的“带显式 signature 参数”入口，但没有像 UEAS2 一样用 `__Internal_*` 前缀把它们从公共命名约定里隔离出去。 |
| 影响 | 当前脚本会在同一命名空间里同时看到“公开 API”与“生成器内部协议”两个同名重载，API surface 比 UEAS2 更噪声也更容易误用。更直接地说，脚本作者现在可以手动传入任意 `UDelegateFunction Signature` 调 raw overload，绕过正常的 `__DelegateSignature(this)` 推导路径，而仓库没有任何自动化去锁住这类内部入口的行为契约。 |
