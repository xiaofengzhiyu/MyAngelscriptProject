# BindSystem 改进计划

## 背景与目标

### 背景

当前 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` 已经形成 `123` 个 `Bind_*.cpp` 的稳定能力面，但本轮五维交叉分析显示，`BindSystem` 的主要风险已经从“有没有文件”转向“现有 bind 是否仍然正确、契约是否一致、三条绑定产物链是否可审计”。尤其是 `Bind_AActor.cpp` / `Bind_UWorld.cpp` 的 world-context 与 spawn 安全、`Bind_TArray.cpp` 的容器语义、`Bind_FInputActionValue.cpp` / `Bind_UEnhancedInputComponent.cpp` / `Bind_FInputBindingHandle.cpp` 的新绑定簇正确性、`Bind_UObject.cpp` 的 class lookup 与对象工厂元数据、以及 `Bind_BlueprintType.cpp` 的双轨绑定主链，都已经被多个维度重复命中。

`Documents/Plans/Plan_UEBindGapRoadmap.md` 已经覆盖“继续扩展 API surface”的主线；本计划不重复那份路线图中的 gameplay/API 扩面，而是只处理当前 bind 体系仍然存在的 correctness、contract 和 pipeline 治理缺口，确保后续扩面不会建立在脆弱基线之上。

### 目标

- 消除当前高风险 bind 的直接崩溃、silent failure 和返回值语义漂移，先把现有入口修到“可依赖”状态。
- 恢复 `__WorldContext`、construction safety、`no_discard`、class lookup 等关键脚本 contract，使公开 surface 与内部生成链不再分叉。
- 为 world/spawn、container、EnhancedInput、class lookup、binding coverage manifest 建立 focused regression，至少覆盖正常路径、边界条件、错误路径三类场景。
- 让 manual bind、reflective bind、bind-db/UHT 产物之间开始拥有同一份可机器对账的 coverage/provenance 视图，避免下一轮继续靠 grep 和人工比对推进。

## 范围与边界

- 纳入范围：
  - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/`
  - 为修复 bind contract 所必需的最小 `Core/`、`Editor/`、`UHTTool/` 配套改动
  - `Plugins/Angelscript/Source/AngelscriptTest/` 下的 `Bindings/`、`Core/` 回归测试
  - 与 contract 直接相关的少量文档/manifest 输出
- 排除范围：
  - `Plan_UEBindGapRoadmap.md` 已经覆盖的 gameplay/API 扩面，不在本计划重复列项
  - `P10 UInterface` 主线的完整实现，不在本计划展开，只在 manifest/provenance 层预留基础设施
  - 一次性重写全部 `Bind_*.cpp` 的大重构；基础设施项优先采用 report-only / 兼容层策略

## 当前事实状态快照

- `Bind_AActor.cpp` 里的三条 `SpawnActor*` 入口仍在判空前直接解引用 `GEngine`，并且直接 `return World->SpawnActor(...)`；`FinishSpawningActor(nullptr)` 仍是 silent no-op。
- `Bind_TArray.cpp` 仍把 `Reserve()` 实现成 `Reset()`，`SetNum()` 对非构造类型不做零初始化，`RemoveSwap()` 删除后不累计返回值，`Add/Insert/Remove/RemoveSwap` 也没有 self-alias 地址防线。
- `Bind_FInputActionValue.cpp`、`Bind_UEnhancedInputComponent.cpp`、`Bind_FInputBindingHandle.cpp` 这组 current-only 新增绑定文件已经有明确签名/const 契约漂移。
- `Bind_UWorld.cpp` 仍只公开 `__WorldContext()` 函数；`Helper_FunctionSignature.h` 只认识 `OptionalWorldContext`，不再等价表达 `CallableWithoutWorldContext`；`Bind_UObject.cpp` 中对象创建/加载 helper 也未回填高风险元数据。
- `Bind_UObject.cpp` 里同时存在 namespace/global 两套 class lookup 实现；`Bind_BlueprintType.cpp` 则继续维护 bind-db lane 与 live-scan lane 两份 reflective bind 主链。

## 分析来源

| 分析文档 | 关键发现 |
|---------|---------|
| `Documents/AutoPlans/BindSystem_Analysis.md` | 命中 world/spawn 安全、`__WorldContext` 漂移、`UnsafeDuringConstruction` trait 丢失、class lookup/tombstone 漏筛、`TArray` 语义回退等核心 correctness 问题 |
| `Documents/AutoPlans/DiscoveryPlans/BindSystem_Plan.md` | 把 `SpawnActor`、`GetCurrentWorld`、`FinishSpawningActor`、`TArray`、EnhancedInput、class lookup、`__WorldContext` 等问题转成可执行修复方案 |
| `Documents/AutoPlans/TestCoverage/BindSystem_TestGaps.md` | 明确 world/spawn、container、class lookup、`Bind_UWorld.cpp`、EnhancedInput 等区域仍缺直接自动化 |
| `Documents/AutoPlans/ArchitectureReview/BindingPipeline_ArchReview.md` | 指出 provider ownership 缺失、`BindingCoverageManifest` 缺位、`PreviousBind` 元数据时序耦合、`Bind_BlueprintType` 双轨维护等结构性问题 |
| `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` | D2/D6 对比显示当前插件应继续保持“类型系统 + bind database”主线，同时把现有 artifact 收束成单一 contract |
| `Documents/AutoPlans/ReferenceComparison/GapAnalysis.md` | 明确指出 bind provider owner、manifest、runtime/type metadata 缺少单一真相，继续扩面会放大对账成本 |
| `Documents/AutoPlans/ReferenceComparison/Hazelight_Analysis.md` | 血缘对比确认 `__WorldContext`、`CallableWithoutWorldContext`、`UnsafeDuringConstruction`、construction trait 等 contract 已与 UEAS2 漂移 |

## 分阶段执行计划

### Phase 1：先修当前会直接伤害正确性的 bind 行为

- [ ] **P1.1** 收口 world-context / spawn 的安全合同，统一 `Bind_AActor.cpp` 与 `Bind_UWorld.cpp` 的失败语义
  - 当前 actor/world 入口在同一组 bind 里同时暴露了三类问题：`GEngine` 判空前裸解引用、`SpawnActor*` 绕过 `VerifySpawnActor` 审核钩子、`FinishSpawningActor(nullptr)` 直接 silent return。它们都集中在已有公开入口上，继续留着会让后续任何 API 扩面都建立在不稳定基础上。
  - 这一项不扩新 API，只把现有入口先修成统一 contract：抽一个仅供 bind 内部使用的 world-context 解析 helper，先收口 `GEngine == nullptr` / invalid world-context，再让三条 `SpawnActor*` 在真正 `World->SpawnActor(...)` 前统一经过 `FAngelscriptRuntimeModule::GetVerifySpawnActor()`；两个 `FinishSpawningActor` overload 则改成与同文件 `GetComponentsByClass` 一致的显式脚本错误。
  - 这项工作应优先落在 `Binds/` 内部 helper + `AngelscriptRuntimeModule` 的最小扩展，不把 spawn policy 继续散落在各个 bind lambda 里。
  - 来源：
    - [A] `BindSystem_Analysis.md` — “发现 10/74/76：spawn 审核钩子丢失、`GEngine` 无保护、`FinishSpawningActor` 空输入静默吞掉”
    - [B] `DiscoveryPlans/BindSystem_Plan.md` — “Issue-4/5/6：恢复 `VerifySpawnActor`、抽 world-context helper、统一 `FinishSpawningActor` 错误契约”
    - [C] `TestCoverage/BindSystem_TestGaps.md` — “NewTest-2 / NewTest-28：world-backed actor helper 与 `GetCurrentWorld()` 仍无直接回归”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp` L166-L196 — `SpawnActorFromMeta()` 先 `GEngine->GetWorldFromContextObject(...)`，随后直接 `return World->SpawnActor(...)`，没有审核 delegate
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp` L199-L253 — `SpawnActor()` / `SpawnPersistentActor()` 同样直接 spawn
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp` L256-L283 — 两个 `FinishSpawningActor` overload 在 `Actor == nullptr` 时直接 `return`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UWorld.cpp` L33-L41 — `GetCurrentWorld()` 直接解引用 `GEngine`
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UWorld.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_WorldContextHelpers.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptWorldBindingsTests.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptActorBindingsTests.cpp`
- [ ] **P1.1** 📦 Git 提交：`[AngelscriptRuntime] Fix: harden world context and actor spawn binds`
- [ ] **P1.1-T** 单元测试：补齐 world/spawn 安全回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptWorldBindingsTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptActorBindingsTests.cpp`
  - 测试场景：
    - 正常路径：有效 world-context 下 `GetCurrentWorld()`、`SpawnActor()`、`SpawnPersistentActor()`、`FinishSpawningActor()` 都保持 current 正常行为；审核 delegate 返回 `true` 时 actor 正常生成
    - 边界条件：`VerifySpawnActor` 未绑定、绑定但返回 `false`、deferred spawn + `FinishSpawningActor` 完成一次的边界都要稳定可观测
    - 错误路径：`GEngine == nullptr`、invalid world-context、`FinishSpawningActor(nullptr)`、重复 finish 都要产生固定脚本错误，而不是崩溃或 silent no-op
  - 测试命名：`Angelscript.TestModule.Bindings.WorldContextAndActorSpawnGuards`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_FULL()` + `FScopedTestWorldContextScope`
- [ ] **P1.1-T** 📦 Git 提交：`[AngelscriptTest] Test: cover world context and actor spawn guard regressions`

- [ ] **P1.2** 恢复 `TArray` bind 的语义基线与 self-alias 防线
  - 当前 `TArray` 绑定同时存在四条高风险回退：`Reserve()` 实际清空数组、`SetNum()` 对非构造类型暴露未初始化槽位、`RemoveSwap()` 返回值永远是 `0`、多个 mutator 缺少对“把容器内部元素引用再传回正在修改该容器”的地址保护。它们已经不是风格问题，而是脚本数据正确性问题。
  - 这一项直接按 UEAS2 的已知正确 contract 回补：`Reserve()` 改回纯 capacity 调整，`SetNum()` 在非构造类型路径补零初始化，`RemoveSwap()` 恢复计数累加，并把 `CheckAddress()` 接回 `Add()/Insert()/Remove()/RemoveSwap()` 四条会在持有 `Value` 指针期间修改容器的 mutator。
  - 这里优先修“当前脚本会得到错误结果或野地址”的行为，不扩更多 container helper。
  - 来源：
    - [A] `BindSystem_Analysis.md` — “发现 79/80/81/82：self-alias 无防线、`RemoveSwap` 计数丢失、`Reserve` 清空数组、`SetNum` 暴露垃圾值”
    - [B] `DiscoveryPlans/BindSystem_Plan.md` — “Issue-7/8/68：恢复 `Reserve`、`RemoveSwap` 与 `CheckAddress()` 合同”
    - [C] `TestCoverage/BindSystem_TestGaps.md` — “新增容器回归应同时覆盖 `Reserve`、`SetNum`、`RemoveSwap`、self-alias”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp` L910-L927 — `Reserve()` 仍调用 `Arr.Reset(...)`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp` L930-L967 — `SetNum()` 走 `SetNumUninitialized(...)`，只在 `bNeedConstruct` 时构造新槽位
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp` L654-L675、L796-L825、L1090-L1119、L1157-L1192 — `Add()` / `Insert()` / `Remove()` / `RemoveSwap()` 都未做地址保护，且 `RemoveSwap()` 从未 `NumRemoved++`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray_Functions.h` L17-L42 — 当前 `FArrayOperations` 头里没有 UEAS2 的 `CheckAddress()` helper
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray_Functions.h`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptContainerBindingsTests.cpp`
- [ ] **P1.2** 📦 Git 提交：`[AngelscriptRuntime] Fix: restore TArray reserve and mutation contracts`
- [ ] **P1.2-T** 单元测试：补齐 `TArray` contract 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptContainerBindingsTests.cpp`
  - 测试场景：
    - 正常路径：`Reserve()` 保留既有元素，`SetNum()` 扩容后新槽位按默认值初始化，`RemoveSwap()` 返回真实删除数量
    - 边界条件：`Reserve(小于当前 Num)` 不缩容，`SetNum(0)` 清空后仍可重新追加，未命中 `RemoveSwap()` 返回 `0`
    - 错误路径：`Values.Add(Values[0])`、`Insert(Values[0], 0)`、`Remove(Values[0])`、`RemoveSwap(Values[0])` 必须抛固定脚本错误而不是继续执行
  - 测试命名：`Angelscript.TestModule.Bindings.ArrayContractCompat`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`
- [ ] **P1.2-T** 📦 Git 提交：`[AngelscriptTest] Test: cover TArray reserve and self-alias regressions`

- [ ] **P1.3** 收口 current-only EnhancedInput 绑定簇的签名与 const 合同
  - `Bind_FInputActionValue.cpp`、`Bind_UEnhancedInputComponent.cpp`、`Bind_FInputBindingHandle.cpp` 是 current 相对 Hazelight 新拆出的绑定簇，但当前正好在这组三个文件里同时命中“返回值签名漂移”“mutating API 被标成 `const`”“成员指针 owner type 复制粘贴错误”三类低级 contract 缺口。
  - 这项工作应一次性把该 binding cluster 收口到“公开 surface 与 UE 原生头一致”的最低标准：`opMulAssign` 改回引用返回，四个 `Clear*Bindings()` 去掉错误的 `const`，`FInputDebugKeyBinding::Execute` 改回自己的 owner type，并顺手复核相邻 `EnhancedInputAction*` 绑定有没有同类 copy-paste 残留。
  - 这里不新增 `UEnhancedInputLocalPlayerSubsystem` 等新 surface；只修现有 current-only bind 的 correctness。
  - 来源：
    - [B] `DiscoveryPlans/BindSystem_Plan.md` — “Issue-1/2/3：`opMulAssign` 引用返回漂移、`UEnhancedInputComponent` 清理接口误标 `const`、`FInputDebugKeyBinding::Execute` owner type 错绑”
    - [C] `TestCoverage/BindSystem_TestGaps.md` — “NewTest-30/31/32：EnhancedInput 三个 bind 文件目前完全无直接回归”
    - [E] `ReferenceComparison/Hazelight_Analysis.md` — “current 把 EnhancedInput 提升成独立 binding cluster，必须先把 dedicated bind 的基础 contract 修稳”；`ReferenceComparison/CrossComparison.md` — “当前 callback authority 仍落在 delegate/API 本身，不应继续容忍签名/const 漂移”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FInputActionValue.cpp` L36-L37 — `opAddAssign` 是引用返回，但 `opMulAssign` 的脚本声明仍是 by-value
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UEnhancedInputComponent.cpp` L14-L17 — 四个 `Clear*Bindings()` 全被声明成 `const`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FInputBindingHandle.cpp` L68-L72 — `FInputDebugKeyBinding::Execute` 绑定到了 `FEnhancedInputActionEventBinding`
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FInputActionValue.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UEnhancedInputComponent.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FInputBindingHandle.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptEnhancedInputBindingsTests.cpp`
- [ ] **P1.3** 📦 Git 提交：`[AngelscriptRuntime] Fix: align enhanced input bind contracts`
- [ ] **P1.3-T** 单元测试：补齐 EnhancedInput bind contract 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptEnhancedInputBindingsTests.cpp`
  - 测试场景：
    - 正常路径：`(Value *= 0.5f) *= 0.5f` 正确回写原对象；非 `const` `UEnhancedInputComponent` 仍可清理 binding；`FInputDebugKeyBinding.Execute(Value)` 可编译解析
    - 边界条件：`HasBindings()` 等只读 API 在 `const`/非 `const` 上都保持可用；`opAddAssign` 与 `opMulAssign` 的返回引用语义保持一致
    - 错误路径：`const UEnhancedInputComponent` 调 `ClearActionBindings()` 必须编译失败；`FInputDebugKeyBinding::Execute` 再次错绑 owner type 时 compile smoke 必须红灯
  - 测试命名：`Angelscript.TestModule.Bindings.EnhancedInputContractCompat`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`
- [ ] **P1.3-T** 📦 Git 提交：`[AngelscriptTest] Test: cover enhanced input binding contracts`

### Phase 2：恢复公开 bind contract 的一致性

- [ ] **P2.1** 恢复 `__WorldContext` 与高风险调用元数据的公开合同，停止让内部生成协议代替 public surface
  - 当前 world-context / invocation metadata 的问题不是单点 bug，而是 public surface 与 helper/trait 同时漂移：脚本公开入口从变量变成了函数，hidden world-context 默认值被硬编码成 `__WorldContext()`，`CallableWithoutWorldContext` 已不再被 helper 识别，而 `NewObject` / `LoadObject` / object lookup 这类高风险 helper 也没有把 construction safety / `no_discard` 合同回填回来。
  - 这项工作优先恢复“public script contract 一致”：把 `__WorldContext` 恢复成 canonical 变量入口，`__WorldContext()` 仅保留一轮兼容并立即标 deprecated；`Helper_FunctionSignature.h` 同时接受旧新两种写法，但 canonical 输出回到变量名，并重新把 `CallableWithoutWorldContext` 纳入 world-context trait 决策；对 `Bind_UObject.cpp` 中的对象创建/加载/查找 helper，则用最小 metadata helper 把 `no_discard` 与 construction-safety contract 明确回来。
  - 这里不要求一次性重写整个 binder DSL，但至少要把最容易误用的 world-context/object-factory 路径先收回统一 owner。
  - 来源：
    - [A] `BindSystem_Analysis.md` — “发现 4/5/7：construction-safety trait 丢失、`no_discard` 回退、`__WorldContext` 从变量漂移成函数”
    - [C] `TestCoverage/BindSystem_TestGaps.md` — “NewTest-28：`Bind_UWorld.cpp` 仍无直接回归”
    - [D] `ArchitectureReview/BindingPipeline_ArchReview.md` — “Arch-BP-4：元数据依赖 `PreviousBind` 游标回填，world-context/output-type/trait 都存在时序耦合”
    - [E] `ReferenceComparison/Hazelight_Analysis.md` — “`__WorldContext`、`CallableWithoutWorldContext`、`UnsafeDuringConstruction` 在 current 已与 UEAS2 contract 漂移”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UWorld.cpp` L33-L41 — 公开入口只有 `UObject __WorldContext()`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h` L16-L30 — 只定义了 `NAME_OptionalWorldContext`，未见 `CallableWithoutWorldContext`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h` L223-L230、L423-L429 — hidden world-context 默认值硬编码为 `__WorldContext()`，并且只用 `OptionalWorldContext` 决定是否写 `asTRAIT_USES_WORLDCONTEXT`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp` L514-L589 — `GetTransientPackage()`、`FindClass()`、`NewObject()`、`LoadObject()` 注册后没有 `no_discard` / construction-safety follow-up；`NewObject()` 只补了 `SetPreviousBindArgumentDeterminesOutputType(1)`
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UWorld.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptWorldBindingsTests.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp`
- [ ] **P2.1** 📦 Git 提交：`[AngelscriptRuntime] Refactor: restore world context and invocation metadata contracts`
- [ ] **P2.1-T** 单元测试：补齐 world-context / metadata contract 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptWorldBindingsTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp`
  - 测试场景：
    - 正常路径：`__WorldContext` 与临时兼容别名 `__WorldContext()` 在同一 scope 下返回同一个对象；`NewObject`/`LoadObject` 仍可正常编译执行
    - 边界条件：hidden world-context 默认值 canonical 输出为 `__WorldContext`；`OptionalWorldContext` 与 `CallableWithoutWorldContext` 都保持“隐藏参数但不强制 trait”的既定 contract
    - 错误路径：construction/defaults 场景调用被标记的 object factory helper 会被拒绝；deprecated alias 会被测试捕捉到弃用信息而不是静默常驻
  - 测试命名：`Angelscript.TestModule.Bindings.WorldContextMetadataContract`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_FULL()` / `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`
- [ ] **P2.1-T** 📦 Git 提交：`[AngelscriptTest] Test: cover world context and invocation metadata contracts`

- [ ] **P2.2** 统一 class lookup 的暴露规则，收口 `FindClass` / `__StaticClass` / `GetAllClasses` / `GetAllSubclassesOf` 的 tombstone 与策略旁路
  - `Bind_UObject.cpp` 当前把 class lookup 写成了多份相似但不一致的扫描循环：namespace/global `FindClass()`、`GetAllClasses()`、`GetAllSubclassesOf()`、`__StaticClass()` 的过滤条件并不一致，`GetAllSubclassesOf()` 也没有排除被清理后的 `UASClass` tombstone；`__StaticClass()` 还会绕过 `StaticClassDeprecation` 的迁移策略。
  - 这一项应把“class 是否允许暴露给脚本 lookup”抽成单一 predicate，并把 `__StaticClass` 也拉回同一 policy：namespace/global helper 只保留名字解析差异，不再各自手写 `TObjectIterator<UClass>`；tombstone / deprecated / `CLASS_NewerVersionExists` / abstract / deprecation mode 统一走一套规则。
  - 这一项同时要修测试隔离：现有 class binding 测试对共享引擎残留类状态有依赖，必须补 focused regression，而不是继续让 `FindClass` happy path 给出误导性绿灯。
  - 来源：
    - [A] `BindSystem_Analysis.md` — “发现 3/8/9：`GetAllSubclassesOf` 不过滤 tombstone，`FindClass` / `__StaticClass` 可返回失效类，namespace/global lookup 规则分叉”
    - [B] `DiscoveryPlans/BindSystem_Plan.md` — “Issue-11/45/69：统一 lookup predicate、堵上 `__StaticClass` 策略旁路、过滤 deleted `UASClass`”
    - [C] `TestCoverage/BindSystem_TestGaps.md` — “类查找测试依赖共享引擎残留状态，缺少 reload/remove 后 lookup consistency 回归”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp` L331-L395 — namespace `FindClass()` 直接 `FindObject`，`GetAllSubclassesOf()` 只过滤 `CLASS_Deprecated | CLASS_NewerVersionExists | CLASS_Abstract`，`__StaticClass()` 命中即返回
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp` L519-L553 — global `FindClass()` / `GetAllClasses()` 又维护了另一套循环与命名规则
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp`
- [ ] **P2.2** 📦 Git 提交：`[AngelscriptRuntime] Fix: unify class lookup exposure rules`
- [ ] **P2.2-T** 单元测试：补齐 class lookup consistency 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp`
  - 测试场景：
    - 正常路径：visible native class 与正常 script class 在 namespace/global `FindClass`、`__StaticClass`、`GetAllClasses`、`GetAllSubclassesOf` 上返回一致结果
    - 边界条件：`StaticClassDeprecation` 的 `Allowed / Deprecated / Disallowed` 三种模式下，`__StaticClass` 与 namespace `StaticClass()` 的可见性/弃用行为一致
    - 错误路径：reload/remove 后的 tombstone class 不再出现在 `GetAllSubclassesOf()` / lookup 结果里；无效类名稳定返回 `null`
  - 测试命名：`Angelscript.TestModule.Bindings.ClassLookupConsistency`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`
- [ ] **P2.2-T** 📦 Git 提交：`[AngelscriptTest] Test: cover class lookup consistency and tombstone filtering`

### Phase 3：给 BindSystem 补上可审计的 contract 视图

- [ ] **P3.1** 先落 report-only 的 `BindingCoverageManifest` 与 provider ownership，避免 manual/reflective/UHT 三条链继续各说各话
  - 当前 `BindSystem` 已经不是单靠 `Bind_*.cpp` 的单线体系，而是至少同时存在 manual bind、`Bind_BlueprintType` reflective bind、bind-db 重放、`AS_FunctionTable_*` UHT sidecar 四条产物链。但对执行者来说，今天仍然很难回答“某个符号来自哪条 lane、由哪个 provider 提供、为什么 fallback、为什么没注册”。这会直接抬高后续 `Bind API GAP` 和回归分析成本。
  - 这一项先做 report-only 基础设施，不改现有脚本 API 与 bind 顺序：为现有 `FBind` 自动补 default provider，给 `GetBindInfoList()` 增加 `ProviderName/ModuleName/RegistrationSource/BindingLane`，并生成一份 `BindingCoverageManifest`，把 `Binds.Cache`、`BindModules.Cache`、`AS_FunctionTable_Entries.csv`、runtime bind info 对齐成同一份可 join 报表。
  - 同时在高风险场景做小范围 descriptor/pilot：把 `Bind_AActor` 的 `SpawnActor` 家族、`Bind_UObject` 的 `NewObject`、`Bind_BlueprintType` 的 reflective class bind 至少接到 manifest 可见的 metadata/provenance 记录，不再只靠 “上一个 bind” 的隐式时序。
  - 这一项是治理项，但不是“以后再说”的文档工作；它是后续继续修 bind gap 时避免重复人工比对的必要前置。
  - 来源：
    - [D] `ArchitectureReview/BindingPipeline_ArchReview.md` — “Arch-BP-1/2/4/13/14：provider ownership 缺失、`BindingCoverageManifest` 缺位、`PreviousBind` 元数据时序耦合、`Bind_BlueprintType` 双轨维护”
    - [E] `ReferenceComparison/CrossComparison.md` — “D2/D6：当前最该吸收的是单一 authority/manifest，而不是继续增加分散产物”；`ReferenceComparison/GapAnalysis.md` — “provider owner、artifact manifest、runtime bind provenance 仍缺单一真相”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp` L705-L725 — bind-db lane 直接消费 `FAngelscriptBindDatabase::Get().Classes`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp` L1029-L1047、L1317-L1363 — live-scan lane 又独立遍历 `TObjectRange<UClass>()` 并构造 late bind 顺序
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp` L450-L467 — `SpawnActor` 家族注册后再靠 `SetPreviousBindArgumentDeterminesOutputType(0)` 补元数据
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp` L72、L556-L579 — `GetTypedOuter()` / `NewObject()` 也依赖“注册后再修改上一条 bind”的时序
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`
    - `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindingCoverageManifestTests.cpp`
- [ ] **P3.1** 📦 Git 提交：`[AngelscriptRuntime] Refactor: add binding coverage manifest and provider ownership`
- [ ] **P3.1-T** 单元测试：补齐 manifest / provider ownership 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindingCoverageManifestTests.cpp`
  - 测试场景：
    - 正常路径：manifest 能同时枚举 `Bind_UObject`、`Bind_AActor`、`Bind_BlueprintType` 的 manual / reflective / bind-db / UHT 来源，provider 信息可读且排序不变
    - 边界条件：legacy `FBind` 未显式声明 provider 时仍能落到 default provider；旧 `BindModules.Cache` 缺失或回退路径仍能产出一致 manifest
    - 错误路径：冲突/缺失/skip 的符号必须记录 `RegistrationSource/ReasonCode`，不再只剩 `bEnabled` 或 silent 缺席
  - 测试命名：`Angelscript.TestModule.Core.BindingCoverageManifest`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`
- [ ] **P3.1-T** 📦 Git 提交：`[AngelscriptTest] Test: cover binding coverage manifest and provider ownership`

## 单元测试总览

| 改进项 | 测试文件 | 测试重点 | 优先级 |
|--------|---------|---------|--------|
| `P1.1` | `Bindings/AngelscriptWorldBindingsTests.cpp`，`Bindings/AngelscriptActorBindingsTests.cpp` | world-context 解析、spawn 审核、`FinishSpawningActor` 错误路径 | P0 |
| `P1.2` | `Bindings/AngelscriptContainerBindingsTests.cpp` | `Reserve`、`SetNum`、`RemoveSwap`、self-alias 防线 | P0 |
| `P1.3` | `Bindings/AngelscriptEnhancedInputBindingsTests.cpp` | `opMulAssign`、EnhancedInput `const` 合同、debug key execute 签名 | P0 |
| `P2.1` | `Bindings/AngelscriptWorldBindingsTests.cpp`，`Core/AngelscriptBindConfigTests.cpp` | `__WorldContext` public contract、world-context traits、construction safety / `no_discard` | P1 |
| `P2.2` | `Bindings/AngelscriptClassBindingsTests.cpp`，`Core/AngelscriptBindConfigTests.cpp` | lookup consistency、tombstone 过滤、`StaticClassDeprecation` 对齐 | P1 |
| `P3.1` | `Core/AngelscriptBindingCoverageManifestTests.cpp` | provider ownership、lane/provenance、reason ledger | P1 |

## 验收标准

1. `GetCurrentWorld()` 与三条 `SpawnActor*` 入口在 `GEngine == nullptr` 或 invalid world-context 下不再崩溃，并且 `VerifySpawnActor` 的 allow/deny 结果可被自动化稳定观测。
2. `TArray` 的 `Reserve()`、`SetNum()`、`RemoveSwap()`、self-alias 行为与计划中的回归断言一致，不再出现 silent 数据破坏。
3. `Bind_FInputActionValue.cpp`、`Bind_UEnhancedInputComponent.cpp`、`Bind_FInputBindingHandle.cpp` 的公开签名与 UE 原生头一致，新增 EnhancedInput 回归全部通过。
4. `__WorldContext` 恢复为 canonical public contract，`CallableWithoutWorldContext`、construction safety、`no_discard` 至少在高风险 helper 上重新闭合成可测试的统一行为。
5. namespace/global `FindClass`、`__StaticClass`、`GetAllClasses`、`GetAllSubclassesOf` 对 visible/deprecated/tombstone class 的判定一致，不再依赖共享引擎残留状态给出误导性绿灯。
6. `BindingCoverageManifest` 能给出 `ProviderName/RegistrationSource/BindingLane/ReasonCode` 级别的最小 provenance，并由自动化锁住，不再只能靠 grep 推断。

## 风险与注意事项

### 风险

1. **兼容性收紧会让历史误用提前暴露**
   - `FinishSpawningActor(nullptr)`、`const UEnhancedInputComponent.ClearActionBindings()`、依赖 bug 的 `TArray::Reserve()`/`RemoveSwap()` 用法修复后都会开始失败；这是预期中的正确性提升，但需要测试与文档同步说明。

2. **world-context / metadata contract 牵涉 public surface**
   - `__WorldContext` 从函数兼容回 canonical 变量后，短期会处于双轨阶段；必须用 deprecated 提示和自动化限制双轨长期固化。

3. **manifest/provenance 项不能影响既有 bind 顺序**
   - `P3.1` 只能先做 report-only 与 default-provider 兼容层；如果 provider ownership 或 metadata descriptor 改动了旧排序，风险会扩大到全仓绑定初始化。

4. **class lookup 修复依赖 hot-reload/test fixture 配合**
   - tombstone 过滤和 `StaticClassDeprecation` 旁路修复必须配套 focused regression；否则很容易只修 happy path，仍让共享引擎残留状态掩盖问题。

### 已知行为变化

1. **`Bind_AActor.cpp`**
   - `FinishSpawningActor(nullptr)` 将从 silent no-op 变成显式脚本错误。
   - `SpawnActor*` 在审核 delegate 返回 `false` 时会稳定返回 `null`，不再默认继续 spawn。

2. **`Bind_TArray.cpp`**
   - `Reserve()` 不再清空数组；错误依赖 current bug 的脚本会表现改变。
   - `RemoveSwap()` 的返回值将改为真实删除数量。
   - self-alias 调用将从“不稳定结果”变成固定脚本错误。

3. **`Bind_UEnhancedInputComponent.cpp` / `Bind_FInputActionValue.cpp`**
   - `const UEnhancedInputComponent` 对四个清理接口的调用会开始编译失败。
   - `opMulAssign` 链式表达式会按引用返回语义继续回写原对象。

4. **`Bind_UWorld.cpp` / `Helper_FunctionSignature.h`**
   - `__WorldContext()` 将降级为兼容别名；canonical script surface 回到 `__WorldContext`。
   - `CallableWithoutWorldContext` 将重新影响 trait 写回，不再与 `OptionalWorldContext` 分裂。

5. **`Bind_UObject.cpp`**
   - `__StaticClass` 的可见性将与 `StaticClassDeprecation` 同步；部分模式下可能不再公开。
   - lookup helper 将隐藏 tombstone / deprecated / 被策略禁用的类，不再“命中即返回”。

---
## 本轮补充（2026-04-09）

以下补充只承接当前 `Plan_BindSystem.md` 尚未覆盖的 correctness / contract gap，并显式避开已由其它计划承接的主题：

- `Documents/Plans/Plan_HazelightBindModuleMigration.md` 已承接 `DeprecateOldActorGenericMethods`、`GetComponentsByClassWithTag()`、`VerifySpawnActor`，本轮不重复列项。
- `Documents/Plans/Plan_CppInterfaceBinding.md` 与 `Documents/Plans/Plan_InterfaceBinding.md` 已承接 C++ `UInterface` 主线，本轮不把 interface 自动绑定与 property bridge 再写进 `BindSystem`。
- 已归档 `Documents/Plans/Archives/Plan_UFunctionReflectiveFallbackBinding.md` 已承接 reflective fallback 主线，本轮只处理 delegate / input / world / debug formatting 的公开 contract 漂移。

### Phase 1 增补：补上尚未纳入的横切崩溃路径

- [ ] **P1.4** 收口 native `UObject` 调试字符串与 debugger value 的 `UASClass*` 裸解引用
  - 当前 `Bind_UObject.cpp`、`Bind_FString.cpp`、`Bind_BlueprintType.cpp` 把“是否 script class”的显示逻辑复制成了三套：一套会在 native `UClass` 上直接解引用空 `UASClass*`，一套会在 native object 上静默提前 `return`，另一套还会继续读 `ScriptTypePtr`。这已经不是单个 helper 的实现细节，而是 `Log` / `Print` / debugger value 三条横切 surface 共用的稳定性缺口。
  - 这一项应先抽统一的 object-debug helper，明确“native `UClass` 走 `Class->GetPrefixCPP()`，只有 `Cast<UASClass>` 成功时才访问 `bIsScriptClass` / `ScriptTypePtr`”。随后统一替换 `Bind_UObject.cpp`、`Bind_FString.cpp`、`Bind_BlueprintType.cpp` 的重复分支，不再允许在 ternary 条件或真分支里再次直接读取 `asClass->...`。
  - 这里不扩 debugger 新功能，只把当前会在 native object 调试输出时崩溃或静默丢字串的路径收口成统一、安全且可回归的 contract。
  - 来源：
    - [A] `BindSystem_Analysis.md` — “发现 6：`FToStringHelper::Register(TEXT("UObject"), ...)` 在 native `UClass` 上会通过空 `UASClass*` 访问 `GetPrefixCPP()` / `bIsScriptClass`”
    - [B] `DiscoveryPlans/BindSystem_Plan.md` — “Issue-10：`Bind_UObject.cpp` / `Bind_FString.cpp` / `Bind_BlueprintType.cpp` 需要统一的 class-display helper，避免 object debug formatting 横向复制同类 bug”
    - [E] `ReferenceComparison/Hazelight_Analysis.md` — “Hazelight 同位点始终先判空 `asClass != nullptr` 再访问脚本类字段，不把 native `UClass` 判定建立在空 `UASClass*` 上”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp` L231-L260 — `UASClass* asClass = Cast<UASClass>(ObjClass)` 后，actor/non-actor 两个格式化分支都直接把 `asClass->bIsScriptClass` / `asClass->GetPrefixCPP()` 放进 ternary
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FString.cpp` L434-L467 — native object 一旦 `Cast<UASClass>(ObjClass)` 失败就提前 `return`，导致对象字符串拼接静默丢失，而非回退到 `UClass::GetPrefixCPP()` 路径
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp` L323-L345 — debugger value 同时在 prefix 选择与 `ScriptTypePtr` 读取上直接依赖 `asClass`
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_ObjectDebugHelpers.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FString.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptObjectDebugFormattingTests.cpp`
- [ ] **P1.4** 📦 Git 提交：`[AngelscriptRuntime] Fix: harden object debug formatting for native classes`
- [ ] **P1.4-T** 单元测试：补齐 native/script object 调试格式化安全回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptObjectDebugFormattingTests.cpp`
  - 测试场景：
    - 正常路径：script class 实例参与 `ToString` / debugger value 构建时，前缀、类名和 `bHasMembers` 与当前脚本类预期保持一致
    - 边界条件：native `AActor::StaticClass()->GetDefaultObject()`、native 非 actor `UObject`、空指针 `nullptr` 三类输入都能稳定格式化，不再静默提前返回或丢掉类型前缀
    - 错误路径：模拟 native object 走到 `FToStringHelper::Generic_AppendToString()` 与 debugger value 逻辑时，不允许再出现空 `UASClass*` 解引用或空字符串吞掉对象信息
  - 测试命名：`Angelscript.TestModule.Bindings.ObjectDebugFormattingSafety`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`
- [ ] **P1.4-T** 📦 Git 提交：`[AngelscriptTest] Test: cover native object debug formatting safety`

### Phase 2 增补：继续收口 input / world / delegate 的公开 contract

- [ ] **P2.3** 统一 `Bind_InputEvents.cpp` 的 canonical virtual-key surface 与 value-factory `no_discard` 合同
  - 当前 `Bind_InputEvents.cpp` 的问题已经不是单点漏绑，而是同一文件里同时存在两类 contract 漂移：一类是 `FKey::GetVirtualKey()`、`Virtual_Gamepad_*` canonical path 被回退成两个硬编码别名；另一类是 `FKey` / `FInputChord` / `FEventReply` 的 value-factory 丢掉 `no_discard`，让“看起来像在做事”的输入表达式可以被静默忽略。
  - 这一项应把两类问题一起收口：恢复 `FKey GetVirtualKey() const`、恢复 `Virtual_Gamepad_Accept` / `Virtual_Gamepad_Back` 常量，把 `Virtual_Accept` / `Virtual_Back` 收敛成兼容 property wrapper；同时为 `FKey(const FName&)`、`FInputChord(...)`、`FEventReply::Handled()` / `Unhandled()` 补回 `asTRAIT_NODISCARD`。
  - 这里不扩额外输入 API，只把当前已经存在但语义漂移的 canonical path 和返回值契约拉回 UEAS2 / UE5.7 推荐用法，避免输入归一化和 Slate reply contract 继续分叉。
  - 来源：
    - [A] `BindSystem_Analysis.md` — “发现 72/73：`FKey::GetVirtualKey()` 缺失，`FKey` / `FInputChord` / `FEventReply` value-factory 丢失 `no_discard`”
    - [B] `DiscoveryPlans/BindSystem_Plan.md` — “Issue-34/70：恢复 `GetVirtualKey()` / `Virtual_Gamepad_*`，并用 trait 断言 + compile-summary 锁住输入值工厂的 `no_discard`”
    - [C] `BindSystem_TestGaps.md` — “`Bind_InputEvents.cpp` 位于当前未见直接对应测试清单，现有测试没有命中 `GetVirtualKey` / `Handled()` / `FInputChord`”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_InputEvents.cpp` L17-L20 — `FKey(const FName&)` constructor 后没有 `SetPreviousBindNoDiscard(true)`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_InputEvents.cpp` L28-L37 — `FKey` methods 只到 `GetDisplayName()` / `GetKeyName()`，没有 `GetVirtualKey()`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_InputEvents.cpp` L51-L59 — 两个 `FInputChord` constructor 同样缺 `no_discard`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_InputEvents.cpp` L219-L224 — `FEventReply::Handled()` / `Unhandled()` 仍是普通返回值函数
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_InputEvents.cpp` L519-L522 — 仍只公开 `Virtual_Accept` / `Virtual_Back` 常量别名，没有 canonical `Virtual_Gamepad_*` surface
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_InputEvents.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptInputBindingsTests.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptControlFlowTests.cpp`
- [ ] **P2.3** 📦 Git 提交：`[AngelscriptRuntime] Fix: restore input virtual-key and no-discard contracts`
- [ ] **P2.3-T** 单元测试：补齐 input virtual-key / `no_discard` 双向回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptInputBindingsTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptControlFlowTests.cpp`
  - 测试场景：
    - 正常路径：`EKeys::Virtual_Gamepad_Accept.GetVirtualKey()`、`EKeys::SpaceBar.GetVirtualKey()`、`GetVirtual_Accept()` / `GetVirtual_Back()` 都可编译执行且结果一致；消费 `FEventReply::Handled()` 返回值时行为保持正常
    - 边界条件：`Virtual_Accept` / `Virtual_Back` 兼容入口与 canonical `Virtual_Gamepad_*` 返回同一 key；`FInputChord` 两个 constructor 都带 `asTRAIT_NODISCARD`
    - 错误路径：忽略 `FKey(n"SpaceBar")`、`FInputChord(EKeys::Enter)`、`FEventReply::Handled()` 的返回值必须产生 `no_discard` diagnostics，而不是静默通过
  - 测试命名：`Angelscript.TestModule.Bindings.InputVirtualKeyAndNoDiscard`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + compile-summary harness
- [ ] **P2.3-T** 📦 Git 提交：`[AngelscriptTest] Test: cover input virtual-key and no-discard regressions`

- [ ] **P2.4** 收紧 `Bind_UWorld.cpp` 对 engine-owned 世界状态的写入口
  - 当前 `Bind_UWorld.cpp` 把 `WorldType`、`OwningGameInstance`、`GFrameNumber` 这三类本应只读或 engine-owned 的状态同时暴露给脚本写入：`WorldType` 走默认可写 property、`SetGameInstance()` 直接改 `OwningGameInstance`、`GFrameNumber` 则作为普通全局变量注册。它们共同构成了“脚本可越权改写世界基础状态”的入口族。
  - 这一项应把三条入口一起收口：`WorldType` 改成显式只读 property；普通脚本 surface 删除 `SetGameInstance()`，如确需 escape hatch 则降级为 `__Internal_*`；`GFrameNumber` 改成 `const uint` 只读全局，并在 bind 注释中明确 `BindGlobalVariable()` 不会自动推断只读语义。
  - 这里不改变 `GetGameInstance()`、`IsGameWorld()`、`GetCurrentWorld()` 的读取用法，只切断“写 world 根状态”的 surface，避免后续继续把 engine-owned 字段当普通 gameplay setter 暴露。
  - 来源：
    - [A] `BindSystem_Analysis.md` — “发现 64/77/78：`GFrameNumber` 可写、`WorldType` 可写、`SetGameInstance()` 直接公开”
    - [B] `DiscoveryPlans/BindSystem_Plan.md` — “Issue-17/58：收紧 `UWorld` engine-owned 写入口与 `GFrameNumber` 的只读语义”
    - [C] `BindSystem_TestGaps.md` — “NewTest-28：`Bind_UWorld.cpp` 目前只覆盖 world 读取 happy-path；若采纳只读修复，应补 `GFrameNumber = 123;` 编译失败断言”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UWorld.cpp` L73-L76 — 公开 `void SetGameInstance(UGameInstance NewGI)`，没有 internal-only 或 deprecation 边界
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UWorld.cpp` L87 — `WorldType` 仍以默认 `Property(...)` 注册，没有只读 `BindParams`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UWorld.cpp` L89 — `GFrameNumber` 仍以 `uint` 普通全局变量暴露
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UWorld.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptWorldBindingsTests.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptControlFlowTests.cpp`
- [ ] **P2.4** 📦 Git 提交：`[AngelscriptRuntime] Fix: guard engine-owned world state from script writes`
- [ ] **P2.4-T** 单元测试：补齐 world engine-owned 状态的只读/禁写回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptWorldBindingsTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptControlFlowTests.cpp`
  - 测试场景：
    - 正常路径：`WorldType`、`GetGameInstance()`、`GFrameNumber` 仍可读取，并与 C++ 同一时刻基线一致
    - 边界条件：`WorldType` 通过 property 或 getter 读取时依旧保持现有枚举值；若保留 `__Internal_SetGameInstance`，只能通过内部名字访问而不出现在普通 surface
    - 错误路径：`GetCurrentWorld().WorldType = EWorldType::Editor;`、`GFrameNumber = 123;`、普通脚本调用 `SetGameInstance(...)` 都必须编译失败或无法解析
  - 测试命名：`Angelscript.TestModule.Bindings.WorldStateWriteGuards`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_FULL()` + `FScopedTestWorldContextScope`
- [ ] **P2.4-T** 📦 Git 提交：`[AngelscriptTest] Test: cover world state write guards`

- [ ] **P2.5** 把 `ULevel::GetActors()` 从 backing array 改成稳定结果集
  - 当前公开 `GetActors()` 直接把 `Level->Actors` 的内部稀疏数组按引用暴露给脚本，这让普通脚本必须理解 Unreal 底层的 null-slot 语义，才能避免在遍历 `PersistentLevel.GetActors()` 时撞上 `nullptr` 句柄。对于公开脚本 surface 来说，这已经不是“性能取舍”，而是“把引擎内部 backing store 误当成稳定列表”。
  - 这一项应把公开 `GetActors()` 改为返回过滤后的 `TArray<AActor>` 副本，只保留有效 actor；如果团队仍需要调试 backing array，则单独新增显式 `__Internal_GetActorsRaw()`，把 raw 语义与普通遍历语义分开。
  - 这里不做更大范围的 level helper 扩面，只把当前已经暴露但语义过底层的一个入口收回到脚本能直接消费的 contract。
  - 来源：
    - [A] `BindSystem_Analysis.md` — “发现 67：`ULevel::GetActors()` 把 `Level->Actors` 稀疏数组原样暴露给脚本，结果集可能包含 `nullptr` 槽位”
    - [B] `DiscoveryPlans/BindSystem_Plan.md` — “Issue-73：公开 `GetActors()` 应返回过滤后的稳定结果集，raw backing array 需要显式 internal 入口”
    - [C] `BindSystem_TestGaps.md` — “`Bind_UWorld.cpp` 当前没有直接 world list 遍历回归，现有 happy-path 只覆盖 `GetActors().Num()`”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UWorld.cpp` L107-L109 — `ULevel::GetActors()` 仍直接 `return Level->Actors`
    - `Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemScenarioTests.cpp` L180 — 现有用例只把 `GetActors().Num()` 当 happy-path，没有遍历 null-slot 或过滤契约回归
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UWorld.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptWorldBindingsTests.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemScenarioTests.cpp`
- [ ] **P2.5** 📦 Git 提交：`[AngelscriptRuntime] Fix: expose filtered level actor snapshots to script`
- [ ] **P2.5-T** 单元测试：补齐 level actor list 的稳定结果集回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptWorldBindingsTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemScenarioTests.cpp`
  - 测试场景：
    - 正常路径：`GetPersistentLevel().GetActors()` 返回的列表只包含当前 level 的有效 actor，脚本可直接遍历并读取名称
    - 边界条件：当底层 `Level->Actors` 中存在空槽位或被延迟清理的条目时，公开结果仍保持顺序稳定且不包含 `null`
    - 错误路径：脚本不再需要针对公开 `GetActors()` 额外写 `if (Actor is null)` 过滤；若保留 `__Internal_GetActorsRaw()`，只有 internal 名字才暴露 raw 语义
  - 测试命名：`Angelscript.TestModule.Bindings.LevelActorListSnapshot`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_FULL()` + `FScopedTestWorldContextScope`
- [ ] **P2.5-T** 📦 Git 提交：`[AngelscriptTest] Test: cover filtered level actor snapshots`

- [ ] **P2.6** 把 delegate 三参数 raw helper 收回 `__Internal_*` 命名面，并补回 constructor `no_discard`
  - 当前 delegate 绑定面把两参数公开 wrapper 和三参数 codegen 协议 helper 混在同一 public 名字下：脚本作者能直接看到并调用 `BindUFunction(..., UDelegateFunction Signature)` / `AddUFunction(..., UDelegateFunction Signature)`，而 preprocessor 生成代码也仍在调用这些公开名字。这让 codegen internal protocol 与用户 API 继续耦在一起。
  - 这一项应把三参数 raw helper 统一改名为 `__Internal_BindUFunction` / `__Internal_AddUFunction`，并同步修改 `AngelscriptPreprocessor.cpp` 生成的 wrapper 只调用 internal 名字；同时在三参数 constructor 注册后补回 `SetPreviousBindNoDiscard(true)`，把“构造出来的 delegate 值必须被消费”的契约重新锁住。
  - 这里不改变两参数 `BindUFunction` / `AddUFunction` 的用户体验，只把 internal bridge 从 public surface 藏回去，并避免 delegate constructor 结果再次被静默忽略。
  - 来源：
    - [A] `BindSystem_Analysis.md` — “发现 83：delegate raw helper 继续以公开名字暴露，codegen 协议和用户 API 耦在一起”
    - [B] `DiscoveryPlans/BindSystem_Plan.md` — “Issue-33：三参数 raw helper 应迁到 `__Internal_*`，并补回 constructor 的 `no_discard` 契约”
    - [C] `BindSystem_TestGaps.md` — “NewTest-2 / NewTest-7：现有 delegate 测试只覆盖 happy-path，未覆盖 signature overload、`__DelegateSignature()` 与 null error path”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.cpp` L1432-L1439 — `_FMulticastScriptDelegate` / `_FScriptDelegate` 仍公开三参数 `AddUFunction` / `BindUFunction` 与对应 constructor
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L665-L665、L710-L714 — 生成代码仍直接调用 `_Inner.AddUFunction(...)` / `_Inner.BindUFunction(...)`，而不是 internal helper 名字
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Delegate/AngelscriptDelegateScenarioTests.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptControlFlowTests.cpp`
- [ ] **P2.6** 📦 Git 提交：`[AngelscriptRuntime] Refactor: internalize delegate raw helpers and restore constructor no-discard`
- [ ] **P2.6-T** 单元测试：补齐 delegate internal helper / constructor contract 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Delegate/AngelscriptDelegateScenarioTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptControlFlowTests.cpp`
  - 测试场景：
    - 正常路径：两参数 `BindUFunction` / `AddUFunction` wrapper 继续能绑定、执行、广播和解绑，`__DelegateSignature(this)` 生成路径不回退
    - 边界条件：通过 signature overload 的 internal helper 完成 bind/add 时，`null` object / `null` signature 仍给出既有错误文本，delegate 状态不被污染
    - 错误路径：普通脚本不再以公开名字解析到三参数 raw helper；忽略 delegate constructor 结果时必须出现 `no_discard` diagnostics
  - 测试命名：`Angelscript.TestModule.Bindings.DelegateInternalHelperSurface`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + compile-summary harness
- [ ] **P2.6-T** 📦 Git 提交：`[AngelscriptTest] Test: cover delegate internal helper surface and constructor contract`

### 单元测试总览增补

| 改进项 | 测试文件 | 测试重点 | 优先级 |
|--------|---------|---------|--------|
| `P1.4` | `Bindings/AngelscriptObjectDebugFormattingTests.cpp` | native/script object 调试字符串、debugger value、不崩溃 | P0 |
| `P2.3` | `Bindings/AngelscriptInputBindingsTests.cpp`，`Core/AngelscriptBindConfigTests.cpp`，`Angelscript/AngelscriptControlFlowTests.cpp` | `GetVirtualKey()`、`Virtual_Gamepad_*`、输入值工厂 `no_discard` | P1 |
| `P2.4` | `Bindings/AngelscriptWorldBindingsTests.cpp`，`Core/AngelscriptBindConfigTests.cpp`，`Angelscript/AngelscriptControlFlowTests.cpp` | `WorldType` / `GFrameNumber` 只读、`SetGameInstance()` 禁写 | P1 |
| `P2.5` | `Bindings/AngelscriptWorldBindingsTests.cpp`，`Subsystem/AngelscriptSubsystemScenarioTests.cpp` | `GetActors()` 过滤 null-slot、稳定结果集 | P1 |
| `P2.6` | `Bindings/AngelscriptFileAndDelegateBindingsTests.cpp`，`Delegate/AngelscriptDelegateScenarioTests.cpp`，`Angelscript/AngelscriptControlFlowTests.cpp` | delegate internal helper 命名面、constructor `no_discard` | P1 |

---

## 深化 (2026-04-09 00:59)

本轮只追加现有 `Plan_BindSystem.md` 尚未承接、且已确认不与 `Documents/Plans/Plan_UEBindGapRoadmap.md` 的 API 扩面主线、`Documents/Plans/Plan_HazelightBindModuleMigration.md` 的 Actor/SceneComponent 迁移主线重复的剩余 correctness 条目。`Plan_TestCoverageExpansion.md` 虽提到 `Bind_Subsystems.cpp` / `Bind_WorldCollision.cpp` 的测试扩面，但不覆盖这里的运行时 guard 与 contract 修复，因此本轮仍保留 focused bind 任务。

### Phase 1 再增补：补齐 literal asset / collision / subsystem / soft reference 的剩余崩溃面

- [ ] **P1.5** 将 literal asset 两段 helper 收回 internal 命名面，并补齐空参防线
  - 当前 `__CreateLiteralAsset` / `__PostLiteralAssetSetup` 仍以普通 global bind 暴露，而 preprocessor 也直接生成对这两个公开名字的调用。它们本质上是 codegen 协议 helper，却分别把 `AssetClass` 与 `Asset` 当成“调用方总会传入”的隐式前提，继续保留会让 literal asset 初始化路径维持一条任何脚本都能误用的 crash surface。
  - 这一项先不改变 literal asset 的软重载语义，只收紧 helper contract：把 `__CreateLiteralAsset` / `__PostLiteralAssetSetup` 迁到 `__Internal_*` 命名面，并明确保留一轮旧名字薄 wrapper 作为兼容层；两个入口都要在首行做 null guard，并与同文件 `NewObject(...)` 的 class-null 诊断保持一致，避免 literal asset 再维护第二套错误语言体系。
  - 来源：
    - [A] `BindSystem_Analysis.md` — “发现 26/29：`__CreateLiteralAsset` / `__PostLiteralAssetSetup` 作为公开 bind 暴露，但缺少空参数保护”
    - [B] `DiscoveryPlans/BindSystem_Plan.md` — “Issue-35/67：literal asset helper 应迁回 internal 名字，并补齐 class-null / asset-null 防线”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp` L606-L687 — `__CreateLiteralAsset()` 入口没有 `AssetClass == nullptr` 检查，后续直接进入 `IsA(AssetClass)`、`AssetClass->GetName()` 与 `AssetClass->GetDefaultObject()`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp` L689-L697 — `__PostLiteralAssetSetup()` 直接 `Asset->GetName()`，没有任何空值收口
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L4116-L4119 — 生成模板仍直接调用公开名字 `__CreateLiteralAsset()` / `__PostLiteralAssetSetup()`
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptLiteralAssetBindingsTests.cpp`
- [ ] **P1.5** 📦 Git 提交：`[AngelscriptRuntime] Fix: internalize literal asset helpers and guard null inputs`
- [ ] **P1.5-T** 单元测试：补齐 literal asset helper internal surface 与空参防线回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptLiteralAssetBindingsTests.cpp`
  - 测试场景：
    - 正常路径：preprocessor 生成的 asset getter 继续能创建资产，并触发 `PostLiteralAssetSetup` 对应广播/后处理
    - 边界条件：保留兼容 wrapper 的情况下，旧公开名字与新的 `__Internal_*` 名字都指向同一资产实例，不会重复创建或重复广播
    - 错误路径：向 `__CreateLiteralAsset(nullptr, ...)` 与 `__PostLiteralAssetSetup(nullptr, ...)` 传空参数时必须记录固定脚本错误，而不是崩溃
  - 测试命名：`Angelscript.TestModule.Bindings.LiteralAssetHelperGuards`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`
- [ ] **P1.5-T** 📦 Git 提交：`[AngelscriptTest] Test: cover literal asset helper guards and internal entry points`

- [ ] **P1.6** 恢复 literal asset 软重载的 script-object 重建与 instanced property 所有权保护
  - 当前 literal asset 软重载只执行 `FProperty` 级别的 `CopyCompleteValue_InContainer()`，没有重新跑脚本对象的 constructor/defaults 链，也没有像 UEAS2 那样跳过 `ContainsInstancedObjectProperty()`。结果不是单点行为差异，而是“脚本态会跨版本残留”与“instanced subobject 可能被 CDO 覆盖”同时存在。
  - 这一项应把 literal asset 软重载重新收口成“先重建 script object，再恢复可安全按值复制的属性”这条 contract：为 current `UASClass` 补回一个仅供 reload 使用的 `ReconstructScriptObject()` 入口，先执行 destructor / constructor / defaults，再在属性恢复循环里显式跳过 instanced property，避免把对象所有权问题继续留给 reload 后的业务代码兜底。
  - 来源：
    - [A] `BindSystem_Analysis.md` — “发现 1/2：literal asset 软重载不再重建 script object，且会无条件覆盖 instanced subobject 属性”
    - [B] `DiscoveryPlans/BindSystem_Plan.md` — “Issue-38/39：恢复 `ReconstructScriptObject()` 语义，并在 reset 路径跳过 `ContainsInstancedObjectProperty()`”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp` L674-L681 — soft-reload 分支只遍历 `FProperty` 并无条件 `CopyCompleteValue_InContainer()`，没有任何 script-object 重建或 instanced-property 过滤
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h` L82-L105 — 当前生命周期 API 只有 `RuntimeDestroyObject` / constructor helpers，未见 reload 可调用的 `ReconstructScriptObject()` 入口
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` L1396-L1405 — `ExecuteConstructFunction()` / `ExecuteDefaultsFunctions()` 只在 constructor path 调用，literal asset reload 路径没有接回这条链
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptLiteralAssetBindingsTests.cpp`
- [ ] **P1.6** 📦 Git 提交：`[AngelscriptRuntime] Fix: reconstruct literal assets safely on soft reload`
- [ ] **P1.6-T** 单元测试：补齐 literal asset 软重载生命周期与 instanced 所有权回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptLiteralAssetBindingsTests.cpp`
  - 测试场景：
    - 正常路径：同名 script literal asset 第二次 `__CreateLiteralAsset()` 后会重新执行 script constructor/defaults，脚本态不再保留第一次运行留下的残值
    - 边界条件：带 `Instanced` 子对象属性的测试 asset 在 soft reload 后保持子对象实例身份不变，而普通按值属性仍恢复为 CDO 默认值
    - 错误路径：当 `ScriptTypePtr == nullptr` 或 script reload 夹具刻意制造 stale class 时，重建 helper 必须稳定早退或走安全回退，不得继续解引用无效脚本类型
  - 测试命名：`Angelscript.TestModule.Bindings.LiteralAssetReloadContracts`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`
- [ ] **P1.6-T** 📦 Git 提交：`[AngelscriptTest] Test: cover literal asset reload reconstruction and instanced ownership`

- [ ] **P1.7** 为 `Bind_WorldCollision.cpp` 整组 trace/overlap helper 收口统一的 world-context guard 与 trait
  - `Bind_WorldCollision.cpp` 当前把 world 解析抽成 `WorldCollision::GetWorld()`，但这层 helper 只做 `LogAndReturnNull`，调用点仍然一律写成 `WorldCollision::GetWorld()->...`。结果是 46 个同步/异步 trace、sweep、overlap、query helper 共用同一条空 world 解引用入口，同时整张 bind surface 还没有把 `asTRAIT_USES_WORLDCONTEXT` 写回去。
  - 这一项应一次性收口整张 collision bind 面，而不是只修某一个 helper：抽统一 `ResolveWorldOrThrow()`，把 `GEngine == nullptr`、invalid world context、默认输出值、无效 `FTraceHandle` 统一成稳定 contract；同时在注册后补 `SetPreviousBindRequiresWorldContext(true)`，让现有编译期/运行期护栏重新生效。
  - 来源：
    - [A] `BindSystem_Analysis.md` — “发现 42/45：`Bind_WorldCollision.cpp` 整组 helper 对无效 world-context 直接空指针解引用，且整块缺 `RequiresWorldContext` trait”
    - [B] `DiscoveryPlans/BindSystem_Plan.md` — “Issue-22/27：`Bind_WorldCollision` 需要统一 `ResolveWorldOrThrow()` 并补回 world-context trait”
    - [C] `BindSystem_TestGaps.md` — “NewTest-3/4：`Bind_WorldCollision.cpp` 需要同步/异步行为测试，当前只有 compile smoke”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_WorldCollision.cpp` L14-L17 — `WorldCollision::GetWorld()` 直接 `GEngine->GetWorldFromContextObject(..., LogAndReturnNull)`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_WorldCollision.cpp` L149-L467 — `LineTrace*` / `Sweep*` / `Overlap*` / `Async*` / `Query*` / `IsTraceHandleValid` 全部直接调用 `WorldCollision::GetWorld()->...`
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_WorldCollision.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_WorldContextHelpers.h`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptWorldCollisionBindingsTests.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp`
- [ ] **P1.7** 📦 Git 提交：`[AngelscriptRuntime] Fix: guard world collision binds against invalid world context`
- [ ] **P1.7-T** 单元测试：补齐 world collision 的 invalid-world 与 trait 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptWorldCollisionBindingsTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp`
  - 测试场景：
    - 正常路径：有效 world context 下 `LineTrace*` / `Overlap*` / `AsyncOverlapByProfile` 继续返回与原生 world 查询一致的结果
    - 边界条件：invalid world context 时同步 helper 返回 `false`、异步 helper 返回无效 `FTraceHandle`，并把 `OutHit` / `OutHits` / `OutOverlaps` / `OutData` 重置为默认值
    - 错误路径：`GEngine == nullptr` 或 world context 缺失时只记录 `"Engine was null."` / `"Invalid World Context"`，不再通过 `WorldCollision::GetWorld()->...` 崩溃
  - 测试命名：`Angelscript.TestModule.Bindings.WorldCollisionWorldContextGuards`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_FULL()` + `FScopedTestWorldContextScope`
- [ ] **P1.7-T** 📦 Git 提交：`[AngelscriptTest] Test: cover world collision world-context guards`

- [ ] **P1.8** 收口 `Bind_Subsystems.cpp` 的 `Class::Get()` lifecycle guard，并把失败路径改成脚本错误
  - `Bind_Subsystems.cpp` 当前对 `UEditorSubsystem`、`UEngineSubsystem`、`UGameInstanceSubsystem`、`UWorldSubsystem` 的 `ClassName::Get()` 都直接解引用 `GEditor` / `GEngine` 或继续使用裸 `GetWorldFromContextObject()` 结果。它们在 API 形态上像是稳定 accessor，但实际首个失败点却是全局单例为空时的原生崩溃。
  - 这一项要把 subsystem `Get()` 入口拉回和 `Bind_UWorld.cpp` / `Bind_AActor.cpp` 一致的失败语义：统一经由 engine/editor/world helper 解析上下文，失败时返回 `null` 并记录明确脚本错误；同时补回 `no_discard` 与 `editor-only`/world-context metadata，避免修完崩溃后仍保留“返回值可被静默忽略”“editor subsystem 在脚本里无 trait”的次级回归面。
  - 来源：
    - [B] `DiscoveryPlans/BindSystem_Plan.md` — “Issue-74：`Bind_Subsystems.cpp` 的 `Class::Get()` accessor 在生命周期边界直接解引用 `GEngine` / `GEditor`”
    - [C] `BindSystem_TestGaps.md` — “`Bind_Subsystems.cpp` 位于当前未见 direct-hit bind 清单，缺少 dedicated 行为级回归”
    - [B] `DiscoveryPlans/BindSystem_Plan.md` — “Issue-53：editor-only hand-written bind（包含 subsystem `Get()`）仍未把 trait 写回脚本类型系统”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Subsystems.cpp` L43-L52 — editor subsystem `Get()` 直接 `GEditor->GetEditorSubsystemBase(SubsystemClass)`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Subsystems.cpp` L58-L94 — engine/game instance/world subsystem `Get()` 直接使用 `GEngine->GetEngineSubsystemBase(...)` 或 `GEngine->GetWorldFromContextObject(...)`
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Subsystems.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_WorldContextHelpers.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptSubsystemBindingsTests.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp`
- [ ] **P1.8** 📦 Git 提交：`[AngelscriptRuntime] Fix: harden subsystem Get accessors across engine lifecycle`
- [ ] **P1.8-T** 单元测试：补齐 subsystem `Class::Get()` 生命周期与 trait 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptSubsystemBindingsTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp`
  - 测试场景：
    - 正常路径：有效 engine/editor/world 上下文下，`EngineSubsystem::Get()`、`WorldSubsystem::Get()`、`GameInstanceSubsystem::Get()` 返回与原生 `GetSubsystemBase()` 一致的对象
    - 边界条件：world context 存在但 `GameInstance == nullptr`、或 editor subsystem 在非 editor script context 下不可见时，返回值与 trait 行为保持可预测
    - 错误路径：`GEngine == nullptr`、`GEditor == nullptr`、invalid world context 时统一返回 `null` 并记录精确错误，而不是直接崩溃
  - 测试命名：`Angelscript.TestModule.Bindings.SubsystemGetLifecycleGuards`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_FULL()` + `FScopedTestWorldContextScope`
- [ ] **P1.8-T** 📦 Git 提交：`[AngelscriptTest] Test: cover subsystem Get lifecycle guards`

- [ ] **P1.9** 为 `TSoftObjectPtr::LoadAsync` 补齐空 soft reference / 空 package path 防线
  - 当前 `LoadAsync()` 只检查 subtype 是否是 actor/component，然后就把 `ObjectCopy.ToString()` 转成 package name 送进 `LoadPackageAsync()`；一旦 `Self` 本身是默认构造或 `Reset()` 后的空引用，这条路径会把空包名请求直接交给底层异步加载器。
  - 这一项应把 `LoadAsync` 的失败语义从“loader 日志或回调里的 `null`”改成确定的脚本 contract：先判 `Self == nullptr || Self->IsNull()`，再校验 `ObjectPathToPackageName()` 不为空；只有通过这两层 guard 后才允许进入异步加载分支，并把现有 actor/component 禁止加载规则保留在 null guard 之后。
  - 来源：
    - [A] `BindSystem_Analysis.md` — “发现 43：`TSoftObjectPtr::LoadAsync` 对空 soft reference 缺少前置校验，会把空路径直接送进 `LoadPackageAsync`”
    - [B] `DiscoveryPlans/BindSystem_Plan.md` — “Issue-75：`LoadAsync` 应先阻断空 soft reference，再校验 package path”
    - [C] `BindSystem_TestGaps.md` — “Issue-33/34：`SoftObjectPtrCompat` / `TSoftClassPtrCompat` 只覆盖已加载 happy path，没有触达 pending/path-only 语义”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSoftObjectPtr.cpp` L483-L526 — `LoadAsync()` 入口没有 `Self->IsNull()` 或空 package path 检查，最终直接 `LoadPackageAsync(*FPackageName::ObjectPathToPackageName(ObjectCopy.ToString()), ...)`
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSoftObjectPtr.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptSoftReferenceBindingsTests.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptObjectBindingsTests.cpp`
- [ ] **P1.9** 📦 Git 提交：`[AngelscriptRuntime] Fix: reject null soft references in TSoftObjectPtr LoadAsync`
- [ ] **P1.9-T** 单元测试：补齐 `TSoftObjectPtr::LoadAsync` 的 null / pending / loaded 三态回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptSoftReferenceBindingsTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptObjectBindingsTests.cpp`
  - 测试场景：
    - 正常路径：已加载对象的 soft pointer 调 `LoadAsync()` 时立即回调原对象，不回退现有 happy path
    - 边界条件：path-only 且 package path 非空的 soft reference 走异步分支时，`IsPending()` / `Get()` / callback 行为与 soft-reference 语义保持一致
    - 错误路径：默认构造或 `Reset()` 后的空 soft reference 调 `LoadAsync()` 时必须记录 `"Soft object reference was null."`，且不会发起空 package name 的异步加载请求
  - 测试命名：`Angelscript.TestModule.Bindings.SoftReferenceLoadAsyncGuards`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`
- [ ] **P1.9-T** 📦 Git 提交：`[AngelscriptTest] Test: cover TSoftObjectPtr LoadAsync null and pending guards`

### 单元测试总览增补（2026-04-09 00:59）

| 改进项 | 测试文件 | 测试重点 | 优先级 |
|--------|---------|---------|--------|
| `P1.5` | `Bindings/AngelscriptLiteralAssetBindingsTests.cpp` | helper internal surface、空 `AssetClass` / 空 `Asset` 防线 | P0 |
| `P1.6` | `Bindings/AngelscriptLiteralAssetBindingsTests.cpp` | literal asset 软重载重建、instanced property 所有权 | P0 |
| `P1.7` | `Bindings/AngelscriptWorldCollisionBindingsTests.cpp`，`Core/AngelscriptBindConfigTests.cpp` | invalid world-context 默认输出、`asTRAIT_USES_WORLDCONTEXT` | P0 |
| `P1.8` | `Bindings/AngelscriptSubsystemBindingsTests.cpp`，`Core/AngelscriptBindConfigTests.cpp` | subsystem `Get()` 生命周期 guard、editor/world metadata | P1 |
| `P1.9` | `Bindings/AngelscriptSoftReferenceBindingsTests.cpp`，`Bindings/AngelscriptObjectBindingsTests.cpp` | `LoadAsync` 空引用防线、pending/path-only 语义 | P1 |

### 风险与注意事项增补（2026-04-09 00:59）

#### 风险

1. literal asset helper 改成 `__Internal_*` 后，如果仓内已有脚本直接调用旧 helper 名字，会暴露编译期兼容性变化。
   - 缓解：保留一轮薄 wrapper，并在同批测试里锁住“旧名仅转发，不再承担真实实现”的过渡语义。
2. `Bind_WorldCollision.cpp` 与 `Bind_Subsystems.cpp` 收口 guard 后，失败形态会从“warning/null/崩溃”收紧成显式脚本错误，可能暴露历史上被当成普通空结果处理的误用。
   - 缓解：测试同时锁正常路径和负向路径，避免把 guard 修复变成新的 silent behavior drift。
3. `TSoftObjectPtr::LoadAsync` 增加空引用 guard 后，现有把空 soft reference 当成“静默失败”容错路径的脚本会在运行时看到新错误。
   - 缓解：在变更说明和回归测试里把“空引用”和“加载失败”区分成不同错误语义，避免脚本调用方继续依赖底层 loader 日志。

#### 已知行为变化

1. literal asset 初始化 helper 的公开命名面将收缩到 `__Internal_*`；若保留旧名字，也只作为兼容 wrapper，不再是推荐 public surface。
2. `WorldCollision` 与 subsystem `Get()` 入口在缺失上下文时会返回稳定默认值并记录脚本错误，不再沿用当前的 crash 或底层 warning 行为。
3. `TSoftObjectPtr::LoadAsync` 对空 soft reference 会立即报脚本错误，而不是继续把空路径送入 `LoadPackageAsync`。

---

## 深化 (2026-04-09 01:14:20)

本轮深化只补当前 `Plan_BindSystem.md` 仍未覆盖、且未被其它活跃 Plan 直接接管的四类 contract 缺口：

- `Documents/Plans/Plan_InterfaceBinding.md` 与 `Documents/Plans/Plan_CppInterfaceBinding.md` 继续承接 interface auto-bind / property bridge 主线；本轮只修 `Bind_UObject.cpp` 已公开 cast/query surface 的错误语义，不重复展开 interface owner。
- `Documents/Plans/Plan_HazelightBindModuleMigration.md` 已承接 `GetComponentsByClassWithTag()`、`VerifySpawnActor`、`DeprecateOldActorGenericMethods`；本轮只处理 actor query 仍残留的公开内部 helper 与 world-context 失败契约。

### Phase 1 深化：收口 `Bind_UObject` 剩余的 crash / UB surface

- [ ] **P1.10** 收紧 `Bind_UObject.cpp` 的 cast/query 基础 contract，移除测试残留日志与 invalid interface 静默吞错
  - 当前 `Bind_UObject.cpp` 把三类问题叠在同一条 `opCast` / `ImplementsInterface` 基础路径上：`Cast<T>(null)` 会在 `Object->IsA(...)` 之前直接解引用；`ImplementsInterface` 对 `nullptr` 与非 interface class 统一静默压成 `false`；共享 helper 里还残留 `ScenarioInterfaceCastSuccess` / `DamageableCast` 字符串特判和 `Display` 级日志。这已经不是 interface 能力缺口，而是现有公开 surface 的 correctness contract 漂移。
  - 这一项不重复开启 interface bridge 主线，只把现有 query/cast 行为收口成脚本可依赖的基础语义：`opCast` 统一先把 `OutAddress` 置 `nullptr`，`Object == nullptr` 走稳定 cast-fail；`ImplementsInterface` 通过内部 `ValidateInterfaceClassOrThrow` helper 对 `nullptr` / 非 interface class 抛精确脚本错误，保留 `Object == nullptr` 时返回 `false` 的对象语义；同时彻底移除测试名耦合和无条件 `Display` 日志，若需要诊断只能挂到显式 debug gate。
  - 来源：
    - [A] `BindSystem_Analysis.md` — “发现 15/17/24：`opCast` 对 null handle 无保护、runtime bind 硬编码测试名/类型名、interface cast 无条件打印 `Display` 日志”
    - [B] `DiscoveryPlans/BindSystem_Plan.md` — “Issue-21/40：`ImplementsInterface` 无效 `UClass` 输入被静默压成 `false`，`Cast<T>(null)` 应稳定返回 `null`”
    - [C] `TestCoverage/BindSystem_TestGaps.md` — “NewTest-45：`ObjectCastCompat` 仍缺 null cast / interface success-fail 直接回归”
    - [E] `ReferenceComparison/GapAnalysis.md` — “[D2] checked contract 建议：`ImplementsInterface()` 需要 `ValidateInterfaceClass` 级别的显式校验，而不是继续吞掉 invalid input”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp` L100-L105 — `ImplementsInterface` 对 `InterfaceClass == nullptr` 直接 `return false`，没有 interface-kind 校验
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp` L135-L197 — `opCast` 在 `Object->IsA(...)` / `Object->GetClass()->ImplementsInterface(...)` 前没有 `Object == nullptr` guard，且同一块代码里仍保留 `ScenarioInterfaceCastSuccess` / `DamageableCast` 特判与 `Display` 日志
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCompatBindingsTests.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceCastTests.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceImplementTests.cpp`
- [ ] **P1.10** 📦 Git 提交：`[AngelscriptRuntime] Fix: harden object cast and interface query contracts`
- [ ] **P1.10-T** 单元测试：补齐 `opCast` / `ImplementsInterface` 的 null、checked-input 与去调试残留回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCompatBindingsTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceCastTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceImplementTests.cpp`
  - 测试场景：
    - 正常路径：合法 UObject cast、合法 interface success/fail query 继续返回与当前 happy path 一致的结果
    - 边界条件：`Cast<UPackage>(null)`、`Cast<UIDamageableCastOk>(null)`、以及不包含 `ScenarioInterfaceCastSuccess` / `DamageableCast` 子串的等价测试类型都稳定返回 `null` 或正确布尔值，不再依赖测试命名偶然触发行为
    - 错误路径：`this.ImplementsInterface(cast<UClass>(null))` 与 `this.ImplementsInterface(AActor::StaticClass())` 都必须记录精确脚本错误，而不是继续静默返回 `false`
  - 测试命名：`Angelscript.TestModule.Bindings.ObjectCastAndInterfaceQueryContracts`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + `CompileAnnotatedModuleFromMemory`
- [ ] **P1.10-T** 📦 Git 提交：`[AngelscriptTest] Test: cover object cast and interface query contracts`

- [ ] **P1.11** 把 `CopyScriptPropertiesFrom` 从未定义行为入口收紧为 live script object 专用 contract
  - 当前 `CopyScriptPropertiesFrom(const UObject OtherObject)` 仍把最低层 `asIScriptObject` 赋值原语直接挂到全部 `UObject` 上，函数体只有一行 `*(asIScriptObject*)Object = *(asIScriptObject*)OtherObject;`。这意味着 `null`、native `UObject`、不同 script class、乃至 stale script object 都会先落进裸指针重解释，而不是在 bind 边界给出稳定错误。
  - 这一项应明确把它收紧成“只对 live script object 有效”的 helper：先校验 source/dest 非空、两边都是 `UASClass` 且 `ScriptTypePtr` 仍有效，再改用 AngelScript 自己的 `asIScriptObject::CopyFrom()` 返回码路径；未来如果确实需要“不同 script type 的受控属性拷贝”，应单独新增命名明确的 helper，而不是继续复用这个全量 copy 入口。
  - 来源：
    - [A] `BindSystem_Analysis.md` — “发现 20：`CopyScriptPropertiesFrom` 把内部 script-object 赋值原语裸暴露给全部 `UObject`，没有任何类型或空值保护”
    - [B] `DiscoveryPlans/BindSystem_Plan.md` — “Issue-13：应改用 `CopyFrom()` + live script object 校验，禁止继续对 `null` / native / mismatched type 裸赋值”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp` L128-L132 — `CopyScriptPropertiesFrom` 仍直接执行 `*(asIScriptObject*)Object = *(asIScriptObject*)OtherObject;`
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptObjectBindingsTests.cpp`
- [ ] **P1.11** 📦 Git 提交：`[AngelscriptRuntime] Fix: guard CopyScriptPropertiesFrom for live script objects`
- [ ] **P1.11-T** 单元测试：补齐 `CopyScriptPropertiesFrom` 的 live-script-object 正负路径回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptObjectBindingsTests.cpp`
  - 测试场景：
    - 正常路径：两个同类型 live script object 之间复制后，脚本字段值完整同步且目标对象保持可执行
    - 边界条件：source 与 dest 为同一个 live script object 时保持稳定 no-op 或等价成功语义，不产生额外错误
    - 错误路径：`null` source、`null` dest、native `UObject`、以及不同 script class 之间的调用都必须记录精确脚本错误，而不是崩溃或静默半拷贝
  - 测试命名：`Angelscript.TestModule.Bindings.ScriptObjectCopyContracts`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`
- [ ] **P1.11-T** 📦 Git 提交：`[AngelscriptTest] Test: cover CopyScriptPropertiesFrom live-script-object contracts`

### Phase 2 深化：补齐 class / actor query 公开 surface 的剩余 contract

- [ ] **P2.7** 统一 `UClass` nullable receiver 与 declaration round-trip 契约，作为 `P2.2` 的后半段收口
  - 现有 `P2.2` 已覆盖 lookup 可见性与 tombstone 过滤，但 `UClass` 的公开 surface 还留着第二层分叉：`FindClass()` 已把 miss 结果公开成 `null`，`GetDefaultObject()` / `IsAbstract()` / `GetSuperClass()` / `IsChildOf()` 却仍按裸 receiver 解引用；同时 `GetScriptTypeDeclaration()` 只对 `UASClass` 返回名字，而同文件全局 `FindClass()` 明明已经把 native class 的 canonical 名称固定成 `FAngelscriptType::GetBoundClassName(Class)`。
  - 这一项应把“class 查到之后还能否稳定继续链式调用”一起收口：为 `UClass` receiver 建统一 `ResolveClassReceiverOrThrow` helper，让 `GetDefaultObject()` / `IsAbstract()` / `GetSuperClass()` / `IsChildOf()` 在 miss 后返回默认值并报告精确错误；`GetScriptTypeDeclaration()` 则改为复用 lookup 的 canonical class-name policy，并对 `ScriptTypePtr == nullptr` 的 tombstone script class 保持空串或等价的“已失效”信号，避免 metadata 再次自相矛盾。
  - 来源：
    - [A] `BindSystem_Analysis.md` — “发现 21/22/23：native `FindClass("AActor")` 与 `GetScriptTypeDeclaration()` 无法 round-trip，nullable `UClass` helper 仍会崩溃，removed-class metadata 彼此矛盾”
    - [B] `DiscoveryPlans/BindSystem_Plan.md` — “Issue-26/28：`FindClass` miss 后的 receiver 需要显式收口，`GetScriptTypeDeclaration()` 应改走 `GetBoundClassName()`”
    - [C] `BindSystem_TestGaps.md` — “Issue-70 与 source-metadata 审查：`ClassLookupCompat` 仍只测 `AActor` happy path，`GetScriptTypeDeclaration()` 也只做非空断言”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp` L276-L278 — `GetDefaultObject()` 仍直接 `return Class->GetDefaultObject();`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp` L299-L326 — `GetScriptTypeDeclaration()` 只在 `Cast<UASClass>(Class)` 成功时返回名字，而 `IsChildOf()` / `IsAbstract()` / `GetSuperClass()` 都没有 null-receiver 收口
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp` L519-L531 — 全局 `FindClass()` 已把 native class lookup 固定为 `Class->GetName()` 或 `FAngelscriptType::GetBoundClassName(Class)`
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp`
- [ ] **P2.7** 📦 Git 提交：`[AngelscriptRuntime] Fix: unify UClass receiver and declaration contracts`
- [ ] **P2.7-T** 单元测试：补齐 `UClass` receiver 与 declaration round-trip 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp`
  - 测试场景：
    - 正常路径：`FindClass("AActor").GetScriptTypeDeclaration()` 返回 `"AActor"`，且 `FindClass(Decl)` 能回到同一 native class；script class metadata 继续返回当前预期 declaration/module/source
    - 边界条件：reload/remove 后的 tombstone script class 仍按统一规则返回空 declaration 或等价“已失效”信号，不再与 module/source metadata 打架
    - 错误路径：`FindClass("DefinitelyMissingType")` 链式调用 `GetDefaultObject()` / `IsAbstract()` / `GetSuperClass()` / `IsChildOf()` 时都返回稳定默认值并记录精确脚本错误，而不是崩溃
  - 测试命名：`Angelscript.TestModule.Bindings.ClassReceiverAndDeclarationContracts`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`
- [ ] **P2.7-T** 📦 Git 提交：`[AngelscriptTest] Test: cover class receiver and declaration contracts`

- [ ] **P2.8** 关闭 `__Actor_GetAllByClass` 公开后门，并让 actor 查询与 spawn 共用 world-context 失败语义
  - 当前 actor 查询 surface 仍有两条没有进入现有 Plan 的 contract gap：一条是 `__Actor_GetAllByClass` 作为公开 global bind 残留在 API 面上，完全绕过 `GetAllActorsOfClass(...)` 的 `TypeId` / class 校验；另一条是三个公开查询 helper 仍把 `TryGetCurrentWorldContextObject()` 直接传给 `UGameplayStatics`，无 world-context 时只会退化成底层 warning + 空数组，而不是像同文件 spawn helper 那样抛出显式脚本错误。
  - 这一项应承接已经规划好的 `Bind_WorldContextHelpers.h` 与 world-context trait 工作，把 actor query 也拉回同一 contract：删除或 internalize `__Actor_GetAllByClass`，抽 `ResolveActorArraySubtypeOrThrow()` 统一公开查询的数组/类型校验，再让 `GetAllActorsOfClass(?&)`、`GetAllActorsOfClass(UClass, ?&)`、`GetAllActorsOfClassWithTag(...)` 在进入 `UGameplayStatics` 前先通过 world helper 解析上下文；解析失败时统一清空 `OutActors` 并记录 `"Engine was null."` / `"Invalid World Context"`，不再把“查询结果为空”和“world context 无效”混成同一种返回值。
  - 来源：
    - [A] `BindSystem_Analysis.md` — “发现 28/34：`__Actor_GetAllByClass` 已经没有真实生成路径却仍公开暴露，`GetAllActorsOfClass*` 在 invalid world-context 时只退化成 warning + 空数组”
    - [B] `DiscoveryPlans/BindSystem_Plan.md` — “Issue-15/47：关闭 `__Actor_GetAllByClass` 后门，并让 actor 查询入口与 spawn 共用 world-context error contract”
    - [C] `BindSystem_TestGaps.md` — “NewTest-2：world-backed actor helper 仍缺 dedicated 行为回归”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp` L317-L352 — `GetAllActorsOfClass(?&)` 在完成数组类型校验后，直接把 `TryGetCurrentWorldContextObject()` 传给 `UGameplayStatics::GetAllActorsOfClass(...)`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp` L355-L402 — `GetAllActorsOfClass(UClass, ?&)` 同样直接把 ambient world-context 传到底层查询
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp` L405-L408、L411-L446 — `__Actor_GetAllByClass` 公开入口没有任何参数校验，`GetAllActorsOfClassWithTag(...)` 也没有 world-context 显式收口
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_WorldContextHelpers.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp`
- [ ] **P2.8** 📦 Git 提交：`[AngelscriptRuntime] Fix: close actor query backdoor and unify world-context failures`
- [ ] **P2.8-T** 单元测试：补齐 actor 查询公开 surface 与 invalid-world 合同回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp`
  - 测试场景：
    - 正常路径：有效 world 下三条公开查询 helper 继续返回与 `UGameplayStatics` 一致的结果，预填充数组也会被本次查询结果覆盖
    - 边界条件：用户脚本不应再解析到 `__Actor_GetAllByClass`；如果内部 helper 仍保留，只能通过生成代码使用，普通模块 compile smoke 必须红灯
    - 错误路径：无效 world-context 或无 engine 的执行窗口里，三条查询 helper 都记录 `"Engine was null."` / `"Invalid World Context"` 并清空 `OutActors`，不再只留下底层 warning + 空数组
  - 测试命名：`Angelscript.TestModule.Bindings.ActorQueryPublicSurfaceContracts`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_FULL()` + `FScopedTestWorldContextScope`
- [ ] **P2.8-T** 📦 Git 提交：`[AngelscriptTest] Test: cover actor query public-surface contracts`

### 单元测试总览增补（2026-04-09 01:14:20）

| 改进项 | 测试文件 | 测试重点 | 优先级 |
|--------|---------|---------|--------|
| `P1.10` | `Bindings/AngelscriptCompatBindingsTests.cpp`，`Interface/AngelscriptInterfaceCastTests.cpp`，`Interface/AngelscriptInterfaceImplementTests.cpp` | null cast、invalid interface input、去测试名耦合 | P0 |
| `P1.11` | `Bindings/AngelscriptObjectBindingsTests.cpp` | `CopyScriptPropertiesFrom` live script object 校验与错误码收口 | P0 |
| `P2.7` | `Bindings/AngelscriptClassBindingsTests.cpp`，`Bindings/AngelscriptFileAndDelegateBindingsTests.cpp` | nullable `UClass` receiver、native/script declaration round-trip、tombstone metadata 一致性 | P1 |
| `P2.8` | `Bindings/AngelscriptNativeEngineBindingsTests.cpp` | actor query 公开 surface、internal helper 隐藏、invalid-world 错误语义 | P1 |

### 风险与注意事项增补（2026-04-09 01:14:20）

#### 风险

1. `ImplementsInterface` 对 `nullptr` / 非 interface class 改为显式报错后，历史脚本里把 lookup 失败当成普通 `false` 处理的路径会暴露出来。
   - 缓解：同批补齐 null / invalid-input 回归，并在变更说明里明确“空对象仍返回 false，但空类/错类不再静默吞掉”。
2. `CopyScriptPropertiesFrom` 一旦收紧到 live script object 专用，少量依赖 current 未定义行为的脚本可能立即转成 runtime error。
   - 缓解：错误文本要明确区分 `null`、native object、mismatched script type，避免调用方只能看到笼统失败。
3. `GetScriptTypeDeclaration()` 对 native class 开始返回 canonical 名称后，个别把空串当成“不是 script-facing class”判据的脚本分支会改变。
   - 缓解：引导改用显式 helper 或 `Cast<UASClass>` 级判定，不继续依赖空串副作用。
4. actor 查询在 invalid-world 时改成显式脚本错误并隐藏 `__Actor_GetAllByClass` 后，仓库外部若私自依赖该内部 helper 或空数组语义，会在编译期/运行时暴露兼容性变化。
   - 缓解：保留 compile smoke 和 invalid-world 回归，确保变更是“更早更清晰地失败”，而不是再次形成 silent drift。

#### 已知行为变化

1. `Cast<T>(null)` 将稳定返回 `null`，`ImplementsInterface(nullptr / non-interface class)` 将改为显式脚本错误；同时 `Bind_UObject.cpp` 不再带测试名耦合的 `Display` 日志。
2. `CopyScriptPropertiesFrom` 不再接受 native `UObject`、空对象或不同 script type 的对象对拷；这些路径会在 bind 边界被拒绝。
3. native `UClass` 的 `GetScriptTypeDeclaration()` 将不再默认返回空串，而会与 `FindClass("AActor")` 等 lookup surface 共享同一 canonical 名称。
4. `__Actor_GetAllByClass` 将收回 internal surface 或直接移除；公开 actor 查询 helper 在无效 world-context 时会报脚本错误并清空输出数组，而不是继续把错误伪装成“结果为空”。

---

## 深化 (2026-04-09 01:24:15)

本轮深化只补当前 `Plan_BindSystem.md` 仍未覆盖、且未被其它活跃 Plan 直接接管的四类条目：

- `Documents/Plans/Plan_AS238NonLambdaPort.md` 已承接 `Bind_TOptional` 的 intrusive optional state 主线；本轮不在 `BindSystem` 里重复展开 `TOptional`。
- `Documents/Plans/Plan_StructUtilsMigration.md` 已承接 `FAngelscriptDelegateWithPayload` 的 payload 承载边界；本轮只处理 manual bind metadata / debug export contract，不触碰 payload storage 迁移。
- `Documents/Plans/Plan_TestCoverageExpansion.md` 覆盖的是 `FText` 的通用测试扩面；本轮只处理 current 已丢失的 compare/case API surface 与其直接回归。

### Phase 2 深化：补齐文本与碰撞配置的公开 surface

- [ ] **P2.9** 恢复 `Bind_FText.cpp` 的 locale-aware compare/case surface，停止把 `FText` 压扁成只剩 `IdenticalTo()` 的最小壳
  - 当前 `FText` bind 已不是“测试不够”这么简单，而是公开 surface 真的被收窄了：脚本只剩 identity/basic-state 能力，没有 `ETextComparisonLevel`、`CompareTo*`、`EqualTo*`、`ToLower()`、`ToUpper()`。这会把原本应留在 `FText` 层的本地化比较与大小写语义逼回 `FString`。
  - 这一项不做泛化测试重写，只把缺失 surface 先补回 current 主链：恢复 `ETextComparisonLevel` 枚举与 6 个成员方法，让 `FText` 重新具备 UEAS2 已有的 lexical / locale-aware compare/case 能力；现有 `IdenticalTo()` 继续保留，用来明确“locale equality”和“identity equality”是两套 contract。
  - 这条任务与 `Plan_TestCoverageExpansion.md` 的差异在于：那里主要补现有 `FText` API 的一般覆盖，这里先恢复 today 直接缺失的绑定面，避免测试只能为一个已缩水的 surface 做背书。
  - 来源：
    - [A] `BindSystem_Analysis.md` — “发现 55：`FText` 丢失 `ETextComparisonLevel`、`CompareTo/EqualTo/ToLower/ToUpper` 整组 API”
    - [B] `DiscoveryPlans/BindSystem_Plan.md` — “Issue-80：恢复 `FText` locale-aware compare/case 绑定面，并补最小行为回归”
    - [C] `TestCoverage/BindSystem_TestGaps.md` — “`Bind_FText.cpp` 位于当前未见 direct-hit 的绑定清单，现有测试没有直接锁住这组 surface”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FText.cpp` L61-L71 — 只注册了 `ETextIdenticalModeFlags` 与 `EDateTimeStyle`，没有 `ETextComparisonLevel`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FText.cpp` L99-L107 — `FText_` 仅保留 `IsEmpty*`、`IsTransient`、`IsCultureInvariant`、`IsFromStringTable`、`IdenticalTo()` 和 `opAssign()`，没有 `CompareTo*`、`EqualTo*`、`ToLower()`、`ToUpper()`
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FText.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptTextBindingsTests.cpp`
- [ ] **P2.9** 📦 Git 提交：`[AngelscriptRuntime] Fix: restore FText compare and case bindings`
- [ ] **P2.9-T** 单元测试：补齐 `FText` compare/case surface 的 compile/runtime 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptTextBindingsTests.cpp`
  - 测试场景：
    - 正常路径：`FText::FromString("Alpha")` 与 `FText::FromString("alpha")` 上，`EqualToCaseIgnored()`、`CompareToCaseIgnored()`、`ToLower()`、`ToUpper()` 与原生 `FText` 基线一致
    - 边界条件：`EqualTo(..., ETextComparisonLevel::Primary)`、`CompareTo(..., ETextComparisonLevel::Default)` 可编译执行，且 `IdenticalTo()` 仍与 compare/case 路径保持独立语义
    - 错误路径：如果 `ETextComparisonLevel` 或任一 compare/case 方法再次从 surface 消失，compile smoke 必须在对应声明处红灯，而不是让测试继续只验证 `IdenticalTo()`
  - 测试命名：`Angelscript.TestModule.Bindings.TextCompareCaseSurface`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`
- [ ] **P2.9-T** 📦 Git 提交：`[AngelscriptTest] Test: cover FText compare and case bindings`

- [ ] **P2.10** 把 `Bind_ConfigEnums.cpp` 重新拉回“项目配置即真值”的枚举生成合同，统一恢复 collision query enums 与 `EPhysicalSurface`
  - 当前配置枚举 shard 同时有两层漂移：一层是 `ECollisionChannel` / `ETraceTypeQuery` / `EObjectTypeQuery` 错把 profile 模板当成 channel 定义来源；另一层是 `EPhysicalSurface` 整段消失。两者共同的问题都不是“少几个常量”，而是脚本看到的配置枚举已经不再由项目配置直接驱动。
  - 这一项应把 `Bind_ConfigEnums.cpp` 收口到统一来源：collision query enums 改回读取 `DefaultChannelResponses`，补回 `Pawn` / `Vehicle` / `Destructible` 等标准 channel；同文件顺手恢复 `UPhysicsSettings::PhysicalSurfaces` 到 `EPhysicalSurface` 的映射，避免脚本再靠字符串或额外桥接访问 surface 配置。
  - 这条任务优先修 source-of-truth，不和 `Bind_CollisionProfile.cpp` 的 profile 常量面混在一起；后续 parity 也要把 “profile 名常量” 与 “query/surface 枚举” 分栏锁定。
  - 来源：
    - [A] `BindSystem_Analysis.md` — “发现 68/69：`Bind_ConfigEnums.cpp` 错用 profile 模板生成 collision enums，且 `EPhysicalSurface` 已从 current 消失”
    - [B] `DiscoveryPlans/BindSystem_Plan.md` — “Issue-53/54：恢复 `DefaultChannelResponses` 驱动的 collision query enums，并补回 `EPhysicalSurface` 项目配置绑定”
    - [C] `TestCoverage/BindSystem_TestGaps.md` — “`Bind_ConfigEnums.cpp` 位于当前未见 direct-hit 的绑定清单，测试树对 `ETraceTypeQuery` / `EObjectTypeQuery` / `EPhysicalSurface` 没有 dedicated parity”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_ConfigEnums.cpp` L12-L37 — 当前仍用 `GetNumOfProfiles()` / `GetProfileByIndex()` 和 `temp->ObjectType` 构造 `ECollisionChannel`、`ETraceTypeQuery`、`EObjectTypeQuery`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_ConfigEnums.cpp` L40-L45 — 只补了少量 `Visibility` / `Camera` / `WorldStatic` / `WorldDynamic` / `PhysicsBody`，没有 `Pawn` / `Vehicle` / `Destructible`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_ConfigEnums.cpp` L1-L46 — 文件只包含 `Engine/CollisionProfile.h` 与三组碰撞枚举注册，没有任何 `PhysicsEngine/PhysicsSettings.h` 或 `EPhysicalSurface` 注册
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_ConfigEnums.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp`
- [ ] **P2.10** 📦 Git 提交：`[AngelscriptRuntime] Fix: restore config-driven collision and surface enums`
- [ ] **P2.10-T** 单元测试：补齐 `Bind_ConfigEnums.cpp` 的 config parity 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp`
  - 测试场景：
    - 正常路径：脚本可编译解析一个 trace channel、一个 object channel、一个 `ECollisionChannel` 常量，以及至少一个项目 `EPhysicalSurface::<Name>`，且值与原生配置一致
    - 边界条件：`Pawn`、`Vehicle`、`Destructible` 等标准 channel 在无额外项目改动时仍稳定可见；若 `PhysicalSurfaces` 为空，`EPhysicalSurface` 类型仍应存在且测试走空配置分支
    - 错误路径：profile 名不得再伪装成 query enum 名；若枚举生成再次回退到 `GetProfileByIndex()` 或缺失 `EPhysicalSurface`，compile parity 必须在对应枚举访问处红灯
  - 测试命名：`Angelscript.TestModule.Core.ConfigEnumParityContracts`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`
- [ ] **P2.10-T** 📦 Git 提交：`[AngelscriptTest] Test: cover config enum parity contracts`

- [ ] **P2.11** 恢复 `FCollisionObjectQueryParams(TArray<EObjectTypeQuery>)` 构造器，补回 object-type trace 的高层输入形态
  - 当前 `FCollisionObjectQueryParams` 只剩默认构造、`ECollisionChannel`、`ECollisionObjectQueryInitType` 和 `int32` bitfield 构造器，缺掉了最接近 Blueprint / UEAS2 迁移代码的 `TArray<EObjectTypeQuery>` 入口。脚本只能退回手写 bitfield 或逐个 `AddObjectTypesToQuery()`。
  - 这一项应按 UEAS2 contract 补回数组构造器，并把它和现有 manual `AddObjectTypesToQuery()` 路径做 bitfield 对账，避免“只是能编译”但语义仍然分叉。由于该构造器直接消费 `EObjectTypeQuery`，执行时应把它与 `P2.10` 的 collision enum parity 分开验证，但允许共用同一批 collision 测试夹具。
  - 来源：
    - [A] `BindSystem_Analysis.md` — “发现 44：`FCollisionObjectQueryParams` 丢失 `TArray<EObjectTypeQuery>` 构造器，object-type trace 初始化能力弱于 UEAS2”
    - [B] `DiscoveryPlans/BindSystem_Plan.md` — “Issue-81：补回 `FCollisionObjectQueryParams(ObjectTypes)` 并用 `GetQueryBitfield()` 与 manual path 对账”
    - [C] `TestCoverage/BindSystem_TestGaps.md` — “collision params parity 目前只锁 `AddObjectTypesToQuery()` / `GetQueryBitfield()`，没有数组构造路径”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FCollisionQueryParams.cpp` L361-L380 — `FCollisionObjectQueryParams_` 在 late pass 只注册了 `ECollisionObjectQueryInitType` 和 `int32 InObjectTypesToQuery` 两个 constructor，以及 `AddObjectTypesToQuery()` / `GetQueryBitfield()`，没有 `const TArray<EObjectTypeQuery>&` constructor
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FCollisionQueryParams.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp`
- [ ] **P2.11** 📦 Git 提交：`[AngelscriptRuntime] Fix: add array constructor for collision object queries`
- [ ] **P2.11-T** 单元测试：补齐 `FCollisionObjectQueryParams(ObjectTypes)` parity 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp`
  - 测试场景：
    - 正常路径：`TArray<EObjectTypeQuery>` 构造出的 `FCollisionObjectQueryParams` 可参与 `SweepSingleByObjectType(...)` / `ComponentOverlapMulti(...)`，并与原生对象类型数组构造结果一致
    - 边界条件：空数组、重复 object type、以及 `WorldStatic + WorldDynamic` 组合都生成稳定 bitfield，且与 `AddObjectTypesToQuery()` 手工路径一致
    - 错误路径：如果数组 constructor 再次缺失，compile smoke 必须在 `FCollisionObjectQueryParams(ObjectTypes)` 处红灯；若 ctor 存在但 bitfield 与 manual path 不一致，runtime parity 必须直接失败
  - 测试命名：`Angelscript.TestModule.Core.CollisionObjectQueryArrayCtor`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`
- [ ] **P2.11-T** 📦 Git 提交：`[AngelscriptTest] Test: cover collision object query array constructor parity`

### Phase 3 深化：收口 payload delegate 的 tooling contract

- [ ] **P3.2** 恢复 `FAngelscriptDelegateWithPayload` 的 manual bind metadata 与 debug informed-meta 导出链
  - current 的 payload delegate 运行时能力还在，但 tooling contract 已经被收窄了：`BindWithPayload()` 没再附 `DelegateWildcardParam` 等 manual metadata，`DebugServer` 也不再把这类 meta 发给客户端。结果是脚本/IDE 只能“能跑”，却拿不到 wildcard payload 的参数提示与绑定提示。
  - 这一项不重复 `Plan_StructUtilsMigration.md` 的 payload storage 迁移，只补 bind/tooling 合同：为 `BindUFunction()` / `BindWithPayload()` 恢复 `DelegateObjectParam`、`DelegateFunctionParam`、`DelegateBindType`，并让 payload overload 额外带上 `DelegateWildcardParam = Payload`；同时把 `AngelscriptDebugServer.cpp` 的 `NAMES_InformedMeta` 补回 `DelegateWildcardParam`，让 debug database / IDE 重新看到 wildcard payload 信息。
  - 这条能力属于典型“runtime path 还活着，但 contract 比 UEAS2 晚一步”的回退；应优先通过 metadata/export 回归把它重新前移到 bind 阶段。
  - 来源：
    - [C] `TestCoverage/BindSystem_TestGaps.md` — “NewTest-10：`Bind_FAngelscriptDelegateWithPayload.cpp` 目前没有 payload 执行与签名错误路径回归”
    - [E] `ReferenceComparison/Hazelight_Analysis.md` — “[D2/D5/D6] current 保留 `BindWithPayload` 运行时能力，但 `Bind_FAngelscriptDelegateWithPayload.cpp` 去掉了 `DelegateWildcardParam` 等 manual meta，`AngelscriptDebugServer.cpp` 的 `NAMES_InformedMeta` 也少了 `DelegateWildcardParam`”
    - [E] `ReferenceComparison/CrossComparison.md` — “[D2] Angelscript 的优势在于把 contract 前置到静态 bind / bind database 阶段，不应让 payload delegate 退回‘运行时能跑、工具链看不见’的状态”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FAngelscriptDelegateWithPayload.cpp` L22-L32 — `BindUFunction()` / `BindWithPayload()` 注册后没有任何 `SCRIPT_MANUAL_BIND_META(...)` 跟随
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L33-L38 — `NAMES_InformedMeta` 只有 `DelegateBindType`、`DelegateFunctionParam`、`DelegateObjectParam`，缺少 `DelegateWildcardParam`
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FAngelscriptDelegateWithPayload.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebugServerMetadataTests.cpp`
- [ ] **P3.2** 📦 Git 提交：`[AngelscriptRuntime] Fix: restore payload delegate metadata export`
- [ ] **P3.2-T** 单元测试：补齐 payload delegate runtime + metadata 导出回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebugServerMetadataTests.cpp`
  - 测试场景：
    - 正常路径：`BindWithPayload()` 可对 primitive payload 和 `UScriptStruct` payload 正常执行；debug metadata / informed meta 同时包含 `DelegateObjectParam`、`DelegateFunctionParam`、`DelegateBindType`、`DelegateWildcardParam`
    - 边界条件：普通 `BindUFunction()` 仍只导出三项基础 delegate meta，而 payload overload 额外导出 `DelegateWildcardParam`；`ExecuteIfBound()` 在未绑定时继续保持 no-op
    - 错误路径：invalid object、无参函数、错误 payload 类型继续抛出当前精确错误；同时非 payload overload 不得误报 `DelegateWildcardParam`
  - 测试命名：`Angelscript.TestModule.Bindings.PayloadDelegateMetadataContracts`，`Angelscript.TestModule.Debugger.PayloadDelegateMetaExport`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`
- [ ] **P3.2-T** 📦 Git 提交：`[AngelscriptTest] Test: cover payload delegate metadata contracts`

### 单元测试总览增补（2026-04-09 01:24:15）

| 改进项 | 测试文件 | 测试重点 | 优先级 |
|--------|---------|---------|--------|
| `P2.9` | `Bindings/AngelscriptTextBindingsTests.cpp` | `FText` compare/case surface、`ETextComparisonLevel`、`IdenticalTo()` 对照 | P1 |
| `P2.10` | `Core/AngelscriptEngineParityTests.cpp` | config-driven collision enums、`EPhysicalSurface`、profile-vs-channel 真值一致性 | P1 |
| `P2.11` | `Core/AngelscriptEngineParityTests.cpp` | `FCollisionObjectQueryParams(ObjectTypes)` 构造器与 manual path bitfield 对账 | P1 |
| `P3.2` | `Bindings/AngelscriptFileAndDelegateBindingsTests.cpp`，`Debugger/AngelscriptDebugServerMetadataTests.cpp` | payload delegate 执行、wildcard meta 导出、IDE/debug informed meta | P1 |

### 风险与注意事项增补（2026-04-09 01:24:15）

#### 风险

1. `FText` compare/case surface 恢复后，少量当前依赖“只能退回 `ToString()` 比较”的脚本可能转而使用 locale-aware 语义，结果与旧字符串比较不再完全一致。
   - 缓解：测试里同时保留 `IdenticalTo()` 与 compare/case 对照，明确 identity 与 lexical/locale 比较不是同一个 contract。
2. `Bind_ConfigEnums.cpp` 改回读取 `DefaultChannelResponses` 与 `PhysicalSurfaces` 后，当前脚本里误用 profile 名当 query enum 名的路径会在编译期暴露出来。
   - 缓解：把 profile parity 与 config enum parity 分开测试，确保报错直接指向“名字来源错了”，而不是混成 collision 子系统整体回归。
3. `FCollisionObjectQueryParams(ObjectTypes)` 直接依赖 `EObjectTypeQuery` 的配置映射；若 collision enum 仍有漂移，新增 ctor 会先暴露第二层问题。
   - 缓解：执行顺序上让 `P2.11` 明确依赖 `P2.10` 的 enum parity 结论，但测试仍将 ctor parity 与 enum parity 分栏，避免定位串线。
4. payload delegate metadata 一旦恢复，IDE/debugger 看到的提示词会变多；若下游工具意外假设 informed meta 白名单固定为 3 项，需要一起同步。
   - 缓解：为 `NAMES_InformedMeta` 增加 dedicated regression，并在调试协议变更说明里明确新增 `DelegateWildcardParam` 是兼容增强，不是运行时语义变更。

#### 已知行为变化

1. `FText` 将重新暴露 `ETextComparisonLevel`、`CompareTo*`、`EqualTo*`、`ToLower()`、`ToUpper()`；脚本不必再把本地化文本强制降级成 `FString` 比较。
2. `ECollisionChannel` / `ETraceTypeQuery` / `EObjectTypeQuery` 将重新以项目 channel 配置为真值，`EPhysicalSurface` 也会重新出现在脚本类型系统里。
3. `FCollisionObjectQueryParams` 将新增 `TArray<EObjectTypeQuery>` 构造器，Blueprint / UEAS2 迁移代码可直接复用 object-type 列表输入形态。
4. `FAngelscriptDelegateWithPayload` 的 payload overload 会重新向 debug/IDE 导出 `DelegateWildcardParam`，工具侧可见的 payload 绑定提示将比 current 更完整。

---

## 深化 (2026-04-09 01:33:43)

本轮深化只补当前 `Plan_BindSystem.md` 尚未覆盖、且没有被其它活跃 Plan 直接承接 implementation 的 5 组 bind contract：

- `Documents/Plans/Plan_TestCoverageExpansion.md` 只承接 `Json` / `AssetManager` / `GAS` / `Math` 的 zero/weak coverage 补测，不承接这里的 runtime bind 修复。
- `Documents/Plans/Plan_AS238NonLambdaPort.md` 当前聚焦 `TOptional`、`TStructType`、accessor parity，不直接覆盖 `Bind_UStruct.cpp` 的 `CanHashValue()` 早期 fallback。

### Phase 1 深化：先封住会崩溃、假成功或在类型门禁处提前断裂的 bind

- [ ] **P1.12** 收口 GAS value bind 的空参与假成功合同，统一 `FGameplayEffectSpec` / `FGameplayTagBlueprintPropertyMap` 的失败语义
  - 当前 GAS value bind 在同一能力簇里暴露了两类高风险分叉：`FGameplayEffectSpec` 直接 placement-new native 对象，`null UGameplayEffect` 会把错误一路带进 GAS 内部 `check(Def)`；`FGameplayTagBlueprintPropertyMap.Initialize()` 则把 `null Owner/ASC` 维持成“只写日志后返回”的假成功。两者共同的问题都不是缺少 API，而是 bind 边界没有把参数非法升级成脚本可见错误。
  - 这一项应把两个入口一起收口成“脚本参数非法即显式失败”的合同：在 `Bind_FGameplayEffectSpec.cpp` 的构造 lambda 前置 `InDef == nullptr` 校验并抛 `FAngelscriptEngine::Throw(...)`；在 `Bind_FGameplayTagBlueprintPropertyMap.cpp` 为 `Initialize()` 增加 wrapper，分别校验 `Owner` 与 `ASC`，同时确保失败路径不会继续执行 `ApplyCurrentTags()` 或把旧注册状态伪装成成功返回。
  - 这里不扩更多 GAS surface，也不改 gameplay 能力语义；只把 current 已经公开的两个 value bind 先修到“不会 crash，也不会 silent success”的最低 correctness 基线。
  - 来源：
    - [B] `DiscoveryPlans/BindSystem_Plan.md` — “Issue-59/60：`FGameplayEffectSpec` 空 effect 会撞原生 `check`，`GameplayTagBlueprintPropertyMap.Initialize` 空参数只写日志不抛脚本错误”
    - [C] `TestCoverage/BindSystem_TestGaps.md` — “NewTest-33/34：这两个 bind 文件目前完全无直接回归，应新增 null-guard 测试”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FGameplayEffectSpec.cpp` L8-L13 — 构造器直接 `new(Address) FGameplayEffectSpec(InDef, InEffectContext, Level)`，没有 `InDef == nullptr` 前置校验
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FGameplayTagBlueprintPropertyMap.cpp` L5-L8 — `Initialize(UObject Owner, UAbilitySystemComponent ASC)` 仍以 `METHODPR_TRIVIAL` 直绑，没有 script-side null guard 或失败态收口
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FGameplayEffectSpec.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FGameplayTagBlueprintPropertyMap.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGASValueBindingsTests.cpp`
- [ ] **P1.12** 📦 Git 提交：`[AngelscriptRuntime] Fix: harden GAS value bind null contracts`
- [ ] **P1.12-T** 单元测试：补齐 GAS value bind 的 null-guard 与 false-success 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGASValueBindingsTests.cpp`
  - 测试场景：
    - 正常路径：有效 `UGameplayEffect` + 默认 `FGameplayEffectContextHandle` 可正常构造 `FGameplayEffectSpec`；有效 `Owner` + `ASC` 仍可执行 `Initialize()`，不回退已有 happy path
    - 边界条件：先经历一次失败初始化，再用有效参数重新初始化同一个 `FGameplayTagBlueprintPropertyMap`，确认失败态不会污染后续成功路径；默认 `Level = -1.f` 入口仍保持 current 语义
    - 错误路径：`FGameplayEffectSpec(null, Context, 1.0f)`、`Map.Initialize(null, null)`、`Map.Initialize(this, null)` 都必须抛显式脚本错误；测试进程不得触发原生 `check` 或只留下日志后继续返回
  - 测试命名：`Angelscript.TestModule.Bindings.GameplayEffectSpecNullDefGuard`；`Angelscript.TestModule.Bindings.GameplayTagBlueprintPropertyMapInitializeNullGuards`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`
- [ ] **P1.12-T** 📦 Git 提交：`[AngelscriptTest] Test: cover GAS value bind null guards`

- [ ] **P1.13** 恢复 script struct 作为 `TSet` / `TMap` key 的早期哈希门禁，收口 `Bind_UStruct.cpp` 与 container gate 的时序断层
  - 当前 `UStruct` 哈希问题不是“没有 hash 实现”，而是 runtime 能力和 bind gate 被拆成了两段互相看不见的链路：`ASStruct.cpp` 仍会在脚本 struct 存在 `uint32 Hash() const` 时把 `HasGetTypeHash` 写进 fake vtable，但 `Bind_UStruct.cpp` 的 `CanHashValue()` 只认 `ICppStructOps`，导致 `TSet` / `TMap` 在 property/type gate 阶段先把脚本 struct 判死。
  - 这一项应恢复 UEAS2 的早期 fallback：在 `Bind_UStruct.cpp` 提取共享 helper，优先检查 `Ops->HasGetTypeHash()`，若 `Ops` 尚未就绪则回退到 `Usage.ScriptClass->GetMethodByDecl("uint32 Hash() const")`；`GetHash()` 仍继续走原生 `Ops->GetStructTypeHash(Address)`，避免把真正的 hash 计算再次挪回脚本反射层。
  - 这里不碰 `TStructType`、`StaticStruct` 或 broader non-lambda 路线，只修“脚本 struct 明明有合法 `Hash()`，却在容器 key gate 被提前拒绝”这一条当前 correctness 断层。
  - 来源：
    - [A] `BindSystem_Analysis.md` — “发现 39：`CanHashValue()` 过早依赖 `ICppStructOps`，带 `Hash()` 的 script struct 仍会被 `TSet` / `TMap` 错误拒绝”
    - [B] `DiscoveryPlans/BindSystem_Plan.md` — “Issue-83：恢复 `uint32 Hash() const` 的早期 fallback，并补 script-struct-key 容器回归”
    - [C] `TestCoverage/BindSystem_TestGaps.md` — “`Bind_UStruct.cpp` 仍位于当前未见 direct-hit 的运行时 bind 清单，现有容器测试只锁 `TSet<FName>` / `TMap<FName,...>` happy path”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UStruct.cpp` L227-L230 — `CanHashValue()` 只返回 `Ops != nullptr && Ops->HasGetTypeHash()`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSet.cpp` L91-L96 — `TSet` property/type gate 直接要求 `Usage.SubTypes[0].CanHashValue()`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp` L108-L113 — `TMap` key gate 同样直接要求 key subtype `CanHashValue()`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.cpp` L65-L72、L108-L111 — fake vtable 仍在 `HashFunction != nullptr` 时写入 `HasGetTypeHash` 与 `CPF_HasGetValueTypeHash`
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UStruct.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSet.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptContainerBindingsTests.cpp`
- [ ] **P1.13** 📦 Git 提交：`[AngelscriptRuntime] Fix: restore script struct hashability gates`
- [ ] **P1.13-T** 单元测试：补齐 script struct key 的 container gate 与 runtime hash 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptContainerBindingsTests.cpp`
  - 测试场景：
    - 正常路径：定义带 `uint32 Hash() const` 和 `opEquals` 的最小 script struct，确认 `TSet<FHashableKey>` / `TMap<FHashableKey, int32>` 可编译、可 `Add()`、可 `Contains()` / `Find()`
    - 边界条件：重复 key 的 `Add()` / `Contains()` 仍保持稳定；native `FName` key 的现有 happy path 不回退
    - 错误路径：缺少 `Hash()`、或 `Hash()` 签名不是严格 `uint32 Hash() const` 的 script struct，仍必须在容器 key gate 处编译失败，而不是被误判为可哈希
  - 测试命名：`Angelscript.TestModule.Bindings.ScriptStructHashableContainerKeys`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`
- [ ] **P1.13-T** 📦 Git 提交：`[AngelscriptTest] Test: cover script struct hashable container keys`

### Phase 2 深化：补 utility / asset / data bind 的公开合同与 parity

- [ ] **P2.12** 恢复 `Math` 命名空间的 64-bit 基础 overload，停止让当前测试源码与真实 bind surface 脱节
  - 这条缺口不是“测试写多了”，而是 runtime bind 确实只保留了 32-bit 和浮点基础算子。更糟的是，仓库自己的 `AngelscriptMathAndPlatformBindingsTests.cpp` 仍然在脚本里直接调用 `Math::Abs(int64(-7))`、`Math::Sign(int64(-7))`、`Math::Min/Max(int64(...))`、`Math::Square(int64(9))`，说明 current 的 bind surface、测试源码和 UEAS2 兼容预期已经彼此脱节。
  - 这一项应按 UEAS2 对齐回补 `Abs/Sign/Min/Max/Square` 的 `int64` / `uint64` overload，并保持 `FUNC_TRIVIAL` / `FUNCPR_TRIVIAL` 的直接注册方式，不为纯 surface parity 额外引入 wrapper lambda。测试侧则把现有 `int64` smoke 升级为 focused regression，并顺手补齐 `uint64` 路径。
  - 这里不展开 `LinePlaneIntersection` / `LineExtentBoxIntersection` 的 out-param family，只收口最基础、且当前测试源码已经直接依赖的 wide integer 算子。
  - 来源：
    - [A] `BindSystem_Analysis.md` — “发现 35：`Math` 命名空间缺失整组 `int64/uint64` 基础 overload”
    - [B] `DiscoveryPlans/BindSystem_Plan.md` — “Issue-82：恢复 `Abs/Sign/Min/Max/Square` 的 64-bit surface，并对齐现有测试源码”
    - [C] `TestCoverage/BindSystem_TestGaps.md` — “`MathExtendedCompat` 仍是宽松 smoke，无法锁住 `Bind_FMath.cpp` 的 signature surface 与精确语义”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FMath.cpp` L369-L393 — 当前只注册了 `float64` / `float32` / `int32`，以及 `uint32` 的 `Min/Max/Square`，没有 `int64` / `uint64` 对应 overload
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathAndPlatformBindingsTests.cpp` L119-L127 — 现有脚本 smoke 仍直接调用 `Math::Abs/Sign/Min/Max/Square(int64)`
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FMath.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathAndPlatformBindingsTests.cpp`
- [ ] **P2.12** 📦 Git 提交：`[AngelscriptRuntime] Fix: restore math wide integer overloads`
- [ ] **P2.12-T** 单元测试：补齐 `Math` 64-bit overload 的 compile/runtime 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathAndPlatformBindingsTests.cpp`
  - 测试场景：
    - 正常路径：`Math::Abs/Sign/Min/Max/Square` 的 `int64` 与 `uint64` overload 都可编译执行，并返回与 C++ 原生 `FMath` 一致的结果
    - 边界条件：`int64(0)`、负数、`uint64(0)` 与大于 `int32` 的宽整数输入都保持稳定；新增 overload 不影响既有 `int32` / 浮点路径
    - 错误路径：若任一 wide overload 再次缺失，compile smoke 必须在 `Math::Min(uint64(...))` / `Math::Square(int64(...))` 处直接红灯，而不是只让现有 happy path 偶然通过
  - 测试命名：`Angelscript.TestModule.Bindings.MathWideIntegerSurface`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`
- [ ] **P2.12-T** 📦 Git 提交：`[AngelscriptTest] Test: cover math wide integer overloads`

- [ ] **P2.13** 收口 `Bind_UAssetManager.cpp` 的 primary-asset 合同，统一修复 silent no-op、typed ctor 漂移与 type-based load 缺口
  - 当前 `UAssetManager` 绑定不是单点缺口，而是在同一个文件里叠了三层 drift：无效 `FPrimaryAssetId` 只触发 `ensure` 后直接 `return`，脚本看见的是 silent no-op；`FPrimaryAssetId` 只剩字符串构造器，且现存 lambda 还把 placement-new 地址参数写成了 `FPrimaryAssetType*`；真正按 `FPrimaryAssetType` 发起批量加载的 `LoadPrimaryAssetsWithType(...)`，以及 `UAssetManager::IsInitialized()` / `Get()` 入口也都缺失或被注释掉了。
  - 这一项应把 `Bind_UAssetManager.cpp` 重新拉回“primary asset 是强类型 contract”这条主线：先把无效 id 输入升级成显式脚本错误，再补回 `FPrimaryAssetId(FPrimaryAssetType, FName)` 强类型构造和 `LoadPrimaryAssetsWithType(...)` 入口，同时恢复 `UAssetManager` namespace 下的 `IsInitialized()` / `Get()` helper，让脚本有标准的可用性探测与入口获取方式。
  - 这里不要求补完整个 asset tooling surface，也不把问题转嫁给业务脚本去先查 type 再拼 ids；目标是先修复当前已经公开、但 contract 明显破碎的 primary asset 基线。
  - 来源：
    - [A] `BindSystem_Analysis.md` — “发现 36/37/62：`LoadPrimaryAssetsWithType` 缺失、`FPrimaryAssetId` 强类型构造器丢失且字符串 ctor 留下类型错位、`UAssetManager::Get()/IsInitialized()` 被整段注释”
    - [B] `DiscoveryPlans/BindSystem_Plan.md` — “Issue-62/64/77：无效 `FPrimaryAssetId` 不应 silent no-op，`FPrimaryAssetId` 需恢复 typed ctor，`UAssetManager` 需补回 type-based load surface”
    - [C] `TestCoverage/BindSystem_TestGaps.md` — “`Bind_UAssetManager.cpp` 仍位于当前未见 direct-hit 的运行时 bind 清单，primary asset 相关 surface 没有 dedicated 回归”
    - [E] `ReferenceComparison/GapAnalysis.md` — “`PrimaryAssetId` 这类 asset symbol 的验收应以 script-facing contract 为准，而不是 raw source 名或半截迁移状态”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UAssetManager.cpp` L33-L35 — `FPrimaryAssetId` 字符串构造器的 lambda 首参数仍写成 `FPrimaryAssetType* Address`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UAssetManager.cpp` L49-L63 — `AssetManager_LoadPrimaryAssets()` 发现无效 `FPrimaryAssetId` 时仍只 `ensureMsgf(...)` 后 `return`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UAssetManager.cpp` L83-L105 — 当前只注册了 `LoadPrimaryAsset(...)` / `LoadPrimaryAssets(...)`，没有 `LoadPrimaryAssetsWithType(...)`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UAssetManager.cpp` L107-L110 — `UAssetManager::IsInitialized()` 与 `GetIfInitialized()` 绑定仍处于注释状态
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UAssetManager.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptAssetManagerBindingsTests.cpp`
- [ ] **P2.13** 📦 Git 提交：`[AngelscriptRuntime] Fix: restore primary asset manager contracts`
- [ ] **P2.13-T** 单元测试：补齐 primary asset ctor/load/state helper 的 dedicated 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptAssetManagerBindingsTests.cpp`
  - 测试场景：
    - 正常路径：`FPrimaryAssetId(FPrimaryAssetType, FName)`、`UAssetManager::Get()`、`UAssetManager::IsInitialized()`、`LoadPrimaryAssetsWithType(...)` 都可编译解析；若运行环境允许，typed load 入口可在空 bundles / 默认优先级下稳定调用
    - 边界条件：字符串 ctor 与 typed ctor 并存；空 bundle、默认 priority、空 callback object / callback name 的 typed load 路径保持稳定
    - 错误路径：无效 `FPrimaryAssetId` 传入 `LoadPrimaryAsset(...)` / `LoadPrimaryAssets(...)` 时必须抛显式脚本错误，不再 silent no-op；若 `LoadPrimaryAssetsWithType(...)` 或 typed ctor 再次缺失，compile smoke 必须直接红灯
  - 测试命名：`Angelscript.TestModule.Bindings.AssetManagerPrimaryAssetContracts`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`
- [ ] **P2.13-T** 📦 Git 提交：`[AngelscriptTest] Test: cover primary asset manager contracts`

- [ ] **P2.14** 恢复 `Bind_Json.cpp` 的 builder/probe 对称面，并把 primitive getter 的错误路径收回 deterministic contract
  - 当前 JSON bind 不是只少一两个 API，而是同一文件同时出现了 surface 缩水与错误路径未定义：`FJsonValueArrayContainer` 明明已经实现了 `AddBoolean()` / `AddArray()` / `AddObject()`，注册到脚本侧的 `FJsonArray` 却只剩 string/number；`FJsonObject` 只给 object/array 保留了 `TryGet*Field()`，primitive 字段全被强制收口成 throwing getter；而 `GetNumberField()` / `GetBoolField()` 在类型错误后还会把未初始化局部变量直接返回。
  - 这一项应把 `Json` 收口成同一条一致合同：补回 `FJsonArray` 的 bool/nested-array/nested-object builder，恢复 `TryGetStringField()` / `TryGetNumberField()` / `TryGetBoolField()`，并把 primitive getter 的错误路径改成“先初始化 deterministic 默认值，再 `Throw()`”，避免 `Throw()` 记录错误后继续返回栈垃圾。throwing getter 与 probe-style API 两套语义都要保留，但边界必须清楚。
  - 这里不扩 `JsonObjectConverter` 或更大的 serialization 家族，只先修 `Bind_Json.cpp` 这条 current 已经分叉的核心 surface。
  - 来源：
    - [A] `BindSystem_Analysis.md` — “发现 30/31/33：`FJsonArray` 丢失 bool/array/object builder，primitive `TryGet*Field` 被裁掉，`GetNumberField` / `GetBoolField` 错误路径会返回未初始化值”
    - [B] `DiscoveryPlans/BindSystem_Plan.md` — “Issue-76：恢复 JSON builder/probe surface 对称性，并把 getter 错误路径拉回可测试合同”
    - [C] `TestCoverage/BindSystem_TestGaps.md` — “NewTest-5/6：`Bind_Json.cpp` 需要同时补 round-trip 与类型错误/越界/迭代修改保护测试”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Json.cpp` L155-L165、L469-L471 — `FJsonValueArrayContainer` 仍实现 `AddBoolean()`、`AddArray()`、`AddObject()`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Json.cpp` L611-L616 — `FJsonArray` 当前只注册 `Empty()`、`AddString()`、`AddNumber()`、`Num()`、`GetValueAt()`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Json.cpp` L638-L665 — `FJsonObject` 只有 throwing getter 与 `TryGetObjectField()` / `TryGetArrayField()`，没有 primitive `TryGet*Field()`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Json.cpp` L274-L286、L306-L318 — `GetNumberField()` / `GetBoolField()` 在 `TryGet...` 失败前没有初始化 `Number` / `bBool`
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Json.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptJsonBindingsTests.cpp`
- [ ] **P2.14** 📦 Git 提交：`[AngelscriptRuntime] Fix: restore json builder and probe contracts`
- [ ] **P2.14-T** 单元测试：补齐 JSON builder/probe 与 deterministic error-path 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptJsonBindingsTests.cpp`
  - 测试场景：
    - 正常路径：脚本可直接构造包含 string/number/bool/object/array 的嵌套 `FJsonObject` / `FJsonArray`，`SaveToString()` 后再 `LoadFromString()`，字段和值都能 round-trip
    - 边界条件：`TryGetStringField()` / `TryGetNumberField()` / `TryGetBoolField()` 在字段缺失或类型不匹配时返回 `false` 且保持输出哨兵值不变；pretty/condensed 保存路径都稳定
    - 错误路径：`GetNumberField("Name")`、`GetBoolField("Count")`、数组越界、迭代期间修改 object 都必须产生固定脚本错误；getter 不得再把未初始化垃圾值带回脚本
  - 测试命名：`Angelscript.TestModule.Bindings.JsonSurfaceAndErrorContracts`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`
- [ ] **P2.14-T** 📦 Git 提交：`[AngelscriptTest] Test: cover json builder and error contracts`

### 单元测试总览增补（2026-04-09 01:33:43）

| 改进项 | 测试文件 | 测试重点 | 优先级 |
|--------|---------|---------|--------|
| `P1.12` | `Bindings/AngelscriptGASValueBindingsTests.cpp` | `FGameplayEffectSpec` null guard、`GameplayTagBlueprintPropertyMap.Initialize` false-success 收口 | P0 |
| `P1.13` | `Bindings/AngelscriptContainerBindingsTests.cpp` | script struct `Hash()` fallback、`TSet/TMap` key gate、wrong-signature 负例 | P1 |
| `P2.12` | `Bindings/AngelscriptMathAndPlatformBindingsTests.cpp` | `Math` 64-bit overload 存在性、`int64/uint64` 结果精确语义 | P1 |
| `P2.13` | `Bindings/AngelscriptAssetManagerBindingsTests.cpp` | invalid `PrimaryAssetId` 错误化、typed ctor、`LoadPrimaryAssetsWithType`、`Get/IsInitialized` | P1 |
| `P2.14` | `Bindings/AngelscriptJsonBindingsTests.cpp` | JSON builder/probe 对称性、primitive `TryGet*Field`、deterministic getter error path | P1 |

### 风险与注意事项增补（2026-04-09 01:33:43）

#### 风险

1. `FGameplayEffectSpec(null, ...)` 与 `GameplayTagBlueprintPropertyMap.Initialize(null, ...)` 从“原生断言或日志后返回”收紧成显式脚本错误后，仓内外若有人依赖当前宽松失败形态，会更早暴露问题。
   - 缓解：测试里固定错误文本，并在变更说明里明确这是把 crash/log-only path 前移为 bind-level contract。
2. `CanHashValue()` 恢复 `uint32 Hash() const` fallback 后，如果签名匹配条件写得过宽，可能把错误签名的脚本方法误判为可哈希。
   - 缓解：helper 只接受完整声明 `uint32 Hash() const`，同时补 wrong-signature compile-fail 回归。
3. `UAssetManager` 同时补 typed ctor、typed load、state helper 与 invalid-id error path，容易把“新增 surface”和“旧入口收紧”混在一起，调试时不易分辨是哪一层出了问题。
   - 缓解：测试把 typed ctor/load/state helper 与 invalid-id negative path 分栏；运行环境不足时至少保留 compile-surface 断言。
4. `Bind_Json.cpp` 恢复 primitive `TryGet*Field()` 后，脚本可能从“总是用 throwing getter”切到 probe-style API；如果 getter 错误路径仍残留未初始化返回值，问题会变得更隐蔽。
   - 缓解：同时固定 throwing getter 与 probe-style API 的双边界测试，确保两套合同都稳定可观测。

#### 已知行为变化

1. `FGameplayEffectSpec(null, ...)`、`GameplayTagBlueprintPropertyMap.Initialize(null, ...)` 将不再依赖原生断言或仅写日志，而会直接变成脚本错误。
2. `Math` 命名空间将重新暴露 `Abs/Sign/Min/Max/Square` 的 `int64` / `uint64` overload，现有 wide integer 脚本可直接按 UEAS2 习惯书写。
3. `UAssetManager` 将重新提供 `LoadPrimaryAssetsWithType(...)`、`UAssetManager::Get()`、`UAssetManager::IsInitialized()` 与 `FPrimaryAssetId(FPrimaryAssetType, FName)`；无效 `PrimaryAssetId` 也会从 silent no-op 变成显式脚本错误。
4. `FJsonArray` 将重新支持 bool / nested-array / nested-object builder；`FJsonObject` 将重新提供 primitive `TryGet*Field()`，而 primitive getter 的错误路径不再返回未初始化值。

---

## 深化 (2026-04-09 01:48:20)

### 本轮追加边界

- `Documents/Plans/Plan_AS238NonLambdaPort.md` 已承接 `TOptional` / `TStructType` / `__StaticType_*` 主线，本轮不重复 `Issue-78/84`。
- `Documents/Plans/Plan_UEBindGapRoadmap.md` 承接的是 UMG / 输入 / gameplay 的扩面主线；本轮只补 current 已公开 surface 的 correctness 与 parity，不新增新的类族 API。
- `Documents/Plans/Plan_TestCoverageExpansion.md` 主要处理广义 coverage 扩面；以下条目只补 bind contract 缺口，并为每条 contract 绑定一组 focused regression。

### Phase 1 再增补：收口 `FMemoryReader` 与 actor component query 的剩余 crash / stale-state 面

- [ ] **P1.14** 把 `Bind_FMemoryReader.cpp` 的短读与负长度路径统一收口成 `Throw + 默认返回值`
  - 当前 `FMemoryReader` 读 helper 同时存在两种错误失败形态：一类是 `ReadInt*` / `ReadUInt*` / `ReadFloat` / `ReadDouble` 在局部变量未初始化时直接 `*reader << Result`，短读只会留下 archive error flag，却把垃圾值继续返回脚本；另一类是 `ReadBytes(int Count)` / `ReadAnsiString(int Count)` 在分配前完全不检查 `Count < 0`，负长度会直接把脚本输入送进 `SetNumUninitialized()` 的 fatal 路径。
  - 这一项应把“读多少、是否还能读”前移成统一 helper，并把失败语义固定到 bind 层：所有数值型 reader 先零初始化结果并校验剩余字节，`ReadBytes()` / `ReadAnsiString()` 在分配前拒绝负长度与越界读取；底层 archive 即便仍置 `IsError()`，也只能转成固定脚本错误与 deterministic 默认返回值，而不是继续泄露半读数据。
  - 这里不扩新的 archive API，也不改 `FMemoryReader` 原生类本身；只修 current 已公开 script-facing 读取 contract。
  - 来源：
    - [A] `BindSystem_Analysis.md` — “发现 61：`FMemoryReader` 对负长度读取没有任何前置校验，脚本会直接触发 `TArray` fatal；短读也会把未初始化值继续回传”
    - [B] `DiscoveryPlans/BindSystem_Plan.md` — “Issue-24：`FMemoryReader` 读取 helper 缺少长度与 EOF 防线，短读会回传垃圾值，负长度会直接触发 fatal”
    - [C] `TestCoverage/BindSystem_TestGaps.md` — “`Bind_FMemoryReader.cpp` 位于当前未见直接对应测试的 bind 清单，现有 `Bindings/` 没有 dedicated memory-reader 回归”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FMemoryReader.cpp` L58-L125 — `ReadInt8()` 到 `ReadDouble()` 这组 reader 都是声明局部 `Result` 后直接 `*reader << Result`，没有默认初始化或剩余字节检查
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FMemoryReader.cpp` L128-L133 — `ReadBytes(int Count)` 直接 `Result.SetNumUninitialized(Count)`，没有 `Count < 0` 或 EOF guard
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FMemoryReader.cpp` L136-L141 — `ReadAnsiString(int Count)` 同样直接 `Buffer.SetNumUninitialized(Count)`，没有负长度或短读保护
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FMemoryReader.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMemoryReaderBindingsTests.cpp`
- [ ] **P1.14** 📦 Git 提交：`[AngelscriptRuntime] Fix: harden FMemoryReader read contracts`
- [ ] **P1.14-T** 单元测试：补齐 `FMemoryReader` 读 contract 的默认返回值与错误路径回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMemoryReaderBindingsTests.cpp`
  - 测试场景：
    - 正常路径：合法 buffer 上的 `ReadInt32()`、`ReadBytes(Count)`、`ReadAnsiString(Count)` 都返回与原生 `FMemoryReader` 基线一致的值
    - 边界条件：`ReadBytes(0)`、`ReadAnsiString(0)`、以及“刚好读到 EOF”的精确长度场景都返回确定结果，不产生额外脚本错误
    - 错误路径：截断 buffer 上的数值型读取返回 `0` / `0.0` 并记录固定脚本错误；`ReadBytes(-1)`、`ReadAnsiString(-1)` 记录脚本错误而不是触发 fatal
  - 测试命名：`Angelscript.TestModule.Bindings.MemoryReaderReadContracts`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`
- [ ] **P1.14-T** 📦 Git 提交：`[AngelscriptTest] Test: cover FMemoryReader read contracts`

- [ ] **P1.15** 为 `FMemoryReader::Seek()` / `Skip()` 补齐 `0..TotalSize()` 的定位 guard
  - 当前 `Seek(int InPos)` 与 `Skip(int Count)` 只拦上界，不拦负偏移。脚本把 reader 推到 `-1` 这类位置后，后续任何读取都可能落到 `Bytes[-1]` 一侧；这种问题不会先变成脚本错误，而是先变成 archive 内部的越界读或更远处的随机异常。
  - 这一项应把 archive 定位 contract 明确为闭区间 `0..TotalSize()`：`Seek()` 与 `Skip()` 共用同一个位置校验 helper，任何负位置、越过尾部的位置都先 `Throw(...)`，并保证失败时 `Tell()` 不被推进；同时把 `P1.14` 的读取校验 helper 与这里的位置 helper 收到同一文件内，避免未来再次把“定位合法”与“读取合法”拆成两套边界规则。
  - 这条任务只收口当前已公开的 script position API，不额外引入 rewind/peek/new reader family。
  - 来源：
    - [A] `BindSystem_Analysis.md` — “发现 53：`FMemoryReader::Seek/Skip` 只防上界不防负偏移，脚本可把 archive 定位到 `0` 之前并触发越界读”
    - [B] `DiscoveryPlans/BindSystem_Plan.md` — “Issue-56：`FMemoryReader::Seek/Skip` 应补 `0..TotalSize()` 闭区间防线”
    - [C] `TestCoverage/BindSystem_TestGaps.md` — “`Bind_FMemoryReader.cpp` 位于当前未见直接对应测试的 bind 清单，现有 `Bindings/` 没有 dedicated memory-reader 回归”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FMemoryReader.cpp` L38-L45 — `Seek(int InPos)` 只在 `InPos > reader->TotalSize()` 时报错，负位置会直接 `reader->Seek(InPos)`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FMemoryReader.cpp` L48-L55 — `Skip(int Count)` 只检查 `reader->Tell() + Count > reader->TotalSize()`，没有 `0` 下界
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FMemoryReader.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMemoryReaderBindingsTests.cpp`
- [ ] **P1.15** 📦 Git 提交：`[AngelscriptRuntime] Fix: validate FMemoryReader seek bounds`
- [ ] **P1.15-T** 单元测试：补齐 `Seek()` / `Skip()` 的下界与上界定位回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMemoryReaderBindingsTests.cpp`
  - 测试场景：
    - 正常路径：`Seek(0)`、`Seek(TotalSize())`、`Skip(0)`、合法正向 `Skip(N)` 后 `Tell()` 与后续读取结果都与原生基线一致
    - 边界条件：恰好跳到 `TotalSize()` 的定位允许发生，但后续继续读会回到 `P1.14` 的 deterministic 失败语义；失败后的 `Tell()` 保持在原位置
    - 错误路径：`Seek(-1)`、`Skip(-1)`、`Seek(TotalSize()+1)`、`Skip(TotalSize()+1)` 都必须记录固定脚本错误，且不会把 reader 推进到非法位置
  - 测试命名：`Angelscript.TestModule.Bindings.MemoryReaderPositionGuards`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`
- [ ] **P1.15-T** 📦 Git 提交：`[AngelscriptTest] Test: cover FMemoryReader seek and skip bounds`

- [ ] **P1.16** 统一 actor component query 的输出数组覆盖语义，收口 `GetComponentsByClass()` / `GetAllComponents()` 的 stale-result 累积
  - 当前 actor component query family 已经在两个入口上出现了相同的 out-array 契约漂移：`Bind_AActor.cpp` 的两个 `GetComponentsByClass(..., ?& OutComponents)` overload 都直接遍历并 `OutComponents.Add(Comp)`；`Bind_UActorComponent.cpp` 的 `GetAllComponents(...)` 也同样只追加结果。脚本一旦复用同一个数组，多次查询就会把旧组件与当前结果混在一起。
  - 这一项不重开 `GetComponentsByClassWithTag()` 或 actor generic deprecation 主线，只把现有 query helper 收口回稳定 out-parameter 语义：在参数校验全部通过后统一 `Reset()` 输出数组，再收集本次命中的组件；同时把 `NativeComponentMethods` 从“空数组 happy path”升级为“预填充数组 + 连续调用”的回归，避免现有测试继续把问题掩掉。
  - 这里建议把 `Bind_AActor.cpp` 与 `Bind_UActorComponent.cpp` 的“Reset + 遍历 + Add”逻辑抽成一个小 helper，防止后续再次只修一边。
  - 来源：
    - [A] `BindSystem_Analysis.md` — “发现 66：两个 `GetComponentsByClass(..., ?& OutComponents)` overload 都只追加结果，不会清空调用方数组”
    - [B] `DiscoveryPlans/BindSystem_Plan.md` — “Issue-63：`AActor::GetAllComponents` 成功路径不重置输出数组，复用查询会把旧组件混进新结果”
    - [C] `TestCoverage/BindSystem_TestGaps.md` — “Issue-71：`NativeComponentMethods` 先手动清空输出数组，掩盖了 `GetComponentsByClass` 是否会错误累积旧元素”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp` L80-L85 — 第一个 `GetComponentsByClass(..., ?& OutComponents)` overload 直接 `OutComponents.Add(Comp)`，没有 `Reset()`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp` L142-L147 — 第二个 `GetComponentsByClass(UClass ComponentClass, ?& OutComponents)` overload 同样只追加结果
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UActorComponent.cpp` L184-L203 — `FAngelscriptActorBinds::GetAllComponents(...)` 遍历 `OnActor->GetComponents()` 后直接 `OutComponents.Add(Comp)`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp` L151-L161 — 现有 `NativeComponentMethods` 在调用 `GetComponentsByClass(SceneComponents)` 前先 `Empty()`，且 `AllComponents` 路径只验证空数组 happy path
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UActorComponent.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Actor.h`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptActorComponentBindingsTests.cpp`
- [ ] **P1.16** 📦 Git 提交：`[AngelscriptRuntime] Fix: reset actor component query output arrays`
- [ ] **P1.16-T** 单元测试：补齐 actor component query 的 overwrite 语义回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptActorComponentBindingsTests.cpp`
  - 测试场景：
    - 正常路径：预填充哨兵元素的 `GetComponentsByClass()` / `GetAllComponents()` 调用后，输出数组只包含当前 actor 的真实组件，不保留旧元素
    - 边界条件：同一输出数组连续调用两次查询、以及 actor 组件集合发生变化后的再次查询，返回结果都严格等于“本次命中集”
    - 错误路径：`Actor == nullptr`、`ComponentClass == nullptr` 或类型不匹配时仍保持 current 的显式脚本错误，并且错误路径不应部分清空或部分污染调用方已有数组
  - 测试命名：`Angelscript.TestModule.Bindings.ActorComponentQueryOutputOverwrite`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_FULL()` + `FScopedTestWorldContextScope`
- [ ] **P1.16-T** 📦 Git 提交：`[AngelscriptTest] Test: cover actor component query output overwrite`

### Phase 2 再增补：恢复输入与 UMG 已公开 surface 的剩余 contract

- [ ] **P2.15** 恢复 `UInputSettings` 唯一命名 / 存在性查询 helper 的 `no_discard` 契约
  - 当前 `UInputSettings` bind 把“返回结果就是全部价值”的 helper 全部降成了普通返回值函数：`GetUniqueActionName()`、`GetUniqueAxisName()`、`DoesActionExist()`、`DoesAxisExist()` 失去 UEAS2 已有的 `no_discard` 后，脚本可以静默写出“看起来像配置动作、其实只是丢弃返回值”的空语句。current 自己新增的 `DoesSpeechExist()` 也一起成了契约例外。
  - 这一项应统一恢复输入配置查询 surface 的返回值语义：四条 UEAS2 对应 helper 重新标 `no_discard`，`DoesSpeechExist()` 作为 current 自己扩出来的查询入口也对齐同一规则；测试侧用 compile smoke 锁住“消费返回值可编译，丢弃返回值必须红灯”的双边界。
  - 这里不新增新的输入设置 API，也不改 `UInputSettings` 原生行为，只恢复 current 已公开 helper 的结果消费 contract。
  - 来源：
    - [A] `BindSystem_Analysis.md` — “发现 60：`UInputSettings` 唯一命名/存在性查询 helper 整组丢失 `no_discard`”
    - [B] `DiscoveryPlans/BindSystem_Plan.md` — “Issue-87：`UInputSettings` 的唯一命名/存在性查询 helper 丢掉 `no_discard`，返回值契约已经弱于 UEAS2”
    - [C] `TestCoverage/BindSystem_TestGaps.md` — “`Bind_UInputSettings.cpp` 位于当前未见直接对应测试的 bind 清单，输入子系统绑定仍缺 direct-hit 行为回归”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UInputSettings.cpp` L8-L9 — `GetUniqueActionName()` / `GetUniqueAxisName()` 当前都是普通返回值声明，没有 `no_discard`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UInputSettings.cpp` L17-L20 — `DoesActionExist()` / `DoesAxisExist()` / `DoesSpeechExist()` 同样没有 `no_discard`
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UInputSettings.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptInputSettingsBindingsTests.cpp`
- [ ] **P2.15** 📦 Git 提交：`[AngelscriptRuntime] Fix: restore UInputSettings nodiscard contracts`
- [ ] **P2.15-T** 单元测试：补齐 `UInputSettings` 查询 helper 的 `no_discard` 编译回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptInputSettingsBindingsTests.cpp`
  - 测试场景：
    - 正常路径：消费 `GetUniqueActionName()`、`GetUniqueAxisName()`、`DoesActionExist()`、`DoesSpeechExist()` 返回值的脚本片段都能编译通过，并与原生 `UInputSettings` 语义保持一致
    - 边界条件：`DoesSpeechExist()` 作为 current 新增入口，也必须与其余 bool 查询 helper 共享同一 `no_discard` 规则；重复 base name 时唯一命名 helper 仍返回新的 `FName`
    - 错误路径：显式丢弃 `GetUniqueActionName(...)`、`DoesActionExist(...)`、`DoesSpeechExist(...)` 的返回值必须产生编译期 diagnostics，而不是静默通过
  - 测试命名：`Angelscript.TestModule.Bindings.InputSettingsNoDiscardContracts`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`
- [ ] **P2.15-T** 📦 Git 提交：`[AngelscriptTest] Test: cover UInputSettings nodiscard contracts`

- [ ] **P2.16** 恢复 `Bind_FGeometry.cpp` 的 render-space 查询与 transformed-child 入口
  - 当前 `FGeometry` shard 已经不是“测试不够”这么简单，而是公开 surface 真实缩水：current 只剩 layout-space 的 `GetLocalSize()`、`GetAbsoluteSize()`、`AbsoluteToLocal()`、`LocalToAbsolute()`、`MakeChild()`；UEAS2 已有的 `GetRenderTransformScale()`、`GetRenderTransformTranslation()`、`GetAbsolutePosition()`、`MakeTransformedChild()` 整组缺失。
  - 这一项只恢复已确认的 parity surface，不展开更大的 UMG 扩面：按 current 现有 `FVector2D` / `FVector2f` 转换习惯补回三条 getter 和一条 render-space child 构造入口，并用 type-info + compile smoke 锁住声明存在性和签名正确性。
  - 这里不引入新的 widget harness，不要求首轮就构造真实 `FGeometry` 实例；优先把“API 是否存在、签名是否正确”固定下来，避免 UI 几何 surface 继续被误判成已对齐。
  - 来源：
    - [A] `BindSystem_Analysis.md` — “发现 70/71：`FGeometry` 丢失 render-transform 与 absolute-position 查询 helper，并缺少 `MakeTransformedChild()`”
    - [B] `DiscoveryPlans/BindSystem_Plan.md` — “Issue-55：`Bind_FGeometry.cpp` 丢失 render-space 查询与 transformed-child 构造入口”
    - [C] `TestCoverage/BindSystem_TestGaps.md` — “`Bind_FGeometry.cpp` 位于当前未见直接对应测试的 bind 清单，现有 `Bindings/` 对 UMG geometry 仍无 focused 回归”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FGeometry.cpp` L8-L35 — current 只注册了 `GetLocalSize()`、`GetAbsoluteSize()`、`AbsoluteToLocal()`、`LocalToAbsolute()`、`MakeChild()`，未见 `GetRenderTransformScale()`、`GetRenderTransformTranslation()`、`GetAbsolutePosition()`、`MakeTransformedChild()`
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FGeometry.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGeometryBindingsTests.cpp`
- [ ] **P2.16** 📦 Git 提交：`[AngelscriptRuntime] Fix: restore FGeometry render-space bindings`
- [ ] **P2.16-T** 单元测试：补齐 `FGeometry` render-space surface 的 parity / compile 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGeometryBindingsTests.cpp`
  - 测试场景：
    - 正常路径：`FGeometry` type info 中可见 `GetRenderTransformScale()`、`GetRenderTransformTranslation()`、`GetAbsolutePosition()`、`MakeTransformedChild()`，并且引用这些 API 的脚本片段可成功编译
    - 边界条件：恢复四条新方法后，既有 `MakeChild()`、`AbsoluteToLocal()`、`LocalToAbsolute()` 的 compile smoke 仍保持通过，不引入同名/重载冲突
    - 错误路径：若任一方法缺失、返回类型写错或参数签名漂移，compile smoke 必须直接在对应调用点红灯，而不是继续让其他 `FGeometry` happy path 掩盖问题
  - 测试命名：`Angelscript.TestModule.Bindings.GeometryRenderSpaceSurface`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`
- [ ] **P2.16-T** 📦 Git 提交：`[AngelscriptTest] Test: cover FGeometry render-space bindings`

### 单元测试总览增补（2026-04-09 01:48:20）

| 改进项 | 测试文件 | 测试重点 | 优先级 |
|--------|---------|---------|--------|
| `P1.14` | `Bindings/AngelscriptMemoryReaderBindingsTests.cpp` | 短读默认返回值、负长度 guard、`Throw + deterministic result` | P0 |
| `P1.15` | `Bindings/AngelscriptMemoryReaderBindingsTests.cpp` | `Seek/Skip` 下界与上界、失败后位置不漂移 | P0 |
| `P1.16` | `Bindings/AngelscriptActorComponentBindingsTests.cpp` | `GetComponentsByClass` / `GetAllComponents` overwrite 语义、错误路径不污染数组 | P1 |
| `P2.15` | `Bindings/AngelscriptInputSettingsBindingsTests.cpp` | `GetUnique*` / `Does*Exist` `no_discard`、speech query 对齐 | P1 |
| `P2.16` | `Bindings/AngelscriptGeometryBindingsTests.cpp` | `FGeometry` render-space surface 存在性、签名正确性、compile parity | P1 |

### 风险与注意事项增补（2026-04-09 01:48:20）

#### 风险

1. `FMemoryReader` 的失败路径会从“垃圾值继续传播 / 底层 fatal”收紧成显式脚本错误加默认返回值，可能让历史上依赖宽松行为的脚本更早暴露问题。
   - 缓解：测试同时固定错误文本、默认返回值和 `Tell()` 不漂移，避免后续回退成另一种不透明失败形态。
2. actor component query 改成 overwrite 语义后，错误依赖“复用数组会累积旧结果”的脚本会在功能上发生收紧。
   - 缓解：测试覆盖预填充数组、连续调用和错误路径三类场景，并在变更说明里明确这是恢复标准 out-parameter 语义。
3. `UInputSettings` 恢复 `no_discard` 后，现有脚本里把查询 helper 当副作用调用的路径会转成编译错误。
   - 缓解：先用 compile smoke 锁住正反两类脚本，再把变更说明同步到输入绑定相关文档或升级说明。
4. `FGeometry` 恢复缺失方法后，若仓内已有同名自定义 wrapper 或 mixin，可能出现重名或调用歧义。
   - 缓解：首轮测试必须覆盖 type-info 声明与 compile smoke，及时发现签名冲突，而不是只做方法存在性断言。

#### 已知行为变化

1. `FMemoryReader` 的负长度、短读、负偏移和越界定位将统一变成显式脚本错误；失败时只返回默认值，不再传播垃圾数据或触发底层 fatal。
2. `GetComponentsByClass()` 与 `GetAllComponents()` 在成功路径会覆盖输出数组，只返回本次查询结果，不再保留旧元素。
3. `UInputSettings` 的 `GetUniqueActionName()`、`GetUniqueAxisName()`、`DoesActionExist()`、`DoesAxisExist()`、`DoesSpeechExist()` 会重新要求消费返回值。
4. `FGeometry` 将重新暴露 `GetRenderTransformScale()`、`GetRenderTransformTranslation()`、`GetAbsolutePosition()`、`MakeTransformedChild()` 四条 render-space API。

---

## 深化 (2026-04-09 01:56:44)

### 本轮追加边界

- `Documents/Plans/Plan_AS238NonLambdaPort.md` 已承接 `struct __StaticType_*` / `TStructType` 恢复主线；本轮只补 current 仍在使用的 **class** `__StaticType_*` regression owner，不重复 struct lane。
- `Documents/Plans/Plan_HazelightBindModuleMigration.md`、`Documents/Plans/Plan_UnrealCSharpArchitectureAbsorption.md`、`Documents/Plans/Plan_BindParallelization.md` 已覆盖更大的 generator / shard / module 迁移主线；本轮只补 `BindModules.Cache` 的 authority、root、version 与 legacy mode contract，不重开整套生成器重构。
- 现有 `P3.1` 已承接 report-only `BindingCoverageManifest` 与 provider ownership 基线；以下 `P3.3` 只补 `Enum / Delegate / GlobalHelper` family 完整性，`P3.4` 只补 legacy lane authority 与 artifact root 收口，不重复 `P3.1` 的基础 manifest 落地。

### Phase 2 深化：补齐 class static-type regression owner

- [ ] **P2.17** 把 class `__StaticType_*` 回归从单样本 `AActor` 检查提升为独立的 multi-type contract
  - current `Bind_BlueprintType.cpp` 会为每个已绑定 class 生成 `const TSubclassOf<UObject> __StaticType_<TypeName>` global，但测试侧仍把这条 contract 混在 `AngelscriptClassBindingsTests.cpp` 的单样本 happy-path 里，只验证 `__StaticType_AActor`。这样 `U*` 前缀类型、generated script class、以及 follow-up plain module 里的 symbol 可见性都没有独立 owner；一旦命名规则或注册时机只在非 `AActor` 路径上漂移，现有回归仍会全绿。
  - 这一项只强化 current 仍存在的 class static-type lane，不重复 `Plan_AS238NonLambdaPort.md` 已接管的 struct static-type 恢复工作。实现上应把 `NativeStaticTypeGlobal` 从共享引擎样本拆成 dedicated regression owner，补上 `USceneComponent` 与 generated class 两类样本，并显式清理模块/生成类残留，避免再次把共享引擎残留误当成 `__StaticType_*` contract 本身。
  - 来源：
    - [A] `BindSystem_Analysis.md` — “发现 40：当前测试只锁住了 `__StaticType_AActor` 的 class happy path，static-type coverage 仍停留在单样本”
    - [B] `DiscoveryPlans/BindSystem_Plan.md` — “Issue-45：现有 class binding 测试只验证 namespace `StaticClass()` 和 `__StaticType_AActor`，没有把 `__StaticType_*` 样本面做实”
    - [C] `TestCoverage/BindSystem_TestGaps.md` — “Issue-81 / NewTest-52：`NativeStaticTypeGlobal` 只抽样 `__StaticType_AActor`，缺少 `U*` 前缀与 generated class 的 static type globals 回归”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp` L697-L701 — `BindStaticClass()` 当前对每个 bound type 都生成 `const TSubclassOf<UObject> __StaticType_<TypeName>`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp` L453-L472 — `NativeStaticTypeGlobal` 只断言 `__StaticType_AActor`，并仍运行在 `ASTEST_CREATE_ENGINE_SHARE()` 上
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptStaticTypeBindingsTests.cpp`
- [ ] **P2.17** 📦 Git 提交：`[AngelscriptTest] Refactor: isolate static type global regression ownership`
- [ ] **P2.17-T** 单元测试：补齐 class `__StaticType_*` 的跨前缀与 generated-class regression
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptStaticTypeBindingsTests.cpp`
  - 测试场景：
    - 正常路径：同一测试同时验证 `__StaticType_AActor` 与 `__StaticType_USceneComponent`，其 `Get()` 结果分别精确等于原生 `StaticClass()`
    - 边界条件：先通过 `CompileAnnotatedModuleFromMemory` 生成 `ABindingStaticTypeGenerated`，再在 follow-up plain module 中读取 `__StaticType_ABindingStaticTypeGenerated`，确认 `FindClass()`、`GetDefaultObject()`、`IsChildOf(AActor::StaticClass())` 与生成类基线一致
    - 错误路径：引用不存在的 `__StaticType_NotExisting` 或在显式丢弃 generated module 后继续读取旧 symbol 时，必须在引用点产生 deterministic compile/runtime 失败，不能再被共享引擎残留掩盖
  - 测试命名：`Angelscript.TestModule.Bindings.StaticTypeGlobalsMultiTypeCompat`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + `ON_SCOPE_EXIT` 清理 generated / plain modules
- [ ] **P2.17-T** 📦 Git 提交：`[AngelscriptTest] Test: cover multi-type static type globals`

### Phase 3 深化续补：收口 artifact family 与 legacy lane authority

- [ ] **P3.3** 在 `P3.1` 的 `BindingCoverageManifest` 基线上，把 `Enum / Delegate / GlobalHelper` 升格为可 join 的一等 artifact family
  - current 的 bind artifact 仍然是 family 裂开的：`Binds.Cache` 只序列化 `Classes` / `Structs`，`UEnum` 和 delegate 最多只在 `.Headers` sidecar 留 header link，而 `NewObject`、`SpawnActor`、`__DelegateSignature` 这类高频 global / namespace helper 甚至没有持久化描述。结果是即便 `P3.1` 落了 provider ownership，`Enum / Delegate / GlobalHelper` 仍然无法和 `Class / Struct` 一样进入同一份可审计合同。
  - 这一项应显式建立 family-complete artifact sidecar，但不替换旧 cache：在 `AngelscriptBindDatabase` 或等价 manifest 层新增 `ArtifactKind / ScriptDeclaration / OwnerOrNamespace / ProviderName / ObjectPath / HeaderPath` 级别的条目，让 `Bind_UEnum.cpp`、`Bind_Delegates.cpp`、`BindGlobalFunction()` 成功注册后都能留下同一份可 join 的 artifact entry；runtime 第一阶段只把它用于 docs、coverage、dump、provider audit，不立即反过来驱动真实注册。
  - 这里优先选三类样本收口：`Bind_UEnum.cpp` 的 enum lookup surface、`Bind_Delegates.cpp` 的 delegate signature family、以及 `Bind_UObject.cpp` / `Bind_AActor.cpp` 的 `NewObject` / `SpawnActor` 级 global helper。只要这三类能进入同一份 artifact catalog，后续 bind gap 与外部 provider 审计才不需要继续 family-specific grep。
  - 来源：
    - [C] `TestCoverage/BindSystem_TestGaps.md` — “NewTest-54：`Bind_UEnum.cpp` 完全无直测，说明 enum family 仍游离在 bind contract 与回归主线之外”
    - [D] `ArchitectureReview/BindingPipeline_ArchReview.md` — “Arch-BP-18：持久化 binding contract 只把 `Class/Struct` 当一等公民，`UEnum / delegate / global helper` 仍停留在 live-only side lane”
    - [E] `ReferenceComparison/GapAnalysis.md` — “第一阶段 report-only `BindingProviderManifest` 至少应显式记录 `Families` 与 `OwnerSymbols`，先让 owner 可见”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp` L27-L30 — `Serialize()` 当前只序列化 `Structs` 与 `Classes`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp` L82-L93 — `BoundEnums` / `BoundDelegateFunctions` 只在保存 `.Headers` sidecar 时参与输出
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UEnum.cpp` L382-L384 — enum 在 live pass 中 `Register(MakeShared<FEnumType>(Enum))` 后只追加到 `BoundEnums`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.cpp` L432-L439 — delegate 也是 live pass 注册后只追加到 `BoundDelegateFunctions`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h` L431-L435、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp` L176-L181 — `FBindInfo` / `GetBindInfoList()` 目前仍只有 `BindName / BindOrder / bEnabled`，无法表达 `ArtifactKind` 或 `OwnerSymbol`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp` L556-L579、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp` L450-L467 — `NewObject` / `SpawnActor` 家族只通过 `BindGlobalFunction(...)` live 注册，没有对应 DB artifact
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UEnum.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindingArtifactCatalogTests.cpp`
- [ ] **P3.3** 📦 Git 提交：`[AngelscriptRuntime] Refactor: extend binding artifact catalog across families`
- [ ] **P3.3-T** 单元测试：补齐 `Enum / Delegate / GlobalHelper` 的 artifact catalog family 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindingArtifactCatalogTests.cpp`
  - 测试场景：
    - 正常路径：artifact catalog 同时包含 `Class / Struct / Enum / Delegate / GlobalFunction / NamespaceFunction` 的代表样本，且 `ProviderName`、`OwnerOrNamespace`、`ArtifactKind` 可直接 join
    - 边界条件：`Bind_UEnum`、`Bind_Delegates`、`NewObject`、`SpawnActor` 与旧 `Class / Struct` 项共存时，family 计数与 manifest / dump 对齐；legacy cache 缺失时 report-only sidecar 仍可稳定生成
    - 错误路径：重复 `ArtifactKey`、缺失 `OwnerSymbol`、或 `ArtifactKind` 与声明形态不一致时，验证器必须给出 deterministic failure / `ReasonCode`，而不是 silent 覆盖
  - 测试命名：`Angelscript.TestModule.Core.BindingArtifactCatalogFamilies`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`
- [ ] **P3.3-T** 📦 Git 提交：`[AngelscriptTest] Test: cover binding artifact catalog families`

- [ ] **P3.4** 把 legacy bind module lane 从隐式默认 authority 收口成显式 mode，并统一 `BindModules.Cache` 的 root / version / provider 身份
  - 当前 legacy lane 的问题不是“完全没退役”，而是 authority 还停在半迁移状态：editor 菜单已经把 `GenerateNativeBinds()` 标成 `Legacy Native Bind Generator (Debug Only)`，但 runtime 仍会无条件读取 `BindModules.Cache` 并把它当成正式输入；同时 editor 写到 project script root，runtime 却从 plugin root 读，cache 文件本身又只是纯字符串数组，既没有 schema/version，也没有 root/provider tag。
  - 更隐蔽的问题是 runtime identity 仍然不稳定：editor 生成出来的 legacy module `StartupModule()` 只是 `RegisterBinds((int32)EOrder::Late, [](){...})`，没有显式 bind name；而 `FBind(int32 BindOrder, ...)` 最终又会退化成 `UnnamedBind_N`。这意味着即使 manifest/日志里看到 legacy module 被加载了，也很难稳定回答“具体是哪一个 generated provider 在负责这段注册”。
  - 这一项不做整套 shard/generator 重写，只收口 contract：新增 `LegacyBindModuleMode`（`Disabled / Explicit / Auto`）、共享的 binding artifact locator、带 `SchemaVersion / RootTag / ProviderId / GeneratedBy` 的 `BindModules.Cache` 头信息，以及 legacy provider 的稳定 bind identity。runtime 只在 mode 允许且 metadata 匹配时消费 legacy lane；主线 authority 继续向 `AngelscriptUHTTool` / unified manifest 收敛。
  - 来源：
    - [D] `ArchitectureReview/BindingPipeline_ArchReview.md` — “Arch-BP-15：`BindModules.Cache` 的产物根目录在 editor 生成与 runtime 加载之间分裂”；“Arch-BP-22：legacy bind module lane 与 UHT 主线并存，authority 不清晰”
    - [E] `ReferenceComparison/GapAnalysis.md` — “`BindModules.Cache` 的写入根和读取根不一致；`generated file -> manifest -> runtime bind` 还没有 single producer key”
    - [E] `ReferenceComparison/Hazelight_Analysis.md` — “[维度 D1/D11] `BindModules.Cache` 仍处在 owner 迁移半途：save 在 root[0]，load 在 plugin base”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp` L729-L730 — editor 菜单已明确标注 `Legacy Native Bind Generator (Debug Only)`，并说明 `AngelscriptUHTTool` 才是 primary path
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp` L1077 — editor 仍把 `BindModules.Cache` 写到 `FAngelscriptEngine::GetScriptRootDirectory() / "BindModules.Cache"`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L781-L793 — `GetScriptRootDirectory()` 当前固定返回 `AllRootPaths[0]`，即 game project root
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L1469-L1477 — runtime 先从 script root 读 `Binds.Cache`，再从 `plugin->GetBaseDir() / "BindModules.Cache"` 读 legacy bind module list
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h` L583-L601 — `SaveBindModules()` / `LoadBindModules()` 仍是纯字符串数组读写，且源码里还留着 “write to base directory” 的 `TO-DO`
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp` L1324-L1326、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h` L455-L458、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp` L138-L153 — generated legacy module 只按 `BindOrder` 注册，最终会退化成 `UnnamedBind_N`
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindModuleAuthorityTests.cpp`
- [ ] **P3.4** 📦 Git 提交：`[AngelscriptRuntime] Refactor: make legacy bind module authority explicit`
- [ ] **P3.4-T** 单元测试：补齐 legacy bind module lane 的 mode / root / version authority 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindModuleAuthorityTests.cpp`
  - 测试场景：
    - 正常路径：`LegacyBindModuleMode=Auto` 且 cache metadata 匹配时，runtime 仍能加载 legacy modules，并把命中的 `ProviderId / RootTag / Mode` 写入 dump 或 manifest
    - 边界条件：`Disabled / Explicit / Auto` 三种模式下，plugin-root 与 script-root 双写 cache 的优先级与 fallback 顺序稳定可观测；仅 UHT lane 存在时主线初始化不受 legacy lane 缺失影响
    - 错误路径：缺失 schema version、root tag 不匹配、或 provider identity 退化成匿名 `UnnamedBind_N` 时，runtime 必须给出 deterministic warning / 拒绝，而不是继续 silent auto-load
  - 测试命名：`Angelscript.TestModule.Core.LegacyBindModuleAuthority`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + 测试专用临时 artifact root
- [ ] **P3.4-T** 📦 Git 提交：`[AngelscriptTest] Test: cover legacy bind module authority`

### 单元测试总览增补（2026-04-09 01:56:44）

| 改进项 | 测试文件 | 测试重点 | 优先级 |
|--------|---------|---------|--------|
| `P2.17` | `Bindings/AngelscriptStaticTypeBindingsTests.cpp` | `__StaticType_*` 跨 `A* / U* / generated class` 样本、follow-up module 可见性、错误 symbol 红灯 | P1 |
| `P3.3` | `Core/AngelscriptBindingArtifactCatalogTests.cpp` | `Enum / Delegate / GlobalHelper` artifact family、join key、reason ledger | P1 |
| `P3.4` | `Core/AngelscriptBindModuleAuthorityTests.cpp` | legacy lane mode、`BindModules.Cache` root/version/provider 身份、拒绝 stale cache | P1 |

### 风险与注意事项增补（2026-04-09 01:56:44）

#### 风险

1. `P2.17` 把 class static-type 回归从共享引擎样本拆到 clean isolation 后，仓内若有用例隐式依赖上一段 generated class 残留，会被这次收口更早暴露。
   - 缓解：新测试必须显式创建/丢弃 generated module，并把“symbol 仍可见”与“引擎残留还在”拆成两条断言，避免再把污染当成 contract。
2. `P3.3` 为 `Enum / Delegate / GlobalHelper` 新增 artifact catalog 后，短期最容易出现的是 manifest 与 live bind 计数不一致，尤其是 namespace helper 与 global helper 的 key 设计如果不稳，会再次落回“同名不同义”。
   - 缓解：首阶段只做 report-only sidecar，并在测试中同时对齐 manifest、dump、runtime observation 三份产物；任何重复 key 都必须产出 `ReasonCode` 而不是静默覆盖。
3. `P3.4` 把 legacy lane 从 silent auto-load 收紧成显式 mode + version/root gate 后，本地仍依赖旧 `BindModules.Cache` 调试流程的环境会更早暴露 stale artifact 问题。
   - 缓解：第一阶段保留 `Auto` 兼容模式，并提供 dual-read/dual-write 与命中日志；等 telemetry 稳定后再考虑把默认值迁到 `Explicit`。
4. 为 legacy generated module 引入稳定 `ProviderId` / bind name 后，现有 dump、日志或脚本化排障工具如果写死了 `UnnamedBind_N` 形态，可能需要同步更新预期。
   - 缓解：测试同时锁住旧排序不变和新身份字段可见，确保变化只发生在 provenance，可观测性增强而非执行顺序回归。

#### 已知行为变化

1. class `__StaticType_*` 的 regression 将不再只依赖 `__StaticType_AActor` 单样本；`U*` 前缀类型、generated class 和 stale symbol 都会进入同一组 focused 自动化。
2. `BindingCoverageManifest` 的后续深化产物将开始显式列出 `Enum / Delegate / GlobalFunction / NamespaceFunction`，而不再只有 `Class / Struct` 能进入统一 artifact 视图。
3. legacy bind module lane 将从“只要有 `BindModules.Cache` 就默认尝试加载”逐步收紧为带 mode、root、version、provider 身份的显式 authority contract。

---

## 深化 (2026-04-09 02:14:09)

### 本轮追加边界

- `Plan_TestCoverageExpansion.md` 负责广义测试扩面，但没有把 `Bind_UEnum.cpp` 提升成 dedicated behavior owner；本轮只补 enum direct contract，不展开 `FGuid` / `FQuat` 这类目前仅由测试维度单点命中的条目。
- `Plan_BindParallelization.md`、`Plan_UnrealCSharpArchitectureAbsorption.md` 已覆盖更大的 shard / generator / 吸收路线；本轮只补 current runtime 仍缺的 `preflight`、`provenance` 与 `phase` contract，不重开整套生成器改造。
- `P3.1` / `P3.3` 已承接 manifest 与 artifact family；以下条目只补它们尚未展开的 strict issue ledger、source context、以及 `Bind_UEnum` 的 direct behavior regression owner。

### Phase 2 深化：补齐 `Bind_UEnum` 的 direct behavior contract

- [ ] **P2.18** 给 `Bind_UEnum.cpp` 建立 direct lookup parity regression owner，避免 `P3.3` 只补 artifact 不补行为
  - 当前 `Bind_UEnum.cpp` 已公开一整组 lookup / display-name / max-value surface，但现有计划里真正承接的是 `P3.3` 的 artifact family 可见性；如果没有 dedicated behavior owner，后续即使 manifest 能看到 enum family，`GetValueByName()`、`GetNameByValue()`、`GetDisplayNameTextByValue()` 这类脚本真正调用的 contract 仍然可能继续处于“live-only 且无人锁定”的状态。
  - 这一项不去重开 enum 生成架构，而是把 `Bind_UEnum` 的 runtime contract 从“存在于代码里”升级成“有专门 owner 的行为账本”：新增独立 `AngelscriptEnumBindingsTests.cpp`，用原生 `UEnum` 基线精确比对脚本 lookup 结果，并让 `P3.3` 后续的 artifact catalog 复用同一枚举样本，避免 catalog 测试被误当成行为覆盖。
  - 来源：
    - [C] `TestCoverage/BindSystem_TestGaps.md` — “NewTest-54：为完全无直测的 `Bind_UEnum.cpp` 建立 enum lookup parity 回归”
    - [D] `ArchitectureReview/BindingPipeline_ArchReview.md` — “Arch-BP-18：持久化 binding contract 只把 `Class/Struct` 当一等公民，`UEnum` 仍停留在 live-only side lane”
    - [E] `ReferenceComparison/GapAnalysis.md` — “UnrealCSharp 的 generator 把 class/struct/enum/binding 串在统一 stage，enum 不应继续停留在 test-orphan / live-only 状态”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UEnum.cpp` L358-L385 — `Bind_Enums` 仍以 live pass 逐个注册 `UEnum`，并只把结果追加到 `BoundEnums`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UEnum.cpp` L488-L499 — `GetNameByValue()`、`GetValueByName()`、`GetNameStringByValue()`、`GetDisplayNameTextByValue()`、`GetMaxEnumValue()` 已公开，但当前仓内没有对应的 direct behavior owner
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UEnum.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptEnumBindingsTests.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindingArtifactCatalogTests.cpp`
- [ ] **P2.18** 📦 Git 提交：`[AngelscriptTest] Refactor: give Bind_UEnum a dedicated behavior owner`
- [ ] **P2.18-T** 单元测试：补齐 `Bind_UEnum.cpp` 的 lookup / display-name / miss-path parity 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptEnumBindingsTests.cpp`
  - 测试场景：
    - 正常路径：选取一个稳定 engine enum，对 `GetNameByValue()`、`GetValueByName()`、`GetNameStringByValue()`、`GetDisplayNameTextByValue()`、`GetMaxEnumValue()` 做脚本结果与原生 `UEnum` 基线逐项比对
    - 边界条件：验证 `0` 值、带前缀名字与 display-name path 都保持当前原生语义；同一枚举样本可被 `P3.3` 的 artifact catalog 回归复用
    - 错误路径：不存在的名称、无效 value、以及 wrong-format 名称查找都必须返回与原生 `UEnum` 一致的 miss 结果，不能再被其他混合 smoke 掩盖
  - 测试命名：`Angelscript.TestModule.Bindings.EnumLookupParity`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`
- [ ] **P2.18-T** 📦 Git 提交：`[AngelscriptTest] Test: cover Bind_UEnum lookup parity`

### Phase 3 深化续补：把 pipeline contract 从“可观察”推进到“可校验”

- [ ] **P3.5** 在 `P3.1` 的 manifest 基线上补一条 runtime `BindPreflight` / issue-ledger，先把 silent registration drift 显式化
  - 当前 `P3.1` 已计划把 provider / lane / reason 落进 manifest，但 runtime 注册本身仍是“直接改 live engine，再希望 `PreviousBind` 能顺利补 metadata”。这使 `invalid FunctionId`、duplicate policy、metadata attach 失败 这些最关键的 bind 质量问题，仍然可能只留下 AngelScript 返回码或完全静默跳过，无法在 bind 当下形成统一 issue ledger。
  - 这一项不改变第一阶段的 commit 时机，只把 registration contract 前移成 report-first：定义 `FAngelscriptBindDraft` / `FAngelscriptBindIssue` / `EAngelscriptBindConflictPolicy`，让 `Method()`、`BindGlobalFunction()`、`Enum()`、`AddFunctionEntry()` 先把返回码与冲突类型转成结构化 issue；Editor/Test 再打开 strict 模式，专门把 `OnBind()` 无法拿到有效函数、`PreviousBind` 元数据没附着成功、或 duplicate 被 silent 吃掉的场景显式报出来。
  - 来源：
    - [A] `BindSystem_Analysis.md` — “发现 4/5/7：construction safety、`no_discard`、`__WorldContext` 等 contract 曾在 bind core 里静默漂移，说明 current 缺少统一的 bind-time 失败账本”
    - [D] `ArchitectureReview/BindingPipeline_ArchReview.md` — “Arch-BP-19：注册 API 直接写入 AngelScript engine，返回码与冲突策略没有进入统一 preflight lane”
    - [E] `ReferenceComparison/GapAnalysis.md` — “共享 `ReasonCode / CapabilityLedger / SymbolDecision`，先让 runtime/UHT/docs/debug 能统一解释为什么没注册/没生效”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp` L288-L289、L591-L617 — `RegisterObjectMethod()` / `RegisterGlobalFunction()` 仍直接注册到 live engine 后立刻 `OnBind(...)`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp` L409-L433 — `OnBind()` 只有在 `GetFunctionById(FunctionId)` 成功时才补 user data / protected / property，但无论成功与否都会写回 `PreviouslyBoundFunction = FunctionId`
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Testing/AngelscriptBindExecutionObservation.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Testing/AngelscriptBindExecutionObservation.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindPreflightTests.cpp`
- [ ] **P3.5** 📦 Git 提交：`[AngelscriptRuntime] Refactor: add bind preflight issue ledger`
- [ ] **P3.5-T** 单元测试：补齐 bind preflight 的 duplicate / invalid-id / metadata-attach 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindPreflightTests.cpp`
  - 测试场景：
    - 正常路径：合法 method、global function、enum 和 function-table entry 都能生成空 issue 或 `Accept` 结果，并保持现有 runtime 行为
    - 边界条件：`asALREADY_REGISTERED`、重复 `AddFunctionEntry()`、以及 `WarnAndSkip` 型冲突都能生成稳定 `ReasonCode` / `ConflictPolicy`
    - 错误路径：无效 `FunctionId`、`PreviousBind` 元数据未附着、或 duplicate 被 silent 覆盖时，Editor/Test strict preflight 必须产出 deterministic issue 并使自动化红灯
  - 测试命名：`Angelscript.TestModule.Core.BindPreflightIssues`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`
- [ ] **P3.5-T** 📦 Git 提交：`[AngelscriptTest] Test: cover bind preflight issue ledger`

- [ ] **P3.6** 把 bind contributor provenance 从 `BindName` 升格为 `SourceContext`，填满 runtime dump 的 `BindModule` 空列
  - 当前 `P3.1` 只计划到 provider / lane 可见，但 runtime 自己仍回答不了“这个 bind 到底来自哪个文件、哪个 generated shard、哪个 provider”。只要继续停留在 `BindName + BindOrder + bEnabled` 三元组，`BindModule` 空列、`UnnamedBind_N`、generated shard 排障和外部 provider 注入都会继续处于贫血状态。
  - 这一项应给每个 contributor 增加最小 `FAngelscriptBindSourceContext`：至少包含 `ProviderName`、`ModuleName`、`SourceFile`、`SourceLine`、`DeclaringSymbol`、`ArtifactLane`。第一阶段只补轻量 provenance，不改排序；让 `FBindInfo`、execution observation、state dump 和 manifest 共用同一份 source context，先把“来源可追踪”补齐。
  - 来源：
    - [D] `ArchitectureReview/BindingPipeline_ArchReview.md` — “Arch-BP-20：注册队列里的贡献者是 opaque lambda，dump 与测试快照无法回答 ‘这个 bind 来自哪里’”
    - [E] `ReferenceComparison/GapAnalysis.md` — “runtime `FBindInfo` 只有 `BindName / BindOrder / bEnabled`，`BindModule` 列当前为空，runtime provenance 仍无法回答 origin + reason”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp` L120-L124 — `FBindFunction` 仍只有 `BindName`、`BindOrder`、`Function`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp` L176-L183 — `GetBindInfoList()` 仍只回填 `BindName`、`BindOrder`、`bEnabled`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp` L853-L867 — dump 已声明 `BindModule` 列，但写行时仍固定输出空字符串
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Testing/AngelscriptBindExecutionObservation.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Testing/AngelscriptBindExecutionObservation.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindProvenanceTests.cpp`
- [ ] **P3.6** 📦 Git 提交：`[AngelscriptRuntime] Refactor: add bind source provenance context`
- [ ] **P3.6-T** 单元测试：补齐 bind provenance / dump source-context 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindProvenanceTests.cpp`
  - 测试场景：
    - 正常路径：手写 named bind、generated shard bind、以及测试 provider bind 都能导出 `ProviderName / ModuleName / ArtifactLane / SourceFile`
    - 边界条件：匿名 bind 仍保留现有执行顺序，但 dump / snapshot 至少能稳定回溯到 `SourceFile:Line` 或等价 hash；旧消费者读取新增字段时保持兼容
    - 错误路径：缺失 `SourceContext`、空 `BindModule`、或多条记录 provenance key 冲突时，验证器必须给出 deterministic failure，而不是继续落空列
  - 测试命名：`Angelscript.TestModule.Core.BindProvenanceContext`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`
- [ ] **P3.6-T** 📦 Git 提交：`[AngelscriptTest] Test: cover bind provenance context`

- [ ] **P3.7** 把 magic `BindOrder` 与 append-only `TypeFinder` 外显成 resolved phase / priority contract
  - current 的 bind 执行图仍主要靠魔法数字与注册先后隐式维持：`Bind_Enums` 占 `Early-1`，`Bind_BlueprintType_Declarations` 用 `Early`，wrapper family 夹在 `Late-10`，反射默认值再跑 `Late+100`，actor helper 又额外占 `Late+150`；与此同时 `RegisterTypeFinder()` 只是 append，`GetByProperty()` 先命中谁就返回谁。继续扩 family 或引入外部 provider 时，维护者仍然只能靠猜顺序插槽。
  - 这一项不要求一次性改写所有 `Bind_*.cpp`，而是先把 today 的隐式顺序外显成 resolved plan：新增 `EAngelscriptBindPhase` / `FAngelscriptBindStepDescriptor` / finder priority，旧 `int32 BindOrder` 与旧 `RegisterTypeFinder()` 继续保留为兼容层；先让 `Bind_UEnum`、`Bind_BlueprintType`、`Bind_AActor` 与一组测试 provider 进入同一张 phase snapshot，避免后续每多一个 family 就再次赌静态初始化顺序。
  - 来源：
    - [D] `ArchitectureReview/BindingPipeline_ArchReview.md` — “Arch-BP-21：绑定阶段依赖被 `BindOrder` 偏移量与 `TypeFinder` 注册顺序隐式编码”
    - [E] `ReferenceComparison/GapAnalysis.md` — “`TypeFinders + TypesImplementingProperties` 仍是 append-order first-match；应先补 explicit priority，再继续扩 family”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp` L114-L117、L151-L158 — `RegisterTypeFinder()` 仍直接 `Add`，`GetByProperty()` 对 `TypeFinders` 仍按 append 顺序 first-match 返回
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UEnum.cpp` L358-L385 — `Bind_Enums` 仍占用 `EOrder::Early-1`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp` L712-L727、L1756-L1760 — `Bind_BlueprintType_Declarations` / `Bind_Defaults` / wrapper late bind 仍分别编码在 `Early`、`Late+100`、`Late-10`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp` L25-L27、L286-L288 — `Bind_AActor_Base` 与 `Bind_Actors` 仍分别硬编码在 `Late-1` 与 `Late+150`
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UEnum.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindPhasePlanTests.cpp`
- [ ] **P3.7** 📦 Git 提交：`[AngelscriptRuntime] Refactor: make bind phase and finder priority explicit`
- [ ] **P3.7-T** 单元测试：补齐 resolved phase plan / finder priority 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindPhasePlanTests.cpp`
  - 测试场景：
    - 正常路径：resolved phase plan 稳定复现 today 的关键顺序：`Bind_Enums` → `Bind_BlueprintType_Declarations` → wrapper late binds → `Bind_Defaults` → `Bind_Actors`
    - 边界条件：高优先级测试 `TypeFinder` 能在不依赖静态初始化顺序的前提下覆盖或让位于内建 finder，并把命中来源写入 snapshot
    - 错误路径：phase 依赖环、未解析依赖、或 finder priority / ownership 冲突时，Editor/Test 必须给出 deterministic failure，而不是继续 silently 取 first-match
  - 测试命名：`Angelscript.TestModule.Core.BindPhaseAndFinderPriority`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`
- [ ] **P3.7-T** 📦 Git 提交：`[AngelscriptTest] Test: cover bind phase and finder priority`

### 单元测试总览增补（2026-04-09 02:14:09）

| 改进项 | 测试文件 | 测试重点 | 优先级 |
|--------|---------|---------|--------|
| `P2.18` | `Bindings/AngelscriptEnumBindingsTests.cpp` | `UEnum` lookup / display-name / miss-path parity，避免 artifact 测试冒充行为覆盖 | P1 |
| `P3.5` | `Core/AngelscriptBindPreflightTests.cpp` | registration issue ledger、duplicate policy、invalid `FunctionId`、metadata attach 失败 | P1 |
| `P3.6` | `Core/AngelscriptBindProvenanceTests.cpp` | `BindModule` / `SourceFile` / `ArtifactLane` provenance、dump 空列收口 | P1 |
| `P3.7` | `Core/AngelscriptBindPhasePlanTests.cpp` | resolved phase plan、finder priority、phase/finder 冲突诊断 | P1 |

### 风险与注意事项增补（2026-04-09 02:14:09）

#### 风险

1. `P2.18` 把 `Bind_UEnum` 从“没有直测”升级成精确 parity 回归后，现有 engine enum 的 display-name / miss-path 差异会更早暴露，尤其是本地化文本或 editor-only display-name 分支。
   - 缓解：首批样本选择稳定 engine enum，并始终用原生 `UEnum` 即时基线对比，而不是把字符串常量写死在脚本里。
2. `P3.5` 的 strict preflight 很可能第一次就打亮当前仓内已有的 duplicate、metadata attach 失败或 legacy silent skip。
   - 缓解：第一阶段保持 runtime report-only，先在 Editor/Test fail；等 issue taxonomy 稳定后再讨论是否扩大到更多配置。
3. `P3.6` 一旦给 dump / snapshot 增加 provenance 字段，外部解析脚本如果写死了旧列布局或默认 `BindModule` 为空，短期会出现兼容性摩擦。
   - 缓解：优先新增列而不是重排旧列，并保留旧字段名；同时用自动化锁住列顺序与 fallback 语义。
4. `P3.7` 把 phase / finder priority 显式化后，隐藏在静态初始化顺序里的历史依赖会被更早暴露；如果直接改执行顺序，容易带来大范围 bind 回归。
   - 缓解：首阶段只做 legacy-int 映射与 plan snapshot，不主动改变 today 的 resolved order；任何顺序变化都必须先由测试快照显式确认。

#### 已知行为变化

1. `Bind_UEnum` 的 lookup / display-name / miss-path 将拥有 standalone regression owner，不再只依赖 artifact catalog 或混合 smoke 间接覆盖。
2. Editor/Test 模式下，invalid `FunctionId`、duplicate policy、`PreviousBind` 元数据未附着成功等注册问题会开始以结构化 issue 暴露，而不是继续 silent drift。
3. runtime bind dump / snapshot 将开始输出 `BindModule`、`SourceFile`、`SourceLine`、`ArtifactLane` 等 provenance 字段，而不是只剩 `BindName`。
4. 新增或覆盖 `TypeFinder` / bind family 时，将逐步转向显式 priority / phase contract，而不是继续依赖静态初始化先后作为唯一 authority。

---
## 深化 (2026-04-09 02:30)

本轮只追加当前 `Plan_BindSystem.md` 尚未承接、且已确认不与其他活跃 Plan 主线重复的剩余 bind correctness / contract 条目：

- `Documents/Plans/Plan_TestCoverageExpansion.md` 只把 `Bind_UDataTable.cpp` 归类为“零测试”资产，不覆盖这里的 `GetRows()` 未初始化槽位与 `AddRow()` silent no-op 运行时合同。
- `Documents/Plans/Plan_AngelscriptLearningTraceTests.md` 与 `Documents/Plans/Plan_ScriptExamplesExpansion.md` 会触达 source metadata 的教学/示例面，但不处理 `UASFunction` stale handle、multi-file source path 与 hot-reload line fallback 的 runtime 正确性。
- `Documents/Plans/Plan_UFunctionReflectiveFallbackBinding.md` 关注 `UFunction` 反射调用后端，不覆盖这里的 `FindFunctionByName()` / `IsFunctionImplementedInScript()` 对 stale `UASFunction` 的 discoverability contract。

### Phase 1 深化：补齐 hot-reload 与 DataTable 的剩余 correctness 断层

- [ ] **P1.17** 收口 stale `UASFunction` 的发现与调用语义，禁止 hot-reload 后继续“能找到但不会执行”
  - 当前 `Bind_UClass` 暴露出来的 `IsFunctionImplementedInScript()` / `FindFunctionByName()` 仍会把已 cleanup 的 `UASFunction` 当成可用结果，而真正调用路径 `AngelscriptCallFromBPVM()` / `AngelscriptCallFromParms()` 又在 `ScriptFunction == nullptr` 时直接 `return`。这会把脚本、调试器和自动化同时拖进假阳性状态：函数看起来还在，执行时却 silent no-op。
  - 这一项应先抽统一的 `IsLiveScriptFunction()` / `ReportStaleScriptFunction()` helper，把“发现链”和“执行链”都收口到同一判断：`IsFunctionImplementedInScript()` 与 `FindFunctionByName()` 只对 live script function 返回正例；一旦命中 stale function，调用路径必须记录精确错误并把返回槽 / out 槽写回 deterministic 默认值，而不是继续把旧栈内容或“什么都没发生”伪装成成功。
  - 这里不重开 interface/callable 主线，也不改变 native `UFunction` 的正常行为；只修 hot-reload / discard 后 script function 的 bind contract。
  - 来源：
    - [A] `BindSystem_Analysis.md` — “发现 46/49：失效 `UASFunction` 仍被视为‘已实现’，stale `ProcessEvent` / thunk 调用只会 silent no-op”
    - [B] `DiscoveryPlans/BindSystem_Plan.md` — “Issue-14/36：`FindFunctionByName` / `IsFunctionImplementedInScript` 应过滤 tombstone function，stale call 必须报显式错误并写默认返回值”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` L149-L156 — `AngelscriptCallFromBPVM()` 在 `ASFunction->ScriptFunction == nullptr` 时直接 `return`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` L475-L482 — `AngelscriptCallFromParms()` 同样在 `ScriptFunction == nullptr` 时直接 `return`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` L979-L984 — `IsFunctionImplementedInScript()` 只检查 `Cast<UASFunction>` 与 outer class，没有校验 `ScriptFunction`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp` L311-L313 — `FindFunctionByName()` 仍直接返回 `Class->FindFunctionByName(...)`
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp`
- [ ] **P1.17** 📦 Git 提交：`[AngelscriptRuntime] Fix: reject stale script functions in lookup and call paths`
- [ ] **P1.17-T** 单元测试：补齐 stale `UASFunction` 的 discoverability / 调用失败回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp`
  - 测试场景：
    - 正常路径：live script function 在 reload 前仍能让 `IsFunctionImplementedInScript()` 返回 `true`，`FindFunctionByName()` 返回可执行 `UASFunction`
    - 边界条件：soft reload / discard 后，旧 `UASFunction` 不再被 discoverable，当场重新查询到的新函数仍保持可执行
    - 错误路径：保留旧 `UFunction*` 并显式触发一次 stale `ProcessEvent` / helper call 时，必须记录固定错误并返回 deterministic 默认值，而不是 silent no-op
  - 测试命名：`Angelscript.TestModule.HotReload.StaleScriptFunctionContracts`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + compile/reload helper
- [ ] **P1.17-T** 📦 Git 提交：`[AngelscriptTest] Test: cover stale script function discovery and call contracts`

- [ ] **P1.18** 收口 `Bind_UDataTable.cpp` 的 row contract，禁止 `GetRows()` 暴露未初始化槽位并让 `AddRow()` 在类型不匹配时显式失败
  - 当前 `Bind_UDataTable.cpp` 在同一文件里叠了两条 data corruption / silent failure 路径：`FDataTableCategoryHandle::GetRows()` 先按匹配集收集 `Matches`，却仍按整表 `RowMap.Num()` 扩容输出数组，只初始化命中项；`UDataTable::AddRow()` 则在 row struct 不匹配时直接静默跳过。两者共同的问题都不是“缺少 API”，而是公开 surface 已经失去最基础的结果契约。
  - 这一项应把 DataTable row family 收回到“匹配数决定输出容量，类型错误必须可见”这条合同：`GetRows()` 的扩容与初始化统一改成 `Matches.Num()`，0 命中时直接返回；`AddRow()` 改用 `GetStructTypeOrThrow(...)` 或等价 helper，把错误 row struct 直接升级成脚本错误，并保证表内容不被污染。
  - 这里不重写 `FindRow()` / `GetAllRows()` 语义，也不扩更多 DataTable helper；只修 current 已经暴露的数组越界语义与 silent no-op。
  - 来源：
    - [B] `DiscoveryPlans/BindSystem_Plan.md` — “Issue-50/51：`FDataTableCategoryHandle::GetRows` 会把未初始化 struct 槽位暴露给脚本，`UDataTable::AddRow` 在 row struct 不匹配时直接静默跳过”
    - [C] `TestCoverage/BindSystem_TestGaps.md` — “`Bind_UDataTable.cpp` 仍位于当前未见 direct-hit 的 bind 清单，现有测试树没有 dedicated DataTable 绑定回归”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UDataTable.cpp` L122-L127 — `AddRow()` 只有 `GetStructType(...) != nullptr` 才写表，类型不匹配时直接 silent skip
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UDataTable.cpp` L204-L225 — `GetRows()` 收集 `Matches` 后仍执行 `OutArray.Insert(Index, RowMap.Num(), ...)`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UDataTable.cpp` L14-L22 — `GetStructType()` 仅返回 `nullptr`，没有任何错误路径或诊断
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UDataTable.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptDataTableBindingsTests.cpp`
- [ ] **P1.18** 📦 Git 提交：`[AngelscriptRuntime] Fix: harden DataTable row contracts`
- [ ] **P1.18-T** 单元测试：补齐 `UDataTable` / `FDataTableCategoryHandle` 的行容量与类型校验回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptDataTableBindingsTests.cpp`
  - 测试场景：
    - 正常路径：匹配 row struct 的 `AddRow()` 正常写入表；category 过滤后的 `GetRows()` 返回精确匹配行数与正确 struct 内容
    - 边界条件：0 命中 category 时输出数组保持空；部分命中时 `Num()` 严格等于 `Matches.Num()`，不会夹带额外默认/垃圾槽位
    - 错误路径：把错误 row struct 传给 `AddRow()` 时必须记录固定脚本错误，且 `GetRowNames()` / `FindRow()` 结果不被污染
  - 测试命名：`Angelscript.TestModule.Bindings.DataTableRowContracts`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`
- [ ] **P1.18-T** 📦 Git 提交：`[AngelscriptTest] Test: cover DataTable row size and row-type contracts`

### Phase 2 深化：把 source metadata 从单文件 happy-path 提升为可依赖合同

- [ ] **P2.19** 精确化 source metadata 的 file / line contract，并把现有 `SourceMetadataCompat` 升级成可重复夹具
  - 当前 source metadata 链路已经持有足够的真实信息，却没有真正用起来：`UASClass::GetSourceFilePath()` / `UASFunction::GetSourceFilePath()` 都硬编码返回 `Module->Code[0]`，`UASFunction::GetSourceLineNumber()` 在 `ScriptFunction == nullptr` 时又直接退回 `-1`，尽管 `GeneratedSourceLineNumber` 已经缓存了最后一次生成行号。结果是 multi-file module 会系统性跳错文件，hot-reload 后的旧函数句柄也会平白丢掉 line 信息。
  - 这一项应把 source metadata 收口成“以声明 section 为准、以 generated line 为 fallback”的统一合同：路径解析改用 `scriptSectionIdx` / `declaredAt` 对应的 code section，stale `UASFunction` 在 live metadata 不可用时回退到 `GeneratedSourceLineNumber`；同时把现有 `SourceMetadataCompat` 从固定工程路径、固定模块名、硬编码行号，升级成 `Saved/Automation` 下的唯一临时文件夹 + 动态期望值，避免无关排版或残留文件继续制造假红灯。
  - 这里不额外引入 path map，也不重写 editor navigation；只修 runtime 已经暴露的 metadata getter 与测试夹具，使 editor navigation / debugger 能继续复用同一份真实 source identity。
  - 来源：
    - [A] `BindSystem_Analysis.md` — “发现 47/48：`GetSourceFilePath()` 只返回模块首文件，`GeneratedSourceLineNumber` 已写入却没有读取路径”
    - [B] `DiscoveryPlans/BindSystem_Plan.md` — “Issue-48/49：应按 section index 解析真实 source file，并让 stale function 的 `GetSourceLineNumber()` 回退到 `GeneratedSourceLineNumber`”
    - [C] `TestCoverage/BindSystem_TestGaps.md` — “Issue-84/35/63：`SourceMetadataCompat` 固定工程路径、模块名断言过宽、函数行号硬编码为 `6`，没有形成稳定的 metadata owner”
    - [E] `ReferenceComparison/CrossComparison.md` — “当前 editor source navigation 与 debugger 共用 `UASFunction::GetSourceFilePath()` / `GetSourceLineNumber()` 这条 metadata 链”；`ReferenceComparison/Hazelight_Analysis.md` — “generated function 应保留 source file path / source line number”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` L1497-L1522 — `UASClass::GetSourceFilePath()` / `GetRelativeSourceFilePath()` 仍直接返回 `Module->Code[0]`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` L1535-L1545 — `UASFunction::GetSourceFilePath()` 同样固定返回 `Module->Code[0]`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` L1548-L1556 — `GetSourceLineNumber()` 在 `ScriptFunction == nullptr` 或 `scriptData == nullptr` 时直接返回 `-1`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h` L124 — `GeneratedSourceLineNumber = -1` 已存在，但当前 getter 未使用
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp` L280-L284、L405-L417 — `UClass` / `UFunction` 的 source metadata accessor 直接暴露上述 getter
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.h`
    - `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSourceNavigationTests.cpp`
- [ ] **P2.19** 📦 Git 提交：`[AngelscriptRuntime] Fix: resolve precise source metadata for classes and functions`
- [ ] **P2.19-T** 单元测试：补齐 multi-file source metadata / stale-line fallback / 唯一路径夹具回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSourceNavigationTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.h`，`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp`
  - 测试场景：
    - 正常路径：双文件 module 中声明在第二个文件里的 class / function，`GetSourceFilePath()`、`GetScriptModuleName()`、`GetScriptFunctionDeclaration()` 与 editor navigation 都返回精确基线
    - 边界条件：测试资产落到 `Saved/Automation` 唯一路径并动态计算期望行号；`ScriptFunction == nullptr` 的旧 `UASFunction` 仍能通过 `GeneratedSourceLineNumber` 返回最后一次有效行号
    - 错误路径：无效 section index、缺失 code section 或已 discard module 时，getter 必须稳定返回空路径 / `-1`，而不是继续悄悄 fallback 到首文件
  - 测试命名：`Angelscript.TestModule.Bindings.SourceMetadataPrecision`，`Angelscript.TestModule.Editor.SourceNavigation.MultiFilePrecision`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + `Saved/Automation` 唯一路径夹具
- [ ] **P2.19-T** 📦 Git 提交：`[AngelscriptTest] Test: cover source metadata precision and stale line fallback`

### 单元测试总览增补（2026-04-09 02:30）

| 改进项 | 测试文件 | 测试重点 | 优先级 |
|--------|---------|---------|--------|
| `P1.17` | `HotReload/AngelscriptHotReloadFunctionTests.cpp`，`Bindings/AngelscriptClassBindingsTests.cpp` | stale `UASFunction` discoverability、stale call 默认返回值与显式错误 | P0 |
| `P1.18` | `Bindings/AngelscriptDataTableBindingsTests.cpp` | `GetRows()` 精确容量、`AddRow()` 类型错误显式失败 | P0 |
| `P2.19` | `Bindings/AngelscriptFileAndDelegateBindingsTests.cpp`，`Editor/AngelscriptSourceNavigationTests.cpp`，`Shared/AngelscriptTestEngineHelper.*` | multi-file source metadata、stale line fallback、唯一路径夹具 | P1 |

### 验收标准增补（2026-04-09 02:30）

1. hot-reload / discard 后，旧 `UASFunction` 不再被 `IsFunctionImplementedInScript()` / `FindFunctionByName()` 误判为可执行；若仍持有旧句柄并触发调用，必须得到 deterministic 错误与默认返回值。
2. `UASClass` / `UASFunction` 的 source metadata 能在 multi-file module 中返回真实声明文件；stale `UASFunction` 在 live metadata 不可用时仍可返回最后一次生成行号。
3. `FDataTableCategoryHandle::GetRows()` 的输出数组长度严格等于匹配行数，不再泄露未初始化 struct 槽位；`UDataTable::AddRow()` 对错误 row struct 会立即报脚本错误，且表内容保持不变。

### 风险与注意事项增补（2026-04-09 02:30）

#### 风险

1. `P1.17` 会把一批历史上“调用看似成功、其实 silent no-op”的 stale function 用法提前打亮成错误。
   - 缓解：测试同时锁住 live function 正例与 stale function 负例，避免把合法 hot-reload 场景一起误伤。
2. `P1.18` 修复后，少量错误依赖 `GetRows()` 额外槽位数量或 `AddRow()` 静默失败的脚本会显式行为改变。
   - 缓解：以 dedicated DataTable regression 固定“匹配行数即数组长度、类型错误即报错”这一新基线，并避免同时改写其它 DataTable helper。
3. `P2.19` 把 source metadata 测试切到临时唯一路径与动态行号后，旧测试夹具里隐含的固定模块名 / 固定脚本路径假设会更早暴露。
   - 缓解：先把 helper 收口成统一入口，再让 existing bindings/editor tests 共用同一套 `Saved/Automation` 夹具，避免每个测试各自拼路径。

#### 已知行为变化

1. stale `UASFunction` 将不再继续被 `FindFunctionByName()` 暴露，也不会在调用时 silent no-op；旧句柄会进入显式错误路径。
2. `UASClass` / `UASFunction` 的 `GetSourceFilePath()` / `GetSourceLineNumber()` 将开始返回更精确的多文件结果，旧的“总是首文件 / 固定行号”假设将失效。
3. `UDataTable::AddRow()` 对错误 row struct 会从 silent skip 变成显式脚本错误；`FDataTableCategoryHandle::GetRows()` 的 `Num()` 将收紧到真实匹配数。

---
## 深化 (2026-04-09 06:36)

本轮只追加 `Bind_UWorld.cpp` 当前 Plan 尚未显式承接的 nullable world-handle contract：

- `P2.1` 已覆盖 `__WorldContext` 的 canonical public surface 与 hidden-argument trait，但还没有把 `GetCurrentWorld()` 自身的 `no_discard` / `RequiresWorldContext` 合同，以及 `UWorld` / `ULevel` receiver 在 null-world 链上的崩溃面收口成同一条执行项。
- `P2.4` 已覆盖 `WorldType` / `SetGameInstance()` / `GFrameNumber` 的只读与 owner 边界，但不处理“公开允许返回 `null` 的 world handle，后续 helper 却继续裸解引用”的链式调用断层。
- `Documents/Plans/Plan_TestCoverageExpansion.md` 只把 `Bind_UWorld.cpp` 归类为高频但缺专项测试，不覆盖这里的 `GetCurrentWorld()` `no_discard` 与 null-receiver 行为合同。

### Phase 2 深化：补齐 nullable world handle 的剩余 contract

- [ ] **P2.20** 收口 `Bind_UWorld.cpp` 的 nullable world-handle contract，阻断 `GetCurrentWorld()` 链式调用的裸解引用
  - 当前 `GetCurrentWorld()` 明确使用 `EGetWorldErrorMode::ReturnNull` 把“没有当前 world”公开成合法结果，但同文件 `GetGameState()`、`IsStartingUp()`、`IsTearingDown()`、`GetGameInstance()`、`GetLevelScriptActor()`、`GetPersistentLevel()`，以及 `ULevel::GetLevelScriptActor()`、`IsVisible()`、`IsBeingRemoved()`、`GetActors()` 仍全部按非空 receiver 直接解引用。再叠加 `GetCurrentWorld()` 仍缺 UEAS2 已有的 `no_discard`，当前脚本很容易写出 `GetCurrentWorld().GetPersistentLevel().GetActors().Num()` 这种看似自然、实则在无 world-context 时直接崩到 bind lambda 的链式调用。
  - 这一项应把 world-handle contract 一次性收口：`GetCurrentWorld()` 恢复 `no_discard`，并在注册后显式回填 `SetPreviousBindRequiresWorldContext(true)`；`Bind_UWorld.cpp` 内新增 `ResolveWorldReceiverOrThrow()` / `ResolveLevelReceiverOrThrow()` 之类的最小 helper，把 `UWorld` / `ULevel` instance helper 的 null receiver 统一改成“报精确脚本错误 + 返回 deterministic 默认值或空后备”，不再让 nullable surface 与裸解引用并存。
  - 这里不重开 `WorldType` 只读、`SetGameInstance()` 禁写或 `ULevel::GetActors()` 过滤语义主线，只处理当前最基础的 world-handle 正确性：调用者要么显式消费 `GetCurrentWorld()`，要么在 chain 断点得到稳定错误，而不是未定义行为。
  - 来源：
    - [A] `Documents/AutoPlans/BindSystem_Analysis.md` — “发现 25/75：`GetCurrentWorld()` 丢失 `no_discard`，且其 nullable 结果与同文件 `UWorld` helper 的裸 receiver 解引用发生契约冲突”
    - [C] `Documents/AutoPlans/TestCoverage/BindSystem_TestGaps.md` — “NewTest-28 只建议补 world globals happy path；当前测试树唯一直接链式调用 `GetWorld().GetPersistentLevel().GetActors().Num()`，没有无 world-context 负例”
    - [E] `Documents/AutoPlans/ReferenceComparison/Hazelight_Analysis.md` — “UEAS2 仍把 `GetCurrentWorld()` 暴露为 `no_discard` world helper；current 已把 world-context owner 收缩到 active engine，因此更需要把函数级 contract 补齐，而不是继续依赖调用方自行回避空 world”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UWorld.cpp` L38-L42 — `GetCurrentWorld()` 仍注册为普通 `UWorld GetCurrentWorld()`，直接 `ReturnNull`，没有 `no_discard`，也没有任何 metadata 回填
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UWorld.cpp` L54-L85 — `GetGameState()`、`IsStartingUp()`、`IsTearingDown()`、`GetGameInstance()`、`GetLevelScriptActor()`、`GetPersistentLevel()` 全部直接解引用 `World`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UWorld.cpp` L92-L109 — `ULevel::GetLevelScriptActor()`、`IsVisible()`、`IsBeingRemoved()`、`GetActors()` 同样直接解引用 `Level`
    - `Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemScenarioTests.cpp` L175-L181 — 当前现有脚本只锁 `GetWorld().GetPersistentLevel().GetActors().Num()` happy path，没有无 world-context 负例
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UWorld.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_WorldContextHelpers.h`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptWorldBindingsTests.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemScenarioTests.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptControlFlowTests.cpp`
- [ ] **P2.20** 📦 Git 提交：`[AngelscriptRuntime] Fix: harden nullable world handle contracts`
- [ ] **P2.20-T** 单元测试：补齐 `GetCurrentWorld()` `no_discard` / null-receiver world chain 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptWorldBindingsTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemScenarioTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptControlFlowTests.cpp`
  - 测试场景：
    - 正常路径：在 `FScopedTestWorldContextScope` 下，`GetCurrentWorld()`、`GetGameState()`、`GetPersistentLevel()`、`GetLevelScriptActor()`、`GetActors()` 与 C++ 同时刻基线一致，`GetCurrentWorld().GetPersistentLevel().GetActors().Num()` 继续通过
    - 边界条件：world-context 被清空后，脚本显式判空 `UWorld World = GetCurrentWorld(); if (World is null) return 1;` 仍可稳定执行；`GetCurrentWorld()` 同时具备 `no_discard` 与 `asTRAIT_USES_WORLDCONTEXT`
    - 错误路径：在无 world-context 的模块里直接链式调用 `GetCurrentWorld().GetPersistentLevel()`、`GetCurrentWorld().GetGameState()`、`GetCurrentWorld().GetPersistentLevel().GetActors().Num()`，以及对空 `ULevel` 调 `GetActors()`，都必须得到 deterministic 脚本错误而不是崩溃；忽略 `GetCurrentWorld()` 返回值必须产生 `no_discard` diagnostics
  - 测试命名：`Angelscript.TestModule.Bindings.WorldHandleContract`，`Angelscript.TestModule.Subsystem.WorldHandleNullSafety`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_FULL()` + `FScopedTestWorldContextScope`
- [ ] **P2.20-T** 📦 Git 提交：`[AngelscriptTest] Test: cover nullable world handle contracts`

### 单元测试总览增补（2026-04-09 06:36）

| 改进项 | 测试文件 | 测试重点 | 优先级 |
|--------|---------|---------|--------|
| `P2.20` | `Bindings/AngelscriptWorldBindingsTests.cpp`，`Subsystem/AngelscriptSubsystemScenarioTests.cpp`，`Core/AngelscriptBindConfigTests.cpp`，`Angelscript/AngelscriptControlFlowTests.cpp` | `GetCurrentWorld()` `no_discard`、`RequiresWorldContext`、null-world chain 与 `UWorld` / `ULevel` receiver 保护 | P1 |

### 验收标准增补（2026-04-09 06:36）

1. `GetCurrentWorld()` 在脚本签名面恢复 `no_discard`，并被自动化确认带 `asTRAIT_USES_WORLDCONTEXT`。
2. 无 world-context 时，`GetCurrentWorld()` 仍可返回 `null`，但链式调用 `GetPersistentLevel()` / `GetGameState()` / `GetActors()` 不再崩溃，而是返回 deterministic 错误与后备值。
3. 现有 `GetWorld().GetPersistentLevel().GetActors().Num()` happy path 保持通过，不因 null guard 引入行为回退。

### 风险与注意事项增补（2026-04-09 06:36）

#### 风险

1. `P2.20` 会把部分历史上依赖“直接 chain 不判空”的脚本提早打成显式错误。
   - 缓解：保留 `GetCurrentWorld() == null` 的合法判空用法，只把真正的 null-receiver 链式调用改成 deterministic 失败。
2. `ULevel::GetActors()` 如使用静态 empty fallback，需要同时与 `P2.5` 的过滤 / ownership 调整保持兼容，避免后续再出现返回引用悬挂。
   - 缓解：优先抽 shared fallback helper，并让 `P2.5` 继续拥有 actors 列表过滤语义，不在本项提前改变过滤规则。

#### 已知行为变化

1. 忽略 `GetCurrentWorld()` 返回值将从当前 silent pass 变成 compile diagnostic。
2. `GetCurrentWorld().GetPersistentLevel()` 等 null-world chain 将从潜在 crash 变成显式脚本错误与后备返回，而不是继续沿 receiver 裸解引用。

---
## 深化 (2026-04-09 06:41)

本轮只追加当前 `Plan_BindSystem.md` 仍未承接、且 `Documents/Plans/` 中未见独立活跃 Plan 覆盖的两条 bind contract：

- `Bind_FString.cpp` 仍存在索引/删改 contract 断层，现有 `StringRemoveAtCompat` 只锁住两个 happy path。
- `Bind_FNumberFormattingOptions.cpp` 仍停留在“命名变量可链式调用”的弱覆盖，temporary builder contract 既没恢复，也没有 dedicated regression。

`Plan_TestCoverageExpansion.md` 负责广义测试扩面，但不处理这里的 runtime bind contract 修复；因此本轮只把这两处 source-verified 剩余缺口追加到 `BindSystem` 主计划。

### Phase 1 深化：补齐字符串 family 的索引与删改合同

- [ ] **P1.19** 收口 `Bind_FString.cpp` 的索引/删改 contract，避免越界路径跌回底层 `RangeCheck`
  - 当前同文件已经出现了明确的 file-internal contract 分叉：两个 `opIndex()` overload 都会在非法 index 时走 `FAngelscriptEngine::Throw("String index out of bounds.")`，但公开 `IsValidIndex()` 的脚本声明却仍写成 `void`，`RemoveAt()` 也仍然直接下放给底层 `String.RemoveAt(Index, Count)`。这意味着脚本作者面对的是三套不同语义：读索引会得到脚本错误，判索引没有可消费的布尔返回，删索引则可能直接跌回底层 `RangeCheck`。
  - 这一项应把 `Bind_FString.cpp` 的最小公开合同收口回同一标准：把 `IsValidIndex()` 的脚本声明修正为真实的 `bool` 返回值；为 `RemoveAt()` 增加与 `opIndex()` 一致的前置 guard，把非法 `Index/Count` 收口成 deterministic 脚本错误与 no-op，而不是把错误继续放大成更底层的容器检查；同时把字符串 family 的 regression owner 从 `UtilityBindingsTests.cpp` 的单个 smoke case 拆出来，避免“看起来已经有覆盖、实际上只锁住两个 happy path”的错觉继续存在。
  - 这里不重开整份 `Bind_FString.cpp` 的大规模 API 扩面，也不重写 `Left/Right/Mid/Split/Replace/ParseIntoArray/Format` 的实现；重点只修当前最容易把脚本输入错误升级成 runtime assert 的边界合同，并让 dedicated regression 真正覆盖 `IsValidIndex` / `opIndex` / `RemoveAt` / substring 这组公开 surface。
  - 来源：
    - [A] `Documents/AutoPlans/BindSystem_Analysis.md` — “发现 57：`FString::RemoveAt` 越界路径丢了脚本级边界检查，会直接跌回底层 `RangeCheck`”
    - [C] `Documents/AutoPlans/TestCoverage/BindSystem_TestGaps.md` — “Issue-85 / NewTest-57：`StringRemoveAtCompat` 只覆盖两个 happy path，应为 `IsValidIndex` / `opIndex` / substring 建立 dedicated regression owner”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FString.cpp` L196-L217 — 两个 `opIndex()` overload 已经对非法 index 走 `Throw + InvalidChar`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FString.cpp` L230-L233 — `IsValidIndex()` 当前仍注册为 `void IsValidIndex(int Index) const`，`RemoveAt()` 仍直接 `String.RemoveAt(Index, Count)`，没有任何前置 guard
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptUtilityBindingsTests.cpp` L295-L335 — 现有 `StringRemoveAtCompat` 只断言 `"ABCDE"` 上的两个合法 `RemoveAt()` happy path，没有任何越界或 `IsValidIndex` 行为回归
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FString.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptUtilityBindingsTests.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptStringBindingsTests.cpp`
- [ ] **P1.19** 📦 Git 提交：`[AngelscriptRuntime] Fix: harden FString index and remove contracts`
- [ ] **P1.19-T** 单元测试：为字符串索引/删改建立 dedicated regression owner
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptStringBindingsTests.cpp`
  - 测试场景：
    - 正常路径：`IsValidIndex(0)` / `IsValidIndex(4)` 返回 `true`；`Value[1]` 读取 `'B'`、写回后字符串精确变成 `"AZCDE"`；`RemoveAt(1, 2)` 仍得到 `"ADE"`；`Left(2)` / `Right(2)` / `Mid(1, 3)` / `Split("CD", OutLeft, OutRight)` / `Replace("BC", "XY")` 全部与原生 `FString` 结果逐项一致
    - 边界条件：`IsValidIndex(5)` 返回 `false`；`RemoveAt(0, 0)` 保持 no-op；`ParseIntoArray` 与 `Format` 用最小参数集继续作为“未被本项误伤”的兼容哨兵
    - 错误路径：`Value[99]`、`Value.RemoveAt(99, 1)`、`Value.RemoveAt(-1, 1)`、`Value.RemoveAt(1, -1)` 都必须进入 deterministic 脚本错误，而不是底层 assert、崩溃或 silent corruption
  - 测试命名：`Angelscript.TestModule.Bindings.StringIndexAndSliceCompat`，`Angelscript.TestModule.Bindings.StringRemoveAtGuardCompat`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`
- [ ] **P1.19-T** 📦 Git 提交：`[AngelscriptTest] Test: cover FString index and remove contracts`

### Phase 2 深化：恢复 `FNumberFormattingOptions` 的 temporary builder contract

- [ ] **P2.21** 恢复 `Bind_FNumberFormattingOptions.cpp` fluent setter 的 `accept_temporary_this` contract
  - 当前 `SetAlwaysSign()`、`SetUseGrouping()`、`SetRoundingMode()`、`SetMinimumIntegralDigits()`、`SetMaximumIntegralDigits()`、`SetMinimumFractionalDigits()`、`SetMaximumFractionalDigits()` 七个 setter 都仍返回 `FNumberFormattingOptions&`，但脚本声明已经丢掉 UEAS2 原本具备的 `accept_temporary_this`。结果是 current 只保留了“命名变量可链式调用”的半套 builder surface，而 `FNumberFormattingOptions().SetAlwaysSign(true).SetUseGrouping(false)`、`FNumberFormattingOptions::DefaultWithGrouping().SetUseGrouping(false)` 这类 temporary receiver 写法已经没有合同保障。
  - 这一项应把 fluent setter contract 恢复到与其返回类型相符的最低标准：七个 setter 统一补回 `accept_temporary_this`，让 temporary builder 与 named-variable builder 重新等价；测试侧则不再只看 `IsIdentical()` / `GetTypeHash()`，而是把 setter 结果送进 `FText::AsNumber` 做 native baseline 对齐，并专门锁住“从 `DefaultWithGrouping()` 派生 temporary builder 不会反向污染默认工厂结果”这一边界。
  - 这里不新增新的 `FNumberFormattingOptions` API，也不修改 `DefaultWithGrouping()` / `DefaultNoGrouping()` 的默认值实现；重点只是把 current 已公开的 fluent builder surface 从“看起来像 builder、实际上只支持 lvalue”恢复成真正可依赖的 contract。
  - 来源：
    - [A] `Documents/AutoPlans/BindSystem_Analysis.md` — “发现 59：`FNumberFormattingOptions` fluent setter 丢了 `accept_temporary_this`，builder-style temporary 在 current 已失效”
    - [C] `Documents/AutoPlans/TestCoverage/BindSystem_TestGaps.md` — “Issue-24 / NewTest-11：现有 `NumberFormattingOptionsCompat` 只验证 copy/hash/default factory，应改为 setter -> `FText::AsNumber` 的可观察语义回归”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FNumberFormattingOptions.cpp` L35-L41 — 七个 fluent setter 当前全部缺少 `accept_temporary_this`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCoreMiscBindingsTests.cpp` L188-L203 — 现有测试只覆盖命名变量链式调用、`Copy`、`GetTypeHash()` 与 `DefaultWithGrouping()/DefaultNoGrouping()`，没有任何 temporary receiver 或格式化输出断言
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FNumberFormattingOptions.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCoreMiscBindingsTests.cpp`
- [ ] **P2.21** 📦 Git 提交：`[AngelscriptRuntime] Fix: restore number formatting temporary builder contracts`
- [ ] **P2.21-T** 单元测试：补齐 temporary builder 与格式化输出的 dedicated regression
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCoreMiscBindingsTests.cpp`
  - 测试场景：
    - 正常路径：命名变量链 `Options.SetAlwaysSign(true)...` 与 temporary builder `FNumberFormattingOptions().SetAlwaysSign(true)...` 都能成功编译，并且把同一数字传给 `FText::AsNumber` 后，与 C++ 侧原生 `FNumberFormattingOptions` 基线输出完全一致
    - 边界条件：`FNumberFormattingOptions::DefaultWithGrouping().SetUseGrouping(false)` 能作为 temporary builder 使用，但不会反向污染 `DefaultWithGrouping()` / `DefaultNoGrouping()` 后续取值；`SetMinimumIntegralDigits(2)` / `SetMaximumFractionalDigits(3)` 等组合在 chaining 后仍保留正确字段写入
    - 错误路径：若 future regression 再次移除 `accept_temporary_this`，`FNumberFormattingOptions().SetAlwaysSign(true).SetUseGrouping(false)` 与 `FNumberFormattingOptions::DefaultWithGrouping().SetUseGrouping(false)` 这两类 compile smoke 必须直接红灯，而不是被 copy/hash 级别的弱断言继续放过
  - 测试命名：`Angelscript.TestModule.Bindings.NumberFormattingOptionsFormattingCompat`，`Angelscript.TestModule.Bindings.NumberFormattingTemporaryBuilderCompat`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`
- [ ] **P2.21-T** 📦 Git 提交：`[AngelscriptTest] Test: cover number formatting temporary builders`

### 单元测试总览增补（2026-04-09 06:41）

| 改进项 | 测试文件 | 测试重点 | 优先级 |
|--------|---------|---------|--------|
| `P1.19` | `Bindings/AngelscriptStringBindingsTests.cpp` | `IsValidIndex` 返回值、`opIndex` / `RemoveAt` 边界、防止 `RangeCheck` 泄漏、substring parity | P0 |
| `P2.21` | `Bindings/AngelscriptCoreMiscBindingsTests.cpp` | `accept_temporary_this`、temporary builder、`FText::AsNumber` 输出对齐、默认工厂不被污染 | P1 |

### 验收标准增补（2026-04-09 06:41）

1. `FString.IsValidIndex()` 在脚本侧恢复为可消费的 `bool` 返回值；非法 `RemoveAt()` 输入不再跌回底层 `RangeCheck`，而是稳定进入脚本错误路径。
2. `Angelscript.TestModule.Bindings.StringIndexAndSliceCompat` 与 `Angelscript.TestModule.Bindings.StringRemoveAtGuardCompat` 能同时锁住合法索引/切片 happy path 与非法索引/删改 error path，不再依赖 `StringRemoveAtCompat` 的单个烟雾用例。
3. `FNumberFormattingOptions` 的 fluent setter 能重新作用在 temporary receiver 上；named-variable builder 与 temporary builder 产出的 `FText::AsNumber` 输出与 C++ 原生基线一致。
4. `DefaultWithGrouping()` / `DefaultNoGrouping()` 在 temporary builder 回归后仍保持工厂默认值不被意外改写。

### 风险与注意事项增补（2026-04-09 06:41）

#### 风险

1. `P1.19` 会把一部分历史上直接跌回底层容器检查的 `FString.RemoveAt()` 非法输入，前移成脚本层 deterministic 错误。
   - 缓解：测试同时锁住合法 `RemoveAt()` happy path 与非法 index/count error path，确保只改变错误收口层级，不改变合法删改语义。
2. `P2.21` 重新启用 `accept_temporary_this` 后，需要防止 `DefaultWithGrouping()` 这类默认工厂结果被误当成可变单例原地修改。
   - 缓解：把 “temporary builder 可链式调用” 与 “默认工厂结果保持不变” 放进同一组 regression，先证明确实是 temporary-copy 语义，再恢复 contract。

#### 已知行为变化

1. `FString.RemoveAt()` 的非法 `Index/Count` 将从当前潜在的底层 `RangeCheck` / assert 路径，变成显式脚本错误。
2. `FNumberFormattingOptions` 将重新接受 temporary builder 写法；当前为规避该限制而额外声明命名变量的脚本仍然继续可用，但不再是唯一写法。

---
## 深化 (2026-04-09 06:55)

本轮只追加现有 `Plan_BindSystem.md` 尚未承接的三条 bind contract：`P2.12` 当时刻意未展开的 `Bind_FMath.cpp` 几何 out-param family、`Bind_Subsystems.cpp` 缺失的 editor `NoBlueprintsOfChildren` guard、以及 `Bind_UUserWidget.cpp` 仍停留在 `FCoreStyle` 的 UMG style contract。它们均已确认不与 `Documents/Plans/Plan_HazelightBindModuleMigration.md`、`Documents/Plans/Plan_AS238NonLambdaPort.md`、`Documents/Plans/Plan_FullDeGlobalization.md`、`Documents/Plans/Plan_TestCoverageExpansion.md` 重复；其中 `Plan_TestCoverageExpansion.md` 只负责测试广义扩面，不替代这里的 runtime/editor bind contract 收口。

用户指定的 `Documents/AutoPlans/DiscoveryPlans/BindSystem_Plan.md` 在当前工作区不存在，因此本轮新增条目仅使用已验证的 [A] / [C] / [E] 证据链。

### Phase 2 深化：补齐几何 helper、editor surface 与 UMG style contract

- [ ] **P2.22** 补回 `Bind_FMath.cpp` 遗失的 `LinePlaneIntersection` / `LineExtentBoxIntersection` bool + out-param geometry helper
  - 现有 `P2.12` 已经把 `Bind_FMath.cpp` 的 64-bit overload 收口到位，但文内也明确没有展开 `LinePlaneIntersection` / `LineExtentBoxIntersection` 的 out-param family。本地源码验证表明这一缺口仍然存在：当前脚本只能拿到“直接返回交点”的 `FVector` / `FVector3f` 版本，拿不到“是否命中 + 命中时间/位置/法线”的原生 contract。
  - 这一项应直接按本地 UEAS2 参考实现恢复两条剩余 geometry helper，而不是再包一层自定义 shim：补回 `bool LinePlaneIntersection(const FVector& LineStart, const FVector& LineEnd, const FPlane& Plane, float32& T, FVector& Intersection)`，其实现与 UEAS2 一样走 `UKismetMathLibrary::LinePlaneIntersection(...)`；同时补回 `bool LineExtentBoxIntersection(const FBox& Box, const FVector& Start, const FVector& End, const FVector& Extent, FVector& out HitLocation, FVector& out HitNormal, float32& out HitTime)`，直接对齐 `FMath::LineExtentBoxIntersection(...)`。
  - 这里不改变已经公开的三个“返回 `FVector`/`FVector3f`” overload，也不把整份 `Bind_FMath.cpp` 拆分重写；目标只是把当前最明显的 success-flag/out-param 契约缺口补齐，并让脚本侧能和原生几何判交 helper 一一对账。
  - 来源：
    - [A] `Documents/AutoPlans/BindSystem_Analysis.md` — “发现 58：当前只保留 `LinePlaneIntersection` 的返回值版本，`bool + out-param` 版本以及 `LineExtentBoxIntersection` 一并缺失”
    - [C] `Documents/AutoPlans/TestCoverage/BindSystem_TestGaps.md` — “Issue-17 / Issue-28 / NewTest-16：`MathExtendedCompat` 仍只有弱断言和 vector-return happy path，没有 dedicated geometry helper regression”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FMath.cpp` L249-L261 — 当前只注册了三个 `LinePlaneIntersection` 返回值 overload，随后立即跳到 `LineSphereIntersection`，未见 `bool + out-param` 版本
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathAndPlatformBindingsTests.cpp` L130-L132 — 现有测试只验证 `FVector PlaneIntersection = Math::LinePlaneIntersection(...)` 的 happy path，没有 success flag / out-param 覆盖
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FMath.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathAndPlatformBindingsTests.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathGeometryBindingsTests.cpp`
- [ ] **P2.22** 📦 Git 提交：`[AngelscriptRuntime] Add: restore math geometry helper overloads`
- [ ] **P2.22-T** 单元测试：为几何判交 helper 建立 dedicated regression owner
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathGeometryBindingsTests.cpp`
  - 测试场景：
    - 正常路径：`Math::LinePlaneIntersection(FVector(0,0,-5), FVector(0,0,5), FPlane(FVector::ZeroVector, FVector::UpVector), T, Intersection)` 返回 `true`，`T` 与 `Intersection` 和原生 `UKismetMathLibrary::LinePlaneIntersection(...)` 基线一致；`Math::LineExtentBoxIntersection(...)` 的 `HitLocation` / `HitNormal` / `HitTime` 与原生 `FMath::LineExtentBoxIntersection(...)` 完全对齐
    - 边界条件：平行于平面的线段、擦边 swept-box、以及“起点就在 box 内部”的输入都要与 C++ 原生 helper 做逐字段对账，确认 `false/true`、`HitTime` 和 out-param 写入规则完全一致
    - 错误路径：compile smoke 必须直接调用 `bool + out-param` overload；若未来回归再次移除这两条签名、把 `out` 参数改成普通值传递，或把 `bool` 版本误绑回返回 `FVector` 版本，测试必须在编译期或行为断言上直接红灯
  - 测试命名：`Angelscript.TestModule.Bindings.MathLinePlaneIntersectionOutParamCompat`，`Angelscript.TestModule.Bindings.MathLineExtentBoxIntersectionCompat`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`
- [ ] **P2.22-T** 📦 Git 提交：`[AngelscriptTest] Test: cover math geometry helper contracts`

- [ ] **P2.23** 恢复 `Bind_Subsystems.cpp` 的 `NoBlueprintsOfChildren` editor guard，阻断当前分支已知不支持的 subsystem 蓝图入口
  - `P1.8` 已经覆盖了 `Class::Get()` 的 lifecycle guard，但还没有处理 editor authoring surface 的另一条缺口：当前 `Bind_Subsystems.cpp` 只保留了运行时 `Get()` accessor 生成逻辑，没有把 UEAS2 明确加在文件末尾的 `USubsystem::StaticClass()->SetMetaData(TEXT("NoBlueprintsOfChildren"), TEXT(""))` 一并迁回。本地基类头仍然是 `Blueprintable`，这会把一条本分支已知未闭环的 subsystem Blueprint 入口重新暴露给 editor。
  - 这一项应优先恢复 UEAS2 同位置的集中式 guard，而不是立刻翻动所有 subsystem 基类声明：在 `Bind_Subsystems.cpp` 末尾加回 `#if WITH_EDITOR` 下的 `SetMetaData(TEXT("NoBlueprintsOfChildren"), TEXT(""))`，让 editor parent picker / blueprint 工厂统一从 `USubsystem` 基类面收窄入口；同时新增 editor automation 验证 metadata 存在，并锁住 `UScriptEngineSubsystem`、`UScriptGameInstanceSubsystem`、`UScriptWorldSubsystem` 不再被当成可创建 Blueprint 的父类。
  - 这里不改变 `P1.8` 已经负责的 `Class::Get()` failure contract，也不把 `UScript*Subsystem` 直接改成 `NotBlueprintable`；优先恢复现有参考实现已经证明可行的集中式 editor guard，避免与后续 subsystem 主线计划交叉。
  - 来源：
    - [A] `Documents/AutoPlans/BindSystem_Analysis.md` — “发现 51：`USubsystem` 的 `NoBlueprintsOfChildren` 保护已被移除，editor 重新暴露不受支持的 subsystem 蓝图入口”
    - [C] `Documents/AutoPlans/TestCoverage/BindSystem_TestGaps.md` — “`Bind_Subsystems.cpp` 仍位于当前未见 direct-hit bind 清单，现有测试没有 editor surface regression”
    - [E] `Documents/AutoPlans/ReferenceComparison/Hazelight_Analysis.md` — “UEAS2 / Hazelight 仍在 `Bind_Subsystems.cpp` 末尾统一写入 `NoBlueprintsOfChildren`，并把它视为 editor-side 明确收窄”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Subsystems.cpp` L24-L122 — 文件当前在自动生成 `Class::Get()` accessor 后直接结束，没有任何 `SetMetaData("NoBlueprintsOfChildren", ...)` 回填
    - `Plugins/Angelscript/Source/AngelscriptRuntime/BaseClasses/ScriptEngineSubsystem.h` L6-L7、`Plugins/Angelscript/Source/AngelscriptRuntime/BaseClasses/ScriptGameInstanceSubsystem.h` L6-L7、`Plugins/Angelscript/Source/AngelscriptRuntime/BaseClasses/ScriptWorldSubsystem.h` L7-L8 — 三个 script subsystem 基类目前都仍是 `UCLASS(Blueprintable, Abstract)`
    - `Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemScenarioTests.cpp` L77-L107、L214-L244 — 现有测试只固定“subsystem script generation 仍然失败”，没有任何 editor 蓝图入口约束
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Subsystems.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/BaseClasses/ScriptEngineSubsystem.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/BaseClasses/ScriptGameInstanceSubsystem.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/BaseClasses/ScriptWorldSubsystem.h`
    - `Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemScenarioTests.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemBlueprintSurfaceTests.cpp`
- [ ] **P2.23** 📦 Git 提交：`[AngelscriptRuntime] Fix: restore subsystem blueprint surface guard`
- [ ] **P2.23-T** 单元测试：锁住 subsystem Blueprint 入口被 editor 正确拒绝
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemBlueprintSurfaceTests.cpp`
  - 测试场景：
    - 正常路径：在 `WITH_EDITOR` automation 下确认 `USubsystem::StaticClass()->HasMetaData(TEXT("NoBlueprintsOfChildren"))` 为真，并验证 `UScriptEngineSubsystem`、`UScriptGameInstanceSubsystem`、`UScriptWorldSubsystem` 不再出现在 Blueprint parent 允许列表中
    - 边界条件：集中式 guard 生效后，现有 runtime `Class::Get()` accessor 与 `AngelscriptSubsystemScenarioTests.cpp` 里“subsystem script generation 仍不支持”的断言都保持原样，确保本项只收窄 editor surface，不顺手改动 runtime 主线
    - 错误路径：显式尝试为 `UScriptEngineSubsystem` / `UScriptGameInstanceSubsystem` / `UScriptWorldSubsystem` 创建 Blueprint parent 或 asset 时，必须得到 deterministic editor 拒绝，而不是继续暴露可选父类入口
  - 测试命名：`Angelscript.TestModule.Subsystem.NoBlueprintsOfChildrenGuard`，`Angelscript.TestModule.Subsystem.ScriptSubsystemBlueprintParentRejected`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_FULL()` + `WITH_EDITOR` automation
- [ ] **P2.23-T** 📦 Git 提交：`[AngelscriptTest] Test: cover subsystem blueprint surface guard`

- [ ] **P2.24** 把 `Bind_UUserWidget.cpp` 的 style lookup contract 从 `FCoreStyle` 收回 `FAppStyle` / `GetOptionalBrush`
  - 当前 `Bind_UUserWidget.cpp` 里三条公开 style lookup surface 仍然停在 `FCoreStyle`：`FPaintContext::GetStyleColor()`、`GetStyleBrush()`、以及 `FSlateBrush(FName BrushStyleName)` 构造器。这样做不仅让脚本侧无法跟随当前 app/editor style set，而且还把原本“可选 brush key”的构造路径退化成 `GetBrush()` 的强查找和默认空 brush fallback。
  - 这一项应严格按参考实现只收口分析文档已命中的三条 surface：把头文件依赖从 `Styling/CoreStyle.h` 换回 `Styling/AppStyle.h`；`GetStyleColor()` / `GetStyleBrush()` 改为走 `FAppStyle::Get()`；`FSlateBrush(FName)` 改成 `FAppStyle::Get().GetOptionalBrush(...)`，从而恢复 app style fallback 链和“缺失 key 不额外产生强查找噪声”的 contract。
  - 这里不顺手改动 `DrawBox()` / `DrawText()` 这类 internal helper 里仍使用 `FCoreStyle` 的默认白刷/默认字体路径；本项只处理当前分析已经确认回退的 public style-name lookup contract，避免和 `Plan_FullDeGlobalization.md` 的其他 `Bind_UUserWidget` 主线混在一起。
  - 来源：
    - [A] `Documents/AutoPlans/BindSystem_Analysis.md` — “发现 52：UMG style helper 从 `FAppStyle` 退回 `FCoreStyle`，brush 构造器也失去 `GetOptionalBrush()` contract”
    - [C] `Documents/AutoPlans/TestCoverage/BindSystem_TestGaps.md` — “`Bind_UUserWidget.cpp` 仍位于当前未见 direct-hit bind 清单；现有 widget example 只覆盖 `CreateWidget`”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UUserWidget.cpp` L19 — 当前仍包含 `#include "Styling/CoreStyle.h"`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UUserWidget.cpp` L93-L112 — `GetStyleColor()` / `GetStyleBrush()` 仍直接走 `FCoreStyle::Get().GetColor()` / `GetBrush()`，未对齐 `FAppStyle`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UUserWidget.cpp` L321-L330 — `FSlateBrush(FName BrushStyleName)` 构造器仍走 `FCoreStyle::Get().GetBrush(...)`，没有 `GetOptionalBrush(...)`
    - `Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleWidgetUmgTest.cpp` L45-L60 — 现有测试只编译 `WidgetBlueprint::CreateWidget(...)` 示例，没有任何 style lookup / missing-brush 行为回归
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UUserWidget.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleWidgetUmgTest.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptUmgStyleBindingsTests.cpp`
- [ ] **P2.24** 📦 Git 提交：`[AngelscriptRuntime] Fix: restore UMG app-style lookup contracts`
- [ ] **P2.24-T** 单元测试：补齐 app-style key 与 optional-brush fallback 的 dedicated regression
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptUmgStyleBindingsTests.cpp`
  - 测试场景：
    - 正常路径：脚本侧 `GetStyleColor(FName("LevelEditor.AssetColor"))` 与 `GetStyleBrush(FName("Graph.TitleBackground"))` 的返回结果分别和 `FAppStyle::Get().GetColor("LevelEditor.AssetColor")`、`FAppStyle::Get().GetBrush("Graph.TitleBackground")` 基线一致；`FSlateBrush(FName("Graph.TitleBackground"))` 生成的 brush 资源名也与原生 `FAppStyle` 一致
    - 边界条件：对明确不存在的 key，例如 `FName("Angelscript.MissingBrush.UnitTest")`，`FSlateBrush(FName(...))` 与 `GetStyleBrush(...)` 都应回落到与 `GetOptionalBrush(...)` 对齐的空 brush / 默认 brush 语义，并保持无额外 style warning 噪声
    - 错误路径：若 future regression 再次切回 `FCoreStyle`，对 app-only key 的 lookup 应在基线比对或 warning 计数断言上直接红灯，而不是继续被 `CreateWidget` smoke test 放过
  - 测试命名：`Angelscript.TestModule.Bindings.UmgAppStyleLookupCompat`，`Angelscript.TestModule.Bindings.UmgOptionalBrushFallbackCompat`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_FULL()`
- [ ] **P2.24-T** 📦 Git 提交：`[AngelscriptTest] Test: cover UMG app-style lookup contracts`

### 单元测试总览增补（2026-04-09 06:55）

| 改进项 | 测试文件 | 测试重点 | 优先级 |
|--------|---------|---------|--------|
| `P2.22` | `Bindings/AngelscriptMathGeometryBindingsTests.cpp` | `LinePlaneIntersection` bool/out-param、`LineExtentBoxIntersection` 命中信息、原生几何 helper 对账 | P1 |
| `P2.23` | `Subsystem/AngelscriptSubsystemBlueprintSurfaceTests.cpp` | `NoBlueprintsOfChildren` metadata、Blueprint parent 拒绝、editor surface 收窄且不影响 runtime guard | P1 |
| `P2.24` | `Bindings/AngelscriptUmgStyleBindingsTests.cpp` | `FAppStyle` app-only key、`GetOptionalBrush` fallback、missing-brush warning contract | P1 |

### 验收标准增补（2026-04-09 06:55）

1. `Bind_FMath.cpp` 在不破坏现有返回值 overload 的前提下，恢复 `LinePlaneIntersection` 的 `bool + out-param` 版本以及 `LineExtentBoxIntersection`，并由 dedicated regression 对齐原生 `UKismetMathLibrary` / `FMath` 基线。
2. `USubsystem::StaticClass()` 在 editor 下重新带有 `NoBlueprintsOfChildren` metadata；`UScriptEngineSubsystem`、`UScriptGameInstanceSubsystem`、`UScriptWorldSubsystem` 不再作为 Blueprint parent 暴露，同时不影响既有 `Class::Get()` runtime contract。
3. `Bind_UUserWidget.cpp` 的 `GetStyleColor()`、`GetStyleBrush()` 和 `FSlateBrush(FName)` 重新对齐 `FAppStyle` / `GetOptionalBrush` contract，app-only key 不再静默退化成 `FCoreStyle` 默认资源。
4. 新增的三组测试都必须覆盖正常路径、边界条件、错误路径；不能再依赖现有 smoke case 间接“顺带命中”来证明 bind contract 正确。

### 风险与注意事项增补（2026-04-09 06:55）

#### 风险

1. `P2.22` 恢复 `bool + out-param` 几何 helper 后，如果脚本侧已经自行包装了同名 helper，可能会暴露出重载解析或命名冲突。
   - 缓解：保持现有 `FVector` 返回值 overload 不变，并用 compile smoke 明确锁住新增签名的解析优先级。
2. `P2.23` 会把当前 editor 中“看起来可选、实际上不支持”的 subsystem Blueprint 入口提前收窄，可能影响已经形成但并未真正可运行的本地试验资产流程。
   - 缓解：先用 metadata + parent picker 拒绝做最小收口，不在本项同时改动 subsystem runtime 主线；必要时在测试中把“editor 被拒绝、runtime 仍按现状”锁成同一条 contract。
3. `P2.24` 切回 `FAppStyle` 后，测试如果选用不稳定的 style key，容易在不同 UE 次版本间产生脆弱断言。
   - 缓解：优先使用本地引擎中被大量 editor 模块消费的稳定 key，例如 `LevelEditor.AssetColor` 和 `Graph.TitleBackground`，并让断言直接对齐 `FAppStyle::Get()` 原生结果而不是写死资源内容。

#### 已知行为变化

1. 脚本将新增两条可用的几何 helper overload：`bool LinePlaneIntersection(..., float32& T, FVector& Intersection)` 与 `bool LineExtentBoxIntersection(..., FVector& HitLocation, FVector& HitNormal, float32& HitTime)`。
2. editor 中针对 `UScript*Subsystem` 的 Blueprint parent 入口会被显式隐藏/拒绝，不再继续暴露一条当前分支已知未闭环的 authoring surface。
3. UMG style-name lookup 将重新跟随 `FAppStyle`；对于 app-only key，脚本可见的 brush/color 结果会从当前的 `FCoreStyle` fallback 回到 editor/app style 真值。

---
## 深化 (2026-04-09 07:10:11)

本轮只追加现有 `Plan_BindSystem.md` 尚未承接、且已确认不与 `Documents/Plans/Plan_HazelightBindModuleMigration.md`、`Documents/Plans/Plan_TestCoverageExpansion.md` 重复的三条 bind contract：`FinishSpawningActor` 的真正完成态判断、`AActor/UActorComponent` 只读查询的 `const` / `no_discard` 回退、以及 subsystem `Class::Get()` 的返回值契约深化。`Plan_HazelightBindModuleMigration.md` 已承接 `DeprecateOldActorGenericMethods`、`GetComponentsByClassWithTag()` 与 `VerifySpawnActor`，因此这里不重复 actor/scene component 的配置迁移主线，只补 current 仍裸露的 correctness / compile-contract 缺口。

用户指定的 `Documents/AutoPlans/DiscoveryPlans/BindSystem_Plan.md` 当前工作区仍不存在，因此本轮新增条目仅使用已验证的 [A] / [C] / [E] 证据链。

### Phase 1 深化：把 spawn-complete 的错误边界补齐到 bind 层

- [ ] **P1.20** 把 `FinishSpawningActor` 的“已完成生成”判定从 `HasActorBegunPlay()` 改回真实 spawn-complete contract
  - `P1.1` 已经承接了 `GEngine` 判空、`VerifySpawnActor` 和 `Actor == nullptr` 的错误收口，但当前 `FinishSpawningActor` 还有一条尚未进入 Plan 的误用窗口：对一个“已经完成普通 spawn、但还没走到 `BeginPlay`”的 actor，bind 仍会放行第二次 `FinishSpawningActor(...)`，把错误从脚本 API 误用降级成底层 `ensure(!bHasFinishedSpawning)`。
  - 这一项只深化 spawn-complete contract，不重开 `VerifySpawnActor` 或 persistent-level 语义：在 `Bind_AActor.cpp` 内抽最小 `ValidateActorCanFinishSpawning(...)` helper，统一把“非 deferred actor”“已经 finish 过的 deferred actor”“空 actor”收口成确定的脚本错误，并让两个 overload 共享同一条判断逻辑。
  - 来源：
    - [A] `Documents/AutoPlans/BindSystem_Analysis.md` — “发现 63/76：`FinishSpawningActor` 用 `HasActorBegunPlay()` 代替真实完成态，且空 actor 仍会被静默吞掉”
    - [C] `Documents/AutoPlans/TestCoverage/BindSystem_TestGaps.md` — “NewTest-2：`Bind_AActor.cpp` 的 world-backed actor helper 仍没有 dedicated spawn regression；现有测试完全未锁住 `FinishSpawningActor` 误用边界”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp` L256-L283 — 两个 `FinishSpawningActor` overload 只检查 `Actor == nullptr` 与 `Actor->HasActorBegunPlay()`，随后直接调用 `Actor->FinishSpawning(...)`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp` L455-L461 — 公开 surface 仍直接导出两个 `FinishSpawningActor` overload，没有额外 bind-side contract 保护
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptActorSpawnContractTests.cpp`
- [ ] **P1.20** 📦 Git 提交：`[AngelscriptRuntime] Fix: reject already-finished actors in FinishSpawningActor`
- [ ] **P1.20-T** 单元测试：为 `FinishSpawningActor` 建立真正的 deferred-only regression owner
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptActorSpawnContractTests.cpp`
  - 测试场景：
    - 正常路径：`bDeferredSpawn=true` 生成的 actor 可成功执行一次 `FinishSpawningActor(...)`，完成后 transform 与原生 `FinishSpawning` 基线一致
    - 边界条件：对“普通 `SpawnActor(...)` 刚返回、尚未 `BeginPlay`”的 actor 调 `FinishSpawningActor(...)` 必须被 bind 层确定性拒绝；deferred actor 完成一次后再次 finish 也必须稳定报错
    - 错误路径：`FinishSpawningActor(null)`、已销毁 actor、或 transform overload 的重复 finish 都必须记录精确脚本错误，而不是继续跌进底层 `ensure`
  - 测试命名：`Angelscript.TestModule.Bindings.FinishSpawningDeferredOnly`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_FULL()` + `FScopedTestWorldContextScope`
- [ ] **P1.20-T** 📦 Git 提交：`[AngelscriptTest] Test: cover FinishSpawningActor deferred-only contract`

### Phase 2 深化：把只读查询与返回值消费合同重新做实

- [ ] **P2.25** 恢复 actor/component 只读查询 family 的 `const` / `no_discard` contract
  - `P1.16` 已经承接了 `GetComponentsByClass()` / `GetAllComponents()` 的输出数组累积问题，但同一 family 仍有另一组未承接的签名回退：`FAngelscriptActorBinds::GetComponent` / `GetAllComponents` / `GetComponentFromMeta` / `GetAllComponentsGeneric` 都退回成 `AActor*`，脚本声明也丢了 `const` 与 `no_discard`。这让 UEAS2 下合法的 `const AActor` 查询脚本在 current 里无法编译，同时继续允许静默丢弃 `GetComponent()` / `ClassName::Get()` 的返回值。
  - 这一项只处理 read-only query surface，不重复 `Plan_HazelightBindModuleMigration.md` 已承接的 `DeprecateOldActorGenericMethods`、`GetComponentsByClassWithTag()` 或更广的 actor helper 配置迁移：把只读 helper 统一收回 `const AActor*` / `const` method，并给 `AActor::GetComponent(...)` 与 component namespace `ClassName::Get(...)` 补回 `no_discard`。
  - 来源：
    - [A] `Documents/AutoPlans/BindSystem_Analysis.md` — “发现 32：`AActor` 组件查询 helper 回退成非 `const` 接口，并丢掉 `no_discard`”
    - [C] `Documents/AutoPlans/TestCoverage/BindSystem_TestGaps.md` — “Issue-71 / Issue-83 / NewTest-56：`NativeComponentMethods` 只覆盖可变 actor happy path，还会主动清空输出数组，没有 const-context 或返回值契约回归”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Actor.h` L9-L18、L32-L34 — `GetComponent` / `GetAllComponents` / `GetComponentGeneric` / `GetAllComponentsGeneric` 全部使用 `AActor*`，没有只读签名
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UActorComponent.cpp` L127-L205 — runtime helper 实现全部以可变 `AActor*` 为入口
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UActorComponent.cpp` L351-L416 — `AActor::GetComponent(...)`、`GetAllComponents(...)`、`ClassName::Get(...)` 和 `__Actor_GetComponentByClass(...)` 的脚本声明都缺 `const` / `no_discard`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp` L132-L167 — 当前测试只在 `GetOwner()` 的可变 happy path 上消费返回值，没有 const actor 或丢弃返回值的 compile regression
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Actor.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UActorComponent.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptActorComponentQueryContractsTests.cpp`
- [ ] **P2.25** 📦 Git 提交：`[AngelscriptRuntime] Fix: restore const and nodiscard on actor component queries`
- [ ] **P2.25-T** 单元测试：给 actor/component 查询签名单独建立 compile-contract owner
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptActorComponentQueryContractsTests.cpp`
  - 测试场景：
    - 正常路径：`const AActor` 场景下 `GetComponent(...)`、`GetAllComponents(...)`、`USceneComponent::Get(ConstActor)` 都可成功编译并返回与可变 actor 相同的组件结果
    - 边界条件：带名字查询、泛型 `TArray<USceneComponent>` 输出、以及预填充输出数组的只读查询都保持与 native 基线一致，不因签名恢复再次引入 append 或 subtype 漂移
    - 错误路径：显式丢弃 `ConstActor.GetComponent(USceneComponent::StaticClass())` 与 `USceneComponent::Get(ConstActor)` 的返回值必须产生 `no_discard` diagnostics；若 future regression 再次把 helper 降回非 `const`，compile smoke 必须直接红灯
  - 测试命名：`Angelscript.TestModule.Bindings.ActorComponentQueryConstCompat`，`Angelscript.TestModule.Bindings.ActorComponentQueryNoDiscardCompat`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`
- [ ] **P2.25-T** 📦 Git 提交：`[AngelscriptTest] Test: cover actor component query const and nodiscard contracts`

- [ ] **P2.26** 为 subsystem `Class::Get()` 补 dedicated `no_discard` compile contract，作为 `P1.8` 的返回值语义深化
  - `P1.8` 已经承接 subsystem `Class::Get()` 的生命周期 guard，但现有条目还没有把“这些 accessor 的唯一价值就是返回 subsystem handle”单独做成 compile-contract owner。当前 `Bind_Subsystems.cpp` 仍把所有 `Class::Get()` 公开成普通返回值函数，脚本可以静默写出 `UMySubsystem::Get();`，而不会收到任何误用提示。
  - 这一项只深化返回值契约，不重复 `P1.8` 的 `GEngine/GEditor` 生命周期 guard，也不重复 `P2.23` 的 `NoBlueprintsOfChildren` editor surface 收窄：所有 native subsystem `Get()` 重建 `no_discard` 后，专门补一组 compile smoke 与最小 runtime 对账，锁住“可消费可判空、不可静默丢弃”的统一规则。
  - 来源：
    - [A] `Documents/AutoPlans/BindSystem_Analysis.md` — “发现 50：native subsystem `Class::Get()` 访问器整组丢失 `no_discard`”
    - [C] `Documents/AutoPlans/TestCoverage/BindSystem_TestGaps.md` — “`Bind_Subsystems.cpp` 仍位于未见 direct-hit bind 清单；现有 subsystem scenario 只锁 script generation 失败，不覆盖 `Class::Get()` 绑定合同”
    - [E] `Documents/AutoPlans/ReferenceComparison/Hazelight_Analysis.md` — “UEAS2 在 subsystem 预处理静态 `Get()` 上仍生成 `__generated no_discard`”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Subsystems.cpp` L48-L53、L60-L65、L69-L81、L85-L94、L99-L106 — editor / engine / game-instance / world / local-player subsystem 的 `Get()` 声明全部是普通返回值签名，没有 `no_discard`
    - `Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemScenarioTests.cpp` L130-L197 — 当前仅覆盖 world/game-instance subsystem script generation 与 actor access 的场景失败，没有任何 `Class::Get()` compile/runtime 契约断言
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Subsystems.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemBindingContractsTests.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp`
- [ ] **P2.26** 📦 Git 提交：`[AngelscriptRuntime] Fix: restore subsystem Get nodiscard contracts`
- [ ] **P2.26-T** 单元测试：把 subsystem accessor 的返回值消费规则从 lifecycle 测试中独立出来
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemBindingContractsTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp`
  - 测试场景：
    - 正常路径：`UEngineSubsystem::Get()`、`UWorldSubsystem::Get()`、`UGameInstanceSubsystem::Get()`、`ULocalPlayerSubsystem::Get(...)` 在消费返回值时都可成功编译，并在有效上下文下与原生 `GetSubsystemBase()` 结果一致
    - 边界条件：`World == nullptr`、`GameInstance == nullptr`、或 editor-only 分支被脚本上下文禁用时，返回值仍可被显式判空，不因 `no_discard` 恢复而破坏 `P1.8` 的 null-safe 语义
    - 错误路径：显式丢弃 `UMyEngineSubsystem::Get()`、`UMyWorldSubsystem::Get()`、`UMyGameInstanceSubsystem::Get()` 的返回值必须产生 compile diagnostics；非 editor script context 下错误解析 editor subsystem overload 也必须红灯
  - 测试命名：`Angelscript.TestModule.Subsystem.GetNoDiscardContract`，`Angelscript.TestModule.Subsystem.LocalPlayerGetNoDiscardContract`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_FULL()` + `FScopedTestWorldContextScope`
- [ ] **P2.26-T** 📦 Git 提交：`[AngelscriptTest] Test: cover subsystem Get nodiscard contracts`

### 单元测试总览增补 (2026-04-09 07:10:11)

| 改进项 | 测试文件 | 测试重点 | 优先级 |
|--------|---------|---------|--------|
| `P1.20` | `Bindings/AngelscriptActorSpawnContractTests.cpp` | deferred-only `FinishSpawningActor`、二次 finish、非 deferred actor 误用 | P0 |
| `P2.25` | `Bindings/AngelscriptActorComponentQueryContractsTests.cpp` | `const AActor` 查询、`GetComponent` / `ClassName::Get` `no_discard`、只读输出数组契约 | P1 |
| `P2.26` | `Subsystem/AngelscriptSubsystemBindingContractsTests.cpp`，`Core/AngelscriptBindConfigTests.cpp` | subsystem `Get()` `no_discard`、有效上下文对账、editor/local-player overload compile contract | P1 |

### 验收标准增补 (2026-04-09 07:10:11)

1. `FinishSpawningActor` 只接受“尚未完成 finish 的 deferred actor”；普通 `SpawnActor(...)` 返回的 actor 即使尚未 `BeginPlay`，再次 `FinishSpawningActor(...)` 也会在 bind 层被确定性拒绝。
2. `AActor::GetComponent(...)`、`AActor::GetAllComponents(...)` 与 component namespace `ClassName::Get(...)` 重新对齐 `const` / `no_discard` contract，且由独立 compile smoke 锁住，不再依赖 `NativeComponentMethods` 的可变 happy path 顺带覆盖。
3. native subsystem `Class::Get()` 重新具备 `no_discard` 返回值契约；消费返回值的 happy path 与 `P1.8` 生命周期 guard 并存，丢弃返回值则稳定触发 diagnostics。

### 风险与注意事项增补 (2026-04-09 07:10:11)

#### 风险

1. `P1.20` 会把一部分当前只是跌进底层 `ensure` 的误用，前移成显式脚本错误。
   - 缓解：测试同时覆盖 deferred happy path、普通 spawn 误用、二次 finish 和空 actor 四条路径，确保只收紧错误边界，不回退合法 deferred spawn 体验。
2. `P2.25` 恢复 `no_discard` 后，现有脚本里把 `GetComponent()` / `ClassName::Get()` 当成“有副作用调用”的空语句会转成编译错误。
   - 缓解：把 compile diagnostics 与 const-context happy path 放进同一组 regression，避免只恢复约束、不验证真实读取路径。
3. `P2.26` 若只补 `no_discard` 不补 dedicated compile owner，后续很容易再次被 `P1.8` 的 runtime guard 测试假绿掩盖。
   - 缓解：单独建 `SubsystemBindingContracts` regression owner，并在 `BindConfig` 侧补 trait / declaration 级断言。

#### 已知行为变化

1. 对非 deferred actor 或已完成 finish 的 actor 再次调用 `FinishSpawningActor(...)` 将从当前的底层 `ensure` / 不稳定行为，变成显式脚本错误。
2. `AActor::GetComponent(...)`、component namespace `ClassName::Get(...)` 会重新要求消费返回值，并重新允许 `const AActor` 读取路径。
3. native subsystem `Class::Get()` 的返回值将从当前 silent pass 变成 `no_discard` contract；脚本可以继续显式判空，但不能再无条件丢弃结果。

---
## 深化 (2026-04-09 07:16:21)

本轮只追加现有 `Plan_BindSystem.md` 尚未承接的三条 current parity / guard 缺口：`Bind_FMemoryReader.cpp` 的负长度 fatal、`Bind_UAssetManager.cpp` 被注释掉的 safe-entry、以及 `Bind_FGeometry.cpp` 的 render-space helper 缺口。已确认不与 `Documents/Plans/Plan_UEBindGapRoadmap.md`、`Documents/Plans/Plan_TestCoverageExpansion.md`、`Documents/Plans/Plan_AngelscriptUnitTestExpansion.md` 重复：前两者分别偏 API 扩面与广义补测，后者只把 `FMemoryReader` 当序列化载体，没有承接这里的 bind contract。`Documents/AutoPlans/DiscoveryPlans/BindSystem_Plan.md` 当前工作区仍不存在，因此本轮新增条目仅使用已验证的 [A] / [C] 证据链。

### Phase 1 深化：收口还会直接跌进引擎 fatal 的读入入口

- [ ] **P1.21** 为 `Bind_FMemoryReader.cpp` 的长度型读取补齐负值护栏，阻断 `TArray` fatal 泄漏
  - 当前 `Seek()` / `Skip()` 已经有“越界时报脚本错误”的 bind-side 护栏，但 `ReadBytes(int Count)` 与 `ReadAnsiString(int Count)` 仍把脚本 `Count` 直接交给 `SetNumUninitialized(Count)`。这会让普通脚本输入错误直接越过 `FAngelscriptEngine::Throw(...)`，跌进 `OnInvalidArrayNum()` 的进程级 fatal。
  - 这一项只修长度型读取 contract，不重开 `FArchive` 其它读方法：在 `Bind_FMemoryReader.cpp` 内抽一个共享 `ValidateReadableCountOrThrow(...)` helper，先收口 `Count < 0` 与“请求字节数超过剩余长度”的错误，再让 `ReadBytes()` / `ReadAnsiString()` 复用同一条 guard，保持错误语言与现有 `Seek()` / `Skip()` 一致。
  - 来源：
    - [A] `Documents/AutoPlans/BindSystem_Analysis.md` — “发现 61：`ReadBytes()` / `ReadAnsiString()` 对负长度没有前置校验，会直接触发 `TArray` fatal”
    - [C] `Documents/AutoPlans/TestCoverage/BindSystem_TestGaps.md` — “`Bind_FMemoryReader.cpp` 仍位于当前未见 direct-hit bind 清单，负长度边界没有任何自动化所有者”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FMemoryReader.cpp` L128-L142 — `ReadBytes()` / `ReadAnsiString()` 仍直接 `SetNumUninitialized(Count)`，没有 `Count < 0` 或剩余长度校验
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FMemoryReader.cpp` L38-L56 — 同文件 `Seek()` / `Skip()` 已有显式 bounds guard，说明读取入口目前确实缺同等级防线
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FMemoryReader.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMemoryReaderBindingsTests.cpp`
- [ ] **P1.21** 📦 Git 提交：`[AngelscriptRuntime] Fix: guard negative length reads in FMemoryReader binds`
- [ ] **P1.21-T** 单元测试：为 `FMemoryReader` 长度型读取建立 dedicated guard regression owner
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMemoryReaderBindingsTests.cpp`
  - 测试场景：
    - 正常路径：`ReadBytes(3)` 与 `ReadAnsiString(5)` 的返回值、读取后 `Tell()` 位移都与原生 `FMemoryReader` 基线一致
    - 边界条件：`ReadBytes(0)` / `ReadAnsiString(0)` 返回空结果且不推进 reader；“恰好读到结尾”继续保持当前可用语义
    - 错误路径：`ReadBytes(-1)`、`ReadAnsiString(-1)`、以及“请求长度超过剩余字节数”的场景都必须转成精确脚本错误，而不是 `OnInvalidArrayNum()` fatal 或 silent overread
  - 测试命名：`Angelscript.TestModule.Bindings.MemoryReaderLengthGuards`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`
- [ ] **P1.21-T** 📦 Git 提交：`[AngelscriptTest] Test: cover FMemoryReader length guard contracts`

### Phase 2 深化：补回 current 收缩掉的 safe-entry 与 geometry helper

- [ ] **P2.27** 恢复 `UAssetManager` namespace 的 `IsInitialized()` / `Get()` 安全入口，并补回 `no_discard` 使用合同
  - 当前 `Bind_UAssetManager.cpp` 已经保留了 `LoadPrimaryAsset(s)` / `UnloadPrimaryAsset(s)` 这类实际工作流入口，却把脚本最基础的“先判断 manager 是否可用，再安全拿句柄”两条 helper 直接注释掉了。这会让同一 namespace 的 API 面从 “check -> get -> use” 退化成 “假定外部已经准备好 manager”。
  - 这项工作只恢复 safe-entry，不重开 `Plan_UEBindGapRoadmap.md` 里更大的 `FStreamableHandle` / AssetManager capability 扩面：在 `UAssetManager` namespace 重新公开 `bool IsInitialized() no_discard` 与 `UAssetManager Get() no_discard`，`Get()` 明确继续走 `GetIfInitialized()` 的 null-safe contract，而不是强制改成 hard singleton。
  - 来源：
    - [A] `Documents/AutoPlans/BindSystem_Analysis.md` — “发现 62：`Bind_UAssetManager.cpp` 把 `IsInitialized()` / `Get()` 整段注释掉，脚本失去初始化探测与安全获取句柄的标准入口”
    - [C] `Documents/AutoPlans/TestCoverage/BindSystem_TestGaps.md` — “`Bind_UAssetManager.cpp` 仍位于当前未见 direct-hit bind 清单，测试树没有任何 asset-manager safe-entry regression”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UAssetManager.cpp` L83-L105 — 文件当前只保留 primary asset 查询与 load/unload surface
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UAssetManager.cpp` L107-L110 — `UAssetManager` namespace 下的 `IsInitialized()` / `Get()` 绑定仍整段被注释
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UAssetManager.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptAssetManagerBindingsTests.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp`
- [ ] **P2.27** 📦 Git 提交：`[AngelscriptRuntime] Fix: restore asset manager safe entry bindings`
- [ ] **P2.27-T** 单元测试：把 asset-manager safe-entry 从“零测试缺口”升级成 compile/runtime 契约 owner
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptAssetManagerBindingsTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp`
  - 测试场景：
    - 正常路径：`UAssetManager::IsInitialized()` 与 C++ `UAssetManager::IsInitialized()` 基线一致；当 `UAssetManager::GetIfInitialized()` 非空时，脚本 `UAssetManager::Get()` 返回同一个 manager，并可继续调用 `GetPrimaryAssetPath()` / `GetPrimaryAssetIdForPath()`
    - 边界条件：当 C++ 基线显示 `GetIfInitialized()` 为 `nullptr` 时，脚本 `UAssetManager::Get()` 也必须返回 `null`，且 `IsInitialized()` 与该状态保持一致，不把“未初始化”伪装成 hard failure
    - 错误路径：显式丢弃 `UAssetManager::IsInitialized()` 与 `UAssetManager::Get()` 返回值必须产生 `no_discard` diagnostics；若 future regression 再次把两条 helper 注释掉，compile smoke 必须直接红灯
  - 测试命名：`Angelscript.TestModule.Bindings.AssetManagerSafeEntryPoints`，`Angelscript.TestModule.Bindings.AssetManagerEntryNoDiscard`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`
- [ ] **P2.27-T** 📦 Git 提交：`[AngelscriptTest] Test: cover asset manager safe entry contracts`

- [ ] **P2.28** 恢复 `FGeometry` 的 render-space 查询与 `MakeTransformedChild()` helper，停止把 geometry surface 压扁成 layout-only 子集
  - 当前 `Bind_FGeometry.cpp` 只剩 `GetLocalSize()`、`GetAbsoluteSize()`、坐标转换与 layout `MakeChild()`；render-space 读数和 transformed child 构造都已经从 current 消失。对 UMG/Slate 脚本来说，这不是“少一个 convenience overload”，而是少了直接读取 render transform 和构造 render-space child geometry 的标准入口。
  - 这一项把 `发现 70` / `发现 71` 合并处理，不把它拆成两条孤立 API checklist：在 `Bind_FGeometry.cpp` 一次性补回 `GetRenderTransformScale()`、`GetRenderTransformTranslation()`、`GetAbsolutePosition()` 与 `MakeTransformedChild(const FVector2D& Translation, const FVector2D& Scale)`，并用同一组测试同时锁住查询 helper 与 child builder 的 native parity。
  - 来源：
    - [A] `Documents/AutoPlans/BindSystem_Analysis.md` — “发现 70/71：`FGeometry` 缺失 render-transform / absolute-position helper 与 `MakeTransformedChild()`”
    - [C] `Documents/AutoPlans/TestCoverage/BindSystem_TestGaps.md` — “`Bind_FGeometry.cpp` 仍位于当前未见 direct-hit bind 清单，测试树没有任何 geometry helper regression”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FGeometry.cpp` L9-L35 — 当前只注册 `GetLocalSize()`、`GetAbsoluteSize()`、`AbsoluteToLocal()`、`LocalToAbsolute()` 与 `MakeChild()`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FGeometry.cpp` L31-L35 — child builder 仍只走 `FSlateLayoutTransform`，没有任何 render-transform 变体
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FGeometry.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGeometryBindingsTests.cpp`
- [ ] **P2.28** 📦 Git 提交：`[AngelscriptRuntime] Fix: restore geometry render-space helper bindings`
- [ ] **P2.28-T** 单元测试：为 geometry render-space helper 建立 dedicated parity owner
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGeometryBindingsTests.cpp`
  - 测试场景：
    - 正常路径：脚本侧 `GetRenderTransformScale()`、`GetRenderTransformTranslation()`、`GetAbsolutePosition()` 与同一 `FGeometry` 的原生 C++ 结果逐项一致；`MakeTransformedChild()` 结果与 native helper 基线一致
    - 边界条件：identity render transform、零 translation、单位 scale 的 geometry 在 script / C++ 两侧都返回稳定一致的读数；`GetAbsolutePosition()` 与 `LocalToAbsolute(FVector2D::ZeroVector)` 对齐
    - 错误路径：若 future regression 再次移除 render-space helper 或把 `MakeTransformedChild()` 签名回退成 layout-only 版本，compile smoke 必须在对应 helper 调用点直接红灯，而不是继续被 `FGeometry` compile smoke 假绿掩盖
  - 测试命名：`Angelscript.TestModule.Bindings.GeometryRenderSpaceHelpers`，`Angelscript.TestModule.Bindings.GeometryTransformedChildCompat`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_FULL()`
- [ ] **P2.28-T** 📦 Git 提交：`[AngelscriptTest] Test: cover geometry render-space helper contracts`

### 单元测试总览增补 (2026-04-09 07:16:21)

| 改进项 | 测试文件 | 测试重点 | 优先级 |
|--------|---------|---------|--------|
| `P1.21` | `Bindings/AngelscriptMemoryReaderBindingsTests.cpp` | `ReadBytes` / `ReadAnsiString` 负长度、零长度、剩余长度 guard | P0 |
| `P2.27` | `Bindings/AngelscriptAssetManagerBindingsTests.cpp`，`Core/AngelscriptBindConfigTests.cpp` | `UAssetManager::IsInitialized()` / `Get()` safe-entry、null-safe parity、`no_discard` | P1 |
| `P2.28` | `Bindings/AngelscriptGeometryBindingsTests.cpp` | render-space helper、`GetAbsolutePosition()`、`MakeTransformedChild()` parity | P1 |

### 验收标准增补 (2026-04-09 07:16:21)

1. `FMemoryReader.ReadBytes(-1)` 与 `ReadAnsiString(-1)` 不再触发引擎 `TArray` fatal；相同输入现在必须落到 bind 层的稳定脚本错误。
2. `UAssetManager::IsInitialized()` / `Get()` 重新出现在脚本 surface，并由自动化确认与 C++ `UAssetManager::IsInitialized()` / `GetIfInitialized()` 基线一致，同时恢复 `no_discard` 使用合同。
3. `FGeometry` 重新具备 `GetRenderTransformScale()`、`GetRenderTransformTranslation()`、`GetAbsolutePosition()` 与 `MakeTransformedChild()`，且 script / C++ 两侧对同一 geometry 的结果一致。

### 风险与注意事项增补 (2026-04-09 07:16:21)

#### 风险

1. `P1.21` 会把当前直接跌进引擎 fatal 的负长度读取，前移成显式脚本错误。
   - 缓解：同一组 regression 同时覆盖正长度、零长度、超剩余长度和负长度，确认只收紧异常边界，不改变合法读取结果。
2. `P2.27` 恢复 `no_discard` 后，现有脚本里把 `UAssetManager::IsInitialized()` / `Get()` 当成空副作用调用的路径会转成编译错误。
   - 缓解：把 compile diagnostics 与 runtime parity 放在同一组 owner 里验证，避免只恢复签名、不验证真实 safe-entry 语义。
3. `P2.28` 补回 geometry render-space helper 后，若仓内已有同名脚本包装 helper，可能暴露出解析优先级或命名冲突。
   - 缓解：测试直接锁住 canonical helper 名和签名；如需兼容脚本自定义包装，优先通过命名空间或局部 rename 处理，不在 bind 层再引入第二套别名。

#### 已知行为变化

1. `FMemoryReader.ReadBytes()` / `ReadAnsiString()` 对负长度输入将从当前的引擎 fatal 变成显式脚本错误。
2. 脚本将重新拥有 `UAssetManager::IsInitialized()` / `UAssetManager::Get()` 两条 safe-entry，并要求消费其返回值。
3. `FGeometry` 将重新暴露 render-space 查询与 `MakeTransformedChild()`；此前靠手写 geometry 推导或自定义包装维持的脚本可改回直接调用标准 helper。

---
## 深化 (2026-04-09 07:27:24)

本轮只追加 `Bind_UInputSettings.cpp` 的 deprecated speech surface 收缩条目，并显式修正现有 `P2.15` 的执行边界：`GetSpeechMappings()` / `DoesSpeechExist()` 不再应被视为 current 需要继续补 `no_discard` 的 supported helper；`P2.15` 后续只保留 `GetUniqueActionName()`、`GetUniqueAxisName()`、`DoesActionExist()`、`DoesAxisExist()` 四条 action/axis 查询的返回值契约。已确认 `Documents/Plans/` 当前没有承接 speech mapping 收缩的活跃 Plan，`Plan_TestCoverageExpansion.md` 也只负责广义补测，不覆盖这里的 public surface 收口。`Documents/AutoPlans/DiscoveryPlans/BindSystem_Plan.md` 当前工作区仍不存在，因此本轮新增条目仅使用已验证的 [A] / [C] 证据链。

### Phase 2 深化：收缩已废弃的输入配置 speech surface

- [ ] **P2.29** 从 `Bind_UInputSettings.cpp` 移除 deprecated speech mapping public surface，并停止用 warning suppression 维持旧 contract
  - 当前 `Bind_UInputSettings.cpp` 不是被动继承了引擎弃用，而是显式用两段 `PRAGMA_DISABLE_DEPRECATION_WARNINGS` 把 `GetSpeechMappings()` / `DoesSpeechExist()` 继续注册给脚本。既然分析已经确认 UEAS2 同位点不再保留这组 API，current 继续公开它只会让输入配置 surface 维持“双轨”：一边在 `P2.15` 里继续加固 action/axis 查询 contract，另一边又保留上游已经放弃的 speech family。
  - 这一项应先做 public surface 收缩，而不是继续给 deprecated speech helper 补 trait：删除两条 speech mapping `Method(...)` 注册与对应的 warning suppression；`P2.15` 后续仅对 `GetUniqueActionName()`、`GetUniqueAxisName()`、`DoesActionExist()`、`DoesAxisExist()` 四条仍受支持的查询保留 `no_discard` / compile-contract。若团队确实还需要迁移缓冲，只能通过显式 `__Deprecated_*` internal gate 或文档迁移说明承接，不能继续占用正常 public 名字。
  - 来源：
    - [A] `Documents/AutoPlans/BindSystem_Analysis.md` — “发现 56：`UInputSettings` 重新暴露已废弃的 speech mapping API，并用编译器 warning 压制来维持旧 surface”
    - [C] `Documents/AutoPlans/TestCoverage/BindSystem_TestGaps.md` — “`Bind_UInputSettings.cpp` 仍位于当前未见 direct-hit bind 清单，说明这组已废弃入口目前没有 dedicated regression owner”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UInputSettings.cpp` L13-L20 — 当前仍以 `PRAGMA_DISABLE_DEPRECATION_WARNINGS` 包住 `GetSpeechMappings()` / `DoesSpeechExist()` 的公开注册
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UInputSettings.cpp` L11-L12、L17-L18 — 同文件 action/axis surface 是正常注册，speech family 是额外保留下来的 deprecated 分支
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UInputSettings.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptInputSettingsBindingsTests.cpp`
- [ ] **P2.29** 📦 Git 提交：`[AngelscriptRuntime] Refactor: remove deprecated speech mappings from input settings binds`
- [ ] **P2.29-T** 单元测试：为 `UInputSettings` 的 supported/deprecated surface 建立分界回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptInputSettingsBindingsTests.cpp`
  - 测试场景：
    - 正常路径：`GetActionMappings()`、`GetAxisMappings()`、`GetUniqueActionName()`、`GetUniqueAxisName()`、`DoesActionExist()`、`DoesAxisExist()` 继续可编译执行，且与原生 `UInputSettings` 基线一致
    - 边界条件：若 `P2.15` 已落地，supported action/axis 查询 helper 继续具备既定 `no_discard` / compile-contract；本项不能误伤这些仍受支持的查询入口
    - 错误路径：普通脚本模块解析 `GetSpeechMappings()`、`DoesSpeechExist(...)` 时必须直接 compile fail；若临时保留 `__Deprecated_*` internal gate，普通 surface 也不得再解析到旧名字
  - 测试命名：`Angelscript.TestModule.Bindings.InputSettingsSupportedQueriesRemain`，`Angelscript.TestModule.Bindings.InputSettingsSpeechSurfaceRemoved`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`
- [ ] **P2.29-T** 📦 Git 提交：`[AngelscriptTest] Test: cover input settings speech surface removal`

### 单元测试总览增补 (2026-04-09 07:27:24)

| 改进项 | 测试文件 | 测试重点 | 优先级 |
|--------|---------|---------|--------|
| `P2.29` | `Bindings/AngelscriptInputSettingsBindingsTests.cpp` | `UInputSettings` supported query 仍可用、speech mapping 公开入口移除、与 `P2.15` 的 action/axis contract 不冲突 | P1 |

### 验收标准增补 (2026-04-09 07:27:24)

1. `Bind_UInputSettings.cpp` 不再以公开名字注册 `GetSpeechMappings()` / `DoesSpeechExist()`，对应的 `PRAGMA_DISABLE_DEPRECATION_WARNINGS` 也随之移除。
2. `P2.15` 的适用范围被明确收口到 action/axis 查询 helper，不再继续把 speech mapping family 当成 current 应保留 surface。
3. `UInputSettings` 的 action/axis 查询入口在 speech surface 收缩后仍保持可编译、可执行、可与原生基线对账。

### 风险与注意事项增补 (2026-04-09 07:27:24)

#### 风险

1. `P2.29` 会把当前仍在使用 `GetSpeechMappings()` / `DoesSpeechExist()` 的脚本，从“继续编译但踩在 deprecated API 上”前移成编译期失败。
   - 缓解：测试同时锁住 supported action/axis surface 仍可用，并在实现说明中把迁移目标明确为 action/axis 输入配置，不再让 speech family 继续伪装成正式 public contract。
2. 若 `P2.29` 与 `P2.15` 执行顺序颠倒，容易再次把 `DoesSpeechExist()` 当成需要补 `no_discard` 的 supported helper。
   - 缓解：把本条作为 `P2.15` 的范围修正前置，并在 `AngelscriptInputSettingsBindingsTests.cpp` 里同时锁住“supported 继续存在、speech 公开名字消失”这两个断言。

#### 已知行为变化

1. `UInputSettings.GetSpeechMappings()` 与 `UInputSettings.DoesSpeechExist()` 将不再属于正常 public script surface；继续调用它们的脚本会在编译期暴露出来。
2. `P2.15` 后续只针对 action/axis 查询 helper 维持 `no_discard` / compile-contract，不再把 speech mapping family 继续留在 supported surface 中。

---
## 深化 (2026-04-09 07:33:55)

本轮只追加现有 `Plan_BindSystem.md` 尚未承接的三条 manual value bind behavior-owner 缺口：`FGuid`、`FDateTime`、`FTimespan`。已确认不与 `Documents/Plans/Plan_TestCoverageExpansion.md` 重复：该计划在 `P5.4` 只承接 `Bind_FDateTime.cpp` / `Bind_FTimespan.cpp` 的杂项编译冒烟，不覆盖本轮的 deterministic parse/format/operator contract；`Documents/Plans/` 当前也未见 `FGuid` 专项活跃 Plan。

### Phase 2 深化：把弱断言的 value bind 从 compat smoke 升级成 deterministic behavior owner

- [ ] **P2.30** 把 `GuidCompat` 从 core-misc 烟雾测试拆成 dedicated contract owner，收口 `Parse` / `ParseExact` / string ctor / `opIndex` 行为
  - 当前 `Bind_FGuid.cpp` 暴露的 surface 已经不只是一条“能不能 round-trip”的 happy path，而是包含 string ctor、两条 parse、format-sensitive `ParseExact`、以及四段 `opIndex()` 槽位读取。现有 `GuidCompat` 却只验证成功路径，并把 `GetTypeHash() != 0` 这种实现细节当成正确性断言，既锁不住失败语义，也会把无关 hash 变化误判成绑定回归。
  - 这一项不改 runtime bind 本体，先把测试 owner 做实：从 `AngelscriptCoreMiscBindingsTests.cpp` 中拆出 `FGuid` 专项文件，保留最小 smoke 后，把真正的行为合同固定到“invalid parse 不改写输出哨兵值、wrong-format `ParseExact` 必须失败、string ctor 与 `opIndex` 逐槽位可对账”。
  - 来源：
    - [C] `Documents/AutoPlans/TestCoverage/BindSystem_TestGaps.md` — “Issue-82 / NewTest-53：`GuidCompat` 只锁 happy path，`Parse`/`ParseExact` 失败语义、string ctor 与 `opIndex` 仍是空白”
    - [D] `Documents/AutoPlans/ArchitectureReview/BindingPipeline_ArchReview.md` — “Arch-BP-2：manual value/container/wrapper bind 目前没有一条自动化链能明确说明‘手写 binder 覆盖了哪些 contract’”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FGuid.cpp` L27-L31 — `FGuid(const FString&)` 仍是公开 constructor，现有 Plan 尚无对应行为 owner
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FGuid.cpp` L51-L76 — `opIndex()`、`ToString(EGuidFormats)`、`Parse()`、`ParseExact()` 都仍是脚本 surface 的一部分
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCoreMiscBindingsTests.cpp` L37-L71 — 当前 `GuidCompat` 只验证成功 round-trip，并以 `GetTypeHash() == 0` 作为失败条件，未覆盖 invalid parse / wrong-format / string ctor / indexer
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FGuid.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCoreMiscBindingsTests.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGuidBindingsTests.cpp`
- [ ] **P2.30** 📦 Git 提交：`[AngelscriptTest] Refactor: isolate FGuid binding behavior ownership`
- [ ] **P2.30-T** 单元测试：为 `FGuid` 建立 parse / format / index 的 deterministic 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGuidBindingsTests.cpp`
  - 测试场景：
    - 正常路径：`FGuid(GuidString)`、`FGuid::Parse(GuidString, OutGuid)`、`FGuid::ParseExact(GuidString, EGuidFormats::DigitsWithHyphens, OutGuid)`、`ToString(EGuidFormats::Digits)` / `ToString(EGuidFormats::DigitsWithHyphens)`、`Guid[0..3]` 都与 C++ 原生 `FGuid(1, 2, 3, 4)` 基线一致
    - 边界条件：`Invalidate()` / `IsValid()`、`FGuid::NewGuid()` 与默认 guid 的区别、以及 `Digits` / `DigitsWithHyphens` 两种 canonical format 都保持稳定 round-trip
    - 错误路径：`Parse("not-a-guid", OutGuid)`、`ParseExact(GuidString, EGuidFormats::Digits, OutGuid)` 都必须返回 `false` 且保持哨兵 `OutGuid` 不变；若 future regression 再次移除 string ctor 或 `opIndex()`，compile smoke 必须在调用点直接红灯
  - 测试命名：`Angelscript.TestModule.Bindings.GuidParseFormatAndIndexContracts`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`
- [ ] **P2.30-T** 📦 Git 提交：`[AngelscriptTest] Test: cover guid parse format and index contracts`

- [ ] **P2.31** 把 `DateTimeCompat` 从宿主时钟烟雾测试拆成固定输入的 parse / format / out-param contract owner
  - 当前 `Bind_FDateTime.cpp` 暴露了 `GetDate(out...)`、`ToHttpDate()`、`ToIso8601()`、`ToString(const FString&)`、`Parse()`、`ParseHttpDate()`、`ParseIso8601()` 等一整组 deterministic surface，但现有 `DateTimeCompat` 只做“字符串非空”与“当前年份大于 2020”这类宽松断言，等于把最容易在 lambda 转发和 out-param 写回上出错的路径全部留在假覆盖状态。
  - 这一项不和 `Plan_TestCoverageExpansion.md` 的编译冒烟重复，而是把 `FDateTime` 从 `Compat` 混合文件里拆成专用行为 owner：固定使用可复算的 ISO / HTTP / format 字符串和闰年样本，不再让 `Now()` / `UtcNow()` 的宿主机时钟状态主导回归结果。
  - 来源：
    - [C] `Documents/AutoPlans/TestCoverage/BindSystem_TestGaps.md` — “Issue-76 / NewTest-46：`DateTimeCompat` 只做非空和年份阈值断言，缺 parse / http / iso8601 round-trip 与 out 参数语义”
    - [D] `Documents/AutoPlans/ArchitectureReview/BindingPipeline_ArchReview.md` — “Arch-BP-2：manual value bind 没有显式行为账本，现有自动化无法说明具体 contract 是否真的被 owner 接住”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FDateTime.cpp` L21-L21 — `GetDate(int& OutYear, int& OutMonth, int& OutDay)` 仍是公开 out-param surface
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FDateTime.cpp` L37-L48 — `ToHttpDate()`、`ToIso8601()`、`ToString(const FString&)` 仍直接暴露给脚本
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FDateTime.cpp` L83-L90 — `Parse()`、`ParseHttpDate()`、`ParseIso8601()` 全部仍是脚本全局函数
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCompatBindingsTests.cpp` L289-L356 — 当前 `DateTimeCompat` 只检查 `ToIso8601()/ToString()` 非空，并用 `Now()/UtcNow()` 的年份阈值代替 deterministic parity
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FDateTime.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCompatBindingsTests.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptDateTimeBindingsTests.cpp`
- [ ] **P2.31** 📦 Git 提交：`[AngelscriptTest] Refactor: split DateTime behavior coverage from compat smoke`
- [ ] **P2.31-T** 单元测试：为 `FDateTime` 建立 deterministic parse / format / out-param 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptDateTimeBindingsTests.cpp`
  - 测试场景：
    - 正常路径：固定输入 `"2024-12-25T14:30:15Z"`、`"Wed, 25 Dec 2024 14:30:15 GMT"`、以及 `FDateTime(2024, 12, 25, 14, 30, 15, 123)` 的 `ParseIso8601`、`ParseHttpDate`、`Parse`、`GetDate(out...)`、`GetTicks()`、`ToHttpDate()`、`ToIso8601()`、`ToString("%%Y-%%m-%%d")` 都与 C++ 原生基线逐项一致
    - 边界条件：闰年 `2024-02-29`、`Today()` 的零点语义、以及 `GetHour12()` / `GetMillisecond()` 这类易被遗漏的 accessor 都必须在固定样本上可重复对账
    - 错误路径：非法 ISO / HTTP / plain datetime 字符串都必须返回 `false` 且保持 `OutDateTime` 哨兵值不变；若 future regression 再次丢失 `ParseIso8601` 或 `ToString(format)` surface，compile smoke 必须在对应调用点直接红灯
  - 测试命名：`Angelscript.TestModule.Bindings.DateTimeDeterministicParseAndFormat`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`
- [ ] **P2.31-T** 📦 Git 提交：`[AngelscriptTest] Test: cover deterministic datetime parse and format contracts`

- [ ] **P2.32** 把 `TimespanCompat` 从单条 happy-path 用例拆成 advanced operator / fraction / ticks contract owner
  - 当前 `Bind_FTimespan.cpp` 已公开 `int64 Ticks` ctor、四/五参数 ctor、一元负号、`+=/-=/\*=//=/%=`, `Ratio()`、`GetDuration()`、`GetFraction*()`、`GetTicks()`、`ToString(const FString)` 等大量 surface，但现有 `TimespanCompat` 只覆盖 `Zero()`、基础算术和 `ToString().IsEmpty()`。这会把最容易在 overload 转发、自定义 `ToString(format)` lambda 和边界值上回退的路径长期藏在“测试名看起来很全、断言其实很少”的灰区。
  - 这一项与 `Plan_TestCoverageExpansion.md` 的 compile smoke 分工明确：前者只证明“能编译能跑”，这里专门承接 `FTimespan` 的 deterministic operator/fraction contract，并把当前混在 `Compat` 文件里的弱断言拆走。
  - 来源：
    - [C] `Documents/AutoPlans/TestCoverage/BindSystem_TestGaps.md` — “Issue-77 / NewTest-47：`TimespanCompat` 仍停留在少量 happy path，ticks ctor、fraction getters、mod/ratio、assign operators、min/max 和 format overload 都无 owner”
    - [D] `Documents/AutoPlans/ArchitectureReview/BindingPipeline_ArchReview.md` — “Arch-BP-2：手写 value bind 的 contract 当前无法从自动化链直接看出，必须补 focused behavior owner”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FTimespan.cpp` L11-L36 — `int64 Ticks` ctor 与四/五参数 ctor 仍是公开 constructor
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FTimespan.cpp` L42-L53 — 一元负号、`-=`、`*=`、`/=`、`%=` 等高级算子仍是脚本 surface
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FTimespan.cpp` L69-L79、L104-L110 — `GetDuration()`、`GetFraction*()`、`GetTicks()`、`Ratio()`、`ToString(const FString)` 全部仍对外可用
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCompatBindingsTests.cpp` L196-L249 — 当前 `TimespanCompat` 只覆盖 `Zero()`、基础算术和 `ToString().IsEmpty()`，没有任何 ticks / fraction / ratio / format 断言
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FTimespan.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCompatBindingsTests.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptTimespanBindingsTests.cpp`
- [ ] **P2.32** 📦 Git 提交：`[AngelscriptTest] Refactor: split Timespan behavior coverage from compat smoke`
- [ ] **P2.32-T** 单元测试：为 `FTimespan` 建立 advanced operator / fraction / format 的 deterministic 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptTimespanBindingsTests.cpp`
  - 测试场景：
    - 正常路径：`FTimespan(900000000)`、`FTimespan(1, 2, 3, 4, 500000000)`、`Ratio()`、`GetTicks()`、`GetFractionMicro/Milli/Nano/Ticks()`、`GetDuration()`、`ToString("%d.%h:%m:%s")`、`%` / `%=` 与 C++ 原生基线逐项一致
    - 边界条件：`Zero()`、`MinValue()`、`MaxValue()`、一元负号与 `GetDuration()` 的组合，以及 `+=/-=/\*=//=` 后的逐步 ticks 变化都稳定可复算
    - 错误路径：若 future regression 再次丢失 ticks ctor、`Ratio()`、`ToString(const FString)` 或 assign-operator surface，compile smoke 必须在对应调用点直接红灯，而不是继续被宽松 happy-path 掩盖
  - 测试命名：`Angelscript.TestModule.Bindings.TimespanAdvancedOperatorsAndFormatting`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`
- [ ] **P2.32-T** 📦 Git 提交：`[AngelscriptTest] Test: cover timespan advanced operator and format contracts`

### 单元测试总览增补 (2026-04-09 07:33:55)

| 改进项 | 测试文件 | 测试重点 | 优先级 |
|--------|---------|---------|--------|
| `P2.30` | `Bindings/AngelscriptGuidBindingsTests.cpp` | `FGuid` invalid parse、wrong-format `ParseExact`、string ctor、`opIndex`、去掉 `GetTypeHash()!=0` 伪断言 | P1 |
| `P2.31` | `Bindings/AngelscriptDateTimeBindingsTests.cpp` | `FDateTime` parse/http/iso round-trip、`GetDate(out...)`、fixed-format 输出、去宿主时钟依赖 | P1 |
| `P2.32` | `Bindings/AngelscriptTimespanBindingsTests.cpp` | `FTimespan` ticks ctor、fraction getters、`Ratio()`、assign operators、`ToString(format)` | P1 |

### 验收标准增补 (2026-04-09 07:33:55)

1. `GuidCompat` 不再依赖 `GetTypeHash() != 0` 这类实现细节；`FGuid` 的 invalid parse、wrong-format `ParseExact`、string ctor 与 `opIndex` 都有独立 deterministic owner。
2. `DateTimeCompat` 的宿主时钟阈值断言被迁出核心行为 owner；`FDateTime` 的 `Parse*`、`GetDate(out...)`、`ToHttpDate()`、`ToIso8601()`、`ToString(format)` 都以固定输入与原生基线对账。
3. `TimespanCompat` 不再只代表少量 happy path；`FTimespan` 的 ticks/fraction/ratio/assign-operator/format surface 都有 dedicated regression owner，且与 compile smoke 分层明确。

### 风险与注意事项增补 (2026-04-09 07:33:55)

#### 风险

1. 把 `FGuid`、`FDateTime`、`FTimespan` 从共享 compat 文件拆到 dedicated owner 后，如果迁移顺序处理不好，短期可能出现旧 smoke 与新 owner 重复覆盖或真空区。
   - 缓解：同一轮提交里完成“拆文件 + 补新断言 + 保留最小 smoke 或显式移除旧断言”，不要让旧文件先删、专项 owner 后补。
2. `FDateTime` 若继续在行为 owner 中保留 `Now()` / `UtcNow()` 的硬阈值判断，仍会把宿主机时间状态引入自动化。
   - 缓解：dedicated owner 只保留固定输入和同一时刻 native baseline；若仍需保留 `Now()` / `UtcNow()` 冒烟，把它们放回广义 smoke，而不是 deterministic contract。
3. `FGuid` / `FTimespan` 的格式化与索引断言一旦直接硬编码预期文本，未来很容易把“测试样本写错”误判成绑定回归。
   - 缓解：所有期望值都优先在 C++ 侧用原生 API 预计算，再注入脚本对账；不要在脚本里手写第二套常量真值。

#### 已知行为变化

1. `FGuid` 的绑定回归不再把 `GetTypeHash() != 0` 当成正确性条件；后续失败信号会集中到 parse/format/index 实际合同。
2. `FDateTime` 的 dedicated owner 将从“字符串非空 / 年份阈值”转成固定输入下的精确 parse/format/out-param 对账。
3. `FTimespan` 的 dedicated owner 将把 ticks/fraction/ratio/format 这组高级 surface 从当前混合 smoke 中单独抽出，并用 deterministic 断言锁住。

---
## 深化 (2026-04-09 07:46:16)

本轮只追加 `FFormatArgumentValue`、`TSubclassOf` 负向 contract 与 wrapper descriptor 试点三项；已再次确认不与 `Documents/Plans/Plan_TestCoverageExpansion.md`、`Documents/Plans/Plan_AS238ForeachPort.md`、`Documents/Plans/Plan_AS238NonLambdaPort.md` 重复：前者当前聚焦 `FTransform/FQuat` 等值类型 broad coverage，后两者分别承接 foreach 语法主线与 `TOptional/TStructType` 非 lambda 类型系统主线。`Documents/AutoPlans/DiscoveryPlans/BindSystem_Plan.md` 当前工作区仍不存在，因此本轮新增条目只使用已核实的 [C]/[D]/[E] 证据链。

### Phase 2 深化：补齐 text / wrapper bind 仍缺失的 dedicated contract owner

- [ ] **P2.33** 把 `Bind_FFormatArgumentValue.cpp` 从零测试状态提升为 ordered / named text-format 的 dedicated behavior owner
  - current 仍通过 8 条 constructor overload 把 `FFormatArgumentValue` 作为 `FText::Format` 的基础桥接值类型暴露给脚本，其中 `FText` 与 `ETextGender` 两条 ctor 还是 late bind 才注册；但测试侧既没有任何 `FormatArgumentValue` 命中，也仍把基础值类型健康度混在 `ValueTypes` 的单个 smoke 里。这意味着一旦 numeric overload 宽化、`FText` / `ETextGender` late ctor 接错，问题不会在 binding 层第一时间暴露。
  - 这一项不改 runtime 行为，只先把 contract owner 建起来：新建 text-format 专项测试文件，用 C++ 原生 `FFormatArgumentValue` 预计算 ordered / named `FText::Format` 结果，再在脚本侧逐个显式调用 `int32/uint32/int64/uint64/float32/float64/FText/ETextGender` ctor，对齐输出文本而不是继续依赖混合 smoke。
  - 来源：
    - [C] `Documents/AutoPlans/TestCoverage/BindSystem_TestGaps.md` — “NewTest-48：`Bind_FFormatArgumentValue.cpp` 完全无直测，ordered / named format 构造器都没有 owner”
    - [D] `Documents/AutoPlans/ArchitectureReview/BindingPipeline_ArchReview.md` — “Arch-BP-2：manual value/container/wrapper bind 没有单一绑定合同来源；新增 value type 仍需要 dedicated owner 才能看清 contract”
    - [E] `Documents/AutoPlans/ReferenceComparison/Hazelight_Analysis.md` — “当前插件为了脱离引擎补丁，明确依赖 plugin-local wrapper + 自动化去兜住 tricky type 的稳定性”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FFormatArgumentValue.cpp` L22-L78 — numeric ctor 在 `Early` pass 逐条手写注册
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FFormatArgumentValue.cpp` L80-L97 — `FText` / `ETextGender` ctor 仍在 `Late` pass 追加，当前没有任何 bind-local 回归 owner
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptEngineBindingsTests.cpp` L33-L67 — `ValueTypes` 只覆盖 `FName/FVector/FRotator/FTransform/FText` 的混合 smoke，没有触达 `FFormatArgumentValue`
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FFormatArgumentValue.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptEngineBindingsTests.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptTextFormattingBindingsTests.cpp`
- [ ] **P2.33** 📦 Git 提交：`[AngelscriptTest] Refactor: add dedicated FFormatArgumentValue binding ownership`
- [ ] **P2.33-T** 单元测试：为 `FFormatArgumentValue` 建立 ordered / named format constructor 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptTextFormattingBindingsTests.cpp`
  - 测试场景：
    - 正常路径：用固定 `int32/uint32/int64/uint64/float32/float64/FText` 输入分别构造 `TArray<FFormatArgumentValue>` 与 `TMap<FString, FFormatArgumentValue>`，验证 ordered / named `FText::Format(...).ToString()` 与原生基线逐字一致
    - 边界条件：显式覆盖 `-7`、`42u`、`9000000000ll`、`15ull`、`3.25f`、`6.5` 这组易触发宽化/重载分派差异的样本；若工程支持 gender-aware format，再补 `ETextGender` 对照
    - 错误路径：为 `FFormatArgumentValue(FText)`、`FFormatArgumentValue(ETextGender)`、`FFormatArgumentValue(uint64)` 各补显式 ctor 调用点的 compile smoke；若 future regression 再次丢失 late ctor 或把宽位整数错误收束到别的 overload，必须在构造点直接红灯
  - 测试命名：`Angelscript.TestModule.Bindings.FormatArgumentValueOrderedAndNamedFormat`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`
- [ ] **P2.33-T** 📦 Git 提交：`[AngelscriptTest] Test: cover ordered and named FFormatArgumentValue constructors`

- [ ] **P2.34** 把 `TSubclassOf` 的模板子类拒绝语义从 happy-path compat 拆成 dedicated negative-contract owner
  - `TSubclassOfCompat` 现在只验证合法 `AActor/ACameraActor` 输入；但真正最脆弱的 bind 行为在 `Bind_TSubclassOf.h` 的 `ImplicitConstruct()` / `SetClass()`：无关 `UClass` 进入时必须 `Throw("Class set to TSubclassOf<> was not a child of templated class.")`，并把值重置为 `nullptr`。这条唯一显式错误路径如果继续没有 owner，后续 wrapper 重构或模板回查改动时最容易悄悄回退。
  - 这一项应把 `TSubclassOf` 从“类绑定兼容 smoke 的一个分支”拆成 focused owner：单独建立 `TSubclassOfRejectsUnrelatedClass` regression，显式覆盖 `ImplicitConstructor(UClass)` 与 `opAssign(UClass)` 的非法输入，并把清空语义、`GetDefaultObject()==null`、合法 narrowing path 一并拉到同一文件，避免负向 contract 继续被混在 class lookup / static type 回归里。
  - 来源：
    - [C] `Documents/AutoPlans/TestCoverage/BindSystem_TestGaps.md` — “Issue-75 / NewTest-43：`TSubclassOfCompat` 完全跳过模板子类校验失败分支”
    - [D] `Documents/AutoPlans/ArchitectureReview/BindingPipeline_ArchReview.md` — “Arch-BP-12：wrapper/value 类型仍是 copy-expand；在迁 descriptor 之前要先把负向 contract 锁住”
    - [E] `Documents/AutoPlans/ReferenceComparison/Hazelight_Analysis.md` — “当前插件无法依赖引擎补丁，只能靠 plugin-local wrapper 与自动化把 wrapper contract 稳住”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSubclassOf.h` L33-L56 — `ImplicitConstruct()` 在模板不匹配时显式 `Throw(...)` 并 `new (...)(nullptr)`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSubclassOf.h` L60-L100 — `SetClass()` 同样在非法 `UClass` 上抛错并清空
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp` L1761-L1772 — `ImplicitConstructor("void f(UClass Class)")`、`Set(UClass Class)`、`opAssign(UClass Class)` 仍公开挂到脚本 surface
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp` L98-L160 — current `TSubclassOfCompat` 只覆盖合法输入与 `GetDefaultObject()` happy path，没有任何 unrelated-class 负向断言
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSubclassOf.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptSubclassOfBindingsTests.cpp`
- [ ] **P2.34** 📦 Git 提交：`[AngelscriptTest] Refactor: isolate TSubclassOf rejection contracts`
- [ ] **P2.34-T** 单元测试：为 `TSubclassOf` 建立 unrelated-class rejection 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptSubclassOfBindingsTests.cpp`
  - 测试场景：
    - 正常路径：`TSubclassOf<AActor>` 的空值、`AActor::StaticClass()`、`ACameraActor::StaticClass()`、copy / `GetDefaultObject()` 继续与 current 基线一致
    - 边界条件：`Value = null` 后 `!Value.IsValid()`、`Value.Get() == null`、`Value.GetDefaultObject() == null`；合法 narrowing 后 `IsChildOf(AActor::StaticClass())` 仍为真
    - 错误路径：`TSubclassOf<AActor> Invalid(UPackage::StaticClass())` 与 `Value = UPackage::StaticClass()` 都必须命中 `Class set to TSubclassOf<> was not a child of templated class.`，并在失败后保持空值而不是静默接受无关 `UClass`
  - 测试命名：`Angelscript.TestModule.Bindings.TSubclassOfRejectsUnrelatedClass`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + `AddExpectedError(...)`
- [ ] **P2.34-T** 📦 Git 提交：`[AngelscriptTest] Test: cover TSubclassOf unrelated-class rejection`

### Phase 3 深化：为 object-wrapper family 补一条可迁移的 descriptor 主干

- [ ] **P3.8** 以 `TemplateTypeAdapterDescriptor` 试点收口 `TSubclassOf` / `TObjectPtr` / `TWeakObjectPtr` 的 copy-expand wiring
  - 现阶段 `Bind_BlueprintType.cpp` 里的三条 wrapper family 仍是“各写一套 declaration + `FAngelscriptType` + late bind + type registration + type finder，再到业务 binder 手工补 output-type metadata”的复制展开结构。只要再加一个 wrapper family，维护者就得在 5 个接入面重复同一轮布线；这既是 `Arch-BP-12` 的直接命中点，也会把后续 `TSoft*` / `TScriptInterface` 等 family 的接入成本继续放大。
  - 这一项不要求一次性重写所有 wrapper。第一阶段只新增 `TemplateTypeAdapterDescriptor` / `RegisterTemplateObjectWrapper(...)` 兼容层，把 family identity、declaration bind、late bind、type registration、`RegisterTypeFinder()` 挂接、以及 `ArgumentDeterminesOutputType` sidecar 从散落点收回同一描述对象；先让 `TSubclassOf` 走 descriptor 主路径，再让 `TObjectPtr` / `TWeakObjectPtr` 迁到同一 registration scaffold，同时保留现有 `FAngelscriptType` 语义实现和脚本声明不变。
  - 这条试点应明确只解决“wiring 重复 + contract owner 分散”，不趁机改 wrapper runtime semantics；真正的收益是后续新增 family 时不再先复制一整屏 `Bind_BlueprintType.cpp` 模板。
  - 来源：
    - [C] `Documents/AutoPlans/TestCoverage/BindSystem_TestGaps.md` — “Issue-72 / Issue-75：current object-wrapper 测试仍停留在 happy-path compat，说明 family 级 contract owner 还不稳定”
    - [D] `Documents/AutoPlans/ArchitectureReview/BindingPipeline_ArchReview.md` — “Arch-BP-12：`TSubclassOf` / `TObjectPtr` / `TWeakObjectPtr` 仍要跨 declaration、`FAngelscriptType`、late bind、type finder、factory metadata 五个面复制展开”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — “UnrealCSharp / UnLua 把 wrapper/property 映射集中在 generator 或 registry owner；current 若继续 copy-expand，会让 canonical wrapper surface 更难收口”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp` L1514-L1778 — `TSubclassOf` 仍独立维护 declaration、`FSubclassOfType` 与 late bind
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp` L1780-L2107 — `TObjectPtr` 以同形结构再复制一遍 object wrapper wiring
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp` L2109-L2418、L2424-L2440 — `TWeakObjectPtr` 再复制第三遍，并在 `BindUClassLookup()` 里手工 `Register()` 三个 type + `RegisterTypeFinder()`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp` L72、L579 — `NewObject(...)` 相关 output-type metadata 仍在业务 binder 侧手工补 `SetPreviousBindArgumentDeterminesOutputType(...)`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp` L453、L467 — actor wrapper 工厂同样在 family owner 之外手工补 output-type metadata
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptObjectBindingsTests.cpp` L21-L81 — `ObjectPtrCompat` 目前只锁 `TObjectPtr` happy path，没有 family-level wiring regression owner
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSubclassOf.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_WrapperTypeDescriptors.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_WrapperTypeDescriptors.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptObjectWrapperBindingsTests.cpp`
- [ ] **P3.8** 📦 Git 提交：`[AngelscriptRuntime] Refactor: add wrapper descriptor pilot for object wrapper families`
- [ ] **P3.8-T** 单元测试：为 descriptor 试点建立 wrapper family wiring 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptObjectWrapperBindingsTests.cpp`
  - 测试场景：
    - 正常路径：`TSubclassOf<AActor>`、`TObjectPtr<UTexture2D>`、`TWeakObjectPtr<AActor>` 在 descriptor 迁移后继续保持 ctor / assign / return / debugger-visible `Get()` / `IsValid()` 语义与迁移前一致
    - 边界条件：`CreateProperty` / `MatchesProperty` / `GetCppForm` 对 `FClassProperty`、`FObjectProperty`、`FWeakObjectProperty` 的映射保持稳定；generated wrapper 声明与 `RegisterTypeFinder()` 选择顺序不变
    - 错误路径：`TSubclassOf<AActor>` 的 unrelated-class rejection、stale weak object 的 invalid state、以及不存在 subtype 的 compile smoke 都必须继续在对应调用点直接红灯，不能被 descriptor 兼容层吞掉
  - 测试命名：`Angelscript.TestModule.Bindings.ObjectWrapperDescriptorRoundTrip`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + compile smoke for negative cases
- [ ] **P3.8-T** 📦 Git 提交：`[AngelscriptTest] Test: cover wrapper descriptor object-family parity`

### 单元测试总览增补 (2026-04-09 07:46:16)

| 改进项 | 测试文件 | 测试重点 | 优先级 |
|--------|---------|---------|--------|
| `P2.33` | `Bindings/AngelscriptTextFormattingBindingsTests.cpp` | `FFormatArgumentValue` ordered / named format、numeric/text/gender ctor 分派、late ctor compile smoke | P1 |
| `P2.34` | `Bindings/AngelscriptSubclassOfBindingsTests.cpp` | `TSubclassOf` null clear、implicit ctor / `opAssign(UClass)` 非法输入拒绝、expected error 文本 | P1 |
| `P3.8` | `Bindings/AngelscriptObjectWrapperBindingsTests.cpp` | `TSubclassOf/TObjectPtr/TWeakObjectPtr` descriptor wiring、type finder / `GetCppForm` 稳定性、negative cases 不回退 | P2 |

### 验收标准增补 (2026-04-09 07:46:16)

1. `Bind_FFormatArgumentValue.cpp` 的 8 条 ctor 不再处于零测试状态；ordered / named `FText::Format` 能把 signed/unsigned/64-bit/`FText`/`ETextGender` 样本逐项对齐到原生基线。
2. `TSubclassOf` 不再只有合法输入 happy path；`ImplicitConstruct(UClass)` 与 `opAssign(UClass)` 的 unrelated-class rejection、清空语义与错误文本都有 dedicated owner。
3. `TSubclassOf` / `TObjectPtr` / `TWeakObjectPtr` 的 declaration / late bind / type registration / type finder wiring 开始收口到 descriptor 试点，不再完全靠 `Bind_BlueprintType.cpp` 的复制展开维持。
4. descriptor 迁移后，现有 wrapper family 的 public script surface 与 runtime semantics 保持兼容，negative cases 仍能在 compile/runtime 回归里第一时间暴露。

### 风险与注意事项增补 (2026-04-09 07:46:16)

#### 风险

1. `FFormatArgumentValue` 的 `FText` / `ETextGender` 构造器依赖 late bind 与本地化格式化；如果测试直接写死字符串而不先用 C++ 原生 API 预计算，极易把本地化配置差异误判成 binding 回归。
   - 缓解：所有期望文本都由 C++ 侧同一时刻用原生 `FText::Format` 预计算，再注入脚本对账；不要在脚本里手写第二套格式化真值。
2. `TSubclassOf` 的负向 contract 依赖具体错误文本 `Class set to TSubclassOf<> was not a child of templated class.`；如果实现阶段顺手改文案，测试会出现“行为正确但文本漂移”的假红。
   - 缓解：除 `AddExpectedError` 外，再同时锁住失败后的 `!IsValid()`、`Get() == null`、`GetDefaultObject() == null`，让语义断言不只依赖字符串。
3. wrapper descriptor 试点一旦误改 `RegisterTypeFinder()` 顺序或 `SetPreviousBindArgumentDeterminesOutputType(...)` 的挂接时机，可能会让 `NewObject(...)`、actor/component wrapper 工厂或 `GetCppForm` 在非测试样本上回退。
   - 缓解：第一阶段只收口 wiring，不改 family-specific `FAngelscriptType` 语义；同时用 `TSubclassOf/TObjectPtr/TWeakObjectPtr` 三族 round-trip + negative cases 锁住迁移边界。

#### 已知行为变化

1. `FFormatArgumentValue` 将从“完全无 owner 的基础桥接值类型”升级成 dedicated text-format contract owner，后续失败信号会直接落在构造器分派或格式化结果，而不是被 `ValueTypes` 混合 smoke 吞掉。
2. `TSubclassOf` 的无关 `UClass` 赋值将被显式视为核心绑定 contract，而不是隐藏在 happy-path compat 之外的未命中分支。
3. `TSubclassOf/TObjectPtr/TWeakObjectPtr` 的内部注册 wiring 将开始从复制展开收口到 descriptor 试点；实现期允许内部结构变化，但 public script surface 与 negative contract 不应改变。

---

## 深化 (2026-04-09 08:02:16)

### 本轮追加边界

- `Documents/Plans/Plan_UFunctionReflectiveFallbackBinding.md` 已承接 direct / reflective / unresolved 的 runtime fallback 主线；本轮只补 fallback 之前的共享 eligibility / symbol / descriptor contract，不重复 fallback 状态机与统计口径。
- `Documents/Plans/Plan_CppInterfaceBinding.md`、`Documents/Plans/Plan_InterfaceBinding.md` 已承接 `UInterface` 能力闭环；本轮只为后续 interface / `Bind API GAP` 提供共享 `Decision / SymbolKey / Descriptor` 基础设施，不展开新的 interface surface。
- `Documents/Plans/Plan_BindParallelization.md` 已承接 bind phase / scheduler 主线；本轮不重开执行顺序或并行化，只处理跨 lane 的 identity 与 contract 对账问题。
- `Documents/AutoPlans/DiscoveryPlans/BindSystem_Plan.md` 当前工作区仍不存在，因此本轮新增条目仅使用已核实的 [D]/[E] 证据链。

### Phase 3 深化：收口 callable / symbol / descriptor 三条共享合同

- [ ] **P3.9** 把 editor / runtime / event / UHT 四条准入链收口到共享 `BindingEligibilityDecision`，先停止 `if` 链复制再谈 family 扩面
  - current 同一个 `UClass/UFunction` 是否进入 Angelscript surface，分别由 `GenerateBindDatabases()`、`ShouldBindEngineType()`、`ShouldSkipBlueprintCallableFunction()`、`BindBlueprintEvent()`、`ShouldGenerate()` 五处各自决定；同名 metadata 在不同 lane 的解释也不完全一致。继续在这种状态下推进 `Bind API GAP` 或 interface family，只会把“为什么没进来”继续埋在五套局部 `if` 里。
  - 这一项第一阶段只做 report-only 收口，不改现有准入结果：新增共享 `FAngelscriptBindingEligibilityDecision`、`EAngelscriptBindingLane`、`EAngelscriptEligibilityReasonCode`，让 editor 扫类、runtime callable、runtime event、UHT sidecar 都先经过同一份 helper，再把 lane-specific decision 与 `ReasonCode` 写到 `AS_FunctionTable_Entries.csv` / skipped ledger、bind dump、future manifest。像 `CustomThunk`、`AllowAngelscriptOverride`、`UActorComponent::GetOwner` 这类今天确实 lane-specific 的特例，可以继续保留差异，但差异必须变成显式 decision，而不是散落的 hardcode。
  - 来源：
    - [D] `Documents/AutoPlans/ArchitectureReview/BindingPipeline_ArchReview.md` — “Arch-BP-10：绑定准入规则分散在 editor / UHT / runtime，多条 lane 没有共享 eligibility policy”
    - [E] `Documents/AutoPlans/ReferenceComparison/GapAnalysis.md` — “`binding eligibility` authority 仍拆在四条 lane；第一阶段不要改行为，只把 `Decision + ReasonCode + Lane` 收口到共享 helper”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp` L1088-L1161 — `GenerateBindDatabases()` 仍以 abstract/deprecated、header 路径与 `BlueprintCallable` 为独立 class-scan 条件
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp` L962-L1026 — `ShouldBindEngineType()` 仍单独按 `CLASS_Native`、editor-only、`NotInAngelscript`、`BlueprintType`、`BlueprintCallable | BlueprintEvent` 再判一次 class eligibility
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp` L83-L117 — `ShouldSkipBlueprintCallableFunction()` 仍独立处理 `FUNC_Native`、`BlueprintInternalUseOnly`、`UsableInAngelscript` 与 `UActorComponent::GetOwner`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp` L553-L585 — event lane 仍单独处理 `DeprecatedFunction`、`BlueprintInternalUseOnly`、`AllowAngelscriptOverride` 与签名有效性
    - `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs` L490-L515 — UHT `ShouldGenerate()` 仍单独处理 `BlueprintCallable`、`NotInAngelscript`、`BlueprintInternalUseOnly`、`CustomThunk` 与 `UUniversalObjectLocatorScriptingExtensions` 特例
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindingEligibility.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindingEligibility.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp`
    - `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`
    - `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindingEligibilityTests.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp`
    - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptBlueprintCallableReflectiveFallbackTests.cpp`
- [ ] **P3.9** 📦 Git 提交：`[AngelscriptRuntime] Refactor: unify binding eligibility decisions across lanes`
- [ ] **P3.9-T** 单元测试：为共享 eligibility decision 建立跨 lane parity / reason-code 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindingEligibilityTests.cpp`
  - 测试场景：
    - 正常路径：对一组已知 `BlueprintCallable` / `BlueprintEvent` 样本同时导出 `EditorClassScan`、`RuntimeCallable`、`RuntimeEvent`、`UhtFunctionTable` decision，确认 `bIncluded`、`Lane`、`ReasonCode` 与 current 行为一致，且不再需要分别调用五套局部 helper 才能解释结果
    - 边界条件：显式覆盖 `BlueprintInternalUseOnly + UsableInAngelscript`、`AllowAngelscriptOverride`、`UserConstructionScript`、`UActorComponent::GetOwner`、`CustomThunk`、`UUniversalObjectLocatorScriptingExtensions::*` 这几类当前 hardcode 特例，确认共享 helper 能表达“lane-specific 但 reason 可追踪”
    - 错误路径：任何一个 lane 仍绕开共享 helper、写出空 `ReasonCode`、或对同一 symbol 给出和 golden snapshot 不一致的 decision 时，测试必须直接红灯，而不是继续靠肉眼比 `if` 链
  - 测试命名：`Angelscript.TestModule.Core.BindingEligibilityDecisionParity`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + test-only UHT summary fixture
- [ ] **P3.9-T** 📦 Git 提交：`[AngelscriptTest] Test: cover binding eligibility decision parity`

- [ ] **P3.10** 给 bind DB / function-table / runtime registry / skip config 增加 canonical `BindingSymbolKey`，停止继续依赖 name-only join
  - current 同一绑定元素在不同 lane 上的 identity 仍然是 family-specific 的：class/struct 多数靠 object path，method/property 在 DB 里却只留 `GetName()`，runtime direct-bind map 用 `UClass* + FString`，skip list 用 `(FName ClassName, FName FunctionName)`，UHT CSV 只写 `ClassName,FunctionName`。这会让 coverage、skip、dump、header-link、future provider merge 只能靠一堆 family-specific join 逻辑勉强对账。
  - 这一项第一阶段只做 dual-write，不删旧字段：新增 `FAngelscriptBindingSymbolKey`，至少覆盖 `OwnerObjectPath`、`MemberName`、`ScriptNameOrNamespace`、`Kind`、`SignatureHash`、`ProviderName`；`FAngelscriptMethodBind/PropertyBind/ClassBind`、`AddFunctionEntry()`、`SkipBindNames`、`AS_FunctionTable_Entries.csv`、future manifest 全部同时写出 canonical key。runtime 回放与工具链 audit 先优先消费新 key，旧 `UnrealPath` / name-only join 只保留为兼容 fallback。
  - 来源：
    - [D] `Documents/AutoPlans/ArchitectureReview/BindingPipeline_ArchReview.md` — “Arch-BP-9：绑定元素缺少统一稳定 key，cache、UHT 产物和 runtime registry 无法直接对齐”
    - [E] `Documents/AutoPlans/ReferenceComparison/GapAnalysis.md` — “首选插件内增量路线是在 `FBindInfo` 或并行 sidecar 中补 `SymbolKey`；当前还没有一个共享的 `SymbolKey + DecisionKind + ReasonCode + ProducerStage` 合同”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h` L9-L13、L38-L42、L56-L60、L90-L94 — `FAngelscriptPropertyBind`、`FAngelscriptStructBind`、`FAngelscriptMethodBind`、`FAngelscriptClassBind` 都复用 `UnrealPath`，但不同 family 的语义并不统一
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h` L379-L394 — method bind 写库时仍把 `DBBind.UnrealPath` 设成 `Function->GetName()`，没有 owner path / script-facing identity
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp` L712-L743、L1102-L1117 — class declaration 回放靠 `FindObject(..., *DBBind.UnrealPath)`，而 method/property 回放仍只靠 `FindFunctionByName(*DBFunc.UnrealPath)` / `Property->GetName()`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h` L497-L510、L539-L568 — runtime direct-bind map 仍是 `AddFunctionEntry(UClass*, FString Name, ...)`，skip list 仍是 `(FName ClassName, FName FunctionName)`
    - `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs` L37-L44、L128-L135、L244-L260 — `AS_FunctionTable_Entries.csv` 仍只输出 `ClassName`、`FunctionName`、`EntryKind`、`EraseMacro`、`ShardIndex`
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindingSymbolKey.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindingSymbolKey.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`
    - `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindingSymbolKeyTests.cpp`
- [ ] **P3.10** 📦 Git 提交：`[AngelscriptRuntime] Refactor: add canonical binding symbol keys across artifacts`
- [ ] **P3.10-T** 单元测试：为 `BindingSymbolKey` 建立 cross-artifact join / orphan audit 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindingSymbolKeyTests.cpp`
  - 测试场景：
    - 正常路径：抽样 `NewObject`、`SpawnActor`、一条 reflected `BlueprintCallable`、一条 generated getter/setter 或 namespace helper，确认 bind DB、runtime function-entry map、skip config、`AS_FunctionTable_Entries.csv` 都能导出同一个 canonical `BindingSymbolKey`
    - 边界条件：显式覆盖“脚本名不等于 Unreal 名”的样本、namespace/global helper、generated accessor、以及两个不同 owner 下同名 `FunctionName` 的情况，确认 key 不会再次退化成 `ClassName + FunctionName`
    - 错误路径：任一 lane 缺 key、重复 key、或只剩 name-only fallback 时，audit 必须给出 deterministic failure，而不是继续 silent join
  - 测试命名：`Angelscript.TestModule.Core.BindingSymbolKeyCrossArtifactJoin`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + test-only DB / CSV / manifest snapshot
- [ ] **P3.10-T** 📦 Git 提交：`[AngelscriptTest] Test: cover binding symbol key cross-artifact joins`

- [ ] **P3.11** 把 manual bind / runtime reflective signature / UHT generated signature 先 dual-write 成共享 `BindingFunctionDescriptor`
  - current 函数合同仍分成三套模型：manual bind 直接写 declaration string，再靠 `SetPreviousBindArgumentDeterminesOutputType()` 或 `PreviousBindPassScriptFunctionAsFirstParam()` 补 trait；runtime reflective lane 用 `FAngelscriptFunctionSignature` 承载 `ScriptName`、`WorldContext`、`DeterminesOutputType`、mixin 等 rich metadata；UHT `AngelscriptFunctionSignature` 则只保留 `OwningType/FunctionName/ReturnType/ParameterTypes/IsStatic/IsConst`。这会让 script-facing owner/name、receiver folding、world-context/output-type 语义无法在 manual/runtime/UHT 三条 lane 上直接对账。
  - 这一项第一阶段也只做 dual-write / validator，不强制重写所有 binder：新增 plain-data `FAngelscriptBindingFunctionDescriptor`，先让 `Helper_FunctionSignature.h` 把 current `FAngelscriptFunctionSignature` 转成 descriptor；manual binder 则为 `NewObject`、`SpawnActor`、一条 static `BlueprintCallable`、一条 `ScriptMethod/ScriptMixin` 样本补 builder API 或 sidecar descriptor；UHT `AngelscriptFunctionSignatureBuilder.cs` 改成生成同构字段，再由 parity validator 比较 `Declaration`、`ScriptName`、`OwnerType`、`WorldContext`、`DeterminesOutputType`、`GlobalScope`、`MixinTarget` 是否一致。
  - 来源：
    - [D] `Documents/AutoPlans/ArchitectureReview/BindingPipeline_ArchReview.md` — “Arch-BP-8：函数签名合同被拆成三套模型，自动化和校验没有共享 IR”
    - [E] `Documents/AutoPlans/ReferenceComparison/GapAnalysis.md` — “generated signature 只保留 raw source identity；当前 generated artifact 仍不知道脚本 owner、脚本函数名和 receiver folding”
  - 源码验证：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h` L16-L24、L33-L55 — runtime signature 当前承载 `ScriptName`、`WorldContext`、`DeterminesOutputType`、`ScriptMixin`、`bGlobalScope` 等 rich metadata
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h` L178-L333、L336-L342、L379-L394 — reflective lane 会构造 declaration / script-facing identity，但写回 bind DB 时仍只落 `Declaration`、`ClassName`、`ScriptName`、`WorldContextArgument`、`DeterminesOutputTypeArgument`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp` L61-L73 — `GetTypedOuter(...)` 仍是手写 declaration 后再调用 `SetPreviousBindArgumentDeterminesOutputType(0)`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp` L303-L312 — `Spawn(...)` 仍是手写 declaration 后再调用 `PreviousBindPassScriptFunctionAsFirstParam()`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp` L50-L55、L100-L150 — current 只有 reflective callable lane 真正复用 `FAngelscriptFunctionSignature`
    - `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs` L8-L15、L90-L97 — UHT generated signature 当前仍只保留 `OwningType`、`FunctionName`、`ReturnType`、`ParameterTypes`、`IsStatic`、`IsConst`
  - 涉及文件：
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindingFunctionDescriptor.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindingFunctionDescriptor.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`
    - `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`
    - `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs`
    - `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`
    - `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindingFunctionDescriptorTests.cpp`
- [ ] **P3.11** 📦 Git 提交：`[AngelscriptRuntime] Refactor: add shared binding function descriptors`
- [ ] **P3.11-T** 单元测试：为 manual / reflective / UHT 三条函数签名链建立 descriptor parity 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindingFunctionDescriptorTests.cpp`
  - 测试场景：
    - 正常路径：对 `NewObject`、`SpawnActor`、一条 static reflected `BlueprintCallable`、一条 `ScriptMethod/ScriptMixin` 样本同时生成 manual/runtime/UHT descriptor，确认 `Declaration`、`ScriptName`、`OwnerType`、`StaticInScript`、`GlobalScope`、`WorldContext`、`DeterminesOutputType` 一致
    - 边界条件：显式覆盖 receiver folding、`__WorldContext()` 默认化、`DeterminesOutputType` 下标修正、`no_discard/allow_discard`、deprecated / tooltip 元数据，确认 descriptor 不再只保留“能生成 `ERASE_*` 宏的最小子集”
    - 错误路径：任一 lane 丢失 script-facing owner/name、manual binder 忘记补 descriptor sidecar、或 UHT builder 仍输出 raw source identity 时，validator 必须直接报字段 diff 而不是继续 silent 通过
  - 测试命名：`Angelscript.TestModule.Core.BindingFunctionDescriptorParity`
  - 隔离方式：`FAngelscriptEngineScope` + `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + descriptor snapshot comparator
- [ ] **P3.11-T** 📦 Git 提交：`[AngelscriptTest] Test: cover binding function descriptor parity`

### 单元测试总览增补 (2026-04-09 08:02:16)

| 改进项 | 测试文件 | 测试重点 | 优先级 |
|--------|---------|---------|--------|
| `P3.9` | `Core/AngelscriptBindingEligibilityTests.cpp` | editor / runtime / event / UHT 四条 lane 的 `Decision + ReasonCode + Lane` 对账，保留 hardcode 特例但变成显式账本 | P1 |
| `P3.10` | `Core/AngelscriptBindingSymbolKeyTests.cpp` | bind DB / runtime registry / skip config / `AS_FunctionTable_Entries.csv` 共用 canonical `BindingSymbolKey`，消灭 name-only join | P1 |
| `P3.11` | `Core/AngelscriptBindingFunctionDescriptorTests.cpp` | manual / reflective / UHT descriptor parity，保住 script-facing owner/name、receiver folding、world-context/output-type 元数据 | P2 |

### 验收标准增补 (2026-04-09 08:02:16)

1. 同一个 `UClass/UFunction` 的准入结果不再只能靠五套局部 helper 猜测；至少 editor class scan、runtime callable、runtime event、UHT sidecar 都能导出稳定 `Lane + ReasonCode + Included` decision，且 current 特例行为不回退。
2. bind DB、runtime function-entry map、skip config、`AS_FunctionTable_Entries.csv` 至少对代表性 `Class / Property / Function / NamespaceHelper` 样本导出同一个 `BindingSymbolKey`；旧 `UnrealPath` / name-only join 只保留为兼容 fallback。
3. `NewObject`、`SpawnActor`、一条 static reflected callable、一条 `ScriptMethod/ScriptMixin` 样本可以在 manual/runtime/UHT 三条链上生成可对账的 `BindingFunctionDescriptor`，不再丢失 script-facing owner/name、receiver folding、`WorldContext`、`DeterminesOutputType`。
4. 第一阶段所有新增合同都保持 report-only / dual-write 属性，不改变现有 public script surface；真实行为收紧必须通过后续专门条目推进，而不是在这三项基础设施里顺手混入。

### 风险与注意事项增补 (2026-04-09 08:02:16)

#### 风险

1. `P3.9` 把五套准入 `if` 链收口到共享 helper 时，最容易出现“把本来就该 lane-specific 的差异强行抹平”或“某个特例顺序漂移导致准入回退”。
   - 缓解：第一阶段只迁 decision owner，不改结果；对 `BlueprintInternalUseOnly + UsableInAngelscript`、`AllowAngelscriptOverride`、`CustomThunk`、`GetOwner` 这些现有特例全部做 golden snapshot。
2. `P3.10` 的 canonical key 如果缺 `Kind`、`ScriptNameOrNamespace` 或 `SignatureHash`，很容易把 generated accessor、namespace helper、同名不同 owner 的函数错误聚合。
   - 缓解：首版 key 设计必须显式区分 `OwnerObjectPath + Kind + ScriptNameOrNamespace + SignatureHash`，并用跨产物 audit 先抓重名误并，不要等到 runtime lookup 才发现。
3. `P3.11` 需要 C++ runtime 与 C# UHT 共享 descriptor schema；如果字段含义在两边各自演化，最先出现的会是“都能编译、但 parity validator 永远红”的 schema drift。
   - 缓解：先选 `NewObject`、`SpawnActor`、一条 static callable、一条 `ScriptMethod/ScriptMixin` 四组样本做 dual-write，schema 未稳定前不要要求全仓 binder 一次性迁移。

#### 已知行为变化

1. 进入 report artifact 的 callable / type / generated row 将开始显式携带 `ReasonCode`、`Lane`、`SymbolKey` 与 descriptor parity 信息，排障入口会从“grep 多份产物”转成“直接查共享合同”。
2. `AS_FunctionTable_Entries.csv`、bind dump、future manifest 的字段会变得更接近 script-facing identity，而不再只是 raw `ClassName + FunctionName` 或 lane-specific 最小记录。
3. 第一阶段不会新增或删除任何 public script API，但会更早暴露“某条 lane 仍绕过共享 helper / key / descriptor”的结构性漂移；这类红灯应视为基础设施回归，而不是测试过严。

---

## 深化 (2026-04-09 08:11:49)

以下内容只细化现有 `P3.9`、`P3.10`、`P3.11` 的批量修改影响范围与落地顺序，不新增新的 Phase 编号；对应自动化仍沿用原条目 `P3.9-T`、`P3.10-T`、`P3.11-T`。

### 影响范围深化：`P3.9` `BindingEligibilityDecision`

| 切片 | 操作类型 | 受影响文件 | 当前分裂点（源码验证） | 关联来源 |
|------|----------|-----------|------------------------|---------|
| `Eligibility Producer / Editor` | 抽离 class-scan 准入 owner | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp` | `AngelscriptEditorModule.cpp` L1088-L1161 仍在 `GenerateBindDatabases()` 内单独处理 `CLASS_Abstract`、header path、`BlueprintCallable` 与 package 分组，没有共享 decision helper | [D] `BindingPipeline_ArchReview.md` `Arch-BP-10`；[E] `GapAnalysis.md` binding eligibility authority 分裂 |
| `Eligibility Producer / Runtime Class` | 抽离 reflected class 准入 owner | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp` | `Bind_BlueprintType.cpp` L962-L1026 仍独立按 `CLASS_Native`、editor-only、`BlueprintType` 与函数扫描决定是否 bind type | [D] `BindingPipeline_ArchReview.md` `Arch-BP-10`；[E] `GapAnalysis.md` lane-specific eligibility 未统一 |
| `Eligibility Producer / Runtime Callable` | 抽离 callable skip owner | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp` | `AngelscriptBinds.cpp` L83-L117 的 `ShouldSkipBlueprintCallableFunction()` 仍单独维护 `NotInAngelscript`、`BlueprintInternalUseOnly`、`UsableInAngelscript` 与 `UActorComponent::GetOwner` 特例 | [D] `BindingPipeline_ArchReview.md` `Arch-BP-10`；[E] `GapAnalysis.md` reason/decision 未共享 |
| `Eligibility Producer / Runtime Event` | 抽离 event override 准入 owner | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp` | `Bind_BlueprintEvent.cpp` L553-L585 仍在 bind 前单独处理 deprecated、`NotInAngelscript`、`BlueprintInternalUseOnly` 与 `AllowAngelscriptOverride` | [D] `BindingPipeline_ArchReview.md` `Arch-BP-10`；[E] `CrossComparison.md` callback authority 应落在统一 contract |
| `Eligibility Sink / UHT + Report` | 仅 dual-write decision，不改 runtime 结果 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs` | `AngelscriptFunctionTableCodeGenerator.cs` L37-L44 与 L250-L260 的 CSV 仍只有 `ClassName`、`FunctionName`、`EntryKind`、`EraseMacro`、`ShardIndex`，还没有 `Lane` / `ReasonCode` / `Included` | [D] `BindingPipeline_ArchReview.md` `Arch-BP-10`；[E] `GapAnalysis.md` 缺统一 decision ledger |

### 影响范围深化：`P3.10` `BindingSymbolKey`

| 切片 | 操作类型 | 受影响文件 | 当前 name-only / path-only 耦合点（源码验证） | 关联来源 |
|------|----------|-----------|---------------------------------------------|---------|
| `DB Schema Dual-Write` | 在 bind DB 侧补 canonical key，不删旧字段 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h` `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp` `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h` | `AngelscriptBindDatabase.h` L56-L87 的 `FAngelscriptMethodBind` 仍只有 `Declaration`、`UnrealPath`、`ClassName`、`ScriptName` 等字段；`Bind_BlueprintType.cpp` L714-L743 仍用 `Class->FindFunctionByName(*DBFunc.UnrealPath)` 回放；`Helper_FunctionSignature.h` L382-L393 仍把 `DBBind.UnrealPath = Function->GetName()` 写回数据库 | [D] `BindingPipeline_ArchReview.md` `Arch-BP-9`；[E] `GapAnalysis.md` 缺 canonical symbol identity |
| `Runtime Registry + Skip Config` | 为 live registry / skip ledger 补 key sidecar | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h` | `AngelscriptBinds.h` L497-L510 的 `AddFunctionEntry()` 仍以 `UClass* + FString Name` 为 live key；L514-L569 的 `SkipFunctionEntry()` / `AddSkipEntry()` 仍只记录 `FName ClassName` + `FName FunctionName` | [D] `BindingPipeline_ArchReview.md` `Arch-BP-9`；[E] `GapAnalysis.md` runtime registry 与 skip config 仍无法稳定 join |
| `Generated Artifact Identity` | 为 UHT sidecar 增加 dual-write key 列 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs` | `AngelscriptFunctionTableCodeGenerator.cs` L37-L44 的 CSV entry 仍只包含 `ModuleName`、`ClassName`、`FunctionName`、`EntryKind`、`EraseMacro`、`ShardIndex`；L250-L260 的 CSV 输出没有 owner path、script name 或 signature hash | [D] `BindingPipeline_ArchReview.md` `Arch-BP-9`；[E] `GapAnalysis.md` 需要 cross-artifact join key |

### 影响范围深化：`P3.11` `BindingFunctionDescriptor`

| 切片 | 操作类型 | 受影响文件 | 当前 descriptor 断裂点（源码验证） | 关联来源 |
|------|----------|-----------|----------------------------------|---------|
| `Runtime Rich Signature Source` | 先把 runtime rich metadata 抽成 plain-data descriptor | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h` | `Helper_FunctionSignature.h` L33-L55 的 `FAngelscriptFunctionSignature` 已持有 `ScriptName`、`WorldContextArgument`、`DeterminesOutputTypeArgument`、`bGlobalScope` 等 rich 字段；但 L382-L393 写 DB 时仍只落 `UnrealPath` / `WorldContextArgument` / `DeterminesOutputTypeArgument` / `ClassName` / `ScriptName`，L423-L429 仍把 hidden world-context 默认值内联写成 `__WorldContext()` | [D] `BindingPipeline_ArchReview.md` `Arch-BP-8`；[E] `GapAnalysis.md` generated signature 不知道 script-facing owner/name |
| `Manual Binder Pilot` | 只给高价值 manual binder 补 sidecar descriptor，不重写全仓 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp` `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp` | `Bind_UObject.cpp` L61-L73 的 `GetTypedOuter(...)` 仍是手写声明后再 `SetPreviousBindArgumentDeterminesOutputType(0)`；L557-L579 的 `NewObject(...)` 同样依赖 `SetPreviousBindArgumentDeterminesOutputType(1)`；`Bind_AActor.cpp` L303-L312 的 namespace `Spawn(...)` 仍通过 `PreviousBindPassScriptFunctionAsFirstParam()` 追加 receiver folding，L450-L467 的 `SpawnActor(...)` 仍通过 `SetPreviousBindArgumentDeterminesOutputType(0)` 追补 output-type | [D] `BindingPipeline_ArchReview.md` `Arch-BP-8`；[E] `GapAnalysis.md` manual lane 仍靠 post-bind side effects |
| `Reflective + UHT Parity` | 保持 reflective lane 与 UHT lane dual-write，不让任一边先成为 authority | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp` `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs` | `Bind_BlueprintCallable.cpp` L50-L55、L101-L150 仍消费完整 `FAngelscriptFunctionSignature` 再决定 global/namespace/member 绑定；`AngelscriptFunctionSignatureBuilder.cs` L8-L15、L90-L97 的 UHT record 仍只有 `OwningType`、`FunctionName`、`ReturnType`、`ParameterTypes`、`IsStatic`、`IsConst`、`UseExplicitSignature` 最小集合 | [D] `BindingPipeline_ArchReview.md` `Arch-BP-8`；[E] `GapAnalysis.md` UHT sidecar 仍只保留 raw source identity |

### 执行顺序深化：`P3.9` → `P3.10` → `P3.11`

1. `P3.9` 必须先落地成 `report-only` decision owner，再推进 `P3.10` 和 `P3.11`。原因是 `BindingSymbolKey` 与 `BindingFunctionDescriptor` 最终都要挂在稳定的 `Lane + ReasonCode + Included` 账本上，否则后续测试很难区分“符号 join 错了”还是“本来就不该进入该 lane”。
2. `P3.10` 只能做 `dual-write`，不能在第一批顺手删除 `UnrealPath`、`AddFunctionEntry(UClass*, FString Name, ...)`、`SkipBindNames(FName, FName)` 等旧 join 字段。原因是 `Bind_BlueprintType.cpp` L714-L743 与 `Helper_FunctionSignature.h` L382-L393 仍直接消费这些旧字段；先删只会把 key 迁移和回放回退混成同一类故障。
3. `P3.11` 要等 `P3.10` 的 key 稳定后再推进。descriptor parity 测试需要用稳定 `BindingSymbolKey` 把 manual / reflective / UHT 三条 lane 对到同一采样对象，否则 `NewObject`、`SpawnActor`、reflected callable 的 descriptor diff 会先败给 join 歧义。
4. 这三项不应并行修改 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`。它们是三项共享热点文件，若并行推进，最容易把 `ReasonCode`、`SymbolKey`、descriptor schema 三种变更搅成一次不可定位的回归。
5. 若使用分批执行或子代理，建议按写域拆分：
   - `P3.9` 写域：`AngelscriptEditorModule.cpp`、`AngelscriptBinds.cpp`、`Bind_BlueprintType.cpp`、`Bind_BlueprintEvent.cpp`、`AngelscriptFunctionTableCodeGenerator.cs`、`AngelscriptBindingEligibilityTests.cpp`
   - `P3.10` 写域：`AngelscriptBindDatabase.*`、`AngelscriptBinds.h`、`Helper_FunctionSignature.h`、`Bind_BlueprintType.cpp`、`AngelscriptFunctionTableCodeGenerator.cs`、`AngelscriptBindingSymbolKeyTests.cpp`
   - `P3.11` 写域：`Helper_FunctionSignature.h`、`Bind_UObject.cpp`、`Bind_AActor.cpp`、`Bind_BlueprintCallable.cpp`、`AngelscriptFunctionSignatureBuilder.cs`、`AngelscriptBindingFunctionDescriptorTests.cpp`
