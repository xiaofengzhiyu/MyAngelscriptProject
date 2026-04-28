# ClassGenerator 发现与规划

---

## 发现与方案 (2026-04-08 12:31)

### Issue-1：删除后未 GC 的同名 `UASClass` 会在重建时被误判为 `ReplacedClass`

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` |
| 行号 | `AngelscriptClassGenerator.cpp:224-279, 2573-2578, 4990-5023`; `ASClass.cpp:912-923` |
| 问题 | `Analyze()` 只在同名对象仍带有效 `ScriptTypePtr` 时才把它视为冲突对象；`CleanupRemovedClass()` 删除类时会把 `ScriptTypePtr` 清空，但保留原始对象名直到 GC；后续如果脚本在 GC 前重新声明同名类，`CreateFullReloadClass()` 会再次 `FindObject<UASClass>(..., UnrealName)` 命中这个“已删除但未搬离 canonical name”的旧类，并把它当成真正的 `ReplacedClass` 去 `Rename(..._REPLACED_*)` 和接入版本链。 |
| 根因 | “删除类”和“替换类”共用同一套按对象名查找旧类的逻辑，但删除路径没有立即把旧类从 canonical 名称空间迁走，也没有留下显式 tombstone 状态让后续创建阶段区分“可替换旧版本”和“待 GC 的删除残留”。 |
| 影响 | 同名类的 `remove -> recreate` 会被错误降格成 `replace`。结果包括：新类沿用删除类的版本链入口、`GetMostUpToDateClass()` 继续把历史引用导向本不该关联的节点，以及旧类遗留的热重载状态被错误复用，直接放大版本链污染和后续崩溃面。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为 removed class 引入显式 tombstone/rename 流程，阻断“按原名再次命中删除残留”的伪替换路径。 |
| 具体步骤 | 1. 在 `CleanupRemovedClass()` 中，先把 removed `UASClass` 立即重命名为稳定的 tombstone 名称，例如 `<Name>_REMOVED_<N>`，再执行 `RemoveFromRoot()`/`ClearFlags(RF_Standalone)`。 2. 在 `Analyze()` 的同名冲突检查里，把“命中同名 `UASClass` 但其处于 removed/tombstone 状态”视为 brand-new create，而不是可替换旧类。 3. 在 `CreateFullReloadClass()` 中增加显式校验：只有当前 `OldClass` 或仍绑定有效 script type 的 live class 才能进入 `ReplacedClass` 分支，否则直接走 brand-new class 流程。 4. 为版本链入口增加一次 defensive check，避免 removed/tombstone 节点再被写入 `NewerVersion`。 5. 新增运行时回归测试：执行 `V1 compile -> remove class -> compile -> recreate same name before GC -> compile`，断言新类不是从 deleted class 继承出来的 replace 节点，且历史 `UASClass*` 不会把 `GetMostUpToDateClass()` 导向新类。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`; `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptScriptClassCreationTests.cpp` 或 `Plugins/Angelscript/Source/AngelscriptTest/HotReload/*` |
| 预估工作量 | M |
| 风险 | 立即改名 removed class 会影响依赖按对象名查找历史类的调试工具或测试 helper，需要同步检查这些调用点是否应改为 tombstone-aware 查询。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 新增自动化覆盖 `remove -> recreate same name`。 2. reload 后枚举 `/Script/Angelscript` 包，确认 canonical 名只对应 live class，deleted class 已迁移到 tombstone 名。 3. 对历史 `UASClass*` 调 `GetMostUpToDateClass()`，确认不会跨越到新建但逻辑无关的类。 |

---

## 发现与方案 (2026-04-08 12:31)

### Issue-2：interface 声明里的额外 `ImplementedInterfaces` 被解析后直接丢弃

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 行号 | `AngelscriptPreprocessor.cpp:800-821, 969-975`; `AngelscriptClassGenerator.cpp:5059-5159, 5189-5199` |
| 问题 | 预处理器会对 `class` 和 `interface` 统一解析继承列表，把第一个父类型后的所有名字写入 `ClassDesc->ImplementedInterfaces`。注释还明确说明 interface-to-interface 继承保留在描述数据里。但 `FinalizeClass()` 只在 `!ClassDesc->bIsInterface` 时才把 `ImplementedInterfaces` materialize 成 `UClass::Interfaces`；interface 分支随后直接 `FinalizeObjectClass()` 并 `return`。结果是 `interface IChild : IParent, IOther` 这类 secondary interface 在生成阶段完全丢失，后续 `AddInterfaceRecursive()` 期待从 `InterfaceClass->Interfaces` 读取的传递接口图也永远拿不到这条边。 |
| 根因 | 预处理器和类生成器对“interface 自己也可以携带额外接口依赖”这一语义没有闭环：前者保留了描述数据，后者却把 interface 当成只有单一 `SuperClass` 的特殊分支处理。 |
| 影响 | 传递接口 contract 会被静默裁剪。直接表现为：实现类只声明实现 `IChild` 时，UE 侧不会把 `IOther` 记进 `Interfaces`，`IOther` 的 required methods 也不会被校验；Blueprint/反射层看到的接口图会比脚本源码少一层，类型检查结果与源码不一致。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 抽出统一的 interface graph materialization 逻辑，让 `class` 和 `interface` 都走同一条 `ImplementedInterfaces` 构建路径。 |
| 具体步骤 | 1. 从 `FinalizeClass()` 中提取一个独立 helper，例如 `BuildImplementedInterfaceGraph(FClassData&, UClass*)`，负责 `ResolveInterfaceClass()`、去重、递归展开 `SuperClass`/`Interfaces` 并写入 `UClass::Interfaces`。 2. 在 `ClassDesc->bIsInterface` 分支里也调用该 helper，再继续 `FinalizeObjectClass()` 和注册。 3. 对 script interface 的 additional interface 做 compile-time validation，确保被引用目标确实是 `CLASS_Interface`。 4. 调整实现类验证逻辑，明确基于 materialized 后的 `NewClass->Interfaces` 校验所有传递接口方法，而不是只信任源码直接声明。 5. 新增自动化：`interface IChild : IParent, IOther`，以及 `class Foo : UObject, IChild`，断言 `Foo->Interfaces` 同时包含 `IParent`、`IOther`、`IChild`，并且缺少 `IOther` 方法时编译失败。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`; `Plugins/Angelscript/Source/AngelscriptTest/Interface/*` |
| 预估工作量 | M |
| 风险 | interface graph 一旦完整 materialize，现有项目里依赖“少校验一层接口”的脚本可能开始在编译期报错，需要提前评估兼容性并给出迁移说明。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 新增 transitive interface 场景测试，检查 `UClass::Interfaces` 内容。 2. 对缺少 secondary interface 方法的脚本执行编译，确认生成阶段稳定报错。 3. 在 Blueprint/反射侧调用 `ImplementsInterface()`，确认结果与脚本声明一致。 |

---

## 发现与方案 (2026-04-08 12:31)

### Issue-3：`DefaultComponent`/`OverrideComponent` 布局在四条路径重复维护，缺少单一 builder

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Refactoring |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` |
| 行号 | `AngelscriptClassGenerator.cpp:4181-4197, 5214-5444, 5467-5695`; `ASClass.cpp:1176-1328` |
| 问题 | 当前 actor 组件布局没有单一真相来源。`FinalizeActorClass()` 负责从 property metadata 组装 `DefaultComponents`/`OverrideComponents` 并做一部分校验；`VerifyClass()` 又在 CDO 上重扫同一批组件关系做另一部分校验；`DoSoftReload()` 只修这两个数组里的 offset；`ApplyOverrideComponents()`/`CreateDefaultComponents()` 则把数组解释成运行时构造步骤。四处代码都在消费“组件布局”概念，但输入、校验和应用分散在不同阶段，且中间状态只是两个裸 `TArray`。 |
| 根因 | 组件树生成被实现成一组阶段式副作用，而不是一个可复用的 `layout plan` 对象。于是解析、验证、offset 更新和运行时 materialize 只能通过共享数组隐式耦合，任何新规则都需要在多处同步修改。 |
| 影响 | 这不是纯粹的可读性问题。多源状态让 soft reload、editor-only 校验、parent override 查找和运行时构造很容易出现语义漂移，后续继续修热重载或组件树问题时，修改成本和回归面都会持续上升。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 提取 `ActorComponentLayoutBuilder`，把“解析 metadata -> 归一化布局 -> 校验 -> 应用到 UASClass/运行时构造”收束成单一路径。 |
| 具体步骤 | 1. 在 `ClassGenerator` 下新增一个聚合类型，例如 `FASActorComponentLayout`，显式承载 default components、override components、root 选择、attach 关系和 editor-only 标记。 2. 新增 `FASActorComponentLayoutBuilder`，只负责从 `FAngelscriptClassDesc` 和父类布局构建这个 plan，并在 builder 内统一完成 root/attach/override 的合法性校验。 3. 让 `FinalizeActorClass()` 只调用 builder 并把结果写入 `UASClass`，不再边扫描边修改数组。 4. 让 `VerifyClass()` 只消费 builder 生成的归一化布局，而不是再次从 `UClass`/CDO 临时推导父子关系。 5. 在 soft reload 中，不再手工遍历数组修 offset，而是重跑 builder 或提供一个 `RebindPropertyOffsets()` 针对 layout plan 更新。 6. 让 `CreateDefaultComponents()`/`ApplyOverrideComponents()` 读取统一的 layout plan 数据结构，从而保证运行时 materialize 与编译期验证使用的是同一份描述。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`; 建议新增 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASActorComponentLayout.h/.cpp` |
| 预估工作量 | L |
| 风险 | 该重构会触碰类生成、soft reload 和 actor 构造三条链路，若没有先补回归测试，容易把现有 happy-path 组件创建一起打坏。 |
| 前置依赖 | 建议先补齐 `DefaultComponent`/`OverrideComponent` 的 reload 与非法声明测试，再做重构。 |
| 验证方式 | 1. 为 builder 补单元测试，覆盖 root、attach、override、editor-only 组合。 2. 运行现有 component/hot reload 场景测试，确认生成和运行时构造结果未回退。 3. 对一次 metadata-only 修改执行 soft reload，确认 builder 输出与 full reload 一致。 |

---

## 本轮汇总 (2026-04-08 12:31)

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-1 | Defect | 先修；它会把 `remove -> recreate` 误接到版本链，继续放大会污染后续排查结果 |
| P1 | Issue-2 | Defect | 紧随其后；修正 interface graph 丢边，避免接口 contract 与反射结果继续分叉 |
| P2 | Issue-3 | Refactoring | 在前两项稳定后执行；作为组件布局单一 builder 的基础重构，降低后续 hot reload 修复成本 |

---

## 发现与方案 (2026-04-08 12:46)

### Issue-4：`PrepareSoftReload()` / `DoSoftReload()` 对 `OldClass->Class == nullptr` 缺少防护，`DiscardModule` 后会落入空类崩溃

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 行号 | `AngelscriptEngine.cpp:3436-3443`; `AngelscriptClassGenerator.cpp:4033-4035, 4087-4101, 4115-4133` |
| 问题 | 已验证事实有两处。其一，重新编译旧模块描述时，`CompileModule` 路径会显式把复制出来的 `FAngelscriptClassDesc` 的 `Class` / `Struct` 清成 `nullptr`。其二，`LinkSoftReloadClasses()` 已经在 `4033-4035` 加了注释和防护，明确说明 “can happen after DiscardModule invalidates shared state”。但同一轮 soft reload 的另外两个入口没有复用这条前置条件：`PrepareSoftReload()` 只检查 `OldClass.IsValid()`，随后直接把 `ClassData.OldClass->Class` 传给 `NewObject(..., Class, ...)`；`DoSoftReload()` 也只检查 `OldClass.IsValid()`，随后立刻 `check(Class)`。根据这组代码可推断，只要旧描述对象仍然有效但其 `Class` 已被清空，soft reload 就会在 prepare 阶段或 `check(Class)` 处硬崩。 |
| 根因 | “旧描述仍有效，但底层 `UClass*` 已失效”这个状态只在 `LinkSoftReloadClasses()` 被局部处理，没有被抽成统一的 soft reload precondition；结果同一生命周期假设在不同入口出现了不一致实现。 |
| 影响 | 这类崩溃不是理论分支，源码已经明确承认 `DiscardModule` 会制造该失效态。当前实现下，只要 reload 判定仍把这类 class 当成 soft reload 目标，类生成器就会在 `PrepareSoftReload()` 或 `DoSoftReload()` 提前中断，导致后续模块 swap-in 无法完成。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 抽出统一的 “live old class 解析” 守卫，把 `OldClass.IsValid()` 和 `OldClass->Class != nullptr` 作为同一个前置条件处理。 |
| 具体步骤 | 1. 在 `AngelscriptClassGenerator.cpp` 新增 helper，例如 `UASClass* TryGetLiveOldClass(FClassData&, bool& bNeedsFullReloadFallback)`，统一检查 `OldClass.IsValid()`、`OldClass->Class != nullptr`、以及对象是否仍为 `UASClass`。 2. 让 `LinkSoftReloadClasses()`、`PrepareSoftReload()`、`DoSoftReload()`、`EnsureReloaded(UASClass*)` 全部改用这个 helper，而不是各自重复做半套判断。 3. 当 helper 发现 `OldClass` 描述仍在但 `Class == nullptr` 时，不要继续 soft reload；直接把当前 `ClassData.ReloadReq` 提升为 `FullReloadRequired`，或把它降级成 brand-new materialization 路径，保证后续只走 `CreateFullReloadClass()`。 4. 给这条 fallback 补日志，明确输出模块名、类名和触发原因，便于后续排查 `DiscardModule` 相关时序问题。 5. 新增回归测试或 harness：构造一次 `DiscardModule()` 触发的共享状态失效，再执行 reload，断言不会在 `PrepareSoftReload()` / `DoSoftReload()` 崩溃，而是稳定转入 full reload / brand-new create。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`; `Plugins/Angelscript/Source/AngelscriptTest/HotReload/*` |
| 预估工作量 | M |
| 风险 | 将异常 soft reload 强制升级为 full reload 会增加一次 reload 的工作量，需要确认不会意外放大普通 body-only 修改的成本。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 人工或自动化构造 `DiscardModule` 后旧描述 `Class == nullptr` 的场景，确认 reload 不触发断言。 2. 观察日志，确认命中该失效态时走的是预期 fallback，而不是继续执行 `PrepareSoftReload()`。 3. 回归运行现有 hot reload suite，确认普通 soft reload 不受影响。 |

### Issue-5：`CallPostInitFunctions()` 忽略执行结果，literal asset 初始化失败后会把半初始化对象永久缓存

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_context.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/Testing/AngelscriptTest.cpp` |
| 行号 | `AngelscriptPreprocessor.cpp:4110-4119, 4132-4133`; `AngelscriptClassGenerator.cpp:5775-5800`; `as_context.h:73-79`; `AngelscriptTest.cpp:357-373` |
| 问题 | literal asset 语法会被预处理器展开成 `Get{Name}()`：先把新建 asset 写入全局 `__Asset_{Name}`，再调用用户实现的 `__Init_{Name}` 和 `__PostLiteralAssetSetup`，随后把 getter 名字登记到 `PostInitFunctions`。类生成阶段执行这些 getter 时，`CallPostInitFunctions()` 只做 `PrepareAngelscriptContextWithLog(...)` 检查，然后直接 `Context->Execute();`，完全不读取返回码，也不设置 `bModuleSwapInError`。`as_context.h` 和测试框架都表明 `Execute()` 的返回值才是区分正常结束、异常和错误的正式信号。这样一来，只要 `__Init_{Name}` 或 `__PostLiteralAssetSetup` 失败，`__Asset_{Name}` 仍然保留着已经创建但未初始化完成的对象；后续任何 `Get{Name}()` 调用都会在 `if (__Asset_{Name} != nullptr) return __Asset_{Name};` 处直接返回这份脏对象。 |
| 根因 | literal asset 的“创建、初始化、缓存”被实现成非事务式流程：缓存发生在用户初始化之前，而执行层又没有把 `Execute()` 失败升级成 reload 错误或清理动作。 |
| 影响 | 这会把一次脚本初始化失败从“本轮 reload 应该中止”放大成“运行时拿到一份永久缓存的半初始化 asset”。更糟的是，这些 `PostInitFunctions` 发生在 `InitDefaultObjects()` 之前，后续 CDO 构造和其他 post-init 代码都可能继续读取这份脏 asset，直接污染默认值和类初始化状态。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 literal asset 初始化改成事务式流程：只有 `Execute()` 成功后才提交缓存，并把失败显式上升为 module swap error。 |
| 具体步骤 | 1. 修改预处理器生成的 getter 模板，不要先写 `__Asset_{Name}`；改成先创建局部临时变量，例如 `auto LocalAsset = Cast<{Type}>(__CreateLiteralAsset(...));`，执行 `__Init_{Name}(LocalAsset)` 与 `__PostLiteralAssetSetup(LocalAsset, ...)` 成功后，再把 `__Asset_{Name} = LocalAsset`。 2. 在 `CallPostInitFunctions()` 中读取 `Context->Execute()` 返回值；若结果是 `asEXECUTION_EXCEPTION` / `asEXECUTION_ERROR` / 非成功态，立刻把 `ModuleData.NewModule->bModuleSwapInError = true`，输出包含函数名的诊断，并停止继续执行后续 post-init。 3. 为失败路径补清理：若 getter 已经创建了临时 asset，但初始化失败，则显式把对象标记为垃圾或放回可回收状态，避免它继续挂在 literal asset package 里。 4. 让 `InitDefaultObjects()` 只在所有 `PostInitFunctions` 成功后继续执行，避免半初始化 asset 参与后续 CDO 构造。 5. 新增测试：让 `__Init_{Name}` 主动抛异常，断言 reload 标记为失败、`Get{Name}()` 不会返回脏 asset、再次编译修复后 getter 能重新成功初始化。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/*` 或 `Plugins/Angelscript/Source/AngelscriptTest/HotReload/*` |
| 预估工作量 | M |
| 风险 | 调整 getter 模板会影响所有 literal asset 生成代码，需要确认成功路径下对象身份与缓存时机没有破坏现有脚本约定。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 新增异常版 literal asset 场景，确认 `Execute()` 失败会阻止 `__Asset_{Name}` 缓存。 2. reload 后检查 `PostInitFunctions` 失败时模块被标成 `bModuleSwapInError`。 3. 修复脚本后再次编译，确认同名 getter 能重新创建并返回完整初始化的 asset。 |

### Issue-6：版本链生命周期缺少单一 owner，替换、删除、查询规则分散在三层代码里，已经超出局部修补的安全边界

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSubclassOf.h` |
| 行号 | `ASClass.h:19`; `ASClass.cpp:912-923, 1186-1189, 1219-1222`; `AngelscriptClassGenerator.cpp:2573-2578, 3696-3699, 4990-5024`; `Bind_TSubclassOf.h:80-89` |
| 问题 | 当前版本链没有集中式 owner。`UASClass` 只暴露一个原生字段 `NewerVersion`；`CreateFullReloadClass()` 负责 rename/replace，`DoFullReloadClass()` 负责把旧类指向新类，`CleanupRemovedClass()` 又单独处理删除，但完全不经过统一接口；运行时查询则散落在 `GetMostUpToDateClass()`、默认组件构造、`TSubclassOf` 绑定等多个调用点。也就是说，链的状态转移规则不是一个模块维护的显式协议，而是若干文件里的隐式 side effect 组合。源码里看不到任何地方集中表达“什么是 live head、什么时候允许 detach、谁负责 tombstone、谁负责 GC 可见性”这些不变量。 |
| 根因 | 版本链最初被实现成轻量的 `UASClass*` 追指针方案，但随着 hot reload、remove、component class resolution、`TSubclassOf` 兼容判断等能力叠加，链已经从“内部实现细节”演化成跨生成器/运行时/绑定层的共享基础设施；代码结构却仍停留在分散写点 + 分散读点。 |
| 影响 | 在这种结构下，任何单点 defect 修复都必须手工同步多个调用方，且很难证明不变量仍然成立。结果就是版本链相关问题会反复以不同表象出现：一次修掉 replace/remove 生命周期，下一次又可能从组件解析或类型绑定侧重新暴露。继续沿用分散式实现，会让后续 hot reload 修复越来越像“追着症状补洞”。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把版本链提炼成独立子模块，统一托管 replace/remove/tombstone/latest-resolve/GC 可见性五类职责。 |
| 具体步骤 | 1. 在 `ClassGenerator` 下新增专门模块，例如 `ASVersionChain.h/.cpp`，提供显式 API：`RegisterReplacement(Old, New)`、`RegisterRemoval(Head)`、`ResolveLatest(UClass*)`、`DetachRemovedHead(...)`、`OnClassGC(...)`。 2. 将 `CreateFullReloadClass()`、`DoFullReloadClass()`、`CleanupRemovedClass()` 里直接写 `NewerVersion`、`Rename`、root flag 的版本链逻辑搬进该模块，只保留调用。 3. 让 `UASClass::GetMostUpToDateClass()`、`CreateDefaultComponents()`、`ApplyOverrideComponents()`、`Bind_TSubclassOf` 等运行时消费者不再自己追 raw pointer，而是统一调用 registry/adapter 获取“当前可用类”。 4. 在 registry 中用 `TWeakObjectPtr<UASClass>` 或等价机制记录链边，并提供显式 tombstone 状态，避免把 GC 安全性寄托在裸指针和 root flag 上。 5. 为 registry 单独补测试矩阵，覆盖 `replace -> replace`、`replace -> remove`、`remove -> recreate`、`TSubclassOf`/component 查询穿越版本链等关键序列，再把现有版本链修复逐步迁入该模块。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; 建议新增 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASVersionChain.h/.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSubclassOf.h` |
| 预估工作量 | L |
| 风险 | 这是跨生成器与运行时消费点的结构调整，若没有先把现有版本链行为用测试冻结，重构过程中容易把历史兼容行为一起改掉。 |
| 前置依赖 | 建议先补齐版本链相关回归测试，再做模块抽离。 |
| 验证方式 | 1. 新模块测试覆盖 replace/remove/recreate 序列，确认 `ResolveLatest` 结果稳定。 2. 对组件创建与 `TSubclassOf` 绑定做回归，确认它们不再直接依赖 raw `NewerVersion`。 3. 在 editor 长时间 reload 压测里观察对象数量与内存曲线，确认版本链不再靠历史节点常驻来维持可用性。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-4 | Defect | 先修；这是已存在失效态上的直接崩溃入口，会让 soft reload 在进入真正修复逻辑前就失败 |
| P1 | Issue-5 | Defect | 第二优先；它会把 literal asset 初始化失败扩大成全局缓存污染，并继续影响后续 CDO 初始化 |
| P1 | Issue-6 | Architecture | 在前两项止血后立即立项；否则版本链问题仍会在不同调用面反复回归 |

---

## 发现与方案 (2026-04-08 12:58)

### Issue-7：`SoftReloadOnly` 没有兑现 `FullReloadRequired`，结构变更仍会被送进原地 soft reload（补充 Analysis 36）

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Critical |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 行号 | `1068-1135, 2081-2093, 2239-2275` |
| 问题 | 分析阶段已经把 superclass 变化、property 删除、property 类型/定义变化等场景提升到 `FullReloadRequired`；但真正执行 reload 时，`ShouldFullReload(FClassData&)` 在 soft reload 模式下完全不看 `Class.ReloadReq`，只对 interface、实现 interface 的类和 brand-new class 返回 `true`。`PerformReload(false)` 随后据此直接把其余类送进 `PrepareSoftReload()` / `DoSoftReload()`。这意味着“已被源码分析证明必须重建类对象”的变更，在 `SoftReloadOnly` 下仍会复用旧 `UASClass` 和旧 `FProperty`/`UFunction` 外壳。 |
| 根因 | reload 判定被拆成“分析阶段设置 `ReloadReq`”和“执行阶段调用 `ShouldFullReload()`”两套逻辑，但后者没有把前者作为硬约束，导致 `FullReloadRequired` 在执行层失效。 |
| 影响 | 结构性变更会在最不安全的路径里继续运行：旧 property/function 壳体不会被删除，后续 offset 重算、CDO 重建和组件布局更新都建立在陈旧类对象之上。现有仓库里已经出现多个由此派生的 hard failure 面，例如 component property 改名后在 `DoSoftReload()` 中 `check(Property != nullptr)` 直接断言。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 `ReloadReq >= FullReloadRequired` 提升为执行层硬门槛，禁止结构变更继续进入原地 soft reload。 |
| 具体步骤 | 1. 在 `AngelscriptClassGenerator.cpp` 提取统一判定 helper，例如 `RequiresClassReinstancing(const FClassData&)`，第一条规则就是 `Class.ReloadReq >= EReloadRequirement::FullReloadRequired` 返回 `true`。 2. 让 `ShouldFullReload(FClassData&)`、`PrepareSoftReload()` 调度点、`DoSoftReload()` 调度点、以及 `EnsureReloaded(...)` 统一只消费这一个 helper，避免“分析认为必须 full reload，执行仍走 soft reload”的分叉。 3. 对 `SoftReloadOnly` 会话增加明确策略分支：若当前环境允许原地 full reload，则直接走 `CreateFullReloadClass()` / `DoFullReloadClass()`；若当前环境不允许，则在任何类对象被修改前设置 `bModuleSwapInError` 并输出明确错误，保留旧类继续生效。 4. 把 brand-new class、interface、implemented-interface 这些现有特例也折叠进同一 helper，形成单一 reload decision source。 5. 补回归测试：`SoftReloadOnly` 下分别覆盖 property rename、property type change、superclass change，断言不会进入 `DoSoftReload()`，并且运行结果要么稳定走 full reload，要么稳定拒绝 swap-in，而不是复用旧类对象。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`; `Plugins/Angelscript/Source/AngelscriptTest/HotReload/*`; `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/*` |
| 预估工作量 | M |
| 风险 | 将更多场景升级为 real full reload 或显式拒绝 swap-in，会改变当前部分“虽然不正确但偶尔能跑”的开发流程，需要同步更新测试和错误提示。 |
| 前置依赖 | 无 |
| 验证方式 | 1. `SoftReloadOnly` 下对 structural change 场景打日志或埋点，确认不会执行 `PrepareSoftReload()` / `DoSoftReload()`。 2. 运行 property rename / superclass change 回归，确认不再出现旧 `FProperty`/旧组件描述导致的断言。 3. 回归普通 body-only 修改，确认仍可保持原有 soft reload 速度。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P0 | Issue-7 | Defect | 立即修复；否则分析阶段标出的结构性变更仍会持续落入错误执行路径 |

---

## 发现与方案 (2026-04-08 13:00)

### Issue-8：`DoSoftReload()` 对 defaults 语义采取“双轨制”，旧实例/CDO 与新建对象会看到两套默认值（补充 Analysis 39）

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` |
| 行号 | `AngelscriptClassGenerator.cpp:4140-4141, 4281, 4605-4623, 4735-4755, 5889-5903`; `ASClass.cpp:1399-1405, 1452-1458, 1488-1494` |
| 问题 | `DoSoftReload()` 明确写着“soft reload 要保留 defaults code”，并把 `ClassDesc->DefaultsCode` 回退成旧版本；但同一函数随后又调用 `UpdateConstructAndDefaultsFunctions()`，把类上的 `DefaultsFunction` 更新成当前新 `ScriptType` 的实现。与此同时，旧实例和旧 CDO 的重建路径只做 `DestructScriptObject()` + `ReinitializeScriptObject()`，而 `ReinitializeScriptObject()` 只重跑构造函数，不执行 `ExecuteDefaultsFunctions()`。结果是同一轮 soft reload 之后，未来新创建的对象会执行新 defaults，而现有实例/CDO 则继续保留“旧 defaults + 旧值拷贝”的状态。 |
| 根因 | defaults 语义在设计上想“soft reload 期间冻结”，但实现上同时保留了“更新类上的 defaults 函数指针”这条路径；对象重建 helper 又没有与之配套地重跑 defaults，导致类级语义与对象级重建策略互相矛盾。 |
| 影响 | 这会直接制造 CDO 和实例状态分叉：同一个 `UASClass` 在同一源码版本下，已有对象与未来对象看到的默认值不同。后续再做一次 hot reload 时，`bModifiedByDefaults` 的基线和实际对象状态都会继续漂移，问题会以“有些对象更新了默认值，有些对象没有”这种最难定位的形式扩散。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 defaults 语义改成单一策略：defaults 一旦变化就不再允许原地 soft reload；保留 soft reload 时则不得换入新 `DefaultsFunction`。 |
| 具体步骤 | 1. 在分析阶段把 defaults 语义变化提升为 `FullReloadRequired`，覆盖 `DefaultsCode` 文本变化、`__InitDefaults()` override 新增/删除、以及 defaults 相关 metadata 变化，禁止这类变更继续走 `DoSoftReload()`。 2. 将 `UpdateConstructAndDefaultsFunctions()` 拆成两个 helper，例如 `UpdateConstructFunction()` 与 `UpdateDefaultsFunction()`；`DoSoftReload()` 在确认 defaults 语义未变时只刷新构造函数，不改 `Class->DefaultsFunction`。 3. 在 `DoSoftReload()` 入口增加 defensive check：如果分析结果表明 defaults 语义已变而当前仍进入 soft reload，立即设置 `bModuleSwapInError` 并中止本类重建，而不是继续留下“双轨默认值”。 4. 若产品希望“defaults 改动也能在线生效”，则需要新增单独事务：在 `ReinitializeScriptObject()` 后执行 `ExecuteDefaultsFunctions()`，再仅回拷真正的用户修改字段；但这应作为后续独立项目，不应与当前止血修复捆绑。 5. 补自动化：`__InitDefaults()` 体修改、子类 defaults override 删除、以及 defaults body 不变的普通 body-only 修改三组场景，分别验证会触发 full reload/拒绝 swap-in，或保持现有 soft reload 行为。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`; `Plugins/Angelscript/Source/AngelscriptTest/HotReload/*`; `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/*` |
| 预估工作量 | M |
| 风险 | 将 defaults 改动升级为 full reload，会让部分此前“勉强可用”的热更新场景变得更保守，但这是为了先恢复类语义一致性。 |
| 前置依赖 | 建议与 Issue-7 同步处理，共用统一 reload 判定入口。 |
| 验证方式 | 1. 修改 `__InitDefaults()` 后执行 `SoftReloadOnly`，确认不会再出现“旧对象一套、新对象一套”的默认值分叉。 2. 对 body-only 且 defaults 未变的场景回归，确认 `DefaultsFunction` 不会被误判成需要 full reload。 3. 比较 reload 前后 CDO 与新建实例的同字段默认值，确认二者语义一致。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-8 | Defect | 在 Issue-7 后立刻处理；否则 CDO 与实例默认值会继续在 soft reload 后分叉 |

---

## 发现与方案 (2026-04-08 13:01)

### Issue-9：`NewerVersion` 是未受 GC 保护的裸指针，remove 后版本链会退化成真实的 use-after-free 风险（补充 Analysis 50）

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Critical |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 行号 | `ASClass.h:19`; `ASClass.cpp:917-923`; `AngelscriptClassGenerator.cpp:3696-3699, 4999-5023` |
| 问题 | 版本链通过 `UASClass::NewerVersion` 这个原生裸指针维护，`GetMostUpToDateClass()` 又会无条件沿链一直解引用到尾节点。与此同时，full reload 会在 `DoFullReloadClass()` 把旧类的 `NewerVersion` 指向新类；删除类时，`CleanupRemovedClass()` 会把当前 head `RemoveFromRoot()` 并清掉 `RF_Standalone`。这条链既不是 `UPROPERTY`，也没有任何有效性检查或失效回写，所以 head 一旦被 GC，历史节点里留下的就是悬挂 `UObject*`。 |
| 根因 | 版本链把“类生命周期引用”实现成了普通原生指针，但查询侧把它当成始终 live 的对象关系使用；删除路径又允许链尾节点进入 GC，彻底打破了这个隐含前提。 |
| 影响 | 这不是单纯的历史状态错误，而是直接的 crash 面。`GetMostUpToDateClass()` 被默认组件创建和 `TSubclassOf` 兼容判断等运行时路径调用，只要历史节点还活着、链尾已经被 GC，任何一次“取最新类”都会落入 use-after-free。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把版本链从“裸指针追尾”改成“可失效、可验证的 live link”，并在 remove 时主动断开指向被删 head 的入边。 |
| 具体步骤 | 1. 将 `UASClass::NewerVersion` 从裸 `UASClass*` 改为 `TWeakObjectPtr<UASClass>` 或等价 weak handle，并同步更新所有写入点。 2. 重写 `GetMostUpToDateClass()`：每一步都先检查 `IsValid()`、是否命中 removed/tombstone 类、以及是否出现自环/重复节点；一旦链条无效，立即停在最后一个 live class，而不是继续解引用。 3. 在 `CleanupRemovedClass()` 中，在 `RemoveFromRoot()` 之前遍历现存 `UASClass`，把所有指向当前 removed head 的 `NewerVersion` 入边清空或重定向到最后一个有效 live 节点，避免 GC 后历史节点仍然保留悬挂 link。 4. 为 `CreateFullReloadClass()` / `DoFullReloadClass()` 增加统一 setter，例如 `LinkNewerVersion(Old, New)`，集中做 weak-link 写入和循环防护，不再直接赋值字段。 5. 与 Issue-6 的版本链模块化方案并轨：若后续抽出 `ASVersionChain`，则把上述 weak-link 和断链逻辑收口到新模块中。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSubclassOf.h` |
| 预估工作量 | M |
| 风险 | 调整链条存储类型后，依赖“旧类永远能追到当前 head”的调用点可能暴露出新的空结果，需要一起补 defensive handling。 |
| 前置依赖 | 建议与 Issue-1、Issue-6 协同；尤其是 removed tombstone 策略能让“何时应当断链”更明确。 |
| 验证方式 | 1. 构造 `replace -> remove -> GC -> GetMostUpToDateClass()` 回归，确认不会崩溃，且解析结果停在最后一个 live 节点或安全返回 self/null。 2. 回归 `CreateDefaultComponents()` 与 `Bind_TSubclassOf`，确认它们在 head 已删除时不会继续追进悬挂对象。 3. 在编辑器里重复执行 replace/remove 压测并强制 GC，确认不再出现版本链随机崩溃。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P0 | Issue-9 | Defect | 与 Issue-7 并列优先；这是 remove/GC 后最直接的版本链崩溃入口 |

---

## 发现与方案 (2026-04-08 13:09)

### Issue-10：`UASClass` 继续 shadow `UClass` 的运行时字段，类生成器和引擎运行时读写的是两套状态（补充 Analysis 74/75）

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Critical |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `UEAS2/Engine/Source/Runtime/CoreUObject/Public/UObject/Class.h`; `UEAS2/Engine/Source/Runtime/CoreUObject/Public/UObject/FastReferenceCollector.h`; `UEAS2/Engine/Source/Runtime/CoreUObject/Private/UObject/UObjectGlobals.cpp`; `UEAS2/Engine/Source/Runtime/CoreUObject/Private/Blueprint/BlueprintSupport.cpp`; `UEAS2/Engine/Source/Runtime/Engine/Private/ActorReplication.cpp` |
| 行号 | `ASClass.h:28-34`; `AngelscriptClassGenerator.cpp:3678-3679, 4200-4203, 4875-4924`; `Class.h:3910-3912, 4094-4095`; `FastReferenceCollector.h:98-107, 1012-1016`; `UObjectGlobals.cpp:4580-4582, 4661-4668, 4967`; `BlueprintSupport.cpp:2683-2691`; `ActorReplication.cpp:536-538` |
| 问题 | `UASClass` 在派生类里重新声明了 `ScriptTypePtr`、`bIsScriptClass` 和 `ReferenceSchema`，而当前类生成器的写入点也都落在这组 shadow 字段上。引擎侧关键消费点却全部通过 `UClass*` 读取基类字段：对象初始化读取 `bIsScriptClass`，析构读取 `ScriptTypePtr`/`bIsScriptClass`，复制读取 `ScriptTypePtr`，GC 读取 `UClass::ReferenceSchema`。这导致“插件写的是 `UASClass` 副本，引擎用的是 `UClass` 正本”，两边天然失配。 |
| 根因 | 插件早期把 runtime generated class 状态挂在 `UASClass` 上；在引擎把同名字段上移到 `UClass` 后，插件头文件和生成流程没有同步收敛到单一存储，留下了同名字段 shadow。 |
| 影响 | 这不是单点逻辑错误，而是系统性状态分裂。直接后果包括：引擎把 script class 继续当普通类初始化、析构阶段跳过 `RuntimeDestroyObject()`、script replication list 无法进入、script-only GC schema 根本不被 collector 消费。热重载和类生成即使局部看起来成功，运行时仍可能因为读取了另一份空状态而表现为引用丢失、析构缺失、复制缺失或默认对象行为异常。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 删除 `UASClass` 对 `UClass` 运行时状态的 shadow，统一以基类 `UClass` 字段作为唯一真相来源。 |
| 具体步骤 | 1. 在 `ASClass.h` 删除或显式废弃 `UASClass` 中重复声明的 `ScriptTypePtr`、`bIsScriptClass`、`ReferenceSchema`，避免后续任何代码再能写到派生类副本。 2. 在 `ClassGenerator` 中提取统一 helper，例如 `SetScriptRuntimeState(UClass*, asITypeInfo*)`、`ClearScriptRuntimeState(UClass*)`、`ResetScriptReferenceSchema(UClass*)`，只操作基类 `UClass` 字段。 3. 将 `CreateClass`、`DoSoftReload`、`CleanupRemovedClass`、derived blueprint sync、`DetectAngelscriptReferences()` 等所有读写点改为走上述 helper，确保 `ScriptTypePtr`、`bIsScriptClass`、`ReferenceSchema` 始终写入同一份存储。 4. 对 `DetectAngelscriptReferences()` 增加一次 defensive assert 或静态检查，明确它构建的是 `UClass::ReferenceSchema`，并在 cleanup/reload 时同步清空旧 schema，避免旧数据残留。 5. 补回归测试：构造带 script-only 引用字段的类并触发 GC，断言对象不会被提前回收；创建/销毁 script actor，断言会进入 `RuntimeDestroyObject()`；对带 replication 的 script class 运行网络属性收集，断言 `GetLifetimeScriptReplicationList()` 被调用。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/*`; `Plugins/Angelscript/Source/AngelscriptTest/GC/*`; `Plugins/Angelscript/Source/AngelscriptTest/HotReload/*` |
| 预估工作量 | L |
| 风险 | 删除 `UASClass` 字段会改变类布局并暴露其他仍依赖 shadow 字段的调用点；这类修改必须以完整重编译和跨模块回归为前提，不能依赖局部 hot reload 验证。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 对 script actor 执行创建、销毁、GC 三段回归，确认 `RuntimeDestroyObject()` 与 script-only 引用收集都正常触发。 2. 对带复制属性的 script actor 跑一轮 replication 测试，确认 `GetLifetimeScriptReplicationList()` 进入。 3. 对 full reload 和 soft reload 各跑一次，确认 `Class->bIsScriptClass`、`Class->ScriptTypePtr`、`Class->ReferenceSchema` 在基类视角与 `UASClass` 视角完全一致。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P0 | Issue-10 | Defect | 立即修复；这是 class runtime state 的单一真相源问题，不先收敛，后续 GC/析构/复制修复都会继续读错字段 |

---

## 发现与方案 (2026-04-08 13:10)

### Issue-11：`PrepareSoftReload()` 用 `RF_ArchetypeObject` 伪造 `CDONoDefaults`，soft reload 的 defaults 基线从一开始就不是实际 CDO（补充 Analysis 69）

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`; `UEAS2/Engine/Source/Runtime/CoreUObject/Private/UObject/UObjectGlobals.cpp`; `UEAS2/Engine/Source/Runtime/CoreUObject/Private/UObject/Obj.cpp` |
| 行号 | `AngelscriptClassGenerator.cpp:4093-4108, 4490-4500`; `ASClass.cpp:1359-1361, 1415-1417, 1467-1469`; `UObjectGlobals.cpp:4657-4670`; `Obj.cpp:1683-1685, 1970-1988, 2255-2258` |
| 问题 | `PrepareSoftReload()` 为了构造 `CDONoDefaults`，用 `NewObject(..., RF_ArchetypeObject)` 生成了一个临时对象，再靠 `GConstructASObjectWithoutDefaults` 抑制 defaults 执行，并把它拿去和真实 `BaseCDO` 做逐字段对比。问题在于引擎明确只把 `RF_ClassDefaultObject` 视为真实 CDO：非 CDO 对象会预加载真实 `Class->GetDefaultObject()`，序列化 diff 也只对 `RF_ClassDefaultObject` 走 CDO 分支。换言之，这里的 `CDONoDefaults` 不是“少跑了 defaults 的 CDO”，而是“一个带 archetype 标志的普通临时对象”。 |
| 根因 | soft reload 试图用对象 flags 近似复刻 CDO 语义，但 CDO 生命周期在 UE 里不只是一个标志位开关，还涉及默认对象命名、序列化 diff、模板预加载和 class-default 快路径。当前实现只复制了最表面的 `RF_ArchetypeObject`。 |
| 影响 | `bModifiedByDefaults` 的判定建立在错误基线上，会把 native 初始化差异、真实 CDO 语义和脚本 defaults 改写混在一起。后续 property copy、CDO 重建和实例保留逻辑因此可能误判“这是 defaults 改出来的值”或反向漏掉它，最终表现为 soft reload 后 CDO/实例默认值继续漂移。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 先停止依赖伪 CDO 做 defaults 差分；凡是需要真实 CDO 语义才能判定正确的类，一律升级为 full reload 或拒绝 swap-in。 |
| 具体步骤 | 1. 在分析阶段新增一个明确判定，例如 `RequiresTrueCDOBaseline(const FClassData&)`，只要类本身或任一脚本父类存在 `DefaultsCode`、`__InitDefaults()`、依赖 default component 构造、或 config/default metadata，就把该类直接提升为 `FullReloadRequired`。 2. 对仍允许 soft reload 的 body-only 类，删除 `PrepareSoftReload()` 中 `CDONoDefaults` 伪造路径，`bModifiedByDefaults` 只在“无脚本 defaults 参与”的前提下恒为 `false`，从而把 soft reload 收敛到纯函数体/非默认值变更场景。 3. 在 `SoftReloadOnly` 模式下，如果命中 `RequiresTrueCDOBaseline()`，不要继续构造 `CDONoDefaults`；直接设置 `bModuleSwapInError` 并输出明确日志，说明当前改动必须 full reload。 4. 如果后续确实需要支持“带 defaults 的 soft reload”，单独在 `UEAS2` 层新增 helper，显式构造真实 scratch CDO 并复刻 `RF_ClassDefaultObject` 语义；这应作为后续独立项目，不和本次止血混做。 5. 为 defaults 相关回归补测试：`__InitDefaults()` 修改、仅函数体修改、带 `DefaultComponent` 的 actor body-only 修改三组场景，分别断言 full reload / 拒绝 swap-in / 继续 soft reload 的行为符合预期。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`; `Plugins/Angelscript/Source/AngelscriptTest/HotReload/*`; `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/*`; 如需长期支持真实 scratch CDO，再新增 `UEAS2/Engine/Source/Runtime/CoreUObject/*` 辅助实现 |
| 预估工作量 | M |
| 风险 | 该止血策略会让更多 defaults 相关修改不再尝试 soft reload，短期内会牺牲一部分热更新速度；但这比继续用错误基线复制默认值更可控。 |
| 前置依赖 | 建议与 Issue-7、Issue-8 一起实施，共用统一的 reload 决策入口。 |
| 验证方式 | 1. 修改 `__InitDefaults()` 或 default component metadata 后执行 `SoftReloadOnly`，确认不会再构造 `CDONoDefaults`，而是稳定 full reload 或明确拒绝 swap-in。 2. 对纯函数体修改回归，确认不受影响，仍能走 soft reload。 3. 对 CDO 与新建实例同字段做 reload 前后对比，确认默认值来源不再依赖伪 CDO。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-11 | Defect | 在 Issue-10 后处理；否则 soft reload 的 defaults 差分仍然建立在错误 CDO 基线上 |

---

## 发现与方案 (2026-04-08 13:11)

### Issue-12：类生成流程把无参构造函数当成必然存在，遇到 ctor-only script class 会在注册或重建时直接崩溃（补充 Analysis 9）

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_builder.cpp` |
| 行号 | `AngelscriptClassGenerator.cpp:4833-4848, 5889-5903`; `as_builder.cpp:724-742` |
| 问题 | AngelScript builder 在类声明了其他构造函数时，会主动移除自动生成的默认构造函数并把 `ot->beh.construct` 置为 `0`。但 `UpdateConstructAndDefaultsFunctions()` 仍然无条件把 `ObjType->beh.construct` 传给 `GetFunctionById()`，随后立刻对结果做 `isInUse = true`；`ReinitializeScriptObject()` 也只是到运行时才 `ensureMsgf(false, "does not have a constructor with no arguments")`。这意味着只要脚本类是“只有带参 ctor，没有默认 ctor”的合法形态，类创建或 hot reload 就会先踩空指针，再落到“will crash soon”的不受控路径。 |
| 根因 | 当前 UObject-backed script class 的运行时模型隐含要求“必须存在无参构造函数”，但这个约束既没有在编译期被强制，也没有在类生成器入口被显式验证；实现把一个约定误当成了类型系统保证。 |
| 影响 | 这会把本该是“编译时报一个明确约束错误”的问题升级成生成期/热重载期崩溃。开发者一旦尝试声明 ctor-only script class，编辑器就可能在注册 `ConstructFunction`、重建 CDO 或 soft reload 旧实例时直接中断。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 先把“UObject-backed script class 必须提供无参 ctor”收敛成显式编译约束，所有运行时路径都改成 fail-fast，而不是继续解引用空函数。 |
| 具体步骤 | 1. 在类生成分析阶段新增显式校验：如果 `asCObjectType::beh.construct == 0`，立刻对该类报编译错误，错误文案直接说明“当前动态 `UClass` 生成要求无参构造函数”。 2. 修改 `UpdateConstructAndDefaultsFunctions()`，在 `beh.construct == 0` 时不要再调用 `GetFunctionById()` 或写 `isInUse`，而是把 `ConstructFunction` 置空并返回失败结果给调用方。 3. 让 `CreateClass`、`DoSoftReload`、`ReinitializeScriptObject()` 统一消费这个失败结果：命中 ctor-only 类时设置 `bModuleSwapInError` 并停止本类生成/重建，而不是走 `ensure` 或空指针崩溃。 4. 若产品后续希望支持 ctor-only script class，则单独立项实现“引擎侧 hidden default ctor”或等价桥接逻辑；在该能力真正落地前，不要让运行时半支持这种类形态。 5. 新增自动化：一个只有带参 ctor 的 script class 应当稳定编译失败；在 hot reload 场景里从“有默认 ctor”改成“只有带参 ctor”也应给出可读错误，而不是崩溃。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`; `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/*`; `Plugins/Angelscript/Source/AngelscriptTest/HotReload/*` |
| 预估工作量 | S |
| 风险 | 将隐式崩溃改成显式编译失败后，可能会暴露已有脚本里依赖该未定义行为的用法；但这是可迁移的显式 breakage，比 editor crash 可控得多。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 新增 ctor-only class 测试，确认报的是编译错误而不是崩溃。 2. 在 hot reload 场景里把默认 ctor 删除，确认 reload 被稳定拒绝并保留旧模块。 3. 回归正常带无参 ctor 的脚本类，确认类创建、CDO 重建和 soft reload 不受影响。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-12 | Defect | 在 Issue-10 和 Issue-11 后处理；它是明确的崩溃入口，但修复面相对局部 |

---

## 发现与方案 (2026-04-08 13:12)

### 本轮汇总（补充）

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P0 | Issue-10 | Defect | 第一优先；先消除 `UClass`/`UASClass` 双状态，后续 GC、析构、复制和热重载修复才能建立在同一份 runtime state 上 |
| P1 | Issue-11 | Defect | 第二优先；收紧 defaults 相关 soft reload，避免继续用伪 CDO 基线复制默认值 |
| P1 | Issue-12 | Defect | 第三优先；把 ctor-only class 从“生成期崩溃”收敛成显式编译错误 |

---

## 发现与方案 (2026-04-08 13:16)

### Issue-13：`DoSoftReload()` 在同步 blueprint 派生类时把 `UBlueprintGeneratedClass` 当成 `UASClass` 解引用，存在直接崩溃路径（补充 Analysis 11）

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Critical |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 行号 | `4286-4312` |
| 问题 | `DoSoftReload()` 为了把新 `ScriptType` 同步到所有 blueprint child，会遍历 `UBlueprintGeneratedClass`，随后立刻执行 `UASClass* asClass = Cast<UASClass>(CheckClass)`。但 `UBlueprintGeneratedClass` 并不继承 `UASClass`，所以这里对真正的 blueprint generated class 得到的 `asClass` 固定是 `nullptr`。代码后面却在 `ASClass == Class` 分支里直接 `ensure(asClass->ScriptTypePtr == OldScriptType)` 并写 `asClass->ScriptTypePtr = Class->ScriptTypePtr`。只要存在任何继承当前脚本类的 blueprint child，这条 soft reload 路径就会在同步派生类 runtime state 时空指针崩溃。 |
| 根因 | blueprint child 同步逻辑把“脚本父类是 `UASClass`”误写成了“blueprint child 自己也是 `UASClass`”。类型判定和写入容器选错，导致 `GetFirstASClass(CheckClass)` 返回的是脚本父类，但真正被写入的对象仍按 `UASClass` 假设处理。 |
| 影响 | 这是一个可达的 reload 崩溃入口。body-only 变更本应走 soft reload 的脚本类，只要被 blueprint 继承，reload 就可能在派生类同步阶段中断；而此时前面的 `ScriptTypePtr` 更新、schema 重建和旧实例重建可能已经部分完成，容易把编辑器留在半 swap-in 状态。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 停止把 blueprint child 当成 `UASClass` 写入，改用统一的 `UClass` 运行时状态 helper 做派生类同步。 |
| 具体步骤 | 1. 在 `AngelscriptClassGenerator.cpp` 提取专用 helper，例如 `SyncDerivedBlueprintRuntimeState(UClass* CheckClass, UASClass* ReloadedClass, asITypeInfo* OldScriptType)`，统一处理 blueprint child 的判定、state 同步和后处理。 2. helper 内不要再 `Cast<UASClass>(CheckClass)` 后直接解引用；改为先用 `UASClass::GetFirstASClass(CheckClass)` 判断该 blueprint 是否继承当前脚本类，再通过 `UClass` 级别的读写 helper 更新 runtime state。短期 containment 可至少把 `ensure(asClass->...)` 改成基于 `CheckClass` 的安全查询；长期应与 Issue-10 合并，统一写基类 `UClass` 的 `ScriptTypePtr` / `bIsScriptClass`。 3. 将 `DestroyAngelscriptUnversionedSchema(CheckClass)`、`AssembleReferenceTokenStream()`、复制列表刷新等 blueprint child 后处理也收进同一 helper，避免未来再出现“判定一套、写入一套”的分散逻辑。 4. 在命中非 `UASClass` blueprint child 时增加显式日志/`checkSlow`，确保后续再有人误用 `Cast<UASClass>(UBlueprintGeneratedClass)` 时能第一时间暴露。 5. 新增 hot reload 回归：脚本父类 + blueprint child，执行 body-only soft reload，断言 reload 不崩溃，blueprint child 仍能正确调用更新后的脚本逻辑。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`; `Plugins/Angelscript/Source/AngelscriptTest/HotReload/*`; `Plugins/Angelscript/Source/AngelscriptTest/Blueprint/*` |
| 预估工作量 | M |
| 风险 | 若继续沿用 `UASClass` shadow 字段做短期止血，可能只修掉空指针而没有真正修正 blueprint child 的 runtime state 来源；因此最好与 Issue-10 统一设计 helper 接口。 |
| 前置依赖 | 建议与 Issue-10 协同，但空指针 containment 可以先独立落地。 |
| 验证方式 | 1. 新增“脚本类被 blueprint 继承”的 soft reload 自动化，确认不会在 `DoSoftReload()` 崩溃。 2. reload 后从 blueprint child 触发脚本 override / blueprint 调用，确认逻辑已切到新脚本版本。 3. 在 debug 构建下保留 `checkSlow`，确认不会再出现对 `nullptr asClass` 的写入。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P0 | Issue-13 | Defect | 与 Issue-10 并列优先；任何带 blueprint child 的 soft reload 都可能直接崩溃 |

---

## 发现与方案 (2026-04-08 13:19)

### Issue-14：script ctor/defaults 的 `Execute()` 失败不会中止类生成事务，旧脚本状态已销毁后仍继续推进重建（补充 Analysis 66）

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_context.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/Testing/AngelscriptTest.cpp` |
| 行号 | `ASClass.cpp:1093-1120, 1125-1133, 1352-1494`; `AngelscriptClassGenerator.cpp:4604-4636, 4735-4755, 4825-4845, 5807-5854`; `as_context.h:73-77`; `AngelscriptTest.cpp:357-373` |
| 问题 | `ExecuteDefaultsFunctions()`、`ExecuteConstructFunction()` 和 `ReinitializeScriptObject()` 都只检查 `PrepareAngelscriptContext*()`，随后直接调用 `Context->Execute()` 而完全忽略返回值。`as_context.h` 明确 `Execute()` 会返回执行状态，测试框架也把 `asEXECUTION_EXCEPTION` / `asEXECUTION_ERROR` 当成正式失败信号。更危险的是，`DoSoftReload()` 在 `4604-4623` 与 `4735-4755` 已经先 `DestructScriptObject()` 销毁旧脚本对象，再调用 `ReinitializeScriptObject()`；如果这次执行失败，当前实现仍继续走后面的属性回拷，没有任何 `bModuleSwapInError`、回滚或中止逻辑。`InitDefaultObjects()` 对 full reload CDO 创建同样只做 `NewClass->GetDefaultObject(true)`，也没有接收构造/defaults 失败状态。 |
| 根因 | script ctor/defaults 被封装成了 `void` helper，执行结果没有被纳入 ClassGenerator 的 reload 事务控制。于是“脚本初始化失败”只会停留在日志层，而不会转成“本次 swap-in 失败、应保留旧模块/旧对象”的显式状态机分支。 |
| 影响 | 只要 ctor/defaults 在 CDO 初始化或 soft reload 重建时抛异常，系统就会落入“旧脚本状态已销毁，新脚本状态没完成，但 live UObject 继续存在”的半初始化状态。后续对象复制、CDO 默认值、析构与 GC 都会建立在这份脏状态上，问题不再是单次 reload 失败，而是把类生成错误扩散成运行时对象污染。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 script ctor/defaults 执行改成可回传结果的事务步骤；一旦失败，立即停止 swap-in，并且绝不在销毁旧 script state 之后继续推进。 |
| 具体步骤 | 1. 将 `ExecuteConstructFunction()`、`ExecuteDefaultsFunctions()`、`ReinitializeScriptObject()` 改成返回显式结果，例如 `EASInitResult { Success, PrepareFailed, ExecuteFailed }`；对 `Context->Execute()` 的返回值按 `asEXECUTION_FINISHED` / 失败态做统一判定。 2. 在 full reload 的 CDO 初始化路径上增加 thread-local 或显式上下文，把 ctor/defaults 失败上传到 `InitDefaultObjects()` / `InitDefaultObject()`，一旦失败就设置 `ModuleData.NewModule->bModuleSwapInError = true`，停止当前模块后续 `CallPostInitFunctions()`、blueprint sync 和 swap-in。 3. 重写 soft reload 的重建顺序：不要先 `DestructScriptObject()` 再赌新 ctor 会成功；先在临时 scratch script object 上完成 `ReinitializeScriptObject()`，确认成功后再销毁旧 script state 并提交属性回拷。这样失败时还能保留旧实例/CDO，而不是把 live 对象清成半成品。 4. 为失败后的清理收口一个 helper，例如 `RollbackFailedScriptInit(...)`，统一释放临时 script object、debug values 和临时 buffer，避免不同路径各自留下半初始化残骸。 5. 新增回归：`__InitDefaults()` 抛异常、无参 ctor 主动 `Throw()`、以及 soft reload 期间旧实例/CDO 重建失败三组场景，断言本轮 reload 被标成 swap-in error、旧模块仍可用、live 对象不会被清成半初始化状态。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`; `Plugins/Angelscript/Source/AngelscriptTest/HotReload/*`; `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/*` |
| 预估工作量 | L |
| 风险 | soft reload 改成 scratch-then-commit 后会触碰实例/CDO 重建主链路，如果没有测试保护，容易把现有属性保留逻辑一起打坏。 |
| 前置依赖 | 建议与 Issue-11 一起评估，因为 defaults 相关 soft reload 判定本来就需要收紧。 |
| 验证方式 | 1. 构造 `Throw()` 版 ctor/defaults 测试，确认 `Execute()` 失败会把模块标成 `bModuleSwapInError`。 2. 对 soft reload 失败场景验证旧实例/CDO 仍可继续工作，而不是被清成空壳。 3. 修复脚本后再次 reload，确认系统能从上一次失败中恢复，而不是永久残留半初始化对象。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-14 | Defect | 在 Issue-11 后立即处理；否则 ctor/defaults 失败仍会把 reload 错误放大成 live 对象污染 |

---

## 发现与方案 (2026-04-08 13:20)

### Issue-15：`DoSoftReload()` 不会重新计算 tick 能力缓存，`Tick`/`ReceiveTick` 语义变化后类和对象仍沿用旧调度状态（补充 Analysis 12）

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` |
| 行号 | `AngelscriptClassGenerator.cpp:4248-4268, 5697-5773, 5807-5844`; `ASClass.cpp:1370-1372, 1423-1425` |
| 问题 | 类的 tick 调度能力被缓存到 `UASClass::bCanEverTick` 和 `bStartWithTickEnabled`，`InitClassTickSettings()` 会根据父类状态以及 `Tick` / `ReceiveTick` 是否是 `ScriptNoOp` 计算这两个值。但 `InitDefaultObjects()` 只在 `ShouldFullReload(ClassData)` 为真时调用它，soft reload 分支完全不会刷新这两个缓存。与此同时，`DoSoftReload()` 已经会在 `4248-4268` 更新函数体和 `FUNCMETA_ScriptNoOp`，说明 tick 语义变化在 soft reload 里是可达的；而 actor/component 构造函数又始终把缓存值拷到 `PrimaryActorTick` / `PrimaryComponentTick`。结果是只要一次 soft reload 改变了 `Tick` / `ReceiveTick` 的存在性或 no-op 语义，后续新对象乃至保留实例都会继续沿用 reload 前的 tick 配置。 |
| 根因 | tick 能力被建模成“类生成阶段的一次性派生状态”，但 soft reload 只更新函数实现和 metadata，没有重新运行依赖函数语义的派生计算，也没有把变更传播到现存对象的 tick registration。 |
| 影响 | 开发者把 `Tick` 从 no-op 改成有效实现后，对象仍可能完全不 tick；反向修改则会继续无意义地注册 tick。该问题只在 soft reload 场景出现，行为与当前源码不一致，而且会同时污染新创建对象和保留下来的 live 实例，排查成本很高。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 在具备完整 live tick 重注册能力前，把 tick 语义变化升级为 full reload；同时抽出可复用的 tick cache 刷新 helper，消除当前“函数更新了、类缓存没更新”的断层。 |
| 具体步骤 | 1. 在 `Analyze()` / function diff 阶段显式检测 `Tick`、`ReceiveTick` 的新增、删除以及 `bIsNoOp` 变化；一旦命中，直接把当前类标记为 `FullReloadRequired`，不要继续走 `DoSoftReload()`。 2. 从 `InitClassTickSettings()` 提取幂等 helper，例如 `RefreshClassTickSettings(FClassData&, bool bForceRecompute)`；full reload 和任何未来允许的 tick-safe soft reload 都统一走它，而不是依赖 `bHasEvalTick` 的一次性缓存。 3. 在 `DoSoftReload()` 末尾增加 defensive check：如果当前类的 tick 语义发生变化却仍进入 soft reload，立刻记录诊断并把模块标记为 `bModuleSwapInError`，防止带着旧 tick cache 继续运行。 4. 若后续需要真正支持 tick 语义的 soft reload，再单独立项补 `ApplyTickSettingsToLiveObject()` / tick re-registration，把类缓存变化同步到 CDO、现存 actor/component 与 blueprint child；在此能力落地前，不要假装 body-only reload 对 tick 是安全的。 5. 新增回归：`Tick` 从 no-op 变有效、从有效变 no-op、以及 `ReceiveTick` 新增/删除三组场景，断言它们都会触发 full reload 或被明确拒绝 soft reload，且 reload 后对象的 `Primary*Tick` 与脚本源码语义一致。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`; `Plugins/Angelscript/Source/AngelscriptTest/HotReload/*`; `Plugins/Angelscript/Source/AngelscriptTest/Actor/*`; `Plugins/Angelscript/Source/AngelscriptTest/Component/*` |
| 预估工作量 | M |
| 风险 | 将 tick 语义变化提升为 full reload 会牺牲一部分 body-only 热更新速度，但这比继续让 live 对象和新对象读旧 tick cache 更可控。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 修改 `Tick` / `ReceiveTick` 后执行 hot reload，确认不会再静默走 soft reload。 2. reload 完成后检查 actor/component 的 `Primary*Tick` 标志，确认与当前脚本实现一致。 3. 回归普通不涉及 tick 的 body-only 修改，确认仍可维持现有 soft reload 体验。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-15 | Defect | 在 Issue-11、Issue-14 之后处理；先把 tick 语义变更从 soft reload 安全集合里剔除 |

---

## 发现与方案 (2026-04-08 13:22)

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P0 | Issue-13 | Defect | 第一优先；先封住 blueprint child soft reload 的直接崩溃入口 |
| P1 | Issue-14 | Defect | 第二优先；把 ctor/defaults 失败从“日志事件”提升为真正的 reload 事务失败 |
| P1 | Issue-15 | Defect | 第三优先；收紧 tick 语义变更的 reload 判定，避免 live/new 对象继续读旧缓存 |

---

## 发现与方案 (2026-04-08 13:28)

### Issue-16：`bModuleSwapInError` 不会阻止当前轮次的类注册与 CDO 初始化，错误类会直接污染 live 类型系统（补充 Analysis 58）

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | `AngelscriptClassGenerator.cpp:2291-2312, 5039-5210, 5214-5695, 5807-5853`; `AngelscriptEngine.cpp:4047-4053` |
| 问题 | `FinalizeClass()`、`FinalizeActorClass()` 和 `VerifyClass()` 在接口缺失、`DefaultComponent` 非法、attach/root 校验失败等路径上多次设置 `ModuleData.NewModule->bModuleSwapInError = true`，但 `FinalizeClass()` 末尾仍无条件 `NotifyRegistrationEvent(...)` 注册 `NewClass`。随后 `PerformReload()` 也不看这个错误位，继续全局执行 `CallPostInitFunctions()`、`InitDefaultObjects()` 和 `VerifyClass()`；`InitDefaultObject()` 还会直接 `NewClass->GetDefaultObject(true)` 创建 CDO。最终 `AngelscriptEngine.cpp` 对 `bModuleSwapInError` 的后处理只是把源文件加入 `PreviouslyFailedReloadFiles` 以便下次继续重编，没有撤销本轮已注册的新类。 |
| 根因 | `bModuleSwapInError` 被实现成“编译后重试提示”而不是 reload 事务的中止/回滚信号。类注册、CDO 构造、post-init 与最终验证之间缺少统一的 abort barrier。 |
| 影响 | 一次已知失败的 class generation 不会停留在“旧类继续生效”的安全状态，而是把错误新类先挂进 `/Script/Angelscript`，再让 post-init 和 CDO 构造继续读取它。这样后续反射查询、默认组件构造、Blueprint 交互和下一轮 reload 都会建立在半成品 `UClass` 上，直接放大 CDO 状态不一致与热重载污染。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 `bModuleSwapInError` 提升为真正的 swap-in 事务失败信号，在“注册 live 类”之前建立统一 abort barrier。 |
| 具体步骤 | 1. 在 `PerformReload()` 中引入显式阶段结果，例如 `FReloadTransactionState`，统一汇总“是否有 class finalize / verify / post-init 失败”。 2. 修改 `FinalizeClass()` / `FinalizeActorClass()` / `VerifyClass()`：一旦设置 `bModuleSwapInError`，立即停止当前类后续 finalize，不再继续 `NotifyRegistrationEvent(...)`。 3. 在 `CallPostInitFunctions()` 和 `InitDefaultObjects()` 前增加全局 barrier：只要任一模块已进入 swap-in error，本轮不再创建新 CDO、不再执行 post-init，也不再推进 blueprint/子系统激活。 4. 对已经 materialize 但尚未 commit 的 full reload 类增加 rollback helper，例如 `RollbackFailedNewClasses(...)`，统一撤销 root、`StaticClass` 全局变量、`NewerVersion` 链接和 registration event，确保旧类保持可用。 5. 将 `AngelscriptEngine.cpp` 里 `PreviouslyFailedReloadFiles` 的逻辑保留为“下轮重试”，但前提改成“本轮已经 rollback 完成”，避免它继续掩盖 live 污染。 6. 新增回归：制造 `DefaultComponent` attach 错误、缺失 interface 方法、以及 verify 阶段 root component 错误三种场景，断言失败轮次不会产生新的已注册 `UClass`/CDO，旧类仍可正常查询与实例化。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptTest/HotReload/*`; `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/*` |
| 预估工作量 | L |
| 风险 | 把 `bModuleSwapInError` 从“延迟修复提示”收紧成“立即 abort”后，会暴露一批当前依赖半成功状态的编辑器工作流；需要用回归测试证明旧类保活与 rollback 清理都完整。 |
| 前置依赖 | 建议与 Issue-14 协同设计，统一 ctor/defaults 失败和 finalize/verify 失败的事务状态机。 |
| 验证方式 | 1. 构造 finalize/verify 失败脚本，确认 `/Script/Angelscript` 中不会留下新的错误类。 2. 失败后立即查询旧类 `StaticClass()`、实例化旧 CDO/对象，确认仍可工作。 3. 修复脚本后再次编译，确认 reload 能正常前进，且不会受到上一次半注册状态残留影响。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-16 | Defect | 在 Issue-14 后执行；先补事务 abort barrier，避免后续任何发现继续把错误类提交到 live 运行时 |

---

## 发现与方案 (2026-04-08 13:29)

### Issue-17：删除 script interface 时未同步清理 engine-level `asITypeInfo->UserData`，后续类型绑定会继续消费 stale `UClass*`（补充 Analysis 52）

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` |
| 行号 | `AngelscriptClassGenerator.cpp:2596-2611, 4990-5024`; `Bind_BlueprintType.cpp:133-138, 165-167`; `AngelscriptDebugServer.cpp:1885-1893` |
| 问题 | script interface 在 `CreateFullReloadClass()` 里不会从模块取 `ScriptType`，而是直接通过 `RegisterObjectType()` 注册 engine-level reference type，再把 `InterfaceScriptType->SetUserData(NewClass)` 指到对应 `UASClass`。但删除路径 `CleanupRemovedClass()` 只清 UE 侧 `Class->ScriptTypePtr`、`ConstructFunction` 和 `DefaultsFunction`，完全没有对 `ClassDesc->ScriptType` 或 engine-level `asITypeInfo` 做 `SetUserData(nullptr)`。后续 `Bind_BlueprintType` 仍会把 `Usage.ScriptClass->GetUserData()` 当成 `PropertyClass`，调试服务器也会继续从 `ScriptType->GetUserData()` 取 metadata 来源。 |
| 根因 | script interface 的类型注册生命周期被拆成了两层：UE 侧 `UASClass` 可以删除/GC，但 AngelScript engine 全局类型表中的 `asITypeInfo` 仍然保留对旧 `UClass` 的裸缓存，没有统一的 remove/unregister 收尾。 |
| 影响 | 一旦 interface 被删除，后续编译和工具链仍可能通过同名 engine-level type 成功解析到旧 `UClass*`。在类对象尚未 GC 时，这会把已隐藏/失效的 interface 继续写回新的属性与反射数据；在类对象 GC 后，它会退化成悬挂指针，直接把热重载清理问题升级成类型绑定和调试路径的潜在 crash 面。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为 script interface 引入成对的 registration handle，把 `RegisterObjectType` 与 remove 清理绑定到同一条生命周期。 |
| 具体步骤 | 1. 在 `FAngelscriptClassDesc` 或专用 registry 结构里显式记录“这是 engine-level registered interface type”以及对应 `asITypeInfo*`。 2. 修改 `CleanupRemovedClass()`：若 `ClassDesc->bIsInterface` 且存在记录的 `asITypeInfo`，先执行 `SetUserData(nullptr)`，再清 UE 侧 `UASClass` 状态和 root flag。 3. 将 `Bind_BlueprintType` 与其他 `GetUserData()` 消费点改成 defensive helper，例如 `ResolveLiveUClassFromScriptType(asITypeInfo*)`；helper 需要验证 `UserData` 指向的 `UClass` 仍有效、未被标记 hidden/tombstone、且与当前 registered type 一致。 4. 若 AngelScript runtime 允许，应进一步增加“逻辑反注册”或 tombstone 标记，确保已删除 interface 不会继续通过 `GetTypeInfoByName()` 参与新一轮属性生成。 5. 为调试和 property 生成路径补日志：命中 stale interface type 时输出 interface 名、来源模块和 remove 轮次，便于追查未清理状态。 6. 新增回归：`interface V1 -> remove interface -> 重新编译引用它的属性/`Cast<>``/调试查询`，断言编译稳定报错或得到空结果，而不是继续拿到旧 `UClass`。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`; `Plugins/Angelscript/Source/AngelscriptTest/Interface/*`; `Plugins/Angelscript/Source/AngelscriptTest/HotReload/*` |
| 预估工作量 | M |
| 风险 | 如果直接把 `UserData` 清空，现有少数依赖“删除后同名 interface 还能被旧类型表找到”的脚本/工具会开始显式失败；但这是把未定义行为转成可诊断错误，风险可控。 |
| 前置依赖 | 建议与 Issue-1、Issue-9 一起统一 old type 清理策略，避免 class/version chain 已 tombstone，而 interface type 仍保留 live cache。 |
| 验证方式 | 1. 删除 interface 后重新编译引用它的脚本，确认绑定阶段不再从 `GetUserData()` 取到旧 `UClass`。 2. 强制 GC 后重复触发 `Bind_BlueprintType` / 调试查询，确认不会因 stale pointer 崩溃。 3. 修复为新的同名 interface 后再次编译，确认新的 registration handle 能正确接管，旧 handle 已彻底失效。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-17 | Defect | 在 Issue-16 后执行；先让删除路径真正清掉 interface type cache，避免 stale `UClass*` 继续渗入新一轮生成 |

---

## 发现与方案 (2026-04-08 13:29)

### Issue-18：类型身份仍以短名为核心键，namespace 在 UE 注册层被整体丢弃，导致 reload 与类型解析都缺乏可扩展性（补充 Analysis 55/56）

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 行号 | `126-170, 1752-1766, 2570-2586, 2596-2611, 5912-5934` |
| 问题 | 类解析阶段已经明确支持 namespace：`GetNamespacedTypeInfoForClass()` 会按 `Namespace + ClassName` 向 AngelScript module 取 `asITypeInfo`。但进入 UE 注册层后，这个限定身份被整段丢掉：`DataRefByName`、`EnsureClassAnalyzed()` 和 `GetClassDesc()` 都只按 `ClassName` 查找；`CreateFullReloadClass()` 又把短名送进 `GetUnrealName()`，随后直接 `FindObject<UASClass>(Package, *UnrealName)` / `NewObject<UASClass>(..., FName(*UnrealName), ...)`；interface 路径更进一步，直接按短名 `RegisterObjectType(InterfaceName)`。结果是两个不同 namespace 的 script class/interface 在 AS 层是不同 type，在 UE 层却会共享同一组 descriptor key、`UObject` 名字和 replace/reload 入口。 |
| 根因 | ClassGenerator 的内部索引和 `UClass` 物化层仍建立在“短名全局唯一”的旧假设上，而 AngelScript 编译层已经演进到“qualified name 才是 type identity”。两层身份模型没有统一。 |
| 影响 | 这会持续破坏类型注册顺序与热重载一致性。只要项目开始使用 namespaced script type，同短名类型就会互相覆盖 `DataRefByName`、误命中 `FindObject` 的 `ReplacedClass`、共享 interface `asITypeInfo`，并把版本链、`StaticClass()`、`implements` 校验和依赖传播全部变成编译顺序相关的非确定性行为。更重要的是，在不先修这层身份模型前，后续任何类型系统扩展都会继续把新功能建在错误主键上。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 抽出统一的 qualified type identity 子模块，让 descriptor 查找、UE 命名、interface 注册和 reload 路由全部基于同一份 canonical key。 |
| 具体步骤 | 1. 在 `ClassGenerator` 新增显式身份类型，例如 `FASQualifiedTypeKey { FString Namespace; FString Name; ETypeKind Kind; }`，并提供 `ToScriptLookupName()`、`ToUnrealObjectName()`、`ToDisplayName()` 三种稳定转换。 2. 用这份 key 替换 `DataRefByName` 的短名索引；`EnsureClassAnalyzed()`、`GetClassDesc()`、依赖传播和 reload 查找全部改为按 qualified key 工作，不再接受裸短名作为唯一主键。 3. 重写 `GetUnrealName()` / `CreateFullReloadClass()`：`UASClass`、`UASStruct`、delegate 和 interface 的实际 `UObject` 名字必须能区分 namespace，推荐采用稳定 mangling 或按 namespace 拆 outer package，而不是继续复用短名。 4. interface 路径不要再 `RegisterObjectType(InterfaceName)`；需要改成基于 qualified key 的 engine-level type 注册/别名映射，保证 `Foo::IDamageable` 与 `Bar::IDamageable` 拿到不同的 `asITypeInfo`。 5. 为旧项目提供兼容层：在加载 legacy 非 namespace 类型时保留短名 alias，但 alias 只用于诊断和迁移提示，不能再参与 replace/reload 主路径。 6. 把 Issue-17 的 interface cleanup 和现有版本链逻辑接到这份 key 上，让“删除哪个 type”“替换哪个 type”“当前 head 是谁”都不再依赖裸 `FName`。 7. 新增回归：同短名不同 namespace 的 class、interface、`StaticClass()`、`implements`、full reload 和 remove/recreate 六组场景，断言它们彼此隔离且结果稳定。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`; 建议新增 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASTypeIdentity.h/.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/*`; `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/*`; `Plugins/Angelscript/Source/AngelscriptTest/Interface/*`; `Plugins/Angelscript/Source/AngelscriptTest/HotReload/*` |
| 预估工作量 | XL |
| 风险 | 改动 identity key 会影响现有 `UObject` 命名、资产引用、调试显示和历史 reload 路径；如果没有兼容层和迁移测试，极易把现有工程的 class lookup 一次性打断。 |
| 前置依赖 | 建议先完成 Issue-16 和 Issue-17，先把事务/清理边界收紧，再做身份层重构。 |
| 验证方式 | 1. 构造同短名不同 namespace 的 class/interface，确认 `DataRef`、`FindObject`、`StaticClass()` 和 `implements` 全部按 qualified key 隔离。 2. 对其中一个类型做 full reload/remove/recreate，确认另一个 namespace 的类型不会被 rename、替换或接入版本链。 3. 回归无 namespace 的 legacy 脚本，确认兼容 alias 仍可工作且不会与 qualified key 冲突。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-18 | Architecture | 在 Issue-16、Issue-17 稳定后推进；这是后续类型系统扩展和 reload 确定性的底层主键重构 |

---

## 发现与方案 (2026-04-08 13:36)

### Issue-19：`CurrentObjectInitializers` 采用进程级共享栈，脚本对象构造与 async loading thread 并发时会互相污染

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` |
| 行号 | `ASClass.cpp:56-75, 987-988, 1011-1025, 1075-1081, 1140-1171` |
| 问题 | `CheckGameThreadExecution()` 明确承认 defaults 可能运行在 async loading thread，但脚本对象构造态却由进程级 `static TArray<FObjectInitializer> CurrentObjectInitializers` 保存。`AllocScriptObject()` 在 `1075-1081` 把 initializer 压入这个共享数组，`FinishConstructObject()` 和三个静态构造函数随后都用 `CurrentObjectInitializers.Last()` 判断“当前 UObject 是否属于脚本分配路径”。同一文件里 `GASDefaultConstructorOuter` 已经被实现成 `thread_local`，说明这类构造上下文原本就是按线程隔离设计；唯独最关键的 initializer 栈仍是全局共享。 |
| 根因 | `AllocScriptObject()` / `FinishConstructObject()` 复用了 UE 对象构造的两阶段流程，但没有把“当前线程正在完成哪一个 script UObject”的状态接入 `FUObjectThreadContext` 或 `thread_local` 存储，导致并发构造时只能依赖一个跨线程 LIFO 容器。 |
| 影响 | 只要两个线程交错构造 script object，`CurrentObjectInitializers.Last()` 就可能读到别的线程压入的对象。结果包括：错误跳过 `ConstructFunction` / defaults、在错误对象上执行 childmost-defaults 判定、以及把别的线程的 initializer 从栈顶弹走。该问题会直接污染 CDO 初始化、async loading thread 上的 defaults 执行，以及 hot reload 期间的对象重建稳定性。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把脚本对象构造态从进程级共享栈改成线程局部或 `FUObjectThreadContext` 绑定状态，消除跨线程交叉污染。 |
| 具体步骤 | 1. 在 `ASClass.cpp` 引入专用结构，例如 `FASPendingConstructionState`，显式保存 `UObject*`、`FObjectInitializer` 和“是否已到 childmost class”标记。 2. 将 `CurrentObjectInitializers` 从进程级 `static TArray` 改为 `thread_local TArray<FASPendingConstructionState>`，或者直接挂到 `FUObjectThreadContext` 的扩展槽位；要求 `AllocScriptObject()`、`FinishConstructObject()` 和三个静态构造函数统一只访问当前线程的构造栈。 3. 抽出 `PushPendingConstruction()` / `PeekPendingConstruction()` / `PopPendingConstruction()` helper，替代散落的 `CurrentObjectInitializers.Last()` 与 `RemoveAt(...)`，并在 helper 内统一校验“栈顶对象是否等于当前 Object”。 4. 让 `GetConstructingASObject()` 也走同一套 helper，避免它一边从 `FUObjectThreadContext` 取 `TopInitializer()`，另一边脚本构造路径又维护独立全局栈。 5. 补回归：在 test harness 中并发构造两个脚本对象，或在 async loading thread 触发 defaults，再断言每个对象只消费自己的 ctor/defaults，不会互相串栈。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`; `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/*`; `Plugins/Angelscript/Source/AngelscriptTest/HotReload/*` |
| 预估工作量 | M |
| 风险 | 构造态存储方式一旦切换，所有依赖“栈顶就是当前对象”的隐式假设都会暴露出来，需要一并核对 `StaticActorConstructor()` / `StaticComponentConstructor()` / `StaticObjectConstructor()` 与 `GetConstructingASObject()` 的行为是否保持一致。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 构造并发或 async loading thread 场景，确认 `CurrentObjectInitializers` 不再跨线程串用。 2. 对 CDO 和普通实例各执行一轮 script ctor/defaults，确认命中的 pending state 始终是当前对象。 3. 回归 hot reload 与 actor/component 创建测试，确认切换为线程局部状态后没有引入新的构造顺序回退。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-19 | Defect | 在继续收紧 soft reload 事务前优先处理；先让脚本对象构造态具备线程隔离，避免后续任何 ctor/defaults 修复继续建立在共享脏栈上 |

---

## 发现与方案 (2026-04-08 13:36)

### Issue-20：脚本构造在 `asBC_ALLOC` 阶段异常退出时不会清理 pending initializer，后续对象会继承脏构造上下文

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_context.cpp` |
| 行号 | `ASClass.cpp:1075-1081, 1137-1171`; `as_context.cpp:2454-2489, 4117-4134` |
| 问题 | `AllocScriptObject()` 在调用 `ClassConstructor` 前，会把 `FObjectInitializer` 压入 pending 栈；本地代码里唯一的弹栈路径则在 `FinishConstructObject()`。但 AngelScript VM 的 `asBC_ALLOC` 分支在 `CallScriptFunction(f)` 之后，只要 `m_status != asEXECUTION_ACTIVE` 就会直接 `return`，根本不会继续执行稍后的 `asBC_FinConstruct`，也就不会调用 `FinishConstructObject()`。这意味着脚本 ctor 一旦抛异常、prepare 失败或中断执行，`CurrentObjectInitializers` 里的栈顶条目就会永久残留。 |
| 根因 | pending initializer 的生命周期被错误绑定到“字节码一定会走到 `asBC_FinConstruct`”这一乐观前提；`asBC_ALLOC` 的失败退出路径没有任何 abort hook 去通知 `UASClass` 清理半途构造状态。 |
| 影响 | 一次失败构造会把脏 initializer 留在栈顶，之后任意脚本对象构造都可能把这份旧状态当成“当前对象正在脚本分配”。直接后果包括：跳过应执行的 ctor/defaults、对错误对象执行 childmost defaults、在别的对象完成构造时错误弹栈，以及把 hot reload / CDO 初始化拖入持续污染状态。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为脚本对象构造增加显式 abort/rollback 路径，保证 `asBC_ALLOC` 的任何失败出口都能对称清理 pending state。 |
| 具体步骤 | 1. 在 `ASClass.cpp` 提供显式 abort helper，例如 `AbortConstructObject(UObject* Object, asITypeInfo* ScriptType, EASInitAbortReason Reason)`，负责移除匹配的 pending initializer，并按需要销毁半初始化的 script state/debug state。 2. 修改 `as_context.cpp` 的 `asBC_ALLOC` 分支：在 `CallScriptFunction(f)` 返回后，如果 `m_status != asEXECUTION_ACTIVE`，在 `return` 前先调用新的 engine hook，把刚刚分配的 `mem` / `objType` 传回去执行 abort 清理。 3. 将 `FinishConstructObject()` 改成基于对象身份或 token 精确弹栈，而不是默认 `RemoveAt(Num()-1)`；这样 abort 和正常完成可以共享同一套“按对象匹配清理”逻辑。 4. 若 ctor 已经构造了 `asCScriptObject` 外壳但未完成脚本初始化，abort helper 需要补对称清理，至少保证不会留下仍在 GC/析构路径可见的半初始化 script object。 5. 新增异常回归：脚本无参 ctor 主动 `Throw()`、nested script allocation 中内层 ctor 失败、以及 hot reload 重建期间 ctor 失败三组场景，断言 pending state 被清空，后续对象构造不受污染。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_context.cpp`; `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/*`; `Plugins/Angelscript/Source/AngelscriptTest/HotReload/*` |
| 预估工作量 | M |
| 风险 | 改动第三方 VM opcode 路径需要格外谨慎，任何 abort hook 都必须保证不改变成功构造时的既有调用顺序，也不能重复释放已经提交给 UE 生命周期管理的对象。 |
| 前置依赖 | 建议与 Issue-19 一起设计，先统一 pending construction state 的数据结构，再补 abort 清理。 |
| 验证方式 | 1. 让脚本 ctor 抛异常，确认 `asBC_ALLOC` 返回后 pending initializer 已清空。 2. 紧接着再构造第二个对象，确认它不会继承上一次失败构造的 `bIsScriptAllocation` 状态。 3. 回归 hot reload / CDO 初始化失败场景，确认失败对象不会把后续 reload 链路拖入持续脏状态。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-20 | Defect | 紧接 Issue-19 处理；先补失败构造回滚，避免 ctor/defaults 异常把整个构造子系统持续污染 |

---

## 发现与方案 (2026-04-08 13:36)

### Issue-21：`IsDeveloperOnly()` 与编译阶段的 editor-only 模块判定规则不一致，嵌套 `*.Editor.*` 模块会在类生成校验中被误判

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 行号 | `ASClass.cpp:1523-1532`; `AngelscriptEngine.cpp:4354-4356`; `AngelscriptClassGenerator.cpp:5603-5622, 5645-5659` |
| 问题 | 编译阶段已经把 `ModuleName.StartsWith("Editor.") || ModuleName.Contains(".Editor.")` 视为 editor-only 模块；但 `UASClass::IsDeveloperOnly()` 只接受 `Dev.` 和 `Editor.` 前缀，不识别 `Game.Tools.Editor.Visualizers` 这类嵌套 `.Editor.` 名称。`FinalizeActorClass()` 在校验 editor-only `DefaultComponent` attach/root 关系时，直接把 `ASClass->IsDeveloperOnly()` 纳入“actor 是否 editor-only”的判定。结果是同一模块在编译器视角是 editor-only，在类生成校验视角却可能变成 non-editor actor，从而触发错误的 `ScriptCompileError(...)`。 |
| 根因 | “脚本模块是否 editor-only”这条语义在 `AngelscriptEngine.cpp` 和 `ASClass.cpp` 被实现了两套不一致的字符串规则，而且类生成阶段没有复用编译阶段已经确定下来的 module classification。 |
| 影响 | 这会让带 editor-only 默认组件的脚本 actor 在特定模块命名下稳定误报。表现形式不是文档或提示偏差，而是类生成阶段直接失败，进而阻断 CDO 创建、组件布局验证和后续 reload；同一份源码只因模块名包含位置不同就得到不同结果，破坏了类型系统与类生成的一致性。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 提取单一的 module classification helper，并把 editor-only 结论缓存到模块描述上，禁止 `ASClass` 和 class generator 再各自做字符串猜测。 |
| 具体步骤 | 1. 在 `AngelscriptEngine` 或共享 utility 中新增 helper，例如 `IsEditorOnlyModuleName(FStringView ModuleName)`，其规则必须与编译器当前使用的 `StartsWith("Editor.") || Contains(".Editor.")` 完全一致。 2. 在模块创建或编译准备阶段，把这条判定结果落到 `FAngelscriptModuleDesc` / 等价描述结构里，例如 `bIsEditorOnlyModule`，后续所有消费方都只读这个布尔值。 3. 修改 `UASClass::IsDeveloperOnly()`，不要再自行做前缀判断；改成通过 `ScriptTypePtr -> Module -> bIsEditorOnlyModule` 取统一结果，并保留 `Dev.` 模块的显式判断。 4. 修改 `FinalizeActorClass()` 的 attach/root 校验，直接消费共享 helper 或缓存字段，而不是把“actor 是否 editor-only”建立在 `ASClass::IsDeveloperOnly()` 的局部字符串规则上。 5. 新增回归：分别覆盖 `Editor.Foo`、`Game.Tools.Editor.Visualizers`、`Dev.Foo` 和普通 runtime 模块四种命名，断言 editor-only `DefaultComponent` attach/root 校验结果与编译阶段分类一致。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/*`; `Plugins/Angelscript/Source/AngelscriptTest/Component/*` |
| 预估工作量 | S |
| 风险 | 一旦规则统一，部分此前“碰巧通过”的嵌套 editor 模块脚本会改为显式按 editor-only 处理，需要确认这与项目期望一致，并更新相关测试基线。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 构造嵌套 `.Editor.` 模块名脚本，确认编译器与 `IsDeveloperOnly()` 返回值一致。 2. 对 editor-only `DefaultComponent` 作为 attach parent / root 的 actor 做生成回归，确认不再误报 non-editor actor 错误。 3. 回归普通 runtime 模块，确认不会被错误提升为 editor-only。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-21 | Defect | 在 Issue-19、Issue-20 后处理；先统一 editor-only 分类规则，避免类生成校验继续出现命名相关的假失败 |

---

## 发现与方案 (2026-04-08 16:33)

### Issue-22：移除最后一个 `ImplementedInterface` 时，`SoftReloadOnly` 会保留旧 `UClass::Interfaces`（补充 Analysis 20）

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 行号 | `AngelscriptEngine.h:1187-1197`; `AngelscriptClassGenerator.cpp:1319-1322, 2081-2089, 4113-4244, 5060-5161` |
| 问题 | `AreFlagsEqual()` 把 `ImplementedInterfaces` 变化并入“flags changed”分支，分析阶段最多把 `ReloadReq` 提升到 `FullReloadSuggested`。执行阶段 `ShouldFullReload()` 在 soft reload 模式下只检查“新类当前是否仍有 `ImplementedInterfaces`”；当脚本把最后一个接口删掉时，`ImplementedInterfaces.Num()` 变成 `0`，该类会直接走 `DoSoftReload()`。但 `DoSoftReload()` 只重链 property/function 和少量 `ClassFlags`，没有清空或重建 `UClass::Interfaces`；真正 materialize 接口表的逻辑只存在于 `FinalizeClass()` 的 `ImplementedInterfaces` 分支。 |
| 根因 | reload 决策把“接口列表是否发生结构性变化”降格成了建议性 full reload，而 soft reload 生命周期里又没有任何接口表 reset/materialize 步骤。 |
| 影响 | `implements IFoo -> remove IFoo` 在 `SoftReloadOnly` 后，live `UClass` 会继续保留旧 `Interfaces` 项。后续 `ImplementsInterface()`、Blueprint 反射、缺失接口方法校验和依赖传播都会继续把该类当成实现了旧接口，直接造成类型系统与源码脱节。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 `ImplementedInterfaces` 的任意增删改都提升为强制 class reinstancing，并让接口表只由 full reload 路径统一重建。 |
| 具体步骤 | 1. 在分析阶段增加显式比较 helper，例如 `HasImplementedInterfaceLayoutChange(OldClass, NewClass)`；只要接口集合有差异，不论是新增、删除还是重排，都把 `ReloadReq` 提升到 `FullReloadRequired`。 2. 修改 `ShouldFullReload()` 或与 Issue-7 共用的统一 reload 判定 helper，确保 `ReloadReq >= FullReloadRequired` 时绝不进入 `DoSoftReload()`；即使 `ImplementedInterfaces.Num() == 0` 也必须走 `CreateFullReloadClass()` / `DoFullReloadClass()` 或稳定拒绝 swap-in。 3. 在 `FinalizeClass()` 开始 materialize 接口前，先 `NewClass->Interfaces.Reset()`，再根据 `ImplementedInterfaces` 重新构建接口图，避免未来其它 full reload 分支继续累积 stale interface entry。 4. 给 soft reload 路径补 defensive assert/log：若当前类的 old/new `ImplementedInterfaces` 不一致却仍进入 `DoSoftReload()`，立即标记 `bModuleSwapInError` 并输出类名、旧接口集、新接口集。 5. 新增回归：`class Foo : UObject, IFoo` 首次编译成功后，在 `SoftReloadOnly` 下删除 `IFoo`，断言 reload 不会复用旧类对象，且 `Foo->ImplementsInterface(UFoo::StaticClass())` 为 false。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`; `Plugins/Angelscript/Source/AngelscriptTest/HotReload/*`; `Plugins/Angelscript/Source/AngelscriptTest/Interface/*` |
| 预估工作量 | M |
| 风险 | 把接口列表变更从“建议 full reload”收紧成“强制 reinstancing”后，会让一部分原本侥幸能走 `SoftReloadOnly` 的工作流改为 full reload 或显式失败，需要同步评估编辑器内的迭代成本。 |
| 前置依赖 | 建议与 Issue-7 统一到同一个 reload decision helper，避免再次出现“分析要求 full reload，执行层仍走 soft reload”的分叉。 |
| 验证方式 | 1. 回归 `remove last interface` 场景，确认 `DoSoftReload()` 不再被调用。 2. reload 后检查 `UClass::Interfaces.Num()` 与源码声明一致，`ImplementsInterface()` 返回值同步更新。 3. 对删除接口前后各执行一次 Blueprint/反射查询，确认接口列表不会残留旧条目。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-22 | Defect | 与 Issue-7 联动优先处理；先封住接口删改仍走 soft reload 的分叉，避免 live `UClass::Interfaces` 继续滞留旧依赖 |

---

## 发现与方案 (2026-04-08 16:36)

### Issue-23：`Config=<Name>` / `DefaultConfig` 变更不会被正确重载，live `UClass` 会持续保留旧配置语义（补充 Analysis 13/40）

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 行号 | `AngelscriptEngine.h:1112, 1187-1197`; `AngelscriptClassGenerator.cpp:1319-1322, 3294-3307, 4209-4239` |
| 问题 | `FAngelscriptClassDesc` 明确保存了 `ConfigName`，但 `AreFlagsEqual()` 只比较若干布尔 class flags 和 `ImplementedInterfaces`，没有比较 `ConfigName`，因此 `Config=Game -> Config=Engine` 这类变化不会被视为结构性差异。真正把 `CLASS_Config`、`ClassConfigName` 和 `CLASS_DefaultConfig` 写进 `UClass` 的代码只存在于 full class creation 路径；`DoSoftReload()` 只回放 `NotPlaceable`、`Abstract`、`Transient`、`HideDropDown`、`DefaultToInstanced`、`EditInlineNew`、`Deprecated`，完全不处理 config 相关状态。 |
| 根因 | 类级配置语义被拆成了两半：diff 判定层不知道 `ConfigName` / `DefaultConfig` 是 reload-sensitive 状态，而 soft reload 执行层也没有统一的 “apply class config semantics” 步骤。 |
| 影响 | 一旦脚本类切换配置节名或增删 `DefaultConfig`，热重载后的 live `UClass` 仍会保留旧 `ClassConfigName` 和旧 flag 组合。结果不是单纯 metadata 漂移，而是 CDO/config 加载仍从旧 ini section 读取默认值，直接制造默认对象状态不一致，并让后续保存/反序列化继续沿用错误的配置语义。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 将 class config 视为强结构语义，任何 `ConfigName` / `DefaultConfig` 变化都必须触发 full reload，并把配置写入逻辑收束成单一路径。 |
| 具体步骤 | 1. 在 `FAngelscriptClassDesc` diff 阶段新增显式比较 helper，例如 `HasClassConfigSemanticChange(OldClass, NewClass)`，覆盖 `ConfigName` 和 `Meta.Contains(DefaultConfig)`；命中时直接把 `ReloadReq` 提升到 `FullReloadRequired`。 2. 修改 `ShouldFullReload()` 或统一 reload decision helper，确保这类变更绝不落入 `DoSoftReload()`；若当前会话强制 `SoftReloadOnly`，则应稳定拒绝 swap-in，而不是带着旧 config 语义继续运行。 3. 从 `CreateFullReloadClass()` 提取公共 helper，例如 `ApplyClassConfigSettings(UClass* Class, const FAngelscriptClassDesc& Desc, UClass* SuperClass)`，统一负责 `CLASS_Config`、`ClassConfigName`、`CLASS_DefaultConfig` 的写入和清理，避免未来其它路径再次漏同步。 4. 在 helper 中显式处理“从有 config 变成无 config”与“从无 `DefaultConfig` 变成有 `DefaultConfig`”这两种反向变更，不能只覆盖设置分支。 5. 新增回归：`Config=Game` 改成 `Config=Engine`、增加/删除 `DefaultConfig`、以及父类带 config 而子类切换回继承父配置三组场景，断言新 `UClass->ClassConfigName`、`CLASS_Config`、`CLASS_DefaultConfig` 与源码一致，且 CDO 从正确 ini section 取值。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`; `Plugins/Angelscript/Source/AngelscriptTest/HotReload/*`; `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/*` |
| 预估工作量 | M |
| 风险 | 把 config 语义变更升级为强制 full reload 后，会让一部分过去“能热更但状态错误”的脚本改为显式重建；需要确认这不会意外打断编辑器里的频繁调参工作流。 |
| 前置依赖 | 建议与 Issue-7 共用同一个 reload decision helper，避免 `ReloadReq` 与执行路径再次分叉。 |
| 验证方式 | 1. 变更 `ConfigName` 后检查 `ClassConfigName` 和 class flags，确认不会保留旧值。 2. 比较 reload 前后 CDO 从 ini 读取的默认值来源，确认命中了新的 config section。 3. 在 `SoftReloadOnly` 场景下触发该变更，确认系统要么稳定转 full reload，要么明确拒绝 swap-in，而不是静默留下旧配置语义。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-23 | Defect | 紧接 Issue-22 处理；先把 class config 语义纳入强结构重载，避免 CDO 与 ini 默认值继续错位 |

---

## 发现与方案 (2026-04-08 16:40)

### Issue-24：`ComposeOntoClass` 已暴露到描述层和序列化层，但类生成阶段没有任何消费闭环，当前实现是稳定的 silent no-op（补充 Analysis 62）

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp` |
| 行号 | `AngelscriptEngine.h:1142`; `ASClass.h:27`; `AngelscriptClassGenerator.cpp:1336-1364, 5052-5056`; `PrecompiledData.cpp:840, 2846-2847` |
| 问题 | `FAngelscriptClassDesc` 持有 `ComposeOntoClass`，`Analyze()` 还会在该字段非空时对 `ComposedStruct` metadata 做额外校验；`FinalizeClass()` 则仅在找到目标描述时把 `UASClass::ComposeOntoClass` 赋值为目标 `UClass`。与此同时，对 `Plugins/Angelscript/Source` 做 `rg -n \"ComposeOntoClass\"`，命中点只剩声明、校验、赋值和 `StaticJIT` 的序列化/反序列化，没有任何运行时消费者会读取这个字段参与 property/function merge、CDO 构造或实例化流程。 |
| 根因 | `ComposeOntoClass` 只完成了“语法接受 + 描述保存 + 序列化恢复”，没有定义 class generation 层的真实 contract，也没有建立与之配套的 materialization pipeline。 |
| 影响 | 当前行为会把一个看似已支持的扩展点暴露给脚本作者，但运行时完全没有 compose 效果；目标类名写错时还会静默退化成 `nullptr`。这种 silent no-op 会直接破坏动态 `UClass` 生成的可预测性，也让后续围绕 compose 的类型系统扩展失去稳定落点。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 先把未实现特性从 silent no-op 收紧成显式失败，再把 composition 抽成独立子模块，定义清晰的生成契约。 |
| 具体步骤 | 1. 立即加安全闸门：若 `ComposeOntoClass` 非空但当前版本尚未完成 materialization 支持，则在 `Analyze()` 或 `FinalizeClass()` 里直接 `ScriptCompileError(...)`，禁止继续生成一个表面成功、实际无效的类。 2. 新增独立模块，例如 `ASComposePlan.h/.cpp`，显式定义 composition contract：哪些成员会被合成，冲突如何解析，metadata 如何继承，CDO/defaults 如何覆盖，hot reload 时 compose target 变化是否强制 full reload。 3. 在 `FinalizeClass()` 前构建 `FASComposePlan`，把 target class 的 property/function/defaults 合成结果显式展开到当前 `FAngelscriptClassDesc` 或中间 plan，而不是只保留一个裸 `UClass* ComposeOntoClass`。 4. 让 `CreateFullReloadClass()`、`DoFullReload()`、`InitDefaultObjects()` 和相关验证逻辑统一消费这份 compose plan，保证编译期校验、类物化、CDO 构造和 hot reload 使用的是同一套合成结果。 5. 为找不到 compose target、target 不是合法 compose 源、成员冲突、以及 compose target 发生 reload/remove 的场景补完整回归，确保 compose 语义不会再次退化成“字段存在但没人用”。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; 建议新增 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASComposePlan.h/.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`; `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/*` |
| 预估工作量 | L |
| 风险 | 一旦把 silent no-op 改成显式错误，现有脚本里所有依赖 `ComposeOntoClass` 的声明都会立刻暴露出来；如果没有分阶段迁移方案，短期内可能增加编译失败数量。 |
| 前置依赖 | 无；但若后续推进真正实现，建议先明确 compose 与 `ComposedStruct`、`DefaultComponent`、hot reload 版本链之间的契约边界。 |
| 验证方式 | 1. 在当前未实现阶段，声明 `ComposeOntoClass` 的脚本必须稳定报错，不再静默成功。 2. 若实现 compose plan，再新增正向测试验证 composed property/function/defaults 会真实反映到生成的 `UClass`/CDO。 3. 对 compose target rename/remove/reload 做回归，确认生成结果与错误诊断都稳定可预测。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-24 | Architecture | 在 Issue-22、Issue-23 收紧热重载边界后推进；先禁止 silent no-op，再决定 composition 子模块的正式 contract |

---

## 发现与方案 (2026-04-08 23:46)

### Issue-25：依赖传播和重载执行都只覆盖 `CompiledModules`，跨模块消费者会长期保留旧 `UClass` / interface 图

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 行号 | `AngelscriptEngine.cpp:119-127, 3896-3907`; `AngelscriptClassGenerator.cpp:85-96, 126-156, 1928-2042, 2145-2293` |
| 问题 | reload 入口只把本轮 `CompiledModules` 传给 `FAngelscriptClassGenerator`，`AddModule()` 也只把这些模块写进内部 `Modules`。随后 `EnsureClassAnalyzed()` / `GetClassDesc()` 对不在当前批次的类型直接回退到 `FAngelscriptEngine::Get().GetClass(...)`，依赖传播也只会从 `DataRefByNewScriptType` 命中当前批次里的 type。最终真正的 create/reload/finalize 循环同样只遍历 `Modules`。这意味着一旦某个脚本 interface、脚本父类或被其他模块作为 property / return / parameter 使用的类型在模块 A 中发生 full reload，模块 B 里未参与本轮编译的消费者类不会被纳入依赖闭包，也不会重建自己的 `SuperStruct`、`Interfaces` 或相关 `FProperty`/`UFunction` 外壳。 |
| 根因 | ClassGenerator 的依赖图是“本轮编译批次内的局部图”，没有基于 `ActiveModules` 维护全局 reverse-dependency 索引，也没有在 swap-in 前计算跨模块受影响集合。 |
| 影响 | 这会把类型系统结果变成编译批次顺序相关。直接后果包括：旧实现类继续持有过期的 `UClass::Interfaces`，`QuickScriptInterfaceCast` 之类依赖 `ObjectClass->ImplementsInterface(TargetClass)` 的路径得到过期结果；脚本子类继续挂在旧父类 `UClass` 上，CDO、默认值和反射关系直到手工重编消费者模块前都不会收敛到新 head。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把依赖收敛范围从“当前编译模块”提升为“全局受影响模块闭包”，让 type producer 的 reload 能主动拉起所有 script consumer。 |
| 具体步骤 | 1. 在 `FAngelscriptEngine` 层新增全局依赖索引，按 qualified type key 记录每个模块对哪些 script superclass、implemented interface、property type、function return/argument type、`ComposeOntoClass` 的消费关系。 2. 在 `CompiledModules` 进入 `ClassGenerator` 前，根据“本轮发生结构变化的类型集合”在 `ActiveModules` 上做 reverse traversal，求出受影响模块闭包；这些模块即使源码未改，也必须以 reload unit 形式加入 `ClassGenerator`。 3. 让 `AddModule()` 支持两类输入：源码刚编译出的新模块，以及为依赖收敛而补入的旧模块描述；后者至少需要重新跑 `SetupModule()`、依赖传播和对应 class 的 full reload / finalize。 4. 对 interface 和 superclass 变化增加硬规则：任何跨模块消费者命中后都强制 class reinstancing，禁止继续沿用旧 `Interfaces` / `SuperStruct`。 5. 在 swap-in 收尾增加诊断：若某个已 full reload 的 type 仍存在不在本轮处理集合内的 live script consumer，立即输出模块名、类型名和 consumer 列表，并把本轮标成 `bModuleSwapInError`。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`; `Plugins/Angelscript/Source/AngelscriptTest/HotReload/*`; `Plugins/Angelscript/Source/AngelscriptTest/Interface/*` |
| 预估工作量 | L |
| 风险 | 把依赖闭包真正算出来后，一次局部脚本修改可能拉起更多模块 reinstance，编辑器内 reload 时长会上升；需要通过缓存依赖索引和精确 type key 降低额外成本。 |
| 前置依赖 | 建议与 Issue-18 的 qualified type key 方案协同，否则跨模块索引仍会受短名冲突污染。 |
| 验证方式 | 1. 构造两个脚本模块：A 定义 interface / base class，B 定义实现类或派生类；只重编 A，确认 B 也会被纳入 reload。 2. reload 后对 B 执行 `ImplementsInterface()`、`GetSuperClass()`、CDO 默认值读取，确认全部指向新 head。 3. 在 `SoftReloadOnly` 和 full reload 两种模式下回归，确认不会再出现“producer 已更新、consumer 仍旧”的跨模块分叉。 |

### Issue-26：`ResolveCodeSuperForProperty()` 沿 script 继承链上溯时没有更新当前节点，热重载后会把脚本组件误判成非 `UActorComponent`

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 行号 | `AngelscriptClassGenerator.cpp:382-401, 3165-3189` |
| 问题 | `Analyze()` 在校验 `UPROPERTY(DefaultComponent)` 时调用 `ResolveCodeSuperForProperty(PropertyType)` 来确认属性类型最终是否继承自 `UActorComponent`。但 helper 在 `Usage.GetClass()` 返回 live script `UClass` 的分支里只在进入循环前做了一次 `UASClass* asClass = Cast<UASClass>(ClassOfProperty)`，随后执行 `while (ClassOfProperty != nullptr && asClass->bIsScriptClass) ClassOfProperty = ClassOfProperty->GetSuperClass();`。由于 `asClass` 从未随着 `ClassOfProperty` 更新，条件会一直沿用“初始 script class 的 `bIsScriptClass == true`”，把继承链一路走到 `nullptr`，即使中间已经到达 `USceneComponent` / `UActorComponent` 这样的 native 父类。初次编译时这个问题常被 `GetDataFor(Usage.ScriptClass)` 的 fallback 掩盖；一旦类型已有 live `UClass`，后续分析或热重载就会命中错误分支。 |
| 根因 | helper 混用了“当前遍历节点”与“初始 script class 节点”两个概念，循环条件绑定到了陈旧的 `asClass`，没有在每次上溯后重新判定当前 `UClass` 是否还是 script class。 |
| 影响 | 这会把本来合法的 script component `DefaultComponent` 声明在第二次编译或热重载时误报成“does not derive from UActorComponent”，导致 actor 类生成被阻断。由于问题取决于 `Usage.GetClass()` 是否已经拿到 live `UClass`，表现会带明显的编译顺序和是否曾经成功生成过该组件的时序敏感性。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 重写 code-super 解析逻辑，让“沿 script 继承链找到第一个 native super”始终基于当前节点重新判定。 |
| 具体步骤 | 1. 将 `ResolveCodeSuperForProperty()` 改为显式游标循环，例如 `for (UClass* Current = Usage.GetClass(); Current != nullptr; Current = Current->GetSuperClass())`，每一步重新 `Cast<UASClass>(Current)`；一旦当前节点不是 script class，就立即返回它。 2. 若 `Usage.ScriptClass` 对应的是本轮正在 reload 的 class，优先走 `GetDataFor(Usage.ScriptClass)` 返回的 `ClassData->NewClass->CodeSuperClass`，不要先信任旧 `UserData` 指向的 live `UClass`。 3. 为 helper 增加 defensive check：若 script class 上溯到 `nullptr` 仍未找到 native super，立刻输出包含类型名和继承链的编译错误，而不是让调用点把它静默当成“不是 component”。 4. 把 `DefaultComponent` / `OverrideComponent` 相关 ancestry 校验统一收口到这个修复后的 helper，避免未来再出现各自手写的脚本父类剥离逻辑。 5. 新增回归：先编译一个 script `USceneComponent`，再让另一个 actor 以它作为 `DefaultComponent`；随后分别只重编 actor、只重编 component、以及同时重编两者，断言三种场景都不会误报非 `UActorComponent`。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptTest/Component/AngelscriptComponentScenarioTests.cpp`; `Plugins/Angelscript/Source/AngelscriptTest/HotReload/*` |
| 预估工作量 | S |
| 风险 | 修改 helper 后，之前被错误挡住的 script component `DefaultComponent` 声明会开始真正进入后续 finalize / verify 链路，可能连带暴露组件树里的其它既有问题。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 回归现有 `DefaultComponent` happy path，并新增“先成功生成再热重载”的用例。 2. 对 script `USceneComponent` 和 script `UActorComponent` 两种父类都验证一次，确认 helper 返回的是正确 native super。 3. 在 editor 中执行重复热重载，确认错误不再依赖“该类型是否已经有 live `UClass`”。 |

### Issue-27：literal asset 的 `__Init_*` 用户代码仍在 `InitDefaultObjects()` 之前执行，类初始化顺序可被用户脚本提前打断（补充 Analysis 44）

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 行号 | `AngelscriptPreprocessor.cpp:4111-4133`; `AngelscriptClassGenerator.cpp:2302-2304, 5775-5843` |
| 问题 | 预处理器把 literal asset 展开成 `Get{Name}()` getter，getter 内部会立即执行用户实现的 `__Init_{Name}` 与 `__PostLiteralAssetSetup(...)`，并把 `Get{Name}` 登记到 `PostInitFunctions`。reload 主流程随后先执行 `CallPostInitFunctions()`，再执行 `InitDefaultObjects()`。而 tick 预计算和 CDO 创建都发生在 `InitDefaultObjects()` 内部。结果是任意 `__Init_*` 用户脚本都可以在“tick 设置尚未收敛、CDO 尚未统一创建”的时间点运行，并通过 `StaticClass()`、`GetDefaultObject()`、对象实例化或默认值读取把半初始化类状态观察出来甚至固化下来。 |
| 根因 | 管线把 literal asset 初始化视为“默认对象初始化之前的安全准备步骤”，但实际执行的是用户可编写的任意脚本逻辑，不是纯资源 materialization。 |
| 影响 | 这会直接制造 CDO 状态不一致和顺序敏感的热重载问题。某些 asset 初始化代码如果依赖最终 `bCanEverTick`、父类 CDO、默认组件树或 `StaticClass()` 返回的最终 head，它拿到的会是 pre-`InitDefaultObjects()` 的半成品状态；之后即使类继续完成初始化，asset 已经把旧结果缓存下来。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 literal asset 流程拆成“纯对象落地”和“用户脚本初始化”两个阶段，严格禁止用户脚本在 CDO/tick 屏障之前运行。 |
| 具体步骤 | 1. 将当前 `CallPostInitFunctions()` 语义拆分成两个阶段：phase A 只做无需脚本回调的 asset shell 创建；phase B 在 `InitDefaultObjects()` 和最终 class verification 之后，再执行用户 `__Init_*` / `__PostLiteralAssetSetup`。 2. 如果某些 literal asset 必须在 defaults 阶段可见，则为它们提供受限的“CDO-safe materialize” API，只允许 native 赋值，不允许 `StaticClass()`、`GetDefaultObject()`、`new` script object 或其它会提前触发类初始化的脚本操作。 3. 在 `CallPostInitFunctions()` 调度结构里记录阶段要求，禁止普通用户 getter 被插入 pre-CDO 阶段；命中违规时直接 `ScriptCompileError(...)` 并标记 `bModuleSwapInError`。 4. 与 Issue-5 联动，把 phase B 的执行结果也纳入事务控制；只要 post-init 任一 getter 失败，本轮不提交缓存、不继续后续 asset 初始化。 5. 新增回归：literal asset 的 `__Init_*` 主动读取某个 script actor 的 `StaticClass()`、`GetDefaultObject()` 和 tick 标志，断言它只能在 `InitDefaultObjects()` 完成后运行，且观察到的是最终稳定状态。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`; `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/*`; `Plugins/Angelscript/Source/AngelscriptTest/HotReload/*` |
| 预估工作量 | M |
| 风险 | 把用户 `__Init_*` 推迟到 CDO barrier 之后，可能改变少数脚本目前依赖的“asset 在 defaults 前就可执行初始化”的时序，需要提供迁移说明或保留受限的 pre-CDO API。 |
| 前置依赖 | 建议与 Issue-5 协同实施，共用 literal asset 的事务控制和失败回滚。 |
| 验证方式 | 1. 构造在 `__Init_*` 中访问 script class CDO/tick 状态的 literal asset，用例必须只在 barrier 之后成功运行。 2. 比较调整前后 asset 缓存中的默认值来源，确认不再读取半初始化类状态。 3. 回归普通 literal asset 场景，确认纯资源创建仍然成功，且不会额外触发早期 CDO materialization。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-25 | Architecture | 先立项；不补全跨模块依赖闭包，后续任何热重载修复都仍会留下旧 consumer |
| P1 | Issue-26 | Defect | 紧随其后；这是 `DefaultComponent` 热重载路径上的直接错误判断，修复成本低且收益立刻可见 |
| P1 | Issue-27 | Defect | 与 Issue-5 配对推进；先把 literal asset 的用户脚本移出 pre-CDO 阶段，收敛 CDO 状态分叉 |
---

## 发现与方案 (2026-04-09 00:02)

### Issue-28：script struct value-type 依赖没有进入 reload 图，struct/class 会按源码顺序重建并继续挂旧 UScriptStruct

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp |
| 行号 | 1729-1750, 2001-2023, 2206-2214, 2916-2918, 4113-4174 |
| 问题 | 已验证事实有五处。其一，SetupModule() 只是按 NewModule->Classes 的出现顺序把 class/struct 填进 ModuleData.Classes，没有建立 struct-level 拓扑序。其二，PerformReload() 虽然把“所有 struct 先于 class”作为大阶段处理，但 2206-2214 仍只是按 Modules -> Classes 顺序线性执行 DoFullReload(...)。其三，PropagateReloadRequirements() 对属性、返回值和参数只在 IsObject() 为真时才加依赖，因此 script struct 这类 value type 不会把 consumer 标记成依赖方。其四，AddClassProperties() 也只在 ScriptProp->type.IsObject() && !IsReferenceType() 时 EnsureReloaded(...)，对 value-type struct 字段不会等待 provider struct 先重建。其五，DoSoftReload() 复用旧 FProperty 外壳时只是重新 Link()，没有任何一步会把已有 FStructProperty 指向的新 UScriptStruct 重新绑定。基于这组代码可推断：只要 StructB 布局变化，而 StructA/ClassC 通过 value field 持有 StructB，consumer 很可能不会被纳入 reload 闭包；即使落入 soft reload，也会继续沿用旧 property shell。 |
| 根因 | reload 依赖图和属性重建逻辑都把“需要等待的类型”近似成了 IsObject()，但 Angelscript 的 script struct 是 value type。结果是 provider struct 的结构变化既不会传播到外层 struct/class，也不会在 property materialization 时建立强制的“先 reload 依赖，再创建/复用 property”顺序。 |
| 影响 | 这会直接破坏类型注册顺序与 CDO/实例 ABI 一致性。外层 FStructProperty 可能继续指向旧 UASStruct，soft reload consumer 也可能保留旧 offset/旧序列化语义；一旦内层 struct size 或成员布局发生变化，后续默认值复制、序列化、GC schema 和实例重建都会在新旧布局之间错位，表现为热重载后随机默认值漂移、属性读写错误，严重时可退化成内存解释错误。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 script struct value-type 边纳入统一 reload dependency graph，并在 property materialization 前显式等待所有 struct 依赖完成重建。 |
| 具体步骤 | 1. 在 AngelscriptClassGenerator.cpp 提取统一 helper，例如 AddReloadDependencyFromUsage(FReloadPropagation*, const FAngelscriptTypeUsage&)，不要再用 IsObject() 作为唯一门槛；只要 Usage 最终指向本轮 GetDataFor(...) 能识别的 script class / script struct / script delegate，就把它纳入依赖图。 2. 将 PropagateReloadRequirements() 中对 ObjType->localProperties、函数返回值、参数的扫描全部改为走 FAngelscriptTypeUsage 层，而不是只看 sCDataType::IsObject()；这样 StructA { StructB Value; }、rray<StructB>、map<int, StructB> 都能把 StructB 的 reload requirement 向外传播。 3. 在 AddClassProperties() 前增加 EnsureTypeDependenciesReloaded(PropertyType)，对 property 使用的 script struct/value type 先执行 EnsureReloaded(TypeId)；DoFullReloadStruct() 和 DoFullReloadClass() 都必须走这条屏障，保证创建 FStructProperty 时拿到的是最新 UASStruct。 4. 对 DoSoftReload() 增加硬门槛：若任一属性或函数签名使用的 script struct 在本轮发生 FullReloadRequired，当前 consumer 不得继续 soft reload；要么升级成 full reload，要么在 SoftReloadOnly 会话里明确拒绝 swap-in。 5. 新增回归：StructA 内含 StructB，ClassC 内含 StructA；修改 StructB 的字段布局后分别执行 full reload 与 SoftReloadOnly，断言 StructA/ClassC 都被拉进受影响闭包，且最终 FStructProperty->Struct、PropertiesSize 和默认值读写都指向最新 head。 |
| 涉及文件 | Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp; Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h; Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp; Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/* |
| 预估工作量 | M |
| 风险 | 把 value-type 依赖真正接入后，一次 struct 变更会拉起更多 consumer full reload，编辑器内 reload 时长会增加；需要靠精确的 type-usage 遍历与闭包去重避免过度扩散。 |
| 前置依赖 | 建议与现有 reload decision helper 收口工作一起做，避免新依赖图算出了 FullReloadRequired，执行层却仍落回 DoSoftReload()。 |
| 验证方式 | 1. 构造 nested script struct 用例，确认修改内层 struct 后外层 struct/class 不会继续留在 soft reload。 2. reload 后直接检查 consumer property 的 FStructProperty->Struct 是否等于最新 UASStruct。 3. 对带该 struct 的 CDO 与 live instance 读写字段，确认默认值、序列化和热重载后的值迁移不再出现新旧布局错位。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-28 | Defect | 立即立项；这是 value-type 依赖缺边导致的结构性重载错误，会直接把 struct ABI 错配带进外层 class/CDO |
---

## 发现与方案 (2026-04-09 00:03)

### Issue-29：script interface 的 UE ABI 被压扁成“只有函数名”，实现校验和运行时 dispatch 都不看签名

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp; Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h |
| 行号 | AngelscriptClassGenerator.cpp:56-67, 2762-2831, 5161-5184; AngelscriptEngine.h:59-62 |
| 问题 | 已验证事实有四处。其一，FInterfaceMethodSignature 只保存 FName FunctionName。其二，CallInterfaceMethod() 运行时只用这个名字做 Object->FindFunction(Sig->FunctionName)，没有任何参数表或返回值比对。其三，interface full reload 分支在 2803-2830 明确采用“For now, create a minimal UFunction with just the name”的实现，只创建同名 UFunction stub，不生成返回值和参数 FProperty。其四，FinalizeClass() 对实现类的校验也只是 FindFunctionByName(InterfaceFunc->GetFName())，只要找到同名函数就算实现成功。基于这组代码可推断：即使脚本源码声明的是 oid Foo(int32 Value)，UE 侧生成出来的 interface UFunction 仍可能是零参数 stub；而实现类若存在另一个同名但签名不同的函数，也会被误判成“已经实现接口”。 |
| 根因 | interface 路径没有复用普通类方法的 UFunction 生成与签名校验流程，而是把 contract 简化成“存在一个同名入口即可”。这导致预处理阶段已经握有的完整 MethodDecl 信息，在 UClass materialization、实现校验和 generic dispatch 三个阶段都被降格成裸 FName。 |
| 影响 | 这会直接破坏 interface 的反射 ABI。Blueprint/反射层看到的是错误签名的 interface UFunction，实现类的签名错配不会在编译期被拦截，运行时 generic dispatch 也可能把调用路由到同名但不同 ABI 的函数上，最终表现为参数封送错误、返回值语义错误，甚至把错误实现静默当成合法实现。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 interface contract 从“函数名”升级为“结构化签名”，并让生成、校验、dispatch 共享同一份 signature source of truth。 |
| 具体步骤 | 1. 在预处理/描述层新增结构化类型，例如 FAngelscriptInterfaceMethodDesc，显式保存返回类型、参数列表、限定符和原始 MethodDecl；不要再把 interface 方法只保存在字符串数组里。 2. 重写 DoFullReload() 的 interface 分支，复用普通类方法的 UFunction 生成路径，为 interface 方法创建真实的返回值与参数 FProperty，而不是继续生成 name-only stub。 3. 扩展 FInterfaceMethodSignature，至少保存可稳定比较的完整签名 key，例如规范化 declaration 或独立的参数/返回值描述；CallInterfaceMethod() 改成先按名字候选，再按完整签名解析真实目标 UFunction。 4. 修改 FinalizeClass() 的 interface 实现校验，新增 InterfaceSignatureMatches(UFunction* Impl, UFunction* InterfaceFunc)，对参数数量、参数类型、返回值类型和 const/BlueprintEvent 语义做逐项比较；只按名字命中的函数不再视为合法实现。 5. 新增回归：interface ITest { void Foo(int32); } + 实现类只提供 oid Foo() 必须编译失败；同时验证生成后的 interface UFunction 在 UE 反射层能正确看到参数和返回值。 |
| 涉及文件 | Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp; Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h; Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h; Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp; Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp; Plugins/Angelscript/Source/AngelscriptTest/Interface/* |
| 预估工作量 | M |
| 风险 | 一旦开启严格签名校验，现有脚本里所有“同名但签名不完全一致”的 interface 实现都会在编译期暴露出来；需要准备迁移说明，避免把历史宽松行为当成兼容回归。 |
| 前置依赖 | 无；但建议与 interface graph materialization 修复协同验证，确保传递接口和签名校验使用同一份 interface UFunction 数据。 |
| 验证方式 | 1. 对 interface 生成后的 UFunction 直接检查 NumParms、ParmsSize、返回值属性，确认与源码声明一致。 2. 构造同名不同签名的实现类，确认编译期稳定报错，而不是在运行时才失真。 3. 对 generic interface call 路径做自动化，确认 CallInterfaceMethod() 最终调到的是 ABI 正确的实现函数。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-29 | Defect | 与 interface graph 问题并行处理；先修签名 contract，避免错误 ABI 继续被反射层和 dispatch 路径放大 |
---

## 发现与方案 (2026-04-09 00:04)

### Issue-30：GConstructASObjectWithoutDefaults 是进程级共享开关，PrepareSoftReload() 会把“只给临时 CDO 关 defaults”扩散成全局状态污染

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp; Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp |
| 行号 | ASClass.cpp:53-80, 987-988, 1360-1361, 1416-1417, 1468-1469; AngelscriptClassGenerator.cpp:4094-4100 |
| 问题 | 已验证事实有三处。其一，ASClass.cpp 把 GConstructASObjectWithoutDefaults 定义成进程级全局 bool，而不是 	hread_local 或按对象绑定的状态。其二，PrepareSoftReload() 在创建 CDONoDefaults 前直接把这个全局 bool 置为 	rue，随后立即通过 NewObject 进入正常对象构造链。其三，actor/component/object 三条静态构造函数都无条件读取这个共享开关决定是否执行 defaults，然后立刻把它清回 alse。同一文件的 CheckGameThreadExecution() 还明确说明 defaults 允许在 async loading thread 执行。基于这组代码可推断：soft reload 期间只要有别的 script object 在同一时间进入任一静态构造函数，它就可能误读这个“只想作用于临时 CDO”的全局开关，并错误跳过自己的 defaults。 |
| 根因 | “为 CDONoDefaults 临时禁用 defaults”被实现成了进程级共享状态，而不是一个具备线程边界和目标对象边界的局部 scope。构造系统里已经存在 	hread_local 状态（例如 GASDefaultConstructorOuter），但 defaults 抑制逻辑没有沿用同样的隔离模型。 |
| 影响 | 这会把本该局限于 soft reload 基线构造的行为泄漏到无关对象上，直接制造 CDO 和普通实例状态不一致。表现不仅是 CDONoDefaults 基线失真，还包括 async loading thread 或嵌套构造期间的 script actor/component/object 被错误地跳过 defaults，最终让热重载后的状态问题呈现明显的时序敏感和线程敏感。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 用“线程局部 + 目标对象精确匹配”的抑制 scope 取代进程级布尔开关，确保只有当前那一个临时 CDONoDefaults 对象会跳过 defaults。 |
| 具体步骤 | 1. 在 ASClass.cpp 引入显式状态结构，例如 	hread_local TArray<UObject*> GDefaultsSuppressionStack 或 	hread_local UObject* GSuppressedDefaultsObject，并提供 RAII helper FASDefaultsSuppressionScope(UObject* Target)。 2. 修改 PrepareSoftReload()：不要直接写全局 bool；改成在创建 CDONoDefaults 前进入 suppression scope，并把目标对象限定为这次 NewObject 正在构造的临时对象。 3. 修改 StaticActorConstructor()、StaticComponentConstructor()、StaticObjectConstructor()：ApplyDefaults 的判定必须同时满足“当前线程存在 suppression scope 且 Initializer.GetObj() 正是被抑制的那个对象”才会跳过 defaults，随后由 RAII 自动出 scope，不再手工写回全局 false。 4. 把 CDONoDefaults 的二次 DestructScriptObject() / ReinitializeScriptObject() 也接到同一套 scope 上，避免这条辅助重建路径再次通过共享状态影响其它对象。 5. 新增回归：在 soft reload 构造 CDONoDefaults 的同时触发另一个 script object 构造，断言只有临时 CDO 跳过 defaults，普通对象和其它线程对象仍执行自己的 defaults。 |
| 涉及文件 | Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp; Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h; Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp; Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp; Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/ClassGeneratorTests.cpp |
| 预估工作量 | M |
| 风险 | 引入线程局部 suppression scope 后，现有代码里任何依赖“写一次全局 bool 就能影响后续嵌套构造”的隐式行为都会被收紧，需要确认 CDONoDefaults 路径之外没有隐藏调用方依赖这种副作用。 |
| 前置依赖 | 建议与 defaults/soft-reload 决策修复一起评估，避免还没解决“哪些场景允许 soft reload”之前，就继续扩大 CDONoDefaults 的使用面。 |
| 验证方式 | 1. 在带日志的测试里同时构造 CDONoDefaults 和普通 script object，确认只有目标对象命中 suppression。 2. 对 async loading thread 或模拟并发构造场景回归，确认不会再出现 unrelated object 跳过 defaults。 3. reload 后比较普通实例与 CDO 的 defaults 应用结果，确认不再出现由共享开关造成的随机漂移。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-30 | Defect | 与 CDO 基线修复一并推进；先把 defaults 抑制从全局状态收口，否则任何 soft reload 诊断都可能继续被线程/时序噪音污染 |
---

## 发现与方案 (2026-04-09 00:05)

### 补充说明：Issue-28 ~ Issue-30 内联术语勘误

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Refactoring |
| 严重度 | Low |
| 文件 | Documents/AutoPlans/DiscoveryPlans/ClassGenerator_Plan.md |
| 行号 | 958-1058 |
| 问题 | 追加写入 Issue-28 ~ Issue-30 时，PowerShell here-string 对反引号转义，导致个别内联技术名显示成了控制字符或丢失反引号。受影响内容仅限术语展示，不影响源码证据、问题结论和解决步骤本身。 |
| 根因 | PowerShell 双引号 here-string 会解释反引号转义；本轮条目里包含 sCDataType::IsObject()、rray<StructB>、oid Foo(int32 Value)、	hread_local、	rue、alse、ApplyDefaults 等内联标记，写入时被部分转义。 |
| 影响 | 如果不补充说明，后续读者在执行计划时可能误读少数关键术语，尤其是 Issue-28 的类型判定点、Issue-29 的示例签名，以及 Issue-30 的线程局部/布尔变量名。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 不修改既有条目，只在末尾追加勘误表，校正受影响的内联技术名。 |
| 具体步骤 | 1. Issue-28 中步骤 2 的类型名应读作 sCDataType::IsObject()，容器示例应读作 rray<StructB>。 2. Issue-29 中问题描述与步骤 5 的示例函数签名应分别读作 oid Foo(int32 Value) 与 oid Foo()。 3. Issue-30 中问题描述与解决步骤里的关键术语应分别读作 	hread_local、	rue、alse 与 ApplyDefaults。 4. 后续若继续追加计划内容，改用单引号 here-string 或 pply_patch 生成文档片段，避免再次引入转义噪音。 |
| 涉及文件 | Documents/AutoPlans/DiscoveryPlans/ClassGenerator_Plan.md |
| 预估工作量 | S |
| 风险 | 无；这是纯文档勘误，不改变任何发现优先级或技术结论。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 重新读取文档尾部，确认读者能按本勘误恢复全部关键术语。 2. 确认 Issue-28 ~ Issue-30 的编号、优先级和方案步骤未被覆盖或删除。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P3 | 补充说明 | Refactoring | 仅文档勘误；不改变 Issue-28 ~ Issue-30 的执行顺序 |
---

## 发现与方案 (2026-04-09 00:05)

### 补充说明：Issue-28 ~ Issue-30 术语二次勘误

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Refactoring |
| 严重度 | Low |
| 文件 | `Documents/AutoPlans/DiscoveryPlans/ClassGenerator_Plan.md` |
| 行号 | `958-1078` |
| 问题 | 上一条勘误本身再次被 PowerShell 反引号转义污染，因此这里用不解释反引号的单引号 here-string 追加最终校正版本。 |
| 根因 | 文档生成方式选择错误，导致包含反引号的技术名在 PowerShell 中被当作转义序列处理。 |
| 影响 | 如果没有最终校正版本，读者仍需要手动猜测个别关键术语。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 直接给出受影响术语的最终正确写法，不再重复原问题全文。 |
| 具体步骤 | 1. `Issue-28` 中受影响术语的正确写法是 `asCDataType::IsObject()` 与 `array<StructB>`。 2. `Issue-29` 中示例签名的正确写法是 `void Foo(int32 Value)` 与 `void Foo()`。 3. `Issue-30` 中受影响术语的正确写法是 `thread_local`、`true`、`false` 与 `bApplyDefaults`。 4. 后续若继续用脚本追加 Markdown，优先使用单引号 here-string 或不含反引号的模板，再在最终渲染层补代码格式。 |
| 涉及文件 | `Documents/AutoPlans/DiscoveryPlans/ClassGenerator_Plan.md` |
| 预估工作量 | S |
| 风险 | 无；这是最终术语校正，不改变任何技术结论。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 重新读取文档尾部，确认上述四组术语能以完整文本出现。 2. 确认 `Issue-28` ~ `Issue-30` 的优先级和方案步骤未被覆盖。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P3 | 术语二次勘误 | Refactoring | 仅文档校正；不改变 `Issue-28` ~ `Issue-30` 的执行顺序 |

---

## 发现与方案 (2026-04-09 00:15)

### Issue-31：full reload 会在类验证失败后仍切换 subsystem 生命周期，旧 subsystem 已停用但坏的新 subsystem 仍会被激活

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Critical |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | `AngelscriptClassGenerator.cpp:2642-2648, 2446-2462`; `AngelscriptEngine.cpp:3875-4005, 4047-4052, 4136-4140` |
| 问题 | `CreateFullReloadClass()` 只要发现脚本类继承 `UDynamicSubsystem` 或 `UWorldSubsystem`，就会立刻对 `ReplacedClass` 调 `FSubsystemCollectionBase::DeactivateExternalSubsystem(...)`，并把 `NewClass` 放进 `ReinstancedSubsystems`。reload 尾声又无条件遍历这个数组执行 `ActivateExternalSubsystem(NewSubsystem)`。与此同时，`FinalizeClass()` / `VerifyClass()` 以及后续阶段仍可能把 `ModuleData.NewModule->bModuleSwapInError` 设为 `true`，而 `AngelscriptEngine.cpp` 只把这种状态记入 `PreviouslyFailedReloadFiles`，不会阻止本轮 `bShouldSwapInModules` 继续为真，也不会撤销 subsystem 变更。结果是：即使新类已经被判定为非法，本轮仍会先停掉旧 subsystem，再启用坏的新 subsystem。 |
| 根因 | subsystem 生命周期切换绑定在“类对象已 materialize”这一过早时点，而不是绑定在“本轮 class generation / verify / post-init 全部通过”的事务提交点；失败路径也没有 subsystem rollback。 |
| 影响 | 这会把普通的 class generator synthetic error 升级成运行时服务切换错误。旧 subsystem 可能在本轮失败 reload 中提前失效，而新 subsystem 又以未通过验证的 `UClass` 被激活，直接制造 editor/runtime 行为漂移、单例状态丢失，严重时会让后续 reload/PIE 继续在错误 subsystem 上运行。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 将 subsystem deactivate/activate 延后到 reload 事务最终提交阶段，只在确认无 `bModuleSwapInError` 后一次性切换。 |
| 具体步骤 | 1. 在 `FAngelscriptClassGenerator` 内新增显式的 pending 队列，例如 `PendingSubsystemReplacements`，记录 `{OldSubsystemClass, NewSubsystemClass}`，`CreateFullReloadClass()` 只登记，不再立即调用 `DeactivateExternalSubsystem(...)`。 2. 把当前 `ReinstancedSubsystems` 尾声激活逻辑改成 `CommitSubsystemReplacements()`，放在“所有类 finalize / verify / post-init 都通过且本轮没有 `bModuleSwapInError`”之后执行。 3. 在 `PerformFullReload()` 或与 `Issue-16` 共用的 reload transaction barrier 中增加失败分支：若任一模块标记了 `bModuleSwapInError`，直接丢弃 pending subsystem replacements，保证旧 subsystem 保持活动状态。 4. 如果某些 full reload 必须先停旧 subsystem 才能完成类替换，则需要把 `DeactivateExternalSubsystem` 也纳入 rollback helper，并在失败时显式恢复旧 subsystem，而不是仅仅跳过新 subsystem 激活。 5. 新增回归：构造一个脚本 subsystem，在 reload 中故意制造 `VerifyClass()` 或 `FinalizeActorClass()` 报错，断言旧 subsystem 仍保持激活，新 subsystem 不会进入 `FSubsystemCollectionBase`。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemScenarioTests.cpp`; `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp` |
| 预估工作量 | M |
| 风险 | 调整 subsystem 切换时机会影响现有依赖“full reload 立刻刷新 subsystem”的工具或 editor workflow，需要回归初始编译、正常 full reload 与失败 reload 三种场景。 |
| 前置依赖 | 建议与 `Issue-16` 的 reload transaction barrier 一起落地，否则 subsystem commit 仍缺统一失败判定源。 |
| 验证方式 | 1. 对 subsystem 脚本制造一次验证失败 reload，确认旧 subsystem 未被停用，新 subsystem 未被激活。 2. 对正常 full reload 场景回归，确认 subsystem 仍能在成功事务提交后完成替换。 3. 在失败后再次修复脚本并 reload，确认 subsystem 不会因为上一轮失败留下重复注册或双激活。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P0 | Issue-31 | Defect | 立即修复；否则一次失败的 full reload 就可能把 live subsystem 切到坏版本 |

---

## 发现与方案 (2026-04-09 00:16)

### Issue-32：`PostInitFunctions` 只按裸短名调度 getter，同名或缺失场景会静默执行错误 literal asset 初始化

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 行号 | `AngelscriptPreprocessor.cpp:4109-4123, 4132-4133`; `AngelscriptClassGenerator.cpp:5783-5803` |
| 问题 | literal asset 预处理器生成 getter 时，会把待执行入口登记成裸字符串 `Get{Name}`。`CallPostInitFunctions()` 随后只在线性遍历 `globalFunctionList` 时比较 `ScriptFunction->name`，既不带 namespace，也不带完整 declaration，命中后立即 `break`。因此只要同一 module 内存在同短名 getter，真正执行的是“列表里先出现的那个”，不是当前 literal asset 对应的那个；若目标 getter 根本没找到，局部变量 `bFound` 也不会参与任何报错或 `bModuleSwapInError` 判定，本轮 reload 会静默当成成功。 |
| 根因 | post-init 调度把“需要执行哪个 getter”的身份压缩成了未限定短名字符串，而执行层既没有 exact-match 解析，也没有 miss-path 的失败升级。 |
| 影响 | 这会把 literal asset 初始化变成编译顺序相关的非确定性行为。不同 namespace 或不同生成阶段出现同名 getter 时，错误 getter 可能 materialize 出完全无关的 asset；若 getter 丢失，asset 则直接保持未创建状态，但 reload 仍继续进入 CDO 初始化和后续 swap-in，最终以资源缺失、默认值漂移或热重载后状态不一致的形式滞后暴露。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 post-init 调度从“短名字符串”升级为“可精确解析的 qualified function identity”，并把 miss / ambiguous match 统一升级为 swap-in error。 |
| 具体步骤 | 1. 将 `FAngelscriptModuleDesc::PostInitFunctions` 的元素改成结构化记录，例如 `{Namespace, FunctionName, Declaration}` 或直接缓存 `asCScriptFunction*` 可恢复的签名 key，而不是继续只存 `Get{Name}`。 2. 在预处理器生成 getter 时，把 `Chunk.Namespace` 一并写进该记录；若未来支持重载，还要连 `property {Type} Get{Name}()` 这类 declaration 一起记录。 3. 重写 `CallPostInitFunctions()` 的查找逻辑：先按 namespace/qualified key 定位目标函数，再校验命中数量必须为 1；命中 0 个或多个都应输出明确诊断并设置 `ModuleData.NewModule->bModuleSwapInError = true`。 4. 与 `Issue-5` 联动：一旦 post-init 解析失败或执行失败，立即停止该模块后续 getter、阻止 `InitDefaultObjects()` 继续推进，避免错误 asset 继续污染 CDO。 5. 新增回归：同 module 下放两个同名 literal asset getter 于不同 namespace；再构造一个“getter 名被删除但 `PostInitFunctions` 仍指向旧名”的场景，断言前者精确执行目标 getter，后者稳定报错而不是静默跳过。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptManager.h` 或 `FAngelscriptModuleDesc` 定义所在文件; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp`; `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/*` |
| 预估工作量 | M |
| 风险 | 调整 `PostInitFunctions` 存储格式会影响预处理器与 class generator 的协议边界，需要同步清理旧缓存和任何调试输出格式。 |
| 前置依赖 | 建议与 `Issue-5` 一起实施，统一 post-init 的“精确定位 + 执行失败回滚”事务语义。 |
| 验证方式 | 1. 构造同短名不同 namespace 的 getter，确认 `CallPostInitFunctions()` 只命中正确目标。 2. 删除目标 getter 后重新编译，确认本轮 reload 被标成 `bModuleSwapInError`，而不是静默继续。 3. 回归普通 literal asset 流程，确认无 namespace、无重名场景下行为保持不变。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-32 | Defect | 在 post-init 事务修复时一并处理；否则 literal asset 初始化结果仍带命名冲突和静默丢失风险 |

---

## 发现与方案 (2026-04-09 00:17)

### Issue-33：`WITH_AS_DEBUGVALUES` 打开时 `ASClass`/ClassGenerator 仍调用已缺失的 per-object debug-value API，debug 构建无法自洽（补充 Analysis 70）

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDebugValue.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` |
| 行号 | `AngelscriptDebugValue.h:5-6, 43-146`; `ASClass.cpp:965-970, 1380-1382, 1447-1449, 1477-1479`; `AngelscriptEngine.cpp:5338-5418, 5497-5514`; `AngelscriptEngine.h:491-499` |
| 问题 | `WITH_AS_DEBUGVALUES` 在 debug 构建下会展开为真，但 `AngelscriptDebugValue.h` 里真正提供给调用方的 `FDebugValuePrototype` 只剩 `Create()` 和 `Reset()` stub，原本的 `FDebugValues`、`Instantiate()`、`Free()` 整段都被注释掉了。与此同时，`ASClass` 的对象构造/析构路径仍无条件调用 `Class->DebugValues.Instantiate(Object)` / `DebugValues.Free(Object->Debug)`，debug 栈同步逻辑也仍把 `Frame.Variables` 声明成 `FDebugValues*`，并调用 `Frame.Prototype->Instantiate(...)` / `Free(...)`。这意味着最需要排查热重载问题的 debug 配置下，ClassGenerator 相关调用侧和实现侧已经失配。 |
| 根因 | debug-value 子系统处于“调用路径保留、底层 per-object API 被半移除”的中间态；feature flag 仍表示功能可用，但头文件暴露的类型接口已经无法满足 `ASClass` 与调试栈的实际调用契约。 |
| 影响 | 一旦使用 `UE_BUILD_DEBUG` 配置，ClassGenerator/ASClass/DebugServer 这几条路径会在编译期就失去自洽性；即便通过局部宏规避编译，也会留下对象级 debug value 生命周期缺口，导致 ctor/dtor、堆栈变量和热重载时最关键的调试信息不可用。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 恢复 `FDebugValuePrototype` 的完整 per-object API，并用真实实现重新对齐 `ASClass` 与 debug stack 调用契约。 |
| 具体步骤 | 1. 在 `AngelscriptDebugValue.h` 恢复 `FDebugValues` 结构以及 `FDebugValuePrototype::Instantiate()` / `Free()` 的完整实现，不再让 `WITH_AS_DEBUGVALUES` 命中只含 stub 的分支。 2. 保留当前 no-op `FDebugValuePrototype` 仅用于 `!WITH_AS_DEBUGVALUES` 的分支，确保 feature flag 与可用 API 一一对应。 3. 对 `ASClass.cpp` 的 `RuntimeDestroyObject()`、三个 `Static*Constructor()` 以及 `AngelscriptEngine.cpp` 的 debug frame 生命周期做一次编译面收口，统一只通过完整 API 访问 `Instantiate()` / `Free()`，不要再依赖隐式存在的方法。 4. 若恢复实现需要额外清理内存布局或所有权语义，补一个局部 RAII wrapper，例如 `FScopedASDebugValues`，把 object ctor/dtor 与 stack frame 的释放路径统一起来，避免后续再次发生“调用点继续存在、实现已被裁掉”的分裂。 5. 新增专门的 debug-build 验证入口，至少覆盖一次 script object 构造/析构和一次 debug frame 变量采样，确保 `WITH_AS_DEBUGVALUES` 打开时能够完整编译并运行。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDebugValue.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`; `Plugins/Angelscript/Source/AngelscriptTest/Debugger/*` 或相关 debug 测试入口 |
| 预估工作量 | M |
| 风险 | 恢复 per-object debug value 后会重新引入 debug-only 堆分配和释放路径，需要确认不会给 hot reload 或异常析构再带来新的只在 debug 配置出现的泄漏。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 用 debug 配置编译 `AngelscriptRuntime`，确认 `Instantiate()` / `Free()` / `FDebugValues` 相关调用全部通过编译。 2. 创建并销毁一个 script actor/component/object，确认 `Object->Debug` 初始化与释放成对发生。 3. 在调试栈里采样一帧脚本调用，确认 `Frame.Variables` 能正确实例化并在 frame 切换时释放。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-33 | Defect | 在需要 debug 构建排查热重载前优先处理；否则调试配置本身就不可靠 |

---

## 发现与方案 (2026-04-09 00:24)

### Issue-34：组件树关键约束只在 `WITH_EDITOR` 下校验，非 Editor 构建会静默生成不同的 actor 层级

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` |
| 行号 | `AngelscriptClassGenerator.cpp:5290-5321, 5354-5440, 5468-5668`; `ASClass.cpp:1202-1323` |
| 问题 | `FinalizeActorClass()` 里“重复 `RootComponent`”和 `OverrideComponent` 目标/类型校验被包在 `#if WITH_EDITOR` 中，`VerifyClass()` 整个函数也只在 `WITH_EDITOR` 下编译；它负责检查 `Attach` 父节点是否存在、是否为 `USceneComponent`、`EditorOnly` 组合是否合法，以及抽象父组件是否被 override。相对地，运行时构造路径 `CreateDefaultComponents()` 在这些约束失效时不会拒绝生成，而是走容错分支：找不到 attach parent 就挂到当前 root，连 root 都没有就把 child 提升成新的 root；多个 `RootComponent` 时后创建的组件会直接抢 root，并把旧 root 重新挂到自己下面。结果是同一份脚本在 Editor 构建会报编译错误，在非 Editor 构建却能生成一个被 runtime fallback 改写过的组件树。 |
| 根因 | 组件树 contract 被拆成了“Editor-only authoring 校验”和“运行时尽量容错构造”两套语义，但 class generation 没有把 root 唯一性、attach 目标存在性、override 合法性这些核心不变量做成跨构建一致的硬约束。 |
| 影响 | 这会直接破坏动态 `UClass` 生成的一致性。非 Editor 构建里，相同脚本可能得到与 Editor 校验结果不同的 CDO/component hierarchy，热重载前后也可能因为是否命中这些 fallback 而出现不同 root/attach 结构，最终表现为组件树漂移、默认值差异和只在特定构建出现的行为 bug。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 将组件树的核心合法性检查抽成与构建配置无关的 validator，Editor-only 只保留诊断增强，不再决定语义是否成立。 |
| 具体步骤 | 1. 在 `AngelscriptClassGenerator.cpp` 提取统一 helper，例如 `ValidateActorComponentLayout(const FAngelscriptClassDesc&, UASClass&, FComponentLayoutValidationResult&)`，把 root 唯一性、override target 存在/类型兼容、attach parent 存在且为 `USceneComponent`、`EditorOnly` parent-child 合法性收口到一个无 `#if WITH_EDITOR` 的核心校验层。 2. `FinalizeActorClass()` 和 `VerifyClass()` 都改为调用该 helper；若是 `WITH_EDITOR`，可以保留更丰富的 `LineNumber`/详情诊断，但非 Editor 构建也必须在命中非法布局时设置 `bModuleSwapInError` 并拒绝继续生成。 3. 收紧 `CreateDefaultComponents()` 的 fallback：仅允许处理“合法但顺序未满足”的延迟 attach，不再把“attach parent 缺失”或“多个 root”悄悄改写成另一棵树；若运行时仍命中这类状态，改为 `ensureMsgf` + 中止当前类的组件 materialization，避免把坏布局带入 CDO。 4. 让 `DefaultComponents`/`OverrideComponents` 的 soft reload 与 full reload 共用同一套 validator，避免未来再次出现“编辑器里有校验、热重载路径里没校验”的分裂。 5. 新增回归：覆盖 duplicate root、missing attach parent、override target 不存在、abstract parent component 未 override 四类场景，并至少增加一条非 Editor 测试目标或 commandlet 验证，确认两种构建下都会稳定失败，而不是一个报错一个容错生成。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`; `Plugins/Angelscript/Source/AngelscriptTest/Component/*`; `Plugins/Angelscript/Source/AngelscriptTest/HotReload/*` |
| 预估工作量 | M |
| 风险 | 将当前非 Editor 的容错路径收紧成硬失败后，可能暴露出一批过去“能跑但布局已被改写”的脚本，需要准备迁移说明和更明确的错误信息。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 在 Editor 与非 Editor 构建分别编译包含非法组件树的脚本，确认都稳定报错。 2. 对合法组件树跑一轮 actor 构造与 hot reload 回归，确认不引入新的 hierarchy 变化。 3. 比较 reload 前后 CDO 的 root/attach 结构，确认不再依赖 runtime fallback 隐式修正。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-34 | Defect | 先修组件树 contract 的跨构建一致性；否则后续热重载和 CDO 差异排查会持续被构建配置噪音污染 |

---

## 发现与方案 (2026-04-09 00:26)

### Issue-35：`IsFunctionImplementedInScript()` 只检查 `UASFunction` 外壳，模块卸载/热重载清理后仍会把失效函数报告成“已实现”

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp` |
| 行号 | `ASClass.cpp:979-985`; `AngelscriptEngine.cpp:1054-1066`; `Bind_UObject.cpp:303-308` |
| 问题 | `UASClass::IsFunctionImplementedInScript()` 当前只做 `FindFunctionByName()` 后 `Cast<UASFunction>`，然后返回 `asFunction && asFunction->GetOuterUClass()`。它完全不检查 `UASFunction::ScriptFunction` 是否仍然绑定到 live script function。与此同时，`FAngelscriptEngine` 在模块清理路径里会把每个 `UASFunction` 的 `ScriptFunction` 和 `ValidateFunction` 明确置空。也就是说，类上还保留着 `UASFunction` UObject 外壳时，这个 API 仍会返回 `true`，哪怕真实脚本实现已经在 `DiscardModule` 或 reload 清理中失效。该 API 还通过 `Bind_UObject` 直接暴露给脚本和工具层。 |
| 根因 | 能力判定把“反射壳对象还在”误当成“脚本实现仍然有效”，没有把热重载生命周期里的真实失效位 `ScriptFunction == nullptr` 纳入 contract。 |
| 影响 | 热重载/模块卸载后，工具层和脚本层会基于错误前提做决策：`IsFunctionImplementedInScript()` 说函数还在，但后续调用路径可能因 `ScriptFunction == nullptr` 提前返回，源码定位信息也已经失效。这会制造能力探测与实际可执行状态不一致的 stale state，增加 reload 后行为分叉和误诊成本。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 将“脚本实现是否有效”收口为显式 helper，要求 `UASFunction` 外壳和底层 `ScriptFunction`/校验缓存同时处于 live 状态。 |
| 具体步骤 | 1. 在 `ASClass.cpp` 或 `ASClass.h` 新增 helper，例如 `static bool HasLiveScriptImplementation(const UFunction* Function)`，内部至少校验 `Cast<UASFunction>(Function)` 成功、`ScriptFunction != nullptr`，并在 RPC 场景下同步处理 `ValidateFunction` 的一致性。 2. 修改 `UASClass::IsFunctionImplementedInScript()` 只通过该 helper 返回结果，不再把 `GetOuterUClass()` 当成有效性的充分条件。 3. 审查所有依赖这条语义的路径，至少覆盖 `Bind_UObject.cpp` 暴露给脚本的 API、可能的编辑器 UI/调试展示，以及 reload 清理后对 capability 的 fallback 判断，确保都改读统一 helper。 4. 在 `DiscardModule()` / reload cleanup 里补一个成对的状态收口 helper，例如 `InvalidateScriptFunction(UASFunction&)`，集中清空 `ScriptFunction`、`ValidateFunction` 与相关缓存，避免未来再出现“清了一半、查询 API 还看另一半”的分裂。 5. 新增回归：先编译一个带脚本函数的类，断言 `IsFunctionImplementedInScript()` 为 true；再执行 `DiscardModule()`、类删除或 full reload cleanup，断言同一查询变为 false，且修复脚本后重新编译能恢复为 true。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`; `Plugins/Angelscript/Source/AngelscriptTest/HotReload/*` 或 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/*` |
| 预估工作量 | S |
| 风险 | 收紧判定后，个别现有调用方如果把“壳对象存在”当成 feature 探测依据，行为会变得更严格，需要同步检查是否应该改成区分“声明存在”和“实现有效”两个 API。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 运行 `DiscardModule`/full reload cleanup 场景，确认 `IsFunctionImplementedInScript()` 会从 true 变 false。 2. 对普通 live 类回归，确认未受影响的脚本函数仍返回 true。 3. 修复脚本并重新编译后再次查询，确认状态可以恢复，不会永久卡在 false。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-35 | Defect | 与模块清理和 reload 状态收口一并处理；否则运行时 capability 探测会持续基于失效函数作出错误判断 |

---

## 发现与方案 (2026-04-09 00:28)

### Issue-36：`UASFunction`/`UASClass` 的源码路径 API 固定返回 `Code[0]`，multi-section module 会持续跳错文件

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_scriptfunction.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_scriptfunction.cpp` |
| 行号 | `ASClass.cpp:1497-1520, 1535-1558`; `AngelscriptEngine.cpp:4343-4345`; `as_scriptfunction.h:193, 284, 405-409`; `as_scriptfunction.cpp:931-945, 1518-1523` |
| 问题 | 模块编译阶段会把 `Module->Code` 里的每个 section 都通过 `AddScriptSection(Section.AbsoluteFilename, Section.Code, ...)` 注入 AngelScript，说明单个 module 可以合法承载多个源文件。但 `UASClass::GetSourceFilePath()` / `GetRelativeSourceFilePath()` 和 `UASFunction::GetSourceFilePath()` 都直接返回 `Module->Code[0]`，完全忽略函数或类型真实声明所在 section。更糟的是，底层 `asCScriptFunction` 已经保存了 `scriptSectionIdx` / `sectionIdxs`，并提供 `GetScriptSectionName()`、`GetLineNumber(..., &sectionIdx)` 这套精确定位能力；当前 API 却没有接入它。结果是 line number 可以来自真实声明位置，而 file path 却永远指向模块首文件。 |
| 根因 | 当前源码定位 contract 只把“对象属于哪个 module”建模到运行时 metadata，没有把“对象属于 module 内哪个 section”保留下来；随后路径 API 又把 module 进一步折叠成 `Code[0]`。 |
| 影响 | 只要脚本模块拆成多文件，编辑器导航、错误跳转、热重载排障和任何依赖 `GetSourceFilePath()` 的工具都会稳定指向错误文件。这个问题不会立刻导致 class generation 崩溃，但会持续增加定位热重载和版本链问题的成本，并放大误诊。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 让函数路径走 AngelScript 已有的 section 索引，让类路径在编译/类生成阶段显式持久化 declaration section，而不是再依赖 `Code[0]`。 |
| 具体步骤 | 1. 修改 `UASFunction::GetSourceFilePath()`：优先调用 `asCScriptFunction::GetLineNumber(0, &SectionIdx)` 或 `GetScriptSectionName()` 取真实 section，再映射回 `FAngelscriptModuleDesc::Code[SectionIdx]` 或按 `AbsoluteFilename` 查找匹配 section；只有无法解析 section 时才回退到旧逻辑。 2. 保留 `GetSourceLineNumber()`，但让它与 file-path 解析共享同一 section helper，避免“行号来自真实 section、路径来自 `Code[0]`”这种分裂。 3. 对 `UASClass`，不要继续从 `ScriptTypePtr->GetModule()` 反推 `Code[0]`；在 `FAngelscriptClassDesc` 或 class finalization 过程中显式记录类声明的 `AbsoluteFilename`/`RelativeFilename`，并把它持久化到 `UASClass` 的 metadata 字段或轻量缓存中。 4. 将源码路径解析抽成统一 helper，例如 `ResolveScriptSourceLocation(...)`，供 `UASClass`、`UASFunction` 和未来调试/IDE 导航入口共用，避免再次出现“函数支持 section、类仍回 `Code[0]`”的半修复状态。 5. 新增 multi-section 回归：同一 module 下放两个脚本文件，类和函数分别声明在第二个 section，断言 `GetSourceFilePath()` / `GetRelativeSourceFilePath()` / `GetSourceLineNumber()` 返回的文件与行号组合一致，且热重载报错跳转命中正确文件。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` 或 `FAngelscriptClassDesc` 定义处; `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/*`; `Plugins/Angelscript/Source/AngelscriptTest/HotReload/*` |
| 预估工作量 | M |
| 风险 | 若类声明位置当前没有现成 metadata，需要补一段编译期持久化逻辑；这里要避免把临时 parser 状态直接泄漏到运行时对象生命周期。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 构造 multi-section module，确认类和函数都能返回实际声明文件而不是 `Code[0]`。 2. 对单文件 module 回归，确认现有导航结果不变。 3. 在热重载报错场景里查看 diagnostics/source jump，确认文件与行号组合不再错位。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-36 | Defect | 在高优先级热重载稳定性问题之后处理；它不直接导致崩溃，但会持续抬高复杂模块的排障成本 |

---

## 发现与方案 (2026-04-09 00:43)

### Issue-37：`DoSoftReload()` 只替换 `ScriptFunction`，不会重建 `UASFunction` 调用壳；JIT 与线程安全分派会继续沿用旧策略（补充 Analysis 15/16）

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h` |
| 行号 | `AngelscriptClassGenerator.cpp:3399-3426, 3660-3664, 4244-4260, 4779-4791`; `ASClass.cpp:1568-1740, 1762-1929, 2952-2989`; `ASClass.h:178-190` |
| 问题 | full reload 创建 `UFunction` 时，会先由 `UASFunction::AllocateFunctionFor()` 按 `bThreadSafe`、参数形态和 `jitFunction*` 是否存在选择具体的 `UASFunction_*` 子类，再把 `JitFunction` / `JitFunction_Raw` / `JitFunction_ParmsEntry` 缓存在对象上。soft reload 则只在 `DoSoftReload()` 里复用旧 `UASFunction`，把 `ScriptFunction` 指向新脚本函数，然后调用 `SoftReloadFunction()` 更新参数/返回值里的类型引用；整个路径没有刷新任何 `JitFunction*` 缓存，也不会把旧的 `UASFunction_JIT` / `UASFunction_NotThreadSafe` 子类替换成新的分派壳。运行时多条调用路径，例如 `OptimizedCall_*()` 和多个 `UASFunction_*_JIT::RuntimeCall*()`，都会直接执行对象上缓存的 `JitFunction_Raw`，不会回退到新的 `ScriptFunction->jitFunction*`。 |
| 根因 | 函数运行时分派策略被编码进了 `UASFunction` 的 UObject 类型和对象级缓存，但 soft reload 把函数更新实现成“只换脚本函数指针”，没有把“调用壳是否仍与新描述匹配”纳入 reload contract。 |
| 影响 | 只要热重载涉及可 JIT 的 `final` 函数、`BlueprintThreadSafe` / `NotBlueprintThreadSafe` 元数据，或任何会改变调用壳选择条件的函数语义，运行时就可能继续执行旧机器码，或继续沿用旧的 game-thread gate。结果是源码和 live `UFunction` 分派策略分叉，问题表面像“函数体没更新”或“线程安全标记没生效”，但根因实际上是软重载没有重建调用壳。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 `UASFunction` 的“调用壳形态”显式建模为 reload 约束；壳形态变化时强制 full reload，壳形态不变时统一刷新缓存。 |
| 具体步骤 | 1. 在 `AngelscriptClassGenerator.cpp` 提取 `ComputeFunctionDispatchShape(const FAngelscriptFunctionDesc&)`，把 `bThreadSafe`、是否可走 non-virtual JIT、是否需要 specialized subclass、是否依赖 generated world-context 等条件归一化成可比较的结构。 2. 在函数 diff / reload 判定阶段比较新旧 dispatch shape；只要 shape 变化，就把当前类至少提升到 `FullReloadRequired`，禁止继续复用旧 `UASFunction` 壳。 3. 对于 shape 未变的 soft reload，新增 `RefreshRuntimeCallCache(UASFunction&, const FAngelscriptFunctionDesc&)`，统一刷新 `ScriptFunction`、`JitFunction`、`JitFunction_Raw`、`JitFunction_ParmsEntry`，并在新函数不再满足 JIT 条件时显式清空旧缓存。 4. 把 full reload 创建函数时的缓存写入也改走同一个 helper，避免“full reload 一套逻辑、soft reload 另一套逻辑”再次分裂。 5. 新增热重载回归：一个 `final` 且可 JIT 的无参函数在 body-only 修改后必须执行新逻辑；一个函数在 `BlueprintThreadSafe` 与 `NotBlueprintThreadSafe` 间切换时，应触发 full reload 或明确拒绝 soft reload，而不是静默沿用旧子类。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`; `Plugins/Angelscript/Source/AngelscriptTest/HotReload/*`; `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/*` |
| 预估工作量 | M |
| 风险 | 将更多函数变化提升为 full reload 会增加开发期 reload 成本；如果 shape 比较定义不完整，仍可能遗漏某些 specialized call path。 |
| 前置依赖 | 无 |
| 验证方式 | 1. body-only 修改一个可 JIT `final` 函数，确认热重载后调用结果来自新实现而不是旧机器码。 2. 切换 `BlueprintThreadSafe` / `NotBlueprintThreadSafe`，确认不会静默沿用旧 `UASFunction` 子类。 3. 在日志或断点里检查 soft reload 后 `UASFunction` 上的 `JitFunction*` 缓存与新 `ScriptFunction` 一致。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-37 | Defect | 在继续依赖 soft reload 排查函数行为前先处理；否则函数体、JIT 和线程约束都可能继续跑旧壳 |

---

## 发现与方案 (2026-04-09 00:47)

### Issue-38：静态函数的 `WorldContextOffsetInParms` 从未初始化，`RuntimeCallEvent`/JIT 路径会按 `-1` 偏移读取参数

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Critical |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` |
| 行号 | `ASClass.h:179-180`; `AngelscriptClassGenerator.cpp:3521-3561, 3626-3644`; `ASClass.cpp:503-519, 577-585` |
| 问题 | `UASFunction` 头文件把 `WorldContextOffsetInParms` 初始化为 `-1`。full reload 生成静态函数时，类生成器会在 `3521-3561` 计算 `WorldContextIndex`、决定是否生成隐藏的 `_World_Context` 参数，并在 `3640-3644` 把这个参数计入 `ParmsSize`；但真正负责记录参数偏移的代码 `NewFunction->WorldContextOffsetInParms = Prop->GetOffset_ForUFunction();` 被整个注释掉了。运行时两条调用路径却都会直接消费这个字段：`AngelscriptCallFromParms()` 的 non-thread-safe JIT 路径在 `503-519` 对所有静态函数都按该偏移读取 world context，generic 路径在 `577-585` 对 generated world context 也这么做。也就是说，当前实现会把 `Parms - 1` 当成 `UObject*` 读取来源。 |
| 根因 | 动态 `UFunction` 生成把 world-context 参数的“存在性”和“布局偏移”拆成了两步，但只实现了前半段；运行时 thunk 假定偏移一定已完成绑定。 |
| 影响 | 任何经由 `RuntimeCallEvent` 或相关 JIT thunk 调用的静态脚本函数，只要路径尝试建立 ambient world context，就会从无效地址读取 `UObject*`。轻则把错误对象注入 `AssignWorldContext()`，重则在参数边界外越界读取后直接崩溃。这是函数生成与运行时调用协议不一致导致的硬错误。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 让 world-context 参数绑定走完整闭环：生成时记录真实偏移，reload 时同步刷新，运行时在未绑定时拒绝继续读取。 |
| 具体步骤 | 1. 在 `AngelscriptClassGenerator.cpp` 恢复并收敛 world-context 偏移绑定逻辑，例如提取 `BindWorldContextLayout(UASFunction&, const TArray<FProperty*>&, FProperty* GeneratedWorldContextProperty)`；在 `StaticLink(true)` 之后显式写入 `WorldContextOffsetInParms`。 2. 对“已有显式 world-context 参数”和“自动生成隐藏 `_World_Context` 参数”两种路径都使用同一个 helper，不再依赖注释掉的扫描代码。 3. 在 soft reload 复用 `UASFunction` 时，同步刷新 `WorldContextIndex`、`bIsWorldContextGenerated` 和 `WorldContextOffsetInParms`，避免旧 offset 残留到新参数布局。 4. 在 `AngelscriptCallFromParms()` 进入静态函数 world-context 分支前增加 defensive check：若 `WorldContextOffsetInParms < 0`，立即 `ensureMsgf` + 中止本次调用，并打出函数名诊断，避免继续读越界地址。 5. 新增回归：覆盖一个自动生成 world-context 的静态函数和一个显式 `meta=(WorldContext=...)` 的静态函数，分别通过 `RuntimeCallEvent` 路径调用，断言读取到的 world context 与传入参数一致，且不再命中非法偏移。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/*`; `Plugins/Angelscript/Source/AngelscriptTest/HotReload/*` |
| 预估工作量 | M |
| 风险 | 运行时加防护后，过去“侥幸未崩但拿到错误 world context”的静态函数会开始显式失败，需要同步补测试和错误文案。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 通过 `RuntimeCallEvent` 调用静态脚本函数，确认 `WorldContextOffsetInParms` 为非负且读取到正确对象。 2. 在 JIT 与非 JIT 路径分别验证一次，确认两条 thunk 都使用同一正确偏移。 3. 人工构造 offset 未绑定场景，确认运行时会拒绝调用并输出诊断，而不是访问 `Parms - 1`。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P0 | Issue-38 | Defect | 立即修复；这是直接的越界读取入口，继续留着会污染所有静态函数调用排查 |

---

## 发现与方案 (2026-04-09 00:52)

### Issue-39：`WithValidate` 语义被拆成三份状态，soft reload 既不会检测定义变化，也不会刷新 RPC 校验绑定（补充 Analysis 18）

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptComponent.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | `AngelscriptPreprocessor.cpp:1627-1638`; `AngelscriptEngine.h:984-985, 1032-1053`; `AngelscriptClassGenerator.cpp:1020-1049, 1223-1254, 3469-3472, 3660-3664, 4244-4260`; `ASClass.cpp:1956-1958`; `AngelscriptComponent.cpp:46-54, 89-103`; `AngelscriptEngine.cpp:1060-1065` |
| 问题 | 预处理器把 `WithValidate` 写进 `FAngelscriptFunctionDesc::bNetValidate`，full reload 会据此给主函数打 `FUNC_NetValidate` 并在收尾时缓存 `ValidateFunction`。但函数定义比较 `IsDefinitionEquivalent()` 并没有比较 `bNetValidate`，因此把 RPC 从“无校验”改成“有校验”或反向移除，都不会在 `1223-1254` 被视为定义变化。进入 soft reload 后，`DoSoftReload()` 只替换 `ScriptFunction` 并调用 `SoftReloadFunction()`，不会同步 `FunctionFlags`，也不会重算 `ValidateFunction`。运行时 `UAngelscriptComponent::ProcessEvent()` 是否先执行 `_Validate`，正是由 `FUNC_NetValidate` 和 `GetRuntimeValidateFunction()` 这两份旧状态共同决定；而模块清理时又会把 `ValidateFunction` 置空。 |
| 根因 | `WithValidate` 被拆成了描述层 `bNetValidate`、运行时 `UFunction::FunctionFlags`、以及 `UASFunction::ValidateFunction` 三个状态源，但 reload 判定只看其中两部分，soft reload 同步则三部分都没有做闭环。 |
| 影响 | 热重载后 RPC 校验链可能长期停留在旧版本：新增 `WithValidate` 时运行时仍不校验，移除 `WithValidate` 时仍可能继续执行旧 `_Validate`，模块清理后甚至会出现 `FUNC_NetValidate` 还在但 `ValidateFunction == nullptr` 的半失效状态。最终表现是网络行为和脚本源码不一致，且问题只在 live reload 场景暴露。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 `WithValidate` 明确纳入函数定义与运行时绑定契约；标志变化强制 full reload，普通 soft reload 也要刷新 validate cache。 |
| 具体步骤 | 1. 在 `FAngelscriptFunctionDesc::IsDefinitionEquivalent()` 中纳入 `bNetValidate` 比较，确保 `WithValidate` 的新增/删除会把函数提升到 `FullReloadRequired`。 2. 在 full reload 与 soft reload 共用一套 `RefreshNetValidateBinding(UASFunction&, UClass&)` helper：根据当前描述重写 `FUNC_NetValidate`、重新查找 `<FunctionName>_Validate` 并更新 `ValidateFunction`，移除 `WithValidate` 时则显式清空两者。 3. 修改 `DoSoftReload()`，在函数重绑定阶段除了 `ScriptFunction` 和类型引用外，还要调用该 helper；不要把 RPC 校验链留给旧 `UFunction` 状态。 4. 在模块清理路径补一个统一的 `InvalidateRuntimeFunctionState(UASFunction&)`，集中清空 `ScriptFunction`、`ValidateFunction` 和相关 runtime 标志，避免半失效壳对象继续被 `ProcessEvent()` 误判。 5. 新增网络/热重载回归：同一个 RPC 分别测试“增加 `WithValidate`”“移除 `WithValidate`”“仅修改 `_Validate` 函数体”三种场景，断言运行时调用链与源码声明保持一致。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptComponent.cpp`; `Plugins/Angelscript/Source/AngelscriptTest/HotReload/*`; `Plugins/Angelscript/Source/AngelscriptTest/Component/*` |
| 预估工作量 | M |
| 风险 | 将 `WithValidate` 变化升级为 full reload 后，会扩大一部分 RPC 变更的 reload 成本；同时需要确认 helper 不会影响纯 C++ RPC 的既有 `ProcessEvent` 路径。 |
| 前置依赖 | 无 |
| 验证方式 | 1. soft reload 前后分别调用带 `WithValidate` 的 RPC，确认新增/移除校验时运行时行为同步变化。 2. 检查主函数的 `FUNC_NetValidate` 与 `ValidateFunction` 是否始终成对更新。 3. 在 `DiscardModule()` 后触发相关查询，确认不会留下“flag 还在、validate 指针已空”的半失效状态。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-39 | Defect | 在修完静态函数 world-context 之后处理；否则网络 RPC 的 live reload 结果仍会和源码声明分叉 |

---

## 发现与方案 (2026-04-09 00:44)

### Issue-40：`CleanupRemovedClass()` 只断脚本指针，不隔离构造入口；removed class 在 GC 窗口内仍可生成半失效对象

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp` |
| 行号 | `AngelscriptClassGenerator.cpp:4990-5023`; `ASClass.cpp:1176-1338, 1352-1490`; `Bind_UObject.cpp:331-334` |
| 问题 | `CleanupRemovedClass()` 删除类时只清 `ScriptTypePtr`、`ConstructFunction`、`DefaultsFunction`，并打 `CLASS_Hidden`/`CLASS_NotPlaceable`，但不会清空 `ClassConstructor`、`CodeSuperClass`、`DefaultComponents`、`OverrideComponents`、tick 缓存或现有 CDO。与此同时，脚本侧全局函数 `UClass.FindClass()` 直接 `FindObject<UClass>(nullptr, *Name)`，不会过滤 hidden/removed class。只要外部仍持有旧 `UClass*`，或在 GC 前通过名字命中这个 removed class，后续 `StaticActorConstructor()` / `StaticComponentConstructor()` / `StaticObjectConstructor()` 仍会继续执行 `CodeSuperClass->ClassConstructor`，actor 路径还会继续 `ApplyOverrideComponents()` 与 `CreateDefaultComponents()`；只是因为 `ScriptTypePtr == nullptr` 才跳过 `asCScriptObject` placement-new 和脚本 ctor/defaults。 |
| 根因 | 当前删除流程只把 removed class 当成“脚本实现失效”，没有把它当成“禁止继续 materialize 的 tombstone runtime type”。类级实例化入口和查找入口没有被统一 quarantine。 |
| 影响 | deleted class 在真正 GC 前仍可能生成 native shell、默认组件树和 CDO 访问结果，但这些对象已经不再有合法 script backing。表现会是“脚本类已删除，但 `FindClass()` / stale `TSubclassOf` 仍能生成一个半失效 actor/object”，直接制造 CDO 状态漂移和运行时误诊。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 removed class 从“仅断脚本指针”升级为“彻底 quarantine 的 tombstone class”，同时收紧外部 lookup。 |
| 具体步骤 | 1. 在 `AngelscriptClassGenerator.cpp` 新增 `QuarantineRemovedClass(UASClass&)` helper，集中处理 removed class 的 runtime 隔离。 2. helper 内在现有 hidden/not-placeable 标志之外，再立即重命名到 tombstone 名称，并补 `CLASS_Abstract`，阻止正常 `NewObject` / spawn 再把它当成可实例化类。 3. 在 helper 内清空 `DefaultComponents`、`OverrideComponents`、`ComposeOntoClass`、tick 缓存，并把 `ClassConstructor` 切到专用的 `StaticRemovedClassConstructor` fail-fast stub；stub 只输出诊断，不再执行 `ApplyOverrideComponents()`、`CreateDefaultComponents()` 或脚本初始化。 4. 修改 `Bind_UObject.cpp` 的 `UClass.FindClass()` 与同类名字查找 helper，只返回 live class：至少要求未命中 tombstone/removed 标记，且 `ScriptTypePtr` 或等价 runtime-state helper 仍有效。 5. 为 removed class 的现有 `ClassDefaultObject` 增加 defensive 标记或诊断 metadata，避免工具层继续把它当成有效 CDO。 6. 新增回归：删除脚本类后，在 GC 前分别走 `FindClass()`、stale `TSubclassOf` 和 `GetDefaultObject()`，断言它们拿不到可继续构造的 live class，且不会再创建默认组件或半失效对象。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`; `Plugins/Angelscript/Source/AngelscriptTest/HotReload/*`; `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/*` |
| 预估工作量 | M |
| 风险 | 过滤 hidden/tombstone class 后，少量依赖“按旧名字继续拿到历史类对象”的调试脚本或测试 helper 会改变行为，需要同步更新为 tombstone-aware 查询。 |
| 前置依赖 | 建议与已记录的 removed-class tombstone 命名修复一起落地，避免 lookup 和构造隔离各自维护一套 removed 状态。 |
| 验证方式 | 1. 删除脚本类后立刻调用 `UClass.FindClass()`，确认不会再返回可实例化的 live class。 2. 对 stale `UClass*`/`TSubclassOf` 执行 `GetDefaultObject()`、`NewObject` 或 spawn，确认命中 fail-fast 诊断，而不是继续生成默认组件。 3. 强制 GC 前后各回归一次，确认 removed class 不再表现为“名字还在、对象还能构造”的半存活状态。 |

### Issue-41：removed class 的 `UASFunction` runtime state 清理只在 `WITH_EDITOR` 且不覆盖 `JitFunction*` / `ValidateFunction`，删除后仍可能执行陈旧代码

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Critical |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` |
| 行号 | `ASClass.h:123-127, 188-190`; `AngelscriptClassGenerator.cpp:4990-5017`; `AngelscriptEngine.cpp:4015-4025`; `ASClass.cpp:154-157, 480-483, 1956-1958, 2982-3010` |
| 问题 | `UASFunction` 持有四类 runtime state：`ScriptFunction`、`ValidateFunction`、`JitFunction` / `JitFunction_ParmsEntry` / `JitFunction_Raw`。但 `CleanupRemovedClass()` 只在 `#if WITH_EDITOR` 下遍历函数并把 `Function->ScriptFunction = nullptr`；非 Editor 构建这一步完全不会执行，而且任何构建下都不会清 `ValidateFunction` 与三份 JIT cache。随后旧模块会在 `AngelscriptEngine.cpp:4015-4025` 被 `DiscardModule()` 并 `DeleteDiscardedModules()`。运行时分派又并不一致：generic path 只在少数入口依赖 `ScriptFunction == nullptr` 早退，而多个 JIT specialized thunk（例如 `UASFunction_NoParams_JIT::RuntimeCallEvent()`）根本不检查这个条件，直接执行 `VerifyScriptVirtualResolved()` 和 `MakeRawJITCall_NoParam(Object, JitFunction_Raw)`。 |
| 根因 | 函数壳对象的失效策略既是 config-sensitive（只在 `WITH_EDITOR` 清一部分状态），又不是完整失效（JIT/Validate cache 没有统一 invalidation helper），导致 removed class 的 `UASFunction` 还能带着旧 runtime payload 继续被调用。 |
| 影响 | 这是直接的 stale-call 面。non-Editor 构建下 removed class 可能继续持有完整 `ScriptFunction*`；Editor 构建即使清了 `ScriptFunction`，JIT specialized thunk 仍可能沿 `JitFunction_Raw` 执行已被 discard module 的机器码，`GetRuntimeValidateFunction()` 也会继续返回过期 `_Validate`。最终结果是 removed class 在模块删除后仍可能 crash、跑旧逻辑或触发 use-after-free。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 引入统一的 `InvalidateRuntimeFunctionState(UASFunction&)`，在所有构建下完整清空函数壳的 runtime payload，并让 JIT thunk 对失效态 fail-closed。 |
| 具体步骤 | 1. 在 `ASClass.h/.cpp` 或 `AngelscriptClassGenerator.cpp` 新增 `InvalidateRuntimeFunctionState(UASFunction&)` helper，统一清空 `ScriptFunction`、`ValidateFunction`、`JitFunction`、`JitFunction_ParmsEntry`、`JitFunction_Raw`，必要时补充 world-context/dispatch-shape 相关缓存复位。 2. 修改 `CleanupRemovedClass()`：无论 `WITH_EDITOR` 与否，都遍历 removed class 上的 `UASFunction` 并调用该 helper，不再把失效清理包在 Editor 宏里。 3. 把 full reload 旧类清理、`DiscardModule()` 相关 cleanup 和后续任何函数壳废弃路径统一改用同一个 helper，避免再次出现“一个路径清 `ScriptFunction`、另一路不清 JIT”的分裂。 4. 收紧所有 JIT specialized `RuntimeCall*()`：在执行 `VerifyScriptVirtualResolved()` 或 `MakeRawJITCall_*()` 前增加统一 `HasLiveRuntimeBinding()` 检查；失效时输出诊断并 fail-closed，不允许继续走旧机器码。 5. 让 `GetRuntimeValidateFunction()` 和 `IsFunctionImplementedInScript()` 也改读统一的 live-binding helper，避免 query API 与实际可执行状态继续分叉。 6. 新增回归：删除一个带 JIT specialization 和 `_Validate` 的脚本类，在 Editor 与非 Editor 两种配置下分别触发旧 `UFunction` 调用，断言不会执行 stale JIT / stale validate，也不会崩溃。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptTest/HotReload/*`; `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/*`; `Plugins/Angelscript/Source/AngelscriptTest/Component/*` |
| 预估工作量 | M |
| 风险 | fail-closed 后，过去“误跑旧代码但表面看起来还能工作”的路径会开始显式报错，需要同步修复依赖这些隐式行为的测试。 |
| 前置依赖 | 建议与已记录的 `WithValidate`/dispatch-shape 清理方案合并设计，避免同一类 runtime function state 出现多套 invalidation 逻辑。 |
| 验证方式 | 1. 删除带 JIT specialization 的脚本函数后，调用旧 `UFunction` 壳，确认不会进入 `MakeRawJITCall_*()`。 2. 非 Editor 构建下重复同样场景，确认 `ScriptFunction` 也被正确清空。 3. 对带 `_Validate` 的 RPC 做 remove/reload 回归，确认 `GetRuntimeValidateFunction()` 不会再返回 stale pointer。 |

### Issue-42：full reload 在旧实例真正 GC 前就清空类级 `ScriptTypePtr`，live old object 会落入“空类型窗口”

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Critical |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`; `J:/UnrealEngine/UEAS2/Engine/Source/Runtime/Engine/Private/UnrealEngine.cpp` |
| 行号 | `AngelscriptClassGenerator.cpp:2398-2444`; `angelscript.cpp:4-11`; `ASClass.cpp:83-120`; `UnrealEngine.cpp:1838-1841` |
| 问题 | full reload 收尾阶段会先遍历所有 replaced class，把 `ReplacedClass->ScriptTypePtr = nullptr`，再把所有仍引用旧 script type 的类一并清空，最后才调用 `GEngine->ForceGarbageCollection(true)`。而本插件把对象类型解析和虚函数分派都建立在类级 `ScriptTypePtr` 上：`asIScriptObject::GetObjectType()` 直接返回 `GetFirstASClass((UObject*)this)->ScriptTypePtr`，`VerifyScriptVirtualResolved()` / `ResolveScriptVirtual()` 也会把 `asClass->ScriptTypePtr` 当成 `asCObjectType*`，并在其为 `nullptr` 时 `checkSlow(ObjectType != nullptr)`。引擎侧 `UEngine::ForceGarbageCollection()` 本身只设置 `TimeSinceLastPendingKillPurge` 与 `bFullPurgeTriggered`，并不会在该调用点同步销毁旧对象。 |
| 根因 | 当前实现把“禁止旧类再参与新一轮 class generation”和“旧 live object 仍需要能解析自身 script type 直到被回收”这两个生命周期需求，混在同一个类级 `ScriptTypePtr` 字段上处理；结果是类状态被提前切断，而旧实例的真实回收却被延后到 GC 调度。 |
| 影响 | 只要旧实例、旧 blueprint child 或任何跨帧持有的 UObject 引用在真正 GC 前再次触发脚本查询/虚调用，就会读到空类型：轻则 `GetObjectType()` 返回 `nullptr`，重则在 `ResolveScriptVirtual()` / `VerifyScriptVirtualResolved()` 处断言崩溃。这是一个发生在 reload 与 GC 之间的真实 crash window。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把“旧对象仍需读取的 script type”与“新类不再接受旧 type 绑定”拆成两个阶段：先 quarantine，再在 post-GC 真正释放 type。 |
| 具体步骤 | 1. 在 class generator 或版本链模块里新增 `FPendingOldScriptType`/`FDeferredScriptTypeRelease` 结构，记录 `ReplacedClass`、旧 `asITypeInfo*`、关联 blueprint child 和待释放轮次。 2. full reload 时不要立刻把 live old class 的 `ScriptTypePtr` 清成 `nullptr`；改为设置 tombstone/removed 标志，阻止它参与新的创建与解析，但保留旧 type 供现存对象在销毁前读取。 3. 通过 `FCoreUObjectDelegates::GetPostGarbageCollect()`、等价 post-GC hook，或显式“确认无实例残留”阶段，再统一执行真正的 `ScriptTypePtr`/`ConstructFunction`/`DefaultsFunction` 清理与旧 type release。 4. 若必须在 reload 当轮就切断类创建入口，则新增独立标志例如 `bRuntimeTypePendingDelete`；`FindClass`、spawn、`GetMostUpToDateClass()` 等入口读这个标志拒绝继续 materialize，但 `GetObjectType()` 与虚调用分派在旧对象销毁前仍可回退到保存的 old type。 5. 为 `ResolveScriptVirtual()` / `VerifyScriptVirtualResolved()` 增加 defensive fallback：若 class 已 tombstone 且存在 deferred old type，优先使用 deferred old type，而不是直接 `checkSlow(nullptr)`。 6. 新增回归：制造一次 full reload 后保留旧实例到下一帧，在 GC 触发前执行 `GetObjectType()`、虚函数调用和 blueprint child callback，断言不会命中空类型崩溃；GC 完成后再确认旧 type 被真正释放。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.cpp`; `Plugins/Angelscript/Source/AngelscriptTest/HotReload/*`; `Plugins/Angelscript/Source/AngelscriptTest/Debugger/*` 或 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/*` |
| 预估工作量 | L |
| 风险 | 延后释放旧 type 会延长一小段旧模块状态存活时间，需要严格通过 post-GC hook 收口，避免把“空类型窗口”换成“旧 type 永久滞留”。 |
| 前置依赖 | 建议与版本链/tombstone 生命周期治理一起设计，避免类删除、replace 和 old-type 延后释放分别维护独立状态机。 |
| 验证方式 | 1. full reload 后在 GC 真正执行前调用旧实例的 `GetObjectType()`，确认不会返回空指针。 2. 对旧实例触发一次虚函数路径，确认不会在 `ResolveScriptVirtual()` / `VerifyScriptVirtualResolved()` 崩溃。 3. GC 完成后检查 deferred old type registry，确认旧 type 已被清空，不会形成新的泄漏。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P0 | Issue-41 | Defect | 立即修复；它允许 removed class 在模块删除后继续跑 stale `JIT`/`Validate` 路径，属于直接 crash / use-after-free 面 |
| P0 | Issue-42 | Defect | 紧随其后；full reload 与 GC 之间存在明确的空类型窗口，旧实例再次参与脚本分派就可能断言崩溃 |
| P1 | Issue-40 | Defect | 在前两项之后处理；先把 removed class 构造入口 quarantine，消除“类已删但仍能生成半失效对象”的长期污染面 |

---

## 发现与方案 (2026-04-09 00:56)

### Issue-43：依赖传播只提升 `ClassData.ReloadReq`，不会回写 `ModuleData.ReloadReq`；执行阶段会按过低等级 reload 破坏依赖顺序

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Critical |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | `AngelscriptClassGenerator.cpp:1798-1848, 1872-1905, 1937-1960, 2081-2090`; `AngelscriptEngine.cpp:3914-3969` |
| 问题 | `Analyze(FModuleData&)` 只在初次扫描时把各个 class/delegate 的 `ReloadReq` 聚合进 `ModuleData.ReloadReq`。但 `Setup()` 随后还会运行 `PropagateReloadRequirements()`，这一步会通过 `AddReloadDependency()` 把 provider 的 `ReloadReq` 提升到 consumer 的 `ClassData.ReloadReq` / `DelegateData.ReloadReq`，却没有任何一步再把传播后的结果回写到 `ModuleData.ReloadReq`。`Setup()` 最终返回的总体 `ReloadReq` 仍然只看各模块旧的 `ModuleData.ReloadReq`。后续 `AngelscriptEngine.cpp` 按这个总体等级决定 `PerformSoftReload()` 还是 `PerformFullReload()`；而 `ShouldFullReload(FClassData&)` 在 `bIsDoingFullReload == false` 时根本不看 `ClassData.ReloadReq`，只看 interface/new-class 等特例。结果就是：某个 consumer 已经因为依赖传播被提升到 `FullReloadRequired`，实际执行阶段仍可能整体走 soft reload，并把这个 consumer 当成普通 soft reload class 处理。 |
| 根因 | reload 需求被拆成了“class/delegate 级真值”和“module/overall 级调度值”两层，但依赖传播只更新前者，没有维护后者的一致性；执行入口又只认 module/overall 层。 |
| 影响 | 依赖图里真正需要 full reload 的 consumer 会被悄悄降格。典型后果是：provider 发生结构性变化后，consumer 没有被正确拉入 full reload 闭包，仍旧复用旧 `UClass` / `FProperty` / `UFunction` 外壳，直接制造“依赖未满足但 reload 继续执行”的状态。这会把类型注册顺序重新变成编译批次和模块顺序相关的问题。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把“传播后的最高 reload requirement”提升为单一调度真值；任何 class/delegate 的升级都必须同步反映到 module 和整体执行模式。 |
| 具体步骤 | 1. 在 `Setup()` 的依赖传播循环之后新增一次 `RecomputeModuleReloadRequirements()`，重新扫描每个 `ModuleData` 下的 `Classes` / `Delegates` / `Enums`，以传播后的最高值覆盖 `ModuleData.ReloadReq`，并同步补齐 `ReloadReqLines`。 2. 将 `Setup()` 返回值改为基于这次重算后的 `ModuleData.ReloadReq`，不要再直接沿用 `Analyze()` 阶段的旧聚合结果。 3. 收紧执行阶段：即使整体处于 `SoftReloadOnly` 会话，`ShouldFullReload(FClassData&)` 也必须优先尊重 `Class.ReloadReq >= FullReloadRequired`，至少要拒绝 swap-in 或把该 class 标成 error，而不是继续走 `PrepareSoftReload()` / `DoSoftReload()`。 4. 给 `AddReloadDependency()` / `PropagateReloadRequirements()` 增加诊断日志或调试断言：当某个 class 因依赖传播被升级时，记录 provider、consumer 和新等级，便于确认闭包是否正确形成。 5. 新增回归：构造一个本地只发生 body 变化的 consumer，但其 property/return/parameter 依赖的 provider 在同轮被提升为 `FullReloadRequired`；断言 `Setup()` 返回至少 `FullReloadRequired`，`NeedsFullReload(Module)` 为 true，且 `SoftReloadOnly` 模式会拒绝继续 swap-in。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptTest/HotReload/*`; `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/*` |
| 预估工作量 | M |
| 风险 | 一旦把传播结果真正抬升到模块级，现有一些“看起来能 soft reload”的场景会开始被正确拦截，短期内会增加 full reload / compile error 的可见次数。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 新增依赖传播回归，检查 `Setup()` 返回值和 `NeedsFullReload()` 是否与 consumer 的最终 `ClassData.ReloadReq` 一致。 2. 在 `SoftReloadOnly` 模式下触发该场景，确认不会再进入 `PrepareSoftReload()` / `DoSoftReload()`。 3. 对正常 body-only 修改做对照回归，确认没有把无结构变化的 class 误升级。 |

### Issue-44：script interface 被排除在依赖传播之外，引用 interface 类型的 consumer 不会在 provider reload 时重建

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp` |
| 行号 | `AngelscriptClassGenerator.cpp:1752-1766, 1910-1918, 1933-1966, 2596-2610, 3940-3976`; `AngelscriptType.cpp:210-226`; `Bind_BlueprintType.cpp:112-123` |
| 问题 | `SetupModule()` 明确把 interface 从 `DataRefByNewScriptType` 建表里排除，注释也写明 interface `ScriptType` 是“预处理阶段注册的 built-in AS type”。随后 `AddReloadDependency(const FAngelscriptTypeUsage&)` 又要求 `Type.ScriptClass` 必须带 `asOBJ_SCRIPT_OBJECT` 才继续追依赖；而 interface 在 `CreateFullReloadClass()` 里是通过 `RegisterObjectType(..., asOBJ_REF | asOBJ_NOCOUNT | asOBJ_IMPLICIT_HANDLE)` 注册的，不满足这个门槛。结果是：property / return / argument 只要“引用了 script interface 类型但没有直接 implements 它”，依赖传播就完全看不见这个 provider。与此同时，这些 consumer 的 `FProperty` / `UFunction` 参数壳在创建时又会把 `Usage.ScriptClass->GetUserData()` 里的 `UClass*` 烧进反射数据。只要 interface provider 在本轮被 full reload 成新的 `UASClass`，而 consumer 没被拉进 reload 闭包，它就会继续持有旧 interface `UClass`。 |
| 根因 | 当前依赖图把“可传播的 script type”错误等同于 `asOBJ_SCRIPT_OBJECT` + `DataRefByNewScriptType`，而 script interface 恰好走的是另一套 engine-level registration 路径。类型系统承认它，依赖系统却完全忽略它。 |
| 影响 | 这会稳定制造“provider 已换新、consumer 仍持旧 interface type”的分叉。直接后果包括：`FObjectProperty::PropertyClass`、函数参数/返回值里的 interface class 指针停留在旧版本；interface 删除或 ABI 调整后，未重载 consumer 仍按旧接口做反射、校验和动态调用。对跨模块 interface API 尤其危险，因为表面上 provider reload 成功，真正出问题的是没有任何显式报错的 consumer 壳对象。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 script interface 纳入和 class/struct/delegate 同级的 reload 依赖图，不再用 `asOBJ_SCRIPT_OBJECT` 作为唯一筛选条件。 |
| 具体步骤 | 1. 在 `SetupModule()` 为 interface 也建立可追踪的数据引用，新增统一 key，例如按 `asITypeInfo*` 或显式 `FASQualifiedTypeKey` 建 `DataRefByScriptType`，不要再只把非 interface class 放进 `DataRefByNewScriptType`。 2. 重写 `AddReloadDependency(const FAngelscriptTypeUsage&)`：若 `Type.ScriptClass` 对应的是当前批次的 script interface/class/struct/delegate，都要进入依赖传播；不要因为缺少 `asOBJ_SCRIPT_OBJECT` 就提前返回。 3. 给 interface provider 的 reload requirement 传播增加硬规则：任何 property / return / argument 使用该 interface 的 consumer，至少提升到 `FullReloadSuggested`，若 interface `UClass` 会被替换或删除，则提升到 `FullReloadRequired`。 4. 在 `AddFunctionReturnType()` / `AddFunctionArgument()` 与 property materialization 前增加统一的 `EnsureTypeDependenciesReloaded(FAngelscriptTypeUsage)` 屏障，确保 consumer 生成反射壳之前，interface provider 已经完成本轮 create/reload。 5. 新增回归：`class Consumer` 只持有 `IMyInterface` property、return 或 parameter，但不 `implements` 该接口；修改或删除 `IMyInterface` 后，断言 `Consumer` 会被纳入本轮 reload 集合，且其 `PropertyClass` / argument type 最终指向新的 live interface `UClass`。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`; `Plugins/Angelscript/Source/AngelscriptTest/Interface/*`; `Plugins/Angelscript/Source/AngelscriptTest/HotReload/*`; `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/*` |
| 预估工作量 | M |
| 风险 | 一旦 interface 依赖真正被传播，现有一些过去“侥幸不重载”的 consumer 会开始被升级为 full reload，需要同步评估对 PIE 内软重载体验的影响。 |
| 前置依赖 | 建议与 Issue-43 一起处理；否则即使 interface 依赖被正确传播到 `ClassData`，模块级调度仍可能把结果降格掉。 |
| 验证方式 | 1. 构造“只引用 interface 类型、不 implements interface”的 consumer 回归，确认 provider reload 时 consumer 被同步重建。 2. 检查 consumer 上对应 `FProperty` / `UFunction` 参数的 `PropertyClass` 是否已切换到新的 interface `UClass`。 3. 对 interface remove 场景再跑一次，确认不会留下引用旧 interface `UClass` 的壳对象。 |

### Issue-45：现有自动化只验证“分析阶段是否建议 full reload”，没有守住“传播后的实际执行闭包”和 interface-typed consumer 壳重建

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/ClassGeneratorTests.cpp`; `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadAnalysisTests.cpp`; `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp`; `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceAdvancedTests.cpp` |
| 行号 | `ClassGeneratorTests.cpp:52-56`; `AngelscriptHotReloadAnalysisTests.cpp:76-365`; `AngelscriptHotReloadScenarioTests.cpp:341-404`; `AngelscriptInterfaceAdvancedTests.cpp:385-409` |
| 问题 | 当前测试矩阵对 reload 判定的覆盖主要停留在“单模块分析输出是什么”。`ClassGeneratorTests.cpp` 里唯一直接调用 `Generator.Setup()` 的断言只是空模块默认 `SoftReload`；`AngelscriptHotReloadAnalysisTests.cpp` 和 `ScenarioTests.cpp` 主要验证 property/super/function/class-add-remove 这些直接改动是否返回 `bWantsFullReload` / `bNeedsFullReload`。相对地，我对 `HotReload` / `Interface` / `ClassGenerator` 测试树做定向检索，`PropagateReloadRequirements`、`PropertyClass`、`GetReturnProperty`、`ChildProperties`、`interface.*property`、`return.*interface`、`parameter.*interface` 全部没有命中；interface 热重载覆盖也只验证 `ImplementsInterface()` 仍为真。也就是说，测试没有任何一条去断言“依赖传播后的 consumer 是否真的被升级执行”“引用 interface 类型但不 implements 它的 consumer，其反射壳是否跟着 provider 一起重建”。 |
| 根因 | 测试设计把 reload contract 近似成“模块自身源码 diff -> 一个枚举/布尔结果”，而没有把 ClassGenerator 真正要保证的两件事纳入断言：一是依赖闭包后的最终调度动作，二是 provider reload 后 consumer 反射壳的一致性。 |
| 影响 | 这正是 Issue-43 / Issue-44 能在现有测试全绿下潜伏的原因。CI 只能证明“分析阶段说了什么”，不能证明“执行阶段按没按这个结果做”，也不能证明 interface provider reload 后 property/parameter/return 的 `UClass` 指针已同步更新。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 ClassGenerator 的可验证 contract 从“分析结果”提升到“最终执行动作 + consumer 反射壳收敛结果”，补一组跨 `Analysis -> Setup -> CompileModuleWithResult` 的闭环测试。 |
| 具体步骤 | 1. 在 `ClassGeneratorTests.cpp` 增加最小闭包测试：构造 provider/consumer 双模块或单模块互依脚本，直接调用 `Generator.Setup()` 后，不只断言 `ReloadRequirement`，还要断言 `NeedsFullReload(Module)` / `WantsFullReload(Module)` 与被传播升级的 consumer 一致。 2. 在 `HotReload` 测试中增加 end-to-end 场景：用 `CompileModuleWithResult(..., ECompileType::SoftReloadOnly, ...)` 触发 provider 结构变化，再断言这轮编译被正确拒绝或升级，而不是只看 analysis helper 返回值。 3. 在 `Interface` 测试中新增“只引用 interface 类型、不 implements interface”的 consumer，用 property、函数返回值、函数参数三种壳分别覆盖；provider interface reload/remove 后，检查对应 `FProperty::PropertyClass`、`UFunction` 参数/返回值上的 interface class 是否更新。 4. 把这些新用例分层放入 `HotReload`、`Interface`、`ClassGenerator` 三个主题目录，不要继续把执行验证挤进纯 analysis test。 5. 为 Issue-43/44 对应场景加入明确断言文本，避免未来有人只修分析结果、没修执行层却再次让测试误报为绿。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/ClassGeneratorTests.cpp`; `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadAnalysisTests.cpp`; `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp`; 建议新增 `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadDependencyTests.cpp`; 建议新增 `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceHotReloadReferenceTests.cpp` |
| 预估工作量 | M |
| 风险 | 新增闭环测试后，短期内很可能把当前隐藏的 reload 分叉直接打红；这是预期行为，需要把修复和测试接入安排在同一迭代内。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 新测试先在当前代码上复现失败，确认确实能抓住 Issue-43/44。 2. 修复后回归，确认 analysis helper、`Setup()`、`CompileModuleWithResult()` 和最终 `PropertyClass` / `ImplementsInterface()` 结果一致。 3. 保留至少一条 body-only 对照用例，确认不会把正常 soft reload 路径误判成结构性失败。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P0 | Issue-43 | Defect | 立即处理；这是 reload 调度真值丢失问题，不修复它，依赖传播得到的高等级结论不会真正进入执行层 |
| P1 | Issue-44 | Defect | 紧随其后；先让 interface-typed consumer 正确进入依赖闭包，避免继续生成引用旧 interface `UClass` 的壳对象 |
| P2 | Issue-45 | Architecture | 在前两项方案落定后补齐；它是防止依赖传播/执行分叉再次回归的验证闭环 |

---

## 发现与方案 (2026-04-09 01:16)

### Issue-46：soft reload 删除类级 `__InitDefaults()` 后不会清空 `DefaultsFunction`，旧 defaults override 会继续污染新 CDO/实例

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` |
| 行号 | `AngelscriptClassGenerator.cpp:4281, 5889-5903`; `ASClass.cpp:1086-1120, 1403-1405, 1456-1458, 1492-1494` |
| 问题 | `DoSoftReload()` 会在复用旧 `UASClass` 后调用 `UpdateConstructAndDefaultsFunctions(ClassDesc, Class)`。这个 helper 每次都会覆盖 `ConstructFunction`，但对 `DefaultsFunction` 只在 `ObjType->GetMethodByDecl(\"void __InitDefaults()\")` 命中且 `objectType == ObjType` 时才赋新值；如果新版本删除了本类自己的 `__InitDefaults()`，代码不会把旧指针清空。随后 `ExecuteDefaultsFunctions()` 只要看到 `DefaultsClass->DefaultsFunction != nullptr` 就继续执行它，Actor/Component/Object 三条构造路径都会在构造后调用这套逻辑。结果是 soft reload 后即使源码已经删除本类 defaults override，旧版本 defaults 仍会继续写入 CDO 和新建实例。 |
| 根因 | `DefaultsFunction` 被当作类级缓存，但刷新逻辑只有“命中新方法就覆盖”，缺少“本类不再拥有 defaults override 时显式清空”的负向分支。 |
| 影响 | `remove __InitDefaults()`、把 defaults 逻辑上移到父类、或把本类 defaults 改成只依赖继承链时，live `UASClass` 会继续执行上一版本类 defaults。直接后果是 CDO 和运行时实例的默认值与当前脚本源码不一致，且该偏差只会在 soft reload 路径出现，最难排查。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 `DefaultsFunction` 刷新改成显式的“先清空、再按当前 `ObjType` 重新绑定”，禁止旧 override 在 soft reload 后残留。 |
| 具体步骤 | 1. 在 `UpdateConstructAndDefaultsFunctions()` 开始处理 `ObjType != nullptr` 分支时，先把 `Class->DefaultsFunction = nullptr`，再执行 `GetMethodByDecl(\"void __InitDefaults()\")` 查询。 2. 保持现有 `objectType == ObjType` 约束，但在未命中时明确保留为 `nullptr`，让 `ExecuteDefaultsFunctions()` 只沿父类链执行当前版本真正存在的 defaults override。 3. 给该 helper 增加一条 defensive log 或 `ensureMsgf`，当 soft reload 前后 `DefaultsFunction` 从“本类 override”切换为“无本类 override”时输出类名，便于确认缓存失效按预期发生。 4. 新增 hot reload 回归：V1 的类定义 `__InitDefaults()` 修改一个 script property；V2 删除该函数或把同样的默认值逻辑移到父类；执行 `SoftReloadOnly` 后断言本类 `DefaultsFunction` 已清空，CDO 与新建实例不再执行旧版本 defaults。 5. 再补一个对照用例：仅修改 `__InitDefaults()` 函数体但保留 override，确认 helper 仍能绑定到新脚本函数，不会把合法 override 误清空。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptTest/HotReload/*`; `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/*` |
| 预估工作量 | S |
| 风险 | 一旦修正缓存失效，过去依赖“旧 defaults override 继续生效”的脚本会暴露真实行为差异，需要同步更新相关热重载测试基线。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 新增 `remove __InitDefaults()` 的 `SoftReloadOnly` 用例，确认 `Class->DefaultsFunction == nullptr`。 2. 比较 reload 前后 CDO 和新建实例的目标属性，确认不再写入旧 defaults。 3. 保留“修改而非删除 `__InitDefaults()`”的对照用例，确认仍绑定到新函数。 |

### Issue-47：soft reload 重建 GC schema 时会把旧 `ReferenceSchema` 继续叠加，引用成员删除或偏移变化后仍保留 stale traversal

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h` |
| 行号 | `AngelscriptClassGenerator.cpp:4037-4040, 4277-4284, 4859-4924`; `ASClass.h:34` |
| 问题 | soft reload 会复用旧 `UASClass` 对象，并在 `DoSoftReload()` 末尾再次调用 `DetectAngelscriptReferences(ClassDesc)`。但这个函数不是从当前脚本布局重建 schema，而是先 `Schema.Append(Class->ReferenceSchema.Get())`，把旧 `ReferenceSchema` 全量拷进 builder，再把当前脚本里的非 `UPROPERTY` 引用成员继续 `EmitReferenceInfo()` 进去。随后只有在 `Schema.NumMembers() != NumPreviousMembers` 或旧成员数为 `0` 时才回写。结果有两条稳定错误路径：一是如果新版本删除了最后一个脚本引用成员，builder 只保留旧 schema，成员数不变，函数根本不会清空 `ReferenceSchema`；二是如果新旧版本引用成员数量相同但偏移/布局变了，旧 schema 会先保留，再叠加一份新 schema，形成重复或 stale offset。 |
| 根因 | `ReferenceSchema` 被当成增量缓存来“追加更新”，而不是每次按当前 `ScriptType` 全量重建；同时回写条件只比较成员数量，无法表达“成员数不变但偏移/类型已变化”或“引用成员被删空”这两类布局变化。 |
| 影响 | 热重载后 GC 仍可能按旧 offset 遍历已经删除或已搬移的脚本引用字段。轻则重复遍历同一批对象、持续放大 schema；重则把新布局中的非对象数据当成对象引用，制造对象保活异常、GC 性能退化，甚至在极端情况下触发非法引用访问。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 script-only GC schema 改成“每轮按当前 `ScriptType` 全量重建并全量替换”，禁止从旧 `ReferenceSchema` 递增追加。 |
| 具体步骤 | 1. 在 `DetectAngelscriptReferences()` 中移除对 `Class->ReferenceSchema.Get()` 的 `Append()` 依赖，改为创建全新的 builder，只根据当前 `ScriptType` 和当前 `ClassDesc` 的非 `UPROPERTY` 引用成员重新发射 schema。 2. 若需要保留 native `AddReferencedObjects` 行为，继续通过 `GetARO(Class)` 传给 `Schema.Build(...)`，不要把旧 script schema 当作 native baseline 复用。 3. 无论当前脚本引用成员数量是否为 `0`，都要在函数末尾显式覆盖 `Class->ReferenceSchema`；当没有 script-only 引用成员时，写回一份空 schema 或调用等价的 reset helper，确保旧 traversal 被清空。 4. 把这段逻辑提取成独立 helper，例如 `RebuildScriptReferenceSchema(UASClass*, const FAngelscriptClassDesc&)`，并让 full reload / soft reload 共用同一条全量构建路径。 5. 新增热重载回归：`A)` 删除最后一个 script-only 引用成员；`B)` 在两个引用成员之间插入普通字段导致 offset 变化；`C)` 保持引用成员数量不变但交换顺序。每个场景都断言 reload 后 schema 成员数与当前脚本一致，不会残留旧 offset。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`; `Plugins/Angelscript/Source/AngelscriptTest/HotReload/*`; `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/*` |
| 预估工作量 | M |
| 风险 | 一旦把 schema 改成全量重建，当前某些“依赖旧 traversal 恰好保活住对象”的隐藏问题会被暴露出来；需要配合测试把真实依赖显式化。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 通过新增回归比较 reload 前后 schema 成员数，确认不会随 soft reload 次数线性增长。 2. 在删除最后一个引用成员的场景下，确认 `ReferenceSchema` 被清空，不再遍历旧 offset。 3. 对 offset 变化场景执行 GC/对象保活回归，确认不会再访问旧位置。 |

### Issue-48：`DefaultComponent` / attach / override / editor-only 等 property metadata 变化只会得到 `FullReloadSuggested`，`SoftReloadOnly` 下 live 组件布局会稳定滞留旧版本

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 行号 | `AngelscriptClassGenerator.cpp:1117-1124, 2081-2093, 2284-2294, 4181-4197, 5214-5444` |
| 问题 | `Analyze()` 对 property metadata 变化的处理只有 `FullReloadSuggested`。但 `ShouldFullReload()` 只有在 `bIsDoingFullReload` 时才把 `FullReloadSuggested` 当成 full reload；在 `SoftReloadOnly` 流程里，这类 class 仍会走 `DoSoftReload()`。问题是，Actor 组件布局的真实构建逻辑只存在于 `FinalizeActorClass()`：它会重新解析 `DefaultComponent`、`RootComponent`、`Attach`、`AttachSocket`、`OverrideComponent`、`EditorOnly` 等 metadata 并填充 `UASClass::DefaultComponents` / `OverrideComponents`。相比之下，`DoSoftReload()` 只会把现有数组中的 `VariableOffset` 按新 property offset 重写，完全不会重建数组或重新解释 metadata。也就是说，只要脚本只改了这些 metadata 而没有触发别的强结构 diff，`SoftReloadOnly` 后 live class 仍沿用旧组件树定义。 |
| 根因 | reload 判定把组件布局 metadata 视为“可建议但非必须”的变化，而执行层又没有任何 soft reload 分支可以重新 materialize `DefaultComponents` / `OverrideComponents`。判定强度与执行能力不匹配。 |
| 影响 | 这会直接制造 CDO 与源码不一致：例如把某个 property 新增为 `DefaultComponent`、把组件改成 `RootComponent`、调整 attach parent/socket、切换 `OverrideComponent` 目标，或修改 `EditorOnly`，在 `SoftReloadOnly` 下都不会进入 live `UASClass`。随后对象构造继续使用旧组件树，验证结果和运行时层级都与当前脚本声明脱节。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 对影响组件布局的 property metadata 引入显式 fingerprint，比对后直接提升为 `FullReloadRequired`；在具备完整 builder 前，不允许这类变更落入 soft reload。 |
| 具体步骤 | 1. 在分析阶段新增专门 helper，例如 `HasActorComponentLayoutMetadataChange(const FAngelscriptClassDesc&, const FAngelscriptClassDesc&)`，只比较会影响 `FinalizeActorClass()` 输出的 metadata：`DefaultComponent`、`RootComponent`、`Attach`、`AttachSocket`、`OverrideComponent`、`EditorOnly`，以及 property 是否从“普通字段”切换成这些角色。 2. 一旦该 helper 命中，就把 `ClassData.ReloadReq` 直接提升到 `FullReloadRequired`，而不是 `FullReloadSuggested`。 3. 在 `DoSoftReload()` 增加 defensive log / assert：如果类是 `AActor` 子类且旧/新组件布局 fingerprint 不同，却仍进入 soft reload，立即标记 `bModuleSwapInError`，避免继续沿用旧数组悄悄运行。 4. 中期配合 Issue-3 的组件布局 builder，把 `FinalizeActorClass()` 的输出收束成可比较的 layout plan；届时可以把上述 helper 改成 plan diff，而不是分散比较 metadata key。 5. 新增回归：分别覆盖“新增/删除 `DefaultComponent`”“切换 `RootComponent`”“改 `AttachSocket`”“修改 `OverrideComponent` 指向”“切换 `EditorOnly`”，并在 `SoftReloadOnly` 下断言系统不会继续保留旧组件布局。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`; `Plugins/Angelscript/Source/AngelscriptTest/HotReload/*`; `Plugins/Angelscript/Source/AngelscriptTest/Component/*`; `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/*` |
| 预估工作量 | M |
| 风险 | 把组件布局 metadata 变化收紧为强制 full reload 后，编辑器内某些原本“表面成功但结果错误”的软重载会改成显式重建或显式失败，需要同步更新工作流预期。 |
| 前置依赖 | 无；若后续推进 Issue-3 的 builder，可把本方案中的 fingerprint helper 收束到统一 layout plan 上。 |
| 验证方式 | 1. 在 `SoftReloadOnly` 下修改 `DefaultComponent`/attach/override metadata，确认系统不再静默沿用旧 `DefaultComponents` / `OverrideComponents`。 2. reload 后检查 CDO 上的组件层级、root 与 attach socket，确认与源码声明一致。 3. 为纯 body-only 变更保留对照用例，确认不会把正常 soft reload 误提升成 full reload。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-47 | Defect | 先修；GC schema stale 会把 hot reload 问题直接放大成对象保活异常和潜在崩溃 |
| P1 | Issue-48 | Defect | 紧随其后；组件布局 metadata 目前在 `SoftReloadOnly` 下稳定失真，会持续制造 CDO/实例状态偏差 |
| P1 | Issue-46 | Defect | 与前两项并行评估；它会让删除后的 defaults override 继续污染新 CDO/实例 |

---

## 发现与方案 (2026-04-09 01:26)

### Issue-49：`AnalyzeEnums()` 把枚举值变化比较写成自比较，纯数值改动会被稳定漏判

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/ClassGeneratorTests.cpp` |
| 行号 | `AngelscriptClassGenerator.cpp:1676-1687`; `ClassGeneratorTests.cpp:52-55` |
| 问题 | `AnalyzeEnums()` 试图在旧枚举存在时比较 `ValueNames` 和 `EnumValues`，但第二个条件被写成了 `EnumData.NewEnum->EnumValues != EnumData.NewEnum->EnumValues`。这是一条永远为假的自比较，所以只有“名字变化”才会把 `ModuleData.ReloadReq` 提升到 `FullReloadSuggested` 并设置 `EnumData.bNeedReload = true`；如果脚本只改枚举底层整数值、名字保持不变，这里不会命中任何变更路径。仓库内现有 `ClassGeneratorTests.cpp` 只验证空模块的 `WantsFullReload()` / `NeedsFullReload()`，没有覆盖 enum value diff。 |
| 根因 | 枚举 diff 逻辑里出现了明显的复制粘贴错误，把“新值数组 vs 旧值数组”的比较写成了“新值数组 vs 新值数组”。同时测试矩阵没有守住这条分析契约。 |
| 影响 | 枚举值重排或显式赋值修改后，ClassGenerator 可能仍把模块当成不需要 enum reload 的软路径处理。结果是运行时 `UEnum`、编辑器显示值和脚本编译产物会继续保留旧数值映射，而源码已经切到新值，直接制造序列化、蓝图分支和数据资产解释不一致。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 修正 enum diff 条件并补最小自动化，确保“名字不变、值变化”也能稳定进入 enum reload 路径。 |
| 具体步骤 | 1. 将 `AnalyzeEnums()` 中的比较改为 `EnumData.NewEnum->EnumValues != EnumData.OldEnum->EnumValues`，与前一行的 `ValueNames` diff 保持对称。 2. 把“枚举名字变化”和“枚举值变化”的判定提取为独立 helper，例如 `HasEnumDefinitionChanged(const FAngelscriptEnumDesc&, const FAngelscriptEnumDesc&)`，避免后续再次在内联条件里出现自比较或漏比较。 3. 在 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/` 增加针对 `Setup()` 的分析回归：构造 old/new module，其中 `ValueNames` 完全一致，但第二个 enumerator 的整数值发生变化；断言 `Generator.WantsFullReload(Module)` 为真，并且对应 `EnumData.bNeedReload` 路径会驱动 `OnEnumChanged` 所需的 full reload。 4. 再补一条对照用例：名字和值都不变时仍保持 `SoftReload`，避免修复后把无差异枚举误判成 reload。 5. 若项目存在依赖 enum value 稳定性的缓存或序列化路径，回归一次枚举变更后的 reload，确认 editor/runtime 看到的是新数值表，而不是旧 `UEnum` 内容。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/ClassGeneratorTests.cpp` |
| 预估工作量 | S |
| 风险 | 修正后会把过去被静默漏掉的枚举值改动升级成显式 reload，短期内可能暴露已有脚本或资产对旧枚举值表的隐式依赖。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 新增“同名不同值”枚举分析测试，确认 `WantsFullReload()` 从 `false` 变为 `true`。 2. 对一份已加载 enum 执行 value-only 修改后的 reload，检查 `UEnum` 的 `GetValueByIndex()` 返回新值。 3. 保留“完全无改动”对照用例，确认不会误报 reload。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-49 | Defect | 立即修复；这是确定性的分析漏判，会让 enum value 改动直接绕过预期 reload 路径 |

---

## 发现与方案 (2026-04-09 01:27)

### Issue-50：existing enum / delegate 的结构变化在 `SoftReloadOnly` 下不会真正物化，执行层会静默沿用旧反射壳

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPropertyTests.cpp` |
| 行号 | `AngelscriptClassGenerator.cpp:1541-1549, 1676-1722, 2096-2113, 2162-2199, 3881-3887`; `AngelscriptHotReloadPropertyTests.cpp:318-370` |
| 问题 | 分析层已经会把 existing enum 的定义变化标成 `EnumData.bNeedReload = true`，把 existing delegate 的签名变化标成 `DelegateData.ReloadReq = FullReloadRequired`。但执行层的两个判定函数都把“真正 full reload”绑定到了 `bIsDoingFullReload`：`ShouldFullReload(FEnumData&)` 只有在 full reload 会话里才对 `bNeedReload` 返回 true；`ShouldFullReload(FDelegateData&)` 也只有在 full reload 会话里才尊重已有 delegate 的 `ReloadReq`。结果是在 `SoftReloadOnly` 会话中，变更过的 existing enum 会走 `LinkSoftReloadClasses()` 直接复用旧 `UEnum`，变更过的 existing delegate 也会直接复用旧 `UDelegateFunction`；`DoFullReload(ModuleData, EnumData)` 和 `CreateFullReloadDelegate()/DoFullReload(ModuleData, DelegateData)` 都不会执行。现有自动化里能直接看到的 enum 场景也只覆盖了 `FullReload`，`AngelscriptHotReloadPropertyTests.cpp` 没有对应的 `SoftReloadOnly` 守护。 |
| 根因 | 分析层与执行层对非 class symbol 的 reload contract 不一致：前者知道 enum/delegate 发生了结构变化，后者却把“是否允许真正 materialize 变化”额外绑定到会话模式，而不是绑定到 symbol 自身的 `ReloadReq`/`bNeedReload`。 |
| 影响 | 这会让模块在 `SoftReloadOnly` 下进入“脚本模块新了，但 UE 反射壳还是旧的”分裂状态。enum 的值表、metadata 和默认值解释会继续读旧 `UEnum`；delegate 的参数/返回值签名仍停留在旧 `UDelegateFunction`，后续 Blueprint、反射调用和序列化对该 delegate 的认知都可能与当前脚本源码不一致，严重时会把真实结构变化伪装成一次表面成功的 soft reload。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 让 enum/delegate 与 class 一样，优先遵守 symbol 自身的 reload 需求；在 `SoftReloadOnly` 下无法安全物化时，必须显式拒绝 swap-in，而不是静默复用旧壳。 |
| 具体步骤 | 1. 重写 `ShouldFullReload(FEnumData&)` 和 `ShouldFullReload(FDelegateData&)`：不要再把 existing symbol 的变更只绑定到 `bIsDoingFullReload`；至少当 `Enum.bNeedReload` 或 `Delegate.ReloadReq >= FullReloadRequired` 时，返回“需要真实 full reload 语义”。 2. 在 `PerformReload(false)` 的 soft reload 分支加入硬屏障：若任一 enum/delegate 命中上述条件，本轮要么升级整个 compile 为 full reload，要么将 `ModuleData.NewModule->bModuleSwapInError = true` 并拒绝继续 swap-in；禁止继续走 `LinkSoftReloadClasses()` 复用旧 `UEnum` / `UDelegateFunction`。 3. 将这条规则接入已有的整体调度真值修复（见 Issue-43 的 module/overall reload 重算），确保 enum/delegate 的结构变化也能影响最终执行模式，而不是只停留在分析结果里。 4. 若产品希望 future 支持“delegate/enum 的安全软物化”，则必须单独实现 `RebuildExistingEnum()` / `RebuildExistingDelegate()` 级别的事务路径，并补齐 Blueprint/serialization 同步；在该能力落地前，不要允许结构变化继续伪装成 soft reload。 5. 新增回归：`A)` existing enum 只改 value/name，使用 `SoftReloadOnly` 编译时应明确失败或被升级为 full reload；`B)` existing delegate 新增参数或修改参数类型时同样不能静默成功；`C)` 保留一条 body-only class 对照用例，确认该收紧不会误伤真正安全的 soft reload。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPropertyTests.cpp`; 建议新增 `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadDelegateTests.cpp` |
| 预估工作量 | M |
| 风险 | 把这类变化从“表面成功”改成“显式升级或显式失败”后，短期内会改变部分开发者对 `SoftReloadOnly` 的预期，但这比继续让旧反射壳和新脚本模块并存更可控。 |
| 前置依赖 | 建议与 Issue-43 一起处理；否则 symbol 级别的新判定仍可能被旧的 module/overall 调度值吞掉。 |
| 验证方式 | 1. 对 existing enum 执行 `SoftReloadOnly` 的 value/name 变化，确认不会继续静默沿用旧 `UEnum`。 2. 对 existing delegate 执行 `SoftReloadOnly` 的签名变化，确认不会继续保留旧 `UDelegateFunction`。 3. 回归 `FullReload` 用例，确认 enum/delegate 的正常重建路径仍然工作。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-50 | Defect | 紧随 Issue-49；否则即使分析层判对 enum/delegate 变化，`SoftReloadOnly` 仍会把执行结果静默留在旧反射壳上 |

---

## 发现与方案 (2026-04-09 01:30)

### Issue-51：reload 决策被拆散在 symbol-specific flag、`ShouldFullReload()` 重载和多段遍历里，ClassGenerator 缺少统一的执行计划层

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 行号 | `AngelscriptClassGenerator.h:149-151, 182-193`; `AngelscriptClassGenerator.cpp:1676-1722, 1872-1905, 2096-2113, 2144-2304` |
| 问题 | 当前 reload policy 没有单一真值。header 里已经能看到三套并行入口：`ShouldFullReload(FClassData&)`、`ShouldFullReload(FEnumData&)`、`ShouldFullReload(FDelegateData&)`，以及三组独立的 `Create/DoFullReload` API。实现里又把状态拆成两种模型：class/delegate 用 `ReloadReq`，enum 用 `bNeedReload`；`Setup()` 只返回模块级聚合值，`PerformReload()` 再分“class 循环”“enum 循环”“delegate 循环”“prepare soft reload 循环”“finalize class 循环”多段遍历。结果是每新增一条 reload 规则，都必须同时改分析聚合、symbol-specific flag、`ShouldFullReload()` 重载和执行循环；任何一处漏同步，就会出现“分析说该重建，执行仍走旧壳”的分叉。Issue-49/50 已经是这种结构漂移的直接体现。 |
| 根因 | ClassGenerator 把“分析结果”“调度策略”“执行动作”耦合在大函数和 symbol-specific 条件分支里，没有显式的 `reload plan` 数据结构去表达“这个 symbol 本轮要 create / link / soft reload / full reload / finalize / rollback 什么”。 |
| 影响 | 这不仅增加维护成本，还会持续放大缺陷发现难度。后续只要再引入一类 symbol、一个新 reload 规则，或调整 `SoftReloadOnly` 行为，就必须人工记住同时修改多处条件与循环；遗漏一次，就会再制造一轮像 enum/delegate、class/module、interface/consumer 这样的执行分叉。对“类型注册顺序、依赖闭包、事务回滚”这类跨 symbol 语义而言，这是明显的扩展性阻碍。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 引入显式 `reload plan` 层，把分析、调度和执行解耦成统一 work-item 模型，消除当前按 symbol kind 手写分支的漂移面。 |
| 具体步骤 | 1. 在 `ClassGenerator` 下新增规划结构，例如 `FASReloadWorkItem` 与 `FASReloadPlan`。每个 work item 至少包含：`SymbolKind(Class/Struct/Enum/Delegate/Interface)`、`QualifiedIdentity`、`RequiredAction(LinkOnly/SoftReload/FullReload/CreateNew/Remove/Abort)`、`NeedsFinalize`、`NeedsCDOInit`、`NeedsPostReloadBroadcast`、`BlockingReason`。 2. 将 `Setup()` 的输出从“散落在 `ClassData` / `EnumData` / `DelegateData` 内的局部 flag”提升为一份统一 plan：分析阶段只负责填充 work item 的需求，不再直接让执行层重新解释 `ReloadReq` / `bNeedReload`。 3. 重写 `ShouldFullReload()` 三个重载为 plan builder 内部规则函数，统一返回 `RequiredAction`，不要再让执行层根据会话模式和 symbol kind 二次猜测。 4. 将 `PerformReload()` 的多段 `for` 循环改成“按阶段执行 plan”：例如 `BuildObjects -> ReloadSymbols -> FinalizeTypes -> InitCDOs -> CommitOrRollback`。阶段调度应按 work item 的显式依赖与 action 决定，而不是继续靠 class/enum/delegate 三组手写循环。 5. 把 `SoftReloadOnly` 的策略也收入口径统一的 planner：当会话模式不允许某个 work item 的 `RequiredAction` 时，planner 直接产出 `Abort` 并附诊断，而不是让执行层悄悄降级到 `LinkOnly`。 6. 让已有的事务化修复（Issue-16）、module/overall 真值修复（Issue-43）、enum/delegate 执行修复（Issue-50）都落到这份 plan 上，避免继续各自新增局部 helper。 7. 新增 planner 级自动化：构造 class、enum、delegate、interface 混合变更矩阵，直接断言生成的 `FASReloadPlan` 是否符合预期；把这类测试放在 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/`，不要只通过最终 compile 成败间接覆盖。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; 建议新增 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASReloadPlan.h/.cpp`; `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/*` |
| 预估工作量 | L |
| 风险 | 这是重构执行骨架的改动，触面广；如果不先补 planner 级测试，容易把现有 happy path 一起打坏。需要分阶段迁移，先引入 plan 层做旁路验证，再逐步让执行主路径切换过去。 |
| 前置依赖 | 建议与 Issue-16、Issue-43、Issue-50 协同设计；这些问题共享同一条“缺少统一 reload plan”根因。 |
| 验证方式 | 1. 对混合变更矩阵生成 planner 快照，确认 class/enum/delegate/interface 的 action 不再由会话模式和 symbol kind 分散决定。 2. 回归现有 hot reload suite，确认 plan 层接管后 body-only soft reload 仍保持工作。 3. 人工构造一次“class + enum + delegate 同轮变更”的场景，确认执行顺序、finalize、CDO 初始化和失败回滚都严格遵守 plan。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-51 | Architecture | 在 Issue-43、Issue-50 定义清楚行为后推进；它是把当前分散的 reload 决策收束成单一执行计划层的根因修复 |

---

## 发现与方案 (2026-04-09 01:39)

### Issue-52：full reload 对 `UASStruct` 和 dynamic delegate 只创建新 head，不回收旧 `_REPLACED_*` 对象，历史反射壳会永久滞留

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 行号 | `ASStruct.h:14-30`; `AngelscriptClassGenerator.cpp:2667-2680, 2720-2732, 3226-3230, 2400-2419, 4990-5035` |
| 问题 | `CreateFullReloadStruct()` 和 `CreateFullReloadDelegate()` 都会先把旧对象改名为 `_REPLACED_*`，再创建一个带 `RF_Public | RF_Standalone | RF_MarkAsRootSet` 的新对象。随后 struct 路径只在 `DoFullReloadStruct()` 把 `ReplacedStruct->NewerVersion` 指向新 struct，reload 收尾也仅在 `2400-2419` 把 `ReplacedStruct->ScriptType` 清空；delegate 路径甚至没有对应的 retirement 步骤。对比 `CleanupRemovedClass()` 可见，当前仅 removed class/struct 会执行 `RemoveFromRoot()` 和 `ClearFlags(RF_Standalone)`，replaced struct 和 replaced delegate 完全没有退场逻辑。 |
| 根因 | class、struct、delegate 的替换策略在“创建新 head”上保持一致，但生命周期管理只为 removed class/struct 做了收口，没有给 replaced struct / delegate 建立对称的 retire 路径。 |
| 影响 | 只要反复 full reload 结构体或 dynamic delegate，包内就会持续累积 rooted 的 `_REPLACED_*` `UASStruct` 和 `UDelegateFunction`。结果包括：内存与对象表线性增长、名字查找与调试枚举被历史壳污染、struct `NewerVersion` 链无限拉长，以及后续热重载继续在越来越脏的反射集合上运行。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为 replaced struct / delegate 增加与 removed class 同级的 retirement 管线，允许短暂过渡但禁止永久 root。 |
| 具体步骤 | 1. 在 `AngelscriptClassGenerator.cpp` 新增显式退场 helper，例如 `RetireReplacedStruct(UASStruct&)` 与 `RetireReplacedDelegate(UDelegateFunction&)`，统一处理 tombstone rename、脚本句柄清理和 root/flag 回收。 2. `CreateFullReloadStruct()` / `CreateFullReloadDelegate()` 只负责登记 replaced 对象到 `PendingRetiredStructs` / `PendingRetiredDelegates`，不要把“是否永久保留旧壳”隐式交给 `_REPLACED_*` 命名。 3. 在 `OnPostReload.Broadcast(...)` 之后、`ForceGarbageCollection(true)` 之前增加一次 commit 阶段：对确认已无 live consumer 必须依赖的 replaced struct / delegate 执行 `RemoveFromRoot()`、`ClearFlags(RF_Standalone)`；若某些路径必须跨过一轮 GC 才能安全释放，则放入 deferred retire 列表并在 post-GC hook 中完成最终清理，而不是永久 rooted。 4. 对 `UASStruct::NewerVersion` 的写入改走统一 helper，retire 旧 struct 时同步校验链尾有效性，避免历史 `_REPLACED_*` struct 持续作为中间节点残留。 5. 补日志和诊断命令：输出当前活跃 `UASStruct` / `UDelegateFunction` 中 `_REPLACED_*` 对象数量，便于回归验证“重复 reload 后对象数不再单调上升”。 6. 新增自动化：连续多轮修改同一个 script struct 和同一个 dynamic delegate 的签名，断言 reload 后 `_REPLACED_*` 对象数保持有界，且最终 `FStructProperty->Struct` / delegate 绑定指向最新 head。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.h`; `Plugins/Angelscript/Source/AngelscriptTest/HotReload/*`; `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/*` |
| 预估工作量 | M |
| 风险 | 过早释放旧 struct / delegate 可能打断仍在持有旧签名壳的反射消费者，因此 retire 时机必须与 reload commit/GC 阶段绑定，不能简单照搬 class removal 的即时清理。 |
| 前置依赖 | 建议与现有版本链/事务化收口方案协同设计，但可独立先做对象退场和计数回归。 |
| 验证方式 | 1. 连续执行多轮 struct/delegate full reload，确认 `_REPLACED_*` 对象数量不再线性增长。 2. reload 后检查相关 `FStructProperty`、`UDelegateFunction` 与广播/绑定路径，确认都指向最新 head。 3. 在 GC 前后各执行一次对象枚举，确认 deferred retire 路径最终能清空历史 rooted 对象。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-52 | Defect | 尽快处理；它会把 repeated full reload 稳定放大成对象表污染和内存增长 |

---

## 发现与方案 (2026-04-09 01:46)

### Issue-53：`ResolveInterfaceClass()` 对 native `UInterface` 的兜底解析只按短名扫全局类表，绑定结果受加载顺序影响

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 行号 | `5062-5108, 5142-5184` |
| 问题 | `FinalizeClass()` 解析 `ImplementedInterfaces` 时，先尝试 script interface 和 bind type；这两步都失败后，会把接口名的前导 `U` 去掉，然后用 `TObjectIterator<UClass>` 线性扫描所有已加载 `UClass`，只要 `GetName() == UnrealInterfaceName` 且带 `CLASS_Interface` 就直接返回。这里既不校验 package/path，也不校验模块来源，更不会检测“存在多个同短名 native interface”这种冲突。返回值随后被直接写进 `NewClass->Interfaces`，并驱动缺失方法校验。 |
| 根因 | native interface 的最后一层解析没有稳定的 canonical identity，只剩“从全局对象表拿第一个同名接口”的加载顺序依赖逻辑。 |
| 影响 | 只要工程里有两个不同模块/插件暴露同短名 `UInterface`，脚本类的 `implements` 结果就会变成非确定性：一次编译可能连到 A，下一次又连到 B。最终表现既可能是把错误接口塞进 `UClass::Interfaces`，也可能按无关接口的方法集合报“缺失方法”，直接破坏类型注册顺序和动态 `UClass` 生成一致性。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 用稳定的 qualified interface identity 替换当前“短名 + 全局扫描”的兜底解析，并把歧义从 silent pick-first 改成显式编译错误。 |
| 具体步骤 | 1. 在 `AngelscriptClassGenerator.cpp` 提取统一 helper，例如 `ResolveNativeInterfaceClass(const FString& InterfaceName, const FAngelscriptClassDesc& OwnerClass)`，内部优先解析显式模块限定名或可唯一映射的 `UClass` object path，禁止直接返回 `TObjectIterator` 的首个命中项。 2. 为 native interface 建立一次性索引，例如按 `/Script/<Module>.<Interface>` 和 display alias 双键缓存；短名只在“唯一命中”时允许使用，一旦命中多个候选就直接 `ScriptCompileError(...)`，错误信息中列出全部候选路径，要求脚本侧改成显式限定名或通过 bind type 注册稳定别名。 3. 将 `FinalizeClass()` 的 `ResolveInterfaceClass` lambda 改成只走三条显式路径：script interface、已注册 bind type、qualified native interface helper；删除当前 `TObjectIterator<UClass>` 的首命中兜底。 4. 为 `ImplementedInterfaces` 的存储补规范化步骤，在分析阶段就把 native interface 名规整成稳定 key，避免 finalize 阶段再次依赖名称启发式。 5. 若兼容性要求必须保留短名写法，则增加诊断日志：短名唯一解析时也记录最终绑定到的 object path，方便发现未来冲突。 6. 新增回归：准备两个不同模块/插件下的同短名 native interface，脚本分别在“短名写法”和“限定名写法”下编译；断言前者稳定报歧义错误，后者稳定绑定到目标接口，并按正确接口方法集合做校验。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`; `Plugins/Angelscript/Source/AngelscriptTest/Interface/*`; `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/*` |
| 预估工作量 | M |
| 风险 | 收紧为“歧义即报错”后，现有依赖短名幸运命中的脚本会开始显式失败，需要提供迁移指引或别名机制。 |
| 前置依赖 | 可独立执行；若后续推进 qualified type identity 总方案，可把本修复直接并入统一键设计。 |
| 验证方式 | 1. 构造同短名 native interface 冲突场景，确认短名写法不再随机命中。 2. 对限定名或显式别名写法回归，确认 `NewClass->Interfaces` 稳定绑定到目标接口。 3. 重复启动编辑器/调整模块加载顺序多次编译，确认结果保持确定性。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-53 | Defect | 在高优先级热重载止血项后处理；先消除 native interface 绑定的非确定性，再补相关自动化 |

---

## 发现与方案 (2026-04-09 01:50)

### Issue-54：现有自动化没有覆盖“interface 删除后的 engine-level 残留”和“同短名 native interface 冲突”，前两条修复缺少回归护栏

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceAdvancedTests.cpp`; `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadAnalysisTests.cpp`; `Plugins/Angelscript/Source/AngelscriptTest/Interface`; `Plugins/Angelscript/Source/AngelscriptTest/HotReload`; `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator` |
| 行号 | `AngelscriptInterfaceAdvancedTests.cpp:327-409`; `AngelscriptHotReloadAnalysisTests.cpp:276-315`; 目录定向 `rg` 统计 |
| 问题 | 已验证事实有两组。其一，`AngelscriptInterfaceAdvancedTests.cpp:327-409` 的 interface hot reload 场景只覆盖 “`UIDamageableHR` 在 V1/V2 都存在，走 full reload 后实现关系仍成立”；它既不删除 interface，也不检查 engine-level `asITypeInfo->UserData` 清理。其二，我对 `Interface` / `HotReload` / `ClassGenerator` 三个测试目录做定向 `rg`，`RegisterObjectType`、`GetUserData(`、`duplicate.*interface`、`same short name` 全部没有命中，说明当前测试树没有任何一条用例去触发 Issue-17 的“删除后注册残留”或 Issue-53 的“同短名 native interface 冲突”。 |
| 根因 | interface 自动化目前主要守正路径：声明成功、`ImplementsInterface()` 为真、同名 interface 在 full reload 后仍可继续实现；没有把“类型注册表是否清干净”和“native interface 解析是否确定”纳入 contract。 |
| 影响 | 即使修完 Issue-17 和 Issue-53，只要没有回归护栏，未来再次引入 stale `UserData` 或短名冲突解析回退，CI 依然会全绿。这会让 interface 类型系统继续在最脆弱的热重载/多模块场景里裸奔。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 增加两组端到端 contract test，把 interface 注册清理和冲突解析从隐含行为提升为显式守护项。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptTest/Interface/` 新增专门用例，例如 `AngelscriptInterfaceRegistryTests.cpp`，覆盖 `interface V1 -> remove interface -> recompile consumer/property/cast`，断言编译失败或得到空解析结果，而不是继续拿到旧 `UClass`。 2. 在同一测试文件或 `ClassGenerator` 测试中新增“同短名 native interface 冲突”场景：准备两个测试用 native `UInterface`，让脚本用短名 `implements`，断言编译期稳定报歧义错误；再用限定名或别名写法断言绑定成功。 3. 将 `AngelscriptHotReloadAnalysisTests.cpp` 扩展为不只验证 `bWantsFullReload/bNeedsFullReload`，还要在 interface delete 场景下验证执行结果不会留下 live stale binding，例如 reload 后 `FindGeneratedClass()` / property materialization 不再返回旧 interface。 4. 为上述用例增加目录级 grep 守护点或明确断言文本，确保未来看到 `RegisterObjectType` / `GetUserData` 回归时能快速定位是“注册残留”还是“解析歧义”。 5. 把新测试分到 `Interface` 与 `HotReload` 两个主题目录，不要继续把这类 contract 混进纯分析测试，保证失败时能明确指向“注册清理”或“解析规则”层。 |
| 涉及文件 | 建议新增 `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceRegistryTests.cpp`; `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceAdvancedTests.cpp`; `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadAnalysisTests.cpp`; `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/*` |
| 预估工作量 | S |
| 风险 | 新测试会把当前潜伏问题直接打红；但这是预期收益，不是副作用。 |
| 前置依赖 | 建议与 Issue-17、Issue-53 同迭代落地，否则新增测试会先稳定失败。 |
| 验证方式 | 1. 删除 interface 后重新编译引用它的脚本，确认测试能捕获 stale `UserData`。 2. 构造同短名 native interface 冲突，确认短名写法稳定失败、限定名写法稳定成功。 3. 多次重复编译和重启后回归，确认测试结果不依赖加载顺序。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-54 | Architecture | 与 Issue-17、Issue-53 配套推进；修复一落地就补回归，避免 interface 类型系统再次回退 |
