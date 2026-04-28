# ClassGenerator 改进计划

## 背景与目标

### 背景

- `ClassGenerator` 当前的主问题已经不是“有没有动态 `UClass` 生成能力”，而是 `soft/full reload`、版本链、`ScriptType` 身份、`DefaultComponent` 布局与 `post-init` 事务边界之间缺少单一 owner，导致多处 correctness 缺陷能在现有主链里稳定到达。
- `Documents/Plans/Plan_AngelscriptEngineBindAndFileWatchValidation.md` 已经覆盖 watcher / 验证矩阵 / broad regression；`Documents/Plans/Plan_UnrealCSharpArchitectureAbsorption.md` 已经覆盖长期依赖图 / manifest / phase graph。本计划不重复那两份活跃 Plan 的范围，只聚焦 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/` 内当前源码仍成立的 runtime correctness 改进项。
- 本轮只纳入五个被多维交叉确认、且已在源码中复核仍存在的问题：版本链生命周期、脚本类型身份与 `ReferenceSchema` owner、`soft reload` 语义收口、`DefaultComponent` layout 单一真相、ctor/defaults/post-init 事务化。

### 目标

- 收口 `replace/remove/recreate` 的版本链规则，消除 `_REPLACED_*` / removed class 对 canonical 名与 latest resolve 的持续污染。
- 让 `ScriptType` 身份、`ReferenceSchema` 写入与移除路径有单一 owner，避免 raw `UserData` 和 `UASClass` 副本长期分裂。
- 明确 `soft reload` 的安全边界：`defaults`、`tick`、`ConfigName/DefaultConfig`、结构性变更不再静默落入旧 `UASClass` 复用路径。
- 把 actor 组件布局收口为单一 plan/builder，避免 `Finalize` / `Verify` / runtime apply / offset rebind 四处各维护一份组件真相。
- 让 ctor/defaults/literal asset `post-init` 只在脚本执行成功后提交结果，禁止半初始化对象、半初始化 asset 和错误 `post-init` 命中继续污染 live state。

## 范围与边界

- 本计划只覆盖 `ClassGenerator` 当前 correctness 修复与紧邻的测试入口，不展开 `DirectoryWatcher`、性能采样、`HotReload request/service` 外层控制面重构。
- 本计划不重复长线 `dependency graph / manifest / artifact generator` 设计；若某条任务需要 registry/helper，会以“先止血 correctness，再给后续长线计划留 owner 接缝”为原则。
- 每个任务都配套 `-T` 测试项；优先吸收 `Documents/AutoPlans/TestCoverage/ClassGenerator_TestGaps.md` 已明确标出的缺口，而不是重造一套脱离现有 harness 的新测试体系。

## 分析来源

| 分析文档 | 关键发现 |
| --- | --- |
| `Documents/AutoPlans/ClassGenerator_Analysis.md` | 指出 `CDONoDefaults` 基线污染、`DefaultComponent` soft reload 不安全、版本链 rooted leak、`ScriptTypePtr/ReferenceSchema` owner 分裂、ctor/defaults 执行失败不回滚等高风险 correctness 问题 |
| `Documents/AutoPlans/DiscoveryPlans/ClassGenerator_Plan.md` | 给出 tombstone/version-chain owner、`DefaultComponent` layout builder、`soft reload` gate、tick/config/defaults 语义、literal asset/post-init 事务化等可执行修复方案 |
| `Documents/AutoPlans/TestCoverage/ClassGenerator_TestGaps.md` | 指出 version chain/CDO、一致性、`RuntimeDestroyObject()`、`GetConstructingASObject()`、literal asset reload、struct/class reload 语义等仍缺 targeted automation |
| `Documents/AutoPlans/ArchitectureReview/TypeSystem_ArchReview.md` | 说明 `ScriptTypePtr/GetUserData` 双向裸指针与 `ReferenceSchema`/type identity 缺少单一 owner，后续扩展与 hot reload 都会继续踩 stale state |
| `Documents/AutoPlans/ArchitectureReview/HotReloadArch_ArchReview.md` | 说明版本链仍是 call-site 责任，`CDONoDefaults` 差异法与 late validation 缺少事务边界，`soft reload`/state bridge 需要更明确的 owner |
| `Documents/AutoPlans/ReferenceComparison/GapAnalysis.md` | D4 维度明确指出当前不缺 reload 框架，缺的是 typed registry、rebind/observer 与 raw `UserData` 生命周期收口 |
| `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` | D2/D4 对比表明最值得吸收的是 weak registry、graph-before-fanout、reload 完成后的显式 consumer/rebind，而不是推倒现有 `ReloadRequirement` 梯度 |

## 影响范围

本计划预计涉及以下操作：

- **版本链收口**：把 `CreateFullReloadClass()` / `CleanupRemovedClass()` / `GetMostUpToDateClass()` 的 replace/remove/latest 规则收进统一 owner。
- **类型身份与 schema owner 收口**：把 `ScriptTypePtr`、`bIsScriptClass`、`ReferenceSchema`、raw `SetUserData()` 的写入/失效路径整理为统一 registry/adapter。
- **soft reload semantic gate**：把 `defaults`、`tick`、`ConfigName/DefaultConfig` 与 structural change 从 `soft reload` 安全集合里剔除或显式拒绝。
- **component layout builder**：让 `DefaultComponent` / `OverrideComponent` 的校验、布局、runtime apply 与 reload offset rebind 消费同一份 plan。
- **事务化执行与回滚**：让 ctor/defaults/`PostInitFunctions`/literal asset 只有在脚本执行成功后才提交结果。

按目录分组的主要文件如下：

- `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/`（7 个现有文件 + 3 组新增 helper 候选）
  - `AngelscriptClassGenerator.cpp` / `.h` — 版本链写点、`soft/full reload` 判定、component layout、`CallPostInitFunctions()`、`InitDefaultObjects()`
  - `ASClass.cpp` / `.h` — `GetMostUpToDateClass()`、对象构造、`DefaultsFunction`/`ConstructFunction` 执行、`RuntimeDestroyObject()`
  - `ASStruct.cpp` / `.h` — struct latest resolve 与 identity mirror
  - 新增候选：`ASVersionChain.h/.cpp`、`ASScriptTypeRegistry.h/.cpp`、`ASActorComponentLayout.h/.cpp`
- `Plugins/Angelscript/Source/AngelscriptRuntime/Core/` 与 `Preprocessor/`（3-4 个现有文件）
  - `AngelscriptEngine.h` / `.cpp` — reload 结果、module swap error、必要的 registry / diagnostic 接缝
  - `AngelscriptManager.h` 或 `FAngelscriptModuleDesc` 定义文件 — `PostInitFunctions` 结构化 key
  - `AngelscriptPreprocessor.cpp` — literal asset getter 模板与 `post-init` 描述
- `Plugins/Angelscript/Source/AngelscriptTest/`（5 个新增或拆分测试文件）
  - `HotReload/AngelscriptHotReloadVersionChainTests.cpp`
  - `ClassGenerator/AngelscriptASClassIdentityTests.cpp`
  - `HotReload/AngelscriptHotReloadClassSemanticsTests.cpp`
  - `ClassGenerator/AngelscriptActorComponentLayoutReloadTests.cpp`
  - `ClassGenerator/AngelscriptClassGeneratorFailureTests.cpp`
- `Documents/Guides/TestCatalog.md`
  - 登记新增 `ClassGenerator/` 与 `HotReload/` 测试文件、代表性 case 与推荐过滤前缀

## 分阶段执行计划

### Phase 1：收口 runtime owner 与版本链生命周期

- [ ] **P1.1** 收口 class version chain 的 replace/remove/recreate 生命周期
  - 当前 `CreateFullReloadClass()`、`CleanupRemovedClass()` 与 `UASClass::GetMostUpToDateClass()` 仍靠 canonical 名查找、root flag 与裸 `NewerVersion` 指针协作，导致 removed class 在 GC 前可被误判成 replace 目标，`_REPLACED_*`/removed head 也没有统一 tombstone 语义。
  - 这一项只处理当前 runtime correctness owner，不复述现有活跃 Plan 中“长线依赖图”或“广义 watcher 验证矩阵”的范围；目标是先把 latest resolve、canonical name、GC 可见性和 replace/remove 状态统一到单一 helper/module。
  - 落地时优先引入 `ASVersionChain` 或等价 helper，统一托管 `RegisterReplacement`、`RegisterRemoval`、`ResolveLatest`、tombstone rename 与 weak-link 校验；`CreateDefaultComponents()`、`ApplyOverrideComponents()`、`TSubclassOf` 相关 consumer 只通过 helper 解析最新类，不再自行追裸链。
  - 来源：
    - [A] `ClassGenerator_Analysis.md` — “full reload 的版本链只增不减，`_REPLACED_*` 类对象会被永久 root 住”
    - [B] `ClassGenerator_Plan.md` — “remove -> recreate same name 会被误接到版本链；版本链生命周期缺少单一 owner”
    - [C] `ClassGenerator_TestGaps.md` — “full reload 后 `UASClass` 版本链与 CDO 一致性测试缺失；rename 用例未验证旧 canonical 名真正退场”
    - [D] `HotReloadArch_ArchReview.md` — “版本链仍是显式调用点责任，遗漏 `GetMostUpToDateClass()` 就会继续看到旧类”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` L2573-L2586、L4990-L5024，`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` L912-L923 — 当前仍按 canonical 名 `FindObject<UASClass>` 认定 `ReplacedClass`，removed class 只在 cleanup 时去 root，latest resolve 仍线性追裸 `NewerVersion`
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASVersionChain.h/.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadVersionChainTests.cpp`
- [ ] **P1.1** 📦 Git 提交：`[ClassGenerator/HotReload] Fix: normalize version-chain lifecycle and tombstones`
- [ ] **P1.1-T** 单元测试：补齐 version chain、rename 与 remove/recreate 的 targeted regression
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadVersionChainTests.cpp`
  - 测试场景：
    - 正常路径：`V1 -> V2` full reload 后，`OldClass->GetMostUpToDateClass()` 指向 `NewClass`，旧 canonical 名退场，新 `CDO`/新实例读取到新默认值
    - 边界条件：`remove -> recreate same name before GC` 不会把新类挂到旧 removed head；struct full reload 的 `NewerVersion` 也能正确切到新 head
    - 错误路径：tombstone/removed 历史指针不会把 `ResolveLatest` 导向逻辑无关的新类；旧 canonical 名和 module record 不再残留双注册
  - 测试命名：`Angelscript.TestModule.HotReload.VersionChain.<CaseName>`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P1.1-T** 📦 Git 提交：`[ClassGenerator/HotReload] Test: cover version-chain replacement removal and recreate`

- [ ] **P1.2** 收口 `ScriptType` 身份与 `ReferenceSchema` 的单一 owner
  - 当前 `UASClass` 仍自己持有 `ScriptTypePtr`、`bIsScriptClass`、`ReferenceSchema` 副本，而类生成器、GC schema 构建与对象分配路径仍直接混用 `Class->ScriptTypePtr`、`ScriptType->GetUserData()` 与 `Class->ReferenceSchema`；这一项的目标是先修 correctness，不把它膨胀成长线 `manifest/catalog` 项目。
  - 需要引入 engine-owned `FAngelscriptScriptTypeRegistry` 或等价 typed slot，把 `ScriptType -> UField -> generation/revision` 变成单一真相；`ClassGenerator` 只通过 registry 注册/失效，`ASClass.cpp` 的 allocation / destroy / lookup 走 registry/adapter，不再直接解 raw `plainUserData`。
  - 同时要改写 `DetectAngelscriptReferences()` 的 schema 写入方式，禁止“先 append 旧 `ReferenceSchema` 再 build”的累积模式；removed class/struct 清理时也必须显式 unregister typed slot 与 schema owner。
  - 来源：
    - [A] `ClassGenerator_Analysis.md` — “`ReferenceSchema` 与 `ScriptTypePtr/bIsScriptClass` 发生基类/派生类字段分裂，script-only schema 与脚本类身份都会写到错误副本”
    - [C] `ClassGenerator_TestGaps.md` — “`RuntimeDestroyObject()` 的析构 contract 目前零覆盖；script lifecycle 没有 targeted automation”
    - [D] `TypeSystem_ArchReview.md` — “脚本类型身份依赖 `ScriptTypePtr` / `GetUserData()` 双向裸指针回填，热重载与扩展共享同一条隐式接缝”
    - [E] `GapAnalysis.md`、`CrossComparison.md` D4 — “raw `UserData` 缺少 unregister owner，参考插件更接近 weak registry / typed slot，而不是匿名 live pointer cache”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h` L29-L35，`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` L3291-L3297、L3678-L3680、L4200-L4203、L4875-L4924，`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` L1037-L1048 — 当前 `UASClass` 仍保存 identity/schema 副本，class generator 持续直写这些副本并依赖 `ScriptType->GetUserData()` 反查 live class
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASScriptTypeRegistry.h/.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASClassIdentityTests.cpp`
- [ ] **P1.2** 📦 Git 提交：`[ClassGenerator/Core] Fix: unify script-type identity and schema owner`
- [ ] **P1.2-T** 单元测试：补齐 script type identity、schema rebuild 与析构路径 regression
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASClassIdentityTests.cpp`
  - 测试场景：
    - 正常路径：class/struct/delegate 在 compile 与 full reload 后都能完成 `ScriptType -> UE field -> latest field` roundtrip，`RuntimeDestroyObject()` 在 GC 路径被正确触发
    - 边界条件： repeated soft/full reload 不会重复累积 script-only schema，也不会留下 stale identity slot；Blueprint child 或派生类读取到的仍是最新 head
    - 错误路径：removed class/old `ScriptType` 不能继续通过 raw `UserData` 解析到 live class；卸载后再次 lookup 应稳定失败而不是命中旧副本
  - 测试命名：`Angelscript.TestModule.ClassGenerator.ASClassIdentity.<CaseName>`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P1.2-T** 📦 Git 提交：`[ClassGenerator] Test: cover script-type registry schema and destroy-path identity`

### Phase 2：收口 soft reload 语义与 component layout 单一真相

- [ ] **P2.1** 统一 `soft reload` 准入，收口 `defaults/tick/config` 的类级语义漂移
  - 当前 `soft reload` 对类级语义仍是双轨实现：`PrepareSoftReload()` 用进程级开关 + `RF_ArchetypeObject` 造 `CDONoDefaults` 基线，`DoSoftReload()` 一边保留旧 `DefaultsCode` 一边又刷新 `DefaultsFunction`，`tick` 缓存只在 full reload 前重算，`ConfigName/DefaultConfig` 只在 create/full path 写入。
  - 这一项要把 `FullReloadRequired`、`defaults` 语义变化、`Tick/ReceiveTick` no-op 翻转、`ConfigName/DefaultConfig` 变化统一折叠进一个 `HasSoftReloadUnsafeSemanticChange` / `RequiresClassReinstancing` helper；命中后不得进入 `PrepareSoftReload()` / `DoSoftReload()`，只能 full reload 或在 `SoftReloadOnly` 会话里显式拒绝 swap-in。
  - 对仍允许 `soft reload` 的真正 body-only case，需改成 object-scoped/thread-local defaults suppression，并保证不会把新 `DefaultsFunction`、新 tick/config 语义偷偷切给 live class；目标是让旧实例、新实例与 `CDO` 看到一套一致 contract。
  - 来源：
    - [A] `ClassGenerator_Analysis.md` — “`CDONoDefaults` 基线会被默认组件/defaults 污染；删除子类 `__InitDefaults()` 会留下旧 `DefaultsFunction`；tick/config 变化不会被正确重载”
    - [B] `ClassGenerator_Plan.md` — “`defaults` 双轨制、tick 缓存不刷新、`Config=<Name>/DefaultConfig` 变更不进入正确 reload lane、`GConstructASObjectWithoutDefaults` 是进程级共享开关”
    - [C] `ClassGenerator_TestGaps.md` — “`SoftReload.CDOAndInstanceConsistency` 缺失；现有 defaults 覆盖只验证局部 happy path”
    - [D] `HotReloadArch_ArchReview.md` — “当前状态迁移以 `CDONoDefaults` 差异法为核心，但没有统一的 runtime state bridge；类级 derived state 与 reload 生命周期脱节”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` L4093-L4108、L4140-L4141、L4281-L4284、L5815-L5824、L5889-L5903，`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` L1359-L1469，`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` L3294-L3308、L4208-L4242 — 当前 `soft reload` 仍用全局 defaults 抑制、保留旧 `DefaultsCode` 却更新 `DefaultsFunction`，tick/config 语义也没有统一 gate
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadClassSemanticsTests.cpp`
- [ ] **P2.1** 📦 Git 提交：`[ClassGenerator/HotReload] Fix: gate unsafe soft-reload class semantics`
- [ ] **P2.1-T** 单元测试：补齐 `defaults/tick/config` 的 class semantic regression
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadClassSemanticsTests.cpp`
  - 测试场景：
    - 正常路径：纯函数 body 改动保持同一个 live `UClass`，且 `CDO`、旧实例、新实例对默认值与行为的观察一致
    - 边界条件：`__InitDefaults()` 新增/删除、`Tick/ReceiveTick` no-op 翻转、`Config=Game -> Config=Engine` 与 `DefaultConfig` 增删都会稳定升级成 full reload 或在 `SoftReloadOnly` 下被明确拒绝
    - 错误路径：构造 `CDONoDefaults` 时只有目标临时对象跳过 defaults；无关对象与其它线程对象不得误中 suppression
  - 测试命名：`Angelscript.TestModule.HotReload.ClassSemantics.<CaseName>`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P2.1-T** 📦 Git 提交：`[ClassGenerator/HotReload] Test: cover defaults tick and config semantic reload rules`

- [ ] **P2.2** 把 `DefaultComponent/OverrideComponent` 收口为单一 layout plan，并前移 hard validation
  - 当前组件布局概念被拆在 `FinalizeActorClass()`、`VerifyClass()`、`DoSoftReload()`、`CreateDefaultComponents()`、`ApplyOverrideComponents()` 五处，`DefaultComponents/OverrideComponents` 两个裸数组既承载编译期 plan，又承载 runtime 物化输入，导致 structural edit、rename、attach/root 合法性和 offset rebind 都容易漂移。
  - 这项要引入 `FASActorComponentLayout` 与 builder，把 root/attach/override/editor-only/NotAngelscriptSpawnable 等语义收成单一 plan；`FinalizeActorClass()` 只构建 plan，`VerifyClass()` 与 runtime apply 消费同一 plan，不再各自重扫 CDO/反射壳。
  - 对 component property rename/delete、script component ancestry、attach/root 非法配置等 structural change，必须在 pre-swap validation 就给出 deterministic error 或 full reload 决策，不能继续落入 `DoSoftReload()` 里的 `check(Property != nullptr)` offset rebind 路径。
  - 来源：
    - [A] `ClassGenerator_Analysis.md` — “带 `DefaultComponent` 的 actor 无法安全 soft reload；移除或重命名组件承载 property 时 `DoSoftReload()` 会直接命中断言”
    - [B] `ClassGenerator_Plan.md` — “`DefaultComponent/OverrideComponent` 布局在四条路径重复维护，缺少单一 builder；script component ancestry helper 也会在 reload 后误判”
    - [D] `HotReloadArch_ArchReview.md` — “late validation 不是原子失败，依赖真实 CDO 的 component tree 错误应尽量前移成 pre-swap audit”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` L4181-L4197、L5214-L5444、L5467-L5695 — 当前 soft reload 仍手工遍历组件数组做 offset rebind 并 `check(Property != nullptr)`，`FinalizeActorClass()` 与 `VerifyClass()` 分别在不同阶段重建/验证组件布局
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASActorComponentLayout.h/.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptActorComponentLayoutReloadTests.cpp`
- [ ] **P2.2** 📦 Git 提交：`[ClassGenerator] Refactor: centralize actor component layout plan`
- [ ] **P2.2-T** 单元测试：补齐 component layout reload 与 pre-swap validation regression
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptActorComponentLayoutReloadTests.cpp`
  - 测试场景：
    - 正常路径：含 `DefaultComponent` / `OverrideComponent` / `RootComponent` / `Attach` 的 actor class 能稳定编译、spawn，并在 reload 后由同一 layout plan 驱动 runtime materialization
    - 边界条件：component property rename/delete、script `USceneComponent/UActorComponent` 继承链变化、root/attach 组合变化会稳定触发 full reload 或 deterministic pre-swap validation
    - 错误路径：缺失 attach parent、abstract override、`NotAngelscriptSpawnable`、非法 editor-only root 之类问题只产生日志/compile error，不进入 runtime `check(Property != nullptr)` 崩溃
  - 测试命名：`Angelscript.TestModule.ClassGenerator.ComponentLayout.<CaseName>`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P2.2-T** 📦 Git 提交：`[ClassGenerator] Test: cover component-layout reload and validation contracts`

### Phase 3：事务化 ctor/defaults/post-init 与 literal asset 初始化

- [ ] **P3.1** 让 ctor/defaults/`PostInitFunctions` 只在脚本执行成功后提交结果
  - 当前 `ExecuteConstructFunction()`、`ExecuteDefaultsFunctions()` 与 `CallPostInitFunctions()` 都只做 `Prepare...` 检查后直接 `Context->Execute()`，没有把执行结果回传给对象构造、literal asset 缓存或 module swap 事务；同时 `PostInitFunctions` 仍按裸短名查找 getter，且执行顺序先于 `InitDefaultObjects()`。
  - 这条任务要把“对象/asset shell materialize”与“用户脚本初始化”拆开：只要 ctor/defaults/`__Init_*`/`__PostLiteralAssetSetup` 任一失败，就清理临时对象或临时 asset、恢复构造上下文、设置 `bModuleSwapInError` 并阻止后续提交；`PostInitFunctions` 改成带 namespace/declaration 的结构化 key，禁止不同 getter 仅靠短名碰撞。
  - 同时要给 `CurrentObjectInitializers`/`GetConstructingASObject()` 一条更稳的 RAII/clear path，保证异常、早退和 nested construct 都不会把半初始化上下文留给下一次构造；用户 `post-init` 需后移到 `InitDefaultObjects()` 与最终 class verification 之后，不能继续读取 half-initialized `CDO/tick` 状态。
  - 来源：
    - [A] `ClassGenerator_Analysis.md` — “script ctor/defaults 执行失败不会中止对象构造或 soft reload 重建；现有自动化没有覆盖构造/defaults 异常与 GC schema 相关回归”
    - [B] `ClassGenerator_Plan.md` — “`CallPostInitFunctions()` 忽略执行结果，literal asset 会缓存半初始化对象；`PostInitFunctions` 只按裸短名调度；用户 `__Init_*` 运行在 `InitDefaultObjects()` 之前”
    - [C] `ClassGenerator_TestGaps.md` — “`GetConstructingASObject()`、`RuntimeDestroyObject()` 与 literal asset reload 相关 contract 当前都缺 targeted regression”
    - [D] `HotReloadArch_ArchReview.md` — “constructor replay 会重放非属性副作用，当前没有对象级 HMR hook 与 prepare/finish 事务边界”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` L987、L1077-L1171，`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` L2299-L2304、L5775-L5800 — 当前构造上下文仍依赖进程级 `CurrentObjectInitializers` 栈，ctor/defaults 与 `post-init` 都忽略 `Execute()` 返回值，且 `CallPostInitFunctions()` 先于 `InitDefaultObjects()`
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptManager.h` 或 `FAngelscriptModuleDesc` 定义文件、`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptClassGeneratorFailureTests.cpp`
- [ ] **P3.1** 📦 Git 提交：`[ClassGenerator/Preprocessor] Fix: make ctor defaults and post-init transactional`
- [ ] **P3.1-T** 单元测试：补齐 ctor/defaults/post-init/literal asset 的 failure regression
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptClassGeneratorFailureTests.cpp`
  - 测试场景：
    - 正常路径：ctor/defaults/`__Init_*` 成功时，对象和 literal asset 只提交一次，后续 lookup 返回已完全初始化的实例
    - 边界条件：同 module 下两个 getter 共享短名但位于不同 namespace 时，`PostInitFunctions` 能精确命中目标；`__Init_*` 读取 `StaticClass()`/`GetDefaultObject()` 时看到的是 `InitDefaultObjects()` 之后的稳定状态
    - 错误路径：`__Init_*`、`__PostLiteralAssetSetup`、ctor/defaults 任一抛异常或执行错误时，不缓存 dirty asset、不保留 half-initialized object、`GetConstructingASObject()` 构造后返回 `nullptr`，module 被标成失败而不是继续 swap-in
  - 测试命名：`Angelscript.TestModule.ClassGenerator.Transaction.<CaseName>`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P3.1-T** 📦 Git 提交：`[ClassGenerator] Test: cover transactional ctor defaults and literal-asset post-init`

## 单元测试总览

| 改进项 | 测试文件 | 测试场景 | 优先级 |
| --- | --- | --- | --- |
| `P1.1` | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadVersionChainTests.cpp` | replace/remove/recreate、canonical name 退场、latest resolve、struct/class version chain | P0 |
| `P1.2` | `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASClassIdentityTests.cpp` | `ScriptType -> UField` roundtrip、schema rebuild、removed identity 失效、`RuntimeDestroyObject()` | P0 |
| `P2.1` | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadClassSemanticsTests.cpp` | body-only safe reload、defaults/tick/config semantic gate、CDO/old/new instance consistency | P0 |
| `P2.2` | `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptActorComponentLayoutReloadTests.cpp` | component layout plan、rename/delete structural change、pre-swap validation | P1 |
| `P3.1` | `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptClassGeneratorFailureTests.cpp` | ctor/defaults failure、literal asset `post-init` transaction、qualified getter lookup | P1 |

## 验收标准

1. `remove -> recreate same name`、`replace -> remove`、rename/full reload 不再污染 canonical 名与 latest resolve；`_REPLACED_*` / removed class 不需要靠永久 rooted 历史节点维持版本链可用性。
2. `ScriptType` 身份、schema rebuild 与 removed/unload 清理有单一 owner；soft/full reload 后不会继续依赖 stale `UserData` 或重复累积旧 `ReferenceSchema`。
3. `defaults`、`tick`、`ConfigName/DefaultConfig`、structural change 等 class semantic 变更不会再静默走 `soft reload`；body-only 安全变更仍可保留现有 fast path。
4. `DefaultComponent/OverrideComponent` 的校验、layout、runtime apply 与 reload offset rebind 已收口到同一 plan/builder；非法 component tree 在 pre-swap 就能稳定报错，不再靠 runtime `check` 暴露。
5. ctor/defaults/`PostInitFunctions`/literal asset 只在脚本执行成功后提交；失败时不会留下 dirty asset、half-initialized object 或脏构造上下文。
6. 上述五个改进项都已有独立 automation 文件和 `TestCatalog` 登记，且测试命名遵循 `Angelscript.TestModule.<Category>.<TestName>`、隔离方式统一为 `FAngelscriptEngineScope`。

## 风险与注意事项

### 风险

1. **类型身份 owner 收口会同时触碰构造、GC、debug、lookup 多条链路**
   - 缓解：先保留兼容 mirror/cached field，一个阶段只替换写点与 lookup helper，等 targeted tests 稳定后再删除 legacy 直写。
2. **版本链 tombstone/weak link 会影响依赖对象名做调试或辅助查找的现有工具**
   - 缓解：统一提供 latest/tombstone-aware helper，并在日志里输出 old/new/tombstone name，避免脚本或测试继续直接按历史对象名猜测。
3. **将 `defaults/tick/config` 从 `soft reload` 安全集合里剔除会降低部分 body-only 编辑回路速度**
   - 缓解：先以 correctness 为先，保留纯 body-only fast path；待 class semantic contract 收口后，再单独评估更细粒度 `method patch reload`。
4. **component layout builder 可能提前暴露一批此前被 runtime 偶然吞掉的 component tree 错误**
   - 缓解：把这类变化视为“更早失败”的正向收敛，并配套明确诊断与迁移说明，而不是保留当前 `check`/late failure。
5. **literal asset/post-init 改成事务式与后置执行后，脚本可观察到的初始化时机将更严格**
   - 缓解：把 timing change 明写进验收与已知行为变化，并优先补“读取 `StaticClass()/CDO/tick` 必须在 barrier 之后”的 regression。

### 已知行为变化

1. **部分过去会勉强成功的 `soft reload` 将改成 full reload 或显式拒绝 swap-in**
   - 影响面：`__InitDefaults()` 新增/删除、`Tick/ReceiveTick` 语义变化、`ConfigName/DefaultConfig` 变化、component property rename/delete、其它 `FullReloadRequired` structural change。
2. **removed/replaced class 会更早离开 canonical 名称空间**
   - 影响面：依赖旧对象名或旧 module entry 的调试脚本/测试 helper 需要改成 latest/tombstone-aware 查询。
3. **literal asset 的用户 `__Init_*` / `__PostLiteralAssetSetup` 不再允许在 `InitDefaultObjects()` 之前观察半初始化类状态**
   - 影响面：任何依赖早期 `StaticClass()`、`GetDefaultObject()`、tick flag 或 component tree 的脚本初始化代码都需要迁到 barrier 之后。

---

## 本轮追加（2026-04-09）

- 上一轮 `P2.1/P3.1` 已经覆盖 `soft reload` 语义收口与 ctor/defaults/post-init 事务边界，但五维输入里仍有三类独立且高风险的 correctness 缺口还没有被显式拆成执行项：`soft reload` 的 live-class / blueprint-child 守卫、脚本构造上下文的 `thread_local + RAII` owner、以及无默认 ctor 的显式失败合同。
- 本轮只补这三项，不重复前一轮已经记录的版本链、`ScriptType` identity、component layout 和 literal asset transaction 主条目。

### Phase 2 补充：soft reload live-class 与 Blueprint child 守卫

- [ ] **P2.3** 统一 `soft reload` 的 live-class 前置条件与派生 `BlueprintGeneratedClass` runtime-state 同步
  - 当前 `PrepareSoftReload()` / `DoSoftReload()` 与 blueprint-child 同步路径共享了同一种过强假设：`OldClass->Class` 一定还是 live `UASClass`，而所有派生 `UBlueprintGeneratedClass` 也都能被当成 `UASClass` 直接写 `ScriptTypePtr`。这两条假设在 `DiscardModule()`、body-only reload + blueprint child、以及类壳失效后再次 soft reload 时都会直接落成崩溃路径。
  - 这项补充不重做 `P1.2` 的 identity owner 主线，而是先在 `soft reload` 边界加一层 shared helper：一边把“旧类壳是否仍可复用”统一成 `TryGetLiveOldClass()` 之类的单入口，一边把 blueprint child 的 runtime-state 更新收进 `SyncDerivedBlueprintRuntimeState()` 之类的专用 helper，禁止继续 `Cast<UASClass>(UBlueprintGeneratedClass)` 后直接解引用。
  - 命中 `OldClass->Class == nullptr`、类壳已失效、或 child class 不是可写脚本壳时，必须稳定退回 `full reload` / brand-new materialization / diagnostic skip，而不是继续进入 `PrepareSoftReload()` 的 `NewObject(..., Class)` 或 `ensure(asClass->ScriptTypePtr == OldScriptType)` 路径。
  - 来源：
    - [A] `ClassGenerator_Analysis.md` — “`DoSoftReload()` 把 `UBlueprintGeneratedClass` 当成 `UASClass` 解引用，存在直接崩溃路径”
    - [B] `ClassGenerator_Plan.md` — “`PrepareSoftReload()` / `DoSoftReload()` 对 `OldClass->Class == nullptr` 缺少统一防护，`DiscardModule` 后会落入空类崩溃”
    - [B] `ClassGenerator_Plan.md` — “blueprint child 的 `ScriptType` / schema 同步需要专用 helper，不能继续 `Cast<UASClass>(UBlueprintGeneratedClass)`”
    - [C] `ClassGenerator_TestGaps.md` — “`ScriptClass.BlueprintChildCompiles` 断言过弱，`HotReload.DiscardModule` 也没有锁住 reload/discard 后的旧类型失效态”
    - [D] `HotReloadArch_ArchReview.md` — “`DoSoftReload()` 会改写派生 `UBlueprintGeneratedClass` 的 flag / schema / token stream，但 soft mutation owner 仍分散在 call-site”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` L4087-L4102、L4115-L4133、L4287-L4315 — 当前 `PrepareSoftReload()` / `DoSoftReload()` 仍只检查 `OldClass.IsValid()` 后直接取 `ClassData.OldClass->Class`，派生 blueprint 同步仍在 `Cast<UASClass>(CheckClass)` 成功前提下访问 `asClass->ScriptTypePtr`
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadBlueprintSyncTests.cpp`
- [ ] **P2.3** 📦 Git 提交：`[ClassGenerator/HotReload] Fix: guard live-class lookup and blueprint-child soft reload sync`
- [ ] **P2.3-T** 单元测试：补齐 `DiscardModule` 失效态与 blueprint child body-only reload 的安全回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadBlueprintSyncTests.cpp`
  - 测试场景：
    - 正常路径：脚本父类 + transient blueprint child 执行 body-only `SoftReloadOnly` 后不崩溃，child 仍能命中新脚本逻辑且 schema/token stream 正常刷新
    - 边界条件：`DiscardModule()` 或等价 shared-state 失效后，`OldClass->Class == nullptr` 的 class 不再进入 `PrepareSoftReload()` / `DoSoftReload()`，而是稳定 fallback 到 `full reload` 或 brand-new create
    - 错误路径：遍历到非 `UASClass` 的 `UBlueprintGeneratedClass` 时不会发生空指针解引用，且会留下可读 diagnostic / skip 结果
  - 测试命名：`Angelscript.TestModule.HotReload.BlueprintSync.<CaseName>`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P2.3-T** 📦 Git 提交：`[ClassGenerator/HotReload] Test: cover blueprint-child sync and dead-old-class fallback`

### Phase 3 补充：构造上下文与 ctor contract

- [ ] **P3.2** 把脚本构造上下文收口为 `thread_local + RAII`，并清理 ctor 异常后的脏 pending state
  - 当前 `CurrentObjectInitializers` 与 `GConstructASObjectWithoutDefaults` 仍是 ambient global state：一个负责标记“当前哪个 UObject 正在脚本构造中”，另一个负责给 `CDONoDefaults` 跳过 defaults。两者都不是 `thread_local`，而 `FinishConstructObject()` 又是唯一可靠的 pop 路径，因此一旦出现 async loading thread、嵌套构造或 ctor 异常，后续对象就可能继承错误构造栈与 defaults 抑制状态。
  - 这项补充要把“谁在构造中”“这次是否跳过 defaults”“栈顶对象是否就是当前对象”统一收进一个 scoped construction context，而不是继续让 `AllocScriptObject()`、`FinishConstructObject()`、三个 `Static*Constructor()` 和 `PrepareSoftReload()` 分别读写进程级静态变量。
  - 实现上优先引入 `FASPendingConstructionState` / `FScopedASConstructionContext` 一类 helper：构造栈改成 `thread_local` 容器，push/pop 只通过共享 helper 发生；异常、中断、prepare 失败和 early-return 都要走统一清理分支，`GetConstructingASObject()` 也改成查询同一条 owner 路径，而不是继续在 `FUObjectThreadContext` 与独立全局栈之间双轨取状态。
  - 来源：
    - [A] `ClassGenerator_Analysis.md` — “`CurrentObjectInitializers` 是进程级共享栈，脚本对象构造与 async loading thread 并发时会互相污染”
    - [A] `ClassGenerator_Analysis.md` — “脚本 ctor 异常时不会弹出 `CurrentObjectInitializers`，后续对象会继承脏构造上下文”
    - [B] `ClassGenerator_Plan.md` — “`CurrentObjectInitializers` 应迁移到 `thread_local` pending state，并给 ctor 异常补显式回滚”
    - [C] `ClassGenerator_TestGaps.md` — “`UASClass::GetConstructingASObject()` 目前零覆盖，构造期上下文 contract 没有 targeted regression”
    - [D] `HotReloadArch_ArchReview.md` — “`GConstructASObjectWithoutDefaults + CurrentObjectInitializers` 当前是 ambient global，而不是 `thread-local + RAII` 构造上下文”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` L987-L988、L1075-L1079、L1140-L1168，`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` L4093-L4100 — 当前 pending initializer 栈与 defaults 抑制仍是进程级静态变量，push 发生在 `AllocScriptObject()`，pop 只在 `FinishConstructObject()` 的有限分支执行
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、新增候选 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASConstructionContext.h/.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASClassConstructionContextTests.cpp`
- [ ] **P3.2** 📦 Git 提交：`[ClassGenerator] Fix: make construction context thread-local and exception-safe`
- [ ] **P3.2-T** 单元测试：补齐构造期上下文、异常清理与多线程隔离回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASClassConstructionContextTests.cpp`
  - 测试场景：
    - 正常路径：脚本 ctor/defaults 执行期间 `GetConstructingASObject()` 返回当前实例，构造完成后上下文被清空
    - 边界条件：嵌套构造或模拟 async loading thread/defaults 场景时，每个线程只看到自己的 pending state，不会互相串栈
    - 错误路径：ctor 抛异常、prepare 失败或 early-return 后，pending state 与 skip-defaults scope 都会被回收，后续对象不再继承脏上下文
  - 测试命名：`Angelscript.TestModule.ClassGenerator.ConstructionContext.<CaseName>`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P3.2-T** 📦 Git 提交：`[ClassGenerator] Test: cover construction-context cleanup threading and nesting`

- [ ] **P3.3** 把“只有带参 ctor、没有无参 ctor”的脚本类升级成显式编译失败合同
  - 当前 `UpdateConstructAndDefaultsFunctions()` 仍默认每个动态 `UClass` 都能从 `ObjType->beh.construct` 取到无参构造函数；一旦 builder 因用户自定义 ctor 移除了自动默认 ctor，这里就会先拿 `GetFunctionById(0)`，随后直接写 `isInUse`，而 `ReinitializeScriptObject()` 也只是在更晚的运行时路径打出 “will crash soon” 的 `ensure`。
  - 这项补充要把“当前动态 `UClass` 生成要求无参 ctor”变成显式 contract：分析阶段先报可读 compile error，`UpdateConstructAndDefaultsFunctions()` 改成返回成功/失败，`CreateClass` / `DoSoftReload` / `ReinitializeScriptObject()` 统一消费失败结果并阻止本类 swap-in，禁止继续以“半支持”状态进入空指针或 `ensure` 崩溃。
  - 若后续确实要支持 ctor-only script class，应另立能力项实现 hidden default ctor 或等价 instantiation bridge；在那之前，现有 runtime 只能选择“明确拒绝”，不能继续“隐式接受到崩溃”。
  - 来源：
    - [A] `ClassGenerator_Analysis.md` — “`UpdateConstructAndDefaultsFunctions()` 假定 `beh.construct` 必然存在，遇到 ctor-only class 会直接空指针解引用”
    - [B] `ClassGenerator_Plan.md` — “无默认 ctor 的 script class 应在分析/生成阶段显式失败，而不是等 `ReinitializeScriptObject()` 走到 `ensure`”
    - [D] `TypeSystem_ArchReview.md` — “当前 `UpdateConstructAndDefaultsFunctions()` 对 object shell / interface shell 一视同仁，object instantiation contract 还没有被显式建模”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` L4827-L4848、L5891-L5903 — 当前 `ReinitializeScriptObject()` 在 `beh.construct == 0` 时只打 `ensure`，`UpdateConstructAndDefaultsFunctions()` 仍无条件 `GetFunctionById(ObjType->beh.construct)` 并写 `isInUse`
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptScriptClassCtorContractTests.cpp`
- [ ] **P3.3** 📦 Git 提交：`[ClassGenerator] Fix: fail fast for script classes without default constructors`
- [ ] **P3.3-T** 单元测试：补齐无默认 ctor 的 compile-time / reload-time contract
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptScriptClassCtorContractTests.cpp`
  - 测试场景：
    - 正常路径：同时声明无参 ctor 与带参 ctor 的脚本类仍可稳定编译、spawn 和 hot reload
    - 边界条件：从“有默认 ctor”的版本热重载到“只有带参 ctor”的版本时，compile result 稳定变为可读错误或 `bModuleSwapInError`，旧 live class 不被半重建污染
    - 错误路径：初次编译 ctor-only script class 时稳定失败，不触发 `GetFunctionById(0)`、空指针写 `isInUse` 或 “will crash soon” `ensure`
  - 测试命名：`Angelscript.TestModule.ClassGenerator.CtorContract.<CaseName>`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P3.3-T** 📦 Git 提交：`[ClassGenerator] Test: enforce explicit no-default-ctor failure contract`

### 单元测试总览补充

| 改进项 | 测试文件 | 测试场景 | 优先级 |
| --- | --- | --- | --- |
| `P2.3` | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadBlueprintSyncTests.cpp` | `OldClass->Class == nullptr` fallback、blueprint child body-only reload、derived schema/token refresh | P0 |
| `P3.2` | `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASClassConstructionContextTests.cpp` | `GetConstructingASObject()`、thread-local pending state、ctor failure cleanup | P0 |
| `P3.3` | `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptScriptClassCtorContractTests.cpp` | 无默认 ctor compile error、reload failure contract、valid ctor overload path | P1 |

### 验收补充

1. `soft reload` 在 `DiscardModule` / shared-state 失效 / blueprint child 共存场景下不再因为空类壳或 `Cast<UASClass>(UBlueprintGeneratedClass)` 假设崩溃。
2. 脚本构造上下文不再依赖进程级共享栈；无论正常构造、嵌套构造、异常退出还是 async loading thread，都不会把 pending state 和 defaults 抑制泄漏给下一次对象创建。
3. 只有带参 ctor 的 script class 会在分析/生成阶段得到明确失败结果，而不是进入空指针、`ensure` 或半初始化 `UASClass` 状态。

---

## 本轮追加（2026-04-09 第三轮）

- 前两轮已经把 class-level `soft/full reload`、版本链、构造事务和 component layout 主线写进 Plan，但五维输入里仍有一块高风险缺口没有被单独拆出来：`UASFunction` 运行时壳本身没有成为 reload owner，导致 dispatch cache、`WorldContext` 布局、`WithValidate` 与 live-state 查询持续分裂。
- 本轮只补函数运行时壳相关的两条任务，不重复 `Documents/Plans/Plan_AngelscriptEngineBindAndFileWatchValidation.md` 的 watcher/矩阵范围，也不展开 `Plan_UFunctionReflectiveFallbackBinding.md` 的反射 fallback 主线。

### Phase 2 补充：函数运行时壳与 RPC 合同

- [ ] **P2.4** 把 `UASFunction` 的 dispatch shape 与 `WorldContext` 参数布局收口成可比较、可刷新的 runtime contract
  - 当前 full reload 会在 `AllocateFunctionFor()` 后把 `JitFunction*` 缓存、`FUNC_Native + UASFunctionNativeThunk`、`WorldContextIndex` 与 `bIsWorldContextGenerated` 一次性写进 `UASFunction`，但 soft reload 只替换 `ScriptFunction` 并重绑 type usage；结果是“函数壳的 dispatch 形态”和“脚本函数句柄”处于不同步状态。
  - 这一项要把 `bThreadSafe`、JIT 可用性、是否依赖 generated/explicit `WorldContext` 参数、是否需要 specialized subclass 统一折叠成 `dispatch shape`。只要 shape 变化，就必须升级成 `FullReloadRequired` 或显式拒绝 `SoftReloadOnly`；只有 shape 不变时，才允许复用旧 `UASFunction` 壳并刷新 `JitFunction`、`JitFunction_Raw`、`JitFunction_ParmsEntry`、`WorldContextIndex`、`WorldContextOffsetInParms`。
  - `WorldContext` 布局绑定也需要从“注释掉的局部代码”提升成正式 helper。生成静态函数时必须在 `StaticLink(true)` 后写入真实 `WorldContextOffsetInParms`；soft reload 复用旧壳时，也要同步重算 offset，而不是继续让 `RuntimeCallEvent()` / JIT thunk 从 `Parms - 1` 读环境对象。
  - 来源：
    - [A] `ClassGenerator_Analysis.md` — “soft reload 不会刷新 `UASFunction` 缓存的 JIT 入口，`BlueprintThreadSafe` 变更和 `WorldContextOffsetInParms` 也不会同步”
    - [B] `ClassGenerator_Plan.md` — “`DoSoftReload()` 只替换 `ScriptFunction`，需要把 dispatch shape 和 world-context layout 显式纳入 reload contract”
    - [C] `ClassGenerator_TestGaps.md` — “`AllocateFunctionFor()` 的线程安全/JIT 分派子类选择目前零覆盖”
    - [D] `HotReloadArch_ArchReview.md` — “soft path 只把旧 `UASFunction` 指向新的 `ScriptFunction`，当前没有 function-level patch owner”
    - [E] `GapAnalysis.md`、`CrossComparison.md` — “当前软重载核心动作只是替换 `UASFunction->ScriptFunction`，而真正的 dispatch owner 仍是 `UASFunction + NativeThunk`”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` L3416-L3429、L3554-L3560、L3633-L3643、L4253-L4259、L4779-L4791，`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h` L178-L190，`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` L503-L519、L577-L585 — 当前 full reload 会缓存 `JitFunction*` 与 `WorldContextIndex`，但 `WorldContextOffsetInParms` 绑定代码仍被注释；soft reload 只替换 `ScriptFunction` 并软重绑类型，运行时 thunk 继续直接解引用旧缓存和 `WorldContextOffsetInParms`
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionDispatchTests.cpp`
- [ ] **P2.4** 📦 Git 提交：`[ClassGenerator/HotReload] Fix: refresh function dispatch caches and world-context layout`
- [ ] **P2.4-T** 单元测试：补齐 `UASFunction` dispatch shape、JIT 缓存与 `WorldContext` 参数布局回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionDispatchTests.cpp`
  - 测试场景：
    - 正常路径：body-only soft reload 后，一个可 JIT 的 `final` 函数会执行新逻辑；静态函数通过 `RuntimeCallEvent` 进入时能拿到正确 `WorldContext`
    - 边界条件：`BlueprintThreadSafe` / `NotBlueprintThreadSafe` 切换、显式 `meta=(WorldContext=...)` 参数与自动生成 `_World_Context` 两种布局都能得到正确的 dispatch 决策与参数偏移
    - 错误路径：dispatch shape 变化不能静默复用旧 `UASFunction` 子类；`WorldContextOffsetInParms` 未绑定时调用会被明确拒绝或升级为 full reload，不再读取 `Parms - 1`
  - 测试命名：`Angelscript.TestModule.HotReload.FunctionDispatch.<CaseName>`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P2.4-T** 📦 Git 提交：`[ClassGenerator/HotReload] Test: cover function dispatch cache and world-context layout`

- [ ] **P2.5** 收口 `UASFunction` live-state、`WithValidate` 与 `IsFunctionImplementedInScript()` 的单一合同
  - 当前函数 live-state 至少被拆成四份：`ScriptFunction`、`ValidateFunction`、`FUNC_NetValidate`、以及 `UASClass::IsFunctionImplementedInScript()` 对 `UASFunction` 外壳的存在性判断。full reload 会缓存 `_Validate` 函数，soft reload 却只替换 `ScriptFunction`；而公开查询 API 又完全不看底层句柄是否仍然 live。
  - 这项补充要把“函数是否仍有有效脚本实现”和“RPC 是否仍带 `_Validate` 合同”统一成共享 helper。`FAngelscriptFunctionDesc::IsDefinitionEquivalent()` 需要把 `bNetValidate` 纳入比较；`DoSoftReload()` / full reload / cleanup 需要共用 `RefreshNetValidateBinding()` 与 `InvalidateRuntimeFunctionState()`；`UASClass::IsFunctionImplementedInScript()` 则只能通过 `HasLiveScriptImplementation()` 之类的 helper 返回结果，禁止继续把“壳对象还在”误报成“脚本实现仍 live”。
  - 同时要把 cleanup 路径做成对称收口：invalidate 时不仅清 `ScriptFunction`，也要同步清 `ValidateFunction` 与相关 runtime flag/cache；恢复时则要求 `_Validate` 函数、`FUNC_NetValidate` 和 live script handle 一起回到一致状态。
  - 来源：
    - [A] `ClassGenerator_Analysis.md` — “`WithValidate` 被拆成三份状态，soft reload 不会同步；`IsFunctionImplementedInScript()` 只检查 `UASFunction` 外壳”
    - [B] `ClassGenerator_Plan.md` — “需要 `RefreshNetValidateBinding()` / `InvalidateRuntimeFunctionState()`，并让 `IsFunctionImplementedInScript()` 改读 live helper”
    - [C] `ClassGenerator_TestGaps.md` — “`GetRuntimeValidateFunction()` 目前零覆盖，`IsFunctionImplementedInScript()` 也缺失 discard/reload 后的失效态测试”
    - [D] `HotReloadArch_ArchReview.md` — “soft path 只把旧 `UASFunction` 指向新的 `ScriptFunction`，reload 成功后旧模块会立刻进入 discard 路径”
    - [E] `GapAnalysis.md` — “当前软重载核心动作只是把 `UASFunction->ScriptFunction` 指到新函数，运行时 function owner 缺少统一 rebind / invalidate 面”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` L979-L984、L1956-L1958，`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h` L123-L127，`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` L1019-L1036、L3467-L3472、L3660-L3663、L4253-L4259、L5009-L5016 — 当前 `IsFunctionImplementedInScript()` 仍只检查 `UASFunction` 外壳，`GetRuntimeValidateFunction()` 只是直接返回缓存指针；`FUNC_NetValidate` 与 `ValidateFunction` 只在 full reload 创建期建立，soft reload 仍只替换 `ScriptFunction`，cleanup 里也只看到 `ScriptFunction = nullptr`
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptComponent.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASFunctionRuntimeStateTests.cpp`
- [ ] **P2.5** 📦 Git 提交：`[ClassGenerator/HotReload] Fix: unify runtime function liveness and net-validate state`
- [ ] **P2.5-T** 单元测试：补齐函数 live-state、`WithValidate` 与 discard/reload cleanup 合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASFunctionRuntimeStateTests.cpp`
  - 测试场景：
    - 正常路径：带 `WithValidate` 的 RPC 编译后同时具备 `FUNC_NetValidate` 与非空 `GetRuntimeValidateFunction()`；普通 live 函数的 `IsFunctionImplementedInScript()` 返回 `true`
    - 边界条件：仅修改 `_Validate` 函数体或在 live reload 中增删 `WithValidate` 时，系统会正确刷新/清空 validate cache，或明确升级到 full reload，不留下 flag/cache 半同步状态
    - 错误路径：`DiscardModule()` 或 full reload cleanup 后，`IsFunctionImplementedInScript()` 会稳定变为 `false`，stale `UASFunction` 不再继续暴露 live script 实现或旧 validate 绑定
  - 测试命名：`Angelscript.TestModule.ClassGenerator.FunctionRuntimeState.<CaseName>`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P2.5-T** 📦 Git 提交：`[ClassGenerator] Test: cover runtime function liveness and net-validate state`

### 单元测试总览补充

| 改进项 | 测试文件 | 测试场景 | 优先级 |
| --- | --- | --- | --- |
| `P2.4` | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionDispatchTests.cpp` | JIT body-only reload、`BlueprintThreadSafe` 分派切换、generated/explicit `WorldContext` thunk | P0 |
| `P2.5` | `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASFunctionRuntimeStateTests.cpp` | `WithValidate` cache/flag、一致性、`IsFunctionImplementedInScript()` discard/reload 失效态 | P1 |

### 验收补充

4. body-only soft reload 不再让 live `UASFunction` 继续执行旧 JIT 入口或旧线程约束子类；静态函数的 `WorldContext` 参数布局在 full/soft reload 后都与 thunk 读取路径保持一致。
5. `WithValidate`、`ValidateFunction`、`FUNC_NetValidate` 与 `IsFunctionImplementedInScript()` 不再在 soft reload / discard / cleanup 后分叉；工具层和运行时看到的是同一份函数 live-state 真相。

### 风险补充

1. **更多函数级 metadata 变化会被提升为 full reload**
   - 缓解：第一阶段只把确实会改变 `UASFunction` 壳形态或 RPC 合同的变化升格，其余仍保守复用旧路径，并配套日志说明“为什么这次不能 soft reload”。
2. **`WorldContextOffsetInParms` 防御性检查会把过去“偶尔侥幸不崩”的坏状态前移成显式失败**
   - 缓解：把这视为正确的 fail-fast，优先配套回归测试和明确诊断，而不是继续允许运行时从无效地址读 `UObject*`。

---

## 本轮追加（2026-04-09 第四轮）

- 现有 `P1.1/P1.2/P2.5` 已经覆盖 version chain、identity owner 与函数 runtime invalidate，但五维输入里还有两条同属 `ClassGenerator` 生命周期收口的 correctness 缺口还没有被单独落成执行项：removed class 在 GC 前仍可被 lookup/materialize，full reload 旧实例在真正 GC 前会进入空类型窗口。
- `Documents/Plans/` 当前活跃 Plan 未覆盖这两条 runtime contract；本轮只补 `ClassGenerator/` 内的 lifecycle owner，不重复 `Plan_AngelscriptEngineBindAndFileWatchValidation.md` 的 watcher/矩阵范围，也不展开 `Plan_UnrealCSharpArchitectureAbsorption.md` 的长期 dependency graph。

### Phase 1 补充：removed class 隔离与旧实例延迟退役

- [ ] **P1.3** 把 removed class 从“断脚本指针”升级为“彻底 quarantine 的 tombstone runtime type”
  - 当前 `CleanupRemovedClass()` 只把 `ScriptTypePtr/ConstructFunction/DefaultsFunction` 断开并隐藏 class，但 lookup、`GetDefaultObject()`、`NewObject/spawn` 入口仍把它当成正常 `UClass` 使用；这让“类已删但仍能生成 native shell / 默认组件 / CDO”的半失效状态在 GC 窗口内继续存在。
  - 这一项不重复 `P1.1` 的 version-chain canonical name 收口，而是把 removed class 的 runtime entry points 一次性 quarantine：`CleanupRemovedClass()` 负责 tombstone rename、`CLASS_Abstract`/等价 runtime 禁入标记、fail-fast constructor stub、组件/缓存清空；`Bind_UClass` 与 `TSubclassOf` 相关 lookup/read path 只接受 live class，不再继续把 hidden removed class 暴露给脚本侧。
  - 落地时优先抽出 `QuarantineRemovedClass(UASClass&)` 与 `IsLiveGeneratedClass(UClass*)`/`ResolveLiveGeneratedClass()` 一类 helper，把构造隔离、名字查找、默认对象读取与 stale class 诊断统一到一处；避免 `CleanupRemovedClass()`、`Bind_UClass`、`Bind_TSubclassOf` 再各自维护一套 removed 判定。
  - 来源：
    - [A] `ClassGenerator_Analysis.md` — “已删除 `UASClass` 在 GC 前仍可按原名被发现并实例化，产出无脚本 backing 的僵尸对象”
    - [B] `ClassGenerator_Plan.md` — “Issue-40：`CleanupRemovedClass()` 只断脚本指针，不隔离构造入口与 lookup”
    - [C] `ClassGenerator_TestGaps.md` — “`HotReload.DiscardModule` 只验证 module record 移除，没有锁住旧类/旧函数 lookup 与创建路径真的失效”
    - [D] `HotReloadArch_ArchReview.md` — “旧 `UClass*` / `TSubclassOf` 句柄与 `NewObject()` 仍是调用点各自处理，版本链与实例化路径缺少统一 canonicalization”
    - [E] `GapAnalysis.md` — “`CleanupRemovedClass()` 仍停留在清壳状态，没有显式 tombstone / unregister owner”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` L4990-L5024，`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` L1367-L1401、L1423-L1430、L1475-L1479，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp` L331-L335、L520-L532，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSubclassOf.h` L80-L97、L105-L135 — 当前 cleanup 仍只清脚本指针并隐藏 class；`FindClass()` 仍按名字直接返回旧类，`TSubclassOf`/`GetDefaultObject()` 仍直接透传旧句柄，三个静态构造路径也仍会执行 `CodeSuperClass->ClassConstructor` 与默认组件流程
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSubclassOf.h`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadRemovedClassQuarantineTests.cpp`
- [ ] **P1.3** 📦 Git 提交：`[ClassGenerator/HotReload] Fix: quarantine removed classes and stale lookups`
- [ ] **P1.3-T** 单元测试：补齐 removed class 在 GC 前的 lookup/materialize quarantine 合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadRemovedClassQuarantineTests.cpp`
  - 测试场景：
    - 正常路径：删除脚本类并完成 discard/remove 后，`FindGeneratedClass`、`UClass FindClass` 与 module lookup 都不再返回 live class；survivor module 的 lookup 不受影响
    - 边界条件：GC 前保留 stale `UClass*`/`TSubclassOf` 句柄时，`GetDefaultObject()`、`NewObject()` 或 actor spawn 只会命中 fail-fast/tombstone 诊断，不会再创建默认组件或新的半失效对象
    - 错误路径：重复执行 `remove -> lookup -> GC` 不会把 tombstone class 重新暴露回 canonical 名，也不会让 hidden removed class 继续通过脚本侧查找 API 复活
  - 测试命名：`Angelscript.TestModule.HotReload.RemovedClassQuarantine.<CaseName>`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P1.3-T** 📦 Git 提交：`[ClassGenerator/HotReload] Test: cover removed-class quarantine and stale lookup rejection`

- [ ] **P1.4** 把 full reload 的旧 `ScriptType` 清理改成 post-GC 延迟退役，消除旧实例“空类型窗口”
  - 当前 full reload 会在 `ForceGarbageCollection(true)` 之前先把 `ReplacedClass->ScriptTypePtr` 和所有仍指向旧 type 的类级指针清成 `nullptr`，但对象类型解析、虚函数分派与 `RuntimeDestroyObject()` 都仍依赖这份类级 `ScriptTypePtr`。结果是旧实例在真正被回收前，已经进入“类型为空但对象还活着”的半失效窗口。
  - 这项补充要把“停止旧类参与新一轮生成/lookup”和“旧实例在 GC 前仍需读到自己的 old type”拆成两个阶段：reload 当轮只做 tombstone/pending-delete 标记并阻止新 lookup/materialize，真正的 `ScriptTypePtr/ConstructFunction/DefaultsFunction` 释放延后到 post-GC 或等价的无实例确认阶段。
  - 落地时优先引入 `FDeferredOldScriptType` / `FPendingRetiredClassState` 一类 registry，统一托管 old type、生效 epoch 与 post-GC release；`asIScriptObject::GetObjectType()`、`ResolveScriptVirtual()`、`VerifyScriptVirtualResolved()`、`RuntimeDestroyObject()` 只通过这套 owner 读取 live-or-deferred type，避免继续直接赌 `UASClass::ScriptTypePtr` 在 GC 前永远有效。
  - 来源：
    - [A] `ClassGenerator_Analysis.md` — “full reload 在 GC 前先清空 `ScriptTypePtr`，旧实例会跳过 `RuntimeDestroyObject()` 并落入空类型窗口”
    - [B] `ClassGenerator_Plan.md` — “Issue-42：旧 live object 在 reload 与 GC 之间需要 deferred old type，而不是立刻把类级 type 切空”
    - [C] `ClassGenerator_TestGaps.md` — “`RuntimeDestroyObject()` 只有正向 GC 建议测试，缺少 full reload 后保留旧实例跨过 GC 窗口的 targeted regression”
    - [D] `HotReloadArch_ArchReview.md` — “`OnPostReload` 与对外可见的 reload 完成信号早于真正 GC，当前生命周期边界会让外部在旧对象仍存活时观察到已换代状态”
    - [E] `GapAnalysis.md` — “D4 仍缺 `execution epoch / prepare-finish observer`，旧对象与新模块会在换代窗口内跨 epoch 混跑”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` L2408-L2444，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.cpp` L4-L11，`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` L83-L120、L965-L976 — 当前 full reload 仍在 `ForceGarbageCollection(true)` 前清空 class-level `ScriptTypePtr`；而对象类型解析、虚调用分派与 `RuntimeDestroyObject()` 都继续直接读取这份字段，旧实例在 GC 前只能看到 `nullptr`
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、新增候选 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASDeferredScriptTypeState.h/.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadOldObjectLifetimeTests.cpp`
- [ ] **P1.4** 📦 Git 提交：`[ClassGenerator/HotReload] Fix: defer old script-type retirement until post-GC`
- [ ] **P1.4-T** 单元测试：补齐 full reload 后旧实例跨 GC 窗口的 type/destructor 合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadOldObjectLifetimeTests.cpp`
  - 测试场景：
    - 正常路径：full reload 后保留一个旧实例到下一帧，在真正 GC 前调用 `GetObjectType()` 与一个虚函数/`BlueprintOverride` 路径仍能解析到 deferred old type；GC 后旧 type 再被真正释放
    - 边界条件：旧 script class 的 transient Blueprint child 或旧对象的 destructor probe 在 `OnPostReload` 与 GC 之间都不会因类级 `ScriptTypePtr == nullptr` 提前失效；GC 完成后析构计数稳定增加一次
    - 错误路径：pending-delete/tombstone class 在旧 type 延迟退役期间不能重新参与 `FindClass`、spawn 或新的对象 materialize；post-GC 后 registry 不残留 deferred old type 泄漏
  - 测试命名：`Angelscript.TestModule.HotReload.OldObjectLifetime.<CaseName>`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P1.4-T** 📦 Git 提交：`[ClassGenerator/HotReload] Test: cover old-object lifetime across full-reload GC window`

### 单元测试总览补充

| 改进项 | 测试文件 | 测试场景 | 优先级 |
| --- | --- | --- | --- |
| `P1.3` | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadRemovedClassQuarantineTests.cpp` | removed lookup/materialize quarantine、stale `UClass/TSubclassOf`、tombstone fail-fast | P1 |
| `P1.4` | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadOldObjectLifetimeTests.cpp` | full reload 前后旧实例 `GetObjectType()`/虚调度、GC 后析构与 deferred old type release | P0 |

### 验收补充

6. removed class 在 GC 前不再通过 `FindClass`、`GetDefaultObject()`、`NewObject()`、stale `TSubclassOf` 等入口继续 materialize 半失效对象；外部看到的只会是 tombstone-aware 拒绝或显式诊断。
7. full reload 与 GC 之间不再存在“旧对象还活着但类级 `ScriptTypePtr` 已空”的窗口；旧实例在真正回收前仍能完成类型解析、虚调用与脚本析构，回收后 deferred old type 会被及时释放。

### 风险补充

3. **removed class quarantine 会收紧一批历史 lookup 行为**
   - 缓解：首阶段统一输出 tombstone/removed 诊断，并把调试 helper 显式迁到 latest/tombstone-aware 查询，避免测试或工具继续依赖“按旧名字还能拿到旧类对象”。
4. **deferred old type release 会引入一段短暂的旧类型保留期**
   - 缓解：把保留期绑定到 post-GC/session epoch，并为 registry 增加计数与 dump；只有“阻止新 materialize、允许旧对象善终”两项职责，不把它扩展成第二套长期 identity store。

---

## 本轮追加（2026-04-09 第五轮）

- 现有 `Plan_ClassGenerator.md` 已覆盖版本链、`soft reload` 语义、function runtime state、removed class quarantine 与 ctor/defaults 事务化，但五维输入里还有三条未独立落项的 contract 仍会继续破坏 `ClassGenerator` 的 correctness：依赖传播后的最终 `ReloadReq` 会丢在 module 层之前，struct/delegate 的 value-type 依赖仍靠 `IsObject()` 近似，多 section 脚本的源码 metadata 仍固定回 `Code[0]`。
- 本轮只补 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/` 内当前源码能独立闭环的 stopgap correctness/tooling owner，不展开 `Plan_UnrealCSharpArchitectureAbsorption.md` 的长线 dependency graph，也不重复 `Plan_CppInterfaceBinding.md` 与 `Plan_NetworkReplicationTests.md` 的主题。

### Phase 2 补充：reload verdict 与 type dependency preflight

- [ ] **P2.6** 把依赖传播后的最终 reload verdict 收口成单一事实源
  - 当前 `Setup()` 会先跑 `PropagateReloadRequirements()`，但之后直接读取旧的 `ModuleData.ReloadReq` 返回总体 verdict；`ShouldFullReload()` 与 `AngelscriptEngine.cpp` 的 switch 又只消费这份 module-level 结果。于是 provider 已经把 consumer 升到 `FullReloadRequired`，执行层仍可能整体走 `SoftReload` 并把 consumer 送进 `PrepareSoftReload()` / `DoSoftReload()`。
  - 这项只修当前 stopgap correctness，不扩成新的 impact manifest：在 `Setup()` 后重算 `ModuleData.ReloadReq` / `ReloadReqLines`，再让 `ShouldFullReload()`、`WantsFullReload()`、`NeedsFullReload()` 和 `SoftReloadOnly` 拒绝路径统一读取“传播后的最终 verdict”，保证 `Analyze -> Setup -> swap-in` 只有一份 reload 事实源。
  - 同时补一条轻量诊断 seam，记录“哪个 provider 把哪个 consumer 升级到了更高的 requirement”；目标不是引入第二套状态机，而是把现有 `ReloadRequirement` 梯度真正兑现到执行层和测试层。
  - 来源：
    - [B] `ClassGenerator_Plan.md` — “Issue-43：依赖传播只提升 `ClassData/DelegateData.ReloadReq`，不会回写 `ModuleData.ReloadReq`；执行阶段会按过低等级 reload”
    - [C] `ClassGenerator_TestGaps.md` — “`NewTest-10` 仍缺 `Setup()` / `GetFullReloadLines()` targeted regression；`Issue-35` 说明现有分析测试没有锁死 `bWantsFullReload/bNeedsFullReload` 语义”
    - [D] `HotReloadArch_ArchReview.md` — “Arch-HR-3：`ReloadReq` 传播缺少 edge/reason owner，最终 verdict 与 fanout 仍可能分叉”
    - [E] `GapAnalysis.md`、`CrossComparison.md` D4 — “当前优势就是显式 `ReloadRequirement` 梯度；不应让最终 module verdict 在传播后静默失真”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` L1886-L1905、L1923-L1960、L2081-L2113，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L3914-L3996 — `Setup()` 在传播后未重算 `ModuleData.ReloadReq`，`ShouldFullReload()` 在 soft path 不看 `Class/Delegate.ReloadReq`，engine switch 只消费 `Setup()` 返回的 module-level verdict
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadRequirementPropagationTests.cpp`
- [ ] **P2.6** 📦 Git 提交：`[ClassGenerator/HotReload] Fix: recompute effective reload verdict after dependency propagation`
- [ ] **P2.6-T** 单元测试：补齐传播后 verdict、`GetFullReloadLines()` 与 `SoftReloadOnly` 拒绝路径回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadRequirementPropagationTests.cpp`
  - 测试场景：
    - 正常路径：provider 仅发生函数体修改时，consumer 仍保持 `SoftReload`，`Setup()` 返回 `SoftReload`，`NeedsFullReload()` 为 `false`
    - 边界条件：provider 的 property/return/parameter 结构变化传播到 consumer 后，`Setup()` 返回至少 `FullReloadRequired`，`GetFullReloadLines()` 同时覆盖 provider 与 consumer 的触发行
    - 错误路径：`SoftReloadOnly` 下命中传播后的 `FullReloadRequired` 时，不会进入 `PerformSoftReload()` / `DoSoftReload()`，旧模块继续保持 live 且新模块不被错误换入
  - 测试命名：`Angelscript.TestModule.HotReload.RequirementPropagation.<CaseName>`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P2.6-T** 📦 Git 提交：`[ClassGenerator/HotReload] Test: cover propagated reload verdict and line mapping`

- [ ] **P2.7** 把 struct/delegate 的 value-type 依赖接进统一 preflight，而不是继续靠 `IsObject()` 近似
  - 当前 `PropagateReloadRequirements(FClassData&)` 在已 materialize 的脚本类型上只扫描 `IsObject()` 的 property/return/param，`AddClassProperties()` 也只对 object non-ref property 才 `EnsureReloaded(TypeId)`；`AddFunctionReturnType()` / `AddFunctionArgument()` 则直接 `CreateProperty()`，没有在 struct/delegate family 前等待 provider 完成 reload。结果是 `StructB -> StructA -> ClassC` 这类 value-type 链，以及 delegate signature 依赖，仍可能继续吃到旧 `UScriptStruct/UDelegateFunction` 外壳。
  - 这一项只补最小 correctness stopgap，不展开 `Plan_UnrealCSharpArchitectureAbsorption.md` 的长期 dependency graph：把 `FAngelscriptTypeUsage` 层的 struct/delegate 依赖接进 `AddReloadDependency`、`EnsureReloaded` 和 function/property materialization 前的统一 helper，例如 `EnsureTypeDependenciesReloaded(...)`，让 class property、function return/arg 与 delegate signature 共用同一条 provider-before-consumer 屏障。
  - 对任何命中 provider `FullReloadRequired` 的 consumer，soft path 都必须升级成 full reload 或显式拒绝 swap-in；禁止继续复用旧 `FStructProperty`、旧 `UDelegateFunction` 或旧 layout size 把 ABI 错配带进外层 `UClass/CDO`。
  - 来源：
    - [B] `ClassGenerator_Plan.md` — “Issue-28：script struct value-type 依赖没有进入 reload 图，struct/class 会按源码顺序重建并继续挂旧 `UScriptStruct`”
    - [C] `ClassGenerator_TestGaps.md` — “`NewTest-1` 指出 `UASStruct::GetNewestVersion` 的 full reload 零覆盖；`NewTest-14` 指出 `OnDelegateReload` signature 替换广播零覆盖”
    - [D] `TypeSystem_ArchReview.md` — “Arch-TS-50：`EnsureReloaded()` 仍是 site-specific 预热，而且当前实现只真正处理 class，不处理 delegate”
    - [E] `GapAnalysis.md` D4 — “`Graph before fanout` 值得吸收；在放大 rebind/observer 前，先把 dependency owner 收口到统一 graph/preflight”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` L1975-L2021、L2044-L2057、L2206-L2214、L2525-L2548、L2907-L2918、L3935-L3977 — class path 只按 `IsObject()` 传播/预热依赖，struct reload 仍按模块内线性顺序执行，`EnsureReloaded(int)` 命中 `DelegateDataPtr` 后没有实际 delegate reload 分支，而 return/arg property materialization 也在没有 preflight 下直接 `CreateProperty()`
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.h`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadTypeDependencyTests.cpp`
- [ ] **P2.7** 📦 Git 提交：`[ClassGenerator/HotReload] Fix: preload struct and delegate type dependencies before materialization`
- [ ] **P2.7-T** 单元测试：补齐 nested struct/delegate provider-before-consumer reload 闭环
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadTypeDependencyTests.cpp`
  - 测试场景：
    - 正常路径：`StructB` 布局变化后，`StructA` 与持有 `StructA` 的 `ClassC` 都会被纳入 reload 闭包，最终 `FStructProperty->Struct` 指向最新 `UASStruct`
    - 边界条件：delegate return/parameter 或 container subtype 依赖也会先完成 provider reload，再生成 consumer `FProperty/UDelegateFunction`，并触发正确的 `OnDelegateReload`
    - 错误路径：`SoftReloadOnly` 下 provider struct/delegate 已是 `FullReloadRequired` 时，consumer 不会继续复用旧 property/function shell，而是明确拒绝 swap-in 或升级到 full reload
  - 测试命名：`Angelscript.TestModule.HotReload.TypeDependency.<CaseName>`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P2.7-T** 📦 Git 提交：`[ClassGenerator/HotReload] Test: cover nested struct and delegate dependency reload closure`

### Phase 4：源码 metadata 与导航 contract

- [ ] **P4.1** 把 `UASClass/UASFunction` 的 source identity 改成 section-aware，而不是固定回 `Code[0]`
  - 当前 `UASClass::GetSourceFilePath()`、`GetRelativeSourceFilePath()` 和 `UASFunction::GetSourceFilePath()` 都直接返回 `Module->Code[0]`；但 `UASFunction::GetSourceLineNumber()` 已经从 `asCScriptFunction::scriptData->declaredAt` 读取真实声明行号。只要 module 拆成多 section，path 与 line 就会天然分叉，editor 导航、debugger 和热重载诊断会把同一个 symbol 指到错误文件。
  - 这项要把 source identity 收口成 section-aware helper：function path 优先使用真实 section index / section name 解析到正确文件；class path 则在 class generation/materialization 时显式记录声明 section 或文件名，避免继续从 `ScriptTypePtr->GetModule()` 反推 `Code[0]`。目标是保住当前“editor navigation 与 debugger 共用同一份 metadata-owned source identity”的优势，而不是引入第二套路径映射。
  - 对失效态仍要 fail-closed：如果函数已被 discard、section 映射缺失或 class metadata 不可恢复，API 应返回空路径 / `-1` 或稳定 fallback，而不是把真实行号与错误首文件静默拼在一起。
  - 来源：
    - [A] `ClassGenerator_Analysis.md` — “发现 73：`UASFunction::GetSourceFilePath()` 无视真实 section，`UASClass` 路径也固定回 `Code[0]`”
    - [C] `ClassGenerator_TestGaps.md` — “`NewTest-17` 指出 `UASClass` 源码路径 metadata API 零覆盖；`ClassGenerator/HotReload` 对 `GetSourceFilePath()` / `GetSourceLineNumber()` 为 0 命中”
    - [E] `CrossComparison.md` — “当前优势是 editor navigation 与 debugger 共享同一份 metadata-owned source identity，应保持 metadata primary owner”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` L1499-L1520、L1535-L1558 — class/function 的 path API 都固定返回 `Module->Code[0]`，而 line number 已来自 per-function script data；multi-section module 目前会得到路径与行号不一致的定位结果
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptSourceMetadataTests.cpp`
- [ ] **P4.1** 📦 Git 提交：`[ClassGenerator] Fix: resolve source metadata by actual script section`
- [ ] **P4.1-T** 单元测试：补齐 multi-section source path / line metadata 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptSourceMetadataTests.cpp`
  - 测试场景：
    - 正常路径：single-section module 下，class/function 的 absolute path、relative path 与 line number 都指向同一脚本文件
    - 边界条件：multi-section module 中，声明在第二个 section 的 class/function 会返回第二个文件的 path，并与 line number 保持一致
    - 错误路径：module discard、function invalidation 或 section 映射缺失后，公开 API 返回空路径 / `-1` 或稳定 fallback，不再把真实行号与错误的 `Code[0]` 首文件混在一起
  - 测试命名：`Angelscript.TestModule.ClassGenerator.SourceMetadata.<CaseName>`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P4.1-T** 📦 Git 提交：`[ClassGenerator] Test: cover multi-section source metadata and stale lookups`

### 单元测试总览补充

| 改进项 | 测试文件 | 测试场景 | 优先级 |
| --- | --- | --- | --- |
| `P2.6` | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadRequirementPropagationTests.cpp` | 传播后 verdict 收口、`GetFullReloadLines()`、`SoftReloadOnly` 拒绝路径 | P0 |
| `P2.7` | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadTypeDependencyTests.cpp` | nested struct reload 闭环、delegate dependency preflight、provider-before-consumer ordering | P0 |
| `P4.1` | `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptSourceMetadataTests.cpp` | multi-section path/line 一致性、stale metadata fail-closed | P1 |

### 验收补充

8. 依赖传播完成后，`Setup()`、`WantsFullReload()`、`NeedsFullReload()`、`GetFullReloadLines()` 与实际执行路径都基于同一份最终 verdict；`SoftReloadOnly` 不再对传播后的 `FullReloadRequired` consumer 执行类级 soft reload。
9. nested script struct/delegate 的 provider 变化会稳定把 consumer 拉进正确的 reload 闭包，`FStructProperty/UDelegateFunction` 不再继续引用旧 head 或旧 ABI 壳。
10. multi-section module 的 class/function 源码路径与行号保持一致，editor navigation、debugger 与热重载诊断不再固定跳到 `Code[0]`。

### 风险补充

5. **传播后的 effective verdict 会让一批历史“侥幸 soft reload 成功”的场景提前报错**
   - 缓解：首阶段优先输出稳定 diagnostics 与 line mapping，对照保留 body-only 场景的 regression，确保只收紧真正需要收紧的路径。
6. **struct/delegate dependency preflight 可能提前暴露隐藏的 reload cycle**
   - 缓解：先把 cycle/未完成依赖转成明确的 compile error 或 full reload fallback，禁止继续 partial rebuild；不要在第一阶段追求过度优化。
7. **section-aware source metadata 会改变部分 editor/test 对旧首文件路径的假设**
   - 缓解：以“真实声明文件优先、缺失时稳定 fallback”为原则，并同步把自动化断言从 `Code[0]` 假设切到精确文件名。

---

## 本轮追加（2026-04-09 第六轮）

- 前五轮已经把版本链、`soft/full reload` gate、component layout、transaction、函数 runtime contract 与 source metadata 收到了可执行条目，但五维输入里仍有四个 `ClassGenerator` 内部 owner 缺口尚未独立成项：`bModuleSwapInError` rollback 边界、纯 body-only 的 method-patch 快速路径、成员物化策略的单一真相，以及 hot reload 扩展/finished phase 的正式协议。
- 本轮不重复 `Plan_AngelscriptEngineBindAndFileWatchValidation.md` 的 watcher / 外层验证矩阵，也不替代 `Plan_NetworkReplicationTests.md` 的网络能力测试面；这里只补 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/` 当前仍会直接污染 live `UClass` 生成与热重载 authority 的 stopgap / owner 收口项。

### Phase 3 补充：swap-in rollback 边界

- [ ] **P3.4** 把 `bModuleSwapInError` 从“失败记账”提升成真正的 swap-in abort/rollback barrier
  - 当前 `FinalizeClass()` / `FinalizeActorClass()` / `VerifyClass()` 即便已经把 module 标成 `bModuleSwapInError`，仍会继续 `NotifyRegistrationEvent(...)`、`CallPostInitFunctions()`、`InitDefaultObjects()`、`OnClassReload` / `OnFullReload` / `OnPostReload`。这让已知非法的新类、坏 CDO 甚至坏 subsystem 先进入 live graph，再依赖下一轮 `PreviouslyFailedReloadFiles` 慢慢纠偏。
  - 这条任务补的是 `ClassGenerator` 内部的提交边界，不重复现有 `P3.1` 的 ctor/defaults/post-init 执行结果处理；目标是把“finalize / verify / subsystem activate 失败”也并入同一条事务，确保失败轮次里 authoritative state 仍是旧类、旧 CDO、旧 subsystem。
  - 落地时优先引入 `FReloadTransactionState`、`RollbackFailedNewClasses(...)` 与 `CommitSubsystemReplacements()` 一类最小 helper：一旦命中 `bModuleSwapInError`，立即停止当前类后续 finalize；在 `CallPostInitFunctions()` 与 `InitDefaultObjects()` 前增加 global barrier；若本轮已有 late error，则统一撤销新类 root、registration、`StaticClass` global、version-chain 写点与 pending subsystem replacement，并禁止发出“成功式” reload 广播。
  - 来源：
    - [A] `ClassGenerator_Analysis.md` — “发现 42/58：`bModuleSwapInError` 不会阻止当前轮次类注册、CDO 初始化与 live 污染”
    - [B] `ClassGenerator_Plan.md` — “Issue-16：需要把 `bModuleSwapInError` 升级为真正的 swap-in abort / rollback 信号”
    - [D] `HotReloadArch_ArchReview.md` — “Arch-HR-28：late validation 不是原子失败，`SwapInModules()` 先提交、错误后置报出”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` L2291-L2315、L2348-L2395、L5039-L5210 — 当前 full reload 仍在 `FinalizeClass()` 后无条件推进 `CallPostInitFunctions()` / `InitDefaultObjects()` / `VerifyClass()` / `OnClassReload` / `OnFullReload` / `OnPostReload`，而 `FinalizeClass()` 在多处设置 `bModuleSwapInError` 后仍继续 `NotifyRegistrationEvent(...)`
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadRollbackTests.cpp`
- [ ] **P3.4** 📦 Git 提交：`[ClassGenerator/HotReload] Fix: rollback failed swap-in before live registration`
- [ ] **P3.4-T** 单元测试：补齐 finalize/verify/subsystem 失败轮次的 rollback 合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadRollbackTests.cpp`
  - 测试场景：
    - 正常路径：合法 full reload 仍会注册新类、切换新 CDO，并按原顺序完成 class/struct/full reload 广播
    - 边界条件：`DefaultComponent` attach/root 非法、缺失 interface 方法、subsystem reload 期间 verify 失败三类脚本都会在 commit 前终止；旧类/旧 CDO/旧 subsystem 继续可查询、可实例化、可激活
    - 错误路径：失败轮次不会留下新的 canonical `UClass`、不会创建坏 CDO、不会把 `OnPostReload` 当成成功收尾广播；修复脚本后二次编译可正常前进
  - 测试命名：`Angelscript.TestModule.HotReload.Rollback.<CaseName>`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P3.4-T** 📦 Git 提交：`[ClassGenerator/HotReload] Test: cover rollback barrier for late swap-in errors`

### Phase 5：reload authority 与成员物化 owner

- [ ] **P5.1** 把纯 body-only 场景从整类 soft reload 中剥离成保守的 method-patch path，并给对象恢复留显式 hook
  - 当前 `SoftReload` 虽然通常不要求 full reload，但仍会整类 relink、遍历全部 direct instance、销毁并重建每个 script object，然后无条件 replay constructor。这让纯函数体改动也要承担整类重建成本，并持续放大 timer / delegate / latent / cache 等非属性副作用重复执行的风险。
  - 这条任务不是回滚 `P2.1` 对 correctness 的收紧，而是在它收口后给真正安全的 body-only delta 增加更细粒度 fast path：先以极保守的 `FunctionBodyOnly` 分类 + `DoMethodPatchReload()` 只更新受影响 `UASFunction::ScriptFunction`，再把 `ReinitializeScriptObject()` 拆成 `RebuildScriptStorage()` / `ReplayConstructFunction()` 两段，并补 `__PreSoftReload()` / `__PostSoftReload(bool)` 或等价 runtime hook，让对象自己修补非属性状态。
  - 首版必须保持 fail-safe：只要碰到 defaults、metadata、property/layout、super/interface、instanced property 或 constructor contract 变化，就直接回退到现有 `DoSoftReload()` / `FullReload`，禁止把“疑似 body-only”误降成 unsafe fast path。
  - 来源：
    - [C] `ClassGenerator_TestGaps.md` — “NewTest-4：body-only soft reload 仍缺 CDO/旧实例/新实例一致性与 live-class 合同”
    - [D] `HotReloadArch_ArchReview.md` — “Arch-HR-1：body-only 仍按整类 reload；Arch-HR-4：`ReinitializeScriptObject()` 无条件 replay constructor，缺少对象级 hook”
    - [E] `CrossComparison.md`、`GapAnalysis.md` — “slua/puerts 展示了更小改动面的 `UFunction` redirect 与 `prepare/finish`/rebind observer，当前 Angelscript 可局部吸收而不放弃签名层”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` L4113-L4188、L4573-L4630、L4825-L4844 — 当前 `DoSoftReload()` 仍整类重链 property、扫描 `GetObjectsOfClass()`、`DestructScriptObject()` + `ReinitializeScriptObject()`，而 `ReinitializeScriptObject()` placement-new 后会直接 `Context->Execute()` 重跑 constructor
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`、可选新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptHotReloadHooks.h/.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadMethodPatchTests.cpp`
- [ ] **P5.1** 📦 Git 提交：`[ClassGenerator/HotReload] Feat: add conservative method-patch path and object reload hooks`
- [ ] **P5.1-T** 单元测试：补齐 body-only method patch、constructor replay 与对象级恢复 contract
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadMethodPatchTests.cpp`
  - 测试场景：
    - 正常路径：纯函数体修改保持同一 `UClass`，只更新目标 `UASFunction` 行为；旧实例、新实例和 `CDO` 均不触发额外 constructor 副作用
    - 边界条件：声明了 `__PreSoftReload()` / `__PostSoftReload(bool)` 的对象能在 body-only reload 后恢复 timer/delegate/cache 等非属性状态；若策略判定需要 replay constructor，hook 能拿到准确 `bCtorReplayed`
    - 错误路径：只要命中 defaults、metadata、property/layout、super/interface 或 constructor signature 变化，就不会误走 method-patch path，而是稳定回退到现有 full/soft reload 或明确拒绝 swap-in
  - 测试命名：`Angelscript.TestModule.HotReload.MethodPatch.<CaseName>`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P5.1-T** 📦 Git 提交：`[ClassGenerator/HotReload] Test: cover body-only method patch and object restore hooks`

- [ ] **P5.2** 引入 `PropertyMaterializationPlan`，把 GC-only hidden lane 与 replication/editor/serialization 语义分开
  - 当前 `ClassGenerator` 对脚本成员的“是否落成真实 `FProperty`”只有 GC 驱动的一条 synthetic lane：hidden property 负责 GC，真正的 `CPF_Net` / `CPF_RepNotify` / editor metadata flag 又在 exported-property lane 里另写一遍。结果是 property metadata/specifier 的 class contract 很容易在 soft reload 与 future family 扩展中继续分叉。
  - 这条任务要把 `StorageKind`、`Reasons`、`PropertyFlags` 和 `bCanReplicate / bCanSerialize / bCanEdit` 显式收进 `PropertyMaterializationPlan`：analysis 阶段先回答“这个成员为什么需要真实 `FProperty`”，hidden property 只成为 plan 的一个结果；`AddClassProperties()`、soft reload property refresh、`GetLifetimeScriptReplicationList()` 与 future interface/wrapper family 都消费同一份 plan，而不是继续各自凭 heuristics 补分支。
  - 这项与 `Plan_NetworkReplicationTests.md` 的边界是：后者负责复制能力与测试矩阵，本项只负责把 `ClassGenerator` 内部的成员物化 owner 收口成单一真相，避免 replication/editor/serialization 再借 GC lane 旁路进入系统。
  - 来源：
    - [A] `ClassGenerator_Analysis.md` — “发现 23/28：`ExposeOnSpawn` / `EditFixedSize` / `EditorOnly`、`ReplicatedUsing` / `SaveGame` / `Config` 等语义在 `SoftReloadOnly` 下继续沿用旧 `FProperty` flags”
    - [B] `ClassGenerator_Plan.md` — “property metadata 变化目前只到 `Suggested`，full path 才真正写 `CPF_*` 与 metadata 驱动 flag”
    - [D] `TypeSystem_ArchReview.md` — “Arch-TS-49：`GC/Replication/Serialization/Editor` 物化理由缺少独立 `PropertyMaterialization` owner”
    - [E] `GapAnalysis.md` — “参考 UnrealCSharp 把 property family 决策集中到 descriptor / factory，不再额外维护一条 synthetic property lane”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` L298-L338、L2920-L3018，`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` L894-L908 — 当前 hidden property 进入条件仍只围绕 `NeverRequiresGC()/RequiresProperty()`，non-struct synthetic lane 默认 `Transient`；真正的 `CPF_Net/CPF_RepNotify` 只在 exported-property path 写入，而 runtime replication list 只扫描已存在且带 `CPF_Net` 的真实 `FProperty`
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`、可选新增 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASPropertyMaterializationPlan.h/.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptPropertyMaterializationPlanTests.cpp`
- [ ] **P5.2** 📦 Git 提交：`[ClassGenerator] Refactor: centralize property materialization plan`
- [ ] **P5.2-T** 单元测试：补齐 hidden/reflected property lane 与 UE 语义分流回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptPropertyMaterializationPlanTests.cpp`
  - 测试场景：
    - 正常路径：显式 reflected + replicated property 会稳定 materialize 成真实 `FProperty`，并带正确 `CPF_Net/CPF_RepNotify`、editor metadata flag 与 runtime replication list
    - 边界条件：GC-only hidden property 仍保持 `HiddenTransientProperty` 行为，不参与 replication/editor exposure；`EditorOnly` / `ExposeOnSpawn` / `EditFixedSize` 之类 metadata 只会在 plan 选择 reflected lane 时生效
    - 错误路径：脚本成员声明了 replication/savegame/editor/serialization 意图但 plan 仍只能落在 hidden lane 时，生成阶段会给出稳定 compile error/diagnostic，而不是静默生成一个看似成功的 transient GC 字段
  - 测试命名：`Angelscript.TestModule.ClassGenerator.PropertyMaterialization.<CaseName>`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P5.2-T** 📦 Git 提交：`[ClassGenerator] Test: cover property materialization reasons and reflected lanes`

- [ ] **P5.3** 把 `AdditionalCompileChecks` 升级成 runtime-usable participant/finalize phase，修正 `OnPostReload` 先发再检查的顺序
  - 当前热重载扩展缝仍是 editor-only 的 `AdditionalCompileChecks`：compile/post-reload hook 都包在 `#if WITH_EDITOR` 里，`PostReloadAdditionalChecks()` 还发生在 `OnPostReload` 之后，返回值也无法表达 `repair` 或 `abort`。这让扩展层只能做日志或副作用，不能真正参与当前 batch 的 finish 边界。
  - 这条任务不重复外层 watcher 矩阵，只收 `ClassGenerator` 里的 participant 生命周期：先把现有 map 包一层兼容 adapter，统一形成 `PreAnalyze / PreCommit / Finalize / Finished / Aborted` 相位；旧 `OnPostReload(bool)` 由新 `Finished` 事件派生，`Finalize` 在 finished 之前完成，runtime `SoftReloadOnly` 路径也能收到同样的 context，而不是只在 editor 下工作。
  - 如果首版暂时不引入完整 session struct，至少也要先把 `PostReloadAdditionalChecks()` 前移到 `OnPostReload` 之前，并给 result enum 留出 `Continue / NeedsRepair / Abort` 三档，避免继续把“已宣布完成的 reload”交给扩展层补救。
  - 来源：
    - [C] `ClassGenerator_TestGaps.md` — “NewTest-25：`FAngelscriptAdditionalCompileChecks` 的 compile/reload hook 调用当前零覆盖”
    - [D] `HotReloadArch_ArchReview.md` — “Arch-HR-39：`AdditionalCompileChecks` 仍是 editor-only 隐式扩展缝；Arch-HR-40：`PostReloadAdditionalChecks` 发生在 `OnPostReload` 之后”
    - [E] `GapAnalysis.md` — “D4 当前缺的是 `execution epoch / prepare-finish observer` 合同，而不是再造一套 reload 框架”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptAdditionalCompileChecks.h` L4-L8，`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` L1370-L1387、L2395、L2472-L2487 — 当前 compile/post-reload hook 仅在 `WITH_EDITOR` 下执行，`PostReloadAdditionalChecks()` 是 `void`，并且发生在 `OnPostReload.Broadcast(...)` 之后
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptAdditionalCompileChecks.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、可选新增 `Plugins/Angelscript/Source/AngelscriptRuntime/HotReload/AngelscriptHotReloadParticipant.h/.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadParticipantTests.cpp`
- [ ] **P5.3** 📦 Git 提交：`[HotReload/ClassGenerator] Refactor: promote compile checks to runtime participants`
- [ ] **P5.3-T** 单元测试：补齐 participant phase、legacy adapter 与 finish ordering 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadParticipantTests.cpp`
  - 测试场景：
    - 正常路径：runtime `SoftReloadOnly` 与 editor full reload 都会命中 participant compile/finalize 回调；legacy `AdditionalCompileChecks` 通过 adapter 仍然能被调用
    - 边界条件：participant 返回 `NeedsRepair` 时，`Finished/OnPostReload` 只会在 repair 完成后广播；同一 session 内的 context 会稳定携带 compile type、affected module/class 与 `bIsFullReload`
    - 错误路径：participant 返回 `Abort` 时，本轮不会提前广播 finished/`OnPostReload`，旧 live state 保持可用，batch/adapter 不会出现双触发或顺序漂移
  - 测试命名：`Angelscript.TestModule.HotReload.Participant.<CaseName>`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P5.3-T** 📦 Git 提交：`[HotReload/ClassGenerator] Test: cover participant phases and legacy compile-check adapters`

### 单元测试总览补充

| 改进项 | 测试文件 | 测试场景 | 优先级 |
| --- | --- | --- | --- |
| `P3.4` | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadRollbackTests.cpp` | finalize/verify/subsystem 失败 rollback、旧类保活、禁止成功式 post-reload 广播 | P0 |
| `P5.1` | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadMethodPatchTests.cpp` | body-only method patch、constructor replay 控制、对象级恢复 hook | P1 |
| `P5.2` | `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptPropertyMaterializationPlanTests.cpp` | hidden/reflected lane、`CPF_*` 物化、replication/editor/serialization 分流 | P1 |
| `P5.3` | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadParticipantTests.cpp` | runtime participant、legacy adapter、finalize/finished ordering、abort/repair | P1 |

### 验收补充

11. late validation / verify 失败不再把坏类、坏 CDO 或坏 subsystem 先提交进 live graph；失败轮次里 authoritative state 始终保持为旧版本。
12. 真正安全的 body-only delta 可以在不重建整类对象图的前提下更新逻辑；需要 replay constructor 的场景会被显式分类，且对象可通过 hook 恢复非属性状态。
13. 脚本成员“为什么需要真实 `FProperty`”有单一 `PropertyMaterializationPlan`；replication/editor/serialization 不再继续借 GC-only hidden lane 旁路进入系统。
14. hot reload 扩展点具备 runtime/editor 一致的 participant / finalize / finished phase；`OnPostReload` 不再早于扩展 finalize，abort/repair 能在 finish 前被系统化处理。

### 风险补充

8. **rollback barrier 会同时触碰 reload 广播、subsystem 生命周期与旧类保活**
   - 缓解：首阶段把 rollback 收敛到最小 commit set，只覆盖 class registration / CDO / pending subsystem replacement / success-style delegates；用 targeted rollback tests 先锁定旧类仍可用，再逐步放大撤销范围。
9. **method-patch fast path 最容易误分流“看似 body-only、实则影响语义”的改动**
   - 缓解：delta classifier 必须极保守，宁可多回退到现有 `DoSoftReload()` / `FullReload`，也不要错误跳过 class-level rebuild；首版加 trace / counter 证明 fast path 没有误命中结构变化。
10. **`PropertyMaterializationPlan` 会让部分历史上“能编译但语义模糊”的成员声明提前报错**
   - 缓解：第一阶段先让 plan 反向生成当前行为，再只对明显违反 `Replication/Serialization/EditorExposure` 合同的组合升级为明确 diagnostic；同时补成员矩阵测试，避免误伤既有 GC-only lane。
11. **participant / finished phase 重构存在新旧事件双触发与顺序漂移风险**
   - 缓解：迁移期要求旧 `OnPostReload(bool)` 严格从新 `Finished` 事件派生，legacy `AdditionalCompileChecks` 只通过 adapter 接入；测试里显式断言 single-fire 与 ordering，而不是只看“有没有被调到”。 

---

## 深化 (2026-04-09 01:29)

- 本轮不重复前文已经独立成项的版本链、构造事务、组件布局、function runtime contract 与 participant phase；只补四个仍停留在分析结论、但尚未在输出里拆成独立执行项的 `ClassGenerator` owner 缺口。
- 这四项分别落在 `soft reload` 的 class-level 反射 contract、`CPF_Config` 默认值来源、editor-only 模块分类一致性，以及 debug-build 专属的 `DebugValues` 生命周期；它们都能在当前源码中直接定位，且与既有活跃 Plan 没有主题重叠。

### Phase 2 补充：反射 contract 与默认值来源

- [ ] **P2.8** 把 class `ClassFlags`、class metadata 与 `SoftReloadOnly` 决策收口成同一份 `ReflectionContract`
  - 当前 `DoSoftReload()` 会立即改写 `CLASS_NotPlaceable`、`CLASS_Abstract`、`CLASS_Transient`、`CLASS_HideDropDown`、`CLASS_DefaultToInstanced`、`CLASS_EditInlineNew`、`CLASS_Deprecated`，但 `DisplayName`、`Blueprintable/NotBlueprintable`、`IsBlueprintBase`、`HideCategories`、`BlueprintSpawnableComponent` 这类 class metadata 仍只在 new/full class 路径重放。结果是同一个 live `UASClass` 能稳定进入“新 flags + 旧 metadata”的混合纪元。
  - 这一项不重复前文对 function dispatch 或 property materialization 的 owner，而是单列 class-level 反射身份的提交边界：先在 diff 阶段把 `FlagsOnlySafe`、`MetaOnlyCosmetic`、`FlagsAndMetaCoupled` 分开，再决定是走白名单 `RefreshClassMetadataSoft()`，还是直接升级为 `FullReloadRequired` / `DeferredClassIdentityReload`。
  - 首版优先把 `Blueprintable/NotBlueprintable/IsBlueprintBase`、`HideCategories`、`BlueprintSpawnableComponent`、`Abstract/NotPlaceable/DefaultToInstanced/EditInlineNew` 这类会改变 Blueprint/editor contract 的键列为 unsafe；`DisplayName/ToolTip` 等纯展示键才允许 soft refresh，避免继续在运行中半提交 class identity。
  - 来源：
    - [A] `ClassGenerator_Analysis.md` — “发现 22：`Blueprintable/NotBlueprintable` 只在 full reload 写入 `UClass` metadata”
    - [A] `ClassGenerator_Analysis.md` — “发现 34/35：`HideCategories` / `BlueprintSpawnableComponent` 的 soft reload 漂移与测试空白”
    - [D] `HotReloadArch_ArchReview.md` — “Arch-HR-41：`SoftReload` 会即时改写 `ClassFlags`，但大部分类 metadata 仍停留在旧版本”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` L3314、L3329、L3383、L4208、L4261 — 当前 full/new class 路径会写 `DisplayName/Blueprintable/HideCategories`，但 `DoSoftReload()` 只回放 `ClassFlags` 与 `FUNCMETA_ScriptNoOp`
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadClassReflectionContractTests.cpp`
- [ ] **P2.8** 📦 Git 提交：`[ClassGenerator/HotReload] Fix: unify class reflection contract in soft reload`
- [ ] **P2.8-T** 单元测试：补齐 class-level flags 与 metadata 的 soft reload contract
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadClassReflectionContractTests.cpp`
  - 测试场景：
    - 正常路径：仅 `DisplayName` / `ToolTip` 这类白名单 metadata 变化时，live `UClass` 能在不重建 class 壳的前提下刷新到新值
    - 边界条件：`Blueprintable <-> NotBlueprintable`、`HideCategories`、`BlueprintSpawnableComponent`、`DefaultToInstanced` 等 unsafe contract 变化会稳定升级为 full reload 或 `DeferredClassIdentityReload`，不会留下“新 flags + 旧 metadata”
    - 错误路径：`SoftReloadOnly` 命中 unsafe class identity 变化时，会返回可读 diagnostic / `ErrorNeedFullReload`，而不是先改 flags 再继续运行旧 metadata
  - 测试命名：`Angelscript.TestModule.HotReload.ClassReflectionContract.<CaseName>`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P2.8-T** 📦 Git 提交：`[ClassGenerator/HotReload] Test: cover class reflection contract gating and metadata refresh`

- [ ] **P2.9** 把 `bModifiedByDefaults` 拆成 `ScriptDefaults` 与 `ConfigDefaults`，禁止普通 hot reload 顺带回灌 config 漂移
  - 当前 `PrepareSoftReload()` 明确把 `Config` 属性和 script `default` 归成同一类来源写进 `CDONoDefaults`，随后 `DoSoftReload()` 又把 `BaseCDO != CDONoDefaults` 统一折成 `bModifiedByDefaults`，并在 live instance / `CDO` 迁移时优先复制。这样一次与配置无关的 body-only reload 也会把 `CPF_Config` 的旧值当成“脚本默认值变化”回灌到新对象里。
  - 这一项是对前文 `P2.1` 的细化，不重复 `ConfigName/DefaultConfig` 的 class-level reload gate，而是单列“为什么某个字段需要复制”的来源 owner。`FPropertyCopy` 需要显式区分 `ScriptDefaults`、`ConfigDefaults`、`ParentCDOOnly`，session summary 与 copy policy 也要能回答本轮值迁移到底来自脚本还是 config。
  - 首版优先增加 `angelscript.ReapplyConfigOnSoftReload` 或等价 session policy，默认在普通 `.as` hot reload 中只自动传播 script defaults；若项目确实需要“保存脚本时顺带刷新 ini”，则应改成显式 `ConfigReload` request / participant，而不是继续借 `bModifiedByDefaults` 旁路进入。
  - 来源：
    - [A] `ClassGenerator_Analysis.md` — “发现 13/40：`Config=<Name>` / `DefaultConfig` 只在 full reload 写入，soft reload 的配置语义长期漂移”
    - [D] `HotReloadArch_ArchReview.md` — “Arch-HR-42：`CDONoDefaults` 把 `Config` 与 script `default` 归入同一迁移来源”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` L4093、L4105、L4490、L4580、L4681 — 当前 `CDONoDefaults` 明确卸载 `Config` 属性，后续 `bModifiedByDefaults` 被统一用于实例与 `CDO` 回拷
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadConfigDefaultPolicyTests.cpp`
- [ ] **P2.9** 📦 Git 提交：`[ClassGenerator/HotReload] Fix: separate config defaults from script default replay`
- [ ] **P2.9-T** 单元测试：补齐 `CPF_Config` 字段在 hot reload 下的默认值来源策略
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadConfigDefaultPolicyTests.cpp`
  - 测试场景：
    - 正常路径：非 `CPF_Config` 的 script `default` 变化仍会按既有 contract 传播到新 `CDO` 与未覆写实例
    - 边界条件：仅函数 body 改动但存在 `CPF_Config` 字段时，关闭 `ReapplyConfigOnSoftReload` 后不会顺带改写 live instance；显式开启策略或显式 `ConfigReload` 时才会重放 config-backed defaults
    - 错误路径：一次普通 `SoftReloadOnly` 不会静默把 config 漂移伪装成 script defaults；session diagnostic 能指出 `ConfigDefaults` 命中而不是悄悄复制
  - 测试命名：`Angelscript.TestModule.HotReload.ConfigDefaultPolicy.<CaseName>`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P2.9-T** 📦 Git 提交：`[ClassGenerator/HotReload] Test: cover config default replay policy and diagnostics`

### Phase 4 补充：模块身份与 debug 构建契约

- [ ] **P4.2** 统一 editor-only 模块分类，收口 `IsDeveloperOnly()` 与 `DefaultComponent` 校验的单一事实源
  - 当前 `UASClass::IsDeveloperOnly()` 只认 `Dev.` 与 `Editor.` 前缀，但 `FinalizeActorClass()` 的 editor-only `DefaultComponent` attach/root 校验又直接依赖这个返回值；一旦模块名采用 `Game.Tools.Editor.Visualizers` 这类嵌套 `.Editor.` 形式，同一脚本会在编译阶段被当成 editor-only，在类生成校验阶段又被当成 runtime actor。
  - 这项不改动更大的 module descriptor 设计，只先把“模块是否 editor-only”抽成共享 helper 或缓存字段，例如 `IsEditorOnlyModuleName()` / `bIsEditorOnlyModule`，让 `UASClass::IsDeveloperOnly()` 与 `FinalizeActorClass()` 都消费同一份分类结果，而不是继续各自维护字符串规则。
  - 实现时要保留 `Dev.` 这条显式开发模块规则，但不能再由 `UASClass::IsDeveloperOnly()` 自行做局部前缀猜测；否则后续任何 editor-only attach/root 校验都还会在 runtime metadata API 上读到另一套答案。
  - 来源：
    - [A] `ClassGenerator_Analysis.md` — “发现 71：`IsDeveloperOnly()` 与编译阶段的 editor-only 模块判定规则不一致，嵌套 `*.Editor.*` 模块会被错误当成非编辑器类”
    - [B] `ClassGenerator_Plan.md` — “Issue-21：需要把 `IsDeveloperOnly()` 改成共享 helper / module-level 缓存字段，而不是局部字符串规则”
    - [C] `ClassGenerator_TestGaps.md` — “NewTest-11：补齐 `UASClass::IsDeveloperOnly()` 的嵌套 `.Editor.` 模块判定测试”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` L1523、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` L5609、L5647 — 当前 `IsDeveloperOnly()` 仍只检查前缀，而 `FinalizeActorClass()` 直接用它决定 editor-only `DefaultComponent` attach/root 是否允许
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASClassDeveloperOnlyTests.cpp`
- [ ] **P4.2** 📦 Git 提交：`[ClassGenerator] Fix: unify developer-only module classification`
- [ ] **P4.2-T** 单元测试：补齐 `IsDeveloperOnly()` 与 editor-only 组件校验的一致性回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASClassDeveloperOnlyTests.cpp`
  - 测试场景：
    - 正常路径：`Editor.Foo` 与 `Dev.Foo` 模块生成的 `UASClass` 返回 `IsDeveloperOnly() == true`，editor-only `DefaultComponent` attach/root 校验可按 editor-only actor 处理
    - 边界条件：`Game.Tools.Editor.Visualizers` 这类嵌套 `.Editor.` 模块也会与编译阶段分类一致，runtime 普通模块仍返回 `false`
    - 错误路径：runtime 模块上的 non-editor actor 继续禁止 editor-only `DefaultComponent` 作为 attach parent / root，不会因为统一 helper 误放宽
  - 测试命名：`Angelscript.TestModule.ClassGenerator.DeveloperOnly.<CaseName>`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P4.2-T** 📦 Git 提交：`[ClassGenerator] Test: cover developer-only module classification and editor-only component rules`

- [ ] **P4.3** 恢复 `WITH_AS_DEBUGVALUES` 的对象级 `DebugValues` 生命周期 contract，禁止 debug build 停在 stub API
  - 当前 `UASClass` 和 `ClassGenerator` 仍会在对象构造/析构与 property 解析阶段读写 `DebugValues`：类壳里持有 `FDebugValuePrototype`，reload/build debug prototype 时继续 `Reset()/CreateDebugValue()`，对象创建与销毁时继续调用 `Instantiate()/Free()`。但 discovery 已确认真正生效的 debug-value 头只剩 stub，意味着 debug build 的调用侧与实现侧已经失配。
  - 这项要先恢复最小可用的 per-object API，再把 object ctor/dtor 与 debug frame 的释放语义收口成对称 contract。若恢复完整实现需要额外 owner，优先用 `FScopedASDebugValues` 一类小 helper 把对象级与 stack-frame 级释放路径统一起来，而不是继续让调用点直接假定 `Instantiate()/Free()` 永远存在。
  - 这里先补 `ClassGenerator` 侧的 debug-build correctness，不把范围扩成整套 debugger 架构重写；目标是让最需要排查 reload/ctor/dtor 问题的 debug 配置重新具备可编译、可构造、可析构的最小闭环。
  - 来源：
    - [A] `ClassGenerator_Analysis.md` — “发现 70：`WITH_AS_DEBUGVALUES` 打开时 `UASClass` 构造/析构路径调用了一个已被注释掉的 `FDebugValuePrototype` API”
    - [B] `ClassGenerator_Plan.md` — “Issue-33：需要恢复 `Instantiate()/Free()` 和 `FDebugValues` 的完整 per-object contract”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h` L66、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` L4939、L4954、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` L967、L1382、L1449、L1479 — 当前类壳仍持有 `DebugValues`，property 解析仍构建 debug prototype，对象 ctor/dtor 仍调用 `Instantiate()/Free()`
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDebugValue.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASDebugValuesTests.cpp`
- [ ] **P4.3** 📦 Git 提交：`[ClassGenerator/Debug] Fix: restore debug-values lifecycle under WITH_AS_DEBUGVALUES`
- [ ] **P4.3-T** 单元测试：补齐 debug build 下 `DebugValues` 的对象级生命周期回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASDebugValuesTests.cpp`
  - 测试场景：
    - 正常路径：在 `WITH_AS_DEBUGVALUES` 打开的构建里，script actor/component/object 的构造会初始化 `Object->Debug`，析构会稳定走 `Free()`，且调用次数成对
    - 边界条件：full reload / soft reload 重建 debug prototype 后，新对象与 debug frame 仍能从最新 prototype 实例化变量，不留下 orphaned debug block
    - 错误路径：ctor/defaults/reload 失败或 `WITH_AS_DEBUGVALUES` 关闭时，不会出现双重释放、缺失符号或残留未释放 `DebugValues`；no-op 分支与 debug 分支都能保持编译自洽
  - 测试命名：`Angelscript.TestModule.ClassGenerator.DebugValues.<CaseName>`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P4.3-T** 📦 Git 提交：`[ClassGenerator/Debug] Test: cover debug-values object lifecycle and reload rebuild`

### 单元测试总览补充

| 改进项 | 测试文件 | 测试场景 | 优先级 |
| --- | --- | --- | --- |
| `P2.8` | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadClassReflectionContractTests.cpp` | class flags/class metadata 半提交、unsafe reflection contract 分流、whitelist metadata soft refresh | P1 |
| `P2.9` | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadConfigDefaultPolicyTests.cpp` | `ScriptDefaults` vs `ConfigDefaults`、config replay policy、session diagnostic | P1 |
| `P4.2` | `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASClassDeveloperOnlyTests.cpp` | `IsDeveloperOnly()`、嵌套 `.Editor.` 模块、editor-only `DefaultComponent` attach/root | P1 |
| `P4.3` | `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASDebugValuesTests.cpp` | `WITH_AS_DEBUGVALUES` 构造/析构、reload 后 prototype rebuild、debug/no-op 分支自洽 | P0 |

### 验收补充

15. `SoftReloadOnly` 不再产生“新 `ClassFlags` + 旧 class metadata”的混合 `UClass` 身份；unsafe reflection contract 会被明确分流到 full reload 或 defer 诊断。
16. `CPF_Config` 字段的默认值来源能与 script `default` 分开统计和控制；普通 `.as` hot reload 不会再静默重放 config 漂移。
17. `IsDeveloperOnly()` 与编译阶段的 editor-only 模块分类保持一致，`Dev.` / `Editor.` / 嵌套 `.Editor.` 模块在 `DefaultComponent` attach/root 校验中不再自相矛盾。
18. `WITH_AS_DEBUGVALUES` 打开时，script object 构造/析构、reload 后 prototype rebuild 与 debug frame 变量采样形成可编译、可执行、可回收的闭环。

### 风险补充

12. **class metadata 白名单切分过粗，会在“应该 full reload”和“可安全 soft refresh”之间误判**
   - 缓解：首版只对白名单极小集合开放 soft refresh，并用 targeted regression 显式锁住 `Blueprintable`、`HideCategories`、`BlueprintSpawnableComponent` 等高风险 contract。
13. **把 `ConfigDefaults` 从 `bModifiedByDefaults` 拆出来后，可能暴露历史项目对“保存脚本顺带刷新 ini”行为的隐式依赖**
   - 缓解：先引入 diagnostics 与显式 policy switch，再决定默认策略；测试里同时保留 policy on/off 两套回归。
14. **editor-only 模块分类一旦出现双真相，后续 `DefaultComponent` 校验与 runtime metadata API 还会继续分叉**
   - 缓解：只保留一个共享 helper / 缓存字段给 `UASClass::IsDeveloperOnly()` 与类生成校验共用，不接受第二套局部字符串规则。
15. **恢复 `DebugValues` 的完整 API 可能把之前被宏分支遮住的 debug-only 生命周期 bug 一次性暴露出来**
   - 缓解：先把 per-object `Instantiate()/Free()` 与 debug frame 的最小 contract 拉通，再用 debug-build regression 锁住“初始化一次、释放一次、reload 后可重建”的对称性。 

---

## 深化 (2026-04-09 01:42)

- 现有输出已经覆盖 class/version/function/transaction 主线，但 `ClassGenerator` 里还有三条当前源码可直接复现、且尚未独立落项的 contract 缺口：non-class symbol 的 `SoftReloadOnly` barrier、组件树跨构建 fail-closed 一致性、以及对外可写却没有任何 materialization 闭环的 `ComposeOntoClass`。
- 本轮不重复 `Documents/Plans/Plan_InterfaceBinding.md` 的 interface 主线，也不展开 `Documents/Plans/Plan_UnrealCSharpArchitectureAbsorption.md` 的长期 descriptor/projection 设计；这里只补当前 `ClassGenerator/` 范围内仍成立的 correctness / fail-closed stopgap。

### Phase 2 补充：non-class symbol barrier 与组件树跨构建一致性

- [ ] **P2.10** 把 enum/delegate 的结构变更从“分析层局部 flag”收口成 symbol-owned full reload barrier
  - 现有 `P2.6/P2.7` 已经开始收口 class/module/type dependency 的执行真值，但 enum/delegate 仍留在一条明显分叉的旧路径上：`AnalyzeEnums()` 会漏掉纯 value diff，delegate 签名变更虽然已在分析层升到 `FullReloadRequired`，`SoftReloadOnly` 执行层却仍会把 existing symbol 链回旧 `UEnum/UDelegateFunction`。
  - 这一项只补 non-class symbol 的 correctness，不提前引入完整 `FASReloadPlan`。目标是让 `Setup()`、`ShouldFullReload(FEnumData/FDelegateData)`、`LinkSoftReloadClasses()` 与 soft-path barrier 对同一份 symbol 结构变更给出同一个 verdict：要么真正 full reload，要么显式拒绝 swap-in，不能再出现“新脚本模块 + 旧反射壳”混跑。
  - 落地时先修 `AnalyzeEnums()` 的 `EnumValues` 自比较，再提取 `HasEnumDefinitionChanged()` / `NeedsDelegateReinstancing()` 一类 helper；`ShouldFullReload(FEnumData/FDelegateData)` 和 `PerformReload(false)` 的 preflight 只消费这些 helper，`LinkSoftReloadClasses()` 仅保留 truly safe 的 existing-body-neutral case。
  - 来源：
    - [A] `ClassGenerator_Analysis.md` — “delegate 签名已经被分析器判成 `FullReloadRequired`，`SoftReloadOnly` 仍会直接复用旧 `UDelegateFunction`”
    - [B] `ClassGenerator_Plan.md` — “Issue-49/50：`AnalyzeEnums()` 自比较漏判；existing enum / delegate 的结构变化在 `SoftReloadOnly` 下不会真正物化”
    - [C] `ClassGenerator_TestGaps.md` — “`HotReload.FullReload.EnumBasic` 只验证快照；`OnEnumChanged` / `OnDelegateReload` 缺少 targeted automation”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` L1676-L1687、L2096-L2113、L2148-L2178、L3881-L3887、L4016-L4022 — 当前 enum value diff 仍写成自比较，enum/delegate 的 `ShouldFullReload()` 在 soft path 不看 symbol 自身的结构变更，执行层会直接 `LinkSoftReloadClasses()` 复用旧 `UEnum/UDelegateFunction`
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadEnumDelegateContractTests.cpp`
- [ ] **P2.10** 📦 Git 提交：`[HotReload/ClassGenerator] Fix: enforce enum and delegate structural reload barriers`
- [ ] **P2.10-T** 单元测试：补齐 enum/delegate 在 `SoftReloadOnly` 与 full reload 下的结构变更合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadEnumDelegateContractTests.cpp`
  - 测试场景：
    - 正常路径：existing enum 的 value/name 变化与 existing delegate 的签名变化在允许 full reload 的会话里都会重建新壳，并触发 `OnEnumChanged` / `OnDelegateReload`
    - 边界条件：`SoftReloadOnly` 命中 existing enum value diff 或 delegate 参数/返回值变化时，会稳定升级成 full reload 或明确拒绝 swap-in；纯 body-only class 用例仍保留原有 soft path
    - 错误路径：不存在“编译成功但继续沿用旧 `UEnum/UDelegateFunction`”的 silent success；reload 后的查询、默认值解释和事件广播都不再读旧壳
  - 测试命名：`Angelscript.TestModule.HotReload.EnumDelegateContract.<CaseName>`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P2.10-T** 📦 Git 提交：`[HotReload/ClassGenerator] Test: cover enum and delegate structural reload contracts`

- [ ] **P2.11** 把 actor component layout 的关键约束从 editor-only authoring 检查提升为跨构建 fail-closed contract
  - 这一项是对现有 `P2.2` 的深化，不重复 layout builder 主线，而是先补一个更基础的 correctness 问题：当前 duplicate root、missing attach parent、override target 缺失/类型不兼容、editor-only parent-child 组合等关键约束仍主要依赖 `#if WITH_EDITOR` 校验，非 Editor 构建会直接落回 runtime fallback，把坏布局静默改写成另一棵组件树。
  - 目标不是让非 Editor 构建拥有完全相同的诊断文案，而是先统一“非法 layout 必须失败”的事实源。`FinalizeActorClass()`、`VerifyClass()` 与 `CreateDefaultComponents()` 要共享同一套 validator/defensive barrier，禁止继续一个构建报错、另一个构建容错生成。
  - 落地时优先提取无 `#if WITH_EDITOR` 的 `ValidateActorComponentLayout(...)` 或等价 helper，把 root 唯一性、override target 存在性与类型兼容、attach parent 合法性、editor-only root/attach 规则收口进去；Editor 分支只额外补 richer diagnostics，runtime 路径不再负责“自动修树”。
  - 来源：
    - [A] `ClassGenerator_Analysis.md` — “组件树合法性校验大量被 `WITH_EDITOR` 包住，非编辑器构建会静默生成与编辑器不同的层级”
    - [B] `ClassGenerator_Plan.md` — “Issue-34：重复 root、missing attach parent、abstract override 等关键约束只在 `WITH_EDITOR` 下校验”
    - [D] `HotReloadArch_ArchReview.md` — “`DefaultComponent` / `Attach` / `EditorOnly` 变化属于 actor construction contract，不能继续靠 deferred full reload 或旧 descriptor 顶着运行”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` L5233-L5237、L5290-L5442、L5467-L5675，`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` L1260-L1323 — 当前 root/override/attach/editor-only 的核心校验仍大量包在 `#if WITH_EDITOR` 内，而 runtime `CreateDefaultComponents()` 在 attach parent 缺失或多 root 情况下会直接重挂/改写组件树
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptActorComponentValidationParityTests.cpp`
- [ ] **P2.11** 📦 Git 提交：`[ClassGenerator] Fix: make actor component validation fail closed across builds`
- [ ] **P2.11-T** 单元测试：补齐组件树非法声明在 editor/non-editor 路径上的一致失败合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptActorComponentValidationParityTests.cpp`
  - 测试场景：
    - 正常路径：合法的 `DefaultComponent` / `RootComponent` / `Attach` / `OverrideComponent` 组合仍能稳定编译、生成 `CDO` 并构造出正确层级
    - 边界条件：duplicate root、missing attach parent、override target 不存在或类型不兼容、abstract parent component 未 override、editor-only parent-child 组合在 editor 与非 Editor 验证入口上都得到一致失败结论
    - 错误路径：运行时 defensive path 不再把非法布局悄悄改写成另一棵树；若 validator 未命中而 runtime 仍遇到坏状态，会输出明确 diagnostic 并中止当前类的组件 materialization
  - 测试命名：`Angelscript.TestModule.ClassGenerator.ComponentValidationParity.<CaseName>`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P2.11-T** 📦 Git 提交：`[ClassGenerator] Test: cover actor component validation parity across build paths`

### Phase 4 补充：未实现 feature 的 fail-closed contract

- [ ] **P4.4** 把 `ComposeOntoClass` 从 silent no-op 收口成 fail-closed contract，并为后续 compose owner 预留接缝
  - 当前 `ComposeOntoClass` 已经是一个对外可写、可序列化、可热重载恢复的字段，但类生成阶段只会把字符串解析成一个裸 `UClass*` 镜像；没有任何 property/function/defaults/CDO materialization 或 runtime instantiation 逻辑消费它。这意味着脚本作者会看到“语法 accepted、字段也保存了”，但运行时行为与普通类完全一致。
  - 这一项不直接展开长期 compose/projection 架构，只先做两件事：第一，把当前未实现状态改成稳定的 fail-closed 诊断；第二，在 `ClassGenerator/` 内预留一个明确的 `ComposePlan` / `HostProjection` 接缝，避免后续继续把 compose 逻辑散在 parser 字符串、`UASClass` 镜像字段和若干局部特判里。
  - 首阶段必须优先保正确性而不是“保留现状兼容”：只要脚本声明了 `ComposeOntoClass` 而当前 runtime 还没有可证明的 materialization pipeline，就直接编译失败；等 owner seam 落地后，再决定是否进入真正的 composed property/function/defaults 合成。
  - 来源：
    - [A] `ClassGenerator_Analysis.md` — “`ComposeOntoClass` 只被记录不被消费，动态 `UClass` 生成对该特性实际上是 no-op”
    - [A] `ClassGenerator_Analysis.md` — “现有自动化没有覆盖 `ComposeOntoClass`，也没有锁住它的失败契约”
    - [B] `ClassGenerator_Plan.md` — “Issue-24：应先把 silent no-op 改成显式错误，再定义 `ComposePlan`”
    - [D] `TypeSystem_ArchReview.md` — “Arch-TS-43：`ComposeOntoClass` 仍是 parser/class shell 旁路字段，未进入 canonical identity”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` L1334-L1368、L5051-L5056，`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h` L27 — 当前仅在分析阶段对 `ComposedStruct` 做额外检查、在 finalize 阶段按字符串命中后把 `ComposeOntoClass` 镜像写进 `UASClass`，看不到任何后续 materialization/instantiation 消费
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`、可选新增 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASComposePlan.h/.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptComposeOntoClassTests.cpp`
- [ ] **P4.4** 📦 Git 提交：`[ClassGenerator] Fix: fail closed on unsupported ComposeOntoClass`
- [ ] **P4.4-T** 单元测试：补齐 `ComposeOntoClass` 的 fail-closed 与 target-resolution 合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptComposeOntoClassTests.cpp`
  - 测试场景：
    - 正常路径：在当前“未实现 materialization”的阶段，声明 `ComposeOntoClass` 会得到稳定 compile error，且不会把一个没有 compose 效果的类发布到 runtime
    - 边界条件：compose target 缺失、target rename/remove、`ComposedStruct` metadata 组合不合法时，都能给出确定性的 target-aware diagnostic
    - 错误路径：不存在“编译成功但 `UASClass::ComposeOntoClass == nullptr` / 无后续消费”的 silent success；失败轮次也不会继续创建 `CDO` 或发布半有效类壳
  - 测试命名：`Angelscript.TestModule.ClassGenerator.ComposeOnto.<CaseName>`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P4.4-T** 📦 Git 提交：`[ClassGenerator] Test: cover ComposeOntoClass fail closed and target diagnostics`

### 单元测试总览补充

| 改进项 | 测试文件 | 测试场景 | 优先级 |
| --- | --- | --- | --- |
| `P2.10` | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadEnumDelegateContractTests.cpp` | enum value/name diff、delegate signature diff、`SoftReloadOnly` barrier、`OnEnumChanged` / `OnDelegateReload` | P0 |
| `P2.11` | `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptActorComponentValidationParityTests.cpp` | duplicate root、missing attach parent、override target/type mismatch、editor/non-editor fail-closed parity | P0 |
| `P4.4` | `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptComposeOntoClassTests.cpp` | `ComposeOntoClass` fail-closed、target-resolution diagnostics、禁止 silent no-op publish | P1 |

### 验收补充

19. existing enum 的 value/name 变化与 existing delegate 的签名变化，不再在 `SoftReloadOnly` 下静默复用旧 `UEnum/UDelegateFunction`；分析层与执行层对 symbol 结构变化的结论保持一致。
20. actor component layout 的 duplicate root、missing attach parent、override target/type mismatch、editor-only root/attach 等非法声明，在 editor 与非 Editor 路径上都能稳定 fail-closed，不再由 runtime fallback 悄悄改写组件树。
21. `ComposeOntoClass` 在真正的 compose materialization owner 落地前，不再以 silent no-op 方式“假成功”；要么给出明确 compile error，要么在 future owner 接入后有一条单一 `ComposePlan`/projection seam 负责发布。

### 风险补充

16. **把 enum/delegate 结构变更从“表面成功”改成 barrier 后，会收紧一批历史 `SoftReloadOnly` 工作流**
   - 缓解：先给出清晰 diagnostic 与 full-reload/abort 结果，再用 dedicated regression 锁住真正的 body-only 对照场景，避免误伤安全 case。
17. **把组件树 validator 下沉到 runtime-core 后，会暴露过去依赖 fallback 的脚本资产**
   - 缓解：首阶段保留最小 runtime defensive diagnostic，但不再自动修树；用 parity tests 先冻结“哪些非法布局必须失败”，再逐步收紧文案与恢复策略。
18. **将 `ComposeOntoClass` 改成 fail-closed 可能立刻增加编译失败数量**
   - 缓解：先把 diagnostic 做到目标类名、声明点和当前未支持原因都明确可读；后续若要真正实现 compose，再基于单一 `ComposePlan` seam 渐进放开，而不是继续让 silent no-op 存活。 

---

## 深化 (2026-04-09 01:51)

- `Documents/Plans/Plan_InterfaceBinding.md` 与 `Documents/Plans/Plan_CppInterfaceBinding.md` 已覆盖更大的 `C++ UInterface` 自动绑定、`FInterfaceProperty` 与 authoring policy 主线。本轮不重复那些能力扩张，只补 `ClassGenerator/` 当前源码仍成立的五条 interface/namespace correctness 缺口；这些条目属于 runtime owner 止血项，若不先收口，后续 interface 计划会继续建立在不稳定的 identity / graph / ABI 上。

### Phase 1 补充：namespace 与类型身份单一主键

- [ ] **P1.5** 把 script class / interface 的 identity 从短名升级为 qualified key，阻断 namespace 场景下的交叉替换与错误版本链
  - 当前 `ClassGenerator` 已经能通过 `GetNamespacedTypeInfoForClass()` 在 AngelScript 模块内按 `Namespace + ClassName` 取到正确 `asITypeInfo`，但进入 UE 注册与 reload 路径后又退回成短名：`DataRefByName`、`GetClassDesc()`、old/new class 匹配、`GetUnrealName()`、`CreateFullReloadClass()` 和 interface placeholder 注册都只看 `ClassName`。这意味着 `Foo::AThing` / `Bar::AThing`、`Foo::IDamageable` / `Bar::IDamageable` 会共享 descriptor key、`UObject` 名位与 replace 入口。
  - 这一项只收口 `ClassGenerator/` 内的 identity owner，不展开 `FInterfaceProperty`、`Bind_UObject` 或 UHT sidecar 主线。首版目标是先让 qualified key 进入 `DataRef`、reload 选择、`FindObject`/`NewObject` 命名与 interface placeholder 注册，legacy 裸短名只保留 alias/诊断用途，不能继续参与 replace/reload 主路径。
  - 落地时优先在 `ClassGenerator/` 新增 `FASQualifiedTypeKey` 或等价 helper，统一提供 script lookup name、UE object name 与 display name 三种变体；`GetClassDesc()`、旧类匹配、`CreateFullReloadClass()`、`ResolveInterfaceClass()` 与 interface script-type 补注册都改成先吃 qualified key，再决定兼容 alias 是否可接受。
  - 来源：
    - [A] `ClassGenerator_Analysis.md` — “类生成器一边支持 AngelScript namespace，一边又用未限定 `ClassName` 做 UClass 命名和查找，命名空间类会互相覆盖”
    - [A] `ClassGenerator_Analysis.md` — “script interface 注册完全绕过 namespace，同短名接口会共享一份 engine-level `asITypeInfo`”
    - [B] `ClassGenerator_Plan.md` — “Issue-18：类型身份仍以短名为核心键，namespace 在 UE 注册层被整体丢弃”
    - [D] `TypeSystem_ArchReview.md` — “Arch-TS-07 / Arch-TS-36：类型 canonical key 仍偏字符串/短名 heuristic，namespace 与 interface identity 缺少统一 nominal resolver”
    - [E] `GapAnalysis.md`、`CrossComparison.md` D2 — “参考实现更接近 registry-first 的 canonical identity，而不是 name-only lookup + fallback scan”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` L145-L166、L1739-L1766、L2570-L2607、L5912-L5934 — script type 查询已读取 `Namespace`，但 `DataRefByName`、旧类匹配、`GetUnrealName()`、full reload 替换和 interface `RegisterObjectType/GetTypeInfoByName` 仍全部只用短名
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、可选新增 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASQualifiedTypeKey.h/.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptNamespacedTypeIdentityTests.cpp`
- [ ] **P1.5** 📦 Git 提交：`[ClassGenerator] Fix: key script types by qualified identity`
- [ ] **P1.5-T** 单元测试：补齐 namespaced script class / interface 的 identity、reload 与 alias 合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptNamespacedTypeIdentityTests.cpp`
  - 测试场景：
    - 正常路径：两个不同 namespace 下的同短名 script class / interface 能稳定生成各自的 `UClass` / `asITypeInfo`，`StaticClass()`、`implements` 与 lookup 均指向正确目标
    - 边界条件：只对其中一个 namespace 类型做 full reload、remove/recreate 或 interface placeholder 重建时，另一个 namespace 的类型不会被 rename、替换或接入版本链
    - 错误路径：legacy 裸短名在产生歧义时给出明确 diagnostic，而不是把不同 namespace 类型静默并入同一个 `UObject` 名位
  - 测试命名：`Angelscript.TestModule.ClassGenerator.NamespaceIdentity.<CaseName>`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P1.5-T** 📦 Git 提交：`[ClassGenerator] Test: cover namespaced type identity and reload isolation`

### Phase 2 补充：interface 变更的 reload barrier

- [ ] **P2.12** 把 `ImplementedInterfaces` 的增删改升级成强制 class reinstancing barrier，禁止最后一个接口删除后继续走 `DoSoftReload()`
  - 现有 `ShouldFullReload(FClassData&)` 只在“新类本身是 interface”或“新类当前仍有 `ImplementedInterfaces`”时返回 true。结果是 `class Foo : UObject, IFoo` 在 `SoftReloadOnly` 会话里删除最后一个接口后，`ImplementedInterfaces.Num()` 变成 0，执行层会直接走 `DoSoftReload()`；而 `DoSoftReload()` 只更新 property/component/script-type/class flags，没有任何清空或重建 `UClass::Interfaces` 的步骤。
  - 这项与已有 `P2.1/P2.6` 同属 reload gate 收口，但它解决的是一个尚未独立落项的 interface contract：接口集合本身属于 class layout，不应继续被降级成“建议 full reload”或依赖 `ImplementedInterfaces.Num() > 0` 的局部启发式。首版只要求 interface 集合一旦变化，就 full reload 或明确拒绝 swap-in，不尝试保留 interface-set 级 soft relink。
  - 落地时需要抽出 `HasImplementedInterfaceLayoutChange()` 或等价 helper，把新增、删除、重排与传递接口差异统一折叠进 `ShouldFullReload()` / preflight barrier；同时在 `FinalizeClass()` materialize 接口前先 `NewClass->Interfaces.Reset()`，并在 `DoSoftReload()` 顶部添加 defensive assert/diagnostic，防止未来再有 interface-set 变化误入 soft path。
  - 来源：
    - [A] `ClassGenerator_Analysis.md` — “类描述的 `ImplementedInterfaces` 发生变化时，删除最后一个接口仍会保留旧 `UClass::Interfaces`”
    - [B] `ClassGenerator_Plan.md` — “Issue-22：移除最后一个 `ImplementedInterface` 时，`SoftReloadOnly` 会保留旧 `UClass::Interfaces`”
    - [D] `TypeSystem_ArchReview.md` — “Arch-TS-26：`ImplementedInterfaces` 仍以裸字符串保存并在 finalize 阶段 late-bind，接口 identity seam 不稳定”
    - [E] `CrossComparison.md` D2 — “`UInterface` 仍未进入统一 owner，接口集合变化不应继续依赖局部 heuristic”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` L2081-L2088、L4113-L4208、L5060-L5158 — full reload gate 仅检查新类当前是否还有 `ImplementedInterfaces`，`DoSoftReload()` 只重链 property/component/script-type，接口表重建仍只存在于 finalize 路径
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadImplementedInterfaceTests.cpp`
- [ ] **P2.12** 📦 Git 提交：`[ClassGenerator/HotReload] Fix: force full reload on implemented-interface layout changes`
- [ ] **P2.12-T** 单元测试：补齐 interface 集合变更的 full reload / reject 合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadImplementedInterfaceTests.cpp`
  - 测试场景：
    - 正常路径：新增或删除 interface 后，reload 会重建新类壳，`ImplementsInterface()` 与 `NewClass->Interfaces` 结果与源码声明一致
    - 边界条件：删除最后一个 interface、重排 interface 顺序、修改传递 interface 图时，都不会继续复用旧 interface 列表
    - 错误路径：`SoftReloadOnly` 命中 interface-set 差异时会稳定升级成 full reload 或明确拒绝，而不是保留 stale `UClass::Interfaces`
  - 测试命名：`Angelscript.TestModule.HotReload.ImplementedInterfaces.<CaseName>`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P2.12-T** 📦 Git 提交：`[ClassGenerator/HotReload] Test: cover implemented-interface reload barriers`

### Phase 4 补充：interface graph、ABI 与 native resolver

- [ ] **P4.5** 把 interface declaration 自身的 `ImplementedInterfaces` materialize 成真实 interface graph，而不是继续在 interface 壳路径里静默丢边
  - 预处理阶段已经会把 `interface IChild : IParent, IOther` 里的 secondary interface 写进 `ImplementedInterfaces`，但 `FinalizeClass()` 当前只在 `!ClassDesc->bIsInterface` 时构建 interface graph；一旦当前类本身是 interface，就直接 `FinalizeObjectClass()` 并 `return`。这会让 interface-to-interface 继承边在生成阶段丢失，后续 `AddInterfaceRecursive()` 也读不到这些边。
  - 这项只做 `ClassGenerator` 内的 graph materialization，不展开 `Plan_InterfaceBinding.md` 里的 C++ `UInterface` 自动绑定或 `FInterfaceProperty`。首版目标是先让 script interface shell 与普通 class 使用同一份 `BuildImplementedInterfaceGraph()` / `ResolveInterfaceRef()` 结果，保证 interface graph 在 `UClass::Interfaces` 上完整可见。
  - 落地时优先提取共享 helper，让 interface 壳也在 `FinalizeObjectClass()` 之前 materialize `ImplementedInterfaces`，并把实现类缺失方法校验改成基于 materialized graph 的传递闭包，而不是只信任源码直接声明的一级列表。
  - 来源：
    - [B] `ClassGenerator_Plan.md` — “Issue-2：interface 声明里的额外 `ImplementedInterfaces` 被解析后直接丢弃”
    - [D] `TypeSystem_ArchReview.md` — “Arch-TS-26 / Arch-TS-44：接口 identity 与实现挂接仍是 late-bound heuristic + `PointerOffset=0` 特例，graph owner 不稳定”
    - [E] `GapAnalysis.md`、`CrossComparison.md` D2 — “参考实现把 class/interface graph 当作一等输入，而不是 finalize 末端的旁路补线”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` L5059-L5158、L5189-L5198 — implemented interface graph 只在 `!ClassDesc->bIsInterface` 时构建，interface shell 随后直接 `FinalizeObjectClass()` 并返回
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、可选新增 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASInterfaceGraph.h/.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceGraphTests.cpp`
- [ ] **P4.5** 📦 Git 提交：`[ClassGenerator/Interface] Fix: materialize interface inheritance graph for interface shells`
- [ ] **P4.5-T** 单元测试：补齐 interface-to-interface 继承图与传递接口校验回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceGraphTests.cpp`
  - 测试场景：
    - 正常路径：`interface IChild : IParent, IOther` 与 `class Foo : UObject, IChild` 会把 `IChild/IParent/IOther` 全部 materialize 到 graph 中
    - 边界条件：diamond interface graph 去重稳定，传递 interface 的顺序变化不会生成重复 `FImplementedInterface`
    - 错误路径：secondary target 不是有效 interface，或实现类缺少传递 interface 所需方法时，编译稳定失败并给出指向性 diagnostic
  - 测试命名：`Angelscript.TestModule.Interface.Graph.<CaseName>`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P4.5-T** 📦 Git 提交：`[ClassGenerator/Interface] Test: cover interface inheritance graph materialization`

- [ ] **P4.6** 把 script interface 的 UE ABI 从“只保留函数名”升级成结构化 signature contract，并让生成/校验/dispatch 共用同一份签名真相
  - 当前 interface 生成链有三处都只看函数名：`DoFullReload()` 为 interface method 只创建最小 `UFunction` stub，不生成参数/返回值 property；实现类校验只做 `FindFunctionByName()`；`CallInterfaceMethod()` 运行时也只按 `FunctionName` 转发。结果是 interface `UFunction` 的 `NumParms`、`ParmsSize`、返回值与参数 ABI 全部丢失，只要实现类存在同名函数就会被当成“已实现”。
  - 这项只收口 script interface 在 `ClassGenerator/` 内的 callable contract，不与 `Plan_InterfaceBinding.md` 的 C++ `UInterface` 自动注册或 `FInterfaceProperty` 支持混在一起。首阶段可以继续沿用 generic callback，但它必须消费结构化签名而不是裸 `FunctionName`；interface shell 生成、实现类校验与运行时 dispatch 必须共享同一份 `FASInterfaceMethodSignature`/descriptor。
  - 落地时优先复用已有函数描述解析结果，给 interface method 生成完整 `UFunction` 参数/返回值，补 `NumParms/ParmsSize/ReturnValueOffset` 与参数 property；验证阶段改成“名字 + 参数/返回值签名”匹配；dispatch 阶段至少在命中多个同名实现或签名不一致时 fail-closed，而不是盲目按名转发。
  - 来源：
    - [A] `ClassGenerator_Analysis.md` — “script interface 的 `UFunction` 反射信息与源码声明不一致；当前实现/dispatch 只看函数名”
    - [B] `ClassGenerator_Plan.md` — “Issue-29：script interface 的 UE ABI 被压扁成‘只有函数名’，实现校验和运行时 dispatch 都不看签名”
    - [D] `TypeSystem_ArchReview.md` — “Arch-TS-02 / Arch-TS-32：`UInterface` 方法签名与 callable pipeline 仍是 full-reload 特例 + `FunctionName`-only lane”
    - [E] `GapAnalysis.md` D2 — “接口 callable / property bridge 需要 first-class signature owner，而不是 stub + helper lane”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` L56-L67、L2803-L2830、L5167-L5177 — interface dispatch、UFunction materialize 与实现校验目前都只依赖 `FunctionName`，没有参数/返回值签名匹配
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、可选新增 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASInterfaceMethodSignature.h/.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceSignatureContractTests.cpp`
- [ ] **P4.6** 📦 Git 提交：`[ClassGenerator/Interface] Fix: promote interface callable contract to structured signatures`
- [ ] **P4.6-T** 单元测试：补齐 interface `UFunction` 签名、实现校验与 reflective dispatch 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceSignatureContractTests.cpp`
  - 测试场景：
    - 正常路径：生成出的 interface `UFunction` 具有正确的参数/返回值 property，按 UE 反射调用 interface 方法时参数与返回值能正确往返
    - 边界条件：带 return、`const&`、out/ref 或多参数的 interface 方法仍能保持正确 `NumParms/ParmsSize/ReturnValueOffset`
    - 错误路径：实现类提供同名但签名不一致的方法时编译稳定失败；运行时不会再把不匹配的同名函数当成合法 interface 实现
  - 测试命名：`Angelscript.TestModule.Interface.Signature.<CaseName>`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P4.6-T** 📦 Git 提交：`[ClassGenerator/Interface] Test: cover interface signature materialization and dispatch`

- [ ] **P4.7** 收口 native `UInterface` 解析为确定性的 resolver，禁止 `ResolveInterfaceClass()` 继续按短名扫描首个 `UClass`
  - 当前 script class `implements` native interface 时，`ResolveInterfaceClass()` 在 script interface 和 bind type 都找不到后，会去掉前导 `U`，再用 `TObjectIterator<UClass>` 在线性扫描所有已加载 `UClass` 中取第一个短名命中。只要工程里有两个不同模块/插件暴露同短名 `UInterface`，最终绑定结果就会依赖加载顺序。
  - 这项与 `P1.5` 的 qualified key 同属 identity seam 收口，但它只处理 native `UInterface` 解析的 fail-closed 规则，不展开更大的 C++ `UInterface` 自动绑定主线。首版要求 `ResolveInterfaceClass()` 优先消费 qualified/module-aware ref；短名只有在唯一命中时才允许使用，一旦出现歧义必须 compile error。
  - 落地时应提取 `ResolveNativeInterfaceClass()` 或等价 helper，统一支持 `/Script/Module.Interface`、稳定 alias 与唯一短名三条路径；禁止继续让 `TObjectIterator` 首命中作为 authority。若兼容性要求保留短名写法，也必须在唯一命中时记录 trace，方便后续审计。
  - 来源：
    - [A] `ClassGenerator_Analysis.md` — “`ResolveInterfaceClass()` 对 native `UInterface` 的 fallback 只按短名扫全局类表，绑定结果受加载顺序影响”
    - [A] `ClassGenerator_Analysis.md` — “现有自动化没有覆盖同短名 native interface 冲突”
    - [B] `ClassGenerator_Plan.md` — “Issue-53：`ResolveInterfaceClass()` 对 native `UInterface` 的兜底解析只按短名扫全局类表”
    - [D] `TypeSystem_ArchReview.md` — “Arch-TS-26 / Arch-TS-36：接口 identity 仍依赖 stripped-name heuristic，缺少统一 nominal resolver”
    - [E] `GapAnalysis.md`、`CrossComparison.md` D2 — “steady-state identity 应回到 registry/`UClass*`，而不是短名 scan”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` L5063-L5105 — resolver 先查短名 `GetClassDesc()` / `GetByAngelscriptTypeName()`，失败后去掉前导 `U` 并返回首个匹配的 loaded `UClass`
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、可选新增 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASInterfaceResolver.h/.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptNativeInterfaceResolutionTests.cpp`
- [ ] **P4.7** 📦 Git 提交：`[ClassGenerator/Interface] Fix: make native interface resolution deterministic`
- [ ] **P4.7-T** 单元测试：补齐 native interface 短名冲突与限定名解析回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptNativeInterfaceResolutionTests.cpp`
  - 测试场景：
    - 正常路径：限定名或显式 alias 写法能稳定绑定到目标 native `UInterface`，并按正确接口方法集合完成校验
    - 边界条件：只有一个候选时，短名写法仍可工作且会记录最终解析到的 object path / alias
    - 错误路径：存在两个同短名 native `UInterface` 时，短名写法稳定报歧义错误，不再因加载顺序不同而随机命中
  - 测试命名：`Angelscript.TestModule.Interface.NativeResolution.<CaseName>`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P4.7-T** 📦 Git 提交：`[ClassGenerator/Interface] Test: cover deterministic native interface resolution`

### 单元测试总览补充

| 改进项 | 测试文件 | 测试场景 | 优先级 |
| --- | --- | --- | --- |
| `P1.5` | `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptNamespacedTypeIdentityTests.cpp` | namespaced 同短名 class/interface 隔离、reload 版本链隔离、legacy alias 歧义诊断 | P0 |
| `P2.12` | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadImplementedInterfaceTests.cpp` | interface 集合增删改、删除最后一个 interface、`SoftReloadOnly` barrier | P0 |
| `P4.5` | `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceGraphTests.cpp` | interface-to-interface 继承、transitive graph、非法 secondary interface 诊断 | P1 |
| `P4.6` | `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceSignatureContractTests.cpp` | interface `UFunction` 签名、reflective dispatch、同名异签名拒绝 | P0 |
| `P4.7` | `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptNativeInterfaceResolutionTests.cpp` | native interface 短名冲突、限定名解析、唯一短名 trace | P1 |

### 验收补充

22. namespaced script class / interface 的 `DataRef`、`UObject` 命名、full reload 替换与 interface placeholder 注册，都不再因同短名发生交叉覆盖。
23. `ImplementedInterfaces` 的任何增删改都不会再在 `SoftReloadOnly` 下静默保留旧 `UClass::Interfaces`；要么 full reload，要么明确拒绝 swap-in。
24. script interface 自身声明的 secondary interface 会真实进入 `UClass::Interfaces`，实现类校验按 materialized transitive graph 执行，不再丢边。
25. generated interface `UFunction` 的参数/返回值 ABI 与脚本声明一致；实现校验和运行时 dispatch 不再只靠函数名碰运气。
26. native `UInterface` 的短名歧义在编译期就能稳定暴露；限定名或显式 alias 的解析结果与加载顺序无关。

### 风险补充

19. **qualified key 进入主路径后，历史依赖短名覆盖或同名“巧合可用”的脚本会被显式打爆**
   - 缓解：保留受控 legacy alias 作为迁移桥，只把它用于明确无歧义场景；一旦出现多候选就 fail-closed 并给出迁移指引。
20. **把 interface 集合变化全部升级为 full reload，会扩大一批原本侥幸可跑的 `SoftReloadOnly` 工作流**
   - 缓解：先把 barrier 与 diagnostic 写清楚，再用 targeted tests 锁住真正安全的 body-only case，避免误伤无关改动。
21. **补齐 interface graph materialization 后，仓内现有脚本可能第一次暴露出传递接口缺方法或非法 secondary interface**
   - 缓解：首版优先保证 diagnostic 指到具体类名、接口名与缺失方法；不要在 graph 修复的同一轮再叠加其它 interface feature。
22. **结构化 interface signature 会收紧现有“同名即可通过”的宽松实现契约**
   - 缓解：先把签名比对做成显式 compile error，并补上 `NumParms/ParmsSize/ReturnValueOffset` regression，确保收紧的是 ABI bug 而不是执行时机差异。
23. **deterministic native interface resolver 会让部分短名脚本从“随机成功”变成稳定失败**
   - 缓解：允许唯一短名保留兼容，但多候选必须列出所有 object path / module 候选，帮助脚本快速改成限定名或稳定 alias。 

---

## 深化 (2026-04-09 02:01)

- 前序条目已经覆盖 class-level version chain、namespace key、native interface resolver 与通用 `ScriptType` owner，但五维输入交叉回看后，`ClassGenerator` 里仍有三条没有被单独落项的 contract：engine-level `RegisterObjectType()` interface handle 仍缺 delete/recreate owner；`UASStruct/UDelegateFunction` 的 `_REPLACED_*` head 还没有对称 retirement；reload verdict 依然散落在 `ReloadReq/bNeedReload + ShouldFullReload() + PerformReload()` 三层，后续 rebind/diagnostic 还拿不到单一 authority。

### Phase 1 补充：engine-level type lifetime 与 non-class version retirement

- [ ] **P1.6** 收口 engine-level script interface type handle 的 unregister / tombstone 生命周期
  - 当前 `CreateFullReloadClass()` 在 `ScriptType == nullptr` 的 interface 分支里直接 `RegisterObjectType()` + `GetTypeInfoByName()`，随后把 `InterfaceScriptType->SetUserData(NewClass)` 和 `ClassDesc->ScriptType = InterfaceScriptType` 写死；但删除路径 `CleanupRemovedClass()` 只清 `UASClass/UASStruct` 本体，没有对 engine-level `asITypeInfo` 做 `SetUserData(nullptr)`、revision 或 tombstone。前序 `P1.2/P1.5/P4.7` 已覆盖一般 identity owner、qualified key 与 native resolver，本项只补“engine-level registered interface handle 何时出生/死亡”的缺口，避免重复改写已有条目。
  - 落地时优先引入 `FASInterfaceTypeHandle` 或等价 registry entry，把 `RegisterObjectType()`、`SetUserData()`、delete/recreate、same-name rebind 与 stale lookup diagnostic 放进同一 owner；property/type/debug 三条消费链都改成通过 `ResolveLiveUClassFromScriptType()` 或等价 helper 取 live class，不再直接信任 raw `GetUserData()`.
  - 来源：
    - [B] `ClassGenerator_Plan.md` — “Issue-17：删除 script interface 时未同步清理 engine-level `asITypeInfo->UserData`，后续类型绑定会继续消费 stale `UClass*`”
    - [D] `TypeSystem_ArchReview.md` — “Arch-TS-03：脚本类型身份依赖 `ScriptTypePtr/GetUserData()` 双向裸指针回填，热重载与扩展共享同一条隐式接缝”
    - [E] `GapAnalysis.md` — “[D4] interface type lifetime 仍由 raw `asITypeInfo::UserData` 驱动，没有显式 unregister owner”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` L2596-L2611、L2626、L4990-L5035 — 当前 interface 在 class 生成期会直接 `RegisterObjectType()` 并把 `asITypeInfo->UserData` 指向 `NewClass`，而删除清理只处理 `UASClass/UASStruct` 本体，没有对 engine-level interface type handle 做 unregister 或 `UserData` 失效
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceRegistryLifetimeTests.cpp`
- [ ] **P1.6** 📦 Git 提交：`[ClassGenerator/Interface] Fix: own engine-level interface type lifetime and unregister`
- [ ] **P1.6-T** 单元测试：补齐 interface delete/recreate 的 engine-level handle 生命周期回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceRegistryLifetimeTests.cpp`
  - 测试场景：
    - 正常路径：script interface 首次编译后，consumer 的 property/parameter/cast 都解析到 live `UClass`；remove 后 recreate 同名 interface 时，新 handle 能正确接管
    - 边界条件：`remove -> recreate same name before GC` 不会继续复用旧 `asITypeInfo->UserData`；debug/type lookup 也只能看到当前 revision
    - 错误路径：删除 interface 后重新编译引用它的脚本会稳定失败或得到空解析结果，而不是继续命中 stale `UClass*`
  - 测试命名：`Angelscript.TestModule.Interface.RegistryLifetime.<CaseName>`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P1.6-T** 📦 Git 提交：`[ClassGenerator/Interface] Test: cover engine-level interface handle lifetime`

- [ ] **P1.7** 为 `UASStruct` 与 dynamic delegate 建立 replaced-head retirement，对齐 non-class version chain 生命周期
  - 当前 class path 已有多轮版本链治理，但 struct/delegate 仍是“新 head 创建出来即可”：`CreateFullReloadStruct()`/`CreateFullReloadDelegate()` 只把旧对象 rename 成 `_REPLACED_*` 并创建新的 rooted head；`DoFullReloadStruct()` 仅给旧 struct 接 `NewerVersion`；reload 收尾也只把 replaced struct 的 `ScriptType` 清空。结果是 non-class symbol 的历史壳会永久留在包里，delegate 甚至没有任何对称 retire 步骤。
  - 这项不重复前文 `P1.1/P2.7/P2.10` 的 class version chain、type dependency 和 enum/delegate barrier，而是单列 non-class symbol 的对象生命周期：引入 `RetireReplacedStruct()` / `RetireReplacedDelegate()` 或等价 helper，把 tombstone rename、deferred release、post-GC clean-up 和 latest resolve 统一收口；只有在确实仍需跨一轮 GC 保活时才进入 deferred retire 列表，不能继续依赖永久 rooted `_REPLACED_*`.
  - 来源：
    - [B] `ClassGenerator_Plan.md` — “Issue-52：full reload 对 `UASStruct` 和 dynamic delegate 只创建新 head，不回收旧 `_REPLACED_*` 对象，历史反射壳会永久滞留”
    - [C] `ClassGenerator_TestGaps.md` — “NewTest-1：`UASStruct::GetNewestVersion` 的 full reload 版本链测试缺失；delegate reload 广播与签名变更闭环也缺 targeted automation”
    - [D] `HotReloadArch_ArchReview.md` — “Arch-HR-12：版本链消费仍以 `UClass` 壳为中心，stale handle 的统一 canonicalization 没有覆盖 non-class symbol”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` L2667-L2680、L2720-L2736、L3226-L3230、L2414-L2418、L5026-L5035，`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.h` L14-L30 — 当前 struct/delegate full reload 只 rename 旧 head 并创建新的 rooted object；struct 仅靠 `NewerVersion` 裸链解析最新版本，delegate 没有任何 replaced-head retire owner，cleanup 也只覆盖 removed struct
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.h`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadStructDelegateRetirementTests.cpp`
- [ ] **P1.7** 📦 Git 提交：`[ClassGenerator/HotReload] Fix: retire replaced struct and delegate heads after reload`
- [ ] **P1.7-T** 单元测试：补齐 struct/delegate replaced-head retirement 与 latest resolve 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadStructDelegateRetirementTests.cpp`
  - 测试场景：
    - 正常路径：struct/delegate full reload 后，新 head 可解析到新增字段/参数，旧 head 通过 `GetNewestVersion()` 或等价 resolver 指向最新版本
    - 边界条件：连续多轮 full reload 后，`_REPLACED_*` `UASStruct/UDelegateFunction` 数量保持有界，post-GC deferred retire 能清空历史 rooted head
    - 错误路径：stale old struct/delegate handle 不会在 GC 后继续被 property/delegate binding 当成 live target；异常状态下给出显式 diagnostic 而不是 silent leak
  - 测试命名：`Angelscript.TestModule.HotReload.StructDelegateRetirement.<CaseName>`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P1.7-T** 📦 Git 提交：`[ClassGenerator/HotReload] Test: cover struct and delegate retirement after full reload`

### Phase 5 补充：reload authority 的单一计划层

- [ ] **P5.4** 把 `ReloadReq/bNeedReload`、`ShouldFullReload()` 与多段 `PerformReload()` 循环收口为最小 `FASReloadPlan`
  - 当前 reload authority 仍被拆成三层：`Setup()` 先在 `ClassData/EnumData/DelegateData` 内写 `ReloadReq/bNeedReload`，再在 `ShouldFullReload(FClassData/FEnumData/FDelegateData)` 里按 symbol kind 和会话模式重新解释，最后 `PerformReload()` 通过多段 `for` 循环把 create/link/reload/finalize/init 交错执行。前文已有多个 stopgap 条目各自修 barrier，但只要没有单一 plan，后续 rebind/diagnostic 仍会继续散落在 symbol-specific 条件里。
  - 本项只做 `ClassGenerator` 内部 authority 收口，不重复 `Plan_UnrealCSharpArchitectureAbsorption.md` 的仓库级 dependency graph / manifest 主线。落地时先引入 `FASReloadWorkItem` / `FASReloadPlan`，把 `RequiredAction`、`NeedsFinalize`、`NeedsCDOInit`、`BlockingReason` 和最小 impact summary 固化下来；`PerformReload()` 改成消费这份 plan，`OnPostReload` / finished 与后续 observer 只读取同一份 work-item/impact 结果，不再各自重算。
  - 来源：
    - [B] `ClassGenerator_Plan.md` — “Issue-51：reload 决策被拆散在 symbol-specific flag、`ShouldFullReload()` 重载和多段遍历里，ClassGenerator 缺少统一的执行计划层”
    - [D] `HotReloadArch_ArchReview.md` — “Arch-HR-40：`PostReloadAdditionalChecks` 发生在 `OnPostReload` 之后，finish 边界不可靠；扩展层缺少统一 phase/plan owner”
    - [E] `GapAnalysis.md`、`CrossComparison.md` D4 — “impact 事实仍被拆成 file list/module list/reload map，缺少可供 rebind/test/editor 共用的 manifest / prepare-finish observer”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h` L149-L153，`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` L1872-L1905、L2081-L2133、L2144-L2304 — 当前 symbol-specific `ShouldFullReload()` 重载、`ReloadReq/bNeedReload` 局部 flag 和按 class/enum/delegate 分段的执行循环仍是三个独立 authority，finished phase 也拿不到统一 work-item 真相
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASReloadPlan.h/.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptReloadPlanTests.cpp`
- [ ] **P5.4** 📦 Git 提交：`[ClassGenerator/HotReload] Refactor: introduce unified reload plan and impact summary`
- [ ] **P5.4-T** 单元测试：补齐 mixed-symbol reload plan 与 blocking reason 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptReloadPlanTests.cpp`
  - 测试场景：
    - 正常路径：class/struct/delegate/interface 混合变更时，planner 生成稳定的 `RequiredAction` 与阶段顺序，consumer 只按 plan 执行一次
    - 边界条件：`SoftReloadOnly` 遇到 `FullReloadRequired`/`Abort` work item 时，会输出明确 `BlockingReason`，且 plan 不会把 symbol 静默降级成 `LinkOnly`
    - 错误路径：provider dependency、implemented-interface delta 或 enum/delegate 结构变化等高风险场景，不会再在执行阶段绕开 planner 进入错误循环
  - 测试命名：`Angelscript.TestModule.ClassGenerator.ReloadPlan.<CaseName>`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P5.4-T** 📦 Git 提交：`[ClassGenerator/HotReload] Test: cover unified reload plan and blocking reasons`

### 单元测试总览补充

| 改进项 | 测试文件 | 测试场景 | 优先级 |
| --- | --- | --- | --- |
| `P1.6` | `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceRegistryLifetimeTests.cpp` | interface delete/recreate、engine-level `UserData` 清理、stale consumer fail-closed | P1 |
| `P1.7` | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadStructDelegateRetirementTests.cpp` | struct/delegate latest resolve、replaced-head retire、post-GC 清理 | P1 |
| `P5.4` | `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptReloadPlanTests.cpp` | mixed-symbol reload plan、blocking reason、plan-driven phase order | P2 |

### 验收补充

27. engine-level script interface type 在 delete/recreate、same-name reuse 与 debug/type lookup 场景下都有显式 unregister/rebind owner；property/type/debug 三条消费链不再共享 stale `UserData`.
28. struct 与 dynamic delegate 的 `_REPLACED_*` 历史壳不再在 repeated full reload 后永久 rooted；latest resolve 与 post-GC retirement 对 non-class symbol 同样成立。
29. `ClassGenerator` 对一次 compile/reload 的 authoritative 决策被收口成单一 `FASReloadPlan`；analysis、execution、finished/rebind consumer 看到的是同一份 work-item 与 blocking reason。

### 风险补充

24. **给 engine-level interface type 引入 unregister / tombstone 后，历史依赖 stale `GetUserData()` 侥幸命中的脚本或工具会从“偶尔可用”变成稳定失败**
   - 缓解：先提供统一 resolver 和清晰 diagnostic，保留一次性 migration log；只允许 live handle 继续进入 property/type/debug 消费链。
25. **replaced struct/delegate 的 deferred retire 时机如果拿捏过早，会打断仍持有旧签名壳的反射消费者**
   - 缓解：把 retire 绑定到 post-GC 或显式无 live consumer 的 commit 点，并在测试里同时覆盖 reload 后与 GC 后两个时间窗。
26. **`FASReloadPlan` 迁移期最容易出现“旧分支还在跑、新 planner 也在给 verdict”的双轨漂移**
   - 缓解：先让 planner 以 shadow mode 产出快照并做测试比对，再逐步切换执行主路径；旧 `ShouldFullReload()` 只允许从 planner 派生，不能继续独立演化。

---

## 深化 (2026-04-09 06:37)

- 现有输出已经覆盖 version chain、identity/schema owner、class metadata half-commit、config defaults、component layout 和 interface 主线，但五维输入与源码复核后，`SoftReload` 的“状态迁移本体”仍有两条没有独立落项的 contract：其一，preconstruct restore 仍把 nested instanced/persistent object graph 近似成顶层 `UObject*`；其二，`PropertiesToCopy` 仍只接受 `Name + exact Type`，schema 演进只能 silent drop，既没有 alias/adapter，也没有结构化 drop reason。

### Phase 2 补充：soft reload 状态迁移 contract

- [ ] **P2.13** 用真实 instancing 语义替换 `bIsInstanced = IsObjectPointer()` 近似，给 nested instanced/persistent object graph 建 preconstruct restore 或 fail-closed barrier
  - 当前类生成阶段已经能把 `CPF_InstancedReference`、`CPF_ContainsInstancedReference`、`CPF_PersistentInstance` 与 `STRUCT_HasInstancedReference` 写进 property/struct，但 `SoftReload` 真正决定“constructor 前先恢复谁”的只有 `FPropertyCopy::bIsInstanced` 这一位，并且它完全来自 `PropertyType.IsObjectPointer()`。这导致 `TArray<UObject*>`、`TMap` / `TSet`、以及携带 persistent instance 子字段的 `UStruct` 在 reload 时会错过 preconstruct restore，只能等 ctor replay 后再整体覆写，object graph contract 仍不完整。
  - 这项不重复 `P2.1` 的 broad soft-reload gate，也不回到 `P5.1` 的 method-patch 主线；它只收 `SoftReload` 里“必须在 `ReinitializeScriptObject()` 之前先恢复的状态”这一层。首版应把 `bIsInstanced` 升级成 `EPreconstructRestoreKind` 或等价分类，至少区分 `DirectObjectRef`、`ContainsInstancedReference`、`PersistentInstanceContainer`；无法准确判定的场景直接升级为 `FullReloadSuggested/Required`，不要继续赌近似软迁移。
  - 实现上优先让 `PropertiesToCopy` 在重链后直接读取 `FProperty` / `UStruct` 的 instancing 语义，再由统一 helper 判断是“direct object pointer”、“contains instanced graph”还是“仅普通值拷贝”；实例与 `CDO` 两条 preconstruct restore 路径都消费同一份分类结果，避免 live instance 与 `CDO` 再各自分叉。
  - 来源：
    - [D] `Documents/AutoPlans/ArchitectureReview/HotReloadArch_ArchReview.md` — “Arch-HR-19：`SoftReload` 只用 `IsObjectPointer()` 决定 preconstruct restore，nested instanced/persistent object graph 会在 ctor replay 前后出现丢状态与重复 subobject 窗口”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — “跨插件对比显示 current AS 的 state owner 明确落在 `UObject + CDO` 原位搬运上；既然当前路线依赖 per-object migration，就必须把 graph-aware restore 做成正式 contract，而不是继续用顶层指针近似”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` L4379-L4392、L4481-L4488、L4607-L4624、L4712-L4725 — 当前 `FLocalPropertyContext` 只把 container 作为单一 property 参与迁移，`FPropertyCopy` 仍仅用 `PropertyType.IsObjectPointer()` 标记 `bIsInstanced`，实例与 `CDO` 也都只对这一个布尔分支做 constructor 前回填
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、可选新增 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASSoftReloadStateRestore.h/.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadInstancedReferenceTests.cpp`
- [ ] **P2.13** 📦 Git 提交：`[ClassGenerator/HotReload] Fix: classify nested instanced state before soft-reload reconstruction`
- [ ] **P2.13-T** 单元测试：补齐 nested instanced/persistent object graph 的 preconstruct restore 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadInstancedReferenceTests.cpp`
  - 测试场景：
    - 正常路径：body-only `SoftReloadOnly` 后，直接 `UObject*`、`TArray<UObject*>`、`TMap<FName, UObject*>` 与带 persistent instance 子字段的 `UStruct` 都保持 object identity 与关键 wiring 不变
    - 边界条件：ctor 会访问这些 nested instanced graph 的类，在 reload 后不会多创建临时 subobject，也不会把旧 graph 在 ctor 后整体覆写成重复节点
    - 错误路径：无法安全判定 preconstruct restore 的容器/struct 形状会明确升级为 full reload 或给出结构化 diagnostic，而不是静默继续走近似 soft path
  - 测试命名：`Angelscript.TestModule.HotReload.InstancedGraph.<CaseName>`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P2.13-T** 📦 Git 提交：`[ClassGenerator/HotReload] Test: cover nested instanced graph restore during soft reload`

- [ ] **P2.14** 把 `PropertiesToCopy` 从 `Name + exact Type` exact-match 升级成显式 schema migration plan，并为 drop/fallback 输出结构化诊断
  - 当前 `SoftReload` 的字段迁移 contract 仍然是“只拷 script-local、可 copy/construct/destruct，且新旧字段 `Name` 与 `Type` 完全相等”。这会把 rename、safe widening、字段迁入 `CodeSuperClass`、以及 noncopyable script type 一律当成 delete + add；live instance 与 `CDO` 在 reload 后只能 silent drop 旧值，没有 alias、adapter 或 rejection reason 可以追踪。
  - 这项不是去做“任意 schema 自动迁移”，而是把当前 silent drop 升级成显式协议。首版优先引入 `FHotReloadFieldMatch` / `FSchemaMigrationRule` 一类最小 owner：exact-match 仍走零成本快路径；只有显式声明 `HotReloadAlias`（或等价 metadata）、可证明安全的 widening/enum-same-underlying-type adapter，才允许跨名或跨类型迁移；其余场景必须给出可测试的 `DroppedBecauseRename` / `DroppedBecauseTypeMismatch` / `DroppedBecauseMovedToCodeSuper` 诊断，而不是继续闷掉。
  - 落地时不要把迁移逻辑散落进 instance 与 `CDO` 两套回填循环；应在构建 `PropertiesToCopy` 之前先形成统一 `SchemaMigrationPlan`，再由 live instance / `CDO` / session summary 共用同一份 match result 和 drop reason。这样后续即便再引入脚本级 `__HotReloadMigrate` hook 或更复杂 adapter，也有单一扩展点可承载。
  - 来源：
    - [D] `Documents/AutoPlans/ArchitectureReview/HotReloadArch_ArchReview.md` — “Arch-HR-6：字段迁移只认 script-local `Name + Type`，schema 演进时会静默掉值，且没有结构化报告”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — “跨插件对比显示 current AS 的 state contract 走 `UObject + CDO` 原位迁移；既然 correctness 压在 per-object migration 上，就不能继续让字段演进以 silent drop 方式失效”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` L4466-L4470、L4479-L4500、L4523-L4530、L4572-L4600、L4679-L4707 — 当前 `PropertiesToCopy` 仅处理 `CodeSuperClass` 之后的 script-local 字段，旧字段必须同时满足 `CanCopy()/CanConstruct()/CanDestruct()`，新字段也只在 `OldProperties.Find(LocalProp.Name)` 且 `Copy->Type == PropertyType` 时才进入迁移表，后续 instance/CDO 回填则完全依赖这份 exact-match 列表
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、可选新增 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASSchemaMigrationPlan.h/.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadSchemaMigrationTests.cpp`
- [ ] **P2.14** 📦 Git 提交：`[ClassGenerator/HotReload] Fix: add explicit schema-migration plan for soft reload`
- [ ] **P2.14-T** 单元测试：补齐字段 rename/type 演进的迁移与 drop-reason 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadSchemaMigrationTests.cpp`
  - 测试场景：
    - 正常路径：exact-match 字段仍按当前语义保值；带显式 alias 的 rename 与安全 widening（如 `int32 -> int64`）会在 `SoftReloadOnly` 下保留旧值
    - 边界条件：字段迁入 `CodeSuperClass`、enum 同底层类型迁移或可 copy script struct 的 adapter 命中时，session summary 会记录使用了哪条迁移规则
    - 错误路径：无 alias 的 rename、narrowing / 不兼容 type change、noncopyable script type 会稳定输出 drop reason 或升级 full reload，不再 silent drop
  - 测试命名：`Angelscript.TestModule.HotReload.SchemaMigration.<CaseName>`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P2.14-T** 📦 Git 提交：`[ClassGenerator/HotReload] Test: cover schema migration aliases adapters and drop reasons`

### 单元测试总览补充

| 改进项 | 测试文件 | 测试场景 | 优先级 |
| --- | --- | --- | --- |
| `P2.13` | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadInstancedReferenceTests.cpp` | nested instanced graph、persistent instance container、unsafe-shape fallback | P0 |
| `P2.14` | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadSchemaMigrationTests.cpp` | alias rename、safe widening、drop reason / full-reload barrier | P1 |

### 验收补充

30. `SoftReloadOnly` 不再只对顶层 `UObject*` 做 preconstruct restore；nested instanced/persistent object graph 要么被正确前置恢复，要么被明确升级到 full reload。
31. 字段 rename、safe widening 与其它 schema 演进场景不再 silent drop；要么按显式 migration rule 成功迁移，要么在编译/diagnostic 中给出稳定原因。
32. live instance、`CDO` 与 session summary 消费同一份 state-restore / schema-migration plan，不再各自内嵌不同的 fallback 规则。

### 风险补充

27. **将 nested instanced/persistent graph 提前到 constructor 前回填，最容易把某些依赖 ctor 初始化的对象图提前覆盖**
   - 缓解：首版只对白名单语义开放 graph-aware preconstruct restore；无法确定的容器/struct 一律 fail-closed 到 full reload。
28. **schema migration plan 一旦允许 alias/adapter，最容易被误用成“自动兜底一切字段演进”**
   - 缓解：首版只开放显式 alias 和少数可证明安全的 widening/underlying-type adapter；所有其它形状必须输出 drop reason，而不是继续扩大隐式迁移面。

---

## 深化 (2026-04-09 06:45)

- 现有 `P1.2/P2.1` 已经覆盖 schema owner 与 `soft reload` 语义主线，但这轮复核后仍有两条没被单独拆开的 correctness contract：其一，script-only 引用的 GC reachability 还没有发布到 runtime 真正消费的单一路径；其二，`CDONoDefaults` 仍是 `RF_ArchetypeObject` 临时对象，不具备真实 `CDO` 语义，即使修掉 defaults 抑制和 config 混入，也仍会在错误基线上比较。
- 对 `Documents/Plans/` 做定向检索后，当前活跃 Plan 中未见 `ReferenceSchema`、`RuntimeAddReferencedObjects`、`CDONoDefaults` 或 `RF_ClassDefaultObject` 同主题执行项，因此以下补充不与现有活跃 Plan 重叠。

### Phase 1 补充：script-only 引用 reachability 的发布与回退合同

- [ ] **P1.8** 把 script-only 引用的 GC reachability 发布到 runtime 实际消费面，并给 `RuntimeAddReferencedObjects()` 补 fail-closed fallback
  - 现有 `P1.2` 已经要求收口 schema owner，但源码复核显示还差最后一层“发布到哪里、失败时谁兜底”的 contract：`UASClass` 仍自己重新声明了一份 `ReferenceSchema`，`DetectAngelscriptReferences()` 也继续基于 `UASClass*` 静态类型写这份字段；与此同时 `RuntimeAddReferencedObjects()` 还是空实现，意味着一旦 schema 发布链断掉，script-only 引用没有第二条保活路径。
  - 这项补充不重做更大的 `ReferenceBridge` 设计，而是先把 correctness 收口成最小可执行面：去掉或停用 `UASClass` 的 shadow `ReferenceSchema`，统一通过 helper 写入 runtime 实际消费的那一份 schema；每次 compile/reload 都必须从空 plan 重建，删除最后一个引用字段时也要显式发布空 schema，而不是继续 append 旧成员或靠成员数相等跳过。
  - 同时要给 `RuntimeAddReferencedObjects()` 一个 fail-closed fallback：当 class 处于 reload 过渡态、schema 尚未发布或 future reference plan 需要旁路时，仍能按同一份 compiled reference plan 遍历 script-only 引用并喂给 `FReferenceCollector`，避免“schema 没更新 + fallback 也为空”直接把对象交给 GC 提前回收。
  - 来源：
    - [A] `Documents/AutoPlans/ClassGenerator_Analysis.md` — “发现 74：`UASClass` 重新声明了 `ReferenceSchema`，类生成器写入的是子类字段，但 GC 读取的是 `UClass` 基类字段；`RuntimeAddReferencedObjects()` 仍是空实现”
    - [A] `Documents/AutoPlans/ClassGenerator_Analysis.md` — “发现 8：删除最后一个带引用的 script 字段后，旧 `ReferenceSchema` 不会被清空”
    - [C] `Documents/AutoPlans/TestCoverage/ClassGenerator_TestGaps.md` — “NewTest-24：当前没有任何用例验证 `RuntimeAddReferencedObjects()` / `ReferenceSchema` 能保活 script-only object reference”
    - [D] `Documents/AutoPlans/ArchitectureReview/TypeSystem_ArchReview.md` — “Arch-TS-18：reference-bearing family 仍同时依赖 hidden property 与 `ReferenceSchema` 双轨物化，owner 没有分层”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — “[D8] 当前 Angelscript 的对象存活 authority 明确落在 `UASClass.ReferenceSchema` 并入 UE GC schema；既然 reachability owner 在这里，就不能允许发布链是 shadow field + 空 fallback”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h` L34、L77，`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` L4875-L4924 — 当前 `UASClass` 仍声明 shadow `ReferenceSchema`，`RuntimeAddReferencedObjects()` 仍为空，`DetectAngelscriptReferences()` 仍先 append 旧 schema 再按成员数决定是否覆盖写回
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、可选新增 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASReferencePlan.h/.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASClassReferenceSchemaTests.cpp`
- [ ] **P1.8** 📦 Git 提交：`[ClassGenerator/GC] Fix: publish script-only reference reachability through one schema path`
- [ ] **P1.8-T** 单元测试：补齐 script-only 引用的 GC 保活、删字段清空与 fallback 合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASClassReferenceSchemaTests.cpp`
  - 测试场景：
    - 正常路径：非 `UPROPERTY` 的 script-only `UObject` 引用在 holder 仍存活时经过 `CollectGarbage()` 仍被保活，且再次读取返回同一对象
    - 边界条件：repeated soft/full reload 后删除最后一个引用字段，会发布空 schema；下一次 GC 不再沿旧 offset 保活对象，二次清理后弱引用失效
    - 错误路径：故意让类处于 schema 尚未发布或 reload 过渡态时，`RuntimeAddReferencedObjects()` fallback 仍能保住引用；不会出现“schema 和 fallback 同时失效”导致的提前回收
  - 测试命名：`Angelscript.TestModule.ClassGenerator.ReferenceSchema.<CaseName>`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P1.8-T** 📦 Git 提交：`[ClassGenerator/GC] Test: cover script-only reference schema publication and fallback`

### Phase 2 补充：`CDONoDefaults` 基线对象的真实 CDO 语义

- [ ] **P2.15** 把 `CDONoDefaults` 从 pseudo-archetype 基线替换成真实 CDO 语义快照或显式 defaults delta plan
  - 现有 `P2.1` 已经收口了 `soft reload` 何时不该继续走旧类复用，但源码复核显示剩下的“继续允许 soft reload 时拿什么做基线”仍然不对：`PrepareSoftReload()` 现在创建的是 `RF_ArchetypeObject` 临时对象，随后 `PropertiesToCopy` 再拿 `BaseCDO` 对比这份 pseudo-object 计算 `bModifiedByDefaults`。这不是线程/全局状态问题，而是 baseline 对象从 UE 语义上就不是 `CDO`。
  - 这项补充要求把“脚本 defaults 改了什么”从 `RF_ArchetypeObject` 假 CDO 上拆出来。首版优先引入 `FSoftReloadDefaultDelta` / `FCDOBaselineSnapshot` 一类 helper，直接在 compile transaction 内记录“真实 `CDO` 相对 super/native/config 基线的 script defaults 差异”；只有这份 delta 可以驱动 `bModifiedByDefaults`、instance copy 和 `CDO` reinstance。若仍需要临时对象，也必须由专用 helper 构造具备 `RF_ClassDefaultObject` 语义的隔离基线，不能继续拿 `RF_ArchetypeObject` 冒充。
  - 同时要把 `Config` / native defaults / script defaults 三种来源分开：当前比较逻辑把 `BaseCDO != CDONoDefaults` 直接折成“被 defaults 修改”，这会把引擎初始化差异和脚本默认语句混在一起。新 baseline plan 必须输出结构化来源，例如 `ModifiedByScriptDefaults`、`ModifiedByConfig`、`ModifiedByNativeCDOInit`，并让 live instance 与 `CDO` 两条迁移路径消费同一份 delta，而不是各自继续内嵌推断。
  - 来源：
    - [A] `Documents/AutoPlans/ClassGenerator_Analysis.md` — “发现 69：`CDONoDefaults` 只按 `RF_ArchetypeObject` 构造，不是实际 `RF_ClassDefaultObject`，soft reload 比较基线先天偏离真实 CDO 语义”
    - [C] `Documents/AutoPlans/TestCoverage/ClassGenerator_TestGaps.md` — “NewTest-4：现有 suite 没有验证 soft reload 后 `CDO`、旧实例、新实例在默认值与行为上保持一致”
    - [D] `Documents/AutoPlans/ArchitectureReview/HotReloadArch_ArchReview.md` — “当前状态迁移的核心仍是 `CDONoDefaults` 差异法和对象属性复制，baseline 本身就是 runtime state bridge 的核心 owner”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — “当前 Angelscript 的 state contract 锚定在 `UObject + CDO` 原位重实例化；既然路线不打算退回 VM cache，就必须保证 CDO baseline 自己是 UE 语义正确的”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` L4093-L4108、L4490-L4500、L4580-L4581、L4681-L4685 — 当前 `PrepareSoftReload()` 仍用 `NewObject(..., RF_ArchetypeObject)` 构造 `CDONoDefaults`，后续 `PropertiesToCopy` 与 instance/CDO 回填仍直接把 `BaseCDO` 对这份 pseudo-baseline 的差异解释成 `bModifiedByDefaults`
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、可选新增 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASSoftReloadBaseline.h/.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadClassSemanticsTests.cpp`
- [ ] **P2.15** 📦 Git 提交：`[ClassGenerator/HotReload] Fix: replace pseudo-CDO baseline with real default-delta plan`
- [ ] **P2.15-T** 单元测试：补齐真实 CDO baseline 与 defaults 来源拆分回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadClassSemanticsTests.cpp`
  - 测试场景：
    - 正常路径：body-only `SoftReloadOnly` 后，真实 `CDO`、旧实例、新实例对 script defaults 的观察保持一致，不会因为 baseline 误差多拷或漏拷值
    - 边界条件：native defaults、`Config` 值和 script defaults 同时存在时，delta plan 会把三者区分开；仅 script defaults 影响 `bModifiedByDefaults`，`Config` 不再被伪装成脚本默认语句
    - 错误路径：当前 `RF_ArchetypeObject` pseudo-baseline 覆盖不到的路径会被新测试锁成 fail；后续实现若再次回退到“非 CDO 对象做比较基线”，测试应直接报警
  - 测试命名：`Angelscript.TestModule.HotReload.CDOBaseline.<CaseName>`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P2.15-T** 📦 Git 提交：`[ClassGenerator/HotReload] Test: cover real CDO baseline and default-delta ownership`

### 单元测试总览补充

| 改进项 | 测试文件 | 测试场景 | 优先级 |
| --- | --- | --- | --- |
| `P1.8` | `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASClassReferenceSchemaTests.cpp` | script-only object keepalive、删字段清空 schema、reload 过渡态 fallback | P0 |
| `P2.15` | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadClassSemanticsTests.cpp` | 真实 CDO baseline、script/config/native defaults 来源拆分、instance/CDO 一致性 | P0 |

### 验收补充

33. script-only 引用的 GC reachability 只有一条发布真相：要么通过 runtime 实际消费的 `ReferenceSchema`，要么通过同一份 reference plan 驱动的 `RuntimeAddReferencedObjects()` fallback；删除最后一个引用字段后不会继续沿旧 offset 扫描。
34. `SoftReload` 不再依赖 `RF_ArchetypeObject` pseudo-CDO 做 defaults 比较基线；真实 `CDO`、旧实例、新实例对 script defaults 的观察保持一致，`Config` / native defaults / script defaults 的来源不会再互相串写。

### 风险补充

29. **去掉 `UASClass` 的 shadow `ReferenceSchema` 并引入 fallback 后，最容易出现“schema 已发布但 fallback 仍按旧 plan 再保活一次”的双轨重复**
   - 缓解：要求 schema publisher 与 fallback 共用同一份 immutable reference plan，并在测试里同时覆盖“schema 生效”和“仅 fallback 生效”两种窗口，禁止双路并行各自演化。
30. **把 `CDONoDefaults` 改成真实 CDO 语义快照后，最容易踩到 UE 对 CDO 创建/序列化的隐含前提**
   - 缓解：首版优先走显式 `default-delta snapshot`，只有在确实需要临时对象时才隔离构造具备 `RF_ClassDefaultObject` 语义的 baseline helper；不接受继续把 `RF_ArchetypeObject` 作为兼容兜底。

---

## 深化 (2026-04-09 06:53)

- 现有 `P3.2/P3.4` 已经覆盖“构造上下文 thread-local 化”和“late swap-in rollback barrier”，但这轮复核后仍有两条 correctness contract 没被单独落项：其一，`DefaultConstructorOuter` / `OverrideConstructingObject` 仍是游离在主 construction owner 之外的隐式状态；其二，subsystem full reload 的停用/激活仍晚于 `OnPostReload`，成功路径与失败路径都缺少稳定交接窗口。
- 对 `Documents/Plans/` 做定向检索后，当前活跃 Plan 中未见 `DefaultConstructorOuter`、`OverrideConstructingObject`、subsystem hot reload state bridge 或 “`OnPostReload` 之前完成 subsystem reinstance” 同主题执行项，因此以下补充不与现有活跃 Plan 重叠。

### Phase 3 补充：construction request owner 与 subsystem reload 窗口

- [ ] **P3.5** 把 `DefaultConstructorOuter` / `OverrideConstructingObject` 收口成与构造栈同源的 `construction request` owner
  - 这项不重复 `P3.2` 已记录的 `CurrentObjectInitializers` 脏栈清理，而是补它尚未覆盖的另一半 ambient state：`FScopeSetDefaultConstructorOuter` 和 `OverrideConstructingObject` 目前各自持有一份独立静态状态，既没有纳入统一 RAII owner，也没有真实 producer contract。结果是 `AllocScriptObject()` 的 outer 语义和 `GetConstructingASObject()` 的 override 语义都还停留在“API 看起来存在、调用链却不受控”的状态。
  - 落地时应把这两条状态并入与 `GetConstructingASObject()` / `AllocScriptObject()` 同一份 `thread_local` request stack：`FScopeSetDefaultConstructorOuter` 退化成兼容 wrapper，同时新增等价的 scoped override helper；`GetConstructingASObject()`、`GetDefaultConstructorOuter()`、`AllocScriptObject()` 只读取同一份 request，而不是继续分别读 `OverrideConstructingObject` 裸静态和 `GASDefaultConstructorOuter`。首版仍可保留 `TransientPackage()` fallback，但必须把“为什么走 fallback”变成显式 contract，并让 request unwind 与 ctor/defaults 失败路径共用同一清理出口。
  - 来源：
    - [A] `Documents/AutoPlans/ClassGenerator_Analysis.md` — “发现 25：脚本 `new` 路径声明了构造 outer 作用域，但仓库内没有任何调用点，所有 `AllocScriptObject` 分配都会退回 `TransientPackage`”
    - [C] `Documents/AutoPlans/TestCoverage/ClassGenerator_TestGaps.md` — “`NewTest-19` 指出 `FScopeSetDefaultConstructorOuter/GetDefaultConstructorOuter()` 仍是 0 命中；`NewTest-31` 指出 `OverrideConstructingObject` 优先级与恢复语义没有 targeted regression”
    - [D] `Documents/AutoPlans/ArchitectureReview/HotReloadArch_ArchReview.md` — “构造相关的 ambient state 应并入 `FScopedASConstructionContext` 一类 thread-local/RAII owner，而不是继续散落在独立静态变量上”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h` L59-L63、L104-L114，`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` L990-L993、L1011-L1025、L1047-L1060 — 当前 `FScopeSetDefaultConstructorOuter` 只定义了 wrapper，`GetDefaultConstructorOuter()` 仍在无 request 时固定回 `GetTransientPackage()`，`AllocScriptObject()` 直接消费这条 fallback；`GetConstructingASObject()` 则先读 process-global `OverrideConstructingObject`。对 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/` 的检索还表明 `FScopeSetDefaultConstructorOuter` 只有声明/定义处命中，没有实际 producer。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASClassConstructionContextTests.cpp`
- [ ] **P3.5** 📦 Git 提交：`[ClassGenerator] Fix: unify constructor outer and override into one scoped construction request`
- [ ] **P3.5-T** 单元测试：补齐 `DefaultConstructorOuter`、override 优先级与 request unwind 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASClassConstructionContextTests.cpp`
  - 测试场景：
    - 正常路径：无 scope 时 `GetDefaultConstructorOuter()` 返回 `GetTransientPackage()`；进入单层与双层 `FScopeSetDefaultConstructorOuter` 后能稳定返回 `OuterA/OuterB`，并在脚本对象分配时使用当前 request outer
    - 边界条件：`OverrideConstructingObject` 生效时，构造期 probe 总是优先读到 override object；内层 scope 退出后恢复到上一层 request，最终完全恢复到默认状态
    - 错误路径：ctor/defaults 失败或嵌套构造中断后，outer/override request 不会泄漏到后续对象；下一次 `AllocScriptObject()` 会回到显式 fallback，而不是继续沿用脏 outer 或脏 override
  - 测试命名：`Angelscript.TestModule.ClassGenerator.ConstructionRequest.<CaseName>`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P3.5-T** 📦 Git 提交：`[ClassGenerator] Test: cover constructor outer override precedence and request unwind`

- [ ] **P3.6** 把 subsystem reload 的 deactivate/activate 收口到 verify/finished 边界，并补 opt-in state bridge
  - 这项不重复 `P3.4` 的通用 rollback barrier，而是单列 subsystem 专有的成功/失败窗口：当前 `CreateFullReloadClass()` 一旦识别出 `UDynamicSubsystem` / `UWorldSubsystem` 派生类，就会立刻停用旧 subsystem 并记录待激活新类；但 full reload 尾声又在 `OnPostReload(true)` 之后才统一激活新 subsystem。这样成功路径上，observer 在 `OnPostReload` 看不到新 subsystem；失败路径上，只要 `VerifyClass()` 把模块标成 `bModuleSwapInError`，旧 subsystem 已经被停用而新 subsystem 仍可能继续激活。
  - 首版要把 subsystem reinstance 收口成 `Verified -> Activate -> Finished` 的单一顺序：只有通过最终 verify 的类才能进入 `ReinstancedSubsystems` 提交；full reload 收尾改为先完成 subsystem replacement，再发通用 finished/`OnPostReload`。同时给 subsystem 提供最小 opt-in state bridge，例如 `Capture/ApplyHotReloadState` 或等价钩子，让 engine/game instance/world/local player 四类长生命周期单例都能在同一窗口里交接状态。若暂时不做完整 bridge，也至少要保证未通过 verify 的新 subsystem 不会替换旧实例，且 `OnPostReload(true)` 观察到的一定是已经完成 replacement 的稳定状态。
  - 来源：
    - [A] `Documents/AutoPlans/ClassGenerator_Analysis.md` — “发现 43：dynamic subsystem 会在类验证失败后继续完成‘旧实例停用 + 新实例激活’，把坏类真正接入全局子系统集合”
    - [C] `Documents/AutoPlans/TestCoverage/ClassGenerator_TestGaps.md` — “`NewTest-5` / `NewTest-13` 指出 `OnClassReload`、`OnFullReload`、`OnPostReload` 的 targeted automation 仍缺失，当前没有用例锁住 reload 收尾顺序”
    - [D] `Documents/AutoPlans/ArchitectureReview/HotReloadArch_ArchReview.md` — “Arch-HR-17：subsystem full reload 的状态续接与事件时序缺少显式迁移窗口，`OnPostReload(true)` 早于新 subsystem 激活”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` L2302-L2312、L2372-L2395、L2442-L2463、L2642-L2647、L5597-L5676 — 当前 verify 发生在 `CallPostInitFunctions()/InitDefaultObjects()` 之后，`OnFullReload/OnPostReload` 又先于 GC 和 `ActivateExternalSubsystem()` 触发；与此同时，subsystem 替换在 `CreateFullReloadClass()` 阶段已经执行 `DeactivateExternalSubsystem(ReplacedClass)` 并把 `NewClass` 放进 `ReinstancedSubsystems`，而 verify 失败只会设置 `bModuleSwapInError`
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/BaseClasses/ScriptEngineSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/BaseClasses/ScriptGameInstanceSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/BaseClasses/ScriptWorldSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/BaseClasses/ScriptLocalPlayerSubsystem.h`、新增 `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptSubsystemHotReloadTests.cpp`
- [ ] **P3.6** 📦 Git 提交：`[ClassGenerator/HotReload] Fix: commit subsystem reinstance before finished reload observers`
- [ ] **P3.6-T** 单元测试：补齐 subsystem reload 时序、失败保护与状态交接回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptSubsystemHotReloadTests.cpp`
  - 测试场景：
    - 正常路径：world/game instance/engine/local player script subsystem full reload 后，`OnPostReload(true)` 触发时已经能拿到新 subsystem，且 opt-in state bridge 能把计数器或持有引用迁移到新实例
    - 边界条件：未实现 bridge 的 subsystem 仍按显式 deinit/init 语义重新初始化，但不会重复激活两次；旧实例只停用一次，新实例只激活一次
    - 错误路径：带非法 `DefaultComponent` / attach / verify 失败的 subsystem 脚本不会停用旧 subsystem 并激活坏的新实例；`OnPostReload(true)` / finished 不会把这轮 reload 误报成成功收尾
  - 测试命名：`Angelscript.TestModule.HotReload.Subsystem.<CaseName>`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P3.6-T** 📦 Git 提交：`[ClassGenerator/HotReload] Test: cover subsystem reinstance timing failure guard and state bridge`

### 单元测试总览补充

| 改进项 | 测试文件 | 测试场景 | 优先级 |
| --- | --- | --- | --- |
| `P3.5` | `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASClassConstructionContextTests.cpp` | `DefaultConstructorOuter` 嵌套恢复、override 优先级、request unwind | P1 |
| `P3.6` | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptSubsystemHotReloadTests.cpp` | subsystem replace 时序、failed-verify guard、opt-in state bridge | P0 |

### 验收补充

35. `DefaultConstructorOuter`、`OverrideConstructingObject` 与构造栈共享同一份 `thread_local` owner；嵌套 scope、override 优先级和失败回滚都不会再依赖游离的 process-global 状态。
36. full reload 的 subsystem replacement 在 finished/`OnPostReload(true)` 之前已经完成；observer 看到的是稳定的新 subsystem，verify 失败的脚本 subsystem 不会把坏实例接入全局集合。

### 风险补充

31. **把 `DefaultConstructorOuter` 和 override 并入统一 request owner 后，最容易碰到历史路径默认依赖 `TransientPackage` fallback 的行为变化**
   - 缓解：首版保持显式 fallback 存在，但把 fallback reason 记录为可测试 contract；只有在 request 确实存在时才改变 outer/override 语义，不对无 request 路径做静默行为扩张。
32. **把 subsystem 激活前移并引入 state bridge 后，最容易把不同 owner 维度的 subsystem 状态串到错误的 engine/world/local player 上**
   - 缓解：首版只允许 opt-in bridge，且按 `Engine/GameInstance/World/LocalPlayer` 维度分别建 key；测试中分别覆盖成功迁移、无 bridge 重初始化和 verify-fail 保旧实例三条路径，禁止用单一全局 map 混装所有 subsystem state。

---

## 深化 (2026-04-09 07:04)

- 检索 `Documents/Plans/` 后，当前活跃 Plan 中未见 `ClassKindPolicy`、`FinalizeInterfaceClass` 或 `NonInstantiableInterfacePolicy` 同主题执行项。本轮只补 `ClassGenerator` 内的 host-kind owner，不重复现有 `P4.5-P4.7` 的 interface graph / ABI / resolver 条目。

### Phase 4 深化：interface shell 的 non-instantiable lane

- [ ] **P4.8** 把 script interface shell 从 `FinalizeObjectClass()` / `StaticObjectConstructor` lane 中剥离，建立 `FinalizeInterfaceClass()` 与 `NonInstantiableInterfacePolicy`
  - 当前 `UASClass` 默认携带 `ConstructFunction`、`DefaultsFunction` 和三套静态构造入口；`FinalizeClass()` 仍把 `bIsInterface` 分支直接送进 `FinalizeObjectClass()`，而类创建阶段又无条件写入 `ScriptPropertyOffset` / `ScriptTypePtr` 并调用 `UpdateConstructAndDefaultsFunctions()`。结果是 interface shell 虽然标了 `CLASS_Interface`，内部仍共享 object-instancing metadata 与 `StaticObjectConstructor` 合同。
  - 这项的目标不是重做 `P4.5/P4.6` 的 interface graph / signature，而是给它们清出正确 owner：新增 `FinalizeInterfaceClass()` 或等价 `NonInstantiableInterfacePolicy`，只负责 `Bind()`、`StaticLink()`、方法壳、source metadata 与 registration 收尾；只有 instantiable host policy 才允许安装 `ClassConstructor`、`ConstructFunction`、`DefaultsFunction` 与 script storage build。
  - 实现上优先把 `UpdateConstructAndDefaultsFunctions()` 拆成 `UpdateCallableMetadata()` / `UpdateInstancingMetadata()` 两层，再把 `FinalizeClass()` 的 `Actor/Component/Object/interface` 分发提升成可注册 `ClassKindPolicy`。首阶段保留 `UASClass` 现有字段作为兼容 mirror，但 interface lane 停止写入 instancing metadata，并加 `ensure` / diagnostic 防止后续代码再从 interface shell 读取 `ConstructFunction` 或走 `StaticObjectConstructor`。
  - 执行顺序上放在 `P4.5/P4.6` 之后或与其同轮收口最稳妥；否则 interface graph / signature 刚修好，又会继续挂在旧 object shell contract 上。
  - 来源：
    - [C] `Documents/AutoPlans/TestCoverage/ClassGenerator_TestGaps.md` — `NewTest-35`、`NewTest-32`、`NewTest-38` 表明当前没有把 `FinalizeObjectClass()` / `StaticObjectConstructor()` 与 function-only / object / actor host-kind contract 分开锁住，refactor 后极易回归
    - [D] `Documents/AutoPlans/ArchitectureReview/TypeSystem_ArchReview.md` — `Arch-TS-25` / `Arch-TS-28` 明确指出 finalization 仍写死 `Actor/Component/Object` 三 lane，interface shell 仍被 object-instancing metadata 绑住
    - [E] `Documents/AutoPlans/ReferenceComparison/GapAnalysis.md`、`Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — interface 分支仍在 `FinalizeObjectClass()` 提前返回，参考实现则把 dynamic interface family 或 constructor seam 单独分层，而不是让 interface 继续继承 object lane
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h` L18-L30、L104-L110，`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` L1352-L1494，`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` L3359-L3364、L3678-L3686、L5189-L5207、L5461-L5464、L5889-L5908 — 当前 `UASClass` 仍默认携带 instancing字段和三套静态构造器；interface 只多了 `CLASS_Interface` flag，却仍写 `ScriptPropertyOffset/ScriptTypePtr`、调用 `UpdateConstructAndDefaultsFunctions()`，最终被 `FinalizeObjectClass()` 安装到 `StaticObjectConstructor` lane
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、新增 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClassKindPolicy.h/.cpp` 或等价 helper、`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptClassKindPolicyTests.cpp`
- [ ] **P4.8** 📦 Git 提交：`[ClassGenerator/Interface] Refactor: split interface shell from object-instancing class policy`
- [ ] **P4.8-T** 单元测试：补齐 host-kind policy 与 interface non-instantiable lane 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptClassKindPolicyTests.cpp`
  - 测试场景：
    - 正常路径：脚本 `Actor`、脚本 `Component`、普通脚本 `Object` 仍分别走各自 constructor lane，ctor/defaults 执行次数与现有 contract 保持不变；function-only object class 仍能编译并执行生成函数
    - 边界条件：script interface shell 编译与 hot reload 后仍能查询方法列表、source metadata 与 `ImplementsInterface()` 结果，但 `ClassConstructor` 不再指向 `UASClass::StaticObjectConstructor`，`ConstructFunction` / `DefaultsFunction` 保持空或仅存在兼容 mirror
    - 错误路径：任何 interface shell 都不会再尝试分配 `asCScriptObject`、执行 ctor/defaults 或依赖 `ScriptPropertyOffset` 参与对象构造；如果旧路径被误触发，测试要稳定暴露 explicit diagnostic / failed assertion
  - 测试命名：`Angelscript.TestModule.ClassGenerator.ClassKindPolicy.<CaseName>`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P4.8-T** 📦 Git 提交：`[ClassGenerator] Test: cover class-kind policy and non-instantiable interface shell`

### 单元测试总览补充

| 改进项 | 测试文件 | 测试场景 | 优先级 |
| --- | --- | --- | --- |
| `P4.8` | `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptClassKindPolicyTests.cpp` | actor/component/object constructor lane 稳定、function-only class、interface shell 非实例化 | P1 |

### 验收补充

37. script interface shell 不再共享 `StaticObjectConstructor`、`ConstructFunction`、`DefaultsFunction` 这条 object-instancing 合同；interface graph、signature 与 source metadata 可以在 metadata-only lane 上独立演进。
38. `FinalizeClass()` 的 host-kind owner 由显式 policy 决定，`Actor` / `Component` / `Object` 现有构造语义保持不变，interface shell 也不会再被错误卷入 ctor/defaults/reload instancing path。

### 风险补充

33. **把 interface 从 object lane 拆出来后，最容易漏掉历史 callsite 对 `ConstructFunction` / `DefaultsFunction` 非空的隐式假设**
   - 缓解：首阶段保留兼容 mirror，并在 interface lane 上加 `ensure` / diagnostic；只有确认调用面全部转去 policy/helper 后再删除旧读取路径。
34. **引入 `ClassKindPolicy` 后，actor/component/object 现有构造顺序最容易因为 owner 搬迁而发生无意行为变化**
   - 缓解：先做“owner 搬迁但行为不变”的薄重构，并用 `NewTest-32`、`NewTest-35`、`NewTest-38` 对应的 targeted regression 锁住 object/function-only/actor 三条 lane，再让 interface lane 单独切换。

---

## 深化 (2026-04-09 07:13)

- 现有 `P1.1/P1.3/P1.4` 已经覆盖 version-chain owner、removed class quarantine 与 old-type 延迟退役，但这轮复核后仍缺一条更底层的内存安全 contract：`NewerVersion` 还是 GC 不可见的裸链，`replace -> remove` 后历史节点没有统一回写，`GetMostUpToDateClass()` 仍会线性追裸指针。
- 现有 `P2.4/P2.5` 已覆盖 dispatch shape 和 net-validate/live-state，但还缺一个“同一 `UASFunction` 三条调用面必须读同一份 dispatch cache”的 owner：`ProcessEvent`/`NativeThunk`、`OptimizedCall_*` 与 JIT specialized `RuntimeCallFunction()` 仍可能在 soft reload 后看到不同 epoch 的入口。
- 检索 `Documents/Plans/` 后，当前活跃 Plan 中未见 `NewerVersion` GC-safe 历史链或 `OptimizedCall`/`ProcessEvent`/JIT cache 一致性同主题执行项；`Plan_UFunctionReflectiveFallbackBinding.md` 聚焦 unresolved/native fallback，不覆盖脚本生成 `UASFunction` 的 reload cache coherence，因此以下补充不与现有活跃 Plan 重叠。
- 当前仓库未找到 `Documents/AutoPlans/DiscoveryPlans/ClassGenerator_Plan.md`；本轮新增项按 [A]/[C]/[D]/[E] 交叉取证，并以 `ClassGenerator` 实码复核为准。

### Phase 1 深化：version chain 的 GC-safe 历史链

- [ ] **P1.9** 把 `NewerVersion` 裸链升级成 GC-safe version link，并在 `replace -> remove` 时回写历史节点
  - 这项不重复 `P1.1` 的 canonical owner，也不重复 `P1.3` 的 removed-class quarantine；它补的是更底层的内存安全 contract：`UASClass::GetMostUpToDateClass()` 仍无条件沿 `NewerVersion` 裸指针链追尾，而 `CleanupRemovedClass()` 只清当前 head，不会回写任何仍指向它的历史节点。结果是类一旦经历 `replace -> remove`，旧节点就会继续把“最新版本”解析到已经 remove 的 head，GC 后还可能退化成悬挂链。
  - 落地时优先把 `NewerVersion` 从“对象外部的原生裸链”改成受统一 owner 管理的 version link：可以是 `FASVersionLink` + weak/tombstone head，也可以是 GC 可见的 handle registry，但必须同时满足三件事。第一，`GetMostUpToDateClass()` 与 `CreateDefaultComponents()/ApplyOverrideComponents()` 只通过同一 helper resolve live head，不再直接手搓裸链遍历。第二，`CleanupRemovedClass()` 在 remove 当前 head 时要显式回写历史节点，把它们导向新的 tombstone/live head，而不是继续保留旧地址。第三，链路上任何节点若进入 retired/tombstone 状态，都要能返回稳定 diagnostic，而不是把 UAF 风险留给调用点。
  - 首版不需要一次性改完所有外部 binding call-site，但至少要让 `ClassGenerator` 自己的 component/layout 消费面先停掉裸链，并把历史节点 unlink/retire 做成统一 helper；这样后续若把 `Bind_TSubclassOf`、`Bind_UClass`、`NewObject()` 等外部 consumer 接进统一 canonicalization，也不会再建立在悬挂链之上。
  - 来源：
    - [A] `Documents/AutoPlans/ClassGenerator_Analysis.md` — “发现 48/51：`replace -> remove` 后旧节点仍会把 `GetMostUpToDateClass()` 导向已移除 head，`NewerVersion` 完全游离于 GC 图之外”
    - [C] `Documents/AutoPlans/TestCoverage/ClassGenerator_TestGaps.md` — “现有 full reload 用例会自动折叠 `GetMostUpToDateClass()`，缺少保留历史节点直接观察版本链与 CDO 切换的 targeted regression”
    - [D] `Documents/AutoPlans/ArchitectureReview/HotReloadArch_ArchReview.md` — “Arch-HR-12：版本链消费仍是局部追新 + 大量透传/过滤，stale class handle 风险长期存在”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — “当前仓库把 hot reload owner 建在 `UASClass` 与 `CLASS_NewerVersionExists` 上，既然 canonical class 仍靠这条链，就不能允许它继续是不可审计的裸指针关系”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h` L19、L77，`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` L912-L923、L1184-L1222，`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` L3695-L3699、L4990-L5024 — 当前 `NewerVersion` 仍是普通成员且 `RuntimeAddReferencedObjects()` 为空；`GetMostUpToDateClass()` 直接线性追裸链，component class 解析靠局部手动 `GetMostUpToDateClass()`，而 remove 路径只清当前 head，不会回写历史节点
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、可选新增 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASVersionLink.h/.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadVersionChainHandleTests.cpp`
- [ ] **P1.9** 📦 Git 提交：`[ClassGenerator/HotReload] Fix: make class version-chain links GC-safe and retire historical heads`
- [ ] **P1.9-T** 单元测试：补齐历史节点、`replace -> remove` 与 GC 后 latest resolve 的回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadVersionChainHandleTests.cpp`
  - 测试场景：
    - 正常路径：`V1 -> V2` full reload 后，保留 `OldClass` 直接调用 latest resolve，稳定得到 `NewClass`；默认组件 class 解析与 canonical lookup 都走同一 live head
    - 边界条件：`V1 -> V2 -> remove` 后，`V1` 历史节点不会继续把 latest resolve 指到 removed head；GC 前后 repeated resolve 都稳定返回 tombstone/live 结果，不会因为 head 已回收而漂移
    - 错误路径：故意保留 stale 历史节点并触发 remove + GC 后，系统给出显式 tombstone/retired diagnostic，而不是继续沿悬挂 `NewerVersion` 访问已失效对象
  - 测试命名：`Angelscript.TestModule.HotReload.VersionChainHandle.<CaseName>`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P1.9-T** 📦 Git 提交：`[ClassGenerator/HotReload] Test: cover GC-safe version-chain handles and remove-after-replace`

### Phase 2 深化：`UASFunction` 三条调用面的统一 dispatch cache

- [ ] **P2.16** 把 `ProcessEvent` / `OptimizedCall_*` / JIT specialized thunk 收口成同一份 `FunctionDispatchCache`
  - 这项不重复 `P2.4` 的 dispatch-shape 判定，也不重复 `P2.5` 的 live-state/`WithValidate`；它补的是“shape 判定之后，所有运行时调用面到底读哪一份 cache”这个 owner。当前 full reload 会在函数创建期一次性写入 `FUNC_Native + UASFunctionNativeThunk` 与 `JitFunction*` 缓存，但 soft reload 只替换 `ScriptFunction`。结果是 `ProcessEvent -> UASFunctionNativeThunk -> RuntimeCallFunction()`、`OptimizedCall_*` 直调包装器，以及各个 JIT specialized `RuntimeCallFunction()` 可能在同一轮 reload 后读到不同 epoch 的入口。
  - 落地时应把这些分散字段收口成 `RefreshFunctionDispatchCache()` 或等价 `FASFunctionDispatchCache`：至少统一托管 `ScriptFunction` epoch、`JitFunction/JitFunction_Raw/JitFunction_ParmsEntry`、native-thunk readiness、optimized-call eligibility 与必要的 fallback lane。full reload 创建、soft reload 复用和 cleanup/invalidate 都只能通过这一个入口刷新或清空 cache，禁止继续把 `ScriptFunction`、`JitFunction_*` 和 `OptimizedCall_*` 各自当成独立 owner。
  - 调用面也要统一消费这份 cache。`UASFunctionNativeThunk` 进来的 `RuntimeCallFunction()`、`OptimizedCall_*` 包装器，以及 JIT specialized 子类都必须先读同一份 dispatch cache；若 cache epoch 与当前 `ScriptFunction`/shape 不一致，就只能回退到共享 generic lane、显式拒绝，或升级为 full reload，不能继续把“旧 JIT 入口 + 新脚本函数”拼成混合调用。
  - 这项与 `Plan_UFunctionReflectiveFallbackBinding.md` 的边界也要保持清楚：那份活跃 Plan 聚焦 unresolved/native callable coverage 与 reflective fallback；本项只处理脚本生成 `UASFunction` 自己的 runtime cache coherence，不引入新的 fallback backend。
  - 来源：
    - [A] `Documents/AutoPlans/ClassGenerator_Analysis.md` — “soft reload 只替换 `ScriptFunction`，不会刷新 `JitFunction*` 缓存；`ProcessEvent`/RPC 调用面和函数壳状态可能分裂”
    - [C] `Documents/AutoPlans/TestCoverage/ClassGenerator_TestGaps.md` — “`NewTest-33/34` 指出 `UASFunctionNativeThunk` / `RuntimeCallFunction()` 的 `ProcessEvent` 桥接与 `OptimizedCall_*` 直调包装器目前都是零覆盖”
    - [D] `Documents/AutoPlans/ArchitectureReview/TypeSystem_ArchReview.md` — “函数 ABI 目前靠 `UASFunction` 子类矩阵与多套 `RuntimeCallFunction()`/optimized thunk 共同维护，新增或切换调用形状时改动面过大”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — “当前仓库已经把 override/call owner 前移到 `UASFunction + UASFunctionNativeThunk`，既然 canonical callable lane 在这里，reload 后也必须由这里统一刷新 cache”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` L3416-L3429、L4253-L4259，`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` L1561-L1738、L1940-L1944、L2952-L2980 — 当前函数创建期会一次性缓存 `JitFunction*` 并安装 `UASFunctionNativeThunk`，但 soft reload 只替换 `ScriptFunction`；`OptimizedCall_*` 与 JIT specialized `RuntimeCallFunction()` 继续分别读取对象上的 `JitFunction_Raw` 或特化入口，没有统一的 reload-time cache refresh owner
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、可选新增 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASFunctionDispatchCache.h/.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASFunctionDispatchSurfaceTests.cpp`
- [ ] **P2.16** 📦 Git 提交：`[ClassGenerator/HotReload] Fix: unify UASFunction dispatch cache across native thunk JIT and optimized calls`
- [ ] **P2.16-T** 单元测试：补齐 `ProcessEvent`、`OptimizedCall_*` 与 JIT specialized path 的一致性回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASFunctionDispatchSurfaceTests.cpp`
  - 测试场景：
    - 正常路径：同一个脚本函数在 `ProcessEvent`、`OptimizedCall_*` 与普通 runtime path 三条入口上得到一致返回值/副作用；body-only soft reload 后三条路径同时观察到新逻辑
    - 边界条件：`final + JIT`、非 JIT fallback、ref-arg/byte-return 等代表性 dispatch shape 都能在 soft reload 后稳定刷新 cache，不会出现一条路径走新逻辑、另一条路径还命中旧 JIT 入口
    - 错误路径：故意制造 cache 失效或 shape 变化时，调用会显式回退/拒绝或升级为 full reload，不再继续混用“旧 cache + 新 `ScriptFunction`”
  - 测试命名：`Angelscript.TestModule.ClassGenerator.FunctionDispatchSurface.<CaseName>`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P2.16-T** 📦 Git 提交：`[ClassGenerator] Test: cover native-thunk JIT and optimized-call dispatch coherence`

### 单元测试总览补充

| 改进项 | 测试文件 | 测试场景 | 优先级 |
| --- | --- | --- | --- |
| `P1.9` | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadVersionChainHandleTests.cpp` | 历史节点 latest resolve、`replace -> remove`、GC 后 tombstone/retired 诊断 | P0 |
| `P2.16` | `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASFunctionDispatchSurfaceTests.cpp` | `ProcessEvent`、`OptimizedCall_*`、JIT specialized path 在 reload 前后的一致性 | P1 |

### 验收补充

39. `NewerVersion` 不再是 GC 不可见的裸悬挂链；`replace -> remove` 后历史节点要么解析到新的 live head，要么解析到稳定的 tombstone/retired 结果，绝不会在 GC 后继续追到已失效对象。
40. 同一个 `UASFunction` 在 `ProcessEvent`、`UASFunctionNativeThunk`、`OptimizedCall_*` 与 JIT specialized runtime path 上观察到的 dispatch target 必须一致；soft reload 后不会再出现“旧 JIT 入口 + 新 `ScriptFunction`”的混合 epoch 调用。

### 风险补充

35. **把 `NewerVersion` 裸链改成 GC-safe/tombstone owner 后，最容易影响现有调试工具对 replaced class 历史壳的可见性**
   - 缓解：首版保留 debug-only inspect 开关与结构化 tombstone 诊断，允许在测试或调试模式下显式观察历史节点，但默认业务路径只能看到 live/tombstone 结果。
36. **把 `UASFunction` 三条调用面统一到同一份 dispatch cache 后，最容易把热路径优化误伤成“每次调用都做重验证”**
   - 缓解：cache refresh 只发生在 create/reload/invalidate 边界，运行时调用继续读 immutable fast cache；若 shape 变化无法安全复用，则在 reload 时直接回退/拒绝，而不是把昂贵校验塞进每次调用。

---

## 深化 (2026-04-09 07:21)

- 现有 `P1.2/P1.8/P4.1/P4.2` 已分别覆盖 script type identity、`ReferenceSchema`、source metadata 与 `IsDeveloperOnly()`，但从执行顺序上仍缺一个更小的前置隔离层：在这些条目逐步落地之前，`ClassGenerator` 代码还在直接读写 `UASClass` 自己声明的 `ScriptTypePtr/bIsScriptClass/ReferenceSchema`，会继续把阴影字段扩散到更多调用点。
- 检索 `Documents/Plans/` 后，当前活跃 Plan 中只在 `Plan_UhtPlugin.md` 提到 “使用外部 `TMap<UClass*, FScriptClassData>` 映射”，没有针对 `ClassGenerator` runtime 阶段的阴影字段访问隔离项；本轮补的是 `ClassGenerator` 内的 stopgap owner，不重复长线 UHT/tooling 方案。

### Phase 1 深化：先封住 `UASClass` 阴影字段访问面

- [ ] **P1.10** 在 `P1.2` 的完整 registry 改造前，先把 `ScriptTypePtr/bIsScriptClass/ReferenceSchema` 的直接读写收口到 base-aware accessor
  - 现有 `P1.2` 已明确要把 script type identity 与 schema owner 收口成单一 registry，但当前 `ASClass.cpp`、`AngelscriptClassGenerator.cpp` 仍在多个关键路径上直接读写 `UASClass::ScriptTypePtr`、`UASClass::bIsScriptClass` 与 `UASClass::ReferenceSchema`。只要这些 raw 访问面还在，后续任何 registry、schema publish、source metadata 或 `IsDeveloperOnly()` 修复都会继续承担双写不一致风险。
  - 这项不重复 `P1.2` 的最终形态，而是补一个更小、更先落地的 fail-closed 过渡层：新增 `FAngelscriptClassFieldAccess` / `FAngelscriptClassRuntimeFieldView` 或等价 helper，把 `Get/SetScriptType`、`Get/SetScriptClassFlag`、`Get/SetReferenceSchemaOwner` 统一成基于 `UClass*` 的访问入口；`RuntimeDestroyObject()`、`AllocScriptObject()`、`GetSourceFilePath()`、`GetRelativeSourceFilePath()`、`IsDeveloperOnly()`、`DetectAngelscriptReferences()`、`CreateFullReloadClass()`、`DoSoftReload()`、`CleanupRemovedClass()` 全部先切到这条 seam。
  - 首版不要求一步移除 `UASClass` 里的阴影字段，但必须把 ClassGenerator 子树中的 raw 访问点收敛完，并在 helper 内加 debug-only mirror check / diagnostic，防止后续又回到“某些调用点读基类、某些调用点写派生类”的状态。这样 `P1.2/P1.8/P4.1/P4.2` 后续只需要替换 helper 内部 owner，而不是再逐文件追 raw field。
  - 来源：
    - [A] `Documents/AutoPlans/ClassGenerator_Analysis.md` — “发现 74/75：`ReferenceSchema`、`ScriptTypePtr` 与 `bIsScriptClass` 在 `UClass/UASClass` 之间发生字段分裂，engine-side consumer 会读到另一份状态”
    - [C] `Documents/AutoPlans/TestCoverage/ClassGenerator_TestGaps.md` — “`NewTest-21`、`NewTest-24`、`NewTest-17` 分别表明 `RuntimeDestroyObject()`、`ReferenceSchema` 与 class/function source metadata 都缺少 targeted automation，当前没有测试锁住 base-vs-derived 视角的一致性”
    - [D] `Documents/AutoPlans/ArchitectureReview/TypeSystem_ArchReview.md` — “Arch-TS-03：registry 与现有裸指针缓存并存时最容易双写不一致，必须先把写入口收敛，再谈移除旧成员”
    - [E] `Documents/AutoPlans/ReferenceComparison/GapAnalysis.md` — “weak/typed registry 优于 raw user-data cache；在真正 registry 落地前，至少先把 live pointer owner 收到单一 resolver”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h` L29-L34，`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` L965-L976、L1037-L1048、L1497-L1532，`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` L3291-L3301、L3678-L3680、L4200-L4202、L4304-L4308、L4875-L4924、L4995-L5016 — 当前 `UASClass` 仍声明阴影字段，析构/构造/source/developer-only 路径都直接读 `ScriptTypePtr`，生成/soft reload/cleanup/schema publish 也都继续按 `UASClass*` 静态类型写这些字段
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、可选新增 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClassFieldAccess.h/.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASClassFieldAccessTests.cpp`
- [ ] **P1.10** 📦 Git 提交：`[ClassGenerator/Core] Refactor: isolate shadow class-field access behind base-aware accessors`
- [ ] **P1.10-T** 单元测试：补齐 `UClass/UASClass` 视角下一致的 identity/schema/source contract
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASClassFieldAccessTests.cpp`
  - 测试场景：
    - 正常路径：初次编译脚本类后，通过 `UClass*` 与 `UASClass*` 两个视角读取 script type、developer-only/source metadata 与 schema publish 结果，helper 返回一致；`RuntimeDestroyObject()` 与 source metadata API 仍按 live type 正常工作
    - 边界条件：soft reload、full reload 与 blueprint child 同步后，base-aware accessor 继续给出同一份 live identity；script-only 引用 schema 与 `GetSourceFilePath()/GetRelativeSourceFilePath()` 不会因为视角不同再次分叉
    - 错误路径：`DiscardModule()`、removed class cleanup 或 full reload retire 后，helper 会把 type/schema/source 状态一起 fail-closed；不存在 `UASClass` 视角看似仍 live、`UClass` 视角已失效，或反过来的半失效状态
  - 测试命名：`Angelscript.TestModule.ClassGenerator.ClassFieldAccess.<CaseName>`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P1.10-T** 📦 Git 提交：`[ClassGenerator] Test: cover base-aware class field access and mirror consistency`

### 单元测试总览补充

| 改进项 | 测试文件 | 测试场景 | 优先级 |
| --- | --- | --- | --- |
| `P1.10` | `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASClassFieldAccessTests.cpp` | `UClass/UASClass` 视角一致性、reload 后 identity/schema/source 同步、cleanup fail-closed | P0 |

### 验收补充

41. `ClassGenerator` 子树内不再直接把 `ScriptTypePtr/bIsScriptClass/ReferenceSchema` 当作 `UASClass` 局部字段任意读写；identity/schema/source/developer-only 相关路径统一经由同一条 base-aware accessor seam，为 `P1.2/P1.8/P4.1/P4.2` 提供稳定前置层。

### 风险补充

37. **若只迁移了一部分 raw 访问点，helper 与遗留字段会形成“看起来已收口、实际上仍双写”的假象**
   - 缓解：首版要求 `ASClass.cpp` 与 `AngelscriptClassGenerator.cpp` 的 ClassGenerator 范围内 raw 访问点一并迁移，并在 helper 中加入 debug-only mirror check；测试同时覆盖 compile、soft reload、full reload 与 discard 四个阶段，避免只在 happy path 下看起来一致。

---

## 深化 (2026-04-09 07:27)

- 现有 `P1.7/P2.7` 已覆盖 struct/delegate 的版本链与依赖闭环，但 `UASStruct` 侧仍缺一条单独的 capability owner：`CreateCppStructOps()` 首次创建 `FASStructOps` 时写死 fake-vtable capability，后续 `UpdateScriptType()` 只刷新函数指针和 `STRUCT_IdenticalNative`，还没有把 hash/identical/lifecycle 能力统一收口成一份可重放的 contract。
- 检索 `Documents/Plans/` 后，当前活跃 Plan 只在 `Plan_UhtPlugin.md` 用一句话说明 `FASStructOps + FakeVTable` 已提供等价能力，没有覆盖 runtime reload/remove 后 capability 清理、`UpdateScriptType()` 的 stale-state owner 或 fake-vtable ABI 收口；因此以下补充不与现有活跃 Plan 重叠。

### Phase 4 深化：收口 `UASStruct` capability set 与 fake-vtable ABI

- [ ] **P4.9** 把 `UASStruct` 的 `CppStructOps` 能力刷新收口成单一 `StructCapabilitySet`，修正 reload/remove 后的 stale `hash/identical` 能力位
  - 当前 `CreateCppStructOps()` 只在首次 `PrepareCppStructOps()` 时创建一次 `FASStructOps`，constructor 同时写入 `FakeVTable.Capabilities.HasIdentical`、`HasGetTypeHash` 和 `ComputedPropertyFlags`；但后续无论是 struct full reload 还是 cleanup，执行层都只是改 `Struct->ScriptType` 后调用 `UpdateScriptType()`，而该函数目前只会 `Ops->SetFromStruct(this)` 并切换 `STRUCT_IdenticalNative`。结果是 script struct 在删除 `opEquals()/Hash()`、切换 `ScriptType` 甚至进入 removed state 后，UE 层看到的 fake-vtable capability、`CPF_HasGetValueTypeHash` 与 `StructFlags` 可能已经分叉。
  - 这项不重复 `P1.7` 的 version chain，也不重复 `P2.7` 的 dependency closure；它补的是 struct value semantics 的 owner：新增 `FAngelscriptStructCapabilitySet` 和 `ApplyStructCapabilities(UASStruct&, FASStructOps&)`，统一从 `ScriptType` 解析 `Construct`、`Destruct`、`Copy`、`Identical`、`Hash`、`ToString` 能力，再一次性写回 `FakeVTable.Capabilities`、`ComputedPropertyFlags`、`StructFlags` 与可查询 accessor。`PrepareCppStructOps()` 只负责确保 compat adapter 存在，`UpdateScriptType()` 与 cleanup/full reload 路径不再各自手改半套状态。
  - 首版不需要改变现有 `FASFakeVTable` 的 ABI 选择，但必须把 fake-vtable 写入集中到 `ASStructOpsCompat` 或等价 helper，避免未来再把 capability 探测、VM 回调函数指针和 UE 私有 ABI 混在 `ASStruct.cpp` 单文件里继续膨胀。与此同时，reload 后删除 `opEquals()/Hash()` 必须真正关掉 `HasIdentical/HasGetTypeHash` 与 `CPF_HasGetValueTypeHash`，而不是只把运行时回调退化成“继续暴露旧 capability，但执行时返回 false/0”。
  - 来源：
    - [C] `Documents/AutoPlans/TestCoverage/ClassGenerator_TestGaps.md` — “`NewTest-2/NewTest-36/NewTest-41` 同时指出 `CppStructOps` 能力暴露、reload 后删除 `opEquals/Hash` 的回收路径，以及 `CreateCppStructOps()` 构造/复制/析构生命周期都没有 targeted regression”
    - [D] `Documents/AutoPlans/ArchitectureReview/TypeSystem_ArchReview.md` — “`FASStructOps` 目前同时承担 capability 探测、fake-vtable 写入与 `UpdateScriptType()` flag 回写；应引入 `FAngelscriptStructCapabilitySet`/compat adapter，把 `Construct/Copy/Identical/Hash` 从同一个 capability plan 派生”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.cpp` L67-L72、L216-L236，`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` L3215-L3216、L4071-L4072、L5028-L5029 — 当前 `FASStructOps` 只在构造时写一次 `FakeVTable.Capabilities.HasGetTypeHash/ComputedPropertyFlags`，`PrepareCppStructOps()` 只在 `CppStructOps == nullptr` 时创建 ops，而 full reload/update/cleanup 路径都只改 `ScriptType` 后调用 `UpdateScriptType()`；`UpdateScriptType()` 现状仅刷新函数指针并切换 `STRUCT_IdenticalNative`
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、可选新增 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStructOpsCompat.h/.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASStructCapabilityTests.cpp`
- [ ] **P4.9** 📦 Git 提交：`[ClassGenerator/Struct] Fix: unify script-struct capability refresh and fake-vtable flags`
- [ ] **P4.9-T** 单元测试：补齐 `CppStructOps` 能力暴露、删除后回收与生命周期回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASStructCapabilityTests.cpp`
  - 测试场景：
    - 正常路径：带 `opEquals()`、`Hash()`、`ToString()` 与默认 ctor 的 script struct 在首次编译后暴露 `GetCppStructOps()`、`STRUCT_IdenticalNative`、哈希能力位与 `GetToStringFunction()`；`InitializeStruct/CopyScriptStruct/DestroyStruct` 分别命中一次 construct/copy/destruct contract
    - 边界条件：对同名 struct 做 full reload，v2 删除 `opEquals()/Hash()` 但保留 `ToString()`；新 head 经 `GetNewestVersion()` 可达，`STRUCT_IdenticalNative`、哈希能力位与 compat capability 会同步清掉，而 `GetToStringFunction()` 继续可用
    - 错误路径：removed struct 或 `ScriptType == nullptr` 清理后，不再继续向 UE 宣称存在 `Identical/Hash` 能力；任何比较/哈希入口都 fail-closed，不会保留旧 capability 或沿旧 fake-vtable 状态继续工作
  - 测试命名：`Angelscript.TestModule.ClassGenerator.ASStructCapability.<CaseName>`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P4.9-T** 📦 Git 提交：`[ClassGenerator/Struct] Test: cover script-struct capability refresh cleanup and lifecycle`

### 单元测试总览补充

| 改进项 | 测试文件 | 测试场景 | 优先级 |
| --- | --- | --- | --- |
| `P4.9` | `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASStructCapabilityTests.cpp` | `CppStructOps` 能力暴露、reload 后删除 `opEquals/Hash` 的能力回收、`InitializeStruct/CopyScriptStruct/DestroyStruct` 生命周期 | P1 |

### 验收补充

42. `UASStruct` 的 `Construct/Destruct/Copy/Identical/Hash/ToString` 能力由同一份 `StructCapabilitySet` 驱动；full reload、remove 与 `ScriptType == nullptr` 清理后，不再出现 fake-vtable capability、`CPF_HasGetValueTypeHash` 与 `STRUCT_IdenticalNative` 彼此分叉的状态。

### 风险补充

38. **把 `FASStructOps` 的 capability 写入收口成 compat adapter 后，最容易碰到“运行时行为看似没变，但 UE 私有 ABI 标志位漏同步”的隐蔽回归**
   - 缓解：首版要求用同一组测试同时锁住 `GetCppStructOps()` 生命周期、`STRUCT_IdenticalNative`、哈希能力位与 reload/remove 后的 capability 清理；迁移期保留对旧 `FASFakeVTable` 写法的逐项对照，直到 compat adapter 成为唯一私有 ABI 触点。

---

## 深化 (2026-04-09 07:33)

- 现有 `P2.2` 已覆盖 component layout builder，但它还建立在一个未收口的前置假设上：`DefaultComponent/OverrideComponent` 的宿主类解析可以直接复用 `FAngelscriptTypeUsage::GetClass()`。对 raw object 这条老路径这成立，但对 `TObjectPtr` / `TWeakObjectPtr` / `TSubclassOf` 这类 wrapper family，`ClassGenerator` 仍看不见 nominal class。
- 现有 `P1.8/P4.3` 已分别覆盖 `ReferenceSchema` 与 `DebugValues` 的发布/生命周期，但它们仍共用一条没有单一 owner 的成员解析链：`PropertyIndexMap + PropertyTypes` 先建一遍，`FAngelscriptPropertyDesc` 再回填一遍，`ReferenceSchema` / `DebugValues` 又各自重扫一遍。只要后续继续扩 wrapper/interface family，这三条消费链就会继续各自漂移。
- 检索 `Documents/Plans/` 后，当前活跃 Plan 未见 `ResolveNominalClass` / `ResolvedMemberDesc` 同主题执行项；`Plan_UnrealCSharpArchitectureAbsorption.md` 只覆盖长线 property family 吸收，不覆盖 `ClassGenerator` 当前 correctness seam，因此以下补充不与现有活跃 Plan 重叠。

### Phase 2 深化：补齐 component nominal-class owner

- [ ] **P2.17** 把 `DefaultComponent/OverrideComponent` 的 component class 解析从 `GetClass()` 误用里剥离，建立 `ResolveNominalComponentClass()` seam
  - 这项不重复 `P2.2` 的 layout builder；它补的是 layout builder 之前的“这个属性最终代表哪个 component class” owner。当前分析阶段用 `ResolveCodeSuperForProperty(PropertyType)` 判 `DefaultComponent` 是否派生自 `UActorComponent`，finalize 阶段又直接把 `Property->PropertyType.GetClass()` 写进 `Comp.ComponentClass`。这两条路径都把 `GetClass()` 当成 nominal-class resolver，但 [D] 已明确它的语义只是 direct reference class；一旦属性类型是 `TObjectPtr<USceneComponent>`、`TWeakObjectPtr<USceneComponent>`、`TSubclassOf<USceneComponent>` 或后续 interface/wrapper family，分析期和 materialization 期就会继续出现“有的分支能看见 nominal class，有的分支只能得到 `nullptr`”的灰色状态。
  - 首阶段优先在 `ClassGenerator/` 内新增 `ResolveNominalComponentClass(const FAngelscriptTypeUsage&, UClass* ExpectedBaseClass)` 或等价 helper，让 `Analyze()` 里的 `DefaultComponent` 校验、`FinalizeActorClass()` 的 `DefaultComponent/OverrideComponent` 物化，以及后续 `Attach/RootComponent` / `EditorOnly` 校验全部吃同一份 nominal-class 解析结果；保留 direct object/script object 的 `GetClass()` fallback，但禁止再让 component 元数据路径直接读 `PropertyType.GetClass()`。
  - 若当前 runtime 还不能稳定支持某些 wrapper 作为 `DefaultComponent`/`OverrideComponent` 声明，首版必须 fail-closed：在 analysis 阶段给出明确 compile error，而不是先把 `Comp.ComponentClass = nullptr` 写进 layout，再等 `FinalizeActorClass()` / runtime actor construction 走到一半才暴露错误。
  - 来源：
    - [A] `Documents/AutoPlans/ClassGenerator_Analysis.md` — “发现 2/5：`DefaultComponent` 已经是 class-generator correctness hotspot；组件元数据一旦漂移，soft reload 与后续 layout 缓存会继续放大错误”
    - [C] `Documents/AutoPlans/TestCoverage/ClassGenerator_TestGaps.md` — “NewTest-27/28：当前没有任何用例直接检查 `DefaultComponents/OverrideComponents` 的编译期元数据，也没有 `SoftReloadOnly` 下的 repeated-reload 稳定性断言”
    - [D] `Documents/AutoPlans/ArchitectureReview/TypeSystem_ArchReview.md` — “`GetClass()` 只表示 direct reference class，`ResolveCodeSuperForProperty()` / `FinalizeActorClass()` 误把它当成 nominal-class resolver”
    - [E] `Documents/AutoPlans/ReferenceComparison/GapAnalysis.md` — “建议把 `TryResolveNominalClass(TypeDesc, ExpectedBaseClass)` 抽成结构化 query，而不是继续让高层 materialization 代码猜 wrapper subtype”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` L394-L396、L3165-L3188、L5228-L5252、L5328-L5334 — 当前 `DefaultComponent` 分析仍调用 `ResolveCodeSuperForProperty(PropertyType)`，而该 helper 只看 `Usage.GetClass()` 或 script class `CodeSuperClass`；`FinalizeActorClass()` 仍直接把 `Property->PropertyType.GetClass()` 写进 `Comp.ComponentClass` 并据此做 component/scene-component 校验
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、可选新增 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASNominalComponentClassResolver.h/.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASClassComponentNominalClassTests.cpp`
- [ ] **P2.17** 📦 Git 提交：`[ClassGenerator] Fix: resolve component nominal class through a single helper`
- [ ] **P2.17-T** 单元测试：补齐 component nominal-class 解析与 fail-closed contract
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASClassComponentNominalClassTests.cpp`
  - 测试场景：
    - 正常路径：分别用 raw `USceneComponent*` 和 `TObjectPtr<USceneComponent>` 声明 `DefaultComponent` / `Attach`，编译后读取 `UASClass::DefaultComponents`，确认两条路径都得到同一个 nominal `ComponentClass`、`bIsRoot` 与 `Attach` 元数据
    - 边界条件：`OverrideComponent` 使用 wrapper 声明时，`OverrideComponentName`、`VariableName` 与继承校验和 raw 路径保持一致；`RootComponent` / `Attach` 的 scene-component 判定继续基于 nominal class，而不是 wrapper 外壳
    - 错误路径：`TSubclassOf<USceneComponent>`、`TWeakObjectPtr<USceneComponent>` 或其它当前不支持的 wrapper 若被标成 `DefaultComponent/OverrideComponent`，会在编译期稳定报 unsupported/invalid nominal-class 诊断，不会把 `nullptr` 写进 `Comp.ComponentClass` 后继续发布类壳
  - 测试命名：`Angelscript.TestModule.ClassGenerator.ComponentNominalClass.<CaseName>`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P2.17-T** 📦 Git 提交：`[ClassGenerator] Test: cover component nominal class resolution and fail-closed rules`

### Phase 4 深化：收口 class member signature owner

- [ ] **P4.10** 在 class analysis 阶段生成单一 `ResolvedMemberDesc/ScriptPropertySignature`，让 property create、`ReferenceSchema`、`DebugValues` 与 property diff 共用同一份成员真相
  - 这项不重复 `P1.8` 的 `ReferenceSchema` owner，也不重复 `P4.3` 的 `DebugValues` 生命周期；它补的是它们共同依赖的前置 owner。当前 `ClassGenerator` 先从 `ScriptType->GetProperty()` 建 `PropertyIndexMap` 与 `PropertyTypes`，再按名字回填 `FAngelscriptPropertyDesc`，随后 `CreateReferenceSchema()` 和 `CreateDebugValuePrototype()` 又再次遍历 `ScriptType->GetProperty()`，必要时再用 `FromProperty(ScriptType, i)` 回退推导类型。结果是 parser property desc、live script property 表、schema build 和 debug prototype 都可能各自读到不同版本的成员语义。
  - 首阶段应在 `ClassGenerator/` 内新增 `FAngelscriptResolvedMemberDesc` 或 `FAngelscriptScriptPropertySignature`，把 `PropertyName`、`ResolvedTypeUsage`、`ScriptPropertyIndex`、`ScriptPropertyOffset`、访问控制与 `bHasUnrealProperty` 一次性 join 完，再让 `CreateProperty()`、`CreateReferenceSchema()`、`CreateDebugValuePrototype()` 与 hot reload property diff 统一消费这份列表；`FromProperty(ScriptType, i)` 仅保留给局部变量或 debugger 临时值这类没有 class member desc 的 VM-only fallback。
  - 这样后续无论是 `TObjectPtr<UObject>`、`TArray<UObject>` 还是未来 `TScriptInterface<>` 成员，扩展点都回到单一 member-signature builder，而不是继续要求 property 物化、GC schema 和 debug prototype 三处同时补 branch。
  - 来源：
    - [C] `Documents/AutoPlans/TestCoverage/ClassGenerator_TestGaps.md` — “NewTest-24/NewTest-22：`ReferenceSchema` 与 `ScriptPropertyOffset` 当前都缺 targeted regression，现有 suite 没锁住成员布局元数据跨消费链一致性”
    - [D] `Documents/AutoPlans/ArchitectureReview/TypeSystem_ArchReview.md` — “当前没有一次构建后复用的 `ResolvedMemberDesc`；property create、GC schema 与 debug prototype 都在独立重扫 `ScriptType->GetProperty()`”
    - [E] `Documents/AutoPlans/ReferenceComparison/GapAnalysis.md` — “建议用 `FAngelscriptPropertySignature / FAngelscriptPropertyBridgeDesc` 把 `Object / Interface / Array / Map / Set / Optional / Delegate` 收口成结构化 family，而不是在多个消费点各写一套特判”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` L200-L220、L345-L379、L4875-L4924、L4939-L4954 — 当前 class analysis 仍先建 `PropertyIndexMap/PropertyTypes`，再回填 `FAngelscriptPropertyDesc`；`CreateReferenceSchema()` 与 `CreateDebugValuePrototype()` 仍各自重新遍历 `ScriptType->GetProperty()` 并在 fallback 时再次调用 `FromProperty(ScriptType, i)`
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`、可选新增 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASResolvedMemberDesc.h/.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASClassResolvedMemberTests.cpp`
- [ ] **P4.10** 📦 Git 提交：`[ClassGenerator] Refactor: build one resolved-member descriptor for property schema and debug`
- [ ] **P4.10-T** 单元测试：补齐成员签名在 property/schema/debug 三条消费链上的一致性回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASClassResolvedMemberTests.cpp`
  - 测试场景：
    - 正常路径：编译同时包含 reflected `UPROPERTY`、script-only `UObject` 成员、`TArray<UObject>` 与 `TObjectPtr<UObject>` 的脚本类，断言 property materialization、`ReferenceSchema` 与成员 offset/index 快照来自同一份 resolved-member 结果；若 `WITH_AS_DEBUGVALUES` 开启，再断言 debug prototype 的成员名/offset 与该快照一致
    - 边界条件：对同一类做 member reorder、访问控制变化或 body-only hot reload，`ScriptPropertyIndex`、`ScriptPropertyOffset`、`bHasUnrealProperty` 在 property create、schema rebuild 与 debug prototype rebuild 三处同步更新，不会出现某一路仍保留旧次序
    - 错误路径：当 parser desc 与 live `ScriptType->GetProperty()` 名称/类型不一致，系统会在 member-signature build 阶段稳定失败，不会先部分创建 `FProperty`、再发布半更新的 `ReferenceSchema/DebugValues`
  - 测试命名：`Angelscript.TestModule.ClassGenerator.ResolvedMemberDesc.<CaseName>`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P4.10-T** 📦 Git 提交：`[ClassGenerator] Test: cover resolved-member descriptor consistency across property schema and debug`

### 单元测试总览补充

| 改进项 | 测试文件 | 测试场景 | 优先级 |
| --- | --- | --- | --- |
| `P2.17` | `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASClassComponentNominalClassTests.cpp` | raw object 与 wrapper component 声明的 nominal-class 解析一致性、unsupported wrapper fail-closed | P1 |
| `P4.10` | `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASClassResolvedMemberTests.cpp` | property/schema/debug 三路共享同一成员签名、member reorder/hot reload 后一致更新 | P1 |

### 验收补充

43. `DefaultComponent/OverrideComponent` 不再把 `FAngelscriptTypeUsage::GetClass()` 误当成 nominal-class authority；component layout、`Attach/RootComponent` 校验与 unsupported wrapper 诊断都走同一条 `ResolveNominalComponentClass()` seam。
44. class member 的 `PropertyType`、`ScriptPropertyIndex`、`ScriptPropertyOffset` 与 `bHasUnrealProperty` 在 property 物化、`ReferenceSchema`、`DebugValues` 和 property diff 四条链路上只由同一份 `ResolvedMemberDesc/ScriptPropertySignature` 产出，不再依赖多次独立重扫 `ScriptType->GetProperty()`。

### 风险补充

39. **把 component nominal-class 解析从 `GetClass()` 切到统一 helper 后，最容易出现“raw object 老路径保持正确，但 wrapper family 只迁了一半”的双轨问题**
   - 缓解：首版要求 `Analyze()`、`FinalizeActorClass()`、`Attach/RootComponent` 校验与 unsupported-wrapper diagnostic 同轮迁移，并用 raw object + `TObjectPtr` + unsupported wrapper 三组测试同时锁住 compile-time metadata 与 fail-closed 行为。
40. **把成员解析收口成 `ResolvedMemberDesc/ScriptPropertySignature` 后，最容易出现“property create 已改读新列表，但 schema/debug 仍偷偷走旧 fallback”**
   - 缓解：迁移期先让新 descriptor 反向填充旧字段，再把 `CreateReferenceSchema()`、`CreateDebugValuePrototype()` 与 property diff 一起改读同一列表；测试同时覆盖 happy path、member reorder/hot reload 和 mismatch fail-fast，避免只在正常编译场景下看起来一致。

---

## 深化 (2026-04-09 07:43)

- 现有 `P2.8/P5.2` 已分别覆盖 class-level 反射身份半提交与长期 `PropertyMaterializationPlan`，但五维输入交叉回看后，`ClassGenerator` 里仍有两条没有独立落项的 soft-reload contract：其一是 `default` / `CPP_Default_*` / function metadata 仍会在 `SoftReloadOnly` 下进入“代码已切换、公开默认值与 callable surface 仍旧”的半提交状态；其二是 property specifier 变化虽然在分析阶段已被看见，却仍会在执行阶段继续复用旧 `FProperty` 壳，把 `CPF_*` / replication layout 留在旧 epoch。
- 对 `Documents/Plans/` 的活跃 Plan 再检索后，`Plan_AngelscriptEngineBindAndFileWatchValidation.md` 只覆盖 broad watcher/reload matrix，`Plan_NetworkReplicationTests.md` 只覆盖网络能力与测试矩阵；下面两项只聚焦 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/` 内的 runtime contract 与 fail-closed barrier，不与现有活跃 Plan 重复。

### Phase 2 深化：补齐 soft reload 的默认值与 property contract

- [ ] **P2.18** 把 `default` / `CPP_Default_*` / function metadata 从 `FullReloadSuggested` 灰区里拆出来，禁止 `SoftReloadOnly` 继续半提交旧默认语义
  - 这项不重复 `P2.8` 的 class-level metadata half-commit，也不回滚前面已经规划的 function dispatch/cache 收口；它补的是更基础的“什么可以在 soft path 里立即提交”合同。当前分析阶段把参数默认值、函数 metadata、`default` 语句、class metadata 与 class flags 大多都只降到 `FullReloadSuggested`，但执行阶段的 `DoSoftReload()` 会直接保留旧 `DefaultsCode`、继续复用旧 `UFunction` 壳，并且只更新极少量 `FUNCMETA_ScriptNoOp`，导致同一轮 reload 后出现“新脚本函数体 + 旧 `CPP_Default_*` / 旧 Blueprint pin 默认值 / 旧 callable metadata”的混合状态。
  - 这项首先要把 delta 明确分类成 `DefaultsReplaySafe`、`FunctionDefaultArgOnly`、`FunctionMetaCosmetic`、`UnsafeFunctionContract` 之类的最小集合：只有纯 `ToolTip/DisplayName/Category` 级别的白名单 metadata 才允许 soft refresh；`BlueprintCallable/BlueprintPure/WorldContext/Latent/Exec` 这类会改变节点形状或调用合同的变化，必须直接升级为 `FullReloadRequired` 或稳定 `DeferredFunctionContractReload`，不能继续在 `SoftReloadOnly` 里沿用旧 `UFunction` 壳。
  - 对 `default` 语句与 `CPP_Default_*` 的近端修复应优先 fail-closed，再逐步开放 replay：首版至少要阻止 `DoSoftReload()` 把 `DefaultsCode` 强行回退到旧版本，同时继续让 `UpdateConstructAndDefaultsFunctions()` 指向新 `DefaultsFunction` 的双轨状态；如果当前还没有可靠的 `DoDefaultsReplayReload()`/`RefreshFunctionMetadataSoft()`，就必须在分析阶段给出稳定 deferred/full-reload 诊断，而不是把“默认值已变但尚未提交”伪装成 handled soft reload。
  - 来源：
    - [A] `Documents/AutoPlans/ClassGenerator_Analysis.md` — “发现 4/43/22：soft reload 会保留旧 `DefaultsCode`、删除 `__InitDefaults()` 后仍可能保留旧 defaults 语义，`Blueprintable` 等 class metadata 也只在 full reload 路径写入”
    - [D] `Documents/AutoPlans/ArchitectureReview/HotReloadArch_ArchReview.md` — “Arch-HR-9：`default` / `CPP_Default_*` / 展示型 metadata 被统一降到 `FullReloadSuggested`，soft path 会继续运行旧默认语义”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — “[D6] 默认参数 canonicalization：`CPP_Default_*` 属于公开签名 contract，不应在 reload 后停留在旧 artifact/旧 metadata”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` L1222-L1237、L1252-L1259、L1290-L1295、L1311-L1322、L3995-L4003、L4140-L4141、L4253-L4269 — 当前参数默认值、函数 metadata、`DefaultsCode`、class metadata/flags 变化大多只会被降到 `FullReloadSuggested`；`CPP_Default_*` 只在 full/new function 生成时写入；`DoSoftReload()` 仍强制回退旧 `DefaultsCode`，并且除 `FUNCMETA_ScriptNoOp` 外不刷新 function metadata
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`、可选新增 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASDefaultSemanticDelta.h/.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadDefaultSemanticTests.cpp`
- [ ] **P2.18** 📦 Git 提交：`[ClassGenerator/HotReload] Fix: split default and function-metadata soft-reload contract`
- [ ] **P2.18-T** 单元测试：补齐 `default` / `CPP_Default_*` / function metadata 的 soft-reload 分流回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadDefaultSemanticTests.cpp`
  - 测试场景：
    - 正常路径：仅函数 body 变化时，`SoftReloadOnly` 仍能立即生效，且 `UFunction` 默认参数 metadata、Blueprint pin 默认值与新旧实例观察到的默认语义保持一致
    - 边界条件：只改 `default` 赋值表达式、只改 `CPP_Default_*`、或只改 `DisplayName/ToolTip/Category` 这类白名单 metadata 时，系统要么安全 replay/refresh，要么给出明确 deferred/full-reload 诊断；不能出现“脚本函数已更新、公开默认值仍旧”的混合状态
    - 错误路径：`BlueprintCallable`、`BlueprintPure`、`WorldContext`、`Latent`、`Exec` 等会改变 callable contract 的 metadata 变化在 `SoftReloadOnly` 下必须返回 `ErrorNeedFullReload` 或稳定 deferred 结果，不得继续复用旧 `UFunction` 壳并对外宣称 handled
  - 测试命名：`Angelscript.TestModule.HotReload.DefaultSemantics.<CaseName>`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P2.18-T** 📦 Git 提交：`[ClassGenerator/HotReload] Test: cover defaults cpp-default metadata and callable-contract reload rules`

- [ ] **P2.19** 在 `P5.2` 的长期物化计划之前，先把 property specifier 变化从 `SoftReloadOnly` 的旧 `FProperty` 壳复用里隔离出去
  - 这项不重复 `P5.2` 的最终 `PropertyMaterializationPlan`，而是补一个更近端的 correctness barrier。当前 property metadata 变化在分析阶段仍只会把 `ReloadReq` 提升到 `FullReloadSuggested`，随后 `DoSoftReload()` 仅对旧 `FProperty` 执行 `Link(ArDummy)` 与 offset 重链；真正写入 `CPF_Net`、`CPF_RepNotify`、`RepNotifyFunc`、`CPF_SaveGame`、`CPF_Config`、`CPF_ExposeOnSpawn`、`CPF_EditFixedSize`、`CPF_EditorOnly` 的逻辑仍只存在于 full/new property materialization。结果是脚本源码里的 property specifier 已经变化，但 live `FProperty` flags、replication list 与 editor-visible contract 仍停留在旧 epoch。
  - 首版优先 fail-closed，不尝试在 soft path 里“热修”旧 `FProperty` 壳：把 `ReplicatedUsing`、`ReplicationCondition`、`SaveGame`、`Config`、`SkipSerialization`、`ExposeOnSpawn`、`EditFixedSize`、`EditorOnly` 等会改变 `FProperty` flags 或 UE 语义的 key 单独折成 `PropertyReflectionContractDelta`，只要命中就升级为 `FullReloadRequired` 或稳定 deferred full reload；不要继续让 `SoftReloadOnly` 在旧 `FProperty` 上假装 handled。
  - 等 `P5.2` 的 `PropertyMaterializationPlan` 落地后，再把这条 barrier 降级成 plan-driven执行：届时 new/full path 与 replication list 都改读同一份 plan；但在那之前，必须先保证不会再出现“新源码 + 旧 property flags/repl layout”的半提交状态。
  - 来源：
    - [A] `Documents/AutoPlans/ClassGenerator_Analysis.md` — “发现 23/28：property metadata/specifier 变化只给 `FullReloadSuggested`，`DoSoftReload()` 不会重写 `CPF_Net/CPF_RepNotify/ExposeOnSpawn/EditFixedSize/EditorOnly` 等 flags”
    - [C] `Documents/AutoPlans/TestCoverage/ClassGenerator_TestGaps.md` — “NewTest-18：`GetLifetimeScriptReplicationList()` 目前零覆盖，现有 suite 没锁住 replicated property 的 reflection contract”
    - [D] `Documents/AutoPlans/ArchitectureReview/TypeSystem_ArchReview.md`、`Documents/AutoPlans/ArchitectureReview/HotReloadArch_ArchReview.md` — “property materialization 的 owner 仍把 GC lane 与 replication/editor/serialization 分开；soft path 只 relink 旧 property，不会同步这些语义”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` L1117-L1124、L2923-L3039、L4150-L4174，`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` L894-L908 — 当前 property metadata 变化只会被提升到 `FullReloadSuggested`；`CPF_Net/CPF_RepNotify/RepNotifyFunc/CPF_SaveGame/CPF_Config/CPF_ExposeOnSpawn/CPF_EditFixedSize/CPF_EditorOnly` 仅在 full/new property 生成时写入；soft reload 只 relink 旧 `FProperty`，而 runtime replication list 继续按旧 `CPF_Net` 属性枚举
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`、可选新增 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASPropertyReflectionContract.h/.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPropertyReflectionContractTests.cpp`
- [ ] **P2.19** 📦 Git 提交：`[ClassGenerator/HotReload] Fix: block property-specifier drift from stale FProperty shells`
- [ ] **P2.19-T** 单元测试：补齐 property specifier 在 `SoftReloadOnly` 下的 fail-closed 与 full-reload 分流回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPropertyReflectionContractTests.cpp`
  - 测试场景：
    - 正常路径：纯函数 body 变化且 property specifier 不变时，soft reload 仍可 handled，`GetLifetimeScriptReplicationList()`、`CPF_Net` 与现有 `FProperty` flags 保持稳定
    - 边界条件：只改 `ReplicatedUsing`、`ReplicationCondition`、`ExposeOnSpawn`、`EditFixedSize`、`EditorOnly`、`SaveGame` 或 `Config` 时，系统会稳定升级为 full reload/deferred full reload；full reload 后新的 `FProperty` flags 与 replication list 与脚本源码一致
    - 错误路径：`SoftReloadOnly` 命中 property reflection contract 变化时，不会返回“handled”并继续沿用旧 `FProperty` 壳；旧 class/new source 之间也不会留下“源码已变、`CPF_*` 仍旧”的混合状态
  - 测试命名：`Angelscript.TestModule.HotReload.PropertyReflectionContract.<CaseName>`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P2.19-T** 📦 Git 提交：`[ClassGenerator/HotReload] Test: cover property-specifier full-reload barrier and stale-flag prevention`

### 单元测试总览补充

| 改进项 | 测试文件 | 测试场景 | 优先级 |
| --- | --- | --- | --- |
| `P2.18` | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadDefaultSemanticTests.cpp` | `default`/`CPP_Default_*`/function metadata 的 soft-reload 分流、白名单 refresh 与 unsafe callable-contract barrier | P1 |
| `P2.19` | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPropertyReflectionContractTests.cpp` | property specifier 变化的 full-reload barrier、`FProperty` flag 更新、replication list 一致性 | P1 |

### 验收补充

45. `SoftReloadOnly` 不再允许 `default`、`CPP_Default_*` 与 function metadata 进入“脚本逻辑已更新、公开默认值/Blueprint 反射仍旧”的半提交状态；白名单 metadata 要么被显式刷新，要么被显式延期/升级。
46. property specifier 变化不再继续复用旧 `FProperty` 壳；`CPF_Net/CPF_RepNotify/RepNotifyFunc/CPF_SaveGame/CPF_Config/CPF_ExposeOnSpawn/CPF_EditFixedSize/CPF_EditorOnly` 与 `GetLifetimeScriptReplicationList()` 在 reload 后只会和同一轮已提交的 property materialization epoch 保持一致。

### 风险补充

41. **把 `default` / `CPP_Default_*` / function metadata 从 `FullReloadSuggested` 灰区拆出来后，最容易出现“白名单太宽，仍把 callable-contract 变化误放进 soft path”**
   - 缓解：首版只对白名单极小集合开放 soft refresh，`BlueprintCallable/BlueprintPure/WorldContext/Latent/Exec` 一律先 fail-closed；测试同时覆盖白名单刷新与高风险 metadata barrier，避免只在展示型 metadata 上看起来正确。
42. **给 property specifier 增加 soft-reload barrier 后，短期内可能把过去侥幸通过的 `SoftReloadOnly` 工作流升级成更多 deferred/full reload**
   - 缓解：这项先作为 `P5.2` 之前的 correctness 护栏，优先保证“不再留下旧 `FProperty` flags”；同时把诊断写清“命中哪类 property reflection contract 变化”，让后续 `PropertyMaterializationPlan` 有明确的降级与回收目标。

---

## 深化 (2026-04-09 07:52)

- 本轮按实际只读输入复核时，未在仓内定位 `Documents/AutoPlans/DiscoveryPlans/ClassGenerator_Plan.md`；以下新增条目只引用当前实际存在且已复核的 [A] / [C] / [D] / [E] 文档，并全部补做源码锚点确认。
- 定向检索 `Documents/Plans/` 后，当前活跃 Plan 中未见 `GetUnrealName`、`A/U` 前缀折叠或 generated object-name key 同主题执行项；因此以下补充只聚焦 `ClassGenerator` 内部的 symbol identity correctness，不与现有活跃 Plan 重复。

### Phase 1 深化：补齐 `A/U` 前缀折叠导致的 generated class identity 冲突

- [ ] **P1.11** 把动态类对象名从 `GetUnrealName()` 的 `A/U` 前缀折叠里剥离，禁止 `AFoo` / `UFoo` 被误判成同一 reload 目标
  - 这项不重复 `P1.5` 的 namespace/qualified-key 主线，而是补它尚未覆盖的另一半 identity seam：即使 namespace 和 `DataRef` key 都收口后，只要两个 script class 位于同一 module / 同一 namespace，`GetUnrealName(false, ClassName)` 仍会把 `AFoo` 与 `UFoo` 同时压成 `Foo`。预检查、replace 判断与 `NewObject<UASClass>` 仍全部吃这个折叠后的对象名，因此两个原本不同的公开 symbol 会共享同一个 `UASClass` 名位，并在 full reload 时互相被当成 `ReplacedClass`。
  - 落地时应新增 `BuildGeneratedTypeObjectKey()` / `FAngelscriptGeneratedTypeKey` 或等价 helper，把“脚本公开符号名”“host kind/class kind”“module-scoped canonical symbol”与“运行时对象名”分开：`FindObject`、`CreateFullReloadClass()`、旧类替换、`StaticClass` 回填与后续 tombstone/rename 全部只使用稳定 object key；`GetUnrealName()` 最多保留给展示或 legacy 诊断，不再担当 runtime identity。若需要兼容历史仅存在单一旧对象名的场景，可以增加一次性 legacy lookup/migrate；一旦发现 `AFoo/UFoo` 这类歧义旧名位，必须 fail-closed，而不是继续把两个 symbol 串成同一条版本链。
  - 同时要把 `GetClassDesc()` / `FAngelscriptEngine::Get().GetClass(ClassName)` 的 fallback 语义与这条 object key 解耦：script symbol lookup 继续按公开符号名工作，generated `UClass` lookup 则改查稳定 object key，避免“脚本名是两个，运行时名位只有一个”的双真相继续扩散到 discard/recompile、rename 与 same-symbol reload 场景。
  - 来源：
    - [A] `Documents/AutoPlans/ClassGenerator_Analysis.md` — “发现 27：`GetUnrealName()` 会把 `A`/`U` 前缀压成同一 `UClass` 名，而同模块冲突会被误判成‘同一类正在 reload’”
    - [C] `Documents/AutoPlans/TestCoverage/ClassGenerator_TestGaps.md` — “Issue-18：`DiscardAndRecompile` 通过改类名绕开了同一 public symbol 的名字回收与 canonical lookup 更新，当前测试没有锁住同名 recompile/replace 语义”
    - [D] `Documents/AutoPlans/ArchitectureReview/TypeSystem_ArchReview.md` — “Arch-TS-07：类型 canonical key 仍以脚本名和少量特例槽位为主，identity 会继续退化成字符串 + special-case”
    - [E] `Documents/AutoPlans/ReferenceComparison/GapAnalysis.md`、`Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — “reload correctness 一旦继续依赖 short-name / global scan，而不是稳定 symbol key 与 registry family 一致性，就会在同名场景下变成非确定性行为”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` L145-L156、L159-L170、L224-L285、L1739-L1766、L2570-L2586 — 当前 `GetClassDesc()`/engine fallback 仍按短 `ClassName` 查找，`GetUnrealName()` 会对非 struct 的 `A*`/`U*` 且第二字符大写的类统一 `Mid(1)`，预检查与 full reload 又都用折叠后的 `UnrealName` 去 `FindObject` / `Rename` / `NewObject<UASClass>`，`DataRefByName` 也仍按原始 `ClassName` 建表，运行时因此持续存在“脚本符号两份、generated object slot 一份”的双真相
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、可选新增 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASGeneratedTypeObjectKey.h/.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptGeneratedTypeObjectKeyTests.cpp`
- [ ] **P1.11** 📦 Git 提交：`[ClassGenerator] Fix: give generated script classes a stable object key beyond A/U prefix folding`
- [ ] **P1.11-T** 单元测试：补齐 `AFoo` / `UFoo` 同后缀脚本类的 generated-name key、replace 与 discard/recompile 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptGeneratedTypeObjectKeyTests.cpp`
  - 测试场景：
    - 正常路径：同一 module 内同时编译 `AFoo : AActor` 与 `UFoo : UObject` 时，会生成两个不同的 `UASClass` 对象名位；`FindGeneratedClass()`、`StaticClass()`、spawn/new 路径各自命中正确 symbol，彼此不会互相替换
    - 边界条件：只对 `AFoo` 做 full reload 或 discard/recompile 时，`UFoo` 的 `UASClass`、defaults 与 version-chain 保持不变；旧 `AFoo` 被 rename/tombstone 时也不会误动 `UFoo`
    - 错误路径：若存在历史遗留的歧义旧对象名位或 object key 真正冲突，本轮编译会稳定报冲突错误，不会把 `AFoo` 当成 `UFoo` 的 `ReplacedClass`，也不会留下 `/Script/Angelscript.Foo` 指向处理顺序相关对象的非确定性状态
  - 测试命名：`Angelscript.TestModule.ClassGenerator.GeneratedTypeObjectKey.<CaseName>`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P1.11-T** 📦 Git 提交：`[ClassGenerator] Test: cover stable generated object key across A/U prefix collisions`

### 单元测试总览补充

| 改进项 | 测试文件 | 测试场景 | 优先级 |
| --- | --- | --- | --- |
| `P1.11` | `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptGeneratedTypeObjectKeyTests.cpp` | `AFoo/UFoo` 同后缀 symbol 的 object-key 隔离、replace 不串链、discard/recompile 后 lookup 稳定 | P1 |

### 验收补充

47. dynamic script class 的 runtime object key 不再由 `GetUnrealName()` 折叠后的 `A/U` 去前缀名字承担；`AFoo` / `UFoo` 这类同后缀不同宿主类脚本会拥有稳定且彼此独立的 `UASClass` 名位、replace 入口与版本链。

### 风险补充

43. **把 generated object key 从历史 `GetUnrealName()` 规则切走后，最容易出现旧对象名位迁移不完整，导致 legacy lookup 与新 key 双轨并存**
   - 缓解：首版要求所有 `FindObject` / replace / rename / `StaticClass` 回填统一改走新 helper，并补一条“存在歧义旧名位时 fail-closed”的迁移规则；测试同时覆盖首编译、full reload 与 discard/recompile，避免只在 clean workspace 下看起来正确。
