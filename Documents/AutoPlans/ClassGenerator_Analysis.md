# ClassGenerator 分析

---

## 分析 (2026-04-08 10:00)

### 发现 1：`CDONoDefaults` 基线会被默认组件的 defaults 污染，导致 soft reload 错判“用户修改”

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` / `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` |
| 行号 | `AngelscriptClassGenerator.cpp:4093-4110, 4490-4500` / `ASClass.cpp:1359-1405, 1415-1458` |
| 描述 | `PrepareSoftReload()` 通过把全局 `GConstructASObjectWithoutDefaults` 设为 `true` 来构造 `CDONoDefaults`，并在后续把它作为“未执行 defaults 的基线”参与 `bModifiedByDefaults` 判定。问题在于 `StaticActorConstructor()` 和 `StaticComponentConstructor()` 一进入就把该标志复位为 `false`，而 `StaticActorConstructor()` 之后还会继续调用 `CreateDefaultComponents()` 创建默认组件。这样 actor 本体虽然跳过了 `ExecuteDefaultsFunctions()`，但它创建出来的 script component 会在自己的构造函数里重新看到 `bApplyDefaults=true`，从而执行 defaults。最终 `CDONoDefaults` 并不是真正的“无 defaults”对象，后面的 `BaseCDO` 与 `CDONoDefaults` 比较会把组件 defaults 误判成“默认语句修改过的值”。 |
| 根因 | “禁用 defaults” 状态被实现成进程级全局 bool，而不是能覆盖整个对象树构造过程的作用域状态。顶层 actor 构造函数过早清零该标志，导致子对象构造脱离了 soft reload 的基线语义。 |
| 影响 | soft reload 的属性保留逻辑会错误地把默认组件上的值视为“必须复制的 defaults 结果”，进而在 body-only reload 中保留本应重新计算的旧默认值，或把旧 CDO 状态错误传播到新实例/CDO。这个问题直接影响 actor/component 热重载正确性，且非常难通过常规 smoke test 发现，因为表面上 reload 会成功，只是状态基线已经失真。 |

### 发现 2：带 `DefaultComponent` 的 actor 无法安全 soft reload，复用类对象时会直接撞上旧组件描述

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 行号 | `4113-4284, 5039-5212, 5214-5444` |
| 描述 | `DoSoftReload()` 明确复用旧 `UASClass` 实例：`ClassDesc->Class = Class`，后续再调用 `FinalizeClass()` 重新做 actor/component/object finalization。但 `FinalizeActorClass()` 一进来就 `check(ASClass->DefaultComponents.Num() == 0)`，随后又把本轮扫描到的 `DefaultComponents` 和 `OverrideComponents` 继续 `Add()` 到 `UASClass` 数组里。代码中看不到任何在 soft reload 前清空这两个数组的逻辑，因此只要某个 actor class 曾经成功初始化过默认组件，再次以 body-only 变更走 soft reload，就会在 finalize 阶段命中旧状态。 |
| 根因 | actor finalization 被按“新建类对象”的前提编写，但 soft reload 路径复用了旧 `UASClass`，却没有为 `DefaultComponents` / `OverrideComponents` 做 reset。类生成阶段的生命周期假设与热重载复用策略不一致。 |
| 影响 | 含默认组件的 script actor 无法稳定享受 soft reload。开发期的 body-only 修改要么直接触发断言中断，要么在非 check 构建里把旧组件描述与新描述混在一起，进一步污染 root component 校验、attachment 校验和实例化时的组件创建顺序。 |

### 发现 3：full reload 的版本链只增不减，`_REPLACED_*` 类对象会被永久 root 住

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` / `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` |
| 行号 | `AngelscriptClassGenerator.cpp:2573-2586, 3696-3699, 2406-2412, 4990-5035` / `ASClass.cpp:912-923` |
| 描述 | full reload 创建新类时，会先把旧类重命名成 `*_REPLACED_*`，给它打上 `CLASS_NewerVersionExists`，然后创建一个带 `RF_Standalone | RF_MarkAsRootSet` 的新 `UASClass`，最后把旧类的 `NewerVersion` 指向新类。问题在于，针对“被替换但仍然存在”的旧类，后续只做了 `ScriptTypePtr/ConstructFunction/DefaultsFunction = nullptr` 清理，没有像 `CleanupRemovedClass()` 那样 `RemoveFromRoot()` 和清掉 `RF_Standalone`。仓库内也看不到其他针对 `ReplacedClass` 的解 root 路径。结果就是每次 full reload 都会把上一版类永久留在包里，并持续挂在 `NewerVersion` 链上。 |
| 根因 | 版本链把“removed class”和“replaced class”分成了两条回收策略，但只有 removed 分支真正释放了 UObject 生命周期持有；replaced 分支只做脚本层断链，没有做 UObject 层回收。 |
| 影响 | 长时间迭代后会持续累积 rooted 的 `_REPLACED_*` 类对象、旧函数子对象和相关元数据，放大编辑器内存占用，并让版本链越来越长。`GetMostUpToDateClass()` 虽然还能走到尾节点，但它依赖的正是这条永不收缩的链，内存安全与热重载可持续性都会随 reload 次数恶化。 |

### 发现 4：soft reload 复用 `UASClass` 时，删除子类 `__InitDefaults()` 不会清掉旧 `DefaultsFunction`

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 行号 | `1290-1296, 4113-4142, 4281, 5889-5908` |
| 描述 | reload 分析把 defaults 代码变化仅标成 `FullReloadSuggested`，不是 `FullReloadRequired`。而真正执行 soft reload 时，`DoSoftReload()` 会复用原有 `UASClass`，随后调用 `UpdateConstructAndDefaultsFunctions()` 刷新构造/默认函数指针。问题在于该函数只有在当前 `ObjType` 上找到了“由本类自己定义的 `void __InitDefaults()`”时才写入 `Class->DefaultsFunction`，否则不会把旧值清空。也就是说，如果旧版本类定义了自己的 defaults override，而新版本删除了这个 override、改为继承父类或不再有 defaults，本轮 soft reload 后 `Class->DefaultsFunction` 仍然指向旧脚本函数。 |
| 根因 | `UpdateConstructAndDefaultsFunctions()` 按“新建类对象”的语义实现，假设 `DefaultsFunction` 初值为空；但 soft reload 复用了旧类对象，导致“没有新 override”与“保留旧 override”在实现上被混淆。 |
| 影响 | 这会让已经从脚本源码移除的 defaults override 继续在后续对象构造中执行，产生“源码已删、运行时仍生效”的幽灵默认值。由于 reload 分析只建议 full reload，不强制 full reload，这个 stale function pointer 在开发流程里是可到达的。 |

### 发现 5：现有 hot reload 测试没有覆盖 `DefaultComponent` soft reload 和版本链清理，导致关键退化面处于盲区

| 项目 | 内容 |
|------|------|
| 维度 | D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp` / `Plugins/Angelscript/Source/AngelscriptTest/Component/AngelscriptComponentScenarioTests.cpp` / `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp` |
| 行号 | `AngelscriptHotReloadScenarioTests.cpp:53-80, 163-180, 250-270` / `AngelscriptComponentScenarioTests.cpp:355-465` / `AngelscriptTestEngineHelper.cpp:500-530` |
| 描述 | hot reload 场景测试目前只覆盖“普通 actor 的属性保留 / 函数体变化 / 结构性变更”这类用例，脚本示例里没有任何一个 hot reload 用例带 `DefaultComponent`、`RootComponent` 或 `Attach` 元数据；相对地，`ComponentScenarioTests` 虽然验证了 `DefaultComponent`/attach 的基础创建，但完全不做 reload。对 `Plugins/Angelscript/Source/AngelscriptTest/HotReload` 做定向 grep，`DefaultComponent` / `OverrideComponent` / `RootComponent` / `Attach =` 为 0 命中。同时，测试 helper 在查类和查函数时总是先调用 `GetMostUpToDateClass()`，这会把旧版本类自动折叠掉，现有测试因此也不会暴露 `_REPLACED_*` 类对象是否持续留存。 |
| 根因 | 测试矩阵把“组件层构造语义”和“热重载语义”分开验证，没有覆盖两者的交叉场景；版本链相关辅助函数又默认帮测试代码跳到最新类，掩盖了旧节点是否被回收。 |
| 影响 | `DefaultComponents` 软重载断言/重复累积，以及 `_REPLACED_*` 链条常驻内存这类问题，都很容易在现有测试体系下长期潜伏。只跑当前 hot reload suite，无法证明 actor 组件树与版本链生命周期在迭代开发中的正确性。 |

---

## 分析 (2026-04-08 02:38)

### 发现 6：脚本对象构造栈使用进程级 `TArray`，与允许异步线程执行 defaults 的设计相冲突

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` |
| 行号 | `56-75, 987-988, 1011-1025, 1075-1081, 1140-1171, 1375, 1428, 1483` |
| 描述 | `AllocScriptObject()` 在 `CurrentObjectInitializers` 里 `Emplace` 一个 `FObjectInitializer`，`FinishConstructObject()` 和三个静态构造函数再通过 `CurrentObjectInitializers.Last()` 判断当前对象是否属于脚本分配路径。但这个“构造栈”被声明成进程级 `static TArray<FObjectInitializer>`，既不是 `thread_local`，也没有任何锁。与此同时，同文件前面的线程检查明确写着 defaults 可能在 async loading thread 执行；同一文件里 `GASDefaultConstructorOuter` 已经被实现为 `thread_local`，说明这类构造上下文本来就是按线程隔离设计的。当前实现把最关键的 initializer 栈放成了全局共享状态。 |
| 根因 | `AllocScriptObject()` / `FinishConstructObject()` 复制了 UE 的对象构造流程，但没有把“当前正在构造哪个 UObject”的状态迁移到线程本地或 `FUObjectThreadContext`，导致脚本分配路径仍依赖一个跨线程共享的 LIFO 容器。 |
| 影响 | 一旦两个线程并行构造 script object，`CurrentObjectInitializers.Last()` 就可能读到别的线程压入的对象，直接让 `bIsScriptAllocation` 误判。轻则跳过本应执行的 `ConstructFunction` / defaults，重则在 `RemoveAt(CurrentObjectInitializers.Num() - 1)` 时把别的线程的 initializer 弹掉，造成对象初始化次序错乱和难以复现的热重载/加载期内存破坏。 |

### 发现 7：脚本构造函数异常时不会弹出 `CurrentObjectInitializers`，后续对象会继承脏构造上下文

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` / `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_context.cpp` |
| 行号 | `ASClass.cpp:1075-1081, 1137-1173` / `as_context.cpp:2454-2489, 4117-4134` |
| 描述 | `AllocScriptObject()` 在进入脚本构造前把 `FObjectInitializer` 压入 `CurrentObjectInitializers`，而本地代码里唯一的弹栈路径只存在于 `FinishConstructObject()`。AngelScript VM 的执行逻辑显示：`asBC_ALLOC` 调用构造函数后，只要 `m_status != asEXECUTION_ACTIVE` 就会直接 `return`，不会继续跑后面的 `asBC_FinConstruct`；而 `FinishConstructObject()` 正是由 `asBC_FinConstruct` 触发。仓库内对 `CurrentObjectInitializers.RemoveAt(...)` 的搜索也只命中 `FinishConstructObject()` 的两个分支，没有任何异常回滚或 finally 清理。 |
| 根因 | 构造栈的生命周期被绑定在“字节码一定会执行到 `FinConstruct`”这个乐观前提上，没有覆盖脚本构造函数抛异常、中断执行或 prepare 失败的退出路径。 |
| 影响 | 只要某次脚本构造异常退出，`CurrentObjectInitializers.Last()` 就会永久保留旧对象，后续构造函数里的 `bIsScriptAllocation` 判定会持续读到脏栈顶。结果包括跳过本应执行的 `ConstructFunction` / defaults、错误地在别的对象完成构造时弹掉旧 initializer，以及把热重载期间的对象重建过程带入错误状态。 |

### 发现 8：删除最后一个带引用的 script 字段后，旧 `ReferenceSchema` 不会被清空

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 行号 | `3686-3689, 4274-4284, 4866-4924` |
| 描述 | 类创建和 soft reload 都会调用 `DetectAngelscriptReferences()` 重建 script-only GC schema。该函数先把旧 `Class->ReferenceSchema` 追加进 `Schema`，记下 `NumPreviousMembers`，再根据当前 `ScriptType` 重新发射引用信息。问题在于最终是否覆写 schema 的条件是 `Schema.NumMembers() != NumPreviousMembers || NumPreviousMembers == 0`。如果热重载后的类已经没有任何 script-only 引用字段，循环体不会再追加新成员，`Schema.NumMembers()` 就会与 `NumPreviousMembers` 完全相等，导致 `ReferenceSchema.Set(...)` 根本不执行。仓库内也没有其他清空 `ReferenceSchema` 的路径。 |
| 根因 | `DetectAngelscriptReferences()` 只把“成员数发生变化”当成 schema 需要更新的条件，却没有覆盖“新 schema 为空，需要清掉旧 schema”这个删除分支。 |
| 影响 | 一旦类在 hot reload 后删掉最后一个带引用的 script 字段，GC 仍会继续按旧 offset 扫描该对象的脚本内存。旧 offset 若被复用于别的字段，会把无关数据当成 UObject 引用保活；若 offset 已失效，则会把已经不存在的布局继续暴露给 GC，造成引用图污染和潜在内存安全问题。 |

### 发现 9：类注册路径把无参构造函数当成必然存在，遇到自定义 ctor-only 的 script class 会直接解引用空指针

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` / `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_builder.cpp` |
| 行号 | `AngelscriptClassGenerator.cpp:4826-4848, 5894-5903` / `as_builder.cpp:730-739` |
| 描述 | `UpdateConstructAndDefaultsFunctions()` 在 `ObjType != nullptr` 时无条件执行 `Class->ConstructFunction = GetFunctionById(ObjType->beh.construct)`，随后立刻 `((asCScriptFunction*)Class->ConstructFunction)->isInUse = true`，完全没有判空。第三方 AngelScript builder 的实现则明确表明：当类声明了其他构造函数时，会移除自动生成的默认构造函数，并把 `ot->beh.construct` 置为 `0`。同一文件里的 `ReinitializeScriptObject()` 甚至已经有一条 `ensureMsgf(false, "does not have a constructor with no arguments")`，说明作者知道这种情况会出现，但主注册路径没有做任何防护。 |
| 根因 | 动态 `UASClass` 生成流程默认所有 script class 都能通过无参 ctor 完成 UObject 包装初始化，却没有在编译期附加检查，也没有在 `UpdateConstructAndDefaultsFunctions()` 里处理 `beh.construct == 0`。 |
| 影响 | 只要脚本类只声明带参构造函数，类创建或热重载刷新 `ConstructFunction` 时就会把 `nullptr` 当 `asCScriptFunction*` 解引用，导致生成流程崩溃。即便某些路径侥幸绕过这次崩溃，后续 `ReinitializeScriptObject()` 也会落到“this will crash soon”的 ensure 分支，说明当前实现对这类合法脚本形态并不稳健。 |

### 发现 10：full reload 在强制 GC 前先清空 `ScriptTypePtr`，导致旧实例完全跳过 `RuntimeDestroyObject`

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` / `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` / `UEAS2/Engine/Source/Runtime/CoreUObject/Private/Blueprint/BlueprintSupport.cpp` |
| 行号 | `AngelscriptClassGenerator.cpp:2400-2444` / `ASClass.cpp:965-976` / `BlueprintSupport.cpp:2683-2696` |
| 描述 | reload 收尾阶段只要发生 reinstance，就会先把 `ClassData.ReplacedClass->ScriptTypePtr` 置空，并且遍历所有 `UClass`，把仍引用旧 script type 的类一并置空；随后才调用 `GEngine->ForceGarbageCollection(true)`。而引擎对象销毁路径在 `BlueprintSupport.cpp` 里有一层硬门槛：只有 `GetClass()->ScriptTypePtr != nullptr` 才会向上找到脚本父类并调用 `RuntimeDestroyObject(this)`。这意味着所有等待 GC 的旧 script 实例、以及派生 blueprint class 实例，只要其类在 reload 过程中被清空了 `ScriptTypePtr`，析构时就不会进入 `UASClass::RuntimeDestroyObject()`。 |
| 根因 | “避免旧类继续引用将被删除的 script type” 与 “对象销毁时仍需要调用 script destructor/debug cleanup” 这两个生命周期需求被用同一个 `ScriptTypePtr` 字段承载；reload 代码提前清空它，直接切断了引擎析构钩子的进入条件。 |
| 影响 | 旧实例在 GC 阶段会跳过 `asCScriptObject::CallDestructor(...)`，也会跳过 `DebugValues.Free(Object->Debug)`。如果脚本析构承担了释放引用、解绑或回收 native 侧资源的职责，这些实例就会在 full reload 后以“未执行脚本析构”的状态被回收，形成典型的热重载资源泄漏和状态悬挂。 |

---

## 分析 (2026-04-08 02:51)

### 发现 11：soft reload 同步 blueprint 派生类时把 `UBlueprintGeneratedClass` 当成 `UASClass`，会直接空指针解引用

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | Critical |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 行号 | `4286-4315` |
| 描述 | `DoSoftReload()` 在更新派生 blueprint class 的 script type 时，先遍历所有 `UBlueprintGeneratedClass`，随后把每个 `CheckClass` 强转成 `UASClass* asClass = Cast<UASClass>(CheckClass)`。但 `UBlueprintGeneratedClass` 并不继承 `UASClass`，因此这里对 blueprint 派生类得到的 `asClass` 必然是 `nullptr`。代码后面却在 `ASClass == Class` 分支里直接访问 `asClass->ScriptTypePtr` 并写回 `asClass->ScriptTypePtr = Class->ScriptTypePtr`。只要存在任何继承当前脚本类的 blueprint generated class，这条 soft reload 路径就会在更新派生类元数据时空指针崩溃。 |
| 根因 | 代码试图把“脚本类本身”和“继承脚本类的 blueprint class”统一走 `UASClass` 字段访问，但实际只通过 `GetFirstASClass(CheckClass)` 找到了 blueprint 的脚本父类，没有给 blueprint class 自身提供可写的 `ScriptTypePtr` 容器。类型假设与运行时类层级不匹配。 |
| 影响 | 这会让“脚本类被 blueprint 继承”这一常见开发路径失去 soft reload 能力。开发者只要在编辑器里存在派生 blueprint，函数体级别的 soft reload 就可能直接崩溃，中断迭代流程，并把热重载问题错误伪装成 blueprint 资产异常。 |

### 发现 12：soft reload 不会重新计算 tick 能力缓存，`Tick`/`ReceiveTick` 变更后新对象仍沿用旧调度状态

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` / `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` |
| 行号 | `AngelscriptClassGenerator.cpp:4113-4242, 5697-5772, 5807-5840` / `ASClass.cpp:1370-1372, 1423-1425` |
| 描述 | 类的 tick 开关被缓存到 `UASClass::bCanEverTick` 和 `bStartWithTickEnabled`，其值只在 `InitClassTickSettings()` 中根据父类 tick 配置以及 `Tick`/`ReceiveTick` 是否为 no-op 计算一次。`InitDefaultObjects()` 只对 `ShouldFullReload(ClassData)` 为真的类调用这段逻辑；相对地，`DoSoftReload()` 更新了 `ClassFlags`、函数指针和 `ScriptTypePtr`，却完全没有刷新这两个 tick 缓存位。与此同时，actor/component 构造函数始终从这两个缓存位拷贝到 `PrimaryActorTick` / `PrimaryComponentTick`。因此，只要一次 soft reload 改变了 `Tick`/`ReceiveTick` 的实现状态，后续新建对象仍会使用 reload 前的 tick 配置。 |
| 根因 | tick 调度能力被设计成“类创建阶段预计算并缓存”的数据，但 soft reload 只重连脚本函数，不重新执行依赖函数语义的预计算流程。热重载生命周期没有覆盖这块类级派生状态。 |
| 影响 | 开发者在只改函数体的情况下，把 `Tick` 从 no-op 改成有效实现，新的 actor/component 仍可能完全不 tick；反过来，把 `Tick` 改回 no-op 后对象仍会继续注册 tick。结果是运行时行为与当前脚本源码不一致，且问题只在 soft reload 场景出现，定位成本很高。 |

### 发现 13：`Config=<Name>` / `DefaultConfig` 只在 full reload 写入 `UClass`，soft reload 既不检测 `ConfigName` 变化也不刷新配置语义

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` / `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` / `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 行号 | `AngelscriptPreprocessor.cpp:2320-2332` / `AngelscriptEngine.h:1187-1199` / `AngelscriptClassGenerator.cpp:3294-3308, 4208-4242` |
| 描述 | 预处理器把 `Config=<Name>` 单独写进 `ClassDesc->ConfigName`，只把 `DefaultConfig` 放进 `Meta`。但 reload 判定时，`AreFlagsEqual()` 完全不比较 `ConfigName`，而 `DoSoftReload()` 也只更新少数 `ClassFlags`，没有同步 `Class->ClassConfigName`，也没有处理 `CLASS_DefaultConfig`。相对地，只有 full reload 的 `CreateFullReloadClass()` 才会按照 `ConfigName` 设置 `CLASS_Config`、写入 `ClassConfigName`，并按 `DefaultConfig` 元数据设置 `CLASS_DefaultConfig`。结果是：把脚本类从 `Config=Game` 改成 `Config=Engine` 这类变更时，热重载分析甚至不会把它识别成需要 full reload 的结构性变化；即便 `DefaultConfig` 元数据变更触发了 `FullReloadSuggested`，soft reload 本身也不会把新的配置语义落到 `UClass`。 |
| 根因 | 类级配置语义被拆成了两个来源字段：`ConfigName` 存在描述对象上，`DefaultConfig` 存在 metadata 上，但 soft reload 路径既没有把两者纳入 reload 需求判定，也没有实现与 full reload 对称的 `UClass` 同步逻辑。 |
| 影响 | 热重载后，类可能继续从旧 ini domain 读写配置，或者漏掉/保留 `DefaultConfig` 标志，表现为“源码里的 Config specifier 已改，但实际载入/保存的配置文件和默认值行为不变”。这直接破坏动态 `UClass` 生成的一致性，也会让配置相关问题只在热重载会话里复现。 |

### 发现 14：现有测试没有覆盖“blueprint 子类 + soft reload”以及 tick/config 类级语义的热重载回归

| 项目 | 内容 |
|------|------|
| 维度 | D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptScriptClassCreationTests.cpp` / `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp` / `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPropertyTests.cpp` |
| 行号 | `AngelscriptScriptClassCreationTests.cpp:327-400` / `AngelscriptHotReloadScenarioTests.cpp:22-40, 42-339` / `AngelscriptHotReloadPropertyTests.cpp:106-364` |
| 描述 | 仓库中确实有 blueprint 子类场景测试，但它只验证“脚本类可被 blueprint 继承并正常生成/BeginPlay”，没有任何 hot reload 步骤；相对地，hot reload 测试只覆盖普通脚本类的属性保留、函数体变化和结构性变更。对这三份测试文件做定向扫描，`CompileModuleWithResult(&Engine, ECompileType::SoftReloadOnly` 一共只有 4 个命中，但 `Tick(`、`ReceiveTick`、`Config=`、`DefaultConfig` 命中均为 0；`HotReload` 目录里也没有真正的 blueprint child reload 用例。 |
| 根因 | 测试矩阵把“script class 可被 blueprint 继承”与“script class 可 soft reload”分成了互不相交的两组用例，没有覆盖脚本父类被 blueprint 持有时的热重载路径，也没有覆盖依赖类级缓存和 class specifier 同步的 tick/config 场景。 |
| 影响 | `DoSoftReload()` 中 blueprint child 空指针崩溃、tick 缓存不刷新、`ConfigName`/`DefaultConfig` 语义漂移这类问题，都会在现有自动化测试下漏检。当前 suite 能证明基础 soft reload 可用，但不能证明“带 blueprint 派生资产的真实编辑器工作流”是安全的。 |

---

## 分析 (2026-04-08 03:04)

### 发现 15：soft reload 不会刷新 `UASFunction` 缓存的 JIT 入口，final 函数会继续执行旧机器码

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` / `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` |
| 行号 | `AngelscriptClassGenerator.cpp:3422-3427, 4244-4260, 4779-4805` / `ASClass.cpp:1762-1928, 2952-3289` |
| 描述 | 初次生成 `UFunction` 时，`AllocateFunctionFor()` 会根据 `asTRAIT_FINAL` 和 `jitFunction*` 是否存在，选择 `UASFunction_*_JIT` 等专用子类，并在创建阶段把 `JitFunction` / `JitFunction_Raw` / `JitFunction_ParmsEntry` 缓存到 `UASFunction` 对象上。soft reload 时，`DoSoftReload()` 只把 `((UASFunction*)FuncDesc->Function)->ScriptFunction` 指到新的 script function，再调用 `SoftReloadFunction()` 更新参数/返回值里的类型引用；整个路径完全没有刷新任何 `JitFunction*` 缓存。与此同时，多个运行时分派实现，例如 `UASFunction_JIT::RuntimeCallFunction()`、`UASFunction_NoParams_JIT::RuntimeCallEvent()`、`UASFunction_DWordArg_JIT::RuntimeCallFunction()` 等，都会直接执行对象上缓存的 `JitFunction*`，并不会回到新的 `ScriptFunction->jitFunction*`。 |
| 根因 | 函数热重载把“脚本函数句柄更新”和“JIT 入口更新”拆成了两套状态，但 soft reload 只维护了前者，没有为 `UASFunction` 的缓存入口建立与全量类生成对称的刷新逻辑。 |
| 影响 | 只要某个 final/non-virtual 脚本函数已被 JIT，soft reload 后该函数就可能继续跑旧版本机器码，而反射数据、`ScriptFunction` 和源码文本却已经指向新版本。结果是开发者看到“soft reload 成功”，实际调用行为仍停留在旧逻辑；若新旧签名周边语义已有变化，还可能出现旧 JIT 代码访问新对象状态的错配。 |

### 发现 16：`BlueprintThreadSafe` 元数据在 soft reload 后不会改变函数分派策略，线程约束会停留在旧版本

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` / `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` |
| 行号 | `AngelscriptClassGenerator.cpp:527-563, 1253-1258, 1312-1315, 1762-1928, 4244-4260` / `ASClass.cpp:1961-1968, 2952-2969` |
| 描述 | 函数是否允许走 thread-safe 调用路径，是在分析阶段按类级 `BlueprintThreadSafe` 与函数级 `BlueprintThreadSafe` / `NotBlueprintThreadSafe` 元数据计算出的 `FunctionDesc->bThreadSafe`，随后由 `AllocateFunctionFor()` 决定实际分配成 `UASFunction`、`UASFunction_JIT` 还是 `UASFunction_NotThreadSafe`、`UASFunction_NotThreadSafe_JIT`。但 reload 分级对“方法 metadata 改变”和“类 metadata 改变”都只提升到 `FullReloadSuggested`；如果调用方坚持 `SoftReloadOnly`，`DoSoftReload()` 只会替换 `ScriptFunction`，不会重建 `UFunction` 对象，也不会把旧的 thread-safe / not-thread-safe 子类互相替换。结果是：把函数或整个类从非线程安全改成线程安全后，运行时仍旧会走带 `CheckGameThreadExecution()` 的旧路径；反过来，把线程安全标记去掉后，也可能继续沿用原先允许跨线程执行的调用实现。 |
| 根因 | “线程安全语义”被固化在 `UASFunction` 的 C++ 具体子类上，而不是运行时可热更新的字段；soft reload 只热更脚本句柄，没有对这层分派对象做重建或迁移。 |
| 影响 | 这会让源码声明的线程安全保证与运行时真实行为脱节。轻则开发者在 soft reload 会话里看到与源码不一致的线程检查结果，重则把本应禁止跨线程调用的函数继续暴露给 thread-safe 路径，或让本应在异步线程可执行的函数被错误拦截，直接影响默认语句和异步加载场景下的行为正确性。 |

### 发现 17：静态函数的 `WorldContextOffsetInParms` 永远保持 `-1`，`RuntimeCallEvent` 会按无效偏移读参数

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` / `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h` / `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` |
| 行号 | `AngelscriptClassGenerator.cpp:3519-3561, 3575-3644` / `ASClass.h:178-181` / `ASClass.cpp:505-516, 577-580` |
| 描述 | 静态 `UFUNCTION` 生成时，类生成器会分析 `WorldContext` 参数，设置 `WorldContextIndex` 与 `bIsWorldContextGenerated`，并在 `ParmsSize` 计算里把生成出来的 world-context 参数算进去。但负责记录参数结构内偏移的代码 `NewFunction->WorldContextOffsetInParms = Prop->GetOffset_ForUFunction();` 被整段注释掉了，因此 `UASFunction::WorldContextOffsetInParms` 一直停留在头文件里的初始值 `-1`。另一方面，`AngelscriptCallFromParms()` 在静态函数走 `RuntimeCallEvent` 路径时，会直接用这个字段做 `Parms` 指针偏移，读取 `*(UObject**)((SIZE_T)Parms + ASFunction->WorldContextOffsetInParms)`。也就是说，所有依赖这条路径的静态脚本函数都在用 `Parms - 1` 的无效地址当 `WorldContext` 来源。 |
| 根因 | 函数生成阶段只完成了 world-context 参数的“存在性”和索引推导，却遗漏了与运行时调用器配套的内存布局缓存；最终留下了一个永远不写入、但运行期必读的偏移字段。 |
| 影响 | 任何经由 `RuntimeCallEvent` 调用的静态脚本函数，只要路径依赖 `WorldContextOffsetInParms`，就会读取错误地址作为 world context。轻则把错误对象灌入 `FAngelscriptEngine::AssignWorldContext()`，重则直接触发越界读取和随后的随机崩溃。这属于动态 `UFunction` 生成与运行时 thunk 不一致的硬错误。 |

### 发现 18：`WithValidate` 变化不会触发函数重建，RPC 校验链可能长期停留在旧版本

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` / `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` / `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` / `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptComponent.cpp` |
| 行号 | `AngelscriptPreprocessor.cpp:1629-1637` / `AngelscriptEngine.h:984-1054` / `AngelscriptClassGenerator.cpp:1022-1047, 3469-3472, 3660-3664, 4244-4260` / `AngelscriptComponent.cpp:47-103` |
| 描述 | 预处理器遇到 `WithValidate` 时会把 `FunctionDesc->bNetValidate` 设为 `true`。全量函数生成阶段则据此给 `UFunction` 打上 `FUNC_NetValidate`，并把主函数的 `ValidateFunction` 缓存到对应 `_Validate` 函数上。问题在于：`FAngelscriptFunctionDesc::IsDefinitionEquivalent()` 比较了大量函数 specifier，却完全漏掉了 `bNetValidate`；因此把 RPC 从“无校验”改成“有校验”，或反向移除 `WithValidate`，都不会被当成定义变化触发 full reload。进入 soft reload 后，`DoSoftReload()` 又只替换 `ScriptFunction`，不会更新 `FunctionFlags`，也不会重算 `ValidateFunction` 缓存。与此同时，`UAngelscriptComponent::ProcessEvent()` 是否先调用 `_Validate`，正是由这两个旧状态共同决定。 |
| 根因 | `WithValidate` 语义被拆成了三个状态源：描述对象上的 `bNetValidate`、运行时 `UFunction::FunctionFlags`、以及 `UASFunction::ValidateFunction` 缓存；reload 判定漏掉了第一个，soft reload 同步又漏掉了后两个。 |
| 影响 | soft reload 之后，RPC 可能继续执行已经被源码移除的 `_Validate`，也可能在源码新增 `WithValidate` 后仍然完全跳过校验。结果是网络行为与当前脚本声明不一致，直接破坏 RPC 安全边界；而且这类偏差不会被 reload 分析主动升级成 full reload。 |

### 发现 19：现有热重载测试没有覆盖 `WorldContext` / `BlueprintThreadSafe` / `WithValidate` 的函数 thunk 回归

| 项目 | 内容 |
|------|------|
| 维度 | D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp` / `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp` / `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPerformanceTests.cpp` / `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPropertyTests.cpp` / `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptNativeScriptHotReloadTests.cpp` |
| 行号 | `AngelscriptHotReloadFunctionTests.cpp:380, 490` / `AngelscriptHotReloadScenarioTests.cpp:112, 309` / `AngelscriptHotReloadPerformanceTests.cpp:114, 294, 296` / `AngelscriptHotReloadPropertyTests.cpp:106, 181` / `AngelscriptNativeScriptHotReloadTests.cpp:48` |
| 描述 | 对 `HotReload`、`Angelscript`、`Subsystem` 相关测试目录做定向 grep，`CompileModuleWithResult(&Engine, ECompileType::SoftReloadOnly` 共命中 10 处，说明仓库确实在跑 body-only 热重载路径；但同一轮扫描里，`WorldContext`、`BlueprintThreadSafe`、`NotBlueprintThreadSafe`、`WithValidate`、`_Validate`、`NetValidate` 命中全部为 0。也就是说，现有热重载 suite 从未覆盖“静态函数 world-context thunk”“线程安全分派子类切换”“RPC validate 缓存/flag 同步”这些直接依赖动态 `UFunction` 生成状态的场景。 |
| 根因 | 测试主要验证“函数体替换是否生效”和“属性是否保留”，没有把函数调用 thunk 本身当成热重载对象来测；因此凡是依赖 `UASFunction` 缓存字段和函数 flags 的路径，天然落在矩阵之外。 |
| 影响 | 发现 15-18 这类缺陷，即使在持续跑现有 hot reload automation 的情况下也很容易长期潜伏。当前测试能够证明基础 soft reload 通了，但不能证明函数级调用约定、线程约束和 RPC 校验链在 reload 前后保持一致。 |

---

## 分析 (2026-04-08 03:25)

### 发现 20：移除最后一个 `ImplementedInterface` 时，`SoftReloadOnly` 会保留旧 `UClass::Interfaces`

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` / `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` / `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp` |
| 行号 | `AngelscriptEngine.h:1187-1198` / `AngelscriptClassGenerator.cpp:1318-1323, 2081-2093, 2284-2294, 5060-5159` / `Bind_UObject.cpp:100-105, 188-190` |
| 描述 | 类描述的 `ImplementedInterfaces` 发生变化时，分析阶段只把 reload 需求提升到 `FullReloadSuggested`。但真正决定是否走 full reload 的 `ShouldFullReload(FClassData&)` 只检查“新类是否是 interface”以及“新类当前是否仍有 `ImplementedInterfaces`”；如果脚本把最后一个接口从类声明里删掉，新版本 `ImplementedInterfaces.Num()` 会变成 0，于是 `SoftReloadOnly` 会直接走 `LinkSoftReloadClasses()` + `DoSoftReload()`。这条路径不会清空或重建 `UClass::Interfaces`，而填充接口表的逻辑只存在于 full reload 的 `FinalizeClass()`。结果就是旧 `UClass` 上上一版留下的 `Interfaces` 数组会原样保留。 |
| 根因 | reload 判定把“接口列表变化”当成建议性 full reload，而 `ShouldFullReload()` 又只看新描述是否还实现接口，没有把“旧类曾实现接口、现在不再实现”视为必须重建 `UClass` 的结构变化。soft reload 生命周期与接口表的写入/清理职责没有对齐。 |
| 影响 | 热重载后，脚本源码已经移除接口，但运行时 `Object->GetClass()->ImplementsInterface(...)` 仍会返回 true，`UObject::ImplementsInterface` 绑定和基于接口的 cast/分派逻辑会继续把该类当成接口实现者。这会让 `UClass` 反射结果与当前脚本声明脱节，直接破坏接口注册一致性，并把接口相关问题伪装成脚本层逻辑异常。 |

### 发现 21：`DefaultComponent` / `Attach` 等 property metadata 变更在 `SoftReloadOnly` 下不会重建组件描述

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` / `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` |
| 行号 | `AngelscriptClassGenerator.cpp:1117-1123, 2081-2093, 2284-2294, 4181-4197, 5214-5445` / `ASClass.cpp:1176-1345, 1397` |
| 描述 | actor 组件树的脚本语义完全由 property metadata 决定：`FinalizeActorClass()` 会扫描 `DefaultComponent`、`OverrideComponent`、`RootComponent`、`Attach`、`AttachSocket`、`EditorOnly` 等 meta，并把结果固化到 `UASClass::DefaultComponents` / `OverrideComponents`。但分析阶段对 property metadata 的差异只给出 `FullReloadSuggested`，在 `SoftReloadOnly` 下 `ShouldFullReload()` 不会因此转成 full reload。进入 `DoSoftReload()` 后，代码只对现有 `DefaultComponents` / `OverrideComponents` 逐项重算 `VariableOffset`，完全不会根据新的 property metadata 重新生成或删除这些描述。后续 actor 构造依旧直接消费旧数组创建和挂接组件。 |
| 根因 | 组件描述的真实来源是 property metadata，但 soft reload 只把它当成“偏移需要修正”的缓存，而不是需要与脚本重新同步的结构数据。生成阶段与热重载阶段对 `DefaultComponents` / `OverrideComponents` 的生命周期假设不一致。 |
| 影响 | 如果开发者在 body-only 编译流程里去掉 `DefaultComponent`、修改 `Attach` 目标、切换 `RootComponent`、把属性从 `DefaultComponent` 改成 `OverrideComponent`，甚至只改 `EditorOnly`，热重载后的 `UClass` 仍会按旧组件树继续构造 CDO 和新实例。表现就是源码里的组件声明已经变了，但运行时组件仍按旧层级生成，直接破坏动态 `UClass` 生成一致性和 actor 热重载正确性。 |

### 发现 22：`Blueprintable` / `NotBlueprintable` 只在 full reload 写入 `UClass` metadata，`SoftReloadOnly` 会留下旧蓝图继承语义

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` / `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` / `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningBlueprintSubclassTraceTests.cpp` |
| 行号 | `AngelscriptPreprocessor.cpp:2286-2294` / `AngelscriptClassGenerator.cpp:1311-1316, 3316-3340, 4208-4242` / `AngelscriptLearningBlueprintSubclassTraceTests.cpp:95-152` |
| 描述 | 预处理器把 `UCLASS(Blueprintable)` / `UCLASS(NotBlueprintable)` 解析成 `ClassDesc->Meta` 上的 `Blueprintable`、`NotBlueprintable` 和 `IsBlueprintBase`。full reload 创建新类时会把这些 meta 写进 `UClass`，并显式做 `SetMetaData` / `RemoveMetaData` 来同步蓝图继承语义。相对地，分析阶段对 class metadata 差异只提升到 `FullReloadSuggested`，而 `DoSoftReload()` 只更新若干 `ClassFlags`，完全不处理 class metadata。仓库内的 learning test 还明确把 `Blueprintable` 当成“enable Blueprint inheritance”的前提。 |
| 根因 | class specifier 被拆成了两类状态：一类映射到 `ClassFlags`，另一类落在 `UClass` metadata；soft reload 只维护了前者，没有为 metadata 建立与 full reload 对称的同步和清理逻辑。 |
| 影响 | 在 `SoftReloadOnly` 流程里切换 `Blueprintable` / `NotBlueprintable` 后，脚本源码与 `UClass` 的蓝图继承语义会分叉：类可能继续允许创建 blueprint child，或者在源码已经改成 `Blueprintable` 后仍然被当成不可继承。这个问题不会直接崩溃，但会让动态 `UClass` 反射状态与编辑器行为长期不一致。 |

### 发现 23：`ExposeOnSpawn` / `EditFixedSize` / `EditorOnly` 这类 property metadata 在 `SoftReloadOnly` 后会继续沿用旧 `FProperty` flags

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` / `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` / `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 行号 | `AngelscriptPreprocessor.cpp:2450-2451, 2676-2687` / `AngelscriptEngine.h:877-900` / `AngelscriptClassGenerator.cpp:1117-1123, 2935-3039, 4150-4174` |
| 描述 | 若 property 只改了 metadata，分析阶段只会给出 `FullReloadSuggested`。但 `AddClassProperties()` 在 full reload 创建 `FProperty` 时，会把 `ExposeOnSpawn`、`EditFixedSize`、`EditorOnly` 等 metadata 转成真正的 `CPF_ExposeOnSpawn`、`CPF_EditFixedSize`、`CPF_EditorOnly` flags；soft reload 重新处理 property 时却只做 `Property->Link(ArDummy)` 和 offset 更新，没有任何 `SetPropertyFlags` / `ClearPropertyFlags` 或 metadata 同步逻辑。因此，这类“metadata 驱动的 property flag”在 `SoftReloadOnly` 后会继续保持旧值。 |
| 根因 | `FAngelscriptPropertyDesc::IsDefinitionEquivalent()` 不把这类 metadata 当成定义变化，而 soft reload 又假设已有 `FProperty` 的 flags 已经正确，只重做内存布局链接。`FProperty` 反射状态与脚本 metadata 的同步只存在于全量生成路径。 |
| 影响 | 开发者在 `SoftReloadOnly` 流程里切换 `ExposeOnSpawn`、`EditFixedSize` 或 `EditorOnly` 后，运行时和编辑器看到的 `FProperty` 语义仍然是旧版本，例如生成的 spawn pin、细节面板编辑限制或 editor-only 剥离行为不会随源码更新。这会让热重载后的 `UClass` 反射结果与当前脚本声明不一致。 |

### 发现 24：现有自动化没有覆盖 `SoftReloadOnly` 下的接口移除与 metadata 漂移场景

| 项目 | 内容 |
|------|------|
| 维度 | D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceAdvancedTests.cpp` / `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningBlueprintSubclassTraceTests.cpp` / `Plugins/Angelscript/Source/AngelscriptTest/HotReload/` |
| 行号 | `AngelscriptInterfaceAdvancedTests.cpp:388-395` / `AngelscriptLearningBlueprintSubclassTraceTests.cpp:95-152` / `HotReload` 目录定向 grep 统计 |
| 描述 | 仓库里确实有接口热重载测试，但 `AngelscriptInterfaceAdvancedTests` 明确要求“interface hot reload should succeed on the full reload path”，没有任何 `SoftReloadOnly` 覆盖。另一方面，`LearningBlueprintSubclassTraceTests` 只验证一次性编译出的 `Blueprintable` 类可用于创建 blueprint child，不做 reload。对 `Plugins/Angelscript/Source/AngelscriptTest/HotReload/` 做定向 grep，`ImplementsInterface`、`Blueprintable`、`NotBlueprintable`、`IsBlueprintBase`、`DefaultComponent`、`OverrideComponent`、`Attach =`、`RootComponent` 命中全部为 0。 |
| 根因 | 现有测试矩阵主要覆盖函数体替换、属性保留和少量 full reload 场景，没有把“接口列表变化”和“metadata 驱动的反射结构变化”作为 soft reload 的一等回归面来验证。 |
| 影响 | 发现 20、22、23 这类不会立刻崩溃、但会让 `UClass`/`FProperty` 反射状态与源码脱节的问题，当前自动化基本不会报警。只跑现有 hot reload suite，无法证明 `SoftReloadOnly` 在接口移除、蓝图继承 specifier 变更和 property metadata 变更后仍保持一致性。 |

---

## 分析 (2026-04-08 03:37)

### 发现 25：脚本 `new` 路径声明了构造 outer 作用域，但仓库内没有任何调用点，所有 `AllocScriptObject` 分配都会退回 `TransientPackage`

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h` / `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` / `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_scriptengine.cpp` / `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_context.cpp` |
| 行号 | `ASClass.h:59-63` / `ASClass.cpp:1011-1025, 1048-1060` / `as_scriptengine.cpp:4754-4766` / `as_context.cpp:2454-2469` |
| 描述 | `UASClass` 暴露了 `FScopeSetDefaultConstructorOuter` 和 `GetDefaultConstructorOuter()`，看起来想把脚本构造时的 outer 作为线程局部状态传进 `AllocScriptObject()`。但对 `Plugins/Angelscript/Source/AngelscriptRuntime` 做全文检索，`FScopeSetDefaultConstructorOuter` 只在 `ASClass.h/.cpp` 的声明与定义处出现，没有任何实际调用点。结果是 `GASDefaultConstructorOuter` 永远保持 `nullptr`，`GetDefaultConstructorOuter()` 每次都返回 `GetTransientPackage()`；而 AngelScript VM 的 `as_context.cpp` / `as_scriptengine.cpp` 又明确表明，脚本 `new` 会直接走 `userAllocScriptObject -> UASClass::AllocScriptObject()`。因此当前所有 script object 的自定义分配路径，都会以 `TransientPackage` 作为 outer，并被 `AllocScriptObject()` 打上 `RF_Transient`。 |
| 根因 | 类生成层已经预留了“构造 outer 作用域”机制，但调用链上没有任何地方在进入 AngelScript `new` 之前设置该线程局部 outer，导致对象分配语义退化成固定的 fallback。 |
| 影响 | 这会让脚本里通过 `new` 创建的 `UObject` / `UActorComponent` 派生对象无法挂到预期 owner、world 或持有者上，生命周期、序列化、事务和 GC 可达性都会退化成 transient 临时对象语义。对于依赖 outer 进行注册或销毁联动的对象，这属于构造/析构不对称的硬错误：创建时已经丢失归属信息，后续再正确析构也无法补回。 |

### 发现 26：类生成器一边支持 AngelScript namespace，一边又用未限定 `ClassName` 做 UClass 命名和查找，命名空间类会互相覆盖

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` / `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` |
| 行号 | `AngelscriptClassGenerator.cpp:145-156, 160-170, 224-225, 1739-1766, 2568-2573, 5912-5934, 5054-5068` / `AngelscriptEngine.h:1335-1345` |
| 描述 | 当前实现明确支持 namespaced script type：`GetNamespacedTypeInfoForClass()` 会读取 `ClassDesc->Namespace` 并在对应 `asSNameSpace` 下取 `TypeInfo`。但同一个类在 UE 侧的关键路径几乎全部退化成未限定短名：`DataRefByName.Add(ClassData.NewClass->ClassName, ...)` 只用 `ClassName` 建索引；`GetClassDesc()` 和 `FAngelscriptEngine::GetClass()` 也只按 `ClassName` 比较；匹配旧类时仅比较 `OldClassDesc->ClassName == ClassDesc->ClassName`；真正创建/替换 `UASClass` 时，又把 `ClassDesc->ClassName` 送进 `GetUnrealName()` 后直接 `FindObject`。这意味着两个不同 namespace 下只要短类名相同，就会在 descriptor lookup、旧类匹配和最终 `UClass` 对象命名上发生合并。`ResolveInterfaceClass()` 的脚本接口路径同样先调用这个 namespace-blind 的 `GetClassDesc()`。 |
| 根因 | 类生成器把“脚本编译期类型身份”与“运行时 UClass 身份”拆成了两套 key：前者是 `namespace + class name`，后者却大量只看 `ClassName`。namespace 只在拿 `asITypeInfo` 时生效，没有贯穿到 reload、注册和反射对象命名。 |
| 影响 | 一旦项目里存在两个同名不同 namespace 的脚本类或接口，后加载/后分析的那个条目会覆盖 `DataRefByName`，full reload 还可能把另一个 namespace 的旧 `UASClass` 误认成当前类并重命名为 `_REPLACED_*`。结果不仅是 `StaticClass`/接口解析可能绑到错误的 `UClass`，还会直接破坏热重载版本链和类型注册顺序，属于动态 `UClass` 生成一致性的基础缺陷。 |

### 发现 27：`GetUnrealName()` 会把 `A`/`U` 前缀压成同一 `UClass` 名，而同模块冲突会被误判成“同一类正在 reload”

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 行号 | `160-170, 223-280, 2568-2586` |
| 描述 | 非 struct 类型的 UE 名字统一通过 `GetUnrealName(false, ClassName)` 生成，它会在首字母为 `U` 或 `A` 且第二个字符大写时直接剥掉前缀。因此 `AFoo` 和 `UFoo` 会落到同一个 `UnrealName = "Foo"`。分析阶段的冲突检查虽然会先 `FindObject` 看包里是否已有同名对象，但它只在“冲突对象来自别的模块”时才报错；如果冲突对象的 `ScriptTypePtr` 仍指向当前正在 reload 的模块，就会被视为可替换对象继续往下走。随后 full reload 又会按这个同名 `Foo` 去 `FindObject<UASClass>()`、重命名旧类并创建新类。也就是说，在同一个 reload 批次里，`AFoo` 与 `UFoo` 这样的不同脚本类会被串到同一个 `UASClass` 名位上。 |
| 根因 | 生成器默认“剥前缀后的 Unreal 名字”与“原始脚本类名”之间是一一对应关系，并据此把“同模块、同 `UnrealName`”视为同一类型的 reload。这个前提对 `A*` / `U*` 双前缀命名并不成立。 |
| 影响 | 只要项目里出现同后缀的 actor/object 脚本类，类生成阶段就可能把一个类当成另一个类的 `ReplacedClass`。后果包括错误的 superclass/constructor 组合、版本链串接到错误节点、以及最终 `/Script/Angelscript.Foo` 对应哪一个脚本类完全取决于处理顺序。这直接破坏 `UClass` 生成一致性，且错误发生在注册层，后续日志往往只会表现成莫名其妙的 reload/反射异常。 |

---

## 分析 (2026-04-08 03:48)

### 发现 28：`ReplicatedUsing` / `ReplicationCondition` / `SaveGame` / `Config` 等 property specifier 在 `SoftReloadOnly` 下会保留旧 `FProperty` flags 与 replication 布局

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` / `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` / `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` |
| 行号 | `AngelscriptEngine.h:1189-1197` / `AngelscriptClassGenerator.cpp:1318-1323, 2081-2088, 2236-2275, 2947-2975, 3017-3023, 4151-4174, 5049` / `ASClass.cpp:894-908` |
| 描述 | property 定义发生变化时，分析阶段最多只把类标成 `FullReloadSuggested`，而真正决定是否走 full reload 的 `ShouldFullReload()` 只关心“是否 interface / 是否实现接口”，不会因为 property specifier 变化而升级。结果是 `SoftReloadOnly` 会直接进入 `DoSoftReload()`；这条路径对旧 `FProperty` 只做 `Property->Link(ArDummy)` 和 offset 重新链接，不会重写 `CPF_Net`、`CPF_RepNotify`、`CPF_RepSkip`、`CPF_SaveGame`、`CPF_Config`、`CPF_SkipSerialization` 等 flags，也不会更新 `RepNotifyFunc`。与此同时，这些状态只在 full reload 的 `AddClassProperties()` 中写入，而 `SetUpRuntimeReplicationData()` 也只在 `FinalizeClass()` 里执行，soft reload 完全不会重建 replication layout。`UASClass::GetLifetimeScriptReplicationList()` 之后仍按旧 `FProperty` flags 和旧 `GetBlueprintReplicationCondition()` 生成 lifetime props。 |
| 根因 | property specifier 的“定义变化”与运行时 `FProperty`/replication state 的“重建”被拆成了两层逻辑：前者只把 reload 需求提升到建议级别，后者却只存在于 full reload/finalize 路径。`SoftReloadOnly` 缺少对 property flags、`RepNotifyFunc` 和 replication data 的对称同步。 |
| 影响 | 在 body-only 工作流里切换 `Replicated`、`ReplicatedUsing`、`COND_*`、`NotReplicated`、`SaveGame`、`Config` 或 `SkipSerialization` 后，运行时类仍会沿用旧 property 语义。直接后果包括：旧 `RepNotify` 继续触发或新 `RepNotify` 永远不触发，网络复制条件不随源码变化，已经取消复制的字段仍出现在 lifetime props 中，以及配置/存档/序列化行为停留在旧版本。这会让热重载后的 `UClass` 反射状态与网络行为同时失真。 |

### 发现 29：full reload 对 `UASStruct` 和动态 delegate signature 也会留下常驻的 `_REPLACED_*` 对象

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.h` / `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 行号 | `ASStruct.h:14-28` / `AngelscriptClassGenerator.cpp:2668-2679, 2720-2731, 3202-3230, 3889-3933, 2414-2418, 4991-5036` |
| 描述 | 结构体和 delegate 的 full reload 复用了与 `UASClass` 相同的“替换旧对象”策略，但回收逻辑更不完整。`CreateFullReloadStruct()` 先把旧 `UASStruct` 重命名成 `*_REPLACED_*`，再创建一个带 `RF_Standalone | RF_MarkAsRootSet` 的新 `UASStruct`；`DoFullReloadStruct()` 随后把旧 struct 的 `NewerVersion` 指向新 struct。reload 收尾时，对 `ReplacedStruct` 只做 `ScriptType = nullptr` 和 `UpdateScriptType()`，没有 `RemoveFromRoot()` 或清 `RF_Standalone`。delegate 路径更直接：`CreateFullReloadDelegate()` 同样把旧 `UDelegateFunction` 重命名成 `*_REPLACED_*`，新建的 signature function 也带 `RF_Standalone | RF_MarkAsRootSet`，但后续 `DoFullReload(FDelegateData&)` 只广播 `OnDelegateReload`，仓库内没有任何对旧 delegate function 的解 root / 清 flag 路径。与之相对，现有的 `CleanupRemovedClass()` 只覆盖“removed class / removed struct”，并不处理 replaced struct 或 replaced delegate。 |
| 根因 | 动态类型替换逻辑统一采用“rename old + root new”的发布模式，但对象回收只覆盖“真正删除的类/结构体”，没有为“被替换但仍保留作版本跳板”的 struct 和 delegate 建立对称的退场流程。 |
| 影响 | 长时间迭代后，包内会持续累积 rooted 的 `_REPLACED_*` `UASStruct` 和旧 delegate signature `UDelegateFunction`。对 struct 来说，`ASStruct.h` 中的 `NewerVersion` 链会像 class 一样只增不减；对 delegate 来说，虽然没有显式版本链，但旧签名对象会继续常驻并占着历史 `FProperty`/metadata。结果是动态类型注册表越来越脏，编辑器内存占用持续增长，后续基于对象枚举或名字查找的调试/反射行为也更容易受到历史残留污染。 |

### 发现 30：script interface 的 `UFunction` 只按名字生成空壳，参数与返回值被全部丢弃

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 行号 | `2769-2835` |
| 描述 | interface full reload 分支会遍历 `InterfaceMethodDeclarations` 生成 `UFunction`，但实现里只从声明字符串中切出函数名，然后直接创建 `UFunction`。源码注释明确写着“`For now, create a minimal UFunction with just the name`”。随后代码没有为返回值调用 `AddFunctionReturnType()`，也没有像普通类方法那样为参数生成 `FProperty`；生成结果仅设置了 `FUNC_Event | FUNC_BlueprintEvent | FUNC_Public`、`ReturnValueOffset = MAX_uint16`，再 `Bind()` / `StaticLink(true)` 并加入 function map。也就是说，像 `void TakeDamage(float Amount)`、`int GetHealth() const` 这类 interface 方法，落到 UE 反射层时会退化成“同名、零参数、无返回值”的空签名函数。 |
| 根因 | interface 路径没有复用普通方法的 `UFunction` 生成流程，而是采用了一个临时的 name-only stub 实现；该实现只满足“类上能找到一个同名 UFunction”，并没有把方法签名同步到 `UFunction`。 |
| 影响 | script interface 的 `UClass` 反射信息与源码声明不一致。任何依赖 UE `UFunction` 签名的路径，例如 Blueprint/反射调用、参数打包、返回值处理、基于 `FindFunctionByName()` 的接口元数据检查，都会看到错误的零参数签名。结果不是简单的“少点元数据”，而是接口方法的 ABI 在 UE 侧被生成错了，直接破坏动态 `UClass` 生成一致性。 |

### 发现 31：现有 interface 自动化只验证“函数名存在”，没有验证 `UFunction` 签名，因此空壳接口函数不会被测出来

| 项目 | 内容 |
|------|------|
| 维度 | D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceAdvancedTests.cpp` / `Plugins/Angelscript/Source/AngelscriptTest/Interface/` |
| 行号 | `AngelscriptInterfaceAdvancedTests.cpp:185-202` / `Interface` 目录定向 grep 统计 |
| 描述 | interface 相关测试里，最接近生成校验的用例只做了两件事：`FindFunctionByName(TEXT("TakeDamage"))` / `FindFunctionByName(TEXT("GetHealth"))` 不为空，以及 `TFieldIterator<UFunction>` 统计到的方法数等于 2。测试没有检查 `NumParms`、`ParmsSize`、`ReturnValueOffset`、参数 `FProperty`，也没有验证通过 UE 反射调用 interface 方法时的参数收发。对 `Plugins/Angelscript/Source/AngelscriptTest/Interface/` 做定向 grep，`NumParms`、`ParmsSize`、`ReturnValueOffset` 命中均为 0。 |
| 根因 | 当前 interface 测试矩阵关注的是“UInterface 是否生成、`ImplementsInterface` 是否成立、脚本侧 cast/方法调用是否可用”，没有把 UE 反射层的 `UFunction` 签名一致性当成独立回归面。 |
| 影响 | 发现 30 这类“函数名存在但签名全错”的生成缺陷，在现有自动化下会稳定漏检。测试能够证明 interface 的名字和实现关系大体存在，但不能证明生成出来的 `UFunction` 可以被 Blueprint/反射系统按正确 ABI 使用。 |

### 发现 32：现有 `HotReload` 自动化没有覆盖 property replication / config / savegame specifier 漂移

| 项目 | 内容 |
|------|------|
| 维度 | D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp` / `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPropertyTests.cpp` / `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp` / `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPerformanceTests.cpp` / `Plugins/Angelscript/Source/AngelscriptTest/HotReload/` |
| 行号 | `AngelscriptHotReloadScenarioTests.cpp:112, 309` / `AngelscriptHotReloadPropertyTests.cpp:106, 181` / `AngelscriptHotReloadFunctionTests.cpp:380, 490` / `AngelscriptHotReloadPerformanceTests.cpp:114, 294, 296` / `HotReload` 目录定向 grep 统计 |
| 描述 | 仓库里确实存在多组 `SoftReloadOnly` 自动化用例，但对 `Plugins/Angelscript/Source/AngelscriptTest/HotReload/` 做定向 grep，`ReplicatedUsing`、`Replicated`、`SaveGame`、`Config=` 命中全部为 0。也就是说，当前 hot reload suite 从未验证过“属性复制语义变化后，`FProperty` flags、`RepNotifyFunc`、replication condition、config/savegame 行为是否同步到旧 `UClass`”这一整类问题。 |
| 根因 | 现有热重载测试重点放在属性值保留、函数体替换和性能烟雾场景，没有把 property specifier 自身当成 hot reload 对象来验证。 |
| 影响 | 发现 28 这类不会立刻崩溃、但会让 `FProperty`/replication state 与源码脱节的问题，当前自动化不会报警。只跑现有 `HotReload` suite，无法证明 property 级网络、配置与序列化语义在 `SoftReloadOnly` 后仍保持一致。 |

---

## 分析 (2026-04-08 04:06)

### 发现 33：`GConstructASObjectWithoutDefaults` 是跨线程共享开关，soft reload 期间会污染无关对象的 defaults 执行

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` / `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` |
| 行号 | `AngelscriptClassGenerator.cpp:4085-4103` / `ASClass.cpp:74, 988, 1360-1361, 1416-1417, 1468-1469` |
| 描述 | `PrepareSoftReload()` 为了构造 `CDONoDefaults`，直接把全局 `GConstructASObjectWithoutDefaults` 置为 `true`，随后调用 `NewObject` 创建临时 CDO。`StaticActorConstructor()`、`StaticComponentConstructor()` 和 `StaticObjectConstructor()` 都无条件读取这个全局 bool 决定是否执行 defaults，然后立刻把它清回 `false`。同文件前面的线程检查又明确写着 defaults 允许在 async loading thread 执行。这意味着“禁用 defaults”并不是绑定到某个对象或某个线程上的局部状态，而是在 soft reload 窗口内对整个进程生效；只要这段时间里有别的 script object 在另一条线程或另一条构造链上进入这些静态构造函数，就会读到同一个开关。 |
| 根因 | “为 `CDONoDefaults` 临时关闭 defaults”被实现成了进程级共享 bool，而不是 `thread_local` 或 RAII 作用域状态。热重载基线构造和普通对象构造共享了同一份全局可变状态。 |
| 影响 | soft reload 期间，任何并发创建的 script actor/component/object 都可能被误判成“正在构造 no-defaults 基线”，从而跳过本应执行的 `ExecuteDefaultsFunctions()`。这不是单一 CDO 基线失真，而是会把无关对象实例直接生成成缺 defaults 的错误状态；在启用 async loading 或并发对象创建时，热重载会引入难以复现的跨线程状态污染。 |

### 发现 34：`HideCategories` 与 `BlueprintSpawnableComponent` 这类 class-level editor metadata 在 `SoftReloadOnly` 后会停留在旧版本

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 行号 | `1312-1322, 2081-2093, 2291-2293, 3383-3385, 4113-4290, 5448-5457` |
| 描述 | 类级 metadata 变化在分析阶段只会把 `ReloadReq` 提升到 `FullReloadSuggested`：`Meta.OrderIndependentCompareEqual()` 和 `AreFlagsEqual()` 变更都不会强制 full reload。真正执行时，`ShouldFullReload()` 在 soft reload 模式下并不会因为这些“建议级”变化而转走 full reload，于是类会进入 `DoSoftReload()`。但 `DoSoftReload()` 只重链 property/function、更新少量 `ClassFlags`，没有任何对 `UClass` metadata map 的重放。相对地，`HideCategories += " DefaultComponents"` 只在 full class creation 时写入，`BlueprintSpawnableComponent` 只在 `FinalizeComponentClass()` 中写入，而 `FinalizeClass()` 本轮也只会对 `ShouldFullReload()` 为真的类执行。结果是这些 class-level editor metadata 在 `SoftReloadOnly` 后继续保留旧值。 |
| 根因 | reload 决策把“类 metadata 改动”降级成了建议级 full reload，但 soft reload 实现没有提供 metadata 同步分支；部分关键 editor metadata 只存在于 full reload/new-class finalization 路径。 |
| 影响 | 修改 actor 的 `HideCategories` 相关声明或 component 的可 spawnable 状态后，热重载得到的 `UClass` 反射结果与源码不一致。依赖这些 metadata 的编辑器路径会继续读取旧值，例如 details 面板分类或组件可添加性不会随脚本声明即时更新；这是动态 `UClass` 生成一致性的直接缺口。 |

### 发现 35：现有自动化对 class-level editor metadata 的 soft reload 漂移是空白，`BlueprintSpawnableComponent` / `HideCategories` 在测试树里均为 0 命中

| 项目 | 内容 |
|------|------|
| 维度 | D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp` / `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 行号 | `AngelscriptHotReloadScenarioTests.cpp:1-120` / `AngelscriptClassGenerator.cpp:3383-3385, 5448-5457` |
| 描述 | 现有 hot reload 场景测试开头就能看出覆盖面仍然集中在“属性保留 / 新增属性 / 函数体变化 / 结构性变更”四类路径，没有任何针对 class-level editor metadata 的断言。我又对 `Plugins/Angelscript/Source/AngelscriptTest/HotReload` 和整个 `Plugins/Angelscript/Source/AngelscriptTest` 做了定向统计，`BlueprintSpawnableComponent|HideCategories` 命中都为 `0`。这意味着发现 34 中那类 “源码已改、`UClass` metadata 仍停留旧值” 的 soft reload 回归，当前自动化完全没有守护。 |
| 根因 | 测试矩阵把 hot reload 正确性主要定义成“运行时属性和函数行为没坏”，没有把 editor-facing `UClass` metadata 一致性纳入回归面。 |
| 影响 | 只跑现有自动化，无法证明 component 可添加性、details 分类等编辑器行为会随着 `SoftReloadOnly` 同步更新。发现 34 这类缺陷即使长期存在，也不会在当前测试体系里被主动报出。 |

---

## 分析 (2026-04-08 04:16)

### 发现 36：`SoftReloadOnly` 根本不消费 `FullReloadRequired`，结构性变更也会被强行送进原地 soft reload

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | Critical |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 行号 | `1079-1135, 1200-1248, 1298-1307, 2081-2093, 2144-2295, 4151-4275` |
| 描述 | 分析阶段明确定义了多类“必须 full reload”的结构性变化：superclass 改变、property 删除或类型/定义变化、method 删除或签名/定义变化，以及 script size 变大到“不能原地替换”。这些路径都会把 `ClassData.ReloadReq` 提升到 `FullReloadRequired`。但真正执行 reload 时，`PerformReload(false)` 全程只看 `ShouldFullReload(ClassData)`，而这个判定在 soft reload 模式下完全不检查 `ReloadReq`，只对 interface、实现了接口的类和 brand-new class 返回 `true`。结果就是哪怕类已经被源码分析标记为 `FullReloadRequired`，它仍会先走 `LinkSoftReloadClasses()`，随后进入 `PrepareSoftReload()` 和 `DoSoftReload()`。而 `DoSoftReload()` 的实现只会重链“新描述里还存在”的 property/function，不会删除旧 `FProperty` / `UFunction`，也不会重建类对象。换句话说，`FullReloadRequired` 在 `SoftReloadOnly` 下只是被记录，执行层没有任何硬门槛。 |
| 根因 | reload 系统把“分析阶段的安全判定”和“执行阶段选择 full/soft reload”的决策拆成了两套逻辑，但 `ShouldFullReload()` 没有把 `ReloadReq` 纳入条件，导致 `FullReloadRequired` 无法约束 `SoftReloadOnly` 的执行路径。 |
| 影响 | 一旦开发流程或自动化触发 `SoftReloadOnly`，系统就可能在 superclass 已变、成员已删、签名已变甚至 script 内存布局已扩大的情况下仍然尝试原地 relink。直接后果包括：已删除的 `FProperty` / `UFunction` 幽灵残留在旧 `UClass` 上，旧父类关系和旧反射布局继续存活，以及分析阶段已经认定“不能原地替换”的大对象布局仍被送进原地重建流程。这不是单个 specifier 漂移，而是整个热重载安全阈值在执行层失效。 |

---

## 分析 (2026-04-08 04:21)

### 发现 37：script interface 的实现校验和运行时 dispatch 都只按函数名匹配，签名完全不参与

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` / `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` / `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 行号 | `AngelscriptEngine.h:59-62` / `AngelscriptPreprocessor.cpp:1106-1153` / `AngelscriptClassGenerator.cpp:56-65, 5160-5184` |
| 描述 | interface 方法的完整声明在预处理阶段其实是已知的，`RegisterObjectMethod()` 也是按完整 `MethodDecl` 注册到 AngelScript type 上的。但真正保存到 `FInterfaceMethodSignature` 的只有一个 `FName FunctionName`。后续 runtime generic callback `CallInterfaceMethod()` 从 `GetUserData()` 取出这个结构后，直接 `Object->FindFunction(Sig->FunctionName)`；类实现校验也一样，只是对每个 interface `UFunction` 调 `NewClass->FindFunctionByName(InterfaceFunc->GetFName())`。整个链路没有任何基于参数列表、返回值或完整 declaration 的匹配步骤。结果是：实现类只要存在一个同名 `UFunction`，即使签名不一致，也会被当成“已经实现了接口”；运行时真正调用时，也只会把这个同名函数拿来做 reflective dispatch。 |
| 根因 | interface 支持在预处理阶段保留了完整声明，但 class generator 和 runtime dispatch 共同把接口方法身份压缩成了单个函数名，导致“名字相同”被错误地当成了“接口 ABI 一致”。 |
| 影响 | script interface 的实现约束会失真：错误签名的方法可以蒙混过关，直到运行时才在反射调用链上暴露为参数打包错误、返回值语义错误或错误函数被调用。对有同名不同语义的方法尤其危险，因为当前实现既不能验证“签名对不对”，也不能保证 dispatch 到的是接口声明想要的那个实现。 |

---

## 分析 (2026-04-08 04:22)

### 发现 38：delegate 签名已经被分析器判成 `FullReloadRequired`，`SoftReloadOnly` 仍会直接复用旧 `UDelegateFunction`

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 行号 | `1539-1549, 2105-2113, 2170-2228, 3889-3928, 4016-4021` |
| 描述 | delegate 分析阶段已经把“签名变更”定义成必须 full reload：只要旧签名不存在、`SignatureMatches(..., true)` 失败，或 `IsDefinitionEquivalent()` 失败，就会把 `DelegateData.ReloadReq` 提升到 `FullReloadRequired`。但执行阶段的 `ShouldFullReload(FDelegateData&)` 在 soft reload 模式下同样完全不看这个字段，只在“当前就是 full reload”或“brand-new delegate”时返回 `true`。因此 `PerformReload(false)` 遇到一个签名已变的 delegate，不会走 `DoFullReload()` 去重新生成 `UDelegateFunction` 的参数/返回值 `FProperty`，而是直接执行 `LinkSoftReloadClasses()`，把 `NewDelegate->Function` 指回旧 `UDelegateFunction`，仅更新 `ScriptType->SetUserData(...)`。 |
| 根因 | delegate reload 的安全判定与执行判定同样分裂成两套逻辑，而 `ShouldFullReload(FDelegateData&)` 没有把 `ReloadReq` 作为硬条件，导致“签名必须重建”的判断在 `SoftReloadOnly` 下失效。 |
| 影响 | 一旦 delegate 的参数列表、返回值或其他定义发生变化，`SoftReloadOnly` 会让脚本源码和运行时 `UDelegateFunction` ABI 脱节。后续绑定、广播和反射参数封送都仍按旧签名执行，轻则表现为绑定失败和参数读写错位，重则在 native/script 边界上触发错误的内存解释。 |

---

## 分析 (2026-04-08 04:31)

### 发现 39：`DoSoftReload()` 会把新 `DefaultsFunction` 换进类上，但对旧实例/CDO 重建时完全不执行 defaults，导致热重载后同一类出现“双轨默认值”

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` / `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` |
| 行号 | `AngelscriptClassGenerator.cpp:4140-4141, 4281, 4623, 4755, 4825-4848, 5889-5903` / `ASClass.cpp:1405, 1458, 1494` |
| 描述 | `DoSoftReload()` 明确写着“`Soft reloads preserve defaults-code`”，并把 `ClassDesc->DefaultsCode` 回退成旧版本；但真正执行时，它仍然调用 `UpdateConstructAndDefaultsFunctions()`，把 `Class->DefaultsFunction` 直接刷新到新 `ScriptType` 上。之后，新创建的 actor/component/object 会在三个 `Static*Constructor()` 里执行这个新 `DefaultsFunction`。相反，soft reload 对已有实例和已有 CDO 的重建全部走 `ReinitializeScriptObject()`，而这个 helper 只 placement-new `asCScriptObject` 并调用构造函数，从头到尾没有任何 `ExecuteDefaultsFunctions()`。结果就是同一次 soft reload 之后，旧对象沿用“无 defaults 的重建 + 旧值拷贝”，未来新对象却立即执行新 defaults 代码。 |
| 根因 | reload 设计把“默认值语义”拆成了两个互相矛盾的实现：分析层想保留旧 defaults 语义，运行层却无条件换成新的 `DefaultsFunction`；同时实例/CDO 重建 helper 又完全绕开了 defaults 执行路径。 |
| 影响 | 默认值变更在 soft reload 后不会表现为单一一致的类语义，而是分裂成“旧实例/CDO 一套、新建对象一套”。这会直接破坏动态 `UClass` 生成一致性：同一脚本类在同一版本下，不同对象来源会观察到不同默认值，尤其容易污染 CDO 基线、实例保留判断以及后续再次热重载时的差异比较。 |

### 发现 40：class-level `Config=` / `DefaultConfig` 变更不会被 soft reload 同步，`ConfigName` 甚至不参与 reload 判定

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` / `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 行号 | `AngelscriptEngine.h:1112, 1187-1197` / `AngelscriptClassGenerator.cpp:1317-1322, 3294-3307, 4208-4242` |
| 描述 | `FAngelscriptClassDesc` 明确保存了 `ConfigName`，但 `AreFlagsEqual()` 比较 class-level reload 条件时只覆盖 `bAbstract`、`bTransient`、`bHideDropdown`、`bDefaultToInstanced`、`bEditInlineNew`、`bIsDeprecatedClass`、`bPlaceable`、`bIsInterface` 和 `ImplementedInterfaces`，没有比较 `ConfigName`。分析阶段随后直接用 `AreFlagsEqual()` 决定是否提升 reload requirement。与此同时，真正把 `ClassConfigName`、`CLASS_Config`、`CLASS_DefaultConfig` 写进 `UClass` 的代码只出现在 full class creation 路径；`DoSoftReload()` 里只回放了若干普通 `ClassFlags`，完全没有 config 相关同步。 |
| 根因 | class-level config 语义没有被纳入统一的“类定义差异”模型：分析阶段漏掉 `ConfigName`，执行阶段 soft reload 也没有对应的 `UClass` 状态回放。 |
| 影响 | 修改脚本类的 `Config=` 目标或 `DefaultConfig` 行为后，soft reload 得到的 `UASClass` 会继续沿用旧 `ClassConfigName` / `CLASS_Config` / `CLASS_DefaultConfig`。直接结果是 CDO 配置装载与保存路径停留在旧版本，类反射状态和源码声明脱节；而且因为 `ConfigName` 根本不参与 `AreFlagsEqual()`，这类变更连“建议 full reload”都不会被稳定报出来。 |

### 发现 41：自动化没有覆盖 defaults-body reload 与 class-level config reload，现有 green test 不能证明这两条路径正确

| 项目 | 内容 |
|------|------|
| 维度 | D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadAnalysisTests.cpp` / `Plugins/Angelscript/Source/AngelscriptTest/` |
| 行号 | `AngelscriptHotReloadAnalysisTests.cpp:76-364` / `AngelscriptTest` 目录定向 grep 统计 |
| 描述 | 现有 `HotReloadAnalysisTests` 覆盖的是“不变更 / 属性数量变化 / 父类变化 / body-only / 类新增删除 / 函数签名变化”这些判定场景，没有任何一个用例构造 `Config=`、`DefaultConfig` 或 defaults-body 变化。我又对整个 `Plugins/Angelscript/Source/AngelscriptTest` 做了定向 grep，`__InitDefaults`、`DefaultConfig`、`Config=` 命中均为 `0`。这意味着当前自动化既没有验证发现 39 中“旧对象与新对象 defaults 分叉”的行为，也没有验证发现 40 中 class-level config specifier 漂移。 |
| 根因 | hot reload 测试矩阵目前把“结构变化”和“body-only 变化”当成主要回归面，但没有把 defaults 语义和 class-level config 语义作为独立的 reload contract 来验证。 |
| 影响 | 只跑现有 `HotReload` / `ClassGenerator` 自动化，无法证明 defaults-body 变更后的对象一致性，也无法证明 `Config=` / `DefaultConfig` 改动能触发正确的 reload 决策与 `UClass` 同步。发现 39 和 40 这类回归可以长期潜伏而不被测试报警。 |

---

## 分析 (2026-04-08 10:20)

### 发现 42：类生成器把 `bModuleSwapInError` 当作“下次重编译提示”，错误类仍会先注册、建 CDO 并留在运行时

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` / `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | `AngelscriptClassGenerator.cpp:2299-2313, 5039-5212, 5254-5437, 5597-5676` / `AngelscriptEngine.cpp:4047-4053` |
| 描述 | `FinalizeClass()` 一开始就把 `ClassData.bFinalized = true`，随后在接口校验、`DefaultComponent` / `OverrideComponent` 收集等分支里多次把 `ModuleData.NewModule->bModuleSwapInError = true`，但函数末尾仍无条件执行 `NotifyRegistrationEvent(...)`。之后 reload 主流程又先跑 `CallPostInitFunctions()`、`InitDefaultObjects()`，最后才在 `VerifyClass()` 做“Very last verification step”。也就是说，哪怕类在 finalization 或 verify 阶段已经被标成 swap-in error，它也已经被注册到 `/Script/Angelscript`，并可能已经创建了 CDO 与默认子对象。更关键的是，`AngelscriptEngine.cpp` 在收尾阶段对 `bModuleSwapInError` 的处理只是把源文件加入 `PreviouslyFailedReloadFiles`，没有撤销本次 swap-in，也没有清理刚刚注册/实例化的新类。 |
| 根因 | reload 管线把 `bModuleSwapInError` 设计成“保留新模块、下次继续报错”的标记，而不是事务式中止条件；类注册、CDO 初始化和最终校验之间缺少 rollback 边界。 |
| 影响 | 一次带 synthetic error 的 full reload 不会回退到旧 `UClass`，而是把“已知不合法”的新类继续留在运行时。后续任何依赖类注册表、CDO 或默认组件树的编辑器/运行时路径，都会读取到与源码验证结果相冲突的脏状态，导致问题从“编译期诊断”升级成“运行时反射和对象状态已被污染”。 |

### 发现 43：dynamic subsystem 会在类验证失败后继续完成“旧实例停用 + 新实例激活”，把坏类真正接入全局子系统集合

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` / `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | `AngelscriptClassGenerator.cpp:2446-2463, 2642-2647, 2306-2312, 5597-5676` / `AngelscriptEngine.cpp:4047-4053` |
| 描述 | `CreateFullReloadClass()` 对 `UDynamicSubsystem` / `UWorldSubsystem` 派生脚本类会先 `DeactivateExternalSubsystem(ReplacedClass)`，并把新类放进 `ReinstancedSubsystems`。reload 尾声又无条件遍历 `ReinstancedSubsystems` 调 `ActivateExternalSubsystem(NewSubsystem)`。问题在于，类级合法性检查里的多处 `ScriptCompileError(...)` 与 `bModuleSwapInError = true` 发生在这之前的 `VerifyClass()` 阶段，但不会阻止后面的 subsystem 激活；`AngelscriptEngine.cpp` 收尾同样不会因为 `bModuleSwapInError` 回滚本次 swap-in。结果是：哪怕新类已经被验证为非法，旧 subsystem 也已经被停用，而新 subsystem 仍会被激活。 |
| 根因 | subsystem 生命周期操作被绑定在“类对象已生成”而不是“类对象已通过最终验证”上；与此同时，module swap error 机制没有提供对应的 subsystem rollback。 |
| 影响 | 一次带组件/attachment/验证错误的 subsystem reload，会把原本工作的旧 subsystem 下线，并把有已知问题的新 subsystem 接入全局集合。由于 subsystem 往往承担世界级或引擎级状态管理，这会把 ClassGenerator 的局部错误放大成全局行为回归，且恢复路径只能依赖下一次成功编译或重启。 |

### 发现 44：literal asset 的 `__Init_*` 用户代码会在 `InitDefaultObjects()` 之前执行，能观察并放大未完成的类初始化状态

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` / `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 行号 | `AngelscriptPreprocessor.cpp:4111-4133` / `AngelscriptClassGenerator.cpp:2302-2304, 5775-5804, 5807-5843` |
| 描述 | 预处理器会把 literal asset 语法展开成 `Get{Name}()` 包装函数；这个包装函数在创建 asset 后立刻调用 `__Init_{Name}(__Asset_{Name})`，并把 `Get{Name}` 记录到 `PostInitFunctions`。reload 主流程随后在 `InitDefaultObjects()` 之前执行 `CallPostInitFunctions()`，也就是在“tick 预计算 + CDO 初始化”之前，先运行一轮真实脚本代码。由于 `__Init_{Name}` 是 `external_implicit_this` 的用户实现，这里不是单纯的 asset materialization，而是允许任意脚本逻辑在类默认对象阶段之前介入。 |
| 根因 | 管线默认把 post-init 当作“只做字面量资源落地”的安全步骤，但生成代码实际上会进入用户提供的 `__Init_*` 脚本体；执行时机却被放在默认对象初始化之前。 |
| 影响 | 若 asset 初始化逻辑访问 `StaticClass()`、读取 CDO、实例化脚本对象或依赖最终 tick/defaults 状态，它观察到的将是 pre-`InitDefaultObjects()` 的半成品类状态。这样一来，类初始化顺序不再由 `InitDefaultObjects()` 单点控制，而会被 literal asset 初始化逻辑提前打断，形成顺序敏感的热重载问题。 |

### 发现 45：`PostInitFunctions` 只按短函数名分派，0 命中和错误命中都不会升级成 reload 失败

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` / `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 行号 | `AngelscriptPreprocessor.cpp:4132-4133` / `AngelscriptClassGenerator.cpp:5783-5801` |
| 描述 | 预处理器往 `PostInitFunctions` 里存的是裸字符串 `Get{Name}`。执行时，`CallPostInitFunctions()` 只在线性遍历 `globalFunctionList` 时比较 `ScriptFunction->name`，既不带 namespace，也不带完整 declaration；命中后立即 `break`，因此同模块里若存在同短名函数，真正被执行的是“遍历遇到的第一个”，不是经过限定后的目标函数。与此同时，局部变量 `bFound` 只被赋值，从未在循环结束后参与任何报错或 `bModuleSwapInError` 判定；如果一个 `Get{Name}` 根本没找到，函数会静默结束。 |
| 根因 | post-init 调度把“需要调用哪个 getter”的身份压缩成了未限定短名字符串，并且缺少“未找到目标函数”的强制失败分支。 |
| 影响 | literal asset 的初始化可能因为名字碰撞而调用到错误 getter，也可能因为目标 getter 丢失而直接跳过，且 reload 结果仍会被当作成功 swap-in。最终表现不是稳定崩溃，而是某些 asset 没有按当前脚本定义被 materialize，问题会以资源缺失或状态不一致的形式滞后暴露。 |

---

## 分析 (2026-04-08 10:47)

### 发现 46：full reload 在旧实例真正被 GC 前先清空类级 `ScriptTypePtr`，旧引用一旦再触发脚本分派就会落入空类型窗口

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` / `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.cpp` / `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` / `Engine/Source/Runtime/Engine/Private/UnrealEngine.cpp` |
| 行号 | `AngelscriptClassGenerator.cpp:2398-2444` / `angelscript.cpp:4-11` / `ASClass.cpp:91-100, 111-120` / `UEngine::ForceGarbageCollection(bool)` |
| 描述 | full reload 收尾阶段会先把 `ReplacedClass->ScriptTypePtr`、以及所有仍指向旧 script type 的 `UASClass->ScriptTypePtr` 清成 `nullptr`，然后才调用 `GEngine->ForceGarbageCollection(true)`。但本插件把 `asIScriptObject::GetObjectType()` 改成了“从 `UASClass::GetFirstASClass((UObject*)this)->ScriptTypePtr` 读取当前类级 type 指针”，而不是读对象自身的 AngelScript header。与此同时，运行时虚函数分派 `VerifyScriptVirtualResolved()` / `ResolveScriptVirtual()` 又直接把这个类级 `ScriptTypePtr` 当成 `asCObjectType*` 使用。UE5 的 `UEngine::ForceGarbageCollection(bool)` 源码本身只是在 `UnrealEngine.cpp` 里设置 `TimeSinceLastPendingKillPurge` 和 `bFullPurgeTriggered`，并不在调用点同步销毁对象。这样一来，只要外部系统在这次 full reload 之后、真正 GC 执行之前，还握着某个旧 script 实例或旧 blueprint 派生实例的引用并再次触发脚本调用，该实例看到的对象类型就已经是 `nullptr`。 |
| 根因 | live object 的脚本类型身份没有存放在对象自身，而是经由 `UASClass::ScriptTypePtr` 间接解析；full reload 又在对象实际回收前提前清空了这份共享类状态，并把“真正销毁旧实例”交给后续的 GC 请求。 |
| 影响 | 旧引用不会只是在析构阶段跳过脚本 destructor；它们在 GC 之前就已经进入“不可再调用”的半失效状态。任何在这个窗口里发生的 `BlueprintEvent` / `BlueprintOverride` / 反射调用，都会在 `ResolveScriptVirtual()` 上命中空 `ObjectType`，在非 shipping 构建下触发 `checkSlow`，在其余路径上则可能继续解引用空类型信息，形成 full reload 后一帧内的崩溃或静默失效窗口。 |

---

## 分析 (2026-04-08 10:50)

### 发现 47：现有 `HotReload` 自动化没有覆盖“full reload 后、GC 前再调用旧实例”的窗口，发现 46 对应回归面完全裸奔

| 项目 | 内容 |
|------|------|
| 维度 | D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPropertyTests.cpp` / `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp` / `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadAnalysisTests.cpp` / `Plugins/Angelscript/Source/AngelscriptTest/HotReload/` |
| 行号 | `AngelscriptHotReloadPropertyTests.cpp:207-301` / `AngelscriptHotReloadScenarioTests.cpp:287-335` / `AngelscriptHotReloadAnalysisTests.cpp:276-315` / `HotReload` 目录定向 grep |
| 描述 | 当前 full reload 运行时测试 `FAngelscriptFullReloadBasicTest` 只验证“类被替换后，新类的 property/function 还存在，并且能创建一个新对象”，整个用例没有保留任何 reload 前实例或旧 `UClass` 引用。软重载场景测试 `FAngelscriptScenarioHotReloadFunctionChangeTest` 虽然会在同一 actor 实例上做 reload 前后调用，但它固定走 `SoftReloadOnly`。而 `FAngelscriptAnalyzeReloadClassRemovedTest` 之类的分析测试只检查 `ReloadRequirement`，不触发真实 runtime 调用。我又对整个 `Plugins/Angelscript/Source/AngelscriptTest/HotReload` 做了定向 grep，`ForceGarbageCollection|CollectGarbage|TryCollectGarbage|OnPostReload|OnFullReload` 为 `NO_HITS`。这说明测试体系里根本没有人为拉开“reload 已完成但 GC 尚未跑完”的窗口。 |
| 根因 | hot reload 测试把关注点放在“编译路径选择是否正确”和“reload 完成后新类/新对象是否能工作”，没有把 full reload 期间旧实例引用的生存窗口当成受保护 contract。 |
| 影响 | 发现 46 这类“旧引用在 GC 前再次被调用即崩”问题，不会被当前任何一条 `HotReload` 自动化捕获。测试绿灯只能说明新类最终可用，不能说明 full reload 过程中的旧对象生命周期是安全的。 |

---

## 分析 (2026-04-08 10:53)

### 发现 48：类经历过 `replace -> remove` 后，旧版本节点仍会把 `GetMostUpToDateClass()` 导向已移除 head，GC 后可退化成悬挂版本链

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` / `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` / `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSubclassOf.h` |
| 行号 | `ASClass.cpp:917-923, 1186-1189, 1219-1222` / `AngelscriptClassGenerator.cpp:2573-2586, 3696-3699, 4990-5024` / `Bind_TSubclassOf.h:84-89` |
| 描述 | full reload 时，旧 `UASClass` 只会被重命名并保留 `NewerVersion = NewClass` 链接；后续如果脚本再次编辑把这个类彻底删除，`CleanupRemovedClass()` 只会把当前 head 的 `ScriptTypePtr` / `ConstructFunction` / `DefaultsFunction` 清空，并 `RemoveFromRoot()` 当前 head，本身完全不回写任何旧版本节点上的 `NewerVersion`。与此同时，`GetMostUpToDateClass()` 仍然无条件沿 `NewerVersion` 走到链尾返回。结果就是：只要某个类先经历过一次 replace，再在后续 reload 中被 remove，仍然留在内存里的旧版本节点会继续把“最新类”解析成这个已经被移除的 head。 |
| 根因 | 版本链只实现了“追加新 head”，没有实现“删除当前 head 时回收或截断旧链”。`CleanupRemovedClass()` 只清理被删除类自身，没有处理所有仍指向它的历史节点。 |
| 影响 | 这不是单纯的陈旧元数据。`ApplyOverrideComponents()`、`CreateDefaultComponents()` 和 `Bind_TSubclassOf` 都会调用 `GetMostUpToDateClass()` 参与组件类解析和类兼容性判断，因此 remove 后的失效 head 仍会被当成“最新脚本类”返回。当前 head 一旦被 GC，旧节点里的 `NewerVersion` 就会从“指向已失效类对象”进一步退化成悬挂指针，后续任何版本链查询都可能落到 use-after-free 风险区。 |

### 发现 49：现有自动化没有覆盖“先 full reload 替换、再删除同名类”的运行时链路，`GetMostUpToDateClass()` 的失效 head 问题不会报警

| 项目 | 内容 |
|------|------|
| 维度 | D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadAnalysisTests.cpp` / `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPropertyTests.cpp` / `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp` |
| 行号 | `AngelscriptHotReloadAnalysisTests.cpp:276-315` / `AngelscriptHotReloadPropertyTests.cpp:207-301` / `AngelscriptTestEngineHelper.cpp:501-517, 525-530` |
| 描述 | 当前“类删除”测试 `FAngelscriptAnalyzeReloadClassRemovedTest` 只验证分析结果是否要求 full reload，根本不执行真实 reload，更不会留下旧版本类指针继续查询。唯一的 full reload 运行时用例 `FAngelscriptFullReloadBasicTest` 也只覆盖单次 `V1 -> V2` 替换，没有后续 `V2 -> removed`。与此同时，测试 helper 的 `FindGeneratedClass()` / `FindGeneratedFunction()` 一旦命中 `UASClass` 就立即调用 `GetMostUpToDateClass()`，这会把历史节点自动折叠到链尾。结果是：测试体系里既没有构造“replace 之后再 remove”的两步序列，也没有保留历史节点直接观察版本链是否被正确截断。 |
| 根因 | 现有热重载测试矩阵把“类被删除的分析判定”和“类被替换后的运行时可用性”拆成了两类互不相交的用例；共享 helper 又默认替测试代码隐藏旧版本节点。 |
| 影响 | 发现 48 对应的回归面在当前自动化下是裸奔的。测试绿灯最多说明“删除会被分析成 full reload”以及“单次 full reload 后新类可用”，不能证明版本链在 remove 场景下不会把旧节点继续导向失效 head，更不能证明 `GetMostUpToDateClass()` 的调用方在这类序列上是安全的。 |

### 发现 50：被 `CleanupRemovedClass()` 标记为已删除的 `UASClass` 在 GC 前仍可按原名被发现并实例化，实例会落入“无脚本对象但保留旧构造元数据”的僵尸状态

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` / `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` / `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp` |
| 行号 | `AngelscriptClassGenerator.cpp:4990-5024` / `ASClass.cpp:917-918, 1352-1405, 1408-1458, 1461-1494` / `AngelscriptTestEngineHelper.cpp:501-517` |
| 描述 | 删除类时，`CleanupRemovedClass()` 只清空 `ScriptTypePtr`、`ConstructFunction`、`DefaultsFunction`，并打上 `CLASS_NotPlaceable` / `CLASS_HideDropDown` / `CLASS_Hidden`，但不会重命名该类、不会清掉 `ClassConstructor`、`CodeSuperClass`、`bIsScriptClass`，也不会清空 `DefaultComponents` / `OverrideComponents`。因此在 GC 真正发生前，这个 `UASClass` 仍然以原名字留在包里，`FindGeneratedClass()` 之类的查找路径依旧能 `FindObject<UClass>(Package, Name)` 命中它；命中后 `GetMostUpToDateClass()` 也会直接返回它自己。若后续代码继续拿这个 `UClass` 做 `NewObject` / actor spawn，三个 `Static*Constructor()` 仍会运行原有 native `ClassConstructor`，actor 路径甚至还会继续套用旧的默认组件描述，只是因为 `ScriptTypePtr == nullptr` 而静默跳过 `asCScriptObject` 构造、script ctor 和 defaults。 |
| 根因 | remove 流程把“从编辑器菜单隐藏”与“从运行时类型系统撤销注册”混为一谈，只做了最小脚本指针清理，没有把删除类转成不可发现、不可实例化的终态。 |
| 影响 | 类从脚本源码删除后，并不会立刻失去可实例化性；相反，它在 GC 前仍可能通过旧 `UClass*` 引用或按名查找被重新创建。新实例会呈现出“native 外壳和旧组件树仍存在，但 script 部分完全缺席”的僵尸行为，极易把删除类问题伪装成随机初始化异常或组件树污染。 |

---

## 分析 (2026-04-08 11:20)

### 发现 51：`NewerVersion` 版本链完全游离于 GC 图之外，历史类不会持有当前 head

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h` / `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` / `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 行号 | `ASClass.h:16-27, 76-77` / `ASClass.cpp:912-923` / `AngelscriptClassGenerator.cpp:4990-5023` |
| 描述 | `UASClass` 把版本链存成原生字段 `UASClass* NewerVersion`，既没有 `UPROPERTY`，也没有在 `RuntimeAddReferencedObjects()` 里把它交给 `FReferenceCollector`。与此同时，`GetMostUpToDateClass()` 会无条件沿 `NewerVersion` 链一路解引用到尾节点。`CleanupRemovedClass()` 又会在删除类时对当前 head 执行 `RemoveFromRoot()` 和 `ClearFlags(RF_Standalone)`。这意味着历史版本节点即使仍然活着，也不会因为持有 `NewerVersion` 而把当前 head 留在 GC 图里；一旦 head 被回收，旧节点里剩下的就是一个裸悬挂指针。 |
| 根因 | 版本链被实现成 UObject 外部的原生指针关系，但类生成器没有为它补上任何 GC 可见的引用边，导致“版本可追溯”与“对象可存活”被错误地假定成同一件事。 |
| 影响 | 版本链的内存安全依赖于“head 永远不会被 GC”这个隐含前提，而删除类路径已经明确打破了这个前提。后续任何 `GetMostUpToDateClass()` 查询都可能在旧节点上追进已回收 UObject；由于该函数被组件类解析和类型兼容性判断直接调用，这会把版本链问题升级成真实的 use-after-free 风险，而不是单纯的元数据陈旧。 |

### 发现 52：删除 script interface 时不会清掉 engine-level `asITypeInfo` 的 `UserData`，后续类型解析可拿到已删除 `UClass`

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` / `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp` / `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` |
| 行号 | `AngelscriptClassGenerator.cpp:2592-2613, 4990-5023` / `Bind_BlueprintType.cpp:133-137, 165-175, 1583-1595` / `AngelscriptDebugServer.cpp:1885-1893` |
| 描述 | script interface 在类生成阶段不会从模块里拿 `ScriptType`，而是通过 `RegisterObjectType(... asOBJ_REF | asOBJ_NOCOUNT | asOBJ_IMPLICIT_HANDLE)` 注册成 engine-level type，并立刻把 `InterfaceScriptType->SetUserData(NewClass)` 指到对应 `UASClass`。但删除路径 `CleanupRemovedClass()` 只清 `Class->ScriptTypePtr` / `ConstructFunction` / `DefaultsFunction`，完全没有对 `ClassDesc->ScriptType` 或 engine-level `asITypeInfo` 调 `SetUserData(nullptr)`，更没有反注册。后续消费者却继续把 `Usage.ScriptClass->GetUserData()` / `ScriptType->GetUserData()` 当成真实 `UClass*` 使用：`Bind_BlueprintType` 会据此创建 `FObjectProperty` / `FClassProperty`，调试服务器也会据此读取 metadata。 |
| 根因 | interface 类型注册走的是 AngelScript engine 全局表，而 remove 流程只回收了 UE 侧 `UASClass`，没有同步清理对应 `asITypeInfo` 上缓存的 `UClass*`。类型注册生命周期与 `UClass` 生命周期脱节。 |
| 影响 | 一旦某个 script interface 被删除，AngelScript engine 里仍会残留一个可解析的同名类型，并继续把其 `UserData` 暴露给属性生成、类型匹配和调试路径。该 `UClass*` 在 remove 后要么指向一个已隐藏/已失效的类对象，要么在 GC 后直接退化成悬挂指针。结果不是单纯“旧类型还在列表里”，而是后续编译和工具链可能继续接受已删除接口，并把错误的 `UClass` 写回新的反射数据。 |

### 发现 53：`ResolveInterfaceClass()` 对 native `UInterface` 的 fallback 只按短名扫全局类表，接口绑定结果受加载顺序影响

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 行号 | `5062-5108, 5142-5184` |
| 描述 | `FinalizeClass()` 解析 `ImplementedInterfaces` 时，脚本接口和已知 bind type 都找不到之后，会进入 fallback：先把名字里的前导 `U` 去掉，然后 `for (TObjectIterator<UClass> It; It; ++It)` 在线性扫描所有已加载 `UClass` 时，只要 `GetName() == UnrealInterfaceName && HasAnyClassFlags(CLASS_Interface)` 就立即返回。这里既不校验 package/path，也不校验 owning module，更不区分是否存在多个同短名 native interface。随后返回值会直接写入 `NewClass->Interfaces`，并驱动“是否缺失接口方法”的校验。 |
| 根因 | interface 解析链在最后一步退化成了“按短名从全局对象表抢第一个命中项”，把本应稳定的类型身份问题交给了 UObject 加载顺序。 |
| 影响 | 只要工程里存在两个不同模块/插件提供的同短名 `UInterface`，脚本类的 `implements` 解析就会变成非确定性行为：某次编译可能挂到 A 接口，下次编译则挂到 B 接口。结果既可能把错误的接口塞进 `UClass::Interfaces`，也可能按不相关接口的方法集合报“缺失方法”，直接破坏类型注册顺序和动态 `UClass` 生成一致性。 |

### 发现 54：现有自动化没有覆盖“interface 类型注册残留”和“同短名 interface 冲突”这两条关键回归面

| 项目 | 内容 |
|------|------|
| 维度 | D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceAdvancedTests.cpp` / `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadAnalysisTests.cpp` / `Plugins/Angelscript/Source/AngelscriptTest/Interface` / `Plugins/Angelscript/Source/AngelscriptTest/HotReload` / `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator` |
| 行号 | `AngelscriptInterfaceAdvancedTests.cpp:327-409` / `AngelscriptHotReloadAnalysisTests.cpp:276-315` / 目录定向 grep 统计 |
| 描述 | interface hot reload 的主场景测试 `FAngelscriptScenarioInterfaceHotReloadTest` 只覆盖“同一个 interface 在 V1/V2 都存在，full reload 后类仍然实现它”。它的脚本 V1 和 V2 都保留了 `UIDamageableHR`，因此不会触发 interface 删除或 engine-level type 清理。另一方面，`FAngelscriptAnalyzeReloadClassRemovedTest` 只分析普通 class removal，不涉及 interface removal。我又对 `Plugins/Angelscript/Source/AngelscriptTest/Interface`、`HotReload`、`ClassGenerator` 做了定向 grep：`RegisterObjectType`、`SetDefaultNamespace`、`GetUserData(`、`duplicate.*interface`、`same short name` 全部是 `NO_HITS`。这说明测试体系完全没有覆盖发现 52 的“删除后注册残留”，也没有覆盖发现 53 的“同短名 native interface 解析碰撞”。 |
| 根因 | 当前 interface 自动化把重点放在“声明是否生成成功”和“继承/dispatch 是否工作”，没有把类型注册表清理和冲突解析当成热重载 contract 的一部分来验证。 |
| 影响 | 52/53 这类不会在普通 interface 行为测试里立即暴露的问题，当前自动化不会报警。测试绿灯最多说明 interface 在理想路径下可声明、可继承、可 dispatch，不能证明删除后的 engine-level 注册状态是干净的，也不能证明 interface 解析在复杂模块环境里具备确定性。 |

---

## 分析 (2026-04-08 11:31)

### 发现 55：脚本 `Namespace` 只在 AngelScript 侧生效，`UClass` 生成和 reload 索引仍按短名折叠，导致不同命名空间的类互相覆盖

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 行号 | `145-156, 1752-1766, 2570-2585, 5912-5934` |
| 描述 | 类描述在解析脚本类型时明确保留了 `Namespace`：`GetNamespacedTypeInfoForClass()` 会用 `Engine->FindNameSpace(...)` + `Module->GetType(ClassName, NameSpace)` 取到命名空间内的 `asITypeInfo`。但到了 UE 侧，生成器的三个关键索引都把 namespace 丢掉了：`DataRefByName.Add(ClassName, ...)` 只用短名建内部查找表；`GetClassDesc(ClassName)` 也只按短名回查；真正创建/查找 `UASClass` 时又把 `ClassName` 送进 `GetUnrealName()`，随后直接 `FindObject<UASClass>(Package, *UnrealName)` 和 `NewObject<UASClass>(..., FName(*UnrealName), ...)`。结果是两个位于不同 namespace 的脚本类虽然在 AngelScript 模块里是两个不同 type，但在 `/Script/Angelscript` 包里会被压成同一个 `UClass` 名字和同一个 reload 入口。 |
| 根因 | 类型系统的“身份”在 AngelScript 层使用了 `Namespace + ClassName`，而类生成器的 UE 注册层和内部 descriptor 索引仍然沿用“短名唯一”的旧假设，没有把 namespace 纳入 UObject 命名和 `DataRefByName` 键。 |
| 影响 | 只要项目开始使用 namespaced script class，同短名类就会在 full reload 时互相 `FindObject`/`Rename`/替换，`GetClassDesc()` 和依赖传播也会把两个不同类型当成同一个节点处理。最终表现不是单一编译错误，而是 `StaticClass()` 指到错误 `UClass`、版本链串到不相关类、热重载把 A namespace 的类实例错绑到 B namespace 的新版本，直接破坏动态 `UClass` 生成一致性。 |

---

## 分析 (2026-04-08 11:32)

### 发现 56：script interface 注册完全绕过 namespace，同短名接口会共享一份 engine-level `asITypeInfo`

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 行号 | `1752-1766, 2592-2613, 5063-5086` |
| 描述 | interface 路径没有走 `GetNamespacedTypeInfoForClass()`，而是在 `ClassDesc->bIsInterface && ScriptType == nullptr` 时直接向 AngelScript engine 全局表执行 `RegisterObjectType(InterfaceName, ...)`，随后再用 `GetTypeInfoByName(InterfaceName)` 取回 type，并把 `UserData` 绑定到当前 `NewClass`。如果同短名接口已经注册过，`asALREADY_REGISTERED` 分支仍会取回那一份全局 type，并直接覆写其 `UserData`。后续 `FinalizeClass()` 解析 `implements` 时又调用 `GetClassDesc(InterfaceName)`，并在线性扫描里仅比较 `CheckClass.NewClass->ClassName == InterfaceName`；内部 `DataRefByName` 也只按短名建表。结果是两个不同 namespace 的 script interface 会共享同一个 engine-level `asITypeInfo` 和同一个类描述入口。 |
| 根因 | 由于 AngelScript 源码层不支持 `interface` 关键字，插件把 script interface 降格成 engine-level reference type 来补语义，但实现时只保留了短名，没有补上任何 namespace-qualified 身份映射。 |
| 影响 | 一旦项目里出现 `Foo::IDamageable` 和 `Bar::IDamageable` 这类同短名接口，后编译的那个接口会重写共享 `asITypeInfo->UserData`，让前一个接口的 `Cast<>`、变量声明、`implements` 校验和热重载目标类全部指向错误 `UClass`。这会把接口绑定结果变成编译顺序相关的非确定性行为，并且比普通类更隐蔽，因为冲突发生在 engine-level type 注册表里。 |

---

## 分析 (2026-04-08 11:33)

### 发现 57：现有自动化几乎没有覆盖 namespaced script type 的类生成与热重载，发现 55/56 对应回归面处于空白

| 项目 | 内容 |
|------|------|
| 维度 | D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp` / `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator` / `Plugins/Angelscript/Source/AngelscriptTest/HotReload` / `Plugins/Angelscript/Source/AngelscriptTest/Interface` / `Plugins/Angelscript/Source/AngelscriptTest/Shared` |
| 行号 | `AngelscriptClassBindingsTests.cpp:433-446` / 目录定向 grep 统计 |
| 描述 | 我对 `ClassGenerator`、`HotReload`、`Interface`、`Shared` 测试目录做了定向 grep：`SetDefaultNamespace`、`"namespace `、`duplicate.*class`、`duplicate.*interface`、`same short name`、`GetClassDesc(` 全部是 `NO_HITS`。当前唯一显式验证 namespace 行为的自动化，是 `AngelscriptClassBindingsTests.cpp` 里把 AngelScript engine 的默认 namespace 切到原生 `AActor`，然后检查原生 `StaticClass()` 全局函数仍绑定到正确 `UClass`。这条用例覆盖的是 native binding，不涉及动态生成的 script class / script interface，更不涉及同短名冲突、UClass 命名折叠或 hot reload 版本链。 |
| 根因 | 测试矩阵只把 namespace 当成脚本语言层面的符号可见性特性来验证，没有把“namespace 进入 ClassGenerator 后是否仍保持类型身份唯一”视为一条需要保护的 runtime contract。 |
| 影响 | 发现 55/56 这类只有在“namespaced script type + 动态 UClass/interface 注册 + reload”三者交叉时才暴露的问题，当前测试体系不会报警。测试绿灯最多说明 native class namespace 可见性正常，不能证明 script class/interface 在 namespace 场景下的类型注册顺序、UClass 生成和热重载路由是正确的。 |

---

## 分析 (2026-04-08 11:35)

### 发现 58：`bModuleSwapInError` 不会阻止当前轮次的类注册和 CDO 初始化，报错类仍会作为 live `UClass` 进入运行时

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` / `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | `AngelscriptClassGenerator.cpp:5152-5183, 5195-5210, 5254-5437, 5520-5676, 5775-5854` / `AngelscriptEngine.cpp:4047-4053` |
| 描述 | `FinalizeClass()`、`FinalizeActorClass()` 等验证路径在发现 interface 缺失方法、`DefaultComponent`/`OverrideComponent` 元数据非法、root component 冲突等问题时，统一只是 `ModuleData.NewModule->bModuleSwapInError = true`。但这些函数并不会提前返回，`FinalizeClass()` 末尾仍然继续 `NotifyRegistrationEvent(...)` 把 `NewClass` 注册进运行时；整个 reload 流程随后也无条件执行 `CallPostInitFunctions()` 和 `InitDefaultObjects()`，其中 `InitDefaultObject()` 会直接 `NewClass->GetDefaultObject(true)` 创建 CDO。对 `bModuleSwapInError` 的唯一后处理只出现在 `AngelscriptEngine.cpp`，它只是把源码文件记进 `PreviouslyFailedReloadFiles`，要求后续继续重编，并没有把本轮已注册的新类回滚掉。 |
| 根因 | ClassGenerator 把“类生成阶段的语义错误”实现成了一个延迟重编标记，而不是当前 reload 事务的 abort 条件；结果错误状态只影响下一轮编译决策，不影响本轮 `UClass` 生命周期。 |
| 影响 | 一旦 class finalization 在本轮触发 synthetic error，工程不会停在“旧类继续生效”的安全状态，而是会得到一个已经报错、但仍被注册并拥有 CDO 的半成品 `UClass`。这会把接口校验失败、组件元数据冲突等本应阻止 swap-in 的问题转化成运行时污染：反射查找能看到错误类，post-init 代码和 CDO 构造会在错误状态下继续执行，后续症状可能是错误类短暂可实例化、默认组件树被部分 materialize，或下一轮 reload 之前就先触发崩溃。 |

---

## 分析 (2026-04-08 11:54)

### 发现 59：`IsFunctionImplementedInScript()` 只检查 `UASFunction` 外壳，模块移除后仍会把失效函数报告成“已实现”

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` / `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | `ASClass.cpp:979-984, 1535-1555` / `AngelscriptEngine.cpp:1052-1066` |
| 描述 | `UASClass::IsFunctionImplementedInScript()` 的判定只有两步：`FindFunctionByName()` 找到 `UFunction`，再把它 `Cast<UASFunction>` 后检查 `GetOuterUClass()`。它完全不看 `UASFunction::ScriptFunction` 是否仍然绑定到有效脚本函数。相对地，`FAngelscriptEngine::DiscardModule()` 在模块移除时会显式把每个 `UASFunction` 的 `ScriptFunction` 和 `ValidateFunction` 置空。结果就是：同一个类在模块 discard、类删除或 full reload 清理之后，`IsFunctionImplementedInScript()` 仍然会返回 `true`，但同文件里的 `UASFunction::GetSourceFilePath()` / `GetSourceLineNumber()` 已经会因为 `ScriptFunction == nullptr` 返回空字符串或 `-1`。 |
| 根因 | 这个 API 把“类上还挂着一个 `UASFunction` UObject”误当成“脚本实现仍然有效”，没有把热重载/模块卸载过程中真正失效的状态位 `ScriptFunction == nullptr` 纳入判定。 |
| 影响 | 热重载或 `DiscardModule()` 之后，脚本和工具层会得到错误的 capability 判断：`IsFunctionImplementedInScript()` 说函数还在，但后续源码定位信息已经失效，运行时调用路径也会因为 `ScriptFunction == nullptr` 提前返回。任何拿这个 API 做功能探测、fallback 分支或调试展示的逻辑，都会在“函数壳子还在、实现已经失效”的 stale 状态下作出错误决策。 |

---

## 分析 (2026-04-08 11:55)

### 发现 60：script-only GC 引用 schema 在插件子树里只有写入没有消费，正确性依赖仓库外隐式契约

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h` / `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 行号 | `ASClass.h:34, 77` / `AngelscriptClassGenerator.cpp:4859-4924` |
| 描述 | `DetectAngelscriptReferences()` 明确在扫描“不是 `UProperty` 但 `HasReferences()` 为真”的脚本字段，并把它们通过 `EmitReferenceInfo()` 填进 `Class->ReferenceSchema`。但 `UASClass` 自己暴露给运行时的唯一相关钩子 `RuntimeAddReferencedObjects()` 是空实现；我又对 `Plugins/Angelscript/Source/AngelscriptRuntime` 做了定向 `rg`，`ReferenceSchema` 只命中 4 处：字段声明 1 处、builder 读写 3 处，没有任何运行时读取或 collector 调用点。也就是说，在当前仓库可见源码里，这套 schema 是“生成后存回类对象”，但没有形成插件子树内部可验证的消费闭环。 |
| 根因 | ClassGenerator 把 script-only 引用追踪实现成了 `ReferenceSchema` 生成逻辑，却没有在同一子树里提供与之对称的收集侧代码；GC 正确性因此依赖于仓库外引擎补丁或未纳入当前分析范围的隐式行为。 |
| 影响 | 这不是单纯的“实现分散”。只要外部引擎侧契约缺失、变更或与当前字段布局失配，插件生成的 script-only 引用 schema 就会退化成 write-only 数据，GC 无法从当前子树中得到任何保护。结果将是最难排查的一类问题：脚本对象里的非 `UProperty` 引用在某些构建或引擎分支上 silently 不被追踪，触发引用丢失、错误回收或悬挂对象保活，而 ClassGenerator 本身不会给出任何显式报警。 |

---

## 分析 (2026-04-08 11:56)

### 发现 61：现有自动化没有覆盖 `IsFunctionImplementedInScript()` 的失效态，也没有验证 script-only GC schema 是否真正生效

| 项目 | 内容 |
|------|------|
| 维度 | D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp` / `Plugins/Angelscript/Source/AngelscriptTest/GC/AngelscriptGCScenarioTests.cpp` / `Plugins/Angelscript/Source/AngelscriptTest` |
| 行号 | `AngelscriptFileAndDelegateBindingsTests.cpp:229-243` / `AngelscriptGCScenarioTests.cpp:73-205` / 目录定向 grep 统计 |
| 描述 | 当前测试树里，`IsFunctionImplementedInScript` 只出现 1 次，而且只验证正路径：`ComputeValue` 存在时返回 `true`。没有任何用例在 `DiscardModule()`、class removal 或 hot reload 之后重新断言它会变成 `false`。与此同时，GC 场景测试 `FAngelscriptScenarioGCActorDestroyTest`、`FAngelscriptScenarioGCComponentDestroyTest`、`FAngelscriptScenarioGCWorldTeardownTest` 只验证“空 actor/component 被销毁后弱引用失效”，脚本样例里没有任何带非 `UProperty` script 引用字段的类。我又对整个 `Plugins/Angelscript/Source/AngelscriptTest` 做了定向 `rg`：`ReferenceSchema`、`RuntimeAddReferencedObjects`、`EmitReferenceInfo`、`HasReferences(` 全部 0 命中。 |
| 根因 | 测试矩阵把“绑定 API 的存在性”和“GC 能否把对象回收”当成了足够条件，没有把“模块移除后的失效态”和“script-only 引用字段是否真的被追踪”列为独立 contract。 |
| 影响 | 发现 59/60 对应的回归面当前都是裸奔的。测试绿灯只能说明正路径的 metadata 查询可用，以及不含复杂脚本引用的对象最终能被 GC；它不能证明热重载/模块卸载后的 `IsFunctionImplementedInScript()` 不会撒谎，也不能证明 `DetectAngelscriptReferences()` 生成出来的 schema 在运行时真的参与了引用收集。 |

---

## 分析 (2026-04-08 12:14)

### 发现 62：`ComposeOntoClass` 只被记录不被消费，动态 `UClass` 生成对该特性实际上是 no-op

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` / `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h` / `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` / `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp` |
| 行号 | `AngelscriptEngine.h:1141-1145` / `ASClass.h:27` / `AngelscriptClassGenerator.cpp:1334-1364, 5052-5056` / `PrecompiledData.cpp:840, 2846-2847` |
| 描述 | `FAngelscriptClassDesc` 明确保留了 `ComposeOntoClass`，`Analyze()` 还会因为它存在而检查 property 上是否带 `ComposedStruct` meta；真正进入类生成后，`FinalizeClass()` 只是尝试 `GetClassDesc(ClassDesc->ComposeOntoClass)`，命中时把结果写进 `UASClass::ComposeOntoClass`，没命中则静默跳过。随后对 `Plugins/Angelscript/Source/AngelscriptRuntime` 做定向 `rg` 可以看到，除了 `Core/AngelscriptEngine.h` 的描述字段、`ASClass.h` 的成员声明、`AngelscriptClassGenerator.cpp` 的赋值，以及 `StaticJIT/PrecompiledData.cpp` 的序列化/反序列化外，仓库内没有任何读取 `ComposeOntoClass` 的运行时实现。也就是说，这个字段会被解析、保存、恢复，但不会参与后续 `UClass` 生成或实例化行为。 |
| 根因 | `ComposeOntoClass` 的数据通路只做到了“预处理/序列化/赋值”，没有建立与之对应的 class-finalization、CDO 构造或运行时消费闭环；连“目标类不存在”都没有被当成编译错误。 |
| 影响 | 脚本侧即使声明了 `ComposeOntoClass`，生成出来的 `UClass` 仍会按普通类工作，开发者看到的是“语法被接受、元数据也被保存”，但运行时没有任何 compose 效果；如果目标类名写错，系统也不会明确报错，只会默默留下一个 `nullptr`。这类 silent no-op 会直接破坏动态 `UClass` 生成一致性。 |

### 发现 63：组件树合法性校验大量被 `WITH_EDITOR` 包住，非编辑器构建会静默生成与编辑器不同的层级

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` / `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` |
| 行号 | `AngelscriptClassGenerator.cpp:5290-5319, 5354-5440, 5467-5676` / `ASClass.cpp:1257-1328` |
| 描述 | actor 组件树里几条最关键的合法性检查都只存在于编辑器构建。`FinalizeActorClass()` 中“重复 `RootComponent`”检查被包在 `#if WITH_EDITOR` 内；`OverrideComponent` 是否真的能在父类里找到目标、类型是否兼容，也同样只在 `#if WITH_EDITOR` 分支里验证。更关键的是，`VerifyClass()` 整个函数本身就在 `#if WITH_EDITOR` 里，里面负责检查 attach parent 是否存在、是否是 `USceneComponent`、`EditorOnly` parent/child 组合是否合法，以及 abstract 父组件是否被 override。相对地，真正的运行时构造路径 `CreateDefaultComponents()` 并不会在这些条件失效时中止，而是直接走 fallback：attach 目标不存在就挂到当前 root，连 root 都没有就把 child 提升成新的 root；如果有多个被标成 root 的组件，后创建的那个会通过 `SetRootComponent()` 抢走 root，并把之前的 root 重新挂到自己下面。 |
| 根因 | 组件树约束被实现成 editor-only authoring 校验，而实际构造逻辑为了“尽量生成对象”内置了宽松 fallback；一旦脱离 `WITH_EDITOR`，非法声明不再被拒绝，只剩下 runtime 的容错路径。 |
| 影响 | 同一份脚本类在 editor 构建里可能报编译错误，在非编辑器构建里却会成功生成一个不同的组件树。结果不是单纯少一条 warning，而是 root/attach 关系会被悄悄改写，直接破坏 `UClass` 生成一致性，并让 cooked/server 行为与开发期验证结果分叉。 |

### 发现 64：移除或重命名 `DefaultComponent` / `OverrideComponent` 背后的 property 时，`DoSoftReload()` 会直接命中断言

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 行号 | `1097-1135, 2081-2093, 4113-4197` |
| 描述 | 分析阶段已经把“property 类型/定义变化”以及“property 被删除”标成 `FullReloadRequired`。但 soft reload 执行判定 `ShouldFullReload(FClassData&)` 在非 full-reload 模式下完全不看 `ReloadReq`，只按“是否 interface / 是否实现 interface / 是否 brand-new class”决定是否重建类对象。于是 actor 类即使因为删改 property 已被标成 `FullReloadRequired`，在 `SoftReloadOnly` 下仍可能进入 `DoSoftReload()`。而这条路径会无条件遍历旧 `Class->DefaultComponents` / `OverrideComponents`，再对每一项执行 `FindPropertyByName(...)` 并 `check(Property != nullptr)` 后重算 `VariableOffset`。只要被删掉或改名的正好是某个 `DefaultComponent`/`OverrideComponent` 的承载 property，这里就会立即断言。 |
| 根因 | 组件描述数组是按“旧 property 名仍然存在”写的，而 soft reload 执行层又没有兑现分析层已经给出的 `FullReloadRequired` 约束，导致 stale component descriptor 直接与新反射布局碰撞。 |
| 影响 | 在 PIE / game-world 的 `SoftReloadOnly` 开发路径里，只要开发者改掉承载组件的 property 名字，或把它从脚本类里删掉，热重载就不是“保留旧状态”那么简单，而是会在 offset 同步阶段直接崩溃。这是一个可稳定触发的 hard failure。 |

### 发现 65：现有自动化没有覆盖 `ComposeOntoClass`，也没有覆盖无效组件树声明在生成阶段的失败契约

| 项目 | 内容 |
|------|------|
| 维度 | D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Component/AngelscriptComponentScenarioTests.cpp` / `Plugins/Angelscript/Source/AngelscriptTest` |
| 行号 | `AngelscriptComponentScenarioTests.cpp:355-490` / 目录定向 grep 统计 |
| 描述 | 对 `Plugins/Angelscript/Source/AngelscriptTest` 做定向 `rg`，`ComposeOntoClass`、`ComposedStruct`、`Attach parent does not exist`、`OverrideComponent for`、`cannot be the RootComponent` 全部 0 命中。当前与组件树最相关的自动化，只有 `FAngelscriptScenarioDefaultComponentBasicTest` 和 `FAngelscriptScenarioDefaultComponentMultipleTest` 两个 happy path 用例，分别验证“单 root default component 可创建”和“`Attach = RootScene` 的基础层级可创建”。测试树里看不到任何一条用例去验证 `ComposeOntoClass` 是否真的生效，也看不到任何 invalid attach / duplicate root / missing override target 在类生成阶段会被稳定拒绝。 |
| 根因 | 组件测试矩阵目前只覆盖“正常声明可以 materialize”，没有把 composition 特性和组件树非法声明的失败语义纳入 contract。 |
| 影响 | 发现 62/63 这类问题当前几乎不会被 CI 主动发现。测试绿灯最多说明正路径的 `DefaultComponent`/`Attach` 能工作，不能证明 `ComposeOntoClass` 不是 no-op，也不能证明无效组件树在不同构建配置下会被一致地拦截。 |

---

## 分析 (2026-04-08 12:29)

### 发现 66：script ctor/defaults 执行失败不会中止对象构造或 soft reload 重建，live `UObject` 会带着半初始化状态继续存活

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` / `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` / `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` / `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_context.h` / `Plugins/Angelscript/Source/AngelscriptRuntime/Testing/AngelscriptTest.cpp` |
| 行号 | `ASClass.cpp:1086-1134, 1352-1494` / `AngelscriptClassGenerator.cpp:4605-4638, 4737-4777, 4825-4849` / `AngelscriptEngine.cpp:89-109` / `as_context.h:73-79` / `AngelscriptTest.cpp:357-373` |
| 描述 | `PrepareAngelscriptContextWithLog()` 只处理 `Prepare()` 失败，后续 `ExecuteConstructFunction()`、`ExecuteDefaultsFunctions()` 和 `ReinitializeScriptObject()` 都直接调用 `Context->Execute()` 且完全忽略返回值。与此同时，`asIScriptContext::Execute()` 明确有返回状态，测试框架也把 `asEXECUTION_EXCEPTION` / `asEXECUTION_ERROR` 当成失败处理。问题在于类生成路径没有任何对应的失败分支：三个 `Static*Constructor()` 会在 placement-new `asCScriptObject`、debug value 初始化、actor 默认组件创建之后继续往下走；soft reload 则先 `DestructScriptObject()` 清空旧脚本对象，再调用 `ReinitializeScriptObject()`，随后无论重建是否失败都继续把旧属性拷回 live instance / CDO。 |
| 根因 | ClassGenerator 把 script ctor/defaults 执行包装成了 `void` helper，没有把 `Execute()` 结果上传到 UObject 构造流程或 hot reload 事务控制层；失败只会被日志侧观察到，不会触发回滚、对象销毁或 `bModuleSwapInError`。 |
| 影响 | 只要 ctor/defaults 在对象创建、CDO 初始化或 soft reload 重建过程中抛异常/执行错误，系统就会留下“native 外壳已创建、script 状态未完成”的对象：actor 路径里默认组件和 tick/debug 状态已经 materialize，soft reload 路径里旧脚本对象已经被析构和清零，但新脚本对象可能根本没构造完成。后续 GC/析构再调用 `CallDestructor()` 时，面对的是一个从未完成初始化的 script object，直接破坏构造/析构对称性，并把失败从编译期问题放大成运行时状态污染。 |

### 发现 67：`DetectAngelscriptReferences()` 在 soft reload 时会把旧 `ReferenceSchema` 整体叠加回新 schema，引用项会随 reload 次数持续累积

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 行号 | `4283-4284, 4866-4924` |
| 描述 | soft reload 复用旧 `UASClass` 后会再次调用 `DetectAngelscriptReferences()`。但这个函数开头不是“从空 schema 重建”，而是先 `Schema.Append(Class->ReferenceSchema.Get())` 把当前类已有的整份 schema 追加进 builder，再对当前 `ScriptType` 的 script-only 引用字段重新 `EmitReferenceInfo()`。只要类仍然有引用字段，`Schema.NumMembers()` 就会从“旧成员数 N”增长为“旧 N + 当前 N”，触发 `ReferenceSchema.Set(...)` 把两份成员一起写回类对象。下一次 soft reload 又会在 `2N` 的基础上继续追加，形成 `3N`、`4N` 的线性累积。 |
| 根因 | `ReferenceSchema` 重建逻辑把“保留旧 schema 尾部结构”误实现成了“先拷贝整份旧 schema，再发射本轮新成员”，却没有区分 inherited members 与当前类旧成员，也没有任何清理当前类旧条目的步骤。 |
| 影响 | 这不只是性能噪音。即便字段集合完全没变，GC schema 也会在每次 soft reload 后重复扫描同一批 offset；一旦引用字段发生位移或类型调整，所有历史 offset 仍会残留在 schema 中，GC 将继续访问已经过时的位置。结果是 script-only 引用追踪会随着 reload 次数越来越偏离真实对象布局，轻则重复保活/重复遍历，重则把旧 offset 上的无关内存当成引用源处理。 |

### 发现 68：现有 `HotReload` / `GC` / `ClassGenerator` 自动化没有覆盖“构造/defaults 异常”与“script-only GC schema 重建”这两类回归面

| 项目 | 内容 |
|------|------|
| 维度 | D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload` / `Plugins/Angelscript/Source/AngelscriptTest/GC` / `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator` |
| 行号 | 目录定向 grep 统计 |
| 描述 | 我对以上三个测试目录做了定向 `rg`。结果是：`SoftReloadOnly` 有 10 个命中、`CompileModuleWithResult` 有 15 个命中，说明这里确实存在不少 soft reload 自动化；但与本轮问题直接相关的 `ReferenceSchema`、`EmitReferenceInfo`、`HasReferences(`、`__InitDefaults`、`asEXECUTION_EXCEPTION`、`Throw(` 全部 `NO_HITS`。这表示当前测试虽然覆盖了不少“编译成功后的正常 soft reload 流程”，却没有任何一条用例去验证“对象构造/默认值阶段抛异常时热重载如何收敛”，也没有任何一条用例去验证 script-only 引用 schema 在 repeated soft reload 后是否仍保持正确。 |
| 根因 | 测试矩阵偏向 happy path 的模块替换、属性保留和函数体更新，没有把“构造期间脚本执行失败”以及“非 `UProperty` 引用字段的 GC schema 演化”当成 ClassGenerator contract 来守护。 |
| 影响 | 发现 66/67 当前都可以在 CI 绿灯下长期潜伏。测试通过最多能证明常规 soft reload 能跑通，不能证明 ctor/defaults 异常不会留下半初始化对象，也不能证明 repeated soft reload 后的 script-only GC schema 没有重复项和 stale offset。 |

### 发现 69：`CDONoDefaults` 只按 `RF_ArchetypeObject` 构造，不是实际 `RF_ClassDefaultObject`，soft reload 比较基线先天偏离真实 CDO 语义

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` / `UEAS2/Engine/Source/Runtime/CoreUObject/Private/UObject/UObjectGlobals.cpp` / `UEAS2/Engine/Source/Runtime/CoreUObject/Private/UObject/Obj.cpp` |
| 行号 | `AngelscriptClassGenerator.cpp:4093-4110` / `UObjectGlobals.cpp:3604-3634, 4657-4670` / `Obj.cpp:1683-1686, 1970-1988, 2255-2258` |
| 描述 | `PrepareSoftReload()` 想构造一个“未执行 defaults 的 CDO 基线”，但实际创建的是 `NewObject(..., RF_ArchetypeObject)`，没有设置 `RF_ClassDefaultObject`。引擎底层明确把这两种对象区分开处理：`StaticAllocateObject` 里 `bCreatingCDO` 与 `bCreatingArchetype` 是两条分支；对象序列化只有 `RF_ClassDefaultObject` 才会 `StartSerializingDefaults()`，并用 `SuperClass` 作为 defaults diff 基准；script class 还有一条专门的 `bIsScriptCDO` 快速路径，只在对象带 `RF_ClassDefaultObject` 时启用。相反，非 CDO 但类有 defaults 时，`Obj.cpp` 还会主动预加载真实 `Class->GetDefaultObject()`。这意味着 `CDONoDefaults` 从引擎视角根本不是“同一类的另一份 CDO”，而是 archetype/non-CDO 对象。 |
| 根因 | hot reload 基线对象的设计目标是“保留 CDO 语义，只关闭 script defaults”，但实现上只复制了 `RF_ArchetypeObject` 标志，没有复制 CDO 专属标志；结果引擎初始化、defaults 序列化和 script CDO 优化路径都不再对它生效。 |
| 影响 | 即使不考虑前面已经记录的全局 bool 污染，`BaseCDO` 与 `CDONoDefaults` 的比较也不是在两份等价 CDO 之间进行。引擎/native defaults、delta 序列化和 script-class 特判都可能让 `CDONoDefaults` 先天偏离真实 CDO，然后再被 `DoSoftReload()` 用来判定“哪些值是 defaults 改出来的”。结果就是属性保留逻辑会建立在错误基线上，把引擎层初始化差异误判成 script defaults，或反向漏掉真正应该保留的默认值变化。 |

---

## 分析 (2026-04-08 12:39)

### 发现 70：`WITH_AS_DEBUGVALUES` 打开时 `UASClass` 构造/析构路径调用了一个已被注释掉的 `FDebugValuePrototype` API，debug 构建无法自洽

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDebugValue.h` / `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` / `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` / `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` |
| 行号 | `AngelscriptDebugValue.h:5-6, 43-136` / `ASClass.cpp:967-976, 1380-1382, 1447-1449, 1477-1479` / `AngelscriptEngine.cpp:5338-5417, 5423-5425, 5497-5514` / `AngelscriptEngine.h:491-500` |
| 描述 | `WITH_AS_DEBUGVALUES` 被定义成 `!UE_BUILD_TEST && !UE_BUILD_SHIPPING && UE_BUILD_DEBUG`。一旦该宏为真，`UASClass::StaticActorConstructor()`、`StaticComponentConstructor()`、`StaticObjectConstructor()` 和 `RuntimeDestroyObject()` 都会分别调用 `Class->DebugValues.Instantiate(Object)` 与 `DebugValues.Free(Object->Debug)`；`AngelscriptEngine.cpp` 里的 debug stack 也会调用同一套 `Prototype->Instantiate(...)` / `Prototype->Free(...)`。但当前仓库里真正生效的 `FDebugValuePrototype` 定义只剩下 `Create()` 和 `Reset()` 两个 stub，原本包含 `Instantiate()`、`Free()` 以及 `FDebugValues` 结构体的完整实现整段都被注释掉了。`AngelscriptEngine.h` 仍把 `FAngelscriptDebugFrame::Variables` 声明成 `FDebugValues*`，说明调用方没有同步降级。 |
| 根因 | debug-value 子系统处于“调用侧保留、实现侧整段注释”的半移除状态：`ASClass` 和 `AngelscriptEngine` 仍按完整 per-object debug value 生命周期编写，但 `Core/AngelscriptDebugValue.h` 实际暴露的类型接口已经不再包含这些方法和结构。 |
| 影响 | 这不是单纯的 dead code。只要目标配置满足 `WITH_AS_DEBUGVALUES`，`ClassGenerator` 的构造/析构路径和脚本调试栈都会依赖一个不存在的 API，debug 构建将无法通过编译；即便通过临时宏绕开编译，`Object->Debug` 的初始化与释放也不再有对称实现。结果是最需要排查热重载/构造析构问题的 debug 配置，反而失去了可构建性和可调试性。 |

### 发现 71：`IsDeveloperOnly()` 与编译阶段的 editor-only 模块判定规则不一致，嵌套 `*.Editor.*` 模块会被错误当成非编辑器类

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` / `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` / `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 行号 | `ASClass.cpp:1523-1532` / `AngelscriptEngine.cpp:4354-4356` / `AngelscriptClassGenerator.cpp:5602-5619, 5643-5658` |
| 描述 | `UASClass::IsDeveloperOnly()` 只把 `Dev.` 和 `Editor.` 前缀模块视为 developer-only；而真正驱动脚本编译器的 `isEditorOnlyModule` 规则同时接受 `Module->ModuleName.Contains(TEXT(".Editor."))`。这意味着 `Game.Tools.Editor.Visualizers` 之类的嵌套 editor 模块，在编译阶段会被当成 editor-only 脚本处理，但落到 `UASClass` 运行时 metadata 上时又会返回 `false`。`FinalizeActorClass()` 随后把 `ASClass->IsDeveloperOnly()` 直接纳入 `DefaultComponent` attach/root 的 editor-only 合法性判断。 |
| 根因 | “模块是否只在编辑器存在”这一语义被重复实现了两份，但 `UASClass` 侧只保留了前缀匹配，漏掉了编译阶段已经承认的 `.Editor.` 命名模式。类 metadata 与脚本编译器使用了不同的分类准则。 |
| 影响 | 对嵌套 editor 模块里的 actor class，生成出来的 `UASClass` 会把自己误报成非 developer-only，进而在 `Editor-Only DefaultComponent` root/attach 校验时触发错误的编译失败。这会让同一份脚本在“模块编译期是 editor-only”与“类校验期不是 editor-only”之间出现自相矛盾的结果，直接破坏 `UClass` 生成一致性。 |

### 发现 72：现有自动化没有覆盖 `IsDeveloperOnly()` 的真实返回值语义，也没有覆盖 `WITH_AS_DEBUGVALUES` 下的对象级 debug-value 生命周期

| 项目 | 内容 |
|------|------|
| 维度 | D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` / `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp` / `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator` / `Plugins/Angelscript/Source/AngelscriptTest/HotReload` |
| 行号 | `AngelscriptBindConfigTests.cpp:514-551` / `AngelscriptEngineParityTests.cpp:476-482` / 目录定向 grep 统计 |
| 描述 | 对 `Plugins/Angelscript/Source/AngelscriptTest` 做定向 `rg`，`IsDeveloperOnly` 只命中 `AngelscriptBindConfigTests.cpp:514-551`，而且该测试只验证 `UASClass::IsDeveloperOnly` 是否被注册进 bind map，没有真正创建脚本类并断言返回值；同一次检索里，`WITH_AS_DEBUGVALUES`、`DebugValues.Instantiate`、`DebugValues.Free`、`FDebugValuePrototype`、`FDebugValues` 全部 `NO_HITS`，说明测试树没有任何对象级 debug-value 生命周期用例。另一个与 editor 模块命名最接近的覆盖点只是 `AngelscriptEngineParityTests.cpp:476-482` 中的前缀模块 `"Editor.SoftReferenceParity"`，并没有构造 `*.Editor.*` 这种会触发发现 71 的嵌套命名。对 `ClassGenerator` / `HotReload` 目录继续检索 `IsDeveloperOnly(`、`GetSourceFilePath(`、`GetSourceLineNumber(` 也是 `NO_HITS`。 |
| 根因 | 当前自动化更关注“绑定是否暴露”“单文件 editor 前缀模块能否编译”这类正路径，没有把 `UASClass` metadata API 的返回值 contract 和 debug-build 专属构造/析构路径纳入测试矩阵。 |
| 影响 | 发现 70/71 对应的回归面当前都不会被 CI 主动发现。测试绿灯最多能证明 `IsDeveloperOnly` 这个函数被导出了，以及 `Editor.` 前缀模块的某些 API 可用；它不能证明嵌套 `*.Editor.*` 模块不会被错误分类，也不能证明 debug 构建下 `DebugValues.Instantiate/Free` 这条对象生命周期路径仍然可编译、可执行。 |

### 发现 73：`UASFunction::GetSourceFilePath()` 无视脚本函数实际所属 section，multi-section 模块的源码导航会固定指向 `Code[0]`

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` / `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` / `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_scriptfunction.h` / `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_scriptfunction.cpp` |
| 行号 | `ASClass.cpp:1535-1560` / `AngelscriptEngine.cpp:4342-4346` / `as_scriptfunction.h:405-409` / `as_scriptfunction.cpp:934-944, 1518-1524` |
| 描述 | 编译阶段会把 `FAngelscriptModuleDesc::Code` 里的每个 section 都通过 `AddScriptSection(Section.AbsoluteFilename, Section.Code, ...)` 注入 AngelScript module，说明单个 module 可以承载多个源码 section。第三方运行时也为每个 `asCScriptFunction` 保存了 `scriptSectionIdx` / `sectionIdxs`，并暴露 `GetScriptSectionName()` 与 `GetLineNumber(..., &sectionIdx)` 来解析函数真实所属的 section。相对地，`UASFunction::GetSourceFilePath()` 完全不读取这些字段，而是只拿 `ScriptFunction->GetModule()` 反查 module 后直接返回 `Module->Code[0].AbsoluteFilename`；`UASClass::GetSourceFilePath()` / `GetRelativeSourceFilePath()` 也采用同样的 `Code[0]` 固定首文件策略。 |
| 根因 | source metadata API 把“函数属于哪个 script section”简化成了“函数属于哪个 module”，然后又把 module 折叠成 `Code[0]`。ClassGenerator 没有把 AngelScript 已经提供的 section-level 信息接入到 `UASFunction` / `UASClass` 的源码定位接口。 |
| 影响 | 只要一个 module 是 multi-section，源码导航、报错跳转和任何依赖 `GetSourceFilePath()` 的工具都会把函数或类固定导向首个 section，而不是实际声明所在文件。这个问题不会让类生成立刻崩溃，但会让热重载排障、source navigation 和编辑器工具在复杂模块上持续给出错误文件路径，破坏开发期的一致性与可追溯性。 |

---

## 分析 (2026-04-08 12:56)

### 发现 74：`UASClass` 重新声明了 `ReferenceSchema`，类生成器写入的是子类字段，但 GC 读取的是 `UClass` 基类字段

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | Critical |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h` / `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` / `UEAS2/Engine/Source/Runtime/CoreUObject/Public/UObject/Class.h` / `UEAS2/Engine/Source/Runtime/CoreUObject/Public/UObject/FastReferenceCollector.h` |
| 行号 | `ASClass.h:34, 77` / `AngelscriptClassGenerator.cpp:4875-4924` / `Class.h:4095, 4275` / `FastReferenceCollector.h:98-107, 1012` |
| 描述 | `UASClass` 在自己的类体里又声明了一份 `UE::GC::FSchemaOwner ReferenceSchema`，而引擎基类 `UClass` 已经在 `Class.h` 中持有同名字段。`DetectAngelscriptReferences()` 以 `UASClass*` 为静态类型执行 `Schema.Append(Class->ReferenceSchema.Get())` 和 `Class->ReferenceSchema.Set(View)`，因此它读写的是 `UASClass` 这份新字段；但 GC 快速路径在 `FastReferenceCollector.h` 里通过 `UClass* Class = CurrentObject->GetClass(); FSchemaView Schema = Class->ReferenceSchema.Get();` 取 schema，并且连预取偏移都显式写成 `offsetof(UClass, ReferenceSchema)`，说明运行时扫描固定读取的是基类 `UClass::ReferenceSchema`。与此同时，`UASClass::RuntimeAddReferencedObjects()` 还是空实现，没有第二条补偿路径去收集脚本侧引用。 |
| 根因 | 插件在引擎已经内建 `UClass::ReferenceSchema` 之后，继续在派生类里保留了旧的同名成员，造成“类生成阶段更新子类字段、GC 阶段消费基类字段”的静态类型分裂。 |
| 影响 | `DetectAngelscriptReferences()` 发射的 script-only 引用信息不会进入 GC 实际扫描的 schema。结果是所有不映射成 `FProperty`、只能靠 `EmitReferenceInfo()` 描述的脚本引用，都可能在对象仍被脚本持有时被 GC 提前回收；而排查时又会看到 `UASClass` 上“明明有 schema”，形成非常隐蔽的假象。这已经不是测试缺口，而是版本链/热重载之外的直接内存安全问题。 |

### 发现 75：`ScriptTypePtr` 与 `bIsScriptClass` 也发生了同样的基类/派生类字段分裂，引擎侧脚本类判定会读到另一份状态

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | Critical |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h` / `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` / `UEAS2/Engine/Source/Runtime/CoreUObject/Public/UObject/Class.h` / `UEAS2/Engine/Source/Runtime/CoreUObject/Private/UObject/UObjectGlobals.cpp` / `UEAS2/Engine/Source/Runtime/CoreUObject/Private/Blueprint/BlueprintSupport.cpp` / `UEAS2/Engine/Source/Runtime/Engine/Private/ActorReplication.cpp` |
| 行号 | `ASClass.h:29-30` / `AngelscriptClassGenerator.cpp:3291, 3678-3679, 4200-4203, 4304-4308` / `Class.h:3911-3912` / `UObjectGlobals.cpp:4582, 4661-4668, 4967` / `BlueprintSupport.cpp:2685-2689` / `ActorReplication.cpp:537-538` |
| 描述 | 引擎基类 `UClass` 已经有 `void* ScriptTypePtr` 和 `bool bIsScriptClass` 字段，但 `UASClass` 在自己的头里又重新声明了一份同名成员。当前类生成器把“脚本类身份”写入的赋值点全部落在 `UASClass*` 静态类型上，例如 full reload 时 `NewClass->bIsScriptClass = true`、`NewClass->ScriptTypePtr = ScriptType`，soft reload 时 `Class->ScriptTypePtr = ScriptType`，以及派生 blueprint 同步时 `asClass->ScriptTypePtr = Class->ScriptTypePtr`。相对地，引擎运行时关键分支都是通过 `UClass*` 读取基类字段：`UObjectGlobals.cpp` 用 `Class->bIsScriptClass` 决定对象初始化/CDO 快路径和“script class 视作 native subobject”的逻辑，`BlueprintSupport.cpp` 用 `Parent->ScriptTypePtr` 与 `Parent->bIsScriptClass` 决定是否调用 `RuntimeDestroyObject()`，`ActorReplication.cpp` 用 `GetClass()->ScriptTypePtr` 决定是否收集 script replication list。 |
| 根因 | 插件把一组已经上移到 `UClass` 的运行时状态继续保留在 `UASClass` 里，随后生成/热重载代码仍按旧习惯只更新派生类副本，导致引擎基类视角与插件派生类视角长期分叉。 |
| 影响 | 这会让大量引擎级脚本类钩子天然建立在错误状态上：对象初始化可能继续把 script class 当成普通非 native 类处理，析构路径可能因为基类 `ScriptTypePtr` 为空而跳过脚本析构，复制路径也可能完全不进入 `GetLifetimeScriptReplicationList()`。更严重的是，这条字段分裂不是单个 callsite 的漏同步，而是 `UClass`/`UASClass` 两套“脚本类身份位”同时存在，后续任何新增引擎判断都可能再次读错那一份。 |
